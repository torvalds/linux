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
#include <linux/sort.h>
#include "internal.h"
#include "pnfs.h"
#include "iostat.h"
#include "nfs4trace.h"
#include "delegation.h"
#include "nfs42.h"
#include "nfs4_fs.h"

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

static void pnfs_layoutreturn_before_put_layout_hdr(struct pnfs_layout_hdr *lo);
static void pnfs_free_returned_lsegs(struct pnfs_layout_hdr *lo,
		struct list_head *free_me,
		const struct pnfs_layout_range *range,
		u32 seq);
static bool pnfs_lseg_dec_and_remove_zero(struct pnfs_layout_segment *lseg,
		                struct list_head *tmp_list);

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

const struct pnfs_layoutdriver_type *pnfs_find_layoutdriver(u32 id)
{
	return find_pnfs_driver(id);
}

void pnfs_put_layoutdriver(const struct pnfs_layoutdriver_type *ld)
{
	if (ld)
		module_put(ld->owner);
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
 * When the server sends a list of layout types, we choose one in the order
 * given in the list below.
 *
 * FIXME: should this list be configurable in some fashion? module param?
 * 	  mount option? something else?
 */
static const u32 ld_prefs[] = {
	LAYOUT_SCSI,
	LAYOUT_BLOCK_VOLUME,
	LAYOUT_OSD2_OBJECTS,
	LAYOUT_FLEX_FILES,
	LAYOUT_NFSV4_1_FILES,
	0
};

static int
ld_cmp(const void *e1, const void *e2)
{
	u32 ld1 = *((u32 *)e1);
	u32 ld2 = *((u32 *)e2);
	int i;

	for (i = 0; ld_prefs[i] != 0; i++) {
		if (ld1 == ld_prefs[i])
			return -1;

		if (ld2 == ld_prefs[i])
			return 1;
	}
	return 0;
}

/*
 * Try to set the server's pnfs module to the pnfs layout type specified by id.
 * Currently only one pNFS layout driver per filesystem is supported.
 *
 * @ids array of layout types supported by MDS.
 */
void
set_pnfs_layoutdriver(struct nfs_server *server, const struct nfs_fh *mntfh,
		      struct nfs_fsinfo *fsinfo)
{
	struct pnfs_layoutdriver_type *ld_type = NULL;
	u32 id;
	int i;

	if (fsinfo->nlayouttypes == 0)
		goto out_no_driver;
	if (!(server->nfs_client->cl_exchange_flags &
		 (EXCHGID4_FLAG_USE_NON_PNFS | EXCHGID4_FLAG_USE_PNFS_MDS))) {
		printk(KERN_ERR "NFS: %s: cl_exchange_flags 0x%x\n",
			__func__, server->nfs_client->cl_exchange_flags);
		goto out_no_driver;
	}

	sort(fsinfo->layouttype, fsinfo->nlayouttypes,
		sizeof(*fsinfo->layouttype), ld_cmp, NULL);

	for (i = 0; i < fsinfo->nlayouttypes; i++) {
		id = fsinfo->layouttype[i];
		ld_type = find_pnfs_driver(id);
		if (!ld_type) {
			request_module("%s-%u", LAYOUT_NFSV4_1_MODULE_PREFIX,
					id);
			ld_type = find_pnfs_driver(id);
		}
		if (ld_type)
			break;
	}

	if (!ld_type) {
		dprintk("%s: No pNFS module found!\n", __func__);
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
	refcount_inc(&lo->plh_refcount);
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

	if (test_and_clear_bit(NFS_LAYOUT_HASHED, &lo->plh_flags)) {
		struct nfs_client *clp = server->nfs_client;

		spin_lock(&clp->cl_lock);
		list_del_rcu(&lo->plh_layouts);
		spin_unlock(&clp->cl_lock);
	}
	put_cred(lo->plh_lc_cred);
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
	struct inode *inode;
	unsigned long i_state;

	if (!lo)
		return;
	inode = lo->plh_inode;
	pnfs_layoutreturn_before_put_layout_hdr(lo);

	if (refcount_dec_and_lock(&lo->plh_refcount, &inode->i_lock)) {
		if (!list_empty(&lo->plh_segs))
			WARN_ONCE(1, "NFS: BUG unfreed layout segments.\n");
		pnfs_detach_layout_hdr(lo);
		i_state = inode->i_state;
		spin_unlock(&inode->i_lock);
		pnfs_free_layout_hdr(lo);
		/* Notify pnfs_destroy_layout_final() that we're done */
		if (i_state & (I_FREEING | I_CLEAR))
			wake_up_var(lo);
	}
}

static struct inode *
pnfs_grab_inode_layout_hdr(struct pnfs_layout_hdr *lo)
{
	struct inode *inode = igrab(lo->plh_inode);
	if (inode)
		return inode;
	set_bit(NFS_LAYOUT_INODE_FREEING, &lo->plh_flags);
	return NULL;
}

/*
 * Compare 2 layout stateid sequence ids, to see which is newer,
 * taking into account wraparound issues.
 */
static bool pnfs_seqid_is_newer(u32 s1, u32 s2)
{
	return (s32)(s1 - s2) > 0;
}

static void pnfs_barrier_update(struct pnfs_layout_hdr *lo, u32 newseq)
{
	if (pnfs_seqid_is_newer(newseq, lo->plh_barrier) || !lo->plh_barrier)
		lo->plh_barrier = newseq;
}

static void
pnfs_set_plh_return_info(struct pnfs_layout_hdr *lo, enum pnfs_iomode iomode,
			 u32 seq)
{
	if (lo->plh_return_iomode != 0 && lo->plh_return_iomode != iomode)
		iomode = IOMODE_ANY;
	lo->plh_return_iomode = iomode;
	set_bit(NFS_LAYOUT_RETURN_REQUESTED, &lo->plh_flags);
	/*
	 * We must set lo->plh_return_seq to avoid livelocks with
	 * pnfs_layout_need_return()
	 */
	if (seq == 0)
		seq = be32_to_cpu(lo->plh_stateid.seqid);
	if (!lo->plh_return_seq || pnfs_seqid_is_newer(seq, lo->plh_return_seq))
		lo->plh_return_seq = seq;
	pnfs_barrier_update(lo, seq);
}

static void
pnfs_clear_layoutreturn_info(struct pnfs_layout_hdr *lo)
{
	struct pnfs_layout_segment *lseg;
	lo->plh_return_iomode = 0;
	lo->plh_return_seq = 0;
	clear_bit(NFS_LAYOUT_RETURN_REQUESTED, &lo->plh_flags);
	list_for_each_entry(lseg, &lo->plh_segs, pls_list) {
		if (!test_bit(NFS_LSEG_LAYOUTRETURN, &lseg->pls_flags))
			continue;
		pnfs_set_plh_return_info(lo, lseg->pls_range.iomode, 0);
	}
}

static void pnfs_clear_layoutreturn_waitbit(struct pnfs_layout_hdr *lo)
{
	clear_bit_unlock(NFS_LAYOUT_RETURN, &lo->plh_flags);
	clear_bit(NFS_LAYOUT_RETURN_LOCK, &lo->plh_flags);
	smp_mb__after_atomic();
	wake_up_bit(&lo->plh_flags, NFS_LAYOUT_RETURN);
	rpc_wake_up(&NFS_SERVER(lo->plh_inode)->roc_rpcwaitq);
}

static void
pnfs_clear_lseg_state(struct pnfs_layout_segment *lseg,
		struct list_head *free_me)
{
	clear_bit(NFS_LSEG_ROC, &lseg->pls_flags);
	clear_bit(NFS_LSEG_LAYOUTRETURN, &lseg->pls_flags);
	if (test_and_clear_bit(NFS_LSEG_VALID, &lseg->pls_flags))
		pnfs_lseg_dec_and_remove_zero(lseg, free_me);
	if (test_and_clear_bit(NFS_LSEG_LAYOUTCOMMIT, &lseg->pls_flags))
		pnfs_lseg_dec_and_remove_zero(lseg, free_me);
}

/*
 * Update the seqid of a layout stateid after receiving
 * NFS4ERR_OLD_STATEID
 */
bool nfs4_layout_refresh_old_stateid(nfs4_stateid *dst,
		struct pnfs_layout_range *dst_range,
		struct inode *inode)
{
	struct pnfs_layout_hdr *lo;
	struct pnfs_layout_range range = {
		.iomode = IOMODE_ANY,
		.offset = 0,
		.length = NFS4_MAX_UINT64,
	};
	bool ret = false;
	LIST_HEAD(head);
	int err;

	spin_lock(&inode->i_lock);
	lo = NFS_I(inode)->layout;
	if (lo &&  pnfs_layout_is_valid(lo) &&
	    nfs4_stateid_match_other(dst, &lo->plh_stateid)) {
		/* Is our call using the most recent seqid? If so, bump it */
		if (!nfs4_stateid_is_newer(&lo->plh_stateid, dst)) {
			nfs4_stateid_seqid_inc(dst);
			ret = true;
			goto out;
		}
		/* Try to update the seqid to the most recent */
		err = pnfs_mark_matching_lsegs_return(lo, &head, &range, 0);
		if (err != -EBUSY) {
			dst->seqid = lo->plh_stateid.seqid;
			*dst_range = range;
			ret = true;
		}
	}
out:
	spin_unlock(&inode->i_lock);
	pnfs_free_lseg_list(&head);
	return ret;
}

/*
 * Mark a pnfs_layout_hdr and all associated layout segments as invalid
 *
 * In order to continue using the pnfs_layout_hdr, a full recovery
 * is required.
 * Note that caller must hold inode->i_lock.
 */
int
pnfs_mark_layout_stateid_invalid(struct pnfs_layout_hdr *lo,
		struct list_head *lseg_list)
{
	struct pnfs_layout_range range = {
		.iomode = IOMODE_ANY,
		.offset = 0,
		.length = NFS4_MAX_UINT64,
	};
	struct pnfs_layout_segment *lseg, *next;

	set_bit(NFS_LAYOUT_INVALID_STID, &lo->plh_flags);
	list_for_each_entry_safe(lseg, next, &lo->plh_segs, pls_list)
		pnfs_clear_lseg_state(lseg, lseg_list);
	pnfs_clear_layoutreturn_info(lo);
	pnfs_free_returned_lsegs(lo, lseg_list, &range, 0);
	if (test_bit(NFS_LAYOUT_RETURN, &lo->plh_flags) &&
	    !test_and_set_bit(NFS_LAYOUT_RETURN_LOCK, &lo->plh_flags))
		pnfs_clear_layoutreturn_waitbit(lo);
	return !list_empty(&lo->plh_segs);
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
		refcount_inc(&lo->plh_refcount);
}

static void
pnfs_layout_clear_fail_bit(struct pnfs_layout_hdr *lo, int fail_bit)
{
	if (test_and_clear_bit(fail_bit, &lo->plh_flags))
		refcount_dec(&lo->plh_refcount);
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
	pnfs_mark_matching_lsegs_invalid(lo, &head, &range, 0);
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
pnfs_init_lseg(struct pnfs_layout_hdr *lo, struct pnfs_layout_segment *lseg,
		const struct pnfs_layout_range *range,
		const nfs4_stateid *stateid)
{
	INIT_LIST_HEAD(&lseg->pls_list);
	INIT_LIST_HEAD(&lseg->pls_lc_list);
	INIT_LIST_HEAD(&lseg->pls_commits);
	refcount_set(&lseg->pls_refcount, 1);
	set_bit(NFS_LSEG_VALID, &lseg->pls_flags);
	lseg->pls_layout = lo;
	lseg->pls_range = *range;
	lseg->pls_seq = be32_to_cpu(stateid->seqid);
}

static void pnfs_free_lseg(struct pnfs_layout_segment *lseg)
{
	if (lseg != NULL) {
		struct inode *inode = lseg->pls_layout->plh_inode;
		NFS_SERVER(inode)->pnfs_curr_ld->free_lseg(lseg);
	}
}

static void
pnfs_layout_remove_lseg(struct pnfs_layout_hdr *lo,
		struct pnfs_layout_segment *lseg)
{
	WARN_ON(test_bit(NFS_LSEG_VALID, &lseg->pls_flags));
	list_del_init(&lseg->pls_list);
	/* Matched by pnfs_get_layout_hdr in pnfs_layout_insert_lseg */
	refcount_dec(&lo->plh_refcount);
	if (test_bit(NFS_LSEG_LAYOUTRETURN, &lseg->pls_flags))
		return;
	if (list_empty(&lo->plh_segs) &&
	    !test_bit(NFS_LAYOUT_RETURN_REQUESTED, &lo->plh_flags) &&
	    !test_bit(NFS_LAYOUT_RETURN, &lo->plh_flags)) {
		if (atomic_read(&lo->plh_outstanding) == 0)
			set_bit(NFS_LAYOUT_INVALID_STID, &lo->plh_flags);
		clear_bit(NFS_LAYOUT_BULK_RECALL, &lo->plh_flags);
	}
}

static bool
pnfs_cache_lseg_for_layoutreturn(struct pnfs_layout_hdr *lo,
		struct pnfs_layout_segment *lseg)
{
	if (test_and_clear_bit(NFS_LSEG_LAYOUTRETURN, &lseg->pls_flags) &&
	    pnfs_layout_is_valid(lo)) {
		pnfs_set_plh_return_info(lo, lseg->pls_range.iomode, 0);
		list_move_tail(&lseg->pls_list, &lo->plh_return_segs);
		return true;
	}
	return false;
}

void
pnfs_put_lseg(struct pnfs_layout_segment *lseg)
{
	struct pnfs_layout_hdr *lo;
	struct inode *inode;

	if (!lseg)
		return;

	dprintk("%s: lseg %p ref %d valid %d\n", __func__, lseg,
		refcount_read(&lseg->pls_refcount),
		test_bit(NFS_LSEG_VALID, &lseg->pls_flags));

	lo = lseg->pls_layout;
	inode = lo->plh_inode;

	if (refcount_dec_and_lock(&lseg->pls_refcount, &inode->i_lock)) {
		if (test_bit(NFS_LSEG_VALID, &lseg->pls_flags)) {
			spin_unlock(&inode->i_lock);
			return;
		}
		pnfs_get_layout_hdr(lo);
		pnfs_layout_remove_lseg(lo, lseg);
		if (pnfs_cache_lseg_for_layoutreturn(lo, lseg))
			lseg = NULL;
		spin_unlock(&inode->i_lock);
		pnfs_free_lseg(lseg);
		pnfs_put_layout_hdr(lo);
	}
}
EXPORT_SYMBOL_GPL(pnfs_put_lseg);

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
	u64 end1 = pnfs_end_offset(start1, l1->length);
	u64 start2 = l2->offset;
	u64 end2 = pnfs_end_offset(start2, l2->length);

	return (start1 <= start2) && (end1 >= end2);
}

static bool pnfs_lseg_dec_and_remove_zero(struct pnfs_layout_segment *lseg,
		struct list_head *tmp_list)
{
	if (!refcount_dec_and_test(&lseg->pls_refcount))
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
			refcount_read(&lseg->pls_refcount));
		if (pnfs_lseg_dec_and_remove_zero(lseg, tmp_list))
			rv = 1;
	}
	return rv;
}

