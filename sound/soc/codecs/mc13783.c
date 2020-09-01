// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2008 Juergen Beisert, kernel@pengutronix.de
 * Copyright 2009 Sascha Hauer, s.hauer@pengutronix.de
 * Copyright 2012 Philippe Retornaz, philippe.retornaz@epfl.ch
 *
 * Initial development of this code was funded by
 * Phytec Messtechnik GmbH, https://www.phytec.de
 */
#include <linux/module.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/mfd/mc13xxx.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/soc-dapm.h>
#include <linux/regmap.h>

#include "mc13783.h"

#define AUDIO_RX0_ALSPEN		(1 << 5)
#define AUDIO_RX0_ALSPSEL		(1 << 7)
#define AUDIO_RX0_ADDCDC		(1 << 21)
#define AUDIO_RX0_ADDSTDC		(1 << 22)
#define AUDIO_RX0_ADDRXIN		(1 << 23)

#define AUDIO_RX1_PGARXEN		(1 << 0);
#define AUDIO_RX1_PGASTEN		(1 << 5)
#define AUDIO_RX1_ARXINEN		(1 << 10)

#define AUDIO_TX_AMC1REN		(1 << 5)
#define AUDIO_TX_AMC1LEN		(1 << 7)
#define AUDIO_TX_AMC2EN			(1 << 9)
#define AUDIO_TX_ATXINEN		(1 << 11)
#define AUDIO_TX_RXINREC		(1 << 13)

#define SSI_NETWORK_CDCTXRXSLOT(x)	(((x) & 0x3) << 2)
#define SSI_NETWORK_CDCTXSECSLOT(x)	(((x) & 0x3) << 4)
#define SSI_NETWORK_CDCRXSECSLOT(x)	(((x) & 0x3) << 6)
#define SSI_NETWORK_CDCRXSECGAIN(x)	(((x) & 0x3) << 8)
#define SSI_NETWORK_CDCSUMGAIN(x)	(1 << 10)
#define SSI_NETWORK_CDCFSDLY(x)		(1 << 11)
#define SSI_NETWORK_DAC_SLOTS_8		(1 << 12)
#define SSI_NETWORK_DAC_SLOTS_4		(2 << 12)
#define SSI_NETWORK_DAC_SLOTS_2		(3 << 12)
#define SSI_NETWORK_DAC_SLOT_MASK	(3 << 12)
#define SSI_NETWORK_DAC_RXSLOT_0_1	(0 << 14)
#define SSI_NETWORK_DAC_RXSLOT_2_3	(1 << 14)
#define SSI_NETWORK_DAC_RXSLOT_4_5	(2 << 14)
#define SSI_NETWORK_DAC_RXSLOT_6_7	(3 << 14)
#define SSI_NETWORK_DAC_RXSLOT_MASK	(3 << 14)
#define SSI_NETWORK_STDCRXSECSLOT(x)	(((x) & 0x3) << 16)
#define SSI_NETWORK_STDCRXSECGAIN(x)	(((x) & 0x3) << 18)
#define SSI_NETWORK_STDCSUMGAIN		(1 << 20)

/*
 * MC13783_AUDIO_CODEC and MC13783_AUDIO_DAC mostly share the same
 * register layout
 */
#define AUDIO_SSI_SEL			(1 << 0)
#define AUDIO_CLK_SEL			(1 << 1)
#define AUDIO_CSM			(1 << 2)
#define AUDIO_BCL_INV			(1 << 3)
#define AUDIO_CFS_INV			(1 << 4)
#define AUDIO_CFS(x)			(((x) & 0x3) << 5)
#define AUDIO_CLK(x)			(((x) & 0x7) << 7)
#define AUDIO_C_EN			(1 << 11)
#define AUDIO_C_CLK_EN			(1 << 12)
#define AUDIO_C_RESET			(1 << 15)

#define AUDIO_CODEC_CDCFS8K16K		(1 << 10)
#define AUDIO_DAC_CFS_DLY_B		(1 << 10)

struct mc13783_priv {
	struct mc13xxx *mc13xxx;
	struct regmap *regmap;

