/*
 *  include/linux/irqchip/irq-st.h
 *
 *  Copyright (C) 2014 STMicroelectronics – All Rights Reserved
 *
 *  Author: Lee Jones <lee.jones@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _DT_BINDINGS_INTERRUPT_CONTROLLER_ST_H
#define _DT_BINDINGS_INTERRUPT_CONTROLLER_ST_H

#define ST_IRQ_SYSCFG_EXT_0		0
#define ST_IRQ_SYSCFG_EXT_1		1
#define ST_IRQ_SYSCFG_EXT_2		2
#define ST_IRQ_SYSCFG_CTI_0		3
#define ST_IRQ_SYSCFG_CTI_1		4
#define ST_IRQ_SYSCFG_PMU_0		5
#define ST_IRQ_SYSCFG_PMU_1		6
#define ST_IRQ_SYSCFG_pl310_L2		7
#define ST_IRQ_SYSCFG_DISABLED		0xFFFFFFFF

#define ST_IRQ_SYSCFG_EXT_1_INV		0x1
#define ST_IRQ_SYSCFG_EXT_2_INV		0x2
#define ST_IRQ_SYSCFG_EXT_3_INV		0x4

#endif
