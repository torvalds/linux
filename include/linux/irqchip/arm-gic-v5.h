/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 ARM Limited, All Rights Reserved.
 */
#ifndef __LINUX_IRQCHIP_ARM_GIC_V5_H
#define __LINUX_IRQCHIP_ARM_GIC_V5_H

#include <asm/sysreg.h>

/*
 * INTID handling
 */
#define GICV5_HWIRQ_ID			GENMASK(23, 0)
#define GICV5_HWIRQ_TYPE		GENMASK(31, 29)
#define GICV5_HWIRQ_INTID		GENMASK_ULL(31, 0)

#define GICV5_HWIRQ_TYPE_PPI		UL(0x1)

#endif
