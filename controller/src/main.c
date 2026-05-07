#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "bsp/board_api.h"
#include "hardware/sync.h"
#include "pico/time.h"
#include "tusb.h"

#include "usb_descriptors.h"

enum {
  SCRIPT_START_DELAY_FRAMES = 120,
  STARTUP_A_PRESS_COUNT = 5,
  STARTUP_B_PRESS_COUNT = 3,
  ACTIVATE_PRESS_FRAMES = 18,
  ACTIVATE_RELEASE_FRAMES = 28,
  POST_ACTIVATION_DELAY_FRAMES = 30,
  GAME_FRAME_RATE = 60,
  USB_SOF_US = 1000,
  DRAW_PREHOLD_FRAMES = 1,
  INPUT_TAP_FRAMES = 1,
  INPUT_WAIT_FRAMES = 1,
  HELPER_WAIT_FRAMES = 8,
  HOME_CANVAS_SETTLE_FRAMES = 11,
  STAMP_SHAPE_CONFIRM_WAIT_FRAMES = 80,
  ZOOM_REPEAT_COUNT = 9,
  ZOOM_WAIT_FRAMES = 48,
  SET_BRUSH_LEFT_COUNT = 2,
  CLEAR_UP_COUNT = 2,
  CLEAR_DOWN_COUNT = 2,
  TOP_LEFT_OUTLINE_MIN_PIXEL = -128,
  TOP_LEFT_OUTLINE_VERTICAL_COUNT = 1 + (-TOP_LEFT_OUTLINE_MIN_PIXEL),
  TOP_LEFT_OUTLINE_HORIZONTAL_COUNT = -TOP_LEFT_OUTLINE_MIN_PIXEL,
  TOP_LEFT_OUTLINE_TOTAL_COUNT =
      TOP_LEFT_OUTLINE_VERTICAL_COUNT + TOP_LEFT_OUTLINE_HORIZONTAL_COUNT,
  CURSOR_Q = 256,
  CURSOR_HALF_Q = CURSOR_Q / 2,
  CURSOR_FREE_SPEED_PIXELS_PER_FRAME = 5,
  CURSOR_FREE_SPEED_Q = CURSOR_FREE_SPEED_PIXELS_PER_FRAME * CURSOR_Q,
  CURSOR_DRAW_SPEED_PIXELS_PER_FRAME = 2,
  CURSOR_DRAW_SPEED_Q = CURSOR_DRAW_SPEED_PIXELS_PER_FRAME * CURSOR_Q,
  MOVEMENT_MAX_STEER_FRAMES = 512,
  CURSOR_FREE_POST_SETTLE_FRAMES = 3,
  AXIS_MIN_MOVING_OFFSET = 1,
  MAX_SCRIPT_STEPS = 2048,
  AXIS_MIN = 0x00,
  AXIS_CENTER = 0x80,
  AXIS_CURSOR_REMAINDER_ONE = 196,
  AXIS_CURSOR_REMAINDER_TWO = 218,
  AXIS_CURSOR_REMAINDER_THREE = 234,
  AXIS_CURSOR_REMAINDER_FOUR = 248,
  AXIS_MAX = 0xFF,
};

typedef enum {
  SCRIPT_PHASE_SETUP = 0,
  SCRIPT_PHASE_TOP_LEFT_OUTLINE_DOT_TEST,
  SCRIPT_PHASE_DONE,
} script_phase_t;

typedef enum {
  TOOL_UNDO = 0,
  TOOL_REDO,
  TOOL_MOVE,
  TOOL_SELECT,
  TOOL_TEXT_STAMP,
  TOOL_SHAPE,
  TOOL_BUCKET,
  TOOL_PEN,
  TOOL_ERASER,
  TOOL_DROPPER,
  TOOL_EFFECT,
  TOOL_SETTING,
  TOOL_QUIT,
  TOOL_COUNT,
} tool_id_t;

typedef struct __attribute__((packed)) {
  switch_controller_report_t report;
  uint16_t frames;
} frame_step_t;

typedef struct {
  uint16_t distance_q;
  uint8_t axis_offset;
} trim_sample_t;

static volatile uint32_t sof_ticks = 0;
static volatile uint32_t last_sof_time_us = 0;
static uint32_t last_report_sof = 0;
static bool last_report_valid = false;
static switch_controller_report_t last_report;
static frame_step_t script_steps[MAX_SCRIPT_STEPS];
static size_t script_step_count = 0;
static size_t script_step_index = 0;
static uint32_t script_step_start_frame = 0;
static bool script_overflowed = false;
static script_phase_t script_phase = SCRIPT_PHASE_SETUP;
static uint16_t script_test_step = 0;
static int16_t script_cursor_x = 0;
static int16_t script_cursor_y = 0;
static int32_t script_cursor_x_q = 0;
static int32_t script_cursor_y_q = 0;
static bool script_cursor_known = false;
static bool selected_tool_known = false;
static tool_id_t selected_tool = TOOL_PEN;

