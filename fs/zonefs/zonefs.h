/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Simple zone file system for zoned block devices.
 *
 * Copyright (C) 2019 Western Digital Corporation or its affiliates.
 */
#ifndef __ZONEFS_H__
#define __ZONEFS_H__

#include <linux/fs.h>
#include <linux/magic.h>
#include <linux/uuid.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <linux/kobject.h>

/*
 * Maximum length of file names: this only needs to be large enough to fit
 * the zone group directory names and a decimal zone number for file names.
 * 16 characters is plenty.
 */
#define ZONEFS_NAME_MAX		16

/*
 * Zone types: ZONEFS_ZTYPE_SEQ is used for all sequential zone types
 * defined in linux/blkzoned.h, that is, BLK_ZONE_TYPE_SEQWRITE_REQ and
 * BLK_ZONE_TYPE_SEQWRITE_PREF.
 */
enum zonefs_ztype {
	ZONEFS_ZTYPE_CNV,
	ZONEFS_ZTYPE_SEQ,
	ZONEFS_ZTYPE_MAX,
};

static inline enum zonefs_ztype zonefs_zone_type(struct blk_zone *zone)
{
	if (zone->type == BLK_ZONE_TYPE_CONVENTIONAL)
		return ZONEFS_ZTYPE_CNV;
	return ZONEFS_ZTYPE_SEQ;
}

#define ZONEFS_ZONE_INIT_MODE	(1U << 0)
#define ZONEFS_ZONE_OPEN	(1U << 1)
#define ZONEFS_ZONE_ACTIVE	(1U << 2)
#define ZONEFS_ZONE_OFFLINE	(1U << 3)
#define ZONEFS_ZONE_READONLY	(1U << 4)
#define ZONEFS_ZONE_CNV		(1U << 31)

/*
 * In-memory per-file inode zone data.
 */
struct zonefs_zone {
	/* Zone state flags */
	unsigned int		z_flags;

	/* Zone start sector (512B unit) */
	sector_t		z_sector;

	/* Zone size (bytes) */
	loff_t			z_size;

	/* Zone capacity (file maximum size, bytes) */
	loff_t			z_capacity;

	/* Write pointer offset in the zone (sequential zones only, bytes) */
	loff_t			z_wpoffset;

	/* Saved inode uid, gid and access rights */
	umode_t			z_mode;
	kuid_t			z_uid;
	kgid_t			z_gid;
};

/*
 * In memory zone group information: all zones of a group are exposed
 * as files, one file per zone.
 */
struct zonefs_zone_group {
	struct inode		*g_inode;
	unsigned int		g_nr_zones;
	struct zonefs_zone	*g_zones;
};

/*
 * In-memory inode data.
 */
struct zonefs_inode_info {
	struct inode		i_vnode;

	/*
	 * To serialise fully against both syscall and mmap based IO and
	 * sequential file truncation, two locks are used. For serializing
	 * zonefs_seq_file_truncate() against zonefs_iomap_begin(), that is,
	 * file truncate operations against block mapping, i_truncate_mutex is
	 * used. i_truncate_mutex also protects against concurrent accesses
	 * and changes to the inode private data, and in particular changes to
	 * a sequential file size on completion of direct IO writes.
	 * Serialization of mmap read IOs with truncate and syscall IO
	 * operations is done with invalidate_lock in addition to
	 * i_truncate_mutex.  Only zonefs_seq_file_truncate() takes both lock
	 * (invalidate_lock first, i_truncate_mutex second).
	 */
	struct mutex		i_truncate_mutex;

	/* guarded by i_truncate_mutex */
	unsigned int		i_wr_refcnt;
};

static inline struct zonefs_inode_info *ZONEFS_I(struct inode *inode)
{
	return container_of(inode, struct zonefs_inode_info, i_vnode);
}

static inline bool zonefs_zone_is_cnv(struct zonefs_zone *z)
{
	return z->z_flags & ZONEFS_ZONE_CNV;
}

static inline bool zonefs_zone_is_seq(struct zonefs_zone *z)
{
	return !zonefs_zone_is_cnv(z);
}

static inline struct zonefs_zone *zonefs_inode_zone(struct inode *inode)
{
	return inode->i_private;
}

static inline bool zonefs_inode_is_cnv(struct inode *inode)
{
	return zonefs_zone_is_cnv(zonefs_inode_zone(inode));
}

static inline bool zonefs_inode_is_seq(struct inode *inode)
{
	return zonefs_zone_is_seq(zonefs_inode_zone(inode));
}

/*
 * On-disk super block (block 0).
 */
#define ZONEFS_LABEL_LEN	64
#define ZONEFS_UUID_SIZE	16
#define ZONEFS_SUPER_SIZE	4096

struct zonefs_super {

	/* Magic number */
	__le32		s_magic;

	/* Checksum */
	__le32		s_crc;

	/* Volume label */
	char		s_label[ZONEFS_LABEL_LEN];

	/* 128-bit uuid */
	__u8		s_uuid[ZONEFS_UUID_SIZE];

