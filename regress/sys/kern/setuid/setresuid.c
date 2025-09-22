/*	$OpenBSD: setresuid.c,v 1.3 2021/12/15 18:42:38 anton Exp $	*/
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

	if ((pw = getpwnam(_SETUID_REGRESS_USER)) == NULL)
		err(1, "unknown user \"%s\"", _SETUID_REGRESS_USER);

	uid = getuid();

	if (setresuid(pw->pw_uid, -1, -1) == -1)
		err(1, "setuid 0");
	checkuids(pw->pw_uid, uid, uid, "0");

	/* should only respond to setuid upon exec */
	if (issetugid())
		errx(1, "process incorrectly marked as issetugid()");

	if (read_kproc_pid(&kproc, getpid()) == -1)
		err(1, "kproc read failed");

	if (!(kproc.p_psflags & PS_SUGID))
		errx(1, "PS_SUGID not set");
	if (kproc.p_psflags & PS_SUGIDEXEC)
		errx(1, "PS_SUGIDEXEC incorrectly set");

	/* we should be able to roll back our uids for now */
	if (setresuid(uid, -1, -1) == -1)
		err(1, "setuid 1");
	checkuids(uid, uid, uid, "1");

	if (setresuid(-1, pw->pw_uid, -1) == -1)
		err(1, "setuid 2");
	checkuids(uid, pw->pw_uid, uid, "2");

	/* we should be able to roll back our uids for now */
	if (setresuid(-1, uid, -1) == -1)
		err(1, "setuid 3");
	checkuids(uid, uid, uid, "3");

	/*
	 * after changing our saved uid, we should be able to change
	 * our real and effective uids to that of our saved uid,
	 * but not to anything else
	 */

	if (setresuid(-1, -1, pw->pw_uid) == -1)
		err(1, "setuid 4");
	checkuids(uid, uid, pw->pw_uid, "4");

	if (setresuid(pw->pw_uid, -1, -1) == -1)
		err(1, "setuid 5");
	checkuids(pw->pw_uid, uid, pw->pw_uid, "5");

	if (setresuid(-1, pw->pw_uid, -1) == -1)
		err(1, "setuid 6");
	checkuids(pw->pw_uid, pw->pw_uid, pw->pw_uid, "6");

	if (setresuid(uid, -1, -1) != -1)
		errx(1, "incorrectly capable of setting real uid");
	checkuids(pw->pw_uid, pw->pw_uid, pw->pw_uid, "7");

	if (setresuid(-1, uid, -1) != -1)
		errx(1, "incorrectly capable of setting effective uid");
	checkuids(pw->pw_uid, pw->pw_uid, pw->pw_uid, "9");

	if (setresuid(-1, -1, uid) != -1)
		errx(1, "incorrectly capable of setting saved uid");
	checkuids(pw->pw_uid, pw->pw_uid, pw->pw_uid, "9");

	/* sanity-check use of -1 as noop */
	if (setresuid(-1, -1, -1) == -1)
		errx(1, "-1 not properly recognized as noop");
	checkuids(pw->pw_uid, pw->pw_uid, pw->pw_uid, "9");

	exit(0);
}
