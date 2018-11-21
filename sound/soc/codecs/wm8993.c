/*
 * wm8993.c -- WM8993 ALSA SoC audio driver
 *
 * Copyright 2009-12 Wolfson Microelectronics plc
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/tlv.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/wm8993.h>

#include "wm8993.h"
#include "wm_hubs.h"

#define WM8993_NUM_SUPPLIES 6
static const char *wm8993_supply_names[WM8993_NUM_SUPPLIES] = {
	"DCVDD",
	"DBVDD",
	"AVDD1",
	"AVDD2",
	"CPVDD",
	"SPKVDD",
};

static const struct reg_default wm8993_reg_defaults[] = {
	{ 1,   0x0000 },     /* R1   - Power Management (1) */
	{ 2,   0x6000 },     /* R2   - Power Management (2) */
	{ 3,   0x0000 },     /* R3   - Power Management (3) */
	{ 4,   0x4050 },     /* R4   - Audio Interface (1) */
	{ 5,   0x4000 },     /* R5   - Audio Interface (2) */
	{ 6,   0x01C8 },     /* R6   - Clocking 1 */
	{ 7,   0x0000 },     /* R7   - Clocking 2 */
	{ 8,   0x0000 },     /* R8   - Audio Interface (3) */
	{ 9,   0x0040 },     /* R9   - Audio Interface (4) */
	{ 10,  0x0004 },     /* R10  - DAC CTRL */
	{ 11,  0x00C0 },     /* R11  - Left DAC Digital Volume */
	{ 12,  0x00C0 },     /* R12  - Right DAC Digital Volume */
	{ 13,  0x0000 },     /* R13  - Digital Side Tone */
	{ 14,  0x0300 },     /* R14  - ADC CTRL */
	{ 15,  0x00C0 },     /* R15  - Left ADC Digital Volume */
	{ 16,  0x00C0 },     /* R16  - Right ADC Digital Volume */
	{ 18,  0x0000 },     /* R18  - GPIO CTRL 1 */
	{ 19,  0x0010 },     /* R19  - GPIO1 */
	{ 20,  0x0000 },     /* R20  - IRQ_DEBOUNCE */
	{ 21,  0x0000 },     /* R21  - Inputs Clamp */
	{ 22,  0x8000 },     /* R22  - GPIOCTRL 2 */
	{ 23,  0x0800 },     /* R23  - GPIO_POL */
	{ 24,  0x008B },     /* R24  - Left Line Input 1&2 Volume */
	{ 25,  0x008B },     /* R25  - Left Line Input 3&4 Volume */
	{ 26,  0x008B },     /* R26  - Right Line Input 1&2 Volume */
	{ 27,  0x008B },     /* R27  - Right Line Input 3&4 Volume */
	{ 28,  0x006D },     /* R28  - Left Output Volume */
	{ 29,  0x006D },     /* R29  - Right Output Volume */
	{ 30,  0x0066 },     /* R30  - Line Outputs Volume */
	{ 31,  0x0020 },     /* R31  - HPOUT2 Volume */
	{ 32,  0x0079 },     /* R32  - Left OPGA Volume */
	{ 33,  0x0079 },     /* R33  - Right OPGA Volume */
	{ 34,  0x0003 },     /* R34  - SPKMIXL Attenuation */
	{ 35,  0x0003 },     /* R35  - SPKMIXR Attenuation */
	{ 36,  0x0011 },     /* R36  - SPKOUT Mixers */
	{ 37,  0x0100 },     /* R37  - SPKOUT Boost */
	{ 38,  0x0079 },     /* R38  - Speaker Volume Left */
	{ 39,  0x0079 },     /* R39  - Speaker Volume Right */
	{ 40,  0x0000 },     /* R40  - Input Mixer2 */
	{ 41,  0x0000 },     /* R41  - Input Mixer3 */
	{ 42,  0x0000 },     /* R42  - Input Mixer4 */
	{ 43,  0x0000 },     /* R43  - Input Mixer5 */
	{ 44,  0x0000 },     /* R44  - Input Mixer6 */
	{ 45,  0x0000 },     /* R45  - Output Mixer1 */
	{ 46,  0x0000 },     /* R46  - Output Mixer2 */
	{ 47,  0x0000 },     /* R47  - Output Mixer3 */
	{ 48,  0x0000 },     /* R48  - Output Mixer4 */
	{ 49,  0x0000 },     /* R49  - Output Mixer5 */
	{ 50,  0x0000 },     /* R50  - Output Mixer6 */
	{ 51,  0x0000 },     /* R51  - HPOUT2 Mixer */
	{ 52,  0x0000 },     /* R52  - Line Mixer1 */
	{ 53,  0x0000 },     /* R53  - Line Mixer2 */
	{ 54,  0x0000 },     /* R54  - Speaker Mixer */
	{ 55,  0x0000 },     /* R55  - Additional Control */
	{ 56,  0x0000 },     /* R56  - AntiPOP1 */
	{ 57,  0x0000 },     /* R57  - AntiPOP2 */
	{ 58,  0x0000 },     /* R58  - MICBIAS */
	{ 60,  0x0000 },     /* R60  - FLL Control 1 */
	{ 61,  0x0000 },     /* R61  - FLL Control 2 */
	{ 62,  0x0000 },     /* R62  - FLL Control 3 */
	{ 63,  0x2EE0 },     /* R63  - FLL Control 4 */
	{ 64,  0x0002 },     /* R64  - FLL Control 5 */
	{ 65,  0x2287 },     /* R65  - Clocking 3 */
	{ 66,  0x025F },     /* R66  - Clocking 4 */
	{ 67,  0x0000 },     /* R67  - MW Slave Control */
	{ 69,  0x0002 },     /* R69  - Bus Control 1 */
	{ 70,  0x0000 },     /* R70  - Write Sequencer 0 */
	{ 71,  0x0000 },     /* R71  - Write Sequencer 1 */
	{ 72,  0x0000 },     /* R72  - Write Sequencer 2 */
	{ 73,  0x0000 },     /* R73  - Write Sequencer 3 */
	{ 74,  0x0000 },     /* R74  - Write Sequencer 4 */
	{ 75,  0x0000 },     /* R75  - Write Sequencer 5 */
	{ 76,  0x1F25 },     /* R76  - Charge Pump 1 */
	{ 81,  0x0000 },     /* R81  - Class W 0 */
	{ 85,  0x054A },     /* R85  - DC Servo 1 */
	{ 87,  0x0000 },     /* R87  - DC Servo 3 */
	{ 96,  0x0100 },     /* R96  - Analogue HP 0 */
	{ 98,  0x0000 },     /* R98  - EQ1 */
	{ 99,  0x000C },     /* R99  - EQ2 */
	{ 100, 0x000C },     /* R100 - EQ3 */
	{ 101, 0x000C },     /* R101 - EQ4 */
	{ 102, 0x000C },     /* R102 - EQ5 */
	{ 103, 0x000C },     /* R103 - EQ6 */
	{ 104, 0x0FCA },     /* R104 - EQ7 */
	{ 105, 0x0400 },     /* R105 - EQ8 */
	{ 106, 0x00D8 },     /* R106 - EQ9 */
	{ 107, 0x1EB5 },     /* R107 - EQ10 */
	{ 108, 0xF145 },     /* R108 - EQ11 */
	{ 109, 0x0B75 },     /* R109 - EQ12 */
	{ 110, 0x01C5 },     /* R110 - EQ13 */
	{ 111, 0x1C58 },     /* R111 - EQ14 */
	{ 112, 0xF373 },     /* R112 - EQ15 */
	{ 113, 0x0A54 },     /* R113 - EQ16 */
	{ 114, 0x0558 },     /* R114 - EQ17 */
	{ 115, 0x168E },     /* R115 - EQ18 */
	{ 116, 0xF829 },     /* R116 - EQ19 */
	{ 117, 0x07AD },     /* R117 - EQ20 */
	{ 118, 0x1103 },     /* R118 - EQ21 */
	{ 119, 0x0564 },     /* R119 - EQ22 */
	{ 120, 0x0559 },     /* R120 - EQ23 */
	{ 121, 0x4000 },     /* R121 - EQ24 */
	{ 122, 0x0000 },     /* R122 - Digital Pulls */
	{ 123, 0x0F08 },     /* R123 - DRC Control 1 */
	{ 124, 0x0000 },     /* R124 - DRC Control 2 */
	{ 125, 0x0080 },     /* R125 - DRC Control 3 */
	{ 126, 0x0000 },     /* R126 - DRC Control 4 */
};

static struct {
	int ratio;
	int clk_sys_rate;
} clk_sys_rates[] = {
	{ 64,   0 },
	{ 128,  1 },
	{ 192,  2 },
	{ 256,  3 },
	{ 384,  4 },
	{ 512,  5 },
	{ 768,  6 },
	{ 1024, 7 },
	{ 1408, 8 },
	{ 1536, 9 },
};

