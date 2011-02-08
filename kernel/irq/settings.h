/*
 * Internal header to deal with irq_desc->status which will be renamed
 * to irq_desc->settings.
 */
enum {
	_IRQ_DEFAULT_INIT_FLAGS	= IRQ_DEFAULT_INIT_FLAGS,
	_IRQ_PER_CPU		= IRQ_PER_CPU,
	_IRQ_NO_BALANCING	= IRQ_NO_BALANCING,
	_IRQF_MODIFY_MASK	= IRQF_MODIFY_MASK,
};

#undef IRQ_INPROGRESS
#define IRQ_INPROGRESS		GOT_YOU_MORON
#undef IRQ_REPLAY
#define IRQ_REPLAY		GOT_YOU_MORON
#undef IRQ_WAITING
#define IRQ_WAITING		GOT_YOU_MORON
#undef IRQ_DISABLED
#define IRQ_DISABLED		GOT_YOU_MORON
#undef IRQ_PENDING
#define IRQ_PENDING		GOT_YOU_MORON
#undef IRQ_MASKED
#define IRQ_MASKED		GOT_YOU_MORON
#undef IRQ_WAKEUP
#define IRQ_WAKEUP		GOT_YOU_MORON
#undef IRQ_MOVE_PENDING
#define IRQ_MOVE_PENDING	GOT_YOU_MORON
#undef IRQ_PER_CPU
#define IRQ_PER_CPU		GOT_YOU_MORON
#undef IRQ_NO_BALANCING
#define IRQ_NO_BALANCING	GOT_YOU_MORON
#undef IRQF_MODIFY_MASK
#define IRQF_MODIFY_MASK	GOT_YOU_MORON

static inline void
irq_settings_clr_and_set(struct irq_desc *desc, u32 clr, u32 set)
{
	desc->status &= ~(clr & _IRQF_MODIFY_MASK);
	desc->status |= (set & _IRQF_MODIFY_MASK);
}

static inline bool irq_settings_is_per_cpu(struct irq_desc *desc)
{
	return desc->status & _IRQ_PER_CPU;
}

static inline void irq_settings_set_per_cpu(struct irq_desc *desc)
{
	desc->status |= _IRQ_PER_CPU;
}

static inline void irq_settings_set_no_balancing(struct irq_desc *desc)
{
	desc->status |= _IRQ_NO_BALANCING;
}

static inline bool irq_settings_has_no_balance_set(struct irq_desc *desc)
{
	return desc->status & _IRQ_NO_BALANCING;
}
