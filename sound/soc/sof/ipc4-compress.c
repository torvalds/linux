// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// Copyright 2026 Intel Corporation. All rights reserved.
//
#include <sound/soc.h>
#include <sound/sof.h>
#include <sound/compress_driver.h>
#include <sound/pcm_params.h>
#include "sof-audio.h"
#include "sof-priv.h"
#include "sof-utils.h"
#include "ops.h"
#include "ipc4-priv.h"
#include "ipc4-topology.h"
#include "ipc4-fw-reg.h"

/* Maximum processing size of the decoder/encoder is 2048 bytes */
#define SOF_IPC4_COMPR_MAX_PROCESSING_SIZE		(SZ_2K)

#define SOF_IPC4_COMPR_MIN_FRAGMENTS			3
#define SOF_IPC4_COMPR_MAX_FRAGMENT_SIZE		(SZ_128K)
#define SOF_IPC4_COMPR_MAX_FRAGMENTS			64
#define SOF_IPC4_COMPR_MIN_BUFFER_SIZE(min_size)	((min_size) * \
							 SOF_IPC4_COMPR_MIN_FRAGMENTS)

struct sof_ipc4_compr_init_data {
	struct snd_codec codec;
	u32 dir;
} __packed __aligned(4);

static struct sof_ipc4_process *
sof_ipc4_compr_get_module(struct snd_sof_pcm *spcm, int dir)
{
	int id = dir ? snd_soc_dapm_encoder : snd_soc_dapm_decoder;
	struct snd_sof_pcm_stream *sps = &spcm->stream[dir];
	struct snd_soc_dapm_widget *widget;
	int i;

	/* Find the (first) compr module in path */
	for_each_dapm_widgets(sps->list, i, widget) {
		struct snd_sof_widget *swidget = widget->dobj.private;

		if (!swidget)
			continue;

		if (swidget->widget->id == id)
			return swidget->private;
	}

	return NULL;
}

static u32 sof_ipc4_compr_calc_min_fragment_size(struct snd_sof_pcm_stream *sps)
{
	u32 host_buffer_estimate;

	/* Estimated host DMA buffer size based on stereo S32_LE, 48KHz */
	host_buffer_estimate = snd_pcm_format_size(SNDRV_PCM_FORMAT_S32_LE, 2 * 48);
	host_buffer_estimate *= sps->dsp_max_burst_size_in_ms;
	/*
	 * The minimum fragment size must not be smaller than the processing size
	 * or in case of deep buffer on host side, the host DMA buffer size.
	 */
	return max(SOF_IPC4_COMPR_MAX_PROCESSING_SIZE, host_buffer_estimate);
}

/* The buffer must be described by the page table allocated for the stream */
static u64 sof_ipc4_compr_calc_max_buffer_size(struct snd_sof_pcm_stream *sps)
{
	return (u64)SOF_PAGE_TABLE_MAX_PFNS(sps->page_table.bytes) * PAGE_SIZE;
}

static int sof_ipc4_compr_open(struct snd_soc_component *component,
			       struct snd_compr_stream *cstream)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(component);
	struct snd_soc_pcm_runtime *rtd = cstream->private_data;
	struct snd_sof_pcm *spcm;
	int dir, ret;

	spcm = snd_sof_find_spcm_dai(component, rtd);
	if (!spcm)
		return -EINVAL;

	dir = cstream->direction;

	if (spcm->stream[dir].cstream)
		return -EBUSY;

	spcm_dbg(spcm, dir, "Entry: open\n");

	ret = snd_sof_compr_platform_open(sdev, cstream);
	if (ret < 0) {
		spcm_err(spcm, dir, "platform compress open failed %d\n", ret);
		return ret;
	}

	spcm->stream[dir].cstream = cstream;
	spcm->stream[dir].posn.host_posn = 0;
	spcm->stream[dir].posn.dai_posn = 0;
	spcm->prepared[dir] = false;
	spcm->pending_stop[dir] = false;
	spcm->compr_started[dir] = false;

	return 0;
}

static int sof_ipc4_compr_stream_free(struct snd_sof_dev *sdev,
				      struct snd_sof_pcm *spcm,
				      struct snd_compr_stream *cstream)
{
	const struct sof_ipc_pcm_ops *pcm_ops = sof_ipc_get_ops(sdev, pcm);
	int dir = cstream->direction;
	int ret = 0;
	int err = 0;

