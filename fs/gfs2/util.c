/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/crc32.h>
#include <linux/gfs2_ondisk.h>
#include <linux/lm_interface.h>
#include <asm/uaccess.h>

#include "gfs2.h"
#include "incore.h"
#include "glock.h"
#include "util.h"

struct kmem_cache *gfs2_glock_cachep __read_mostly;
struct kmem_cache *gfs2_inode_cachep __read_mostly;
struct kmem_cache *gfs2_bufdata_cachep __read_mostly;
struct kmem_cache *gfs2_rgrpd_cachep __read_mostly;
struct kmem_cache *gfs2_quotad_cachep __read_mostly;

void gfs2_assert_i(struct gfs2_sbd *sdp)
{
	printk(KERN_EMERG "GFS2: fsid=%s: fatal assertion failed\n",
	       sdp->sd_fsname);
}

int gfs2_lm_withdraw(struct gfs2_sbd *sdp, char *fmt, ...)
{
	va_list args;

	if (test_and_set_bit(SDF_SHUTDOWN, &sdp->sd_flags))
		return 0;

	va_start(args, fmt);
	vprintk(fmt, args);
	va_end(args);

	fs_err(sdp, "about to withdraw this file system\n");
	BUG_ON(sdp->sd_args.ar_debug);

	fs_err(sdp, "telling LM to withdraw\n");
	gfs2_withdraw_lockproto(&sdp->sd_lockstruct);
	fs_err(sdp, "withdrawn\n");
	dump_stack();

	return -1;
}

/**
 * gfs2_assert_withdraw_i - Cause the machine to withdraw if @assertion is false
 * Returns: -1 if this call withdrew the machine,
 *          -2 if it was already withdrawn
 */

int gfs2_assert_withdraw_i(struct gfs2_sbd *sdp, char *assertion,
			   const char *function, char *file, unsigned int line)
{
	int me;
	me = gfs2_lm_withdraw(sdp,
		"GFS2: fsid=%s: fatal: assertion \"%s\" failed\n"
		"GFS2: fsid=%s:   function = %s, file = %s, line = %u\n",
		sdp->sd_fsname, assertion,
		sdp->sd_fsname, function, file, line);
	dump_stack();
	return (me) ? -1 : -2;
}

/**
 * gfs2_assert_warn_i - Print a message to the console if @assertion is false
 * Returns: -1 if we printed something
 *          -2 if we didn't
 */

int gfs2_assert_warn_i(struct gfs2_sbd *sdp, char *assertion,
		       const char *function, char *file, unsigned int line)
{
	if (time_before(jiffies,
			sdp->sd_last_warning +
			gfs2_tune_get(sdp, gt_complain_secs) * HZ))
		return -2;

	printk(KERN_WARNING
	       "GFS2: fsid=%s: warning: assertion \"%s\" failed\n"
	       "GFS2: fsid=%s:   function = %s, file = %s, line = %u\n",
	       sdp->sd_fsname, assertion,
	       sdp->sd_fsname, function, file, line);

	if (sdp->sd_args.ar_debug)
		BUG();
	else
		dump_stack();

	sdp->sd_last_warning = jiffies;

	return -1;
}

/**
 * gfs2_consist_i - Flag a filesystem consistency error and withdraw
 * Returns: -1 if this call withdrew the machine,
 *          0 if it was already withdrawn
 */

int gfs2_consist_i(struct gfs2_sbd *sdp, int cluster_wide, const char *function,
		   char *file, unsigned int line)
{
	int rv;
	rv = gfs2_lm_withdraw(sdp,
		"GFS2: fsid=%s: fatal: filesystem consistency error\n"
		"GFS2: fsid=%s:   function = %s, file = %s, line = %u\n",
		sdp->sd_fsname,
		sdp->sd_fsname, function, file, line);
	return rv;
}

/**
 * gfs2_consist_inode_i - Flag an inode consistency error and withdraw
 * Returns: -1 if this call withdrew the machine,
 *          0 if it was already withdrawn
 */

