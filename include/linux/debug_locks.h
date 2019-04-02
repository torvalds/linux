/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_DE_LOCKING_H
#define __LINUX_DE_LOCKING_H

#include <linux/kernel.h>
#include <linux/atomic.h>
#include <linux/.h>

struct task_struct;

extern int de_locks __read_mostly;
extern int de_locks_silent __read_mostly;


static inline int __de_locks_off(void)
{
	return xchg(&de_locks, 0);
}

/*
 * Generic 'turn off all lock deging' function:
 */
extern int de_locks_off(void);

#define DE_LOCKS_WARN_ON(c)						\
({									\
	int __ret = 0;							\
									\
	if (!oops_in_progress && unlikely(c)) {				\
		if (de_locks_off() && !de_locks_silent)		\
			WARN(1, "DE_LOCKS_WARN_ON(%s)", #c);		\
		__ret = 1;						\
	}								\
	__ret;								\
})

#ifdef CONFIG_SMP
# define SMP_DE_LOCKS_WARN_ON(c)			DE_LOCKS_WARN_ON(c)
#else
# define SMP_DE_LOCKS_WARN_ON(c)			do { } while (0)
#endif

#ifdef CONFIG_DE_LOCKING_API_SELFTESTS
  extern void locking_selftest(void);
#else
# define locking_selftest()	do { } while (0)
#endif

struct task_struct;

#ifdef CONFIG_LOCKDEP
extern void de_show_all_locks(void);
extern void de_show_held_locks(struct task_struct *task);
extern void de_check_no_locks_freed(const void *from, unsigned long len);
extern void de_check_no_locks_held(void);
#else
static inline void de_show_all_locks(void)
{
}

static inline void de_show_held_locks(struct task_struct *task)
{
}

static inline void
de_check_no_locks_freed(const void *from, unsigned long len)
{
}

static inline void
de_check_no_locks_held(void)
{
}
#endif

#endif
