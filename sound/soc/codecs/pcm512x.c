// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for the PCM512x CODECs
 *
 * Author:	Mark Brown <broonie@kernel.org>
 *		Copyright 2014 Linaro Ltd
 */


#include <linux/init.h>
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/kernel.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/gcd.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/pcm_params.h>
#include <sound/tlv.h>

#include "pcm512x.h"

#define PCM512x_NUM_SUPPLIES 3
static const char * const pcm512x_supply_names[PCM512x_NUM_SUPPLIES] = {
	"AVDD",
	"DVDD",
	"CPVDD",
};

struct pcm512x_priv {
	struct regmap *regmap;
	struct clk *sclk;
	struct regulator_bulk_data supplies[PCM512x_NUM_SUPPLIES];
	struct notifier_block supply_nb[PCM512x_NUM_SUPPLIES];
	int fmt;
	int pll_in;
	int pll_out;
	int pll_r;
	int pll_j;
	int pll_d;
	int pll_p;
	unsigned long real_pll;
	unsigned long overclock_pll;
	unsigned long overclock_dac;
	unsigned long overclock_dsp;
	int mute;
	struct mutex mutex;
	unsigned int bclk_ratio;
};

/*
 * We can't use the same notifier block for more than one supply and
 * there's no way I can see to get from a callback to the caller
 * except container_of().
 */
#define PCM512x_REGULATOR_EVENT(n) \
static int pcm512x_regulator_event_##n(struct notifier_block *nb, \
				      unsigned long event, void *data)    \
{ \
	struct pcm512x_priv *pcm512x = container_of(nb, struct pcm512x_priv, \
						    supply_nb[n]); \
	if (event & REGULATOR_EVENT_DISABLE) { \
		regcache_mark_dirty(pcm512x->regmap);	\
		regcache_cache_only(pcm512x->regmap, true);	\
	} \
	return 0; \
}

PCM512x_REGULATOR_EVENT(0)
PCM512x_REGULATOR_EVENT(1)
PCM512x_REGULATOR_EVENT(2)

static const struct reg_default pcm512x_reg_defaults[] = {
	{ PCM512x_RESET,             0x00 },
	{ PCM512x_POWER,             0x00 },
	{ PCM512x_MUTE,              0x00 },
	{ PCM512x_DSP,               0x00 },
	{ PCM512x_PLL_REF,           0x00 },
	{ PCM512x_DAC_REF,           0x00 },
	{ PCM512x_DAC_ROUTING,       0x11 },
	{ PCM512x_DSP_PROGRAM,       0x01 },
	{ PCM512x_CLKDET,            0x00 },
	{ PCM512x_AUTO_MUTE,         0x00 },
	{ PCM512x_ERROR_DETECT,      0x00 },
	{ PCM512x_DIGITAL_VOLUME_1,  0x00 },
	{ PCM512x_DIGITAL_VOLUME_2,  0x30 },
	{ PCM512x_DIGITAL_VOLUME_3,  0x30 },
	{ PCM512x_DIGITAL_MUTE_1,    0x22 },
	{ PCM512x_DIGITAL_MUTE_2,    0x00 },
	{ PCM512x_DIGITAL_MUTE_3,    0x07 },
	{ PCM512x_OUTPUT_AMPLITUDE,  0x00 },
	{ PCM512x_ANALOG_GAIN_CTRL,  0x00 },
	{ PCM512x_UNDERVOLTAGE_PROT, 0x00 },
	{ PCM512x_ANALOG_MUTE_CTRL,  0x00 },
	{ PCM512x_ANALOG_GAIN_BOOST, 0x00 },
	{ PCM512x_VCOM_CTRL_1,       0x00 },
	{ PCM512x_VCOM_CTRL_2,       0x01 },
	{ PCM512x_BCLK_LRCLK_CFG,    0x00 },
	{ PCM512x_MASTER_MODE,       0x7c },
	{ PCM512x_GPIO_DACIN,        0x00 },
	{ PCM512x_GPIO_PLLIN,        0x00 },
	{ PCM512x_SYNCHRONIZE,       0x10 },
	{ PCM512x_PLL_COEFF_0,       0x00 },
	{ PCM512x_PLL_COEFF_1,       0x00 },
	{ PCM512x_PLL_COEFF_2,       0x00 },
	{ PCM512x_PLL_COEFF_3,       0x00 },
	{ PCM512x_PLL_COEFF_4,       0x00 },
	{ PCM512x_DSP_CLKDIV,        0x00 },
	{ PCM512x_DAC_CLKDIV,        0x00 },
	{ PCM512x_NCP_CLKDIV,        0x00 },
	{ PCM512x_OSR_CLKDIV,        0x00 },
	{ PCM512x_MASTER_CLKDIV_1,   0x00 },
	{ PCM512x_MASTER_CLKDIV_2,   0x00 },
	{ PCM512x_FS_SPEED_MODE,     0x00 },
	{ PCM512x_IDAC_1,            0x01 },
	{ PCM512x_IDAC_2,            0x00 },
	{ PCM512x_I2S_1,             0x02 },
	{ PCM512x_I2S_2,             0x00 },
};

static bool pcm512x_readable(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case PCM512x_RESET:
	case PCM512x_POWER:
	case PCM512x_MUTE:
	case PCM512x_PLL_EN:
	case PCM512x_SPI_MISO_FUNCTION:
	case PCM512x_DSP:
	case PCM512x_GPIO_EN:
	case PCM512x_BCLK_LRCLK_CFG:
	case PCM512x_DSP_GPIO_INPUT:
	case PCM512x_MASTER_MODE:
	case PCM512x_PLL_REF:
	case PCM512x_DAC_REF:
	case PCM512x_GPIO_DACIN:
	case PCM512x_GPIO_PLLIN:
	case PCM512x_SYNCHRONIZE:
	case PCM512x_PLL_COEFF_0:
	case PCM512x_PLL_COEFF_1:
	case PCM512x_PLL_COEFF_2:
	case PCM512x_PLL_COEFF_3:
	case PCM512x_PLL_COEFF_4:
	case PCM512x_DSP_CLKDIV:
	case PCM512x_DAC_CLKDIV:
	case PCM512x_NCP_CLKDIV:
	case PCM512x_OSR_CLKDIV:
	case PCM512x_MASTER_CLKDIV_1:
	case PCM512x_MASTER_CLKDIV_2:
	case PCM512x_FS_SPEED_MODE:
	case PCM512x_IDAC_1:
	case PCM512x_IDAC_2:
	case PCM512x_ERROR_DETECT:
	case PCM512x_I2S_1:
	case PCM512x_I2S_2:
	case PCM512x_DAC_ROUTING:
	case PCM512x_DSP_PROGRAM:
	case PCM512x_CLKDET:
	case PCM512x_AUTO_MUTE:
	case PCM512x_DIGITAL_VOLUME_1:
	case PCM512x_DIGITAL_VOLUME_2:
	case PCM512x_DIGITAL_VOLUME_3:
	case PCM512x_DIGITAL_MUTE_1:
	case PCM512x_DIGITAL_MUTE_2:
	case PCM512x_DIGITAL_MUTE_3:
	case PCM512x_GPIO_OUTPUT_1:
	case PCM512x_GPIO_OUTPUT_2:
	case PCM512x_GPIO_OUTPUT_3:
	case PCM512x_GPIO_OUTPUT_4:
	case PCM512x_GPIO_OUTPUT_5:
	case PCM512x_GPIO_OUTPUT_6:
	case PCM512x_GPIO_CONTROL_1:
	case PCM512x_GPIO_CONTROL_2:
	case PCM512x_OVERFLOW:
	case PCM512x_RATE_DET_1:
	case PCM512x_RATE_DET_2:
	case PCM512x_RATE_DET_3:
	case PCM512x_RATE_DET_4:
	case PCM512x_CLOCK_STATUS:
	case PCM512x_ANALOG_MUTE_DET:
	case PCM512x_GPIN:
	case PCM512x_DIGITAL_MUTE_DET:
	case PCM512x_OUTPUT_AMPLITUDE:
	case PCM512x_ANALOG_GAIN_CTRL:
	case PCM512x_UNDERVOLTAGE_PROT:
	case PCM512x_ANALOG_MUTE_CTRL:
	case PCM512x_ANALOG_GAIN_BOOST:
	case PCM512x_VCOM_CTRL_1:
	case PCM512x_VCOM_CTRL_2:
	case PCM512x_CRAM_CTRL:
	case PCM512x_FLEX_A:
	case PCM512x_FLEX_B:
		return true;
	default:
		/* There are 256 raw register addresses */
		return reg < 0xff;
	}
}

