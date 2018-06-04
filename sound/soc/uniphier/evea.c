// SPDX-License-Identifier: GPL-2.0
//
// Socionext UniPhier EVEA ADC/DAC codec driver.
//
// Copyright (c) 2016-2017 Socionext Inc.

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <sound/pcm.h>
#include <sound/soc.h>

#define DRV_NAME        "evea"
#define EVEA_RATES      SNDRV_PCM_RATE_48000
#define EVEA_FORMATS    SNDRV_PCM_FMTBIT_S32_LE

#define AADCPOW(n)                           (0x0078 + 0x04 * (n))
#define   AADCPOW_AADC_POWD                   BIT(0)
#define ALINSW1                              0x0088
#define   ALINSW1_SEL1_SHIFT                  3
#define AHPOUTPOW                            0x0098
#define   AHPOUTPOW_HP_ON                     BIT(4)
#define ALINEPOW                             0x009c
#define   ALINEPOW_LIN2_POWD                  BIT(3)
#define   ALINEPOW_LIN1_POWD                  BIT(4)
#define ALO1OUTPOW                           0x00a8
#define   ALO1OUTPOW_LO1_ON                   BIT(4)
#define ALO2OUTPOW                           0x00ac
#define   ALO2OUTPOW_ADAC2_MUTE               BIT(0)
#define   ALO2OUTPOW_LO2_ON                   BIT(4)
#define AANAPOW                              0x00b8
#define   AANAPOW_A_POWD                      BIT(4)
#define ADACSEQ1(n)                          (0x0144 + 0x40 * (n))
#define   ADACSEQ1_MMUTE                      BIT(1)
#define ADACSEQ2(n)                          (0x0160 + 0x40 * (n))
#define   ADACSEQ2_ADACIN_FIX                 BIT(0)
#define ADAC1ODC                             0x0200
#define   ADAC1ODC_HP_DIS_RES_MASK            GENMASK(2, 1)
#define   ADAC1ODC_HP_DIS_RES_OFF             (0x0 << 1)
#define   ADAC1ODC_HP_DIS_RES_ON              (0x3 << 1)
#define   ADAC1ODC_ADAC_RAMPCLT_MASK          GENMASK(8, 7)
#define   ADAC1ODC_ADAC_RAMPCLT_NORMAL        (0x0 << 7)
#define   ADAC1ODC_ADAC_RAMPCLT_REDUCE        (0x1 << 7)

struct evea_priv {
	struct clk *clk, *clk_exiv;
	struct reset_control *rst, *rst_exiv, *rst_adamv;
	struct regmap *regmap;

	int switch_lin;
	int switch_lo;
	int switch_hp;
};

