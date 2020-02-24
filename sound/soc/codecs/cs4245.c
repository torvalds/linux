// SPDX-License-Identifier: GPL-2.0-only
/*
 * cs4245.c -- CS4245 ALSA SoC audio driver
 *
 * Author: Rezzonics <rezzonics@gmail.com>
 *
 * cs4245.c -- CS4245 ALSA SoC audio driver
 *
 * Notes:
 *
 * Based on cs4265.c ALSA SoC audio driver
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include "cs4245.h"

struct cs4245_private {
	struct regmap *regmap;
	struct gpio_desc *reset_gpio;
	u8 format;
	u32 sysclk;
};

static const struct reg_default cs4245_reg_defaults[] = {
	{ CS4245_PWRCTL, 0x0F },  // HW default is 0x01
	{ CS4245_DAC_CTL, 0x08 },
	{ CS4245_ADC_CTL, 0x00 },
	{ CS4245_MCLK_FREQ, 0x00 },
	{ CS4245_SIG_SEL, 0x40 },
	{ CS4245_CHB_PGA_CTL, 0x00 },
	{ CS4245_CHA_PGA_CTL, 0x00 },
	{ CS4245_ADC_CTL2, 0x19 },
	{ CS4245_DAC_CHA_VOL, 0x00 },
	{ CS4245_DAC_CHB_VOL, 0x00 },
	{ CS4245_DAC_CTL2, 0xC0 },
	{ CS4245_INT_MASK, 0x00 },
	{ CS4245_STATUS_MODE_MSB, 0x00 },
	{ CS4245_STATUS_MODE_LSB, 0x00 },
};

static bool cs4245_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CS4245_CHIP_ID ... CS4245_MAX_REGISTER:
		return true;
	default:
		return false;
	}
}

static bool cs4245_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CS4245_INT_STATUS:
		return true;
	default:
		return false;
	}
}

/* PGA scale -12.0 to +12.0dB in 0.5dB steps => -24 to 24 */
/* Original cs4265 scale goes from -12 to 0dB
static DECLARE_TLV_DB_SCALE(pga_tlv, -1200, 50, 0); 
*/
static DECLARE_TLV_DB_SCALE(pga_tlv, -1200, 50, 1200);

/* DAC VOL scale 0 to -127.5dB in 0.5dB steps => 0 to 255 */
static DECLARE_TLV_DB_SCALE(dac_tlv, -12750, 50, 0);

/* Removed from CS4245: No SDIN Digital input mux on Sig Sel[5] */
/*
static const char * const digital_input_mux_text[] = {
	"SDIN1", "SDIN2"
};
static SOC_ENUM_SINGLE_DECL(digital_input_mux_enum, CS4245_SIG_SEL, 5,
		digital_input_mux_text);
*/

/* Added to CS4245: AUX Analog Output mux on Sig Sel[5] */ 
static const char * const aux_analog_output_mux_text[] = {
	"HI_IMP", "DAC", "PGA", "RSV"
};

static SOC_ENUM_SINGLE_DECL(aux_analog_output_mux_enum, CS4245_SIG_SEL, 5,
		aux_analog_output_mux_text);

static const struct snd_kcontrol_new aux_analog_output_mux =
	SOC_DAPM_ENUM("AUX Analog Output Mux", aux_analog_output_mux_enum);

static const char * const mic_linein_text[] = {
	"MIC", "LINEIN1", "LINEIN2", "LINEIN3", "LINEIN4", "LINEIN5", "LINEIN6", "RSV"
};

static SOC_ENUM_SINGLE_DECL(mic_linein_enum, CS4245_ADC_CTL2, 0,
		mic_linein_text);

/* No SPDIF on CS4245 */
/*
static const char * const cam_mode_text[] = {
	"One Byte", "Two Byte"
};

static SOC_ENUM_SINGLE_DECL(cam_mode_enum, CS4245_SPDIF_CTL1, 5,
		cam_mode_text);

static const char * const cam_mono_stereo_text[] = {
	"Stereo", "Mono"
};

static SOC_ENUM_SINGLE_DECL(spdif_mono_stereo_enum, CS4245_SPDIF_CTL2, 2,
		cam_mono_stereo_text);

static const char * const mono_select_text[] = {
	"Channel A", "Channel B"
};

static SOC_ENUM_SINGLE_DECL(spdif_mono_select_enum, CS4245_SPDIF_CTL2, 0,
		mono_select_text);
*/

