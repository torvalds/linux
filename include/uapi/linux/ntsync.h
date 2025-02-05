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

struct ntsync_event_args {
	__u32 manual;
	__u32 signaled;
};

#define NTSYNC_WAIT_REALTIME	0x1

struct ntsync_wait_args {
	__u64 timeout;
	__u64 objs;
	__u32 count;
	__u32 index;
	__u32 flags;
	__u32 owner;
	__u32 alert;
	__u32 pad;
};

#define NTSYNC_MAX_WAIT_COUNT 64

#define NTSYNC_IOC_CREATE_SEM		_IOW ('N', 0x80, struct ntsync_sem_args)
#define NTSYNC_IOC_WAIT_ANY		_IOWR('N', 0x82, struct ntsync_wait_args)
#define NTSYNC_IOC_WAIT_ALL		_IOWR('N', 0x83, struct ntsync_wait_args)
#define NTSYNC_IOC_CREATE_MUTEX		_IOW ('N', 0x84, struct ntsync_mutex_args)
#define NTSYNC_IOC_CREATE_EVENT		_IOW ('N', 0x87, struct ntsync_event_args)

#define NTSYNC_IOC_SEM_RELEASE		_IOWR('N', 0x81, __u32)
#define NTSYNC_IOC_MUTEX_UNLOCK		_IOWR('N', 0x85, struct ntsync_mutex_args)
#define NTSYNC_IOC_MUTEX_KILL		_IOW ('N', 0x86, __u32)
#define NTSYNC_IOC_EVENT_SET		_IOR ('N', 0x88, __u32)
#define NTSYNC_IOC_EVENT_RESET		_IOR ('N', 0x89, __u32)
#define NTSYNC_IOC_EVENT_PULSE		_IOR ('N', 0x8a, __u32)
#define NTSYNC_IOC_SEM_READ		_IOR ('N', 0x8b, struct ntsync_sem_args)
#define NTSYNC_IOC_MUTEX_READ		_IOR ('N', 0x8c, struct ntsync_mutex_args)
#define NTSYNC_IOC_EVENT_READ		_IOR ('N', 0x8d, struct ntsync_event_args)

#endif
