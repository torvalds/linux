// SPDX-License-Identifier: GPL-2.0
//
// CS42L43 CODEC driver
//
// Copyright (C) 2022-2023 Cirrus Logic, Inc. and
//                         Cirrus Logic International Semiconductor Ltd.

#include <linux/bitops.h>
#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/find.h>
#include <linux/gcd.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/jiffies.h>
#include <linux/mfd/cs42l43.h>
#include <linux/mfd/cs42l43-regs.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/string.h>
#include <linux/workqueue.h>
#include <sound/control.h>
#include <sound/cs42l43.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc-component.h>
#include <sound/soc-dapm.h>
#include <sound/soc-dai.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#include "cs42l43.h"

#define CS42L43_DECL_MUX(name, reg) \
static SOC_VALUE_ENUM_SINGLE_DECL(cs42l43_##name##_enum, reg, \
				  0, CS42L43_MIXER_SRC_MASK, \
				  cs42l43_mixer_texts, cs42l43_mixer_values); \
static const struct snd_kcontrol_new cs42l43_##name##_mux = \
		SOC_DAPM_ENUM("Route", cs42l43_##name##_enum)

#define CS42L43_DECL_MIXER(name, reg) \
	CS42L43_DECL_MUX(name##_in1, reg); \
	CS42L43_DECL_MUX(name##_in2, reg + 0x4); \
	CS42L43_DECL_MUX(name##_in3, reg + 0x8); \
	CS42L43_DECL_MUX(name##_in4, reg + 0xC)

#define CS42L43_DAPM_MUX(name_str, name) \
	SND_SOC_DAPM_MUX(name_str " Input", SND_SOC_NOPM, 0, 0, &cs42l43_##name##_mux)

#define CS42L43_DAPM_MIXER(name_str, name) \
	SND_SOC_DAPM_MUX(name_str " Input 1", SND_SOC_NOPM, 0, 0, &cs42l43_##name##_in1_mux), \
	SND_SOC_DAPM_MUX(name_str " Input 2", SND_SOC_NOPM, 0, 0, &cs42l43_##name##_in2_mux), \
	SND_SOC_DAPM_MUX(name_str " Input 3", SND_SOC_NOPM, 0, 0, &cs42l43_##name##_in3_mux), \
	SND_SOC_DAPM_MUX(name_str " Input 4", SND_SOC_NOPM, 0, 0, &cs42l43_##name##_in4_mux), \
	SND_SOC_DAPM_MIXER(name_str " Mixer", SND_SOC_NOPM, 0, 0, NULL, 0)

#define CS42L43_BASE_ROUTES(name_str) \
	{ name_str,		"Tone Generator 1",	"Tone 1" }, \
	{ name_str,		"Tone Generator 2",	"Tone 2" }, \
	{ name_str,		"Decimator 1",		"Decimator 1" }, \
	{ name_str,		"Decimator 2",		"Decimator 2" }, \
	{ name_str,		"Decimator 3",		"Decimator 3" }, \
	{ name_str,		"Decimator 4",		"Decimator 4" }, \
	{ name_str,		"ASPRX1",		"ASPRX1" }, \
	{ name_str,		"ASPRX2",		"ASPRX2" }, \
	{ name_str,		"ASPRX3",		"ASPRX3" }, \
	{ name_str,		"ASPRX4",		"ASPRX4" }, \
	{ name_str,		"ASPRX5",		"ASPRX5" }, \
	{ name_str,		"ASPRX6",		"ASPRX6" }, \
	{ name_str,		"DP5RX1",		"DP5RX1" }, \
	{ name_str,		"DP5RX2",		"DP5RX2" }, \
	{ name_str,		"DP6RX1",		"DP6RX1" }, \
	{ name_str,		"DP6RX2",		"DP6RX2" }, \
	{ name_str,		"DP7RX1",		"DP7RX1" }, \
	{ name_str,		"DP7RX2",		"DP7RX2" }, \
	{ name_str,		"ASRC INT1",		"ASRC_INT1" }, \
	{ name_str,		"ASRC INT2",		"ASRC_INT2" }, \
	{ name_str,		"ASRC INT3",		"ASRC_INT3" }, \
	{ name_str,		"ASRC INT4",		"ASRC_INT4" }, \
	{ name_str,		"ASRC DEC1",		"ASRC_DEC1" }, \
	{ name_str,		"ASRC DEC2",		"ASRC_DEC2" }, \
	{ name_str,		"ASRC DEC3",		"ASRC_DEC3" }, \
	{ name_str,		"ASRC DEC4",		"ASRC_DEC4" }, \
	{ name_str,		"ISRC1 INT1",		"ISRC1INT1" }, \
	{ name_str,		"ISRC1 INT2",		"ISRC1INT2" }, \
	{ name_str,		"ISRC1 DEC1",		"ISRC1DEC1" }, \
	{ name_str,		"ISRC1 DEC2",		"ISRC1DEC2" }, \
	{ name_str,		"ISRC2 INT1",		"ISRC2INT1" }, \
	{ name_str,		"ISRC2 INT2",		"ISRC2INT2" }, \
	{ name_str,		"ISRC2 DEC1",		"ISRC2DEC1" }, \
	{ name_str,		"ISRC2 DEC2",		"ISRC2DEC2" }, \
	{ name_str,		"EQ1",			"EQ" }, \
	{ name_str,		"EQ2",			"EQ" }

#define CS42L43_MUX_ROUTES(name_str, widget) \
	{ widget,		NULL,			name_str " Input" }, \
	{ name_str " Input",	NULL,			"Mixer Core" }, \
	CS42L43_BASE_ROUTES(name_str " Input")

#define CS42L43_MIXER_ROUTES(name_str, widget) \
	{ name_str " Mixer",	NULL,			name_str " Input 1" }, \
	{ name_str " Mixer",	NULL,			name_str " Input 2" }, \
	{ name_str " Mixer",	NULL,			name_str " Input 3" }, \
	{ name_str " Mixer",	NULL,			name_str " Input 4" }, \
	{ widget,		NULL,			name_str " Mixer" }, \
	{ name_str " Mixer",	NULL,			"Mixer Core" }, \
	CS42L43_BASE_ROUTES(name_str " Input 1"), \
	CS42L43_BASE_ROUTES(name_str " Input 2"), \
	CS42L43_BASE_ROUTES(name_str " Input 3"), \
	CS42L43_BASE_ROUTES(name_str " Input 4")

#define CS42L43_MIXER_VOLUMES(name_str, base) \
	SOC_SINGLE_RANGE_TLV(name_str " Input 1 Volume", base, \
			     CS42L43_MIXER_VOL_SHIFT, 0x20, 0x50, 0, \
			     cs42l43_mixer_tlv), \
	SOC_SINGLE_RANGE_TLV(name_str " Input 2 Volume", base + 4, \
			     CS42L43_MIXER_VOL_SHIFT, 0x20, 0x50, 0, \
			     cs42l43_mixer_tlv), \
	SOC_SINGLE_RANGE_TLV(name_str " Input 3 Volume", base + 8, \
			     CS42L43_MIXER_VOL_SHIFT, 0x20, 0x50, 0, \
			     cs42l43_mixer_tlv), \
	SOC_SINGLE_RANGE_TLV(name_str " Input 4 Volume", base + 12, \
			     CS42L43_MIXER_VOL_SHIFT, 0x20, 0x50, 0, \
			     cs42l43_mixer_tlv)

#define CS42L43_IRQ_ERROR(name) \
static irqreturn_t cs42l43_##name(int irq, void *data) \
{ \
	struct cs42l43_codec *priv = data; \
	dev_err(priv->dev, "Error " #name " IRQ\n"); \
	return IRQ_HANDLED; \
}

CS42L43_IRQ_ERROR(pll_lost_lock)
CS42L43_IRQ_ERROR(spkr_clock_stop)
CS42L43_IRQ_ERROR(spkl_clock_stop)
CS42L43_IRQ_ERROR(spkr_brown_out)
CS42L43_IRQ_ERROR(spkl_brown_out)
CS42L43_IRQ_ERROR(spkr_therm_shutdown)
CS42L43_IRQ_ERROR(spkl_therm_shutdown)
CS42L43_IRQ_ERROR(spkr_therm_warm)
CS42L43_IRQ_ERROR(spkl_therm_warm)
CS42L43_IRQ_ERROR(spkr_sc_detect)
CS42L43_IRQ_ERROR(spkl_sc_detect)

static void cs42l43_hp_ilimit_clear_work(struct work_struct *work)
{
	struct cs42l43_codec *priv = container_of(work, struct cs42l43_codec,
						  hp_ilimit_clear_work.work);
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(priv->component);

	snd_soc_dapm_mutex_lock(dapm);

	priv->hp_ilimit_count--;

	if (priv->hp_ilimit_count)
		queue_delayed_work(system_wq, &priv->hp_ilimit_clear_work,
				   msecs_to_jiffies(CS42L43_HP_ILIMIT_DECAY_MS));

	snd_soc_dapm_mutex_unlock(dapm);
}

static void cs42l43_hp_ilimit_work(struct work_struct *work)
{
	struct cs42l43_codec *priv = container_of(work, struct cs42l43_codec,
						  hp_ilimit_work);
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(priv->component);
	struct cs42l43 *cs42l43 = priv->core;

	snd_soc_dapm_mutex_lock(dapm);

	if (priv->hp_ilimit_count < CS42L43_HP_ILIMIT_MAX_COUNT) {
		if (!priv->hp_ilimit_count)
			queue_delayed_work(system_wq, &priv->hp_ilimit_clear_work,
					   msecs_to_jiffies(CS42L43_HP_ILIMIT_DECAY_MS));

		priv->hp_ilimit_count++;
		snd_soc_dapm_mutex_unlock(dapm);
		return;
	}

	dev_err(priv->dev, "Disabling headphone for %dmS, due to frequent current limit\n",
		CS42L43_HP_ILIMIT_BACKOFF_MS);

	priv->hp_ilimited = true;

	// No need to wait for disable, as just disabling for a period of time
	regmap_update_bits(cs42l43->regmap, CS42L43_BLOCK_EN8,
			   CS42L43_HP_EN_MASK, 0);

	snd_soc_dapm_mutex_unlock(dapm);

	msleep(CS42L43_HP_ILIMIT_BACKOFF_MS);

	snd_soc_dapm_mutex_lock(dapm);

	if (priv->hp_ena && !priv->load_detect_running) {
		unsigned long time_left;

		reinit_completion(&priv->hp_startup);

		regmap_update_bits(cs42l43->regmap, CS42L43_BLOCK_EN8,
				   CS42L43_HP_EN_MASK, priv->hp_ena);

		time_left = wait_for_completion_timeout(&priv->hp_startup,
							msecs_to_jiffies(CS42L43_HP_TIMEOUT_MS));
		if (!time_left)
			dev_err(priv->dev, "ilimit HP restore timed out\n");
	}

	priv->hp_ilimited = false;

	snd_soc_dapm_mutex_unlock(dapm);
}

static irqreturn_t cs42l43_hp_ilimit(int irq, void *data)
{
	struct cs42l43_codec *priv = data;

	dev_dbg(priv->dev, "headphone ilimit IRQ\n");

	queue_work(system_long_wq, &priv->hp_ilimit_work);

	return IRQ_HANDLED;
}

#define CS42L43_IRQ_COMPLETE(name) \
static irqreturn_t cs42l43_##name(int irq, void *data) \
{ \
	struct cs42l43_codec *priv = data; \
	dev_dbg(priv->dev, #name " completed\n"); \
	complete(&priv->name); \
	return IRQ_HANDLED; \
}

CS42L43_IRQ_COMPLETE(pll_ready)
CS42L43_IRQ_COMPLETE(hp_startup)
CS42L43_IRQ_COMPLETE(hp_shutdown)
CS42L43_IRQ_COMPLETE(type_detect)
CS42L43_IRQ_COMPLETE(spkr_shutdown)
CS42L43_IRQ_COMPLETE(spkl_shutdown)
CS42L43_IRQ_COMPLETE(spkr_startup)
CS42L43_IRQ_COMPLETE(spkl_startup)
CS42L43_IRQ_COMPLETE(load_detect)

static irqreturn_t cs42l43_mic_shutter(int irq, void *data)
{
	struct cs42l43_codec *priv = data;
	static const char * const controls[] = {
		"Decimator 1 Switch",
		"Decimator 2 Switch",
		"Decimator 3 Switch",
		"Decimator 4 Switch",
	};
	int i, ret;

	dev_dbg(priv->dev, "Microphone shutter changed\n");

	if (!priv->component)
		return IRQ_NONE;

	for (i = 0; i < ARRAY_SIZE(controls); i++) {
		ret = snd_soc_component_notify_control(priv->component,
						       controls[i]);
		if (ret)
			return IRQ_NONE;
	}

	return IRQ_HANDLED;
}

static irqreturn_t cs42l43_spk_shutter(int irq, void *data)
{
	struct cs42l43_codec *priv = data;
	int ret;

	dev_dbg(priv->dev, "Speaker shutter changed\n");

	if (!priv->component)
		return IRQ_NONE;

	ret = snd_soc_component_notify_control(priv->component,
					       "Speaker Digital Switch");
	if (ret)
		return IRQ_NONE;

	return IRQ_HANDLED;
}

static const unsigned int cs42l43_sample_rates[] = {
	8000, 16000, 24000, 32000, 44100, 48000, 96000, 192000,
};

#define CS42L43_CONSUMER_RATE_MASK 0xFF
#define CS42L43_PROVIDER_RATE_MASK 0xEF // 44.1k only supported as consumer

static const struct snd_pcm_hw_constraint_list cs42l43_constraint = {
	.count		= ARRAY_SIZE(cs42l43_sample_rates),
	.list		= cs42l43_sample_rates,
};

static int cs42l43_startup(struct snd_pcm_substream *substream, struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct cs42l43_codec *priv = snd_soc_component_get_drvdata(component);
	struct cs42l43 *cs42l43 = priv->core;
	int provider = !dai->id || !!regmap_test_bits(cs42l43->regmap,
						      CS42L43_ASP_CLK_CONFIG2,
						      CS42L43_ASP_MASTER_MODE_MASK);

	if (provider)
		priv->constraint.mask = CS42L43_PROVIDER_RATE_MASK;
	else
		priv->constraint.mask = CS42L43_CONSUMER_RATE_MASK;

	return snd_pcm_hw_constraint_list(substream->runtime, 0,
					  SNDRV_PCM_HW_PARAM_RATE,
					  &priv->constraint);
}

static int cs42l43_convert_sample_rate(unsigned int rate)
{
	switch (rate) {
	case 8000:
		return 0x11;
	case 16000:
		return 0x12;
	case 24000:
		return 0x02;
	case 32000:
		return 0x13;
	case 44100:
		return 0x0B;
	case 48000:
		return 0x03;
	case 96000:
		return 0x04;
	case 192000:
		return 0x05;
	default:
		return -EINVAL;
	}
}

static int cs42l43_set_sample_rate(struct snd_pcm_substream *substream,
				   struct snd_pcm_hw_params *params,
				   struct snd_soc_dai *dai)
{
	struct cs42l43_codec *priv = snd_soc_component_get_drvdata(dai->component);
	struct cs42l43 *cs42l43 = priv->core;
	int ret;

	ret = cs42l43_convert_sample_rate(params_rate(params));
	if (ret < 0) {
		dev_err(priv->dev, "Failed to convert sample rate: %d\n", ret);
		return ret;
	}

	//FIXME: For now lets just set sample rate 1, this needs expanded in the future
	regmap_update_bits(cs42l43->regmap, CS42L43_SAMPLE_RATE1,
			   CS42L43_SAMPLE_RATE_MASK, ret);

	return 0;
}

static int cs42l43_asp_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct cs42l43_codec *priv = snd_soc_component_get_drvdata(dai->component);
	struct cs42l43 *cs42l43 = priv->core;
	int dsp_mode = !!regmap_test_bits(cs42l43->regmap, CS42L43_ASP_CTRL,
					  CS42L43_ASP_FSYNC_MODE_MASK);
	int provider = !!regmap_test_bits(cs42l43->regmap, CS42L43_ASP_CLK_CONFIG2,
					  CS42L43_ASP_MASTER_MODE_MASK);
	int n_chans = params_channels(params);
	int data_width = params_width(params);
	int n_slots = n_chans;
	int slot_width = data_width;
	int frame, bclk_target, i;
	unsigned int reg;
	int *slots;

	if (priv->n_slots) {
		n_slots = priv->n_slots;
		slot_width = priv->slot_width;
	}

	if (!dsp_mode && (n_slots & 0x1)) {
		dev_dbg(priv->dev, "Forcing balanced channels on ASP\n");
		n_slots++;
	}

	frame = n_slots * slot_width;
	bclk_target = params_rate(params) * frame;

	if (provider) {
		unsigned int gcd_nm = gcd(bclk_target, CS42L43_INTERNAL_SYSCLK);
		int n = bclk_target / gcd_nm;
		int m = CS42L43_INTERNAL_SYSCLK / gcd_nm;

		if (n > (CS42L43_ASP_BCLK_N_MASK >> CS42L43_ASP_BCLK_N_SHIFT) ||
		    m > CS42L43_ASP_BCLK_M_MASK) {
			dev_err(priv->dev, "Can't produce %dHz bclk\n", bclk_target);
			return -EINVAL;
		}

		dev_dbg(priv->dev, "bclk %d/%d = %dHz, with %dx%d frame\n",
			n, m, bclk_target, n_slots, slot_width);

		regmap_update_bits(cs42l43->regmap, CS42L43_ASP_CLK_CONFIG1,
				   CS42L43_ASP_BCLK_N_MASK | CS42L43_ASP_BCLK_M_MASK,
				   n << CS42L43_ASP_BCLK_N_SHIFT |
				   m << CS42L43_ASP_BCLK_M_SHIFT);
		regmap_update_bits(cs42l43->regmap, CS42L43_ASP_FSYNC_CTRL1,
				   CS42L43_ASP_FSYNC_M_MASK, frame);
	}

	regmap_update_bits(cs42l43->regmap, CS42L43_ASP_FSYNC_CTRL4,
			   CS42L43_ASP_NUM_BCLKS_PER_FSYNC_MASK,
			   frame << CS42L43_ASP_NUM_BCLKS_PER_FSYNC_SHIFT);

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		reg = CS42L43_ASP_TX_CH1_CTRL;
		slots = priv->tx_slots;
	} else {
		reg = CS42L43_ASP_RX_CH1_CTRL;
		slots = priv->rx_slots;
	}

	for (i = 0; i < n_chans; i++, reg += 4) {
		int slot_phase = dsp_mode | (i & CS42L43_ASP_CH_SLOT_PHASE_MASK);
		int slot_pos;

		if (dsp_mode)
			slot_pos = slots[i] * slot_width;
		else
			slot_pos = (slots[i] / 2) * slot_width;

		dev_dbg(priv->dev, "Configure channel %d at slot %d (%d,%d)\n",
			i, slots[i], slot_pos, slot_phase);

		regmap_update_bits(cs42l43->regmap, reg,
				   CS42L43_ASP_CH_WIDTH_MASK |
				   CS42L43_ASP_CH_SLOT_MASK |
				   CS42L43_ASP_CH_SLOT_PHASE_MASK,
				   ((data_width - 1) << CS42L43_ASP_CH_WIDTH_SHIFT) |
				   (slot_pos << CS42L43_ASP_CH_SLOT_SHIFT) |
				   slot_phase);
	}

	return cs42l43_set_sample_rate(substream, params, dai);
}

static int cs42l43_asp_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *component = dai->component;
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);
	struct cs42l43_codec *priv = snd_soc_component_get_drvdata(component);
	struct cs42l43 *cs42l43 = priv->core;
	int provider = regmap_test_bits(cs42l43->regmap, CS42L43_ASP_CLK_CONFIG2,
					CS42L43_ASP_MASTER_MODE_MASK);
	struct snd_soc_dapm_route routes[] = {
		{ "BCLK", NULL, "FSYNC" },
	};
	unsigned int asp_ctrl = 0;
	unsigned int data_ctrl = 0;
	unsigned int fsync_ctrl = 0;
	unsigned int clk_config = 0;

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
		data_ctrl |= 2 << CS42L43_ASP_FSYNC_FRAME_START_DLY_SHIFT;
		fallthrough;
	case SND_SOC_DAIFMT_DSP_B:
		asp_ctrl |= CS42L43_ASP_FSYNC_MODE_MASK;
		data_ctrl |= CS42L43_ASP_FSYNC_FRAME_START_PHASE_MASK;
		break;
	case SND_SOC_DAIFMT_I2S:
		data_ctrl |= 2 << CS42L43_ASP_FSYNC_FRAME_START_DLY_SHIFT;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		data_ctrl |= CS42L43_ASP_FSYNC_FRAME_START_PHASE_MASK;
		break;
	default:
		dev_err(priv->dev, "Unsupported DAI format 0x%x\n",
			fmt & SND_SOC_DAIFMT_FORMAT_MASK);
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_CBC_CFC:
		if (provider)
			snd_soc_dapm_del_routes(dapm, routes, ARRAY_SIZE(routes));
		break;
	case SND_SOC_DAIFMT_CBP_CFP:
		if (!provider)
			snd_soc_dapm_add_routes(dapm, routes, ARRAY_SIZE(routes));
		clk_config |= CS42L43_ASP_MASTER_MODE_MASK;
		break;
	default:
		dev_err(priv->dev, "Unsupported ASP mode 0x%x\n",
			fmt & SND_SOC_DAIFMT_MASTER_MASK);
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		clk_config |= CS42L43_ASP_BCLK_INV_MASK; /* Yes BCLK_INV = NB */
		break;
	case SND_SOC_DAIFMT_IB_NF:
		break;
	case SND_SOC_DAIFMT_NB_IF:
		clk_config |= CS42L43_ASP_BCLK_INV_MASK;
		fsync_ctrl |= CS42L43_ASP_FSYNC_IN_INV_MASK |
			      CS42L43_ASP_FSYNC_OUT_INV_MASK;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		fsync_ctrl |= CS42L43_ASP_FSYNC_IN_INV_MASK |
			      CS42L43_ASP_FSYNC_OUT_INV_MASK;
		break;
	default:
		dev_err(priv->dev, "Unsupported invert mode 0x%x\n",
			fmt & SND_SOC_DAIFMT_INV_MASK);
		return -EINVAL;
	}

	regmap_update_bits(cs42l43->regmap, CS42L43_ASP_CTRL,
			   CS42L43_ASP_FSYNC_MODE_MASK,
			   asp_ctrl);
	regmap_update_bits(cs42l43->regmap, CS42L43_ASP_DATA_CTRL,
			   CS42L43_ASP_FSYNC_FRAME_START_DLY_MASK |
			   CS42L43_ASP_FSYNC_FRAME_START_PHASE_MASK,
			   data_ctrl);
	regmap_update_bits(cs42l43->regmap, CS42L43_ASP_CLK_CONFIG2,
			   CS42L43_ASP_MASTER_MODE_MASK |
			   CS42L43_ASP_BCLK_INV_MASK,
			   clk_config);
	regmap_update_bits(cs42l43->regmap, CS42L43_ASP_FSYNC_CTRL3,
			   CS42L43_ASP_FSYNC_IN_INV_MASK |
			   CS42L43_ASP_FSYNC_OUT_INV_MASK,
			   fsync_ctrl);

	return 0;
}

static void cs42l43_mask_to_slots(struct cs42l43_codec *priv, unsigned long mask,
				  int *slots, unsigned int nslots)
{
	int i = 0;
	int slot;

	for_each_set_bit(slot, &mask, BITS_PER_TYPE(mask)) {
		if (i == nslots) {
			dev_warn(priv->dev, "Too many channels in TDM mask: %lx\n",
				 mask);
			return;
		}

		slots[i++] = slot;
	}

}

static int cs42l43_asp_set_tdm_slot(struct snd_soc_dai *dai, unsigned int tx_mask,
				    unsigned int rx_mask, int slots, int slot_width)
{
	struct snd_soc_component *component = dai->component;
	struct cs42l43_codec *priv = snd_soc_component_get_drvdata(component);

	priv->n_slots = slots;
	priv->slot_width = slot_width;

	if (!slots) {
		tx_mask = CS42L43_DEFAULT_SLOTS;
		rx_mask = CS42L43_DEFAULT_SLOTS;
	}

	cs42l43_mask_to_slots(priv, tx_mask, priv->tx_slots,
			      ARRAY_SIZE(priv->tx_slots));
	cs42l43_mask_to_slots(priv, rx_mask, priv->rx_slots,
			      ARRAY_SIZE(priv->rx_slots));

	return 0;
}

static const struct snd_soc_dai_ops cs42l43_asp_ops = {
	.startup	= cs42l43_startup,
	.hw_params	= cs42l43_asp_hw_params,
	.set_fmt	= cs42l43_asp_set_fmt,
	.set_tdm_slot	= cs42l43_asp_set_tdm_slot,
};

static int cs42l43_sdw_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	int ret;

	ret = cs42l43_sdw_add_peripheral(substream, params, dai);
	if (ret)
		return ret;

	return cs42l43_set_sample_rate(substream, params, dai);
};

static const struct snd_soc_dai_ops cs42l43_sdw_ops = {
	.startup	= cs42l43_startup,
	.set_stream	= cs42l43_sdw_set_stream,
	.hw_params	= cs42l43_sdw_hw_params,
	.hw_free	= cs42l43_sdw_remove_peripheral,
};

#define CS42L43_ASP_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE | \
			     SNDRV_PCM_FMTBIT_S32_LE)
#define CS42L43_SDW_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE)

static struct snd_soc_dai_driver cs42l43_dais[] = {
	{
		.name			= "cs42l43-asp",
		.ops			= &cs42l43_asp_ops,
		.symmetric_rate		= 1,
		.capture = {
			.stream_name	= "ASP Capture",
			.channels_min	= 1,
			.channels_max	= CS42L43_ASP_MAX_CHANNELS,
			.rates		= SNDRV_PCM_RATE_KNOT,
			.formats	= CS42L43_ASP_FORMATS,
		},
		.playback = {
			.stream_name	= "ASP Playback",
			.channels_min	= 1,
			.channels_max	= CS42L43_ASP_MAX_CHANNELS,
			.rates		= SNDRV_PCM_RATE_KNOT,
			.formats	= CS42L43_ASP_FORMATS,
		},
	},
	{
		.name			= "cs42l43-dp1",
		.id			= 1,
		.ops			= &cs42l43_sdw_ops,
		.capture = {
			.stream_name	= "DP1 Capture",
			.channels_min	= 1,
			.channels_max	= 4,
			.rates		= SNDRV_PCM_RATE_KNOT,
			.formats	= CS42L43_SDW_FORMATS,
		},
	},
	{
		.name			= "cs42l43-dp2",
		.id			= 2,
		.ops			= &cs42l43_sdw_ops,
		.capture = {
			.stream_name	= "DP2 Capture",
			.channels_min	= 1,
			.channels_max	= 2,
			.rates		= SNDRV_PCM_RATE_KNOT,
			.formats	= CS42L43_SDW_FORMATS,
		},
	},
	{
		.name			= "cs42l43-dp3",
		.id			= 3,
		.ops			= &cs42l43_sdw_ops,
		.capture = {
			.stream_name	= "DP3 Capture",
			.channels_min	= 1,
			.channels_max	= 2,
			.rates		= SNDRV_PCM_RATE_KNOT,
			.formats	= CS42L43_SDW_FORMATS,
		},
	},
	{
		.name			= "cs42l43-dp4",
		.id			= 4,
		.ops			= &cs42l43_sdw_ops,
		.capture = {
			.stream_name	= "DP4 Capture",
			.channels_min	= 1,
			.channels_max	= 2,
			.rates		= SNDRV_PCM_RATE_KNOT,
			.formats	= CS42L43_SDW_FORMATS,
		},
	},
	{
		.name			= "cs42l43-dp5",
		.id			= 5,
		.ops			= &cs42l43_sdw_ops,
		.playback = {
			.stream_name	= "DP5 Playback",
			.channels_min	= 1,
			.channels_max	= 2,
			.rates		= SNDRV_PCM_RATE_KNOT,
			.formats	= CS42L43_SDW_FORMATS,
		},
	},
	{
		.name			= "cs42l43-dp6",
		.id			= 6,
		.ops			= &cs42l43_sdw_ops,
		.playback = {
			.stream_name	= "DP6 Playback",
			.channels_min	= 1,
			.channels_max	= 2,
			.rates		= SNDRV_PCM_RATE_KNOT,
			.formats	= CS42L43_SDW_FORMATS,
		},
	},
	{
		.name			= "cs42l43-dp7",
		.id			= 7,
		.ops			= &cs42l43_sdw_ops,
		.playback = {
			.stream_name	= "DP7 Playback",
			.channels_min	= 1,
			.channels_max	= 2,
			.rates		= SNDRV_PCM_RATE_KNOT,
			.formats	= CS42L43_SDW_FORMATS,
		},
	},
};

static const DECLARE_TLV_DB_SCALE(cs42l43_mixer_tlv, -3200, 100, 0);

static const char * const cs42l43_ramp_text[] = {
	"0ms/6dB", "0.5ms/6dB", "1ms/6dB", "2ms/6dB", "4ms/6dB", "8ms/6dB",
	"15ms/6dB", "30ms/6dB",
};

static const char * const cs42l43_adc1_input_text[] = { "IN1", "IN2" };

static SOC_ENUM_SINGLE_DECL(cs42l43_adc1_input, CS42L43_ADC_B_CTRL1,
			    CS42L43_ADC_AIN_SEL_SHIFT,
			    cs42l43_adc1_input_text);

static const struct snd_kcontrol_new cs42l43_adc1_input_ctl =
	SOC_DAPM_ENUM("ADC1 Input", cs42l43_adc1_input);

static const char * const cs42l43_dec_mode_text[] = { "ADC", "PDM" };

static SOC_ENUM_SINGLE_VIRT_DECL(cs42l43_dec1_mode, cs42l43_dec_mode_text);
static SOC_ENUM_SINGLE_VIRT_DECL(cs42l43_dec2_mode, cs42l43_dec_mode_text);

static const struct snd_kcontrol_new cs42l43_dec_mode_ctl[] = {
	SOC_DAPM_ENUM("Decimator 1 Mode", cs42l43_dec1_mode),
	SOC_DAPM_ENUM("Decimator 2 Mode", cs42l43_dec2_mode),
};

static const char * const cs42l43_pdm_clk_text[] = {
	"3.072MHz", "1.536MHz", "768kHz",
};

static SOC_ENUM_SINGLE_DECL(cs42l43_pdm1_clk, CS42L43_PDM_CONTROL,
			    CS42L43_PDM1_CLK_DIV_SHIFT, cs42l43_pdm_clk_text);
static SOC_ENUM_SINGLE_DECL(cs42l43_pdm2_clk, CS42L43_PDM_CONTROL,
			    CS42L43_PDM2_CLK_DIV_SHIFT, cs42l43_pdm_clk_text);

static DECLARE_TLV_DB_SCALE(cs42l43_adc_tlv, -600, 600, 0);
static DECLARE_TLV_DB_SCALE(cs42l43_dec_tlv, -6400, 50, 0);

static const char * const cs42l43_wnf_corner_text[] = {
	"160Hz", "180Hz", "200Hz", "220Hz", "240Hz", "260Hz", "280Hz", "300Hz",
};

static SOC_ENUM_SINGLE_DECL(cs42l43_dec1_wnf_corner, CS42L43_DECIM_HPF_WNF_CTRL1,
			    CS42L43_DECIM_WNF_CF_SHIFT, cs42l43_wnf_corner_text);
static SOC_ENUM_SINGLE_DECL(cs42l43_dec2_wnf_corner, CS42L43_DECIM_HPF_WNF_CTRL2,
			    CS42L43_DECIM_WNF_CF_SHIFT, cs42l43_wnf_corner_text);
static SOC_ENUM_SINGLE_DECL(cs42l43_dec3_wnf_corner, CS42L43_DECIM_HPF_WNF_CTRL3,
			    CS42L43_DECIM_WNF_CF_SHIFT, cs42l43_wnf_corner_text);
static SOC_ENUM_SINGLE_DECL(cs42l43_dec4_wnf_corner, CS42L43_DECIM_HPF_WNF_CTRL4,
			    CS42L43_DECIM_WNF_CF_SHIFT, cs42l43_wnf_corner_text);

static const char * const cs42l43_hpf_corner_text[] = {
	"3Hz", "12Hz", "48Hz", "96Hz",
};

static SOC_ENUM_SINGLE_DECL(cs42l43_dec1_hpf_corner, CS42L43_DECIM_HPF_WNF_CTRL1,
			    CS42L43_DECIM_HPF_CF_SHIFT, cs42l43_hpf_corner_text);
static SOC_ENUM_SINGLE_DECL(cs42l43_dec2_hpf_corner, CS42L43_DECIM_HPF_WNF_CTRL2,
			    CS42L43_DECIM_HPF_CF_SHIFT, cs42l43_hpf_corner_text);
static SOC_ENUM_SINGLE_DECL(cs42l43_dec3_hpf_corner, CS42L43_DECIM_HPF_WNF_CTRL3,
			    CS42L43_DECIM_HPF_CF_SHIFT, cs42l43_hpf_corner_text);
static SOC_ENUM_SINGLE_DECL(cs42l43_dec4_hpf_corner, CS42L43_DECIM_HPF_WNF_CTRL4,
			    CS42L43_DECIM_HPF_CF_SHIFT, cs42l43_hpf_corner_text);

static SOC_ENUM_SINGLE_DECL(cs42l43_dec1_ramp_up, CS42L43_DECIM_VOL_CTRL_CH1_CH2,
			    CS42L43_DECIM1_VI_RAMP_SHIFT, cs42l43_ramp_text);
static SOC_ENUM_SINGLE_DECL(cs42l43_dec1_ramp_down, CS42L43_DECIM_VOL_CTRL_CH1_CH2,
			    CS42L43_DECIM1_VD_RAMP_SHIFT, cs42l43_ramp_text);
static SOC_ENUM_SINGLE_DECL(cs42l43_dec2_ramp_up, CS42L43_DECIM_VOL_CTRL_CH1_CH2,
			    CS42L43_DECIM2_VI_RAMP_SHIFT, cs42l43_ramp_text);
static SOC_ENUM_SINGLE_DECL(cs42l43_dec2_ramp_down, CS42L43_DECIM_VOL_CTRL_CH1_CH2,
			    CS42L43_DECIM2_VD_RAMP_SHIFT, cs42l43_ramp_text);
static SOC_ENUM_SINGLE_DECL(cs42l43_dec3_ramp_up, CS42L43_DECIM_VOL_CTRL_CH3_CH4,
			    CS42L43_DECIM3_VI_RAMP_SHIFT, cs42l43_ramp_text);
static SOC_ENUM_SINGLE_DECL(cs42l43_dec3_ramp_down, CS42L43_DECIM_VOL_CTRL_CH3_CH4,
			    CS42L43_DECIM3_VD_RAMP_SHIFT, cs42l43_ramp_text);
static SOC_ENUM_SINGLE_DECL(cs42l43_dec4_ramp_up, CS42L43_DECIM_VOL_CTRL_CH3_CH4,
			    CS42L43_DECIM4_VI_RAMP_SHIFT, cs42l43_ramp_text);
static SOC_ENUM_SINGLE_DECL(cs42l43_dec4_ramp_down, CS42L43_DECIM_VOL_CTRL_CH3_CH4,
			    CS42L43_DECIM4_VD_RAMP_SHIFT, cs42l43_ramp_text);

static DECLARE_TLV_DB_SCALE(cs42l43_speaker_tlv, -6400, 50, 0);

static SOC_ENUM_SINGLE_DECL(cs42l43_speaker_ramp_up, CS42L43_AMP1_2_VOL_RAMP,
			    CS42L43_AMP1_2_VI_RAMP_SHIFT, cs42l43_ramp_text);

static SOC_ENUM_SINGLE_DECL(cs42l43_speaker_ramp_down, CS42L43_AMP1_2_VOL_RAMP,
			    CS42L43_AMP1_2_VD_RAMP_SHIFT, cs42l43_ramp_text);

static DECLARE_TLV_DB_SCALE(cs42l43_headphone_tlv, -11450, 50, 1);

static const char * const cs42l43_headphone_ramp_text[] = {
	"1", "2", "4", "6", "8", "11", "12", "16", "22", "24", "33", "36", "44",
	"48", "66", "72",
};

static SOC_ENUM_SINGLE_DECL(cs42l43_headphone_ramp, CS42L43_PGAVOL,
			    CS42L43_HP_PATH_VOL_RAMP_SHIFT,
			    cs42l43_headphone_ramp_text);

static const char * const cs42l43_tone_freq_text[] = {
	"1kHz", "2kHz", "4kHz", "6kHz", "8kHz",
};

static SOC_ENUM_SINGLE_DECL(cs42l43_tone1_freq, CS42L43_TONE_CH1_CTRL,
			    CS42L43_TONE_FREQ_SHIFT, cs42l43_tone_freq_text);

static SOC_ENUM_SINGLE_DECL(cs42l43_tone2_freq, CS42L43_TONE_CH2_CTRL,
			    CS42L43_TONE_FREQ_SHIFT, cs42l43_tone_freq_text);

static const char * const cs42l43_mixer_texts[] = {
	"None",
	"Tone Generator 1", "Tone Generator 2",
	"Decimator 1", "Decimator 2", "Decimator 3", "Decimator 4",
	"ASPRX1", "ASPRX2", "ASPRX3", "ASPRX4", "ASPRX5", "ASPRX6",
	"DP5RX1", "DP5RX2", "DP6RX1", "DP6RX2", "DP7RX1", "DP7RX2",
	"ASRC INT1", "ASRC INT2", "ASRC INT3", "ASRC INT4",
	"ASRC DEC1", "ASRC DEC2", "ASRC DEC3", "ASRC DEC4",
	"ISRC1 INT1", "ISRC1 INT2",
	"ISRC1 DEC1", "ISRC1 DEC2",
	"ISRC2 INT1", "ISRC2 INT2",
	"ISRC2 DEC1", "ISRC2 DEC2",
	"EQ1", "EQ2",
};

static const unsigned int cs42l43_mixer_values[] = {
	0x00, // None
	0x04, 0x05, // Tone Generator 1, 2
	0x10, 0x11, 0x12, 0x13, // Decimator 1, 2, 3, 4
	0x20, 0x21, 0x22, 0x23, 0x24, 0x25, // ASPRX1,2,3,4,5,6
	0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, // DP5, 6, 7RX1, 2
	0x40, 0x41, 0x42, 0x43, // ASRC INT1, 2, 3, 4
	0x44, 0x45, 0x46, 0x47, // ASRC DEC1, 2, 3, 4
	0x50, 0x51, // ISRC1 INT1, 2
	0x52, 0x53, // ISRC1 DEC1, 2
	0x54, 0x55, // ISRC2 INT1, 2
	0x56, 0x57, // ISRC2 DEC1, 2
	0x58, 0x59, // EQ1, 2
};

CS42L43_DECL_MUX(asptx1, CS42L43_ASPTX1_INPUT);
CS42L43_DECL_MUX(asptx2, CS42L43_ASPTX2_INPUT);
CS42L43_DECL_MUX(asptx3, CS42L43_ASPTX3_INPUT);
CS42L43_DECL_MUX(asptx4, CS42L43_ASPTX4_INPUT);
CS42L43_DECL_MUX(asptx5, CS42L43_ASPTX5_INPUT);
CS42L43_DECL_MUX(asptx6, CS42L43_ASPTX6_INPUT);

CS42L43_DECL_MUX(dp1tx1, CS42L43_SWIRE_DP1_CH1_INPUT);
CS42L43_DECL_MUX(dp1tx2, CS42L43_SWIRE_DP1_CH2_INPUT);
CS42L43_DECL_MUX(dp1tx3, CS42L43_SWIRE_DP1_CH3_INPUT);
CS42L43_DECL_MUX(dp1tx4, CS42L43_SWIRE_DP1_CH4_INPUT);
CS42L43_DECL_MUX(dp2tx1, CS42L43_SWIRE_DP2_CH1_INPUT);
CS42L43_DECL_MUX(dp2tx2, CS42L43_SWIRE_DP2_CH2_INPUT);
CS42L43_DECL_MUX(dp3tx1, CS42L43_SWIRE_DP3_CH1_INPUT);
CS42L43_DECL_MUX(dp3tx2, CS42L43_SWIRE_DP3_CH2_INPUT);
CS42L43_DECL_MUX(dp4tx1, CS42L43_SWIRE_DP4_CH1_INPUT);
CS42L43_DECL_MUX(dp4tx2, CS42L43_SWIRE_DP4_CH2_INPUT);

CS42L43_DECL_MUX(asrcint1, CS42L43_ASRC_INT1_INPUT1);
CS42L43_DECL_MUX(asrcint2, CS42L43_ASRC_INT2_INPUT1);
CS42L43_DECL_MUX(asrcint3, CS42L43_ASRC_INT3_INPUT1);
CS42L43_DECL_MUX(asrcint4, CS42L43_ASRC_INT4_INPUT1);
CS42L43_DECL_MUX(asrcdec1, CS42L43_ASRC_DEC1_INPUT1);
CS42L43_DECL_MUX(asrcdec2, CS42L43_ASRC_DEC2_INPUT1);
CS42L43_DECL_MUX(asrcdec3, CS42L43_ASRC_DEC3_INPUT1);
CS42L43_DECL_MUX(asrcdec4, CS42L43_ASRC_DEC4_INPUT1);

CS42L43_DECL_MUX(isrc1int1, CS42L43_ISRC1INT1_INPUT1);
CS42L43_DECL_MUX(isrc1int2, CS42L43_ISRC1INT2_INPUT1);
CS42L43_DECL_MUX(isrc1dec1, CS42L43_ISRC1DEC1_INPUT1);
CS42L43_DECL_MUX(isrc1dec2, CS42L43_ISRC1DEC2_INPUT1);
CS42L43_DECL_MUX(isrc2int1, CS42L43_ISRC2INT1_INPUT1);
CS42L43_DECL_MUX(isrc2int2, CS42L43_ISRC2INT2_INPUT1);
CS42L43_DECL_MUX(isrc2dec1, CS42L43_ISRC2DEC1_INPUT1);
CS42L43_DECL_MUX(isrc2dec2, CS42L43_ISRC2DEC2_INPUT1);

CS42L43_DECL_MUX(spdif1, CS42L43_SPDIF1_INPUT1);
CS42L43_DECL_MUX(spdif2, CS42L43_SPDIF2_INPUT1);

CS42L43_DECL_MIXER(eq1, CS42L43_EQ1MIX_INPUT1);
CS42L43_DECL_MIXER(eq2, CS42L43_EQ2MIX_INPUT1);

CS42L43_DECL_MIXER(amp1, CS42L43_AMP1MIX_INPUT1);
CS42L43_DECL_MIXER(amp2, CS42L43_AMP2MIX_INPUT1);

CS42L43_DECL_MIXER(amp3, CS42L43_AMP3MIX_INPUT1);
CS42L43_DECL_MIXER(amp4, CS42L43_AMP4MIX_INPUT1);

static int cs42l43_dapm_get_volsw(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);
	int ret;

	snd_soc_dapm_mutex_lock(dapm);
	ret = snd_soc_get_volsw(kcontrol, ucontrol);
	snd_soc_dapm_mutex_unlock(dapm);

	return ret;
}

static int cs42l43_dapm_put_volsw(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);
	int ret;

	snd_soc_dapm_mutex_lock(dapm);
	ret = snd_soc_put_volsw(kcontrol, ucontrol);
	snd_soc_dapm_mutex_unlock(dapm);

	return ret;
}

static int cs42l43_dapm_get_enum(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);
	int ret;

	snd_soc_dapm_mutex_lock(dapm);
	ret = snd_soc_get_enum_double(kcontrol, ucontrol);
	snd_soc_dapm_mutex_unlock(dapm);

	return ret;
}

static int cs42l43_dapm_put_enum(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);
	int ret;

	snd_soc_dapm_mutex_lock(dapm);
	ret = snd_soc_put_enum_double(kcontrol, ucontrol);
	snd_soc_dapm_mutex_unlock(dapm);

	return ret;
}

static int cs42l43_eq_get(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct cs42l43_codec *priv = snd_soc_component_get_drvdata(component);

	memcpy(ucontrol->value.integer.value, priv->eq_coeffs, sizeof(priv->eq_coeffs));

	return 0;
}

static int cs42l43_eq_put(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);
	struct cs42l43_codec *priv = snd_soc_component_get_drvdata(component);

	snd_soc_dapm_mutex_lock(dapm);

	memcpy(priv->eq_coeffs, ucontrol->value.integer.value, sizeof(priv->eq_coeffs));

	snd_soc_dapm_mutex_unlock(dapm);

	return 0;
}

