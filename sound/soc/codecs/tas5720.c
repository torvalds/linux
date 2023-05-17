// SPDX-License-Identifier: GPL-2.0-only
/*
 * tas5720.c - ALSA SoC Texas Instruments TAS5720 Mono Audio Amplifier
 *
 * Copyright (C)2015-2016 Texas Instruments Incorporated -  https://www.ti.com
 *
 * Author: Andreas Dannenberg <dannenberg@ti.com>
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/regulator/consumer.h>
#include <linux/delay.h>

#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/tlv.h>

#include "tas5720.h"

/* Define how often to check (and clear) the fault status register (in ms) */
#define TAS5720_FAULT_CHECK_INTERVAL		200

enum tas572x_type {
	TAS5720,
	TAS5720A_Q1,
	TAS5722,
};

static const char * const tas5720_supply_names[] = {
	"dvdd",		/* Digital power supply. Connect to 3.3-V supply. */
	"pvdd",		/* Class-D amp and analog power supply (connected). */
};

#define TAS5720_NUM_SUPPLIES	ARRAY_SIZE(tas5720_supply_names)

struct tas5720_data {
	struct snd_soc_component *component;
	struct regmap *regmap;
	struct i2c_client *tas5720_client;
	enum tas572x_type devtype;
	struct regulator_bulk_data supplies[TAS5720_NUM_SUPPLIES];
	struct delayed_work fault_check_work;
	unsigned int last_fault;
};

static int tas5720_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	unsigned int rate = params_rate(params);
	bool ssz_ds;
	int ret;

	switch (rate) {
	case 44100:
	case 48000:
		ssz_ds = false;
		break;
	case 88200:
	case 96000:
		ssz_ds = true;
		break;
	default:
		dev_err(component->dev, "unsupported sample rate: %u\n", rate);
		return -EINVAL;
	}

	ret = snd_soc_component_update_bits(component, TAS5720_DIGITAL_CTRL1_REG,
				  TAS5720_SSZ_DS, ssz_ds);
	if (ret < 0) {
		dev_err(component->dev, "error setting sample rate: %d\n", ret);
		return ret;
	}

	return 0;
}

static int tas5720_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *component = dai->component;
	u8 serial_format;
	int ret;

	if ((fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) != SND_SOC_DAIFMT_CBC_CFC) {
		dev_vdbg(component->dev, "DAI clocking invalid\n");
		return -EINVAL;
	}

	switch (fmt & (SND_SOC_DAIFMT_FORMAT_MASK |
		       SND_SOC_DAIFMT_INV_MASK)) {
	case (SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF):
		/* 1st data bit occur one BCLK cycle after the frame sync */
		serial_format = TAS5720_SAIF_I2S;
		break;
	case (SND_SOC_DAIFMT_DSP_A | SND_SOC_DAIFMT_NB_NF):
		/*
		 * Note that although the TAS5720 does not have a dedicated DSP
		 * mode it doesn't care about the LRCLK duty cycle during TDM
		 * operation. Therefore we can use the device's I2S mode with
		 * its delaying of the 1st data bit to receive DSP_A formatted
		 * data. See device datasheet for additional details.
		 */
		serial_format = TAS5720_SAIF_I2S;
		break;
	case (SND_SOC_DAIFMT_DSP_B | SND_SOC_DAIFMT_NB_NF):
		/*
		 * Similar to DSP_A, we can use the fact that the TAS5720 does
		 * not care about the LRCLK duty cycle during TDM to receive
		 * DSP_B formatted data in LEFTJ mode (no delaying of the 1st
		 * data bit).
		 */
		serial_format = TAS5720_SAIF_LEFTJ;
		break;
	case (SND_SOC_DAIFMT_LEFT_J | SND_SOC_DAIFMT_NB_NF):
		/* No delay after the frame sync */
		serial_format = TAS5720_SAIF_LEFTJ;
		break;
	default:
		dev_vdbg(component->dev, "DAI Format is not found\n");
		return -EINVAL;
	}

	ret = snd_soc_component_update_bits(component, TAS5720_DIGITAL_CTRL1_REG,
				  TAS5720_SAIF_FORMAT_MASK,
				  serial_format);
	if (ret < 0) {
		dev_err(component->dev, "error setting SAIF format: %d\n", ret);
		return ret;
	}

	return 0;
}