	if (spcm->prepared[dir]) {
		if (spcm->pending_stop[dir])
			pcm_ops->trigger(sdev->component, NULL, spcm,
					 SNDRV_PCM_TRIGGER_STOP, dir);

		snd_sof_compr_platform_trigger(sdev, cstream,
					       SNDRV_PCM_TRIGGER_STOP);

		err = pcm_ops->hw_free(sdev->component, NULL, spcm, dir);
		if (err < 0)
			spcm_err(spcm, dir, "pcm_ops->hw_free failed %d\n", err);
	}

	spcm->prepared[dir] = false;
	spcm->pending_stop[dir] = false;
	spcm->compr_started[dir] = false;
	spcm->stream[dir].cstream = NULL;

	/* reset the DMA */
	ret = snd_sof_compr_platform_hw_free(sdev, cstream);
	if (ret < 0) {
		spcm_err(spcm, dir, "platform hw free failed %d\n", ret);
		if (!err)
			err = ret;
	}

	/* free widget list */
	ret = sof_widget_list_free(sdev, spcm, dir);
	if (ret < 0 && err == 0) {
		spcm_err(spcm, dir, "sof_widget_list_free failed %d\n", ret);
		err = ret;
	}

	return err;
}

static int sof_ipc4_compr_free(struct snd_soc_component *component,
			       struct snd_compr_stream *cstream)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(component);
	struct snd_soc_pcm_runtime *rtd = cstream->private_data;
	struct snd_sof_pcm *spcm;
	int ret, err;

	spcm = snd_sof_find_spcm_dai(component, rtd);
	if (!spcm)
		return -EINVAL;

	spcm_dbg(spcm, cstream->direction, "Entry: free\n");

	ret = sof_ipc4_compr_stream_free(sdev, spcm, cstream);

	/* unprepare and free the list of DAPM widgets */
	sof_widget_list_unprepare(sdev, spcm, cstream->direction);

	cancel_work_sync(&spcm->stream[cstream->direction].period_elapsed_work);

	snd_compr_free_pages(cstream);

	err = snd_sof_compr_platform_close(sdev, cstream);
	if (err < 0) {
		spcm_err(spcm, cstream->direction,
			 "platform compress close failed %d\n", err);
		if (!ret)
			ret = err;
	}

	return ret;
}

#define SOF_IPC4_CODEC_INFO_GET_ID(value)	((value) & 0xff)
#define SOF_IPC4_CODEC_INFO_GET_DIR(value)	(((value) >> 8) & 0xf)

struct sof_ipc4_codec_info_data {
	u32 count;
	u32 items[];
} __packed __aligned(4);

static int sof_ipc4_compr_get_caps(struct snd_soc_component *component,
				   struct snd_compr_stream *cstream,
				   struct snd_compr_caps *caps)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(component);
	struct snd_soc_pcm_runtime *rtd = cstream->private_data;
	struct sof_ipc4_fw_data *ipc4_data = sdev->private;
	struct sof_ipc4_codec_info_data *codec_info = ipc4_data->codec_info;
	int dir = cstream->direction;
	u64 max_buffer_size;
	u64 max_fragments;
	struct snd_sof_pcm *spcm;
	int i;

	spcm = snd_sof_find_spcm_dai(component, rtd);
	if (!spcm)
		return -EINVAL;

	/* No compress support available in booted firmware */
	if (!codec_info || !codec_info->count) {
		spcm_err(spcm, dir,
			 "Compress is not supported (no codecs available)\n");
		return -EINVAL;
	}

	for (i = 0; i < codec_info->count; i++) {
		int _dir = SOF_IPC4_CODEC_INFO_GET_DIR(codec_info->items[i]);

		if (_dir == dir) {
			int id = SOF_IPC4_CODEC_INFO_GET_ID(codec_info->items[i]);

			if (caps->num_codecs < ARRAY_SIZE(caps->codecs)) {
				spcm_dbg(spcm, dir, "codec#%d: %d\n",
					 caps->num_codecs, id);
				caps->codecs[caps->num_codecs++] = id;
			} else {
				spcm_dbg(spcm, dir, "codec#%d: %d ignored\n",
					 caps->num_codecs, id);
			}
		}
	}

	caps->direction = dir;
	caps->min_fragment_size =
		sof_ipc4_compr_calc_min_fragment_size(&spcm->stream[dir]);
	caps->max_fragment_size = SOF_IPC4_COMPR_MAX_FRAGMENT_SIZE;
	if (caps->max_fragment_size < caps->min_fragment_size)
		caps->max_fragment_size = caps->min_fragment_size;

	caps->min_fragments = SOF_IPC4_COMPR_MIN_FRAGMENTS;
	caps->max_fragments = SOF_IPC4_COMPR_MAX_FRAGMENTS;
	/*
	 * A buffer of max_fragments number of max_fragment_size fragments must
	 * be describable by the page table allocated for the stream
	 */
	max_buffer_size = sof_ipc4_compr_calc_max_buffer_size(&spcm->stream[dir]);
	max_fragments = div_u64(max_buffer_size, caps->max_fragment_size);
	if (caps->max_fragments > max_fragments)
		caps->max_fragments = max_fragments;

	spcm_dbg(spcm, dir,
		 "num_codecs: %u, fragment_size: %u-%u, fragments: %u-%u\n",
		 caps->num_codecs,
		 caps->min_fragment_size, caps->max_fragment_size,
		 caps->min_fragments, caps->max_fragments);
	return 0;
}

