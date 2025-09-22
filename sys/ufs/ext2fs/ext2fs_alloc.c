/*	$OpenBSD: ext2fs_alloc.c,v 1.39 2021/05/16 15:10:20 deraadt Exp $	*/
/*	$NetBSD: ext2fs_alloc.c,v 1.10 2001/07/05 08:38:27 toshii Exp $	*/

/*
 * Copyright (c) 1997 Manuel Bouyer.
 * Copyright (c) 1982, 1986, 1989, 1993
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
 *
 *	@(#)ffs_alloc.c	8.11 (Berkeley) 10/27/94
 *  Modified for ext2fs by Manuel Bouyer.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/syslog.h>

#include <ufs/ufs/quota.h>
#include <ufs/ufs/inode.h>
#include <ufs/ufs/ufsmount.h>
#include <ufs/ufs/ufs_extern.h>

#include <ufs/ext2fs/ext2fs.h>
#include <ufs/ext2fs/ext2fs_extern.h>

u_long ext2gennumber;

static u_int32_t	ext2fs_alloccg(struct inode *, int, u_int32_t, int);
static int		ext2fs_dirpref(struct m_ext2fs *);
static void		ext2fs_fserr(struct m_ext2fs *, uid_t, char *);
static u_int32_t	ext2fs_hashalloc(struct inode *, int, u_int32_t, int,
			    u_int32_t (*)(struct inode *, int, u_int32_t, int));
static ufsino_t		ext2fs_nodealloccg(struct inode *, int, ufsino_t, int);
static u_int32_t	ext2fs_mapsearch(struct m_ext2fs *, char *, u_int32_t);

/*
 * Allocate a block in the file system.
 *
 * A preference may be optionally specified. If a preference is given
 * the following hierarchy is used to allocate a block:
 *   1) allocate the requested block.
 *   2) allocate a rotationally optimal block in the same cylinder.
 *   3) allocate a block in the same cylinder group.
 *   4) quadratically rehash into other cylinder groups, until an
 *	  available block is located.
 * If no block preference is given the following hierarchy is used
 * to allocate a block:
 *   1) allocate a block in the cylinder group that contains the
 *	  inode for the file.
 *   2) quadratically rehash into other cylinder groups, until an
 *	  available block is located.
 */
int
ext2fs_alloc(struct inode *ip, u_int32_t lbn, u_int32_t bpref,
    struct ucred *cred, u_int32_t *bnp)
{
	struct m_ext2fs *fs;
	u_int32_t bno;
	int cg;

	*bnp = 0;
	fs = ip->i_e2fs;
#ifdef DIAGNOSTIC
	if (cred == NOCRED)
		panic("ext2fs_alloc: missing credential");
#endif /* DIAGNOSTIC */
	if (fs->e2fs.e2fs_fbcount == 0)
		goto nospace;
	if (cred->cr_uid != 0 && freespace(fs) <= 0)
		goto nospace;
	if (bpref >= fs->e2fs.e2fs_bcount)
		bpref = 0;
	if (bpref == 0)
		cg = ino_to_cg(fs, ip->i_number);
	else
		cg = dtog(fs, bpref);
	bno = ext2fs_hashalloc(ip, cg, bpref, fs->e2fs_bsize, ext2fs_alloccg);
	if (bno > 0) {
		ip->i_e2fs_nblock += btodb(fs->e2fs_bsize);
		ip->i_flag |= IN_CHANGE | IN_UPDATE;
		*bnp = bno;
		return (0);
	}
nospace:
	ext2fs_fserr(fs, cred->cr_uid, "file system full");
	uprintf("\n%s: write failed, file system is full\n", fs->e2fs_fsmnt);
	return (ENOSPC);
}

/*
 * Allocate an inode in the file system.
 *
 * If allocating a directory, use ext2fs_dirpref to select the inode.
 * If allocating in a directory, the following hierarchy is followed:
 *   1) allocate the preferred inode.
 *   2) allocate an inode in the same cylinder group.
 *   3) quadratically rehash into other cylinder groups, until an
 *	  available inode is located.
 * If no inode preference is given the following hierarchy is used
 * to allocate an inode:
 *   1) allocate an inode in cylinder group 0.
 *   2) quadratically rehash into other cylinder groups, until an
 *	  available inode is located.
 */
