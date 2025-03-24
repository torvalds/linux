#ifndef _UAPI_LINUX_MOUNT_H
#define _UAPI_LINUX_MOUNT_H

#include <linux/types.h>

/*
 * These are the fs-independent mount-flags: up to 32 flags are supported
 *
 * Usage of these is restricted within the kernel to core mount(2) code and
 * callers of sys_mount() only.  Filesystems should be using the SB_*
 * equivalent instead.
 */
#define MS_RDONLY	 1	/* Mount read-only */
#define MS_NOSUID	 2	/* Ignore suid and sgid bits */
#define MS_NODEV	 4	/* Disallow access to device special files */
#define MS_NOEXEC	 8	/* Disallow program execution */
#define MS_SYNCHRONOUS	16	/* Writes are synced at once */
#define MS_REMOUNT	32	/* Alter flags of a mounted FS */
#define MS_MANDLOCK	64	/* Allow mandatory locks on an FS */
#define MS_DIRSYNC	128	/* Directory modifications are synchronous */
#define MS_NOSYMFOLLOW	256	/* Do not follow symlinks */
#define MS_NOATIME	1024	/* Do not update access times. */
#define MS_NODIRATIME	2048	/* Do not update directory access times */
#define MS_BIND		4096
#define MS_MOVE		8192
#define MS_REC		16384
#define MS_VERBOSE	32768	/* War is peace. Verbosity is silence.
				   MS_VERBOSE is deprecated. */
#define MS_SILENT	32768
#define MS_POSIXACL	(1<<16)	/* VFS does not apply the umask */
#define MS_UNBINDABLE	(1<<17)	/* change to unbindable */
#define MS_PRIVATE	(1<<18)	/* change to private */
#define MS_SLAVE	(1<<19)	/* change to slave */
#define MS_SHARED	(1<<20)	/* change to shared */
#define MS_RELATIME	(1<<21)	/* Update atime relative to mtime/ctime. */
#define MS_KERNMOUNT	(1<<22) /* this is a kern_mount call */
#define MS_I_VERSION	(1<<23) /* Update inode I_version field */
#define MS_STRICTATIME	(1<<24) /* Always perform atime updates */
#define MS_LAZYTIME	(1<<25) /* Update the on-disk [acm]times lazily */

/* These sb flags are internal to the kernel */
#define MS_SUBMOUNT     (1<<26)
#define MS_NOREMOTELOCK	(1<<27)
#define MS_NOSEC	(1<<28)
#define MS_BORN		(1<<29)
#define MS_ACTIVE	(1<<30)
#define MS_NOUSER	(1<<31)

/*
 * Superblock flags that can be altered by MS_REMOUNT
 */
#define MS_RMT_MASK	(MS_RDONLY|MS_SYNCHRONOUS|MS_MANDLOCK|MS_I_VERSION|\
			 MS_LAZYTIME)

/*
 * Old magic mount flag and mask
 */
#define MS_MGC_VAL 0xC0ED0000
#define MS_MGC_MSK 0xffff0000

/*
 * open_tree() flags.
 */
#define OPEN_TREE_CLONE		1		/* Clone the target tree and attach the clone */
#define OPEN_TREE_CLOEXEC	O_CLOEXEC	/* Close the file on execve() */

/*
 * move_mount() flags.
 */
#define MOVE_MOUNT_F_SYMLINKS		0x00000001 /* Follow symlinks on from path */
#define MOVE_MOUNT_F_AUTOMOUNTS		0x00000002 /* Follow automounts on from path */
#define MOVE_MOUNT_F_EMPTY_PATH		0x00000004 /* Empty from path permitted */
#define MOVE_MOUNT_T_SYMLINKS		0x00000010 /* Follow symlinks on to path */
#define MOVE_MOUNT_T_AUTOMOUNTS		0x00000020 /* Follow automounts on to path */
#define MOVE_MOUNT_T_EMPTY_PATH		0x00000040 /* Empty to path permitted */
#define MOVE_MOUNT_SET_GROUP		0x00000100 /* Set sharing group instead */
#define MOVE_MOUNT_BENEATH		0x00000200 /* Mount beneath top mount */
#define MOVE_MOUNT__MASK		0x00000377

/*
 * fsopen() flags.
 */
#define FSOPEN_CLOEXEC		0x00000001

/*
 * fspick() flags.
 */
#define FSPICK_CLOEXEC		0x00000001
#define FSPICK_SYMLINK_NOFOLLOW	0x00000002
#define FSPICK_NO_AUTOMOUNT	0x00000004
#define FSPICK_EMPTY_PATH	0x00000008

/*
 * The type of fsconfig() call made.
 */
