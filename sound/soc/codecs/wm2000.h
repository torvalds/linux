/*
 * wm2000.h  --  WM2000 Soc Audio driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _WM2000_H
#define _WM2000_H

#define WM2000_REG_SYS_START	    0x8000
#define WM2000_REG_ANC_GAIN_CTRL    0x8fa2
#define WM2000_REG_MSE_TH2          0x8fdf
#define WM2000_REG_MSE_TH1          0x8fe0
#define WM2000_REG_SPEECH_CLARITY   0x8fef
#define WM2000_REG_SYS_WATCHDOG     0x8ff6
#define WM2000_REG_ANA_VMID_PD_TIME 0x8ff7
#define WM2000_REG_ANA_VMID_PU_TIME 0x8ff8
#define WM2000_REG_CAT_FLTR_INDX    0x8ff9
#define WM2000_REG_CAT_GAIN_0       0x8ffa
#define WM2000_REG_SYS_STATUS       0x8ffc
#define WM2000_REG_SYS_MODE_CNTRL   0x8ffd
#define WM2000_REG_SYS_START0       0x8ffe
#define WM2000_REG_SYS_START1       0x8fff
#define WM2000_REG_ID1              0xf000
#define WM2000_REG_ID2              0xf001
#define WM2000_REG_REVISON          0xf002
#define WM2000_REG_SYS_CTL1         0xf003
#define WM2000_REG_SYS_CTL2         0xf004
#define WM2000_REG_ANC_STAT         0xf005
#define WM2000_REG_IF_CTL           0xf006
#define WM2000_REG_ANA_MIC_CTL      0xf028
#define WM2000_REG_SPK_CTL          0xf034

/* SPEECH_CLARITY */
#define WM2000_SPEECH_CLARITY   0x01

/* SYS_STATUS */
#define WM2000_STATUS_MOUSE_ACTIVE              0x40
#define WM2000_STATUS_CAT_FREQ_COMPLETE         0x20
#define WM2000_STATUS_CAT_GAIN_COMPLETE         0x10
#define WM2000_STATUS_THERMAL_SHUTDOWN_COMPLETE 0x08
#define WM2000_STATUS_ANC_DISABLED              0x04
#define WM2000_STATUS_POWER_DOWN_COMPLETE       0x02
#define WM2000_STATUS_BOOT_COMPLETE             0x01

/* SYS_MODE_CNTRL */
#define WM2000_MODE_ANA_SEQ_INCLUDE 0x80
#define WM2000_MODE_MOUSE_ENABLE    0x40
#define WM2000_MODE_CAT_FREQ_ENABLE 0x20
#define WM2000_MODE_CAT_GAIN_ENABLE 0x10
#define WM2000_MODE_BYPASS_ENTRY    0x08
#define WM2000_MODE_STANDBY_ENTRY   0x04
#define WM2000_MODE_THERMAL_ENABLE  0x02
#define WM2000_MODE_POWER_DOWN      0x01

/* SYS_CTL1 */
#define WM2000_SYS_STBY          0x01

/* SYS_CTL2 */
#define WM2000_MCLK_DIV2_ENA_CLR 0x80
#define WM2000_MCLK_DIV2_ENA_SET 0x40
#define WM2000_ANC_ENG_CLR       0x20
#define WM2000_ANC_ENG_SET       0x10
#define WM2000_ANC_INT_N_CLR     0x08
#define WM2000_ANC_INT_N_SET     0x04
#define WM2000_RAM_CLR           0x02
#define WM2000_RAM_SET           0x01

/* ANC_STAT */
#define WM2000_ANC_ENG_IDLE      0x01

#endif
