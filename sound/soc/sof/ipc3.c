// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2021 Intel Corporation. All rights reserved.
//
//

#include <sound/sof/stream.h>
#include <sound/sof/control.h>
#include <trace/events/sof.h>
#include "sof-priv.h"
#include "sof-audio.h"
#include "ipc3-priv.h"
#include "ops.h"

typedef void (*ipc3_rx_callback)(struct snd_sof_dev *sdev, void *msg_buf);

#if IS_ENABLED(CONFIG_SND_SOC_SOF_DEBUG_VERBOSE_IPC)
static void ipc3_log_header(struct device *dev, u8 *text, u32 cmd)
{
	u8 *str;
	u8 *str2 = NULL;
	u32 glb;
	u32 type;
	bool is_sof_ipc_stream_position = false;

	glb = cmd & SOF_GLB_TYPE_MASK;
	type = cmd & SOF_CMD_TYPE_MASK;

	switch (glb) {
	case SOF_IPC_GLB_REPLY:
		str = "GLB_REPLY"; break;
	case SOF_IPC_GLB_COMPOUND:
		str = "GLB_COMPOUND"; break;
	case SOF_IPC_GLB_TPLG_MSG:
		str = "GLB_TPLG_MSG";
		switch (type) {
		case SOF_IPC_TPLG_COMP_NEW:
			str2 = "COMP_NEW"; break;
		case SOF_IPC_TPLG_COMP_FREE:
			str2 = "COMP_FREE"; break;
		case SOF_IPC_TPLG_COMP_CONNECT:
			str2 = "COMP_CONNECT"; break;
		case SOF_IPC_TPLG_PIPE_NEW:
			str2 = "PIPE_NEW"; break;
		case SOF_IPC_TPLG_PIPE_FREE:
			str2 = "PIPE_FREE"; break;
		case SOF_IPC_TPLG_PIPE_CONNECT:
			str2 = "PIPE_CONNECT"; break;
		case SOF_IPC_TPLG_PIPE_COMPLETE:
			str2 = "PIPE_COMPLETE"; break;
		case SOF_IPC_TPLG_BUFFER_NEW:
			str2 = "BUFFER_NEW"; break;
		case SOF_IPC_TPLG_BUFFER_FREE:
			str2 = "BUFFER_FREE"; break;
		default:
			str2 = "unknown type"; break;
		}
		break;
	case SOF_IPC_GLB_PM_MSG:
		str = "GLB_PM_MSG";
		switch (type) {
		case SOF_IPC_PM_CTX_SAVE:
			str2 = "CTX_SAVE"; break;
		case SOF_IPC_PM_CTX_RESTORE:
			str2 = "CTX_RESTORE"; break;
		case SOF_IPC_PM_CTX_SIZE:
			str2 = "CTX_SIZE"; break;
		case SOF_IPC_PM_CLK_SET:
			str2 = "CLK_SET"; break;
		case SOF_IPC_PM_CLK_GET:
			str2 = "CLK_GET"; break;
		case SOF_IPC_PM_CLK_REQ:
			str2 = "CLK_REQ"; break;
		case SOF_IPC_PM_CORE_ENABLE:
			str2 = "CORE_ENABLE"; break;
		case SOF_IPC_PM_GATE:
			str2 = "GATE"; break;
		default:
			str2 = "unknown type"; break;
		}
		break;
	case SOF_IPC_GLB_COMP_MSG:
		str = "GLB_COMP_MSG";
		switch (type) {
		case SOF_IPC_COMP_SET_VALUE:
			str2 = "SET_VALUE"; break;
		case SOF_IPC_COMP_GET_VALUE:
			str2 = "GET_VALUE"; break;
		case SOF_IPC_COMP_SET_DATA:
			str2 = "SET_DATA"; break;
		case SOF_IPC_COMP_GET_DATA:
			str2 = "GET_DATA"; break;
		default:
			str2 = "unknown type"; break;
		}
		break;
	case SOF_IPC_GLB_STREAM_MSG:
		str = "GLB_STREAM_MSG";
		switch (type) {
		case SOF_IPC_STREAM_PCM_PARAMS:
			str2 = "PCM_PARAMS"; break;
		case SOF_IPC_STREAM_PCM_PARAMS_REPLY:
			str2 = "PCM_REPLY"; break;
		case SOF_IPC_STREAM_PCM_FREE:
			str2 = "PCM_FREE"; break;
		case SOF_IPC_STREAM_TRIG_START:
			str2 = "TRIG_START"; break;
		case SOF_IPC_STREAM_TRIG_STOP:
			str2 = "TRIG_STOP"; break;
		case SOF_IPC_STREAM_TRIG_PAUSE:
			str2 = "TRIG_PAUSE"; break;
		case SOF_IPC_STREAM_TRIG_RELEASE:
			str2 = "TRIG_RELEASE"; break;
		case SOF_IPC_STREAM_TRIG_DRAIN:
			str2 = "TRIG_DRAIN"; break;
		case SOF_IPC_STREAM_TRIG_XRUN:
			str2 = "TRIG_XRUN"; break;
		case SOF_IPC_STREAM_POSITION:
			is_sof_ipc_stream_position = true;
			str2 = "POSITION"; break;
		case SOF_IPC_STREAM_VORBIS_PARAMS:
			str2 = "VORBIS_PARAMS"; break;
		case SOF_IPC_STREAM_VORBIS_FREE:
			str2 = "VORBIS_FREE"; break;
		default:
			str2 = "unknown type"; break;
		}
		break;
	case SOF_IPC_FW_READY:
		str = "FW_READY"; break;
	case SOF_IPC_GLB_DAI_MSG:
		str = "GLB_DAI_MSG";
		switch (type) {
		case SOF_IPC_DAI_CONFIG:
			str2 = "CONFIG"; break;
		case SOF_IPC_DAI_LOOPBACK:
			str2 = "LOOPBACK"; break;
		default:
			str2 = "unknown type"; break;
		}
		break;
	case SOF_IPC_GLB_TRACE_MSG:
		str = "GLB_TRACE_MSG";
		switch (type) {
		case SOF_IPC_TRACE_DMA_PARAMS:
			str2 = "DMA_PARAMS"; break;
		case SOF_IPC_TRACE_DMA_POSITION:
			if (!sof_debug_check_flag(SOF_DBG_PRINT_DMA_POSITION_UPDATE_LOGS))
				return;
			str2 = "DMA_POSITION"; break;
		case SOF_IPC_TRACE_DMA_PARAMS_EXT:
			str2 = "DMA_PARAMS_EXT"; break;
		case SOF_IPC_TRACE_FILTER_UPDATE:
			str2 = "FILTER_UPDATE"; break;
		case SOF_IPC_TRACE_DMA_FREE:
			str2 = "DMA_FREE"; break;
		default:
			str2 = "unknown type"; break;
		}
		break;
	case SOF_IPC_GLB_TEST_MSG:
		str = "GLB_TEST_MSG";
		switch (type) {
		case SOF_IPC_TEST_IPC_FLOOD:
			str2 = "IPC_FLOOD"; break;
		default:
			str2 = "unknown type"; break;
		}
		break;
	case SOF_IPC_GLB_DEBUG:
		str = "GLB_DEBUG";
		switch (type) {
		case SOF_IPC_DEBUG_MEM_USAGE:
			str2 = "MEM_USAGE"; break;
		default:
			str2 = "unknown type"; break;
		}
		break;
	case SOF_IPC_GLB_PROBE:
		str = "GLB_PROBE";
		switch (type) {
		case SOF_IPC_PROBE_INIT:
			str2 = "INIT"; break;
		case SOF_IPC_PROBE_DEINIT:
			str2 = "DEINIT"; break;
		case SOF_IPC_PROBE_DMA_ADD:
			str2 = "DMA_ADD"; break;
		case SOF_IPC_PROBE_DMA_INFO:
			str2 = "DMA_INFO"; break;
		case SOF_IPC_PROBE_DMA_REMOVE:
			str2 = "DMA_REMOVE"; break;
		case SOF_IPC_PROBE_POINT_ADD:
			str2 = "POINT_ADD"; break;
		case SOF_IPC_PROBE_POINT_INFO:
			str2 = "POINT_INFO"; break;
		case SOF_IPC_PROBE_POINT_REMOVE:
			str2 = "POINT_REMOVE"; break;
		default:
			str2 = "unknown type"; break;
		}
		break;
	default:
		str = "unknown GLB command"; break;
	}

	if (str2) {
		if (is_sof_ipc_stream_position)
			trace_sof_stream_position_ipc_rx(dev);
		else
			dev_dbg(dev, "%s: 0x%x: %s: %s\n", text, cmd, str, str2);
	} else {
		dev_dbg(dev, "%s: 0x%x: %s\n", text, cmd, str);
	}
}
#else
static inline void ipc3_log_header(struct device *dev, u8 *text, u32 cmd)
{
	if ((cmd & SOF_GLB_TYPE_MASK) != SOF_IPC_GLB_TRACE_MSG)
		dev_dbg(dev, "%s: 0x%x\n", text, cmd);
}
#endif

