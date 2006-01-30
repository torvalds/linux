/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License v.2.
 */

/*
 * Quota change tags are associated with each transaction that allocates or
 * deallocates space.  Those changes are accumulated locally to each node (in a
 * per-node file) and then are periodically synced to the quota file.  This
 * avoids the bottleneck of constantly touching the quota file, but introduces
 * fuzziness in the current usage value of IDs that are being used on different
 * nodes in the cluster simultaneously.  So, it is possible for a user on
 * multiple nodes to overrun their quota, but that overrun is controlable.
 * Since quota tags are part of transactions, there is no need to a quota check
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

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/tty.h>
#include <linux/sort.h>
#include <asm/semaphore.h>

#include "gfs2.h"
#include "bmap.h"
#include "glock.h"
#include "glops.h"
#include "jdata.h"
#include "log.h"
#include "meta_io.h"
#include "quota.h"
#include "rgrp.h"
#include "super.h"
#include "trans.h"
#include "ops_file.h"

#define QUOTA_USER 1
#define QUOTA_GROUP 0

static uint64_t qd2offset(struct gfs2_quota_data *qd)
{
	uint64_t offset;

	offset = 2 * (uint64_t)qd->qd_id + !test_bit(QDF_USER, &qd->qd_flags);
	offset *= sizeof(struct gfs2_quota);

	return offset;
}

static int qd_alloc(struct gfs2_sbd *sdp, int user, uint32_t id,
		    struct gfs2_quota_data **qdp)
{
	struct gfs2_quota_data *qd;
	int error;

	qd = kzalloc(sizeof(struct gfs2_quota_data), GFP_KERNEL);
	if (!qd)
		return -ENOMEM;

	qd->qd_count = 1;
	qd->qd_id = id;
	if (user)
		set_bit(QDF_USER, &qd->qd_flags);
	qd->qd_slot = -1;

	error = gfs2_glock_get(sdp, 2 * (uint64_t)id + !user,
			      &gfs2_quota_glops, CREATE, &qd->qd_gl);
	if (error)
		goto fail;

	error = gfs2_lvb_hold(qd->qd_gl);
	gfs2_glock_put(qd->qd_gl);
	if (error)
		goto fail;

	*qdp = qd;

	return 0;

 fail:
	kfree(qd);
	return error;
}

static int qd_get(struct gfs2_sbd *sdp, int user, uint32_t id, int create,
		  struct gfs2_quota_data **qdp)
{
	struct gfs2_quota_data *qd = NULL, *new_qd = NULL;
	int error, found;

	*qdp = NULL;

	for (;;) {
		found = 0;
		spin_lock(&sdp->sd_quota_spin);
		list_for_each_entry(qd, &sdp->sd_quota_list, qd_list) {
			if (qd->qd_id == id &&
			    !test_bit(QDF_USER, &qd->qd_flags) == !user) {
				qd->qd_count++;
				found = 1;
				break;
			}
		}

		if (!found)
			qd = NULL;

		if (!qd && new_qd) {
			qd = new_qd;
			list_add(&qd->qd_list, &sdp->sd_quota_list);
			atomic_inc(&sdp->sd_quota_count);
			new_qd = NULL;
		}

		spin_unlock(&sdp->sd_quota_spin);

		if (qd || !create) {
			if (new_qd) {
				gfs2_lvb_unhold(new_qd->qd_gl);
				kfree(new_qd);
			}
			*qdp = qd;
			return 0;
		}

		error = qd_alloc(sdp, user, id, &new_qd);
		if (error)
			return error;
	}
}

static void qd_hold(struct gfs2_quota_data *qd)
{
	struct gfs2_sbd *sdp = qd->qd_gl->gl_sbd;

	spin_lock(&sdp->sd_quota_spin);
	gfs2_assert(sdp, qd->qd_count);
	qd->qd_count++;
	spin_unlock(&sdp->sd_quota_spin);
}

static void qd_put(struct gfs2_quota_data *qd)
{
	struct gfs2_sbd *sdp = qd->qd_gl->gl_sbd;
	spin_lock(&sdp->sd_quota_spin);
	gfs2_assert(sdp, qd->qd_count);
	if (!--qd->qd_count)
		qd->qd_last_touched = jiffies;
	spin_unlock(&sdp->sd_quota_spin);
}

static int slot_get(struct gfs2_quota_data *qd)
{
	struct gfs2_sbd *sdp = qd->qd_gl->gl_sbd;
	unsigned int c, o = 0, b;
	unsigned char byte = 0;

	spin_lock(&sdp->sd_quota_spin);

	if (qd->qd_slot_count++) {
		spin_unlock(&sdp->sd_quota_spin);
		return 0;
	}

	for (c = 0; c < sdp->sd_quota_chunks; c++)
		for (o = 0; o < PAGE_SIZE; o++) {
			byte = sdp->sd_quota_bitmap[c][o];
			if (byte != 0xFF)
				goto found;
		}

	goto fail;

 found:
	for (b = 0; b < 8; b++)
		if (!(byte & (1 << b)))
			break;
	qd->qd_slot = c * (8 * PAGE_SIZE) + o * 8 + b;

	if (qd->qd_slot >= sdp->sd_quota_slots)
		goto fail;

	sdp->sd_quota_bitmap[c][o] |= 1 << b;

	spin_unlock(&sdp->sd_quota_spin);

	return 0;

 fail:
	qd->qd_slot_count--;
	spin_unlock(&sdp->sd_quota_spin);
	return -ENOSPC;
}

static void slot_hold(struct gfs2_quota_data *qd)
{
	struct gfs2_sbd *sdp = qd->qd_gl->gl_sbd;

	spin_lock(&sdp->sd_quota_spin);
	gfs2_assert(sdp, qd->qd_slot_count);
	qd->qd_slot_count++;
	spin_unlock(&sdp->sd_quota_spin);
}

static void slot_put(struct gfs2_quota_data *qd)
{
	struct gfs2_sbd *sdp = qd->qd_gl->gl_sbd;

	spin_lock(&sdp->sd_quota_spin);
	gfs2_assert(sdp, qd->qd_slot_count);
	if (!--qd->qd_slot_count) {
		gfs2_icbit_munge(sdp, sdp->sd_quota_bitmap, qd->qd_slot, 0);
		qd->qd_slot = -1;
	}
	spin_unlock(&sdp->sd_quota_spin);
}

static int bh_get(struct gfs2_quota_data *qd)
{
	struct gfs2_sbd *sdp = qd->qd_gl->gl_sbd;
	struct gfs2_inode *ip = get_v2ip(sdp->sd_qc_inode);
	unsigned int block, offset;
	uint64_t dblock;
	int new = 0;
	struct buffer_head *bh;
	int error;

	down(&sdp->sd_quota_mutex);

	if (qd->qd_bh_count++) {
		up(&sdp->sd_quota_mutex);
		return 0;
	}

	block = qd->qd_slot / sdp->sd_qc_per_block;
	offset = qd->qd_slot % sdp->sd_qc_per_block;;

	error = gfs2_block_map(ip, block, &new, &dblock, NULL);
	if (error)
		goto fail;
	error = gfs2_meta_read(ip->i_gl, dblock, DIO_START | DIO_WAIT, &bh);
	if (error)
		goto fail;
	error = -EIO;
	if (gfs2_metatype_check(sdp, bh, GFS2_METATYPE_QC))
		goto fail_brelse;

	qd->qd_bh = bh;
	qd->qd_bh_qc = (struct gfs2_quota_change *)
		(bh->b_data + sizeof(struct gfs2_meta_header) +
		 offset * sizeof(struct gfs2_quota_change));

	up(&sdp->sd_quota_mutex);

	return 0;

 fail_brelse:
	brelse(bh);

 fail:
	qd->qd_bh_count--;
	up(&sdp->sd_quota_mutex);
	return error;
}

static void bh_put(struct gfs2_quota_data *qd)
{
	struct gfs2_sbd *sdp = qd->qd_gl->gl_sbd;

	down(&sdp->sd_quota_mutex);
	gfs2_assert(sdp, qd->qd_bh_count);
	if (!--qd->qd_bh_count) {
		brelse(qd->qd_bh);
		qd->qd_bh = NULL;
		qd->qd_bh_qc = NULL;
	}
	up(&sdp->sd_quota_mutex);
}

static int qd_fish(struct gfs2_sbd *sdp, struct gfs2_quota_data **qdp)
{
	struct gfs2_quota_data *qd = NULL;
	int error;
	int found = 0;

	*qdp = NULL;

	if (sdp->sd_vfs->s_flags & MS_RDONLY)
		return 0;

	spin_lock(&sdp->sd_quota_spin);

	list_for_each_entry(qd, &sdp->sd_quota_list, qd_list) {
		if (test_bit(QDF_LOCKED, &qd->qd_flags) ||
		    !test_bit(QDF_CHANGE, &qd->qd_flags) ||
		    qd->qd_sync_gen >= sdp->sd_quota_sync_gen)
			continue;

		list_move_tail(&qd->qd_list, &sdp->sd_quota_list);

		set_bit(QDF_LOCKED, &qd->qd_flags);
		gfs2_assert_warn(sdp, qd->qd_count);
		qd->qd_count++;
		qd->qd_change_sync = qd->qd_change;
		gfs2_assert_warn(sdp, qd->qd_slot_count);
		qd->qd_slot_count++;
		found = 1;

		break;
	}

	if (!found)
		qd = NULL;

	spin_unlock(&sdp->sd_quota_spin);

	if (qd) {
		gfs2_assert_warn(sdp, qd->qd_change_sync);
		error = bh_get(qd);
		if (error) {
			clear_bit(QDF_LOCKED, &qd->qd_flags);
			slot_put(qd);
			qd_put(qd);
			return error;
		}
	}

	*qdp = qd;

	return 0;
}

static int qd_trylock(struct gfs2_quota_data *qd)
{
	struct gfs2_sbd *sdp = qd->qd_gl->gl_sbd;

	if (sdp->sd_vfs->s_flags & MS_RDONLY)
		return 0;

	spin_lock(&sdp->sd_quota_spin);

	if (test_bit(QDF_LOCKED, &qd->qd_flags) ||
	    !test_bit(QDF_CHANGE, &qd->qd_flags)) {
		spin_unlock(&sdp->sd_quota_spin);
		return 0;
	}

	list_move_tail(&qd->qd_list, &sdp->sd_quota_list);

	set_bit(QDF_LOCKED, &qd->qd_flags);
	gfs2_assert_warn(sdp, qd->qd_count);
	qd->qd_count++;
	qd->qd_change_sync = qd->qd_change;
	gfs2_assert_warn(sdp, qd->qd_slot_count);
	qd->qd_slot_count++;

	spin_unlock(&sdp->sd_quota_spin);

	gfs2_assert_warn(sdp, qd->qd_change_sync);
	if (bh_get(qd)) {
		clear_bit(QDF_LOCKED, &qd->qd_flags);
		slot_put(qd);
		qd_put(qd);
		return 0;
	}

	return 1;
}

static void qd_unlock(struct gfs2_quota_data *qd)
{
	gfs2_assert_warn(qd->qd_gl->gl_sbd, test_bit(QDF_LOCKED, &qd->qd_flags));
	clear_bit(QDF_LOCKED, &qd->qd_flags);
	bh_put(qd);
	slot_put(qd);
	qd_put(qd);
}

static int qdsb_get(struct gfs2_sbd *sdp, int user, uint32_t id, int create,
		    struct gfs2_quota_data **qdp)
{
	int error;

	error = qd_get(sdp, user, id, create, qdp);
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

static void qdsb_put(struct gfs2_quota_data *qd)
{
	bh_put(qd);
	slot_put(qd);
	qd_put(qd);
}

int gfs2_quota_hold(struct gfs2_inode *ip, uint32_t uid, uint32_t gid)
{
	struct gfs2_sbd *sdp = ip->i_sbd;
	struct gfs2_alloc *al = &ip->i_alloc;
	struct gfs2_quota_data **qd = al->al_qd;
	int error;

	if (gfs2_assert_warn(sdp, !al->al_qd_num) ||
	    gfs2_assert_warn(sdp, !test_bit(GIF_QD_LOCKED, &ip->i_flags)))
		return -EIO;

	if (sdp->sd_args.ar_quota == GFS2_QUOTA_OFF)
		return 0;

	error = qdsb_get(sdp, QUOTA_USER, ip->i_di.di_uid, CREATE, qd);
	if (error)
		goto out;
	al->al_qd_num++;
	qd++;

	error = qdsb_get(sdp, QUOTA_GROUP, ip->i_di.di_gid, CREATE, qd);
	if (error)
		goto out;
	al->al_qd_num++;
	qd++;

	if (uid != NO_QUOTA_CHANGE && uid != ip->i_di.di_uid) {
		error = qdsb_get(sdp, QUOTA_USER, uid, CREATE, qd);
		if (error)
			goto out;
		al->al_qd_num++;
		qd++;
	}

	if (gid != NO_QUOTA_CHANGE && gid != ip->i_di.di_gid) {
		error = qdsb_get(sdp, QUOTA_GROUP, gid, CREATE, qd);
		if (error)
			goto out;
		al->al_qd_num++;
		qd++;
	}

 out:
	if (error)
		gfs2_quota_unhold(ip);

	return error;
}

void gfs2_quota_unhold(struct gfs2_inode *ip)
{
	struct gfs2_sbd *sdp = ip->i_sbd;
	struct gfs2_alloc *al = &ip->i_alloc;
	unsigned int x;

	gfs2_assert_warn(sdp, !test_bit(GIF_QD_LOCKED, &ip->i_flags));

	for (x = 0; x < al->al_qd_num; x++) {
		qdsb_put(al->al_qd[x]);
		al->al_qd[x] = NULL;
	}
	al->al_qd_num = 0;
}

static int sort_qd(const void *a, const void *b)
{
	struct gfs2_quota_data *qd_a = *(struct gfs2_quota_data **)a;
	struct gfs2_quota_data *qd_b = *(struct gfs2_quota_data **)b;
	int ret = 0;

	if (!test_bit(QDF_USER, &qd_a->qd_flags) !=
	    !test_bit(QDF_USER, &qd_b->qd_flags)) {
		if (test_bit(QDF_USER, &qd_a->qd_flags))
			ret = -1;
		else
			ret = 1;
	} else {
		if (qd_a->qd_id < qd_b->qd_id)
			ret = -1;
		else if (qd_a->qd_id > qd_b->qd_id)
			ret = 1;
	}

	return ret;
}

static void do_qc(struct gfs2_quota_data *qd, int64_t change)
{
	struct gfs2_sbd *sdp = qd->qd_gl->gl_sbd;
	struct gfs2_inode *ip = get_v2ip(sdp->sd_qc_inode);
	struct gfs2_quota_change *qc = qd->qd_bh_qc;
	int64_t x;

	down(&sdp->sd_quota_mutex);
	gfs2_trans_add_bh(ip->i_gl, qd->qd_bh, 1);

	if (!test_bit(QDF_CHANGE, &qd->qd_flags)) {
		qc->qc_change = 0;
		qc->qc_flags = 0;
		if (test_bit(QDF_USER, &qd->qd_flags))
			qc->qc_flags = cpu_to_be32(GFS2_QCF_USER);
		qc->qc_id = cpu_to_be32(qd->qd_id);
	}

	x = qc->qc_change;
	x = be64_to_cpu(x) + change;
	qc->qc_change = cpu_to_be64(x);

	spin_lock(&sdp->sd_quota_spin);
	qd->qd_change = x;
	spin_unlock(&sdp->sd_quota_spin);

	if (!x) {
		gfs2_assert_warn(sdp, test_bit(QDF_CHANGE, &qd->qd_flags));
		clear_bit(QDF_CHANGE, &qd->qd_flags);
		qc->qc_flags = 0;
		qc->qc_id = 0;
		slot_put(qd);
		qd_put(qd);
	} else if (!test_and_set_bit(QDF_CHANGE, &qd->qd_flags)) {
		qd_hold(qd);
		slot_hold(qd);
	}
			
	up(&sdp->sd_quota_mutex);
}

static int do_sync(unsigned int num_qd, struct gfs2_quota_data **qda)
{
	struct gfs2_sbd *sdp = (*qda)->qd_gl->gl_sbd;
	struct gfs2_inode *ip = get_v2ip(sdp->sd_quota_inode);
	unsigned int data_blocks, ind_blocks;
	struct file_ra_state ra_state;
	struct gfs2_holder *ghs, i_gh;
	unsigned int qx, x;
	struct gfs2_quota_data *qd;
	loff_t offset;
	unsigned int nalloc = 0;
	struct gfs2_alloc *al = NULL;
	int error;

	gfs2_write_calc_reserv(ip, sizeof(struct gfs2_quota),
			      &data_blocks, &ind_blocks);

	ghs = kcalloc(num_qd, sizeof(struct gfs2_holder), GFP_KERNEL);
	if (!ghs)
		return -ENOMEM;

	sort(qda, num_qd, sizeof(struct gfs2_quota_data *), sort_qd, NULL);
	for (qx = 0; qx < num_qd; qx++) {
		error = gfs2_glock_nq_init(qda[qx]->qd_gl,
					   LM_ST_EXCLUSIVE,
					   GL_NOCACHE, &ghs[qx]);
		if (error)
			goto out;
	}

	error = gfs2_glock_nq_init(ip->i_gl, LM_ST_EXCLUSIVE, 0, &i_gh);
	if (error)
		goto out;

	for (x = 0; x < num_qd; x++) {
		int alloc_required;

		offset = qd2offset(qda[x]);
		error = gfs2_write_alloc_required(ip, offset,
						  sizeof(struct gfs2_quota),
						  &alloc_required);
		if (error)
			goto out_gunlock;
		if (alloc_required)
			nalloc++;
	}

	if (nalloc) {
		al = gfs2_alloc_get(ip);

		al->al_requested = nalloc * (data_blocks + ind_blocks);

		error = gfs2_inplace_reserve(ip);
		if (error)
			goto out_alloc;

		error = gfs2_trans_begin(sdp,
					 al->al_rgd->rd_ri.ri_length +
					 num_qd * data_blocks +
					 nalloc * ind_blocks +
					 RES_DINODE + num_qd +
					 RES_STATFS, 0);
		if (error)
			goto out_ipres;
	} else {
		error = gfs2_trans_begin(sdp,
					 num_qd * data_blocks +
					 RES_DINODE + num_qd, 0);
		if (error)
			goto out_gunlock;
	}

	file_ra_state_init(&ra_state, ip->i_vnode->i_mapping);
	for (x = 0; x < num_qd; x++) {
		char buf[sizeof(struct gfs2_quota)];
		struct gfs2_quota q;

		qd = qda[x];
		offset = qd2offset(qd);

		/* The quota file may not be a multiple of
		   sizeof(struct gfs2_quota) bytes. */
		memset(buf, 0, sizeof(struct gfs2_quota));

		error = gfs2_internal_read(ip, &ra_state, buf, &offset,
					    sizeof(struct gfs2_quota));
		if (error < 0)
			goto out_end_trans;

		gfs2_quota_in(&q, buf);
		q.qu_value += qda[x]->qd_change_sync;
		gfs2_quota_out(&q, buf);

		error = gfs2_jdata_write_mem(ip, buf, offset,
					     sizeof(struct gfs2_quota));
		if (error < 0)
			goto out_end_trans;
		else if (error != sizeof(struct gfs2_quota)) {
			error = -EIO;
			goto out_end_trans;
		}

		do_qc(qd, -qd->qd_change_sync);

		memset(&qd->qd_qb, 0, sizeof(struct gfs2_quota_lvb));
		qd->qd_qb.qb_magic = GFS2_MAGIC;
		qd->qd_qb.qb_limit = q.qu_limit;
		qd->qd_qb.qb_warn = q.qu_warn;
		qd->qd_qb.qb_value = q.qu_value;

		gfs2_quota_lvb_out(&qd->qd_qb, qd->qd_gl->gl_lvb);
	}

	error = 0;

 out_end_trans:
	gfs2_trans_end(sdp);

 out_ipres:
	if (nalloc)
		gfs2_inplace_release(ip);

 out_alloc:
	if (nalloc)
		gfs2_alloc_put(ip);

 out_gunlock:
	gfs2_glock_dq_uninit(&i_gh);

 out:
	while (qx--)
		gfs2_glock_dq_uninit(&ghs[qx]);
	kfree(ghs);
	gfs2_log_flush_glock(ip->i_gl);

	return error;
}

