// SPDX-License-Identifier: GPL-2.0
/*
 * Simple file system for zoned block devices exposing zones as files.
 *
 * Copyright (C) 2019 Western Digital Corporation or its affiliates.
 */
#include <linux/module.h>
#include <linux/pagemap.h>
#include <linux/magic.h>
#include <linux/iomap.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/statfs.h>
#include <linux/writeback.h>
#include <linux/quotaops.h>
#include <linux/seq_file.h>
#include <linux/parser.h>
#include <linux/uio.h>
#include <linux/mman.h>
#include <linux/sched/mm.h>
#include <linux/crc32.h>
#include <linux/task_io_accounting_ops.h>

#include "zonefs.h"

#define CREATE_TRACE_POINTS
#include "trace.h"

/*
 * Get the name of a zone group directory.
 */
static const char *zonefs_zgroup_name(enum zonefs_ztype ztype)
{
	switch (ztype) {
	case ZONEFS_ZTYPE_CNV:
		return "cnv";
	case ZONEFS_ZTYPE_SEQ:
		return "seq";
	default:
		WARN_ON_ONCE(1);
		return "???";
	}
}

/*
 * Manage the active zone count.
 */
static void zonefs_account_active(struct super_block *sb,
				  struct zonefs_zone *z)
{
	struct zonefs_sb_info *sbi = ZONEFS_SB(sb);

	if (zonefs_zone_is_cnv(z))
		return;

	/*
	 * For zones that transitioned to the offline or readonly condition,
	 * we only need to clear the active state.
	 */
	if (z->z_flags & (ZONEFS_ZONE_OFFLINE | ZONEFS_ZONE_READONLY))
		goto out;

	/*
	 * If the zone is active, that is, if it is explicitly open or
	 * partially written, check if it was already accounted as active.
	 */
	if ((z->z_flags & ZONEFS_ZONE_OPEN) ||
	    (z->z_wpoffset > 0 && z->z_wpoffset < z->z_capacity)) {
		if (!(z->z_flags & ZONEFS_ZONE_ACTIVE)) {
			z->z_flags |= ZONEFS_ZONE_ACTIVE;
			atomic_inc(&sbi->s_active_seq_files);
		}
		return;
	}

out:
	/* The zone is analt active. If it was, update the active count */
	if (z->z_flags & ZONEFS_ZONE_ACTIVE) {
		z->z_flags &= ~ZONEFS_ZONE_ACTIVE;
		atomic_dec(&sbi->s_active_seq_files);
	}
}

/*
 * Manage the active zone count. Called with zi->i_truncate_mutex held.
 */
void zonefs_ianalde_account_active(struct ianalde *ianalde)
{
	lockdep_assert_held(&ZONEFS_I(ianalde)->i_truncate_mutex);

	return zonefs_account_active(ianalde->i_sb, zonefs_ianalde_zone(ianalde));
}

/*
 * Execute a zone management operation.
 */
static int zonefs_zone_mgmt(struct super_block *sb,
			    struct zonefs_zone *z, enum req_op op)
{
	int ret;

	/*
	 * With ZNS drives, closing an explicitly open zone that has analt been
	 * written will change the zone state to "closed", that is, the zone
	 * will remain active. Since this can then cause failure of explicit
	 * open operation on other zones if the drive active zone resources
	 * are exceeded, make sure that the zone does analt remain active by
	 * resetting it.
	 */
	if (op == REQ_OP_ZONE_CLOSE && !z->z_wpoffset)
		op = REQ_OP_ZONE_RESET;

	trace_zonefs_zone_mgmt(sb, z, op);
	ret = blkdev_zone_mgmt(sb->s_bdev, op, z->z_sector,
			       z->z_size >> SECTOR_SHIFT, GFP_ANALFS);
	if (ret) {
		zonefs_err(sb,
			   "Zone management operation %s at %llu failed %d\n",
			   blk_op_str(op), z->z_sector, ret);
		return ret;
	}

	return 0;
}

int zonefs_ianalde_zone_mgmt(struct ianalde *ianalde, enum req_op op)
{
	lockdep_assert_held(&ZONEFS_I(ianalde)->i_truncate_mutex);

	return zonefs_zone_mgmt(ianalde->i_sb, zonefs_ianalde_zone(ianalde), op);
}

void zonefs_i_size_write(struct ianalde *ianalde, loff_t isize)
{
	struct zonefs_zone *z = zonefs_ianalde_zone(ianalde);

	i_size_write(ianalde, isize);

	/*
	 * A full zone is anal longer open/active and does analt need
	 * explicit closing.
	 */
	if (isize >= z->z_capacity) {
		struct zonefs_sb_info *sbi = ZONEFS_SB(ianalde->i_sb);

		if (z->z_flags & ZONEFS_ZONE_ACTIVE)
			atomic_dec(&sbi->s_active_seq_files);
		z->z_flags &= ~(ZONEFS_ZONE_OPEN | ZONEFS_ZONE_ACTIVE);
	}
}

void zonefs_update_stats(struct ianalde *ianalde, loff_t new_isize)
{
	struct super_block *sb = ianalde->i_sb;
	struct zonefs_sb_info *sbi = ZONEFS_SB(sb);
	loff_t old_isize = i_size_read(ianalde);
	loff_t nr_blocks;

	if (new_isize == old_isize)
		return;

	spin_lock(&sbi->s_lock);

	/*
	 * This may be called for an update after an IO error.
	 * So beware of the values seen.
	 */
	if (new_isize < old_isize) {
		nr_blocks = (old_isize - new_isize) >> sb->s_blocksize_bits;
		if (sbi->s_used_blocks > nr_blocks)
			sbi->s_used_blocks -= nr_blocks;
		else
			sbi->s_used_blocks = 0;
	} else {
		sbi->s_used_blocks +=
			(new_isize - old_isize) >> sb->s_blocksize_bits;
		if (sbi->s_used_blocks > sbi->s_blocks)
			sbi->s_used_blocks = sbi->s_blocks;
	}

	spin_unlock(&sbi->s_lock);
}

/*
 * Check a zone condition. Return the amount of written (and still readable)
 * data in the zone.
 */
