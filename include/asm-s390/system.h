/*
 *  include/asm-s390/system.h
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 *
 *  Derived from "include/asm-i386/system.h"
 */

#ifndef __ASM_SYSTEM_H
#define __ASM_SYSTEM_H

#include <linux/kernel.h>
#include <asm/types.h>
#include <asm/ptrace.h>
#include <asm/setup.h>
#include <asm/processor.h>
#include <asm/lowcore.h>

#ifdef __KERNEL__

struct task_struct;

extern struct task_struct *__switch_to(void *, void *);

static inline void save_fp_regs(s390_fp_regs *fpregs)
{
	asm volatile(
		"	std	0,8(%1)\n"
		"	std	2,24(%1)\n"
		"	std	4,40(%1)\n"
		"	std	6,56(%1)"
		: "=m" (*fpregs) : "a" (fpregs), "m" (*fpregs) : "memory");
	if (!MACHINE_HAS_IEEE)
		return;
	asm volatile(
		"	stfpc	0(%1)\n"
		"	std	1,16(%1)\n"
		"	std	3,32(%1)\n"
		"	std	5,48(%1)\n"
		"	std	7,64(%1)\n"
		"	std	8,72(%1)\n"
		"	std	9,80(%1)\n"
		"	std	10,88(%1)\n"
		"	std	11,96(%1)\n"
		"	std	12,104(%1)\n"
		"	std	13,112(%1)\n"
		"	std	14,120(%1)\n"
		"	std	15,128(%1)\n"
		: "=m" (*fpregs) : "a" (fpregs), "m" (*fpregs) : "memory");
}

static inline void restore_fp_regs(s390_fp_regs *fpregs)
{
	asm volatile(
		"	ld	0,8(%0)\n"
		"	ld	2,24(%0)\n"
		"	ld	4,40(%0)\n"
		"	ld	6,56(%0)"
		: : "a" (fpregs), "m" (*fpregs));
	if (!MACHINE_HAS_IEEE)
		return;
	asm volatile(
		"	lfpc	0(%0)\n"
		"	ld	1,16(%0)\n"
		"	ld	3,32(%0)\n"
		"	ld	5,48(%0)\n"
		"	ld	7,64(%0)\n"
		"	ld	8,72(%0)\n"
		"	ld	9,80(%0)\n"
		"	ld	10,88(%0)\n"
		"	ld	11,96(%0)\n"
		"	ld	12,104(%0)\n"
		"	ld	13,112(%0)\n"
		"	ld	14,120(%0)\n"
		"	ld	15,128(%0)\n"
		: : "a" (fpregs), "m" (*fpregs));
}

static inline void save_access_regs(unsigned int *acrs)
{
	asm volatile("stam 0,15,0(%0)" : : "a" (acrs) : "memory");
}

static inline void restore_access_regs(unsigned int *acrs)
{
	asm volatile("lam 0,15,0(%0)" : : "a" (acrs));
}

#define switch_to(prev,next,last) do {					     \
	if (prev == next)						     \
		break;							     \
	save_fp_regs(&prev->thread.fp_regs);				     \
	restore_fp_regs(&next->thread.fp_regs);				     \
	save_access_regs(&prev->thread.acrs[0]);			     \
	restore_access_regs(&next->thread.acrs[0]);			     \
	prev = __switch_to(prev,next);					     \
} while (0)

#ifdef CONFIG_VIRT_CPU_ACCOUNTING
extern void account_vtime(struct task_struct *);
extern void account_tick_vtime(struct task_struct *);
extern void account_system_vtime(struct task_struct *);
#else
#define account_vtime(x) do { /* empty */ } while (0)
#endif

#ifdef CONFIG_PFAULT
extern void pfault_irq_init(void);
extern int pfault_init(void);
extern void pfault_fini(void);
#else /* CONFIG_PFAULT */
#define pfault_irq_init()	do { } while (0)
#define pfault_init()		({-1;})
#define pfault_fini()		do { } while (0)
#endif /* CONFIG_PFAULT */

