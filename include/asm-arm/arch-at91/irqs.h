/*
 * include/asm-arm/arch-at91/irqs.h
 *
 *  Copyright (C) 2004 SAN People
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __ASM_ARCH_IRQS_H
#define __ASM_ARCH_IRQS_H

#include <asm/io.h>
#include <asm/arch/at91_aic.h>

#define NR_AIC_IRQS 32


/*
 * Acknowledge interrupt with AIC after interrupt has been handled.
 *   (by kernel/irq.c)
 */
#define irq_finish(irq) do { at91_sys_write(AT91_AIC_EOICR, 0); } while (0)


/*
 * IRQ interrupt symbols are the AT91xxx_ID_* symbols
 * for IRQs handled directly through the AIC, or else the AT91_PIN_*
 * symbols in gpio.h for ones handled indirectly as GPIOs.
 * We make provision for 5 banks of GPIO.
 */
#define	NR_IRQS		(NR_AIC_IRQS + (5 * 32))

#endif
