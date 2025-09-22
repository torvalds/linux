/*
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

/*
 * Disk quota editor.
 */
#include <sys/param.h>	/* btodb dbtob */
#include <sys/stat.h>
#include <sys/wait.h>
#include <ufs/ufs/quota.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <fstab.h>
#include <grp.h>
#include <paths.h>
#include <pwd.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

char *qfname = QUOTAFILENAME;
char *qfextension[] = INITQFNAMES;
char *quotagroup = QUOTAGROUP;
char tmpfil[] = "/tmp/edquota.XXXXXXXXXX";

struct quotause {
	struct	quotause *next;
	long	flags;
	struct	dqblk dqblk;
	char	fsname[PATH_MAX];
	char	qfname[1];	/* actually longer */
} *getprivs(u_int, int);
#define	FOUND	0x01

void	usage(void);
int	getentry(char *, int, u_int *);
struct quotause *
	getprivs(u_int, int);
void	putprivs(long, int, struct quotause *);
int	editit(const char *);
int	writeprivs(struct quotause *, int, char *, int);
int	readprivs(struct quotause *, int);
int	writetimes(struct quotause *, int, int);
int	readtimes(struct quotause *, int);
char *	cvtstoa(time_t);
int	cvtatos(long long, char *, time_t *);
void	freeprivs(struct quotause *);
int	alldigits(char *s);
int	hasquota(struct fstab *, int, char **);

