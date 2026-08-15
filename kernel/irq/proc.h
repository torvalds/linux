/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KERNEL_IRQ_PROC_H
#define _KERNEL_IRQ_PROC_H

#if defined(CONFIG_PROC_FS) && defined(CONFIG_GENERIC_IRQ_SHOW)
void irq_proc_calc_prec(void);
void irq_proc_update_chip(const struct irq_chip *chip);
#else
static inline void irq_proc_calc_prec(void) { }
static inline void irq_proc_update_chip(const struct irq_chip *chip) { }
#endif

#endif
