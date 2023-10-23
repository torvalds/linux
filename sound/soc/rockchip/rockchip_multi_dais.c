// SPDX-License-Identifier: GPL-2.0
/*
 * ALSA SoC Audio Layer - Rockchip Multi-DAIS  driver
 *
 * Copyright (c) 2018 Rockchip Electronics Co. Ltd.
 * Author: Sugar Zhang <sugar.zhang@rock-chips.com>
 *
 */

#include <linux/module.h>
#include <linux/mfd/syscon.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "rockchip_multi_dais.h"

#define BITCLOCK_INV_STR	"bitclock-inversion"
#define FRAME_INV_STR		"frame-inversion"
#define BITCLOCK_MASTER_STR	"bitclock-master"
#define FRAME_MASTER_STR	"frame-master"
#define DAIS_DRV_NAME		"rockchip-mdais"
#define RK3308_GRF_SOC_CON2	0x308

#define SOUND_NAME_PREFIX	"sound-name-prefix"

static inline struct rk_mdais_dev *to_info(struct snd_soc_dai *dai)
{
	return snd_soc_dai_get_drvdata(dai);
}

static inline unsigned int *mdais_channel_maps(struct rk_mdais_dev *mdais,
					       struct snd_pcm_substream *substream)
{
	return substream->stream ? mdais->capture_channel_maps :
				   mdais->playback_channel_maps;
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

	channel_maps = mdais_channel_maps(mdais, substream);

	for (i = 0; i < mdais->num_dais; i++) {
		child = mdais->dais[i].dai;
		if (!channel_maps[i])
			continue;

		hw_refine_channels(cparams, channel_maps[i]);
		if (child->driver->ops && child->driver->ops->hw_params) {
			ret = child->driver->ops->hw_params(substream, cparams, child);
			if (ret < 0) {
				dev_err(dai->dev, "Failed to set %s hw params: %d\n",
					dai->name, ret);
				break;
			}
		}
	}

	kfree(cparams);

	return ret;
}

static int rockchip_mdais_hw_free(struct snd_pcm_substream *substream,
				  struct snd_soc_dai *dai)
{
	struct rk_mdais_dev *mdais = to_info(dai);
	struct snd_soc_dai *child;
	unsigned int *channel_maps;
	int ret = 0, i = 0;

	channel_maps = mdais_channel_maps(mdais, substream);

	for (i = 0; i < mdais->num_dais; i++) {
		child = mdais->dais[i].dai;
		if (!channel_maps[i])
			continue;

		if (child->driver->ops && child->driver->ops->hw_free) {
			ret = child->driver->ops->hw_free(substream, child);
			if (ret < 0)
				return ret;
		}
	}

	return 0;
}

