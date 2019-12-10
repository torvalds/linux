// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel SST Haswell/Broadwell PCM Support
 *
 * Copyright (C) 2013, Intel Corporation. All rights reserved.
 */

#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/pm_runtime.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/dmaengine_pcm.h>
#include <sound/soc.h>
#include <sound/tlv.h>
#include <sound/compress_driver.h>

#include "../haswell/sst-haswell-ipc.h"
#include "../common/sst-dsp-priv.h"
#include "../common/sst-dsp.h"

#define HSW_PCM_COUNT		6
#define HSW_VOLUME_MAX		0x7FFFFFFF	/* 0dB */

#define SST_OLD_POSITION(d, r, o) ((d) +		\
			frames_to_bytes(r, o))
#define SST_SAMPLES(r, x) (bytes_to_samples(r,	\
			frames_to_bytes(r, (x))))

/* simple volume table */
static const u32 volume_map[] = {
	HSW_VOLUME_MAX >> 30,
	HSW_VOLUME_MAX >> 29,
	HSW_VOLUME_MAX >> 28,
	HSW_VOLUME_MAX >> 27,
	HSW_VOLUME_MAX >> 26,
	HSW_VOLUME_MAX >> 25,
	HSW_VOLUME_MAX >> 24,
	HSW_VOLUME_MAX >> 23,
	HSW_VOLUME_MAX >> 22,
	HSW_VOLUME_MAX >> 21,
	HSW_VOLUME_MAX >> 20,
	HSW_VOLUME_MAX >> 19,
	HSW_VOLUME_MAX >> 18,
	HSW_VOLUME_MAX >> 17,
	HSW_VOLUME_MAX >> 16,
	HSW_VOLUME_MAX >> 15,
	HSW_VOLUME_MAX >> 14,
	HSW_VOLUME_MAX >> 13,
	HSW_VOLUME_MAX >> 12,
	HSW_VOLUME_MAX >> 11,
	HSW_VOLUME_MAX >> 10,
	HSW_VOLUME_MAX >> 9,
	HSW_VOLUME_MAX >> 8,
	HSW_VOLUME_MAX >> 7,
	HSW_VOLUME_MAX >> 6,
	HSW_VOLUME_MAX >> 5,
	HSW_VOLUME_MAX >> 4,
	HSW_VOLUME_MAX >> 3,
	HSW_VOLUME_MAX >> 2,
	HSW_VOLUME_MAX >> 1,
	HSW_VOLUME_MAX >> 0,
};

#define HSW_PCM_PERIODS_MAX	64
#define HSW_PCM_PERIODS_MIN	2

#define HSW_PCM_DAI_ID_SYSTEM	0
#define HSW_PCM_DAI_ID_OFFLOAD0	1
#define HSW_PCM_DAI_ID_OFFLOAD1	2
#define HSW_PCM_DAI_ID_LOOPBACK	3


static const struct snd_pcm_hardware hsw_pcm_hardware = {
	.info			= SNDRV_PCM_INFO_MMAP |
				  SNDRV_PCM_INFO_MMAP_VALID |
				  SNDRV_PCM_INFO_INTERLEAVED |
				  SNDRV_PCM_INFO_PAUSE |
				  SNDRV_PCM_INFO_RESUME |
				  SNDRV_PCM_INFO_NO_PERIOD_WAKEUP |
				  SNDRV_PCM_INFO_DRAIN_TRIGGER,
	.formats		= SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE |
				  SNDRV_PCM_FMTBIT_S32_LE,
	.period_bytes_min	= PAGE_SIZE,
	.period_bytes_max	= (HSW_PCM_PERIODS_MAX / HSW_PCM_PERIODS_MIN) * PAGE_SIZE,
	.periods_min		= HSW_PCM_PERIODS_MIN,
	.periods_max		= HSW_PCM_PERIODS_MAX,
	.buffer_bytes_max	= HSW_PCM_PERIODS_MAX * PAGE_SIZE,
};

struct hsw_pcm_module_map {
	int dai_id;
	int stream;
	enum sst_hsw_module_id mod_id;
};

/* private data for each PCM DSP stream */
struct hsw_pcm_data {
	int dai_id;
	struct sst_hsw_stream *stream;
	struct sst_module_runtime *runtime;
	struct sst_module_runtime_context context;
	struct snd_pcm *hsw_pcm;
	u32 volume[2];
	struct snd_pcm_substream *substream;
	struct snd_compr_stream *cstream;
	unsigned int wpos;
	struct mutex mutex;
	bool allocated;
	int persistent_offset;
};

enum hsw_pm_state {
	HSW_PM_STATE_D0 = 0,
	HSW_PM_STATE_RTD3 = 1,
	HSW_PM_STATE_D3 = 2,
};

/* private data for the driver */
struct hsw_priv_data {
	/* runtime DSP */
	struct sst_hsw *hsw;
	struct device *dev;
	enum hsw_pm_state pm_state;
	struct snd_soc_card *soc_card;
	struct sst_module_runtime *runtime_waves; /* sound effect module */

	/* page tables */
	struct snd_dma_buffer dmab[HSW_PCM_COUNT][2];

	/* DAI data */
	struct hsw_pcm_data pcm[HSW_PCM_COUNT][2];
};


/* static mappings between PCMs and modules - may be dynamic in future */
static struct hsw_pcm_module_map mod_map[] = {
	{HSW_PCM_DAI_ID_SYSTEM, 0, SST_HSW_MODULE_PCM_SYSTEM},
	{HSW_PCM_DAI_ID_OFFLOAD0, 0, SST_HSW_MODULE_PCM},
	{HSW_PCM_DAI_ID_OFFLOAD1, 0, SST_HSW_MODULE_PCM},
	{HSW_PCM_DAI_ID_LOOPBACK, 1, SST_HSW_MODULE_PCM_REFERENCE},
	{HSW_PCM_DAI_ID_SYSTEM, 1, SST_HSW_MODULE_PCM_CAPTURE},
};

static u32 hsw_notify_pointer(struct sst_hsw_stream *stream, void *data);

