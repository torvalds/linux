/* $OpenBSD: ufs_dirhash.c,v 1.43 2024/01/09 03:15:59 guenther Exp $	*/
/*
 * Copyright (c) 2001, 2002 Ian Dowse.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * This implements a hash-based lookup scheme for UFS directories.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/pool.h>
#include <sys/buf.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/sysctl.h>
#include <sys/mutex.h>

#include <crypto/siphash.h>

#include <ufs/ufs/quota.h>
#include <ufs/ufs/inode.h>
#include <ufs/ufs/dir.h>
#include <ufs/ufs/dirhash.h>
#include <ufs/ufs/ufsmount.h>
#include <ufs/ufs/ufs_extern.h>

#define WRAPINCR(val, limit)	(((val) + 1 == (limit)) ? 0 : ((val) + 1))
#define WRAPDECR(val, limit)	(((val) == 0) ? ((limit) - 1) : ((val) - 1))
#define BLKFREE2IDX(n)		((n) > DH_NFSTATS ? DH_NFSTATS : (n))

int ufs_mindirhashsize;
int ufs_dirhashmaxmem;
int ufs_dirhashmem;
int ufs_dirhashcheck;

SIPHASH_KEY ufsdirhash_key;

int ufsdirhash_hash(struct dirhash *dh, char *name, int namelen);
void ufsdirhash_adjfree(struct dirhash *dh, doff_t offset, int diff);
void ufsdirhash_delslot(struct dirhash *dh, int slot);
int ufsdirhash_findslot(struct dirhash *dh, char *name, int namelen,
   doff_t offset);
doff_t ufsdirhash_getprev(struct direct *dp, doff_t offset);
int ufsdirhash_recycle(int wanted);

struct pool		ufsdirhash_pool;

#define	DIRHASHLIST_LOCK()	rw_enter_write(&ufsdirhash_mtx)
#define	DIRHASHLIST_UNLOCK()	rw_exit_write(&ufsdirhash_mtx)
#define	DIRHASH_LOCK(dh)	rw_enter_write(&(dh)->dh_mtx)
#define	DIRHASH_UNLOCK(dh)	rw_exit_write(&(dh)->dh_mtx)
#define	DIRHASH_BLKALLOC_WAITOK()	pool_get(&ufsdirhash_pool, PR_WAITOK)
#define	DIRHASH_BLKFREE(v)		pool_put(&ufsdirhash_pool, v)

#define	mtx_assert(l, f)	/* nothing */
#define DIRHASH_ASSERT(e, m)	KASSERT((e))

/* Dirhash list; recently-used entries are near the tail. */
TAILQ_HEAD(, dirhash) ufsdirhash_list;

/* Protects: ufsdirhash_list, `dh_list' field, ufs_dirhashmem. */
struct rwlock		ufsdirhash_mtx;

/*
 * Locking order:
 *	ufsdirhash_mtx
 *	dh_mtx
 *
 * The dh_mtx mutex should be acquired either via the inode lock, or via
 * ufsdirhash_mtx. Only the owner of the inode may free the associated
 * dirhash, but anything can steal its memory and set dh_hash to NULL.
 */

/*
 * Attempt to build up a hash table for the directory contents in
 * inode 'ip'. Returns 0 on success, or -1 of the operation failed.
 */
