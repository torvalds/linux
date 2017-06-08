/*
 * Pistachio internal dac driver
 *
 * Copyright (C) 2015 Imagination Technologies Ltd.
 *
 * Author: Damien Horsley <Damien.Horsley@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

#include <sound/pcm_params.h>
#include <sound/soc.h>

#define PISTACHIO_INTERNAL_DAC_CTRL			0x40
#define PISTACHIO_INTERNAL_DAC_CTRL_PWR_SEL_MASK	0x2
#define PISTACHIO_INTERNAL_DAC_CTRL_PWRDN_MASK		0x1

#define PISTACHIO_INTERNAL_DAC_SRST			0x44
#define PISTACHIO_INTERNAL_DAC_SRST_MASK		0x1

#define PISTACHIO_INTERNAL_DAC_GTI_CTRL			0x48
#define PISTACHIO_INTERNAL_DAC_GTI_CTRL_ADDR_SHIFT	0
#define PISTACHIO_INTERNAL_DAC_GTI_CTRL_ADDR_MASK	0xFFF
#define PISTACHIO_INTERNAL_DAC_GTI_CTRL_WE_MASK		0x1000
#define PISTACHIO_INTERNAL_DAC_GTI_CTRL_WDATA_SHIFT	13
#define PISTACHIO_INTERNAL_DAC_GTI_CTRL_WDATA_MASK	0x1FE000

#define PISTACHIO_INTERNAL_DAC_PWR			0x1
#define PISTACHIO_INTERNAL_DAC_PWR_MASK			0x1

#define PISTACHIO_INTERNAL_DAC_FORMATS (SNDRV_PCM_FMTBIT_S24_LE |  \
					SNDRV_PCM_FMTBIT_S32_LE)

/* codec private data */
struct pistachio_internal_dac {
	struct regmap *regmap;
	struct regulator *supply;
	bool mute;
};

static const struct snd_kcontrol_new pistachio_internal_dac_snd_controls[] = {
	SOC_SINGLE("Playback Switch", PISTACHIO_INTERNAL_DAC_CTRL, 2, 1, 1)
};

static const struct snd_soc_dapm_widget pistachio_internal_dac_widgets[] = {
	SND_SOC_DAPM_DAC("DAC", "Playback", SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_OUTPUT("AOUTL"),
	SND_SOC_DAPM_OUTPUT("AOUTR"),
};

static const struct snd_soc_dapm_route pistachio_internal_dac_routes[] = {
	{ "AOUTL", NULL, "DAC" },
	{ "AOUTR", NULL, "DAC" },
};

static void pistachio_internal_dac_reg_writel(struct regmap *top_regs,
						u32 val, u32 reg)
{
	regmap_update_bits(top_regs, PISTACHIO_INTERNAL_DAC_GTI_CTRL,
			PISTACHIO_INTERNAL_DAC_GTI_CTRL_ADDR_MASK,
			reg << PISTACHIO_INTERNAL_DAC_GTI_CTRL_ADDR_SHIFT);

	regmap_update_bits(top_regs, PISTACHIO_INTERNAL_DAC_GTI_CTRL,
			PISTACHIO_INTERNAL_DAC_GTI_CTRL_WDATA_MASK,
			val << PISTACHIO_INTERNAL_DAC_GTI_CTRL_WDATA_SHIFT);

	regmap_update_bits(top_regs, PISTACHIO_INTERNAL_DAC_GTI_CTRL,
			PISTACHIO_INTERNAL_DAC_GTI_CTRL_WE_MASK,
			PISTACHIO_INTERNAL_DAC_GTI_CTRL_WE_MASK);

	regmap_update_bits(top_regs, PISTACHIO_INTERNAL_DAC_GTI_CTRL,
			PISTACHIO_INTERNAL_DAC_GTI_CTRL_WE_MASK, 0);
}

static void pistachio_internal_dac_pwr_off(struct pistachio_internal_dac *dac)
{
	regmap_update_bits(dac->regmap, PISTACHIO_INTERNAL_DAC_CTRL,
		PISTACHIO_INTERNAL_DAC_CTRL_PWRDN_MASK,
		PISTACHIO_INTERNAL_DAC_CTRL_PWRDN_MASK);

	pistachio_internal_dac_reg_writel(dac->regmap, 0,
					PISTACHIO_INTERNAL_DAC_PWR);
}

static void pistachio_internal_dac_pwr_on(struct pistachio_internal_dac *dac)
{
	regmap_update_bits(dac->regmap, PISTACHIO_INTERNAL_DAC_SRST,
			PISTACHIO_INTERNAL_DAC_SRST_MASK,
			PISTACHIO_INTERNAL_DAC_SRST_MASK);

	regmap_update_bits(dac->regmap, PISTACHIO_INTERNAL_DAC_SRST,
			PISTACHIO_INTERNAL_DAC_SRST_MASK, 0);

	pistachio_internal_dac_reg_writel(dac->regmap,
					PISTACHIO_INTERNAL_DAC_PWR_MASK,
					PISTACHIO_INTERNAL_DAC_PWR);

	regmap_update_bits(dac->regmap, PISTACHIO_INTERNAL_DAC_CTRL,
			PISTACHIO_INTERNAL_DAC_CTRL_PWRDN_MASK, 0);
}

static struct snd_soc_dai_driver pistachio_internal_dac_dais[] = {
	{
		.name = "pistachio_internal_dac",
		.playback = {
			.stream_name = "Playback",
			.channels_min = 2,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_48000,
			.formats = PISTACHIO_INTERNAL_DAC_FORMATS,
		}
	},
};

static int pistachio_internal_dac_codec_probe(struct snd_soc_codec *codec)
{
	struct pistachio_internal_dac *dac = snd_soc_codec_get_drvdata(codec);

	snd_soc_codec_init_regmap(codec, dac->regmap);

	return 0;
}

static const struct snd_soc_codec_driver pistachio_internal_dac_driver = {
	.probe = pistachio_internal_dac_codec_probe,
	.idle_bias_off = true,
	.component_driver = {
		.controls		= pistachio_internal_dac_snd_controls,
		.num_controls		= ARRAY_SIZE(pistachio_internal_dac_snd_controls),
		.dapm_widgets		= pistachio_internal_dac_widgets,
		.num_dapm_widgets	= ARRAY_SIZE(pistachio_internal_dac_widgets),
		.dapm_routes		= pistachio_internal_dac_routes,
		.num_dapm_routes	= ARRAY_SIZE(pistachio_internal_dac_routes),
	},
};

static int pistachio_internal_dac_probe(struct platform_device *pdev)
{
	struct pistachio_internal_dac *dac;
	int ret, voltage;
	struct device *dev = &pdev->dev;
	u32 reg;

	dac = devm_kzalloc(dev, sizeof(*dac), GFP_KERNEL);

	if (!dac)
		return -ENOMEM;

	platform_set_drvdata(pdev, dac);

	dac->regmap = syscon_regmap_lookup_by_phandle(pdev->dev.of_node,
							    "img,cr-top");
	if (IS_ERR(dac->regmap))
		return PTR_ERR(dac->regmap);

	dac->supply = devm_regulator_get(dev, "VDD");
	if (IS_ERR(dac->supply)) {
		ret = PTR_ERR(dac->supply);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "failed to acquire supply 'VDD-supply': %d\n", ret);
		return ret;
	}

	ret = regulator_enable(dac->supply);
	if (ret) {
		dev_err(dev, "failed to enable supply: %d\n", ret);
		return ret;
	}

	voltage = regulator_get_voltage(dac->supply);

	switch (voltage) {
	case 1800000:
		reg = 0;
		break;
	case 3300000:
		reg = PISTACHIO_INTERNAL_DAC_CTRL_PWR_SEL_MASK;
		break;
	default:
		dev_err(dev, "invalid voltage: %d\n", voltage);
		ret = -EINVAL;
		goto err_regulator;
	}

	regmap_update_bits(dac->regmap, PISTACHIO_INTERNAL_DAC_CTRL,
			PISTACHIO_INTERNAL_DAC_CTRL_PWR_SEL_MASK, reg);

	pistachio_internal_dac_pwr_off(dac);
	pistachio_internal_dac_pwr_on(dac);

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

	ret = snd_soc_register_codec(dev, &pistachio_internal_dac_driver,
			pistachio_internal_dac_dais,
			ARRAY_SIZE(pistachio_internal_dac_dais));
	if (ret) {
		dev_err(dev, "failed to register codec: %d\n", ret);
		goto err_pwr;
	}

	return 0;

err_pwr:
	pm_runtime_disable(&pdev->dev);
	pistachio_internal_dac_pwr_off(dac);
err_regulator:
	regulator_disable(dac->supply);

	return ret;
}

