/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2013 by Delphix. All rights reserved.
 */

#ifndef	_SYS_ZFS_IOCTL_H
#define	_SYS_ZFS_IOCTL_H

#include <sys/cred.h>
#include <sys/dmu.h>
#include <sys/zio.h>
#include <sys/dsl_deleg.h>
#include <sys/spa.h>
#include <sys/zfs_stat.h>

#ifdef _KERNEL
#include <sys/nvpair.h>
#endif	/* _KERNEL */

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * The structures in this file are passed between userland and the
 * kernel.  Userland may be running a 32-bit process, while the kernel
 * is 64-bit.  Therefore, these structures need to compile the same in
 * 32-bit and 64-bit.  This means not using type "long", and adding
 * explicit padding so that the 32-bit structure will not be packed more
 * tightly than the 64-bit structure (which requires 64-bit alignment).
 */

/*
 * Property values for snapdir
 */
#define	ZFS_SNAPDIR_HIDDEN		0
#define	ZFS_SNAPDIR_VISIBLE		1

/*
 * Property values for snapdev
 */
#define	ZFS_SNAPDEV_HIDDEN		0
#define	ZFS_SNAPDEV_VISIBLE		1
/*
 * Property values for acltype
 */
#define	ZFS_ACLTYPE_OFF			0
#define	ZFS_ACLTYPE_POSIXACL		1

/*
 * Field manipulation macros for the drr_versioninfo field of the
 * send stream header.
 */

/*
 * Header types for zfs send streams.
 */
typedef enum drr_headertype {
	DMU_SUBSTREAM = 0x1,
	DMU_COMPOUNDSTREAM = 0x2
} drr_headertype_t;

#define	DMU_GET_STREAM_HDRTYPE(vi)	BF64_GET((vi), 0, 2)
#define	DMU_SET_STREAM_HDRTYPE(vi, x)	BF64_SET((vi), 0, 2, x)

#define	DMU_GET_FEATUREFLAGS(vi)	BF64_GET((vi), 2, 30)
#define	DMU_SET_FEATUREFLAGS(vi, x)	BF64_SET((vi), 2, 30, x)

/*
 * Feature flags for zfs send streams (flags in drr_versioninfo)
 */

#define	DMU_BACKUP_FEATURE_DEDUP		(1<<0)
#define	DMU_BACKUP_FEATURE_DEDUPPROPS		(1<<1)
#define	DMU_BACKUP_FEATURE_SA_SPILL		(1<<2)
/* flags #3 - #15 are reserved for incompatible closed-source implementations */
#define	DMU_BACKUP_FEATURE_EMBED_DATA		(1<<16)
#define	DMU_BACKUP_FEATURE_EMBED_DATA_LZ4	(1<<17)
/* flag #18 is reserved for a Delphix feature */
#define	DMU_BACKUP_FEATURE_LARGE_BLOCKS		(1<<19)

/*
 * Mask of all supported backup features
 */
#define	DMU_BACKUP_FEATURE_MASK	(DMU_BACKUP_FEATURE_DEDUP | \
    DMU_BACKUP_FEATURE_DEDUPPROPS | DMU_BACKUP_FEATURE_SA_SPILL | \
    DMU_BACKUP_FEATURE_EMBED_DATA | DMU_BACKUP_FEATURE_EMBED_DATA_LZ4 | \
    DMU_BACKUP_FEATURE_LARGE_BLOCKS)

/* Are all features in the given flag word currently supported? */
#define	DMU_STREAM_SUPPORTED(x)	(!((x) & ~DMU_BACKUP_FEATURE_MASK))

/*
 * The drr_versioninfo field of the dmu_replay_record has the
 * following layout:
 *
 *	64	56	48	40	32	24	16	8	0
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 *  	|		reserved	|        feature-flags	    |C|S|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 *
 * The low order two bits indicate the header type: SUBSTREAM (0x1)
 * or COMPOUNDSTREAM (0x2).  Using two bits for this is historical:
 * this field used to be a version number, where the two version types
 * were 1 and 2.  Using two bits for this allows earlier versions of
 * the code to be able to recognize send streams that don't use any
 * of the features indicated by feature flags.
 */

#define	DMU_BACKUP_MAGIC 0x2F5bacbacULL

#define	DRR_FLAG_CLONE		(1<<0)
#define	DRR_FLAG_CI_DATA	(1<<1)

/*
 * flags in the drr_checksumflags field in the DRR_WRITE and
 * DRR_WRITE_BYREF blocks
 */
#define	DRR_CHECKSUM_DEDUP	(1<<0)

#define	DRR_IS_DEDUP_CAPABLE(flags)	((flags) & DRR_CHECKSUM_DEDUP)

/*
 * zfs ioctl command structure
 */
