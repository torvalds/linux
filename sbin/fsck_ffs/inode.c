/*	$OpenBSD: inode.c,v 1.52 2024/05/09 08:35:40 florian Exp $	*/
/*	$NetBSD: inode.c,v 1.23 1996/10/11 20:15:47 thorpej Exp $	*/

/*
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

#include <sys/param.h>	/* setbit btodb */
#include <sys/time.h>
#include <ufs/ufs/dinode.h>
#include <ufs/ufs/dir.h>
#include <ufs/ffs/fs.h>
#ifndef SMALL
#include <pwd.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include "fsck.h"
#include "fsutil.h"
#include "extern.h"

#define MINIMUM(a, b)	(((a) < (b)) ? (a) : (b))
#define MAXIMUM(a, b)	(((a) > (b)) ? (a) : (b))

static ino_t startinum;

static int iblock(struct inodesc *, long, off_t);

int
ckinode(union dinode *dp, struct inodesc *idesc)
{
	long ret, ndb, offset;
	union dinode dino;
	off_t sizepb, remsize;
	mode_t mode;
	int i;
	char pathbuf[PATH_MAX + 1];

	if (idesc->id_fix != IGNORE)
		idesc->id_fix = DONTKNOW;
	idesc->id_entryno = 0;
	idesc->id_filesize = DIP(dp, di_size);
	mode = DIP(dp, di_mode) & IFMT;
	if (mode == IFBLK || mode == IFCHR || (mode == IFLNK &&
	    DIP(dp, di_size) < sblock.fs_maxsymlinklen))
		return (KEEPON);
	if (sblock.fs_magic == FS_UFS1_MAGIC)
		dino.dp1 = dp->dp1;
	else
		dino.dp2 = dp->dp2;
	ndb = howmany(DIP(&dino, di_size), sblock.fs_bsize);
	for (i = 0; i < NDADDR; i++) {
		if (--ndb == 0 && (offset = blkoff(&sblock,
		    DIP(&dino, di_size))) != 0)
			idesc->id_numfrags =
				numfrags(&sblock, fragroundup(&sblock, offset));
		else
			idesc->id_numfrags = sblock.fs_frag;
		if (DIP(&dino, di_db[i]) == 0) {
			if (idesc->id_type == DATA && ndb >= 0) {
				/* An empty block in a directory XXX */
				getpathname(pathbuf, sizeof pathbuf,
				    idesc->id_number, idesc->id_number);
				pfatal("DIRECTORY %s: CONTAINS EMPTY BLOCKS",
				    pathbuf);
				if (reply("ADJUST LENGTH") == 1) {
					dp = ginode(idesc->id_number);
					DIP_SET(dp, di_size,
					    i * sblock.fs_bsize);
					printf(
					    "YOU MUST RERUN FSCK AFTERWARDS\n");
					rerun = 1;
					inodirty();
				}
			}
			continue;
		}
		idesc->id_blkno = DIP(&dino, di_db[i]);
		if (idesc->id_type == ADDR)
			ret = (*idesc->id_func)(idesc);
		else
			ret = dirscan(idesc);
		if (ret & STOP)
			return (ret);
	}
	idesc->id_numfrags = sblock.fs_frag;
	remsize = DIP(&dino, di_size) - sblock.fs_bsize * NDADDR;
	sizepb = sblock.fs_bsize;
	for (i = 0; i < NIADDR; i++) {
		if (DIP(&dino, di_ib[i])) {
			idesc->id_blkno = DIP(&dino, di_ib[i]);
			ret = iblock(idesc, i + 1, remsize);
			if (ret & STOP)
				return (ret);
		} else {
			if (idesc->id_type == DATA && remsize > 0) {
				/* An empty block in a directory XXX */
				getpathname(pathbuf, sizeof pathbuf,
				    idesc->id_number, idesc->id_number);
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
					inodirty();
					break;
				}
			}
		}
		sizepb *= NINDIR(&sblock);
		remsize -= sizepb;
	}
	return (KEEPON);
}

