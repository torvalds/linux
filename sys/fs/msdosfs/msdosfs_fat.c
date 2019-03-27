/* $FreeBSD$ */
/*	$NetBSD: msdosfs_fat.c,v 1.28 1997/11/17 15:36:49 ws Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (C) 1994, 1995, 1997 Wolfgang Solfrank.
 * Copyright (C) 1994, 1995, 1997 TooLs GmbH.
 * All rights reserved.
 * Original code by Paul Popelka (paulp@uts.amdahl.com) (see below).
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by TooLs GmbH.
 * 4. The name of TooLs GmbH may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY TOOLS GMBH ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*-
 * Written by Paul Popelka (paulp@uts.amdahl.com)
 *
 * You can do anything you want with this software, just don't say you wrote
 * it, and don't remove this notice.
 *
 * This software is provided "as is".
 *
 * The author supplies this software to be publicly redistributed on the
 * understanding that the author is not responsible for the correct
 * functioning of this software in any circumstances and is not liable for
 * any damages caused by this software.
 *
 * October 1992
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/mount.h>
#include <sys/vnode.h>

#include <fs/msdosfs/bpb.h>
#include <fs/msdosfs/direntry.h>
#include <fs/msdosfs/denode.h>
#include <fs/msdosfs/fat.h>
#include <fs/msdosfs/msdosfsmount.h>

#define	FULL_RUN	((u_int)0xffffffff)

static int	chainalloc(struct msdosfsmount *pmp, u_long start,
		    u_long count, u_long fillwith, u_long *retcluster,
		    u_long *got);
static int	chainlength(struct msdosfsmount *pmp, u_long start,
		    u_long count);
static void	fatblock(struct msdosfsmount *pmp, u_long ofs, u_long *bnp,
		    u_long *sizep, u_long *bop);
static int	fatchain(struct msdosfsmount *pmp, u_long start, u_long count,
		    u_long fillwith);
static void	fc_lookup(struct denode *dep, u_long findcn, u_long *frcnp,
		    u_long *fsrcnp);
static void	updatefats(struct msdosfsmount *pmp, struct buf *bp,
		    u_long fatbn);
static __inline void
		usemap_alloc(struct msdosfsmount *pmp, u_long cn);
static __inline void
		usemap_free(struct msdosfsmount *pmp, u_long cn);
static int	clusteralloc1(struct msdosfsmount *pmp, u_long start,
		    u_long count, u_long fillwith, u_long *retcluster,
		    u_long *got);

static void
fatblock(struct msdosfsmount *pmp, u_long ofs, u_long *bnp, u_long *sizep,
    u_long *bop)
{
	u_long bn, size;

	bn = ofs / pmp->pm_fatblocksize * pmp->pm_fatblocksec;
	size = min(pmp->pm_fatblocksec, pmp->pm_FATsecs - bn)
	    * DEV_BSIZE;
	bn += pmp->pm_fatblk + pmp->pm_curfat * pmp->pm_FATsecs;

	if (bnp)
		*bnp = bn;
	if (sizep)
		*sizep = size;
	if (bop)
		*bop = ofs % pmp->pm_fatblocksize;
}

/*
 * Map the logical cluster number of a file into a physical disk sector
 * that is filesystem relative.
 *
 * dep	  - address of denode representing the file of interest
 * findcn - file relative cluster whose filesystem relative cluster number
 *	    and/or block number are/is to be found
 * bnp	  - address of where to place the filesystem relative block number.
 *	    If this pointer is null then don't return this quantity.
 * cnp	  - address of where to place the filesystem relative cluster number.
 *	    If this pointer is null then don't return this quantity.
 * sp     - pointer to returned block size
 *
 * NOTE: Either bnp or cnp must be non-null.
 * This function has one side effect.  If the requested file relative cluster
 * is beyond the end of file, then the actual number of clusters in the file
 * is returned in *cnp.  This is useful for determining how long a directory is.
 *  If cnp is null, nothing is returned.
 */
