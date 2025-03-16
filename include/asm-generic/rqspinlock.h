/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Resilient Queued Spin Lock
 *
 * (C) Copyright 2024-2025 Meta Platforms, Inc. and affiliates.
 *
 * Authors: Kumar Kartikeya Dwivedi <memxor@gmail.com>
 */
#ifndef __ASM_GENERIC_RQSPINLOCK_H
#define __ASM_GENERIC_RQSPINLOCK_H

#include <linux/types.h>

struct qspinlock;
typedef struct qspinlock rqspinlock_t;

extern void resilient_queued_spin_lock_slowpath(rqspinlock_t *lock, u32 val);

#endif /* __ASM_GENERIC_RQSPINLOCK_H */
