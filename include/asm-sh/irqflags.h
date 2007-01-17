#ifndef __ASM_SH_IRQFLAGS_H
#define __ASM_SH_IRQFLAGS_H

static inline void raw_local_irq_enable(void)
{
	unsigned long __dummy0, __dummy1;

	__asm__ __volatile__ (
		"stc	sr, %0\n\t"
		"and	%1, %0\n\t"
#ifdef CONFIG_CPU_HAS_SR_RB
		"stc	r6_bank, %1\n\t"
		"or	%1, %0\n\t"
#endif
		"ldc	%0, sr\n\t"
		: "=&r" (__dummy0), "=r" (__dummy1)
		: "1" (~0x000000f0)
		: "memory"
	);
}

static inline void raw_local_irq_disable(void)
{
	unsigned long flags;

	__asm__ __volatile__ (
		"stc	sr, %0\n\t"
		"or	#0xf0, %0\n\t"
		"ldc	%0, sr\n\t"
		: "=&z" (flags)
		: /* no inputs */
		: "memory"
	);
}

static inline void set_bl_bit(void)
{
	unsigned long __dummy0, __dummy1;

	__asm__ __volatile__ (
		"stc	sr, %0\n\t"
		"or	%2, %0\n\t"
		"and	%3, %0\n\t"
		"ldc	%0, sr\n\t"
		: "=&r" (__dummy0), "=r" (__dummy1)
		: "r" (0x10000000), "r" (0xffffff0f)
		: "memory"
	);
}

static inline void clear_bl_bit(void)
{
	unsigned long __dummy0, __dummy1;

	__asm__ __volatile__ (
		"stc	sr, %0\n\t"
		"and	%2, %0\n\t"
		"ldc	%0, sr\n\t"
		: "=&r" (__dummy0), "=r" (__dummy1)
		: "1" (~0x10000000)
		: "memory"
	);
}

static inline unsigned long __raw_local_save_flags(void)
{
	unsigned long flags;

	__asm__ __volatile__ (
		"stc	sr, %0\n\t"
		"and	#0xf0, %0\n\t"
		: "=&z" (flags)
		: /* no inputs */
		: "memory"
	);

	return flags;
}

#define raw_local_save_flags(flags) \
		do { (flags) = __raw_local_save_flags(); } while (0)

static inline int raw_irqs_disabled_flags(unsigned long flags)
{
	return (flags != 0);
}

static inline int raw_irqs_disabled(void)
{
	unsigned long flags = __raw_local_save_flags();

	return raw_irqs_disabled_flags(flags);
}

static inline unsigned long __raw_local_irq_save(void)
{
	unsigned long flags, __dummy;

	__asm__ __volatile__ (
		"stc	sr, %1\n\t"
		"mov	%1, %0\n\t"
		"or	#0xf0, %0\n\t"
		"ldc	%0, sr\n\t"
		"mov	%1, %0\n\t"
		"and	#0xf0, %0\n\t"
		: "=&z" (flags), "=&r" (__dummy)
		: /* no inputs */
		: "memory"
	);

	return flags;
}

#define raw_local_irq_save(flags) \
		do { (flags) = __raw_local_irq_save(); } while (0)

static inline void raw_local_irq_restore(unsigned long flags)
{
	if ((flags & 0xf0) != 0xf0)
		raw_local_irq_enable();
}

#endif /* __ASM_SH_IRQFLAGS_H */
