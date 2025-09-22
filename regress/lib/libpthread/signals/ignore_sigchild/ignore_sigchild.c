/*	$OpenBSD: ignore_sigchild.c,v 1.3 2016/03/17 19:40:43 krw Exp $	*/
/*
 * Federico G. Schwindt <fgsch@openbsd.org>, 2011. Public Domain.
 */
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include "test.h"

int
main(int argc, char **argv)
{
	int status;
	pid_t pid;

	ASSERT(signal(SIGCHLD, SIG_IGN) != SIG_ERR);

	switch ((pid = fork())) {
	case -1:
		PANIC("fork");
	case 0:
		execl("/usr/bin/false", "false", (char *)NULL);
		PANIC("execlp");
	default:
		break;
	}

	CHECKe(alarm(2));
	ASSERT(wait(&status) == -1);
	ASSERT(errno == ECHILD);
	SUCCEED;
}
