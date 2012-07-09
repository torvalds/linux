/*
 * Marvell 88PM80x Interface
 *
 * Copyright (C) 2012 Marvell International Ltd.
 * Qiao Zhou <zhouqiao@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_MFD_88PM80X_H
#define __LINUX_MFD_88PM80X_H

#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/regmap.h>
#include <linux/atomic.h>

#define PM80X_VERSION_MASK		(0xFF)	/* 80X chip ID mask */
enum {
	CHIP_INVALID = 0,
	CHIP_PM800,
	CHIP_PM805,
	CHIP_MAX,
};

enum {
	PM800_ID_BUCK1 = 0,
	PM800_ID_BUCK2,
	PM800_ID_BUCK3,
	PM800_ID_BUCK4,
	PM800_ID_BUCK5,

	PM800_ID_LDO1,
	PM800_ID_LDO2,
	PM800_ID_LDO3,
	PM800_ID_LDO4,
	PM800_ID_LDO5,
	PM800_ID_LDO6,
	PM800_ID_LDO7,
	PM800_ID_LDO8,
	PM800_ID_LDO9,
	PM800_ID_LDO10,
	PM800_ID_LDO11,
	PM800_ID_LDO12,
	PM800_ID_LDO13,
	PM800_ID_LDO14,
	PM800_ID_LDO15,
	PM800_ID_LDO16,
	PM800_ID_LDO17,
	PM800_ID_LDO18,
	PM800_ID_LDO19,

	PM800_ID_RG_MAX,
};
#define PM800_MAX_REGULATOR	PM800_ID_RG_MAX	/* 5 Bucks, 19 LDOs */
#define PM800_NUM_BUCK (5)	/*5 Bucks */
#define PM800_NUM_LDO (19)	/*19 Bucks */

/* page 0 basic: slave adder 0x60 */

#define PM800_STATUS_1			(0x01)
#define PM800_ONKEY_STS1		(1 << 0)
#define PM800_EXTON_STS1		(1 << 1)
#define PM800_CHG_STS1			(1 << 2)
#define PM800_BAT_STS1			(1 << 3)
#define PM800_VBUS_STS1			(1 << 4)
#define PM800_LDO_PGOOD_STS1	(1 << 5)
#define PM800_BUCK_PGOOD_STS1	(1 << 6)

#define PM800_STATUS_2			(0x02)
#define PM800_RTC_ALARM_STS2	(1 << 0)

/* Wakeup Registers */
#define PM800_WAKEUP1		(0x0D)

#define PM800_WAKEUP2		(0x0E)
#define PM800_WAKEUP2_INV_INT		(1 << 0)
#define PM800_WAKEUP2_INT_CLEAR		(1 << 1)
#define PM800_WAKEUP2_INT_MASK		(1 << 2)

#define PM800_POWER_UP_LOG	(0x10)

/* Referance and low power registers */
#define PM800_LOW_POWER1		(0x20)
#define PM800_LOW_POWER2		(0x21)
#define PM800_LOW_POWER_CONFIG3	(0x22)
#define PM800_LOW_POWER_CONFIG4	(0x23)

/* GPIO register */
#define PM800_GPIO_0_1_CNTRL		(0x30)
#define PM800_GPIO0_VAL				(1 << 0)
#define PM800_GPIO0_GPIO_MODE(x)	(x << 1)
#define PM800_GPIO1_VAL				(1 << 4)
#define PM800_GPIO1_GPIO_MODE(x)	(x << 5)

#define PM800_GPIO_2_3_CNTRL		(0x31)
#define PM800_GPIO2_VAL				(1 << 0)
#define PM800_GPIO2_GPIO_MODE(x)	(x << 1)
#define PM800_GPIO3_VAL				(1 << 4)
#define PM800_GPIO3_GPIO_MODE(x)	(x << 5)
#define PM800_GPIO3_MODE_MASK		0x1F
#define PM800_GPIO3_HEADSET_MODE	PM800_GPIO3_GPIO_MODE(6)

