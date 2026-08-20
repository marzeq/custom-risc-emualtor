Weekend project RISC-like architecture, emulator, and assembler.

The ISA is documented in [ISA.md](./ISA.md).

## Build and run

```sh
make
make run-example
```

MMIO devices are native shared-library plugins. Load each device explicitly
when starting the emulator:

```sh
./emulator \
  --device ./plugins/stdio.so \
  program.bin \
  512
```

`--device` (or `-d`) is repeatable. `512` is the optional RAM size in MiB.
No MMIO devices are installed when no `--device` arguments are supplied.
Plugins are trusted native code and have the same permissions as the emulator.

The emulator assigns MMIO ranges in plugin argument order, aligned to 8 bytes.
A plugin never chooses or learns its absolute guest address. Guest firmware
finds the resulting type, base, size, and short name through the existing
device list in the machine-info ROM. This keeps host configuration out of the
device implementation and lets the same plugin work with any RAM layout.

## Writing an MMIO plugin

Include [`mmio_plugin.h`](./mmio_plugin.h), implement offset-based read and
write callbacks, and export `mmio_plugin_get_descriptor`:

```c
#include "mmio_plugin.h"

static bool counter_read(uint64_t offset, uint64_t* value) {
  if (offset != 0) return false;
  *value = 42;
  return true;
}

static bool counter_write(uint64_t offset, uint64_t value) {
  (void)value;
  return offset == 0;
}

static const mmio_plugin_descriptor counter = {
  .type = 0x100,
  .size = 8,
  .read = counter_read,
  .write = counter_write,
  .name = "counter",
};

const mmio_plugin_descriptor* mmio_plugin_get_descriptor(
  uint64_t abi_version
) {
  return abi_version == MMIO_PLUGIN_ABI_VERSION ? &counter : NULL;
}
```

Build it as a position-independent shared library:

```sh
clang -std=c23 -fPIC -shared -I. -o counter.so counter.c
```

The descriptor supplies only:

- `type`: the numeric identifier guest software uses to recognize the device;
- `size`: the number of bytes in its MMIO window;
- `read` and `write`: callbacks receiving a device-relative byte offset;
- `name`: a human-readable name (the guest device record stores its first 15
  bytes plus a terminator).

Returning `false` from a callback rejects the access and produces the
emulator's normal memory-access runtime error. Reads return a 64-bit value;
8-, 16-, and 32-bit guest loads take the corresponding low bits. Narrow guest
writes pass their zero-extended value to `write`.

Plugins remain loaded for the entire emulation run and can keep private state
in normal file-scope data. If a device needs lifecycle work, shared-library
constructor/destructor functions may initialize and clean it up. The bundled
[`plugins/stdio.c`](./plugins/stdio.c) is a complete example, including terminal
setup and restoration.

The entrypoint receives an ABI version so incompatible plugins fail during
startup with a useful error. Device types are intentionally plugin-defined;
authors of independently distributed devices should coordinate identifiers to
avoid guest-visible type collisions.
