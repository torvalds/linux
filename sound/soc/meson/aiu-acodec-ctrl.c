// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2020 BayLibre, SAS.
// Author: Jerome Brunet <jbrunet@baylibre.com>

#include <linux/bitfield.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>

#include <dt-bindings/sound/meson-aiu.h>
#include "aiu.h"
#include "meson-codec-glue.h"

#define CTRL_DIN_EN			15
#define CTRL_CLK_INV			BIT(14)
#define CTRL_LRCLK_INV			BIT(13)
#define CTRL_I2S_IN_BCLK_SRC		BIT(11)
#define CTRL_DIN_LRCLK_SRC_SHIFT	6
#define CTRL_DIN_LRCLK_SRC		(0x3 << CTRL_DIN_LRCLK_SRC_SHIFT)
#define CTRL_BCLK_MCLK_SRC		GENMASK(5, 4)
#define CTRL_DIN_SKEW			GENMASK(3, 2)
#define CTRL_I2S_OUT_LANE_SRC		0

#define AIU_ACODEC_OUT_CHMAX		2

static const char * const aiu_acodec_ctrl_mux_texts[] = {
	"DISABLED", "I2S", "PCM",
};

static int aiu_acodec_ctrl_mux_put_enum(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_dapm_kcontrol_component(kcontrol);
	struct snd_soc_dapm_context *dapm =
		snd_soc_dapm_kcontrol_dapm(kcontrol);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int mux, changed;

	mux = snd_soc_enum_item_to_val(e, ucontrol->value.enumerated.item[0]);
	changed = snd_soc_component_test_bits(component, e->reg,
					      CTRL_DIN_LRCLK_SRC,
					      FIELD_PREP(CTRL_DIN_LRCLK_SRC,
							 mux));

	if (!changed)
		return 0;

	/* Force disconnect of the mux while updating */
	snd_soc_dapm_mux_update_power(dapm, kcontrol, 0, NULL, NULL);

	snd_soc_component_update_bits(component, e->reg,
				      CTRL_DIN_LRCLK_SRC |
				      CTRL_BCLK_MCLK_SRC,
				      FIELD_PREP(CTRL_DIN_LRCLK_SRC, mux) |
				      FIELD_PREP(CTRL_BCLK_MCLK_SRC, mux));

	snd_soc_dapm_mux_update_power(dapm, kcontrol, mux, e, NULL);

	return 0;
}

static SOC_ENUM_SINGLE_DECL(aiu_acodec_ctrl_mux_enum, AIU_ACODEC_CTRL,
			    CTRL_DIN_LRCLK_SRC_SHIFT,
			    aiu_acodec_ctrl_mux_texts);

static const struct snd_kcontrol_new aiu_acodec_ctrl_mux =
	SOC_DAPM_ENUM_EXT("ACodec Source", aiu_acodec_ctrl_mux_enum,
			  snd_soc_dapm_get_enum_double,
			  aiu_acodec_ctrl_mux_put_enum);

static const struct snd_kcontrol_new aiu_acodec_ctrl_out_enable =
	SOC_DAPM_SINGLE_AUTODISABLE("Switch", AIU_ACODEC_CTRL,
				    CTRL_DIN_EN, 1, 0);

static const struct snd_soc_dapm_widget aiu_acodec_ctrl_widgets[] = {
	SND_SOC_DAPM_MUX("ACODEC SRC", SND_SOC_NOPM, 0, 0,
			 &aiu_acodec_ctrl_mux),
	SND_SOC_DAPM_SWITCH("ACODEC OUT EN", SND_SOC_NOPM, 0, 0,
			    &aiu_acodec_ctrl_out_enable),
};

static int aiu_acodec_ctrl_input_hw_params(struct snd_pcm_substream *substream,
					   struct snd_pcm_hw_params *params,
					   struct snd_soc_dai *dai)
{
	struct meson_codec_glue_input *data;
	int ret;

	ret = meson_codec_glue_input_hw_params(substream, params, dai);
	if (ret)
		return ret;

	/* The glue will provide 1 lane out of the 4 to the output */
	data = meson_codec_glue_input_get_data(dai);
	data->params.channels_min = min_t(unsigned int, AIU_ACODEC_OUT_CHMAX,
					  data->params.channels_min);
	data->params.channels_max = min_t(unsigned int, AIU_ACODEC_OUT_CHMAX,
					  data->params.channels_max);

	return 0;
}

static const struct snd_soc_dai_ops aiu_acodec_ctrl_input_ops = {
	.hw_params	= aiu_acodec_ctrl_input_hw_params,
	.set_fmt	= meson_codec_glue_input_set_fmt,
};

