// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2010-2011,2013-2015 The Linux Foundation. All rights reserved.
 *
 * lpass-cpu.c -- ALSA SoC CPU DAI driver for QTi LPASS
 */

#include <linux/clk.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <linux/regmap.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>
#include "lpass-lpaif-reg.h"
#include "lpass.h"

#define LPASS_CPU_MAX_MI2S_LINES	4
#define LPASS_CPU_I2S_SD0_MASK		BIT(0)
#define LPASS_CPU_I2S_SD1_MASK		BIT(1)
#define LPASS_CPU_I2S_SD2_MASK		BIT(2)
#define LPASS_CPU_I2S_SD3_MASK		BIT(3)
#define LPASS_CPU_I2S_SD0_1_MASK	GENMASK(1, 0)
#define LPASS_CPU_I2S_SD2_3_MASK	GENMASK(3, 2)
#define LPASS_CPU_I2S_SD0_1_2_MASK	GENMASK(2, 0)
#define LPASS_CPU_I2S_SD0_1_2_3_MASK	GENMASK(3, 0)

static int lpass_cpu_daiops_set_sysclk(struct snd_soc_dai *dai, int clk_id,
		unsigned int freq, int dir)
{
	struct lpass_data *drvdata = snd_soc_dai_get_drvdata(dai);
	int ret;

	ret = clk_set_rate(drvdata->mi2s_osr_clk[dai->driver->id], freq);
	if (ret)
		dev_err(dai->dev, "error setting mi2s osrclk to %u: %d\n",
			freq, ret);

	return ret;
}

