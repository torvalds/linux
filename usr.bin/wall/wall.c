/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1988, 1990, 1993
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

#include <sys/cdefs.h>

__FBSDID("$FreeBSD$");

#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1988, 1990, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif

#ifndef lint
static const char sccsid[] = "@(#)wall.c	8.2 (Berkeley) 11/16/93";
#endif

/*
 * This program is not related to David Wall, whose Stanford Ph.D. thesis
 * is entitled "Mechanisms for Broadcast and Selective Broadcast".
 */

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/uio.h>

#include <ctype.h>
#include <err.h>
#include <grp.h>
#include <locale.h>
#include <paths.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <utmpx.h>
#include <wchar.h>
#include <wctype.h>

#include "ttymsg.h"

static void makemsg(char *);
static void usage(void);

static struct wallgroup {
	struct wallgroup *next;
	char		*name;
	gid_t		gid;
} *grouplist;
static int nobanner;
static int mbufsize;
static char *mbuf;

static int
ttystat(char *line)
{
	struct stat sb;
	char ttybuf[MAXPATHLEN];

	(void)snprintf(ttybuf, sizeof(ttybuf), "%s%s", _PATH_DEV, line);
	if (stat(ttybuf, &sb) == 0) {
		return (0);
	} else
		return (-1);
}

int
main(int argc, char *argv[])
{
	struct iovec iov;
	struct utmpx *utmp;
	int ch;
	int ingroup;
	struct wallgroup *g;
	struct group *grp;
	char **np;
	const char *p;
	struct passwd *pw;

	(void)setlocale(LC_CTYPE, "");

	while ((ch = getopt(argc, argv, "g:n")) != -1)
		switch (ch) {
		case 'n':
			/* undoc option for shutdown: suppress banner */
			if (geteuid() == 0)
				nobanner = 1;
			break;
		case 'g':
			g = (struct wallgroup *)malloc(sizeof *g);
			g->next = grouplist;
			g->name = optarg;
			g->gid = -1;
			grouplist = g;
			break;
		case '?':
		default:
			usage();
		}
	argc -= optind;
	argv += optind;
	if (argc > 1)
		usage();

	for (g = grouplist; g; g = g->next) {
		grp = getgrnam(g->name);
		if (grp != NULL)
			g->gid = grp->gr_gid;
		else
			warnx("%s: no such group", g->name);
	}

	makemsg(*argv);

	iov.iov_base = mbuf;
	iov.iov_len = mbufsize;
	/* NOSTRICT */
	while ((utmp = getutxent()) != NULL) {
		if (utmp->ut_type != USER_PROCESS)
			continue;
		if (ttystat(utmp->ut_line) != 0)
			continue;
		if (grouplist) {
			ingroup = 0;
			pw = getpwnam(utmp->ut_user);
			if (!pw)
				continue;
			for (g = grouplist; g && ingroup == 0; g = g->next) {
				if (g->gid == (gid_t)-1)
					continue;
				if (g->gid == pw->pw_gid)
					ingroup = 1;
				else if ((grp = getgrgid(g->gid)) != NULL) {
					for (np = grp->gr_mem; *np; np++) {
						if (strcmp(*np, utmp->ut_user) == 0) {
							ingroup = 1;
							break;
						}
					}
				}
			}
			if (ingroup == 0)
				continue;
		}
		if ((p = ttymsg(&iov, 1, utmp->ut_line, 60*5)) != NULL)
			warnx("%s", p);
	}
	exit(0);
}

static void
usage(void)
{
	(void)fprintf(stderr, "usage: wall [-g group] [file]\n");
	exit(1);
}

void
makemsg(char *fname)
{
	int cnt;
	wchar_t ch;
	struct tm *lt;
	struct passwd *pw;
	struct stat sbuf;
	time_t now;
	FILE *fp;
	int fd;
	char hostname[MAXHOSTNAMELEN], tmpname[64];
	wchar_t *p, *tmp, lbuf[256], codebuf[13];
	const char *tty;
	const char *whom;
	gid_t egid;

	(void)snprintf(tmpname, sizeof(tmpname), "%s/wall.XXXXXX", _PATH_TMP);
	if ((fd = mkstemp(tmpname)) == -1 || !(fp = fdopen(fd, "r+")))
		err(1, "can't open temporary file");
	(void)unlink(tmpname);

	if (!nobanner) {
		tty = ttyname(STDERR_FILENO);
		if (tty == NULL)
			tty = "no tty";

		if (!(whom = getlogin()))
			whom = (pw = getpwuid(getuid())) ? pw->pw_name : "???";
		(void)gethostname(hostname, sizeof(hostname));
		(void)time(&now);
		lt = localtime(&now);

		/*
		 * all this stuff is to blank out a square for the message;
		 * we wrap message lines at column 79, not 80, because some
		 * terminals wrap after 79, some do not, and we can't tell.
		 * Which means that we may leave a non-blank character
		 * in column 80, but that can't be helped.
		 */
		(void)fwprintf(fp, L"\r%79s\r\n", " ");
		(void)swprintf(lbuf, sizeof(lbuf)/sizeof(wchar_t),
		    L"Broadcast Message from %s@%s",
		    whom, hostname);
		(void)fwprintf(fp, L"%-79.79S\007\007\r\n", lbuf);
		(void)swprintf(lbuf, sizeof(lbuf)/sizeof(wchar_t),
		    L"        (%s) at %d:%02d %s...", tty,
		    lt->tm_hour, lt->tm_min, lt->tm_zone);
		(void)fwprintf(fp, L"%-79.79S\r\n", lbuf);
	}
	(void)fwprintf(fp, L"%79s\r\n", " ");

	if (fname) {
		egid = getegid();
		setegid(getgid());
		if (freopen(fname, "r", stdin) == NULL)
			err(1, "can't read %s", fname);
		if (setegid(egid) != 0)
			err(1, "setegid failed");
	}
	cnt = 0;
	while (fgetws(lbuf, sizeof(lbuf)/sizeof(wchar_t), stdin)) {
		for (p = lbuf; (ch = *p) != L'\0'; ++p, ++cnt) {
			if (ch == L'\r') {
				putwc(L'\r', fp);
				cnt = 0;
				continue;
			} else if (ch == L'\n') {
				for (; cnt < 79; ++cnt)
					putwc(L' ', fp);
				putwc(L'\r', fp);
				putwc(L'\n', fp);
				break;
			}
			if (cnt == 79) {
				putwc(L'\r', fp);
				putwc(L'\n', fp);
				cnt = 0;
			}
			if (iswprint(ch) || iswspace(ch) || ch == L'\a' || ch == L'\b') {
				putwc(ch, fp);
			} else {
				(void)swprintf(codebuf, sizeof(codebuf)/sizeof(wchar_t), L"<0x%X>", ch);
				for (tmp = codebuf; *tmp != L'\0'; ++tmp) {
					putwc(*tmp, fp);
					if (++cnt == 79) {
						putwc(L'\r', fp);
						putwc(L'\n', fp);
						cnt = 0;
					}
				}
				--cnt;
			}
		}
	}
	(void)fwprintf(fp, L"%79s\r\n", " ");
	rewind(fp);

	if (fstat(fd, &sbuf))
		err(1, "can't stat temporary file");
	mbufsize = sbuf.st_size;
	if (!(mbuf = malloc((u_int)mbufsize)))
		err(1, "out of memory");
	if ((int)fread(mbuf, sizeof(*mbuf), mbufsize, fp) != mbufsize)
		err(1, "can't read temporary file");
	fclose(fp);
}
