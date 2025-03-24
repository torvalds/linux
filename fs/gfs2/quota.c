// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2007 Red Hat, Inc.  All rights reserved.
 */

/*
 * Quota change tags are associated with each transaction that allocates or
 * deallocates space.  Those changes are accumulated locally to each node (in a
 * per-node file) and then are periodically synced to the quota file.  This
 * avoids the bottleneck of constantly touching the quota file, but introduces
 * fuzziness in the current usage value of IDs that are being used on different
 * nodes in the cluster simultaneously.  So, it is possible for a user on
 * multiple nodes to overrun their quota, but that overrun is controlable.
 * Since quota tags are part of transactions, there is no need for a quota check
 * program to be run on node crashes or anything like that.
 *
 * There are couple of knobs that let the administrator manage the quota
 * fuzziness.  "quota_quantum" sets the maximum time a quota change can be
 * sitting on one node before being synced to the quota file.  (The default is
 * 60 seconds.)  Another knob, "quota_scale" controls how quickly the frequency
 * of quota file syncs increases as the user moves closer to their limit.  The
 * more frequent the syncs, the more accurate the quota enforcement, but that
 * means that there is more contention between the nodes for the quota file.
 * The default value is one.  This sets the maximum theoretical quota overrun
 * (with infinite node with infinite bandwidth) to twice the user's limit.  (In
 * practice, the maximum overrun you see should be much less.)  A "quota_scale"
 * number greater than one makes quota syncs more frequent and reduces the
 * maximum overrun.  Numbers less than one (but greater than zero) make quota
 * syncs less frequent.
 *
 * GFS quotas also use per-ID Lock Value Blocks (LVBs) to cache the contents of
 * the quota file, so it is not being constantly read.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/sort.h>
#include <linux/fs.h>
#include <linux/bio.h>
#include <linux/gfs2_ondisk.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/quota.h>
#include <linux/dqblk_xfs.h>
#include <linux/lockref.h>
#include <linux/list_lru.h>
#include <linux/rcupdate.h>
#include <linux/rculist_bl.h>
#include <linux/bit_spinlock.h>
#include <linux/jhash.h>
#include <linux/vmalloc.h>

#include "gfs2.h"
#include "incore.h"
#include "bmap.h"
#include "glock.h"
#include "glops.h"
#include "log.h"
#include "meta_io.h"
#include "quota.h"
#include "rgrp.h"
#include "super.h"
#include "trans.h"
#include "inode.h"
#include "util.h"

#define GFS2_QD_HASH_SHIFT      12
#define GFS2_QD_HASH_SIZE       BIT(GFS2_QD_HASH_SHIFT)
#define GFS2_QD_HASH_MASK       (GFS2_QD_HASH_SIZE - 1)

/* Lock order: qd_lock -> bucket lock -> qd->lockref.lock -> lru lock */
/*                     -> sd_bitmap_lock                              */
static DEFINE_SPINLOCK(qd_lock);
struct list_lru gfs2_qd_lru;

static struct hlist_bl_head qd_hash_table[GFS2_QD_HASH_SIZE];

static unsigned int gfs2_qd_hash(const struct gfs2_sbd *sdp,
				 const struct kqid qid)
{
	unsigned int h;

	h = jhash(&sdp, sizeof(struct gfs2_sbd *), 0);
	h = jhash(&qid, sizeof(struct kqid), h);

	return h & GFS2_QD_HASH_MASK;
}

static inline void spin_lock_bucket(unsigned int hash)
{
        hlist_bl_lock(&qd_hash_table[hash]);
}

static inline void spin_unlock_bucket(unsigned int hash)
{
        hlist_bl_unlock(&qd_hash_table[hash]);
}

static void gfs2_qd_dealloc(struct rcu_head *rcu)
{
	struct gfs2_quota_data *qd = container_of(rcu, struct gfs2_quota_data, qd_rcu);
	struct gfs2_sbd *sdp = qd->qd_sbd;

	kmem_cache_free(gfs2_quotad_cachep, qd);
	if (atomic_dec_and_test(&sdp->sd_quota_count))
		wake_up(&sdp->sd_kill_wait);
}

static void gfs2_qd_dispose(struct gfs2_quota_data *qd)
{
	struct gfs2_sbd *sdp = qd->qd_sbd;

	spin_lock(&qd_lock);
	list_del(&qd->qd_list);
	spin_unlock(&qd_lock);

	spin_lock_bucket(qd->qd_hash);
	hlist_bl_del_rcu(&qd->qd_hlist);
	spin_unlock_bucket(qd->qd_hash);

	if (!gfs2_withdrawing_or_withdrawn(sdp)) {
		gfs2_assert_warn(sdp, !qd->qd_change);
		gfs2_assert_warn(sdp, !qd->qd_slot_ref);
		gfs2_assert_warn(sdp, !qd->qd_bh_count);
	}

	gfs2_glock_put(qd->qd_gl);
	call_rcu(&qd->qd_rcu, gfs2_qd_dealloc);
}

static void gfs2_qd_list_dispose(struct list_head *list)
{
	struct gfs2_quota_data *qd;

	while (!list_empty(list)) {
		qd = list_first_entry(list, struct gfs2_quota_data, qd_lru);
		list_del(&qd->qd_lru);

		gfs2_qd_dispose(qd);
	}
}


static enum lru_status gfs2_qd_isolate(struct list_head *item,
		struct list_lru_one *lru, void *arg)
{
	struct list_head *dispose = arg;
	struct gfs2_quota_data *qd =
		list_entry(item, struct gfs2_quota_data, qd_lru);
	enum lru_status status;

	if (!spin_trylock(&qd->qd_lockref.lock))
		return LRU_SKIP;

	status = LRU_SKIP;
	if (qd->qd_lockref.count == 0) {
		lockref_mark_dead(&qd->qd_lockref);
		list_lru_isolate_move(lru, &qd->qd_lru, dispose);
		status = LRU_REMOVED;
	}

	spin_unlock(&qd->qd_lockref.lock);
	return status;
}

static unsigned long gfs2_qd_shrink_scan(struct shrinker *shrink,
					 struct shrink_control *sc)
{
	LIST_HEAD(dispose);
	unsigned long freed;

	if (!(sc->gfp_mask & __GFP_FS))
		return SHRINK_STOP;

	freed = list_lru_shrink_walk(&gfs2_qd_lru, sc,
				     gfs2_qd_isolate, &dispose);

	gfs2_qd_list_dispose(&dispose);

	return freed;
}

static unsigned long gfs2_qd_shrink_count(struct shrinker *shrink,
					  struct shrink_control *sc)
{
	return vfs_pressure_ratio(list_lru_shrink_count(&gfs2_qd_lru, sc));
}

static struct shrinker *gfs2_qd_shrinker;

int __init gfs2_qd_shrinker_init(void)
{
	gfs2_qd_shrinker = shrinker_alloc(SHRINKER_NUMA_AWARE, "gfs2-qd");
	if (!gfs2_qd_shrinker)
		return -ENOMEM;

	gfs2_qd_shrinker->count_objects = gfs2_qd_shrink_count;
	gfs2_qd_shrinker->scan_objects = gfs2_qd_shrink_scan;

	shrinker_register(gfs2_qd_shrinker);

	return 0;
}

void gfs2_qd_shrinker_exit(void)
{
	shrinker_free(gfs2_qd_shrinker);
}

static u64 qd2index(struct gfs2_quota_data *qd)
{
	struct kqid qid = qd->qd_id;
	return (2 * (u64)from_kqid(&init_user_ns, qid)) +
		((qid.type == USRQUOTA) ? 0 : 1);
}

static u64 qd2offset(struct gfs2_quota_data *qd)
{
	return qd2index(qd) * sizeof(struct gfs2_quota);
}

static struct gfs2_quota_data *qd_alloc(unsigned hash, struct gfs2_sbd *sdp, struct kqid qid)
{
	struct gfs2_quota_data *qd;
	int error;