// These tables store cursor displacement per frame, not the painted pixel count.
// A mark on cursor boundary 0 lands in positive pixel 1, so measured dot/line
// counts must subtract that starting-cell artifact before becoming travel data.
static const trim_sample_t free_cursor_trim_samples[] = {
    {0, 0},    {9, 13},   {26, 19},   {43, 25},   {77, 32},
    {111, 38}, {154, 44}, {205, 51},  {256, 57},  {324, 64},
    {384, 70}, {452, 76}, {546, 83},  {623, 89},  {708, 95},
    {819, 102}, {922, 108}, {1024, 114}, {1161, 121}, {1280, 127},
};

// For example, a 120-frame full-speed draw painted 241 cells, but that is
// 240 cursor units of travel because the first painted cell is the start cell.
static const trim_sample_t draw_trim_samples[] = {
    {0, 0},    {4, 1},    {17, 3},   {23, 4},   {30, 5},   {34, 6},
    {49, 8},   {55, 9},   {62, 10},  {68, 11},  {81, 13},  {156, 25},
    {230, 38}, {301, 51}, {363, 64}, {407, 76}, {454, 89}, {486, 102},
    {503, 114}, {512, 127},
};

static void hid_task(void);
static switch_controller_report_t neutral_report(void);
static uint32_t disciplined_usb_time_us(void);
static uint32_t usb_time_to_frame(uint32_t now_usb_us);
static void build_script(void);
static void reset_script_cursor(void);
static void script_begin_chunk(void);
static bool script_generate_next_chunk(void);
static void script_add_report(switch_controller_report_t report, uint16_t frames);
static void script_wait_frames(uint16_t frames);
static void script_hold_buttons(uint16_t buttons, uint16_t hold_frames, uint16_t wait_frames);
static void script_tap_hat(uint8_t hat);
static void script_tap_hat_wait(uint8_t hat, uint16_t wait_frames);
static void script_tap_right_stick(uint8_t rx, uint8_t ry, uint16_t wait_frames);
static void script_focus_tool_cursor(tool_id_t target);
static void script_tool_change(tool_id_t target);
static void script_homing_tool_change(tool_id_t target);
static void script_set_brush(void);
static void script_clear(void);
static void script_home(void);
static void script_zoom_out(void);
static void script_move_cursor_right(uint16_t units);
static void script_move_cursor_up(uint16_t units);
static void script_move_cursor_down(uint16_t units);
static void script_move_cursor_left(uint16_t units);
static uint8_t cursor_remainder_axis(uint16_t remainder_pixels, bool positive);
static void script_move_cursor_axis_exact(int16_t delta, bool is_x_axis);
static void script_move_cursor_to(int16_t target_x, int16_t target_y);
static void script_place_dot(void);
static void script_hold_left_stick(uint8_t lx, uint8_t ly, uint16_t frames, uint16_t wait_frames);
static uint16_t abs_i16(int16_t value);
static uint32_t abs_i32(int32_t value);
static uint32_t isqrt_u64(uint64_t value);
static int16_t axis_delta_from_center(uint8_t axis);
static void apply_modeled_stick_motion_q(switch_controller_report_t report, uint16_t speed_q,
                                         bool drawing, int32_t *cursor_x_q,
                                         int32_t *cursor_y_q);
static void script_move_free_axis_q(int32_t delta_q, bool is_x_axis);
static void script_move_axis_to_cursor_q(int32_t target_x_q, int32_t target_y_q);
static uint8_t free_cursor_trim_offset_for_distance(uint16_t distance_q);
static uint8_t draw_trim_offset_for_distance(uint16_t distance_q);
static uint8_t cursor_axis_offset_for_speed_q(const trim_sample_t *samples, size_t sample_count,
                                              uint16_t speed_q);
static uint16_t cursor_speed_for_axis_offset_q(const trim_sample_t *samples, size_t sample_count,
                                               uint8_t axis_offset);
static int32_t pixel_to_cursor_center_q(int16_t pixel);
static uint8_t axis_for_signed_offset(int32_t component_q, uint8_t offset);
static uint8_t vector_axis_for_component_speed_q(int32_t component_q, uint32_t length_q,
                                                 uint16_t speed_q, bool drawing);
static void script_move_vector_to_cursor_q(int32_t target_x_q, int32_t target_y_q);
static void script_draw_vector_to_cursor_q(int32_t target_x_q, int32_t target_y_q);
static void script_mark_home_known(void);
static void script_top_left_outline_dot_step(uint16_t step);
static switch_controller_report_t scripted_report(uint32_t now_usb_us);

int main(void) {
  build_script();
  board_init();

  tud_init(BOARD_TUD_RHPORT);

  if (board_init_after_tusb) {
    board_init_after_tusb();
  }

  while (1) {
    tud_task();
    hid_task();
  }
}

void tud_mount_cb(void) {
  uint32_t now_us = time_us_32();
  sof_ticks = 0;
  last_sof_time_us = now_us;
  last_report_sof = 0;
  last_report_valid = false;
  reset_script_cursor();
  tud_sof_cb_enable(true);
}

void tud_umount_cb(void) {
  tud_sof_cb_enable(false);
  last_report_valid = false;
}

void tud_suspend_cb(bool remote_wakeup_en) {
  (void) remote_wakeup_en;
  tud_sof_cb_enable(false);
}

void tud_resume_cb(void) {
  if (tud_mounted()) {
    tud_sof_cb_enable(true);
  }
}

