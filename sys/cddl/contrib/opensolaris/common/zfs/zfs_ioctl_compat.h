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
 * Copyright 2014 Xin Li <delphij@FreeBSD.org>.  All rights reserved.
 * Copyright 2013 Martin Matuska <mm@FreeBSD.org>.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_SYS_ZFS_IOCTL_COMPAT_H
#define	_SYS_ZFS_IOCTL_COMPAT_H

#include <sys/cred.h>
#include <sys/dmu.h>
#include <sys/zio.h>
#include <sys/dsl_deleg.h>
#include <sys/zfs_ioctl.h>

#ifdef _KERNEL
#include <sys/nvpair.h>
#endif  /* _KERNEL */

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Backwards ioctl compatibility
 */

/* ioctl versions for vfs.zfs.version.ioctl */
#define	ZFS_IOCVER_UNDEF	-1
#define	ZFS_IOCVER_NONE		0
#define	ZFS_IOCVER_DEADMAN	1
#define	ZFS_IOCVER_LZC		2
#define	ZFS_IOCVER_ZCMD		3
#define	ZFS_IOCVER_EDBP		4
#define	ZFS_IOCVER_RESUME	5
#define	ZFS_IOCVER_INLANES	6
#define	ZFS_IOCVER_PAD		7
#define	ZFS_IOCVER_CURRENT	ZFS_IOCVER_PAD

/* compatibility conversion flag */
#define	ZFS_CMD_COMPAT_NONE	0
#define	ZFS_CMD_COMPAT_V15	1
#define	ZFS_CMD_COMPAT_V28	2
#define	ZFS_CMD_COMPAT_DEADMAN	3
#define	ZFS_CMD_COMPAT_LZC	4
#define	ZFS_CMD_COMPAT_ZCMD	5
#define	ZFS_CMD_COMPAT_EDBP	6
#define	ZFS_CMD_COMPAT_RESUME	7
#define	ZFS_CMD_COMPAT_INLANES	8

#define	ZFS_IOC_COMPAT_PASS	254
#define	ZFS_IOC_COMPAT_FAIL	255

#define	ZFS_IOCREQ(ioreq)	((ioreq) & 0xff)

typedef struct zfs_iocparm {
	uint32_t	zfs_ioctl_version;
	uint64_t	zfs_cmd;
	uint64_t	zfs_cmd_size;
} zfs_iocparm_t;

typedef struct zinject_record_v15 {
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
} zinject_record_v15_t;

typedef struct zfs_cmd_v15 {
	char		zc_name[MAXPATHLEN];
	char		zc_value[MAXPATHLEN];
	char		zc_string[MAXNAMELEN];
	uint64_t	zc_guid;
	uint64_t	zc_nvlist_conf;		/* really (char *) */
	uint64_t	zc_nvlist_conf_size;
	uint64_t	zc_nvlist_src;		/* really (char *) */
	uint64_t	zc_nvlist_src_size;
	uint64_t	zc_nvlist_dst;		/* really (char *) */
	uint64_t	zc_nvlist_dst_size;
	uint64_t	zc_cookie;
	uint64_t	zc_objset_type;
	uint64_t	zc_perm_action;
	uint64_t 	zc_history;		/* really (char *) */
	uint64_t 	zc_history_len;
	uint64_t	zc_history_offset;
	uint64_t	zc_obj;
	zfs_share_t	zc_share;
	uint64_t	zc_jailid;
	dmu_objset_stats_t zc_objset_stats;
	struct drr_begin zc_begin_record;
	zinject_record_v15_t zc_inject_record;
} zfs_cmd_v15_t;

typedef struct zinject_record_v28 {
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
} zinject_record_v28_t;

