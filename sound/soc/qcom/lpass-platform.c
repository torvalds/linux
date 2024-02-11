// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2010-2011,2013-2015 The Linux Foundation. All rights reserved.
 *
 * lpass-platform.c -- ALSA SoC platform driver for QTi LPASS
 */

#include <dt-bindings/sound/qcom,lpass.h>
#include <linux/dma-mapping.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <sound/pcm_params.h>
#include <linux/regmap.h>
#include <sound/soc.h>
#include "lpass-lpaif-reg.h"
#include "lpass.h"

#define DRV_NAME "lpass-platform"

#define LPASS_PLATFORM_BUFFER_SIZE	(24 *  2 * 1024)
#define LPASS_PLATFORM_PERIODS		2
#define LPASS_RXTX_CDC_DMA_LPM_BUFF_SIZE (8 * 1024)
#define LPASS_VA_CDC_DMA_LPM_BUFF_SIZE (12 * 1024)
#define LPASS_CDC_DMA_REGISTER_FIELDS_MAX 15

static const struct snd_pcm_hardware lpass_platform_pcm_hardware = {
	.info			=	SNDRV_PCM_INFO_MMAP |
					SNDRV_PCM_INFO_MMAP_VALID |
					SNDRV_PCM_INFO_INTERLEAVED |
					SNDRV_PCM_INFO_PAUSE |
					SNDRV_PCM_INFO_RESUME,
	.formats		=	SNDRV_PCM_FMTBIT_S16 |
					SNDRV_PCM_FMTBIT_S24 |
					SNDRV_PCM_FMTBIT_S32,
	.rates			=	SNDRV_PCM_RATE_8000_192000,
	.rate_min		=	8000,
	.rate_max		=	192000,
	.channels_min		=	1,
	.channels_max		=	8,
	.buffer_bytes_max	=	LPASS_PLATFORM_BUFFER_SIZE,
	.period_bytes_max	=	LPASS_PLATFORM_BUFFER_SIZE /
						LPASS_PLATFORM_PERIODS,
	.period_bytes_min	=	LPASS_PLATFORM_BUFFER_SIZE /
						LPASS_PLATFORM_PERIODS,
	.periods_min		=	LPASS_PLATFORM_PERIODS,
	.periods_max		=	LPASS_PLATFORM_PERIODS,
	.fifo_size		=	0,
};

static const struct snd_pcm_hardware lpass_platform_rxtx_hardware = {
	.info			=	SNDRV_PCM_INFO_MMAP |
					SNDRV_PCM_INFO_MMAP_VALID |
					SNDRV_PCM_INFO_INTERLEAVED |
					SNDRV_PCM_INFO_PAUSE |
					SNDRV_PCM_INFO_RESUME,
	.formats		=	SNDRV_PCM_FMTBIT_S16 |
					SNDRV_PCM_FMTBIT_S24 |
					SNDRV_PCM_FMTBIT_S32,
	.rates			=	SNDRV_PCM_RATE_8000_192000,
	.rate_min		=	8000,
	.rate_max		=	192000,
	.channels_min		=	1,
	.channels_max		=	8,
	.buffer_bytes_max	=	LPASS_RXTX_CDC_DMA_LPM_BUFF_SIZE,
	.period_bytes_max	=	LPASS_RXTX_CDC_DMA_LPM_BUFF_SIZE /
						LPASS_PLATFORM_PERIODS,
	.period_bytes_min	=	LPASS_RXTX_CDC_DMA_LPM_BUFF_SIZE /
						LPASS_PLATFORM_PERIODS,
	.periods_min		=	LPASS_PLATFORM_PERIODS,
	.periods_max		=	LPASS_PLATFORM_PERIODS,
	.fifo_size		=	0,
};

static const struct snd_pcm_hardware lpass_platform_va_hardware = {
	.info			=	SNDRV_PCM_INFO_MMAP |
					SNDRV_PCM_INFO_MMAP_VALID |
					SNDRV_PCM_INFO_INTERLEAVED |
					SNDRV_PCM_INFO_PAUSE |
					SNDRV_PCM_INFO_RESUME,
	.formats		=	SNDRV_PCM_FMTBIT_S16 |
					SNDRV_PCM_FMTBIT_S24 |
					SNDRV_PCM_FMTBIT_S32,
	.rates			=	SNDRV_PCM_RATE_8000_192000,
	.rate_min		=	8000,
	.rate_max		=	192000,
	.channels_min		=	1,
	.channels_max		=	8,
	.buffer_bytes_max	=	LPASS_VA_CDC_DMA_LPM_BUFF_SIZE,
	.period_bytes_max	=	LPASS_VA_CDC_DMA_LPM_BUFF_SIZE /
						LPASS_PLATFORM_PERIODS,
	.period_bytes_min	=	LPASS_VA_CDC_DMA_LPM_BUFF_SIZE /
						LPASS_PLATFORM_PERIODS,
	.periods_min		=	LPASS_PLATFORM_PERIODS,
	.periods_max		=	LPASS_PLATFORM_PERIODS,
	.fifo_size		=	0,
};

static int lpass_platform_alloc_rxtx_dmactl_fields(struct device *dev,
					 struct regmap *map)
{
	struct lpass_data *drvdata = dev_get_drvdata(dev);
	const struct lpass_variant *v = drvdata->variant;
	struct lpaif_dmactl *rd_dmactl, *wr_dmactl;
	int rval;

	rd_dmactl = devm_kzalloc(dev, sizeof(*rd_dmactl), GFP_KERNEL);
	if (!rd_dmactl)
		return -ENOMEM;

	wr_dmactl = devm_kzalloc(dev, sizeof(*wr_dmactl), GFP_KERNEL);
	if (!wr_dmactl)
		return -ENOMEM;

	drvdata->rxtx_rd_dmactl = rd_dmactl;
	drvdata->rxtx_wr_dmactl = wr_dmactl;

	rval = devm_regmap_field_bulk_alloc(dev, map, &rd_dmactl->intf,
					    &v->rxtx_rdma_intf, LPASS_CDC_DMA_REGISTER_FIELDS_MAX);
	if (rval)
		return rval;

	return devm_regmap_field_bulk_alloc(dev, map, &wr_dmactl->intf,
					    &v->rxtx_wrdma_intf, LPASS_CDC_DMA_REGISTER_FIELDS_MAX);
}

static int lpass_platform_alloc_va_dmactl_fields(struct device *dev,
					 struct regmap *map)
{
	struct lpass_data *drvdata = dev_get_drvdata(dev);
	const struct lpass_variant *v = drvdata->variant;
	struct lpaif_dmactl *wr_dmactl;

	wr_dmactl = devm_kzalloc(dev, sizeof(*wr_dmactl), GFP_KERNEL);
	if (!wr_dmactl)
		return -ENOMEM;

	drvdata->va_wr_dmactl = wr_dmactl;
	return devm_regmap_field_bulk_alloc(dev, map, &wr_dmactl->intf,
					    &v->va_wrdma_intf, LPASS_CDC_DMA_REGISTER_FIELDS_MAX);
}


