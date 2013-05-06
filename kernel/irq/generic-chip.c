/*
 * Library implementing the most common irq chip callback functions
 *
 * Copyright (C) 2011, Thomas Gleixner
 */
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/syscore_ops.h>

#include "internals.h"

static LIST_HEAD(gc_list);
static DEFINE_RAW_SPINLOCK(gc_lock);

/**
 * irq_gc_noop - NOOP function
 * @d: irq_data
 */
void irq_gc_noop(struct irq_data *d)
{
}

/**
 * irq_gc_mask_disable_reg - Mask chip via disable register
 * @d: irq_data
 *
 * Chip has separate enable/disable registers instead of a single mask
 * register.
 */
void irq_gc_mask_disable_reg(struct irq_data *d)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct irq_chip_type *ct = irq_data_get_chip_type(d);
	u32 mask = 1 << (d->irq - gc->irq_base);

	irq_gc_lock(gc);
	irq_reg_writel(mask, gc->reg_base + ct->regs.disable);
	*ct->mask_cache &= ~mask;
	irq_gc_unlock(gc);
}

/**
 * irq_gc_mask_set_mask_bit - Mask chip via setting bit in mask register
 * @d: irq_data
 *
 * Chip has a single mask register. Values of this register are cached
 * and protected by gc->lock
 */
void irq_gc_mask_set_bit(struct irq_data *d)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct irq_chip_type *ct = irq_data_get_chip_type(d);
	u32 mask = 1 << (d->irq - gc->irq_base);

	irq_gc_lock(gc);
	*ct->mask_cache |= mask;
	irq_reg_writel(*ct->mask_cache, gc->reg_base + ct->regs.mask);
	irq_gc_unlock(gc);
}

/**
 * irq_gc_mask_set_mask_bit - Mask chip via clearing bit in mask register
 * @d: irq_data
 *
 * Chip has a single mask register. Values of this register are cached
 * and protected by gc->lock
 */
void irq_gc_mask_clr_bit(struct irq_data *d)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct irq_chip_type *ct = irq_data_get_chip_type(d);
	u32 mask = 1 << (d->irq - gc->irq_base);

	irq_gc_lock(gc);
	*ct->mask_cache &= ~mask;
	irq_reg_writel(*ct->mask_cache, gc->reg_base + ct->regs.mask);
	irq_gc_unlock(gc);
}

/**
 * irq_gc_unmask_enable_reg - Unmask chip via enable register
 * @d: irq_data
 *
 * Chip has separate enable/disable registers instead of a single mask
 * register.
 */
void irq_gc_unmask_enable_reg(struct irq_data *d)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct irq_chip_type *ct = irq_data_get_chip_type(d);
	u32 mask = 1 << (d->irq - gc->irq_base);

	irq_gc_lock(gc);
	irq_reg_writel(mask, gc->reg_base + ct->regs.enable);
	*ct->mask_cache |= mask;
	irq_gc_unlock(gc);
}

/**
 * irq_gc_ack_set_bit - Ack pending interrupt via setting bit
 * @d: irq_data
 */
void irq_gc_ack_set_bit(struct irq_data *d)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct irq_chip_type *ct = irq_data_get_chip_type(d);
	u32 mask = 1 << (d->irq - gc->irq_base);

	irq_gc_lock(gc);
	irq_reg_writel(mask, gc->reg_base + ct->regs.ack);
	irq_gc_unlock(gc);
}

/**
 * irq_gc_ack_clr_bit - Ack pending interrupt via clearing bit
 * @d: irq_data
 */
void irq_gc_ack_clr_bit(struct irq_data *d)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct irq_chip_type *ct = irq_data_get_chip_type(d);
	u32 mask = ~(1 << (d->irq - gc->irq_base));

	irq_gc_lock(gc);
	irq_reg_writel(mask, gc->reg_base + ct->regs.ack);
	irq_gc_unlock(gc);
}

/**
 * irq_gc_mask_disable_reg_and_ack- Mask and ack pending interrupt
 * @d: irq_data
 */
