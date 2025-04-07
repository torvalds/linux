// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license. When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2022 MediaTek Inc. All rights reserved.
//
// Author: YC Hung <yc.hung@mediatek.com>

/*
 * Common helpers for the audio DSP on MediaTek platforms
 */

#include <linux/module.h>
#include <sound/asound.h>
#include <sound/sof/xtensa.h>
#include "../ops.h"
#include "../sof-audio.h"
#include "adsp_helper.h"
#include "mtk-adsp-common.h"

/**
 * mtk_adsp_get_registers() - This function is called in case of DSP oops
 * in order to gather information about the registers, filename and
 * linenumber and stack.
 * @sdev: SOF device
 * @xoops: Stores information about registers.
 * @panic_info: Stores information about filename and line number.
 * @stack: Stores the stack dump.
 * @stack_words: Size of the stack dump.
 */
static void mtk_adsp_get_registers(struct snd_sof_dev *sdev,
				   struct sof_ipc_dsp_oops_xtensa *xoops,
				   struct sof_ipc_panic_info *panic_info,
				   u32 *stack, size_t stack_words)
{
	u32 offset = sdev->dsp_oops_offset;

	/* first read registers */
	sof_mailbox_read(sdev, offset, xoops, sizeof(*xoops));

	/* then get panic info */
	if (xoops->arch_hdr.totalsize > EXCEPT_MAX_HDR_SIZE) {
		dev_err(sdev->dev, "invalid header size 0x%x\n",
			xoops->arch_hdr.totalsize);
		return;
	}
	offset += xoops->arch_hdr.totalsize;
	sof_mailbox_read(sdev, offset, panic_info, sizeof(*panic_info));

	/* then get the stack */
	offset += sizeof(*panic_info);
	sof_mailbox_read(sdev, offset, stack, stack_words * sizeof(u32));
}

/**
 * mtk_adsp_dump() - This function is called when a panic message is
 * received from the firmware.
 * @sdev: SOF device
 * @flags: parameter not used but required by ops prototype
 */
void mtk_adsp_dump(struct snd_sof_dev *sdev, u32 flags)
{
	char *level = (flags & SOF_DBG_DUMP_OPTIONAL) ? KERN_DEBUG : KERN_ERR;
	struct sof_ipc_dsp_oops_xtensa xoops;
	struct sof_ipc_panic_info panic_info = {};
	u32 stack[MTK_ADSP_STACK_DUMP_SIZE];
	u32 status;

	/* Get information about the panic status from the debug box area.
	 * Compute the trace point based on the status.
	 */
	sof_mailbox_read(sdev, sdev->debug_box.offset + 0x4, &status, 4);

	/* Get information about the registers, the filename and line
	 * number and the stack.
	 */
	mtk_adsp_get_registers(sdev, &xoops, &panic_info, stack,
			       MTK_ADSP_STACK_DUMP_SIZE);

	/* Print the information to the console */
	sof_print_oops_and_stack(sdev, level, status, status, &xoops, &panic_info,
				 stack, MTK_ADSP_STACK_DUMP_SIZE);
}
EXPORT_SYMBOL(mtk_adsp_dump);

/**
 * mtk_adsp_send_msg - Send message to Audio DSP
 * @sdev: SOF device
 * @msg: SOF IPC Message to send
 */
int mtk_adsp_send_msg(struct snd_sof_dev *sdev, struct snd_sof_ipc_msg *msg)
{
	struct adsp_priv *priv = sdev->pdata->hw_pdata;

	sof_mailbox_write(sdev, sdev->host_box.offset, msg->msg_data,
			  msg->msg_size);

	return mtk_adsp_ipc_send(priv->dsp_ipc, MTK_ADSP_IPC_REQ, MTK_ADSP_IPC_OP_REQ);
}
EXPORT_SYMBOL(mtk_adsp_send_msg);

/**
 * mtk_adsp_handle_reply - Handle reply from the Audio DSP through Mailbox
 * @ipc: ADSP IPC handle
 */
