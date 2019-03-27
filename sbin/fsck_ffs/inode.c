/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1980, 1986, 1993
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

#if 0
#ifndef lint
static const char sccsid[] = "@(#)inode.c	8.8 (Berkeley) 4/28/95";
#endif /* not lint */
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/stdint.h>
#include <sys/sysctl.h>

#include <ufs/ufs/dinode.h>
#include <ufs/ufs/dir.h>
#include <ufs/ffs/fs.h>

#include <err.h>
#include <pwd.h>
#include <string.h>
#include <time.h>
#include <libufs.h>

#include "fsck.h"

static ino_t startinum;

static int iblock(struct inodesc *, long ilevel, off_t isize, int type);

int
ckinode(union dinode *dp, struct inodesc *idesc)
{
	off_t remsize, sizepb;
	int i, offset, ret;
	union dinode dino;
	ufs2_daddr_t ndb;
	mode_t mode;
	char pathbuf[MAXPATHLEN + 1];

	if (idesc->id_fix != IGNORE)
		idesc->id_fix = DONTKNOW;
	idesc->id_lbn = -1;
	idesc->id_entryno = 0;
	idesc->id_filesize = DIP(dp, di_size);
	mode = DIP(dp, di_mode) & IFMT;
	if (mode == IFBLK || mode == IFCHR || (mode == IFLNK &&
	    DIP(dp, di_size) < (unsigned)sblock.fs_maxsymlinklen))
		return (KEEPON);
	if (sblock.fs_magic == FS_UFS1_MAGIC)
		dino.dp1 = dp->dp1;
	else
		dino.dp2 = dp->dp2;
	ndb = howmany(DIP(&dino, di_size), sblock.fs_bsize);
	for (i = 0; i < UFS_NDADDR; i++) {
		idesc->id_lbn++;
		if (--ndb == 0 &&
		    (offset = blkoff(&sblock, DIP(&dino, di_size))) != 0)
			idesc->id_numfrags =
				numfrags(&sblock, fragroundup(&sblock, offset));
		else
			idesc->id_numfrags = sblock.fs_frag;
		if (DIP(&dino, di_db[i]) == 0) {
			if (idesc->id_type == DATA && ndb >= 0) {
				/* An empty block in a directory XXX */
				getpathname(pathbuf, idesc->id_number,
						idesc->id_number);
				pfatal("DIRECTORY %s: CONTAINS EMPTY BLOCKS",
					pathbuf);
				if (reply("ADJUST LENGTH") == 1) {
					dp = ginode(idesc->id_number);
					DIP_SET(dp, di_size,
					    i * sblock.fs_bsize);
					printf(
					    "YOU MUST RERUN FSCK AFTERWARDS\n");
					rerun = 1;
					inodirty(dp);

				}
			}
			continue;
		}
		idesc->id_blkno = DIP(&dino, di_db[i]);
		if (idesc->id_type != DATA)
			ret = (*idesc->id_func)(idesc);
		else
			ret = dirscan(idesc);
		if (ret & STOP)
			return (ret);
	}
	idesc->id_numfrags = sblock.fs_frag;
	remsize = DIP(&dino, di_size) - sblock.fs_bsize * UFS_NDADDR;
	sizepb = sblock.fs_bsize;
	for (i = 0; i < UFS_NIADDR; i++) {
		sizepb *= NINDIR(&sblock);
		if (DIP(&dino, di_ib[i])) {
			idesc->id_blkno = DIP(&dino, di_ib[i]);
			ret = iblock(idesc, i + 1, remsize, BT_LEVEL1 + i);
			if (ret & STOP)
				return (ret);
		} else if (remsize > 0) {
			idesc->id_lbn += sizepb / sblock.fs_bsize;
			if (idesc->id_type == DATA) {
				/* An empty block in a directory XXX */
				getpathname(pathbuf, idesc->id_number,
						idesc->id_number);
				pfatal("DIRECTORY %s: CONTAINS EMPTY BLOCKS",
					pathbuf);
				if (reply("ADJUST LENGTH") == 1) {
					dp = ginode(idesc->id_number);
					DIP_SET(dp, di_size,
					    DIP(dp, di_size) - remsize);
					remsize = 0;
					printf(
					    "YOU MUST RERUN FSCK AFTERWARDS\n");
					rerun = 1;
					inodirty(dp);
					break;
				}
			}
		}
		remsize -= sizepb;
	}
	return (KEEPON);
}

