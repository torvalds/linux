// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2019 BayLibre, SAS.
// Author: Jerome Brunet <jbrunet@baylibre.com>

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <sound/pcm_params.h>
#include <linux/regmap.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>

#include <dt-bindings/sound/meson-g12a-tohdmitx.h>

#define G12A_TOHDMITX_DRV_NAME "g12a-tohdmitx"

#define TOHDMITX_CTRL0			0x0
#define  CTRL0_ENABLE_SHIFT		31
#define  CTRL0_I2S_DAT_SEL		GENMASK(13, 12)
#define  CTRL0_I2S_LRCLK_SEL		GENMASK(9, 8)
#define  CTRL0_I2S_BLK_CAP_INV		BIT(7)
#define  CTRL0_I2S_BCLK_O_INV		BIT(6)
#define  CTRL0_I2S_BCLK_SEL		GENMASK(5, 4)
#define  CTRL0_SPDIF_CLK_CAP_INV	BIT(3)
#define  CTRL0_SPDIF_CLK_O_INV		BIT(2)
#define  CTRL0_SPDIF_SEL		BIT(1)
#define  CTRL0_SPDIF_CLK_SEL		BIT(0)

struct g12a_tohdmitx_input {
	struct snd_soc_pcm_stream params;
	unsigned int fmt;
};

static struct snd_soc_dapm_widget *
g12a_tohdmitx_get_input(struct snd_soc_dapm_widget *w)
{
	struct snd_soc_dapm_path *p = NULL;
	struct snd_soc_dapm_widget *in;

	snd_soc_dapm_widget_for_each_source_path(w, p) {
		if (!p->connect)
			continue;

		/* Check that we still are in the same component */
		if (snd_soc_dapm_to_component(w->dapm) !=
		    snd_soc_dapm_to_component(p->source->dapm))
			continue;

		if (p->source->id == snd_soc_dapm_dai_in)
			return p->source;

		in = g12a_tohdmitx_get_input(p->source);
		if (in)
			return in;
	}

	return NULL;
}

static struct g12a_tohdmitx_input *
g12a_tohdmitx_get_input_data(struct snd_soc_dapm_widget *w)
{
	struct snd_soc_dapm_widget *in =
		g12a_tohdmitx_get_input(w);
	struct snd_soc_dai *dai;

	if (WARN_ON(!in))
		return NULL;

	dai = in->priv;

	return dai->playback_dma_data;
}

static const char * const g12a_tohdmitx_i2s_mux_texts[] = {
	"I2S A", "I2S B", "I2S C",
};

static SOC_ENUM_SINGLE_EXT_DECL(g12a_tohdmitx_i2s_mux_enum,
				g12a_tohdmitx_i2s_mux_texts);

static int g12a_tohdmitx_get_input_val(struct snd_soc_component *component,
				       unsigned int mask)
{
	unsigned int val;

	snd_soc_component_read(component, TOHDMITX_CTRL0, &val);
	return (val & mask) >> __ffs(mask);
}

static int g12a_tohdmitx_i2s_mux_get_enum(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_dapm_kcontrol_component(kcontrol);

	ucontrol->value.enumerated.item[0] =
		g12a_tohdmitx_get_input_val(component, CTRL0_I2S_DAT_SEL);

	return 0;
}

static int g12a_tohdmitx_i2s_mux_put_enum(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_dapm_kcontrol_component(kcontrol);
	struct snd_soc_dapm_context *dapm =
		snd_soc_dapm_kcontrol_dapm(kcontrol);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int mux = ucontrol->value.enumerated.item[0];
	unsigned int val = g12a_tohdmitx_get_input_val(component,
						       CTRL0_I2S_DAT_SEL);

	/* Force disconnect of the mux while updating */
	if (val != mux)
		snd_soc_dapm_mux_update_power(dapm, kcontrol, 0, NULL, NULL);

	snd_soc_component_update_bits(component, TOHDMITX_CTRL0,
				      CTRL0_I2S_DAT_SEL |
				      CTRL0_I2S_LRCLK_SEL |
				      CTRL0_I2S_BCLK_SEL,
				      FIELD_PREP(CTRL0_I2S_DAT_SEL, mux) |
				      FIELD_PREP(CTRL0_I2S_LRCLK_SEL, mux) |
				      FIELD_PREP(CTRL0_I2S_BCLK_SEL, mux));

	snd_soc_dapm_mux_update_power(dapm, kcontrol, mux, e, NULL);

	return 0;
}