int
pcbmap(struct denode *dep, u_long findcn, daddr_t *bnp, u_long *cnp, int *sp)
{
	int error;
	u_long i;
	u_long cn;
	u_long prevcn = 0; /* XXX: prevcn could be used unititialized */
	u_long byteoffset;
	u_long bn;
	u_long bo;
	struct buf *bp = NULL;
	u_long bp_bn = -1;
	struct msdosfsmount *pmp = dep->de_pmp;
	u_long bsize;

	KASSERT(bnp != NULL || cnp != NULL || sp != NULL,
	    ("pcbmap: extra call"));
	ASSERT_VOP_ELOCKED(DETOV(dep), "pcbmap");

	cn = dep->de_StartCluster;
	/*
	 * The "file" that makes up the root directory is contiguous,
	 * permanently allocated, of fixed size, and is not made up of
	 * clusters.  If the cluster number is beyond the end of the root
	 * directory, then return the number of clusters in the file.
	 */
	if (cn == MSDOSFSROOT) {
		if (dep->de_Attributes & ATTR_DIRECTORY) {
			if (de_cn2off(pmp, findcn) >= dep->de_FileSize) {
				if (cnp)
					*cnp = de_bn2cn(pmp, pmp->pm_rootdirsize);
				return (E2BIG);
			}
			if (bnp)
				*bnp = pmp->pm_rootdirblk + de_cn2bn(pmp, findcn);
			if (cnp)
				*cnp = MSDOSFSROOT;
			if (sp)
				*sp = min(pmp->pm_bpcluster,
				    dep->de_FileSize - de_cn2off(pmp, findcn));
			return (0);
		} else {		/* just an empty file */
			if (cnp)
				*cnp = 0;
			return (E2BIG);
		}
	}

	/*
	 * All other files do I/O in cluster sized blocks
	 */
	if (sp)
		*sp = pmp->pm_bpcluster;

	/*
	 * Rummage around in the FAT cache, maybe we can avoid tromping
	 * through every FAT entry for the file. And, keep track of how far
	 * off the cache was from where we wanted to be.
	 */
	i = 0;
	fc_lookup(dep, findcn, &i, &cn);

	/*
	 * Handle all other files or directories the normal way.
	 */
	for (; i < findcn; i++) {
		/*
		 * Stop with all reserved clusters, not just with EOF.
		 */
		if ((cn | ~pmp->pm_fatmask) >= CLUST_RSRVD)
			goto hiteof;
		byteoffset = FATOFS(pmp, cn);
		fatblock(pmp, byteoffset, &bn, &bsize, &bo);
		if (bn != bp_bn) {
			if (bp)
				brelse(bp);
			error = bread(pmp->pm_devvp, bn, bsize, NOCRED, &bp);
			if (error) {
				brelse(bp);
				return (error);
			}
			bp_bn = bn;
		}
		prevcn = cn;
		if (bo >= bsize) {
			if (bp)
				brelse(bp);
			return (EIO);
		}
		if (FAT32(pmp))
			cn = getulong(&bp->b_data[bo]);
		else
			cn = getushort(&bp->b_data[bo]);
		if (FAT12(pmp) && (prevcn & 1))
			cn >>= 4;
		cn &= pmp->pm_fatmask;

		/*
		 * Force the special cluster numbers
		 * to be the same for all cluster sizes
		 * to let the rest of msdosfs handle
		 * all cases the same.
		 */
		if ((cn | ~pmp->pm_fatmask) >= CLUST_RSRVD)
			cn |= ~pmp->pm_fatmask;
	}

	if (!MSDOSFSEOF(pmp, cn)) {
		if (bp)
			brelse(bp);
		if (bnp)
			*bnp = cntobn(pmp, cn);
		if (cnp)
			*cnp = cn;
		fc_setcache(dep, FC_LASTMAP, i, cn);
		return (0);
	}

hiteof:;
	if (cnp)
		*cnp = i;
	if (bp)
		brelse(bp);
	/* update last file cluster entry in the FAT cache */
	fc_setcache(dep, FC_LASTFC, i - 1, prevcn);
	return (E2BIG);
}

/*
 * Find the closest entry in the FAT cache to the cluster we are looking
 * for.
 */
static void
fc_lookup(struct denode *dep, u_long findcn, u_long *frcnp, u_long *fsrcnp)
{
	int i;
	u_long cn;
	struct fatcache *closest = NULL;

	ASSERT_VOP_LOCKED(DETOV(dep), "fc_lookup");

	for (i = 0; i < FC_SIZE; i++) {
		cn = dep->de_fc[i].fc_frcn;
		if (cn != FCE_EMPTY && cn <= findcn) {
			if (closest == NULL || cn > closest->fc_frcn)
				closest = &dep->de_fc[i];
		}
	}
	if (closest) {
		*frcnp = closest->fc_frcn;
		*fsrcnp = closest->fc_fsrcn;
	}
}

/*
 * Purge the FAT cache in denode dep of all entries relating to file
 * relative cluster frcn and beyond.
 */
void
fc_purge(struct denode *dep, u_int frcn)
{
	int i;
	struct fatcache *fcp;

	ASSERT_VOP_ELOCKED(DETOV(dep), "fc_purge");

	fcp = dep->de_fc;
	for (i = 0; i < FC_SIZE; i++, fcp++) {
		if (fcp->fc_frcn >= frcn)
			fcp->fc_frcn = FCE_EMPTY;
	}
}

/*
 * Update the FAT.
 * If mirroring the FAT, update all copies, with the first copy as last.
 * Else update only the current FAT (ignoring the others).
 *
 * pmp	 - msdosfsmount structure for filesystem to update
 * bp	 - addr of modified FAT block
 * fatbn - block number relative to begin of filesystem of the modified FAT block.
 */