static void sof_ipc3_dump_payload(struct snd_sof_dev *sdev,
				  void *ipc_data, size_t size)
{
	printk(KERN_DEBUG "Size of payload following the header: %zu\n", size);
	print_hex_dump_debug("Message payload: ", DUMP_PREFIX_OFFSET,
			     16, 4, ipc_data, size, false);
}

static int sof_ipc3_get_reply(struct snd_sof_dev *sdev)
{
	struct snd_sof_ipc_msg *msg = sdev->msg;
	struct sof_ipc_reply *reply;
	int ret = 0;

	/* get the generic reply */
	reply = msg->reply_data;
	snd_sof_dsp_mailbox_read(sdev, sdev->host_box.offset, reply, sizeof(*reply));

	if (reply->error < 0)
		return reply->error;

	if (!reply->hdr.size) {
		/* Reply should always be >= sizeof(struct sof_ipc_reply) */
		if (msg->reply_size)
			dev_err(sdev->dev,
				"empty reply received, expected %zu bytes\n",
				msg->reply_size);
		else
			dev_err(sdev->dev, "empty reply received\n");

		return -EINVAL;
	}

	if (msg->reply_size > 0) {
		if (reply->hdr.size == msg->reply_size) {
			ret = 0;
		} else if (reply->hdr.size < msg->reply_size) {
			dev_dbg(sdev->dev,
				"reply size (%u) is less than expected (%zu)\n",
				reply->hdr.size, msg->reply_size);

			msg->reply_size = reply->hdr.size;
			ret = 0;
		} else {
			dev_err(sdev->dev,
				"reply size (%u) exceeds the buffer size (%zu)\n",
				reply->hdr.size, msg->reply_size);
			ret = -EINVAL;
		}

		/*
		 * get the full message if reply->hdr.size <= msg->reply_size
		 * and the reply->hdr.size > sizeof(struct sof_ipc_reply)
		 */
		if (!ret && msg->reply_size > sizeof(*reply))
			snd_sof_dsp_mailbox_read(sdev, sdev->host_box.offset,
						 msg->reply_data, msg->reply_size);
	}

	return ret;
}