static int lpass_platform_alloc_dmactl_fields(struct device *dev,
					 struct regmap *map)
{
	struct lpass_data *drvdata = dev_get_drvdata(dev);
	const struct lpass_variant *v = drvdata->variant;
	struct lpaif_dmactl *rd_dmactl, *wr_dmactl;
	int rval;

	drvdata->rd_dmactl = devm_kzalloc(dev, sizeof(struct lpaif_dmactl),
					  GFP_KERNEL);
	if (drvdata->rd_dmactl == NULL)
		return -ENOMEM;

	drvdata->wr_dmactl = devm_kzalloc(dev, sizeof(struct lpaif_dmactl),
					  GFP_KERNEL);
	if (drvdata->wr_dmactl == NULL)
		return -ENOMEM;

	rd_dmactl = drvdata->rd_dmactl;
	wr_dmactl = drvdata->wr_dmactl;

	rval = devm_regmap_field_bulk_alloc(dev, map, &rd_dmactl->intf,
					    &v->rdma_intf, 6);
	if (rval)
		return rval;

	return devm_regmap_field_bulk_alloc(dev, map, &wr_dmactl->intf,
					    &v->wrdma_intf, 6);
}

static int lpass_platform_alloc_hdmidmactl_fields(struct device *dev,
					 struct regmap *map)
{
	struct lpass_data *drvdata = dev_get_drvdata(dev);
	const struct lpass_variant *v = drvdata->variant;
	struct lpaif_dmactl *rd_dmactl;

	rd_dmactl = devm_kzalloc(dev, sizeof(struct lpaif_dmactl), GFP_KERNEL);
	if (rd_dmactl == NULL)
		return -ENOMEM;

	drvdata->hdmi_rd_dmactl = rd_dmactl;

	return devm_regmap_field_bulk_alloc(dev, map, &rd_dmactl->bursten,
					    &v->hdmi_rdma_bursten, 8);
}

static int lpass_platform_pcmops_open(struct snd_soc_component *component,
				      struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *soc_runtime = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(soc_runtime, 0);
	struct lpass_data *drvdata = snd_soc_component_get_drvdata(component);
	const struct lpass_variant *v = drvdata->variant;
	int ret, dma_ch, dir = substream->stream;
	struct lpass_pcm_data *data;
	struct regmap *map;
	unsigned int dai_id = cpu_dai->driver->id;

	component->id = dai_id;
	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->i2s_port = cpu_dai->driver->id;
	runtime->private_data = data;

	if (v->alloc_dma_channel)
		dma_ch = v->alloc_dma_channel(drvdata, dir, dai_id);
	else
		dma_ch = 0;

	if (dma_ch < 0) {
		kfree(data);
		return dma_ch;
	}

	switch (dai_id) {
	case MI2S_PRIMARY ... MI2S_QUINARY:
		map = drvdata->lpaif_map;
		drvdata->substream[dma_ch] = substream;
		break;
	case LPASS_DP_RX:
		map = drvdata->hdmiif_map;
		drvdata->hdmi_substream[dma_ch] = substream;
		break;
	case LPASS_CDC_DMA_RX0 ... LPASS_CDC_DMA_RX9:
	case LPASS_CDC_DMA_TX0 ... LPASS_CDC_DMA_TX8:
		map = drvdata->rxtx_lpaif_map;
		drvdata->rxtx_substream[dma_ch] = substream;
		break;
	case LPASS_CDC_DMA_VA_TX0 ... LPASS_CDC_DMA_VA_TX8:
		map = drvdata->va_lpaif_map;
		drvdata->va_substream[dma_ch] = substream;
		break;
	default:
		break;
	}

	data->dma_ch = dma_ch;
	switch (dai_id) {
	case MI2S_PRIMARY ... MI2S_QUINARY:
	case LPASS_DP_RX:
		ret = regmap_write(map, LPAIF_DMACTL_REG(v, dma_ch, dir, data->i2s_port), 0);
		if (ret) {
			kfree(data);
			dev_err(soc_runtime->dev, "error writing to rdmactl reg: %d\n", ret);
			return ret;
		}
		snd_soc_set_runtime_hwparams(substream, &lpass_platform_pcm_hardware);
		runtime->dma_bytes = lpass_platform_pcm_hardware.buffer_bytes_max;
		break;
	case LPASS_CDC_DMA_RX0 ... LPASS_CDC_DMA_RX9:
	case LPASS_CDC_DMA_TX0 ... LPASS_CDC_DMA_TX8:
		snd_soc_set_runtime_hwparams(substream, &lpass_platform_rxtx_hardware);
		runtime->dma_bytes = lpass_platform_rxtx_hardware.buffer_bytes_max;
		snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);
		break;
	case LPASS_CDC_DMA_VA_TX0 ... LPASS_CDC_DMA_VA_TX8:
		snd_soc_set_runtime_hwparams(substream, &lpass_platform_va_hardware);
		runtime->dma_bytes = lpass_platform_va_hardware.buffer_bytes_max;
		snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);
		break;
	default:
		break;
	}
	ret = snd_pcm_hw_constraint_integer(runtime,
			SNDRV_PCM_HW_PARAM_PERIODS);
	if (ret < 0) {
		kfree(data);
		dev_err(soc_runtime->dev, "setting constraints failed: %d\n",
			ret);
		return -EINVAL;
	}

	return 0;
}

static int lpass_platform_pcmops_close(struct snd_soc_component *component,
				       struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *soc_runtime = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(soc_runtime, 0);
	struct lpass_data *drvdata = snd_soc_component_get_drvdata(component);
	const struct lpass_variant *v = drvdata->variant;
	struct lpass_pcm_data *data;
	unsigned int dai_id = cpu_dai->driver->id;

	data = runtime->private_data;

	switch (dai_id) {
	case MI2S_PRIMARY ... MI2S_QUINARY:
		drvdata->substream[data->dma_ch] = NULL;
		break;
	case LPASS_DP_RX:
		drvdata->hdmi_substream[data->dma_ch] = NULL;
		break;
	case LPASS_CDC_DMA_RX0 ... LPASS_CDC_DMA_RX9:
	case LPASS_CDC_DMA_TX0 ... LPASS_CDC_DMA_TX8:
		drvdata->rxtx_substream[data->dma_ch] = NULL;
		break;
	case LPASS_CDC_DMA_VA_TX0 ... LPASS_CDC_DMA_VA_TX8:
		drvdata->va_substream[data->dma_ch] = NULL;
		break;
	default:
		break;
	}

	if (v->free_dma_channel)
		v->free_dma_channel(drvdata, data->dma_ch, dai_id);

	kfree(data);
	return 0;
}

static struct lpaif_dmactl *__lpass_get_dmactl_handle(const struct snd_pcm_substream *substream,
				     struct snd_soc_component *component)
{
	struct snd_soc_pcm_runtime *soc_runtime = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(soc_runtime, 0);
	struct lpass_data *drvdata = snd_soc_component_get_drvdata(component);
	struct lpaif_dmactl *dmactl = NULL;

