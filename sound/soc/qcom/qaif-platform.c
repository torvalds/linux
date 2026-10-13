// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * qaif-platform.c -- ALSA SoC PCM platform driver for the Qualcomm Audio Interface (QAIF)
 */

#include <linux/dma-mapping.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include "qaif-reg.h"
#include "qaif.h"

#define DRV_NAME "qaif-platform"

/* period: 5ms @ 48kHz/1ch/S16 min, 20ms @ 48kHz/8ch/S32 max */
#define QAIF_PLATFORM_PERIOD_BYTES_MIN		(48 * 5 * 1 * 2)	/* 480 B */
#define QAIF_PLATFORM_PERIOD_BYTES_MAX		(48 * 20 * 8 * 4)	/* 30720 B */
#define QAIF_PLATFORM_PERIODS_MIN		2
#define QAIF_PLATFORM_PERIODS_MAX		4
/* QAIF_PLATFORM_PERIODS_MAX * QAIF_PLATFORM_PERIOD_BYTES_MAX = 122880 B */
#define QAIF_PLATFORM_BUFFER_BYTES_MAX		\
	(QAIF_PLATFORM_PERIODS_MAX * QAIF_PLATFORM_PERIOD_BYTES_MAX)

/* DMA CFG register bit positions */
#define QAIF_DMACFG_SHRAM_WM_MASK	GENMASK(11, 0)
#define QAIF_DMACFG_BURST4_BIT		BIT(18)
#define QAIF_DMACFG_DYNCLK_BIT		BIT(24)

/* DMA CTL register bit positions */
#define QAIF_DMACTL_ENABLE_BIT		BIT(0)
#define QAIF_ALL_CLIENTS_MASK		(QAIF_BITMASK_AIF_RDDMA_WRDMA | \
					 QAIF_BITMASK_CIF_RDDMA_WRDMA)

static const struct snd_pcm_hardware qaif_platform_hardware = {
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
	.buffer_bytes_max	=	QAIF_PLATFORM_BUFFER_BYTES_MAX,
	.period_bytes_min	=	QAIF_PLATFORM_PERIOD_BYTES_MIN,
	.period_bytes_max	=	QAIF_PLATFORM_PERIOD_BYTES_MAX,
	.periods_min		=	QAIF_PLATFORM_PERIODS_MIN,
	.periods_max		=	QAIF_PLATFORM_PERIODS_MAX,
	.fifo_size		=	0,
};

static int qaif_map_ee_resource(struct qaif_drv_data *drvdata)
{
	const struct qaif_variant *v = drvdata->variant;
	struct regmap *map = drvdata->audio_qaif_map;
	int ret;

	ret = regmap_write(map, QAIF_EE_RDDMA_MAP_REG(v), GENMASK(v->num_rddma - 1, 0));
	if (ret)
		return ret;
	ret = regmap_write(map, QAIF_EE_WRDMA_MAP_REG(v), GENMASK(v->num_wrdma - 1, 0));
	if (ret)
		return ret;
	if (v->num_intf > 0) {
		ret = regmap_write(map, QAIF_EE_INTF_MAP_REG(v), GENMASK(v->num_intf - 1, 0));
		if (ret)
			return ret;
	}
	ret = regmap_write(map, QAIF_EE_CODEC_RDDMA_MAP_REG(v),
			   GENMASK(v->num_codec_rddma - 1, 0));
	if (ret)
		return ret;
	return regmap_write(map, QAIF_EE_CODEC_WRDMA_MAP_REG(v),
			    GENMASK(v->num_codec_wrdma - 1, 0));
}

static int qaif_map_dma_path(struct qaif_drv_data *drvdata)
{
	struct regmap *map = drvdata->audio_qaif_map;
	int ret;

	if (drvdata->variant->qxm_type != QAIF_QXM0) {
		dev_err(regmap_get_device(map),
			"only QXM0 is supported, qxm_type=%d\n",
			drvdata->variant->qxm_type);
		return -EINVAL;
	}

	ret = regmap_write(map, QAIF_RDDMA_MAP_QXM, QAIF_QXM0);
	if (ret)
		return ret;
	ret = regmap_write(map, QAIF_WRDMA_MAP_QXM, QAIF_QXM0);
	if (ret)
		return ret;
	ret = regmap_write(map, QAIF_CDC_RDDMA_MAP_QXM, QAIF_QXM0);
	if (ret)
		return ret;
	return regmap_write(map, QAIF_CDC_WRDMA_MAP_QXM, QAIF_QXM0);
}

