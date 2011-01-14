/*
 *  pNFS functions to call and manage layout drivers.
 *
 *  Copyright (c) 2002 [year of first publication]
 *  The Regents of the University of Michigan
 *  All Rights Reserved
 *
 *  Dean Hildebrand <dhildebz@umich.edu>
 *
 *  Permission is granted to use, copy, create derivative works, and
 *  redistribute this software and such derivative works for any purpose,
 *  so long as the name of the University of Michigan is not used in
 *  any advertising or publicity pertaining to the use or distribution
 *  of this software without specific, written prior authorization. If
 *  the above copyright notice or any other identification of the
 *  University of Michigan is included in any copy of any portion of
 *  this software, then the disclaimer below must also be included.
 *
 *  This software is provided as is, without representation or warranty
 *  of any kind either express or implied, including without limitation
 *  the implied warranties of merchantability, fitness for a particular
 *  purpose, or noninfringement.  The Regents of the University of
 *  Michigan shall not be liable for any damages, including special,
 *  indirect, incidental, or consequential damages, with respect to any
 *  claim arising out of or in connection with the use of the software,
 *  even if it has been or is hereafter advised of the possibility of
 *  such damages.
 */

#include <linux/nfs_fs.h>
#include "internal.h"
#include "pnfs.h"

#define NFSDBG_FACILITY		NFSDBG_PNFS

/* Locking:
 *
 * pnfs_spinlock:
 *      protects pnfs_modules_tbl.
 */
static DEFINE_SPINLOCK(pnfs_spinlock);

/*
 * pnfs_modules_tbl holds all pnfs modules
 */
static LIST_HEAD(pnfs_modules_tbl);

/* Return the registered pnfs layout driver module matching given id */
static struct pnfs_layoutdriver_type *
find_pnfs_driver_locked(u32 id)
{
	struct pnfs_layoutdriver_type *local;

	list_for_each_entry(local, &pnfs_modules_tbl, pnfs_tblid)
		if (local->id == id)
			goto out;
	local = NULL;
out:
	dprintk("%s: Searching for id %u, found %p\n", __func__, id, local);
	return local;
}

static struct pnfs_layoutdriver_type *
find_pnfs_driver(u32 id)
{
	struct pnfs_layoutdriver_type *local;

	spin_lock(&pnfs_spinlock);
	local = find_pnfs_driver_locked(id);
	spin_unlock(&pnfs_spinlock);
	return local;
}

void
unset_pnfs_layoutdriver(struct nfs_server *nfss)
{
	if (nfss->pnfs_curr_ld) {
		nfss->pnfs_curr_ld->clear_layoutdriver(nfss);
		module_put(nfss->pnfs_curr_ld->owner);
	}
	nfss->pnfs_curr_ld = NULL;
}

/*
 * Try to set the server's pnfs module to the pnfs layout type specified by id.
 * Currently only one pNFS layout driver per filesystem is supported.
 *
 * @id layout type. Zero (illegal layout type) indicates pNFS not in use.
 */
void
set_pnfs_layoutdriver(struct nfs_server *server, u32 id)
{
	struct pnfs_layoutdriver_type *ld_type = NULL;

	if (id == 0)
		goto out_no_driver;
	if (!(server->nfs_client->cl_exchange_flags &
		 (EXCHGID4_FLAG_USE_NON_PNFS | EXCHGID4_FLAG_USE_PNFS_MDS))) {
		printk(KERN_ERR "%s: id %u cl_exchange_flags 0x%x\n", __func__,
		       id, server->nfs_client->cl_exchange_flags);
		goto out_no_driver;
	}
	ld_type = find_pnfs_driver(id);
	if (!ld_type) {
		request_module("%s-%u", LAYOUT_NFSV4_1_MODULE_PREFIX, id);
		ld_type = find_pnfs_driver(id);
		if (!ld_type) {
			dprintk("%s: No pNFS module found for %u.\n",
				__func__, id);
			goto out_no_driver;
		}
	}
	if (!try_module_get(ld_type->owner)) {
		dprintk("%s: Could not grab reference on module\n", __func__);
		goto out_no_driver;
	}
	server->pnfs_curr_ld = ld_type;
	if (ld_type->set_layoutdriver(server)) {
		printk(KERN_ERR
		       "%s: Error initializing mount point for layout driver %u.\n",
		       __func__, id);
		module_put(ld_type->owner);
		goto out_no_driver;
	}
	dprintk("%s: pNFS module for %u set\n", __func__, id);
	return;

out_no_driver:
	dprintk("%s: Using NFSv4 I/O\n", __func__);
	server->pnfs_curr_ld = NULL;
}

