// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/crc32.h>
#include <linux/gfs2_ondisk.h>
#include <linux/uaccess.h>

#include "gfs2.h"
#include "incore.h"
#include "glock.h"
#include "lops.h"
#include "recovery.h"
#include "rgrp.h"
#include "super.h"
#include "util.h"

struct kmem_cache *gfs2_glock_cachep __read_mostly;
struct kmem_cache *gfs2_glock_aspace_cachep __read_mostly;
struct kmem_cache *gfs2_inode_cachep __read_mostly;
struct kmem_cache *gfs2_bufdata_cachep __read_mostly;
struct kmem_cache *gfs2_rgrpd_cachep __read_mostly;
struct kmem_cache *gfs2_quotad_cachep __read_mostly;
struct kmem_cache *gfs2_qadata_cachep __read_mostly;
mempool_t *gfs2_page_pool __read_mostly;

void gfs2_assert_i(struct gfs2_sbd *sdp)
{
	fs_emerg(sdp, "fatal assertion failed\n");
}

/**
 * check_journal_clean - Make sure a journal is clean for a spectator mount
 * @sdp: The GFS2 superblock
 * @jd: The journal descriptor
 *
 * Returns: 0 if the journal is clean or locked, else an error
 */
int check_journal_clean(struct gfs2_sbd *sdp, struct gfs2_jdesc *jd)
{
	int error;
	struct gfs2_holder j_gh;
	struct gfs2_log_header_host head;
	struct gfs2_inode *ip;

	ip = GFS2_I(jd->jd_inode);
	error = gfs2_glock_nq_init(ip->i_gl, LM_ST_SHARED, LM_FLAG_NOEXP |
				   GL_EXACT | GL_NOCACHE, &j_gh);
	if (error) {
		fs_err(sdp, "Error locking journal for spectator mount.\n");
		return -EPERM;
	}
	error = gfs2_jdesc_check(jd);
	if (error) {
		fs_err(sdp, "Error checking journal for spectator mount.\n");
		goto out_unlock;
	}
	error = gfs2_find_jhead(jd, &head, false);
	if (error) {
		fs_err(sdp, "Error parsing journal for spectator mount.\n");
		goto out_unlock;
	}
	if (!(head.lh_flags & GFS2_LOG_HEAD_UNMOUNT)) {
		error = -EPERM;
		fs_err(sdp, "jid=%u: Journal is dirty, so the first mounter "
		       "must not be a spectator.\n", jd->jd_jid);
	}

out_unlock:
	gfs2_glock_dq_uninit(&j_gh);
	return error;
}

void gfs2_lm(struct gfs2_sbd *sdp, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	if (sdp->sd_args.ar_errors == GFS2_ERRORS_WITHDRAW &&
	    test_bit(SDF_WITHDRAWN, &sdp->sd_flags))
		return;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;
	fs_err(sdp, "%pV", &vaf);
	va_end(args);
}

int gfs2_withdraw(struct gfs2_sbd *sdp)
{
	struct lm_lockstruct *ls = &sdp->sd_lockstruct;
	const struct lm_lockops *lm = ls->ls_ops;

	if (sdp->sd_args.ar_errors == GFS2_ERRORS_WITHDRAW &&
	    test_and_set_bit(SDF_WITHDRAWN, &sdp->sd_flags))
		return 0;

	if (sdp->sd_args.ar_errors == GFS2_ERRORS_WITHDRAW) {
		fs_err(sdp, "about to withdraw this file system\n");
		BUG_ON(sdp->sd_args.ar_debug);

		kobject_uevent(&sdp->sd_kobj, KOBJ_OFFLINE);

		if (!strcmp(sdp->sd_lockstruct.ls_ops->lm_proto_name, "lock_dlm"))
			wait_for_completion(&sdp->sd_wdack);

		if (lm->lm_unmount) {
			fs_err(sdp, "telling LM to unmount\n");
			lm->lm_unmount(sdp);
		}
		set_bit(SDF_SKIP_DLM_UNLOCK, &sdp->sd_flags);
		fs_err(sdp, "withdrawn\n");
		dump_stack();
	}

	if (sdp->sd_args.ar_errors == GFS2_ERRORS_PANIC)
		panic("GFS2: fsid=%s: panic requested\n", sdp->sd_fsname);

	return -1;
}

/**
 * gfs2_assert_withdraw_i - Cause the machine to withdraw if @assertion is false
 */

void gfs2_assert_withdraw_i(struct gfs2_sbd *sdp, char *assertion,
			    const char *function, char *file, unsigned int line)
{
	gfs2_lm(sdp,
		"fatal: assertion \"%s\" failed\n"
		"   function = %s, file = %s, line = %u\n",
		assertion, function, file, line);
	gfs2_withdraw(sdp);
	dump_stack();
}

/**
 * gfs2_assert_warn_i - Print a message to the console if @assertion is false
 */