	qd = kmem_cache_zalloc(gfs2_quotad_cachep, GFP_NOFS);
	if (!qd)
		return NULL;

	qd->qd_sbd = sdp;
	lockref_init(&qd->qd_lockref);
	qd->qd_id = qid;
	qd->qd_slot = -1;
	INIT_LIST_HEAD(&qd->qd_lru);
	qd->qd_hash = hash;

	error = gfs2_glock_get(sdp, qd2index(qd),
			      &gfs2_quota_glops, CREATE, &qd->qd_gl);
	if (error)
		goto fail;

	return qd;

fail:
	kmem_cache_free(gfs2_quotad_cachep, qd);
	return NULL;
}

static struct gfs2_quota_data *gfs2_qd_search_bucket(unsigned int hash,
						     const struct gfs2_sbd *sdp,
						     struct kqid qid)
{
	struct gfs2_quota_data *qd;
	struct hlist_bl_node *h;

	hlist_bl_for_each_entry_rcu(qd, h, &qd_hash_table[hash], qd_hlist) {
		if (!qid_eq(qd->qd_id, qid))
			continue;
		if (qd->qd_sbd != sdp)
			continue;
		if (lockref_get_not_dead(&qd->qd_lockref)) {
			list_lru_del_obj(&gfs2_qd_lru, &qd->qd_lru);
			return qd;
		}
	}

	return NULL;
}


static int qd_get(struct gfs2_sbd *sdp, struct kqid qid,
		  struct gfs2_quota_data **qdp)
{
	struct gfs2_quota_data *qd, *new_qd;
	unsigned int hash = gfs2_qd_hash(sdp, qid);

	rcu_read_lock();
	*qdp = qd = gfs2_qd_search_bucket(hash, sdp, qid);
	rcu_read_unlock();

	if (qd)
		return 0;

	new_qd = qd_alloc(hash, sdp, qid);
	if (!new_qd)
		return -ENOMEM;

	spin_lock(&qd_lock);
	spin_lock_bucket(hash);
	*qdp = qd = gfs2_qd_search_bucket(hash, sdp, qid);
	if (qd == NULL) {
		*qdp = new_qd;
		list_add(&new_qd->qd_list, &sdp->sd_quota_list);
		hlist_bl_add_head_rcu(&new_qd->qd_hlist, &qd_hash_table[hash]);
		atomic_inc(&sdp->sd_quota_count);
	}
	spin_unlock_bucket(hash);
	spin_unlock(&qd_lock);

	if (qd) {
		gfs2_glock_put(new_qd->qd_gl);
		kmem_cache_free(gfs2_quotad_cachep, new_qd);
	}

	return 0;
}


static void __qd_hold(struct gfs2_quota_data *qd)
{
	struct gfs2_sbd *sdp = qd->qd_sbd;
	gfs2_assert(sdp, qd->qd_lockref.count > 0);
	qd->qd_lockref.count++;
}

static void qd_put(struct gfs2_quota_data *qd)
{
	struct gfs2_sbd *sdp;

	if (lockref_put_or_lock(&qd->qd_lockref))
		return;

	BUG_ON(__lockref_is_dead(&qd->qd_lockref));
	sdp = qd->qd_sbd;
	if (unlikely(!test_bit(SDF_JOURNAL_LIVE, &sdp->sd_flags))) {
		lockref_mark_dead(&qd->qd_lockref);
		spin_unlock(&qd->qd_lockref.lock);

		gfs2_qd_dispose(qd);
		return;
	}

	qd->qd_lockref.count = 0;
	list_lru_add_obj(&gfs2_qd_lru, &qd->qd_lru);
	spin_unlock(&qd->qd_lockref.lock);
}

static int slot_get(struct gfs2_quota_data *qd)
{
	struct gfs2_sbd *sdp = qd->qd_sbd;
	unsigned int bit;
	int error = 0;

	spin_lock(&sdp->sd_bitmap_lock);
	if (qd->qd_slot_ref == 0) {
		bit = find_first_zero_bit(sdp->sd_quota_bitmap,
					  sdp->sd_quota_slots);
		if (bit >= sdp->sd_quota_slots) {
			error = -ENOSPC;
			goto out;
		}
		set_bit(bit, sdp->sd_quota_bitmap);
		qd->qd_slot = bit;
	}
	qd->qd_slot_ref++;
out:
	spin_unlock(&sdp->sd_bitmap_lock);
	return error;
}

static void slot_hold(struct gfs2_quota_data *qd)
{
	struct gfs2_sbd *sdp = qd->qd_sbd;

	spin_lock(&sdp->sd_bitmap_lock);
	gfs2_assert(sdp, qd->qd_slot_ref);
	qd->qd_slot_ref++;
	spin_unlock(&sdp->sd_bitmap_lock);
}

static void slot_put(struct gfs2_quota_data *qd)
{
	struct gfs2_sbd *sdp = qd->qd_sbd;

	spin_lock(&sdp->sd_bitmap_lock);
	gfs2_assert(sdp, qd->qd_slot_ref);
	if (!--qd->qd_slot_ref) {
		BUG_ON(!test_and_clear_bit(qd->qd_slot, sdp->sd_quota_bitmap));
		qd->qd_slot = -1;
	}
	spin_unlock(&sdp->sd_bitmap_lock);
}

static int bh_get(struct gfs2_quota_data *qd)
{
	struct gfs2_sbd *sdp = qd->qd_sbd;
	struct inode *inode = sdp->sd_qc_inode;
	struct gfs2_inode *ip = GFS2_I(inode);
	unsigned int block, offset;
	struct buffer_head *bh = NULL;
	struct iomap iomap = { };
	int error;

	spin_lock(&qd->qd_lockref.lock);
	if (qd->qd_bh_count) {
		qd->qd_bh_count++;
		spin_unlock(&qd->qd_lockref.lock);
		return 0;
	}
	spin_unlock(&qd->qd_lockref.lock);

	block = qd->qd_slot / sdp->sd_qc_per_block;
	offset = qd->qd_slot % sdp->sd_qc_per_block;

	error = gfs2_iomap_get(inode,
			       (loff_t)block << inode->i_blkbits,
			       i_blocksize(inode), &iomap);
	if (error)
		return error;
	error = -ENOENT;
	if (iomap.type != IOMAP_MAPPED)
		return error;

	error = gfs2_meta_read(ip->i_gl, iomap.addr >> inode->i_blkbits,
			       DIO_WAIT, 0, &bh);
	if (error)
		return error;
	error = -EIO;
	if (gfs2_metatype_check(sdp, bh, GFS2_METATYPE_QC))
		goto out;

	spin_lock(&qd->qd_lockref.lock);
	if (qd->qd_bh == NULL) {
		qd->qd_bh = bh;
		qd->qd_bh_qc = (struct gfs2_quota_change *)
			(bh->b_data + sizeof(struct gfs2_meta_header) +
			 offset * sizeof(struct gfs2_quota_change));
		bh = NULL;
	}
	qd->qd_bh_count++;
	spin_unlock(&qd->qd_lockref.lock);
	error = 0;

out:
	brelse(bh);
	return error;
}

static void bh_put(struct gfs2_quota_data *qd)
{
	struct gfs2_sbd *sdp = qd->qd_sbd;
	struct buffer_head *bh = NULL;

	spin_lock(&qd->qd_lockref.lock);
	gfs2_assert(sdp, qd->qd_bh_count);
	if (!--qd->qd_bh_count) {
		bh = qd->qd_bh;
		qd->qd_bh = NULL;
		qd->qd_bh_qc = NULL;
	}
	spin_unlock(&qd->qd_lockref.lock);
	brelse(bh);
}

