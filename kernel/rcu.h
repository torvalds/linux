/*
 * Read-Copy Update definitions shared among RCU implementations.
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright IBM Corporation, 2011
 *
 * Author: Paul E. McKenney <paulmck@linux.vnet.ibm.com>
 */

#ifndef __LINUX_RCU_H
#define __LINUX_RCU_H

/*
 * debug_rcu_head_queue()/debug_rcu_head_unqueue() are used internally
 * by call_rcu() and rcu callback execution, and are therefore not part of the
 * RCU API. Leaving in rcupdate.h because they are used by all RCU flavors.
 */

#ifdef CONFIG_DEBUG_OBJECTS_RCU_HEAD
# define STATE_RCU_HEAD_READY	0
# define STATE_RCU_HEAD_QUEUED	1

extern struct debug_obj_descr rcuhead_debug_descr;

static inline void debug_rcu_head_queue(struct rcu_head *head)
{
	WARN_ON_ONCE((unsigned long)head & 0x3);
	debug_object_activate(head, &rcuhead_debug_descr);
	debug_object_active_state(head, &rcuhead_debug_descr,
				  STATE_RCU_HEAD_READY,
				  STATE_RCU_HEAD_QUEUED);
}

static inline void debug_rcu_head_unqueue(struct rcu_head *head)
{
	debug_object_active_state(head, &rcuhead_debug_descr,
				  STATE_RCU_HEAD_QUEUED,
				  STATE_RCU_HEAD_READY);
	debug_object_deactivate(head, &rcuhead_debug_descr);
}
#else	/* !CONFIG_DEBUG_OBJECTS_RCU_HEAD */
static inline void debug_rcu_head_queue(struct rcu_head *head)
{
}

static inline void debug_rcu_head_unqueue(struct rcu_head *head)
{
}
#endif	/* #else !CONFIG_DEBUG_OBJECTS_RCU_HEAD */

extern void kfree(const void *);

static inline void __rcu_reclaim(struct rcu_head *head)
{
	unsigned long offset = (unsigned long)head->func;

	if (__is_kfree_rcu_offset(offset)) {
		trace_rcu_invoke_kfree_callback(head, offset);
		kfree((void *)head - offset);
	} else {
		trace_rcu_invoke_callback(head);
		head->func(head);
	}
}

#endif /* __LINUX_RCU_H */