static int
iblock(struct inodesc *idesc, long ilevel, off_t isize, int type)
{
	struct bufarea *bp;
	int i, n, (*func)(struct inodesc *), nif;
	off_t sizepb;
	char buf[BUFSIZ];
	char pathbuf[MAXPATHLEN + 1];
	union dinode *dp;

	if (idesc->id_type != DATA) {
		func = idesc->id_func;
		if (((n = (*func)(idesc)) & KEEPON) == 0)
			return (n);
	} else
		func = dirscan;
	if (chkrange(idesc->id_blkno, idesc->id_numfrags))
		return (SKIP);
	bp = getdatablk(idesc->id_blkno, sblock.fs_bsize, type);
	ilevel--;
	for (sizepb = sblock.fs_bsize, i = 0; i < ilevel; i++)
		sizepb *= NINDIR(&sblock);
	if (howmany(isize, sizepb) > NINDIR(&sblock))
		nif = NINDIR(&sblock);
	else
		nif = howmany(isize, sizepb);
	if (idesc->id_func == pass1check && nif < NINDIR(&sblock)) {
		for (i = nif; i < NINDIR(&sblock); i++) {
			if (IBLK(bp, i) == 0)
				continue;
			(void)sprintf(buf, "PARTIALLY TRUNCATED INODE I=%lu",
			    (u_long)idesc->id_number);
			if (preen) {
				pfatal("%s", buf);
			} else if (dofix(idesc, buf)) {
				IBLK_SET(bp, i, 0);
				dirty(bp);
			}
		}
		flush(fswritefd, bp);
	}
	for (i = 0; i < nif; i++) {
		if (ilevel == 0)
			idesc->id_lbn++;
		if (IBLK(bp, i)) {
			idesc->id_blkno = IBLK(bp, i);
			if (ilevel == 0)
				n = (*func)(idesc);
			else
				n = iblock(idesc, ilevel, isize, type);
			if (n & STOP) {
				bp->b_flags &= ~B_INUSE;
				return (n);
			}
		} else {
			if (idesc->id_type == DATA && isize > 0) {
				/* An empty block in a directory XXX */
				getpathname(pathbuf, idesc->id_number,
						idesc->id_number);
				pfatal("DIRECTORY %s: CONTAINS EMPTY BLOCKS",
					pathbuf);
				if (reply("ADJUST LENGTH") == 1) {
					dp = ginode(idesc->id_number);
					DIP_SET(dp, di_size,
					    DIP(dp, di_size) - isize);
					isize = 0;
					printf(
					    "YOU MUST RERUN FSCK AFTERWARDS\n");
					rerun = 1;
					inodirty(dp);
					bp->b_flags &= ~B_INUSE;
					return(STOP);
				}
			}
		}
		isize -= sizepb;
	}
	bp->b_flags &= ~B_INUSE;
	return (KEEPON);
}

/*
 * Check that a block in a legal block number.
 * Return 0 if in range, 1 if out of range.
 */
int
chkrange(ufs2_daddr_t blk, int cnt)
{
	int c;

	if (cnt <= 0 || blk <= 0 || blk > maxfsblock ||
	    cnt - 1 > maxfsblock - blk)
		return (1);
	if (cnt > sblock.fs_frag ||
	    fragnum(&sblock, blk) + cnt > sblock.fs_frag) {
		if (debug)
			printf("bad size: blk %ld, offset %i, size %d\n",
			    (long)blk, (int)fragnum(&sblock, blk), cnt);
		return (1);
	}
	c = dtog(&sblock, blk);
	if (blk < cgdmin(&sblock, c)) {
		if ((blk + cnt) > cgsblock(&sblock, c)) {
			if (debug) {
				printf("blk %ld < cgdmin %ld;",
				    (long)blk, (long)cgdmin(&sblock, c));
				printf(" blk + cnt %ld > cgsbase %ld\n",
				    (long)(blk + cnt),
				    (long)cgsblock(&sblock, c));
			}
			return (1);
		}
	} else {
		if ((blk + cnt) > cgbase(&sblock, c+1)) {
			if (debug)  {
				printf("blk %ld >= cgdmin %ld;",
				    (long)blk, (long)cgdmin(&sblock, c));
				printf(" blk + cnt %ld > sblock.fs_fpg %ld\n",
				    (long)(blk + cnt), (long)sblock.fs_fpg);
			}
			return (1);
		}
	}
	return (0);
}

