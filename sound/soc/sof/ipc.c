// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
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

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/device.h>
#include <linux/wait.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/platform_device.h>
#include <linux/kthread.h>
#include <linux/firmware.h>
#include <linux/dma-mapping.h>
#include <linux/debugfs.h>
#include <linux/pm_runtime.h>
#include <sound/asound.h>
#include <sound/sof.h>
#include <uapi/sound/sof-ipc.h>
#include "sof-priv.h"
#include "ops.h"

/*
 * IPC message default size and timeout (msecs).
 * TODO: allow platforms to set size and timeout.
 */
#define IPC_TIMEOUT_MSECS	300
#define IPC_EMPTY_LIST_SIZE	8

static void ipc_trace_message(struct snd_sof_dev *sdev, u32 msg_id);
static void ipc_stream_message(struct snd_sof_dev *sdev, u32 msg_cmd);

/*
 * IPC message Tx/Rx message handling.
 */

/* SOF generic IPC data */
struct snd_sof_ipc {
	struct snd_sof_dev *sdev;

	/* TX message work and status */
	wait_queue_head_t wait_txq;
	struct work_struct tx_kwork;
	bool msg_pending;

	/* Rx Message work and status */
	struct work_struct rx_kwork;

	/* lists */
	struct list_head tx_list;
	struct list_head reply_list;
	struct list_head empty_list;
};

/* locks held by caller */
static struct snd_sof_ipc_msg *msg_get_empty(struct snd_sof_ipc *ipc)
{
	struct snd_sof_ipc_msg *msg = NULL;

	/* get first empty message in the list */
	if (!list_empty(&ipc->empty_list)) {
		msg = list_first_entry(&ipc->empty_list, struct snd_sof_ipc_msg,
				       list);
		list_del(&msg->list);
	}

	return msg;
}

