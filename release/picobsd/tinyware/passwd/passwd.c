/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1988, 1993, 1994
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

#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1988, 1993, 1994\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
#if 0
static char sccsid[] = "@(#)passwd.c	8.3 (Berkeley) 4/2/94";
#endif
static const char rcsid[] =
  "$FreeBSD$";
#endif /* not lint */

#include <sys/types.h>

#include <err.h>
#include <errno.h>
#include <libutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef YP
#include <pwd.h>
#include <pw_yp.h>
#include <rpcsvc/yp.h>
int __use_yp = 0;
int yp_errno = YP_TRUE;
extern int yp_passwd( char * );
#endif

#include "extern.h"

static void usage(void);

int use_local_passwd = 0;

int
main(argc, argv)
	int argc;
	char **argv;
{
	int ch;
	char *uname;

#ifdef YP
#define OPTIONS "d:h:lysfo"
#else
#define OPTIONS "l"
#endif

#ifdef YP
	int res = 0;

	if (strstr(argv[0], "yppasswd")) __use_yp = 1;
#endif

	while ((ch = getopt(argc, argv, OPTIONS)) != -1) {
		switch (ch) {
		case 'l':		/* change local password file */
			use_local_passwd = 1;
			break;
#ifdef	YP
		case 'y':			/* Change NIS password */
			__use_yp = 1;
			break;
		case 'd':			/* Specify NIS domain. */
#ifdef PARANOID
			if (!getuid()) {
#endif
				yp_domain = optarg;
				if (yp_server == NULL)
					yp_server = "localhost";
#ifdef PARANOID
			} else {
				warnx("only the super-user may use the -d flag");
			}
#endif
			break;
		case 'h':			/* Specify NIS server. */
#ifdef PARANOID
			if (!getuid()) {
#endif
				yp_server = optarg;
#ifdef PARANOID
			} else {
				warnx("only the super-user may use the -h flag");
			}
#endif
			break;
		case 'o':
			force_old++;
			break;
#endif
		default:
		case '?':
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	if ((uname = getlogin()) == NULL)
		err(1, "getlogin");

	switch(argc) {
	case 0:
		break;
	case 1:
		uname = argv[0];
		break;
	default:
		usage();
	}

#ifdef YP
	/*
	 * If NIS is turned on in the password database, use it, else punt.
	 */
	res = use_yp(uname, 0, 0);
	if (res == USER_YP_ONLY) {
		if (!use_local_passwd) {
			exit(yp_passwd(uname));
		} else {
			/*
			 * Reject -l flag if NIS is turned on and the user
			 * doesn't exist in the local password database.
			 */
			errx(1, "unknown local user: %s", uname);
		}
	} else if (res == USER_LOCAL_ONLY) {
		/*
		 * Reject -y flag if user only exists locally.
		 */
		if (__use_yp)
			errx(1, "unknown NIS user: %s", uname);
	} else if (res == USER_YP_AND_LOCAL) {
		if (!use_local_passwd && (yp_in_pw_file || __use_yp))
			exit(yp_passwd(uname));
	}
#endif

	exit(local_passwd(uname));
}

static void
usage()
{

#ifdef	YP
	(void)fprintf(stderr,
		"usage: passwd [-l] [-y] [-o] [-d domain [-h host]] [user]\n");
#else
	(void)fprintf(stderr, "usage: passwd [-l] user\n");
#endif
	exit(1);
}
