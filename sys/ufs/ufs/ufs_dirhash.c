/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ufs.h"

#ifdef UFS_DIRHASH

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/malloc.h>
#include <sys/fnv_hash.h>
#include <sys/proc.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/refcount.h>
#include <sys/sysctl.h>
#include <sys/sx.h>
#include <sys/eventhandler.h>
#include <sys/time.h>
#include <vm/uma.h>

#include <ufs/ufs/quota.h>
#include <ufs/ufs/inode.h>
#include <ufs/ufs/dir.h>
#include <ufs/ufs/dirhash.h>
#include <ufs/ufs/extattr.h>
#include <ufs/ufs/ufsmount.h>
#include <ufs/ufs/ufs_extern.h>

#define WRAPINCR(val, limit)	(((val) + 1 == (limit)) ? 0 : ((val) + 1))
#define WRAPDECR(val, limit)	(((val) == 0) ? ((limit) - 1) : ((val) - 1))
#define OFSFMT(vp)		((vp)->v_mount->mnt_maxsymlinklen <= 0)
#define BLKFREE2IDX(n)		((n) > DH_NFSTATS ? DH_NFSTATS : (n))

static MALLOC_DEFINE(M_DIRHASH, "ufs_dirhash", "UFS directory hash tables");

static int ufs_mindirhashsize = DIRBLKSIZ * 5;
SYSCTL_INT(_vfs_ufs, OID_AUTO, dirhash_minsize, CTLFLAG_RW,
    &ufs_mindirhashsize,
    0, "minimum directory size in bytes for which to use hashed lookup");
static int ufs_dirhashmaxmem = 2 * 1024 * 1024;	/* NOTE: initial value. It is
						   tuned in ufsdirhash_init() */
SYSCTL_INT(_vfs_ufs, OID_AUTO, dirhash_maxmem, CTLFLAG_RW, &ufs_dirhashmaxmem,
    0, "maximum allowed dirhash memory usage");
static int ufs_dirhashmem;
SYSCTL_INT(_vfs_ufs, OID_AUTO, dirhash_mem, CTLFLAG_RD, &ufs_dirhashmem,
    0, "current dirhash memory usage");
static int ufs_dirhashcheck = 0;
SYSCTL_INT(_vfs_ufs, OID_AUTO, dirhash_docheck, CTLFLAG_RW, &ufs_dirhashcheck,
    0, "enable extra sanity tests");
static int ufs_dirhashlowmemcount = 0;
SYSCTL_INT(_vfs_ufs, OID_AUTO, dirhash_lowmemcount, CTLFLAG_RD, 
    &ufs_dirhashlowmemcount, 0, "number of times low memory hook called");
static int ufs_dirhashreclaimpercent = 10;
static int ufsdirhash_set_reclaimpercent(SYSCTL_HANDLER_ARGS);
SYSCTL_PROC(_vfs_ufs, OID_AUTO, dirhash_reclaimpercent,
    CTLTYPE_INT | CTLFLAG_RW, 0, 0, ufsdirhash_set_reclaimpercent, "I",
    "set percentage of dirhash cache to be removed in low VM events");


static int ufsdirhash_hash(struct dirhash *dh, char *name, int namelen);
static void ufsdirhash_adjfree(struct dirhash *dh, doff_t offset, int diff);
static void ufsdirhash_delslot(struct dirhash *dh, int slot);
static int ufsdirhash_findslot(struct dirhash *dh, char *name, int namelen,
	   doff_t offset);
static doff_t ufsdirhash_getprev(struct direct *dp, doff_t offset);
static int ufsdirhash_recycle(int wanted);
static void ufsdirhash_lowmem(void);
static void ufsdirhash_free_locked(struct inode *ip);

static uma_zone_t	ufsdirhash_zone;

#define DIRHASHLIST_LOCK() 		mtx_lock(&ufsdirhash_mtx)
#define DIRHASHLIST_UNLOCK() 		mtx_unlock(&ufsdirhash_mtx)
#define DIRHASH_BLKALLOC_WAITOK() 	uma_zalloc(ufsdirhash_zone, M_WAITOK)
#define DIRHASH_BLKFREE(ptr) 		uma_zfree(ufsdirhash_zone, (ptr))
#define	DIRHASH_ASSERT_LOCKED(dh)					\
    sx_assert(&(dh)->dh_lock, SA_LOCKED)

/* Dirhash list; recently-used entries are near the tail. */
static TAILQ_HEAD(, dirhash) ufsdirhash_list;

/* Protects: ufsdirhash_list, `dh_list' field, ufs_dirhashmem. */
static struct mtx	ufsdirhash_mtx;

