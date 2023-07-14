// SPDX-License-Identifier: GPL-2.0-only
//
// Based on sound/soc/codecs/tlv320aic3x.c by  Vladimir Barinov
//
// Copyright (C) 2010 Mistral Solutions Pvt Ltd.
// Author: Shahina Shaik <shahina.s@mistralsolutions.com>
//
// Copyright (C) 2014-2018, Ambarella, Inc.
// Author: Dongge wu <dgwu@ambarella.com>
//
// Copyright (C) 2021 Axis Communications AB
// Author: Ricard Wanderlof <ricardw@axis.com>
//

#include <dt-bindings/sound/tlv320adc3xxx.h>
#include <linux/clk.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/gpio/driver.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/cdev.h>
#include <linux/of_gpio.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/tlv.h>
#include <sound/initval.h>

/*
 * General definitions defining exported functionality.
 */

#define ADC3XXX_MICBIAS_PINS		2

/* Number of GPIO pins exposed via the gpiolib interface */
#define ADC3XXX_GPIOS_MAX		2

#define ADC3XXX_RATES		SNDRV_PCM_RATE_8000_96000
#define ADC3XXX_FORMATS		(SNDRV_PCM_FMTBIT_S16_LE | \
				 SNDRV_PCM_FMTBIT_S20_3LE | \
				 SNDRV_PCM_FMTBIT_S24_3LE | \
				 SNDRV_PCM_FMTBIT_S32_LE)

/*
 * PLL modes, to be used for clk_id for set_sysclk callback.
 *
 * The default behavior (AUTO) is to take the first matching entry in the clock
 * table, which is intended to be the PLL based one if there is more than one.
 *
 * Setting the clock source using simple-card (clocks or
 * system-clock-frequency property) sets clk_id = 0 = ADC3XXX_PLL_AUTO.
 */
#define ADC3XXX_PLL_AUTO	0 /* Use first available mode */
#define ADC3XXX_PLL_ENABLE	1 /* Use PLL for clock generation */
#define ADC3XXX_PLL_BYPASS	2 /* Don't use PLL for clock generation */

/* Register definitions. */

#define ADC3XXX_PAGE_SIZE		128
#define ADC3XXX_REG(page, reg)		((page * ADC3XXX_PAGE_SIZE) + reg)

/*
 * Page 0 registers.
 */

#define ADC3XXX_PAGE_SELECT			ADC3XXX_REG(0, 0)
#define ADC3XXX_RESET				ADC3XXX_REG(0, 1)

/* 2-3 Reserved */

#define ADC3XXX_CLKGEN_MUX			ADC3XXX_REG(0, 4)
#define ADC3XXX_PLL_PROG_PR			ADC3XXX_REG(0, 5)
#define ADC3XXX_PLL_PROG_J			ADC3XXX_REG(0, 6)
#define ADC3XXX_PLL_PROG_D_MSB			ADC3XXX_REG(0, 7)
#define ADC3XXX_PLL_PROG_D_LSB			ADC3XXX_REG(0, 8)

/* 9-17 Reserved */

#define ADC3XXX_ADC_NADC			ADC3XXX_REG(0, 18)
#define ADC3XXX_ADC_MADC			ADC3XXX_REG(0, 19)
#define ADC3XXX_ADC_AOSR			ADC3XXX_REG(0, 20)
#define ADC3XXX_ADC_IADC			ADC3XXX_REG(0, 21)

/* 23-24 Reserved */

#define ADC3XXX_CLKOUT_MUX			ADC3XXX_REG(0, 25)
#define ADC3XXX_CLKOUT_M_DIV			ADC3XXX_REG(0, 26)
#define ADC3XXX_INTERFACE_CTRL_1		ADC3XXX_REG(0, 27)
#define ADC3XXX_CH_OFFSET_1			ADC3XXX_REG(0, 28)
#define ADC3XXX_INTERFACE_CTRL_2		ADC3XXX_REG(0, 29)
#define ADC3XXX_BCLK_N_DIV			ADC3XXX_REG(0, 30)
#define ADC3XXX_INTERFACE_CTRL_3		ADC3XXX_REG(0, 31)
#define ADC3XXX_INTERFACE_CTRL_4		ADC3XXX_REG(0, 32)
#define ADC3XXX_INTERFACE_CTRL_5		ADC3XXX_REG(0, 33)
#define ADC3XXX_I2S_SYNC			ADC3XXX_REG(0, 34)
/* 35 Reserved */
#define ADC3XXX_ADC_FLAG			ADC3XXX_REG(0, 36)
#define ADC3XXX_CH_OFFSET_2			ADC3XXX_REG(0, 37)
#define ADC3XXX_I2S_TDM_CTRL			ADC3XXX_REG(0, 38)
/* 39-41 Reserved */
#define ADC3XXX_INTR_FLAG_1			ADC3XXX_REG(0, 42)
#define ADC3XXX_INTR_FLAG_2			ADC3XXX_REG(0, 43)
/* 44 Reserved */
#define ADC3XXX_INTR_FLAG_ADC1			ADC3XXX_REG(0, 45)
/* 46 Reserved */
#define ADC3XXX_INTR_FLAG_ADC2			ADC3XXX_REG(0, 47)
#define ADC3XXX_INT1_CTRL			ADC3XXX_REG(0, 48)
#define ADC3XXX_INT2_CTRL			ADC3XXX_REG(0, 49)
/* 50 Reserved */
#define ADC3XXX_GPIO2_CTRL			ADC3XXX_REG(0, 51)
#define ADC3XXX_GPIO1_CTRL			ADC3XXX_REG(0, 52)
#define ADC3XXX_DOUT_CTRL			ADC3XXX_REG(0, 53)
/* 54-56 Reserved */
#define ADC3XXX_SYNC_CTRL_1			ADC3XXX_REG(0, 57)
#define ADC3XXX_SYNC_CTRL_2			ADC3XXX_REG(0, 58)
#define ADC3XXX_CIC_GAIN_CTRL			ADC3XXX_REG(0, 59)
/* 60 Reserved */
#define ADC3XXX_PRB_SELECT			ADC3XXX_REG(0, 61)
#define ADC3XXX_INST_MODE_CTRL			ADC3XXX_REG(0, 62)
/* 63-79 Reserved */
#define ADC3XXX_MIC_POLARITY_CTRL		ADC3XXX_REG(0, 80)
#define ADC3XXX_ADC_DIGITAL			ADC3XXX_REG(0, 81)
#define	ADC3XXX_ADC_FGA				ADC3XXX_REG(0, 82)
#define ADC3XXX_LADC_VOL			ADC3XXX_REG(0, 83)
#define ADC3XXX_RADC_VOL			ADC3XXX_REG(0, 84)
#define ADC3XXX_ADC_PHASE_COMP			ADC3XXX_REG(0, 85)
#define ADC3XXX_LEFT_CHN_AGC_1			ADC3XXX_REG(0, 86)
#define ADC3XXX_LEFT_CHN_AGC_2			ADC3XXX_REG(0, 87)
#define ADC3XXX_LEFT_CHN_AGC_3			ADC3XXX_REG(0, 88)
#define ADC3XXX_LEFT_CHN_AGC_4			ADC3XXX_REG(0, 89)
#define ADC3XXX_LEFT_CHN_AGC_5			ADC3XXX_REG(0, 90)
#define ADC3XXX_LEFT_CHN_AGC_6			ADC3XXX_REG(0, 91)
#define ADC3XXX_LEFT_CHN_AGC_7			ADC3XXX_REG(0, 92)
#define ADC3XXX_LEFT_AGC_GAIN			ADC3XXX_REG(0, 93)
#define ADC3XXX_RIGHT_CHN_AGC_1			ADC3XXX_REG(0, 94)
#define ADC3XXX_RIGHT_CHN_AGC_2			ADC3XXX_REG(0, 95)
#define ADC3XXX_RIGHT_CHN_AGC_3			ADC3XXX_REG(0, 96)
#define ADC3XXX_RIGHT_CHN_AGC_4			ADC3XXX_REG(0, 97)
#define ADC3XXX_RIGHT_CHN_AGC_5			ADC3XXX_REG(0, 98)
#define ADC3XXX_RIGHT_CHN_AGC_6			ADC3XXX_REG(0, 99)
#define ADC3XXX_RIGHT_CHN_AGC_7			ADC3XXX_REG(0, 100)
#define ADC3XXX_RIGHT_AGC_GAIN			ADC3XXX_REG(0, 101)
/* 102-127 Reserved */

/*
 * Page 1 registers.
 */

/* 1-25 Reserved */
#define ADC3XXX_DITHER_CTRL			ADC3XXX_REG(1, 26)
/* 27-50 Reserved */
#define ADC3XXX_MICBIAS_CTRL			ADC3XXX_REG(1, 51)
#define ADC3XXX_LEFT_PGA_SEL_1			ADC3XXX_REG(1, 52)
/* 53 Reserved */
#define ADC3XXX_LEFT_PGA_SEL_2			ADC3XXX_REG(1, 54)
#define ADC3XXX_RIGHT_PGA_SEL_1			ADC3XXX_REG(1, 55)
#define ADC3XXX_RIGHT_PGA_SEL_2			ADC3XXX_REG(1, 57)
#define ADC3XXX_LEFT_APGA_CTRL			ADC3XXX_REG(1, 59)
#define ADC3XXX_RIGHT_APGA_CTRL			ADC3XXX_REG(1, 60)
#define ADC3XXX_LOW_CURRENT_MODES		ADC3XXX_REG(1, 61)
#define ADC3XXX_ANALOG_PGA_FLAGS		ADC3XXX_REG(1, 62)
/* 63-127 Reserved */

/*
 * Page 4 registers. First page of coefficient memory for the miniDSP.
 */