static int lpass_cpu_daiops_startup(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	struct lpass_data *drvdata = snd_soc_dai_get_drvdata(dai);
	int ret;

	ret = clk_prepare_enable(drvdata->mi2s_osr_clk[dai->driver->id]);
	if (ret) {
		dev_err(dai->dev, "error in enabling mi2s osr clk: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(drvdata->mi2s_bit_clk[dai->driver->id]);
	if (ret) {
		dev_err(dai->dev, "error in enabling mi2s bit clk: %d\n", ret);
		clk_disable_unprepare(drvdata->mi2s_osr_clk[dai->driver->id]);
		return ret;
	}

	return 0;
}

static void lpass_cpu_daiops_shutdown(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	struct lpass_data *drvdata = snd_soc_dai_get_drvdata(dai);

	clk_disable_unprepare(drvdata->mi2s_bit_clk[dai->driver->id]);

	clk_disable_unprepare(drvdata->mi2s_osr_clk[dai->driver->id]);
}

static int lpass_cpu_daiops_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct lpass_data *drvdata = snd_soc_dai_get_drvdata(dai);
	snd_pcm_format_t format = params_format(params);
	unsigned int channels = params_channels(params);
	unsigned int rate = params_rate(params);
	unsigned int mode;
	unsigned int regval;
	int bitwidth, ret;

	bitwidth = snd_pcm_format_width(format);
	if (bitwidth < 0) {
		dev_err(dai->dev, "invalid bit width given: %d\n", bitwidth);
		return bitwidth;
	}

	regval = LPAIF_I2SCTL_LOOPBACK_DISABLE |
			LPAIF_I2SCTL_WSSRC_INTERNAL;

	switch (bitwidth) {
	case 16:
		regval |= LPAIF_I2SCTL_BITWIDTH_16;
		break;
	case 24:
		regval |= LPAIF_I2SCTL_BITWIDTH_24;
		break;
	case 32:
		regval |= LPAIF_I2SCTL_BITWIDTH_32;
		break;
	default:
		dev_err(dai->dev, "invalid bitwidth given: %d\n", bitwidth);
		return -EINVAL;
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		mode = drvdata->mi2s_playback_sd_mode[dai->driver->id];
	else
		mode = drvdata->mi2s_capture_sd_mode[dai->driver->id];

	if (!mode) {
		dev_err(dai->dev, "no line is assigned\n");
		return -EINVAL;
	}

	switch (channels) {
	case 1:
	case 2:
		switch (mode) {
		case LPAIF_I2SCTL_MODE_QUAD01:
		case LPAIF_I2SCTL_MODE_6CH:
		case LPAIF_I2SCTL_MODE_8CH:
			mode = LPAIF_I2SCTL_MODE_SD0;
			break;
		case LPAIF_I2SCTL_MODE_QUAD23:
			mode = LPAIF_I2SCTL_MODE_SD2;
			break;
		}

		break;
	case 4:
		if (mode < LPAIF_I2SCTL_MODE_QUAD01) {
			dev_err(dai->dev, "cannot configure 4 channels with mode %d\n",
				mode);
			return -EINVAL;
		}

		switch (mode) {
		case LPAIF_I2SCTL_MODE_6CH:
		case LPAIF_I2SCTL_MODE_8CH:
			mode = LPAIF_I2SCTL_MODE_QUAD01;
			break;
		}
		break;
	case 6:
		if (mode < LPAIF_I2SCTL_MODE_6CH) {
			dev_err(dai->dev, "cannot configure 6 channels with mode %d\n",
				mode);
			return -EINVAL;
		}

		switch (mode) {
		case LPAIF_I2SCTL_MODE_8CH:
			mode = LPAIF_I2SCTL_MODE_6CH;
			break;
		}
		break;
	case 8:
		if (mode < LPAIF_I2SCTL_MODE_8CH) {
			dev_err(dai->dev, "cannot configure 8 channels with mode %d\n",
				mode);
			return -EINVAL;
		}
		break;
	default:
		dev_err(dai->dev, "invalid channels given: %u\n", channels);
		return -EINVAL;
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		regval |= LPAIF_I2SCTL_SPKMODE(mode);

		if (channels >= 2)
			regval |= LPAIF_I2SCTL_SPKMONO_STEREO;
		else
			regval |= LPAIF_I2SCTL_SPKMONO_MONO;
	} else {
		regval |= LPAIF_I2SCTL_MICMODE(mode);

		if (channels >= 2)
			regval |= LPAIF_I2SCTL_MICMONO_STEREO;
		else
			regval |= LPAIF_I2SCTL_MICMONO_MONO;
	}

	ret = regmap_write(drvdata->lpaif_map,
			   LPAIF_I2SCTL_REG(drvdata->variant, dai->driver->id),
			   regval);
	if (ret) {
		dev_err(dai->dev, "error writing to i2sctl reg: %d\n", ret);
		return ret;
	}

	ret = clk_set_rate(drvdata->mi2s_bit_clk[dai->driver->id],
			   rate * bitwidth * 2);
	if (ret) {
		dev_err(dai->dev, "error setting mi2s bitclk to %u: %d\n",
			rate * bitwidth * 2, ret);
		return ret;
	}

	return 0;
}

static int lpass_cpu_daiops_hw_free(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	struct lpass_data *drvdata = snd_soc_dai_get_drvdata(dai);
	int ret;

	ret = regmap_write(drvdata->lpaif_map,
			   LPAIF_I2SCTL_REG(drvdata->variant, dai->driver->id),
			   0);
	if (ret)
		dev_err(dai->dev, "error writing to i2sctl reg: %d\n", ret);

	return ret;
}

static int lpass_cpu_daiops_prepare(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	struct lpass_data *drvdata = snd_soc_dai_get_drvdata(dai);
	int ret;
	unsigned int val, mask;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		val = LPAIF_I2SCTL_SPKEN_ENABLE;
		mask = LPAIF_I2SCTL_SPKEN_MASK;
	} else  {
		val = LPAIF_I2SCTL_MICEN_ENABLE;
		mask = LPAIF_I2SCTL_MICEN_MASK;
	}

	ret = regmap_update_bits(drvdata->lpaif_map,
			LPAIF_I2SCTL_REG(drvdata->variant, dai->driver->id),
			mask, val);
	if (ret)
		dev_err(dai->dev, "error writing to i2sctl reg: %d\n", ret);

	return ret;
}

static int lpass_cpu_daiops_trigger(struct snd_pcm_substream *substream,
		int cmd, struct snd_soc_dai *dai)
{
	struct lpass_data *drvdata = snd_soc_dai_get_drvdata(dai);
	int ret = -EINVAL;
	unsigned int val, mask;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			val = LPAIF_I2SCTL_SPKEN_ENABLE;
			mask = LPAIF_I2SCTL_SPKEN_MASK;
		} else  {
			val = LPAIF_I2SCTL_MICEN_ENABLE;
			mask = LPAIF_I2SCTL_MICEN_MASK;
		}

		ret = regmap_update_bits(drvdata->lpaif_map,
				LPAIF_I2SCTL_REG(drvdata->variant,
						dai->driver->id),
				mask, val);
		if (ret)
			dev_err(dai->dev, "error writing to i2sctl reg: %d\n",
				ret);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			val = LPAIF_I2SCTL_SPKEN_DISABLE;
			mask = LPAIF_I2SCTL_SPKEN_MASK;
		} else  {
			val = LPAIF_I2SCTL_MICEN_DISABLE;
			mask = LPAIF_I2SCTL_MICEN_MASK;
		}

		ret = regmap_update_bits(drvdata->lpaif_map,
				LPAIF_I2SCTL_REG(drvdata->variant,
						dai->driver->id),
				mask, val);
		if (ret)
			dev_err(dai->dev, "error writing to i2sctl reg: %d\n",
				ret);
		break;
	}

	return ret;
}