static int do_glock(struct gfs2_quota_data *qd, int force_refresh,
		    struct gfs2_holder *q_gh)
{
	struct gfs2_sbd *sdp = qd->qd_gl->gl_sbd;
	struct gfs2_holder i_gh;
	struct gfs2_quota q;
	char buf[sizeof(struct gfs2_quota)];
	struct file_ra_state ra_state;
	int error;

	file_ra_state_init(&ra_state, sdp->sd_quota_inode->i_mapping);
 restart:
	error = gfs2_glock_nq_init(qd->qd_gl, LM_ST_SHARED, 0, q_gh);
	if (error)
		return error;

	gfs2_quota_lvb_in(&qd->qd_qb, qd->qd_gl->gl_lvb);

	if (force_refresh || qd->qd_qb.qb_magic != GFS2_MAGIC) {
		loff_t pos;
		gfs2_glock_dq_uninit(q_gh);
		error = gfs2_glock_nq_init(qd->qd_gl,
					  LM_ST_EXCLUSIVE, GL_NOCACHE,
					  q_gh);
		if (error)
			return error;

		error = gfs2_glock_nq_init(get_v2ip(sdp->sd_quota_inode)->i_gl,
					  LM_ST_SHARED, 0,
					  &i_gh);
		if (error)
			goto fail;

		memset(buf, 0, sizeof(struct gfs2_quota));
		pos = qd2offset(qd);
		error = gfs2_internal_read(get_v2ip(sdp->sd_quota_inode),
					    &ra_state, buf,
					    &pos,
					    sizeof(struct gfs2_quota));
		if (error < 0)
			goto fail_gunlock;

		gfs2_glock_dq_uninit(&i_gh);

		gfs2_quota_in(&q, buf);

		memset(&qd->qd_qb, 0, sizeof(struct gfs2_quota_lvb));
		qd->qd_qb.qb_magic = GFS2_MAGIC;
		qd->qd_qb.qb_limit = q.qu_limit;
		qd->qd_qb.qb_warn = q.qu_warn;
		qd->qd_qb.qb_value = q.qu_value;

		gfs2_quota_lvb_out(&qd->qd_qb, qd->qd_gl->gl_lvb);

		if (gfs2_glock_is_blocking(qd->qd_gl)) {
			gfs2_glock_dq_uninit(q_gh);
			force_refresh = 0;
			goto restart;
		}
	}

