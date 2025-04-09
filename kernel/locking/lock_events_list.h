/* SPDX-License-Identifier: GPL-2.0 */
/*
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
 * Authors: Waiman Long <longman@redhat.com>
 */

#ifndef LOCK_EVENT
#define LOCK_EVENT(name)	LOCKEVENT_ ## name,
#endif

#ifdef CONFIG_QUEUED_SPINLOCKS
#ifdef CONFIG_PARAVIRT_SPINLOCKS
/*
 * Locking events for PV qspinlock.
 */
LOCK_EVENT(pv_hash_hops)	/* Average # of hops per hashing operation */
LOCK_EVENT(pv_kick_unlock)	/* # of vCPU kicks issued at unlock time   */
LOCK_EVENT(pv_kick_wake)	/* # of vCPU kicks for pv_latency_wake	   */
LOCK_EVENT(pv_latency_kick)	/* Average latency (ns) of vCPU kick	   */
LOCK_EVENT(pv_latency_wake)	/* Average latency (ns) of kick-to-wakeup  */
LOCK_EVENT(pv_lock_stealing)	/* # of lock stealing operations	   */
LOCK_EVENT(pv_spurious_wakeup)	/* # of spurious wakeups in non-head vCPUs */
LOCK_EVENT(pv_wait_again)	/* # of wait's after queue head vCPU kick  */
LOCK_EVENT(pv_wait_early)	/* # of early vCPU wait's		   */
LOCK_EVENT(pv_wait_head)	/* # of vCPU wait's at the queue head	   */
LOCK_EVENT(pv_wait_node)	/* # of vCPU wait's at non-head queue node */
#endif /* CONFIG_PARAVIRT_SPINLOCKS */

/*
 * Locking events for qspinlock
 *
 * Subtracting lock_use_node[234] from lock_slowpath will give you
 * lock_use_node1.
 */
LOCK_EVENT(lock_pending)	/* # of locking ops via pending code	     */
LOCK_EVENT(lock_slowpath)	/* # of locking ops via MCS lock queue	     */
LOCK_EVENT(lock_use_node2)	/* # of locking ops that use 2nd percpu node */
LOCK_EVENT(lock_use_node3)	/* # of locking ops that use 3rd percpu node */
LOCK_EVENT(lock_use_node4)	/* # of locking ops that use 4th percpu node */
LOCK_EVENT(lock_no_node)	/* # of locking ops w/o using percpu node    */
#endif /* CONFIG_QUEUED_SPINLOCKS */

/*
 * Locking events for Resilient Queued Spin Lock
 */
LOCK_EVENT(rqspinlock_lock_timeout)	/* # of locking ops that timeout	*/

/*
 * Locking events for rwsem
 */
LOCK_EVENT(rwsem_sleep_reader)	/* # of reader sleeps			*/
LOCK_EVENT(rwsem_sleep_writer)	/* # of writer sleeps			*/
LOCK_EVENT(rwsem_wake_reader)	/* # of reader wakeups			*/
LOCK_EVENT(rwsem_wake_writer)	/* # of writer wakeups			*/
LOCK_EVENT(rwsem_opt_lock)	/* # of opt-acquired write locks	*/
LOCK_EVENT(rwsem_opt_fail)	/* # of failed optspins			*/
LOCK_EVENT(rwsem_opt_nospin)	/* # of disabled optspins		*/
LOCK_EVENT(rwsem_rlock)		/* # of read locks acquired		*/
LOCK_EVENT(rwsem_rlock_steal)	/* # of read locks by lock stealing	*/
LOCK_EVENT(rwsem_rlock_fast)	/* # of fast read locks acquired	*/
LOCK_EVENT(rwsem_rlock_fail)	/* # of failed read lock acquisitions	*/
LOCK_EVENT(rwsem_rlock_handoff)	/* # of read lock handoffs		*/
LOCK_EVENT(rwsem_wlock)		/* # of write locks acquired		*/
LOCK_EVENT(rwsem_wlock_fail)	/* # of failed write lock acquisitions	*/
LOCK_EVENT(rwsem_wlock_handoff)	/* # of write lock handoffs		*/

/*
 * Locking events for rtlock_slowlock()
 */
LOCK_EVENT(rtlock_slowlock)	/* # of rtlock_slowlock() calls		*/
LOCK_EVENT(rtlock_slow_acq1)	/* # of locks acquired after wait_lock	*/
LOCK_EVENT(rtlock_slow_acq2)	/* # of locks acquired in for loop	*/
LOCK_EVENT(rtlock_slow_sleep)	/* # of sleeps				*/
LOCK_EVENT(rtlock_slow_wake)	/* # of wakeup's			*/

/*
 * Locking events for rt_mutex_slowlock()
 */
LOCK_EVENT(rtmutex_slowlock)	/* # of rt_mutex_slowlock() calls	*/
LOCK_EVENT(rtmutex_slow_block)	/* # of rt_mutex_slowlock_block() calls	*/
LOCK_EVENT(rtmutex_slow_acq1)	/* # of locks acquired after wait_lock	*/
LOCK_EVENT(rtmutex_slow_acq2)	/* # of locks acquired at the end	*/
LOCK_EVENT(rtmutex_slow_acq3)	/* # of locks acquired in *block()	*/
LOCK_EVENT(rtmutex_slow_sleep)	/* # of sleeps				*/
LOCK_EVENT(rtmutex_slow_wake)	/* # of wakeup's			*/
LOCK_EVENT(rtmutex_deadlock)	/* # of rt_mutex_handle_deadlock()'s	*/

/*
 * Locking events for lockdep
 */
LOCK_EVENT(lockdep_acquire)
LOCK_EVENT(lockdep_lock)
LOCK_EVENT(lockdep_nocheck)
