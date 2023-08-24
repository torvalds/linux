// SPDX-License-Identifier: GPL-2.0-only
//
// Driver for Cirrus Logic CS35L56 smart amp
//
// Copyright (C) 2023 Cirrus Logic, Inc. and
//                    Cirrus Logic International Semiconductor Ltd.

#include <linux/completion.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/math.h>
#include <linux/module.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/soundwire/sdw.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/tlv.h>

#include "wm_adsp.h"
#include "cs35l56.h"

static int cs35l56_dsp_event(struct snd_soc_dapm_widget *w,
			     struct snd_kcontrol *kcontrol, int event);

static int cs35l56_mbox_send(struct cs35l56_private *cs35l56, unsigned int command)
{
	unsigned int val;
	int ret;

	regmap_write(cs35l56->regmap, CS35L56_DSP_VIRTUAL1_MBOX_1, command);
	ret = regmap_read_poll_timeout(cs35l56->regmap, CS35L56_DSP_VIRTUAL1_MBOX_1,
				       val, (val == 0),
				       CS35L56_MBOX_POLL_US, CS35L56_MBOX_TIMEOUT_US);
	if (ret) {
		dev_warn(cs35l56->dev, "MBOX command %#x failed: %d\n", command, ret);
		return ret;
	}

	return 0;
}

static void cs35l56_wait_dsp_ready(struct cs35l56_private *cs35l56)
{
	/* Wait for patching to complete */
	flush_work(&cs35l56->dsp_work);
}

static int cs35l56_dspwait_get_volsw(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct cs35l56_private *cs35l56 = snd_soc_component_get_drvdata(component);

	cs35l56_wait_dsp_ready(cs35l56);
	return snd_soc_get_volsw(kcontrol, ucontrol);
}

static int cs35l56_dspwait_put_volsw(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct cs35l56_private *cs35l56 = snd_soc_component_get_drvdata(component);

	cs35l56_wait_dsp_ready(cs35l56);
	return snd_soc_put_volsw(kcontrol, ucontrol);
}

static DECLARE_TLV_DB_SCALE(vol_tlv, -10000, 25, 0);

static const struct snd_kcontrol_new cs35l56_controls[] = {
	SOC_SINGLE_EXT("Speaker Switch",
		       CS35L56_MAIN_RENDER_USER_MUTE, 0, 1, 1,
		       cs35l56_dspwait_get_volsw, cs35l56_dspwait_put_volsw),
	SOC_SINGLE_S_EXT_TLV("Speaker Volume",
			     CS35L56_MAIN_RENDER_USER_VOLUME,
			     6, -400, 400, 9, 0,
			     cs35l56_dspwait_get_volsw,
			     cs35l56_dspwait_put_volsw,
			     vol_tlv),
	SOC_SINGLE_EXT("Posture Number", CS35L56_MAIN_POSTURE_NUMBER,
		       0, 255, 0,
		       cs35l56_dspwait_get_volsw, cs35l56_dspwait_put_volsw),
};

static SOC_VALUE_ENUM_SINGLE_DECL(cs35l56_asp1tx1_enum,
				  CS35L56_ASP1TX1_INPUT,
				  0, CS35L56_ASP_TXn_SRC_MASK,
				  cs35l56_tx_input_texts,
				  cs35l56_tx_input_values);

static const struct snd_kcontrol_new asp1_tx1_mux =
	SOC_DAPM_ENUM("ASP1TX1 SRC", cs35l56_asp1tx1_enum);

static SOC_VALUE_ENUM_SINGLE_DECL(cs35l56_asp1tx2_enum,
				  CS35L56_ASP1TX2_INPUT,
				  0, CS35L56_ASP_TXn_SRC_MASK,
				  cs35l56_tx_input_texts,
				  cs35l56_tx_input_values);

static const struct snd_kcontrol_new asp1_tx2_mux =
	SOC_DAPM_ENUM("ASP1TX2 SRC", cs35l56_asp1tx2_enum);

static SOC_VALUE_ENUM_SINGLE_DECL(cs35l56_asp1tx3_enum,
				  CS35L56_ASP1TX3_INPUT,
				  0, CS35L56_ASP_TXn_SRC_MASK,
				  cs35l56_tx_input_texts,
				  cs35l56_tx_input_values);

static const struct snd_kcontrol_new asp1_tx3_mux =
	SOC_DAPM_ENUM("ASP1TX3 SRC", cs35l56_asp1tx3_enum);

static SOC_VALUE_ENUM_SINGLE_DECL(cs35l56_asp1tx4_enum,
				  CS35L56_ASP1TX4_INPUT,
				  0, CS35L56_ASP_TXn_SRC_MASK,
				  cs35l56_tx_input_texts,
				  cs35l56_tx_input_values);

static const struct snd_kcontrol_new asp1_tx4_mux =
	SOC_DAPM_ENUM("ASP1TX4 SRC", cs35l56_asp1tx4_enum);

static SOC_VALUE_ENUM_SINGLE_DECL(cs35l56_sdw1tx1_enum,
				CS35L56_SWIRE_DP3_CH1_INPUT,
				0, CS35L56_SWIRETXn_SRC_MASK,
				cs35l56_tx_input_texts,
				cs35l56_tx_input_values);

static const struct snd_kcontrol_new sdw1_tx1_mux =
	SOC_DAPM_ENUM("SDW1TX1 SRC", cs35l56_sdw1tx1_enum);

static SOC_VALUE_ENUM_SINGLE_DECL(cs35l56_sdw1tx2_enum,
				CS35L56_SWIRE_DP3_CH2_INPUT,
				0, CS35L56_SWIRETXn_SRC_MASK,
				cs35l56_tx_input_texts,
				cs35l56_tx_input_values);

static const struct snd_kcontrol_new sdw1_tx2_mux =
	SOC_DAPM_ENUM("SDW1TX2 SRC", cs35l56_sdw1tx2_enum);

static SOC_VALUE_ENUM_SINGLE_DECL(cs35l56_sdw1tx3_enum,
				CS35L56_SWIRE_DP3_CH3_INPUT,
				0, CS35L56_SWIRETXn_SRC_MASK,
				cs35l56_tx_input_texts,
				cs35l56_tx_input_values);

static const struct snd_kcontrol_new sdw1_tx3_mux =
	SOC_DAPM_ENUM("SDW1TX3 SRC", cs35l56_sdw1tx3_enum);

static SOC_VALUE_ENUM_SINGLE_DECL(cs35l56_sdw1tx4_enum,
				CS35L56_SWIRE_DP3_CH4_INPUT,
				0, CS35L56_SWIRETXn_SRC_MASK,
				cs35l56_tx_input_texts,
				cs35l56_tx_input_values);

static const struct snd_kcontrol_new sdw1_tx4_mux =
	SOC_DAPM_ENUM("SDW1TX4 SRC", cs35l56_sdw1tx4_enum);

static int cs35l56_play_event(struct snd_soc_dapm_widget *w,
			      struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct cs35l56_private *cs35l56 = snd_soc_component_get_drvdata(component);
	unsigned int val;
	int ret;

	dev_dbg(cs35l56->dev, "play: %d\n", event);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* Don't wait for ACK, we check in POST_PMU that it completed */
		return regmap_write(cs35l56->regmap, CS35L56_DSP_VIRTUAL1_MBOX_1,
				    CS35L56_MBOX_CMD_AUDIO_PLAY);
	case SND_SOC_DAPM_POST_PMU:
		/* Wait for firmware to enter PS0 power state */
		ret = regmap_read_poll_timeout(cs35l56->regmap,
					       CS35L56_TRANSDUCER_ACTUAL_PS,
					       val, (val == CS35L56_PS0),
					       CS35L56_PS0_POLL_US,
					       CS35L56_PS0_TIMEOUT_US);
		if (ret)
			dev_err(cs35l56->dev, "PS0 wait failed: %d\n", ret);
		return ret;
	case SND_SOC_DAPM_POST_PMD:
		return cs35l56_mbox_send(cs35l56, CS35L56_MBOX_CMD_AUDIO_PAUSE);
	default:
		return 0;
	}
}

