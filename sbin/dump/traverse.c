/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1980, 1988, 1991, 1993
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
#if 0
static char sccsid[] = "@(#)traverse.c	8.7 (Berkeley) 6/15/95";
#endif
static const char rcsid[] =
  "$FreeBSD$";
#endif /* not lint */

#include <sys/param.h>
#include <sys/stat.h>

#include <ufs/ufs/dir.h>
#include <ufs/ufs/dinode.h>
#include <ufs/ffs/fs.h>

#include <protocols/dumprestore.h>

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <timeconv.h>
#include <unistd.h>

#include "dump.h"

union dinode {
	struct ufs1_dinode dp1;
	struct ufs2_dinode dp2;
};
#define	DIP(dp, field) \
	((sblock->fs_magic == FS_UFS1_MAGIC) ? \
	(dp)->dp1.field : (dp)->dp2.field)
#define DIP_SET(dp, field, val) do {\
	if (sblock->fs_magic == FS_UFS1_MAGIC) \
		(dp)->dp1.field = (val); \
	else \
		(dp)->dp2.field = (val); \
	} while (0)

#define	HASDUMPEDFILE	0x1
#define	HASSUBDIRS	0x2

static	int dirindir(ino_t ino, ufs2_daddr_t blkno, int level, long *size,
    long *tapesize, int nodump, ino_t maxino);
static	void dmpindir(union dinode *dp, ino_t ino, ufs2_daddr_t blk, int level,
    off_t *size);
static	void ufs1_blksout(ufs1_daddr_t *blkp, int frags, ino_t ino);
static	void ufs2_blksout(union dinode *dp, ufs2_daddr_t *blkp, int frags,
    ino_t ino, int last);
static	int appendextdata(union dinode *dp);
static	void writeextdata(union dinode *dp, ino_t ino, int added);
static	int searchdir(ino_t ino, ufs2_daddr_t blkno, long size, long filesize,
    long *tapesize, int nodump, ino_t maxino);
static	long blockest(union dinode *dp);

/*
 * This is an estimation of the number of TP_BSIZE blocks in the file.
 * It estimates the number of blocks in files with holes by assuming
 * that all of the blocks accounted for by di_blocks are data blocks
 * (when some of the blocks are usually used for indirect pointers);
 * hence the estimate may be high.
 */
static long
blockest(union dinode *dp)
{
	long blkest, sizeest;

	/*
	 * dp->di_size is the size of the file in bytes.
	 * dp->di_blocks stores the number of sectors actually in the file.
	 * If there are more sectors than the size would indicate, this just
	 *	means that there are indirect blocks in the file or unused
	 *	sectors in the last file block; we can safely ignore these
	 *	(blkest = sizeest below).
	 * If the file is bigger than the number of sectors would indicate,
	 *	then the file has holes in it.	In this case we must use the
	 *	block count to estimate the number of data blocks used, but
	 *	we use the actual size for estimating the number of indirect
	 *	dump blocks (sizeest vs. blkest in the indirect block
	 *	calculation).
	 */
	if ((DIP(dp, di_flags) & SF_SNAPSHOT) != 0)
		return (1);
	blkest = howmany(dbtob(DIP(dp, di_blocks)), TP_BSIZE);
	sizeest = howmany(DIP(dp, di_size), TP_BSIZE);
	if (blkest > sizeest)
		blkest = sizeest;
	if (DIP(dp, di_size) > sblock->fs_bsize * UFS_NDADDR) {
		/* calculate the number of indirect blocks on the dump tape */
		blkest += howmany(sizeest -
		    UFS_NDADDR * sblock->fs_bsize / TP_BSIZE, TP_NINDIR);
	}
	return (blkest + 1);
}

/* Auxiliary macro to pick up files changed since previous dump. */
#define	CHANGEDSINCE(dp, t) \
	(DIP(dp, di_mtime) >= (t) || DIP(dp, di_ctime) >= (t))

/* The WANTTODUMP macro decides whether a file should be dumped. */
#ifdef UF_NODUMP
#define	WANTTODUMP(dp) \
	(CHANGEDSINCE(dp, spcl.c_ddate) && \
	 (nonodump || (DIP(dp, di_flags) & UF_NODUMP) != UF_NODUMP))
#else
#define	WANTTODUMP(dp) CHANGEDSINCE(dp, spcl.c_ddate)
#endif

