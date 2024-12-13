/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Kernel support for NT synchronization primitive emulation
 *
 * Copyright (C) 2021-2022 Elizabeth Figura <zfigura@codeweavers.com>
 */

#ifndef __LINUX_NTSYNC_H
#define __LINUX_NTSYNC_H

#include <linux/types.h>

struct ntsync_sem_args {
	__u32 count;
	__u32 max;
};

struct ntsync_mutex_args {
	__u32 owner;
	__u32 count;
};

#define NTSYNC_WAIT_REALTIME	0x1

struct ntsync_wait_args {
	__u64 timeout;
	__u64 objs;
	__u32 count;
	__u32 index;
	__u32 flags;
	__u32 owner;
	__u32 pad[2];
};

#define NTSYNC_MAX_WAIT_COUNT 64

#define NTSYNC_IOC_CREATE_SEM		_IOW ('N', 0x80, struct ntsync_sem_args)
#define NTSYNC_IOC_WAIT_ANY		_IOWR('N', 0x82, struct ntsync_wait_args)
#define NTSYNC_IOC_WAIT_ALL		_IOWR('N', 0x83, struct ntsync_wait_args)
#define NTSYNC_IOC_CREATE_MUTEX		_IOW ('N', 0x84, struct ntsync_mutex_args)

#define NTSYNC_IOC_SEM_RELEASE		_IOWR('N', 0x81, __u32)
#define NTSYNC_IOC_MUTEX_UNLOCK		_IOWR('N', 0x85, struct ntsync_mutex_args)

#endif
