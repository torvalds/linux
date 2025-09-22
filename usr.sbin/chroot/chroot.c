/*	$OpenBSD: chroot.c,v 1.14 2015/05/19 16:05:12 millert Exp $	*/

/*
 * Copyright (c) 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/types.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <grp.h>
#include <limits.h>
#include <login_cap.h>
#include <paths.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int		main(int, char **);
__dead void	usage(void);

int
main(int argc, char **argv)
{
	struct group	*grp;
	struct passwd	*pwd;
	login_cap_t	*lc;
	const char	*shell;
	char		*user, *group, *grouplist;
	gid_t		gidlist[NGROUPS_MAX];
	int		ch, ngids;
	int		flags = LOGIN_SETALL & ~(LOGIN_SETLOGIN|LOGIN_SETUSER);

	lc = NULL;
	ngids = 0;
	pwd = NULL;
	user = grouplist = NULL;
	while ((ch = getopt(argc, argv, "g:u:")) != -1) {
		switch(ch) {
		case 'u':
			user = optarg;
			if (*user == '\0')
				usage();
			break;
		case 'g':
			grouplist = optarg;
			if (*grouplist == '\0')
				usage();
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc < 1)
		usage();

	if (user != NULL) {
		if ((pwd = getpwnam(user)) == NULL)
			errx(1, "no such user `%s'", user);
		if ((lc = login_getclass(pwd->pw_class)) == NULL)
			err(1, "unable to get login class for `%s'", user);
	}

	while ((group = strsep(&grouplist, ",")) != NULL) {
		if (*group == '\0')
			continue;

		if (ngids == NGROUPS_MAX)
			errx(1, "too many supplementary groups provided");
		if ((grp = getgrnam(group)) == NULL)
			errx(1, "no such group `%s'", group);
		gidlist[ngids++] = grp->gr_gid;
	}

	if (ngids != 0) {
		if (setgid(gidlist[0]) != 0)
			err(1, "setgid");
		if (setgroups(ngids, gidlist) != 0)
			err(1, "setgroups");
		flags &= ~LOGIN_SETGROUP;
	}
	if (lc != NULL) {
		if (setusercontext(lc, pwd, pwd->pw_uid, flags) == -1)
			err(1, "setusercontext");
	}

	if (chroot(argv[0]) != 0 || chdir("/") != 0)
		err(1, "%s", argv[0]);

	if (pwd != NULL) {
		/* only set login name if we are/can be a session leader */
		if (getsid(0) == getpid() || setsid() != -1)
			setlogin(pwd->pw_name);
		if (setuid(pwd->pw_uid) != 0)
			err(1, "setuid");
	}

	if (argv[1]) {
		execvp(argv[1], &argv[1]);
		err(1, "%s", argv[1]);
	}

	if ((shell = getenv("SHELL")) == NULL || *shell == '\0')
		shell = _PATH_BSHELL;
	execlp(shell, shell, "-i", (char *)NULL);
	err(1, "%s", shell);
	/* NOTREACHED */
}

__dead void
usage(void)
{
	extern char *__progname;

	(void)fprintf(stderr, "usage: %s [-g group,group,...] [-u user] "
	    "newroot [command]\n", __progname);
	exit(1);
}