static bool pcm512x_volatile(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case PCM512x_PLL_EN:
	case PCM512x_OVERFLOW:
	case PCM512x_RATE_DET_1:
	case PCM512x_RATE_DET_2:
	case PCM512x_RATE_DET_3:
	case PCM512x_RATE_DET_4:
	case PCM512x_CLOCK_STATUS:
	case PCM512x_ANALOG_MUTE_DET:
	case PCM512x_GPIN:
	case PCM512x_DIGITAL_MUTE_DET:
	case PCM512x_CRAM_CTRL:
		return true;
	default:
		/* There are 256 raw register addresses */
		return reg < 0xff;
	}
}

static int pcm512x_overclock_pll_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct pcm512x_priv *pcm512x = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = pcm512x->overclock_pll;
	return 0;
}

static int pcm512x_overclock_pll_put(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct pcm512x_priv *pcm512x = snd_soc_component_get_drvdata(component);

	switch (snd_soc_component_get_bias_level(component)) {
	case SND_SOC_BIAS_OFF:
	case SND_SOC_BIAS_STANDBY:
		break;
	default:
		return -EBUSY;
	}

	pcm512x->overclock_pll = ucontrol->value.integer.value[0];
	return 0;
}

static int pcm512x_overclock_dsp_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct pcm512x_priv *pcm512x = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = pcm512x->overclock_dsp;
	return 0;
}

static int pcm512x_overclock_dsp_put(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct pcm512x_priv *pcm512x = snd_soc_component_get_drvdata(component);

	switch (snd_soc_component_get_bias_level(component)) {
	case SND_SOC_BIAS_OFF:
	case SND_SOC_BIAS_STANDBY:
		break;
	default:
		return -EBUSY;
	}

	pcm512x->overclock_dsp = ucontrol->value.integer.value[0];
	return 0;
}

static int pcm512x_overclock_dac_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct pcm512x_priv *pcm512x = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = pcm512x->overclock_dac;
	return 0;
}

static int pcm512x_overclock_dac_put(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct pcm512x_priv *pcm512x = snd_soc_component_get_drvdata(component);

	switch (snd_soc_component_get_bias_level(component)) {
	case SND_SOC_BIAS_OFF:
	case SND_SOC_BIAS_STANDBY:
		break;
	default:
		return -EBUSY;
	}

	pcm512x->overclock_dac = ucontrol->value.integer.value[0];
	return 0;
}

static const DECLARE_TLV_DB_SCALE(digital_tlv, -10350, 50, 1);
static const DECLARE_TLV_DB_SCALE(analog_tlv, -600, 600, 0);
static const DECLARE_TLV_DB_SCALE(boost_tlv, 0, 80, 0);

static const char * const pcm512x_dsp_program_texts[] = {
	"FIR interpolation with de-emphasis",
	"Low latency IIR with de-emphasis",
	"High attenuation with de-emphasis",
	"Fixed process flow",
	"Ringing-less low latency FIR",
};

static const unsigned int pcm512x_dsp_program_values[] = {
	1,
	2,
	3,
	5,
	7,
};

static SOC_VALUE_ENUM_SINGLE_DECL(pcm512x_dsp_program,
				  PCM512x_DSP_PROGRAM, 0, 0x1f,
				  pcm512x_dsp_program_texts,
				  pcm512x_dsp_program_values);

static const char * const pcm512x_clk_missing_text[] = {
	"1s", "2s", "3s", "4s", "5s", "6s", "7s", "8s"
};

static const struct soc_enum pcm512x_clk_missing =
	SOC_ENUM_SINGLE(PCM512x_CLKDET, 0,  8, pcm512x_clk_missing_text);

static const char * const pcm512x_autom_text[] = {
	"21ms", "106ms", "213ms", "533ms", "1.07s", "2.13s", "5.33s", "10.66s"
};

static const struct soc_enum pcm512x_autom_l =
	SOC_ENUM_SINGLE(PCM512x_AUTO_MUTE, PCM512x_ATML_SHIFT, 8,
			pcm512x_autom_text);

static const struct soc_enum pcm512x_autom_r =
	SOC_ENUM_SINGLE(PCM512x_AUTO_MUTE, PCM512x_ATMR_SHIFT, 8,
			pcm512x_autom_text);

static const char * const pcm512x_ramp_rate_text[] = {
	"1 sample/update", "2 samples/update", "4 samples/update",
	"Immediate"
};

static const struct soc_enum pcm512x_vndf =
	SOC_ENUM_SINGLE(PCM512x_DIGITAL_MUTE_1, PCM512x_VNDF_SHIFT, 4,
			pcm512x_ramp_rate_text);

static const struct soc_enum pcm512x_vnuf =
	SOC_ENUM_SINGLE(PCM512x_DIGITAL_MUTE_1, PCM512x_VNUF_SHIFT, 4,
			pcm512x_ramp_rate_text);

static const struct soc_enum pcm512x_vedf =
	SOC_ENUM_SINGLE(PCM512x_DIGITAL_MUTE_2, PCM512x_VEDF_SHIFT, 4,
			pcm512x_ramp_rate_text);

static const char * const pcm512x_ramp_step_text[] = {
	"4dB/step", "2dB/step", "1dB/step", "0.5dB/step"
};

static const struct soc_enum pcm512x_vnds =
	SOC_ENUM_SINGLE(PCM512x_DIGITAL_MUTE_1, PCM512x_VNDS_SHIFT, 4,
			pcm512x_ramp_step_text);

static const struct soc_enum pcm512x_vnus =
	SOC_ENUM_SINGLE(PCM512x_DIGITAL_MUTE_1, PCM512x_VNUS_SHIFT, 4,
			pcm512x_ramp_step_text);

static const struct soc_enum pcm512x_veds =
	SOC_ENUM_SINGLE(PCM512x_DIGITAL_MUTE_2, PCM512x_VEDS_SHIFT, 4,
			pcm512x_ramp_step_text);

static int pcm512x_update_mute(struct pcm512x_priv *pcm512x)
{
	return regmap_update_bits(
		pcm512x->regmap, PCM512x_MUTE, PCM512x_RQML | PCM512x_RQMR,
		(!!(pcm512x->mute & 0x5) << PCM512x_RQML_SHIFT)
		| (!!(pcm512x->mute & 0x3) << PCM512x_RQMR_SHIFT));
}

static int pcm512x_digital_playback_switch_get(struct snd_kcontrol *kcontrol,
					       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct pcm512x_priv *pcm512x = snd_soc_component_get_drvdata(component);

	mutex_lock(&pcm512x->mutex);
	ucontrol->value.integer.value[0] = !(pcm512x->mute & 0x4);
	ucontrol->value.integer.value[1] = !(pcm512x->mute & 0x2);
	mutex_unlock(&pcm512x->mutex);

	return 0;
}

static int pcm512x_digital_playback_switch_put(struct snd_kcontrol *kcontrol,
					       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct pcm512x_priv *pcm512x = snd_soc_component_get_drvdata(component);
	int ret, changed = 0;

	mutex_lock(&pcm512x->mutex);

	if ((pcm512x->mute & 0x4) == (ucontrol->value.integer.value[0] << 2)) {
		pcm512x->mute ^= 0x4;
		changed = 1;
	}
	if ((pcm512x->mute & 0x2) == (ucontrol->value.integer.value[1] << 1)) {
		pcm512x->mute ^= 0x2;
		changed = 1;
	}

	if (changed) {
		ret = pcm512x_update_mute(pcm512x);
		if (ret != 0) {
			dev_err(component->dev,
				"Failed to update digital mute: %d\n", ret);
			mutex_unlock(&pcm512x->mutex);
			return ret;
		}
	}

	mutex_unlock(&pcm512x->mutex);

	return changed;
}