#if IS_ENABLED(CONFIG_SND_SOC_SOF_DEBUG_VERBOSE_IPC)
static void ipc_log_header(struct device *dev, u8 *text, u32 cmd)
{
	u8 *str;
	u32 glb;
	u32 type;

	glb = cmd & SOF_GLB_TYPE_MASK;
	type = cmd & SOF_CMD_TYPE_MASK;

	switch (glb) {
	case SOF_IPC_GLB_REPLY:
		str = "GLB_REPLY"; break;
	case SOF_IPC_GLB_COMPOUND:
		str = "GLB_COMPOUND"; break;
	case SOF_IPC_GLB_TPLG_MSG:
		switch (type) {
		case SOF_IPC_TPLG_COMP_NEW:
			str = "GLB_TPLG_MSG: COMP_NEW"; break;
		case SOF_IPC_TPLG_COMP_FREE:
			str = "GLB_TPLG_MSG: COMP_FREE"; break;
		case SOF_IPC_TPLG_COMP_CONNECT:
			str = "GLB_TPLG_MSG: COMP_CONNECT"; break;
		case SOF_IPC_TPLG_PIPE_NEW:
			str = "GLB_TPLG_MSG: PIPE_NEW"; break;
		case SOF_IPC_TPLG_PIPE_FREE:
			str = "GLB_TPLG_MSG: PIPE_FREE"; break;
		case SOF_IPC_TPLG_PIPE_CONNECT:
			str = "GLB_TPLG_MSG: PIPE_CONNECT"; break;
		case SOF_IPC_TPLG_PIPE_COMPLETE:
			str = "GLB_TPLG_MSG: PIPE_COMPLETE"; break;
		case SOF_IPC_TPLG_BUFFER_NEW:
			str = "GLB_TPLG_MSG: BUFFER_NEW"; break;
		case SOF_IPC_TPLG_BUFFER_FREE:
			str = "GLB_TPLG_MSG: BUFFER_FREE"; break;
		default:
			str = "GLB_TPLG_MSG: unknown type"; break;
		}
		break;
	case SOF_IPC_GLB_PM_MSG:
		switch (type) {
		case SOF_IPC_PM_CTX_SAVE:
			str = "GLB_PM_MSG: CTX_SAVE"; break;
		case SOF_IPC_PM_CTX_RESTORE:
			str = "GLB_PM_MSG: CTX_RESTORE"; break;
		case SOF_IPC_PM_CTX_SIZE:
			str = "GLB_PM_MSG: CTX_SIZE"; break;
		case SOF_IPC_PM_CLK_SET:
			str = "GLB_PM_MSG: CLK_SET"; break;
		case SOF_IPC_PM_CLK_GET:
			str = "GLB_PM_MSG: CLK_GET"; break;
		case SOF_IPC_PM_CLK_REQ:
			str = "GLB_PM_MSG: CLK_REQ"; break;
		default:
			str = "GLB_PM_MSG: unknown type"; break;
		}
		break;
	case SOF_IPC_GLB_COMP_MSG:
		switch (type) {
		case SOF_IPC_COMP_SET_VALUE:
			str = "GLB_COMP_MSG: SET_VALUE"; break;
		case SOF_IPC_COMP_GET_VALUE:
			str = "GLB_COMP_MSG: GET_VALUE"; break;
		case SOF_IPC_COMP_SET_DATA:
			str = "GLB_COMP_MSG: SET_DATA"; break;
		case SOF_IPC_COMP_GET_DATA:
			str = "GLB_COMP_MSG: GET_DATA"; break;
		default:
			str = "GLB_COMP_MSG: unknown type"; break;
		}
		break;
	case SOF_IPC_GLB_STREAM_MSG:
		switch (type) {
		case SOF_IPC_STREAM_PCM_PARAMS:
			str = "GLB_STREAM_MSG: PCM_PARAMS"; break;
		case SOF_IPC_STREAM_PCM_PARAMS_REPLY:
			str = "GLB_STREAM_MSG: PCM_REPLY"; break;
		case SOF_IPC_STREAM_PCM_FREE:
			str = "GLB_STREAM_MSG: PCM_FREE"; break;
		case SOF_IPC_STREAM_TRIG_START:
			str = "GLB_STREAM_MSG: TRIG_START"; break;
		case SOF_IPC_STREAM_TRIG_STOP:
			str = "GLB_STREAM_MSG: TRIG_STOP"; break;
		case SOF_IPC_STREAM_TRIG_PAUSE:
			str = "GLB_STREAM_MSG: TRIG_PAUSE"; break;
		case SOF_IPC_STREAM_TRIG_RELEASE:
			str = "GLB_STREAM_MSG: TRIG_RELEASE"; break;
		case SOF_IPC_STREAM_TRIG_DRAIN:
			str = "GLB_STREAM_MSG: TRIG_DRAIN"; break;
		case SOF_IPC_STREAM_TRIG_XRUN:
			str = "GLB_STREAM_MSG: TRIG_XRUN"; break;
		case SOF_IPC_STREAM_POSITION:
			str = "GLB_STREAM_MSG: POSITION"; break;
		case SOF_IPC_STREAM_VORBIS_PARAMS:
			str = "GLB_STREAM_MSG: VORBIS_PARAMS"; break;
		case SOF_IPC_STREAM_VORBIS_FREE:
			str = "GLB_STREAM_MSG: VORBIS_FREE"; break;
		default:
			str = "GLB_STREAM_MSG: unknown type"; break;
		}
		break;
	case SOF_IPC_FW_READY:
		str = "FW_READY"; break;
	case SOF_IPC_GLB_DAI_MSG:
		switch (type) {
		case SOF_IPC_DAI_CONFIG:
			str = "GLB_DAI_MSG: CONFIG"; break;
		case SOF_IPC_DAI_LOOPBACK:
			str = "GLB_DAI_MSG: LOOPBACK"; break;
		default:
			str = "GLB_DAI_MSG: unknown type"; break;
		}
		break;
	case SOF_IPC_GLB_TRACE_MSG:
		str = "GLB_TRACE_MSG"; break;
	default:
		str = "unknown GLB command"; break;
	}

	dev_dbg(dev, "%s: 0x%x: %s\n", text, cmd, str);
}
#else
static inline void ipc_log_header(struct device *dev, u8 *text, u32 cmd)
{
	dev_dbg(dev, "%s: 0x%x\n", text, cmd);
}
#endif

