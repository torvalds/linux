/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Common syscall restarting data
 */
#ifndef __LINUX_RESTART_BLOCK_H
#define __LINUX_RESTART_BLOCK_H

#include <linux/compiler.h>
#include <linux/types.h>

struct __kernel_timespec;
struct timespec;
struct old_timespec32;
struct pollfd;

enum timespec_type {
	TT_NONE		= 0,
	TT_NATIVE	= 1,
	TT_COMPAT	= 2,
};

/*
 * System call restart block.
 */
struct restart_block {
	unsigned long arch_data;
	long (*fn)(struct restart_block *);
	union {
		/* For futex_wait and futex_wait_requeue_pi */
		struct {
			u32 __user *uaddr;
			u32 val;
			u32 flags;
			u32 bitset;
			u64 time;
			u32 __user *uaddr2;
		} futex;
		/* For nanosleep */
		struct {
			clockid_t clockid;
			enum timespec_type type;
			union {
				struct __kernel_timespec __user *rmtp;
				struct old_timespec32 __user *compat_rmtp;
			};
			u64 expires;
		} nanosleep;
		/* For poll */
		struct {
			struct pollfd __user *ufds;
			int nfds;
			int has_timeout;
			unsigned long tv_sec;
			unsigned long tv_nsec;
		} poll;
	};
};

extern long do_no_restart_syscall(struct restart_block *parm);

#endif /* __LINUX_RESTART_BLOCK_H */