	/* Features */
	__le64		s_features;

	/* UID/GID to use for files */
	__le32		s_uid;
	__le32		s_gid;

	/* File permissions */
	__le32		s_perm;

	/* Padding to ZONEFS_SUPER_SIZE bytes */
	__u8		s_reserved[3988];

} __packed;

/*
 * Feature flags: specified in the s_features field of the on-disk super
 * block struct zonefs_super and in-memory in the s_feartures field of
 * struct zonefs_sb_info.
 */
enum zonefs_features {
	/*
	 * Aggregate contiguous conventional zones into a single file.
	 */
	ZONEFS_F_AGGRCNV = 1ULL << 0,
	/*
	 * Use super block specified UID for files instead of default 0.
	 */
	ZONEFS_F_UID = 1ULL << 1,
	/*
	 * Use super block specified GID for files instead of default 0.
	 */
	ZONEFS_F_GID = 1ULL << 2,
	/*
	 * Use super block specified file permissions instead of default 640.
	 */
	ZONEFS_F_PERM = 1ULL << 3,
};

#define ZONEFS_F_DEFINED_FEATURES \
	(ZONEFS_F_AGGRCNV | ZONEFS_F_UID | ZONEFS_F_GID | ZONEFS_F_PERM)

/*
 * Mount options for zone write pointer error handling.
 */
#define ZONEFS_MNTOPT_ERRORS_RO		(1 << 0) /* Make zone file readonly */
#define ZONEFS_MNTOPT_ERRORS_ZRO	(1 << 1) /* Make zone file offline */
#define ZONEFS_MNTOPT_ERRORS_ZOL	(1 << 2) /* Make zone file offline */
#define ZONEFS_MNTOPT_ERRORS_REPAIR	(1 << 3) /* Remount read-only */
#define ZONEFS_MNTOPT_ERRORS_MASK	\
	(ZONEFS_MNTOPT_ERRORS_RO | ZONEFS_MNTOPT_ERRORS_ZRO | \
	 ZONEFS_MNTOPT_ERRORS_ZOL | ZONEFS_MNTOPT_ERRORS_REPAIR)
#define ZONEFS_MNTOPT_EXPLICIT_OPEN	(1 << 4) /* Explicit open/close of zones on open/close */

/*
 * In-memory Super block information.
 */
struct zonefs_sb_info {

	unsigned long		s_mount_opts;

	spinlock_t		s_lock;

	unsigned long long	s_features;
	kuid_t			s_uid;
	kgid_t			s_gid;
	umode_t			s_perm;
	uuid_t			s_uuid;
	unsigned int		s_zone_sectors_shift;

	struct zonefs_zone_group s_zgroup[ZONEFS_ZTYPE_MAX];

	loff_t			s_blocks;
	loff_t			s_used_blocks;

	unsigned int		s_max_wro_seq_files;
	atomic_t		s_wro_seq_files;

	unsigned int		s_max_active_seq_files;
	atomic_t		s_active_seq_files;

	bool			s_sysfs_registered;
	struct kobject		s_kobj;
	struct completion	s_kobj_unregister;
};

static inline struct zonefs_sb_info *ZONEFS_SB(struct super_block *sb)
{
	return sb->s_fs_info;
}

#define zonefs_info(sb, format, args...)	\
	pr_info("zonefs (%s): " format, sb->s_id, ## args)
#define zonefs_err(sb, format, args...)		\
	pr_err("zonefs (%s) ERROR: " format, sb->s_id, ## args)
#define zonefs_warn(sb, format, args...)	\
	pr_warn("zonefs (%s) WARNING: " format, sb->s_id, ## args)

/* In super.c */
void zonefs_inode_account_active(struct inode *inode);
int zonefs_inode_zone_mgmt(struct inode *inode, enum req_op op);
void zonefs_i_size_write(struct inode *inode, loff_t isize);
void zonefs_update_stats(struct inode *inode, loff_t new_isize);
void __zonefs_io_error(struct inode *inode, bool write);

static inline void zonefs_io_error(struct inode *inode, bool write)
{
	struct zonefs_inode_info *zi = ZONEFS_I(inode);

	mutex_lock(&zi->i_truncate_mutex);
	__zonefs_io_error(inode, write);
	mutex_unlock(&zi->i_truncate_mutex);
}

/* In super.c */
extern const struct inode_operations zonefs_dir_inode_operations;
extern const struct file_operations zonefs_dir_operations;

/* In file.c */
extern const struct address_space_operations zonefs_file_aops;
extern const struct file_operations zonefs_file_operations;
int zonefs_file_truncate(struct inode *inode, loff_t isize);
int zonefs_file_bioset_init(void);
void zonefs_file_bioset_exit(void);

/* In sysfs.c */
int zonefs_sysfs_register(struct super_block *sb);
void zonefs_sysfs_unregister(struct super_block *sb);
int zonefs_sysfs_init(void);
void zonefs_sysfs_exit(void);

#endif
