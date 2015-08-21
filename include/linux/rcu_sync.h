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

enum rcu_sync_type { RCU_SYNC, RCU_SCHED_SYNC, RCU_BH_SYNC };

/* Structure to mediate between updaters and fastpath-using readers.  */
struct rcu_sync {
	int			gp_state;
	int			gp_count;
	wait_queue_head_t	gp_wait;

	int			cb_state;
	struct rcu_head		cb_head;

	enum rcu_sync_type	gp_type;
};

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

extern void rcu_sync_init(struct rcu_sync *, enum rcu_sync_type);
extern void rcu_sync_enter(struct rcu_sync *);
extern void rcu_sync_exit(struct rcu_sync *);

#define __RCU_SYNC_INITIALIZER(name, type) {				\
		.gp_state = 0,						\
		.gp_count = 0,						\
		.gp_wait = __WAIT_QUEUE_HEAD_INITIALIZER(name.gp_wait),	\
		.cb_state = 0,						\
		.gp_type = type,					\
	}

#define	__DEFINE_RCU_SYNC(name, type)	\
	struct rcu_sync_struct name = __RCU_SYNC_INITIALIZER(name, type)

#define DEFINE_RCU_SYNC(name)		\
	__DEFINE_RCU_SYNC(name, RCU_SYNC)

#define DEFINE_RCU_SCHED_SYNC(name)	\
	__DEFINE_RCU_SYNC(name, RCU_SCHED_SYNC)

#define DEFINE_RCU_BH_SYNC(name)	\
	__DEFINE_RCU_SYNC(name, RCU_BH_SYNC)

#endif /* _LINUX_RCU_SYNC_H_ */