	return 0;

 fail_gunlock:
	gfs2_glock_dq_uninit(&i_gh);

 fail:
	gfs2_glock_dq_uninit(q_gh);

	return error;
}

int gfs2_quota_lock(struct gfs2_inode *ip, uint32_t uid, uint32_t gid)
{
	struct gfs2_sbd *sdp = ip->i_sbd;
	struct gfs2_alloc *al = &ip->i_alloc;
	unsigned int x;
	int error = 0;

	gfs2_quota_hold(ip, uid, gid);

	if (capable(CAP_SYS_RESOURCE) ||
	    sdp->sd_args.ar_quota != GFS2_QUOTA_ON)
		return 0;

	sort(al->al_qd, al->al_qd_num, sizeof(struct gfs2_quota_data *),
	     sort_qd, NULL);

	for (x = 0; x < al->al_qd_num; x++) {
		error = do_glock(al->al_qd[x], NO_FORCE, &al->al_qd_ghs[x]);
		if (error)
			break;
	}

	if (!error)
		set_bit(GIF_QD_LOCKED, &ip->i_flags);
	else {
		while (x--)
			gfs2_glock_dq_uninit(&al->al_qd_ghs[x]);
		gfs2_quota_unhold(ip);
	}

	return error;
}

static int need_sync(struct gfs2_quota_data *qd)
{
	struct gfs2_sbd *sdp = qd->qd_gl->gl_sbd;
	struct gfs2_tune *gt = &sdp->sd_tune;
	int64_t value;
	unsigned int num, den;
	int do_sync = 1;

	if (!qd->qd_qb.qb_limit)
		return 0;

	spin_lock(&sdp->sd_quota_spin);
	value = qd->qd_change;
	spin_unlock(&sdp->sd_quota_spin);

	spin_lock(&gt->gt_spin);
	num = gt->gt_quota_scale_num;
	den = gt->gt_quota_scale_den;
	spin_unlock(&gt->gt_spin);

	if (value < 0)
		do_sync = 0;
	else if (qd->qd_qb.qb_value >= (int64_t)qd->qd_qb.qb_limit)
		do_sync = 0;
	else {
		value *= gfs2_jindex_size(sdp) * num;
		do_div(value, den);
		value += qd->qd_qb.qb_value;
		if (value < (int64_t)qd->qd_qb.qb_limit)
			do_sync = 0;
	}

	return do_sync;
}

