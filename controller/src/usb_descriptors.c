#include <string.h>

#include "bsp/board_api.h"
#include "tusb.h"

#include "usb_descriptors.h"

// Compatibility identity used only for local Switch bring-up testing.
#define USB_VID 0x0F0D
#define USB_PID 0x00C1
#define USB_BCD 0x0200

enum {
  ITF_NUM_HID = 0,
  ITF_NUM_TOTAL
};

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_INOUT_DESC_LEN)
#define EPNUM_HID_OUT 0x02
#define EPNUM_HID_IN 0x81

static const tusb_desc_device_t desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = USB_BCD,
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = USB_VID,
    .idProduct = USB_PID,
    .bcdDevice = 0x0572,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x00,
    .bNumConfigurations = 0x01,
};

static const uint8_t desc_hid_report[] = {
    0x05, 0x01,
    0x09, 0x05,
    0xA1, 0x01,
    0x15, 0x00,
    0x25, 0x01,
    0x35, 0x00,
    0x45, 0x01,
    0x75, 0x01,
    0x95, 0x0E,
    0x05, 0x09,
    0x19, 0x01,
    0x29, 0x0E,
    0x81, 0x02,
    0x95, 0x02,
    0x81, 0x01,
    0x05, 0x01,
    0x25, 0x07,
    0x46, 0x3B, 0x01,
    0x75, 0x04,
    0x95, 0x01,
    0x65, 0x14,
    0x09, 0x39,
    0x81, 0x42,
    0x65, 0x00,
    0x95, 0x01,
    0x81, 0x01,
    0x15, 0x00,
    0x26, 0xFF, 0x00,
    0x46, 0xFF, 0x00,
    0x09, 0x30,
    0x09, 0x31,
    0x09, 0x32,
    0x09, 0x35,
    0x75, 0x08,
    0x95, 0x04,
    0x81, 0x02,
    0x95, 0x01,
    0x75, 0x08,
    0x81, 0x01,
    0xC0,
};

static const uint8_t desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x80, 250),
    TUD_HID_INOUT_DESCRIPTOR(ITF_NUM_HID,
                             0,
                             HID_ITF_PROTOCOL_NONE,
                             sizeof(desc_hid_report),
                             EPNUM_HID_OUT,
                             EPNUM_HID_IN,
                             CFG_TUD_HID_EP_BUFSIZE,
                             1),
};

static const char *string_desc_arr[] = {
    (const char[]) {0x09, 0x04},
    "HORI CO.,LTD.",
    "HORIPAD S",
};

static uint16_t desc_str[32 + 1];

uint8_t const *tud_descriptor_device_cb(void) {
  return (uint8_t const *) &desc_device;
}

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
  (void) instance;
  return desc_hid_report;
}

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
  (void) index;
  return desc_configuration;
}

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void) langid;
  size_t chr_count;

  switch (index) {
    case 0:
      memcpy(&desc_str[1], string_desc_arr[0], 2);
      chr_count = 1;
      break;

    default: {
      if (index >= TU_ARRAY_SIZE(string_desc_arr)) {
        return NULL;
      }

      const char *str = string_desc_arr[index];
      if (str == NULL) {
        return NULL;
      }

      chr_count = strlen(str);
      size_t max_count = TU_ARRAY_SIZE(desc_str) - 1;
      if (chr_count > max_count) {
        chr_count = max_count;
      }

      for (size_t i = 0; i < chr_count; ++i) {
        desc_str[1 + i] = str[i];
      }
      break;
    }
  }

  desc_str[0] = (uint16_t) ((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
  return desc_str;
}
