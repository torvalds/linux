/*
 * include/asm-arm/plat-orion/irq.h
 *
 * Marvell Orion SoC IRQ handling.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __ASM_PLAT_ORION_IRQ_H
#define __ASM_PLAT_ORION_IRQ_H

void orion_irq_init(unsigned int irq_start, void __iomem *maskaddr);


#endif