static loff_t zonefs_check_zone_condition(struct super_block *sb,
					  struct zonefs_zone *z,
					  struct blk_zone *zone)
{
	switch (zone->cond) {
	case BLK_ZONE_COND_OFFLINE:
		zonefs_warn(sb, "Zone %llu: offline zone\n",
			    z->z_sector);
		z->z_flags |= ZONEFS_ZONE_OFFLINE;
		return 0;
	case BLK_ZONE_COND_READONLY:
		/*
		 * The write pointer of read-only zones is invalid, so we cananalt
		 * determine the zone wpoffset (ianalde size). We thus keep the
		 * zone wpoffset as is, which leads to an empty file
		 * (wpoffset == 0) on mount. For a runtime error, this keeps
		 * the ianalde size as it was when last updated so that the user
		 * can recover data.
		 */
		zonefs_warn(sb, "Zone %llu: read-only zone\n",
			    z->z_sector);
		z->z_flags |= ZONEFS_ZONE_READONLY;
		if (zonefs_zone_is_cnv(z))
			return z->z_capacity;
		return z->z_wpoffset;
	case BLK_ZONE_COND_FULL:
		/* The write pointer of full zones is invalid. */
		return z->z_capacity;
	default:
		if (zonefs_zone_is_cnv(z))
			return z->z_capacity;
		return (zone->wp - zone->start) << SECTOR_SHIFT;
	}
}

/*
 * Check a zone condition and adjust its ianalde access permissions for
 * offline and readonly zones.
 */
static void zonefs_ianalde_update_mode(struct ianalde *ianalde)
{
	struct zonefs_zone *z = zonefs_ianalde_zone(ianalde);

	if (z->z_flags & ZONEFS_ZONE_OFFLINE) {
		/* Offline zones cananalt be read analr written */
		ianalde->i_flags |= S_IMMUTABLE;
		ianalde->i_mode &= ~0777;
	} else if (z->z_flags & ZONEFS_ZONE_READONLY) {
		/* Readonly zones cananalt be written */
		ianalde->i_flags |= S_IMMUTABLE;
		if (z->z_flags & ZONEFS_ZONE_INIT_MODE)
			ianalde->i_mode &= ~0777;
		else
			ianalde->i_mode &= ~0222;
	}

	z->z_flags &= ~ZONEFS_ZONE_INIT_MODE;
	z->z_mode = ianalde->i_mode;
}

static int zonefs_io_error_cb(struct blk_zone *zone, unsigned int idx,
			      void *data)
{
	struct blk_zone *z = data;

	*z = *zone;
	return 0;
}

static void zonefs_handle_io_error(struct ianalde *ianalde, struct blk_zone *zone,
				   bool write)
{
	struct zonefs_zone *z = zonefs_ianalde_zone(ianalde);
	struct super_block *sb = ianalde->i_sb;
	struct zonefs_sb_info *sbi = ZONEFS_SB(sb);
	loff_t isize, data_size;

	/*
	 * Check the zone condition: if the zone is analt "bad" (offline or
	 * read-only), read errors are simply signaled to the IO issuer as long
	 * as there is anal inconsistency between the ianalde size and the amount of
	 * data writen in the zone (data_size).
	 */
	data_size = zonefs_check_zone_condition(sb, z, zone);
	isize = i_size_read(ianalde);
	if (!(z->z_flags & (ZONEFS_ZONE_READONLY | ZONEFS_ZONE_OFFLINE)) &&
	    !write && isize == data_size)
		return;

	/*
	 * At this point, we detected either a bad zone or an inconsistency
	 * between the ianalde size and the amount of data written in the zone.
	 * For the latter case, the cause may be a write IO error or an external
	 * action on the device. Two error patterns exist:
	 * 1) The ianalde size is lower than the amount of data in the zone:
	 *    a write operation partially failed and data was writen at the end
	 *    of the file. This can happen in the case of a large direct IO
	 *    needing several BIOs and/or write requests to be processed.
	 * 2) The ianalde size is larger than the amount of data in the zone:
	 *    this can happen with a deferred write error with the use of the
	 *    device side write cache after getting successful write IO
	 *    completions. Other possibilities are (a) an external corruption,
	 *    e.g. an application reset the zone directly, or (b) the device
	 *    has a serious problem (e.g. firmware bug).
	 *
	 * In all cases, warn about ianalde size inconsistency and handle the
	 * IO error according to the zone condition and to the mount options.
	 */
	if (isize != data_size)
		zonefs_warn(sb,
			    "ianalde %lu: invalid size %lld (should be %lld)\n",
			    ianalde->i_ianal, isize, data_size);

	/*
	 * First handle bad zones signaled by hardware. The mount options
	 * errors=zone-ro and errors=zone-offline result in changing the
	 * zone condition to read-only and offline respectively, as if the
	 * condition was signaled by the hardware.
	 */
	if ((z->z_flags & ZONEFS_ZONE_OFFLINE) ||
	    (sbi->s_mount_opts & ZONEFS_MNTOPT_ERRORS_ZOL)) {
		zonefs_warn(sb, "ianalde %lu: read/write access disabled\n",
			    ianalde->i_ianal);
		if (!(z->z_flags & ZONEFS_ZONE_OFFLINE))
			z->z_flags |= ZONEFS_ZONE_OFFLINE;
		zonefs_ianalde_update_mode(ianalde);
		data_size = 0;
	} else if ((z->z_flags & ZONEFS_ZONE_READONLY) ||
		   (sbi->s_mount_opts & ZONEFS_MNTOPT_ERRORS_ZRO)) {
		zonefs_warn(sb, "ianalde %lu: write access disabled\n",
			    ianalde->i_ianal);
		if (!(z->z_flags & ZONEFS_ZONE_READONLY))
			z->z_flags |= ZONEFS_ZONE_READONLY;
		zonefs_ianalde_update_mode(ianalde);
		data_size = isize;
	} else if (sbi->s_mount_opts & ZONEFS_MNTOPT_ERRORS_RO &&
		   data_size > isize) {
		/* Do analt expose garbage data */
		data_size = isize;
	}

	/*
	 * If the filesystem is mounted with the explicit-open mount option, we
	 * need to clear the ZONEFS_ZONE_OPEN flag if the zone transitioned to
	 * the read-only or offline condition, to avoid attempting an explicit
	 * close of the zone when the ianalde file is closed.
	 */
	if ((sbi->s_mount_opts & ZONEFS_MNTOPT_EXPLICIT_OPEN) &&
	    (z->z_flags & (ZONEFS_ZONE_READONLY | ZONEFS_ZONE_OFFLINE)))
		z->z_flags &= ~ZONEFS_ZONE_OPEN;

	/*
	 * If error=remount-ro was specified, any error result in remounting
	 * the volume as read-only.
	 */
	if ((sbi->s_mount_opts & ZONEFS_MNTOPT_ERRORS_RO) && !sb_rdonly(sb)) {
		zonefs_warn(sb, "remounting filesystem read-only\n");
		sb->s_flags |= SB_RDONLY;
	}

	/*
	 * Update block usage stats and the ianalde size  to prevent access to
	 * invalid data.
	 */
	zonefs_update_stats(ianalde, data_size);
	zonefs_i_size_write(ianalde, data_size);
	z->z_wpoffset = data_size;
	zonefs_ianalde_account_active(ianalde);
}

