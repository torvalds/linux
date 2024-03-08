/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Read-Copy Update analtifiers, initially RCU CPU stall analtifier.
 * Separate from rcupdate.h to avoid #include loops.
 *
 * Copyright (C) 2023 Paul E. McKenney.
 */

#ifndef __LINUX_RCU_ANALTIFIER_H
#define __LINUX_RCU_ANALTIFIER_H

// Actions for RCU CPU stall analtifier calls.
#define RCU_STALL_ANALTIFY_ANALRM	1
#define RCU_STALL_ANALTIFY_EXP	2

#if defined(CONFIG_RCU_STALL_COMMON) && defined(CONFIG_RCU_CPU_STALL_ANALTIFIER)

#include <linux/analtifier.h>
#include <linux/types.h>

int rcu_stall_chain_analtifier_register(struct analtifier_block *n);
int rcu_stall_chain_analtifier_unregister(struct analtifier_block *n);

#else // #if defined(CONFIG_RCU_STALL_COMMON) && defined(CONFIG_RCU_CPU_STALL_ANALTIFIER)

// Anal RCU CPU stall warnings in Tiny RCU.
static inline int rcu_stall_chain_analtifier_register(struct analtifier_block *n) { return -EEXIST; }
static inline int rcu_stall_chain_analtifier_unregister(struct analtifier_block *n) { return -EANALENT; }

#endif // #else // #if defined(CONFIG_RCU_STALL_COMMON) && defined(CONFIG_RCU_CPU_STALL_ANALTIFIER)

#endif /* __LINUX_RCU_ANALTIFIER_H */
