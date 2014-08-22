/*
 *  include/linux/power/rt5025/rt5025-battery.h
 *  Include header file for Richtek RT5025 battery Driver
 *
 *  Copyright (C) 2013 Richtek Technology Corp.
 *  cy_huang <cy_huang@richtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#ifndef __LINUX_RT5025_BATTERY_H
#define __LINUX_RT5025_BATTERY_H

#undef ROCKCHIP_BATTERY_6900MAH
#undef ROCKCHIP_BATTERY_4000MAH

#define RT5025_REG_IRQ_CTL	0x50
#define RT5025_REG_IRQ_FLAG	0x51
#define RT5025_REG_VALRT_MAXTH	0x53
#define RT5025_REG_VALRT_MIN1TH	0x54
#define RT5025_REG_VALRT_MIN2TH	0x55
#define RT5025_REG_TALRT_MAXTH	0x56
#define RT5025_REG_TALRT_MINTH	0x57
#define RT5025_REG_VCELL_MSB	0x58
#define RT5025_REG_VCELL_LSB	0x59
#define RT5025_REG_INT_TEMPERATUE_MSB	0x5A
#define RT5025_REG_INT_TEMPERATUE_LSB	0x5B
#define RT5025_REG_EXT_TEMPERATUE_MSB	0x5E
#define RT5025_REG_EXT_TEMPERATUE_LSB	0x5F
#define RT5025_REG_TIMER	0x60
#define RT5025_REG_CHANNEL_MSB	0x62
#define RT5025_REG_CHANNEL_LSB	0x63
#define RT5025_REG_CURRENT_MSB	0x76
#define RT5025_REG_CURRENT_LSB	0x77
#define RT5025_REG_QCHGH_MSB	0x78
#define RT5025_REG_QCHGH_LSB	0x79
#define RT5025_REG_QCHGL_MSB	0x7A
#define RT5025_REG_QCHGL_LSB	0x7B
#define RT5025_REG_QDCHGH_MSB	0x7C
#define RT5025_REG_QDCHGH_LSB	0x7D
#define RT5025_REG_QDCHGL_MSB	0x7E
#define RT5025_REG_QDCHGL_LSB	0x7F

#define IRQ_CTL_BIT_TMX		(1 << 5)
#define IRQ_CTL_BIT_TMN		(1 << 4)
#define IRQ_CTL_BIT_VMX		(1 << 2)
#define IRQ_CTL_BIT_VMN1	(1 << 1)
#define IRQ_CTL_BIT_VMN2	(1 << 0)

#define IRQ_FLG_BIT_TMX  (1 << 5)
#define IRQ_FLG_BIT_TMN  (1 << 4)
#define IRQ_FLG_BIT_VMX  (1 << 2)
#define IRQ_FLG_BIT_VMN1 (1 << 1)
#define IRQ_FLG_BIT_VMN2 (1 << 0)

#define CHANNEL_H_BIT_CLRQDCHG  (1 << 7)
#define CHANNEL_H_BIT_CLRQCHG   (1 << 6)

#define CHANNEL_L_BIT_CADC_EN   (1 << 7)
#define CHANNEL_L_BIT_INTEMPCH  (1 << 6)
#define CHANNEL_L_BIT_AINCH     (1 << 2)
#define CHANNEL_L_BIT_VBATSCH   (1 << 1)
#define CHANNEL_L_BIT_VADC_EN   (1 << 0)

#define NORMAL_POLL  30       /* 30 sec */
#define TP_POLL      5       /* 5 sec */
#define EDV_POLL     5       /* 1 sec */
#define SMOOTH_POLL  20      /* 5 sec */
#define SUSPEND_POLL (30*60) /* 30 min */
#define INIT_POLL    1
#define LOW_BAT_WAKE_LOK_TIME 120

#define HIGH_TEMP_THRES	650
#define HIGH_TEMP_RECOVER 430
#define LOW_TEMP_THRES (-30)
#define LOW_TEMP_RECOVER 0
#define TEMP_ABNORMAL_COUNT	3

#define EDV_HYS      100
#define IRQ_THRES_UNIT 1953

#define TALRTMAX_VALUE  0x38 /*65.39'C*/
#define TALRTMIN_VALUE  0x9 /*-18.75'C*/
#define TRLS_VALUE      55   /*5'C ; unit:mV*/
#define VRLS_VALUE      100  /*100mV*/


#define DEADBAND   10

/*#define SLEEP_CURRENT 3 //mA*/

typedef enum{
	CHG,
	DCHG
} operation_mode;

typedef struct{
	int x;
	int y;
} battery_graph_prop;

typedef enum {
	MAXTEMP,
	MINTEMP,
	MAXVOLT,
	MINVOLT1,
	MINVOLT2,
	TEMP_RLS,
	VOLT_RLS,
	LAST_TYPE,
} alert_type;

#if defined(ROCKCHIP_BATTERY_6900MAH)
#include <linux/power/rockchip-6900ma-bat.h>
#elif defined(ROCKCHIP_BATTERY_4000MAH)
#include <linux/power/rockchip-4000ma-bat.h>
#else
#include <linux/power/rockchip-general-bat.h>
#endif

#define VALRTMIN2_VALUE (rt5025_battery_param2[4].x * 100 / IRQ_THRES_UNIT + 1) /*EDV0 voltage*/

#endif /* #ifndef __LINUX_RT5025_BATTERY_H */