void tud_sof_cb(uint32_t frame_count) {
  (void) frame_count;
  last_sof_time_us = time_us_32();
  ++sof_ticks;
}

static switch_controller_report_t neutral_report(void) {
  switch_controller_report_t report = {
      .buttons = 0,
      .hat = SWITCH_HAT_CENTERED,
      .lx = AXIS_CENTER,
      .ly = AXIS_CENTER,
      .rx = AXIS_CENTER,
      .ry = AXIS_CENTER,
      .vendor_spec = 0,
  };

  return report;
}

static uint32_t usb_time_to_frame(uint32_t now_usb_us) {
  return (uint32_t) (((uint64_t) now_usb_us * GAME_FRAME_RATE) / 1000000u);
}

static void reset_script_cursor(void) {
  script_phase = SCRIPT_PHASE_SETUP;
  script_test_step = 0;
  script_cursor_x = 0;
  script_cursor_y = 0;
  script_cursor_x_q = 0;
  script_cursor_y_q = 0;
  script_cursor_known = false;
  selected_tool_known = false;
  selected_tool = TOOL_PEN;
  script_step_index = 0;
  script_step_start_frame = 0;
  script_generate_next_chunk();
}

static void script_begin_chunk(void) {
  script_step_count = 0;
  script_step_index = 0;
  script_overflowed = false;
}

static bool script_generate_next_chunk(void) {
  script_begin_chunk();

  switch (script_phase) {
    case SCRIPT_PHASE_SETUP:
      script_wait_frames(SCRIPT_START_DELAY_FRAMES);
      for (uint8_t i = 0; i < STARTUP_A_PRESS_COUNT; ++i) {
        script_hold_buttons(SWITCH_BUTTON_A, ACTIVATE_PRESS_FRAMES, ACTIVATE_RELEASE_FRAMES);
      }
      for (uint8_t i = 0; i < STARTUP_B_PRESS_COUNT; ++i) {
        script_hold_buttons(SWITCH_BUTTON_B, ACTIVATE_PRESS_FRAMES, ACTIVATE_RELEASE_FRAMES);
      }
      script_wait_frames(POST_ACTIVATION_DELAY_FRAMES);
      script_set_brush();
      script_clear();
      script_zoom_out();
      script_home();
      script_mark_home_known();
      script_phase = SCRIPT_PHASE_TOP_LEFT_OUTLINE_DOT_TEST;
      break;

    case SCRIPT_PHASE_TOP_LEFT_OUTLINE_DOT_TEST:
      if (script_test_step >= TOP_LEFT_OUTLINE_TOTAL_COUNT) {
        script_phase = SCRIPT_PHASE_DONE;
        return false;
      }

      script_top_left_outline_dot_step(script_test_step);
      ++script_test_step;
      break;

    case SCRIPT_PHASE_DONE:
      return false;
  }

  if (script_overflowed || (script_step_count == 0)) {
    script_phase = SCRIPT_PHASE_DONE;
    return false;
  }

  return true;
}

static void script_add_report(switch_controller_report_t report, uint16_t frames) {
  if (script_overflowed) {
    return;
  }

  if (frames == 0) {
    return;
  }

  if (script_step_count > 0) {
    frame_step_t *last_step = &script_steps[script_step_count - 1];
    if ((memcmp(&last_step->report, &report, sizeof(report)) == 0) &&
        (last_step->frames <= (uint16_t) (UINT16_MAX - frames))) {
      last_step->frames = (uint16_t) (last_step->frames + frames);
      return;
    }
  }

  if (script_step_count >= MAX_SCRIPT_STEPS) {
    script_overflowed = true;
    return;
  }

  script_steps[script_step_count].report = report;
  script_steps[script_step_count].frames = frames;
  ++script_step_count;
}

static void script_wait_frames(uint16_t frames) {
  script_add_report(neutral_report(), frames);
}

static void script_hold_buttons(uint16_t buttons, uint16_t hold_frames, uint16_t wait_frames) {
  switch_controller_report_t report = neutral_report();
  report.buttons = buttons;
  script_add_report(report, hold_frames);
  script_wait_frames(wait_frames);
}

static void script_tap_hat(uint8_t hat) {
  script_tap_hat_wait(hat, INPUT_WAIT_FRAMES);
}

static void script_tap_hat_wait(uint8_t hat, uint16_t wait_frames) {
  switch_controller_report_t report = neutral_report();
  report.hat = hat;
  script_add_report(report, INPUT_TAP_FRAMES);
  script_wait_frames(wait_frames);
}

static void script_tap_right_stick(uint8_t rx, uint8_t ry, uint16_t wait_frames) {
  switch_controller_report_t report = neutral_report();
  report.rx = rx;
  report.ry = ry;
  script_add_report(report, INPUT_TAP_FRAMES);
  script_wait_frames(wait_frames);
}

