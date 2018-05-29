// SPDX-License-Identifier: GPL-2.0
/*
 * ALSA SoC Audio Layer - Rockchip Multi-DAIS  driver
 *
 * Copyright (c) 2018 Rockchip Electronics Co. Ltd.
 * Author: Sugar Zhang <sugar.zhang@rock-chips.com>
 *
 */

#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "rockchip_multi_dais.h"

#define DAIS_DRV_NAME "rockchip-mdais"

static inline struct rk_mdais_dev *to_info(struct snd_soc_dai *dai)
{
	return snd_soc_dai_get_drvdata(dai);
}

static void hw_refine_channels(struct snd_pcm_hw_params *params,
			       unsigned int channel)
{
	struct snd_interval *c =
		hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);

	c->min = channel;
	c->max = channel;
}

static int rockchip_mdais_hw_params(struct snd_pcm_substream *substream,
				    struct snd_pcm_hw_params *params,
				    struct snd_soc_dai *dai)
{
	struct rk_mdais_dev *mdais = to_info(dai);
	struct snd_pcm_hw_params *cparams;
	struct snd_soc_dai *child;
	unsigned int *channel_maps;
	int ret = 0, i = 0;

	cparams = kmemdup(params, sizeof(*params), GFP_KERNEL);
	if (IS_ERR(cparams))
		return PTR_ERR(cparams);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		channel_maps = mdais->playback_channel_maps;
	else
		channel_maps = mdais->capture_channel_maps;

	for (i = 0; i < mdais->num_dais; i++) {
		child = mdais->dais[i].dai;
		if (channel_maps[i])
			hw_refine_channels(cparams, channel_maps[i]);
		if (child->driver->ops && child->driver->ops->hw_params) {
			ret = child->driver->ops->hw_params(substream, cparams, child);
			if (ret < 0) {
				dev_err(dai->dev, "ASoC: can't set %s hw params: %d\n",
					dai->name, ret);
				return ret;
			}
		}
	}

	kfree(cparams);
	return 0;
}

static int rockchip_mdais_trigger(struct snd_pcm_substream *substream,
				  int cmd, struct snd_soc_dai *dai)
{
	struct rk_mdais_dev *mdais = to_info(dai);
	struct snd_soc_dai *child;
	int ret = 0, i = 0;

	for (i = 0; i < mdais->num_dais; i++) {
		child = mdais->dais[i].dai;
		if (child->driver->ops && child->driver->ops->trigger) {
			ret = child->driver->ops->trigger(substream,
							  cmd, child);
			if (ret < 0)
				return ret;
		}
	}

	return 0;
}

static int rockchip_mdais_set_sysclk(struct snd_soc_dai *cpu_dai, int clk_id,
				     unsigned int freq, int dir)
{
	struct rk_mdais_dev *mdais = to_info(cpu_dai);
	struct snd_soc_dai *child;
	int ret, i = 0;

	for (i = 0; i < mdais->num_dais; i++) {
		child = mdais->dais[i].dai;
		ret = snd_soc_dai_set_sysclk(child, clk_id, freq, dir);
		if (ret && ret != -ENOTSUPP)
			return ret;
	}

	return 0;
}

static int rockchip_mdais_set_fmt(struct snd_soc_dai *cpu_dai,
				  unsigned int fmt)
{
	struct rk_mdais_dev *mdais = to_info(cpu_dai);
	struct snd_soc_dai *child;
	int ret, i = 0;

	for (i = 0; i < mdais->num_dais; i++) {
		child = mdais->dais[i].dai;
		ret = snd_soc_dai_set_fmt(child, fmt);
		if (ret && ret != -ENOTSUPP)
			return ret;
	}

	return 0;
}

static int rockchip_mdais_dai_probe(struct snd_soc_dai *dai)
{
	struct rk_mdais_dev *mdais = to_info(dai);
	struct snd_soc_dai *child;
	int ret, i = 0;

	for (i = 0; i < mdais->num_dais; i++) {
		child = mdais->dais[i].dai;
		if (!child->probed && child->driver->probe) {
			ret = child->driver->probe(child);
			if (ret < 0) {
				dev_err(child->dev,
					"ASoC: failed to probe DAI %s: %d\n",
					child->name, ret);
				return ret;
			}
			dai->probed = 1;
		}
	}

	return 0;
}

static const struct snd_soc_dai_ops rockchip_mdais_dai_ops = {
	.hw_params = rockchip_mdais_hw_params,
	.set_sysclk = rockchip_mdais_set_sysclk,
	.set_fmt = rockchip_mdais_set_fmt,
	.trigger = rockchip_mdais_trigger,
};

static struct snd_soc_dai_driver rockchip_mdais_dai = {
	.probe = rockchip_mdais_dai_probe,
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 32,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = (SNDRV_PCM_FMTBIT_S8 |
			    SNDRV_PCM_FMTBIT_S16_LE |
			    SNDRV_PCM_FMTBIT_S20_3LE |
			    SNDRV_PCM_FMTBIT_S24_LE |
			    SNDRV_PCM_FMTBIT_S32_LE),
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 2,
		.channels_max = 32,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = (SNDRV_PCM_FMTBIT_S8 |
			    SNDRV_PCM_FMTBIT_S16_LE |
			    SNDRV_PCM_FMTBIT_S20_3LE |
			    SNDRV_PCM_FMTBIT_S24_LE |
			    SNDRV_PCM_FMTBIT_S32_LE),
	},
	.ops = &rockchip_mdais_dai_ops,
};

static const struct snd_soc_component_driver rockchip_mdais_component = {
	.name = DAIS_DRV_NAME,
};