#ifdef CONFIG_PAGE_STATES
extern void cmma_init(void);
#else
static inline void cmma_init(void) { }
#endif

#define finish_arch_switch(prev) do {					     \
	set_fs(current->thread.mm_segment);				     \
	account_vtime(prev);						     \
} while (0)

#define nop() asm volatile("nop")

#define xchg(ptr,x)							  \
({									  \
	__typeof__(*(ptr)) __ret;					  \
	__ret = (__typeof__(*(ptr)))					  \
		__xchg((unsigned long)(x), (void *)(ptr),sizeof(*(ptr))); \
	__ret;								  \
})

extern void __xchg_called_with_bad_pointer(void);

static inline unsigned long __xchg(unsigned long x, void * ptr, int size)
{
	unsigned long addr, old;
	int shift;

        switch (size) {
	case 1:
		addr = (unsigned long) ptr;
		shift = (3 ^ (addr & 3)) << 3;
		addr ^= addr & 3;
		asm volatile(
			"	l	%0,0(%4)\n"
			"0:	lr	0,%0\n"
			"	nr	0,%3\n"
			"	or	0,%2\n"
			"	cs	%0,0,0(%4)\n"
			"	jl	0b\n"
			: "=&d" (old), "=m" (*(int *) addr)
			: "d" (x << shift), "d" (~(255 << shift)), "a" (addr),
			  "m" (*(int *) addr) : "memory", "cc", "0");
		return old >> shift;
	case 2:
		addr = (unsigned long) ptr;
		shift = (2 ^ (addr & 2)) << 3;
		addr ^= addr & 2;
		asm volatile(
			"	l	%0,0(%4)\n"
			"0:	lr	0,%0\n"
			"	nr	0,%3\n"
			"	or	0,%2\n"
			"	cs	%0,0,0(%4)\n"
			"	jl	0b\n"
			: "=&d" (old), "=m" (*(int *) addr)
			: "d" (x << shift), "d" (~(65535 << shift)), "a" (addr),
			  "m" (*(int *) addr) : "memory", "cc", "0");
		return old >> shift;
	case 4:
		asm volatile(
			"	l	%0,0(%3)\n"
			"0:	cs	%0,%2,0(%3)\n"
			"	jl	0b\n"
			: "=&d" (old), "=m" (*(int *) ptr)
			: "d" (x), "a" (ptr), "m" (*(int *) ptr)
			: "memory", "cc");
		return old;
#ifdef __s390x__
	case 8:
		asm volatile(
			"	lg	%0,0(%3)\n"
			"0:	csg	%0,%2,0(%3)\n"
			"	jl	0b\n"
			: "=&d" (old), "=m" (*(long *) ptr)
			: "d" (x), "a" (ptr), "m" (*(long *) ptr)
			: "memory", "cc");
		return old;
#endif /* __s390x__ */
	}
	__xchg_called_with_bad_pointer();
	return x;
}

/*
 * Atomic compare and exchange.  Compare OLD with MEM, if identical,
 * store NEW in MEM.  Return the initial value in MEM.  Success is
 * indicated by comparing RETURN with OLD.
 */

#define __HAVE_ARCH_CMPXCHG 1

#define cmpxchg(ptr, o, n)						\
	((__typeof__(*(ptr)))__cmpxchg((ptr), (unsigned long)(o),	\
					(unsigned long)(n), sizeof(*(ptr))))

extern void __cmpxchg_called_with_bad_pointer(void);

