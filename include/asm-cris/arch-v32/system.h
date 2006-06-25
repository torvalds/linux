#ifndef _ASM_CRIS_ARCH_SYSTEM_H
#define _ASM_CRIS_ARCH_SYSTEM_H


/* Read the CPU version register. */
static inline unsigned long rdvr(void)
{
	unsigned char vr;

	__asm__ __volatile__ ("move $vr, %0" : "=rm" (vr));
	return vr;
}

#define cris_machine_name "crisv32"

/* Read the user-mode stack pointer. */
static inline unsigned long rdusp(void)
{
	unsigned long usp;

	__asm__ __volatile__ ("move $usp, %0" : "=rm" (usp));
	return usp;
}

/* Read the current stack pointer. */
static inline unsigned long rdsp(void)
{
	unsigned long sp;

	__asm__ __volatile__ ("move.d $sp, %0" : "=rm" (sp));
	return sp;
}

/* Write the user-mode stack pointer. */
#define wrusp(usp) __asm__ __volatile__ ("move %0, $usp" : : "rm" (usp))

#define nop() __asm__ __volatile__ ("nop");

#define xchg(ptr,x) \
	((__typeof__(*(ptr)))__xchg((unsigned long) (x),(ptr),sizeof(*(ptr))))

#define tas(ptr) (xchg((ptr),1))

struct __xchg_dummy { unsigned long a[100]; };
#define __xg(x) ((struct __xchg_dummy *)(x))

/* Used for interrupt control. */
#define local_save_flags(x) \
	__asm__ __volatile__ ("move $ccs, %0" : "=rm" (x) : : "memory");

#define local_irq_restore(x) \
	__asm__ __volatile__ ("move %0, $ccs" : : "rm" (x) : "memory");

#define local_irq_disable()  __asm__ __volatile__ ("di" : : : "memory");
#define local_irq_enable()   __asm__ __volatile__ ("ei" : : : "memory");

#define irqs_disabled()		\
({				\
	unsigned long flags;	\
				\
	local_save_flags(flags);\
	!(flags & (1 << I_CCS_BITNR));	\
})

/* Used for spinlocks, etc. */
#define local_irq_save(x) \
	__asm__ __volatile__ ("move $ccs, %0\n\tdi" : "=rm" (x) : : "memory");

#ifdef CONFIG_SMP
typedef struct {
	volatile unsigned int lock __attribute__ ((aligned(4)));
#ifdef CONFIG_PREEMPT
	unsigned int break_lock;
#endif
} spinlock_t;
#endif

#endif /* _ASM_CRIS_ARCH_SYSTEM_H */
