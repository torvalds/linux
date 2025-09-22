/*	$OpenBSD: sigsetjmp.c,v 1.4 2020/01/13 14:58:38 bluhm Exp $	*/

int	test_sigsetjmp(void);

int
main(int argc, char *argv[])
{
	return test_sigsetjmp();
}

#define	SETJMP(env, savemask)	sigsetjmp(env, savemask)
#define	LONGJMP(env, val)	siglongjmp(env, val)
#define	TEST_SETJMP		test_sigsetjmp
#define	JMP_BUF			sigjmp_buf

#include "setjmp-fpu.c"