int
ext2fs_inode_alloc(struct inode *pip, mode_t mode, struct ucred *cred,
    struct vnode **vpp)
{
	struct vnode *pvp;
	struct m_ext2fs *fs;
	struct inode *ip;
	ufsino_t ino, ipref;
	int cg, error;

	*vpp = NULL;
	pvp = ITOV(pip);
	fs = pip->i_e2fs;
	if (fs->e2fs.e2fs_ficount == 0)
		goto noinodes;

	if ((mode & IFMT) == IFDIR)
		cg = ext2fs_dirpref(fs);
	else
		cg = ino_to_cg(fs, pip->i_number);
	ipref = cg * fs->e2fs.e2fs_ipg + 1;
	ino = ext2fs_hashalloc(pip, cg, ipref, mode, ext2fs_nodealloccg);
	if (ino == 0)
		goto noinodes;
	error = VFS_VGET(pvp->v_mount, ino, vpp);
	if (error) {
		ext2fs_inode_free(pip, ino, mode);
		return (error);
	}
	ip = VTOI(*vpp);
	if (ip->i_e2fs_mode && ip->i_e2fs_nlink != 0) {
		printf("mode = 0%o, nlinks %u, inum = %u, fs = %s\n",
		    ip->i_e2fs_mode, ip->i_e2fs_nlink, ip->i_number,
		    fs->e2fs_fsmnt);
		panic("ext2fs_valloc: dup alloc");
	}

	memset(ip->i_e2din, 0, sizeof(struct ext2fs_dinode));

	/*
	 * Set up a new generation number for this inode.
	 */
	if (++ext2gennumber < (u_long)gettime())
		ext2gennumber = gettime();
	ip->i_e2fs_gen = ext2gennumber;
	return (0);
 noinodes:
	ext2fs_fserr(fs, cred->cr_uid, "out of inodes");
	uprintf("\n%s: create/symlink failed, no inodes free\n", fs->e2fs_fsmnt);
	return (ENOSPC);
}

/*
 * Find a cylinder to place a directory.
 *
 * The policy implemented by this algorithm is to select from
 * among those cylinder groups with above the average number of
 * free inodes, the one with the smallest number of directories.
 */
static int
ext2fs_dirpref(struct m_ext2fs *fs)
{
	int cg, maxspace, mincg, avgifree;

	avgifree = fs->e2fs.e2fs_ficount / fs->e2fs_ncg;
	maxspace = 0;
	mincg = -1;
	for (cg = 0; cg < fs->e2fs_ncg; cg++)
		if ( fs->e2fs_gd[cg].ext2bgd_nifree >= avgifree) {
			if (mincg == -1 || fs->e2fs_gd[cg].ext2bgd_nbfree > maxspace) {
				mincg = cg;
				maxspace = fs->e2fs_gd[cg].ext2bgd_nbfree;
			}
		}
	return mincg;
}

/*
 * Select the desired position for the next block in a file.  The file is
 * logically divided into sections. The first section is composed of the
 * direct blocks. Each additional section contains fs_maxbpg blocks.
 *
 * If no blocks have been allocated in the first section, the policy is to
 * request a block in the same cylinder group as the inode that describes
 * the file. Otherwise, the policy is to try to allocate the blocks
 * contiguously. The two fields of the ext2 inode extension (see
 * ufs/ufs/inode.h) help this.
 */
