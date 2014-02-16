/*
 * Specialised local-global spinlock. Can only be declared as global variables
 * to avoid overhead and keep things simple (and we don't want to start using
 * these inside dynamically allocated structures).
 *
 * "local/global locks" (lglocks) can be used to:
 *
 * - Provide fast exclusive access to per-CPU data, with exclusive access to
 *   another CPU's data allowed but possibly subject to contention, and to
 *   provide very slow exclusive access to all per-CPU data.
 * - Or to provide very fast and scalable read serialisation, and to provide
 *   very slow exclusive serialisation of data (not necessarily per-CPU data).
 *
 * Brlocks are also implemented as a short-hand notation for the latter use
 * case.
 *
 * Copyright 2009, 2010, Nick Piggin, Novell Inc.
 */
#ifndef __LINUX_LGLOCK_H
#define __LINUX_LGLOCK_H

#include <linux/spinlock.h>
#include <linux/lockdep.h>
#include <linux/percpu.h>
#include <linux/cpu.h>
#include <linux/notifier.h>

#ifdef CONFIG_DEBUG_LOCK_ALLOC
#define LOCKDEP_INIT_MAP lockdep_init_map
#else
#define LOCKDEP_INIT_MAP(a, b, c, d)
#endif

struct lglock {
	arch_spinlock_t __percpu *lock;
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	struct lock_class_key lock_key;
	struct lockdep_map    lock_dep_map;
#endif
};

#define DEFINE_LGLOCK(name)						\
	static DEFINE_PER_CPU(arch_spinlock_t, name ## _lock)		\
	= __ARCH_SPIN_LOCK_UNLOCKED;					\
	struct lglock name = { .lock = &name ## _lock }

#define DEFINE_STATIC_LGLOCK(name)					\
	static DEFINE_PER_CPU(arch_spinlock_t, name ## _lock)		\
	= __ARCH_SPIN_LOCK_UNLOCKED;					\
	static struct lglock name = { .lock = &name ## _lock }

void lg_lock_init(struct lglock *lg, char *name);
void lg_local_lock(struct lglock *lg);
void lg_local_unlock(struct lglock *lg);
void lg_local_lock_cpu(struct lglock *lg, int cpu);
void lg_local_unlock_cpu(struct lglock *lg, int cpu);
void lg_global_lock(struct lglock *lg);
void lg_global_unlock(struct lglock *lg);

#endif
