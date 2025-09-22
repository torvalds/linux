/*	$OpenBSD: quotacheck.c,v 1.44 2025/04/20 17:15:57 kn Exp $	*/
/*	$NetBSD: quotacheck.c,v 1.12 1996/03/30 22:34:25 mark Exp $	*/

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
 * Fix up / report on disk quotas & usage
 */
#include <sys/param.h>	/* DEV_BSIZE MAXBSIZE */
#include <sys/stat.h>
#include <sys/wait.h>

#include <ufs/ufs/dinode.h>
#include <ufs/ufs/quota.h>
#include <ufs/ffs/fs.h>

#include <fcntl.h>
#include <fstab.h>
#include <pwd.h>
#include <grp.h>
#include <errno.h>
#include <unistd.h>
#include <limits.h>
#include <util.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>
#include "fsutil.h"

char *qfname = QUOTAFILENAME;
char *qfextension[] = INITQFNAMES;
char *quotagroup = QUOTAGROUP;

union {
	struct	fs	sblk;
	char	dummy[MAXBSIZE];
} sb_un;
#define	sblock	sb_un.sblk
union {
	struct	cg	cgblk;
	char	dummy[MAXBSIZE];
} cg_un;
#define	cgblk	cg_un.cgblk

long maxino;

union dinode {
	struct ufs1_dinode dp1;
	struct ufs2_dinode dp2;
};
#define	DIP(dp, field) \
	((sblock.fs_magic == FS_UFS1_MAGIC) ? \
	(dp)->dp1.field : (dp)->dp2.field)

struct quotaname {
	long	flags;
	char	grpqfname[PATH_MAX + 1];
	char	usrqfname[PATH_MAX + 1];
};
#define	HASUSR	1
#define	HASGRP	2

struct fileusage {
	struct fileusage *fu_next;
	u_int32_t	fu_curinodes;
	u_int32_t	fu_curblocks;
	u_int32_t	fu_id;	/* uid_t or gid_t */
	char		fu_name[1];
	/* actually bigger */
};
#define FUHASH 1024	/* must be power of two */
struct fileusage *fuhead[MAXQUOTAS][FUHASH];

int	gflag;			/* check group quotas */
int	uflag;			/* check user quotas */
int	flags;			/* check flags (avd) */
int	fi;			/* open disk file descriptor */
u_int32_t highid[MAXQUOTAS];	/* highest addid()'ed identifier per type */

struct fileusage *
	 addid(u_int32_t, int, char *);
char	*blockcheck(char *);
void	 bread(daddr_t, char *, long);
int	 chkquota(const char *, const char *, const char *, void *, pid_t *);
void	 freeinodebuf(void);
union dinode *
	 getnextinode(ino_t);
int	 getquotagid(void);
int	 hasquota(struct fstab *, int, char **);
struct fileusage *
	 lookup(u_int32_t, int);
void	*needchk(struct fstab *);
int	 oneof_realpath(char *, char*[], int);
int	 oneof_specname(char *, char*[], int);
void	 setinodebuf(ino_t);
int	 update(const char *, const char *, int);
void	 usage(void);