int
ufsdirhash_build(struct inode *ip)
{
	struct dirhash *dh;
	struct buf *bp = NULL;
	struct direct *ep;
	struct vnode *vp;
	doff_t bmask, pos;
	int dirblocks, i, j, memreqd, nblocks, narrays, nslots, slot;

	/* Check if we can/should use dirhash. */
	if (ip->i_dirhash == NULL) {
		if (DIP(ip, size) < ufs_mindirhashsize)
			return (-1);
	} else {
		/* Hash exists, but sysctls could have changed. */
		if (DIP(ip, size) < ufs_mindirhashsize ||
		    ufs_dirhashmem > ufs_dirhashmaxmem) {
			ufsdirhash_free(ip);
			return (-1);
		}
		/* Check if hash exists and is intact (note: unlocked read). */
		if (ip->i_dirhash->dh_hash != NULL)
			return (0);
		/* Free the old, recycled hash and build a new one. */
		ufsdirhash_free(ip);
	}

	/* Don't hash removed directories. */
	if (ip->i_effnlink == 0)
		return (-1);

	vp = ip->i_vnode;
	/* Allocate 50% more entries than this dir size could ever need. */
	DIRHASH_ASSERT(DIP(ip, size) >= DIRBLKSIZ, ("ufsdirhash_build size"));
	nslots = DIP(ip, size) / DIRECTSIZ(1);
	nslots = (nslots * 3 + 1) / 2;
	narrays = howmany(nslots, DH_NBLKOFF);
	nslots = narrays * DH_NBLKOFF;
	dirblocks = howmany(DIP(ip, size), DIRBLKSIZ);
	nblocks = (dirblocks * 3 + 1) / 2;

	memreqd = sizeof(*dh) + narrays * sizeof(*dh->dh_hash) +
	    narrays * DH_NBLKOFF * sizeof(**dh->dh_hash) +
	    nblocks * sizeof(*dh->dh_blkfree);
	DIRHASHLIST_LOCK();
	if (memreqd + ufs_dirhashmem > ufs_dirhashmaxmem) {
		DIRHASHLIST_UNLOCK();
		if (memreqd > ufs_dirhashmaxmem / 2)
			return (-1);

		/* Try to free some space. */
		if (ufsdirhash_recycle(memreqd) != 0)
			return (-1);
		/* Enough was freed, and list has been locked. */
	}
	ufs_dirhashmem += memreqd;
	DIRHASHLIST_UNLOCK();

	/*
	 * Use non-blocking mallocs so that we will revert to a linear
	 * lookup on failure rather than potentially blocking forever.
	 */
	dh = malloc(sizeof(*dh), M_DIRHASH, M_NOWAIT|M_ZERO);
	if (dh == NULL) {
		DIRHASHLIST_LOCK();
		ufs_dirhashmem -= memreqd;
		DIRHASHLIST_UNLOCK();
		return (-1);
	}
	dh->dh_hash = mallocarray(narrays, sizeof(dh->dh_hash[0]),
	    M_DIRHASH, M_NOWAIT|M_ZERO);
	dh->dh_blkfree = mallocarray(nblocks, sizeof(dh->dh_blkfree[0]),
	    M_DIRHASH, M_NOWAIT | M_ZERO);
	if (dh->dh_hash == NULL || dh->dh_blkfree == NULL)
		goto fail;
	for (i = 0; i < narrays; i++) {
		if ((dh->dh_hash[i] = DIRHASH_BLKALLOC_WAITOK()) == NULL)
			goto fail;
		for (j = 0; j < DH_NBLKOFF; j++)
			dh->dh_hash[i][j] = DIRHASH_EMPTY;
	}

	/* Initialise the hash table and block statistics. */
	rw_init(&dh->dh_mtx, "dirhash");
	dh->dh_narrays = narrays;
	dh->dh_hlen = nslots;
	dh->dh_nblk = nblocks;
	dh->dh_dirblks = dirblocks;
	for (i = 0; i < dirblocks; i++)
		dh->dh_blkfree[i] = DIRBLKSIZ / DIRALIGN;
	for (i = 0; i < DH_NFSTATS; i++)
		dh->dh_firstfree[i] = -1;
	dh->dh_firstfree[DH_NFSTATS] = 0;
	dh->dh_seqopt = 0;
	dh->dh_seqoff = 0;
	dh->dh_score = DH_SCOREINIT;
	ip->i_dirhash = dh;

	bmask = VFSTOUFS(vp->v_mount)->um_mountp->mnt_stat.f_iosize - 1;
	pos = 0;
	while (pos < DIP(ip, size)) {
		/* If necessary, get the next directory block. */
		if ((pos & bmask) == 0) {
			if (bp != NULL)
				brelse(bp);
			if (UFS_BUFATOFF(ip, (off_t)pos, NULL, &bp) != 0)
				goto fail;
		}
		/* Add this entry to the hash. */
		ep = (struct direct *)((char *)bp->b_data + (pos & bmask));
		if (ep->d_reclen == 0 || ep->d_reclen >
		    DIRBLKSIZ - (pos & (DIRBLKSIZ - 1))) {
			/* Corrupted directory. */
			brelse(bp);
			goto fail;
		}
		if (ep->d_ino != 0) {
			/* Add the entry (simplified ufsdirhash_add). */
			slot = ufsdirhash_hash(dh, ep->d_name, ep->d_namlen);
			while (DH_ENTRY(dh, slot) != DIRHASH_EMPTY)
				slot = WRAPINCR(slot, dh->dh_hlen);
			dh->dh_hused++;
			DH_ENTRY(dh, slot) = pos;
			ufsdirhash_adjfree(dh, pos, -DIRSIZ(ep));
		}
		pos += ep->d_reclen;
	}

	if (bp != NULL)
		brelse(bp);
	DIRHASHLIST_LOCK();
	TAILQ_INSERT_TAIL(&ufsdirhash_list, dh, dh_list);
	dh->dh_onlist = 1;
	DIRHASHLIST_UNLOCK();
	return (0);

fail:
	if (dh->dh_hash != NULL) {
		for (i = 0; i < narrays; i++)
			if (dh->dh_hash[i] != NULL)
				DIRHASH_BLKFREE(dh->dh_hash[i]);
		free(dh->dh_hash, M_DIRHASH,
		    narrays * sizeof(dh->dh_hash[0]));
	}
	if (dh->dh_blkfree != NULL)
		free(dh->dh_blkfree, M_DIRHASH,
		    nblocks * sizeof(dh->dh_blkfree[0]));
	free(dh, M_DIRHASH, sizeof(*dh));
	ip->i_dirhash = NULL;
	DIRHASHLIST_LOCK();
	ufs_dirhashmem -= memreqd;
	DIRHASHLIST_UNLOCK();
	return (-1);
}

