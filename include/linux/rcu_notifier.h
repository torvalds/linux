/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Read-Copy Update notifiers, initially RCU CPU stall notifier.
 * Separate from rcupdate.h to avoid #include loops.
 *
 * Copyright (C) 2023 Paul E. McKenney.
 */

#ifndef __LINUX_RCU_NOTIFIER_H
#define __LINUX_RCU_NOTIFIER_H

// Actions for RCU CPU stall notifier calls.
#define RCU_STALL_NOTIFY_NORM	1
#define RCU_STALL_NOTIFY_EXP	2

#ifdef CONFIG_RCU_STALL_COMMON

#include <linux/notifier.h>
#include <linux/types.h>

int rcu_stall_chain_notifier_register(struct notifier_block *n);
int rcu_stall_chain_notifier_unregister(struct notifier_block *n);

#else // #ifdef CONFIG_RCU_STALL_COMMON

// No RCU CPU stall warnings in Tiny RCU.
static inline int rcu_stall_chain_notifier_register(struct notifier_block *n) { return -EEXIST; }
static inline int rcu_stall_chain_notifier_unregister(struct notifier_block *n) { return -ENOENT; }

#endif // #else // #ifdef CONFIG_RCU_STALL_COMMON

#endif /* __LINUX_RCU_NOTIFIER_H */
