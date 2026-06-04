#include "runtime.asm"

main:
  // device_count
  load r0, machine_info, 48

  // device_list
  load r1, machine_info, 56

find_device:
  cmpi r0, 0
  je not_found

  // device.type
  load r2, r1, 0

  cmpi r2, 1              // DEVICE_STDIO
  je found_stdio

  // advance to next device
  load r5, r1, 8          // device.self_size
  add r1, r1, r5

  subi r0, r0, 1
  jmp find_device

found_stdio:
  // r3 = device.start
  load r3, r1, 16

#define PRINT_CHAR(c) \
  loadi r6, c;        \
  storeb r6, r3, 0

  PRINT_CHAR('H')
  PRINT_CHAR('e')
  PRINT_CHAR('l')
  PRINT_CHAR('l')
  PRINT_CHAR('o')
  PRINT_CHAR(' ')
  PRINT_CHAR('w')
  PRINT_CHAR('o')
  PRINT_CHAR('r')
  PRINT_CHAR('l')
  PRINT_CHAR('d')
  PRINT_CHAR('\n')

#undef PRINT_CHAR

  ret

not_found:
  ret
