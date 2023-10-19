/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Header file for Intel Merrifield Basin Cove PMIC
 *
 * Copyright (C) 2019 Intel Corporation. All rights reserved.
 */

#ifndef __INTEL_SOC_PMIC_MRFLD_H__
#define __INTEL_SOC_PMIC_MRFLD_H__

#include <linux/bits.h>

#define BCOVE_ID		0x00

#define BCOVE_ID_MINREV0	GENMASK(2, 0)
#define BCOVE_ID_MAJREV0	GENMASK(5, 3)
#define BCOVE_ID_VENDID0	GENMASK(7, 6)

#define BCOVE_MINOR(x)		(unsigned int)(((x) & BCOVE_ID_MINREV0) >> 0)
#define BCOVE_MAJOR(x)		(unsigned int)(((x) & BCOVE_ID_MAJREV0) >> 3)
#define BCOVE_VENDOR(x)		(unsigned int)(((x) & BCOVE_ID_VENDID0) >> 6)

#define BCOVE_IRQLVL1		0x01

#define BCOVE_PBIRQ		0x02
#define BCOVE_TMUIRQ		0x03
#define BCOVE_THRMIRQ		0x04
#define BCOVE_BCUIRQ		0x05
#define BCOVE_ADCIRQ		0x06
#define BCOVE_CHGRIRQ0		0x07
#define BCOVE_CHGRIRQ1		0x08
#define BCOVE_GPIOIRQ		0x09
#define BCOVE_CRITIRQ		0x0B

#define BCOVE_MIRQLVL1		0x0C

#define BCOVE_MPBIRQ		0x0D
#define BCOVE_MTMUIRQ		0x0E
#define BCOVE_MTHRMIRQ		0x0F
#define BCOVE_MBCUIRQ		0x10
#define BCOVE_MADCIRQ		0x11
#define BCOVE_MCHGRIRQ0		0x12
#define BCOVE_MCHGRIRQ1		0x13
#define BCOVE_MGPIOIRQ		0x14
#define BCOVE_MCRITIRQ		0x16

#define BCOVE_SCHGRIRQ0		0x4E
#define BCOVE_SCHGRIRQ1		0x4F

/* Level 1 IRQs */
#define BCOVE_LVL1_PWRBTN	BIT(0)	/* power button */
#define BCOVE_LVL1_TMU		BIT(1)	/* time management unit */
#define BCOVE_LVL1_THRM		BIT(2)	/* thermal */
#define BCOVE_LVL1_BCU		BIT(3)	/* burst control unit */
#define BCOVE_LVL1_ADC		BIT(4)	/* ADC */
#define BCOVE_LVL1_CHGR		BIT(5)	/* charger */
#define BCOVE_LVL1_GPIO		BIT(6)	/* GPIO */
#define BCOVE_LVL1_CRIT		BIT(7)	/* critical event */

/* Level 2 IRQs: power button */
#define BCOVE_PBIRQ_PBTN	BIT(0)
#define BCOVE_PBIRQ_UBTN	BIT(1)

/* Level 2 IRQs: ADC */
#define BCOVE_ADCIRQ_BATTEMP	BIT(2)
#define BCOVE_ADCIRQ_SYSTEMP	BIT(3)
#define BCOVE_ADCIRQ_BATTID	BIT(4)
#define BCOVE_ADCIRQ_VIBATT	BIT(5)
#define BCOVE_ADCIRQ_CCTICK	BIT(7)

/* Level 2 IRQs: charger */
#define BCOVE_CHGRIRQ_BAT0ALRT	BIT(4)
#define BCOVE_CHGRIRQ_BAT1ALRT	BIT(5)
#define BCOVE_CHGRIRQ_BATCRIT	BIT(6)

#define BCOVE_CHGRIRQ_VBUSDET	BIT(0)
#define BCOVE_CHGRIRQ_DCDET	BIT(1)
#define BCOVE_CHGRIRQ_BATTDET	BIT(2)
#define BCOVE_CHGRIRQ_USBIDDET	BIT(3)

#endif	/* __INTEL_SOC_PMIC_MRFLD_H__ */
