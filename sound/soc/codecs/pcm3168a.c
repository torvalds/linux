// SPDX-License-Identifier: GPL-2.0-only
/*
 * PCM3168A codec driver
 *
 * Copyright (C) 2015 Imagination Technologies Ltd.
 *
 * Author: Damien Horsley <Damien.Horsley@imgtec.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>

#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#include "pcm3168a.h"

#define PCM3168A_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | \
			 SNDRV_PCM_FMTBIT_S24_3LE | \
			 SNDRV_PCM_FMTBIT_S24_LE)

#define PCM3168A_FMT_I2S		0x0
#define PCM3168A_FMT_LEFT_J		0x1
#define PCM3168A_FMT_RIGHT_J		0x2
#define PCM3168A_FMT_RIGHT_J_16		0x3
#define PCM3168A_FMT_DSP_A		0x4
#define PCM3168A_FMT_DSP_B		0x5
#define PCM3168A_FMT_I2S_TDM		0x6
#define PCM3168A_FMT_LEFT_J_TDM		0x7

#define PCM3168A_NUM_SUPPLIES 6
static const char *const pcm3168a_supply_names[PCM3168A_NUM_SUPPLIES] = {
	"VDD1",
	"VDD2",
	"VCCAD1",
	"VCCAD2",
	"VCCDA1",
	"VCCDA2"
};

#define PCM3168A_DAI_DAC		0
#define PCM3168A_DAI_ADC		1

/* ADC/DAC side parameters */
struct pcm3168a_io_params {
	bool master_mode;
	unsigned int format;
	int tdm_slots;
	u32 tdm_mask;
	int slot_width;
};

struct pcm3168a_priv {
	struct regulator_bulk_data supplies[PCM3168A_NUM_SUPPLIES];
	struct regmap *regmap;
	struct clk *scki;
	struct gpio_desc *gpio_rst;
	unsigned long sysclk;

	struct pcm3168a_io_params io_params[2];
	struct snd_soc_dai_driver dai_drv[2];
};

static const char *const pcm3168a_roll_off[] = { "Sharp", "Slow" };

static SOC_ENUM_SINGLE_DECL(pcm3168a_d1_roll_off, PCM3168A_DAC_OP_FLT,
		PCM3168A_DAC_FLT_SHIFT, pcm3168a_roll_off);
static SOC_ENUM_SINGLE_DECL(pcm3168a_d2_roll_off, PCM3168A_DAC_OP_FLT,
		PCM3168A_DAC_FLT_SHIFT + 1, pcm3168a_roll_off);
static SOC_ENUM_SINGLE_DECL(pcm3168a_d3_roll_off, PCM3168A_DAC_OP_FLT,
		PCM3168A_DAC_FLT_SHIFT + 2, pcm3168a_roll_off);
static SOC_ENUM_SINGLE_DECL(pcm3168a_d4_roll_off, PCM3168A_DAC_OP_FLT,
		PCM3168A_DAC_FLT_SHIFT + 3, pcm3168a_roll_off);

static const char *const pcm3168a_volume_type[] = {
		"Individual", "Master + Individual" };

static SOC_ENUM_SINGLE_DECL(pcm3168a_dac_volume_type, PCM3168A_DAC_ATT_DEMP_ZF,
		PCM3168A_DAC_ATMDDA_SHIFT, pcm3168a_volume_type);

static const char *const pcm3168a_att_speed_mult[] = { "2048", "4096" };

static SOC_ENUM_SINGLE_DECL(pcm3168a_dac_att_mult, PCM3168A_DAC_ATT_DEMP_ZF,
		PCM3168A_DAC_ATSPDA_SHIFT, pcm3168a_att_speed_mult);

static const char *const pcm3168a_demp[] = {
		"Disabled", "48khz", "44.1khz", "32khz" };

static SOC_ENUM_SINGLE_DECL(pcm3168a_dac_demp, PCM3168A_DAC_ATT_DEMP_ZF,
		PCM3168A_DAC_DEMP_SHIFT, pcm3168a_demp);

static const char *const pcm3168a_zf_func[] = {
		"DAC 1/2/3/4 AND", "DAC 1/2/3/4 OR", "DAC 1/2/3 AND",
		"DAC 1/2/3 OR", "DAC 4 AND", "DAC 4 OR" };

static SOC_ENUM_SINGLE_DECL(pcm3168a_dac_zf_func, PCM3168A_DAC_ATT_DEMP_ZF,
		PCM3168A_DAC_AZRO_SHIFT, pcm3168a_zf_func);

static const char *const pcm3168a_pol[] = { "Active High", "Active Low" };

static SOC_ENUM_SINGLE_DECL(pcm3168a_dac_zf_pol, PCM3168A_DAC_ATT_DEMP_ZF,
		PCM3168A_DAC_ATSPDA_SHIFT, pcm3168a_pol);

static const char *const pcm3168a_con[] = { "Differential", "Single-Ended" };