static const struct snd_kcontrol_new g12a_tohdmitx_i2s_mux =
	SOC_DAPM_ENUM_EXT("I2S Source", g12a_tohdmitx_i2s_mux_enum,
			  g12a_tohdmitx_i2s_mux_get_enum,
			  g12a_tohdmitx_i2s_mux_put_enum);

static const char * const g12a_tohdmitx_spdif_mux_texts[] = {
	"SPDIF A", "SPDIF B",
};

static SOC_ENUM_SINGLE_EXT_DECL(g12a_tohdmitx_spdif_mux_enum,
				g12a_tohdmitx_spdif_mux_texts);

static int g12a_tohdmitx_spdif_mux_get_enum(struct snd_kcontrol *kcontrol,
					    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_dapm_kcontrol_component(kcontrol);

	ucontrol->value.enumerated.item[0] =
		g12a_tohdmitx_get_input_val(component, CTRL0_SPDIF_SEL);

	return 0;
}

static int g12a_tohdmitx_spdif_mux_put_enum(struct snd_kcontrol *kcontrol,
					    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_dapm_kcontrol_component(kcontrol);
	struct snd_soc_dapm_context *dapm =
		snd_soc_dapm_kcontrol_dapm(kcontrol);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int mux = ucontrol->value.enumerated.item[0];
	unsigned int val = g12a_tohdmitx_get_input_val(component,
						       CTRL0_SPDIF_SEL);

	/* Force disconnect of the mux while updating */
	if (val != mux)
		snd_soc_dapm_mux_update_power(dapm, kcontrol, 0, NULL, NULL);

	snd_soc_component_update_bits(component, TOHDMITX_CTRL0,
				      CTRL0_SPDIF_SEL |
				      CTRL0_SPDIF_CLK_SEL,
				      FIELD_PREP(CTRL0_SPDIF_SEL, mux) |
				      FIELD_PREP(CTRL0_SPDIF_CLK_SEL, mux));

	snd_soc_dapm_mux_update_power(dapm, kcontrol, mux, e, NULL);

	return 0;
}

static const struct snd_kcontrol_new g12a_tohdmitx_spdif_mux =
	SOC_DAPM_ENUM_EXT("SPDIF Source", g12a_tohdmitx_spdif_mux_enum,
			  g12a_tohdmitx_spdif_mux_get_enum,
			  g12a_tohdmitx_spdif_mux_put_enum);

static const struct snd_kcontrol_new g12a_tohdmitx_out_enable =
	SOC_DAPM_SINGLE_AUTODISABLE("Switch", TOHDMITX_CTRL0,
				    CTRL0_ENABLE_SHIFT, 1, 0);

static const struct snd_soc_dapm_widget g12a_tohdmitx_widgets[] = {
	SND_SOC_DAPM_MUX("I2S SRC", SND_SOC_NOPM, 0, 0,
			 &g12a_tohdmitx_i2s_mux),
	SND_SOC_DAPM_SWITCH("I2S OUT EN", SND_SOC_NOPM, 0, 0,
			    &g12a_tohdmitx_out_enable),
	SND_SOC_DAPM_MUX("SPDIF SRC", SND_SOC_NOPM, 0, 0,
			 &g12a_tohdmitx_spdif_mux),
	SND_SOC_DAPM_SWITCH("SPDIF OUT EN", SND_SOC_NOPM, 0, 0,
			    &g12a_tohdmitx_out_enable),
};

static int g12a_tohdmitx_input_probe(struct snd_soc_dai *dai)
{
	struct g12a_tohdmitx_input *data;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	dai->playback_dma_data = data;
	return 0;
}