typedef struct dmu_replay_record {
	enum {
		DRR_BEGIN, DRR_OBJECT, DRR_FREEOBJECTS,
		DRR_WRITE, DRR_FREE, DRR_END, DRR_WRITE_BYREF,
		DRR_SPILL, DRR_WRITE_EMBEDDED, DRR_NUMTYPES
	} drr_type;
	uint32_t drr_payloadlen;
	union {
		struct drr_begin {
			uint64_t drr_magic;
			uint64_t drr_versioninfo; /* was drr_version */
			uint64_t drr_creation_time;
			dmu_objset_type_t drr_type;
			uint32_t drr_flags;
			uint64_t drr_toguid;
			uint64_t drr_fromguid;
			char drr_toname[MAXNAMELEN];
		} drr_begin;
		struct drr_end {
			zio_cksum_t drr_checksum;
			uint64_t drr_toguid;
		} drr_end;
		struct drr_object {
			uint64_t drr_object;
			dmu_object_type_t drr_type;
			dmu_object_type_t drr_bonustype;
			uint32_t drr_blksz;
			uint32_t drr_bonuslen;
			uint8_t drr_checksumtype;
			uint8_t drr_compress;
			uint8_t drr_pad[6];
			uint64_t drr_toguid;
			/* bonus content follows */
		} drr_object;
		struct drr_freeobjects {
			uint64_t drr_firstobj;
			uint64_t drr_numobjs;
			uint64_t drr_toguid;
		} drr_freeobjects;
		struct drr_write {
			uint64_t drr_object;
			dmu_object_type_t drr_type;
			uint32_t drr_pad;
			uint64_t drr_offset;
			uint64_t drr_length;
			uint64_t drr_toguid;
			uint8_t drr_checksumtype;
			uint8_t drr_checksumflags;
			uint8_t drr_pad2[6];
			ddt_key_t drr_key; /* deduplication key */
			/* content follows */
		} drr_write;
		struct drr_free {
			uint64_t drr_object;
			uint64_t drr_offset;
			uint64_t drr_length;
			uint64_t drr_toguid;
		} drr_free;
		struct drr_write_byref {
			/* where to put the data */
			uint64_t drr_object;
			uint64_t drr_offset;
			uint64_t drr_length;
			uint64_t drr_toguid;
			/* where to find the prior copy of the data */
			uint64_t drr_refguid;
			uint64_t drr_refobject;
			uint64_t drr_refoffset;
			/* properties of the data */
			uint8_t drr_checksumtype;
			uint8_t drr_checksumflags;
			uint8_t drr_pad2[6];
			ddt_key_t drr_key; /* deduplication key */
		} drr_write_byref;
		struct drr_spill {
			uint64_t drr_object;
			uint64_t drr_length;
			uint64_t drr_toguid;
			uint64_t drr_pad[4]; /* needed for crypto */
			/* spill data follows */
		} drr_spill;
		struct drr_write_embedded {
			uint64_t drr_object;
			uint64_t drr_offset;
			/* logical length, should equal blocksize */
			uint64_t drr_length;
			uint64_t drr_toguid;
			uint8_t drr_compression;
			uint8_t drr_etype;
			uint8_t drr_pad[6];
			uint32_t drr_lsize; /* uncompressed size of payload */
			uint32_t drr_psize; /* compr. (real) size of payload */
			/* (possibly compressed) content follows */
		} drr_write_embedded;
	} drr_u;
} dmu_replay_record_t;

/* diff record range types */
typedef enum diff_type {
	DDR_NONE = 0x1,
	DDR_INUSE = 0x2,
	DDR_FREE = 0x4
} diff_type_t;

/*
 * The diff reports back ranges of free or in-use objects.
 */
typedef struct dmu_diff_record {
	uint64_t ddr_type;
	uint64_t ddr_first;
	uint64_t ddr_last;
} dmu_diff_record_t;

typedef struct zinject_record {
	uint64_t	zi_objset;
	uint64_t	zi_object;
	uint64_t	zi_start;
	uint64_t	zi_end;
	uint64_t	zi_guid;
	uint32_t	zi_level;
	uint32_t	zi_error;
	uint64_t	zi_type;
	uint32_t	zi_freq;
	uint32_t	zi_failfast;
	char		zi_func[MAXNAMELEN];
	uint32_t	zi_iotype;
	int32_t		zi_duration;
	uint64_t	zi_timer;
	uint32_t	zi_cmd;
	uint32_t	zi_pad;
} zinject_record_t;

#define	ZINJECT_NULL		0x1
#define	ZINJECT_FLUSH_ARC	0x2
#define	ZINJECT_UNLOAD_SPA	0x4

#define	ZEVENT_NONE		0x0
#define	ZEVENT_NONBLOCK		0x1
#define	ZEVENT_SIZE		1024

#define	ZEVENT_SEEK_START	0
#define	ZEVENT_SEEK_END		UINT64_MAX