static void script_focus_tool_cursor(tool_id_t target) {
  script_wait_frames(HELPER_WAIT_FRAMES);
  script_hold_buttons(SWITCH_BUTTON_X, INPUT_TAP_FRAMES, HELPER_WAIT_FRAMES);
  script_wait_frames(HELPER_WAIT_FRAMES);

  if (!selected_tool_known) {
    for (uint8_t i = 0; i < (uint8_t) TOOL_COUNT; ++i) {
      script_tap_hat_wait(SWITCH_HAT_RIGHT, HELPER_WAIT_FRAMES);
    }
    selected_tool = TOOL_QUIT;
    selected_tool_known = true;
  }

  if (target != selected_tool) {
    uint8_t hat = (target < selected_tool) ? SWITCH_HAT_LEFT : SWITCH_HAT_RIGHT;
    uint8_t moves = (uint8_t) ((target < selected_tool) ? (selected_tool - target)
                                                        : (target - selected_tool));
    for (uint8_t i = 0; i < moves; ++i) {
      script_tap_hat_wait(hat, HELPER_WAIT_FRAMES);
    }
  }
}

static void script_tool_change(tool_id_t target) {
  script_focus_tool_cursor(target);

  if ((target == TOOL_TEXT_STAMP) || (target == TOOL_SHAPE)) {
    script_hold_buttons(SWITCH_BUTTON_A, INPUT_TAP_FRAMES, STAMP_SHAPE_CONFIRM_WAIT_FRAMES);
    script_hold_buttons(SWITCH_BUTTON_A, INPUT_TAP_FRAMES, STAMP_SHAPE_CONFIRM_WAIT_FRAMES);
  } else {
    script_hold_buttons(SWITCH_BUTTON_A, INPUT_TAP_FRAMES, HELPER_WAIT_FRAMES);
  }

  selected_tool = target;
}

static void script_homing_tool_change(tool_id_t target) {
  script_focus_tool_cursor(target);

  if ((target == TOOL_TEXT_STAMP) || (target == TOOL_SHAPE)) {
    script_hold_buttons(SWITCH_BUTTON_A, INPUT_TAP_FRAMES, STAMP_SHAPE_CONFIRM_WAIT_FRAMES);
    script_hold_buttons(SWITCH_BUTTON_A, INPUT_TAP_FRAMES, STAMP_SHAPE_CONFIRM_WAIT_FRAMES);
  } else {
    script_hold_buttons(SWITCH_BUTTON_A, INPUT_TAP_FRAMES, HELPER_WAIT_FRAMES);
  }

  selected_tool = target;
}

static void script_set_brush(void) {
  script_focus_tool_cursor(TOOL_PEN);

  script_wait_frames(HELPER_WAIT_FRAMES);
  script_hold_buttons(SWITCH_BUTTON_X, INPUT_TAP_FRAMES, HELPER_WAIT_FRAMES);

  for (uint8_t i = 0; i < SET_BRUSH_LEFT_COUNT; ++i) {
    script_tap_hat_wait(SWITCH_HAT_LEFT, HELPER_WAIT_FRAMES);
  }

  script_hold_buttons(SWITCH_BUTTON_A, INPUT_TAP_FRAMES, HELPER_WAIT_FRAMES);
  script_hold_buttons(SWITCH_BUTTON_A, INPUT_TAP_FRAMES, HELPER_WAIT_FRAMES);
  selected_tool = TOOL_PEN;
}

static void script_clear(void) {
  tool_id_t prior_tool = selected_tool;
  bool prior_tool_known = selected_tool_known;

  script_focus_tool_cursor(TOOL_ERASER);

  script_wait_frames(HELPER_WAIT_FRAMES);
  script_hold_buttons(SWITCH_BUTTON_X, INPUT_TAP_FRAMES, HELPER_WAIT_FRAMES);

  for (uint8_t i = 0; i < CLEAR_UP_COUNT; ++i) {
    script_tap_hat_wait(SWITCH_HAT_UP, HELPER_WAIT_FRAMES);
  }

  script_tap_hat_wait(SWITCH_HAT_DOWN, HELPER_WAIT_FRAMES);

  for (uint8_t i = 0; i < CLEAR_DOWN_COUNT; ++i) {
    script_tap_hat_wait(SWITCH_HAT_DOWN, HELPER_WAIT_FRAMES);
  }

  script_hold_buttons(SWITCH_BUTTON_A, INPUT_TAP_FRAMES, HELPER_WAIT_FRAMES);
  selected_tool = prior_tool;
  selected_tool_known = prior_tool_known;
}

static void script_home(void) {
  script_homing_tool_change(TOOL_TEXT_STAMP);
  script_homing_tool_change(TOOL_PEN);
  script_wait_frames(HOME_CANVAS_SETTLE_FRAMES);
}

static void script_zoom_out(void) {
  for (uint8_t i = 0; i < ZOOM_REPEAT_COUNT; ++i) {
    script_tap_right_stick(AXIS_CENTER, AXIS_MAX, ZOOM_WAIT_FRAMES);
  }
}

static void script_move_cursor_right(uint16_t units) {
  for (uint16_t i = 0; i < units; ++i) {
    script_tap_hat(SWITCH_HAT_RIGHT);
  }

  if (script_cursor_known) {
    script_cursor_x = (int16_t) (script_cursor_x + (int16_t) units);
    script_cursor_x_q += (int32_t) units * CURSOR_Q;
  }
}

static void script_move_cursor_up(uint16_t units) {
  for (uint16_t i = 0; i < units; ++i) {
    script_tap_hat(SWITCH_HAT_UP);
  }

  if (script_cursor_known) {
    script_cursor_y = (int16_t) (script_cursor_y - (int16_t) units);
    script_cursor_y_q -= (int32_t) units * CURSOR_Q;
  }
}