int
pnfs_register_layoutdriver(struct pnfs_layoutdriver_type *ld_type)
{
	int status = -EINVAL;
	struct pnfs_layoutdriver_type *tmp;

	if (ld_type->id == 0) {
		printk(KERN_ERR "%s id 0 is reserved\n", __func__);
		return status;
	}
	if (!ld_type->alloc_lseg || !ld_type->free_lseg) {
		printk(KERN_ERR "%s Layout driver must provide "
		       "alloc_lseg and free_lseg.\n", __func__);
		return status;
	}

	spin_lock(&pnfs_spinlock);
	tmp = find_pnfs_driver_locked(ld_type->id);
	if (!tmp) {
		list_add(&ld_type->pnfs_tblid, &pnfs_modules_tbl);
		status = 0;
		dprintk("%s Registering id:%u name:%s\n", __func__, ld_type->id,
			ld_type->name);
	} else {
		printk(KERN_ERR "%s Module with id %d already loaded!\n",
			__func__, ld_type->id);
	}
	spin_unlock(&pnfs_spinlock);

	return status;
}
EXPORT_SYMBOL_GPL(pnfs_register_layoutdriver);

void
pnfs_unregister_layoutdriver(struct pnfs_layoutdriver_type *ld_type)
{
	dprintk("%s Deregistering id:%u\n", __func__, ld_type->id);
	spin_lock(&pnfs_spinlock);
	list_del(&ld_type->pnfs_tblid);
	spin_unlock(&pnfs_spinlock);
}
EXPORT_SYMBOL_GPL(pnfs_unregister_layoutdriver);

/*
 * pNFS client layout cache
 */

/* Need to hold i_lock if caller does not already hold reference */
void
get_layout_hdr(struct pnfs_layout_hdr *lo)
{
	atomic_inc(&lo->plh_refcount);
}

static void
destroy_layout_hdr(struct pnfs_layout_hdr *lo)
{
	dprintk("%s: freeing layout cache %p\n", __func__, lo);
	BUG_ON(!list_empty(&lo->plh_layouts));
	NFS_I(lo->plh_inode)->layout = NULL;
	kfree(lo);
}

static void
put_layout_hdr_locked(struct pnfs_layout_hdr *lo)
{
	if (atomic_dec_and_test(&lo->plh_refcount))
		destroy_layout_hdr(lo);
}

void
put_layout_hdr(struct pnfs_layout_hdr *lo)
{
	struct inode *inode = lo->plh_inode;

	if (atomic_dec_and_lock(&lo->plh_refcount, &inode->i_lock)) {
		destroy_layout_hdr(lo);
		spin_unlock(&inode->i_lock);
	}
}

static void
init_lseg(struct pnfs_layout_hdr *lo, struct pnfs_layout_segment *lseg)
{
	INIT_LIST_HEAD(&lseg->pls_list);
	atomic_set(&lseg->pls_refcount, 1);
	smp_mb();
	set_bit(NFS_LSEG_VALID, &lseg->pls_flags);
	lseg->pls_layout = lo;
}

static void free_lseg(struct pnfs_layout_segment *lseg)
{
	struct inode *ino = lseg->pls_layout->plh_inode;

	NFS_SERVER(ino)->pnfs_curr_ld->free_lseg(lseg);
	/* Matched by get_layout_hdr in pnfs_insert_layout */
	put_layout_hdr(NFS_I(ino)->layout);
}

/* The use of tmp_list is necessary because pnfs_curr_ld->free_lseg
 * could sleep, so must be called outside of the lock.
 * Returns 1 if object was removed, otherwise return 0.
 */