static const struct snd_kcontrol_new pcm512x_controls[] = {
SOC_DOUBLE_R_TLV("Digital Playback Volume", PCM512x_DIGITAL_VOLUME_2,
		 PCM512x_DIGITAL_VOLUME_3, 0, 255, 1, digital_tlv),
SOC_DOUBLE_TLV("Analogue Playback Volume", PCM512x_ANALOG_GAIN_CTRL,
	       PCM512x_LAGN_SHIFT, PCM512x_RAGN_SHIFT, 1, 1, analog_tlv),
SOC_DOUBLE_TLV("Analogue Playback Boost Volume", PCM512x_ANALOG_GAIN_BOOST,
	       PCM512x_AGBL_SHIFT, PCM512x_AGBR_SHIFT, 1, 0, boost_tlv),
{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Digital Playback Switch",
	.index = 0,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.info = snd_ctl_boolean_stereo_info,
	.get = pcm512x_digital_playback_switch_get,
	.put = pcm512x_digital_playback_switch_put
},

SOC_SINGLE("Deemphasis Switch", PCM512x_DSP, PCM512x_DEMP_SHIFT, 1, 1),
SOC_ENUM("DSP Program", pcm512x_dsp_program),

SOC_ENUM("Clock Missing Period", pcm512x_clk_missing),
SOC_ENUM("Auto Mute Time Left", pcm512x_autom_l),
SOC_ENUM("Auto Mute Time Right", pcm512x_autom_r),
SOC_SINGLE("Auto Mute Mono Switch", PCM512x_DIGITAL_MUTE_3,
	   PCM512x_ACTL_SHIFT, 1, 0),
SOC_DOUBLE("Auto Mute Switch", PCM512x_DIGITAL_MUTE_3, PCM512x_AMLE_SHIFT,
	   PCM512x_AMRE_SHIFT, 1, 0),

SOC_ENUM("Volume Ramp Down Rate", pcm512x_vndf),
SOC_ENUM("Volume Ramp Down Step", pcm512x_vnds),
SOC_ENUM("Volume Ramp Up Rate", pcm512x_vnuf),
SOC_ENUM("Volume Ramp Up Step", pcm512x_vnus),
SOC_ENUM("Volume Ramp Down Emergency Rate", pcm512x_vedf),
SOC_ENUM("Volume Ramp Down Emergency Step", pcm512x_veds),

SOC_SINGLE_EXT("Max Overclock PLL", SND_SOC_NOPM, 0, 20, 0,
	       pcm512x_overclock_pll_get, pcm512x_overclock_pll_put),
SOC_SINGLE_EXT("Max Overclock DSP", SND_SOC_NOPM, 0, 40, 0,
	       pcm512x_overclock_dsp_get, pcm512x_overclock_dsp_put),
SOC_SINGLE_EXT("Max Overclock DAC", SND_SOC_NOPM, 0, 40, 0,
	       pcm512x_overclock_dac_get, pcm512x_overclock_dac_put),
};

static const struct snd_soc_dapm_widget pcm512x_dapm_widgets[] = {
SND_SOC_DAPM_DAC("DACL", NULL, SND_SOC_NOPM, 0, 0),
SND_SOC_DAPM_DAC("DACR", NULL, SND_SOC_NOPM, 0, 0),

SND_SOC_DAPM_OUTPUT("OUTL"),
SND_SOC_DAPM_OUTPUT("OUTR"),
};

static const struct snd_soc_dapm_route pcm512x_dapm_routes[] = {
	{ "DACL", NULL, "Playback" },
	{ "DACR", NULL, "Playback" },

	{ "OUTL", NULL, "DACL" },
	{ "OUTR", NULL, "DACR" },
};

static unsigned long pcm512x_pll_max(struct pcm512x_priv *pcm512x)
{
	return 25000000 + 25000000 * pcm512x->overclock_pll / 100;
}

static unsigned long pcm512x_dsp_max(struct pcm512x_priv *pcm512x)
{
	return 50000000 + 50000000 * pcm512x->overclock_dsp / 100;
}

static unsigned long pcm512x_dac_max(struct pcm512x_priv *pcm512x,
				     unsigned long rate)
{
	return rate + rate * pcm512x->overclock_dac / 100;
}

static unsigned long pcm512x_sck_max(struct pcm512x_priv *pcm512x)
{
	if (!pcm512x->pll_out)
		return 25000000;
	return pcm512x_pll_max(pcm512x);
}

static unsigned long pcm512x_ncp_target(struct pcm512x_priv *pcm512x,
					unsigned long dac_rate)
{
	/*
	 * If the DAC is not actually overclocked, use the good old
	 * NCP target rate...
	 */
	if (dac_rate <= 6144000)
		return 1536000;
	/*
	 * ...but if the DAC is in fact overclocked, bump the NCP target
	 * rate to get the recommended dividers even when overclocking.
	 */
	return pcm512x_dac_max(pcm512x, 1536000);
}

static const u32 pcm512x_dai_rates[] = {
	8000, 11025, 16000, 22050, 32000, 44100, 48000, 64000,
	88200, 96000, 176400, 192000, 384000,
};

static const struct snd_pcm_hw_constraint_list constraints_slave = {
	.count = ARRAY_SIZE(pcm512x_dai_rates),
	.list  = pcm512x_dai_rates,
};

static int pcm512x_hw_rule_rate(struct snd_pcm_hw_params *params,
				struct snd_pcm_hw_rule *rule)
{
	struct pcm512x_priv *pcm512x = rule->private;
	struct snd_interval ranges[2];
	int frame_size;

	frame_size = snd_soc_params_to_frame_size(params);
	if (frame_size < 0)
		return frame_size;

	switch (frame_size) {
	case 32:
		/* No hole when the frame size is 32. */
		return 0;
	case 48:
	case 64:
		/* There is only one hole in the range of supported
		 * rates, but it moves with the frame size.
		 */
		memset(ranges, 0, sizeof(ranges));
		ranges[0].min = 8000;
		ranges[0].max = pcm512x_sck_max(pcm512x) / frame_size / 2;
		ranges[1].min = DIV_ROUND_UP(16000000, frame_size);
		ranges[1].max = 384000;
		break;
	default:
		return -EINVAL;
	}

	return snd_interval_ranges(hw_param_interval(params, rule->var),
				   ARRAY_SIZE(ranges), ranges, 0);
}

static int pcm512x_dai_startup_master(struct snd_pcm_substream *substream,
				      struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct pcm512x_priv *pcm512x = snd_soc_component_get_drvdata(component);
	struct device *dev = dai->dev;
	struct snd_pcm_hw_constraint_ratnums *constraints_no_pll;
	struct snd_ratnum *rats_no_pll;

	if (IS_ERR(pcm512x->sclk)) {
		dev_err(dev, "Need SCLK for master mode: %ld\n",
			PTR_ERR(pcm512x->sclk));
		return PTR_ERR(pcm512x->sclk);
	}

	if (pcm512x->pll_out)
		return snd_pcm_hw_rule_add(substream->runtime, 0,
					   SNDRV_PCM_HW_PARAM_RATE,
					   pcm512x_hw_rule_rate,
					   pcm512x,
					   SNDRV_PCM_HW_PARAM_FRAME_BITS,
					   SNDRV_PCM_HW_PARAM_CHANNELS, -1);

	constraints_no_pll = devm_kzalloc(dev, sizeof(*constraints_no_pll),
					  GFP_KERNEL);
	if (!constraints_no_pll)
		return -ENOMEM;
	constraints_no_pll->nrats = 1;
	rats_no_pll = devm_kzalloc(dev, sizeof(*rats_no_pll), GFP_KERNEL);
	if (!rats_no_pll)
		return -ENOMEM;
	constraints_no_pll->rats = rats_no_pll;
	rats_no_pll->num = clk_get_rate(pcm512x->sclk) / 64;
	rats_no_pll->den_min = 1;
	rats_no_pll->den_max = 128;
	rats_no_pll->den_step = 1;

	return snd_pcm_hw_constraint_ratnums(substream->runtime, 0,
					     SNDRV_PCM_HW_PARAM_RATE,
					     constraints_no_pll);
}