const struct snd_soc_dai_ops asoc_qcom_lpass_cpu_dai_ops = {
	.set_sysclk	= lpass_cpu_daiops_set_sysclk,
	.startup	= lpass_cpu_daiops_startup,
	.shutdown	= lpass_cpu_daiops_shutdown,
	.hw_params	= lpass_cpu_daiops_hw_params,
	.hw_free	= lpass_cpu_daiops_hw_free,
	.prepare	= lpass_cpu_daiops_prepare,
	.trigger	= lpass_cpu_daiops_trigger,
};
EXPORT_SYMBOL_GPL(asoc_qcom_lpass_cpu_dai_ops);

int asoc_qcom_lpass_cpu_dai_probe(struct snd_soc_dai *dai)
{
	struct lpass_data *drvdata = snd_soc_dai_get_drvdata(dai);
	int ret;

	/* ensure audio hardware is disabled */
	ret = regmap_write(drvdata->lpaif_map,
			LPAIF_I2SCTL_REG(drvdata->variant, dai->driver->id), 0);
	if (ret)
		dev_err(dai->dev, "error writing to i2sctl reg: %d\n", ret);

	return ret;
}
EXPORT_SYMBOL_GPL(asoc_qcom_lpass_cpu_dai_probe);

static const struct snd_soc_component_driver lpass_cpu_comp_driver = {
	.name = "lpass-cpu",
};

static bool lpass_cpu_regmap_writeable(struct device *dev, unsigned int reg)
{
	struct lpass_data *drvdata = dev_get_drvdata(dev);
	struct lpass_variant *v = drvdata->variant;
	int i;

	for (i = 0; i < v->i2s_ports; ++i)
		if (reg == LPAIF_I2SCTL_REG(v, i))
			return true;

	for (i = 0; i < v->irq_ports; ++i) {
		if (reg == LPAIF_IRQEN_REG(v, i))
			return true;
		if (reg == LPAIF_IRQCLEAR_REG(v, i))
			return true;
	}

	for (i = 0; i < v->rdma_channels; ++i) {
		if (reg == LPAIF_RDMACTL_REG(v, i))
			return true;
		if (reg == LPAIF_RDMABASE_REG(v, i))
			return true;
		if (reg == LPAIF_RDMABUFF_REG(v, i))
			return true;
		if (reg == LPAIF_RDMAPER_REG(v, i))
			return true;
	}

	for (i = 0; i < v->wrdma_channels; ++i) {
		if (reg == LPAIF_WRDMACTL_REG(v, i + v->wrdma_channel_start))
			return true;
		if (reg == LPAIF_WRDMABASE_REG(v, i + v->wrdma_channel_start))
			return true;
		if (reg == LPAIF_WRDMABUFF_REG(v, i + v->wrdma_channel_start))
			return true;
		if (reg == LPAIF_WRDMAPER_REG(v, i + v->wrdma_channel_start))
			return true;
	}

	return false;
}

static bool lpass_cpu_regmap_readable(struct device *dev, unsigned int reg)
{
	struct lpass_data *drvdata = dev_get_drvdata(dev);
	struct lpass_variant *v = drvdata->variant;
	int i;

	for (i = 0; i < v->i2s_ports; ++i)
		if (reg == LPAIF_I2SCTL_REG(v, i))
			return true;

	for (i = 0; i < v->irq_ports; ++i) {
		if (reg == LPAIF_IRQEN_REG(v, i))
			return true;
		if (reg == LPAIF_IRQSTAT_REG(v, i))
			return true;
	}

	for (i = 0; i < v->rdma_channels; ++i) {
		if (reg == LPAIF_RDMACTL_REG(v, i))
			return true;
		if (reg == LPAIF_RDMABASE_REG(v, i))
			return true;
		if (reg == LPAIF_RDMABUFF_REG(v, i))
			return true;
		if (reg == LPAIF_RDMACURR_REG(v, i))
			return true;
		if (reg == LPAIF_RDMAPER_REG(v, i))
			return true;
	}

	for (i = 0; i < v->wrdma_channels; ++i) {
		if (reg == LPAIF_WRDMACTL_REG(v, i + v->wrdma_channel_start))
			return true;
		if (reg == LPAIF_WRDMABASE_REG(v, i + v->wrdma_channel_start))
			return true;
		if (reg == LPAIF_WRDMABUFF_REG(v, i + v->wrdma_channel_start))
			return true;
		if (reg == LPAIF_WRDMACURR_REG(v, i + v->wrdma_channel_start))
			return true;
		if (reg == LPAIF_WRDMAPER_REG(v, i + v->wrdma_channel_start))
			return true;
	}

	return false;
}