/* wait for IPC message reply */
static int ipc3_wait_tx_done(struct snd_sof_ipc *ipc, void *reply_data)
{
	struct snd_sof_ipc_msg *msg = &ipc->msg;
	struct sof_ipc_cmd_hdr *hdr = msg->msg_data;
	struct snd_sof_dev *sdev = ipc->sdev;
	int ret;

	/* wait for DSP IPC completion */
	ret = wait_event_timeout(msg->waitq, msg->ipc_complete,
				 msecs_to_jiffies(sdev->ipc_timeout));

	if (ret == 0) {
		dev_err(sdev->dev,
			"ipc tx timed out for %#x (msg/reply size: %d/%zu)\n",
			hdr->cmd, hdr->size, msg->reply_size);
		snd_sof_handle_fw_exception(ipc->sdev, "IPC timeout");
		ret = -ETIMEDOUT;
	} else {
		ret = msg->reply_error;
		if (ret < 0) {
			dev_err(sdev->dev,
				"ipc tx error for %#x (msg/reply size: %d/%zu): %d\n",
				hdr->cmd, hdr->size, msg->reply_size, ret);
		} else {
			if (sof_debug_check_flag(SOF_DBG_PRINT_IPC_SUCCESS_LOGS))
				ipc3_log_header(sdev->dev, "ipc tx succeeded", hdr->cmd);
			if (reply_data && msg->reply_size)
				/* copy the data returned from DSP */
				memcpy(reply_data, msg->reply_data,
				       msg->reply_size);
		}

		/* re-enable dumps after successful IPC tx */
		if (sdev->ipc_dump_printed) {
			sdev->dbg_dump_printed = false;
			sdev->ipc_dump_printed = false;
		}
	}

	return ret;
}

/* send IPC message from host to DSP */
static int ipc3_tx_msg_unlocked(struct snd_sof_ipc *ipc,
				void *msg_data, size_t msg_bytes,
				void *reply_data, size_t reply_bytes)
{
	struct sof_ipc_cmd_hdr *hdr = msg_data;
	struct snd_sof_dev *sdev = ipc->sdev;
	int ret;

	ipc3_log_header(sdev->dev, "ipc tx", hdr->cmd);

	ret = sof_ipc_send_msg(sdev, msg_data, msg_bytes, reply_bytes);

	if (ret) {
		dev_err_ratelimited(sdev->dev,
				    "%s: ipc message send for %#x failed: %d\n",
				    __func__, hdr->cmd, ret);
		return ret;
	}

	/* now wait for completion */
	return ipc3_wait_tx_done(ipc, reply_data);
}

static int sof_ipc3_tx_msg(struct snd_sof_dev *sdev, void *msg_data, size_t msg_bytes,
			   void *reply_data, size_t reply_bytes, bool no_pm)
{
	struct snd_sof_ipc *ipc = sdev->ipc;
	int ret;

	if (!msg_data || msg_bytes < sizeof(struct sof_ipc_cmd_hdr)) {
		dev_err_ratelimited(sdev->dev, "No IPC message to send\n");
		return -EINVAL;
	}

	if (!no_pm) {
		const struct sof_dsp_power_state target_state = {
			.state = SOF_DSP_PM_D0,
		};

		/* ensure the DSP is in D0 before sending a new IPC */
		ret = snd_sof_dsp_set_power_state(sdev, &target_state);
		if (ret < 0) {
			dev_err(sdev->dev, "%s: resuming DSP failed: %d\n",
				__func__, ret);
			return ret;
		}
	}

	/* Serialise IPC TX */
	mutex_lock(&ipc->tx_mutex);

	ret = ipc3_tx_msg_unlocked(ipc, msg_data, msg_bytes, reply_data, reply_bytes);

	if (sof_debug_check_flag(SOF_DBG_DUMP_IPC_MESSAGE_PAYLOAD)) {
		size_t payload_bytes, header_bytes;
		char *payload = NULL;

		/* payload is indicated by non zero msg/reply_bytes */
		if (msg_bytes > sizeof(struct sof_ipc_cmd_hdr)) {
			payload = msg_data;

			header_bytes = sizeof(struct sof_ipc_cmd_hdr);
			payload_bytes = msg_bytes - header_bytes;
		} else if (reply_bytes > sizeof(struct sof_ipc_reply)) {
			payload = reply_data;

			header_bytes = sizeof(struct sof_ipc_reply);
			payload_bytes = reply_bytes - header_bytes;
		}

		if (payload) {
			payload += header_bytes;
			sof_ipc3_dump_payload(sdev, payload, payload_bytes);
		}
	}

	mutex_unlock(&ipc->tx_mutex);

	return ret;
}