void
usage(void)
{
	(void)fprintf(stderr, "%s%s%s%s",
	    "usage: edquota [-u] [-p proto-username] username | uid ...\n",
	    "       edquota -g [-p proto-groupname] groupname | gid ...\n",
	    "       edquota -t [-u]\n",
	    "       edquota -g -t\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	struct quotause *qup, *protoprivs, *curprivs;
	u_int id, protoid;
	int quotatype, tmpfd;
	char *protoname = NULL;
	int ch;
	int tflag = 0, pflag = 0;

	if (argc < 2)
		usage();
	if (getuid())
		errx(1, "%s", strerror(EPERM));
	quotatype = USRQUOTA;
	while ((ch = getopt(argc, argv, "ugtp:")) != -1) {
		switch(ch) {
		case 'p':
			protoname = optarg;
			pflag = 1;
			break;
		case 'g':
			quotatype = GRPQUOTA;
			break;
		case 'u':
			quotatype = USRQUOTA;
			break;
		case 't':
			tflag = 1;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (pflag) {
		if (getentry(protoname, quotatype, &protoid) == -1)
			exit(1);
		protoprivs = getprivs(protoid, quotatype);
		for (qup = protoprivs; qup; qup = qup->next) {
			qup->dqblk.dqb_btime = 0;
			qup->dqblk.dqb_itime = 0;
		}
		while (argc-- > 0) {
			if (getentry(*argv++, quotatype, &id) == -1)
				continue;
			putprivs(id, quotatype, protoprivs);
		}
		exit(0);
	}
	if ((tmpfd = mkstemp(tmpfil)) == -1)
		errx(1, "%s", tmpfil);
	if (tflag) {
		protoprivs = getprivs(0, quotatype);
		if (writetimes(protoprivs, tmpfd, quotatype) == 0) {
			unlink(tmpfil);
			exit(1);
		}
		if (editit(tmpfil) == -1) {
			int saved_errno = errno;
			unlink(tmpfil);
			errc(1, saved_errno, "error starting editor");
		}
		if (readtimes(protoprivs, tmpfd))
			putprivs(0, quotatype, protoprivs);
		freeprivs(protoprivs);
		unlink(tmpfil);
		exit(0);
	}
	for ( ; argc > 0; argc--, argv++) {
		if (getentry(*argv, quotatype, &id) == -1)
			continue;
		curprivs = getprivs(id, quotatype);
		if (writeprivs(curprivs, tmpfd, *argv, quotatype) == 0)
			continue;
		if (editit(tmpfil) == -1) {
			warn("error starting editor");
			continue;
		}
		if (readprivs(curprivs, tmpfd))
			putprivs(id, quotatype, curprivs);
		freeprivs(curprivs);
	}
	close(tmpfd);
	unlink(tmpfil);
	exit(0);
}

/*
 * This routine converts a name for a particular quota type to
 * an identifier. This routine must agree with the kernel routine
 * getinoquota as to the interpretation of quota types.
 */
int
getentry(char *name, int quotatype, u_int *idp)
{
	u_int id;

	switch(quotatype) {
	case USRQUOTA:
		if (uid_from_user(name, idp) != -1) {
			return 0;
		} else if (alldigits(name)) {
			if ((id = strtoul(name, NULL, 10)) <= UID_MAX) {
				*idp = id;
				return 0;
			}
		}
		warnx("%s: no such user", name);
		break;
	case GRPQUOTA:
		if (gid_from_group(name, idp) != -1) {
			return 0;
		} else if (alldigits(name)) {
			if ((id = strtoul(name, NULL, 10)) <= GID_MAX) {
				*idp = id;
				return (0);
			}
		}
		warnx("%s: no such group", name);
		break;
	default:
		warnx("%d: unknown quota type", quotatype);
		break;
	}
	sleep(1);
	return(-1);
}

/*
 * Collect the requested quota information.
 */
struct quotause *
getprivs(u_int id, int quotatype)
{
	struct fstab *fs;
	struct quotause *qup, *quptail;
	struct quotause *quphead;
	int qcmd, qupsize, fd;
	u_int mid;
	char *qfpathname;
	static int warned = 0;
	size_t qfpathnamelen;

	setfsent();
	quphead = NULL;
	qcmd = QCMD(Q_GETQUOTA, quotatype);
	while ((fs = getfsent())) {
		if (strcmp(fs->fs_vfstype, "ffs") &&
		    strcmp(fs->fs_vfstype, "ufs") &&
		    strcmp(fs->fs_vfstype, "mfs"))
			continue;
		if (!hasquota(fs, quotatype, &qfpathname))
			continue;
		qfpathnamelen = strlen(qfpathname);
		qupsize = sizeof(*qup) + qfpathnamelen;
		if ((qup = malloc(qupsize)) == NULL)
			errx(2, "out of memory");
		if (quotactl(fs->fs_file, qcmd, id, (char *)&qup->dqblk) != 0) {
	    		if (errno == EOPNOTSUPP && !warned) {
				warned++;
				(void)fprintf(stderr, "Warning: %s\n",
				    "Quotas are not compiled into this kernel");
				sleep(3);
			}
			if (getentry(quotagroup, GRPQUOTA, &mid) == -1) {
				warned++;
				(void)fprintf(stderr, "Warning: "
				    "group %s not known, skipping %s\n",
				    quotagroup, fs->fs_file);
			}
			if ((fd = open(qfpathname, O_RDONLY)) == -1) {
				fd = open(qfpathname, O_RDWR|O_CREAT, 0640);
				if (fd == -1 && errno != ENOENT) {
					perror(qfpathname);
					free(qup);
					continue;
				}
				(void)fprintf(stderr, "Creating quota file %s\n",
				    qfpathname);
				sleep(3);
				(void)fchown(fd, getuid(), mid);
				(void)fchmod(fd, 0640);
			}
			lseek(fd, (off_t)(id * sizeof(struct dqblk)), SEEK_SET);
			switch (read(fd, &qup->dqblk, sizeof(struct dqblk))) {
			case 0:			/* EOF */
				/*
				 * Convert implicit 0 quota (EOF)
				 * into an explicit one (zero'ed dqblk)
				 */
				bzero((caddr_t)&qup->dqblk,
				    sizeof(struct dqblk));
				break;

			case sizeof(struct dqblk):	/* OK */
				break;

			default:		/* ERROR */
				warn("read error in %s", qfpathname);
				close(fd);
				free(qup);
				continue;
			}
			close(fd);
		}
		strlcpy(qup->qfname, qfpathname, qfpathnamelen + 1);
		strlcpy(qup->fsname, fs->fs_file, sizeof qup->fsname);
		if (quphead == NULL)
			quphead = qup;
		else
			quptail->next = qup;
		quptail = qup;
		qup->next = NULL;
	}
	endfsent();
	return(quphead);
}

/*
 * Store the requested quota information.
 */
void
putprivs(long id, int quotatype, struct quotause *quplist)
{
	struct quotause *qup;
	int qcmd, fd;

	qcmd = QCMD(Q_SETQUOTA, quotatype);
	for (qup = quplist; qup; qup = qup->next) {
		if (quotactl(qup->fsname, qcmd, id, (char *)&qup->dqblk) == 0)
			continue;
		if ((fd = open(qup->qfname, O_WRONLY)) == -1) {
			perror(qup->qfname);
		} else {
			lseek(fd, (off_t)(id * sizeof (struct dqblk)), SEEK_SET);
			if (write(fd, &qup->dqblk, sizeof (struct dqblk)) !=
			    sizeof (struct dqblk))
				warn("%s", qup->qfname);
			close(fd);
		}
	}
}

/*
 * Execute an editor on the specified pathname, which is interpreted
 * from the shell.  This means flags may be included.
 *
 * Returns -1 on error, or the exit value on success.
 */
int
editit(const char *pathname)
{
	char *argp[] = {"sh", "-c", NULL, NULL}, *ed, *p;
	sig_t sighup, sigint, sigquit, sigchld;
	pid_t pid;
	int saved_errno, st, ret = -1;

	ed = getenv("VISUAL");
	if (ed == NULL || ed[0] == '\0')
		ed = getenv("EDITOR");
	if (ed == NULL || ed[0] == '\0')
		ed = _PATH_VI;
	if (asprintf(&p, "%s %s", ed, pathname) == -1)
		return (-1);
	argp[2] = p;

	sighup = signal(SIGHUP, SIG_IGN);
	sigint = signal(SIGINT, SIG_IGN);
	sigquit = signal(SIGQUIT, SIG_IGN);
	sigchld = signal(SIGCHLD, SIG_DFL);
	if ((pid = fork()) == -1)
		goto fail;
	if (pid == 0) {
		execv(_PATH_BSHELL, argp);
		_exit(127);
	}
	while (waitpid(pid, &st, 0) == -1)
		if (errno != EINTR)
			goto fail;
	if (!WIFEXITED(st))
		errno = EINTR;
	else
		ret = WEXITSTATUS(st);

 fail:
	saved_errno = errno;
	(void)signal(SIGHUP, sighup);
	(void)signal(SIGINT, sigint);
	(void)signal(SIGQUIT, sigquit);
	(void)signal(SIGCHLD, sigchld);
	free(p);
	errno = saved_errno;
	return (ret);
}

/*
 * Convert a quotause list to an ASCII file.
 */
int
writeprivs(struct quotause *quplist, int outfd, char *name, int quotatype)
{
	struct quotause *qup;
	FILE *fp;

	ftruncate(outfd, 0);
	lseek(outfd, 0, SEEK_SET);
	if ((fp = fdopen(dup(outfd), "w")) == NULL)
		err(1, "%s", tmpfil);
	(void)fprintf(fp, "Quotas for %s %s:\n", qfextension[quotatype], name);
	for (qup = quplist; qup; qup = qup->next) {
		(void)fprintf(fp, "%s: %s %d, limits (soft = %d, hard = %d)\n",
		    qup->fsname, "KBytes in use:",
		    (int)(dbtob((u_quad_t)qup->dqblk.dqb_curblocks) / 1024),
		    (int)(dbtob((u_quad_t)qup->dqblk.dqb_bsoftlimit) / 1024),
		    (int)(dbtob((u_quad_t)qup->dqblk.dqb_bhardlimit) / 1024));
		(void)fprintf(fp, "%s %d, limits (soft = %d, hard = %d)\n",
		    "\tinodes in use:", qup->dqblk.dqb_curinodes,
		    qup->dqblk.dqb_isoftlimit, qup->dqblk.dqb_ihardlimit);
	}
	fclose(fp);
	return(1);
}

/*
 * Merge changes to an ASCII file into a quotause list.
 */
int
readprivs(struct quotause *quplist, int infd)
{
	struct quotause *qup;
	FILE *fp;
	int cnt;
	char *cp;
	struct dqblk dqblk;
	char *fsp, line1[BUFSIZ], line2[BUFSIZ];

	lseek(infd, 0, SEEK_SET);
	fp = fdopen(dup(infd), "r");
	if (fp == NULL) {
		warnx("can't re-read temp file!!");
		return(0);
	}
	/*
	 * Discard title line, then read pairs of lines to process.
	 */
	(void)fgets(line1, sizeof (line1), fp);
	while (fgets(line1, sizeof (line1), fp) != NULL &&
	       fgets(line2, sizeof (line2), fp) != NULL) {
		if ((fsp = strtok(line1, " \t:")) == NULL) {
			warnx("%s: bad format", line1);
			return(0);
		}
		if ((cp = strtok(NULL, "\n")) == NULL) {
			warnx("%s: %s: bad format", fsp, &fsp[strlen(fsp) + 1]);
			return(0);
		}
		cnt = sscanf(cp,
		    " KBytes in use: %d, limits (soft = %d, hard = %d)",
		    &dqblk.dqb_curblocks, &dqblk.dqb_bsoftlimit,
		    &dqblk.dqb_bhardlimit);
		if (cnt != 3) {
			warnx("%s:%s: bad format", fsp, cp);
			return(0);
		}
		dqblk.dqb_curblocks = btodb((u_quad_t)
		    dqblk.dqb_curblocks * 1024);
		dqblk.dqb_bsoftlimit = btodb((u_quad_t)
		    dqblk.dqb_bsoftlimit * 1024);
		dqblk.dqb_bhardlimit = btodb((u_quad_t)
		    dqblk.dqb_bhardlimit * 1024);
		if ((cp = strtok(line2, "\n")) == NULL) {
			warnx("%s: %s: bad format", fsp, line2);
			return(0);
		}
		cnt = sscanf(cp,
		    "\tinodes in use: %d, limits (soft = %d, hard = %d)",
		    &dqblk.dqb_curinodes, &dqblk.dqb_isoftlimit,
		    &dqblk.dqb_ihardlimit);
		if (cnt != 3) {
			warnx("%s: %s: bad format", fsp, line2);
			return(0);
		}
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
			if (dqblk.dqb_curblocks == qup->dqblk.dqb_curblocks &&
			    dqblk.dqb_curinodes == qup->dqblk.dqb_curinodes)
				break;
			warnx("%s: cannot change current allocation", fsp);
			break;
		}
	}
	fclose(fp);
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
	return(1);
}

/*
 * Convert a quotause list to an ASCII file of grace times.
 */
int
writetimes(struct quotause *quplist, int outfd, int quotatype)
{
	struct quotause *qup;
	FILE *fp;

	ftruncate(outfd, 0);
	lseek(outfd, 0, SEEK_SET);
	if ((fp = fdopen(dup(outfd), "w")) == NULL)
		err(1, "%s", tmpfil);
	(void)fprintf(fp,
	    "Time units may be: days, hours, minutes, or seconds\n");
	(void)fprintf(fp,
	    "Grace period before enforcing soft limits for %ss:\n",
	    qfextension[quotatype]);
	for (qup = quplist; qup; qup = qup->next) {
		(void)fprintf(fp, "%s: block grace period: %s, ",
		    qup->fsname, cvtstoa(qup->dqblk.dqb_btime));
		(void)fprintf(fp, "file grace period: %s\n",
		    cvtstoa(qup->dqblk.dqb_itime));
	}
	fclose(fp);
	return(1);
}

/*
 * Merge changes of grace times in an ASCII file into a quotause list.
 */
int
readtimes(struct quotause *quplist, int infd)
{
	struct quotause *qup;
	FILE *fp;
	int cnt;
	char *cp;
	long long itime, btime;
	time_t iseconds, bseconds;
	char *fsp, bunits[10], iunits[10], line1[BUFSIZ];

	lseek(infd, 0, SEEK_SET);
	fp = fdopen(dup(infd), "r");
	if (fp == NULL) {
		warnx("can't re-read temp file!!");
		return(0);
	}
	/*
	 * Discard two title lines, then read lines to process.
	 */
	(void)fgets(line1, sizeof (line1), fp);
	(void)fgets(line1, sizeof (line1), fp);
	while (fgets(line1, sizeof (line1), fp) != NULL) {
		if ((fsp = strtok(line1, " \t:")) == NULL) {
			warnx("%s: bad format", line1);
			return(0);
		}
		if ((cp = strtok(NULL, "\n")) == NULL) {
			warnx("%s: %s: bad format", fsp,
			    &fsp[strlen(fsp) + 1]);
			return(0);
		}
		cnt = sscanf(cp,
		    " block grace period: %lld %9s file grace period: %lld %9s",
		    &btime, bunits, &itime, iunits);
		if (cnt != 4) {
			warnx("%s:%s: bad format", fsp, cp);
			return(0);
		}
		if (cvtatos(btime, bunits, &bseconds) == 0)
			return(0);
		if (cvtatos(itime, iunits, &iseconds) == 0)
			return(0);
		for (qup = quplist; qup; qup = qup->next) {
			if (strcmp(fsp, qup->fsname))
				continue;
			qup->dqblk.dqb_btime = bseconds;
			qup->dqblk.dqb_itime = iseconds;
			qup->flags |= FOUND;
			break;
		}
	}
	fclose(fp);
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
	return(1);
}

/*
 * Convert seconds to ASCII times.
 */
char *
cvtstoa(time_t time)
{
	static char buf[20];

	if (time % (24 * 60 * 60) == 0) {
		time /= 24 * 60 * 60;
		(void)snprintf(buf, sizeof buf, "%lld day%s", (long long)time,
		    time == 1 ? "" : "s");
	} else if (time % (60 * 60) == 0) {
		time /= 60 * 60;
		(void)snprintf(buf, sizeof buf, "%lld hour%s", (long long)time,
		    time == 1 ? "" : "s");
	} else if (time % 60 == 0) {
		time /= 60;
		(void)snprintf(buf, sizeof buf, "%lld minute%s",
		    (long long)time, time == 1 ? "" : "s");
	} else
		(void)snprintf(buf, sizeof buf, "%lld second%s",
		    (long long)time, time == 1 ? "" : "s");
	return(buf);
}

/*
 * Convert ASCII input times to seconds.
 */
int
cvtatos(long long time, char *units, time_t *seconds)
{

	if (bcmp(units, "second", 6) == 0)
		*seconds = time;
	else if (bcmp(units, "minute", 6) == 0)
		*seconds = time * 60;
	else if (bcmp(units, "hour", 4) == 0)
		*seconds = time * 60 * 60;
	else if (bcmp(units, "day", 3) == 0)
		*seconds = time * 24 * 60 * 60;
	else {
		(void)printf("%s: bad units, specify %s\n", units,
		    "days, hours, minutes, or seconds");
		return(0);
	}
	return(1);
}

/*
 * Free a list of quotause structures.
 */
void
freeprivs(struct quotause *quplist)
{
	struct quotause *qup, *nextqup;

	for (qup = quplist; qup; qup = nextqup) {
		nextqup = qup->next;
		free(qup);
	}
}

/*
 * Check whether a string is completely composed of digits.
 */
int
alldigits(char *s)
{
	int c;

	c = (unsigned char)*s++;
	do {
		if (!isdigit(c))
			return(0);
	} while ((c = (unsigned char)*s++));
	return(1);
}

/*
 * Check to see if a particular quota is to be enabled.
 */
int
hasquota(struct fstab *fs, int type, char **qfnamep)
{
	char *opt;
	char *cp;
	static char initname, usrname[100], grpname[100];
	static char buf[BUFSIZ];

	if (!initname) {
		(void)snprintf(usrname, sizeof usrname, "%s%s",
		    qfextension[USRQUOTA], qfname);
		(void)snprintf(grpname, sizeof grpname, "%s%s",
		    qfextension[GRPQUOTA], qfname);
		initname = 1;
	}
	strlcpy(buf, fs->fs_mntops, sizeof buf);
	for (opt = strtok(buf, ","); opt; opt = strtok(NULL, ",")) {
		if ((cp = strchr(opt, '=')))
			*cp++ = '\0';
		if (type == USRQUOTA && strcmp(opt, usrname) == 0)
			break;
		if (type == GRPQUOTA && strcmp(opt, grpname) == 0)
			break;
	}
	if (!opt)
		return(0);
	if (cp) {
		*qfnamep = cp;
		return(1);
	}
	(void)snprintf(buf, sizeof buf, "%s/%s.%s",
	    fs->fs_file, qfname, qfextension[type]);
	*qfnamep = buf;
	return(1);
}