static int pcm512x_dai_startup_slave(struct snd_pcm_substream *substream,
				     struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct pcm512x_priv *pcm512x = snd_soc_component_get_drvdata(component);
	struct device *dev = dai->dev;
	struct regmap *regmap = pcm512x->regmap;

	if (IS_ERR(pcm512x->sclk)) {
		dev_info(dev, "No SCLK, using BCLK: %ld\n",
			 PTR_ERR(pcm512x->sclk));

		/* Disable reporting of missing SCLK as an error */
		regmap_update_bits(regmap, PCM512x_ERROR_DETECT,
				   PCM512x_IDCH, PCM512x_IDCH);

		/* Switch PLL input to BCLK */
		regmap_update_bits(regmap, PCM512x_PLL_REF,
				   PCM512x_SREF, PCM512x_SREF_BCK);
	}

	return snd_pcm_hw_constraint_list(substream->runtime, 0,
					  SNDRV_PCM_HW_PARAM_RATE,
					  &constraints_slave);
}

static int pcm512x_dai_startup(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct pcm512x_priv *pcm512x = snd_soc_component_get_drvdata(component);

	switch (pcm512x->fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_CBP_CFP:
	case SND_SOC_DAIFMT_CBP_CFC:
		return pcm512x_dai_startup_master(substream, dai);

	case SND_SOC_DAIFMT_CBC_CFC:
		return pcm512x_dai_startup_slave(substream, dai);

	default:
		return -EINVAL;
	}
}

static int pcm512x_set_bias_level(struct snd_soc_component *component,
				  enum snd_soc_bias_level level)
{
	struct pcm512x_priv *pcm512x = dev_get_drvdata(component->dev);
	int ret;

	switch (level) {
	case SND_SOC_BIAS_ON:
	case SND_SOC_BIAS_PREPARE:
		break;

	case SND_SOC_BIAS_STANDBY:
		ret = regmap_update_bits(pcm512x->regmap, PCM512x_POWER,
					 PCM512x_RQST, 0);
		if (ret != 0) {
			dev_err(component->dev, "Failed to remove standby: %d\n",
				ret);
			return ret;
		}
		break;

	case SND_SOC_BIAS_OFF:
		ret = regmap_update_bits(pcm512x->regmap, PCM512x_POWER,
					 PCM512x_RQST, PCM512x_RQST);
		if (ret != 0) {
			dev_err(component->dev, "Failed to request standby: %d\n",
				ret);
			return ret;
		}
		break;
	}

	return 0;
}

static unsigned long pcm512x_find_sck(struct snd_soc_dai *dai,
				      unsigned long bclk_rate)
{
	struct device *dev = dai->dev;
	struct snd_soc_component *component = dai->component;
	struct pcm512x_priv *pcm512x = snd_soc_component_get_drvdata(component);
	unsigned long sck_rate;
	int pow2;

	/* 64 MHz <= pll_rate <= 100 MHz, VREF mode */
	/* 16 MHz <= sck_rate <=  25 MHz, VREF mode */

	/* select sck_rate as a multiple of bclk_rate but still with
	 * as many factors of 2 as possible, as that makes it easier
	 * to find a fast DAC rate
	 */
	pow2 = 1 << fls((pcm512x_pll_max(pcm512x) - 16000000) / bclk_rate);
	for (; pow2; pow2 >>= 1) {
		sck_rate = rounddown(pcm512x_pll_max(pcm512x),
				     bclk_rate * pow2);
		if (sck_rate >= 16000000)
			break;
	}
	if (!pow2) {
		dev_err(dev, "Impossible to generate a suitable SCK\n");
		return 0;
	}

	dev_dbg(dev, "sck_rate %lu\n", sck_rate);
	return sck_rate;
}

/* pll_rate = pllin_rate * R * J.D / P
 * 1 <= R <= 16
 * 1 <= J <= 63
 * 0 <= D <= 9999
 * 1 <= P <= 15
 * 64 MHz <= pll_rate <= 100 MHz
 * if D == 0
 *     1 MHz <= pllin_rate / P <= 20 MHz
 * else if D > 0
 *     6.667 MHz <= pllin_rate / P <= 20 MHz
 *     4 <= J <= 11
 *     R = 1
 */
static int pcm512x_find_pll_coeff(struct snd_soc_dai *dai,
				  unsigned long pllin_rate,
				  unsigned long pll_rate)
{
	struct device *dev = dai->dev;
	struct snd_soc_component *component = dai->component;
	struct pcm512x_priv *pcm512x = snd_soc_component_get_drvdata(component);
	unsigned long common;
	int R, J, D, P;
	unsigned long K; /* 10000 * J.D */
	unsigned long num;
	unsigned long den;

	common = gcd(pll_rate, pllin_rate);
	dev_dbg(dev, "pll %lu pllin %lu common %lu\n",
		pll_rate, pllin_rate, common);
	num = pll_rate / common;
	den = pllin_rate / common;

	/* pllin_rate / P (or here, den) cannot be greater than 20 MHz */
	if (pllin_rate / den > 20000000 && num < 8) {
		num *= DIV_ROUND_UP(pllin_rate / den, 20000000);
		den *= DIV_ROUND_UP(pllin_rate / den, 20000000);
	}
	dev_dbg(dev, "num / den = %lu / %lu\n", num, den);

	P = den;
	if (den <= 15 && num <= 16 * 63
	    && 1000000 <= pllin_rate / P && pllin_rate / P <= 20000000) {
		/* Try the case with D = 0 */
		D = 0;
		/* factor 'num' into J and R, such that R <= 16 and J <= 63 */
		for (R = 16; R; R--) {
			if (num % R)
				continue;
			J = num / R;
			if (J == 0 || J > 63)
				continue;

			dev_dbg(dev, "R * J / P = %d * %d / %d\n", R, J, P);
			pcm512x->real_pll = pll_rate;
			goto done;
		}
		/* no luck */
	}

	R = 1;

	if (num > 0xffffffffUL / 10000)
		goto fallback;

	/* Try to find an exact pll_rate using the D > 0 case */
	common = gcd(10000 * num, den);
	num = 10000 * num / common;
	den /= common;
	dev_dbg(dev, "num %lu den %lu common %lu\n", num, den, common);

	for (P = den; P <= 15; P++) {
		if (pllin_rate / P < 6667000 || 200000000 < pllin_rate / P)
			continue;
		if (num * P % den)
			continue;
		K = num * P / den;
		/* J == 12 is ok if D == 0 */
		if (K < 40000 || K > 120000)
			continue;

		J = K / 10000;
		D = K % 10000;
		dev_dbg(dev, "J.D / P = %d.%04d / %d\n", J, D, P);
		pcm512x->real_pll = pll_rate;
		goto done;
	}

	/* Fall back to an approximate pll_rate */

fallback:
	/* find smallest possible P */
	P = DIV_ROUND_UP(pllin_rate, 20000000);
	if (!P)
		P = 1;
	else if (P > 15) {
		dev_err(dev, "Need a slower clock as pll-input\n");
		return -EINVAL;
	}
	if (pllin_rate / P < 6667000) {
		dev_err(dev, "Need a faster clock as pll-input\n");
		return -EINVAL;
	}
	K = DIV_ROUND_CLOSEST_ULL(10000ULL * pll_rate * P, pllin_rate);
	if (K < 40000)
		K = 40000;
	/* J == 12 is ok if D == 0 */
	if (K > 120000)
		K = 120000;
	J = K / 10000;
	D = K % 10000;
	dev_dbg(dev, "J.D / P ~ %d.%04d / %d\n", J, D, P);
	pcm512x->real_pll = DIV_ROUND_DOWN_ULL((u64)K * pllin_rate, 10000 * P);

done:
	pcm512x->pll_r = R;
	pcm512x->pll_j = J;
	pcm512x->pll_d = D;
	pcm512x->pll_p = P;
	return 0;
}