static void
updatefats(struct msdosfsmount *pmp, struct buf *bp, u_long fatbn)
{
	struct buf *bpn;
	int cleanfat, i;

#ifdef MSDOSFS_DEBUG
	printf("updatefats(pmp %p, bp %p, fatbn %lu)\n", pmp, bp, fatbn);
#endif

	if (pmp->pm_flags & MSDOSFS_FATMIRROR) {
		/*
		 * Now copy the block(s) of the modified FAT to the other copies of
		 * the FAT and write them out.  This is faster than reading in the
		 * other FATs and then writing them back out.  This could tie up
		 * the FAT for quite a while. Preventing others from accessing it.
		 * To prevent us from going after the FAT quite so much we use
		 * delayed writes, unless they specified "synchronous" when the
		 * filesystem was mounted.  If synch is asked for then use
		 * bwrite()'s and really slow things down.
		 */
		if (fatbn != pmp->pm_fatblk || FAT12(pmp))
			cleanfat = 0;
		else if (FAT16(pmp))
			cleanfat = 16;
		else
			cleanfat = 32;
		for (i = 1; i < pmp->pm_FATs; i++) {
			fatbn += pmp->pm_FATsecs;
			/* getblk() never fails */
			bpn = getblk(pmp->pm_devvp, fatbn, bp->b_bcount,
			    0, 0, 0);
			memcpy(bpn->b_data, bp->b_data, bp->b_bcount);
			/* Force the clean bit on in the other copies. */
			if (cleanfat == 16)
				((uint8_t *)bpn->b_data)[3] |= 0x80;
			else if (cleanfat == 32)
				((uint8_t *)bpn->b_data)[7] |= 0x08;
			if (pmp->pm_mountp->mnt_flag & MNT_SYNCHRONOUS)
				bwrite(bpn);
			else
				bdwrite(bpn);
		}
	}

	/*
	 * Write out the first (or current) FAT last.
	 */
	if (pmp->pm_mountp->mnt_flag & MNT_SYNCHRONOUS)
		bwrite(bp);
	else
		bdwrite(bp);
}

/*
 * Updating entries in 12 bit FATs is a pain in the butt.
 *
 * The following picture shows where nibbles go when moving from a 12 bit
 * cluster number into the appropriate bytes in the FAT.
 *
 *	byte m        byte m+1      byte m+2
 *	+----+----+   +----+----+   +----+----+
 *	|  0    1 |   |  2    3 |   |  4    5 |   FAT bytes
 *	+----+----+   +----+----+   +----+----+
 *
 *	+----+----+----+   +----+----+----+
 *	|  3    0    1 |   |  4    5    2 |
 *	+----+----+----+   +----+----+----+
 *	cluster n  	   cluster n+1
 *
 * Where n is even. m = n + (n >> 2)
 *
 */
static __inline void
usemap_alloc(struct msdosfsmount *pmp, u_long cn)
{

	MSDOSFS_ASSERT_MP_LOCKED(pmp);

	KASSERT(cn <= pmp->pm_maxcluster, ("cn too large %lu %lu", cn,
	    pmp->pm_maxcluster));
	KASSERT((pmp->pm_flags & MSDOSFSMNT_RONLY) == 0,
	    ("usemap_alloc on ro msdosfs mount"));
	KASSERT((pmp->pm_inusemap[cn / N_INUSEBITS] & (1 << (cn % N_INUSEBITS)))
	    == 0, ("Allocating used sector %ld %ld %x", cn, cn % N_INUSEBITS,
		(unsigned)pmp->pm_inusemap[cn / N_INUSEBITS]));
	pmp->pm_inusemap[cn / N_INUSEBITS] |= 1U << (cn % N_INUSEBITS);
	KASSERT(pmp->pm_freeclustercount > 0, ("usemap_alloc: too little"));
	pmp->pm_freeclustercount--;
	pmp->pm_flags |= MSDOSFS_FSIMOD;
}

static __inline void
usemap_free(struct msdosfsmount *pmp, u_long cn)
{

	MSDOSFS_ASSERT_MP_LOCKED(pmp);

	KASSERT(cn <= pmp->pm_maxcluster, ("cn too large %lu %lu", cn,
	    pmp->pm_maxcluster));
	KASSERT((pmp->pm_flags & MSDOSFSMNT_RONLY) == 0,
	    ("usemap_free on ro msdosfs mount"));
	pmp->pm_freeclustercount++;
	pmp->pm_flags |= MSDOSFS_FSIMOD;
	KASSERT((pmp->pm_inusemap[cn / N_INUSEBITS] & (1 << (cn % N_INUSEBITS)))
	    != 0, ("Freeing unused sector %ld %ld %x", cn, cn % N_INUSEBITS,
		(unsigned)pmp->pm_inusemap[cn / N_INUSEBITS]));
	pmp->pm_inusemap[cn / N_INUSEBITS] &= ~(1U << (cn % N_INUSEBITS));
}

