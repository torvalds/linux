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
static char sccsid[] = "@(#)edquota.c	8.1 (Berkeley) 6/6/93";
#endif /* not lint */
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Disk quota editor.
 */

#include <sys/file.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <ufs/ufs/quota.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fstab.h>
#include <grp.h>
#include <inttypes.h>
#include <libutil.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pathnames.h"

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

const char *qfextension[] = INITQFNAMES;
char tmpfil[] = _PATH_TMP;
int hflag;

struct quotause {
	struct	quotause *next;
	struct	quotafile *qf;
	struct	dqblk dqblk;
	int	flags;
	char	fsname[MAXPATHLEN + 1];
};
#define	FOUND	0x01

int alldigits(const char *s);
int cvtatos(uint64_t, char *, uint64_t *);
char *cvtstoa(uint64_t);
uint64_t cvtblkval(uint64_t, char, const char *);
uint64_t cvtinoval(uint64_t, char, const char *);
int editit(char *);
char *fmthumanvalblks(int64_t);
char *fmthumanvalinos(int64_t);
void freeprivs(struct quotause *);
int getentry(const char *, int);
struct quotause *getprivs(long, int, char *);
void putprivs(long, struct quotause *);
int readprivs(struct quotause *, char *);
int readtimes(struct quotause *, char *);
static void usage(void);
int writetimes(struct quotause *, int, int);
int writeprivs(struct quotause *, int, char *, int);