static int pistachio_internal_dac_remove(struct platform_device *pdev)
{
	struct pistachio_internal_dac *dac = dev_get_drvdata(&pdev->dev);

	snd_soc_unregister_codec(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	pistachio_internal_dac_pwr_off(dac);
	regulator_disable(dac->supply);

	return 0;
}

#ifdef CONFIG_PM
static int pistachio_internal_dac_rt_resume(struct device *dev)
{
	struct pistachio_internal_dac *dac = dev_get_drvdata(dev);
	int ret;

	ret = regulator_enable(dac->supply);
	if (ret) {
		dev_err(dev, "failed to enable supply: %d\n", ret);
		return ret;
	}

	pistachio_internal_dac_pwr_on(dac);

	return 0;
}

static int pistachio_internal_dac_rt_suspend(struct device *dev)
{
	struct pistachio_internal_dac *dac = dev_get_drvdata(dev);

	pistachio_internal_dac_pwr_off(dac);

	regulator_disable(dac->supply);

	return 0;
}
#endif

static const struct dev_pm_ops pistachio_internal_dac_pm_ops = {
	SET_RUNTIME_PM_OPS(pistachio_internal_dac_rt_suspend,
			pistachio_internal_dac_rt_resume, NULL)
};

static const struct of_device_id pistachio_internal_dac_of_match[] = {
	{ .compatible = "img,pistachio-internal-dac" },
	{}
};
MODULE_DEVICE_TABLE(of, pistachio_internal_dac_of_match);

static struct platform_driver pistachio_internal_dac_plat_driver = {
	.driver = {
		.name = "img-pistachio-internal-dac",
		.of_match_table = pistachio_internal_dac_of_match,
		.pm = &pistachio_internal_dac_pm_ops
	},
	.probe = pistachio_internal_dac_probe,
	.remove = pistachio_internal_dac_remove
};
module_platform_driver(pistachio_internal_dac_plat_driver);

MODULE_DESCRIPTION("Pistachio Internal DAC driver");
MODULE_AUTHOR("Damien Horsley <Damien.Horsley@imgtec.com>");
MODULE_LICENSE("GPL v2");
