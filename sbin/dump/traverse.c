/*	$OpenBSD: traverse.c,v 1.43 2024/09/15 07:14:58 jsg Exp $	*/
/*	$NetBSD: traverse.c,v 1.17 1997/06/05 11:13:27 lukem Exp $	*/

/*-
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

#include <sys/param.h>	/* MAXBSIZE DEV_BSIZE dbtob */
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/disklabel.h>
#include <ufs/ffs/fs.h>
#include <ufs/ufs/dir.h>
#include <ufs/ufs/dinode.h>

#include <protocols/dumprestore.h>

#include <ctype.h>
#include <errno.h>
#include <fts.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include "dump.h"

extern struct disklabel lab;

union dinode {
	struct ufs1_dinode dp1;
	struct ufs2_dinode dp2;
};
#define	DIP(dp, field) \
	((sblock->fs_magic == FS_UFS1_MAGIC) ? \
	(dp)->dp1.field : (dp)->dp2.field)

#define	HASDUMPEDFILE	0x1
#define	HASSUBDIRS	0x2

static	int dirindir(ino_t, daddr_t, int, off_t *, int64_t *, int);
static	void dmpindir(ino_t, daddr_t, int, off_t *);
static	int searchdir(ino_t, daddr_t, long, off_t, int64_t *, int);
void	fs_mapinodes(ino_t maxino, off_t *tapesize, int *anydirskipped);

/*
 * This is an estimation of the number of TP_BSIZE blocks in the file.
 * It estimates the number of blocks in files with holes by assuming
 * that all of the blocks accounted for by di_blocks are data blocks
 * (when some of the blocks are usually used for indirect pointers);
 * hence the estimate may be high.
 */
int64_t
blockest(union dinode *dp)
{
	int64_t blkest, sizeest;

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
	blkest = howmany(dbtob((int64_t)DIP(dp, di_blocks)), TP_BSIZE);
	sizeest = howmany((int64_t)DIP(dp, di_size), TP_BSIZE);
	if (blkest > sizeest)
		blkest = sizeest;
	if (DIP(dp, di_size) > sblock->fs_bsize * NDADDR) {
		/* calculate the number of indirect blocks on the dump tape */
		blkest +=
			howmany(sizeest - NDADDR * sblock->fs_bsize / TP_BSIZE,
			TP_NINDIR);
	}
	return (blkest + 1);
}

/* true if "nodump" flag has no effect here, i.e. dumping allowed */
#define CHECKNODUMP(dp) \
	(nonodump || (DIP((dp), di_flags) & UF_NODUMP) != UF_NODUMP)

/*
 * Determine if given inode should be dumped
 */
void
mapfileino(ino_t ino, int64_t *tapesize, int *dirskipped)
{
	int mode;
	union dinode *dp;

	dp = getino(ino, &mode);
	if (mode == 0)
		return;
	SETINO(ino, usedinomap);
	if (mode == IFDIR)
		SETINO(ino, dumpdirmap);
	if (CHECKNODUMP(dp) &&
	    (DIP(dp, di_mtime) >= spcl.c_ddate ||
	     DIP(dp, di_ctime) >= spcl.c_ddate)) {
		SETINO(ino, dumpinomap);
		if (mode != IFREG && mode != IFDIR && mode != IFLNK)
			*tapesize += 1;
		else
			*tapesize += blockest(dp);
		return;
	}
	if (mode == IFDIR) {
		if (!CHECKNODUMP(dp))
			CLRINO(ino, usedinomap);
		*dirskipped = 1;
	}
}

void
fs_mapinodes(ino_t maxino, int64_t *tapesize, int *anydirskipped)
{
	int i, cg, inosused;
	struct cg *cgp;
	ino_t ino;

	if ((cgp = malloc(sblock->fs_cgsize)) == NULL)
		quit("fs_mapinodes: cannot allocate memory.\n");

	for (cg = 0; cg < sblock->fs_ncg; cg++) {
		ino = cg * (ino_t)sblock->fs_ipg;
		bread(fsbtodb(sblock, cgtod(sblock, cg)), (char *)cgp,
		    sblock->fs_cgsize);
		if (sblock->fs_magic == FS_UFS2_MAGIC)
			inosused = cgp->cg_initediblk;
		else
			inosused = sblock->fs_ipg;
		for (i = 0; i < inosused; i++, ino++) {
			if (ino < ROOTINO)
				continue;
			mapfileino(ino, tapesize, anydirskipped);
		}
	}

	free(cgp);
}