/*
 * Free any hash table associated with inode 'ip'.
 */
void
ufsdirhash_free(struct inode *ip)
{
	struct dirhash *dh;
	int i, mem;

	if ((dh = ip->i_dirhash) == NULL)
		return;
	DIRHASHLIST_LOCK();
	DIRHASH_LOCK(dh);
	if (dh->dh_onlist)
		TAILQ_REMOVE(&ufsdirhash_list, dh, dh_list);
	DIRHASH_UNLOCK(dh);
	DIRHASHLIST_UNLOCK();

	/* The dirhash pointed to by 'dh' is exclusively ours now. */

	mem = sizeof(*dh);
	if (dh->dh_hash != NULL) {
		for (i = 0; i < dh->dh_narrays; i++)
			DIRHASH_BLKFREE(dh->dh_hash[i]);
		free(dh->dh_hash, M_DIRHASH,
		    dh->dh_narrays * sizeof(dh->dh_hash[0]));
		free(dh->dh_blkfree, M_DIRHASH,
		    dh->dh_nblk * sizeof(dh->dh_blkfree[0]));
		mem += dh->dh_narrays * sizeof(*dh->dh_hash) +
		    dh->dh_narrays * DH_NBLKOFF * sizeof(**dh->dh_hash) +
		    dh->dh_nblk * sizeof(*dh->dh_blkfree);
	}
	free(dh, M_DIRHASH, sizeof(*dh));
	ip->i_dirhash = NULL;

	DIRHASHLIST_LOCK();
	ufs_dirhashmem -= mem;
	DIRHASHLIST_UNLOCK();
}

/*
 * Find the offset of the specified name within the given inode.
 * Returns 0 on success, ENOENT if the entry does not exist, or
 * EJUSTRETURN if the caller should revert to a linear search.
 *
 * If successful, the directory offset is stored in *offp, and a
 * pointer to a struct buf containing the entry is stored in *bpp. If
 * prevoffp is non-NULL, the offset of the previous entry within
 * the DIRBLKSIZ-sized block is stored in *prevoffp (if the entry
 * is the first in a block, the start of the block is used).
 */
int
ufsdirhash_lookup(struct inode *ip, char *name, int namelen, doff_t *offp,
    struct buf **bpp, doff_t *prevoffp)
{
	struct dirhash *dh, *dh_next;
	struct direct *dp;
	struct vnode *vp;
	struct buf *bp;
	doff_t blkoff, bmask, offset, prevoff;
	int i, slot;

	if ((dh = ip->i_dirhash) == NULL)
		return (EJUSTRETURN);
	/*
	 * Move this dirhash towards the end of the list if it has a
	 * score higher than the next entry, and acquire the dh_mtx.
	 * Optimise the case where it's already the last by performing
	 * an unlocked read of the TAILQ_NEXT pointer.
	 *
	 * In both cases, end up holding just dh_mtx.
	 */
	if (TAILQ_NEXT(dh, dh_list) != NULL) {
		DIRHASHLIST_LOCK();
		DIRHASH_LOCK(dh);
		/*
		 * If the new score will be greater than that of the next
		 * entry, then move this entry past it. With both mutexes
		 * held, dh_next won't go away, but its dh_score could
		 * change; that's not important since it is just a hint.
		 */
		if (dh->dh_hash != NULL &&
		    (dh_next = TAILQ_NEXT(dh, dh_list)) != NULL &&
		    dh->dh_score >= dh_next->dh_score) {
			DIRHASH_ASSERT(dh->dh_onlist, ("dirhash: not on list"));
			TAILQ_REMOVE(&ufsdirhash_list, dh, dh_list);
			TAILQ_INSERT_AFTER(&ufsdirhash_list, dh_next, dh,
			    dh_list);
		}
		DIRHASHLIST_UNLOCK();
	} else {
		/* Already the last, though that could change as we wait. */
		DIRHASH_LOCK(dh);
	}
	if (dh->dh_hash == NULL) {
		DIRHASH_UNLOCK(dh);
		ufsdirhash_free(ip);
		return (EJUSTRETURN);
	}

	/* Update the score. */
	if (dh->dh_score < DH_SCOREMAX)
		dh->dh_score++;

	vp = ip->i_vnode;
	bmask = VFSTOUFS(vp->v_mount)->um_mountp->mnt_stat.f_iosize - 1;
	blkoff = -1;
	bp = NULL;
restart:
	slot = ufsdirhash_hash(dh, name, namelen);

	if (dh->dh_seqopt) {
		/*
		 * Sequential access optimisation. dh_seqoff contains the
		 * offset of the directory entry immediately following
		 * the last entry that was looked up. Check if this offset
		 * appears in the hash chain for the name we are looking for.
		 */
		for (i = slot; (offset = DH_ENTRY(dh, i)) != DIRHASH_EMPTY;
		    i = WRAPINCR(i, dh->dh_hlen))
			if (offset == dh->dh_seqoff)
				break;
		if (offset == dh->dh_seqoff) {
			/*
			 * We found an entry with the expected offset. This
			 * is probably the entry we want, but if not, the
			 * code below will turn off seqopt and retry.
			 */
			slot = i;
		} else
			dh->dh_seqopt = 0;
	}

