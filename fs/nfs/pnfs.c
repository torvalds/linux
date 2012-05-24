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
#include <linux/nfs_page.h>
#include <linux/module.h>
#include "internal.h"
#include "pnfs.h"
#include "iostat.h"

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
		if (nfss->pnfs_curr_ld->clear_layoutdriver)
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
set_pnfs_layoutdriver(struct nfs_server *server, const struct nfs_fh *mntfh,
		      u32 id)
{
	struct pnfs_layoutdriver_type *ld_type = NULL;

	if (id == 0)
		goto out_no_driver;
	if (!(server->nfs_client->cl_exchange_flags &
		 (EXCHGID4_FLAG_USE_NON_PNFS | EXCHGID4_FLAG_USE_PNFS_MDS))) {
		printk(KERN_ERR "NFS: %s: id %u cl_exchange_flags 0x%x\n",
			__func__, id, server->nfs_client->cl_exchange_flags);
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
	if (ld_type->set_layoutdriver
	    && ld_type->set_layoutdriver(server, mntfh)) {
		printk(KERN_ERR "NFS: %s: Error initializing pNFS layout "
			"driver %u.\n", __func__, id);
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
		printk(KERN_ERR "NFS: %s id 0 is reserved\n", __func__);
		return status;
	}
	if (!ld_type->alloc_lseg || !ld_type->free_lseg) {
		printk(KERN_ERR "NFS: %s Layout driver must provide "
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
		printk(KERN_ERR "NFS: %s Module with id %d already loaded!\n",
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

static struct pnfs_layout_hdr *
pnfs_alloc_layout_hdr(struct inode *ino, gfp_t gfp_flags)
{
	struct pnfs_layoutdriver_type *ld = NFS_SERVER(ino)->pnfs_curr_ld;
	return ld->alloc_layout_hdr ? ld->alloc_layout_hdr(ino, gfp_flags) :
		kzalloc(sizeof(struct pnfs_layout_hdr), gfp_flags);
}

static void
pnfs_free_layout_hdr(struct pnfs_layout_hdr *lo)
{
	struct pnfs_layoutdriver_type *ld = NFS_SERVER(lo->plh_inode)->pnfs_curr_ld;
	put_rpccred(lo->plh_lc_cred);
	return ld->alloc_layout_hdr ? ld->free_layout_hdr(lo) : kfree(lo);
}

static void
destroy_layout_hdr(struct pnfs_layout_hdr *lo)
{
	dprintk("%s: freeing layout cache %p\n", __func__, lo);
	BUG_ON(!list_empty(&lo->plh_layouts));
	NFS_I(lo->plh_inode)->layout = NULL;
	pnfs_free_layout_hdr(lo);
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
	INIT_LIST_HEAD(&lseg->pls_lc_list);
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

static void
put_lseg_common(struct pnfs_layout_segment *lseg)
{
	struct inode *inode = lseg->pls_layout->plh_inode;

	WARN_ON(test_bit(NFS_LSEG_VALID, &lseg->pls_flags));
	list_del_init(&lseg->pls_list);
	if (list_empty(&lseg->pls_layout->plh_segs)) {
		set_bit(NFS_LAYOUT_DESTROYED, &lseg->pls_layout->plh_flags);
		/* Matched by initial refcount set in alloc_init_layout_hdr */
		put_layout_hdr_locked(lseg->pls_layout);
	}
	rpc_wake_up(&NFS_SERVER(inode)->roc_rpcwaitq);
}

void
put_lseg(struct pnfs_layout_segment *lseg)
{
	struct inode *inode;

	if (!lseg)
		return;

	dprintk("%s: lseg %p ref %d valid %d\n", __func__, lseg,
		atomic_read(&lseg->pls_refcount),
		test_bit(NFS_LSEG_VALID, &lseg->pls_flags));
	inode = lseg->pls_layout->plh_inode;
	if (atomic_dec_and_lock(&lseg->pls_refcount, &inode->i_lock)) {
		LIST_HEAD(free_me);

		put_lseg_common(lseg);
		list_add(&lseg->pls_list, &free_me);
		spin_unlock(&inode->i_lock);
		pnfs_free_lseg_list(&free_me);
	}
}
EXPORT_SYMBOL_GPL(put_lseg);

static inline u64
end_offset(u64 start, u64 len)
{
	u64 end;

	end = start + len;
	return end >= start ? end : NFS4_MAX_UINT64;
}

/* last octet in a range */
static inline u64
last_byte_offset(u64 start, u64 len)
{
	u64 end;

	BUG_ON(!len);
	end = start + len;
	return end > start ? end - 1 : NFS4_MAX_UINT64;
}

/*
 * is l2 fully contained in l1?
 *   start1                             end1
 *   [----------------------------------)
 *           start2           end2
 *           [----------------)
 */
static inline int
lo_seg_contained(struct pnfs_layout_range *l1,
		 struct pnfs_layout_range *l2)
{
	u64 start1 = l1->offset;
	u64 end1 = end_offset(start1, l1->length);
	u64 start2 = l2->offset;
	u64 end2 = end_offset(start2, l2->length);

	return (start1 <= start2) && (end1 >= end2);
}

/*
 * is l1 and l2 intersecting?
 *   start1                             end1
 *   [----------------------------------)
 *                              start2           end2
 *                              [----------------)
 */
static inline int
lo_seg_intersecting(struct pnfs_layout_range *l1,
		    struct pnfs_layout_range *l2)
{
	u64 start1 = l1->offset;
	u64 end1 = end_offset(start1, l1->length);
	u64 start2 = l2->offset;
	u64 end2 = end_offset(start2, l2->length);

	return (end1 == NFS4_MAX_UINT64 || end1 > start2) &&
	       (end2 == NFS4_MAX_UINT64 || end2 > start1);
}

static bool
should_free_lseg(struct pnfs_layout_range *lseg_range,
		 struct pnfs_layout_range *recall_range)
{
	return (recall_range->iomode == IOMODE_ANY ||
		lseg_range->iomode == recall_range->iomode) &&
	       lo_seg_intersecting(lseg_range, recall_range);
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
		dprintk("%s: lseg %p ref %d\n", __func__, lseg,
			atomic_read(&lseg->pls_refcount));
		if (atomic_dec_and_test(&lseg->pls_refcount)) {
			put_lseg_common(lseg);
			list_add(&lseg->pls_list, tmp_list);
			rv = 1;
		}
	}
	return rv;
}

/* Returns count of number of matching invalid lsegs remaining in list
 * after call.
 */
int
mark_matching_lsegs_invalid(struct pnfs_layout_hdr *lo,
			    struct list_head *tmp_list,
			    struct pnfs_layout_range *recall_range)
{
	struct pnfs_layout_segment *lseg, *next;
	int invalid = 0, removed = 0;

	dprintk("%s:Begin lo %p\n", __func__, lo);

	if (list_empty(&lo->plh_segs)) {
		/* Reset MDS Threshold I/O counters */
		NFS_I(lo->plh_inode)->write_io = 0;
		NFS_I(lo->plh_inode)->read_io = 0;
		if (!test_and_set_bit(NFS_LAYOUT_DESTROYED, &lo->plh_flags))
			put_layout_hdr_locked(lo);
		return 0;
	}
	list_for_each_entry_safe(lseg, next, &lo->plh_segs, pls_list)
		if (!recall_range ||
		    should_free_lseg(&lseg->pls_range, recall_range)) {
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

/* note free_me must contain lsegs from a single layout_hdr */
void
pnfs_free_lseg_list(struct list_head *free_me)
{
	struct pnfs_layout_segment *lseg, *tmp;
	struct pnfs_layout_hdr *lo;

	if (list_empty(free_me))
		return;

	lo = list_first_entry(free_me, struct pnfs_layout_segment,
			      pls_list)->pls_layout;

	if (test_bit(NFS_LAYOUT_DESTROYED, &lo->plh_flags)) {
		struct nfs_client *clp;

		clp = NFS_SERVER(lo->plh_inode)->nfs_client;
		spin_lock(&clp->cl_lock);
		list_del_init(&lo->plh_layouts);
		spin_unlock(&clp->cl_lock);
	}
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
		lo->plh_block_lgets++; /* permanently block new LAYOUTGETs */
		mark_matching_lsegs_invalid(lo, &tmp_list, NULL);
	}
	spin_unlock(&nfsi->vfs_inode.i_lock);
	pnfs_free_lseg_list(&tmp_list);
}
EXPORT_SYMBOL_GPL(pnfs_destroy_layout);

/*
 * Called by the state manger to remove all layouts established under an
 * expired lease.
 */
void
pnfs_destroy_all_layouts(struct nfs_client *clp)
{
	struct nfs_server *server;
	struct pnfs_layout_hdr *lo;
	LIST_HEAD(tmp_list);

	nfs4_deviceid_mark_client_invalid(clp);
	nfs4_deviceid_purge_client(clp);

	spin_lock(&clp->cl_lock);
	rcu_read_lock();
	list_for_each_entry_rcu(server, &clp->cl_superblocks, client_link) {
		if (!list_empty(&server->layouts))
			list_splice_init(&server->layouts, &tmp_list);
	}
	rcu_read_unlock();
	spin_unlock(&clp->cl_lock);

	while (!list_empty(&tmp_list)) {
		lo = list_entry(tmp_list.next, struct pnfs_layout_hdr,
				plh_layouts);
		dprintk("%s freeing layout for inode %lu\n", __func__,
			lo->plh_inode->i_ino);
		list_del_init(&lo->plh_layouts);
		pnfs_destroy_layout(NFS_I(lo->plh_inode));
	}
}

/* update lo->plh_stateid with new if is more recent */
void
pnfs_set_layout_stateid(struct pnfs_layout_hdr *lo, const nfs4_stateid *new,
			bool update_barrier)
{
	u32 oldseq, newseq;

	oldseq = be32_to_cpu(lo->plh_stateid.seqid);
	newseq = be32_to_cpu(new->seqid);
	if ((int)(newseq - oldseq) > 0) {
		nfs4_stateid_copy(&lo->plh_stateid, new);
		if (update_barrier) {
			u32 new_barrier = be32_to_cpu(new->seqid);

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
	    (int)(lo->plh_barrier - be32_to_cpu(stateid->seqid)) >= 0)
		return true;
	return lo->plh_block_lgets ||
		test_bit(NFS_LAYOUT_DESTROYED, &lo->plh_flags) ||
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
			nfs4_stateid_copy(dst, &open_state->stateid);
		} while (read_seqretry(&open_state->seqlock, seq));
	} else
		nfs4_stateid_copy(dst, &lo->plh_stateid);
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
	   struct pnfs_layout_range *range,
	   gfp_t gfp_flags)
{
	struct inode *ino = lo->plh_inode;
	struct nfs_server *server = NFS_SERVER(ino);
	struct nfs4_layoutget *lgp;
	struct pnfs_layout_segment *lseg = NULL;
	struct page **pages = NULL;
	int i;
	u32 max_resp_sz, max_pages;

	dprintk("--> %s\n", __func__);

	BUG_ON(ctx == NULL);
	lgp = kzalloc(sizeof(*lgp), gfp_flags);
	if (lgp == NULL)
		return NULL;

	/* allocate pages for xdr post processing */
	max_resp_sz = server->nfs_client->cl_session->fc_attrs.max_resp_sz;
	max_pages = nfs_page_array_len(0, max_resp_sz);

	pages = kcalloc(max_pages, sizeof(struct page *), gfp_flags);
	if (!pages)
		goto out_err_free;

	for (i = 0; i < max_pages; i++) {
		pages[i] = alloc_page(gfp_flags);
		if (!pages[i])
			goto out_err_free;
	}

	lgp->args.minlength = PAGE_CACHE_SIZE;
	if (lgp->args.minlength > range->length)
		lgp->args.minlength = range->length;
	lgp->args.maxcount = PNFS_LAYOUT_MAXSIZE;
	lgp->args.range = *range;
	lgp->args.type = server->pnfs_curr_ld->id;
	lgp->args.inode = ino;
	lgp->args.ctx = get_nfs_open_context(ctx);
	lgp->args.layout.pages = pages;
	lgp->args.layout.pglen = max_pages * PAGE_SIZE;
	lgp->lsegpp = &lseg;
	lgp->gfp_flags = gfp_flags;

	/* Synchronously retrieve layout information from server and
	 * store in lseg.
	 */
	nfs4_proc_layoutget(lgp);
	if (!lseg) {
		/* remember that LAYOUTGET failed and suspend trying */
		set_bit(lo_fail_bit(range->iomode), &lo->plh_flags);
	}

	/* free xdr pages */
	for (i = 0; i < max_pages; i++)
		__free_page(pages[i]);
	kfree(pages);

	return lseg;

out_err_free:
	/* free any allocated xdr pages, lgp as it's not used */
	if (pages) {
		for (i = 0; i < max_pages; i++) {
			if (!pages[i])
				break;
			__free_page(pages[i]);
		}
		kfree(pages);
	}
	kfree(lgp);
	return NULL;
}

/* Initiates a LAYOUTRETURN(FILE) */
int
_pnfs_return_layout(struct inode *ino)
{
	struct pnfs_layout_hdr *lo = NULL;
	struct nfs_inode *nfsi = NFS_I(ino);
	LIST_HEAD(tmp_list);
	struct nfs4_layoutreturn *lrp;
	nfs4_stateid stateid;
	int status = 0;

	dprintk("--> %s\n", __func__);

	spin_lock(&ino->i_lock);
	lo = nfsi->layout;
	if (!lo) {
		spin_unlock(&ino->i_lock);
		dprintk("%s: no layout to return\n", __func__);
		return status;
	}
	stateid = nfsi->layout->plh_stateid;
	/* Reference matched in nfs4_layoutreturn_release */
	get_layout_hdr(lo);
	mark_matching_lsegs_invalid(lo, &tmp_list, NULL);
	lo->plh_block_lgets++;
	spin_unlock(&ino->i_lock);
	pnfs_free_lseg_list(&tmp_list);

	WARN_ON(test_bit(NFS_INO_LAYOUTCOMMIT, &nfsi->flags));

	lrp = kzalloc(sizeof(*lrp), GFP_KERNEL);
	if (unlikely(lrp == NULL)) {
		status = -ENOMEM;
		set_bit(NFS_LAYOUT_RW_FAILED, &lo->plh_flags);
		set_bit(NFS_LAYOUT_RO_FAILED, &lo->plh_flags);
		put_layout_hdr(lo);
		goto out;
	}

	lrp->args.stateid = stateid;
	lrp->args.layout_type = NFS_SERVER(ino)->pnfs_curr_ld->id;
	lrp->args.inode = ino;
	lrp->args.layout = lo;
	lrp->clp = NFS_SERVER(ino)->nfs_client;

	status = nfs4_proc_layoutreturn(lrp);
out:
	dprintk("<-- %s status: %d\n", __func__, status);
	return status;
}
EXPORT_SYMBOL_GPL(_pnfs_return_layout);

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
		u32 current_seqid = be32_to_cpu(lo->plh_stateid.seqid);

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
cmp_layout(struct pnfs_layout_range *l1,
	   struct pnfs_layout_range *l2)
{
	s64 d;

	/* high offset > low offset */
	d = l1->offset - l2->offset;
	if (d)
		return d;

	/* short length > long length */
	d = l2->length - l1->length;
	if (d)
		return d;

	/* read > read/write */
	return (int)(l1->iomode == IOMODE_READ) - (int)(l2->iomode == IOMODE_READ);
}

static void
pnfs_insert_layout(struct pnfs_layout_hdr *lo,
		   struct pnfs_layout_segment *lseg)
{
	struct pnfs_layout_segment *lp;

	dprintk("%s:Begin\n", __func__);

	assert_spin_locked(&lo->plh_inode->i_lock);
	list_for_each_entry(lp, &lo->plh_segs, pls_list) {
		if (cmp_layout(&lseg->pls_range, &lp->pls_range) > 0)
			continue;
		list_add_tail(&lseg->pls_list, &lp->pls_list);
		dprintk("%s: inserted lseg %p "
			"iomode %d offset %llu length %llu before "
			"lp %p iomode %d offset %llu length %llu\n",
			__func__, lseg, lseg->pls_range.iomode,
			lseg->pls_range.offset, lseg->pls_range.length,
			lp, lp->pls_range.iomode, lp->pls_range.offset,
			lp->pls_range.length);
		goto out;
	}
	list_add_tail(&lseg->pls_list, &lo->plh_segs);
	dprintk("%s: inserted lseg %p "
		"iomode %d offset %llu length %llu at tail\n",
		__func__, lseg, lseg->pls_range.iomode,
		lseg->pls_range.offset, lseg->pls_range.length);
out:
	get_layout_hdr(lo);

	dprintk("%s:Return\n", __func__);
}

static struct pnfs_layout_hdr *
alloc_init_layout_hdr(struct inode *ino,
		      struct nfs_open_context *ctx,
		      gfp_t gfp_flags)
{
	struct pnfs_layout_hdr *lo;

	lo = pnfs_alloc_layout_hdr(ino, gfp_flags);
	if (!lo)
		return NULL;
	atomic_set(&lo->plh_refcount, 1);
	INIT_LIST_HEAD(&lo->plh_layouts);
	INIT_LIST_HEAD(&lo->plh_segs);
	INIT_LIST_HEAD(&lo->plh_bulk_recall);
	lo->plh_inode = ino;
	lo->plh_lc_cred = get_rpccred(ctx->state->owner->so_cred);
	return lo;
}

static struct pnfs_layout_hdr *
pnfs_find_alloc_layout(struct inode *ino,
		       struct nfs_open_context *ctx,
		       gfp_t gfp_flags)
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
	new = alloc_init_layout_hdr(ino, ctx, gfp_flags);
	spin_lock(&ino->i_lock);

	if (likely(nfsi->layout == NULL))	/* Won the race? */
		nfsi->layout = new;
	else
		pnfs_free_layout_hdr(new);
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
is_matching_lseg(struct pnfs_layout_range *ls_range,
		 struct pnfs_layout_range *range)
{
	struct pnfs_layout_range range1;

	if ((range->iomode == IOMODE_RW &&
	     ls_range->iomode != IOMODE_RW) ||
	    !lo_seg_intersecting(ls_range, range))
		return 0;

	/* range1 covers only the first byte in the range */
	range1 = *range;
	range1.length = 1;
	return lo_seg_contained(ls_range, &range1);
}

/*
 * lookup range in layout
 */
static struct pnfs_layout_segment *
pnfs_find_lseg(struct pnfs_layout_hdr *lo,
		struct pnfs_layout_range *range)
{
	struct pnfs_layout_segment *lseg, *ret = NULL;

	dprintk("%s:Begin\n", __func__);

	assert_spin_locked(&lo->plh_inode->i_lock);
	list_for_each_entry(lseg, &lo->plh_segs, pls_list) {
		if (test_bit(NFS_LSEG_VALID, &lseg->pls_flags) &&
		    is_matching_lseg(&lseg->pls_range, range)) {
			ret = get_lseg(lseg);
			break;
		}
		if (lseg->pls_range.offset > range->offset)
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
		   loff_t pos,
		   u64 count,
		   enum pnfs_iomode iomode,
		   gfp_t gfp_flags)
{
	struct pnfs_layout_range arg = {
		.iomode = iomode,
		.offset = pos,
		.length = count,
	};
	unsigned pg_offset;
	struct nfs_inode *nfsi = NFS_I(ino);
	struct nfs_server *server = NFS_SERVER(ino);
	struct nfs_client *clp = server->nfs_client;
	struct pnfs_layout_hdr *lo;
	struct pnfs_layout_segment *lseg = NULL;
	bool first = false;

	if (!pnfs_enabled_sb(NFS_SERVER(ino)))
		return NULL;
	spin_lock(&ino->i_lock);
	lo = pnfs_find_alloc_layout(ino, ctx, gfp_flags);
	if (lo == NULL) {
		dprintk("%s ERROR: can't get pnfs_layout_hdr\n", __func__);
		goto out_unlock;
	}

	/* Do we even need to bother with this? */
	if (test_bit(NFS_LAYOUT_BULK_RECALL, &lo->plh_flags)) {
		dprintk("%s matches recall, use MDS\n", __func__);
		goto out_unlock;
	}

	/* if LAYOUTGET already failed once we don't try again */
	if (test_bit(lo_fail_bit(iomode), &nfsi->layout->plh_flags))
		goto out_unlock;

	/* Check to see if the layout for the given range already exists */
	lseg = pnfs_find_lseg(lo, &arg);
	if (lseg)
		goto out_unlock;

	if (pnfs_layoutgets_blocked(lo, NULL, 0))
		goto out_unlock;
	atomic_inc(&lo->plh_outstanding);

	get_layout_hdr(lo);
	if (list_empty(&lo->plh_segs))
		first = true;
	spin_unlock(&ino->i_lock);
	if (first) {
		/* The lo must be on the clp list if there is any
		 * chance of a CB_LAYOUTRECALL(FILE) coming in.
		 */
		spin_lock(&clp->cl_lock);
		BUG_ON(!list_empty(&lo->plh_layouts));
		list_add_tail(&lo->plh_layouts, &server->layouts);
		spin_unlock(&clp->cl_lock);
	}

	pg_offset = arg.offset & ~PAGE_CACHE_MASK;
	if (pg_offset) {
		arg.offset -= pg_offset;
		arg.length += pg_offset;
	}
	if (arg.length != NFS4_MAX_UINT64)
		arg.length = PAGE_CACHE_ALIGN(arg.length);

	lseg = send_layoutget(lo, ctx, &arg, gfp_flags);
	if (!lseg && first) {
		spin_lock(&clp->cl_lock);
		list_del_init(&lo->plh_layouts);
		spin_unlock(&clp->cl_lock);
	}
	atomic_dec(&lo->plh_outstanding);
	put_layout_hdr(lo);
out:
	dprintk("%s end, state 0x%lx lseg %p\n", __func__,
		nfsi->layout ? nfsi->layout->plh_flags : -1, lseg);
	return lseg;
out_unlock:
	spin_unlock(&ino->i_lock);
	goto out;
}
EXPORT_SYMBOL_GPL(pnfs_update_layout);

int
pnfs_layout_process(struct nfs4_layoutget *lgp)
{
	struct pnfs_layout_hdr *lo = NFS_I(lgp->args.inode)->layout;
	struct nfs4_layoutget_res *res = &lgp->res;
	struct pnfs_layout_segment *lseg;
	struct inode *ino = lo->plh_inode;
	int status = 0;

	/* Inject layout blob into I/O device driver */
	lseg = NFS_SERVER(ino)->pnfs_curr_ld->alloc_lseg(lo, res, lgp->gfp_flags);
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
	if (test_bit(NFS_LAYOUT_BULK_RECALL, &lo->plh_flags)) {
		dprintk("%s forget reply due to recall\n", __func__);
		goto out_forget_reply;
	}

	if (pnfs_layoutgets_blocked(lo, &res->stateid, 1)) {
		dprintk("%s forget reply due to state\n", __func__);
		goto out_forget_reply;
	}
	init_lseg(lo, lseg);
	lseg->pls_range = res->range;
	*lgp->lsegpp = get_lseg(lseg);
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

void
pnfs_generic_pg_init_read(struct nfs_pageio_descriptor *pgio, struct nfs_page *req)
{
	BUG_ON(pgio->pg_lseg != NULL);

	if (req->wb_offset != req->wb_pgbase) {
		nfs_pageio_reset_read_mds(pgio);
		return;
	}
	pgio->pg_lseg = pnfs_update_layout(pgio->pg_inode,
					   req->wb_context,
					   req_offset(req),
					   req->wb_bytes,
					   IOMODE_READ,
					   GFP_KERNEL);
	/* If no lseg, fall back to read through mds */
	if (pgio->pg_lseg == NULL)
		nfs_pageio_reset_read_mds(pgio);

}
EXPORT_SYMBOL_GPL(pnfs_generic_pg_init_read);

void
pnfs_generic_pg_init_write(struct nfs_pageio_descriptor *pgio, struct nfs_page *req)
{
	BUG_ON(pgio->pg_lseg != NULL);

	if (req->wb_offset != req->wb_pgbase) {
		nfs_pageio_reset_write_mds(pgio);
		return;
	}
	pgio->pg_lseg = pnfs_update_layout(pgio->pg_inode,
					   req->wb_context,
					   req_offset(req),
					   req->wb_bytes,
					   IOMODE_RW,
					   GFP_NOFS);
	/* If no lseg, fall back to write through mds */
	if (pgio->pg_lseg == NULL)
		nfs_pageio_reset_write_mds(pgio);
}
EXPORT_SYMBOL_GPL(pnfs_generic_pg_init_write);

bool
pnfs_pageio_init_read(struct nfs_pageio_descriptor *pgio, struct inode *inode,
		      const struct nfs_pgio_completion_ops *compl_ops)
{
	struct nfs_server *server = NFS_SERVER(inode);
	struct pnfs_layoutdriver_type *ld = server->pnfs_curr_ld;

	if (ld == NULL)
		return false;
	nfs_pageio_init(pgio, inode, ld->pg_read_ops, compl_ops,
			server->rsize, 0);
	return true;
}

bool
pnfs_pageio_init_write(struct nfs_pageio_descriptor *pgio, struct inode *inode,
		       int ioflags,
		       const struct nfs_pgio_completion_ops *compl_ops)
{
	struct nfs_server *server = NFS_SERVER(inode);
	struct pnfs_layoutdriver_type *ld = server->pnfs_curr_ld;

	if (ld == NULL)
		return false;
	nfs_pageio_init(pgio, inode, ld->pg_write_ops, compl_ops,
			server->wsize, ioflags);
	return true;
}

bool
pnfs_generic_pg_test(struct nfs_pageio_descriptor *pgio, struct nfs_page *prev,
		     struct nfs_page *req)
{
	if (pgio->pg_lseg == NULL)
		return nfs_generic_pg_test(pgio, prev, req);

	/*
	 * Test if a nfs_page is fully contained in the pnfs_layout_range.
	 * Note that this test makes several assumptions:
	 * - that the previous nfs_page in the struct nfs_pageio_descriptor
	 *   is known to lie within the range.
	 *   - that the nfs_page being tested is known to be contiguous with the
	 *   previous nfs_page.
	 *   - Layout ranges are page aligned, so we only have to test the
	 *   start offset of the request.
	 *
	 * Please also note that 'end_offset' is actually the offset of the
	 * first byte that lies outside the pnfs_layout_range. FIXME?
	 *
	 */
	return req_offset(req) < end_offset(pgio->pg_lseg->pls_range.offset,
					 pgio->pg_lseg->pls_range.length);
}
EXPORT_SYMBOL_GPL(pnfs_generic_pg_test);

int pnfs_write_done_resend_to_mds(struct inode *inode,
				struct list_head *head,
				const struct nfs_pgio_completion_ops *compl_ops)
{
	struct nfs_pageio_descriptor pgio;
	LIST_HEAD(failed);

	/* Resend all requests through the MDS */
	nfs_pageio_init_write_mds(&pgio, inode, FLUSH_STABLE, compl_ops);
	while (!list_empty(head)) {
		struct nfs_page *req = nfs_list_entry(head->next);

		nfs_list_remove_request(req);
		if (!nfs_pageio_add_request(&pgio, req))
			nfs_list_add_request(req, &failed);
	}
	nfs_pageio_complete(&pgio);

	if (!list_empty(&failed)) {
		/* For some reason our attempt to resend pages. Mark the
		 * overall send request as having failed, and let
		 * nfs_writeback_release_full deal with the error.
		 */
		list_move(&failed, head);
		return -EIO;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(pnfs_write_done_resend_to_mds);

static void pnfs_ld_handle_write_error(struct nfs_write_data *data)
{
	struct nfs_pgio_header *hdr = data->header;

	dprintk("pnfs write error = %d\n", hdr->pnfs_error);
	if (NFS_SERVER(hdr->inode)->pnfs_curr_ld->flags &
	    PNFS_LAYOUTRET_ON_ERROR) {
		clear_bit(NFS_INO_LAYOUTCOMMIT, &NFS_I(hdr->inode)->flags);
		pnfs_return_layout(hdr->inode);
	}
	if (!test_and_set_bit(NFS_IOHDR_REDO, &hdr->flags))
		data->task.tk_status = pnfs_write_done_resend_to_mds(hdr->inode,
							&hdr->pages,
							hdr->completion_ops);
}

/*
 * Called by non rpc-based layout drivers
 */
void pnfs_ld_write_done(struct nfs_write_data *data)
{
	struct nfs_pgio_header *hdr = data->header;

	if (!hdr->pnfs_error) {
		pnfs_set_layoutcommit(data);
		hdr->mds_ops->rpc_call_done(&data->task, data);
	} else
		pnfs_ld_handle_write_error(data);
	hdr->mds_ops->rpc_release(data);
}
EXPORT_SYMBOL_GPL(pnfs_ld_write_done);

static void
pnfs_write_through_mds(struct nfs_pageio_descriptor *desc,
		struct nfs_write_data *data)
{
	struct nfs_pgio_header *hdr = data->header;

	if (!test_and_set_bit(NFS_IOHDR_REDO, &hdr->flags)) {
		list_splice_tail_init(&hdr->pages, &desc->pg_list);
		nfs_pageio_reset_write_mds(desc);
		desc->pg_recoalesce = 1;
	}
	nfs_writedata_release(data);
}

static enum pnfs_try_status
pnfs_try_to_write_data(struct nfs_write_data *wdata,
			const struct rpc_call_ops *call_ops,
			struct pnfs_layout_segment *lseg,
			int how)
{
	struct nfs_pgio_header *hdr = wdata->header;
	struct inode *inode = hdr->inode;
	enum pnfs_try_status trypnfs;
	struct nfs_server *nfss = NFS_SERVER(inode);

	hdr->mds_ops = call_ops;

	dprintk("%s: Writing ino:%lu %u@%llu (how %d)\n", __func__,
		inode->i_ino, wdata->args.count, wdata->args.offset, how);
	trypnfs = nfss->pnfs_curr_ld->write_pagelist(wdata, how);
	if (trypnfs != PNFS_NOT_ATTEMPTED)
		nfs_inc_stats(inode, NFSIOS_PNFS_WRITE);
	dprintk("%s End (trypnfs:%d)\n", __func__, trypnfs);
	return trypnfs;
}

static void
pnfs_do_multiple_writes(struct nfs_pageio_descriptor *desc, struct list_head *head, int how)
{
	struct nfs_write_data *data;
	const struct rpc_call_ops *call_ops = desc->pg_rpc_callops;
	struct pnfs_layout_segment *lseg = desc->pg_lseg;

	desc->pg_lseg = NULL;
	while (!list_empty(head)) {
		enum pnfs_try_status trypnfs;

		data = list_first_entry(head, struct nfs_write_data, list);
		list_del_init(&data->list);

		trypnfs = pnfs_try_to_write_data(data, call_ops, lseg, how);
		if (trypnfs == PNFS_NOT_ATTEMPTED)
			pnfs_write_through_mds(desc, data);
	}
	put_lseg(lseg);
}

static void pnfs_writehdr_free(struct nfs_pgio_header *hdr)
{
	put_lseg(hdr->lseg);
	nfs_writehdr_free(hdr);
}

int
pnfs_generic_pg_writepages(struct nfs_pageio_descriptor *desc)
{
	struct nfs_write_header *whdr;
	struct nfs_pgio_header *hdr;
	int ret;

	whdr = nfs_writehdr_alloc();
	if (!whdr) {
		desc->pg_completion_ops->error_cleanup(&desc->pg_list);
		put_lseg(desc->pg_lseg);
		desc->pg_lseg = NULL;
		return -ENOMEM;
	}
	hdr = &whdr->header;
	nfs_pgheader_init(desc, hdr, pnfs_writehdr_free);
	hdr->lseg = get_lseg(desc->pg_lseg);
	atomic_inc(&hdr->refcnt);
	ret = nfs_generic_flush(desc, hdr);
	if (ret != 0) {
		put_lseg(desc->pg_lseg);
		desc->pg_lseg = NULL;
	} else
		pnfs_do_multiple_writes(desc, &hdr->rpc_list, desc->pg_ioflags);
	if (atomic_dec_and_test(&hdr->refcnt))
		hdr->completion_ops->completion(hdr);
	return ret;
}
EXPORT_SYMBOL_GPL(pnfs_generic_pg_writepages);

int pnfs_read_done_resend_to_mds(struct inode *inode,
				struct list_head *head,
				const struct nfs_pgio_completion_ops *compl_ops)
{
	struct nfs_pageio_descriptor pgio;
	LIST_HEAD(failed);

	/* Resend all requests through the MDS */
	nfs_pageio_init_read_mds(&pgio, inode, compl_ops);
	while (!list_empty(head)) {
		struct nfs_page *req = nfs_list_entry(head->next);

		nfs_list_remove_request(req);
		if (!nfs_pageio_add_request(&pgio, req))
			nfs_list_add_request(req, &failed);
	}
	nfs_pageio_complete(&pgio);

	if (!list_empty(&failed)) {
		list_move(&failed, head);
		return -EIO;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(pnfs_read_done_resend_to_mds);

static void pnfs_ld_handle_read_error(struct nfs_read_data *data)
{
	struct nfs_pgio_header *hdr = data->header;

	dprintk("pnfs read error = %d\n", hdr->pnfs_error);
	if (NFS_SERVER(hdr->inode)->pnfs_curr_ld->flags &
	    PNFS_LAYOUTRET_ON_ERROR) {
		clear_bit(NFS_INO_LAYOUTCOMMIT, &NFS_I(hdr->inode)->flags);
		pnfs_return_layout(hdr->inode);
	}
	if (!test_and_set_bit(NFS_IOHDR_REDO, &hdr->flags))
		data->task.tk_status = pnfs_read_done_resend_to_mds(hdr->inode,
							&hdr->pages,
							hdr->completion_ops);
}

/*
 * Called by non rpc-based layout drivers
 */
void pnfs_ld_read_done(struct nfs_read_data *data)
{
	struct nfs_pgio_header *hdr = data->header;

	if (likely(!hdr->pnfs_error)) {
		__nfs4_read_done_cb(data);
		hdr->mds_ops->rpc_call_done(&data->task, data);
	} else
		pnfs_ld_handle_read_error(data);
	hdr->mds_ops->rpc_release(data);
}
EXPORT_SYMBOL_GPL(pnfs_ld_read_done);

static void
pnfs_read_through_mds(struct nfs_pageio_descriptor *desc,
		struct nfs_read_data *data)
{
	struct nfs_pgio_header *hdr = data->header;

	if (!test_and_set_bit(NFS_IOHDR_REDO, &hdr->flags)) {
		list_splice_tail_init(&hdr->pages, &desc->pg_list);
		nfs_pageio_reset_read_mds(desc);
		desc->pg_recoalesce = 1;
	}
	nfs_readdata_release(data);
}

/*
 * Call the appropriate parallel I/O subsystem read function.
 */
static enum pnfs_try_status
pnfs_try_to_read_data(struct nfs_read_data *rdata,
		       const struct rpc_call_ops *call_ops,
		       struct pnfs_layout_segment *lseg)
{
	struct nfs_pgio_header *hdr = rdata->header;
	struct inode *inode = hdr->inode;
	struct nfs_server *nfss = NFS_SERVER(inode);
	enum pnfs_try_status trypnfs;

	hdr->mds_ops = call_ops;

	dprintk("%s: Reading ino:%lu %u@%llu\n",
		__func__, inode->i_ino, rdata->args.count, rdata->args.offset);

	trypnfs = nfss->pnfs_curr_ld->read_pagelist(rdata);
	if (trypnfs != PNFS_NOT_ATTEMPTED)
		nfs_inc_stats(inode, NFSIOS_PNFS_READ);
	dprintk("%s End (trypnfs:%d)\n", __func__, trypnfs);
	return trypnfs;
}

static void
pnfs_do_multiple_reads(struct nfs_pageio_descriptor *desc, struct list_head *head)
{
	struct nfs_read_data *data;
	const struct rpc_call_ops *call_ops = desc->pg_rpc_callops;
	struct pnfs_layout_segment *lseg = desc->pg_lseg;

	desc->pg_lseg = NULL;
	while (!list_empty(head)) {
		enum pnfs_try_status trypnfs;

		data = list_first_entry(head, struct nfs_read_data, list);
		list_del_init(&data->list);

		trypnfs = pnfs_try_to_read_data(data, call_ops, lseg);
		if (trypnfs == PNFS_NOT_ATTEMPTED)
			pnfs_read_through_mds(desc, data);
	}
	put_lseg(lseg);
}

static void pnfs_readhdr_free(struct nfs_pgio_header *hdr)
{
	put_lseg(hdr->lseg);
	nfs_readhdr_free(hdr);
}

int
pnfs_generic_pg_readpages(struct nfs_pageio_descriptor *desc)
{
	struct nfs_read_header *rhdr;
	struct nfs_pgio_header *hdr;
	int ret;

	rhdr = nfs_readhdr_alloc();
	if (!rhdr) {
		desc->pg_completion_ops->error_cleanup(&desc->pg_list);
		ret = -ENOMEM;
		put_lseg(desc->pg_lseg);
		desc->pg_lseg = NULL;
		return ret;
	}
	hdr = &rhdr->header;
	nfs_pgheader_init(desc, hdr, pnfs_readhdr_free);
	hdr->lseg = get_lseg(desc->pg_lseg);
	atomic_inc(&hdr->refcnt);
	ret = nfs_generic_pagein(desc, hdr);
	if (ret != 0) {
		put_lseg(desc->pg_lseg);
		desc->pg_lseg = NULL;
	} else
		pnfs_do_multiple_reads(desc, &hdr->rpc_list);
	if (atomic_dec_and_test(&hdr->refcnt))
		hdr->completion_ops->completion(hdr);
	return ret;
}
EXPORT_SYMBOL_GPL(pnfs_generic_pg_readpages);

/*
 * There can be multiple RW segments.
 */
static void pnfs_list_write_lseg(struct inode *inode, struct list_head *listp)
{
	struct pnfs_layout_segment *lseg;

	list_for_each_entry(lseg, &NFS_I(inode)->layout->plh_segs, pls_list) {
		if (lseg->pls_range.iomode == IOMODE_RW &&
		    test_bit(NFS_LSEG_LAYOUTCOMMIT, &lseg->pls_flags))
			list_add(&lseg->pls_lc_list, listp);
	}
}

void pnfs_set_lo_fail(struct pnfs_layout_segment *lseg)
{
	if (lseg->pls_range.iomode == IOMODE_RW) {
		dprintk("%s Setting layout IOMODE_RW fail bit\n", __func__);
		set_bit(lo_fail_bit(IOMODE_RW), &lseg->pls_layout->plh_flags);
	} else {
		dprintk("%s Setting layout IOMODE_READ fail bit\n", __func__);
		set_bit(lo_fail_bit(IOMODE_READ), &lseg->pls_layout->plh_flags);
	}
}
EXPORT_SYMBOL_GPL(pnfs_set_lo_fail);

void
pnfs_set_layoutcommit(struct nfs_write_data *wdata)
{
	struct nfs_pgio_header *hdr = wdata->header;
	struct inode *inode = hdr->inode;
	struct nfs_inode *nfsi = NFS_I(inode);
	loff_t end_pos = wdata->mds_offset + wdata->res.count;
	bool mark_as_dirty = false;

	spin_lock(&inode->i_lock);
	if (!test_and_set_bit(NFS_INO_LAYOUTCOMMIT, &nfsi->flags)) {
		mark_as_dirty = true;
		dprintk("%s: Set layoutcommit for inode %lu ",
			__func__, inode->i_ino);
	}
	if (!test_and_set_bit(NFS_LSEG_LAYOUTCOMMIT, &hdr->lseg->pls_flags)) {
		/* references matched in nfs4_layoutcommit_release */
		get_lseg(hdr->lseg);
	}
	if (end_pos > nfsi->layout->plh_lwb)
		nfsi->layout->plh_lwb = end_pos;
	spin_unlock(&inode->i_lock);
	dprintk("%s: lseg %p end_pos %llu\n",
		__func__, hdr->lseg, nfsi->layout->plh_lwb);

	/* if pnfs_layoutcommit_inode() runs between inode locks, the next one
	 * will be a noop because NFS_INO_LAYOUTCOMMIT will not be set */
	if (mark_as_dirty)
		mark_inode_dirty_sync(inode);
}
EXPORT_SYMBOL_GPL(pnfs_set_layoutcommit);

void pnfs_cleanup_layoutcommit(struct nfs4_layoutcommit_data *data)
{
	struct nfs_server *nfss = NFS_SERVER(data->args.inode);

	if (nfss->pnfs_curr_ld->cleanup_layoutcommit)
		nfss->pnfs_curr_ld->cleanup_layoutcommit(data);
}

/*
 * For the LAYOUT4_NFSV4_1_FILES layout type, NFS_DATA_SYNC WRITEs and
 * NFS_UNSTABLE WRITEs with a COMMIT to data servers must store enough
 * data to disk to allow the server to recover the data if it crashes.
 * LAYOUTCOMMIT is only needed when the NFL4_UFLG_COMMIT_THRU_MDS flag
 * is off, and a COMMIT is sent to a data server, or
 * if WRITEs to a data server return NFS_DATA_SYNC.
 */
int
pnfs_layoutcommit_inode(struct inode *inode, bool sync)
{
	struct nfs4_layoutcommit_data *data;
	struct nfs_inode *nfsi = NFS_I(inode);
	loff_t end_pos;
	int status = 0;

	dprintk("--> %s inode %lu\n", __func__, inode->i_ino);

	if (!test_bit(NFS_INO_LAYOUTCOMMIT, &nfsi->flags))
		return 0;

	/* Note kzalloc ensures data->res.seq_res.sr_slot == NULL */
	data = kzalloc(sizeof(*data), GFP_NOFS);
	if (!data) {
		status = -ENOMEM;
		goto out;
	}

	if (!test_bit(NFS_INO_LAYOUTCOMMIT, &nfsi->flags))
		goto out_free;

	if (test_and_set_bit(NFS_INO_LAYOUTCOMMITTING, &nfsi->flags)) {
		if (!sync) {
			status = -EAGAIN;
			goto out_free;
		}
		status = wait_on_bit_lock(&nfsi->flags, NFS_INO_LAYOUTCOMMITTING,
					nfs_wait_bit_killable, TASK_KILLABLE);
		if (status)
			goto out_free;
	}

	INIT_LIST_HEAD(&data->lseg_list);
	spin_lock(&inode->i_lock);
	if (!test_and_clear_bit(NFS_INO_LAYOUTCOMMIT, &nfsi->flags)) {
		clear_bit(NFS_INO_LAYOUTCOMMITTING, &nfsi->flags);
		spin_unlock(&inode->i_lock);
		wake_up_bit(&nfsi->flags, NFS_INO_LAYOUTCOMMITTING);
		goto out_free;
	}

	pnfs_list_write_lseg(inode, &data->lseg_list);

	end_pos = nfsi->layout->plh_lwb;
	nfsi->layout->plh_lwb = 0;

	nfs4_stateid_copy(&data->args.stateid, &nfsi->layout->plh_stateid);
	spin_unlock(&inode->i_lock);

	data->args.inode = inode;
	data->cred = get_rpccred(nfsi->layout->plh_lc_cred);
	nfs_fattr_init(&data->fattr);
	data->args.bitmask = NFS_SERVER(inode)->cache_consistency_bitmask;
	data->res.fattr = &data->fattr;
	data->args.lastbytewritten = end_pos - 1;
	data->res.server = NFS_SERVER(inode);

	status = nfs4_proc_layoutcommit(data, sync);
out:
	if (status)
		mark_inode_dirty_sync(inode);
	dprintk("<-- %s status %d\n", __func__, status);
	return status;
out_free:
	kfree(data);
	goto out;
}

struct nfs4_threshold *pnfs_mdsthreshold_alloc(void)
{
	struct nfs4_threshold *thp;

	thp = kzalloc(sizeof(*thp), GFP_NOFS);
	if (!thp) {
		dprintk("%s mdsthreshold allocation failed\n", __func__);
		return NULL;
	}
	return thp;
}