int
clusterfree(struct msdosfsmount *pmp, u_long cluster, u_long *oldcnp)
{
	int error;
	u_long oldcn;

	error = fatentry(FAT_GET_AND_SET, pmp, cluster, &oldcn, MSDOSFSFREE);
	if (error)
		return (error);
	/*
	 * If the cluster was successfully marked free, then update
	 * the count of free clusters, and turn off the "allocated"
	 * bit in the "in use" cluster bit map.
	 */
	MSDOSFS_LOCK_MP(pmp);
	usemap_free(pmp, cluster);
	MSDOSFS_UNLOCK_MP(pmp);
	if (oldcnp)
		*oldcnp = oldcn;
	return (0);
}

/*
 * Get or Set or 'Get and Set' the cluster'th entry in the FAT.
 *
 * function	- whether to get or set a FAT entry
 * pmp		- address of the msdosfsmount structure for the filesystem
 *		  whose FAT is to be manipulated.
 * cn		- which cluster is of interest
 * oldcontents	- address of a word that is to receive the contents of the
 *		  cluster'th entry if this is a get function
 * newcontents	- the new value to be written into the cluster'th element of
 *		  the FAT if this is a set function.
 *
 * This function can also be used to free a cluster by setting the FAT entry
 * for a cluster to 0.
 *
 * All copies of the FAT are updated if this is a set function. NOTE: If
 * fatentry() marks a cluster as free it does not update the inusemap in
 * the msdosfsmount structure. This is left to the caller.
 */
int
fatentry(int function, struct msdosfsmount *pmp, u_long cn, u_long *oldcontents,
    u_long newcontents)
{
	int error;
	u_long readcn;
	u_long bn, bo, bsize, byteoffset;
	struct buf *bp;

#ifdef	MSDOSFS_DEBUG
	printf("fatentry(func %d, pmp %p, clust %lu, oldcon %p, newcon %lx)\n",
	    function, pmp, cn, oldcontents, newcontents);
#endif

#ifdef DIAGNOSTIC
	/*
	 * Be sure they asked us to do something.
	 */
	if ((function & (FAT_SET | FAT_GET)) == 0) {
#ifdef MSDOSFS_DEBUG
		printf("fatentry(): function code doesn't specify get or set\n");
#endif
		return (EINVAL);
	}

	/*
	 * If they asked us to return a cluster number but didn't tell us
	 * where to put it, give them an error.
	 */
	if ((function & FAT_GET) && oldcontents == NULL) {
#ifdef MSDOSFS_DEBUG
		printf("fatentry(): get function with no place to put result\n");
#endif
		return (EINVAL);
	}
#endif

	/*
	 * Be sure the requested cluster is in the filesystem.
	 */
	if (cn < CLUST_FIRST || cn > pmp->pm_maxcluster)
		return (EINVAL);

	byteoffset = FATOFS(pmp, cn);
	fatblock(pmp, byteoffset, &bn, &bsize, &bo);
	error = bread(pmp->pm_devvp, bn, bsize, NOCRED, &bp);
	if (error) {
		brelse(bp);
		return (error);
	}

	if (function & FAT_GET) {
		if (FAT32(pmp))
			readcn = getulong(&bp->b_data[bo]);
		else
			readcn = getushort(&bp->b_data[bo]);
		if (FAT12(pmp) & (cn & 1))
			readcn >>= 4;
		readcn &= pmp->pm_fatmask;
		/* map reserved FAT entries to same values for all FATs */
		if ((readcn | ~pmp->pm_fatmask) >= CLUST_RSRVD)
			readcn |= ~pmp->pm_fatmask;
		*oldcontents = readcn;
	}
	if (function & FAT_SET) {
		switch (pmp->pm_fatmask) {
		case FAT12_MASK:
			readcn = getushort(&bp->b_data[bo]);
			if (cn & 1) {
				readcn &= 0x000f;
				readcn |= newcontents << 4;
			} else {
				readcn &= 0xf000;
				readcn |= newcontents & 0xfff;
			}
			putushort(&bp->b_data[bo], readcn);
			break;
		case FAT16_MASK:
			putushort(&bp->b_data[bo], newcontents);
			break;
		case FAT32_MASK:
			/*
			 * According to spec we have to retain the
			 * high order bits of the FAT entry.
			 */
			readcn = getulong(&bp->b_data[bo]);
			readcn &= ~FAT32_MASK;
			readcn |= newcontents & FAT32_MASK;
			putulong(&bp->b_data[bo], readcn);
			break;
		}
		updatefats(pmp, bp, bn);
		bp = NULL;
		pmp->pm_fmod = 1;
	}
	if (bp)
		brelse(bp);
	return (0);
}

/*
 * Update a contiguous cluster chain
 *
 * pmp	    - mount point
 * start    - first cluster of chain
 * count    - number of clusters in chain
 * fillwith - what to write into FAT entry of last cluster
 */