static void script_move_cursor_down(uint16_t units) {
  for (uint16_t i = 0; i < units; ++i) {
    script_tap_hat(SWITCH_HAT_DOWN);
  }

  if (script_cursor_known) {
    script_cursor_y = (int16_t) (script_cursor_y + (int16_t) units);
    script_cursor_y_q += (int32_t) units * CURSOR_Q;
  }
}

static void script_move_cursor_left(uint16_t units) {
  for (uint16_t i = 0; i < units; ++i) {
    script_tap_hat(SWITCH_HAT_LEFT);
  }

  if (script_cursor_known) {
    script_cursor_x = (int16_t) (script_cursor_x - (int16_t) units);
    script_cursor_x_q -= (int32_t) units * CURSOR_Q;
  }
}

static uint8_t cursor_remainder_axis(uint16_t remainder_pixels, bool positive) {
  uint8_t positive_axis = AXIS_CENTER;

  switch (remainder_pixels) {
    case 1:
      positive_axis = AXIS_CURSOR_REMAINDER_ONE;
      break;
    case 2:
      positive_axis = AXIS_CURSOR_REMAINDER_TWO;
      break;
    case 3:
      positive_axis = AXIS_CURSOR_REMAINDER_THREE;
      break;
    case 4:
      positive_axis = AXIS_CURSOR_REMAINDER_FOUR;
      break;
    default:
      return AXIS_CENTER;
  }

  if (positive) {
    return positive_axis;
  }

  return (uint8_t) ((AXIS_CENTER * 2u) - positive_axis);
}

static void script_move_cursor_axis_exact(int16_t delta, bool is_x_axis) {
  switch_controller_report_t move_report = neutral_report();
  uint16_t distance = (uint16_t) ((delta >= 0) ? delta : -delta);
  uint16_t fast_frames = (uint16_t) (distance / CURSOR_FREE_SPEED_PIXELS_PER_FRAME);
  uint16_t remainder_pixels = (uint16_t) (distance % CURSOR_FREE_SPEED_PIXELS_PER_FRAME);
  bool positive = (delta >= 0);

  if (distance == 0) {
    return;
  }

  if (fast_frames > 0) {
    if (is_x_axis) {
      move_report.lx = positive ? AXIS_MAX : AXIS_MIN;
    } else {
      move_report.ly = positive ? AXIS_MAX : AXIS_MIN;
    }
    script_add_report(move_report, fast_frames);
  }

  if (remainder_pixels > 0) {
    move_report = neutral_report();
    if (is_x_axis) {
      move_report.lx = cursor_remainder_axis(remainder_pixels, positive);
    } else {
      move_report.ly = cursor_remainder_axis(remainder_pixels, positive);
    }
    script_add_report(move_report, INPUT_TAP_FRAMES);
  }

  script_wait_frames(INPUT_WAIT_FRAMES);
}

static void script_move_cursor_to(int16_t target_x, int16_t target_y) {
  int16_t delta_x = 0;
  int16_t delta_y = 0;

  if (!script_cursor_known) {
    return;
  }

  delta_x = (int16_t) (target_x - script_cursor_x);
  delta_y = (int16_t) (target_y - script_cursor_y);

  script_move_cursor_axis_exact(delta_x, true);
  script_move_cursor_axis_exact(delta_y, false);

  script_cursor_x = target_x;
  script_cursor_y = target_y;
  script_cursor_x_q = (int32_t) target_x * CURSOR_Q;
  script_cursor_y_q = (int32_t) target_y * CURSOR_Q;
}

static void script_place_dot(void) {
  script_hold_buttons(SWITCH_BUTTON_A, INPUT_TAP_FRAMES, INPUT_WAIT_FRAMES);
}

static void script_hold_left_stick(uint8_t lx, uint8_t ly, uint16_t frames, uint16_t wait_frames) {
  switch_controller_report_t report = neutral_report();
  report.lx = lx;
  report.ly = ly;
  script_add_report(report, frames);
  script_wait_frames(wait_frames);
}

static uint16_t abs_i16(int16_t value) {
  return (uint16_t) ((value >= 0) ? value : -value);
}

static uint32_t abs_i32(int32_t value) {
  return (uint32_t) ((value >= 0) ? value : -value);
}

static uint32_t isqrt_u64(uint64_t value) {
  uint64_t bit = 1ull << 62;
  uint64_t result = 0;

  while (bit > value) {
    bit >>= 2;
  }

  while (bit != 0) {
    if (value >= result + bit) {
      value -= result + bit;
      result = (result >> 1) + bit;
    } else {
      result >>= 1;
    }
    bit >>= 2;
  }

  return (uint32_t) result;
}

static int16_t axis_delta_from_center(uint8_t axis) {
  if (axis >= AXIS_CENTER) {
    return (int16_t) (axis - AXIS_CENTER);
  }

  return (int16_t) -((int16_t) AXIS_CENTER - (int16_t) axis);
}