	for (; (offset = DH_ENTRY(dh, slot)) != DIRHASH_EMPTY;
	    slot = WRAPINCR(slot, dh->dh_hlen)) {
		if (offset == DIRHASH_DEL)
			continue;
		DIRHASH_UNLOCK(dh);

		if (offset < 0 || offset >= DIP(ip, size))
			panic("ufsdirhash_lookup: bad offset in hash array");
		if ((offset & ~bmask) != blkoff) {
			if (bp != NULL)
				brelse(bp);
			blkoff = offset & ~bmask;
			if (UFS_BUFATOFF(ip, (off_t)blkoff, NULL, &bp) != 0)
				return (EJUSTRETURN);
		}
		dp = (struct direct *)(bp->b_data + (offset & bmask));
		if (dp->d_reclen == 0 || dp->d_reclen >
		    DIRBLKSIZ - (offset & (DIRBLKSIZ - 1))) {
			/* Corrupted directory. */
			brelse(bp);
			return (EJUSTRETURN);
		}
		if (dp->d_namlen == namelen &&
		    memcmp(dp->d_name, name, namelen) == 0) {
			/* Found. Get the prev offset if needed. */
			if (prevoffp != NULL) {
				if (offset & (DIRBLKSIZ - 1)) {
					prevoff = ufsdirhash_getprev(dp,
					    offset);
					if (prevoff == -1) {
						brelse(bp);
						return (EJUSTRETURN);
					}
				} else
					prevoff = offset;
				*prevoffp = prevoff;
			}

			/* Check for sequential access, and update offset. */
			if (dh->dh_seqopt == 0 && dh->dh_seqoff == offset)
				dh->dh_seqopt = 1;
			dh->dh_seqoff = offset + DIRSIZ(dp);

			*bpp = bp;
			*offp = offset;
			return (0);
		}

		DIRHASH_LOCK(dh);
		if (dh->dh_hash == NULL) {
			DIRHASH_UNLOCK(dh);
			if (bp != NULL)
				brelse(bp);
			ufsdirhash_free(ip);
			return (EJUSTRETURN);
		}
		/*
		 * When the name doesn't match in the seqopt case, go back
		 * and search normally.
		 */
		if (dh->dh_seqopt) {
			dh->dh_seqopt = 0;
			goto restart;
		}
	}
	DIRHASH_UNLOCK(dh);
	if (bp != NULL)
		brelse(bp);
	return (ENOENT);
}

/*
 * Find a directory block with room for 'slotneeded' bytes. Returns
 * the offset of the directory entry that begins the free space.
 * This will either be the offset of an existing entry that has free
 * space at the end, or the offset of an entry with d_ino == 0 at
 * the start of a DIRBLKSIZ block.
 *
 * To use the space, the caller may need to compact existing entries in
 * the directory. The total number of bytes in all of the entries involved
 * in the compaction is stored in *slotsize. In other words, all of
 * the entries that must be compacted are exactly contained in the
 * region beginning at the returned offset and spanning *slotsize bytes.
 *
 * Returns -1 if no space was found, indicating that the directory
 * must be extended.
 */
doff_t
ufsdirhash_findfree(struct inode *ip, int slotneeded, int *slotsize)
{
	struct direct *dp;
	struct dirhash *dh;
	struct buf *bp;
	doff_t pos, slotstart;
	int dirblock, error, freebytes, i;

	if ((dh = ip->i_dirhash) == NULL)
		return (-1);
	DIRHASH_LOCK(dh);
	if (dh->dh_hash == NULL) {
		DIRHASH_UNLOCK(dh);
		ufsdirhash_free(ip);
		return (-1);
	}

	/* Find a directory block with the desired free space. */
	dirblock = -1;
	for (i = howmany(slotneeded, DIRALIGN); i <= DH_NFSTATS; i++)
		if ((dirblock = dh->dh_firstfree[i]) != -1)
			break;
	if (dirblock == -1) {
		DIRHASH_UNLOCK(dh);
		return (-1);
	}

	DIRHASH_ASSERT(dirblock < dh->dh_nblk &&
	    dh->dh_blkfree[dirblock] >= howmany(slotneeded, DIRALIGN),
	    ("ufsdirhash_findfree: bad stats"));
	DIRHASH_UNLOCK(dh);
	pos = dirblock * DIRBLKSIZ;
	error = UFS_BUFATOFF(ip, (off_t)pos, (char **)&dp, &bp);
	if (error)
		return (-1);

	/* Find the first entry with free space. */
	for (i = 0; i < DIRBLKSIZ; ) {
		if (dp->d_reclen == 0) {
			brelse(bp);
			return (-1);
		}
		if (dp->d_ino == 0 || dp->d_reclen > DIRSIZ(dp))
			break;
		i += dp->d_reclen;
		dp = (struct direct *)((char *)dp + dp->d_reclen);
	}
	if (i > DIRBLKSIZ) {
		brelse(bp);
		return (-1);
	}
	slotstart = pos + i;

	/* Find the range of entries needed to get enough space */
	freebytes = 0;
	while (i < DIRBLKSIZ && freebytes < slotneeded) {
		freebytes += dp->d_reclen;
		if (dp->d_ino != 0)
			freebytes -= DIRSIZ(dp);
		if (dp->d_reclen == 0) {
			brelse(bp);
			return (-1);
		}
		i += dp->d_reclen;
		dp = (struct direct *)((char *)dp + dp->d_reclen);
	}
	if (i > DIRBLKSIZ) {
		brelse(bp);
		return (-1);
	}
	if (freebytes < slotneeded)
		panic("ufsdirhash_findfree: free mismatch");
	brelse(bp);
	*slotsize = pos + i - slotstart;
	return (slotstart);
}

