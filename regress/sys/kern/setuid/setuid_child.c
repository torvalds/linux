/*	$OpenBSD: setuid_child.c,v 1.3 2021/12/15 18:42:38 anton Exp $	*/
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
	pid_t			 pid;
	int			 status;

	if ((pw = getpwnam(_SETUID_REGRESS_USER)) == NULL)
		err(1, "unknown user \"%s\"", _SETUID_REGRESS_USER);

	if (setuid(pw->pw_uid) == -1)
		err(1, "setuid");

	switch ((pid = fork())) {

	default:
		waitpid(pid, &status, 0);
		if (WIFSIGNALED(status))
			errx(1, "child exited due to signal %d",
			    WTERMSIG(status));
		else if (WEXITSTATUS(status) != 0)
			errx(1, "child exited with status %d",
			    WEXITSTATUS(status));
		break;

	case 0:
		/*
		 * From the setuid man page:
		 *   The setuid() function sets the real and effective user IDs
		 *   and the saved set-user-ID of the current process
		 */
		checkuids(pw->pw_uid, pw->pw_uid, pw->pw_uid, "setuid child");

		/* should only respond to setuid upon exec */
		if (issetugid())
			errx(1, "child incorrectly marked as issetugid()");

		if (read_kproc_pid(&kproc, getpid()) == -1)
			err(1, "kproc read failed");

		if (!(kproc.p_psflags & PS_SUGID))
			errx(1, "PS_SUGID not set");
		if (kproc.p_psflags & PS_SUGIDEXEC)
			errx(1, "PS_SUGIDEXEC incorrectly set");

		break;

	case -1:
		err(1, "fork");
		/* NOTREACHED */
	}

	exit(0);
}