static void apply_modeled_stick_motion_q(switch_controller_report_t report, uint16_t speed_q,
                                         bool drawing, int32_t *cursor_x_q, int32_t *cursor_y_q) {
  int16_t stick_x = axis_delta_from_center(report.lx);
  int16_t stick_y = axis_delta_from_center(report.ly);
  const trim_sample_t *samples = drawing ? draw_trim_samples : free_cursor_trim_samples;
  size_t sample_count = drawing ? (sizeof(draw_trim_samples) / sizeof(draw_trim_samples[0]))
                                : (sizeof(free_cursor_trim_samples) /
                                   sizeof(free_cursor_trim_samples[0]));
  uint16_t speed_x_q = cursor_speed_for_axis_offset_q(samples, sample_count, abs_i16(stick_x));
  uint16_t speed_y_q = cursor_speed_for_axis_offset_q(samples, sample_count, abs_i16(stick_y));
  uint32_t movement_length_q =
      isqrt_u64(((uint64_t) speed_x_q * speed_x_q) + ((uint64_t) speed_y_q * speed_y_q));

  if (movement_length_q == 0) {
    return;
  }

  if (movement_length_q > speed_q) {
    speed_x_q = (uint16_t) ((((uint64_t) speed_x_q * speed_q) + (movement_length_q / 2u)) /
                            movement_length_q);
    speed_y_q = (uint16_t) ((((uint64_t) speed_y_q * speed_q) + (movement_length_q / 2u)) /
                            movement_length_q);
  }

  *cursor_x_q += (stick_x >= 0) ? (int32_t) speed_x_q : -(int32_t) speed_x_q;
  *cursor_y_q += (stick_y >= 0) ? (int32_t) speed_y_q : -(int32_t) speed_y_q;
}

static void script_move_free_axis_q(int32_t delta_q, bool is_x_axis) {
  switch_controller_report_t move_report = neutral_report();
  uint32_t distance_q = abs_i32(delta_q);
  uint16_t full_frames = 0;
  uint16_t trim_distance_q = 0;
  uint8_t trim_magnitude = 0;
  bool positive = (delta_q > 0);

  if ((delta_q == 0) || !script_cursor_known) {
    return;
  }

  full_frames = (uint16_t) (distance_q / CURSOR_FREE_SPEED_Q);
  trim_distance_q = (uint16_t) (distance_q % CURSOR_FREE_SPEED_Q);
  trim_magnitude = free_cursor_trim_offset_for_distance(trim_distance_q);

  if (full_frames > 0) {
    if (is_x_axis) {
      move_report.lx = positive ? AXIS_MAX : AXIS_MIN;
    } else {
      move_report.ly = positive ? AXIS_MAX : AXIS_MIN;
    }
    script_add_report(move_report, full_frames);
  }

  if (trim_magnitude > 0) {
    move_report = neutral_report();
    if (is_x_axis) {
      move_report.lx = positive ? (uint8_t) (AXIS_CENTER + trim_magnitude)
                                : (uint8_t) (AXIS_CENTER - trim_magnitude);
    } else {
      move_report.ly = positive ? (uint8_t) (AXIS_CENTER + trim_magnitude)
                                : (uint8_t) (AXIS_CENTER - trim_magnitude);
    }
    script_add_report(move_report, INPUT_TAP_FRAMES);
  }

  script_wait_frames(INPUT_WAIT_FRAMES);
  if (is_x_axis) {
    script_cursor_x_q += delta_q;
  } else {
    script_cursor_y_q += delta_q;
  }
}

static void script_move_axis_to_cursor_q(int32_t target_x_q, int32_t target_y_q) {
  if (!script_cursor_known) {
    return;
  }

  script_move_free_axis_q(target_x_q - script_cursor_x_q, true);
  script_wait_frames(CURSOR_FREE_POST_SETTLE_FRAMES);
  script_move_free_axis_q(target_y_q - script_cursor_y_q, false);
  script_wait_frames(CURSOR_FREE_POST_SETTLE_FRAMES);
  script_cursor_x_q = target_x_q;
  script_cursor_y_q = target_y_q;
  script_cursor_x = (int16_t) (target_x_q / CURSOR_Q);
  script_cursor_y = (int16_t) (target_y_q / CURSOR_Q);
}

static uint8_t cursor_axis_offset_for_speed_q(const trim_sample_t *samples, size_t sample_count,
                                              uint16_t speed_q) {
  if (speed_q == 0) {
    return 0;
  }

  for (size_t i = 1; i < sample_count; ++i) {
    const trim_sample_t *low = &samples[i - 1];
    const trim_sample_t *high = &samples[i];
    uint16_t span_q = (uint16_t) (high->distance_q - low->distance_q);
    uint16_t into_q = (uint16_t) (speed_q - low->distance_q);

    if (speed_q > high->distance_q) {
      continue;
    }

    if (span_q == 0) {
      return high->axis_offset;
    }

    uint8_t offset =
        (uint8_t) (low->axis_offset +
                   ((((uint16_t) (high->axis_offset - low->axis_offset) * into_q) +
                     (span_q / 2u)) /
                    span_q));
    return (offset < AXIS_MIN_MOVING_OFFSET) ? AXIS_MIN_MOVING_OFFSET : offset;
  }

  return 127;
}

