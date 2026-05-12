main: main.c
	clear && gcc main.c -g -O2 && ./a.out ./hello.bf
asmTest: test.asm
	fasm test.asm && hexdump test.bin -v

clean:
	rm test.bin a.out
