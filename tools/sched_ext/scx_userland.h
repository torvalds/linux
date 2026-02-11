// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Meta, Inc */

#ifndef __SCX_USERLAND_COMMON_H
#define __SCX_USERLAND_COMMON_H

/*
 * An instance of a task that has been enqueued by the kernel for consumption
 * by a user space global scheduler thread.
 */
struct scx_userland_enqueued_task {
	__s32 pid;
	u64 sum_exec_runtime;
	u64 weight;
};

#endif  // __SCX_USERLAND_COMMON_H