static bool qd_grab_sync(struct gfs2_sbd *sdp, struct gfs2_quota_data *qd,
			 u64 sync_gen)
{
	bool ret = false;

	spin_lock(&qd->qd_lockref.lock);
	if (test_bit(QDF_LOCKED, &qd->qd_flags) ||
	    !test_bit(QDF_CHANGE, &qd->qd_flags) ||
	    qd->qd_sync_gen >= sync_gen)
		goto out;

	if (__lockref_is_dead(&qd->qd_lockref))
		goto out;
	qd->qd_lockref.count++;

	list_move_tail(&qd->qd_list, &sdp->sd_quota_list);
	set_bit(QDF_LOCKED, &qd->qd_flags);
	qd->qd_change_sync = qd->qd_change;
	slot_hold(qd);
	ret = true;

out:
	spin_unlock(&qd->qd_lockref.lock);
	return ret;
}

static void qd_ungrab_sync(struct gfs2_quota_data *qd)
{
	clear_bit(QDF_LOCKED, &qd->qd_flags);
	slot_put(qd);
	qd_put(qd);
}

static void qdsb_put(struct gfs2_quota_data *qd)
{
	bh_put(qd);
	slot_put(qd);
	qd_put(qd);
}

static void qd_unlock(struct gfs2_quota_data *qd)
{
	spin_lock(&qd->qd_lockref.lock);
	gfs2_assert_warn(qd->qd_sbd, test_bit(QDF_LOCKED, &qd->qd_flags));
	clear_bit(QDF_LOCKED, &qd->qd_flags);
	spin_unlock(&qd->qd_lockref.lock);
	qdsb_put(qd);
}

static int qdsb_get(struct gfs2_sbd *sdp, struct kqid qid,
		    struct gfs2_quota_data **qdp)
{
	int error;

	error = qd_get(sdp, qid, qdp);
	if (error)
		return error;

	error = slot_get(*qdp);
	if (error)
		goto fail;

	error = bh_get(*qdp);
	if (error)
		goto fail_slot;

	return 0;

fail_slot:
	slot_put(*qdp);
fail:
	qd_put(*qdp);
	return error;
}

/**
 * gfs2_qa_get - make sure we have a quota allocations data structure,
 *               if necessary
 * @ip: the inode for this reservation
 */
int gfs2_qa_get(struct gfs2_inode *ip)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	struct inode *inode = &ip->i_inode;

	if (sdp->sd_args.ar_quota == GFS2_QUOTA_OFF)
		return 0;

	spin_lock(&inode->i_lock);
	if (ip->i_qadata == NULL) {
		struct gfs2_qadata *tmp;

		spin_unlock(&inode->i_lock);
		tmp = kmem_cache_zalloc(gfs2_qadata_cachep, GFP_NOFS);
		if (!tmp)
			return -ENOMEM;

		spin_lock(&inode->i_lock);
		if (ip->i_qadata == NULL)
			ip->i_qadata = tmp;
		else
			kmem_cache_free(gfs2_qadata_cachep, tmp);
	}
	ip->i_qadata->qa_ref++;
	spin_unlock(&inode->i_lock);
	return 0;
}

void gfs2_qa_put(struct gfs2_inode *ip)
{
	struct inode *inode = &ip->i_inode;

	spin_lock(&inode->i_lock);
	if (ip->i_qadata && --ip->i_qadata->qa_ref == 0) {
		kmem_cache_free(gfs2_qadata_cachep, ip->i_qadata);
		ip->i_qadata = NULL;
	}
	spin_unlock(&inode->i_lock);
}

int gfs2_quota_hold(struct gfs2_inode *ip, kuid_t uid, kgid_t gid)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	struct gfs2_quota_data **qd;
	int error;

	if (sdp->sd_args.ar_quota == GFS2_QUOTA_OFF)
		return 0;

	error = gfs2_qa_get(ip);
	if (error)
		return error;

	qd = ip->i_qadata->qa_qd;

	if (gfs2_assert_warn(sdp, !ip->i_qadata->qa_qd_num) ||
	    gfs2_assert_warn(sdp, !test_bit(GIF_QD_LOCKED, &ip->i_flags))) {
		error = -EIO;
		gfs2_qa_put(ip);
		goto out;
	}

	error = qdsb_get(sdp, make_kqid_uid(ip->i_inode.i_uid), qd);
	if (error)
		goto out_unhold;
	ip->i_qadata->qa_qd_num++;
	qd++;

	error = qdsb_get(sdp, make_kqid_gid(ip->i_inode.i_gid), qd);
	if (error)
		goto out_unhold;
	ip->i_qadata->qa_qd_num++;
	qd++;

	if (!uid_eq(uid, NO_UID_QUOTA_CHANGE) &&
	    !uid_eq(uid, ip->i_inode.i_uid)) {
		error = qdsb_get(sdp, make_kqid_uid(uid), qd);
		if (error)
			goto out_unhold;
		ip->i_qadata->qa_qd_num++;
		qd++;
	}

	if (!gid_eq(gid, NO_GID_QUOTA_CHANGE) &&
	    !gid_eq(gid, ip->i_inode.i_gid)) {
		error = qdsb_get(sdp, make_kqid_gid(gid), qd);
		if (error)
			goto out_unhold;
		ip->i_qadata->qa_qd_num++;
		qd++;
	}

out_unhold:
	if (error)
		gfs2_quota_unhold(ip);
out:
	return error;
}

void gfs2_quota_unhold(struct gfs2_inode *ip)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	u32 x;

	if (ip->i_qadata == NULL)
		return;

	gfs2_assert_warn(sdp, !test_bit(GIF_QD_LOCKED, &ip->i_flags));

	for (x = 0; x < ip->i_qadata->qa_qd_num; x++) {
		qdsb_put(ip->i_qadata->qa_qd[x]);
		ip->i_qadata->qa_qd[x] = NULL;
	}
	ip->i_qadata->qa_qd_num = 0;
	gfs2_qa_put(ip);
}

static int sort_qd(const void *a, const void *b)
{
	const struct gfs2_quota_data *qd_a = *(const struct gfs2_quota_data **)a;
	const struct gfs2_quota_data *qd_b = *(const struct gfs2_quota_data **)b;

	if (qid_lt(qd_a->qd_id, qd_b->qd_id))
		return -1;
	if (qid_lt(qd_b->qd_id, qd_a->qd_id))
		return 1;
	return 0;
}

static void do_qc(struct gfs2_quota_data *qd, s64 change)
{
	struct gfs2_sbd *sdp = qd->qd_sbd;
	struct gfs2_inode *ip = GFS2_I(sdp->sd_qc_inode);
	struct gfs2_quota_change *qc = qd->qd_bh_qc;
	bool needs_put = false;
	s64 x;

	gfs2_trans_add_meta(ip->i_gl, qd->qd_bh);

	/*
	 * The QDF_CHANGE flag indicates that the slot in the quota change file
	 * is used.  Here, we use the value of qc->qc_change when the slot is
	 * used, and we assume a value of 0 otherwise.
	 */

	spin_lock(&qd->qd_lockref.lock);

	x = 0;
	if (test_bit(QDF_CHANGE, &qd->qd_flags))
		x = be64_to_cpu(qc->qc_change);
	x += change;
	qd->qd_change += change;

	if (!x && test_bit(QDF_CHANGE, &qd->qd_flags)) {
		/* The slot in the quota change file becomes unused. */
		clear_bit(QDF_CHANGE, &qd->qd_flags);
		qc->qc_flags = 0;
		qc->qc_id = 0;
		needs_put = true;
	} else if (x && !test_bit(QDF_CHANGE, &qd->qd_flags)) {
		/* The slot in the quota change file becomes used. */
		set_bit(QDF_CHANGE, &qd->qd_flags);
		__qd_hold(qd);
		slot_hold(qd);

		qc->qc_flags = 0;
		if (qd->qd_id.type == USRQUOTA)
			qc->qc_flags = cpu_to_be32(GFS2_QCF_USER);
		qc->qc_id = cpu_to_be32(from_kqid(&init_user_ns, qd->qd_id));
	}
	qc->qc_change = cpu_to_be64(x);

	spin_unlock(&qd->qd_lockref.lock);

	if (needs_put) {
		slot_put(qd);
		qd_put(qd);
	}
	if (change < 0) /* Reset quiet flag if we freed some blocks */
		clear_bit(QDF_QMSG_QUIET, &qd->qd_flags);
}