static bool lpass_cpu_regmap_volatile(struct device *dev, unsigned int reg)
{
	struct lpass_data *drvdata = dev_get_drvdata(dev);
	struct lpass_variant *v = drvdata->variant;
	int i;

	for (i = 0; i < v->irq_ports; ++i)
		if (reg == LPAIF_IRQSTAT_REG(v, i))
			return true;

	for (i = 0; i < v->rdma_channels; ++i)
		if (reg == LPAIF_RDMACURR_REG(v, i))
			return true;

	for (i = 0; i < v->wrdma_channels; ++i)
		if (reg == LPAIF_WRDMACURR_REG(v, i + v->wrdma_channel_start))
			return true;

	return false;
}

static struct regmap_config lpass_cpu_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.writeable_reg = lpass_cpu_regmap_writeable,
	.readable_reg = lpass_cpu_regmap_readable,
	.volatile_reg = lpass_cpu_regmap_volatile,
	.cache_type = REGCACHE_FLAT,
};

static unsigned int of_lpass_cpu_parse_sd_lines(struct device *dev,
						struct device_node *node,
						const char *name)
{
	unsigned int lines[LPASS_CPU_MAX_MI2S_LINES];
	unsigned int sd_line_mask = 0;
	int num_lines, i;

	num_lines = of_property_read_variable_u32_array(node, name, lines, 0,
							LPASS_CPU_MAX_MI2S_LINES);
	if (num_lines < 0)
		return LPAIF_I2SCTL_MODE_NONE;

	for (i = 0; i < num_lines; i++)
		sd_line_mask |= BIT(lines[i]);

	switch (sd_line_mask) {
	case LPASS_CPU_I2S_SD0_MASK:
		return LPAIF_I2SCTL_MODE_SD0;
	case LPASS_CPU_I2S_SD1_MASK:
		return LPAIF_I2SCTL_MODE_SD1;
	case LPASS_CPU_I2S_SD2_MASK:
		return LPAIF_I2SCTL_MODE_SD2;
	case LPASS_CPU_I2S_SD3_MASK:
		return LPAIF_I2SCTL_MODE_SD3;
	case LPASS_CPU_I2S_SD0_1_MASK:
		return LPAIF_I2SCTL_MODE_QUAD01;
	case LPASS_CPU_I2S_SD2_3_MASK:
		return LPAIF_I2SCTL_MODE_QUAD23;
	case LPASS_CPU_I2S_SD0_1_2_MASK:
		return LPAIF_I2SCTL_MODE_6CH;
	case LPASS_CPU_I2S_SD0_1_2_3_MASK:
		return LPAIF_I2SCTL_MODE_8CH;
	default:
		dev_err(dev, "Unsupported SD line mask: %#x\n", sd_line_mask);
		return LPAIF_I2SCTL_MODE_NONE;
	}
}

static void of_lpass_cpu_parse_dai_data(struct device *dev,
					struct lpass_data *data)
{
	struct device_node *node;
	int ret, id;

	/* Allow all channels by default for backwards compatibility */
	for (id = 0; id < data->variant->num_dai; id++) {
		data->mi2s_playback_sd_mode[id] = LPAIF_I2SCTL_MODE_8CH;
		data->mi2s_capture_sd_mode[id] = LPAIF_I2SCTL_MODE_8CH;
	}

	for_each_child_of_node(dev->of_node, node) {
		ret = of_property_read_u32(node, "reg", &id);
		if (ret || id < 0 || id >= data->variant->num_dai) {
			dev_err(dev, "valid dai id not found: %d\n", ret);
			continue;
		}

		data->mi2s_playback_sd_mode[id] =
			of_lpass_cpu_parse_sd_lines(dev, node,
						    "qcom,playback-sd-lines");
		data->mi2s_capture_sd_mode[id] =
			of_lpass_cpu_parse_sd_lines(dev, node,
						    "qcom,capture-sd-lines");
	}
}