static int
fatchain(struct msdosfsmount *pmp, u_long start, u_long count, u_long fillwith)
{
	int error;
	u_long bn, bo, bsize, byteoffset, readcn, newc;
	struct buf *bp;

#ifdef MSDOSFS_DEBUG
	printf("fatchain(pmp %p, start %lu, count %lu, fillwith %lx)\n",
	    pmp, start, count, fillwith);
#endif
	/*
	 * Be sure the clusters are in the filesystem.
	 */
	if (start < CLUST_FIRST || start + count - 1 > pmp->pm_maxcluster)
		return (EINVAL);

	while (count > 0) {
		byteoffset = FATOFS(pmp, start);
		fatblock(pmp, byteoffset, &bn, &bsize, &bo);
		error = bread(pmp->pm_devvp, bn, bsize, NOCRED, &bp);
		if (error) {
			brelse(bp);
			return (error);
		}
		while (count > 0) {
			start++;
			newc = --count > 0 ? start : fillwith;
			switch (pmp->pm_fatmask) {
			case FAT12_MASK:
				readcn = getushort(&bp->b_data[bo]);
				if (start & 1) {
					readcn &= 0xf000;
					readcn |= newc & 0xfff;
				} else {
					readcn &= 0x000f;
					readcn |= newc << 4;
				}
				putushort(&bp->b_data[bo], readcn);
				bo++;
				if (!(start & 1))
					bo++;
				break;
			case FAT16_MASK:
				putushort(&bp->b_data[bo], newc);
				bo += 2;
				break;
			case FAT32_MASK:
				readcn = getulong(&bp->b_data[bo]);
				readcn &= ~pmp->pm_fatmask;
				readcn |= newc & pmp->pm_fatmask;
				putulong(&bp->b_data[bo], readcn);
				bo += 4;
				break;
			}
			if (bo >= bsize)
				break;
		}
		updatefats(pmp, bp, bn);
	}
	pmp->pm_fmod = 1;
	return (0);
}

/*
 * Check the length of a free cluster chain starting at start.
 *
 * pmp	 - mount point
 * start - start of chain
 * count - maximum interesting length
 */
static int
chainlength(struct msdosfsmount *pmp, u_long start, u_long count)
{
	u_long idx, max_idx;
	u_int map;
	u_long len;

	MSDOSFS_ASSERT_MP_LOCKED(pmp);

	if (start > pmp->pm_maxcluster)
		return (0);
	max_idx = pmp->pm_maxcluster / N_INUSEBITS;
	idx = start / N_INUSEBITS;
	start %= N_INUSEBITS;
	map = pmp->pm_inusemap[idx];
	map &= ~((1 << start) - 1);
	if (map) {
		len = ffs(map) - 1 - start;
		len = MIN(len, count);
		if (start + len > pmp->pm_maxcluster)
			len = pmp->pm_maxcluster - start + 1;
		return (len);
	}
	len = N_INUSEBITS - start;
	if (len >= count) {
		len = count;
		if (start + len > pmp->pm_maxcluster)
			len = pmp->pm_maxcluster - start + 1;
		return (len);
	}
	while (++idx <= max_idx) {
		if (len >= count)
			break;
		map = pmp->pm_inusemap[idx];
		if (map) {
			len += ffs(map) - 1;
			break;
		}
		len += N_INUSEBITS;
	}
	len = MIN(len, count);
	if (start + len > pmp->pm_maxcluster)
		len = pmp->pm_maxcluster - start + 1;
	return (len);
}

/*
 * Allocate contigous free clusters.
 *
 * pmp	      - mount point.
 * start      - start of cluster chain.
 * count      - number of clusters to allocate.
 * fillwith   - put this value into the FAT entry for the
 *		last allocated cluster.
 * retcluster - put the first allocated cluster's number here.
 * got	      - how many clusters were actually allocated.
 */
static int
chainalloc(struct msdosfsmount *pmp, u_long start, u_long count,
    u_long fillwith, u_long *retcluster, u_long *got)
{
	int error;
	u_long cl, n;

	MSDOSFS_ASSERT_MP_LOCKED(pmp);
	KASSERT((pmp->pm_flags & MSDOSFSMNT_RONLY) == 0,
	    ("chainalloc on ro msdosfs mount"));

	for (cl = start, n = count; n-- > 0;)
		usemap_alloc(pmp, cl++);
	pmp->pm_nxtfree = start + count;
	if (pmp->pm_nxtfree > pmp->pm_maxcluster)
		pmp->pm_nxtfree = CLUST_FIRST;
	pmp->pm_flags |= MSDOSFS_FSIMOD;
	error = fatchain(pmp, start, count, fillwith);
	if (error != 0) {
		for (cl = start, n = count; n-- > 0;)
			usemap_free(pmp, cl++);
		return (error);
	}
#ifdef MSDOSFS_DEBUG
	printf("clusteralloc(): allocated cluster chain at %lu (%lu clusters)\n",
	    start, count);
#endif
	if (retcluster)
		*retcluster = start;
	if (got)
		*got = count;
	return (0);
}

/*
 * Allocate contiguous free clusters.
 *
 * pmp	      - mount point.
 * start      - preferred start of cluster chain.
 * count      - number of clusters requested.
 * fillwith   - put this value into the FAT entry for the
 *		last allocated cluster.
 * retcluster - put the first allocated cluster's number here.
 * got	      - how many clusters were actually allocated.
 */