void mtk_adsp_handle_reply(struct mtk_adsp_ipc *ipc)
{
	struct adsp_priv *priv = mtk_adsp_ipc_get_data(ipc);
	unsigned long flags;

	spin_lock_irqsave(&priv->sdev->ipc_lock, flags);
	snd_sof_ipc_process_reply(priv->sdev, 0);
	spin_unlock_irqrestore(&priv->sdev->ipc_lock, flags);
}
EXPORT_SYMBOL(mtk_adsp_handle_reply);

/**
 * mtk_adsp_handle_request - Handle request from the Audio DSP through Mailbox
 * @ipc: ADSP IPC handle
 */
void mtk_adsp_handle_request(struct mtk_adsp_ipc *ipc)
{
	struct adsp_priv *priv = mtk_adsp_ipc_get_data(ipc);
	u32 panic_code;
	int ret;

	/* Read the message from the debug box. */
	sof_mailbox_read(priv->sdev, priv->sdev->debug_box.offset + 4,
			 &panic_code, sizeof(panic_code));

	/* Check to see if the message is a panic code 0x0dead*** */
	if ((panic_code & SOF_IPC_PANIC_MAGIC_MASK) == SOF_IPC_PANIC_MAGIC) {
		snd_sof_dsp_panic(priv->sdev, panic_code, true);
	} else {
		snd_sof_ipc_msgs_rx(priv->sdev);

		/* Tell DSP cmd is done */
		ret = mtk_adsp_ipc_send(priv->dsp_ipc, MTK_ADSP_IPC_RSP, MTK_ADSP_IPC_OP_RSP);
		if (ret)
			dev_err(priv->dev, "request send ipc failed");
	}
}
EXPORT_SYMBOL(mtk_adsp_handle_request);

/**
 * mtk_adsp_get_bar_index - Map section type with BAR idx
 * @sdev: SOF device
 * @type: Section type as described by snd_sof_fw_blk_type
 *
 * MediaTek Audio DSPs have a 1:1 match between type and BAR idx
 */
int mtk_adsp_get_bar_index(struct snd_sof_dev *sdev, u32 type)
{
	return type;
}
EXPORT_SYMBOL(mtk_adsp_get_bar_index);

/**
 * mtk_adsp_stream_pcm_hw_params - Platform specific host stream hw params
 * @sdev: SOF device
 * @substream: PCM Substream
 * @params: hw params
 * @platform_params: Platform specific SOF stream parameters
 */
int mtk_adsp_stream_pcm_hw_params(struct snd_sof_dev *sdev,
				  struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_sof_platform_stream_params *platform_params)
{
	platform_params->cont_update_posn = 1;
	return 0;
}
EXPORT_SYMBOL(mtk_adsp_stream_pcm_hw_params);

/**
 * mtk_adsp_stream_pcm_pointer - Get host stream pointer
 * @sdev: SOF device
 * @substream: PCM substream
 */
snd_pcm_uframes_t mtk_adsp_stream_pcm_pointer(struct snd_sof_dev *sdev,
					      struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_component *scomp = sdev->component;
	struct snd_sof_pcm_stream *stream;
	struct sof_ipc_stream_posn posn;
	struct snd_sof_pcm *spcm;
	snd_pcm_uframes_t pos;
	int ret;

	spcm = snd_sof_find_spcm_dai(scomp, rtd);
	if (!spcm) {
		dev_warn_ratelimited(sdev->dev, "warn: can't find PCM with DAI ID %d\n",
				     rtd->dai_link->id);
		return 0;
	}

	stream = &spcm->stream[substream->stream];
	ret = snd_sof_ipc_msg_data(sdev, stream, &posn, sizeof(posn));
	if (ret < 0) {
		dev_warn(sdev->dev, "failed to read stream position: %d\n", ret);
		return 0;
	}

	memcpy(&stream->posn, &posn, sizeof(posn));
	pos = spcm->stream[substream->stream].posn.host_posn;
	pos = bytes_to_frames(substream->runtime, pos);

	return pos;
}
EXPORT_SYMBOL(mtk_adsp_stream_pcm_pointer);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("SOF helpers for MTK ADSP platforms");