	switch (cpu_dai->driver->id) {
	case MI2S_PRIMARY ... MI2S_QUINARY:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			dmactl = drvdata->rd_dmactl;
		else
			dmactl = drvdata->wr_dmactl;
		break;
	case LPASS_DP_RX:
		dmactl = drvdata->hdmi_rd_dmactl;
		break;
	case LPASS_CDC_DMA_RX0 ... LPASS_CDC_DMA_RX9:
		dmactl = drvdata->rxtx_rd_dmactl;
		break;
	case LPASS_CDC_DMA_TX0 ... LPASS_CDC_DMA_TX8:
		dmactl = drvdata->rxtx_wr_dmactl;
		break;
	case LPASS_CDC_DMA_VA_TX0 ... LPASS_CDC_DMA_VA_TX8:
		dmactl = drvdata->va_wr_dmactl;
		break;
	}

	return dmactl;
}

static int __lpass_get_id(const struct snd_pcm_substream *substream,
				     struct snd_soc_component *component)
{
	struct snd_soc_pcm_runtime *soc_runtime = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(soc_runtime, 0);
	struct lpass_data *drvdata = snd_soc_component_get_drvdata(component);
	struct snd_pcm_runtime *rt = substream->runtime;
	struct lpass_pcm_data *pcm_data = rt->private_data;
	const struct lpass_variant *v = drvdata->variant;
	int id;

	switch (cpu_dai->driver->id) {
	case MI2S_PRIMARY ... MI2S_QUINARY:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			id = pcm_data->dma_ch;
		else
			id = pcm_data->dma_ch - v->wrdma_channel_start;
		break;
	case LPASS_DP_RX:
		id = pcm_data->dma_ch;
		break;
	case LPASS_CDC_DMA_RX0 ... LPASS_CDC_DMA_RX9:
		id = pcm_data->dma_ch;
		break;
	case LPASS_CDC_DMA_TX0 ... LPASS_CDC_DMA_TX8:
		id = pcm_data->dma_ch - v->rxtx_wrdma_channel_start;
		break;
	case LPASS_CDC_DMA_VA_TX0 ... LPASS_CDC_DMA_VA_TX8:
		id = pcm_data->dma_ch - v->va_wrdma_channel_start;
		break;
	}

	return id;
}

static struct regmap *__lpass_get_regmap_handle(const struct snd_pcm_substream *substream,
				     struct snd_soc_component *component)
{
	struct snd_soc_pcm_runtime *soc_runtime = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(soc_runtime, 0);
	struct lpass_data *drvdata = snd_soc_component_get_drvdata(component);
	struct regmap *map = NULL;

	switch (cpu_dai->driver->id) {
	case MI2S_PRIMARY ... MI2S_QUINARY:
		map = drvdata->lpaif_map;
		break;
	case LPASS_DP_RX:
		map = drvdata->hdmiif_map;
		break;
	case LPASS_CDC_DMA_RX0 ... LPASS_CDC_DMA_RX9:
	case LPASS_CDC_DMA_TX0 ... LPASS_CDC_DMA_TX8:
		map = drvdata->rxtx_lpaif_map;
		break;
	case LPASS_CDC_DMA_VA_TX0 ... LPASS_CDC_DMA_VA_TX8:
		map = drvdata->va_lpaif_map;
		break;
	}

	return map;
}

static int lpass_platform_pcmops_hw_params(struct snd_soc_component *component,
					   struct snd_pcm_substream *substream,
					   struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *soc_runtime = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(soc_runtime, 0);
	struct lpass_data *drvdata = snd_soc_component_get_drvdata(component);
	struct snd_pcm_runtime *rt = substream->runtime;
	struct lpass_pcm_data *pcm_data = rt->private_data;
	const struct lpass_variant *v = drvdata->variant;
	snd_pcm_format_t format = params_format(params);
	unsigned int channels = params_channels(params);
	unsigned int regval;
	struct lpaif_dmactl *dmactl;
	int id;
	int bitwidth;
	int ret, dma_port = pcm_data->i2s_port + v->dmactl_audif_start;
	unsigned int dai_id = cpu_dai->driver->id;

	dmactl = __lpass_get_dmactl_handle(substream, component);
	id = __lpass_get_id(substream, component);

	bitwidth = snd_pcm_format_width(format);
	if (bitwidth < 0) {
		dev_err(soc_runtime->dev, "invalid bit width given: %d\n",
				bitwidth);
		return bitwidth;
	}

	ret = regmap_fields_write(dmactl->bursten, id, LPAIF_DMACTL_BURSTEN_INCR4);
	if (ret) {
		dev_err(soc_runtime->dev, "error updating bursten field: %d\n", ret);
		return ret;
	}

	ret = regmap_fields_write(dmactl->fifowm, id, LPAIF_DMACTL_FIFOWM_8);
	if (ret) {
		dev_err(soc_runtime->dev, "error updating fifowm field: %d\n", ret);
		return ret;
	}

	switch (dai_id) {
	case LPASS_DP_RX:
		ret = regmap_fields_write(dmactl->burst8, id,
							LPAIF_DMACTL_BURSTEN_INCR4);
		if (ret) {
			dev_err(soc_runtime->dev, "error updating burst8en field: %d\n", ret);
			return ret;
		}
		ret = regmap_fields_write(dmactl->burst16, id,
							LPAIF_DMACTL_BURSTEN_INCR4);
		if (ret) {
			dev_err(soc_runtime->dev, "error updating burst16en field: %d\n", ret);
			return ret;
		}
		ret = regmap_fields_write(dmactl->dynburst, id,
							LPAIF_DMACTL_BURSTEN_INCR4);
		if (ret) {
			dev_err(soc_runtime->dev, "error updating dynbursten field: %d\n", ret);
			return ret;
		}
		break;
	case MI2S_PRIMARY:
	case MI2S_SECONDARY:
	case MI2S_TERTIARY:
	case MI2S_QUATERNARY:
	case MI2S_QUINARY:
		ret = regmap_fields_write(dmactl->intf, id,
						LPAIF_DMACTL_AUDINTF(dma_port));
		if (ret) {
			dev_err(soc_runtime->dev, "error updating audio interface field: %d\n",
					ret);
			return ret;
		}

		break;
	case LPASS_CDC_DMA_RX0 ... LPASS_CDC_DMA_RX9:
	case LPASS_CDC_DMA_TX0 ... LPASS_CDC_DMA_TX8:
	case LPASS_CDC_DMA_VA_TX0 ... LPASS_CDC_DMA_VA_TX0:
		break;
	default:
		dev_err(soc_runtime->dev, "%s: invalid  interface: %d\n", __func__, dai_id);
		break;
	}
	switch (bitwidth) {
	case 16:
		switch (channels) {
		case 1:
		case 2:
			regval = LPAIF_DMACTL_WPSCNT_ONE;
			break;
		case 4:
			regval = LPAIF_DMACTL_WPSCNT_TWO;
			break;
		case 6:
			regval = LPAIF_DMACTL_WPSCNT_THREE;
			break;
		case 8:
			regval = LPAIF_DMACTL_WPSCNT_FOUR;
			break;
		default:
			dev_err(soc_runtime->dev, "invalid PCM config given: bw=%d, ch=%u\n",
				bitwidth, channels);
			return -EINVAL;
		}
		break;
	case 24:
	case 32:
		switch (channels) {
		case 1:
			regval = LPAIF_DMACTL_WPSCNT_ONE;
			break;
		case 2:
			regval = (dai_id == LPASS_DP_RX ?
			LPAIF_DMACTL_WPSCNT_ONE :
			LPAIF_DMACTL_WPSCNT_TWO);
			break;
		case 4:
			regval = (dai_id == LPASS_DP_RX ?
			LPAIF_DMACTL_WPSCNT_TWO :
			LPAIF_DMACTL_WPSCNT_FOUR);
			break;
		case 6:
			regval = (dai_id == LPASS_DP_RX ?
			LPAIF_DMACTL_WPSCNT_THREE :
			LPAIF_DMACTL_WPSCNT_SIX);
			break;
		case 8:
			regval = (dai_id == LPASS_DP_RX ?
			LPAIF_DMACTL_WPSCNT_FOUR :
			LPAIF_DMACTL_WPSCNT_EIGHT);
			break;
		default:
			dev_err(soc_runtime->dev, "invalid PCM config given: bw=%d, ch=%u\n",
				bitwidth, channels);
			return -EINVAL;
		}
		break;
	default:
		dev_err(soc_runtime->dev, "invalid PCM config given: bw=%d, ch=%u\n",
			bitwidth, channels);
		return -EINVAL;
	}

	ret = regmap_fields_write(dmactl->wpscnt, id, regval);
	if (ret) {
		dev_err(soc_runtime->dev, "error writing to dmactl reg: %d\n",
			ret);
		return ret;
	}

	return 0;
}