static unsigned long pcm512x_pllin_dac_rate(struct snd_soc_dai *dai,
					    unsigned long osr_rate,
					    unsigned long pllin_rate)
{
	struct snd_soc_component *component = dai->component;
	struct pcm512x_priv *pcm512x = snd_soc_component_get_drvdata(component);
	unsigned long dac_rate;

	if (!pcm512x->pll_out)
		return 0; /* no PLL to bypass, force SCK as DAC input */

	if (pllin_rate % osr_rate)
		return 0; /* futile, quit early */

	/* run DAC no faster than 6144000 Hz */
	for (dac_rate = rounddown(pcm512x_dac_max(pcm512x, 6144000), osr_rate);
	     dac_rate;
	     dac_rate -= osr_rate) {

		if (pllin_rate / dac_rate > 128)
			return 0; /* DAC divider would be too big */

		if (!(pllin_rate % dac_rate))
			return dac_rate;

		dac_rate -= osr_rate;
	}

	return 0;
}

static int pcm512x_set_dividers(struct snd_soc_dai *dai,
				struct snd_pcm_hw_params *params)
{
	struct device *dev = dai->dev;
	struct snd_soc_component *component = dai->component;
	struct pcm512x_priv *pcm512x = snd_soc_component_get_drvdata(component);
	unsigned long pllin_rate = 0;
	unsigned long pll_rate;
	unsigned long sck_rate;
	unsigned long mck_rate;
	unsigned long bclk_rate;
	unsigned long sample_rate;
	unsigned long osr_rate;
	unsigned long dacsrc_rate;
	int bclk_div;
	int lrclk_div;
	int dsp_div;
	int dac_div;
	unsigned long dac_rate;
	int ncp_div;
	int osr_div;
	int ret;
	int idac;
	int fssp;
	int gpio;

	if (pcm512x->bclk_ratio > 0) {
		lrclk_div = pcm512x->bclk_ratio;
	} else {
		lrclk_div = snd_soc_params_to_frame_size(params);

		if (lrclk_div == 0) {
			dev_err(dev, "No LRCLK?\n");
			return -EINVAL;
		}
	}

	if (!pcm512x->pll_out) {
		sck_rate = clk_get_rate(pcm512x->sclk);
		bclk_rate = params_rate(params) * lrclk_div;
		bclk_div = DIV_ROUND_CLOSEST(sck_rate, bclk_rate);

		mck_rate = sck_rate;
	} else {
		ret = snd_soc_params_to_bclk(params);
		if (ret < 0) {
			dev_err(dev, "Failed to find suitable BCLK: %d\n", ret);
			return ret;
		}
		if (ret == 0) {
			dev_err(dev, "No BCLK?\n");
			return -EINVAL;
		}
		bclk_rate = ret;

		pllin_rate = clk_get_rate(pcm512x->sclk);

		sck_rate = pcm512x_find_sck(dai, bclk_rate);
		if (!sck_rate)
			return -EINVAL;
		pll_rate = 4 * sck_rate;

		ret = pcm512x_find_pll_coeff(dai, pllin_rate, pll_rate);
		if (ret != 0)
			return ret;

		ret = regmap_write(pcm512x->regmap,
				   PCM512x_PLL_COEFF_0, pcm512x->pll_p - 1);
		if (ret != 0) {
			dev_err(dev, "Failed to write PLL P: %d\n", ret);
			return ret;
		}

		ret = regmap_write(pcm512x->regmap,
				   PCM512x_PLL_COEFF_1, pcm512x->pll_j);
		if (ret != 0) {
			dev_err(dev, "Failed to write PLL J: %d\n", ret);
			return ret;
		}

		ret = regmap_write(pcm512x->regmap,
				   PCM512x_PLL_COEFF_2, pcm512x->pll_d >> 8);
		if (ret != 0) {
			dev_err(dev, "Failed to write PLL D msb: %d\n", ret);
			return ret;
		}

		ret = regmap_write(pcm512x->regmap,
				   PCM512x_PLL_COEFF_3, pcm512x->pll_d & 0xff);
		if (ret != 0) {
			dev_err(dev, "Failed to write PLL D lsb: %d\n", ret);
			return ret;
		}

		ret = regmap_write(pcm512x->regmap,
				   PCM512x_PLL_COEFF_4, pcm512x->pll_r - 1);
		if (ret != 0) {
			dev_err(dev, "Failed to write PLL R: %d\n", ret);
			return ret;
		}

		mck_rate = pcm512x->real_pll;

		bclk_div = DIV_ROUND_CLOSEST(sck_rate, bclk_rate);
	}

	if (bclk_div > 128) {
		dev_err(dev, "Failed to find BCLK divider\n");
		return -EINVAL;
	}

	/* the actual rate */
	sample_rate = sck_rate / bclk_div / lrclk_div;
	osr_rate = 16 * sample_rate;

	/* run DSP no faster than 50 MHz */
	dsp_div = mck_rate > pcm512x_dsp_max(pcm512x) ? 2 : 1;

	dac_rate = pcm512x_pllin_dac_rate(dai, osr_rate, pllin_rate);
	if (dac_rate) {
		/* the desired clock rate is "compatible" with the pll input
		 * clock, so use that clock as dac input instead of the pll
		 * output clock since the pll will introduce jitter and thus
		 * noise.
		 */
		dev_dbg(dev, "using pll input as dac input\n");
		ret = regmap_update_bits(pcm512x->regmap, PCM512x_DAC_REF,
					 PCM512x_SDAC, PCM512x_SDAC_GPIO);
		if (ret != 0) {
			dev_err(component->dev,
				"Failed to set gpio as dacref: %d\n", ret);
			return ret;
		}

		gpio = PCM512x_GREF_GPIO1 + pcm512x->pll_in - 1;
		ret = regmap_update_bits(pcm512x->regmap, PCM512x_GPIO_DACIN,
					 PCM512x_GREF, gpio);
		if (ret != 0) {
			dev_err(component->dev,
				"Failed to set gpio %d as dacin: %d\n",
				pcm512x->pll_in, ret);
			return ret;
		}

		dacsrc_rate = pllin_rate;
	} else {
		/* run DAC no faster than 6144000 Hz */
		unsigned long dac_mul = pcm512x_dac_max(pcm512x, 6144000)
			/ osr_rate;
		unsigned long sck_mul = sck_rate / osr_rate;

		for (; dac_mul; dac_mul--) {
			if (!(sck_mul % dac_mul))
				break;
		}
		if (!dac_mul) {
			dev_err(dev, "Failed to find DAC rate\n");
			return -EINVAL;
		}

		dac_rate = dac_mul * osr_rate;
		dev_dbg(dev, "dac_rate %lu sample_rate %lu\n",
			dac_rate, sample_rate);

		ret = regmap_update_bits(pcm512x->regmap, PCM512x_DAC_REF,
					 PCM512x_SDAC, PCM512x_SDAC_SCK);
		if (ret != 0) {
			dev_err(component->dev,
				"Failed to set sck as dacref: %d\n", ret);
			return ret;
		}

		dacsrc_rate = sck_rate;
	}

	osr_div = DIV_ROUND_CLOSEST(dac_rate, osr_rate);
	if (osr_div > 128) {
		dev_err(dev, "Failed to find OSR divider\n");
		return -EINVAL;
	}

