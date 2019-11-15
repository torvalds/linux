/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _CLONE3_SELFTESTS_H
#define _CLONE3_SELFTESTS_H

#define _GNU_SOURCE
#include <sched.h>
#include <stdint.h>
#include <syscall.h>
#include <linux/types.h>

#define ptr_to_u64(ptr) ((__u64)((uintptr_t)(ptr)))

#ifndef __NR_clone3
#define __NR_clone3 -1
struct clone_args {
	__aligned_u64 flags;
	__aligned_u64 pidfd;
	__aligned_u64 child_tid;
	__aligned_u64 parent_tid;
	__aligned_u64 exit_signal;
	__aligned_u64 stack;
	__aligned_u64 stack_size;
	__aligned_u64 tls;
	__aligned_u64 set_tid;
	__aligned_u64 set_tid_size;
};
#endif

static pid_t sys_clone3(struct clone_args *args, size_t size)
{
	return syscall(__NR_clone3, args, size);
}

#endif /* _CLONE3_SELFTESTS_H */