static int gfs2_write_buf_to_page(struct gfs2_sbd *sdp, unsigned long index,
				  unsigned off, void *buf, unsigned bytes)
{
	struct gfs2_inode *ip = GFS2_I(sdp->sd_quota_inode);
	struct inode *inode = &ip->i_inode;
	struct address_space *mapping = inode->i_mapping;
	struct folio *folio;
	struct buffer_head *bh;
	u64 blk;
	unsigned bsize = sdp->sd_sb.sb_bsize, bnum = 0, boff = 0;
	unsigned to_write = bytes, pg_off = off;

	blk = index << (PAGE_SHIFT - sdp->sd_sb.sb_bsize_shift);
	boff = off % bsize;

	folio = filemap_grab_folio(mapping, index);
	if (IS_ERR(folio))
		return PTR_ERR(folio);
	bh = folio_buffers(folio);
	if (!bh)
		bh = create_empty_buffers(folio, bsize, 0);

	for (;;) {
		/* Find the beginning block within the folio */
		if (pg_off >= ((bnum * bsize) + bsize)) {
			bh = bh->b_this_page;
			bnum++;
			blk++;
			continue;
		}
		if (!buffer_mapped(bh)) {
			gfs2_block_map(inode, blk, bh, 1);
			if (!buffer_mapped(bh))
				goto unlock_out;
			/* If it's a newly allocated disk block, zero it */
			if (buffer_new(bh))
				folio_zero_range(folio, bnum * bsize,
						bh->b_size);
		}
		if (folio_test_uptodate(folio))
			set_buffer_uptodate(bh);
		if (bh_read(bh, REQ_META | REQ_PRIO) < 0)
			goto unlock_out;
		gfs2_trans_add_data(ip->i_gl, bh);

		/* If we need to write to the next block as well */
		if (to_write > (bsize - boff)) {
			pg_off += (bsize - boff);
			to_write -= (bsize - boff);
			boff = pg_off % bsize;
			continue;
		}
		break;
	}

	/* Write to the folio, now that we have setup the buffer(s) */
	memcpy_to_folio(folio, off, buf, bytes);
	flush_dcache_folio(folio);
	folio_unlock(folio);
	folio_put(folio);

	return 0;

unlock_out:
	folio_unlock(folio);
	folio_put(folio);
	return -EIO;
}

static int gfs2_write_disk_quota(struct gfs2_sbd *sdp, struct gfs2_quota *qp,
				 loff_t loc)
{
	unsigned long pg_beg;
	unsigned pg_off, nbytes, overflow = 0;
	int error;
	void *ptr;

	nbytes = sizeof(struct gfs2_quota);

	pg_beg = loc >> PAGE_SHIFT;
	pg_off = offset_in_page(loc);

	/* If the quota straddles a page boundary, split the write in two */
	if ((pg_off + nbytes) > PAGE_SIZE)
		overflow = (pg_off + nbytes) - PAGE_SIZE;

	ptr = qp;
	error = gfs2_write_buf_to_page(sdp, pg_beg, pg_off, ptr,
				       nbytes - overflow);
	/* If there's an overflow, write the remaining bytes to the next page */
	if (!error && overflow)
		error = gfs2_write_buf_to_page(sdp, pg_beg + 1, 0,
					       ptr + nbytes - overflow,
					       overflow);
	return error;
}

/**
 * gfs2_adjust_quota - adjust record of current block usage
 * @sdp: The superblock
 * @loc: Offset of the entry in the quota file
 * @change: The amount of usage change to record
 * @qd: The quota data
 * @fdq: The updated limits to record
 *
 * This function was mostly borrowed from gfs2_block_truncate_page which was
 * in turn mostly borrowed from ext3
 *
 * Returns: 0 or -ve on error
 */

static int gfs2_adjust_quota(struct gfs2_sbd *sdp, loff_t loc,
			     s64 change, struct gfs2_quota_data *qd,
			     struct qc_dqblk *fdq)
{
	struct gfs2_inode *ip = GFS2_I(sdp->sd_quota_inode);
	struct inode *inode = &ip->i_inode;
	struct gfs2_quota q;
	int err;
	u64 size;

	if (gfs2_is_stuffed(ip)) {
		err = gfs2_unstuff_dinode(ip);
		if (err)
			return err;
	}

	memset(&q, 0, sizeof(struct gfs2_quota));
	err = gfs2_internal_read(ip, (char *)&q, &loc, sizeof(q));
	if (err < 0)
		return err;

	loc -= sizeof(q); /* gfs2_internal_read would've advanced the loc ptr */
	be64_add_cpu(&q.qu_value, change);
	if (((s64)be64_to_cpu(q.qu_value)) < 0)
		q.qu_value = 0; /* Never go negative on quota usage */
	spin_lock(&qd->qd_lockref.lock);
	qd->qd_qb.qb_value = q.qu_value;
	if (fdq) {
		if (fdq->d_fieldmask & QC_SPC_SOFT) {
			q.qu_warn = cpu_to_be64(fdq->d_spc_softlimit >> sdp->sd_sb.sb_bsize_shift);
			qd->qd_qb.qb_warn = q.qu_warn;
		}
		if (fdq->d_fieldmask & QC_SPC_HARD) {
			q.qu_limit = cpu_to_be64(fdq->d_spc_hardlimit >> sdp->sd_sb.sb_bsize_shift);
			qd->qd_qb.qb_limit = q.qu_limit;
		}
		if (fdq->d_fieldmask & QC_SPACE) {
			q.qu_value = cpu_to_be64(fdq->d_space >> sdp->sd_sb.sb_bsize_shift);
			qd->qd_qb.qb_value = q.qu_value;
		}
	}
	spin_unlock(&qd->qd_lockref.lock);

	err = gfs2_write_disk_quota(sdp, &q, loc);
	if (!err) {
		size = loc + sizeof(struct gfs2_quota);
		if (size > inode->i_size)
			i_size_write(inode, size);
		inode_set_mtime_to_ts(inode, inode_set_ctime_current(inode));
		mark_inode_dirty(inode);
		set_bit(QDF_REFRESH, &qd->qd_flags);
	}

	return err;
}

