// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2021 Intel Corporation. All rights reserved.
//
//

#include "sof-priv.h"
#include "ipc3-ops.h"
#include "ops.h"

#if IS_ENABLED(CONFIG_SND_SOC_SOF_DEBUG_VERBOSE_IPC)
static void ipc3_log_header(struct device *dev, u8 *text, u32 cmd)
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
static inline void ipc3_log_header(struct device *dev, u8 *text, u32 cmd)
{
	if ((cmd & SOF_GLB_TYPE_MASK) != SOF_IPC_GLB_TRACE_MSG)
		dev_dbg(dev, "%s: 0x%x\n", text, cmd);
}
#endif

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
		snd_sof_handle_fw_exception(ipc->sdev);
		ret = -ETIMEDOUT;
	} else {
		ret = msg->reply_error;
		if (ret < 0) {
			dev_err(sdev->dev,
				"ipc tx error for %#x (msg/reply size: %d/%zu): %d\n",
				hdr->cmd, hdr->size, msg->reply_size, ret);
		} else {
			ipc3_log_header(sdev->dev, "ipc tx succeeded", hdr->cmd);
			if (msg->reply_size)
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

	ret = sof_ipc_send_msg(sdev, msg_data, msg_bytes, reply_bytes);

	if (ret) {
		dev_err_ratelimited(sdev->dev,
				    "%s: ipc message send for %#x failed: %d\n",
				    __func__, hdr->cmd, ret);
		return ret;
	}

	ipc3_log_header(sdev->dev, "ipc tx", hdr->cmd);

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

	mutex_unlock(&ipc->tx_mutex);

	return ret;
}

static int sof_ipc3_ctx_ipc(struct snd_sof_dev *sdev, int cmd)
{
	struct sof_ipc_pm_ctx pm_ctx = {
		.hdr.size = sizeof(pm_ctx),
		.hdr.cmd = SOF_IPC_GLB_PM_MSG | cmd,
	};
	struct sof_ipc_reply reply;

	/* send ctx save ipc to dsp */
	return sof_ipc3_tx_msg(sdev, &pm_ctx, sizeof(pm_ctx),
			       &reply, sizeof(reply), false);
}

static int sof_ipc3_ctx_save(struct snd_sof_dev *sdev)
{
	return sof_ipc3_ctx_ipc(sdev, SOF_IPC_PM_CTX_SAVE);
}

static int sof_ipc3_ctx_restore(struct snd_sof_dev *sdev)
{
	return sof_ipc3_ctx_ipc(sdev, SOF_IPC_PM_CTX_RESTORE);
}

static const struct sof_ipc_pm_ops ipc3_pm_ops = {
	.ctx_save = sof_ipc3_ctx_save,
	.ctx_restore = sof_ipc3_ctx_restore,
};

const struct sof_ipc_ops ipc3_ops = {
	.tplg = &ipc3_tplg_ops,
	.pm = &ipc3_pm_ops,
	.pcm = &ipc3_pcm_ops,

	.tx_msg = sof_ipc3_tx_msg,
};
