/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Samsung Exynos PPMU event types for counting in regs
 *
 * Copyright (c) 2019, Samsung Electronics
 * Author: Lukasz Luba <l.luba@partner.samsung.com>
 */

#ifndef __DT_BINDINGS_PMU_EXYNOS_PPMU_H
#define __DT_BINDINGS_PMU_EXYNOS_PPMU_H

#define PPMU_RO_BUSY_CYCLE_CNT		0x0
#define PPMU_WO_BUSY_CYCLE_CNT		0x1
#define PPMU_RW_BUSY_CYCLE_CNT		0x2
#define PPMU_RO_REQUEST_CNT		0x3
#define PPMU_WO_REQUEST_CNT		0x4
#define PPMU_RO_DATA_CNT		0x5
#define PPMU_WO_DATA_CNT		0x6
#define PPMU_RO_LATENCY			0x12
#define PPMU_WO_LATENCY			0x16
#define PPMU_V2_RO_DATA_CNT		0x4
#define PPMU_V2_WO_DATA_CNT		0x5
#define PPMU_V2_EVT3_RW_DATA_CNT	0x22

#endif
