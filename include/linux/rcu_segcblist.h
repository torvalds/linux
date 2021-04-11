/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * RCU segmented callback lists
 *
 * This seemingly RCU-private file must be available to SRCU users
 * because the size of the TREE SRCU srcu_struct structure depends
 * on these definitions.
 *
 * Copyright IBM Corporation, 2017
 *
 * Authors: Paul E. McKenney <paulmck@linux.net.ibm.com>
 */

#ifndef __INCLUDE_LINUX_RCU_SEGCBLIST_H
#define __INCLUDE_LINUX_RCU_SEGCBLIST_H

#include <linux/types.h>
#include <linux/atomic.h>

/* Simple unsegmented callback lists. */
struct rcu_cblist {
	struct rcu_head *head;
	struct rcu_head **tail;
	long len;
};

#define RCU_CBLIST_INITIALIZER(n) { .head = NULL, .tail = &n.head }

/* Complicated segmented callback lists.  ;-) */

/*
 * Index values for segments in rcu_segcblist structure.
 *
 * The segments are as follows:
 *
 * [head, *tails[RCU_DONE_TAIL]):
 *	Callbacks whose grace period has elapsed, and thus can be invoked.
 * [*tails[RCU_DONE_TAIL], *tails[RCU_WAIT_TAIL]):
 *	Callbacks waiting for the current GP from the current CPU's viewpoint.
 * [*tails[RCU_WAIT_TAIL], *tails[RCU_NEXT_READY_TAIL]):
 *	Callbacks that arrived before the next GP started, again from
 *	the current CPU's viewpoint.  These can be handled by the next GP.
 * [*tails[RCU_NEXT_READY_TAIL], *tails[RCU_NEXT_TAIL]):
 *	Callbacks that might have arrived after the next GP started.
 *	There is some uncertainty as to when a given GP starts and
 *	ends, but a CPU knows the exact times if it is the one starting
 *	or ending the GP.  Other CPUs know that the previous GP ends
 *	before the next one starts.
 *
 * Note that RCU_WAIT_TAIL cannot be empty unless RCU_NEXT_READY_TAIL is also
 * empty.
 *
 * The ->gp_seq[] array contains the grace-period number at which the
 * corresponding segment of callbacks will be ready to invoke.  A given
 * element of this array is meaningful only when the corresponding segment
 * is non-empty, and it is never valid for RCU_DONE_TAIL (whose callbacks
 * are already ready to invoke) or for RCU_NEXT_TAIL (whose callbacks have
 * not yet been assigned a grace-period number).
 */
#define RCU_DONE_TAIL		0	/* Also RCU_WAIT head. */
#define RCU_WAIT_TAIL		1	/* Also RCU_NEXT_READY head. */
#define RCU_NEXT_READY_TAIL	2	/* Also RCU_NEXT head. */
#define RCU_NEXT_TAIL		3
#define RCU_CBLIST_NSEGS	4


/*
 *                     ==NOCB Offloading state machine==
 *
 *
 *  ----------------------------------------------------------------------------
 *  |                         SEGCBLIST_SOFTIRQ_ONLY                           |
 *  |                                                                          |
 *  |  Callbacks processed by rcu_core() from softirqs or local                |
 *  |  rcuc kthread, without holding nocb_lock.                                |
 *  ----------------------------------------------------------------------------
 *                                         |
 *                                         v
 *  ----------------------------------------------------------------------------
 *  |                        SEGCBLIST_OFFLOADED                               |
 *  |                                                                          |
 *  | Callbacks processed by rcu_core() from softirqs or local                 |
 *  | rcuc kthread, while holding nocb_lock. Waking up CB and GP kthreads,     |
 *  | allowing nocb_timer to be armed.                                         |
 *  ----------------------------------------------------------------------------
 *                                         |
 *                                         v
 *                        -----------------------------------
 *                        |                                 |
 *                        v                                 v
 *  ---------------------------------------  ----------------------------------|
 *  |        SEGCBLIST_OFFLOADED |        |  |     SEGCBLIST_OFFLOADED |       |
 *  |        SEGCBLIST_KTHREAD_CB         |  |     SEGCBLIST_KTHREAD_GP        |
 *  |                                     |  |                                 |
 *  |                                     |  |                                 |
 *  | CB kthread woke up and              |  | GP kthread woke up and          |
 *  | acknowledged SEGCBLIST_OFFLOADED.   |  | acknowledged SEGCBLIST_OFFLOADED|
 *  | Processes callbacks concurrently    |  |                                 |
 *  | with rcu_core(), holding            |  |                                 |
 *  | nocb_lock.                          |  |                                 |
 *  ---------------------------------------  -----------------------------------
 *                        |                                 |
 *                        -----------------------------------
 *                                         |
 *                                         v
 *  |--------------------------------------------------------------------------|
 *  |                           SEGCBLIST_OFFLOADED |                          |
 *  |                           SEGCBLIST_KTHREAD_CB |                         |
 *  |                           SEGCBLIST_KTHREAD_GP                           |
 *  |                                                                          |
 *  |   Kthreads handle callbacks holding nocb_lock, local rcu_core() stops    |
 *  |   handling callbacks. Enable bypass queueing.                            |
 *  ----------------------------------------------------------------------------
 */