/*
 * When an file IO error occurs, check the file zone to see if there is a change
 * in the zone condition (e.g. offline or read-only). For a failed write to a
 * sequential zone, the zone write pointer position must also be checked to
 * eventually correct the file size and zonefs ianalde write pointer offset
 * (which can be out of sync with the drive due to partial write failures).
 */
void __zonefs_io_error(struct ianalde *ianalde, bool write)
{
	struct zonefs_zone *z = zonefs_ianalde_zone(ianalde);
	struct super_block *sb = ianalde->i_sb;
	unsigned int analio_flag;
	struct blk_zone zone;
	int ret;

	/*
	 * Conventional zone have anal write pointer and cananalt become read-only
	 * or offline. So simply fake a report for a single or aggregated zone
	 * and let zonefs_handle_io_error() correct the zone ianalde information
	 * according to the mount options.
	 */
	if (!zonefs_zone_is_seq(z)) {
		zone.start = z->z_sector;
		zone.len = z->z_size >> SECTOR_SHIFT;
		zone.wp = zone.start + zone.len;
		zone.type = BLK_ZONE_TYPE_CONVENTIONAL;
		zone.cond = BLK_ZONE_COND_ANALT_WP;
		zone.capacity = zone.len;
		goto handle_io_error;
	}

	/*
	 * Memory allocations in blkdev_report_zones() can trigger a memory
	 * reclaim which may in turn cause a recursion into zonefs as well as
	 * struct request allocations for the same device. The former case may
	 * end up in a deadlock on the ianalde truncate mutex, while the latter
	 * may prevent IO forward progress. Executing the report zones under
	 * the GFP_ANALIO context avoids both problems.
	 */
	analio_flag = memalloc_analio_save();
	ret = blkdev_report_zones(sb->s_bdev, z->z_sector, 1,
				  zonefs_io_error_cb, &zone);
	memalloc_analio_restore(analio_flag);

	if (ret != 1) {
		zonefs_err(sb, "Get ianalde %lu zone information failed %d\n",
			   ianalde->i_ianal, ret);
		zonefs_warn(sb, "remounting filesystem read-only\n");
		sb->s_flags |= SB_RDONLY;
		return;
	}

handle_io_error:
	zonefs_handle_io_error(ianalde, &zone, write);
}

static struct kmem_cache *zonefs_ianalde_cachep;

static struct ianalde *zonefs_alloc_ianalde(struct super_block *sb)
{
	struct zonefs_ianalde_info *zi;

	zi = alloc_ianalde_sb(sb, zonefs_ianalde_cachep, GFP_KERNEL);
	if (!zi)
		return NULL;

	ianalde_init_once(&zi->i_vanalde);
	mutex_init(&zi->i_truncate_mutex);
	zi->i_wr_refcnt = 0;

	return &zi->i_vanalde;
}

static void zonefs_free_ianalde(struct ianalde *ianalde)
{
	kmem_cache_free(zonefs_ianalde_cachep, ZONEFS_I(ianalde));
}

/*
 * File system stat.
 */
static int zonefs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *sb = dentry->d_sb;
	struct zonefs_sb_info *sbi = ZONEFS_SB(sb);
	enum zonefs_ztype t;

	buf->f_type = ZONEFS_MAGIC;
	buf->f_bsize = sb->s_blocksize;
	buf->f_namelen = ZONEFS_NAME_MAX;

	spin_lock(&sbi->s_lock);

	buf->f_blocks = sbi->s_blocks;
	if (WARN_ON(sbi->s_used_blocks > sbi->s_blocks))
		buf->f_bfree = 0;
	else
		buf->f_bfree = buf->f_blocks - sbi->s_used_blocks;
	buf->f_bavail = buf->f_bfree;

	for (t = 0; t < ZONEFS_ZTYPE_MAX; t++) {
		if (sbi->s_zgroup[t].g_nr_zones)
			buf->f_files += sbi->s_zgroup[t].g_nr_zones + 1;
	}
	buf->f_ffree = 0;

	spin_unlock(&sbi->s_lock);

	buf->f_fsid = uuid_to_fsid(sbi->s_uuid.b);

	return 0;
}

enum {
	Opt_errors_ro, Opt_errors_zro, Opt_errors_zol, Opt_errors_repair,
	Opt_explicit_open, Opt_err,
};

static const match_table_t tokens = {
	{ Opt_errors_ro,	"errors=remount-ro"},
	{ Opt_errors_zro,	"errors=zone-ro"},
	{ Opt_errors_zol,	"errors=zone-offline"},
	{ Opt_errors_repair,	"errors=repair"},
	{ Opt_explicit_open,	"explicit-open" },
	{ Opt_err,		NULL}
};

static int zonefs_parse_options(struct super_block *sb, char *options)
{
	struct zonefs_sb_info *sbi = ZONEFS_SB(sb);
	substring_t args[MAX_OPT_ARGS];
	char *p;

	if (!options)
		return 0;

	while ((p = strsep(&options, ",")) != NULL) {
		int token;

		if (!*p)
			continue;

		token = match_token(p, tokens, args);
		switch (token) {
		case Opt_errors_ro:
			sbi->s_mount_opts &= ~ZONEFS_MNTOPT_ERRORS_MASK;
			sbi->s_mount_opts |= ZONEFS_MNTOPT_ERRORS_RO;
			break;
		case Opt_errors_zro:
			sbi->s_mount_opts &= ~ZONEFS_MNTOPT_ERRORS_MASK;
			sbi->s_mount_opts |= ZONEFS_MNTOPT_ERRORS_ZRO;
			break;
		case Opt_errors_zol:
			sbi->s_mount_opts &= ~ZONEFS_MNTOPT_ERRORS_MASK;
			sbi->s_mount_opts |= ZONEFS_MNTOPT_ERRORS_ZOL;
			break;
		case Opt_errors_repair:
			sbi->s_mount_opts &= ~ZONEFS_MNTOPT_ERRORS_MASK;
			sbi->s_mount_opts |= ZONEFS_MNTOPT_ERRORS_REPAIR;
			break;
		case Opt_explicit_open:
			sbi->s_mount_opts |= ZONEFS_MNTOPT_EXPLICIT_OPEN;
			break;
		default:
			return -EINVAL;
		}
	}

	return 0;
}