/*
 * General purpose interface for reading inodes.
 */
union dinode *
ginode(ino_t inumber)
{
	ufs2_daddr_t iblk;
	union dinode *dp;

	if (inumber < UFS_ROOTINO || inumber > maxino)
		errx(EEXIT, "bad inode number %ju to ginode",
		    (uintmax_t)inumber);
	if (startinum == 0 ||
	    inumber < startinum || inumber >= startinum + INOPB(&sblock)) {
		iblk = ino_to_fsba(&sblock, inumber);
		if (pbp != NULL)
			pbp->b_flags &= ~B_INUSE;
		pbp = getdatablk(iblk, sblock.fs_bsize, BT_INODES);
		startinum = rounddown(inumber, INOPB(&sblock));
	}
	if (sblock.fs_magic == FS_UFS1_MAGIC)
		return ((union dinode *)
		    &pbp->b_un.b_dinode1[inumber % INOPB(&sblock)]);
	dp = (union dinode *)&pbp->b_un.b_dinode2[inumber % INOPB(&sblock)];
	if (ffs_verify_dinode_ckhash(&sblock, (struct ufs2_dinode *)dp) != 0) {
		pwarn("INODE CHECK-HASH FAILED");
		prtinode(inumber, dp);
		if (preen || reply("FIX") != 0) {
			if (preen)
				printf(" (FIXED)\n");
			ffs_update_dinode_ckhash(&sblock,
			    (struct ufs2_dinode *)dp);
			inodirty(dp);
		}
	}
	return (dp);
}

/*
 * Special purpose version of ginode used to optimize first pass
 * over all the inodes in numerical order.
 */
static ino_t nextino, lastinum, lastvalidinum;
static long readcount, readpercg, fullcnt, inobufsize, partialcnt, partialsize;
static struct bufarea inobuf;