static int tas5720_set_dai_tdm_slot(struct snd_soc_dai *dai,
				    unsigned int tx_mask, unsigned int rx_mask,
				    int slots, int slot_width)
{
	struct snd_soc_component *component = dai->component;
	struct tas5720_data *tas5720 = snd_soc_component_get_drvdata(component);
	unsigned int first_slot;
	int ret;

	if (!tx_mask) {
		dev_err(component->dev, "tx masks must not be 0\n");
		return -EINVAL;
	}

	/*
	 * Determine the first slot that is being requested. We will only
	 * use the first slot that is found since the TAS5720 is a mono
	 * amplifier.
	 */
	first_slot = __ffs(tx_mask);

	if (first_slot > 7) {
		dev_err(component->dev, "slot selection out of bounds (%u)\n",
			first_slot);
		return -EINVAL;
	}

	/*
	 * Enable manual TDM slot selection (instead of I2C ID based).
	 * This is not applicable to TAS5720A-Q1.
	 */
	switch (tas5720->devtype) {
	case TAS5720A_Q1:
		break;
	default:
		ret = snd_soc_component_update_bits(component, TAS5720_DIGITAL_CTRL1_REG,
					  TAS5720_TDM_CFG_SRC, TAS5720_TDM_CFG_SRC);
		if (ret < 0)
			goto error_snd_soc_component_update_bits;

		/* Configure the TDM slot to process audio from */
		ret = snd_soc_component_update_bits(component, TAS5720_DIGITAL_CTRL2_REG,
					  TAS5720_TDM_SLOT_SEL_MASK, first_slot);
		if (ret < 0)
			goto error_snd_soc_component_update_bits;
		break;
	}

	/* Configure TDM slot width. This is only applicable to TAS5722. */
	switch (tas5720->devtype) {
	case TAS5722:
		ret = snd_soc_component_update_bits(component, TAS5722_DIGITAL_CTRL2_REG,
						    TAS5722_TDM_SLOT_16B,
						    slot_width == 16 ?
						    TAS5722_TDM_SLOT_16B : 0);
		if (ret < 0)
			goto error_snd_soc_component_update_bits;
		break;
	default:
		break;
	}

	return 0;

error_snd_soc_component_update_bits:
	dev_err(component->dev, "error configuring TDM mode: %d\n", ret);
	return ret;
}

static int tas5720_mute_soc_component(struct snd_soc_component *component, int mute)
{
	struct tas5720_data *tas5720 = snd_soc_component_get_drvdata(component);
	unsigned int reg, mask;
	int ret;

	switch (tas5720->devtype) {
	case TAS5720A_Q1:
		reg = TAS5720_Q1_VOLUME_CTRL_CFG_REG;
		mask = TAS5720_Q1_MUTE;
		break;
	default:
		reg = TAS5720_DIGITAL_CTRL2_REG;
		mask = TAS5720_MUTE;
		break;
	}

	ret = snd_soc_component_update_bits(component, reg, mask, mute ? mask : 0);
	if (ret < 0) {
		dev_err(component->dev, "error (un-)muting device: %d\n", ret);
		return ret;
	}

	return 0;
}

static int tas5720_mute(struct snd_soc_dai *dai, int mute, int direction)
{
	return tas5720_mute_soc_component(dai->component, mute);
}