void gfs2_quota_unlock(struct gfs2_inode *ip)
{
	struct gfs2_alloc *al = &ip->i_alloc;
	struct gfs2_quota_data *qda[4];
	unsigned int count = 0;
	unsigned int x;

	if (!test_and_clear_bit(GIF_QD_LOCKED, &ip->i_flags))
		goto out;

	for (x = 0; x < al->al_qd_num; x++) {
		struct gfs2_quota_data *qd;
		int sync;

		qd = al->al_qd[x];
		sync = need_sync(qd);

		gfs2_glock_dq_uninit(&al->al_qd_ghs[x]);

		if (sync && qd_trylock(qd))
			qda[count++] = qd;
	}

	if (count) {
		do_sync(count, qda);
		for (x = 0; x < count; x++)
			qd_unlock(qda[x]);
	}

 out:
	gfs2_quota_unhold(ip);
}

#define MAX_LINE 256

static int print_message(struct gfs2_quota_data *qd, char *type)
{
	struct gfs2_sbd *sdp = qd->qd_gl->gl_sbd;
	char *line;
	int len;

	line = kmalloc(MAX_LINE, GFP_KERNEL);
	if (!line)
		return -ENOMEM;

	len = snprintf(line, MAX_LINE-1, "GFS2: fsid=%s: quota %s for %s %u\r\n",
		       sdp->sd_fsname, type,
		       (test_bit(QDF_USER, &qd->qd_flags)) ? "user" : "group",
		       qd->qd_id);
	line[MAX_LINE-1] = 0;

	if (current->signal) { /* Is this test still required? */
		tty_write_message(current->signal->tty, line);
	}

	kfree(line);

	return 0;
}