union dinode *
getnextinode(ino_t inumber, int rebuildcg)
{
	int j;
	long size;
	mode_t mode;
	ufs2_daddr_t ndb, blk;
	union dinode *dp;
	static caddr_t nextinop;

	if (inumber != nextino++ || inumber > lastvalidinum)
		errx(EEXIT, "bad inode number %ju to nextinode",
		    (uintmax_t)inumber);
	if (inumber >= lastinum) {
		readcount++;
		blk = ino_to_fsba(&sblock, lastinum);
		if (readcount % readpercg == 0) {
			size = partialsize;
			lastinum += partialcnt;
		} else {
			size = inobufsize;
			lastinum += fullcnt;
		}
		/*
		 * Flush old contents in case they have been updated.
		 * If getblk encounters an error, it will already have zeroed
		 * out the buffer, so we do not need to do so here.
		 */
		flush(fswritefd, &inobuf);
		getblk(&inobuf, blk, size);
		nextinop = inobuf.b_un.b_buf;
	}
	dp = (union dinode *)nextinop;
	if (sblock.fs_magic == FS_UFS1_MAGIC)
		nextinop += sizeof(struct ufs1_dinode);
	else
		nextinop += sizeof(struct ufs2_dinode);
	if ((ckhashadd & CK_INODE) != 0) {
		ffs_update_dinode_ckhash(&sblock, (struct ufs2_dinode *)dp);
		dirty(&inobuf);
	}
	if (ffs_verify_dinode_ckhash(&sblock, (struct ufs2_dinode *)dp) != 0) {
		pwarn("INODE CHECK-HASH FAILED");
		prtinode(inumber, dp);
		if (preen || reply("FIX") != 0) {
			if (preen)
				printf(" (FIXED)\n");
			ffs_update_dinode_ckhash(&sblock,
			    (struct ufs2_dinode *)dp);
			dirty(&inobuf);
		}
	}
	if (rebuildcg && (char *)dp == inobuf.b_un.b_buf) {
		/*
		 * Try to determine if we have reached the end of the
		 * allocated inodes.
		 */
		mode = DIP(dp, di_mode) & IFMT;
		if (mode == 0) {
			if (memcmp(dp->dp2.di_db, ufs2_zino.di_db,
				UFS_NDADDR * sizeof(ufs2_daddr_t)) ||
			      memcmp(dp->dp2.di_ib, ufs2_zino.di_ib,
				UFS_NIADDR * sizeof(ufs2_daddr_t)) ||
			      dp->dp2.di_mode || dp->dp2.di_size)
				return (NULL);
			return (dp);
		}
		if (!ftypeok(dp))
			return (NULL);
		ndb = howmany(DIP(dp, di_size), sblock.fs_bsize);
		if (ndb < 0)
			return (NULL);
		if (mode == IFBLK || mode == IFCHR)
			ndb++;
		if (mode == IFLNK) {
			/*
			 * Fake ndb value so direct/indirect block checks below
			 * will detect any garbage after symlink string.
			 */
			if (DIP(dp, di_size) < (off_t)sblock.fs_maxsymlinklen) {
				ndb = howmany(DIP(dp, di_size),
				    sizeof(ufs2_daddr_t));
				if (ndb > UFS_NDADDR) {
					j = ndb - UFS_NDADDR;
					for (ndb = 1; j > 1; j--)
						ndb *= NINDIR(&sblock);
					ndb += UFS_NDADDR;
				}
			}
		}
		for (j = ndb; ndb < UFS_NDADDR && j < UFS_NDADDR; j++)
			if (DIP(dp, di_db[j]) != 0)
				return (NULL);
		for (j = 0, ndb -= UFS_NDADDR; ndb > 0; j++)
			ndb /= NINDIR(&sblock);
		for (; j < UFS_NIADDR; j++)
			if (DIP(dp, di_ib[j]) != 0)
				return (NULL);
	}
	return (dp);
}

void
setinodebuf(ino_t inum)
{

	if (inum % sblock.fs_ipg != 0)
		errx(EEXIT, "bad inode number %ju to setinodebuf",
		    (uintmax_t)inum);
	lastvalidinum = inum + sblock.fs_ipg - 1;
	startinum = 0;
	nextino = inum;
	lastinum = inum;
	readcount = 0;
	if (inobuf.b_un.b_buf != NULL)
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
	initbarea(&inobuf, BT_INODES);
	if ((inobuf.b_un.b_buf = Malloc((unsigned)inobufsize)) == NULL)
		errx(EEXIT, "cannot allocate space for inode buffer");
}

void
freeinodebuf(void)
{

	/*
	 * Flush old contents in case they have been updated.
	 */
	flush(fswritefd, &inobuf);
	if (inobuf.b_un.b_buf != NULL)
		free((char *)inobuf.b_un.b_buf);
	inobuf.b_un.b_buf = NULL;
}

/*
 * Routines to maintain information about directory inodes.
 * This is built during the first pass and used during the
 * second and third passes.
 *
 * Enter inodes into the cache.
 */
void
cacheino(union dinode *dp, ino_t inumber)
{
	struct inoinfo *inp, **inpp;
	int i, blks;

	if (howmany(DIP(dp, di_size), sblock.fs_bsize) > UFS_NDADDR)
		blks = UFS_NDADDR + UFS_NIADDR;
	else if (DIP(dp, di_size) > 0)
		blks = howmany(DIP(dp, di_size), sblock.fs_bsize);
	else
		blks = 1;
	inp = (struct inoinfo *)
		Malloc(sizeof(*inp) + (blks - 1) * sizeof(ufs2_daddr_t));
	if (inp == NULL)
		errx(EEXIT, "cannot increase directory list");
	inpp = &inphead[inumber % dirhash];
	inp->i_nexthash = *inpp;
	*inpp = inp;
	inp->i_parent = inumber == UFS_ROOTINO ? UFS_ROOTINO : (ino_t)0;
	inp->i_dotdot = (ino_t)0;
	inp->i_number = inumber;
	inp->i_isize = DIP(dp, di_size);
	inp->i_numblks = blks;
	for (i = 0; i < MIN(blks, UFS_NDADDR); i++)
		inp->i_blks[i] = DIP(dp, di_db[i]);
	if (blks > UFS_NDADDR)
		for (i = 0; i < UFS_NIADDR; i++)
			inp->i_blks[UFS_NDADDR + i] = DIP(dp, di_ib[i]);
	if (inplast == listmax) {
		listmax += 100;
		inpsort = (struct inoinfo **)reallocarray((char *)inpsort,
		    listmax, sizeof(struct inoinfo *));
		if (inpsort == NULL)
			errx(EEXIT, "cannot increase directory list");
	}
	inpsort[inplast++] = inp;
}