static int
put_lseg_locked(struct pnfs_layout_segment *lseg,
		struct list_head *tmp_list)
{
	dprintk("%s: lseg %p ref %d valid %d\n", __func__, lseg,
		atomic_read(&lseg->pls_refcount),
		test_bit(NFS_LSEG_VALID, &lseg->pls_flags));
	if (atomic_dec_and_test(&lseg->pls_refcount)) {
		struct inode *ino = lseg->pls_layout->plh_inode;

		BUG_ON(test_bit(NFS_LSEG_VALID, &lseg->pls_flags));
		list_del(&lseg->pls_list);
		if (list_empty(&lseg->pls_layout->plh_segs)) {
			struct nfs_client *clp;

			clp = NFS_SERVER(ino)->nfs_client;
			spin_lock(&clp->cl_lock);
			/* List does not take a reference, so no need for put here */
			list_del_init(&lseg->pls_layout->plh_layouts);
			spin_unlock(&clp->cl_lock);
			clear_bit(NFS_LAYOUT_BULK_RECALL, &lseg->pls_layout->plh_flags);
		}
		rpc_wake_up(&NFS_SERVER(ino)->roc_rpcwaitq);
		list_add(&lseg->pls_list, tmp_list);
		return 1;
	}
	return 0;
}

static bool
should_free_lseg(u32 lseg_iomode, u32 recall_iomode)
{
	return (recall_iomode == IOMODE_ANY ||
		lseg_iomode == recall_iomode);
}

/* Returns 1 if lseg is removed from list, 0 otherwise */
static int mark_lseg_invalid(struct pnfs_layout_segment *lseg,
			     struct list_head *tmp_list)
{
	int rv = 0;

	if (test_and_clear_bit(NFS_LSEG_VALID, &lseg->pls_flags)) {
		/* Remove the reference keeping the lseg in the
		 * list.  It will now be removed when all
		 * outstanding io is finished.
		 */
		rv = put_lseg_locked(lseg, tmp_list);
	}
	return rv;
}

/* Returns count of number of matching invalid lsegs remaining in list
 * after call.
 */
int
mark_matching_lsegs_invalid(struct pnfs_layout_hdr *lo,
			    struct list_head *tmp_list,
			    u32 iomode)
{
	struct pnfs_layout_segment *lseg, *next;
	int invalid = 0, removed = 0;

	dprintk("%s:Begin lo %p\n", __func__, lo);

	list_for_each_entry_safe(lseg, next, &lo->plh_segs, pls_list)
		if (should_free_lseg(lseg->pls_range.iomode, iomode)) {
			dprintk("%s: freeing lseg %p iomode %d "
				"offset %llu length %llu\n", __func__,
				lseg, lseg->pls_range.iomode, lseg->pls_range.offset,
				lseg->pls_range.length);
			invalid++;
			removed += mark_lseg_invalid(lseg, tmp_list);
		}
	dprintk("%s:Return %i\n", __func__, invalid - removed);
	return invalid - removed;
}

void
pnfs_free_lseg_list(struct list_head *free_me)
{
	struct pnfs_layout_segment *lseg, *tmp;

	list_for_each_entry_safe(lseg, tmp, free_me, pls_list) {
		list_del(&lseg->pls_list);
		free_lseg(lseg);
	}
}

void
pnfs_destroy_layout(struct nfs_inode *nfsi)
{
	struct pnfs_layout_hdr *lo;
	LIST_HEAD(tmp_list);

	spin_lock(&nfsi->vfs_inode.i_lock);
	lo = nfsi->layout;
	if (lo) {
		set_bit(NFS_LAYOUT_DESTROYED, &nfsi->layout->plh_flags);
		mark_matching_lsegs_invalid(lo, &tmp_list, IOMODE_ANY);
		/* Matched by refcount set to 1 in alloc_init_layout_hdr */
		put_layout_hdr_locked(lo);
	}
	spin_unlock(&nfsi->vfs_inode.i_lock);
	pnfs_free_lseg_list(&tmp_list);
}

/*
 * Called by the state manger to remove all layouts established under an
 * expired lease.
 */
void
pnfs_destroy_all_layouts(struct nfs_client *clp)
{
	struct pnfs_layout_hdr *lo;
	LIST_HEAD(tmp_list);

	spin_lock(&clp->cl_lock);
	list_splice_init(&clp->cl_layouts, &tmp_list);
	spin_unlock(&clp->cl_lock);

	while (!list_empty(&tmp_list)) {
		lo = list_entry(tmp_list.next, struct pnfs_layout_hdr,
				plh_layouts);
		dprintk("%s freeing layout for inode %lu\n", __func__,
			lo->plh_inode->i_ino);
		pnfs_destroy_layout(NFS_I(lo->plh_inode));
	}
}

