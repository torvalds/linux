/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 Mike Barcroft <mike@FreeBSD.org>
 * Copyright (c) 2008 Bjoern A. Zeeb <bz@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/jail.h>
#include <sys/socket.h>
#include <sys/sysctl.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <err.h>
#include <errno.h>
#include <jail.h>
#include <limits.h>
#include <login_cap.h>
#include <paths.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern char **environ;

static void	get_user_info(const char *username, const struct passwd **pwdp,
    login_cap_t **lcapp);
static void	usage(void);

int
main(int argc, char *argv[])
{
	int jid;
	login_cap_t *lcap = NULL;
	int ch, clean, uflag, Uflag;
	char *cleanenv;
	const struct passwd *pwd = NULL;
	const char *username, *shell, *term;

	ch = clean = uflag = Uflag = 0;
	username = NULL;

	while ((ch = getopt(argc, argv, "lnu:U:")) != -1) {
		switch (ch) {
		case 'l':
			clean = 1;
			break;
		case 'n':
			/* Specified name, now unused */
			break;
		case 'u':
			username = optarg;
			uflag = 1;
			break;
		case 'U':
			username = optarg;
			Uflag = 1;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc < 1)
		usage();
	if (uflag && Uflag)
		usage();
	if (uflag || (clean && !Uflag))
		/* User info from the home environment */
		get_user_info(username, &pwd, &lcap);

	/* Attach to the jail */
	jid = jail_getid(argv[0]);
	if (jid < 0)
		errx(1, "%s", jail_errmsg);
	if (jail_attach(jid) == -1)
		err(1, "jail_attach(%d)", jid);
	if (chdir("/") == -1)
		err(1, "chdir(): /");

	/* Set up user environment */
	if (clean || username != NULL) {
		if (Uflag)
			/* User info from the jail environment */
			get_user_info(username, &pwd, &lcap);
		if (clean) {
			term = getenv("TERM");
			cleanenv = NULL;
			environ = &cleanenv;
			setenv("PATH", "/bin:/usr/bin", 1);
			if (term != NULL)
				setenv("TERM", term, 1);
		}
		if (setgid(pwd->pw_gid) != 0)
			err(1, "setgid");
		if (setusercontext(lcap, pwd, pwd->pw_uid, username
		    ? LOGIN_SETALL & ~LOGIN_SETGROUP & ~LOGIN_SETLOGIN
		    : LOGIN_SETPATH | LOGIN_SETENV) != 0)
			err(1, "setusercontext");
		login_close(lcap);
		setenv("USER", pwd->pw_name, 1);
		setenv("HOME", pwd->pw_dir, 1);
		setenv("SHELL",
		    *pwd->pw_shell ? pwd->pw_shell : _PATH_BSHELL, 1);
		if (clean && chdir(pwd->pw_dir) < 0)
			err(1, "chdir: %s", pwd->pw_dir);
		endpwent();
	}

	/* Run the specified command, or the shell */
	if (argc > 1) {
		if (execvp(argv[1], argv + 1) < 0)
			err(1, "execvp: %s", argv[1]);
	} else {
		if (!(shell = getenv("SHELL")))
			shell = _PATH_BSHELL;
		if (execlp(shell, shell, "-i", NULL) < 0)
			err(1, "execlp: %s", shell);
	}
	exit(0);
}

static void
get_user_info(const char *username, const struct passwd **pwdp,
    login_cap_t **lcapp)
{
	uid_t uid;
	const struct passwd *pwd;

	errno = 0;
	if (username) {
		pwd = getpwnam(username);
		if (pwd == NULL) {
			if (errno)
				err(1, "getpwnam: %s", username);
			else
				errx(1, "%s: no such user", username);
		}
	} else {
		uid = getuid();
		pwd = getpwuid(uid);
		if (pwd == NULL) {
			if (errno)
				err(1, "getpwuid: %d", uid);
			else
				errx(1, "unknown uid: %d", uid);
		}
	}
	*pwdp = pwd;
	*lcapp = login_getpwclass(pwd);
	if (*lcapp == NULL)
		err(1, "getpwclass: %s", pwd->pw_name);
	if (initgroups(pwd->pw_name, pwd->pw_gid) < 0)
		err(1, "initgroups: %s", pwd->pw_name);
}

static void
usage(void)
{

	fprintf(stderr, "%s\n",
	    "usage: jexec [-l] [-u username | -U username] jail [command ...]");
	exit(1);
}