static const struct snd_soc_dapm_widget evea_widgets[] = {
	SND_SOC_DAPM_ADC("ADC", "Capture", SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_INPUT("LIN1_LP"),
	SND_SOC_DAPM_INPUT("LIN1_RP"),
	SND_SOC_DAPM_INPUT("LIN2_LP"),
	SND_SOC_DAPM_INPUT("LIN2_RP"),
	SND_SOC_DAPM_INPUT("LIN3_LP"),
	SND_SOC_DAPM_INPUT("LIN3_RP"),

	SND_SOC_DAPM_DAC("DAC", "Playback", SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_OUTPUT("HP1_L"),
	SND_SOC_DAPM_OUTPUT("HP1_R"),
	SND_SOC_DAPM_OUTPUT("LO2_L"),
	SND_SOC_DAPM_OUTPUT("LO2_R"),
};

static const struct snd_soc_dapm_route evea_routes[] = {
	{ "ADC", NULL, "LIN1_LP" },
	{ "ADC", NULL, "LIN1_RP" },
	{ "ADC", NULL, "LIN2_LP" },
	{ "ADC", NULL, "LIN2_RP" },
	{ "ADC", NULL, "LIN3_LP" },
	{ "ADC", NULL, "LIN3_RP" },

	{ "HP1_L", NULL, "DAC" },
	{ "HP1_R", NULL, "DAC" },
	{ "LO2_L", NULL, "DAC" },
	{ "LO2_R", NULL, "DAC" },
};

static void evea_set_power_state_on(struct evea_priv *evea)
{
	struct regmap *map = evea->regmap;

	regmap_update_bits(map, AANAPOW, AANAPOW_A_POWD,
			   AANAPOW_A_POWD);

	regmap_update_bits(map, ADAC1ODC, ADAC1ODC_HP_DIS_RES_MASK,
			   ADAC1ODC_HP_DIS_RES_ON);

	regmap_update_bits(map, ADAC1ODC, ADAC1ODC_ADAC_RAMPCLT_MASK,
			   ADAC1ODC_ADAC_RAMPCLT_REDUCE);

	regmap_update_bits(map, ADACSEQ2(0), ADACSEQ2_ADACIN_FIX, 0);
	regmap_update_bits(map, ADACSEQ2(1), ADACSEQ2_ADACIN_FIX, 0);
	regmap_update_bits(map, ADACSEQ2(2), ADACSEQ2_ADACIN_FIX, 0);
}

static void evea_set_power_state_off(struct evea_priv *evea)
{
	struct regmap *map = evea->regmap;

	regmap_update_bits(map, ADAC1ODC, ADAC1ODC_HP_DIS_RES_MASK,
			   ADAC1ODC_HP_DIS_RES_ON);

	regmap_update_bits(map, ADACSEQ1(0), ADACSEQ1_MMUTE,
			   ADACSEQ1_MMUTE);
	regmap_update_bits(map, ADACSEQ1(1), ADACSEQ1_MMUTE,
			   ADACSEQ1_MMUTE);
	regmap_update_bits(map, ADACSEQ1(2), ADACSEQ1_MMUTE,
			   ADACSEQ1_MMUTE);

	regmap_update_bits(map, ALO1OUTPOW, ALO1OUTPOW_LO1_ON, 0);
	regmap_update_bits(map, ALO2OUTPOW, ALO2OUTPOW_LO2_ON, 0);
	regmap_update_bits(map, AHPOUTPOW, AHPOUTPOW_HP_ON, 0);
}

static int evea_update_switch_lin(struct evea_priv *evea)
{
	struct regmap *map = evea->regmap;

	if (evea->switch_lin) {
		regmap_update_bits(map, ALINEPOW,
				   ALINEPOW_LIN2_POWD | ALINEPOW_LIN1_POWD,
				   ALINEPOW_LIN2_POWD | ALINEPOW_LIN1_POWD);

		regmap_update_bits(map, AADCPOW(0), AADCPOW_AADC_POWD,
				   AADCPOW_AADC_POWD);
		regmap_update_bits(map, AADCPOW(1), AADCPOW_AADC_POWD,
				   AADCPOW_AADC_POWD);
	} else {
		regmap_update_bits(map, AADCPOW(0), AADCPOW_AADC_POWD, 0);
		regmap_update_bits(map, AADCPOW(1), AADCPOW_AADC_POWD, 0);

		regmap_update_bits(map, ALINEPOW,
				   ALINEPOW_LIN2_POWD | ALINEPOW_LIN1_POWD, 0);
	}

	return 0;
}

static int evea_update_switch_lo(struct evea_priv *evea)
{
	struct regmap *map = evea->regmap;

	if (evea->switch_lo) {
		regmap_update_bits(map, ADACSEQ1(0), ADACSEQ1_MMUTE, 0);
		regmap_update_bits(map, ADACSEQ1(2), ADACSEQ1_MMUTE, 0);

		regmap_update_bits(map, ALO1OUTPOW, ALO1OUTPOW_LO1_ON,
				   ALO1OUTPOW_LO1_ON);
		regmap_update_bits(map, ALO2OUTPOW,
				   ALO2OUTPOW_ADAC2_MUTE | ALO2OUTPOW_LO2_ON,
				   ALO2OUTPOW_ADAC2_MUTE | ALO2OUTPOW_LO2_ON);
	} else {
		regmap_update_bits(map, ADACSEQ1(0), ADACSEQ1_MMUTE,
				   ADACSEQ1_MMUTE);
		regmap_update_bits(map, ADACSEQ1(2), ADACSEQ1_MMUTE,
				   ADACSEQ1_MMUTE);

		regmap_update_bits(map, ALO1OUTPOW, ALO1OUTPOW_LO1_ON, 0);
		regmap_update_bits(map, ALO2OUTPOW,
				   ALO2OUTPOW_ADAC2_MUTE | ALO2OUTPOW_LO2_ON,
				   0);
	}

	return 0;
}

static int evea_update_switch_hp(struct evea_priv *evea)
{
	struct regmap *map = evea->regmap;

	if (evea->switch_hp) {
		regmap_update_bits(map, ADACSEQ1(1), ADACSEQ1_MMUTE, 0);

		regmap_update_bits(map, AHPOUTPOW, AHPOUTPOW_HP_ON,
				   AHPOUTPOW_HP_ON);

		regmap_update_bits(map, ADAC1ODC, ADAC1ODC_HP_DIS_RES_MASK,
				   ADAC1ODC_HP_DIS_RES_OFF);
	} else {
		regmap_update_bits(map, ADAC1ODC, ADAC1ODC_HP_DIS_RES_MASK,
				   ADAC1ODC_HP_DIS_RES_ON);

		regmap_update_bits(map, ADACSEQ1(1), ADACSEQ1_MMUTE,
				   ADACSEQ1_MMUTE);

		regmap_update_bits(map, AHPOUTPOW, AHPOUTPOW_HP_ON, 0);
	}

	return 0;
}

static void evea_update_switch_all(struct evea_priv *evea)
{
	evea_update_switch_lin(evea);
	evea_update_switch_lo(evea);
	evea_update_switch_hp(evea);
}

static int evea_get_switch_lin(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct evea_priv *evea = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = evea->switch_lin;

	return 0;
}

static int evea_set_switch_lin(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct evea_priv *evea = snd_soc_component_get_drvdata(component);

	if (evea->switch_lin == ucontrol->value.integer.value[0])
		return 0;

	evea->switch_lin = ucontrol->value.integer.value[0];

	return evea_update_switch_lin(evea);
}

static int evea_get_switch_lo(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct evea_priv *evea = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = evea->switch_lo;

	return 0;
}

static int evea_set_switch_lo(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct evea_priv *evea = snd_soc_component_get_drvdata(component);

	if (evea->switch_lo == ucontrol->value.integer.value[0])
		return 0;

	evea->switch_lo = ucontrol->value.integer.value[0];

	return evea_update_switch_lo(evea);
}

static int evea_get_switch_hp(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct evea_priv *evea = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = evea->switch_hp;

	return 0;
}

static int evea_set_switch_hp(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct evea_priv *evea = snd_soc_component_get_drvdata(component);

	if (evea->switch_hp == ucontrol->value.integer.value[0])
		return 0;

	evea->switch_hp = ucontrol->value.integer.value[0];

	return evea_update_switch_hp(evea);
}

static const char * const linsw1_sel1_text[] = {
	"LIN1", "LIN2", "LIN3"
};

static SOC_ENUM_SINGLE_DECL(linsw1_sel1_enum,
	ALINSW1, ALINSW1_SEL1_SHIFT,
	linsw1_sel1_text);

static const struct snd_kcontrol_new evea_controls[] = {
	SOC_ENUM("Line Capture Source", linsw1_sel1_enum),
	SOC_SINGLE_BOOL_EXT("Line Capture Switch", 0,
			    evea_get_switch_lin, evea_set_switch_lin),
	SOC_SINGLE_BOOL_EXT("Line Playback Switch", 0,
			    evea_get_switch_lo, evea_set_switch_lo),
	SOC_SINGLE_BOOL_EXT("Headphone Playback Switch", 0,
			    evea_get_switch_hp, evea_set_switch_hp),
};

static int evea_codec_probe(struct snd_soc_component *component)
{
	struct evea_priv *evea = snd_soc_component_get_drvdata(component);

	evea->switch_lin = 1;
	evea->switch_lo = 1;
	evea->switch_hp = 1;

	evea_set_power_state_on(evea);
	evea_update_switch_all(evea);

	return 0;
}

static int evea_codec_suspend(struct snd_soc_component *component)
{
	struct evea_priv *evea = snd_soc_component_get_drvdata(component);

	evea_set_power_state_off(evea);

	reset_control_assert(evea->rst_adamv);
	reset_control_assert(evea->rst_exiv);
	reset_control_assert(evea->rst);

	clk_disable_unprepare(evea->clk_exiv);
	clk_disable_unprepare(evea->clk);

	return 0;
}

static int evea_codec_resume(struct snd_soc_component *component)
{
	struct evea_priv *evea = snd_soc_component_get_drvdata(component);
	int ret;

	ret = clk_prepare_enable(evea->clk);
	if (ret)
		return ret;

	ret = clk_prepare_enable(evea->clk_exiv);
	if (ret)
		goto err_out_clock;

	ret = reset_control_deassert(evea->rst);
	if (ret)
		goto err_out_clock_exiv;

	ret = reset_control_deassert(evea->rst_exiv);
	if (ret)
		goto err_out_reset;

	ret = reset_control_deassert(evea->rst_adamv);
	if (ret)
		goto err_out_reset_exiv;

	evea_set_power_state_on(evea);
	evea_update_switch_all(evea);

	return 0;

err_out_reset_exiv:
	reset_control_assert(evea->rst_exiv);

err_out_reset:
	reset_control_assert(evea->rst);

err_out_clock_exiv:
	clk_disable_unprepare(evea->clk_exiv);

err_out_clock:
	clk_disable_unprepare(evea->clk);

	return ret;
}

static struct snd_soc_component_driver soc_codec_evea = {
	.probe			= evea_codec_probe,
	.suspend		= evea_codec_suspend,
	.resume			= evea_codec_resume,
	.dapm_widgets		= evea_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(evea_widgets),
	.dapm_routes		= evea_routes,
	.num_dapm_routes	= ARRAY_SIZE(evea_routes),
	.controls		= evea_controls,
	.num_controls		= ARRAY_SIZE(evea_controls),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static struct snd_soc_dai_driver soc_dai_evea[] = {
	{
		.name     = DRV_NAME "-line1",
		.playback = {
			.stream_name  = "Line Out 1",
			.formats      = EVEA_FORMATS,
			.rates        = EVEA_RATES,
			.channels_min = 2,
			.channels_max = 2,
		},
		.capture = {
			.stream_name  = "Line In 1",
			.formats      = EVEA_FORMATS,
			.rates        = EVEA_RATES,
			.channels_min = 2,
			.channels_max = 2,
		},
	},
	{
		.name     = DRV_NAME "-hp1",
		.playback = {
			.stream_name  = "Headphone 1",
			.formats      = EVEA_FORMATS,
			.rates        = EVEA_RATES,
			.channels_min = 2,
			.channels_max = 2,
		},
	},
	{
		.name     = DRV_NAME "-lo2",
		.playback = {
			.stream_name  = "Line Out 2",
			.formats      = EVEA_FORMATS,
			.rates        = EVEA_RATES,
			.channels_min = 2,
			.channels_max = 2,
		},
	},
};

static const struct regmap_config evea_regmap_config = {
	.reg_bits      = 32,
	.reg_stride    = 4,
	.val_bits      = 32,
	.max_register  = 0xffc,
	.cache_type    = REGCACHE_NONE,
};

static int evea_probe(struct platform_device *pdev)
{
	struct evea_priv *evea;
	struct resource *res;
	void __iomem *preg;
	int ret;

	evea = devm_kzalloc(&pdev->dev, sizeof(struct evea_priv), GFP_KERNEL);
	if (!evea)
		return -ENOMEM;

	evea->clk = devm_clk_get(&pdev->dev, "evea");
	if (IS_ERR(evea->clk))
		return PTR_ERR(evea->clk);

	evea->clk_exiv = devm_clk_get(&pdev->dev, "exiv");
	if (IS_ERR(evea->clk_exiv))
		return PTR_ERR(evea->clk_exiv);

	evea->rst = devm_reset_control_get_shared(&pdev->dev, "evea");
	if (IS_ERR(evea->rst))
		return PTR_ERR(evea->rst);

	evea->rst_exiv = devm_reset_control_get_shared(&pdev->dev, "exiv");
	if (IS_ERR(evea->rst_exiv))
		return PTR_ERR(evea->rst_exiv);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	preg = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(preg))
		return PTR_ERR(preg);

	evea->regmap = devm_regmap_init_mmio(&pdev->dev, preg,
					     &evea_regmap_config);
	if (IS_ERR(evea->regmap))
		return PTR_ERR(evea->regmap);

	ret = clk_prepare_enable(evea->clk);
	if (ret)
		return ret;

	ret = clk_prepare_enable(evea->clk_exiv);
	if (ret)
		goto err_out_clock;

	ret = reset_control_deassert(evea->rst);
	if (ret)
		goto err_out_clock_exiv;

	ret = reset_control_deassert(evea->rst_exiv);
	if (ret)
		goto err_out_reset;

	/* ADAMV will hangup if EXIV reset is asserted */
	evea->rst_adamv = devm_reset_control_get_shared(&pdev->dev, "adamv");
	if (IS_ERR(evea->rst_adamv)) {
		ret = PTR_ERR(evea->rst_adamv);
		goto err_out_reset_exiv;
	}

	ret = reset_control_deassert(evea->rst_adamv);
	if (ret)
		goto err_out_reset_exiv;

	platform_set_drvdata(pdev, evea);

	ret = devm_snd_soc_register_component(&pdev->dev, &soc_codec_evea,
				     soc_dai_evea, ARRAY_SIZE(soc_dai_evea));
	if (ret)
		goto err_out_reset_adamv;

	return 0;

err_out_reset_adamv:
	reset_control_assert(evea->rst_adamv);

err_out_reset_exiv:
	reset_control_assert(evea->rst_exiv);

err_out_reset:
	reset_control_assert(evea->rst);

err_out_clock_exiv:
	clk_disable_unprepare(evea->clk_exiv);

err_out_clock:
	clk_disable_unprepare(evea->clk);

	return ret;
}

static int evea_remove(struct platform_device *pdev)
{
	struct evea_priv *evea = platform_get_drvdata(pdev);

	reset_control_assert(evea->rst_adamv);
	reset_control_assert(evea->rst_exiv);
	reset_control_assert(evea->rst);

	clk_disable_unprepare(evea->clk_exiv);
	clk_disable_unprepare(evea->clk);

	return 0;
}

static const struct of_device_id evea_of_match[] = {
	{ .compatible = "socionext,uniphier-evea", },
	{}
};
MODULE_DEVICE_TABLE(of, evea_of_match);

static struct platform_driver evea_codec_driver = {
	.driver = {
		.name = DRV_NAME,
		.of_match_table = of_match_ptr(evea_of_match),
	},
	.probe  = evea_probe,
	.remove = evea_remove,
};
module_platform_driver(evea_codec_driver);

MODULE_AUTHOR("Katsuhiro Suzuki <suzuki.katsuhiro@socionext.com>");
MODULE_DESCRIPTION("UniPhier EVEA codec driver");
MODULE_LICENSE("GPL v2");