/*
 * Return the start of the unused space at the end of a directory, or
 * -1 if there are no trailing unused blocks.
 */
doff_t
ufsdirhash_enduseful(struct inode *ip)
{

	struct dirhash *dh;
	int i;

	if ((dh = ip->i_dirhash) == NULL)
		return (-1);
	DIRHASH_LOCK(dh);
	if (dh->dh_hash == NULL) {
		DIRHASH_UNLOCK(dh);
		ufsdirhash_free(ip);
		return (-1);
	}

	if (dh->dh_blkfree[dh->dh_dirblks - 1] != DIRBLKSIZ / DIRALIGN) {
		DIRHASH_UNLOCK(dh);
		return (-1);
	}

	for (i = dh->dh_dirblks - 1; i >= 0; i--)
		if (dh->dh_blkfree[i] != DIRBLKSIZ / DIRALIGN)
			break;
	DIRHASH_UNLOCK(dh);
	return ((doff_t)(i + 1) * DIRBLKSIZ);
}

/*
 * Insert information into the hash about a new directory entry. dirp
 * points to a struct direct containing the entry, and offset specifies
 * the offset of this entry.
 */
void
ufsdirhash_add(struct inode *ip, struct direct *dirp, doff_t offset)
{
	struct dirhash *dh;
	int slot;

	if ((dh = ip->i_dirhash) == NULL)
		return;
	DIRHASH_LOCK(dh);
	if (dh->dh_hash == NULL) {
		DIRHASH_UNLOCK(dh);
		ufsdirhash_free(ip);
		return;
	}

	DIRHASH_ASSERT(offset < dh->dh_dirblks * DIRBLKSIZ,
	    ("ufsdirhash_add: bad offset"));
	/*
	 * Normal hash usage is < 66%. If the usage gets too high then
	 * remove the hash entirely and let it be rebuilt later.
	 */
	if (dh->dh_hused >= (dh->dh_hlen * 3) / 4) {
		DIRHASH_UNLOCK(dh);
		ufsdirhash_free(ip);
		return;
	}

	/* Find a free hash slot (empty or deleted), and add the entry. */
	slot = ufsdirhash_hash(dh, dirp->d_name, dirp->d_namlen);
	while (DH_ENTRY(dh, slot) >= 0)
		slot = WRAPINCR(slot, dh->dh_hlen);
	if (DH_ENTRY(dh, slot) == DIRHASH_EMPTY)
		dh->dh_hused++;
	DH_ENTRY(dh, slot) = offset;

	/* Update the per-block summary info. */
	ufsdirhash_adjfree(dh, offset, -DIRSIZ(dirp));
	DIRHASH_UNLOCK(dh);
}

/*
 * Remove the specified directory entry from the hash. The entry to remove
 * is defined by the name in `dirp', which must exist at the specified
 * `offset' within the directory.
 */
void
ufsdirhash_remove(struct inode *ip, struct direct *dirp, doff_t offset)
{
	struct dirhash *dh;
	int slot;

	if ((dh = ip->i_dirhash) == NULL)
		return;
	DIRHASH_LOCK(dh);
	if (dh->dh_hash == NULL) {
		DIRHASH_UNLOCK(dh);
		ufsdirhash_free(ip);
		return;
	}

	DIRHASH_ASSERT(offset < dh->dh_dirblks * DIRBLKSIZ,
	    ("ufsdirhash_remove: bad offset"));
	/* Find the entry */
	slot = ufsdirhash_findslot(dh, dirp->d_name, dirp->d_namlen, offset);

	/* Remove the hash entry. */
	ufsdirhash_delslot(dh, slot);

	/* Update the per-block summary info. */
	ufsdirhash_adjfree(dh, offset, DIRSIZ(dirp));
	DIRHASH_UNLOCK(dh);
}

/*
 * Change the offset associated with a directory entry in the hash. Used
 * when compacting directory blocks.
 */
