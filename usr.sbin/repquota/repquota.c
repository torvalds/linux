/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1980, 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Robert Elz at The University of Melbourne.
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

#if 0
#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1980, 1990, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
static char sccsid[] = "@(#)repquota.c	8.1 (Berkeley) 6/6/93";
#endif /* not lint */
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Quota report
 */
#include <sys/param.h>
#include <sys/mount.h>

#include <ufs/ufs/quota.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <fstab.h>
#include <grp.h>
#include <libutil.h>
#include <pwd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Let's be paranoid about block size */
#if 10 > DEV_BSHIFT
#define dbtokb(db) \
	((off_t)(db) >> (10-DEV_BSHIFT))
#elif 10 < DEV_BSHIFT
#define dbtokb(db) \
	((off_t)(db) << (DEV_BSHIFT-10))
#else
#define dbtokb(db)	(db)
#endif

#define max(a,b) ((a) >= (b) ? (a) : (b))

static const char *qfextension[] = INITQFNAMES;

struct fileusage {
	struct	fileusage *fu_next;
	u_long	fu_id;
	char	fu_name[1];
	/* actually bigger */
};
#define FUHASH 1024	/* must be power of two */
static struct fileusage *fuhead[MAXQUOTAS][FUHASH];
static struct fileusage *lookup(u_long, int);
static struct fileusage *addid(u_long, int, char *);
static u_long highid[MAXQUOTAS]; /* highest addid()'ed identifier per type */

static int	vflag;		/* verbose */
static int	aflag;		/* all filesystems */
static int	nflag;		/* display user/group by id */
static int	hflag;		/* display in human readable format */

int oneof(char *, char *[], int);
int repquota(struct fstab *, int);
char *timeprt(time_t);
static void prthumanval(int64_t bytes);
static void usage(void);

