/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020 The Linux Foundation. All rights reserved.
 */

#ifndef _DT_BINDINGS_QCOM_SPMI_VADC_PMR735A_H
#define _DT_BINDINGS_QCOM_SPMI_VADC_PMR735A_H

#ifndef PMR735A_SID
#define PMR735A_SID					4
#endif

/* ADC channels for PMR735A_ADC for PMIC7 */
#define PMR735A_ADC7_REF_GND			(PMR735A_SID << 8 | 0x0)
#define PMR735A_ADC7_1P25VREF			(PMR735A_SID << 8 | 0x01)
#define PMR735A_ADC7_VREF_VADC			(PMR735A_SID << 8 | 0x02)
#define PMR735A_ADC7_DIE_TEMP			(PMR735A_SID << 8 | 0x03)

#define PMR735A_ADC7_GPIO1			(PMR735A_SID << 8 | 0x0a)
#define PMR735A_ADC7_GPIO2			(PMR735A_SID << 8 | 0x0b)
#define PMR735A_ADC7_GPIO3			(PMR735A_SID << 8 | 0x0c)

/* 100k pull-up2 */
#define PMR735A_ADC7_GPIO1_100K_PU		(PMR735A_SID << 8 | 0x4a)
#define PMR735A_ADC7_GPIO2_100K_PU		(PMR735A_SID << 8 | 0x4b)
#define PMR735A_ADC7_GPIO3_100K_PU		(PMR735A_SID << 8 | 0x4c)

#endif /* _DT_BINDINGS_QCOM_SPMI_VADC_PMR735A_H */