static const struct snd_soc_dai_ops aiu_acodec_ctrl_output_ops = {
	.startup	= meson_codec_glue_output_startup,
};

#define AIU_ACODEC_CTRL_FORMATS					\
	(SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |	\
	 SNDRV_PCM_FMTBIT_S24_3LE | SNDRV_PCM_FMTBIT_S24_LE |	\
	 SNDRV_PCM_FMTBIT_S32_LE)

#define AIU_ACODEC_STREAM(xname, xsuffix, xchmax)		\
{								\
	.stream_name	= xname " " xsuffix,			\
	.channels_min	= 1,					\
	.channels_max	= (xchmax),				\
	.rate_min       = 5512,					\
	.rate_max	= 192000,				\
	.formats	= AIU_ACODEC_CTRL_FORMATS,		\
}

#define AIU_ACODEC_INPUT(xname) {				\
	.name = "ACODEC CTRL " xname,				\
	.playback = AIU_ACODEC_STREAM(xname, "Playback", 8),	\
	.ops = &aiu_acodec_ctrl_input_ops,			\
	.probe = meson_codec_glue_input_dai_probe,		\
	.remove = meson_codec_glue_input_dai_remove,		\
}

#define AIU_ACODEC_OUTPUT(xname) {				\
	.name = "ACODEC CTRL " xname,				\
	.capture = AIU_ACODEC_STREAM(xname, "Capture", AIU_ACODEC_OUT_CHMAX), \
	.ops = &aiu_acodec_ctrl_output_ops,			\
}

static struct snd_soc_dai_driver aiu_acodec_ctrl_dai_drv[] = {
	[CTRL_I2S] = AIU_ACODEC_INPUT("ACODEC I2S IN"),
	[CTRL_PCM] = AIU_ACODEC_INPUT("ACODEC PCM IN"),
	[CTRL_OUT] = AIU_ACODEC_OUTPUT("ACODEC OUT"),
};

static const struct snd_soc_dapm_route aiu_acodec_ctrl_routes[] = {
	{ "ACODEC SRC", "I2S", "ACODEC I2S IN Playback" },
	{ "ACODEC SRC", "PCM", "ACODEC PCM IN Playback" },
	{ "ACODEC OUT EN", "Switch", "ACODEC SRC" },
	{ "ACODEC OUT Capture", NULL, "ACODEC OUT EN" },
};

static const struct snd_kcontrol_new aiu_acodec_ctrl_controls[] = {
	SOC_SINGLE("ACODEC I2S Lane Select", AIU_ACODEC_CTRL,
		   CTRL_I2S_OUT_LANE_SRC, 3, 0),
};

static int aiu_acodec_of_xlate_dai_name(struct snd_soc_component *component,
					struct of_phandle_args *args,
					const char **dai_name)
{
	return aiu_of_xlate_dai_name(component, args, dai_name, AIU_ACODEC);
}

static int aiu_acodec_ctrl_component_probe(struct snd_soc_component *component)
{
	/*
	 * NOTE: Din Skew setting
	 * According to the documentation, the following update adds one delay
	 * to the din line. Without this, the output saturates. This happens
	 * regardless of the link format (i2s or left_j) so it is not clear what
	 * it actually does but it seems to be required
	 */
	snd_soc_component_update_bits(component, AIU_ACODEC_CTRL,
				      CTRL_DIN_SKEW,
				      FIELD_PREP(CTRL_DIN_SKEW, 2));

	return 0;
}

static const struct snd_soc_component_driver aiu_acodec_ctrl_component = {
	.name			= "AIU Internal DAC Codec Control",
	.probe			= aiu_acodec_ctrl_component_probe,
	.controls		= aiu_acodec_ctrl_controls,
	.num_controls		= ARRAY_SIZE(aiu_acodec_ctrl_controls),
	.dapm_widgets		= aiu_acodec_ctrl_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(aiu_acodec_ctrl_widgets),
	.dapm_routes		= aiu_acodec_ctrl_routes,
	.num_dapm_routes	= ARRAY_SIZE(aiu_acodec_ctrl_routes),
	.of_xlate_dai_name	= aiu_acodec_of_xlate_dai_name,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

int aiu_acodec_ctrl_register_component(struct device *dev)
{
	return snd_soc_register_component(dev, &aiu_acodec_ctrl_component,
					  aiu_acodec_ctrl_dai_drv,
					  ARRAY_SIZE(aiu_acodec_ctrl_dai_drv));
}