/*
 * Locking:
 *
 * The relationship between inode and dirhash is protected either by an
 * exclusive vnode lock or the vnode interlock where a shared vnode lock
 * may be used.  The dirhash_mtx is acquired after the dirhash lock.  To
 * handle teardown races, code wishing to lock the dirhash for an inode
 * when using a shared vnode lock must obtain a private reference on the
 * dirhash while holding the vnode interlock.  They can drop it once they
 * have obtained the dirhash lock and verified that the dirhash wasn't
 * recycled while they waited for the dirhash lock.
 *
 * ufsdirhash_build() acquires a shared lock on the dirhash when it is
 * successful.  This lock is released after a call to ufsdirhash_lookup().
 *
 * Functions requiring exclusive access use ufsdirhash_acquire() which may
 * free a dirhash structure that was recycled by ufsdirhash_recycle().
 *
 * The dirhash lock may be held across io operations.
 *
 * WITNESS reports a lock order reversal between the "bufwait" lock
 * and the "dirhash" lock.  However, this specific reversal will not
 * cause a deadlock.  To get a deadlock, one would have to lock a
 * buffer followed by the dirhash while a second thread locked a
 * buffer while holding the dirhash lock.  The second order can happen
 * under a shared or exclusive vnode lock for the associated directory
 * in lookup().  The first order, however, can only happen under an
 * exclusive vnode lock (e.g. unlink(), rename(), etc.).  Thus, for
 * a thread to be doing a "bufwait" -> "dirhash" order, it has to hold
 * an exclusive vnode lock.  That exclusive vnode lock will prevent
 * any other threads from doing a "dirhash" -> "bufwait" order.
 */

static void
ufsdirhash_hold(struct dirhash *dh)
{

	refcount_acquire(&dh->dh_refcount);
}

static void
ufsdirhash_drop(struct dirhash *dh)
{

	if (refcount_release(&dh->dh_refcount)) {
		sx_destroy(&dh->dh_lock);
		free(dh, M_DIRHASH);
	}
}

/*
 * Release the lock on a dirhash.
 */
static void
ufsdirhash_release(struct dirhash *dh)
{

	sx_unlock(&dh->dh_lock);
}

/*
 * Either acquire an existing hash locked shared or create a new hash and
 * return it exclusively locked.  May return NULL if the allocation fails.
 *
 * The vnode interlock is used to protect the i_dirhash pointer from
 * simultaneous access while only a shared vnode lock is held.
 */
static struct dirhash *
ufsdirhash_create(struct inode *ip)
{
	struct dirhash *ndh;
	struct dirhash *dh;
	struct vnode *vp;
	bool excl;

	ndh = dh = NULL;
	vp = ip->i_vnode;
	excl = false;
	for (;;) {
		/* Racy check for i_dirhash to prefetch a dirhash structure. */
		if (ip->i_dirhash == NULL && ndh == NULL) {
			ndh = malloc(sizeof *dh, M_DIRHASH,
			    M_NOWAIT | M_ZERO);
			if (ndh == NULL)
				return (NULL);
			refcount_init(&ndh->dh_refcount, 1);

			/*
			 * The DUPOK is to prevent warnings from the
			 * sx_slock() a few lines down which is safe
			 * since the duplicate lock in that case is
			 * the one for this dirhash we are creating
			 * now which has no external references until
			 * after this function returns.
			 */
			sx_init_flags(&ndh->dh_lock, "dirhash", SX_DUPOK);
			sx_xlock(&ndh->dh_lock);
		}
		/*
		 * Check i_dirhash.  If it's NULL just try to use a
		 * preallocated structure.  If none exists loop and try again.
		 */
		VI_LOCK(vp);
		dh = ip->i_dirhash;
		if (dh == NULL) {
			ip->i_dirhash = ndh;
			VI_UNLOCK(vp);
			if (ndh == NULL)
				continue;
			return (ndh);
		}
		ufsdirhash_hold(dh);
		VI_UNLOCK(vp);

		/* Acquire a lock on existing hashes. */
		if (excl)
			sx_xlock(&dh->dh_lock);
		else
			sx_slock(&dh->dh_lock);

		/* The hash could've been recycled while we were waiting. */
		VI_LOCK(vp);
		if (ip->i_dirhash != dh) {
			VI_UNLOCK(vp);
			ufsdirhash_release(dh);
			ufsdirhash_drop(dh);
			continue;
		}
		VI_UNLOCK(vp);
		ufsdirhash_drop(dh);

		/* If the hash is still valid we've succeeded. */
		if (dh->dh_hash != NULL)
			break;
		/*
		 * If the hash is NULL it has been recycled.  Try to upgrade
		 * so we can recreate it.  If we fail the upgrade, drop our
		 * lock and try again.
		 */
		if (excl || sx_try_upgrade(&dh->dh_lock))
			break;
		sx_sunlock(&dh->dh_lock);
		excl = true;
	}
	/* Free the preallocated structure if it was not necessary. */
	if (ndh) {
		ufsdirhash_release(ndh);
		ufsdirhash_drop(ndh);
	}
	return (dh);
}

