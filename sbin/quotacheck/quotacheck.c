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
static char sccsid[] = "@(#)quotacheck.c	8.3 (Berkeley) 1/29/94";
#endif /* not lint */
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Fix up / report on disk quotas & usage
 */
#include <sys/param.h>
#include <sys/disklabel.h>
#include <sys/mount.h>
#include <sys/stat.h>

#include <ufs/ufs/dinode.h>
#include <ufs/ufs/quota.h>
#include <ufs/ffs/fs.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <fstab.h>
#include <grp.h>
#include <libufs.h>
#include <libutil.h>
#include <pwd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "quotacheck.h"

const char *qfname = QUOTAFILENAME;
const char *qfextension[] = INITQFNAMES;
const char *quotagroup = QUOTAGROUP;

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
long dev_bsize = 1;
ino_t maxino;

union dinode {
	struct ufs1_dinode dp1;
	struct ufs2_dinode dp2;
};
#define	DIP(dp, field) \
	((sblock.fs_magic == FS_UFS1_MAGIC) ? \
	(dp)->dp1.field : (dp)->dp2.field)

#define	HASUSR	1
#define	HASGRP	2

struct fileusage {
	struct	fileusage *fu_next;
	u_long	fu_curinodes;
	u_long	fu_curblocks;
	u_long	fu_id;
	char	fu_name[1];
	/* actually bigger */
};
#define FUHASH 1024	/* must be power of two */
struct fileusage *fuhead[MAXQUOTAS][FUHASH];

int	aflag;			/* all file systems */
int	cflag;			/* convert format to 32 or 64 bit size */
int	gflag;			/* check group quotas */
int	uflag;			/* check user quotas */
int	vflag;			/* verbose */
int	fi;			/* open disk file descriptor */

struct fileusage *
	 addid(u_long, int, char *, const char *);
void	 blkread(ufs2_daddr_t, char *, long);
void	 freeinodebuf(void);
union dinode *
	 getnextinode(ino_t);
int	 getquotagid(void);
struct fileusage *
	 lookup(u_long, int);
int	 oneof(char *, char*[], int);
void	 printchanges(const char *, int, struct dqblk *, struct fileusage *,
	    u_long);
void	 setinodebuf(ino_t);
int	 update(const char *, struct quotafile *, int);
void	 usage(void);