	dac_div = DIV_ROUND_CLOSEST(dacsrc_rate, dac_rate);
	if (dac_div > 128) {
		dev_err(dev, "Failed to find DAC divider\n");
		return -EINVAL;
	}
	dac_rate = dacsrc_rate / dac_div;

	ncp_div = DIV_ROUND_CLOSEST(dac_rate,
				    pcm512x_ncp_target(pcm512x, dac_rate));
	if (ncp_div > 128 || dac_rate / ncp_div > 2048000) {
		/* run NCP no faster than 2048000 Hz, but why? */
		ncp_div = DIV_ROUND_UP(dac_rate, 2048000);
		if (ncp_div > 128) {
			dev_err(dev, "Failed to find NCP divider\n");
			return -EINVAL;
		}
	}

	idac = mck_rate / (dsp_div * sample_rate);

	ret = regmap_write(pcm512x->regmap, PCM512x_DSP_CLKDIV, dsp_div - 1);
	if (ret != 0) {
		dev_err(dev, "Failed to write DSP divider: %d\n", ret);
		return ret;
	}

	ret = regmap_write(pcm512x->regmap, PCM512x_DAC_CLKDIV, dac_div - 1);
	if (ret != 0) {
		dev_err(dev, "Failed to write DAC divider: %d\n", ret);
		return ret;
	}

	ret = regmap_write(pcm512x->regmap, PCM512x_NCP_CLKDIV, ncp_div - 1);
	if (ret != 0) {
		dev_err(dev, "Failed to write NCP divider: %d\n", ret);
		return ret;
	}

	ret = regmap_write(pcm512x->regmap, PCM512x_OSR_CLKDIV, osr_div - 1);
	if (ret != 0) {
		dev_err(dev, "Failed to write OSR divider: %d\n", ret);
		return ret;
	}

	ret = regmap_write(pcm512x->regmap,
			   PCM512x_MASTER_CLKDIV_1, bclk_div - 1);
	if (ret != 0) {
		dev_err(dev, "Failed to write BCLK divider: %d\n", ret);
		return ret;
	}

	ret = regmap_write(pcm512x->regmap,
			   PCM512x_MASTER_CLKDIV_2, lrclk_div - 1);
	if (ret != 0) {
		dev_err(dev, "Failed to write LRCLK divider: %d\n", ret);
		return ret;
	}

	ret = regmap_write(pcm512x->regmap, PCM512x_IDAC_1, idac >> 8);
	if (ret != 0) {
		dev_err(dev, "Failed to write IDAC msb divider: %d\n", ret);
		return ret;
	}

	ret = regmap_write(pcm512x->regmap, PCM512x_IDAC_2, idac & 0xff);
	if (ret != 0) {
		dev_err(dev, "Failed to write IDAC lsb divider: %d\n", ret);
		return ret;
	}

	if (sample_rate <= pcm512x_dac_max(pcm512x, 48000))
		fssp = PCM512x_FSSP_48KHZ;
	else if (sample_rate <= pcm512x_dac_max(pcm512x, 96000))
		fssp = PCM512x_FSSP_96KHZ;
	else if (sample_rate <= pcm512x_dac_max(pcm512x, 192000))
		fssp = PCM512x_FSSP_192KHZ;
	else
		fssp = PCM512x_FSSP_384KHZ;
	ret = regmap_update_bits(pcm512x->regmap, PCM512x_FS_SPEED_MODE,
				 PCM512x_FSSP, fssp);
	if (ret != 0) {
		dev_err(component->dev, "Failed to set fs speed: %d\n", ret);
		return ret;
	}

	dev_dbg(component->dev, "DSP divider %d\n", dsp_div);
	dev_dbg(component->dev, "DAC divider %d\n", dac_div);
	dev_dbg(component->dev, "NCP divider %d\n", ncp_div);
	dev_dbg(component->dev, "OSR divider %d\n", osr_div);
	dev_dbg(component->dev, "BCK divider %d\n", bclk_div);
	dev_dbg(component->dev, "LRCK divider %d\n", lrclk_div);
	dev_dbg(component->dev, "IDAC %d\n", idac);
	dev_dbg(component->dev, "1<<FSSP %d\n", 1 << fssp);

	return 0;
}

static int pcm512x_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct pcm512x_priv *pcm512x = snd_soc_component_get_drvdata(component);
	int alen;
	int gpio;
	int ret;

	dev_dbg(component->dev, "hw_params %u Hz, %u channels\n",
		params_rate(params),
		params_channels(params));

	switch (params_width(params)) {
	case 16:
		alen = PCM512x_ALEN_16;
		break;
	case 20:
		alen = PCM512x_ALEN_20;
		break;
	case 24:
		alen = PCM512x_ALEN_24;
		break;
	case 32:
		alen = PCM512x_ALEN_32;
		break;
	default:
		dev_err(component->dev, "Bad frame size: %d\n",
			params_width(params));
		return -EINVAL;
	}

	ret = regmap_update_bits(pcm512x->regmap, PCM512x_I2S_1,
				 PCM512x_ALEN, alen);
	if (ret != 0) {
		dev_err(component->dev, "Failed to set frame size: %d\n", ret);
		return ret;
	}

	if ((pcm512x->fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) ==
	    SND_SOC_DAIFMT_CBC_CFC) {
		ret = regmap_update_bits(pcm512x->regmap, PCM512x_ERROR_DETECT,
					 PCM512x_DCAS, 0);
		if (ret != 0) {
			dev_err(component->dev,
				"Failed to enable clock divider autoset: %d\n",
				ret);
			return ret;
		}
		goto skip_pll;
	}

	if (pcm512x->pll_out) {
		ret = regmap_write(pcm512x->regmap, PCM512x_FLEX_A, 0x11);
		if (ret != 0) {
			dev_err(component->dev, "Failed to set FLEX_A: %d\n", ret);
			return ret;
		}

		ret = regmap_write(pcm512x->regmap, PCM512x_FLEX_B, 0xff);
		if (ret != 0) {
			dev_err(component->dev, "Failed to set FLEX_B: %d\n", ret);
			return ret;
		}

		ret = regmap_update_bits(pcm512x->regmap, PCM512x_ERROR_DETECT,
					 PCM512x_IDFS | PCM512x_IDBK
					 | PCM512x_IDSK | PCM512x_IDCH
					 | PCM512x_IDCM | PCM512x_DCAS
					 | PCM512x_IPLK,
					 PCM512x_IDFS | PCM512x_IDBK
					 | PCM512x_IDSK | PCM512x_IDCH
					 | PCM512x_DCAS);
		if (ret != 0) {
			dev_err(component->dev,
				"Failed to ignore auto-clock failures: %d\n",
				ret);
			return ret;
		}
	} else {
		ret = regmap_update_bits(pcm512x->regmap, PCM512x_ERROR_DETECT,
					 PCM512x_IDFS | PCM512x_IDBK
					 | PCM512x_IDSK | PCM512x_IDCH
					 | PCM512x_IDCM | PCM512x_DCAS
					 | PCM512x_IPLK,
					 PCM512x_IDFS | PCM512x_IDBK
					 | PCM512x_IDSK | PCM512x_IDCH
					 | PCM512x_DCAS | PCM512x_IPLK);
		if (ret != 0) {
			dev_err(component->dev,
				"Failed to ignore auto-clock failures: %d\n",
				ret);
			return ret;
		}

		ret = regmap_update_bits(pcm512x->regmap, PCM512x_PLL_EN,
					 PCM512x_PLLE, 0);
		if (ret != 0) {
			dev_err(component->dev, "Failed to disable pll: %d\n", ret);
			return ret;
		}
	}

	ret = pcm512x_set_dividers(dai, params);
	if (ret != 0)
		return ret;

	if (pcm512x->pll_out) {
		ret = regmap_update_bits(pcm512x->regmap, PCM512x_PLL_REF,
					 PCM512x_SREF, PCM512x_SREF_GPIO);
		if (ret != 0) {
			dev_err(component->dev,
				"Failed to set gpio as pllref: %d\n", ret);
			return ret;
		}

		gpio = PCM512x_GREF_GPIO1 + pcm512x->pll_in - 1;
		ret = regmap_update_bits(pcm512x->regmap, PCM512x_GPIO_PLLIN,
					 PCM512x_GREF, gpio);
		if (ret != 0) {
			dev_err(component->dev,
				"Failed to set gpio %d as pllin: %d\n",
				pcm512x->pll_in, ret);
			return ret;
		}

		ret = regmap_update_bits(pcm512x->regmap, PCM512x_PLL_EN,
					 PCM512x_PLLE, PCM512x_PLLE);
		if (ret != 0) {
			dev_err(component->dev, "Failed to enable pll: %d\n", ret);
			return ret;
		}

		gpio = PCM512x_G1OE << (pcm512x->pll_out - 1);
		ret = regmap_update_bits(pcm512x->regmap, PCM512x_GPIO_EN,
					 gpio, gpio);
		if (ret != 0) {
			dev_err(component->dev, "Failed to enable gpio %d: %d\n",
				pcm512x->pll_out, ret);
			return ret;
		}

		gpio = PCM512x_GPIO_OUTPUT_1 + pcm512x->pll_out - 1;
		ret = regmap_update_bits(pcm512x->regmap, gpio,
					 PCM512x_GxSL, PCM512x_GxSL_PLLCK);
		if (ret != 0) {
			dev_err(component->dev, "Failed to output pll on %d: %d\n",
				ret, pcm512x->pll_out);
			return ret;
		}
	}

	ret = regmap_update_bits(pcm512x->regmap, PCM512x_SYNCHRONIZE,
				 PCM512x_RQSY, PCM512x_RQSY_HALT);
	if (ret != 0) {
		dev_err(component->dev, "Failed to halt clocks: %d\n", ret);
		return ret;
	}

	ret = regmap_update_bits(pcm512x->regmap, PCM512x_SYNCHRONIZE,
				 PCM512x_RQSY, PCM512x_RQSY_RESUME);
	if (ret != 0) {
		dev_err(component->dev, "Failed to resume clocks: %d\n", ret);
		return ret;
	}