	enum mc13783_ssi_port adc_ssi_port;
	enum mc13783_ssi_port dac_ssi_port;
};

/* Mapping between sample rates and register value */
static unsigned int mc13783_rates[] = {
	8000, 11025, 12000, 16000,
	22050, 24000, 32000, 44100,
	48000, 64000, 96000
};

static int mc13783_pcm_hw_params_dac(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	unsigned int rate = params_rate(params);
	int i;

	for (i = 0; i < ARRAY_SIZE(mc13783_rates); i++) {
		if (rate == mc13783_rates[i]) {
			snd_soc_component_update_bits(component, MC13783_AUDIO_DAC,
					0xf << 17, i << 17);
			return 0;
		}
	}

	return -EINVAL;
}

static int mc13783_pcm_hw_params_codec(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	unsigned int rate = params_rate(params);
	unsigned int val;

	switch (rate) {
	case 8000:
		val = 0;
		break;
	case 16000:
		val = AUDIO_CODEC_CDCFS8K16K;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_component_update_bits(component, MC13783_AUDIO_CODEC, AUDIO_CODEC_CDCFS8K16K,
			val);

	return 0;
}

static int mc13783_pcm_hw_params_sync(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		return mc13783_pcm_hw_params_dac(substream, params, dai);
	else
		return mc13783_pcm_hw_params_codec(substream, params, dai);
}

static int mc13783_set_fmt(struct snd_soc_dai *dai, unsigned int fmt,
			unsigned int reg)
{
	struct snd_soc_component *component = dai->component;
	unsigned int val = 0;
	unsigned int mask = AUDIO_CFS(3) | AUDIO_BCL_INV | AUDIO_CFS_INV |
				AUDIO_CSM | AUDIO_C_CLK_EN | AUDIO_C_RESET;


	/* DAI mode */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		val |= AUDIO_CFS(2);
		break;
	case SND_SOC_DAIFMT_DSP_A:
		val |= AUDIO_CFS(1);
		break;
	default:
		return -EINVAL;
	}

	/* DAI clock inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		val |= AUDIO_BCL_INV;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		val |= AUDIO_BCL_INV | AUDIO_CFS_INV;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		break;
	case SND_SOC_DAIFMT_IB_IF:
		val |= AUDIO_CFS_INV;
		break;
	}

	/* DAI clock master masks */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		val |= AUDIO_C_CLK_EN;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		val |= AUDIO_CSM;
		break;
	case SND_SOC_DAIFMT_CBM_CFS:
	case SND_SOC_DAIFMT_CBS_CFM:
		return -EINVAL;
	}

	val |= AUDIO_C_RESET;

	snd_soc_component_update_bits(component, reg, mask, val);

	return 0;
}

static int mc13783_set_fmt_async(struct snd_soc_dai *dai, unsigned int fmt)
{
	if (dai->id == MC13783_ID_STEREO_DAC)
		return mc13783_set_fmt(dai, fmt, MC13783_AUDIO_DAC);
	else
		return mc13783_set_fmt(dai, fmt, MC13783_AUDIO_CODEC);
}

static int mc13783_set_fmt_sync(struct snd_soc_dai *dai, unsigned int fmt)
{
	int ret;

	ret = mc13783_set_fmt(dai, fmt, MC13783_AUDIO_DAC);
	if (ret)
		return ret;

	/*
	 * In synchronous mode force the voice codec into slave mode
	 * so that the clock / framesync from the stereo DAC is used
	 */
	fmt &= ~SND_SOC_DAIFMT_MASTER_MASK;
	fmt |= SND_SOC_DAIFMT_CBS_CFS;
	ret = mc13783_set_fmt(dai, fmt, MC13783_AUDIO_CODEC);

	return ret;
}

static int mc13783_sysclk[] = {
	13000000,
	15360000,
	16800000,
	-1,
	26000000,
	-1, /* 12000000, invalid for voice codec */
	-1, /* 3686400, invalid for voice codec */
	33600000,
};