/* wait for IPC message reply */
static int tx_wait_done(struct snd_sof_ipc *ipc, struct snd_sof_ipc_msg *msg,
			void *reply_data)
{
	struct snd_sof_dev *sdev = ipc->sdev;
	struct sof_ipc_hdr *hdr = (struct sof_ipc_hdr *)msg->msg_data;
	unsigned long flags;
	int ret;

	/* wait for DSP IPC completion */
	ret = wait_event_timeout(msg->waitq, msg->complete,
				 msecs_to_jiffies(IPC_TIMEOUT_MSECS));

	spin_lock_irqsave(&sdev->ipc_lock, flags);

	if (ret == 0) {
		dev_err(sdev->dev, "error: ipc timed out for 0x%x size 0x%x\n",
			hdr->cmd, hdr->size);
		snd_sof_dsp_dbg_dump(ipc->sdev, SOF_DBG_REGS | SOF_DBG_MBOX);
		snd_sof_trace_notify_for_error(ipc->sdev);
		ret = -ETIMEDOUT;
	} else {
		/* copy the data returned from DSP */
		ret = snd_sof_dsp_get_reply(sdev, msg);
		if (msg->reply_size)
			memcpy(reply_data, msg->reply_data, msg->reply_size);
		if (ret < 0)
			dev_err(sdev->dev, "error: ipc error for 0x%x size 0x%zx\n",
				hdr->cmd, msg->reply_size);
		else
			ipc_log_header(sdev->dev, "ipc tx succeeded", hdr->cmd);
	}

	/* return message body to empty list */
	list_move(&msg->list, &ipc->empty_list);

	spin_unlock_irqrestore(&sdev->ipc_lock, flags);

	snd_sof_dsp_cmd_done(sdev, SOF_IPC_DSP_REPLY);

	/* continue to schedule any remaining messages... */
	snd_sof_ipc_msgs_tx(sdev);

	return ret;
}

/* send IPC message from host to DSP */
int sof_ipc_tx_message(struct snd_sof_ipc *ipc, u32 header,
		       void *msg_data, size_t msg_bytes, void *reply_data,
		       size_t reply_bytes)
{
	struct snd_sof_dev *sdev = ipc->sdev;
	struct snd_sof_ipc_msg *msg;
	unsigned long flags;

	spin_lock_irqsave(&sdev->ipc_lock, flags);

	/* get an empty message */
	msg = msg_get_empty(ipc);
	if (!msg) {
		spin_unlock_irqrestore(&sdev->ipc_lock, flags);
		return -EBUSY;
	}

	msg->header = header;
	msg->msg_size = msg_bytes;
	msg->reply_size = reply_bytes;
	msg->complete = false;

	/* attach any data */
	if (msg_bytes)
		memcpy(msg->msg_data, msg_data, msg_bytes);

	/* add message to transmit list */
	list_add_tail(&msg->list, &ipc->tx_list);

	/* schedule the message if not busy */
	if (snd_sof_dsp_is_ready(sdev))
		schedule_work(&ipc->tx_kwork);

	spin_unlock_irqrestore(&sdev->ipc_lock, flags);

	/* now wait for completion */
	return tx_wait_done(ipc, msg, reply_data);
}
EXPORT_SYMBOL(sof_ipc_tx_message);

/* send next IPC message in list */
static void ipc_tx_next_msg(struct work_struct *work)
{
	struct snd_sof_ipc *ipc =
		container_of(work, struct snd_sof_ipc, tx_kwork);
	struct snd_sof_dev *sdev = ipc->sdev;
	struct snd_sof_ipc_msg *msg;
	unsigned long flags;

	spin_lock_irqsave(&sdev->ipc_lock, flags);

	/* send message if HW read and message in TX list */
	if (list_empty(&ipc->tx_list) || !snd_sof_dsp_is_ready(sdev))
		goto out;

	/* send first message in TX list */
	msg = list_first_entry(&ipc->tx_list, struct snd_sof_ipc_msg, list);
	list_move(&msg->list, &ipc->reply_list);
	snd_sof_dsp_send_msg(sdev, msg);

	ipc_log_header(sdev->dev, "ipc tx", msg->header);
out:
	spin_unlock_irqrestore(&sdev->ipc_lock, flags);
}

