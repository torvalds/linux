/*	$OpenBSD: setjmp-signal.c,v 1.4 2017/07/27 12:31:09 bluhm Exp $	*/
/*
 *	Written by Artur Grabowski <art@openbsd.org> 2002 Public Domain.
 */

#include <setjmp.h>
#include <signal.h>

jmp_buf jb;

void
segv_handler(int signum)
{
	longjmp(jb, 1);
}

int
main()
{
	signal(SIGSEGV, segv_handler);
	if (setjmp(jb) == 0) {
		*((volatile int *)0L) = 0;
		return (1);
	}
	return (0);
}