static SOC_ENUM_DOUBLE_DECL(pcm3168a_adc1_con, PCM3168A_ADC_SEAD,
				0, 1, pcm3168a_con);
static SOC_ENUM_DOUBLE_DECL(pcm3168a_adc2_con, PCM3168A_ADC_SEAD,
				2, 3, pcm3168a_con);
static SOC_ENUM_DOUBLE_DECL(pcm3168a_adc3_con, PCM3168A_ADC_SEAD,
				4, 5, pcm3168a_con);

static SOC_ENUM_SINGLE_DECL(pcm3168a_adc_volume_type, PCM3168A_ADC_ATT_OVF,
		PCM3168A_ADC_ATMDAD_SHIFT, pcm3168a_volume_type);

static SOC_ENUM_SINGLE_DECL(pcm3168a_adc_att_mult, PCM3168A_ADC_ATT_OVF,
		PCM3168A_ADC_ATSPAD_SHIFT, pcm3168a_att_speed_mult);

static SOC_ENUM_SINGLE_DECL(pcm3168a_adc_ov_pol, PCM3168A_ADC_ATT_OVF,
		PCM3168A_ADC_OVFP_SHIFT, pcm3168a_pol);

/* -100db to 0db, register values 0-54 cause mute */
static const DECLARE_TLV_DB_SCALE(pcm3168a_dac_tlv, -10050, 50, 1);

/* -100db to 20db, register values 0-14 cause mute */
static const DECLARE_TLV_DB_SCALE(pcm3168a_adc_tlv, -10050, 50, 1);

static const struct snd_kcontrol_new pcm3168a_snd_controls[] = {
	SOC_SINGLE("DAC Power-Save Switch", PCM3168A_DAC_PWR_MST_FMT,
			PCM3168A_DAC_PSMDA_SHIFT, 1, 1),
	SOC_ENUM("DAC1 Digital Filter roll-off", pcm3168a_d1_roll_off),
	SOC_ENUM("DAC2 Digital Filter roll-off", pcm3168a_d2_roll_off),
	SOC_ENUM("DAC3 Digital Filter roll-off", pcm3168a_d3_roll_off),
	SOC_ENUM("DAC4 Digital Filter roll-off", pcm3168a_d4_roll_off),
	SOC_DOUBLE("DAC1 Invert Switch", PCM3168A_DAC_INV, 0, 1, 1, 0),
	SOC_DOUBLE("DAC2 Invert Switch", PCM3168A_DAC_INV, 2, 3, 1, 0),
	SOC_DOUBLE("DAC3 Invert Switch", PCM3168A_DAC_INV, 4, 5, 1, 0),
	SOC_DOUBLE("DAC4 Invert Switch", PCM3168A_DAC_INV, 6, 7, 1, 0),
	SOC_ENUM("DAC Volume Control Type", pcm3168a_dac_volume_type),
	SOC_ENUM("DAC Volume Rate Multiplier", pcm3168a_dac_att_mult),
	SOC_ENUM("DAC De-Emphasis", pcm3168a_dac_demp),
	SOC_ENUM("DAC Zero Flag Function", pcm3168a_dac_zf_func),
	SOC_ENUM("DAC Zero Flag Polarity", pcm3168a_dac_zf_pol),
	SOC_SINGLE_RANGE_TLV("Master Playback Volume",
			PCM3168A_DAC_VOL_MASTER, 0, 54, 255, 0,
			pcm3168a_dac_tlv),
	SOC_DOUBLE_R_RANGE_TLV("DAC1 Playback Volume",
			PCM3168A_DAC_VOL_CHAN_START,
			PCM3168A_DAC_VOL_CHAN_START + 1,
			0, 54, 255, 0, pcm3168a_dac_tlv),
	SOC_DOUBLE_R_RANGE_TLV("DAC2 Playback Volume",
			PCM3168A_DAC_VOL_CHAN_START + 2,
			PCM3168A_DAC_VOL_CHAN_START + 3,
			0, 54, 255, 0, pcm3168a_dac_tlv),
	SOC_DOUBLE_R_RANGE_TLV("DAC3 Playback Volume",
			PCM3168A_DAC_VOL_CHAN_START + 4,
			PCM3168A_DAC_VOL_CHAN_START + 5,
			0, 54, 255, 0, pcm3168a_dac_tlv),
	SOC_DOUBLE_R_RANGE_TLV("DAC4 Playback Volume",
			PCM3168A_DAC_VOL_CHAN_START + 6,
			PCM3168A_DAC_VOL_CHAN_START + 7,
			0, 54, 255, 0, pcm3168a_dac_tlv),
	SOC_SINGLE("ADC1 High-Pass Filter Switch", PCM3168A_ADC_PWR_HPFB,
			PCM3168A_ADC_BYP_SHIFT, 1, 1),
	SOC_SINGLE("ADC2 High-Pass Filter Switch", PCM3168A_ADC_PWR_HPFB,
			PCM3168A_ADC_BYP_SHIFT + 1, 1, 1),
	SOC_SINGLE("ADC3 High-Pass Filter Switch", PCM3168A_ADC_PWR_HPFB,
			PCM3168A_ADC_BYP_SHIFT + 2, 1, 1),
	SOC_ENUM("ADC1 Connection Type", pcm3168a_adc1_con),
	SOC_ENUM("ADC2 Connection Type", pcm3168a_adc2_con),
	SOC_ENUM("ADC3 Connection Type", pcm3168a_adc3_con),
	SOC_DOUBLE("ADC1 Invert Switch", PCM3168A_ADC_INV, 0, 1, 1, 0),
	SOC_DOUBLE("ADC2 Invert Switch", PCM3168A_ADC_INV, 2, 3, 1, 0),
	SOC_DOUBLE("ADC3 Invert Switch", PCM3168A_ADC_INV, 4, 5, 1, 0),
	SOC_DOUBLE("ADC1 Mute Switch", PCM3168A_ADC_MUTE, 0, 1, 1, 0),
	SOC_DOUBLE("ADC2 Mute Switch", PCM3168A_ADC_MUTE, 2, 3, 1, 0),
	SOC_DOUBLE("ADC3 Mute Switch", PCM3168A_ADC_MUTE, 4, 5, 1, 0),
	SOC_ENUM("ADC Volume Control Type", pcm3168a_adc_volume_type),
	SOC_ENUM("ADC Volume Rate Multiplier", pcm3168a_adc_att_mult),
	SOC_ENUM("ADC Overflow Flag Polarity", pcm3168a_adc_ov_pol),
	SOC_SINGLE_RANGE_TLV("Master Capture Volume",
			PCM3168A_ADC_VOL_MASTER, 0, 14, 255, 0,
			pcm3168a_adc_tlv),
	SOC_DOUBLE_R_RANGE_TLV("ADC1 Capture Volume",
			PCM3168A_ADC_VOL_CHAN_START,
			PCM3168A_ADC_VOL_CHAN_START + 1,
			0, 14, 255, 0, pcm3168a_adc_tlv),
	SOC_DOUBLE_R_RANGE_TLV("ADC2 Capture Volume",
			PCM3168A_ADC_VOL_CHAN_START + 2,
			PCM3168A_ADC_VOL_CHAN_START + 3,
			0, 14, 255, 0, pcm3168a_adc_tlv),
	SOC_DOUBLE_R_RANGE_TLV("ADC3 Capture Volume",
			PCM3168A_ADC_VOL_CHAN_START + 4,
			PCM3168A_ADC_VOL_CHAN_START + 5,
			0, 14, 255, 0, pcm3168a_adc_tlv)
};