static inline unsigned long
__cmpxchg(volatile void *ptr, unsigned long old, unsigned long new, int size)
{
	unsigned long addr, prev, tmp;
	int shift;

        switch (size) {
	case 1:
		addr = (unsigned long) ptr;
		shift = (3 ^ (addr & 3)) << 3;
		addr ^= addr & 3;
		asm volatile(
			"	l	%0,0(%4)\n"
			"0:	nr	%0,%5\n"
			"	lr	%1,%0\n"
			"	or	%0,%2\n"
			"	or	%1,%3\n"
			"	cs	%0,%1,0(%4)\n"
			"	jnl	1f\n"
			"	xr	%1,%0\n"
			"	nr	%1,%5\n"
			"	jnz	0b\n"
			"1:"
			: "=&d" (prev), "=&d" (tmp)
			: "d" (old << shift), "d" (new << shift), "a" (ptr),
			  "d" (~(255 << shift))
			: "memory", "cc");
		return prev >> shift;
	case 2:
		addr = (unsigned long) ptr;
		shift = (2 ^ (addr & 2)) << 3;
		addr ^= addr & 2;
		asm volatile(
			"	l	%0,0(%4)\n"
			"0:	nr	%0,%5\n"
			"	lr	%1,%0\n"
			"	or	%0,%2\n"
			"	or	%1,%3\n"
			"	cs	%0,%1,0(%4)\n"
			"	jnl	1f\n"
			"	xr	%1,%0\n"
			"	nr	%1,%5\n"
			"	jnz	0b\n"
			"1:"
			: "=&d" (prev), "=&d" (tmp)
			: "d" (old << shift), "d" (new << shift), "a" (ptr),
			  "d" (~(65535 << shift))
			: "memory", "cc");
		return prev >> shift;
	case 4:
		asm volatile(
			"	cs	%0,%2,0(%3)\n"
			: "=&d" (prev) : "0" (old), "d" (new), "a" (ptr)
			: "memory", "cc");
		return prev;
#ifdef __s390x__
	case 8:
		asm volatile(
			"	csg	%0,%2,0(%3)\n"
			: "=&d" (prev) : "0" (old), "d" (new), "a" (ptr)
			: "memory", "cc");
		return prev;
#endif /* __s390x__ */
        }
	__cmpxchg_called_with_bad_pointer();
	return old;
}

/*
 * Force strict CPU ordering.
 * And yes, this is required on UP too when we're talking
 * to devices.
 *
 * This is very similar to the ppc eieio/sync instruction in that is
 * does a checkpoint syncronisation & makes sure that 
 * all memory ops have completed wrt other CPU's ( see 7-15 POP  DJB ).
 */

#define eieio()	asm volatile("bcr 15,0" : : : "memory")
#define SYNC_OTHER_CORES(x)   eieio()
#define mb()    eieio()
#define rmb()   eieio()
#define wmb()   eieio()
#define read_barrier_depends() do { } while(0)
#define smp_mb()       mb()
#define smp_rmb()      rmb()
#define smp_wmb()      wmb()
#define smp_read_barrier_depends()    read_barrier_depends()
#define smp_mb__before_clear_bit()     smp_mb()
#define smp_mb__after_clear_bit()      smp_mb()


#define set_mb(var, value)      do { var = value; mb(); } while (0)

#ifdef __s390x__

#define __ctl_load(array, low, high) ({				\
	typedef struct { char _[sizeof(array)]; } addrtype;	\
	asm volatile(						\
		"	lctlg	%1,%2,0(%0)\n"			\
		: : "a" (&array), "i" (low), "i" (high),	\
		    "m" (*(addrtype *)(&array)));		\
	})

#define __ctl_store(array, low, high) ({			\
	typedef struct { char _[sizeof(array)]; } addrtype;	\
	asm volatile(						\
		"	stctg	%2,%3,0(%1)\n"			\
		: "=m" (*(addrtype *)(&array))			\
		: "a" (&array), "i" (low), "i" (high));		\
	})

#else /* __s390x__ */

#define __ctl_load(array, low, high) ({				\
	typedef struct { char _[sizeof(array)]; } addrtype;	\
	asm volatile(						\
		"	lctl	%1,%2,0(%0)\n"			\
		: : "a" (&array), "i" (low), "i" (high),	\
		    "m" (*(addrtype *)(&array)));		\
})

