#ifndef __ASM_SH_ATOMIC_IRQ_H
#define __ASM_SH_ATOMIC_IRQ_H

/*
 * To get proper branch prediction for the main line, we must branch
 * forward to code at the end of this object's .text section, then
 * branch back to restart the operation.
 */
static inline void atomic_add(int i, atomic_t *v)
{
	unsigned long flags;

	local_irq_save(flags);
	*(long *)v += i;
	local_irq_restore(flags);
}

static inline void atomic_sub(int i, atomic_t *v)
{
	unsigned long flags;

	local_irq_save(flags);
	*(long *)v -= i;
	local_irq_restore(flags);
}

static inline int atomic_add_return(int i, atomic_t *v)
{
	unsigned long temp, flags;

	local_irq_save(flags);
	temp = *(long *)v;
	temp += i;
	*(long *)v = temp;
	local_irq_restore(flags);

	return temp;
}

static inline int atomic_sub_return(int i, atomic_t *v)
{
	unsigned long temp, flags;

	local_irq_save(flags);
	temp = *(long *)v;
	temp -= i;
	*(long *)v = temp;
	local_irq_restore(flags);

	return temp;
}

static inline void atomic_clear_mask(unsigned int mask, atomic_t *v)
{
	unsigned long flags;

	local_irq_save(flags);
	*(long *)v &= ~mask;
	local_irq_restore(flags);
}

static inline void atomic_set_mask(unsigned int mask, atomic_t *v)
{
	unsigned long flags;

	local_irq_save(flags);
	*(long *)v |= mask;
	local_irq_restore(flags);
}

#endif /* __ASM_SH_ATOMIC_IRQ_H */
