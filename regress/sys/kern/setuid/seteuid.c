/*	$OpenBSD: seteuid.c,v 1.3 2021/12/15 18:42:38 anton Exp $	*/
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
	struct passwd		*pw;
	uid_t			 uid;

	uid = getuid();

	if ((pw = getpwnam(_SETUID_REGRESS_USER)) == NULL)
		err(1, "unknown user \"%s\"", _SETUID_REGRESS_USER);

	if (seteuid(pw->pw_uid) == -1)
		err(1, "seteuid 0");

	if (geteuid() != pw->pw_uid)
		errx(1, "mismatched effective uids");

	checkuids(uid, pw->pw_uid, uid, "seteuid");

	/* should only respond to setuid upon exec */
	if (issetugid())
		errx(1, "process incorrectly marked as issetugid()");

	if (read_kproc_pid(&kproc, getpid()) == -1)
		err(1, "kproc read 0 failed");

	if (!(kproc.p_psflags & PS_SUGID))
		errx(1, "PS_SUGID not set");
	if (kproc.p_psflags & PS_SUGIDEXEC)
		errx(1, "PS_SUGIDEXEC incorrectly set");

	/* at this point, we should be able to reset our uid */
	if (seteuid(uid) == -1)
		err(1, "seteuid 1");

	if (read_kproc_pid(&kproc, getpid()) == -1)
		err(1, "kproc read 1 failed");

	if (!(kproc.p_psflags & PS_SUGID))
		errx(1, "PS_SUGID not set");
	if (kproc.p_psflags & PS_SUGIDEXEC)
		errx(1, "PS_SUGIDEXEC incorrectly set");

	exit(0);
}
