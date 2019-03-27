/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1988, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 2002 Networks Associates Technology, Inc.
 * All rights reserved.
 *
 * Portions of this software were developed for the FreeBSD Project by
 * ThinkSec AS and NAI Labs, the Security Research Division of Network
 * Associates, Inc.  under DARPA/SPAWAR contract N66001-01-C-8035
 * ("CBOSS"), as part of the DARPA CHATS research program.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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

#if 0
#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1988, 1993, 1994\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
static char sccsid[] = "@(#)chpass.c	8.4 (Berkeley) 4/2/94";
#endif /* not lint */
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>

#include <err.h>
#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef YP
#include <ypclnt.h>
#endif

#include <pw_scan.h>
#include <libutil.h>

#include "chpass.h"

int master_mode;

static void	baduser(void);
static void	usage(void);

int
main(int argc, char *argv[])
{
	enum { NEWSH, LOADENTRY, EDITENTRY, NEWPW, NEWEXP } op;
	struct passwd lpw, *old_pw, *pw;
	int ch, pfd, tfd;
	const char *password;
	char *arg = NULL, *cryptpw;
	uid_t uid;
#ifdef YP
	struct ypclnt *ypclnt;
	const char *yp_domain = NULL, *yp_host = NULL;
#endif

	pw = old_pw = NULL;
	op = EDITENTRY;
#ifdef YP
	while ((ch = getopt(argc, argv, "a:p:s:e:d:h:loy")) != -1)
#else
	while ((ch = getopt(argc, argv, "a:p:s:e:")) != -1)
#endif
		switch (ch) {
		case 'a':
			op = LOADENTRY;
			arg = optarg;
			break;
		case 's':
			op = NEWSH;
			arg = optarg;
			break;
		case 'p':
			op = NEWPW;
			arg = optarg;
			break;
		case 'e':
			op = NEWEXP;
			arg = optarg;
			break;
#ifdef YP
		case 'd':
			yp_domain = optarg;
			break;
		case 'h':
			yp_host = optarg;
			break;
		case 'l':
		case 'o':
		case 'y':
			/* compatibility */
			break;
#endif
		case '?':
		default:
			usage();
		}

	argc -= optind;
	argv += optind;

	if (argc > 1)
		usage();

	uid = getuid();

	if (op == EDITENTRY || op == NEWSH || op == NEWPW || op == NEWEXP) {
		if (argc == 0) {
			if ((pw = getpwuid(uid)) == NULL)
				errx(1, "unknown user: uid %lu",
				    (unsigned long)uid);
		} else {
			if ((pw = getpwnam(*argv)) == NULL)
				errx(1, "unknown user: %s", *argv);
			if (uid != 0 && uid != pw->pw_uid)
				baduser();
		}

		/* Make a copy for later verification */
		if ((pw = pw_dup(pw)) == NULL ||
		    (old_pw = pw_dup(pw)) == NULL)
			err(1, "pw_dup");
	}

#ifdef YP
	if (pw != NULL && (pw->pw_fields & _PWF_SOURCE) == _PWF_NIS) {
		ypclnt = ypclnt_new(yp_domain, "passwd.byname", yp_host);
		master_mode = (ypclnt != NULL &&
		    ypclnt_connect(ypclnt) != -1 &&
		    ypclnt_havepasswdd(ypclnt) == 1);
		ypclnt_free(ypclnt);
	} else
#endif
	master_mode = (uid == 0);

	if (op == NEWSH) {
		/* protect p_shell -- it thinks NULL is /bin/sh */
		if (!arg[0])
			usage();
		if (p_shell(arg, pw, (ENTRY *)NULL) == -1)
			exit(1);
	}

	if (op == NEWEXP) {
		if (uid)	/* only root can change expire */
			baduser();
		if (p_expire(arg, pw, (ENTRY *)NULL) == -1)
			exit(1);
	}

	if (op == LOADENTRY) {
		if (uid)
			baduser();
		pw = &lpw;
		old_pw = NULL;
		if (!__pw_scan(arg, pw, _PWSCAN_WARN|_PWSCAN_MASTER))
			exit(1);
	}

	if (op == NEWPW) {
		if (uid)
			baduser();

		if (strchr(arg, ':'))
			errx(1, "invalid format for password");
		pw->pw_passwd = arg;
	}

	if (op == EDITENTRY) {
		/*
		 * We don't really need pw_*() here, but pw_edit() (used
		 * by edit()) is just too useful...
		 */
		if (pw_init(NULL, NULL))
			err(1, "pw_init()");
		if ((tfd = pw_tmp(-1)) == -1) {
			pw_fini();
			err(1, "pw_tmp()");
		}
		free(pw);
		pw = edit(pw_tempname(), old_pw);
		pw_fini();
		if (pw == NULL)
			err(1, "edit()");
		/* 
		 * pw_equal does not check for crypted passwords, so we
		 * should do it explicitly
		 */
		if (pw_equal(old_pw, pw) && 
		    strcmp(old_pw->pw_passwd, pw->pw_passwd) == 0)
			errx(0, "user information unchanged");
	}

	if (old_pw && !master_mode) {
		password = getpass("Password: ");
		cryptpw = crypt(password, old_pw->pw_passwd);
		if (cryptpw == NULL || strcmp(cryptpw, old_pw->pw_passwd) != 0)
			baduser();
	} else {
		password = "";
	}

	if (old_pw != NULL)
		pw->pw_fields |= (old_pw->pw_fields & _PWF_SOURCE);
	switch (pw->pw_fields & _PWF_SOURCE) {
#ifdef YP
	case _PWF_NIS:
		ypclnt = ypclnt_new(yp_domain, "passwd.byname", yp_host);
		if (ypclnt == NULL) {
			warnx("ypclnt_new failed");
			exit(1);
		}
		if (ypclnt_connect(ypclnt) == -1 ||
		    ypclnt_passwd(ypclnt, pw, password) == -1) {
			warnx("%s", ypclnt->error);
			ypclnt_free(ypclnt);
			exit(1);
		}
		ypclnt_free(ypclnt);
		errx(0, "NIS user information updated");
#endif /* YP */
	case 0:
	case _PWF_FILES:
		if (pw_init(NULL, NULL))
			err(1, "pw_init()");
		if ((pfd = pw_lock()) == -1) {
			pw_fini();
			err(1, "pw_lock()");
		}
		if ((tfd = pw_tmp(-1)) == -1) {
			pw_fini();
			err(1, "pw_tmp()");
		}
		if (pw_copy(pfd, tfd, pw, old_pw) == -1) {
			pw_fini();
			err(1, "pw_copy");
		}
		if (pw_mkdb(pw->pw_name) == -1) {
			pw_fini();
			err(1, "pw_mkdb()");
		}
		pw_fini();
		errx(0, "user information updated");
		break;
	default:
		errx(1, "unsupported passwd source");
	}
}

static void
baduser(void)
{

	errx(1, "%s", strerror(EACCES));
}

static void
usage(void)
{

	(void)fprintf(stderr,
	    "usage: chpass%s %s [user]\n",
#ifdef YP
	    " [-d domain] [-h host]",
#else
	    "",
#endif
	    "[-a list] [-p encpass] [-s shell] [-e mmm dd yy]");
	exit(1);
}
