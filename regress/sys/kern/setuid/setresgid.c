/*	$OpenBSD: setresgid.c,v 1.3 2021/12/15 18:42:38 anton Exp $	*/
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
	gid_t			 gid;

	if ((pw = getpwnam(_SETUID_REGRESS_USER)) == NULL)
		err(1, "unknown user \"%s\"", _SETUID_REGRESS_USER);

	gid = getgid();

	if (setresgid(pw->pw_gid, -1, -1) == -1)
		err(1, "setgid 0");
	checkgids(pw->pw_gid, gid, gid, "0");

	/* should only respond to setgid upon exec */
	if (issetugid())
		errx(1, "process incorrectly marked as issetugid()");

	if (read_kproc_pid(&kproc, getpid()) == -1)
		err(1, "kproc read failed");

	if (!(kproc.p_psflags & PS_SUGID))
		errx(1, "PS_SUGID not set");
	if (kproc.p_psflags & PS_SUGIDEXEC)
		errx(1, "PS_SUGIDEXEC incorrectly set");

	/* we should be able to roll back our gids for now */
	if (setresgid(gid, -1, -1) == -1)
		err(1, "setgid 1");
	checkgids(gid, gid, gid, "1");

	if (setresgid(-1, pw->pw_gid, -1) == -1)
		err(1, "setgid 2");
	checkgids(gid, pw->pw_gid, gid, "2");

	/* we should be able to roll back our gids for now */
	if (setresgid(-1, gid, -1) == -1)
		err(1, "setgid 3");
	checkgids(gid, gid, gid, "3");

	/*
	 * after changing our saved gid and dropping superuser privs,
	 * we should be able to change our real and effective gids to
	 * that of our saved gid, but not to anything else
	 */

	if (setresgid(-1, -1, pw->pw_gid) == -1)
		err(1, "setgid 4");
	checkgids(gid, gid, pw->pw_gid, "4");

	if (setresuid(pw->pw_gid, pw->pw_gid, pw->pw_gid) == -1)
		err(1, "setresuid 4");

	if (setresgid(pw->pw_gid, -1, -1) == -1)
		err(1, "setgid 5");
	checkgids(pw->pw_gid, gid, pw->pw_gid, "5");

	if (setresgid(-1, pw->pw_gid, -1) == -1)
		err(1, "setgid 6");
	checkgids(pw->pw_gid, pw->pw_gid, pw->pw_gid, "6");

	if (setresgid(gid, -1, -1) != -1)
		errx(1, "incorrectly capable of setting real gid");
	checkgids(pw->pw_gid, pw->pw_gid, pw->pw_gid, "7");

	if (setresgid(-1, gid, -1) != -1)
		errx(1, "incorrectly capable of setting effective gid");
	checkgids(pw->pw_gid, pw->pw_gid, pw->pw_gid, "9");

	if (setresgid(-1, -1, gid) != -1)
		errx(1, "incorrectly capable of setting saved gid");
	checkgids(pw->pw_gid, pw->pw_gid, pw->pw_gid, "9");

	/* sanity-check use of -1 as noop */
	if (setresgid(-1, -1, -1) == -1)
		errx(1, "-1 not properly recognized as noop");
	checkgids(pw->pw_gid, pw->pw_gid, pw->pw_gid, "9");

	exit(0);
}