/*
 * Dump pass 1.
 *
 * Walk the inode list for a filesystem to find all allocated inodes
 * that have been modified since the previous dump time. Also, find all
 * the directories in the filesystem.
 */
int
mapfiles(ino_t maxino, int64_t *tapesize, char *disk, char * const *dirv)
{
	int anydirskipped = 0;

	if (dirv != NULL) {
		char	 curdir[PATH_MAX];
		FTS	*dirh;
		FTSENT	*entry;
		int	 d;

		if (getcwd(curdir, sizeof(curdir)) == NULL) {
			msg("Can't determine cwd: %s\n", strerror(errno));
			dumpabort(0);
		}
		if ((dirh = fts_open(dirv, FTS_PHYSICAL|FTS_SEEDOT|FTS_XDEV,
		    NULL)) == NULL) {
			msg("fts_open failed: %s\n", strerror(errno));
			dumpabort(0);
		}
		while ((entry = fts_read(dirh)) != NULL) {
			switch (entry->fts_info) {
			case FTS_DNR:		/* an error */
			case FTS_ERR:
			case FTS_NS:
				msg("Can't fts_read %s: %s\n", entry->fts_path,
				    strerror(errno));
				/* FALLTHROUGH */
			case FTS_DP:		/* already seen dir */
				continue;
			}
			mapfileino(entry->fts_statp->st_ino, tapesize,
			    &anydirskipped);
		}
		if (errno) {
			msg("fts_read failed: %s\n", strerror(errno));
			dumpabort(0);
		}
		(void)fts_close(dirh);

		/*
		 * Add any parent directories
		 */
		for (d = 0 ; dirv[d] != NULL ; d++) {
			char path[PATH_MAX];

			if (dirv[d][0] != '/')
				(void)snprintf(path, sizeof(path), "%s/%s",
				    curdir, dirv[d]);
			else
				(void)snprintf(path, sizeof(path), "%s",
				    dirv[d]);
			while (strcmp(path, disk) != 0) {
				char *p;
				struct stat sb;

				if (*path == '\0')
					break;
				if ((p = strrchr(path, '/')) == NULL)
					break;
				if (p == path)
					break;
				*p = '\0';
				if (stat(path, &sb) == -1) {
					msg("Can't stat %s: %s\n", path,
					    strerror(errno));
					break;
				}
				mapfileino(sb.st_ino, tapesize, &anydirskipped);
			}
		}

		/*
		 * Ensure that the root inode actually appears in the
		 * file list for a subdir
		 */
		mapfileino(ROOTINO, tapesize, &anydirskipped);
	} else {
		fs_mapinodes(maxino, tapesize, &anydirskipped);
	}
	/*
	 * Restore gets very upset if the root is not dumped,
	 * so ensure that it always is dumped.
	 */
	SETINO(ROOTINO, dumpinomap);
	return (anydirskipped);
}

/*
 * Dump pass 2.
 *
 * Scan each directory on the filesystem to see if it has any modified
 * files in it. If it does, and has not already been added to the dump
 * list (because it was itself modified), then add it. If a directory
 * has not been modified itself, contains no modified files and has no
 * subdirectories, then it can be deleted from the dump list and from
 * the list of directories. By deleting it from the list of directories,
 * its parent may now qualify for the same treatment on this or a later
 * pass using this algorithm.
 */
