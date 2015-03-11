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
#include "nfs4trace.h"
#include "delegation.h"

#define NFSDBG_FACILITY		NFSDBG_PNFS
#define PNFS_LAYOUTGET_RETRY_TIMEOUT (120*HZ)

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

static int
pnfs_send_layoutreturn(struct pnfs_layout_hdr *lo, nfs4_stateid stateid,
		       enum pnfs_iomode iomode, bool sync);

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
	if (local != NULL && !try_module_get(local->owner)) {
		dprintk("%s: Could not grab reference on module\n", __func__);
		local = NULL;
	}
	spin_unlock(&pnfs_spinlock);
	return local;
}

void
unset_pnfs_layoutdriver(struct nfs_server *nfss)
{
	if (nfss->pnfs_curr_ld) {
		if (nfss->pnfs_curr_ld->clear_layoutdriver)
			nfss->pnfs_curr_ld->clear_layoutdriver(nfss);
		/* Decrement the MDS count. Purge the deviceid cache if zero */
		if (atomic_dec_and_test(&nfss->nfs_client->cl_mds_count))
			nfs4_deviceid_purge_client(nfss->nfs_client);
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
	server->pnfs_curr_ld = ld_type;
	if (ld_type->set_layoutdriver
	    && ld_type->set_layoutdriver(server, mntfh)) {
		printk(KERN_ERR "NFS: %s: Error initializing pNFS layout "
			"driver %u.\n", __func__, id);
		module_put(ld_type->owner);
		goto out_no_driver;
	}
	/* Bump the MDS count */
	atomic_inc(&server->nfs_client->cl_mds_count);

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
pnfs_get_layout_hdr(struct pnfs_layout_hdr *lo)
{
	atomic_inc(&lo->plh_refcount);
}

static struct pnfs_layout_hdr *
pnfs_alloc_layout_hdr(struct inode *ino, gfp_t gfp_flags)
{
	struct pnfs_layoutdriver_type *ld = NFS_SERVER(ino)->pnfs_curr_ld;
	return ld->alloc_layout_hdr(ino, gfp_flags);
}

static void
pnfs_free_layout_hdr(struct pnfs_layout_hdr *lo)
{
	struct nfs_server *server = NFS_SERVER(lo->plh_inode);
	struct pnfs_layoutdriver_type *ld = server->pnfs_curr_ld;

	if (!list_empty(&lo->plh_layouts)) {
		struct nfs_client *clp = server->nfs_client;

		spin_lock(&clp->cl_lock);
		list_del_init(&lo->plh_layouts);
		spin_unlock(&clp->cl_lock);
	}
	put_rpccred(lo->plh_lc_cred);
	return ld->free_layout_hdr(lo);
}

static void
pnfs_detach_layout_hdr(struct pnfs_layout_hdr *lo)
{
	struct nfs_inode *nfsi = NFS_I(lo->plh_inode);
	dprintk("%s: freeing layout cache %p\n", __func__, lo);
	nfsi->layout = NULL;
	/* Reset MDS Threshold I/O counters */
	nfsi->write_io = 0;
	nfsi->read_io = 0;
}

void
pnfs_put_layout_hdr(struct pnfs_layout_hdr *lo)
{
	struct inode *inode = lo->plh_inode;

	if (atomic_dec_and_lock(&lo->plh_refcount, &inode->i_lock)) {
		if (!list_empty(&lo->plh_segs))
			WARN_ONCE(1, "NFS: BUG unfreed layout segments.\n");
		pnfs_detach_layout_hdr(lo);
		spin_unlock(&inode->i_lock);
		pnfs_free_layout_hdr(lo);
	}
}

static int
pnfs_iomode_to_fail_bit(u32 iomode)
{
	return iomode == IOMODE_RW ?
		NFS_LAYOUT_RW_FAILED : NFS_LAYOUT_RO_FAILED;
}

static void
pnfs_layout_set_fail_bit(struct pnfs_layout_hdr *lo, int fail_bit)
{
	lo->plh_retry_timestamp = jiffies;
	if (!test_and_set_bit(fail_bit, &lo->plh_flags))
		atomic_inc(&lo->plh_refcount);
}

static void
pnfs_layout_clear_fail_bit(struct pnfs_layout_hdr *lo, int fail_bit)
{
	if (test_and_clear_bit(fail_bit, &lo->plh_flags))
		atomic_dec(&lo->plh_refcount);
}

static void
pnfs_layout_io_set_failed(struct pnfs_layout_hdr *lo, u32 iomode)
{
	struct inode *inode = lo->plh_inode;
	struct pnfs_layout_range range = {
		.iomode = iomode,
		.offset = 0,
		.length = NFS4_MAX_UINT64,
	};
	LIST_HEAD(head);

	spin_lock(&inode->i_lock);
	pnfs_layout_set_fail_bit(lo, pnfs_iomode_to_fail_bit(iomode));
	pnfs_mark_matching_lsegs_invalid(lo, &head, &range);
	spin_unlock(&inode->i_lock);
	pnfs_free_lseg_list(&head);
	dprintk("%s Setting layout IOMODE_%s fail bit\n", __func__,
			iomode == IOMODE_RW ?  "RW" : "READ");
}

static bool
pnfs_layout_io_test_failed(struct pnfs_layout_hdr *lo, u32 iomode)
{
	unsigned long start, end;
	int fail_bit = pnfs_iomode_to_fail_bit(iomode);

	if (test_bit(fail_bit, &lo->plh_flags) == 0)
		return false;
	end = jiffies;
	start = end - PNFS_LAYOUTGET_RETRY_TIMEOUT;
	if (!time_in_range(lo->plh_retry_timestamp, start, end)) {
		/* It is time to retry the failed layoutgets */
		pnfs_layout_clear_fail_bit(lo, fail_bit);
		return false;
	}
	return true;
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

static void pnfs_free_lseg(struct pnfs_layout_segment *lseg)
{
	struct inode *ino = lseg->pls_layout->plh_inode;

	NFS_SERVER(ino)->pnfs_curr_ld->free_lseg(lseg);
}

static void
pnfs_layout_remove_lseg(struct pnfs_layout_hdr *lo,
		struct pnfs_layout_segment *lseg)
{
	struct inode *inode = lo->plh_inode;

	WARN_ON(test_bit(NFS_LSEG_VALID, &lseg->pls_flags));
	list_del_init(&lseg->pls_list);
	/* Matched by pnfs_get_layout_hdr in pnfs_layout_insert_lseg */
	atomic_dec(&lo->plh_refcount);
	if (list_empty(&lo->plh_segs))
		clear_bit(NFS_LAYOUT_BULK_RECALL, &lo->plh_flags);
	rpc_wake_up(&NFS_SERVER(inode)->roc_rpcwaitq);
}

/* Return true if layoutreturn is needed */
static bool
pnfs_layout_need_return(struct pnfs_layout_hdr *lo,
			struct pnfs_layout_segment *lseg)
{
	struct pnfs_layout_segment *s;

	if (!test_bit(NFS_LSEG_LAYOUTRETURN, &lseg->pls_flags))
		return false;

	list_for_each_entry(s, &lo->plh_segs, pls_list)
		if (s != lseg && test_bit(NFS_LSEG_LAYOUTRETURN, &s->pls_flags))
			return false;

	return true;
}

static void pnfs_layoutreturn_before_put_lseg(struct pnfs_layout_segment *lseg,
		struct pnfs_layout_hdr *lo, struct inode *inode)
{
	lo = lseg->pls_layout;
	inode = lo->plh_inode;

	spin_lock(&inode->i_lock);
	if (pnfs_layout_need_return(lo, lseg)) {
		nfs4_stateid stateid;
		enum pnfs_iomode iomode;

		stateid = lo->plh_stateid;
		iomode = lo->plh_return_iomode;
		/* decreased in pnfs_send_layoutreturn() */
		lo->plh_block_lgets++;
		lo->plh_return_iomode = 0;
		spin_unlock(&inode->i_lock);
		pnfs_get_layout_hdr(lo);

		/* Send an async layoutreturn so we dont deadlock */
		pnfs_send_layoutreturn(lo, stateid, iomode, false);
	} else
		spin_unlock(&inode->i_lock);
}

void
pnfs_put_lseg(struct pnfs_layout_segment *lseg)
{
	struct pnfs_layout_hdr *lo;
	struct inode *inode;

	if (!lseg)
		return;

	dprintk("%s: lseg %p ref %d valid %d\n", __func__, lseg,
		atomic_read(&lseg->pls_refcount),
		test_bit(NFS_LSEG_VALID, &lseg->pls_flags));

	/* Handle the case where refcount != 1 */
	if (atomic_add_unless(&lseg->pls_refcount, -1, 1))
		return;

	lo = lseg->pls_layout;
	inode = lo->plh_inode;
	/* Do we need a layoutreturn? */
	if (test_bit(NFS_LSEG_LAYOUTRETURN, &lseg->pls_flags))
		pnfs_layoutreturn_before_put_lseg(lseg, lo, inode);

	if (atomic_dec_and_lock(&lseg->pls_refcount, &inode->i_lock)) {
		pnfs_get_layout_hdr(lo);
		pnfs_layout_remove_lseg(lo, lseg);
		spin_unlock(&inode->i_lock);
		pnfs_free_lseg(lseg);
		pnfs_put_layout_hdr(lo);
	}
}
EXPORT_SYMBOL_GPL(pnfs_put_lseg);

static void pnfs_free_lseg_async_work(struct work_struct *work)
{
	struct pnfs_layout_segment *lseg;
	struct pnfs_layout_hdr *lo;

	lseg = container_of(work, struct pnfs_layout_segment, pls_work);
	lo = lseg->pls_layout;

	pnfs_free_lseg(lseg);
	pnfs_put_layout_hdr(lo);
}

static void pnfs_free_lseg_async(struct pnfs_layout_segment *lseg)
{
	INIT_WORK(&lseg->pls_work, pnfs_free_lseg_async_work);
	schedule_work(&lseg->pls_work);
}

void
pnfs_put_lseg_locked(struct pnfs_layout_segment *lseg)
{
	if (!lseg)
		return;

	assert_spin_locked(&lseg->pls_layout->plh_inode->i_lock);

	dprintk("%s: lseg %p ref %d valid %d\n", __func__, lseg,
		atomic_read(&lseg->pls_refcount),
		test_bit(NFS_LSEG_VALID, &lseg->pls_flags));
	if (atomic_dec_and_test(&lseg->pls_refcount)) {
		struct pnfs_layout_hdr *lo = lseg->pls_layout;
		pnfs_get_layout_hdr(lo);
		pnfs_layout_remove_lseg(lo, lseg);
		pnfs_free_lseg_async(lseg);
	}
}
EXPORT_SYMBOL_GPL(pnfs_put_lseg_locked);

static u64
end_offset(u64 start, u64 len)
{
	u64 end;

	end = start + len;
	return end >= start ? end : NFS4_MAX_UINT64;
}

/*
 * is l2 fully contained in l1?
 *   start1                             end1
 *   [----------------------------------)
 *           start2           end2
 *           [----------------)
 */
static bool
pnfs_lseg_range_contained(const struct pnfs_layout_range *l1,
		 const struct pnfs_layout_range *l2)
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
static bool
pnfs_lseg_range_intersecting(const struct pnfs_layout_range *l1,
		    const struct pnfs_layout_range *l2)
{
	u64 start1 = l1->offset;
	u64 end1 = end_offset(start1, l1->length);
	u64 start2 = l2->offset;
	u64 end2 = end_offset(start2, l2->length);

	return (end1 == NFS4_MAX_UINT64 || end1 > start2) &&
	       (end2 == NFS4_MAX_UINT64 || end2 > start1);
}

static bool
should_free_lseg(const struct pnfs_layout_range *lseg_range,
		 const struct pnfs_layout_range *recall_range)
{
	return (recall_range->iomode == IOMODE_ANY ||
		lseg_range->iomode == recall_range->iomode) &&
	       pnfs_lseg_range_intersecting(lseg_range, recall_range);
}

static bool pnfs_lseg_dec_and_remove_zero(struct pnfs_layout_segment *lseg,
		struct list_head *tmp_list)
{
	if (!atomic_dec_and_test(&lseg->pls_refcount))
		return false;
	pnfs_layout_remove_lseg(lseg->pls_layout, lseg);
	list_add(&lseg->pls_list, tmp_list);
	return true;
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
		if (pnfs_lseg_dec_and_remove_zero(lseg, tmp_list))
			rv = 1;
	}
	return rv;
}

/* Returns count of number of matching invalid lsegs remaining in list
 * after call.
 */
int
pnfs_mark_matching_lsegs_invalid(struct pnfs_layout_hdr *lo,
			    struct list_head *tmp_list,
			    struct pnfs_layout_range *recall_range)
{
	struct pnfs_layout_segment *lseg, *next;
	int invalid = 0, removed = 0;

	dprintk("%s:Begin lo %p\n", __func__, lo);

	if (list_empty(&lo->plh_segs))
		return 0;
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

	if (list_empty(free_me))
		return;

	list_for_each_entry_safe(lseg, tmp, free_me, pls_list) {
		list_del(&lseg->pls_list);
		pnfs_free_lseg(lseg);
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
		pnfs_mark_matching_lsegs_invalid(lo, &tmp_list, NULL);
		pnfs_get_layout_hdr(lo);
		pnfs_layout_clear_fail_bit(lo, NFS_LAYOUT_RO_FAILED);
		pnfs_layout_clear_fail_bit(lo, NFS_LAYOUT_RW_FAILED);
		pnfs_clear_retry_layoutget(lo);
		spin_unlock(&nfsi->vfs_inode.i_lock);
		pnfs_free_lseg_list(&tmp_list);
		pnfs_put_layout_hdr(lo);
	} else
		spin_unlock(&nfsi->vfs_inode.i_lock);
}
EXPORT_SYMBOL_GPL(pnfs_destroy_layout);

static bool
pnfs_layout_add_bulk_destroy_list(struct inode *inode,
		struct list_head *layout_list)
{
	struct pnfs_layout_hdr *lo;
	bool ret = false;

	spin_lock(&inode->i_lock);
	lo = NFS_I(inode)->layout;
	if (lo != NULL && list_empty(&lo->plh_bulk_destroy)) {
		pnfs_get_layout_hdr(lo);
		list_add(&lo->plh_bulk_destroy, layout_list);
		ret = true;
	}
	spin_unlock(&inode->i_lock);
	return ret;
}

/* Caller must hold rcu_read_lock and clp->cl_lock */
static int
pnfs_layout_bulk_destroy_byserver_locked(struct nfs_client *clp,
		struct nfs_server *server,
		struct list_head *layout_list)
{
	struct pnfs_layout_hdr *lo, *next;
	struct inode *inode;

	list_for_each_entry_safe(lo, next, &server->layouts, plh_layouts) {
		inode = igrab(lo->plh_inode);
		if (inode == NULL)
			continue;
		list_del_init(&lo->plh_layouts);
		if (pnfs_layout_add_bulk_destroy_list(inode, layout_list))
			continue;
		rcu_read_unlock();
		spin_unlock(&clp->cl_lock);
		iput(inode);
		spin_lock(&clp->cl_lock);
		rcu_read_lock();
		return -EAGAIN;
	}
	return 0;
}

static int
pnfs_layout_free_bulk_destroy_list(struct list_head *layout_list,
		bool is_bulk_recall)
{
	struct pnfs_layout_hdr *lo;
	struct inode *inode;
	struct pnfs_layout_range range = {
		.iomode = IOMODE_ANY,
		.offset = 0,
		.length = NFS4_MAX_UINT64,
	};
	LIST_HEAD(lseg_list);
	int ret = 0;

	while (!list_empty(layout_list)) {
		lo = list_entry(layout_list->next, struct pnfs_layout_hdr,
				plh_bulk_destroy);
		dprintk("%s freeing layout for inode %lu\n", __func__,
			lo->plh_inode->i_ino);
		inode = lo->plh_inode;

		pnfs_layoutcommit_inode(inode, false);

		spin_lock(&inode->i_lock);
		list_del_init(&lo->plh_bulk_destroy);
		lo->plh_block_lgets++; /* permanently block new LAYOUTGETs */
		if (is_bulk_recall)
			set_bit(NFS_LAYOUT_BULK_RECALL, &lo->plh_flags);
		if (pnfs_mark_matching_lsegs_invalid(lo, &lseg_list, &range))
			ret = -EAGAIN;
		spin_unlock(&inode->i_lock);
		pnfs_free_lseg_list(&lseg_list);
		pnfs_put_layout_hdr(lo);
		iput(inode);
	}
	return ret;
}

int
pnfs_destroy_layouts_byfsid(struct nfs_client *clp,
		struct nfs_fsid *fsid,
		bool is_recall)
{
	struct nfs_server *server;
	LIST_HEAD(layout_list);

	spin_lock(&clp->cl_lock);
	rcu_read_lock();
restart:
	list_for_each_entry_rcu(server, &clp->cl_superblocks, client_link) {
		if (memcmp(&server->fsid, fsid, sizeof(*fsid)) != 0)
			continue;
		if (pnfs_layout_bulk_destroy_byserver_locked(clp,
				server,
				&layout_list) != 0)
			goto restart;
	}
	rcu_read_unlock();
	spin_unlock(&clp->cl_lock);

	if (list_empty(&layout_list))
		return 0;
	return pnfs_layout_free_bulk_destroy_list(&layout_list, is_recall);
}

int
pnfs_destroy_layouts_byclid(struct nfs_client *clp,
		bool is_recall)
{
	struct nfs_server *server;
	LIST_HEAD(layout_list);

	spin_lock(&clp->cl_lock);
	rcu_read_lock();
restart:
	list_for_each_entry_rcu(server, &clp->cl_superblocks, client_link) {
		if (pnfs_layout_bulk_destroy_byserver_locked(clp,
					server,
					&layout_list) != 0)
			goto restart;
	}
	rcu_read_unlock();
	spin_unlock(&clp->cl_lock);

	if (list_empty(&layout_list))
		return 0;
	return pnfs_layout_free_bulk_destroy_list(&layout_list, is_recall);
}

/*
 * Called by the state manger to remove all layouts established under an
 * expired lease.
 */
void
pnfs_destroy_all_layouts(struct nfs_client *clp)
{
	nfs4_deviceid_mark_client_invalid(clp);
	nfs4_deviceid_purge_client(clp);

	pnfs_destroy_layouts_byclid(clp, false);
}

/*
 * Compare 2 layout stateid sequence ids, to see which is newer,
 * taking into account wraparound issues.
 */
static bool pnfs_seqid_is_newer(u32 s1, u32 s2)
{
	return (s32)(s1 - s2) > 0;
}

/* update lo->plh_stateid with new if is more recent */
void
pnfs_set_layout_stateid(struct pnfs_layout_hdr *lo, const nfs4_stateid *new,
			bool update_barrier)
{
	u32 oldseq, newseq, new_barrier;
	int empty = list_empty(&lo->plh_segs);

	oldseq = be32_to_cpu(lo->plh_stateid.seqid);
	newseq = be32_to_cpu(new->seqid);
	if (empty || pnfs_seqid_is_newer(newseq, oldseq)) {
		nfs4_stateid_copy(&lo->plh_stateid, new);
		if (update_barrier) {
			new_barrier = be32_to_cpu(new->seqid);
		} else {
			/* Because of wraparound, we want to keep the barrier
			 * "close" to the current seqids.
			 */
			new_barrier = newseq - atomic_read(&lo->plh_outstanding);
		}
		if (empty || pnfs_seqid_is_newer(new_barrier, lo->plh_barrier))
			lo->plh_barrier = new_barrier;
	}
}

static bool
pnfs_layout_stateid_blocked(const struct pnfs_layout_hdr *lo,
		const nfs4_stateid *stateid)
{
	u32 seqid = be32_to_cpu(stateid->seqid);

	return !pnfs_seqid_is_newer(seqid, lo->plh_barrier);
}

static bool
pnfs_layout_returning(const struct pnfs_layout_hdr *lo,
		      struct pnfs_layout_range *range)
{
	return test_bit(NFS_LAYOUT_RETURN, &lo->plh_flags) &&
		(lo->plh_return_iomode == IOMODE_ANY ||
		 lo->plh_return_iomode == range->iomode);
}

/* lget is set to 1 if called from inside send_layoutget call chain */
static bool
pnfs_layoutgets_blocked(const struct pnfs_layout_hdr *lo,
			struct pnfs_layout_range *range, int lget)
{
	return lo->plh_block_lgets ||
		test_bit(NFS_LAYOUT_BULK_RECALL, &lo->plh_flags) ||
		(list_empty(&lo->plh_segs) &&
		 (atomic_read(&lo->plh_outstanding) > lget)) ||
		pnfs_layout_returning(lo, range);
}

int
pnfs_choose_layoutget_stateid(nfs4_stateid *dst, struct pnfs_layout_hdr *lo,
			      struct pnfs_layout_range *range,
			      struct nfs4_state *open_state)
{
	int status = 0;

	dprintk("--> %s\n", __func__);
	spin_lock(&lo->plh_inode->i_lock);
	if (pnfs_layoutgets_blocked(lo, range, 1)) {
		status = -EAGAIN;
	} else if (!nfs4_valid_open_stateid(open_state)) {
		status = -EBADF;
	} else if (list_empty(&lo->plh_segs) ||
		   test_bit(NFS_LAYOUT_INVALID_STID, &lo->plh_flags)) {
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
	struct pnfs_layout_segment *lseg;

	dprintk("--> %s\n", __func__);

	lgp = kzalloc(sizeof(*lgp), gfp_flags);
	if (lgp == NULL)
		return NULL;

	lgp->args.minlength = PAGE_CACHE_SIZE;
	if (lgp->args.minlength > range->length)
		lgp->args.minlength = range->length;
	lgp->args.maxcount = PNFS_LAYOUT_MAXSIZE;
	lgp->args.range = *range;
	lgp->args.type = server->pnfs_curr_ld->id;
	lgp->args.inode = ino;
	lgp->args.ctx = get_nfs_open_context(ctx);
	lgp->gfp_flags = gfp_flags;
	lgp->cred = lo->plh_lc_cred;

	/* Synchronously retrieve layout information from server and
	 * store in lseg.
	 */
	lseg = nfs4_proc_layoutget(lgp, gfp_flags);
	if (IS_ERR(lseg)) {
		switch (PTR_ERR(lseg)) {
		case -ENOMEM:
		case -ERESTARTSYS:
			break;
		default:
			/* remember that LAYOUTGET failed and suspend trying */
			pnfs_layout_io_set_failed(lo, range->iomode);
		}
		return NULL;
	} else
		pnfs_layout_clear_fail_bit(lo,
				pnfs_iomode_to_fail_bit(range->iomode));

	return lseg;
}

static void pnfs_clear_layoutcommit(struct inode *inode,
		struct list_head *head)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	struct pnfs_layout_segment *lseg, *tmp;

	if (!test_and_clear_bit(NFS_INO_LAYOUTCOMMIT, &nfsi->flags))
		return;
	list_for_each_entry_safe(lseg, tmp, &nfsi->layout->plh_segs, pls_list) {
		if (!test_and_clear_bit(NFS_LSEG_LAYOUTCOMMIT, &lseg->pls_flags))
			continue;
		pnfs_lseg_dec_and_remove_zero(lseg, head);
	}
}

void pnfs_clear_layoutreturn_waitbit(struct pnfs_layout_hdr *lo)
{
	clear_bit_unlock(NFS_LAYOUT_RETURN, &lo->plh_flags);
	smp_mb__after_atomic();
	wake_up_bit(&lo->plh_flags, NFS_LAYOUT_RETURN);
}

static int
pnfs_send_layoutreturn(struct pnfs_layout_hdr *lo, nfs4_stateid stateid,
		       enum pnfs_iomode iomode, bool sync)
{
	struct inode *ino = lo->plh_inode;
	struct nfs4_layoutreturn *lrp;
	int status = 0;

	lrp = kzalloc(sizeof(*lrp), GFP_NOFS);
	if (unlikely(lrp == NULL)) {
		status = -ENOMEM;
		spin_lock(&ino->i_lock);
		lo->plh_block_lgets--;
		pnfs_clear_layoutreturn_waitbit(lo);
		rpc_wake_up(&NFS_SERVER(ino)->roc_rpcwaitq);
		spin_unlock(&ino->i_lock);
		pnfs_put_layout_hdr(lo);
		goto out;
	}

	lrp->args.stateid = stateid;
	lrp->args.layout_type = NFS_SERVER(ino)->pnfs_curr_ld->id;
	lrp->args.inode = ino;
	lrp->args.range.iomode = iomode;
	lrp->args.range.offset = 0;
	lrp->args.range.length = NFS4_MAX_UINT64;
	lrp->args.layout = lo;
	lrp->clp = NFS_SERVER(ino)->nfs_client;
	lrp->cred = lo->plh_lc_cred;

	status = nfs4_proc_layoutreturn(lrp, sync);
out:
	dprintk("<-- %s status: %d\n", __func__, status);
	return status;
}

/*
 * Initiates a LAYOUTRETURN(FILE), and removes the pnfs_layout_hdr
 * when the layout segment list is empty.
 *
 * Note that a pnfs_layout_hdr can exist with an empty layout segment
 * list when LAYOUTGET has failed, or when LAYOUTGET succeeded, but the
 * deviceid is marked invalid.
 */
int
_pnfs_return_layout(struct inode *ino)
{
	struct pnfs_layout_hdr *lo = NULL;
	struct nfs_inode *nfsi = NFS_I(ino);
	LIST_HEAD(tmp_list);
	nfs4_stateid stateid;
	int status = 0, empty;

	dprintk("NFS: %s for inode %lu\n", __func__, ino->i_ino);

	spin_lock(&ino->i_lock);
	lo = nfsi->layout;
	if (!lo) {
		spin_unlock(&ino->i_lock);
		dprintk("NFS: %s no layout to return\n", __func__);
		goto out;
	}
	stateid = nfsi->layout->plh_stateid;
	/* Reference matched in nfs4_layoutreturn_release */
	pnfs_get_layout_hdr(lo);
	empty = list_empty(&lo->plh_segs);
	pnfs_clear_layoutcommit(ino, &tmp_list);
	pnfs_mark_matching_lsegs_invalid(lo, &tmp_list, NULL);

	if (NFS_SERVER(ino)->pnfs_curr_ld->return_range) {
		struct pnfs_layout_range range = {
			.iomode		= IOMODE_ANY,
			.offset		= 0,
			.length		= NFS4_MAX_UINT64,
		};
		NFS_SERVER(ino)->pnfs_curr_ld->return_range(lo, &range);
	}

	/* Don't send a LAYOUTRETURN if list was initially empty */
	if (empty) {
		spin_unlock(&ino->i_lock);
		pnfs_put_layout_hdr(lo);
		dprintk("NFS: %s no layout segments to return\n", __func__);
		goto out;
	}

	set_bit(NFS_LAYOUT_INVALID_STID, &lo->plh_flags);
	lo->plh_block_lgets++;
	spin_unlock(&ino->i_lock);
	pnfs_free_lseg_list(&tmp_list);

	status = pnfs_send_layoutreturn(lo, stateid, IOMODE_ANY, true);
out:
	dprintk("<-- %s status: %d\n", __func__, status);
	return status;
}
EXPORT_SYMBOL_GPL(_pnfs_return_layout);

int
pnfs_commit_and_return_layout(struct inode *inode)
{
	struct pnfs_layout_hdr *lo;
	int ret;

	spin_lock(&inode->i_lock);
	lo = NFS_I(inode)->layout;
	if (lo == NULL) {
		spin_unlock(&inode->i_lock);
		return 0;
	}
	pnfs_get_layout_hdr(lo);
	/* Block new layoutgets and read/write to ds */
	lo->plh_block_lgets++;
	spin_unlock(&inode->i_lock);
	filemap_fdatawait(inode->i_mapping);
	ret = pnfs_layoutcommit_inode(inode, true);
	if (ret == 0)
		ret = _pnfs_return_layout(inode);
	spin_lock(&inode->i_lock);
	lo->plh_block_lgets--;
	spin_unlock(&inode->i_lock);
	pnfs_put_layout_hdr(lo);
	return ret;
}

bool pnfs_roc(struct inode *ino)
{
	struct nfs_inode *nfsi = NFS_I(ino);
	struct nfs_open_context *ctx;
	struct nfs4_state *state;
	struct pnfs_layout_hdr *lo;
	struct pnfs_layout_segment *lseg, *tmp;
	nfs4_stateid stateid;
	LIST_HEAD(tmp_list);
	bool found = false, layoutreturn = false;

	spin_lock(&ino->i_lock);
	lo = nfsi->layout;
	if (!lo || !test_and_clear_bit(NFS_LAYOUT_ROC, &lo->plh_flags) ||
	    test_bit(NFS_LAYOUT_BULK_RECALL, &lo->plh_flags))
		goto out_noroc;

	/* Don't return layout if we hold a delegation */
	if (nfs4_check_delegation(ino, FMODE_READ))
		goto out_noroc;

	list_for_each_entry(ctx, &nfsi->open_files, list) {
		state = ctx->state;
		/* Don't return layout if there is open file state */
		if (state != NULL && state->state != 0)
			goto out_noroc;
	}

	pnfs_clear_retry_layoutget(lo);
	list_for_each_entry_safe(lseg, tmp, &lo->plh_segs, pls_list)
		if (test_bit(NFS_LSEG_ROC, &lseg->pls_flags)) {
			mark_lseg_invalid(lseg, &tmp_list);
			found = true;
		}
	if (!found)
		goto out_noroc;
	lo->plh_block_lgets++;
	pnfs_get_layout_hdr(lo); /* matched in pnfs_roc_release */
	spin_unlock(&ino->i_lock);
	pnfs_free_lseg_list(&tmp_list);
	return true;

out_noroc:
	if (lo) {
		stateid = lo->plh_stateid;
		layoutreturn =
			test_and_clear_bit(NFS_LAYOUT_RETURN_BEFORE_CLOSE,
					   &lo->plh_flags);
		if (layoutreturn) {
			lo->plh_block_lgets++;
			pnfs_get_layout_hdr(lo);
		}
	}
	spin_unlock(&ino->i_lock);
	if (layoutreturn)
		pnfs_send_layoutreturn(lo, stateid, IOMODE_ANY, true);
	return false;
}

void pnfs_roc_release(struct inode *ino)
{
	struct pnfs_layout_hdr *lo;

	spin_lock(&ino->i_lock);
	lo = NFS_I(ino)->layout;
	lo->plh_block_lgets--;
	if (atomic_dec_and_test(&lo->plh_refcount)) {
		pnfs_detach_layout_hdr(lo);
		spin_unlock(&ino->i_lock);
		pnfs_free_layout_hdr(lo);
	} else
		spin_unlock(&ino->i_lock);
}

void pnfs_roc_set_barrier(struct inode *ino, u32 barrier)
{
	struct pnfs_layout_hdr *lo;

	spin_lock(&ino->i_lock);
	lo = NFS_I(ino)->layout;
	if (pnfs_seqid_is_newer(barrier, lo->plh_barrier))
		lo->plh_barrier = barrier;
	spin_unlock(&ino->i_lock);
}

bool pnfs_roc_drain(struct inode *ino, u32 *barrier, struct rpc_task *task)
{
	struct nfs_inode *nfsi = NFS_I(ino);
	struct pnfs_layout_hdr *lo;
	struct pnfs_layout_segment *lseg;
	nfs4_stateid stateid;
	u32 current_seqid;
	bool found = false, layoutreturn = false;

	spin_lock(&ino->i_lock);
	list_for_each_entry(lseg, &nfsi->layout->plh_segs, pls_list)
		if (test_bit(NFS_LSEG_ROC, &lseg->pls_flags)) {
			rpc_sleep_on(&NFS_SERVER(ino)->roc_rpcwaitq, task, NULL);
			found = true;
			goto out;
		}
	lo = nfsi->layout;
	current_seqid = be32_to_cpu(lo->plh_stateid.seqid);

	/* Since close does not return a layout stateid for use as
	 * a barrier, we choose the worst-case barrier.
	 */
	*barrier = current_seqid + atomic_read(&lo->plh_outstanding);
out:
	if (!found) {
		stateid = lo->plh_stateid;
		layoutreturn =
			test_and_clear_bit(NFS_LAYOUT_RETURN_BEFORE_CLOSE,
					   &lo->plh_flags);
		if (layoutreturn) {
			lo->plh_block_lgets++;
			pnfs_get_layout_hdr(lo);
		}
	}
	spin_unlock(&ino->i_lock);
	if (layoutreturn) {
		rpc_sleep_on(&NFS_SERVER(ino)->roc_rpcwaitq, task, NULL);
		pnfs_send_layoutreturn(lo, stateid, IOMODE_ANY, false);
	}
	return found;
}

/*
 * Compare two layout segments for sorting into layout cache.
 * We want to preferentially return RW over RO layouts, so ensure those
 * are seen first.
 */
static s64
pnfs_lseg_range_cmp(const struct pnfs_layout_range *l1,
	   const struct pnfs_layout_range *l2)
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
pnfs_layout_insert_lseg(struct pnfs_layout_hdr *lo,
		   struct pnfs_layout_segment *lseg)
{
	struct pnfs_layout_segment *lp;

	dprintk("%s:Begin\n", __func__);

	list_for_each_entry(lp, &lo->plh_segs, pls_list) {
		if (pnfs_lseg_range_cmp(&lseg->pls_range, &lp->pls_range) > 0)
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
	pnfs_get_layout_hdr(lo);

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
	INIT_LIST_HEAD(&lo->plh_bulk_destroy);
	lo->plh_inode = ino;
	lo->plh_lc_cred = get_rpccred(ctx->cred);
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

	if (nfsi->layout != NULL)
		goto out_existing;
	spin_unlock(&ino->i_lock);
	new = alloc_init_layout_hdr(ino, ctx, gfp_flags);
	spin_lock(&ino->i_lock);

	if (likely(nfsi->layout == NULL)) {	/* Won the race? */
		nfsi->layout = new;
		return new;
	} else if (new != NULL)
		pnfs_free_layout_hdr(new);
out_existing:
	pnfs_get_layout_hdr(nfsi->layout);
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
static bool
pnfs_lseg_range_match(const struct pnfs_layout_range *ls_range,
		 const struct pnfs_layout_range *range)
{
	struct pnfs_layout_range range1;

	if ((range->iomode == IOMODE_RW &&
	     ls_range->iomode != IOMODE_RW) ||
	    !pnfs_lseg_range_intersecting(ls_range, range))
		return 0;

	/* range1 covers only the first byte in the range */
	range1 = *range;
	range1.length = 1;
	return pnfs_lseg_range_contained(ls_range, &range1);
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

	list_for_each_entry(lseg, &lo->plh_segs, pls_list) {
		if (test_bit(NFS_LSEG_VALID, &lseg->pls_flags) &&
		    !test_bit(NFS_LSEG_LAYOUTRETURN, &lseg->pls_flags) &&
		    pnfs_lseg_range_match(&lseg->pls_range, range)) {
			ret = pnfs_get_lseg(lseg);
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
 * Use mdsthreshold hints set at each OPEN to determine if I/O should go
 * to the MDS or over pNFS
 *
 * The nfs_inode read_io and write_io fields are cumulative counters reset
 * when there are no layout segments. Note that in pnfs_update_layout iomode
 * is set to IOMODE_READ for a READ request, and set to IOMODE_RW for a
 * WRITE request.
 *
 * A return of true means use MDS I/O.
 *
 * From rfc 5661:
 * If a file's size is smaller than the file size threshold, data accesses
 * SHOULD be sent to the metadata server.  If an I/O request has a length that
 * is below the I/O size threshold, the I/O SHOULD be sent to the metadata
 * server.  If both file size and I/O size are provided, the client SHOULD
 * reach or exceed  both thresholds before sending its read or write
 * requests to the data server.
 */
static bool pnfs_within_mdsthreshold(struct nfs_open_context *ctx,
				     struct inode *ino, int iomode)
{
	struct nfs4_threshold *t = ctx->mdsthreshold;
	struct nfs_inode *nfsi = NFS_I(ino);
	loff_t fsize = i_size_read(ino);
	bool size = false, size_set = false, io = false, io_set = false, ret = false;

	if (t == NULL)
		return ret;

	dprintk("%s bm=0x%x rd_sz=%llu wr_sz=%llu rd_io=%llu wr_io=%llu\n",
		__func__, t->bm, t->rd_sz, t->wr_sz, t->rd_io_sz, t->wr_io_sz);

	switch (iomode) {
	case IOMODE_READ:
		if (t->bm & THRESHOLD_RD) {
			dprintk("%s fsize %llu\n", __func__, fsize);
			size_set = true;
			if (fsize < t->rd_sz)
				size = true;
		}
		if (t->bm & THRESHOLD_RD_IO) {
			dprintk("%s nfsi->read_io %llu\n", __func__,
				nfsi->read_io);
			io_set = true;
			if (nfsi->read_io < t->rd_io_sz)
				io = true;
		}
		break;
	case IOMODE_RW:
		if (t->bm & THRESHOLD_WR) {
			dprintk("%s fsize %llu\n", __func__, fsize);
			size_set = true;
			if (fsize < t->wr_sz)
				size = true;
		}
		if (t->bm & THRESHOLD_WR_IO) {
			dprintk("%s nfsi->write_io %llu\n", __func__,
				nfsi->write_io);
			io_set = true;
			if (nfsi->write_io < t->wr_io_sz)
				io = true;
		}
		break;
	}
	if (size_set && io_set) {
		if (size && io)
			ret = true;
	} else if (size || io)
		ret = true;

	dprintk("<-- %s size %d io %d ret %d\n", __func__, size, io, ret);
	return ret;
}

/* stop waiting if someone clears NFS_LAYOUT_RETRY_LAYOUTGET bit. */
static int pnfs_layoutget_retry_bit_wait(struct wait_bit_key *key)
{
	if (!test_bit(NFS_LAYOUT_RETRY_LAYOUTGET, key->flags))
		return 1;
	return nfs_wait_bit_killable(key);
}

static bool pnfs_prepare_to_retry_layoutget(struct pnfs_layout_hdr *lo)
{
	/*
	 * send layoutcommit as it can hold up layoutreturn due to lseg
	 * reference
	 */
	pnfs_layoutcommit_inode(lo->plh_inode, false);
	return !wait_on_bit_action(&lo->plh_flags, NFS_LAYOUT_RETURN,
				   pnfs_layoutget_retry_bit_wait,
				   TASK_UNINTERRUPTIBLE);
}

static void pnfs_clear_first_layoutget(struct pnfs_layout_hdr *lo)
{
	unsigned long *bitlock = &lo->plh_flags;

	clear_bit_unlock(NFS_LAYOUT_FIRST_LAYOUTGET, bitlock);
	smp_mb__after_atomic();
	wake_up_bit(bitlock, NFS_LAYOUT_FIRST_LAYOUTGET);
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
	struct nfs_server *server = NFS_SERVER(ino);
	struct nfs_client *clp = server->nfs_client;
	struct pnfs_layout_hdr *lo;
	struct pnfs_layout_segment *lseg = NULL;
	bool first;

	if (!pnfs_enabled_sb(NFS_SERVER(ino)))
		goto out;

	if (pnfs_within_mdsthreshold(ctx, ino, iomode))
		goto out;

lookup_again:
	first = false;
	spin_lock(&ino->i_lock);
	lo = pnfs_find_alloc_layout(ino, ctx, gfp_flags);
	if (lo == NULL) {
		spin_unlock(&ino->i_lock);
		goto out;
	}

	/* Do we even need to bother with this? */
	if (test_bit(NFS_LAYOUT_BULK_RECALL, &lo->plh_flags)) {
		dprintk("%s matches recall, use MDS\n", __func__);
		goto out_unlock;
	}

	/* if LAYOUTGET already failed once we don't try again */
	if (pnfs_layout_io_test_failed(lo, iomode) &&
	    !pnfs_should_retry_layoutget(lo))
		goto out_unlock;

	first = list_empty(&lo->plh_segs);
	if (first) {
		/* The first layoutget for the file. Need to serialize per
		 * RFC 5661 Errata 3208.
		 */
		if (test_and_set_bit(NFS_LAYOUT_FIRST_LAYOUTGET,
				     &lo->plh_flags)) {
			spin_unlock(&ino->i_lock);
			wait_on_bit(&lo->plh_flags, NFS_LAYOUT_FIRST_LAYOUTGET,
				    TASK_UNINTERRUPTIBLE);
			pnfs_put_layout_hdr(lo);
			goto lookup_again;
		}
	} else {
		/* Check to see if the layout for the given range
		 * already exists
		 */
		lseg = pnfs_find_lseg(lo, &arg);
		if (lseg)
			goto out_unlock;
	}

	/*
	 * Because we free lsegs before sending LAYOUTRETURN, we need to wait
	 * for LAYOUTRETURN even if first is true.
	 */
	if (!lseg && pnfs_should_retry_layoutget(lo) &&
	    test_bit(NFS_LAYOUT_RETURN, &lo->plh_flags)) {
		spin_unlock(&ino->i_lock);
		dprintk("%s wait for layoutreturn\n", __func__);
		if (pnfs_prepare_to_retry_layoutget(lo)) {
			if (first)
				pnfs_clear_first_layoutget(lo);
			pnfs_put_layout_hdr(lo);
			dprintk("%s retrying\n", __func__);
			goto lookup_again;
		}
		goto out_put_layout_hdr;
	}

	if (pnfs_layoutgets_blocked(lo, &arg, 0))
		goto out_unlock;
	atomic_inc(&lo->plh_outstanding);
	spin_unlock(&ino->i_lock);

	if (list_empty(&lo->plh_layouts)) {
		/* The lo must be on the clp list if there is any
		 * chance of a CB_LAYOUTRECALL(FILE) coming in.
		 */
		spin_lock(&clp->cl_lock);
		if (list_empty(&lo->plh_layouts))
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
	pnfs_clear_retry_layoutget(lo);
	atomic_dec(&lo->plh_outstanding);
out_put_layout_hdr:
	if (first)
		pnfs_clear_first_layoutget(lo);
	pnfs_put_layout_hdr(lo);
out:
	dprintk("%s: inode %s/%llu pNFS layout segment %s for "
			"(%s, offset: %llu, length: %llu)\n",
			__func__, ino->i_sb->s_id,
			(unsigned long long)NFS_FILEID(ino),
			lseg == NULL ? "not found" : "found",
			iomode==IOMODE_RW ?  "read/write" : "read-only",
			(unsigned long long)pos,
			(unsigned long long)count);
	return lseg;
out_unlock:
	spin_unlock(&ino->i_lock);
	goto out_put_layout_hdr;
}
EXPORT_SYMBOL_GPL(pnfs_update_layout);

struct pnfs_layout_segment *
pnfs_layout_process(struct nfs4_layoutget *lgp)
{
	struct pnfs_layout_hdr *lo = NFS_I(lgp->args.inode)->layout;
	struct nfs4_layoutget_res *res = &lgp->res;
	struct pnfs_layout_segment *lseg;
	struct inode *ino = lo->plh_inode;
	LIST_HEAD(free_me);
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

	init_lseg(lo, lseg);
	lseg->pls_range = res->range;

	spin_lock(&ino->i_lock);
	if (test_bit(NFS_LAYOUT_BULK_RECALL, &lo->plh_flags)) {
		dprintk("%s forget reply due to recall\n", __func__);
		goto out_forget_reply;
	}

	if (pnfs_layoutgets_blocked(lo, &lgp->args.range, 1)) {
		dprintk("%s forget reply due to state\n", __func__);
		goto out_forget_reply;
	}

	if (nfs4_stateid_match_other(&lo->plh_stateid, &res->stateid)) {
		/* existing state ID, make sure the sequence number matches. */
		if (pnfs_layout_stateid_blocked(lo, &res->stateid)) {
			dprintk("%s forget reply due to sequence\n", __func__);
			goto out_forget_reply;
		}
		pnfs_set_layout_stateid(lo, &res->stateid, false);
	} else {
		/*
		 * We got an entirely new state ID.  Mark all segments for the
		 * inode invalid, and don't bother validating the stateid
		 * sequence number.
		 */
		pnfs_mark_matching_lsegs_invalid(lo, &free_me, NULL);

		nfs4_stateid_copy(&lo->plh_stateid, &res->stateid);
		lo->plh_barrier = be32_to_cpu(res->stateid.seqid);
	}

	clear_bit(NFS_LAYOUT_INVALID_STID, &lo->plh_flags);

	pnfs_get_lseg(lseg);
	pnfs_layout_insert_lseg(lo, lseg);

	if (res->return_on_close) {
		set_bit(NFS_LSEG_ROC, &lseg->pls_flags);
		set_bit(NFS_LAYOUT_ROC, &lo->plh_flags);
	}

	spin_unlock(&ino->i_lock);
	pnfs_free_lseg_list(&free_me);
	return lseg;
out:
	return ERR_PTR(status);

out_forget_reply:
	spin_unlock(&ino->i_lock);
	lseg->pls_layout = lo;
	NFS_SERVER(ino)->pnfs_curr_ld->free_lseg(lseg);
	goto out;
}

static void
pnfs_mark_matching_lsegs_return(struct pnfs_layout_hdr *lo,
				struct list_head *tmp_list,
				struct pnfs_layout_range *return_range)
{
	struct pnfs_layout_segment *lseg, *next;

	dprintk("%s:Begin lo %p\n", __func__, lo);

	if (list_empty(&lo->plh_segs))
		return;

	list_for_each_entry_safe(lseg, next, &lo->plh_segs, pls_list)
		if (should_free_lseg(&lseg->pls_range, return_range)) {
			dprintk("%s: marking lseg %p iomode %d "
				"offset %llu length %llu\n", __func__,
				lseg, lseg->pls_range.iomode,
				lseg->pls_range.offset,
				lseg->pls_range.length);
			set_bit(NFS_LSEG_LAYOUTRETURN, &lseg->pls_flags);
			mark_lseg_invalid(lseg, tmp_list);
		}
}

void pnfs_error_mark_layout_for_return(struct inode *inode,
				       struct pnfs_layout_segment *lseg)
{
	struct pnfs_layout_hdr *lo = NFS_I(inode)->layout;
	int iomode = pnfs_iomode_to_fail_bit(lseg->pls_range.iomode);
	struct pnfs_layout_range range = {
		.iomode = lseg->pls_range.iomode,
		.offset = 0,
		.length = NFS4_MAX_UINT64,
	};
	LIST_HEAD(free_me);

	spin_lock(&inode->i_lock);
	/* set failure bit so that pnfs path will be retried later */
	pnfs_layout_set_fail_bit(lo, iomode);
	set_bit(NFS_LAYOUT_RETURN, &lo->plh_flags);
	if (lo->plh_return_iomode == 0)
		lo->plh_return_iomode = range.iomode;
	else if (lo->plh_return_iomode != range.iomode)
		lo->plh_return_iomode = IOMODE_ANY;
	/*
	 * mark all matching lsegs so that we are sure to have no live
	 * segments at hand when sending layoutreturn. See pnfs_put_lseg()
	 * for how it works.
	 */
	pnfs_mark_matching_lsegs_return(lo, &free_me, &range);
	spin_unlock(&inode->i_lock);
	pnfs_free_lseg_list(&free_me);
}
EXPORT_SYMBOL_GPL(pnfs_error_mark_layout_for_return);

void
pnfs_generic_pg_init_read(struct nfs_pageio_descriptor *pgio, struct nfs_page *req)
{
	u64 rd_size = req->wb_bytes;

	if (pgio->pg_lseg == NULL) {
		if (pgio->pg_dreq == NULL)
			rd_size = i_size_read(pgio->pg_inode) - req_offset(req);
		else
			rd_size = nfs_dreq_bytes_left(pgio->pg_dreq);

		pgio->pg_lseg = pnfs_update_layout(pgio->pg_inode,
						   req->wb_context,
						   req_offset(req),
						   rd_size,
						   IOMODE_READ,
						   GFP_KERNEL);
	}
	/* If no lseg, fall back to read through mds */
	if (pgio->pg_lseg == NULL)
		nfs_pageio_reset_read_mds(pgio);

}
EXPORT_SYMBOL_GPL(pnfs_generic_pg_init_read);

void
pnfs_generic_pg_init_write(struct nfs_pageio_descriptor *pgio,
			   struct nfs_page *req, u64 wb_size)
{
	if (pgio->pg_lseg == NULL)
		pgio->pg_lseg = pnfs_update_layout(pgio->pg_inode,
						   req->wb_context,
						   req_offset(req),
						   wb_size,
						   IOMODE_RW,
						   GFP_NOFS);
	/* If no lseg, fall back to write through mds */
	if (pgio->pg_lseg == NULL)
		nfs_pageio_reset_write_mds(pgio);
}
EXPORT_SYMBOL_GPL(pnfs_generic_pg_init_write);

void
pnfs_generic_pg_cleanup(struct nfs_pageio_descriptor *desc)
{
	if (desc->pg_lseg) {
		pnfs_put_lseg(desc->pg_lseg);
		desc->pg_lseg = NULL;
	}
}
EXPORT_SYMBOL_GPL(pnfs_generic_pg_cleanup);

/*
 * Return 0 if @req cannot be coalesced into @pgio, otherwise return the number
 * of bytes (maximum @req->wb_bytes) that can be coalesced.
 */
size_t
pnfs_generic_pg_test(struct nfs_pageio_descriptor *pgio,
		     struct nfs_page *prev, struct nfs_page *req)
{
	unsigned int size;
	u64 seg_end, req_start, seg_left;

	size = nfs_generic_pg_test(pgio, prev, req);
	if (!size)
		return 0;

	/*
	 * 'size' contains the number of bytes left in the current page (up
	 * to the original size asked for in @req->wb_bytes).
	 *
	 * Calculate how many bytes are left in the layout segment
	 * and if there are less bytes than 'size', return that instead.
	 *
	 * Please also note that 'end_offset' is actually the offset of the
	 * first byte that lies outside the pnfs_layout_range. FIXME?
	 *
	 */
	if (pgio->pg_lseg) {
		seg_end = end_offset(pgio->pg_lseg->pls_range.offset,
				     pgio->pg_lseg->pls_range.length);
		req_start = req_offset(req);
		WARN_ON_ONCE(req_start >= seg_end);
		/* start of request is past the last byte of this segment */
		if (req_start >= seg_end) {
			/* reference the new lseg */
			if (pgio->pg_ops->pg_cleanup)
				pgio->pg_ops->pg_cleanup(pgio);
			if (pgio->pg_ops->pg_init)
				pgio->pg_ops->pg_init(pgio, req);
			return 0;
		}

		/* adjust 'size' iff there are fewer bytes left in the
		 * segment than what nfs_generic_pg_test returned */
		seg_left = seg_end - req_start;
		if (seg_left < size)
			size = (unsigned int)seg_left;
	}

	return size;
}
EXPORT_SYMBOL_GPL(pnfs_generic_pg_test);

int pnfs_write_done_resend_to_mds(struct nfs_pgio_header *hdr)
{
	struct nfs_pageio_descriptor pgio;

	/* Resend all requests through the MDS */
	nfs_pageio_init_write(&pgio, hdr->inode, FLUSH_STABLE, true,
			      hdr->completion_ops);
	return nfs_pageio_resend(&pgio, hdr);
}
EXPORT_SYMBOL_GPL(pnfs_write_done_resend_to_mds);

static void pnfs_ld_handle_write_error(struct nfs_pgio_header *hdr)
{

	dprintk("pnfs write error = %d\n", hdr->pnfs_error);
	if (NFS_SERVER(hdr->inode)->pnfs_curr_ld->flags &
	    PNFS_LAYOUTRET_ON_ERROR) {
		pnfs_return_layout(hdr->inode);
	}
	if (!test_and_set_bit(NFS_IOHDR_REDO, &hdr->flags))
		hdr->task.tk_status = pnfs_write_done_resend_to_mds(hdr);
}

/*
 * Called by non rpc-based layout drivers
 */
void pnfs_ld_write_done(struct nfs_pgio_header *hdr)
{
	trace_nfs4_pnfs_write(hdr, hdr->pnfs_error);
	if (!hdr->pnfs_error) {
		pnfs_set_layoutcommit(hdr);
		hdr->mds_ops->rpc_call_done(&hdr->task, hdr);
	} else
		pnfs_ld_handle_write_error(hdr);
	hdr->mds_ops->rpc_release(hdr);
}
EXPORT_SYMBOL_GPL(pnfs_ld_write_done);

static void
pnfs_write_through_mds(struct nfs_pageio_descriptor *desc,
		struct nfs_pgio_header *hdr)
{
	struct nfs_pgio_mirror *mirror = nfs_pgio_current_mirror(desc);

	if (!test_and_set_bit(NFS_IOHDR_REDO, &hdr->flags)) {
		list_splice_tail_init(&hdr->pages, &mirror->pg_list);
		nfs_pageio_reset_write_mds(desc);
		mirror->pg_recoalesce = 1;
	}
	nfs_pgio_data_destroy(hdr);
}

static enum pnfs_try_status
pnfs_try_to_write_data(struct nfs_pgio_header *hdr,
			const struct rpc_call_ops *call_ops,
			struct pnfs_layout_segment *lseg,
			int how)
{
	struct inode *inode = hdr->inode;
	enum pnfs_try_status trypnfs;
	struct nfs_server *nfss = NFS_SERVER(inode);

	hdr->mds_ops = call_ops;

	dprintk("%s: Writing ino:%lu %u@%llu (how %d)\n", __func__,
		inode->i_ino, hdr->args.count, hdr->args.offset, how);
	trypnfs = nfss->pnfs_curr_ld->write_pagelist(hdr, how);
	if (trypnfs != PNFS_NOT_ATTEMPTED)
		nfs_inc_stats(inode, NFSIOS_PNFS_WRITE);
	dprintk("%s End (trypnfs:%d)\n", __func__, trypnfs);
	return trypnfs;
}

static void
pnfs_do_write(struct nfs_pageio_descriptor *desc,
	      struct nfs_pgio_header *hdr, int how)
{
	const struct rpc_call_ops *call_ops = desc->pg_rpc_callops;
	struct pnfs_layout_segment *lseg = desc->pg_lseg;
	enum pnfs_try_status trypnfs;

	trypnfs = pnfs_try_to_write_data(hdr, call_ops, lseg, how);
	if (trypnfs == PNFS_NOT_ATTEMPTED)
		pnfs_write_through_mds(desc, hdr);
}

static void pnfs_writehdr_free(struct nfs_pgio_header *hdr)
{
	pnfs_put_lseg(hdr->lseg);
	nfs_pgio_header_free(hdr);
}

int
pnfs_generic_pg_writepages(struct nfs_pageio_descriptor *desc)
{
	struct nfs_pgio_mirror *mirror = nfs_pgio_current_mirror(desc);

	struct nfs_pgio_header *hdr;
	int ret;

	hdr = nfs_pgio_header_alloc(desc->pg_rw_ops);
	if (!hdr) {
		desc->pg_completion_ops->error_cleanup(&mirror->pg_list);
		return -ENOMEM;
	}
	nfs_pgheader_init(desc, hdr, pnfs_writehdr_free);

	hdr->lseg = pnfs_get_lseg(desc->pg_lseg);
	ret = nfs_generic_pgio(desc, hdr);
	if (!ret)
		pnfs_do_write(desc, hdr, desc->pg_ioflags);

	return ret;
}
EXPORT_SYMBOL_GPL(pnfs_generic_pg_writepages);

int pnfs_read_done_resend_to_mds(struct nfs_pgio_header *hdr)
{
	struct nfs_pageio_descriptor pgio;

	/* Resend all requests through the MDS */
	nfs_pageio_init_read(&pgio, hdr->inode, true, hdr->completion_ops);
	return nfs_pageio_resend(&pgio, hdr);
}
EXPORT_SYMBOL_GPL(pnfs_read_done_resend_to_mds);

static void pnfs_ld_handle_read_error(struct nfs_pgio_header *hdr)
{
	dprintk("pnfs read error = %d\n", hdr->pnfs_error);
	if (NFS_SERVER(hdr->inode)->pnfs_curr_ld->flags &
	    PNFS_LAYOUTRET_ON_ERROR) {
		pnfs_return_layout(hdr->inode);
	}
	if (!test_and_set_bit(NFS_IOHDR_REDO, &hdr->flags))
		hdr->task.tk_status = pnfs_read_done_resend_to_mds(hdr);
}

/*
 * Called by non rpc-based layout drivers
 */
void pnfs_ld_read_done(struct nfs_pgio_header *hdr)
{
	trace_nfs4_pnfs_read(hdr, hdr->pnfs_error);
	if (likely(!hdr->pnfs_error)) {
		__nfs4_read_done_cb(hdr);
		hdr->mds_ops->rpc_call_done(&hdr->task, hdr);
	} else
		pnfs_ld_handle_read_error(hdr);
	hdr->mds_ops->rpc_release(hdr);
}
EXPORT_SYMBOL_GPL(pnfs_ld_read_done);

static void
pnfs_read_through_mds(struct nfs_pageio_descriptor *desc,
		struct nfs_pgio_header *hdr)
{
	struct nfs_pgio_mirror *mirror = nfs_pgio_current_mirror(desc);

	if (!test_and_set_bit(NFS_IOHDR_REDO, &hdr->flags)) {
		list_splice_tail_init(&hdr->pages, &mirror->pg_list);
		nfs_pageio_reset_read_mds(desc);
		mirror->pg_recoalesce = 1;
	}
	nfs_pgio_data_destroy(hdr);
}

/*
 * Call the appropriate parallel I/O subsystem read function.
 */
static enum pnfs_try_status
pnfs_try_to_read_data(struct nfs_pgio_header *hdr,
		       const struct rpc_call_ops *call_ops,
		       struct pnfs_layout_segment *lseg)
{
	struct inode *inode = hdr->inode;
	struct nfs_server *nfss = NFS_SERVER(inode);
	enum pnfs_try_status trypnfs;

	hdr->mds_ops = call_ops;

	dprintk("%s: Reading ino:%lu %u@%llu\n",
		__func__, inode->i_ino, hdr->args.count, hdr->args.offset);

	trypnfs = nfss->pnfs_curr_ld->read_pagelist(hdr);
	if (trypnfs != PNFS_NOT_ATTEMPTED)
		nfs_inc_stats(inode, NFSIOS_PNFS_READ);
	dprintk("%s End (trypnfs:%d)\n", __func__, trypnfs);
	return trypnfs;
}

/* Resend all requests through pnfs. */
int pnfs_read_resend_pnfs(struct nfs_pgio_header *hdr)
{
	struct nfs_pageio_descriptor pgio;

	nfs_pageio_init_read(&pgio, hdr->inode, false, hdr->completion_ops);
	return nfs_pageio_resend(&pgio, hdr);
}
EXPORT_SYMBOL_GPL(pnfs_read_resend_pnfs);

static void
pnfs_do_read(struct nfs_pageio_descriptor *desc, struct nfs_pgio_header *hdr)
{
	const struct rpc_call_ops *call_ops = desc->pg_rpc_callops;
	struct pnfs_layout_segment *lseg = desc->pg_lseg;
	enum pnfs_try_status trypnfs;
	int err = 0;

	trypnfs = pnfs_try_to_read_data(hdr, call_ops, lseg);
	if (trypnfs == PNFS_TRY_AGAIN)
		err = pnfs_read_resend_pnfs(hdr);
	if (trypnfs == PNFS_NOT_ATTEMPTED || err)
		pnfs_read_through_mds(desc, hdr);
}

static void pnfs_readhdr_free(struct nfs_pgio_header *hdr)
{
	pnfs_put_lseg(hdr->lseg);
	nfs_pgio_header_free(hdr);
}

int
pnfs_generic_pg_readpages(struct nfs_pageio_descriptor *desc)
{
	struct nfs_pgio_mirror *mirror = nfs_pgio_current_mirror(desc);

	struct nfs_pgio_header *hdr;
	int ret;

	hdr = nfs_pgio_header_alloc(desc->pg_rw_ops);
	if (!hdr) {
		desc->pg_completion_ops->error_cleanup(&mirror->pg_list);
		return -ENOMEM;
	}
	nfs_pgheader_init(desc, hdr, pnfs_readhdr_free);
	hdr->lseg = pnfs_get_lseg(desc->pg_lseg);
	ret = nfs_generic_pgio(desc, hdr);
	if (!ret)
		pnfs_do_read(desc, hdr);
	return ret;
}
EXPORT_SYMBOL_GPL(pnfs_generic_pg_readpages);

static void pnfs_clear_layoutcommitting(struct inode *inode)
{
	unsigned long *bitlock = &NFS_I(inode)->flags;

	clear_bit_unlock(NFS_INO_LAYOUTCOMMITTING, bitlock);
	smp_mb__after_atomic();
	wake_up_bit(bitlock, NFS_INO_LAYOUTCOMMITTING);
}

/*
 * There can be multiple RW segments.
 */
static void pnfs_list_write_lseg(struct inode *inode, struct list_head *listp)
{
	struct pnfs_layout_segment *lseg;

	list_for_each_entry(lseg, &NFS_I(inode)->layout->plh_segs, pls_list) {
		if (lseg->pls_range.iomode == IOMODE_RW &&
		    test_and_clear_bit(NFS_LSEG_LAYOUTCOMMIT, &lseg->pls_flags))
			list_add(&lseg->pls_lc_list, listp);
	}
}

static void pnfs_list_write_lseg_done(struct inode *inode, struct list_head *listp)
{
	struct pnfs_layout_segment *lseg, *tmp;

	/* Matched by references in pnfs_set_layoutcommit */
	list_for_each_entry_safe(lseg, tmp, listp, pls_lc_list) {
		list_del_init(&lseg->pls_lc_list);
		pnfs_put_lseg(lseg);
	}

	pnfs_clear_layoutcommitting(inode);
}

void pnfs_set_lo_fail(struct pnfs_layout_segment *lseg)
{
	pnfs_layout_io_set_failed(lseg->pls_layout, lseg->pls_range.iomode);
}
EXPORT_SYMBOL_GPL(pnfs_set_lo_fail);

void
pnfs_set_layoutcommit(struct nfs_pgio_header *hdr)
{
	struct inode *inode = hdr->inode;
	struct nfs_inode *nfsi = NFS_I(inode);
	loff_t end_pos = hdr->mds_offset + hdr->res.count;
	bool mark_as_dirty = false;

	spin_lock(&inode->i_lock);
	if (!test_and_set_bit(NFS_INO_LAYOUTCOMMIT, &nfsi->flags)) {
		mark_as_dirty = true;
		dprintk("%s: Set layoutcommit for inode %lu ",
			__func__, inode->i_ino);
	}
	if (!test_and_set_bit(NFS_LSEG_LAYOUTCOMMIT, &hdr->lseg->pls_flags)) {
		/* references matched in nfs4_layoutcommit_release */
		pnfs_get_lseg(hdr->lseg);
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

void pnfs_commit_set_layoutcommit(struct nfs_commit_data *data)
{
	struct inode *inode = data->inode;
	struct nfs_inode *nfsi = NFS_I(inode);
	bool mark_as_dirty = false;

	spin_lock(&inode->i_lock);
	if (!test_and_set_bit(NFS_INO_LAYOUTCOMMIT, &nfsi->flags)) {
		mark_as_dirty = true;
		dprintk("%s: Set layoutcommit for inode %lu ",
			__func__, inode->i_ino);
	}
	if (!test_and_set_bit(NFS_LSEG_LAYOUTCOMMIT, &data->lseg->pls_flags)) {
		/* references matched in nfs4_layoutcommit_release */
		pnfs_get_lseg(data->lseg);
	}
	if (data->lwb > nfsi->layout->plh_lwb)
		nfsi->layout->plh_lwb = data->lwb;
	spin_unlock(&inode->i_lock);
	dprintk("%s: lseg %p end_pos %llu\n",
		__func__, data->lseg, nfsi->layout->plh_lwb);

	/* if pnfs_layoutcommit_inode() runs between inode locks, the next one
	 * will be a noop because NFS_INO_LAYOUTCOMMIT will not be set */
	if (mark_as_dirty)
		mark_inode_dirty_sync(inode);
}
EXPORT_SYMBOL_GPL(pnfs_commit_set_layoutcommit);

void pnfs_cleanup_layoutcommit(struct nfs4_layoutcommit_data *data)
{
	struct nfs_server *nfss = NFS_SERVER(data->args.inode);

	if (nfss->pnfs_curr_ld->cleanup_layoutcommit)
		nfss->pnfs_curr_ld->cleanup_layoutcommit(data);
	pnfs_list_write_lseg_done(data->args.inode, &data->lseg_list);
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
	struct pnfs_layoutdriver_type *ld = NFS_SERVER(inode)->pnfs_curr_ld;
	struct nfs4_layoutcommit_data *data;
	struct nfs_inode *nfsi = NFS_I(inode);
	loff_t end_pos;
	int status;

	if (!pnfs_layoutcommit_outstanding(inode))
		return 0;

	dprintk("--> %s inode %lu\n", __func__, inode->i_ino);

	status = -EAGAIN;
	if (test_and_set_bit(NFS_INO_LAYOUTCOMMITTING, &nfsi->flags)) {
		if (!sync)
			goto out;
		status = wait_on_bit_lock_action(&nfsi->flags,
				NFS_INO_LAYOUTCOMMITTING,
				nfs_wait_bit_killable,
				TASK_KILLABLE);
		if (status)
			goto out;
	}

	status = -ENOMEM;
	/* Note kzalloc ensures data->res.seq_res.sr_slot == NULL */
	data = kzalloc(sizeof(*data), GFP_NOFS);
	if (!data)
		goto clear_layoutcommitting;

	status = 0;
	spin_lock(&inode->i_lock);
	if (!test_and_clear_bit(NFS_INO_LAYOUTCOMMIT, &nfsi->flags))
		goto out_unlock;

	INIT_LIST_HEAD(&data->lseg_list);
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

	if (ld->prepare_layoutcommit) {
		status = ld->prepare_layoutcommit(&data->args);
		if (status) {
			spin_lock(&inode->i_lock);
			if (end_pos < nfsi->layout->plh_lwb)
				nfsi->layout->plh_lwb = end_pos;
			spin_unlock(&inode->i_lock);
			put_rpccred(data->cred);
			set_bit(NFS_INO_LAYOUTCOMMIT, &nfsi->flags);
			goto clear_layoutcommitting;
		}
	}


	status = nfs4_proc_layoutcommit(data, sync);
out:
	if (status)
		mark_inode_dirty_sync(inode);
	dprintk("<-- %s status %d\n", __func__, status);
	return status;
out_unlock:
	spin_unlock(&inode->i_lock);
	kfree(data);
clear_layoutcommitting:
	pnfs_clear_layoutcommitting(inode);
	goto out;
}
EXPORT_SYMBOL_GPL(pnfs_layoutcommit_inode);

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