static int rockchip_mdais_trigger(struct snd_pcm_substream *substream,
				  int cmd, struct snd_soc_dai *dai)
{
	struct rk_mdais_dev *mdais = to_info(dai);
	struct snd_soc_dai *child;
	unsigned int *channel_maps;
	int ret = 0, i = 0;

	channel_maps = mdais_channel_maps(mdais, substream);

	for (i = 0; i < mdais->num_dais; i++) {
		/* skip DAIs which have no channel mapping */
		if (!channel_maps[i])
			continue;

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

static int rockchip_mdais_startup(struct snd_pcm_substream *substream,
				  struct snd_soc_dai *dai)
{
	struct rk_mdais_dev *mdais = to_info(dai);
	struct snd_soc_dai *child;
	unsigned int *channel_maps;
	int ret = 0, i = 0;

	channel_maps = mdais_channel_maps(mdais, substream);

	for (i = 0; i < mdais->num_dais; i++) {
		if (!channel_maps[i])
			continue;

		child = mdais->dais[i].dai;
		if (child->driver->ops && child->driver->ops->startup) {
			ret = child->driver->ops->startup(substream, child);
			if (ret < 0)
				return ret;
		}
	}

	return 0;
}

static void rockchip_mdais_shutdown(struct snd_pcm_substream *substream,
				  struct snd_soc_dai *dai)
{
	struct rk_mdais_dev *mdais = to_info(dai);
	struct snd_soc_dai *child;
	unsigned int *channel_maps;
	int i = 0;

	channel_maps = mdais_channel_maps(mdais, substream);

	for (i = 0; i < mdais->num_dais; i++) {
		if (!channel_maps[i])
			continue;

		child = mdais->dais[i].dai;
		if (child->driver->ops && child->driver->ops->shutdown) {
			child->driver->ops->shutdown(substream, child);
		}
	}
}

static int rockchip_mdais_prepare(struct snd_pcm_substream *substream,
				  struct snd_soc_dai *dai)
{
	struct rk_mdais_dev *mdais = to_info(dai);
	struct snd_soc_dai *child;
	unsigned int *channel_maps;
	int ret = 0, i = 0;

	channel_maps = mdais_channel_maps(mdais, substream);

	for (i = 0; i < mdais->num_dais; i++) {
		if (!channel_maps[i])
			continue;

		child = mdais->dais[i].dai;
		if (child->driver->ops && child->driver->ops->prepare) {
			ret = child->driver->ops->prepare(substream, child);
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
	unsigned int dai_fmt;
	int ret, i = 0;

	for (i = 0; i < mdais->num_dais; i++) {
		child = mdais->dais[i].dai;
		dai_fmt = fmt;
		if (mdais->dais[i].fmt_msk) {
			dai_fmt &= ~(mdais->dais[i].fmt_msk);
			dai_fmt |= mdais->dais[i].fmt;
		}
		ret = snd_soc_dai_set_fmt(child, dai_fmt);
		if (ret && ret != -ENOTSUPP)
			return ret;
	}

	return 0;
}

static int rockchip_mdais_tdm_slot(struct snd_soc_dai *dai,
				   unsigned int tx_mask, unsigned int rx_mask,
				   int slots, int slot_width)
{
	struct rk_mdais_dev *mdais = to_info(dai);
	struct snd_soc_dai *child;
	int ret, i = 0;

	for (i = 0; i < mdais->num_dais; i++) {
		child = mdais->dais[i].dai;
		ret = snd_soc_dai_set_tdm_slot(child, tx_mask, rx_mask,
					       slots, slot_width);
		if (ret && ret != -ENOTSUPP)
			return ret;
	}

	return 0;
}

static int rockchip_mdais_dai_probe(struct snd_soc_dai *dai)
{
	struct rk_mdais_dev *mdais = to_info(dai);
	struct snd_soc_component *comp;
	struct snd_soc_dai *child;
	const char *str;
	int ret, i = 0;

	for (i = 0; i < mdais->num_dais; i++) {
		child = mdais->dais[i].dai;
		comp = child->component;
		if (!child->probed && child->driver->probe) {
			if (!comp->name_prefix) {
				ret = device_property_read_string(child->dev,
								  SOUND_NAME_PREFIX, &str);
				if (!ret)
					comp->name_prefix = str;
			}

			comp->card = dai->component->card;
			ret = child->driver->probe(child);
			if (ret < 0) {
				dev_err(child->dev,
					"Failed to probe DAI %s: %d\n",
					child->name, ret);
				return ret;
			}

			ret = snd_soc_add_component_controls(comp,
							     comp->driver->controls,
							     comp->driver->num_controls);
			if (ret)
				dev_err(dai->dev, "%s: Failed to add controls, should add '%s' in DT\n",
					dev_name(child->dev), SOUND_NAME_PREFIX);

			dai->probed = 1;
		}
	}

	return 0;
}

static const struct snd_soc_dai_ops rockchip_mdais_dai_ops = {
	.hw_params = rockchip_mdais_hw_params,
	.hw_free = rockchip_mdais_hw_free,
	.set_sysclk = rockchip_mdais_set_sysclk,
	.set_fmt = rockchip_mdais_set_fmt,
	.set_tdm_slot = rockchip_mdais_tdm_slot,
	.trigger = rockchip_mdais_trigger,
	.startup = rockchip_mdais_startup,
	.shutdown = rockchip_mdais_shutdown,
	.prepare = rockchip_mdais_prepare,
};

static const struct snd_soc_component_driver rockchip_mdais_component = {
	.name = DAIS_DRV_NAME,
};

static const struct of_device_id rockchip_mdais_match[] = {
	{ .compatible = "rockchip,multi-dais", },
	{ .compatible = "rockchip,rk3308-multi-dais", },
	{},
};

static struct snd_soc_dai *rockchip_mdais_find_dai(struct device_node *np)
{
	struct snd_soc_dai_link_component dai_component = { 0 };

	dai_component.of_node = np;

	return snd_soc_find_dai_with_mutex(&dai_component);
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

static int mdais_read_prop_array(struct device_node *node,
				 const char *propname,
				 unsigned int *array, int num)
{
	int ret = 0;

	memset(array, 0, sizeof(*array) * num);
	if (of_property_read_bool(node, propname)) {
		ret = of_property_read_u32_array(node, propname, array, num);
		if (ret)
			ret = -EINVAL;
	} else {
		ret = -EINVAL;
	}

	return ret;
}

static void mdais_parse_daifmt(struct device_node *node, struct rk_dai *dais,
			       int num_dai)
{
	unsigned int cinv[MAX_DAIS], finv[MAX_DAIS];
	unsigned int cmst[MAX_DAIS], fmst[MAX_DAIS];
	unsigned int format = 0, format_mask = 0;
	int i = 0, ret = 0;

	ret = mdais_read_prop_array(node, BITCLOCK_INV_STR, cinv, num_dai);
	if (!ret)
		format_mask |= SND_SOC_DAIFMT_INV_MASK;
	ret = mdais_read_prop_array(node, FRAME_INV_STR, finv, num_dai);
	if (!ret)
		format_mask |= SND_SOC_DAIFMT_INV_MASK;
	ret = mdais_read_prop_array(node, BITCLOCK_MASTER_STR, cmst, num_dai);
	if (!ret)
		format_mask |= SND_SOC_DAIFMT_MASTER_MASK;
	ret = mdais_read_prop_array(node, FRAME_MASTER_STR, fmst, num_dai);
	if (!ret)
		format_mask |= SND_SOC_DAIFMT_MASTER_MASK;

	for (i = 0; i < num_dai; i++) {
		format = 0;

		switch ((cinv[i] << 4) + finv[i]) {
		case 0x11:
			format |= SND_SOC_DAIFMT_IB_IF;
			break;
		case 0x10:
			format |= SND_SOC_DAIFMT_IB_NF;
			break;
		case 0x01:
			format |= SND_SOC_DAIFMT_NB_IF;
			break;
		default:
			/* SND_SOC_DAIFMT_NB_NF is default */
			break;
		}

		switch ((!cmst[i] << 4) + !fmst[i]) {
		case 0x11:
			format |= SND_SOC_DAIFMT_CBM_CFM;
			break;
		case 0x10:
			format |= SND_SOC_DAIFMT_CBM_CFS;
			break;
		case 0x01:
			format |= SND_SOC_DAIFMT_CBS_CFM;
			break;
		default:
			format |= SND_SOC_DAIFMT_CBS_CFS;
			break;
		}

		dais[i].fmt = format & format_mask;
		dais[i].fmt_msk = format_mask;
	}
}

static int rockchip_mdais_dai_prepare(struct platform_device *pdev,
				      struct snd_soc_dai_driver **soc_dai)
{
	struct snd_soc_dai_driver rockchip_mdais_dai = {
		.probe = rockchip_mdais_dai_probe,
		.playback = {
			.stream_name = "Playback",
			.channels_min = 1,
			.channels_max = 512,
			.rates = SNDRV_PCM_RATE_8000_384000,
			.formats = (SNDRV_PCM_FMTBIT_S8 |
				    SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_S20_3LE |
				    SNDRV_PCM_FMTBIT_S24_LE |
				    SNDRV_PCM_FMTBIT_S32_LE),
		},
		.capture = {
			.stream_name = "Capture",
			.channels_min = 1,
			.channels_max = 512,
			.rates = SNDRV_PCM_RATE_8000_384000,
			.formats = (SNDRV_PCM_FMTBIT_S8 |
				    SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_S20_3LE |
				    SNDRV_PCM_FMTBIT_S24_LE |
				    SNDRV_PCM_FMTBIT_S32_LE),
		},
		.ops = &rockchip_mdais_dai_ops,
	};

	*soc_dai = devm_kmemdup(&pdev->dev, &rockchip_mdais_dai,
				sizeof(rockchip_mdais_dai), GFP_KERNEL);
	if (!(*soc_dai))
		return -ENOMEM;

	return 0;
}

static void mdais_fixup_dai(struct snd_soc_dai_driver *soc_dai,
			    struct rk_mdais_dev *mdais)
{
	int i, tch, rch;
	unsigned int *tx_maps, *rx_maps;

	tch = 0;
	rch = 0;
	tx_maps = mdais->playback_channel_maps;
	rx_maps = mdais->capture_channel_maps;
	for (i = 0; i < mdais->num_dais; i++) {
		tch += tx_maps[i];
		rch += rx_maps[i];
	}

	soc_dai->playback.channels_min = tch;
	soc_dai->playback.channels_max = tch;
	soc_dai->capture.channels_min = rch;
	soc_dai->capture.channels_max = rch;
}

static int rockchip_mdais_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct platform_device  *sub_pdev;
	struct rk_mdais_dev *mdais;
	struct device_node *node;
	struct snd_soc_dai_driver *soc_dai;
	struct rk_dai *dais;
	unsigned int *map;
	int count, mp_count;
	int ret = 0, i = 0;

	ret = rockchip_mdais_dai_prepare(pdev, &soc_dai);
	if (ret < 0)
		return ret;

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

	mdais_parse_daifmt(np, dais, count);
	mdais_fixup_dai(soc_dai, mdais);

	if (of_device_is_compatible(np, "rockchip,rk3308-multi-dais")) {
		struct regmap *grf;
		const char *name;
		unsigned int i2s0_fmt = 0, i2s1_fmt = 0;

		for (i = 0; i < count; i++) {
			name = dev_name(dais[i].dev);
			if (strstr(name, "ff300000"))
				i2s0_fmt = dais[i].fmt;
			else if (strstr(name, "ff310000"))
				i2s1_fmt = dais[i].fmt;
		}
		i2s0_fmt &= SND_SOC_DAIFMT_MASTER_MASK;
		i2s1_fmt &= SND_SOC_DAIFMT_MASTER_MASK;

		if ((i2s0_fmt == SND_SOC_DAIFMT_CBS_CFS &&
		     i2s1_fmt == SND_SOC_DAIFMT_CBM_CFM) ||
		    (i2s0_fmt == SND_SOC_DAIFMT_CBM_CFM &&
		     i2s1_fmt == SND_SOC_DAIFMT_CBS_CFS)) {
			grf = syscon_regmap_lookup_by_phandle(np,
							      "rockchip,grf");
			if (IS_ERR(grf))
				return PTR_ERR(grf);

			dev_info(&pdev->dev, "enable i2s 16ch ctrl en\n");
			regmap_write(grf, RK3308_GRF_SOC_CON2,
				     BIT(14) << 16 | BIT(14));
		}
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
					      soc_dai, 1);

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
