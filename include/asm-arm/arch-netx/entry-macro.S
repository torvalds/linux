/*
 * include/asm-arm/arch-netx/entry-macro.S
 *
 * Low-level IRQ helper macros for Hilscher netX based platforms
 *
 * Copyright (C) 2005 Sascha Hauer <s.hauer@pengutronix.de>, Pengutronix
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
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
#include <asm/hardware.h>

		.macro  disable_fiq
		.endm

		.macro  get_irqnr_and_base, irqnr, irqstat, base, tmp
		mov	\base, #io_p2v(0x00100000)
		add	\base, \base, #0x000ff000

		ldr	\irqstat, [\base, #0]
		clz	\irqnr, \irqstat
		rsb     \irqnr, \irqnr, #31
		cmp	\irqstat, #0
		.endm