static int do_sync(unsigned int num_qd, struct gfs2_quota_data **qda,
		   u64 sync_gen)
{
	struct gfs2_sbd *sdp = (*qda)->qd_sbd;
	struct gfs2_inode *ip = GFS2_I(sdp->sd_quota_inode);
	struct gfs2_alloc_parms ap = {};
	unsigned int data_blocks, ind_blocks;
	struct gfs2_holder *ghs, i_gh;
	unsigned int qx, x;
	struct gfs2_quota_data *qd;
	unsigned reserved;
	loff_t offset;
	unsigned int nalloc = 0, blocks;
	int error;

	gfs2_write_calc_reserv(ip, sizeof(struct gfs2_quota),
			      &data_blocks, &ind_blocks);

	ghs = kmalloc_array(num_qd, sizeof(struct gfs2_holder), GFP_NOFS);
	if (!ghs)
		return -ENOMEM;

	sort(qda, num_qd, sizeof(struct gfs2_quota_data *), sort_qd, NULL);
	inode_lock(&ip->i_inode);
	for (qx = 0; qx < num_qd; qx++) {
		error = gfs2_glock_nq_init(qda[qx]->qd_gl, LM_ST_EXCLUSIVE,
					   GL_NOCACHE, &ghs[qx]);
		if (error)
			goto out_dq;
	}

	error = gfs2_glock_nq_init(ip->i_gl, LM_ST_EXCLUSIVE, 0, &i_gh);
	if (error)
		goto out_dq;

	for (x = 0; x < num_qd; x++) {
		offset = qd2offset(qda[x]);
		if (gfs2_write_alloc_required(ip, offset,
					      sizeof(struct gfs2_quota)))
			nalloc++;
	}

	/* 
	 * 1 blk for unstuffing inode if stuffed. We add this extra
	 * block to the reservation unconditionally. If the inode
	 * doesn't need unstuffing, the block will be released to the 
	 * rgrp since it won't be allocated during the transaction
	 */
	/* +3 in the end for unstuffing block, inode size update block
	 * and another block in case quota straddles page boundary and 
	 * two blocks need to be updated instead of 1 */
	blocks = num_qd * data_blocks + RES_DINODE + num_qd + 3;

	reserved = 1 + (nalloc * (data_blocks + ind_blocks));
	ap.target = reserved;
	error = gfs2_inplace_reserve(ip, &ap);
	if (error)
		goto out_alloc;

	if (nalloc)
		blocks += gfs2_rg_blocks(ip, reserved) + nalloc * ind_blocks + RES_STATFS;

	error = gfs2_trans_begin(sdp, blocks, 0);
	if (error)
		goto out_ipres;

	for (x = 0; x < num_qd; x++) {
		qd = qda[x];
		offset = qd2offset(qd);
		error = gfs2_adjust_quota(sdp, offset, qd->qd_change_sync, qd,
							NULL);
		if (error)
			goto out_end_trans;

		do_qc(qd, -qd->qd_change_sync);
		set_bit(QDF_REFRESH, &qd->qd_flags);
	}

out_end_trans:
	gfs2_trans_end(sdp);
out_ipres:
	gfs2_inplace_release(ip);
out_alloc:
	gfs2_glock_dq_uninit(&i_gh);
out_dq:
	while (qx--)
		gfs2_glock_dq_uninit(&ghs[qx]);
	inode_unlock(&ip->i_inode);
	kfree(ghs);
	gfs2_log_flush(ip->i_gl->gl_name.ln_sbd, ip->i_gl,
		       GFS2_LOG_HEAD_FLUSH_NORMAL | GFS2_LFC_DO_SYNC);
	if (!error) {
		for (x = 0; x < num_qd; x++) {
			qd = qda[x];
			spin_lock(&qd->qd_lockref.lock);
			if (qd->qd_sync_gen < sync_gen)
				qd->qd_sync_gen = sync_gen;
			spin_unlock(&qd->qd_lockref.lock);
		}
	}
	return error;
}

static int update_qd(struct gfs2_sbd *sdp, struct gfs2_quota_data *qd)
{
	struct gfs2_inode *ip = GFS2_I(sdp->sd_quota_inode);
	struct gfs2_quota q;
	struct gfs2_quota_lvb *qlvb;
	loff_t pos;
	int error;

	memset(&q, 0, sizeof(struct gfs2_quota));
	pos = qd2offset(qd);
	error = gfs2_internal_read(ip, (char *)&q, &pos, sizeof(q));
	if (error < 0)
		return error;

	qlvb = (struct gfs2_quota_lvb *)qd->qd_gl->gl_lksb.sb_lvbptr;
	qlvb->qb_magic = cpu_to_be32(GFS2_MAGIC);
	qlvb->__pad = 0;
	qlvb->qb_limit = q.qu_limit;
	qlvb->qb_warn = q.qu_warn;
	qlvb->qb_value = q.qu_value;
	spin_lock(&qd->qd_lockref.lock);
	qd->qd_qb = *qlvb;
	spin_unlock(&qd->qd_lockref.lock);

	return 0;
}

static int do_glock(struct gfs2_quota_data *qd, int force_refresh,
		    struct gfs2_holder *q_gh)
{
	struct gfs2_sbd *sdp = qd->qd_sbd;
	struct gfs2_inode *ip = GFS2_I(sdp->sd_quota_inode);
	struct gfs2_holder i_gh;
	int error;

	gfs2_assert_warn(sdp, sdp == qd->qd_gl->gl_name.ln_sbd);
restart:
	error = gfs2_glock_nq_init(qd->qd_gl, LM_ST_SHARED, 0, q_gh);
	if (error)
		return error;

	if (test_and_clear_bit(QDF_REFRESH, &qd->qd_flags))
		force_refresh = FORCE;

	spin_lock(&qd->qd_lockref.lock);
	qd->qd_qb = *(struct gfs2_quota_lvb *)qd->qd_gl->gl_lksb.sb_lvbptr;
	spin_unlock(&qd->qd_lockref.lock);

	if (force_refresh || qd->qd_qb.qb_magic != cpu_to_be32(GFS2_MAGIC)) {
		gfs2_glock_dq_uninit(q_gh);
		error = gfs2_glock_nq_init(qd->qd_gl, LM_ST_EXCLUSIVE,
					   GL_NOCACHE, q_gh);
		if (error)
			return error;

		error = gfs2_glock_nq_init(ip->i_gl, LM_ST_SHARED, 0, &i_gh);
		if (error)
			goto fail;

		error = update_qd(sdp, qd);
		if (error)
			goto fail_gunlock;

		gfs2_glock_dq_uninit(&i_gh);
		gfs2_glock_dq_uninit(q_gh);
		force_refresh = 0;
		goto restart;
	}

	return 0;

fail_gunlock:
	gfs2_glock_dq_uninit(&i_gh);
fail:
	gfs2_glock_dq_uninit(q_gh);
	return error;
}

int gfs2_quota_lock(struct gfs2_inode *ip, kuid_t uid, kgid_t gid)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	struct gfs2_quota_data *qd;
	u32 x;
	int error;

	if (sdp->sd_args.ar_quota == GFS2_QUOTA_OFF)
		return 0;

	error = gfs2_quota_hold(ip, uid, gid);
	if (error)
		return error;

	sort(ip->i_qadata->qa_qd, ip->i_qadata->qa_qd_num,
	     sizeof(struct gfs2_quota_data *), sort_qd, NULL);

	for (x = 0; x < ip->i_qadata->qa_qd_num; x++) {
		qd = ip->i_qadata->qa_qd[x];
		error = do_glock(qd, NO_FORCE, &ip->i_qadata->qa_qd_ghs[x]);
		if (error)
			break;
	}

	if (!error)
		set_bit(GIF_QD_LOCKED, &ip->i_flags);
	else {
		while (x--)
			gfs2_glock_dq_uninit(&ip->i_qadata->qa_qd_ghs[x]);
		gfs2_quota_unhold(ip);
	}

	return error;
}

static bool need_sync(struct gfs2_quota_data *qd)
{
	struct gfs2_sbd *sdp = qd->qd_sbd;
	struct gfs2_tune *gt = &sdp->sd_tune;
	s64 value, change, limit;
	unsigned int num, den;
	int ret = false;

	spin_lock(&qd->qd_lockref.lock);
	if (!qd->qd_qb.qb_limit)
		goto out;

	change = qd->qd_change;
	if (change <= 0)
		goto out;
	value = (s64)be64_to_cpu(qd->qd_qb.qb_value);
	limit = (s64)be64_to_cpu(qd->qd_qb.qb_limit);
	if (value >= limit)
		goto out;

	spin_lock(&gt->gt_spin);
	num = gt->gt_quota_scale_num;
	den = gt->gt_quota_scale_den;
	spin_unlock(&gt->gt_spin);

	change *= gfs2_jindex_size(sdp) * num;
	change = div_s64(change, den);
	if (value + change < limit)
		goto out;

	ret = true;
out:
	spin_unlock(&qd->qd_lockref.lock);
	return ret;
}

