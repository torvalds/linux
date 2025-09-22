/*	$OpenBSD: stackjmp.c,v 1.3 2013/12/21 05:45:04 guenther Exp $	*/
/*
 * Written by Matthew Dempsky, 2012.
 * Public domain.
 */

#include <assert.h>
#include <setjmp.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

static sigjmp_buf jb;
static char buf[SIGSTKSZ];
static volatile int handled;

static int
isaltstack()
{
	stack_t os;
	assert(sigaltstack(NULL, &os) == 0);
	return (os.ss_flags & SS_ONSTACK) != 0;
}

static void
inthandler(int signo)
{
	assert(isaltstack());
	handled = 1;
	siglongjmp(jb, 1);
}

int
main()
{
	struct sigaction sa;
	stack_t stack;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = inthandler;
	sa.sa_flags = SA_ONSTACK;
	assert(sigaction(SIGINT, &sa, NULL) == 0);

	memset(&stack, 0, sizeof(stack));
	stack.ss_sp = buf;
	stack.ss_size = sizeof(buf);
	stack.ss_flags = 0;
	assert(sigaltstack(&stack, NULL) == 0);

	assert(!isaltstack());
	sigsetjmp(jb, 1);
	assert(!isaltstack());
	if (!handled) {
		kill(getpid(), SIGINT);
		assert(0); /* Shouldn't reach here. */
	}

	return (0);
}
