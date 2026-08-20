#ifndef MMIO_PLUGIN_H
#define MMIO_PLUGIN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Host-side ABI for MMIO device plugins.
 *
 * The emulator owns address placement. Callbacks therefore receive an offset
 * within the device, never a guest physical address.
 */
#define MMIO_PLUGIN_ABI_VERSION 1u
#define MMIO_PLUGIN_ENTRYPOINT "mmio_plugin_get_descriptor"

#if defined(__GNUC__) || defined(__clang__)
#define MMIO_PLUGIN_EXPORT [[gnu::visibility("default")]]
#else
#define MMIO_PLUGIN_EXPORT
#endif

typedef bool (*mmio_plugin_read_fn)(uint64_t offset, uint64_t* value);
typedef bool (*mmio_plugin_write_fn)(uint64_t offset, uint64_t value);

/*
 * Stable guest-visible device type registry. Values are append-only: never
 * renumber or reuse an existing value. The emulator treats them as opaque.
 */
typedef enum {
  MMIO_DEVICE_STDIO = 1,
  MMIO_DEVICE_SIMPLE_VDISK = 2,
} mmio_device_type;

typedef struct {
  uint64_t type;
  uint64_t size;
  mmio_plugin_read_fn read;
  mmio_plugin_write_fn write;
  const char* name;
} mmio_plugin_descriptor;

typedef const mmio_plugin_descriptor* (*mmio_plugin_entrypoint_fn)(
  uint64_t abi_version
);

/* Every plugin exports this symbol with default visibility. */
MMIO_PLUGIN_EXPORT
const mmio_plugin_descriptor* mmio_plugin_get_descriptor(
  uint64_t abi_version
);

/* Convenience macros for the usual one-device-per-shared-library plugin. */
#define MMIO_PLUGIN_READ(callback_name, offset_name, value_name) \
  static bool callback_name(uint64_t offset_name, uint64_t* value_name)

#define MMIO_PLUGIN_WRITE(callback_name, offset_name, value_name) \
  static bool callback_name(uint64_t offset_name, uint64_t value_name)

#define MMIO_PLUGIN_DEFINE(...)                                  \
  static const mmio_plugin_descriptor mmio_plugin_device =       \
    __VA_ARGS__;                                                  \
                                                                  \
  MMIO_PLUGIN_EXPORT                                             \
  const mmio_plugin_descriptor* mmio_plugin_get_descriptor(      \
    uint64_t abi_version                                         \
  ) {                                                            \
    return abi_version == MMIO_PLUGIN_ABI_VERSION                 \
      ? &mmio_plugin_device                                      \
      : NULL;                                                    \
  }

#endif
