/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2007 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/crc32.h>
#include <linux/gfs2_ondisk.h>
#include <linux/bio.h>
#include <linux/lm_interface.h>

#include "gfs2.h"
#include "incore.h"
#include "bmap.h"
#include "dir.h"
#include "glock.h"
#include "glops.h"
#include "inode.h"
#include "log.h"
#include "meta_io.h"
#include "quota.h"
#include "recovery.h"
#include "rgrp.h"
#include "super.h"
#include "trans.h"
#include "util.h"

/**
 * gfs2_jindex_hold - Grab a lock on the jindex
 * @sdp: The GFS2 superblock
 * @ji_gh: the holder for the jindex glock
 *
 * This is very similar to the gfs2_rindex_hold() function, except that
 * in general we hold the jindex lock for longer periods of time and
 * we grab it far less frequently (in general) then the rgrp lock.
 *
 * Returns: errno
 */

int gfs2_jindex_hold(struct gfs2_sbd *sdp, struct gfs2_holder *ji_gh)
{
	struct gfs2_inode *dip = GFS2_I(sdp->sd_jindex);
	struct qstr name;
	char buf[20];
	struct gfs2_jdesc *jd;
	int error;

	name.name = buf;

	mutex_lock(&sdp->sd_jindex_mutex);

	for (;;) {
		error = gfs2_glock_nq_init(dip->i_gl, LM_ST_SHARED, 0, ji_gh);
		if (error)
			break;

		name.len = sprintf(buf, "journal%u", sdp->sd_journals);
		name.hash = gfs2_disk_hash(name.name, name.len);

		error = gfs2_dir_check(sdp->sd_jindex, &name, NULL);
		if (error == -ENOENT) {
			error = 0;
			break;
		}

		gfs2_glock_dq_uninit(ji_gh);

		if (error)
			break;

		error = -ENOMEM;
		jd = kzalloc(sizeof(struct gfs2_jdesc), GFP_KERNEL);
		if (!jd)
			break;

		INIT_LIST_HEAD(&jd->extent_list);
		jd->jd_inode = gfs2_lookupi(sdp->sd_jindex, &name, 1);
		if (!jd->jd_inode || IS_ERR(jd->jd_inode)) {
			if (!jd->jd_inode)
				error = -ENOENT;
			else
				error = PTR_ERR(jd->jd_inode);
			kfree(jd);
			break;
		}

		spin_lock(&sdp->sd_jindex_spin);
		jd->jd_jid = sdp->sd_journals++;
		list_add_tail(&jd->jd_list, &sdp->sd_jindex_list);
		spin_unlock(&sdp->sd_jindex_spin);
	}

	mutex_unlock(&sdp->sd_jindex_mutex);

	return error;
}

/**
 * gfs2_jindex_free - Clear all the journal index information
 * @sdp: The GFS2 superblock
 *
 */

void gfs2_jindex_free(struct gfs2_sbd *sdp)
{
	struct list_head list, *head;
	struct gfs2_jdesc *jd;
	struct gfs2_journal_extent *jext;

	spin_lock(&sdp->sd_jindex_spin);
	list_add(&list, &sdp->sd_jindex_list);
	list_del_init(&sdp->sd_jindex_list);
	sdp->sd_journals = 0;
	spin_unlock(&sdp->sd_jindex_spin);

	while (!list_empty(&list)) {
		jd = list_entry(list.next, struct gfs2_jdesc, jd_list);
		head = &jd->extent_list;
		while (!list_empty(head)) {
			jext = list_entry(head->next,
					  struct gfs2_journal_extent,
					  extent_list);
			list_del(&jext->extent_list);
			kfree(jext);
		}
		list_del(&jd->jd_list);
		iput(jd->jd_inode);
		kfree(jd);
	}
}

static struct gfs2_jdesc *jdesc_find_i(struct list_head *head, unsigned int jid)
{
	struct gfs2_jdesc *jd;
	int found = 0;

	list_for_each_entry(jd, head, jd_list) {
		if (jd->jd_jid == jid) {
			found = 1;
			break;
		}
	}

	if (!found)
		jd = NULL;

	return jd;
}

struct gfs2_jdesc *gfs2_jdesc_find(struct gfs2_sbd *sdp, unsigned int jid)
{
	struct gfs2_jdesc *jd;