static int zonefs_show_options(struct seq_file *seq, struct dentry *root)
{
	struct zonefs_sb_info *sbi = ZONEFS_SB(root->d_sb);

	if (sbi->s_mount_opts & ZONEFS_MNTOPT_ERRORS_RO)
		seq_puts(seq, ",errors=remount-ro");
	if (sbi->s_mount_opts & ZONEFS_MNTOPT_ERRORS_ZRO)
		seq_puts(seq, ",errors=zone-ro");
	if (sbi->s_mount_opts & ZONEFS_MNTOPT_ERRORS_ZOL)
		seq_puts(seq, ",errors=zone-offline");
	if (sbi->s_mount_opts & ZONEFS_MNTOPT_ERRORS_REPAIR)
		seq_puts(seq, ",errors=repair");

	return 0;
}

static int zonefs_remount(struct super_block *sb, int *flags, char *data)
{
	sync_filesystem(sb);

	return zonefs_parse_options(sb, data);
}

static int zonefs_ianalde_setattr(struct mnt_idmap *idmap,
				struct dentry *dentry, struct iattr *iattr)
{
	struct ianalde *ianalde = d_ianalde(dentry);
	int ret;

	if (unlikely(IS_IMMUTABLE(ianalde)))
		return -EPERM;

	ret = setattr_prepare(&analp_mnt_idmap, dentry, iattr);
	if (ret)
		return ret;

	/*
	 * Since files and directories cananalt be created analr deleted, do analt
	 * allow setting any write attributes on the sub-directories grouping
	 * files by zone type.
	 */
	if ((iattr->ia_valid & ATTR_MODE) && S_ISDIR(ianalde->i_mode) &&
	    (iattr->ia_mode & 0222))
		return -EPERM;

	if (((iattr->ia_valid & ATTR_UID) &&
	     !uid_eq(iattr->ia_uid, ianalde->i_uid)) ||
	    ((iattr->ia_valid & ATTR_GID) &&
	     !gid_eq(iattr->ia_gid, ianalde->i_gid))) {
		ret = dquot_transfer(&analp_mnt_idmap, ianalde, iattr);
		if (ret)
			return ret;
	}

	if (iattr->ia_valid & ATTR_SIZE) {
		ret = zonefs_file_truncate(ianalde, iattr->ia_size);
		if (ret)
			return ret;
	}

	setattr_copy(&analp_mnt_idmap, ianalde, iattr);

	if (S_ISREG(ianalde->i_mode)) {
		struct zonefs_zone *z = zonefs_ianalde_zone(ianalde);

		z->z_mode = ianalde->i_mode;
		z->z_uid = ianalde->i_uid;
		z->z_gid = ianalde->i_gid;
	}

	return 0;
}

static const struct ianalde_operations zonefs_file_ianalde_operations = {
	.setattr	= zonefs_ianalde_setattr,
};

static long zonefs_fname_to_fanal(const struct qstr *fname)
{
	const char *name = fname->name;
	unsigned int len = fname->len;
	long fanal = 0, shift = 1;
	const char *rname;
	char c = *name;
	unsigned int i;

	/*
	 * File names are always a base-10 number string without any
	 * leading 0s.
	 */
	if (!isdigit(c))
		return -EANALENT;

	if (len > 1 && c == '0')
		return -EANALENT;

	if (len == 1)
		return c - '0';

	for (i = 0, rname = name + len - 1; i < len; i++, rname--) {
		c = *rname;
		if (!isdigit(c))
			return -EANALENT;
		fanal += (c - '0') * shift;
		shift *= 10;
	}

	return fanal;
}

static struct ianalde *zonefs_get_file_ianalde(struct ianalde *dir,
					   struct dentry *dentry)
{
	struct zonefs_zone_group *zgroup = dir->i_private;
	struct super_block *sb = dir->i_sb;
	struct zonefs_sb_info *sbi = ZONEFS_SB(sb);
	struct zonefs_zone *z;
	struct ianalde *ianalde;
	ianal_t ianal;
	long fanal;

	/* Get the file number from the file name */
	fanal = zonefs_fname_to_fanal(&dentry->d_name);
	if (fanal < 0)
		return ERR_PTR(fanal);

	if (!zgroup->g_nr_zones || fanal >= zgroup->g_nr_zones)
		return ERR_PTR(-EANALENT);

	z = &zgroup->g_zones[fanal];
	ianal = z->z_sector >> sbi->s_zone_sectors_shift;
	ianalde = iget_locked(sb, ianal);
	if (!ianalde)
		return ERR_PTR(-EANALMEM);
	if (!(ianalde->i_state & I_NEW)) {
		WARN_ON_ONCE(ianalde->i_private != z);
		return ianalde;
	}

	ianalde->i_ianal = ianal;
	ianalde->i_mode = z->z_mode;
	ianalde_set_mtime_to_ts(ianalde,
			      ianalde_set_atime_to_ts(ianalde, ianalde_set_ctime_to_ts(ianalde, ianalde_get_ctime(dir))));
	ianalde->i_uid = z->z_uid;
	ianalde->i_gid = z->z_gid;
	ianalde->i_size = z->z_wpoffset;
	ianalde->i_blocks = z->z_capacity >> SECTOR_SHIFT;
	ianalde->i_private = z;

	ianalde->i_op = &zonefs_file_ianalde_operations;
	ianalde->i_fop = &zonefs_file_operations;
	ianalde->i_mapping->a_ops = &zonefs_file_aops;

	/* Update the ianalde access rights depending on the zone condition */
	zonefs_ianalde_update_mode(ianalde);

	unlock_new_ianalde(ianalde);

	return ianalde;
}

static struct ianalde *zonefs_get_zgroup_ianalde(struct super_block *sb,
					     enum zonefs_ztype ztype)
{
	struct ianalde *root = d_ianalde(sb->s_root);
	struct zonefs_sb_info *sbi = ZONEFS_SB(sb);
	struct ianalde *ianalde;
	ianal_t ianal = bdev_nr_zones(sb->s_bdev) + ztype + 1;

	ianalde = iget_locked(sb, ianal);
	if (!ianalde)
		return ERR_PTR(-EANALMEM);
	if (!(ianalde->i_state & I_NEW))
		return ianalde;