/* update lo->plh_stateid with new if is more recent */
void
pnfs_set_layout_stateid(struct pnfs_layout_hdr *lo, const nfs4_stateid *new,
			bool update_barrier)
{
	u32 oldseq, newseq;

	oldseq = be32_to_cpu(lo->plh_stateid.stateid.seqid);
	newseq = be32_to_cpu(new->stateid.seqid);
	if ((int)(newseq - oldseq) > 0) {
		memcpy(&lo->plh_stateid, &new->stateid, sizeof(new->stateid));
		if (update_barrier) {
			u32 new_barrier = be32_to_cpu(new->stateid.seqid);

			if ((int)(new_barrier - lo->plh_barrier))
				lo->plh_barrier = new_barrier;
		} else {
			/* Because of wraparound, we want to keep the barrier
			 * "close" to the current seqids.  It needs to be
			 * within 2**31 to count as "behind", so if it
			 * gets too near that limit, give us a litle leeway
			 * and bring it to within 2**30.
			 * NOTE - and yes, this is all unsigned arithmetic.
			 */
			if (unlikely((newseq - lo->plh_barrier) > (3 << 29)))
				lo->plh_barrier = newseq - (1 << 30);
		}
	}
}

/* lget is set to 1 if called from inside send_layoutget call chain */
static bool
pnfs_layoutgets_blocked(struct pnfs_layout_hdr *lo, nfs4_stateid *stateid,
			int lget)
{
	if ((stateid) &&
	    (int)(lo->plh_barrier - be32_to_cpu(stateid->stateid.seqid)) >= 0)
		return true;
	return lo->plh_block_lgets ||
		test_bit(NFS_LAYOUT_BULK_RECALL, &lo->plh_flags) ||
		(list_empty(&lo->plh_segs) &&
		 (atomic_read(&lo->plh_outstanding) > lget));
}

int
pnfs_choose_layoutget_stateid(nfs4_stateid *dst, struct pnfs_layout_hdr *lo,
			      struct nfs4_state *open_state)
{
	int status = 0;

	dprintk("--> %s\n", __func__);
	spin_lock(&lo->plh_inode->i_lock);
	if (pnfs_layoutgets_blocked(lo, NULL, 1)) {
		status = -EAGAIN;
	} else if (list_empty(&lo->plh_segs)) {
		int seq;

		do {
			seq = read_seqbegin(&open_state->seqlock);
			memcpy(dst->data, open_state->stateid.data,
			       sizeof(open_state->stateid.data));
		} while (read_seqretry(&open_state->seqlock, seq));
	} else
		memcpy(dst->data, lo->plh_stateid.data, sizeof(lo->plh_stateid.data));
	spin_unlock(&lo->plh_inode->i_lock);
	dprintk("<-- %s\n", __func__);
	return status;
}

/*
* Get layout from server.
*    for now, assume that whole file layouts are requested.
*    arg->offset: 0
*    arg->length: all ones
*/
static struct pnfs_layout_segment *
send_layoutget(struct pnfs_layout_hdr *lo,
	   struct nfs_open_context *ctx,
	   u32 iomode)
{
	struct inode *ino = lo->plh_inode;
	struct nfs_server *server = NFS_SERVER(ino);
	struct nfs4_layoutget *lgp;
	struct pnfs_layout_segment *lseg = NULL;

	dprintk("--> %s\n", __func__);

	BUG_ON(ctx == NULL);
	lgp = kzalloc(sizeof(*lgp), GFP_KERNEL);
	if (lgp == NULL)
		return NULL;
	lgp->args.minlength = NFS4_MAX_UINT64;
	lgp->args.maxcount = PNFS_LAYOUT_MAXSIZE;
	lgp->args.range.iomode = iomode;
	lgp->args.range.offset = 0;
	lgp->args.range.length = NFS4_MAX_UINT64;
	lgp->args.type = server->pnfs_curr_ld->id;
	lgp->args.inode = ino;
	lgp->args.ctx = get_nfs_open_context(ctx);
	lgp->lsegpp = &lseg;

	/* Synchronously retrieve layout information from server and
	 * store in lseg.
	 */
	nfs4_proc_layoutget(lgp);
	if (!lseg) {
		/* remember that LAYOUTGET failed and suspend trying */
		set_bit(lo_fail_bit(iomode), &lo->plh_flags);
	}
	return lseg;
}

