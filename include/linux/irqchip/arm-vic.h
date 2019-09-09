/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  arch/arm/include/asm/hardware/vic.h
 *
 *  Copyright (c) ARM Limited 2003.  All rights reserved.
 */
#ifndef __ASM_ARM_HARDWARE_VIC_H
#define __ASM_ARM_HARDWARE_VIC_H

#include <linux/types.h>

#define VIC_RAW_STATUS			0x08
#define VIC_INT_ENABLE			0x10	/* 1 = enable, 0 = disable */
#define VIC_INT_ENABLE_CLEAR		0x14

struct device_node;
struct pt_regs;

void __vic_init(void __iomem *base, int parent_irq, int irq_start,
		u32 vic_sources, u32 resume_sources, struct device_node *node);
void vic_init(void __iomem *base, unsigned int irq_start, u32 vic_sources, u32 resume_sources);
int vic_init_cascaded(void __iomem *base, unsigned int parent_irq,
		      u32 vic_sources, u32 resume_sources);

#endif