/* find original TX message from DSP reply */
static struct snd_sof_ipc_msg *sof_ipc_reply_find_msg(struct snd_sof_ipc *ipc,
						      u32 header)
{
	struct snd_sof_dev *sdev = ipc->sdev;
	struct snd_sof_ipc_msg *msg;

	header = SOF_IPC_MESSAGE_ID(header);

	if (list_empty(&ipc->reply_list))
		goto err;

	list_for_each_entry(msg, &ipc->reply_list, list) {
		if (SOF_IPC_MESSAGE_ID(msg->header) == header)
			return msg;
	}

err:
	dev_err(sdev->dev, "error: rx list empty but received 0x%x\n",
		header);
	return NULL;
}

/* mark IPC message as complete - locks held by caller */
static void sof_ipc_tx_msg_reply_complete(struct snd_sof_ipc *ipc,
					  struct snd_sof_ipc_msg *msg)
{
	msg->complete = true;
	wake_up(&msg->waitq);
}

/* drop all IPC messages in preparation for DSP stall/reset */
void sof_ipc_drop_all(struct snd_sof_ipc *ipc)
{
	struct snd_sof_dev *sdev = ipc->sdev;
	struct snd_sof_ipc_msg *msg, *tmp;
	unsigned long flags;

	/* drop all TX and Rx messages before we stall + reset DSP */
	spin_lock_irqsave(&sdev->ipc_lock, flags);

	list_for_each_entry_safe(msg, tmp, &ipc->tx_list, list) {
		list_move(&msg->list, &ipc->empty_list);
		dev_err(sdev->dev, "error: dropped msg %d\n", msg->header);
	}

	list_for_each_entry_safe(msg, tmp, &ipc->reply_list, list) {
		list_move(&msg->list, &ipc->empty_list);
		dev_err(sdev->dev, "error: dropped reply %d\n", msg->header);
	}

	spin_unlock_irqrestore(&sdev->ipc_lock, flags);
}
EXPORT_SYMBOL(sof_ipc_drop_all);

/* handle reply message from DSP */
int snd_sof_ipc_reply(struct snd_sof_dev *sdev, u32 msg_id)
{
	struct snd_sof_ipc_msg *msg;

	msg = sof_ipc_reply_find_msg(sdev->ipc, msg_id);
	if (!msg) {
		dev_err(sdev->dev, "error: can't find message header 0x%x",
			msg_id);
		return -EINVAL;
	}

	/* wake up and return the error if we have waiters on this message ? */
	sof_ipc_tx_msg_reply_complete(sdev->ipc, msg);
	return 0;
}
EXPORT_SYMBOL(snd_sof_ipc_reply);

/* DSP firmware has sent host a message  */
static void ipc_msgs_rx(struct work_struct *work)
{
	struct snd_sof_ipc *ipc =
		container_of(work, struct snd_sof_ipc, rx_kwork);
	struct snd_sof_dev *sdev = ipc->sdev;
	struct sof_ipc_hdr hdr;
	u32 cmd, type;
	int err = -EINVAL;

	/* read back header */
	snd_sof_dsp_mailbox_read(sdev, sdev->dsp_box.offset, &hdr, sizeof(hdr));
	ipc_log_header(sdev->dev, "ipc rx", hdr.cmd);

	cmd = hdr.cmd & SOF_GLB_TYPE_MASK;
	type = hdr.cmd & SOF_CMD_TYPE_MASK;

	/* check message type */
	switch (cmd) {
	case SOF_IPC_GLB_REPLY:
		dev_err(sdev->dev, "error: ipc reply unknown\n");
		break;
	case SOF_IPC_FW_READY:
		/* check for FW boot completion */
		if (!sdev->boot_complete) {
			if (sdev->ops->fw_ready)
				err = sdev->ops->fw_ready(sdev, cmd);
			if (err < 0) {
				dev_err(sdev->dev, "DSP firmware boot timeout %d\n",
					err);
			} else {
				/* firmware boot completed OK */
				sdev->boot_complete = true;
				dev_dbg(sdev->dev, "booting DSP firmware completed\n");
				wake_up(&sdev->boot_wait);
			}
		}
		break;
	case SOF_IPC_GLB_COMPOUND:
	case SOF_IPC_GLB_TPLG_MSG:
	case SOF_IPC_GLB_PM_MSG:
	case SOF_IPC_GLB_COMP_MSG:
		break;
	case SOF_IPC_GLB_STREAM_MSG:
		/* need to pass msg id into the function */
		ipc_stream_message(sdev, hdr.cmd);
		break;
	case SOF_IPC_GLB_TRACE_MSG:
		ipc_trace_message(sdev, type);
		break;
	default:
		dev_err(sdev->dev, "unknown DSP message 0x%x\n", cmd);
		break;
	}

	ipc_log_header(sdev->dev, "ipc rx done", hdr.cmd);

	/* tell DSP we are done */
	snd_sof_dsp_cmd_done(sdev, SOF_IPC_HOST_REPLY);
}