void gfs2_quota_unlock(struct gfs2_inode *ip)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	struct gfs2_quota_data *qda[2 * GFS2_MAXQUOTAS];
	unsigned int count = 0;
	u32 x;

	if (!test_and_clear_bit(GIF_QD_LOCKED, &ip->i_flags))
		return;

	for (x = 0; x < ip->i_qadata->qa_qd_num; x++) {
		struct gfs2_quota_data *qd;
		bool sync;
		int error;

		qd = ip->i_qadata->qa_qd[x];
		sync = need_sync(qd);

		gfs2_glock_dq_uninit(&ip->i_qadata->qa_qd_ghs[x]);
		if (!sync)
			continue;

		spin_lock(&qd_lock);
		sync = qd_grab_sync(sdp, qd, U64_MAX);
		spin_unlock(&qd_lock);

		if (!sync)
			continue;

		gfs2_assert_warn(sdp, qd->qd_change_sync);
		error = bh_get(qd);
		if (error) {
			qd_ungrab_sync(qd);
			continue;
		}

		qda[count++] = qd;
	}

	if (count) {
		u64 sync_gen = READ_ONCE(sdp->sd_quota_sync_gen);

		do_sync(count, qda, sync_gen);
		for (x = 0; x < count; x++)
			qd_unlock(qda[x]);
	}

	gfs2_quota_unhold(ip);
}

#define MAX_LINE 256

static void print_message(struct gfs2_quota_data *qd, char *type)
{
	struct gfs2_sbd *sdp = qd->qd_sbd;

	if (sdp->sd_args.ar_quota != GFS2_QUOTA_QUIET) {
		fs_info(sdp, "quota %s for %s %u\n",
			type,
			(qd->qd_id.type == USRQUOTA) ? "user" : "group",
			from_kqid(&init_user_ns, qd->qd_id));
	}
}

/**
 * gfs2_quota_check - check if allocating new blocks will exceed quota
 * @ip:  The inode for which this check is being performed
 * @uid: The uid to check against
 * @gid: The gid to check against
 * @ap:  The allocation parameters. ap->target contains the requested
 *       blocks. ap->min_target, if set, contains the minimum blks
 *       requested.
 *
 * Returns: 0 on success.
 *                  min_req = ap->min_target ? ap->min_target : ap->target;
 *                  quota must allow at least min_req blks for success and
 *                  ap->allowed is set to the number of blocks allowed
 *
 *          -EDQUOT otherwise, quota violation. ap->allowed is set to number
 *                  of blocks available.
 */
int gfs2_quota_check(struct gfs2_inode *ip, kuid_t uid, kgid_t gid,
		     struct gfs2_alloc_parms *ap)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	struct gfs2_quota_data *qd;
	s64 value, warn, limit;
	u32 x;
	int error = 0;

	ap->allowed = UINT_MAX; /* Assume we are permitted a whole lot */
	if (!test_bit(GIF_QD_LOCKED, &ip->i_flags))
		return 0;

	for (x = 0; x < ip->i_qadata->qa_qd_num; x++) {
		qd = ip->i_qadata->qa_qd[x];

		if (!(qid_eq(qd->qd_id, make_kqid_uid(uid)) ||
		      qid_eq(qd->qd_id, make_kqid_gid(gid))))
			continue;

		spin_lock(&qd->qd_lockref.lock);
		warn = (s64)be64_to_cpu(qd->qd_qb.qb_warn);
		limit = (s64)be64_to_cpu(qd->qd_qb.qb_limit);
		value = (s64)be64_to_cpu(qd->qd_qb.qb_value);
		value += qd->qd_change;
		spin_unlock(&qd->qd_lockref.lock);

		if (limit > 0 && (limit - value) < ap->allowed)
			ap->allowed = limit - value;
		/* If we can't meet the target */
		if (limit && limit < (value + (s64)ap->target)) {
			/* If no min_target specified or we don't meet
			 * min_target, return -EDQUOT */
			if (!ap->min_target || ap->min_target > ap->allowed) {
				if (!test_and_set_bit(QDF_QMSG_QUIET,
						      &qd->qd_flags)) {
					print_message(qd, "exceeded");
					quota_send_warning(qd->qd_id,
							   sdp->sd_vfs->s_dev,
							   QUOTA_NL_BHARDWARN);
				}
				error = -EDQUOT;
				break;
			}
		} else if (warn && warn < value &&
			   time_after_eq(jiffies, qd->qd_last_warn +
					 gfs2_tune_get(sdp, gt_quota_warn_period)
					 * HZ)) {
			quota_send_warning(qd->qd_id,
					   sdp->sd_vfs->s_dev, QUOTA_NL_BSOFTWARN);
			print_message(qd, "warning");
			error = 0;
			qd->qd_last_warn = jiffies;
		}
	}
	return error;
}

void gfs2_quota_change(struct gfs2_inode *ip, s64 change,
		       kuid_t uid, kgid_t gid)
{
	struct gfs2_quota_data *qd;
	u32 x;
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);

	if (sdp->sd_args.ar_quota == GFS2_QUOTA_OFF ||
	    gfs2_assert_warn(sdp, change))
		return;
	if (ip->i_diskflags & GFS2_DIF_SYSTEM)
		return;

	if (gfs2_assert_withdraw(sdp, ip->i_qadata &&
				 ip->i_qadata->qa_ref > 0))
		return;
	for (x = 0; x < ip->i_qadata->qa_qd_num; x++) {
		qd = ip->i_qadata->qa_qd[x];

		if (qid_eq(qd->qd_id, make_kqid_uid(uid)) ||
		    qid_eq(qd->qd_id, make_kqid_gid(gid))) {
			do_qc(qd, change);
		}
	}
}

int gfs2_quota_sync(struct super_block *sb, int type)
{
	struct gfs2_sbd *sdp = sb->s_fs_info;
	struct gfs2_quota_data **qda;
	unsigned int max_qd = PAGE_SIZE / sizeof(struct gfs2_holder);
	u64 sync_gen;
	int error = 0;

	if (sb_rdonly(sdp->sd_vfs))
		return 0;

	qda = kcalloc(max_qd, sizeof(struct gfs2_quota_data *), GFP_KERNEL);
	if (!qda)
		return -ENOMEM;

	mutex_lock(&sdp->sd_quota_sync_mutex);
	sync_gen = sdp->sd_quota_sync_gen + 1;

	do {
		struct gfs2_quota_data *iter;
		unsigned int num_qd = 0;
		unsigned int x;

		spin_lock(&qd_lock);
		list_for_each_entry(iter, &sdp->sd_quota_list, qd_list) {
			if (qd_grab_sync(sdp, iter, sync_gen)) {
				qda[num_qd++] = iter;
				if (num_qd == max_qd)
					break;
			}
		}
		spin_unlock(&qd_lock);

		if (!num_qd)
			break;

		for (x = 0; x < num_qd; x++) {
			error = bh_get(qda[x]);
			if (!error)
				continue;

			while (x < num_qd)
				qd_ungrab_sync(qda[--num_qd]);
			break;
		}

		if (!error) {
			WRITE_ONCE(sdp->sd_quota_sync_gen, sync_gen);
			error = do_sync(num_qd, qda, sync_gen);
		}

		for (x = 0; x < num_qd; x++)
			qd_unlock(qda[x]);
	} while (!error);

	mutex_unlock(&sdp->sd_quota_sync_mutex);
	kfree(qda);

	return error;
}

int gfs2_quota_refresh(struct gfs2_sbd *sdp, struct kqid qid)
{
	struct gfs2_quota_data *qd;
	struct gfs2_holder q_gh;
	int error;

	error = qd_get(sdp, qid, &qd);
	if (error)
		return error;

	error = do_glock(qd, FORCE, &q_gh);
	if (!error)
		gfs2_glock_dq_uninit(&q_gh);

	qd_put(qd);
	return error;
}

