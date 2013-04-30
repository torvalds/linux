/*
 * Copyright (C) ST-Ericsson SA 2012
 *
 * Author: Ola Lilja <ola.o.lilja@stericsson.com>,
 *         Kristoffer Karlsson <kristoffer.karlsson@stericsson.com>
 *         for ST-Ericsson.
 *
 * License terms:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/clk.h>

#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>

#include "ux500_pcm.h"
#include "ux500_msp_dai.h"
#include "../codecs/ab8500-codec.h"

#define TX_SLOT_MONO	0x0008
#define TX_SLOT_STEREO	0x000a
#define RX_SLOT_MONO	0x0001
#define RX_SLOT_STEREO	0x0003
#define TX_SLOT_8CH	0x00FF
#define RX_SLOT_8CH	0x00FF

#define DEF_TX_SLOTS	TX_SLOT_STEREO
#define DEF_RX_SLOTS	RX_SLOT_MONO

#define DRIVERMODE_NORMAL	0
#define DRIVERMODE_CODEC_ONLY	1

/* Slot configuration */
static unsigned int tx_slots = DEF_TX_SLOTS;
static unsigned int rx_slots = DEF_RX_SLOTS;

/* Clocks */
static const char * const enum_mclk[] = {
	"SYSCLK",
	"ULPCLK"
};
enum mclk {
	MCLK_SYSCLK,
	MCLK_ULPCLK,
};

static SOC_ENUM_SINGLE_EXT_DECL(soc_enum_mclk, enum_mclk);

/* Private data for machine-part MOP500<->AB8500 */
struct mop500_ab8500_drvdata {
	/* Clocks */
	enum mclk mclk_sel;
	struct clk *clk_ptr_intclk;
	struct clk *clk_ptr_sysclk;
	struct clk *clk_ptr_ulpclk;
};

static inline const char *get_mclk_str(enum mclk mclk_sel)
{
	switch (mclk_sel) {
	case MCLK_SYSCLK:
		return "SYSCLK";
	case MCLK_ULPCLK:
		return "ULPCLK";
	default:
		return "Unknown";
	}
}

static int mop500_ab8500_set_mclk(struct device *dev,
				struct mop500_ab8500_drvdata *drvdata)
{
	int status;
	struct clk *clk_ptr;

	if (IS_ERR(drvdata->clk_ptr_intclk)) {
		dev_err(dev,
			"%s: ERROR: intclk not initialized!\n", __func__);
		return -EIO;
	}

	switch (drvdata->mclk_sel) {
	case MCLK_SYSCLK:
		clk_ptr = drvdata->clk_ptr_sysclk;
		break;
	case MCLK_ULPCLK:
		clk_ptr = drvdata->clk_ptr_ulpclk;
		break;
	default:
		return -EINVAL;
	}

	if (IS_ERR(clk_ptr)) {
		dev_err(dev, "%s: ERROR: %s not initialized!\n", __func__,
			get_mclk_str(drvdata->mclk_sel));
		return -EIO;
	}

	status = clk_set_parent(drvdata->clk_ptr_intclk, clk_ptr);
	if (status)
		dev_err(dev,
			"%s: ERROR: Setting intclk parent to %s failed (ret = %d)!",
			__func__, get_mclk_str(drvdata->mclk_sel), status);
	else
		dev_dbg(dev,
			"%s: intclk parent changed to %s.\n",
			__func__, get_mclk_str(drvdata->mclk_sel));

	return status;
}

/*
 * Control-events
 */

static int mclk_input_control_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_card *card = snd_kcontrol_chip(kcontrol);
	struct mop500_ab8500_drvdata *drvdata =
				snd_soc_card_get_drvdata(card);

	ucontrol->value.enumerated.item[0] = drvdata->mclk_sel;

	return 0;
}

static int mclk_input_control_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_card *card = snd_kcontrol_chip(kcontrol);
	struct mop500_ab8500_drvdata *drvdata =
				snd_soc_card_get_drvdata(card);
	unsigned int val = ucontrol->value.enumerated.item[0];

	if (val > (unsigned int)MCLK_ULPCLK)
		return -EINVAL;
	if (drvdata->mclk_sel == val)
		return 0;

	drvdata->mclk_sel = val;

	return 1;
}

/*
 * Controls
 */