static int sof_ipc4_compr_alloc_pages(struct device *dev,
				      struct snd_sof_pcm_stream *sps,
				      struct snd_soc_component *component,
				      struct snd_compr_stream *cstream)
{
	u32 min_fragment_size = sof_ipc4_compr_calc_min_fragment_size(sps);
	u64 max_buffer_size = sof_ipc4_compr_calc_max_buffer_size(sps);
	struct snd_compr_runtime *crtd = cstream->runtime;
	u64 fragments = crtd->buffer_size;
	int ret;

	if (crtd->buffer_size < SOF_IPC4_COMPR_MIN_BUFFER_SIZE(min_fragment_size)) {
		dev_err(dev, "%s: Buffer size is too small: %llu (minimum is %u)\n",
			__func__, crtd->buffer_size,
			SOF_IPC4_COMPR_MIN_BUFFER_SIZE(min_fragment_size));
		return -EINVAL;
	}

	if (crtd->buffer_size > max_buffer_size) {
		dev_err(dev, "%s: Buffer size is too big: %llu (maximum is %llu)\n",
			__func__, crtd->buffer_size, max_buffer_size);
		return -EINVAL;
	}

	if (crtd->fragment_size < min_fragment_size) {
		dev_err(dev, "%s: Fragment size is too small: %u (minimum is %u)\n",
			__func__, crtd->fragment_size, min_fragment_size);
		return -EINVAL;
	}

	do_div(fragments, crtd->fragment_size);
	if (fragments < SOF_IPC4_COMPR_MIN_FRAGMENTS ||
	    fragments > SOF_IPC4_COMPR_MAX_FRAGMENTS) {
		dev_err(dev,
			"%s: Incorrect number of fragments: %llu (valid: %d-%d)\n",
			__func__, fragments, SOF_IPC4_COMPR_MIN_FRAGMENTS,
			SOF_IPC4_COMPR_MAX_FRAGMENTS);
		return -EINVAL;
	}

	cstream->dma_buffer.dev.type = SNDRV_DMA_TYPE_DEV_SG;
	cstream->dma_buffer.dev.dev = dev;

	ret = snd_compr_malloc_pages(cstream, crtd->buffer_size);
	if (ret < 0)
		return ret;

	ret = snd_sof_compr_create_page_table(component, cstream, crtd->dma_area,
					      crtd->dma_bytes);
	if (ret < 0)
		snd_compr_free_pages(cstream);

	return ret;
}

static bool
sof_ipc4_compr_codec_supported(struct snd_sof_dev *sdev, int codec_id, int dir)
{
	struct sof_ipc4_fw_data *ipc4_data = sdev->private;
	struct sof_ipc4_codec_info_data *codec_info = ipc4_data->codec_info;
	int i;

	/* No compress support available in booted firmware */
	if (!codec_info || !codec_info->count)
		return false;

	for (i = 0; i < codec_info->count; i++) {
		int _dir = SOF_IPC4_CODEC_INFO_GET_DIR(codec_info->items[i]);
		int _id = SOF_IPC4_CODEC_INFO_GET_ID(codec_info->items[i]);

		if (_dir == dir && codec_id == _id)
			return true;
	}

	return false;
}

