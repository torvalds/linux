/*	$OpenBSD: sgidexec.c,v 1.3 2021/12/15 18:42:38 anton Exp $	*/
/*
 *	Written by Bret Stephen Lambert <blambert@openbsd.org> 2014
 *	Public Domain.
 */

#include <sys/types.h>
#include <sys/signal.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/wait.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pwd.h>
#include <unistd.h>

#include "setuid_regress.h"

int
main(int argc, char *argv[])
{
	struct kinfo_proc	 kproc;
	char			*toexec = NULL;

	if (argc > 1) {
		argv++;
		if ((toexec = strdup(argv[0])) == NULL)
			err(1, "strdup");
	}

	if (read_kproc_pid(&kproc, getpid()) == -1)
		err(1, "kproc read failed");

	if (issetugid() != 1)
		errx(1, "process not marked as issetugid()");

	if (!(kproc.p_psflags & PS_SUGID))
		errx(1, "PS_SUGID not set");
	if (!(kproc.p_psflags & PS_SUGIDEXEC))
		errx(1, "PS_SUGIDEXEC not set");

	if (toexec != NULL)
		if (execv(toexec, argv) == -1)
			err(1, "exec of %s failed", toexec);
	free(toexec);

	exit(0);
}