static const struct snd_soc_dapm_widget cs35l56_dapm_widgets[] = {
	SND_SOC_DAPM_REGULATOR_SUPPLY("VDD_B", 0, 0),
	SND_SOC_DAPM_REGULATOR_SUPPLY("VDD_AMP", 0, 0),

	SND_SOC_DAPM_SUPPLY("PLAY", SND_SOC_NOPM, 0, 0, cs35l56_play_event,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_OUT_DRV("AMP", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_OUTPUT("SPK"),

	SND_SOC_DAPM_PGA_E("DSP1", SND_SOC_NOPM, 0, 0, NULL, 0, cs35l56_dsp_event,
			   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),

	SND_SOC_DAPM_AIF_IN("ASP1RX1", NULL, 0, CS35L56_ASP1_ENABLES1,
			    CS35L56_ASP_RX1_EN_SHIFT, 0),
	SND_SOC_DAPM_AIF_IN("ASP1RX2", NULL, 1, CS35L56_ASP1_ENABLES1,
			    CS35L56_ASP_RX2_EN_SHIFT, 0),
	SND_SOC_DAPM_AIF_OUT("ASP1TX1", NULL, 0, CS35L56_ASP1_ENABLES1,
			     CS35L56_ASP_TX1_EN_SHIFT, 0),
	SND_SOC_DAPM_AIF_OUT("ASP1TX2", NULL, 1, CS35L56_ASP1_ENABLES1,
			     CS35L56_ASP_TX2_EN_SHIFT, 0),
	SND_SOC_DAPM_AIF_OUT("ASP1TX3", NULL, 2, CS35L56_ASP1_ENABLES1,
			     CS35L56_ASP_TX3_EN_SHIFT, 0),
	SND_SOC_DAPM_AIF_OUT("ASP1TX4", NULL, 3, CS35L56_ASP1_ENABLES1,
			     CS35L56_ASP_TX4_EN_SHIFT, 0),

	SND_SOC_DAPM_MUX("ASP1 TX1 Source", SND_SOC_NOPM, 0, 0, &asp1_tx1_mux),
	SND_SOC_DAPM_MUX("ASP1 TX2 Source", SND_SOC_NOPM, 0, 0, &asp1_tx2_mux),
	SND_SOC_DAPM_MUX("ASP1 TX3 Source", SND_SOC_NOPM, 0, 0, &asp1_tx3_mux),
	SND_SOC_DAPM_MUX("ASP1 TX4 Source", SND_SOC_NOPM, 0, 0, &asp1_tx4_mux),

	SND_SOC_DAPM_MUX("SDW1 TX1 Source", SND_SOC_NOPM, 0, 0, &sdw1_tx1_mux),
	SND_SOC_DAPM_MUX("SDW1 TX2 Source", SND_SOC_NOPM, 0, 0, &sdw1_tx2_mux),
	SND_SOC_DAPM_MUX("SDW1 TX3 Source", SND_SOC_NOPM, 0, 0, &sdw1_tx3_mux),
	SND_SOC_DAPM_MUX("SDW1 TX4 Source", SND_SOC_NOPM, 0, 0, &sdw1_tx4_mux),

	SND_SOC_DAPM_SIGGEN("VMON ADC"),
	SND_SOC_DAPM_SIGGEN("IMON ADC"),
	SND_SOC_DAPM_SIGGEN("ERRVOL ADC"),
	SND_SOC_DAPM_SIGGEN("CLASSH ADC"),
	SND_SOC_DAPM_SIGGEN("VDDBMON ADC"),
	SND_SOC_DAPM_SIGGEN("VBSTMON ADC"),
	SND_SOC_DAPM_SIGGEN("TEMPMON ADC"),
};

#define CS35L56_SRC_ROUTE(name) \
	{ name" Source", "ASP1RX1", "ASP1RX1" }, \
	{ name" Source", "ASP1RX2", "ASP1RX2" }, \
	{ name" Source", "VMON", "VMON ADC" }, \
	{ name" Source", "IMON", "IMON ADC" }, \
	{ name" Source", "ERRVOL", "ERRVOL ADC" },   \
	{ name" Source", "CLASSH", "CLASSH ADC" },   \
	{ name" Source", "VDDBMON", "VDDBMON ADC" }, \
	{ name" Source", "VBSTMON", "VBSTMON ADC" }, \
	{ name" Source", "DSP1TX1", "DSP1" }, \
	{ name" Source", "DSP1TX2", "DSP1" }, \
	{ name" Source", "DSP1TX3", "DSP1" }, \
	{ name" Source", "DSP1TX4", "DSP1" }, \
	{ name" Source", "DSP1TX5", "DSP1" }, \
	{ name" Source", "DSP1TX6", "DSP1" }, \
	{ name" Source", "DSP1TX7", "DSP1" }, \
	{ name" Source", "DSP1TX8", "DSP1" }, \
	{ name" Source", "TEMPMON", "TEMPMON ADC" }, \
	{ name" Source", "INTERPOLATOR", "AMP" }, \
	{ name" Source", "SDW1RX1", "SDW1 Playback" }, \
	{ name" Source", "SDW1RX2", "SDW1 Playback" },

static const struct snd_soc_dapm_route cs35l56_audio_map[] = {
	{ "AMP", NULL, "VDD_B" },
	{ "AMP", NULL, "VDD_AMP" },

	{ "ASP1 Playback", NULL, "PLAY" },
	{ "SDW1 Playback", NULL, "PLAY" },

	{ "ASP1RX1", NULL, "ASP1 Playback" },
	{ "ASP1RX2", NULL, "ASP1 Playback" },
	{ "DSP1", NULL, "ASP1RX1" },
	{ "DSP1", NULL, "ASP1RX2" },
	{ "DSP1", NULL, "SDW1 Playback" },
	{ "AMP", NULL, "DSP1" },
	{ "SPK", NULL, "AMP" },

	CS35L56_SRC_ROUTE("ASP1 TX1")
	CS35L56_SRC_ROUTE("ASP1 TX2")
	CS35L56_SRC_ROUTE("ASP1 TX3")
	CS35L56_SRC_ROUTE("ASP1 TX4")

	{ "ASP1TX1", NULL, "ASP1 TX1 Source" },
	{ "ASP1TX2", NULL, "ASP1 TX2 Source" },
	{ "ASP1TX3", NULL, "ASP1 TX3 Source" },
	{ "ASP1TX4", NULL, "ASP1 TX4 Source" },
	{ "ASP1 Capture", NULL, "ASP1TX1" },
	{ "ASP1 Capture", NULL, "ASP1TX2" },
	{ "ASP1 Capture", NULL, "ASP1TX3" },
	{ "ASP1 Capture", NULL, "ASP1TX4" },

	CS35L56_SRC_ROUTE("SDW1 TX1")
	CS35L56_SRC_ROUTE("SDW1 TX2")
	CS35L56_SRC_ROUTE("SDW1 TX3")
	CS35L56_SRC_ROUTE("SDW1 TX4")
	{ "SDW1 Capture", NULL, "SDW1 TX1 Source" },
	{ "SDW1 Capture", NULL, "SDW1 TX2 Source" },
	{ "SDW1 Capture", NULL, "SDW1 TX3 Source" },
	{ "SDW1 Capture", NULL, "SDW1 TX4 Source" },
};

static int cs35l56_dsp_event(struct snd_soc_dapm_widget *w,
			     struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct cs35l56_private *cs35l56 = snd_soc_component_get_drvdata(component);

	dev_dbg(cs35l56->dev, "%s: %d\n", __func__, event);

	return wm_adsp_event(w, kcontrol, event);
}

irqreturn_t cs35l56_irq(int irq, void *data)
{
	struct cs35l56_private *cs35l56 = data;
	unsigned int status1 = 0, status8 = 0, status20 = 0;
	unsigned int mask1, mask8, mask20;
	unsigned int val;
	int rv;

	irqreturn_t ret = IRQ_NONE;

	if (!cs35l56->init_done)
		return IRQ_NONE;

	mutex_lock(&cs35l56->irq_lock);

	rv = pm_runtime_resume_and_get(cs35l56->dev);
	if (rv < 0) {
		dev_err(cs35l56->dev, "irq: failed to get pm_runtime: %d\n", rv);
		goto err_unlock;
	}

	regmap_read(cs35l56->regmap, CS35L56_IRQ1_STATUS, &val);
	if ((val & CS35L56_IRQ1_STS_MASK) == 0) {
		dev_dbg(cs35l56->dev, "Spurious IRQ: no pending interrupt\n");
		goto err;
	}

	/* Ack interrupts */
	regmap_read(cs35l56->regmap, CS35L56_IRQ1_EINT_1, &status1);
	regmap_read(cs35l56->regmap, CS35L56_IRQ1_MASK_1, &mask1);
	status1 &= ~mask1;
	regmap_write(cs35l56->regmap, CS35L56_IRQ1_EINT_1, status1);

	regmap_read(cs35l56->regmap, CS35L56_IRQ1_EINT_8, &status8);
	regmap_read(cs35l56->regmap, CS35L56_IRQ1_MASK_8, &mask8);
	status8 &= ~mask8;
	regmap_write(cs35l56->regmap, CS35L56_IRQ1_EINT_8, status8);

	regmap_read(cs35l56->regmap, CS35L56_IRQ1_EINT_20, &status20);
	regmap_read(cs35l56->regmap, CS35L56_IRQ1_MASK_20, &mask20);
	status20 &= ~mask20;
	/* We don't want EINT20 but they default to unmasked: force mask */
	regmap_write(cs35l56->regmap, CS35L56_IRQ1_MASK_20, 0xffffffff);

	dev_dbg(cs35l56->dev, "%s: %#x %#x\n", __func__, status1, status8);

	/* Check to see if unmasked bits are active */
	if (!status1 && !status8 && !status20)
		goto err;

	if (status1 & CS35L56_AMP_SHORT_ERR_EINT1_MASK)
		dev_crit(cs35l56->dev, "Amp short error\n");

	if (status8 & CS35L56_TEMP_ERR_EINT1_MASK)
		dev_crit(cs35l56->dev, "Overtemp error\n");

	ret = IRQ_HANDLED;

err:
	pm_runtime_put(cs35l56->dev);
err_unlock:
	mutex_unlock(&cs35l56->irq_lock);

	return ret;
}
EXPORT_SYMBOL_NS_GPL(cs35l56_irq, SND_SOC_CS35L56_CORE);

int cs35l56_irq_request(struct cs35l56_private *cs35l56, int irq)
{
	int ret;

	if (!irq)
		return 0;

	ret = devm_request_threaded_irq(cs35l56->dev, irq, NULL, cs35l56_irq,
					IRQF_ONESHOT | IRQF_SHARED | IRQF_TRIGGER_LOW,
					"cs35l56", cs35l56);
	if (!ret)
		cs35l56->irq = irq;
	else
		dev_err(cs35l56->dev, "Failed to get IRQ: %d\n", ret);

	return ret;
}
EXPORT_SYMBOL_NS_GPL(cs35l56_irq_request, SND_SOC_CS35L56_CORE);

static int cs35l56_asp_dai_set_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt)
{
	struct cs35l56_private *cs35l56 = snd_soc_component_get_drvdata(codec_dai->component);
	unsigned int val;

	dev_dbg(cs35l56->dev, "%s: %#x\n", __func__, fmt);

	switch (fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_CBC_CFC:
		break;
	default:
		dev_err(cs35l56->dev, "Unsupported clock source mode\n");
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
		val = CS35L56_ASP_FMT_DSP_A << CS35L56_ASP_FMT_SHIFT;
		cs35l56->tdm_mode = true;
		break;
	case SND_SOC_DAIFMT_I2S:
		val = CS35L56_ASP_FMT_I2S << CS35L56_ASP_FMT_SHIFT;
		cs35l56->tdm_mode = false;
		break;
	default:
		dev_err(cs35l56->dev, "Unsupported DAI format\n");
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_IF:
		val |= CS35L56_ASP_FSYNC_INV_MASK;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		val |= CS35L56_ASP_BCLK_INV_MASK;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		val |= CS35L56_ASP_BCLK_INV_MASK | CS35L56_ASP_FSYNC_INV_MASK;
		break;
	case SND_SOC_DAIFMT_NB_NF:
		break;
	default:
		dev_err(cs35l56->dev, "Invalid clock invert\n");
		return -EINVAL;
	}

	regmap_update_bits(cs35l56->regmap,
			   CS35L56_ASP1_CONTROL2,
			   CS35L56_ASP_FMT_MASK |
			   CS35L56_ASP_BCLK_INV_MASK | CS35L56_ASP_FSYNC_INV_MASK,
			   val);

	/* Hi-Z DOUT in unused slots and when all TX are disabled */
	regmap_update_bits(cs35l56->regmap, CS35L56_ASP1_CONTROL3,
			   CS35L56_ASP1_DOUT_HIZ_CTRL_MASK,
			   CS35L56_ASP_UNUSED_HIZ_OFF_HIZ);

	return 0;
}

static void cs35l56_set_asp_slot_positions(struct cs35l56_private *cs35l56,
					   unsigned int reg, unsigned long mask)
{
	unsigned int reg_val, channel_shift;
	int bit_num;

	/* Init all slots to 63 */
	switch (reg) {
	case CS35L56_ASP1_FRAME_CONTROL1:
		reg_val = 0x3f3f3f3f;
		break;
	case CS35L56_ASP1_FRAME_CONTROL5:
		reg_val = 0x3f3f3f;
		break;
	}

	/* Enable consecutive TX1..TXn for each of the slots set in mask */
	channel_shift = 0;
	for_each_set_bit(bit_num, &mask, 32) {
		reg_val &= ~(0x3f << channel_shift);
		reg_val |= bit_num << channel_shift;
		channel_shift += 8;
	}

	regmap_write(cs35l56->regmap, reg, reg_val);
}

static int cs35l56_asp_dai_set_tdm_slot(struct snd_soc_dai *dai, unsigned int tx_mask,
					unsigned int rx_mask, int slots, int slot_width)
{
	struct cs35l56_private *cs35l56 = snd_soc_component_get_drvdata(dai->component);

	if ((slots == 0) || (slot_width == 0)) {
		dev_dbg(cs35l56->dev, "tdm config cleared\n");
		cs35l56->asp_slot_width = 0;
		cs35l56->asp_slot_count = 0;
		return 0;
	}

	if (slot_width > (CS35L56_ASP_RX_WIDTH_MASK >> CS35L56_ASP_RX_WIDTH_SHIFT)) {
		dev_err(cs35l56->dev, "tdm invalid slot width %d\n", slot_width);
		return -EINVAL;
	}

	/* More than 32 slots would give an unsupportable BCLK frequency */
	if (slots > 32) {
		dev_err(cs35l56->dev, "tdm invalid slot count %d\n", slots);
		return -EINVAL;
	}

	cs35l56->asp_slot_width = (u8)slot_width;
	cs35l56->asp_slot_count = (u8)slots;

	// Note: rx/tx is from point of view of the CPU end
	if (tx_mask == 0)
		tx_mask = 0x3;	// ASPRX1/RX2 in slots 0 and 1

	if (rx_mask == 0)
		rx_mask = 0xf;	// ASPTX1..TX4 in slots 0..3

	cs35l56_set_asp_slot_positions(cs35l56, CS35L56_ASP1_FRAME_CONTROL1, rx_mask);
	cs35l56_set_asp_slot_positions(cs35l56, CS35L56_ASP1_FRAME_CONTROL5, tx_mask);

	dev_dbg(cs35l56->dev, "tdm slot width: %u count: %u tx_mask: %#x rx_mask: %#x\n",
		cs35l56->asp_slot_width, cs35l56->asp_slot_count, tx_mask, rx_mask);

	return 0;
}

static int cs35l56_asp_dai_hw_params(struct snd_pcm_substream *substream,
				     struct snd_pcm_hw_params *params,
				     struct snd_soc_dai *dai)
{
	struct cs35l56_private *cs35l56 = snd_soc_component_get_drvdata(dai->component);
	unsigned int rate = params_rate(params);
	u8 asp_width, asp_wl;

	asp_wl = params_width(params);
	if (cs35l56->asp_slot_width)
		asp_width = cs35l56->asp_slot_width;
	else
		asp_width = asp_wl;

	dev_dbg(cs35l56->dev, "%s: wl=%d, width=%d, rate=%d", __func__, asp_wl, asp_width, rate);

	if (!cs35l56->sysclk_set) {
		unsigned int slots = cs35l56->asp_slot_count;
		unsigned int bclk_freq;
		int freq_id;

		if (slots == 0) {
			slots = params_channels(params);

			/* I2S always has an even number of slots */
			if (!cs35l56->tdm_mode)
				slots = round_up(slots, 2);
		}

		bclk_freq = asp_width * slots * rate;
		freq_id = cs35l56_get_bclk_freq_id(bclk_freq);
		if (freq_id < 0) {
			dev_err(cs35l56->dev, "%s: Invalid BCLK %u\n", __func__, bclk_freq);
			return -EINVAL;
		}

		regmap_update_bits(cs35l56->regmap, CS35L56_ASP1_CONTROL1,
				   CS35L56_ASP_BCLK_FREQ_MASK,
				   freq_id << CS35L56_ASP_BCLK_FREQ_SHIFT);
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		regmap_update_bits(cs35l56->regmap, CS35L56_ASP1_CONTROL2,
				   CS35L56_ASP_RX_WIDTH_MASK, asp_width <<
				   CS35L56_ASP_RX_WIDTH_SHIFT);
		regmap_update_bits(cs35l56->regmap, CS35L56_ASP1_DATA_CONTROL5,
				   CS35L56_ASP_RX_WL_MASK, asp_wl);
	} else {
		regmap_update_bits(cs35l56->regmap, CS35L56_ASP1_CONTROL2,
				   CS35L56_ASP_TX_WIDTH_MASK, asp_width <<
				   CS35L56_ASP_TX_WIDTH_SHIFT);
		regmap_update_bits(cs35l56->regmap, CS35L56_ASP1_DATA_CONTROL1,
				   CS35L56_ASP_TX_WL_MASK, asp_wl);
	}

	return 0;
}

static int cs35l56_asp_dai_set_sysclk(struct snd_soc_dai *dai,
				      int clk_id, unsigned int freq, int dir)
{
	struct cs35l56_private *cs35l56 = snd_soc_component_get_drvdata(dai->component);
	int freq_id;

	if (freq == 0) {
		cs35l56->sysclk_set = false;
		return 0;
	}

	freq_id = cs35l56_get_bclk_freq_id(freq);
	if (freq_id < 0)
		return freq_id;

	regmap_update_bits(cs35l56->regmap, CS35L56_ASP1_CONTROL1,
			   CS35L56_ASP_BCLK_FREQ_MASK,
			   freq_id << CS35L56_ASP_BCLK_FREQ_SHIFT);
	cs35l56->sysclk_set = true;

	return 0;
}

static const struct snd_soc_dai_ops cs35l56_ops = {
	.set_fmt = cs35l56_asp_dai_set_fmt,
	.set_tdm_slot = cs35l56_asp_dai_set_tdm_slot,
	.hw_params = cs35l56_asp_dai_hw_params,
	.set_sysclk = cs35l56_asp_dai_set_sysclk,
};

static void cs35l56_sdw_dai_shutdown(struct snd_pcm_substream *substream,
				     struct snd_soc_dai *dai)
{
	snd_soc_dai_set_dma_data(dai, substream, NULL);
}

static int cs35l56_sdw_dai_set_tdm_slot(struct snd_soc_dai *dai, unsigned int tx_mask,
					unsigned int rx_mask, int slots, int slot_width)
{
	struct cs35l56_private *cs35l56 = snd_soc_component_get_drvdata(dai->component);

	/* rx/tx are from point of view of the CPU end so opposite to our rx/tx */
	cs35l56->rx_mask = tx_mask;
	cs35l56->tx_mask = rx_mask;

	return 0;
}

static int cs35l56_sdw_dai_hw_params(struct snd_pcm_substream *substream,
				     struct snd_pcm_hw_params *params,
				     struct snd_soc_dai *dai)
{
	struct cs35l56_private *cs35l56 = snd_soc_component_get_drvdata(dai->component);
	struct sdw_stream_runtime *sdw_stream = snd_soc_dai_get_dma_data(dai, substream);
	struct sdw_stream_config sconfig;
	struct sdw_port_config pconfig;
	int ret;

	dev_dbg(cs35l56->dev, "%s: rate %d\n", __func__, params_rate(params));

	if (!cs35l56->init_done)
		return -ENODEV;

	if (!sdw_stream)
		return -EINVAL;

	memset(&sconfig, 0, sizeof(sconfig));
	memset(&pconfig, 0, sizeof(pconfig));

	sconfig.frame_rate = params_rate(params);
	sconfig.bps = snd_pcm_format_width(params_format(params));

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		sconfig.direction = SDW_DATA_DIR_RX;
		pconfig.num = CS35L56_SDW1_PLAYBACK_PORT;
		pconfig.ch_mask = cs35l56->rx_mask;
	} else {
		sconfig.direction = SDW_DATA_DIR_TX;
		pconfig.num = CS35L56_SDW1_CAPTURE_PORT;
		pconfig.ch_mask = cs35l56->tx_mask;
	}

	if (pconfig.ch_mask == 0) {
		sconfig.ch_count = params_channels(params);
		pconfig.ch_mask = GENMASK(sconfig.ch_count - 1, 0);
	} else {
		sconfig.ch_count = hweight32(pconfig.ch_mask);
	}

	ret = sdw_stream_add_slave(cs35l56->sdw_peripheral, &sconfig, &pconfig,
				   1, sdw_stream);
	if (ret) {
		dev_err(dai->dev, "Failed to add sdw stream: %d\n", ret);
		return ret;
	}

	return 0;
}