static int sof_ipc4_compr_set_params(struct snd_soc_component *component,
				     struct snd_compr_stream *cstream,
				     struct snd_compr_params *params)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(component);
	const struct sof_ipc_tplg_ops *tplg_ops = sof_ipc_get_ops(sdev, tplg);
	struct sof_ipc4_compr_init_data *compr_data __free(kfree) = NULL;
	struct snd_soc_pcm_runtime *rtd = cstream->private_data;
	struct snd_sof_platform_stream_params *platform_params;
	struct sof_ipc4_timestamp_info *time_info;
	struct snd_compr_params *compr_params;
	struct snd_sof_widget *host_swidget;
	struct sof_ipc4_process *process;
	struct snd_pcm_hw_params p = {0};
	struct snd_interval *interval;
	struct snd_sof_pcm *spcm;
	struct snd_mask *fmt;
	int dir = cstream->direction;
	int ret;

	spcm = snd_sof_find_spcm_dai(component, rtd);
	if (!spcm)
		return -EINVAL;

	host_swidget = snd_sof_find_swidget_by_comp_id(sdev, spcm->stream[dir].comp_id);
	if (!host_swidget) {
		spcm_err(spcm, dir, "failed to find host widget with comp_id %d\n",
			 spcm->stream[dir].comp_id);
		return -ENODEV;
	}

	if (!sof_ipc4_compr_codec_supported(sdev, params->codec.id, dir)) {
		spcm_err(spcm, dir, "Unsupported codec id: %u\n", params->codec.id);
		return -EINVAL;
	}

	spcm_dbg(spcm, dir,
		 "codec_id: %u, rate: %u, ch in/out: %u/%u, format: %u/%u\n",
		 params->codec.id, params->codec.sample_rate, params->codec.ch_in,
		 params->codec.ch_out, params->codec.format, params->codec.pcm_format);

	if (spcm->prepared[dir]) {
		/*
		 * This can only happen if user space re-configures the device
		 * without closing it, for example after DRAIN completion
		 */
		ret = sof_ipc4_compr_stream_free(sdev, spcm, cstream);
		if (ret)
			return ret;
	}

	/* save the compress params */
	compr_params = &spcm->cparams[dir];
	memcpy(compr_params, params, sizeof(*params));

	/*
	 * Force format, rate and channels and use PCM hw_params structure to
	 * set up the pipelines.
	 */
	fmt = hw_param_mask(&p, SNDRV_PCM_HW_PARAM_FORMAT);
	snd_mask_none(fmt);
	/* Use correct format based on the used codec */
	switch (params->codec.id) {
	case SND_AUDIOCODEC_PCM:
		/* The format is provided by user space, it must be validated */
		if (snd_pcm_format_width((snd_pcm_format_t)params->codec.format) <= 0) {
			spcm_err(spcm, dir, "Invalid PCM format: %u\n",
				 params->codec.format);
			return -EINVAL;
		}
		snd_mask_set_format(fmt, (snd_pcm_format_t)params->codec.format);
		break;
	case SND_AUDIOCODEC_VORBIS:
		snd_mask_set_format(fmt, SNDRV_PCM_FORMAT_S16_LE);
		break;
	case SND_AUDIOCODEC_FLAC:
	{
		struct snd_dec_flac *dec_flac = &params->codec.options.flac_d;

		if (dec_flac->sample_size == 16)
			snd_mask_set_format(fmt, SNDRV_PCM_FORMAT_S16_LE);
		else
			snd_mask_set_format(fmt, SNDRV_PCM_FORMAT_S32_LE);
		break;
	}
	default:
		snd_mask_set_format(fmt, SNDRV_PCM_FORMAT_S32_LE);
	}

	interval = hw_param_interval(&p, SNDRV_PCM_HW_PARAM_CHANNELS);
	interval->min = compr_params->codec.ch_out;
	interval->max = compr_params->codec.ch_out;

	interval = hw_param_interval(&p, SNDRV_PCM_HW_PARAM_RATE);
	interval->min = compr_params->codec.sample_rate;
	interval->max = compr_params->codec.sample_rate;

	ret = sof_ipc4_compr_alloc_pages(sdev->dev, &spcm->stream[dir],
					 component, cstream);
	if (ret < 0)
		return ret;

	spcm_dbg(spcm, dir,
		 "buffer_size: %llu, fragment_size: %u (fragments: %u)\n",
		 cstream->runtime->buffer_size, cstream->runtime->fragment_size,
		 (u32)cstream->runtime->buffer_size / cstream->runtime->fragment_size);

	interval = hw_param_interval(&p, SNDRV_PCM_HW_PARAM_PERIOD_BYTES);
	interval->min = cstream->runtime->fragment_size;
	interval->max = cstream->runtime->fragment_size;

	interval = hw_param_interval(&p, SNDRV_PCM_HW_PARAM_BUFFER_BYTES);
	interval->min = cstream->runtime->buffer_size;
	interval->max = cstream->runtime->buffer_size;

	platform_params = &spcm->platform_params[dir];
	ret = snd_sof_compr_platform_hw_params(sdev, cstream, compr_params,
					       platform_params);
	if (ret < 0) {
		spcm_err(spcm, dir, "platform compress hw params failed\n");
		goto free_pages;
	}

	/* set up the list of DAPM widgets if not already done */
	if (!spcm->stream[dir].list) {
		ret = sof_pcm_setup_connected_widgets(sdev, rtd, spcm, &p,
						      platform_params, dir);
		if (ret < 0)
			goto free_pages;
	}

	process = sof_ipc4_compr_get_module(spcm, dir);
	if (!process) {
		ret = -EINVAL;
		goto free_list;
	}

	compr_data = kzalloc(sizeof(*compr_data), GFP_KERNEL);
	if (!compr_data) {
		ret = -ENOMEM;
		goto free_list;
	}

	memcpy(&compr_data->codec, &compr_params->codec, sizeof(compr_data->codec));
	compr_data->dir = dir;

	process->init_ext_module_data = compr_data;
	process->init_ext_module_size = sizeof(*compr_data);

	/*
	 * Make sure that the DSP is booted up, which might not be the
	 * case if the on-demand DSP boot is used
	 */
	ret = snd_sof_boot_dsp_firmware(sdev);
	if (ret)
		goto clear_init_ext;

	/* set the host DMA ID */
	if (tplg_ops && tplg_ops->host_config)
		tplg_ops->host_config(sdev, host_swidget, platform_params);

	/* set up the widgets and pipelines in the DSP */
	ret = sof_widget_list_setup(sdev, spcm, &p, platform_params, dir);
	if (ret < 0) {
		spcm_err(spcm, dir, "widget list set up failed\n");
		goto clear_init_ext;
	}

	memcpy(&spcm->params[dir], &p, sizeof(p));
	spcm->prepared[dir] = true;

	time_info = sof_ipc4_sps_to_time_info(&spcm->stream[dir]);
	if (time_info) {
		/* delay calculation supported */
		time_info->stream_start_offset = SOF_IPC4_INVALID_STREAM_POSITION;
		time_info->llp_offset = 0;

		sof_ipc4_build_time_info(sdev, &spcm->stream[dir]);
	}

	process->init_ext_module_data = NULL;
	process->init_ext_module_size = 0;

	return 0;

