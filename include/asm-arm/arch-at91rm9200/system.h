/*
 * include/asm-arm/arch-at91rm9200/system.h
 *
 *  Copyright (C) 2003 SAN People
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

#ifndef __ASM_ARCH_SYSTEM_H
#define __ASM_ARCH_SYSTEM_H

#include <asm/arch/hardware.h>

static inline void arch_idle(void)
{
	/*
	 * Disable the processor clock.  The processor will be automatically
	 * re-enabled by an interrupt or by a reset.
	 */
//	at91_sys_write(AT91_PMC_SCDR, AT91_PMC_PCK);

	/*
	 * Set the processor (CP15) into 'Wait for Interrupt' mode.
	 * Unlike disabling the processor clock via the PMC (above)
	 *  this allows the processor to be woken via JTAG.
	 */
	cpu_do_idle();
}

static inline void arch_reset(char mode)
{
	/*
	 * Perform a hardware reset with the use of the Watchdog timer.
	 */
	at91_sys_write(AT91_ST_WDMR, AT91_ST_RSTEN | AT91_ST_EXTEN | 1);
	at91_sys_write(AT91_ST_CR, AT91_ST_WDRST);
}

#endif
