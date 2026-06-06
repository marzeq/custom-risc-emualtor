cc := clang
cflags := -Wall -Wextra -Werror -std=c23 -Wswitch-enum -g

all: emulator asm

emulator: emulator.c isa.h
	$(cc) $(cflags) -o emulator emulator.c

debug-emulator: emulator.c isa.h
	$(cc) $(cflags) -o debug-emulator emulator.c -DDEBUG

asm: assembler.c isa.h
	$(cc) $(cflags) -o asm assembler.c

.PHONY: clean
clean:
	rm -f main asm

.PHONY: dump-example
dump-example: asm
	./asm example.asm /tmp/example.bin && xxd -g1 /tmp/example.bin && rm -f /tmp/example.bin

.PHONY: run-example
run-example: emulator asm
	./asm example.asm /tmp/example.bin && ./emulator /tmp/example.bin && rm -f /tmp/example.bin