static inline u32 hsw_mixer_to_ipc(unsigned int value)
{
	if (value >= ARRAY_SIZE(volume_map))
		return volume_map[0];
	else
		return volume_map[value];
}

static inline unsigned int hsw_ipc_to_mixer(u32 value)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(volume_map); i++) {
		if (volume_map[i] >= value)
			return i;
	}

	return i - 1;
}

static int hsw_stream_volume_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct hsw_priv_data *pdata =
		snd_soc_component_get_drvdata(component);
	struct hsw_pcm_data *pcm_data;
	struct sst_hsw *hsw = pdata->hsw;
	u32 volume;
	int dai, stream;

	dai = mod_map[mc->reg].dai_id;
	stream = mod_map[mc->reg].stream;
	pcm_data = &pdata->pcm[dai][stream];

	mutex_lock(&pcm_data->mutex);
	pm_runtime_get_sync(pdata->dev);

	if (!pcm_data->stream) {
		pcm_data->volume[0] =
			hsw_mixer_to_ipc(ucontrol->value.integer.value[0]);
		pcm_data->volume[1] =
			hsw_mixer_to_ipc(ucontrol->value.integer.value[1]);
		pm_runtime_mark_last_busy(pdata->dev);
		pm_runtime_put_autosuspend(pdata->dev);
		mutex_unlock(&pcm_data->mutex);
		return 0;
	}

	if (ucontrol->value.integer.value[0] ==
		ucontrol->value.integer.value[1]) {
		volume = hsw_mixer_to_ipc(ucontrol->value.integer.value[0]);
		/* apply volume value to all channels */
		sst_hsw_stream_set_volume(hsw, pcm_data->stream, 0, SST_HSW_CHANNELS_ALL, volume);
	} else {
		volume = hsw_mixer_to_ipc(ucontrol->value.integer.value[0]);
		sst_hsw_stream_set_volume(hsw, pcm_data->stream, 0, 0, volume);
		volume = hsw_mixer_to_ipc(ucontrol->value.integer.value[1]);
		sst_hsw_stream_set_volume(hsw, pcm_data->stream, 0, 1, volume);
	}

	pm_runtime_mark_last_busy(pdata->dev);
	pm_runtime_put_autosuspend(pdata->dev);
	mutex_unlock(&pcm_data->mutex);
	return 0;
}

static int hsw_stream_volume_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct hsw_priv_data *pdata =
		snd_soc_component_get_drvdata(component);
	struct hsw_pcm_data *pcm_data;
	struct sst_hsw *hsw = pdata->hsw;
	u32 volume;
	int dai, stream;

	dai = mod_map[mc->reg].dai_id;
	stream = mod_map[mc->reg].stream;
	pcm_data = &pdata->pcm[dai][stream];

	mutex_lock(&pcm_data->mutex);
	pm_runtime_get_sync(pdata->dev);

	if (!pcm_data->stream) {
		ucontrol->value.integer.value[0] =
			hsw_ipc_to_mixer(pcm_data->volume[0]);
		ucontrol->value.integer.value[1] =
			hsw_ipc_to_mixer(pcm_data->volume[1]);
		pm_runtime_mark_last_busy(pdata->dev);
		pm_runtime_put_autosuspend(pdata->dev);
		mutex_unlock(&pcm_data->mutex);
		return 0;
	}

	sst_hsw_stream_get_volume(hsw, pcm_data->stream, 0, 0, &volume);
	ucontrol->value.integer.value[0] = hsw_ipc_to_mixer(volume);
	sst_hsw_stream_get_volume(hsw, pcm_data->stream, 0, 1, &volume);
	ucontrol->value.integer.value[1] = hsw_ipc_to_mixer(volume);

	pm_runtime_mark_last_busy(pdata->dev);
	pm_runtime_put_autosuspend(pdata->dev);
	mutex_unlock(&pcm_data->mutex);

	return 0;
}

static int hsw_volume_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct hsw_priv_data *pdata = snd_soc_component_get_drvdata(component);
	struct sst_hsw *hsw = pdata->hsw;
	u32 volume;

	pm_runtime_get_sync(pdata->dev);

	if (ucontrol->value.integer.value[0] ==
		ucontrol->value.integer.value[1]) {

		volume = hsw_mixer_to_ipc(ucontrol->value.integer.value[0]);
		sst_hsw_mixer_set_volume(hsw, 0, SST_HSW_CHANNELS_ALL, volume);

	} else {
		volume = hsw_mixer_to_ipc(ucontrol->value.integer.value[0]);
		sst_hsw_mixer_set_volume(hsw, 0, 0, volume);

		volume = hsw_mixer_to_ipc(ucontrol->value.integer.value[1]);
		sst_hsw_mixer_set_volume(hsw, 0, 1, volume);
	}

	pm_runtime_mark_last_busy(pdata->dev);
	pm_runtime_put_autosuspend(pdata->dev);
	return 0;
}

static int hsw_volume_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct hsw_priv_data *pdata = snd_soc_component_get_drvdata(component);
	struct sst_hsw *hsw = pdata->hsw;
	unsigned int volume = 0;

	pm_runtime_get_sync(pdata->dev);
	sst_hsw_mixer_get_volume(hsw, 0, 0, &volume);
	ucontrol->value.integer.value[0] = hsw_ipc_to_mixer(volume);

	sst_hsw_mixer_get_volume(hsw, 0, 1, &volume);
	ucontrol->value.integer.value[1] = hsw_ipc_to_mixer(volume);

	pm_runtime_mark_last_busy(pdata->dev);
	pm_runtime_put_autosuspend(pdata->dev);
	return 0;
}

static int hsw_waves_switch_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct hsw_priv_data *pdata = snd_soc_component_get_drvdata(component);
	struct sst_hsw *hsw = pdata->hsw;
	enum sst_hsw_module_id id = SST_HSW_MODULE_WAVES;

	ucontrol->value.integer.value[0] =
		(sst_hsw_is_module_active(hsw, id) ||
		sst_hsw_is_module_enabled_rtd3(hsw, id));
	return 0;
}

