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

MMIO_PLUGIN_READ(stdio_read, offset, value) {
  if (offset != 0) {
    return false;
  }

  int ch = getchar();
  *value = ch == EOF ? 0 : (uint64_t)(unsigned char)ch;
  return true;
}

MMIO_PLUGIN_WRITE(stdio_write, offset, value) {
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

MMIO_PLUGIN_DEFINE({
  .type = MMIO_DEVICE_STDIO,
  .size = 16,
  .read = stdio_read,
  .write = stdio_write,
  .name = "stdio device",
})