	spin_lock(&sdp->sd_jindex_spin);
	jd = jdesc_find_i(&sdp->sd_jindex_list, jid);
	spin_unlock(&sdp->sd_jindex_spin);

	return jd;
}

void gfs2_jdesc_make_dirty(struct gfs2_sbd *sdp, unsigned int jid)
{
	struct gfs2_jdesc *jd;

	spin_lock(&sdp->sd_jindex_spin);
	jd = jdesc_find_i(&sdp->sd_jindex_list, jid);
	if (jd)
		jd->jd_dirty = 1;
	spin_unlock(&sdp->sd_jindex_spin);
}

struct gfs2_jdesc *gfs2_jdesc_find_dirty(struct gfs2_sbd *sdp)
{
	struct gfs2_jdesc *jd;
	int found = 0;

	spin_lock(&sdp->sd_jindex_spin);

	list_for_each_entry(jd, &sdp->sd_jindex_list, jd_list) {
		if (jd->jd_dirty) {
			jd->jd_dirty = 0;
			found = 1;
			break;
		}
	}
	spin_unlock(&sdp->sd_jindex_spin);

	if (!found)
		jd = NULL;

	return jd;
}

int gfs2_jdesc_check(struct gfs2_jdesc *jd)
{
	struct gfs2_inode *ip = GFS2_I(jd->jd_inode);
	struct gfs2_sbd *sdp = GFS2_SB(jd->jd_inode);
	int ar;
	int error;

	if (ip->i_di.di_size < (8 << 20) || ip->i_di.di_size > (1 << 30) ||
	    (ip->i_di.di_size & (sdp->sd_sb.sb_bsize - 1))) {
		gfs2_consist_inode(ip);
		return -EIO;
	}
	jd->jd_blocks = ip->i_di.di_size >> sdp->sd_sb.sb_bsize_shift;

	error = gfs2_write_alloc_required(ip, 0, ip->i_di.di_size, &ar);
	if (!error && ar) {
		gfs2_consist_inode(ip);
		error = -EIO;
	}

	return error;
}

/**
 * gfs2_make_fs_rw - Turn a Read-Only FS into a Read-Write one
 * @sdp: the filesystem
 *
 * Returns: errno
 */

int gfs2_make_fs_rw(struct gfs2_sbd *sdp)
{
	struct gfs2_inode *ip = GFS2_I(sdp->sd_jdesc->jd_inode);
	struct gfs2_glock *j_gl = ip->i_gl;
	struct gfs2_holder t_gh;
	struct gfs2_log_header_host head;
	int error;

	error = gfs2_glock_nq_init(sdp->sd_trans_gl, LM_ST_SHARED, 0, &t_gh);
	if (error)
		return error;

	j_gl->gl_ops->go_inval(j_gl, DIO_METADATA);

	error = gfs2_find_jhead(sdp->sd_jdesc, &head);
	if (error)
		goto fail;

	if (!(head.lh_flags & GFS2_LOG_HEAD_UNMOUNT)) {
		gfs2_consist(sdp);
		error = -EIO;
		goto fail;
	}

	/*  Initialize some head of the log stuff  */
	sdp->sd_log_sequence = head.lh_sequence + 1;
	gfs2_log_pointers_init(sdp, head.lh_blkno);

	error = gfs2_quota_init(sdp);
	if (error)
		goto fail;

	set_bit(SDF_JOURNAL_LIVE, &sdp->sd_flags);

	gfs2_glock_dq_uninit(&t_gh);

	return 0;

fail:
	t_gh.gh_flags |= GL_NOCACHE;
	gfs2_glock_dq_uninit(&t_gh);

	return error;
}

static void gfs2_statfs_change_in(struct gfs2_statfs_change_host *sc, const void *buf)
{
	const struct gfs2_statfs_change *str = buf;

	sc->sc_total = be64_to_cpu(str->sc_total);
	sc->sc_free = be64_to_cpu(str->sc_free);
	sc->sc_dinodes = be64_to_cpu(str->sc_dinodes);
}

static void gfs2_statfs_change_out(const struct gfs2_statfs_change_host *sc, void *buf)
{
	struct gfs2_statfs_change *str = buf;

	str->sc_total = cpu_to_be64(sc->sc_total);
	str->sc_free = cpu_to_be64(sc->sc_free);
	str->sc_dinodes = cpu_to_be64(sc->sc_dinodes);
}