static struct {
	int rate;
	int sample_rate;
} sample_rates[] = {
	{ 8000,  0  },
	{ 11025, 1  },
	{ 12000, 1  },
	{ 16000, 2  },
	{ 22050, 3  },
	{ 24000, 3  },
	{ 32000, 4  },
	{ 44100, 5  },
	{ 48000, 5  },
};

static struct {
	int div; /* *10 due to .5s */
	int bclk_div;
} bclk_divs[] = {
	{ 10,  0  },
	{ 15,  1  },
	{ 20,  2  },
	{ 30,  3  },
	{ 40,  4  },
	{ 55,  5  },
	{ 60,  6  },
	{ 80,  7  },
	{ 110, 8  },
	{ 120, 9  },
	{ 160, 10 },
	{ 220, 11 },
	{ 240, 12 },
	{ 320, 13 },
	{ 440, 14 },
	{ 480, 15 },
};

struct wm8993_priv {
	struct wm_hubs_data hubs_data;
	struct device *dev;
	struct regmap *regmap;
	struct regulator_bulk_data supplies[WM8993_NUM_SUPPLIES];
	struct wm8993_platform_data pdata;
	struct completion fll_lock;
	int master;
	int sysclk_source;
	int tdm_slots;
	int tdm_width;
	unsigned int mclk_rate;
	unsigned int sysclk_rate;
	unsigned int fs;
	unsigned int bclk;
	unsigned int fll_fref;
	unsigned int fll_fout;
	int fll_src;
};

static bool wm8993_volatile(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case WM8993_SOFTWARE_RESET:
	case WM8993_GPIO_CTRL_1:
	case WM8993_DC_SERVO_0:
	case WM8993_DC_SERVO_READBACK_0:
	case WM8993_DC_SERVO_READBACK_1:
	case WM8993_DC_SERVO_READBACK_2:
		return true;
	default:
		return false;
	}
}

static bool wm8993_readable(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case WM8993_SOFTWARE_RESET:
	case WM8993_POWER_MANAGEMENT_1:
	case WM8993_POWER_MANAGEMENT_2:
	case WM8993_POWER_MANAGEMENT_3:
	case WM8993_AUDIO_INTERFACE_1:
	case WM8993_AUDIO_INTERFACE_2:
	case WM8993_CLOCKING_1:
	case WM8993_CLOCKING_2:
	case WM8993_AUDIO_INTERFACE_3:
	case WM8993_AUDIO_INTERFACE_4:
	case WM8993_DAC_CTRL:
	case WM8993_LEFT_DAC_DIGITAL_VOLUME:
	case WM8993_RIGHT_DAC_DIGITAL_VOLUME:
	case WM8993_DIGITAL_SIDE_TONE:
	case WM8993_ADC_CTRL:
	case WM8993_LEFT_ADC_DIGITAL_VOLUME:
	case WM8993_RIGHT_ADC_DIGITAL_VOLUME:
	case WM8993_GPIO_CTRL_1:
	case WM8993_GPIO1:
	case WM8993_IRQ_DEBOUNCE:
	case WM8993_GPIOCTRL_2:
	case WM8993_GPIO_POL:
	case WM8993_LEFT_LINE_INPUT_1_2_VOLUME:
	case WM8993_LEFT_LINE_INPUT_3_4_VOLUME:
	case WM8993_RIGHT_LINE_INPUT_1_2_VOLUME:
	case WM8993_RIGHT_LINE_INPUT_3_4_VOLUME:
	case WM8993_LEFT_OUTPUT_VOLUME:
	case WM8993_RIGHT_OUTPUT_VOLUME:
	case WM8993_LINE_OUTPUTS_VOLUME:
	case WM8993_HPOUT2_VOLUME:
	case WM8993_LEFT_OPGA_VOLUME:
	case WM8993_RIGHT_OPGA_VOLUME:
	case WM8993_SPKMIXL_ATTENUATION:
	case WM8993_SPKMIXR_ATTENUATION:
	case WM8993_SPKOUT_MIXERS:
	case WM8993_SPKOUT_BOOST:
	case WM8993_SPEAKER_VOLUME_LEFT:
	case WM8993_SPEAKER_VOLUME_RIGHT:
	case WM8993_INPUT_MIXER2:
	case WM8993_INPUT_MIXER3:
	case WM8993_INPUT_MIXER4:
	case WM8993_INPUT_MIXER5:
	case WM8993_INPUT_MIXER6:
	case WM8993_OUTPUT_MIXER1:
	case WM8993_OUTPUT_MIXER2:
	case WM8993_OUTPUT_MIXER3:
	case WM8993_OUTPUT_MIXER4:
	case WM8993_OUTPUT_MIXER5:
	case WM8993_OUTPUT_MIXER6:
	case WM8993_HPOUT2_MIXER:
	case WM8993_LINE_MIXER1:
	case WM8993_LINE_MIXER2:
	case WM8993_SPEAKER_MIXER:
	case WM8993_ADDITIONAL_CONTROL:
	case WM8993_ANTIPOP1:
	case WM8993_ANTIPOP2:
	case WM8993_MICBIAS:
	case WM8993_FLL_CONTROL_1:
	case WM8993_FLL_CONTROL_2:
	case WM8993_FLL_CONTROL_3:
	case WM8993_FLL_CONTROL_4:
	case WM8993_FLL_CONTROL_5:
	case WM8993_CLOCKING_3:
	case WM8993_CLOCKING_4:
	case WM8993_MW_SLAVE_CONTROL:
	case WM8993_BUS_CONTROL_1:
	case WM8993_WRITE_SEQUENCER_0:
	case WM8993_WRITE_SEQUENCER_1:
	case WM8993_WRITE_SEQUENCER_2:
	case WM8993_WRITE_SEQUENCER_3:
	case WM8993_WRITE_SEQUENCER_4:
	case WM8993_WRITE_SEQUENCER_5:
	case WM8993_CHARGE_PUMP_1:
	case WM8993_CLASS_W_0:
	case WM8993_DC_SERVO_0:
	case WM8993_DC_SERVO_1:
	case WM8993_DC_SERVO_3:
	case WM8993_DC_SERVO_READBACK_0:
	case WM8993_DC_SERVO_READBACK_1:
	case WM8993_DC_SERVO_READBACK_2:
	case WM8993_ANALOGUE_HP_0:
	case WM8993_EQ1:
	case WM8993_EQ2:
	case WM8993_EQ3:
	case WM8993_EQ4:
	case WM8993_EQ5:
	case WM8993_EQ6:
	case WM8993_EQ7:
	case WM8993_EQ8:
	case WM8993_EQ9:
	case WM8993_EQ10:
	case WM8993_EQ11:
	case WM8993_EQ12:
	case WM8993_EQ13:
	case WM8993_EQ14:
	case WM8993_EQ15:
	case WM8993_EQ16:
	case WM8993_EQ17:
	case WM8993_EQ18:
	case WM8993_EQ19:
	case WM8993_EQ20:
	case WM8993_EQ21:
	case WM8993_EQ22:
	case WM8993_EQ23:
	case WM8993_EQ24:
	case WM8993_DIGITAL_PULLS:
	case WM8993_DRC_CONTROL_1:
	case WM8993_DRC_CONTROL_2:
	case WM8993_DRC_CONTROL_3:
	case WM8993_DRC_CONTROL_4:
		return true;
	default:
		return false;
	}
}

struct _fll_div {
	u16 fll_fratio;
	u16 fll_outdiv;
	u16 fll_clk_ref_div;
	u16 n;
	u16 k;
};

/* The size in bits of the FLL divide multiplied by 10
 * to allow rounding later */
#define FIXED_FLL_SIZE ((1 << 16) * 10)

static struct {
	unsigned int min;
	unsigned int max;
	u16 fll_fratio;
	int ratio;
} fll_fratios[] = {
	{       0,    64000, 4, 16 },
	{   64000,   128000, 3,  8 },
	{  128000,   256000, 2,  4 },
	{  256000,  1000000, 1,  2 },
	{ 1000000, 13500000, 0,  1 },
};