int gfs2_quota_init(struct gfs2_sbd *sdp)
{
	struct gfs2_inode *ip = GFS2_I(sdp->sd_qc_inode);
	u64 size = i_size_read(sdp->sd_qc_inode);
	unsigned int blocks = size >> sdp->sd_sb.sb_bsize_shift;
	unsigned int x, slot = 0;
	unsigned int found = 0;
	unsigned int hash;
	unsigned int bm_size;
	struct buffer_head *bh;
	u64 dblock;
	u32 extlen = 0;
	int error;

	if (gfs2_check_internal_file_size(sdp->sd_qc_inode, 1, 64 << 20))
		return -EIO;

	sdp->sd_quota_slots = blocks * sdp->sd_qc_per_block;
	bm_size = DIV_ROUND_UP(sdp->sd_quota_slots, 8 * sizeof(unsigned long));
	bm_size *= sizeof(unsigned long);
	error = -ENOMEM;
	sdp->sd_quota_bitmap = kzalloc(bm_size, GFP_NOFS | __GFP_NOWARN);
	if (sdp->sd_quota_bitmap == NULL)
		sdp->sd_quota_bitmap = __vmalloc(bm_size, GFP_NOFS |
						 __GFP_ZERO);
	if (!sdp->sd_quota_bitmap)
		return error;

	for (x = 0; x < blocks; x++) {
		struct gfs2_quota_change *qc;
		unsigned int y;

		if (!extlen) {
			extlen = 32;
			error = gfs2_get_extent(&ip->i_inode, x, &dblock, &extlen);
			if (error)
				goto fail;
		}
		error = -EIO;
		bh = gfs2_meta_ra(ip->i_gl, dblock, extlen);
		if (!bh)
			goto fail;
		if (gfs2_metatype_check(sdp, bh, GFS2_METATYPE_QC))
			goto fail_brelse;

		qc = (struct gfs2_quota_change *)(bh->b_data + sizeof(struct gfs2_meta_header));
		for (y = 0; y < sdp->sd_qc_per_block && slot < sdp->sd_quota_slots;
		     y++, slot++) {
			struct gfs2_quota_data *old_qd, *qd;
			s64 qc_change = be64_to_cpu(qc->qc_change);
			u32 qc_flags = be32_to_cpu(qc->qc_flags);
			enum quota_type qtype = (qc_flags & GFS2_QCF_USER) ?
						USRQUOTA : GRPQUOTA;
			struct kqid qc_id = make_kqid(&init_user_ns, qtype,
						      be32_to_cpu(qc->qc_id));
			qc++;
			if (!qc_change)
				continue;

			hash = gfs2_qd_hash(sdp, qc_id);
			qd = qd_alloc(hash, sdp, qc_id);
			if (qd == NULL)
				goto fail_brelse;

			qd->qd_lockref.count = 0;
			set_bit(QDF_CHANGE, &qd->qd_flags);
			qd->qd_change = qc_change;
			qd->qd_slot = slot;
			qd->qd_slot_ref = 1;

			spin_lock(&qd_lock);
			spin_lock_bucket(hash);
			old_qd = gfs2_qd_search_bucket(hash, sdp, qc_id);
			if (old_qd) {
				fs_err(sdp, "Corruption found in quota_change%u"
					    "file: duplicate identifier in "
					    "slot %u\n",
					    sdp->sd_jdesc->jd_jid, slot);

				spin_unlock_bucket(hash);
				spin_unlock(&qd_lock);
				qd_put(old_qd);

				gfs2_glock_put(qd->qd_gl);
				kmem_cache_free(gfs2_quotad_cachep, qd);

				/* zero out the duplicate slot */
				lock_buffer(bh);
				memset(qc, 0, sizeof(*qc));
				mark_buffer_dirty(bh);
				unlock_buffer(bh);

				continue;
			}
			BUG_ON(test_and_set_bit(slot, sdp->sd_quota_bitmap));
			list_add(&qd->qd_list, &sdp->sd_quota_list);
			atomic_inc(&sdp->sd_quota_count);
			hlist_bl_add_head_rcu(&qd->qd_hlist, &qd_hash_table[hash]);
			spin_unlock_bucket(hash);
			spin_unlock(&qd_lock);

			found++;
		}

		if (buffer_dirty(bh))
			sync_dirty_buffer(bh);
		brelse(bh);
		dblock++;
		extlen--;
	}

	if (found)
		fs_info(sdp, "found %u quota changes\n", found);

	return 0;

fail_brelse:
	if (buffer_dirty(bh))
		sync_dirty_buffer(bh);
	brelse(bh);
fail:
	gfs2_quota_cleanup(sdp);
	return error;
}

void gfs2_quota_cleanup(struct gfs2_sbd *sdp)
{
	struct gfs2_quota_data *qd;
	LIST_HEAD(dispose);
	int count;

	BUG_ON(!test_bit(SDF_NORECOVERY, &sdp->sd_flags) &&
		test_bit(SDF_JOURNAL_LIVE, &sdp->sd_flags));

	spin_lock(&qd_lock);
	list_for_each_entry(qd, &sdp->sd_quota_list, qd_list) {
		spin_lock(&qd->qd_lockref.lock);
		if (qd->qd_lockref.count != 0) {
			spin_unlock(&qd->qd_lockref.lock);
			continue;
		}
		lockref_mark_dead(&qd->qd_lockref);
		spin_unlock(&qd->qd_lockref.lock);

		list_lru_del_obj(&gfs2_qd_lru, &qd->qd_lru);
		list_add(&qd->qd_lru, &dispose);
	}
	spin_unlock(&qd_lock);

	gfs2_qd_list_dispose(&dispose);

	wait_event_timeout(sdp->sd_kill_wait,
		(count = atomic_read(&sdp->sd_quota_count)) == 0,
		HZ * 60);

	if (count != 0)
		fs_err(sdp, "%d left-over quota data objects\n", count);

	kvfree(sdp->sd_quota_bitmap);
	sdp->sd_quota_bitmap = NULL;
}

static void quotad_error(struct gfs2_sbd *sdp, const char *msg, int error)
{
	if (error == 0 || error == -EROFS)
		return;
	if (!gfs2_withdrawing_or_withdrawn(sdp)) {
		if (!cmpxchg(&sdp->sd_log_error, 0, error))
			fs_err(sdp, "gfs2_quotad: %s error %d\n", msg, error);
		wake_up(&sdp->sd_logd_waitq);
	}
}

static void quotad_check_timeo(struct gfs2_sbd *sdp, const char *msg,
			       int (*fxn)(struct super_block *sb, int type),
			       unsigned long t, unsigned long *timeo,
			       unsigned int *new_timeo)
{
	if (t >= *timeo) {
		int error = fxn(sdp->sd_vfs, 0);
		quotad_error(sdp, msg, error);
		*timeo = gfs2_tune_get_i(&sdp->sd_tune, new_timeo) * HZ;
	} else {
		*timeo -= t;
	}
}

void gfs2_wake_up_statfs(struct gfs2_sbd *sdp) {
	if (!sdp->sd_statfs_force_sync) {
		sdp->sd_statfs_force_sync = 1;
		wake_up(&sdp->sd_quota_wait);
	}
}


/**
 * gfs2_quotad - Write cached quota changes into the quota file
 * @data: Pointer to GFS2 superblock
 *
 */

int gfs2_quotad(void *data)
{
	struct gfs2_sbd *sdp = data;
	struct gfs2_tune *tune = &sdp->sd_tune;
	unsigned long statfs_timeo = 0;
	unsigned long quotad_timeo = 0;
	unsigned long t = 0;

	set_freezable();
	while (!kthread_should_stop()) {
		if (gfs2_withdrawing_or_withdrawn(sdp))
			break;

		/* Update the master statfs file */
		if (sdp->sd_statfs_force_sync) {
			int error = gfs2_statfs_sync(sdp->sd_vfs, 0);
			quotad_error(sdp, "statfs", error);
			statfs_timeo = gfs2_tune_get(sdp, gt_statfs_quantum) * HZ;
		}
		else
			quotad_check_timeo(sdp, "statfs", gfs2_statfs_sync, t,
				   	   &statfs_timeo,
					   &tune->gt_statfs_quantum);

		/* Update quota file */
		quotad_check_timeo(sdp, "sync", gfs2_quota_sync, t,
				   &quotad_timeo, &tune->gt_quota_quantum);

		t = min(quotad_timeo, statfs_timeo);

		t = wait_event_freezable_timeout(sdp->sd_quota_wait,
				sdp->sd_statfs_force_sync ||
				gfs2_withdrawing_or_withdrawn(sdp) ||
				kthread_should_stop(),
				t);

		if (sdp->sd_statfs_force_sync)
			t = 0;
	}

	return 0;
}

