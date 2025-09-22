/*	$OpenBSD: setresgid_saved_exec.c,v 1.3 2021/12/15 18:42:38 anton Exp $	*/
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
	struct passwd		*pw;
	char			*toexec = NULL;
	gid_t			 gid;

	if (argc > 1) {
		argv++;
		if ((toexec = strdup(argv[0])) == NULL)
			err(1, "strdup");
	}

	gid = getgid();

	if ((pw = getpwnam(_SETUID_REGRESS_USER)) == NULL)
		err(1, "unknown user \"%s\"", _SETUID_REGRESS_USER);

	if (setresgid(-1, -1, pw->pw_gid) == -1)
		err(1, "setgid");
	checkgids(gid, gid, pw->pw_gid, "setgid");

	if (issetugid())
		errx(1, "process incorrectly marked as issetugid()");

	if (read_kproc_pid(&kproc, getpid()) == -1)
		err(1, "kproc read failed");

	if (!(kproc.p_psflags & PS_SUGID))
		errx(1, "PS_SUGID not set");
	if (kproc.p_psflags & PS_SUGIDEXEC)
		errx(1, "PS_SUGIDEXEC incorrectly set");

	if (toexec != NULL)
		if (execv(toexec, argv) == -1)
			err(1, "exec of %s failed", toexec);
	free(toexec);

	exit(0);
}