#define PM800_GPIO_4_CNTRL			(0x32)
#define PM800_GPIO4_VAL				(1 << 0)
#define PM800_GPIO4_GPIO_MODE(x)	(x << 1)

#define PM800_HEADSET_CNTRL		(0x38)
#define PM800_HEADSET_DET_EN		(1 << 7)
#define PM800_HSDET_SLP			(1 << 1)
/* PWM register */
#define PM800_PWM1		(0x40)
#define PM800_PWM2		(0x41)
#define PM800_PWM3		(0x42)
#define PM800_PWM4		(0x43)

/* RTC Registers */
#define PM800_RTC_CONTROL		(0xD0)
#define PM800_RTC_MISC1			(0xE1)
#define PM800_RTC_MISC2			(0xE2)
#define PM800_RTC_MISC3			(0xE3)
#define PM800_RTC_MISC4			(0xE4)
#define PM800_RTC_MISC5			(0xE7)
/* bit definitions of RTC Register 1 (0xD0) */
#define PM800_ALARM1_EN			(1 << 0)
#define PM800_ALARM_WAKEUP		(1 << 4)
#define PM800_ALARM			(1 << 5)
#define PM800_RTC1_USE_XO		(1 << 7)

/* Regulator Control Registers: BUCK1,BUCK5,LDO1 have DVC */

/* buck registers */
#define PM800_SLEEP_BUCK1	(0x30)

/* BUCK Sleep Mode Register 1: BUCK[1..4] */
#define PM800_BUCK_SLP1		(0x5A)
#define PM800_BUCK1_SLP1_SHIFT	0
#define PM800_BUCK1_SLP1_MASK	(0x3 << PM800_BUCK1_SLP1_SHIFT)

/* page 2 GPADC: slave adder 0x02 */
#define PM800_GPADC_MEAS_EN1		(0x01)
#define PM800_MEAS_EN1_VBAT         (1 << 2)
#define PM800_GPADC_MEAS_EN2		(0x02)
#define PM800_MEAS_EN2_RFTMP        (1 << 0)
#define PM800_MEAS_GP0_EN			(1 << 2)
#define PM800_MEAS_GP1_EN			(1 << 3)
#define PM800_MEAS_GP2_EN			(1 << 4)
#define PM800_MEAS_GP3_EN			(1 << 5)
#define PM800_MEAS_GP4_EN			(1 << 6)

#define PM800_GPADC_MISC_CONFIG1	(0x05)
#define PM800_GPADC_MISC_CONFIG2	(0x06)
#define PM800_GPADC_MISC_GPFSM_EN	(1 << 0)
#define PM800_GPADC_SLOW_MODE(x)	(x << 3)

#define PM800_GPADC_MISC_CONFIG3		(0x09)
#define PM800_GPADC_MISC_CONFIG4		(0x0A)

#define PM800_GPADC_PREBIAS1			(0x0F)
#define PM800_GPADC0_GP_PREBIAS_TIME(x)	(x << 0)
#define PM800_GPADC_PREBIAS2			(0x10)

#define PM800_GP_BIAS_ENA1				(0x14)
#define PM800_GPADC_GP_BIAS_EN0			(1 << 0)
#define PM800_GPADC_GP_BIAS_EN1			(1 << 1)
#define PM800_GPADC_GP_BIAS_EN2			(1 << 2)
#define PM800_GPADC_GP_BIAS_EN3			(1 << 3)

#define PM800_GP_BIAS_OUT1		(0x15)
#define PM800_BIAS_OUT_GP0		(1 << 0)
#define PM800_BIAS_OUT_GP1		(1 << 1)
#define PM800_BIAS_OUT_GP2		(1 << 2)
#define PM800_BIAS_OUT_GP3		(1 << 3)

#define PM800_GPADC0_LOW_TH		0x20
#define PM800_GPADC1_LOW_TH		0x21
#define PM800_GPADC2_LOW_TH		0x22
#define PM800_GPADC3_LOW_TH		0x23
#define PM800_GPADC4_LOW_TH		0x24