static const struct snd_soc_dapm_widget pcm3168a_dapm_widgets[] = {
	SND_SOC_DAPM_DAC("DAC1", "Playback", PCM3168A_DAC_OP_FLT,
			PCM3168A_DAC_OPEDA_SHIFT, 1),
	SND_SOC_DAPM_DAC("DAC2", "Playback", PCM3168A_DAC_OP_FLT,
			PCM3168A_DAC_OPEDA_SHIFT + 1, 1),
	SND_SOC_DAPM_DAC("DAC3", "Playback", PCM3168A_DAC_OP_FLT,
			PCM3168A_DAC_OPEDA_SHIFT + 2, 1),
	SND_SOC_DAPM_DAC("DAC4", "Playback", PCM3168A_DAC_OP_FLT,
			PCM3168A_DAC_OPEDA_SHIFT + 3, 1),

	SND_SOC_DAPM_OUTPUT("AOUT1L"),
	SND_SOC_DAPM_OUTPUT("AOUT1R"),
	SND_SOC_DAPM_OUTPUT("AOUT2L"),
	SND_SOC_DAPM_OUTPUT("AOUT2R"),
	SND_SOC_DAPM_OUTPUT("AOUT3L"),
	SND_SOC_DAPM_OUTPUT("AOUT3R"),
	SND_SOC_DAPM_OUTPUT("AOUT4L"),
	SND_SOC_DAPM_OUTPUT("AOUT4R"),

	SND_SOC_DAPM_ADC("ADC1", "Capture", PCM3168A_ADC_PWR_HPFB,
			PCM3168A_ADC_PSVAD_SHIFT, 1),
	SND_SOC_DAPM_ADC("ADC2", "Capture", PCM3168A_ADC_PWR_HPFB,
			PCM3168A_ADC_PSVAD_SHIFT + 1, 1),
	SND_SOC_DAPM_ADC("ADC3", "Capture", PCM3168A_ADC_PWR_HPFB,
			PCM3168A_ADC_PSVAD_SHIFT + 2, 1),

	SND_SOC_DAPM_INPUT("AIN1L"),
	SND_SOC_DAPM_INPUT("AIN1R"),
	SND_SOC_DAPM_INPUT("AIN2L"),
	SND_SOC_DAPM_INPUT("AIN2R"),
	SND_SOC_DAPM_INPUT("AIN3L"),
	SND_SOC_DAPM_INPUT("AIN3R")
};

