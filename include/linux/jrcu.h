/*
 * JRCU - An RCU suitable for small SMP systems.
 *
 * Author: Joe Korty <joe.korty@ccur.com>
 * Copyright Concurrent Computer Corporation, 2011
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#ifndef __LINUX_JRCU_H
#define __LINUX_JRCU_H

#define __rcu_read_lock()                      preempt_disable()
#define __rcu_read_unlock()                    jrcu_read_unlock()
extern void jrcu_read_unlock(void);

#define __rcu_read_lock_bh()                   __rcu_read_lock()
#define __rcu_read_unlock_bh()                 __rcu_read_unlock()

extern void call_rcu_sched(struct rcu_head *head, void (*func)(struct rcu_head *rcu));

#define call_rcu_bh                            call_rcu_sched
#define call_rcu                               call_rcu_sched

extern void rcu_barrier(void);

#define rcu_barrier_sched                      rcu_barrier
#define rcu_barrier_bh                         rcu_barrier

extern void synchronize_sched(void);

#define synchronize_rcu                                synchronize_sched
#define synchronize_rcu_bh                     synchronize_sched
#define synchronize_rcu_expedited              synchronize_sched
#define synchronize_rcu_bh_expedited           synchronize_sched
#define synchronize_sched_expedited            synchronize_sched

#define rcu_init(cpu)                          do { } while (0)
#define rcu_init_sched()                       do { } while (0)
#define exit_rcu()                             do { } while (0)

static inline void __rcu_check_callbacks(int cpu, int user) { }
#define rcu_check_callbacks                    __rcu_check_callbacks

#define rcu_needs_cpu(cpu)                     (0)
#define rcu_batches_completed()                        (0)
#define rcu_batches_completed_bh()             (0)
#define rcu_preempt_depth()                    (0)

extern void rcu_force_quiescent_state(void);

#define rcu_sched_force_quiescent_state                rcu_force_quiescent_state
#define rcu_bh_force_quiescent_state           rcu_force_quiescent_state

#define rcu_enter_nohz()                       do { } while (0)
#define rcu_exit_nohz()                                do { } while (0)

extern void rcu_note_context_switch(int cpu);

#define rcu_sched_qs                           rcu_note_context_switch
#define rcu_bh_qs                              rcu_note_context_switch
#define rcu_virt_note_context_switch           rcu_note_context_switch

extern void rcu_note_might_resched(void);

extern void rcu_scheduler_starting(void);
extern int rcu_scheduler_active __read_mostly;

#endif /* __LINUX_JRCU_H */