/* schedule work to transmit any IPC in queue */
void snd_sof_ipc_msgs_tx(struct snd_sof_dev *sdev)
{
	schedule_work(&sdev->ipc->tx_kwork);
}
EXPORT_SYMBOL(snd_sof_ipc_msgs_tx);

/* schedule work to handle IPC from DSP */
void snd_sof_ipc_msgs_rx(struct snd_sof_dev *sdev)
{
	schedule_work(&sdev->ipc->rx_kwork);
}
EXPORT_SYMBOL(snd_sof_ipc_msgs_rx);

/*
 * IPC trace mechanism.
 */

static void ipc_trace_message(struct snd_sof_dev *sdev, u32 msg_id)
{
	struct sof_ipc_dma_trace_posn posn;

	switch (msg_id) {
	case SOF_IPC_TRACE_DMA_POSITION:
		/* read back full message */
		snd_sof_dsp_mailbox_read(sdev, sdev->dsp_box.offset, &posn,
					 sizeof(posn));
		snd_sof_trace_update_pos(sdev, &posn);
		break;
	default:
		dev_err(sdev->dev, "error: unhandled trace message %x\n",
			msg_id);
		break;
	}
}

/*
 * IPC stream position.
 */

static void ipc_period_elapsed(struct snd_sof_dev *sdev, u32 msg_id)
{
	struct sof_ipc_stream_posn posn;
	struct snd_sof_pcm *spcm;
	u32 posn_offset;
	int direction;

	/* check if we have stream box */
	if (sdev->stream_box.size == 0) {
		/* read back full message */
		snd_sof_dsp_mailbox_read(sdev, sdev->dsp_box.offset, &posn,
					 sizeof(posn));

		spcm = snd_sof_find_spcm_comp(sdev, posn.comp_id, &direction);
	} else {
		spcm = snd_sof_find_spcm_comp(sdev, msg_id, &direction);
	}

	if (!spcm) {
		dev_err(sdev->dev,
			"period elapsed for unknown stream, msg_id %d\n",
			msg_id);
		return;
	}

	/* have stream box read from stream box */
	if (sdev->stream_box.size != 0) {
		posn_offset = spcm->posn_offset[direction];
		snd_sof_dsp_mailbox_read(sdev, posn_offset, &posn,
					 sizeof(posn));

		dev_dbg(sdev->dev, "posn mailbox: posn offset is 0x%x",
			posn_offset);
	}

	dev_dbg(sdev->dev, "posn : host 0x%llx dai 0x%llx wall 0x%llx\n",
		posn.host_posn, posn.dai_posn, posn.wallclock);

	memcpy(&spcm->stream[direction].posn, &posn, sizeof(posn));
	snd_pcm_period_elapsed(spcm->stream[direction].substream);
}

/* DSP notifies host of an XRUN within FW */
static void ipc_xrun(struct snd_sof_dev *sdev, u32 msg_id)
{
	struct sof_ipc_stream_posn posn;
	struct snd_sof_pcm *spcm;
	u32 posn_offset;
	int direction;

	/* check if we have stream MMIO on this platform */
	if (sdev->stream_box.size == 0) {
		/* read back full message */
		snd_sof_dsp_mailbox_read(sdev, sdev->dsp_box.offset, &posn,
					 sizeof(posn));

		spcm = snd_sof_find_spcm_comp(sdev, posn.comp_id, &direction);
	} else {
		spcm = snd_sof_find_spcm_comp(sdev, msg_id, &direction);
	}

	if (!spcm) {
		dev_err(sdev->dev, "XRUN for unknown stream, msg_id %d\n",
			msg_id);
		return;
	}

	/* have stream box read from stream box */
	if (sdev->stream_box.size != 0) {
		posn_offset = spcm->posn_offset[direction];
		snd_sof_dsp_mailbox_read(sdev, posn_offset, &posn,
					 sizeof(posn));

		dev_dbg(sdev->dev, "posn mailbox: posn offset is 0x%x",
			posn_offset);
	}

	dev_dbg(sdev->dev,  "posn XRUN: host %llx comp %d size %d\n",
		posn.host_posn, posn.xrun_comp_id, posn.xrun_size);

#if defined(CONFIG_SOC_SOF_DEBUG_XRUN_STOP)
	/* stop PCM on XRUN - used for pipeline debug */
	memcpy(&spcm->stream[direction].posn, &posn, sizeof(posn));
	snd_pcm_stop_xrun(spcm->stream[direction].substream);
#endif
}