static int lpass_platform_pcmops_hw_free(struct snd_soc_component *component,
					 struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *soc_runtime = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(soc_runtime, 0);
	struct lpass_data *drvdata = snd_soc_component_get_drvdata(component);
	struct snd_pcm_runtime *rt = substream->runtime;
	struct lpass_pcm_data *pcm_data = rt->private_data;
	const struct lpass_variant *v = drvdata->variant;
	unsigned int reg;
	int ret;
	struct regmap *map;
	unsigned int dai_id = cpu_dai->driver->id;

	if (is_cdc_dma_port(dai_id))
		return 0;
	map = __lpass_get_regmap_handle(substream, component);

	reg = LPAIF_DMACTL_REG(v, pcm_data->dma_ch, substream->stream, dai_id);
	ret = regmap_write(map, reg, 0);
	if (ret)
		dev_err(soc_runtime->dev, "error writing to rdmactl reg: %d\n",
			ret);

	return ret;
}

static int lpass_platform_pcmops_prepare(struct snd_soc_component *component,
					 struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *soc_runtime = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(soc_runtime, 0);
	struct lpass_data *drvdata = snd_soc_component_get_drvdata(component);
	struct snd_pcm_runtime *rt = substream->runtime;
	struct lpass_pcm_data *pcm_data = rt->private_data;
	const struct lpass_variant *v = drvdata->variant;
	struct lpaif_dmactl *dmactl;
	struct regmap *map;
	int ret, id, ch, dir = substream->stream;
	unsigned int dai_id = cpu_dai->driver->id;

	ch = pcm_data->dma_ch;

	dmactl = __lpass_get_dmactl_handle(substream, component);
	id = __lpass_get_id(substream, component);
	map = __lpass_get_regmap_handle(substream, component);

	ret = regmap_write(map, LPAIF_DMABASE_REG(v, ch, dir, dai_id),
				runtime->dma_addr);
	if (ret) {
		dev_err(soc_runtime->dev, "error writing to rdmabase reg: %d\n",
			ret);
		return ret;
	}

	ret = regmap_write(map, LPAIF_DMABUFF_REG(v, ch, dir, dai_id),
			(snd_pcm_lib_buffer_bytes(substream) >> 2) - 1);
	if (ret) {
		dev_err(soc_runtime->dev, "error writing to rdmabuff reg: %d\n",
			ret);
		return ret;
	}

	ret = regmap_write(map, LPAIF_DMAPER_REG(v, ch, dir, dai_id),
			(snd_pcm_lib_period_bytes(substream) >> 2) - 1);
	if (ret) {
		dev_err(soc_runtime->dev, "error writing to rdmaper reg: %d\n",
			ret);
		return ret;
	}

	if (is_cdc_dma_port(dai_id)) {
		ret = regmap_fields_write(dmactl->fifowm, id, LPAIF_DMACTL_FIFOWM_8);
		if (ret) {
			dev_err(soc_runtime->dev, "error writing fifowm field to dmactl reg: %d, id: %d\n",
				ret, id);
			return ret;
		}
	}
	ret = regmap_fields_write(dmactl->enable, id, LPAIF_DMACTL_ENABLE_ON);
	if (ret) {
		dev_err(soc_runtime->dev, "error writing to rdmactl reg: %d\n",
			ret);
		return ret;
	}

	return 0;
}