static void cs42l43_spk_vu_sync(struct cs42l43_codec *priv)
{
	struct cs42l43 *cs42l43 = priv->core;

	mutex_lock(&priv->spk_vu_lock);

	regmap_update_bits(cs42l43->regmap, CS42L43_INTP_VOLUME_CTRL1,
			   CS42L43_AMP1_2_VU_MASK, CS42L43_AMP1_2_VU_MASK);
	regmap_update_bits(cs42l43->regmap, CS42L43_INTP_VOLUME_CTRL1,
			   CS42L43_AMP1_2_VU_MASK, 0);

	mutex_unlock(&priv->spk_vu_lock);
}

static int cs42l43_shutter_get(struct cs42l43_codec *priv, unsigned int shift)
{
	struct cs42l43 *cs42l43 = priv->core;
	unsigned int val;
	int ret;

	ret = pm_runtime_resume_and_get(priv->dev);
	if (ret) {
		dev_err(priv->dev, "Failed to resume for shutters: %d\n", ret);
		return ret;
	}

	/*
	 * SHUTTER_CONTROL is a mix of volatile and non-volatile bits, so must
	 * be cached for the non-volatiles, so drop it from the cache here so
	 * we force a read.
	 */
	ret = regcache_drop_region(cs42l43->regmap, CS42L43_SHUTTER_CONTROL,
				   CS42L43_SHUTTER_CONTROL);
	if (ret) {
		dev_err(priv->dev, "Failed to drop shutter from cache: %d\n", ret);
		goto error;
	}

	ret = regmap_read(cs42l43->regmap, CS42L43_SHUTTER_CONTROL, &val);
	if (ret) {
		dev_err(priv->dev, "Failed to check shutter status: %d\n", ret);
		goto error;
	}

	ret = !(val & BIT(shift));

	dev_dbg(priv->dev, "%s shutter is %s\n",
		BIT(shift) == CS42L43_STATUS_MIC_SHUTTER_MUTE_MASK ? "Mic" : "Speaker",
		ret ? "open" : "closed");

error:
	pm_runtime_mark_last_busy(priv->dev);
	pm_runtime_put_autosuspend(priv->dev);

	return ret;
}