static int cs35l56_sdw_dai_hw_free(struct snd_pcm_substream *substream,
				   struct snd_soc_dai *dai)
{
	struct cs35l56_private *cs35l56 = snd_soc_component_get_drvdata(dai->component);
	struct sdw_stream_runtime *sdw_stream = snd_soc_dai_get_dma_data(dai, substream);

	if (!cs35l56->sdw_peripheral)
		return -EINVAL;

	sdw_stream_remove_slave(cs35l56->sdw_peripheral, sdw_stream);

	return 0;
}

static int cs35l56_sdw_dai_set_stream(struct snd_soc_dai *dai,
				      void *sdw_stream, int direction)
{
	snd_soc_dai_dma_data_set(dai, direction, sdw_stream);

	return 0;
}

static const struct snd_soc_dai_ops cs35l56_sdw_dai_ops = {
	.set_tdm_slot = cs35l56_sdw_dai_set_tdm_slot,
	.shutdown = cs35l56_sdw_dai_shutdown,
	.hw_params = cs35l56_sdw_dai_hw_params,
	.hw_free = cs35l56_sdw_dai_hw_free,
	.set_stream = cs35l56_sdw_dai_set_stream,
};

static struct snd_soc_dai_driver cs35l56_dai[] = {
	{
		.name = "cs35l56-asp1",
		.id = 0,
		.playback = {
			.stream_name = "ASP1 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = CS35L56_RATES,
			.formats = CS35L56_RX_FORMATS,
		},
		.capture = {
			.stream_name = "ASP1 Capture",
			.channels_min = 1,
			.channels_max = 4,
			.rates = CS35L56_RATES,
			.formats = CS35L56_TX_FORMATS,
		},
		.ops = &cs35l56_ops,
		.symmetric_rate = 1,
		.symmetric_sample_bits = 1,
	},
	{
		.name = "cs35l56-sdw1",
		.id = 1,
		.playback = {
			.stream_name = "SDW1 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = CS35L56_RATES,
			.formats = CS35L56_RX_FORMATS,
		},
		.capture = {
			.stream_name = "SDW1 Capture",
			.channels_min = 1,
			.channels_max = 4,
			.rates = CS35L56_RATES,
			.formats = CS35L56_TX_FORMATS,
		},
		.symmetric_rate = 1,
		.ops = &cs35l56_sdw_dai_ops,
	}
};

