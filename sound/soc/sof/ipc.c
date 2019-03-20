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

#include <linux/mutex.h>
#include <linux/types.h>

#include "sof-priv.h"
#include "ops.h"

/*
 * IPC message default size and timeout (ms).
 * TODO: allow platforms to set size and timeout.
 */
#define IPC_TIMEOUT_MS		300

static void ipc_trace_message(struct snd_sof_dev *sdev, u32 msg_id);
static void ipc_stream_message(struct snd_sof_dev *sdev, u32 msg_cmd);

/*
 * IPC message Tx/Rx message handling.
 */

/* SOF generic IPC data */
struct snd_sof_ipc {
	struct snd_sof_dev *sdev;

	/* protects messages and the disable flag */
	struct mutex tx_mutex;
	/* disables further sending of ipc's */
	bool disable_ipc_tx;

	struct snd_sof_ipc_msg msg;
};

#if IS_ENABLED(CONFIG_SND_SOC_SOF_DEBUG_VERBOSE_IPC)
static void ipc_log_header(struct device *dev, u8 *text, u32 cmd)
{
	u8 *str;
	u8 *str2 = NULL;
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
		default:
			str2 = "unknown type"; break;
		}
		break;
	case SOF_IPC_GLB_COMP_MSG:
		str = "GLB_COMP_MSG: SET_VALUE";
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
		str = "GLB_TRACE_MSG"; break;
	default:
		str = "unknown GLB command"; break;
	}

	if (str2)
		dev_dbg(dev, "%s: 0x%x: %s: %s\n", text, cmd, str, str2);
	else
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
	struct sof_ipc_cmd_hdr *hdr = msg->msg_data;
	int ret;

	/* wait for DSP IPC completion */
	ret = wait_event_timeout(msg->waitq, msg->ipc_complete,
				 msecs_to_jiffies(IPC_TIMEOUT_MS));

	if (ret == 0) {
		dev_err(sdev->dev, "error: ipc timed out for 0x%x size %d\n",
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
			dev_err(sdev->dev, "error: ipc error for 0x%x size %zu\n",
				hdr->cmd, msg->reply_size);
		else
			ipc_log_header(sdev->dev, "ipc tx succeeded", hdr->cmd);
	}

	snd_sof_dsp_cmd_done(sdev, SOF_IPC_DSP_REPLY);

	return ret;
}

/* send IPC message from host to DSP */
int sof_ipc_tx_message(struct snd_sof_ipc *ipc, u32 header,
		       void *msg_data, size_t msg_bytes, void *reply_data,
		       size_t reply_bytes)
{
	struct snd_sof_dev *sdev = ipc->sdev;
	struct snd_sof_ipc_msg *msg;
	int ret;

	if (msg_bytes > SOF_IPC_MSG_MAX_SIZE ||
	    reply_bytes > SOF_IPC_MSG_MAX_SIZE)
		return -ENOBUFS;

	/* Serialise IPC TX */
	mutex_lock(&ipc->tx_mutex);

	if (ipc->disable_ipc_tx) {
		ret = -ENODEV;
		goto unlock;
	}

	/*
	 * The spin-lock is also still needed to protect message objects against
	 * other atomic contexts.
	 */
	spin_lock_irq(&sdev->ipc_lock);

	/* initialise the message */
	msg = &ipc->msg;

	msg->header = header;
	msg->msg_size = msg_bytes;
	msg->reply_size = reply_bytes;

	/* attach any data */
	if (msg_bytes)
		memcpy(msg->msg_data, msg_data, msg_bytes);

	ret = snd_sof_dsp_send_msg(sdev, msg);
	/* Next reply that we receive will be related to this message */
	if (!ret)
		msg->ipc_complete = false;

	spin_unlock_irq(&sdev->ipc_lock);

	if (ret < 0) {
		/* So far IPC TX never fails, consider making the above void */
		dev_err_ratelimited(sdev->dev,
				    "error: ipc tx failed with error %d\n",
				    ret);
		goto unlock;
	}

	ipc_log_header(sdev->dev, "ipc tx", msg->header);

	/* now wait for completion */
	if (!ret)
		ret = tx_wait_done(ipc, msg, reply_data);

unlock:
	mutex_unlock(&ipc->tx_mutex);

	return ret;
}
EXPORT_SYMBOL(sof_ipc_tx_message);

/* mark IPC message as complete - locks held by caller */
static void sof_ipc_tx_msg_reply_complete(struct snd_sof_ipc *ipc,
					  struct snd_sof_ipc_msg *msg)
{
	msg->ipc_complete = true;
	wake_up(&msg->waitq);
}