static const struct snd_kcontrol_new mic_linein_mux =
	SOC_DAPM_ENUM("ADC Input Capture Mux", mic_linein_enum);

static const struct snd_kcontrol_new loopback_ctl =
	SOC_DAPM_SINGLE("Loopback Switch", CS4245_SIG_SEL, 1, 1, 0);

/*
static const struct snd_kcontrol_new spdif_switch =
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 0, 0);
*/
/*Use DAC widget not DAC switch */
/*
static const struct snd_kcontrol_new dac_switch =
	SOC_DAPM_SINGLE("Switch", CS4245_PWRCTL, 1, 1, 0);
*/

static const struct snd_kcontrol_new cs4245_snd_controls[] = {

//Replaced min, max vakues 0x28, 0x30 by -24, 24
	SOC_DOUBLE_R_SX_TLV("PGA Volume", CS4245_CHA_PGA_CTL,
			      CS4245_CHB_PGA_CTL, 0, -24, 24, pga_tlv), 
	SOC_DOUBLE_R_TLV("DAC Volume", CS4245_DAC_CHA_VOL,
		      CS4245_DAC_CHB_VOL, 0, 255, 0, dac_tlv), // 0xFF, 1
	SOC_SINGLE("De-emp 44.1kHz Switch", CS4245_DAC_CTL, 1,
				1, 0),
	SOC_SINGLE("DAC INV Switch", CS4245_DAC_CTL2, 5,
				1, 0),
	SOC_SINGLE("DAC Zero Cross Switch", CS4245_DAC_CTL2, 6,
				1, 0),
	SOC_SINGLE("DAC Soft Ramp Switch", CS4245_DAC_CTL2, 7,
				1, 0),
	SOC_SINGLE("ADC HPF Switch", CS4245_ADC_CTL, 1,
				1, 0),
//	SOC_SINGLE("ADC Zero Cross Switch", CS4245_ADC_CTL2, 3,
//				1, 1),
//	SOC_SINGLE("ADC Soft Ramp Switch", CS4245_ADC_CTL2, 7,
//				1, 0),
//	SOC_SINGLE("PGA Zero", CS4245_ADC_CTL2, 3,
//				1, 1),
//	SOC_SINGLE("PGA Soft Ramp", CS4245_ADC_CTL2, 4,
//				1, 0),
//	SOC_SINGLE("E to F Buffer Disable Switch", CS4245_SPDIF_CTL1,
//				6, 1, 0),
//	SOC_ENUM("C Data Access", cam_mode_enum),
//	SOC_SINGLE("SPDIF Switch", CS4245_SPDIF_CTL2, 5, 1, 1),
//	SOC_SINGLE("Validity Bit Control Switch", CS4245_SPDIF_CTL2,
//				3, 1, 0),
//	SOC_ENUM("SPDIF Mono/Stereo", spdif_mono_stereo_enum),
//	SOC_SINGLE("MMTLR Data Switch", CS4245_SPDIF_CTL2, 0, 1, 0),
//	SOC_ENUM("Mono Channel Select", spdif_mono_select_enum),
//	SND_SOC_BYTES("C Data Buffer", CS4245_C_DATA_BUFF, 24),
};