static int cs42l43_decim_get(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct cs42l43_codec *priv = snd_soc_component_get_drvdata(component);
	int ret;

	ret = cs42l43_shutter_get(priv, CS42L43_STATUS_MIC_SHUTTER_MUTE_SHIFT);
	if (ret > 0)
		ret = cs42l43_dapm_get_volsw(kcontrol, ucontrol);
	else if (!ret)
		ucontrol->value.integer.value[0] = ret;

	return ret;
}

static int cs42l43_spk_get(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct cs42l43_codec *priv = snd_soc_component_get_drvdata(component);
	int ret;

	ret = cs42l43_shutter_get(priv, CS42L43_STATUS_SPK_SHUTTER_MUTE_SHIFT);
	if (ret > 0)
		ret = snd_soc_get_volsw(kcontrol, ucontrol);
	else if (!ret)
		ucontrol->value.integer.value[0] = ret;

	return ret;
}

static int cs42l43_spk_put(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct cs42l43_codec *priv = snd_soc_component_get_drvdata(component);
	int ret;

	ret = snd_soc_put_volsw(kcontrol, ucontrol);
	if (ret > 0)
		cs42l43_spk_vu_sync(priv);

	return ret;
}

static const struct snd_kcontrol_new cs42l43_controls[] = {
	SOC_ENUM_EXT("Jack Override", cs42l43_jack_enum,
		     cs42l43_jack_get, cs42l43_jack_put),

	SOC_DOUBLE_R_SX_TLV("ADC Volume", CS42L43_ADC_B_CTRL1, CS42L43_ADC_B_CTRL2,
			    CS42L43_ADC_PGA_GAIN_SHIFT,
			    0xF, 5, cs42l43_adc_tlv),

	SOC_DOUBLE("PDM1 Invert Switch", CS42L43_DMIC_PDM_CTRL,
		   CS42L43_PDM1L_INV_SHIFT, CS42L43_PDM1R_INV_SHIFT, 1, 0),
	SOC_DOUBLE("PDM2 Invert Switch", CS42L43_DMIC_PDM_CTRL,
		   CS42L43_PDM2L_INV_SHIFT, CS42L43_PDM2R_INV_SHIFT, 1, 0),
	SOC_ENUM("PDM1 Clock", cs42l43_pdm1_clk),
	SOC_ENUM("PDM2 Clock", cs42l43_pdm2_clk),

	SOC_SINGLE("Decimator 1 WNF Switch", CS42L43_DECIM_HPF_WNF_CTRL1,
		   CS42L43_DECIM_WNF_EN_SHIFT, 1, 0),
	SOC_SINGLE("Decimator 2 WNF Switch", CS42L43_DECIM_HPF_WNF_CTRL2,
		   CS42L43_DECIM_WNF_EN_SHIFT, 1, 0),
	SOC_SINGLE("Decimator 3 WNF Switch", CS42L43_DECIM_HPF_WNF_CTRL3,
		   CS42L43_DECIM_WNF_EN_SHIFT, 1, 0),
	SOC_SINGLE("Decimator 4 WNF Switch", CS42L43_DECIM_HPF_WNF_CTRL4,
		   CS42L43_DECIM_WNF_EN_SHIFT, 1, 0),

	SOC_ENUM("Decimator 1 WNF Corner Frequency", cs42l43_dec1_wnf_corner),
	SOC_ENUM("Decimator 2 WNF Corner Frequency", cs42l43_dec2_wnf_corner),
	SOC_ENUM("Decimator 3 WNF Corner Frequency", cs42l43_dec3_wnf_corner),
	SOC_ENUM("Decimator 4 WNF Corner Frequency", cs42l43_dec4_wnf_corner),

	SOC_SINGLE("Decimator 1 HPF Switch", CS42L43_DECIM_HPF_WNF_CTRL1,
		   CS42L43_DECIM_HPF_EN_SHIFT, 1, 0),
	SOC_SINGLE("Decimator 2 HPF Switch", CS42L43_DECIM_HPF_WNF_CTRL2,
		   CS42L43_DECIM_HPF_EN_SHIFT, 1, 0),
	SOC_SINGLE("Decimator 3 HPF Switch", CS42L43_DECIM_HPF_WNF_CTRL3,
		   CS42L43_DECIM_HPF_EN_SHIFT, 1, 0),
	SOC_SINGLE("Decimator 4 HPF Switch", CS42L43_DECIM_HPF_WNF_CTRL4,
		   CS42L43_DECIM_HPF_EN_SHIFT, 1, 0),

	SOC_ENUM("Decimator 1 HPF Corner Frequency", cs42l43_dec1_hpf_corner),
	SOC_ENUM("Decimator 2 HPF Corner Frequency", cs42l43_dec2_hpf_corner),
	SOC_ENUM("Decimator 3 HPF Corner Frequency", cs42l43_dec3_hpf_corner),
	SOC_ENUM("Decimator 4 HPF Corner Frequency", cs42l43_dec4_hpf_corner),

	SOC_SINGLE_TLV("Decimator 1 Volume", CS42L43_DECIM_VOL_CTRL_CH1_CH2,
		       CS42L43_DECIM1_VOL_SHIFT, 0xBF, 0, cs42l43_dec_tlv),
	SOC_SINGLE_EXT("Decimator 1 Switch", CS42L43_DECIM_VOL_CTRL_CH1_CH2,
		       CS42L43_DECIM1_MUTE_SHIFT, 1, 1,
		       cs42l43_decim_get, cs42l43_dapm_put_volsw),
	SOC_SINGLE_TLV("Decimator 2 Volume", CS42L43_DECIM_VOL_CTRL_CH1_CH2,
		       CS42L43_DECIM2_VOL_SHIFT, 0xBF, 0, cs42l43_dec_tlv),
	SOC_SINGLE_EXT("Decimator 2 Switch", CS42L43_DECIM_VOL_CTRL_CH1_CH2,
		       CS42L43_DECIM2_MUTE_SHIFT, 1, 1,
		       cs42l43_decim_get, cs42l43_dapm_put_volsw),
	SOC_SINGLE_TLV("Decimator 3 Volume", CS42L43_DECIM_VOL_CTRL_CH3_CH4,
		       CS42L43_DECIM3_VOL_SHIFT, 0xBF, 0, cs42l43_dec_tlv),
	SOC_SINGLE_EXT("Decimator 3 Switch", CS42L43_DECIM_VOL_CTRL_CH3_CH4,
		       CS42L43_DECIM3_MUTE_SHIFT, 1, 1,
		       cs42l43_decim_get, cs42l43_dapm_put_volsw),
	SOC_SINGLE_TLV("Decimator 4 Volume", CS42L43_DECIM_VOL_CTRL_CH3_CH4,
		       CS42L43_DECIM4_VOL_SHIFT, 0xBF, 0, cs42l43_dec_tlv),
	SOC_SINGLE_EXT("Decimator 4 Switch", CS42L43_DECIM_VOL_CTRL_CH3_CH4,
		       CS42L43_DECIM4_MUTE_SHIFT, 1, 1,
		       cs42l43_decim_get, cs42l43_dapm_put_volsw),

	SOC_ENUM_EXT("Decimator 1 Ramp Up", cs42l43_dec1_ramp_up,
		     cs42l43_dapm_get_enum, cs42l43_dapm_put_enum),
	SOC_ENUM_EXT("Decimator 1 Ramp Down", cs42l43_dec1_ramp_down,
		     cs42l43_dapm_get_enum, cs42l43_dapm_put_enum),
	SOC_ENUM_EXT("Decimator 2 Ramp Up", cs42l43_dec2_ramp_up,
		     cs42l43_dapm_get_enum, cs42l43_dapm_put_enum),
	SOC_ENUM_EXT("Decimator 2 Ramp Down", cs42l43_dec2_ramp_down,
		     cs42l43_dapm_get_enum, cs42l43_dapm_put_enum),
	SOC_ENUM_EXT("Decimator 3 Ramp Up", cs42l43_dec3_ramp_up,
		     cs42l43_dapm_get_enum, cs42l43_dapm_put_enum),
	SOC_ENUM_EXT("Decimator 3 Ramp Down", cs42l43_dec3_ramp_down,
		     cs42l43_dapm_get_enum, cs42l43_dapm_put_enum),
	SOC_ENUM_EXT("Decimator 4 Ramp Up", cs42l43_dec4_ramp_up,
		     cs42l43_dapm_get_enum, cs42l43_dapm_put_enum),
	SOC_ENUM_EXT("Decimator 4 Ramp Down", cs42l43_dec4_ramp_down,
		     cs42l43_dapm_get_enum, cs42l43_dapm_put_enum),

	SOC_DOUBLE_R_EXT("Speaker Digital Switch",
			 CS42L43_INTP_VOLUME_CTRL1, CS42L43_INTP_VOLUME_CTRL2,
			 CS42L43_AMP_MUTE_SHIFT, 1, 1,
			 cs42l43_spk_get, cs42l43_spk_put),

	SOC_DOUBLE_R_EXT_TLV("Speaker Digital Volume",
			     CS42L43_INTP_VOLUME_CTRL1, CS42L43_INTP_VOLUME_CTRL2,
			     CS42L43_AMP_VOL_SHIFT,
			     0xBF, 0, snd_soc_get_volsw, cs42l43_spk_put,
			     cs42l43_speaker_tlv),

	SOC_ENUM("Speaker Ramp Up", cs42l43_speaker_ramp_up),
	SOC_ENUM("Speaker Ramp Down", cs42l43_speaker_ramp_down),

	CS42L43_MIXER_VOLUMES("Speaker L", CS42L43_AMP1MIX_INPUT1),
	CS42L43_MIXER_VOLUMES("Speaker R", CS42L43_AMP2MIX_INPUT1),

	SOC_DOUBLE_SX_TLV("Headphone Digital Volume", CS42L43_HPPATHVOL,
			  CS42L43_AMP3_PATH_VOL_SHIFT, CS42L43_AMP4_PATH_VOL_SHIFT,
			  0x11B, 229, cs42l43_headphone_tlv),

	SOC_DOUBLE("Headphone Invert Switch", CS42L43_DACCNFG1,
		   CS42L43_AMP3_INV_SHIFT, CS42L43_AMP4_INV_SHIFT, 1, 0),

	SOC_SINGLE("Headphone Zero Cross Switch", CS42L43_PGAVOL,
		   CS42L43_HP_PATH_VOL_ZC_SHIFT, 1, 0),
	SOC_SINGLE("Headphone Ramp Switch", CS42L43_PGAVOL,
		   CS42L43_HP_PATH_VOL_SFT_SHIFT, 1, 0),
	SOC_ENUM("Headphone Ramp Rate", cs42l43_headphone_ramp),

	CS42L43_MIXER_VOLUMES("Headphone L", CS42L43_AMP3MIX_INPUT1),
	CS42L43_MIXER_VOLUMES("Headphone R", CS42L43_AMP4MIX_INPUT1),

	SOC_ENUM("Tone 1 Frequency", cs42l43_tone1_freq),
	SOC_ENUM("Tone 2 Frequency", cs42l43_tone2_freq),

	SOC_DOUBLE_EXT("EQ Switch",
		       CS42L43_MUTE_EQ_IN0, CS42L43_MUTE_EQ_CH1_SHIFT,
		       CS42L43_MUTE_EQ_CH2_SHIFT, 1, 1,
		       cs42l43_dapm_get_volsw, cs42l43_dapm_put_volsw),

	SND_SOC_BYTES_E("EQ Coefficients", 0, CS42L43_N_EQ_COEFFS,
			cs42l43_eq_get, cs42l43_eq_put),

	CS42L43_MIXER_VOLUMES("EQ1", CS42L43_EQ1MIX_INPUT1),
	CS42L43_MIXER_VOLUMES("EQ2", CS42L43_EQ2MIX_INPUT1),
};

