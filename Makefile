cc := clang
cflags := -Wall -Wextra -Werror -std=c23 -Wswitch-enum

all: emulator asm

emulator: emulator.c isa.h
	$(cc) $(cflags) -o emulator emulator.c

asm: assembler.c isa.h
	$(cc) $(cflags) -o asm assembler.c

.PHONY: clean
clean:
	rm -f main asm