void irq_gc_mask_disable_reg_and_ack(struct irq_data *d)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct irq_chip_type *ct = irq_data_get_chip_type(d);
	u32 mask = 1 << (d->irq - gc->irq_base);

	irq_gc_lock(gc);
	irq_reg_writel(mask, gc->reg_base + ct->regs.mask);
	irq_reg_writel(mask, gc->reg_base + ct->regs.ack);
	irq_gc_unlock(gc);
}

/**
 * irq_gc_eoi - EOI interrupt
 * @d: irq_data
 */
void irq_gc_eoi(struct irq_data *d)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct irq_chip_type *ct = irq_data_get_chip_type(d);
	u32 mask = 1 << (d->irq - gc->irq_base);

	irq_gc_lock(gc);
	irq_reg_writel(mask, gc->reg_base + ct->regs.eoi);
	irq_gc_unlock(gc);
}

/**
 * irq_gc_set_wake - Set/clr wake bit for an interrupt
 * @d: irq_data
 *
 * For chips where the wake from suspend functionality is not
 * configured in a separate register and the wakeup active state is
 * just stored in a bitmask.
 */
int irq_gc_set_wake(struct irq_data *d, unsigned int on)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	u32 mask = 1 << (d->irq - gc->irq_base);

	if (!(mask & gc->wake_enabled))
		return -EINVAL;

	irq_gc_lock(gc);
	if (on)
		gc->wake_active |= mask;
	else
		gc->wake_active &= ~mask;
	irq_gc_unlock(gc);
	return 0;
}

/**
 * irq_alloc_generic_chip - Allocate a generic chip and initialize it
 * @name:	Name of the irq chip
 * @num_ct:	Number of irq_chip_type instances associated with this
 * @irq_base:	Interrupt base nr for this chip
 * @reg_base:	Register base address (virtual)
 * @handler:	Default flow handler associated with this chip
 *
 * Returns an initialized irq_chip_generic structure. The chip defaults
 * to the primary (index 0) irq_chip_type and @handler
 */
struct irq_chip_generic *
irq_alloc_generic_chip(const char *name, int num_ct, unsigned int irq_base,
		       void __iomem *reg_base, irq_flow_handler_t handler)
{
	struct irq_chip_generic *gc;
	unsigned long sz = sizeof(*gc) + num_ct * sizeof(struct irq_chip_type);

	gc = kzalloc(sz, GFP_KERNEL);
	if (gc) {
		raw_spin_lock_init(&gc->lock);
		gc->num_ct = num_ct;
		gc->irq_base = irq_base;
		gc->reg_base = reg_base;
		gc->chip_types->chip.name = name;
		gc->chip_types->handler = handler;
	}
	return gc;
}
EXPORT_SYMBOL_GPL(irq_alloc_generic_chip);

/*
 * Separate lockdep class for interrupt chip which can nest irq_desc
 * lock.
 */
static struct lock_class_key irq_nested_lock_class;

/**
 * irq_setup_generic_chip - Setup a range of interrupts with a generic chip
 * @gc:		Generic irq chip holding all data
 * @msk:	Bitmask holding the irqs to initialize relative to gc->irq_base
 * @flags:	Flags for initialization
 * @clr:	IRQ_* bits to clear
 * @set:	IRQ_* bits to set
 *
 * Set up max. 32 interrupts starting from gc->irq_base. Note, this
 * initializes all interrupts to the primary irq_chip_type and its
 * associated handler.
 */