int
main(int argc, char *argv[])
{
	struct quotause *qup, *protoprivs, *curprivs;
	long id, protoid;
	int i, quotatype, range, tmpfd;
	uid_t startuid, enduid;
	uint64_t lim;
	char *protoname, *cp, *endpt, *oldoptarg;
	int eflag = 0, tflag = 0, pflag = 0, ch;
	char *fspath = NULL;
	char buf[MAXLOGNAME];

	if (argc < 2)
		usage();
	if (getuid())
		errx(1, "permission denied");
	quotatype = USRQUOTA;
	protoprivs = NULL;
	curprivs = NULL;
	protoname = NULL;
	while ((ch = getopt(argc, argv, "ughtf:p:e:")) != -1) {
		switch(ch) {
		case 'f':
			fspath = optarg;
			break;
		case 'p':
			if (eflag) {
				warnx("cannot specify both -e and -p");
				usage();
				/* not reached */
			}
			protoname = optarg;
			pflag++;
			break;
		case 'g':
			quotatype = GRPQUOTA;
			break;
		case 'h':
			hflag++;
			break;
		case 'u':
			quotatype = USRQUOTA;
			break;
		case 't':
			tflag++;
			break;
		case 'e':
			if (pflag) {
				warnx("cannot specify both -e and -p");
				usage();
				/* not reached */
			}
			if ((qup = calloc(1, sizeof(*qup))) == NULL)
				errx(2, "out of memory");
			oldoptarg = optarg;
			for (i = 0, cp = optarg;
			     (cp = strsep(&optarg, ":")) != NULL; i++) {
				if (cp != oldoptarg)
					*(cp - 1) = ':';
				if (i > 0 && !isdigit(*cp)) {
					warnx("incorrect quota specification: "
					    "%s", oldoptarg);
					usage();
					/* Not Reached */
				}
				switch (i) {
				case 0:
					strlcpy(qup->fsname, cp,
					    sizeof(qup->fsname));
					break;
				case 1:
					lim = strtoll(cp, &endpt, 10);
					qup->dqblk.dqb_bsoftlimit =
						cvtblkval(lim, *endpt,
						    "block soft limit");
					continue;
				case 2:
					lim = strtoll(cp, &endpt, 10);
					qup->dqblk.dqb_bhardlimit =
						cvtblkval(lim, *endpt,
						    "block hard limit");
					continue;
				case 3:
					lim = strtoll(cp, &endpt, 10);
					qup->dqblk.dqb_isoftlimit =
						cvtinoval(lim, *endpt,
						    "inode soft limit");
					continue;
				case 4:
					lim = strtoll(cp, &endpt, 10);
					qup->dqblk.dqb_ihardlimit =
						cvtinoval(lim, *endpt,
						    "inode hard limit");
					continue;
				default:
					warnx("incorrect quota specification: "
					    "%s", oldoptarg);
					usage();
					/* Not Reached */
				}
			}
			if (protoprivs == NULL) {
				protoprivs = curprivs = qup;
			} else {
				curprivs->next = qup;
				curprivs = qup;
			}
			eflag++;
			break;
		default:
			usage();
			/* Not Reached */
		}
	}
	argc -= optind;
	argv += optind;
	if (pflag || eflag) {
		if (pflag) {
			if ((protoid = getentry(protoname, quotatype)) == -1)
				exit(1);
			protoprivs = getprivs(protoid, quotatype, fspath);
			if (protoprivs == NULL)
				exit(0);
			for (qup = protoprivs; qup; qup = qup->next) {
				qup->dqblk.dqb_btime = 0;
				qup->dqblk.dqb_itime = 0;
			}
		}
		for (; argc-- > 0; argv++) {
			if (strspn(*argv, "0123456789-") == strlen(*argv) &&
			    (cp = strchr(*argv, '-')) != NULL) {
				*cp++ = '\0';
				startuid = atoi(*argv);
				enduid = atoi(cp);
				if (enduid < startuid)
					errx(1,
	"ending uid (%d) must be >= starting uid (%d) when using uid ranges",
						enduid, startuid);
				range = 1;
			} else {
				startuid = enduid = 0;
				range = 0;
			}
			for ( ; startuid <= enduid; startuid++) {
				if (range)
					snprintf(buf, sizeof(buf), "%d",
					    startuid);
				else
					snprintf(buf, sizeof(buf), "%s",
						*argv);
				if ((id = getentry(buf, quotatype)) < 0)
					continue;
				if (pflag) {
					putprivs(id, protoprivs);
					continue;
				}
				for (qup = protoprivs; qup; qup = qup->next) {
					curprivs = getprivs(id, quotatype,
					    qup->fsname);
					if (curprivs == NULL)
						continue;
					curprivs->dqblk = qup->dqblk;
					putprivs(id, curprivs);
					freeprivs(curprivs);
				}
			}
		}
		if (pflag)
			freeprivs(protoprivs);
		exit(0);
	}
	tmpfd = mkostemp(tmpfil, O_CLOEXEC);
	fchown(tmpfd, getuid(), getgid());
	if (tflag) {
		if ((protoprivs = getprivs(0, quotatype, fspath)) != NULL) {
			if (writetimes(protoprivs, tmpfd, quotatype) != 0 &&
			    editit(tmpfil) && readtimes(protoprivs, tmpfil))
				putprivs(0L, protoprivs);
			freeprivs(protoprivs);
		}
		close(tmpfd);
		unlink(tmpfil);
		exit(0);
	}
	for ( ; argc > 0; argc--, argv++) {
		if ((id = getentry(*argv, quotatype)) == -1)
			continue;
		if ((curprivs = getprivs(id, quotatype, fspath)) == NULL)
			exit(1);
		if (writeprivs(curprivs, tmpfd, *argv, quotatype) == 0)
			continue;
		if (editit(tmpfil) && readprivs(curprivs, tmpfil))
			putprivs(id, curprivs);
		freeprivs(curprivs);
	}
	close(tmpfd);
	unlink(tmpfil);
	exit(0);
}

static void
usage(void)
{
	fprintf(stderr, "%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n",
		"usage: edquota [-uh] [-f fspath] [-p username] username ...",
		"       edquota [-u] -e fspath[:bslim[:bhlim[:islim[:ihlim]]]] [-e ...]",
		"               username ...",
		"       edquota -g [-h] [-f fspath] [-p groupname] groupname ...",
		"       edquota -g -e fspath[:bslim[:bhlim[:islim[:ihlim]]]] [-e ...]",
		"               groupname ...",
		"       edquota [-u] -t [-f fspath]",
		"       edquota -g -t [-f fspath]");
	exit(1);
}

