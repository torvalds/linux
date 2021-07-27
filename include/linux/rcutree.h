/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Read-Copy Update mechanism for mutual exclusion (tree-based version)
 *
 * Copyright IBM Corporation, 2008
 *
 * Author: Dipankar Sarma <dipankar@in.ibm.com>
 *	   Paul E. McKenney <paulmck@linux.ibm.com> Hierarchical algorithm
 *
 * Based on the original work by Paul McKenney <paulmck@linux.ibm.com>
 * and inputs from Rusty Russell, Andrea Arcangeli and Andi Kleen.
 *
 * For detailed explanation of Read-Copy Update mechanism see -
 *	Documentation/RCU
 */

#ifndef __LINUX_RCUTREE_H
#define __LINUX_RCUTREE_H

void rcu_softirq_qs(void);
void rcu_note_context_switch(bool preempt);
int rcu_needs_cpu(u64 basem, u64 *nextevt);
void rcu_cpu_stall_reset(void);

/*
 * Note a virtualization-based context switch.  This is simply a
 * wrapper around rcu_note_context_switch(), which allows TINY_RCU
 * to save a few bytes. The caller must have disabled interrupts.
 */
static inline void rcu_virt_note_context_switch(int cpu)
{
	rcu_note_context_switch(false);
}

void synchronize_rcu_expedited(void);
void kvfree_call_rcu(struct rcu_head *head, rcu_callback_t func);

void rcu_barrier(void);
bool rcu_eqs_special_set(int cpu);
void rcu_momentary_dyntick_idle(void);
void kfree_rcu_scheduler_running(void);
bool rcu_gp_might_be_stalled(void);
unsigned long get_state_synchronize_rcu(void);
unsigned long start_poll_synchronize_rcu(void);
bool poll_state_synchronize_rcu(unsigned long oldstate);
void cond_synchronize_rcu(unsigned long oldstate);

void rcu_idle_enter(void);
void rcu_idle_exit(void);
void rcu_irq_enter(void);
void rcu_irq_exit(void);
void rcu_irq_enter_irqson(void);
void rcu_irq_exit_irqson(void);
bool rcu_is_idle_cpu(int cpu);

#ifdef CONFIG_PROVE_RCU
void rcu_irq_exit_check_preempt(void);
#else
static inline void rcu_irq_exit_check_preempt(void) { }
#endif

void exit_rcu(void);

void rcu_scheduler_starting(void);
extern int rcu_scheduler_active __read_mostly;
void rcu_end_inkernel_boot(void);
bool rcu_inkernel_boot_has_ended(void);
bool rcu_is_watching(void);
#ifndef CONFIG_PREEMPTION
void rcu_all_qs(void);
#endif

/* RCUtree hotplug events */
int rcutree_prepare_cpu(unsigned int cpu);
int rcutree_online_cpu(unsigned int cpu);
int rcutree_offline_cpu(unsigned int cpu);
int rcutree_dead_cpu(unsigned int cpu);
int rcutree_dying_cpu(unsigned int cpu);
void rcu_cpu_starting(unsigned int cpu);

#endif /* __LINUX_RCUTREE_H */
