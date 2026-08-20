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

### Persistent virtual disk

Load `plugins/simple_vdisk.so` to attach the `vdisk` file in the emulator's
current working directory:

```sh
./emulator --device ./plugins/simple_vdisk.so program.bin
```

The plugin creates a 16 MiB `vdisk` when the file does not exist or is empty.
An existing non-empty file keeps its current size and contents. The file is not
removed by `make clean`.

Guest software discovers it as `MMIO_DEVICE_SIMPLE_VDISK`. Its MMIO registers
are:

| Offset | Access | Meaning |
| ---: | :---: | --- |
| `0` | read/write | Current byte position. A write seeks within the disk. |
| `8` | read/write | Read or write one little-endian 64-bit word, then advance the position by 8. |
| `16` | read | Disk capacity in bytes. |

Position and data operations outside the disk capacity fail as invalid MMIO
accesses. Successful data writes are flushed to `vdisk` before returning.

The emulator assigns MMIO ranges in plugin argument order, aligned to 8 bytes.
A plugin never chooses or learns its absolute guest address. Guest firmware
finds the resulting type, base, size, and short name through the existing
device list in the machine-info ROM. This keeps host configuration out of the
device implementation and lets the same plugin work with any RAM layout.

## Writing an MMIO plugin

Include [`mmio_plugin.h`](./mmio_plugin.h), implement offset-based read and
write callbacks, and use `MMIO_PLUGIN_DEFINE` to generate the descriptor and
version-checked exported entrypoint:

```c
#include "mmio_plugin.h"

MMIO_PLUGIN_READ(counter_read, register_offset, result) {
  if (register_offset != 0) return false;
  *result = 42;
  return true;
}

MMIO_PLUGIN_WRITE(counter_write, register_offset, new_value) {
  (void)new_value;
  return register_offset == 0;
}

MMIO_PLUGIN_DEFINE({
  .type = 0x100, // private/experimental type
  .size = 8,
  .read = counter_read,
  .write = counter_write,
  .name = "counter",
})
```

Build it as a position-independent shared library:

```sh
clang -std=c23 -fPIC -shared -I. -o counter.so counter.c
```

The descriptor supplies only:

- `type`: the identifier guest software uses to recognize the device; standard
  identifiers come from the stable `mmio_device_type` enum;
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
startup with a useful error. Standard device identifiers live in the
append-only `mmio_device_type` enum in `mmio_plugin.h`. The emulator treats
these values as opaque, but the stable registry lets plugins and guest software
agree on device semantics. Experimental or private plugins may use unregistered
values, taking care to avoid guest-visible type collisions.