int asoc_qcom_lpass_cpu_platform_probe(struct platform_device *pdev)
{
	struct lpass_data *drvdata;
	struct device_node *dsp_of_node;
	struct resource *res;
	struct lpass_variant *variant;
	struct device *dev = &pdev->dev;
	const struct of_device_id *match;
	int ret, i, dai_id;

	dsp_of_node = of_parse_phandle(pdev->dev.of_node, "qcom,adsp", 0);
	if (dsp_of_node) {
		dev_err(dev, "DSP exists and holds audio resources\n");
		return -EBUSY;
	}

	drvdata = devm_kzalloc(dev, sizeof(struct lpass_data), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;
	platform_set_drvdata(pdev, drvdata);

	match = of_match_device(dev->driver->of_match_table, dev);
	if (!match || !match->data)
		return -EINVAL;

	drvdata->variant = (struct lpass_variant *)match->data;
	variant = drvdata->variant;

	of_lpass_cpu_parse_dai_data(dev, drvdata);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "lpass-lpaif");

	drvdata->lpaif = devm_ioremap_resource(dev, res);
	if (IS_ERR((void const __force *)drvdata->lpaif)) {
		dev_err(dev, "error mapping reg resource: %ld\n",
				PTR_ERR((void const __force *)drvdata->lpaif));
		return PTR_ERR((void const __force *)drvdata->lpaif);
	}

	lpass_cpu_regmap_config.max_register = LPAIF_WRDMAPER_REG(variant,
						variant->wrdma_channels +
						variant->wrdma_channel_start);

	drvdata->lpaif_map = devm_regmap_init_mmio(dev, drvdata->lpaif,
			&lpass_cpu_regmap_config);
	if (IS_ERR(drvdata->lpaif_map)) {
		dev_err(dev, "error initializing regmap: %ld\n",
			PTR_ERR(drvdata->lpaif_map));
		return PTR_ERR(drvdata->lpaif_map);
	}

	if (variant->init) {
		ret = variant->init(pdev);
		if (ret) {
			dev_err(dev, "error initializing variant: %d\n", ret);
			return ret;
		}
	}

	for (i = 0; i < variant->num_dai; i++) {
		dai_id = variant->dai_driver[i].id;
		drvdata->mi2s_osr_clk[dai_id] = devm_clk_get(dev,
					     variant->dai_osr_clk_names[i]);
		if (IS_ERR(drvdata->mi2s_osr_clk[dai_id])) {
			dev_warn(dev,
				"%s() error getting optional %s: %ld\n",
				__func__,
				variant->dai_osr_clk_names[i],
				PTR_ERR(drvdata->mi2s_osr_clk[dai_id]));

			drvdata->mi2s_osr_clk[dai_id] = NULL;
		}

		drvdata->mi2s_bit_clk[dai_id] = devm_clk_get(dev,
						variant->dai_bit_clk_names[i]);
		if (IS_ERR(drvdata->mi2s_bit_clk[dai_id])) {
			dev_err(dev,
				"error getting %s: %ld\n",
				variant->dai_bit_clk_names[i],
				PTR_ERR(drvdata->mi2s_bit_clk[dai_id]));
			return PTR_ERR(drvdata->mi2s_bit_clk[dai_id]);
		}
	}

	ret = devm_snd_soc_register_component(dev,
					      &lpass_cpu_comp_driver,
					      variant->dai_driver,
					      variant->num_dai);
	if (ret) {
		dev_err(dev, "error registering cpu driver: %d\n", ret);
		goto err;
	}

	ret = asoc_qcom_lpass_platform_register(pdev);
	if (ret) {
		dev_err(dev, "error registering platform driver: %d\n", ret);
		goto err;
	}

err:
	return ret;
}
EXPORT_SYMBOL_GPL(asoc_qcom_lpass_cpu_platform_probe);

int asoc_qcom_lpass_cpu_platform_remove(struct platform_device *pdev)
{
	struct lpass_data *drvdata = platform_get_drvdata(pdev);

	if (drvdata->variant->exit)
		drvdata->variant->exit(pdev);


	return 0;
}
EXPORT_SYMBOL_GPL(asoc_qcom_lpass_cpu_platform_remove);

MODULE_DESCRIPTION("QTi LPASS CPU Driver");
MODULE_LICENSE("GPL v2");