/*
 * Dump pass 1.
 *
 * Walk the inode list for a file system to find all allocated inodes
 * that have been modified since the previous dump time. Also, find all
 * the directories in the file system.
 */
int
mapfiles(ino_t maxino, long *tapesize)
{
	int i, cg, mode, inosused;
	int anydirskipped = 0;
	union dinode *dp;
	struct cg *cgp;
	ino_t ino;
	u_char *cp;

	if ((cgp = malloc(sblock->fs_cgsize)) == NULL)
		quit("mapfiles: cannot allocate memory.\n");
	for (cg = 0; cg < sblock->fs_ncg; cg++) {
		ino = cg * sblock->fs_ipg;
		blkread(fsbtodb(sblock, cgtod(sblock, cg)), (char *)cgp,
		    sblock->fs_cgsize);
		if (sblock->fs_magic == FS_UFS2_MAGIC)
			inosused = cgp->cg_initediblk;
		else
			inosused = sblock->fs_ipg;
		/*
		 * If we are using soft updates, then we can trust the
		 * cylinder group inode allocation maps to tell us which
		 * inodes are allocated. We will scan the used inode map
		 * to find the inodes that are really in use, and then
		 * read only those inodes in from disk.
		 */
		if (sblock->fs_flags & FS_DOSOFTDEP) {
			if (!cg_chkmagic(cgp))
				quit("mapfiles: cg %d: bad magic number\n", cg);
			cp = &cg_inosused(cgp)[(inosused - 1) / CHAR_BIT];
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
		for (i = 0; i < inosused; i++, ino++) {
			if (ino < UFS_ROOTINO ||
			    (dp = getino(ino, &mode)) == NULL ||
			    (mode & IFMT) == 0)
				continue;
			if (ino >= maxino) {
				msg("Skipping inode %ju >= maxino %ju\n",
				    (uintmax_t)ino, (uintmax_t)maxino);
				continue;
			}
			/*
			 * Everything must go in usedinomap so that a check
			 * for "in dumpdirmap but not in usedinomap" to detect
			 * dirs with nodump set has a chance of succeeding
			 * (this is used in mapdirs()).
			 */
			SETINO(ino, usedinomap);
			if (mode == IFDIR)
				SETINO(ino, dumpdirmap);
			if (WANTTODUMP(dp)) {
				SETINO(ino, dumpinomap);
				if (mode != IFREG &&
				    mode != IFDIR &&
				    mode != IFLNK)
					*tapesize += 1;
				else
					*tapesize += blockest(dp);
				continue;
			}
			if (mode == IFDIR) {
				if (!nonodump &&
				    (DIP(dp, di_flags) & UF_NODUMP))
					CLRINO(ino, usedinomap);
				anydirskipped = 1;
			}
		}
	}
	/*
	 * Restore gets very upset if the root is not dumped,
	 * so ensure that it always is dumped.
	 */
	SETINO(UFS_ROOTINO, dumpinomap);
	return (anydirskipped);
}

/*
 * Dump pass 2.
 *
 * Scan each directory on the file system to see if it has any modified
 * files in it. If it does, and has not already been added to the dump
 * list (because it was itself modified), then add it. If a directory
 * has not been modified itself, contains no modified files and has no
 * subdirectories, then it can be deleted from the dump list and from
 * the list of directories. By deleting it from the list of directories,
 * its parent may now qualify for the same treatment on this or a later
 * pass using this algorithm.
 */
int
mapdirs(ino_t maxino, long *tapesize)
{
	union dinode *dp;
	int i, isdir, nodump;
	char *map;
	ino_t ino;
	union dinode di;
	long filesize;
	int ret, change = 0;

	isdir = 0;		/* XXX just to get gcc to shut up */
	for (map = dumpdirmap, ino = 1; ino < maxino; ino++) {
		if (((ino - 1) % CHAR_BIT) == 0)	/* map is offset by 1 */
			isdir = *map++;
		else
			isdir >>= 1;
		/*
		 * If a directory has been removed from usedinomap, it
		 * either has the nodump flag set, or has inherited
		 * it.  Although a directory can't be in dumpinomap if
		 * it isn't in usedinomap, we have to go through it to
		 * propagate the nodump flag.
		 */
		nodump = !nonodump && (TSTINO(ino, usedinomap) == 0);
		if ((isdir & 1) == 0 || (TSTINO(ino, dumpinomap) && !nodump))
			continue;
		dp = getino(ino, &i);
		/*
		 * inode buf may change in searchdir().
		 */
		if (sblock->fs_magic == FS_UFS1_MAGIC)
			di.dp1 = dp->dp1;
		else
			di.dp2 = dp->dp2;
		filesize = DIP(&di, di_size);
		for (ret = 0, i = 0; filesize > 0 && i < UFS_NDADDR; i++) {
			if (DIP(&di, di_db[i]) != 0)
				ret |= searchdir(ino, DIP(&di, di_db[i]),
				    (long)sblksize(sblock, DIP(&di, di_size),
				    i), filesize, tapesize, nodump, maxino);
			if (ret & HASDUMPEDFILE)
				filesize = 0;
			else
				filesize -= sblock->fs_bsize;
		}
		for (i = 0; filesize > 0 && i < UFS_NIADDR; i++) {
			if (DIP(&di, di_ib[i]) == 0)
				continue;
			ret |= dirindir(ino, DIP(&di, di_ib[i]), i, &filesize,
			    tapesize, nodump, maxino);
		}
		if (ret & HASDUMPEDFILE) {
			SETINO(ino, dumpinomap);
			*tapesize += blockest(&di);
			change = 1;
			continue;
		}
		if (nodump) {
			if (ret & HASSUBDIRS)
				change = 1;	/* subdirs inherit nodump */
			CLRINO(ino, dumpdirmap);
		} else if ((ret & HASSUBDIRS) == 0)
			if (!TSTINO(ino, dumpinomap)) {
				CLRINO(ino, dumpdirmap);
				change = 1;
			}
	}
	return (change);
}

/*
 * Read indirect blocks, and pass the data blocks to be searched
 * as directories. Quit as soon as any entry is found that will
 * require the directory to be dumped.
 */
static int
dirindir(
	ino_t ino,
	ufs2_daddr_t blkno,
	int ind_level,
	long *filesize,
	long *tapesize,
	int nodump,
	ino_t maxino)
{
	union {
		ufs1_daddr_t ufs1[MAXBSIZE / sizeof(ufs1_daddr_t)];
		ufs2_daddr_t ufs2[MAXBSIZE / sizeof(ufs2_daddr_t)];
	} idblk;
	int ret = 0;
	int i;

	blkread(fsbtodb(sblock, blkno), (char *)&idblk, (int)sblock->fs_bsize);
	if (ind_level <= 0) {
		for (i = 0; *filesize > 0 && i < NINDIR(sblock); i++) {
			if (sblock->fs_magic == FS_UFS1_MAGIC)
				blkno = idblk.ufs1[i];
			else
				blkno = idblk.ufs2[i];
			if (blkno != 0)
				ret |= searchdir(ino, blkno, sblock->fs_bsize,
					*filesize, tapesize, nodump, maxino);
			if (ret & HASDUMPEDFILE)
				*filesize = 0;
			else
				*filesize -= sblock->fs_bsize;
		}
		return (ret);
	}
	ind_level--;
	for (i = 0; *filesize > 0 && i < NINDIR(sblock); i++) {
		if (sblock->fs_magic == FS_UFS1_MAGIC)
			blkno = idblk.ufs1[i];
		else
			blkno = idblk.ufs2[i];
		if (blkno != 0)
			ret |= dirindir(ino, blkno, ind_level, filesize,
			    tapesize, nodump, maxino);
	}
	return (ret);
}

/*
 * Scan a disk block containing directory information looking to see if
 * any of the entries are on the dump list and to see if the directory
 * contains any subdirectories.
 */
static int
searchdir(
	ino_t ino,
	ufs2_daddr_t blkno,
	long size,
	long filesize, 
	long *tapesize,
	int nodump,
	ino_t maxino)
{
	int mode;
	struct direct *dp;
	union dinode *ip;
	long loc, ret = 0;
	static caddr_t dblk;

	if (dblk == NULL && (dblk = malloc(sblock->fs_bsize)) == NULL)
		quit("searchdir: cannot allocate indirect memory.\n");
	blkread(fsbtodb(sblock, blkno), dblk, (int)size);
	if (filesize < size)
		size = filesize;
	for (loc = 0; loc < size; ) {
		dp = (struct direct *)(dblk + loc);
		if (dp->d_reclen == 0) {
			msg("corrupted directory, inumber %ju\n",
			    (uintmax_t)ino);
			break;
		}
		loc += dp->d_reclen;
		if (dp->d_ino == 0)
			continue;
		if (dp->d_ino >= maxino) {
			msg("corrupted directory entry, d_ino %ju >= %ju\n",
			    (uintmax_t)dp->d_ino, (uintmax_t)maxino);
			break;
		}
		if (dp->d_name[0] == '.') {
			if (dp->d_name[1] == '\0')
				continue;
			if (dp->d_name[1] == '.' && dp->d_name[2] == '\0')
				continue;
		}
		if (nodump) {
			ip = getino(dp->d_ino, &mode);
			if (TSTINO(dp->d_ino, dumpinomap)) {
				CLRINO(dp->d_ino, dumpinomap);
				*tapesize -= blockest(ip);
			}
			/*
			 * Add back to dumpdirmap and remove from usedinomap
			 * to propagate nodump.
			 */
			if (mode == IFDIR) {
				SETINO(dp->d_ino, dumpdirmap);
				CLRINO(dp->d_ino, usedinomap);
				ret |= HASSUBDIRS;
			}
		} else {
			if (TSTINO(dp->d_ino, dumpinomap)) {
				ret |= HASDUMPEDFILE;
				if (ret & HASSUBDIRS)
					break;
			}
			if (TSTINO(dp->d_ino, dumpdirmap)) {
				ret |= HASSUBDIRS;
				if (ret & HASDUMPEDFILE)
					break;
			}
		}
	}
	return (ret);
}

/*
 * Dump passes 3 and 4.
 *
 * Dump the contents of an inode to tape.
 */
void
dumpino(union dinode *dp, ino_t ino)
{
	int ind_level, cnt, last, added;
	off_t size;
	char buf[TP_BSIZE];

	if (newtape) {
		newtape = 0;
		dumpmap(dumpinomap, TS_BITS, ino);
	}
	CLRINO(ino, dumpinomap);
	/*
	 * Zero out the size of a snapshot so that it will be dumped
	 * as a zero length file.
	 */
	if ((DIP(dp, di_flags) & SF_SNAPSHOT) != 0) {
		DIP_SET(dp, di_size, 0);
		DIP_SET(dp, di_flags, DIP(dp, di_flags) & ~SF_SNAPSHOT);
	}
	if (sblock->fs_magic == FS_UFS1_MAGIC) {
		spcl.c_mode = dp->dp1.di_mode;
		spcl.c_size = dp->dp1.di_size;
		spcl.c_extsize = 0;
		spcl.c_atime = _time32_to_time(dp->dp1.di_atime);
		spcl.c_atimensec = dp->dp1.di_atimensec;
		spcl.c_mtime = _time32_to_time(dp->dp1.di_mtime);
		spcl.c_mtimensec = dp->dp1.di_mtimensec;
		spcl.c_birthtime = 0;
		spcl.c_birthtimensec = 0;
		spcl.c_rdev = dp->dp1.di_rdev;
		spcl.c_file_flags = dp->dp1.di_flags;
		spcl.c_uid = dp->dp1.di_uid;
		spcl.c_gid = dp->dp1.di_gid;
	} else {
		spcl.c_mode = dp->dp2.di_mode;
		spcl.c_size = dp->dp2.di_size;
		spcl.c_extsize = dp->dp2.di_extsize;
		spcl.c_atime = _time64_to_time(dp->dp2.di_atime);
		spcl.c_atimensec = dp->dp2.di_atimensec;
		spcl.c_mtime = _time64_to_time(dp->dp2.di_mtime);
		spcl.c_mtimensec = dp->dp2.di_mtimensec;
		spcl.c_birthtime = _time64_to_time(dp->dp2.di_birthtime);
		spcl.c_birthtimensec = dp->dp2.di_birthnsec;
		spcl.c_rdev = dp->dp2.di_rdev;
		spcl.c_file_flags = dp->dp2.di_flags;
		spcl.c_uid = dp->dp2.di_uid;
		spcl.c_gid = dp->dp2.di_gid;
	}
	spcl.c_type = TS_INODE;
	spcl.c_count = 0;
	switch (DIP(dp, di_mode) & S_IFMT) {

	case 0:
		/*
		 * Freed inode.
		 */
		return;

	case S_IFLNK:
		/*
		 * Check for short symbolic link.
		 */
		if (DIP(dp, di_size) > 0 &&
		    DIP(dp, di_size) < sblock->fs_maxsymlinklen) {
			spcl.c_addr[0] = 1;
			spcl.c_count = 1;
			added = appendextdata(dp);
			writeheader(ino);
			if (sblock->fs_magic == FS_UFS1_MAGIC)
				memmove(buf, (caddr_t)dp->dp1.di_db,
				    (u_long)DIP(dp, di_size));
			else
				memmove(buf, (caddr_t)dp->dp2.di_db,
				    (u_long)DIP(dp, di_size));
			buf[DIP(dp, di_size)] = '\0';
			writerec(buf, 0);
			writeextdata(dp, ino, added);
			return;
		}
		/* FALLTHROUGH */

	case S_IFDIR:
	case S_IFREG:
		if (DIP(dp, di_size) > 0)
			break;
		/* FALLTHROUGH */

	case S_IFIFO:
	case S_IFSOCK:
	case S_IFCHR:
	case S_IFBLK:
		added = appendextdata(dp);
		writeheader(ino);
		writeextdata(dp, ino, added);
		return;

	default:
		msg("Warning: undefined file type 0%o\n",
		    DIP(dp, di_mode) & IFMT);
		return;
	}
	if (DIP(dp, di_size) > UFS_NDADDR * sblock->fs_bsize) {
		cnt = UFS_NDADDR * sblock->fs_frag;
		last = 0;
	} else {
		cnt = howmany(DIP(dp, di_size), sblock->fs_fsize);
		last = 1;
	}
	if (sblock->fs_magic == FS_UFS1_MAGIC)
		ufs1_blksout(&dp->dp1.di_db[0], cnt, ino);
	else
		ufs2_blksout(dp, &dp->dp2.di_db[0], cnt, ino, last);
	if ((size = DIP(dp, di_size) - UFS_NDADDR * sblock->fs_bsize) <= 0)
		return;
	for (ind_level = 0; ind_level < UFS_NIADDR; ind_level++) {
		dmpindir(dp, ino, DIP(dp, di_ib[ind_level]), ind_level, &size);
		if (size <= 0)
			return;
	}
}

/*
 * Read indirect blocks, and pass the data blocks to be dumped.
 */
static void
dmpindir(union dinode *dp, ino_t ino, ufs2_daddr_t blk, int ind_level,
	off_t *size)
{
	union {
		ufs1_daddr_t ufs1[MAXBSIZE / sizeof(ufs1_daddr_t)];
		ufs2_daddr_t ufs2[MAXBSIZE / sizeof(ufs2_daddr_t)];
	} idblk;
	int i, cnt, last;

	if (blk != 0)
		blkread(fsbtodb(sblock, blk), (char *)&idblk,
		    (int)sblock->fs_bsize);
	else
		memset(&idblk, 0, sblock->fs_bsize);
	if (ind_level <= 0) {
		if (*size > NINDIR(sblock) * sblock->fs_bsize) {
			cnt = NINDIR(sblock) * sblock->fs_frag;
			last = 0;
		} else {
			cnt = howmany(*size, sblock->fs_fsize);
			last = 1;
		}
		*size -= NINDIR(sblock) * sblock->fs_bsize;
		if (sblock->fs_magic == FS_UFS1_MAGIC)
			ufs1_blksout(idblk.ufs1, cnt, ino);
		else
			ufs2_blksout(dp, idblk.ufs2, cnt, ino, last);
		return;
	}
	ind_level--;
	for (i = 0; i < NINDIR(sblock); i++) {
		if (sblock->fs_magic == FS_UFS1_MAGIC)
			dmpindir(dp, ino, idblk.ufs1[i], ind_level, size);
		else
			dmpindir(dp, ino, idblk.ufs2[i], ind_level, size);
		if (*size <= 0)
			return;
	}
}

/*
 * Collect up the data into tape record sized buffers and output them.
 */
static void
ufs1_blksout(ufs1_daddr_t *blkp, int frags, ino_t ino)
{
	ufs1_daddr_t *bp;
	int i, j, count, blks, tbperdb;

	blks = howmany(frags * sblock->fs_fsize, TP_BSIZE);
	tbperdb = sblock->fs_bsize >> tp_bshift;
	for (i = 0; i < blks; i += TP_NINDIR) {
		if (i + TP_NINDIR > blks)
			count = blks;
		else
			count = i + TP_NINDIR;
		assert(count <= TP_NINDIR + i);
		for (j = i; j < count; j++)
			if (blkp[j / tbperdb] != 0)
				spcl.c_addr[j - i] = 1;
			else
				spcl.c_addr[j - i] = 0;
		spcl.c_count = count - i;
		writeheader(ino);
		bp = &blkp[i / tbperdb];
		for (j = i; j < count; j += tbperdb, bp++)
			if (*bp != 0) {
				if (j + tbperdb <= count)
					dumpblock(*bp, (int)sblock->fs_bsize);
				else
					dumpblock(*bp, (count - j) * TP_BSIZE);
			}
		spcl.c_type = TS_ADDR;
	}
}

/*
 * Collect up the data into tape record sized buffers and output them.
 */
static void
ufs2_blksout(union dinode *dp, ufs2_daddr_t *blkp, int frags, ino_t ino,
	int last)
{
	ufs2_daddr_t *bp;
	int i, j, count, resid, blks, tbperdb, added;
	static int writingextdata = 0;

	/*
	 * Calculate the number of TP_BSIZE blocks to be dumped.
	 * For filesystems with a fragment size bigger than TP_BSIZE,
	 * only part of the final fragment may need to be dumped.
	 */
	blks = howmany(frags * sblock->fs_fsize, TP_BSIZE);
	if (last) {
		if (writingextdata)
			resid = howmany(fragoff(sblock, spcl.c_extsize),
			    TP_BSIZE);
		else
			resid = howmany(fragoff(sblock, dp->dp2.di_size),
			    TP_BSIZE);
		if (resid > 0)
			blks -= howmany(sblock->fs_fsize, TP_BSIZE) - resid;
	}
	tbperdb = sblock->fs_bsize >> tp_bshift;
	for (i = 0; i < blks; i += TP_NINDIR) {
		if (i + TP_NINDIR > blks)
			count = blks;
		else
			count = i + TP_NINDIR;
		assert(count <= TP_NINDIR + i);
		for (j = i; j < count; j++)
			if (blkp[j / tbperdb] != 0)
				spcl.c_addr[j - i] = 1;
			else
				spcl.c_addr[j - i] = 0;
		spcl.c_count = count - i;
		if (last && count == blks && !writingextdata)
			added = appendextdata(dp);
		writeheader(ino);
		bp = &blkp[i / tbperdb];
		for (j = i; j < count; j += tbperdb, bp++)
			if (*bp != 0) {
				if (j + tbperdb <= count)
					dumpblock(*bp, (int)sblock->fs_bsize);
				else
					dumpblock(*bp, (count - j) * TP_BSIZE);
			}
		spcl.c_type = TS_ADDR;
		spcl.c_count = 0;
		if (last && count == blks && !writingextdata) {
			writingextdata = 1;
			writeextdata(dp, ino, added);
			writingextdata = 0;
		}
	}
}

/*
 * If there is room in the current block for the extended attributes
 * as well as the file data, update the header to reflect the added
 * attribute data at the end. Attributes are placed at the end so that
 * old versions of restore will correctly restore the file and simply
 * discard the extra data at the end that it does not understand.
 * The attribute data is dumped following the file data by the
 * writeextdata() function (below).
 */
static int
appendextdata(union dinode *dp)
{
	int i, blks, tbperdb;

	/*
	 * If no extended attributes, there is nothing to do.
	 */
	if (spcl.c_extsize == 0)
		return (0);
	/*
	 * If there is not enough room at the end of this block
	 * to add the extended attributes, then rather than putting
	 * part of them here, we simply push them entirely into a
	 * new block rather than putting some here and some later.
	 */
	if (spcl.c_extsize > UFS_NXADDR * sblock->fs_bsize)
		blks = howmany(UFS_NXADDR * sblock->fs_bsize, TP_BSIZE);
	else
		blks = howmany(spcl.c_extsize, TP_BSIZE);
	if (spcl.c_count + blks > TP_NINDIR)
		return (0);
	/*
	 * Update the block map in the header to indicate the added
	 * extended attribute. They will be appended after the file
	 * data by the writeextdata() routine.
	 */
	tbperdb = sblock->fs_bsize >> tp_bshift;
	assert(spcl.c_count + blks < TP_NINDIR);
	for (i = 0; i < blks; i++)
		if (&dp->dp2.di_extb[i / tbperdb] != 0)
				spcl.c_addr[spcl.c_count + i] = 1;
			else
				spcl.c_addr[spcl.c_count + i] = 0;
	spcl.c_count += blks;
	return (blks);
}

/*
 * Dump the extended attribute data. If there was room in the file
 * header, then all we need to do is output the data blocks. If there
 * was not room in the file header, then an additional TS_ADDR header
 * is created to hold the attribute data.
 */
static void
writeextdata(union dinode *dp, ino_t ino, int added)
{
	int i, frags, blks, tbperdb, last;
	ufs2_daddr_t *bp;
	off_t size;

	/*
	 * If no extended attributes, there is nothing to do.
	 */
	if (spcl.c_extsize == 0)
		return;
	/*
	 * If there was no room in the file block for the attributes,
	 * dump them out in a new block, otherwise just dump the data.
	 */
	if (added == 0) {
		if (spcl.c_extsize > UFS_NXADDR * sblock->fs_bsize) {
			frags = UFS_NXADDR * sblock->fs_frag;
			last = 0;
		} else {
			frags = howmany(spcl.c_extsize, sblock->fs_fsize);
			last = 1;
		}
		ufs2_blksout(dp, &dp->dp2.di_extb[0], frags, ino, last);
	} else {
		if (spcl.c_extsize > UFS_NXADDR * sblock->fs_bsize)
			blks = howmany(UFS_NXADDR * sblock->fs_bsize, TP_BSIZE);
		else
			blks = howmany(spcl.c_extsize, TP_BSIZE);
		tbperdb = sblock->fs_bsize >> tp_bshift;
		for (i = 0; i < blks; i += tbperdb) {
			bp = &dp->dp2.di_extb[i / tbperdb];
			if (*bp != 0) {
				if (i + tbperdb <= blks)
					dumpblock(*bp, (int)sblock->fs_bsize);
				else
					dumpblock(*bp, (blks - i) * TP_BSIZE);
			}
		}

	}
	/*
	 * If an indirect block is added for extended attributes, then
	 * di_exti below should be changed to the structure element
	 * that references the extended attribute indirect block. This
	 * definition is here only to make it compile without complaint.
	 */
#define di_exti di_spare[0]
	/*
	 * If the extended attributes fall into an indirect block,
	 * dump it as well.
	 */
	if ((size = spcl.c_extsize - UFS_NXADDR * sblock->fs_bsize) > 0)
		dmpindir(dp, ino, dp->dp2.di_exti, 0, &size);
}

/*
 * Dump a map to the tape.
 */
void
dumpmap(char *map, int type, ino_t ino)
{
	int i;
	char *cp;

	spcl.c_type = type;
	spcl.c_count = howmany(mapsize * sizeof(char), TP_BSIZE);
	writeheader(ino);
	for (i = 0, cp = map; i < spcl.c_count; i++, cp += TP_BSIZE)
		writerec(cp, 0);
}

/*
 * Write a header record to the dump tape.
 */
void
writeheader(ino_t ino)
{
	int32_t sum, cnt, *lp;

	if (rsync_friendly >= 2) {
		/* don't track changes to access time */
		spcl.c_atime = spcl.c_mtime;
		spcl.c_atimensec = spcl.c_mtimensec;
	}
	spcl.c_inumber = ino;
	spcl.c_magic = FS_UFS2_MAGIC;
	spcl.c_checksum = 0;
	lp = (int32_t *)&spcl;
	sum = 0;
	cnt = sizeof(union u_spcl) / (4 * sizeof(int32_t));
	while (--cnt >= 0) {
		sum += *lp++;
		sum += *lp++;
		sum += *lp++;
		sum += *lp++;
	}
	spcl.c_checksum = CHECKSUM - sum;
	writerec((char *)&spcl, 1);
}

union dinode *
getino(ino_t inum, int *modep)
{
	static ino_t minino, maxino;
	static caddr_t inoblock;
	struct ufs1_dinode *dp1;
	struct ufs2_dinode *dp2;

	if (inoblock == NULL && (inoblock = malloc(sblock->fs_bsize)) == NULL)
		quit("cannot allocate inode memory.\n");
	curino = inum;
	if (inum >= minino && inum < maxino)
		goto gotit;
	blkread(fsbtodb(sblock, ino_to_fsba(sblock, inum)), inoblock,
	    (int)sblock->fs_bsize);
	minino = inum - (inum % INOPB(sblock));
	maxino = minino + INOPB(sblock);
gotit:
	if (sblock->fs_magic == FS_UFS1_MAGIC) {
		dp1 = &((struct ufs1_dinode *)inoblock)[inum - minino];
		*modep = (dp1->di_mode & IFMT);
		return ((union dinode *)dp1);
	}
	dp2 = &((struct ufs2_dinode *)inoblock)[inum - minino];
	*modep = (dp2->di_mode & IFMT);
	return ((union dinode *)dp2);
}

/*
 * Read a chunk of data from the disk.
 * Try to recover from hard errors by reading in sector sized pieces.
 * Error recovery is attempted at most BREADEMAX times before seeking
 * consent from the operator to continue.
 */
int	breaderrors = 0;
#define	BREADEMAX 32

void
blkread(ufs2_daddr_t blkno, char *buf, int size)
{
	int secsize, bytes, resid, xfer, base, cnt, i;
	static char *tmpbuf;
	off_t offset;

loop:
	offset = blkno << dev_bshift;
	secsize = sblock->fs_fsize;
	base = offset % secsize;
	resid = size % secsize;
	/*
	 * If the transfer request starts or ends on a non-sector
	 * boundary, we must read the entire sector and copy out
	 * just the part that we need.
	 */
	if (base == 0 && resid == 0) {
		cnt = cread(diskfd, buf, size, offset);
		if (cnt == size)
			return;
	} else {
		if (tmpbuf == NULL && (tmpbuf = malloc(secsize)) == NULL)
			quit("buffer malloc failed\n");
		xfer = 0;
		bytes = size;
		if (base != 0) {
			cnt = cread(diskfd, tmpbuf, secsize, offset - base);
			if (cnt != secsize)
				goto bad;
			xfer = MIN(secsize - base, size);
			offset += xfer;
			bytes -= xfer;
			resid = bytes % secsize;
			memcpy(buf, &tmpbuf[base], xfer);
		}
		if (bytes >= secsize) {
			cnt = cread(diskfd, &buf[xfer], bytes - resid, offset);
			if (cnt != bytes - resid)
				goto bad;
			xfer += cnt;
			offset += cnt;
		}
		if (resid == 0)
			return;
		cnt = cread(diskfd, tmpbuf, secsize, offset);
		if (cnt == secsize) {
			memcpy(&buf[xfer], tmpbuf, resid);
			return;
		}
	}
bad:
	if (blkno + (size / dev_bsize) > fsbtodb(sblock, sblock->fs_size)) {
		/*
		 * Trying to read the final fragment.
		 *
		 * NB - dump only works in TP_BSIZE blocks, hence
		 * rounds `dev_bsize' fragments up to TP_BSIZE pieces.
		 * It should be smarter about not actually trying to
		 * read more than it can get, but for the time being
		 * we punt and scale back the read only when it gets
		 * us into trouble. (mkm 9/25/83)
		 */
		size -= dev_bsize;
		goto loop;
	}
	if (cnt == -1)
		msg("read error from %s: %s: [block %jd]: count=%d\n",
			disk, strerror(errno), (intmax_t)blkno, size);
	else
		msg("short read error from %s: [block %jd]: count=%d, got=%d\n",
			disk, (intmax_t)blkno, size, cnt);
	if (++breaderrors > BREADEMAX) {
		msg("More than %d block read errors from %s\n",
			BREADEMAX, disk);
		broadcast("DUMP IS AILING!\n");
		msg("This is an unrecoverable error.\n");
		if (!query("Do you want to attempt to continue?")){
			dumpabort(0);
			/*NOTREACHED*/
		} else
			breaderrors = 0;
	}
	/*
	 * Zero buffer, then try to read each sector of buffer separately,
	 * and bypass the cache.
	 */
	memset(buf, 0, size);
	for (i = 0; i < size; i += dev_bsize, buf += dev_bsize, blkno++) {
		if ((cnt = pread(diskfd, buf, (int)dev_bsize,
		    ((off_t)blkno << dev_bshift))) == dev_bsize)
			continue;
		if (cnt == -1) {
			msg("read error from %s: %s: [sector %jd]: count=%ld\n",
			    disk, strerror(errno), (intmax_t)blkno, dev_bsize);
			continue;
		}
		msg("short read from %s: [sector %jd]: count=%ld, got=%d\n",
		    disk, (intmax_t)blkno, dev_bsize, cnt);
	}
}