/*
 * This routine converts a name for a particular quota type to
 * an identifier. This routine must agree with the kernel routine
 * getinoquota as to the interpretation of quota types.
 */
int
getentry(const char *name, int quotatype)
{
	struct passwd *pw;
	struct group *gr;

	if (alldigits(name))
		return (atoi(name));
	switch(quotatype) {
	case USRQUOTA:
		if ((pw = getpwnam(name)))
			return (pw->pw_uid);
		warnx("%s: no such user", name);
		sleep(3);
		break;
	case GRPQUOTA:
		if ((gr = getgrnam(name)))
			return (gr->gr_gid);
		warnx("%s: no such group", name);
		sleep(3);
		break;
	default:
		warnx("%d: unknown quota type", quotatype);
		sleep(3);
		break;
	}
	sleep(1);
	return (-1);
}

/*
 * Collect the requested quota information.
 */
struct quotause *
getprivs(long id, int quotatype, char *fspath)
{
	struct quotafile *qf;
	struct fstab *fs;
	struct quotause *qup, *quptail;
	struct quotause *quphead;

	setfsent();
	quphead = quptail = NULL;
	while ((fs = getfsent())) {
		if (fspath && *fspath && strcmp(fspath, fs->fs_spec) &&
		    strcmp(fspath, fs->fs_file))
			continue;
		if (strcmp(fs->fs_vfstype, "ufs"))
			continue;
		if ((qf = quota_open(fs, quotatype, O_CREAT|O_RDWR)) == NULL) {
			if (errno != EOPNOTSUPP)
				warn("cannot open quotas on %s", fs->fs_file);
			continue;
		}
		if ((qup = (struct quotause *)calloc(1, sizeof(*qup))) == NULL)
			errx(2, "out of memory");
		qup->qf = qf;
		strlcpy(qup->fsname, fs->fs_file, sizeof(qup->fsname));
		if (quota_read(qf, &qup->dqblk, id) == -1) {
			warn("cannot read quotas on %s", fs->fs_file);
			freeprivs(qup);
			continue;
		}
		if (quphead == NULL)
			quphead = qup;
		else
			quptail->next = qup;
		quptail = qup;
		qup->next = 0;
	}
	if (quphead == NULL) {
		warnx("No quotas on %s", fspath ? fspath : "any filesystems");
	}
	endfsent();
	return (quphead);
}

/*
 * Store the requested quota information.
 */
void
putprivs(long id, struct quotause *quplist)
{
	struct quotause *qup;

	for (qup = quplist; qup; qup = qup->next)
		if (quota_write_limits(qup->qf, &qup->dqblk, id) == -1)
			warn("%s", qup->fsname);
}

/*
 * Take a list of privileges and get it edited.
 */
int
editit(char *tmpf)
{
	long omask;
	int pid, status;

	omask = sigblock(sigmask(SIGINT)|sigmask(SIGQUIT)|sigmask(SIGHUP));
 top:
	if ((pid = fork()) < 0) {

		if (errno == EPROCLIM) {
			warnx("you have too many processes");
			return(0);
		}
		if (errno == EAGAIN) {
			sleep(1);
			goto top;
		}
		warn("fork");
		return (0);
	}
	if (pid == 0) {
		const char *ed;

		sigsetmask(omask);
		if (setgid(getgid()) != 0)
			err(1, "setgid failed");
		if (setuid(getuid()) != 0)
			err(1, "setuid failed");
		if ((ed = getenv("EDITOR")) == (char *)0)
			ed = _PATH_VI;
		execlp(ed, ed, tmpf, (char *)0);
		err(1, "%s", ed);
	}
	waitpid(pid, &status, 0);
	sigsetmask(omask);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		return (0);
	return (1);
}

/*
 * Convert a quotause list to an ASCII file.
 */