static int fll_factors(struct _fll_div *fll_div, unsigned int Fref,
		       unsigned int Fout)
{
	u64 Kpart;
	unsigned int K, Ndiv, Nmod, target;
	unsigned int div;
	int i;

	/* Fref must be <=13.5MHz */
	div = 1;
	fll_div->fll_clk_ref_div = 0;
	while ((Fref / div) > 13500000) {
		div *= 2;
		fll_div->fll_clk_ref_div++;

		if (div > 8) {
			pr_err("Can't scale %dMHz input down to <=13.5MHz\n",
			       Fref);
			return -EINVAL;
		}
	}

	pr_debug("Fref=%u Fout=%u\n", Fref, Fout);

	/* Apply the division for our remaining calculations */
	Fref /= div;

	/* Fvco should be 90-100MHz; don't check the upper bound */
	div = 0;
	target = Fout * 2;
	while (target < 90000000) {
		div++;
		target *= 2;
		if (div > 7) {
			pr_err("Unable to find FLL_OUTDIV for Fout=%uHz\n",
			       Fout);
			return -EINVAL;
		}
	}
	fll_div->fll_outdiv = div;

	pr_debug("Fvco=%dHz\n", target);

	/* Find an appropriate FLL_FRATIO and factor it out of the target */
	for (i = 0; i < ARRAY_SIZE(fll_fratios); i++) {
		if (fll_fratios[i].min <= Fref && Fref <= fll_fratios[i].max) {
			fll_div->fll_fratio = fll_fratios[i].fll_fratio;
			target /= fll_fratios[i].ratio;
			break;
		}
	}
	if (i == ARRAY_SIZE(fll_fratios)) {
		pr_err("Unable to find FLL_FRATIO for Fref=%uHz\n", Fref);
		return -EINVAL;
	}

	/* Now, calculate N.K */
	Ndiv = target / Fref;

	fll_div->n = Ndiv;
	Nmod = target % Fref;
	pr_debug("Nmod=%d\n", Nmod);

	/* Calculate fractional part - scale up so we can round. */
	Kpart = FIXED_FLL_SIZE * (long long)Nmod;

	do_div(Kpart, Fref);

	K = Kpart & 0xFFFFFFFF;

	if ((K % 10) >= 5)
		K += 5;

	/* Move down to proper range now rounding is done */
	fll_div->k = K / 10;

	pr_debug("N=%x K=%x FLL_FRATIO=%x FLL_OUTDIV=%x FLL_CLK_REF_DIV=%x\n",
		 fll_div->n, fll_div->k,
		 fll_div->fll_fratio, fll_div->fll_outdiv,
		 fll_div->fll_clk_ref_div);

	return 0;
}

static int _wm8993_set_fll(struct snd_soc_component *component, int fll_id, int source,
			  unsigned int Fref, unsigned int Fout)
{
	struct wm8993_priv *wm8993 = snd_soc_component_get_drvdata(component);
	struct i2c_client *i2c = to_i2c_client(component->dev);
	u16 reg1, reg4, reg5;
	struct _fll_div fll_div;
	unsigned int timeout;
	int ret;

	/* Any change? */
	if (Fref == wm8993->fll_fref && Fout == wm8993->fll_fout)
		return 0;

	/* Disable the FLL */
	if (Fout == 0) {
		dev_dbg(component->dev, "FLL disabled\n");
		wm8993->fll_fref = 0;
		wm8993->fll_fout = 0;

		reg1 = snd_soc_component_read32(component, WM8993_FLL_CONTROL_1);
		reg1 &= ~WM8993_FLL_ENA;
		snd_soc_component_write(component, WM8993_FLL_CONTROL_1, reg1);

		return 0;
	}

	ret = fll_factors(&fll_div, Fref, Fout);
	if (ret != 0)
		return ret;

	reg5 = snd_soc_component_read32(component, WM8993_FLL_CONTROL_5);
	reg5 &= ~WM8993_FLL_CLK_SRC_MASK;

	switch (fll_id) {
	case WM8993_FLL_MCLK:
		break;

	case WM8993_FLL_LRCLK:
		reg5 |= 1;
		break;

	case WM8993_FLL_BCLK:
		reg5 |= 2;
		break;

	default:
		dev_err(component->dev, "Unknown FLL ID %d\n", fll_id);
		return -EINVAL;
	}

	/* Any FLL configuration change requires that the FLL be
	 * disabled first. */
	reg1 = snd_soc_component_read32(component, WM8993_FLL_CONTROL_1);
	reg1 &= ~WM8993_FLL_ENA;
	snd_soc_component_write(component, WM8993_FLL_CONTROL_1, reg1);

	/* Apply the configuration */
	if (fll_div.k)
		reg1 |= WM8993_FLL_FRAC_MASK;
	else
		reg1 &= ~WM8993_FLL_FRAC_MASK;
	snd_soc_component_write(component, WM8993_FLL_CONTROL_1, reg1);

	snd_soc_component_write(component, WM8993_FLL_CONTROL_2,
		      (fll_div.fll_outdiv << WM8993_FLL_OUTDIV_SHIFT) |
		      (fll_div.fll_fratio << WM8993_FLL_FRATIO_SHIFT));
	snd_soc_component_write(component, WM8993_FLL_CONTROL_3, fll_div.k);

	reg4 = snd_soc_component_read32(component, WM8993_FLL_CONTROL_4);
	reg4 &= ~WM8993_FLL_N_MASK;
	reg4 |= fll_div.n << WM8993_FLL_N_SHIFT;
	snd_soc_component_write(component, WM8993_FLL_CONTROL_4, reg4);

	reg5 &= ~WM8993_FLL_CLK_REF_DIV_MASK;
	reg5 |= fll_div.fll_clk_ref_div << WM8993_FLL_CLK_REF_DIV_SHIFT;
	snd_soc_component_write(component, WM8993_FLL_CONTROL_5, reg5);

	/* If we've got an interrupt wired up make sure we get it */
	if (i2c->irq)
		timeout = msecs_to_jiffies(20);
	else if (Fref < 1000000)
		timeout = msecs_to_jiffies(3);
	else
		timeout = msecs_to_jiffies(1);

	try_wait_for_completion(&wm8993->fll_lock);

	/* Enable the FLL */
	snd_soc_component_write(component, WM8993_FLL_CONTROL_1, reg1 | WM8993_FLL_ENA);

	timeout = wait_for_completion_timeout(&wm8993->fll_lock, timeout);
	if (i2c->irq && !timeout)
		dev_warn(component->dev, "Timed out waiting for FLL\n");

	dev_dbg(component->dev, "FLL enabled at %dHz->%dHz\n", Fref, Fout);

	wm8993->fll_fref = Fref;
	wm8993->fll_fout = Fout;
	wm8993->fll_src = source;

	return 0;
}

static int wm8993_set_fll(struct snd_soc_dai *dai, int fll_id, int source,
			  unsigned int Fref, unsigned int Fout)
{
	return _wm8993_set_fll(dai->component, fll_id, source, Fref, Fout);
}

static int configure_clock(struct snd_soc_component *component)
{
	struct wm8993_priv *wm8993 = snd_soc_component_get_drvdata(component);
	unsigned int reg;

	/* This should be done on init() for bypass paths */
	switch (wm8993->sysclk_source) {
	case WM8993_SYSCLK_MCLK:
		dev_dbg(component->dev, "Using %dHz MCLK\n", wm8993->mclk_rate);

		reg = snd_soc_component_read32(component, WM8993_CLOCKING_2);
		reg &= ~(WM8993_MCLK_DIV | WM8993_SYSCLK_SRC);
		if (wm8993->mclk_rate > 13500000) {
			reg |= WM8993_MCLK_DIV;
			wm8993->sysclk_rate = wm8993->mclk_rate / 2;
		} else {
			reg &= ~WM8993_MCLK_DIV;
			wm8993->sysclk_rate = wm8993->mclk_rate;
		}
		snd_soc_component_write(component, WM8993_CLOCKING_2, reg);
		break;

	case WM8993_SYSCLK_FLL:
		dev_dbg(component->dev, "Using %dHz FLL clock\n",
			wm8993->fll_fout);

		reg = snd_soc_component_read32(component, WM8993_CLOCKING_2);
		reg |= WM8993_SYSCLK_SRC;
		if (wm8993->fll_fout > 13500000) {
			reg |= WM8993_MCLK_DIV;
			wm8993->sysclk_rate = wm8993->fll_fout / 2;
		} else {
			reg &= ~WM8993_MCLK_DIV;
			wm8993->sysclk_rate = wm8993->fll_fout;
		}
		snd_soc_component_write(component, WM8993_CLOCKING_2, reg);
		break;

	default:
		dev_err(component->dev, "System clock not configured\n");
		return -EINVAL;
	}

	dev_dbg(component->dev, "CLK_SYS is %dHz\n", wm8993->sysclk_rate);

	return 0;
}

static const DECLARE_TLV_DB_SCALE(sidetone_tlv, -3600, 300, 0);
static const DECLARE_TLV_DB_SCALE(drc_comp_threash, -4500, 75, 0);
static const DECLARE_TLV_DB_SCALE(drc_comp_amp, -2250, 75, 0);
static const DECLARE_TLV_DB_SCALE(drc_min_tlv, -1800, 600, 0);
static const DECLARE_TLV_DB_RANGE(drc_max_tlv,
	0, 2, TLV_DB_SCALE_ITEM(1200, 600, 0),
	3, 3, TLV_DB_SCALE_ITEM(3600, 0, 0)
);
static const DECLARE_TLV_DB_SCALE(drc_qr_tlv, 1200, 600, 0);
static const DECLARE_TLV_DB_SCALE(drc_startup_tlv, -1800, 300, 0);
static const DECLARE_TLV_DB_SCALE(eq_tlv, -1200, 100, 0);
static const DECLARE_TLV_DB_SCALE(digital_tlv, -7200, 75, 1);
static const DECLARE_TLV_DB_SCALE(dac_boost_tlv, 0, 600, 0);

