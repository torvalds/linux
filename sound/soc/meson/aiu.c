// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2020 BayLibre, SAS.
// Author: Jerome Brunet <jbrunet@baylibre.com>

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>

#include <dt-bindings/sound/meson-aiu.h>
#include "aiu.h"
#include "aiu-fifo.h"

#define AIU_I2S_MISC_958_SRC_SHIFT 3

static const char * const aiu_spdif_encode_sel_texts[] = {
	"SPDIF", "I2S",
};

static SOC_ENUM_SINGLE_DECL(aiu_spdif_encode_sel_enum, AIU_I2S_MISC,
			    AIU_I2S_MISC_958_SRC_SHIFT,
			    aiu_spdif_encode_sel_texts);

static const struct snd_kcontrol_new aiu_spdif_encode_mux =
	SOC_DAPM_ENUM("SPDIF Buffer Src", aiu_spdif_encode_sel_enum);

static const struct snd_soc_dapm_widget aiu_cpu_dapm_widgets[] = {
	SND_SOC_DAPM_MUX("SPDIF SRC SEL", SND_SOC_NOPM, 0, 0,
			 &aiu_spdif_encode_mux),
};

static const struct snd_soc_dapm_route aiu_cpu_dapm_routes[] = {
	{ "I2S Encoder Playback", NULL, "I2S FIFO Playback" },
	{ "SPDIF SRC SEL", "SPDIF", "SPDIF FIFO Playback" },
	{ "SPDIF SRC SEL", "I2S", "I2S FIFO Playback" },
	{ "SPDIF Encoder Playback", NULL, "SPDIF SRC SEL" },
};

int aiu_of_xlate_dai_name(struct snd_soc_component *component,
			  const struct of_phandle_args *args,
			  const char **dai_name,
			  unsigned int component_id)
{
	struct snd_soc_dai *dai;
	int id;

	if (args->args_count != 2)
		return -EINVAL;

	if (args->args[0] != component_id)
		return -EINVAL;

	id = args->args[1];

	if (id < 0 || id >= component->num_dai)
		return -EINVAL;

	for_each_component_dais(component, dai) {
		if (id == 0)
			break;
		id--;
	}

	*dai_name = dai->driver->name;

	return 0;
}

static int aiu_cpu_of_xlate_dai_name(struct snd_soc_component *component,
				     const struct of_phandle_args *args,
				     const char **dai_name)
{
	return aiu_of_xlate_dai_name(component, args, dai_name, AIU_CPU);
}

static int aiu_cpu_component_probe(struct snd_soc_component *component)
{
	struct aiu *aiu = snd_soc_component_get_drvdata(component);

	/* Required for the SPDIF Source control operation */
	return clk_prepare_enable(aiu->i2s.clks[PCLK].clk);
}

static void aiu_cpu_component_remove(struct snd_soc_component *component)
{
	struct aiu *aiu = snd_soc_component_get_drvdata(component);

	clk_disable_unprepare(aiu->i2s.clks[PCLK].clk);
}

static const struct snd_soc_component_driver aiu_cpu_component = {
	.name			= "AIU CPU",
	.dapm_widgets		= aiu_cpu_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(aiu_cpu_dapm_widgets),
	.dapm_routes		= aiu_cpu_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(aiu_cpu_dapm_routes),
	.of_xlate_dai_name	= aiu_cpu_of_xlate_dai_name,
	.pointer		= aiu_fifo_pointer,
	.probe			= aiu_cpu_component_probe,
	.remove			= aiu_cpu_component_remove,
#ifdef CONFIG_DEBUG_FS
	.debugfs_prefix		= "cpu",
#endif
};