#define ADC3XXX_LEFT_ADC_IIR_COEFF_N0_MSB	ADC3XXX_REG(4, 8)
#define ADC3XXX_LEFT_ADC_IIR_COEFF_N0_LSB	ADC3XXX_REG(4, 9)
#define ADC3XXX_LEFT_ADC_IIR_COEFF_N1_MSB	ADC3XXX_REG(4, 10)
#define ADC3XXX_LEFT_ADC_IIR_COEFF_N1_LSB	ADC3XXX_REG(4, 11)
#define ADC3XXX_LEFT_ADC_IIR_COEFF_D1_MSB	ADC3XXX_REG(4, 12)
#define ADC3XXX_LEFT_ADC_IIR_COEFF_D1_LSB	ADC3XXX_REG(4, 13)

#define ADC3XXX_RIGHT_ADC_IIR_COEFF_N0_MSB	ADC3XXX_REG(4, 72)
#define ADC3XXX_RIGHT_ADC_IIR_COEFF_N0_LSB	ADC3XXX_REG(4, 73)
#define ADC3XXX_RIGHT_ADC_IIR_COEFF_N1_MSB	ADC3XXX_REG(4, 74)
#define ADC3XXX_RIGHT_ADC_IIR_COEFF_N1_LSB	ADC3XXX_REG(4, 75)
#define ADC3XXX_RIGHT_ADC_IIR_COEFF_D1_MSB	ADC3XXX_REG(4, 76)
#define ADC3XXX_RIGHT_ADC_IIR_COEFF_D1_LSB	ADC3XXX_REG(4, 77)

/*
 * Register bits.
 */

/* PLL Enable bits */
#define ADC3XXX_ENABLE_PLL_SHIFT	7
#define ADC3XXX_ENABLE_PLL		(1 << ADC3XXX_ENABLE_PLL_SHIFT)
#define ADC3XXX_ENABLE_NADC_SHIFT	7
#define ADC3XXX_ENABLE_NADC		(1 << ADC3XXX_ENABLE_NADC_SHIFT)
#define ADC3XXX_ENABLE_MADC_SHIFT	7
#define ADC3XXX_ENABLE_MADC		(1 << ADC3XXX_ENABLE_MADC_SHIFT)
#define ADC3XXX_ENABLE_BCLK_SHIFT	7
#define ADC3XXX_ENABLE_BCLK		(1 << ADC3XXX_ENABLE_BCLK_SHIFT)

/* Power bits */
#define ADC3XXX_LADC_PWR_ON		0x80
#define ADC3XXX_RADC_PWR_ON		0x40

#define ADC3XXX_SOFT_RESET		0x01
#define ADC3XXX_BCLK_MASTER		0x08
#define ADC3XXX_WCLK_MASTER		0x04

/* Interface register masks */
#define ADC3XXX_FORMAT_MASK		0xc0
#define ADC3XXX_FORMAT_SHIFT		6
#define ADC3XXX_WLENGTH_MASK		0x30
#define ADC3XXX_WLENGTH_SHIFT		4
#define ADC3XXX_CLKDIR_MASK		0x0c
#define ADC3XXX_CLKDIR_SHIFT		2

/* Interface register bit patterns */
#define ADC3XXX_FORMAT_I2S		(0 << ADC3XXX_FORMAT_SHIFT)
#define ADC3XXX_FORMAT_DSP		(1 << ADC3XXX_FORMAT_SHIFT)
#define ADC3XXX_FORMAT_RJF		(2 << ADC3XXX_FORMAT_SHIFT)
#define ADC3XXX_FORMAT_LJF		(3 << ADC3XXX_FORMAT_SHIFT)

#define ADC3XXX_IFACE_16BITS		(0 << ADC3XXX_WLENGTH_SHIFT)
#define ADC3XXX_IFACE_20BITS		(1 << ADC3XXX_WLENGTH_SHIFT)
#define ADC3XXX_IFACE_24BITS		(2 << ADC3XXX_WLENGTH_SHIFT)
#define ADC3XXX_IFACE_32BITS		(3 << ADC3XXX_WLENGTH_SHIFT)

/* PLL P/R bit offsets */
#define ADC3XXX_PLLP_SHIFT		4
#define ADC3XXX_PLLR_SHIFT		0
#define ADC3XXX_PLL_PR_MASK		0x7f
#define ADC3XXX_PLLJ_MASK		0x3f
#define ADC3XXX_PLLD_MSB_MASK		0x3f
#define ADC3XXX_PLLD_LSB_MASK		0xff
#define ADC3XXX_NADC_MASK		0x7f
#define ADC3XXX_MADC_MASK		0x7f
#define ADC3XXX_AOSR_MASK		0xff
#define ADC3XXX_IADC_MASK		0xff
#define ADC3XXX_BDIV_MASK		0x7f

/* PLL_CLKIN bits */
#define ADC3XXX_PLL_CLKIN_SHIFT		2
#define ADC3XXX_PLL_CLKIN_MCLK		0x0
#define ADC3XXX_PLL_CLKIN_BCLK		0x1
#define ADC3XXX_PLL_CLKIN_ZERO		0x3

/* CODEC_CLKIN bits */
#define ADC3XXX_CODEC_CLKIN_SHIFT	0
#define ADC3XXX_CODEC_CLKIN_MCLK	0x0
#define ADC3XXX_CODEC_CLKIN_BCLK	0x1
#define ADC3XXX_CODEC_CLKIN_PLL_CLK	0x3

#define ADC3XXX_USE_PLL	((ADC3XXX_PLL_CLKIN_MCLK << ADC3XXX_PLL_CLKIN_SHIFT) | \
			 (ADC3XXX_CODEC_CLKIN_PLL_CLK << ADC3XXX_CODEC_CLKIN_SHIFT))
#define ADC3XXX_NO_PLL	((ADC3XXX_PLL_CLKIN_ZERO << ADC3XXX_PLL_CLKIN_SHIFT) | \
			 (ADC3XXX_CODEC_CLKIN_MCLK << ADC3XXX_CODEC_CLKIN_SHIFT))

/*  Analog PGA control bits */
#define ADC3XXX_LPGA_MUTE		0x80
#define ADC3XXX_RPGA_MUTE		0x80

#define ADC3XXX_LPGA_GAIN_MASK		0x7f
#define ADC3XXX_RPGA_GAIN_MASK		0x7f

/* ADC current modes */
#define ADC3XXX_ADC_LOW_CURR_MODE	0x01

/* Left ADC Input selection bits */
#define ADC3XXX_LCH_SEL1_SHIFT		0
#define ADC3XXX_LCH_SEL2_SHIFT		2
#define ADC3XXX_LCH_SEL3_SHIFT		4
#define ADC3XXX_LCH_SEL4_SHIFT		6

#define ADC3XXX_LCH_SEL1X_SHIFT		0
#define ADC3XXX_LCH_SEL2X_SHIFT		2
#define ADC3XXX_LCH_SEL3X_SHIFT		4
#define ADC3XXX_LCH_COMMON_MODE		0x40
#define ADC3XXX_BYPASS_LPGA		0x80

/* Right ADC Input selection bits */
#define ADC3XXX_RCH_SEL1_SHIFT		0
#define ADC3XXX_RCH_SEL2_SHIFT		2
#define ADC3XXX_RCH_SEL3_SHIFT		4
#define ADC3XXX_RCH_SEL4_SHIFT		6

#define ADC3XXX_RCH_SEL1X_SHIFT		0
#define ADC3XXX_RCH_SEL2X_SHIFT		2
#define ADC3XXX_RCH_SEL3X_SHIFT		4
#define ADC3XXX_RCH_COMMON_MODE		0x40
#define ADC3XXX_BYPASS_RPGA		0x80

/* MICBIAS control bits */
#define ADC3XXX_MICBIAS_MASK		0x2
#define ADC3XXX_MICBIAS1_SHIFT		5
#define ADC3XXX_MICBIAS2_SHIFT		3

#define ADC3XXX_ADC_MAX_VOLUME		64
#define ADC3XXX_ADC_POS_VOL		24

/* GPIO control bits (GPIO1_CTRL and GPIO2_CTRL) */
#define ADC3XXX_GPIO_CTRL_CFG_MASK		0x3c
#define ADC3XXX_GPIO_CTRL_CFG_SHIFT		2
#define ADC3XXX_GPIO_CTRL_OUTPUT_CTRL_MASK	0x01
#define ADC3XXX_GPIO_CTRL_OUTPUT_CTRL_SHIFT	0
#define ADC3XXX_GPIO_CTRL_INPUT_VALUE_MASK	0x02
#define ADC3XXX_GPIO_CTRL_INPUT_VALUE_SHIFT	1

enum adc3xxx_type {
	ADC3001 = 0,
	ADC3101
};

struct adc3xxx {
	struct device *dev;
	enum adc3xxx_type type;
	struct clk *mclk;
	struct regmap *regmap;
	struct gpio_desc *rst_pin;
	unsigned int pll_mode;
	unsigned int sysclk;
	unsigned int gpio_cfg[ADC3XXX_GPIOS_MAX]; /* value+1 (0 => not set)  */
	unsigned int micbias_vg[ADC3XXX_MICBIAS_PINS];
	int master;
	u8 page_no;
	int use_pll;
	struct gpio_chip gpio_chip;
};

static const unsigned int adc3xxx_gpio_ctrl_reg[ADC3XXX_GPIOS_MAX] = {
	ADC3XXX_GPIO1_CTRL,
	ADC3XXX_GPIO2_CTRL
};

static const unsigned int adc3xxx_micbias_shift[ADC3XXX_MICBIAS_PINS] = {
	ADC3XXX_MICBIAS1_SHIFT,
	ADC3XXX_MICBIAS2_SHIFT
};

