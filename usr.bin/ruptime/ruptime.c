/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1983, 1993, 1994
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

#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1983, 1993, 1994\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
static const char sccsid[] = "@(#)ruptime.c	8.2 (Berkeley) 4/5/94";
#endif /* not lint */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>

#include <protocols/rwhod.h>

#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static struct hs {
	struct	whod hs_wd;
	int	hs_nusers;
} *hs;
#define	LEFTEARTH(h)	(now - (h) > 4*24*60*60)
#define	ISDOWN(h)	(now - (h)->hs_wd.wd_recvtime > 11 * 60)
#define	WHDRSIZE	__offsetof(struct whod, wd_we)

static size_t nhosts;
static time_t now;
static int rflg = 1;
static DIR *dirp;

static int	 hscmp(const void *, const void *);
static char	*interval(time_t, const char *);
static int	 iwidth(int);
static int	 lcmp(const void *, const void *);
static void	 ruptime(const char *, int, int (*)(const void *, const void *));
static int	 tcmp(const void *, const void *);
static int	 ucmp(const void *, const void *);
static void	 usage(void);

int
main(int argc, char *argv[])
{
	int (*cmp)(const void *, const void *);
	int aflg, ch;

	aflg = 0;
	cmp = hscmp;
	while ((ch = getopt(argc, argv, "alrut")) != -1)
		switch (ch) {
		case 'a':
			aflg = 1;
			break;
		case 'l':
			cmp = lcmp;
			break;
		case 'r':
			rflg = -1;
			break;
		case 't':
			cmp = tcmp;
			break;
		case 'u':
			cmp = ucmp;
			break;
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	if (chdir(_PATH_RWHODIR) || (dirp = opendir(".")) == NULL)
		err(1, "%s", _PATH_RWHODIR);

	ruptime(*argv, aflg, cmp);
	while (*argv++ != NULL) {
		if (*argv == NULL)
			break;
		ruptime(*argv, aflg, cmp);
	}
	exit(0);
}

static char *
interval(time_t tval, const char *updown)
{
	static char resbuf[32];
	int days, hours, minutes;

	if (tval < 0) {
		(void)snprintf(resbuf, sizeof(resbuf), "%s      ??:??", updown);
		return (resbuf);
	}
	/* Round to minutes. */
	minutes = (tval + (60 - 1)) / 60;
	hours = minutes / 60;
	minutes %= 60;
	days = hours / 24;
	hours %= 24;
	if (days)
		(void)snprintf(resbuf, sizeof(resbuf),
		    "%s %4d+%02d:%02d", updown, days, hours, minutes);
	else
		(void)snprintf(resbuf, sizeof(resbuf),
		    "%s      %2d:%02d", updown, hours, minutes);
	return (resbuf);
}

/* Width to print a small nonnegative integer. */
static int
iwidth(int w)
{
	if (w < 10)
		return (1);
	if (w < 100)
		return (2);
	if (w < 1000)
		return (3);
	if (w < 10000)
		return (4);
	return (5);
}

#define	HS(a)	((const struct hs *)(a))

/* Alphabetical comparison. */
static int
hscmp(const void *a1, const void *a2)
{
	return (rflg *
	    strcmp(HS(a1)->hs_wd.wd_hostname, HS(a2)->hs_wd.wd_hostname));
}

/* Load average comparison. */
static int
lcmp(const void *a1, const void *a2)
{
	if (ISDOWN(HS(a1)))
		if (ISDOWN(HS(a2)))
			return (tcmp(a1, a2));
		else
			return (rflg);
	else if (ISDOWN(HS(a2)))
		return (-rflg);
	else
		return (rflg *
		   (HS(a2)->hs_wd.wd_loadav[0] - HS(a1)->hs_wd.wd_loadav[0]));
}

static void
ruptime(const char *host, int aflg, int (*cmp)(const void *, const void *))
{
	struct hs *hsp;
	struct whod *wd;
	struct whoent *we;
	struct dirent *dp;
	int fd, hostnamewidth, i, loadavwidth[3], userswidth, w;
	size_t hspace;
	ssize_t cc;

	rewinddir(dirp);
	hsp = NULL;
	hostnamewidth = 0;
	loadavwidth[0] = 4;
	loadavwidth[1] = 4;
	loadavwidth[2] = 4;
	userswidth = 1;
	(void)time(&now);
	for (nhosts = hspace = 0; (dp = readdir(dirp)) != NULL;) {
		if (dp->d_ino == 0 || strncmp(dp->d_name, "whod.", 5) != 0)
			continue;
		if ((fd = open(dp->d_name, O_RDONLY, 0)) < 0) {
			warn("%s", dp->d_name);
			continue;
		}

		if (nhosts == hspace) {
			if ((hs =
			    realloc(hs, (hspace += 40) * sizeof(*hs))) == NULL)
				err(1, NULL);
			hsp = hs + nhosts;
		}

		wd = &hsp->hs_wd;
		cc = read(fd, wd, sizeof(*wd));
		(void)close(fd);
		if (cc < (ssize_t)WHDRSIZE)
			continue;

		if (host != NULL && strcasecmp(wd->wd_hostname, host) != 0)
			continue;
		if (LEFTEARTH(wd->wd_recvtime))
			continue;

		if (hostnamewidth < (int)strlen(wd->wd_hostname))
			hostnamewidth = (int)strlen(wd->wd_hostname);
		for (i = 0; i < 3; i++) {
			w = iwidth(wd->wd_loadav[i] / 100) + 3;
			if (loadavwidth[i] < w)
				loadavwidth[i] = w;
		}

		for (hsp->hs_nusers = 0, we = &wd->wd_we[0];
		    (char *)(we + 1) <= (char *)wd + cc; we++)
			if (aflg || we->we_idle < 3600)
				++hsp->hs_nusers;
		if (userswidth < iwidth(hsp->hs_nusers))
			userswidth = iwidth(hsp->hs_nusers);
		++hsp;
		++nhosts;
	}
	if (nhosts == 0) {
		if (host == NULL)
			errx(1, "no hosts in %s", _PATH_RWHODIR);
		else
			warnx("host %s not in %s", host, _PATH_RWHODIR);
	}

	qsort(hs, nhosts, sizeof(hs[0]), cmp);
	w = userswidth + loadavwidth[0] + loadavwidth[1] + loadavwidth[2];
	if (hostnamewidth + w > 41)
		hostnamewidth = 41 - w;	/* limit to 79 cols */
	for (i = 0; i < (int)nhosts; i++) {
		hsp = &hs[i];
		wd = &hsp->hs_wd;
		if (ISDOWN(hsp)) {
			(void)printf("%-*.*s  %s\n",
			    hostnamewidth, hostnamewidth, wd->wd_hostname,
			    interval(now - hsp->hs_wd.wd_recvtime, "down"));
			continue;
		}
		(void)printf(
		    "%-*.*s  %s,  %*d user%s  load %*.2f, %*.2f, %*.2f\n",
		    hostnamewidth, hostnamewidth, wd->wd_hostname,
		    interval((time_t)wd->wd_sendtime -
		        (time_t)wd->wd_boottime, "  up"),
		    userswidth, hsp->hs_nusers,
		    hsp->hs_nusers == 1 ? ", " : "s,",
		    loadavwidth[0], wd->wd_loadav[0] / 100.0,
		    loadavwidth[1], wd->wd_loadav[1] / 100.0,
		    loadavwidth[2], wd->wd_loadav[2] / 100.0);
	}
	free(hs);
	hs = NULL;
}

/* Number of users comparison. */
static int
ucmp(const void *a1, const void *a2)
{
	if (ISDOWN(HS(a1)))
		if (ISDOWN(HS(a2)))
			return (tcmp(a1, a2));
		else
			return (rflg);
	else if (ISDOWN(HS(a2)))
		return (-rflg);
	else
		return (rflg * (HS(a2)->hs_nusers - HS(a1)->hs_nusers));
}

/* Uptime comparison. */
static int
tcmp(const void *a1, const void *a2)
{
	return (rflg * (
		(ISDOWN(HS(a2)) ? HS(a2)->hs_wd.wd_recvtime - now
		    : HS(a2)->hs_wd.wd_sendtime - HS(a2)->hs_wd.wd_boottime)
		-
		(ISDOWN(HS(a1)) ? HS(a1)->hs_wd.wd_recvtime - now
		    : HS(a1)->hs_wd.wd_sendtime - HS(a1)->hs_wd.wd_boottime)
	));
}

static void
usage(void)
{
	(void)fprintf(stderr, "usage: ruptime [-alrtu] [host ...]\n");
	exit(1);
}
