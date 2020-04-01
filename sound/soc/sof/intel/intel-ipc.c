// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2019 Intel Corporation. All rights reserved.
//
// Authors: Guennadi Liakhovetski <guennadi.liakhovetski@linux.intel.com>

/* Intel-specific SOF IPC code */

#include <linux/device.h>
#include <linux/export.h>
#include <linux/module.h>
#include <linux/types.h>

#include <sound/pcm.h>
#include <sound/sof/stream.h>

#include "../ops.h"
#include "../sof-priv.h"

struct intel_stream {
	size_t posn_offset;
};

/* Mailbox-based Intel IPC implementation */
void intel_ipc_msg_data(struct snd_sof_dev *sdev,
			struct snd_pcm_substream *substream,
			void *p, size_t sz)
{
	if (!substream || !sdev->stream_box.size) {
		sof_mailbox_read(sdev, sdev->dsp_box.offset, p, sz);
	} else {
		struct intel_stream *stream = substream->runtime->private_data;

		/* The stream might already be closed */
		if (stream)
			sof_mailbox_read(sdev, stream->posn_offset, p, sz);
	}
}
EXPORT_SYMBOL_NS(intel_ipc_msg_data, SND_SOC_SOF_INTEL_HIFI_EP_IPC);

int intel_ipc_pcm_params(struct snd_sof_dev *sdev,
			 struct snd_pcm_substream *substream,
			 const struct sof_ipc_pcm_params_reply *reply)
{
	struct intel_stream *stream = substream->runtime->private_data;
	size_t posn_offset = reply->posn_offset;

	/* check if offset is overflow or it is not aligned */
	if (posn_offset > sdev->stream_box.size ||
	    posn_offset % sizeof(struct sof_ipc_stream_posn) != 0)
		return -EINVAL;

	stream->posn_offset = sdev->stream_box.offset + posn_offset;

	dev_dbg(sdev->dev, "pcm: stream dir %d, posn mailbox offset is %zu",
		substream->stream, stream->posn_offset);

	return 0;
}
EXPORT_SYMBOL_NS(intel_ipc_pcm_params, SND_SOC_SOF_INTEL_HIFI_EP_IPC);

int intel_pcm_open(struct snd_sof_dev *sdev,
		   struct snd_pcm_substream *substream)
{
	struct intel_stream *stream = kmalloc(sizeof(*stream), GFP_KERNEL);

	if (!stream)
		return -ENOMEM;

	/* binding pcm substream to hda stream */
	substream->runtime->private_data = stream;

	return 0;
}
EXPORT_SYMBOL_NS(intel_pcm_open, SND_SOC_SOF_INTEL_HIFI_EP_IPC);

int intel_pcm_close(struct snd_sof_dev *sdev,
		    struct snd_pcm_substream *substream)
{
	struct intel_stream *stream = substream->runtime->private_data;

	substream->runtime->private_data = NULL;
	kfree(stream);

	return 0;
}
EXPORT_SYMBOL_NS(intel_pcm_close, SND_SOC_SOF_INTEL_HIFI_EP_IPC);

MODULE_LICENSE("Dual BSD/GPL");