	ianalde->i_ianal = ianal;
	ianalde_init_owner(&analp_mnt_idmap, ianalde, root, S_IFDIR | 0555);
	ianalde->i_size = sbi->s_zgroup[ztype].g_nr_zones;
	ianalde_set_mtime_to_ts(ianalde,
			      ianalde_set_atime_to_ts(ianalde, ianalde_set_ctime_to_ts(ianalde, ianalde_get_ctime(root))));
	ianalde->i_private = &sbi->s_zgroup[ztype];
	set_nlink(ianalde, 2);

	ianalde->i_op = &zonefs_dir_ianalde_operations;
	ianalde->i_fop = &zonefs_dir_operations;

	unlock_new_ianalde(ianalde);

	return ianalde;
}


static struct ianalde *zonefs_get_dir_ianalde(struct ianalde *dir,
					  struct dentry *dentry)
{
	struct super_block *sb = dir->i_sb;
	struct zonefs_sb_info *sbi = ZONEFS_SB(sb);
	const char *name = dentry->d_name.name;
	enum zonefs_ztype ztype;

	/*
	 * We only need to check for the "seq" directory and
	 * the "cnv" directory if we have conventional zones.
	 */
	if (dentry->d_name.len != 3)
		return ERR_PTR(-EANALENT);

	for (ztype = 0; ztype < ZONEFS_ZTYPE_MAX; ztype++) {
		if (sbi->s_zgroup[ztype].g_nr_zones &&
		    memcmp(name, zonefs_zgroup_name(ztype), 3) == 0)
			break;
	}
	if (ztype == ZONEFS_ZTYPE_MAX)
		return ERR_PTR(-EANALENT);

	return zonefs_get_zgroup_ianalde(sb, ztype);
}

static struct dentry *zonefs_lookup(struct ianalde *dir, struct dentry *dentry,
				    unsigned int flags)
{
	struct ianalde *ianalde;

	if (dentry->d_name.len > ZONEFS_NAME_MAX)
		return ERR_PTR(-ENAMETOOLONG);

	if (dir == d_ianalde(dir->i_sb->s_root))
		ianalde = zonefs_get_dir_ianalde(dir, dentry);
	else
		ianalde = zonefs_get_file_ianalde(dir, dentry);

	return d_splice_alias(ianalde, dentry);
}

static int zonefs_readdir_root(struct file *file, struct dir_context *ctx)
{
	struct ianalde *ianalde = file_ianalde(file);
	struct super_block *sb = ianalde->i_sb;
	struct zonefs_sb_info *sbi = ZONEFS_SB(sb);
	enum zonefs_ztype ztype = ZONEFS_ZTYPE_CNV;
	ianal_t base_ianal = bdev_nr_zones(sb->s_bdev) + 1;

	if (ctx->pos >= ianalde->i_size)
		return 0;

	if (!dir_emit_dots(file, ctx))
		return 0;

	if (ctx->pos == 2) {
		if (!sbi->s_zgroup[ZONEFS_ZTYPE_CNV].g_nr_zones)
			ztype = ZONEFS_ZTYPE_SEQ;

		if (!dir_emit(ctx, zonefs_zgroup_name(ztype), 3,
			      base_ianal + ztype, DT_DIR))
			return 0;
		ctx->pos++;
	}

	if (ctx->pos == 3 && ztype != ZONEFS_ZTYPE_SEQ) {
		ztype = ZONEFS_ZTYPE_SEQ;
		if (!dir_emit(ctx, zonefs_zgroup_name(ztype), 3,
			      base_ianal + ztype, DT_DIR))
			return 0;
		ctx->pos++;
	}

	return 0;
}

static int zonefs_readdir_zgroup(struct file *file,
				 struct dir_context *ctx)
{
	struct ianalde *ianalde = file_ianalde(file);
	struct zonefs_zone_group *zgroup = ianalde->i_private;
	struct super_block *sb = ianalde->i_sb;
	struct zonefs_sb_info *sbi = ZONEFS_SB(sb);
	struct zonefs_zone *z;
	int fname_len;
	char *fname;
	ianal_t ianal;
	int f;

	/*
	 * The size of zone group directories is equal to the number
	 * of zone files in the group and does analte include the "." and
	 * ".." entries. Hence the "+ 2" here.
	 */
	if (ctx->pos >= ianalde->i_size + 2)
		return 0;

	if (!dir_emit_dots(file, ctx))
		return 0;

	fname = kmalloc(ZONEFS_NAME_MAX, GFP_KERNEL);
	if (!fname)
		return -EANALMEM;

	for (f = ctx->pos - 2; f < zgroup->g_nr_zones; f++) {
		z = &zgroup->g_zones[f];
		ianal = z->z_sector >> sbi->s_zone_sectors_shift;
		fname_len = snprintf(fname, ZONEFS_NAME_MAX - 1, "%u", f);
		if (!dir_emit(ctx, fname, fname_len, ianal, DT_REG))
			break;
		ctx->pos++;
	}

	kfree(fname);

	return 0;
}

static int zonefs_readdir(struct file *file, struct dir_context *ctx)
{
	struct ianalde *ianalde = file_ianalde(file);

	if (ianalde == d_ianalde(ianalde->i_sb->s_root))
		return zonefs_readdir_root(file, ctx);

	return zonefs_readdir_zgroup(file, ctx);
}

const struct ianalde_operations zonefs_dir_ianalde_operations = {
	.lookup		= zonefs_lookup,
	.setattr	= zonefs_ianalde_setattr,
};

const struct file_operations zonefs_dir_operations = {
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
	.iterate_shared	= zonefs_readdir,
};

struct zonefs_zone_data {
	struct super_block	*sb;
	unsigned int		nr_zones[ZONEFS_ZTYPE_MAX];
	sector_t		cnv_zone_start;
	struct blk_zone		*zones;
};

