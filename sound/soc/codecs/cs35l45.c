// SPDX-License-Identifier: GPL-2.0
//
// cs35l45.c - CS35L45 ALSA SoC audio driver
//
// Copyright 2019-2022 Cirrus Logic, Inc.
//
// Author: James Schulman <james.schulman@cirrus.com>

#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>
#include <linux/firmware.h>
#include <linux/regulator/consumer.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#include "cs35l45.h"

static bool cs35l45_check_cspl_mbox_sts(const enum cs35l45_cspl_mboxcmd cmd,
					enum cs35l45_cspl_mboxstate sts)
{
	switch (cmd) {
	case CSPL_MBOX_CMD_NONE:
	case CSPL_MBOX_CMD_UNKNOWN_CMD:
		return true;
	case CSPL_MBOX_CMD_PAUSE:
	case CSPL_MBOX_CMD_OUT_OF_HIBERNATE:
		return (sts == CSPL_MBOX_STS_PAUSED);
	case CSPL_MBOX_CMD_RESUME:
		return (sts == CSPL_MBOX_STS_RUNNING);
	case CSPL_MBOX_CMD_REINIT:
		return (sts == CSPL_MBOX_STS_RUNNING);
	case CSPL_MBOX_CMD_STOP_PRE_REINIT:
		return (sts == CSPL_MBOX_STS_RDY_FOR_REINIT);
	case CSPL_MBOX_CMD_HIBERNATE:
		return (sts == CSPL_MBOX_STS_HIBERNATE);
	default:
		return false;
	}
}

static int cs35l45_set_cspl_mbox_cmd(struct cs35l45_private *cs35l45,
				      struct regmap *regmap,
				      const enum cs35l45_cspl_mboxcmd cmd)
{
	unsigned int sts = 0, i;
	int ret;

	if (!cs35l45->dsp.cs_dsp.running) {
		dev_err(cs35l45->dev, "DSP not running\n");
		return -EPERM;
	}

	// Set mailbox cmd
	ret = regmap_write(regmap, CS35L45_DSP_VIRT1_MBOX_1, cmd);
	if (ret < 0) {
		if (cmd != CSPL_MBOX_CMD_OUT_OF_HIBERNATE)
			dev_err(cs35l45->dev, "Failed to write MBOX: %d\n", ret);
		return ret;
	}

	// Read mailbox status and verify it is appropriate for the given cmd
	for (i = 0; i < 5; i++) {
		usleep_range(1000, 1100);

		ret = regmap_read(regmap, CS35L45_DSP_MBOX_2, &sts);
		if (ret < 0) {
			dev_err(cs35l45->dev, "Failed to read MBOX STS: %d\n", ret);
			continue;
		}

		if (!cs35l45_check_cspl_mbox_sts(cmd, sts))
			dev_dbg(cs35l45->dev, "[%u] cmd %u returned invalid sts %u", i, cmd, sts);
		else
			return 0;
	}

	if (cmd != CSPL_MBOX_CMD_OUT_OF_HIBERNATE)
		dev_err(cs35l45->dev, "Failed to set mailbox cmd %u (status %u)\n", cmd, sts);

	return -ENOMSG;
}

static int cs35l45_global_en_ev(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct cs35l45_private *cs35l45 = snd_soc_component_get_drvdata(component);

	dev_dbg(cs35l45->dev, "%s event : %x\n", __func__, event);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_write(cs35l45->regmap, CS35L45_GLOBAL_ENABLES,
			     CS35L45_GLOBAL_EN_MASK);

		usleep_range(CS35L45_POST_GLOBAL_EN_US, CS35L45_POST_GLOBAL_EN_US + 100);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		usleep_range(CS35L45_PRE_GLOBAL_DIS_US, CS35L45_PRE_GLOBAL_DIS_US + 100);

		regmap_write(cs35l45->regmap, CS35L45_GLOBAL_ENABLES, 0);
		break;
	default:
		break;
	}

	return 0;
}

static int cs35l45_dsp_preload_ev(struct snd_soc_dapm_widget *w,
				  struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct cs35l45_private *cs35l45 = snd_soc_component_get_drvdata(component);
	int ret;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		if (cs35l45->dsp.cs_dsp.booted)
			return 0;

		return wm_adsp_early_event(w, kcontrol, event);
	case SND_SOC_DAPM_POST_PMU:
		if (cs35l45->dsp.cs_dsp.running)
			return 0;

		regmap_set_bits(cs35l45->regmap, CS35L45_PWRMGT_CTL,
				   CS35L45_MEM_RDY_MASK);

		return wm_adsp_event(w, kcontrol, event);
	case SND_SOC_DAPM_PRE_PMD:
		if (cs35l45->dsp.preloaded)
			return 0;

		if (cs35l45->dsp.cs_dsp.running) {
			ret = wm_adsp_event(w, kcontrol, event);
			if (ret)
				return ret;
		}

		return wm_adsp_early_event(w, kcontrol, event);
	default:
		return 0;
	}
}

static int cs35l45_dsp_audio_ev(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct cs35l45_private *cs35l45 = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		return cs35l45_set_cspl_mbox_cmd(cs35l45, cs35l45->regmap,
						 CSPL_MBOX_CMD_RESUME);
	case SND_SOC_DAPM_PRE_PMD:
		return cs35l45_set_cspl_mbox_cmd(cs35l45, cs35l45->regmap,
						 CSPL_MBOX_CMD_PAUSE);
	default:
		return 0;
	}

	return 0;
}

static int cs35l45_activate_ctl(struct snd_soc_component *component,
				const char *ctl_name, bool active)
{
	struct snd_card *card = component->card->snd_card;
	struct snd_kcontrol *kcontrol;
	struct snd_kcontrol_volatile *vd;
	unsigned int index_offset;

	kcontrol = snd_soc_component_get_kcontrol(component, ctl_name);
	if (!kcontrol) {
		dev_err(component->dev, "Can't find kcontrol %s\n", ctl_name);
		return -EINVAL;
	}

	index_offset = snd_ctl_get_ioff(kcontrol, &kcontrol->id);
	vd = &kcontrol->vd[index_offset];
	if (active)
		vd->access |= SNDRV_CTL_ELEM_ACCESS_WRITE;
	else
		vd->access &= ~SNDRV_CTL_ELEM_ACCESS_WRITE;

	snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_INFO, &kcontrol->id);

	return 0;
}

static int cs35l45_amplifier_mode_get(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
			snd_soc_kcontrol_component(kcontrol);
	struct cs35l45_private *cs35l45 =
			snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = cs35l45->amplifier_mode;

	return 0;
}

static int cs35l45_amplifier_mode_put(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
			snd_soc_kcontrol_component(kcontrol);
	struct cs35l45_private *cs35l45 =
			snd_soc_component_get_drvdata(component);
	struct snd_soc_dapm_context *dapm =
			snd_soc_component_get_dapm(component);
	unsigned int amp_state;
	int ret;

	if ((ucontrol->value.integer.value[0] == cs35l45->amplifier_mode) ||
	    (ucontrol->value.integer.value[0] > AMP_MODE_RCV))
		return 0;

	snd_soc_dapm_mutex_lock(dapm);

	ret = regmap_read(cs35l45->regmap, CS35L45_BLOCK_ENABLES, &amp_state);
	if (ret < 0) {
		dev_err(cs35l45->dev, "Failed to read AMP state: %d\n", ret);
		snd_soc_dapm_mutex_unlock(dapm);
		return ret;
	}

	regmap_clear_bits(cs35l45->regmap, CS35L45_BLOCK_ENABLES,
				  CS35L45_AMP_EN_MASK);
	snd_soc_component_disable_pin_unlocked(component, "SPK");
	snd_soc_dapm_sync_unlocked(dapm);

	if (ucontrol->value.integer.value[0] == AMP_MODE_SPK) {
		regmap_clear_bits(cs35l45->regmap, CS35L45_BLOCK_ENABLES,
				  CS35L45_RCV_EN_MASK);

		regmap_update_bits(cs35l45->regmap, CS35L45_BLOCK_ENABLES,
				   CS35L45_BST_EN_MASK,
				   CS35L45_BST_ENABLE << CS35L45_BST_EN_SHIFT);

		regmap_update_bits(cs35l45->regmap, CS35L45_HVLV_CONFIG,
				   CS35L45_HVLV_MODE_MASK,
				   CS35L45_HVLV_OPERATION <<
				   CS35L45_HVLV_MODE_SHIFT);

		ret = cs35l45_activate_ctl(component, "Analog PCM Volume", true);
		if (ret < 0)
			dev_err(cs35l45->dev,
				"Unable to deactivate ctl (%d)\n", ret);

	} else  /* AMP_MODE_RCV */ {
		regmap_set_bits(cs35l45->regmap, CS35L45_BLOCK_ENABLES,
				CS35L45_RCV_EN_MASK);

		regmap_update_bits(cs35l45->regmap, CS35L45_BLOCK_ENABLES,
				   CS35L45_BST_EN_MASK,
				   CS35L45_BST_DISABLE_FET_OFF <<
				   CS35L45_BST_EN_SHIFT);

		regmap_update_bits(cs35l45->regmap, CS35L45_HVLV_CONFIG,
				   CS35L45_HVLV_MODE_MASK,
				   CS35L45_FORCE_LV_OPERATION <<
				   CS35L45_HVLV_MODE_SHIFT);

		regmap_clear_bits(cs35l45->regmap,
				  CS35L45_BLOCK_ENABLES2,
				  CS35L45_AMP_DRE_EN_MASK);

		regmap_update_bits(cs35l45->regmap, CS35L45_AMP_GAIN,
				   CS35L45_AMP_GAIN_PCM_MASK,
				   CS35L45_AMP_GAIN_PCM_13DBV <<
				   CS35L45_AMP_GAIN_PCM_SHIFT);

		ret = cs35l45_activate_ctl(component, "Analog PCM Volume", false);
		if (ret < 0)
			dev_err(cs35l45->dev,
				"Unable to deactivate ctl (%d)\n", ret);
	}

	if (amp_state & CS35L45_AMP_EN_MASK)
		regmap_set_bits(cs35l45->regmap, CS35L45_BLOCK_ENABLES,
				CS35L45_AMP_EN_MASK);

	snd_soc_component_enable_pin_unlocked(component, "SPK");
	snd_soc_dapm_sync_unlocked(dapm);
	snd_soc_dapm_mutex_unlock(dapm);

	cs35l45->amplifier_mode = ucontrol->value.integer.value[0];

	return 1;
}

