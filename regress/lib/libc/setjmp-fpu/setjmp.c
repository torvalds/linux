/*	$OpenBSD: setjmp.c,v 1.4 2020/01/13 14:58:38 bluhm Exp $	*/

int	test_setjmp(void);

int
main(int argc, char *argv[])
{
	return test_setjmp();
}

#define	SETJMP(env, savemask)	setjmp(env)
#define	LONGJMP(env, val)	longjmp(env, val)
#define	TEST_SETJMP		test_setjmp
#define	JMP_BUF			jmp_buf

#include "setjmp-fpu.c"