static void tas5720_fault_check_work(struct work_struct *work)
{
	struct tas5720_data *tas5720 = container_of(work, struct tas5720_data,
			fault_check_work.work);
	struct device *dev = tas5720->component->dev;
	unsigned int curr_fault;
	int ret;

	ret = regmap_read(tas5720->regmap, TAS5720_FAULT_REG, &curr_fault);
	if (ret < 0) {
		dev_err(dev, "failed to read FAULT register: %d\n", ret);
		goto out;
	}

	/* Check/handle all errors except SAIF clock errors */
	curr_fault &= TAS5720_OCE | TAS5720_DCE | TAS5720_OTE;

	/*
	 * Only flag errors once for a given occurrence. This is needed as
	 * the TAS5720 will take time clearing the fault condition internally
	 * during which we don't want to bombard the system with the same
	 * error message over and over.
	 */
	if ((curr_fault & TAS5720_OCE) && !(tas5720->last_fault & TAS5720_OCE))
		dev_crit(dev, "experienced an over current hardware fault\n");

	if ((curr_fault & TAS5720_DCE) && !(tas5720->last_fault & TAS5720_DCE))
		dev_crit(dev, "experienced a DC detection fault\n");

	if ((curr_fault & TAS5720_OTE) && !(tas5720->last_fault & TAS5720_OTE))
		dev_crit(dev, "experienced an over temperature fault\n");

	/* Store current fault value so we can detect any changes next time */
	tas5720->last_fault = curr_fault;

	if (!curr_fault)
		goto out;

	/*
	 * Periodically toggle SDZ (shutdown bit) H->L->H to clear any latching
	 * faults as long as a fault condition persists. Always going through
	 * the full sequence no matter the first return value to minimizes
	 * chances for the device to end up in shutdown mode.
	 */
	ret = regmap_write_bits(tas5720->regmap, TAS5720_POWER_CTRL_REG,
				TAS5720_SDZ, 0);
	if (ret < 0)
		dev_err(dev, "failed to write POWER_CTRL register: %d\n", ret);

	ret = regmap_write_bits(tas5720->regmap, TAS5720_POWER_CTRL_REG,
				TAS5720_SDZ, TAS5720_SDZ);
	if (ret < 0)
		dev_err(dev, "failed to write POWER_CTRL register: %d\n", ret);

out:
	/* Schedule the next fault check at the specified interval */
	schedule_delayed_work(&tas5720->fault_check_work,
			      msecs_to_jiffies(TAS5720_FAULT_CHECK_INTERVAL));
}

static int tas5720_codec_probe(struct snd_soc_component *component)
{
	struct tas5720_data *tas5720 = snd_soc_component_get_drvdata(component);
	unsigned int device_id, expected_device_id;
	int ret;

	tas5720->component = component;

	ret = regulator_bulk_enable(ARRAY_SIZE(tas5720->supplies),
				    tas5720->supplies);
	if (ret != 0) {
		dev_err(component->dev, "failed to enable supplies: %d\n", ret);
		return ret;
	}

	/*
	 * Take a liberal approach to checking the device ID to allow the
	 * driver to be used even if the device ID does not match, however
	 * issue a warning if there is a mismatch.
	 */
	ret = regmap_read(tas5720->regmap, TAS5720_DEVICE_ID_REG, &device_id);
	if (ret < 0) {
		dev_err(component->dev, "failed to read device ID register: %d\n",
			ret);
		goto probe_fail;
	}

	switch (tas5720->devtype) {
	case TAS5720:
		expected_device_id = TAS5720_DEVICE_ID;
		break;
	case TAS5720A_Q1:
		expected_device_id = TAS5720A_Q1_DEVICE_ID;
		break;
	case TAS5722:
		expected_device_id = TAS5722_DEVICE_ID;
		break;
	default:
		dev_err(component->dev, "unexpected private driver data\n");
		ret = -EINVAL;
		goto probe_fail;
	}

	if (device_id != expected_device_id)
		dev_warn(component->dev, "wrong device ID. expected: %u read: %u\n",
			 expected_device_id, device_id);

	/* Set device to mute */
	ret = tas5720_mute_soc_component(component, 1);
	if (ret < 0)
		goto error_snd_soc_component_update_bits;

	/* Set Bit 7 in TAS5720_ANALOG_CTRL_REG to 1 for TAS5720A_Q1 */
	switch (tas5720->devtype) {
	case TAS5720A_Q1:
		ret = snd_soc_component_update_bits(component, TAS5720_ANALOG_CTRL_REG,
						    TAS5720_Q1_RESERVED7_BIT,
						    TAS5720_Q1_RESERVED7_BIT);
		break;
	default:
		break;
	}
	if (ret < 0)
		goto error_snd_soc_component_update_bits;

	/*
	 * Enter shutdown mode - our default when not playing audio - to
	 * minimize current consumption. On the TAS5720 there is no real down
	 * side doing so as all device registers are preserved and the wakeup
	 * of the codec is rather quick which we do using a dapm widget.
	 */
	ret = snd_soc_component_update_bits(component, TAS5720_POWER_CTRL_REG,
				  TAS5720_SDZ, 0);
	if (ret < 0)
		goto error_snd_soc_component_update_bits;

	INIT_DELAYED_WORK(&tas5720->fault_check_work, tas5720_fault_check_work);

	return 0;

error_snd_soc_component_update_bits:
	dev_err(component->dev, "error configuring device registers: %d\n", ret);

probe_fail:
	regulator_bulk_disable(ARRAY_SIZE(tas5720->supplies),
			       tas5720->supplies);
	return ret;
}

