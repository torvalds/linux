// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// Copyright 2021 NXP
//
// Author: Daniel Baluta <daniel.baluta@nxp.com>

#include <sound/soc.h>
#include <sound/sof.h>
#include <sound/compress_driver.h>
#include "sof-audio.h"
#include "sof-priv.h"
#include "sof-utils.h"
#include "ops.h"

static void sof_set_transferred_bytes(struct sof_compr_stream *sstream,
				      u64 host_pos, u64 buffer_size)
{
	u64 prev_pos;
	unsigned int copied;

	div64_u64_rem(sstream->copied_total, buffer_size, &prev_pos);

	if (host_pos < prev_pos)
		copied = (buffer_size - prev_pos) + host_pos;
	else
		copied = host_pos - prev_pos;

	sstream->copied_total += copied;
}

static void snd_sof_compr_fragment_elapsed_work(struct work_struct *work)
{
	struct snd_sof_pcm_stream *sps =
		container_of(work, struct snd_sof_pcm_stream,
			     period_elapsed_work);

	snd_compr_fragment_elapsed(sps->cstream);
}

void snd_sof_compr_init_elapsed_work(struct work_struct *work)
{
	INIT_WORK(work, snd_sof_compr_fragment_elapsed_work);
}

/*
 * sof compr fragment elapse, this could be called in irq thread context
 */
void snd_sof_compr_fragment_elapsed(struct snd_compr_stream *cstream)
{
	struct snd_soc_pcm_runtime *rtd;
	struct snd_compr_runtime *crtd;
	struct snd_soc_component *component;
	struct sof_compr_stream *sstream;
	struct snd_sof_pcm *spcm;

	if (!cstream)
		return;

	rtd = cstream->private_data;
	crtd = cstream->runtime;
	sstream = crtd->private_data;
	component = snd_soc_rtdcom_lookup(rtd, SOF_AUDIO_PCM_DRV_NAME);

	spcm = snd_sof_find_spcm_dai(component, rtd);
	if (!spcm) {
		dev_err(component->dev,
			"fragment elapsed called for unknown stream!\n");
		return;
	}

	sof_set_transferred_bytes(sstream, spcm->stream[cstream->direction].posn.host_posn,
				  crtd->buffer_size);

	/* use the same workqueue-based solution as for PCM, cf. snd_sof_pcm_elapsed */
	schedule_work(&spcm->stream[cstream->direction].period_elapsed_work);
}

static int create_page_table(struct snd_soc_component *component,
			     struct snd_compr_stream *cstream,
			     unsigned char *dma_area, size_t size)
{
	struct snd_dma_buffer *dmab = cstream->runtime->dma_buffer_p;
	struct snd_soc_pcm_runtime *rtd = cstream->private_data;
	int dir = cstream->direction;
	struct snd_sof_pcm *spcm;

	spcm = snd_sof_find_spcm_dai(component, rtd);
	if (!spcm)
		return -EINVAL;

	return snd_sof_create_page_table(component->dev, dmab,
					 spcm->stream[dir].page_table.area, size);
}

static int sof_compr_open(struct snd_soc_component *component,
			  struct snd_compr_stream *cstream)
{
	struct snd_soc_pcm_runtime *rtd = cstream->private_data;
	struct snd_compr_runtime *crtd = cstream->runtime;
	struct sof_compr_stream *sstream;
	struct snd_sof_pcm *spcm;
	int dir;

	sstream = kzalloc(sizeof(*sstream), GFP_KERNEL);
	if (!sstream)
		return -ENOMEM;

	spcm = snd_sof_find_spcm_dai(component, rtd);
	if (!spcm) {
		kfree(sstream);
		return -EINVAL;
	}

	dir = cstream->direction;

	if (spcm->stream[dir].cstream) {
		kfree(sstream);
		return -EBUSY;
	}

	spcm->stream[dir].cstream = cstream;
	spcm->stream[dir].posn.host_posn = 0;
	spcm->stream[dir].posn.dai_posn = 0;
	spcm->prepared[dir] = false;

	crtd->private_data = sstream;

	return 0;
}

