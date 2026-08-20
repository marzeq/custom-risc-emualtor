#include "../mmio_plugin.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>

#define DEFAULT_VDISK_SIZE (16ull * 1024ull * 1024ull)

enum {
  VDISK_POSITION = 0,
  VDISK_DATA = 8,
  VDISK_CAPACITY = 16,
  VDISK_MMIO_SIZE = 24,
};

static FILE* disk;
static uint64_t disk_capacity;
static uint64_t disk_position;

static bool seek_to_position(void) {
  if (!disk || disk_position > (uint64_t)LONG_MAX) {
    return false;
  }
  return fseek(disk, (long)disk_position, SEEK_SET) == 0;
}

[[gnu::constructor]]
static void simple_vdisk_start(void) {
  disk = fopen("vdisk", "r+b");
  if (!disk && errno == ENOENT) {
    disk = fopen("vdisk", "w+b");
  }
  if (!disk) {
    fprintf(stderr, "simple vdisk: could not open or create './vdisk'\n");
    return;
  }

  if (fseek(disk, 0, SEEK_END) != 0) {
    fprintf(stderr, "simple vdisk: could not seek './vdisk'\n");
    fclose(disk);
    disk = NULL;
    return;
  }

  long size = ftell(disk);
  if (size < 0) {
    fprintf(stderr, "simple vdisk: could not determine './vdisk' size\n");
    fclose(disk);
    disk = NULL;
    return;
  }

  if (size == 0) {
    if (fseek(disk, (long)(DEFAULT_VDISK_SIZE - 1), SEEK_SET) != 0 ||
        fputc(0, disk) == EOF || fflush(disk) != 0) {
      fprintf(stderr, "simple vdisk: could not initialize './vdisk'\n");
      fclose(disk);
      disk = NULL;
      return;
    }
    disk_capacity = DEFAULT_VDISK_SIZE;
  } else {
    disk_capacity = (uint64_t)size;
  }
}

[[gnu::destructor]]
static void simple_vdisk_stop(void) {
  if (disk) {
    fclose(disk);
  }
}

MMIO_PLUGIN_READ(simple_vdisk_read, register_offset, result) {
  if (register_offset == VDISK_POSITION) {
    *result = disk_position;
    return true;
  }
  if (register_offset == VDISK_CAPACITY) {
    *result = disk_capacity;
    return disk != NULL;
  }
  if (register_offset != VDISK_DATA || !disk ||
      disk_position > disk_capacity || disk_capacity - disk_position < 8 ||
      !seek_to_position()) {
    return false;
  }

  unsigned char bytes[8];
  if (fread(bytes, 1, sizeof(bytes), disk) != sizeof(bytes)) {
    return false;
  }

  *result = (uint64_t)bytes[0]
    | ((uint64_t)bytes[1] << 8)
    | ((uint64_t)bytes[2] << 16)
    | ((uint64_t)bytes[3] << 24)
    | ((uint64_t)bytes[4] << 32)
    | ((uint64_t)bytes[5] << 40)
    | ((uint64_t)bytes[6] << 48)
    | ((uint64_t)bytes[7] << 56);
  disk_position += 8;
  return true;
}

MMIO_PLUGIN_WRITE(simple_vdisk_write, register_offset, new_value) {
  if (register_offset == VDISK_POSITION) {
    if (!disk || new_value > disk_capacity) {
      return false;
    }
    disk_position = new_value;
    return true;
  }
  if (register_offset != VDISK_DATA || !disk ||
      disk_position > disk_capacity || disk_capacity - disk_position < 8 ||
      !seek_to_position()) {
    return false;
  }

  unsigned char bytes[8] = {
    (unsigned char)new_value,
    (unsigned char)(new_value >> 8),
    (unsigned char)(new_value >> 16),
    (unsigned char)(new_value >> 24),
    (unsigned char)(new_value >> 32),
    (unsigned char)(new_value >> 40),
    (unsigned char)(new_value >> 48),
    (unsigned char)(new_value >> 56),
  };
  if (fwrite(bytes, 1, sizeof(bytes), disk) != sizeof(bytes) ||
      fflush(disk) != 0) {
    return false;
  }

  disk_position += 8;
  return true;
}

MMIO_PLUGIN_DEFINE({
  .type = MMIO_DEVICE_SIMPLE_VDISK,
  .size = VDISK_MMIO_SIZE,
  .read = simple_vdisk_read,
  .write = simple_vdisk_write,
  .name = "simple vdisk",
})
