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
#define  CTRL0_DAT_SEL_SHIFT		14
#define  CTRL0_DAT_SEL			(0x3 << CTRL0_DAT_SEL_SHIFT)
#define  CTRL0_LANE_SEL			12
#define  CTRL0_LRCLK_SEL		GENMASK(9, 8)
#define  CTRL0_BLK_CAP_INV		BIT(7)
#define  CTRL0_BCLK_O_INV		BIT(6)
#define  CTRL0_BCLK_SEL			GENMASK(5, 4)
#define  CTRL0_MCLK_SEL			GENMASK(2, 0)

#define TOACODEC_OUT_CHMAX		2

static const char * const g12a_toacodec_mux_texts[] = {
	"I2S A", "I2S B", "I2S C",
};

static int g12a_toacodec_mux_put_enum(struct snd_kcontrol *kcontrol,
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
					      CTRL0_DAT_SEL,
					      FIELD_PREP(CTRL0_DAT_SEL, mux));

	if (!changed)
		return 0;

	/* Force disconnect of the mux while updating */
	snd_soc_dapm_mux_update_power(dapm, kcontrol, 0, NULL, NULL);

	snd_soc_component_update_bits(component, e->reg,
				      CTRL0_DAT_SEL |
				      CTRL0_LRCLK_SEL |
				      CTRL0_BCLK_SEL,
				      FIELD_PREP(CTRL0_DAT_SEL, mux) |
				      FIELD_PREP(CTRL0_LRCLK_SEL, mux) |
				      FIELD_PREP(CTRL0_BCLK_SEL, mux));

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
			    CTRL0_DAT_SEL_SHIFT,
			    g12a_toacodec_mux_texts);

static const struct snd_kcontrol_new g12a_toacodec_mux =
	SOC_DAPM_ENUM_EXT("Source", g12a_toacodec_mux_enum,
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

static const struct regmap_config g12a_toacodec_regmap_cfg = {
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
};

static const struct of_device_id g12a_toacodec_of_match[] = {
	{ .compatible = "amlogic,g12a-toacodec", },
	{}
};
MODULE_DEVICE_TABLE(of, g12a_toacodec_of_match);

static int g12a_toacodec_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	void __iomem *regs;
	struct regmap *map;
	int ret;

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

	return devm_snd_soc_register_component(dev,
			&g12a_toacodec_component_drv, g12a_toacodec_dai_drv,
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