void
ufsdirhash_move(struct inode *ip, struct direct *dirp, doff_t oldoff,
    doff_t newoff)
{
	struct dirhash *dh;
	int slot;

	if ((dh = ip->i_dirhash) == NULL)
		return;
	DIRHASH_LOCK(dh);
	if (dh->dh_hash == NULL) {
		DIRHASH_UNLOCK(dh);
		ufsdirhash_free(ip);
		return;
	}

	DIRHASH_ASSERT(oldoff < dh->dh_dirblks * DIRBLKSIZ &&
	    newoff < dh->dh_dirblks * DIRBLKSIZ,
	    ("ufsdirhash_move: bad offset"));
	/* Find the entry, and update the offset. */
	slot = ufsdirhash_findslot(dh, dirp->d_name, dirp->d_namlen, oldoff);
	DH_ENTRY(dh, slot) = newoff;
	DIRHASH_UNLOCK(dh);
}

/*
 * Inform dirhash that the directory has grown by one block that
 * begins at offset (i.e. the new length is offset + DIRBLKSIZ).
 */
void
ufsdirhash_newblk(struct inode *ip, doff_t offset)
{
	struct dirhash *dh;
	int block;

	if ((dh = ip->i_dirhash) == NULL)
		return;
	DIRHASH_LOCK(dh);
	if (dh->dh_hash == NULL) {
		DIRHASH_UNLOCK(dh);
		ufsdirhash_free(ip);
		return;
	}

	DIRHASH_ASSERT(offset == dh->dh_dirblks * DIRBLKSIZ,
	    ("ufsdirhash_newblk: bad offset"));
	block = offset / DIRBLKSIZ;
	if (block >= dh->dh_nblk) {
		/* Out of space; must rebuild. */
		DIRHASH_UNLOCK(dh);
		ufsdirhash_free(ip);
		return;
	}
	dh->dh_dirblks = block + 1;

	/* Account for the new free block. */
	dh->dh_blkfree[block] = DIRBLKSIZ / DIRALIGN;
	if (dh->dh_firstfree[DH_NFSTATS] == -1)
		dh->dh_firstfree[DH_NFSTATS] = block;
	DIRHASH_UNLOCK(dh);
}

/*
 * Inform dirhash that the directory is being truncated.
 */
void
ufsdirhash_dirtrunc(struct inode *ip, doff_t offset)
{
	struct dirhash *dh;
	int block, i;

	if ((dh = ip->i_dirhash) == NULL)
		return;
	DIRHASH_LOCK(dh);
	if (dh->dh_hash == NULL) {
		DIRHASH_UNLOCK(dh);
		ufsdirhash_free(ip);
		return;
	}

	DIRHASH_ASSERT(offset <= dh->dh_dirblks * DIRBLKSIZ,
	    ("ufsdirhash_dirtrunc: bad offset"));
	block = howmany(offset, DIRBLKSIZ);
	/*
	 * If the directory shrinks to less than 1/8 of dh_nblk blocks
	 * (about 20% of its original size due to the 50% extra added in
	 * ufsdirhash_build) then free it, and let the caller rebuild
	 * if necessary.
	 */
	if (block < dh->dh_nblk / 8 && dh->dh_narrays > 1) {
		DIRHASH_UNLOCK(dh);
		ufsdirhash_free(ip);
		return;
	}

	/*
	 * Remove any `first free' information pertaining to the
	 * truncated blocks. All blocks we're removing should be
	 * completely unused.
	 */
	if (dh->dh_firstfree[DH_NFSTATS] >= block)
		dh->dh_firstfree[DH_NFSTATS] = -1;
	for (i = block; i < dh->dh_dirblks; i++)
		if (dh->dh_blkfree[i] != DIRBLKSIZ / DIRALIGN)
			panic("ufsdirhash_dirtrunc: blocks in use");
	for (i = 0; i < DH_NFSTATS; i++)
		if (dh->dh_firstfree[i] >= block)
			panic("ufsdirhash_dirtrunc: first free corrupt");
	dh->dh_dirblks = block;
	DIRHASH_UNLOCK(dh);
}

/*
 * Debugging function to check that the dirhash information about
 * a directory block matches its actual contents. Panics if a mismatch
 * is detected.
 *
 * On entry, `buf' should point to the start of an in-core
 * DIRBLKSIZ-sized directory block, and `offset' should contain the
 * offset from the start of the directory of that block.
 */