int
clusteralloc(struct msdosfsmount *pmp, u_long start, u_long count,
    u_long fillwith, u_long *retcluster, u_long *got)
{
	int error;

	MSDOSFS_LOCK_MP(pmp);
	error = clusteralloc1(pmp, start, count, fillwith, retcluster, got);
	MSDOSFS_UNLOCK_MP(pmp);
	return (error);
}

static int
clusteralloc1(struct msdosfsmount *pmp, u_long start, u_long count,
    u_long fillwith, u_long *retcluster, u_long *got)
{
	u_long idx;
	u_long len, newst, foundl, cn, l;
	u_long foundcn = 0; /* XXX: foundcn could be used unititialized */
	u_int map;

	MSDOSFS_ASSERT_MP_LOCKED(pmp);

#ifdef MSDOSFS_DEBUG
	printf("clusteralloc(): find %lu clusters\n", count);
#endif
	if (start) {
		if ((len = chainlength(pmp, start, count)) >= count)
			return (chainalloc(pmp, start, count, fillwith, retcluster, got));
	} else
		len = 0;

	newst = pmp->pm_nxtfree;
	foundl = 0;

	for (cn = newst; cn <= pmp->pm_maxcluster;) {
		idx = cn / N_INUSEBITS;
		map = pmp->pm_inusemap[idx];
		map |= (1U << (cn % N_INUSEBITS)) - 1;
		if (map != FULL_RUN) {
			cn = idx * N_INUSEBITS + ffs(map ^ FULL_RUN) - 1;
			if ((l = chainlength(pmp, cn, count)) >= count)
				return (chainalloc(pmp, cn, count, fillwith, retcluster, got));
			if (l > foundl) {
				foundcn = cn;
				foundl = l;
			}
			cn += l + 1;
			continue;
		}
		cn += N_INUSEBITS - cn % N_INUSEBITS;
	}
	for (cn = 0; cn < newst;) {
		idx = cn / N_INUSEBITS;
		map = pmp->pm_inusemap[idx];
		map |= (1U << (cn % N_INUSEBITS)) - 1;
		if (map != FULL_RUN) {
			cn = idx * N_INUSEBITS + ffs(map ^ FULL_RUN) - 1;
			if ((l = chainlength(pmp, cn, count)) >= count)
				return (chainalloc(pmp, cn, count, fillwith, retcluster, got));
			if (l > foundl) {
				foundcn = cn;
				foundl = l;
			}
			cn += l + 1;
			continue;
		}
		cn += N_INUSEBITS - cn % N_INUSEBITS;
	}

	if (!foundl)
		return (ENOSPC);

	if (len)
		return (chainalloc(pmp, start, len, fillwith, retcluster, got));
	else
		return (chainalloc(pmp, foundcn, foundl, fillwith, retcluster, got));
}


/*
 * Free a chain of clusters.
 *
 * pmp		- address of the msdosfs mount structure for the filesystem
 *		  containing the cluster chain to be freed.
 * startcluster - number of the 1st cluster in the chain of clusters to be
 *		  freed.
 */
int
freeclusterchain(struct msdosfsmount *pmp, u_long cluster)
{
	int error;
	struct buf *bp = NULL;
	u_long bn, bo, bsize, byteoffset;
	u_long readcn, lbn = -1;

	MSDOSFS_LOCK_MP(pmp);
	while (cluster >= CLUST_FIRST && cluster <= pmp->pm_maxcluster) {
		byteoffset = FATOFS(pmp, cluster);
		fatblock(pmp, byteoffset, &bn, &bsize, &bo);
		if (lbn != bn) {
			if (bp)
				updatefats(pmp, bp, lbn);
			error = bread(pmp->pm_devvp, bn, bsize, NOCRED, &bp);
			if (error) {
				brelse(bp);
				MSDOSFS_UNLOCK_MP(pmp);
				return (error);
			}
			lbn = bn;
		}
		usemap_free(pmp, cluster);
		switch (pmp->pm_fatmask) {
		case FAT12_MASK:
			readcn = getushort(&bp->b_data[bo]);
			if (cluster & 1) {
				cluster = readcn >> 4;
				readcn &= 0x000f;
				readcn |= MSDOSFSFREE << 4;
			} else {
				cluster = readcn;
				readcn &= 0xf000;
				readcn |= MSDOSFSFREE & 0xfff;
			}
			putushort(&bp->b_data[bo], readcn);
			break;
		case FAT16_MASK:
			cluster = getushort(&bp->b_data[bo]);
			putushort(&bp->b_data[bo], MSDOSFSFREE);
			break;
		case FAT32_MASK:
			cluster = getulong(&bp->b_data[bo]);
			putulong(&bp->b_data[bo],
				 (MSDOSFSFREE & FAT32_MASK) | (cluster & ~FAT32_MASK));
			break;
		}
		cluster &= pmp->pm_fatmask;
		if ((cluster | ~pmp->pm_fatmask) >= CLUST_RSRVD)
			cluster |= pmp->pm_fatmask;
	}
	if (bp)
		updatefats(pmp, bp, bn);
	MSDOSFS_UNLOCK_MP(pmp);
	return (0);
}