static int sof_compr_free(struct snd_soc_component *component,
			  struct snd_compr_stream *cstream)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(component);
	struct sof_compr_stream *sstream = cstream->runtime->private_data;
	struct snd_soc_pcm_runtime *rtd = cstream->private_data;
	struct sof_ipc_stream stream;
	struct snd_sof_pcm *spcm;
	int ret = 0;

	spcm = snd_sof_find_spcm_dai(component, rtd);
	if (!spcm)
		return -EINVAL;

	stream.hdr.size = sizeof(stream);
	stream.hdr.cmd = SOF_IPC_GLB_STREAM_MSG | SOF_IPC_STREAM_PCM_FREE;
	stream.comp_id = spcm->stream[cstream->direction].comp_id;

	if (spcm->prepared[cstream->direction]) {
		ret = sof_ipc_tx_message_no_reply(sdev->ipc, &stream, sizeof(stream));
		if (!ret)
			spcm->prepared[cstream->direction] = false;
	}

	cancel_work_sync(&spcm->stream[cstream->direction].period_elapsed_work);
	spcm->stream[cstream->direction].cstream = NULL;
	kfree(sstream);

	return ret;
}

static int sof_compr_set_params(struct snd_soc_component *component,
				struct snd_compr_stream *cstream, struct snd_compr_params *params)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(component);
	struct snd_soc_pcm_runtime *rtd = cstream->private_data;
	struct snd_compr_runtime *crtd = cstream->runtime;
	struct sof_ipc_pcm_params_reply ipc_params_reply;
	struct sof_ipc_fw_ready *ready = &sdev->fw_ready;
	struct sof_ipc_fw_version *v = &ready->version;
	struct sof_compr_stream *sstream;
	struct sof_ipc_pcm_params *pcm;
	struct snd_sof_pcm *spcm;
	size_t ext_data_size;
	int ret;

	if (v->abi_version < SOF_ABI_VER(3, 22, 0)) {
		dev_err(component->dev,
			"Compress params not supported with FW ABI version %d:%d:%d\n",
			SOF_ABI_VERSION_MAJOR(v->abi_version),
			SOF_ABI_VERSION_MINOR(v->abi_version),
			SOF_ABI_VERSION_PATCH(v->abi_version));
		return -EINVAL;
	}

	sstream = crtd->private_data;

	spcm = snd_sof_find_spcm_dai(component, rtd);

	if (!spcm)
		return -EINVAL;

	ext_data_size = sizeof(params->codec);

	if (sizeof(*pcm) + ext_data_size > sdev->ipc->max_payload_size)
		return -EINVAL;

	pcm = kzalloc(sizeof(*pcm) + ext_data_size, GFP_KERNEL);
	if (!pcm)
		return -ENOMEM;

	cstream->dma_buffer.dev.type = SNDRV_DMA_TYPE_DEV_SG;
	cstream->dma_buffer.dev.dev = sdev->dev;
	ret = snd_compr_malloc_pages(cstream, crtd->buffer_size);
	if (ret < 0)
		goto out;

	ret = create_page_table(component, cstream, crtd->dma_area, crtd->dma_bytes);
	if (ret < 0)
		goto out;

	pcm->params.buffer.pages = PFN_UP(crtd->dma_bytes);
	pcm->hdr.size = sizeof(*pcm) + ext_data_size;
	pcm->hdr.cmd = SOF_IPC_GLB_STREAM_MSG | SOF_IPC_STREAM_PCM_PARAMS;

	pcm->comp_id = spcm->stream[cstream->direction].comp_id;
	pcm->params.hdr.size = sizeof(pcm->params) + ext_data_size;
	pcm->params.buffer.phy_addr = spcm->stream[cstream->direction].page_table.addr;
	pcm->params.buffer.size = crtd->dma_bytes;
	pcm->params.direction = cstream->direction;
	pcm->params.channels = params->codec.ch_out;
	pcm->params.rate = params->codec.sample_rate;
	pcm->params.buffer_fmt = SOF_IPC_BUFFER_INTERLEAVED;
	pcm->params.frame_fmt = SOF_IPC_FRAME_S32_LE;
	pcm->params.sample_container_bytes =
		snd_pcm_format_physical_width(SNDRV_PCM_FORMAT_S32) >> 3;
	pcm->params.host_period_bytes = params->buffer.fragment_size;
	pcm->params.ext_data_length = ext_data_size;

	memcpy((u8 *)pcm->params.ext_data, &params->codec, ext_data_size);

	ret = sof_ipc_tx_message(sdev->ipc, pcm, sizeof(*pcm) + ext_data_size,
				 &ipc_params_reply, sizeof(ipc_params_reply));
	if (ret < 0) {
		dev_err(component->dev, "error ipc failed\n");
		goto out;
	}

	ret = snd_sof_set_stream_data_offset(sdev, &spcm->stream[cstream->direction],
					     ipc_params_reply.posn_offset);
	if (ret < 0) {
		dev_err(component->dev, "Invalid stream data offset for Compr %d\n",
			spcm->pcm.pcm_id);
		goto out;
	}

	sstream->sampling_rate = params->codec.sample_rate;
	sstream->channels = params->codec.ch_out;
	sstream->sample_container_bytes = pcm->params.sample_container_bytes;

	spcm->prepared[cstream->direction] = true;