typedef struct zfs_cmd_v28 {
	char		zc_name[MAXPATHLEN];
	char		zc_value[MAXPATHLEN * 2];
	char		zc_string[MAXNAMELEN];
	char		zc_top_ds[MAXPATHLEN];
	uint64_t	zc_guid;
	uint64_t	zc_nvlist_conf;		/* really (char *) */
	uint64_t	zc_nvlist_conf_size;
	uint64_t	zc_nvlist_src;		/* really (char *) */
	uint64_t	zc_nvlist_src_size;
	uint64_t	zc_nvlist_dst;		/* really (char *) */
	uint64_t	zc_nvlist_dst_size;
	uint64_t	zc_cookie;
	uint64_t	zc_objset_type;
	uint64_t	zc_perm_action;
	uint64_t 	zc_history;		/* really (char *) */
	uint64_t 	zc_history_len;
	uint64_t	zc_history_offset;
	uint64_t	zc_obj;
	uint64_t	zc_iflags;		/* internal to zfs(7fs) */
	zfs_share_t	zc_share;
	uint64_t	zc_jailid;
	dmu_objset_stats_t zc_objset_stats;
	struct drr_begin zc_begin_record;
	zinject_record_v28_t zc_inject_record;
	boolean_t	zc_defer_destroy;
	boolean_t	zc_temphold;
	uint64_t	zc_action_handle;
	int		zc_cleanup_fd;
	uint8_t		zc_simple;
	uint8_t		zc_pad[3];		/* alignment */
	uint64_t	zc_sendobj;
	uint64_t	zc_fromobj;
	uint64_t	zc_createtxg;
	zfs_stat_t	zc_stat;
} zfs_cmd_v28_t;

typedef struct zinject_record_deadman {
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
} zinject_record_deadman_t;

typedef struct zfs_cmd_deadman {
	char		zc_name[MAXPATHLEN];
	char		zc_value[MAXPATHLEN * 2];
	char		zc_string[MAXNAMELEN];
	char		zc_top_ds[MAXPATHLEN];
	uint64_t	zc_guid;
	uint64_t	zc_nvlist_conf;		/* really (char *) */
	uint64_t	zc_nvlist_conf_size;
	uint64_t	zc_nvlist_src;		/* really (char *) */
	uint64_t	zc_nvlist_src_size;
	uint64_t	zc_nvlist_dst;		/* really (char *) */
	uint64_t	zc_nvlist_dst_size;
	uint64_t	zc_cookie;
	uint64_t	zc_objset_type;
	uint64_t	zc_perm_action;
	uint64_t 	zc_history;		/* really (char *) */
	uint64_t 	zc_history_len;
	uint64_t	zc_history_offset;
	uint64_t	zc_obj;
	uint64_t	zc_iflags;		/* internal to zfs(7fs) */
	zfs_share_t	zc_share;
	uint64_t	zc_jailid;
	dmu_objset_stats_t zc_objset_stats;
	struct drr_begin zc_begin_record;
	/* zc_inject_record doesn't change in libzfs_core */
	zinject_record_deadman_t zc_inject_record;
	boolean_t	zc_defer_destroy;
	boolean_t	zc_temphold;
	uint64_t	zc_action_handle;
	int		zc_cleanup_fd;
	uint8_t		zc_simple;
	uint8_t		zc_pad[3];		/* alignment */
	uint64_t	zc_sendobj;
	uint64_t	zc_fromobj;
	uint64_t	zc_createtxg;
	zfs_stat_t	zc_stat;
} zfs_cmd_deadman_t;

typedef struct zfs_cmd_zcmd {
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
	uint64_t	zc_jailid;
	dmu_objset_stats_t zc_objset_stats;
	struct drr_begin zc_begin_record;
	zinject_record_deadman_t zc_inject_record;
	boolean_t	zc_defer_destroy;
	boolean_t	zc_temphold;
	uint64_t	zc_action_handle;
	int		zc_cleanup_fd;
	uint8_t		zc_simple;
	uint8_t		zc_pad[3];		/* alignment */
	uint64_t	zc_sendobj;
	uint64_t	zc_fromobj;
	uint64_t	zc_createtxg;
	zfs_stat_t	zc_stat;
} zfs_cmd_zcmd_t;