static int hsw_waves_switch_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct hsw_priv_data *pdata = snd_soc_component_get_drvdata(component);
	struct sst_hsw *hsw = pdata->hsw;
	int ret = 0;
	enum sst_hsw_module_id id = SST_HSW_MODULE_WAVES;
	bool switch_on = (bool)ucontrol->value.integer.value[0];

	/* if module is in RAM on the DSP, apply user settings to module through
	 * ipc. If module is not in RAM on the DSP, store user setting for
	 * track */
	if (sst_hsw_is_module_loaded(hsw, id)) {
		if (switch_on == sst_hsw_is_module_active(hsw, id))
			return 0;

		if (switch_on)
			ret = sst_hsw_module_enable(hsw, id, 0);
		else
			ret = sst_hsw_module_disable(hsw, id, 0);
	} else {
		if (switch_on == sst_hsw_is_module_enabled_rtd3(hsw, id))
			return 0;

		if (switch_on)
			sst_hsw_set_module_enabled_rtd3(hsw, id);
		else
			sst_hsw_set_module_disabled_rtd3(hsw, id);
	}

	return ret;
}

static int hsw_waves_param_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct hsw_priv_data *pdata = snd_soc_component_get_drvdata(component);
	struct sst_hsw *hsw = pdata->hsw;

	/* return a matching line from param buffer */
	return sst_hsw_load_param_line(hsw, ucontrol->value.bytes.data);
}

static int hsw_waves_param_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct hsw_priv_data *pdata = snd_soc_component_get_drvdata(component);
	struct sst_hsw *hsw = pdata->hsw;
	int ret;
	enum sst_hsw_module_id id = SST_HSW_MODULE_WAVES;
	int param_id = ucontrol->value.bytes.data[0];
	int param_size = WAVES_PARAM_COUNT;

	/* clear param buffer and reset buffer index */
	if (param_id == 0xFF) {
		sst_hsw_reset_param_buf(hsw);
		return 0;
	}

	/* store params into buffer */
	ret = sst_hsw_store_param_line(hsw, ucontrol->value.bytes.data);
	if (ret < 0)
		return ret;

	if (sst_hsw_is_module_active(hsw, id))
		ret = sst_hsw_module_set_param(hsw, id, 0, param_id,
				param_size, ucontrol->value.bytes.data);
	return ret;
}

/* TLV used by both global and stream volumes */
static const DECLARE_TLV_DB_SCALE(hsw_vol_tlv, -9000, 300, 1);

/* System Pin has no volume control */
static const struct snd_kcontrol_new hsw_volume_controls[] = {
	/* Global DSP volume */
	SOC_DOUBLE_EXT_TLV("Master Playback Volume", 0, 0, 8,
		ARRAY_SIZE(volume_map) - 1, 0,
		hsw_volume_get, hsw_volume_put, hsw_vol_tlv),
	/* Offload 0 volume */
	SOC_DOUBLE_EXT_TLV("Media0 Playback Volume", 1, 0, 8,
		ARRAY_SIZE(volume_map) - 1, 0,
		hsw_stream_volume_get, hsw_stream_volume_put, hsw_vol_tlv),
	/* Offload 1 volume */
	SOC_DOUBLE_EXT_TLV("Media1 Playback Volume", 2, 0, 8,
		ARRAY_SIZE(volume_map) - 1, 0,
		hsw_stream_volume_get, hsw_stream_volume_put, hsw_vol_tlv),
	/* Mic Capture volume */
	SOC_DOUBLE_EXT_TLV("Mic Capture Volume", 4, 0, 8,
		ARRAY_SIZE(volume_map) - 1, 0,
		hsw_stream_volume_get, hsw_stream_volume_put, hsw_vol_tlv),
	/* enable/disable module waves */
	SOC_SINGLE_BOOL_EXT("Waves Switch", 0,
		hsw_waves_switch_get, hsw_waves_switch_put),
	/* set parameters to module waves */
	SND_SOC_BYTES_EXT("Waves Set Param", WAVES_PARAM_COUNT,
		hsw_waves_param_get, hsw_waves_param_put),
};

/* Create DMA buffer page table for DSP */
static int create_adsp_page_table(struct snd_pcm_substream *substream,
	struct hsw_priv_data *pdata, struct snd_soc_pcm_runtime *rtd,
	unsigned char *dma_area, size_t size, int pcm)
{
	struct snd_dma_buffer *dmab = snd_pcm_get_dma_buf(substream);
	int i, pages, stream = substream->stream;

	pages = snd_sgbuf_aligned_pages(size);

	dev_dbg(rtd->dev, "generating page table for %p size 0x%zx pages %d\n",
		dma_area, size, pages);

	for (i = 0; i < pages; i++) {
		u32 idx = (((i << 2) + i)) >> 1;
		u32 pfn = snd_sgbuf_get_addr(dmab, i * PAGE_SIZE) >> PAGE_SHIFT;
		u32 *pg_table;

		dev_dbg(rtd->dev, "pfn i %i idx %d pfn %x\n", i, idx, pfn);

		pg_table = (u32 *)(pdata->dmab[pcm][stream].area + idx);

		if (i & 1)
			*pg_table |= (pfn << 4);
		else
			*pg_table |= pfn;
	}

	return 0;
}