static const struct snd_soc_dapm_widget cs4245_dapm_widgets[] = {

	SND_SOC_DAPM_INPUT("MICL"),
	SND_SOC_DAPM_INPUT("MICR"),
	SND_SOC_DAPM_INPUT("LINEIN1L"),
	SND_SOC_DAPM_INPUT("LINEIN1R"),
	SND_SOC_DAPM_INPUT("LINEIN2L"),
	SND_SOC_DAPM_INPUT("LINEIN2R"),
	SND_SOC_DAPM_INPUT("LINEIN3L"),
	SND_SOC_DAPM_INPUT("LINEIN3R"),
	SND_SOC_DAPM_INPUT("LINEIN4L"),
	SND_SOC_DAPM_INPUT("LINEIN4R"),
	SND_SOC_DAPM_INPUT("LINEIN5L"),
	SND_SOC_DAPM_INPUT("LINEIN5R"),
	SND_SOC_DAPM_INPUT("LINEIN6L"),
	SND_SOC_DAPM_INPUT("LINEIN6R"),

	SND_SOC_DAPM_MUX("ADC Mux", SND_SOC_NOPM, 0, 0, &mic_linein_mux),

	SND_SOC_DAPM_PGA("PGA", CS4245_PWRCTL, 3,
			1, NULL, 0),

	SND_SOC_DAPM_ADC("ADC", NULL, CS4245_PWRCTL, 2, 1),

	SND_SOC_DAPM_AIF_IN("DIN1", NULL,  0,
			SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_DAC("DAC", NULL, CS4245_PWRCTL, 1, 1),

	SND_SOC_DAPM_AIF_OUT("DOUT", NULL,  0,
			SND_SOC_NOPM, 0, 0),

//	SND_SOC_DAPM_MIXER("SDIN1 Input Mixer", SND_SOC_NOPM, 0, 0, NULL, 0),
//	SND_SOC_DAPM_MIXER("SDIN2 Input Mixer", SND_SOC_NOPM, 0, 0, NULL, 0),
//	SND_SOC_DAPM_MIXER("SPDIF Transmitter", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_MUX("AUX Output Mux", SND_SOC_NOPM,
			 0, 0, &aux_analog_output_mux),

	SND_SOC_DAPM_SWITCH("Loopback", SND_SOC_NOPM, 0, 0,
			&loopback_ctl),
//	SND_SOC_DAPM_SWITCH("SPDIF", SND_SOC_NOPM, 0, 0,
//			&spdif_switch),
//	SND_SOC_DAPM_SWITCH("DAC", CS4245_PWRCTL, 1, 1,
//			&dac_switch),

//	SND_SOC_DAPM_AIF_IN("DIN2", NULL,  0,
//			SND_SOC_NOPM, 0, 0),
//	SND_SOC_DAPM_AIF_IN("TXIN", NULL,  0,
//			CS4245_SPDIF_CTL2, 5, 1),

//	SND_SOC_DAPM_AIF_OUT("SPDIFOUT", NULL,  0,
//			SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_OUTPUT("LINEOUTL"),
	SND_SOC_DAPM_OUTPUT("LINEOUTR"),
	SND_SOC_DAPM_OUTPUT("AUXOUTL"),
	SND_SOC_DAPM_OUTPUT("AUXOUTR"),

};

static const struct snd_soc_dapm_route cs4245_audio_map[] = {

	{"DIN1", NULL, "DAI1 Playback"},
//	{"DIN1", NULL, "DAI2 Playback"},
//	{"SDIN1 Input Mixer", NULL, "DIN1"},
//	{"SDIN2 Input Mixer", NULL, "DIN2"},
//	{"Input Mux", "SDIN1", "SDIN1 Input Mixer"},
//	{"Input Mux", "SDIN2", "SDIN2 Input Mixer"},
	{"DAC", NULL, "DIN1"},
//	{"SPDIF", "Switch", "Input Mux"},
	{"LINEOUTL", NULL, "DAC"},
	{"LINEOUTR", NULL, "DAC"},
	{"Aux Output Mux", NULL, "DAC"},
	{"Aux Output Mux", NULL, "PGA"},
	{"AUXOUTL", NULL, "Aux Output Mux"},
	{"AUXOUTR", NULL, "Aux Output Mux"},
//	{"SPDIFOUT", NULL, "SPDIF"},

	{"ADC Mux", NULL, "MICL"},
	{"ADC Mux", NULL, "MICR"},
//	{"ADC Mux", "MIC", "Pre-amp MIC"},
	{"ADC Mux", NULL, "LINEIN1L"},
	{"ADC Mux", NULL, "LINEIN1R"},
	{"ADC Mux", NULL, "LINEIN2L"},
	{"ADC Mux", NULL, "LINEIN2R"},
	{"ADC Mux", NULL, "LINEIN3L"},
	{"ADC Mux", NULL, "LINEIN3R"},
	{"ADC Mux", NULL, "LINEIN4L"},
	{"ADC Mux", NULL, "LINEIN4R"},
	{"ADC Mux", NULL, "LINEIN5L"},
	{"ADC Mux", NULL, "LINEIN5R"},
	{"ADC Mux", NULL, "LINEIN6L"},
	{"ADC Mux", NULL, "LINEIN6R"},
	{"PGA", NULL, "ADC Mux"},
	{"ADC", NULL, "PGA"},
	{"DOUT", NULL, "ADC"},
	{"DAI1 Capture", NULL, "DOUT"},
	{"DAI2 Capture", NULL, "DOUT"},

	/* Loopback */
	{"DAC", "Loopback", "ADC"},
};

