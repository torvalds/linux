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
int rcu_needs_cpu(void);
void rcu_cpu_stall_reset(void);
void rcu_request_urgent_qs_task(struct task_struct *t);

/*
 * Note a virtualization-based context switch.  This is simply a
 * wrapper around rcu_note_context_switch(), which allows TINY_RCU
 * to save a few bytes. The caller must have disabled interrupts.
 */
static inline void rcu_virt_note_context_switch(void)
{
	rcu_note_context_switch(false);
}

void synchronize_rcu_expedited(void);
void kvfree_call_rcu(struct rcu_head *head, void *ptr);

void rcu_barrier(void);
void rcu_momentary_dyntick_idle(void);
void kfree_rcu_scheduler_running(void);
bool rcu_gp_might_be_stalled(void);

struct rcu_gp_oldstate {
	unsigned long rgos_norm;
	unsigned long rgos_exp;
};

// Maximum number of rcu_gp_oldstate values corresponding to
// not-yet-completed RCU grace periods.
#define NUM_ACTIVE_RCU_POLL_FULL_OLDSTATE 4

/**
 * same_state_synchronize_rcu_full - Are two old-state values identical?
 * @rgosp1: First old-state value.
 * @rgosp2: Second old-state value.
 *
 * The two old-state values must have been obtained from either
 * get_state_synchronize_rcu_full(), start_poll_synchronize_rcu_full(),
 * or get_completed_synchronize_rcu_full().  Returns @true if the two
 * values are identical and @false otherwise.  This allows structures
 * whose lifetimes are tracked by old-state values to push these values
 * to a list header, allowing those structures to be slightly smaller.
 *
 * Note that equality is judged on a bitwise basis, so that an
 * @rcu_gp_oldstate structure with an already-completed state in one field
 * will compare not-equal to a structure with an already-completed state
 * in the other field.  After all, the @rcu_gp_oldstate structure is opaque
 * so how did such a situation come to pass in the first place?
 */
static inline bool same_state_synchronize_rcu_full(struct rcu_gp_oldstate *rgosp1,
						   struct rcu_gp_oldstate *rgosp2)
{
	return rgosp1->rgos_norm == rgosp2->rgos_norm && rgosp1->rgos_exp == rgosp2->rgos_exp;
}

unsigned long start_poll_synchronize_rcu_expedited(void);
void start_poll_synchronize_rcu_expedited_full(struct rcu_gp_oldstate *rgosp);
void cond_synchronize_rcu_expedited(unsigned long oldstate);
void cond_synchronize_rcu_expedited_full(struct rcu_gp_oldstate *rgosp);
unsigned long get_state_synchronize_rcu(void);
void get_state_synchronize_rcu_full(struct rcu_gp_oldstate *rgosp);
unsigned long start_poll_synchronize_rcu(void);
void start_poll_synchronize_rcu_full(struct rcu_gp_oldstate *rgosp);
bool poll_state_synchronize_rcu(unsigned long oldstate);
bool poll_state_synchronize_rcu_full(struct rcu_gp_oldstate *rgosp);
void cond_synchronize_rcu(unsigned long oldstate);
void cond_synchronize_rcu_full(struct rcu_gp_oldstate *rgosp);

#ifdef CONFIG_PROVE_RCU
void rcu_irq_exit_check_preempt(void);
#else
static inline void rcu_irq_exit_check_preempt(void) { }
#endif

struct task_struct;
void rcu_preempt_deferred_qs(struct task_struct *t);

void exit_rcu(void);

void rcu_scheduler_starting(void);
extern int rcu_scheduler_active;
void rcu_end_inkernel_boot(void);
bool rcu_inkernel_boot_has_ended(void);
bool rcu_is_watching(void);
#ifndef CONFIG_PREEMPTION
void rcu_all_qs(void);
#endif

/* RCUtree hotplug events */
int rcutree_prepare_cpu(unsigned int cpu);
int rcutree_online_cpu(unsigned int cpu);
void rcutree_report_cpu_starting(unsigned int cpu);

#ifdef CONFIG_HOTPLUG_CPU
int rcutree_dead_cpu(unsigned int cpu);
int rcutree_dying_cpu(unsigned int cpu);
int rcutree_offline_cpu(unsigned int cpu);
#else
#define rcutree_dead_cpu NULL
#define rcutree_dying_cpu NULL
#define rcutree_offline_cpu NULL
#endif

void rcutree_migrate_callbacks(int cpu);

/* Called from hotplug and also arm64 early secondary boot failure */
void rcutree_report_cpu_dead(void);

#endif /* __LINUX_RCUTREE_H */