static int g12a_tohdmitx_input_remove(struct snd_soc_dai *dai)
{
	kfree(dai->playback_dma_data);
	return 0;
}

static int g12a_tohdmitx_input_hw_params(struct snd_pcm_substream *substream,
					 struct snd_pcm_hw_params *params,
					 struct snd_soc_dai *dai)
{
	struct g12a_tohdmitx_input *data = dai->playback_dma_data;

	data->params.rates = snd_pcm_rate_to_rate_bit(params_rate(params));
	data->params.rate_min = params_rate(params);
	data->params.rate_max = params_rate(params);
	data->params.formats = 1 << params_format(params);
	data->params.channels_min = params_channels(params);
	data->params.channels_max = params_channels(params);
	data->params.sig_bits = dai->driver->playback.sig_bits;

	return 0;
}


static int g12a_tohdmitx_input_set_fmt(struct snd_soc_dai *dai,
				       unsigned int fmt)
{
	struct g12a_tohdmitx_input *data = dai->playback_dma_data;

	/* Save the source stream format for the downstream link */
	data->fmt = fmt;
	return 0;
}

static int g12a_tohdmitx_output_startup(struct snd_pcm_substream *substream,
					struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct g12a_tohdmitx_input *in_data =
		g12a_tohdmitx_get_input_data(dai->capture_widget);

	if (!in_data)
		return -ENODEV;

	if (WARN_ON(!rtd->dai_link->params)) {
		dev_warn(dai->dev, "codec2codec link expected\n");
		return -EINVAL;
	}

	/* Replace link params with the input params */
	rtd->dai_link->params = &in_data->params;

	if (!in_data->fmt)
		return 0;

	return snd_soc_runtime_set_dai_fmt(rtd, in_data->fmt);
}

static const struct snd_soc_dai_ops g12a_tohdmitx_input_ops = {
	.hw_params	= g12a_tohdmitx_input_hw_params,
	.set_fmt	= g12a_tohdmitx_input_set_fmt,
};

static const struct snd_soc_dai_ops g12a_tohdmitx_output_ops = {
	.startup	= g12a_tohdmitx_output_startup,
};

#define TOHDMITX_SPDIF_FORMATS					\
	(SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |	\
	 SNDRV_PCM_FMTBIT_S24_3LE | SNDRV_PCM_FMTBIT_S24_LE)

#define TOHDMITX_I2S_FORMATS					\
	(SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |	\
	 SNDRV_PCM_FMTBIT_S24_3LE | SNDRV_PCM_FMTBIT_S24_LE |	\
	 SNDRV_PCM_FMTBIT_S32_LE)

#define TOHDMITX_STREAM(xname, xsuffix, xfmt, xchmax)		\
{								\
	.stream_name	= xname " " xsuffix,			\
	.channels_min	= 1,					\
	.channels_max	= (xchmax),				\
	.rate_min       = 8000,					\
	.rate_max	= 192000,				\
	.formats	= (xfmt),				\
}

#define TOHDMITX_IN(xname, xid, xfmt, xchmax) {				\
	.name = xname,							\
	.id = (xid),							\
	.playback = TOHDMITX_STREAM(xname, "Playback", xfmt, xchmax),	\
	.ops = &g12a_tohdmitx_input_ops,				\
	.probe = g12a_tohdmitx_input_probe,				\
	.remove = g12a_tohdmitx_input_remove,				\
}

#define TOHDMITX_OUT(xname, xid, xfmt, xchmax) {			\
	.name = xname,							\
	.id = (xid),							\
	.capture = TOHDMITX_STREAM(xname, "Capture", xfmt, xchmax),	\
	.ops = &g12a_tohdmitx_output_ops,				\
}