static const struct snd_soc_dapm_route pcm3168a_dapm_routes[] = {
	/* Playback */
	{ "AOUT1L", NULL, "DAC1" },
	{ "AOUT1R", NULL, "DAC1" },

	{ "AOUT2L", NULL, "DAC2" },
	{ "AOUT2R", NULL, "DAC2" },

	{ "AOUT3L", NULL, "DAC3" },
	{ "AOUT3R", NULL, "DAC3" },

	{ "AOUT4L", NULL, "DAC4" },
	{ "AOUT4R", NULL, "DAC4" },

	/* Capture */
	{ "ADC1", NULL, "AIN1L" },
	{ "ADC1", NULL, "AIN1R" },

	{ "ADC2", NULL, "AIN2L" },
	{ "ADC2", NULL, "AIN2R" },

	{ "ADC3", NULL, "AIN3L" },
	{ "ADC3", NULL, "AIN3R" }
};

static unsigned int pcm3168a_scki_ratios[] = {
	768,
	512,
	384,
	256,
	192,
	128
};

#define PCM3168A_NUM_SCKI_RATIOS_DAC	ARRAY_SIZE(pcm3168a_scki_ratios)
#define PCM3168A_NUM_SCKI_RATIOS_ADC	(ARRAY_SIZE(pcm3168a_scki_ratios) - 2)

#define PCM3168A_MAX_SYSCLK		36864000

static int pcm3168a_reset(struct pcm3168a_priv *pcm3168a)
{
	int ret;

	ret = regmap_write(pcm3168a->regmap, PCM3168A_RST_SMODE, 0);
	if (ret)
		return ret;

	/* Internal reset is de-asserted after 3846 SCKI cycles */
	msleep(DIV_ROUND_UP(3846 * 1000, pcm3168a->sysclk));

	return regmap_write(pcm3168a->regmap, PCM3168A_RST_SMODE,
			PCM3168A_MRST_MASK | PCM3168A_SRST_MASK);
}

static int pcm3168a_mute(struct snd_soc_dai *dai, int mute, int direction)
{
	struct snd_soc_component *component = dai->component;
	struct pcm3168a_priv *pcm3168a = snd_soc_component_get_drvdata(component);

	regmap_write(pcm3168a->regmap, PCM3168A_DAC_MUTE, mute ? 0xff : 0);

	return 0;
}

static int pcm3168a_set_dai_sysclk(struct snd_soc_dai *dai,
				  int clk_id, unsigned int freq, int dir)
{
	struct pcm3168a_priv *pcm3168a = snd_soc_component_get_drvdata(dai->component);
	int ret;

	/*
	 * Some sound card sets 0 Hz as reset,
	 * but it is impossible to set. Ignore it here
	 */
	if (freq == 0)
		return 0;

	if (freq > PCM3168A_MAX_SYSCLK)
		return -EINVAL;

	ret = clk_set_rate(pcm3168a->scki, freq);
	if (ret)
		return ret;

	pcm3168a->sysclk = freq;

	return 0;
}

static void pcm3168a_update_fixup_pcm_stream(struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct pcm3168a_priv *pcm3168a = snd_soc_component_get_drvdata(component);
	struct pcm3168a_io_params *io_params = &pcm3168a->io_params[dai->id];
	u64 formats = SNDRV_PCM_FMTBIT_S24_3LE | SNDRV_PCM_FMTBIT_S24_LE;
	unsigned int channel_max = dai->id == PCM3168A_DAI_DAC ? 8 : 6;

	if (io_params->format == SND_SOC_DAIFMT_RIGHT_J) {
		/* S16_LE is only supported in RIGHT_J mode */
		formats |= SNDRV_PCM_FMTBIT_S16_LE;

		/*
		 * If multi DIN/DOUT is not selected, RIGHT_J can only support
		 * two channels (no TDM support)
		 */
		if (io_params->tdm_slots != 2)
			channel_max = 2;
	}

	if (dai->id == PCM3168A_DAI_DAC) {
		dai->driver->playback.channels_max = channel_max;
		dai->driver->playback.formats = formats;
	} else {
		dai->driver->capture.channels_max = channel_max;
		dai->driver->capture.formats = formats;
	}
}

