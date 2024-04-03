/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

#ifndef _DT_BINDINGS_QCOM_SPMI_VADC_PM8350_H
#define _DT_BINDINGS_QCOM_SPMI_VADC_PM8350_H

#include <dt-bindings/iio/qcom,spmi-vadc.h>

/* ADC channels for PM8350_ADC for PMIC7 */
#define PM8350_ADC7_REF_GND(sid)			((sid) << 8 | ADC7_REF_GND)
#define PM8350_ADC7_1P25VREF(sid)			((sid) << 8 | ADC7_1P25VREF)
#define PM8350_ADC7_VREF_VADC(sid)			((sid) << 8 | ADC7_VREF_VADC)
#define PM8350_ADC7_DIE_TEMP(sid)			((sid) << 8 | ADC7_DIE_TEMP)

#define PM8350_ADC7_AMUX_THM1(sid)			((sid) << 8 | ADC7_AMUX_THM1)
#define PM8350_ADC7_AMUX_THM2(sid)			((sid) << 8 | ADC7_AMUX_THM2)
#define PM8350_ADC7_AMUX_THM3(sid)			((sid) << 8 | ADC7_AMUX_THM3)
#define PM8350_ADC7_AMUX_THM4(sid)			((sid) << 8 | ADC7_AMUX_THM4)
#define PM8350_ADC7_AMUX_THM5(sid)			((sid) << 8 | ADC7_AMUX_THM5)
#define PM8350_ADC7_GPIO1(sid)				((sid) << 8 | ADC7_GPIO1)
#define PM8350_ADC7_GPIO2(sid)				((sid) << 8 | ADC7_GPIO2)
#define PM8350_ADC7_GPIO3(sid)				((sid) << 8 | ADC7_GPIO3)
#define PM8350_ADC7_GPIO4(sid)				((sid) << 8 | ADC7_GPIO4)

/* 30k pull-up1 */
#define PM8350_ADC7_AMUX_THM1_30K_PU(sid)		((sid) << 8 | ADC7_AMUX_THM1_30K_PU)
#define PM8350_ADC7_AMUX_THM2_30K_PU(sid)		((sid) << 8 | ADC7_AMUX_THM2_30K_PU)
#define PM8350_ADC7_AMUX_THM3_30K_PU(sid)		((sid) << 8 | ADC7_AMUX_THM3_30K_PU)
#define PM8350_ADC7_AMUX_THM4_30K_PU(sid)		((sid) << 8 | ADC7_AMUX_THM4_30K_PU)
#define PM8350_ADC7_AMUX_THM5_30K_PU(sid)		((sid) << 8 | ADC7_AMUX_THM5_30K_PU)
#define PM8350_ADC7_GPIO1_30K_PU(sid)			((sid) << 8 | ADC7_GPIO1_30K_PU)
#define PM8350_ADC7_GPIO2_30K_PU(sid)			((sid) << 8 | ADC7_GPIO2_30K_PU)
#define PM8350_ADC7_GPIO3_30K_PU(sid)			((sid) << 8 | ADC7_GPIO3_30K_PU)
#define PM8350_ADC7_GPIO4_30K_PU(sid)			((sid) << 8 | ADC7_GPIO4_30K_PU)

/* 100k pull-up2 */
#define PM8350_ADC7_AMUX_THM1_100K_PU(sid)		((sid) << 8 | ADC7_AMUX_THM1_100K_PU)
#define PM8350_ADC7_AMUX_THM2_100K_PU(sid)		((sid) << 8 | ADC7_AMUX_THM2_100K_PU)
#define PM8350_ADC7_AMUX_THM3_100K_PU(sid)		((sid) << 8 | ADC7_AMUX_THM3_100K_PU)
#define PM8350_ADC7_AMUX_THM4_100K_PU(sid)		((sid) << 8 | ADC7_AMUX_THM4_100K_PU)
#define PM8350_ADC7_AMUX_THM5_100K_PU(sid)		((sid) << 8 | ADC7_AMUX_THM5_100K_PU)
#define PM8350_ADC7_GPIO1_100K_PU(sid)			((sid) << 8 | ADC7_GPIO1_100K_PU)
#define PM8350_ADC7_GPIO2_100K_PU(sid)			((sid) << 8 | ADC7_GPIO2_100K_PU)
#define PM8350_ADC7_GPIO3_100K_PU(sid)			((sid) << 8 | ADC7_GPIO3_100K_PU)
#define PM8350_ADC7_GPIO4_100K_PU(sid)			((sid) << 8 | ADC7_GPIO4_100K_PU)

/* 400k pull-up3 */
#define PM8350_ADC7_AMUX_THM1_400K_PU(sid)		((sid) << 8 | ADC7_AMUX_THM1_400K_PU)
#define PM8350_ADC7_AMUX_THM2_400K_PU(sid)		((sid) << 8 | ADC7_AMUX_THM2_400K_PU)
#define PM8350_ADC7_AMUX_THM3_400K_PU(sid)		((sid) << 8 | ADC7_AMUX_THM3_400K_PU)
#define PM8350_ADC7_AMUX_THM4_400K_PU(sid)		((sid) << 8 | ADC7_AMUX_THM4_400K_PU)
#define PM8350_ADC7_AMUX_THM5_400K_PU(sid)		((sid) << 8 | ADC7_AMUX_THM5_400K_PU)
#define PM8350_ADC7_GPIO1_400K_PU(sid)			((sid) << 8 | ADC7_GPIO1_400K_PU)
#define PM8350_ADC7_GPIO2_400K_PU(sid)			((sid) << 8 | ADC7_GPIO2_400K_PU)
#define PM8350_ADC7_GPIO3_400K_PU(sid)			((sid) << 8 | ADC7_GPIO3_400K_PU)
#define PM8350_ADC7_GPIO4_400K_PU(sid)			((sid) << 8 | ADC7_GPIO4_400K_PU)

/* 1/3 Divider */
#define PM8350_ADC7_GPIO4_DIV3(sid)			((sid) << 8 | ADC7_GPIO4_DIV3)

#define PM8350_ADC7_VPH_PWR(sid)			((sid) << 8 | ADC7_VPH_PWR)

#endif /* _DT_BINDINGS_QCOM_SPMI_VADC_PM8350_H */
