/*
 * Internal header to deal with irq_desc->status which will be renamed
 * to irq_desc->settings.
 */
enum {
	_IRQ_DEFAULT_INIT_FLAGS	= IRQ_DEFAULT_INIT_FLAGS,
	_IRQ_PER_CPU		= IRQ_PER_CPU,
	_IRQ_LEVEL		= IRQ_LEVEL,
	_IRQ_NOPROBE		= IRQ_NOPROBE,
	_IRQ_NOREQUEST		= IRQ_NOREQUEST,
	_IRQ_NOAUTOEN		= IRQ_NOAUTOEN,
	_IRQ_MOVE_PCNTXT	= IRQ_MOVE_PCNTXT,
	_IRQ_NO_BALANCING	= IRQ_NO_BALANCING,
	_IRQ_NESTED_THREAD	= IRQ_NESTED_THREAD,
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
#undef IRQ_AFFINITY_SET
#define IRQ_AFFINITY_SET	GOT_YOU_MORON
#undef IRQ_LEVEL
#define IRQ_LEVEL		GOT_YOU_MORON
#undef IRQ_NOPROBE
#define IRQ_NOPROBE		GOT_YOU_MORON
#undef IRQ_NOREQUEST
#define IRQ_NOREQUEST		GOT_YOU_MORON
#undef IRQ_NOAUTOEN
#define IRQ_NOAUTOEN		GOT_YOU_MORON
#undef IRQ_NESTED_THREAD
#define IRQ_NESTED_THREAD	GOT_YOU_MORON
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

static inline u32 irq_settings_get_trigger_mask(struct irq_desc *desc)
{
	return desc->status & IRQ_TYPE_SENSE_MASK;
}

static inline void
irq_settings_set_trigger_mask(struct irq_desc *desc, u32 mask)
{
	desc->status &= ~IRQ_TYPE_SENSE_MASK;
	desc->status |= mask & IRQ_TYPE_SENSE_MASK;
}

static inline bool irq_settings_is_level(struct irq_desc *desc)
{
	return desc->status & _IRQ_LEVEL;
}

static inline void irq_settings_clr_level(struct irq_desc *desc)
{
	desc->status &= ~_IRQ_LEVEL;
}

static inline void irq_settings_set_level(struct irq_desc *desc)
{
	desc->status |= _IRQ_LEVEL;
}

static inline bool irq_settings_can_request(struct irq_desc *desc)
{
	return !(desc->status & _IRQ_NOREQUEST);
}

static inline void irq_settings_clr_norequest(struct irq_desc *desc)
{
	desc->status &= ~_IRQ_NOREQUEST;
}

static inline void irq_settings_set_norequest(struct irq_desc *desc)
{
	desc->status |= _IRQ_NOREQUEST;
}

static inline bool irq_settings_can_probe(struct irq_desc *desc)
{
	return !(desc->status & _IRQ_NOPROBE);
}

static inline void irq_settings_clr_noprobe(struct irq_desc *desc)
{
	desc->status &= ~_IRQ_NOPROBE;
}

static inline void irq_settings_set_noprobe(struct irq_desc *desc)
{
	desc->status |= _IRQ_NOPROBE;
}

static inline bool irq_settings_can_move_pcntxt(struct irq_desc *desc)
{
	return desc->status & _IRQ_MOVE_PCNTXT;
}

static inline bool irq_settings_can_autoenable(struct irq_desc *desc)
{
	return !(desc->status & _IRQ_NOAUTOEN);
}

static inline bool irq_settings_is_nested_thread(struct irq_desc *desc)
{
	return desc->status & _IRQ_NESTED_THREAD;
}

/* Nothing should touch desc->status from now on */
#define status		USE_THE_PROPER_WRAPPERS_YOU_MORON
