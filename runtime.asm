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

  // IVT starts after 2 MMIO addresses
  addi r3, r1, 16
  mov ivt, r3

  // Fill all 256 vectors with panic
  mov   r4, r3          // current IVT entry
  loadi r5, 256         // remaining entries
  loadi r6, panic       // default handler

.init_ivt:
  store r6, r4, 0
  addi  r4, r4, 8
  subi  r5, r5, 1
  jne   .init_ivt

  // sp = ram_end
  add sp, r1, r2

  // reserve some initial stack space
  subi sp, sp, 64

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
  je .stdio_device

  jmp .next_device

.stdio_device:
  // first stdio device wins
  cmpi r15, 0
  jne .next_device

  load r15, r2, 8          // MMIO base address
  jmp .next_device

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
  load8 r1, r2, 0
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


getline: // (r1 = buffer, r2 = sizeof buffer) -> void
  push r5
  push r6
  push r7

  mov r5, r1          // buffer start
  mov r6, r1          // current write pointer
  mov r7, r2          // remaining buffer size

  // Cannot store anything
  cmpi r7, 0
  je .discard_line

  // Reserve space for terminating NUL
  subi r7, r7, 1

.read_loop:
  call getch          // r0 = char

  // Backspace?
  cmpi r0, 0x7f
  je .handle_backspace

  cmpi r0, 0x08
  je .handle_backspace

  cmpi r0, '\n'
  je .finish

  // Buffer full?
  cmpi r7, 0
  je .discard_rest

  store8 r0, r6, 0

  // Echo character
  mov r1, r0
  call putch

  addi r6, r6, 1
  subi r7, r7, 1

  jmp .read_loop

.handle_backspace:
  // Already at start of buffer?
  cmp r6, r5
  je .read_loop

  subi r6, r6, 1
  addi r7, r7, 1

  call backspace

  jmp .read_loop

.discard_rest:
  // Echo but dont store
  mov r1, r0
  call putch

.discard_loop:
  call getch

  cmpi r0, '\n'
  je .finish

  mov r1, r0
  call putch

  jmp .discard_loop

.finish:
  // Echo newline
  mov r1, r0
  call putch

  loadi r0, 0
  store8 r0, r6, 0

  pop r7
  pop r6
  pop r5
  ret

.discard_line:
  call getch
  cmpi r0, '\n'
  jne .discard_line

  pop r7
  pop r6
  pop r5
  ret



panic:
  int 0xff
  halt
