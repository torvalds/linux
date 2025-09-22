/*	$OpenBSD: msdosfs_fat.c,v 1.36 2023/06/16 08:42:08 sf Exp $	*/
/*	$NetBSD: msdosfs_fat.c,v 1.26 1997/10/17 11:24:02 ws Exp $	*/

/*-
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
/*
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

/*
 * kernel include files.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/namei.h>
#include <sys/mount.h>		/* to define statfs structure */
#include <sys/vnode.h>		/* to define vattr structure */
#include <sys/lock.h>
#include <sys/errno.h>
#include <sys/dirent.h>

/*
 * msdosfs include files.
 */
#include <msdosfs/bpb.h>
#include <msdosfs/msdosfsmount.h>
#include <msdosfs/direntry.h>
#include <msdosfs/denode.h>
#include <msdosfs/fat.h>

/*
 * Fat cache stats.
 */
int fc_fileextends;		/* # of file extends			 */
int fc_lfcempty;		/* # of time last file cluster cache entry
				 * was empty */
int fc_bmapcalls;		/* # of times pcbmap was called		 */

#define	LMMAX	20
int fc_lmdistance[LMMAX];	/* counters for how far off the last
				 * cluster mapped entry was. */
int fc_largedistance;		/* off by more than LMMAX		 */

static void fatblock(struct msdosfsmount *, uint32_t, uint32_t *, uint32_t *,
			  uint32_t *);
void updatefats(struct msdosfsmount *, struct buf *, uint32_t);
static __inline void usemap_free(struct msdosfsmount *, uint32_t);
static __inline void usemap_alloc(struct msdosfsmount *, uint32_t);
static int fatchain(struct msdosfsmount *, uint32_t, uint32_t, uint32_t);
int chainlength(struct msdosfsmount *, uint32_t, uint32_t);
int chainalloc(struct msdosfsmount *, uint32_t, uint32_t, uint32_t, uint32_t *,
		    uint32_t *);