bool pnfs_roc(struct inode *ino)
{
	struct pnfs_layout_hdr *lo;
	struct pnfs_layout_segment *lseg, *tmp;
	LIST_HEAD(tmp_list);
	bool found = false;

	spin_lock(&ino->i_lock);
	lo = NFS_I(ino)->layout;
	if (!lo || !test_and_clear_bit(NFS_LAYOUT_ROC, &lo->plh_flags) ||
	    test_bit(NFS_LAYOUT_BULK_RECALL, &lo->plh_flags))
		goto out_nolayout;
	list_for_each_entry_safe(lseg, tmp, &lo->plh_segs, pls_list)
		if (test_bit(NFS_LSEG_ROC, &lseg->pls_flags)) {
			mark_lseg_invalid(lseg, &tmp_list);
			found = true;
		}
	if (!found)
		goto out_nolayout;
	lo->plh_block_lgets++;
	get_layout_hdr(lo); /* matched in pnfs_roc_release */
	spin_unlock(&ino->i_lock);
	pnfs_free_lseg_list(&tmp_list);
	return true;

out_nolayout:
	spin_unlock(&ino->i_lock);
	return false;
}

void pnfs_roc_release(struct inode *ino)
{
	struct pnfs_layout_hdr *lo;

	spin_lock(&ino->i_lock);
	lo = NFS_I(ino)->layout;
	lo->plh_block_lgets--;
	put_layout_hdr_locked(lo);
	spin_unlock(&ino->i_lock);
}

void pnfs_roc_set_barrier(struct inode *ino, u32 barrier)
{
	struct pnfs_layout_hdr *lo;

	spin_lock(&ino->i_lock);
	lo = NFS_I(ino)->layout;
	if ((int)(barrier - lo->plh_barrier) > 0)
		lo->plh_barrier = barrier;
	spin_unlock(&ino->i_lock);
}

bool pnfs_roc_drain(struct inode *ino, u32 *barrier)
{
	struct nfs_inode *nfsi = NFS_I(ino);
	struct pnfs_layout_segment *lseg;
	bool found = false;

	spin_lock(&ino->i_lock);
	list_for_each_entry(lseg, &nfsi->layout->plh_segs, pls_list)
		if (test_bit(NFS_LSEG_ROC, &lseg->pls_flags)) {
			found = true;
			break;
		}
	if (!found) {
		struct pnfs_layout_hdr *lo = nfsi->layout;
		u32 current_seqid = be32_to_cpu(lo->plh_stateid.stateid.seqid);

		/* Since close does not return a layout stateid for use as
		 * a barrier, we choose the worst-case barrier.
		 */
		*barrier = current_seqid + atomic_read(&lo->plh_outstanding);
	}
	spin_unlock(&ino->i_lock);
	return found;
}

/*
 * Compare two layout segments for sorting into layout cache.
 * We want to preferentially return RW over RO layouts, so ensure those
 * are seen first.
 */
static s64
cmp_layout(u32 iomode1, u32 iomode2)
{
	/* read > read/write */
	return (int)(iomode2 == IOMODE_READ) - (int)(iomode1 == IOMODE_READ);
}

static void
pnfs_insert_layout(struct pnfs_layout_hdr *lo,
		   struct pnfs_layout_segment *lseg)
{
	struct pnfs_layout_segment *lp;
	int found = 0;

	dprintk("%s:Begin\n", __func__);

	assert_spin_locked(&lo->plh_inode->i_lock);
	list_for_each_entry(lp, &lo->plh_segs, pls_list) {
		if (cmp_layout(lp->pls_range.iomode, lseg->pls_range.iomode) > 0)
			continue;
		list_add_tail(&lseg->pls_list, &lp->pls_list);
		dprintk("%s: inserted lseg %p "
			"iomode %d offset %llu length %llu before "
			"lp %p iomode %d offset %llu length %llu\n",
			__func__, lseg, lseg->pls_range.iomode,
			lseg->pls_range.offset, lseg->pls_range.length,
			lp, lp->pls_range.iomode, lp->pls_range.offset,
			lp->pls_range.length);
		found = 1;
		break;
	}
	if (!found) {
		list_add_tail(&lseg->pls_list, &lo->plh_segs);
		dprintk("%s: inserted lseg %p "
			"iomode %d offset %llu length %llu at tail\n",
			__func__, lseg, lseg->pls_range.iomode,
			lseg->pls_range.offset, lseg->pls_range.length);
	}
	get_layout_hdr(lo);

	dprintk("%s:Return\n", __func__);
}

