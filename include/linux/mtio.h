/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MTIO_COMPAT_H
#define _LINUX_MTIO_COMPAT_H

#include <linux/compat.h>
#include <uapi/linux/mtio.h>
#include <linux/uaccess.h>

/*
 * helper functions for implementing compat ioctls on the four tape
 * drivers: we define the 32-bit layout of each incompatible structure,
 * plus a wrapper function to copy it to user space in either format.
 */

struct	mtget32 {
	s32	mt_type;
	s32	mt_resid;
	s32	mt_dsreg;
	s32	mt_gstat;
	s32	mt_erreg;
	s32	mt_fileno;
	s32	mt_blkno;
};
#define	MTIOCGET32	_IOR('m', 2, struct mtget32)

struct	mtpos32 {
	s32 	mt_blkno;
};
#define	MTIOCPOS32	_IOR('m', 3, struct mtpos32)

static inline int put_user_mtget(void __user *u, struct mtget *k)
{
	struct mtget32 k32 = {
		.mt_type   = k->mt_type,
		.mt_resid  = k->mt_resid,
		.mt_dsreg  = k->mt_dsreg,
		.mt_gstat  = k->mt_gstat,
		.mt_erreg  = k->mt_erreg,
		.mt_fileno = k->mt_fileno,
		.mt_blkno  = k->mt_blkno,
	};
	int ret;

	if (in_compat_syscall())
		ret = copy_to_user(u, &k32, sizeof(k32));
	else
		ret = copy_to_user(u, k, sizeof(*k));

	return ret ? -EFAULT : 0;
}

static inline int put_user_mtpos(void __user *u, struct mtpos *k)
{
	if (in_compat_syscall())
		return put_user(k->mt_blkno, (u32 __user *)u);
	else
		return put_user(k->mt_blkno, (long __user *)u);
}

#endif