static struct snd_kcontrol_new mop500_ab8500_ctrls[] = {
	SOC_ENUM_EXT("Master Clock Select",
		soc_enum_mclk,
		mclk_input_control_get, mclk_input_control_put),
	/* Digital interface - Clocks */
	SOC_SINGLE("Digital Interface Master Generator Switch",
		AB8500_DIGIFCONF1, AB8500_DIGIFCONF1_ENMASTGEN,
		1, 0),
	SOC_SINGLE("Digital Interface 0 Bit-clock Switch",
		AB8500_DIGIFCONF1, AB8500_DIGIFCONF1_ENFSBITCLK0,
		1, 0),
	SOC_SINGLE("Digital Interface 1 Bit-clock Switch",
		AB8500_DIGIFCONF1, AB8500_DIGIFCONF1_ENFSBITCLK1,
		1, 0),
	SOC_DAPM_PIN_SWITCH("Headset Left"),
	SOC_DAPM_PIN_SWITCH("Headset Right"),
	SOC_DAPM_PIN_SWITCH("Earpiece"),
	SOC_DAPM_PIN_SWITCH("Speaker Left"),
	SOC_DAPM_PIN_SWITCH("Speaker Right"),
	SOC_DAPM_PIN_SWITCH("LineOut Left"),
	SOC_DAPM_PIN_SWITCH("LineOut Right"),
	SOC_DAPM_PIN_SWITCH("Vibra 1"),
	SOC_DAPM_PIN_SWITCH("Vibra 2"),
	SOC_DAPM_PIN_SWITCH("Mic 1"),
	SOC_DAPM_PIN_SWITCH("Mic 2"),
	SOC_DAPM_PIN_SWITCH("LineIn Left"),
	SOC_DAPM_PIN_SWITCH("LineIn Right"),
	SOC_DAPM_PIN_SWITCH("DMic 1"),
	SOC_DAPM_PIN_SWITCH("DMic 2"),
	SOC_DAPM_PIN_SWITCH("DMic 3"),
	SOC_DAPM_PIN_SWITCH("DMic 4"),
	SOC_DAPM_PIN_SWITCH("DMic 5"),
	SOC_DAPM_PIN_SWITCH("DMic 6"),
};

/* ASoC */

int mop500_ab8500_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;

	/* Set audio-clock source */
	return mop500_ab8500_set_mclk(rtd->card->dev,
				snd_soc_card_get_drvdata(rtd->card));
}

void mop500_ab8500_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct device *dev = rtd->card->dev;

	dev_dbg(dev, "%s: Enter\n", __func__);

	/* Reset slots configuration to default(s) */
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		tx_slots = DEF_TX_SLOTS;
	else
		rx_slots = DEF_RX_SLOTS;
}

int mop500_ab8500_hw_params(struct snd_pcm_substream *substream,
			struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct device *dev = rtd->card->dev;
	unsigned int fmt;
	int channels, ret = 0, driver_mode, slots;
	unsigned int sw_codec, sw_cpu;
	bool is_playback;

	dev_dbg(dev, "%s: Enter\n", __func__);

	dev_dbg(dev, "%s: substream->pcm->name = %s\n"
		"substream->pcm->id = %s.\n"
		"substream->name = %s.\n"
		"substream->number = %d.\n",
		__func__,
		substream->pcm->name,
		substream->pcm->id,
		substream->name,
		substream->number);

	channels = params_channels(params);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S32_LE:
		sw_cpu = 32;
		break;

	case SNDRV_PCM_FORMAT_S16_LE:
		sw_cpu = 16;
		break;

	default:
		return -EINVAL;
	}

	/* Setup codec depending on driver-mode */
	if (channels == 8)
		driver_mode = DRIVERMODE_CODEC_ONLY;
	else
		driver_mode = DRIVERMODE_NORMAL;
	dev_dbg(dev, "%s: Driver-mode: %s.\n", __func__,
		(driver_mode == DRIVERMODE_NORMAL) ? "NORMAL" : "CODEC_ONLY");

	/* Setup format */

	if (driver_mode == DRIVERMODE_NORMAL) {
		fmt = SND_SOC_DAIFMT_DSP_A |
			SND_SOC_DAIFMT_CBM_CFM |
			SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CONT;
	} else {
		fmt = SND_SOC_DAIFMT_DSP_A |
			SND_SOC_DAIFMT_CBM_CFM |
			SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_GATED;
	}

	ret = snd_soc_dai_set_fmt(codec_dai, fmt);
	if (ret < 0) {
		dev_err(dev,
			"%s: ERROR: snd_soc_dai_set_fmt failed for codec_dai (ret = %d)!\n",
			__func__, ret);
		return ret;
	}

	ret = snd_soc_dai_set_fmt(cpu_dai, fmt);
	if (ret < 0) {
		dev_err(dev,
			"%s: ERROR: snd_soc_dai_set_fmt failed for cpu_dai (ret = %d)!\n",
			__func__, ret);
		return ret;
	}

	/* Setup TDM-slots */

	is_playback = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK);
	switch (channels) {
	case 1:
		slots = 16;
		tx_slots = (is_playback) ? TX_SLOT_MONO : 0;
		rx_slots = (is_playback) ? 0 : RX_SLOT_MONO;
		break;
	case 2:
		slots = 16;
		tx_slots = (is_playback) ? TX_SLOT_STEREO : 0;
		rx_slots = (is_playback) ? 0 : RX_SLOT_STEREO;
		break;
	case 8:
		slots = 16;
		tx_slots = (is_playback) ? TX_SLOT_8CH : 0;
		rx_slots = (is_playback) ? 0 : RX_SLOT_8CH;
		break;
	default:
		return -EINVAL;
	}

	if (driver_mode == DRIVERMODE_NORMAL)
		sw_codec = sw_cpu;
	else
		sw_codec = 20;

	dev_dbg(dev, "%s: CPU-DAI TDM: TX=0x%04X RX=0x%04x\n", __func__,
		tx_slots, rx_slots);
	ret = snd_soc_dai_set_tdm_slot(cpu_dai, tx_slots, rx_slots, slots,
				sw_cpu);
	if (ret)
		return ret;

	dev_dbg(dev, "%s: CODEC-DAI TDM: TX=0x%04X RX=0x%04x\n", __func__,
		tx_slots, rx_slots);
	ret = snd_soc_dai_set_tdm_slot(codec_dai, tx_slots, rx_slots, slots,
				sw_codec);
	if (ret)
		return ret;

	return 0;
}

