/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * TI Touch Screen / ADC MFD driver
 *
 * Copyright (C) 2012 Texas Instruments Incorporated - https://www.ti.com/
 */

#ifndef __LINUX_TI_AM335X_TSCADC_MFD_H
#define __LINUX_TI_AM335X_TSCADC_MFD_H

#include <linux/bitfield.h>
#include <linux/mfd/core.h>
#include <linux/units.h>

#define REG_RAWIRQSTATUS	0x024
#define REG_IRQSTATUS		0x028
#define REG_IRQENABLE		0x02C
#define REG_IRQCLR		0x030
#define REG_IRQWAKEUP		0x034
#define REG_DMAENABLE_SET	0x038
#define REG_DMAENABLE_CLEAR	0x03c
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
#define REG_DMA1REQ		0xF8
#define REG_FIFO0		0x100
#define REG_FIFO1		0x200

/*	Register Bitfields	*/
/* IRQ wakeup enable */
#define IRQWKUP_ENB		BIT(0)

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
#define STEPCONFIG_MODE(val)	FIELD_PREP(GENMASK(1, 0), (val))
#define STEPCONFIG_MODE_SWCNT	STEPCONFIG_MODE(1)
#define STEPCONFIG_MODE_HWSYNC	STEPCONFIG_MODE(2)
#define STEPCONFIG_AVG(val)	FIELD_PREP(GENMASK(4, 2), (val))
#define STEPCONFIG_AVG_16	STEPCONFIG_AVG(4)
#define STEPCONFIG_XPP		BIT(5)
#define STEPCONFIG_XNN		BIT(6)
#define STEPCONFIG_YPP		BIT(7)
#define STEPCONFIG_YNN		BIT(8)
#define STEPCONFIG_XNP		BIT(9)
#define STEPCONFIG_YPN		BIT(10)
#define STEPCONFIG_RFP(val)	FIELD_PREP(GENMASK(13, 12), (val))
#define STEPCONFIG_RFP_VREFP	STEPCONFIG_RFP(3)
#define STEPCONFIG_INM(val)	FIELD_PREP(GENMASK(18, 15), (val))
#define STEPCONFIG_INM_ADCREFM	STEPCONFIG_INM(8)
#define STEPCONFIG_INP(val)	FIELD_PREP(GENMASK(22, 19), (val))
#define STEPCONFIG_INP_AN4	STEPCONFIG_INP(4)
#define STEPCONFIG_INP_ADCREFM	STEPCONFIG_INP(8)
#define STEPCONFIG_FIFO1	BIT(26)
#define STEPCONFIG_RFM(val)	FIELD_PREP(GENMASK(24, 23), (val))
#define STEPCONFIG_RFM_VREFN	STEPCONFIG_RFM(3)

/* Delay register */
#define STEPDELAY_OPEN(val)	FIELD_PREP(GENMASK(17, 0), (val))
#define STEPCONFIG_OPENDLY	STEPDELAY_OPEN(0x098)
#define STEPCONFIG_MAX_OPENDLY	GENMASK(17, 0)
#define STEPDELAY_SAMPLE(val)	FIELD_PREP(GENMASK(31, 24), (val))
#define STEPCONFIG_SAMPLEDLY	STEPDELAY_SAMPLE(0)
#define STEPCONFIG_MAX_SAMPLE	GENMASK(7, 0)

/* Charge Config */
#define STEPCHARGE_RFP(val)	FIELD_PREP(GENMASK(14, 12), (val))
#define STEPCHARGE_RFP_XPUL	STEPCHARGE_RFP(1)
#define STEPCHARGE_INM(val)	FIELD_PREP(GENMASK(18, 15), (val))
#define STEPCHARGE_INM_AN1	STEPCHARGE_INM(1)
#define STEPCHARGE_INP(val)	FIELD_PREP(GENMASK(22, 19), (val))
#define STEPCHARGE_RFM(val)	FIELD_PREP(GENMASK(24, 23), (val))
#define STEPCHARGE_RFM_XNUR	STEPCHARGE_RFM(1)

/* Charge delay */
#define CHARGEDLY_OPEN(val)	FIELD_PREP(GENMASK(17, 0), (val))
#define CHARGEDLY_OPENDLY	CHARGEDLY_OPEN(0x400)

/* Control register */
#define CNTRLREG_SSENB		BIT(0)
#define CNTRLREG_STEPID		BIT(1)
#define CNTRLREG_TSC_STEPCONFIGWRT BIT(2)
#define CNTRLREG_POWERDOWN	BIT(4)
#define CNTRLREG_TSC_AFE_CTRL(val) FIELD_PREP(GENMASK(6, 5), (val))
#define CNTRLREG_TSC_4WIRE	CNTRLREG_TSC_AFE_CTRL(1)
#define CNTRLREG_TSC_5WIRE	CNTRLREG_TSC_AFE_CTRL(2)
#define CNTRLREG_TSC_8WIRE	CNTRLREG_TSC_AFE_CTRL(3)
#define CNTRLREG_TSC_ENB	BIT(7)

/*Control registers bitfields  for MAGADC IP */
#define CNTRLREG_MAGADCENB      BIT(0)
#define CNTRLREG_MAG_PREAMP_PWRDOWN BIT(5)
#define CNTRLREG_MAG_PREAMP_BYPASS  BIT(6)

/* FIFO READ Register */
#define FIFOREAD_DATA_MASK	GENMASK(11, 0)
#define FIFOREAD_CHNLID_MASK	GENMASK(19, 16)

/* DMA ENABLE/CLEAR Register */
#define DMA_FIFO0		BIT(0)
#define DMA_FIFO1		BIT(1)

/* Sequencer Status */
#define SEQ_STATUS		BIT(5)
#define CHARGE_STEP		0x11

#define TSC_ADC_CLK		(3 * HZ_PER_MHZ)
#define MAG_ADC_CLK		(13 * HZ_PER_MHZ)
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
#define IDLE_TIMEOUT_MS		83 /* milliseconds */

#define TSCADC_CELLS		2

struct ti_tscadc_data {
	char *adc_feature_name;
	char *adc_feature_compatible;
	char *secondary_feature_name;
	char *secondary_feature_compatible;
	unsigned int target_clk_rate;
};

struct ti_tscadc_dev {
	struct device *dev;
	struct regmap *regmap;
	void __iomem *tscadc_base;
	phys_addr_t tscadc_phys_base;
	const struct ti_tscadc_data *data;
	int irq;
	struct mfd_cell cells[TSCADC_CELLS];
	u32 ctrl;
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

static inline bool ti_adc_with_touchscreen(struct ti_tscadc_dev *tscadc)
{
	return of_device_is_compatible(tscadc->dev->of_node,
				       "ti,am3359-tscadc");
}

void am335x_tsc_se_set_cache(struct ti_tscadc_dev *tsadc, u32 val);
void am335x_tsc_se_set_once(struct ti_tscadc_dev *tsadc, u32 val);
void am335x_tsc_se_clr(struct ti_tscadc_dev *tsadc, u32 val);
void am335x_tsc_se_adc_done(struct ti_tscadc_dev *tsadc);

#endif
