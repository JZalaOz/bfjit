main: main.c
	gcc main.c -g -O2 -o main
asmTest: test.asm
	fasm test.asm
