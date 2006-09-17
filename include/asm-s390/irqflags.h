/*
 *  include/asm-s390/irqflags.h
 *
 *    Copyright (C) IBM Corp. 2006
 *    Author(s): Heiko Carstens <heiko.carstens@de.ibm.com>
 */

#ifndef __ASM_IRQFLAGS_H
#define __ASM_IRQFLAGS_H

#ifdef __KERNEL__

/* interrupt control.. */
#define raw_local_irq_enable() ({ \
	unsigned long  __dummy; \
	__asm__ __volatile__ ( \
		"stosm 0(%1),0x03" \
		: "=m" (__dummy) : "a" (&__dummy) : "memory" ); \
	})

#define raw_local_irq_disable() ({ \
	unsigned long __flags; \
	__asm__ __volatile__ ( \
		"stnsm 0(%1),0xfc" : "=m" (__flags) : "a" (&__flags) ); \
	__flags; \
	})

#define raw_local_save_flags(x)							\
do {										\
	typecheck(unsigned long, x);						\
	__asm__ __volatile__("stosm 0(%1),0" : "=m" (x) : "a" (&x), "m" (x) );	\
} while (0)

#define raw_local_irq_restore(x)						\
do {										\
	typecheck(unsigned long, x);						\
	__asm__ __volatile__("ssm   0(%0)" : : "a" (&x), "m" (x) : "memory");	\
} while (0)

#define raw_irqs_disabled()		\
({					\
	unsigned long flags;		\
	raw_local_save_flags(flags);	\
	!((flags >> __FLAG_SHIFT) & 3);	\
})

static inline int raw_irqs_disabled_flags(unsigned long flags)
{
	return !((flags >> __FLAG_SHIFT) & 3);
}

/* For spinlocks etc */
#define raw_local_irq_save(x)	((x) = raw_local_irq_disable())

#endif /* __KERNEL__ */
#endif /* __ASM_IRQFLAGS_H */