static int sof_ipc3_set_get_data(struct snd_sof_dev *sdev, void *data, size_t data_bytes,
				 bool set)
{
	size_t msg_bytes, hdr_bytes, payload_size, send_bytes;
	struct sof_ipc_ctrl_data *cdata = data;
	struct sof_ipc_ctrl_data *cdata_chunk;
	struct snd_sof_ipc *ipc = sdev->ipc;
	size_t offset = 0;
	u8 *src, *dst;
	u32 num_msg;
	int ret = 0;
	int i;

	if (!cdata || data_bytes < sizeof(*cdata))
		return -EINVAL;

	if ((cdata->rhdr.hdr.cmd & SOF_GLB_TYPE_MASK) != SOF_IPC_GLB_COMP_MSG) {
		dev_err(sdev->dev, "%s: Not supported message type of %#x\n",
			__func__, cdata->rhdr.hdr.cmd);
		return -EINVAL;
	}

	/* send normal size ipc in one part */
	if (cdata->rhdr.hdr.size <= ipc->max_payload_size)
		return sof_ipc3_tx_msg(sdev, cdata, cdata->rhdr.hdr.size,
				       cdata, cdata->rhdr.hdr.size, false);

	cdata_chunk = kzalloc(ipc->max_payload_size, GFP_KERNEL);
	if (!cdata_chunk)
		return -ENOMEM;

	switch (cdata->type) {
	case SOF_CTRL_TYPE_VALUE_CHAN_GET:
	case SOF_CTRL_TYPE_VALUE_CHAN_SET:
		hdr_bytes = sizeof(struct sof_ipc_ctrl_data);
		if (set) {
			src = (u8 *)cdata->chanv;
			dst = (u8 *)cdata_chunk->chanv;
		} else {
			src = (u8 *)cdata_chunk->chanv;
			dst = (u8 *)cdata->chanv;
		}
		break;
	case SOF_CTRL_TYPE_DATA_GET:
	case SOF_CTRL_TYPE_DATA_SET:
		hdr_bytes = sizeof(struct sof_ipc_ctrl_data) + sizeof(struct sof_abi_hdr);
		if (set) {
			src = (u8 *)cdata->data->data;
			dst = (u8 *)cdata_chunk->data->data;
		} else {
			src = (u8 *)cdata_chunk->data->data;
			dst = (u8 *)cdata->data->data;
		}
		break;
	default:
		kfree(cdata_chunk);
		return -EINVAL;
	}

	msg_bytes = cdata->rhdr.hdr.size - hdr_bytes;
	payload_size = ipc->max_payload_size - hdr_bytes;
	num_msg = DIV_ROUND_UP(msg_bytes, payload_size);

	/* copy the header data */
	memcpy(cdata_chunk, cdata, hdr_bytes);

	/* Serialise IPC TX */
	mutex_lock(&sdev->ipc->tx_mutex);

	/* copy the payload data in a loop */
	for (i = 0; i < num_msg; i++) {
		send_bytes = min(msg_bytes, payload_size);
		cdata_chunk->num_elems = send_bytes;
		cdata_chunk->rhdr.hdr.size = hdr_bytes + send_bytes;
		cdata_chunk->msg_index = i;
		msg_bytes -= send_bytes;
		cdata_chunk->elems_remaining = msg_bytes;

		if (set)
			memcpy(dst, src + offset, send_bytes);

		ret = ipc3_tx_msg_unlocked(sdev->ipc,
					   cdata_chunk, cdata_chunk->rhdr.hdr.size,
					   cdata_chunk, cdata_chunk->rhdr.hdr.size);
		if (ret < 0)
			break;

		if (!set)
			memcpy(dst + offset, src, send_bytes);

		offset += payload_size;
	}

	if (sof_debug_check_flag(SOF_DBG_DUMP_IPC_MESSAGE_PAYLOAD)) {
		size_t header_bytes = sizeof(struct sof_ipc_reply);
		char *payload = (char *)cdata;

		payload += header_bytes;
		sof_ipc3_dump_payload(sdev, payload, data_bytes - header_bytes);
	}

	mutex_unlock(&sdev->ipc->tx_mutex);

	kfree(cdata_chunk);

	return ret;
}

int sof_ipc3_get_ext_windows(struct snd_sof_dev *sdev,
			     const struct sof_ipc_ext_data_hdr *ext_hdr)
{
	const struct sof_ipc_window *w =
		container_of(ext_hdr, struct sof_ipc_window, ext_hdr);

	if (w->num_windows == 0 || w->num_windows > SOF_IPC_MAX_ELEMS)
		return -EINVAL;

	if (sdev->info_window) {
		if (memcmp(sdev->info_window, w, ext_hdr->hdr.size)) {
			dev_err(sdev->dev, "mismatch between window descriptor from extended manifest and mailbox");
			return -EINVAL;
		}
		return 0;
	}

	/* keep a local copy of the data */
	sdev->info_window = devm_kmemdup(sdev->dev, w, ext_hdr->hdr.size, GFP_KERNEL);
	if (!sdev->info_window)
		return -ENOMEM;

	return 0;
}

int sof_ipc3_get_cc_info(struct snd_sof_dev *sdev,
			 const struct sof_ipc_ext_data_hdr *ext_hdr)
{
	int ret;

	const struct sof_ipc_cc_version *cc =
		container_of(ext_hdr, struct sof_ipc_cc_version, ext_hdr);

	if (sdev->cc_version) {
		if (memcmp(sdev->cc_version, cc, cc->ext_hdr.hdr.size)) {
			dev_err(sdev->dev,
				"Receive diverged cc_version descriptions");
			return -EINVAL;
		}
		return 0;
	}

	dev_dbg(sdev->dev,
		"Firmware info: used compiler %s %d:%d:%d%s used optimization flags %s\n",
		cc->name, cc->major, cc->minor, cc->micro, cc->desc, cc->optim);

	/* create read-only cc_version debugfs to store compiler version info */
	/* use local copy of the cc_version to prevent data corruption */
	if (sdev->first_boot) {
		sdev->cc_version = devm_kmemdup(sdev->dev, cc, cc->ext_hdr.hdr.size, GFP_KERNEL);
		if (!sdev->cc_version)
			return -ENOMEM;

		ret = snd_sof_debugfs_buf_item(sdev, sdev->cc_version,
					       cc->ext_hdr.hdr.size,
					       "cc_version", 0444);

		/* errors are only due to memory allocation, not debugfs */
		if (ret < 0) {
			dev_err(sdev->dev, "snd_sof_debugfs_buf_item failed\n");
			return ret;
		}
	}

	return 0;
}