static const struct reg_default adc3xxx_defaults[] = {
	/* Page 0 */
	{ 0, 0x00 },    { 1, 0x00 },    { 2, 0x00 },    { 3, 0x00 },
	{ 4, 0x00 },    { 5, 0x11 },    { 6, 0x04 },    { 7, 0x00 },
	{ 8, 0x00 },    { 9, 0x00 },    { 10, 0x00 },   { 11, 0x00 },
	{ 12, 0x00 },   { 13, 0x00 },   { 14, 0x00 },   { 15, 0x00 },
	{ 16, 0x00 },   { 17, 0x00 },   { 18, 0x01 },   { 19, 0x01 },
	{ 20, 0x80 },   { 21, 0x80 },   { 22, 0x04 },   { 23, 0x00 },
	{ 24, 0x00 },   { 25, 0x00 },   { 26, 0x01 },   { 27, 0x00 },
	{ 28, 0x00 },   { 29, 0x02 },   { 30, 0x01 },   { 31, 0x00 },
	{ 32, 0x00 },   { 33, 0x10 },   { 34, 0x00 },   { 35, 0x00 },
	{ 36, 0x00 },   { 37, 0x00 },   { 38, 0x02 },   { 39, 0x00 },
	{ 40, 0x00 },   { 41, 0x00 },   { 42, 0x00 },   { 43, 0x00 },
	{ 44, 0x00 },   { 45, 0x00 },   { 46, 0x00 },   { 47, 0x00 },
	{ 48, 0x00 },   { 49, 0x00 },   { 50, 0x00 },   { 51, 0x00 },
	{ 52, 0x00 },   { 53, 0x12 },   { 54, 0x00 },   { 55, 0x00 },
	{ 56, 0x00 },   { 57, 0x00 },   { 58, 0x00 },   { 59, 0x44 },
	{ 60, 0x00 },   { 61, 0x01 },   { 62, 0x00 },   { 63, 0x00 },
	{ 64, 0x00 },   { 65, 0x00 },   { 66, 0x00 },   { 67, 0x00 },
	{ 68, 0x00 },   { 69, 0x00 },   { 70, 0x00 },   { 71, 0x00 },
	{ 72, 0x00 },   { 73, 0x00 },   { 74, 0x00 },   { 75, 0x00 },
	{ 76, 0x00 },   { 77, 0x00 },   { 78, 0x00 },   { 79, 0x00 },
	{ 80, 0x00 },   { 81, 0x00 },   { 82, 0x88 },   { 83, 0x00 },
	{ 84, 0x00 },   { 85, 0x00 },   { 86, 0x00 },   { 87, 0x00 },
	{ 88, 0x7f },   { 89, 0x00 },   { 90, 0x00 },   { 91, 0x00 },
	{ 92, 0x00 },   { 93, 0x00 },   { 94, 0x00 },   { 95, 0x00 },
	{ 96, 0x7f },   { 97, 0x00 },   { 98, 0x00 },   { 99, 0x00 },
	{ 100, 0x00 },  { 101, 0x00 },  { 102, 0x00 },  { 103, 0x00 },
	{ 104, 0x00 },  { 105, 0x00 },  { 106, 0x00 },  { 107, 0x00 },
	{ 108, 0x00 },  { 109, 0x00 },  { 110, 0x00 },  { 111, 0x00 },
	{ 112, 0x00 },  { 113, 0x00 },  { 114, 0x00 },  { 115, 0x00 },
	{ 116, 0x00 },  { 117, 0x00 },  { 118, 0x00 },  { 119, 0x00 },
	{ 120, 0x00 },  { 121, 0x00 },  { 122, 0x00 },  { 123, 0x00 },
	{ 124, 0x00 },  { 125, 0x00 },  { 126, 0x00 },  { 127, 0x00 },

	/* Page 1 */
	{ 128, 0x00 },  { 129, 0x00 },  { 130, 0x00 },  { 131, 0x00 },
	{ 132, 0x00 },  { 133, 0x00 },  { 134, 0x00 },  { 135, 0x00 },
	{ 136, 0x00 },  { 137, 0x00 },  { 138, 0x00 },  { 139, 0x00 },
	{ 140, 0x00 },  { 141, 0x00 },  { 142, 0x00 },  { 143, 0x00 },
	{ 144, 0x00 },  { 145, 0x00 },  { 146, 0x00 },  { 147, 0x00 },
	{ 148, 0x00 },  { 149, 0x00 },  { 150, 0x00 },  { 151, 0x00 },
	{ 152, 0x00 },  { 153, 0x00 },  { 154, 0x00 },  { 155, 0x00 },
	{ 156, 0x00 },  { 157, 0x00 },  { 158, 0x00 },  { 159, 0x00 },
	{ 160, 0x00 },  { 161, 0x00 },  { 162, 0x00 },  { 163, 0x00 },
	{ 164, 0x00 },  { 165, 0x00 },  { 166, 0x00 },  { 167, 0x00 },
	{ 168, 0x00 },  { 169, 0x00 },  { 170, 0x00 },  { 171, 0x00 },
	{ 172, 0x00 },  { 173, 0x00 },  { 174, 0x00 },  { 175, 0x00 },
	{ 176, 0x00 },  { 177, 0x00 },  { 178, 0x00 },  { 179, 0x00 },
	{ 180, 0xff },  { 181, 0x00 },  { 182, 0x3f },  { 183, 0xff },
	{ 184, 0x00 },  { 185, 0x3f },  { 186, 0x00 },  { 187, 0x80 },
	{ 188, 0x80 },  { 189, 0x00 },  { 190, 0x00 },  { 191, 0x00 },

	/* Page 4 */
	{ 1024, 0x00 },			{ 1026, 0x01 },	{ 1027, 0x17 },
	{ 1028, 0x01 }, { 1029, 0x17 }, { 1030, 0x7d }, { 1031, 0xd3 },
	{ 1032, 0x7f }, { 1033, 0xff }, { 1034, 0x00 }, { 1035, 0x00 },
	{ 1036, 0x00 }, { 1037, 0x00 }, { 1038, 0x7f }, { 1039, 0xff },
	{ 1040, 0x00 }, { 1041, 0x00 }, { 1042, 0x00 }, { 1043, 0x00 },
	{ 1044, 0x00 }, { 1045, 0x00 }, { 1046, 0x00 }, { 1047, 0x00 },
	{ 1048, 0x7f }, { 1049, 0xff }, { 1050, 0x00 }, { 1051, 0x00 },
	{ 1052, 0x00 }, { 1053, 0x00 }, { 1054, 0x00 }, { 1055, 0x00 },
	{ 1056, 0x00 }, { 1057, 0x00 }, { 1058, 0x7f }, { 1059, 0xff },
	{ 1060, 0x00 }, { 1061, 0x00 }, { 1062, 0x00 }, { 1063, 0x00 },
	{ 1064, 0x00 }, { 1065, 0x00 }, { 1066, 0x00 }, { 1067, 0x00 },
	{ 1068, 0x7f }, { 1069, 0xff }, { 1070, 0x00 }, { 1071, 0x00 },
	{ 1072, 0x00 }, { 1073, 0x00 }, { 1074, 0x00 }, { 1075, 0x00 },
	{ 1076, 0x00 }, { 1077, 0x00 }, { 1078, 0x7f }, { 1079, 0xff },
	{ 1080, 0x00 }, { 1081, 0x00 }, { 1082, 0x00 }, { 1083, 0x00 },
	{ 1084, 0x00 }, { 1085, 0x00 }, { 1086, 0x00 }, { 1087, 0x00 },
	{ 1088, 0x00 }, { 1089, 0x00 }, { 1090, 0x00 }, { 1091, 0x00 },
	{ 1092, 0x00 }, { 1093, 0x00 }, { 1094, 0x00 }, { 1095, 0x00 },
	{ 1096, 0x00 }, { 1097, 0x00 }, { 1098, 0x00 }, { 1099, 0x00 },
	{ 1100, 0x00 }, { 1101, 0x00 }, { 1102, 0x00 }, { 1103, 0x00 },
	{ 1104, 0x00 }, { 1105, 0x00 }, { 1106, 0x00 }, { 1107, 0x00 },
	{ 1108, 0x00 }, { 1109, 0x00 }, { 1110, 0x00 }, { 1111, 0x00 },
	{ 1112, 0x00 }, { 1113, 0x00 }, { 1114, 0x00 }, { 1115, 0x00 },
	{ 1116, 0x00 }, { 1117, 0x00 }, { 1118, 0x00 }, { 1119, 0x00 },
	{ 1120, 0x00 }, { 1121, 0x00 }, { 1122, 0x00 }, { 1123, 0x00 },
	{ 1124, 0x00 }, { 1125, 0x00 }, { 1126, 0x00 }, { 1127, 0x00 },
	{ 1128, 0x00 }, { 1129, 0x00 }, { 1130, 0x00 }, { 1131, 0x00 },
	{ 1132, 0x00 }, { 1133, 0x00 }, { 1134, 0x00 }, { 1135, 0x00 },
	{ 1136, 0x00 }, { 1137, 0x00 }, { 1138, 0x00 }, { 1139, 0x00 },
	{ 1140, 0x00 }, { 1141, 0x00 }, { 1142, 0x00 }, { 1143, 0x00 },
	{ 1144, 0x00 }, { 1145, 0x00 }, { 1146, 0x00 }, { 1147, 0x00 },
	{ 1148, 0x00 }, { 1149, 0x00 }, { 1150, 0x00 }, { 1151, 0x00 },
};

static bool adc3xxx_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case ADC3XXX_RESET:
		return true;
	default:
		return false;
	}
}

