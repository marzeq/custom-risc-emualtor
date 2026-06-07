.entry _setup

_setup:
  load r0, machine_info, 0
  cmpi r0, 0
  loadi r0, 0xf324 // invalid firmware version
  jne panic

  load r0, machine_info, 8
  cmpi r0, 72
  loadi r0, 0xf325 // invalid machine info size
  jne panic

  // r1 = ram_start
  load r1, machine_info, 16

  // r2 = ram_size
  load r2, machine_info, 24

  // sp = ram_end
  add sp, r1, r2

  // reserve some initial stack space
  subi sp, sp, 16

  // r15 = stdio MMIO base (0 = not found)
  loadi r15, 0

  load r0, machine_info, 48 // device count
  load r1, machine_info, 56 // device header size
  load r2, machine_info, 64 // device list address

  mul r3, r0, r1
  add r4, r2, r3           // end of device list
  jmp .find_device

.call_main:
  cmpi r15, 0
  je panic
  call main
  halt

.find_device:
  cmp r2, r4
  jge .call_main

  load r5, r2, 0           // device type
  cmpi r5, 1               // 1 = stdio
  jne .next_device

  // first stdio device wins
  cmpi r15, 0
  jne .next_device

  load r15, r2, 8          // MMIO base address

.next_device:
  add r2, r2, r1
  jmp .find_device


getch:
  cmpi r15, 0
  je panic

  load r0, r15, 0
  ret


putch:
  cmpi r15, 0
  je panic

  store r1, r15, 0
  ret


puts:
  mov r2, r1

.loop:
  loadb r1, r2, 0
  cmpi  r1, 0
  je    .done

  call  putch

  addi  r2, r2, 1
  jmp   .loop

.done:
  ret


backspace:
  cmpi r15, 0
  je panic

  loadi r1, 1
  store r1, r15, 8
  ret


panic:
  dump_regs
  halt
