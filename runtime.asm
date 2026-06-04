  jmp _setup

_setup:
  load r0, machine_info, 0
  cmpi r0, 0
  jne invalid_firmware_version

  load r0, machine_info, 8
  cmpi r0, 64
  jne invalid_machine_info_size

  // r1 = ram_start
  load r1, machine_info, 16

  // r2 = ram_size
  load r2, machine_info, 24

  // sp = ram_end
  add sp, r1, r2

  call main
  halt
invalid_firmware_version:
  loadi r0, 0xf324 // invalid firmware version error code
  dump_reg
  halt
invalid_machine_info_size:
  loadi r0, 0xf325 // invalid machine info size error code
  dump_reg
  halt

// LIBRARY FUNCTIONS