static struct snd_soc_dai_driver g12a_tohdmitx_dai_drv[] = {
	TOHDMITX_IN("I2S IN A", TOHDMITX_I2S_IN_A,
		    TOHDMITX_I2S_FORMATS, 8),
	TOHDMITX_IN("I2S IN B", TOHDMITX_I2S_IN_B,
		    TOHDMITX_I2S_FORMATS, 8),
	TOHDMITX_IN("I2S IN C", TOHDMITX_I2S_IN_C,
		    TOHDMITX_I2S_FORMATS, 8),
	TOHDMITX_OUT("I2S OUT", TOHDMITX_I2S_OUT,
		     TOHDMITX_I2S_FORMATS, 8),
	TOHDMITX_IN("SPDIF IN A", TOHDMITX_SPDIF_IN_A,
		    TOHDMITX_SPDIF_FORMATS, 2),
	TOHDMITX_IN("SPDIF IN B", TOHDMITX_SPDIF_IN_B,
		    TOHDMITX_SPDIF_FORMATS, 2),
	TOHDMITX_OUT("SPDIF OUT", TOHDMITX_SPDIF_OUT,
		     TOHDMITX_SPDIF_FORMATS, 2),
};

static int g12a_tohdmi_component_probe(struct snd_soc_component *c)
{
	/* Initialize the static clock parameters */
	return snd_soc_component_write(c, TOHDMITX_CTRL0,
		     CTRL0_I2S_BLK_CAP_INV | CTRL0_SPDIF_CLK_CAP_INV);
}

static const struct snd_soc_dapm_route g12a_tohdmitx_routes[] = {
	{ "I2S SRC", "I2S A", "I2S IN A Playback" },
	{ "I2S SRC", "I2S B", "I2S IN B Playback" },
	{ "I2S SRC", "I2S C", "I2S IN C Playback" },
	{ "I2S OUT EN", "Switch", "I2S SRC" },
	{ "I2S OUT Capture", NULL, "I2S OUT EN" },
	{ "SPDIF SRC", "SPDIF A", "SPDIF IN A Playback" },
	{ "SPDIF SRC", "SPDIF B", "SPDIF IN B Playback" },
	{ "SPDIF OUT EN", "Switch", "SPDIF SRC" },
	{ "SPDIF OUT Capture", NULL, "SPDIF OUT EN" },
};

static const struct snd_soc_component_driver g12a_tohdmitx_component_drv = {
	.probe			= g12a_tohdmi_component_probe,
	.dapm_widgets		= g12a_tohdmitx_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(g12a_tohdmitx_widgets),
	.dapm_routes		= g12a_tohdmitx_routes,
	.num_dapm_routes	= ARRAY_SIZE(g12a_tohdmitx_routes),
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static const struct regmap_config g12a_tohdmitx_regmap_cfg = {
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
};

static const struct of_device_id g12a_tohdmitx_of_match[] = {
	{ .compatible = "amlogic,g12a-tohdmitx", },
	{}
};
MODULE_DEVICE_TABLE(of, g12a_tohdmitx_of_match);

static int g12a_tohdmitx_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	void __iomem *regs;
	struct regmap *map;

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	map = devm_regmap_init_mmio(dev, regs, &g12a_tohdmitx_regmap_cfg);
	if (IS_ERR(map)) {
		dev_err(dev, "failed to init regmap: %ld\n",
			PTR_ERR(map));
		return PTR_ERR(map);
	}

	return devm_snd_soc_register_component(dev,
			&g12a_tohdmitx_component_drv, g12a_tohdmitx_dai_drv,
			ARRAY_SIZE(g12a_tohdmitx_dai_drv));
}

static struct platform_driver g12a_tohdmitx_pdrv = {
	.driver = {
		.name = G12A_TOHDMITX_DRV_NAME,
		.of_match_table = g12a_tohdmitx_of_match,
	},
	.probe = g12a_tohdmitx_probe,
};
module_platform_driver(g12a_tohdmitx_pdrv);

MODULE_AUTHOR("Jerome Brunet <jbrunet@baylibre.com>");
MODULE_DESCRIPTION("Amlogic G12a To HDMI Tx Control Codec Driver");
MODULE_LICENSE("GPL v2");
