/*
 * include/asm-arm/arch-netx/hardware.h
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
#ifndef __ASM_ARCH_HARDWARE_H
#define __ASM_ARCH_HARDWARE_H

#define NETX_IO_PHYS	0x00100000
#define NETX_IO_VIRT	0xe0000000
#define NETX_IO_SIZE	0x00100000

#define SRAM_INTERNAL_PHYS_0 0x00000
#define SRAM_INTERNAL_PHYS_1 0x08000
#define SRAM_INTERNAL_PHYS_2 0x10000
#define SRAM_INTERNAL_PHYS_3 0x18000
#define SRAM_INTERNAL_PHYS(no) ((no) * 0x8000)

#define XPEC_MEM_SIZE 0x4000
#define XMAC_MEM_SIZE 0x1000
#define SRAM_MEM_SIZE 0x8000

#define io_p2v(x) ((x) - NETX_IO_PHYS + NETX_IO_VIRT)
#define io_v2p(x) ((x) - NETX_IO_VIRT + NETX_IO_PHYS)

#endif
