/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SCHED_TASK_H
#define _LINUX_SCHED_TASK_H

/*
 * Interface between the scheduler and various task lifetime (fork()/exit())
 * functionality:
 */

#include <linux/sched.h>

struct task_struct;
struct rusage;
union thread_union;

/*
 * This serializes "schedule()" and also protects
 * the run-queue from deletions/modifications (but
 * _adding_ to the beginning of the run-queue has
 * a separate lock).
 */
extern rwlock_t tasklist_lock;
extern spinlock_t mmlist_lock;

extern union thread_union init_thread_union;
extern struct task_struct init_task;

#ifdef CONFIG_PROVE_RCU
extern int lockdep_tasklist_lock_is_held(void);
#endif /* #ifdef CONFIG_PROVE_RCU */

extern asmlinkage void schedule_tail(struct task_struct *prev);
extern void init_idle(struct task_struct *idle, int cpu);

extern int sched_fork(unsigned long clone_flags, struct task_struct *p);
extern void sched_dead(struct task_struct *p);

void __noreturn do_task_dead(void);

extern void proc_caches_init(void);

extern void release_task(struct task_struct * p);

#ifdef CONFIG_HAVE_COPY_THREAD_TLS
extern int copy_thread_tls(unsigned long, unsigned long, unsigned long,
			struct task_struct *, unsigned long);
#else
extern int copy_thread(unsigned long, unsigned long, unsigned long,
			struct task_struct *);

/* Architectures that haven't opted into copy_thread_tls get the tls argument
 * via pt_regs, so ignore the tls argument passed via C. */
static inline int copy_thread_tls(
		unsigned long clone_flags, unsigned long sp, unsigned long arg,
		struct task_struct *p, unsigned long tls)
{
	return copy_thread(clone_flags, sp, arg, p);
}
#endif
extern void flush_thread(void);

#ifdef CONFIG_HAVE_EXIT_THREAD
extern void exit_thread(struct task_struct *tsk);
#else
static inline void exit_thread(struct task_struct *tsk)
{
}
#endif
extern void do_group_exit(int);

extern void exit_files(struct task_struct *);
extern void exit_itimers(struct signal_struct *);

extern long _do_fork(unsigned long, unsigned long, unsigned long, int __user *, int __user *, unsigned long);
extern long do_fork(unsigned long, unsigned long, unsigned long, int __user *, int __user *);
struct task_struct *fork_idle(int);
extern pid_t kernel_thread(int (*fn)(void *), void *arg, unsigned long flags);
extern long kernel_wait4(pid_t, int *, int, struct rusage *);

extern void free_task(struct task_struct *tsk);

/* sched_exec is called by processes performing an exec */
#ifdef CONFIG_SMP
extern void sched_exec(void);
#else
#define sched_exec()   {}
#endif

#define get_task_struct(tsk) do { atomic_inc(&(tsk)->usage); } while(0)

extern void __put_task_struct(struct task_struct *t);

static inline void put_task_struct(struct task_struct *t)
{
	if (atomic_dec_and_test(&t->usage))
		__put_task_struct(t);
}

struct task_struct *task_rcu_dereference(struct task_struct **ptask);

#ifdef CONFIG_ARCH_WANTS_DYNAMIC_TASK_STRUCT
extern int arch_task_struct_size __read_mostly;
#else
# define arch_task_struct_size (sizeof(struct task_struct))
#endif

#ifndef CONFIG_HAVE_ARCH_THREAD_STRUCT_WHITELIST
/*
 * If an architecture has not declared a thread_struct whitelist we
 * must assume something there may need to be copied to userspace.
 */
static inline void arch_thread_struct_whitelist(unsigned long *offset,
						unsigned long *size)
{
	*offset = 0;
	/* Handle dynamically sized thread_struct. */
	*size = arch_task_struct_size - offsetof(struct task_struct, thread);
}
#endif

#ifdef CONFIG_VMAP_STACK
static inline struct vm_struct *task_stack_vm_area(const struct task_struct *t)
{
	return t->stack_vm_area;
}
#else
static inline struct vm_struct *task_stack_vm_area(const struct task_struct *t)
{
	return NULL;
}
#endif

/*
 * Protects ->fs, ->files, ->mm, ->group_info, ->comm, keyring
 * subscriptions and synchronises with wait4().  Also used in procfs.  Also
 * pins the final release of task.io_context.  Also protects ->cpuset and
 * ->cgroup.subsys[]. And ->vfork_done.
 *
 * Nests both inside and outside of read_lock(&tasklist_lock).
 * It must not be nested with write_lock_irq(&tasklist_lock),
 * neither inside nor outside.
 */
static inline void task_lock(struct task_struct *p)
{
	spin_lock(&p->alloc_lock);
}

static inline void task_unlock(struct task_struct *p)
{
	spin_unlock(&p->alloc_lock);
}

#endif /* _LINUX_SCHED_TASK_H */