static bool
pnfs_should_free_range(const struct pnfs_layout_range *lseg_range,
		 const struct pnfs_layout_range *recall_range)
{
	return (recall_range->iomode == IOMODE_ANY ||
		lseg_range->iomode == recall_range->iomode) &&
	       pnfs_lseg_range_intersecting(lseg_range, recall_range);
}

static bool
pnfs_match_lseg_recall(const struct pnfs_layout_segment *lseg,
		const struct pnfs_layout_range *recall_range,
		u32 seq)
{
	if (seq != 0 && pnfs_seqid_is_newer(lseg->pls_seq, seq))
		return false;
	if (recall_range == NULL)
		return true;
	return pnfs_should_free_range(&lseg->pls_range, recall_range);
}

/**
 * pnfs_mark_matching_lsegs_invalid - tear down lsegs or mark them for later
 * @lo: layout header containing the lsegs
 * @tmp_list: list head where doomed lsegs should go
 * @recall_range: optional recall range argument to match (may be NULL)
 * @seq: only invalidate lsegs obtained prior to this sequence (may be 0)
 *
 * Walk the list of lsegs in the layout header, and tear down any that should
 * be destroyed. If "recall_range" is specified then the segment must match
 * that range. If "seq" is non-zero, then only match segments that were handed
 * out at or before that sequence.
 *
 * Returns number of matching invalid lsegs remaining in list after scanning
 * it and purging them.
 */
int
pnfs_mark_matching_lsegs_invalid(struct pnfs_layout_hdr *lo,
			    struct list_head *tmp_list,
			    const struct pnfs_layout_range *recall_range,
			    u32 seq)
{
	struct pnfs_layout_segment *lseg, *next;
	int remaining = 0;

	dprintk("%s:Begin lo %p\n", __func__, lo);

	if (list_empty(&lo->plh_segs))
		return 0;
	list_for_each_entry_safe(lseg, next, &lo->plh_segs, pls_list)
		if (pnfs_match_lseg_recall(lseg, recall_range, seq)) {
			dprintk("%s: freeing lseg %p iomode %d seq %u "
				"offset %llu length %llu\n", __func__,
				lseg, lseg->pls_range.iomode, lseg->pls_seq,
				lseg->pls_range.offset, lseg->pls_range.length);
			if (!mark_lseg_invalid(lseg, tmp_list))
				remaining++;
		}
	dprintk("%s:Return %i\n", __func__, remaining);
	return remaining;
}

