#ifndef __ASM_CRIS_ARCH_SYSTEM_H
#define __ASM_CRIS_ARCH_SYSTEM_H


/* read the CPU version register */

static inline unsigned long rdvr(void) {
	unsigned char vr;
	__asm__ volatile ("move $vr,%0" : "=rm" (vr));
	return vr;
}

#define cris_machine_name "cris"

/* read/write the user-mode stackpointer */

static inline unsigned long rdusp(void) {
	unsigned long usp;
	__asm__ __volatile__("move $usp,%0" : "=rm" (usp));
	return usp;
}

#define wrusp(usp) \
	__asm__ __volatile__("move %0,$usp" : /* no outputs */ : "rm" (usp))

/* read the current stackpointer */

static inline unsigned long rdsp(void) {
	unsigned long sp;
	__asm__ __volatile__("move.d $sp,%0" : "=rm" (sp));
	return sp;
}

static inline unsigned long _get_base(char * addr)
{
  return 0;
}

#define nop() __asm__ __volatile__ ("nop");

#define xchg(ptr,x) ((__typeof__(*(ptr)))__xchg((unsigned long)(x),(ptr),sizeof(*(ptr))))
#define tas(ptr) (xchg((ptr),1))

struct __xchg_dummy { unsigned long a[100]; };
#define __xg(x) ((struct __xchg_dummy *)(x))

/* interrupt control.. */
#define local_save_flags(x)	__asm__ __volatile__ ("move $ccr,%0" : "=rm" (x) : : "memory");
#define local_irq_restore(x) 	__asm__ __volatile__ ("move %0,$ccr" : : "rm" (x) : "memory");
#define local_irq_disable() 	__asm__ __volatile__ ( "di" : : :"memory");
#define local_irq_enable()	__asm__ __volatile__ ( "ei" : : :"memory");

#define irqs_disabled()			\
({					\
	unsigned long flags;		\
	local_save_flags(flags);	\
	!(flags & (1<<5));		\
})

/* For spinlocks etc */
#define local_irq_save(x) __asm__ __volatile__ ("move $ccr,%0\n\tdi" : "=rm" (x) : : "memory");

#endif
