/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ADAU17X1_H__
#define __ADAU17X1_H__

#include <linux/regmap.h>
#include <linux/platform_data/adau17x1.h>

#include "sigmadsp.h"

enum adau17x1_type {
	ADAU1361,
	ADAU1761,
	ADAU1761_AS_1361,
	ADAU1381,
	ADAU1781,
};

enum adau17x1_pll {
	ADAU17X1_PLL,
};

enum adau17x1_pll_src {
	ADAU17X1_PLL_SRC_MCLK,
};

enum adau17x1_clk_src {
	/* Automatically configure PLL based on the sample rate */
	ADAU17X1_CLK_SRC_PLL_AUTO,
	ADAU17X1_CLK_SRC_MCLK,
	ADAU17X1_CLK_SRC_PLL,
};

struct clk;

struct adau {
	unsigned int sysclk;
	unsigned int pll_freq;
	struct clk *mclk;

	enum adau17x1_clk_src clk_src;
	enum adau17x1_type type;
	void (*switch_mode)(struct device *dev);

	unsigned int dai_fmt;

	uint8_t pll_regs[6];

	bool master;

	unsigned int tdm_slot[2];
	bool dsp_bypass[2];

	struct regmap *regmap;
	struct sigmadsp *sigmadsp;
};

int adau17x1_add_widgets(struct snd_soc_component *component);
int adau17x1_add_routes(struct snd_soc_component *component);
int adau17x1_probe(struct device *dev, struct regmap *regmap,
	enum adau17x1_type type, void (*switch_mode)(struct device *dev),
	const char *firmware_name);
void adau17x1_remove(struct device *dev);
int adau17x1_set_micbias_voltage(struct snd_soc_component *component,
	enum adau17x1_micbias_voltage micbias);
bool adau17x1_readable_register(struct device *dev, unsigned int reg);
bool adau17x1_volatile_register(struct device *dev, unsigned int reg);
bool adau17x1_precious_register(struct device *dev, unsigned int reg);
int adau17x1_resume(struct snd_soc_component *component);

extern const struct snd_soc_dai_ops adau17x1_dai_ops;

#define ADAU17X1_CLOCK_CONTROL			0x4000
#define ADAU17X1_PLL_CONTROL			0x4002
#define ADAU17X1_REC_POWER_MGMT			0x4009
#define ADAU17X1_MICBIAS			0x4010
#define ADAU17X1_SERIAL_PORT0			0x4015
#define ADAU17X1_SERIAL_PORT1			0x4016
#define ADAU17X1_CONVERTER0			0x4017
#define ADAU17X1_CONVERTER1			0x4018
#define ADAU17X1_LEFT_INPUT_DIGITAL_VOL		0x401a
#define ADAU17X1_RIGHT_INPUT_DIGITAL_VOL	0x401b
#define ADAU17X1_ADC_CONTROL			0x4019
#define ADAU17X1_PLAY_POWER_MGMT		0x4029
#define ADAU17X1_DAC_CONTROL0			0x402a
#define ADAU17X1_DAC_CONTROL1			0x402b
#define ADAU17X1_DAC_CONTROL2			0x402c
#define ADAU17X1_SERIAL_PORT_PAD		0x402d
#define ADAU17X1_CONTROL_PORT_PAD0		0x402f
#define ADAU17X1_CONTROL_PORT_PAD1		0x4030
#define ADAU17X1_DSP_SAMPLING_RATE		0x40eb
#define ADAU17X1_SERIAL_INPUT_ROUTE		0x40f2
#define ADAU17X1_SERIAL_OUTPUT_ROUTE		0x40f3
#define ADAU17X1_DSP_ENABLE			0x40f5
#define ADAU17X1_DSP_RUN			0x40f6
#define ADAU17X1_SERIAL_SAMPLING_RATE		0x40f8

#define ADAU17X1_SERIAL_PORT0_BCLK_POL		BIT(4)
#define ADAU17X1_SERIAL_PORT0_LRCLK_POL		BIT(3)
#define ADAU17X1_SERIAL_PORT0_MASTER		BIT(0)

#define ADAU17X1_SERIAL_PORT1_DELAY1		0x00
#define ADAU17X1_SERIAL_PORT1_DELAY0		0x01
#define ADAU17X1_SERIAL_PORT1_DELAY8		0x02
#define ADAU17X1_SERIAL_PORT1_DELAY16		0x03
#define ADAU17X1_SERIAL_PORT1_DELAY_MASK	0x03

#define ADAU17X1_CLOCK_CONTROL_INFREQ_MASK	0x6
#define ADAU17X1_CLOCK_CONTROL_CORECLK_SRC_PLL	BIT(3)
#define ADAU17X1_CLOCK_CONTROL_SYSCLK_EN	BIT(0)

#define ADAU17X1_SERIAL_PORT1_BCLK64		(0x0 << 5)
#define ADAU17X1_SERIAL_PORT1_BCLK32		(0x1 << 5)
#define ADAU17X1_SERIAL_PORT1_BCLK48		(0x2 << 5)
#define ADAU17X1_SERIAL_PORT1_BCLK128		(0x3 << 5)
#define ADAU17X1_SERIAL_PORT1_BCLK256		(0x4 << 5)
#define ADAU17X1_SERIAL_PORT1_BCLK_MASK		(0x7 << 5)

#define ADAU17X1_SERIAL_PORT0_STEREO		(0x0 << 1)
#define ADAU17X1_SERIAL_PORT0_TDM4		(0x1 << 1)
#define ADAU17X1_SERIAL_PORT0_TDM8		(0x2 << 1)
#define ADAU17X1_SERIAL_PORT0_TDM_MASK		(0x3 << 1)
#define ADAU17X1_SERIAL_PORT0_PULSE_MODE	BIT(5)

#define ADAU17X1_CONVERTER0_DAC_PAIR(x)		(((x) - 1) << 5)
#define ADAU17X1_CONVERTER0_DAC_PAIR_MASK	(0x3 << 5)
#define ADAU17X1_CONVERTER1_ADC_PAIR(x)		((x) - 1)
#define ADAU17X1_CONVERTER1_ADC_PAIR_MASK	0x3

#define ADAU17X1_CONVERTER0_CONVSR_MASK		0x7

#define ADAU17X1_CONVERTER0_ADOSR		BIT(3)


#endif