/* parse the extended FW boot data structures from FW boot message */
static int ipc3_fw_parse_ext_data(struct snd_sof_dev *sdev, u32 offset)
{
	struct sof_ipc_ext_data_hdr *ext_hdr;
	void *ext_data;
	int ret = 0;

	ext_data = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!ext_data)
		return -ENOMEM;

	/* get first header */
	snd_sof_dsp_block_read(sdev, SOF_FW_BLK_TYPE_SRAM, offset, ext_data,
			       sizeof(*ext_hdr));
	ext_hdr = ext_data;

	while (ext_hdr->hdr.cmd == SOF_IPC_FW_READY) {
		/* read in ext structure */
		snd_sof_dsp_block_read(sdev, SOF_FW_BLK_TYPE_SRAM,
				       offset + sizeof(*ext_hdr),
				       (void *)((u8 *)ext_data + sizeof(*ext_hdr)),
				       ext_hdr->hdr.size - sizeof(*ext_hdr));

		dev_dbg(sdev->dev, "found ext header type %d size 0x%x\n",
			ext_hdr->type, ext_hdr->hdr.size);

		/* process structure data */
		switch (ext_hdr->type) {
		case SOF_IPC_EXT_WINDOW:
			ret = sof_ipc3_get_ext_windows(sdev, ext_hdr);
			break;
		case SOF_IPC_EXT_CC_INFO:
			ret = sof_ipc3_get_cc_info(sdev, ext_hdr);
			break;
		case SOF_IPC_EXT_UNUSED:
		case SOF_IPC_EXT_PROBE_INFO:
		case SOF_IPC_EXT_USER_ABI_INFO:
			/* They are supported but we don't do anything here */
			break;
		default:
			dev_info(sdev->dev, "unknown ext header type %d size 0x%x\n",
				 ext_hdr->type, ext_hdr->hdr.size);
			ret = 0;
			break;
		}

		if (ret < 0) {
			dev_err(sdev->dev, "Failed to parse ext data type %d\n",
				ext_hdr->type);
			break;
		}

		/* move to next header */
		offset += ext_hdr->hdr.size;
		snd_sof_dsp_block_read(sdev, SOF_FW_BLK_TYPE_SRAM, offset, ext_data,
				       sizeof(*ext_hdr));
		ext_hdr = ext_data;
	}

	kfree(ext_data);
	return ret;
}

static void ipc3_get_windows(struct snd_sof_dev *sdev)
{
	struct sof_ipc_window_elem *elem;
	u32 outbox_offset = 0;
	u32 stream_offset = 0;
	u32 inbox_offset = 0;
	u32 outbox_size = 0;
	u32 stream_size = 0;
	u32 inbox_size = 0;
	u32 debug_size = 0;
	u32 debug_offset = 0;
	int window_offset;
	int i;

	if (!sdev->info_window) {
		dev_err(sdev->dev, "%s: No window info present\n", __func__);
		return;
	}

	for (i = 0; i < sdev->info_window->num_windows; i++) {
		elem = &sdev->info_window->window[i];

		window_offset = snd_sof_dsp_get_window_offset(sdev, elem->id);
		if (window_offset < 0) {
			dev_warn(sdev->dev, "No offset for window %d\n", elem->id);
			continue;
		}

		switch (elem->type) {
		case SOF_IPC_REGION_UPBOX:
			inbox_offset = window_offset + elem->offset;
			inbox_size = elem->size;
			snd_sof_debugfs_add_region_item(sdev, SOF_FW_BLK_TYPE_SRAM,
							inbox_offset,
							elem->size, "inbox",
							SOF_DEBUGFS_ACCESS_D0_ONLY);
			break;
		case SOF_IPC_REGION_DOWNBOX:
			outbox_offset = window_offset + elem->offset;
			outbox_size = elem->size;
			snd_sof_debugfs_add_region_item(sdev, SOF_FW_BLK_TYPE_SRAM,
							outbox_offset,
							elem->size, "outbox",
							SOF_DEBUGFS_ACCESS_D0_ONLY);
			break;
		case SOF_IPC_REGION_TRACE:
			snd_sof_debugfs_add_region_item(sdev, SOF_FW_BLK_TYPE_SRAM,
							window_offset + elem->offset,
							elem->size, "etrace",
							SOF_DEBUGFS_ACCESS_D0_ONLY);
			break;
		case SOF_IPC_REGION_DEBUG:
			debug_offset = window_offset + elem->offset;
			debug_size = elem->size;
			snd_sof_debugfs_add_region_item(sdev, SOF_FW_BLK_TYPE_SRAM,
							window_offset + elem->offset,
							elem->size, "debug",
							SOF_DEBUGFS_ACCESS_D0_ONLY);
			break;
		case SOF_IPC_REGION_STREAM:
			stream_offset = window_offset + elem->offset;
			stream_size = elem->size;
			snd_sof_debugfs_add_region_item(sdev, SOF_FW_BLK_TYPE_SRAM,
							stream_offset,
							elem->size, "stream",
							SOF_DEBUGFS_ACCESS_D0_ONLY);
			break;
		case SOF_IPC_REGION_REGS:
			snd_sof_debugfs_add_region_item(sdev, SOF_FW_BLK_TYPE_SRAM,
							window_offset + elem->offset,
							elem->size, "regs",
							SOF_DEBUGFS_ACCESS_D0_ONLY);
			break;
		case SOF_IPC_REGION_EXCEPTION:
			sdev->dsp_oops_offset = window_offset + elem->offset;
			snd_sof_debugfs_add_region_item(sdev, SOF_FW_BLK_TYPE_SRAM,
							window_offset + elem->offset,
							elem->size, "exception",
							SOF_DEBUGFS_ACCESS_D0_ONLY);
			break;
		default:
			dev_err(sdev->dev, "%s: Illegal window info: %u\n",
				__func__, elem->type);
			return;
		}
	}

	if (outbox_size == 0 || inbox_size == 0) {
		dev_err(sdev->dev, "%s: Illegal mailbox window\n", __func__);
		return;
	}

	sdev->dsp_box.offset = inbox_offset;
	sdev->dsp_box.size = inbox_size;

	sdev->host_box.offset = outbox_offset;
	sdev->host_box.size = outbox_size;

	sdev->stream_box.offset = stream_offset;
	sdev->stream_box.size = stream_size;

	sdev->debug_box.offset = debug_offset;
	sdev->debug_box.size = debug_size;

	dev_dbg(sdev->dev, " mailbox upstream 0x%x - size 0x%x\n",
		inbox_offset, inbox_size);
	dev_dbg(sdev->dev, " mailbox downstream 0x%x - size 0x%x\n",
		outbox_offset, outbox_size);
	dev_dbg(sdev->dev, " stream region 0x%x - size 0x%x\n",
		stream_offset, stream_size);
	dev_dbg(sdev->dev, " debug region 0x%x - size 0x%x\n",
		debug_offset, debug_size);
}