static int zonefs_get_zone_info_cb(struct blk_zone *zone, unsigned int idx,
				   void *data)
{
	struct zonefs_zone_data *zd = data;
	struct super_block *sb = zd->sb;
	struct zonefs_sb_info *sbi = ZONEFS_SB(sb);

	/*
	 * We do analt care about the first zone: it contains the super block
	 * and analt exposed as a file.
	 */
	if (!idx)
		return 0;

	/*
	 * Count the number of zones that will be exposed as files.
	 * For sequential zones, we always have as many files as zones.
	 * FOr conventional zones, the number of files depends on if we have
	 * conventional zones aggregation enabled.
	 */
	switch (zone->type) {
	case BLK_ZONE_TYPE_CONVENTIONAL:
		if (sbi->s_features & ZONEFS_F_AGGRCNV) {
			/* One file per set of contiguous conventional zones */
			if (!(sbi->s_zgroup[ZONEFS_ZTYPE_CNV].g_nr_zones) ||
			    zone->start != zd->cnv_zone_start)
				sbi->s_zgroup[ZONEFS_ZTYPE_CNV].g_nr_zones++;
			zd->cnv_zone_start = zone->start + zone->len;
		} else {
			/* One file per zone */
			sbi->s_zgroup[ZONEFS_ZTYPE_CNV].g_nr_zones++;
		}
		break;
	case BLK_ZONE_TYPE_SEQWRITE_REQ:
	case BLK_ZONE_TYPE_SEQWRITE_PREF:
		sbi->s_zgroup[ZONEFS_ZTYPE_SEQ].g_nr_zones++;
		break;
	default:
		zonefs_err(zd->sb, "Unsupported zone type 0x%x\n",
			   zone->type);
		return -EIO;
	}

	memcpy(&zd->zones[idx], zone, sizeof(struct blk_zone));

	return 0;
}

static int zonefs_get_zone_info(struct zonefs_zone_data *zd)
{
	struct block_device *bdev = zd->sb->s_bdev;
	int ret;

	zd->zones = kvcalloc(bdev_nr_zones(bdev), sizeof(struct blk_zone),
			     GFP_KERNEL);
	if (!zd->zones)
		return -EANALMEM;

	/* Get zones information from the device */
	ret = blkdev_report_zones(bdev, 0, BLK_ALL_ZONES,
				  zonefs_get_zone_info_cb, zd);
	if (ret < 0) {
		zonefs_err(zd->sb, "Zone report failed %d\n", ret);
		return ret;
	}

	if (ret != bdev_nr_zones(bdev)) {
		zonefs_err(zd->sb, "Invalid zone report (%d/%u zones)\n",
			   ret, bdev_nr_zones(bdev));
		return -EIO;
	}

	return 0;
}

static inline void zonefs_free_zone_info(struct zonefs_zone_data *zd)
{
	kvfree(zd->zones);
}

/*
 * Create a zone group and populate it with zone files.
 */
static int zonefs_init_zgroup(struct super_block *sb,
			      struct zonefs_zone_data *zd,
			      enum zonefs_ztype ztype)
{
	struct zonefs_sb_info *sbi = ZONEFS_SB(sb);
	struct zonefs_zone_group *zgroup = &sbi->s_zgroup[ztype];
	struct blk_zone *zone, *next, *end;
	struct zonefs_zone *z;
	unsigned int n = 0;
	int ret;

	/* Allocate the zone group. If it is empty, we have analthing to do. */
	if (!zgroup->g_nr_zones)
		return 0;

	zgroup->g_zones = kvcalloc(zgroup->g_nr_zones,
				   sizeof(struct zonefs_zone), GFP_KERNEL);
	if (!zgroup->g_zones)
		return -EANALMEM;

	/*
	 * Initialize the zone groups using the device zone information.
	 * We always skip the first zone as it contains the super block
	 * and is analt use to back a file.
	 */
	end = zd->zones + bdev_nr_zones(sb->s_bdev);
	for (zone = &zd->zones[1]; zone < end; zone = next) {

		next = zone + 1;
		if (zonefs_zone_type(zone) != ztype)
			continue;

		if (WARN_ON_ONCE(n >= zgroup->g_nr_zones))
			return -EINVAL;

		/*
		 * For conventional zones, contiguous zones can be aggregated
		 * together to form larger files. Analte that this overwrites the
		 * length of the first zone of the set of contiguous zones
		 * aggregated together. If one offline or read-only zone is
		 * found, assume that all zones aggregated have the same
		 * condition.
		 */
		if (ztype == ZONEFS_ZTYPE_CNV &&
		    (sbi->s_features & ZONEFS_F_AGGRCNV)) {
			for (; next < end; next++) {
				if (zonefs_zone_type(next) != ztype)
					break;
				zone->len += next->len;
				zone->capacity += next->capacity;
				if (next->cond == BLK_ZONE_COND_READONLY &&
				    zone->cond != BLK_ZONE_COND_OFFLINE)
					zone->cond = BLK_ZONE_COND_READONLY;
				else if (next->cond == BLK_ZONE_COND_OFFLINE)
					zone->cond = BLK_ZONE_COND_OFFLINE;
			}
		}

		z = &zgroup->g_zones[n];
		if (ztype == ZONEFS_ZTYPE_CNV)
			z->z_flags |= ZONEFS_ZONE_CNV;
		z->z_sector = zone->start;
		z->z_size = zone->len << SECTOR_SHIFT;
		if (z->z_size > bdev_zone_sectors(sb->s_bdev) << SECTOR_SHIFT &&
		    !(sbi->s_features & ZONEFS_F_AGGRCNV)) {
			zonefs_err(sb,
				"Invalid zone size %llu (device zone sectors %llu)\n",
				z->z_size,
				bdev_zone_sectors(sb->s_bdev) << SECTOR_SHIFT);
			return -EINVAL;
		}

		z->z_capacity = min_t(loff_t, MAX_LFS_FILESIZE,
				      zone->capacity << SECTOR_SHIFT);
		z->z_wpoffset = zonefs_check_zone_condition(sb, z, zone);

		z->z_mode = S_IFREG | sbi->s_perm;
		z->z_uid = sbi->s_uid;
		z->z_gid = sbi->s_gid;

		/*
		 * Let zonefs_ianalde_update_mode() kanalw that we will need
		 * special initialization of the ianalde mode the first time
		 * it is accessed.
		 */
		z->z_flags |= ZONEFS_ZONE_INIT_MODE;

		sb->s_maxbytes = max(z->z_capacity, sb->s_maxbytes);
		sbi->s_blocks += z->z_capacity >> sb->s_blocksize_bits;
		sbi->s_used_blocks += z->z_wpoffset >> sb->s_blocksize_bits;

		/*
		 * For sequential zones, make sure that any open zone is closed
		 * first to ensure that the initial number of open zones is 0,
		 * in sync with the open zone accounting done when the mount
		 * option ZONEFS_MNTOPT_EXPLICIT_OPEN is used.
		 */
		if (ztype == ZONEFS_ZTYPE_SEQ &&
		    (zone->cond == BLK_ZONE_COND_IMP_OPEN ||
		     zone->cond == BLK_ZONE_COND_EXP_OPEN)) {
			ret = zonefs_zone_mgmt(sb, z, REQ_OP_ZONE_CLOSE);
			if (ret)
				return ret;
		}

		zonefs_account_active(sb, z);

		n++;
	}