static int qaif_config_shram(struct qaif_drv_data *drvdata)
{
	const struct qaif_variant *v = drvdata->variant;
	struct regmap *map = drvdata->audio_qaif_map;
	u32 start_addr, shram_len;
	int ret, i;

	if (v->qxm_type != QAIF_QXM0) {
		dev_err(regmap_get_device(map),
			"only QXM0 is supported, qxm_type=%d\n",
			v->qxm_type);
		return -EINVAL;
	}

	start_addr = v->rddma_shram_start_addr[QAIF_AIF_DMA];
	shram_len = v->rddma_shram_len;
	for (i = 0; i < v->num_rddma; i++) {
		ret = regmap_write(map, QAIF_RDDMA_QXM0_SHRAM_ST_ADDR(i),
				   start_addr + (shram_len * i));
		if (ret)
			return ret;
		ret = regmap_write(map, QAIF_RDDMA_QXM0_SHRAM_LEN(i), shram_len);
		if (ret)
			return ret;
	}

	start_addr = v->wrdma_shram_start_addr[QAIF_AIF_DMA];
	shram_len = v->wrdma_shram_len;
	for (i = 0; i < v->num_wrdma; i++) {
		ret = regmap_write(map, QAIF_WRDMA_QXM0_SHRAM_ST_ADDR(i),
				   start_addr + (shram_len * i));
		if (ret)
			return ret;
		ret = regmap_write(map, QAIF_WRDMA_QXM0_SHRAM_LEN(i), shram_len);
		if (ret)
			return ret;
	}

	start_addr = v->rddma_shram_start_addr[QAIF_CIF_DMA];
	shram_len = v->rddma_shram_len;
	for (i = 0; i < v->num_codec_rddma; i++) {
		ret = regmap_write(map, QAIF_CDC_RDDMA_QXM0_SHRAM_ST_ADDR(i),
				   start_addr + (shram_len * i));
		if (ret)
			return ret;
		ret = regmap_write(map, QAIF_CDC_RDDMA_QXM0_SHRAM_LEN(i), shram_len);
		if (ret)
			return ret;
	}

	start_addr = v->wrdma_shram_start_addr[QAIF_CIF_DMA];
	shram_len = v->wrdma_shram_len;
	for (i = 0; i < v->num_codec_wrdma; i++) {
		ret = regmap_write(map, QAIF_CDC_WRDMA_QXM0_SHRAM_ST_ADDR(i),
				   start_addr + (shram_len * i));
		if (ret)
			return ret;
		ret = regmap_write(map, QAIF_CDC_WRDMA_QXM0_SHRAM_LEN(i), shram_len);
		if (ret)
			return ret;
	}
	return 0;
}

static int qaif_init(struct snd_soc_component *component)
{
	struct qaif_drv_data *drvdata = snd_soc_component_get_drvdata(component);
	int ret;

	if (drvdata->qaif_hw_configured)
		return 0;

	ret = qaif_config_shram(drvdata);
	if (ret) {
		dev_err(component->dev, "failed to config shram: %d\n", ret);
		return ret;
	}

	ret = qaif_map_ee_resource(drvdata);
	if (ret) {
		dev_err(component->dev, "failed to map EE resources: %d\n", ret);
		return ret;
	}

	ret = qaif_map_dma_path(drvdata);
	if (ret)
		return ret;

	drvdata->qaif_hw_configured = true;
	return 0;
}

