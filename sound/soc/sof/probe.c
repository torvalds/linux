// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2019-2020 Intel Corporation. All rights reserved.
//
// Author: Cezary Rojewski <cezary.rojewski@intel.com>
//

#include "sof-priv.h"
#include "probe.h"

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
int sof_ipc_probe_init(struct snd_sof_dev *sdev,
		u32 stream_tag, size_t buffer_size)
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
EXPORT_SYMBOL(sof_ipc_probe_init);

/**
 * sof_ipc_probe_deinit - cleanup after data probing
 * @sdev:	SOF sound device
 *
 * Host sends DEINIT request to free previously initialized probe
 * on DSP side once it is no longer needed. DEINIT only when there
 * are no probes connected and with all injectors detached.
 */
int sof_ipc_probe_deinit(struct snd_sof_dev *sdev)
{
	struct sof_ipc_cmd_hdr msg;
	struct sof_ipc_reply reply;

	msg.size = sizeof(msg);
	msg.cmd = SOF_IPC_GLB_PROBE | SOF_IPC_PROBE_DEINIT;

	return sof_ipc_tx_message(sdev->ipc, msg.cmd, &msg, msg.size,
			&reply, sizeof(reply));
}
EXPORT_SYMBOL(sof_ipc_probe_deinit);

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
 * sof_ipc_probe_dma_info - retrieve list of active injection dmas
 * @sdev:	SOF sound device
 * @dma:	Returned list of active dmas
 * @num_dma:	Returned count of active dmas
 *
 * Host sends DMA_INFO request to obtain list of injection dmas it
 * can use to transfer data over with.
 *
 * Note that list contains only injection dmas as there is only one
 * extractor (dma) and it is always assigned on probing init.
 * DSP knows exactly where data from extraction probes is going to,
 * which is not the case for injection where multiple streams
 * could be engaged.
 */
int sof_ipc_probe_dma_info(struct snd_sof_dev *sdev,
		struct sof_probe_dma **dma, size_t *num_dma)
{
	return sof_ipc_probe_info(sdev, SOF_IPC_PROBE_DMA_INFO,
			(void **)dma, num_dma);
}
EXPORT_SYMBOL(sof_ipc_probe_dma_info);

/**
 * sof_ipc_probe_dma_add - attach to specified dmas
 * @sdev:	SOF sound device
 * @dma:	List of streams (dmas) to attach to
 * @num_dma:	Number of elements in @dma
 *
 * Contrary to extraction, injection streams are never assigned
 * on init. Before attempting any data injection, host is responsible
 * for specifying streams which will be later used to transfer data
 * to connected probe points.
 */
int sof_ipc_probe_dma_add(struct snd_sof_dev *sdev,
		struct sof_probe_dma *dma, size_t num_dma)
{
	struct sof_ipc_probe_dma_add_params *msg;
	struct sof_ipc_reply reply;
	size_t size = struct_size(msg, dma, num_dma);
	int ret;

	msg = kmalloc(size, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;
	msg->hdr.size = size;
	msg->num_elems = num_dma;
	msg->hdr.cmd = SOF_IPC_GLB_PROBE | SOF_IPC_PROBE_DMA_ADD;
	memcpy(&msg->dma[0], dma, size - sizeof(*msg));

	ret = sof_ipc_tx_message(sdev->ipc, msg->hdr.cmd, msg, msg->hdr.size,
			&reply, sizeof(reply));
	kfree(msg);
	return ret;
}
EXPORT_SYMBOL(sof_ipc_probe_dma_add);

/**
 * sof_ipc_probe_dma_remove - detach from specified dmas
 * @sdev:		SOF sound device
 * @stream_tag:		List of stream tags to detach from
 * @num_stream_tag:	Number of elements in @stream_tag
 *
 * Host sends DMA_REMOVE request to free previously attached stream
 * from being occupied for injection. Each detach operation should
 * match equivalent DMA_ADD. Detach only when all probes tied to
 * given stream have been disconnected.
 */
int sof_ipc_probe_dma_remove(struct snd_sof_dev *sdev,
		unsigned int *stream_tag, size_t num_stream_tag)
{
	struct sof_ipc_probe_dma_remove_params *msg;
	struct sof_ipc_reply reply;
	size_t size = struct_size(msg, stream_tag, num_stream_tag);
	int ret;

	msg = kmalloc(size, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;
	msg->hdr.size = size;
	msg->num_elems = num_stream_tag;
	msg->hdr.cmd = SOF_IPC_GLB_PROBE | SOF_IPC_PROBE_DMA_REMOVE;
	memcpy(&msg->stream_tag[0], stream_tag, size - sizeof(*msg));

	ret = sof_ipc_tx_message(sdev->ipc, msg->hdr.cmd, msg, msg->hdr.size,
			&reply, sizeof(reply));
	kfree(msg);
	return ret;
}
EXPORT_SYMBOL(sof_ipc_probe_dma_remove);

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
		struct sof_probe_point_desc **desc, size_t *num_desc)
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
