// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2019-2021 Intel Corporation. All rights reserved.
// Author: Cezary Rojewski <cezary.rojewski@intel.com>
//

#include <sound/soc.h>
#include "ops.h"
#include "sof-priv.h"
#include "sof-probes.h"

struct sof_probe_dma {
	unsigned int stream_tag;
	unsigned int dma_buffer_size;
} __packed;

struct sof_ipc_probe_dma_add_params {
	struct sof_ipc_cmd_hdr hdr;
	unsigned int num_elems;
	struct sof_probe_dma dma[];
} __packed;

struct sof_ipc_probe_info_params {
	struct sof_ipc_reply rhdr;
	unsigned int num_elems;
	union {
		struct sof_probe_dma dma[0];
		struct sof_probe_point_desc desc[0];
	};
} __packed;

struct sof_ipc_probe_point_add_params {
	struct sof_ipc_cmd_hdr hdr;
	unsigned int num_elems;
	struct sof_probe_point_desc desc[];
} __packed;

struct sof_ipc_probe_point_remove_params {
	struct sof_ipc_cmd_hdr hdr;
	unsigned int num_elems;
	unsigned int buffer_id[];
} __packed;

/**
 * sof_ipc_probe_init - initialize data probing
 * @sdev:		SOF sound device
 * @stream_tag:		Extractor stream tag
 * @buffer_size:	DMA buffer size to set for extractor
 *
 * Host chooses whether extraction is supported or not by providing
 * valid stream tag to DSP. Once specified, stream described by that
 * tag will be tied to DSP for extraction for the entire lifetime of
 * probe.
 *
 * Probing is initialized only once and each INIT request must be
 * matched by DEINIT call.
 */