/*
 *                       ==NOCB De-Offloading state machine==
 *
 *
 *  |--------------------------------------------------------------------------|
 *  |                           SEGCBLIST_OFFLOADED |                          |
 *  |                           SEGCBLIST_KTHREAD_CB |                         |
 *  |                           SEGCBLIST_KTHREAD_GP                           |
 *  |                                                                          |
 *  |   CB/GP kthreads handle callbacks holding nocb_lock, local rcu_core()    |
 *  |   ignores callbacks. Bypass enqueue is enabled.                          |
 *  ----------------------------------------------------------------------------
 *                                      |
 *                                      v
 *  |--------------------------------------------------------------------------|
 *  |                           SEGCBLIST_KTHREAD_CB |                         |
 *  |                           SEGCBLIST_KTHREAD_GP                           |
 *  |                                                                          |
 *  |   CB/GP kthreads and local rcu_core() handle callbacks concurrently      |
 *  |   holding nocb_lock. Wake up CB and GP kthreads if necessary. Disable    |
 *  |   bypass enqueue.                                                        |
 *  ----------------------------------------------------------------------------
 *                                      |
 *                                      v
 *                     -----------------------------------
 *                     |                                 |
 *                     v                                 v
 *  ---------------------------------------------------------------------------|
 *  |                                                                          |
 *  |        SEGCBLIST_KTHREAD_CB         |       SEGCBLIST_KTHREAD_GP         |
 *  |                                     |                                    |
 *  | GP kthread woke up and              |   CB kthread woke up and           |
 *  | acknowledged the fact that          |   acknowledged the fact that       |
 *  | SEGCBLIST_OFFLOADED got cleared.    |   SEGCBLIST_OFFLOADED got cleared. |
 *  |                                     |   The CB kthread goes to sleep     |
 *  | The callbacks from the target CPU   |   until it ever gets re-offloaded. |
 *  | will be ignored from the GP kthread |                                    |
 *  | loop.                               |                                    |
 *  ----------------------------------------------------------------------------
 *                      |                                 |
 *                      -----------------------------------
 *                                      |
 *                                      v
 *  ----------------------------------------------------------------------------
 *  |                                   0                                      |
 *  |                                                                          |
 *  | Callbacks processed by rcu_core() from softirqs or local                 |
 *  | rcuc kthread, while holding nocb_lock. Forbid nocb_timer to be armed.    |
 *  | Flush pending nocb_timer. Flush nocb bypass callbacks.                   |
 *  ----------------------------------------------------------------------------
 *                                      |
 *                                      v
 *  ----------------------------------------------------------------------------
 *  |                         SEGCBLIST_SOFTIRQ_ONLY                           |
 *  |                                                                          |
 *  |  Callbacks processed by rcu_core() from softirqs or local                |
 *  |  rcuc kthread, without holding nocb_lock.                                |
 *  ----------------------------------------------------------------------------
 */
#define SEGCBLIST_ENABLED	BIT(0)
#define SEGCBLIST_SOFTIRQ_ONLY	BIT(1)
#define SEGCBLIST_KTHREAD_CB	BIT(2)
#define SEGCBLIST_KTHREAD_GP	BIT(3)
#define SEGCBLIST_OFFLOADED	BIT(4)

struct rcu_segcblist {
	struct rcu_head *head;
	struct rcu_head **tails[RCU_CBLIST_NSEGS];
	unsigned long gp_seq[RCU_CBLIST_NSEGS];
#ifdef CONFIG_RCU_NOCB_CPU
	atomic_long_t len;
#else
	long len;
#endif
	long seglen[RCU_CBLIST_NSEGS];
	u8 flags;
};

#define RCU_SEGCBLIST_INITIALIZER(n) \
{ \
	.head = NULL, \
	.tails[RCU_DONE_TAIL] = &n.head, \
	.tails[RCU_WAIT_TAIL] = &n.head, \
	.tails[RCU_NEXT_READY_TAIL] = &n.head, \
	.tails[RCU_NEXT_TAIL] = &n.head, \
}

#endif /* __INCLUDE_LINUX_RCU_SEGCBLIST_H */