static int cs42l43_eq_ev(struct snd_soc_dapm_widget *w,
			 struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct cs42l43_codec *priv = snd_soc_component_get_drvdata(component);
	struct cs42l43 *cs42l43 = priv->core;
	unsigned int val;
	int i, ret;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		regmap_update_bits(cs42l43->regmap, CS42L43_MUTE_EQ_IN0,
				   CS42L43_MUTE_EQ_CH1_MASK | CS42L43_MUTE_EQ_CH2_MASK,
				   CS42L43_MUTE_EQ_CH1_MASK | CS42L43_MUTE_EQ_CH2_MASK);

		regmap_update_bits(cs42l43->regmap, CS42L43_COEFF_RD_WR0,
				   CS42L43_WRITE_MODE_MASK, CS42L43_WRITE_MODE_MASK);

		for (i = 0; i < CS42L43_N_EQ_COEFFS; i++)
			regmap_write(cs42l43->regmap, CS42L43_COEFF_DATA_IN0,
				     priv->eq_coeffs[i]);

		regmap_update_bits(cs42l43->regmap, CS42L43_COEFF_RD_WR0,
				   CS42L43_WRITE_MODE_MASK, 0);

		return 0;
	case SND_SOC_DAPM_POST_PMU:
		ret = regmap_read_poll_timeout(cs42l43->regmap, CS42L43_INIT_DONE0,
					       val, (val & CS42L43_INITIALIZE_DONE_MASK),
					       2000, 10000);
		if (ret)
			dev_err(priv->dev, "Failed to start EQs: %d\n", ret);

		regmap_update_bits(cs42l43->regmap, CS42L43_MUTE_EQ_IN0,
				   CS42L43_MUTE_EQ_CH1_MASK | CS42L43_MUTE_EQ_CH2_MASK, 0);
		return ret;
	default:
		return 0;
	}
}