int gfs2_statfs_init(struct gfs2_sbd *sdp)
{
	struct gfs2_inode *m_ip = GFS2_I(sdp->sd_statfs_inode);
	struct gfs2_statfs_change_host *m_sc = &sdp->sd_statfs_master;
	struct gfs2_inode *l_ip = GFS2_I(sdp->sd_sc_inode);
	struct gfs2_statfs_change_host *l_sc = &sdp->sd_statfs_local;
	struct buffer_head *m_bh, *l_bh;
	struct gfs2_holder gh;
	int error;

	error = gfs2_glock_nq_init(m_ip->i_gl, LM_ST_EXCLUSIVE, GL_NOCACHE,
				   &gh);
	if (error)
		return error;

	error = gfs2_meta_inode_buffer(m_ip, &m_bh);
	if (error)
		goto out;

	if (sdp->sd_args.ar_spectator) {
		spin_lock(&sdp->sd_statfs_spin);
		gfs2_statfs_change_in(m_sc, m_bh->b_data +
				      sizeof(struct gfs2_dinode));
		spin_unlock(&sdp->sd_statfs_spin);
	} else {
		error = gfs2_meta_inode_buffer(l_ip, &l_bh);
		if (error)
			goto out_m_bh;

		spin_lock(&sdp->sd_statfs_spin);
		gfs2_statfs_change_in(m_sc, m_bh->b_data +
				      sizeof(struct gfs2_dinode));
		gfs2_statfs_change_in(l_sc, l_bh->b_data +
				      sizeof(struct gfs2_dinode));
		spin_unlock(&sdp->sd_statfs_spin);

		brelse(l_bh);
	}

out_m_bh:
	brelse(m_bh);
out:
	gfs2_glock_dq_uninit(&gh);
	return 0;
}

void gfs2_statfs_change(struct gfs2_sbd *sdp, s64 total, s64 free,
			s64 dinodes)
{
	struct gfs2_inode *l_ip = GFS2_I(sdp->sd_sc_inode);
	struct gfs2_statfs_change_host *l_sc = &sdp->sd_statfs_local;
	struct buffer_head *l_bh;
	int error;

	error = gfs2_meta_inode_buffer(l_ip, &l_bh);
	if (error)
		return;

	gfs2_trans_add_bh(l_ip->i_gl, l_bh, 1);

	spin_lock(&sdp->sd_statfs_spin);
	l_sc->sc_total += total;
	l_sc->sc_free += free;
	l_sc->sc_dinodes += dinodes;
	gfs2_statfs_change_out(l_sc, l_bh->b_data + sizeof(struct gfs2_dinode));
	spin_unlock(&sdp->sd_statfs_spin);

	brelse(l_bh);
}

int gfs2_statfs_sync(struct gfs2_sbd *sdp)
{
	struct gfs2_inode *m_ip = GFS2_I(sdp->sd_statfs_inode);
	struct gfs2_inode *l_ip = GFS2_I(sdp->sd_sc_inode);
	struct gfs2_statfs_change_host *m_sc = &sdp->sd_statfs_master;
	struct gfs2_statfs_change_host *l_sc = &sdp->sd_statfs_local;
	struct gfs2_holder gh;
	struct buffer_head *m_bh, *l_bh;
	int error;

	error = gfs2_glock_nq_init(m_ip->i_gl, LM_ST_EXCLUSIVE, GL_NOCACHE,
				   &gh);
	if (error)
		return error;

	error = gfs2_meta_inode_buffer(m_ip, &m_bh);
	if (error)
		goto out;

	spin_lock(&sdp->sd_statfs_spin);
	gfs2_statfs_change_in(m_sc, m_bh->b_data +
			      sizeof(struct gfs2_dinode));
	if (!l_sc->sc_total && !l_sc->sc_free && !l_sc->sc_dinodes) {
		spin_unlock(&sdp->sd_statfs_spin);
		goto out_bh;
	}
	spin_unlock(&sdp->sd_statfs_spin);

	error = gfs2_meta_inode_buffer(l_ip, &l_bh);
	if (error)
		goto out_bh;

	error = gfs2_trans_begin(sdp, 2 * RES_DINODE, 0);
	if (error)
		goto out_bh2;

	gfs2_trans_add_bh(l_ip->i_gl, l_bh, 1);

	spin_lock(&sdp->sd_statfs_spin);
	m_sc->sc_total += l_sc->sc_total;
	m_sc->sc_free += l_sc->sc_free;
	m_sc->sc_dinodes += l_sc->sc_dinodes;
	memset(l_sc, 0, sizeof(struct gfs2_statfs_change));
	memset(l_bh->b_data + sizeof(struct gfs2_dinode),
	       0, sizeof(struct gfs2_statfs_change));
	spin_unlock(&sdp->sd_statfs_spin);

	gfs2_trans_add_bh(m_ip->i_gl, m_bh, 1);
	gfs2_statfs_change_out(m_sc, m_bh->b_data + sizeof(struct gfs2_dinode));

	gfs2_trans_end(sdp);

out_bh2:
	brelse(l_bh);
out_bh:
	brelse(m_bh);
out:
	gfs2_glock_dq_uninit(&gh);
	return error;
}