int gfs2_quota_check(struct gfs2_inode *ip, uint32_t uid, uint32_t gid)
{
	struct gfs2_sbd *sdp = ip->i_sbd;
	struct gfs2_alloc *al = &ip->i_alloc;
	struct gfs2_quota_data *qd;
	int64_t value;
	unsigned int x;
	int error = 0;

	if (!test_bit(GIF_QD_LOCKED, &ip->i_flags))
		return 0;

        if (sdp->sd_args.ar_quota != GFS2_QUOTA_ON)
                return 0;

	for (x = 0; x < al->al_qd_num; x++) {
		qd = al->al_qd[x];

		if (!((qd->qd_id == uid && test_bit(QDF_USER, &qd->qd_flags)) ||
		      (qd->qd_id == gid && !test_bit(QDF_USER, &qd->qd_flags))))
			continue;

		value = qd->qd_qb.qb_value;
		spin_lock(&sdp->sd_quota_spin);
		value += qd->qd_change;
		spin_unlock(&sdp->sd_quota_spin);

		if (qd->qd_qb.qb_limit && (int64_t)qd->qd_qb.qb_limit < value) {
			print_message(qd, "exceeded");
			error = -EDQUOT;
			break;
		} else if (qd->qd_qb.qb_warn &&
			   (int64_t)qd->qd_qb.qb_warn < value &&
			   time_after_eq(jiffies, qd->qd_last_warn +
					 gfs2_tune_get(sdp, gt_quota_warn_period) * HZ)) {
			error = print_message(qd, "warning");
			qd->qd_last_warn = jiffies;
		}
	}

	return error;
}