static int cs35l56_wait_for_firmware_boot(struct cs35l56_private *cs35l56)
{
	unsigned int reg;
	unsigned int val;
	int ret;

	if (cs35l56->rev < CS35L56_REVID_B0)
		reg = CS35L56_DSP1_HALO_STATE_A1;
	else
		reg = CS35L56_DSP1_HALO_STATE;

	ret = regmap_read_poll_timeout(cs35l56->regmap, reg,
				       val,
				       (val < 0xFFFF) && (val >= CS35L56_HALO_STATE_BOOT_DONE),
				       CS35L56_HALO_STATE_POLL_US,
				       CS35L56_HALO_STATE_TIMEOUT_US);

	if ((ret < 0) && (ret != -ETIMEDOUT)) {
		dev_err(cs35l56->dev, "Failed to read HALO_STATE: %d\n", ret);
		return ret;
	}

	if ((ret == -ETIMEDOUT) || (val != CS35L56_HALO_STATE_BOOT_DONE)) {
		dev_err(cs35l56->dev, "Firmware boot fail: HALO_STATE=%#x\n", val);
		return -EIO;
	}

	return 0;
}

static inline void cs35l56_wait_min_reset_pulse(void)
{
	/* Satisfy minimum reset pulse width spec */
	usleep_range(CS35L56_RESET_PULSE_MIN_US, 2 * CS35L56_RESET_PULSE_MIN_US);
}

