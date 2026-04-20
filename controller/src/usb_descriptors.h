#ifndef USB_DESCRIPTORS_H_
#define USB_DESCRIPTORS_H_

#include <stdint.h>

enum {
  SWITCH_HAT_UP = 0x00,
  SWITCH_HAT_UP_RIGHT = 0x01,
  SWITCH_HAT_RIGHT = 0x02,
  SWITCH_HAT_DOWN_RIGHT = 0x03,
  SWITCH_HAT_DOWN = 0x04,
  SWITCH_HAT_DOWN_LEFT = 0x05,
  SWITCH_HAT_LEFT = 0x06,
  SWITCH_HAT_UP_LEFT = 0x07,
  SWITCH_HAT_CENTERED = 0x08,
};

enum {
  SWITCH_BUTTON_Y = 1u << 0,
  SWITCH_BUTTON_B = 1u << 1,
  SWITCH_BUTTON_A = 1u << 2,
  SWITCH_BUTTON_X = 1u << 3,
  SWITCH_BUTTON_L = 1u << 4,
  SWITCH_BUTTON_R = 1u << 5,
  SWITCH_BUTTON_ZL = 1u << 6,
  SWITCH_BUTTON_ZR = 1u << 7,
  SWITCH_BUTTON_MINUS = 1u << 8,
  SWITCH_BUTTON_PLUS = 1u << 9,
  SWITCH_BUTTON_LSTICK = 1u << 10,
  SWITCH_BUTTON_RSTICK = 1u << 11,
  SWITCH_BUTTON_HOME = 1u << 12,
  SWITCH_BUTTON_CAPTURE = 1u << 13,
};

typedef struct __attribute__((packed)) {
  uint16_t buttons;
  uint8_t hat;
  uint8_t lx;
  uint8_t ly;
  uint8_t rx;
  uint8_t ry;
  uint8_t vendor_spec;
} switch_controller_report_t;

#endif /* USB_DESCRIPTORS_H_ */
