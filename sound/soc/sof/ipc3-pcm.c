// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2021 Intel Corporation. All rights reserved.
//
//

#include <sound/pcm_params.h>
#include "ipc3-ops.h"
#include "ops.h"
#include "sof-priv.h"
#include "sof-audio.h"

static int sof_ipc3_pcm_hw_free(struct snd_soc_component *component,
				struct snd_pcm_substream *substream)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(component);
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct sof_ipc_stream stream;
	struct sof_ipc_reply reply;
	struct snd_sof_pcm *spcm;

	spcm = snd_sof_find_spcm_dai(component, rtd);
	if (!spcm)
		return -EINVAL;

	if (!spcm->prepared[substream->stream])
		return 0;

	stream.hdr.size = sizeof(stream);
	stream.hdr.cmd = SOF_IPC_GLB_STREAM_MSG | SOF_IPC_STREAM_PCM_FREE;
	stream.comp_id = spcm->stream[substream->stream].comp_id;

	/* send IPC to the DSP */
	return sof_ipc_tx_message(sdev->ipc, stream.hdr.cmd, &stream,
				  sizeof(stream), &reply, sizeof(reply));
}

static int sof_ipc3_pcm_hw_params(struct snd_soc_component *component,
				  struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_sof_platform_stream_params *platform_params)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(component);
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct sof_ipc_fw_version *v = &sdev->fw_ready.version;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct sof_ipc_pcm_params_reply ipc_params_reply;
	struct sof_ipc_pcm_params pcm;
	struct snd_sof_pcm *spcm;
	int ret;

	spcm = snd_sof_find_spcm_dai(component, rtd);
	if (!spcm)
		return -EINVAL;

	memset(&pcm, 0, sizeof(pcm));

	/* number of pages should be rounded up */
	pcm.params.buffer.pages = PFN_UP(runtime->dma_bytes);

	/* set IPC PCM parameters */
	pcm.hdr.size = sizeof(pcm);
	pcm.hdr.cmd = SOF_IPC_GLB_STREAM_MSG | SOF_IPC_STREAM_PCM_PARAMS;
	pcm.comp_id = spcm->stream[substream->stream].comp_id;
	pcm.params.hdr.size = sizeof(pcm.params);
	pcm.params.buffer.phy_addr = spcm->stream[substream->stream].page_table.addr;
	pcm.params.buffer.size = runtime->dma_bytes;
	pcm.params.direction = substream->stream;
	pcm.params.sample_valid_bytes = params_width(params) >> 3;
	pcm.params.buffer_fmt = SOF_IPC_BUFFER_INTERLEAVED;
	pcm.params.rate = params_rate(params);
	pcm.params.channels = params_channels(params);
	pcm.params.host_period_bytes = params_period_bytes(params);

	/* container size */
	ret = snd_pcm_format_physical_width(params_format(params));
	if (ret < 0)
		return ret;
	pcm.params.sample_container_bytes = ret >> 3;

	/* format */
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16:
		pcm.params.frame_fmt = SOF_IPC_FRAME_S16_LE;
		break;
	case SNDRV_PCM_FORMAT_S24:
		pcm.params.frame_fmt = SOF_IPC_FRAME_S24_4LE;
		break;
	case SNDRV_PCM_FORMAT_S32:
		pcm.params.frame_fmt = SOF_IPC_FRAME_S32_LE;
		break;
	case SNDRV_PCM_FORMAT_FLOAT:
		pcm.params.frame_fmt = SOF_IPC_FRAME_FLOAT;
		break;
	default:
		return -EINVAL;
	}

	/* Update the IPC message with information from the platform */
	pcm.params.stream_tag = platform_params->stream_tag;

	if (platform_params->use_phy_address)
		pcm.params.buffer.phy_addr = platform_params->phy_addr;

	if (platform_params->no_ipc_position) {
		/* For older ABIs set host_period_bytes to zero to inform
		 * FW we don't want position updates. Newer versions use
		 * no_stream_position for this purpose.
		 */
		if (v->abi_version < SOF_ABI_VER(3, 10, 0))
			pcm.params.host_period_bytes = 0;
		else
			pcm.params.no_stream_position = 1;
	}

	dev_dbg(component->dev, "stream_tag %d", pcm.params.stream_tag);

	/* send hw_params IPC to the DSP */
	ret = sof_ipc_tx_message(sdev->ipc, pcm.hdr.cmd, &pcm, sizeof(pcm),
				 &ipc_params_reply, sizeof(ipc_params_reply));
	if (ret < 0) {
		dev_err(component->dev, "HW params ipc failed for stream %d\n",
			pcm.params.stream_tag);
		return ret;
	}

	ret = snd_sof_set_stream_data_offset(sdev, substream, ipc_params_reply.posn_offset);
	if (ret < 0) {
		dev_err(component->dev, "%s: invalid stream data offset for PCM %d\n",
			__func__, spcm->pcm.pcm_id);
		return ret;
	}

	return ret;
}

static int sof_ipc3_pcm_trigger(struct snd_soc_component *component,
				struct snd_pcm_substream *substream, int cmd)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(component);
	struct sof_ipc_stream stream;
	struct sof_ipc_reply reply;
	struct snd_sof_pcm *spcm;

	spcm = snd_sof_find_spcm_dai(component, rtd);
	if (!spcm)
		return -EINVAL;

	stream.hdr.size = sizeof(stream);
	stream.hdr.cmd = SOF_IPC_GLB_STREAM_MSG;
	stream.comp_id = spcm->stream[substream->stream].comp_id;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		stream.hdr.cmd |= SOF_IPC_STREAM_TRIG_PAUSE;
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		stream.hdr.cmd |= SOF_IPC_STREAM_TRIG_RELEASE;
		break;
	case SNDRV_PCM_TRIGGER_START:
		stream.hdr.cmd |= SOF_IPC_STREAM_TRIG_START;
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
		fallthrough;
	case SNDRV_PCM_TRIGGER_STOP:
		stream.hdr.cmd |= SOF_IPC_STREAM_TRIG_STOP;
		break;
	default:
		dev_err(component->dev, "Unhandled trigger cmd %d\n", cmd);
		return -EINVAL;
	}

	/* send IPC to the DSP */
	return sof_ipc_tx_message(sdev->ipc, stream.hdr.cmd, &stream,
				  sizeof(stream), &reply, sizeof(reply));
}

const struct sof_ipc_pcm_ops ipc3_pcm_ops = {
	.hw_params = sof_ipc3_pcm_hw_params,
	.hw_free = sof_ipc3_pcm_hw_free,
	.trigger = sof_ipc3_pcm_trigger,
};
