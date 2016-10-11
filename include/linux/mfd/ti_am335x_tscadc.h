#ifndef __LINUX_TI_AM335X_TSCADC_MFD_H
#define __LINUX_TI_AM335X_TSCADC_MFD_H

/*
 * TI Touch Screen / ADC MFD driver
 *
 * Copyright (C) 2012 Texas Instruments Incorporated - http://www.ti.com/
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/mfd/core.h>

#define REG_RAWIRQSTATUS	0x024
#define REG_IRQSTATUS		0x028
#define REG_IRQENABLE		0x02C
#define REG_IRQCLR		0x030
#define REG_IRQWAKEUP		0x034
#define REG_CTRL		0x040
#define REG_ADCFSM		0x044
#define REG_CLKDIV		0x04C
#define REG_SE			0x054
#define REG_IDLECONFIG		0x058
#define REG_CHARGECONFIG	0x05C
#define REG_CHARGEDELAY		0x060
#define REG_STEPCONFIG(n)	(0x64 + ((n) * 8))
#define REG_STEPDELAY(n)	(0x68 + ((n) * 8))
#define REG_FIFO0CNT		0xE4
#define REG_FIFO0THR		0xE8
#define REG_FIFO1CNT		0xF0
#define REG_FIFO1THR		0xF4
#define REG_FIFO0		0x100
#define REG_FIFO1		0x200

/*	Register Bitfields	*/
/* IRQ wakeup enable */
#define IRQWKUP_ENB		BIT(0)

/* Step Enable */
#define STEPENB_MASK		(0x1FFFF << 0)
#define STEPENB(val)		((val) << 0)
#define ENB(val)			(1 << (val))
#define STPENB_STEPENB		STEPENB(0x1FFFF)
#define STPENB_STEPENB_TC	STEPENB(0x1FFF)

/* IRQ enable */
#define IRQENB_HW_PEN		BIT(0)
#define IRQENB_EOS		BIT(1)
#define IRQENB_FIFO0THRES	BIT(2)
#define IRQENB_FIFO0OVRRUN	BIT(3)
#define IRQENB_FIFO0UNDRFLW	BIT(4)
#define IRQENB_FIFO1THRES	BIT(5)
#define IRQENB_FIFO1OVRRUN	BIT(6)
#define IRQENB_FIFO1UNDRFLW	BIT(7)
#define IRQENB_PENUP		BIT(9)

/* Step Configuration */
#define STEPCONFIG_MODE_MASK	(3 << 0)
#define STEPCONFIG_MODE(val)	((val) << 0)
#define STEPCONFIG_MODE_SWCNT	STEPCONFIG_MODE(1)
#define STEPCONFIG_MODE_HWSYNC	STEPCONFIG_MODE(2)
#define STEPCONFIG_AVG_MASK	(7 << 2)
#define STEPCONFIG_AVG(val)	((val) << 2)
#define STEPCONFIG_AVG_16	STEPCONFIG_AVG(4)
#define STEPCONFIG_XPP		BIT(5)
#define STEPCONFIG_XNN		BIT(6)
#define STEPCONFIG_YPP		BIT(7)
#define STEPCONFIG_YNN		BIT(8)
#define STEPCONFIG_XNP		BIT(9)
#define STEPCONFIG_YPN		BIT(10)
#define STEPCONFIG_INM_MASK	(0xF << 15)
#define STEPCONFIG_INM(val)	((val) << 15)
#define STEPCONFIG_INM_ADCREFM	STEPCONFIG_INM(8)
#define STEPCONFIG_INP_MASK	(0xF << 19)
#define STEPCONFIG_INP(val)	((val) << 19)
#define STEPCONFIG_INP_AN4	STEPCONFIG_INP(4)
#define STEPCONFIG_INP_ADCREFM	STEPCONFIG_INP(8)
#define STEPCONFIG_FIFO1	BIT(26)

/* Delay register */
#define STEPDELAY_OPEN_MASK	(0x3FFFF << 0)
#define STEPDELAY_OPEN(val)	((val) << 0)
#define STEPCONFIG_OPENDLY	STEPDELAY_OPEN(0x098)
#define STEPDELAY_SAMPLE_MASK	(0xFF << 24)
#define STEPDELAY_SAMPLE(val)	((val) << 24)
#define STEPCONFIG_SAMPLEDLY	STEPDELAY_SAMPLE(0)