static int sof_ipc_probe_init(struct snd_sof_dev *sdev, u32 stream_tag,
			      size_t buffer_size)
{
	struct sof_ipc_probe_dma_add_params *msg;
	struct sof_ipc_reply reply;
	size_t size = struct_size(msg, dma, 1);
	int ret;

	msg = kmalloc(size, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;
	msg->hdr.size = size;
	msg->hdr.cmd = SOF_IPC_GLB_PROBE | SOF_IPC_PROBE_INIT;
	msg->num_elems = 1;
	msg->dma[0].stream_tag = stream_tag;
	msg->dma[0].dma_buffer_size = buffer_size;

	ret = sof_ipc_tx_message(sdev->ipc, msg->hdr.cmd, msg, msg->hdr.size,
			&reply, sizeof(reply));
	kfree(msg);
	return ret;
}

/**
 * sof_ipc_probe_deinit - cleanup after data probing
 * @sdev:	SOF sound device
 *
 * Host sends DEINIT request to free previously initialized probe
 * on DSP side once it is no longer needed. DEINIT only when there
 * are no probes connected and with all injectors detached.
 */
static int sof_ipc_probe_deinit(struct snd_sof_dev *sdev)
{
	struct sof_ipc_cmd_hdr msg;
	struct sof_ipc_reply reply;

	msg.size = sizeof(msg);
	msg.cmd = SOF_IPC_GLB_PROBE | SOF_IPC_PROBE_DEINIT;

	return sof_ipc_tx_message(sdev->ipc, msg.cmd, &msg, msg.size,
			&reply, sizeof(reply));
}

static int sof_ipc_probe_info(struct snd_sof_dev *sdev, unsigned int cmd,
			      void **params, size_t *num_params)
{
	struct sof_ipc_probe_info_params msg = {{{0}}};
	struct sof_ipc_probe_info_params *reply;
	size_t bytes;
	int ret;

	*params = NULL;
	*num_params = 0;

	reply = kzalloc(SOF_IPC_MSG_MAX_SIZE, GFP_KERNEL);
	if (!reply)
		return -ENOMEM;
	msg.rhdr.hdr.size = sizeof(msg);
	msg.rhdr.hdr.cmd = SOF_IPC_GLB_PROBE | cmd;

	ret = sof_ipc_tx_message(sdev->ipc, msg.rhdr.hdr.cmd, &msg,
			msg.rhdr.hdr.size, reply, SOF_IPC_MSG_MAX_SIZE);
	if (ret < 0 || reply->rhdr.error < 0)
		goto exit;

	if (!reply->num_elems)
		goto exit;

	if (cmd == SOF_IPC_PROBE_DMA_INFO)
		bytes = sizeof(reply->dma[0]);
	else
		bytes = sizeof(reply->desc[0]);
	bytes *= reply->num_elems;
	*params = kmemdup(&reply->dma[0], bytes, GFP_KERNEL);
	if (!*params) {
		ret = -ENOMEM;
		goto exit;
	}
	*num_params = reply->num_elems;

exit:
	kfree(reply);
	return ret;
}

/**
 * sof_ipc_probe_points_info - retrieve list of active probe points
 * @sdev:	SOF sound device
 * @desc:	Returned list of active probes
 * @num_desc:	Returned count of active probes
 *
 * Host sends PROBE_POINT_INFO request to obtain list of active probe
 * points, valid for disconnection when given probe is no longer
 * required.
 */
int sof_ipc_probe_points_info(struct snd_sof_dev *sdev,
			      struct sof_probe_point_desc **desc,
			      size_t *num_desc)
{
	return sof_ipc_probe_info(sdev, SOF_IPC_PROBE_POINT_INFO,
				 (void **)desc, num_desc);
}
EXPORT_SYMBOL(sof_ipc_probe_points_info);

/**
 * sof_ipc_probe_points_add - connect specified probes
 * @sdev:	SOF sound device
 * @desc:	List of probe points to connect
 * @num_desc:	Number of elements in @desc
 *
 * Dynamically connects to provided set of endpoints. Immediately
 * after connection is established, host must be prepared to
 * transfer data from or to target stream given the probing purpose.
 *
 * Each probe point should be removed using PROBE_POINT_REMOVE
 * request when no longer needed.
 */
int sof_ipc_probe_points_add(struct snd_sof_dev *sdev,
			     struct sof_probe_point_desc *desc, size_t num_desc)
{
	struct sof_ipc_probe_point_add_params *msg;
	struct sof_ipc_reply reply;
	size_t size = struct_size(msg, desc, num_desc);
	int ret;

	msg = kmalloc(size, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;
	msg->hdr.size = size;
	msg->num_elems = num_desc;
	msg->hdr.cmd = SOF_IPC_GLB_PROBE | SOF_IPC_PROBE_POINT_ADD;
	memcpy(&msg->desc[0], desc, size - sizeof(*msg));

	ret = sof_ipc_tx_message(sdev->ipc, msg->hdr.cmd, msg, msg->hdr.size,
			&reply, sizeof(reply));
	kfree(msg);
	return ret;
}
EXPORT_SYMBOL(sof_ipc_probe_points_add);

/**
 * sof_ipc_probe_points_remove - disconnect specified probes
 * @sdev:		SOF sound device
 * @buffer_id:		List of probe points to disconnect
 * @num_buffer_id:	Number of elements in @desc
 *
 * Removes previously connected probes from list of active probe
 * points and frees all resources on DSP side.
 */
int sof_ipc_probe_points_remove(struct snd_sof_dev *sdev,
				unsigned int *buffer_id, size_t num_buffer_id)
{
	struct sof_ipc_probe_point_remove_params *msg;
	struct sof_ipc_reply reply;
	size_t size = struct_size(msg, buffer_id, num_buffer_id);
	int ret;

	msg = kmalloc(size, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;
	msg->hdr.size = size;
	msg->num_elems = num_buffer_id;
	msg->hdr.cmd = SOF_IPC_GLB_PROBE | SOF_IPC_PROBE_POINT_REMOVE;
	memcpy(&msg->buffer_id[0], buffer_id, size - sizeof(*msg));

	ret = sof_ipc_tx_message(sdev->ipc, msg->hdr.cmd, msg, msg->hdr.size,
			&reply, sizeof(reply));
	kfree(msg);
	return ret;
}
EXPORT_SYMBOL(sof_ipc_probe_points_remove);

static int sof_probe_compr_startup(struct snd_compr_stream *cstream,
				   struct snd_soc_dai *dai)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(dai->component);
	int ret;

	ret = snd_sof_probe_compr_assign(sdev, cstream, dai);
	if (ret < 0) {
		dev_err(dai->dev, "Failed to assign probe stream: %d\n", ret);
		return ret;
	}

	sdev->extractor_stream_tag = ret;
	return 0;
}

static int sof_probe_compr_shutdown(struct snd_compr_stream *cstream,
				    struct snd_soc_dai *dai)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(dai->component);
	struct sof_probe_point_desc *desc;
	size_t num_desc;
	int i, ret;

	/* disconnect all probe points */
	ret = sof_ipc_probe_points_info(sdev, &desc, &num_desc);
	if (ret < 0) {
		dev_err(dai->dev, "Failed to get probe points: %d\n", ret);
		goto exit;
	}

	for (i = 0; i < num_desc; i++)
		sof_ipc_probe_points_remove(sdev, &desc[i].buffer_id, 1);
	kfree(desc);

exit:
	ret = sof_ipc_probe_deinit(sdev);
	if (ret < 0)
		dev_err(dai->dev, "Failed to deinit probe: %d\n", ret);

	sdev->extractor_stream_tag = SOF_PROBE_INVALID_NODE_ID;
	snd_compr_free_pages(cstream);

	return snd_sof_probe_compr_free(sdev, cstream, dai);
}

static int sof_probe_compr_set_params(struct snd_compr_stream *cstream,
				      struct snd_compr_params *params,
				      struct snd_soc_dai *dai)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(dai->component);
	struct snd_compr_runtime *rtd = cstream->runtime;
	int ret;

	cstream->dma_buffer.dev.type = SNDRV_DMA_TYPE_DEV_SG;
	cstream->dma_buffer.dev.dev = sdev->dev;
	ret = snd_compr_malloc_pages(cstream, rtd->buffer_size);
	if (ret < 0)
		return ret;

	ret = snd_sof_probe_compr_set_params(sdev, cstream, params, dai);
	if (ret < 0)
		return ret;

	ret = sof_ipc_probe_init(sdev, sdev->extractor_stream_tag,
				 rtd->dma_bytes);
	if (ret < 0) {
		dev_err(dai->dev, "Failed to init probe: %d\n", ret);
		return ret;
	}

	return 0;
}

