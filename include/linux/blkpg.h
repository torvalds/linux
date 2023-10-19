/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_BLKPG_H
#define _LINUX_BLKPG_H

/*
 * Partition table and disk geometry handling
 */

#include <linux/compat.h>
#include <uapi/linux/blkpg.h>

#ifdef CONFIG_COMPAT
/* For 32-bit/64-bit compatibility of struct blkpg_ioctl_arg */
struct blkpg_compat_ioctl_arg {
	compat_int_t op;
	compat_int_t flags;
	compat_int_t datalen;
	compat_uptr_t data;
};
#endif

#endif /* _LINUX_BLKPG_H */
