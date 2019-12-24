/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __DT_BINDINGS_POWER_RV1126_POWER_H__
#define __DT_BINDINGS_POWER_RV1126_POWER_H__

/* VD_CORE */
#define RV1126_PD_CPU_0		0
#define RV1126_PD_CPU_1		1
#define RV1126_PD_CPU_2		2
#define RV1126_PD_CPU_3		3
#define RV1126_PD_CORE_ALIVE	4

/* VD_PMU */
#define RV1126_PD_PMU           5
#define RV1126_PD_PMU_ALIVE	6

/* VD_NPU */
#define RV1126_PD_NPU		7

/* VD_VEPU */
#define RV1126_PD_VEPU		8

/* VD_LOGIC */
#define RV1126_PD_VI		9
#define RV1126_PD_VO		10
#define RV1126_PD_ISPP		11
#define RV1126_PD_VDPU		12
#define RV1126_PD_CRYPTO	13
#define RV1126_PD_DDR		14
#define RV1126_PD_NVM		15
#define RV1126_PD_SDIO		16
#define RV1126_PD_USB		17
#define RV1126_PD_LOGIC_ALIVE	18

#endif