daddr_t
ext2fs_blkpref(struct inode *ip, u_int32_t lbn, int baps, u_int32_t *bap)
{
	struct m_ext2fs *fs;
	int cg, i;

	fs = ip->i_e2fs;
	/*
	 * if we are doing contiguous lbn allocation, try to alloc blocks
	 * contiguously on disk
	 */

	if ( ip->i_e2fs_last_blk && lbn == ip->i_e2fs_last_lblk + 1) {
		return ip->i_e2fs_last_blk + 1;
	}

	/*
	 * bap, if provided, gives us a list of blocks to which we want to
	 * stay close
	 */

	if (bap) {
		for (i = baps; i >= 0 ; i--) {
			if (bap[i]) {
				return letoh32(bap[i]) + 1;
			}
		}
	}

	/* fall back to the first block of the cylinder containing the inode */

	cg = ino_to_cg(fs, ip->i_number);
	return fs->e2fs.e2fs_bpg * cg + fs->e2fs.e2fs_first_dblock + 1;
}

/*
 * Implement the cylinder overflow algorithm.
 *
 * The policy implemented by this algorithm is:
 *   1) allocate the block in its requested cylinder group.
 *   2) quadratically rehash on the cylinder group number.
 *   3) brute force search for a free block.
 */
static u_int32_t
ext2fs_hashalloc(struct inode *ip, int cg, u_int32_t pref, int size,
    u_int32_t (*allocator)(struct inode *, int, u_int32_t, int))
{
	struct m_ext2fs *fs;
	long result;
	int i, icg = cg;

	fs = ip->i_e2fs;
	/*
	 * 1: preferred cylinder group
	 */
	result = (*allocator)(ip, cg, pref, size);
	if (result)
		return (result);
	/*
	 * 2: quadratic rehash
	 */
	for (i = 1; i < fs->e2fs_ncg; i *= 2) {
		cg += i;
		if (cg >= fs->e2fs_ncg)
			cg -= fs->e2fs_ncg;
		result = (*allocator)(ip, cg, 0, size);
		if (result)
			return (result);
	}
	/*
	 * 3: brute force search
	 * Note that we start at i == 2, since 0 was checked initially,
	 * and 1 is always checked in the quadratic rehash.
	 */
	cg = (icg + 2) % fs->e2fs_ncg;
	for (i = 2; i < fs->e2fs_ncg; i++) {
		result = (*allocator)(ip, cg, 0, size);
		if (result)
			return (result);
		cg++;
		if (cg == fs->e2fs_ncg)
			cg = 0;
	}
	return (0);
}

/*
 * Determine whether a block can be allocated.
 *
 * Check to see if a block of the appropriate size is available,
 * and if it is, allocate it.
 */
static u_int32_t
ext2fs_alloccg(struct inode *ip, int cg, u_int32_t bpref, int size)
{
	struct m_ext2fs *fs;
	char *bbp;
	struct buf *bp;
	u_int32_t bno;
	int error, start, end, loc;

	fs = ip->i_e2fs;
	if (fs->e2fs_gd[cg].ext2bgd_nbfree == 0)
		return (0);
	error = bread(ip->i_devvp, fsbtodb(fs,
	    fs->e2fs_gd[cg].ext2bgd_b_bitmap), (int)fs->e2fs_bsize, &bp);
	if (error || fs->e2fs_gd[cg].ext2bgd_nbfree == 0) {
		brelse(bp);
		return (0);
	}
	bbp = (char *)bp->b_data;

	if (dtog(fs, bpref) != cg)
		bpref = 0;
	if (bpref != 0) {
		bpref = dtogd(fs, bpref);
		/*
		 * if the requested block is available, use it
		 */
		if (isclr(bbp, bpref)) {
			bno = bpref;
			goto gotit;
		}
	}
	/*
	 * no blocks in the requested cylinder, so take next
	 * available one in this cylinder group.
	 * first try to get 8 contiguous blocks, then fall back to a single
	 * block.
	 */
	if (bpref)
		start = dtogd(fs, bpref) / NBBY;
	else
		start = 0;
	end = howmany(fs->e2fs.e2fs_fpg, NBBY) - start;
	for (loc = start; loc < end; loc++) {
		if (bbp[loc] == 0) {
			bno = loc * NBBY;
			goto gotit;
		}
	}
	for (loc = 0; loc < start; loc++) {
		if (bbp[loc] == 0) {
			bno = loc * NBBY;
			goto gotit;
		}
	}

	bno = ext2fs_mapsearch(fs, bbp, bpref);
 gotit:
#ifdef DIAGNOSTIC
	if (isset(bbp, bno)) {
		panic("%s: dup alloc: cg=%d bno=%u fs=%s",
		    __func__, cg, bno, fs->e2fs_fsmnt);
	}
#endif
	setbit(bbp, bno);
	fs->e2fs.e2fs_fbcount--;
	fs->e2fs_gd[cg].ext2bgd_nbfree--;
	fs->e2fs_fmod = 1;
	bdwrite(bp);
	return (cg * fs->e2fs.e2fs_fpg + fs->e2fs.e2fs_first_dblock + bno);
}