void gfs2_quota_change(struct gfs2_inode *ip, int64_t change,
		       uint32_t uid, uint32_t gid)
{
	struct gfs2_alloc *al = &ip->i_alloc;
	struct gfs2_quota_data *qd;
	unsigned int x;
	unsigned int found = 0;

	if (gfs2_assert_warn(ip->i_sbd, change))
		return;
	if (ip->i_di.di_flags & GFS2_DIF_SYSTEM)
		return;

	for (x = 0; x < al->al_qd_num; x++) {
		qd = al->al_qd[x];

		if ((qd->qd_id == uid && test_bit(QDF_USER, &qd->qd_flags)) ||
		    (qd->qd_id == gid && !test_bit(QDF_USER, &qd->qd_flags))) {
			do_qc(qd, change);
			found++;
		}
	}
}

int gfs2_quota_sync(struct gfs2_sbd *sdp)
{
	struct gfs2_quota_data **qda;
	unsigned int max_qd = gfs2_tune_get(sdp, gt_quota_simul_sync);
	unsigned int num_qd;
	unsigned int x;
	int error = 0;

	sdp->sd_quota_sync_gen++;

	qda = kcalloc(max_qd, sizeof(struct gfs2_quota_data *), GFP_KERNEL);
	if (!qda)
		return -ENOMEM;

	do {
		num_qd = 0;

		for (;;) {
			error = qd_fish(sdp, qda + num_qd);
			if (error || !qda[num_qd])
				break;
			if (++num_qd == max_qd)
				break;
		}

		if (num_qd) {
			if (!error)
				error = do_sync(num_qd, qda);
			if (!error)
				for (x = 0; x < num_qd; x++)
					qda[x]->qd_sync_gen =
						sdp->sd_quota_sync_gen;

			for (x = 0; x < num_qd; x++)
				qd_unlock(qda[x]);
		}
	} while (!error && num_qd == max_qd);

	kfree(qda);

	return error;
}

