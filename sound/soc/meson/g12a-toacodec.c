// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2020 BayLibre, SAS.
// Author: Jerome Brunet <jbrunet@baylibre.com>

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <sound/pcm_params.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>

#include <dt-bindings/sound/meson-g12a-toacodec.h>
#include "axg-tdm.h"
#include "meson-codec-glue.h"

#define G12A_TOACODEC_DRV_NAME "g12a-toacodec"

#define TOACODEC_CTRL0			0x0
#define  CTRL0_ENABLE_SHIFT		31
#define  CTRL0_DAT_SEL_SM1_MSB		19
#define  CTRL0_DAT_SEL_SM1_LSB		18
#define  CTRL0_DAT_SEL_MSB		15
#define  CTRL0_DAT_SEL_LSB		14
#define  CTRL0_LANE_SEL_SM1		16
#define  CTRL0_LANE_SEL			12
#define  CTRL0_LRCLK_SEL_SM1_MSB	14
#define  CTRL0_LRCLK_SEL_SM1_LSB	12
#define  CTRL0_LRCLK_SEL_MSB		9
#define  CTRL0_LRCLK_SEL_LSB		8
#define  CTRL0_LRCLK_INV_SM1		BIT(10)
#define  CTRL0_BLK_CAP_INV_SM1		BIT(9)
#define  CTRL0_BLK_CAP_INV		BIT(7)
#define  CTRL0_BCLK_O_INV_SM1		BIT(8)
#define  CTRL0_BCLK_O_INV		BIT(6)
#define  CTRL0_BCLK_SEL_SM1_MSB		6
#define  CTRL0_BCLK_SEL_MSB		5
#define  CTRL0_BCLK_SEL_LSB		4
#define  CTRL0_MCLK_SEL			GENMASK(2, 0)

#define TOACODEC_OUT_CHMAX		2

struct g12a_toacodec {
	struct regmap_field *field_dat_sel;
	struct regmap_field *field_lrclk_sel;
	struct regmap_field *field_bclk_sel;
};

struct g12a_toacodec_match_data {
	const struct snd_soc_component_driver *component_drv;
	struct reg_field field_dat_sel;
	struct reg_field field_lrclk_sel;
	struct reg_field field_bclk_sel;
};

static const char * const g12a_toacodec_mux_texts[] = {
	"I2S A", "I2S B", "I2S C",
};

static int g12a_toacodec_mux_put_enum(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_dapm_kcontrol_component(kcontrol);
	struct g12a_toacodec *priv = snd_soc_component_get_drvdata(component);
	struct snd_soc_dapm_context *dapm =
		snd_soc_dapm_kcontrol_dapm(kcontrol);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int mux, reg;

	mux = snd_soc_enum_item_to_val(e, ucontrol->value.enumerated.item[0]);
	regmap_field_read(priv->field_dat_sel, &reg);

	if (mux == reg)
		return 0;

	/* Force disconnect of the mux while updating */
	snd_soc_dapm_mux_update_power(dapm, kcontrol, 0, NULL, NULL);

	regmap_field_write(priv->field_dat_sel, mux);
	regmap_field_write(priv->field_lrclk_sel, mux);
	regmap_field_write(priv->field_bclk_sel, mux);

	/*
	 * FIXME:
	 * On this soc, the glue gets the MCLK directly from the clock
	 * controller instead of going the through the TDM interface.
	 *
	 * Here we assume interface A uses clock A, etc ... While it is
	 * true for now, it could be different. Instead the glue should
	 * find out the clock used by the interface and select the same
	 * source. For that, we will need regmap backed clock mux which
	 * is a work in progress
	 */
	snd_soc_component_update_bits(component, e->reg,
				      CTRL0_MCLK_SEL,
				      FIELD_PREP(CTRL0_MCLK_SEL, mux));

	snd_soc_dapm_mux_update_power(dapm, kcontrol, mux, e, NULL);

	return 0;
}

static SOC_ENUM_SINGLE_DECL(g12a_toacodec_mux_enum, TOACODEC_CTRL0,
			    CTRL0_DAT_SEL_LSB,
			    g12a_toacodec_mux_texts);

static SOC_ENUM_SINGLE_DECL(sm1_toacodec_mux_enum, TOACODEC_CTRL0,
			    CTRL0_DAT_SEL_SM1_LSB,
			    g12a_toacodec_mux_texts);

static const struct snd_kcontrol_new g12a_toacodec_mux =
	SOC_DAPM_ENUM_EXT("Source", g12a_toacodec_mux_enum,
			  snd_soc_dapm_get_enum_double,
			  g12a_toacodec_mux_put_enum);