int
main(int argc, char *argv[])
{
	struct fstab *fs;
	struct passwd *pw;
	struct group *gr;
	int ch, gflag = 0, uflag = 0, errs = 0;
	long i, argnum, done = 0;

	while ((ch = getopt(argc, argv, "aghnuv")) != -1) {
		switch(ch) {
		case 'a':
			aflag++;
			break;
		case 'g':
			gflag++;
			break;
		case 'h':
			hflag++;
			break;
		case 'n':
			nflag++;
			break;
		case 'u':
			uflag++;
			break;
		case 'v':
			vflag++;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc == 0 && !aflag)
		usage();
	if (!gflag && !uflag) {
		if (aflag)
			gflag++;
		uflag++;
	}
	if (gflag && !nflag) {
		setgrent();
		while ((gr = getgrent()) != 0)
			(void) addid((u_long)gr->gr_gid, GRPQUOTA, gr->gr_name);
		endgrent();
	}
	if (uflag && !nflag) {
		setpwent();
		while ((pw = getpwent()) != 0)
			(void) addid((u_long)pw->pw_uid, USRQUOTA, pw->pw_name);
		endpwent();
	}
	setfsent();
	while ((fs = getfsent()) != NULL) {
		if (strcmp(fs->fs_vfstype, "ufs"))
			continue;
		if (aflag) {
			if (gflag)
				errs += repquota(fs, GRPQUOTA);
			if (uflag)
				errs += repquota(fs, USRQUOTA);
			continue;
		}
		if ((argnum = oneof(fs->fs_file, argv, argc)) >= 0 ||
		    (argnum = oneof(fs->fs_spec, argv, argc)) >= 0) {
			done |= 1 << argnum;
			if (gflag)
				errs += repquota(fs, GRPQUOTA);
			if (uflag)
				errs += repquota(fs, USRQUOTA);
		}
	}
	endfsent();
	for (i = 0; i < argc; i++)
		if ((done & (1 << i)) == 0)
			warnx("%s not found in fstab", argv[i]);
	exit(errs);
}

static void
usage(void)
{
	fprintf(stderr, "%s\n%s\n",
		"usage: repquota [-h] [-v] [-g] [-n] [-u] -a",
		"       repquota [-h] [-v] [-g] [-n] [-u] filesystem ...");
	exit(1);
}

int
repquota(struct fstab *fs, int type)
{
	struct fileusage *fup;
	struct quotafile *qf;
	u_long id, maxid;
	struct dqblk dqbuf;
	static int multiple = 0;

	if ((qf = quota_open(fs, type, O_RDONLY)) == NULL) {
		if (vflag && !aflag) {
			if (multiple++)
				printf("\n");
			fprintf(stdout, "*** No %s quotas on %s (%s)\n",
			    qfextension[type], fs->fs_file, fs->fs_spec);
			return(1);
		}
		return(0);
	}
	if (multiple++)
		printf("\n");
	if (vflag)
		fprintf(stdout, "*** Report for %s quotas on %s (%s)\n",
		    qfextension[type], fs->fs_file, fs->fs_spec);
	printf("%*s           Block  limits                    File  limits\n",
		max(MAXLOGNAME - 1, 10), " ");
	printf("User%*s  used   soft   hard  grace     used    soft    hard  grace\n",
		max(MAXLOGNAME - 1, 10), " ");
	maxid = quota_maxid(qf);
	for (id = 0; id <= maxid; id++) {
		if (quota_read(qf, &dqbuf, id) != 0)
			break;
		if (dqbuf.dqb_curinodes == 0 && dqbuf.dqb_curblocks == 0)
			continue;
		if ((fup = lookup(id, type)) == 0)
			fup = addid(id, type, (char *)0);
		printf("%-*s ", max(MAXLOGNAME - 1, 10), fup->fu_name);
		printf("%c%c", 
		    dqbuf.dqb_bsoftlimit &&
		    dqbuf.dqb_curblocks >=
		    dqbuf.dqb_bsoftlimit ? '+' : '-',
		    dqbuf.dqb_isoftlimit &&
		    dqbuf.dqb_curinodes >=
		    dqbuf.dqb_isoftlimit ? '+' : '-');
		prthumanval(dqbuf.dqb_curblocks);
		prthumanval(dqbuf.dqb_bsoftlimit);
		prthumanval(dqbuf.dqb_bhardlimit);
		printf(" %6s",
		    dqbuf.dqb_bsoftlimit &&
		    dqbuf.dqb_curblocks >=
		    dqbuf.dqb_bsoftlimit ?
		    timeprt(dqbuf.dqb_btime) : "-");
		printf("  %7ju %7ju %7ju %6s\n",
		    (uintmax_t)dqbuf.dqb_curinodes,
		    (uintmax_t)dqbuf.dqb_isoftlimit,
		    (uintmax_t)dqbuf.dqb_ihardlimit,
		    dqbuf.dqb_isoftlimit &&
		    dqbuf.dqb_curinodes >=
		    dqbuf.dqb_isoftlimit ?
		    timeprt(dqbuf.dqb_itime) : "-");
	}
	quota_close(qf);
	return (0);
}

static void
prthumanval(int64_t blocks)
{
	char buf[7];
	int flags;

	if (!hflag) {
		printf(" %6ju", (uintmax_t)dbtokb(blocks));
		return;
	}
	flags = HN_NOSPACE | HN_DECIMAL;
	if (blocks != 0)
		flags |= HN_B;
	humanize_number(buf, sizeof(buf) - (blocks < 0 ? 0 : 1),
	    dbtob(blocks), "", HN_AUTOSCALE, flags);
	(void)printf("%7s", buf);
}

/*
 * Check to see if target appears in list of size cnt.
 */
int
oneof(char *target, char *list[], int cnt)
{
	int i;

	for (i = 0; i < cnt; i++)
		if (strcmp(target, list[i]) == 0)
			return (i);
	return (-1);
}

/*
 * Routines to manage the file usage table.
 *
 * Lookup an id of a specific type.
 */
struct fileusage *
lookup(u_long id, int type)
{
	struct fileusage *fup;

	for (fup = fuhead[type][id & (FUHASH-1)]; fup != 0; fup = fup->fu_next)
		if (fup->fu_id == id)
			return (fup);
	return ((struct fileusage *)0);
}

/*
 * Add a new file usage id if it does not already exist.
 */
struct fileusage *
addid(u_long id, int type, char *name)
{
	struct fileusage *fup, **fhp;
	int len;

	if ((fup = lookup(id, type)))
		return (fup);
	if (name)
		len = strlen(name);
	else
		len = 10;
	if ((fup = (struct fileusage *)calloc(1, sizeof(*fup) + len)) == NULL)
		errx(1, "out of memory for fileusage structures");
	fhp = &fuhead[type][id & (FUHASH - 1)];
	fup->fu_next = *fhp;
	*fhp = fup;
	fup->fu_id = id;
	if (id > highid[type])
		highid[type] = id;
	if (name) {
		bcopy(name, fup->fu_name, len + 1);
	} else {
		sprintf(fup->fu_name, "%lu", id);
	}
	return (fup);
}

/*
 * Calculate the grace period and return a printable string for it.
 */
char *
timeprt(time_t seconds)
{
	time_t hours, minutes;
	static char buf[20];
	static time_t now;

	if (now == 0)
		time(&now);
	if (now > seconds) {
		strlcpy(buf, "none", sizeof (buf));
		return (buf);
	}
	seconds -= now;
	minutes = (seconds + 30) / 60;
	hours = (minutes + 30) / 60;
	if (hours >= 36) {
		sprintf(buf, "%lddays", (long)(hours + 12) / 24);
		return (buf);
	}
	if (minutes >= 60) {
		sprintf(buf, "%2ld:%ld", (long)minutes / 60,
		    (long)minutes % 60);
		return (buf);
	}
	sprintf(buf, "%2ld", (long)minutes);
	return (buf);
}