int
writeprivs(struct quotause *quplist, int outfd, char *name, int quotatype)
{
	struct quotause *qup;
	FILE *fd;

	ftruncate(outfd, 0);
	lseek(outfd, 0, L_SET);
	if ((fd = fdopen(dup(outfd), "w")) == NULL)
		err(1, "%s", tmpfil);
	fprintf(fd, "Quotas for %s %s:\n", qfextension[quotatype], name);
	for (qup = quplist; qup; qup = qup->next) {
		fprintf(fd, "%s: in use: %s, ", qup->fsname,
		    fmthumanvalblks(qup->dqblk.dqb_curblocks));
		fprintf(fd, "limits (soft = %s, ",
		    fmthumanvalblks(qup->dqblk.dqb_bsoftlimit));
		fprintf(fd, "hard = %s)\n",
		    fmthumanvalblks(qup->dqblk.dqb_bhardlimit));
		fprintf(fd, "\tinodes in use: %s, ",
		    fmthumanvalinos(qup->dqblk.dqb_curinodes));
		fprintf(fd, "limits (soft = %s, ",
		    fmthumanvalinos(qup->dqblk.dqb_isoftlimit));
		fprintf(fd, "hard = %s)\n",
		    fmthumanvalinos(qup->dqblk.dqb_ihardlimit));
	}
	fclose(fd);
	return (1);
}

char *
fmthumanvalblks(int64_t blocks)
{
	static char numbuf[20];

	if (hflag) {
		humanize_number(numbuf, blocks < 0 ? 7 : 6,
		    dbtob(blocks), "", HN_AUTOSCALE, HN_NOSPACE);
		return (numbuf);
	}
	snprintf(numbuf, sizeof(numbuf), "%juk", (uintmax_t)dbtokb(blocks));
	return(numbuf);
}

char *
fmthumanvalinos(int64_t inos)
{
	static char numbuf[20];

	if (hflag) {
		humanize_number(numbuf, inos < 0 ? 7 : 6,
		    inos, "", HN_AUTOSCALE, HN_NOSPACE | HN_DIVISOR_1000);
		return (numbuf);
	}
	snprintf(numbuf, sizeof(numbuf), "%ju", (uintmax_t)inos);
	return(numbuf);
}

/*
 * Merge changes to an ASCII file into a quotause list.
 */
