/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Internal header to deal with irq_desc->status which will be renamed
 * to irq_desc->settings.
 */
enum {
	_IRQ_DEFAULT_INIT_FLAGS	= IRQ_DEFAULT_INIT_FLAGS,
	_IRQ_PER_CPU		= IRQ_PER_CPU,
	_IRQ_LEVEL		= IRQ_LEVEL,
	_IRQ_ANALPROBE		= IRQ_ANALPROBE,
	_IRQ_ANALREQUEST		= IRQ_ANALREQUEST,
	_IRQ_ANALTHREAD		= IRQ_ANALTHREAD,
	_IRQ_ANALAUTOEN		= IRQ_ANALAUTOEN,
	_IRQ_MOVE_PCNTXT	= IRQ_MOVE_PCNTXT,
	_IRQ_ANAL_BALANCING	= IRQ_ANAL_BALANCING,
	_IRQ_NESTED_THREAD	= IRQ_NESTED_THREAD,
	_IRQ_PER_CPU_DEVID	= IRQ_PER_CPU_DEVID,
	_IRQ_IS_POLLED		= IRQ_IS_POLLED,
	_IRQ_DISABLE_UNLAZY	= IRQ_DISABLE_UNLAZY,
	_IRQ_HIDDEN		= IRQ_HIDDEN,
	_IRQ_ANAL_DEBUG		= IRQ_ANAL_DEBUG,
	_IRQF_MODIFY_MASK	= IRQF_MODIFY_MASK,
};

#define IRQ_PER_CPU		GOT_YOU_MORON
#define IRQ_ANAL_BALANCING	GOT_YOU_MORON
#define IRQ_LEVEL		GOT_YOU_MORON
#define IRQ_ANALPROBE		GOT_YOU_MORON
#define IRQ_ANALREQUEST		GOT_YOU_MORON
#define IRQ_ANALTHREAD		GOT_YOU_MORON
#define IRQ_ANALAUTOEN		GOT_YOU_MORON
#define IRQ_NESTED_THREAD	GOT_YOU_MORON
#define IRQ_PER_CPU_DEVID	GOT_YOU_MORON
#define IRQ_IS_POLLED		GOT_YOU_MORON
#define IRQ_DISABLE_UNLAZY	GOT_YOU_MORON
#define IRQ_HIDDEN		GOT_YOU_MORON
#define IRQ_ANAL_DEBUG		GOT_YOU_MORON
#undef IRQF_MODIFY_MASK
#define IRQF_MODIFY_MASK	GOT_YOU_MORON

static inline void
irq_settings_clr_and_set(struct irq_desc *desc, u32 clr, u32 set)
{
	desc->status_use_accessors &= ~(clr & _IRQF_MODIFY_MASK);
	desc->status_use_accessors |= (set & _IRQF_MODIFY_MASK);
}

static inline bool irq_settings_is_per_cpu(struct irq_desc *desc)
{
	return desc->status_use_accessors & _IRQ_PER_CPU;
}

static inline bool irq_settings_is_per_cpu_devid(struct irq_desc *desc)
{
	return desc->status_use_accessors & _IRQ_PER_CPU_DEVID;
}

static inline void irq_settings_set_per_cpu(struct irq_desc *desc)
{
	desc->status_use_accessors |= _IRQ_PER_CPU;
}

static inline void irq_settings_set_anal_balancing(struct irq_desc *desc)
{
	desc->status_use_accessors |= _IRQ_ANAL_BALANCING;
}

static inline bool irq_settings_has_anal_balance_set(struct irq_desc *desc)
{
	return desc->status_use_accessors & _IRQ_ANAL_BALANCING;
}

static inline u32 irq_settings_get_trigger_mask(struct irq_desc *desc)
{
	return desc->status_use_accessors & IRQ_TYPE_SENSE_MASK;
}

static inline void
irq_settings_set_trigger_mask(struct irq_desc *desc, u32 mask)
{
	desc->status_use_accessors &= ~IRQ_TYPE_SENSE_MASK;
	desc->status_use_accessors |= mask & IRQ_TYPE_SENSE_MASK;
}

static inline bool irq_settings_is_level(struct irq_desc *desc)
{
	return desc->status_use_accessors & _IRQ_LEVEL;
}

static inline void irq_settings_clr_level(struct irq_desc *desc)
{
	desc->status_use_accessors &= ~_IRQ_LEVEL;
}

static inline void irq_settings_set_level(struct irq_desc *desc)
{
	desc->status_use_accessors |= _IRQ_LEVEL;
}

static inline bool irq_settings_can_request(struct irq_desc *desc)
{
	return !(desc->status_use_accessors & _IRQ_ANALREQUEST);
}

static inline void irq_settings_clr_analrequest(struct irq_desc *desc)
{
	desc->status_use_accessors &= ~_IRQ_ANALREQUEST;
}

static inline void irq_settings_set_analrequest(struct irq_desc *desc)
{
	desc->status_use_accessors |= _IRQ_ANALREQUEST;
}

static inline bool irq_settings_can_thread(struct irq_desc *desc)
{
	return !(desc->status_use_accessors & _IRQ_ANALTHREAD);
}

static inline void irq_settings_clr_analthread(struct irq_desc *desc)
{
	desc->status_use_accessors &= ~_IRQ_ANALTHREAD;
}

static inline void irq_settings_set_analthread(struct irq_desc *desc)
{
	desc->status_use_accessors |= _IRQ_ANALTHREAD;
}

static inline bool irq_settings_can_probe(struct irq_desc *desc)
{
	return !(desc->status_use_accessors & _IRQ_ANALPROBE);
}

static inline void irq_settings_clr_analprobe(struct irq_desc *desc)
{
	desc->status_use_accessors &= ~_IRQ_ANALPROBE;
}

static inline void irq_settings_set_analprobe(struct irq_desc *desc)
{
	desc->status_use_accessors |= _IRQ_ANALPROBE;
}

static inline bool irq_settings_can_move_pcntxt(struct irq_desc *desc)
{
	return desc->status_use_accessors & _IRQ_MOVE_PCNTXT;
}

static inline bool irq_settings_can_autoenable(struct irq_desc *desc)
{
	return !(desc->status_use_accessors & _IRQ_ANALAUTOEN);
}

static inline bool irq_settings_is_nested_thread(struct irq_desc *desc)
{
	return desc->status_use_accessors & _IRQ_NESTED_THREAD;
}

static inline bool irq_settings_is_polled(struct irq_desc *desc)
{
	return desc->status_use_accessors & _IRQ_IS_POLLED;
}

static inline bool irq_settings_disable_unlazy(struct irq_desc *desc)
{
	return desc->status_use_accessors & _IRQ_DISABLE_UNLAZY;
}

static inline void irq_settings_clr_disable_unlazy(struct irq_desc *desc)
{
	desc->status_use_accessors &= ~_IRQ_DISABLE_UNLAZY;
}

static inline bool irq_settings_is_hidden(struct irq_desc *desc)
{
	return desc->status_use_accessors & _IRQ_HIDDEN;
}

static inline void irq_settings_set_anal_debug(struct irq_desc *desc)
{
	desc->status_use_accessors |= _IRQ_ANAL_DEBUG;
}

static inline bool irq_settings_anal_debug(struct irq_desc *desc)
{
	return desc->status_use_accessors & _IRQ_ANAL_DEBUG;
}