static const char *dac_deemph_text[] = {
	"None",
	"32kHz",
	"44.1kHz",
	"48kHz",
};

static SOC_ENUM_SINGLE_DECL(dac_deemph,
			    WM8993_DAC_CTRL, 4, dac_deemph_text);

static const char *adc_hpf_text[] = {
	"Hi-Fi",
	"Voice 1",
	"Voice 2",
	"Voice 3",
};

static SOC_ENUM_SINGLE_DECL(adc_hpf,
			    WM8993_ADC_CTRL, 5, adc_hpf_text);

static const char *drc_path_text[] = {
	"ADC",
	"DAC"
};

static SOC_ENUM_SINGLE_DECL(drc_path,
			    WM8993_DRC_CONTROL_1, 14, drc_path_text);

static const char *drc_r0_text[] = {
	"1",
	"1/2",
	"1/4",
	"1/8",
	"1/16",
	"0",
};

static SOC_ENUM_SINGLE_DECL(drc_r0,
			    WM8993_DRC_CONTROL_3, 8, drc_r0_text);

static const char *drc_r1_text[] = {
	"1",
	"1/2",
	"1/4",
	"1/8",
	"0",
};

static SOC_ENUM_SINGLE_DECL(drc_r1,
			    WM8993_DRC_CONTROL_4, 13, drc_r1_text);

static const char *drc_attack_text[] = {
	"Reserved",
	"181us",
	"363us",
	"726us",
	"1.45ms",
	"2.9ms",
	"5.8ms",
	"11.6ms",
	"23.2ms",
	"46.4ms",
	"92.8ms",
	"185.6ms",
};

static SOC_ENUM_SINGLE_DECL(drc_attack,
			    WM8993_DRC_CONTROL_2, 12, drc_attack_text);

static const char *drc_decay_text[] = {
	"186ms",
	"372ms",
	"743ms",
	"1.49s",
	"2.97ms",
	"5.94ms",
	"11.89ms",
	"23.78ms",
	"47.56ms",
};

static SOC_ENUM_SINGLE_DECL(drc_decay,
			    WM8993_DRC_CONTROL_2, 8, drc_decay_text);

static const char *drc_ff_text[] = {
	"5 samples",
	"9 samples",
};

static SOC_ENUM_SINGLE_DECL(drc_ff,
			    WM8993_DRC_CONTROL_3, 7, drc_ff_text);

static const char *drc_qr_rate_text[] = {
	"0.725ms",
	"1.45ms",
	"5.8ms",
};

static SOC_ENUM_SINGLE_DECL(drc_qr_rate,
			    WM8993_DRC_CONTROL_3, 0, drc_qr_rate_text);

static const char *drc_smooth_text[] = {
	"Low",
	"Medium",
	"High",
};

static SOC_ENUM_SINGLE_DECL(drc_smooth,
			    WM8993_DRC_CONTROL_1, 4, drc_smooth_text);

static const struct snd_kcontrol_new wm8993_snd_controls[] = {
SOC_DOUBLE_TLV("Digital Sidetone Volume", WM8993_DIGITAL_SIDE_TONE,
	       5, 9, 12, 0, sidetone_tlv),

SOC_SINGLE("DRC Switch", WM8993_DRC_CONTROL_1, 15, 1, 0),
SOC_ENUM("DRC Path", drc_path),
SOC_SINGLE_TLV("DRC Compressor Threshold Volume", WM8993_DRC_CONTROL_2,
	       2, 60, 1, drc_comp_threash),
SOC_SINGLE_TLV("DRC Compressor Amplitude Volume", WM8993_DRC_CONTROL_3,
	       11, 30, 1, drc_comp_amp),
SOC_ENUM("DRC R0", drc_r0),
SOC_ENUM("DRC R1", drc_r1),
SOC_SINGLE_TLV("DRC Minimum Volume", WM8993_DRC_CONTROL_1, 2, 3, 1,
	       drc_min_tlv),
SOC_SINGLE_TLV("DRC Maximum Volume", WM8993_DRC_CONTROL_1, 0, 3, 0,
	       drc_max_tlv),
SOC_ENUM("DRC Attack Rate", drc_attack),
SOC_ENUM("DRC Decay Rate", drc_decay),
SOC_ENUM("DRC FF Delay", drc_ff),
SOC_SINGLE("DRC Anti-clip Switch", WM8993_DRC_CONTROL_1, 9, 1, 0),
SOC_SINGLE("DRC Quick Release Switch", WM8993_DRC_CONTROL_1, 10, 1, 0),
SOC_SINGLE_TLV("DRC Quick Release Volume", WM8993_DRC_CONTROL_3, 2, 3, 0,
	       drc_qr_tlv),
SOC_ENUM("DRC Quick Release Rate", drc_qr_rate),
SOC_SINGLE("DRC Smoothing Switch", WM8993_DRC_CONTROL_1, 11, 1, 0),
SOC_SINGLE("DRC Smoothing Hysteresis Switch", WM8993_DRC_CONTROL_1, 8, 1, 0),
SOC_ENUM("DRC Smoothing Hysteresis Threshold", drc_smooth),
SOC_SINGLE_TLV("DRC Startup Volume", WM8993_DRC_CONTROL_4, 8, 18, 0,
	       drc_startup_tlv),

SOC_SINGLE("EQ Switch", WM8993_EQ1, 0, 1, 0),

SOC_DOUBLE_R_TLV("Capture Volume", WM8993_LEFT_ADC_DIGITAL_VOLUME,
		 WM8993_RIGHT_ADC_DIGITAL_VOLUME, 1, 96, 0, digital_tlv),
SOC_SINGLE("ADC High Pass Filter Switch", WM8993_ADC_CTRL, 8, 1, 0),
SOC_ENUM("ADC High Pass Filter Mode", adc_hpf),

SOC_DOUBLE_R_TLV("Playback Volume", WM8993_LEFT_DAC_DIGITAL_VOLUME,
		 WM8993_RIGHT_DAC_DIGITAL_VOLUME, 1, 96, 0, digital_tlv),
SOC_SINGLE_TLV("Playback Boost Volume", WM8993_AUDIO_INTERFACE_2, 10, 3, 0,
	       dac_boost_tlv),
SOC_ENUM("DAC Deemphasis", dac_deemph),

SOC_SINGLE_TLV("SPKL DAC Volume", WM8993_SPKMIXL_ATTENUATION,
	       2, 1, 1, wm_hubs_spkmix_tlv),

SOC_SINGLE_TLV("SPKR DAC Volume", WM8993_SPKMIXR_ATTENUATION,
	       2, 1, 1, wm_hubs_spkmix_tlv),
};

static const struct snd_kcontrol_new wm8993_eq_controls[] = {
SOC_SINGLE_TLV("EQ1 Volume", WM8993_EQ2, 0, 24, 0, eq_tlv),
SOC_SINGLE_TLV("EQ2 Volume", WM8993_EQ3, 0, 24, 0, eq_tlv),
SOC_SINGLE_TLV("EQ3 Volume", WM8993_EQ4, 0, 24, 0, eq_tlv),
SOC_SINGLE_TLV("EQ4 Volume", WM8993_EQ5, 0, 24, 0, eq_tlv),
SOC_SINGLE_TLV("EQ5 Volume", WM8993_EQ6, 0, 24, 0, eq_tlv),
};

static int clk_sys_event(struct snd_soc_dapm_widget *w,
			 struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		return configure_clock(component);

	case SND_SOC_DAPM_POST_PMD:
		break;
	}

	return 0;
}

static const struct snd_kcontrol_new left_speaker_mixer[] = {
SOC_DAPM_SINGLE("Input Switch", WM8993_SPEAKER_MIXER, 7, 1, 0),
SOC_DAPM_SINGLE("IN1LP Switch", WM8993_SPEAKER_MIXER, 5, 1, 0),
SOC_DAPM_SINGLE("Output Switch", WM8993_SPEAKER_MIXER, 3, 1, 0),
SOC_DAPM_SINGLE("DAC Switch", WM8993_SPEAKER_MIXER, 6, 1, 0),
};