static const char * const cs35l45_asp_tx_txt[] = {
	"Zero", "ASP_RX1", "ASP_RX2",
	"VMON", "IMON", "ERR_VOL",
	"VDD_BATTMON", "VDD_BSTMON",
	"DSP_TX1", "DSP_TX2",
	"Interpolator", "IL_TARGET",
};

static const unsigned int cs35l45_asp_tx_val[] = {
	CS35L45_PCM_SRC_ZERO, CS35L45_PCM_SRC_ASP_RX1, CS35L45_PCM_SRC_ASP_RX2,
	CS35L45_PCM_SRC_VMON, CS35L45_PCM_SRC_IMON, CS35L45_PCM_SRC_ERR_VOL,
	CS35L45_PCM_SRC_VDD_BATTMON, CS35L45_PCM_SRC_VDD_BSTMON,
	CS35L45_PCM_SRC_DSP_TX1, CS35L45_PCM_SRC_DSP_TX2,
	CS35L45_PCM_SRC_INTERPOLATOR, CS35L45_PCM_SRC_IL_TARGET,
};

static const struct soc_enum cs35l45_asp_tx_enums[] = {
	SOC_VALUE_ENUM_SINGLE(CS35L45_ASPTX1_INPUT, 0, CS35L45_PCM_SRC_MASK,
			      ARRAY_SIZE(cs35l45_asp_tx_txt), cs35l45_asp_tx_txt,
			      cs35l45_asp_tx_val),
	SOC_VALUE_ENUM_SINGLE(CS35L45_ASPTX2_INPUT, 0, CS35L45_PCM_SRC_MASK,
			      ARRAY_SIZE(cs35l45_asp_tx_txt), cs35l45_asp_tx_txt,
			      cs35l45_asp_tx_val),
	SOC_VALUE_ENUM_SINGLE(CS35L45_ASPTX3_INPUT, 0, CS35L45_PCM_SRC_MASK,
			      ARRAY_SIZE(cs35l45_asp_tx_txt), cs35l45_asp_tx_txt,
			      cs35l45_asp_tx_val),
	SOC_VALUE_ENUM_SINGLE(CS35L45_ASPTX4_INPUT, 0, CS35L45_PCM_SRC_MASK,
			      ARRAY_SIZE(cs35l45_asp_tx_txt), cs35l45_asp_tx_txt,
			      cs35l45_asp_tx_val),
	SOC_VALUE_ENUM_SINGLE(CS35L45_ASPTX5_INPUT, 0, CS35L45_PCM_SRC_MASK,
			      ARRAY_SIZE(cs35l45_asp_tx_txt), cs35l45_asp_tx_txt,
			      cs35l45_asp_tx_val),
};

static const char * const cs35l45_dsp_rx_txt[] = {
	"Zero", "ASP_RX1", "ASP_RX2",
	"VMON", "IMON", "ERR_VOL",
	"CLASSH_TGT", "VDD_BATTMON",
	"VDD_BSTMON", "TEMPMON",
};

static const unsigned int cs35l45_dsp_rx_val[] = {
	CS35L45_PCM_SRC_ZERO, CS35L45_PCM_SRC_ASP_RX1, CS35L45_PCM_SRC_ASP_RX2,
	CS35L45_PCM_SRC_VMON, CS35L45_PCM_SRC_IMON, CS35L45_PCM_SRC_ERR_VOL,
	CS35L45_PCM_SRC_CLASSH_TGT, CS35L45_PCM_SRC_VDD_BATTMON,
	CS35L45_PCM_SRC_VDD_BSTMON, CS35L45_PCM_SRC_TEMPMON,
};

static const struct soc_enum cs35l45_dsp_rx_enums[] = {
	SOC_VALUE_ENUM_SINGLE(CS35L45_DSP1RX1_INPUT, 0, CS35L45_PCM_SRC_MASK,
			      ARRAY_SIZE(cs35l45_dsp_rx_txt), cs35l45_dsp_rx_txt,
			      cs35l45_dsp_rx_val),
	SOC_VALUE_ENUM_SINGLE(CS35L45_DSP1RX2_INPUT, 0, CS35L45_PCM_SRC_MASK,
			      ARRAY_SIZE(cs35l45_dsp_rx_txt), cs35l45_dsp_rx_txt,
			      cs35l45_dsp_rx_val),
	SOC_VALUE_ENUM_SINGLE(CS35L45_DSP1RX3_INPUT, 0, CS35L45_PCM_SRC_MASK,
			      ARRAY_SIZE(cs35l45_dsp_rx_txt), cs35l45_dsp_rx_txt,
			      cs35l45_dsp_rx_val),
	SOC_VALUE_ENUM_SINGLE(CS35L45_DSP1RX4_INPUT, 0, CS35L45_PCM_SRC_MASK,
			      ARRAY_SIZE(cs35l45_dsp_rx_txt), cs35l45_dsp_rx_txt,
			      cs35l45_dsp_rx_val),
	SOC_VALUE_ENUM_SINGLE(CS35L45_DSP1RX5_INPUT, 0, CS35L45_PCM_SRC_MASK,
			      ARRAY_SIZE(cs35l45_dsp_rx_txt), cs35l45_dsp_rx_txt,
			      cs35l45_dsp_rx_val),
	SOC_VALUE_ENUM_SINGLE(CS35L45_DSP1RX6_INPUT, 0, CS35L45_PCM_SRC_MASK,
			      ARRAY_SIZE(cs35l45_dsp_rx_txt), cs35l45_dsp_rx_txt,
			      cs35l45_dsp_rx_val),
	SOC_VALUE_ENUM_SINGLE(CS35L45_DSP1RX7_INPUT, 0, CS35L45_PCM_SRC_MASK,
			      ARRAY_SIZE(cs35l45_dsp_rx_txt), cs35l45_dsp_rx_txt,
			      cs35l45_dsp_rx_val),
	SOC_VALUE_ENUM_SINGLE(CS35L45_DSP1RX8_INPUT, 0, CS35L45_PCM_SRC_MASK,
			      ARRAY_SIZE(cs35l45_dsp_rx_txt), cs35l45_dsp_rx_txt,
			      cs35l45_dsp_rx_val),
};

static const char * const cs35l45_dac_txt[] = {
	"Zero", "ASP_RX1", "ASP_RX2", "DSP_TX1", "DSP_TX2"
};

static const unsigned int cs35l45_dac_val[] = {
	CS35L45_PCM_SRC_ZERO, CS35L45_PCM_SRC_ASP_RX1, CS35L45_PCM_SRC_ASP_RX2,
	CS35L45_PCM_SRC_DSP_TX1, CS35L45_PCM_SRC_DSP_TX2
};

static const struct soc_enum cs35l45_dacpcm_enums[] = {
	SOC_VALUE_ENUM_SINGLE(CS35L45_DACPCM1_INPUT, 0, CS35L45_PCM_SRC_MASK,
			      ARRAY_SIZE(cs35l45_dac_txt), cs35l45_dac_txt,
			      cs35l45_dac_val),
};