static void
fatblock(struct msdosfsmount *pmp, uint32_t ofs, uint32_t *bnp, uint32_t *sizep,
    uint32_t *bop)
{
	uint32_t bn, size;

	bn = ofs / pmp->pm_fatblocksize * pmp->pm_fatblocksec;
	size = min(pmp->pm_fatblocksec, pmp->pm_FATsecs - bn) * DEV_BSIZE;
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
 * bnp	  - address of where to place the file system relative block number.
 *	    If this pointer is null then don't return this quantity.
 * cnp	  - address of where to place the file system relative cluster number.
 *	    If this pointer is null then don't return this quantity.
 * sp	  - address of where to place the block size for the file/dir
 *	    If this pointer is null then don't return this quantity.
 *
 * NOTE: Either bnp or cnp must be non-null.
 * This function has one side effect.  If the requested file relative cluster
 * is beyond the end of file, then the actual number of clusters in the file
 * is returned in *cnp.  This is useful for determining how long a directory is.
 *  If cnp is null, nothing is returned.
 */
int
pcbmap(struct denode *dep, uint32_t findcn, daddr_t *bnp, uint32_t *cnp,
    int *sp)
{
	int error;
	uint32_t i;
	uint32_t cn;
	uint32_t prevcn = 0; /* XXX: prevcn could be used uninitialized */
	uint32_t byteoffset;
	uint32_t bn;
	uint32_t bo;
	struct buf *bp = NULL;
	uint32_t bp_bn = -1;
	struct msdosfsmount *pmp = dep->de_pmp;
	uint32_t bsize;

	fc_bmapcalls++;

	/*
	 * If they don't give us someplace to return a value then don't
	 * bother doing anything.
	 */
	if (bnp == NULL && cnp == NULL && sp == NULL)
		return (0);

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
	 * Rummage around in the fat cache, maybe we can avoid tromping
	 * thru every fat entry for the file. And, keep track of how far
	 * off the cache was from where we wanted to be.
	 */
	i = 0;
	fc_lookup(dep, findcn, &i, &cn);
	if ((bn = findcn - i) >= LMMAX)
		fc_largedistance++;
	else
		fc_lmdistance[bn]++;

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
			error = bread(pmp->pm_devvp, bn, bsize, &bp);
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

hiteof:
	if (cnp)
		*cnp = i;
	if (bp)
		brelse(bp);
	/* update last file cluster entry in the fat cache */
	fc_setcache(dep, FC_LASTFC, i - 1, prevcn);
	return (E2BIG);
}

/*
 * Find the closest entry in the fat cache to the cluster we are looking
 * for.
 */
void
fc_lookup(struct denode *dep, uint32_t findcn, uint32_t *frcnp, uint32_t *fsrcnp)
{
	int i;
	uint32_t cn;
	struct fatcache *closest = 0;

	for (i = 0; i < FC_SIZE; i++) {
		cn = dep->de_fc[i].fc_frcn;
		if (cn != FCE_EMPTY && cn <= findcn) {
			if (closest == 0 || cn > closest->fc_frcn)
				closest = &dep->de_fc[i];
		}
	}
	if (closest) {
		*frcnp = closest->fc_frcn;
		*fsrcnp = closest->fc_fsrcn;
	}
}

/*
 * Purge the fat cache in denode dep of all entries relating to file
 * relative cluster frcn and beyond.
 */
void
fc_purge(struct denode *dep, u_int frcn)
{
	int i;
	struct fatcache *fcp;

	fcp = dep->de_fc;
	for (i = 0; i < FC_SIZE; i++, fcp++) {
		if (fcp->fc_frcn >= frcn)
			fcp->fc_frcn = FCE_EMPTY;
	}
}

/*
 * Update the fat.
 * If mirroring the fat, update all copies, with the first copy as last.
 * Else update only the current fat (ignoring the others).
 *
 * pmp	 - msdosfsmount structure for filesystem to update
 * bp	 - addr of modified fat block
 * fatbn - block number relative to begin of filesystem of the modified fat block.
 */
void
updatefats(struct msdosfsmount *pmp, struct buf *bp, uint32_t fatbn)
{
	int i;
	struct buf *bpn;

#ifdef MSDOSFS_DEBUG
	printf("updatefats(pmp %p, buf %p, fatbn %d)\n", pmp, bp, fatbn);
#endif

	/*
	 * If we have an FSInfo block, update it.
	 */
	if (pmp->pm_fsinfo) {
		if (bread(pmp->pm_devvp, pmp->pm_fsinfo, fsi_size(pmp),
		    &bpn) != 0) {
			/*
			 * Ignore the error, but turn off FSInfo update for the future.
			 */
			pmp->pm_fsinfo = 0;
			brelse(bpn);
		} else {
			struct fsinfo *fp = (struct fsinfo *)bpn->b_data;

			putulong(fp->fsinfree, pmp->pm_freeclustercount);
			if (pmp->pm_flags & MSDOSFSMNT_WAITONFAT)
				bwrite(bpn);
			else
				bdwrite(bpn);
		}
	}

	if (pmp->pm_flags & MSDOSFS_FATMIRROR) {
		/*
		 * Now copy the block(s) of the modified fat to the other copies of
		 * the fat and write them out.  This is faster than reading in the
		 * other fats and then writing them back out.  This could tie up
		 * the fat for quite a while. Preventing others from accessing it.
		 * To prevent us from going after the fat quite so much we use
		 * delayed writes, unless they specified "synchronous" when the
		 * filesystem was mounted.  If synch is asked for then use
		 * bwrite()'s and really slow things down.
		 */
		for (i = 1; i < pmp->pm_FATs; i++) {
			fatbn += pmp->pm_FATsecs;
			/* getblk() never fails */
			bpn = getblk(pmp->pm_devvp, fatbn, bp->b_bcount, 0,
			    INFSLP);
			bcopy(bp->b_data, bpn->b_data, bp->b_bcount);
			if (pmp->pm_flags & MSDOSFSMNT_WAITONFAT)
				bwrite(bpn);
			else
				bdwrite(bpn);
		}
	}

	/*
	 * Write out the first (or current) fat last.
	 */
	if (pmp->pm_flags & MSDOSFSMNT_WAITONFAT)
		bwrite(bp);
	else
		bdwrite(bp);
	/*
	 * Maybe update fsinfo sector here?
	 */
}

/*
 * Updating entries in 12 bit fats is a pain in the butt.
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
 *	cluster n	   cluster n+1
 *
 * Where n is even. m = n + (n >> 2)
 *
 */
static __inline void
usemap_alloc(struct msdosfsmount *pmp, uint32_t cn)
{
	KASSERT(cn <= pmp->pm_maxcluster);

	pmp->pm_inusemap[cn / N_INUSEBITS] |= 1U << (cn % N_INUSEBITS);
	pmp->pm_freeclustercount--;
}

static __inline void
usemap_free(struct msdosfsmount *pmp, uint32_t cn)
{
	KASSERT(cn <= pmp->pm_maxcluster);

	pmp->pm_freeclustercount++;
	pmp->pm_inusemap[cn / N_INUSEBITS] &= ~(1U << (cn % N_INUSEBITS));
}

int
clusterfree(struct msdosfsmount *pmp, uint32_t cluster, uint32_t *oldcnp)
{
	int error;
	uint32_t oldcn;

	usemap_free(pmp, cluster);
	error = fatentry(FAT_GET_AND_SET, pmp, cluster, &oldcn, MSDOSFSFREE);
	if (error) {
		usemap_alloc(pmp, cluster);
		return (error);
	}
	/*
	 * If the cluster was successfully marked free, then update
	 * the count of free clusters, and turn off the "allocated"
	 * bit in the "in use" cluster bit map.
	 */
	if (oldcnp)
		*oldcnp = oldcn;
	return (0);
}

/*
 * Get or Set or 'Get and Set' the cluster'th entry in the fat.
 *
 * function	- whether to get or set a fat entry
 * pmp		- address of the msdosfsmount structure for the filesystem
 *		  whose fat is to be manipulated.
 * cn		- which cluster is of interest
 * oldcontents	- address of a word that is to receive the contents of the
 *		  cluster'th entry if this is a get function
 * newcontents	- the new value to be written into the cluster'th element of
 *		  the fat if this is a set function.
 *
 * This function can also be used to free a cluster by setting the fat entry
 * for a cluster to 0.
 *
 * All copies of the fat are updated if this is a set function. NOTE: If
 * fatentry() marks a cluster as free it does not update the inusemap in
 * the msdosfsmount structure. This is left to the caller.
 */
int
fatentry(int function, struct msdosfsmount *pmp, uint32_t cn, uint32_t *oldcontents,
    uint32_t newcontents)
{
	int error;
	uint32_t readcn;
	uint32_t bn, bo, bsize, byteoffset;
	struct buf *bp;

#ifdef MSDOSFS_DEBUG
	 printf("fatentry(func %d, pmp %p, clust %d, oldcon %p, "
	     "newcon %d)\n", function, pmp, cn, oldcontents, newcontents);
#endif

#ifdef DIAGNOSTIC
	/*
	 * Be sure they asked us to do something.
	 */
	if ((function & (FAT_SET | FAT_GET)) == 0) {
		printf("fatentry(): function code doesn't specify get or set\n");
		return (EINVAL);
	}

	/*
	 * If they asked us to return a cluster number but didn't tell us
	 * where to put it, give them an error.
	 */
	if ((function & FAT_GET) && oldcontents == NULL) {
		printf("fatentry(): get function with no place to put result\n");
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
	if ((error = bread(pmp->pm_devvp, bn, bsize, &bp)) != 0) {
		brelse(bp);
		return (error);
	}

	if (function & FAT_GET) {
		if (FAT32(pmp))
			readcn = getulong(&bp->b_data[bo]);
		else
			readcn = getushort(&bp->b_data[bo]);
		if (FAT12(pmp) && (cn & 1))
			readcn >>= 4;
		readcn &= pmp->pm_fatmask;
		/* map reserved fat entries to same values for all fats */
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
			 * high order bits of the fat entry.
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
 * fillwith - what to write into fat entry of last cluster
 */
static int
fatchain(struct msdosfsmount *pmp, uint32_t start, uint32_t count, uint32_t fillwith)
{
	int error;
	uint32_t bn, bo, bsize, byteoffset, readcn, newc;
	struct buf *bp;

#ifdef MSDOSFS_DEBUG
	printf("fatchain(pmp %p, start %d, count %d, fillwith %d)\n",
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
		error = bread(pmp->pm_devvp, bn, bsize, &bp);
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
int
chainlength(struct msdosfsmount *pmp, uint32_t start, uint32_t count)
{
	uint32_t idx, max_idx;
	u_int map;
	uint32_t len;

	if (start > pmp->pm_maxcluster)
	    return (0);
	max_idx = pmp->pm_maxcluster / N_INUSEBITS;
	idx = start / N_INUSEBITS;
	start %= N_INUSEBITS;
	map = pmp->pm_inusemap[idx];
	map &= ~((1U << start) - 1);
	if (map) {
		len = ffs(map) - 1 - start;
		len = MIN(len, count);
		len = MIN(len, pmp->pm_maxcluster - start + 1);
		return (len);
	}
	len = N_INUSEBITS - start;
	if (len >= count) {
		len = MIN(count, pmp->pm_maxcluster - start + 1);
		return (len);
	}
	while (++idx <= max_idx) {
		if (len >= count)
			break;
		if ((map = pmp->pm_inusemap[idx]) != 0) {
			len +=  ffs(map) - 1;
			break;
		}
		len += N_INUSEBITS;
	}
	len = MIN(len, count);
	len = MIN(len, pmp->pm_maxcluster - start + 1);
	return (len);
}

/*
 * Allocate contiguous free clusters.
 *
 * pmp	      - mount point.
 * start      - start of cluster chain.
 * count      - number of clusters to allocate.
 * fillwith   - put this value into the fat entry for the
 *		last allocated cluster.
 * retcluster - put the first allocated cluster's number here.
 * got	      - how many clusters were actually allocated.
 */
int
chainalloc(struct msdosfsmount *pmp, uint32_t start, uint32_t count,
    uint32_t fillwith, uint32_t *retcluster, uint32_t *got)
{
	int error;
	uint32_t cl, n;

	for (cl = start, n = count; n-- > 0;)
		usemap_alloc(pmp, cl++);
	if ((error = fatchain(pmp, start, count, fillwith)) != 0)
		return (error);
#ifdef MSDOSFS_DEBUG
	printf("clusteralloc(): allocated cluster chain at %d (%d clusters)\n",
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
 * fillwith   - put this value into the fat entry for the
 *		last allocated cluster.
 * retcluster - put the first allocated cluster's number here.
 * got	      - how many clusters were actually allocated.
 */
int
clusteralloc(struct msdosfsmount *pmp, uint32_t start, uint32_t count,
    uint32_t *retcluster, uint32_t *got)
{
	uint32_t idx;
	uint32_t len, newst, foundl, cn, l;
	uint32_t foundcn = 0; /* XXX: foundcn could be used uninitialized */
	uint32_t fillwith = CLUST_EOFE;
	u_int map;

#ifdef MSDOSFS_DEBUG
	printf("clusteralloc(): find %d clusters\n",count);
#endif
	if (start) {
		if ((len = chainlength(pmp, start, count)) >= count)
			return (chainalloc(pmp, start, count, fillwith, retcluster, got));
	} else {
		/*
		 * This is a new file, initialize start
		 */
		struct timeval tv;

		microtime(&tv);
		start = (tv.tv_usec >> 10) | tv.tv_usec;
		len = 0;
	}

	/*
	 * Start at a (pseudo) random place to maximize cluster runs
	 * under multiple writers.
	 */
	newst = (start * 1103515245 + 12345) % (pmp->pm_maxcluster + 1);
	foundl = 0;

	for (cn = newst; cn <= pmp->pm_maxcluster;) {
		idx = cn / N_INUSEBITS;
		map = pmp->pm_inusemap[idx];
		map |= (1U << (cn % N_INUSEBITS)) - 1;
		if (map != (u_int)-1) {
			cn = idx * N_INUSEBITS + ffs(map^(u_int)-1) - 1;
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
		if (map != (u_int)-1) {
			cn = idx * N_INUSEBITS + ffs(map^(u_int)-1) - 1;
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
freeclusterchain(struct msdosfsmount *pmp, uint32_t cluster)
{
	int error;
	struct buf *bp = NULL;
	uint32_t bn, bo, bsize, byteoffset;
	uint32_t readcn, lbn = -1;

	while (cluster >= CLUST_FIRST && cluster <= pmp->pm_maxcluster) {
		byteoffset = FATOFS(pmp, cluster);
		fatblock(pmp, byteoffset, &bn, &bsize, &bo);
		if (lbn != bn) {
			if (bp)
				updatefats(pmp, bp, lbn);
			error = bread(pmp->pm_devvp, bn, bsize, &bp);
			if (error) {
				brelse(bp);
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
	return (0);
}

/*
 * Read in fat blocks looking for free clusters. For every free cluster
 * found turn off its corresponding bit in the pm_inusemap.
 */
int
fillinusemap(struct msdosfsmount *pmp)
{
	struct buf *bp = NULL;
	uint32_t cn, readcn;
	int error;
	uint32_t bn, bo, bsize, byteoffset;

	/*
	 * Mark all clusters in use, we mark the free ones in the fat scan
	 * loop further down.
	 */
	for (cn = 0; cn < howmany(pmp->pm_maxcluster + 1, N_INUSEBITS); cn++)
		pmp->pm_inusemap[cn] = (u_int)-1;

	/*
	 * Figure how many free clusters are in the filesystem by ripping
	 * through the fat counting the number of entries whose content is
	 * zero.  These represent free clusters.
	 */
	pmp->pm_freeclustercount = 0;
	for (cn = CLUST_FIRST; cn <= pmp->pm_maxcluster; cn++) {
		byteoffset = FATOFS(pmp, cn);
		bo = byteoffset % pmp->pm_fatblocksize;
		if (!bo || !bp) {
			/* Read new FAT block */
			if (bp)
				brelse(bp);
			fatblock(pmp, byteoffset, &bn, &bsize, NULL);
			error = bread(pmp->pm_devvp, bn, bsize, &bp);
			if (error) {
				brelse(bp);
				return (error);
			}
		}
		if (FAT32(pmp))
			readcn = getulong(&bp->b_data[bo]);
		else
			readcn = getushort(&bp->b_data[bo]);
		if (FAT12(pmp) && (cn & 1))
			readcn >>= 4;
		readcn &= pmp->pm_fatmask;

		if (readcn == 0)
			usemap_free(pmp, cn);
	}
	brelse(bp);
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
extendfile(struct denode *dep, uint32_t count, struct buf **bpp, uint32_t *ncp,
    int flags)
{
	int error;
	uint32_t frcn;
	uint32_t cn, got;
	struct msdosfsmount *pmp = dep->de_pmp;
	struct buf *bp;

	/*
	 * Don't try to extend the root directory
	 */
	if (dep->de_StartCluster == MSDOSFSROOT
	    && (dep->de_Attributes & ATTR_DIRECTORY)) {
		printf("extendfile(): attempt to extend root directory\n");
		return (ENOSPC);
	}

	/*
	 * If the "file's last cluster" cache entry is empty, and the file
	 * is not empty, then fill the cache entry by calling pcbmap().
	 */
	fc_fileextends++;
	if (dep->de_fc[FC_LASTFC].fc_frcn == FCE_EMPTY &&
	    dep->de_StartCluster != 0) {
		fc_lfcempty++;
		error = pcbmap(dep, CLUST_END, 0, &cn, 0);
		/* we expect it to return E2BIG */
		if (error != E2BIG)
			return (error);
	}

	/*
	 * Preserve value for the last cluster before extending the file
	 * to speed up further lookups.
	 */
	fc_setcache(dep, FC_OLASTFC, dep->de_fc[FC_LASTFC].fc_frcn,
	    dep->de_fc[FC_LASTFC].fc_fsrcn);

	while (count > 0) {
		/*
		 * Allocate a new cluster chain and cat onto the end of the
		 * file.  * If the file is empty we make de_StartCluster point
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
		error = clusteralloc(pmp, cn, count, &cn, &got);
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
		 * Update the "last cluster of the file" entry in the denode's fat
		 * cache.
		 */
		fc_setcache(dep, FC_LASTFC, frcn + got - 1, cn + got - 1);

		if (flags & DE_CLEAR) {
			while (got-- > 0) {
				/*
				 * Get the buf header for the new block of the file.
				 */
				if (dep->de_Attributes & ATTR_DIRECTORY)
					bp = getblk(pmp->pm_devvp, cntobn(pmp, cn++),
					    pmp->pm_bpcluster, 0, INFSLP);
				else {
					bp = getblk(DETOV(dep), frcn++,
					    pmp->pm_bpcluster, 0, INFSLP);
					/*
					 * Do the bmap now, as in msdosfs_write
					 */
					if (pcbmap(dep, bp->b_lblkno, &bp->b_blkno, 0, 0))
						bp->b_blkno = -1;
					if (bp->b_blkno == -1)
						panic("extendfile: pcbmap");
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
