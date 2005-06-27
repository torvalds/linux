#ifndef _LINUX_SECCOMP_H
#define _LINUX_SECCOMP_H

#include <linux/config.h>

#ifdef CONFIG_SECCOMP

#define NR_SECCOMP_MODES 1

#include <linux/thread_info.h>
#include <asm/seccomp.h>

typedef struct { int mode; } seccomp_t;

extern void __secure_computing(int);
static inline void secure_computing(int this_syscall)
{
	if (unlikely(test_thread_flag(TIF_SECCOMP)))
		__secure_computing(this_syscall);
}

static inline int has_secure_computing(struct thread_info *ti)
{
	return unlikely(test_ti_thread_flag(ti, TIF_SECCOMP));
}

#else /* CONFIG_SECCOMP */

#if (__GNUC__ > 2)
  typedef struct { } seccomp_t;
#else
  typedef struct { int gcc_is_buggy; } seccomp_t;
#endif

#define secure_computing(x) do { } while (0)
/* static inline to preserve typechecking */
static inline int has_secure_computing(struct thread_info *ti)
{
	return 0;
}

#endif /* CONFIG_SECCOMP */

#endif /* _LINUX_SECCOMP_H */