static int lpass_platform_pcmops_trigger(struct snd_soc_component *component,
					 struct snd_pcm_substream *substream,
					 int cmd)
{
	struct snd_soc_pcm_runtime *soc_runtime = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(soc_runtime, 0);
	struct lpass_data *drvdata = snd_soc_component_get_drvdata(component);
	struct snd_pcm_runtime *rt = substream->runtime;
	struct lpass_pcm_data *pcm_data = rt->private_data;
	const struct lpass_variant *v = drvdata->variant;
	struct lpaif_dmactl *dmactl;
	struct regmap *map;
	int ret, ch, id;
	unsigned int reg_irqclr = 0, val_irqclr = 0;
	unsigned int  reg_irqen = 0, val_irqen = 0, val_mask = 0;
	unsigned int dai_id = cpu_dai->driver->id;

	ch = pcm_data->dma_ch;
	dmactl = __lpass_get_dmactl_handle(substream, component);
	id = __lpass_get_id(substream, component);
	map = __lpass_get_regmap_handle(substream, component);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		ret = regmap_fields_write(dmactl->enable, id,
						 LPAIF_DMACTL_ENABLE_ON);
		if (ret) {
			dev_err(soc_runtime->dev,
				"error writing to rdmactl reg: %d\n", ret);
			return ret;
		}
		switch (dai_id) {
		case LPASS_DP_RX:
			ret = regmap_fields_write(dmactl->dyncclk, id,
					 LPAIF_DMACTL_DYNCLK_ON);
			if (ret) {
				dev_err(soc_runtime->dev,
					"error writing to rdmactl reg: %d\n", ret);
				return ret;
			}
			reg_irqclr = LPASS_HDMITX_APP_IRQCLEAR_REG(v);
			val_irqclr = (LPAIF_IRQ_ALL(ch) |
					LPAIF_IRQ_HDMI_REQ_ON_PRELOAD(ch) |
					LPAIF_IRQ_HDMI_METADONE |
					LPAIF_IRQ_HDMI_SDEEP_AUD_DIS(ch));

			reg_irqen = LPASS_HDMITX_APP_IRQEN_REG(v);
			val_mask = (LPAIF_IRQ_ALL(ch) |
					LPAIF_IRQ_HDMI_REQ_ON_PRELOAD(ch) |
					LPAIF_IRQ_HDMI_METADONE |
					LPAIF_IRQ_HDMI_SDEEP_AUD_DIS(ch));
			val_irqen = (LPAIF_IRQ_ALL(ch) |
					LPAIF_IRQ_HDMI_REQ_ON_PRELOAD(ch) |
					LPAIF_IRQ_HDMI_METADONE |
					LPAIF_IRQ_HDMI_SDEEP_AUD_DIS(ch));
			break;
		case MI2S_PRIMARY:
		case MI2S_SECONDARY:
		case MI2S_TERTIARY:
		case MI2S_QUATERNARY:
		case MI2S_QUINARY:
			reg_irqclr = LPAIF_IRQCLEAR_REG(v, LPAIF_IRQ_PORT_HOST);
			val_irqclr = LPAIF_IRQ_ALL(ch);


			reg_irqen = LPAIF_IRQEN_REG(v, LPAIF_IRQ_PORT_HOST);
			val_mask = LPAIF_IRQ_ALL(ch);
			val_irqen = LPAIF_IRQ_ALL(ch);
			break;
		case LPASS_CDC_DMA_RX0 ... LPASS_CDC_DMA_RX9:
		case LPASS_CDC_DMA_TX0 ... LPASS_CDC_DMA_TX8:
			ret = regmap_fields_write(dmactl->dyncclk, id, LPAIF_DMACTL_DYNCLK_ON);
			if (ret) {
				dev_err(soc_runtime->dev,
					"error writing to rdmactl reg field: %d\n", ret);
				return ret;
			}
			reg_irqclr = LPAIF_RXTX_IRQCLEAR_REG(v, LPAIF_IRQ_PORT_HOST);
			val_irqclr = LPAIF_IRQ_ALL(ch);

			reg_irqen = LPAIF_RXTX_IRQEN_REG(v, LPAIF_IRQ_PORT_HOST);
			val_mask = LPAIF_IRQ_ALL(ch);
			val_irqen = LPAIF_IRQ_ALL(ch);
			break;
		case LPASS_CDC_DMA_VA_TX0 ... LPASS_CDC_DMA_VA_TX8:
			ret = regmap_fields_write(dmactl->dyncclk, id, LPAIF_DMACTL_DYNCLK_ON);
			if (ret) {
				dev_err(soc_runtime->dev,
					"error writing to rdmactl reg field: %d\n", ret);
				return ret;
			}
			reg_irqclr = LPAIF_VA_IRQCLEAR_REG(v, LPAIF_IRQ_PORT_HOST);
			val_irqclr = LPAIF_IRQ_ALL(ch);

			reg_irqen = LPAIF_VA_IRQEN_REG(v, LPAIF_IRQ_PORT_HOST);
			val_mask = LPAIF_IRQ_ALL(ch);
			val_irqen = LPAIF_IRQ_ALL(ch);
			break;
		default:
			dev_err(soc_runtime->dev, "%s: invalid %d interface\n", __func__, dai_id);
			return -EINVAL;
		}

		ret = regmap_write_bits(map, reg_irqclr, val_irqclr, val_irqclr);
		if (ret) {
			dev_err(soc_runtime->dev, "error writing to irqclear reg: %d\n", ret);
			return ret;
		}
		ret = regmap_update_bits(map, reg_irqen, val_mask, val_irqen);
		if (ret) {
			dev_err(soc_runtime->dev, "error writing to irqen reg: %d\n", ret);
			return ret;
		}
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		ret = regmap_fields_write(dmactl->enable, id,
					 LPAIF_DMACTL_ENABLE_OFF);
		if (ret) {
			dev_err(soc_runtime->dev,
				"error writing to rdmactl reg: %d\n", ret);
			return ret;
		}
		switch (dai_id) {
		case LPASS_DP_RX:
			ret = regmap_fields_write(dmactl->dyncclk, id,
					 LPAIF_DMACTL_DYNCLK_OFF);
			if (ret) {
				dev_err(soc_runtime->dev,
					"error writing to rdmactl reg: %d\n", ret);
				return ret;
			}
			reg_irqen = LPASS_HDMITX_APP_IRQEN_REG(v);
			val_mask = (LPAIF_IRQ_ALL(ch) |
					LPAIF_IRQ_HDMI_REQ_ON_PRELOAD(ch) |
					LPAIF_IRQ_HDMI_METADONE |
					LPAIF_IRQ_HDMI_SDEEP_AUD_DIS(ch));
			val_irqen = 0;
			break;
		case MI2S_PRIMARY:
		case MI2S_SECONDARY:
		case MI2S_TERTIARY:
		case MI2S_QUATERNARY:
		case MI2S_QUINARY:
			reg_irqen = LPAIF_IRQEN_REG(v, LPAIF_IRQ_PORT_HOST);
			val_mask = LPAIF_IRQ_ALL(ch);
			val_irqen = 0;
			break;
		case LPASS_CDC_DMA_RX0 ... LPASS_CDC_DMA_RX9:
		case LPASS_CDC_DMA_TX0 ... LPASS_CDC_DMA_TX8:
			ret = regmap_fields_write(dmactl->dyncclk, id, LPAIF_DMACTL_DYNCLK_OFF);
			if (ret) {
				dev_err(soc_runtime->dev,
					"error writing to rdmactl reg field: %d\n", ret);
				return ret;
			}

			reg_irqclr = LPAIF_RXTX_IRQCLEAR_REG(v, LPAIF_IRQ_PORT_HOST);
			val_irqclr = LPAIF_IRQ_ALL(ch);

			reg_irqen = LPAIF_RXTX_IRQEN_REG(v, LPAIF_IRQ_PORT_HOST);
			val_mask = LPAIF_IRQ_ALL(ch);
			val_irqen = LPAIF_IRQ_ALL(ch);
			break;
		case LPASS_CDC_DMA_VA_TX0 ... LPASS_CDC_DMA_VA_TX8:
			ret = regmap_fields_write(dmactl->dyncclk, id, LPAIF_DMACTL_DYNCLK_OFF);
			if (ret) {
				dev_err(soc_runtime->dev,
					"error writing to rdmactl reg field: %d\n", ret);
				return ret;
			}

			reg_irqclr = LPAIF_VA_IRQCLEAR_REG(v, LPAIF_IRQ_PORT_HOST);
			val_irqclr = LPAIF_IRQ_ALL(ch);

			reg_irqen = LPAIF_VA_IRQEN_REG(v, LPAIF_IRQ_PORT_HOST);
			val_mask = LPAIF_IRQ_ALL(ch);
			val_irqen = LPAIF_IRQ_ALL(ch);
			break;
		default:
			dev_err(soc_runtime->dev, "%s: invalid %d interface\n", __func__, dai_id);
			return -EINVAL;
		}

		ret = regmap_update_bits(map, reg_irqen, val_mask, val_irqen);
		if (ret) {
			dev_err(soc_runtime->dev,
				"error writing to irqen reg: %d\n", ret);
			return ret;
		}
		break;
	}

	return 0;
}

