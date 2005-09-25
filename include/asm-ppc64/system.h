#ifndef __PPC64_SYSTEM_H
#define __PPC64_SYSTEM_H

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/compiler.h>
#include <asm/page.h>
#include <asm/processor.h>
#include <asm/hw_irq.h>
#include <asm/memory.h>

/*
 * Memory barrier.
 * The sync instruction guarantees that all memory accesses initiated
 * by this processor have been performed (with respect to all other
 * mechanisms that access memory).  The eieio instruction is a barrier
 * providing an ordering (separately) for (a) cacheable stores and (b)
 * loads and stores to non-cacheable memory (e.g. I/O devices).
 *
 * mb() prevents loads and stores being reordered across this point.
 * rmb() prevents loads being reordered across this point.
 * wmb() prevents stores being reordered across this point.
 * read_barrier_depends() prevents data-dependent loads being reordered
 *	across this point (nop on PPC).
 *
 * We have to use the sync instructions for mb(), since lwsync doesn't
 * order loads with respect to previous stores.  Lwsync is fine for
 * rmb(), though.
 * For wmb(), we use sync since wmb is used in drivers to order
 * stores to system memory with respect to writes to the device.
 * However, smp_wmb() can be a lighter-weight eieio barrier on
 * SMP since it is only used to order updates to system memory.
 */
#define mb()   __asm__ __volatile__ ("sync" : : : "memory")
#define rmb()  __asm__ __volatile__ ("lwsync" : : : "memory")
#define wmb()  __asm__ __volatile__ ("sync" : : : "memory")
#define read_barrier_depends()  do { } while(0)

#define set_mb(var, value)	do { var = value; smp_mb(); } while (0)
#define set_wmb(var, value)	do { var = value; smp_wmb(); } while (0)

#ifdef CONFIG_SMP
#define smp_mb()	mb()
#define smp_rmb()	rmb()
#define smp_wmb()	__asm__ __volatile__ ("eieio" : : : "memory")
#define smp_read_barrier_depends()  read_barrier_depends()
#else
#define smp_mb()	__asm__ __volatile__("": : :"memory")
#define smp_rmb()	__asm__ __volatile__("": : :"memory")
#define smp_wmb()	__asm__ __volatile__("": : :"memory")
#define smp_read_barrier_depends()  do { } while(0)
#endif /* CONFIG_SMP */

#ifdef __KERNEL__
struct task_struct;
struct pt_regs;

#ifdef CONFIG_DEBUGGER

extern int (*__debugger)(struct pt_regs *regs);
extern int (*__debugger_ipi)(struct pt_regs *regs);
extern int (*__debugger_bpt)(struct pt_regs *regs);
extern int (*__debugger_sstep)(struct pt_regs *regs);
extern int (*__debugger_iabr_match)(struct pt_regs *regs);
extern int (*__debugger_dabr_match)(struct pt_regs *regs);
extern int (*__debugger_fault_handler)(struct pt_regs *regs);