static int qaif_platform_pcmops_open(struct snd_soc_component *component,
				     struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *soc_runtime = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(soc_runtime, 0);
	struct qaif_drv_data *drvdata = snd_soc_component_get_drvdata(component);
	const struct qaif_variant *v = drvdata->variant;
	unsigned int dai_id = cpu_dai->driver->id;
	struct qaif_pcm_data *data;
	int ret, stream_dma_idx, dma_reg_idx, dir = substream->stream;

	dma_reg_idx = v->get_dma_idx(dai_id);
	if (dma_reg_idx < 0) {
		dev_err(component->dev, "invalid DMA register index for DAI %u: %d\n",
			dai_id, dma_reg_idx);
		return dma_reg_idx;
	}

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	runtime->private_data = data;

	mutex_lock(&drvdata->stream_lock);
	stream_dma_idx = v->alloc_stream_dma_idx(drvdata, dir, dai_id);
	mutex_unlock(&drvdata->stream_lock);
	if (stream_dma_idx < 0) {
		ret = stream_dma_idx;
		goto err_free_data;
	}

	data->stream_dma_idx = stream_dma_idx;
	data->dma_reg_idx = dma_reg_idx;

	if (qaif_is_cif_port(dai_id))
		WRITE_ONCE(drvdata->cif_substream[stream_dma_idx], substream);
	else
		WRITE_ONCE(drvdata->aif_substream[stream_dma_idx], substream);

	snd_soc_set_runtime_hwparams(substream, &qaif_platform_hardware);

	/*
	 * The DMA buffer/period length registers are programmed in 64-bit
	 * (8-byte) words, so constrain buffer and period sizes to that step
	 * to keep the ALSA and hardware sizes identical.
	 */
	ret = snd_pcm_hw_constraint_step(runtime, 0,
					 SNDRV_PCM_HW_PARAM_BUFFER_BYTES,
					 BIT(QAIF_DMA_BYTES_TO_WORDS_SHIFT));
	if (ret >= 0)
		ret = snd_pcm_hw_constraint_step(runtime, 0,
						 SNDRV_PCM_HW_PARAM_PERIOD_BYTES,
						 BIT(QAIF_DMA_BYTES_TO_WORDS_SHIFT));
	if (ret >= 0)
		ret = snd_pcm_hw_constraint_integer(runtime,
						    SNDRV_PCM_HW_PARAM_PERIODS);
	if (ret < 0) {
		dev_err(soc_runtime->dev, "setting constraints failed: %d\n", ret);
		if (qaif_is_cif_port(dai_id))
			WRITE_ONCE(drvdata->cif_substream[stream_dma_idx], NULL);
		else
			WRITE_ONCE(drvdata->aif_substream[stream_dma_idx], NULL);
		mutex_lock(&drvdata->stream_lock);
		v->free_stream_dma_idx(drvdata, stream_dma_idx, dai_id);
		mutex_unlock(&drvdata->stream_lock);
		goto err_free_data;
	}
	return 0;

err_free_data:
	kfree(data);
	return ret;
}

static int qaif_platform_pcmops_close(struct snd_soc_component *component,
				      struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *soc_runtime = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(soc_runtime, 0);
	struct qaif_drv_data *drvdata = snd_soc_component_get_drvdata(component);
	const struct qaif_variant *v = drvdata->variant;
	struct qaif_pcm_data *data = substream->runtime->private_data;
	unsigned int dai_id = cpu_dai->driver->id;

	if (qaif_is_cif_port(dai_id))
		WRITE_ONCE(drvdata->cif_substream[data->stream_dma_idx], NULL);
	else
		WRITE_ONCE(drvdata->aif_substream[data->stream_dma_idx], NULL);

	mutex_lock(&drvdata->stream_lock);
	v->free_stream_dma_idx(drvdata, data->stream_dma_idx, dai_id);
	mutex_unlock(&drvdata->stream_lock);

	substream->runtime->private_data = NULL;
	kfree(data);
	return 0;
}

static int qaif_platform_pcmops_hw_params(struct snd_soc_component *component,
					  struct snd_pcm_substream *substream,
					  struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *soc_runtime = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(soc_runtime, 0);
	struct qaif_drv_data *drvdata = snd_soc_component_get_drvdata(component);
	const struct qaif_variant *v = drvdata->variant;
	struct qaif_pcm_data *data = substream->runtime->private_data;
	unsigned int dai_id = cpu_dai->driver->id;
	int idx = data->dma_reg_idx;
	int ret;

	mutex_lock(&drvdata->stream_lock);
	ret = qaif_init(component);
	mutex_unlock(&drvdata->stream_lock);
	if (ret) {
		dev_err(soc_runtime->dev, "qaif_init failed: %d\n", ret);
		return ret;
	}

	ret = regmap_update_bits(drvdata->audio_qaif_map,
				 qaif_dmacfg_reg(v, idx, substream->stream, dai_id),
				 QAIF_DMACFG_BURST4_BIT | QAIF_DMACFG_SHRAM_WM_MASK,
				 QAIF_DMACFG_BURST4_BIT | QAIF_DMACTL_WM_5);
	if (ret)
		dev_err(soc_runtime->dev, "error updating DMA CFG: %d\n", ret);
	return ret;
}

static int qaif_platform_pcmops_hw_free(struct snd_soc_component *component,
					struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *soc_runtime = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(soc_runtime, 0);
	struct qaif_drv_data *drvdata = snd_soc_component_get_drvdata(component);
	const struct qaif_variant *v = drvdata->variant;
	struct qaif_pcm_data *data = substream->runtime->private_data;
	unsigned int dai_id = cpu_dai->driver->id;
	int idx = data->dma_reg_idx;
	int ret;

	ret = regmap_update_bits(drvdata->audio_qaif_map,
				 qaif_dmactl_reg(v, idx, substream->stream, dai_id),
				 QAIF_DMACTL_ENABLE_BIT, 0);
	if (ret) {
		dev_err(soc_runtime->dev, "error disabling DMA: %d\n", ret);
		return ret;
	}

	ret = regmap_write(drvdata->audio_qaif_map,
			   qaif_dmacfg_reg(v, idx, substream->stream, dai_id), 0);
	if (ret)
		dev_err(soc_runtime->dev, "error clearing DMA CFG: %d\n", ret);
	return ret;
}