static void tas5720_codec_remove(struct snd_soc_component *component)
{
	struct tas5720_data *tas5720 = snd_soc_component_get_drvdata(component);
	int ret;

	cancel_delayed_work_sync(&tas5720->fault_check_work);

	ret = regulator_bulk_disable(ARRAY_SIZE(tas5720->supplies),
				     tas5720->supplies);
	if (ret < 0)
		dev_err(component->dev, "failed to disable supplies: %d\n", ret);
};

static int tas5720_dac_event(struct snd_soc_dapm_widget *w,
			     struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct tas5720_data *tas5720 = snd_soc_component_get_drvdata(component);
	int ret;

	if (event & SND_SOC_DAPM_POST_PMU) {
		/* Take TAS5720 out of shutdown mode */
		ret = snd_soc_component_update_bits(component, TAS5720_POWER_CTRL_REG,
					  TAS5720_SDZ, TAS5720_SDZ);
		if (ret < 0) {
			dev_err(component->dev, "error waking component: %d\n", ret);
			return ret;
		}

		/*
		 * Observe codec shutdown-to-active time. The datasheet only
		 * lists a nominal value however just use-it as-is without
		 * additional padding to minimize the delay introduced in
		 * starting to play audio (actually there is other setup done
		 * by the ASoC framework that will provide additional delays,
		 * so we should always be safe).
		 */
		msleep(25);

		/* Turn on TAS5720 periodic fault checking/handling */
		tas5720->last_fault = 0;
		schedule_delayed_work(&tas5720->fault_check_work,
				msecs_to_jiffies(TAS5720_FAULT_CHECK_INTERVAL));
	} else if (event & SND_SOC_DAPM_PRE_PMD) {
		/* Disable TAS5720 periodic fault checking/handling */
		cancel_delayed_work_sync(&tas5720->fault_check_work);

		/* Place TAS5720 in shutdown mode to minimize current draw */
		ret = snd_soc_component_update_bits(component, TAS5720_POWER_CTRL_REG,
					  TAS5720_SDZ, 0);
		if (ret < 0) {
			dev_err(component->dev, "error shutting down component: %d\n",
				ret);
			return ret;
		}
	}

	return 0;
}

#ifdef CONFIG_PM
static int tas5720_suspend(struct snd_soc_component *component)
{
	struct tas5720_data *tas5720 = snd_soc_component_get_drvdata(component);
	int ret;

	regcache_cache_only(tas5720->regmap, true);
	regcache_mark_dirty(tas5720->regmap);

	ret = regulator_bulk_disable(ARRAY_SIZE(tas5720->supplies),
				     tas5720->supplies);
	if (ret < 0)
		dev_err(component->dev, "failed to disable supplies: %d\n", ret);

	return ret;
}