static const struct snd_kcontrol_new cs35l45_asp_muxes[] = {
	SOC_DAPM_ENUM("ASP_TX1 Source", cs35l45_asp_tx_enums[0]),
	SOC_DAPM_ENUM("ASP_TX2 Source", cs35l45_asp_tx_enums[1]),
	SOC_DAPM_ENUM("ASP_TX3 Source", cs35l45_asp_tx_enums[2]),
	SOC_DAPM_ENUM("ASP_TX4 Source", cs35l45_asp_tx_enums[3]),
	SOC_DAPM_ENUM("ASP_TX5 Source", cs35l45_asp_tx_enums[4]),
};

static const struct snd_kcontrol_new cs35l45_dsp_muxes[] = {
	SOC_DAPM_ENUM("DSP_RX1 Source", cs35l45_dsp_rx_enums[0]),
	SOC_DAPM_ENUM("DSP_RX2 Source", cs35l45_dsp_rx_enums[1]),
	SOC_DAPM_ENUM("DSP_RX3 Source", cs35l45_dsp_rx_enums[2]),
	SOC_DAPM_ENUM("DSP_RX4 Source", cs35l45_dsp_rx_enums[3]),
	SOC_DAPM_ENUM("DSP_RX5 Source", cs35l45_dsp_rx_enums[4]),
	SOC_DAPM_ENUM("DSP_RX6 Source", cs35l45_dsp_rx_enums[5]),
	SOC_DAPM_ENUM("DSP_RX7 Source", cs35l45_dsp_rx_enums[6]),
	SOC_DAPM_ENUM("DSP_RX8 Source", cs35l45_dsp_rx_enums[7]),
};

static const struct snd_kcontrol_new cs35l45_dac_muxes[] = {
	SOC_DAPM_ENUM("DACPCM Source", cs35l45_dacpcm_enums[0]),
};
static const struct snd_kcontrol_new amp_en_ctl =
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0);