/*
 * Read in FAT blocks looking for free clusters. For every free cluster
 * found turn off its corresponding bit in the pm_inusemap.
 */
int
fillinusemap(struct msdosfsmount *pmp)
{
	struct buf *bp;
	u_long bn, bo, bsize, byteoffset, cn, readcn;
	int error;

	MSDOSFS_ASSERT_MP_LOCKED(pmp);
	bp = NULL;

	/*
	 * Mark all clusters in use, we mark the free ones in the FAT scan
	 * loop further down.
	 */
	for (cn = 0; cn < (pmp->pm_maxcluster + N_INUSEBITS) / N_INUSEBITS; cn++)
		pmp->pm_inusemap[cn] = FULL_RUN;

	/*
	 * Figure how many free clusters are in the filesystem by ripping
	 * through the FAT counting the number of entries whose content is
	 * zero.  These represent free clusters.
	 */
	pmp->pm_freeclustercount = 0;
	for (cn = 0; cn <= pmp->pm_maxcluster; cn++) {
		byteoffset = FATOFS(pmp, cn);
		bo = byteoffset % pmp->pm_fatblocksize;
		if (bo == 0) {
			/* Read new FAT block */
			if (bp != NULL)
				brelse(bp);
			fatblock(pmp, byteoffset, &bn, &bsize, NULL);
			error = bread(pmp->pm_devvp, bn, bsize, NOCRED, &bp);
			if (error != 0)
				return (error);
		}
		if (FAT32(pmp))
			readcn = getulong(&bp->b_data[bo]);
		else
			readcn = getushort(&bp->b_data[bo]);
		if (FAT12(pmp) && (cn & 1))
			readcn >>= 4;
		readcn &= pmp->pm_fatmask;

		/*
		 * Check if the FAT ID matches the BPB's media descriptor and
		 * all other bits are set to 1.
		 */
		if (cn == 0 && readcn != ((pmp->pm_fatmask & 0xffffff00) |
		    pmp->pm_bpb.bpbMedia)) {
#ifdef MSDOSFS_DEBUG
			printf("mountmsdosfs(): Media descriptor in BPB"
			    "does not match FAT ID\n");
#endif
			brelse(bp);
			return (EINVAL);
		} else if (readcn == CLUST_FREE)
			usemap_free(pmp, cn);
	}
	if (bp != NULL)
		brelse(bp);

	for (cn = pmp->pm_maxcluster + 1; cn < (pmp->pm_maxcluster +
	    N_INUSEBITS) / N_INUSEBITS; cn++)
		pmp->pm_inusemap[cn / N_INUSEBITS] |= 1U << (cn % N_INUSEBITS);

	return (0);
}

/*
 * Allocate a new cluster and chain it onto the end of the file.
 *
 * dep	 - the file to extend
 * count - number of clusters to allocate
 * bpp	 - where to return the address of the buf header for the first new
 *	   file block
 * ncp	 - where to put cluster number of the first newly allocated cluster
 *	   If this pointer is 0, do not return the cluster number.
 * flags - see fat.h
 *
 * NOTE: This function is not responsible for turning on the DE_UPDATE bit of
 * the de_flag field of the denode and it does not change the de_FileSize
 * field.  This is left for the caller to do.
 */