static const struct regmap_range_cfg adc3xxx_ranges[] = {
	{
		.range_min = 0,
		.range_max = 5 * ADC3XXX_PAGE_SIZE,
		.selector_reg = ADC3XXX_PAGE_SELECT,
		.selector_mask = 0xff,
		.selector_shift = 0,
		.window_start = 0,
		.window_len = ADC3XXX_PAGE_SIZE,
	}
};

static const struct regmap_config adc3xxx_regmap = {
	.reg_bits = 8,
	.val_bits = 8,

	.reg_defaults = adc3xxx_defaults,
	.num_reg_defaults = ARRAY_SIZE(adc3xxx_defaults),

	.volatile_reg = adc3xxx_volatile_reg,

	.cache_type = REGCACHE_RBTREE,

	.ranges = adc3xxx_ranges,
	.num_ranges = ARRAY_SIZE(adc3xxx_ranges),
	.max_register = 5 * ADC3XXX_PAGE_SIZE,
};

struct adc3xxx_rate_divs {
	u32 mclk;
	u32 rate;
	u8 pll_p;
	u8 pll_r;
	u8 pll_j;
	u16 pll_d;
	u8 nadc;
	u8 madc;
	u8 aosr;
};

/*
 * PLL and Clock settings.
 * If p member is 0, PLL is not used.
 * The order of the entries in this table have the PLL entries before
 * the non-PLL entries, so that the PLL modes are preferred unless
 * the PLL mode setting says otherwise.
 */
static const struct adc3xxx_rate_divs adc3xxx_divs[] = {
	/* mclk, rate, p, r, j, d, nadc, madc, aosr */
	/* 8k rate */
	{ 12000000, 8000, 1, 1, 7, 1680, 42, 2, 128 },
	{ 12288000, 8000, 1, 1, 7, 0000, 42, 2, 128 },
	/* 11.025k rate */
	{ 12000000, 11025, 1, 1, 6, 8208, 29, 2, 128 },
	/* 16k rate */
	{ 12000000, 16000, 1, 1, 7, 1680, 21, 2, 128 },
	{ 12288000, 16000, 1, 1, 7, 0000, 21, 2, 128 },
	/* 22.05k rate */
	{ 12000000, 22050, 1, 1, 7, 560, 15, 2, 128 },
	/* 32k rate */
	{ 12000000, 32000, 1, 1, 8, 1920, 12, 2, 128 },
	{ 12288000, 32000, 1, 1, 8, 0000, 12, 2, 128 },
	/* 44.1k rate */
	{ 12000000, 44100, 1, 1, 7, 5264, 8, 2, 128 },
	/* 48k rate */
	{ 12000000, 48000, 1, 1, 7, 1680, 7, 2, 128 },
	{ 12288000, 48000, 1, 1, 7, 0000, 7, 2, 128 },
	{ 24576000, 48000, 1, 1, 3, 5000, 7, 2, 128 }, /* With PLL */
	{ 24576000, 48000, 0, 0, 0, 0000, 2, 2, 128 }, /* Without PLL */
	/* 88.2k rate */
	{ 12000000, 88200, 1, 1, 7, 5264, 4, 4, 64 },
	/* 96k rate */
	{ 12000000, 96000, 1, 1, 8, 1920, 4, 4, 64 },
};

static int adc3xxx_get_divs(struct device *dev, int mclk, int rate, int pll_mode)
{
	int i;

	dev_dbg(dev, "mclk = %d, rate = %d, clock mode %u\n",
		mclk, rate, pll_mode);
	for (i = 0; i < ARRAY_SIZE(adc3xxx_divs); i++) {
		const struct adc3xxx_rate_divs *mode = &adc3xxx_divs[i];

		/* Skip this entry if it doesn't fulfill the intended clock
		 * mode requirement. We consider anything besides the two
		 * modes below to be the same as ADC3XXX_PLL_AUTO.
		 */
		if ((pll_mode == ADC3XXX_PLL_BYPASS && mode->pll_p) ||
		    (pll_mode == ADC3XXX_PLL_ENABLE && !mode->pll_p))
			continue;

		if (mode->rate == rate && mode->mclk == mclk)
			return i;
	}

	dev_info(dev, "Master clock rate %d and sample rate %d is not supported\n",
		 mclk, rate);
	return -EINVAL;
}

static int adc3xxx_pll_delay(struct snd_soc_dapm_widget *w,
			     struct snd_kcontrol *kcontrol, int event)
{
	/* 10msec delay needed after PLL power-up to allow
	 * PLL and dividers to stabilize (datasheet p13).
	 */
	usleep_range(10000, 20000);

	return 0;
}

static int adc3xxx_coefficient_info(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_info *uinfo)
{
	int numcoeff = kcontrol->private_value >> 16;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = numcoeff;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 0xffff; /* all coefficients are 16 bit */
	return 0;
}

static int adc3xxx_coefficient_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	int numcoeff  = kcontrol->private_value >> 16;
	int reg = kcontrol->private_value & 0xffff;
	int index = 0;

	for (index = 0; index < numcoeff; index++) {
		unsigned int value_msb, value_lsb, value;

		value_msb = snd_soc_component_read(component, reg++);
		if ((int)value_msb < 0)
			return (int)value_msb;

		value_lsb = snd_soc_component_read(component, reg++);
		if ((int)value_lsb < 0)
			return (int)value_lsb;

		value = (value_msb << 8) | value_lsb;
		ucontrol->value.integer.value[index] = value;
	}

	return 0;
}

static int adc3xxx_coefficient_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	int numcoeff  = kcontrol->private_value >> 16;
	int reg = kcontrol->private_value & 0xffff;
	int index = 0;
	int ret;

	for (index = 0; index < numcoeff; index++) {
		unsigned int value = ucontrol->value.integer.value[index];
		unsigned int value_msb = (value >> 8) & 0xff;
		unsigned int value_lsb = value & 0xff;

		ret = snd_soc_component_write(component, reg++, value_msb);
		if (ret)
			return ret;

		ret = snd_soc_component_write(component, reg++, value_lsb);
		if (ret)
			return ret;
	}

	return 0;
}

/* All on-chip filters have coefficients which are expressed in terms of
 * 16 bit values, so represent them as strings of 16-bit integers.
 */
#define TI_COEFFICIENTS(xname, reg, numcoeffs) { \
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
	.name = xname, \
	.info = adc3xxx_coefficient_info, \
	.get = adc3xxx_coefficient_get,\
	.put = adc3xxx_coefficient_put, \
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE, \
	.private_value = reg | (numcoeffs << 16) \
}

static const char * const adc_softstepping_text[] = { "1 step", "2 step", "off" };
static SOC_ENUM_SINGLE_DECL(adc_softstepping_enum, ADC3XXX_ADC_DIGITAL, 0,
			    adc_softstepping_text);

static const char * const multiplier_text[] = { "1", "2", "4", "8", "16", "32", "64", "128" };
static SOC_ENUM_SINGLE_DECL(left_agc_attack_mult_enum,
			    ADC3XXX_LEFT_CHN_AGC_4, 0, multiplier_text);
static SOC_ENUM_SINGLE_DECL(right_agc_attack_mult_enum,
			    ADC3XXX_RIGHT_CHN_AGC_4, 0, multiplier_text);
static SOC_ENUM_SINGLE_DECL(left_agc_decay_mult_enum,
			    ADC3XXX_LEFT_CHN_AGC_5, 0, multiplier_text);
static SOC_ENUM_SINGLE_DECL(right_agc_decay_mult_enum,
			    ADC3XXX_RIGHT_CHN_AGC_5, 0, multiplier_text);

static const char * const dither_dc_offset_text[] = {
	"0mV", "15mV", "30mV", "45mV", "60mV", "75mV", "90mV", "105mV",
	"-15mV", "-30mV", "-45mV", "-60mV", "-75mV", "-90mV", "-105mV"
};
static const unsigned int dither_dc_offset_values[] = {
	0, 1, 2, 3, 4, 5, 6, 7, 9, 10, 11, 12, 13, 14, 15
};
static SOC_VALUE_ENUM_DOUBLE_DECL(dither_dc_offset_enum,
				  ADC3XXX_DITHER_CTRL,
				  4, 0, 0xf, dither_dc_offset_text,
				  dither_dc_offset_values);

static const DECLARE_TLV_DB_SCALE(pga_tlv, 0, 50, 0);
static const DECLARE_TLV_DB_SCALE(adc_tlv, -1200, 50, 0);
static const DECLARE_TLV_DB_SCALE(adc_fine_tlv, -40, 10, 0);
/* AGC target: 8 values: -5.5, -8, -10, -12, -14, -17, -20, -24 dB */
/* It would be nice to declare these in the order above, but empirically
 * TLV_DB_SCALE_ITEM doesn't take lightly to the increment (second) parameter
 * being negative, despite there being examples to the contrary in other
 * drivers. So declare these in the order from lowest to highest, and
 * set the invert flag in the SOC_DOUBLE_R_TLV declaration instead.
 */
static const DECLARE_TLV_DB_RANGE(agc_target_tlv,
	0, 0, TLV_DB_SCALE_ITEM(-2400, 0, 0),
	1, 3, TLV_DB_SCALE_ITEM(-2000, 300, 0),
	4, 6, TLV_DB_SCALE_ITEM(-1200, 200, 0),
	7, 7, TLV_DB_SCALE_ITEM(-550, 0, 0));
/* Since the 'disabled' value (mute) is at the highest value in the dB
 * range (i.e. just before -32 dB) rather than the lowest, we need to resort
 * to using a TLV_DB_RANGE in order to get the mute value in the right place.
 */
static const DECLARE_TLV_DB_RANGE(agc_thresh_tlv,
	0, 30, TLV_DB_SCALE_ITEM(-9000, 200, 0),
	31, 31, TLV_DB_SCALE_ITEM(0, 0, 1)); /* disabled = mute */