static int tas5720_resume(struct snd_soc_component *component)
{
	struct tas5720_data *tas5720 = snd_soc_component_get_drvdata(component);
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(tas5720->supplies),
				    tas5720->supplies);
	if (ret < 0) {
		dev_err(component->dev, "failed to enable supplies: %d\n", ret);
		return ret;
	}

	regcache_cache_only(tas5720->regmap, false);

	ret = regcache_sync(tas5720->regmap);
	if (ret < 0) {
		dev_err(component->dev, "failed to sync regcache: %d\n", ret);
		return ret;
	}

	return 0;
}
#else
#define tas5720_suspend NULL
#define tas5720_resume NULL
#endif

static bool tas5720_is_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TAS5720_DEVICE_ID_REG:
	case TAS5720_FAULT_REG:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config tas5720_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = TAS5720_MAX_REG,
	.cache_type = REGCACHE_RBTREE,
	.volatile_reg = tas5720_is_volatile_reg,
};

static const struct regmap_config tas5720a_q1_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = TAS5720_MAX_REG,
	.cache_type = REGCACHE_RBTREE,
	.volatile_reg = tas5720_is_volatile_reg,
};

static const struct regmap_config tas5722_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = TAS5722_MAX_REG,
	.cache_type = REGCACHE_RBTREE,
	.volatile_reg = tas5720_is_volatile_reg,
};

/*
 * DAC analog gain. There are four discrete values to select from, ranging
 * from 19.2 dB to 26.3dB.
 */
static const DECLARE_TLV_DB_RANGE(dac_analog_tlv,
	0x0, 0x0, TLV_DB_SCALE_ITEM(1920, 0, 0),
	0x1, 0x1, TLV_DB_SCALE_ITEM(2070, 0, 0),
	0x2, 0x2, TLV_DB_SCALE_ITEM(2350, 0, 0),
	0x3, 0x3, TLV_DB_SCALE_ITEM(2630, 0, 0),
);

/*
 * DAC analog gain for TAS5720A-Q1. There are three discrete values to select from, ranging
 * from 19.2 dB to 25.0dB.
 */
static const DECLARE_TLV_DB_RANGE(dac_analog_tlv_a_q1,
	0x0, 0x0, TLV_DB_SCALE_ITEM(1920, 0, 0),
	0x1, 0x1, TLV_DB_SCALE_ITEM(2260, 0, 0),
	0x2, 0x2, TLV_DB_SCALE_ITEM(2500, 0, 0),
);

/*
 * DAC digital volumes. From -103.5 to 24 dB in 0.5 dB or 0.25 dB steps
 * depending on the device. Note that setting the gain below -100 dB
 * (register value <0x7) is effectively a MUTE as per device datasheet.
 *
 * Note that for the TAS5722 the digital volume controls are actually split
 * over two registers, so we need custom getters/setters for access.
 */
static DECLARE_TLV_DB_SCALE(tas5720_dac_tlv, -10350, 50, 0);
static DECLARE_TLV_DB_SCALE(tas5722_dac_tlv, -10350, 25, 0);

static int tas5722_volume_get(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	unsigned int val;

	val = snd_soc_component_read(component, TAS5720_VOLUME_CTRL_REG);
	ucontrol->value.integer.value[0] = val << 1;

	val = snd_soc_component_read(component, TAS5722_DIGITAL_CTRL2_REG);
	ucontrol->value.integer.value[0] |= val & TAS5722_VOL_CONTROL_LSB;

	return 0;
}

static int tas5722_volume_set(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	unsigned int sel = ucontrol->value.integer.value[0];

	snd_soc_component_write(component, TAS5720_VOLUME_CTRL_REG, sel >> 1);
	snd_soc_component_update_bits(component, TAS5722_DIGITAL_CTRL2_REG,
				      TAS5722_VOL_CONTROL_LSB, sel);

	return 0;
}

static const struct snd_kcontrol_new tas5720_snd_controls[] = {
	SOC_SINGLE_TLV("Speaker Driver Playback Volume",
		       TAS5720_VOLUME_CTRL_REG, 0, 0xff, 0, tas5720_dac_tlv),
	SOC_SINGLE_TLV("Speaker Driver Analog Gain", TAS5720_ANALOG_CTRL_REG,
		       TAS5720_ANALOG_GAIN_SHIFT, 3, 0, dac_analog_tlv),
};