struct cs4245_clk_para {
	u32 mclk;
	u32 rate;
	u8 fm_mode; /* values 1, 2, or 4 */
	u8 mclkdiv;
};

static const struct cs4245_clk_para clk_map_table[] = {
	/*32k*/
	{8192000, 32000, 0, 0},
	{12288000, 32000, 0, 1},
	{16384000, 32000, 0, 2},
	{24576000, 32000, 0, 3},
	{32768000, 32000, 0, 4},

	/*44.1k*/
	{11289600, 44100, 0, 0},
	{16934400, 44100, 0, 1},
	{22579200, 44100, 0, 2},
	{33868800, 44100, 0, 3},
	{45158400, 44100, 0, 4},

	/*48k*/
	{12288000, 48000, 0, 0},
	{18432000, 48000, 0, 1},
	{24576000, 48000, 0, 2},
	{36864000, 48000, 0, 3},
	{49152000, 48000, 0, 4},

	/*64k*/
	{8192000, 64000, 1, 0},
	{12288000, 64000, 1, 1},
	{16934400, 64000, 1, 2},
	{24576000, 64000, 1, 3},
	{32768000, 64000, 1, 4},

	/* 88.2k */
	{11289600, 88200, 1, 0},
	{16934400, 88200, 1, 1},
	{22579200, 88200, 1, 2},
	{33868800, 88200, 1, 3},
	{45158400, 88200, 1, 4},

	/* 96k */
	{12288000, 96000, 1, 0},
	{18432000, 96000, 1, 1},
	{24576000, 96000, 1, 2},
	{36864000, 96000, 1, 3},
	{49152000, 96000, 1, 4},

	/* 128k */
	{8192000, 128000, 2, 0},
	{12288000, 128000, 2, 1},
	{16934400, 128000, 2, 2},
	{24576000, 128000, 2, 3},
	{32768000, 128000, 2, 4},

	/* 176.4k */
	{11289600, 176400, 2, 0},
	{16934400, 176400, 2, 1},
	{22579200, 176400, 2, 2},
	{33868800, 176400, 2, 3},
	{49152000, 176400, 2, 4},

	/* 192k */
	{12288000, 192000, 2, 0},
	{18432000, 192000, 2, 1},
	{24576000, 192000, 2, 2},
	{36864000, 192000, 2, 3},
	{49152000, 192000, 2, 4},
};

static int cs4245_get_clk_index(int mclk, int rate)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(clk_map_table); i++) {
		if (clk_map_table[i].rate == rate &&
				clk_map_table[i].mclk == mclk)
			return i;
	}
	return -EINVAL;
}

static int cs4245_set_sysclk(struct snd_soc_dai *codec_dai, int clk_id,
			unsigned int freq, int dir)
{
	struct snd_soc_component *component = codec_dai->component;
	struct cs4245_private *cs4245 = snd_soc_component_get_drvdata(component);
	int i;

	if (clk_id != 0) {
		dev_err(component->dev, "Invalid clk_id %d\n", clk_id);
		return -EINVAL;
	}
	for (i = 0; i < ARRAY_SIZE(clk_map_table); i++) {
		if (clk_map_table[i].mclk == freq) {
			cs4245->sysclk = freq;
			return 0;
		}
	}
	cs4245->sysclk = 0;
	dev_err(component->dev, "Invalid freq parameter %d\n", freq);
	return -EINVAL;
}

static int cs4245_set_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt)
{
	struct snd_soc_component *component = codec_dai->component;
	struct cs4245_private *cs4245 = snd_soc_component_get_drvdata(component);
	u8 iface = 0;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		snd_soc_component_update_bits(component, CS4245_ADC_CTL,
				CS4245_ADC_MASTER,
				CS4245_ADC_MASTER);
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		snd_soc_component_update_bits(component, CS4245_ADC_CTL,
				CS4245_ADC_MASTER,
				0);
		break;
	default:
		return -EINVAL;
	}

	 /* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		iface |= SND_SOC_DAIFMT_I2S;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		iface |= SND_SOC_DAIFMT_RIGHT_J;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		iface |= SND_SOC_DAIFMT_LEFT_J;
		break;
	default:
		return -EINVAL;
	}

	cs4245->format = iface;
	return 0;
}

static int cs4245_digital_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_component *component = dai->component;

	if (mute) {
		snd_soc_component_update_bits(component, CS4245_DAC_CTL,
			CS4245_DAC_CTL_MUTE,
			CS4245_DAC_CTL_MUTE);
/*
		snd_soc_component_update_bits(component, CS4245_SPDIF_CTL2,
			CS4245_SPDIF_CTL2_MUTE,
			CS4245_SPDIF_CTL2_MUTE);
*/
	} else {
		snd_soc_component_update_bits(component, CS4245_DAC_CTL,
			CS4245_DAC_CTL_MUTE,
			0);
/*
		snd_soc_component_update_bits(component, CS4245_SPDIF_CTL2,
			CS4245_SPDIF_CTL2_MUTE,
			0);
*/
	}
	return 0;
}

