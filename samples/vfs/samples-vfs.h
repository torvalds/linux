/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __SAMPLES_VFS_H
#define __SAMPLES_VFS_H

#include <errno.h>
#include <linux/types.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>

#define die_errno(format, ...)                                             \
	do {                                                               \
		fprintf(stderr, "%m | %s: %d: %s: " format "\n", __FILE__, \
			__LINE__, __func__, ##__VA_ARGS__);                \
		exit(EXIT_FAILURE);                                        \
	} while (0)

struct statmount {
	__u32 size;		/* Total size, including strings */
	__u32 mnt_opts;		/* [str] Options (comma separated, escaped) */
	__u64 mask;		/* What results were written */
	__u32 sb_dev_major;	/* Device ID */
	__u32 sb_dev_minor;
	__u64 sb_magic;		/* ..._SUPER_MAGIC */
	__u32 sb_flags;		/* SB_{RDONLY,SYNCHRONOUS,DIRSYNC,LAZYTIME} */
	__u32 fs_type;		/* [str] Filesystem type */
	__u64 mnt_id;		/* Unique ID of mount */
	__u64 mnt_parent_id;	/* Unique ID of parent (for root == mnt_id) */
	__u32 mnt_id_old;	/* Reused IDs used in proc/.../mountinfo */
	__u32 mnt_parent_id_old;
	__u64 mnt_attr;		/* MOUNT_ATTR_... */
	__u64 mnt_propagation;	/* MS_{SHARED,SLAVE,PRIVATE,UNBINDABLE} */
	__u64 mnt_peer_group;	/* ID of shared peer group */
	__u64 mnt_master;	/* Mount receives propagation from this ID */
	__u64 propagate_from;	/* Propagation from in current namespace */
	__u32 mnt_root;		/* [str] Root of mount relative to root of fs */
	__u32 mnt_point;	/* [str] Mountpoint relative to current root */
	__u64 mnt_ns_id;	/* ID of the mount namespace */
	__u32 fs_subtype;	/* [str] Subtype of fs_type (if any) */
	__u32 sb_source;	/* [str] Source string of the mount */
	__u32 opt_num;		/* Number of fs options */
	__u32 opt_array;	/* [str] Array of nul terminated fs options */
	__u32 opt_sec_num;	/* Number of security options */
	__u32 opt_sec_array;	/* [str] Array of nul terminated security options */
	__u32 mnt_uidmap_num;	/* Number of uid mappings */
	__u32 mnt_uidmap;	/* [str] Array of uid mappings */
	__u32 mnt_gidmap_num;	/* Number of gid mappings */
	__u32 mnt_gidmap;	/* [str] Array of gid mappings */
	__u64 __spare2[44];
	char str[];		/* Variable size part containing strings */
};

struct mnt_id_req {
	__u32 size;
	__u32 spare;
	__u64 mnt_id;
	__u64 param;
	__u64 mnt_ns_id;
};

#ifndef MNT_ID_REQ_SIZE_VER0
#define MNT_ID_REQ_SIZE_VER0	24 /* sizeof first published struct */
#endif

#ifndef MNT_ID_REQ_SIZE_VER1
#define MNT_ID_REQ_SIZE_VER1	32 /* sizeof second published struct */
#endif

/* Get the id for a mount namespace */
#ifndef NS_GET_MNTNS_ID
#define NS_GET_MNTNS_ID _IO(0xb7, 0x5)
#endif

struct mnt_ns_info {
	__u32 size;
	__u32 nr_mounts;
	__u64 mnt_ns_id;
};

#ifndef MNT_NS_INFO_SIZE_VER0
#define MNT_NS_INFO_SIZE_VER0 16 /* size of first published struct */
#endif

#ifndef NS_MNT_GET_INFO
#define NS_MNT_GET_INFO _IOR(0xb7, 10, struct mnt_ns_info)
#endif

#ifndef NS_MNT_GET_NEXT
#define NS_MNT_GET_NEXT _IOR(0xb7, 11, struct mnt_ns_info)
#endif

#ifndef NS_MNT_GET_PREV
#define NS_MNT_GET_PREV _IOR(0xb7, 12, struct mnt_ns_info)
#endif

#ifndef PIDFD_GET_MNT_NAMESPACE
#define PIDFD_GET_MNT_NAMESPACE _IO(0xFF, 3)
#endif

#ifndef __NR_listmount
#define __NR_listmount 458
#endif

#ifndef __NR_statmount
#define __NR_statmount 457
#endif

#ifndef LSMT_ROOT
#define LSMT_ROOT		0xffffffffffffffff	/* root mount */
#endif

/* @mask bits for statmount(2) */
#ifndef STATMOUNT_SB_BASIC
#define STATMOUNT_SB_BASIC		0x00000001U /* Want/got sb_... */
#endif

#ifndef STATMOUNT_MNT_BASIC
#define STATMOUNT_MNT_BASIC		0x00000002U /* Want/got mnt_... */
#endif

#ifndef STATMOUNT_PROPAGATE_FROM
#define STATMOUNT_PROPAGATE_FROM	0x00000004U /* Want/got propagate_from */
#endif

#ifndef STATMOUNT_MNT_ROOT
#define STATMOUNT_MNT_ROOT		0x00000008U /* Want/got mnt_root  */
#endif

#ifndef STATMOUNT_MNT_POINT
#define STATMOUNT_MNT_POINT		0x00000010U /* Want/got mnt_point */
#endif

#ifndef STATMOUNT_FS_TYPE
#define STATMOUNT_FS_TYPE		0x00000020U /* Want/got fs_type */
#endif

#ifndef STATMOUNT_MNT_NS_ID
#define STATMOUNT_MNT_NS_ID		0x00000040U /* Want/got mnt_ns_id */
#endif

#ifndef STATMOUNT_MNT_OPTS
#define STATMOUNT_MNT_OPTS		0x00000080U /* Want/got mnt_opts */
#endif

#ifndef STATMOUNT_FS_SUBTYPE
#define STATMOUNT_FS_SUBTYPE		0x00000100U /* Want/got fs_subtype */
#endif

#ifndef STATMOUNT_SB_SOURCE
#define STATMOUNT_SB_SOURCE		0x00000200U /* Want/got sb_source */
#endif

#ifndef STATMOUNT_OPT_ARRAY
#define STATMOUNT_OPT_ARRAY		0x00000400U /* Want/got opt_... */
#endif

#ifndef STATMOUNT_OPT_SEC_ARRAY
#define STATMOUNT_OPT_SEC_ARRAY		0x00000800U /* Want/got opt_sec... */
#endif

#ifndef STATX_MNT_ID_UNIQUE
#define STATX_MNT_ID_UNIQUE 0x00004000U /* Want/got extended stx_mount_id */
#endif

#ifndef STATMOUNT_MNT_UIDMAP
#define STATMOUNT_MNT_UIDMAP		0x00002000U	/* Want/got uidmap... */
#endif

#ifndef STATMOUNT_MNT_GIDMAP
#define STATMOUNT_MNT_GIDMAP		0x00004000U	/* Want/got gidmap... */
#endif

#ifndef MOUNT_ATTR_RDONLY
#define MOUNT_ATTR_RDONLY	0x00000001 /* Mount read-only */
#endif

#ifndef MOUNT_ATTR_NOSUID
#define MOUNT_ATTR_NOSUID	0x00000002 /* Ignore suid and sgid bits */
#endif

#ifndef MOUNT_ATTR_NODEV
#define MOUNT_ATTR_NODEV	0x00000004 /* Disallow access to device special files */
#endif

#ifndef MOUNT_ATTR_NOEXEC
#define MOUNT_ATTR_NOEXEC	0x00000008 /* Disallow program execution */
#endif

#ifndef MOUNT_ATTR__ATIME
#define MOUNT_ATTR__ATIME	0x00000070 /* Setting on how atime should be updated */
#endif

#ifndef MOUNT_ATTR_RELATIME
#define MOUNT_ATTR_RELATIME	0x00000000 /* - Update atime relative to mtime/ctime. */
#endif

#ifndef MOUNT_ATTR_NOATIME
#define MOUNT_ATTR_NOATIME	0x00000010 /* - Do not update access times. */
#endif

#ifndef MOUNT_ATTR_STRICTATIME
#define MOUNT_ATTR_STRICTATIME	0x00000020 /* - Always perform atime updates */
#endif

#ifndef MOUNT_ATTR_NODIRATIME
#define MOUNT_ATTR_NODIRATIME	0x00000080 /* Do not update directory access times */
#endif

#ifndef MOUNT_ATTR_IDMAP
#define MOUNT_ATTR_IDMAP	0x00100000 /* Idmap mount to @userns_fd in struct mount_attr. */
#endif

#ifndef MOUNT_ATTR_NOSYMFOLLOW
#define MOUNT_ATTR_NOSYMFOLLOW	0x00200000 /* Do not follow symlinks */
#endif

#ifndef MS_RDONLY
#define MS_RDONLY	 1	/* Mount read-only */
#endif

#ifndef MS_SYNCHRONOUS
#define MS_SYNCHRONOUS	16	/* Writes are synced at once */
#endif

#ifndef MS_MANDLOCK
#define MS_MANDLOCK	64	/* Allow mandatory locks on an FS */
#endif

#ifndef MS_DIRSYNC
#define MS_DIRSYNC	128	/* Directory modifications are synchronous */
#endif

#ifndef MS_UNBINDABLE
#define MS_UNBINDABLE	(1<<17)	/* change to unbindable */
#endif

#ifndef MS_PRIVATE
#define MS_PRIVATE	(1<<18)	/* change to private */
#endif

#ifndef MS_SLAVE
#define MS_SLAVE	(1<<19)	/* change to slave */
#endif

#ifndef MS_SHARED
#define MS_SHARED	(1<<20)	/* change to shared */
#endif

#ifndef MS_LAZYTIME
#define MS_LAZYTIME	(1<<25) /* Update the on-disk [acm]times lazily */
#endif

#endif /* __SAMPLES_VFS_H */