skip_pll:
	return 0;
}

static int pcm512x_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *component = dai->component;
	struct pcm512x_priv *pcm512x = snd_soc_component_get_drvdata(component);
	int afmt;
	int offset = 0;
	int clock_output;
	int provider_mode;
	int ret;

	switch (fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_CBC_CFC:
		clock_output = 0;
		provider_mode = 0;
		break;
	case SND_SOC_DAIFMT_CBP_CFP:
		clock_output = PCM512x_BCKO | PCM512x_LRKO;
		provider_mode = PCM512x_RLRK | PCM512x_RBCK;
		break;
	case SND_SOC_DAIFMT_CBP_CFC:
		clock_output = PCM512x_BCKO;
		provider_mode = PCM512x_RBCK;
		break;
	default:
		return -EINVAL;
	}

	ret = regmap_update_bits(pcm512x->regmap, PCM512x_BCLK_LRCLK_CFG,
				 PCM512x_BCKP | PCM512x_BCKO | PCM512x_LRKO,
				 clock_output);
	if (ret != 0) {
		dev_err(component->dev, "Failed to enable clock output: %d\n", ret);
		return ret;
	}

	ret = regmap_update_bits(pcm512x->regmap, PCM512x_MASTER_MODE,
				 PCM512x_RLRK | PCM512x_RBCK,
				 provider_mode);
	if (ret != 0) {
		dev_err(component->dev, "Failed to enable provider mode: %d\n", ret);
		return ret;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		afmt = PCM512x_AFMT_I2S;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		afmt = PCM512x_AFMT_RTJ;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		afmt = PCM512x_AFMT_LTJ;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		offset = 1;
		fallthrough;
	case SND_SOC_DAIFMT_DSP_B:
		afmt = PCM512x_AFMT_DSP;
		break;
	default:
		dev_err(component->dev, "unsupported DAI format: 0x%x\n",
			pcm512x->fmt);
		return -EINVAL;
	}

	ret = regmap_update_bits(pcm512x->regmap, PCM512x_I2S_1,
				 PCM512x_AFMT, afmt);
	if (ret != 0) {
		dev_err(component->dev, "Failed to set data format: %d\n", ret);
		return ret;
	}

	ret = regmap_update_bits(pcm512x->regmap, PCM512x_I2S_2,
				 0xFF, offset);
	if (ret != 0) {
		dev_err(component->dev, "Failed to set data offset: %d\n", ret);
		return ret;
	}

	pcm512x->fmt = fmt;

	return 0;
}

static int pcm512x_set_bclk_ratio(struct snd_soc_dai *dai, unsigned int ratio)
{
	struct snd_soc_component *component = dai->component;
	struct pcm512x_priv *pcm512x = snd_soc_component_get_drvdata(component);

	if (ratio > 256)
		return -EINVAL;

	pcm512x->bclk_ratio = ratio;

	return 0;
}

static int pcm512x_mute(struct snd_soc_dai *dai, int mute, int direction)
{
	struct snd_soc_component *component = dai->component;
	struct pcm512x_priv *pcm512x = snd_soc_component_get_drvdata(component);
	int ret;
	unsigned int mute_det;

	mutex_lock(&pcm512x->mutex);

	if (mute) {
		pcm512x->mute |= 0x1;
		ret = regmap_update_bits(pcm512x->regmap, PCM512x_MUTE,
					 PCM512x_RQML | PCM512x_RQMR,
					 PCM512x_RQML | PCM512x_RQMR);
		if (ret != 0) {
			dev_err(component->dev,
				"Failed to set digital mute: %d\n", ret);
			goto unlock;
		}

		regmap_read_poll_timeout(pcm512x->regmap,
					 PCM512x_ANALOG_MUTE_DET,
					 mute_det, (mute_det & 0x3) == 0,
					 200, 10000);
	} else {
		pcm512x->mute &= ~0x1;
		ret = pcm512x_update_mute(pcm512x);
		if (ret != 0) {
			dev_err(component->dev,
				"Failed to update digital mute: %d\n", ret);
			goto unlock;
		}

		regmap_read_poll_timeout(pcm512x->regmap,
					 PCM512x_ANALOG_MUTE_DET,
					 mute_det,
					 (mute_det & 0x3)
					 == ((~pcm512x->mute >> 1) & 0x3),
					 200, 10000);
	}

unlock:
	mutex_unlock(&pcm512x->mutex);

	return ret;
}

static const struct snd_soc_dai_ops pcm512x_dai_ops = {
	.startup = pcm512x_dai_startup,
	.hw_params = pcm512x_hw_params,
	.set_fmt = pcm512x_set_fmt,
	.mute_stream = pcm512x_mute,
	.set_bclk_ratio = pcm512x_set_bclk_ratio,
	.no_capture_mute = 1,
};

static struct snd_soc_dai_driver pcm512x_dai = {
	.name = "pcm512x-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_CONTINUOUS,
		.rate_min = 8000,
		.rate_max = 384000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE |
			   SNDRV_PCM_FMTBIT_S24_LE |
			   SNDRV_PCM_FMTBIT_S32_LE
	},
	.ops = &pcm512x_dai_ops,
};

static const struct snd_soc_component_driver pcm512x_component_driver = {
	.set_bias_level		= pcm512x_set_bias_level,
	.controls		= pcm512x_controls,
	.num_controls		= ARRAY_SIZE(pcm512x_controls),
	.dapm_widgets		= pcm512x_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(pcm512x_dapm_widgets),
	.dapm_routes		= pcm512x_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(pcm512x_dapm_routes),
	.use_pmdown_time	= 1,
	.endianness		= 1,
};

static const struct regmap_range_cfg pcm512x_range = {
	.name = "Pages", .range_min = PCM512x_VIRT_BASE,
	.range_max = PCM512x_MAX_REGISTER,
	.selector_reg = PCM512x_PAGE,
	.selector_mask = 0xff,
	.window_start = 0, .window_len = 0x100,
};