static const struct reg_sequence cs35l56_system_reset_seq[] = {
	REG_SEQ0(CS35L56_DSP_VIRTUAL1_MBOX_1, CS35L56_MBOX_CMD_SYSTEM_RESET),
};

static void cs35l56_system_reset(struct cs35l56_private *cs35l56)
{
	cs35l56->soft_resetting = true;

	/*
	 * Must enter cache-only first so there can't be any more register
	 * accesses other than the controlled system reset sequence below.
	 */
	regcache_cache_only(cs35l56->regmap, true);
	regmap_multi_reg_write_bypassed(cs35l56->regmap,
					cs35l56_system_reset_seq,
					ARRAY_SIZE(cs35l56_system_reset_seq));

	/* On SoundWire the registers won't be accessible until it re-enumerates. */
	if (cs35l56->sdw_peripheral)
		return;

	usleep_range(CS35L56_CONTROL_PORT_READY_US, CS35L56_CONTROL_PORT_READY_US + 400);
	regcache_cache_only(cs35l56->regmap, false);
}

static void cs35l56_secure_patch(struct cs35l56_private *cs35l56)
{
	int ret;

	/* Use wm_adsp to load and apply the firmware patch and coefficient files */
	ret = wm_adsp_power_up(&cs35l56->dsp);
	if (ret)
		dev_dbg(cs35l56->dev, "%s: wm_adsp_power_up ret %d\n", __func__, ret);
	else
		cs35l56_mbox_send(cs35l56, CS35L56_MBOX_CMD_AUDIO_REINIT);
}

static void cs35l56_patch(struct cs35l56_private *cs35l56)
{
	unsigned int reg;
	unsigned int val;
	int ret;

	/*
	 * Disable SoundWire interrupts to prevent race with IRQ work.
	 * Setting sdw_irq_no_unmask prevents the handler re-enabling
	 * the SoundWire interrupt.
	 */
	if (cs35l56->sdw_peripheral) {
		cs35l56->sdw_irq_no_unmask = true;
		flush_work(&cs35l56->sdw_irq_work);
		sdw_write_no_pm(cs35l56->sdw_peripheral, CS35L56_SDW_GEN_INT_MASK_1, 0);
		sdw_read_no_pm(cs35l56->sdw_peripheral, CS35L56_SDW_GEN_INT_STAT_1);
		sdw_write_no_pm(cs35l56->sdw_peripheral, CS35L56_SDW_GEN_INT_STAT_1, 0xFF);
		flush_work(&cs35l56->sdw_irq_work);
	}

	ret = cs35l56_mbox_send(cs35l56, CS35L56_MBOX_CMD_SHUTDOWN);
	if (ret)
		goto err;

	if (cs35l56->rev < CS35L56_REVID_B0)
		reg = CS35L56_DSP1_PM_CUR_STATE_A1;
	else
		reg = CS35L56_DSP1_PM_CUR_STATE;

	ret = regmap_read_poll_timeout(cs35l56->regmap, reg,
				       val, (val == CS35L56_HALO_STATE_SHUTDOWN),
				       CS35L56_HALO_STATE_POLL_US,
				       CS35L56_HALO_STATE_TIMEOUT_US);
	if (ret < 0)
		dev_err(cs35l56->dev, "Failed to poll PM_CUR_STATE to 1 is %d (ret %d)\n",
			val, ret);

	/* Use wm_adsp to load and apply the firmware patch and coefficient files */
	ret = wm_adsp_power_up(&cs35l56->dsp);
	if (ret) {
		dev_dbg(cs35l56->dev, "%s: wm_adsp_power_up ret %d\n", __func__, ret);
		goto err;
	}

	mutex_lock(&cs35l56->irq_lock);

	init_completion(&cs35l56->init_completion);

	cs35l56_system_reset(cs35l56);

	if (cs35l56->sdw_peripheral) {
		/*
		 * The system-reset causes the CS35L56 to detach from the bus.
		 * Wait for the manager to re-enumerate the CS35L56 and
		 * cs35l56_init() to run again.
		 */
		if (!wait_for_completion_timeout(&cs35l56->init_completion,
						 msecs_to_jiffies(5000))) {
			dev_err(cs35l56->dev, "%s: init_completion timed out (SDW)\n", __func__);
			goto err_unlock;
		}
	} else if (cs35l56_init(cs35l56)) {
		goto err_unlock;
	}

	regmap_clear_bits(cs35l56->regmap, CS35L56_PROTECTION_STATUS, CS35L56_FIRMWARE_MISSING);
	cs35l56->fw_patched = true;

err_unlock:
	mutex_unlock(&cs35l56->irq_lock);
err:
	/* Re-enable SoundWire interrupts */
	if (cs35l56->sdw_peripheral) {
		cs35l56->sdw_irq_no_unmask = false;
		sdw_write_no_pm(cs35l56->sdw_peripheral, CS35L56_SDW_GEN_INT_MASK_1,
				CS35L56_SDW_INT_MASK_CODEC_IRQ);
	}
}

static void cs35l56_dsp_work(struct work_struct *work)
{
	struct cs35l56_private *cs35l56 = container_of(work,
						       struct cs35l56_private,
						       dsp_work);

	if (!cs35l56->init_done)
		return;

	pm_runtime_get_sync(cs35l56->dev);

	/*
	 * When the device is running in secure mode the firmware files can
	 * only contain insecure tunings and therefore we do not need to
	 * shutdown the firmware to apply them and can use the lower cost
	 * reinit sequence instead.
	 */
	if (cs35l56->secured)
		cs35l56_secure_patch(cs35l56);
	else
		cs35l56_patch(cs35l56);

	pm_runtime_mark_last_busy(cs35l56->dev);
	pm_runtime_put_autosuspend(cs35l56->dev);
}

static int cs35l56_component_probe(struct snd_soc_component *component)
{
	struct cs35l56_private *cs35l56 = snd_soc_component_get_drvdata(component);
	struct dentry *debugfs_root = component->debugfs_root;

	BUILD_BUG_ON(ARRAY_SIZE(cs35l56_tx_input_texts) != ARRAY_SIZE(cs35l56_tx_input_values));

	if (!wait_for_completion_timeout(&cs35l56->init_completion,
					 msecs_to_jiffies(5000))) {
		dev_err(cs35l56->dev, "%s: init_completion timed out\n", __func__);
		return -ENODEV;
	}

	cs35l56->component = component;
	wm_adsp2_component_probe(&cs35l56->dsp, component);

	debugfs_create_bool("init_done", 0444, debugfs_root, &cs35l56->init_done);
	debugfs_create_bool("can_hibernate", 0444, debugfs_root, &cs35l56->can_hibernate);
	debugfs_create_bool("fw_patched", 0444, debugfs_root, &cs35l56->fw_patched);

	queue_work(cs35l56->dsp_wq, &cs35l56->dsp_work);

	return 0;
}