int gfs2_quota_refresh(struct gfs2_sbd *sdp, int user, uint32_t id)
{
	struct gfs2_quota_data *qd;
	struct gfs2_holder q_gh;
	int error;

	error = qd_get(sdp, user, id, CREATE, &qd);
	if (error)
		return error;

	error = do_glock(qd, FORCE, &q_gh);
	if (!error)
		gfs2_glock_dq_uninit(&q_gh);

	qd_put(qd);

	return error;
}

int gfs2_quota_read(struct gfs2_sbd *sdp, int user, uint32_t id,
		    struct gfs2_quota *q)
{
	struct gfs2_quota_data *qd;
	struct gfs2_holder q_gh;
	int error;

	if (((user) ? (id != current->fsuid) : (!in_group_p(id))) &&
	    !capable(CAP_SYS_ADMIN))
		return -EACCES;

	error = qd_get(sdp, user, id, CREATE, &qd);
	if (error)
		return error;

	error = do_glock(qd, NO_FORCE, &q_gh);
	if (error)
		goto out;

	memset(q, 0, sizeof(struct gfs2_quota));
	q->qu_limit = qd->qd_qb.qb_limit;
	q->qu_warn = qd->qd_qb.qb_warn;
	q->qu_value = qd->qd_qb.qb_value;

	spin_lock(&sdp->sd_quota_spin);
	q->qu_value += qd->qd_change;
	spin_unlock(&sdp->sd_quota_spin);

	gfs2_glock_dq_uninit(&q_gh);

 out:
	qd_put(qd);

	return error;
}