static snd_pcm_uframes_t lpass_platform_pcmops_pointer(
		struct snd_soc_component *component,
		struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *soc_runtime = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(soc_runtime, 0);
	struct lpass_data *drvdata = snd_soc_component_get_drvdata(component);
	struct snd_pcm_runtime *rt = substream->runtime;
	struct lpass_pcm_data *pcm_data = rt->private_data;
	const struct lpass_variant *v = drvdata->variant;
	unsigned int base_addr, curr_addr;
	int ret, ch, dir = substream->stream;
	struct regmap *map;
	unsigned int dai_id = cpu_dai->driver->id;

	map = __lpass_get_regmap_handle(substream, component);
	ch = pcm_data->dma_ch;

	ret = regmap_read(map,
			LPAIF_DMABASE_REG(v, ch, dir, dai_id), &base_addr);
	if (ret) {
		dev_err(soc_runtime->dev,
			"error reading from rdmabase reg: %d\n", ret);
		return ret;
	}

	ret = regmap_read(map,
			LPAIF_DMACURR_REG(v, ch, dir, dai_id), &curr_addr);
	if (ret) {
		dev_err(soc_runtime->dev,
			"error reading from rdmacurr reg: %d\n", ret);
		return ret;
	}

	return bytes_to_frames(substream->runtime, curr_addr - base_addr);
}

static int lpass_platform_cdc_dma_mmap(struct snd_pcm_substream *substream,
				       struct vm_area_struct *vma)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned long size, offset;

	vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
	size = vma->vm_end - vma->vm_start;
	offset = vma->vm_pgoff << PAGE_SHIFT;
	return io_remap_pfn_range(vma, vma->vm_start,
			(runtime->dma_addr + offset) >> PAGE_SHIFT,
			size, vma->vm_page_prot);

}

static int lpass_platform_pcmops_mmap(struct snd_soc_component *component,
				      struct snd_pcm_substream *substream,
				      struct vm_area_struct *vma)
{
	struct snd_soc_pcm_runtime *soc_runtime = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(soc_runtime, 0);
	unsigned int dai_id = cpu_dai->driver->id;

	if (is_cdc_dma_port(dai_id))
		return lpass_platform_cdc_dma_mmap(substream, vma);

	return snd_pcm_lib_default_mmap(substream, vma);
}

static irqreturn_t lpass_dma_interrupt_handler(
			struct snd_pcm_substream *substream,
			struct lpass_data *drvdata,
			int chan, u32 interrupts)
{
	struct snd_soc_pcm_runtime *soc_runtime = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(soc_runtime, 0);
	const struct lpass_variant *v = drvdata->variant;
	irqreturn_t ret = IRQ_NONE;
	int rv;
	unsigned int reg, val, mask;
	struct regmap *map;
	unsigned int dai_id = cpu_dai->driver->id;

	mask = LPAIF_IRQ_ALL(chan);
	switch (dai_id) {
	case LPASS_DP_RX:
		map = drvdata->hdmiif_map;
		reg = LPASS_HDMITX_APP_IRQCLEAR_REG(v);
		val = (LPAIF_IRQ_HDMI_REQ_ON_PRELOAD(chan) |
		LPAIF_IRQ_HDMI_METADONE |
		LPAIF_IRQ_HDMI_SDEEP_AUD_DIS(chan));
	break;
	case MI2S_PRIMARY:
	case MI2S_SECONDARY:
	case MI2S_TERTIARY:
	case MI2S_QUATERNARY:
	case MI2S_QUINARY:
		map = drvdata->lpaif_map;
		reg = LPAIF_IRQCLEAR_REG(v, LPAIF_IRQ_PORT_HOST);
		val = 0;
	break;
	case LPASS_CDC_DMA_RX0 ... LPASS_CDC_DMA_RX9:
	case LPASS_CDC_DMA_TX0 ... LPASS_CDC_DMA_TX8:
		map = drvdata->rxtx_lpaif_map;
		reg = LPAIF_RXTX_IRQCLEAR_REG(v, LPAIF_IRQ_PORT_HOST);
		val = 0;
	break;
	case LPASS_CDC_DMA_VA_TX0 ... LPASS_CDC_DMA_VA_TX8:
		map = drvdata->va_lpaif_map;
		reg = LPAIF_VA_IRQCLEAR_REG(v, LPAIF_IRQ_PORT_HOST);
		val = 0;
	break;
	default:
	dev_err(soc_runtime->dev, "%s: invalid  %d interface\n", __func__, dai_id);
	return -EINVAL;
	}
	if (interrupts & LPAIF_IRQ_PER(chan)) {
		rv = regmap_write_bits(map, reg, mask, (LPAIF_IRQ_PER(chan) | val));
		if (rv) {
			dev_err(soc_runtime->dev,
				"error writing to irqclear reg: %d\n", rv);
			return IRQ_NONE;
		}
		snd_pcm_period_elapsed(substream);
		ret = IRQ_HANDLED;
	}

	if (interrupts & LPAIF_IRQ_XRUN(chan)) {
		rv = regmap_write_bits(map, reg, mask, (LPAIF_IRQ_XRUN(chan) | val));
		if (rv) {
			dev_err(soc_runtime->dev,
				"error writing to irqclear reg: %d\n", rv);
			return IRQ_NONE;
		}
		dev_warn_ratelimited(soc_runtime->dev, "xrun warning\n");

		snd_pcm_stop_xrun(substream);
		ret = IRQ_HANDLED;
	}

	if (interrupts & LPAIF_IRQ_ERR(chan)) {
		rv = regmap_write_bits(map, reg, mask, (LPAIF_IRQ_ERR(chan) | val));
		if (rv) {
			dev_err(soc_runtime->dev,
				"error writing to irqclear reg: %d\n", rv);
			return IRQ_NONE;
		}
		dev_err(soc_runtime->dev, "bus access error\n");
		snd_pcm_stop(substream, SNDRV_PCM_STATE_DISCONNECTED);
		ret = IRQ_HANDLED;
	}

	if (interrupts & val) {
		rv = regmap_write(map, reg, val);
		if (rv) {
			dev_err(soc_runtime->dev,
			"error writing to irqclear reg: %d\n", rv);
			return IRQ_NONE;
		}
		ret = IRQ_HANDLED;
	}

	return ret;
}