static int pcm3168a_set_dai_fmt(struct snd_soc_dai *dai, unsigned int format)
{
	struct snd_soc_component *component = dai->component;
	struct pcm3168a_priv *pcm3168a = snd_soc_component_get_drvdata(component);
	struct pcm3168a_io_params *io_params = &pcm3168a->io_params[dai->id];
	bool master_mode;

	switch (format & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_LEFT_J:
	case SND_SOC_DAIFMT_I2S:
	case SND_SOC_DAIFMT_RIGHT_J:
	case SND_SOC_DAIFMT_DSP_A:
	case SND_SOC_DAIFMT_DSP_B:
		break;
	default:
		dev_err(component->dev, "unsupported dai format\n");
		return -EINVAL;
	}

	switch (format & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		master_mode = false;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		master_mode = true;
		break;
	default:
		dev_err(component->dev, "unsupported master/slave mode\n");
		return -EINVAL;
	}

	switch (format & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	default:
		return -EINVAL;
	}

	io_params->master_mode = master_mode;
	io_params->format = format & SND_SOC_DAIFMT_FORMAT_MASK;

	pcm3168a_update_fixup_pcm_stream(dai);

	return 0;
}

static int pcm3168a_set_tdm_slot(struct snd_soc_dai *dai, unsigned int tx_mask,
				 unsigned int rx_mask, int slots,
				 int slot_width)
{
	struct snd_soc_component *component = dai->component;
	struct pcm3168a_priv *pcm3168a = snd_soc_component_get_drvdata(component);
	struct pcm3168a_io_params *io_params = &pcm3168a->io_params[dai->id];

	if (tx_mask >= (1<<slots) || rx_mask >= (1<<slots)) {
		dev_err(component->dev,
			"Bad tdm mask tx: 0x%08x rx: 0x%08x slots %d\n",
			tx_mask, rx_mask, slots);
		return -EINVAL;
	}

	if (slot_width &&
	    (slot_width != 16 && slot_width != 24 && slot_width != 32 )) {
		dev_err(component->dev, "Unsupported slot_width %d\n",
			slot_width);
		return -EINVAL;
	}

	io_params->tdm_slots = slots;
	io_params->slot_width = slot_width;
	/* Ignore the not relevant mask for the DAI/direction */
	if (dai->id == PCM3168A_DAI_DAC)
		io_params->tdm_mask = tx_mask;
	else
		io_params->tdm_mask = rx_mask;

	pcm3168a_update_fixup_pcm_stream(dai);

	return 0;
}

static int pcm3168a_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct pcm3168a_priv *pcm3168a = snd_soc_component_get_drvdata(component);
	struct pcm3168a_io_params *io_params = &pcm3168a->io_params[dai->id];
	bool master_mode, tdm_mode;
	unsigned int format;
	unsigned int reg, mask, ms, ms_shift, fmt, fmt_shift, ratio, tdm_slots;
	int i, num_scki_ratios, slot_width;

	if (dai->id == PCM3168A_DAI_DAC) {
		num_scki_ratios = PCM3168A_NUM_SCKI_RATIOS_DAC;
		reg = PCM3168A_DAC_PWR_MST_FMT;
		mask = PCM3168A_DAC_MSDA_MASK | PCM3168A_DAC_FMT_MASK;
		ms_shift = PCM3168A_DAC_MSDA_SHIFT;
		fmt_shift = PCM3168A_DAC_FMT_SHIFT;
	} else {
		num_scki_ratios = PCM3168A_NUM_SCKI_RATIOS_ADC;
		reg = PCM3168A_ADC_MST_FMT;
		mask = PCM3168A_ADC_MSAD_MASK | PCM3168A_ADC_FMTAD_MASK;
		ms_shift = PCM3168A_ADC_MSAD_SHIFT;
		fmt_shift = PCM3168A_ADC_FMTAD_SHIFT;
	}

	master_mode = io_params->master_mode;

	if (master_mode) {
		ratio = pcm3168a->sysclk / params_rate(params);

		for (i = 0; i < num_scki_ratios; i++) {
			if (pcm3168a_scki_ratios[i] == ratio)
				break;
		}

		if (i == num_scki_ratios) {
			dev_err(component->dev, "unsupported sysclk ratio\n");
			return -EINVAL;
		}

		ms = (i + 1);
	} else {
		ms = 0;
	}

	format = io_params->format;

	if (io_params->slot_width)
		slot_width = io_params->slot_width;
	else
		slot_width = params_width(params);

	switch (slot_width) {
	case 16:
		if (master_mode || (format != SND_SOC_DAIFMT_RIGHT_J)) {
			dev_err(component->dev, "16-bit slots are supported only for slave mode using right justified\n");
			return -EINVAL;
		}
		break;
	case 24:
		if (master_mode || (format == SND_SOC_DAIFMT_DSP_A) ||
				   (format == SND_SOC_DAIFMT_DSP_B)) {
			dev_err(component->dev, "24-bit slots not supported in master mode, or slave mode using DSP\n");
			return -EINVAL;
		}
		break;
	case 32:
		break;
	default:
		dev_err(component->dev, "unsupported frame size: %d\n", slot_width);
		return -EINVAL;
	}

	if (io_params->tdm_slots)
		tdm_slots = io_params->tdm_slots;
	else
		tdm_slots = params_channels(params);

	/*
	 * Switch the codec to TDM mode when more than 2 TDM slots are needed
	 * for the stream.
	 * If pcm3168a->tdm_slots is not set or set to more than 2 (8/6 usually)
	 * then DIN1/DOUT1 is used in TDM mode.
	 * If pcm3168a->tdm_slots is set to 2 then DIN1/2/3/4 and DOUT1/2/3 is
	 * used in normal mode, no need to switch to TDM modes.
	 */
	tdm_mode = (tdm_slots > 2);

	if (tdm_mode) {
		switch (format) {
		case SND_SOC_DAIFMT_I2S:
		case SND_SOC_DAIFMT_DSP_A:
		case SND_SOC_DAIFMT_LEFT_J:
		case SND_SOC_DAIFMT_DSP_B:
			break;
		default:
			dev_err(component->dev,
				"TDM is supported under DSP/I2S/Left_J only\n");
			return -EINVAL;
		}
	}

	switch (format) {
	case SND_SOC_DAIFMT_I2S:
		fmt = tdm_mode ? PCM3168A_FMT_I2S_TDM : PCM3168A_FMT_I2S;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		fmt = tdm_mode ? PCM3168A_FMT_LEFT_J_TDM : PCM3168A_FMT_LEFT_J;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		fmt = (slot_width == 16) ? PCM3168A_FMT_RIGHT_J_16 :
					   PCM3168A_FMT_RIGHT_J;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		fmt = tdm_mode ? PCM3168A_FMT_I2S_TDM : PCM3168A_FMT_DSP_A;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		fmt = tdm_mode ? PCM3168A_FMT_LEFT_J_TDM : PCM3168A_FMT_DSP_B;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(pcm3168a->regmap, reg, mask,
			(ms << ms_shift) | (fmt << fmt_shift));

	return 0;
}