static int mc13783_set_sysclk(struct snd_soc_dai *dai,
				  int clk_id, unsigned int freq, int dir,
				  unsigned int reg)
{
	struct snd_soc_component *component = dai->component;
	int clk;
	unsigned int val = 0;
	unsigned int mask = AUDIO_CLK(0x7) | AUDIO_CLK_SEL;

	for (clk = 0; clk < ARRAY_SIZE(mc13783_sysclk); clk++) {
		if (mc13783_sysclk[clk] < 0)
			continue;
		if (mc13783_sysclk[clk] == freq)
			break;
	}

	if (clk == ARRAY_SIZE(mc13783_sysclk))
		return -EINVAL;

	if (clk_id == MC13783_CLK_CLIB)
		val |= AUDIO_CLK_SEL;

	val |= AUDIO_CLK(clk);

	snd_soc_component_update_bits(component, reg, mask, val);

	return 0;
}

static int mc13783_set_sysclk_dac(struct snd_soc_dai *dai,
				  int clk_id, unsigned int freq, int dir)
{
	return mc13783_set_sysclk(dai, clk_id, freq, dir, MC13783_AUDIO_DAC);
}

static int mc13783_set_sysclk_codec(struct snd_soc_dai *dai,
				  int clk_id, unsigned int freq, int dir)
{
	return mc13783_set_sysclk(dai, clk_id, freq, dir, MC13783_AUDIO_CODEC);
}

static int mc13783_set_sysclk_sync(struct snd_soc_dai *dai,
				  int clk_id, unsigned int freq, int dir)
{
	int ret;

	ret = mc13783_set_sysclk(dai, clk_id, freq, dir, MC13783_AUDIO_DAC);
	if (ret)
		return ret;

	return mc13783_set_sysclk(dai, clk_id, freq, dir, MC13783_AUDIO_CODEC);
}

