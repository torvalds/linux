/*	$OpenBSD: tokenadm.c,v 1.12 2016/03/22 00:06:55 bluhm Exp $	*/

/*-
 * Copyright (c) 1995 Migration Associates Corp. All Rights Reserved
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
 *      This product includes software developed by Berkeley Software Design,
 *      Inc.
 * 4. The name of Berkeley Software Design, Inc.  may not be used to endorse
 *    or promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY BERKELEY SOFTWARE DESIGN, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL BERKELEY SOFTWARE DESIGN, INC. BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	BSDI $From: tokenadm.c,v 1.2 1996/10/17 00:54:28 prb Exp $
 */

#include <sys/signal.h>
#include <sys/resource.h>
#include <sys/time.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <syslog.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>

#include "token.h"
#include "tokendb.h"


typedef enum { LIST, ENABLE, DISABLE, REMOVE, MODECH } what_t;
typedef enum {
	NOBANNER = 0x01,
	TERSE = 0x02,
	ENONLY = 0x04,
	DISONLY = 0x08,
	ONECOL = 0x10,
	REVERSE = 0x20
} how_t;

static	int	force_unlock(char *);
static	int	process_record(char *, unsigned, unsigned);
static	int	process_modes(char *, unsigned, unsigned);
static  void	print_record(TOKENDB_Rec *, how_t);

extern	int
main(int argc, char **argv)
{
	int c, errors;
	u_int emode, dmode, pmode;
	struct rlimit cds;
	what_t what;
	how_t how;
	TOKENDB_Rec tokenrec;

	what = LIST;
	emode = dmode = 0;
	pmode = 0;
	errors = 0;
	how = 0;

	(void)signal(SIGQUIT, SIG_IGN);
	(void)signal(SIGINT, SIG_IGN);
	(void)setpriority(PRIO_PROCESS, 0, 0);

	openlog(NULL, LOG_ODELAY, LOG_AUTH);

	/*
	 * Make sure we never dump core as we might have a
	 * valid user shared-secret in memory.
	 */

	cds.rlim_cur = 0;
	cds.rlim_max = 0;
	if (setrlimit(RLIMIT_CORE, &cds) < 0)
		syslog(LOG_ERR, "couldn't set core dump size to 0: %m");

	if (pledge("stdio rpath wpath cpath fattr flock getpw", NULL) == -1)
		err(1, "pledge");

	if (token_init(argv[0]) < 0) {
		syslog(LOG_ERR, "unknown token type");
		errx(1, "unknown token type");
	}

	while ((c = getopt(argc, argv, "BDERT1dem:r")) != -1)
		switch (c) {
		case 'B':
			if (what != LIST)
				goto usage;
			how |= NOBANNER;
			break;
		case 'T':
			if (what != LIST)
				goto usage;
			how |= TERSE;
			break;
		case '1':
			if (what != LIST)
				goto usage;
			how |= ONECOL;
			break;
		case 'D':
			if (what != LIST)
				goto usage;
			how |= DISONLY;
			break;
		case 'E':
			if (what != LIST)
				goto usage;
			how |= ENONLY;
			break;
		case 'R':
			if (what != LIST)
				goto usage;
			how |= REVERSE;
			break;
		case 'd':
			if (what != LIST || how)
				goto usage;
			what = DISABLE;
			break;
		case 'e':
			if (what != LIST || how)
				goto usage;
			what = ENABLE;
			break;
		case 'r':
			if (what != LIST || emode || dmode || how)
				goto usage;
			what = REMOVE;
			break;
		case 'm':
			if (what == REMOVE || how)
				goto usage;
			if (*optarg == '-') {
				if ((c = token_mode(optarg+1)) == 0)
					errx(1, "%s: unknown mode", optarg+1);
				dmode |= c;
			} else {
				if ((c = token_mode(optarg)) == 0)
					errx(1, "%s: unknown mode", optarg);
				emode |= c;
			}
			break;
		default:
			goto usage;
		}

	if (what == LIST && (dmode || emode))
		what = MODECH;

	if (what == LIST) {
		if ((how & (ENONLY|DISONLY)) == 0)
			how |= ENONLY|DISONLY;
		if (!(how & NOBANNER)) {
			if ((how & (TERSE|ONECOL)) == (TERSE|ONECOL)) {
				printf("User\n");
				printf("----------------\n");
			} else if (how & (TERSE)) {
				printf("User             ");
				printf("User             ");
				printf("User             ");
				printf("User\n");
				printf("---------------- ");
				printf("---------------- ");
				printf("---------------- ");
				printf("----------------\n");
			} else {
				printf("User             Status   Modes\n");
				printf("---------------- -------- -----\n");
			}
		}

		if (optind >= argc) {
			if (tokendb_firstrec(how & REVERSE, &tokenrec))
				exit(0);
			do
				print_record(&tokenrec, how);
			while (tokendb_nextrec(how & REVERSE, &tokenrec) == 0);
			print_record(NULL, how);
			exit(0);
		}
	}

	if (optind >= argc) {
usage:
		fprintf(stderr,
		    "usage: %sadm [-1BDdEeRrT] [-m [-]mode] [user ...]\n",
			tt->name);
		exit(1);
	}

	argv += optind - 1;
	while (*++argv)
		switch (what) {
		case LIST:
			if (tokendb_getrec(*argv, &tokenrec)) {
				printf("%s: no such user\n", *argv);
				break;
			}
			print_record(&tokenrec, how);
			break;
		case REMOVE:
			if (tokendb_delrec(*argv)) {
				warnx("%s: could not remove", *argv);
				errors++;
			}
			break;
		case DISABLE:
			if (process_record(*argv, ~TOKEN_ENABLED, 0)) {
				warnx("%s: could not disable", *argv);
				++errors;
			}
			if (emode || dmode)
				goto modech;
			break;
		case ENABLE:
			if (process_record(*argv, ~TOKEN_ENABLED, TOKEN_ENABLED)) {
				warnx("%s: could not enable", *argv);
				++errors;
			}
			if (emode || dmode)
				goto modech;
			break;
		modech:
		case MODECH:
			if (process_modes(*argv, ~dmode, emode)) {
				warnx("%s: could not change modes", *argv);
				++errors;
			}
			break;
		}

	if (what == LIST)
		print_record(NULL, how);

	exit(errors);
}

