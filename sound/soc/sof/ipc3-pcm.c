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

const struct sof_ipc_pcm_ops ipc3_pcm_ops = {
	.hw_free = sof_ipc3_pcm_hw_free,
};