static int sof_probe_compr_trigger(struct snd_compr_stream *cstream, int cmd,
				   struct snd_soc_dai *dai)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(dai->component);

	return snd_sof_probe_compr_trigger(sdev, cstream, cmd, dai);
}

static int sof_probe_compr_pointer(struct snd_compr_stream *cstream,
				   struct snd_compr_tstamp *tstamp,
				   struct snd_soc_dai *dai)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(dai->component);

	return snd_sof_probe_compr_pointer(sdev, cstream, tstamp, dai);
}

struct snd_soc_cdai_ops sof_probe_compr_ops = {
	.startup	= sof_probe_compr_startup,
	.shutdown	= sof_probe_compr_shutdown,
	.set_params	= sof_probe_compr_set_params,
	.trigger	= sof_probe_compr_trigger,
	.pointer	= sof_probe_compr_pointer,
};
EXPORT_SYMBOL(sof_probe_compr_ops);

static int sof_probe_compr_copy(struct snd_soc_component *component,
				struct snd_compr_stream *cstream,
				char __user *buf, size_t count)
{
	struct snd_compr_runtime *rtd = cstream->runtime;
	unsigned int offset, n;
	void *ptr;
	int ret;

	if (count > rtd->buffer_size)
		count = rtd->buffer_size;

	div_u64_rem(rtd->total_bytes_transferred, rtd->buffer_size, &offset);
	ptr = rtd->dma_area + offset;
	n = rtd->buffer_size - offset;

	if (count < n) {
		ret = copy_to_user(buf, ptr, count);
	} else {
		ret = copy_to_user(buf, ptr, n);
		ret += copy_to_user(buf + n, rtd->dma_area, count - n);
	}

	if (ret)
		return count - ret;
	return count;
}

const struct snd_compress_ops sof_probe_compressed_ops = {
	.copy		= sof_probe_compr_copy,
};
EXPORT_SYMBOL(sof_probe_compressed_ops);
