// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
//
// Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
//
// Generic IPC layer that can work over MMIO and SPI/I2C. PHY layer provided
// by platform driver code.
//

#include <linux/mutex.h>
#include <linux/types.h>

#include "sof-priv.h"
#include "sof-audio.h"
#include "ops.h"
#include "ipc3-ops.h"

typedef void (*ipc_rx_callback)(struct snd_sof_dev *sdev, void *msg_buf);

static void ipc_trace_message(struct snd_sof_dev *sdev, void *msg_buf);
static void ipc_stream_message(struct snd_sof_dev *sdev, void *msg_buf);

#if IS_ENABLED(CONFIG_SND_SOC_SOF_DEBUG_VERBOSE_IPC)
static void ipc_log_header(struct device *dev, u8 *text, u32 cmd)
{
	u8 *str;
	u8 *str2 = NULL;
	u32 glb;
	u32 type;
	bool vdbg = false;

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
			vdbg = true;
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
		if (vdbg)
			dev_vdbg(dev, "%s: 0x%x: %s: %s\n", text, cmd, str, str2);
		else
			dev_dbg(dev, "%s: 0x%x: %s: %s\n", text, cmd, str, str2);
	} else {
		dev_dbg(dev, "%s: 0x%x: %s\n", text, cmd, str);
	}
}
#else
static inline void ipc_log_header(struct device *dev, u8 *text, u32 cmd)
{
	if ((cmd & SOF_GLB_TYPE_MASK) != SOF_IPC_GLB_TRACE_MSG)
		dev_dbg(dev, "%s: 0x%x\n", text, cmd);
}
#endif

/**
 * sof_ipc_send_msg - generic function to prepare and send one IPC message
 * @sdev:		pointer to SOF core device struct
 * @msg_data:		pointer to a message to send
 * @msg_bytes:		number of bytes in the message
 * @reply_bytes:	number of bytes available for the reply.
 *			The buffer for the reply data is not passed to this
 *			function, the available size is an information for the
 *			reply handling functions.
 *
 * On success the function returns 0, otherwise negative error number.
 *
 * Note: higher level sdev->ipc->tx_mutex must be held to make sure that
 *	 transfers are synchronized.
 */
int sof_ipc_send_msg(struct snd_sof_dev *sdev, void *msg_data, size_t msg_bytes,
		     size_t reply_bytes)
{
	struct snd_sof_ipc *ipc = sdev->ipc;
	struct snd_sof_ipc_msg *msg;
	int ret;

	if (ipc->disable_ipc_tx || sdev->fw_state != SOF_FW_BOOT_COMPLETE)
		return -ENODEV;

	/*
	 * The spin-lock is needed to protect message objects against other
	 * atomic contexts.
	 */
	spin_lock_irq(&sdev->ipc_lock);

	/* initialise the message */
	msg = &ipc->msg;

	/* attach message data */
	msg->msg_data = msg_data;
	msg->msg_size = msg_bytes;

	msg->reply_size = reply_bytes;
	msg->reply_error = 0;

	sdev->msg = msg;

	ret = snd_sof_dsp_send_msg(sdev, msg);
	/* Next reply that we receive will be related to this message */
	if (!ret)
		msg->ipc_complete = false;

	spin_unlock_irq(&sdev->ipc_lock);

	return ret;
}

/* send IPC message from host to DSP */
int sof_ipc_tx_message(struct snd_sof_ipc *ipc, void *msg_data, size_t msg_bytes,
		       void *reply_data, size_t reply_bytes)
{
	if (msg_bytes > ipc->max_payload_size ||
	    reply_bytes > ipc->max_payload_size)
		return -ENOBUFS;

	return ipc->ops->tx_msg(ipc->sdev, msg_data, msg_bytes, reply_data,
				reply_bytes, false);
}
EXPORT_SYMBOL(sof_ipc_tx_message);

/*
 * send IPC message from host to DSP without modifying the DSP state.
 * This will be used for IPC's that can be handled by the DSP
 * even in a low-power D0 substate.
 */