static uint16_t cursor_speed_for_axis_offset_q(const trim_sample_t *samples, size_t sample_count,
                                               uint8_t axis_offset) {
  if (axis_offset == 0) {
    return 0;
  }

  for (size_t i = 1; i < sample_count; ++i) {
    const trim_sample_t *low = &samples[i - 1];
    const trim_sample_t *high = &samples[i];
    uint16_t span_offset = (uint16_t) (high->axis_offset - low->axis_offset);
    uint16_t into_offset = (uint16_t) (axis_offset - low->axis_offset);

    if (axis_offset > high->axis_offset) {
      continue;
    }

    if (span_offset == 0) {
      return high->distance_q;
    }

    return (uint16_t) (low->distance_q +
                       ((((uint32_t) (high->distance_q - low->distance_q) * into_offset) +
                         (span_offset / 2u)) /
                        span_offset));
  }

  return samples[sample_count - 1].distance_q;
}

static uint8_t free_cursor_trim_offset_for_distance(uint16_t distance_q) {
  return cursor_axis_offset_for_speed_q(
      free_cursor_trim_samples,
      sizeof(free_cursor_trim_samples) / sizeof(free_cursor_trim_samples[0]), distance_q);
}

static uint8_t draw_trim_offset_for_distance(uint16_t distance_q) {
  return cursor_axis_offset_for_speed_q(
      draw_trim_samples, sizeof(draw_trim_samples) / sizeof(draw_trim_samples[0]), distance_q);
}

static int32_t pixel_to_cursor_center_q(int16_t pixel) {
  // Target cell centers, not boundaries: drawing exactly on boundary 0 would
  // mark positive pixel 1, so centers keep dot calibration unambiguous.
  if (pixel > 0) {
    return ((int32_t) pixel * CURSOR_Q) - CURSOR_HALF_Q;
  }

  if (pixel < 0) {
    return ((int32_t) pixel * CURSOR_Q) + CURSOR_HALF_Q;
  }

  return 0;
}

static uint8_t axis_for_signed_offset(int32_t component_q, uint8_t offset) {
  if ((component_q == 0) || (offset == 0)) {
    return AXIS_CENTER;
  }

  if (component_q > 0) {
    return (uint8_t) (AXIS_CENTER + offset);
  }

  if (offset >= 127u) {
    return AXIS_MIN;
  }

  return (uint8_t) (AXIS_CENTER - offset);
}

static uint8_t vector_axis_for_component_speed_q(int32_t component_q, uint32_t length_q,
                                                 uint16_t speed_q, bool drawing) {
  uint16_t component_speed_q = 0;
  uint8_t offset = 0;

  if ((component_q == 0) || (length_q == 0) || (speed_q == 0)) {
    return AXIS_CENTER;
  }

  component_speed_q =
      (uint16_t) ((((uint64_t) abs_i32(component_q) * speed_q) + (length_q / 2u)) / length_q);
  offset = drawing ? draw_trim_offset_for_distance(component_speed_q)
                   : free_cursor_trim_offset_for_distance(component_speed_q);
  return axis_for_signed_offset(component_q, offset);
}

static void script_vector_to_cursor_q(int32_t target_x_q, int32_t target_y_q, uint16_t buttons,
                                      bool prehold) {
  switch_controller_report_t move_report = neutral_report();
  int32_t estimated_x_q = script_cursor_x_q;
  int32_t estimated_y_q = script_cursor_y_q;
  int32_t delta_x_q = 0;
  int32_t delta_y_q = 0;
  uint32_t abs_x_q = 0;
  uint32_t abs_y_q = 0;
  uint32_t travel_length_q = 0;
  uint16_t speed_q = 0;
  uint16_t steered_frames = 0;
  bool drawing = ((buttons & SWITCH_BUTTON_A) != 0);

  if (!script_cursor_known) {
    return;
  }

  delta_x_q = target_x_q - estimated_x_q;
  delta_y_q = target_y_q - estimated_y_q;
  if ((delta_x_q == 0) && (delta_y_q == 0)) {
    return;
  }

  if (prehold) {
    move_report.buttons = buttons;
    script_add_report(move_report, DRAW_PREHOLD_FRAMES);
  }

  speed_q = drawing ? CURSOR_DRAW_SPEED_Q : CURSOR_FREE_SPEED_Q;

  while (steered_frames < MOVEMENT_MAX_STEER_FRAMES) {
    delta_x_q = target_x_q - estimated_x_q;
    delta_y_q = target_y_q - estimated_y_q;
    abs_x_q = abs_i32(delta_x_q);
    abs_y_q = abs_i32(delta_y_q);
    travel_length_q =
        isqrt_u64(((uint64_t) abs_x_q * abs_x_q) + ((uint64_t) abs_y_q * abs_y_q));

    if (travel_length_q <= speed_q) {
      break;
    }

    move_report = neutral_report();
    move_report.buttons = buttons;
    move_report.lx =
        vector_axis_for_component_speed_q(delta_x_q, travel_length_q, speed_q, drawing);
    move_report.ly =
        vector_axis_for_component_speed_q(delta_y_q, travel_length_q, speed_q, drawing);

    script_add_report(move_report, INPUT_TAP_FRAMES);
    apply_modeled_stick_motion_q(move_report, speed_q, drawing, &estimated_x_q, &estimated_y_q);
    ++steered_frames;
  }

  delta_x_q = target_x_q - estimated_x_q;
  delta_y_q = target_y_q - estimated_y_q;
  abs_x_q = abs_i32(delta_x_q);
  abs_y_q = abs_i32(delta_y_q);
  travel_length_q =
      isqrt_u64(((uint64_t) abs_x_q * abs_x_q) + ((uint64_t) abs_y_q * abs_y_q));

  if (travel_length_q > 0) {
    move_report = neutral_report();
    move_report.buttons = buttons;
    move_report.lx = vector_axis_for_component_speed_q(
        delta_x_q, travel_length_q, (uint16_t) travel_length_q, drawing);
    move_report.ly = vector_axis_for_component_speed_q(
        delta_y_q, travel_length_q, (uint16_t) travel_length_q, drawing);
    script_add_report(move_report, INPUT_TAP_FRAMES);
  }

  script_wait_frames(INPUT_WAIT_FRAMES);
  script_cursor_x_q = target_x_q;
  script_cursor_y_q = target_y_q;
  script_cursor_x = (int16_t) (target_x_q / CURSOR_Q);
  script_cursor_y = (int16_t) (target_y_q / CURSOR_Q);
}

