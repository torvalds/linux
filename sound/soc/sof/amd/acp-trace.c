// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2021 Advanced Micro Devices, Inc. All rights reserved.
//
// Authors: Vishnuvardhanrao Ravuapati <vishnuvardhanrao.ravulapati@amd.com>
//	    V Sujith Kumar Reddy <Vsujithkumar.Reddy@amd.com>

/*This file support Host TRACE Logger driver callback for SOF FW */

#include "acp.h"

#define ACP_LOGGER_STREAM	8
#define NUM_PAGES		16

int acp_sof_trace_release(struct snd_sof_dev *sdev)
{
	struct acp_dsp_stream *stream;
	struct acp_dev_data *adata;
	int ret;

	adata = sdev->pdata->hw_pdata;
	stream = adata->dtrace_stream;
	ret = acp_dsp_stream_put(sdev, stream);
	if (ret < 0) {
		dev_err(sdev->dev, "Failed to release trace stream\n");
		return ret;
	}

	adata->dtrace_stream = NULL;
	return 0;
}
EXPORT_SYMBOL_NS(acp_sof_trace_release, SND_SOC_SOF_AMD_COMMON);

int acp_sof_trace_init(struct snd_sof_dev *sdev,
		       struct sof_ipc_dma_trace_params_ext *dtrace_params)
{
	struct acp_dsp_stream *stream;
	struct acp_dev_data *adata;
	int ret;

	adata = sdev->pdata->hw_pdata;
	stream = acp_dsp_stream_get(sdev, ACP_LOGGER_STREAM);
	if (!stream)
		return -ENODEV;

	stream->dmab = &sdev->dmatb;
	stream->num_pages = NUM_PAGES;

	ret = acp_dsp_stream_config(sdev, stream);
	if (ret < 0) {
		acp_dsp_stream_put(sdev, stream);
		return ret;
	}

	adata->dtrace_stream = stream;
	dtrace_params->stream_tag = stream->stream_tag;
	dtrace_params->buffer.phy_addr = stream->reg_offset;

	return 0;
}
EXPORT_SYMBOL_NS(acp_sof_trace_init, SND_SOC_SOF_AMD_COMMON);