void
ufsdirhash_checkblock(struct inode *ip, char *buf, doff_t offset)
{
	struct dirhash *dh;
	struct direct *dp;
	int block, ffslot, i, nfree;

	if (!ufs_dirhashcheck)
		return;
	if ((dh = ip->i_dirhash) == NULL)
		return;
	DIRHASH_LOCK(dh);
	if (dh->dh_hash == NULL) {
		DIRHASH_UNLOCK(dh);
		ufsdirhash_free(ip);
		return;
	}

	block = offset / DIRBLKSIZ;
	if ((offset & (DIRBLKSIZ - 1)) != 0 || block >= dh->dh_dirblks)
		panic("ufsdirhash_checkblock: bad offset");

	nfree = 0;
	for (i = 0; i < DIRBLKSIZ; i += dp->d_reclen) {
		dp = (struct direct *)(buf + i);
		if (dp->d_reclen == 0 || i + dp->d_reclen > DIRBLKSIZ)
			panic("ufsdirhash_checkblock: bad dir");

		if (dp->d_ino == 0) {
#if 0
			/*
			 * XXX entries with d_ino == 0 should only occur
			 * at the start of a DIRBLKSIZ block. However the
			 * ufs code is tolerant of such entries at other
			 * offsets, and fsck does not fix them.
			 */
			if (i != 0)
				panic("ufsdirhash_checkblock: bad dir inode");
#endif
			nfree += dp->d_reclen;
			continue;
		}

		/* Check that the entry	exists (will panic if it doesn't). */
		ufsdirhash_findslot(dh, dp->d_name, dp->d_namlen, offset + i);

		nfree += dp->d_reclen - DIRSIZ(dp);
	}
	if (i != DIRBLKSIZ)
		panic("ufsdirhash_checkblock: bad dir end");

	if (dh->dh_blkfree[block] * DIRALIGN != nfree)
		panic("ufsdirhash_checkblock: bad free count");

	ffslot = BLKFREE2IDX(nfree / DIRALIGN);
	for (i = 0; i <= DH_NFSTATS; i++)
		if (dh->dh_firstfree[i] == block && i != ffslot)
			panic("ufsdirhash_checkblock: bad first-free");
	if (dh->dh_firstfree[ffslot] == -1)
		panic("ufsdirhash_checkblock: missing first-free entry");
	DIRHASH_UNLOCK(dh);
}

/*
 * Hash the specified filename into a dirhash slot.
 */
int
ufsdirhash_hash(struct dirhash *dh, char *name, int namelen)
{
	return SipHash24(&ufsdirhash_key, name, namelen) % dh->dh_hlen;
}

/*
 * Adjust the number of free bytes in the block containing `offset'
 * by the value specified by `diff'.
 *
 * The caller must ensure we have exclusive access to `dh'; normally
 * that means that dh_mtx should be held, but this is also called
 * from ufsdirhash_build() where exclusive access can be assumed.
 */
void
ufsdirhash_adjfree(struct dirhash *dh, doff_t offset, int diff)
{
	int block, i, nfidx, ofidx;

	/* Update the per-block summary info. */
	block = offset / DIRBLKSIZ;
	DIRHASH_ASSERT(block < dh->dh_nblk && block < dh->dh_dirblks,
	     ("dirhash bad offset"));
	ofidx = BLKFREE2IDX(dh->dh_blkfree[block]);
	dh->dh_blkfree[block] = (int)dh->dh_blkfree[block] + (diff / DIRALIGN);
	nfidx = BLKFREE2IDX(dh->dh_blkfree[block]);

	/* Update the `first free' list if necessary. */
	if (ofidx != nfidx) {
		/* If removing, scan forward for the next block. */
		if (dh->dh_firstfree[ofidx] == block) {
			for (i = block + 1; i < dh->dh_dirblks; i++)
				if (BLKFREE2IDX(dh->dh_blkfree[i]) == ofidx)
					break;
			dh->dh_firstfree[ofidx] = (i < dh->dh_dirblks) ? i : -1;
		}

		/* Make this the new `first free' if necessary */
		if (dh->dh_firstfree[nfidx] > block ||
		    dh->dh_firstfree[nfidx] == -1)
			dh->dh_firstfree[nfidx] = block;
	}
}

/*
 * Find the specified name which should have the specified offset.
 * Returns a slot number, and panics on failure.
 *
 * `dh' must be locked on entry and remains so on return.
 */
int
ufsdirhash_findslot(struct dirhash *dh, char *name, int namelen, doff_t offset)
{
	int slot;

	mtx_assert(&dh->dh_mtx, MA_OWNED);

	/* Find the entry. */
	DIRHASH_ASSERT(dh->dh_hused < dh->dh_hlen, ("dirhash find full"));
	slot = ufsdirhash_hash(dh, name, namelen);
	while (DH_ENTRY(dh, slot) != offset &&
	    DH_ENTRY(dh, slot) != DIRHASH_EMPTY)
		slot = WRAPINCR(slot, dh->dh_hlen);
	if (DH_ENTRY(dh, slot) != offset)
		panic("ufsdirhash_findslot: '%.*s' not found", namelen, name);

	return (slot);
}

/*
 * Remove the entry corresponding to the specified slot from the hash array.
 *
 * `dh' must be locked on entry and remains so on return.
 */