static struct pnfs_layout_hdr *
alloc_init_layout_hdr(struct inode *ino)
{
	struct pnfs_layout_hdr *lo;

	lo = kzalloc(sizeof(struct pnfs_layout_hdr), GFP_KERNEL);
	if (!lo)
		return NULL;
	atomic_set(&lo->plh_refcount, 1);
	INIT_LIST_HEAD(&lo->plh_layouts);
	INIT_LIST_HEAD(&lo->plh_segs);
	INIT_LIST_HEAD(&lo->plh_bulk_recall);
	lo->plh_inode = ino;
	return lo;
}

static struct pnfs_layout_hdr *
pnfs_find_alloc_layout(struct inode *ino)
{
	struct nfs_inode *nfsi = NFS_I(ino);
	struct pnfs_layout_hdr *new = NULL;

	dprintk("%s Begin ino=%p layout=%p\n", __func__, ino, nfsi->layout);

	assert_spin_locked(&ino->i_lock);
	if (nfsi->layout) {
		if (test_bit(NFS_LAYOUT_DESTROYED, &nfsi->layout->plh_flags))
			return NULL;
		else
			return nfsi->layout;
	}
	spin_unlock(&ino->i_lock);
	new = alloc_init_layout_hdr(ino);
	spin_lock(&ino->i_lock);

	if (likely(nfsi->layout == NULL))	/* Won the race? */
		nfsi->layout = new;
	else
		kfree(new);
	return nfsi->layout;
}

/*
 * iomode matching rules:
 * iomode	lseg	match
 * -----	-----	-----
 * ANY		READ	true
 * ANY		RW	true
 * RW		READ	false
 * RW		RW	true
 * READ		READ	true
 * READ		RW	true
 */
static int
is_matching_lseg(struct pnfs_layout_segment *lseg, u32 iomode)
{
	return (iomode != IOMODE_RW || lseg->pls_range.iomode == IOMODE_RW);
}

/*
 * lookup range in layout
 */
static struct pnfs_layout_segment *
pnfs_find_lseg(struct pnfs_layout_hdr *lo, u32 iomode)
{
	struct pnfs_layout_segment *lseg, *ret = NULL;

	dprintk("%s:Begin\n", __func__);

	assert_spin_locked(&lo->plh_inode->i_lock);
	list_for_each_entry(lseg, &lo->plh_segs, pls_list) {
		if (test_bit(NFS_LSEG_VALID, &lseg->pls_flags) &&
		    is_matching_lseg(lseg, iomode)) {
			ret = lseg;
			break;
		}
		if (cmp_layout(iomode, lseg->pls_range.iomode) > 0)
			break;
	}

	dprintk("%s:Return lseg %p ref %d\n",
		__func__, ret, ret ? atomic_read(&ret->pls_refcount) : 0);
	return ret;
}

/*
 * Layout segment is retreived from the server if not cached.
 * The appropriate layout segment is referenced and returned to the caller.
 */
struct pnfs_layout_segment *
pnfs_update_layout(struct inode *ino,
		   struct nfs_open_context *ctx,
		   enum pnfs_iomode iomode)
{
	struct nfs_inode *nfsi = NFS_I(ino);
	struct nfs_client *clp = NFS_SERVER(ino)->nfs_client;
	struct pnfs_layout_hdr *lo;
	struct pnfs_layout_segment *lseg = NULL;

	if (!pnfs_enabled_sb(NFS_SERVER(ino)))
		return NULL;
	spin_lock(&ino->i_lock);
	lo = pnfs_find_alloc_layout(ino);
	if (lo == NULL) {
		dprintk("%s ERROR: can't get pnfs_layout_hdr\n", __func__);
		goto out_unlock;
	}

	/* Do we even need to bother with this? */
	if (test_bit(NFS4CLNT_LAYOUTRECALL, &clp->cl_state) ||
	    test_bit(NFS_LAYOUT_BULK_RECALL, &lo->plh_flags)) {
		dprintk("%s matches recall, use MDS\n", __func__);
		goto out_unlock;
	}
	/* Check to see if the layout for the given range already exists */
	lseg = pnfs_find_lseg(lo, iomode);
	if (lseg)
		goto out_unlock;

	/* if LAYOUTGET already failed once we don't try again */
	if (test_bit(lo_fail_bit(iomode), &nfsi->layout->plh_flags))
		goto out_unlock;

	if (pnfs_layoutgets_blocked(lo, NULL, 0))
		goto out_unlock;
	atomic_inc(&lo->plh_outstanding);