struct cs42l43_pll_config {
	unsigned int freq;

	unsigned int div;
	unsigned int mode;
	unsigned int cal;
};

static const struct cs42l43_pll_config cs42l43_pll_configs[] = {
	{ 2400000, 0x50000000, 0x1, 0xA4 },
	{ 3000000, 0x40000000, 0x1, 0x83 },
	{ 3072000, 0x40000000, 0x3, 0x80 },
};

static int cs42l43_set_pll(struct cs42l43_codec *priv, unsigned int src,
			   unsigned int freq)
{
	struct cs42l43 *cs42l43 = priv->core;

	lockdep_assert_held(&cs42l43->pll_lock);

	if (priv->refclk_src == src && priv->refclk_freq == freq)
		return 0;

	if (regmap_test_bits(cs42l43->regmap, CS42L43_CTRL_REG, CS42L43_PLL_EN_MASK)) {
		dev_err(priv->dev, "PLL active, can't change configuration\n");
		return -EBUSY;
	}

	switch (src) {
	case CS42L43_SYSCLK_MCLK:
	case CS42L43_SYSCLK_SDW:
		dev_dbg(priv->dev, "Source PLL from %s at %uHz\n",
			src ? "SoundWire" : "MCLK", freq);

		priv->refclk_src = src;
		priv->refclk_freq = freq;

		return 0;
	default:
		dev_err(priv->dev, "Invalid PLL source: 0x%x\n", src);
		return -EINVAL;
	}
}

static int cs42l43_enable_pll(struct cs42l43_codec *priv)
{
	static const struct reg_sequence enable_seq[] = {
		{ CS42L43_OSC_DIV_SEL, 0x0, },
		{ CS42L43_MCLK_SRC_SEL, CS42L43_OSC_PLL_MCLK_SEL_MASK, 5, },
	};
	struct cs42l43 *cs42l43 = priv->core;
	const struct cs42l43_pll_config *config = NULL;
	unsigned int div = 0;
	unsigned int freq = priv->refclk_freq;
	unsigned long time_left;

	lockdep_assert_held(&cs42l43->pll_lock);

	if (priv->refclk_src == CS42L43_SYSCLK_SDW) {
		if (!freq)
			freq = cs42l43->sdw_freq;
		else if (!cs42l43->sdw_freq)
			cs42l43->sdw_freq = freq;
	}

	dev_dbg(priv->dev, "Enabling PLL at %uHz\n", freq);

	div = fls(freq) -
	      fls(cs42l43_pll_configs[ARRAY_SIZE(cs42l43_pll_configs) - 1].freq);
	freq >>= div;

	if (div <= CS42L43_PLL_REFCLK_DIV_MASK) {
		int i;

		for (i = 0; i < ARRAY_SIZE(cs42l43_pll_configs); i++) {
			if (freq == cs42l43_pll_configs[i].freq) {
				config = &cs42l43_pll_configs[i];
				break;
			}
		}
	}

	if (!config) {
		dev_err(priv->dev, "No suitable PLL config: 0x%x, %uHz\n", div, freq);
		return -EINVAL;
	}

	regmap_update_bits(cs42l43->regmap, CS42L43_PLL_CONTROL,
			   CS42L43_PLL_REFCLK_DIV_MASK | CS42L43_PLL_REFCLK_SRC_MASK,
			   div << CS42L43_PLL_REFCLK_DIV_SHIFT |
			   priv->refclk_src << CS42L43_PLL_REFCLK_SRC_SHIFT);
	regmap_write(cs42l43->regmap, CS42L43_FDIV_FRAC, config->div);
	regmap_update_bits(cs42l43->regmap, CS42L43_CTRL_REG,
			   CS42L43_PLL_MODE_BYPASS_500_MASK |
			   CS42L43_PLL_MODE_BYPASS_1029_MASK,
			   config->mode << CS42L43_PLL_MODE_BYPASS_1029_SHIFT);
	regmap_update_bits(cs42l43->regmap, CS42L43_CAL_RATIO,
			   CS42L43_PLL_CAL_RATIO_MASK, config->cal);
	regmap_update_bits(cs42l43->regmap, CS42L43_PLL_CONTROL,
			   CS42L43_PLL_REFCLK_EN_MASK, CS42L43_PLL_REFCLK_EN_MASK);

	reinit_completion(&priv->pll_ready);

	regmap_update_bits(cs42l43->regmap, CS42L43_CTRL_REG,
			   CS42L43_PLL_EN_MASK, CS42L43_PLL_EN_MASK);

	time_left = wait_for_completion_timeout(&priv->pll_ready,
						msecs_to_jiffies(CS42L43_PLL_TIMEOUT_MS));
	if (!time_left) {
		regmap_update_bits(cs42l43->regmap, CS42L43_CTRL_REG,
				   CS42L43_PLL_EN_MASK, 0);
		regmap_update_bits(cs42l43->regmap, CS42L43_PLL_CONTROL,
				   CS42L43_PLL_REFCLK_EN_MASK, 0);

		dev_err(priv->dev, "Timeout out waiting for PLL\n");
		return -ETIMEDOUT;
	}

	if (priv->refclk_src == CS42L43_SYSCLK_SDW)
		cs42l43->sdw_pll_active = true;

	dev_dbg(priv->dev, "PLL locked in %ums\n", 200 - jiffies_to_msecs(time_left));

	/*
	 * Reads are not allowed over Soundwire without OSC_DIV2_EN or the PLL,
	 * but you can not change to PLL with OSC_DIV2_EN set. So ensure the whole
	 * change over happens under the regmap lock to prevent any reads.
	 */
	regmap_multi_reg_write(cs42l43->regmap, enable_seq, ARRAY_SIZE(enable_seq));

	return 0;
}

static int cs42l43_disable_pll(struct cs42l43_codec *priv)
{
	static const struct reg_sequence disable_seq[] = {
		{ CS42L43_MCLK_SRC_SEL, 0x0, 5, },
		{ CS42L43_OSC_DIV_SEL, CS42L43_OSC_DIV2_EN_MASK, },
	};
	struct cs42l43 *cs42l43 = priv->core;

	dev_dbg(priv->dev, "Disabling PLL\n");

	lockdep_assert_held(&cs42l43->pll_lock);

	regmap_multi_reg_write(cs42l43->regmap, disable_seq, ARRAY_SIZE(disable_seq));
	regmap_update_bits(cs42l43->regmap, CS42L43_CTRL_REG, CS42L43_PLL_EN_MASK, 0);
	regmap_update_bits(cs42l43->regmap, CS42L43_PLL_CONTROL,
			   CS42L43_PLL_REFCLK_EN_MASK, 0);

	cs42l43->sdw_pll_active = false;

	return 0;
}

static int cs42l43_pll_ev(struct snd_soc_dapm_widget *w,
			  struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct cs42l43_codec *priv = snd_soc_component_get_drvdata(component);
	struct cs42l43 *cs42l43 = priv->core;
	int ret;

	mutex_lock(&cs42l43->pll_lock);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		if (priv->refclk_src == CS42L43_SYSCLK_MCLK) {
			ret = clk_prepare_enable(priv->mclk);
			if (ret) {
				dev_err(priv->dev, "Failed to enable MCLK: %d\n", ret);
				break;
			}
		}

		ret = cs42l43_enable_pll(priv);
		break;
	case SND_SOC_DAPM_POST_PMD:
		ret = cs42l43_disable_pll(priv);

		if (priv->refclk_src == CS42L43_SYSCLK_MCLK)
			clk_disable_unprepare(priv->mclk);
		break;
	default:
		ret = 0;
		break;
	}

	mutex_unlock(&cs42l43->pll_lock);

	return ret;
}

static int cs42l43_dapm_wait_completion(struct completion *pmu, struct completion *pmd,
					int event, int timeout_ms)
{
	unsigned long time_left;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		reinit_completion(pmu);
		return 0;
	case SND_SOC_DAPM_PRE_PMD:
		reinit_completion(pmd);
		return 0;
	case SND_SOC_DAPM_POST_PMU:
		time_left = wait_for_completion_timeout(pmu, msecs_to_jiffies(timeout_ms));
		break;
	case SND_SOC_DAPM_POST_PMD:
		time_left = wait_for_completion_timeout(pmd, msecs_to_jiffies(timeout_ms));
		break;
	default:
		return 0;
	}

	if (!time_left)
		return -ETIMEDOUT;
	else
		return 0;
}

static int cs42l43_spkr_ev(struct snd_soc_dapm_widget *w,
			   struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct cs42l43_codec *priv = snd_soc_component_get_drvdata(component);

	return cs42l43_dapm_wait_completion(&priv->spkr_startup,
					    &priv->spkr_shutdown, event,
					    CS42L43_SPK_TIMEOUT_MS);
}

static int cs42l43_spkl_ev(struct snd_soc_dapm_widget *w,
			   struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct cs42l43_codec *priv = snd_soc_component_get_drvdata(component);

	return cs42l43_dapm_wait_completion(&priv->spkl_startup,
					    &priv->spkl_shutdown, event,
					    CS42L43_SPK_TIMEOUT_MS);
}

static int cs42l43_hp_ev(struct snd_soc_dapm_widget *w,
			 struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct cs42l43_codec *priv = snd_soc_component_get_drvdata(component);
	struct cs42l43 *cs42l43 = priv->core;
	unsigned int mask = 1 << w->shift;
	unsigned int val = 0;
	int ret;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		val = mask;
		fallthrough;
	case SND_SOC_DAPM_PRE_PMD:
		priv->hp_ena &= ~mask;
		priv->hp_ena |= val;

		ret = cs42l43_dapm_wait_completion(&priv->hp_startup,
						   &priv->hp_shutdown, event,
						   CS42L43_HP_TIMEOUT_MS);
		if (ret)
			return ret;

		if (!priv->load_detect_running && !priv->hp_ilimited)
			regmap_update_bits(cs42l43->regmap, CS42L43_BLOCK_EN8,
					   mask, val);
		break;
	case SND_SOC_DAPM_POST_PMU:
	case SND_SOC_DAPM_POST_PMD:
		if (priv->load_detect_running || priv->hp_ilimited)
			break;

		ret = cs42l43_dapm_wait_completion(&priv->hp_startup,
						   &priv->hp_shutdown, event,
						   CS42L43_HP_TIMEOUT_MS);
		if (ret)
			return ret;
		break;
	default:
		break;
	}

	return 0;
}