/*
 * Determine whether an inode can be allocated.
 *
 * Check to see if an inode is available, and if it is,
 * allocate it using the following policy:
 *   1) allocate the requested inode.
 *   2) allocate the next available inode after the requested
 *	  inode in the specified cylinder group.
 */
static ufsino_t
ext2fs_nodealloccg(struct inode *ip, int cg, ufsino_t ipref, int mode)
{
	struct m_ext2fs *fs;
	char *ibp;
	struct buf *bp;
	int error, start, len, loc, map, i;

	ipref--; /* to avoid a lot of (ipref -1) */
	fs = ip->i_e2fs;
	if (fs->e2fs_gd[cg].ext2bgd_nifree == 0)
		return (0);
	error = bread(ip->i_devvp, fsbtodb(fs,
	    fs->e2fs_gd[cg].ext2bgd_i_bitmap), (int)fs->e2fs_bsize, &bp);
	if (error) {
		brelse(bp);
		return (0);
	}
	ibp = (char *)bp->b_data;
	if (ipref) {
		ipref %= fs->e2fs.e2fs_ipg;
		if (isclr(ibp, ipref))
			goto gotit;
	}
	start = ipref / NBBY;
	len = howmany(fs->e2fs.e2fs_ipg - ipref, NBBY);
	loc = skpc(0xff, len, &ibp[start]);
	if (loc == 0) {
		len = start + 1;
		start = 0;
		loc = skpc(0xff, len, &ibp[0]);
		if (loc == 0) {
			printf("cg = %d, ipref = %u, fs = %s\n",
			    cg, ipref, fs->e2fs_fsmnt);
			panic("ext2fs_nodealloccg: map corrupted");
			/* NOTREACHED */
		}
	}
	i = start + len - loc;
	map = ibp[i];
	ipref = i * NBBY;
	for (i = 1; i < (1 << NBBY); i <<= 1, ipref++) {
		if ((map & i) == 0) {
			goto gotit;
		}
	}
	printf("fs = %s\n", fs->e2fs_fsmnt);
	panic("ext2fs_nodealloccg: block not in map");
	/* NOTREACHED */
 gotit:
	setbit(ibp, ipref);
	fs->e2fs.e2fs_ficount--;
	fs->e2fs_gd[cg].ext2bgd_nifree--;
	fs->e2fs_fmod = 1;
	if ((mode & IFMT) == IFDIR) {
		fs->e2fs_gd[cg].ext2bgd_ndirs++;
	}
	bdwrite(bp);
	return (cg * fs->e2fs.e2fs_ipg + ipref + 1);
}

/*
 * Free a block.
 *
 * The specified block is placed back in the
 * free map.
 */
void
ext2fs_blkfree(struct inode *ip, u_int32_t bno)
{
	struct m_ext2fs *fs;
	char *bbp;
	struct buf *bp;
	int error, cg;

	fs = ip->i_e2fs;
	cg = dtog(fs, bno);
	if (bno >= fs->e2fs.e2fs_bcount) {
		printf("bad block %u, ino %u\n", bno, ip->i_number);
		ext2fs_fserr(fs, ip->i_e2fs_uid, "bad block");
		return;
	}
	error = bread(ip->i_devvp,
	    fsbtodb(fs, fs->e2fs_gd[cg].ext2bgd_b_bitmap),
	    (int)fs->e2fs_bsize, &bp);
	if (error) {
		brelse(bp);
		return;
	}
	bbp = (char *)bp->b_data;
	bno = dtogd(fs, bno);
	if (isclr(bbp, bno))
		panic("%s: freeing free block: dev = 0x%x, block = %u, fs = %s",
		    __func__, ip->i_dev, bno, fs->e2fs_fsmnt);

	clrbit(bbp, bno);
	fs->e2fs.e2fs_fbcount++;
	fs->e2fs_gd[cg].ext2bgd_nbfree++;

	fs->e2fs_fmod = 1;
	bdwrite(bp);
}