static int ipc3_init_reply_data_buffer(struct snd_sof_dev *sdev)
{
	struct snd_sof_ipc_msg *msg = &sdev->ipc->msg;

	msg->reply_data = devm_kzalloc(sdev->dev, SOF_IPC_MSG_MAX_SIZE, GFP_KERNEL);
	if (!msg->reply_data)
		return -ENOMEM;

	sdev->ipc->max_payload_size = SOF_IPC_MSG_MAX_SIZE;

	return 0;
}

int sof_ipc3_validate_fw_version(struct snd_sof_dev *sdev)
{
	struct sof_ipc_fw_ready *ready = &sdev->fw_ready;
	struct sof_ipc_fw_version *v = &ready->version;

	dev_info(sdev->dev,
		 "Firmware info: version %d:%d:%d-%s\n",  v->major, v->minor,
		 v->micro, v->tag);
	dev_info(sdev->dev,
		 "Firmware: ABI %d:%d:%d Kernel ABI %d:%d:%d\n",
		 SOF_ABI_VERSION_MAJOR(v->abi_version),
		 SOF_ABI_VERSION_MINOR(v->abi_version),
		 SOF_ABI_VERSION_PATCH(v->abi_version),
		 SOF_ABI_MAJOR, SOF_ABI_MINOR, SOF_ABI_PATCH);

	if (SOF_ABI_VERSION_INCOMPATIBLE(SOF_ABI_VERSION, v->abi_version)) {
		dev_err(sdev->dev, "incompatible FW ABI version\n");
		return -EINVAL;
	}

	if (IS_ENABLED(CONFIG_SND_SOC_SOF_STRICT_ABI_CHECKS) &&
	    SOF_ABI_VERSION_MINOR(v->abi_version) > SOF_ABI_MINOR) {
		dev_err(sdev->dev, "FW ABI is more recent than kernel\n");
		return -EINVAL;
	}

	if (ready->flags & SOF_IPC_INFO_BUILD) {
		dev_info(sdev->dev,
			 "Firmware debug build %d on %s-%s - options:\n"
			 " GDB: %s\n"
			 " lock debug: %s\n"
			 " lock vdebug: %s\n",
			 v->build, v->date, v->time,
			 (ready->flags & SOF_IPC_INFO_GDB) ?
				"enabled" : "disabled",
			 (ready->flags & SOF_IPC_INFO_LOCKS) ?
				"enabled" : "disabled",
			 (ready->flags & SOF_IPC_INFO_LOCKSV) ?
				"enabled" : "disabled");
	}

	/* copy the fw_version into debugfs at first boot */
	memcpy(&sdev->fw_version, v, sizeof(*v));

	return 0;
}

static int ipc3_fw_ready(struct snd_sof_dev *sdev, u32 cmd)
{
	struct sof_ipc_fw_ready *fw_ready = &sdev->fw_ready;
	int offset;
	int ret;

	/* mailbox must be on 4k boundary */
	offset = snd_sof_dsp_get_mailbox_offset(sdev);
	if (offset < 0) {
		dev_err(sdev->dev, "%s: no mailbox offset\n", __func__);
		return offset;
	}

	dev_dbg(sdev->dev, "DSP is ready 0x%8.8x offset 0x%x\n", cmd, offset);

	/* no need to re-check version/ABI for subsequent boots */
	if (!sdev->first_boot)
		return 0;

	/*
	 * copy data from the DSP FW ready offset
	 * Subsequent error handling is not needed for BLK_TYPE_SRAM
	 */
	ret = snd_sof_dsp_block_read(sdev, SOF_FW_BLK_TYPE_SRAM, offset, fw_ready,
				     sizeof(*fw_ready));
	if (ret) {
		dev_err(sdev->dev,
			"Unable to read fw_ready, read from TYPE_SRAM failed\n");
		return ret;
	}

	/* make sure ABI version is compatible */
	ret = sof_ipc3_validate_fw_version(sdev);
	if (ret < 0)
		return ret;

	/* now check for extended data */
	ipc3_fw_parse_ext_data(sdev, offset + sizeof(struct sof_ipc_fw_ready));

	ipc3_get_windows(sdev);

	return ipc3_init_reply_data_buffer(sdev);
}