static int cs42l43_mic_ev(struct snd_soc_dapm_widget *w,
			  struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct cs42l43_codec *priv = snd_soc_component_get_drvdata(component);
	struct cs42l43 *cs42l43 = priv->core;
	unsigned int reg, ramp, mute;
	unsigned int *val;
	int ret;

	switch (w->shift) {
	case CS42L43_ADC1_EN_SHIFT:
	case CS42L43_PDM1_DIN_L_EN_SHIFT:
		reg = CS42L43_DECIM_VOL_CTRL_CH1_CH2;
		ramp = CS42L43_DECIM1_VD_RAMP_MASK;
		mute = CS42L43_DECIM1_MUTE_MASK;
		val = &priv->decim_cache[0];
		break;
	case CS42L43_ADC2_EN_SHIFT:
	case CS42L43_PDM1_DIN_R_EN_SHIFT:
		reg = CS42L43_DECIM_VOL_CTRL_CH1_CH2;
		ramp = CS42L43_DECIM2_VD_RAMP_MASK;
		mute = CS42L43_DECIM2_MUTE_MASK;
		val = &priv->decim_cache[1];
		break;
	case CS42L43_PDM2_DIN_L_EN_SHIFT:
		reg = CS42L43_DECIM_VOL_CTRL_CH3_CH4;
		ramp  = CS42L43_DECIM3_VD_RAMP_MASK;
		mute = CS42L43_DECIM3_MUTE_MASK;
		val = &priv->decim_cache[2];
		break;
	case CS42L43_PDM2_DIN_R_EN_SHIFT:
		reg = CS42L43_DECIM_VOL_CTRL_CH3_CH4;
		ramp = CS42L43_DECIM4_VD_RAMP_MASK;
		mute = CS42L43_DECIM4_MUTE_MASK;
		val = &priv->decim_cache[3];
		break;
	default:
		dev_err(priv->dev, "Invalid microphone shift: %d\n", w->shift);
		return -EINVAL;
	}

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		ret = regmap_read(cs42l43->regmap, reg, val);
		if (ret) {
			dev_err(priv->dev,
				"Failed to cache decimator settings: %d\n",
				ret);
			return ret;
		}

		regmap_update_bits(cs42l43->regmap, reg, mute | ramp, mute);
		break;
	case SND_SOC_DAPM_POST_PMU:
		regmap_update_bits(cs42l43->regmap, reg, mute | ramp, *val);
		break;
	default:
		break;
	}

	return 0;
}

static int cs42l43_adc_ev(struct snd_soc_dapm_widget *w,
			  struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct cs42l43_codec *priv = snd_soc_component_get_drvdata(component);
	struct cs42l43 *cs42l43 = priv->core;
	unsigned int mask = 1 << w->shift;
	unsigned int val = 0;
	int ret;

	ret = cs42l43_mic_ev(w, kcontrol, event);
	if (ret)
		return ret;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		val = mask;
		fallthrough;
	case SND_SOC_DAPM_PRE_PMD:
		priv->adc_ena &= ~mask;
		priv->adc_ena |= val;

		if (!priv->load_detect_running)
			regmap_update_bits(cs42l43->regmap, CS42L43_BLOCK_EN3,
					   mask, val);
		fallthrough;
	default:
		return 0;
	}
}