static u64 pcm3168a_dai_formats[] = {
	/*
	 * Select below from Sound Card, not here
	 *	SND_SOC_DAIFMT_CBC_CFC
	 *	SND_SOC_DAIFMT_CBP_CFP
	 */

	/*
	 * First Priority
	 */
	SND_SOC_POSSIBLE_DAIFMT_I2S	|
	SND_SOC_POSSIBLE_DAIFMT_LEFT_J,
	/*
	 * Second Priority
	 *
	 * These have picky limitation.
	 * see
	 *	pcm3168a_hw_params()
	 */
	SND_SOC_POSSIBLE_DAIFMT_RIGHT_J	|
	SND_SOC_POSSIBLE_DAIFMT_DSP_A	|
	SND_SOC_POSSIBLE_DAIFMT_DSP_B,
};

static const struct snd_soc_dai_ops pcm3168a_dai_ops = {
	.set_fmt	= pcm3168a_set_dai_fmt,
	.set_sysclk	= pcm3168a_set_dai_sysclk,
	.hw_params	= pcm3168a_hw_params,
	.mute_stream	= pcm3168a_mute,
	.set_tdm_slot	= pcm3168a_set_tdm_slot,
	.no_capture_mute = 1,
	.auto_selectable_formats	= pcm3168a_dai_formats,
	.num_auto_selectable_formats	= ARRAY_SIZE(pcm3168a_dai_formats),
};

static struct snd_soc_dai_driver pcm3168a_dais[] = {
	{
		.name = "pcm3168a-dac",
		.id = PCM3168A_DAI_DAC,
		.playback = {
			.stream_name = "Playback",
			.channels_min = 1,
			.channels_max = 8,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = PCM3168A_FORMATS
		},
		.ops = &pcm3168a_dai_ops
	},
	{
		.name = "pcm3168a-adc",
		.id = PCM3168A_DAI_ADC,
		.capture = {
			.stream_name = "Capture",
			.channels_min = 1,
			.channels_max = 6,
			.rates = SNDRV_PCM_RATE_8000_96000,
			.formats = PCM3168A_FORMATS
		},
		.ops = &pcm3168a_dai_ops
	},
};

