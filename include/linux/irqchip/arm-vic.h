/*
 *  arch/arm/include/asm/hardware/vic.h
 *
 *  Copyright (c) ARM Limited 2003.  All rights reserved.
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
#ifndef __ASM_ARM_HARDWARE_VIC_H
#define __ASM_ARM_HARDWARE_VIC_H

#include <linux/types.h>

#define VIC_RAW_STATUS			0x08
#define VIC_INT_ENABLE			0x10	/* 1 = enable, 0 = disable */
#define VIC_INT_ENABLE_CLEAR		0x14

struct device_node;
struct pt_regs;

void __vic_init(void __iomem *base, int irq_start, u32 vic_sources,
		u32 resume_sources, struct device_node *node);
void vic_init(void __iomem *base, unsigned int irq_start, u32 vic_sources, u32 resume_sources);

#endif
