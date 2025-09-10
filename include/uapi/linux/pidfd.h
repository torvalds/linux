/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */

#ifndef _UAPI_LINUX_PIDFD_H
#define _UAPI_LINUX_PIDFD_H

#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/ioctl.h>

/* Flags for pidfd_open().  */
#define PIDFD_NONBLOCK	O_NONBLOCK
#define PIDFD_THREAD	O_EXCL
#ifdef __KERNEL__
#include <linux/sched.h>
#define PIDFD_STALE CLONE_PIDFD
#endif

/* Flags for pidfd_send_signal(). */
#define PIDFD_SIGNAL_THREAD		(1UL << 0)
#define PIDFD_SIGNAL_THREAD_GROUP	(1UL << 1)
#define PIDFD_SIGNAL_PROCESS_GROUP	(1UL << 2)

/* Flags for pidfd_info. */
#define PIDFD_INFO_PID			(1UL << 0) /* Always returned, even if not requested */
#define PIDFD_INFO_CREDS		(1UL << 1) /* Always returned, even if not requested */
#define PIDFD_INFO_CGROUPID		(1UL << 2) /* Always returned if available, even if not requested */
#define PIDFD_INFO_EXIT			(1UL << 3) /* Only returned if requested. */
#define PIDFD_INFO_COREDUMP		(1UL << 4) /* Only returned if requested. */

#define PIDFD_INFO_SIZE_VER0		64 /* sizeof first published struct */

/*
 * Values for @coredump_mask in pidfd_info.
 * Only valid if PIDFD_INFO_COREDUMP is set in @mask.
 *
 * Note, the @PIDFD_COREDUMP_ROOT flag indicates that the generated
 * coredump should be treated as sensitive and access should only be
 * granted to privileged users.
 */
#define PIDFD_COREDUMPED	(1U << 0) /* Did crash and... */
#define PIDFD_COREDUMP_SKIP	(1U << 1) /* coredumping generation was skipped. */
#define PIDFD_COREDUMP_USER	(1U << 2) /* coredump was done as the user. */
#define PIDFD_COREDUMP_ROOT	(1U << 3) /* coredump was done as root. */

/*
 * ...and for userland we make life simpler - PIDFD_SELF refers to the current
 * thread, PIDFD_SELF_PROCESS refers to the process thread group leader.
 *
 * For nearly all practical uses, a user will want to use PIDFD_SELF.
 */
#define PIDFD_SELF		PIDFD_SELF_THREAD
#define PIDFD_SELF_PROCESS	PIDFD_SELF_THREAD_GROUP

struct pidfd_info {
	/*
	 * This mask is similar to the request_mask in statx(2).
	 *
	 * Userspace indicates what extensions or expensive-to-calculate fields
	 * they want by setting the corresponding bits in mask. The kernel
	 * will ignore bits that it does not know about.
	 *
	 * When filling the structure, the kernel will only set bits
	 * corresponding to the fields that were actually filled by the kernel.
	 * This also includes any future extensions that might be automatically
	 * filled. If the structure size is too small to contain a field
	 * (requested or not), to avoid confusion the mask will not
	 * contain a bit for that field.
	 *
	 * As such, userspace MUST verify that mask contains the
	 * corresponding flags after the ioctl(2) returns to ensure that it is
	 * using valid data.
	 */
	__u64 mask;
	/*
	 * The information contained in the following fields might be stale at the
	 * time it is received, as the target process might have exited as soon as
	 * the IOCTL was processed, and there is no way to avoid that. However, it
	 * is guaranteed that if the call was successful, then the information was
	 * correct and referred to the intended process at the time the work was
	 * performed. */
	__u64 cgroupid;
	__u32 pid;
	__u32 tgid;
	__u32 ppid;
	__u32 ruid;
	__u32 rgid;
	__u32 euid;
	__u32 egid;
	__u32 suid;
	__u32 sgid;
	__u32 fsuid;
	__u32 fsgid;
	__s32 exit_code;
	__u32 coredump_mask;
	__u32 __spare1;
};

#define PIDFS_IOCTL_MAGIC 0xFF

#define PIDFD_GET_CGROUP_NAMESPACE            _IO(PIDFS_IOCTL_MAGIC, 1)
#define PIDFD_GET_IPC_NAMESPACE               _IO(PIDFS_IOCTL_MAGIC, 2)
#define PIDFD_GET_MNT_NAMESPACE               _IO(PIDFS_IOCTL_MAGIC, 3)
#define PIDFD_GET_NET_NAMESPACE               _IO(PIDFS_IOCTL_MAGIC, 4)
#define PIDFD_GET_PID_NAMESPACE               _IO(PIDFS_IOCTL_MAGIC, 5)
#define PIDFD_GET_PID_FOR_CHILDREN_NAMESPACE  _IO(PIDFS_IOCTL_MAGIC, 6)
#define PIDFD_GET_TIME_NAMESPACE              _IO(PIDFS_IOCTL_MAGIC, 7)
#define PIDFD_GET_TIME_FOR_CHILDREN_NAMESPACE _IO(PIDFS_IOCTL_MAGIC, 8)
#define PIDFD_GET_USER_NAMESPACE              _IO(PIDFS_IOCTL_MAGIC, 9)
#define PIDFD_GET_UTS_NAMESPACE               _IO(PIDFS_IOCTL_MAGIC, 10)
#define PIDFD_GET_INFO                        _IOWR(PIDFS_IOCTL_MAGIC, 11, struct pidfd_info)

#endif /* _UAPI_LINUX_PIDFD_H */
