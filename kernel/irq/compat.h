/*
 * Compat layer for transition period
 */
#ifndef CONFIG_GENERIC_HARDIRQS_NO_COMPAT
static inline void irq_compat_set_progress(struct irq_desc *desc)
{
	desc->status |= IRQ_INPROGRESS;
}

static inline void irq_compat_clr_progress(struct irq_desc *desc)
{
	desc->status &= ~IRQ_INPROGRESS;
}
static inline void irq_compat_set_disabled(struct irq_desc *desc)
{
	desc->status |= IRQ_DISABLED;
}

static inline void irq_compat_clr_disabled(struct irq_desc *desc)
{
	desc->status &= ~IRQ_DISABLED;
}
#else
static inline void irq_compat_set_progress(struct irq_desc *desc) { }
static inline void irq_compat_clr_progress(struct irq_desc *desc) { }
static inline void irq_compat_set_disabled(struct irq_desc *desc) { }
static inline void irq_compat_clr_disabled(struct irq_desc *desc) { }
#endif