int gfs2_consist_inode_i(struct gfs2_inode *ip, int cluster_wide,
			 const char *function, char *file, unsigned int line)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	int rv;
	rv = gfs2_lm_withdraw(sdp,
		"GFS2: fsid=%s: fatal: filesystem consistency error\n"
		"GFS2: fsid=%s:   inode = %llu %llu\n"
		"GFS2: fsid=%s:   function = %s, file = %s, line = %u\n",
		sdp->sd_fsname,
		sdp->sd_fsname, (unsigned long long)ip->i_no_formal_ino,
		(unsigned long long)ip->i_no_addr,
		sdp->sd_fsname, function, file, line);
	return rv;
}

/**
 * gfs2_consist_rgrpd_i - Flag a RG consistency error and withdraw
 * Returns: -1 if this call withdrew the machine,
 *          0 if it was already withdrawn
 */

int gfs2_consist_rgrpd_i(struct gfs2_rgrpd *rgd, int cluster_wide,
			 const char *function, char *file, unsigned int line)
{
	struct gfs2_sbd *sdp = rgd->rd_sbd;
	int rv;
	rv = gfs2_lm_withdraw(sdp,
		"GFS2: fsid=%s: fatal: filesystem consistency error\n"
		"GFS2: fsid=%s:   RG = %llu\n"
		"GFS2: fsid=%s:   function = %s, file = %s, line = %u\n",
		sdp->sd_fsname,
		sdp->sd_fsname, (unsigned long long)rgd->rd_addr,
		sdp->sd_fsname, function, file, line);
	return rv;
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
	me = gfs2_lm_withdraw(sdp,
		"GFS2: fsid=%s: fatal: invalid metadata block\n"
		"GFS2: fsid=%s:   bh = %llu (%s)\n"
		"GFS2: fsid=%s:   function = %s, file = %s, line = %u\n",
		sdp->sd_fsname,
		sdp->sd_fsname, (unsigned long long)bh->b_blocknr, type,
		sdp->sd_fsname, function, file, line);
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
	me = gfs2_lm_withdraw(sdp,
		"GFS2: fsid=%s: fatal: invalid metadata block\n"
		"GFS2: fsid=%s:   bh = %llu (type: exp=%u, found=%u)\n"
		"GFS2: fsid=%s:   function = %s, file = %s, line = %u\n",
		sdp->sd_fsname,
		sdp->sd_fsname, (unsigned long long)bh->b_blocknr, type, t,
		sdp->sd_fsname, function, file, line);
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
	int rv;
	rv = gfs2_lm_withdraw(sdp,
		"GFS2: fsid=%s: fatal: I/O error\n"
		"GFS2: fsid=%s:   function = %s, file = %s, line = %u\n",
		sdp->sd_fsname,
		sdp->sd_fsname, function, file, line);
	return rv;
}

/**
 * gfs2_io_error_bh_i - Flag a buffer I/O error and withdraw
 * Returns: -1 if this call withdrew the machine,
 *          0 if it was already withdrawn
 */

int gfs2_io_error_bh_i(struct gfs2_sbd *sdp, struct buffer_head *bh,
		       const char *function, char *file, unsigned int line)
{
	int rv;
	rv = gfs2_lm_withdraw(sdp,
		"GFS2: fsid=%s: fatal: I/O error\n"
		"GFS2: fsid=%s:   block = %llu\n"
		"GFS2: fsid=%s:   function = %s, file = %s, line = %u\n",
		sdp->sd_fsname,
		sdp->sd_fsname, (unsigned long long)bh->b_blocknr,
		sdp->sd_fsname, function, file, line);
	return rv;
}

void gfs2_icbit_munge(struct gfs2_sbd *sdp, unsigned char **bitmap,
		      unsigned int bit, int new_value)
{
	unsigned int c, o, b = bit;
	int old_value;

	c = b / (8 * PAGE_SIZE);
	b %= 8 * PAGE_SIZE;
	o = b / 8;
	b %= 8;

	old_value = (bitmap[c][o] & (1 << b));
	gfs2_assert_withdraw(sdp, !old_value != !new_value);

	if (new_value)
		bitmap[c][o] |= 1 << b;
	else
		bitmap[c][o] &= ~(1 << b);
}