static irqreturn_t lpass_platform_lpaif_irq(int irq, void *data)
{
	struct lpass_data *drvdata = data;
	const struct lpass_variant *v = drvdata->variant;
	unsigned int irqs;
	int rv, chan;

	rv = regmap_read(drvdata->lpaif_map,
			LPAIF_IRQSTAT_REG(v, LPAIF_IRQ_PORT_HOST), &irqs);
	if (rv) {
		pr_err("error reading from irqstat reg: %d\n", rv);
		return IRQ_NONE;
	}

	/* Handle per channel interrupts */
	for (chan = 0; chan < LPASS_MAX_DMA_CHANNELS; chan++) {
		if (irqs & LPAIF_IRQ_ALL(chan) && drvdata->substream[chan]) {
			rv = lpass_dma_interrupt_handler(
						drvdata->substream[chan],
						drvdata, chan, irqs);
			if (rv != IRQ_HANDLED)
				return rv;
		}
	}

	return IRQ_HANDLED;
}

static irqreturn_t lpass_platform_hdmiif_irq(int irq, void *data)
{
	struct lpass_data *drvdata = data;
	const struct lpass_variant *v = drvdata->variant;
	unsigned int irqs;
	int rv, chan;

	rv = regmap_read(drvdata->hdmiif_map,
			LPASS_HDMITX_APP_IRQSTAT_REG(v), &irqs);
	if (rv) {
		pr_err("error reading from irqstat reg: %d\n", rv);
		return IRQ_NONE;
	}

	/* Handle per channel interrupts */
	for (chan = 0; chan < LPASS_MAX_HDMI_DMA_CHANNELS; chan++) {
		if (irqs & (LPAIF_IRQ_ALL(chan) | LPAIF_IRQ_HDMI_REQ_ON_PRELOAD(chan) |
				LPAIF_IRQ_HDMI_METADONE |
				LPAIF_IRQ_HDMI_SDEEP_AUD_DIS(chan))
			&& drvdata->hdmi_substream[chan]) {
			rv = lpass_dma_interrupt_handler(
						drvdata->hdmi_substream[chan],
						drvdata, chan, irqs);
			if (rv != IRQ_HANDLED)
				return rv;
		}
	}
	return IRQ_HANDLED;
}

static irqreturn_t lpass_platform_rxtxif_irq(int irq, void *data)
{
	struct lpass_data *drvdata = data;
	const struct lpass_variant *v = drvdata->variant;
	unsigned int irqs;
	irqreturn_t rv;
	int chan;

	rv = regmap_read(drvdata->rxtx_lpaif_map,
			LPAIF_RXTX_IRQSTAT_REG(v, LPAIF_IRQ_PORT_HOST), &irqs);

	/* Handle per channel interrupts */
	for (chan = 0; chan < LPASS_MAX_CDC_DMA_CHANNELS; chan++) {
		if (irqs & LPAIF_IRQ_ALL(chan) && drvdata->rxtx_substream[chan]) {
			rv = lpass_dma_interrupt_handler(
						drvdata->rxtx_substream[chan],
						drvdata, chan, irqs);
			if (rv != IRQ_HANDLED)
				return rv;
		}
	}

	return IRQ_HANDLED;
}

static irqreturn_t lpass_platform_vaif_irq(int irq, void *data)
{
	struct lpass_data *drvdata = data;
	const struct lpass_variant *v = drvdata->variant;
	unsigned int irqs;
	irqreturn_t rv;
	int chan;

	rv = regmap_read(drvdata->va_lpaif_map,
			LPAIF_VA_IRQSTAT_REG(v, LPAIF_IRQ_PORT_HOST), &irqs);

	/* Handle per channel interrupts */
	for (chan = 0; chan < LPASS_MAX_VA_CDC_DMA_CHANNELS; chan++) {
		if (irqs & LPAIF_IRQ_ALL(chan) && drvdata->va_substream[chan]) {
			rv = lpass_dma_interrupt_handler(
						drvdata->va_substream[chan],
						drvdata, chan, irqs);
			if (rv != IRQ_HANDLED)
				return rv;
		}
	}
	return IRQ_HANDLED;
}

static int lpass_platform_prealloc_cdc_dma_buffer(struct snd_soc_component *component,
						  struct snd_pcm *pcm, int dai_id)
{
	struct lpass_data *drvdata = snd_soc_component_get_drvdata(component);
	struct snd_pcm_substream *substream;
	struct snd_dma_buffer *buf;

	if (pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream)
		substream = pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream;
	else
		substream = pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream;

	buf = &substream->dma_buffer;
	buf->dev.dev = pcm->card->dev;
	buf->private_data = NULL;

	/* Assign Codec DMA buffer pointers */
	buf->dev.type = SNDRV_DMA_TYPE_CONTINUOUS;

	switch (dai_id) {
	case LPASS_CDC_DMA_RX0 ... LPASS_CDC_DMA_RX9:
		buf->bytes = lpass_platform_rxtx_hardware.buffer_bytes_max;
		buf->addr = drvdata->rxtx_cdc_dma_lpm_buf;
		break;
	case LPASS_CDC_DMA_TX0 ... LPASS_CDC_DMA_TX8:
		buf->bytes = lpass_platform_rxtx_hardware.buffer_bytes_max;
		buf->addr = drvdata->rxtx_cdc_dma_lpm_buf + LPASS_RXTX_CDC_DMA_LPM_BUFF_SIZE;
		break;
	case LPASS_CDC_DMA_VA_TX0 ... LPASS_CDC_DMA_VA_TX8:
		buf->bytes = lpass_platform_va_hardware.buffer_bytes_max;
		buf->addr = drvdata->va_cdc_dma_lpm_buf;
		break;
	default:
		break;
	}

	buf->area = (unsigned char * __force)memremap(buf->addr, buf->bytes, MEMREMAP_WC);

	return 0;
}

static int lpass_platform_pcm_new(struct snd_soc_component *component,
				  struct snd_soc_pcm_runtime *soc_runtime)
{
	struct snd_pcm *pcm = soc_runtime->pcm;
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(soc_runtime, 0);
	unsigned int dai_id = cpu_dai->driver->id;

	size_t size = lpass_platform_pcm_hardware.buffer_bytes_max;

	/*
	 * Lpass codec dma can access only lpass lpm hardware memory.
	 * ioremap is for HLOS to access hardware memory.
	 */
	if (is_cdc_dma_port(dai_id))
		return lpass_platform_prealloc_cdc_dma_buffer(component, pcm, dai_id);

	return snd_pcm_set_fixed_buffer_all(pcm, SNDRV_DMA_TYPE_NONCOHERENT,
					    component->dev, size);
}

static int lpass_platform_pcmops_suspend(struct snd_soc_component *component)
{
	struct lpass_data *drvdata = snd_soc_component_get_drvdata(component);
	struct regmap *map;
	unsigned int dai_id = component->id;

	if (dai_id == LPASS_DP_RX)
		map = drvdata->hdmiif_map;
	else
		map = drvdata->lpaif_map;

	regcache_cache_only(map, true);
	regcache_mark_dirty(map);

	return 0;
}