/* AGC hysteresis: 4 values: 1, 2, 4 dB, disabled (= mute) */
static const DECLARE_TLV_DB_RANGE(agc_hysteresis_tlv,
	0, 1, TLV_DB_SCALE_ITEM(100, 100, 0),
	2, 2, TLV_DB_SCALE_ITEM(400, 0, 0),
	3, 3, TLV_DB_SCALE_ITEM(0, 0, 1)); /* disabled = mute */
static const DECLARE_TLV_DB_SCALE(agc_max_tlv, 0, 50, 0);
/* Input attenuation: -6 dB or 0 dB */
static const DECLARE_TLV_DB_SCALE(input_attenuation_tlv, -600, 600, 0);

static const struct snd_kcontrol_new adc3xxx_snd_controls[] = {
	SOC_DOUBLE_R_TLV("PGA Capture Volume", ADC3XXX_LEFT_APGA_CTRL,
			 ADC3XXX_RIGHT_APGA_CTRL, 0, 80, 0, pga_tlv),
	SOC_DOUBLE("PGA Capture Switch", ADC3XXX_ADC_FGA, 7, 3, 1, 1),
	SOC_DOUBLE_R("AGC Capture Switch", ADC3XXX_LEFT_CHN_AGC_1,
		     ADC3XXX_RIGHT_CHN_AGC_1, 7, 1, 0),
	SOC_DOUBLE_R_TLV("AGC Target Level Capture Volume", ADC3XXX_LEFT_CHN_AGC_1,
		     ADC3XXX_RIGHT_CHN_AGC_2, 4, 0x07, 1, agc_target_tlv),
	SOC_DOUBLE_R_TLV("AGC Noise Threshold Capture Volume", ADC3XXX_LEFT_CHN_AGC_2,
		     ADC3XXX_RIGHT_CHN_AGC_2, 1, 0x1f, 1, agc_thresh_tlv),
	SOC_DOUBLE_R_TLV("AGC Hysteresis Capture Volume", ADC3XXX_LEFT_CHN_AGC_2,
		     ADC3XXX_RIGHT_CHN_AGC_2, 6, 3, 0, agc_hysteresis_tlv),
	SOC_DOUBLE_R("AGC Clip Stepping Capture Switch", ADC3XXX_LEFT_CHN_AGC_2,
		     ADC3XXX_RIGHT_CHN_AGC_2, 0, 1, 0),
	/*
	 * Oddly enough, the data sheet says the default value
	 * for the left/right AGC maximum gain register field
	 * (ADC3XXX_LEFT/RIGHT_CHN_AGC_3 bits 0..6) is 0x7f = 127
	 * (verified empirically) even though this value (indeed, above
	 * 0x50) is specified as 'Reserved. Do not use.' in the accompanying
	 * table in the data sheet.
	 */
	SOC_DOUBLE_R_TLV("AGC Maximum Capture Volume", ADC3XXX_LEFT_CHN_AGC_3,
		     ADC3XXX_RIGHT_CHN_AGC_3, 0, 0x50, 0, agc_max_tlv),
	SOC_DOUBLE_R("AGC Attack Time", ADC3XXX_LEFT_CHN_AGC_4,
		     ADC3XXX_RIGHT_CHN_AGC_4, 3, 0x1f, 0),
	/* Would like to have the multipliers as LR pairs, but there is
	 * no SOC_ENUM_foo which accepts two values in separate registers.
	 */
	SOC_ENUM("AGC Left Attack Time Multiplier", left_agc_attack_mult_enum),
	SOC_ENUM("AGC Right Attack Time Multiplier", right_agc_attack_mult_enum),
	SOC_DOUBLE_R("AGC Decay Time", ADC3XXX_LEFT_CHN_AGC_5,
		     ADC3XXX_RIGHT_CHN_AGC_5, 3, 0x1f, 0),
	SOC_ENUM("AGC Left Decay Time Multiplier", left_agc_decay_mult_enum),
	SOC_ENUM("AGC Right Decay Time Multiplier", right_agc_decay_mult_enum),
	SOC_DOUBLE_R("AGC Noise Debounce", ADC3XXX_LEFT_CHN_AGC_6,
		     ADC3XXX_RIGHT_CHN_AGC_6, 0, 0x1f, 0),
	SOC_DOUBLE_R("AGC Signal Debounce", ADC3XXX_LEFT_CHN_AGC_7,
		     ADC3XXX_RIGHT_CHN_AGC_7, 0, 0x0f, 0),
	/* Read only register */
	SOC_DOUBLE_R_S_TLV("AGC Applied Capture Volume", ADC3XXX_LEFT_AGC_GAIN,
			   ADC3XXX_RIGHT_AGC_GAIN, 0, -24, 40, 6, 0, adc_tlv),
	/* ADC soft stepping */
	SOC_ENUM("ADC Soft Stepping", adc_softstepping_enum),
	/* Left/Right Input attenuation */
	SOC_SINGLE_TLV("Left Input IN_1L Capture Volume",
		       ADC3XXX_LEFT_PGA_SEL_1, 0, 1, 1, input_attenuation_tlv),
	SOC_SINGLE_TLV("Left Input IN_2L Capture Volume",
		       ADC3XXX_LEFT_PGA_SEL_1, 2, 1, 1, input_attenuation_tlv),
	SOC_SINGLE_TLV("Left Input IN_3L Capture Volume",
		       ADC3XXX_LEFT_PGA_SEL_1, 4, 1, 1, input_attenuation_tlv),
	SOC_SINGLE_TLV("Left Input IN_1R Capture Volume",
		       ADC3XXX_LEFT_PGA_SEL_2, 0, 1, 1, input_attenuation_tlv),
	SOC_SINGLE_TLV("Left Input DIF_2L_3L Capture Volume",
		       ADC3XXX_LEFT_PGA_SEL_1, 6, 1, 1, input_attenuation_tlv),
	SOC_SINGLE_TLV("Left Input DIF_1L_1R Capture Volume",
		       ADC3XXX_LEFT_PGA_SEL_2, 4, 1, 1, input_attenuation_tlv),
	SOC_SINGLE_TLV("Left Input DIF_2R_3R Capture Volume",
		       ADC3XXX_LEFT_PGA_SEL_2, 2, 1, 1, input_attenuation_tlv),
	SOC_SINGLE_TLV("Right Input IN_1R Capture Volume",
		       ADC3XXX_RIGHT_PGA_SEL_1, 0, 1, 1, input_attenuation_tlv),
	SOC_SINGLE_TLV("Right Input IN_2R Capture Volume",
		       ADC3XXX_RIGHT_PGA_SEL_1, 2, 1, 1, input_attenuation_tlv),
	SOC_SINGLE_TLV("Right Input IN_3R Capture Volume",
		       ADC3XXX_RIGHT_PGA_SEL_1, 4, 1, 1, input_attenuation_tlv),
	SOC_SINGLE_TLV("Right Input IN_1L Capture Volume",
		       ADC3XXX_RIGHT_PGA_SEL_2, 0, 1, 1, input_attenuation_tlv),
	SOC_SINGLE_TLV("Right Input DIF_2R_3R Capture Volume",
		       ADC3XXX_RIGHT_PGA_SEL_1, 6, 1, 1, input_attenuation_tlv),
	SOC_SINGLE_TLV("Right Input DIF_1L_1R Capture Volume",
		       ADC3XXX_RIGHT_PGA_SEL_2, 4, 1, 1, input_attenuation_tlv),
	SOC_SINGLE_TLV("Right Input DIF_2L_3L Capture Volume",
		       ADC3XXX_RIGHT_PGA_SEL_2, 2, 1, 1, input_attenuation_tlv),
	SOC_DOUBLE_R_S_TLV("ADC Volume Control Capture Volume", ADC3XXX_LADC_VOL,
			   ADC3XXX_RADC_VOL, 0, -24, 40, 6, 0, adc_tlv),
	/* Empirically, the following doesn't work the way it's supposed
	 * to. Values 0, -0.1, -0.2 and -0.3 dB result in the same level, and
	 * -0.4 dB drops about 0.12 dB on a specific chip.
	 */
	SOC_DOUBLE_TLV("ADC Fine Volume Control Capture Volume", ADC3XXX_ADC_FGA,
		       4, 0, 4, 1, adc_fine_tlv),
	SOC_SINGLE("Left ADC Unselected CM Bias Capture Switch",
		   ADC3XXX_LEFT_PGA_SEL_2, 6, 1, 0),
	SOC_SINGLE("Right ADC Unselected CM Bias Capture Switch",
		   ADC3XXX_RIGHT_PGA_SEL_2, 6, 1, 0),
	SOC_ENUM("Dither Control DC Offset", dither_dc_offset_enum),

	/* Coefficient memory for miniDSP. */
	/* For the default PRB_R1 processing block, the only available
	 * filter is the first order IIR.
	 */

	TI_COEFFICIENTS("Left ADC IIR Coefficients N0 N1 D1",
			ADC3XXX_LEFT_ADC_IIR_COEFF_N0_MSB, 3),

	TI_COEFFICIENTS("Right ADC IIR Coefficients N0 N1 D1",
			ADC3XXX_RIGHT_ADC_IIR_COEFF_N0_MSB, 3),
};