typedef struct zfs_cmd_edbp {
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
	uint64_t	zc_jailid;
	dmu_objset_stats_t zc_objset_stats;
	struct drr_begin zc_begin_record;
	zinject_record_deadman_t zc_inject_record;
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
} zfs_cmd_edbp_t;

typedef struct zfs_cmd_resume {
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
	uint64_t	zc_jailid;
	dmu_objset_stats_t zc_objset_stats;
	dmu_replay_record_t zc_begin_record;
	zinject_record_deadman_t zc_inject_record;
	uint32_t	zc_defer_destroy;
	uint32_t	zc_flags;
	uint64_t	zc_action_handle;
	int		zc_cleanup_fd;
	uint8_t		zc_simple;
	boolean_t	zc_resumable;
	uint64_t	zc_sendobj;
	uint64_t	zc_fromobj;
	uint64_t	zc_createtxg;
	zfs_stat_t	zc_stat;
} zfs_cmd_resume_t;

typedef struct zfs_cmd_inlanes {
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
	uint64_t	zc_jailid;
	dmu_objset_stats_t zc_objset_stats;
	dmu_replay_record_t zc_begin_record;
	zinject_record_t zc_inject_record;
	uint32_t	zc_defer_destroy;
	uint32_t	zc_flags;
	uint64_t	zc_action_handle;
	int		zc_cleanup_fd;
	uint8_t		zc_simple;
	boolean_t	zc_resumable;
	uint64_t	zc_sendobj;
	uint64_t	zc_fromobj;
	uint64_t	zc_createtxg;
	zfs_stat_t	zc_stat;
} zfs_cmd_inlanes_t;

#ifdef _KERNEL
unsigned static long zfs_ioctl_v15_to_v28[] = {
	0,	/*  0 ZFS_IOC_POOL_CREATE */
	1,	/*  1 ZFS_IOC_POOL_DESTROY */
	2,	/*  2 ZFS_IOC_POOL_IMPORT */
	3,	/*  3 ZFS_IOC_POOL_EXPORT */
	4,	/*  4 ZFS_IOC_POOL_CONFIGS */
	5,	/*  5 ZFS_IOC_POOL_STATS */
	6,	/*  6 ZFS_IOC_POOL_TRYIMPORT */
	7,	/*  7 ZFS_IOC_POOL_SCRUB */
	8,	/*  8 ZFS_IOC_POOL_FREEZE */
	9,	/*  9 ZFS_IOC_POOL_UPGRADE */
	10,	/* 10 ZFS_IOC_POOL_GET_HISTORY */
	11,	/* 11 ZFS_IOC_VDEV_ADD */
	12,	/* 12 ZFS_IOC_VDEV_REMOVE */
	13,	/* 13 ZFS_IOC_VDEV_SET_STATE */
	14,	/* 14 ZFS_IOC_VDEV_ATTACH */
	15,	/* 15 ZFS_IOC_VDEV_DETACH */
	16,	/* 16 ZFS_IOC_VDEV_SETPATH */
	18,	/* 17 ZFS_IOC_OBJSET_STATS */
	19,	/* 18 ZFS_IOC_OBJSET_ZPLPROPS */
	20, 	/* 19 ZFS_IOC_DATASET_LIST_NEXT */
	21,	/* 20 ZFS_IOC_SNAPSHOT_LIST_NEXT */
	22,	/* 21 ZFS_IOC_SET_PROP */
	ZFS_IOC_COMPAT_PASS,	/* 22 ZFS_IOC_CREATE_MINOR */
	ZFS_IOC_COMPAT_PASS,	/* 23 ZFS_IOC_REMOVE_MINOR */
	23,	/* 24 ZFS_IOC_CREATE */
	24,	/* 25 ZFS_IOC_DESTROY */
	25,	/* 26 ZFS_IOC_ROLLBACK */
	26,	/* 27 ZFS_IOC_RENAME */
	27,	/* 28 ZFS_IOC_RECV */
	28,	/* 29 ZFS_IOC_SEND */
	29,	/* 30 ZFS_IOC_INJECT_FAULT */
	30,	/* 31 ZFS_IOC_CLEAR_FAULT */
	31,	/* 32 ZFS_IOC_INJECT_LIST_NEXT */
	32,	/* 33 ZFS_IOC_ERROR_LOG */
	33,	/* 34 ZFS_IOC_CLEAR */
	34,	/* 35 ZFS_IOC_PROMOTE */
	35,	/* 36 ZFS_IOC_DESTROY_SNAPS */
	36,	/* 37 ZFS_IOC_SNAPSHOT */
	37,	/* 38 ZFS_IOC_DSOBJ_TO_DSNAME */
	38,	/* 39 ZFS_IOC_OBJ_TO_PATH */
	39,	/* 40 ZFS_IOC_POOL_SET_PROPS */
	40,	/* 41 ZFS_IOC_POOL_GET_PROPS */
	41,	/* 42 ZFS_IOC_SET_FSACL */
	42,	/* 43 ZFS_IOC_GET_FSACL */
	ZFS_IOC_COMPAT_PASS,	/* 44 ZFS_IOC_ISCSI_PERM_CHECK */
	43,	/* 45 ZFS_IOC_SHARE */
	44,	/* 46 ZFS_IOC_IHNERIT_PROP */
	58,	/* 47 ZFS_IOC_JAIL */
	59,	/* 48 ZFS_IOC_UNJAIL */
	45,	/* 49 ZFS_IOC_SMB_ACL */
	46,	/* 50 ZFS_IOC_USERSPACE_ONE */
	47,	/* 51 ZFS_IOC_USERSPACE_MANY */
	48,	/* 52 ZFS_IOC_USERSPACE_UPGRADE */
	17,	/* 53 ZFS_IOC_SETFRU */
};

