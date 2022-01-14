// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license. When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2021 Advanced Micro Devices, Inc.
//
// Authors: Ajit Kumar Pandey <AjitKumar.Pandey@amd.com>

/*
 * PCM interface for generic AMD audio ACP DSP block
 */
#include <sound/pcm_params.h>

#include "../ops.h"
#include "acp.h"
#include "acp-dsp-offset.h"

int acp_pcm_hw_params(struct snd_sof_dev *sdev, struct snd_pcm_substream *substream,
		      struct snd_pcm_hw_params *params, struct sof_ipc_stream_params *ipc_params)
{
	struct acp_dsp_stream *stream = substream->runtime->private_data;
	unsigned int buf_offset, index;
	u32 size;
	int ret;

	size = ipc_params->buffer.size;
	stream->num_pages = ipc_params->buffer.pages;
	stream->dmab = substream->runtime->dma_buffer_p;

	ret = acp_dsp_stream_config(sdev, stream);
	if (ret < 0) {
		dev_err(sdev->dev, "stream configuration failed\n");
		return ret;
	}

	ipc_params->buffer.phy_addr = stream->reg_offset;
	ipc_params->stream_tag = stream->stream_tag;

	/* write buffer size of stream in scratch memory */

	buf_offset = offsetof(struct scratch_reg_conf, buf_size);
	index = stream->stream_tag - 1;
	buf_offset = buf_offset + index * 4;

	snd_sof_dsp_write(sdev, ACP_DSP_BAR, ACP_SCRATCH_REG_0 + buf_offset, size);

	return 0;
}
EXPORT_SYMBOL_NS(acp_pcm_hw_params, SND_SOC_SOF_AMD_COMMON);

int acp_pcm_open(struct snd_sof_dev *sdev, struct snd_pcm_substream *substream)
{
	struct acp_dsp_stream *stream;

	stream = acp_dsp_stream_get(sdev, 0);
	if (!stream)
		return -ENODEV;

	substream->runtime->private_data = stream;
	stream->substream = substream;

	return 0;
}
EXPORT_SYMBOL_NS(acp_pcm_open, SND_SOC_SOF_AMD_COMMON);

int acp_pcm_close(struct snd_sof_dev *sdev, struct snd_pcm_substream *substream)
{
	struct acp_dsp_stream *stream;

	stream = substream->runtime->private_data;
	if (!stream) {
		dev_err(sdev->dev, "No open stream\n");
		return -EINVAL;
	}

	stream->substream = NULL;
	substream->runtime->private_data = NULL;

	return acp_dsp_stream_put(sdev, stream);
}
EXPORT_SYMBOL_NS(acp_pcm_close, SND_SOC_SOF_AMD_COMMON);