static const struct reg_default pcm3168a_reg_default[] = {
	{ PCM3168A_RST_SMODE, PCM3168A_MRST_MASK | PCM3168A_SRST_MASK },
	{ PCM3168A_DAC_PWR_MST_FMT, 0x00 },
	{ PCM3168A_DAC_OP_FLT, 0x00 },
	{ PCM3168A_DAC_INV, 0x00 },
	{ PCM3168A_DAC_MUTE, 0x00 },
	{ PCM3168A_DAC_ZERO, 0x00 },
	{ PCM3168A_DAC_ATT_DEMP_ZF, 0x00 },
	{ PCM3168A_DAC_VOL_MASTER, 0xff },
	{ PCM3168A_DAC_VOL_CHAN_START, 0xff },
	{ PCM3168A_DAC_VOL_CHAN_START + 1, 0xff },
	{ PCM3168A_DAC_VOL_CHAN_START + 2, 0xff },
	{ PCM3168A_DAC_VOL_CHAN_START + 3, 0xff },
	{ PCM3168A_DAC_VOL_CHAN_START + 4, 0xff },
	{ PCM3168A_DAC_VOL_CHAN_START + 5, 0xff },
	{ PCM3168A_DAC_VOL_CHAN_START + 6, 0xff },
	{ PCM3168A_DAC_VOL_CHAN_START + 7, 0xff },
	{ PCM3168A_ADC_SMODE, 0x00 },
	{ PCM3168A_ADC_MST_FMT, 0x00 },
	{ PCM3168A_ADC_PWR_HPFB, 0x00 },
	{ PCM3168A_ADC_SEAD, 0x00 },
	{ PCM3168A_ADC_INV, 0x00 },
	{ PCM3168A_ADC_MUTE, 0x00 },
	{ PCM3168A_ADC_OV, 0x00 },
	{ PCM3168A_ADC_ATT_OVF, 0x00 },
	{ PCM3168A_ADC_VOL_MASTER, 0xd3 },
	{ PCM3168A_ADC_VOL_CHAN_START, 0xd3 },
	{ PCM3168A_ADC_VOL_CHAN_START + 1, 0xd3 },
	{ PCM3168A_ADC_VOL_CHAN_START + 2, 0xd3 },
	{ PCM3168A_ADC_VOL_CHAN_START + 3, 0xd3 },
	{ PCM3168A_ADC_VOL_CHAN_START + 4, 0xd3 },
	{ PCM3168A_ADC_VOL_CHAN_START + 5, 0xd3 }
};

static bool pcm3168a_readable_register(struct device *dev, unsigned int reg)
{
	if (reg >= PCM3168A_RST_SMODE)
		return true;
	else
		return false;
}

static bool pcm3168a_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case PCM3168A_RST_SMODE:
	case PCM3168A_DAC_ZERO:
	case PCM3168A_ADC_OV:
		return true;
	default:
		return false;
	}
}

static bool pcm3168a_writeable_register(struct device *dev, unsigned int reg)
{
	if (reg < PCM3168A_RST_SMODE)
		return false;

	switch (reg) {
	case PCM3168A_DAC_ZERO:
	case PCM3168A_ADC_OV:
		return false;
	default:
		return true;
	}
}

const struct regmap_config pcm3168a_regmap = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = PCM3168A_ADC_VOL_CHAN_START + 5,
	.reg_defaults = pcm3168a_reg_default,
	.num_reg_defaults = ARRAY_SIZE(pcm3168a_reg_default),
	.readable_reg = pcm3168a_readable_register,
	.volatile_reg = pcm3168a_volatile_register,
	.writeable_reg = pcm3168a_writeable_register,
	.cache_type = REGCACHE_FLAT
};
EXPORT_SYMBOL_GPL(pcm3168a_regmap);