enum fsconfig_command {
	FSCONFIG_SET_FLAG	= 0,	/* Set parameter, supplying no value */
	FSCONFIG_SET_STRING	= 1,	/* Set parameter, supplying a string value */
	FSCONFIG_SET_BINARY	= 2,	/* Set parameter, supplying a binary blob value */
	FSCONFIG_SET_PATH	= 3,	/* Set parameter, supplying an object by path */
	FSCONFIG_SET_PATH_EMPTY	= 4,	/* Set parameter, supplying an object by (empty) path */
	FSCONFIG_SET_FD		= 5,	/* Set parameter, supplying an object by fd */
	FSCONFIG_CMD_CREATE	= 6,	/* Create new or reuse existing superblock */
	FSCONFIG_CMD_RECONFIGURE = 7,	/* Invoke superblock reconfiguration */
	FSCONFIG_CMD_CREATE_EXCL = 8,	/* Create new superblock, fail if reusing existing superblock */
};

/*
 * fsmount() flags.
 */
#define FSMOUNT_CLOEXEC		0x00000001

/*
 * Mount attributes.
 */
#define MOUNT_ATTR_RDONLY	0x00000001 /* Mount read-only */
#define MOUNT_ATTR_NOSUID	0x00000002 /* Ignore suid and sgid bits */
#define MOUNT_ATTR_NODEV	0x00000004 /* Disallow access to device special files */
#define MOUNT_ATTR_NOEXEC	0x00000008 /* Disallow program execution */
#define MOUNT_ATTR__ATIME	0x00000070 /* Setting on how atime should be updated */
#define MOUNT_ATTR_RELATIME	0x00000000 /* - Update atime relative to mtime/ctime. */
#define MOUNT_ATTR_NOATIME	0x00000010 /* - Do not update access times. */
#define MOUNT_ATTR_STRICTATIME	0x00000020 /* - Always perform atime updates */
#define MOUNT_ATTR_NODIRATIME	0x00000080 /* Do not update directory access times */
#define MOUNT_ATTR_IDMAP	0x00100000 /* Idmap mount to @userns_fd in struct mount_attr. */
#define MOUNT_ATTR_NOSYMFOLLOW	0x00200000 /* Do not follow symlinks */

/*
 * mount_setattr()
 */
struct mount_attr {
	__u64 attr_set;
	__u64 attr_clr;
	__u64 propagation;
	__u64 userns_fd;
};

/* List of all mount_attr versions. */
#define MOUNT_ATTR_SIZE_VER0	32 /* sizeof first published struct */


/*
 * Structure for getting mount/superblock/filesystem info with statmount(2).
 *
 * The interface is similar to statx(2): individual fields or groups can be
 * selected with the @mask argument of statmount().  Kernel will set the @mask
 * field according to the supported fields.
 *
 * If string fields are selected, then the caller needs to pass a buffer that
 * has space after the fixed part of the structure.  Nul terminated strings are
 * copied there and offsets relative to @str are stored in the relevant fields.
 * If the buffer is too small, then EOVERFLOW is returned.  The actually used
 * size is returned in @size.
 */
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
	__u64 __spare2[46];
	char str[];		/* Variable size part containing strings */
};

/*
 * Structure for passing mount ID and miscellaneous parameters to statmount(2)
 * and listmount(2).
 *
 * For statmount(2) @param represents the request mask.
 * For listmount(2) @param represents the last listed mount id (or zero).
 */
struct mnt_id_req {
	__u32 size;
	__u32 spare;
	__u64 mnt_id;
	__u64 param;
	__u64 mnt_ns_id;
};

/* List of all mnt_id_req versions. */
#define MNT_ID_REQ_SIZE_VER0	24 /* sizeof first published struct */
#define MNT_ID_REQ_SIZE_VER1	32 /* sizeof second published struct */

/*
 * @mask bits for statmount(2)
 */
#define STATMOUNT_SB_BASIC		0x00000001U     /* Want/got sb_... */
#define STATMOUNT_MNT_BASIC		0x00000002U	/* Want/got mnt_... */
#define STATMOUNT_PROPAGATE_FROM	0x00000004U	/* Want/got propagate_from */
#define STATMOUNT_MNT_ROOT		0x00000008U	/* Want/got mnt_root  */
#define STATMOUNT_MNT_POINT		0x00000010U	/* Want/got mnt_point */
#define STATMOUNT_FS_TYPE		0x00000020U	/* Want/got fs_type */
#define STATMOUNT_MNT_NS_ID		0x00000040U	/* Want/got mnt_ns_id */
#define STATMOUNT_MNT_OPTS		0x00000080U	/* Want/got mnt_opts */
#define STATMOUNT_FS_SUBTYPE		0x00000100U	/* Want/got fs_subtype */
#define STATMOUNT_SB_SOURCE		0x00000200U	/* Want/got sb_source */
#define STATMOUNT_OPT_ARRAY		0x00000400U	/* Want/got opt_... */
#define STATMOUNT_OPT_SEC_ARRAY		0x00000800U	/* Want/got opt_sec... */

/*
 * Special @mnt_id values that can be passed to listmount
 */
#define LSMT_ROOT		0xffffffffffffffff	/* root mount */
#define LISTMOUNT_REVERSE	(1 << 0) /* List later mounts first */

#endif /* _UAPI_LINUX_MOUNT_H */
