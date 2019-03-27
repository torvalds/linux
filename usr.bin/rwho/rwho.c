/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1983, 1993 The Regents of the University of California.
 * Copyright (c) 2013 Mariusz Zaborski <oshogbo@FreeBSD.org>
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

#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1983, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
#if 0
static char sccsid[] = "@(#)rwho.c	8.1 (Berkeley) 6/6/93";
#endif
#endif /* not lint */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/capsicum.h>
#include <sys/param.h>
#include <sys/file.h>

#include <protocols/rwhod.h>

#include <capsicum_helpers.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <langinfo.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <timeconv.h>
#include <unistd.h>

#define	NUSERS		1000
#define	WHDRSIZE	(ssize_t)(sizeof(wd) - sizeof(wd.wd_we))
/*
 * this macro should be shared with ruptime.
 */
#define	down(w,now)	((now) - (w)->wd_recvtime > 11 * 60)

static DIR	*dirp;
static struct	whod wd;
static int	nusers;
static struct	myutmp {
	char    myhost[sizeof(wd.wd_hostname)];
	int	myidle;
	struct	outmp myutmp;
} myutmp[NUSERS];

static time_t	now;
static int	aflg;

static void usage(void);
static int utmpcmp(const void *, const void *);

int
main(int argc, char *argv[])
{
	int ch;
	struct dirent *dp;
	int width;
	ssize_t cc;
	struct whod *w;
	struct whoent *we;
	struct myutmp *mp;
	cap_rights_t rights;
	int f, n, i;
	int d_first;
	int dfd;
	time_t ct;

	w = &wd;
	(void) setlocale(LC_TIME, "");
	d_first = (*nl_langinfo(D_MD_ORDER) == 'd');

	while ((ch = getopt(argc, argv, "a")) != -1) {
		switch ((char)ch) {
		case 'a':
			aflg = 1;
			break;
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 0)
		usage();

	if (chdir(_PATH_RWHODIR) < 0)
		err(1, "chdir(%s)", _PATH_RWHODIR);
	if ((dirp = opendir(".")) == NULL)
		err(1, "opendir(%s)", _PATH_RWHODIR);
	dfd = dirfd(dirp);
	mp = myutmp;
	cap_rights_init(&rights, CAP_READ, CAP_LOOKUP);
	if (caph_rights_limit(dfd, &rights) < 0)
		err(1, "cap_rights_limit failed: %s", _PATH_RWHODIR);
	/*
	 * Cache files required for time(3) and localtime(3) before entering
	 * capability mode.
	 */
	(void) time(&ct);
	(void) localtime(&ct);
	if (caph_enter() < 0)
		err(1, "cap_enter");
	(void) time(&now);
	cap_rights_init(&rights, CAP_READ);
	while ((dp = readdir(dirp)) != NULL) {
		if (dp->d_ino == 0 || strncmp(dp->d_name, "whod.", 5) != 0)
			continue;
		f = openat(dfd, dp->d_name, O_RDONLY);
		if (f < 0)
			continue;
		if (caph_rights_limit(f, &rights) < 0)
			err(1, "cap_rights_limit failed: %s", dp->d_name);
		cc = read(f, (char *)&wd, sizeof(struct whod));
		if (cc < WHDRSIZE) {
			(void) close(f);
			continue;
		}
		if (down(w, now) != 0) {
			(void) close(f);
			continue;
		}
		cc -= WHDRSIZE;
		we = w->wd_we;
		for (n = cc / sizeof(struct whoent); n > 0; n--) {
			if (aflg == 0 && we->we_idle >= 60 * 60) {
				we++;
				continue;
			}
			if (nusers >= NUSERS)
				errx(1, "too many users");
			mp->myutmp = we->we_utmp;
			mp->myidle = we->we_idle;
			(void) strcpy(mp->myhost, w->wd_hostname);
			nusers++;
			we++;
			mp++;
		}
		(void) close(f);
	}
	qsort((char *)myutmp, nusers, sizeof(struct myutmp), utmpcmp);
	mp = myutmp;
	width = 0;
	for (i = 0; i < nusers; i++) {
		/* append one for the blank and use 8 for the out_line */
		int j;

		j = strlen(mp->myhost) + 1 + sizeof(mp->myutmp.out_line);
		if (j > width)
			width = j;
		mp++;
	}
	mp = myutmp;
	for (i = 0; i < nusers; i++) {
		char buf[BUFSIZ], cbuf[80];
		time_t t;

		t = _int_to_time(mp->myutmp.out_time);
		strftime(cbuf, sizeof(cbuf), d_first ? "%e %b %R" : "%b %e %R",
		    localtime(&t));
		(void) sprintf(buf, "%s:%-.*s", mp->myhost,
		    (int)sizeof(mp->myutmp.out_line), mp->myutmp.out_line);
		printf("%-*.*s %-*s %s",
		    (int)sizeof(mp->myutmp.out_name),
		    (int)sizeof(mp->myutmp.out_name),
		    mp->myutmp.out_name, width, buf, cbuf);
		mp->myidle /= 60;
		if (mp->myidle != 0) {
			if (aflg != 0) {
				if (mp->myidle >= 100 * 60)
					mp->myidle = 100 * 60 - 1;
				if (mp->myidle >= 60)
					printf(" %2d", mp->myidle / 60);
				else
					printf("   ");
			} else {
				printf(" ");
			}
			printf(":%02d", mp->myidle % 60);
		}
		printf("\n");
		mp++;
	}
	exit(0);
}


static void
usage(void)
{

	fprintf(stderr, "usage: rwho [-a]\n");
	exit(1);
}

#define MYUTMP(a) ((const struct myutmp *)(a))

static int
utmpcmp(const void *u1, const void *u2)
{
	int rc;

	rc = strncmp(MYUTMP(u1)->myutmp.out_name, MYUTMP(u2)->myutmp.out_name,
	    sizeof(MYUTMP(u2)->myutmp.out_name));
	if (rc != 0)
		return (rc);
	rc = strcmp(MYUTMP(u1)->myhost, MYUTMP(u2)->myhost);
	if (rc != 0)
		return (rc);
	return (strncmp(MYUTMP(u1)->myutmp.out_line,
	    MYUTMP(u2)->myutmp.out_line, sizeof(MYUTMP(u2)->myutmp.out_line)));
}