#define PM800_GPADC0_UPP_TH		0x30
#define PM800_GPADC1_UPP_TH		0x31
#define PM800_GPADC2_UPP_TH		0x32
#define PM800_GPADC3_UPP_TH		0x33
#define PM800_GPADC4_UPP_TH		0x34

#define PM800_VBBAT_MEAS1		0x40
#define PM800_VBBAT_MEAS2		0x41
#define PM800_VBAT_MEAS1		0x42
#define PM800_VBAT_MEAS2		0x43
#define PM800_VSYS_MEAS1		0x44
#define PM800_VSYS_MEAS2		0x45
#define PM800_VCHG_MEAS1		0x46
#define PM800_VCHG_MEAS2		0x47
#define PM800_TINT_MEAS1		0x50
#define PM800_TINT_MEAS2		0x51
#define PM800_PMOD_MEAS1		0x52
#define PM800_PMOD_MEAS2		0x53

#define PM800_GPADC0_MEAS1		0x54
#define PM800_GPADC0_MEAS2		0x55
#define PM800_GPADC1_MEAS1		0x56
#define PM800_GPADC1_MEAS2		0x57
#define PM800_GPADC2_MEAS1		0x58
#define PM800_GPADC2_MEAS2		0x59
#define PM800_GPADC3_MEAS1		0x5A
#define PM800_GPADC3_MEAS2		0x5B
#define PM800_GPADC4_MEAS1		0x5C
#define PM800_GPADC4_MEAS2		0x5D

#define PM800_GPADC4_AVG1		0xA8
#define PM800_GPADC4_AVG2		0xA9

/* 88PM805 Registers */
#define PM805_MAIN_POWERUP		(0x01)
#define PM805_INT_STATUS0		(0x02)	/* for ena/dis all interrupts */

#define PM805_STATUS0_INT_CLEAR		(1 << 0)
#define PM805_STATUS0_INV_INT		(1 << 1)
#define PM800_STATUS0_INT_MASK		(1 << 2)

#define PM805_INT_STATUS1		(0x03)

#define PM805_INT1_HP1_SHRT		(1 << 0)
#define PM805_INT1_HP2_SHRT		(1 << 1)
#define PM805_INT1_MIC_CONFLICT		(1 << 2)
#define PM805_INT1_CLIP_FAULT		(1 << 3)
#define PM805_INT1_LDO_OFF			(1 << 4)
#define PM805_INT1_SRC_DPLL_LOCK	(1 << 5)

#define PM805_INT_STATUS2		(0x04)

#define PM805_INT2_MIC_DET			(1 << 0)
#define PM805_INT2_SHRT_BTN_DET		(1 << 1)
#define PM805_INT2_VOLM_BTN_DET		(1 << 2)
#define PM805_INT2_VOLP_BTN_DET		(1 << 3)
#define PM805_INT2_RAW_PLL_FAULT	(1 << 4)
#define PM805_INT2_FINE_PLL_FAULT	(1 << 5)

#define PM805_INT_MASK1			(0x05)
#define PM805_INT_MASK2			(0x06)
#define PM805_SHRT_BTN_DET		(1 << 1)

/* number of status and int reg in a row */
#define PM805_INT_REG_NUM		(2)

#define PM805_MIC_DET1			(0x07)
#define PM805_MIC_DET_EN_MIC_DET (1 << 0)
#define PM805_MIC_DET2			(0x08)
#define PM805_MIC_DET_STATUS1	(0x09)

#define PM805_MIC_DET_STATUS3	(0x0A)
#define PM805_AUTO_SEQ_STATUS1	(0x0B)
#define PM805_AUTO_SEQ_STATUS2	(0x0C)