static struct snd_soc_dai_driver aiu_cpu_dai_drv[] = {
	[CPU_I2S_FIFO] = {
		.name = "I2S FIFO",
		.playback = {
			.stream_name	= "I2S FIFO Playback",
			.channels_min	= 2,
			.channels_max	= 8,
			.rates		= SNDRV_PCM_RATE_CONTINUOUS,
			.rate_min	= 5512,
			.rate_max	= 192000,
			.formats	= AIU_FORMATS,
		},
		.ops		= &aiu_fifo_i2s_dai_ops,
		.pcm_new	= aiu_fifo_pcm_new,
		.probe		= aiu_fifo_i2s_dai_probe,
		.remove		= aiu_fifo_dai_remove,
	},
	[CPU_SPDIF_FIFO] = {
		.name = "SPDIF FIFO",
		.playback = {
			.stream_name	= "SPDIF FIFO Playback",
			.channels_min	= 2,
			.channels_max	= 2,
			.rates		= SNDRV_PCM_RATE_CONTINUOUS,
			.rate_min	= 5512,
			.rate_max	= 192000,
			.formats	= AIU_FORMATS,
		},
		.ops		= &aiu_fifo_spdif_dai_ops,
		.pcm_new	= aiu_fifo_pcm_new,
		.probe		= aiu_fifo_spdif_dai_probe,
		.remove		= aiu_fifo_dai_remove,
	},
	[CPU_I2S_ENCODER] = {
		.name = "I2S Encoder",
		.playback = {
			.stream_name = "I2S Encoder Playback",
			.channels_min = 2,
			.channels_max = 8,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = AIU_FORMATS,
		},
		.ops = &aiu_encoder_i2s_dai_ops,
	},
	[CPU_SPDIF_ENCODER] = {
		.name = "SPDIF Encoder",
		.playback = {
			.stream_name = "SPDIF Encoder Playback",
			.channels_min = 2,
			.channels_max = 2,
			.rates = (SNDRV_PCM_RATE_32000  |
				  SNDRV_PCM_RATE_44100  |
				  SNDRV_PCM_RATE_48000  |
				  SNDRV_PCM_RATE_88200  |
				  SNDRV_PCM_RATE_96000  |
				  SNDRV_PCM_RATE_176400 |
				  SNDRV_PCM_RATE_192000),
			.formats = AIU_FORMATS,
		},
		.ops = &aiu_encoder_spdif_dai_ops,
	}
};

static const struct regmap_config aiu_regmap_cfg = {
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
	.max_register	= 0x2ac,
};

static int aiu_clk_bulk_get(struct device *dev,
			    const char * const *ids,
			    unsigned int num,
			    struct aiu_interface *interface)
{
	struct clk_bulk_data *clks;
	int i, ret;

	clks = devm_kcalloc(dev, num, sizeof(*clks), GFP_KERNEL);
	if (!clks)
		return -ENOMEM;

	for (i = 0; i < num; i++)
		clks[i].id = ids[i];

	ret = devm_clk_bulk_get(dev, num, clks);
	if (ret < 0)
		return ret;

	interface->clks = clks;
	interface->clk_num = num;
	return 0;
}

static const char * const aiu_i2s_ids[] = {
	[PCLK]	= "i2s_pclk",
	[AOCLK]	= "i2s_aoclk",
	[MCLK]	= "i2s_mclk",
	[MIXER]	= "i2s_mixer",
};

static const char * const aiu_spdif_ids[] = {
	[PCLK]	= "spdif_pclk",
	[AOCLK]	= "spdif_aoclk",
	[MCLK]	= "spdif_mclk_sel"
};

static int aiu_clk_get(struct device *dev)
{
	struct aiu *aiu = dev_get_drvdata(dev);
	struct clk *pclk;
	int ret;

	pclk = devm_clk_get_enabled(dev, "pclk");
	if (IS_ERR(pclk))
		return dev_err_probe(dev, PTR_ERR(pclk), "Can't get the aiu pclk\n");

	aiu->spdif_mclk = devm_clk_get(dev, "spdif_mclk");
	if (IS_ERR(aiu->spdif_mclk))
		return dev_err_probe(dev, PTR_ERR(aiu->spdif_mclk),
				     "Can't get the aiu spdif master clock\n");

	ret = aiu_clk_bulk_get(dev, aiu_i2s_ids, ARRAY_SIZE(aiu_i2s_ids),
			       &aiu->i2s);
	if (ret)
		return dev_err_probe(dev, ret, "Can't get the i2s clocks\n");

	ret = aiu_clk_bulk_get(dev, aiu_spdif_ids, ARRAY_SIZE(aiu_spdif_ids),
			       &aiu->spdif);
	if (ret)
		return dev_err_probe(dev, ret, "Can't get the spdif clocks\n");

	return ret;
}

