/*
 * RCU-based infrastructure for lightweight reader-writer locking
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 * Copyright (c) 2015, Red Hat, Inc.
 *
 * Author: Oleg Nesterov <oleg@redhat.com>
 */

#ifndef _LINUX_RCU_SYNC_H_
#define _LINUX_RCU_SYNC_H_

#include <linux/wait.h>
#include <linux/rcupdate.h>

/* Structure to mediate between updaters and fastpath-using readers.  */
struct rcu_sync {
	int			gp_state;
	int			gp_count;
	wait_queue_head_t	gp_wait;

	int			cb_state;
	struct rcu_head		cb_head;

	void (*sync)(void);
	void (*call)(struct rcu_head *, void (*)(struct rcu_head *));
};

#define ___RCU_SYNC_INIT(name)						\
	.gp_state = 0,							\
	.gp_count = 0,							\
	.gp_wait = __WAIT_QUEUE_HEAD_INITIALIZER(name.gp_wait),		\
	.cb_state = 0

#define __RCU_SCHED_SYNC_INIT(name) {					\
	___RCU_SYNC_INIT(name),						\
	.sync = synchronize_sched,					\
	.call = call_rcu_sched,						\
}

#define __RCU_BH_SYNC_INIT(name) {					\
	___RCU_SYNC_INIT(name),						\
	.sync = synchronize_rcu_bh,					\
	.call = call_rcu_bh,						\
}

#define __RCU_SYNC_INIT(name) {						\
	___RCU_SYNC_INIT(name),						\
	.sync = synchronize_rcu,					\
	.call = call_rcu,						\
}

#define DEFINE_RCU_SCHED_SYNC(name)					\
	struct rcu_sync name = __RCU_SCHED_SYNC_INIT(name)

#define DEFINE_RCU_BH_SYNC(name)					\
	struct rcu_sync name = __RCU_BH_SYNC_INIT(name)

#define DEFINE_RCU_SYNC(name)						\
	struct rcu_sync name = __RCU_SYNC_INIT(name)

/**
 * rcu_sync_is_idle() - Are readers permitted to use their fastpaths?
 * @rsp: Pointer to rcu_sync structure to use for synchronization
 *
 * Returns true if readers are permitted to use their fastpaths.
 * Must be invoked within an RCU read-side critical section whose
 * flavor matches that of the rcu_sync struture.
 */
static inline bool rcu_sync_is_idle(struct rcu_sync *rsp)
{
	return !rsp->gp_state; /* GP_IDLE */
}

enum rcu_sync_type { RCU_SYNC, RCU_SCHED_SYNC, RCU_BH_SYNC };

extern void rcu_sync_init(struct rcu_sync *, enum rcu_sync_type);
extern void rcu_sync_enter(struct rcu_sync *);
extern void rcu_sync_exit(struct rcu_sync *);

#endif /* _LINUX_RCU_SYNC_H_ */