/*
 * Acquire an exclusive lock on an existing hash.  Requires an exclusive
 * vnode lock to protect the i_dirhash pointer.  hashes that have been
 * recycled are reclaimed here and NULL is returned.
 */
static struct dirhash *
ufsdirhash_acquire(struct inode *ip)
{
	struct dirhash *dh;

	ASSERT_VOP_ELOCKED(ip->i_vnode, __FUNCTION__);

	dh = ip->i_dirhash;
	if (dh == NULL)
		return (NULL);
	sx_xlock(&dh->dh_lock);
	if (dh->dh_hash != NULL)
		return (dh);
	ufsdirhash_free_locked(ip);
	return (NULL);
}

/*
 * Acquire exclusively and free the hash pointed to by ip.  Works with a
 * shared or exclusive vnode lock.
 */
void
ufsdirhash_free(struct inode *ip)
{
	struct dirhash *dh;
	struct vnode *vp;

	vp = ip->i_vnode;
	for (;;) {
		/* Grab a reference on this inode's dirhash if it has one. */
		VI_LOCK(vp);
		dh = ip->i_dirhash;
		if (dh == NULL) {
			VI_UNLOCK(vp);
			return;
		}
		ufsdirhash_hold(dh);
		VI_UNLOCK(vp);

		/* Exclusively lock the dirhash. */
		sx_xlock(&dh->dh_lock);

		/* If this dirhash still belongs to this inode, then free it. */
		VI_LOCK(vp);
		if (ip->i_dirhash == dh) {
			VI_UNLOCK(vp);
			ufsdirhash_drop(dh);
			break;
		}
		VI_UNLOCK(vp);

		/*
		 * This inode's dirhash has changed while we were
		 * waiting for the dirhash lock, so try again.
		 */
		ufsdirhash_release(dh);
		ufsdirhash_drop(dh);
	}
	ufsdirhash_free_locked(ip);
}

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
	u_int dirblocks, i, narrays, nblocks, nslots;
	int j, memreqd, slot;

	/* Take care of a decreased sysctl value. */
	while (ufs_dirhashmem > ufs_dirhashmaxmem) {
		if (ufsdirhash_recycle(0) != 0)
			return (-1);
		/* Recycled enough memory, so unlock the list. */
		DIRHASHLIST_UNLOCK();
	}

	/* Check if we can/should use dirhash. */
	if (ip->i_size < ufs_mindirhashsize || OFSFMT(ip->i_vnode) ||
	    ip->i_effnlink == 0) {
		if (ip->i_dirhash)
			ufsdirhash_free(ip);
		return (-1);
	}
	dh = ufsdirhash_create(ip);
	if (dh == NULL)
		return (-1);
	if (dh->dh_hash != NULL)
		return (0);

	vp = ip->i_vnode;
	/* Allocate 50% more entries than this dir size could ever need. */
	KASSERT(ip->i_size >= DIRBLKSIZ, ("ufsdirhash_build size"));
	nslots = ip->i_size / DIRECTSIZ(1);
	nslots = (nslots * 3 + 1) / 2;
	narrays = howmany(nslots, DH_NBLKOFF);
	nslots = narrays * DH_NBLKOFF;
	dirblocks = howmany(ip->i_size, DIRBLKSIZ);
	nblocks = (dirblocks * 3 + 1) / 2;
	memreqd = sizeof(*dh) + narrays * sizeof(*dh->dh_hash) +
	    narrays * DH_NBLKOFF * sizeof(**dh->dh_hash) +
	    nblocks * sizeof(*dh->dh_blkfree);
	DIRHASHLIST_LOCK();
	if (memreqd + ufs_dirhashmem > ufs_dirhashmaxmem) {
		DIRHASHLIST_UNLOCK();
		if (memreqd > ufs_dirhashmaxmem / 2)
			goto fail;
		/* Try to free some space. */
		if (ufsdirhash_recycle(memreqd) != 0)
			goto fail;
		/* Enough was freed, and list has been locked. */
	}
	ufs_dirhashmem += memreqd;
	DIRHASHLIST_UNLOCK();

	/* Initialise the hash table and block statistics. */
	dh->dh_memreq = memreqd;
	dh->dh_narrays = narrays;
	dh->dh_hlen = nslots;
	dh->dh_nblk = nblocks;
	dh->dh_dirblks = dirblocks;
	for (i = 0; i < DH_NFSTATS; i++)
		dh->dh_firstfree[i] = -1;
	dh->dh_firstfree[DH_NFSTATS] = 0;
	dh->dh_hused = 0;
	dh->dh_seqoff = -1;
	dh->dh_score = DH_SCOREINIT;
	dh->dh_lastused = time_second;

	/*
	 * Use non-blocking mallocs so that we will revert to a linear
	 * lookup on failure rather than potentially blocking forever.
	 */
	dh->dh_hash = malloc(narrays * sizeof(dh->dh_hash[0]),
	    M_DIRHASH, M_NOWAIT | M_ZERO);
	if (dh->dh_hash == NULL)
		goto fail;
	dh->dh_blkfree = malloc(nblocks * sizeof(dh->dh_blkfree[0]),
	    M_DIRHASH, M_NOWAIT);
	if (dh->dh_blkfree == NULL)
		goto fail;
	for (i = 0; i < narrays; i++) {
		if ((dh->dh_hash[i] = DIRHASH_BLKALLOC_WAITOK()) == NULL)
			goto fail;
		for (j = 0; j < DH_NBLKOFF; j++)
			dh->dh_hash[i][j] = DIRHASH_EMPTY;
	}
	for (i = 0; i < dirblocks; i++)
		dh->dh_blkfree[i] = DIRBLKSIZ / DIRALIGN;
	bmask = vp->v_mount->mnt_stat.f_iosize - 1;
	pos = 0;
	while (pos < ip->i_size) {
		/* If necessary, get the next directory block. */
		if ((pos & bmask) == 0) {
			if (bp != NULL)
				brelse(bp);
			if (UFS_BLKATOFF(vp, (off_t)pos, NULL, &bp) != 0)
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
			ufsdirhash_adjfree(dh, pos, -DIRSIZ(0, ep));
		}
		pos += ep->d_reclen;
	}

	if (bp != NULL)
		brelse(bp);
	DIRHASHLIST_LOCK();
	TAILQ_INSERT_TAIL(&ufsdirhash_list, dh, dh_list);
	dh->dh_onlist = 1;
	DIRHASHLIST_UNLOCK();
	sx_downgrade(&dh->dh_lock);
	return (0);