static int aiu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	void __iomem *regs;
	struct regmap *map;
	struct aiu *aiu;
	int ret;

	aiu = devm_kzalloc(dev, sizeof(*aiu), GFP_KERNEL);
	if (!aiu)
		return -ENOMEM;

	aiu->platform = device_get_match_data(dev);
	if (!aiu->platform)
		return -ENODEV;

	platform_set_drvdata(pdev, aiu);

	ret = device_reset(dev);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to reset device\n");

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	map = devm_regmap_init_mmio(dev, regs, &aiu_regmap_cfg);
	if (IS_ERR(map)) {
		dev_err(dev, "failed to init regmap: %ld\n",
			PTR_ERR(map));
		return PTR_ERR(map);
	}

	aiu->i2s.irq = platform_get_irq_byname(pdev, "i2s");
	if (aiu->i2s.irq < 0)
		return aiu->i2s.irq;

	aiu->spdif.irq = platform_get_irq_byname(pdev, "spdif");
	if (aiu->spdif.irq < 0)
		return aiu->spdif.irq;

	ret = aiu_clk_get(dev);
	if (ret)
		return ret;

	/* Register the cpu component of the aiu */
	ret = snd_soc_register_component(dev, &aiu_cpu_component,
					 aiu_cpu_dai_drv,
					 ARRAY_SIZE(aiu_cpu_dai_drv));
	if (ret) {
		dev_err(dev, "Failed to register cpu component\n");
		return ret;
	}

	/* Register the hdmi codec control component */
	ret = aiu_hdmi_ctrl_register_component(dev);
	if (ret) {
		dev_err(dev, "Failed to register hdmi control component\n");
		goto err;
	}

	/* Register the internal dac control component on gxl */
	if (aiu->platform->has_acodec) {
		ret = aiu_acodec_ctrl_register_component(dev);
		if (ret) {
			dev_err(dev,
			    "Failed to register acodec control component\n");
			goto err;
		}
	}

	return 0;
err:
	snd_soc_unregister_component(dev);
	return ret;
}

static int aiu_remove(struct platform_device *pdev)
{
	snd_soc_unregister_component(&pdev->dev);

	return 0;
}

static const struct aiu_platform_data aiu_gxbb_pdata = {
	.has_acodec = false,
	.has_clk_ctrl_more_i2s_div = true,
};

static const struct aiu_platform_data aiu_gxl_pdata = {
	.has_acodec = true,
	.has_clk_ctrl_more_i2s_div = true,
};

static const struct aiu_platform_data aiu_meson8_pdata = {
	.has_acodec = false,
	.has_clk_ctrl_more_i2s_div = false,
};

static const struct of_device_id aiu_of_match[] = {
	{ .compatible = "amlogic,aiu-gxbb", .data = &aiu_gxbb_pdata },
	{ .compatible = "amlogic,aiu-gxl", .data = &aiu_gxl_pdata },
	{ .compatible = "amlogic,aiu-meson8", .data = &aiu_meson8_pdata },
	{ .compatible = "amlogic,aiu-meson8b", .data = &aiu_meson8_pdata },
	{}
};
MODULE_DEVICE_TABLE(of, aiu_of_match);

static struct platform_driver aiu_pdrv = {
	.probe = aiu_probe,
	.remove = aiu_remove,
	.driver = {
		.name = "meson-aiu",
		.of_match_table = aiu_of_match,
	},
};
module_platform_driver(aiu_pdrv);

MODULE_DESCRIPTION("Meson AIU Driver");
MODULE_AUTHOR("Jerome Brunet <jbrunet@baylibre.com>");
MODULE_LICENSE("GPL v2");
