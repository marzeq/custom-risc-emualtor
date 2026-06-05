#include "runtime.asm"

main:
  load r0, machine_info, 48 // devices count
  load r1, machine_info, 56 // device header size
  load r2, machine_info, 64 // device list address
  mul  r3, r0, r1           // total size of device list
  add  r4, r2, r3           // end of device list
find_device:
  cmp  r2, r4
  jge  fail

  load r3, r2, 0       // device type
  cmpi r3, 1           // 1 = stdio
  je   found_stdio_dev
  add  r2, r2, r1      // move to next device
  jmp  find_device
found_stdio_dev:
  load   r3, r2, 8 // device address
loop:
  load r0, r3, 0   // read a character
  cmpi r0, 0x7f    // check if it's backspace
  je   backspace
putch:
  store r0, r3, 0  // write the character back
  jmp loop
backspace:
  loadi r0, 1      // tell device to perform backspace
  store r0, r3, 8
  jmp loop
fail:
  halt