int
extendfile(struct denode *dep, u_long count, struct buf **bpp, u_long *ncp,
    int flags)
{
	int error;
	u_long frcn;
	u_long cn, got;
	struct msdosfsmount *pmp = dep->de_pmp;
	struct buf *bp;
	daddr_t blkno;

	/*
	 * Don't try to extend the root directory
	 */
	if (dep->de_StartCluster == MSDOSFSROOT
	    && (dep->de_Attributes & ATTR_DIRECTORY)) {
#ifdef MSDOSFS_DEBUG
		printf("extendfile(): attempt to extend root directory\n");
#endif
		return (ENOSPC);
	}

	/*
	 * If the "file's last cluster" cache entry is empty, and the file
	 * is not empty, then fill the cache entry by calling pcbmap().
	 */
	if (dep->de_fc[FC_LASTFC].fc_frcn == FCE_EMPTY &&
	    dep->de_StartCluster != 0) {
		error = pcbmap(dep, 0xffff, 0, &cn, 0);
		/* we expect it to return E2BIG */
		if (error != E2BIG)
			return (error);
	}

	dep->de_fc[FC_NEXTTOLASTFC].fc_frcn =
	    dep->de_fc[FC_LASTFC].fc_frcn;
	dep->de_fc[FC_NEXTTOLASTFC].fc_fsrcn =
	    dep->de_fc[FC_LASTFC].fc_fsrcn;
	while (count > 0) {
		/*
		 * Allocate a new cluster chain and cat onto the end of the
		 * file.  If the file is empty we make de_StartCluster point
		 * to the new block.  Note that de_StartCluster being 0 is
		 * sufficient to be sure the file is empty since we exclude
		 * attempts to extend the root directory above, and the root
		 * dir is the only file with a startcluster of 0 that has
		 * blocks allocated (sort of).
		 */
		if (dep->de_StartCluster == 0)
			cn = 0;
		else
			cn = dep->de_fc[FC_LASTFC].fc_fsrcn + 1;
		error = clusteralloc(pmp, cn, count, CLUST_EOFE, &cn, &got);
		if (error)
			return (error);

		count -= got;

		/*
		 * Give them the filesystem relative cluster number if they want
		 * it.
		 */
		if (ncp) {
			*ncp = cn;
			ncp = NULL;
		}

		if (dep->de_StartCluster == 0) {
			dep->de_StartCluster = cn;
			frcn = 0;
		} else {
			error = fatentry(FAT_SET, pmp,
					 dep->de_fc[FC_LASTFC].fc_fsrcn,
					 0, cn);
			if (error) {
				clusterfree(pmp, cn, NULL);
				return (error);
			}
			frcn = dep->de_fc[FC_LASTFC].fc_frcn + 1;
		}

		/*
		 * Update the "last cluster of the file" entry in the
		 * denode's FAT cache.
		 */
		fc_setcache(dep, FC_LASTFC, frcn + got - 1, cn + got - 1);

		if (flags & DE_CLEAR) {
			while (got-- > 0) {
				/*
				 * Get the buf header for the new block of the file.
				 */
				if (dep->de_Attributes & ATTR_DIRECTORY)
					bp = getblk(pmp->pm_devvp,
					    cntobn(pmp, cn++),
					    pmp->pm_bpcluster, 0, 0, 0);
				else {
					bp = getblk(DETOV(dep),
					    frcn++,
					    pmp->pm_bpcluster, 0, 0, 0);
					/*
					 * Do the bmap now, as in msdosfs_write
					 */
					if (pcbmap(dep,
					    bp->b_lblkno,
					    &blkno, 0, 0))
						bp->b_blkno = -1;
					if (bp->b_blkno == -1)
						panic("extendfile: pcbmap");
					else
						bp->b_blkno = blkno;
				}
				clrbuf(bp);
				if (bpp) {
					*bpp = bp;
					bpp = NULL;
				} else
					bdwrite(bp);
			}
		}
	}

	return (0);
}

/*-
 * Routine to mark a FAT16 or FAT32 volume as "clean" or "dirty" by
 * manipulating the upper bit of the FAT entry for cluster 1.  Note that
 * this bit is not defined for FAT12 volumes, which are always assumed to
 * be clean.
 *
 * The fatentry() routine only works on cluster numbers that a file could
 * occupy, so it won't manipulate the entry for cluster 1.  So we have to do
 * it here.  The code was stolen from fatentry() and tailored for cluster 1.
 *
 * Inputs:
 *	pmp	The MS-DOS volume to mark
 *	dirty	Non-zero if the volume should be marked dirty; zero if it
 *		should be marked clean
 *
 * Result:
 *	0	Success
 *	EROFS	Volume is read-only
 *	?	(other errors from called routines)
 */
int
markvoldirty(struct msdosfsmount *pmp, int dirty)
{
	struct buf *bp;
	u_long bn, bo, bsize, byteoffset, fatval;
	int error;

	/*
	 * FAT12 does not support a "clean" bit, so don't do anything for
	 * FAT12.
	 */
	if (FAT12(pmp))
		return (0);

	/* Can't change the bit on a read-only filesystem. */
	if (pmp->pm_flags & MSDOSFSMNT_RONLY)
		return (EROFS);

	/*
	 * Fetch the block containing the FAT entry.  It is given by the
	 * pseudo-cluster 1.
	 */
	byteoffset = FATOFS(pmp, 1);
	fatblock(pmp, byteoffset, &bn, &bsize, &bo);
	error = bread(pmp->pm_devvp, bn, bsize, NOCRED, &bp);
	if (error) {
		brelse(bp);
		return (error);
	}

	/*
	 * Get the current value of the FAT entry and set/clear the relevant
	 * bit.  Dirty means clear the "clean" bit; clean means set the
	 * "clean" bit.
	 */
	if (FAT32(pmp)) {
		/* FAT32 uses bit 27. */
		fatval = getulong(&bp->b_data[bo]);
		if (dirty)
			fatval &= 0xF7FFFFFF;
		else
			fatval |= 0x08000000;
		putulong(&bp->b_data[bo], fatval);
	} else {
		/* Must be FAT16; use bit 15. */
		fatval = getushort(&bp->b_data[bo]);
		if (dirty)
			fatval &= 0x7FFF;
		else
			fatval |= 0x8000;
		putushort(&bp->b_data[bo], fatval);
	}

	/* Write out the modified FAT block synchronously. */
	return (bwrite(bp));
}