/**
 * gfs2_statfs_i - Do a statfs
 * @sdp: the filesystem
 * @sg: the sg structure
 *
 * Returns: errno
 */

int gfs2_statfs_i(struct gfs2_sbd *sdp, struct gfs2_statfs_change_host *sc)
{
	struct gfs2_statfs_change_host *m_sc = &sdp->sd_statfs_master;
	struct gfs2_statfs_change_host *l_sc = &sdp->sd_statfs_local;

	spin_lock(&sdp->sd_statfs_spin);

	*sc = *m_sc;
	sc->sc_total += l_sc->sc_total;
	sc->sc_free += l_sc->sc_free;
	sc->sc_dinodes += l_sc->sc_dinodes;

	spin_unlock(&sdp->sd_statfs_spin);

	if (sc->sc_free < 0)
		sc->sc_free = 0;
	if (sc->sc_free > sc->sc_total)
		sc->sc_free = sc->sc_total;
	if (sc->sc_dinodes < 0)
		sc->sc_dinodes = 0;

	return 0;
}

/**
 * statfs_fill - fill in the sg for a given RG
 * @rgd: the RG
 * @sc: the sc structure
 *
 * Returns: 0 on success, -ESTALE if the LVB is invalid
 */

static int statfs_slow_fill(struct gfs2_rgrpd *rgd,
			    struct gfs2_statfs_change_host *sc)
{
	gfs2_rgrp_verify(rgd);
	sc->sc_total += rgd->rd_data;
	sc->sc_free += rgd->rd_rg.rg_free;
	sc->sc_dinodes += rgd->rd_rg.rg_dinodes;
	return 0;
}

/**
 * gfs2_statfs_slow - Stat a filesystem using asynchronous locking
 * @sdp: the filesystem
 * @sc: the sc info that will be returned
 *
 * Any error (other than a signal) will cause this routine to fall back
 * to the synchronous version.
 *
 * FIXME: This really shouldn't busy wait like this.
 *
 * Returns: errno
 */

int gfs2_statfs_slow(struct gfs2_sbd *sdp, struct gfs2_statfs_change_host *sc)
{
	struct gfs2_holder ri_gh;
	struct gfs2_rgrpd *rgd_next;
	struct gfs2_holder *gha, *gh;
	unsigned int slots = 64;
	unsigned int x;
	int done;
	int error = 0, err;

	memset(sc, 0, sizeof(struct gfs2_statfs_change_host));
	gha = kcalloc(slots, sizeof(struct gfs2_holder), GFP_KERNEL);
	if (!gha)
		return -ENOMEM;

	error = gfs2_rindex_hold(sdp, &ri_gh);
	if (error)
		goto out;

	rgd_next = gfs2_rgrpd_get_first(sdp);

	for (;;) {
		done = 1;

		for (x = 0; x < slots; x++) {
			gh = gha + x;

			if (gh->gh_gl && gfs2_glock_poll(gh)) {
				err = gfs2_glock_wait(gh);
				if (err) {
					gfs2_holder_uninit(gh);
					error = err;
				} else {
					if (!error)
						error = statfs_slow_fill(
							gh->gh_gl->gl_object, sc);
					gfs2_glock_dq_uninit(gh);
				}
			}

			if (gh->gh_gl)
				done = 0;
			else if (rgd_next && !error) {
				error = gfs2_glock_nq_init(rgd_next->rd_gl,
							   LM_ST_SHARED,
							   GL_ASYNC,
							   gh);
				rgd_next = gfs2_rgrpd_get_next(rgd_next);
				done = 0;
			}

			if (signal_pending(current))
				error = -ERESTARTSYS;
		}

		if (done)
			break;

		yield();
	}

	gfs2_glock_dq_uninit(&ri_gh);

out:
	kfree(gha);
	return error;
}

