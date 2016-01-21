/*
 * intel_bxtwc.h - Header file for Intel Broxton Whiskey Cove PMIC
 *
 * Copyright (C) 2015 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/mfd/intel_soc_pmic.h>

#ifndef __INTEL_BXTWC_H__
#define __INTEL_BXTWC_H__

/* BXT WC devices */
#define BXTWC_DEVICE1_ADDR		0x4E
#define BXTWC_DEVICE2_ADDR		0x4F
#define BXTWC_DEVICE3_ADDR		0x5E

/* device1 Registers */
#define BXTWC_CHIPID			0x4E00
#define BXTWC_CHIPVER			0x4E01

#define BXTWC_SCHGRIRQ0_ADDR		0x5E1A
#define BXTWC_CHGRCTRL0_ADDR		0x5E16
#define BXTWC_CHGRCTRL1_ADDR		0x5E17
#define BXTWC_CHGRCTRL2_ADDR		0x5E18
#define BXTWC_CHGRSTATUS_ADDR		0x5E19
#define BXTWC_THRMBATZONE_ADDR		0x4F22

#define BXTWC_USBPATH_ADDR		0x5E19
#define BXTWC_USBPHYCTRL_ADDR		0x5E07
#define BXTWC_USBIDCTRL_ADDR		0x5E05
#define BXTWC_USBIDEN_MASK		0x01
#define BXTWC_USBIDSTAT_ADDR		0x00FF
#define BXTWC_USBSRCDETSTATUS_ADDR	0x5E29

#define BXTWC_DBGUSBBC1_ADDR		0x5FE0
#define BXTWC_DBGUSBBC2_ADDR		0x5FE1
#define BXTWC_DBGUSBBCSTAT_ADDR		0x5FE2

#define BXTWC_WAKESRC_ADDR		0x4E22
#define BXTWC_WAKESRC2_ADDR		0x4EE5
#define BXTWC_CHRTTADDR_ADDR		0x5E22
#define BXTWC_CHRTTDATA_ADDR		0x5E23

#define BXTWC_STHRMIRQ0_ADDR		0x4F19
#define WC_MTHRMIRQ1_ADDR		0x4E12
#define WC_STHRMIRQ1_ADDR		0x4F1A
#define WC_STHRMIRQ2_ADDR		0x4F1B

#define BXTWC_THRMZN0H_ADDR		0x4F44
#define BXTWC_THRMZN0L_ADDR		0x4F45
#define BXTWC_THRMZN1H_ADDR		0x4F46
#define BXTWC_THRMZN1L_ADDR		0x4F47
#define BXTWC_THRMZN2H_ADDR		0x4F48
#define BXTWC_THRMZN2L_ADDR		0x4F49
#define BXTWC_THRMZN3H_ADDR		0x4F4A
#define BXTWC_THRMZN3L_ADDR		0x4F4B
#define BXTWC_THRMZN4H_ADDR		0x4F4C
#define BXTWC_THRMZN4L_ADDR		0x4F4D

#endif