void
ufsdirhash_delslot(struct dirhash *dh, int slot)
{
	int i;

	mtx_assert(&dh->dh_mtx, MA_OWNED);

	/* Mark the entry as deleted. */
	DH_ENTRY(dh, slot) = DIRHASH_DEL;

	/* If this is the end of a chain of DIRHASH_DEL slots, remove them. */
	for (i = slot; DH_ENTRY(dh, i) == DIRHASH_DEL; )
		i = WRAPINCR(i, dh->dh_hlen);
	if (DH_ENTRY(dh, i) == DIRHASH_EMPTY) {
		i = WRAPDECR(i, dh->dh_hlen);
		while (DH_ENTRY(dh, i) == DIRHASH_DEL) {
			DH_ENTRY(dh, i) = DIRHASH_EMPTY;
			dh->dh_hused--;
			i = WRAPDECR(i, dh->dh_hlen);
		}
		DIRHASH_ASSERT(dh->dh_hused >= 0, ("ufsdirhash_delslot neg hlen"));
	}
}

/*
 * Given a directory entry and its offset, find the offset of the
 * previous entry in the same DIRBLKSIZ-sized block. Returns an
 * offset, or -1 if there is no previous entry in the block or some
 * other problem occurred.
 */
doff_t
ufsdirhash_getprev(struct direct *dirp, doff_t offset)
{
	struct direct *dp;
	char *blkbuf;
	doff_t blkoff, prevoff;
	int entrypos, i;

	blkoff = offset & ~(DIRBLKSIZ - 1);	/* offset of start of block */
	entrypos = offset & (DIRBLKSIZ - 1);	/* entry relative to block */
	blkbuf = (char *)dirp - entrypos;
	prevoff = blkoff;

	/* If `offset' is the start of a block, there is no previous entry. */
	if (entrypos == 0)
		return (-1);

	/* Scan from the start of the block until we get to the entry. */
	for (i = 0; i < entrypos; i += dp->d_reclen) {
		dp = (struct direct *)(blkbuf + i);
		if (dp->d_reclen == 0 || i + dp->d_reclen > entrypos)
			return (-1);	/* Corrupted directory. */
		prevoff = blkoff + i;
	}
	return (prevoff);
}

/*
 * Try to free up `wanted' bytes by stealing memory from existing
 * dirhashes. Returns zero with list locked if successful.
 */
int
ufsdirhash_recycle(int wanted)
{
	struct dirhash *dh;
	doff_t **hash;
	u_int8_t *blkfree;
	int i, mem, narrays, nblk;

	DIRHASHLIST_LOCK();
	while (wanted + ufs_dirhashmem > ufs_dirhashmaxmem) {
		/* Find a dirhash, and lock it. */
		if ((dh = TAILQ_FIRST(&ufsdirhash_list)) == NULL) {
			DIRHASHLIST_UNLOCK();
			return (-1);
		}
		DIRHASH_LOCK(dh);
		DIRHASH_ASSERT(dh->dh_hash != NULL, ("dirhash: NULL hash on list"));

		/* Decrement the score; only recycle if it becomes zero. */
		if (--dh->dh_score > 0) {
			DIRHASH_UNLOCK(dh);
			DIRHASHLIST_UNLOCK();
			return (-1);
		}

		/* Remove it from the list and detach its memory. */
		TAILQ_REMOVE(&ufsdirhash_list, dh, dh_list);
		dh->dh_onlist = 0;
		hash = dh->dh_hash;
		dh->dh_hash = NULL;
		blkfree = dh->dh_blkfree;
		dh->dh_blkfree = NULL;
		narrays = dh->dh_narrays;
		nblk = dh->dh_nblk;
		mem = narrays * sizeof(*dh->dh_hash) +
		    narrays * DH_NBLKOFF * sizeof(**dh->dh_hash) +
		    dh->dh_nblk * sizeof(*dh->dh_blkfree);

		/* Unlock everything, free the detached memory. */
		DIRHASH_UNLOCK(dh);
		DIRHASHLIST_UNLOCK();
		for (i = 0; i < narrays; i++)
			DIRHASH_BLKFREE(hash[i]);
		free(hash, M_DIRHASH, narrays * sizeof(hash[0]));
		free(blkfree, M_DIRHASH, nblk * sizeof(blkfree[0]));

		/* Account for the returned memory, and repeat if necessary. */
		DIRHASHLIST_LOCK();
		ufs_dirhashmem -= mem;
	}
	/* Success; return with list locked. */
	return (0);
}


void
ufsdirhash_init(void)
{
	pool_init(&ufsdirhash_pool, DH_NBLKOFF * sizeof(doff_t), 0, IPL_NONE,
	    PR_WAITOK, "dirhash", NULL);
	rw_init(&ufsdirhash_mtx, "dirhash_list");
	arc4random_buf(&ufsdirhash_key, sizeof(ufsdirhash_key));
	TAILQ_INIT(&ufsdirhash_list);
	ufs_dirhashmaxmem = 5 * 1024 * 1024;
	ufs_mindirhashsize = 5 * DIRBLKSIZ;
}

void
ufsdirhash_uninit(void)
{
	DIRHASH_ASSERT(TAILQ_EMPTY(&ufsdirhash_list), ("ufsdirhash_uninit"));
	pool_destroy(&ufsdirhash_pool);
}
