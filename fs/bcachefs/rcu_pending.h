/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_RCU_PENDING_H
#define _LINUX_RCU_PENDING_H

struct rcu_pending;
typedef void (*rcu_pending_process_fn)(struct rcu_pending *, struct rcu_head *);

struct rcu_pending_pcpu;

struct rcu_pending {
	struct rcu_pending_pcpu __percpu *p;
	struct srcu_struct		*srcu;
	rcu_pending_process_fn		process;
};

void rcu_pending_enqueue(struct rcu_pending *pending, struct rcu_head *obj);
struct rcu_head *rcu_pending_dequeue(struct rcu_pending *pending);
struct rcu_head *rcu_pending_dequeue_from_all(struct rcu_pending *pending);

void rcu_pending_exit(struct rcu_pending *pending);
int rcu_pending_init(struct rcu_pending *pending,
		     struct srcu_struct *srcu,
		     rcu_pending_process_fn process);

#endif /* _LINUX_RCU_PENDING_H */