int
main(int argc, char *argv[])
{
	struct fstab *fs;
	struct passwd *pw;
	struct group *gr;
	struct quotafile *qfu, *qfg;
	int i, argnum, maxrun, errs, ch;
	long done = 0;
	char *name;

	errs = maxrun = 0;
	while ((ch = getopt(argc, argv, "ac:guvl:")) != -1) {
		switch(ch) {
		case 'a':
			aflag++;
			break;
		case 'c':
			if (cflag)
				usage();
			cflag = atoi(optarg);
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
		case 'l':
			maxrun = atoi(optarg);
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if ((argc == 0 && !aflag) || (argc > 0 && aflag))
		usage();
	if (cflag && cflag != 32 && cflag != 64)
		usage();
	if (!gflag && !uflag) {
		gflag++;
		uflag++;
	}
	if (gflag) {
		setgrent();
		while ((gr = getgrent()) != NULL)
			(void) addid((u_long)gr->gr_gid, GRPQUOTA, gr->gr_name,
			    NULL);
		endgrent();
	}
	if (uflag) {
		setpwent();
		while ((pw = getpwent()) != NULL)
			(void) addid((u_long)pw->pw_uid, USRQUOTA, pw->pw_name,
			    NULL);
		endpwent();
	}
	/*
	 * The maxrun (-l) option is now deprecated.
	 */
	if (maxrun > 0)
		warnx("the -l option is now deprecated");
	if (aflag)
		exit(checkfstab(uflag, gflag));
	if (setfsent() == 0)
		errx(1, "%s: can't open", FSTAB);
	while ((fs = getfsent()) != NULL) {
		if (((argnum = oneof(fs->fs_file, argv, argc)) >= 0 ||
		     (argnum = oneof(fs->fs_spec, argv, argc)) >= 0) &&
		    (name = blockcheck(fs->fs_spec))) {
			done |= 1 << argnum;
			qfu = NULL;
			if (uflag)
				qfu = quota_open(fs, USRQUOTA, O_CREAT|O_RDWR);
			qfg = NULL;
			if (gflag)
				qfg = quota_open(fs, GRPQUOTA, O_CREAT|O_RDWR);
			if (qfu == NULL && qfg == NULL)
				continue;
			errs += chkquota(name, qfu, qfg);
			if (qfu)
				quota_close(qfu);
			if (qfg)
				quota_close(qfg);
		}
	}
	endfsent();
	for (i = 0; i < argc; i++)
		if ((done & (1 << i)) == 0)
			fprintf(stderr, "%s not found in %s\n",
				argv[i], FSTAB);
	exit(errs);
}

void
usage(void)
{
	(void)fprintf(stderr, "%s\n%s\n",
		"usage: quotacheck [-guv] [-c 32 | 64] [-l maxrun] -a",
		"       quotacheck [-guv] [-c 32 | 64] filesystem ...");
	exit(1);
}

/*
 * Scan the specified file system to check quota(s) present on it.
 */
int
chkquota(char *specname, struct quotafile *qfu, struct quotafile *qfg)
{
	struct fileusage *fup;
	union dinode *dp;
	struct fs *fs;
	int i, ret, mode, errs = 0;
	u_int32_t cg;
	ino_t curino, ino, inosused, userino = 0, groupino = 0;
	dev_t dev, userdev = 0, groupdev = 0;
	struct stat sb;
	const char *mntpt;
	char *cp;

	if (qfu != NULL)
		mntpt = quota_fsname(qfu);
	else if (qfg != NULL)
		mntpt = quota_fsname(qfg);
	else
		errx(1, "null quotafile information passed to chkquota()\n");
	if (cflag) {
		if (vflag && qfu != NULL)
			printf("%s: convert user quota to %d bits\n",
			    mntpt, cflag);
		if (qfu != NULL && quota_convert(qfu, cflag) < 0) {
			if (errno == EBADF)
				errx(1,
				    "%s: cannot convert an active quota file",
				    mntpt);
			err(1, "user quota conversion to size %d failed",
			    cflag);
		}
		if (vflag && qfg != NULL)
			printf("%s: convert group quota to %d bits\n",
			    mntpt, cflag);
		if (qfg != NULL && quota_convert(qfg, cflag) < 0) {
			if (errno == EBADF)
				errx(1,
				    "%s: cannot convert an active quota file",
				    mntpt);
			err(1, "group quota conversion to size %d failed",
			    cflag);
		}
	}
	if ((fi = open(specname, O_RDONLY, 0)) < 0) {
		warn("%s", specname);
		return (1);
	}
	if ((stat(mntpt, &sb)) < 0) {
		warn("%s", mntpt);
		return (1);
	}
	dev = sb.st_dev;
	if (vflag) {
		(void)printf("*** Checking ");
		if (qfu)
			(void)printf("user%s", qfg ? " and " : "");
		if (qfg)
			(void)printf("group");
		(void)printf(" quotas for %s (%s)\n", specname, mntpt);
	}
	if (qfu) {
		if (stat(quota_qfname(qfu), &sb) == 0) {
			userino = sb.st_ino;
			userdev = sb.st_dev;
		}
	}
	if (qfg) {
		if (stat(quota_qfname(qfg), &sb) == 0) {
			groupino = sb.st_ino;
			groupdev = sb.st_dev;
		}
	}
	sync();
	if ((ret = sbget(fi, &fs, STDSB)) != 0) {
		switch (ret) {
		case ENOENT:
			warn("Cannot find file system superblock");
			return (1);
		default:
			warn("Unable to read file system superblock");
			return (1);
		}
	}
	bcopy(fs, &sblock, fs->fs_sbsize);
	free(fs);
	dev_bsize = sblock.fs_fsize / fsbtodb(&sblock, 1);
	maxino = sblock.fs_ncg * sblock.fs_ipg;
	for (cg = 0; cg < sblock.fs_ncg; cg++) {
		ino = cg * sblock.fs_ipg;
		setinodebuf(ino);
		blkread(fsbtodb(&sblock, cgtod(&sblock, cg)), (char *)(&cgblk),
		    sblock.fs_cgsize);
		if (sblock.fs_magic == FS_UFS2_MAGIC)
			inosused = cgblk.cg_initediblk;
		else
			inosused = sblock.fs_ipg;
		/*
		 * If we are using soft updates, then we can trust the
		 * cylinder group inode allocation maps to tell us which
		 * inodes are allocated. We will scan the used inode map
		 * to find the inodes that are really in use, and then
		 * read only those inodes in from disk.
		 */
		if (sblock.fs_flags & FS_DOSOFTDEP) {
			if (!cg_chkmagic(&cgblk))
				errx(1, "CG %d: BAD MAGIC NUMBER\n", cg);
			cp = &cg_inosused(&cgblk)[(inosused - 1) / CHAR_BIT];
			for ( ; inosused > 0; inosused -= CHAR_BIT, cp--) {
				if (*cp == 0)
					continue;
				for (i = 1 << (CHAR_BIT - 1); i > 0; i >>= 1) {
					if (*cp & i)
						break;
					inosused--;
				}
				break;
			}
			if (inosused <= 0)
				continue;
		}
		for (curino = 0; curino < inosused; curino++, ino++) {
			if ((dp = getnextinode(ino)) == NULL ||
			    ino < UFS_ROOTINO ||
			    (mode = DIP(dp, di_mode) & IFMT) == 0)
				continue;
			/*
			 * XXX: Do not account for UIDs or GIDs that appear
			 * to be negative to prevent generating 100GB+
			 * quota files.
			 */
			if ((int)DIP(dp, di_uid) < 0 ||
			    (int)DIP(dp, di_gid) < 0) {
				if (vflag) {
					if (aflag)
						(void)printf("%s: ", mntpt);
			(void)printf("out of range UID/GID (%u/%u) ino=%ju\n",
					    DIP(dp, di_uid), DIP(dp,di_gid),
					    (uintmax_t)ino);
				}
				continue;
			}

			/*
			 * Do not account for file system snapshot files
			 * or the actual quota data files to be consistent
			 * with how they are handled inside the kernel.
			 */
#ifdef	SF_SNAPSHOT
			if (DIP(dp, di_flags) & SF_SNAPSHOT)
				continue;
#endif
			if ((ino == userino && dev == userdev) ||
			    (ino == groupino && dev == groupdev))
				continue;
			if (qfg) {
				fup = addid((u_long)DIP(dp, di_gid), GRPQUOTA,
				    NULL, mntpt);
				fup->fu_curinodes++;
				if (mode == IFREG || mode == IFDIR ||
				    mode == IFLNK)
					fup->fu_curblocks += DIP(dp, di_blocks);
			}
			if (qfu) {
				fup = addid((u_long)DIP(dp, di_uid), USRQUOTA,
				    NULL, mntpt);
				fup->fu_curinodes++;
				if (mode == IFREG || mode == IFDIR ||
				    mode == IFLNK)
					fup->fu_curblocks += DIP(dp, di_blocks);
			}
		}
	}
	freeinodebuf();
	if (qfu)
		errs += update(mntpt, qfu, USRQUOTA);
	if (qfg)
		errs += update(mntpt, qfg, GRPQUOTA);
	close(fi);
	(void)fflush(stdout);
	return (errs);
}

/*
 * Update a specified quota file.
 */
int
update(const char *fsname, struct quotafile *qf, int type)
{
	struct fileusage *fup;
	u_long id, lastid, highid = 0;
	struct dqblk dqbuf;
	struct stat sb;
	static struct dqblk zerodqbuf;
	static struct fileusage zerofileusage;

	/*
	 * Scan the on-disk quota file and record any usage changes.
	 */
	lastid = quota_maxid(qf);
	for (id = 0; id <= lastid; id++) {
		if (quota_read(qf, &dqbuf, id) < 0)
			dqbuf = zerodqbuf;
		if ((fup = lookup(id, type)) == NULL)
			fup = &zerofileusage;
		if (fup->fu_curinodes || fup->fu_curblocks ||
		    dqbuf.dqb_bsoftlimit || dqbuf.dqb_bhardlimit ||
		    dqbuf.dqb_isoftlimit || dqbuf.dqb_ihardlimit)
			highid = id;
		if (dqbuf.dqb_curinodes == fup->fu_curinodes &&
		    dqbuf.dqb_curblocks == fup->fu_curblocks) {
			fup->fu_curinodes = 0;
			fup->fu_curblocks = 0;
			continue;
		}
		printchanges(fsname, type, &dqbuf, fup, id);
		dqbuf.dqb_curinodes = fup->fu_curinodes;
		dqbuf.dqb_curblocks = fup->fu_curblocks;
		(void) quota_write_usage(qf, &dqbuf, id);
		fup->fu_curinodes = 0;
		fup->fu_curblocks = 0;
	}

	/*
	 * Walk the hash table looking for ids with non-zero usage
	 * that are not currently recorded in the quota file. E.g.
	 * ids that are past the end of the current file.
	 */
	for (id = 0; id < FUHASH; id++) {
		for (fup = fuhead[type][id]; fup != NULL; fup = fup->fu_next) {
			if (fup->fu_id <= lastid)
				continue;
			if (fup->fu_curinodes == 0 && fup->fu_curblocks == 0)
				continue;
			bzero(&dqbuf, sizeof(struct dqblk));
			if (fup->fu_id > highid)
				highid = fup->fu_id;
			printchanges(fsname, type, &dqbuf, fup, fup->fu_id);
			dqbuf.dqb_curinodes = fup->fu_curinodes;
			dqbuf.dqb_curblocks = fup->fu_curblocks;
			(void) quota_write_usage(qf, &dqbuf, fup->fu_id);
			fup->fu_curinodes = 0;
			fup->fu_curblocks = 0;
		}
	}
	/*
	 * If this is old format file, then size may be smaller,
	 * so ensure that we only truncate when it will make things
	 * smaller, and not if it will grow an old format file.
	 */
	if (highid < lastid &&
	    stat(quota_qfname(qf), &sb) == 0 &&
	    sb.st_size > (off_t)((highid + 2) * sizeof(struct dqblk)))
		truncate(quota_qfname(qf),
		    (((off_t)highid + 2) * sizeof(struct dqblk)));
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
 * Routines to manage the file usage table.
 *
 * Lookup an id of a specific type.
 */
struct fileusage *
lookup(u_long id, int type)
{
	struct fileusage *fup;

	for (fup = fuhead[type][id & (FUHASH-1)]; fup != NULL; fup = fup->fu_next)
		if (fup->fu_id == id)
			return (fup);
	return (NULL);
}

/*
 * Add a new file usage id if it does not already exist.
 */
struct fileusage *
addid(u_long id, int type, char *name, const char *fsname)
{
	struct fileusage *fup, **fhp;
	int len;

	if ((fup = lookup(id, type)) != NULL)
		return (fup);
	if (name)
		len = strlen(name);
	else
		len = 0;
	if ((fup = calloc(1, sizeof(*fup) + len)) == NULL)
		errx(1, "calloc failed");
	fhp = &fuhead[type][id & (FUHASH - 1)];
	fup->fu_next = *fhp;
	*fhp = fup;
	fup->fu_id = id;
	if (name)
		bcopy(name, fup->fu_name, len + 1);
	else {
		(void)sprintf(fup->fu_name, "%lu", id);
		if (vflag) {
			if (aflag && fsname != NULL)
				(void)printf("%s: ", fsname);
			printf("unknown %cid: %lu\n",
			    type == USRQUOTA ? 'u' : 'g', id);
		}
	}
	return (fup);
}

/*
 * Special purpose version of ginode used to optimize pass
 * over all the inodes in numerical order.
 */
static ino_t nextino, lastinum, lastvalidinum;
static long readcnt, readpercg, fullcnt, inobufsize, partialcnt, partialsize;
static caddr_t inodebuf;
#define INOBUFSIZE	56*1024		/* size of buffer to read inodes */

union dinode *
getnextinode(ino_t inumber)
{
	long size;
	ufs2_daddr_t dblk;
	union dinode *dp;
	static caddr_t nextinop;

	if (inumber != nextino++ || inumber > lastvalidinum)
		errx(1, "bad inode number %ju to nextinode",
		    (uintmax_t)inumber);
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
		 * If blkread returns an error, it will already have zeroed
		 * out the buffer, so we do not need to do so here.
		 */
		blkread(dblk, inodebuf, size);
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
		errx(1, "bad inode number %ju to setinodebuf", (uintmax_t)inum);
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
	if ((inodebuf = malloc((unsigned)inobufsize)) == NULL)
		errx(1, "cannot allocate space for inode buffer");
}

/*
 * Free up data structures used to scan inodes.
 */
void
freeinodebuf(void)
{

	if (inodebuf != NULL)
		free(inodebuf);
	inodebuf = NULL;
}

/*
 * Read specified disk blocks.
 */
void
blkread(ufs2_daddr_t bno, char *buf, long cnt)
{

	if (lseek(fi, (off_t)bno * dev_bsize, SEEK_SET) < 0 ||
	    read(fi, buf, cnt) != cnt)
		errx(1, "blkread failed on block %ld", (long)bno);
}

/*
 * Display updated block and i-node counts.
 */
void
printchanges(const char *fsname, int type, struct dqblk *dp,
    struct fileusage *fup, u_long id)
{
	if (!vflag)
		return;
	if (aflag)
		(void)printf("%s: ", fsname);
	if (fup->fu_name[0] == '\0')
		(void)printf("%-8lu fixed ", id);
	else
		(void)printf("%-8s fixed ", fup->fu_name);
	switch (type) {

	case GRPQUOTA:
		(void)printf("(group):");
		break;

	case USRQUOTA:
		(void)printf("(user): ");
		break;

	default:
		(void)printf("(unknown quota type %d)", type);
		break;
	}
	if (dp->dqb_curinodes != fup->fu_curinodes)
		(void)printf("\tinodes %lu -> %lu", (u_long)dp->dqb_curinodes,
		    (u_long)fup->fu_curinodes);
	if (dp->dqb_curblocks != fup->fu_curblocks)
		(void)printf("\tblocks %lu -> %lu",
		    (u_long)dp->dqb_curblocks,
		    (u_long)fup->fu_curblocks);
	(void)printf("\n");
}