/* stream notifications from DSP FW */
static void ipc_stream_message(struct snd_sof_dev *sdev, u32 msg_cmd)
{
	/* get msg cmd type and msd id */
	u32 msg_type = msg_cmd & SOF_CMD_TYPE_MASK;
	u32 msg_id = SOF_IPC_MESSAGE_ID(msg_cmd);

	switch (msg_type) {
	case SOF_IPC_STREAM_POSITION:
		ipc_period_elapsed(sdev, msg_id);
		break;
	case SOF_IPC_STREAM_TRIG_XRUN:
		ipc_xrun(sdev, msg_id);
		break;
	default:
		dev_err(sdev->dev, "error: unhandled stream message %x\n",
			msg_id);
		break;
	}
}

/* get stream position IPC - use faster MMIO method if available on platform */
int snd_sof_ipc_stream_posn(struct snd_sof_dev *sdev,
			    struct snd_sof_pcm *spcm, int direction,
			    struct sof_ipc_stream_posn *posn)
{
	struct sof_ipc_stream stream;
	int err;

	/* read position via slower IPC */
	stream.hdr.size = sizeof(stream);
	stream.hdr.cmd = SOF_IPC_GLB_STREAM_MSG | SOF_IPC_STREAM_POSITION;
	stream.comp_id = spcm->stream[direction].comp_id;