static const struct snd_kcontrol_new right_speaker_mixer[] = {
SOC_DAPM_SINGLE("Input Switch", WM8993_SPEAKER_MIXER, 6, 1, 0),
SOC_DAPM_SINGLE("IN1RP Switch", WM8993_SPEAKER_MIXER, 4, 1, 0),
SOC_DAPM_SINGLE("Output Switch", WM8993_SPEAKER_MIXER, 2, 1, 0),
SOC_DAPM_SINGLE("DAC Switch", WM8993_SPEAKER_MIXER, 0, 1, 0),
};

static const char *aif_text[] = {
	"Left", "Right"
};

static SOC_ENUM_SINGLE_DECL(aifoutl_enum,
			    WM8993_AUDIO_INTERFACE_1, 15, aif_text);

static const struct snd_kcontrol_new aifoutl_mux =
	SOC_DAPM_ENUM("AIFOUTL Mux", aifoutl_enum);

static SOC_ENUM_SINGLE_DECL(aifoutr_enum,
			    WM8993_AUDIO_INTERFACE_1, 14, aif_text);

static const struct snd_kcontrol_new aifoutr_mux =
	SOC_DAPM_ENUM("AIFOUTR Mux", aifoutr_enum);

static SOC_ENUM_SINGLE_DECL(aifinl_enum,
			    WM8993_AUDIO_INTERFACE_2, 15, aif_text);

static const struct snd_kcontrol_new aifinl_mux =
	SOC_DAPM_ENUM("AIFINL Mux", aifinl_enum);

static SOC_ENUM_SINGLE_DECL(aifinr_enum,
			    WM8993_AUDIO_INTERFACE_2, 14, aif_text);

static const struct snd_kcontrol_new aifinr_mux =
	SOC_DAPM_ENUM("AIFINR Mux", aifinr_enum);

static const char *sidetone_text[] = {
	"None", "Left", "Right"
};

static SOC_ENUM_SINGLE_DECL(sidetonel_enum,
			    WM8993_DIGITAL_SIDE_TONE, 2, sidetone_text);

static const struct snd_kcontrol_new sidetonel_mux =
	SOC_DAPM_ENUM("Left Sidetone", sidetonel_enum);

static SOC_ENUM_SINGLE_DECL(sidetoner_enum,
			    WM8993_DIGITAL_SIDE_TONE, 0, sidetone_text);

static const struct snd_kcontrol_new sidetoner_mux =
	SOC_DAPM_ENUM("Right Sidetone", sidetoner_enum);

