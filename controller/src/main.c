#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "bsp/board_api.h"
#include "hardware/sync.h"
#include "pico/time.h"
#include "tusb.h"

#include "usb_descriptors.h"

enum {
  SCRIPT_START_DELAY_US = 2000000,
  ACTIVATE_PRESS_US = 300000,
  ACTIVATE_RELEASE_US = 300000,
  USB_FRAME_BLOCK_US = 50000,
  GAME_FRAMES_PER_BLOCK = 3,
  USB_SOF_US = 1000,
  AXIS_MIN = 0x00,
  AXIS_CENTER = 0x80,
  AXIS_MAX = 0xFF,
};

static volatile uint32_t sof_ticks = 0;
static volatile uint32_t last_sof_time_us = 0;
static uint32_t last_report_sof = 0;
static bool last_report_valid = false;
static switch_controller_report_t last_report;

static void hid_task(void);
static switch_controller_report_t neutral_report(void);
static uint32_t disciplined_usb_time_us(void);
static switch_controller_report_t scripted_report(uint32_t now_usb_us);

int main(void) {
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
  static const struct {
    uint8_t lx;
    uint8_t ly;
  } pattern[] = {
      {AXIS_CENTER, AXIS_MIN},
      {AXIS_MAX, AXIS_CENTER},
  };

  switch_controller_report_t report = neutral_report();

  if (!tud_mounted()) {
    return report;
  }

  uint32_t elapsed_us = now_usb_us;
  if (elapsed_us < SCRIPT_START_DELAY_US) {
    return report;
  }

  elapsed_us -= SCRIPT_START_DELAY_US;

  if (elapsed_us < ACTIVATE_PRESS_US) {
    report.buttons = SWITCH_BUTTON_A;
    return report;
  }

  if (elapsed_us < ACTIVATE_PRESS_US + ACTIVATE_RELEASE_US) {
    return report;
  }

  elapsed_us -= ACTIVATE_PRESS_US + ACTIVATE_RELEASE_US;

  // Use SOF as the long-term clock source, but interpolate between SOFs with
  // the local microsecond timer so the logical frame midpoint stays fixed.
  uint32_t block_phase_us = elapsed_us % USB_FRAME_BLOCK_US;
  uint32_t frame_phase_units =
      (block_phase_us * GAME_FRAMES_PER_BLOCK) % USB_FRAME_BLOCK_US;
  size_t step = (frame_phase_units * TU_ARRAY_SIZE(pattern)) / USB_FRAME_BLOCK_US;
  if (step >= TU_ARRAY_SIZE(pattern)) {
    step = TU_ARRAY_SIZE(pattern) - 1;
  }

  report.lx = pattern[step].lx;
  report.ly = pattern[step].ly;
  return report;
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