void irq_setup_generic_chip(struct irq_chip_generic *gc, u32 msk,
			    enum irq_gc_flags flags, unsigned int clr,
			    unsigned int set)
{
	struct irq_chip_type *ct = gc->chip_types;
	unsigned int i;

	raw_spin_lock(&gc_lock);
	list_add_tail(&gc->list, &gc_list);
	raw_spin_unlock(&gc_lock);

	/* Init mask cache ? */
	if (flags & IRQ_GC_INIT_MASK_CACHE)
		gc->mask_cache = irq_reg_readl(gc->reg_base + ct->regs.mask);

	/* Initialize mask cache pointer */
	for (i = 0; i < gc->num_ct; i++)
		ct[i].mask_cache = &gc->mask_cache;

	for (i = gc->irq_base; msk; msk >>= 1, i++) {
		if (!(msk & 0x01))
			continue;

		if (flags & IRQ_GC_INIT_NESTED_LOCK)
			irq_set_lockdep_class(i, &irq_nested_lock_class);

		irq_set_chip_and_handler(i, &ct->chip, ct->handler);
		irq_set_chip_data(i, gc);
		irq_modify_status(i, clr, set);
	}
	gc->irq_cnt = i - gc->irq_base;
}
EXPORT_SYMBOL_GPL(irq_setup_generic_chip);

/**
 * irq_setup_alt_chip - Switch to alternative chip
 * @d:		irq_data for this interrupt
 * @type	Flow type to be initialized
 *
 * Only to be called from chip->irq_set_type() callbacks.
 */
int irq_setup_alt_chip(struct irq_data *d, unsigned int type)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct irq_chip_type *ct = gc->chip_types;
	unsigned int i;

	for (i = 0; i < gc->num_ct; i++, ct++) {
		if (ct->type & type) {
			d->chip = &ct->chip;
			irq_data_to_desc(d)->handle_irq = ct->handler;
			return 0;
		}
	}
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(irq_setup_alt_chip);

/**
 * irq_remove_generic_chip - Remove a chip
 * @gc:		Generic irq chip holding all data
 * @msk:	Bitmask holding the irqs to initialize relative to gc->irq_base
 * @clr:	IRQ_* bits to clear
 * @set:	IRQ_* bits to set
 *
 * Remove up to 32 interrupts starting from gc->irq_base.
 */
void irq_remove_generic_chip(struct irq_chip_generic *gc, u32 msk,
			     unsigned int clr, unsigned int set)
{
	unsigned int i = gc->irq_base;

	raw_spin_lock(&gc_lock);
	list_del(&gc->list);
	raw_spin_unlock(&gc_lock);

	for (; msk; msk >>= 1, i++) {
		if (!(msk & 0x01))
			continue;

		/* Remove handler first. That will mask the irq line */
		irq_set_handler(i, NULL);
		irq_set_chip(i, &no_irq_chip);
		irq_set_chip_data(i, NULL);
		irq_modify_status(i, clr, set);
	}
}
EXPORT_SYMBOL_GPL(irq_remove_generic_chip);

#ifdef CONFIG_PM
static int irq_gc_suspend(void)
{
	struct irq_chip_generic *gc;

	list_for_each_entry(gc, &gc_list, list) {
		struct irq_chip_type *ct = gc->chip_types;

		if (ct->chip.irq_suspend)
			ct->chip.irq_suspend(irq_get_irq_data(gc->irq_base));
	}
	return 0;
}

static void irq_gc_resume(void)
{
	struct irq_chip_generic *gc;

	list_for_each_entry(gc, &gc_list, list) {
		struct irq_chip_type *ct = gc->chip_types;

		if (ct->chip.irq_resume)
			ct->chip.irq_resume(irq_get_irq_data(gc->irq_base));
	}
}
#else
#define irq_gc_suspend NULL
#define irq_gc_resume NULL
#endif

static void irq_gc_shutdown(void)
{
	struct irq_chip_generic *gc;

	list_for_each_entry(gc, &gc_list, list) {
		struct irq_chip_type *ct = gc->chip_types;

		if (ct->chip.irq_pm_shutdown)
			ct->chip.irq_pm_shutdown(irq_get_irq_data(gc->irq_base));
	}
}

static struct syscore_ops irq_gc_syscore_ops = {
	.suspend = irq_gc_suspend,
	.resume = irq_gc_resume,
	.shutdown = irq_gc_shutdown,
};

static int __init irq_gc_init_ops(void)
{
	register_syscore_ops(&irq_gc_syscore_ops);
	return 0;
}
device_initcall(irq_gc_init_ops);