typedef enum zinject_type {
	ZINJECT_UNINITIALIZED,
	ZINJECT_DATA_FAULT,
	ZINJECT_DEVICE_FAULT,
	ZINJECT_LABEL_FAULT,
	ZINJECT_IGNORED_WRITES,
	ZINJECT_PANIC,
	ZINJECT_DELAY_IO,
} zinject_type_t;

typedef struct zfs_share {
	uint64_t	z_exportdata;
	uint64_t	z_sharedata;
	uint64_t	z_sharetype;	/* 0 = share, 1 = unshare */
	uint64_t	z_sharemax;  /* max length of share string */
} zfs_share_t;

/*
 * ZFS file systems may behave the usual, POSIX-compliant way, where
 * name lookups are case-sensitive.  They may also be set up so that
 * all the name lookups are case-insensitive, or so that only some
 * lookups, the ones that set an FIGNORECASE flag, are case-insensitive.
 */
typedef enum zfs_case {
	ZFS_CASE_SENSITIVE,
	ZFS_CASE_INSENSITIVE,
	ZFS_CASE_MIXED
} zfs_case_t;

typedef struct zfs_cmd {
	char		zc_name[MAXPATHLEN];	/* name of pool or dataset */
	uint64_t	zc_nvlist_src;		/* really (char *) */
	uint64_t	zc_nvlist_src_size;
	uint64_t	zc_nvlist_dst;		/* really (char *) */
	uint64_t	zc_nvlist_dst_size;
	boolean_t	zc_nvlist_dst_filled;	/* put an nvlist in dst? */
	int		zc_pad2;

	/*
	 * The following members are for legacy ioctls which haven't been
	 * converted to the new method.
	 */
	uint64_t	zc_history;		/* really (char *) */
	char		zc_value[MAXPATHLEN * 2];
	char		zc_string[MAXNAMELEN];
	uint64_t	zc_guid;
	uint64_t	zc_nvlist_conf;		/* really (char *) */
	uint64_t	zc_nvlist_conf_size;
	uint64_t	zc_cookie;
	uint64_t	zc_objset_type;
	uint64_t	zc_perm_action;
	uint64_t	zc_history_len;
	uint64_t	zc_history_offset;
	uint64_t	zc_obj;
	uint64_t	zc_iflags;		/* internal to zfs(7fs) */
	zfs_share_t	zc_share;
	dmu_objset_stats_t zc_objset_stats;
	struct drr_begin zc_begin_record;
	zinject_record_t zc_inject_record;
	uint32_t	zc_defer_destroy;
	uint32_t	zc_flags;
	uint64_t	zc_action_handle;
	int		zc_cleanup_fd;
	uint8_t		zc_simple;
	uint8_t		zc_pad[3];		/* alignment */
	uint64_t	zc_sendobj;
	uint64_t	zc_fromobj;
	uint64_t	zc_createtxg;
	zfs_stat_t	zc_stat;
} zfs_cmd_t;

typedef struct zfs_useracct {
	char zu_domain[256];
	uid_t zu_rid;
	uint32_t zu_pad;
	uint64_t zu_space;
} zfs_useracct_t;

#define	ZFSDEV_MAX_MINOR	(1 << 16)
#define	ZFS_MIN_MINOR	(ZFSDEV_MAX_MINOR + 1)

#define	ZPOOL_EXPORT_AFTER_SPLIT 0x1

#ifdef _KERNEL

typedef struct zfs_creat {
	nvlist_t	*zct_zplprops;
	nvlist_t	*zct_props;
} zfs_creat_t;

extern int zfs_secpolicy_snapshot_perms(const char *name, cred_t *cr);
extern int zfs_secpolicy_rename_perms(const char *from,
    const char *to, cred_t *cr);
extern int zfs_secpolicy_destroy_perms(const char *name, cred_t *cr);
extern int zfs_unmount_snap(const char *);
extern void zfs_destroy_unmount_origin(const char *);

extern boolean_t dataset_name_hidden(const char *name);

enum zfsdev_state_type {
	ZST_ONEXIT,
	ZST_ZEVENT,
	ZST_ALL,
};

/*
 * The zfsdev_state_t structure is managed as a singly-linked list
 * from which items are never deleted.  This allows for lock-free
 * reading of the list so long as assignments to the zs_next and
 * reads from zs_minor are performed atomically.  Empty items are
 * indicated by storing -1 into zs_minor.
 */
typedef struct zfsdev_state {
	struct zfsdev_state	*zs_next;	/* next zfsdev_state_t link */
	struct file		*zs_file;	/* associated file struct */
	minor_t			zs_minor;	/* made up minor number */
	void			*zs_onexit;	/* onexit data */
	void			*zs_zevent;	/* zevent data */
} zfsdev_state_t;

extern void *zfsdev_get_state(minor_t minor, enum zfsdev_state_type which);
extern int zfsdev_getminor(struct file *filp, minor_t *minorp);
extern minor_t zfsdev_minor_alloc(void);

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_ZFS_IOCTL_H */