clear_init_ext:
	process->init_ext_module_data = NULL;
	process->init_ext_module_size = 0;

free_list:
	sof_widget_list_unprepare(sdev, spcm, dir);

free_pages:
	snd_compr_free_pages(cstream);

	return ret;
}

static int sof_ipc4_compr_get_params(struct snd_soc_component *component,
				     struct snd_compr_stream *cstream,
				     struct snd_codec *params)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(component);
	struct snd_soc_pcm_runtime *rtd = cstream->private_data;
	struct snd_sof_pcm *spcm;
	/* TODO: we don't query the supported codecs for now, if the
	 * application asks for an unsupported codec the set_params() will fail.
	 */
	spcm = snd_sof_find_spcm_dai(component, rtd);
	if (!spcm) {
		dev_err(sdev->dev, "%s: can't find spcm\n", __func__);
		return -EINVAL;
	}

	spcm_dbg(spcm, cstream->direction, "Entry: get_params\n");

	memcpy(params, &spcm->cparams[cstream->direction].codec,
	       sizeof(*params));

	return 0;
}

static int sof_ipc4_compr_trigger(struct snd_soc_component *component,
				  struct snd_compr_stream *cstream, int cmd)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(component);
	struct snd_soc_pcm_runtime *rtd = cstream->private_data;
	const struct sof_ipc_pcm_ops *pcm_ops = sof_ipc_get_ops(sdev, pcm);
	struct snd_sof_pcm *spcm;
	int dir = cstream->direction;
	bool trigger_platform = false;
	int ret = 0;

	spcm = snd_sof_find_spcm_dai(component, rtd);
	if (!spcm) {
		dev_err(sdev->dev, "%s: can't find spcm\n", __func__);
		return -EINVAL;
	}

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		/*
		 * The host DMA is left running on stop and drain to match the
		 * firmware, which only pauses the pipeline, and it is only
		 * stopped during hw_free. A stream restarted after stop or
		 * drain - which the compress core allows for capture - would
		 * continue from a position the two sides no longer agree on.
		 */
		if (spcm->compr_started[dir]) {
			spcm_err(spcm, dir, "Stream restart is not supported\n");
			return -EINVAL;
		}
		fallthrough;
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		spcm->pending_stop[dir] = false;
		trigger_platform = true;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		spcm->pending_stop[dir] = false;
		break;
	case SND_COMPR_TRIGGER_DRAIN:
		spcm->pending_stop[dir] = true;
		break;
	case SND_COMPR_TRIGGER_PARTIAL_DRAIN:
	case SND_COMPR_TRIGGER_NEXT_TRACK:
		/* Gapless playback is not supported */
		spcm_dbg(spcm, dir, "Unsupported trigger cmd: %d\n", cmd);
		return -EOPNOTSUPP;
	default:
		spcm_dbg(spcm, dir, "Unhandled trigger cmd: %d\n", cmd);
		return 0;
	}

	spcm_dbg(spcm, dir, "Entry: trigger (cmd: %d)\n", cmd);

	ret = pcm_ops->trigger(component, NULL, spcm, cmd, dir);
	if (ret < 0) {
		spcm_err(spcm, dir, "pcm_ops->trigger failed for cmd %d\n", cmd);
		return ret;
	}

	if (!ret && trigger_platform) {
		ret = snd_sof_compr_platform_trigger(sdev, cstream, cmd);
		if (ret < 0)
			spcm_err(spcm, dir,
				 "platform compress trigger start failed %d\n",
				 ret);
	}

	if (!ret && cmd == SNDRV_PCM_TRIGGER_START)
		spcm->compr_started[dir] = true;

	return ret;
}