/* IPC stream position. */
static void ipc3_period_elapsed(struct snd_sof_dev *sdev, u32 msg_id)
{
	struct snd_soc_component *scomp = sdev->component;
	struct snd_sof_pcm_stream *stream;
	struct sof_ipc_stream_posn posn;
	struct snd_sof_pcm *spcm;
	int direction, ret;

	spcm = snd_sof_find_spcm_comp(scomp, msg_id, &direction);
	if (!spcm) {
		dev_err(sdev->dev, "period elapsed for unknown stream, msg_id %d\n",
			msg_id);
		return;
	}

	stream = &spcm->stream[direction];
	ret = snd_sof_ipc_msg_data(sdev, stream, &posn, sizeof(posn));
	if (ret < 0) {
		dev_warn(sdev->dev, "failed to read stream position: %d\n", ret);
		return;
	}

	trace_sof_ipc3_period_elapsed_position(sdev, &posn);

	memcpy(&stream->posn, &posn, sizeof(posn));

	if (spcm->pcm.compress)
		snd_sof_compr_fragment_elapsed(stream->cstream);
	else if (stream->substream->runtime &&
		 !stream->substream->runtime->no_period_wakeup)
		/* only inform ALSA for period_wakeup mode */
		snd_sof_pcm_period_elapsed(stream->substream);
}

/* DSP notifies host of an XRUN within FW */
static void ipc3_xrun(struct snd_sof_dev *sdev, u32 msg_id)
{
	struct snd_soc_component *scomp = sdev->component;
	struct snd_sof_pcm_stream *stream;
	struct sof_ipc_stream_posn posn;
	struct snd_sof_pcm *spcm;
	int direction, ret;

	spcm = snd_sof_find_spcm_comp(scomp, msg_id, &direction);
	if (!spcm) {
		dev_err(sdev->dev, "XRUN for unknown stream, msg_id %d\n",
			msg_id);
		return;
	}

	stream = &spcm->stream[direction];
	ret = snd_sof_ipc_msg_data(sdev, stream, &posn, sizeof(posn));
	if (ret < 0) {
		dev_warn(sdev->dev, "failed to read overrun position: %d\n", ret);
		return;
	}

	dev_dbg(sdev->dev,  "posn XRUN: host %llx comp %d size %d\n",
		posn.host_posn, posn.xrun_comp_id, posn.xrun_size);

#if defined(CONFIG_SND_SOC_SOF_DEBUG_XRUN_STOP)
	/* stop PCM on XRUN - used for pipeline debug */
	memcpy(&stream->posn, &posn, sizeof(posn));
	snd_pcm_stop_xrun(stream->substream);
#endif
}

/* stream notifications from firmware */
static void ipc3_stream_message(struct snd_sof_dev *sdev, void *msg_buf)
{
	struct sof_ipc_cmd_hdr *hdr = msg_buf;
	u32 msg_type = hdr->cmd & SOF_CMD_TYPE_MASK;
	u32 msg_id = SOF_IPC_MESSAGE_ID(hdr->cmd);

	switch (msg_type) {
	case SOF_IPC_STREAM_POSITION:
		ipc3_period_elapsed(sdev, msg_id);
		break;
	case SOF_IPC_STREAM_TRIG_XRUN:
		ipc3_xrun(sdev, msg_id);
		break;
	default:
		dev_err(sdev->dev, "unhandled stream message %#x\n",
			msg_id);
		break;
	}
}

/* component notifications from firmware */
static void ipc3_comp_notification(struct snd_sof_dev *sdev, void *msg_buf)
{
	const struct sof_ipc_tplg_ops *tplg_ops = sdev->ipc->ops->tplg;
	struct sof_ipc_cmd_hdr *hdr = msg_buf;
	u32 msg_type = hdr->cmd & SOF_CMD_TYPE_MASK;

	switch (msg_type) {
	case SOF_IPC_COMP_GET_VALUE:
	case SOF_IPC_COMP_GET_DATA:
		break;
	default:
		dev_err(sdev->dev, "unhandled component message %#x\n", msg_type);
		return;
	}

	if (tplg_ops->control->update)
		tplg_ops->control->update(sdev, msg_buf);
}

static void ipc3_trace_message(struct snd_sof_dev *sdev, void *msg_buf)
{
	struct sof_ipc_cmd_hdr *hdr = msg_buf;
	u32 msg_type = hdr->cmd & SOF_CMD_TYPE_MASK;

	switch (msg_type) {
	case SOF_IPC_TRACE_DMA_POSITION:
		ipc3_dtrace_posn_update(sdev, msg_buf);
		break;
	default:
		dev_err(sdev->dev, "unhandled trace message %#x\n", msg_type);
		break;
	}
}