static int cs4245_pcm_hw_params(struct snd_pcm_substream *substream,
				     struct snd_pcm_hw_params *params,
				     struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct cs4245_private *cs4245 = snd_soc_component_get_drvdata(component);
	int index;

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE &&
		((cs4245->format & SND_SOC_DAIFMT_FORMAT_MASK)
		== SND_SOC_DAIFMT_RIGHT_J))
		return -EINVAL;

	index = cs4245_get_clk_index(cs4245->sysclk, params_rate(params));
	if (index >= 0) {
		snd_soc_component_update_bits(component, CS4245_ADC_CTL,
			CS4245_ADC_FM, clk_map_table[index].fm_mode << 6);
		snd_soc_component_update_bits(component, CS4245_MCLK_FREQ,
			CS4245_MCLK_FREQ_MASK,
			clk_map_table[index].mclkdiv << 4);

	} else {
		dev_err(component->dev, "can't get correct mclk\n");
		return -EINVAL;
	}

	switch (cs4245->format & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		snd_soc_component_update_bits(component, CS4245_DAC_CTL,
			CS4245_DAC_CTL_DIF, (1 << 4));
		snd_soc_component_update_bits(component, CS4245_ADC_CTL,
			CS4245_ADC_DIF, (1 << 4));
//		snd_soc_component_update_bits(component, CS4245_SPDIF_CTL2,
//			CS4245_SPDIF_CTL2_DIF, (1 << 6));
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		if (params_width(params) == 16) {
			snd_soc_component_update_bits(component, CS4245_DAC_CTL,
				CS4245_DAC_CTL_DIF, (2 << 4));
//			snd_soc_component_update_bits(component, CS4245_SPDIF_CTL2,
//				CS4245_SPDIF_CTL2_DIF, (2 << 6));
		} else {
			snd_soc_component_update_bits(component, CS4245_DAC_CTL,
				CS4245_DAC_CTL_DIF, (3 << 4));
//			snd_soc_component_update_bits(component, CS4245_SPDIF_CTL2,
//				CS4245_SPDIF_CTL2_DIF, (3 << 6));
		}
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		snd_soc_component_update_bits(component, CS4245_DAC_CTL,
			CS4245_DAC_CTL_DIF, 0);
		snd_soc_component_update_bits(component, CS4245_ADC_CTL,
			CS4245_ADC_DIF, 0);
//		snd_soc_component_update_bits(component, CS4245_SPDIF_CTL2,
//			CS4245_SPDIF_CTL2_DIF, 0);

		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int cs4245_set_bias_level(struct snd_soc_component *component,
					enum snd_soc_bias_level level)
{
	switch (level) {
	case SND_SOC_BIAS_ON:
		break;
	case SND_SOC_BIAS_PREPARE:
		snd_soc_component_update_bits(component, CS4245_PWRCTL,
			CS4245_PWRCTL_PDN, 0);
		break;
	case SND_SOC_BIAS_STANDBY:
		snd_soc_component_update_bits(component, CS4245_PWRCTL,
			CS4245_PWRCTL_PDN,
			CS4245_PWRCTL_PDN);
		break;
	case SND_SOC_BIAS_OFF:
		snd_soc_component_update_bits(component, CS4245_PWRCTL,
			CS4245_PWRCTL_PDN,
			CS4245_PWRCTL_PDN);
		break;
	}
	return 0;
}

#define CS4245_RATES (SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 | \
			SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_64000 | \
			SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000 | \
			SNDRV_PCM_RATE_176400 | SNDRV_PCM_RATE_192000)

#define CS4245_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_U16_LE | \
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_U24_LE | \
			SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_U32_LE)