#define __ctl_store(array, low, high) ({			\
	typedef struct { char _[sizeof(array)]; } addrtype;	\
	asm volatile(						\
		"	stctl	%2,%3,0(%1)\n"			\
		: "=m" (*(addrtype *)(&array))			\
		: "a" (&array), "i" (low), "i" (high));		\
	})

#endif /* __s390x__ */

#define __ctl_set_bit(cr, bit) ({	\
	unsigned long __dummy;		\
	__ctl_store(__dummy, cr, cr);	\
	__dummy |= 1UL << (bit);	\
	__ctl_load(__dummy, cr, cr);	\
})

#define __ctl_clear_bit(cr, bit) ({	\
	unsigned long __dummy;		\
	__ctl_store(__dummy, cr, cr);	\
	__dummy &= ~(1UL << (bit));	\
	__ctl_load(__dummy, cr, cr);	\
})

#include <linux/irqflags.h>

#include <asm-generic/cmpxchg-local.h>

static inline unsigned long __cmpxchg_local(volatile void *ptr,
				      unsigned long old,
				      unsigned long new, int size)
{
	switch (size) {
	case 1:
	case 2:
	case 4:
#ifdef __s390x__
	case 8:
#endif
		return __cmpxchg(ptr, old, new, size);
	default:
		return __cmpxchg_local_generic(ptr, old, new, size);
	}

	return old;
}

/*
 * cmpxchg_local and cmpxchg64_local are atomic wrt current CPU. Always make
 * them available.
 */
#define cmpxchg_local(ptr, o, n)					\
	((__typeof__(*(ptr)))__cmpxchg_local((ptr), (unsigned long)(o),	\
			(unsigned long)(n), sizeof(*(ptr))))
#ifdef __s390x__
#define cmpxchg64_local(ptr, o, n)					\
  ({									\
	BUILD_BUG_ON(sizeof(*(ptr)) != 8);				\
	cmpxchg_local((ptr), (o), (n));					\
  })
#else
#define cmpxchg64_local(ptr, o, n) __cmpxchg64_local_generic((ptr), (o), (n))
#endif

/*
 * Use to set psw mask except for the first byte which
 * won't be changed by this function.
 */
static inline void
__set_psw_mask(unsigned long mask)
{
	__load_psw_mask(mask | (__raw_local_irq_stosm(0x00) & ~(-1UL >> 8)));
}

#define local_mcck_enable()  __set_psw_mask(psw_kernel_bits)
#define local_mcck_disable() __set_psw_mask(psw_kernel_bits & ~PSW_MASK_MCHECK)

int stfle(unsigned long long *list, int doublewords);

#ifdef CONFIG_SMP

extern void smp_ctl_set_bit(int cr, int bit);
extern void smp_ctl_clear_bit(int cr, int bit);
#define ctl_set_bit(cr, bit) smp_ctl_set_bit(cr, bit)
#define ctl_clear_bit(cr, bit) smp_ctl_clear_bit(cr, bit)

#else

#define ctl_set_bit(cr, bit) __ctl_set_bit(cr, bit)
#define ctl_clear_bit(cr, bit) __ctl_clear_bit(cr, bit)

#endif /* CONFIG_SMP */

static inline unsigned int stfl(void)
{
	asm volatile(
		"	.insn	s,0xb2b10000,0(0)\n" /* stfl */
		"0:\n"
		EX_TABLE(0b,0b));
	return S390_lowcore.stfl_fac_list;
}

static inline unsigned short stap(void)
{
	unsigned short cpu_address;

	asm volatile("stap %0" : "=m" (cpu_address));
	return cpu_address;
}

extern void (*_machine_restart)(char *command);
extern void (*_machine_halt)(void);
extern void (*_machine_power_off)(void);

#define arch_align_stack(x) (x)

#ifdef CONFIG_TRACE_IRQFLAGS
extern psw_t sysc_restore_trace_psw;
extern psw_t io_restore_trace_psw;
#endif

#endif /* __KERNEL__ */

#endif