struct lfcc {
	struct list_head list;
	struct gfs2_holder gh;
};

/**
 * gfs2_lock_fs_check_clean - Stop all writes to the FS and check that all
 *                            journals are clean
 * @sdp: the file system
 * @state: the state to put the transaction lock into
 * @t_gh: the hold on the transaction lock
 *
 * Returns: errno
 */

static int gfs2_lock_fs_check_clean(struct gfs2_sbd *sdp,
				    struct gfs2_holder *t_gh)
{
	struct gfs2_inode *ip;
	struct gfs2_holder ji_gh;
	struct gfs2_jdesc *jd;
	struct lfcc *lfcc;
	LIST_HEAD(list);
	struct gfs2_log_header_host lh;
	int error;

	error = gfs2_jindex_hold(sdp, &ji_gh);
	if (error)
		return error;

	list_for_each_entry(jd, &sdp->sd_jindex_list, jd_list) {
		lfcc = kmalloc(sizeof(struct lfcc), GFP_KERNEL);
		if (!lfcc) {
			error = -ENOMEM;
			goto out;
		}
		ip = GFS2_I(jd->jd_inode);
		error = gfs2_glock_nq_init(ip->i_gl, LM_ST_SHARED, 0, &lfcc->gh);
		if (error) {
			kfree(lfcc);
			goto out;
		}
		list_add(&lfcc->list, &list);
	}

	error = gfs2_glock_nq_init(sdp->sd_trans_gl, LM_ST_DEFERRED,
				   GL_NOCACHE, t_gh);

	list_for_each_entry(jd, &sdp->sd_jindex_list, jd_list) {
		error = gfs2_jdesc_check(jd);
		if (error)
			break;
		error = gfs2_find_jhead(jd, &lh);
		if (error)
			break;
		if (!(lh.lh_flags & GFS2_LOG_HEAD_UNMOUNT)) {
			error = -EBUSY;
			break;
		}
	}

	if (error)
		gfs2_glock_dq_uninit(t_gh);

out:
	while (!list_empty(&list)) {
		lfcc = list_entry(list.next, struct lfcc, list);
		list_del(&lfcc->list);
		gfs2_glock_dq_uninit(&lfcc->gh);
		kfree(lfcc);
	}
	gfs2_glock_dq_uninit(&ji_gh);
	return error;
}

/**
 * gfs2_freeze_fs - freezes the file system
 * @sdp: the file system
 *
 * This function flushes data and meta data for all machines by
 * aquiring the transaction log exclusively.  All journals are
 * ensured to be in a clean state as well.
 *
 * Returns: errno
 */

int gfs2_freeze_fs(struct gfs2_sbd *sdp)
{
	int error = 0;

	mutex_lock(&sdp->sd_freeze_lock);

	if (!sdp->sd_freeze_count++) {
		error = gfs2_lock_fs_check_clean(sdp, &sdp->sd_freeze_gh);
		if (error)
			sdp->sd_freeze_count--;
	}

	mutex_unlock(&sdp->sd_freeze_lock);

	return error;
}

/**
 * gfs2_unfreeze_fs - unfreezes the file system
 * @sdp: the file system
 *
 * This function allows the file system to proceed by unlocking
 * the exclusively held transaction lock.  Other GFS2 nodes are
 * now free to acquire the lock shared and go on with their lives.
 *
 */

void gfs2_unfreeze_fs(struct gfs2_sbd *sdp)
{
	mutex_lock(&sdp->sd_freeze_lock);

	if (sdp->sd_freeze_count && !--sdp->sd_freeze_count)
		gfs2_glock_dq_uninit(&sdp->sd_freeze_gh);

	mutex_unlock(&sdp->sd_freeze_lock);
}

