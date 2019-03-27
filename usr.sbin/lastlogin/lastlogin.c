/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1996 John M. Vinopal
 * Copyright (c) 2018 Philip Paeps
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed for the NetBSD Project
 *	by John M. Vinopal.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
#ifndef lint
__RCSID("$FreeBSD$");
__RCSID("$NetBSD: lastlogin.c,v 1.4 1998/02/03 04:45:35 perry Exp $");
#endif

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <utmpx.h>

#include <libxo/xo.h>

	int	main(int, char **);
static	void	output(struct utmpx *);
static	void	usage(void);
static int	utcmp_user(const void *, const void *);

static int	order = 1;
static const char *file = NULL;
static int	(*utcmp)(const void *, const void *) = utcmp_user;

static int
utcmp_user(const void *u1, const void *u2)
{

	return (order * strcmp(((const struct utmpx *)u1)->ut_user,
	    ((const struct utmpx *)u2)->ut_user));
}

static int
utcmp_time(const void *u1, const void *u2)
{
	time_t t1, t2;

	t1 = ((const struct utmpx *)u1)->ut_tv.tv_sec;
	t2 = ((const struct utmpx *)u2)->ut_tv.tv_sec;
	return (t1 < t2 ? order : t1 > t2 ? -order : 0);
}

int
main(int argc, char *argv[])
{
	int	ch, i, ulistsize;
	struct utmpx *u, *ulist;

	argc = xo_parse_args(argc, argv);
	if (argc < 0)
		exit(1);

	while ((ch = getopt(argc, argv, "f:rt")) != -1) {
		switch (ch) {
		case 'f':
			file = optarg;
			break;
		case 'r':
			order = -1;
			break;
		case 't':
			utcmp = utcmp_time;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	xo_open_container("lastlogin-information");
	xo_open_list("lastlogin");

	if (argc > 0) {
		/* Process usernames given on the command line. */
		for (i = 0; i < argc; i++) {
			if (setutxdb(UTXDB_LASTLOGIN, file) != 0)
				xo_err(1, "failed to open lastlog database");
			if ((u = getutxuser(argv[i])) == NULL) {
				xo_warnx("user '%s' not found", argv[i]);
				continue;
			}
			output(u);
			endutxent();
		}
	} else {
		/* Read all lastlog entries, looking for active ones. */
		if (setutxdb(UTXDB_LASTLOGIN, file) != 0)
			xo_err(1, "failed to open lastlog database");
		ulist = NULL;
		ulistsize = 0;
		while ((u = getutxent()) != NULL) {
			if (u->ut_type != USER_PROCESS)
				continue;
			if ((ulistsize % 16) == 0) {
				ulist = realloc(ulist,
				    (ulistsize + 16) * sizeof(struct utmpx));
				if (ulist == NULL)
					xo_err(1, "malloc");
			}
			ulist[ulistsize++] = *u;
		}
		endutxent();

		qsort(ulist, ulistsize, sizeof(struct utmpx), utcmp);
		for (i = 0; i < ulistsize; i++)
			output(&ulist[i]);
	}

	xo_close_list("lastlogin");
	xo_close_container("lastlogin-information");
	xo_finish();

	exit(0);
}

/* Duplicate the output of last(1) */
static void
output(struct utmpx *u)
{
	time_t t = u->ut_tv.tv_sec;

	xo_open_instance("lastlogin");
	xo_emit("{:user/%-10s/%s} {:tty/%-8s/%s} {:from/%-22.22s/%s}",
		u->ut_user, u->ut_line, u->ut_host);
	xo_attr("seconds", "%lu", (unsigned long)t);
	xo_emit(" {:login-time/%.24s/%.24s}\n", ctime(&t));
	xo_close_instance("lastlogin");
}

static void
usage(void)
{
	xo_error("usage: lastlogin [-f file] [-rt] [user ...]\n");
	xo_finish();
	exit(1);
}
