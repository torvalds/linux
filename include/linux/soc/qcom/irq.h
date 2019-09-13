/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __QCOM_IRQ_H
#define __QCOM_IRQ_H

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

#endif
