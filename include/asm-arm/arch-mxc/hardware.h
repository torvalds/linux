/*
 *  Copyright 2004-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*!
 * @file hardware.h
 * @brief This file contains the hardware definitions of the board.
 *
 * @ingroup System
 */
#ifndef __ASM_ARCH_MXC_HARDWARE_H__
#define __ASM_ARCH_MXC_HARDWARE_H__

#include <asm/sizes.h>

#include <asm/arch/mx31.h>

#include <asm/arch/mxc.h>

#define MXC_MAX_GPIO_LINES      (GPIO_NUM_PIN * GPIO_PORT_NUM)

/*
 * ---------------------------------------------------------------------------
 * Board specific defines
 * ---------------------------------------------------------------------------
 */
#define MXC_EXP_IO_BASE         (MXC_GPIO_INT_BASE + MXC_MAX_GPIO_LINES)

#include <asm/arch/board-mx31ads.h>

#ifndef MXC_MAX_EXP_IO_LINES
#define MXC_MAX_EXP_IO_LINES 0
#endif

#define MXC_MAX_VIRTUAL_INTS	16
#define MXC_VIRTUAL_INTS_BASE	(MXC_EXP_IO_BASE + MXC_MAX_EXP_IO_LINES)
#define MXC_SDIO1_CARD_IRQ	MXC_VIRTUAL_INTS_BASE
#define MXC_SDIO2_CARD_IRQ	(MXC_VIRTUAL_INTS_BASE + 1)
#define MXC_SDIO3_CARD_IRQ	(MXC_VIRTUAL_INTS_BASE + 2)

#define MXC_MAX_INTS            (MXC_MAX_INT_LINES + \
                                MXC_MAX_GPIO_LINES + \
                                MXC_MAX_EXP_IO_LINES + \
                                MXC_MAX_VIRTUAL_INTS)

#endif				/* __ASM_ARCH_MXC_HARDWARE_H__ */