/*
 * Look up an inode cache structure.
 */
struct inoinfo *
getinoinfo(ino_t inumber)
{
	struct inoinfo *inp;

	for (inp = inphead[inumber % dirhash]; inp; inp = inp->i_nexthash) {
		if (inp->i_number != inumber)
			continue;
		return (inp);
	}
	errx(EEXIT, "cannot find inode %ju", (uintmax_t)inumber);
	return ((struct inoinfo *)0);
}

/*
 * Clean up all the inode cache structure.
 */
void
inocleanup(void)
{
	struct inoinfo **inpp;

	if (inphead == NULL)
		return;
	for (inpp = &inpsort[inplast - 1]; inpp >= inpsort; inpp--)
		free((char *)(*inpp));
	free((char *)inphead);
	free((char *)inpsort);
	inphead = inpsort = NULL;
}

void
inodirty(union dinode *dp)
{

	if (sblock.fs_magic == FS_UFS2_MAGIC)
		ffs_update_dinode_ckhash(&sblock, (struct ufs2_dinode *)dp);
	dirty(pbp);
}

void
clri(struct inodesc *idesc, const char *type, int flag)
{
	union dinode *dp;

	dp = ginode(idesc->id_number);
	if (flag == 1) {
		pwarn("%s %s", type,
		    (DIP(dp, di_mode) & IFMT) == IFDIR ? "DIR" : "FILE");
		prtinode(idesc->id_number, dp);
		printf("\n");
	}
	if (preen || reply("CLEAR") == 1) {
		if (preen)
			printf(" (CLEARED)\n");
		n_files--;
		if (bkgrdflag == 0) {
			(void)ckinode(dp, idesc);
			inoinfo(idesc->id_number)->ino_state = USTATE;
			clearinode(dp);
			inodirty(dp);
		} else {
			cmd.value = idesc->id_number;
			cmd.size = -DIP(dp, di_nlink);
			if (debug)
				printf("adjrefcnt ino %ld amt %lld\n",
				    (long)cmd.value, (long long)cmd.size);
			if (sysctl(adjrefcnt, MIBSIZE, 0, 0,
			    &cmd, sizeof cmd) == -1)
				rwerror("ADJUST INODE", cmd.value);
		}
	}
}

int
findname(struct inodesc *idesc)
{
	struct direct *dirp = idesc->id_dirp;

	if (dirp->d_ino != idesc->id_parent || idesc->id_entryno < 2) {
		idesc->id_entryno++;
		return (KEEPON);
	}
	memmove(idesc->id_name, dirp->d_name, (size_t)dirp->d_namlen + 1);
	return (STOP|FOUND);
}

int
findino(struct inodesc *idesc)
{
	struct direct *dirp = idesc->id_dirp;

	if (dirp->d_ino == 0)
		return (KEEPON);
	if (strcmp(dirp->d_name, idesc->id_name) == 0 &&
	    dirp->d_ino >= UFS_ROOTINO && dirp->d_ino <= maxino) {
		idesc->id_parent = dirp->d_ino;
		return (STOP|FOUND);
	}
	return (KEEPON);
}

int
clearentry(struct inodesc *idesc)
{
	struct direct *dirp = idesc->id_dirp;

	if (dirp->d_ino != idesc->id_parent || idesc->id_entryno < 2) {
		idesc->id_entryno++;
		return (KEEPON);
	}
	dirp->d_ino = 0;
	return (STOP|FOUND|ALTERED);
}

