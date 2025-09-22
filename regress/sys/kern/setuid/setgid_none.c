/*	$OpenBSD: setgid_none.c,v 1.3 2021/12/15 18:42:38 anton Exp $	*/
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
#include <pwd.h>
#include <unistd.h>

#include "setuid_regress.h"

int
main(int argc, const char *argv[])
{
	struct kinfo_proc	 kproc;
	gid_t			 gid;

	gid = getgid();

	checkgids(gid, gid, gid, "checkgid");

	/* should only respond to setgid upon exec */
	if (issetugid())
		errx(1, "process incorrectly marked as issetugid()");

	if (read_kproc_pid(&kproc, getpid()) == -1)
		err(1, "kproc read failed");

	if (kproc.p_psflags & PS_SUGID)
		errx(1, "PS_SUGID incorrectly set");
	if (kproc.p_psflags & PS_SUGIDEXEC)
		errx(1, "PS_SUGIDEXEC incorrectly set");

	exit(0);
}