/* Left input selection, Single Ended inputs and Differential inputs */
static const struct snd_kcontrol_new left_input_mixer_controls[] = {
	SOC_DAPM_SINGLE("IN_1L Capture Switch",
			ADC3XXX_LEFT_PGA_SEL_1, 1, 0x1, 1),
	SOC_DAPM_SINGLE("IN_2L Capture Switch",
			ADC3XXX_LEFT_PGA_SEL_1, 3, 0x1, 1),
	SOC_DAPM_SINGLE("IN_3L Capture Switch",
			ADC3XXX_LEFT_PGA_SEL_1, 5, 0x1, 1),
	SOC_DAPM_SINGLE("DIF_2L_3L Capture Switch",
			ADC3XXX_LEFT_PGA_SEL_1, 7, 0x1, 1),
	SOC_DAPM_SINGLE("DIF_1L_1R Capture Switch",
			ADC3XXX_LEFT_PGA_SEL_2, 5, 0x1, 1),
	SOC_DAPM_SINGLE("DIF_2R_3R Capture Switch",
			ADC3XXX_LEFT_PGA_SEL_2, 3, 0x1, 1),
	SOC_DAPM_SINGLE("IN_1R Capture Switch",
			ADC3XXX_LEFT_PGA_SEL_2, 1, 0x1, 1),
};

/* Right input selection, Single Ended inputs and Differential inputs */
static const struct snd_kcontrol_new right_input_mixer_controls[] = {
	SOC_DAPM_SINGLE("IN_1R Capture Switch",
			ADC3XXX_RIGHT_PGA_SEL_1, 1, 0x1, 1),
	SOC_DAPM_SINGLE("IN_2R Capture Switch",
			ADC3XXX_RIGHT_PGA_SEL_1, 3, 0x1, 1),
	SOC_DAPM_SINGLE("IN_3R Capture Switch",
			 ADC3XXX_RIGHT_PGA_SEL_1, 5, 0x1, 1),
	SOC_DAPM_SINGLE("DIF_2R_3R Capture Switch",
			 ADC3XXX_RIGHT_PGA_SEL_1, 7, 0x1, 1),
	SOC_DAPM_SINGLE("DIF_1L_1R Capture Switch",
			 ADC3XXX_RIGHT_PGA_SEL_2, 5, 0x1, 1),
	SOC_DAPM_SINGLE("DIF_2L_3L Capture Switch",
			 ADC3XXX_RIGHT_PGA_SEL_2, 3, 0x1, 1),
	SOC_DAPM_SINGLE("IN_1L Capture Switch",
			 ADC3XXX_RIGHT_PGA_SEL_2, 1, 0x1, 1),
};

/* Left Digital Mic input for left ADC */
static const struct snd_kcontrol_new left_input_dmic_controls[] = {
	SOC_DAPM_SINGLE("Left ADC Capture Switch",
			ADC3XXX_ADC_DIGITAL, 3, 0x1, 0),
};

/* Right Digital Mic input for Right ADC */
static const struct snd_kcontrol_new right_input_dmic_controls[] = {
	SOC_DAPM_SINGLE("Right ADC Capture Switch",
			ADC3XXX_ADC_DIGITAL, 2, 0x1, 0),
};

/* DAPM widgets */
static const struct snd_soc_dapm_widget adc3xxx_dapm_widgets[] = {

	/* Left Input Selection */
	SND_SOC_DAPM_MIXER("Left Input", SND_SOC_NOPM, 0, 0,
			   &left_input_mixer_controls[0],
			   ARRAY_SIZE(left_input_mixer_controls)),
	/* Right Input Selection */
	SND_SOC_DAPM_MIXER("Right Input", SND_SOC_NOPM, 0, 0,
			   &right_input_mixer_controls[0],
			   ARRAY_SIZE(right_input_mixer_controls)),
	/* PGA selection */
	SND_SOC_DAPM_PGA("Left PGA", ADC3XXX_LEFT_APGA_CTRL, 7, 1, NULL, 0),
	SND_SOC_DAPM_PGA("Right PGA", ADC3XXX_RIGHT_APGA_CTRL, 7, 1, NULL, 0),

	/* Digital Microphone Input Control for Left/Right ADC */
	SND_SOC_DAPM_MIXER("Left DMic Input", SND_SOC_NOPM, 0, 0,
			&left_input_dmic_controls[0],
			ARRAY_SIZE(left_input_dmic_controls)),
	SND_SOC_DAPM_MIXER("Right DMic Input", SND_SOC_NOPM, 0, 0,
			&right_input_dmic_controls[0],
			ARRAY_SIZE(right_input_dmic_controls)),

	/* Left/Right ADC */
	SND_SOC_DAPM_ADC("Left ADC", "Left Capture", ADC3XXX_ADC_DIGITAL, 7, 0),
	SND_SOC_DAPM_ADC("Right ADC", "Right Capture", ADC3XXX_ADC_DIGITAL, 6, 0),

	/* Inputs */
	SND_SOC_DAPM_INPUT("IN_1L"),
	SND_SOC_DAPM_INPUT("IN_1R"),
	SND_SOC_DAPM_INPUT("IN_2L"),
	SND_SOC_DAPM_INPUT("IN_2R"),
	SND_SOC_DAPM_INPUT("IN_3L"),
	SND_SOC_DAPM_INPUT("IN_3R"),
	SND_SOC_DAPM_INPUT("DIFL_1L_1R"),
	SND_SOC_DAPM_INPUT("DIFL_2L_3L"),
	SND_SOC_DAPM_INPUT("DIFL_2R_3R"),
	SND_SOC_DAPM_INPUT("DIFR_1L_1R"),
	SND_SOC_DAPM_INPUT("DIFR_2L_3L"),
	SND_SOC_DAPM_INPUT("DIFR_2R_3R"),
	SND_SOC_DAPM_INPUT("DMic_L"),
	SND_SOC_DAPM_INPUT("DMic_R"),

	/* Digital audio interface output */
	SND_SOC_DAPM_AIF_OUT("AIF_OUT", "Capture", 0, SND_SOC_NOPM, 0, 0),

	/* Clocks */
	SND_SOC_DAPM_SUPPLY("PLL_CLK", ADC3XXX_PLL_PROG_PR, ADC3XXX_ENABLE_PLL_SHIFT,
			    0, adc3xxx_pll_delay, SND_SOC_DAPM_POST_PMU),

	SND_SOC_DAPM_SUPPLY("ADC_CLK", ADC3XXX_ADC_NADC, ADC3XXX_ENABLE_NADC_SHIFT,
			    0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC_MOD_CLK", ADC3XXX_ADC_MADC, ADC3XXX_ENABLE_MADC_SHIFT,
			    0, NULL, 0),

	/* This refers to the generated BCLK in master mode. */
	SND_SOC_DAPM_SUPPLY("BCLK", ADC3XXX_BCLK_N_DIV, ADC3XXX_ENABLE_BCLK_SHIFT,
			    0, NULL, 0),
};

static const struct snd_soc_dapm_route adc3xxx_intercon[] = {
	/* Left input selection from switches */
	{ "Left Input", "IN_1L Capture Switch", "IN_1L" },
	{ "Left Input", "IN_2L Capture Switch", "IN_2L" },
	{ "Left Input", "IN_3L Capture Switch", "IN_3L" },
	{ "Left Input", "DIF_2L_3L Capture Switch", "DIFL_2L_3L" },
	{ "Left Input", "DIF_1L_1R Capture Switch", "DIFL_1L_1R" },
	{ "Left Input", "DIF_2R_3R Capture Switch", "DIFL_2R_3R" },
	{ "Left Input", "IN_1R Capture Switch", "IN_1R" },

	/* Left input selection to left PGA */
	{ "Left PGA", NULL, "Left Input" },

	/* Left PGA to left ADC */
	{ "Left ADC", NULL, "Left PGA" },

	/* Right input selection from switches */
	{ "Right Input", "IN_1R Capture Switch", "IN_1R" },
	{ "Right Input", "IN_2R Capture Switch", "IN_2R" },
	{ "Right Input", "IN_3R Capture Switch", "IN_3R" },
	{ "Right Input", "DIF_2R_3R Capture Switch", "DIFR_2R_3R" },
	{ "Right Input", "DIF_1L_1R Capture Switch", "DIFR_1L_1R" },
	{ "Right Input", "DIF_2L_3L Capture Switch", "DIFR_2L_3L" },
	{ "Right Input", "IN_1L Capture Switch", "IN_1L" },

	/* Right input selection to right PGA */
	{ "Right PGA", NULL, "Right Input" },

	/* Right PGA to right ADC */
	{ "Right ADC", NULL, "Right PGA" },

	/* Left DMic Input selection from switch */
	{ "Left DMic Input", "Left ADC Capture Switch", "DMic_L" },

	/* Left DMic to left ADC */
	{ "Left ADC", NULL, "Left DMic Input" },

	/* Right DMic Input selection from switch */
	{ "Right DMic Input", "Right ADC Capture Switch", "DMic_R" },

	/* Right DMic to right ADC */
	{ "Right ADC", NULL, "Right DMic Input" },

	/* ADC to AIF output */
	{ "AIF_OUT", NULL, "Left ADC" },
	{ "AIF_OUT", NULL, "Right ADC" },

	/* Clocking */
	{ "ADC_MOD_CLK", NULL, "ADC_CLK" },
	{ "Left ADC", NULL, "ADC_MOD_CLK" },
	{ "Right ADC", NULL, "ADC_MOD_CLK" },

	{ "BCLK", NULL, "ADC_CLK" },
};

static const struct snd_soc_dapm_route adc3xxx_pll_intercon[] = {
	{ "ADC_CLK", NULL, "PLL_CLK" },
};

static const struct snd_soc_dapm_route adc3xxx_bclk_out_intercon[] = {
	{ "AIF_OUT", NULL, "BCLK" }
};

static int adc3xxx_gpio_request(struct gpio_chip *chip, unsigned int offset)
{
	struct adc3xxx *adc3xxx = gpiochip_get_data(chip);

	if (offset >= ADC3XXX_GPIOS_MAX)
		return -EINVAL;

	/* GPIO1 is offset 0, GPIO2 is offset 1 */
	/* We check here that the GPIO pins are either not configured in the
	 * DT, or that they purposely are set as outputs.
	 * (Input mode not yet implemented).
	 */
	if (adc3xxx->gpio_cfg[offset] != 0 &&
	    adc3xxx->gpio_cfg[offset] != ADC3XXX_GPIO_GPO + 1)
		return -EINVAL;

	return 0;
}