int
main(int argc, char *argv[])
{
	struct fstab *fs;
	struct passwd *pw;
	struct group *gr;
	struct quotaname *auxdata;
	int i, argnum, maxrun, errs, ch;
	u_int64_t done = 0;	/* XXX supports maximum 64 filesystems */
	const char *errstr;
	char *name;

	checkroot();

	errs = maxrun = 0;
	while ((ch = getopt(argc, argv, "adguvl:")) != -1) {
		switch(ch) {
		case 'a':
			flags |= CHECK_PREEN;
			break;
		case 'd':
			flags |= CHECK_DEBUG;
			break;
		case 'g':
			gflag = 1;
			break;
		case 'l':
			maxrun = strtonum(optarg, 0, INT_MAX, &errstr);
			if (errstr)
				errx(1, "-l %s: %s", optarg, errstr);
			break;
		case 'u':
			uflag = 1;
			break;
		case 'v':
			flags |= CHECK_VERBOSE;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if ((argc == 0 && !(flags&CHECK_PREEN)) ||
	    (argc > 0 && (flags&CHECK_PREEN)))
		usage();
	if (!gflag && !uflag) {
		gflag++;
		uflag++;
	}
	if (gflag) {
		setgrent();
		while ((gr = getgrent()) != 0)
			(void) addid(gr->gr_gid, GRPQUOTA, gr->gr_name);
		endgrent();
	}
	if (uflag) {
		setpwent();
		while ((pw = getpwent()) != 0)
			(void) addid(pw->pw_uid, USRQUOTA, pw->pw_name);
		endpwent();
	}
	if (flags&CHECK_PREEN)
		exit(checkfstab(flags, maxrun, needchk, chkquota));
	if (setfsent() == 0)
		err(1, "%s: can't open", _PATH_FSTAB);
	while ((fs = getfsent()) != NULL) {
		if (((argnum = oneof_realpath(fs->fs_file, argv, argc)) >= 0 ||
		    (argnum = oneof_specname(fs->fs_spec, argv, argc)) >= 0) &&
		    (auxdata = needchk(fs)) &&
		    (name = blockcheck(fs->fs_spec))) {
			done |= 1 << argnum;
			errs += chkquota(fs->fs_vfstype, name,
			    fs->fs_file, auxdata, NULL);
		}
	}
	endfsent();
	for (i = 0; i < argc; i++)
		if ((done & (1 << i)) == 0)
			fprintf(stderr, "%s not found in %s\n",
			    argv[i], _PATH_FSTAB);
	exit(errs);
}

void
usage(void)
{
	extern char *__progname;
	(void)fprintf(stderr, "usage: %s [-adguv] [-l maxparallel] "
	    "filesystem ...\n", __progname);
	exit(1);
}

void *
needchk(struct fstab *fs)
{
	struct quotaname *qnp;
	char *qfnp;

	if (fs->fs_passno == 0)
		return NULL;
	if (strcmp(fs->fs_type, FSTAB_RW))
		return (NULL);
	if (strcmp(fs->fs_vfstype, "ffs") &&
	    strcmp(fs->fs_vfstype, "ufs") &&
	    strcmp(fs->fs_vfstype, "mfs"))
		return (NULL);
	if ((qnp = malloc(sizeof(*qnp))) == NULL)
		err(1, NULL);
	qnp->flags = 0;
	if (gflag && hasquota(fs, GRPQUOTA, &qfnp)) {
		strlcpy(qnp->grpqfname, qfnp, sizeof qnp->grpqfname);
		qnp->flags |= HASGRP;
	}
	if (uflag && hasquota(fs, USRQUOTA, &qfnp)) {
		strlcpy(qnp->usrqfname, qfnp, sizeof qnp->usrqfname);
		qnp->flags |= HASUSR;
	}
	if (qnp->flags)
		return (qnp);
	free(qnp);
	return (NULL);
}

/*
 * Possible superblock locations ordered from most to least likely.
 */
static int sblock_try[] = SBLOCKSEARCH;

/*
 * Scan the specified file system to check quota(s) present on it.
 */
int
chkquota(const char *vfstype, const char *fsname, const char *mntpt,
    void *auxarg, pid_t *pidp)
{
	struct quotaname *qnp = auxarg;
	struct fileusage *fup;
	union dinode *dp;
	int cg, i, mode, errs = 0, status;
	ino_t ino, inosused;
	pid_t pid;

	switch (pid = fork()) {
	case -1:	/* error */
		warn("fork");
		return 1;
	case 0:		/* child */
		if ((fi = opendev(fsname, O_RDONLY, 0, NULL)) == -1)
			err(1, "%s", fsname);
		sync();
		for (i = 0; sblock_try[i] != -1; i++) {
			bread(sblock_try[i] / DEV_BSIZE, (char *)&sblock,
			    (long)SBLOCKSIZE);
			if ((sblock.fs_magic == FS_UFS1_MAGIC ||
			     (sblock.fs_magic == FS_UFS2_MAGIC &&
			      sblock.fs_sblockloc == sblock_try[i])) &&
			    sblock.fs_bsize <= MAXBSIZE &&
			    sblock.fs_bsize >= sizeof(struct fs))
				break;
		}
		if (sblock_try[i] == -1) {
			warn("Cannot find file system superblock");
			return (1);
		}
		maxino = sblock.fs_ncg * sblock.fs_ipg;
		for (cg = 0; cg < sblock.fs_ncg; cg++) {
			ino = cg * sblock.fs_ipg;
			setinodebuf(ino);
			bread(fsbtodb(&sblock, cgtod(&sblock, cg)),
			    (char *)(&cgblk), sblock.fs_cgsize);
			if (sblock.fs_magic == FS_UFS2_MAGIC)
				inosused = cgblk.cg_initediblk;
			else
				inosused = sblock.fs_ipg;
			for (i = 0; i < inosused; i++, ino++) {
				if ((dp = getnextinode(ino)) == NULL ||
				    ino < ROOTINO ||
				    (mode = DIP(dp, di_mode) & IFMT) == 0)
					continue;
				if (qnp->flags & HASGRP) {
					fup = addid(DIP(dp, di_gid),
					    GRPQUOTA, NULL);
					fup->fu_curinodes++;
					if (mode == IFREG || mode == IFDIR ||
					    mode == IFLNK)
						fup->fu_curblocks +=
						    DIP(dp, di_blocks);
				}
				if (qnp->flags & HASUSR) {
					fup = addid(DIP(dp, di_uid),
					    USRQUOTA, NULL);
					fup->fu_curinodes++;
					if (mode == IFREG || mode == IFDIR ||
					    mode == IFLNK)
						fup->fu_curblocks +=
						    DIP(dp, di_blocks);
				}
			}
		}
		freeinodebuf();
		if (flags&(CHECK_DEBUG|CHECK_VERBOSE)) {
			(void)printf("*** Checking ");
			if (qnp->flags & HASUSR) {
				(void)printf("%s", qfextension[USRQUOTA]);
				if (qnp->flags & HASGRP)
					(void)printf(" and ");
			}
			if (qnp->flags & HASGRP)
				(void)printf("%s", qfextension[GRPQUOTA]);
			(void)printf(" quotas for %s (%s), %swait\n",
			    fsname, mntpt, pidp? "no" : "");
		}
		if (qnp->flags & HASUSR)
			errs += update(mntpt, qnp->usrqfname, USRQUOTA);
		if (qnp->flags & HASGRP)
			errs += update(mntpt, qnp->grpqfname, GRPQUOTA);
		close(fi);
		exit (errs);
		break;
	default:	/* parent */
		if (pidp != NULL) {
			*pidp = pid;
			return 0;
		}
		if (waitpid(pid, &status, 0) == -1) {
			warn("waitpid");
			return 1;
		}
		if (WIFEXITED(status)) {
			if (WEXITSTATUS(status) != 0)
				return WEXITSTATUS(status);
		} else if (WIFSIGNALED(status)) {
			warnx("%s: %s", fsname, strsignal(WTERMSIG(status)));
			return 1;
		}
		break;
	}
	return (0);
}

/*
 * Update a specified quota file.
 */
int
update(const char *fsname, const char *quotafile, int type)
{
	struct fileusage *fup;
	FILE *qfi, *qfo;
	u_int32_t id, lastid;
	struct dqblk dqbuf;
	static int warned = 0;
	static struct dqblk zerodqbuf;
	static struct fileusage zerofileusage;

	if (flags&CHECK_DEBUG)
		printf("updating: %s\n", quotafile);

	if ((qfo = fopen(quotafile, (flags&CHECK_DEBUG)? "r" : "r+")) == NULL) {
		if (errno == ENOENT)
			qfo = fopen(quotafile, "w+");
		if (qfo) {
			warnx("creating quota file: %s", quotafile);
#define	MODE	(S_IRUSR|S_IWUSR|S_IRGRP)
			(void) fchown(fileno(qfo), getuid(), getquotagid());
			(void) fchmod(fileno(qfo), MODE);
		} else {
			warn("%s", quotafile);
			return (1);
		}
	}
	if ((qfi = fopen(quotafile, "r")) == NULL) {
		warn("%s", quotafile);
		(void) fclose(qfo);
		return (1);
	}
	if (quotactl(fsname, QCMD(Q_SYNC, type), 0, (caddr_t)0) == -1 &&
	    errno == EOPNOTSUPP && !warned &&
	    (flags&(CHECK_DEBUG|CHECK_VERBOSE))) {
		warned++;
		(void)printf("*** Warning: %s\n",
		    "Quotas are not compiled into this kernel");
	}
	for (lastid = highid[type], id = 0; id <= lastid; id++) {
		if (fread((char *)&dqbuf, sizeof(struct dqblk), 1, qfi) == 0)
			dqbuf = zerodqbuf;
		if ((fup = lookup(id, type)) == 0)
			fup = &zerofileusage;
		if (dqbuf.dqb_curinodes == fup->fu_curinodes &&
		    dqbuf.dqb_curblocks == fup->fu_curblocks) {
			fup->fu_curinodes = 0;
			fup->fu_curblocks = 0;
			fseek(qfo, (long)sizeof(struct dqblk), SEEK_CUR);
			continue;
		}
		if (flags&(CHECK_DEBUG|CHECK_VERBOSE)) {
			if (flags&CHECK_PREEN)
				printf("%s: ", fsname);
			printf("%-8s fixed:", fup->fu_name);
			if (dqbuf.dqb_curinodes != fup->fu_curinodes)
				(void)printf("\tinodes %d -> %u",
				    dqbuf.dqb_curinodes, fup->fu_curinodes);
			if (dqbuf.dqb_curblocks != fup->fu_curblocks)
				(void)printf("\tblocks %u -> %u",
				    dqbuf.dqb_curblocks, fup->fu_curblocks);
			(void)printf("\n");
		}
		/*
		 * Reset time limit if have a soft limit and were
		 * previously under it, but are now over it.
		 */
		if (dqbuf.dqb_bsoftlimit &&
		    dqbuf.dqb_curblocks < dqbuf.dqb_bsoftlimit &&
		    fup->fu_curblocks >= dqbuf.dqb_bsoftlimit)
			dqbuf.dqb_btime = 0;
		if (dqbuf.dqb_isoftlimit &&
		    dqbuf.dqb_curblocks < dqbuf.dqb_isoftlimit &&
		    fup->fu_curblocks >= dqbuf.dqb_isoftlimit)
			dqbuf.dqb_itime = 0;
		dqbuf.dqb_curinodes = fup->fu_curinodes;
		dqbuf.dqb_curblocks = fup->fu_curblocks;
		if (!(flags & CHECK_DEBUG)) {
			fwrite((char *)&dqbuf, sizeof(struct dqblk), 1, qfo);
			(void) quotactl(fsname, QCMD(Q_SETUSE, type), id,
			    (caddr_t)&dqbuf);
		}
		fup->fu_curinodes = 0;
		fup->fu_curblocks = 0;
	}
	fclose(qfi);
	fflush(qfo);
	if (!(flags & CHECK_DEBUG))
		ftruncate(fileno(qfo),
		    (off_t)((highid[type] + 1) * sizeof(struct dqblk)));
	fclose(qfo);
	return (0);
}

/*
 * Check to see if realpath(target) matches a realpath() in list of size cnt.
 */
int
oneof_realpath(char *target, char *list[], int cnt)
{
	int i;
	char realtarget[PATH_MAX], realargv[PATH_MAX];
	char *rv;

	rv = realpath(target, realtarget);
	if (rv == NULL)
		return (-1);

	for (i = 0; i < cnt; i++) {
		rv = realpath(list[i], realargv);
		if (rv && strcmp(realtarget, realargv) == 0)
			break;
	}

	if (i < cnt)
		return (i);
	else
		return (-1);
}

/*
 * Check to see if opendev(target) matches a opendev() in list of size cnt.
 */
int
oneof_specname(char *target, char *list[], int cnt)
{
	int i, fd;
	char *tmp, *targetdev, *argvdev;

	fd = opendev(target, O_RDONLY, 0, &tmp);
	if (fd == -1)
		return (-1);
	close(fd);
	targetdev = strdup(tmp);

	for (i = 0; i < cnt; i++) {
		fd = opendev(list[i], O_RDONLY, 0, &argvdev);
		if (fd == -1)
			continue;
		close(fd);
		if (strcmp(targetdev, argvdev) == 0)
			break;
	}

	free(targetdev);

	if (i < cnt)
		return (i);
	else
		return (-1);
}

/*
 * Determine the group identifier for quota files.
 */
int
getquotagid(void)
{
	struct group *gr;

	if ((gr = getgrnam(quotagroup)) != NULL)
		return (gr->gr_gid);
	return (-1);
}

/*
 * Check to see if a particular quota is to be enabled.
 */
int
hasquota(struct fstab *fs, int type, char **qfnamep)
{
	char *opt, *cp;
	static char initname, usrname[100], grpname[100];
	static char buf[BUFSIZ];

	if (!initname) {
		(void)snprintf(usrname, sizeof(usrname),
		    "%s%s", qfextension[USRQUOTA], qfname);
		(void)snprintf(grpname, sizeof(grpname),
		    "%s%s", qfextension[GRPQUOTA], qfname);
		initname = 1;
	}
	(void)strlcpy(buf, fs->fs_mntops, sizeof(buf));
	for (opt = strtok(buf, ","); opt; opt = strtok(NULL, ",")) {
		if ((cp = strchr(opt, '=')) != NULL)
			*cp++ = '\0';
		if (type == USRQUOTA && strcmp(opt, usrname) == 0)
			break;
		if (type == GRPQUOTA && strcmp(opt, grpname) == 0)
			break;
	}
	if (!opt)
		return (0);
	if (cp)
		*qfnamep = cp;
	else {
		(void)snprintf(buf, sizeof(buf),
		    "%s/%s.%s", fs->fs_file, qfname, qfextension[type]);
		*qfnamep = buf;
	}
	return (1);
}

/*
 * Routines to manage the file usage table.
 *
 * Lookup an id of a specific type.
 */
struct fileusage *
lookup(u_int32_t id, int type)
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
addid(u_int32_t id, int type, char *name)
{
	struct fileusage *fup, **fhp;
	int len;

	if ((fup = lookup(id, type)) != NULL)
		return (fup);
	if (name)
		len = strlen(name);
	else
		len = 10;
	if ((fup = calloc(1, sizeof(*fup) + len)) == NULL)
		err(1, NULL);
	fhp = &fuhead[type][id & (FUHASH - 1)];
	fup->fu_next = *fhp;
	*fhp = fup;
	fup->fu_id = id;
	if (id > highid[type])
		highid[type] = id;
	if (name)
		memcpy(fup->fu_name, name, len + 1);
	else
		(void)snprintf(fup->fu_name, len, "%u", id);
	return (fup);
}

/*
 * Special purpose version of ginode used to optimize pass
 * over all the inodes in numerical order.
 */
static ino_t nextino, lastinum, lastvalidinum;
static long readcnt, readpercg, fullcnt, inobufsize, partialcnt, partialsize;
static caddr_t inodebuf;
#define	INOBUFSIZE	56*1024		/* size of buffer to read inodes */

union dinode *
getnextinode(ino_t inumber)
{
	long size;
	daddr_t dblk;
	union dinode *dp;
	static caddr_t nextinop;

	if (inumber != nextino++ || inumber > lastvalidinum)
		err(1, "bad inode number %llu to nextinode",
		    (unsigned long long)inumber);
	if (inumber >= lastinum) {
		readcnt++;
		dblk = fsbtodb(&sblock, ino_to_fsba(&sblock, lastinum));
		if (readcnt % readpercg == 0) {
			size = partialsize;
			lastinum += partialcnt;
		} else {
			size = inobufsize;
			lastinum += fullcnt;
		}
		/*
		 * If bread returns an error, it will already have zeroed
		 * out the buffer, so we do not need to do so here.
		 */
		bread(dblk, inodebuf, size);
		nextinop = inodebuf;
	}
	dp = (union dinode *)nextinop;
	if (sblock.fs_magic == FS_UFS1_MAGIC)
		nextinop += sizeof(struct ufs1_dinode);
	else
		nextinop += sizeof(struct ufs2_dinode);
	return (dp);
}

/*
 * Prepare to scan a set of inodes.
 */
void
setinodebuf(ino_t inum)
{

	if (inum % sblock.fs_ipg != 0)
		errx(1, "bad inode number %llu to setinodebuf",
		    (unsigned long long)inum);
	lastvalidinum = inum + sblock.fs_ipg - 1;
	nextino = inum;
	lastinum = inum;
	readcnt = 0;
	if (inodebuf != NULL)
		return;
	inobufsize = blkroundup(&sblock, INOBUFSIZE);
	fullcnt = inobufsize / ((sblock.fs_magic == FS_UFS1_MAGIC) ?
	    sizeof(struct ufs1_dinode) : sizeof(struct ufs2_dinode));
	readpercg = sblock.fs_ipg / fullcnt;
	partialcnt = sblock.fs_ipg % fullcnt;
	partialsize = partialcnt * ((sblock.fs_magic == FS_UFS1_MAGIC) ?
	    sizeof(struct ufs1_dinode) : sizeof(struct ufs2_dinode));
	if (partialcnt != 0) {
		readpercg++;
	} else {
		partialcnt = fullcnt;
		partialsize = inobufsize;
	}
	if ((inodebuf = malloc((size_t)inobufsize)) == NULL)
		errx(1, "cannot allocate space for inode buffer");
}

/*
 * Free up data structures used to scan inodes.
 */
void
freeinodebuf(void)
{

	free(inodebuf);
	inodebuf = NULL;
}

/*
 * Read specified disk blocks.
 */
void
bread(daddr_t bno, char *buf, long cnt)
{
	if (pread(fi, buf, cnt, bno * DEV_BSIZE) != cnt)
		err(1, "read failed on block %lld", (long long)bno);
}