int gfs2_quota_init(struct gfs2_sbd *sdp)
{
	struct gfs2_inode *ip = get_v2ip(sdp->sd_qc_inode);
	unsigned int blocks = ip->i_di.di_size >> sdp->sd_sb.sb_bsize_shift;
	unsigned int x, slot = 0;
	unsigned int found = 0;
	uint64_t dblock;
	uint32_t extlen = 0;
	int error;

	if (!ip->i_di.di_size ||
	    ip->i_di.di_size > (64 << 20) ||
	    ip->i_di.di_size & (sdp->sd_sb.sb_bsize - 1)) {
		gfs2_consist_inode(ip);
		return -EIO;		
	}
	sdp->sd_quota_slots = blocks * sdp->sd_qc_per_block;
	sdp->sd_quota_chunks = DIV_RU(sdp->sd_quota_slots, 8 * PAGE_SIZE);

	error = -ENOMEM;

	sdp->sd_quota_bitmap = kcalloc(sdp->sd_quota_chunks,
				       sizeof(unsigned char *), GFP_KERNEL);
	if (!sdp->sd_quota_bitmap)
		return error;

	for (x = 0; x < sdp->sd_quota_chunks; x++) {
		sdp->sd_quota_bitmap[x] = kzalloc(PAGE_SIZE, GFP_KERNEL);
		if (!sdp->sd_quota_bitmap[x])
			goto fail;
	}

	for (x = 0; x < blocks; x++) {
		struct buffer_head *bh;
		unsigned int y;

		if (!extlen) {
			int new = 0;
			error = gfs2_block_map(ip, x, &new, &dblock, &extlen);
			if (error)
				goto fail;
		}
		gfs2_meta_ra(ip->i_gl,  dblock, extlen);
		error = gfs2_meta_read(ip->i_gl, dblock, DIO_START | DIO_WAIT,
				       &bh);
		if (error)
			goto fail;
		error = -EIO;
		if (gfs2_metatype_check(sdp, bh, GFS2_METATYPE_QC)) {
			brelse(bh);
			goto fail;
		}

		for (y = 0;
		     y < sdp->sd_qc_per_block && slot < sdp->sd_quota_slots;
		     y++, slot++) {
			struct gfs2_quota_change qc;
			struct gfs2_quota_data *qd;

			gfs2_quota_change_in(&qc, bh->b_data +
					  sizeof(struct gfs2_meta_header) +
					  y * sizeof(struct gfs2_quota_change));
			if (!qc.qc_change)
				continue;

			error = qd_alloc(sdp, (qc.qc_flags & GFS2_QCF_USER),
					 qc.qc_id, &qd);
			if (error) {
				brelse(bh);
				goto fail;
			}

			set_bit(QDF_CHANGE, &qd->qd_flags);
			qd->qd_change = qc.qc_change;
			qd->qd_slot = slot;
			qd->qd_slot_count = 1;
			qd->qd_last_touched = jiffies;

			spin_lock(&sdp->sd_quota_spin);
			gfs2_icbit_munge(sdp, sdp->sd_quota_bitmap, slot, 1);
			list_add(&qd->qd_list, &sdp->sd_quota_list);
			atomic_inc(&sdp->sd_quota_count);
			spin_unlock(&sdp->sd_quota_spin);

			found++;
		}

		brelse(bh);
		dblock++;
		extlen--;
	}

	if (found)
		fs_info(sdp, "found %u quota changes\n", found);

	return 0;

 fail:
	gfs2_quota_cleanup(sdp);
	return error;
}

void gfs2_quota_scan(struct gfs2_sbd *sdp)
{
	struct gfs2_quota_data *qd, *safe;
	LIST_HEAD(dead);

	spin_lock(&sdp->sd_quota_spin);
	list_for_each_entry_safe(qd, safe, &sdp->sd_quota_list, qd_list) {
		if (!qd->qd_count &&
		    time_after_eq(jiffies, qd->qd_last_touched +
			        gfs2_tune_get(sdp, gt_quota_cache_secs) * HZ)) {
			list_move(&qd->qd_list, &dead);
			gfs2_assert_warn(sdp,
					 atomic_read(&sdp->sd_quota_count) > 0);
			atomic_dec(&sdp->sd_quota_count);
		}
	}
	spin_unlock(&sdp->sd_quota_spin);

	while (!list_empty(&dead)) {
		qd = list_entry(dead.next, struct gfs2_quota_data, qd_list);
		list_del(&qd->qd_list);

		gfs2_assert_warn(sdp, !qd->qd_change);
		gfs2_assert_warn(sdp, !qd->qd_slot_count);
		gfs2_assert_warn(sdp, !qd->qd_bh_count);

		gfs2_lvb_unhold(qd->qd_gl);
		kfree(qd);
	}
}

void gfs2_quota_cleanup(struct gfs2_sbd *sdp)
{
	struct list_head *head = &sdp->sd_quota_list;
	struct gfs2_quota_data *qd;
	unsigned int x;

	spin_lock(&sdp->sd_quota_spin);
	while (!list_empty(head)) {
		qd = list_entry(head->prev, struct gfs2_quota_data, qd_list);

		if (qd->qd_count > 1 ||
		    (qd->qd_count && !test_bit(QDF_CHANGE, &qd->qd_flags))) {
			list_move(&qd->qd_list, head);
			spin_unlock(&sdp->sd_quota_spin);
			schedule();
			spin_lock(&sdp->sd_quota_spin);
			continue;
		}

		list_del(&qd->qd_list);
		atomic_dec(&sdp->sd_quota_count);
		spin_unlock(&sdp->sd_quota_spin);

		if (!qd->qd_count) {
			gfs2_assert_warn(sdp, !qd->qd_change);
			gfs2_assert_warn(sdp, !qd->qd_slot_count);
		} else
			gfs2_assert_warn(sdp, qd->qd_slot_count == 1);
		gfs2_assert_warn(sdp, !qd->qd_bh_count);

		gfs2_lvb_unhold(qd->qd_gl);
		kfree(qd);

		spin_lock(&sdp->sd_quota_spin);
	}
	spin_unlock(&sdp->sd_quota_spin);

	gfs2_assert_warn(sdp, !atomic_read(&sdp->sd_quota_count));

	if (sdp->sd_quota_bitmap) {
		for (x = 0; x < sdp->sd_quota_chunks; x++)
			kfree(sdp->sd_quota_bitmap[x]);
		kfree(sdp->sd_quota_bitmap);
	}
}