#define DEBUGGER_BOILERPLATE(__NAME) \
static inline int __NAME(struct pt_regs *regs) \
{ \
	if (unlikely(__ ## __NAME)) \
		return __ ## __NAME(regs); \
	return 0; \
}

DEBUGGER_BOILERPLATE(debugger)
DEBUGGER_BOILERPLATE(debugger_ipi)
DEBUGGER_BOILERPLATE(debugger_bpt)
DEBUGGER_BOILERPLATE(debugger_sstep)
DEBUGGER_BOILERPLATE(debugger_iabr_match)
DEBUGGER_BOILERPLATE(debugger_dabr_match)
DEBUGGER_BOILERPLATE(debugger_fault_handler)

#ifdef CONFIG_XMON
extern void xmon_init(int enable);
#endif

#else
static inline int debugger(struct pt_regs *regs) { return 0; }
static inline int debugger_ipi(struct pt_regs *regs) { return 0; }
static inline int debugger_bpt(struct pt_regs *regs) { return 0; }
static inline int debugger_sstep(struct pt_regs *regs) { return 0; }
static inline int debugger_iabr_match(struct pt_regs *regs) { return 0; }
static inline int debugger_dabr_match(struct pt_regs *regs) { return 0; }
static inline int debugger_fault_handler(struct pt_regs *regs) { return 0; }
#endif

extern int set_dabr(unsigned long dabr);
extern void _exception(int signr, struct pt_regs *regs, int code,
		       unsigned long addr);
extern int fix_alignment(struct pt_regs *regs);
extern void bad_page_fault(struct pt_regs *regs, unsigned long address,
			   int sig);
extern void show_regs(struct pt_regs * regs);
extern void low_hash_fault(struct pt_regs *regs, unsigned long address);
extern int die(const char *str, struct pt_regs *regs, long err);

extern int _get_PVR(void);
extern void giveup_fpu(struct task_struct *);
extern void disable_kernel_fp(void);
extern void flush_fp_to_thread(struct task_struct *);
extern void enable_kernel_fp(void);
extern void giveup_altivec(struct task_struct *);
extern void disable_kernel_altivec(void);
extern void enable_kernel_altivec(void);
extern int emulate_altivec(struct pt_regs *);
extern void cvt_fd(float *from, double *to, unsigned long *fpscr);
extern void cvt_df(double *from, float *to, unsigned long *fpscr);

#ifdef CONFIG_ALTIVEC
extern void flush_altivec_to_thread(struct task_struct *);
#else
static inline void flush_altivec_to_thread(struct task_struct *t)
{
}
#endif

extern int mem_init_done;	/* set on boot once kmalloc can be called */

/* EBCDIC -> ASCII conversion for [0-9A-Z] on iSeries */
extern unsigned char e2a(unsigned char);

extern struct task_struct *__switch_to(struct task_struct *,
				       struct task_struct *);
#define switch_to(prev, next, last)	((last) = __switch_to((prev), (next)))

struct thread_struct;
extern struct task_struct * _switch(struct thread_struct *prev,
				    struct thread_struct *next);

static inline int __is_processor(unsigned long pv)
{
	unsigned long pvr;
	asm("mfspr %0, 0x11F" : "=r" (pvr)); 
	return(PVR_VER(pvr) == pv);
}

/*
 * Atomic exchange
 *
 * Changes the memory location '*ptr' to be val and returns
 * the previous value stored there.
 *
 * Inline asm pulled from arch/ppc/kernel/misc.S so ppc64
 * is more like most of the other architectures.
 */
static __inline__ unsigned long
__xchg_u32(volatile unsigned int *m, unsigned long val)
{
	unsigned long dummy;

	__asm__ __volatile__(
	EIEIO_ON_SMP
"1:	lwarx %0,0,%3		# __xchg_u32\n\
	stwcx. %2,0,%3\n\
2:	bne- 1b"
	ISYNC_ON_SMP
 	: "=&r" (dummy), "=m" (*m)
	: "r" (val), "r" (m)
	: "cc", "memory");

	return (dummy);
}

static __inline__ unsigned long
__xchg_u64(volatile long *m, unsigned long val)
{
	unsigned long dummy;

	__asm__ __volatile__(
	EIEIO_ON_SMP
"1:	ldarx %0,0,%3		# __xchg_u64\n\
	stdcx. %2,0,%3\n\
2:	bne- 1b"
	ISYNC_ON_SMP
	: "=&r" (dummy), "=m" (*m)
	: "r" (val), "r" (m)
	: "cc", "memory");

	return (dummy);
}

/*
 * This function doesn't exist, so you'll get a linker error
 * if something tries to do an invalid xchg().
 */
extern void __xchg_called_with_bad_pointer(void);

static __inline__ unsigned long
__xchg(volatile void *ptr, unsigned long x, unsigned int size)
{
	switch (size) {
	case 4:
		return __xchg_u32(ptr, x);
	case 8:
		return __xchg_u64(ptr, x);
	}
	__xchg_called_with_bad_pointer();
	return x;
}

#define xchg(ptr,x)							     \
  ({									     \
     __typeof__(*(ptr)) _x_ = (x);					     \
     (__typeof__(*(ptr))) __xchg((ptr), (unsigned long)_x_, sizeof(*(ptr))); \
  })

#define tas(ptr) (xchg((ptr),1))

#define __HAVE_ARCH_CMPXCHG	1

static __inline__ unsigned long
__cmpxchg_u32(volatile unsigned int *p, unsigned long old, unsigned long new)
{
	unsigned int prev;

	__asm__ __volatile__ (
	EIEIO_ON_SMP
"1:	lwarx	%0,0,%2		# __cmpxchg_u32\n\
	cmpw	0,%0,%3\n\
	bne-	2f\n\
	stwcx.	%4,0,%2\n\
	bne-	1b"
	ISYNC_ON_SMP
	"\n\
2:"
	: "=&r" (prev), "=m" (*p)
	: "r" (p), "r" (old), "r" (new), "m" (*p)
	: "cc", "memory");

	return prev;
}

static __inline__ unsigned long
__cmpxchg_u64(volatile long *p, unsigned long old, unsigned long new)
{
	unsigned long prev;

	__asm__ __volatile__ (
	EIEIO_ON_SMP
"1:	ldarx	%0,0,%2		# __cmpxchg_u64\n\
	cmpd	0,%0,%3\n\
	bne-	2f\n\
	stdcx.	%4,0,%2\n\
	bne-	1b"
	ISYNC_ON_SMP
	"\n\
2:"
	: "=&r" (prev), "=m" (*p)
	: "r" (p), "r" (old), "r" (new), "m" (*p)
	: "cc", "memory");

	return prev;
}

/* This function doesn't exist, so you'll get a linker error
   if something tries to do an invalid cmpxchg().  */
extern void __cmpxchg_called_with_bad_pointer(void);

static __inline__ unsigned long
__cmpxchg(volatile void *ptr, unsigned long old, unsigned long new,
	  unsigned int size)
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

#define cmpxchg(ptr,o,n)\
	((__typeof__(*(ptr)))__cmpxchg((ptr),(unsigned long)(o),\
	(unsigned long)(n),sizeof(*(ptr))))

/*
 * We handle most unaligned accesses in hardware. On the other hand 
 * unaligned DMA can be very expensive on some ppc64 IO chips (it does
 * powers of 2 writes until it reaches sufficient alignment).
 *
 * Based on this we disable the IP header alignment in network drivers.
 */
#define NET_IP_ALIGN   0

#define arch_align_stack(x) (x)

extern unsigned long reloc_offset(void);

#endif /* __KERNEL__ */
#endif