void sof_ipc3_do_rx_work(struct snd_sof_dev *sdev, struct sof_ipc_cmd_hdr *hdr, void *msg_buf)
{
	ipc3_rx_callback rx_callback = NULL;
	u32 cmd;
	int err;

	ipc3_log_header(sdev->dev, "ipc rx", hdr->cmd);

	if (hdr->size < sizeof(*hdr) || hdr->size > SOF_IPC_MSG_MAX_SIZE) {
		dev_err(sdev->dev, "The received message size is invalid: %u\n",
			hdr->size);
		return;
	}

	cmd = hdr->cmd & SOF_GLB_TYPE_MASK;

	/* check message type */
	switch (cmd) {
	case SOF_IPC_GLB_REPLY:
		dev_err(sdev->dev, "ipc reply unknown\n");
		break;
	case SOF_IPC_FW_READY:
		/* check for FW boot completion */
		if (sdev->fw_state == SOF_FW_BOOT_IN_PROGRESS) {
			err = ipc3_fw_ready(sdev, cmd);
			if (err < 0)
				sof_set_fw_state(sdev, SOF_FW_BOOT_READY_FAILED);
			else
				sof_set_fw_state(sdev, SOF_FW_BOOT_READY_OK);

			/* wake up firmware loader */
			wake_up(&sdev->boot_wait);
		}
		break;
	case SOF_IPC_GLB_COMPOUND:
	case SOF_IPC_GLB_TPLG_MSG:
	case SOF_IPC_GLB_PM_MSG:
		break;
	case SOF_IPC_GLB_COMP_MSG:
		rx_callback = ipc3_comp_notification;
		break;
	case SOF_IPC_GLB_STREAM_MSG:
		rx_callback = ipc3_stream_message;
		break;
	case SOF_IPC_GLB_TRACE_MSG:
		rx_callback = ipc3_trace_message;
		break;
	default:
		dev_err(sdev->dev, "%s: Unknown DSP message: 0x%x\n", __func__, cmd);
		break;
	}

	/* Call local handler for the message */
	if (rx_callback)
		rx_callback(sdev, msg_buf);

	/* Notify registered clients */
	sof_client_ipc_rx_dispatcher(sdev, msg_buf);

	ipc3_log_header(sdev->dev, "ipc rx done", hdr->cmd);
}
EXPORT_SYMBOL(sof_ipc3_do_rx_work);

/* DSP firmware has sent host a message  */
static void sof_ipc3_rx_msg(struct snd_sof_dev *sdev)
{
	struct sof_ipc_cmd_hdr hdr;
	void *msg_buf;
	int err;

	/* read back header */
	err = snd_sof_ipc_msg_data(sdev, NULL, &hdr, sizeof(hdr));
	if (err < 0) {
		dev_warn(sdev->dev, "failed to read IPC header: %d\n", err);
		return;
	}

	if (hdr.size < sizeof(hdr) || hdr.size > SOF_IPC_MSG_MAX_SIZE) {
		dev_err(sdev->dev, "The received message size is invalid\n");
		return;
	}

	/* read the full message */
	msg_buf = kmalloc(hdr.size, GFP_KERNEL);
	if (!msg_buf)
		return;

	err = snd_sof_ipc_msg_data(sdev, NULL, msg_buf, hdr.size);
	if (err < 0) {
		dev_err(sdev->dev, "%s: Failed to read message: %d\n", __func__, err);
		kfree(msg_buf);
		return;
	}

	sof_ipc3_do_rx_work(sdev, &hdr, msg_buf);

	kfree(msg_buf);
}

static int sof_ipc3_set_core_state(struct snd_sof_dev *sdev, int core_idx, bool on)
{
	struct sof_ipc_pm_core_config core_cfg = {
		.hdr.size = sizeof(core_cfg),
		.hdr.cmd = SOF_IPC_GLB_PM_MSG | SOF_IPC_PM_CORE_ENABLE,
	};

	if (on)
		core_cfg.enable_mask = sdev->enabled_cores_mask | BIT(core_idx);
	else
		core_cfg.enable_mask = sdev->enabled_cores_mask & ~BIT(core_idx);

	return sof_ipc3_tx_msg(sdev, &core_cfg, sizeof(core_cfg), NULL, 0, false);
}

static int sof_ipc3_ctx_ipc(struct snd_sof_dev *sdev, int cmd)
{
	struct sof_ipc_pm_ctx pm_ctx = {
		.hdr.size = sizeof(pm_ctx),
		.hdr.cmd = SOF_IPC_GLB_PM_MSG | cmd,
	};

	/* send ctx save ipc to dsp */
	return sof_ipc3_tx_msg(sdev, &pm_ctx, sizeof(pm_ctx), NULL, 0, false);
}

static int sof_ipc3_ctx_save(struct snd_sof_dev *sdev)
{
	return sof_ipc3_ctx_ipc(sdev, SOF_IPC_PM_CTX_SAVE);
}

static int sof_ipc3_ctx_restore(struct snd_sof_dev *sdev)
{
	return sof_ipc3_ctx_ipc(sdev, SOF_IPC_PM_CTX_RESTORE);
}

static int sof_ipc3_set_pm_gate(struct snd_sof_dev *sdev, u32 flags)
{
	struct sof_ipc_pm_gate pm_gate;

	memset(&pm_gate, 0, sizeof(pm_gate));

	/* configure pm_gate ipc message */
	pm_gate.hdr.size = sizeof(pm_gate);
	pm_gate.hdr.cmd = SOF_IPC_GLB_PM_MSG | SOF_IPC_PM_GATE;
	pm_gate.flags = flags;

	/* send pm_gate ipc to dsp */
	return sof_ipc_tx_message_no_pm_no_reply(sdev->ipc, &pm_gate, sizeof(pm_gate));
}

static const struct sof_ipc_pm_ops ipc3_pm_ops = {
	.ctx_save = sof_ipc3_ctx_save,
	.ctx_restore = sof_ipc3_ctx_restore,
	.set_core_state = sof_ipc3_set_core_state,
	.set_pm_gate = sof_ipc3_set_pm_gate,
};

const struct sof_ipc_ops ipc3_ops = {
	.tplg = &ipc3_tplg_ops,
	.pm = &ipc3_pm_ops,
	.pcm = &ipc3_pcm_ops,
	.fw_loader = &ipc3_loader_ops,
	.fw_tracing = &ipc3_dtrace_ops,

	.tx_msg = sof_ipc3_tx_msg,
	.rx_msg = sof_ipc3_rx_msg,
	.set_get_data = sof_ipc3_set_get_data,
	.get_reply = sof_ipc3_get_reply,
};
