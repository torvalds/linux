/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_CONTEXT_TRACKING_IRQ_H
#define _LINUX_CONTEXT_TRACKING_IRQ_H

#ifdef CONFIG_CONTEXT_TRACKING_IDLE
void ct_irq_enter(void);
void ct_irq_exit(void);
void ct_irq_enter_irqson(void);
void ct_irq_exit_irqson(void);
void ct_nmi_enter(void);
void ct_nmi_exit(void);
#else
static __always_inline void ct_irq_enter(void) { }
static __always_inline void ct_irq_exit(void) { }
static inline void ct_irq_enter_irqson(void) { }
static inline void ct_irq_exit_irqson(void) { }
static __always_inline void ct_nmi_enter(void) { }
static __always_inline void ct_nmi_exit(void) { }
#endif

#endif