static const struct snd_soc_dapm_widget wm8993_dapm_widgets[] = {
SND_SOC_DAPM_SUPPLY("CLK_SYS", WM8993_BUS_CONTROL_1, 1, 0, clk_sys_event,
		    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
SND_SOC_DAPM_SUPPLY("TOCLK", WM8993_CLOCKING_1, 14, 0, NULL, 0),
SND_SOC_DAPM_SUPPLY("CLK_DSP", WM8993_CLOCKING_3, 0, 0, NULL, 0),
SND_SOC_DAPM_SUPPLY("VMID", SND_SOC_NOPM, 0, 0, NULL, 0),

SND_SOC_DAPM_ADC("ADCL", NULL, WM8993_POWER_MANAGEMENT_2, 1, 0),
SND_SOC_DAPM_ADC("ADCR", NULL, WM8993_POWER_MANAGEMENT_2, 0, 0),

SND_SOC_DAPM_MUX("AIFOUTL Mux", SND_SOC_NOPM, 0, 0, &aifoutl_mux),
SND_SOC_DAPM_MUX("AIFOUTR Mux", SND_SOC_NOPM, 0, 0, &aifoutr_mux),

SND_SOC_DAPM_AIF_OUT("AIFOUTL", "Capture", 0, SND_SOC_NOPM, 0, 0),
SND_SOC_DAPM_AIF_OUT("AIFOUTR", "Capture", 1, SND_SOC_NOPM, 0, 0),

SND_SOC_DAPM_AIF_IN("AIFINL", "Playback", 0, SND_SOC_NOPM, 0, 0),
SND_SOC_DAPM_AIF_IN("AIFINR", "Playback", 1, SND_SOC_NOPM, 0, 0),

SND_SOC_DAPM_MUX("DACL Mux", SND_SOC_NOPM, 0, 0, &aifinl_mux),
SND_SOC_DAPM_MUX("DACR Mux", SND_SOC_NOPM, 0, 0, &aifinr_mux),

SND_SOC_DAPM_MUX("DACL Sidetone", SND_SOC_NOPM, 0, 0, &sidetonel_mux),
SND_SOC_DAPM_MUX("DACR Sidetone", SND_SOC_NOPM, 0, 0, &sidetoner_mux),

SND_SOC_DAPM_DAC("DACL", NULL, WM8993_POWER_MANAGEMENT_3, 1, 0),
SND_SOC_DAPM_DAC("DACR", NULL, WM8993_POWER_MANAGEMENT_3, 0, 0),

SND_SOC_DAPM_MUX("Left Headphone Mux", SND_SOC_NOPM, 0, 0, &wm_hubs_hpl_mux),
SND_SOC_DAPM_MUX("Right Headphone Mux", SND_SOC_NOPM, 0, 0, &wm_hubs_hpr_mux),

SND_SOC_DAPM_MIXER("SPKL", WM8993_POWER_MANAGEMENT_3, 8, 0,
		   left_speaker_mixer, ARRAY_SIZE(left_speaker_mixer)),
SND_SOC_DAPM_MIXER("SPKR", WM8993_POWER_MANAGEMENT_3, 9, 0,
		   right_speaker_mixer, ARRAY_SIZE(right_speaker_mixer)),
SND_SOC_DAPM_PGA("Direct Voice", SND_SOC_NOPM, 0, 0, NULL, 0),
};

static const struct snd_soc_dapm_route routes[] = {
	{ "MICBIAS1", NULL, "VMID" },
	{ "MICBIAS2", NULL, "VMID" },

	{ "ADCL", NULL, "CLK_SYS" },
	{ "ADCL", NULL, "CLK_DSP" },
	{ "ADCR", NULL, "CLK_SYS" },
	{ "ADCR", NULL, "CLK_DSP" },

	{ "AIFOUTL Mux", "Left", "ADCL" },
	{ "AIFOUTL Mux", "Right", "ADCR" },
	{ "AIFOUTR Mux", "Left", "ADCL" },
	{ "AIFOUTR Mux", "Right", "ADCR" },

	{ "AIFOUTL", NULL, "AIFOUTL Mux" },
	{ "AIFOUTR", NULL, "AIFOUTR Mux" },

	{ "DACL Mux", "Left", "AIFINL" },
	{ "DACL Mux", "Right", "AIFINR" },
	{ "DACR Mux", "Left", "AIFINL" },
	{ "DACR Mux", "Right", "AIFINR" },

	{ "DACL Sidetone", "Left", "ADCL" },
	{ "DACL Sidetone", "Right", "ADCR" },
	{ "DACR Sidetone", "Left", "ADCL" },
	{ "DACR Sidetone", "Right", "ADCR" },

	{ "DACL", NULL, "CLK_SYS" },
	{ "DACL", NULL, "CLK_DSP" },
	{ "DACL", NULL, "DACL Mux" },
	{ "DACL", NULL, "DACL Sidetone" },
	{ "DACR", NULL, "CLK_SYS" },
	{ "DACR", NULL, "CLK_DSP" },
	{ "DACR", NULL, "DACR Mux" },
	{ "DACR", NULL, "DACR Sidetone" },

	{ "Left Output Mixer", "DAC Switch", "DACL" },

	{ "Right Output Mixer", "DAC Switch", "DACR" },

	{ "Left Output PGA", NULL, "CLK_SYS" },

	{ "Right Output PGA", NULL, "CLK_SYS" },

	{ "SPKL", "DAC Switch", "DACL" },
	{ "SPKL", NULL, "CLK_SYS" },

	{ "SPKR", "DAC Switch", "DACR" },
	{ "SPKR", NULL, "CLK_SYS" },

	{ "Left Headphone Mux", "DAC", "DACL" },
	{ "Right Headphone Mux", "DAC", "DACR" },
};

static int wm8993_set_bias_level(struct snd_soc_component *component,
				 enum snd_soc_bias_level level)
{
	struct wm8993_priv *wm8993 = snd_soc_component_get_drvdata(component);
	int ret;

	wm_hubs_set_bias_level(component, level);

	switch (level) {
	case SND_SOC_BIAS_ON:
	case SND_SOC_BIAS_PREPARE:
		/* VMID=2*40k */
		snd_soc_component_update_bits(component, WM8993_POWER_MANAGEMENT_1,
				    WM8993_VMID_SEL_MASK, 0x2);
		snd_soc_component_update_bits(component, WM8993_POWER_MANAGEMENT_2,
				    WM8993_TSHUT_ENA, WM8993_TSHUT_ENA);
		break;

	case SND_SOC_BIAS_STANDBY:
		if (snd_soc_component_get_bias_level(component) == SND_SOC_BIAS_OFF) {
			ret = regulator_bulk_enable(ARRAY_SIZE(wm8993->supplies),
						    wm8993->supplies);
			if (ret != 0)
				return ret;

			regcache_cache_only(wm8993->regmap, false);
			regcache_sync(wm8993->regmap);

			wm_hubs_vmid_ena(component);

			/* Bring up VMID with fast soft start */
			snd_soc_component_update_bits(component, WM8993_ANTIPOP2,
					    WM8993_STARTUP_BIAS_ENA |
					    WM8993_VMID_BUF_ENA |
					    WM8993_VMID_RAMP_MASK |
					    WM8993_BIAS_SRC,
					    WM8993_STARTUP_BIAS_ENA |
					    WM8993_VMID_BUF_ENA |
					    WM8993_VMID_RAMP_MASK |
					    WM8993_BIAS_SRC);

			/* If either line output is single ended we
			 * need the VMID buffer */
			if (!wm8993->pdata.lineout1_diff ||
			    !wm8993->pdata.lineout2_diff)
				snd_soc_component_update_bits(component, WM8993_ANTIPOP1,
						 WM8993_LINEOUT_VMID_BUF_ENA,
						 WM8993_LINEOUT_VMID_BUF_ENA);

			/* VMID=2*40k */
			snd_soc_component_update_bits(component, WM8993_POWER_MANAGEMENT_1,
					    WM8993_VMID_SEL_MASK |
					    WM8993_BIAS_ENA,
					    WM8993_BIAS_ENA | 0x2);
			msleep(32);

			/* Switch to normal bias */
			snd_soc_component_update_bits(component, WM8993_ANTIPOP2,
					    WM8993_BIAS_SRC |
					    WM8993_STARTUP_BIAS_ENA, 0);
		}

		/* VMID=2*240k */
		snd_soc_component_update_bits(component, WM8993_POWER_MANAGEMENT_1,
				    WM8993_VMID_SEL_MASK, 0x4);

		snd_soc_component_update_bits(component, WM8993_POWER_MANAGEMENT_2,
				    WM8993_TSHUT_ENA, 0);
		break;

	case SND_SOC_BIAS_OFF:
		snd_soc_component_update_bits(component, WM8993_ANTIPOP1,
				    WM8993_LINEOUT_VMID_BUF_ENA, 0);

		snd_soc_component_update_bits(component, WM8993_POWER_MANAGEMENT_1,
				    WM8993_VMID_SEL_MASK | WM8993_BIAS_ENA,
				    0);

		snd_soc_component_update_bits(component, WM8993_ANTIPOP2,
				    WM8993_STARTUP_BIAS_ENA |
				    WM8993_VMID_BUF_ENA |
				    WM8993_VMID_RAMP_MASK |
				    WM8993_BIAS_SRC, 0);

		regcache_cache_only(wm8993->regmap, true);
		regcache_mark_dirty(wm8993->regmap);

		regulator_bulk_disable(ARRAY_SIZE(wm8993->supplies),
				       wm8993->supplies);
		break;
	}

	return 0;
}

static int wm8993_set_sysclk(struct snd_soc_dai *codec_dai,
			     int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_component *component = codec_dai->component;
	struct wm8993_priv *wm8993 = snd_soc_component_get_drvdata(component);

	switch (clk_id) {
	case WM8993_SYSCLK_MCLK:
		wm8993->mclk_rate = freq;
		/* fall through */
	case WM8993_SYSCLK_FLL:
		wm8993->sysclk_source = clk_id;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int wm8993_set_dai_fmt(struct snd_soc_dai *dai,
			      unsigned int fmt)
{
	struct snd_soc_component *component = dai->component;
	struct wm8993_priv *wm8993 = snd_soc_component_get_drvdata(component);
	unsigned int aif1 = snd_soc_component_read32(component, WM8993_AUDIO_INTERFACE_1);
	unsigned int aif4 = snd_soc_component_read32(component, WM8993_AUDIO_INTERFACE_4);

	aif1 &= ~(WM8993_BCLK_DIR | WM8993_AIF_BCLK_INV |
		  WM8993_AIF_LRCLK_INV | WM8993_AIF_FMT_MASK);
	aif4 &= ~WM8993_LRCLK_DIR;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		wm8993->master = 0;
		break;
	case SND_SOC_DAIFMT_CBS_CFM:
		aif4 |= WM8993_LRCLK_DIR;
		wm8993->master = 1;
		break;
	case SND_SOC_DAIFMT_CBM_CFS:
		aif1 |= WM8993_BCLK_DIR;
		wm8993->master = 1;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		aif1 |= WM8993_BCLK_DIR;
		aif4 |= WM8993_LRCLK_DIR;
		wm8993->master = 1;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_B:
		aif1 |= WM8993_AIF_LRCLK_INV;
		/* fall through */
	case SND_SOC_DAIFMT_DSP_A:
		aif1 |= 0x18;
		break;
	case SND_SOC_DAIFMT_I2S:
		aif1 |= 0x10;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		aif1 |= 0x8;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
	case SND_SOC_DAIFMT_DSP_B:
		/* frame inversion not valid for DSP modes */
		switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
		case SND_SOC_DAIFMT_NB_NF:
			break;
		case SND_SOC_DAIFMT_IB_NF:
			aif1 |= WM8993_AIF_BCLK_INV;
			break;
		default:
			return -EINVAL;
		}
		break;

	case SND_SOC_DAIFMT_I2S:
	case SND_SOC_DAIFMT_RIGHT_J:
	case SND_SOC_DAIFMT_LEFT_J:
		switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
		case SND_SOC_DAIFMT_NB_NF:
			break;
		case SND_SOC_DAIFMT_IB_IF:
			aif1 |= WM8993_AIF_BCLK_INV | WM8993_AIF_LRCLK_INV;
			break;
		case SND_SOC_DAIFMT_IB_NF:
			aif1 |= WM8993_AIF_BCLK_INV;
			break;
		case SND_SOC_DAIFMT_NB_IF:
			aif1 |= WM8993_AIF_LRCLK_INV;
			break;
		default:
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	snd_soc_component_write(component, WM8993_AUDIO_INTERFACE_1, aif1);
	snd_soc_component_write(component, WM8993_AUDIO_INTERFACE_4, aif4);

	return 0;
}

static int wm8993_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct wm8993_priv *wm8993 = snd_soc_component_get_drvdata(component);
	int ret, i, best, best_val, cur_val;
	unsigned int clocking1, clocking3, aif1, aif4;

	clocking1 = snd_soc_component_read32(component, WM8993_CLOCKING_1);
	clocking1 &= ~WM8993_BCLK_DIV_MASK;

	clocking3 = snd_soc_component_read32(component, WM8993_CLOCKING_3);
	clocking3 &= ~(WM8993_CLK_SYS_RATE_MASK | WM8993_SAMPLE_RATE_MASK);

	aif1 = snd_soc_component_read32(component, WM8993_AUDIO_INTERFACE_1);
	aif1 &= ~WM8993_AIF_WL_MASK;

	aif4 = snd_soc_component_read32(component, WM8993_AUDIO_INTERFACE_4);
	aif4 &= ~WM8993_LRCLK_RATE_MASK;

	/* What BCLK do we need? */
	wm8993->fs = params_rate(params);
	wm8993->bclk = 2 * wm8993->fs;
	if (wm8993->tdm_slots) {
		dev_dbg(component->dev, "Configuring for %d %d bit TDM slots\n",
			wm8993->tdm_slots, wm8993->tdm_width);
		wm8993->bclk *= wm8993->tdm_width * wm8993->tdm_slots;
	} else {
		switch (params_width(params)) {
		case 16:
			wm8993->bclk *= 16;
			break;
		case 20:
			wm8993->bclk *= 20;
			aif1 |= 0x8;
			break;
		case 24:
			wm8993->bclk *= 24;
			aif1 |= 0x10;
			break;
		case 32:
			wm8993->bclk *= 32;
			aif1 |= 0x18;
			break;
		default:
			return -EINVAL;
		}
	}

	dev_dbg(component->dev, "Target BCLK is %dHz\n", wm8993->bclk);

	ret = configure_clock(component);
	if (ret != 0)
		return ret;

	/* Select nearest CLK_SYS_RATE */
	best = 0;
	best_val = abs((wm8993->sysclk_rate / clk_sys_rates[0].ratio)
		       - wm8993->fs);
	for (i = 1; i < ARRAY_SIZE(clk_sys_rates); i++) {
		cur_val = abs((wm8993->sysclk_rate /
			       clk_sys_rates[i].ratio) - wm8993->fs);
		if (cur_val < best_val) {
			best = i;
			best_val = cur_val;
		}
	}
	dev_dbg(component->dev, "Selected CLK_SYS_RATIO of %d\n",
		clk_sys_rates[best].ratio);
	clocking3 |= (clk_sys_rates[best].clk_sys_rate
		      << WM8993_CLK_SYS_RATE_SHIFT);

	/* SAMPLE_RATE */
	best = 0;
	best_val = abs(wm8993->fs - sample_rates[0].rate);
	for (i = 1; i < ARRAY_SIZE(sample_rates); i++) {
		/* Closest match */
		cur_val = abs(wm8993->fs - sample_rates[i].rate);
		if (cur_val < best_val) {
			best = i;
			best_val = cur_val;
		}
	}
	dev_dbg(component->dev, "Selected SAMPLE_RATE of %dHz\n",
		sample_rates[best].rate);
	clocking3 |= (sample_rates[best].sample_rate
		      << WM8993_SAMPLE_RATE_SHIFT);

	/* BCLK_DIV */
	best = 0;
	best_val = INT_MAX;
	for (i = 0; i < ARRAY_SIZE(bclk_divs); i++) {
		cur_val = ((wm8993->sysclk_rate * 10) / bclk_divs[i].div)
			- wm8993->bclk;
		if (cur_val < 0) /* Table is sorted */
			break;
		if (cur_val < best_val) {
			best = i;
			best_val = cur_val;
		}
	}
	wm8993->bclk = (wm8993->sysclk_rate * 10) / bclk_divs[best].div;
	dev_dbg(component->dev, "Selected BCLK_DIV of %d for %dHz BCLK\n",
		bclk_divs[best].div, wm8993->bclk);
	clocking1 |= bclk_divs[best].bclk_div << WM8993_BCLK_DIV_SHIFT;

	/* LRCLK is a simple fraction of BCLK */
	dev_dbg(component->dev, "LRCLK_RATE is %d\n", wm8993->bclk / wm8993->fs);
	aif4 |= wm8993->bclk / wm8993->fs;

	snd_soc_component_write(component, WM8993_CLOCKING_1, clocking1);
	snd_soc_component_write(component, WM8993_CLOCKING_3, clocking3);
	snd_soc_component_write(component, WM8993_AUDIO_INTERFACE_1, aif1);
	snd_soc_component_write(component, WM8993_AUDIO_INTERFACE_4, aif4);

	/* ReTune Mobile? */
	if (wm8993->pdata.num_retune_configs) {
		u16 eq1 = snd_soc_component_read32(component, WM8993_EQ1);
		struct wm8993_retune_mobile_setting *s;

		best = 0;
		best_val = abs(wm8993->pdata.retune_configs[0].rate
			       - wm8993->fs);
		for (i = 0; i < wm8993->pdata.num_retune_configs; i++) {
			cur_val = abs(wm8993->pdata.retune_configs[i].rate
				      - wm8993->fs);
			if (cur_val < best_val) {
				best_val = cur_val;
				best = i;
			}
		}
		s = &wm8993->pdata.retune_configs[best];

		dev_dbg(component->dev, "ReTune Mobile %s tuned for %dHz\n",
			s->name, s->rate);

		/* Disable EQ while we reconfigure */
		snd_soc_component_update_bits(component, WM8993_EQ1, WM8993_EQ_ENA, 0);

		for (i = 1; i < ARRAY_SIZE(s->config); i++)
			snd_soc_component_write(component, WM8993_EQ1 + i, s->config[i]);

		snd_soc_component_update_bits(component, WM8993_EQ1, WM8993_EQ_ENA, eq1);
	}

	return 0;
}

static int wm8993_digital_mute(struct snd_soc_dai *codec_dai, int mute)
{
	struct snd_soc_component *component = codec_dai->component;
	unsigned int reg;

	reg = snd_soc_component_read32(component, WM8993_DAC_CTRL);

	if (mute)
		reg |= WM8993_DAC_MUTE;
	else
		reg &= ~WM8993_DAC_MUTE;

	snd_soc_component_write(component, WM8993_DAC_CTRL, reg);

	return 0;
}

static int wm8993_set_tdm_slot(struct snd_soc_dai *dai, unsigned int tx_mask,
			       unsigned int rx_mask, int slots, int slot_width)
{
	struct snd_soc_component *component = dai->component;
	struct wm8993_priv *wm8993 = snd_soc_component_get_drvdata(component);
	int aif1 = 0;
	int aif2 = 0;

	/* Don't need to validate anything if we're turning off TDM */
	if (slots == 0) {
		wm8993->tdm_slots = 0;
		goto out;
	}

	/* Note that we allow configurations we can't handle ourselves - 
	 * for example, we can generate clocks for slots 2 and up even if
	 * we can't use those slots ourselves.
	 */
	aif1 |= WM8993_AIFADC_TDM;
	aif2 |= WM8993_AIFDAC_TDM;

	switch (rx_mask) {
	case 3:
		break;
	case 0xc:
		aif1 |= WM8993_AIFADC_TDM_CHAN;
		break;
	default:
		return -EINVAL;
	}


	switch (tx_mask) {
	case 3:
		break;
	case 0xc:
		aif2 |= WM8993_AIFDAC_TDM_CHAN;
		break;
	default:
		return -EINVAL;
	}

out:
	wm8993->tdm_width = slot_width;
	wm8993->tdm_slots = slots / 2;

	snd_soc_component_update_bits(component, WM8993_AUDIO_INTERFACE_1,
			    WM8993_AIFADC_TDM | WM8993_AIFADC_TDM_CHAN, aif1);
	snd_soc_component_update_bits(component, WM8993_AUDIO_INTERFACE_2,
			    WM8993_AIFDAC_TDM | WM8993_AIFDAC_TDM_CHAN, aif2);

	return 0;
}

static irqreturn_t wm8993_irq(int irq, void *data)
{
	struct wm8993_priv *wm8993 = data;
	int mask, val, ret;

	ret = regmap_read(wm8993->regmap, WM8993_GPIO_CTRL_1, &val);
	if (ret != 0) {
		dev_err(wm8993->dev, "Failed to read interrupt status: %d\n",
			ret);
		return IRQ_NONE;
	}

	ret = regmap_read(wm8993->regmap, WM8993_GPIOCTRL_2, &mask);
	if (ret != 0) {
		dev_err(wm8993->dev, "Failed to read interrupt mask: %d\n",
			ret);
		return IRQ_NONE;
	}

	/* The IRQ pin status is visible in the register too */
	val &= ~(mask | WM8993_IRQ);
	if (!val)
		return IRQ_NONE;

	if (val & WM8993_TEMPOK_EINT)
		dev_crit(wm8993->dev, "Thermal warning\n");

	if (val & WM8993_FLL_LOCK_EINT) {
		dev_dbg(wm8993->dev, "FLL locked\n");
		complete(&wm8993->fll_lock);
	}

	ret = regmap_write(wm8993->regmap, WM8993_GPIO_CTRL_1, val);
	if (ret != 0)
		dev_err(wm8993->dev, "Failed to ack interrupt: %d\n", ret);

	return IRQ_HANDLED;
}

static const struct snd_soc_dai_ops wm8993_ops = {
	.set_sysclk = wm8993_set_sysclk,
	.set_fmt = wm8993_set_dai_fmt,
	.hw_params = wm8993_hw_params,
	.digital_mute = wm8993_digital_mute,
	.set_pll = wm8993_set_fll,
	.set_tdm_slot = wm8993_set_tdm_slot,
};

#define WM8993_RATES SNDRV_PCM_RATE_8000_48000

#define WM8993_FORMATS (SNDRV_PCM_FMTBIT_S16_LE |\
			SNDRV_PCM_FMTBIT_S20_3LE |\
			SNDRV_PCM_FMTBIT_S24_LE |\
			SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_driver wm8993_dai = {
	.name = "wm8993-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = WM8993_RATES,
		.formats = WM8993_FORMATS,
		.sig_bits = 24,
	},
	.capture = {
		 .stream_name = "Capture",
		 .channels_min = 1,
		 .channels_max = 2,
		 .rates = WM8993_RATES,
		 .formats = WM8993_FORMATS,
		 .sig_bits = 24,
	 },
	.ops = &wm8993_ops,
	.symmetric_rates = 1,
};