int
readprivs(struct quotause *quplist, char *inname)
{
	struct quotause *qup;
	FILE *fd;
	uintmax_t hardlimit, softlimit, curitems;
	char hardunits, softunits, curitemunits;
	int cnt;
	char *cp;
	struct dqblk dqblk;
	char *fsp, line1[BUFSIZ], line2[BUFSIZ];

	fd = fopen(inname, "r");
	if (fd == NULL) {
		warnx("can't re-read temp file!!");
		return (0);
	}
	/*
	 * Discard title line, then read pairs of lines to process.
	 */
	(void) fgets(line1, sizeof (line1), fd);
	while (fgets(line1, sizeof (line1), fd) != NULL &&
	       fgets(line2, sizeof (line2), fd) != NULL) {
		if ((fsp = strtok(line1, " \t:")) == NULL) {
			warnx("%s: bad format", line1);
			return (0);
		}
		if ((cp = strtok((char *)0, "\n")) == NULL) {
			warnx("%s: %s: bad format", fsp, &fsp[strlen(fsp) + 1]);
			return (0);
		}
		cnt = sscanf(cp,
		    " in use: %ju%c, limits (soft = %ju%c, hard = %ju%c)",
		    &curitems, &curitemunits, &softlimit, &softunits,
		    &hardlimit, &hardunits);
		/*
		 * The next three check for old-style input formats.
		 */
		if (cnt != 6)
			cnt = sscanf(cp,
			 " in use: %ju%c, limits (soft = %ju%c hard = %ju%c",
			    &curitems, &curitemunits, &softlimit,
			    &softunits, &hardlimit, &hardunits);
		if (cnt != 6)
			cnt = sscanf(cp,
			" in use: %ju%c, limits (soft = %ju%c hard = %ju%c)",
			    &curitems, &curitemunits, &softlimit,
			    &softunits, &hardlimit, &hardunits);
		if (cnt != 6)
			cnt = sscanf(cp,
			" in use: %ju%c, limits (soft = %ju%c, hard = %ju%c",
			    &curitems, &curitemunits, &softlimit,
			    &softunits, &hardlimit, &hardunits);
		if (cnt != 6) {
			warnx("%s:%s: bad format", fsp, cp);
			return (0);
		}
		dqblk.dqb_curblocks = cvtblkval(curitems, curitemunits,
		    "current block count");
		dqblk.dqb_bsoftlimit = cvtblkval(softlimit, softunits,
		    "block soft limit");
		dqblk.dqb_bhardlimit = cvtblkval(hardlimit, hardunits,
		    "block hard limit");
		if ((cp = strtok(line2, "\n")) == NULL) {
			warnx("%s: %s: bad format", fsp, line2);
			return (0);
		}
		cnt = sscanf(&cp[7],
		    " in use: %ju%c limits (soft = %ju%c, hard = %ju%c)",
		    &curitems, &curitemunits, &softlimit,
		    &softunits, &hardlimit, &hardunits);
		/*
		 * The next three check for old-style input formats.
		 */
		if (cnt != 6)
			cnt = sscanf(&cp[7],
			 " in use: %ju%c limits (soft = %ju%c hard = %ju%c",
			    &curitems, &curitemunits, &softlimit,
			    &softunits, &hardlimit, &hardunits);
		if (cnt != 6)
			cnt = sscanf(&cp[7],
			" in use: %ju%c limits (soft = %ju%c hard = %ju%c)",
			    &curitems, &curitemunits, &softlimit,
			    &softunits, &hardlimit, &hardunits);
		if (cnt != 6)
			cnt = sscanf(&cp[7],
			" in use: %ju%c limits (soft = %ju%c, hard = %ju%c",
			    &curitems, &curitemunits, &softlimit,
			    &softunits, &hardlimit, &hardunits);
		if (cnt != 6) {
			warnx("%s: %s: bad format cnt %d", fsp, &cp[7], cnt);
			return (0);
		}
		dqblk.dqb_curinodes = cvtinoval(curitems, curitemunits,
		    "current inode count");
		dqblk.dqb_isoftlimit = cvtinoval(softlimit, softunits,
		    "inode soft limit");
		dqblk.dqb_ihardlimit = cvtinoval(hardlimit, hardunits,
		    "inode hard limit");
		for (qup = quplist; qup; qup = qup->next) {
			if (strcmp(fsp, qup->fsname))
				continue;
			/*
			 * Cause time limit to be reset when the quota
			 * is next used if previously had no soft limit
			 * or were under it, but now have a soft limit
			 * and are over it.
			 */
			if (dqblk.dqb_bsoftlimit &&
			    qup->dqblk.dqb_curblocks >= dqblk.dqb_bsoftlimit &&
			    (qup->dqblk.dqb_bsoftlimit == 0 ||
			     qup->dqblk.dqb_curblocks <
			     qup->dqblk.dqb_bsoftlimit))
				qup->dqblk.dqb_btime = 0;
			if (dqblk.dqb_isoftlimit &&
			    qup->dqblk.dqb_curinodes >= dqblk.dqb_isoftlimit &&
			    (qup->dqblk.dqb_isoftlimit == 0 ||
			     qup->dqblk.dqb_curinodes <
			     qup->dqblk.dqb_isoftlimit))
				qup->dqblk.dqb_itime = 0;
			qup->dqblk.dqb_bsoftlimit = dqblk.dqb_bsoftlimit;
			qup->dqblk.dqb_bhardlimit = dqblk.dqb_bhardlimit;
			qup->dqblk.dqb_isoftlimit = dqblk.dqb_isoftlimit;
			qup->dqblk.dqb_ihardlimit = dqblk.dqb_ihardlimit;
			qup->flags |= FOUND;
			/* Humanized input returns only approximate counts */
			if (hflag ||
			    (dqblk.dqb_curblocks == qup->dqblk.dqb_curblocks &&
			     dqblk.dqb_curinodes == qup->dqblk.dqb_curinodes))
				break;
			warnx("%s: cannot change current allocation", fsp);
			break;
		}
	}
	fclose(fd);
	/*
	 * Disable quotas for any filesystems that have not been found.
	 */
	for (qup = quplist; qup; qup = qup->next) {
		if (qup->flags & FOUND) {
			qup->flags &= ~FOUND;
			continue;
		}
		qup->dqblk.dqb_bsoftlimit = 0;
		qup->dqblk.dqb_bhardlimit = 0;
		qup->dqblk.dqb_isoftlimit = 0;
		qup->dqblk.dqb_ihardlimit = 0;
	}
	return (1);
}