/* Charge Config */
#define STEPCHARGE_RFP_MASK	(7 << 12)
#define STEPCHARGE_RFP(val)	((val) << 12)
#define STEPCHARGE_RFP_XPUL	STEPCHARGE_RFP(1)
#define STEPCHARGE_INM_MASK	(0xF << 15)
#define STEPCHARGE_INM(val)	((val) << 15)
#define STEPCHARGE_INM_AN1	STEPCHARGE_INM(1)
#define STEPCHARGE_INP_MASK	(0xF << 19)
#define STEPCHARGE_INP(val)	((val) << 19)
#define STEPCHARGE_RFM_MASK	(3 << 23)
#define STEPCHARGE_RFM(val)	((val) << 23)
#define STEPCHARGE_RFM_XNUR	STEPCHARGE_RFM(1)

/* Charge delay */
#define CHARGEDLY_OPEN_MASK	(0x3FFFF << 0)
#define CHARGEDLY_OPEN(val)	((val) << 0)
#define CHARGEDLY_OPENDLY	CHARGEDLY_OPEN(0x400)

/* Control register */
#define CNTRLREG_TSCSSENB	BIT(0)
#define CNTRLREG_STEPID		BIT(1)
#define CNTRLREG_STEPCONFIGWRT	BIT(2)
#define CNTRLREG_POWERDOWN	BIT(4)
#define CNTRLREG_AFE_CTRL_MASK	(3 << 5)
#define CNTRLREG_AFE_CTRL(val)	((val) << 5)
#define CNTRLREG_4WIRE		CNTRLREG_AFE_CTRL(1)
#define CNTRLREG_5WIRE		CNTRLREG_AFE_CTRL(2)
#define CNTRLREG_8WIRE		CNTRLREG_AFE_CTRL(3)
#define CNTRLREG_TSCENB		BIT(7)

/* FIFO READ Register */
#define FIFOREAD_DATA_MASK (0xfff << 0)
#define FIFOREAD_CHNLID_MASK (0xf << 16)

/* Sequencer Status */
#define SEQ_STATUS BIT(5)
#define CHARGE_STEP		0x11

#define ADC_CLK			3000000
#define TOTAL_STEPS		16
#define TOTAL_CHANNELS		8
#define FIFO1_THRESHOLD		19

/*
 * time in us for processing a single channel, calculated as follows:
 *
 * max num cycles = open delay + (sample delay + conv time) * averaging
 *
 * max num cycles: 262143 + (255 + 13) * 16 = 266431
 *
 * clock frequency: 26MHz / 8 = 3.25MHz
 * clock period: 1 / 3.25MHz = 308ns
 *
 * max processing time: 266431 * 308ns = 83ms(approx)
 */
#define IDLE_TIMEOUT 83 /* milliseconds */

#define TSCADC_CELLS		2

struct ti_tscadc_dev {
	struct device *dev;
	struct regmap *regmap_tscadc;
	void __iomem *tscadc_base;
	int irq;
	int used_cells;	/* 1-2 */
	int tsc_wires;
	int tsc_cell;	/* -1 if not used */
	int adc_cell;	/* -1 if not used */
	struct mfd_cell cells[TSCADC_CELLS];
	u32 reg_se_cache;
	bool adc_waiting;
	bool adc_in_use;
	wait_queue_head_t reg_se_wait;
	spinlock_t reg_lock;
	unsigned int clk_div;

	/* tsc device */
	struct titsc *tsc;

	/* adc device */
	struct adc_device *adc;
};

static inline struct ti_tscadc_dev *ti_tscadc_dev_get(struct platform_device *p)
{
	struct ti_tscadc_dev **tscadc_dev = p->dev.platform_data;

	return *tscadc_dev;
}

void am335x_tsc_se_set_cache(struct ti_tscadc_dev *tsadc, u32 val);
void am335x_tsc_se_set_once(struct ti_tscadc_dev *tsadc, u32 val);
void am335x_tsc_se_clr(struct ti_tscadc_dev *tsadc, u32 val);
void am335x_tsc_se_adc_done(struct ti_tscadc_dev *tsadc);

#endif
