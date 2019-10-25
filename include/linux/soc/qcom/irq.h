/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __QCOM_IRQ_H
#define __QCOM_IRQ_H

#include <linux/irqdomain.h>

#define GPIO_NO_WAKE_IRQ	~0U

/**
 * QCOM specific IRQ domain flags that distinguishes the handling of wakeup
 * capable interrupts by different interrupt controllers.
 *
 * IRQ_DOMAIN_FLAG_QCOM_PDC_WAKEUP: Line must be masked at TLMM and the
 *                                  interrupt configuration is done at PDC
 * IRQ_DOMAIN_FLAG_QCOM_MPM_WAKEUP: Interrupt configuration is handled at TLMM
 */
#define IRQ_DOMAIN_FLAG_QCOM_PDC_WAKEUP		(IRQ_DOMAIN_FLAG_NONCORE << 0)
#define IRQ_DOMAIN_FLAG_QCOM_MPM_WAKEUP		(IRQ_DOMAIN_FLAG_NONCORE << 1)

/**
 * irq_domain_qcom_handle_wakeup: Return if the domain handles interrupt
 *                                configuration
 * @parent: irq domain
 *
 * This QCOM specific irq domain call returns if the interrupt controller
 * requires the interrupt be masked at the child interrupt controller.
 */
static inline bool irq_domain_qcom_handle_wakeup(struct irq_domain *parent)
{
	return (parent->flags & IRQ_DOMAIN_FLAG_QCOM_PDC_WAKEUP);
}

#endif