/*
 * Convert a quotause list to an ASCII file of grace times.
 */
int
writetimes(struct quotause *quplist, int outfd, int quotatype)
{
	struct quotause *qup;
	FILE *fd;

	ftruncate(outfd, 0);
	lseek(outfd, 0, L_SET);
	if ((fd = fdopen(dup(outfd), "w")) == NULL)
		err(1, "%s", tmpfil);
	fprintf(fd, "Time units may be: days, hours, minutes, or seconds\n");
	fprintf(fd, "Grace period before enforcing soft limits for %ss:\n",
	    qfextension[quotatype]);
	for (qup = quplist; qup; qup = qup->next) {
		fprintf(fd, "%s: block grace period: %s, ",
		    qup->fsname, cvtstoa(qup->dqblk.dqb_btime));
		fprintf(fd, "file grace period: %s\n",
		    cvtstoa(qup->dqblk.dqb_itime));
	}
	fclose(fd);
	return (1);
}

/*
 * Merge changes of grace times in an ASCII file into a quotause list.
 */
int
readtimes(struct quotause *quplist, char *inname)
{
	struct quotause *qup;
	FILE *fd;
	int cnt;
	char *cp;
	uintmax_t itime, btime, iseconds, bseconds;
	char *fsp, bunits[10], iunits[10], line1[BUFSIZ];

	fd = fopen(inname, "r");
	if (fd == NULL) {
		warnx("can't re-read temp file!!");
		return (0);
	}
	/*
	 * Discard two title lines, then read lines to process.
	 */
	(void) fgets(line1, sizeof (line1), fd);
	(void) fgets(line1, sizeof (line1), fd);
	while (fgets(line1, sizeof (line1), fd) != NULL) {
		if ((fsp = strtok(line1, " \t:")) == NULL) {
			warnx("%s: bad format", line1);
			return (0);
		}
		if ((cp = strtok((char *)0, "\n")) == NULL) {
			warnx("%s: %s: bad format", fsp, &fsp[strlen(fsp) + 1]);
			return (0);
		}
		cnt = sscanf(cp,
		    " block grace period: %ju %s file grace period: %ju %s",
		    &btime, bunits, &itime, iunits);
		if (cnt != 4) {
			warnx("%s:%s: bad format", fsp, cp);
			return (0);
		}
		if (cvtatos(btime, bunits, &bseconds) == 0)
			return (0);
		if (cvtatos(itime, iunits, &iseconds) == 0)
			return (0);
		for (qup = quplist; qup; qup = qup->next) {
			if (strcmp(fsp, qup->fsname))
				continue;
			qup->dqblk.dqb_btime = bseconds;
			qup->dqblk.dqb_itime = iseconds;
			qup->flags |= FOUND;
			break;
		}
	}
	fclose(fd);
	/*
	 * reset default grace periods for any filesystems
	 * that have not been found.
	 */
	for (qup = quplist; qup; qup = qup->next) {
		if (qup->flags & FOUND) {
			qup->flags &= ~FOUND;
			continue;
		}
		qup->dqblk.dqb_btime = 0;
		qup->dqblk.dqb_itime = 0;
	}
	return (1);
}

/*
 * Convert seconds to ASCII times.
 */