static void
pnfs_free_returned_lsegs(struct pnfs_layout_hdr *lo,
		struct list_head *free_me,
		const struct pnfs_layout_range *range,
		u32 seq)
{
	struct pnfs_layout_segment *lseg, *next;

	list_for_each_entry_safe(lseg, next, &lo->plh_return_segs, pls_list) {
		if (pnfs_match_lseg_recall(lseg, range, seq))
			list_move_tail(&lseg->pls_list, free_me);
	}
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

static struct pnfs_layout_hdr *__pnfs_destroy_layout(struct nfs_inode *nfsi)
{
	struct pnfs_layout_hdr *lo;
	LIST_HEAD(tmp_list);

	spin_lock(&nfsi->vfs_inode.i_lock);
	lo = nfsi->layout;
	if (lo) {
		pnfs_get_layout_hdr(lo);
		pnfs_mark_layout_stateid_invalid(lo, &tmp_list);
		pnfs_layout_clear_fail_bit(lo, NFS_LAYOUT_RO_FAILED);
		pnfs_layout_clear_fail_bit(lo, NFS_LAYOUT_RW_FAILED);
		spin_unlock(&nfsi->vfs_inode.i_lock);
		pnfs_free_lseg_list(&tmp_list);
		nfs_commit_inode(&nfsi->vfs_inode, 0);
		pnfs_put_layout_hdr(lo);
	} else
		spin_unlock(&nfsi->vfs_inode.i_lock);
	return lo;
}

void pnfs_destroy_layout(struct nfs_inode *nfsi)
{
	__pnfs_destroy_layout(nfsi);
}
EXPORT_SYMBOL_GPL(pnfs_destroy_layout);

static bool pnfs_layout_removed(struct nfs_inode *nfsi,
				struct pnfs_layout_hdr *lo)
{
	bool ret;

	spin_lock(&nfsi->vfs_inode.i_lock);
	ret = nfsi->layout != lo;
	spin_unlock(&nfsi->vfs_inode.i_lock);
	return ret;
}

void pnfs_destroy_layout_final(struct nfs_inode *nfsi)
{
	struct pnfs_layout_hdr *lo = __pnfs_destroy_layout(nfsi);

	if (lo)
		wait_var_event(lo, pnfs_layout_removed(nfsi, lo));
}

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
	__must_hold(&clp->cl_lock)
	__must_hold(RCU)
{
	struct pnfs_layout_hdr *lo, *next;
	struct inode *inode;

	list_for_each_entry_safe(lo, next, &server->layouts, plh_layouts) {
		if (test_bit(NFS_LAYOUT_INVALID_STID, &lo->plh_flags) ||
		    test_bit(NFS_LAYOUT_INODE_FREEING, &lo->plh_flags) ||
		    !list_empty(&lo->plh_bulk_destroy))
			continue;
		/* If the sb is being destroyed, just bail */
		if (!nfs_sb_active(server->super))
			break;
		inode = pnfs_grab_inode_layout_hdr(lo);
		if (inode != NULL) {
			if (test_and_clear_bit(NFS_LAYOUT_HASHED, &lo->plh_flags))
				list_del_rcu(&lo->plh_layouts);
			if (pnfs_layout_add_bulk_destroy_list(inode,
						layout_list))
				continue;
			rcu_read_unlock();
			spin_unlock(&clp->cl_lock);
			iput(inode);
		} else {
			rcu_read_unlock();
			spin_unlock(&clp->cl_lock);
		}
		nfs_sb_deactive(server->super);
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
		if (pnfs_mark_layout_stateid_invalid(lo, &lseg_list)) {
			if (is_bulk_recall)
				set_bit(NFS_LAYOUT_BULK_RECALL, &lo->plh_flags);
			ret = -EAGAIN;
		}
		spin_unlock(&inode->i_lock);
		pnfs_free_lseg_list(&lseg_list);
		/* Free all lsegs that are attached to commit buckets */
		nfs_commit_inode(inode, 0);
		pnfs_put_layout_hdr(lo);
		nfs_iput_and_deactive(inode);
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
 * Called by the state manager to remove all layouts established under an
 * expired lease.
 */
void
pnfs_destroy_all_layouts(struct nfs_client *clp)
{
	nfs4_deviceid_mark_client_invalid(clp);
	nfs4_deviceid_purge_client(clp);

	pnfs_destroy_layouts_byclid(clp, false);
}

static void
pnfs_set_layout_cred(struct pnfs_layout_hdr *lo, const struct cred *cred)
{
	const struct cred *old;

	if (cred && cred_fscmp(lo->plh_lc_cred, cred) != 0) {
		old = xchg(&lo->plh_lc_cred, get_cred(cred));
		put_cred(old);
	}
}

/* update lo->plh_stateid with new if is more recent */
void
pnfs_set_layout_stateid(struct pnfs_layout_hdr *lo, const nfs4_stateid *new,
			const struct cred *cred, bool update_barrier)
{
	u32 oldseq = be32_to_cpu(lo->plh_stateid.seqid);
	u32 newseq = be32_to_cpu(new->seqid);

	if (!pnfs_layout_is_valid(lo)) {
		pnfs_set_layout_cred(lo, cred);
		nfs4_stateid_copy(&lo->plh_stateid, new);
		lo->plh_barrier = newseq;
		pnfs_clear_layoutreturn_info(lo);
		clear_bit(NFS_LAYOUT_INVALID_STID, &lo->plh_flags);
		return;
	}

	if (pnfs_seqid_is_newer(newseq, oldseq))
		nfs4_stateid_copy(&lo->plh_stateid, new);

	if (update_barrier) {
		pnfs_barrier_update(lo, newseq);
		return;
	}
	/*
	 * Because of wraparound, we want to keep the barrier
	 * "close" to the current seqids. We really only want to
	 * get here from a layoutget call.
	 */
	if (atomic_read(&lo->plh_outstanding) == 1)
		 pnfs_barrier_update(lo, be32_to_cpu(lo->plh_stateid.seqid));
}

static bool
pnfs_layout_stateid_blocked(const struct pnfs_layout_hdr *lo,
		const nfs4_stateid *stateid)
{
	u32 seqid = be32_to_cpu(stateid->seqid);

	return lo->plh_barrier && pnfs_seqid_is_newer(lo->plh_barrier, seqid);
}

/* lget is set to 1 if called from inside send_layoutget call chain */
static bool
pnfs_layoutgets_blocked(const struct pnfs_layout_hdr *lo)
{
	return lo->plh_block_lgets ||
		test_bit(NFS_LAYOUT_BULK_RECALL, &lo->plh_flags);
}

static struct nfs_server *
pnfs_find_server(struct inode *inode, struct nfs_open_context *ctx)
{
	struct nfs_server *server;

	if (inode) {
		server = NFS_SERVER(inode);
	} else {
		struct dentry *parent_dir = dget_parent(ctx->dentry);
		server = NFS_SERVER(parent_dir->d_inode);
		dput(parent_dir);
	}
	return server;
}

static void nfs4_free_pages(struct page **pages, size_t size)
{
	int i;

	if (!pages)
		return;

	for (i = 0; i < size; i++) {
		if (!pages[i])
			break;
		__free_page(pages[i]);
	}
	kfree(pages);
}

static struct page **nfs4_alloc_pages(size_t size, gfp_t gfp_flags)
{
	struct page **pages;
	int i;

	pages = kmalloc_array(size, sizeof(struct page *), gfp_flags);
	if (!pages) {
		dprintk("%s: can't alloc array of %zu pages\n", __func__, size);
		return NULL;
	}

	for (i = 0; i < size; i++) {
		pages[i] = alloc_page(gfp_flags);
		if (!pages[i]) {
			dprintk("%s: failed to allocate page\n", __func__);
			nfs4_free_pages(pages, i);
			return NULL;
		}
	}

	return pages;
}

static struct nfs4_layoutget *
pnfs_alloc_init_layoutget_args(struct inode *ino,
	   struct nfs_open_context *ctx,
	   const nfs4_stateid *stateid,
	   const struct pnfs_layout_range *range,
	   gfp_t gfp_flags)
{
	struct nfs_server *server = pnfs_find_server(ino, ctx);
	size_t max_reply_sz = server->pnfs_curr_ld->max_layoutget_response;
	size_t max_pages = max_response_pages(server);
	struct nfs4_layoutget *lgp;

	dprintk("--> %s\n", __func__);

	lgp = kzalloc(sizeof(*lgp), gfp_flags);
	if (lgp == NULL)
		return NULL;

	if (max_reply_sz) {
		size_t npages = (max_reply_sz + PAGE_SIZE - 1) >> PAGE_SHIFT;
		if (npages < max_pages)
			max_pages = npages;
	}

	lgp->args.layout.pages = nfs4_alloc_pages(max_pages, gfp_flags);
	if (!lgp->args.layout.pages) {
		kfree(lgp);
		return NULL;
	}
	lgp->args.layout.pglen = max_pages * PAGE_SIZE;
	lgp->res.layoutp = &lgp->args.layout;

	/* Don't confuse uninitialised result and success */
	lgp->res.status = -NFS4ERR_DELAY;

	lgp->args.minlength = PAGE_SIZE;
	if (lgp->args.minlength > range->length)
		lgp->args.minlength = range->length;
	if (ino) {
		loff_t i_size = i_size_read(ino);

		if (range->iomode == IOMODE_READ) {
			if (range->offset >= i_size)
				lgp->args.minlength = 0;
			else if (i_size - range->offset < lgp->args.minlength)
				lgp->args.minlength = i_size - range->offset;
		}
	}
	lgp->args.maxcount = PNFS_LAYOUT_MAXSIZE;
	pnfs_copy_range(&lgp->args.range, range);
	lgp->args.type = server->pnfs_curr_ld->id;
	lgp->args.inode = ino;
	lgp->args.ctx = get_nfs_open_context(ctx);
	nfs4_stateid_copy(&lgp->args.stateid, stateid);
	lgp->gfp_flags = gfp_flags;
	lgp->cred = ctx->cred;
	return lgp;
}

void pnfs_layoutget_free(struct nfs4_layoutget *lgp)
{
	size_t max_pages = lgp->args.layout.pglen / PAGE_SIZE;

	nfs4_free_pages(lgp->args.layout.pages, max_pages);
	if (lgp->args.inode)
		pnfs_put_layout_hdr(NFS_I(lgp->args.inode)->layout);
	put_nfs_open_context(lgp->args.ctx);
	kfree(lgp);
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

void pnfs_layoutreturn_free_lsegs(struct pnfs_layout_hdr *lo,
		const nfs4_stateid *arg_stateid,
		const struct pnfs_layout_range *range,
		const nfs4_stateid *stateid)
{
	struct inode *inode = lo->plh_inode;
	LIST_HEAD(freeme);

	spin_lock(&inode->i_lock);
	if (!pnfs_layout_is_valid(lo) || !arg_stateid ||
	    !nfs4_stateid_match_other(&lo->plh_stateid, arg_stateid))
		goto out_unlock;
	if (stateid) {
		u32 seq = be32_to_cpu(arg_stateid->seqid);

		pnfs_mark_matching_lsegs_invalid(lo, &freeme, range, seq);
		pnfs_free_returned_lsegs(lo, &freeme, range, seq);
		pnfs_set_layout_stateid(lo, stateid, NULL, true);
	} else
		pnfs_mark_layout_stateid_invalid(lo, &freeme);
out_unlock:
	pnfs_clear_layoutreturn_waitbit(lo);
	spin_unlock(&inode->i_lock);
	pnfs_free_lseg_list(&freeme);

}

static bool
pnfs_prepare_layoutreturn(struct pnfs_layout_hdr *lo,
		nfs4_stateid *stateid,
		const struct cred **cred,
		enum pnfs_iomode *iomode)
{
	/* Serialise LAYOUTGET/LAYOUTRETURN */
	if (atomic_read(&lo->plh_outstanding) != 0)
		return false;
	if (test_and_set_bit(NFS_LAYOUT_RETURN_LOCK, &lo->plh_flags))
		return false;
	set_bit(NFS_LAYOUT_RETURN, &lo->plh_flags);
	pnfs_get_layout_hdr(lo);
	nfs4_stateid_copy(stateid, &lo->plh_stateid);
	*cred = get_cred(lo->plh_lc_cred);
	if (test_bit(NFS_LAYOUT_RETURN_REQUESTED, &lo->plh_flags)) {
		if (lo->plh_return_seq != 0)
			stateid->seqid = cpu_to_be32(lo->plh_return_seq);
		if (iomode != NULL)
			*iomode = lo->plh_return_iomode;
		pnfs_clear_layoutreturn_info(lo);
	} else if (iomode != NULL)
		*iomode = IOMODE_ANY;
	pnfs_barrier_update(lo, be32_to_cpu(stateid->seqid));
	return true;
}

static void
pnfs_init_layoutreturn_args(struct nfs4_layoutreturn_args *args,
		struct pnfs_layout_hdr *lo,
		const nfs4_stateid *stateid,
		enum pnfs_iomode iomode)
{
	struct inode *inode = lo->plh_inode;

	args->layout_type = NFS_SERVER(inode)->pnfs_curr_ld->id;
	args->inode = inode;
	args->range.iomode = iomode;
	args->range.offset = 0;
	args->range.length = NFS4_MAX_UINT64;
	args->layout = lo;
	nfs4_stateid_copy(&args->stateid, stateid);
}

static int
pnfs_send_layoutreturn(struct pnfs_layout_hdr *lo,
		       const nfs4_stateid *stateid,
		       const struct cred **pcred,
		       enum pnfs_iomode iomode,
		       bool sync)
{
	struct inode *ino = lo->plh_inode;
	struct pnfs_layoutdriver_type *ld = NFS_SERVER(ino)->pnfs_curr_ld;
	struct nfs4_layoutreturn *lrp;
	const struct cred *cred = *pcred;
	int status = 0;

	*pcred = NULL;
	lrp = kzalloc(sizeof(*lrp), GFP_NOFS);
	if (unlikely(lrp == NULL)) {
		status = -ENOMEM;
		spin_lock(&ino->i_lock);
		pnfs_clear_layoutreturn_waitbit(lo);
		spin_unlock(&ino->i_lock);
		put_cred(cred);
		pnfs_put_layout_hdr(lo);
		goto out;
	}

	pnfs_init_layoutreturn_args(&lrp->args, lo, stateid, iomode);
	lrp->args.ld_private = &lrp->ld_private;
	lrp->clp = NFS_SERVER(ino)->nfs_client;
	lrp->cred = cred;
	if (ld->prepare_layoutreturn)
		ld->prepare_layoutreturn(&lrp->args);

	status = nfs4_proc_layoutreturn(lrp, sync);
out:
	dprintk("<-- %s status: %d\n", __func__, status);
	return status;
}

static bool
pnfs_layout_segments_returnable(struct pnfs_layout_hdr *lo,
				enum pnfs_iomode iomode,
				u32 seq)
{
	struct pnfs_layout_range recall_range = {
		.length = NFS4_MAX_UINT64,
		.iomode = iomode,
	};
	return pnfs_mark_matching_lsegs_return(lo, &lo->plh_return_segs,
					       &recall_range, seq) != -EBUSY;
}

/* Return true if layoutreturn is needed */
static bool
pnfs_layout_need_return(struct pnfs_layout_hdr *lo)
{
	if (!test_bit(NFS_LAYOUT_RETURN_REQUESTED, &lo->plh_flags))
		return false;
	return pnfs_layout_segments_returnable(lo, lo->plh_return_iomode,
					       lo->plh_return_seq);
}

static void pnfs_layoutreturn_before_put_layout_hdr(struct pnfs_layout_hdr *lo)
{
	struct inode *inode= lo->plh_inode;

	if (!test_bit(NFS_LAYOUT_RETURN_REQUESTED, &lo->plh_flags))
		return;
	spin_lock(&inode->i_lock);
	if (pnfs_layout_need_return(lo)) {
		const struct cred *cred;
		nfs4_stateid stateid;
		enum pnfs_iomode iomode;
		bool send;

		send = pnfs_prepare_layoutreturn(lo, &stateid, &cred, &iomode);
		spin_unlock(&inode->i_lock);
		if (send) {
			/* Send an async layoutreturn so we dont deadlock */
			pnfs_send_layoutreturn(lo, &stateid, &cred, iomode, false);
		}
	} else
		spin_unlock(&inode->i_lock);
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
	struct pnfs_layout_range range = {
		.iomode		= IOMODE_ANY,
		.offset		= 0,
		.length		= NFS4_MAX_UINT64,
	};
	LIST_HEAD(tmp_list);
	const struct cred *cred;
	nfs4_stateid stateid;
	int status = 0;
	bool send, valid_layout;

	dprintk("NFS: %s for inode %lu\n", __func__, ino->i_ino);

	spin_lock(&ino->i_lock);
	lo = nfsi->layout;
	if (!lo) {
		spin_unlock(&ino->i_lock);
		dprintk("NFS: %s no layout to return\n", __func__);
		goto out;
	}
	/* Reference matched in nfs4_layoutreturn_release */
	pnfs_get_layout_hdr(lo);
	/* Is there an outstanding layoutreturn ? */
	if (test_bit(NFS_LAYOUT_RETURN_LOCK, &lo->plh_flags)) {
		spin_unlock(&ino->i_lock);
		if (wait_on_bit(&lo->plh_flags, NFS_LAYOUT_RETURN,
					TASK_UNINTERRUPTIBLE))
			goto out_put_layout_hdr;
		spin_lock(&ino->i_lock);
	}
	valid_layout = pnfs_layout_is_valid(lo);
	pnfs_clear_layoutcommit(ino, &tmp_list);
	pnfs_mark_matching_lsegs_return(lo, &tmp_list, &range, 0);

	if (NFS_SERVER(ino)->pnfs_curr_ld->return_range)
		NFS_SERVER(ino)->pnfs_curr_ld->return_range(lo, &range);

	/* Don't send a LAYOUTRETURN if list was initially empty */
	if (!test_bit(NFS_LAYOUT_RETURN_REQUESTED, &lo->plh_flags) ||
			!valid_layout) {
		spin_unlock(&ino->i_lock);
		dprintk("NFS: %s no layout segments to return\n", __func__);
		goto out_wait_layoutreturn;
	}

	send = pnfs_prepare_layoutreturn(lo, &stateid, &cred, NULL);
	spin_unlock(&ino->i_lock);
	if (send)
		status = pnfs_send_layoutreturn(lo, &stateid, &cred, IOMODE_ANY, true);
out_wait_layoutreturn:
	wait_on_bit(&lo->plh_flags, NFS_LAYOUT_RETURN, TASK_UNINTERRUPTIBLE);
out_put_layout_hdr:
	pnfs_free_lseg_list(&tmp_list);
	pnfs_put_layout_hdr(lo);
out:
	dprintk("<-- %s status: %d\n", __func__, status);
	return status;
}

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

bool pnfs_roc(struct inode *ino,
		struct nfs4_layoutreturn_args *args,
		struct nfs4_layoutreturn_res *res,
		const struct cred *cred)
{
	struct nfs_inode *nfsi = NFS_I(ino);
	struct nfs_open_context *ctx;
	struct nfs4_state *state;
	struct pnfs_layout_hdr *lo;
	struct pnfs_layout_segment *lseg, *next;
	const struct cred *lc_cred;
	nfs4_stateid stateid;
	enum pnfs_iomode iomode = 0;
	bool layoutreturn = false, roc = false;
	bool skip_read = false;

	if (!nfs_have_layout(ino))
		return false;
retry:
	rcu_read_lock();
	spin_lock(&ino->i_lock);
	lo = nfsi->layout;
	if (!lo || !pnfs_layout_is_valid(lo) ||
	    test_bit(NFS_LAYOUT_BULK_RECALL, &lo->plh_flags)) {
		lo = NULL;
		goto out_noroc;
	}
	pnfs_get_layout_hdr(lo);
	if (test_bit(NFS_LAYOUT_RETURN_LOCK, &lo->plh_flags)) {
		spin_unlock(&ino->i_lock);
		rcu_read_unlock();
		wait_on_bit(&lo->plh_flags, NFS_LAYOUT_RETURN,
				TASK_UNINTERRUPTIBLE);
		pnfs_put_layout_hdr(lo);
		goto retry;
	}

	/* no roc if we hold a delegation */
	if (nfs4_check_delegation(ino, FMODE_READ)) {
		if (nfs4_check_delegation(ino, FMODE_WRITE))
			goto out_noroc;
		skip_read = true;
	}

	list_for_each_entry_rcu(ctx, &nfsi->open_files, list) {
		state = ctx->state;
		if (state == NULL)
			continue;
		/* Don't return layout if there is open file state */
		if (state->state & FMODE_WRITE)
			goto out_noroc;
		if (state->state & FMODE_READ)
			skip_read = true;
	}


	list_for_each_entry_safe(lseg, next, &lo->plh_segs, pls_list) {
		if (skip_read && lseg->pls_range.iomode == IOMODE_READ)
			continue;
		/* If we are sending layoutreturn, invalidate all valid lsegs */
		if (!test_and_clear_bit(NFS_LSEG_ROC, &lseg->pls_flags))
			continue;
		/*
		 * Note: mark lseg for return so pnfs_layout_remove_lseg
		 * doesn't invalidate the layout for us.
		 */
		set_bit(NFS_LSEG_LAYOUTRETURN, &lseg->pls_flags);
		if (!mark_lseg_invalid(lseg, &lo->plh_return_segs))
			continue;
		pnfs_set_plh_return_info(lo, lseg->pls_range.iomode, 0);
	}

	if (!test_bit(NFS_LAYOUT_RETURN_REQUESTED, &lo->plh_flags))
		goto out_noroc;

	/* ROC in two conditions:
	 * 1. there are ROC lsegs
	 * 2. we don't send layoutreturn
	 */
	/* lo ref dropped in pnfs_roc_release() */
	layoutreturn = pnfs_prepare_layoutreturn(lo, &stateid, &lc_cred, &iomode);
	/* If the creds don't match, we can't compound the layoutreturn */
	if (!layoutreturn || cred_fscmp(cred, lc_cred) != 0)
		goto out_noroc;

	roc = layoutreturn;
	pnfs_init_layoutreturn_args(args, lo, &stateid, iomode);
	res->lrs_present = 0;
	layoutreturn = false;
	put_cred(lc_cred);

out_noroc:
	spin_unlock(&ino->i_lock);
	rcu_read_unlock();
	pnfs_layoutcommit_inode(ino, true);
	if (roc) {
		struct pnfs_layoutdriver_type *ld = NFS_SERVER(ino)->pnfs_curr_ld;
		if (ld->prepare_layoutreturn)
			ld->prepare_layoutreturn(args);
		pnfs_put_layout_hdr(lo);
		return true;
	}
	if (layoutreturn)
		pnfs_send_layoutreturn(lo, &stateid, &lc_cred, iomode, true);
	pnfs_put_layout_hdr(lo);
	return false;
}

int pnfs_roc_done(struct rpc_task *task, struct nfs4_layoutreturn_args **argpp,
		  struct nfs4_layoutreturn_res **respp, int *ret)
{
	struct nfs4_layoutreturn_args *arg = *argpp;
	int retval = -EAGAIN;

	if (!arg)
		return 0;
	/* Handle Layoutreturn errors */
	switch (*ret) {
	case 0:
		retval = 0;
		break;
	case -NFS4ERR_NOMATCHING_LAYOUT:
		/* Was there an RPC level error? If not, retry */
		if (task->tk_rpc_status == 0)
			break;
		/* If the call was not sent, let caller handle it */
		if (!RPC_WAS_SENT(task))
			return 0;
		/*
		 * Otherwise, assume the call succeeded and
		 * that we need to release the layout
		 */
		*ret = 0;
		(*respp)->lrs_present = 0;
		retval = 0;
		break;
	case -NFS4ERR_DELAY:
		/* Let the caller handle the retry */
		*ret = -NFS4ERR_NOMATCHING_LAYOUT;
		return 0;
	case -NFS4ERR_OLD_STATEID:
		if (!nfs4_layout_refresh_old_stateid(&arg->stateid,
						     &arg->range, arg->inode))
			break;
		*ret = -NFS4ERR_NOMATCHING_LAYOUT;
		return -EAGAIN;
	}
	*argpp = NULL;
	*respp = NULL;
	return retval;
}

void pnfs_roc_release(struct nfs4_layoutreturn_args *args,
		struct nfs4_layoutreturn_res *res,
		int ret)
{
	struct pnfs_layout_hdr *lo = args->layout;
	struct inode *inode = args->inode;
	const nfs4_stateid *arg_stateid = NULL;
	const nfs4_stateid *res_stateid = NULL;
	struct nfs4_xdr_opaque_data *ld_private = args->ld_private;

	switch (ret) {
	case -NFS4ERR_NOMATCHING_LAYOUT:
		spin_lock(&inode->i_lock);
		if (pnfs_layout_is_valid(lo) &&
		    nfs4_stateid_match_other(&args->stateid, &lo->plh_stateid))
			pnfs_set_plh_return_info(lo, args->range.iomode, 0);
		spin_unlock(&inode->i_lock);
		break;
	case 0:
		if (res->lrs_present)
			res_stateid = &res->stateid;
		fallthrough;
	default:
		arg_stateid = &args->stateid;
	}
	trace_nfs4_layoutreturn_on_close(args->inode, &args->stateid, ret);
	pnfs_layoutreturn_free_lsegs(lo, arg_stateid, &args->range,
			res_stateid);
	if (ld_private && ld_private->ops && ld_private->ops->free)
		ld_private->ops->free(ld_private);
	pnfs_put_layout_hdr(lo);
}

bool pnfs_wait_on_layoutreturn(struct inode *ino, struct rpc_task *task)
{
	struct nfs_inode *nfsi = NFS_I(ino);
        struct pnfs_layout_hdr *lo;
        bool sleep = false;

	/* we might not have grabbed lo reference. so need to check under
	 * i_lock */
        spin_lock(&ino->i_lock);
        lo = nfsi->layout;
        if (lo && test_bit(NFS_LAYOUT_RETURN, &lo->plh_flags)) {
                rpc_sleep_on(&NFS_SERVER(ino)->roc_rpcwaitq, task, NULL);
                sleep = true;
	}
        spin_unlock(&ino->i_lock);
        return sleep;
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

static bool
pnfs_lseg_range_is_after(const struct pnfs_layout_range *l1,
		const struct pnfs_layout_range *l2)
{
	return pnfs_lseg_range_cmp(l1, l2) > 0;
}

static bool
pnfs_lseg_no_merge(struct pnfs_layout_segment *lseg,
		struct pnfs_layout_segment *old)
{
	return false;
}

void
pnfs_generic_layout_insert_lseg(struct pnfs_layout_hdr *lo,
		   struct pnfs_layout_segment *lseg,
		   bool (*is_after)(const struct pnfs_layout_range *,
			   const struct pnfs_layout_range *),
		   bool (*do_merge)(struct pnfs_layout_segment *,
			   struct pnfs_layout_segment *),
		   struct list_head *free_me)
{
	struct pnfs_layout_segment *lp, *tmp;

	dprintk("%s:Begin\n", __func__);

	list_for_each_entry_safe(lp, tmp, &lo->plh_segs, pls_list) {
		if (test_bit(NFS_LSEG_VALID, &lp->pls_flags) == 0)
			continue;
		if (do_merge(lseg, lp)) {
			mark_lseg_invalid(lp, free_me);
			continue;
		}
		if (is_after(&lseg->pls_range, &lp->pls_range))
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
EXPORT_SYMBOL_GPL(pnfs_generic_layout_insert_lseg);

static void
pnfs_layout_insert_lseg(struct pnfs_layout_hdr *lo,
		   struct pnfs_layout_segment *lseg,
		   struct list_head *free_me)
{
	struct inode *inode = lo->plh_inode;
	struct pnfs_layoutdriver_type *ld = NFS_SERVER(inode)->pnfs_curr_ld;

	if (ld->add_lseg != NULL)
		ld->add_lseg(lo, lseg, free_me);
	else
		pnfs_generic_layout_insert_lseg(lo, lseg,
				pnfs_lseg_range_is_after,
				pnfs_lseg_no_merge,
				free_me);
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
	refcount_set(&lo->plh_refcount, 1);
	INIT_LIST_HEAD(&lo->plh_layouts);
	INIT_LIST_HEAD(&lo->plh_segs);
	INIT_LIST_HEAD(&lo->plh_return_segs);
	INIT_LIST_HEAD(&lo->plh_bulk_destroy);
	lo->plh_inode = ino;
	lo->plh_lc_cred = get_cred(ctx->cred);
	lo->plh_flags |= 1 << NFS_LAYOUT_INVALID_STID;
	return lo;
}

static struct pnfs_layout_hdr *
pnfs_find_alloc_layout(struct inode *ino,
		       struct nfs_open_context *ctx,
		       gfp_t gfp_flags)
	__releases(&ino->i_lock)
	__acquires(&ino->i_lock)
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
 * iomode	lseg	strict match
 *                      iomode
 * -----	-----	------ -----
 * ANY		READ	N/A    true
 * ANY		RW	N/A    true
 * RW		READ	N/A    false
 * RW		RW	N/A    true
 * READ		READ	N/A    true
 * READ		RW	true   false
 * READ		RW	false  true
 */
static bool
pnfs_lseg_range_match(const struct pnfs_layout_range *ls_range,
		 const struct pnfs_layout_range *range,
		 bool strict_iomode)
{
	struct pnfs_layout_range range1;

	if ((range->iomode == IOMODE_RW &&
	     ls_range->iomode != IOMODE_RW) ||
	    (range->iomode != ls_range->iomode &&
	     strict_iomode) ||
	    !pnfs_lseg_range_intersecting(ls_range, range))
		return false;

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
		struct pnfs_layout_range *range,
		bool strict_iomode)
{
	struct pnfs_layout_segment *lseg, *ret = NULL;

	dprintk("%s:Begin\n", __func__);

	list_for_each_entry(lseg, &lo->plh_segs, pls_list) {
		if (test_bit(NFS_LSEG_VALID, &lseg->pls_flags) &&
		    pnfs_lseg_range_match(&lseg->pls_range, range,
					  strict_iomode)) {
			ret = pnfs_get_lseg(lseg);
			break;
		}
	}

	dprintk("%s:Return lseg %p ref %d\n",
		__func__, ret, ret ? refcount_read(&ret->pls_refcount) : 0);
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

static int pnfs_prepare_to_retry_layoutget(struct pnfs_layout_hdr *lo)
{
	/*
	 * send layoutcommit as it can hold up layoutreturn due to lseg
	 * reference
	 */
	pnfs_layoutcommit_inode(lo->plh_inode, false);
	return wait_on_bit_action(&lo->plh_flags, NFS_LAYOUT_RETURN,
				   nfs_wait_bit_killable,
				   TASK_KILLABLE);
}

static void nfs_layoutget_begin(struct pnfs_layout_hdr *lo)
{
	atomic_inc(&lo->plh_outstanding);
}

static void nfs_layoutget_end(struct pnfs_layout_hdr *lo)
{
	if (atomic_dec_and_test(&lo->plh_outstanding))
		wake_up_var(&lo->plh_outstanding);
}

static bool pnfs_is_first_layoutget(struct pnfs_layout_hdr *lo)
{
	return test_bit(NFS_LAYOUT_FIRST_LAYOUTGET, &lo->plh_flags);
}

static void pnfs_clear_first_layoutget(struct pnfs_layout_hdr *lo)
{
	unsigned long *bitlock = &lo->plh_flags;

	clear_bit_unlock(NFS_LAYOUT_FIRST_LAYOUTGET, bitlock);
	smp_mb__after_atomic();
	wake_up_bit(bitlock, NFS_LAYOUT_FIRST_LAYOUTGET);
}

static void _add_to_server_list(struct pnfs_layout_hdr *lo,
				struct nfs_server *server)
{
	if (!test_and_set_bit(NFS_LAYOUT_HASHED, &lo->plh_flags)) {
		struct nfs_client *clp = server->nfs_client;

		/* The lo must be on the clp list if there is any
		 * chance of a CB_LAYOUTRECALL(FILE) coming in.
		 */
		spin_lock(&clp->cl_lock);
		list_add_tail_rcu(&lo->plh_layouts, &server->layouts);
		spin_unlock(&clp->cl_lock);
	}
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
		   bool strict_iomode,
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
	struct pnfs_layout_hdr *lo = NULL;
	struct pnfs_layout_segment *lseg = NULL;
	struct nfs4_layoutget *lgp;
	nfs4_stateid stateid;
	long timeout = 0;
	unsigned long giveup = jiffies + (clp->cl_lease_time << 1);
	bool first;

	if (!pnfs_enabled_sb(NFS_SERVER(ino))) {
		trace_pnfs_update_layout(ino, pos, count, iomode, lo, lseg,
				 PNFS_UPDATE_LAYOUT_NO_PNFS);
		goto out;
	}

	if (pnfs_within_mdsthreshold(ctx, ino, iomode)) {
		trace_pnfs_update_layout(ino, pos, count, iomode, lo, lseg,
				 PNFS_UPDATE_LAYOUT_MDSTHRESH);
		goto out;
	}

lookup_again:
	lseg = ERR_PTR(nfs4_client_recover_expired_lease(clp));
	if (IS_ERR(lseg))
		goto out;
	first = false;
	spin_lock(&ino->i_lock);
	lo = pnfs_find_alloc_layout(ino, ctx, gfp_flags);
	if (lo == NULL) {
		spin_unlock(&ino->i_lock);
		lseg = ERR_PTR(-ENOMEM);
		trace_pnfs_update_layout(ino, pos, count, iomode, lo, lseg,
				 PNFS_UPDATE_LAYOUT_NOMEM);
		goto out;
	}

	/* Do we even need to bother with this? */
	if (test_bit(NFS_LAYOUT_BULK_RECALL, &lo->plh_flags)) {
		trace_pnfs_update_layout(ino, pos, count, iomode, lo, lseg,
				 PNFS_UPDATE_LAYOUT_BULK_RECALL);
		dprintk("%s matches recall, use MDS\n", __func__);
		goto out_unlock;
	}

	/* if LAYOUTGET already failed once we don't try again */
	if (pnfs_layout_io_test_failed(lo, iomode)) {
		trace_pnfs_update_layout(ino, pos, count, iomode, lo, lseg,
				 PNFS_UPDATE_LAYOUT_IO_TEST_FAIL);
		goto out_unlock;
	}

	/*
	 * If the layout segment list is empty, but there are outstanding
	 * layoutget calls, then they might be subject to a layoutrecall.
	 */
	if ((list_empty(&lo->plh_segs) || !pnfs_layout_is_valid(lo)) &&
	    atomic_read(&lo->plh_outstanding) != 0) {
		spin_unlock(&ino->i_lock);
		lseg = ERR_PTR(wait_var_event_killable(&lo->plh_outstanding,
					!atomic_read(&lo->plh_outstanding)));
		if (IS_ERR(lseg))
			goto out_put_layout_hdr;
		pnfs_put_layout_hdr(lo);
		goto lookup_again;
	}

	/*
	 * Because we free lsegs when sending LAYOUTRETURN, we need to wait
	 * for LAYOUTRETURN.
	 */
	if (test_bit(NFS_LAYOUT_RETURN, &lo->plh_flags)) {
		spin_unlock(&ino->i_lock);
		dprintk("%s wait for layoutreturn\n", __func__);
		lseg = ERR_PTR(pnfs_prepare_to_retry_layoutget(lo));
		if (!IS_ERR(lseg)) {
			pnfs_put_layout_hdr(lo);
			dprintk("%s retrying\n", __func__);
			trace_pnfs_update_layout(ino, pos, count, iomode, lo,
						 lseg,
						 PNFS_UPDATE_LAYOUT_RETRY);
			goto lookup_again;
		}
		trace_pnfs_update_layout(ino, pos, count, iomode, lo, lseg,
					 PNFS_UPDATE_LAYOUT_RETURN);
		goto out_put_layout_hdr;
	}

	lseg = pnfs_find_lseg(lo, &arg, strict_iomode);
	if (lseg) {
		trace_pnfs_update_layout(ino, pos, count, iomode, lo, lseg,
				PNFS_UPDATE_LAYOUT_FOUND_CACHED);
		goto out_unlock;
	}

	/*
	 * Choose a stateid for the LAYOUTGET. If we don't have a layout
	 * stateid, or it has been invalidated, then we must use the open
	 * stateid.
	 */
	if (test_bit(NFS_LAYOUT_INVALID_STID, &lo->plh_flags)) {
		int status;

		/*
		 * The first layoutget for the file. Need to serialize per
		 * RFC 5661 Errata 3208.
		 */
		if (test_and_set_bit(NFS_LAYOUT_FIRST_LAYOUTGET,
				     &lo->plh_flags)) {
			spin_unlock(&ino->i_lock);
			lseg = ERR_PTR(wait_on_bit(&lo->plh_flags,
						NFS_LAYOUT_FIRST_LAYOUTGET,
						TASK_KILLABLE));
			if (IS_ERR(lseg))
				goto out_put_layout_hdr;
			pnfs_put_layout_hdr(lo);
			dprintk("%s retrying\n", __func__);
			goto lookup_again;
		}

		spin_unlock(&ino->i_lock);
		first = true;
		status = nfs4_select_rw_stateid(ctx->state,
					iomode == IOMODE_RW ? FMODE_WRITE : FMODE_READ,
					NULL, &stateid, NULL);
		if (status != 0) {
			lseg = ERR_PTR(status);
			trace_pnfs_update_layout(ino, pos, count,
					iomode, lo, lseg,
					PNFS_UPDATE_LAYOUT_INVALID_OPEN);
			nfs4_schedule_stateid_recovery(server, ctx->state);
			pnfs_clear_first_layoutget(lo);
			pnfs_put_layout_hdr(lo);
			goto lookup_again;
		}
		spin_lock(&ino->i_lock);
	} else {
		nfs4_stateid_copy(&stateid, &lo->plh_stateid);
	}

	if (pnfs_layoutgets_blocked(lo)) {
		trace_pnfs_update_layout(ino, pos, count, iomode, lo, lseg,
				PNFS_UPDATE_LAYOUT_BLOCKED);
		goto out_unlock;
	}
	nfs_layoutget_begin(lo);
	spin_unlock(&ino->i_lock);

	_add_to_server_list(lo, server);

	pg_offset = arg.offset & ~PAGE_MASK;
	if (pg_offset) {
		arg.offset -= pg_offset;
		arg.length += pg_offset;
	}
	if (arg.length != NFS4_MAX_UINT64)
		arg.length = PAGE_ALIGN(arg.length);

	lgp = pnfs_alloc_init_layoutget_args(ino, ctx, &stateid, &arg, gfp_flags);
	if (!lgp) {
		lseg = ERR_PTR(-ENOMEM);
		trace_pnfs_update_layout(ino, pos, count, iomode, lo, NULL,
					 PNFS_UPDATE_LAYOUT_NOMEM);
		nfs_layoutget_end(lo);
		goto out_put_layout_hdr;
	}

	lseg = nfs4_proc_layoutget(lgp, &timeout);
	trace_pnfs_update_layout(ino, pos, count, iomode, lo, lseg,
				 PNFS_UPDATE_LAYOUT_SEND_LAYOUTGET);
	nfs_layoutget_end(lo);
	if (IS_ERR(lseg)) {
		switch(PTR_ERR(lseg)) {
		case -EBUSY:
			if (time_after(jiffies, giveup))
				lseg = NULL;
			break;
		case -ERECALLCONFLICT:
		case -EAGAIN:
			break;
		default:
			if (!nfs_error_is_fatal(PTR_ERR(lseg))) {
				pnfs_layout_clear_fail_bit(lo, pnfs_iomode_to_fail_bit(iomode));
				lseg = NULL;
			}
			goto out_put_layout_hdr;
		}
		if (lseg) {
			if (first)
				pnfs_clear_first_layoutget(lo);
			trace_pnfs_update_layout(ino, pos, count,
				iomode, lo, lseg, PNFS_UPDATE_LAYOUT_RETRY);
			pnfs_put_layout_hdr(lo);
			goto lookup_again;
		}
	} else {
		pnfs_layout_clear_fail_bit(lo, pnfs_iomode_to_fail_bit(iomode));
	}

out_put_layout_hdr:
	if (first)
		pnfs_clear_first_layoutget(lo);
	trace_pnfs_update_layout(ino, pos, count, iomode, lo, lseg,
				 PNFS_UPDATE_LAYOUT_EXIT);
	pnfs_put_layout_hdr(lo);
out:
	dprintk("%s: inode %s/%llu pNFS layout segment %s for "
			"(%s, offset: %llu, length: %llu)\n",
			__func__, ino->i_sb->s_id,
			(unsigned long long)NFS_FILEID(ino),
			IS_ERR_OR_NULL(lseg) ? "not found" : "found",
			iomode==IOMODE_RW ?  "read/write" : "read-only",
			(unsigned long long)pos,
			(unsigned long long)count);
	return lseg;
out_unlock:
	spin_unlock(&ino->i_lock);
	goto out_put_layout_hdr;
}
EXPORT_SYMBOL_GPL(pnfs_update_layout);

static bool
pnfs_sanity_check_layout_range(struct pnfs_layout_range *range)
{
	switch (range->iomode) {
	case IOMODE_READ:
	case IOMODE_RW:
		break;
	default:
		return false;
	}
	if (range->offset == NFS4_MAX_UINT64)
		return false;
	if (range->length == 0)
		return false;
	if (range->length != NFS4_MAX_UINT64 &&
	    range->length > NFS4_MAX_UINT64 - range->offset)
		return false;
	return true;
}

static struct pnfs_layout_hdr *
_pnfs_grab_empty_layout(struct inode *ino, struct nfs_open_context *ctx)
{
	struct pnfs_layout_hdr *lo;

	spin_lock(&ino->i_lock);
	lo = pnfs_find_alloc_layout(ino, ctx, GFP_KERNEL);
	if (!lo)
		goto out_unlock;
	if (!test_bit(NFS_LAYOUT_INVALID_STID, &lo->plh_flags))
		goto out_unlock;
	if (test_bit(NFS_LAYOUT_RETURN, &lo->plh_flags))
		goto out_unlock;
	if (pnfs_layoutgets_blocked(lo))
		goto out_unlock;
	if (test_and_set_bit(NFS_LAYOUT_FIRST_LAYOUTGET, &lo->plh_flags))
		goto out_unlock;
	nfs_layoutget_begin(lo);
	spin_unlock(&ino->i_lock);
	_add_to_server_list(lo, NFS_SERVER(ino));
	return lo;

out_unlock:
	spin_unlock(&ino->i_lock);
	pnfs_put_layout_hdr(lo);
	return NULL;
}

static void _lgopen_prepare_attached(struct nfs4_opendata *data,
				     struct nfs_open_context *ctx)
{
	struct inode *ino = data->dentry->d_inode;
	struct pnfs_layout_range rng = {
		.iomode = (data->o_arg.fmode & FMODE_WRITE) ?
			  IOMODE_RW: IOMODE_READ,
		.offset = 0,
		.length = NFS4_MAX_UINT64,
	};
	struct nfs4_layoutget *lgp;
	struct pnfs_layout_hdr *lo;

	/* Heuristic: don't send layoutget if we have cached data */
	if (rng.iomode == IOMODE_READ &&
	   (i_size_read(ino) == 0 || ino->i_mapping->nrpages != 0))
		return;

	lo = _pnfs_grab_empty_layout(ino, ctx);
	if (!lo)
		return;
	lgp = pnfs_alloc_init_layoutget_args(ino, ctx, &current_stateid,
					     &rng, GFP_KERNEL);
	if (!lgp) {
		pnfs_clear_first_layoutget(lo);
		nfs_layoutget_end(lo);
		pnfs_put_layout_hdr(lo);
		return;
	}
	data->lgp = lgp;
	data->o_arg.lg_args = &lgp->args;
	data->o_res.lg_res = &lgp->res;
}

static void _lgopen_prepare_floating(struct nfs4_opendata *data,
				     struct nfs_open_context *ctx)
{
	struct pnfs_layout_range rng = {
		.iomode = (data->o_arg.fmode & FMODE_WRITE) ?
			  IOMODE_RW: IOMODE_READ,
		.offset = 0,
		.length = NFS4_MAX_UINT64,
	};
	struct nfs4_layoutget *lgp;

	lgp = pnfs_alloc_init_layoutget_args(NULL, ctx, &current_stateid,
					     &rng, GFP_KERNEL);
	if (!lgp)
		return;
	data->lgp = lgp;
	data->o_arg.lg_args = &lgp->args;
	data->o_res.lg_res = &lgp->res;
}

void pnfs_lgopen_prepare(struct nfs4_opendata *data,
			 struct nfs_open_context *ctx)
{
	struct nfs_server *server = NFS_SERVER(data->dir->d_inode);

	if (!(pnfs_enabled_sb(server) &&
	      server->pnfs_curr_ld->flags & PNFS_LAYOUTGET_ON_OPEN))
		return;
	/* Could check on max_ops, but currently hardcoded high enough */
	if (!nfs_server_capable(data->dir->d_inode, NFS_CAP_LGOPEN))
		return;
	if (data->state)
		_lgopen_prepare_attached(data, ctx);
	else
		_lgopen_prepare_floating(data, ctx);
}

void pnfs_parse_lgopen(struct inode *ino, struct nfs4_layoutget *lgp,
		       struct nfs_open_context *ctx)
{
	struct pnfs_layout_hdr *lo;
	struct pnfs_layout_segment *lseg;
	struct nfs_server *srv = NFS_SERVER(ino);
	u32 iomode;

	if (!lgp)
		return;
	dprintk("%s: entered with status %i\n", __func__, lgp->res.status);
	if (lgp->res.status) {
		switch (lgp->res.status) {
		default:
			break;
		/*
		 * Halt lgopen attempts if the server doesn't recognise
		 * the "current stateid" value, the layout type, or the
		 * layoutget operation as being valid.
		 * Also if it complains about too many ops in the compound
		 * or of the request/reply being too big.
		 */
		case -NFS4ERR_BAD_STATEID:
		case -NFS4ERR_NOTSUPP:
		case -NFS4ERR_REP_TOO_BIG:
		case -NFS4ERR_REP_TOO_BIG_TO_CACHE:
		case -NFS4ERR_REQ_TOO_BIG:
		case -NFS4ERR_TOO_MANY_OPS:
		case -NFS4ERR_UNKNOWN_LAYOUTTYPE:
			srv->caps &= ~NFS_CAP_LGOPEN;
		}
		return;
	}
	if (!lgp->args.inode) {
		lo = _pnfs_grab_empty_layout(ino, ctx);
		if (!lo)
			return;
		lgp->args.inode = ino;
	} else
		lo = NFS_I(lgp->args.inode)->layout;

	lseg = pnfs_layout_process(lgp);
	if (!IS_ERR(lseg)) {
		iomode = lgp->args.range.iomode;
		pnfs_layout_clear_fail_bit(lo, pnfs_iomode_to_fail_bit(iomode));
		pnfs_put_lseg(lseg);
	}
}

void nfs4_lgopen_release(struct nfs4_layoutget *lgp)
{
	if (lgp != NULL) {
		struct inode *inode = lgp->args.inode;
		if (inode) {
			struct pnfs_layout_hdr *lo = NFS_I(inode)->layout;
			pnfs_clear_first_layoutget(lo);
			nfs_layoutget_end(lo);
		}
		pnfs_layoutget_free(lgp);
	}
}

struct pnfs_layout_segment *
pnfs_layout_process(struct nfs4_layoutget *lgp)
{
	struct pnfs_layout_hdr *lo = NFS_I(lgp->args.inode)->layout;
	struct nfs4_layoutget_res *res = &lgp->res;
	struct pnfs_layout_segment *lseg;
	struct inode *ino = lo->plh_inode;
	LIST_HEAD(free_me);

	if (!pnfs_sanity_check_layout_range(&res->range))
		return ERR_PTR(-EINVAL);

	/* Inject layout blob into I/O device driver */
	lseg = NFS_SERVER(ino)->pnfs_curr_ld->alloc_lseg(lo, res, lgp->gfp_flags);
	if (IS_ERR_OR_NULL(lseg)) {
		if (!lseg)
			lseg = ERR_PTR(-ENOMEM);

		dprintk("%s: Could not allocate layout: error %ld\n",
		       __func__, PTR_ERR(lseg));
		return lseg;
	}

	pnfs_init_lseg(lo, lseg, &res->range, &res->stateid);

	spin_lock(&ino->i_lock);
	if (pnfs_layoutgets_blocked(lo)) {
		dprintk("%s forget reply due to state\n", __func__);
		goto out_forget;
	}

	if (!pnfs_layout_is_valid(lo) && !pnfs_is_first_layoutget(lo))
		goto out_forget;

	if (nfs4_stateid_match_other(&lo->plh_stateid, &res->stateid)) {
		/* existing state ID, make sure the sequence number matches. */
		if (pnfs_layout_stateid_blocked(lo, &res->stateid)) {
			if (!pnfs_layout_is_valid(lo))
				lo->plh_barrier = 0;
			dprintk("%s forget reply due to sequence\n", __func__);
			goto out_forget;
		}
		pnfs_set_layout_stateid(lo, &res->stateid, lgp->cred, false);
	} else if (pnfs_layout_is_valid(lo)) {
		/*
		 * We got an entirely new state ID.  Mark all segments for the
		 * inode invalid, and retry the layoutget
		 */
		struct pnfs_layout_range range = {
			.iomode = IOMODE_ANY,
			.length = NFS4_MAX_UINT64,
		};
		pnfs_set_plh_return_info(lo, IOMODE_ANY, 0);
		pnfs_mark_matching_lsegs_return(lo, &lo->plh_return_segs,
						&range, 0);
		goto out_forget;
	} else {
		/* We have a completely new layout */
		pnfs_set_layout_stateid(lo, &res->stateid, lgp->cred, true);
	}

	pnfs_get_lseg(lseg);
	pnfs_layout_insert_lseg(lo, lseg, &free_me);


	if (res->return_on_close)
		set_bit(NFS_LSEG_ROC, &lseg->pls_flags);

	spin_unlock(&ino->i_lock);
	pnfs_free_lseg_list(&free_me);
	return lseg;

out_forget:
	spin_unlock(&ino->i_lock);
	lseg->pls_layout = lo;
	NFS_SERVER(ino)->pnfs_curr_ld->free_lseg(lseg);
	pnfs_free_lseg_list(&free_me);
	return ERR_PTR(-EAGAIN);
}

/**
 * pnfs_mark_matching_lsegs_return - Free or return matching layout segments
 * @lo: pointer to layout header
 * @tmp_list: list header to be used with pnfs_free_lseg_list()
 * @return_range: describe layout segment ranges to be returned
 * @seq: stateid seqid to match
 *
 * This function is mainly intended for use by layoutrecall. It attempts
 * to free the layout segment immediately, or else to mark it for return
 * as soon as its reference count drops to zero.
 *
 * Returns
 * - 0: a layoutreturn needs to be scheduled.
 * - EBUSY: there are layout segment that are still in use.
 * - ENOENT: there are no layout segments that need to be returned.
 */
int
pnfs_mark_matching_lsegs_return(struct pnfs_layout_hdr *lo,
				struct list_head *tmp_list,
				const struct pnfs_layout_range *return_range,
				u32 seq)
{
	struct pnfs_layout_segment *lseg, *next;
	int remaining = 0;

	dprintk("%s:Begin lo %p\n", __func__, lo);

	assert_spin_locked(&lo->plh_inode->i_lock);

	if (test_bit(NFS_LAYOUT_RETURN_REQUESTED, &lo->plh_flags))
		tmp_list = &lo->plh_return_segs;

	list_for_each_entry_safe(lseg, next, &lo->plh_segs, pls_list)
		if (pnfs_match_lseg_recall(lseg, return_range, seq)) {
			dprintk("%s: marking lseg %p iomode %d "
				"offset %llu length %llu\n", __func__,
				lseg, lseg->pls_range.iomode,
				lseg->pls_range.offset,
				lseg->pls_range.length);
			if (test_bit(NFS_LSEG_LAYOUTRETURN, &lseg->pls_flags))
				tmp_list = &lo->plh_return_segs;
			if (mark_lseg_invalid(lseg, tmp_list))
				continue;
			remaining++;
			set_bit(NFS_LSEG_LAYOUTRETURN, &lseg->pls_flags);
		}

	if (remaining) {
		pnfs_set_plh_return_info(lo, return_range->iomode, seq);
		return -EBUSY;
	}

	if (!list_empty(&lo->plh_return_segs)) {
		pnfs_set_plh_return_info(lo, return_range->iomode, seq);
		return 0;
	}

	return -ENOENT;
}

static void
pnfs_mark_layout_for_return(struct inode *inode,
			    const struct pnfs_layout_range *range)
{
	struct pnfs_layout_hdr *lo;
	bool return_now = false;

	spin_lock(&inode->i_lock);
	lo = NFS_I(inode)->layout;
	if (!pnfs_layout_is_valid(lo)) {
		spin_unlock(&inode->i_lock);
		return;
	}
	pnfs_set_plh_return_info(lo, range->iomode, 0);
	/*
	 * mark all matching lsegs so that we are sure to have no live
	 * segments at hand when sending layoutreturn. See pnfs_put_lseg()
	 * for how it works.
	 */
	if (pnfs_mark_matching_lsegs_return(lo, &lo->plh_return_segs, range, 0) != -EBUSY) {
		const struct cred *cred;
		nfs4_stateid stateid;
		enum pnfs_iomode iomode;

		return_now = pnfs_prepare_layoutreturn(lo, &stateid, &cred, &iomode);
		spin_unlock(&inode->i_lock);
		if (return_now)
			pnfs_send_layoutreturn(lo, &stateid, &cred, iomode, false);
	} else {
		spin_unlock(&inode->i_lock);
		nfs_commit_inode(inode, 0);
	}
}

void pnfs_error_mark_layout_for_return(struct inode *inode,
				       struct pnfs_layout_segment *lseg)
{
	struct pnfs_layout_range range = {
		.iomode = lseg->pls_range.iomode,
		.offset = 0,
		.length = NFS4_MAX_UINT64,
	};

	pnfs_mark_layout_for_return(inode, &range);
}
EXPORT_SYMBOL_GPL(pnfs_error_mark_layout_for_return);

static bool
pnfs_layout_can_be_returned(struct pnfs_layout_hdr *lo)
{
	return pnfs_layout_is_valid(lo) &&
		!test_bit(NFS_LAYOUT_INODE_FREEING, &lo->plh_flags) &&
		!test_bit(NFS_LAYOUT_RETURN, &lo->plh_flags);
}

static struct pnfs_layout_segment *
pnfs_find_first_lseg(struct pnfs_layout_hdr *lo,
		     const struct pnfs_layout_range *range,
		     enum pnfs_iomode iomode)
{
	struct pnfs_layout_segment *lseg;

	list_for_each_entry(lseg, &lo->plh_segs, pls_list) {
		if (!test_bit(NFS_LSEG_VALID, &lseg->pls_flags))
			continue;
		if (test_bit(NFS_LSEG_LAYOUTRETURN, &lseg->pls_flags))
			continue;
		if (lseg->pls_range.iomode != iomode && iomode != IOMODE_ANY)
			continue;
		if (pnfs_lseg_range_intersecting(&lseg->pls_range, range))
			return lseg;
	}
	return NULL;
}

/* Find open file states whose mode matches that of the range */
static bool
pnfs_should_return_unused_layout(struct pnfs_layout_hdr *lo,
				 const struct pnfs_layout_range *range)
{
	struct list_head *head;
	struct nfs_open_context *ctx;
	fmode_t mode = 0;

	if (!pnfs_layout_can_be_returned(lo) ||
	    !pnfs_find_first_lseg(lo, range, range->iomode))
		return false;

	head = &NFS_I(lo->plh_inode)->open_files;
	list_for_each_entry_rcu(ctx, head, list) {
		if (ctx->state)
			mode |= ctx->state->state & (FMODE_READ|FMODE_WRITE);
	}

	switch (range->iomode) {
	default:
		break;
	case IOMODE_READ:
		mode &= ~FMODE_WRITE;
		break;
	case IOMODE_RW:
		if (pnfs_find_first_lseg(lo, range, IOMODE_READ))
			mode &= ~FMODE_READ;
	}
	return mode == 0;
}

static int
pnfs_layout_return_unused_byserver(struct nfs_server *server, void *data)
{
	const struct pnfs_layout_range *range = data;
	struct pnfs_layout_hdr *lo;
	struct inode *inode;
restart:
	rcu_read_lock();
	list_for_each_entry_rcu(lo, &server->layouts, plh_layouts) {
		if (!pnfs_layout_can_be_returned(lo) ||
		    test_bit(NFS_LAYOUT_RETURN_REQUESTED, &lo->plh_flags))
			continue;
		inode = lo->plh_inode;
		spin_lock(&inode->i_lock);
		if (!pnfs_should_return_unused_layout(lo, range)) {
			spin_unlock(&inode->i_lock);
			continue;
		}
		spin_unlock(&inode->i_lock);
		inode = pnfs_grab_inode_layout_hdr(lo);
		if (!inode)
			continue;
		rcu_read_unlock();
		pnfs_mark_layout_for_return(inode, range);
		iput(inode);
		cond_resched();
		goto restart;
	}
	rcu_read_unlock();
	return 0;
}

void
pnfs_layout_return_unused_byclid(struct nfs_client *clp,
				 enum pnfs_iomode iomode)
{
	struct pnfs_layout_range range = {
		.iomode = iomode,
		.offset = 0,
		.length = NFS4_MAX_UINT64,
	};

	nfs_client_for_each_server(clp, pnfs_layout_return_unused_byserver,
			&range);
}

void
pnfs_generic_pg_check_layout(struct nfs_pageio_descriptor *pgio)
{
	if (pgio->pg_lseg == NULL ||
	    test_bit(NFS_LSEG_VALID, &pgio->pg_lseg->pls_flags))
		return;
	pnfs_put_lseg(pgio->pg_lseg);
	pgio->pg_lseg = NULL;
}
EXPORT_SYMBOL_GPL(pnfs_generic_pg_check_layout);

/*
 * Check for any intersection between the request and the pgio->pg_lseg,
 * and if none, put this pgio->pg_lseg away.
 */
void
pnfs_generic_pg_check_range(struct nfs_pageio_descriptor *pgio, struct nfs_page *req)
{
	if (pgio->pg_lseg && !pnfs_lseg_request_intersecting(pgio->pg_lseg, req)) {
		pnfs_put_lseg(pgio->pg_lseg);
		pgio->pg_lseg = NULL;
	}
}
EXPORT_SYMBOL_GPL(pnfs_generic_pg_check_range);

void
pnfs_generic_pg_init_read(struct nfs_pageio_descriptor *pgio, struct nfs_page *req)
{
	u64 rd_size = req->wb_bytes;

	pnfs_generic_pg_check_layout(pgio);
	pnfs_generic_pg_check_range(pgio, req);
	if (pgio->pg_lseg == NULL) {
		if (pgio->pg_dreq == NULL)
			rd_size = i_size_read(pgio->pg_inode) - req_offset(req);
		else
			rd_size = nfs_dreq_bytes_left(pgio->pg_dreq);

		pgio->pg_lseg = pnfs_update_layout(pgio->pg_inode,
						   nfs_req_openctx(req),
						   req_offset(req),
						   rd_size,
						   IOMODE_READ,
						   false,
						   GFP_KERNEL);
		if (IS_ERR(pgio->pg_lseg)) {
			pgio->pg_error = PTR_ERR(pgio->pg_lseg);
			pgio->pg_lseg = NULL;
			return;
		}
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
	pnfs_generic_pg_check_layout(pgio);
	pnfs_generic_pg_check_range(pgio, req);
	if (pgio->pg_lseg == NULL) {
		pgio->pg_lseg = pnfs_update_layout(pgio->pg_inode,
						   nfs_req_openctx(req),
						   req_offset(req),
						   wb_size,
						   IOMODE_RW,
						   false,
						   GFP_KERNEL);
		if (IS_ERR(pgio->pg_lseg)) {
			pgio->pg_error = PTR_ERR(pgio->pg_lseg);
			pgio->pg_lseg = NULL;
			return;
		}
	}
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
		seg_end = pnfs_end_offset(pgio->pg_lseg->pls_range.offset,
				     pgio->pg_lseg->pls_range.length);
		req_start = req_offset(req);

		/* start of request is past the last byte of this segment */
		if (req_start >= seg_end)
			return 0;

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
	set_bit(NFS_CONTEXT_RESEND_WRITES, &hdr->args.context->flags);
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
	if (likely(!hdr->pnfs_error)) {
		pnfs_set_layoutcommit(hdr->inode, hdr->lseg,
				hdr->mds_offset + hdr->res.count);
		hdr->mds_ops->rpc_call_done(&hdr->task, hdr);
	}
	trace_nfs4_pnfs_write(hdr, hdr->pnfs_error);
	if (unlikely(hdr->pnfs_error))
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
	hdr->completion_ops->completion(hdr);
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
	switch (trypnfs) {
	case PNFS_NOT_ATTEMPTED:
		pnfs_write_through_mds(desc, hdr);
	case PNFS_ATTEMPTED:
		break;
	case PNFS_TRY_AGAIN:
		/* cleanup hdr and prepare to redo pnfs */
		if (!test_and_set_bit(NFS_IOHDR_REDO, &hdr->flags)) {
			struct nfs_pgio_mirror *mirror = nfs_pgio_current_mirror(desc);
			list_splice_init(&hdr->pages, &mirror->pg_list);
			mirror->pg_recoalesce = 1;
		}
		hdr->mds_ops->rpc_release(hdr);
	}
}

static void pnfs_writehdr_free(struct nfs_pgio_header *hdr)
{
	pnfs_put_lseg(hdr->lseg);
	nfs_pgio_header_free(hdr);
}

int
pnfs_generic_pg_writepages(struct nfs_pageio_descriptor *desc)
{
	struct nfs_pgio_header *hdr;
	int ret;

	hdr = nfs_pgio_header_alloc(desc->pg_rw_ops);
	if (!hdr) {
		desc->pg_error = -ENOMEM;
		return desc->pg_error;
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
	if (likely(!hdr->pnfs_error))
		hdr->mds_ops->rpc_call_done(&hdr->task, hdr);
	trace_nfs4_pnfs_read(hdr, hdr->pnfs_error);
	if (unlikely(hdr->pnfs_error))
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
	hdr->completion_ops->completion(hdr);
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
void pnfs_read_resend_pnfs(struct nfs_pgio_header *hdr,
			   unsigned int mirror_idx)
{
	struct nfs_pageio_descriptor pgio;

	if (!test_and_set_bit(NFS_IOHDR_REDO, &hdr->flags)) {
		/* Prevent deadlocks with layoutreturn! */
		pnfs_put_lseg(hdr->lseg);
		hdr->lseg = NULL;

		nfs_pageio_init_read(&pgio, hdr->inode, false,
					hdr->completion_ops);
		pgio.pg_mirror_idx = mirror_idx;
		hdr->task.tk_status = nfs_pageio_resend(&pgio, hdr);
	}
}
EXPORT_SYMBOL_GPL(pnfs_read_resend_pnfs);

static void
pnfs_do_read(struct nfs_pageio_descriptor *desc, struct nfs_pgio_header *hdr)
{
	const struct rpc_call_ops *call_ops = desc->pg_rpc_callops;
	struct pnfs_layout_segment *lseg = desc->pg_lseg;
	enum pnfs_try_status trypnfs;

	trypnfs = pnfs_try_to_read_data(hdr, call_ops, lseg);
	switch (trypnfs) {
	case PNFS_NOT_ATTEMPTED:
		pnfs_read_through_mds(desc, hdr);
	case PNFS_ATTEMPTED:
		break;
	case PNFS_TRY_AGAIN:
		/* cleanup hdr and prepare to redo pnfs */
		if (!test_and_set_bit(NFS_IOHDR_REDO, &hdr->flags)) {
			struct nfs_pgio_mirror *mirror = nfs_pgio_current_mirror(desc);
			list_splice_init(&hdr->pages, &mirror->pg_list);
			mirror->pg_recoalesce = 1;
		}
		hdr->mds_ops->rpc_release(hdr);
	}
}

static void pnfs_readhdr_free(struct nfs_pgio_header *hdr)
{
	pnfs_put_lseg(hdr->lseg);
	nfs_pgio_header_free(hdr);
}

int
pnfs_generic_pg_readpages(struct nfs_pageio_descriptor *desc)
{
	struct nfs_pgio_header *hdr;
	int ret;

	hdr = nfs_pgio_header_alloc(desc->pg_rw_ops);
	if (!hdr) {
		desc->pg_error = -ENOMEM;
		return desc->pg_error;
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
pnfs_set_layoutcommit(struct inode *inode, struct pnfs_layout_segment *lseg,
		loff_t end_pos)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	bool mark_as_dirty = false;

	spin_lock(&inode->i_lock);
	if (!test_and_set_bit(NFS_INO_LAYOUTCOMMIT, &nfsi->flags)) {
		nfsi->layout->plh_lwb = end_pos;
		mark_as_dirty = true;
		dprintk("%s: Set layoutcommit for inode %lu ",
			__func__, inode->i_ino);
	} else if (end_pos > nfsi->layout->plh_lwb)
		nfsi->layout->plh_lwb = end_pos;
	if (!test_and_set_bit(NFS_LSEG_LAYOUTCOMMIT, &lseg->pls_flags)) {
		/* references matched in nfs4_layoutcommit_release */
		pnfs_get_lseg(lseg);
	}
	spin_unlock(&inode->i_lock);
	dprintk("%s: lseg %p end_pos %llu\n",
		__func__, lseg, nfsi->layout->plh_lwb);

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

	nfs4_stateid_copy(&data->args.stateid, &nfsi->layout->plh_stateid);
	data->cred = get_cred(nfsi->layout->plh_lc_cred);
	spin_unlock(&inode->i_lock);

	data->args.inode = inode;
	nfs_fattr_init(&data->fattr);
	data->args.bitmask = NFS_SERVER(inode)->cache_consistency_bitmask;
	data->res.fattr = &data->fattr;
	if (end_pos != 0)
		data->args.lastbytewritten = end_pos - 1;
	else
		data->args.lastbytewritten = U64_MAX;
	data->res.server = NFS_SERVER(inode);

	if (ld->prepare_layoutcommit) {
		status = ld->prepare_layoutcommit(&data->args);
		if (status) {
			put_cred(data->cred);
			spin_lock(&inode->i_lock);
			set_bit(NFS_INO_LAYOUTCOMMIT, &nfsi->flags);
			if (end_pos > nfsi->layout->plh_lwb)
				nfsi->layout->plh_lwb = end_pos;
			goto out_unlock;
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

int
pnfs_generic_sync(struct inode *inode, bool datasync)
{
	return pnfs_layoutcommit_inode(inode, true);
}
EXPORT_SYMBOL_GPL(pnfs_generic_sync);

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

#if IS_ENABLED(CONFIG_NFS_V4_2)
int
pnfs_report_layoutstat(struct inode *inode, gfp_t gfp_flags)
{
	struct pnfs_layoutdriver_type *ld = NFS_SERVER(inode)->pnfs_curr_ld;
	struct nfs_server *server = NFS_SERVER(inode);
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs42_layoutstat_data *data;
	struct pnfs_layout_hdr *hdr;
	int status = 0;

	if (!pnfs_enabled_sb(server) || !ld->prepare_layoutstats)
		goto out;

	if (!nfs_server_capable(inode, NFS_CAP_LAYOUTSTATS))
		goto out;

	if (test_and_set_bit(NFS_INO_LAYOUTSTATS, &nfsi->flags))
		goto out;

	spin_lock(&inode->i_lock);
	if (!NFS_I(inode)->layout) {
		spin_unlock(&inode->i_lock);
		goto out_clear_layoutstats;
	}
	hdr = NFS_I(inode)->layout;
	pnfs_get_layout_hdr(hdr);
	spin_unlock(&inode->i_lock);

	data = kzalloc(sizeof(*data), gfp_flags);
	if (!data) {
		status = -ENOMEM;
		goto out_put;
	}

	data->args.fh = NFS_FH(inode);
	data->args.inode = inode;
	status = ld->prepare_layoutstats(&data->args);
	if (status)
		goto out_free;

	status = nfs42_proc_layoutstats_generic(NFS_SERVER(inode), data);

out:
	dprintk("%s returns %d\n", __func__, status);
	return status;

out_free:
	kfree(data);
out_put:
	pnfs_put_layout_hdr(hdr);
out_clear_layoutstats:
	smp_mb__before_atomic();
	clear_bit(NFS_INO_LAYOUTSTATS, &nfsi->flags);
	smp_mb__after_atomic();
	goto out;
}
EXPORT_SYMBOL_GPL(pnfs_report_layoutstat);
#endif

unsigned int layoutstats_timer;
module_param(layoutstats_timer, uint, 0644);
EXPORT_SYMBOL_GPL(layoutstats_timer);
