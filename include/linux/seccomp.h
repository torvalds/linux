#ifndef _LINUX_SECCOMP_H
#define _LINUX_SECCOMP_H

#include <uapi/linux/seccomp.h>

#ifdef CONFIG_SECCOMP

#include <linux/thread_info.h>
#include <asm/seccomp.h>

struct seccomp_filter;
/**
 * struct seccomp - the state of a seccomp'ed process
 *
 * @mode:  indicates one of the valid values above for controlled
 *         system calls available to a process.
 * @filter: The metadata and ruleset for determining what system calls
 *          are allowed for a task.
 *
 *          @filter must only be accessed from the context of current as there
 *          is no locking.
 */
struct seccomp {
	int mode;
	struct seccomp_filter *filter;
};

extern int __secure_computing(int);
static inline int secure_computing(int this_syscall)
{
	if (unlikely(test_thread_flag(TIF_SECCOMP)))
		return  __secure_computing(this_syscall);
	return 0;
}

/* A wrapper for architectures supporting only SECCOMP_MODE_STRICT. */
static inline void secure_computing_strict(int this_syscall)
{
	BUG_ON(secure_computing(this_syscall) != 0);
}

extern long prctl_get_seccomp(void);
extern long prctl_set_seccomp(unsigned long, char __user *);

static inline int seccomp_mode(struct seccomp *s)
{
	return s->mode;
}

#else /* CONFIG_SECCOMP */

#include <linux/errno.h>

struct seccomp { };
struct seccomp_filter { };

static inline int secure_computing(int this_syscall) { return 0; }
static inline void secure_computing_strict(int this_syscall) { return; }

static inline long prctl_get_seccomp(void)
{
	return -EINVAL;
}

static inline long prctl_set_seccomp(unsigned long arg2, char __user *arg3)
{
	return -EINVAL;
}

static inline int seccomp_mode(struct seccomp *s)
{
	return 0;
}
#endif /* CONFIG_SECCOMP */

#ifdef CONFIG_SECCOMP_FILTER
extern void put_seccomp_filter(struct task_struct *tsk);
extern void get_seccomp_filter(struct task_struct *tsk);
extern u32 seccomp_bpf_load(int off);
#else  /* CONFIG_SECCOMP_FILTER */
static inline void put_seccomp_filter(struct task_struct *tsk)
{
	return;
}
static inline void get_seccomp_filter(struct task_struct *tsk)
{
	return;
}
#endif /* CONFIG_SECCOMP_FILTER */
#endif /* _LINUX_SECCOMP_H */
