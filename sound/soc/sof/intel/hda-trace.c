// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2017 Intel Corporation. All rights reserved.
 *
 * Authors: Liam Girdwood <liam.r.girdwood@linux.intel.com>
 *	    Ranjani Sridharan <ranjani.sridharan@linux.intel.com>
 *	    Jeeja KP <jeeja.kp@intel.com>
 *	    Rander Wang <rander.wang@intel.com>
 *          Keyon Jie <yang.jie@linux.intel.com>
 */

/*
 * Hardware interface for generic Intel audio DSP HDA IP
 */

#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/firmware.h>
#include <linux/pci.h>
#include <sound/hdaudio_ext.h>
#include <sound/sof.h>
#include <sound/pcm_params.h>
#include <linux/pm_runtime.h>

#include "../sof-priv.h"
#include "../ops.h"
#include "hda.h"

static int hda_dsp_trace_prepare(struct snd_sof_dev *sdev)
{
	struct sof_intel_hda_stream *stream = sdev->hda->dtrace_stream;
	struct snd_dma_buffer *dmab = &sdev->dmatb;
	int ret;

	stream->bufsize = sdev->dmatb.bytes;

	ret = hda_dsp_stream_hw_params(sdev, stream, dmab, NULL);
	if (ret < 0)
		dev_err(sdev->dev, "error: hdac prepare failed: %x\n", ret);

	return ret;
}

int hda_dsp_trace_init(struct snd_sof_dev *sdev, u32 *tag)
{
	sdev->hda->dtrace_stream = hda_dsp_stream_get_cstream(sdev);

	if (!sdev->hda->dtrace_stream) {
		dev_err(sdev->dev,
			"error: no available capture stream for DMA trace\n");
		return -ENODEV;
	}

	*tag = sdev->hda->dtrace_stream->tag;

	/*
	 * initialize capture stream, set BDL address and return corresponding
	 * stream tag which will be sent to the firmware by IPC message.
	 */
	return hda_dsp_trace_prepare(sdev);
}

int hda_dsp_trace_release(struct snd_sof_dev *sdev)
{
	if (sdev->hda->dtrace_stream) {
		sdev->hda->dtrace_stream->open = false;
		hda_dsp_stream_put_cstream(sdev,
					   sdev->hda->dtrace_stream->tag);
		sdev->hda->dtrace_stream = NULL;
		return 0;
	}

	dev_dbg(sdev->dev, "DMA trace stream is not opened!\n");
	return -ENODEV;
}

int hda_dsp_trace_trigger(struct snd_sof_dev *sdev, int cmd)
{
	return hda_dsp_stream_trigger(sdev, sdev->hda->dtrace_stream, cmd);
}