fail:
	ufsdirhash_free_locked(ip);
	return (-1);
}

/*
 * Free any hash table associated with inode 'ip'.
 */
static void
ufsdirhash_free_locked(struct inode *ip)
{
	struct dirhash *dh;
	struct vnode *vp;
	int i;

	DIRHASH_ASSERT_LOCKED(ip->i_dirhash);

	/*
	 * Clear the pointer in the inode to prevent new threads from
	 * finding the dead structure.
	 */
	vp = ip->i_vnode;
	VI_LOCK(vp);
	dh = ip->i_dirhash;
	ip->i_dirhash = NULL;
	VI_UNLOCK(vp);

	/*
	 * Remove the hash from the list since we are going to free its
	 * memory.
	 */
	DIRHASHLIST_LOCK();
	if (dh->dh_onlist)
		TAILQ_REMOVE(&ufsdirhash_list, dh, dh_list);
	ufs_dirhashmem -= dh->dh_memreq;
	DIRHASHLIST_UNLOCK();

	/*
	 * At this point, any waiters for the lock should hold their
	 * own reference on the dirhash structure.  They will drop
	 * that reference once they grab the vnode interlock and see
	 * that ip->i_dirhash is NULL.
	 */
	sx_xunlock(&dh->dh_lock);

	/*
	 * Handle partially recycled as well as fully constructed hashes.
	 */
	if (dh->dh_hash != NULL) {
		for (i = 0; i < dh->dh_narrays; i++)
			if (dh->dh_hash[i] != NULL)
				DIRHASH_BLKFREE(dh->dh_hash[i]);
		free(dh->dh_hash, M_DIRHASH);
		if (dh->dh_blkfree != NULL)
			free(dh->dh_blkfree, M_DIRHASH);
	}

	/*
	 * Drop the inode's reference to the data structure.
	 */
	ufsdirhash_drop(dh);
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
 *
 * Must be called with the hash locked.  Returns with the hash unlocked.
 */
int
ufsdirhash_lookup(struct inode *ip, char *name, int namelen, doff_t *offp,
    struct buf **bpp, doff_t *prevoffp)
{
	struct dirhash *dh, *dh_next;
	struct direct *dp;
	struct vnode *vp;
	struct buf *bp;
	doff_t blkoff, bmask, offset, prevoff, seqoff;
	int i, slot;
	int error;

	dh = ip->i_dirhash;
	KASSERT(dh != NULL && dh->dh_hash != NULL,
	    ("ufsdirhash_lookup: Invalid dirhash %p\n", dh));
	DIRHASH_ASSERT_LOCKED(dh);
	/*
	 * Move this dirhash towards the end of the list if it has a
	 * score higher than the next entry, and acquire the dh_lock.
	 */
	DIRHASHLIST_LOCK();
	if (TAILQ_NEXT(dh, dh_list) != NULL) {
		/*
		 * If the new score will be greater than that of the next
		 * entry, then move this entry past it. With both mutexes
		 * held, dh_next won't go away, but its dh_score could
		 * change; that's not important since it is just a hint.
		 */
		if ((dh_next = TAILQ_NEXT(dh, dh_list)) != NULL &&
		    dh->dh_score >= dh_next->dh_score) {
			KASSERT(dh->dh_onlist, ("dirhash: not on list"));
			TAILQ_REMOVE(&ufsdirhash_list, dh, dh_list);
			TAILQ_INSERT_AFTER(&ufsdirhash_list, dh_next, dh,
			    dh_list);
		}
	}
	/* Update the score. */
	if (dh->dh_score < DH_SCOREMAX)
		dh->dh_score++;

	/* Update last used time. */
	dh->dh_lastused = time_second;
	DIRHASHLIST_UNLOCK();

	vp = ip->i_vnode;
	bmask = vp->v_mount->mnt_stat.f_iosize - 1;
	blkoff = -1;
	bp = NULL;
	seqoff = dh->dh_seqoff;
restart:
	slot = ufsdirhash_hash(dh, name, namelen);

	if (seqoff != -1) {
		/*
		 * Sequential access optimisation. seqoff contains the
		 * offset of the directory entry immediately following
		 * the last entry that was looked up. Check if this offset
		 * appears in the hash chain for the name we are looking for.
		 */
		for (i = slot; (offset = DH_ENTRY(dh, i)) != DIRHASH_EMPTY;
		    i = WRAPINCR(i, dh->dh_hlen))
			if (offset == seqoff)
				break;
		if (offset == seqoff) {
			/*
			 * We found an entry with the expected offset. This
			 * is probably the entry we want, but if not, the
			 * code below will retry.
			 */ 
			slot = i;
		} else
			seqoff = -1;
	}

	for (; (offset = DH_ENTRY(dh, slot)) != DIRHASH_EMPTY;
	    slot = WRAPINCR(slot, dh->dh_hlen)) {
		if (offset == DIRHASH_DEL)
			continue;
		if (offset < 0 || offset >= ip->i_size)
			panic("ufsdirhash_lookup: bad offset in hash array");
		if ((offset & ~bmask) != blkoff) {
			if (bp != NULL)
				brelse(bp);
			blkoff = offset & ~bmask;
			if (UFS_BLKATOFF(vp, (off_t)blkoff, NULL, &bp) != 0) {
				error = EJUSTRETURN;
				goto fail;
			}
		}
		KASSERT(bp != NULL, ("no buffer allocated"));
		dp = (struct direct *)(bp->b_data + (offset & bmask));
		if (dp->d_reclen == 0 || dp->d_reclen >
		    DIRBLKSIZ - (offset & (DIRBLKSIZ - 1))) {
			/* Corrupted directory. */
			error = EJUSTRETURN;
			goto fail;
		}
		if (dp->d_namlen == namelen &&
		    bcmp(dp->d_name, name, namelen) == 0) {
			/* Found. Get the prev offset if needed. */
			if (prevoffp != NULL) {
				if (offset & (DIRBLKSIZ - 1)) {
					prevoff = ufsdirhash_getprev(dp,
					    offset);
					if (prevoff == -1) {
						error = EJUSTRETURN;
						goto fail;
					}
				} else
					prevoff = offset;
				*prevoffp = prevoff;
			}

			/* Update offset. */
			dh->dh_seqoff = offset + DIRSIZ(0, dp);
			*bpp = bp;
			*offp = offset;
			ufsdirhash_release(dh);
			return (0);
		}

		/*
		 * When the name doesn't match in the sequential
		 * optimization case, go back and search normally.
		 */
		if (seqoff != -1) {
			seqoff = -1;
			goto restart;
		}
	}
	error = ENOENT;
fail:
	ufsdirhash_release(dh);
	if (bp != NULL)
		brelse(bp);
	return (error);
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

	dh = ip->i_dirhash;
	KASSERT(dh != NULL && dh->dh_hash != NULL,
	    ("ufsdirhash_findfree: Invalid dirhash %p\n", dh));
	DIRHASH_ASSERT_LOCKED(dh);

	/* Find a directory block with the desired free space. */
	dirblock = -1;
	for (i = howmany(slotneeded, DIRALIGN); i <= DH_NFSTATS; i++)
		if ((dirblock = dh->dh_firstfree[i]) != -1)
			break;
	if (dirblock == -1)
		return (-1);

	KASSERT(dirblock < dh->dh_nblk &&
	    dh->dh_blkfree[dirblock] >= howmany(slotneeded, DIRALIGN),
	    ("ufsdirhash_findfree: bad stats"));
	pos = dirblock * DIRBLKSIZ;
	error = UFS_BLKATOFF(ip->i_vnode, (off_t)pos, (char **)&dp, &bp);
	if (error)
		return (-1);

	/* Find the first entry with free space. */
	for (i = 0; i < DIRBLKSIZ; ) {
		if (dp->d_reclen == 0) {
			brelse(bp);
			return (-1);
		}
		if (dp->d_ino == 0 || dp->d_reclen > DIRSIZ(0, dp))
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
			freebytes -= DIRSIZ(0, dp);
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

	dh = ip->i_dirhash;
	DIRHASH_ASSERT_LOCKED(dh);
	KASSERT(dh != NULL && dh->dh_hash != NULL,
	    ("ufsdirhash_enduseful: Invalid dirhash %p\n", dh));

	if (dh->dh_blkfree[dh->dh_dirblks - 1] != DIRBLKSIZ / DIRALIGN)
		return (-1);

	for (i = dh->dh_dirblks - 1; i >= 0; i--)
		if (dh->dh_blkfree[i] != DIRBLKSIZ / DIRALIGN)
			break;

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

	if ((dh = ufsdirhash_acquire(ip)) == NULL)
		return;
	
	KASSERT(offset < dh->dh_dirblks * DIRBLKSIZ,
	    ("ufsdirhash_add: bad offset"));
	/*
	 * Normal hash usage is < 66%. If the usage gets too high then
	 * remove the hash entirely and let it be rebuilt later.
	 */
	if (dh->dh_hused >= (dh->dh_hlen * 3) / 4) {
		ufsdirhash_free_locked(ip);
		return;
	}

	/* Find a free hash slot (empty or deleted), and add the entry. */
	slot = ufsdirhash_hash(dh, dirp->d_name, dirp->d_namlen);
	while (DH_ENTRY(dh, slot) >= 0)
		slot = WRAPINCR(slot, dh->dh_hlen);
	if (DH_ENTRY(dh, slot) == DIRHASH_EMPTY)
		dh->dh_hused++;
	DH_ENTRY(dh, slot) = offset;

	/* Update last used time. */
	dh->dh_lastused = time_second;

	/* Update the per-block summary info. */
	ufsdirhash_adjfree(dh, offset, -DIRSIZ(0, dirp));
	ufsdirhash_release(dh);
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

	if ((dh = ufsdirhash_acquire(ip)) == NULL)
		return;

	KASSERT(offset < dh->dh_dirblks * DIRBLKSIZ,
	    ("ufsdirhash_remove: bad offset"));
	/* Find the entry */
	slot = ufsdirhash_findslot(dh, dirp->d_name, dirp->d_namlen, offset);

	/* Remove the hash entry. */
	ufsdirhash_delslot(dh, slot);

	/* Update the per-block summary info. */
	ufsdirhash_adjfree(dh, offset, DIRSIZ(0, dirp));
	ufsdirhash_release(dh);
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

	if ((dh = ufsdirhash_acquire(ip)) == NULL)
		return;

	KASSERT(oldoff < dh->dh_dirblks * DIRBLKSIZ &&
	    newoff < dh->dh_dirblks * DIRBLKSIZ,
	    ("ufsdirhash_move: bad offset"));
	/* Find the entry, and update the offset. */
	slot = ufsdirhash_findslot(dh, dirp->d_name, dirp->d_namlen, oldoff);
	DH_ENTRY(dh, slot) = newoff;
	ufsdirhash_release(dh);
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

	if ((dh = ufsdirhash_acquire(ip)) == NULL)
		return;

	KASSERT(offset == dh->dh_dirblks * DIRBLKSIZ,
	    ("ufsdirhash_newblk: bad offset"));
	block = offset / DIRBLKSIZ;
	if (block >= dh->dh_nblk) {
		/* Out of space; must rebuild. */
		ufsdirhash_free_locked(ip);
		return;
	}
	dh->dh_dirblks = block + 1;

	/* Account for the new free block. */
	dh->dh_blkfree[block] = DIRBLKSIZ / DIRALIGN;
	if (dh->dh_firstfree[DH_NFSTATS] == -1)
		dh->dh_firstfree[DH_NFSTATS] = block;
	ufsdirhash_release(dh);
}

/*
 * Inform dirhash that the directory is being truncated.
 */
void
ufsdirhash_dirtrunc(struct inode *ip, doff_t offset)
{
	struct dirhash *dh;
	int block, i;

	if ((dh = ufsdirhash_acquire(ip)) == NULL)
		return;

	KASSERT(offset <= dh->dh_dirblks * DIRBLKSIZ,
	    ("ufsdirhash_dirtrunc: bad offset"));
	block = howmany(offset, DIRBLKSIZ);
	/*
	 * If the directory shrinks to less than 1/8 of dh_nblk blocks
	 * (about 20% of its original size due to the 50% extra added in
	 * ufsdirhash_build) then free it, and let the caller rebuild
	 * if necessary.
	 */
	if (block < dh->dh_nblk / 8 && dh->dh_narrays > 1) {
		ufsdirhash_free_locked(ip);
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
	ufsdirhash_release(dh);
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
	if ((dh = ufsdirhash_acquire(ip)) == NULL)
		return;

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

		nfree += dp->d_reclen - DIRSIZ(0, dp);
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
	ufsdirhash_release(dh);
}

/*
 * Hash the specified filename into a dirhash slot.
 */
static int
ufsdirhash_hash(struct dirhash *dh, char *name, int namelen)
{
	u_int32_t hash;

	/*
	 * We hash the name and then some other bit of data that is
	 * invariant over the dirhash's lifetime. Otherwise names
	 * differing only in the last byte are placed close to one
	 * another in the table, which is bad for linear probing.
	 */
	hash = fnv_32_buf(name, namelen, FNV1_32_INIT);
	hash = fnv_32_buf(&dh, sizeof(dh), hash);
	return (hash % dh->dh_hlen);
}

/*
 * Adjust the number of free bytes in the block containing `offset'
 * by the value specified by `diff'.
 *
 * The caller must ensure we have exclusive access to `dh'; normally
 * that means that dh_lock should be held, but this is also called
 * from ufsdirhash_build() where exclusive access can be assumed.
 */
static void
ufsdirhash_adjfree(struct dirhash *dh, doff_t offset, int diff)
{
	int block, i, nfidx, ofidx;

	/* Update the per-block summary info. */
	block = offset / DIRBLKSIZ;
	KASSERT(block < dh->dh_nblk && block < dh->dh_dirblks,
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
static int
ufsdirhash_findslot(struct dirhash *dh, char *name, int namelen, doff_t offset)
{
	int slot;

	DIRHASH_ASSERT_LOCKED(dh);

	/* Find the entry. */
	KASSERT(dh->dh_hused < dh->dh_hlen, ("dirhash find full"));
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
static void
ufsdirhash_delslot(struct dirhash *dh, int slot)
{
	int i;

	DIRHASH_ASSERT_LOCKED(dh);

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
		KASSERT(dh->dh_hused >= 0, ("ufsdirhash_delslot neg hlen"));
	}
}

/*
 * Given a directory entry and its offset, find the offset of the
 * previous entry in the same DIRBLKSIZ-sized block. Returns an
 * offset, or -1 if there is no previous entry in the block or some
 * other problem occurred.
 */
static doff_t
ufsdirhash_getprev(struct direct *dirp, doff_t offset)
{
	struct direct *dp;
	char *blkbuf;
	doff_t blkoff, prevoff;
	int entrypos, i;

	blkoff = rounddown2(offset, DIRBLKSIZ);	/* offset of start of block */
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
 * Delete the given dirhash and reclaim its memory. Assumes that 
 * ufsdirhash_list is locked, and leaves it locked. Also assumes 
 * that dh is locked. Returns the amount of memory freed.
 */
static int
ufsdirhash_destroy(struct dirhash *dh)
{
	doff_t **hash;
	u_int8_t *blkfree;
	int i, mem, narrays;

	KASSERT(dh->dh_hash != NULL, ("dirhash: NULL hash on list"));
	
	/* Remove it from the list and detach its memory. */
	TAILQ_REMOVE(&ufsdirhash_list, dh, dh_list);
	dh->dh_onlist = 0;
	hash = dh->dh_hash;
	dh->dh_hash = NULL;
	blkfree = dh->dh_blkfree;
	dh->dh_blkfree = NULL;
	narrays = dh->dh_narrays;
	mem = dh->dh_memreq;
	dh->dh_memreq = 0;

	/* Unlock dirhash and free the detached memory. */
	ufsdirhash_release(dh);
	for (i = 0; i < narrays; i++)
		DIRHASH_BLKFREE(hash[i]);
	free(hash, M_DIRHASH);
	free(blkfree, M_DIRHASH);

	/* Account for the returned memory. */
	ufs_dirhashmem -= mem;

	return (mem);
}

/*
 * Try to free up `wanted' bytes by stealing memory from existing
 * dirhashes. Returns zero with list locked if successful.
 */
static int
ufsdirhash_recycle(int wanted)
{
	struct dirhash *dh;

	DIRHASHLIST_LOCK();
	dh = TAILQ_FIRST(&ufsdirhash_list);
	while (wanted + ufs_dirhashmem > ufs_dirhashmaxmem) {
		/* Decrement the score; only recycle if it becomes zero. */
		if (dh == NULL || --dh->dh_score > 0) {
			DIRHASHLIST_UNLOCK();
			return (-1);
		}
		/*
		 * If we can't lock it it's in use and we don't want to
		 * recycle it anyway.
		 */
		if (!sx_try_xlock(&dh->dh_lock)) {
			dh = TAILQ_NEXT(dh, dh_list);
			continue;
		}

		ufsdirhash_destroy(dh);

		/* Repeat if necessary. */
		dh = TAILQ_FIRST(&ufsdirhash_list);
	}
	/* Success; return with list locked. */
	return (0);
}

/*
 * Callback that frees some dirhashes when the system is low on virtual memory.
 */
static void
ufsdirhash_lowmem()
{
	struct dirhash *dh, *dh_temp;
	int memfreed, memwanted;

	ufs_dirhashlowmemcount++;
	memfreed = 0;
	memwanted = ufs_dirhashmem * ufs_dirhashreclaimpercent / 100;

	DIRHASHLIST_LOCK();

	/*
	 * Reclaim up to memwanted from the oldest dirhashes. This will allow
	 * us to make some progress when the system is running out of memory
	 * without compromising the dinamicity of maximum age. If the situation
	 * does not improve lowmem will be eventually retriggered and free some
	 * other entry in the cache. The entries on the head of the list should
	 * be the oldest. If during list traversal we can't get a lock on the
	 * dirhash, it will be skipped.
	 */
	TAILQ_FOREACH_SAFE(dh, &ufsdirhash_list, dh_list, dh_temp) {
		if (sx_try_xlock(&dh->dh_lock))
			memfreed += ufsdirhash_destroy(dh);
		if (memfreed >= memwanted)
			break;
	}
	DIRHASHLIST_UNLOCK();
}

static int
ufsdirhash_set_reclaimpercent(SYSCTL_HANDLER_ARGS)
{
	int error, v;

	v = ufs_dirhashreclaimpercent;
	error = sysctl_handle_int(oidp, &v, v, req);
	if (error)
		return (error);
	if (req->newptr == NULL)
		return (error);
	if (v == ufs_dirhashreclaimpercent)
		return (0);

	/* Refuse invalid percentages */
	if (v < 0 || v > 100)
		return (EINVAL);
	ufs_dirhashreclaimpercent = v;
	return (0);
}

void
ufsdirhash_init()
{
	ufs_dirhashmaxmem = lmax(roundup(hibufspace / 64, PAGE_SIZE),
	    2 * 1024 * 1024);

	ufsdirhash_zone = uma_zcreate("DIRHASH", DH_NBLKOFF * sizeof(doff_t),
	    NULL, NULL, NULL, NULL, UMA_ALIGN_PTR, 0);
	mtx_init(&ufsdirhash_mtx, "dirhash list", NULL, MTX_DEF);
	TAILQ_INIT(&ufsdirhash_list);

	/* Register a callback function to handle low memory signals */
	EVENTHANDLER_REGISTER(vm_lowmem, ufsdirhash_lowmem, NULL, 
	    EVENTHANDLER_PRI_FIRST);
}

void
ufsdirhash_uninit()
{
	KASSERT(TAILQ_EMPTY(&ufsdirhash_list), ("ufsdirhash_uninit"));
	uma_zdestroy(ufsdirhash_zone);
	mtx_destroy(&ufsdirhash_mtx);
}

#endif /* UFS_DIRHASH */
