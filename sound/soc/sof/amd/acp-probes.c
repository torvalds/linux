// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license. When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2023 Advanced Micro Devices, Inc.
//
// Authors: V Sujith Kumar Reddy <Vsujithkumar.Reddy@amd.com>

/*
 * Probe interface for generic AMD audio ACP DSP block
 */

#include <linux/module.h>
#include <sound/soc.h>
#include "../sof-priv.h"
#include "../sof-client-probes.h"
#include "../sof-client.h"
#include "../ops.h"
#include "acp.h"
#include "acp-dsp-offset.h"

static int acp_probes_compr_startup(struct sof_client_dev *cdev,
				    struct snd_compr_stream *cstream,
				    struct snd_soc_dai *dai, u32 *stream_id)
{
	struct snd_sof_dev *sdev = sof_client_dev_to_sof_dev(cdev);
	struct acp_dsp_stream *stream;
	struct acp_dev_data *adata;

	adata = sdev->pdata->hw_pdata;
	stream = acp_dsp_stream_get(sdev, 0);
	if (!stream)
		return -ENODEV;

	stream->cstream = cstream;
	cstream->runtime->private_data = stream;

	adata->probe_stream = stream;
	*stream_id = stream->stream_tag;

	return 0;
}

static int acp_probes_compr_shutdown(struct sof_client_dev *cdev,
				     struct snd_compr_stream *cstream,
				     struct snd_soc_dai *dai)
{
	struct snd_sof_dev *sdev = sof_client_dev_to_sof_dev(cdev);
	struct acp_dsp_stream *stream = cstream->runtime->private_data;
	struct acp_dev_data *adata;
	int ret;

	ret = acp_dsp_stream_put(sdev, stream);
	if (ret < 0) {
		dev_err(sdev->dev, "Failed to release probe compress stream\n");
		return ret;
	}

	adata = sdev->pdata->hw_pdata;
	stream->cstream = NULL;
	cstream->runtime->private_data = NULL;
	adata->probe_stream = NULL;

	return 0;
}

static int acp_probes_compr_set_params(struct sof_client_dev *cdev,
				       struct snd_compr_stream *cstream,
				       struct snd_compr_params *params,
				       struct snd_soc_dai *dai)
{
	struct snd_sof_dev *sdev = sof_client_dev_to_sof_dev(cdev);
	struct acp_dsp_stream *stream = cstream->runtime->private_data;
	unsigned int buf_offset, index;
	u32 size;
	int ret;

	stream->dmab = cstream->runtime->dma_buffer_p;
	stream->num_pages = PFN_UP(cstream->runtime->dma_bytes);
	size = cstream->runtime->buffer_size;

	ret = acp_dsp_stream_config(sdev, stream);
	if (ret < 0) {
		acp_dsp_stream_put(sdev, stream);
		return ret;
	}

	/* write buffer size of stream in scratch memory */

	buf_offset = sdev->debug_box.offset +
		     offsetof(struct scratch_reg_conf, buf_size);
	index = stream->stream_tag - 1;
	buf_offset = buf_offset + index * 4;

	snd_sof_dsp_write(sdev, ACP_DSP_BAR, ACP_SCRATCH_REG_0 + buf_offset, size);

	return 0;
}

static int acp_probes_compr_trigger(struct sof_client_dev *cdev,
				    struct snd_compr_stream *cstream,
				    int cmd, struct snd_soc_dai *dai)
{
	/* Nothing to do here, as it is a mandatory callback just defined */
	return 0;
}

static int acp_probes_compr_pointer(struct sof_client_dev *cdev,
				    struct snd_compr_stream *cstream,
				    struct snd_compr_tstamp *tstamp,
				    struct snd_soc_dai *dai)
{
	struct acp_dsp_stream *stream = cstream->runtime->private_data;
	struct snd_soc_pcm_stream *pstream;

	pstream = &dai->driver->capture;
	tstamp->copied_total = stream->cstream_posn;
	tstamp->sampling_rate = snd_pcm_rate_bit_to_rate(pstream->rates);

	return 0;
}

/* SOF client implementation */
static const struct sof_probes_host_ops acp_probes_ops = {
	.startup = acp_probes_compr_startup,
	.shutdown = acp_probes_compr_shutdown,
	.set_params = acp_probes_compr_set_params,
	.trigger = acp_probes_compr_trigger,
	.pointer = acp_probes_compr_pointer,
};

int acp_probes_register(struct snd_sof_dev *sdev)
{
	return sof_client_dev_register(sdev, "acp-probes", 0, &acp_probes_ops,
				       sizeof(acp_probes_ops));
}
EXPORT_SYMBOL_NS(acp_probes_register, SND_SOC_SOF_AMD_COMMON);

void acp_probes_unregister(struct snd_sof_dev *sdev)
{
	sof_client_dev_unregister(sdev, "acp-probes", 0);
}
EXPORT_SYMBOL_NS(acp_probes_unregister, SND_SOC_SOF_AMD_COMMON);

MODULE_IMPORT_NS(SND_SOC_SOF_CLIENT);