static int adc3xxx_gpio_direction_out(struct gpio_chip *chip,
				      unsigned int offset, int value)
{
	struct adc3xxx *adc3xxx = gpiochip_get_data(chip);

	/* Set GPIO output function. */
	return regmap_update_bits(adc3xxx->regmap,
				  adc3xxx_gpio_ctrl_reg[offset],
				  ADC3XXX_GPIO_CTRL_CFG_MASK |
				  ADC3XXX_GPIO_CTRL_OUTPUT_CTRL_MASK,
				  ADC3XXX_GPIO_GPO << ADC3XXX_GPIO_CTRL_CFG_SHIFT |
				  !!value << ADC3XXX_GPIO_CTRL_OUTPUT_CTRL_SHIFT);
}

/* With only GPIO outputs configured, we never get the .direction_out call,
 * so we set the output mode and output value in the same call. Hence
 * .set in practice does the same thing as .direction_out .
 */
static void adc3xxx_gpio_set(struct gpio_chip *chip, unsigned int offset,
			     int value)
{
	(void) adc3xxx_gpio_direction_out(chip, offset, value);
}

/* Even though we only support GPIO output for now, some GPIO clients
 * want to read the current pin state using the .get callback.
 */
static int adc3xxx_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct adc3xxx *adc3xxx = gpiochip_get_data(chip);
	unsigned int regval;
	int ret;

	/* We only allow output pins, so just read the value set in the output
	 * pin register field.
	 */
	ret = regmap_read(adc3xxx->regmap, adc3xxx_gpio_ctrl_reg[offset], &regval);
	if (ret)
		return ret;
	return !!(regval & ADC3XXX_GPIO_CTRL_OUTPUT_CTRL_MASK);
}

static const struct gpio_chip adc3xxx_gpio_chip = {
	.label			= "adc3xxx",
	.owner			= THIS_MODULE,
	.request		= adc3xxx_gpio_request,
	.direction_output	= adc3xxx_gpio_direction_out,
	.set			= adc3xxx_gpio_set,
	.get			= adc3xxx_gpio_get,
	.can_sleep		= 1,
};

static void adc3xxx_free_gpio(struct adc3xxx *adc3xxx)
{
#ifdef CONFIG_GPIOLIB
	gpiochip_remove(&adc3xxx->gpio_chip);
#endif
}

static void adc3xxx_init_gpio(struct adc3xxx *adc3xxx)
{
	int gpio, micbias;
	int ret;

	adc3xxx->gpio_chip = adc3xxx_gpio_chip;
	adc3xxx->gpio_chip.ngpio = ADC3XXX_GPIOS_MAX;
	adc3xxx->gpio_chip.parent = adc3xxx->dev;
	adc3xxx->gpio_chip.base = -1;

	ret = gpiochip_add_data(&adc3xxx->gpio_chip, adc3xxx);
	if (ret)
		dev_err(adc3xxx->dev, "Failed to add gpios: %d\n", ret);

	/* Set up potential GPIO configuration from the devicetree.
	 * This allows us to set up things which are not software
	 * controllable GPIOs, such as PDM microphone I/O,
	 */
	for (gpio = 0; gpio < ADC3XXX_GPIOS_MAX; gpio++) {
		unsigned int cfg = adc3xxx->gpio_cfg[gpio];

		if (cfg) {
			cfg--; /* actual value to use is stored +1 */
			regmap_update_bits(adc3xxx->regmap,
					   adc3xxx_gpio_ctrl_reg[gpio],
					   ADC3XXX_GPIO_CTRL_CFG_MASK,
					   cfg << ADC3XXX_GPIO_CTRL_CFG_SHIFT);
		}
	}

	/* Set up micbias voltage */
	for (micbias = 0; micbias < ADC3XXX_MICBIAS_PINS; micbias++) {
		unsigned int vg = adc3xxx->micbias_vg[micbias];

		regmap_update_bits(adc3xxx->regmap,
				   ADC3XXX_MICBIAS_CTRL,
				   ADC3XXX_MICBIAS_MASK << adc3xxx_micbias_shift[micbias],
				   vg << adc3xxx_micbias_shift[micbias]);
	}
}

static int adc3xxx_parse_dt_gpio(struct adc3xxx *adc3xxx,
				 const char *propname, unsigned int *cfg)
{
	struct device *dev = adc3xxx->dev;
	struct device_node *np = dev->of_node;
	unsigned int val;

	if (!of_property_read_u32(np, propname, &val)) {
		if (val & ~15 || val == 7 || val >= 11) {
			dev_err(dev, "Invalid property value for '%s'\n", propname);
			return -EINVAL;
		}
		if (val == ADC3XXX_GPIO_GPI)
			dev_warn(dev, "GPIO Input read not yet implemented\n");
		*cfg = val + 1; /* 0 => not set up, all others shifted +1 */
	}
	return 0;
}

static int adc3xxx_parse_dt_micbias(struct adc3xxx *adc3xxx,
				    const char *propname, unsigned int *vg)
{
	struct device *dev = adc3xxx->dev;
	struct device_node *np = dev->of_node;
	unsigned int val;

	if (!of_property_read_u32(np, propname, &val)) {
		if (val >= ADC3XXX_MICBIAS_AVDD) {
			dev_err(dev, "Invalid property value for '%s'\n", propname);
			return -EINVAL;
		}
		*vg = val;
	}
	return 0;
}

static int adc3xxx_parse_pll_mode(uint32_t val, unsigned int *pll_mode)
{
	if (val != ADC3XXX_PLL_ENABLE && val != ADC3XXX_PLL_BYPASS &&
	    val != ADC3XXX_PLL_AUTO)
		return -EINVAL;

	*pll_mode = val;

	return 0;
}

static void adc3xxx_setup_pll(struct snd_soc_component *component,
			      int div_entry)
{
	int i = div_entry;

	/* P & R values */
	snd_soc_component_write(component, ADC3XXX_PLL_PROG_PR,
				(adc3xxx_divs[i].pll_p << ADC3XXX_PLLP_SHIFT) |
				(adc3xxx_divs[i].pll_r << ADC3XXX_PLLR_SHIFT));
	/* J value */
	snd_soc_component_write(component, ADC3XXX_PLL_PROG_J,
				adc3xxx_divs[i].pll_j & ADC3XXX_PLLJ_MASK);
	/* D value */
	snd_soc_component_write(component, ADC3XXX_PLL_PROG_D_LSB,
				adc3xxx_divs[i].pll_d & ADC3XXX_PLLD_LSB_MASK);
	snd_soc_component_write(component, ADC3XXX_PLL_PROG_D_MSB,
				(adc3xxx_divs[i].pll_d >> 8) & ADC3XXX_PLLD_MSB_MASK);
}

static int adc3xxx_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(dai->component);
	struct adc3xxx *adc3xxx = snd_soc_component_get_drvdata(component);
	int i, width = 16;
	u8 iface_len, bdiv;

	i = adc3xxx_get_divs(component->dev, adc3xxx->sysclk,
			     params_rate(params), adc3xxx->pll_mode);

	if (i < 0)
		return i;

	/* select data word length */
	switch (params_width(params)) {
	case 16:
		iface_len = ADC3XXX_IFACE_16BITS;
		width = 16;
		break;
	case 20:
		iface_len = ADC3XXX_IFACE_20BITS;
		width = 20;
		break;
	case 24:
		iface_len = ADC3XXX_IFACE_24BITS;
		width = 24;
		break;
	case 32:
		iface_len = ADC3XXX_IFACE_32BITS;
		width = 32;
		break;
	default:
		dev_err(component->dev, "Unsupported serial data format\n");
		return -EINVAL;
	}
	snd_soc_component_update_bits(component, ADC3XXX_INTERFACE_CTRL_1,
				      ADC3XXX_WLENGTH_MASK, iface_len);
	if (adc3xxx_divs[i].pll_p) { /* If PLL used for this mode */
		adc3xxx_setup_pll(component, i);
		snd_soc_component_write(component, ADC3XXX_CLKGEN_MUX, ADC3XXX_USE_PLL);
		if (!adc3xxx->use_pll) {
			snd_soc_dapm_add_routes(dapm, adc3xxx_pll_intercon,
						ARRAY_SIZE(adc3xxx_pll_intercon));
			adc3xxx->use_pll = 1;
		}
	} else {
		snd_soc_component_write(component, ADC3XXX_CLKGEN_MUX, ADC3XXX_NO_PLL);
		if (adc3xxx->use_pll) {
			snd_soc_dapm_del_routes(dapm, adc3xxx_pll_intercon,
						ARRAY_SIZE(adc3xxx_pll_intercon));
			adc3xxx->use_pll = 0;
		}
	}

	/* NADC */
	snd_soc_component_update_bits(component, ADC3XXX_ADC_NADC,
				      ADC3XXX_NADC_MASK, adc3xxx_divs[i].nadc);
	/* MADC */
	snd_soc_component_update_bits(component, ADC3XXX_ADC_MADC,
				      ADC3XXX_MADC_MASK, adc3xxx_divs[i].madc);
	/* AOSR */
	snd_soc_component_update_bits(component, ADC3XXX_ADC_AOSR,
				      ADC3XXX_AOSR_MASK, adc3xxx_divs[i].aosr);
	/* BDIV N Value */
	/* BCLK is (by default) set up to be derived from ADC_CLK */
	bdiv = (adc3xxx_divs[i].aosr * adc3xxx_divs[i].madc) / (2 * width);
	snd_soc_component_update_bits(component, ADC3XXX_BCLK_N_DIV,
				      ADC3XXX_BDIV_MASK, bdiv);

	return 0;
}