static int qaif_platform_pcmops_prepare(struct snd_soc_component *component,
					struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *soc_runtime = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(soc_runtime, 0);
	struct qaif_drv_data *drvdata = snd_soc_component_get_drvdata(component);
	const struct qaif_variant *v = drvdata->variant;
	struct regmap *map = drvdata->audio_qaif_map;
	struct qaif_pcm_data *data = runtime->private_data;
	unsigned int dai_id = cpu_dai->driver->id;
	int idx = data->dma_reg_idx;
	int ret, dir = substream->stream;

	ret = clk_set_rate(drvdata->aud_dma_clk, QAIF_DMA_CLOCK_FREQ);
	if (ret) {
		dev_err(soc_runtime->dev, "error setting aud_dma_clk rate: %d\n", ret);
		return ret;
	}
	ret = clk_set_rate(drvdata->aud_dma_mem_clk, QAIF_DMA_CLOCK_FREQ);
	if (ret) {
		dev_err(soc_runtime->dev, "error setting aud_dma_mem_clk rate: %d\n", ret);
		return ret;
	}

	ret = regmap_write(map, qaif_sid_map_reg(dir, dai_id), drvdata->smmu_csid_bits);
	if (ret) {
		dev_err(soc_runtime->dev, "error writing to SID MAP reg: %d\n", ret);
		return ret;
	}

	ret = regmap_write(map, qaif_dmabase_reg(v, idx, dir, dai_id),
			   lower_32_bits(runtime->dma_addr));
	if (ret) {
		dev_err(soc_runtime->dev, "error writing to dma base reg: %d\n", ret);
		return ret;
	}

	ret = regmap_write(map, qaif_dmabuff_reg(v, idx, dir, dai_id),
			   (snd_pcm_lib_buffer_bytes(substream) >>
			    QAIF_DMA_BYTES_TO_WORDS_SHIFT) - 1);
	if (ret) {
		dev_err(soc_runtime->dev, "error writing to dma buf reg: %d\n", ret);
		return ret;
	}

	ret = regmap_write(map, qaif_dmaper_len_reg(v, idx, dir, dai_id),
			   (snd_pcm_lib_period_bytes(substream) >>
			    QAIF_DMA_BYTES_TO_WORDS_SHIFT) - 1);
	if (ret)
		dev_err(soc_runtime->dev, "error writing to dma period reg: %d\n", ret);
	return ret;
}

static snd_pcm_uframes_t qaif_platform_pcmops_pointer(struct snd_soc_component *component,
						      struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *soc_runtime = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(soc_runtime, 0);
	struct qaif_drv_data *drvdata = snd_soc_component_get_drvdata(component);
	const struct qaif_variant *v = drvdata->variant;
	struct regmap *map = drvdata->audio_qaif_map;
	struct qaif_pcm_data *data = substream->runtime->private_data;
	unsigned int dai_id = cpu_dai->driver->id;
	unsigned int base_addr, curr_addr;
	int idx = data->dma_reg_idx;
	int ret, dir = substream->stream;

	ret = regmap_read(map, qaif_dmabase_reg(v, idx, dir, dai_id), &base_addr);
	if (ret) {
		dev_err(soc_runtime->dev, "error reading from rdmabase reg: %d\n", ret);
		return SNDRV_PCM_POS_XRUN;
	}

	ret = regmap_read(map, qaif_dmacurr_reg(v, idx, dir, dai_id), &curr_addr);
	if (ret) {
		dev_err(soc_runtime->dev, "error reading from rdmacurr reg: %d\n", ret);
		return SNDRV_PCM_POS_XRUN;
	}

	return bytes_to_frames(substream->runtime,
			       (curr_addr - base_addr) %
			       snd_pcm_lib_buffer_bytes(substream));
}

static int qaif_platform_pcmops_mmap(struct snd_soc_component *component,
				     struct snd_pcm_substream *substream,
				     struct vm_area_struct *vma)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	return dma_mmap_coherent(component->dev, vma,
				 runtime->dma_area, runtime->dma_addr,
				 runtime->dma_bytes);
}

static int qaif_platform_pcm_new(struct snd_soc_component *component,
				 struct snd_soc_pcm_runtime *rtd)
{
	struct snd_pcm *pcm = rtd->pcm;

	return snd_pcm_set_fixed_buffer_all(pcm, SNDRV_DMA_TYPE_DEV,
					    component->dev,
					    QAIF_PLATFORM_BUFFER_BYTES_MAX);
}
