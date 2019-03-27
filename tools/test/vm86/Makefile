# $FreeBSD$

all:	vm86_test

vm86_test:	vm86_test_asm.s vm86_test.c
	$(CC) -Wall -Wextra -g -m32 -O -o vm86_test vm86_test_asm.s vm86_test.c

