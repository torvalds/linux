#ifndef _UAPI_LINUX_MOUNT_H
#define _UAPI_LINUX_MOUNT_H

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

#endif /* _UAPI_LINUX_MOUNT_H */
