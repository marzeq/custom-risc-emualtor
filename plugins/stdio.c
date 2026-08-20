#include "../mmio_plugin.h"

#include <stdio.h>
#include <termios.h>
#include <unistd.h>

static struct termios saved_termios;
static bool termios_was_changed;

[[gnu::constructor]]
static void stdio_start(void) {
  if (!isatty(STDIN_FILENO) || tcgetattr(STDIN_FILENO, &saved_termios) != 0) {
    return;
  }

  struct termios raw = saved_termios;
  raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
  if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0) {
    termios_was_changed = true;
  }
}

[[gnu::destructor]]
static void stdio_stop(void) {
  if (termios_was_changed) {
    tcsetattr(STDIN_FILENO, TCSANOW, &saved_termios);
  }
}

static bool stdio_read(uint64_t offset, uint64_t* value) {
  if (offset != 0) {
    return false;
  }

  int ch = getchar();
  *value = ch == EOF ? 0 : (uint64_t)(unsigned char)ch;
  return true;
}

static bool stdio_write(uint64_t offset, uint64_t value) {
  if (offset == 0) {
    return putchar((int)(value & 0xffu)) != EOF;
  }

  if (offset != 8) {
    return false;
  }

  if (putchar('\b') == EOF) {
    return false;
  }
  if (value != 0 && (putchar(' ') == EOF || putchar('\b') == EOF)) {
    return false;
  }
  return true;
}

static const mmio_plugin_descriptor descriptor = {
  .type = 1,
  .size = 16,
  .read = stdio_read,
  .write = stdio_write,
  .name = "stdio device",
};

const mmio_plugin_descriptor* mmio_plugin_get_descriptor(
  uint64_t abi_version
) {
  return abi_version == MMIO_PLUGIN_ABI_VERSION ? &descriptor : NULL;
}
