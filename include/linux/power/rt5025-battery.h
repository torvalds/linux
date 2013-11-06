/*
 *  include/linux/power/rt5025-battery.h
 *  Include header file for Richtek RT5025 battery Driver
 *
 *  Copyright (C) 2013 Richtek Electronics
 *  cy_huang <cy_huang@richtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_RT5025_BATTERY_H
#define __LINUX_RT5025_BATTERY_H

#undef ROCKCHIP_BATTERY_6900MAH
#undef ROCKCHIP_BATTERY_4000MAH

#define RT5025_REG_IRQ_CTL             0x50
#define RT5025_REG_IRQ_FLAG            0x51
#define RT5025_REG_VALRT_MAXTH         0x53
#define RT5025_REG_VALRT_MIN1TH        0x54
#define RT5025_REG_VALRT_MIN2TH        0x55
#define RT5025_REG_TALRT_MAXTH         0x56
#define RT5025_REG_TALRT_MINTH         0x57
#define RT5025_REG_VCELL_MSB           0x58
#define RT5025_REG_VCELL_LSB           0x59
#define RT5025_REG_INT_TEMPERATUE_MSB  0x5A
#define RT5025_REG_INT_TEMPERATUE_LSB  0x5B
#define RT5025_REG_EXT_TEMPERATUE_MSB  0x5E
#define RT5025_REG_EXT_TEMPERATUE_LSB  0x5F
#define RT5025_REG_TIMER               0x60
#define RT5025_REG_CHANNEL_MSB         0x62
#define RT5025_REG_CHANNEL_LSB         0x63
#define RT5025_REG_CURRENT_MSB         0x76
#define RT5025_REG_CURRENT_LSB         0x77
#define RT5025_REG_QCHGH_MSB           0x78
#define RT5025_REG_QCHGH_LSB           0x79
#define RT5025_REG_QCHGL_MSB           0x7A
#define RT5025_REG_QCHGL_LSB           0x7B
#define RT5025_REG_QDCHGH_MSB          0x7C
#define RT5025_REG_QDCHGH_LSB          0x7D
#define RT5025_REG_QDCHGL_MSB          0x7E
#define RT5025_REG_QDCHGL_LSB          0x7F

#define IRQ_CTL_BIT_TMX  (1 << 5)
#define IRQ_CTL_BIT_TMN  (1 << 4)
#define IRQ_CTL_BIT_VMX  (1 << 2)
#define IRQ_CTL_BIT_VMN1 (1 << 1)
#define IRQ_CTL_BIT_VMN2 (1 << 0)

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

#define TALRTMAX_VALUE  0x38 //65.39'C
#define TALRTMIN_VALUE  0x9 //-18.75'C
#define TRLS_VALUE      55   //5'C ; unit:mV
#define VRLS_VALUE      100  //100mV


#define DEADBAND   10

//#define SLEEP_CURRENT 3 //mA

u16 crctab16[256] =
{
                0x0000, 0x1189, 0x2312, 0x329b, 0x4624, 0x57ad, 0x6536, 0x74bf, 
                0x8c48, 0x9dc1, 0xaf5a, 0xbed3, 0xca6c, 0xdbe5, 0xe97e, 0xf8f7, 
                0x1081, 0x0108, 0x3393, 0x221a, 0x56a5, 0x472c, 0x75b7, 0x643e, 
                0x9cc9, 0x8d40, 0xbfdb, 0xae52, 0xdaed, 0xcb64, 0xf9ff, 0xe876, 
                0x2102, 0x308b, 0x0210, 0x1399, 0x6726, 0x76af, 0x4434, 0x55bd, 
                0xad4a, 0xbcc3, 0x8e58, 0x9fd1, 0xeb6e, 0xfae7, 0xc87c, 0xd9f5, 
                0x3183, 0x200a, 0x1291, 0x0318, 0x77a7, 0x662e, 0x54b5, 0x453c, 
                0xbdcb, 0xac42, 0x9ed9, 0x8f50, 0xfbef, 0xea66, 0xd8fd, 0xc974, 
                0x4204, 0x538d, 0x6116, 0x709f, 0x0420, 0x15a9, 0x2732, 0x36bb, 
                0xce4c, 0xdfc5, 0xed5e, 0xfcd7, 0x8868, 0x99e1, 0xab7a, 0xbaf3, 
                0x5285, 0x430c, 0x7197, 0x601e, 0x14a1, 0x0528, 0x37b3, 0x263a, 
                0xdecd, 0xcf44, 0xfddf, 0xec56, 0x98e9, 0x8960, 0xbbfb, 0xaa72, 
                0x6306, 0x728f, 0x4014, 0x519d, 0x2522, 0x34ab, 0x0630, 0x17b9, 
                0xef4e, 0xfec7, 0xcc5c, 0xddd5, 0xa96a, 0xb8e3, 0x8a78, 0x9bf1, 
                0x7387, 0x620e, 0x5095, 0x411c, 0x35a3, 0x242a, 0x16b1, 0x0738, 
                0xffcf, 0xee46, 0xdcdd, 0xcd54, 0xb9eb, 0xa862, 0x9af9, 0x8b70, 
                0x8408, 0x9581, 0xa71a, 0xb693, 0xc22c, 0xd3a5, 0xe13e, 0xf0b7, 
                0x0840, 0x19c9, 0x2b52, 0x3adb, 0x4e64, 0x5fed, 0x6d76, 0x7cff, 
                0x9489, 0x8500, 0xb79b, 0xa612, 0xd2ad, 0xc324, 0xf1bf, 0xe036, 
                0x18c1, 0x0948, 0x3bd3, 0x2a5a, 0x5ee5, 0x4f6c, 0x7df7, 0x6c7e, 
                0xa50a, 0xb483, 0x8618, 0x9791, 0xe32e, 0xf2a7, 0xc03c, 0xd1b5, 
                0x2942, 0x38cb, 0x0a50, 0x1bd9, 0x6f66, 0x7eef, 0x4c74, 0x5dfd, 
                0xb58b, 0xa402, 0x9699, 0x8710, 0xf3af, 0xe226, 0xd0bd, 0xc134, 
                0x39c3, 0x284a, 0x1ad1, 0x0b58, 0x7fe7, 0x6e6e, 0x5cf5, 0x4d7c, 
                0xc60c, 0xd785, 0xe51e, 0xf497, 0x8028, 0x91a1, 0xa33a, 0xb2b3, 
                0x4a44, 0x5bcd, 0x6956, 0x78df, 0x0c60, 0x1de9, 0x2f72, 0x3efb, 
                0xd68d, 0xc704, 0xf59f, 0xe416, 0x90a9, 0x8120, 0xb3bb, 0xa232, 
                0x5ac5, 0x4b4c, 0x79d7, 0x685e, 0x1ce1, 0x0d68, 0x3ff3, 0x2e7a, 
                0xe70e, 0xf687, 0xc41c, 0xd595, 0xa12a, 0xb0a3, 0x8238, 0x93b1, 
                0x6b46, 0x7acf, 0x4854, 0x59dd, 0x2d62, 0x3ceb, 0x0e70, 0x1ff9, 
                0xf78f, 0xe606, 0xd49d, 0xc514, 0xb1ab, 0xa022, 0x92b9, 0x8330, 
                0x7bc7, 0x6a4e, 0x58d5, 0x495c, 0x3de3, 0x2c6a, 0x1ef1, 0x0f78,
};

typedef enum{
	CHG,
	DCHG
}operation_mode;

typedef struct{
	int x;
	int y;
}battery_graph_prop;

typedef enum {
	MAXTEMP,
	MINTEMP,
	MAXVOLT,
	MINVOLT1,
	MINVOLT2,
	TEMP_RLS,
	VOLT_RLS,
	LAST_TYPE,
}alert_type;

#if defined(ROCKCHIP_BATTERY_6900MAH)
#include <linux/power/rockchip-6900ma-bat.h>
#elif defined(ROCKCHIP_BATTERY_4000MAH)
#include <linux/power/rockchip-4000ma-bat.h>
#else
#include <linux/power/rockchip-general-bat.h>
#endif

#define VALRTMIN2_VALUE (rt5025_battery_param2[4].x * 100 / IRQ_THRES_UNIT + 1) //EDV0 voltage

#endif /* #ifndef __LINUX_RT5025_BATTERY_H */
