/* SPDX-License-Identifier: (GPL-2.0-only OR MIT) */
/*
 * Copyright (c) 2024 Amlogic, Inc. All rights reserved.
 * Author: Xianwei Zhao <xianwei.zhao@amlogic.com>
 */

#ifndef _DT_BINDINGS_AMLOGIC_PINCTRL_H
#define _DT_BINDINGS_AMLOGIC_PINCTRL_H
/* Normal PIN bank */
#define AMLOGIC_GPIO_A		0
#define AMLOGIC_GPIO_B		1
#define AMLOGIC_GPIO_C		2
#define AMLOGIC_GPIO_D		3
#define AMLOGIC_GPIO_E		4
#define AMLOGIC_GPIO_F		5
#define AMLOGIC_GPIO_G		6
#define AMLOGIC_GPIO_H		7
#define AMLOGIC_GPIO_I		8
#define AMLOGIC_GPIO_J		9
#define AMLOGIC_GPIO_K		10
#define AMLOGIC_GPIO_L		11
#define AMLOGIC_GPIO_M		12
#define AMLOGIC_GPIO_N		13
#define AMLOGIC_GPIO_O		14
#define AMLOGIC_GPIO_P		15
#define AMLOGIC_GPIO_Q		16
#define AMLOGIC_GPIO_R		17
#define AMLOGIC_GPIO_S		18
#define AMLOGIC_GPIO_T		19
#define AMLOGIC_GPIO_U		20
#define AMLOGIC_GPIO_V		21
#define AMLOGIC_GPIO_W		22
#define AMLOGIC_GPIO_X		23
#define AMLOGIC_GPIO_Y		24
#define AMLOGIC_GPIO_Z		25

/* Special PIN bank */
#define AMLOGIC_GPIO_DV		26
#define AMLOGIC_GPIO_AO		27
#define AMLOGIC_GPIO_CC		28
#define AMLOGIC_GPIO_TEST_N	29
#define AMLOGIC_GPIO_ANALOG	30

#define AML_PINMUX(bank, offset, mode)	(((((bank) << 8) + (offset)) << 8) | (mode))

#endif /* _DT_BINDINGS_AMLOGIC_PINCTRL_H */