void gfs2_assert_warn_i(struct gfs2_sbd *sdp, char *assertion,
			const char *function, char *file, unsigned int line)
{
	if (time_before(jiffies,
			sdp->sd_last_warning +
			gfs2_tune_get(sdp, gt_complain_secs) * HZ))
		return;

	if (sdp->sd_args.ar_errors == GFS2_ERRORS_WITHDRAW)
		fs_warn(sdp, "warning: assertion \"%s\" failed at function = %s, file = %s, line = %u\n",
			assertion, function, file, line);

	if (sdp->sd_args.ar_debug)
		BUG();
	else
		dump_stack();

	if (sdp->sd_args.ar_errors == GFS2_ERRORS_PANIC)
		panic("GFS2: fsid=%s: warning: assertion \"%s\" failed\n"
		      "GFS2: fsid=%s:   function = %s, file = %s, line = %u\n",
		      sdp->sd_fsname, assertion,
		      sdp->sd_fsname, function, file, line);

	sdp->sd_last_warning = jiffies;
}

/**
 * gfs2_consist_i - Flag a filesystem consistency error and withdraw
 */

void gfs2_consist_i(struct gfs2_sbd *sdp, const char *function,
		    char *file, unsigned int line)
{
	gfs2_lm(sdp,
		"fatal: filesystem consistency error - function = %s, file = %s, line = %u\n",
		function, file, line);
	gfs2_withdraw(sdp);
}

/**
 * gfs2_consist_inode_i - Flag an inode consistency error and withdraw
 */

void gfs2_consist_inode_i(struct gfs2_inode *ip,
			  const char *function, char *file, unsigned int line)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);

	gfs2_lm(sdp,
		"fatal: filesystem consistency error\n"
		"  inode = %llu %llu\n"
		"  function = %s, file = %s, line = %u\n",
		(unsigned long long)ip->i_no_formal_ino,
		(unsigned long long)ip->i_no_addr,
		function, file, line);
	gfs2_withdraw(sdp);
}

/**
 * gfs2_consist_rgrpd_i - Flag a RG consistency error and withdraw
 */

void gfs2_consist_rgrpd_i(struct gfs2_rgrpd *rgd,
			  const char *function, char *file, unsigned int line)
{
	struct gfs2_sbd *sdp = rgd->rd_sbd;
	char fs_id_buf[sizeof(sdp->sd_fsname) + 7];

	sprintf(fs_id_buf, "fsid=%s: ", sdp->sd_fsname);
	gfs2_rgrp_dump(NULL, rgd->rd_gl, fs_id_buf);
	gfs2_lm(sdp,
		"fatal: filesystem consistency error\n"
		"  RG = %llu\n"
		"  function = %s, file = %s, line = %u\n",
		(unsigned long long)rgd->rd_addr,
		function, file, line);
	gfs2_withdraw(sdp);
}

/**
 * gfs2_meta_check_ii - Flag a magic number consistency error and withdraw
 * Returns: -1 if this call withdrew the machine,
 *          -2 if it was already withdrawn
 */

int gfs2_meta_check_ii(struct gfs2_sbd *sdp, struct buffer_head *bh,
		       const char *type, const char *function, char *file,
		       unsigned int line)
{
	int me;

	gfs2_lm(sdp,
		"fatal: invalid metadata block\n"
		"  bh = %llu (%s)\n"
		"  function = %s, file = %s, line = %u\n",
		(unsigned long long)bh->b_blocknr, type,
		function, file, line);
	me = gfs2_withdraw(sdp);
	return (me) ? -1 : -2;
}

/**
 * gfs2_metatype_check_ii - Flag a metadata type consistency error and withdraw
 * Returns: -1 if this call withdrew the machine,
 *          -2 if it was already withdrawn
 */

int gfs2_metatype_check_ii(struct gfs2_sbd *sdp, struct buffer_head *bh,
			   u16 type, u16 t, const char *function,
			   char *file, unsigned int line)
{
	int me;

	gfs2_lm(sdp,
		"fatal: invalid metadata block\n"
		"  bh = %llu (type: exp=%u, found=%u)\n"
		"  function = %s, file = %s, line = %u\n",
		(unsigned long long)bh->b_blocknr, type, t,
		function, file, line);
	me = gfs2_withdraw(sdp);
	return (me) ? -1 : -2;
}

/**
 * gfs2_io_error_i - Flag an I/O error and withdraw
 * Returns: -1 if this call withdrew the machine,
 *          0 if it was already withdrawn
 */

int gfs2_io_error_i(struct gfs2_sbd *sdp, const char *function, char *file,
		    unsigned int line)
{
	gfs2_lm(sdp,
		"fatal: I/O error\n"
		"  function = %s, file = %s, line = %u\n",
		function, file, line);
	return gfs2_withdraw(sdp);
}

/**
 * gfs2_io_error_bh_i - Flag a buffer I/O error
 * @withdraw: withdraw the filesystem
 */

void gfs2_io_error_bh_i(struct gfs2_sbd *sdp, struct buffer_head *bh,
			const char *function, char *file, unsigned int line,
			bool withdraw)
{
	if (gfs2_withdrawn(sdp))
		return;

	fs_err(sdp, "fatal: I/O error\n"
	       "  block = %llu\n"
	       "  function = %s, file = %s, line = %u\n",
	       (unsigned long long)bh->b_blocknr, function, file, line);
	if (withdraw)
		gfs2_withdraw(sdp);
}