static int gfs2_quota_get_state(struct super_block *sb, struct qc_state *state)
{
	struct gfs2_sbd *sdp = sb->s_fs_info;

	memset(state, 0, sizeof(*state));

	switch (sdp->sd_args.ar_quota) {
	case GFS2_QUOTA_QUIET:
		fallthrough;
	case GFS2_QUOTA_ON:
		state->s_state[USRQUOTA].flags |= QCI_LIMITS_ENFORCED;
		state->s_state[GRPQUOTA].flags |= QCI_LIMITS_ENFORCED;
		fallthrough;
	case GFS2_QUOTA_ACCOUNT:
		state->s_state[USRQUOTA].flags |= QCI_ACCT_ENABLED |
						  QCI_SYSFILE;
		state->s_state[GRPQUOTA].flags |= QCI_ACCT_ENABLED |
						  QCI_SYSFILE;
		break;
	case GFS2_QUOTA_OFF:
		break;
	}
	if (sdp->sd_quota_inode) {
		state->s_state[USRQUOTA].ino =
					GFS2_I(sdp->sd_quota_inode)->i_no_addr;
		state->s_state[USRQUOTA].blocks = sdp->sd_quota_inode->i_blocks;
	}
	state->s_state[USRQUOTA].nextents = 1;	/* unsupported */
	state->s_state[GRPQUOTA] = state->s_state[USRQUOTA];
	state->s_incoredqs = list_lru_count(&gfs2_qd_lru);
	return 0;
}

static int gfs2_get_dqblk(struct super_block *sb, struct kqid qid,
			  struct qc_dqblk *fdq)
{
	struct gfs2_sbd *sdp = sb->s_fs_info;
	struct gfs2_quota_lvb *qlvb;
	struct gfs2_quota_data *qd;
	struct gfs2_holder q_gh;
	int error;

	memset(fdq, 0, sizeof(*fdq));

	if (sdp->sd_args.ar_quota == GFS2_QUOTA_OFF)
		return -ESRCH; /* Crazy XFS error code */

	if ((qid.type != USRQUOTA) &&
	    (qid.type != GRPQUOTA))
		return -EINVAL;

	error = qd_get(sdp, qid, &qd);
	if (error)
		return error;
	error = do_glock(qd, FORCE, &q_gh);
	if (error)
		goto out;

	qlvb = (struct gfs2_quota_lvb *)qd->qd_gl->gl_lksb.sb_lvbptr;
	fdq->d_spc_hardlimit = be64_to_cpu(qlvb->qb_limit) << sdp->sd_sb.sb_bsize_shift;
	fdq->d_spc_softlimit = be64_to_cpu(qlvb->qb_warn) << sdp->sd_sb.sb_bsize_shift;
	fdq->d_space = be64_to_cpu(qlvb->qb_value) << sdp->sd_sb.sb_bsize_shift;

	gfs2_glock_dq_uninit(&q_gh);
out:
	qd_put(qd);
	return error;
}

/* GFS2 only supports a subset of the XFS fields */
#define GFS2_FIELDMASK (QC_SPC_SOFT|QC_SPC_HARD|QC_SPACE)

static int gfs2_set_dqblk(struct super_block *sb, struct kqid qid,
			  struct qc_dqblk *fdq)
{
	struct gfs2_sbd *sdp = sb->s_fs_info;
	struct gfs2_inode *ip = GFS2_I(sdp->sd_quota_inode);
	struct gfs2_quota_data *qd;
	struct gfs2_holder q_gh, i_gh;
	unsigned int data_blocks, ind_blocks;
	unsigned int blocks = 0;
	int alloc_required;
	loff_t offset;
	int error;

	if (sdp->sd_args.ar_quota == GFS2_QUOTA_OFF)
		return -ESRCH; /* Crazy XFS error code */

	if ((qid.type != USRQUOTA) &&
	    (qid.type != GRPQUOTA))
		return -EINVAL;

	if (fdq->d_fieldmask & ~GFS2_FIELDMASK)
		return -EINVAL;

	error = qd_get(sdp, qid, &qd);
	if (error)
		return error;

	error = gfs2_qa_get(ip);
	if (error)
		goto out_put;

	inode_lock(&ip->i_inode);
	error = gfs2_glock_nq_init(qd->qd_gl, LM_ST_EXCLUSIVE, 0, &q_gh);
	if (error)
		goto out_unlockput;
	error = gfs2_glock_nq_init(ip->i_gl, LM_ST_EXCLUSIVE, 0, &i_gh);
	if (error)
		goto out_q;

	/* Check for existing entry, if none then alloc new blocks */
	error = update_qd(sdp, qd);
	if (error)
		goto out_i;

	/* If nothing has changed, this is a no-op */
	if ((fdq->d_fieldmask & QC_SPC_SOFT) &&
	    ((fdq->d_spc_softlimit >> sdp->sd_sb.sb_bsize_shift) == be64_to_cpu(qd->qd_qb.qb_warn)))
		fdq->d_fieldmask ^= QC_SPC_SOFT;

	if ((fdq->d_fieldmask & QC_SPC_HARD) &&
	    ((fdq->d_spc_hardlimit >> sdp->sd_sb.sb_bsize_shift) == be64_to_cpu(qd->qd_qb.qb_limit)))
		fdq->d_fieldmask ^= QC_SPC_HARD;

	if ((fdq->d_fieldmask & QC_SPACE) &&
	    ((fdq->d_space >> sdp->sd_sb.sb_bsize_shift) == be64_to_cpu(qd->qd_qb.qb_value)))
		fdq->d_fieldmask ^= QC_SPACE;

	if (fdq->d_fieldmask == 0)
		goto out_i;

	offset = qd2offset(qd);
	alloc_required = gfs2_write_alloc_required(ip, offset, sizeof(struct gfs2_quota));
	if (gfs2_is_stuffed(ip))
		alloc_required = 1;
	if (alloc_required) {
		struct gfs2_alloc_parms ap = {};
		gfs2_write_calc_reserv(ip, sizeof(struct gfs2_quota),
				       &data_blocks, &ind_blocks);
		blocks = 1 + data_blocks + ind_blocks;
		ap.target = blocks;
		error = gfs2_inplace_reserve(ip, &ap);
		if (error)
			goto out_i;
		blocks += gfs2_rg_blocks(ip, blocks);
	}

	/* Some quotas span block boundaries and can update two blocks,
	   adding an extra block to the transaction to handle such quotas */
	error = gfs2_trans_begin(sdp, blocks + RES_DINODE + 2, 0);
	if (error)
		goto out_release;

	/* Apply changes */
	error = gfs2_adjust_quota(sdp, offset, 0, qd, fdq);
	if (!error)
		clear_bit(QDF_QMSG_QUIET, &qd->qd_flags);

	gfs2_trans_end(sdp);
out_release:
	if (alloc_required)
		gfs2_inplace_release(ip);
out_i:
	gfs2_glock_dq_uninit(&i_gh);
out_q:
	gfs2_glock_dq_uninit(&q_gh);
out_unlockput:
	gfs2_qa_put(ip);
	inode_unlock(&ip->i_inode);
out_put:
	qd_put(qd);
	return error;
}

const struct quotactl_ops gfs2_quotactl_ops = {
	.quota_sync     = gfs2_quota_sync,
	.get_state	= gfs2_quota_get_state,
	.get_dqblk	= gfs2_get_dqblk,
	.set_dqblk	= gfs2_set_dqblk,
};

void __init gfs2_quota_hash_init(void)
{
	unsigned i;

	for(i = 0; i < GFS2_QD_HASH_SIZE; i++)
		INIT_HLIST_BL_HEAD(&qd_hash_table[i]);
}