/*
 * Process a user record
 */

static	int
process_record(char *username, unsigned and_mask, unsigned or_mask)
{
	int	count = 0;
	TOKENDB_Rec tokenrec;

retry:
	switch (tokendb_lockrec(username, &tokenrec, TOKEN_LOCKED)) {
	case 0:
		tokenrec.flags &= and_mask;
		tokenrec.flags |= or_mask;
		tokenrec.flags &= ~TOKEN_LOCKED;
		if (!tokendb_putrec(username, &tokenrec))
			return (0);
		else
			return (-1);
	case 1:
		sleep(1);
		if (count++ < 60)
			goto retry;
		if (force_unlock(username))
			return (1);
		goto retry;

	case ENOENT:
		warnx("%s: nonexistent user", username);
		return (1);
	default:
		return (-1);
	}
}

static	int
process_modes(char *username, unsigned and_mask, unsigned or_mask)
{
	int	count = 0;
	TOKENDB_Rec tokenrec;

retry:
	switch (tokendb_lockrec(username, &tokenrec, TOKEN_LOCKED)) {
	case 0:
		tokenrec.mode &= and_mask;
		tokenrec.mode |= or_mask;
		/*
		 * When ever we set up for rim mode (even if we are
		 * already set up for it) reset the rim key
		 */
		if (or_mask & TOKEN_RIM)
			memset(tokenrec.rim, 0, sizeof(tokenrec.rim));
		tokenrec.flags &= ~TOKEN_LOCKED;
		if (!tokendb_putrec(username, &tokenrec))
			return (0);
		else
			return (-1);
	case 1:
		sleep(1);
		if (count++ < 60)
			goto retry;
		if (force_unlock(username))
			return (1);
		goto retry;

	case ENOENT:
		warnx("%s: nonexistent user", username);
		return (1);
	default:
		return (-1);
	}
}

/*
 * Force remove a user record-level lock.
 */

static	int
force_unlock(char *username)
{
	TOKENDB_Rec tokenrec;

	if (tokendb_getrec(username, &tokenrec))
		return (-1);

	tokenrec.flags &= ~TOKEN_LOCKED;
	tokenrec.flags &= ~TOKEN_LOGIN;

	if (tokendb_putrec(username, &tokenrec))
		return (1);

	return (0);
}

/*
 * Print a database record according to user a specified format
 */

static	void
print_record(TOKENDB_Rec *rec, how_t how)
{
	static int count = 0;
	int i;

	if (rec == NULL) {
		if ((count & 3) && (how & (TERSE|ONECOL)) == TERSE)
			printf("\n");
		return;
	}

	if (rec->flags & TOKEN_ENABLED) {
		if ((how & ENONLY) == 0)
			return;
	} else {
		if ((how & DISONLY) == 0)
			return;
	}

	switch (how & (TERSE|ONECOL)) {
	case 0:
	case ONECOL:
		printf("%-16s %-8s", rec->uname,
		  rec->flags & TOKEN_ENABLED ? "enabled" : "disabled");

		for (i = 1; i; i <<= 1)
			if (rec->mode & i)
				printf(" %s", token_getmode(i));
		printf("\n");
		break;
	case TERSE:
		if ((count & 3) == 3)
			printf("%s\n", rec->uname);
		else
			printf("%-16s ", rec->uname);
		break;
	case TERSE|ONECOL:
		printf("%s\n", rec->uname);
		break;
	}
	++count;
}