#else	/* KERNEL */
unsigned static long zfs_ioctl_v28_to_v15[] = {
	0,	/*  0 ZFS_IOC_POOL_CREATE */
	1,	/*  1 ZFS_IOC_POOL_DESTROY */
	2,	/*  2 ZFS_IOC_POOL_IMPORT */
	3,	/*  3 ZFS_IOC_POOL_EXPORT */
	4,	/*  4 ZFS_IOC_POOL_CONFIGS */
	5,	/*  5 ZFS_IOC_POOL_STATS */
	6,	/*  6 ZFS_IOC_POOL_TRYIMPORT */
	7,	/*  7 ZFS_IOC_POOL_SCAN */
	8,	/*  8 ZFS_IOC_POOL_FREEZE */
	9,	/*  9 ZFS_IOC_POOL_UPGRADE */
	10,	/* 10 ZFS_IOC_POOL_GET_HISTORY */
	11,	/* 11 ZFS_IOC_VDEV_ADD */
	12,	/* 12 ZFS_IOC_VDEV_REMOVE */
	13,	/* 13 ZFS_IOC_VDEV_SET_STATE */
	14,	/* 14 ZFS_IOC_VDEV_ATTACH */
	15,	/* 15 ZFS_IOC_VDEV_DETACH */
	16,	/* 16 ZFS_IOC_VDEV_SETPATH */
	53,	/* 17 ZFS_IOC_VDEV_SETFRU */
	17,	/* 18 ZFS_IOC_OBJSET_STATS */
	18,	/* 19 ZFS_IOC_OBJSET_ZPLPROPS */
	19, 	/* 20 ZFS_IOC_DATASET_LIST_NEXT */
	20,	/* 21 ZFS_IOC_SNAPSHOT_LIST_NEXT */
	21,	/* 22 ZFS_IOC_SET_PROP */
	24,	/* 23 ZFS_IOC_CREATE */
	25,	/* 24 ZFS_IOC_DESTROY */
	26,	/* 25 ZFS_IOC_ROLLBACK */
	27,	/* 26 ZFS_IOC_RENAME */
	28,	/* 27 ZFS_IOC_RECV */
	29,	/* 28 ZFS_IOC_SEND */
	30,	/* 39 ZFS_IOC_INJECT_FAULT */
	31,	/* 30 ZFS_IOC_CLEAR_FAULT */
	32,	/* 31 ZFS_IOC_INJECT_LIST_NEXT */
	33,	/* 32 ZFS_IOC_ERROR_LOG */
	34,	/* 33 ZFS_IOC_CLEAR */
	35,	/* 34 ZFS_IOC_PROMOTE */
	36,	/* 35 ZFS_IOC_DESTROY_SNAPS */
	37,	/* 36 ZFS_IOC_SNAPSHOT */
	38,	/* 37 ZFS_IOC_DSOBJ_TO_DSNAME */
	39,	/* 38 ZFS_IOC_OBJ_TO_PATH */
	40,	/* 39 ZFS_IOC_POOL_SET_PROPS */
	41,	/* 40 ZFS_IOC_POOL_GET_PROPS */
	42,	/* 41 ZFS_IOC_SET_FSACL */
	43,	/* 42 ZFS_IOC_GET_FSACL */
	45,	/* 43 ZFS_IOC_SHARE */
	46,	/* 44 ZFS_IOC_IHNERIT_PROP */
	49,	/* 45 ZFS_IOC_SMB_ACL */
	50,	/* 46 ZFS_IOC_USERSPACE_ONE */
	51,	/* 47 ZFS_IOC_USERSPACE_MANY */
	52,	/* 48 ZFS_IOC_USERSPACE_UPGRADE */
	ZFS_IOC_COMPAT_FAIL,	/* 49 ZFS_IOC_HOLD */
	ZFS_IOC_COMPAT_FAIL,	/* 50 ZFS_IOC_RELEASE */
	ZFS_IOC_COMPAT_FAIL,	/* 51 ZFS_IOC_GET_HOLDS */
	ZFS_IOC_COMPAT_FAIL,	/* 52 ZFS_IOC_OBJSET_RECVD_PROPS */
	ZFS_IOC_COMPAT_FAIL,	/* 53 ZFS_IOC_VDEV_SPLIT */
	ZFS_IOC_COMPAT_FAIL,	/* 54 ZFS_IOC_NEXT_OBJ */
	ZFS_IOC_COMPAT_FAIL,	/* 55 ZFS_IOC_DIFF */
	ZFS_IOC_COMPAT_FAIL,	/* 56 ZFS_IOC_TMP_SNAPSHOT */
	ZFS_IOC_COMPAT_FAIL,	/* 57 ZFS_IOC_OBJ_TO_STATS */
	47,	/* 58 ZFS_IOC_JAIL */
	48,	/* 59 ZFS_IOC_UNJAIL */
};
#endif	/* ! _KERNEL */

#ifdef _KERNEL
int zfs_ioctl_compat_pre(zfs_cmd_t *, int *, const int);
void zfs_ioctl_compat_post(zfs_cmd_t *, const int, const int);
nvlist_t *zfs_ioctl_compat_innvl(zfs_cmd_t *, nvlist_t *, const int,
    const int);
nvlist_t *zfs_ioctl_compat_outnvl(zfs_cmd_t *, nvlist_t *, const int,
    const int);
#else
int zcmd_ioctl_compat(int, int, zfs_cmd_t *, const int);
#endif	/* _KERNEL */
void zfs_cmd_compat_get(zfs_cmd_t *, caddr_t, const int);
void zfs_cmd_compat_put(zfs_cmd_t *, caddr_t, const int, const int);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_ZFS_IOCTL_COMPAT_H */