#define PM805_ADC_SETTING1		(0x10)
#define PM805_ADC_SETTING2		(0x11)
#define PM805_ADC_SETTING3		(0x11)
#define PM805_ADC_GAIN1			(0x12)
#define PM805_ADC_GAIN2			(0x13)
#define PM805_DMIC_SETTING		(0x15)
#define PM805_DWS_SETTING		(0x16)
#define PM805_MIC_CONFLICT_STS	(0x17)

#define PM805_PDM_SETTING1		(0x20)
#define PM805_PDM_SETTING2		(0x21)
#define PM805_PDM_SETTING3		(0x22)
#define PM805_PDM_CONTROL1		(0x23)
#define PM805_PDM_CONTROL2		(0x24)
#define PM805_PDM_CONTROL3		(0x25)

#define PM805_HEADPHONE_SETTING			(0x26)
#define PM805_HEADPHONE_GAIN_A2A		(0x27)
#define PM805_HEADPHONE_SHORT_STATE		(0x28)
#define PM805_EARPHONE_SETTING			(0x29)
#define PM805_AUTO_SEQ_SETTING			(0x2A)

struct pm80x_rtc_pdata {
	int		vrtc;
	int		rtc_wakeup;
};

struct pm80x_subchip {
	struct i2c_client *power_page;	/* chip client for power page */
	struct i2c_client *gpadc_page;	/* chip client for gpadc page */
	struct regmap *regmap_power;
	struct regmap *regmap_gpadc;
	unsigned short power_page_addr;	/* power page I2C address */
	unsigned short gpadc_page_addr;	/* gpadc page I2C address */
};

struct pm80x_chip {
	struct pm80x_subchip *subchip;
	struct device *dev;
	struct i2c_client *client;
	struct regmap *regmap;
	struct regmap_irq_chip *regmap_irq_chip;
	struct regmap_irq_chip_data *irq_data;
	unsigned char version;
	int id;
	int irq;
	int irq_mode;
	unsigned long wu_flag;
	spinlock_t lock;
};

struct pm80x_platform_data {
	struct pm80x_rtc_pdata *rtc;
	unsigned short power_page_addr;	/* power page I2C address */
	unsigned short gpadc_page_addr;	/* gpadc page I2C address */
	int irq_mode;		/* Clear interrupt by read/write(0/1) */
	int batt_det;		/* enable/disable */
	int (*plat_config)(struct pm80x_chip *chip,
				struct pm80x_platform_data *pdata);
};

extern const struct dev_pm_ops pm80x_pm_ops;
extern const struct regmap_config pm80x_regmap_config;

static inline int pm80x_request_irq(struct pm80x_chip *pm80x, int irq,
				     irq_handler_t handler, unsigned long flags,
				     const char *name, void *data)
{
	if (!pm80x->irq_data)
		return -EINVAL;
	return request_threaded_irq(regmap_irq_get_virq(pm80x->irq_data, irq),
				    NULL, handler, flags, name, data);
}

static inline void pm80x_free_irq(struct pm80x_chip *pm80x, int irq, void *data)
{
	if (!pm80x->irq_data)
		return;
	free_irq(regmap_irq_get_virq(pm80x->irq_data, irq), data);
}

#ifdef CONFIG_PM
static inline int pm80x_dev_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct pm80x_chip *chip = dev_get_drvdata(pdev->dev.parent);
	int irq = platform_get_irq(pdev, 0);

	if (device_may_wakeup(dev))
		set_bit((1 << irq), &chip->wu_flag);

	return 0;
}

static inline int pm80x_dev_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct pm80x_chip *chip = dev_get_drvdata(pdev->dev.parent);
	int irq = platform_get_irq(pdev, 0);

	if (device_may_wakeup(dev))
		clear_bit((1 << irq), &chip->wu_flag);

	return 0;
}
#endif

extern int pm80x_init(struct i2c_client *client,
			     const struct i2c_device_id *id) __devinit;
extern int pm80x_deinit(struct i2c_client *client) __devexit;
#endif /* __LINUX_MFD_88PM80X_H */