	get_layout_hdr(lo);
	if (list_empty(&lo->plh_segs)) {
		/* The lo must be on the clp list if there is any
		 * chance of a CB_LAYOUTRECALL(FILE) coming in.
		 */
		spin_lock(&clp->cl_lock);
		BUG_ON(!list_empty(&lo->plh_layouts));
		list_add_tail(&lo->plh_layouts, &clp->cl_layouts);
		spin_unlock(&clp->cl_lock);
	}
	spin_unlock(&ino->i_lock);

	lseg = send_layoutget(lo, ctx, iomode);
	if (!lseg) {
		spin_lock(&ino->i_lock);
		if (list_empty(&lo->plh_segs)) {
			spin_lock(&clp->cl_lock);
			list_del_init(&lo->plh_layouts);
			spin_unlock(&clp->cl_lock);
			clear_bit(NFS_LAYOUT_BULK_RECALL, &lo->plh_flags);
		}
		spin_unlock(&ino->i_lock);
	}
	atomic_dec(&lo->plh_outstanding);
	put_layout_hdr(lo);
out:
	dprintk("%s end, state 0x%lx lseg %p\n", __func__,
		nfsi->layout->plh_flags, lseg);
	return lseg;
out_unlock:
	spin_unlock(&ino->i_lock);
	goto out;
}

int
pnfs_layout_process(struct nfs4_layoutget *lgp)
{
	struct pnfs_layout_hdr *lo = NFS_I(lgp->args.inode)->layout;
	struct nfs4_layoutget_res *res = &lgp->res;
	struct pnfs_layout_segment *lseg;
	struct inode *ino = lo->plh_inode;
	struct nfs_client *clp = NFS_SERVER(ino)->nfs_client;
	int status = 0;

	/* Verify we got what we asked for.
	 * Note that because the xdr parsing only accepts a single
	 * element array, this can fail even if the server is behaving
	 * correctly.
	 */
	if (lgp->args.range.iomode > res->range.iomode ||
	    res->range.offset != 0 ||
	    res->range.length != NFS4_MAX_UINT64) {
		status = -EINVAL;
		goto out;
	}
	/* Inject layout blob into I/O device driver */
	lseg = NFS_SERVER(ino)->pnfs_curr_ld->alloc_lseg(lo, res);
	if (!lseg || IS_ERR(lseg)) {
		if (!lseg)
			status = -ENOMEM;
		else
			status = PTR_ERR(lseg);
		dprintk("%s: Could not allocate layout: error %d\n",
		       __func__, status);
		goto out;
	}

	spin_lock(&ino->i_lock);
	if (test_bit(NFS4CLNT_LAYOUTRECALL, &clp->cl_state) ||
	    test_bit(NFS_LAYOUT_BULK_RECALL, &lo->plh_flags)) {
		dprintk("%s forget reply due to recall\n", __func__);
		goto out_forget_reply;
	}

	if (pnfs_layoutgets_blocked(lo, &res->stateid, 1)) {
		dprintk("%s forget reply due to state\n", __func__);
		goto out_forget_reply;
	}
	init_lseg(lo, lseg);
	lseg->pls_range = res->range;
	*lgp->lsegpp = lseg;
	pnfs_insert_layout(lo, lseg);

	if (res->return_on_close) {
		set_bit(NFS_LSEG_ROC, &lseg->pls_flags);
		set_bit(NFS_LAYOUT_ROC, &lo->plh_flags);
	}

	/* Done processing layoutget. Set the layout stateid */
	pnfs_set_layout_stateid(lo, &res->stateid, false);
	spin_unlock(&ino->i_lock);
out:
	return status;

out_forget_reply:
	spin_unlock(&ino->i_lock);
	lseg->pls_layout = lo;
	NFS_SERVER(ino)->pnfs_curr_ld->free_lseg(lseg);
	goto out;
}

/*
 * Device ID cache. Currently supports one layout type per struct nfs_client.
 * Add layout type to the lookup key to expand to support multiple types.
 */
int
pnfs_alloc_init_deviceid_cache(struct nfs_client *clp,
			 void (*free_callback)(struct pnfs_deviceid_node *))
{
	struct pnfs_deviceid_cache *c;

	c = kzalloc(sizeof(struct pnfs_deviceid_cache), GFP_KERNEL);
	if (!c)
		return -ENOMEM;
	spin_lock(&clp->cl_lock);
	if (clp->cl_devid_cache != NULL) {
		atomic_inc(&clp->cl_devid_cache->dc_ref);
		dprintk("%s [kref [%d]]\n", __func__,
			atomic_read(&clp->cl_devid_cache->dc_ref));
		kfree(c);
	} else {
		/* kzalloc initializes hlists */
		spin_lock_init(&c->dc_lock);
		atomic_set(&c->dc_ref, 1);
		c->dc_free_callback = free_callback;
		clp->cl_devid_cache = c;
		dprintk("%s [new]\n", __func__);
	}
	spin_unlock(&clp->cl_lock);
	return 0;
}
EXPORT_SYMBOL_GPL(pnfs_alloc_init_deviceid_cache);