const struct regmap_config pcm512x_regmap = {
	.reg_bits = 8,
	.val_bits = 8,

	.readable_reg = pcm512x_readable,
	.volatile_reg = pcm512x_volatile,

	.ranges = &pcm512x_range,
	.num_ranges = 1,

	.max_register = PCM512x_MAX_REGISTER,
	.reg_defaults = pcm512x_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(pcm512x_reg_defaults),
	.cache_type = REGCACHE_RBTREE,
};
EXPORT_SYMBOL_GPL(pcm512x_regmap);

int pcm512x_probe(struct device *dev, struct regmap *regmap)
{
	struct pcm512x_priv *pcm512x;
	int i, ret;

	pcm512x = devm_kzalloc(dev, sizeof(struct pcm512x_priv), GFP_KERNEL);
	if (!pcm512x)
		return -ENOMEM;

	mutex_init(&pcm512x->mutex);

	dev_set_drvdata(dev, pcm512x);
	pcm512x->regmap = regmap;

	for (i = 0; i < ARRAY_SIZE(pcm512x->supplies); i++)
		pcm512x->supplies[i].supply = pcm512x_supply_names[i];

	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(pcm512x->supplies),
				      pcm512x->supplies);
	if (ret != 0) {
		dev_err(dev, "Failed to get supplies: %d\n", ret);
		return ret;
	}

	pcm512x->supply_nb[0].notifier_call = pcm512x_regulator_event_0;
	pcm512x->supply_nb[1].notifier_call = pcm512x_regulator_event_1;
	pcm512x->supply_nb[2].notifier_call = pcm512x_regulator_event_2;

	for (i = 0; i < ARRAY_SIZE(pcm512x->supplies); i++) {
		ret = devm_regulator_register_notifier(
						pcm512x->supplies[i].consumer,
						&pcm512x->supply_nb[i]);
		if (ret != 0) {
			dev_err(dev,
				"Failed to register regulator notifier: %d\n",
				ret);
		}
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(pcm512x->supplies),
				    pcm512x->supplies);
	if (ret != 0) {
		dev_err(dev, "Failed to enable supplies: %d\n", ret);
		return ret;
	}

	/* Reset the device, verifying I/O in the process for I2C */
	ret = regmap_write(regmap, PCM512x_RESET,
			   PCM512x_RSTM | PCM512x_RSTR);
	if (ret != 0) {
		dev_err(dev, "Failed to reset device: %d\n", ret);
		goto err;
	}

	ret = regmap_write(regmap, PCM512x_RESET, 0);
	if (ret != 0) {
		dev_err(dev, "Failed to reset device: %d\n", ret);
		goto err;
	}

	pcm512x->sclk = devm_clk_get(dev, NULL);
	if (PTR_ERR(pcm512x->sclk) == -EPROBE_DEFER) {
		ret = -EPROBE_DEFER;
		goto err;
	}
	if (!IS_ERR(pcm512x->sclk)) {
		ret = clk_prepare_enable(pcm512x->sclk);
		if (ret != 0) {
			dev_err(dev, "Failed to enable SCLK: %d\n", ret);
			goto err;
		}
	}

	/* Default to standby mode */
	ret = regmap_update_bits(pcm512x->regmap, PCM512x_POWER,
				 PCM512x_RQST, PCM512x_RQST);
	if (ret != 0) {
		dev_err(dev, "Failed to request standby: %d\n",
			ret);
		goto err_clk;
	}

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

#ifdef CONFIG_OF
	if (dev->of_node) {
		const struct device_node *np = dev->of_node;
		u32 val;

		if (of_property_read_u32(np, "pll-in", &val) >= 0) {
			if (val > 6) {
				dev_err(dev, "Invalid pll-in\n");
				ret = -EINVAL;
				goto err_pm;
			}
			pcm512x->pll_in = val;
		}

		if (of_property_read_u32(np, "pll-out", &val) >= 0) {
			if (val > 6) {
				dev_err(dev, "Invalid pll-out\n");
				ret = -EINVAL;
				goto err_pm;
			}
			pcm512x->pll_out = val;
		}

		if (!pcm512x->pll_in != !pcm512x->pll_out) {
			dev_err(dev,
				"Error: both pll-in and pll-out, or none\n");
			ret = -EINVAL;
			goto err_pm;
		}
		if (pcm512x->pll_in && pcm512x->pll_in == pcm512x->pll_out) {
			dev_err(dev, "Error: pll-in == pll-out\n");
			ret = -EINVAL;
			goto err_pm;
		}
	}
#endif

	ret = devm_snd_soc_register_component(dev, &pcm512x_component_driver,
				    &pcm512x_dai, 1);
	if (ret != 0) {
		dev_err(dev, "Failed to register CODEC: %d\n", ret);
		goto err_pm;
	}

	return 0;

err_pm:
	pm_runtime_disable(dev);
err_clk:
	if (!IS_ERR(pcm512x->sclk))
		clk_disable_unprepare(pcm512x->sclk);
err:
	regulator_bulk_disable(ARRAY_SIZE(pcm512x->supplies),
				     pcm512x->supplies);
	return ret;
}
EXPORT_SYMBOL_GPL(pcm512x_probe);

void pcm512x_remove(struct device *dev)
{
	struct pcm512x_priv *pcm512x = dev_get_drvdata(dev);

	pm_runtime_disable(dev);
	if (!IS_ERR(pcm512x->sclk))
		clk_disable_unprepare(pcm512x->sclk);
	regulator_bulk_disable(ARRAY_SIZE(pcm512x->supplies),
			       pcm512x->supplies);
}
EXPORT_SYMBOL_GPL(pcm512x_remove);

#ifdef CONFIG_PM
static int pcm512x_suspend(struct device *dev)
{
	struct pcm512x_priv *pcm512x = dev_get_drvdata(dev);
	int ret;

	ret = regmap_update_bits(pcm512x->regmap, PCM512x_POWER,
				 PCM512x_RQPD, PCM512x_RQPD);
	if (ret != 0) {
		dev_err(dev, "Failed to request power down: %d\n", ret);
		return ret;
	}

	ret = regulator_bulk_disable(ARRAY_SIZE(pcm512x->supplies),
				     pcm512x->supplies);
	if (ret != 0) {
		dev_err(dev, "Failed to disable supplies: %d\n", ret);
		return ret;
	}

	if (!IS_ERR(pcm512x->sclk))
		clk_disable_unprepare(pcm512x->sclk);

	return 0;
}

static int pcm512x_resume(struct device *dev)
{
	struct pcm512x_priv *pcm512x = dev_get_drvdata(dev);
	int ret;

	if (!IS_ERR(pcm512x->sclk)) {
		ret = clk_prepare_enable(pcm512x->sclk);
		if (ret != 0) {
			dev_err(dev, "Failed to enable SCLK: %d\n", ret);
			return ret;
		}
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(pcm512x->supplies),
				    pcm512x->supplies);
	if (ret != 0) {
		dev_err(dev, "Failed to enable supplies: %d\n", ret);
		return ret;
	}

	regcache_cache_only(pcm512x->regmap, false);
	ret = regcache_sync(pcm512x->regmap);
	if (ret != 0) {
		dev_err(dev, "Failed to sync cache: %d\n", ret);
		return ret;
	}

	ret = regmap_update_bits(pcm512x->regmap, PCM512x_POWER,
				 PCM512x_RQPD, 0);
	if (ret != 0) {
		dev_err(dev, "Failed to remove power down: %d\n", ret);
		return ret;
	}

	return 0;
}
#endif

const struct dev_pm_ops pcm512x_pm_ops = {
	SET_RUNTIME_PM_OPS(pcm512x_suspend, pcm512x_resume, NULL)
};
EXPORT_SYMBOL_GPL(pcm512x_pm_ops);

MODULE_DESCRIPTION("ASoC PCM512x codec driver");
MODULE_AUTHOR("Mark Brown <broonie@kernel.org>");
MODULE_LICENSE("GPL v2");
