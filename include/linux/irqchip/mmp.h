/* SPDX-License-Identifier: GPL-2.0 */
#ifndef	__IRQCHIP_MMP_H
#define	__IRQCHIP_MMP_H

extern struct irq_chip icu_irq_chip;

extern void icu_init_irq(void);
extern void mmp2_init_icu(void);

#endif	/* __IRQCHIP_MMP_H */