/* this may get called several times by oss emulation */
static int hsw_pcm_hw_params(struct snd_soc_component *component,
			     struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct hsw_priv_data *pdata = snd_soc_component_get_drvdata(component);
	struct hsw_pcm_data *pcm_data;
	struct sst_hsw *hsw = pdata->hsw;
	struct sst_module *module_data;
	struct sst_dsp *dsp;
	struct snd_dma_buffer *dmab;
	enum sst_hsw_stream_type stream_type;
	enum sst_hsw_stream_path_id path_id;
	u32 rate, bits, map, pages, module_id;
	u8 channels;
	int ret, dai;

	dai = mod_map[rtd->cpu_dai->id].dai_id;
	pcm_data = &pdata->pcm[dai][substream->stream];

	/* check if we are being called a subsequent time */
	if (pcm_data->allocated) {
		ret = sst_hsw_stream_reset(hsw, pcm_data->stream);
		if (ret < 0)
			dev_dbg(rtd->dev, "error: reset stream failed %d\n",
				ret);

		ret = sst_hsw_stream_free(hsw, pcm_data->stream);
		if (ret < 0) {
			dev_dbg(rtd->dev, "error: free stream failed %d\n",
				ret);
			return ret;
		}
		pcm_data->allocated = false;

		pcm_data->stream = sst_hsw_stream_new(hsw, rtd->cpu_dai->id,
			hsw_notify_pointer, pcm_data);
		if (pcm_data->stream == NULL) {
			dev_err(rtd->dev, "error: failed to create stream\n");
			return -EINVAL;
		}
	}

	/* stream direction */
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		path_id = SST_HSW_STREAM_PATH_SSP0_OUT;
	else
		path_id = SST_HSW_STREAM_PATH_SSP0_IN;

	/* DSP stream type depends on DAI ID */
	switch (rtd->cpu_dai->id) {
	case 0:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			stream_type = SST_HSW_STREAM_TYPE_SYSTEM;
			module_id = SST_HSW_MODULE_PCM_SYSTEM;
		}
		else {
			stream_type = SST_HSW_STREAM_TYPE_CAPTURE;
			module_id = SST_HSW_MODULE_PCM_CAPTURE;
		}
		break;
	case 1:
	case 2:
		stream_type = SST_HSW_STREAM_TYPE_RENDER;
		module_id = SST_HSW_MODULE_PCM;
		break;
	case 3:
		/* path ID needs to be OUT for loopback */
		stream_type = SST_HSW_STREAM_TYPE_LOOPBACK;
		path_id = SST_HSW_STREAM_PATH_SSP0_OUT;
		module_id = SST_HSW_MODULE_PCM_REFERENCE;
		break;
	default:
		dev_err(rtd->dev, "error: invalid DAI ID %d\n",
			rtd->cpu_dai->id);
		return -EINVAL;
	}

	ret = sst_hsw_stream_format(hsw, pcm_data->stream,
		path_id, stream_type, SST_HSW_STREAM_FORMAT_PCM_FORMAT);
	if (ret < 0) {
		dev_err(rtd->dev, "error: failed to set format %d\n", ret);
		return ret;
	}

	rate = params_rate(params);
	ret = sst_hsw_stream_set_rate(hsw, pcm_data->stream, rate);
	if (ret < 0) {
		dev_err(rtd->dev, "error: could not set rate %d\n", rate);
		return ret;
	}

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		bits = SST_HSW_DEPTH_16BIT;
		sst_hsw_stream_set_valid(hsw, pcm_data->stream, 16);
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		bits = SST_HSW_DEPTH_32BIT;
		sst_hsw_stream_set_valid(hsw, pcm_data->stream, 24);
		break;
	case SNDRV_PCM_FORMAT_S8:
		bits = SST_HSW_DEPTH_8BIT;
		sst_hsw_stream_set_valid(hsw, pcm_data->stream, 8);
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		bits = SST_HSW_DEPTH_32BIT;
		sst_hsw_stream_set_valid(hsw, pcm_data->stream, 32);
		break;
	default:
		dev_err(rtd->dev, "error: invalid format %d\n",
			params_format(params));
		return -EINVAL;
	}

	ret = sst_hsw_stream_set_bits(hsw, pcm_data->stream, bits);
	if (ret < 0) {
		dev_err(rtd->dev, "error: could not set bits %d\n", bits);
		return ret;
	}

	channels = params_channels(params);
	map = create_channel_map(SST_HSW_CHANNEL_CONFIG_STEREO);
	sst_hsw_stream_set_map_config(hsw, pcm_data->stream,
			map, SST_HSW_CHANNEL_CONFIG_STEREO);

	ret = sst_hsw_stream_set_channels(hsw, pcm_data->stream, channels);
	if (ret < 0) {
		dev_err(rtd->dev, "error: could not set channels %d\n",
			channels);
		return ret;
	}

	dmab = snd_pcm_get_dma_buf(substream);

	ret = create_adsp_page_table(substream, pdata, rtd, runtime->dma_area,
		runtime->dma_bytes, rtd->cpu_dai->id);
	if (ret < 0)
		return ret;

	sst_hsw_stream_set_style(hsw, pcm_data->stream,
		SST_HSW_INTERLEAVING_PER_CHANNEL);

	if (runtime->dma_bytes % PAGE_SIZE)
		pages = (runtime->dma_bytes / PAGE_SIZE) + 1;
	else
		pages = runtime->dma_bytes / PAGE_SIZE;

	ret = sst_hsw_stream_buffer(hsw, pcm_data->stream,
		pdata->dmab[rtd->cpu_dai->id][substream->stream].addr,
		pages, runtime->dma_bytes, 0,
		snd_sgbuf_get_addr(dmab, 0) >> PAGE_SHIFT);
	if (ret < 0) {
		dev_err(rtd->dev, "error: failed to set DMA buffer %d\n", ret);
		return ret;
	}

	dsp = sst_hsw_get_dsp(hsw);

	module_data = sst_module_get_from_id(dsp, module_id);
	if (module_data == NULL) {
		dev_err(rtd->dev, "error: failed to get module config\n");
		return -EINVAL;
	}

	sst_hsw_stream_set_module_info(hsw, pcm_data->stream,
		pcm_data->runtime);

	ret = sst_hsw_stream_commit(hsw, pcm_data->stream);
	if (ret < 0) {
		dev_err(rtd->dev, "error: failed to commit stream %d\n", ret);
		return ret;
	}

	if (!pcm_data->allocated) {
		/* Set previous saved volume */
		sst_hsw_stream_set_volume(hsw, pcm_data->stream, 0,
				0, pcm_data->volume[0]);
		sst_hsw_stream_set_volume(hsw, pcm_data->stream, 0,
				1, pcm_data->volume[1]);
		pcm_data->allocated = true;
	}

	ret = sst_hsw_stream_pause(hsw, pcm_data->stream, 1);
	if (ret < 0)
		dev_err(rtd->dev, "error: failed to pause %d\n", ret);

	return 0;
}

