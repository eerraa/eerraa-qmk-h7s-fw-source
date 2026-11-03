#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
  uint8_t type, request;
  uint16_t value, index, length;
} usb_setup_t;

enum { USB_CLASS_ALL_INTERFACES = 0xff };

typedef enum {
  USB_CLASS_FRESH,
  USB_CLASS_DEFAULTS
} usb_class_transition_t;

static inline uint32_t usbEndpointMask(uint8_t address)
{
  uint8_t endpoint = address & 15U;
  if ((address & 0x70U) || endpoint >= 9U) return 0U;
  return 1UL << (endpoint + ((address & 0x80U) ? 0U : 16U));
}

// Endpoint lifetime belongs to the lifecycle executor: a class never closes,
// flushes or stops an endpoint itself.
typedef struct {
  bool (*control)(const usb_setup_t *setup);
  bool (*received)(const usb_setup_t *setup, const uint8_t *data, uint16_t length);
  // Frame-paced service; settles its own SOF demand.
  void (*sof)(void);
  // Caller owns the frozen, quiesced scope. OPEN never arms or publishes
  // admission. RESTART follows successful OPEN of every participating class.
  // False is a completed phase failure; the lifecycle owner performs rollback.
  bool (*open)(uint8_t interface, usb_class_transition_t transition);
  bool (*restart)(uint8_t interface, usb_class_transition_t transition);
  uint32_t (*scope)(uint8_t interface, bool opened_only);
  void (*retire)(void);
  // Release only after the scoped PCD owners have physically completed.
  bool (*release)(uint8_t interface, usb_class_transition_t transition);
} usb_class_t;