static const struct snd_kcontrol_new sm1_toacodec_mux =
	SOC_DAPM_ENUM_EXT("Source", sm1_toacodec_mux_enum,
			  snd_soc_dapm_get_enum_double,
			  g12a_toacodec_mux_put_enum);

static const struct snd_kcontrol_new g12a_toacodec_out_enable =
	SOC_DAPM_SINGLE_AUTODISABLE("Switch", TOACODEC_CTRL0,
				    CTRL0_ENABLE_SHIFT, 1, 0);

static const struct snd_soc_dapm_widget g12a_toacodec_widgets[] = {
	SND_SOC_DAPM_MUX("SRC", SND_SOC_NOPM, 0, 0,
			 &g12a_toacodec_mux),
	SND_SOC_DAPM_SWITCH("OUT EN", SND_SOC_NOPM, 0, 0,
			    &g12a_toacodec_out_enable),
};

static const struct snd_soc_dapm_widget sm1_toacodec_widgets[] = {
	SND_SOC_DAPM_MUX("SRC", SND_SOC_NOPM, 0, 0,
			 &sm1_toacodec_mux),
	SND_SOC_DAPM_SWITCH("OUT EN", SND_SOC_NOPM, 0, 0,
			    &g12a_toacodec_out_enable),
};

static int g12a_toacodec_input_hw_params(struct snd_pcm_substream *substream,
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
	data->params.channels_min = min_t(unsigned int, TOACODEC_OUT_CHMAX,
					data->params.channels_min);
	data->params.channels_max = min_t(unsigned int, TOACODEC_OUT_CHMAX,
					data->params.channels_max);

	return 0;
}

static const struct snd_soc_dai_ops g12a_toacodec_input_ops = {
	.hw_params	= g12a_toacodec_input_hw_params,
	.set_fmt	= meson_codec_glue_input_set_fmt,
};

static const struct snd_soc_dai_ops g12a_toacodec_output_ops = {
	.startup	= meson_codec_glue_output_startup,
};

#define TOACODEC_STREAM(xname, xsuffix, xchmax)			\
{								\
	.stream_name	= xname " " xsuffix,			\
	.channels_min	= 1,					\
	.channels_max	= (xchmax),				\
	.rate_min       = 5512,					\
	.rate_max	= 192000,				\
	.formats	= AXG_TDM_FORMATS,			\
}

#define TOACODEC_INPUT(xname, xid) {					\
	.name = xname,							\
	.id = (xid),							\
	.playback = TOACODEC_STREAM(xname, "Playback", 8),		\
	.ops = &g12a_toacodec_input_ops,				\
	.probe = meson_codec_glue_input_dai_probe,			\
	.remove = meson_codec_glue_input_dai_remove,			\
}

#define TOACODEC_OUTPUT(xname, xid) {					\
	.name = xname,							\
	.id = (xid),							\
	.capture = TOACODEC_STREAM(xname, "Capture", TOACODEC_OUT_CHMAX), \
	.ops = &g12a_toacodec_output_ops,				\
}

static struct snd_soc_dai_driver g12a_toacodec_dai_drv[] = {
	TOACODEC_INPUT("IN A", TOACODEC_IN_A),
	TOACODEC_INPUT("IN B", TOACODEC_IN_B),
	TOACODEC_INPUT("IN C", TOACODEC_IN_C),
	TOACODEC_OUTPUT("OUT", TOACODEC_OUT),
};

static int g12a_toacodec_component_probe(struct snd_soc_component *c)
{
	/* Initialize the static clock parameters */
	return snd_soc_component_write(c, TOACODEC_CTRL0,
				       CTRL0_BLK_CAP_INV);
}

static int sm1_toacodec_component_probe(struct snd_soc_component *c)
{
	/* Initialize the static clock parameters */
	return snd_soc_component_write(c, TOACODEC_CTRL0,
				       CTRL0_BLK_CAP_INV_SM1);
}

static const struct snd_soc_dapm_route g12a_toacodec_routes[] = {
	{ "SRC", "I2S A", "IN A Playback" },
	{ "SRC", "I2S B", "IN B Playback" },
	{ "SRC", "I2S C", "IN C Playback" },
	{ "OUT EN", "Switch", "SRC" },
	{ "OUT Capture", NULL, "OUT EN" },
};

static const struct snd_kcontrol_new g12a_toacodec_controls[] = {
	SOC_SINGLE("Lane Select", TOACODEC_CTRL0, CTRL0_LANE_SEL, 3, 0),
};

static const struct snd_kcontrol_new sm1_toacodec_controls[] = {
	SOC_SINGLE("Lane Select", TOACODEC_CTRL0, CTRL0_LANE_SEL_SM1, 3, 0),
};