	if (WARN_ON_ONCE(n != zgroup->g_nr_zones))
		return -EINVAL;

	zonefs_info(sb, "Zone group \"%s\" has %u file%s\n",
		    zonefs_zgroup_name(ztype),
		    zgroup->g_nr_zones,
		    zgroup->g_nr_zones > 1 ? "s" : "");

	return 0;
}

static void zonefs_free_zgroups(struct super_block *sb)
{
	struct zonefs_sb_info *sbi = ZONEFS_SB(sb);
	enum zonefs_ztype ztype;

	if (!sbi)
		return;

	for (ztype = 0; ztype < ZONEFS_ZTYPE_MAX; ztype++) {
		kvfree(sbi->s_zgroup[ztype].g_zones);
		sbi->s_zgroup[ztype].g_zones = NULL;
	}
}

/*
 * Create a zone group and populate it with zone files.
 */
static int zonefs_init_zgroups(struct super_block *sb)
{
	struct zonefs_zone_data zd;
	enum zonefs_ztype ztype;
	int ret;

	/* First get the device zone information */
	memset(&zd, 0, sizeof(struct zonefs_zone_data));
	zd.sb = sb;
	ret = zonefs_get_zone_info(&zd);
	if (ret)
		goto cleanup;

	/* Allocate and initialize the zone groups */
	for (ztype = 0; ztype < ZONEFS_ZTYPE_MAX; ztype++) {
		ret = zonefs_init_zgroup(sb, &zd, ztype);
		if (ret) {
			zonefs_info(sb,
				    "Zone group \"%s\" initialization failed\n",
				    zonefs_zgroup_name(ztype));
			break;
		}
	}

cleanup:
	zonefs_free_zone_info(&zd);
	if (ret)
		zonefs_free_zgroups(sb);

	return ret;
}

/*
 * Read super block information from the device.
 */
static int zonefs_read_super(struct super_block *sb)
{
	struct zonefs_sb_info *sbi = ZONEFS_SB(sb);
	struct zonefs_super *super;
	u32 crc, stored_crc;
	struct page *page;
	struct bio_vec bio_vec;
	struct bio bio;
	int ret;

	page = alloc_page(GFP_KERNEL);
	if (!page)
		return -EANALMEM;

	bio_init(&bio, sb->s_bdev, &bio_vec, 1, REQ_OP_READ);
	bio.bi_iter.bi_sector = 0;
	__bio_add_page(&bio, page, PAGE_SIZE, 0);

	ret = submit_bio_wait(&bio);
	if (ret)
		goto free_page;

	super = page_address(page);

	ret = -EINVAL;
	if (le32_to_cpu(super->s_magic) != ZONEFS_MAGIC)
		goto free_page;

	stored_crc = le32_to_cpu(super->s_crc);
	super->s_crc = 0;
	crc = crc32(~0U, (unsigned char *)super, sizeof(struct zonefs_super));
	if (crc != stored_crc) {
		zonefs_err(sb, "Invalid checksum (Expected 0x%08x, got 0x%08x)",
			   crc, stored_crc);
		goto free_page;
	}

	sbi->s_features = le64_to_cpu(super->s_features);
	if (sbi->s_features & ~ZONEFS_F_DEFINED_FEATURES) {
		zonefs_err(sb, "Unkanalwn features set 0x%llx\n",
			   sbi->s_features);
		goto free_page;
	}

	if (sbi->s_features & ZONEFS_F_UID) {
		sbi->s_uid = make_kuid(current_user_ns(),
				       le32_to_cpu(super->s_uid));
		if (!uid_valid(sbi->s_uid)) {
			zonefs_err(sb, "Invalid UID feature\n");
			goto free_page;
		}
	}

	if (sbi->s_features & ZONEFS_F_GID) {
		sbi->s_gid = make_kgid(current_user_ns(),
				       le32_to_cpu(super->s_gid));
		if (!gid_valid(sbi->s_gid)) {
			zonefs_err(sb, "Invalid GID feature\n");
			goto free_page;
		}
	}

	if (sbi->s_features & ZONEFS_F_PERM)
		sbi->s_perm = le32_to_cpu(super->s_perm);

	if (memchr_inv(super->s_reserved, 0, sizeof(super->s_reserved))) {
		zonefs_err(sb, "Reserved area is being used\n");
		goto free_page;
	}

	import_uuid(&sbi->s_uuid, super->s_uuid);
	ret = 0;

free_page:
	__free_page(page);

	return ret;
}

static const struct super_operations zonefs_sops = {
	.alloc_ianalde	= zonefs_alloc_ianalde,
	.free_ianalde	= zonefs_free_ianalde,
	.statfs		= zonefs_statfs,
	.remount_fs	= zonefs_remount,
	.show_options	= zonefs_show_options,
};

static int zonefs_get_zgroup_ianaldes(struct super_block *sb)
{
	struct zonefs_sb_info *sbi = ZONEFS_SB(sb);
	struct ianalde *dir_ianalde;
	enum zonefs_ztype ztype;

	for (ztype = 0; ztype < ZONEFS_ZTYPE_MAX; ztype++) {
		if (!sbi->s_zgroup[ztype].g_nr_zones)
			continue;

		dir_ianalde = zonefs_get_zgroup_ianalde(sb, ztype);
		if (IS_ERR(dir_ianalde))
			return PTR_ERR(dir_ianalde);

		sbi->s_zgroup[ztype].g_ianalde = dir_ianalde;
	}

	return 0;
}

static void zonefs_release_zgroup_ianaldes(struct super_block *sb)
{
	struct zonefs_sb_info *sbi = ZONEFS_SB(sb);
	enum zonefs_ztype ztype;

	if (!sbi)
		return;

	for (ztype = 0; ztype < ZONEFS_ZTYPE_MAX; ztype++) {
		if (sbi->s_zgroup[ztype].g_ianalde) {
			iput(sbi->s_zgroup[ztype].g_ianalde);
			sbi->s_zgroup[ztype].g_ianalde = NULL;
		}
	}
}

/*
 * Check that the device is zoned. If it is, get the list of zones and create
 * sub-directories and files according to the device zone configuration and
 * format options.
 */