static const struct of_device_id rockchip_mdais_match[] = {
	{ .compatible = "rockchip,multi-dais", },
	{},
};

static struct snd_soc_dai *rockchip_mdais_find_dai(struct device_node *np)
{
	struct snd_soc_dai_link_component dai_component = { 0 };

	dai_component.of_node = np;

	return snd_soc_find_dai(&dai_component);
}

static int mdais_runtime_suspend(struct device *dev)
{
	struct rk_mdais_dev *mdais = dev_get_drvdata(dev);
	struct snd_soc_dai *child;
	int i = 0;

	for (i = 0; i < mdais->num_dais; i++) {
		child = mdais->dais[i].dai;
		pm_runtime_put(child->dev);
	}

	return 0;
}

static int mdais_runtime_resume(struct device *dev)
{
	struct rk_mdais_dev *mdais = dev_get_drvdata(dev);
	struct snd_soc_dai *child;
	int i = 0;

	for (i = 0; i < mdais->num_dais; i++) {
		child = mdais->dais[i].dai;
		pm_runtime_get_sync(child->dev);
	}

	return 0;
}

static int rockchip_mdais_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct platform_device  *sub_pdev;
	struct rk_mdais_dev *mdais;
	struct device_node *node;
	struct rk_dai *dais;
	unsigned int *map;
	int count, mp_count;
	int ret = 0, i = 0;

	mdais = devm_kzalloc(&pdev->dev, sizeof(*mdais), GFP_KERNEL);
	if (!mdais)
		return -ENOMEM;

	count = of_count_phandle_with_args(np, "dais", NULL);
	if (count < 0 || count > MAX_DAIS)
		return -EINVAL;

	mp_count = of_property_count_u32_elems(np, "capture,channel-mapping");
	if (mp_count != count)
		return -EINVAL;
	mp_count = of_property_count_u32_elems(np, "playback,channel-mapping");
	if (mp_count != count)
		return -EINVAL;

	mdais->num_dais = count;
	dais = devm_kcalloc(&pdev->dev, count,
			    sizeof(*dais), GFP_KERNEL);
	if (!dais)
		return -ENOMEM;

	map = devm_kcalloc(&pdev->dev, count,
			   sizeof(*map), GFP_KERNEL);
	if (!map)
		return -ENOMEM;
	ret = of_property_read_u32_array(np, "capture,channel-mapping",
					 map, count);
	if (ret)
		return -EINVAL;
	mdais->capture_channel_maps = map;
	map = devm_kcalloc(&pdev->dev, count,
			   sizeof(*map), GFP_KERNEL);
	if (!map)
		return -ENOMEM;
	ret = of_property_read_u32_array(np, "playback,channel-mapping",
					 map, count);
	if (ret)
		return -EINVAL;
	mdais->playback_channel_maps = map;

	for (i = 0; i < count; i++) {
		node = of_parse_phandle(np, "dais", i);
		sub_pdev = of_find_device_by_node(node);
		if (!sub_pdev) {
			dev_err(&pdev->dev, "fail to find subnode dev\n");
			return -ENODEV;
		}
		dais[i].of_node = node;
		dais[i].dev = &sub_pdev->dev;
		dais[i].dai = rockchip_mdais_find_dai(node);
		if (!dais[i].dai)
			return -EPROBE_DEFER;
	}

	mdais->dais = dais;
	mdais->dev = &pdev->dev;
	dev_set_drvdata(&pdev->dev, mdais);

	pm_runtime_enable(&pdev->dev);
	if (!pm_runtime_enabled(&pdev->dev)) {
		ret = mdais_runtime_resume(&pdev->dev);
		if (ret)
			goto err_pm_disable;
	}

	ret = devm_snd_soc_register_component(&pdev->dev,
					      &rockchip_mdais_component,
					      &rockchip_mdais_dai, 1);

	if (ret) {
		dev_err(&pdev->dev, "could not register dai: %d\n", ret);
		goto err_suspend;
	}

	ret = snd_dmaengine_mpcm_register(mdais);
	if (ret) {
		dev_err(&pdev->dev, "Could not register PCM\n");
		return ret;
	}

	return 0;

err_suspend:
	if (!pm_runtime_status_suspended(&pdev->dev))
		mdais_runtime_resume(&pdev->dev);
err_pm_disable:
	pm_runtime_disable(&pdev->dev);

	return ret;
}

static int rockchip_mdais_remove(struct platform_device *pdev)
{
	snd_dmaengine_mpcm_unregister(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	if (!pm_runtime_status_suspended(&pdev->dev))
		mdais_runtime_suspend(&pdev->dev);

	return 0;
}

static const struct dev_pm_ops rockchip_mdais_pm_ops = {
	SET_RUNTIME_PM_OPS(mdais_runtime_suspend, mdais_runtime_resume,
			   NULL)
};

static struct platform_driver rockchip_mdais_driver = {
	.probe = rockchip_mdais_probe,
	.remove = rockchip_mdais_remove,
	.driver = {
		.name = DAIS_DRV_NAME,
		.of_match_table = of_match_ptr(rockchip_mdais_match),
		.pm = &rockchip_mdais_pm_ops,
	},
};
module_platform_driver(rockchip_mdais_driver);

MODULE_DESCRIPTION("ROCKCHIP MULTI-DAIS ASoC Interface");
MODULE_AUTHOR("Sugar Zhang <sugar.zhang@rock-chips.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DAIS_DRV_NAME);
MODULE_DEVICE_TABLE(of, rockchip_mdais_match);