/*
 * Free an inode.
 *
 * The specified inode is placed back in the free map.
 */
void
ext2fs_inode_free(struct inode *pip, ufsino_t ino, mode_t mode)
{
	struct m_ext2fs *fs;
	char *ibp;
	struct buf *bp;
	int error, cg;

	fs = pip->i_e2fs;
	if (ino > fs->e2fs.e2fs_icount || ino < EXT2_FIRSTINO)
		panic("ifree: range: dev = 0x%x, ino = %u, fs = %s",
		    pip->i_dev, ino, fs->e2fs_fsmnt);
	cg = ino_to_cg(fs, ino);
	error = bread(pip->i_devvp,
	    fsbtodb(fs, fs->e2fs_gd[cg].ext2bgd_i_bitmap),
	    (int)fs->e2fs_bsize, &bp);
	if (error) {
		brelse(bp);
		return;
	}
	ibp = (char *)bp->b_data;
	ino = (ino - 1) % fs->e2fs.e2fs_ipg;
	if (isclr(ibp, ino)) {
		printf("dev = 0x%x, ino = %d, fs = %s\n",
		    pip->i_dev, ino, fs->e2fs_fsmnt);
		if (fs->e2fs_ronly == 0)
			panic("ifree: freeing free inode");
	}
	clrbit(ibp, ino);
	fs->e2fs.e2fs_ficount++;
	fs->e2fs_gd[cg].ext2bgd_nifree++;
	if ((mode & IFMT) == IFDIR) {
		fs->e2fs_gd[cg].ext2bgd_ndirs--;
	}
	fs->e2fs_fmod = 1;
	bdwrite(bp);
}

/*
 * Find a block in the specified cylinder group.
 *
 * It is a panic if a request is made to find a block if none are
 * available.
 */

static u_int32_t
ext2fs_mapsearch(struct m_ext2fs *fs, char *bbp, u_int32_t bpref)
{
	u_int32_t bno;
	int start, len, loc, i, map;

	/*
	 * find the fragment by searching through the free block
	 * map for an appropriate bit pattern
	 */
	if (bpref)
		start = dtogd(fs, bpref) / NBBY;
	else
		start = 0;
	len = howmany(fs->e2fs.e2fs_fpg, NBBY) - start;
	loc = skpc(0xff, len, &bbp[start]);
	if (loc == 0) {
		len = start + 1;
		start = 0;
		loc = skpc(0xff, len, &bbp[start]);
		if (loc == 0) {
			printf("start = %d, len = %d, fs = %s\n",
				start, len, fs->e2fs_fsmnt);
			panic("ext2fs_alloccg: map corrupted");
			/* NOTREACHED */
		}
	}
	i = start + len - loc;
	map = bbp[i];
	bno = i * NBBY;
	for (i = 1; i < (1 << NBBY); i <<= 1, bno++) {
		if ((map & i) == 0)
			return (bno);
	}
	printf("fs = %s\n", fs->e2fs_fsmnt);
	panic("ext2fs_mapsearch: block not in map");
	/* NOTREACHED */
}

/*
 * Fserr prints the name of a file system with an error diagnostic.
 *
 * The form of the error message is:
 *	fs: error message
 */
static void
ext2fs_fserr(struct m_ext2fs *fs, uid_t uid, char *cp)
{
	log(LOG_ERR, "uid %u on %s: %s\n", uid, fs->e2fs_fsmnt, cp);
}