static const struct snd_soc_dai_ops cs4245_ops = {
	.hw_params	= cs4245_pcm_hw_params,
	.digital_mute	= cs4245_digital_mute,
	.set_fmt	= cs4245_set_fmt,
	.set_sysclk	= cs4245_set_sysclk,
};

static struct snd_soc_dai_driver cs4245_dai[] = {
	{
		.name = "cs4245-dai",
		.playback = {
			.stream_name = "I2S Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = CS4245_RATES,
			.formats = CS4245_FORMATS,
		},
		.capture = {
			.stream_name = "I2S Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = CS4245_RATES,
			.formats = CS4245_FORMATS,
		},
		.ops = &cs4245_ops,
	}
};

static const struct snd_soc_component_driver soc_component_cs4245 = {
	.set_bias_level		= cs4245_set_bias_level,
	.controls		= cs4245_snd_controls,
	.num_controls		= ARRAY_SIZE(cs4245_snd_controls),
	.dapm_widgets		= cs4245_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(cs4245_dapm_widgets),
	.dapm_routes		= cs4245_audio_map,
	.num_dapm_routes	= ARRAY_SIZE(cs4245_audio_map),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static const struct regmap_config cs4245_regmap = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = CS4245_MAX_REGISTER,
	.reg_defaults = cs4245_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(cs4245_reg_defaults),
	.readable_reg = cs4245_readable_register,
	.volatile_reg = cs4245_volatile_register,
	.cache_type = REGCACHE_RBTREE,
};

static int cs4245_i2c_probe(struct i2c_client *i2c_client,
			     const struct i2c_device_id *id)
{
	struct cs4245_private *cs4245;
	int ret = 0;
	unsigned int devid = 0;
	unsigned int reg;

	cs4245 = devm_kzalloc(&i2c_client->dev, sizeof(struct cs4245_private),
			       GFP_KERNEL);
	if (cs4245 == NULL)
		return -ENOMEM;

	cs4245->regmap = devm_regmap_init_i2c(i2c_client, &cs4245_regmap);
	if (IS_ERR(cs4245->regmap)) {
		ret = PTR_ERR(cs4245->regmap);
		dev_err(&i2c_client->dev, "regmap_init() failed: %d\n", ret);
		return ret;
	}
// Temp: Reset on Odroid C2 is GPIOX.21 => gpio491
	cs4245->reset_gpio = devm_gpiod_get_optional(&i2c_client->dev,
		"reset", GPIOD_OUT_LOW);
	if (IS_ERR(cs4245->reset_gpio))
		return PTR_ERR(cs4245->reset_gpio);

	if (cs4245->reset_gpio) {
		mdelay(1);
		gpiod_set_value_cansleep(cs4245->reset_gpio, 1);
	}

	i2c_set_clientdata(i2c_client, cs4245);

	ret = regmap_read(cs4245->regmap, CS4245_CHIP_ID, &reg);
	devid = reg & CS4245_CHIP_ID_MASK;
	if (devid != CS4245_CHIP_ID_VAL) {
		ret = -ENODEV;
		dev_err(&i2c_client->dev,
			"CS4245 Device ID (%X). Expected %X\n",
			devid, CS4245_CHIP_ID);
		return ret;
	}
	dev_info(&i2c_client->dev,
		"CS4245 Version %x\n",
			reg & CS4245_REV_ID_MASK);

	regmap_write(cs4245->regmap, CS4245_PWRCTL, 0x0F);

	ret = devm_snd_soc_register_component(&i2c_client->dev,
			&soc_component_cs4245, cs4245_dai,
			ARRAY_SIZE(cs4245_dai));
	return ret;
}

static const struct of_device_id cs4245_of_match[] = {
	{ .compatible = "cirrus,cs4245", },
	{ }
};
MODULE_DEVICE_TABLE(of, cs4245_of_match);

static const struct i2c_device_id cs4245_id[] = {
	{ "cs4245", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, cs4245_id);

static struct i2c_driver cs4245_i2c_driver = {
	.driver = {
		.name = "cs4245",
		.of_match_table = cs4245_of_match,
	},
	.id_table = cs4245_id,
	.probe =    cs4245_i2c_probe,
};

module_i2c_driver(cs4245_i2c_driver);

MODULE_DESCRIPTION("ASoC CS4245 driver");
MODULE_AUTHOR("Rezzonics, <rezzonics@gmail.com>");
MODULE_LICENSE("GPL");
