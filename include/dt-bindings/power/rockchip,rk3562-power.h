/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (c) 2022-2024 Rockchip Electronics Co., Ltd.
 */
#ifndef __DT_BINDINGS_POWER_RK3562_POWER_H__
#define __DT_BINDINGS_POWER_RK3562_POWER_H__

/* VD_CORE */
#define RK3562_PD_CPU_0		0
#define RK3562_PD_CPU_1		1
#define RK3562_PD_CPU_2		2
#define RK3562_PD_CPU_3		3
#define RK3562_PD_CORE_ALIVE	4

/* VD_PMU */
#define RK3562_PD_PMU		5
#define RK3562_PD_PMU_ALIVE	6

/* VD_NPU */
#define RK3562_PD_NPU		7

/* VD_GPU */
#define RK3562_PD_GPU		8

/* VD_LOGIC */
#define RK3562_PD_DDR		9
#define RK3562_PD_VEPU		10
#define RK3562_PD_VDPU		11
#define RK3562_PD_VI		12
#define RK3562_PD_VO		13
#define RK3562_PD_RGA		14
#define RK3562_PD_PHP		15
#define RK3562_PD_LOGIC_ALIVE	16

#endif