static int wm8993_probe(struct snd_soc_component *component)
{
	struct wm8993_priv *wm8993 = snd_soc_component_get_drvdata(component);
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);

	wm8993->hubs_data.hp_startup_mode = 1;
	wm8993->hubs_data.dcs_codes_l = -2;
	wm8993->hubs_data.dcs_codes_r = -2;
	wm8993->hubs_data.series_startup = 1;

	/* Latch volume update bits and default ZC on */
	snd_soc_component_update_bits(component, WM8993_RIGHT_DAC_DIGITAL_VOLUME,
			    WM8993_DAC_VU, WM8993_DAC_VU);
	snd_soc_component_update_bits(component, WM8993_RIGHT_ADC_DIGITAL_VOLUME,
			    WM8993_ADC_VU, WM8993_ADC_VU);

	/* Manualy manage the HPOUT sequencing for independent stereo
	 * control. */
	snd_soc_component_update_bits(component, WM8993_ANALOGUE_HP_0,
			    WM8993_HPOUT1_AUTO_PU, 0);

	/* Use automatic clock configuration */
	snd_soc_component_update_bits(component, WM8993_CLOCKING_4, WM8993_SR_MODE, 0);

	wm_hubs_handle_analogue_pdata(component, wm8993->pdata.lineout1_diff,
				      wm8993->pdata.lineout2_diff,
				      wm8993->pdata.lineout1fb,
				      wm8993->pdata.lineout2fb,
				      wm8993->pdata.jd_scthr,
				      wm8993->pdata.jd_thr,
				      wm8993->pdata.micbias1_delay,
				      wm8993->pdata.micbias2_delay,
				      wm8993->pdata.micbias1_lvl,
				      wm8993->pdata.micbias2_lvl);

	snd_soc_add_component_controls(component, wm8993_snd_controls,
			     ARRAY_SIZE(wm8993_snd_controls));
	if (wm8993->pdata.num_retune_configs != 0) {
		dev_dbg(component->dev, "Using ReTune Mobile\n");
	} else {
		dev_dbg(component->dev, "No ReTune Mobile, using normal EQ\n");
		snd_soc_add_component_controls(component, wm8993_eq_controls,
				     ARRAY_SIZE(wm8993_eq_controls));
	}

	snd_soc_dapm_new_controls(dapm, wm8993_dapm_widgets,
				  ARRAY_SIZE(wm8993_dapm_widgets));
	wm_hubs_add_analogue_controls(component);

	snd_soc_dapm_add_routes(dapm, routes, ARRAY_SIZE(routes));
	wm_hubs_add_analogue_routes(component, wm8993->pdata.lineout1_diff,
				    wm8993->pdata.lineout2_diff);

	/* If the line outputs are differential then we aren't presenting
	 * VMID as an output and can disable it.
	 */
	if (wm8993->pdata.lineout1_diff && wm8993->pdata.lineout2_diff)
		dapm->idle_bias_off = 1;

	return 0;

}

