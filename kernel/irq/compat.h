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
static inline void irq_compat_set_pending(struct irq_desc *desc)
{
	desc->status |= IRQ_PENDING;
}

static inline void irq_compat_clr_pending(struct irq_desc *desc)
{
	desc->status &= ~IRQ_PENDING;
}
static inline void irq_compat_set_masked(struct irq_desc *desc)
{
	desc->status |= IRQ_MASKED;
}

static inline void irq_compat_clr_masked(struct irq_desc *desc)
{
	desc->status &= ~IRQ_MASKED;
}
#else
static inline void irq_compat_set_progress(struct irq_desc *desc) { }
static inline void irq_compat_clr_progress(struct irq_desc *desc) { }
static inline void irq_compat_set_disabled(struct irq_desc *desc) { }
static inline void irq_compat_clr_disabled(struct irq_desc *desc) { }
static inline void irq_compat_set_pending(struct irq_desc *desc) { }
static inline void irq_compat_clr_pending(struct irq_desc *desc) { }
static inline void irq_compat_set_masked(struct irq_desc *desc) { }
static inline void irq_compat_clr_masked(struct irq_desc *desc) { }
#endif

