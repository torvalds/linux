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
 * Quota report
 */
#include <sys/param.h>	/* dbtob */
#include <sys/stat.h>
#include <ufs/ufs/quota.h>
#include <fstab.h>
#include <pwd.h>
#include <grp.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

char *qfname = QUOTAFILENAME;
char *qfextension[] = INITQFNAMES;

struct fileusage {
	struct	fileusage *fu_next;
	struct	dqblk fu_dqblk;
	uid_t	fu_id;
	char	fu_name[1];
	/* actually bigger */
};
#define FUHASH 1024	/* must be power of two */
struct fileusage *fuhead[MAXQUOTAS][FUHASH];
struct fileusage *lookup(uid_t, int);
struct fileusage *addid(uid_t id, int type, char *name);
uid_t highid[MAXQUOTAS];	/* highest addid()'ed identifier per type */

int	vflag;			/* verbose */
int	aflag;			/* all file systems */

void	usage(void);
int	repquota(struct fstab *, int, char *);
int	hasquota(struct fstab *, int, char **);
int	oneof(char *, char *[], int);
char	*timeprt(time_t);
int
main(int argc, char *argv[])
{
	struct fstab *fs;
	struct passwd *pw;
	struct group *gr;
	int gflag = 0, uflag = 0, errs = 0;
	long i, argnum, done = 0;
	char *qfnp;
	int ch;

	while ((ch = getopt(argc, argv, "aguv")) != -1) {
		switch(ch) {
		case 'a':
			aflag++;
			break;
		case 'g':
			gflag++;
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
	if ((argc == 0) == (aflag == 0))
		usage();
	if (!gflag && !uflag) {
		if (aflag)
			gflag++;
		uflag++;
	}
	if (gflag) {
		setgrent();
		while ((gr = getgrent()) != 0)
			(void) addid((uid_t)gr->gr_gid, GRPQUOTA, gr->gr_name);
		endgrent();
	}
	if (uflag) {
		setpwent();
		while ((pw = getpwent()) != 0)
			(void) addid(pw->pw_uid, USRQUOTA, pw->pw_name);
		endpwent();
	}
	setfsent();
	while ((fs = getfsent()) != NULL) {
		if (strcmp(fs->fs_vfstype, "ffs") &&
		    strcmp(fs->fs_vfstype, "ufs") &&
		    strcmp(fs->fs_vfstype, "mfs"))
			continue;
		if (aflag) {
			if (gflag && hasquota(fs, GRPQUOTA, &qfnp))
				errs += repquota(fs, GRPQUOTA, qfnp);
			if (uflag && hasquota(fs, USRQUOTA, &qfnp))
				errs += repquota(fs, USRQUOTA, qfnp);
			continue;
		}
		if ((argnum = oneof(fs->fs_file, argv, argc)) >= 0 ||
		    (argnum = oneof(fs->fs_spec, argv, argc)) >= 0) {
			done |= 1 << argnum;
			if (gflag && hasquota(fs, GRPQUOTA, &qfnp))
				errs += repquota(fs, GRPQUOTA, qfnp);
			if (uflag && hasquota(fs, USRQUOTA, &qfnp))
				errs += repquota(fs, USRQUOTA, qfnp);
		}
	}
	endfsent();
	for (i = 0; i < argc; i++)
		if ((done & (1 << i)) == 0)
			fprintf(stderr, "%s not found in fstab\n", argv[i]);
	exit(errs);
}

void
usage(void)
{
	extern char *__progname;
	fprintf(stderr, "usage: %s [-aguv] filesystem ...\n", __progname);
	exit(1);
}

int
repquota(struct fstab *fs, int type, char *qfpathname)
{
	struct fileusage *fup;
	FILE *qf;
	uid_t id;
	struct dqblk dqbuf;
	char *timeprt(time_t);
	static struct dqblk zerodqblk;
	static int warned = 0;
	static int multiple = 0;

	if (quotactl(fs->fs_file, QCMD(Q_SYNC, type), 0, 0) < 0 &&
	    errno == EOPNOTSUPP && !warned && vflag) {
		warned++;
		fprintf(stdout,
		    "*** Warning: Quotas are not compiled into this kernel\n");
	}
	if (multiple++)
		printf("\n");
	if (vflag)
		fprintf(stdout, "*** Report for %s quotas on %s (%s)\n",
		    qfextension[type], fs->fs_file, fs->fs_spec);
	if ((qf = fopen(qfpathname, "r")) == NULL) {
		perror(qfpathname);
		return (1);
	}
	for (id = 0; ; id++) {
		fread(&dqbuf, sizeof(struct dqblk), 1, qf);
		if (feof(qf))
			break;
		if (dqbuf.dqb_curinodes == 0 && dqbuf.dqb_curblocks == 0)
			continue;
		if ((fup = lookup(id, type)) == 0)
			fup = addid(id, type, NULL);
		fup->fu_dqblk = dqbuf;
	}
	fclose(qf);
	printf("                        KByte limits               File limits\n");
	printf("User            used    soft    hard  grace    used  soft  hard  grace\n");
	for (id = 0; id <= highid[type]; id++) {
		fup = lookup(id, type);
		if (fup == 0)
			continue;
		if (fup->fu_dqblk.dqb_curinodes == 0 &&
		    fup->fu_dqblk.dqb_curblocks == 0)
			continue;
		printf("%-10s", fup->fu_name);
		printf("%c%c %7d %7d %7d %6s",
			fup->fu_dqblk.dqb_bsoftlimit && 
			    fup->fu_dqblk.dqb_curblocks >= 
			    fup->fu_dqblk.dqb_bsoftlimit ? '+' : '-',
			fup->fu_dqblk.dqb_isoftlimit &&
			    fup->fu_dqblk.dqb_curinodes >=
			    fup->fu_dqblk.dqb_isoftlimit ? '+' : '-',
			(int)(dbtob((u_quad_t)fup->fu_dqblk.dqb_curblocks)
			    / 1024),
			(int)(dbtob((u_quad_t)fup->fu_dqblk.dqb_bsoftlimit)
			    / 1024),
			(int)(dbtob((u_quad_t)fup->fu_dqblk.dqb_bhardlimit)
			    / 1024),
			fup->fu_dqblk.dqb_bsoftlimit && 
			    fup->fu_dqblk.dqb_curblocks >= 
			    fup->fu_dqblk.dqb_bsoftlimit ?
			    timeprt(fup->fu_dqblk.dqb_btime) : "");
		printf("  %6d %5d %5d %6s\n",
			fup->fu_dqblk.dqb_curinodes,
			fup->fu_dqblk.dqb_isoftlimit,
			fup->fu_dqblk.dqb_ihardlimit,
			fup->fu_dqblk.dqb_isoftlimit &&
			    fup->fu_dqblk.dqb_curinodes >=
			    fup->fu_dqblk.dqb_isoftlimit ?
			    timeprt(fup->fu_dqblk.dqb_itime) : "");
		fup->fu_dqblk = zerodqblk;
	}
	return (0);
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
		return (0);
	if (cp) {
		*qfnamep = cp;
		return (1);
	}
	(void)snprintf(buf, sizeof buf, "%s/%s.%s",
	    fs->fs_file, qfname, qfextension[type]);
	*qfnamep = buf;
	return (1);
}

/*
 * Routines to manage the file usage table.
 *
 * Lookup an id of a specific type.
 */
struct fileusage *
lookup(uid_t id, int type)
{
	struct fileusage *fup;

	for (fup = fuhead[type][id & (FUHASH-1)]; fup != 0; fup = fup->fu_next)
		if (fup->fu_id == id)
			return (fup);
	return (NULL);
}

/*
 * Add a new file usage id if it does not already exist.
 */
struct fileusage *
addid(uid_t id, int type, char *name)
{
	struct fileusage *fup, **fhp;
	size_t len;

	if ((fup = lookup(id, type)))
		return (fup);
	if (name)
		len = strlen(name);
	else
		len = 10;
	if ((fup = calloc(1, sizeof(*fup) + len)) == NULL) {
		fprintf(stderr, "out of memory for fileusage structures\n");
		exit(1);
	}
	fhp = &fuhead[type][id & (FUHASH - 1)];
	fup->fu_next = *fhp;
	*fhp = fup;
	fup->fu_id = id;
	if (id > highid[type])
		highid[type] = id;
	if (name) {
		bcopy(name, fup->fu_name, len + 1);
	} else {
		snprintf(fup->fu_name, len, "%u", id);
	}
	return (fup);
}

/*
 * Calculate the grace period and return a printable string for it.
 */
char *
timeprt(time_t seconds)
{
	int hours, minutes;
	static char buf[20];
	static time_t now;

	if (now == 0)
		time(&now);
	if (now > seconds)
		return ("none");
	seconds -= now;
	minutes = (seconds + 30) / 60;
	hours = (minutes + 30) / 60;
	if (hours >= 36) {
		snprintf(buf, sizeof buf, "%ddays", (hours + 12) / 24);
		return (buf);
	}
	if (minutes >= 60) {
		snprintf(buf, sizeof buf, "%2d:%d", minutes / 60,
		    minutes % 60);
		return (buf);
	}
	snprintf(buf, sizeof buf, "%2d", minutes);
	return (buf);
}