static int sof_ipc4_compr_copy_playback(struct snd_soc_component *component,
					struct snd_compr_stream *cstream,
					char __user *buf, size_t count)
{
	struct snd_compr_runtime *crtd = cstream->runtime;
	u64 offset, n;
	void *ptr;
	int ret;

	div64_u64_rem(crtd->total_bytes_available, crtd->buffer_size, &offset);
	ptr = crtd->dma_area + offset;
	n = crtd->buffer_size - offset;

	if (count < n) {
		ret = copy_from_user(ptr, buf, count);
	} else {
		ret = copy_from_user(ptr, buf, n);
		ret += copy_from_user(crtd->dma_area, buf + n, count - n);
	}

	return count - ret;
}

static int sof_ipc4_compr_copy_capture(struct snd_compr_runtime *crtd,
				       char __user *buf, size_t count)
{
	u64 offset, n;
	void *ptr;
	int ret;

	div64_u64_rem(crtd->total_bytes_transferred, crtd->buffer_size, &offset);
	ptr = crtd->dma_area + offset;
	n = crtd->buffer_size - offset;

	if (count < n) {
		ret = copy_to_user(buf, ptr, count);
	} else {
		ret = copy_to_user(buf, ptr, n);
		ret += copy_to_user(buf + n, crtd->dma_area, count - n);
	}

	return count - ret;
}

static int sof_ipc4_compr_copy(struct snd_soc_component *component,
			       struct snd_compr_stream *cstream,
			       char __user *buf, size_t count)
{
	struct snd_compr_runtime *crtd = cstream->runtime;

	if (count > crtd->buffer_size)
		count = crtd->buffer_size;

	if (cstream->direction == SND_COMPRESS_PLAYBACK)
		return sof_ipc4_compr_copy_playback(component, cstream, buf, count);

	return sof_ipc4_compr_copy_capture(crtd, buf, count);
}