static int
iblock(struct inodesc *idesc, long ilevel, off_t isize)
{
	struct bufarea *bp;
	int i, n, (*func)(struct inodesc *), nif;
	off_t sizepb;
	char buf[BUFSIZ];
	char pathbuf[PATH_MAX + 1];
	union dinode *dp;

	if (idesc->id_type == ADDR) {
		func = idesc->id_func;
		if (((n = (*func)(idesc)) & KEEPON) == 0)
			return (n);
	} else
		func = dirscan;
	if (isize < 0 || chkrange(idesc->id_blkno, idesc->id_numfrags))
		return (SKIP);
	bp = getdatablk(idesc->id_blkno, sblock.fs_bsize);
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
			(void)snprintf(buf, sizeof buf,
			    "PARTIALLY TRUNCATED INODE I=%llu",
			    (unsigned long long)idesc->id_number);
			if (preen)
				pfatal("%s", buf);
			else if (dofix(idesc, buf)) {
				IBLK_SET(bp, i, 0);
				dirty(bp);
			}
		}
		flush(fswritefd, bp);
	}
	for (i = 0; i < nif; i++) {
		if (IBLK(bp, i)) {
			idesc->id_blkno = IBLK(bp, i);
			if (ilevel == 0)
				n = (*func)(idesc);
			else
				n = iblock(idesc, ilevel, isize);
			if (n & STOP) {
				bp->b_flags &= ~B_INUSE;
				return (n);
			}
		} else {
			if (idesc->id_type == DATA && isize > 0) {
				/* An empty block in a directory XXX */
				getpathname(pathbuf, sizeof pathbuf,
				    idesc->id_number, idesc->id_number);
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
					inodirty();
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
chkrange(daddr_t blk, int cnt)
{
	int c;

	if (cnt <= 0 || blk <= 0 || blk > maxfsblock ||
	    cnt - 1 > maxfsblock - blk)
		return (1);
	if (cnt > sblock.fs_frag ||
	    fragnum(&sblock, blk) + cnt > sblock.fs_frag) {
		if (debug)
			printf("bad size: blk %lld, offset %lld, size %d\n",
			    (long long)blk, (long long)fragnum(&sblock, blk),
			    cnt);
		return (1);
	}
	c = dtog(&sblock, blk);
	if (blk < cgdmin(&sblock, c)) {
		if ((blk + cnt) > cgsblock(&sblock, c)) {
			if (debug) {
				printf("blk %lld < cgdmin %lld;",
				    (long long)blk,
				    (long long)cgdmin(&sblock, c));
				printf(" blk + cnt %lld > cgsbase %lld\n",
				    (long long)(blk + cnt),
				    (long long)cgsblock(&sblock, c));
			}
			return (1);
		}
	} else {
		if ((blk + cnt) > cgbase(&sblock, c+1)) {
			if (debug)  {
				printf("blk %lld >= cgdmin %lld;",
				    (long long)blk,
				    (long long)cgdmin(&sblock, c));
				printf(" blk + cnt %lld > sblock.fs_fpg %d\n",
				    (long long)(blk+cnt), sblock.fs_fpg);
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
	daddr_t iblk;

	if (inumber < ROOTINO || inumber > maxino)
		errexit("bad inode number %llu to ginode\n",
		    (unsigned long long)inumber);
	if (startinum == 0 ||
	    inumber < startinum || inumber >= startinum + INOPB(&sblock)) {
		iblk = ino_to_fsba(&sblock, inumber);
		if (pbp != 0)
			pbp->b_flags &= ~B_INUSE;
		pbp = getdatablk(iblk, sblock.fs_bsize);
		startinum = (inumber / INOPB(&sblock)) * INOPB(&sblock);
	}
	if (sblock.fs_magic == FS_UFS1_MAGIC)
		return ((union dinode *)
		    &pbp->b_un.b_dinode1[inumber % INOPB(&sblock)]);
	return ((union dinode *)&pbp->b_un.b_dinode2[inumber % INOPB(&sblock)]);
}

/*
 * Special purpose version of ginode used to optimize first pass
 * over all the inodes in numerical order.
 */
ino_t nextino, lastinum;
long readcnt, readpercg, fullcnt, inobufsize, partialcnt, partialsize;
static caddr_t inodebuf;

union dinode *
getnextinode(ino_t inumber)
{
	long size;
	daddr_t dblk;
	union dinode *dp;
	static caddr_t nextinop;

	if (inumber != nextino++ || inumber > maxino)
		errexit("bad inode number %llu to nextinode %llu\n",
		    (unsigned long long)inumber,
		    (unsigned long long)nextino);
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
		(void)bread(fsreadfd, inodebuf, dblk, size);
		nextinop = inodebuf;
	}
	dp = (union dinode *)nextinop;
	if (sblock.fs_magic == FS_UFS1_MAGIC)
		nextinop += sizeof(struct ufs1_dinode);
	else
		nextinop += sizeof(struct ufs2_dinode);
	return (dp);
}

void
setinodebuf(ino_t inum)
{

	startinum = 0;
	nextino = inum;
	lastinum = inum;
	readcnt = 0;
	if (inodebuf != NULL)
		return;
	inobufsize = blkroundup(&sblock, INOBUFSIZE);
	if (sblock.fs_magic == FS_UFS1_MAGIC)
		fullcnt = inobufsize / sizeof(struct ufs1_dinode);
	else
		fullcnt = inobufsize / sizeof(struct ufs2_dinode);
	readpercg = sblock.fs_ipg / fullcnt;
	partialcnt = sblock.fs_ipg % fullcnt;
	if (sblock.fs_magic == FS_UFS1_MAGIC)
		partialsize = partialcnt * sizeof(struct ufs1_dinode);
	else
		partialsize = partialcnt * sizeof(struct ufs2_dinode);
	if (partialcnt != 0) {
		readpercg++;
	} else {
		partialcnt = fullcnt;
		partialsize = inobufsize;
	}
	if (inodebuf == NULL &&
	    (inodebuf = Malloc((unsigned)inobufsize)) == NULL)
		errexit("Cannot allocate space for inode buffer\n");
}

void
freeinodebuf(void)
{

	free(inodebuf);
	inodebuf = NULL;
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
	struct inoinfo *inp;
	struct inoinfo **inpp, **newinpsort;
	unsigned int blks;
	long newlistmax;
	int i;

	blks = howmany(DIP(dp, di_size), sblock.fs_bsize);
	if (blks > NDADDR)
		blks = NDADDR + NIADDR;
	inp = Malloc(sizeof(*inp) + (blks ? blks - 1 : 0) * sizeof(daddr_t));
	if (inp == NULL)
		errexit("cannot allocate memory for inode cache\n");
	inpp = &inphead[inumber % numdirs];
	inp->i_nexthash = *inpp;
	*inpp = inp;
	inp->i_child = inp->i_sibling = 0;
	if (inumber == ROOTINO)
		inp->i_parent = ROOTINO;
	else
		inp->i_parent = 0;
	inp->i_dotdot = 0;
	inp->i_number = inumber;
	inp->i_isize = DIP(dp, di_size);
	inp->i_numblks = blks;
	for (i = 0; i < (blks < NDADDR ? blks : NDADDR); i++)
		inp->i_blks[i] = DIP(dp, di_db[i]);
	if (blks > NDADDR)
		for (i = 0; i < NIADDR; i++)
			inp->i_blks[NDADDR + i] = DIP(dp, di_ib[i]);
	if (inplast == listmax) {
		newlistmax = listmax + 100;
		newinpsort = Reallocarray(inpsort,
		    (unsigned)newlistmax, sizeof(struct inoinfo *));
		if (newinpsort == NULL)
			errexit("cannot increase directory list\n");
		inpsort = newinpsort;
		listmax = newlistmax;
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

	for (inp = inphead[inumber % numdirs]; inp; inp = inp->i_nexthash) {
		if (inp->i_number != inumber)
			continue;
		return (inp);
	}
	errexit("cannot find inode %llu\n", (unsigned long long)inumber);
	return (NULL);
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
		free(*inpp);
	free(inphead);
	free(inpsort);
	inphead = inpsort = NULL;
}

void
inodirty(void)
{
	dirty(pbp);
}

void
clri(struct inodesc *idesc, char *type, int flag)
{
	union dinode *dp;

	dp = ginode(idesc->id_number);
	if (flag == 1) {
		pwarn("%s %s", type,
		    (DIP(dp, di_mode) & IFMT) == IFDIR ? "DIR" : "FILE");
		pinode(idesc->id_number);
	}
	if (preen || reply("CLEAR") == 1) {
		if (preen)
			printf(" (CLEARED)\n");
		n_files--;
		(void)ckinode(dp, idesc);
		clearinode(dp);
		SET_ISTATE(idesc->id_number, USTATE);
		inodirty();
	}
}

int
findname(struct inodesc *idesc)
{
	struct direct *dirp = idesc->id_dirp;

	if (dirp->d_ino != idesc->id_parent)
		return (KEEPON);
	memcpy(idesc->id_name, dirp->d_name, (size_t)dirp->d_namlen + 1);
	return (STOP|FOUND);
}

int
findino(struct inodesc *idesc)
{
	struct direct *dirp = idesc->id_dirp;

	if (dirp->d_ino == 0)
		return (KEEPON);
	if (strcmp(dirp->d_name, idesc->id_name) == 0 &&
	    dirp->d_ino >= ROOTINO && dirp->d_ino <= maxino) {
		idesc->id_parent = dirp->d_ino;
		return (STOP|FOUND);
	}
	return (KEEPON);
}

void
pinode(ino_t ino)
{
	union dinode *dp;
	const char *p;
	time_t t;

	printf(" I=%llu ", (unsigned long long)ino);
	if (ino < ROOTINO || ino > maxino)
		return;
	dp = ginode(ino);
	printf(" OWNER=");
#ifndef SMALL
	if ((p = user_from_uid(DIP(dp, di_uid), 1)) != NULL)
		printf("%s ", p);
	else
#endif
		printf("%u ", (unsigned)DIP(dp, di_uid));
	printf("MODE=%o\n", DIP(dp, di_mode));
	if (preen)
		printf("%s: ", cdevname());
	printf("SIZE=%llu ", (unsigned long long)DIP(dp, di_size));
	t = DIP(dp, di_mtime);
	p = ctime(&t);
	if (p)
		printf("MTIME=%12.12s %4.4s ", &p[4], &p[20]);
	else
		printf("MTIME=%lld ", t);
}

void
blkerror(ino_t ino, char *type, daddr_t blk)
{

	pfatal("%lld %s I=%llu", blk, type, (unsigned long long)ino);
	printf("\n");
	switch (GET_ISTATE(ino)) {

	case FSTATE:
		SET_ISTATE(ino, FCLEAR);
		return;

	case DSTATE:
		SET_ISTATE(ino, DCLEAR);
		return;

	case FCLEAR:
	case DCLEAR:
		return;

	default:
		errexit("BAD STATE %d TO BLKERR\n", GET_ISTATE(ino));
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
	time_t t;
	struct inostat *info;

	if (request == 0)
		request = ROOTINO;
	else if (GET_ISTATE(request) != USTATE)
		return (0);
	for (ino = request; ino < maxino; ino++)
		if (GET_ISTATE(ino) == USTATE)
			break;
	if (ino == maxino)
		return (0);
	cg = ino_to_cg(&sblock, ino);
	/* If necessary, extend the inoinfo array. grow exponentially */
	if ((ino % sblock.fs_ipg) >= (uint64_t)inostathead[cg].il_numalloced) {
		unsigned long newalloced, i;
		newalloced = MINIMUM(sblock.fs_ipg,
			MAXIMUM(2 * inostathead[cg].il_numalloced, 10));
		info = Calloc(newalloced, sizeof(struct inostat));
		if (info == NULL) {
			pwarn("cannot alloc %zu bytes to extend inoinfo\n",
				sizeof(struct inostat) * newalloced);
			return 0;
		}
		memmove(info, inostathead[cg].il_stat,
			inostathead[cg].il_numalloced * sizeof(*info));
		for (i = inostathead[cg].il_numalloced; i < newalloced; i++) {
			info[i].ino_state = USTATE;
		}
		if (inostathead[cg].il_numalloced)
			free(inostathead[cg].il_stat);
		inostathead[cg].il_stat = info;
		inostathead[cg].il_numalloced = newalloced;
		info = inoinfo(ino);
	}
	cgbp = cglookup(cg);
	cgp = cgbp->b_un.b_cg;
	if (!cg_chkmagic(cgp))
		pfatal("CG %d: BAD MAGIC NUMBER\n", cg);
	setbit(cg_inosused(cgp), ino % sblock.fs_ipg);
	cgp->cg_cs.cs_nifree--;

	switch (type & IFMT) {
	case IFDIR:
		SET_ISTATE(ino, DSTATE);
		cgp->cg_cs.cs_ndir++;
		break;
	case IFREG:
	case IFLNK:
		SET_ISTATE(ino, FSTATE);
		break;
	default:
		return (0);
	}
	dirty(cgbp);
	dp = ginode(ino);
	DIP_SET(dp, di_db[0],  allocblk(1));
	if (DIP(dp, di_db[0]) == 0) {
		SET_ISTATE(ino, USTATE);
		return (0);
	}
	DIP_SET(dp, di_mode, type);
	DIP_SET(dp, di_uid, geteuid());
	DIP_SET(dp, di_gid, getegid());
	DIP_SET(dp, di_flags, 0);
	(void)time(&t);
	DIP_SET(dp, di_atime, t);
	DIP_SET(dp, di_atimensec, 0);
	DIP_SET(dp, di_mtime, t);
	DIP_SET(dp, di_mtimensec, 0);
	DIP_SET(dp, di_ctime, t);
	DIP_SET(dp, di_ctimensec, 0);
	DIP_SET(dp, di_size, sblock.fs_fsize);
	DIP_SET(dp, di_blocks, btodb(sblock.fs_fsize));
	n_files++;
	inodirty();
	SET_ITYPE(ino, IFTODT(type));
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
	inodirty();
	SET_ISTATE(ino, USTATE);
	n_files--;
}