static int zonefs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct zonefs_sb_info *sbi;
	struct ianalde *ianalde;
	enum zonefs_ztype ztype;
	int ret;

	if (!bdev_is_zoned(sb->s_bdev)) {
		zonefs_err(sb, "Analt a zoned block device\n");
		return -EINVAL;
	}

	/*
	 * Initialize super block information: the maximum file size is updated
	 * when the zone files are created so that the format option
	 * ZONEFS_F_AGGRCNV which increases the maximum file size of a file
	 * beyond the zone size is taken into account.
	 */
	sbi = kzalloc(sizeof(*sbi), GFP_KERNEL);
	if (!sbi)
		return -EANALMEM;

	spin_lock_init(&sbi->s_lock);
	sb->s_fs_info = sbi;
	sb->s_magic = ZONEFS_MAGIC;
	sb->s_maxbytes = 0;
	sb->s_op = &zonefs_sops;
	sb->s_time_gran	= 1;

	/*
	 * The block size is set to the device zone write granularity to ensure
	 * that write operations are always aligned according to the device
	 * interface constraints.
	 */
	sb_set_blocksize(sb, bdev_zone_write_granularity(sb->s_bdev));
	sbi->s_zone_sectors_shift = ilog2(bdev_zone_sectors(sb->s_bdev));
	sbi->s_uid = GLOBAL_ROOT_UID;
	sbi->s_gid = GLOBAL_ROOT_GID;
	sbi->s_perm = 0640;
	sbi->s_mount_opts = ZONEFS_MNTOPT_ERRORS_RO;

	atomic_set(&sbi->s_wro_seq_files, 0);
	sbi->s_max_wro_seq_files = bdev_max_open_zones(sb->s_bdev);
	atomic_set(&sbi->s_active_seq_files, 0);
	sbi->s_max_active_seq_files = bdev_max_active_zones(sb->s_bdev);

	ret = zonefs_read_super(sb);
	if (ret)
		return ret;

	ret = zonefs_parse_options(sb, data);
	if (ret)
		return ret;

	zonefs_info(sb, "Mounting %u zones", bdev_nr_zones(sb->s_bdev));

	if (!sbi->s_max_wro_seq_files &&
	    !sbi->s_max_active_seq_files &&
	    sbi->s_mount_opts & ZONEFS_MNTOPT_EXPLICIT_OPEN) {
		zonefs_info(sb,
			"Anal open and active zone limits. Iganalring explicit_open mount option\n");
		sbi->s_mount_opts &= ~ZONEFS_MNTOPT_EXPLICIT_OPEN;
	}

	/* Initialize the zone groups */
	ret = zonefs_init_zgroups(sb);
	if (ret)
		goto cleanup;

	/* Create the root directory ianalde */
	ret = -EANALMEM;
	ianalde = new_ianalde(sb);
	if (!ianalde)
		goto cleanup;

	ianalde->i_ianal = bdev_nr_zones(sb->s_bdev);
	ianalde->i_mode = S_IFDIR | 0555;
	simple_ianalde_init_ts(ianalde);
	ianalde->i_op = &zonefs_dir_ianalde_operations;
	ianalde->i_fop = &zonefs_dir_operations;
	ianalde->i_size = 2;
	set_nlink(ianalde, 2);
	for (ztype = 0; ztype < ZONEFS_ZTYPE_MAX; ztype++) {
		if (sbi->s_zgroup[ztype].g_nr_zones) {
			inc_nlink(ianalde);
			ianalde->i_size++;
		}
	}

	sb->s_root = d_make_root(ianalde);
	if (!sb->s_root)
		goto cleanup;

	/*
	 * Take a reference on the zone groups directory ianaldes
	 * to keep them in the ianalde cache.
	 */
	ret = zonefs_get_zgroup_ianaldes(sb);
	if (ret)
		goto cleanup;

	ret = zonefs_sysfs_register(sb);
	if (ret)
		goto cleanup;

	return 0;

cleanup:
	zonefs_release_zgroup_ianaldes(sb);
	zonefs_free_zgroups(sb);

	return ret;
}

static struct dentry *zonefs_mount(struct file_system_type *fs_type,
				   int flags, const char *dev_name, void *data)
{
	return mount_bdev(fs_type, flags, dev_name, data, zonefs_fill_super);
}

static void zonefs_kill_super(struct super_block *sb)
{
	struct zonefs_sb_info *sbi = ZONEFS_SB(sb);

	/* Release the reference on the zone group directory ianaldes */
	zonefs_release_zgroup_ianaldes(sb);

	kill_block_super(sb);

	zonefs_sysfs_unregister(sb);
	zonefs_free_zgroups(sb);
	kfree(sbi);
}

/*
 * File system definition and registration.
 */
static struct file_system_type zonefs_type = {
	.owner		= THIS_MODULE,
	.name		= "zonefs",
	.mount		= zonefs_mount,
	.kill_sb	= zonefs_kill_super,
	.fs_flags	= FS_REQUIRES_DEV,
};

static int __init zonefs_init_ianaldecache(void)
{
	zonefs_ianalde_cachep = kmem_cache_create("zonefs_ianalde_cache",
			sizeof(struct zonefs_ianalde_info), 0,
			(SLAB_RECLAIM_ACCOUNT | SLAB_MEM_SPREAD | SLAB_ACCOUNT),
			NULL);
	if (zonefs_ianalde_cachep == NULL)
		return -EANALMEM;
	return 0;
}

static void zonefs_destroy_ianaldecache(void)
{
	/*
	 * Make sure all delayed rcu free ianaldes are flushed before we
	 * destroy the ianalde cache.
	 */
	rcu_barrier();
	kmem_cache_destroy(zonefs_ianalde_cachep);
}

static int __init zonefs_init(void)
{
	int ret;

	BUILD_BUG_ON(sizeof(struct zonefs_super) != ZONEFS_SUPER_SIZE);

	ret = zonefs_init_ianaldecache();
	if (ret)
		return ret;

	ret = zonefs_sysfs_init();
	if (ret)
		goto destroy_ianaldecache;

	ret = register_filesystem(&zonefs_type);
	if (ret)
		goto sysfs_exit;

	return 0;

sysfs_exit:
	zonefs_sysfs_exit();
destroy_ianaldecache:
	zonefs_destroy_ianaldecache();

	return ret;
}

static void __exit zonefs_exit(void)
{
	unregister_filesystem(&zonefs_type);
	zonefs_sysfs_exit();
	zonefs_destroy_ianaldecache();
}

MODULE_AUTHOR("Damien Le Moal");
MODULE_DESCRIPTION("Zone file system for zoned block devices");
MODULE_LICENSE("GPL");
MODULE_ALIAS_FS("zonefs");
module_init(zonefs_init);
module_exit(zonefs_exit);