static int sof_ipc4_compr_pointer(struct snd_soc_component *component,
				  struct snd_compr_stream *cstream,
				  struct snd_compr_tstamp64 *tstamp)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(component);
	struct snd_soc_pcm_runtime *rtd = cstream->private_data;
	struct sof_ipc4_timestamp_info *time_info;
	struct snd_pcm_hw_params *params;
	struct snd_sof_pcm_stream *sps;
	struct snd_sof_pcm *spcm;
	u64 dai_cnt = 0;
	int ret;

	spcm = snd_sof_find_spcm_dai(component, rtd);
	if (!spcm)
		return -EINVAL;

	params = &spcm->params[cstream->direction];

	ret = snd_sof_compr_platform_pointer(sdev, cstream, tstamp);
	if (ret < 0) {
		spcm_err(spcm, cstream->direction,
			 "platform compress pointer failed %d\n", ret);
		return ret;
	}

	sps = &spcm->stream[cstream->direction];
	time_info = sof_ipc4_sps_to_time_info(sps);
	if (!time_info)
		goto host_only;

	/*
	 * stream_start_offset is updated to memory window by FW based on
	 * pipeline statistics and it may be invalid if host query happens before
	 * the statistics is complete. And it will not change after the first
	 * initialization.
	 */
	if (time_info->stream_start_offset == SOF_IPC4_INVALID_STREAM_POSITION) {
		ret = sof_ipc4_get_stream_start_offset(sdev, NULL, sps, time_info);
		if (ret < 0)
			goto host_only;
	}

	if (!time_info->llp_offset) {
		dai_cnt = snd_sof_compr_get_dai_frame_counter(sdev, cstream);
	} else {
		struct sof_ipc4_llp_reading_slot llp;

		sof_mailbox_read(sdev, time_info->llp_offset, &llp, sizeof(llp));
		dai_cnt = ((u64)llp.reading.llp_u << 32) | llp.reading.llp_l;
	}

	if (dai_cnt) {
		dai_cnt = sof_ipc4_frames_dai_to_host(time_info, dai_cnt);
		dai_cnt += time_info->stream_end_offset;
		if (dai_cnt < time_info->stream_start_offset)
			dai_cnt = 0;
		else
			dai_cnt -= time_info->stream_start_offset;
	}

host_only:
	tstamp->sampling_rate = params_rate(params);
	tstamp->pcm_io_frames = dai_cnt;

	return 0;
}

void sof_ipc4_compr_drain_done(struct snd_sof_dev *sdev, void *ipc_message)
{
	struct sof_ipc4_msg *ipc4_msg = ipc_message;
	struct sof_ipc4_notify_module_data *ndata = ipc4_msg->data_ptr;
	struct snd_sof_widget *swidget, *host_swidget;
	bool widget_found = false;
	struct snd_sof_pcm *spcm;
	int dir;

	/* Find the swidget based on ndata->module_id and ndata->instance_id */
	swidget = sof_ipc4_find_swidget_by_ids(sdev, ndata->module_id,
					       ndata->instance_id);
	if (!swidget) {
		dev_err(sdev->dev, "%s: Failed to find widget for module %u.%u\n",
			__func__, ndata->module_id, ndata->instance_id);
		return;
	}

	if (!swidget->spipe || !swidget->spipe->pipe_widget)
		return;

	/* Find the swidget of the host copier on the same pipeline */
	list_for_each_entry(host_swidget, &sdev->widget_list, list) {
		if (WIDGET_IS_AIF(host_swidget->id) &&
		    host_swidget->pipeline_id == swidget->pipeline_id) {
			widget_found = true;
			break;
		}
	}

	if (!widget_found) {
		dev_err(sdev->dev, "%s: Host widget not found for pipeline: %s\n",
			__func__, swidget->spipe->pipe_widget->widget->name);
		return;
	}

	/* Look up the spcm of the host copier */
	spcm = snd_sof_find_spcm_comp(sdev->component, host_swidget->comp_id, &dir);
	if (!spcm) {
		dev_err(sdev->dev, "%s: Stream cannot be found for %s\n", __func__,
			host_swidget->widget->name);
		return;
	}

	spcm_dbg(spcm, dir, "Entry: EOS done\n");

	if (spcm->stream[dir].cstream)
		snd_compr_drain_notify(spcm->stream[dir].cstream);
}

const struct snd_compress_ops sof_ipc4_compressed_ops = {
	.open		= sof_ipc4_compr_open,
	.free		= sof_ipc4_compr_free,
	.get_caps	= sof_ipc4_compr_get_caps,
	.set_params	= sof_ipc4_compr_set_params,
	.get_params	= sof_ipc4_compr_get_params,
	.trigger	= sof_ipc4_compr_trigger,
	.pointer	= sof_ipc4_compr_pointer,
	.copy		= sof_ipc4_compr_copy,
};
