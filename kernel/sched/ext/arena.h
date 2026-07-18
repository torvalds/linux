/* SPDX-License-Identifier: GPL-2.0 */
/*
 * BPF extensible scheduler class: Documentation/scheduler/sched-ext.rst
 *
 * Copyright (c) 2025 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2025 Tejun Heo <tj@kernel.org>
 */
#ifndef _KERNEL_SCHED_EXT_ARENA_H
#define _KERNEL_SCHED_EXT_ARENA_H

#include <linux/types.h>

struct scx_sched;

s32 scx_arena_pool_init(struct scx_sched *sch);
void scx_arena_pool_destroy(struct scx_sched *sch);
void *scx_arena_alloc(struct scx_sched *sch, size_t size);
void scx_arena_free(struct scx_sched *sch, void *kern_va, size_t size);

#endif /* _KERNEL_SCHED_EXT_ARENA_H */