static const struct snd_soc_dapm_widget cs42l43_widgets[] = {
	SND_SOC_DAPM_SUPPLY("PLL", SND_SOC_NOPM, 0, 0, cs42l43_pll_ev,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_INPUT("ADC1_IN1_P"),
	SND_SOC_DAPM_INPUT("ADC1_IN1_N"),
	SND_SOC_DAPM_INPUT("ADC1_IN2_P"),
	SND_SOC_DAPM_INPUT("ADC1_IN2_N"),
	SND_SOC_DAPM_INPUT("ADC2_IN_P"),
	SND_SOC_DAPM_INPUT("ADC2_IN_N"),

	SND_SOC_DAPM_INPUT("PDM1_DIN"),
	SND_SOC_DAPM_INPUT("PDM2_DIN"),

	SND_SOC_DAPM_MUX("ADC1 Input", SND_SOC_NOPM, 0, 0, &cs42l43_adc1_input_ctl),

	SND_SOC_DAPM_PGA_E("ADC1", SND_SOC_NOPM, CS42L43_ADC1_EN_SHIFT, 0, NULL, 0,
			   cs42l43_adc_ev, SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_PGA_E("ADC2", SND_SOC_NOPM, CS42L43_ADC2_EN_SHIFT, 0, NULL, 0,
			   cs42l43_adc_ev, SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD),

	SND_SOC_DAPM_PGA_E("PDM1L", CS42L43_BLOCK_EN3, CS42L43_PDM1_DIN_L_EN_SHIFT,
			   0, NULL, 0, cs42l43_mic_ev,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_PGA_E("PDM1R", CS42L43_BLOCK_EN3, CS42L43_PDM1_DIN_R_EN_SHIFT,
			   0, NULL, 0, cs42l43_mic_ev,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_PGA_E("PDM2L", CS42L43_BLOCK_EN3, CS42L43_PDM2_DIN_L_EN_SHIFT,
			   0, NULL, 0, cs42l43_mic_ev,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_PGA_E("PDM2R", CS42L43_BLOCK_EN3, CS42L43_PDM2_DIN_R_EN_SHIFT,
			   0, NULL, 0, cs42l43_mic_ev,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU),

	SND_SOC_DAPM_MUX("Decimator 1 Mode", SND_SOC_NOPM, 0, 0,
			 &cs42l43_dec_mode_ctl[0]),
	SND_SOC_DAPM_MUX("Decimator 2 Mode", SND_SOC_NOPM, 0, 0,
			 &cs42l43_dec_mode_ctl[1]),

	SND_SOC_DAPM_PGA("Decimator 1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Decimator 2", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Decimator 3", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Decimator 4", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_SUPPLY_S("FSYNC", 0, CS42L43_ASP_CTRL, CS42L43_ASP_FSYNC_EN_SHIFT,
			      0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("BCLK", 1, CS42L43_ASP_CTRL, CS42L43_ASP_BCLK_EN_SHIFT,
			      0, NULL, 0),

	SND_SOC_DAPM_AIF_OUT("ASPTX1", NULL, 0,
			     CS42L43_ASP_TX_EN, CS42L43_ASP_TX_CH1_EN_SHIFT, 0),
	SND_SOC_DAPM_AIF_OUT("ASPTX2", NULL, 1,
			     CS42L43_ASP_TX_EN, CS42L43_ASP_TX_CH2_EN_SHIFT, 0),
	SND_SOC_DAPM_AIF_OUT("ASPTX3", NULL, 2,
			     CS42L43_ASP_TX_EN, CS42L43_ASP_TX_CH3_EN_SHIFT, 0),
	SND_SOC_DAPM_AIF_OUT("ASPTX4", NULL, 3,
			     CS42L43_ASP_TX_EN, CS42L43_ASP_TX_CH4_EN_SHIFT, 0),
	SND_SOC_DAPM_AIF_OUT("ASPTX5", NULL, 4,
			     CS42L43_ASP_TX_EN, CS42L43_ASP_TX_CH5_EN_SHIFT, 0),
	SND_SOC_DAPM_AIF_OUT("ASPTX6", NULL, 5,
			     CS42L43_ASP_TX_EN, CS42L43_ASP_TX_CH6_EN_SHIFT, 0),

	SND_SOC_DAPM_AIF_IN("ASPRX1", NULL, 0,
			    CS42L43_ASP_RX_EN, CS42L43_ASP_RX_CH1_EN_SHIFT, 0),
	SND_SOC_DAPM_AIF_IN("ASPRX2", NULL, 1,
			    CS42L43_ASP_RX_EN, CS42L43_ASP_RX_CH2_EN_SHIFT, 0),
	SND_SOC_DAPM_AIF_IN("ASPRX3", NULL, 2,
			    CS42L43_ASP_RX_EN, CS42L43_ASP_RX_CH3_EN_SHIFT, 0),
	SND_SOC_DAPM_AIF_IN("ASPRX4", NULL, 3,
			    CS42L43_ASP_RX_EN, CS42L43_ASP_RX_CH4_EN_SHIFT, 0),
	SND_SOC_DAPM_AIF_IN("ASPRX5", NULL, 4,
			    CS42L43_ASP_RX_EN, CS42L43_ASP_RX_CH5_EN_SHIFT, 0),
	SND_SOC_DAPM_AIF_IN("ASPRX6", NULL, 5,
			    CS42L43_ASP_RX_EN, CS42L43_ASP_RX_CH6_EN_SHIFT, 0),

	SND_SOC_DAPM_AIF_OUT("DP1TX1", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("DP1TX2", NULL, 1, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("DP1TX3", NULL, 2, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("DP1TX4", NULL, 3, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_AIF_OUT("DP2TX1", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("DP2TX2", NULL, 1, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_AIF_OUT("DP3TX1", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("DP3TX2", NULL, 1, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_AIF_OUT("DP4TX1", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("DP4TX2", NULL, 1, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_AIF_IN("DP5RX1", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("DP5RX2", NULL, 1, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_AIF_IN("DP6RX1", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("DP6RX2", NULL, 1, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_AIF_IN("DP7RX1", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("DP7RX2", NULL, 1, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_REGULATOR_SUPPLY("vdd-amp", 0, 0),

	SND_SOC_DAPM_PGA_E("AMP1", CS42L43_BLOCK_EN10, CS42L43_AMP1_EN_SHIFT, 0, NULL, 0,
			   cs42l43_spkl_ev, SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_PGA_E("AMP2", CS42L43_BLOCK_EN10, CS42L43_AMP2_EN_SHIFT, 0, NULL, 0,
			   cs42l43_spkr_ev, SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_OUTPUT("AMP1_OUT_P"),
	SND_SOC_DAPM_OUTPUT("AMP1_OUT_N"),
	SND_SOC_DAPM_OUTPUT("AMP2_OUT_P"),
	SND_SOC_DAPM_OUTPUT("AMP2_OUT_N"),

	SND_SOC_DAPM_PGA("SPDIF", CS42L43_BLOCK_EN11, CS42L43_SPDIF_EN_SHIFT,
			 0, NULL, 0),
	SND_SOC_DAPM_OUTPUT("SPDIF_TX"),

	SND_SOC_DAPM_PGA_E("HP", SND_SOC_NOPM, CS42L43_HP_EN_SHIFT, 0, NULL, 0,
			   cs42l43_hp_ev, SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_OUTPUT("AMP3_OUT"),
	SND_SOC_DAPM_OUTPUT("AMP4_OUT"),

	SND_SOC_DAPM_SIGGEN("Tone"),
	SND_SOC_DAPM_SUPPLY("Tone Generator", CS42L43_BLOCK_EN9, CS42L43_TONE_EN_SHIFT,
			    0, NULL, 0),
	SND_SOC_DAPM_REG(snd_soc_dapm_pga, "Tone 1", CS42L43_TONE_CH1_CTRL,
			 CS42L43_TONE_SEL_SHIFT, CS42L43_TONE_SEL_MASK, 0xA, 0),
	SND_SOC_DAPM_REG(snd_soc_dapm_pga, "Tone 2", CS42L43_TONE_CH2_CTRL,
			 CS42L43_TONE_SEL_SHIFT, CS42L43_TONE_SEL_MASK, 0xA, 0),

	SND_SOC_DAPM_SUPPLY("ISRC1", CS42L43_BLOCK_EN5, CS42L43_ISRC1_BANK_EN_SHIFT,
			    0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ISRC2", CS42L43_BLOCK_EN5, CS42L43_ISRC2_BANK_EN_SHIFT,
			    0, NULL, 0),

	SND_SOC_DAPM_PGA("ISRC1INT2", CS42L43_ISRC1_CTRL,
			 CS42L43_ISRC_INT2_EN_SHIFT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("ISRC1INT1", CS42L43_ISRC1_CTRL,
			 CS42L43_ISRC_INT1_EN_SHIFT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("ISRC1DEC2", CS42L43_ISRC1_CTRL,
			 CS42L43_ISRC_DEC2_EN_SHIFT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("ISRC1DEC1", CS42L43_ISRC1_CTRL,
			 CS42L43_ISRC_DEC1_EN_SHIFT, 0, NULL, 0),

	SND_SOC_DAPM_PGA("ISRC2INT2", CS42L43_ISRC2_CTRL,
			 CS42L43_ISRC_INT2_EN_SHIFT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("ISRC2INT1", CS42L43_ISRC2_CTRL,
			 CS42L43_ISRC_INT1_EN_SHIFT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("ISRC2DEC2", CS42L43_ISRC2_CTRL,
			 CS42L43_ISRC_DEC2_EN_SHIFT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("ISRC2DEC1", CS42L43_ISRC2_CTRL,
			 CS42L43_ISRC_DEC1_EN_SHIFT, 0, NULL, 0),

	SND_SOC_DAPM_SUPPLY("ASRC_INT", CS42L43_BLOCK_EN4,
			    CS42L43_ASRC_INT_BANK_EN_SHIFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ASRC_DEC", CS42L43_BLOCK_EN4,
			    CS42L43_ASRC_DEC_BANK_EN_SHIFT, 0, NULL, 0),

	SND_SOC_DAPM_PGA("ASRC_INT1", CS42L43_ASRC_INT_ENABLES,
			 CS42L43_ASRC_INT1_EN_SHIFT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("ASRC_INT2", CS42L43_ASRC_INT_ENABLES,
			 CS42L43_ASRC_INT2_EN_SHIFT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("ASRC_INT3", CS42L43_ASRC_INT_ENABLES,
			 CS42L43_ASRC_INT3_EN_SHIFT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("ASRC_INT4", CS42L43_ASRC_INT_ENABLES,
			 CS42L43_ASRC_INT4_EN_SHIFT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("ASRC_DEC1", CS42L43_ASRC_DEC_ENABLES,
			 CS42L43_ASRC_DEC1_EN_SHIFT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("ASRC_DEC2", CS42L43_ASRC_DEC_ENABLES,
			 CS42L43_ASRC_DEC2_EN_SHIFT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("ASRC_DEC3", CS42L43_ASRC_DEC_ENABLES,
			 CS42L43_ASRC_DEC3_EN_SHIFT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("ASRC_DEC4", CS42L43_ASRC_DEC_ENABLES,
			 CS42L43_ASRC_DEC4_EN_SHIFT, 0, NULL, 0),

	SND_SOC_DAPM_SUPPLY("EQ Clock", CS42L43_BLOCK_EN7, CS42L43_EQ_EN_SHIFT,
			    0, NULL, 0),
	SND_SOC_DAPM_PGA_E("EQ", CS42L43_START_EQZ0, CS42L43_START_FILTER_SHIFT,
			   0, NULL, 0, cs42l43_eq_ev,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU),

	SND_SOC_DAPM_SUPPLY("Mixer Core", CS42L43_BLOCK_EN6, CS42L43_MIXER_EN_SHIFT,
			    0, NULL, 0),
	CS42L43_DAPM_MUX("ASPTX1", asptx1),
	CS42L43_DAPM_MUX("ASPTX2", asptx2),
	CS42L43_DAPM_MUX("ASPTX3", asptx3),
	CS42L43_DAPM_MUX("ASPTX4", asptx4),
	CS42L43_DAPM_MUX("ASPTX5", asptx5),
	CS42L43_DAPM_MUX("ASPTX6", asptx6),

	CS42L43_DAPM_MUX("DP1TX1", dp1tx1),
	CS42L43_DAPM_MUX("DP1TX2", dp1tx2),
	CS42L43_DAPM_MUX("DP1TX3", dp1tx3),
	CS42L43_DAPM_MUX("DP1TX4", dp1tx4),
	CS42L43_DAPM_MUX("DP2TX1", dp2tx1),
	CS42L43_DAPM_MUX("DP2TX2", dp2tx2),
	CS42L43_DAPM_MUX("DP3TX1", dp3tx1),
	CS42L43_DAPM_MUX("DP3TX2", dp3tx2),
	CS42L43_DAPM_MUX("DP4TX1", dp4tx1),
	CS42L43_DAPM_MUX("DP4TX2", dp4tx2),

	CS42L43_DAPM_MUX("ASRC INT1", asrcint1),
	CS42L43_DAPM_MUX("ASRC INT2", asrcint2),
	CS42L43_DAPM_MUX("ASRC INT3", asrcint3),
	CS42L43_DAPM_MUX("ASRC INT4", asrcint4),
	CS42L43_DAPM_MUX("ASRC DEC1", asrcdec1),
	CS42L43_DAPM_MUX("ASRC DEC2", asrcdec2),
	CS42L43_DAPM_MUX("ASRC DEC3", asrcdec3),
	CS42L43_DAPM_MUX("ASRC DEC4", asrcdec4),

	CS42L43_DAPM_MUX("ISRC1INT1", isrc1int1),
	CS42L43_DAPM_MUX("ISRC1INT2", isrc1int2),
	CS42L43_DAPM_MUX("ISRC1DEC1", isrc1dec1),
	CS42L43_DAPM_MUX("ISRC1DEC2", isrc1dec2),
	CS42L43_DAPM_MUX("ISRC2INT1", isrc2int1),
	CS42L43_DAPM_MUX("ISRC2INT2", isrc2int2),
	CS42L43_DAPM_MUX("ISRC2DEC1", isrc2dec1),
	CS42L43_DAPM_MUX("ISRC2DEC2", isrc2dec2),

	CS42L43_DAPM_MUX("SPDIF1", spdif1),
	CS42L43_DAPM_MUX("SPDIF2", spdif2),

	CS42L43_DAPM_MIXER("EQ1", eq1),
	CS42L43_DAPM_MIXER("EQ2", eq2),

	CS42L43_DAPM_MIXER("Speaker L", amp1),
	CS42L43_DAPM_MIXER("Speaker R", amp2),

	CS42L43_DAPM_MIXER("Headphone L", amp3),
	CS42L43_DAPM_MIXER("Headphone R", amp4),
};

static const struct snd_soc_dapm_route cs42l43_routes[] = {
	{ "ADC1_IN1_P",		NULL,	"PLL" },
	{ "ADC1_IN1_N",		NULL,	"PLL" },
	{ "ADC1_IN2_P",		NULL,	"PLL" },
	{ "ADC1_IN2_N",		NULL,	"PLL" },
	{ "ADC2_IN_P",		NULL,	"PLL" },
	{ "ADC2_IN_N",		NULL,	"PLL" },
	{ "PDM1_DIN",		NULL,	"PLL" },
	{ "PDM2_DIN",		NULL,	"PLL" },
	{ "AMP1_OUT_P",		NULL,	"PLL" },
	{ "AMP1_OUT_N",		NULL,	"PLL" },
	{ "AMP2_OUT_P",		NULL,	"PLL" },
	{ "AMP2_OUT_N",		NULL,	"PLL" },
	{ "SPDIF_TX",		NULL,	"PLL" },
	{ "HP",			NULL,	"PLL" },
	{ "AMP3_OUT",		NULL,	"PLL" },
	{ "AMP4_OUT",		NULL,	"PLL" },
	{ "Tone 1",		NULL,	"PLL" },
	{ "Tone 2",		NULL,	"PLL" },
	{ "ASP Playback",	NULL,	"PLL" },
	{ "ASP Capture",	NULL,	"PLL" },
	{ "DP1 Capture",	NULL,	"PLL" },
	{ "DP2 Capture",	NULL,	"PLL" },
	{ "DP3 Capture",	NULL,	"PLL" },
	{ "DP4 Capture",	NULL,	"PLL" },
	{ "DP5 Playback",	NULL,	"PLL" },
	{ "DP6 Playback",	NULL,	"PLL" },
	{ "DP7 Playback",	NULL,	"PLL" },

	{ "ADC1 Input",		"IN1",	"ADC1_IN1_P" },
	{ "ADC1 Input",		"IN1",	"ADC1_IN1_N" },
	{ "ADC1 Input",		"IN2",	"ADC1_IN2_P" },
	{ "ADC1 Input",		"IN2",	"ADC1_IN2_N" },

	{ "ADC1",		NULL,	"ADC1 Input" },
	{ "ADC2",		NULL,	"ADC2_IN_P" },
	{ "ADC2",		NULL,	"ADC2_IN_N" },

	{ "PDM1L",		NULL,	"PDM1_DIN" },
	{ "PDM1R",		NULL,	"PDM1_DIN" },
	{ "PDM2L",		NULL,	"PDM2_DIN" },
	{ "PDM2R",		NULL,	"PDM2_DIN" },

	{ "Decimator 1 Mode",	"PDM",	"PDM1L" },
	{ "Decimator 1 Mode",	"ADC",	"ADC1" },
	{ "Decimator 2 Mode",	"PDM",	"PDM1R" },
	{ "Decimator 2 Mode",	"ADC",	"ADC2" },

	{ "Decimator 1",	NULL,	"Decimator 1 Mode" },
	{ "Decimator 2",	NULL,	"Decimator 2 Mode" },
	{ "Decimator 3",	NULL,	"PDM2L" },
	{ "Decimator 4",	NULL,	"PDM2R" },

	{ "ASP Capture",	NULL,	"ASPTX1" },
	{ "ASP Capture",	NULL,	"ASPTX2" },
	{ "ASP Capture",	NULL,	"ASPTX3" },
	{ "ASP Capture",	NULL,	"ASPTX4" },
	{ "ASP Capture",	NULL,	"ASPTX5" },
	{ "ASP Capture",	NULL,	"ASPTX6" },
	{ "ASPTX1",		NULL,	"BCLK" },
	{ "ASPTX2",		NULL,	"BCLK" },
	{ "ASPTX3",		NULL,	"BCLK" },
	{ "ASPTX4",		NULL,	"BCLK" },
	{ "ASPTX5",		NULL,	"BCLK" },
	{ "ASPTX6",		NULL,	"BCLK" },

	{ "ASPRX1",		NULL,	"ASP Playback" },
	{ "ASPRX2",		NULL,	"ASP Playback" },
	{ "ASPRX3",		NULL,	"ASP Playback" },
	{ "ASPRX4",		NULL,	"ASP Playback" },
	{ "ASPRX5",		NULL,	"ASP Playback" },
	{ "ASPRX6",		NULL,	"ASP Playback" },
	{ "ASPRX1",		NULL,	"BCLK" },
	{ "ASPRX2",		NULL,	"BCLK" },
	{ "ASPRX3",		NULL,	"BCLK" },
	{ "ASPRX4",		NULL,	"BCLK" },
	{ "ASPRX5",		NULL,	"BCLK" },
	{ "ASPRX6",		NULL,	"BCLK" },

	{ "DP1 Capture",	NULL, "DP1TX1" },
	{ "DP1 Capture",	NULL, "DP1TX2" },
	{ "DP1 Capture",	NULL, "DP1TX3" },
	{ "DP1 Capture",	NULL, "DP1TX4" },

	{ "DP2 Capture",	NULL, "DP2TX1" },
	{ "DP2 Capture",	NULL, "DP2TX2" },

	{ "DP3 Capture",	NULL, "DP3TX1" },
	{ "DP3 Capture",	NULL, "DP3TX2" },

	{ "DP4 Capture",	NULL, "DP4TX1" },
	{ "DP4 Capture",	NULL, "DP4TX2" },

	{ "DP5RX1",		NULL, "DP5 Playback" },
	{ "DP5RX2",		NULL, "DP5 Playback" },

	{ "DP6RX1",		NULL, "DP6 Playback" },
	{ "DP6RX2",		NULL, "DP6 Playback" },

	{ "DP7RX1",		NULL, "DP7 Playback" },
	{ "DP7RX2",		NULL, "DP7 Playback" },

	{ "AMP1",		NULL,	"vdd-amp" },
	{ "AMP2",		NULL,	"vdd-amp" },

	{ "AMP1_OUT_P",		NULL,	"AMP1" },
	{ "AMP1_OUT_N",		NULL,	"AMP1" },
	{ "AMP2_OUT_P",		NULL,	"AMP2" },
	{ "AMP2_OUT_N",		NULL,	"AMP2" },

	{ "SPDIF_TX",		NULL,	"SPDIF" },

	{ "AMP3_OUT",		NULL,	"HP" },
	{ "AMP4_OUT",		NULL,	"HP" },

	{ "Tone 1",		NULL,	"Tone" },
	{ "Tone 1",		NULL,	"Tone Generator" },
	{ "Tone 2",		NULL,	"Tone" },
	{ "Tone 2",		NULL,	"Tone Generator" },

	{ "ISRC1INT2",		NULL,	"ISRC1" },
	{ "ISRC1INT1",		NULL,	"ISRC1" },
	{ "ISRC1DEC2",		NULL,	"ISRC1" },
	{ "ISRC1DEC1",		NULL,	"ISRC1" },

	{ "ISRC2INT2",		NULL,	"ISRC2" },
	{ "ISRC2INT1",		NULL,	"ISRC2" },
	{ "ISRC2DEC2",		NULL,	"ISRC2" },
	{ "ISRC2DEC1",		NULL,	"ISRC2" },

	{ "ASRC_INT1",		NULL,	"ASRC_INT" },
	{ "ASRC_INT2",		NULL,	"ASRC_INT" },
	{ "ASRC_INT3",		NULL,	"ASRC_INT" },
	{ "ASRC_INT4",		NULL,	"ASRC_INT" },
	{ "ASRC_DEC1",		NULL,	"ASRC_DEC" },
	{ "ASRC_DEC2",		NULL,	"ASRC_DEC" },
	{ "ASRC_DEC3",		NULL,	"ASRC_DEC" },
	{ "ASRC_DEC4",		NULL,	"ASRC_DEC" },

	{ "EQ",			NULL,	"EQ Clock" },

	CS42L43_MUX_ROUTES("ASPTX1", "ASPTX1"),
	CS42L43_MUX_ROUTES("ASPTX2", "ASPTX2"),
	CS42L43_MUX_ROUTES("ASPTX3", "ASPTX3"),
	CS42L43_MUX_ROUTES("ASPTX4", "ASPTX4"),
	CS42L43_MUX_ROUTES("ASPTX5", "ASPTX5"),
	CS42L43_MUX_ROUTES("ASPTX6", "ASPTX6"),

	CS42L43_MUX_ROUTES("DP1TX1", "DP1TX1"),
	CS42L43_MUX_ROUTES("DP1TX2", "DP1TX2"),
	CS42L43_MUX_ROUTES("DP1TX3", "DP1TX3"),
	CS42L43_MUX_ROUTES("DP1TX4", "DP1TX4"),
	CS42L43_MUX_ROUTES("DP2TX1", "DP2TX1"),
	CS42L43_MUX_ROUTES("DP2TX2", "DP2TX2"),
	CS42L43_MUX_ROUTES("DP3TX1", "DP3TX1"),
	CS42L43_MUX_ROUTES("DP3TX2", "DP3TX2"),
	CS42L43_MUX_ROUTES("DP4TX1", "DP4TX1"),
	CS42L43_MUX_ROUTES("DP4TX2", "DP4TX2"),

	CS42L43_MUX_ROUTES("ASRC INT1", "ASRC_INT1"),
	CS42L43_MUX_ROUTES("ASRC INT2", "ASRC_INT2"),
	CS42L43_MUX_ROUTES("ASRC INT3", "ASRC_INT3"),
	CS42L43_MUX_ROUTES("ASRC INT4", "ASRC_INT4"),
	CS42L43_MUX_ROUTES("ASRC DEC1", "ASRC_DEC1"),
	CS42L43_MUX_ROUTES("ASRC DEC2", "ASRC_DEC2"),
	CS42L43_MUX_ROUTES("ASRC DEC3", "ASRC_DEC3"),
	CS42L43_MUX_ROUTES("ASRC DEC4", "ASRC_DEC4"),

	CS42L43_MUX_ROUTES("ISRC1INT1", "ISRC1INT1"),
	CS42L43_MUX_ROUTES("ISRC1INT2", "ISRC1INT2"),
	CS42L43_MUX_ROUTES("ISRC1DEC1", "ISRC1DEC1"),
	CS42L43_MUX_ROUTES("ISRC1DEC2", "ISRC1DEC2"),
	CS42L43_MUX_ROUTES("ISRC2INT1", "ISRC2INT1"),
	CS42L43_MUX_ROUTES("ISRC2INT2", "ISRC2INT2"),
	CS42L43_MUX_ROUTES("ISRC2DEC1", "ISRC2DEC1"),
	CS42L43_MUX_ROUTES("ISRC2DEC2", "ISRC2DEC2"),

	CS42L43_MUX_ROUTES("SPDIF1", "SPDIF"),
	CS42L43_MUX_ROUTES("SPDIF2", "SPDIF"),

	CS42L43_MIXER_ROUTES("EQ1", "EQ"),
	CS42L43_MIXER_ROUTES("EQ2", "EQ"),

	CS42L43_MIXER_ROUTES("Speaker L", "AMP1"),
	CS42L43_MIXER_ROUTES("Speaker R", "AMP2"),

	CS42L43_MIXER_ROUTES("Headphone L", "HP"),
	CS42L43_MIXER_ROUTES("Headphone R", "HP"),
};

static int cs42l43_set_sysclk(struct snd_soc_component *component, int clk_id,
			      int src, unsigned int freq, int dir)
{
	struct cs42l43_codec *priv = snd_soc_component_get_drvdata(component);
	struct cs42l43 *cs42l43 = priv->core;
	int ret;

	mutex_lock(&cs42l43->pll_lock);
	ret = cs42l43_set_pll(priv, src, freq);
	mutex_unlock(&cs42l43->pll_lock);

	return ret;
}

static int cs42l43_component_probe(struct snd_soc_component *component)
{
	struct cs42l43_codec *priv = snd_soc_component_get_drvdata(component);
	struct cs42l43 *cs42l43 = priv->core;

	snd_soc_component_init_regmap(component, cs42l43->regmap);

	cs42l43_mask_to_slots(priv, CS42L43_DEFAULT_SLOTS, priv->tx_slots,
			      ARRAY_SIZE(priv->tx_slots));
	cs42l43_mask_to_slots(priv, CS42L43_DEFAULT_SLOTS, priv->rx_slots,
			      ARRAY_SIZE(priv->rx_slots));

	priv->component = component;
	priv->constraint = cs42l43_constraint;

	return 0;
}

static void cs42l43_component_remove(struct snd_soc_component *component)
{
	struct cs42l43_codec *priv = snd_soc_component_get_drvdata(component);

	cs42l43_set_jack(priv->component, NULL, NULL);

	cancel_delayed_work_sync(&priv->bias_sense_timeout);
	cancel_delayed_work_sync(&priv->tip_sense_work);
	cancel_delayed_work_sync(&priv->button_press_work);
	cancel_work_sync(&priv->button_release_work);

	cancel_work_sync(&priv->hp_ilimit_work);
	cancel_delayed_work_sync(&priv->hp_ilimit_clear_work);

	priv->component = NULL;
}

static const struct snd_soc_component_driver cs42l43_component_drv = {
	.name			= "cs42l43-codec",

	.probe			= cs42l43_component_probe,
	.remove			= cs42l43_component_remove,
	.set_sysclk		= cs42l43_set_sysclk,
	.set_jack		= cs42l43_set_jack,

	.endianness		= 1,

	.controls		= cs42l43_controls,
	.num_controls		= ARRAY_SIZE(cs42l43_controls),
	.dapm_widgets		= cs42l43_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(cs42l43_widgets),
	.dapm_routes		= cs42l43_routes,
	.num_dapm_routes	= ARRAY_SIZE(cs42l43_routes),
};

struct cs42l43_irq {
	unsigned int irq;
	const char *name;
	irq_handler_t handler;
};

static const struct cs42l43_irq cs42l43_irqs[] = {
	{ CS42L43_PLL_LOST_LOCK, "pll lost lock", cs42l43_pll_lost_lock },
	{ CS42L43_PLL_READY, "pll ready", cs42l43_pll_ready },
	{ CS42L43_HP_STARTUP_DONE, "hp startup", cs42l43_hp_startup },
	{ CS42L43_HP_SHUTDOWN_DONE, "hp shutdown", cs42l43_hp_shutdown },
	{ CS42L43_HSDET_DONE, "type detect", cs42l43_type_detect },
	{ CS42L43_TIPSENSE_UNPLUG_PDET, "tip sense unplug", cs42l43_tip_sense },
	{ CS42L43_TIPSENSE_PLUG_PDET, "tip sense plug", cs42l43_tip_sense },
	{ CS42L43_DC_DETECT1_TRUE, "button press", cs42l43_button_press },
	{ CS42L43_DC_DETECT1_FALSE, "button release", cs42l43_button_release },
	{ CS42L43_HSBIAS_CLAMPED, "hsbias detect clamp", cs42l43_bias_detect_clamp },
	{ CS42L43_AMP2_CLK_STOP_FAULT, "spkr clock stop", cs42l43_spkr_clock_stop },
	{ CS42L43_AMP1_CLK_STOP_FAULT, "spkl clock stop", cs42l43_spkl_clock_stop },
	{ CS42L43_AMP2_VDDSPK_FAULT, "spkr brown out", cs42l43_spkr_brown_out },
	{ CS42L43_AMP1_VDDSPK_FAULT, "spkl brown out", cs42l43_spkl_brown_out },
	{ CS42L43_AMP2_SHUTDOWN_DONE, "spkr shutdown", cs42l43_spkr_shutdown },
	{ CS42L43_AMP1_SHUTDOWN_DONE, "spkl shutdown", cs42l43_spkl_shutdown },
	{ CS42L43_AMP2_STARTUP_DONE, "spkr startup", cs42l43_spkr_startup },
	{ CS42L43_AMP1_STARTUP_DONE, "spkl startup", cs42l43_spkl_startup },
	{ CS42L43_AMP2_THERM_SHDN, "spkr thermal shutdown", cs42l43_spkr_therm_shutdown },
	{ CS42L43_AMP1_THERM_SHDN, "spkl thermal shutdown", cs42l43_spkl_therm_shutdown },
	{ CS42L43_AMP2_THERM_WARN, "spkr thermal warning", cs42l43_spkr_therm_warm },
	{ CS42L43_AMP1_THERM_WARN, "spkl thermal warning", cs42l43_spkl_therm_warm },
	{ CS42L43_AMP2_SCDET, "spkr short circuit", cs42l43_spkr_sc_detect },
	{ CS42L43_AMP1_SCDET, "spkl short circuit", cs42l43_spkl_sc_detect },
	{ CS42L43_HP_ILIMIT, "hp ilimit", cs42l43_hp_ilimit },
	{ CS42L43_HP_LOADDET_DONE, "load detect done", cs42l43_load_detect },
};

static int cs42l43_request_irq(struct cs42l43_codec *priv,
			       struct irq_domain *dom, const char * const name,
			       unsigned int irq, irq_handler_t handler,
			       unsigned long flags)
{
	int ret;

	ret = irq_create_mapping(dom, irq);
	if (ret < 0)
		return dev_err_probe(priv->dev, ret, "Failed to map IRQ %s\n", name);

	dev_dbg(priv->dev, "Request IRQ %d for %s\n", ret, name);

	ret = devm_request_threaded_irq(priv->dev, ret, NULL, handler,
					IRQF_ONESHOT | flags, name, priv);
	if (ret)
		return dev_err_probe(priv->dev, ret, "Failed to request IRQ %s\n", name);

	return 0;
}

static int cs42l43_shutter_irq(struct cs42l43_codec *priv,
			       struct irq_domain *dom, unsigned int shutter,
			       const char * const open_name,
			       const char * const close_name,
			       irq_handler_t handler)
{
	unsigned int open_irq, close_irq;
	int ret;

	switch (shutter) {
	case 0x1:
		dev_warn(priv->dev, "Manual shutters, notifications not available\n");
		return 0;
	case 0x2:
		open_irq = CS42L43_GPIO1_RISE;
		close_irq = CS42L43_GPIO1_FALL;
		break;
	case 0x4:
		open_irq = CS42L43_GPIO2_RISE;
		close_irq = CS42L43_GPIO2_FALL;
		break;
	case 0x8:
		open_irq = CS42L43_GPIO3_RISE;
		close_irq = CS42L43_GPIO3_FALL;
		break;
	default:
		return 0;
	}

	ret = cs42l43_request_irq(priv, dom, close_name, close_irq, handler, IRQF_SHARED);
	if (ret)
		return ret;

	return cs42l43_request_irq(priv, dom, open_name, open_irq, handler, IRQF_SHARED);
}

static int cs42l43_codec_probe(struct platform_device *pdev)
{
	struct cs42l43 *cs42l43 = dev_get_drvdata(pdev->dev.parent);
	struct cs42l43_codec *priv;
	struct irq_domain *dom;
	unsigned int val;
	int i, ret;

	dom = irq_find_matching_fwnode(dev_fwnode(cs42l43->dev), DOMAIN_BUS_ANY);
	if (!dom)
		return -EPROBE_DEFER;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = &pdev->dev;
	priv->core = cs42l43;

	platform_set_drvdata(pdev, priv);

	mutex_init(&priv->jack_lock);
	mutex_init(&priv->spk_vu_lock);

	init_completion(&priv->hp_startup);
	init_completion(&priv->hp_shutdown);
	init_completion(&priv->spkr_shutdown);
	init_completion(&priv->spkl_shutdown);
	init_completion(&priv->spkr_startup);
	init_completion(&priv->spkl_startup);
	init_completion(&priv->pll_ready);
	init_completion(&priv->type_detect);
	init_completion(&priv->load_detect);

	INIT_DELAYED_WORK(&priv->tip_sense_work, cs42l43_tip_sense_work);
	INIT_DELAYED_WORK(&priv->bias_sense_timeout, cs42l43_bias_sense_timeout);
	INIT_DELAYED_WORK(&priv->button_press_work, cs42l43_button_press_work);
	INIT_DELAYED_WORK(&priv->hp_ilimit_clear_work, cs42l43_hp_ilimit_clear_work);
	INIT_WORK(&priv->button_release_work, cs42l43_button_release_work);
	INIT_WORK(&priv->hp_ilimit_work, cs42l43_hp_ilimit_work);

	pm_runtime_set_autosuspend_delay(priv->dev, 100);
	pm_runtime_use_autosuspend(priv->dev);
	pm_runtime_set_active(priv->dev);
	pm_runtime_get_noresume(priv->dev);

	ret = devm_pm_runtime_enable(priv->dev);
	if (ret)
		goto err_pm;

	for (i = 0; i < ARRAY_SIZE(cs42l43_irqs); i++) {
		ret = cs42l43_request_irq(priv, dom, cs42l43_irqs[i].name,
					  cs42l43_irqs[i].irq,
					  cs42l43_irqs[i].handler, 0);
		if (ret)
			goto err_pm;
	}

	ret = regmap_read(cs42l43->regmap, CS42L43_SHUTTER_CONTROL, &val);
	if (ret) {
		dev_err(priv->dev, "Failed to check shutter source: %d\n", ret);
		goto err_pm;
	}

	ret = cs42l43_shutter_irq(priv, dom, val & CS42L43_MIC_SHUTTER_CFG_MASK,
				  "mic shutter open", "mic shutter close",
				  cs42l43_mic_shutter);
	if (ret)
		goto err_pm;

	ret = cs42l43_shutter_irq(priv, dom, (val & CS42L43_SPK_SHUTTER_CFG_MASK) >>
				  CS42L43_SPK_SHUTTER_CFG_SHIFT,
				  "spk shutter open", "spk shutter close",
				  cs42l43_spk_shutter);
	if (ret)
		goto err_pm;

	// Don't use devm as we need to get against the MFD device
	priv->mclk = clk_get_optional(cs42l43->dev, "mclk");
	if (IS_ERR(priv->mclk)) {
		ret = PTR_ERR(priv->mclk);
		dev_err_probe(priv->dev, ret, "Failed to get mclk\n");
		goto err_pm;
	}

	ret = devm_snd_soc_register_component(priv->dev, &cs42l43_component_drv,
					      cs42l43_dais, ARRAY_SIZE(cs42l43_dais));
	if (ret) {
		dev_err_probe(priv->dev, ret, "Failed to register component\n");
		goto err_clk;
	}

	pm_runtime_mark_last_busy(priv->dev);
	pm_runtime_put_autosuspend(priv->dev);

	return 0;

err_clk:
	clk_put(priv->mclk);
err_pm:
	pm_runtime_put_sync(priv->dev);

	return ret;
}

static void cs42l43_codec_remove(struct platform_device *pdev)
{
	struct cs42l43_codec *priv = platform_get_drvdata(pdev);

	clk_put(priv->mclk);
}

static int cs42l43_codec_runtime_resume(struct device *dev)
{
	struct cs42l43_codec *priv = dev_get_drvdata(dev);

	dev_dbg(priv->dev, "Runtime resume\n");

	// Toggle the speaker volume update incase the speaker volume was synced
	cs42l43_spk_vu_sync(priv);

	return 0;
}

static int cs42l43_codec_suspend(struct device *dev)
{
	struct cs42l43_codec *priv = dev_get_drvdata(dev);
	struct cs42l43 *cs42l43 = priv->core;

	disable_irq(cs42l43->irq);

	return 0;
}

static int cs42l43_codec_suspend_noirq(struct device *dev)
{
	struct cs42l43_codec *priv = dev_get_drvdata(dev);
	struct cs42l43 *cs42l43 = priv->core;

	enable_irq(cs42l43->irq);

	return 0;
}

static int cs42l43_codec_resume(struct device *dev)
{
	struct cs42l43_codec *priv = dev_get_drvdata(dev);
	struct cs42l43 *cs42l43 = priv->core;

	enable_irq(cs42l43->irq);

	return 0;
}

static int cs42l43_codec_resume_noirq(struct device *dev)
{
	struct cs42l43_codec *priv = dev_get_drvdata(dev);
	struct cs42l43 *cs42l43 = priv->core;

	disable_irq(cs42l43->irq);

	return 0;
}

static const struct dev_pm_ops cs42l43_codec_pm_ops = {
	SYSTEM_SLEEP_PM_OPS(cs42l43_codec_suspend, cs42l43_codec_resume)
	NOIRQ_SYSTEM_SLEEP_PM_OPS(cs42l43_codec_suspend_noirq, cs42l43_codec_resume_noirq)
	RUNTIME_PM_OPS(NULL, cs42l43_codec_runtime_resume, NULL)
};

static const struct platform_device_id cs42l43_codec_id_table[] = {
	{ "cs42l43-codec", },
	{}
};
MODULE_DEVICE_TABLE(platform, cs42l43_codec_id_table);

static struct platform_driver cs42l43_codec_driver = {
	.driver = {
		.name	= "cs42l43-codec",
		.pm	= pm_ptr(&cs42l43_codec_pm_ops),
	},

	.probe		= cs42l43_codec_probe,
	.remove_new	= cs42l43_codec_remove,
	.id_table	= cs42l43_codec_id_table,
};
module_platform_driver(cs42l43_codec_driver);

MODULE_IMPORT_NS(SND_SOC_CS42L43);

MODULE_DESCRIPTION("CS42L43 CODEC Driver");
MODULE_AUTHOR("Charles Keepax <ckeepax@opensource.cirrus.com>");
MODULE_LICENSE("GPL");
