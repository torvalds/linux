/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _FALLOC_H_
#define _FALLOC_H_

#include <uapi/linux/falloc.h>


/*
 * Space reservation ioctls and argument structure
 * are designed to be compatible with the legacy XFS ioctls.
 */
struct space_resv {
	__s16		l_type;
	__s16		l_whence;
	__s64		l_start;
	__s64		l_len;		/* len == 0 means until end of file */
	__s32		l_sysid;
	__u32		l_pid;
	__s32		l_pad[4];	/* reserved area */
};

#define FS_IOC_RESVSP		_IOW('X', 40, struct space_resv)
#define FS_IOC_RESVSP64		_IOW('X', 42, struct space_resv)

#define	FALLOC_FL_SUPPORTED_MASK	(FALLOC_FL_KEEP_SIZE |		\
					 FALLOC_FL_PUNCH_HOLE |		\
					 FALLOC_FL_COLLAPSE_RANGE |	\
					 FALLOC_FL_ZERO_RANGE |		\
					 FALLOC_FL_INSERT_RANGE |	\
					 FALLOC_FL_UNSHARE_RANGE)

/* on ia32 l_start is on a 32-bit boundary */
#if defined(CONFIG_X86_64)
struct space_resv_32 {
	__s16		l_type;
	__s16		l_whence;
	__s64		l_start	__attribute__((packed));
			/* len == 0 means until end of file */
	__s64		l_len __attribute__((packed));
	__s32		l_sysid;
	__u32		l_pid;
	__s32		l_pad[4];	/* reserve area */
};

#define FS_IOC_RESVSP_32		_IOW ('X', 40, struct space_resv_32)
#define FS_IOC_RESVSP64_32	_IOW ('X', 42, struct space_resv_32)

int compat_ioctl_preallocate(struct file *, struct space_resv_32 __user *);

#endif

#endif /* _FALLOC_H_ */