static void cs35l56_component_remove(struct snd_soc_component *component)
{
	struct cs35l56_private *cs35l56 = snd_soc_component_get_drvdata(component);

	cancel_work_sync(&cs35l56->dsp_work);
}

static int cs35l56_set_bias_level(struct snd_soc_component *component,
				  enum snd_soc_bias_level level)
{
	struct cs35l56_private *cs35l56 = snd_soc_component_get_drvdata(component);

	switch (level) {
	case SND_SOC_BIAS_STANDBY:
		/*
		 * Wait for patching to complete when transitioning from
		 * BIAS_OFF to BIAS_STANDBY
		 */
		if (snd_soc_component_get_bias_level(component) == SND_SOC_BIAS_OFF)
			cs35l56_wait_dsp_ready(cs35l56);

		break;
	default:
		break;
	}

	return 0;
}

static const struct snd_soc_component_driver soc_component_dev_cs35l56 = {
	.probe = cs35l56_component_probe,
	.remove = cs35l56_component_remove,

	.dapm_widgets = cs35l56_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(cs35l56_dapm_widgets),
	.dapm_routes = cs35l56_audio_map,
	.num_dapm_routes = ARRAY_SIZE(cs35l56_audio_map),
	.controls = cs35l56_controls,
	.num_controls = ARRAY_SIZE(cs35l56_controls),

	.set_bias_level = cs35l56_set_bias_level,

	.suspend_bias_off = 1, /* see cs35l56_system_resume() */
};

static const struct reg_sequence cs35l56_hibernate_seq[] = {
	/* This must be the last register access */
	REG_SEQ0(CS35L56_DSP_VIRTUAL1_MBOX_1, CS35L56_MBOX_CMD_HIBERNATE_NOW),
};

static const struct reg_sequence cs35l56_hibernate_wake_seq[] = {
	REG_SEQ0(CS35L56_DSP_VIRTUAL1_MBOX_1, CS35L56_MBOX_CMD_WAKEUP),
};

int cs35l56_runtime_suspend(struct device *dev)
{
	struct cs35l56_private *cs35l56 = dev_get_drvdata(dev);
	unsigned int val;
	int ret;

	if (!cs35l56->init_done)
		return 0;

	/* Firmware must have entered a power-save state */
	ret = regmap_read_poll_timeout(cs35l56->regmap,
				       CS35L56_TRANSDUCER_ACTUAL_PS,
				       val, (val >= CS35L56_PS3),
				       CS35L56_PS3_POLL_US,
				       CS35L56_PS3_TIMEOUT_US);
	if (ret)
		dev_warn(cs35l56->dev, "PS3 wait failed: %d\n", ret);

	/* Clear BOOT_DONE so it can be used to detect a reboot */
	regmap_write(cs35l56->regmap, CS35L56_IRQ1_EINT_4, CS35L56_OTP_BOOT_DONE_MASK);

	if (!cs35l56->can_hibernate) {
		regcache_cache_only(cs35l56->regmap, true);
		dev_dbg(dev, "Suspended: no hibernate");

		return 0;
	}

	/*
	 * Enable auto-hibernate. If it is woken by some other wake source
	 * it will automatically return to hibernate.
	 */
	cs35l56_mbox_send(cs35l56, CS35L56_MBOX_CMD_ALLOW_AUTO_HIBERNATE);

	/*
	 * Must enter cache-only first so there can't be any more register
	 * accesses other than the controlled hibernate sequence below.
	 */
	regcache_cache_only(cs35l56->regmap, true);

	regmap_multi_reg_write_bypassed(cs35l56->regmap,
					cs35l56_hibernate_seq,
					ARRAY_SIZE(cs35l56_hibernate_seq));

	dev_dbg(dev, "Suspended: hibernate");

	return 0;
}
EXPORT_SYMBOL_NS_GPL(cs35l56_runtime_suspend, SND_SOC_CS35L56_CORE);

static int __maybe_unused cs35l56_runtime_resume_i2c_spi(struct device *dev)
{
	struct cs35l56_private *cs35l56 = dev_get_drvdata(dev);

	if (!cs35l56->init_done)
		return 0;

	return cs35l56_runtime_resume_common(cs35l56);
}

int cs35l56_runtime_resume_common(struct cs35l56_private *cs35l56)
{
	unsigned int val;
	int ret;

	if (!cs35l56->can_hibernate)
		goto out_sync;

	if (!cs35l56->sdw_peripheral) {
		/*
		 * Dummy transaction to trigger I2C/SPI auto-wake. This will NAK on I2C.
		 * Must be done before releasing cache-only.
		 */
		regmap_multi_reg_write_bypassed(cs35l56->regmap,
						cs35l56_hibernate_wake_seq,
						ARRAY_SIZE(cs35l56_hibernate_wake_seq));

		usleep_range(CS35L56_CONTROL_PORT_READY_US,
			     CS35L56_CONTROL_PORT_READY_US + 400);
	}

out_sync:
	regcache_cache_only(cs35l56->regmap, false);

	ret = cs35l56_wait_for_firmware_boot(cs35l56);
	if (ret) {
		dev_err(cs35l56->dev, "Hibernate wake failed: %d\n", ret);
		goto err;
	}

	ret = cs35l56_mbox_send(cs35l56, CS35L56_MBOX_CMD_PREVENT_AUTO_HIBERNATE);
	if (ret)
		goto err;

	/* BOOT_DONE will be 1 if the amp reset */
	regmap_read(cs35l56->regmap, CS35L56_IRQ1_EINT_4, &val);
	if (val & CS35L56_OTP_BOOT_DONE_MASK) {
		dev_dbg(cs35l56->dev, "Registers reset in suspend\n");
		regcache_mark_dirty(cs35l56->regmap);
	}

	regcache_sync(cs35l56->regmap);

	dev_dbg(cs35l56->dev, "Resumed");

	return 0;

err:
	regmap_write(cs35l56->regmap, CS35L56_DSP_VIRTUAL1_MBOX_1,
		     CS35L56_MBOX_CMD_HIBERNATE_NOW);

	regcache_cache_only(cs35l56->regmap, true);

	return ret;
}
EXPORT_SYMBOL_NS_GPL(cs35l56_runtime_resume_common, SND_SOC_CS35L56_CORE);

static int cs35l56_is_fw_reload_needed(struct cs35l56_private *cs35l56)
{
	unsigned int val;
	int ret;

	/* Nothing to re-patch if we haven't done any patching yet. */
	if (!cs35l56->fw_patched)
		return false;

	/*
	 * If we have control of RESET we will have asserted it so the firmware
	 * will need re-patching.
	 */
	if (cs35l56->reset_gpio)
		return true;

	/*
	 * In secure mode FIRMWARE_MISSING is cleared by the BIOS loader so
	 * can't be used here to test for memory retention.
	 * Assume that tuning must be re-loaded.
	 */
	if (cs35l56->secured)
		return true;

	ret = pm_runtime_resume_and_get(cs35l56->dev);
	if (ret) {
		dev_err(cs35l56->dev, "Failed to runtime_get: %d\n", ret);
		return ret;
	}

	ret = regmap_read(cs35l56->regmap, CS35L56_PROTECTION_STATUS, &val);
	if (ret)
		dev_err(cs35l56->dev, "Failed to read PROTECTION_STATUS: %d\n", ret);
	else
		ret = !!(val & CS35L56_FIRMWARE_MISSING);

	pm_runtime_put_autosuspend(cs35l56->dev);

	return ret;
}

int cs35l56_system_suspend(struct device *dev)
{
	struct cs35l56_private *cs35l56 = dev_get_drvdata(dev);

	dev_dbg(dev, "system_suspend\n");

	if (cs35l56->component)
		flush_work(&cs35l56->dsp_work);

	/*
	 * The interrupt line is normally shared, but after we start suspending
	 * we can't check if our device is the source of an interrupt, and can't
	 * clear it. Prevent this race by temporarily disabling the parent irq
	 * until we reach _no_irq.
	 */
	if (cs35l56->irq)
		disable_irq(cs35l56->irq);

	return pm_runtime_force_suspend(dev);
}
EXPORT_SYMBOL_GPL(cs35l56_system_suspend);