static const struct snd_soc_dapm_widget cs35l45_dapm_widgets[] = {
	SND_SOC_DAPM_SPK("DSP1 Preload", NULL),
	SND_SOC_DAPM_SUPPLY_S("DSP1 Preloader", 100, SND_SOC_NOPM, 0, 0,
				cs35l45_dsp_preload_ev,
				SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_OUT_DRV_E("DSP1", SND_SOC_NOPM, 0, 0, NULL, 0,
				cs35l45_dsp_audio_ev,
				SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_SUPPLY("GLOBAL_EN", SND_SOC_NOPM, 0, 0,
			    cs35l45_global_en_ev,
			    SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_SUPPLY("ASP_EN", CS35L45_BLOCK_ENABLES2, CS35L45_ASP_EN_SHIFT, 0, NULL, 0),

	SND_SOC_DAPM_SIGGEN("VMON_SRC"),
	SND_SOC_DAPM_SIGGEN("IMON_SRC"),
	SND_SOC_DAPM_SIGGEN("TEMPMON_SRC"),
	SND_SOC_DAPM_SIGGEN("VDD_BATTMON_SRC"),
	SND_SOC_DAPM_SIGGEN("VDD_BSTMON_SRC"),
	SND_SOC_DAPM_SIGGEN("ERR_VOL"),
	SND_SOC_DAPM_SIGGEN("AMP_INTP"),
	SND_SOC_DAPM_SIGGEN("IL_TARGET"),

	SND_SOC_DAPM_SUPPLY("VMON_EN", CS35L45_BLOCK_ENABLES, CS35L45_VMON_EN_SHIFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("IMON_EN", CS35L45_BLOCK_ENABLES, CS35L45_IMON_EN_SHIFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("TEMPMON_EN", CS35L45_BLOCK_ENABLES, CS35L45_TEMPMON_EN_SHIFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("VDD_BATTMON_EN", CS35L45_BLOCK_ENABLES, CS35L45_VDD_BATTMON_EN_SHIFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("VDD_BSTMON_EN", CS35L45_BLOCK_ENABLES, CS35L45_VDD_BSTMON_EN_SHIFT, 0, NULL, 0),

	SND_SOC_DAPM_ADC("VMON", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_ADC("IMON", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_ADC("TEMPMON", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_ADC("VDD_BATTMON", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_ADC("VDD_BSTMON", NULL, SND_SOC_NOPM, 0, 0),


	SND_SOC_DAPM_AIF_IN("ASP_RX1", NULL, 0, CS35L45_ASP_ENABLES1, CS35L45_ASP_RX1_EN_SHIFT, 0),
	SND_SOC_DAPM_AIF_IN("ASP_RX2", NULL, 1, CS35L45_ASP_ENABLES1, CS35L45_ASP_RX2_EN_SHIFT, 0),

	SND_SOC_DAPM_AIF_OUT("ASP_TX1", NULL, 0, CS35L45_ASP_ENABLES1, CS35L45_ASP_TX1_EN_SHIFT, 0),
	SND_SOC_DAPM_AIF_OUT("ASP_TX2", NULL, 1, CS35L45_ASP_ENABLES1, CS35L45_ASP_TX2_EN_SHIFT, 0),
	SND_SOC_DAPM_AIF_OUT("ASP_TX3", NULL, 2, CS35L45_ASP_ENABLES1, CS35L45_ASP_TX3_EN_SHIFT, 0),
	SND_SOC_DAPM_AIF_OUT("ASP_TX4", NULL, 3, CS35L45_ASP_ENABLES1, CS35L45_ASP_TX4_EN_SHIFT, 0),
	SND_SOC_DAPM_AIF_OUT("ASP_TX5", NULL, 3, CS35L45_ASP_ENABLES1, CS35L45_ASP_TX5_EN_SHIFT, 0),

	SND_SOC_DAPM_MUX("ASP_TX1 Source", SND_SOC_NOPM, 0, 0, &cs35l45_asp_muxes[0]),
	SND_SOC_DAPM_MUX("ASP_TX2 Source", SND_SOC_NOPM, 0, 0, &cs35l45_asp_muxes[1]),
	SND_SOC_DAPM_MUX("ASP_TX3 Source", SND_SOC_NOPM, 0, 0, &cs35l45_asp_muxes[2]),
	SND_SOC_DAPM_MUX("ASP_TX4 Source", SND_SOC_NOPM, 0, 0, &cs35l45_asp_muxes[3]),
	SND_SOC_DAPM_MUX("ASP_TX5 Source", SND_SOC_NOPM, 0, 0, &cs35l45_asp_muxes[4]),

	SND_SOC_DAPM_MUX("DSP_RX1 Source", SND_SOC_NOPM, 0, 0, &cs35l45_dsp_muxes[0]),
	SND_SOC_DAPM_MUX("DSP_RX2 Source", SND_SOC_NOPM, 0, 0, &cs35l45_dsp_muxes[1]),
	SND_SOC_DAPM_MUX("DSP_RX3 Source", SND_SOC_NOPM, 0, 0, &cs35l45_dsp_muxes[2]),
	SND_SOC_DAPM_MUX("DSP_RX4 Source", SND_SOC_NOPM, 0, 0, &cs35l45_dsp_muxes[3]),
	SND_SOC_DAPM_MUX("DSP_RX5 Source", SND_SOC_NOPM, 0, 0, &cs35l45_dsp_muxes[4]),
	SND_SOC_DAPM_MUX("DSP_RX6 Source", SND_SOC_NOPM, 0, 0, &cs35l45_dsp_muxes[5]),
	SND_SOC_DAPM_MUX("DSP_RX7 Source", SND_SOC_NOPM, 0, 0, &cs35l45_dsp_muxes[6]),
	SND_SOC_DAPM_MUX("DSP_RX8 Source", SND_SOC_NOPM, 0, 0, &cs35l45_dsp_muxes[7]),

	SND_SOC_DAPM_MUX("DACPCM Source", SND_SOC_NOPM, 0, 0, &cs35l45_dac_muxes[0]),

	SND_SOC_DAPM_SWITCH("AMP Enable", SND_SOC_NOPM, 0, 0, &amp_en_ctl),

	SND_SOC_DAPM_OUT_DRV("AMP", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_OUTPUT("SPK"),
};

#define CS35L45_ASP_MUX_ROUTE(name) \
	{ name" Source", "ASP_RX1",	 "ASP_RX1" }, \
	{ name" Source", "ASP_RX2",	 "ASP_RX2" }, \
	{ name" Source", "DSP_TX1",	 "DSP1" }, \
	{ name" Source", "DSP_TX2",	 "DSP1" }, \
	{ name" Source", "VMON",	 "VMON" }, \
	{ name" Source", "IMON",	 "IMON" }, \
	{ name" Source", "ERR_VOL",	 "ERR_VOL" }, \
	{ name" Source", "VDD_BATTMON",	 "VDD_BATTMON" }, \
	{ name" Source", "VDD_BSTMON",	 "VDD_BSTMON" }, \
	{ name" Source", "Interpolator", "AMP_INTP" }, \
	{ name" Source", "IL_TARGET",	 "IL_TARGET" }

#define CS35L45_DSP_MUX_ROUTE(name) \
	{ name" Source", "ASP_RX1",	"ASP_RX1" }, \
	{ name" Source", "ASP_RX2",	"ASP_RX2" }

#define CS35L45_DAC_MUX_ROUTE(name) \
	{ name" Source", "ASP_RX1",	"ASP_RX1" }, \
	{ name" Source", "ASP_RX2",	"ASP_RX2" }, \
	{ name" Source", "DSP_TX1",	"DSP1" }, \
	{ name" Source", "DSP_TX2",	"DSP1" }

static const struct snd_soc_dapm_route cs35l45_dapm_routes[] = {
	/* Feedback */
	{ "VMON", NULL, "VMON_SRC" },
	{ "IMON", NULL, "IMON_SRC" },
	{ "TEMPMON", NULL, "TEMPMON_SRC" },
	{ "VDD_BATTMON", NULL, "VDD_BATTMON_SRC" },
	{ "VDD_BSTMON", NULL, "VDD_BSTMON_SRC" },

	{ "VMON", NULL, "VMON_EN" },
	{ "IMON", NULL, "IMON_EN" },
	{ "TEMPMON", NULL, "TEMPMON_EN" },
	{ "VDD_BATTMON", NULL, "VDD_BATTMON_EN" },
	{ "VDD_BSTMON", NULL, "VDD_BSTMON_EN" },

	{ "Capture", NULL, "ASP_TX1"},
	{ "Capture", NULL, "ASP_TX2"},
	{ "Capture", NULL, "ASP_TX3"},
	{ "Capture", NULL, "ASP_TX4"},
	{ "Capture", NULL, "ASP_TX5"},
	{ "ASP_TX1", NULL, "ASP_TX1 Source"},
	{ "ASP_TX2", NULL, "ASP_TX2 Source"},
	{ "ASP_TX3", NULL, "ASP_TX3 Source"},
	{ "ASP_TX4", NULL, "ASP_TX4 Source"},
	{ "ASP_TX5", NULL, "ASP_TX5 Source"},

	{ "ASP_TX1", NULL, "ASP_EN" },
	{ "ASP_TX2", NULL, "ASP_EN" },
	{ "ASP_TX3", NULL, "ASP_EN" },
	{ "ASP_TX4", NULL, "ASP_EN" },
	{ "ASP_TX1", NULL, "GLOBAL_EN" },
	{ "ASP_TX2", NULL, "GLOBAL_EN" },
	{ "ASP_TX3", NULL, "GLOBAL_EN" },
	{ "ASP_TX4", NULL, "GLOBAL_EN" },
	{ "ASP_TX5", NULL, "GLOBAL_EN" },

	CS35L45_ASP_MUX_ROUTE("ASP_TX1"),
	CS35L45_ASP_MUX_ROUTE("ASP_TX2"),
	CS35L45_ASP_MUX_ROUTE("ASP_TX3"),
	CS35L45_ASP_MUX_ROUTE("ASP_TX4"),
	CS35L45_ASP_MUX_ROUTE("ASP_TX5"),

	/* Playback */
	{ "ASP_RX1", NULL, "Playback" },
	{ "ASP_RX2", NULL, "Playback" },
	{ "ASP_RX1", NULL, "ASP_EN" },
	{ "ASP_RX2", NULL, "ASP_EN" },

	{ "AMP", NULL, "DACPCM Source"},
	{ "AMP", NULL, "GLOBAL_EN"},

	CS35L45_DSP_MUX_ROUTE("DSP_RX1"),
	CS35L45_DSP_MUX_ROUTE("DSP_RX2"),
	CS35L45_DSP_MUX_ROUTE("DSP_RX3"),
	CS35L45_DSP_MUX_ROUTE("DSP_RX4"),
	CS35L45_DSP_MUX_ROUTE("DSP_RX5"),
	CS35L45_DSP_MUX_ROUTE("DSP_RX6"),
	CS35L45_DSP_MUX_ROUTE("DSP_RX7"),
	CS35L45_DSP_MUX_ROUTE("DSP_RX8"),

	{"DSP1", NULL, "DSP_RX1 Source"},
	{"DSP1", NULL, "DSP_RX2 Source"},
	{"DSP1", NULL, "DSP_RX3 Source"},
	{"DSP1", NULL, "DSP_RX4 Source"},
	{"DSP1", NULL, "DSP_RX5 Source"},
	{"DSP1", NULL, "DSP_RX6 Source"},
	{"DSP1", NULL, "DSP_RX7 Source"},
	{"DSP1", NULL, "DSP_RX8 Source"},

	{"DSP1", NULL, "VMON_EN"},
	{"DSP1", NULL, "IMON_EN"},
	{"DSP1", NULL, "VDD_BATTMON_EN"},
	{"DSP1", NULL, "VDD_BSTMON_EN"},
	{"DSP1", NULL, "TEMPMON_EN"},

	{"DSP1 Preload", NULL, "DSP1 Preloader"},
	{"DSP1", NULL, "DSP1 Preloader"},

	CS35L45_DAC_MUX_ROUTE("DACPCM"),

	{ "AMP Enable", "Switch", "AMP" },
	{ "SPK", NULL, "AMP Enable"},
};

static const char * const amplifier_mode_texts[] = {"SPK", "RCV"};
static SOC_ENUM_SINGLE_DECL(amplifier_mode_enum, SND_SOC_NOPM, 0,
			    amplifier_mode_texts);
static DECLARE_TLV_DB_SCALE(amp_gain_tlv, 1000, 300, 0);
static const DECLARE_TLV_DB_SCALE(cs35l45_dig_pcm_vol_tlv, -10225, 25, true);

static const struct snd_kcontrol_new cs35l45_controls[] = {
	SOC_ENUM_EXT("Amplifier Mode", amplifier_mode_enum,
		     cs35l45_amplifier_mode_get, cs35l45_amplifier_mode_put),
	SOC_SINGLE_TLV("Analog PCM Volume", CS35L45_AMP_GAIN,
			CS35L45_AMP_GAIN_PCM_SHIFT,
			CS35L45_AMP_GAIN_PCM_MASK >> CS35L45_AMP_GAIN_PCM_SHIFT,
			0, amp_gain_tlv),
	/* Ignore bit 0: it is beyond the resolution of TLV_DB_SCALE */
	SOC_SINGLE_S_TLV("Digital PCM Volume",
			 CS35L45_AMP_PCM_CONTROL,
			 CS35L45_AMP_VOL_PCM_SHIFT + 1,
			 -409, 48,
			 (CS35L45_AMP_VOL_PCM_WIDTH - 1) - 1,
			 0, cs35l45_dig_pcm_vol_tlv),
	WM_ADSP2_PRELOAD_SWITCH("DSP1", 1),
	WM_ADSP_FW_CONTROL("DSP1", 0),
};

static int cs35l45_set_pll(struct cs35l45_private *cs35l45, unsigned int freq)
{
	unsigned int val;
	int freq_id;

	freq_id = cs35l45_get_clk_freq_id(freq);
	if (freq_id < 0) {
		dev_err(cs35l45->dev, "Invalid freq: %u\n", freq);
		return -EINVAL;
	}

	regmap_read(cs35l45->regmap, CS35L45_REFCLK_INPUT, &val);
	val = (val & CS35L45_PLL_REFCLK_FREQ_MASK) >> CS35L45_PLL_REFCLK_FREQ_SHIFT;
	if (val == freq_id)
		return 0;

	regmap_set_bits(cs35l45->regmap, CS35L45_REFCLK_INPUT, CS35L45_PLL_OPEN_LOOP_MASK);
	regmap_update_bits(cs35l45->regmap, CS35L45_REFCLK_INPUT,
			   CS35L45_PLL_REFCLK_FREQ_MASK,
			   freq_id << CS35L45_PLL_REFCLK_FREQ_SHIFT);
	regmap_clear_bits(cs35l45->regmap, CS35L45_REFCLK_INPUT, CS35L45_PLL_REFCLK_EN_MASK);
	regmap_clear_bits(cs35l45->regmap, CS35L45_REFCLK_INPUT, CS35L45_PLL_OPEN_LOOP_MASK);
	regmap_set_bits(cs35l45->regmap, CS35L45_REFCLK_INPUT, CS35L45_PLL_REFCLK_EN_MASK);

	return 0;
}

static int cs35l45_asp_set_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt)
{
	struct cs35l45_private *cs35l45 = snd_soc_component_get_drvdata(codec_dai->component);
	unsigned int asp_fmt, fsync_inv, bclk_inv;

	switch (fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_CBC_CFC:
		break;
	default:
		dev_err(cs35l45->dev, "Invalid DAI clocking\n");
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
		asp_fmt = CS35l45_ASP_FMT_DSP_A;
		break;
	case SND_SOC_DAIFMT_I2S:
		asp_fmt = CS35L45_ASP_FMT_I2S;
		break;
	default:
		dev_err(cs35l45->dev, "Invalid DAI format\n");
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_IF:
		fsync_inv = 1;
		bclk_inv = 0;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		fsync_inv = 0;
		bclk_inv = 1;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		fsync_inv = 1;
		bclk_inv = 1;
		break;
	case SND_SOC_DAIFMT_NB_NF:
		fsync_inv = 0;
		bclk_inv = 0;
		break;
	default:
		dev_warn(cs35l45->dev, "Invalid DAI clock polarity\n");
		return -EINVAL;
	}

	regmap_update_bits(cs35l45->regmap, CS35L45_ASP_CONTROL2,
			   CS35L45_ASP_FMT_MASK |
			   CS35L45_ASP_FSYNC_INV_MASK |
			   CS35L45_ASP_BCLK_INV_MASK,
			   (asp_fmt << CS35L45_ASP_FMT_SHIFT) |
			   (fsync_inv << CS35L45_ASP_FSYNC_INV_SHIFT) |
			   (bclk_inv << CS35L45_ASP_BCLK_INV_SHIFT));

	return 0;
}

static int cs35l45_asp_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct cs35l45_private *cs35l45 = snd_soc_component_get_drvdata(dai->component);
	unsigned int asp_width, asp_wl, global_fs, slot_multiple, asp_fmt;
	int bclk;

	switch (params_rate(params)) {
	case 44100:
		global_fs = CS35L45_44P100_KHZ;
		break;
	case 48000:
		global_fs = CS35L45_48P0_KHZ;
		break;
	case 88200:
		global_fs = CS35L45_88P200_KHZ;
		break;
	case 96000:
		global_fs = CS35L45_96P0_KHZ;
		break;
	default:
		dev_warn(cs35l45->dev, "Unsupported sample rate (%d)\n",
			 params_rate(params));
		return -EINVAL;
	}

	regmap_update_bits(cs35l45->regmap, CS35L45_GLOBAL_SAMPLE_RATE,
			   CS35L45_GLOBAL_FS_MASK,
			   global_fs << CS35L45_GLOBAL_FS_SHIFT);

	asp_wl = params_width(params);

	if (cs35l45->slot_width)
		asp_width = cs35l45->slot_width;
	else
		asp_width = params_width(params);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		regmap_update_bits(cs35l45->regmap, CS35L45_ASP_CONTROL2,
				   CS35L45_ASP_WIDTH_RX_MASK,
				   asp_width << CS35L45_ASP_WIDTH_RX_SHIFT);

		regmap_update_bits(cs35l45->regmap, CS35L45_ASP_DATA_CONTROL5,
				   CS35L45_ASP_WL_MASK,
				   asp_wl << CS35L45_ASP_WL_SHIFT);
	} else {
		regmap_update_bits(cs35l45->regmap, CS35L45_ASP_CONTROL2,
				   CS35L45_ASP_WIDTH_TX_MASK,
				   asp_width << CS35L45_ASP_WIDTH_TX_SHIFT);

		regmap_update_bits(cs35l45->regmap, CS35L45_ASP_DATA_CONTROL1,
				   CS35L45_ASP_WL_MASK,
				   asp_wl << CS35L45_ASP_WL_SHIFT);
	}

	if (cs35l45->sysclk_set)
		return 0;

	/* I2S always has an even number of channels */
	regmap_read(cs35l45->regmap, CS35L45_ASP_CONTROL2, &asp_fmt);
	asp_fmt = (asp_fmt & CS35L45_ASP_FMT_MASK) >> CS35L45_ASP_FMT_SHIFT;
	if (asp_fmt == CS35L45_ASP_FMT_I2S)
		slot_multiple = 2;
	else
		slot_multiple = 1;

	bclk = snd_soc_tdm_params_to_bclk(params, asp_width,
					  cs35l45->slot_count, slot_multiple);

	return cs35l45_set_pll(cs35l45, bclk);
}

static int cs35l45_asp_set_tdm_slot(struct snd_soc_dai *dai,
				    unsigned int tx_mask, unsigned int rx_mask,
				    int slots, int slot_width)
{
	struct cs35l45_private *cs35l45 = snd_soc_component_get_drvdata(dai->component);

	if (slot_width && ((slot_width < 16) || (slot_width > 128)))
		return -EINVAL;

	cs35l45->slot_width = slot_width;
	cs35l45->slot_count = slots;

	return 0;
}

static int cs35l45_asp_set_sysclk(struct snd_soc_dai *dai,
				  int clk_id, unsigned int freq, int dir)
{
	struct cs35l45_private *cs35l45 = snd_soc_component_get_drvdata(dai->component);
	int ret;

	if (clk_id != 0) {
		dev_err(cs35l45->dev, "Invalid clk_id %d\n", clk_id);
		return -EINVAL;
	}

	cs35l45->sysclk_set = false;
	if (freq == 0)
		return 0;

	ret = cs35l45_set_pll(cs35l45, freq);
	if (ret < 0)
		return -EINVAL;

	cs35l45->sysclk_set = true;

	return 0;
}

static int cs35l45_mute_stream(struct snd_soc_dai *dai, int mute, int stream)
{
	struct cs35l45_private *cs35l45 = snd_soc_component_get_drvdata(dai->component);
	unsigned int global_fs, val, hpf_tune;

	if (mute)
		return 0;

	regmap_read(cs35l45->regmap, CS35L45_GLOBAL_SAMPLE_RATE, &global_fs);
	global_fs = (global_fs & CS35L45_GLOBAL_FS_MASK) >> CS35L45_GLOBAL_FS_SHIFT;
	switch (global_fs) {
	case CS35L45_44P100_KHZ:
		hpf_tune = CS35L45_HPF_44P1;
		break;
	case CS35L45_88P200_KHZ:
		hpf_tune = CS35L45_HPF_88P2;
		break;
	default:
		hpf_tune = CS35l45_HPF_DEFAULT;
		break;
	}

	regmap_read(cs35l45->regmap, CS35L45_AMP_PCM_HPF_TST, &val);
	if (val != hpf_tune) {
		struct reg_sequence hpf_override_seq[] = {
			{ 0x00000040,			0x00000055 },
			{ 0x00000040,			0x000000AA },
			{ 0x00000044,			0x00000055 },
			{ 0x00000044,			0x000000AA },
			{ CS35L45_AMP_PCM_HPF_TST,	hpf_tune },
			{ 0x00000040,			0x00000000 },
			{ 0x00000044,			0x00000000 },
		};
		regmap_multi_reg_write(cs35l45->regmap, hpf_override_seq,
				       ARRAY_SIZE(hpf_override_seq));
	}

	return 0;
}

static const struct snd_soc_dai_ops cs35l45_asp_dai_ops = {
	.set_fmt = cs35l45_asp_set_fmt,
	.hw_params = cs35l45_asp_hw_params,
	.set_tdm_slot = cs35l45_asp_set_tdm_slot,
	.set_sysclk = cs35l45_asp_set_sysclk,
	.mute_stream = cs35l45_mute_stream,
};

static struct snd_soc_dai_driver cs35l45_dai[] = {
	{
		.name = "cs35l45",
		.playback = {
			.stream_name = "Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = CS35L45_RATES,
			.formats = CS35L45_FORMATS,
		},
		.capture = {
			.stream_name = "Capture",
			.channels_min = 1,
			.channels_max = 5,
			.rates = CS35L45_RATES,
			.formats = CS35L45_FORMATS,
		},
		.symmetric_rate = true,
		.symmetric_sample_bits = true,
		.ops = &cs35l45_asp_dai_ops,
	},
};

static int cs35l45_component_probe(struct snd_soc_component *component)
{
	struct cs35l45_private *cs35l45 = snd_soc_component_get_drvdata(component);

	return wm_adsp2_component_probe(&cs35l45->dsp, component);
}

static void cs35l45_component_remove(struct snd_soc_component *component)
{
	struct cs35l45_private *cs35l45 = snd_soc_component_get_drvdata(component);

	wm_adsp2_component_remove(&cs35l45->dsp, component);
}

static const struct snd_soc_component_driver cs35l45_component = {
	.probe = cs35l45_component_probe,
	.remove = cs35l45_component_remove,

	.dapm_widgets = cs35l45_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(cs35l45_dapm_widgets),

	.dapm_routes = cs35l45_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(cs35l45_dapm_routes),

	.controls = cs35l45_controls,
	.num_controls = ARRAY_SIZE(cs35l45_controls),

	.name = "cs35l45",

	.endianness = 1,
};

static void cs35l45_setup_hibernate(struct cs35l45_private *cs35l45)
{
	unsigned int wksrc;

	if (cs35l45->bus_type == CONTROL_BUS_I2C)
		wksrc = CS35L45_WKSRC_I2C;
	else
		wksrc = CS35L45_WKSRC_SPI;

	regmap_update_bits(cs35l45->regmap, CS35L45_WAKESRC_CTL,
			   CS35L45_WKSRC_EN_MASK,
			   wksrc << CS35L45_WKSRC_EN_SHIFT);

	regmap_set_bits(cs35l45->regmap, CS35L45_WAKESRC_CTL,
			   CS35L45_UPDT_WKCTL_MASK);

	regmap_update_bits(cs35l45->regmap, CS35L45_WKI2C_CTL,
			   CS35L45_WKI2C_ADDR_MASK, cs35l45->i2c_addr);

	regmap_set_bits(cs35l45->regmap, CS35L45_WKI2C_CTL,
			   CS35L45_UPDT_WKI2C_MASK);
}

static int cs35l45_enter_hibernate(struct cs35l45_private *cs35l45)
{
	dev_dbg(cs35l45->dev, "Enter hibernate\n");

	cs35l45_setup_hibernate(cs35l45);

	regmap_set_bits(cs35l45->regmap, CS35L45_IRQ1_MASK_2, CS35L45_DSP_VIRT2_MBOX_MASK);

	// Don't wait for ACK since bus activity would wake the device
	regmap_write(cs35l45->regmap, CS35L45_DSP_VIRT1_MBOX_1, CSPL_MBOX_CMD_HIBERNATE);

	return 0;
}

static int cs35l45_exit_hibernate(struct cs35l45_private *cs35l45)
{
	const int wake_retries = 20;
	const int sleep_retries = 5;
	int ret, i, j;

	for (i = 0; i < sleep_retries; i++) {
		dev_dbg(cs35l45->dev, "Exit hibernate\n");

		for (j = 0; j < wake_retries; j++) {
			ret = cs35l45_set_cspl_mbox_cmd(cs35l45, cs35l45->regmap,
					  CSPL_MBOX_CMD_OUT_OF_HIBERNATE);
			if (!ret) {
				dev_dbg(cs35l45->dev, "Wake success at cycle: %d\n", j);
				regmap_clear_bits(cs35l45->regmap, CS35L45_IRQ1_MASK_2,
						 CS35L45_DSP_VIRT2_MBOX_MASK);
				return 0;
			}
			usleep_range(100, 200);
		}

		dev_err(cs35l45->dev, "Wake failed, re-enter hibernate: %d\n", ret);

		cs35l45_setup_hibernate(cs35l45);
	}

	dev_err(cs35l45->dev, "Timed out waking device\n");

	return -ETIMEDOUT;
}

static int cs35l45_runtime_suspend(struct device *dev)
{
	struct cs35l45_private *cs35l45 = dev_get_drvdata(dev);

	if (!cs35l45->dsp.preloaded || !cs35l45->dsp.cs_dsp.running)
		return 0;

	cs35l45_enter_hibernate(cs35l45);

	regcache_cache_only(cs35l45->regmap, true);
	regcache_mark_dirty(cs35l45->regmap);

	dev_dbg(cs35l45->dev, "Runtime suspended\n");

	return 0;
}

static int cs35l45_runtime_resume(struct device *dev)
{
	struct cs35l45_private *cs35l45 = dev_get_drvdata(dev);
	int ret;

	if (!cs35l45->dsp.preloaded || !cs35l45->dsp.cs_dsp.running)
		return 0;

	dev_dbg(cs35l45->dev, "Runtime resume\n");

	regcache_cache_only(cs35l45->regmap, false);

	ret = cs35l45_exit_hibernate(cs35l45);
	if (ret)
		return ret;

	ret = regcache_sync(cs35l45->regmap);
	if (ret != 0)
		dev_warn(cs35l45->dev, "regcache_sync failed: %d\n", ret);

	/* Clear global error status */
	regmap_clear_bits(cs35l45->regmap, CS35L45_ERROR_RELEASE, CS35L45_GLOBAL_ERR_RLS_MASK);
	regmap_set_bits(cs35l45->regmap, CS35L45_ERROR_RELEASE, CS35L45_GLOBAL_ERR_RLS_MASK);
	regmap_clear_bits(cs35l45->regmap, CS35L45_ERROR_RELEASE, CS35L45_GLOBAL_ERR_RLS_MASK);
	return ret;
}

static int cs35l45_sys_suspend(struct device *dev)
{
	struct cs35l45_private *cs35l45 = dev_get_drvdata(dev);

	dev_dbg(cs35l45->dev, "System suspend, disabling IRQ\n");
	disable_irq(cs35l45->irq);

	return 0;
}

static int cs35l45_sys_suspend_noirq(struct device *dev)
{
	struct cs35l45_private *cs35l45 = dev_get_drvdata(dev);

	dev_dbg(cs35l45->dev, "Late system suspend, reenabling IRQ\n");
	enable_irq(cs35l45->irq);

	return 0;
}

static int cs35l45_sys_resume_noirq(struct device *dev)
{
	struct cs35l45_private *cs35l45 = dev_get_drvdata(dev);

	dev_dbg(cs35l45->dev, "Early system resume, disabling IRQ\n");
	disable_irq(cs35l45->irq);

	return 0;
}

static int cs35l45_sys_resume(struct device *dev)
{
	struct cs35l45_private *cs35l45 = dev_get_drvdata(dev);

	dev_dbg(cs35l45->dev, "System resume, reenabling IRQ\n");
	enable_irq(cs35l45->irq);

	return 0;
}

static int cs35l45_apply_property_config(struct cs35l45_private *cs35l45)
{
	struct device_node *node = cs35l45->dev->of_node;
	unsigned int gpio_regs[] = {CS35L45_GPIO1_CTRL1, CS35L45_GPIO2_CTRL1,
				    CS35L45_GPIO3_CTRL1};
	unsigned int pad_regs[] = {CS35L45_SYNC_GPIO1,
				   CS35L45_INTB_GPIO2_MCLK_REF, CS35L45_GPIO3};
	struct device_node *child;
	unsigned int val;
	char of_name[32];
	int ret, i;

	if (!node)
		return 0;

	for (i = 0; i < CS35L45_NUM_GPIOS; i++) {
		sprintf(of_name, "cirrus,gpio-ctrl%d", i + 1);
		child = of_get_child_by_name(node, of_name);
		if (!child)
			continue;

		ret = of_property_read_u32(child, "gpio-dir", &val);
		if (!ret)
			regmap_update_bits(cs35l45->regmap, gpio_regs[i],
					   CS35L45_GPIO_DIR_MASK,
					   val << CS35L45_GPIO_DIR_SHIFT);

		ret = of_property_read_u32(child, "gpio-lvl", &val);
		if (!ret)
			regmap_update_bits(cs35l45->regmap, gpio_regs[i],
					   CS35L45_GPIO_LVL_MASK,
					   val << CS35L45_GPIO_LVL_SHIFT);

		ret = of_property_read_u32(child, "gpio-op-cfg", &val);
		if (!ret)
			regmap_update_bits(cs35l45->regmap, gpio_regs[i],
					   CS35L45_GPIO_OP_CFG_MASK,
					   val << CS35L45_GPIO_OP_CFG_SHIFT);

		ret = of_property_read_u32(child, "gpio-pol", &val);
		if (!ret)
			regmap_update_bits(cs35l45->regmap, gpio_regs[i],
					   CS35L45_GPIO_POL_MASK,
					   val << CS35L45_GPIO_POL_SHIFT);

		ret = of_property_read_u32(child, "gpio-ctrl", &val);
		if (!ret)
			regmap_update_bits(cs35l45->regmap, pad_regs[i],
					   CS35L45_GPIO_CTRL_MASK,
					   val << CS35L45_GPIO_CTRL_SHIFT);

		ret = of_property_read_u32(child, "gpio-invert", &val);
		if (!ret) {
			regmap_update_bits(cs35l45->regmap, pad_regs[i],
					   CS35L45_GPIO_INVERT_MASK,
					   val << CS35L45_GPIO_INVERT_SHIFT);
			if (i == 1)
				cs35l45->irq_invert = val;
		}

		of_node_put(child);
	}

	if (device_property_read_u32(cs35l45->dev,
				     "cirrus,asp-sdout-hiz-ctrl", &val) == 0) {
		regmap_update_bits(cs35l45->regmap, CS35L45_ASP_CONTROL3,
				   CS35L45_ASP_DOUT_HIZ_CTRL_MASK,
				   val << CS35L45_ASP_DOUT_HIZ_CTRL_SHIFT);
	}

	return 0;
}

static int cs35l45_dsp_virt2_mbox3_irq_handle(struct cs35l45_private *cs35l45,
					      const unsigned int cmd,
					      unsigned int data)
{
	static char *speak_status = "Unknown";

	switch (cmd) {
	case EVENT_SPEAKER_STATUS:
		switch (data) {
		case 1:
			speak_status = "All Clear";
			break;
		case 2:
			speak_status = "Open Circuit";
			break;
		case 4:
			speak_status = "Short Circuit";
			break;
		}

		dev_info(cs35l45->dev, "MBOX event (SPEAKER_STATUS): %s\n",
			 speak_status);
		break;
	case EVENT_BOOT_DONE:
		dev_dbg(cs35l45->dev, "MBOX event (BOOT_DONE)\n");
		break;
	default:
		dev_err(cs35l45->dev, "MBOX event not supported %u\n", cmd);
		return -EINVAL;
	}

	return 0;
}

static irqreturn_t cs35l45_dsp_virt2_mbox_cb(int irq, void *data)
{
	struct cs35l45_private *cs35l45 = data;
	unsigned int mbox_val;
	int ret = 0;

	ret = regmap_read(cs35l45->regmap, CS35L45_DSP_VIRT2_MBOX_3, &mbox_val);
	if (!ret && mbox_val)
		cs35l45_dsp_virt2_mbox3_irq_handle(cs35l45, mbox_val & CS35L45_MBOX3_CMD_MASK,
				(mbox_val & CS35L45_MBOX3_DATA_MASK) >> CS35L45_MBOX3_DATA_SHIFT);

	/* Handle DSP trace log IRQ */
	ret = regmap_read(cs35l45->regmap, CS35L45_DSP_VIRT2_MBOX_4, &mbox_val);
	if (!ret && mbox_val != 0) {
		dev_err(cs35l45->dev, "Spurious DSP MBOX4 IRQ\n");
	}

	return IRQ_RETVAL(ret);
}

static irqreturn_t cs35l45_pll_unlock(int irq, void *data)
{
	struct cs35l45_private *cs35l45 = data;

	dev_dbg(cs35l45->dev, "PLL unlock detected!");

	return IRQ_HANDLED;
}

static irqreturn_t cs35l45_pll_lock(int irq, void *data)
{
	struct cs35l45_private *cs35l45 = data;

	dev_dbg(cs35l45->dev, "PLL lock detected!");

	return IRQ_HANDLED;
}

static irqreturn_t cs35l45_spk_safe_err(int irq, void *data);

static const struct cs35l45_irq cs35l45_irqs[] = {
	CS35L45_IRQ(AMP_SHORT_ERR, "Amplifier short error", cs35l45_spk_safe_err),
	CS35L45_IRQ(UVLO_VDDBATT_ERR, "VDDBATT undervoltage error", cs35l45_spk_safe_err),
	CS35L45_IRQ(BST_SHORT_ERR, "Boost inductor error", cs35l45_spk_safe_err),
	CS35L45_IRQ(BST_UVP_ERR, "Boost undervoltage error", cs35l45_spk_safe_err),
	CS35L45_IRQ(TEMP_ERR, "Overtemperature error", cs35l45_spk_safe_err),
	CS35L45_IRQ(AMP_CAL_ERR, "Amplifier calibration error", cs35l45_spk_safe_err),
	CS35L45_IRQ(UVLO_VDDLV_ERR, "LV threshold detector error", cs35l45_spk_safe_err),
	CS35L45_IRQ(GLOBAL_ERROR, "Global error", cs35l45_spk_safe_err),
	CS35L45_IRQ(DSP_WDT_EXPIRE, "DSP Watchdog Timer", cs35l45_spk_safe_err),
	CS35L45_IRQ(PLL_UNLOCK_FLAG_RISE, "PLL unlock", cs35l45_pll_unlock),
	CS35L45_IRQ(PLL_LOCK_FLAG, "PLL lock", cs35l45_pll_lock),
	CS35L45_IRQ(DSP_VIRT2_MBOX, "DSP virtual MBOX 2 write flag", cs35l45_dsp_virt2_mbox_cb),
};

static irqreturn_t cs35l45_spk_safe_err(int irq, void *data)
{
	struct cs35l45_private *cs35l45 = data;
	int i;

	i = irq - regmap_irq_get_virq(cs35l45->irq_data, 0);

	if (i < 0 || i >= ARRAY_SIZE(cs35l45_irqs))
		dev_err(cs35l45->dev, "Unspecified global error condition (%d) detected!\n", irq);
	else
		dev_err(cs35l45->dev, "%s condition detected!\n", cs35l45_irqs[i].name);

	return IRQ_HANDLED;
}

static const struct regmap_irq cs35l45_reg_irqs[] = {
	CS35L45_REG_IRQ(IRQ1_EINT_1, AMP_SHORT_ERR),
	CS35L45_REG_IRQ(IRQ1_EINT_1, UVLO_VDDBATT_ERR),
	CS35L45_REG_IRQ(IRQ1_EINT_1, BST_SHORT_ERR),
	CS35L45_REG_IRQ(IRQ1_EINT_1, BST_UVP_ERR),
	CS35L45_REG_IRQ(IRQ1_EINT_1, TEMP_ERR),
	CS35L45_REG_IRQ(IRQ1_EINT_3, AMP_CAL_ERR),
	CS35L45_REG_IRQ(IRQ1_EINT_18, UVLO_VDDLV_ERR),
	CS35L45_REG_IRQ(IRQ1_EINT_18, GLOBAL_ERROR),
	CS35L45_REG_IRQ(IRQ1_EINT_2, DSP_WDT_EXPIRE),
	CS35L45_REG_IRQ(IRQ1_EINT_3, PLL_UNLOCK_FLAG_RISE),
	CS35L45_REG_IRQ(IRQ1_EINT_3, PLL_LOCK_FLAG),
	CS35L45_REG_IRQ(IRQ1_EINT_2, DSP_VIRT2_MBOX),
};

static const struct regmap_irq_chip cs35l45_regmap_irq_chip = {
	.name = "cs35l45 IRQ1 Controller",
	.main_status = CS35L45_IRQ1_STATUS,
	.status_base = CS35L45_IRQ1_EINT_1,
	.mask_base = CS35L45_IRQ1_MASK_1,
	.ack_base = CS35L45_IRQ1_EINT_1,
	.num_regs = 18,
	.irqs = cs35l45_reg_irqs,
	.num_irqs = ARRAY_SIZE(cs35l45_reg_irqs),
	.runtime_pm = true,
};

static int cs35l45_initialize(struct cs35l45_private *cs35l45)
{
	struct device *dev = cs35l45->dev;
	unsigned int dev_id[5];
	unsigned int sts;
	int ret;

	ret = regmap_read_poll_timeout(cs35l45->regmap, CS35L45_IRQ1_EINT_4, sts,
				       (sts & CS35L45_OTP_BOOT_DONE_STS_MASK),
				       1000, 5000);
	if (ret < 0) {
		dev_err(cs35l45->dev, "Timeout waiting for OTP boot\n");
		return ret;
	}

	ret = regmap_bulk_read(cs35l45->regmap, CS35L45_DEVID, dev_id, ARRAY_SIZE(dev_id));
	if (ret) {
		dev_err(cs35l45->dev, "Get Device ID failed: %d\n", ret);
		return ret;
	}

	switch (dev_id[0]) {
	case 0x35A450:
	case 0x35A460:
		break;
	default:
		dev_err(cs35l45->dev, "Bad DEVID 0x%x\n", dev_id[0]);
		return -ENODEV;
	}

	dev_info(cs35l45->dev, "Cirrus Logic CS35L45: REVID %02X OTPID %02X\n",
		 dev_id[1], dev_id[4]);

	regmap_write(cs35l45->regmap, CS35L45_IRQ1_EINT_4,
		     CS35L45_OTP_BOOT_DONE_STS_MASK | CS35L45_OTP_BUSY_MASK);

	ret = cs35l45_apply_patch(cs35l45);
	if (ret < 0) {
		dev_err(dev, "Failed to apply init patch %d\n", ret);
		return ret;
	}

	ret = cs35l45_apply_property_config(cs35l45);
	if (ret < 0)
		return ret;

	cs35l45->amplifier_mode = AMP_MODE_SPK;

	return 0;
}

static const struct reg_sequence cs35l45_fs_errata_patch[] = {
	{0x02B80080,			0x00000001},
	{0x02B80088,			0x00000001},
	{0x02B80090,			0x00000001},
	{0x02B80098,			0x00000001},
	{0x02B800A0,			0x00000001},
	{0x02B800A8,			0x00000001},
	{0x02B800B0,			0x00000001},
	{0x02B800B8,			0x00000001},
	{0x02B80280,			0x00000001},
	{0x02B80288,			0x00000001},
	{0x02B80290,			0x00000001},
	{0x02B80298,			0x00000001},
	{0x02B802A0,			0x00000001},
	{0x02B802A8,			0x00000001},
	{0x02B802B0,			0x00000001},
	{0x02B802B8,			0x00000001},
};

static const struct cs_dsp_region cs35l45_dsp1_regions[] = {
	{ .type = WMFW_HALO_PM_PACKED,	.base = CS35L45_DSP1_PMEM_0 },
	{ .type = WMFW_HALO_XM_PACKED,	.base = CS35L45_DSP1_XMEM_PACK_0 },
	{ .type = WMFW_HALO_YM_PACKED,	.base = CS35L45_DSP1_YMEM_PACK_0 },
	{. type = WMFW_ADSP2_XM,	.base = CS35L45_DSP1_XMEM_UNPACK24_0},
	{. type = WMFW_ADSP2_YM,	.base = CS35L45_DSP1_YMEM_UNPACK24_0},
};

static int cs35l45_dsp_init(struct cs35l45_private *cs35l45)
{
	struct wm_adsp *dsp = &cs35l45->dsp;
	int ret;

	dsp->part = "cs35l45";
	dsp->fw = 9; /* 9 is WM_ADSP_FW_SPK_PROT in wm_adsp.c */
	dsp->toggle_preload = true;
	dsp->cs_dsp.num = 1;
	dsp->cs_dsp.type = WMFW_HALO;
	dsp->cs_dsp.rev = 0;
	dsp->cs_dsp.dev = cs35l45->dev;
	dsp->cs_dsp.regmap = cs35l45->regmap;
	dsp->cs_dsp.base = CS35L45_DSP1_CLOCK_FREQ;
	dsp->cs_dsp.base_sysinfo = CS35L45_DSP1_SYS_ID;
	dsp->cs_dsp.mem = cs35l45_dsp1_regions;
	dsp->cs_dsp.num_mems = ARRAY_SIZE(cs35l45_dsp1_regions);
	dsp->cs_dsp.lock_regions = 0xFFFFFFFF;

	ret = wm_halo_init(dsp);

	regmap_multi_reg_write(cs35l45->regmap, cs35l45_fs_errata_patch,
						   ARRAY_SIZE(cs35l45_fs_errata_patch));

	return ret;
}

int cs35l45_probe(struct cs35l45_private *cs35l45)
{
	struct device *dev = cs35l45->dev;
	unsigned long irq_pol = IRQF_ONESHOT | IRQF_SHARED;
	int ret, i, irq;

	cs35l45->vdd_batt = devm_regulator_get(dev, "vdd-batt");
	if (IS_ERR(cs35l45->vdd_batt))
		return dev_err_probe(dev, PTR_ERR(cs35l45->vdd_batt),
				     "Failed to request vdd-batt\n");

	cs35l45->vdd_a = devm_regulator_get(dev, "vdd-a");
	if (IS_ERR(cs35l45->vdd_a))
		return dev_err_probe(dev, PTR_ERR(cs35l45->vdd_a),
				     "Failed to request vdd-a\n");

	/* VDD_BATT must always be enabled before other supplies */
	ret = regulator_enable(cs35l45->vdd_batt);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to enable vdd-batt\n");

	ret = regulator_enable(cs35l45->vdd_a);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to enable vdd-a\n");

	/* If reset is shared only one instance can claim it */
	cs35l45->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(cs35l45->reset_gpio)) {
		ret = PTR_ERR(cs35l45->reset_gpio);
		cs35l45->reset_gpio = NULL;
		if (ret == -EBUSY) {
			dev_dbg(dev, "Reset line busy, assuming shared reset\n");
		} else {
			dev_err_probe(dev, ret, "Failed to get reset GPIO\n");
			goto err;
		}
	}

	if (cs35l45->reset_gpio) {
		usleep_range(CS35L45_RESET_HOLD_US, CS35L45_RESET_HOLD_US + 100);
		gpiod_set_value_cansleep(cs35l45->reset_gpio, 1);
	}

	usleep_range(CS35L45_RESET_US, CS35L45_RESET_US + 100);

	ret = cs35l45_initialize(cs35l45);
	if (ret < 0)
		goto err_reset;

	ret = cs35l45_dsp_init(cs35l45);
	if (ret < 0)
		goto err_reset;

	pm_runtime_set_autosuspend_delay(cs35l45->dev, 3000);
	pm_runtime_use_autosuspend(cs35l45->dev);
	pm_runtime_mark_last_busy(cs35l45->dev);
	pm_runtime_set_active(cs35l45->dev);
	pm_runtime_get_noresume(cs35l45->dev);
	pm_runtime_enable(cs35l45->dev);

	if (cs35l45->irq) {
		if (cs35l45->irq_invert)
			irq_pol |= IRQF_TRIGGER_HIGH;
		else
			irq_pol |= IRQF_TRIGGER_LOW;

		ret = devm_regmap_add_irq_chip(dev, cs35l45->regmap, cs35l45->irq, irq_pol, 0,
					       &cs35l45_regmap_irq_chip, &cs35l45->irq_data);
		if (ret) {
			dev_err(dev, "Failed to register IRQ chip: %d\n", ret);
			goto err_dsp;
		}

		for (i = 0; i < ARRAY_SIZE(cs35l45_irqs); i++) {
			irq = regmap_irq_get_virq(cs35l45->irq_data, cs35l45_irqs[i].irq);
			if (irq < 0) {
				dev_err(dev, "Failed to get %s\n", cs35l45_irqs[i].name);
				ret = irq;
				goto err_dsp;
			}

			ret = devm_request_threaded_irq(dev, irq, NULL, cs35l45_irqs[i].handler,
							irq_pol, cs35l45_irqs[i].name, cs35l45);
			if (ret) {
				dev_err(dev, "Failed to request IRQ %s: %d\n",
					cs35l45_irqs[i].name, ret);
				goto err_dsp;
			}
		}
	}

	ret = devm_snd_soc_register_component(dev, &cs35l45_component,
					      cs35l45_dai,
					      ARRAY_SIZE(cs35l45_dai));
	if (ret < 0)
		goto err_dsp;

	pm_runtime_put_autosuspend(cs35l45->dev);

	return 0;

err_dsp:
	pm_runtime_disable(cs35l45->dev);
	pm_runtime_put_noidle(cs35l45->dev);
	wm_adsp2_remove(&cs35l45->dsp);

err_reset:
	gpiod_set_value_cansleep(cs35l45->reset_gpio, 0);
err:
	regulator_disable(cs35l45->vdd_a);
	regulator_disable(cs35l45->vdd_batt);

	return ret;
}
EXPORT_SYMBOL_NS_GPL(cs35l45_probe, "SND_SOC_CS35L45");

void cs35l45_remove(struct cs35l45_private *cs35l45)
{
	pm_runtime_get_sync(cs35l45->dev);
	pm_runtime_disable(cs35l45->dev);
	wm_adsp2_remove(&cs35l45->dsp);

	gpiod_set_value_cansleep(cs35l45->reset_gpio, 0);

	pm_runtime_put_noidle(cs35l45->dev);
	regulator_disable(cs35l45->vdd_a);
	/* VDD_BATT must be the last to power-off */
	regulator_disable(cs35l45->vdd_batt);
}
EXPORT_SYMBOL_NS_GPL(cs35l45_remove, "SND_SOC_CS35L45");

EXPORT_GPL_DEV_PM_OPS(cs35l45_pm_ops) = {
	RUNTIME_PM_OPS(cs35l45_runtime_suspend, cs35l45_runtime_resume, NULL)

	SYSTEM_SLEEP_PM_OPS(cs35l45_sys_suspend, cs35l45_sys_resume)
	NOIRQ_SYSTEM_SLEEP_PM_OPS(cs35l45_sys_suspend_noirq, cs35l45_sys_resume_noirq)
};

MODULE_DESCRIPTION("ASoC CS35L45 driver");
MODULE_AUTHOR("James Schulman, Cirrus Logic Inc, <james.schulman@cirrus.com>");
MODULE_AUTHOR("Richard Fitzgerald <rf@opensource.cirrus.com>");
MODULE_LICENSE("GPL");