static void script_move_vector_to_cursor_q(int32_t target_x_q, int32_t target_y_q) {
  script_vector_to_cursor_q(target_x_q, target_y_q, 0, false);
}

static void script_draw_vector_to_cursor_q(int32_t target_x_q, int32_t target_y_q) {
  script_vector_to_cursor_q(target_x_q, target_y_q, SWITCH_BUTTON_A, true);
}

static void script_mark_home_known(void) {
  script_cursor_x = 0;
  script_cursor_y = 0;
  script_cursor_x_q = 0;
  script_cursor_y_q = 0;
  script_cursor_known = true;
}

static void script_top_left_outline_dot_step(uint16_t step) {
  int16_t pixel_x = TOP_LEFT_OUTLINE_MIN_PIXEL;
  int16_t pixel_y = 1;

  if (step < TOP_LEFT_OUTLINE_VERTICAL_COUNT) {
    pixel_y = (step == 0) ? 1 : (int16_t) -step;
  } else {
    uint16_t horizontal_step = (uint16_t) (step - TOP_LEFT_OUTLINE_VERTICAL_COUNT);
    pixel_y = TOP_LEFT_OUTLINE_MIN_PIXEL;
    pixel_x = (horizontal_step < (uint16_t) (TOP_LEFT_OUTLINE_HORIZONTAL_COUNT - 1))
                  ? (int16_t) (TOP_LEFT_OUTLINE_MIN_PIXEL + 1 + (int16_t) horizontal_step)
                  : 1;
  }

  script_home();
  script_mark_home_known();
  script_move_axis_to_cursor_q(pixel_to_cursor_center_q(pixel_x),
                               pixel_to_cursor_center_q(pixel_y));
  script_place_dot();
}

static void build_script(void) {
  reset_script_cursor();
}

static uint32_t disciplined_usb_time_us(void) {
  uint32_t irq_state = save_and_disable_interrupts();
  uint32_t tick_snapshot = sof_ticks;
  uint32_t sof_time_snapshot = last_sof_time_us;
  restore_interrupts(irq_state);

  uint32_t now_local_us = time_us_32();
  uint32_t since_sof_us = now_local_us - sof_time_snapshot;
  if (since_sof_us >= USB_SOF_US) {
    since_sof_us = USB_SOF_US - 1;
  }

  return tick_snapshot * USB_SOF_US + since_sof_us;
}

static switch_controller_report_t scripted_report(uint32_t now_usb_us) {
  uint32_t current_frame = usb_time_to_frame(now_usb_us);

  while (true) {
    while (script_step_index < script_step_count) {
      uint32_t step_end_frame =
          script_step_start_frame + script_steps[script_step_index].frames;
      if (current_frame < step_end_frame) {
        return script_steps[script_step_index].report;
      }

      script_step_start_frame = step_end_frame;
      ++script_step_index;
    }

    if (!script_generate_next_chunk()) {
      return neutral_report();
    }
  }
}

static void hid_task(void) {
  uint32_t now_sof = sof_ticks;
  if (now_sof == last_report_sof) {
    return;
  }

  if (!tud_hid_ready()) {
    return;
  }

  switch_controller_report_t report = scripted_report(disciplined_usb_time_us());
  if (last_report_valid && memcmp(&last_report, &report, sizeof(report)) == 0) {
    last_report_sof = now_sof;
    return;
  }

  if (tud_hid_report(0, &report, sizeof(report))) {
    last_report = report;
    last_report_valid = true;
    last_report_sof = now_sof;
  }
}

uint16_t tud_hid_get_report_cb(uint8_t instance,
                               uint8_t report_id,
                               hid_report_type_t report_type,
                               uint8_t *buffer,
                               uint16_t reqlen) {
  (void) instance;
  (void) report_id;
  (void) report_type;
  (void) buffer;
  (void) reqlen;
  return 0;
}

void tud_hid_set_report_cb(uint8_t instance,
                           uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const *buffer,
                           uint16_t bufsize) {
  (void) instance;
  (void) report_id;
  (void) report_type;
  (void) buffer;
  (void) bufsize;
}