char *
cvtstoa(uint64_t secs)
{
	static char buf[20];

	if (secs % (24 * 60 * 60) == 0) {
		secs /= 24 * 60 * 60;
		sprintf(buf, "%ju day%s", (uintmax_t)secs,
		    secs == 1 ? "" : "s");
	} else if (secs % (60 * 60) == 0) {
		secs /= 60 * 60;
		sprintf(buf, "%ju hour%s", (uintmax_t)secs,
		    secs == 1 ? "" : "s");
	} else if (secs % 60 == 0) {
		secs /= 60;
		sprintf(buf, "%ju minute%s", (uintmax_t)secs,
		    secs == 1 ? "" : "s");
	} else
		sprintf(buf, "%ju second%s", (uintmax_t)secs,
		    secs == 1 ? "" : "s");
	return (buf);
}

/*
 * Convert ASCII input times to seconds.
 */
int
cvtatos(uint64_t period, char *units, uint64_t *seconds)
{

	if (bcmp(units, "second", 6) == 0)
		*seconds = period;
	else if (bcmp(units, "minute", 6) == 0)
		*seconds = period * 60;
	else if (bcmp(units, "hour", 4) == 0)
		*seconds = period * 60 * 60;
	else if (bcmp(units, "day", 3) == 0)
		*seconds = period * 24 * 60 * 60;
	else {
		warnx("%s: bad units, specify %s\n", units,
		    "days, hours, minutes, or seconds");
		return (0);
	}
	return (1);
}

/*
 * Convert a limit to number of disk blocks.
 */
uint64_t
cvtblkval(uint64_t limit, char units, const char *itemname)
{

	switch(units) {
	case 'B':
	case 'b':
		limit = btodb(limit);
		break;
	case '\0':	/* historic behavior */
	case ',':	/* historic behavior */
	case ')':	/* historic behavior */
	case 'K':
	case 'k':
		limit *= btodb(1024);
		break;
	case 'M':
	case 'm':
		limit *= btodb(1048576);
		break;
	case 'G':
	case 'g':
		limit *= btodb(1073741824);
		break;
	case 'T':
	case 't':
		limit *= btodb(1099511627776);
		break;
	case 'P':
	case 'p':
		limit *= btodb(1125899906842624);
		break;
	case 'E':
	case 'e':
		limit *= btodb(1152921504606846976);
		break;
	case ' ':
		errx(2, "No space permitted between value and units for %s\n",
		    itemname);
		break;
	default:
		errx(2, "%ju%c: unknown units for %s, specify "
		    "none, K, M, G, T, P, or E\n",
		    (uintmax_t)limit, units, itemname);
		break;
	}
	return (limit);
}

/*
 * Convert a limit to number of inodes.
 */
uint64_t
cvtinoval(uint64_t limit, char units, const char *itemname)
{

	switch(units) {
	case 'B':
	case 'b':
	case '\0':	/* historic behavior */
	case ',':	/* historic behavior */
	case ')':	/* historic behavior */
		break;
	case 'K':
	case 'k':
		limit *= 1000;
		break;
	case 'M':
	case 'm':
		limit *= 1000000;
		break;
	case 'G':
	case 'g':
		limit *= 1000000000;
		break;
	case 'T':
	case 't':
		limit *= 1000000000000;
		break;
	case 'P':
	case 'p':
		limit *= 1000000000000000;
		break;
	case 'E':
	case 'e':
		limit *= 1000000000000000000;
		break;
	case ' ':
		errx(2, "No space permitted between value and units for %s\n",
		    itemname);
		break;
	default:
		errx(2, "%ju%c: unknown units for %s, specify "
		    "none, K, M, G, T, P, or E\n",
		    (uintmax_t)limit, units, itemname);
		break;
	}
	return (limit);
}

/*
 * Free a list of quotause structures.
 */
void
freeprivs(struct quotause *quplist)
{
	struct quotause *qup, *nextqup;

	for (qup = quplist; qup; qup = nextqup) {
		quota_close(qup->qf);
		nextqup = qup->next;
		free(qup);
	}
}

/*
 * Check whether a string is completely composed of digits.
 */
int
alldigits(const char *s)
{
	int c;

	c = *s++;
	do {
		if (!isdigit(c))
			return (0);
	} while ((c = *s++));
	return (1);
}
