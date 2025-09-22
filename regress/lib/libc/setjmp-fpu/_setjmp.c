/*	$OpenBSD: _setjmp.c,v 1.4 2020/01/13 14:58:38 bluhm Exp $	*/

int	test__setjmp(void);

int
main(int argc, char *argv[])
{
	return test__setjmp();
}

#define	SETJMP(env, savemask)	_setjmp(env)
#define	LONGJMP(env, val)	_longjmp(env, val)
#define	TEST_SETJMP		test__setjmp
#define	JMP_BUF			jmp_buf

#include "setjmp-fpu.c"