static int hsw_pcm_trigger(struct snd_soc_component *component,
			   struct snd_pcm_substream *substream, int cmd)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct hsw_priv_data *pdata = snd_soc_component_get_drvdata(component);
	struct hsw_pcm_data *pcm_data;
	struct sst_hsw_stream *sst_stream;
	struct sst_hsw *hsw = pdata->hsw;
	struct snd_pcm_runtime *runtime = substream->runtime;
	snd_pcm_uframes_t pos;
	int dai;

	dai = mod_map[rtd->cpu_dai->id].dai_id;
	pcm_data = &pdata->pcm[dai][substream->stream];
	sst_stream = pcm_data->stream;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		sst_hsw_stream_set_silence_start(hsw, sst_stream, false);
		sst_hsw_stream_resume(hsw, pcm_data->stream, 0);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		sst_hsw_stream_set_silence_start(hsw, sst_stream, false);
		sst_hsw_stream_pause(hsw, pcm_data->stream, 0);
		break;
	case SNDRV_PCM_TRIGGER_DRAIN:
		pos = runtime->control->appl_ptr % runtime->buffer_size;
		sst_hsw_stream_set_old_position(hsw, pcm_data->stream, pos);
		sst_hsw_stream_set_silence_start(hsw, sst_stream, true);
		break;
	default:
		break;
	}

	return 0;
}

