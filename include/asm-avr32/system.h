/*
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_AVR32_SYSTEM_H
#define __ASM_AVR32_SYSTEM_H

#include <linux/compiler.h>
#include <linux/linkage.h>
#include <linux/types.h>

#include <asm/ptrace.h>
#include <asm/sysreg.h>

#define xchg(ptr,x) \
	((__typeof__(*(ptr)))__xchg((unsigned long)(x),(ptr),sizeof(*(ptr))))

#define nop() asm volatile("nop")

#define mb()			asm volatile("" : : : "memory")
#define rmb()			mb()
#define wmb()			asm volatile("sync 0" : : : "memory")
#define read_barrier_depends()  do { } while(0)
#define set_mb(var, value)      do { var = value; mb(); } while(0)

/*
 * Help PathFinder and other Nexus-compliant debuggers keep track of
 * the current PID by emitting an Ownership Trace Message each time we
 * switch task.
 */
#ifdef CONFIG_OWNERSHIP_TRACE
#include <asm/ocd.h>
#define finish_arch_switch(prev)			\
	do {						\
		ocd_write(PID, prev->pid);		\
		ocd_write(PID, current->pid);		\
	} while(0)
#endif

/*
 * switch_to(prev, next, last) should switch from task `prev' to task
 * `next'. `prev' will never be the same as `next'.
 *
 * We just delegate everything to the __switch_to assembly function,
 * which is implemented in arch/avr32/kernel/switch_to.S
 *
 * mb() tells GCC not to cache `current' across this call.
 */
struct cpu_context;
struct task_struct;
extern struct task_struct *__switch_to(struct task_struct *,
				       struct cpu_context *,
				       struct cpu_context *);
#define switch_to(prev, next, last)					\
	do {								\
		last = __switch_to(prev, &prev->thread.cpu_context + 1,	\
				   &next->thread.cpu_context);		\
	} while (0)

#ifdef CONFIG_SMP
# error "The AVR32 port does not support SMP"
#else
# define smp_mb()		barrier()
# define smp_rmb()		barrier()
# define smp_wmb()		barrier()
# define smp_read_barrier_depends() do { } while(0)
#endif

#include <linux/irqflags.h>

extern void __xchg_called_with_bad_pointer(void);

static inline unsigned long xchg_u32(u32 val, volatile u32 *m)
{
	u32 ret;

	asm volatile("xchg %[ret], %[m], %[val]"
			: [ret] "=&r"(ret), "=m"(*m)
			: "m"(*m), [m] "r"(m), [val] "r"(val)
			: "memory");
	return ret;
}

static inline unsigned long __xchg(unsigned long x,
				       volatile void *ptr,
				       int size)
{
	switch(size) {
	case 4:
		return xchg_u32(x, ptr);
	default:
		__xchg_called_with_bad_pointer();
		return x;
	}
}

static inline unsigned long __cmpxchg_u32(volatile int *m, unsigned long old,
					  unsigned long new)
{
	__u32 ret;

	asm volatile(
		"1:	ssrf	5\n"
		"	ld.w	%[ret], %[m]\n"
		"	cp.w	%[ret], %[old]\n"
		"	brne	2f\n"
		"	stcond	%[m], %[new]\n"
		"	brne	1b\n"
		"2:\n"
		: [ret] "=&r"(ret), [m] "=m"(*m)
		: "m"(m), [old] "ir"(old), [new] "r"(new)
		: "memory", "cc");
	return ret;
}

extern unsigned long __cmpxchg_u64_unsupported_on_32bit_kernels(
        volatile int * m, unsigned long old, unsigned long new);
#define __cmpxchg_u64 __cmpxchg_u64_unsupported_on_32bit_kernels

/* This function doesn't exist, so you'll get a linker error
   if something tries to do an invalid cmpxchg().  */
extern void __cmpxchg_called_with_bad_pointer(void);

#define __HAVE_ARCH_CMPXCHG 1

static inline unsigned long __cmpxchg(volatile void *ptr, unsigned long old,
				      unsigned long new, int size)
{
	switch (size) {
	case 4:
		return __cmpxchg_u32(ptr, old, new);
	case 8:
		return __cmpxchg_u64(ptr, old, new);
	}

	__cmpxchg_called_with_bad_pointer();
	return old;
}

#define cmpxchg(ptr, old, new)					\
	((typeof(*(ptr)))__cmpxchg((ptr), (unsigned long)(old),	\
				   (unsigned long)(new),	\
				   sizeof(*(ptr))))

struct pt_regs;
void NORET_TYPE die(const char *str, struct pt_regs *regs, long err);
void _exception(long signr, struct pt_regs *regs, int code,
		unsigned long addr);

#define arch_align_stack(x)	(x)

#endif /* __ASM_AVR32_SYSTEM_H */