int cs35l56_system_suspend_late(struct device *dev)
{
	struct cs35l56_private *cs35l56 = dev_get_drvdata(dev);

	dev_dbg(dev, "system_suspend_late\n");

	/*
	 * Assert RESET before removing supplies.
	 * RESET is usually shared by all amps so it must not be asserted until
	 * all driver instances have done their suspend() stage.
	 */
	if (cs35l56->reset_gpio) {
		gpiod_set_value_cansleep(cs35l56->reset_gpio, 0);
		cs35l56_wait_min_reset_pulse();
	}

	regulator_bulk_disable(ARRAY_SIZE(cs35l56->supplies), cs35l56->supplies);

	return 0;
}
EXPORT_SYMBOL_GPL(cs35l56_system_suspend_late);

int cs35l56_system_suspend_no_irq(struct device *dev)
{
	struct cs35l56_private *cs35l56 = dev_get_drvdata(dev);

	dev_dbg(dev, "system_suspend_no_irq\n");

	/* Handlers are now disabled so the parent IRQ can safely be re-enabled. */
	if (cs35l56->irq)
		enable_irq(cs35l56->irq);

	return 0;
}
EXPORT_SYMBOL_GPL(cs35l56_system_suspend_no_irq);

int cs35l56_system_resume_no_irq(struct device *dev)
{
	struct cs35l56_private *cs35l56 = dev_get_drvdata(dev);

	dev_dbg(dev, "system_resume_no_irq\n");

	/*
	 * WAKE interrupts unmask if the CS35L56 hibernates, which can cause
	 * spurious interrupts, and the interrupt line is normally shared.
	 * We can't check if our device is the source of an interrupt, and can't
	 * clear it, until it has fully resumed. Prevent this race by temporarily
	 * disabling the parent irq until we complete resume().
	 */
	if (cs35l56->irq)
		disable_irq(cs35l56->irq);

	return 0;
}
EXPORT_SYMBOL_GPL(cs35l56_system_resume_no_irq);

int cs35l56_system_resume_early(struct device *dev)
{
	struct cs35l56_private *cs35l56 = dev_get_drvdata(dev);
	int ret;

	dev_dbg(dev, "system_resume_early\n");

	/* Ensure a spec-compliant RESET pulse. */
	if (cs35l56->reset_gpio) {
		gpiod_set_value_cansleep(cs35l56->reset_gpio, 0);
		cs35l56_wait_min_reset_pulse();
	}

	/* Enable supplies before releasing RESET. */
	ret = regulator_bulk_enable(ARRAY_SIZE(cs35l56->supplies), cs35l56->supplies);
	if (ret) {
		dev_err(dev, "system_resume_early failed to enable supplies: %d\n", ret);
		return ret;
	}

	/* Release shared RESET before drivers start resume(). */
	gpiod_set_value_cansleep(cs35l56->reset_gpio, 1);

	return 0;
}
EXPORT_SYMBOL_GPL(cs35l56_system_resume_early);

int cs35l56_system_resume(struct device *dev)
{
	struct cs35l56_private *cs35l56 = dev_get_drvdata(dev);
	int ret;

	dev_dbg(dev, "system_resume\n");

	/* Undo pm_runtime_force_suspend() before re-enabling the irq */
	ret = pm_runtime_force_resume(dev);
	if (cs35l56->irq)
		enable_irq(cs35l56->irq);

	if (ret)
		return ret;

	/* Firmware won't have been loaded if the component hasn't probed */
	if (!cs35l56->component)
		return 0;

	ret = cs35l56_is_fw_reload_needed(cs35l56);
	dev_dbg(cs35l56->dev, "fw_reload_needed: %d\n", ret);
	if (ret < 1)
		return ret;

	cs35l56->fw_patched = false;
	queue_work(cs35l56->dsp_wq, &cs35l56->dsp_work);

	/*
	 * suspend_bias_off ensures we are now in BIAS_OFF so there will be
	 * a BIAS_OFF->BIAS_STANDBY transition to complete dsp patching.
	 */

	return 0;
}
EXPORT_SYMBOL_GPL(cs35l56_system_resume);

static int cs35l56_dsp_init(struct cs35l56_private *cs35l56)
{
	struct wm_adsp *dsp;
	int ret;

	cs35l56->dsp_wq = create_singlethread_workqueue("cs35l56-dsp");
	if (!cs35l56->dsp_wq)
		return -ENOMEM;

	INIT_WORK(&cs35l56->dsp_work, cs35l56_dsp_work);

	dsp = &cs35l56->dsp;
	dsp->part = "cs35l56";
	dsp->cs_dsp.num = 1;
	dsp->cs_dsp.type = WMFW_HALO;
	dsp->cs_dsp.rev = 0;
	dsp->fw = 12;
	dsp->cs_dsp.dev = cs35l56->dev;
	dsp->cs_dsp.regmap = cs35l56->regmap;
	dsp->cs_dsp.base = CS35L56_DSP1_CORE_BASE;
	dsp->cs_dsp.base_sysinfo = CS35L56_DSP1_SYS_INFO_ID;
	dsp->cs_dsp.mem = cs35l56_dsp1_regions;
	dsp->cs_dsp.num_mems = ARRAY_SIZE(cs35l56_dsp1_regions);
	dsp->cs_dsp.no_core_startstop = true;
	dsp->wmfw_optional = true;

	dev_dbg(cs35l56->dev, "DSP system name: '%s'\n", dsp->system_name);

	ret = wm_halo_init(dsp);
	if (ret != 0) {
		dev_err(cs35l56->dev, "wm_halo_init failed\n");
		return ret;
	}

	return 0;
}

static int cs35l56_get_firmware_uid(struct cs35l56_private *cs35l56)
{
	struct device *dev = cs35l56->dev;
	const char *prop;
	int ret;

	ret = device_property_read_string(dev, "cirrus,firmware-uid", &prop);
	/* If bad sw node property, return 0 and fallback to legacy firmware path */
	if (ret < 0)
		return 0;

	cs35l56->dsp.system_name = devm_kstrdup(dev, prop, GFP_KERNEL);
	if (cs35l56->dsp.system_name == NULL)
		return -ENOMEM;

	dev_dbg(dev, "Firmware UID: %s\n", cs35l56->dsp.system_name);

	return 0;
}

int cs35l56_common_probe(struct cs35l56_private *cs35l56)
{
	int ret;

	init_completion(&cs35l56->init_completion);
	mutex_init(&cs35l56->irq_lock);

	dev_set_drvdata(cs35l56->dev, cs35l56);

	cs35l56_fill_supply_names(cs35l56->supplies);
	ret = devm_regulator_bulk_get(cs35l56->dev, ARRAY_SIZE(cs35l56->supplies),
				      cs35l56->supplies);
	if (ret != 0)
		return dev_err_probe(cs35l56->dev, ret, "Failed to request supplies\n");

	/* Reset could be controlled by the BIOS or shared by multiple amps */
	cs35l56->reset_gpio = devm_gpiod_get_optional(cs35l56->dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(cs35l56->reset_gpio)) {
		ret = PTR_ERR(cs35l56->reset_gpio);
		/*
		 * If RESET is shared the first amp to probe will grab the reset
		 * line and reset all the amps
		 */
		if (ret != -EBUSY)
			return dev_err_probe(cs35l56->dev, ret, "Failed to get reset GPIO\n");

		dev_info(cs35l56->dev, "Reset GPIO busy, assume shared reset\n");
		cs35l56->reset_gpio = NULL;
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(cs35l56->supplies), cs35l56->supplies);
	if (ret != 0)
		return dev_err_probe(cs35l56->dev, ret, "Failed to enable supplies\n");

	if (cs35l56->reset_gpio) {
		cs35l56_wait_min_reset_pulse();
		gpiod_set_value_cansleep(cs35l56->reset_gpio, 1);
	}

	ret = cs35l56_get_firmware_uid(cs35l56);
	if (ret != 0)
		goto err;

	ret = cs35l56_dsp_init(cs35l56);
	if (ret < 0) {
		dev_err_probe(cs35l56->dev, ret, "DSP init failed\n");
		goto err;
	}

	ret = devm_snd_soc_register_component(cs35l56->dev,
					      &soc_component_dev_cs35l56,
					      cs35l56_dai, ARRAY_SIZE(cs35l56_dai));
	if (ret < 0) {
		dev_err_probe(cs35l56->dev, ret, "Register codec failed\n");
		goto err;
	}

	return 0;

err:
	gpiod_set_value_cansleep(cs35l56->reset_gpio, 0);
	regulator_bulk_disable(ARRAY_SIZE(cs35l56->supplies), cs35l56->supplies);

	return ret;
}
EXPORT_SYMBOL_NS_GPL(cs35l56_common_probe, SND_SOC_CS35L56_CORE);