static u32 hsw_notify_pointer(struct sst_hsw_stream *stream, void *data)
{
	struct hsw_pcm_data *pcm_data = data;
	struct snd_pcm_substream *substream = pcm_data->substream;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_component *component = snd_soc_rtdcom_lookup(rtd, DRV_NAME);
	struct hsw_priv_data *pdata = snd_soc_component_get_drvdata(component);
	struct sst_hsw *hsw = pdata->hsw;
	u32 pos;
	snd_pcm_uframes_t position = bytes_to_frames(runtime,
		 sst_hsw_get_dsp_position(hsw, pcm_data->stream));
	unsigned char *dma_area = runtime->dma_area;
	snd_pcm_uframes_t dma_frames =
		bytes_to_frames(runtime, runtime->dma_bytes);
	snd_pcm_uframes_t old_position;
	ssize_t samples;

	pos = frames_to_bytes(runtime,
		(runtime->control->appl_ptr % runtime->buffer_size));

	dev_vdbg(rtd->dev, "PCM: App pointer %d bytes\n", pos);

	/* SST fw don't know where to stop dma
	 * So, SST driver need to clean the data which has been consumed
	 */
	if (dma_area == NULL || dma_frames <= 0
		|| (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		|| !sst_hsw_stream_get_silence_start(hsw, stream)) {
		snd_pcm_period_elapsed(substream);
		return pos;
	}

	old_position = sst_hsw_stream_get_old_position(hsw, stream);
	if (position > old_position) {
		if (position < dma_frames) {
			samples = SST_SAMPLES(runtime, position - old_position);
			snd_pcm_format_set_silence(runtime->format,
				SST_OLD_POSITION(dma_area,
					runtime, old_position),
				samples);
		} else
			dev_err(rtd->dev, "PCM: position is wrong\n");
	} else {
		if (old_position < dma_frames) {
			samples = SST_SAMPLES(runtime,
				dma_frames - old_position);
			snd_pcm_format_set_silence(runtime->format,
				SST_OLD_POSITION(dma_area,
					runtime, old_position),
				samples);
		} else
			dev_err(rtd->dev, "PCM: dma_bytes is wrong\n");
		if (position < dma_frames) {
			samples = SST_SAMPLES(runtime, position);
			snd_pcm_format_set_silence(runtime->format,
				dma_area, samples);
		} else
			dev_err(rtd->dev, "PCM: position is wrong\n");
	}
	sst_hsw_stream_set_old_position(hsw, stream, position);

	/* let alsa know we have play a period */
	snd_pcm_period_elapsed(substream);
	return pos;
}

static snd_pcm_uframes_t hsw_pcm_pointer(struct snd_soc_component *component,
					 struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct hsw_priv_data *pdata = snd_soc_component_get_drvdata(component);
	struct hsw_pcm_data *pcm_data;
	struct sst_hsw *hsw = pdata->hsw;
	snd_pcm_uframes_t offset;
	uint64_t ppos;
	u32 position;
	int dai;

	dai = mod_map[rtd->cpu_dai->id].dai_id;
	pcm_data = &pdata->pcm[dai][substream->stream];
	position = sst_hsw_get_dsp_position(hsw, pcm_data->stream);

	offset = bytes_to_frames(runtime, position);
	ppos = sst_hsw_get_dsp_presentation_position(hsw, pcm_data->stream);

	dev_vdbg(rtd->dev, "PCM: DMA pointer %du bytes, pos %llu\n",
		position, ppos);
	return offset;
}

static int hsw_pcm_open(struct snd_soc_component *component,
			struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct hsw_priv_data *pdata = snd_soc_component_get_drvdata(component);
	struct hsw_pcm_data *pcm_data;
	struct sst_hsw *hsw = pdata->hsw;
	int dai;

	dai = mod_map[rtd->cpu_dai->id].dai_id;
	pcm_data = &pdata->pcm[dai][substream->stream];

	mutex_lock(&pcm_data->mutex);
	pm_runtime_get_sync(pdata->dev);

	pcm_data->substream = substream;

	snd_soc_set_runtime_hwparams(substream, &hsw_pcm_hardware);

	pcm_data->stream = sst_hsw_stream_new(hsw, rtd->cpu_dai->id,
		hsw_notify_pointer, pcm_data);
	if (pcm_data->stream == NULL) {
		dev_err(rtd->dev, "error: failed to create stream\n");
		pm_runtime_mark_last_busy(pdata->dev);
		pm_runtime_put_autosuspend(pdata->dev);
		mutex_unlock(&pcm_data->mutex);
		return -EINVAL;
	}

	mutex_unlock(&pcm_data->mutex);
	return 0;
}

static int hsw_pcm_close(struct snd_soc_component *component,
			 struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct hsw_priv_data *pdata = snd_soc_component_get_drvdata(component);
	struct hsw_pcm_data *pcm_data;
	struct sst_hsw *hsw = pdata->hsw;
	int ret, dai;

	dai = mod_map[rtd->cpu_dai->id].dai_id;
	pcm_data = &pdata->pcm[dai][substream->stream];

	mutex_lock(&pcm_data->mutex);
	ret = sst_hsw_stream_reset(hsw, pcm_data->stream);
	if (ret < 0) {
		dev_dbg(rtd->dev, "error: reset stream failed %d\n", ret);
		goto out;
	}

	ret = sst_hsw_stream_free(hsw, pcm_data->stream);
	if (ret < 0) {
		dev_dbg(rtd->dev, "error: free stream failed %d\n", ret);
		goto out;
	}
	pcm_data->allocated = false;
	pcm_data->stream = NULL;

out:
	pm_runtime_mark_last_busy(pdata->dev);
	pm_runtime_put_autosuspend(pdata->dev);
	mutex_unlock(&pcm_data->mutex);
	return ret;
}

static int hsw_pcm_create_modules(struct hsw_priv_data *pdata)
{
	struct sst_hsw *hsw = pdata->hsw;
	struct hsw_pcm_data *pcm_data;
	int i;

	for (i = 0; i < ARRAY_SIZE(mod_map); i++) {
		pcm_data = &pdata->pcm[mod_map[i].dai_id][mod_map[i].stream];

		/* create new runtime module, use same offset if recreated */
		pcm_data->runtime = sst_hsw_runtime_module_create(hsw,
			mod_map[i].mod_id, pcm_data->persistent_offset);
		if (pcm_data->runtime == NULL)
			goto err;
		pcm_data->persistent_offset =
			pcm_data->runtime->persistent_offset;
	}

	/* create runtime blocks for module waves */
	if (sst_hsw_is_module_loaded(hsw, SST_HSW_MODULE_WAVES)) {
		pdata->runtime_waves = sst_hsw_runtime_module_create(hsw,
			SST_HSW_MODULE_WAVES, 0);
		if (pdata->runtime_waves == NULL)
			goto err;
	}

	return 0;

err:
	for (--i; i >= 0; i--) {
		pcm_data = &pdata->pcm[mod_map[i].dai_id][mod_map[i].stream];
		sst_hsw_runtime_module_free(pcm_data->runtime);
	}

	return -ENODEV;
}

static void hsw_pcm_free_modules(struct hsw_priv_data *pdata)
{
	struct sst_hsw *hsw = pdata->hsw;
	struct hsw_pcm_data *pcm_data;
	int i;

	for (i = 0; i < ARRAY_SIZE(mod_map); i++) {
		pcm_data = &pdata->pcm[mod_map[i].dai_id][mod_map[i].stream];
		if (pcm_data->runtime){
			sst_hsw_runtime_module_free(pcm_data->runtime);
			pcm_data->runtime = NULL;
		}
	}
	if (sst_hsw_is_module_loaded(hsw, SST_HSW_MODULE_WAVES) &&
				pdata->runtime_waves) {
		sst_hsw_runtime_module_free(pdata->runtime_waves);
		pdata->runtime_waves = NULL;
	}
}

static int hsw_pcm_new(struct snd_soc_component *component,
		       struct snd_soc_pcm_runtime *rtd)
{
	struct snd_pcm *pcm = rtd->pcm;
	struct sst_pdata *pdata = dev_get_platdata(component->dev);
	struct hsw_priv_data *priv_data = dev_get_drvdata(component->dev);
	struct device *dev = pdata->dma_dev;

	if (pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream ||
			pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream) {
		snd_pcm_set_managed_buffer_all(pcm,
			SNDRV_DMA_TYPE_DEV_SG,
			dev,
			hsw_pcm_hardware.buffer_bytes_max,
			hsw_pcm_hardware.buffer_bytes_max);
	}
	if (pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream)
		priv_data->pcm[rtd->cpu_dai->id][SNDRV_PCM_STREAM_PLAYBACK].hsw_pcm = pcm;
	if (pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream)
		priv_data->pcm[rtd->cpu_dai->id][SNDRV_PCM_STREAM_CAPTURE].hsw_pcm = pcm;

	return 0;
}

#define HSW_FORMATS \
	(SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S16_LE)

static struct snd_soc_dai_driver hsw_dais[] = {
	{
		.name  = "System Pin",
		.id = HSW_PCM_DAI_ID_SYSTEM,
		.playback = {
			.stream_name = "System Playback",
			.channels_min = 2,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_48000,
			.formats = SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S16_LE,
		},
		.capture = {
			.stream_name = "Analog Capture",
			.channels_min = 2,
			.channels_max = 4,
			.rates = SNDRV_PCM_RATE_48000,
			.formats = SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S16_LE,
		},
	},
	{
		/* PCM */
		.name  = "Offload0 Pin",
		.id = HSW_PCM_DAI_ID_OFFLOAD0,
		.playback = {
			.stream_name = "Offload0 Playback",
			.channels_min = 2,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = HSW_FORMATS,
		},
	},
	{
		/* PCM */
		.name  = "Offload1 Pin",
		.id = HSW_PCM_DAI_ID_OFFLOAD1,
		.playback = {
			.stream_name = "Offload1 Playback",
			.channels_min = 2,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = HSW_FORMATS,
		},
	},
	{
		.name  = "Loopback Pin",
		.id = HSW_PCM_DAI_ID_LOOPBACK,
		.capture = {
			.stream_name = "Loopback Capture",
			.channels_min = 2,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_48000,
			.formats = SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S16_LE,
		},
	},
};

static const struct snd_soc_dapm_widget widgets[] = {

	/* Backend DAIs  */
	SND_SOC_DAPM_AIF_IN("SSP0 CODEC IN", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("SSP0 CODEC OUT", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("SSP1 BT IN", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("SSP1 BT OUT", NULL, 0, SND_SOC_NOPM, 0, 0),

	/* Global Playback Mixer */
	SND_SOC_DAPM_MIXER("Playback VMixer", SND_SOC_NOPM, 0, 0, NULL, 0),
};

static const struct snd_soc_dapm_route graph[] = {

	/* Playback Mixer */
	{"Playback VMixer", NULL, "System Playback"},
	{"Playback VMixer", NULL, "Offload0 Playback"},
	{"Playback VMixer", NULL, "Offload1 Playback"},

	{"SSP0 CODEC OUT", NULL, "Playback VMixer"},

	{"Analog Capture", NULL, "SSP0 CODEC IN"},
};

static int hsw_pcm_probe(struct snd_soc_component *component)
{
	struct hsw_priv_data *priv_data = snd_soc_component_get_drvdata(component);
	struct sst_pdata *pdata = dev_get_platdata(component->dev);
	struct device *dma_dev, *dev;
	int i, ret = 0;

	if (!pdata)
		return -ENODEV;

	dev = component->dev;
	dma_dev = pdata->dma_dev;

	priv_data->hsw = pdata->dsp;
	priv_data->dev = dev;
	priv_data->pm_state = HSW_PM_STATE_D0;
	priv_data->soc_card = component->card;

	/* allocate DSP buffer page tables */
	for (i = 0; i < ARRAY_SIZE(hsw_dais); i++) {

		/* playback */
		if (hsw_dais[i].playback.channels_min) {
			mutex_init(&priv_data->pcm[i][SNDRV_PCM_STREAM_PLAYBACK].mutex);
			ret = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, dma_dev,
				PAGE_SIZE, &priv_data->dmab[i][0]);
			if (ret < 0)
				goto err;
		}

		/* capture */
		if (hsw_dais[i].capture.channels_min) {
			mutex_init(&priv_data->pcm[i][SNDRV_PCM_STREAM_CAPTURE].mutex);
			ret = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, dma_dev,
				PAGE_SIZE, &priv_data->dmab[i][1]);
			if (ret < 0)
				goto err;
		}
	}

	/* allocate runtime modules */
	ret = hsw_pcm_create_modules(priv_data);
	if (ret < 0)
		goto err;

	/* enable runtime PM with auto suspend */
	pm_runtime_set_autosuspend_delay(dev, SST_RUNTIME_SUSPEND_DELAY);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

	return 0;

err:
	for (--i; i >= 0; i--) {
		if (hsw_dais[i].playback.channels_min)
			snd_dma_free_pages(&priv_data->dmab[i][0]);
		if (hsw_dais[i].capture.channels_min)
			snd_dma_free_pages(&priv_data->dmab[i][1]);
	}
	return ret;
}

static void hsw_pcm_remove(struct snd_soc_component *component)
{
	struct hsw_priv_data *priv_data =
		snd_soc_component_get_drvdata(component);
	int i;

	pm_runtime_disable(component->dev);
	hsw_pcm_free_modules(priv_data);

	for (i = 0; i < ARRAY_SIZE(hsw_dais); i++) {
		if (hsw_dais[i].playback.channels_min)
			snd_dma_free_pages(&priv_data->dmab[i][0]);
		if (hsw_dais[i].capture.channels_min)
			snd_dma_free_pages(&priv_data->dmab[i][1]);
	}
}

static const struct snd_soc_component_driver hsw_dai_component = {
	.name		= DRV_NAME,
	.probe		= hsw_pcm_probe,
	.remove		= hsw_pcm_remove,
	.open		= hsw_pcm_open,
	.close		= hsw_pcm_close,
	.hw_params	= hsw_pcm_hw_params,
	.trigger	= hsw_pcm_trigger,
	.pointer	= hsw_pcm_pointer,
	.pcm_construct	= hsw_pcm_new,
	.controls	= hsw_volume_controls,
	.num_controls	= ARRAY_SIZE(hsw_volume_controls),
	.dapm_widgets	= widgets,
	.num_dapm_widgets = ARRAY_SIZE(widgets),
	.dapm_routes	= graph,
	.num_dapm_routes = ARRAY_SIZE(graph),
};

static int hsw_pcm_dev_probe(struct platform_device *pdev)
{
	struct sst_pdata *sst_pdata = dev_get_platdata(&pdev->dev);
	struct hsw_priv_data *priv_data;
	int ret;

	if (!sst_pdata)
		return -EINVAL;

	priv_data = devm_kzalloc(&pdev->dev, sizeof(*priv_data), GFP_KERNEL);
	if (!priv_data)
		return -ENOMEM;

	ret = sst_hsw_dsp_init(&pdev->dev, sst_pdata);
	if (ret < 0)
		return -ENODEV;

	priv_data->hsw = sst_pdata->dsp;
	platform_set_drvdata(pdev, priv_data);

	ret = devm_snd_soc_register_component(&pdev->dev, &hsw_dai_component,
		hsw_dais, ARRAY_SIZE(hsw_dais));
	if (ret < 0)
		goto err_plat;

	return 0;

err_plat:
	sst_hsw_dsp_free(&pdev->dev, sst_pdata);
	return 0;
}

static int hsw_pcm_dev_remove(struct platform_device *pdev)
{
	struct sst_pdata *sst_pdata = dev_get_platdata(&pdev->dev);

	sst_hsw_dsp_free(&pdev->dev, sst_pdata);

	return 0;
}

#ifdef CONFIG_PM

static int hsw_pcm_runtime_idle(struct device *dev)
{
	return 0;
}

static int hsw_pcm_suspend(struct device *dev)
{
	struct hsw_priv_data *pdata = dev_get_drvdata(dev);
	struct sst_hsw *hsw = pdata->hsw;

	/* enter D3 state and stall */
	sst_hsw_dsp_runtime_suspend(hsw);
	/* free all runtime modules */
	hsw_pcm_free_modules(pdata);
	/* put the DSP to sleep, fw unloaded after runtime modules freed */
	sst_hsw_dsp_runtime_sleep(hsw);
	return 0;
}

static int hsw_pcm_runtime_suspend(struct device *dev)
{
	struct hsw_priv_data *pdata = dev_get_drvdata(dev);
	struct sst_hsw *hsw = pdata->hsw;
	int ret;

	if (pdata->pm_state >= HSW_PM_STATE_RTD3)
		return 0;

	/* fw modules will be unloaded on RTD3, set flag to track */
	if (sst_hsw_is_module_active(hsw, SST_HSW_MODULE_WAVES)) {
		ret = sst_hsw_module_disable(hsw, SST_HSW_MODULE_WAVES, 0);
		if (ret < 0)
			return ret;
		sst_hsw_set_module_enabled_rtd3(hsw, SST_HSW_MODULE_WAVES);
	}
	hsw_pcm_suspend(dev);
	pdata->pm_state = HSW_PM_STATE_RTD3;

	return 0;
}

static int hsw_pcm_runtime_resume(struct device *dev)
{
	struct hsw_priv_data *pdata = dev_get_drvdata(dev);
	struct sst_hsw *hsw = pdata->hsw;
	int ret;

	if (pdata->pm_state != HSW_PM_STATE_RTD3)
		return 0;

	ret = sst_hsw_dsp_load(hsw);
	if (ret < 0) {
		dev_err(dev, "failed to reload %d\n", ret);
		return ret;
	}

	ret = hsw_pcm_create_modules(pdata);
	if (ret < 0) {
		dev_err(dev, "failed to create modules %d\n", ret);
		return ret;
	}

	ret = sst_hsw_dsp_runtime_resume(hsw);
	if (ret < 0)
		return ret;
	else if (ret == 1) /* no action required */
		return 0;

	/* check flag when resume */
	if (sst_hsw_is_module_enabled_rtd3(hsw, SST_HSW_MODULE_WAVES)) {
		ret = sst_hsw_module_enable(hsw, SST_HSW_MODULE_WAVES, 0);
		if (ret < 0)
			return ret;
		/* put parameters from buffer to dsp */
		ret = sst_hsw_launch_param_buf(hsw);
		if (ret < 0)
			return ret;
		/* unset flag */
		sst_hsw_set_module_disabled_rtd3(hsw, SST_HSW_MODULE_WAVES);
	}

	pdata->pm_state = HSW_PM_STATE_D0;
	return ret;
}

#else
#define hsw_pcm_runtime_idle		NULL
#define hsw_pcm_runtime_suspend		NULL
#define hsw_pcm_runtime_resume		NULL
#endif

#ifdef CONFIG_PM

static void hsw_pcm_complete(struct device *dev)
{
	struct hsw_priv_data *pdata = dev_get_drvdata(dev);
	struct sst_hsw *hsw = pdata->hsw;
	struct hsw_pcm_data *pcm_data;
	int i, err;

	if (pdata->pm_state != HSW_PM_STATE_D3)
		return;

	err = sst_hsw_dsp_load(hsw);
	if (err < 0) {
		dev_err(dev, "failed to reload %d\n", err);
		return;
	}

	err = hsw_pcm_create_modules(pdata);
	if (err < 0) {
		dev_err(dev, "failed to create modules %d\n", err);
		return;
	}

	for (i = 0; i < ARRAY_SIZE(mod_map); i++) {
		pcm_data = &pdata->pcm[mod_map[i].dai_id][mod_map[i].stream];

		if (!pcm_data->substream)
			continue;

		err = sst_module_runtime_restore(pcm_data->runtime,
			&pcm_data->context);
		if (err < 0)
			dev_err(dev, "failed to restore context for PCM %d\n", i);
	}

	snd_soc_resume(pdata->soc_card->dev);

	err = sst_hsw_dsp_runtime_resume(hsw);
	if (err < 0)
		return;
	else if (err == 1) /* no action required */
		return;

	pdata->pm_state = HSW_PM_STATE_D0;
	return;
}

static int hsw_pcm_prepare(struct device *dev)
{
	struct hsw_priv_data *pdata = dev_get_drvdata(dev);
	struct hsw_pcm_data *pcm_data;
	int i, err;

	if (pdata->pm_state == HSW_PM_STATE_D3)
		return 0;
	else if (pdata->pm_state == HSW_PM_STATE_D0) {
		/* suspend all active streams */
		for (i = 0; i < ARRAY_SIZE(mod_map); i++) {
			pcm_data = &pdata->pcm[mod_map[i].dai_id][mod_map[i].stream];

			if (!pcm_data->substream)
				continue;
			dev_dbg(dev, "suspending pcm %d\n", i);
			snd_pcm_suspend_all(pcm_data->hsw_pcm);

			/* We need to wait until the DSP FW stops the streams */
			msleep(2);
		}

		/* preserve persistent memory */
		for (i = 0; i < ARRAY_SIZE(mod_map); i++) {
			pcm_data = &pdata->pcm[mod_map[i].dai_id][mod_map[i].stream];

			if (!pcm_data->substream)
				continue;

			dev_dbg(dev, "saving context pcm %d\n", i);
			err = sst_module_runtime_save(pcm_data->runtime,
				&pcm_data->context);
			if (err < 0)
				dev_err(dev, "failed to save context for PCM %d\n", i);
		}
		hsw_pcm_suspend(dev);
	}

	snd_soc_suspend(pdata->soc_card->dev);
	snd_soc_poweroff(pdata->soc_card->dev);

	pdata->pm_state = HSW_PM_STATE_D3;

	return 0;
}

#else
#define hsw_pcm_prepare		NULL
#define hsw_pcm_complete	NULL
#endif

static const struct dev_pm_ops hsw_pcm_pm = {
	.runtime_idle = hsw_pcm_runtime_idle,
	.runtime_suspend = hsw_pcm_runtime_suspend,
	.runtime_resume = hsw_pcm_runtime_resume,
	.prepare = hsw_pcm_prepare,
	.complete = hsw_pcm_complete,
};

static struct platform_driver hsw_pcm_driver = {
	.driver = {
		.name = "haswell-pcm-audio",
		.pm = &hsw_pcm_pm,
	},

	.probe = hsw_pcm_dev_probe,
	.remove = hsw_pcm_dev_remove,
};
module_platform_driver(hsw_pcm_driver);

MODULE_AUTHOR("Liam Girdwood, Xingchao Wang");
MODULE_DESCRIPTION("Haswell/Lynxpoint + Broadwell/Wildcatpoint PCM");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:haswell-pcm-audio");