struct snd_soc_ops mop500_ab8500_ops[] = {
	{
		.hw_params = mop500_ab8500_hw_params,
		.startup = mop500_ab8500_startup,
		.shutdown = mop500_ab8500_shutdown,
	}
};

int mop500_ab8500_machine_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	struct device *dev = rtd->card->dev;
	struct mop500_ab8500_drvdata *drvdata;
	int ret;

	dev_dbg(dev, "%s Enter.\n", __func__);

	/* Create driver private-data struct */
	drvdata = devm_kzalloc(dev, sizeof(struct mop500_ab8500_drvdata),
			GFP_KERNEL);
	snd_soc_card_set_drvdata(rtd->card, drvdata);

	/* Setup clocks */

	drvdata->clk_ptr_sysclk = clk_get(dev, "sysclk");
	if (IS_ERR(drvdata->clk_ptr_sysclk))
		dev_warn(dev, "%s: WARNING: clk_get failed for 'sysclk'!\n",
			__func__);
	drvdata->clk_ptr_ulpclk = clk_get(dev, "ulpclk");
	if (IS_ERR(drvdata->clk_ptr_ulpclk))
		dev_warn(dev, "%s: WARNING: clk_get failed for 'ulpclk'!\n",
			__func__);
	drvdata->clk_ptr_intclk = clk_get(dev, "intclk");
	if (IS_ERR(drvdata->clk_ptr_intclk))
		dev_warn(dev, "%s: WARNING: clk_get failed for 'intclk'!\n",
			__func__);

	/* Set intclk default parent to ulpclk */
	drvdata->mclk_sel = MCLK_ULPCLK;
	ret = mop500_ab8500_set_mclk(dev, drvdata);
	if (ret < 0)
		dev_warn(dev, "%s: WARNING: mop500_ab8500_set_mclk!\n",
			__func__);

	drvdata->mclk_sel = MCLK_ULPCLK;

	/* Add controls */
	ret = snd_soc_add_card_controls(codec->card, mop500_ab8500_ctrls,
			ARRAY_SIZE(mop500_ab8500_ctrls));
	if (ret < 0) {
		pr_err("%s: Failed to add machine-controls (%d)!\n",
				__func__, ret);
		return ret;
	}

	ret = snd_soc_dapm_disable_pin(&codec->dapm, "Earpiece");
	ret |= snd_soc_dapm_disable_pin(&codec->dapm, "Speaker Left");
	ret |= snd_soc_dapm_disable_pin(&codec->dapm, "Speaker Right");
	ret |= snd_soc_dapm_disable_pin(&codec->dapm, "LineOut Left");
	ret |= snd_soc_dapm_disable_pin(&codec->dapm, "LineOut Right");
	ret |= snd_soc_dapm_disable_pin(&codec->dapm, "Vibra 1");
	ret |= snd_soc_dapm_disable_pin(&codec->dapm, "Vibra 2");
	ret |= snd_soc_dapm_disable_pin(&codec->dapm, "Mic 1");
	ret |= snd_soc_dapm_disable_pin(&codec->dapm, "Mic 2");
	ret |= snd_soc_dapm_disable_pin(&codec->dapm, "LineIn Left");
	ret |= snd_soc_dapm_disable_pin(&codec->dapm, "LineIn Right");
	ret |= snd_soc_dapm_disable_pin(&codec->dapm, "DMic 1");
	ret |= snd_soc_dapm_disable_pin(&codec->dapm, "DMic 2");
	ret |= snd_soc_dapm_disable_pin(&codec->dapm, "DMic 3");
	ret |= snd_soc_dapm_disable_pin(&codec->dapm, "DMic 4");
	ret |= snd_soc_dapm_disable_pin(&codec->dapm, "DMic 5");
	ret |= snd_soc_dapm_disable_pin(&codec->dapm, "DMic 6");

	return ret;
}

void mop500_ab8500_remove(struct snd_soc_card *card)
{
	struct mop500_ab8500_drvdata *drvdata = snd_soc_card_get_drvdata(card);

	if (drvdata->clk_ptr_sysclk != NULL)
		clk_put(drvdata->clk_ptr_sysclk);
	if (drvdata->clk_ptr_ulpclk != NULL)
		clk_put(drvdata->clk_ptr_ulpclk);
	if (drvdata->clk_ptr_intclk != NULL)
		clk_put(drvdata->clk_ptr_intclk);

	snd_soc_card_set_drvdata(card, drvdata);
}