void
prtinode(ino_t ino, union dinode *dp)
{
	char *p;
	struct passwd *pw;
	time_t t;

	printf(" I=%lu ", (u_long)ino);
	if (ino < UFS_ROOTINO || ino > maxino)
		return;
	printf(" OWNER=");
	if ((pw = getpwuid((int)DIP(dp, di_uid))) != NULL)
		printf("%s ", pw->pw_name);
	else
		printf("%u ", (unsigned)DIP(dp, di_uid));
	printf("MODE=%o\n", DIP(dp, di_mode));
	if (preen)
		printf("%s: ", cdevname);
	printf("SIZE=%ju ", (uintmax_t)DIP(dp, di_size));
	t = DIP(dp, di_mtime);
	p = ctime(&t);
	printf("MTIME=%12.12s %4.4s ", &p[4], &p[20]);
}

void
blkerror(ino_t ino, const char *type, ufs2_daddr_t blk)
{

	pfatal("%jd %s I=%ju", (intmax_t)blk, type, (uintmax_t)ino);
	printf("\n");
	switch (inoinfo(ino)->ino_state) {

	case FSTATE:
	case FZLINK:
		inoinfo(ino)->ino_state = FCLEAR;
		return;

	case DSTATE:
	case DZLINK:
		inoinfo(ino)->ino_state = DCLEAR;
		return;

	case FCLEAR:
	case DCLEAR:
		return;

	default:
		errx(EEXIT, "BAD STATE %d TO BLKERR", inoinfo(ino)->ino_state);
		/* NOTREACHED */
	}
}

/*
 * allocate an unused inode
 */
ino_t
allocino(ino_t request, int type)
{
	ino_t ino;
	union dinode *dp;
	struct bufarea *cgbp;
	struct cg *cgp;
	int cg;

	if (request == 0)
		request = UFS_ROOTINO;
	else if (inoinfo(request)->ino_state != USTATE)
		return (0);
	for (ino = request; ino < maxino; ino++)
		if (inoinfo(ino)->ino_state == USTATE)
			break;
	if (ino == maxino)
		return (0);
	cg = ino_to_cg(&sblock, ino);
	cgbp = cglookup(cg);
	cgp = cgbp->b_un.b_cg;
	if (!check_cgmagic(cg, cgbp))
		return (0);
	setbit(cg_inosused(cgp), ino % sblock.fs_ipg);
	cgp->cg_cs.cs_nifree--;
	switch (type & IFMT) {
	case IFDIR:
		inoinfo(ino)->ino_state = DSTATE;
		cgp->cg_cs.cs_ndir++;
		break;
	case IFREG:
	case IFLNK:
		inoinfo(ino)->ino_state = FSTATE;
		break;
	default:
		return (0);
	}
	cgdirty(cgbp);
	dp = ginode(ino);
	DIP_SET(dp, di_db[0], allocblk((long)1));
	if (DIP(dp, di_db[0]) == 0) {
		inoinfo(ino)->ino_state = USTATE;
		return (0);
	}
	DIP_SET(dp, di_mode, type);
	DIP_SET(dp, di_flags, 0);
	DIP_SET(dp, di_atime, time(NULL));
	DIP_SET(dp, di_ctime, DIP(dp, di_atime));
	DIP_SET(dp, di_mtime, DIP(dp, di_ctime));
	DIP_SET(dp, di_mtimensec, 0);
	DIP_SET(dp, di_ctimensec, 0);
	DIP_SET(dp, di_atimensec, 0);
	DIP_SET(dp, di_size, sblock.fs_fsize);
	DIP_SET(dp, di_blocks, btodb(sblock.fs_fsize));
	n_files++;
	inodirty(dp);
	inoinfo(ino)->ino_type = IFTODT(type);
	return (ino);
}

/*
 * deallocate an inode
 */
void
freeino(ino_t ino)
{
	struct inodesc idesc;
	union dinode *dp;

	memset(&idesc, 0, sizeof(struct inodesc));
	idesc.id_type = ADDR;
	idesc.id_func = pass4check;
	idesc.id_number = ino;
	dp = ginode(ino);
	(void)ckinode(dp, &idesc);
	clearinode(dp);
	inodirty(dp);
	inoinfo(ino)->ino_state = USTATE;
	n_files--;
}