static const char *adc3xxx_pll_mode_text(int pll_mode)
{
	switch (pll_mode) {
	case ADC3XXX_PLL_AUTO:
		return "PLL auto";
	case ADC3XXX_PLL_ENABLE:
		return "PLL enable";
	case ADC3XXX_PLL_BYPASS:
		return "PLL bypass";
	default:
		break;
	}

	return "PLL unknown";
}

static int adc3xxx_set_dai_sysclk(struct snd_soc_dai *codec_dai,
				  int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_component *component = codec_dai->component;
	struct adc3xxx *adc3xxx = snd_soc_component_get_drvdata(component);
	int ret;

	ret = adc3xxx_parse_pll_mode(clk_id, &adc3xxx->pll_mode);
	if (ret < 0)
		return ret;

	adc3xxx->sysclk = freq;
	dev_dbg(component->dev, "Set sysclk to %u Hz, %s\n",
		freq, adc3xxx_pll_mode_text(adc3xxx->pll_mode));
	return 0;
}

static int adc3xxx_set_dai_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt)
{
	struct snd_soc_component *component = codec_dai->component;
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);
	struct adc3xxx *adc3xxx = snd_soc_component_get_drvdata(component);
	u8 clkdir = 0, format = 0;
	int master = 0;
	int ret;

	switch (fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_CBP_CFP:
		master = 1;
		clkdir = ADC3XXX_BCLK_MASTER | ADC3XXX_WCLK_MASTER;
		break;
	case SND_SOC_DAIFMT_CBC_CFC:
		master = 0;
		break;
	default:
		dev_err(component->dev, "Invalid DAI clock setup\n");
		return -EINVAL;
	}

	/*
	 * match both interface format and signal polarities since they
	 * are fixed
	 */
	switch (fmt & (SND_SOC_DAIFMT_FORMAT_MASK | SND_SOC_DAIFMT_INV_MASK)) {
	case SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF:
		format = ADC3XXX_FORMAT_I2S;
		break;
	case SND_SOC_DAIFMT_DSP_A | SND_SOC_DAIFMT_IB_NF:
		format = ADC3XXX_FORMAT_DSP;
		break;
	case SND_SOC_DAIFMT_DSP_B | SND_SOC_DAIFMT_IB_NF:
		format = ADC3XXX_FORMAT_DSP;
		break;
	case SND_SOC_DAIFMT_RIGHT_J | SND_SOC_DAIFMT_NB_NF:
		format = ADC3XXX_FORMAT_RJF;
		break;
	case SND_SOC_DAIFMT_LEFT_J | SND_SOC_DAIFMT_NB_NF:
		format = ADC3XXX_FORMAT_LJF;
		break;
	default:
		dev_err(component->dev, "Invalid DAI format\n");
		return -EINVAL;
	}

	/* Add/del route enabling BCLK output as applicable */
	if (master && !adc3xxx->master)
		snd_soc_dapm_add_routes(dapm, adc3xxx_bclk_out_intercon,
					ARRAY_SIZE(adc3xxx_bclk_out_intercon));
	else if (!master && adc3xxx->master)
		snd_soc_dapm_del_routes(dapm, adc3xxx_bclk_out_intercon,
					ARRAY_SIZE(adc3xxx_bclk_out_intercon));
	adc3xxx->master = master;

	/* set clock direction and format */
	ret = snd_soc_component_update_bits(component,
					    ADC3XXX_INTERFACE_CTRL_1,
					    ADC3XXX_CLKDIR_MASK | ADC3XXX_FORMAT_MASK,
					    clkdir | format);
	if (ret < 0)
		return ret;
	return 0;
}

static const struct snd_soc_dai_ops adc3xxx_dai_ops = {
	.hw_params	= adc3xxx_hw_params,
	.set_sysclk	= adc3xxx_set_dai_sysclk,
	.set_fmt	= adc3xxx_set_dai_fmt,
};

static struct snd_soc_dai_driver adc3xxx_dai = {
	.name = "tlv320adc3xxx-hifi",
	.capture = {
		    .stream_name = "Capture",
		    .channels_min = 1,
		    .channels_max = 2,
		    .rates = ADC3XXX_RATES,
		    .formats = ADC3XXX_FORMATS,
		   },
	.ops = &adc3xxx_dai_ops,
};

static const struct snd_soc_component_driver soc_component_dev_adc3xxx = {
	.controls		= adc3xxx_snd_controls,
	.num_controls		= ARRAY_SIZE(adc3xxx_snd_controls),
	.dapm_widgets		= adc3xxx_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(adc3xxx_dapm_widgets),
	.dapm_routes		= adc3xxx_intercon,
	.num_dapm_routes	= ARRAY_SIZE(adc3xxx_intercon),
	.endianness		= 1,
};

static const struct i2c_device_id adc3xxx_i2c_id[] = {
	{ "tlv320adc3001", ADC3001 },
	{ "tlv320adc3101", ADC3101 },
	{}
};
MODULE_DEVICE_TABLE(i2c, adc3xxx_i2c_id);

static int adc3xxx_i2c_probe(struct i2c_client *i2c)
{
	struct device *dev = &i2c->dev;
	struct adc3xxx *adc3xxx = NULL;
	const struct i2c_device_id *id;
	int ret;

	adc3xxx = devm_kzalloc(dev, sizeof(struct adc3xxx), GFP_KERNEL);
	if (!adc3xxx)
		return -ENOMEM;
	adc3xxx->dev = dev;

	adc3xxx->rst_pin = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(adc3xxx->rst_pin)) {
		return dev_err_probe(dev, PTR_ERR(adc3xxx->rst_pin),
				     "Failed to request rst_pin\n");
	}

	adc3xxx->mclk = devm_clk_get(dev, NULL);
	if (IS_ERR(adc3xxx->mclk)) {
		/*
		 * The chip itself supports running off the BCLK either
		 * directly or via the PLL, but the driver does not (yet), so
		 * having a specified mclk is required. Otherwise, we could
		 * use the lack of a clocks property to indicate when BCLK is
		 * intended as the clock source.
		 */
		return dev_err_probe(dev, PTR_ERR(adc3xxx->mclk),
				     "Failed to acquire MCLK\n");
	} else if (adc3xxx->mclk) {
		ret = clk_prepare_enable(adc3xxx->mclk);
		if (ret < 0)
			return ret;
		dev_dbg(dev, "Enabled MCLK, freq %lu Hz\n", clk_get_rate(adc3xxx->mclk));
	}

	ret = adc3xxx_parse_dt_gpio(adc3xxx, "ti,dmdin-gpio1", &adc3xxx->gpio_cfg[0]);
	if (ret < 0)
		goto err_unprepare_mclk;
	ret = adc3xxx_parse_dt_gpio(adc3xxx, "ti,dmclk-gpio2", &adc3xxx->gpio_cfg[1]);
	if (ret < 0)
		goto err_unprepare_mclk;
	ret = adc3xxx_parse_dt_micbias(adc3xxx, "ti,micbias1-vg", &adc3xxx->micbias_vg[0]);
	if (ret < 0)
		goto err_unprepare_mclk;
	ret = adc3xxx_parse_dt_micbias(adc3xxx, "ti,micbias2-vg", &adc3xxx->micbias_vg[1]);
	if (ret < 0)
		goto err_unprepare_mclk;

	adc3xxx->regmap = devm_regmap_init_i2c(i2c, &adc3xxx_regmap);
	if (IS_ERR(adc3xxx->regmap)) {
		ret = PTR_ERR(adc3xxx->regmap);
		goto err_unprepare_mclk;
	}

	i2c_set_clientdata(i2c, adc3xxx);

	id = i2c_match_id(adc3xxx_i2c_id, i2c);
	adc3xxx->type = id->driver_data;

	/* Reset codec chip */
	gpiod_set_value_cansleep(adc3xxx->rst_pin, 1);
	usleep_range(2000, 100000); /* Requirement: > 10 ns (datasheet p13) */
	gpiod_set_value_cansleep(adc3xxx->rst_pin, 0);

	/* Potentially set up pins used as GPIOs */
	adc3xxx_init_gpio(adc3xxx);

	ret = snd_soc_register_component(dev,
			&soc_component_dev_adc3xxx, &adc3xxx_dai, 1);
	if (ret < 0) {
		dev_err(dev, "Failed to register codec: %d\n", ret);
		goto err_unprepare_mclk;
	}

	return 0;

err_unprepare_mclk:
	clk_disable_unprepare(adc3xxx->mclk);
	return ret;
}

static void __exit adc3xxx_i2c_remove(struct i2c_client *client)
{
	struct adc3xxx *adc3xxx = i2c_get_clientdata(client);

	if (adc3xxx->mclk)
		clk_disable_unprepare(adc3xxx->mclk);
	adc3xxx_free_gpio(adc3xxx);
	snd_soc_unregister_component(&client->dev);
}

static const struct of_device_id tlv320adc3xxx_of_match[] = {
	{ .compatible = "ti,tlv320adc3001", },
	{ .compatible = "ti,tlv320adc3101", },
	{},
};
MODULE_DEVICE_TABLE(of, tlv320adc3xxx_of_match);

static struct i2c_driver adc3xxx_i2c_driver = {
	.driver = {
		   .name = "tlv320adc3xxx-codec",
		   .of_match_table = tlv320adc3xxx_of_match,
		  },
	.probe = adc3xxx_i2c_probe,
	.remove = __exit_p(adc3xxx_i2c_remove),
	.id_table = adc3xxx_i2c_id,
};

module_i2c_driver(adc3xxx_i2c_driver);

MODULE_DESCRIPTION("ASoC TLV320ADC3xxx codec driver");
MODULE_AUTHOR("shahina.s@mistralsolutions.com");
MODULE_LICENSE("GPL v2");