static const struct snd_kcontrol_new tas5720a_q1_snd_controls[] = {
	SOC_DOUBLE_R_TLV("Speaker Driver Playback Volume",
				TAS5720_Q1_VOLUME_CTRL_LEFT_REG,
				TAS5720_Q1_VOLUME_CTRL_RIGHT_REG,
				0, 0xff, 0, tas5720_dac_tlv),
	SOC_SINGLE_TLV("Speaker Driver Analog Gain", TAS5720_ANALOG_CTRL_REG,
				TAS5720_ANALOG_GAIN_SHIFT, 3, 0, dac_analog_tlv_a_q1),
};

static const struct snd_kcontrol_new tas5722_snd_controls[] = {
	SOC_SINGLE_EXT_TLV("Speaker Driver Playback Volume",
			   0, 0, 511, 0,
			   tas5722_volume_get, tas5722_volume_set,
			   tas5722_dac_tlv),
	SOC_SINGLE_TLV("Speaker Driver Analog Gain", TAS5720_ANALOG_CTRL_REG,
		       TAS5720_ANALOG_GAIN_SHIFT, 3, 0, dac_analog_tlv),
};

static const struct snd_soc_dapm_widget tas5720_dapm_widgets[] = {
	SND_SOC_DAPM_AIF_IN("DAC IN", "Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_DAC_E("DAC", NULL, SND_SOC_NOPM, 0, 0, tas5720_dac_event,
			   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_OUTPUT("OUT")
};

static const struct snd_soc_dapm_route tas5720_audio_map[] = {
	{ "DAC", NULL, "DAC IN" },
	{ "OUT", NULL, "DAC" },
};

static const struct snd_soc_component_driver soc_component_dev_tas5720 = {
	.probe			= tas5720_codec_probe,
	.remove			= tas5720_codec_remove,
	.suspend		= tas5720_suspend,
	.resume			= tas5720_resume,
	.controls		= tas5720_snd_controls,
	.num_controls		= ARRAY_SIZE(tas5720_snd_controls),
	.dapm_widgets		= tas5720_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(tas5720_dapm_widgets),
	.dapm_routes		= tas5720_audio_map,
	.num_dapm_routes	= ARRAY_SIZE(tas5720_audio_map),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
};

static const struct snd_soc_component_driver soc_component_dev_tas5720_a_q1 = {
	.probe			= tas5720_codec_probe,
	.remove			= tas5720_codec_remove,
	.suspend		= tas5720_suspend,
	.resume			= tas5720_resume,
	.controls		= tas5720a_q1_snd_controls,
	.num_controls		= ARRAY_SIZE(tas5720a_q1_snd_controls),
	.dapm_widgets		= tas5720_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(tas5720_dapm_widgets),
	.dapm_routes		= tas5720_audio_map,
	.num_dapm_routes	= ARRAY_SIZE(tas5720_audio_map),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
};

static const struct snd_soc_component_driver soc_component_dev_tas5722 = {
	.probe = tas5720_codec_probe,
	.remove = tas5720_codec_remove,
	.suspend = tas5720_suspend,
	.resume = tas5720_resume,
	.controls = tas5722_snd_controls,
	.num_controls = ARRAY_SIZE(tas5722_snd_controls),
	.dapm_widgets = tas5720_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(tas5720_dapm_widgets),
	.dapm_routes = tas5720_audio_map,
	.num_dapm_routes = ARRAY_SIZE(tas5720_audio_map),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
};

/* PCM rates supported by the TAS5720 driver */
#define TAS5720_RATES	(SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 |\
			 SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000)

/* Formats supported by TAS5720 driver */
#define TAS5720_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S18_3LE |\
			 SNDRV_PCM_FMTBIT_S20_3LE | SNDRV_PCM_FMTBIT_S24_LE)

static const struct snd_soc_dai_ops tas5720_speaker_dai_ops = {
	.hw_params	= tas5720_hw_params,
	.set_fmt	= tas5720_set_dai_fmt,
	.set_tdm_slot	= tas5720_set_dai_tdm_slot,
	.mute_stream	= tas5720_mute,
	.no_capture_mute = 1,
};