static int lpass_platform_pcmops_resume(struct snd_soc_component *component)
{
	struct lpass_data *drvdata = snd_soc_component_get_drvdata(component);
	struct regmap *map;
	unsigned int dai_id = component->id;

	if (dai_id == LPASS_DP_RX)
		map = drvdata->hdmiif_map;
	else
		map = drvdata->lpaif_map;

	regcache_cache_only(map, false);
	return regcache_sync(map);
}

static int lpass_platform_copy(struct snd_soc_component *component,
			       struct snd_pcm_substream *substream, int channel,
			       unsigned long pos, struct iov_iter *buf,
			       unsigned long bytes)
{
	struct snd_pcm_runtime *rt = substream->runtime;
	unsigned int dai_id = component->id;
	int ret = 0;

	void __iomem *dma_buf = (void __iomem *) (rt->dma_area + pos +
				channel * (rt->dma_bytes / rt->channels));

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		if (is_cdc_dma_port(dai_id)) {
			ret = copy_from_iter_toio(dma_buf, buf, bytes);
		} else {
			if (copy_from_iter((void __force *)dma_buf, bytes, buf) != bytes)
				ret = -EFAULT;
		}
	} else if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		if (is_cdc_dma_port(dai_id)) {
			ret = copy_to_iter_fromio(buf, dma_buf, bytes);
		} else {
			if (copy_to_iter((void __force *)dma_buf, bytes, buf) != bytes)
				ret = -EFAULT;
		}
	}

	return ret;
}

static const struct snd_soc_component_driver lpass_component_driver = {
	.name		= DRV_NAME,
	.open		= lpass_platform_pcmops_open,
	.close		= lpass_platform_pcmops_close,
	.hw_params	= lpass_platform_pcmops_hw_params,
	.hw_free	= lpass_platform_pcmops_hw_free,
	.prepare	= lpass_platform_pcmops_prepare,
	.trigger	= lpass_platform_pcmops_trigger,
	.pointer	= lpass_platform_pcmops_pointer,
	.mmap		= lpass_platform_pcmops_mmap,
	.pcm_construct	= lpass_platform_pcm_new,
	.suspend		= lpass_platform_pcmops_suspend,
	.resume			= lpass_platform_pcmops_resume,
	.copy		= lpass_platform_copy,

};

int asoc_qcom_lpass_platform_register(struct platform_device *pdev)
{
	struct lpass_data *drvdata = platform_get_drvdata(pdev);
	const struct lpass_variant *v = drvdata->variant;
	int ret;

	drvdata->lpaif_irq = platform_get_irq_byname(pdev, "lpass-irq-lpaif");
	if (drvdata->lpaif_irq < 0)
		return -ENODEV;

	/* ensure audio hardware is disabled */
	ret = regmap_write(drvdata->lpaif_map,
			LPAIF_IRQEN_REG(v, LPAIF_IRQ_PORT_HOST), 0);
	if (ret) {
		dev_err(&pdev->dev, "error writing to irqen reg: %d\n", ret);
		return ret;
	}

	ret = devm_request_irq(&pdev->dev, drvdata->lpaif_irq,
			lpass_platform_lpaif_irq, IRQF_TRIGGER_RISING,
			"lpass-irq-lpaif", drvdata);
	if (ret) {
		dev_err(&pdev->dev, "irq request failed: %d\n", ret);
		return ret;
	}

	ret = lpass_platform_alloc_dmactl_fields(&pdev->dev,
						 drvdata->lpaif_map);
	if (ret) {
		dev_err(&pdev->dev,
			"error initializing dmactl fields: %d\n", ret);
		return ret;
	}

	if (drvdata->codec_dma_enable) {
		ret = regmap_write(drvdata->rxtx_lpaif_map,
			LPAIF_RXTX_IRQEN_REG(v, LPAIF_IRQ_PORT_HOST), 0x0);
		if (ret) {
			dev_err(&pdev->dev, "error writing to rxtx irqen reg: %d\n", ret);
			return ret;
		}
		ret = regmap_write(drvdata->va_lpaif_map,
			LPAIF_VA_IRQEN_REG(v, LPAIF_IRQ_PORT_HOST), 0x0);
		if (ret) {
			dev_err(&pdev->dev, "error writing to rxtx irqen reg: %d\n", ret);
			return ret;
		}
		drvdata->rxtxif_irq = platform_get_irq_byname(pdev, "lpass-irq-rxtxif");
		if (drvdata->rxtxif_irq < 0)
			return -ENODEV;

		ret = devm_request_irq(&pdev->dev, drvdata->rxtxif_irq,
				lpass_platform_rxtxif_irq, 0, "lpass-irq-rxtxif", drvdata);
		if (ret) {
			dev_err(&pdev->dev, "rxtx irq request failed: %d\n", ret);
			return ret;
		}

		ret = lpass_platform_alloc_rxtx_dmactl_fields(&pdev->dev,
						 drvdata->rxtx_lpaif_map);
		if (ret) {
			dev_err(&pdev->dev,
				"error initializing rxtx dmactl fields: %d\n", ret);
			return ret;
		}

		drvdata->vaif_irq = platform_get_irq_byname(pdev, "lpass-irq-vaif");
		if (drvdata->vaif_irq < 0)
			return -ENODEV;

		ret = devm_request_irq(&pdev->dev, drvdata->vaif_irq,
				lpass_platform_vaif_irq, 0, "lpass-irq-vaif", drvdata);
		if (ret) {
			dev_err(&pdev->dev, "va irq request failed: %d\n", ret);
			return ret;
		}

		ret = lpass_platform_alloc_va_dmactl_fields(&pdev->dev,
						 drvdata->va_lpaif_map);
		if (ret) {
			dev_err(&pdev->dev,
				"error initializing va dmactl fields: %d\n", ret);
			return ret;
		}
	}

	if (drvdata->hdmi_port_enable) {
		drvdata->hdmiif_irq = platform_get_irq_byname(pdev, "lpass-irq-hdmi");
		if (drvdata->hdmiif_irq < 0)
			return -ENODEV;

		ret = devm_request_irq(&pdev->dev, drvdata->hdmiif_irq,
				lpass_platform_hdmiif_irq, 0, "lpass-irq-hdmi", drvdata);
		if (ret) {
			dev_err(&pdev->dev, "irq hdmi request failed: %d\n", ret);
			return ret;
		}
		ret = regmap_write(drvdata->hdmiif_map,
				LPASS_HDMITX_APP_IRQEN_REG(v), 0);
		if (ret) {
			dev_err(&pdev->dev, "error writing to hdmi irqen reg: %d\n", ret);
			return ret;
		}

		ret = lpass_platform_alloc_hdmidmactl_fields(&pdev->dev,
							 drvdata->hdmiif_map);
		if (ret) {
			dev_err(&pdev->dev,
				"error initializing hdmidmactl fields: %d\n", ret);
			return ret;
		}
	}
	return devm_snd_soc_register_component(&pdev->dev,
			&lpass_component_driver, NULL, 0);
}
EXPORT_SYMBOL_GPL(asoc_qcom_lpass_platform_register);

MODULE_DESCRIPTION("QTi LPASS Platform Driver");
MODULE_LICENSE("GPL");