int
mapdirs(ino_t maxino, int64_t *tapesize)
{
	union dinode *dp;
	int i, isdir, nodump;
	char *map;
	ino_t ino;
	union dinode di;
	off_t filesize;
	int ret, change = 0;

	isdir = 0;		/* XXX just to get gcc to shut up */
	for (map = dumpdirmap, ino = 1; ino < maxino; ino++) {
		if (((ino - 1) % NBBY) == 0)	/* map is offset by 1 */
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
		nodump = !nonodump && !TSTINO(ino, usedinomap);
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
		filesize = (off_t)DIP(dp, di_size);
		for (ret = 0, i = 0; filesize > 0 && i < NDADDR; i++) {
			if (DIP(&di, di_db[i]) != 0)
				ret |= searchdir(ino, DIP(&di, di_db[i]),
				    sblksize(sblock, DIP(dp, di_size), i),
				    filesize, tapesize, nodump);
			if (ret & HASDUMPEDFILE)
				filesize = 0;
			else
				filesize -= sblock->fs_bsize;
		}
		for (i = 0; filesize > 0 && i < NIADDR; i++) {
			if (DIP(&di, di_ib[i]) == 0)
				continue;
			ret |= dirindir(ino, DIP(&di, di_ib[i]), i, &filesize,
			    tapesize, nodump);
		}
		if (ret & HASDUMPEDFILE) {
			SETINO(ino, dumpinomap);
			*tapesize += blockest(dp);
			change = 1;
			continue;
		}
                if (nodump) {
                        if (ret & HASSUBDIRS)
                                change = 1;     /* subdirs inherit nodump */
                        CLRINO(ino, dumpdirmap);
                } else if ((ret & HASSUBDIRS) == 0) {
			if (!TSTINO(ino, dumpinomap)) {
				CLRINO(ino, dumpdirmap);
				change = 1;
			}
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
dirindir(ino_t ino, daddr_t blkno, int ind_level, off_t *filesize,
    int64_t *tapesize, int nodump)
{
	int ret = 0;
	int i;
	char idblk[MAXBSIZE];

	bread(fsbtodb(sblock, blkno), idblk, (int)sblock->fs_bsize);
	if (ind_level <= 0) {
		for (i = 0; *filesize > 0 && i < NINDIR(sblock); i++) {
			if (sblock->fs_magic == FS_UFS1_MAGIC)
				blkno = ((int32_t *)idblk)[i];
			else
				blkno = ((int64_t *)idblk)[i];
			if (blkno != 0)
				ret |= searchdir(ino, blkno, sblock->fs_bsize,
					*filesize, tapesize, nodump);
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
			blkno = ((int32_t *)idblk)[i];
		else
			blkno = ((int64_t *)idblk)[i];
		if (blkno != 0)
			ret |= dirindir(ino, blkno, ind_level, filesize,
			    tapesize, nodump);
	}
	return (ret);
}

/*
 * Scan a disk block containing directory information looking to see if
 * any of the entries are on the dump list and to see if the directory
 * contains any subdirectories.
 */
static int
searchdir(ino_t ino, daddr_t blkno, long size, off_t filesize,
    int64_t *tapesize, int nodump)
{
	struct direct *dp;
	union dinode *ip;
	long loc;
	static caddr_t dblk;
	int mode, ret = 0;

	if (dblk == NULL && (dblk = malloc(sblock->fs_bsize)) == NULL)
		quit("searchdir: cannot allocate indirect memory.\n");
	bread(fsbtodb(sblock, blkno), dblk, (int)size);
	if (filesize < size)
		size = filesize;
	for (loc = 0; loc < size; ) {
		dp = (struct direct *)(dblk + loc);
		if (dp->d_reclen == 0) {
			msg("corrupted directory, inumber %llu\n",
			    (unsigned long long)ino);
			break;
		}
		loc += dp->d_reclen;
		if (dp->d_ino == 0)
			continue;
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
	int ind_level, cnt;
	off_t size;
	char buf[TP_BSIZE];

	if (newtape) {
		newtape = 0;
		dumpmap(dumpinomap, TS_BITS, ino);
	}
	CLRINO(ino, dumpinomap);
	if (sblock->fs_magic == FS_UFS1_MAGIC) {
		spcl.c_mode = dp->dp1.di_mode;
		spcl.c_size = dp->dp1.di_size;
		spcl.c_old_atime = (time_t)dp->dp1.di_atime;
		spcl.c_atime = dp->dp1.di_atime;
		spcl.c_atimensec = dp->dp1.di_atimensec;
		spcl.c_old_mtime = (time_t)dp->dp1.di_mtime;
		spcl.c_mtime = dp->dp1.di_mtime;
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
		spcl.c_atime = dp->dp2.di_atime;
		spcl.c_atimensec = dp->dp2.di_atimensec;
		spcl.c_mtime = dp->dp2.di_mtime;
		spcl.c_mtimensec = dp->dp2.di_mtimensec;
		spcl.c_birthtime = dp->dp2.di_birthtime;
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

	case IFLNK:
		/*
		 * Check for short symbolic link.
		 */
		if (DIP(dp, di_size) > 0 &&
		    DIP(dp, di_size) < sblock->fs_maxsymlinklen) {
			void *shortlink;

			spcl.c_addr[0] = 1;
			spcl.c_count = 1;
			writeheader(ino);
			if (sblock->fs_magic == FS_UFS1_MAGIC)
				shortlink = dp->dp1.di_shortlink;
			else
				shortlink = dp->dp2.di_shortlink;
			memcpy(buf, shortlink, DIP(dp, di_size));
			buf[DIP(dp, di_size)] = '\0';
			writerec(buf, 0);
			return;
		}
		/* FALLTHROUGH */

	case IFDIR:
	case IFREG:
		if (DIP(dp, di_size) > 0)
			break;
		/* FALLTHROUGH */

	case IFIFO:
	case IFSOCK:
	case IFCHR:
	case IFBLK:
		writeheader(ino);
		return;

	default:
		msg("Warning: undefined file type 0%o\n",
		    DIP(dp, di_mode) & IFMT);
		return;
	}
	if (DIP(dp, di_size) > NDADDR * sblock->fs_bsize)
		cnt = NDADDR * sblock->fs_frag;
	else
		cnt = howmany(DIP(dp, di_size), sblock->fs_fsize);
	if (sblock->fs_magic == FS_UFS1_MAGIC)
		ufs1_blksout(&dp->dp1.di_db[0], cnt, ino);
	else
		ufs2_blksout(&dp->dp2.di_db[0], cnt, ino);
	if ((size = DIP(dp, di_size) - NDADDR * sblock->fs_bsize) <= 0)
		return;
	for (ind_level = 0; ind_level < NIADDR; ind_level++) {
		dmpindir(ino, DIP(dp, di_ib[ind_level]), ind_level, &size);
		if (size <= 0)
			return;
	}
}

/*
 * Read indirect blocks, and pass the data blocks to be dumped.
 */
static void
dmpindir(ino_t ino, daddr_t  blk, int ind_level, off_t *size)
{
	int i, cnt;
	char idblk[MAXBSIZE];

	if (blk != 0)
		bread(fsbtodb(sblock, blk), idblk, (int) sblock->fs_bsize);
	else
		memset(idblk, 0, (int)sblock->fs_bsize);
	if (ind_level <= 0) {
		if (*size < NINDIR(sblock) * sblock->fs_bsize)
			cnt = howmany(*size, sblock->fs_fsize);
		else
			cnt = NINDIR(sblock) * sblock->fs_frag;
		*size -= NINDIR(sblock) * sblock->fs_bsize;
		if (sblock->fs_magic == FS_UFS1_MAGIC)
			ufs1_blksout((int32_t *)idblk, cnt, ino);
		else
			ufs2_blksout((int64_t *)idblk, cnt, ino);
		return;
	}
	ind_level--;
	for (i = 0; i < NINDIR(sblock); i++) {
		if (sblock->fs_magic == FS_UFS1_MAGIC)
			dmpindir(ino, ((int32_t *)idblk)[i], ind_level,
			    size);
		else
			dmpindir(ino, ((int64_t *)idblk)[i], ind_level,
			    size);
		if (*size <= 0)
			return;
	}
}

/*
 * Collect up the data into tape record sized buffers and output them.
 */
void
ufs1_blksout(int32_t *blkp, int frags, ino_t ino)
{
	int32_t *bp;
	int i, j, count, blks, tbperdb;

	blks = howmany(frags * sblock->fs_fsize, TP_BSIZE);
	tbperdb = sblock->fs_bsize >> tp_bshift;
	for (i = 0; i < blks; i += TP_NINDIR) {
		if (i + TP_NINDIR > blks)
			count = blks;
		else
			count = i + TP_NINDIR;
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
void
ufs2_blksout(daddr_t *blkp, int frags, ino_t ino)
{
	daddr_t *bp;
	int i, j, count, blks, tbperdb;

	blks = howmany(frags * sblock->fs_fsize, TP_BSIZE);
	tbperdb = sblock->fs_bsize >> tp_bshift;
	for (i = 0; i < blks; i += TP_NINDIR) {
		if (i + TP_NINDIR > blks)
			count = blks;
		else
			count = i + TP_NINDIR;
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

	spcl.c_inumber = ino;
	if (sblock->fs_magic == FS_UFS2_MAGIC) {
		spcl.c_magic = FS_UFS2_MAGIC;
	} else {
		spcl.c_magic = NFS_MAGIC;
		spcl.c_old_date = (int32_t)spcl.c_date;
		spcl.c_old_ddate = (int32_t)spcl.c_ddate;
		spcl.c_old_tapea = (int32_t)spcl.c_tapea;
		spcl.c_old_firstrec = (int32_t)spcl.c_firstrec;
	}
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
	static void *inoblock;
	struct ufs1_dinode *dp1;
	struct ufs2_dinode *dp2;

	if (inoblock == NULL && (inoblock = malloc(sblock->fs_bsize)) == NULL)
		quit("cannot allocate inode memory.\n");
	curino = inum;
	if (inum >= minino && inum < maxino)
		goto gotit;
	bread(fsbtodb(sblock, ino_to_fsba(sblock, inum)), inoblock,
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
bread(daddr_t blkno, char *buf, int size)
{
	static char *mybuf = NULL;
	char *mybufp, *bufp, *np;
	static size_t mybufsz = 0;
	off_t offset;
	int cnt, i;
	u_int64_t secno, seccount;
	u_int32_t secoff, secsize = lab.d_secsize;

	/*
	 * We must read an integral number of sectors large enough to contain
	 * all the requested data. The read must begin at a sector.
	 */
	if (DL_BLKOFFSET(&lab, blkno) == 0 && size % secsize == 0) {
		secno = DL_BLKTOSEC(&lab, blkno);
		secoff = 0;
		seccount = size / secsize;
		bufp = buf;
	} else {
		secno = DL_BLKTOSEC(&lab, blkno);
		secoff = DL_BLKOFFSET(&lab, blkno);
		seccount = DL_BLKTOSEC(&lab, (size + secoff) / DEV_BSIZE);
		if (seccount * secsize < (size + secoff))
			seccount++;
		if (mybufsz < seccount * secsize) {
			np = reallocarray(mybuf, seccount, secsize);
			if (np == NULL) {
				msg("No memory to read %llu %u-byte sectors",
				    seccount, secsize);
				dumpabort(0);
			}
			mybufsz = seccount * secsize;
			mybuf = np;
		}
		bufp = mybuf;
	}

	offset = secno * secsize;

loop:
	if ((cnt = pread(diskfd, bufp, seccount * secsize, offset)) ==
	    seccount * secsize)
		goto done;
	if (blkno + (size / DEV_BSIZE) >
	    fsbtodb(sblock, sblock->fs_ffs1_size)) {
		/*
		 * Trying to read the final fragment.
		 *
		 * NB - dump only works in TP_BSIZE blocks, hence
		 * rounds `DEV_BSIZE' fragments up to TP_BSIZE pieces.
		 * It should be smarter about not actually trying to
		 * read more than it can get, but for the time being
		 * we punt and scale back the read only when it gets
		 * us into trouble. (mkm 9/25/83)
		 */
		size -= secsize;
		seccount--;
		goto loop;
	}
	if (cnt == -1)
		msg("read error from %s: %s: [block %lld]: count=%d\n",
		    disk, strerror(errno), (long long)blkno, size);
	else
		msg("short read error from %s: [block %lld]: count=%d, "
		    "got=%d\n", disk, (long long)blkno, size, cnt);
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
	 * Zero buffer, then try to read each sector of buffer separately.
	 */
	if (bufp == mybuf)
		memset(bufp, 0, mybufsz);
	else
		memset(bufp, 0, size);
	for (i = 0, mybufp = bufp; i < size; i += secsize, mybufp += secsize) {
		if ((cnt = pread(diskfd, mybufp, secsize, offset + i)) ==
		    secsize)
			continue;
		if (cnt == -1) {
			msg("read error from %s: %s: [block %lld]: "
			    "count=%u\n", disk, strerror(errno),
			    (long long)(offset + i) / DEV_BSIZE, secsize);
			continue;
		}
		msg("short read error from %s: [block %lld]: count=%u, "
		    "got=%d\n", disk, (long long)(offset + i) / DEV_BSIZE,
		    secsize, cnt);
	}

done:
	/* If necessary, copy out data that was read. */
	if (bufp == mybuf)
		memcpy(buf, bufp + secoff, size);
}