int sof_ipc_tx_message_no_pm(struct snd_sof_ipc *ipc, void *msg_data, size_t msg_bytes,
			     void *reply_data, size_t reply_bytes)
{
	if (msg_bytes > ipc->max_payload_size ||
	    reply_bytes > ipc->max_payload_size)
		return -ENOBUFS;

	return ipc->ops->tx_msg(ipc->sdev, msg_data, msg_bytes, reply_data,
				reply_bytes, true);
}
EXPORT_SYMBOL(sof_ipc_tx_message_no_pm);

/* Generic helper function to retrieve the reply */
void snd_sof_ipc_get_reply(struct snd_sof_dev *sdev)
{
	/*
	 * Sometimes, there is unexpected reply ipc arriving. The reply
	 * ipc belongs to none of the ipcs sent from driver.
	 * In this case, the driver must ignore the ipc.
	 */
	if (!sdev->msg) {
		dev_warn(sdev->dev, "unexpected ipc interrupt raised!\n");
		return;
	}

	sdev->msg->reply_error = sdev->ipc->ops->get_reply(sdev);
}
EXPORT_SYMBOL(snd_sof_ipc_get_reply);

/* handle reply message from DSP */
void snd_sof_ipc_reply(struct snd_sof_dev *sdev, u32 msg_id)
{
	struct snd_sof_ipc_msg *msg = &sdev->ipc->msg;

	if (msg->ipc_complete) {
		dev_dbg(sdev->dev,
			"no reply expected, received 0x%x, will be ignored",
			msg_id);
		return;
	}

	/* wake up and return the error if we have waiters on this message ? */
	msg->ipc_complete = true;
	wake_up(&msg->waitq);
}
EXPORT_SYMBOL(snd_sof_ipc_reply);

static void ipc_comp_notification(struct snd_sof_dev *sdev, void *msg_buf)
{
	const struct sof_ipc_tplg_ops *tplg_ops = sdev->ipc->ops->tplg;
	struct sof_ipc_cmd_hdr *hdr = msg_buf;
	u32 msg_type = hdr->cmd & SOF_CMD_TYPE_MASK;

	switch (msg_type) {
	case SOF_IPC_COMP_GET_VALUE:
	case SOF_IPC_COMP_GET_DATA:
		break;
	default:
		dev_err(sdev->dev, "error: unhandled component message %#x\n", msg_type);
		return;
	}

	if (tplg_ops->control->update)
		tplg_ops->control->update(sdev, msg_buf);
}