/*
 * TAS5720 DAI structure
 *
 * Note that were are advertising .playback.channels_max = 2 despite this being
 * a mono amplifier. The reason for that is that some serial ports such as TI's
 * McASP module have a minimum number of channels (2) that they can output.
 * Advertising more channels than we have will allow us to interface with such
 * a serial port without really any negative side effects as the TAS5720 will
 * simply ignore any extra channel(s) asides from the one channel that is
 * configured to be played back.
 */
static struct snd_soc_dai_driver tas5720_dai[] = {
	{
		.name = "tas5720-amplifier",
		.playback = {
			.stream_name = "Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = TAS5720_RATES,
			.formats = TAS5720_FORMATS,
		},
		.ops = &tas5720_speaker_dai_ops,
	},
};

static const struct i2c_device_id tas5720_id[] = {
	{ "tas5720", TAS5720 },
	{ "tas5720a-q1", TAS5720A_Q1 },
	{ "tas5722", TAS5722 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tas5720_id);

static int tas5720_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct tas5720_data *data;
	const struct regmap_config *regmap_config;
	const struct i2c_device_id *id;
	int ret;
	int i;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	id = i2c_match_id(tas5720_id, client);
	data->tas5720_client = client;
	data->devtype = id->driver_data;

	switch (id->driver_data) {
	case TAS5720:
		regmap_config = &tas5720_regmap_config;
		break;
	case TAS5720A_Q1:
		regmap_config = &tas5720a_q1_regmap_config;
		break;
	case TAS5722:
		regmap_config = &tas5722_regmap_config;
		break;
	default:
		dev_err(dev, "unexpected private driver data\n");
		return -EINVAL;
	}
	data->regmap = devm_regmap_init_i2c(client, regmap_config);
	if (IS_ERR(data->regmap)) {
		ret = PTR_ERR(data->regmap);
		dev_err(dev, "failed to allocate register map: %d\n", ret);
		return ret;
	}

	for (i = 0; i < ARRAY_SIZE(data->supplies); i++)
		data->supplies[i].supply = tas5720_supply_names[i];

	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(data->supplies),
				      data->supplies);
	if (ret != 0) {
		dev_err(dev, "failed to request supplies: %d\n", ret);
		return ret;
	}

	dev_set_drvdata(dev, data);

	switch (id->driver_data) {
	case TAS5720:
		ret = devm_snd_soc_register_component(&client->dev,
					&soc_component_dev_tas5720,
					tas5720_dai,
					ARRAY_SIZE(tas5720_dai));
		break;
	case TAS5720A_Q1:
		ret = devm_snd_soc_register_component(&client->dev,
					&soc_component_dev_tas5720_a_q1,
					tas5720_dai,
					ARRAY_SIZE(tas5720_dai));
		break;
	case TAS5722:
		ret = devm_snd_soc_register_component(&client->dev,
					&soc_component_dev_tas5722,
					tas5720_dai,
					ARRAY_SIZE(tas5720_dai));
		break;
	default:
		dev_err(dev, "unexpected private driver data\n");
		return -EINVAL;
	}
	if (ret < 0) {
		dev_err(dev, "failed to register component: %d\n", ret);
		return ret;
	}

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id tas5720_of_match[] = {
	{ .compatible = "ti,tas5720", },
	{ .compatible = "ti,tas5720a-q1", },
	{ .compatible = "ti,tas5722", },
	{ },
};
MODULE_DEVICE_TABLE(of, tas5720_of_match);
#endif

static struct i2c_driver tas5720_i2c_driver = {
	.driver = {
		.name = "tas5720",
		.of_match_table = of_match_ptr(tas5720_of_match),
	},
	.probe_new = tas5720_probe,
	.id_table = tas5720_id,
};

module_i2c_driver(tas5720_i2c_driver);

MODULE_AUTHOR("Andreas Dannenberg <dannenberg@ti.com>");
MODULE_DESCRIPTION("TAS5720 Audio amplifier driver");
MODULE_LICENSE("GPL");
