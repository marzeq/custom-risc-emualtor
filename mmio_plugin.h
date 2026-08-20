#ifndef MMIO_PLUGIN_H
#define MMIO_PLUGIN_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Host-side ABI for MMIO device plugins.
 *
 * The emulator owns address placement. Callbacks therefore receive an offset
 * within the device, never a guest physical address.
 */
#define MMIO_PLUGIN_ABI_VERSION 1u
#define MMIO_PLUGIN_ENTRYPOINT "mmio_plugin_get_descriptor"

typedef bool (*mmio_plugin_read_fn)(uint64_t offset, uint64_t* value);
typedef bool (*mmio_plugin_write_fn)(uint64_t offset, uint64_t value);

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
const mmio_plugin_descriptor* mmio_plugin_get_descriptor(
  uint64_t abi_version
);

#endif