int cs35l56_init(struct cs35l56_private *cs35l56)
{
	int ret;
	unsigned int devid, revid, otpid, secured;

	/*
	 * Check whether the actions associated with soft reset or one time
	 * init need to be performed.
	 */
	if (cs35l56->soft_resetting)
		goto post_soft_reset;

	if (cs35l56->init_done)
		return 0;

	pm_runtime_set_autosuspend_delay(cs35l56->dev, 100);
	pm_runtime_use_autosuspend(cs35l56->dev);
	pm_runtime_set_active(cs35l56->dev);
	pm_runtime_enable(cs35l56->dev);

	/*
	 * If the system is not using a reset_gpio then issue a
	 * dummy read to force a wakeup.
	 */
	if (!cs35l56->reset_gpio)
		regmap_read(cs35l56->regmap, CS35L56_DSP_VIRTUAL1_MBOX_1, &devid);

	/* Wait for control port to be ready (datasheet tIRS). */
	usleep_range(CS35L56_CONTROL_PORT_READY_US,
		     CS35L56_CONTROL_PORT_READY_US + 400);

	/*
	 * The HALO_STATE register is in different locations on Ax and B0
	 * devices so the REVID needs to be determined before waiting for the
	 * firmware to boot.
	 */
	ret = regmap_read(cs35l56->regmap, CS35L56_REVID, &revid);
	if (ret < 0) {
		dev_err(cs35l56->dev, "Get Revision ID failed\n");
		return ret;
	}
	cs35l56->rev = revid & (CS35L56_AREVID_MASK | CS35L56_MTLREVID_MASK);

	ret = cs35l56_wait_for_firmware_boot(cs35l56);
	if (ret)
		return ret;

	ret = regmap_read(cs35l56->regmap, CS35L56_DEVID, &devid);
	if (ret < 0) {
		dev_err(cs35l56->dev, "Get Device ID failed\n");
		return ret;
	}
	devid &= CS35L56_DEVID_MASK;

	switch (devid) {
	case 0x35A56:
		break;
	default:
		dev_err(cs35l56->dev, "Unknown device %x\n", devid);
		return ret;
	}

	ret = regmap_read(cs35l56->regmap, CS35L56_DSP_RESTRICT_STS1, &secured);
	if (ret) {
		dev_err(cs35l56->dev, "Get Secure status failed\n");
		return ret;
	}

	/* When any bus is restricted treat the device as secured */
	if (secured & CS35L56_RESTRICTED_MASK)
		cs35l56->secured = true;

	ret = regmap_read(cs35l56->regmap, CS35L56_OTPID, &otpid);
	if (ret < 0) {
		dev_err(cs35l56->dev, "Get OTP ID failed\n");
		return ret;
	}

	dev_info(cs35l56->dev, "Cirrus Logic CS35L56%s Rev %02X OTP%d\n",
		 cs35l56->secured ? "s" : "", cs35l56->rev, otpid);

	/* Populate the DSP information with the revision and security state */
	cs35l56->dsp.part = devm_kasprintf(cs35l56->dev, GFP_KERNEL, "cs35l56%s-%02x",
					   cs35l56->secured ? "s" : "", cs35l56->rev);
	if (!cs35l56->dsp.part)
		return -ENOMEM;

	/* Wake source and *_BLOCKED interrupts default to unmasked, so mask them */
	regmap_write(cs35l56->regmap, CS35L56_IRQ1_MASK_20, 0xffffffff);
	regmap_update_bits(cs35l56->regmap, CS35L56_IRQ1_MASK_1,
			   CS35L56_AMP_SHORT_ERR_EINT1_MASK,
			   0);
	regmap_update_bits(cs35l56->regmap, CS35L56_IRQ1_MASK_8,
			   CS35L56_TEMP_ERR_EINT1_MASK,
			   0);

	if (!cs35l56->reset_gpio) {
		dev_dbg(cs35l56->dev, "No reset gpio: using soft reset\n");
		cs35l56_system_reset(cs35l56);
		if (cs35l56->sdw_peripheral) {
			/* Keep alive while we wait for re-enumeration */
			pm_runtime_get_noresume(cs35l56->dev);
			return 0;
		}
	}

post_soft_reset:
	if (cs35l56->soft_resetting) {
		cs35l56->soft_resetting = false;

		/* Done re-enumerating after one-time init so release the keep-alive */
		if (cs35l56->sdw_peripheral && !cs35l56->init_done)
			pm_runtime_put_noidle(cs35l56->dev);

		regcache_mark_dirty(cs35l56->regmap);
		ret = cs35l56_wait_for_firmware_boot(cs35l56);
		if (ret)
			return ret;

		dev_dbg(cs35l56->dev, "Firmware rebooted after soft reset\n");
	}

	/* Disable auto-hibernate so that runtime_pm has control */
	ret = cs35l56_mbox_send(cs35l56, CS35L56_MBOX_CMD_PREVENT_AUTO_HIBERNATE);
	if (ret)
		return ret;

	/* Populate soft registers in the regmap cache */
	cs35l56_reread_firmware_registers(cs35l56->dev, cs35l56->regmap);

	/* Registers could be dirty after soft reset or SoundWire enumeration */
	regcache_sync(cs35l56->regmap);

	cs35l56->init_done = true;
	complete(&cs35l56->init_completion);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(cs35l56_init, SND_SOC_CS35L56_CORE);

void cs35l56_remove(struct cs35l56_private *cs35l56)
{
	cs35l56->init_done = false;

	/*
	 * WAKE IRQs unmask if CS35L56 hibernates so free the handler to
	 * prevent it racing with remove().
	 */
	if (cs35l56->irq)
		devm_free_irq(cs35l56->dev, cs35l56->irq, cs35l56);

	flush_workqueue(cs35l56->dsp_wq);
	destroy_workqueue(cs35l56->dsp_wq);

	pm_runtime_suspend(cs35l56->dev);
	pm_runtime_disable(cs35l56->dev);

	regcache_cache_only(cs35l56->regmap, true);

	gpiod_set_value_cansleep(cs35l56->reset_gpio, 0);
	regulator_bulk_disable(ARRAY_SIZE(cs35l56->supplies), cs35l56->supplies);
}
EXPORT_SYMBOL_NS_GPL(cs35l56_remove, SND_SOC_CS35L56_CORE);

const struct dev_pm_ops cs35l56_pm_ops_i2c_spi = {
	SET_RUNTIME_PM_OPS(cs35l56_runtime_suspend, cs35l56_runtime_resume_i2c_spi, NULL)
	SYSTEM_SLEEP_PM_OPS(cs35l56_system_suspend, cs35l56_system_resume)
	LATE_SYSTEM_SLEEP_PM_OPS(cs35l56_system_suspend_late, cs35l56_system_resume_early)
	NOIRQ_SYSTEM_SLEEP_PM_OPS(cs35l56_system_suspend_no_irq, cs35l56_system_resume_no_irq)
};
EXPORT_SYMBOL_NS_GPL(cs35l56_pm_ops_i2c_spi, SND_SOC_CS35L56_CORE);

MODULE_DESCRIPTION("ASoC CS35L56 driver");
MODULE_IMPORT_NS(SND_SOC_CS35L56_SHARED);
MODULE_AUTHOR("Richard Fitzgerald <rf@opensource.cirrus.com>");
MODULE_AUTHOR("Simon Trimmer <simont@opensource.cirrus.com>");
MODULE_LICENSE("GPL");