/* handle reply message from DSP */
int snd_sof_ipc_reply(struct snd_sof_dev *sdev, u32 msg_id)
{
	struct snd_sof_ipc_msg *msg = &sdev->ipc->msg;
	unsigned long flags;

	/*
	 * Protect against a theoretical race with sof_ipc_tx_message(): if the
	 * DSP is fast enough to receive an IPC message, reply to it, and the
	 * host interrupt processing calls this function on a different core
	 * from the one, where the sending is taking place, the message might
	 * not yet be marked as expecting a reply.
	 */
	spin_lock_irqsave(&sdev->ipc_lock, flags);

	if (msg->ipc_complete) {
		spin_unlock_irqrestore(&sdev->ipc_lock, flags);
		dev_err(sdev->dev, "error: no reply expected, received 0x%x",
			msg_id);
		return -EINVAL;
	}

	/* wake up and return the error if we have waiters on this message ? */
	sof_ipc_tx_msg_reply_complete(sdev->ipc, msg);

	spin_unlock_irqrestore(&sdev->ipc_lock, flags);

	return 0;
}
EXPORT_SYMBOL(snd_sof_ipc_reply);

/* DSP firmware has sent host a message  */
void snd_sof_ipc_msgs_rx(struct snd_sof_dev *sdev)
{
	struct sof_ipc_cmd_hdr hdr;
	u32 cmd, type;
	int err = 0;

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
			err = sof_ops(sdev)->fw_ready(sdev, cmd);
			if (err < 0) {
				/*
				 * this indicates a mismatch in ABI
				 * between the driver and fw
				 */
				dev_err(sdev->dev, "error: ABI mismatch %d\n",
					err);
			} else {
				/* firmware boot completed OK */
				sdev->boot_complete = true;
			}

			/* wake up firmware loader */
			wake_up(&sdev->boot_wait);
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
		dev_err(sdev->dev, "error: unknown DSP message 0x%x\n", cmd);
		break;
	}

	ipc_log_header(sdev->dev, "ipc rx done", hdr.cmd);

	/* tell DSP we are done */
	snd_sof_dsp_cmd_done(sdev, SOF_IPC_HOST_REPLY);
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
			"error: period elapsed for unknown stream, msg_id %d\n",
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

	/* only inform ALSA for period_wakeup mode */
	if (!spcm->stream[direction].substream->runtime->no_period_wakeup)
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
		dev_err(sdev->dev, "error: XRUN for unknown stream, msg_id %d\n",
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

#if defined(CONFIG_SND_SOC_SOF_DEBUG_XRUN_STOP)
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
		snd_sof_dsp_block_write(sdev, sdev->mmio_bar,
					scontrol->readback_offset, cdata->chanv,
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
		snd_sof_dsp_block_read(sdev, sdev->mmio_bar,
				       scontrol->readback_offset, cdata->chanv,
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

	if (ready->debug.bits.build) {
		dev_info(sdev->dev,
			 "Firmware debug build %d on %s-%s - options:\n"
			 " GDB: %s\n"
			 " lock debug: %s\n"
			 " lock vdebug: %s\n",
			 v->build, v->date, v->time,
			 ready->debug.bits.gdb ? "enabled" : "disabled",
			 ready->debug.bits.locks ? "enabled" : "disabled",
			 ready->debug.bits.locks_verbose ? "enabled" : "disabled");
	}

	/* copy the fw_version into debugfs at first boot */
	memcpy(&sdev->fw_version, v, sizeof(*v));

	return 0;
}
EXPORT_SYMBOL(snd_sof_ipc_valid);

struct snd_sof_ipc *snd_sof_ipc_init(struct snd_sof_dev *sdev)
{
	struct snd_sof_ipc *ipc;
	struct snd_sof_ipc_msg *msg;

	/* check if mandatory ops required for ipc are defined */
	if (!sof_ops(sdev)->fw_ready) {
		dev_err(sdev->dev, "error: ipc mandatory ops not defined\n");
		return NULL;
	}

	ipc = devm_kzalloc(sdev->dev, sizeof(*ipc), GFP_KERNEL);
	if (!ipc)
		return NULL;

	mutex_init(&ipc->tx_mutex);
	ipc->sdev = sdev;
	msg = &ipc->msg;

	/* Indicate, that we aren't sending a message ATM */
	msg->ipc_complete = true;

	/* pre-allocate message data */
	msg->msg_data = devm_kzalloc(sdev->dev, SOF_IPC_MSG_MAX_SIZE,
				     GFP_KERNEL);
	if (!msg->msg_data)
		return NULL;

	msg->reply_data = devm_kzalloc(sdev->dev, SOF_IPC_MSG_MAX_SIZE,
				       GFP_KERNEL);
	if (!msg->reply_data)
		return NULL;

	init_waitqueue_head(&msg->waitq);

	return ipc;
}
EXPORT_SYMBOL(snd_sof_ipc_init);

void snd_sof_ipc_free(struct snd_sof_dev *sdev)
{
	struct snd_sof_ipc *ipc = sdev->ipc;

	/* disable sending of ipc's */
	mutex_lock(&ipc->tx_mutex);
	ipc->disable_ipc_tx = true;
	mutex_unlock(&ipc->tx_mutex);
}
EXPORT_SYMBOL(snd_sof_ipc_free);