	/* send IPC to the DSP */
	err = sof_ipc_tx_message(sdev->ipc,
				 stream.hdr.cmd, &stream, sizeof(stream), &posn,
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

int snd_sof_ipc_set_comp_data(struct snd_sof_ipc *ipc,
			      struct snd_sof_control *scontrol, u32 ipc_cmd,
			      enum sof_ipc_ctrl_type ctrl_type,
			      enum sof_ipc_ctrl_cmd ctrl_cmd)
{
	struct snd_sof_dev *sdev = ipc->sdev;
	struct sof_ipc_ctrl_data *cdata = scontrol->control_data;
	int err;

	/* read firmware volume */
	if (scontrol->readback_offset != 0) {
		/* we can read value header via mmaped region */
		snd_sof_dsp_block_write(sdev, scontrol->readback_offset,
					cdata->chanv,
					sizeof(struct sof_ipc_ctrl_value_chan) *
					cdata->num_elems);

	} else {
		/* write value via slower IPC */
		cdata->rhdr.hdr.cmd = SOF_IPC_GLB_COMP_MSG | ipc_cmd;
		cdata->cmd = ctrl_cmd;
		cdata->type = ctrl_type;
		cdata->rhdr.hdr.size = scontrol->size;
		cdata->comp_id = scontrol->comp_id;
		cdata->num_elems = scontrol->num_channels;

		/* send IPC to the DSP */
		err = sof_ipc_tx_message(sdev->ipc,
					 cdata->rhdr.hdr.cmd, cdata,
					 cdata->rhdr.hdr.size,
					 cdata, cdata->rhdr.hdr.size);
		if (err < 0) {
			dev_err(sdev->dev, "error: failed to set control %d values\n",
				cdata->comp_id);
			return err;
		}
	}

	return 0;
}
EXPORT_SYMBOL(snd_sof_ipc_set_comp_data);

int snd_sof_ipc_get_comp_data(struct snd_sof_ipc *ipc,
			      struct snd_sof_control *scontrol, u32 ipc_cmd,
			      enum sof_ipc_ctrl_type ctrl_type,
			      enum sof_ipc_ctrl_cmd ctrl_cmd)
{
	struct snd_sof_dev *sdev = ipc->sdev;
	struct sof_ipc_ctrl_data *cdata = scontrol->control_data;
	int err;

	/* read firmware byte counters */
	if (scontrol->readback_offset != 0) {
		/* we can read values via mmaped region */
		snd_sof_dsp_block_read(sdev, scontrol->readback_offset,
				       cdata->chanv,
				       sizeof(struct sof_ipc_ctrl_value_chan) *
				       cdata->num_elems);

	} else {
		/* read position via slower IPC */
		cdata->rhdr.hdr.cmd = SOF_IPC_GLB_COMP_MSG | ipc_cmd;
		cdata->cmd = ctrl_cmd;
		cdata->type = ctrl_type;
		cdata->rhdr.hdr.size = scontrol->size;
		cdata->comp_id = scontrol->comp_id;
		cdata->num_elems = scontrol->num_channels;

		/* send IPC to the DSP */
		err = sof_ipc_tx_message(sdev->ipc,
					 cdata->rhdr.hdr.cmd, cdata,
					 cdata->rhdr.hdr.size,
					 cdata, cdata->rhdr.hdr.size);
		if (err < 0) {
			dev_err(sdev->dev, "error: failed to get control %d values\n",
				cdata->comp_id);
			return err;
		}
	}

	return 0;
}
EXPORT_SYMBOL(snd_sof_ipc_get_comp_data);

/*
 * IPC layer enumeration.
 */

int snd_sof_dsp_mailbox_init(struct snd_sof_dev *sdev, u32 dspbox,
			     size_t dspbox_size, u32 hostbox,
			     size_t hostbox_size)
{
	sdev->dsp_box.offset = dspbox;
	sdev->dsp_box.size = dspbox_size;
	sdev->host_box.offset = hostbox;
	sdev->host_box.size = hostbox_size;
	return 0;
}
EXPORT_SYMBOL(snd_sof_dsp_mailbox_init);

struct snd_sof_ipc *snd_sof_ipc_init(struct snd_sof_dev *sdev)
{
	struct snd_sof_ipc *ipc;
	struct snd_sof_ipc_msg *msg;
	int i;

	ipc = devm_kzalloc(sdev->dev, sizeof(*ipc), GFP_KERNEL);
	if (!ipc)
		return NULL;

	INIT_LIST_HEAD(&ipc->tx_list);
	INIT_LIST_HEAD(&ipc->reply_list);
	INIT_LIST_HEAD(&ipc->empty_list);
	init_waitqueue_head(&ipc->wait_txq);
	INIT_WORK(&ipc->tx_kwork, ipc_tx_next_msg);
	INIT_WORK(&ipc->rx_kwork, ipc_msgs_rx);
	ipc->sdev = sdev;

	/* pre-allocate messages */
	dev_dbg(sdev->dev, "pre-allocate %d IPC messages\n",
		IPC_EMPTY_LIST_SIZE);
	msg = devm_kzalloc(sdev->dev, sizeof(struct snd_sof_ipc_msg) *
			   IPC_EMPTY_LIST_SIZE, GFP_KERNEL);
	if (!msg)
		return NULL;

	/* pre-allocate message data */
	for (i = 0; i < IPC_EMPTY_LIST_SIZE; i++) {
		msg->msg_data = devm_kzalloc(sdev->dev, PAGE_SIZE, GFP_KERNEL);
		if (!msg->msg_data)
			return NULL;

		msg->reply_data = devm_kzalloc(sdev->dev, PAGE_SIZE,
					       GFP_KERNEL);
		if (!msg->reply_data)
			return NULL;

		init_waitqueue_head(&msg->waitq);
		list_add(&msg->list, &ipc->empty_list);
		msg++;
	}

	return ipc;
}
EXPORT_SYMBOL(snd_sof_ipc_init);

void snd_sof_ipc_free(struct snd_sof_dev *sdev)
{
	cancel_work_sync(&sdev->ipc->tx_kwork);
	cancel_work_sync(&sdev->ipc->rx_kwork);
}
EXPORT_SYMBOL(snd_sof_ipc_free);