static int mc13783_set_tdm_slot_dac(struct snd_soc_dai *dai,
	unsigned int tx_mask, unsigned int rx_mask, int slots,
	int slot_width)
{
	struct snd_soc_component *component = dai->component;
	unsigned int val = 0;
	unsigned int mask = SSI_NETWORK_DAC_SLOT_MASK |
				SSI_NETWORK_DAC_RXSLOT_MASK;

	switch (slots) {
	case 2:
		val |= SSI_NETWORK_DAC_SLOTS_2;
		break;
	case 4:
		val |= SSI_NETWORK_DAC_SLOTS_4;
		break;
	case 8:
		val |= SSI_NETWORK_DAC_SLOTS_8;
		break;
	default:
		return -EINVAL;
	}

	switch (rx_mask) {
	case 0x03:
		val |= SSI_NETWORK_DAC_RXSLOT_0_1;
		break;
	case 0x0c:
		val |= SSI_NETWORK_DAC_RXSLOT_2_3;
		break;
	case 0x30:
		val |= SSI_NETWORK_DAC_RXSLOT_4_5;
		break;
	case 0xc0:
		val |= SSI_NETWORK_DAC_RXSLOT_6_7;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_component_update_bits(component, MC13783_SSI_NETWORK, mask, val);

	return 0;
}

static int mc13783_set_tdm_slot_codec(struct snd_soc_dai *dai,
	unsigned int tx_mask, unsigned int rx_mask, int slots,
	int slot_width)
{
	struct snd_soc_component *component = dai->component;
	unsigned int val = 0;
	unsigned int mask = 0x3f;

	if (slots != 4)
		return -EINVAL;

	if (tx_mask != 0x3)
		return -EINVAL;

	val |= (0x00 << 2);	/* primary timeslot RX/TX(?) is 0 */
	val |= (0x01 << 4);	/* secondary timeslot TX is 1 */

	snd_soc_component_update_bits(component, MC13783_SSI_NETWORK, mask, val);

	return 0;
}

static int mc13783_set_tdm_slot_sync(struct snd_soc_dai *dai,
	unsigned int tx_mask, unsigned int rx_mask, int slots,
	int slot_width)
{
	int ret;

	ret = mc13783_set_tdm_slot_dac(dai, tx_mask, rx_mask, slots,
			slot_width);
	if (ret)
		return ret;

	ret = mc13783_set_tdm_slot_codec(dai, tx_mask, rx_mask, slots,
			slot_width);

	return ret;
}

static const struct snd_kcontrol_new mc1l_amp_ctl =
	SOC_DAPM_SINGLE("Switch", MC13783_AUDIO_TX, 7, 1, 0);

static const struct snd_kcontrol_new mc1r_amp_ctl =
	SOC_DAPM_SINGLE("Switch", MC13783_AUDIO_TX, 5, 1, 0);

static const struct snd_kcontrol_new mc2_amp_ctl =
	SOC_DAPM_SINGLE("Switch", MC13783_AUDIO_TX, 9, 1, 0);

static const struct snd_kcontrol_new atx_amp_ctl =
	SOC_DAPM_SINGLE("Switch", MC13783_AUDIO_TX, 11, 1, 0);


/* Virtual mux. The chip does the input selection automatically
 * as soon as we enable one input. */
static const char * const adcl_enum_text[] = {
	"MC1L", "RXINL",
};

static SOC_ENUM_SINGLE_VIRT_DECL(adcl_enum, adcl_enum_text);

static const struct snd_kcontrol_new left_input_mux =
	SOC_DAPM_ENUM("Route", adcl_enum);

static const char * const adcr_enum_text[] = {
	"MC1R", "MC2", "RXINR", "TXIN",
};

static SOC_ENUM_SINGLE_VIRT_DECL(adcr_enum, adcr_enum_text);

static const struct snd_kcontrol_new right_input_mux =
	SOC_DAPM_ENUM("Route", adcr_enum);

static const struct snd_kcontrol_new samp_ctl =
	SOC_DAPM_SINGLE("Switch", MC13783_AUDIO_RX0, 3, 1, 0);

static const char * const speaker_amp_source_text[] = {
	"CODEC", "Right"
};
static SOC_ENUM_SINGLE_DECL(speaker_amp_source, MC13783_AUDIO_RX0, 4,
			    speaker_amp_source_text);
static const struct snd_kcontrol_new speaker_amp_source_mux =
	SOC_DAPM_ENUM("Speaker Amp Source MUX", speaker_amp_source);

static const char * const headset_amp_source_text[] = {
	"CODEC", "Mixer"
};

static SOC_ENUM_SINGLE_DECL(headset_amp_source, MC13783_AUDIO_RX0, 11,
			    headset_amp_source_text);
static const struct snd_kcontrol_new headset_amp_source_mux =
	SOC_DAPM_ENUM("Headset Amp Source MUX", headset_amp_source);

static const struct snd_kcontrol_new cdcout_ctl =
	SOC_DAPM_SINGLE("Switch", MC13783_AUDIO_RX0, 18, 1, 0);

static const struct snd_kcontrol_new adc_bypass_ctl =
	SOC_DAPM_SINGLE("Switch", MC13783_AUDIO_CODEC, 16, 1, 0);

static const struct snd_kcontrol_new lamp_ctl =
	SOC_DAPM_SINGLE("Switch", MC13783_AUDIO_RX0, 5, 1, 0);

static const struct snd_kcontrol_new hlamp_ctl =
	SOC_DAPM_SINGLE("Switch", MC13783_AUDIO_RX0, 10, 1, 0);

static const struct snd_kcontrol_new hramp_ctl =
	SOC_DAPM_SINGLE("Switch", MC13783_AUDIO_RX0, 9, 1, 0);

static const struct snd_kcontrol_new llamp_ctl =
	SOC_DAPM_SINGLE("Switch", MC13783_AUDIO_RX0, 16, 1, 0);

static const struct snd_kcontrol_new lramp_ctl =
	SOC_DAPM_SINGLE("Switch", MC13783_AUDIO_RX0, 15, 1, 0);

static const struct snd_soc_dapm_widget mc13783_dapm_widgets[] = {
/* Input */
	SND_SOC_DAPM_INPUT("MC1LIN"),
	SND_SOC_DAPM_INPUT("MC1RIN"),
	SND_SOC_DAPM_INPUT("MC2IN"),
	SND_SOC_DAPM_INPUT("RXINR"),
	SND_SOC_DAPM_INPUT("RXINL"),
	SND_SOC_DAPM_INPUT("TXIN"),

	SND_SOC_DAPM_SUPPLY("MC1 Bias", MC13783_AUDIO_TX, 0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("MC2 Bias", MC13783_AUDIO_TX, 1, 0, NULL, 0),

	SND_SOC_DAPM_SWITCH("MC1L Amp", MC13783_AUDIO_TX, 7, 0, &mc1l_amp_ctl),
	SND_SOC_DAPM_SWITCH("MC1R Amp", MC13783_AUDIO_TX, 5, 0, &mc1r_amp_ctl),
	SND_SOC_DAPM_SWITCH("MC2 Amp", MC13783_AUDIO_TX, 9, 0, &mc2_amp_ctl),
	SND_SOC_DAPM_SWITCH("TXIN Amp", MC13783_AUDIO_TX, 11, 0, &atx_amp_ctl),

	SND_SOC_DAPM_MUX("PGA Left Input Mux", SND_SOC_NOPM, 0, 0,
			      &left_input_mux),
	SND_SOC_DAPM_MUX("PGA Right Input Mux", SND_SOC_NOPM, 0, 0,
			      &right_input_mux),

	SND_SOC_DAPM_MUX("Speaker Amp Source MUX", SND_SOC_NOPM, 0, 0,
			 &speaker_amp_source_mux),

	SND_SOC_DAPM_MUX("Headset Amp Source MUX", SND_SOC_NOPM, 0, 0,
			 &headset_amp_source_mux),

	SND_SOC_DAPM_PGA("PGA Left Input", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("PGA Right Input", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_ADC("ADC", "Capture", MC13783_AUDIO_CODEC, 11, 0),
	SND_SOC_DAPM_SUPPLY("ADC_Reset", MC13783_AUDIO_CODEC, 15, 0, NULL, 0),

	SND_SOC_DAPM_PGA("Voice CODEC PGA", MC13783_AUDIO_RX1, 0, 0, NULL, 0),
	SND_SOC_DAPM_SWITCH("Voice CODEC Bypass", MC13783_AUDIO_CODEC, 16, 0,
			&adc_bypass_ctl),

/* Output */
	SND_SOC_DAPM_SUPPLY("DAC_E", MC13783_AUDIO_DAC, 11, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DAC_Reset", MC13783_AUDIO_DAC, 15, 0, NULL, 0),
	SND_SOC_DAPM_OUTPUT("RXOUTL"),
	SND_SOC_DAPM_OUTPUT("RXOUTR"),
	SND_SOC_DAPM_OUTPUT("HSL"),
	SND_SOC_DAPM_OUTPUT("HSR"),
	SND_SOC_DAPM_OUTPUT("LSPL"),
	SND_SOC_DAPM_OUTPUT("LSP"),
	SND_SOC_DAPM_OUTPUT("SP"),
	SND_SOC_DAPM_OUTPUT("CDCOUT"),

	SND_SOC_DAPM_SWITCH("CDCOUT Switch", MC13783_AUDIO_RX0, 18, 0,
			&cdcout_ctl),
	SND_SOC_DAPM_SWITCH("Speaker Amp Switch", MC13783_AUDIO_RX0, 3, 0,
			&samp_ctl),
	SND_SOC_DAPM_SWITCH("Loudspeaker Amp", SND_SOC_NOPM, 0, 0, &lamp_ctl),
	SND_SOC_DAPM_SWITCH("Headset Amp Left", MC13783_AUDIO_RX0, 10, 0,
			&hlamp_ctl),
	SND_SOC_DAPM_SWITCH("Headset Amp Right", MC13783_AUDIO_RX0, 9, 0,
			&hramp_ctl),
	SND_SOC_DAPM_SWITCH("Line out Amp Left", MC13783_AUDIO_RX0, 16, 0,
			&llamp_ctl),
	SND_SOC_DAPM_SWITCH("Line out Amp Right", MC13783_AUDIO_RX0, 15, 0,
			&lramp_ctl),
	SND_SOC_DAPM_DAC("DAC", "Playback", MC13783_AUDIO_RX0, 22, 0),
	SND_SOC_DAPM_PGA("DAC PGA", MC13783_AUDIO_RX1, 5, 0, NULL, 0),
};

static struct snd_soc_dapm_route mc13783_routes[] = {
/* Input */
	{ "MC1L Amp", NULL, "MC1LIN"},
	{ "MC1R Amp", NULL, "MC1RIN" },
	{ "MC2 Amp", NULL, "MC2IN" },
	{ "TXIN Amp", NULL, "TXIN"},

	{ "PGA Left Input Mux", "MC1L", "MC1L Amp" },
	{ "PGA Left Input Mux", "RXINL", "RXINL"},
	{ "PGA Right Input Mux", "MC1R", "MC1R Amp" },
	{ "PGA Right Input Mux", "MC2",  "MC2 Amp"},
	{ "PGA Right Input Mux", "TXIN", "TXIN Amp"},
	{ "PGA Right Input Mux", "RXINR", "RXINR"},

	{ "PGA Left Input", NULL, "PGA Left Input Mux"},
	{ "PGA Right Input", NULL, "PGA Right Input Mux"},

	{ "ADC", NULL, "PGA Left Input"},
	{ "ADC", NULL, "PGA Right Input"},
	{ "ADC", NULL, "ADC_Reset"},

	{ "Voice CODEC PGA", "Voice CODEC Bypass", "ADC" },

	{ "Speaker Amp Source MUX", "CODEC", "Voice CODEC PGA"},
	{ "Speaker Amp Source MUX", "Right", "DAC PGA"},

	{ "Headset Amp Source MUX", "CODEC", "Voice CODEC PGA"},
	{ "Headset Amp Source MUX", "Mixer", "DAC PGA"},

/* Output */
	{ "HSL", NULL, "Headset Amp Left" },
	{ "HSR", NULL, "Headset Amp Right"},
	{ "RXOUTL", NULL, "Line out Amp Left"},
	{ "RXOUTR", NULL, "Line out Amp Right"},
	{ "SP", "Speaker Amp Switch", "Speaker Amp Source MUX"},
	{ "LSP", "Loudspeaker Amp", "Speaker Amp Source MUX"},
	{ "HSL", "Headset Amp Left", "Headset Amp Source MUX"},
	{ "HSR", "Headset Amp Right", "Headset Amp Source MUX"},
	{ "Line out Amp Left", NULL, "DAC PGA"},
	{ "Line out Amp Right", NULL, "DAC PGA"},
	{ "DAC PGA", NULL, "DAC"},
	{ "DAC", NULL, "DAC_E"},
	{ "CDCOUT", "CDCOUT Switch", "Voice CODEC PGA"},
};

static const char * const mc13783_3d_mixer[] = {"Stereo", "Phase Mix",
						"Mono", "Mono Mix"};

static SOC_ENUM_SINGLE_DECL(mc13783_enum_3d_mixer,
			    MC13783_AUDIO_RX1, 16,
			    mc13783_3d_mixer);

static struct snd_kcontrol_new mc13783_control_list[] = {
	SOC_SINGLE("Loudspeaker enable", MC13783_AUDIO_RX0, 5, 1, 0),
	SOC_SINGLE("PCM Playback Volume", MC13783_AUDIO_RX1, 6, 15, 0),
	SOC_SINGLE("PCM Playback Switch", MC13783_AUDIO_RX1, 5, 1, 0),
	SOC_DOUBLE("PCM Capture Volume", MC13783_AUDIO_TX, 19, 14, 31, 0),
	SOC_ENUM("3D Control", mc13783_enum_3d_mixer),

	SOC_SINGLE("CDCOUT Switch", MC13783_AUDIO_RX0, 18, 1, 0),
	SOC_SINGLE("Earpiece Amp Switch", MC13783_AUDIO_RX0, 3, 1, 0),
	SOC_DOUBLE("Headset Amp Switch", MC13783_AUDIO_RX0, 10, 9, 1, 0),
	SOC_DOUBLE("Line out Amp Switch", MC13783_AUDIO_RX0, 16, 15, 1, 0),

	SOC_SINGLE("PCM Capture Mixin Switch", MC13783_AUDIO_RX0, 22, 1, 0),
	SOC_SINGLE("Line in Capture Mixin Switch", MC13783_AUDIO_RX0, 23, 1, 0),

	SOC_SINGLE("CODEC Capture Volume", MC13783_AUDIO_RX1, 1, 15, 0),
	SOC_SINGLE("CODEC Capture Mixin Switch", MC13783_AUDIO_RX0, 21, 1, 0),

	SOC_SINGLE("Line in Capture Volume", MC13783_AUDIO_RX1, 12, 15, 0),
	SOC_SINGLE("Line in Capture Switch", MC13783_AUDIO_RX1, 10, 1, 0),

	SOC_SINGLE("MC1 Capture Bias Switch", MC13783_AUDIO_TX, 0, 1, 0),
	SOC_SINGLE("MC2 Capture Bias Switch", MC13783_AUDIO_TX, 1, 1, 0),
};

static int mc13783_probe(struct snd_soc_component *component)
{
	struct mc13783_priv *priv = snd_soc_component_get_drvdata(component);

	snd_soc_component_init_regmap(component,
				  dev_get_regmap(component->dev->parent, NULL));

	/* these are the reset values */
	mc13xxx_reg_write(priv->mc13xxx, MC13783_AUDIO_RX0, 0x25893);
	mc13xxx_reg_write(priv->mc13xxx, MC13783_AUDIO_RX1, 0x00d35A);
	mc13xxx_reg_write(priv->mc13xxx, MC13783_AUDIO_TX, 0x420000);
	mc13xxx_reg_write(priv->mc13xxx, MC13783_SSI_NETWORK, 0x013060);
	mc13xxx_reg_write(priv->mc13xxx, MC13783_AUDIO_CODEC, 0x180027);
	mc13xxx_reg_write(priv->mc13xxx, MC13783_AUDIO_DAC, 0x0e0004);

	if (priv->adc_ssi_port == MC13783_SSI1_PORT)
		mc13xxx_reg_rmw(priv->mc13xxx, MC13783_AUDIO_CODEC,
				AUDIO_SSI_SEL, 0);
	else
		mc13xxx_reg_rmw(priv->mc13xxx, MC13783_AUDIO_CODEC,
				AUDIO_SSI_SEL, AUDIO_SSI_SEL);

	if (priv->dac_ssi_port == MC13783_SSI1_PORT)
		mc13xxx_reg_rmw(priv->mc13xxx, MC13783_AUDIO_DAC,
				AUDIO_SSI_SEL, 0);
	else
		mc13xxx_reg_rmw(priv->mc13xxx, MC13783_AUDIO_DAC,
				AUDIO_SSI_SEL, AUDIO_SSI_SEL);

	return 0;
}

static void mc13783_remove(struct snd_soc_component *component)
{
	struct mc13783_priv *priv = snd_soc_component_get_drvdata(component);

	/* Make sure VAUDIOON is off */
	mc13xxx_reg_rmw(priv->mc13xxx, MC13783_AUDIO_RX0, 0x3, 0);
}

#define MC13783_RATES_RECORD (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000)

#define MC13783_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
	SNDRV_PCM_FMTBIT_S24_LE)

static const struct snd_soc_dai_ops mc13783_ops_dac = {
	.hw_params	= mc13783_pcm_hw_params_dac,
	.set_fmt	= mc13783_set_fmt_async,
	.set_sysclk	= mc13783_set_sysclk_dac,
	.set_tdm_slot	= mc13783_set_tdm_slot_dac,
};

static const struct snd_soc_dai_ops mc13783_ops_codec = {
	.hw_params	= mc13783_pcm_hw_params_codec,
	.set_fmt	= mc13783_set_fmt_async,
	.set_sysclk	= mc13783_set_sysclk_codec,
	.set_tdm_slot	= mc13783_set_tdm_slot_codec,
};

/*
 * The mc13783 has two SSI ports, both of them can be routed either
 * to the voice codec or the stereo DAC. When two different SSI ports
 * are used for the voice codec and the stereo DAC we can do different
 * formats and sysclock settings for playback and capture
 * (mc13783-hifi-playback and mc13783-hifi-capture). Using the same port
 * forces us to use symmetric rates (mc13783-hifi).
 */
static struct snd_soc_dai_driver mc13783_dai_async[] = {
	{
		.name = "mc13783-hifi-playback",
		.id = MC13783_ID_STEREO_DAC,
		.playback = {
			.stream_name = "Playback",
			.channels_min = 2,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_96000,
			.formats = MC13783_FORMATS,
		},
		.ops = &mc13783_ops_dac,
	}, {
		.name = "mc13783-hifi-capture",
		.id = MC13783_ID_STEREO_CODEC,
		.capture = {
			.stream_name = "Capture",
			.channels_min = 2,
			.channels_max = 2,
			.rates = MC13783_RATES_RECORD,
			.formats = MC13783_FORMATS,
		},
		.ops = &mc13783_ops_codec,
	},
};

static const struct snd_soc_dai_ops mc13783_ops_sync = {
	.hw_params	= mc13783_pcm_hw_params_sync,
	.set_fmt	= mc13783_set_fmt_sync,
	.set_sysclk	= mc13783_set_sysclk_sync,
	.set_tdm_slot	= mc13783_set_tdm_slot_sync,
};

static struct snd_soc_dai_driver mc13783_dai_sync[] = {
	{
		.name = "mc13783-hifi",
		.id = MC13783_ID_SYNC,
		.playback = {
			.stream_name = "Playback",
			.channels_min = 2,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_96000,
			.formats = MC13783_FORMATS,
		},
		.capture = {
			.stream_name = "Capture",
			.channels_min = 2,
			.channels_max = 2,
			.rates = MC13783_RATES_RECORD,
			.formats = MC13783_FORMATS,
		},
		.ops = &mc13783_ops_sync,
		.symmetric_rates = 1,
	}
};

static const struct snd_soc_component_driver soc_component_dev_mc13783 = {
	.probe			= mc13783_probe,
	.remove			= mc13783_remove,
	.controls		= mc13783_control_list,
	.num_controls		= ARRAY_SIZE(mc13783_control_list),
	.dapm_widgets		= mc13783_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(mc13783_dapm_widgets),
	.dapm_routes		= mc13783_routes,
	.num_dapm_routes	= ARRAY_SIZE(mc13783_routes),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static int __init mc13783_codec_probe(struct platform_device *pdev)
{
	struct mc13783_priv *priv;
	struct mc13xxx_codec_platform_data *pdata = pdev->dev.platform_data;
	struct device_node *np;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	if (pdata) {
		priv->adc_ssi_port = pdata->adc_ssi_port;
		priv->dac_ssi_port = pdata->dac_ssi_port;
	} else {
		np = of_get_child_by_name(pdev->dev.parent->of_node, "codec");
		if (!np)
			return -ENOSYS;

		ret = of_property_read_u32(np, "adc-port", &priv->adc_ssi_port);
		if (ret) {
			of_node_put(np);
			return ret;
		}

		ret = of_property_read_u32(np, "dac-port", &priv->dac_ssi_port);
		if (ret) {
			of_node_put(np);
			return ret;
		}

		of_node_put(np);
	}

	dev_set_drvdata(&pdev->dev, priv);
	priv->mc13xxx = dev_get_drvdata(pdev->dev.parent);

	if (priv->adc_ssi_port == priv->dac_ssi_port)
		ret = devm_snd_soc_register_component(&pdev->dev, &soc_component_dev_mc13783,
			mc13783_dai_sync, ARRAY_SIZE(mc13783_dai_sync));
	else
		ret = devm_snd_soc_register_component(&pdev->dev, &soc_component_dev_mc13783,
			mc13783_dai_async, ARRAY_SIZE(mc13783_dai_async));

	return ret;
}

static int mc13783_codec_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver mc13783_codec_driver = {
	.driver = {
		.name	= "mc13783-codec",
	},
	.remove = mc13783_codec_remove,
};
module_platform_driver_probe(mc13783_codec_driver, mc13783_codec_probe);

MODULE_DESCRIPTION("ASoC MC13783 driver");
MODULE_AUTHOR("Sascha Hauer, Pengutronix <s.hauer@pengutronix.de>");
MODULE_AUTHOR("Philippe Retornaz <philippe.retornaz@epfl.ch>");
MODULE_LICENSE("GPL");
