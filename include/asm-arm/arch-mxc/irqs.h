/*
 *  Copyright 2004-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_MXC_IRQS_H__
#define __ASM_ARCH_MXC_IRQS_H__

#include <asm/hardware.h>

/*!
 * @file irqs.h
 * @brief This file defines the number of normal interrupts and fast interrupts
 *
 * @ingroup Interrupt
 */

#define MXC_IRQ_TO_EXPIO(irq)	((irq) - MXC_EXP_IO_BASE)

#define MXC_IRQ_TO_GPIO(irq)	((irq) - MXC_GPIO_INT_BASE)
#define MXC_GPIO_TO_IRQ(x)	(MXC_GPIO_INT_BASE + x)

/*!
 * Number of normal interrupts
 */
#define NR_IRQS		MXC_MAX_INTS

/*!
 * Number of fast interrupts
 */
#define NR_FIQS		MXC_MAX_INTS

#endif				/* __ASM_ARCH_MXC_IRQS_H__ */