static const struct snd_soc_component_driver pcm3168a_driver = {
	.controls		= pcm3168a_snd_controls,
	.num_controls		= ARRAY_SIZE(pcm3168a_snd_controls),
	.dapm_widgets		= pcm3168a_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(pcm3168a_dapm_widgets),
	.dapm_routes		= pcm3168a_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(pcm3168a_dapm_routes),
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

int pcm3168a_probe(struct device *dev, struct regmap *regmap)
{
	struct pcm3168a_priv *pcm3168a;
	int ret, i;

	pcm3168a = devm_kzalloc(dev, sizeof(*pcm3168a), GFP_KERNEL);
	if (pcm3168a == NULL)
		return -ENOMEM;

	dev_set_drvdata(dev, pcm3168a);

	/*
	 * Request the reset (connected to RST pin) gpio line as non exclusive
	 * as the same reset line might be connected to multiple pcm3168a codec
	 *
	 * The RST is low active, we want the GPIO line to be high initially, so
	 * request the initial level to LOW which in practice means DEASSERTED:
	 * The deasserted level of GPIO_ACTIVE_LOW is HIGH.
	 */
	pcm3168a->gpio_rst = devm_gpiod_get_optional(dev, "reset",
						GPIOD_OUT_LOW |
						GPIOD_FLAGS_BIT_NONEXCLUSIVE);
	if (IS_ERR(pcm3168a->gpio_rst))
		return dev_err_probe(dev, PTR_ERR(pcm3168a->gpio_rst),
				     "failed to acquire RST gpio\n");

	pcm3168a->scki = devm_clk_get(dev, "scki");
	if (IS_ERR(pcm3168a->scki))
		return dev_err_probe(dev, PTR_ERR(pcm3168a->scki),
				     "failed to acquire clock 'scki'\n");

	ret = clk_prepare_enable(pcm3168a->scki);
	if (ret) {
		dev_err(dev, "Failed to enable mclk: %d\n", ret);
		return ret;
	}

	pcm3168a->sysclk = clk_get_rate(pcm3168a->scki);

	for (i = 0; i < ARRAY_SIZE(pcm3168a->supplies); i++)
		pcm3168a->supplies[i].supply = pcm3168a_supply_names[i];

	ret = devm_regulator_bulk_get(dev,
			ARRAY_SIZE(pcm3168a->supplies), pcm3168a->supplies);
	if (ret) {
		dev_err_probe(dev, ret, "failed to request supplies\n");
		goto err_clk;
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(pcm3168a->supplies),
				    pcm3168a->supplies);
	if (ret) {
		dev_err(dev, "failed to enable supplies: %d\n", ret);
		goto err_clk;
	}

	pcm3168a->regmap = regmap;
	if (IS_ERR(pcm3168a->regmap)) {
		ret = PTR_ERR(pcm3168a->regmap);
		dev_err(dev, "failed to allocate regmap: %d\n", ret);
		goto err_regulator;
	}

	if (pcm3168a->gpio_rst) {
		/*
		 * The device is taken out from reset via GPIO line, wait for
		 * 3846 SCKI clock cycles for the internal reset de-assertion
		 */
		msleep(DIV_ROUND_UP(3846 * 1000, pcm3168a->sysclk));
	} else {
		ret = pcm3168a_reset(pcm3168a);
		if (ret) {
			dev_err(dev, "Failed to reset device: %d\n", ret);
			goto err_regulator;
		}
	}

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

	memcpy(pcm3168a->dai_drv, pcm3168a_dais, sizeof(pcm3168a->dai_drv));
	ret = devm_snd_soc_register_component(dev, &pcm3168a_driver,
					      pcm3168a->dai_drv,
					      ARRAY_SIZE(pcm3168a->dai_drv));
	if (ret) {
		dev_err(dev, "failed to register component: %d\n", ret);
		goto err_regulator;
	}

	return 0;

err_regulator:
	regulator_bulk_disable(ARRAY_SIZE(pcm3168a->supplies),
			pcm3168a->supplies);
err_clk:
	clk_disable_unprepare(pcm3168a->scki);

	return ret;
}
EXPORT_SYMBOL_GPL(pcm3168a_probe);

static void pcm3168a_disable(struct device *dev)
{
	struct pcm3168a_priv *pcm3168a = dev_get_drvdata(dev);

	regulator_bulk_disable(ARRAY_SIZE(pcm3168a->supplies),
			       pcm3168a->supplies);
	clk_disable_unprepare(pcm3168a->scki);
}

void pcm3168a_remove(struct device *dev)
{
	struct pcm3168a_priv *pcm3168a = dev_get_drvdata(dev);

	/*
	 * The RST is low active, we want the GPIO line to be low when the
	 * driver is removed, so set level to 1 which in practice means
	 * ASSERTED:
	 * The asserted level of GPIO_ACTIVE_LOW is LOW.
	 */
	gpiod_set_value_cansleep(pcm3168a->gpio_rst, 1);
	pm_runtime_disable(dev);
#ifndef CONFIG_PM
	pcm3168a_disable(dev);
#endif
}
EXPORT_SYMBOL_GPL(pcm3168a_remove);

#ifdef CONFIG_PM
static int pcm3168a_rt_resume(struct device *dev)
{
	struct pcm3168a_priv *pcm3168a = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(pcm3168a->scki);
	if (ret) {
		dev_err(dev, "Failed to enable mclk: %d\n", ret);
		return ret;
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(pcm3168a->supplies),
				    pcm3168a->supplies);
	if (ret) {
		dev_err(dev, "Failed to enable supplies: %d\n", ret);
		goto err_clk;
	}

	ret = pcm3168a_reset(pcm3168a);
	if (ret) {
		dev_err(dev, "Failed to reset device: %d\n", ret);
		goto err_regulator;
	}

	regcache_cache_only(pcm3168a->regmap, false);

	regcache_mark_dirty(pcm3168a->regmap);

	ret = regcache_sync(pcm3168a->regmap);
	if (ret) {
		dev_err(dev, "Failed to sync regmap: %d\n", ret);
		goto err_regulator;
	}

	return 0;

err_regulator:
	regulator_bulk_disable(ARRAY_SIZE(pcm3168a->supplies),
			       pcm3168a->supplies);
err_clk:
	clk_disable_unprepare(pcm3168a->scki);

	return ret;
}

static int pcm3168a_rt_suspend(struct device *dev)
{
	struct pcm3168a_priv *pcm3168a = dev_get_drvdata(dev);

	regcache_cache_only(pcm3168a->regmap, true);

	pcm3168a_disable(dev);

	return 0;
}
#endif

const struct dev_pm_ops pcm3168a_pm_ops = {
	SET_RUNTIME_PM_OPS(pcm3168a_rt_suspend, pcm3168a_rt_resume, NULL)
};
EXPORT_SYMBOL_GPL(pcm3168a_pm_ops);

MODULE_DESCRIPTION("PCM3168A codec driver");
MODULE_AUTHOR("Damien Horsley <Damien.Horsley@imgtec.com>");
MODULE_LICENSE("GPL v2");
