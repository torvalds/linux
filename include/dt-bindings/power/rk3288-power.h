/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
#ifndef __DT_BINDINGS_POWER_RK3288_POWER_H__
#define __DT_BINDINGS_POWER_RK3288_POWER_H__

/**
 * RK3288 Power Domain and Voltage Domain Summary.
 */

/* VD_CORE */
#define RK3288_PD_A17_0		0
#define RK3288_PD_A17_1		1
#define RK3288_PD_A17_2		2
#define RK3288_PD_A17_3		3
#define RK3288_PD_SCU		4
#define RK3288_PD_DEBUG		5
#define RK3288_PD_MEM		6

/* VD_LOGIC */
#define RK3288_PD_BUS		7
#define RK3288_PD_PERI		8
#define RK3288_PD_VIO		9
#define RK3288_PD_ALIVE		10
#define RK3288_PD_HEVC		11
#define RK3288_PD_VIDEO		12

/* VD_GPU */
#define RK3288_PD_GPU		13

/* VD_PMU */
#define RK3288_PD_PMU		14

#endif