static const struct snd_soc_component_driver g12a_toacodec_component_drv = {
	.probe			= g12a_toacodec_component_probe,
	.controls		= g12a_toacodec_controls,
	.num_controls		= ARRAY_SIZE(g12a_toacodec_controls),
	.dapm_widgets		= g12a_toacodec_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(g12a_toacodec_widgets),
	.dapm_routes		= g12a_toacodec_routes,
	.num_dapm_routes	= ARRAY_SIZE(g12a_toacodec_routes),
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static const struct snd_soc_component_driver sm1_toacodec_component_drv = {
	.probe			= sm1_toacodec_component_probe,
	.controls		= sm1_toacodec_controls,
	.num_controls		= ARRAY_SIZE(sm1_toacodec_controls),
	.dapm_widgets		= sm1_toacodec_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(sm1_toacodec_widgets),
	.dapm_routes		= g12a_toacodec_routes,
	.num_dapm_routes	= ARRAY_SIZE(g12a_toacodec_routes),
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static const struct regmap_config g12a_toacodec_regmap_cfg = {
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
};

static const struct g12a_toacodec_match_data g12a_toacodec_match_data = {
	.component_drv	= &g12a_toacodec_component_drv,
	.field_dat_sel	= REG_FIELD(TOACODEC_CTRL0, 14, 15),
	.field_lrclk_sel = REG_FIELD(TOACODEC_CTRL0, 8, 9),
	.field_bclk_sel	= REG_FIELD(TOACODEC_CTRL0, 4, 5),
};

static const struct g12a_toacodec_match_data sm1_toacodec_match_data = {
	.component_drv	= &sm1_toacodec_component_drv,
	.field_dat_sel	= REG_FIELD(TOACODEC_CTRL0, 18, 19),
	.field_lrclk_sel = REG_FIELD(TOACODEC_CTRL0, 12, 14),
	.field_bclk_sel	= REG_FIELD(TOACODEC_CTRL0, 4, 6),
};

static const struct of_device_id g12a_toacodec_of_match[] = {
	{
		.compatible = "amlogic,g12a-toacodec",
		.data = &g12a_toacodec_match_data,
	},
	{
		.compatible = "amlogic,sm1-toacodec",
		.data = &sm1_toacodec_match_data,
	},
	{}
};
MODULE_DEVICE_TABLE(of, g12a_toacodec_of_match);

static int g12a_toacodec_probe(struct platform_device *pdev)
{
	const struct g12a_toacodec_match_data *data;
	struct device *dev = &pdev->dev;
	struct g12a_toacodec *priv;
	void __iomem *regs;
	struct regmap *map;
	int ret;

	data = device_get_match_data(dev);
	if (!data) {
		dev_err(dev, "failed to match device\n");
		return -ENODEV;
	}

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	platform_set_drvdata(pdev, priv);

	ret = device_reset(dev);
	if (ret)
		return ret;

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	map = devm_regmap_init_mmio(dev, regs, &g12a_toacodec_regmap_cfg);
	if (IS_ERR(map)) {
		dev_err(dev, "failed to init regmap: %ld\n",
			PTR_ERR(map));
		return PTR_ERR(map);
	}

	priv->field_dat_sel = devm_regmap_field_alloc(dev, map, data->field_dat_sel);
	if (IS_ERR(priv->field_dat_sel))
		return PTR_ERR(priv->field_dat_sel);

	priv->field_lrclk_sel = devm_regmap_field_alloc(dev, map, data->field_lrclk_sel);
	if (IS_ERR(priv->field_lrclk_sel))
		return PTR_ERR(priv->field_lrclk_sel);

	priv->field_bclk_sel = devm_regmap_field_alloc(dev, map, data->field_bclk_sel);
	if (IS_ERR(priv->field_bclk_sel))
		return PTR_ERR(priv->field_bclk_sel);

	return devm_snd_soc_register_component(dev,
			data->component_drv, g12a_toacodec_dai_drv,
			ARRAY_SIZE(g12a_toacodec_dai_drv));
}

static struct platform_driver g12a_toacodec_pdrv = {
	.driver = {
		.name = G12A_TOACODEC_DRV_NAME,
		.of_match_table = g12a_toacodec_of_match,
	},
	.probe = g12a_toacodec_probe,
};
module_platform_driver(g12a_toacodec_pdrv);

MODULE_AUTHOR("Jerome Brunet <jbrunet@baylibre.com>");
MODULE_DESCRIPTION("Amlogic G12a To Internal DAC Codec Driver");
MODULE_LICENSE("GPL v2");
