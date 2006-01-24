#ifndef __ASM_SH64_SYSTEM_H
#define __ASM_SH64_SYSTEM_H

/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * include/asm-sh64/system.h
 *
 * Copyright (C) 2000, 2001  Paolo Alberelli
 * Copyright (C) 2003  Paul Mundt
 * Copyright (C) 2004  Richard Curnow
 *
 */

#include <linux/config.h>
#include <asm/registers.h>
#include <asm/processor.h>

/*
 *	switch_to() should switch tasks to task nr n, first
 */

typedef struct {
	unsigned long seg;
} mm_segment_t;

extern struct task_struct *sh64_switch_to(struct task_struct *prev,
					  struct thread_struct *prev_thread,
					  struct task_struct *next,
					  struct thread_struct *next_thread);

#define switch_to(prev,next,last) \
	do {\
		if (last_task_used_math != next) {\
			struct pt_regs *regs = next->thread.uregs;\
			if (regs) regs->sr |= SR_FD;\
		}\
		last = sh64_switch_to(prev, &prev->thread, next, &next->thread);\
	} while(0)

#define nop() __asm__ __volatile__ ("nop")

#define xchg(ptr,x) ((__typeof__(*(ptr)))__xchg((unsigned long)(x),(ptr),sizeof(*(ptr))))

#define tas(ptr) (xchg((ptr), 1))

extern void __xchg_called_with_bad_pointer(void);

#define mb()	__asm__ __volatile__ ("synco": : :"memory")
#define rmb()	mb()
#define wmb()	__asm__ __volatile__ ("synco": : :"memory")
#define read_barrier_depends()	do { } while (0)

#ifdef CONFIG_SMP
#define smp_mb()	mb()
#define smp_rmb()	rmb()
#define smp_wmb()	wmb()
#define smp_read_barrier_depends()	read_barrier_depends()
#else
#define smp_mb()	barrier()
#define smp_rmb()	barrier()
#define smp_wmb()	barrier()
#define smp_read_barrier_depends()	do { } while (0)
#endif /* CONFIG_SMP */

#define set_rmb(var, value) do { xchg(&var, value); } while (0)
#define set_mb(var, value) set_rmb(var, value)
#define set_wmb(var, value) do { var = value; wmb(); } while (0)

/* Interrupt Control */
#ifndef HARD_CLI
#define SR_MASK_L 0x000000f0L
#define SR_MASK_LL 0x00000000000000f0LL
#else
#define SR_MASK_L 0x10000000L
#define SR_MASK_LL 0x0000000010000000LL
#endif

static __inline__ void local_irq_enable(void)
{
	/* cli/sti based on SR.BL */
	unsigned long long __dummy0, __dummy1=~SR_MASK_LL;

	__asm__ __volatile__("getcon	" __SR ", %0\n\t"
			     "and	%0, %1, %0\n\t"
			     "putcon	%0, " __SR "\n\t"
			     : "=&r" (__dummy0)
			     : "r" (__dummy1));
}

static __inline__ void local_irq_disable(void)
{
	/* cli/sti based on SR.BL */
	unsigned long long __dummy0, __dummy1=SR_MASK_LL;
	__asm__ __volatile__("getcon	" __SR ", %0\n\t"
			     "or	%0, %1, %0\n\t"
			     "putcon	%0, " __SR "\n\t"
			     : "=&r" (__dummy0)
			     : "r" (__dummy1));
}

#define local_save_flags(x) 						\
(__extension__ ({	unsigned long long __dummy=SR_MASK_LL;		\
	__asm__ __volatile__(						\
		"getcon	" __SR ", %0\n\t"				\
		"and	%0, %1, %0"					\
		: "=&r" (x)						\
		: "r" (__dummy));}))

#define local_irq_save(x)						\
(__extension__ ({	unsigned long long __d2=SR_MASK_LL, __d1;	\
	__asm__ __volatile__(          	         			\
		"getcon	" __SR ", %1\n\t" 				\
		"or	%1, r63, %0\n\t"				\
		"or	%1, %2, %1\n\t"					\
		"putcon	%1, " __SR "\n\t"    				\
		"and	%0, %2, %0"    					\
		: "=&r" (x), "=&r" (__d1)				\
		: "r" (__d2));}));

#define local_irq_restore(x) do { 					\
	if ( ((x) & SR_MASK_L) == 0 )		/* dropping to 0 ? */	\
		local_irq_enable();		/* yes...re-enable */	\
} while (0)

#define irqs_disabled()			\
({					\
	unsigned long flags;		\
	local_save_flags(flags);	\
	(flags != 0);			\
})

static inline unsigned long xchg_u32(volatile int * m, unsigned long val)
{
	unsigned long flags, retval;

	local_irq_save(flags);
	retval = *m;
	*m = val;
	local_irq_restore(flags);
	return retval;
}

static inline unsigned long xchg_u8(volatile unsigned char * m, unsigned long val)
{
	unsigned long flags, retval;

	local_irq_save(flags);
	retval = *m;
	*m = val & 0xff;
	local_irq_restore(flags);
	return retval;
}

static __inline__ unsigned long __xchg(unsigned long x, volatile void * ptr, int size)
{
	switch (size) {
	case 4:
		return xchg_u32(ptr, x);
		break;
	case 1:
		return xchg_u8(ptr, x);
		break;
	}
	__xchg_called_with_bad_pointer();
	return x;
}

/* XXX
 * disable hlt during certain critical i/o operations
 */
#define HAVE_DISABLE_HLT
void disable_hlt(void);
void enable_hlt(void);


#define smp_mb()        barrier()
#define smp_rmb()       barrier()
#define smp_wmb()       barrier()

#ifdef CONFIG_SH_ALPHANUMERIC
/* This is only used for debugging. */
extern void print_seg(char *file,int line);
#define PLS() print_seg(__FILE__,__LINE__)
#else	/* CONFIG_SH_ALPHANUMERIC */
#define PLS()
#endif	/* CONFIG_SH_ALPHANUMERIC */

#define PL() printk("@ <%s,%s:%d>\n",__FILE__,__FUNCTION__,__LINE__)

#define arch_align_stack(x) (x)

#endif /* __ASM_SH64_SYSTEM_H */