/* DSP firmware has sent host a message  */
void snd_sof_ipc_msgs_rx(struct snd_sof_dev *sdev)
{
	ipc_rx_callback rx_callback = NULL;
	struct sof_ipc_cmd_hdr hdr;
	void *msg_buf;
	u32 cmd;
	int err;

	/* read back header */
	err = snd_sof_ipc_msg_data(sdev, NULL, &hdr, sizeof(hdr));
	if (err < 0) {
		dev_warn(sdev->dev, "failed to read IPC header: %d\n", err);
		return;
	}

	if (hdr.size < sizeof(hdr)) {
		dev_err(sdev->dev, "The received message size is invalid\n");
		return;
	}

	ipc_log_header(sdev->dev, "ipc rx", hdr.cmd);

	cmd = hdr.cmd & SOF_GLB_TYPE_MASK;

	/* check message type */
	switch (cmd) {
	case SOF_IPC_GLB_REPLY:
		dev_err(sdev->dev, "error: ipc reply unknown\n");
		break;
	case SOF_IPC_FW_READY:
		/* check for FW boot completion */
		if (sdev->fw_state == SOF_FW_BOOT_IN_PROGRESS) {
			err = sof_ops(sdev)->fw_ready(sdev, cmd);
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
		rx_callback = ipc_comp_notification;
		break;
	case SOF_IPC_GLB_STREAM_MSG:
		rx_callback = ipc_stream_message;
		break;
	case SOF_IPC_GLB_TRACE_MSG:
		rx_callback = ipc_trace_message;
		break;
	default:
		dev_err(sdev->dev, "%s: Unknown DSP message: 0x%x\n", __func__, cmd);
		break;
	}

	/* read the full message */
	msg_buf = kmalloc(hdr.size, GFP_KERNEL);
	if (!msg_buf)
		return;

	err = snd_sof_ipc_msg_data(sdev, NULL, msg_buf, hdr.size);
	if (err < 0) {
		dev_err(sdev->dev, "%s: Failed to read message: %d\n", __func__, err);
	} else {
		/* Call local handler for the message */
		if (rx_callback)
			rx_callback(sdev, msg_buf);

		/* Notify registered clients */
		sof_client_ipc_rx_dispatcher(sdev, msg_buf);
	}

	kfree(msg_buf);

	ipc_log_header(sdev->dev, "ipc rx done", hdr.cmd);
}
EXPORT_SYMBOL(snd_sof_ipc_msgs_rx);

/*
 * IPC trace mechanism.
 */

static void ipc_trace_message(struct snd_sof_dev *sdev, void *msg_buf)
{
	struct sof_ipc_cmd_hdr *hdr = msg_buf;
	u32 msg_type = hdr->cmd & SOF_CMD_TYPE_MASK;

	switch (msg_type) {
	case SOF_IPC_TRACE_DMA_POSITION:
		snd_sof_trace_update_pos(sdev, msg_buf);
		break;
	default:
		dev_err(sdev->dev, "error: unhandled trace message %#x\n", msg_type);
		break;
	}
}

/*
 * IPC stream position.
 */

static void ipc_period_elapsed(struct snd_sof_dev *sdev, u32 msg_id)
{
	struct snd_soc_component *scomp = sdev->component;
	struct snd_sof_pcm_stream *stream;
	struct sof_ipc_stream_posn posn;
	struct snd_sof_pcm *spcm;
	int direction, ret;

	spcm = snd_sof_find_spcm_comp(scomp, msg_id, &direction);
	if (!spcm) {
		dev_err(sdev->dev,
			"error: period elapsed for unknown stream, msg_id %d\n",
			msg_id);
		return;
	}

	stream = &spcm->stream[direction];
	ret = snd_sof_ipc_msg_data(sdev, stream->substream, &posn, sizeof(posn));
	if (ret < 0) {
		dev_warn(sdev->dev, "failed to read stream position: %d\n", ret);
		return;
	}

	dev_vdbg(sdev->dev, "posn : host 0x%llx dai 0x%llx wall 0x%llx\n",
		 posn.host_posn, posn.dai_posn, posn.wallclock);

	memcpy(&stream->posn, &posn, sizeof(posn));

	if (spcm->pcm.compress)
		snd_sof_compr_fragment_elapsed(stream->cstream);
	else if (stream->substream->runtime &&
		 !stream->substream->runtime->no_period_wakeup)
		/* only inform ALSA for period_wakeup mode */
		snd_sof_pcm_period_elapsed(stream->substream);
}

/* DSP notifies host of an XRUN within FW */
static void ipc_xrun(struct snd_sof_dev *sdev, u32 msg_id)
{
	struct snd_soc_component *scomp = sdev->component;
	struct snd_sof_pcm_stream *stream;
	struct sof_ipc_stream_posn posn;
	struct snd_sof_pcm *spcm;
	int direction, ret;

	spcm = snd_sof_find_spcm_comp(scomp, msg_id, &direction);
	if (!spcm) {
		dev_err(sdev->dev, "error: XRUN for unknown stream, msg_id %d\n",
			msg_id);
		return;
	}

	stream = &spcm->stream[direction];
	ret = snd_sof_ipc_msg_data(sdev, stream->substream, &posn, sizeof(posn));
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

/* stream notifications from DSP FW */
static void ipc_stream_message(struct snd_sof_dev *sdev, void *msg_buf)
{
	struct sof_ipc_cmd_hdr *hdr = msg_buf;
	u32 msg_type = hdr->cmd & SOF_CMD_TYPE_MASK;
	u32 msg_id = SOF_IPC_MESSAGE_ID(hdr->cmd);

	switch (msg_type) {
	case SOF_IPC_STREAM_POSITION:
		ipc_period_elapsed(sdev, msg_id);
		break;
	case SOF_IPC_STREAM_TRIG_XRUN:
		ipc_xrun(sdev, msg_id);
		break;
	default:
		dev_err(sdev->dev, "error: unhandled stream message %#x\n",
			msg_id);
		break;
	}
}

/* get stream position IPC - use faster MMIO method if available on platform */
int snd_sof_ipc_stream_posn(struct snd_soc_component *scomp,
			    struct snd_sof_pcm *spcm, int direction,
			    struct sof_ipc_stream_posn *posn)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct sof_ipc_stream stream;
	int err;

	/* read position via slower IPC */
	stream.hdr.size = sizeof(stream);
	stream.hdr.cmd = SOF_IPC_GLB_STREAM_MSG | SOF_IPC_STREAM_POSITION;
	stream.comp_id = spcm->stream[direction].comp_id;

	/* send IPC to the DSP */
	err = sof_ipc_tx_message(sdev->ipc, &stream, sizeof(stream), posn,
				 sizeof(*posn));
	if (err < 0) {
		dev_err(sdev->dev, "error: failed to get stream %d position\n",
			stream.comp_id);
		return err;
	}

	return 0;
}
EXPORT_SYMBOL(snd_sof_ipc_stream_posn);

/*
 * IPC get()/set() for kcontrols.
 */
int snd_sof_ipc_set_get_comp_data(struct snd_sof_control *scontrol, bool set)
{
	struct snd_soc_component *scomp = scontrol->scomp;
	struct sof_ipc_ctrl_data *cdata = scontrol->ipc_control_data;
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	const struct sof_ipc_ops *iops = sdev->ipc->ops;
	enum sof_ipc_ctrl_type ctrl_type;
	struct snd_sof_widget *swidget;
	bool widget_found = false;
	u32 ipc_cmd, msg_bytes;

	list_for_each_entry(swidget, &sdev->widget_list, list) {
		if (swidget->comp_id == scontrol->comp_id) {
			widget_found = true;
			break;
		}
	}

	if (!widget_found) {
		dev_err(sdev->dev, "%s: can't find widget with id %d\n", __func__,
			scontrol->comp_id);
		return -EINVAL;
	}

	/*
	 * Volatile controls should always be part of static pipelines and the widget use_count
	 * would always be > 0 in this case. For the others, just return the cached value if the
	 * widget is not set up.
	 */
	if (!swidget->use_count)
		return 0;

	/*
	 * Select the IPC cmd and the ctrl_type based on the ctrl_cmd and the
	 * direction
	 * Note: SOF_CTRL_TYPE_VALUE_COMP_* is not used and supported currently
	 *	 for ctrl_type
	 */
	if (cdata->cmd == SOF_CTRL_CMD_BINARY) {
		ipc_cmd = set ? SOF_IPC_COMP_SET_DATA : SOF_IPC_COMP_GET_DATA;
		ctrl_type = set ? SOF_CTRL_TYPE_DATA_SET : SOF_CTRL_TYPE_DATA_GET;
	} else {
		ipc_cmd = set ? SOF_IPC_COMP_SET_VALUE : SOF_IPC_COMP_GET_VALUE;
		ctrl_type = set ? SOF_CTRL_TYPE_VALUE_CHAN_SET : SOF_CTRL_TYPE_VALUE_CHAN_GET;
	}

	cdata->rhdr.hdr.cmd = SOF_IPC_GLB_COMP_MSG | ipc_cmd;
	cdata->type = ctrl_type;
	cdata->comp_id = scontrol->comp_id;
	cdata->msg_index = 0;

	/* calculate header and data size */
	switch (cdata->type) {
	case SOF_CTRL_TYPE_VALUE_CHAN_GET:
	case SOF_CTRL_TYPE_VALUE_CHAN_SET:
		cdata->num_elems = scontrol->num_channels;

		msg_bytes = scontrol->num_channels *
			    sizeof(struct sof_ipc_ctrl_value_chan);
		msg_bytes += sizeof(struct sof_ipc_ctrl_data);
		break;
	case SOF_CTRL_TYPE_DATA_GET:
	case SOF_CTRL_TYPE_DATA_SET:
		cdata->num_elems = cdata->data->size;

		msg_bytes = cdata->data->size;
		msg_bytes += sizeof(struct sof_ipc_ctrl_data) +
			     sizeof(struct sof_abi_hdr);
		break;
	default:
		return -EINVAL;
	}

	cdata->rhdr.hdr.size = msg_bytes;
	cdata->elems_remaining = 0;

	return iops->set_get_data(sdev, cdata, cdata->rhdr.hdr.size, set);
}
EXPORT_SYMBOL(snd_sof_ipc_set_get_comp_data);

int snd_sof_ipc_valid(struct snd_sof_dev *sdev)
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
		dev_err(sdev->dev, "error: incompatible FW ABI version\n");
		return -EINVAL;
	}

	if (SOF_ABI_VERSION_MINOR(v->abi_version) > SOF_ABI_MINOR) {
		if (!IS_ENABLED(CONFIG_SND_SOC_SOF_STRICT_ABI_CHECKS)) {
			dev_warn(sdev->dev, "warn: FW ABI is more recent than kernel\n");
		} else {
			dev_err(sdev->dev, "error: FW ABI is more recent than kernel\n");
			return -EINVAL;
		}
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
EXPORT_SYMBOL(snd_sof_ipc_valid);

int sof_ipc_init_msg_memory(struct snd_sof_dev *sdev)
{
	struct snd_sof_ipc_msg *msg;

	msg = &sdev->ipc->msg;

	msg->reply_data = devm_kzalloc(sdev->dev, SOF_IPC_MSG_MAX_SIZE, GFP_KERNEL);
	if (!msg->reply_data)
		return -ENOMEM;

	sdev->ipc->max_payload_size = SOF_IPC_MSG_MAX_SIZE;

	return 0;
}

struct snd_sof_ipc *snd_sof_ipc_init(struct snd_sof_dev *sdev)
{
	struct snd_sof_ipc *ipc;
	struct snd_sof_ipc_msg *msg;
	const struct sof_ipc_ops *ops;

	ipc = devm_kzalloc(sdev->dev, sizeof(*ipc), GFP_KERNEL);
	if (!ipc)
		return NULL;

	mutex_init(&ipc->tx_mutex);
	ipc->sdev = sdev;
	msg = &ipc->msg;

	/* indicate that we aren't sending a message ATM */
	msg->ipc_complete = true;

	init_waitqueue_head(&msg->waitq);

	/*
	 * Use IPC3 ops as it is the only available version now. With the addition of new IPC
	 * versions, this will need to be modified to use the selected version at runtime.
	 */
	ipc->ops = &ipc3_ops;
	ops = ipc->ops;

	/* check for mandatory ops */
	if (!ops->tx_msg || !ops->rx_msg || !ops->set_get_data || !ops->get_reply) {
		dev_err(sdev->dev, "Missing IPC message handling ops\n");
		return NULL;
	}

	if (!ops->pcm) {
		dev_err(sdev->dev, "Missing IPC PCM ops\n");
		return NULL;
	}

	if (!ops->tplg || !ops->tplg->widget || !ops->tplg->control) {
		dev_err(sdev->dev, "Missing IPC topology ops\n");
		return NULL;
	}

	return ipc;
}
EXPORT_SYMBOL(snd_sof_ipc_init);

void snd_sof_ipc_free(struct snd_sof_dev *sdev)
{
	struct snd_sof_ipc *ipc = sdev->ipc;

	if (!ipc)
		return;

	/* disable sending of ipc's */
	mutex_lock(&ipc->tx_mutex);
	ipc->disable_ipc_tx = true;
	mutex_unlock(&ipc->tx_mutex);
}
EXPORT_SYMBOL(snd_sof_ipc_free);
