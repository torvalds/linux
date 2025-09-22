/*	$OpenBSD: cansig.c,v 1.2 2021/12/13 16:56:50 deraadt Exp $	*/
/*	Written by Michael Shalayeff, 2008. Public Domain	*/

#include <sys/types.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <err.h>

int
main()
{
	pid_t	pid;
	int	serrno;

	if ((pid = fork()) < 0)
		err(1, "fork");

	/* nun sex */
	if (!pid) {
		if (seteuid(1))
			err(1, "seteuid");
		sleep(3);
		return 0;
	}

	/* monk rock */
	sleep(1);
	/* first see if we can still do it */
	if (kill(pid, 0)) {
		serrno = errno;
		kill(pid, SIGKILL);
		errno = serrno;
		err(1, "kill0");
	}
	if (setreuid(1, 1)) {
		serrno = errno;
		kill(pid, SIGKILL);
		errno = serrno;
		err(1, "seteuid1");
	}
	/* not allowed */
	if (!kill(pid, 0))
		errx(1, "kill1");

	/* we can't collect the remains from the kiddo */
	return 0;
}