/*
 * Called from pnfs_layoutdriver_type->free_lseg
 * last layout segment reference frees deviceid
 */
void
pnfs_put_deviceid(struct pnfs_deviceid_cache *c,
		  struct pnfs_deviceid_node *devid)
{
	struct nfs4_deviceid *id = &devid->de_id;
	struct pnfs_deviceid_node *d;
	struct hlist_node *n;
	long h = nfs4_deviceid_hash(id);

	dprintk("%s [%d]\n", __func__, atomic_read(&devid->de_ref));
	if (!atomic_dec_and_lock(&devid->de_ref, &c->dc_lock))
		return;

	hlist_for_each_entry_rcu(d, n, &c->dc_deviceids[h], de_node)
		if (!memcmp(&d->de_id, id, sizeof(*id))) {
			hlist_del_rcu(&d->de_node);
			spin_unlock(&c->dc_lock);
			synchronize_rcu();
			c->dc_free_callback(devid);
			return;
		}
	spin_unlock(&c->dc_lock);
	/* Why wasn't it found in  the list? */
	BUG();
}
EXPORT_SYMBOL_GPL(pnfs_put_deviceid);

/* Find and reference a deviceid */
struct pnfs_deviceid_node *
pnfs_find_get_deviceid(struct pnfs_deviceid_cache *c, struct nfs4_deviceid *id)
{
	struct pnfs_deviceid_node *d;
	struct hlist_node *n;
	long hash = nfs4_deviceid_hash(id);

	dprintk("--> %s hash %ld\n", __func__, hash);
	rcu_read_lock();
	hlist_for_each_entry_rcu(d, n, &c->dc_deviceids[hash], de_node) {
		if (!memcmp(&d->de_id, id, sizeof(*id))) {
			if (!atomic_inc_not_zero(&d->de_ref)) {
				goto fail;
			} else {
				rcu_read_unlock();
				return d;
			}
		}
	}
fail:
	rcu_read_unlock();
	return NULL;
}
EXPORT_SYMBOL_GPL(pnfs_find_get_deviceid);

/*
 * Add a deviceid to the cache.
 * GETDEVICEINFOs for same deviceid can race. If deviceid is found, discard new
 */
struct pnfs_deviceid_node *
pnfs_add_deviceid(struct pnfs_deviceid_cache *c, struct pnfs_deviceid_node *new)
{
	struct pnfs_deviceid_node *d;
	long hash = nfs4_deviceid_hash(&new->de_id);

	dprintk("--> %s hash %ld\n", __func__, hash);
	spin_lock(&c->dc_lock);
	d = pnfs_find_get_deviceid(c, &new->de_id);
	if (d) {
		spin_unlock(&c->dc_lock);
		dprintk("%s [discard]\n", __func__);
		c->dc_free_callback(new);
		return d;
	}
	INIT_HLIST_NODE(&new->de_node);
	atomic_set(&new->de_ref, 1);
	hlist_add_head_rcu(&new->de_node, &c->dc_deviceids[hash]);
	spin_unlock(&c->dc_lock);
	dprintk("%s [new]\n", __func__);
	return new;
}
EXPORT_SYMBOL_GPL(pnfs_add_deviceid);

void
pnfs_put_deviceid_cache(struct nfs_client *clp)
{
	struct pnfs_deviceid_cache *local = clp->cl_devid_cache;

	dprintk("--> %s cl_devid_cache %p\n", __func__, clp->cl_devid_cache);
	if (atomic_dec_and_lock(&local->dc_ref, &clp->cl_lock)) {
		int i;
		/* Verify cache is empty */
		for (i = 0; i < NFS4_DEVICE_ID_HASH_SIZE; i++)
			BUG_ON(!hlist_empty(&local->dc_deviceids[i]));
		clp->cl_devid_cache = NULL;
		spin_unlock(&clp->cl_lock);
		kfree(local);
	}
}
EXPORT_SYMBOL_GPL(pnfs_put_deviceid_cache);
