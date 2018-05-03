/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SECCOMP_H
#define _LINUX_SECCOMP_H

#include <uapi/linux/seccomp.h>

#define SECCOMP_FILTER_FLAG_MASK	(SECCOMP_FILTER_FLAG_TSYNC	| \
					 SECCOMP_FILTER_FLAG_LOG	| \
					 SECCOMP_FILTER_FLAG_SPEC_ALLOW)

#ifdef CONFIG_SECCOMP

#include <linux/thread_info.h>
#include <asm/seccomp.h>

struct seccomp_filter;
/**
 * struct seccomp - the state of a seccomp'ed process
 *
 * @mode:  indicates one of the valid values above for controlled
 *         system calls available to a process.
 * @filter: must always point to a valid seccomp-filter or NULL as it is
 *          accessed without locking during system call entry.
 *
 *          @filter must only be accessed from the context of current as there
 *          is no read locking.
 */
struct seccomp {
	int mode;
	struct seccomp_filter *filter;
};

#ifdef CONFIG_HAVE_ARCH_SECCOMP_FILTER
extern int __secure_computing(const struct seccomp_data *sd);
static inline int secure_computing(const struct seccomp_data *sd)
{
	if (unlikely(test_thread_flag(TIF_SECCOMP)))
		return  __secure_computing(sd);
	return 0;
}
#else
extern void secure_computing_strict(int this_syscall);
#endif

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

#ifdef CONFIG_HAVE_ARCH_SECCOMP_FILTER
static inline int secure_computing(struct seccomp_data *sd) { return 0; }
#else
static inline void secure_computing_strict(int this_syscall) { return; }
#endif

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
	return SECCOMP_MODE_DISABLED;
}
#endif /* CONFIG_SECCOMP */

#ifdef CONFIG_SECCOMP_FILTER
extern void put_seccomp_filter(struct task_struct *tsk);
extern void get_seccomp_filter(struct task_struct *tsk);
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

#if defined(CONFIG_SECCOMP_FILTER) && defined(CONFIG_CHECKPOINT_RESTORE)
extern long seccomp_get_filter(struct task_struct *task,
			       unsigned long filter_off, void __user *data);
extern long seccomp_get_metadata(struct task_struct *task,
				 unsigned long filter_off, void __user *data);
#else
static inline long seccomp_get_filter(struct task_struct *task,
				      unsigned long n, void __user *data)
{
	return -EINVAL;
}
static inline long seccomp_get_metadata(struct task_struct *task,
					unsigned long filter_off,
					void __user *data)
{
	return -EINVAL;
}
#endif /* CONFIG_SECCOMP_FILTER && CONFIG_CHECKPOINT_RESTORE */
#endif /* _LINUX_SECCOMP_H */
