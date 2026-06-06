.entry _setup

_setup:
  load r0, machine_info, 0
  cmpi r0, 0
  loadi r0, 0xf324 // invalid firmware version potential error code
  jne panic

  load r0, machine_info, 8
  cmpi r0, 72
  loadi r0, 0xf325 // invalid machine info size potential error code
  jne panic

  // r1 = ram_start
  load r1, machine_info, 16

  // r2 = ram_size
  load r2, machine_info, 24

  // sp = ram_end
  add sp, r1, r2

  call main
  halt
panic:
  dump_regs
  halt

// LIBRARY FUNCTIONS