out:
	kfree(pcm);

	return ret;
}

static int sof_compr_get_params(struct snd_soc_component *component,
				struct snd_compr_stream *cstream, struct snd_codec *params)
{
	/* TODO: we don't query the supported codecs for now, if the
	 * application asks for an unsupported codec the set_params() will fail.
	 */
	return 0;
}

static int sof_compr_trigger(struct snd_soc_component *component,
			     struct snd_compr_stream *cstream, int cmd)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(component);
	struct snd_soc_pcm_runtime *rtd = cstream->private_data;
	struct sof_ipc_stream stream;
	struct snd_sof_pcm *spcm;

	spcm = snd_sof_find_spcm_dai(component, rtd);
	if (!spcm)
		return -EINVAL;

	stream.hdr.size = sizeof(stream);
	stream.hdr.cmd = SOF_IPC_GLB_STREAM_MSG;
	stream.comp_id = spcm->stream[cstream->direction].comp_id;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		stream.hdr.cmd |= SOF_IPC_STREAM_TRIG_START;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		stream.hdr.cmd |= SOF_IPC_STREAM_TRIG_STOP;
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		stream.hdr.cmd |= SOF_IPC_STREAM_TRIG_PAUSE;
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		stream.hdr.cmd |= SOF_IPC_STREAM_TRIG_RELEASE;
		break;
	default:
		dev_err(component->dev, "error: unhandled trigger cmd %d\n", cmd);
		break;
	}

	return sof_ipc_tx_message_no_reply(sdev->ipc, &stream, sizeof(stream));
}

static int sof_compr_copy_playback(struct snd_compr_runtime *rtd,
				   char __user *buf, size_t count)
{
	void *ptr;
	unsigned int offset, n;
	int ret;

	div_u64_rem(rtd->total_bytes_available, rtd->buffer_size, &offset);
	ptr = rtd->dma_area + offset;
	n = rtd->buffer_size - offset;

	if (count < n) {
		ret = copy_from_user(ptr, buf, count);
	} else {
		ret = copy_from_user(ptr, buf, n);
		ret += copy_from_user(rtd->dma_area, buf + n, count - n);
	}

	return count - ret;
}

static int sof_compr_copy_capture(struct snd_compr_runtime *rtd,
				  char __user *buf, size_t count)
{
	void *ptr;
	unsigned int offset, n;
	int ret;

	div_u64_rem(rtd->total_bytes_transferred, rtd->buffer_size, &offset);
	ptr = rtd->dma_area + offset;
	n = rtd->buffer_size - offset;

	if (count < n) {
		ret = copy_to_user(buf, ptr, count);
	} else {
		ret = copy_to_user(buf, ptr, n);
		ret += copy_to_user(buf + n, rtd->dma_area, count - n);
	}

	return count - ret;
}

static int sof_compr_copy(struct snd_soc_component *component,
			  struct snd_compr_stream *cstream,
			  char __user *buf, size_t count)
{
	struct snd_compr_runtime *rtd = cstream->runtime;

	if (count > rtd->buffer_size)
		count = rtd->buffer_size;

	if (cstream->direction == SND_COMPRESS_PLAYBACK)
		return sof_compr_copy_playback(rtd, buf, count);
	else
		return sof_compr_copy_capture(rtd, buf, count);
}

static int sof_compr_pointer(struct snd_soc_component *component,
			     struct snd_compr_stream *cstream,
			     struct snd_compr_tstamp64 *tstamp)
{
	struct snd_sof_pcm *spcm;
	struct snd_soc_pcm_runtime *rtd = cstream->private_data;
	struct sof_compr_stream *sstream = cstream->runtime->private_data;

	spcm = snd_sof_find_spcm_dai(component, rtd);
	if (!spcm)
		return -EINVAL;

	tstamp->sampling_rate = sstream->sampling_rate;
	tstamp->copied_total = sstream->copied_total;
	tstamp->pcm_io_frames = div_u64(spcm->stream[cstream->direction].posn.dai_posn,
					sstream->channels * sstream->sample_container_bytes);

	return 0;
}

struct snd_compress_ops sof_compressed_ops = {
	.open		= sof_compr_open,
	.free		= sof_compr_free,
	.set_params	= sof_compr_set_params,
	.get_params	= sof_compr_get_params,
	.trigger	= sof_compr_trigger,
	.pointer	= sof_compr_pointer,
	.copy		= sof_compr_copy,
};
EXPORT_SYMBOL(sof_compressed_ops);