#ifdef CONFIG_PM
static int wm8993_suspend(struct snd_soc_component *component)
{
	struct wm8993_priv *wm8993 = snd_soc_component_get_drvdata(component);
	int fll_fout = wm8993->fll_fout;
	int fll_fref  = wm8993->fll_fref;
	int ret;

	/* Stop the FLL in an orderly fashion */
	ret = _wm8993_set_fll(component, 0, 0, 0, 0);
	if (ret != 0) {
		dev_err(component->dev, "Failed to stop FLL\n");
		return ret;
	}

	wm8993->fll_fout = fll_fout;
	wm8993->fll_fref = fll_fref;

	snd_soc_component_force_bias_level(component, SND_SOC_BIAS_OFF);

	return 0;
}

static int wm8993_resume(struct snd_soc_component *component)
{
	struct wm8993_priv *wm8993 = snd_soc_component_get_drvdata(component);
	int ret;

	snd_soc_component_force_bias_level(component, SND_SOC_BIAS_STANDBY);

	/* Restart the FLL? */
	if (wm8993->fll_fout) {
		int fll_fout = wm8993->fll_fout;
		int fll_fref  = wm8993->fll_fref;

		wm8993->fll_fref = 0;
		wm8993->fll_fout = 0;

		ret = _wm8993_set_fll(component, 0, wm8993->fll_src,
				     fll_fref, fll_fout);
		if (ret != 0)
			dev_err(component->dev, "Failed to restart FLL\n");
	}

	return 0;
}
#else
#define wm8993_suspend NULL
#define wm8993_resume NULL
#endif

/* Tune DC servo configuration */
static const struct reg_sequence wm8993_regmap_patch[] = {
	{ 0x44, 3 },
	{ 0x56, 3 },
	{ 0x44, 0 },
};

static const struct regmap_config wm8993_regmap = {
	.reg_bits = 8,
	.val_bits = 16,

	.max_register = WM8993_MAX_REGISTER,
	.volatile_reg = wm8993_volatile,
	.readable_reg = wm8993_readable,

	.cache_type = REGCACHE_RBTREE,
	.reg_defaults = wm8993_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(wm8993_reg_defaults),
};

static const struct snd_soc_component_driver soc_component_dev_wm8993 = {
	.probe			= wm8993_probe,
	.suspend		= wm8993_suspend,
	.resume			= wm8993_resume,
	.set_bias_level		= wm8993_set_bias_level,
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static int wm8993_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct wm8993_priv *wm8993;
	unsigned int reg;
	int ret, i;

	wm8993 = devm_kzalloc(&i2c->dev, sizeof(struct wm8993_priv),
			      GFP_KERNEL);
	if (wm8993 == NULL)
		return -ENOMEM;

	wm8993->dev = &i2c->dev;
	init_completion(&wm8993->fll_lock);

	wm8993->regmap = devm_regmap_init_i2c(i2c, &wm8993_regmap);
	if (IS_ERR(wm8993->regmap)) {
		ret = PTR_ERR(wm8993->regmap);
		dev_err(&i2c->dev, "Failed to allocate regmap: %d\n", ret);
		return ret;
	}

	i2c_set_clientdata(i2c, wm8993);

	for (i = 0; i < ARRAY_SIZE(wm8993->supplies); i++)
		wm8993->supplies[i].supply = wm8993_supply_names[i];

	ret = devm_regulator_bulk_get(&i2c->dev, ARRAY_SIZE(wm8993->supplies),
				 wm8993->supplies);
	if (ret != 0) {
		dev_err(&i2c->dev, "Failed to request supplies: %d\n", ret);
		return ret;
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(wm8993->supplies),
				    wm8993->supplies);
	if (ret != 0) {
		dev_err(&i2c->dev, "Failed to enable supplies: %d\n", ret);
		return ret;
	}

	ret = regmap_read(wm8993->regmap, WM8993_SOFTWARE_RESET, &reg);
	if (ret != 0) {
		dev_err(&i2c->dev, "Failed to read chip ID: %d\n", ret);
		goto err_enable;
	}

	if (reg != 0x8993) {
		dev_err(&i2c->dev, "Invalid ID register value %x\n", reg);
		ret = -EINVAL;
		goto err_enable;
	}

	ret = regmap_write(wm8993->regmap, WM8993_SOFTWARE_RESET, 0xffff);
	if (ret != 0)
		goto err_enable;

	ret = regmap_register_patch(wm8993->regmap, wm8993_regmap_patch,
				    ARRAY_SIZE(wm8993_regmap_patch));
	if (ret != 0)
		dev_warn(wm8993->dev, "Failed to apply regmap patch: %d\n",
			 ret);

	if (i2c->irq) {
		/* Put GPIO1 into interrupt mode (only GPIO1 can output IRQ) */
		ret = regmap_update_bits(wm8993->regmap, WM8993_GPIO1,
					 WM8993_GPIO1_PD |
					 WM8993_GPIO1_SEL_MASK, 7);
		if (ret != 0)
			goto err_enable;

		ret = request_threaded_irq(i2c->irq, NULL, wm8993_irq,
					   IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
					   "wm8993", wm8993);
		if (ret != 0)
			goto err_enable;

	}

	regulator_bulk_disable(ARRAY_SIZE(wm8993->supplies), wm8993->supplies);

	regcache_cache_only(wm8993->regmap, true);

	ret = devm_snd_soc_register_component(&i2c->dev,
			&soc_component_dev_wm8993, &wm8993_dai, 1);
	if (ret != 0) {
		dev_err(&i2c->dev, "Failed to register CODEC: %d\n", ret);
		goto err_irq;
	}

	return 0;

err_irq:
	if (i2c->irq)
		free_irq(i2c->irq, wm8993);
err_enable:
	regulator_bulk_disable(ARRAY_SIZE(wm8993->supplies), wm8993->supplies);
	return ret;
}

static int wm8993_i2c_remove(struct i2c_client *i2c)
{
	struct wm8993_priv *wm8993 = i2c_get_clientdata(i2c);

	if (i2c->irq)
		free_irq(i2c->irq, wm8993);
	regulator_bulk_disable(ARRAY_SIZE(wm8993->supplies), wm8993->supplies);

	return 0;
}

static const struct i2c_device_id wm8993_i2c_id[] = {
	{ "wm8993", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wm8993_i2c_id);

static struct i2c_driver wm8993_i2c_driver = {
	.driver = {
		.name = "wm8993",
	},
	.probe =    wm8993_i2c_probe,
	.remove =   wm8993_i2c_remove,
	.id_table = wm8993_i2c_id,
};

module_i2c_driver(wm8993_i2c_driver);

MODULE_DESCRIPTION("ASoC WM8993 driver");
MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_LICENSE("GPL");
