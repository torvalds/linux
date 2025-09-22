/*	$OpenBSD: kqueue-fork.c,v 1.3 2016/09/20 23:05:27 bluhm Exp $	*/
/*
 *	Written by Artur Grabowski <art@openbsd.org> 2002 Public Domain
 */

#include <sys/types.h>
#include <sys/event.h>
#include <sys/wait.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "main.h"

int
check_inheritance(void)
{
	int kq, status;

	ASS((kq = kqueue()) >= 0,
	    warn("kqueue"));

	/*
	 * Check if the kqueue is properly closed on fork().
	 */

	switch (fork()) {
	case -1:
		err(1, "fork");
	case 0:
		if (close(kq) < 0)
			_exit(0);
		warnx("fork didn't close kqueue");
		_exit(1);
	}
	if (wait(&status) < 0)
		err(1, "wait");

	if (!WIFEXITED(status))
		errx(1, "child didn't exit?");

	close(kq);
	return (WEXITSTATUS(status) != 0);
}
