/*
 *  sst_stream.c - Intel SST Driver for audio engine
 *
 *  Copyright (C) 2008-14 Intel Corp
 *  Authors:	Vinod Koul <vinod.koul@intel.com>
 *		Harsha Priya <priya.harsha@intel.com>
 *		Dharageswari R <dharageswari.r@intel.com>
 *		KP Jeeja <jeeja.kp@intel.com>
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */
#include <linux/pci.h>
#include <linux/firmware.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/pm_runtime.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/compress_driver.h>
#include <asm/platform_sst_audio.h>
#include "../sst-mfld-platform.h"
#include "sst.h"
#include "../../common/sst-dsp.h"

int sst_alloc_stream_mrfld(struct intel_sst_drv *sst_drv_ctx, void *params)
{
	struct snd_sst_alloc_mrfld alloc_param;
	struct snd_sst_params *str_params;
	struct snd_sst_tstamp fw_tstamp;
	struct stream_info *str_info;
	struct snd_sst_alloc_response *response;
	unsigned int str_id, pipe_id, task_id;
	int i, num_ch, ret = 0;
	void *data = NULL;

	dev_dbg(sst_drv_ctx->dev, "Enter\n");

	str_params = (struct snd_sst_params *)params;
	memset(&alloc_param, 0, sizeof(alloc_param));
	alloc_param.operation = str_params->ops;
	alloc_param.codec_type = str_params->codec;
	alloc_param.sg_count = str_params->aparams.sg_count;
	alloc_param.ring_buf_info[0].addr =
		str_params->aparams.ring_buf_info[0].addr;
	alloc_param.ring_buf_info[0].size =
		str_params->aparams.ring_buf_info[0].size;
	alloc_param.frag_size = str_params->aparams.frag_size;

	memcpy(&alloc_param.codec_params, &str_params->sparams,
			sizeof(struct snd_sst_stream_params));

	/*
	 * fill channel map params for multichannel support.
	 * Ideally channel map should be received from upper layers
	 * for multichannel support.
	 * Currently hardcoding as per FW reqm.
	 */
	num_ch = sst_get_num_channel(str_params);
	for (i = 0; i < 8; i++) {
		if (i < num_ch)
			alloc_param.codec_params.uc.pcm_params.channel_map[i] = i;
		else
			alloc_param.codec_params.uc.pcm_params.channel_map[i] = 0xFF;
	}

	str_id = str_params->stream_id;
	str_info = get_stream_info(sst_drv_ctx, str_id);
	if (str_info == NULL) {
		dev_err(sst_drv_ctx->dev, "get stream info returned null\n");
		return -EINVAL;
	}

	pipe_id = str_params->device_type;
	task_id = str_params->task;
	sst_drv_ctx->streams[str_id].pipe_id = pipe_id;
	sst_drv_ctx->streams[str_id].task_id = task_id;
	sst_drv_ctx->streams[str_id].num_ch = num_ch;

	if (sst_drv_ctx->info.lpe_viewpt_rqd)
		alloc_param.ts = sst_drv_ctx->info.mailbox_start +
			sst_drv_ctx->tstamp + (str_id * sizeof(fw_tstamp));
	else
		alloc_param.ts = sst_drv_ctx->mailbox_add +
			sst_drv_ctx->tstamp + (str_id * sizeof(fw_tstamp));

	dev_dbg(sst_drv_ctx->dev, "alloc tstamp location = 0x%x\n",
			alloc_param.ts);
	dev_dbg(sst_drv_ctx->dev, "assigned pipe id 0x%x to task %d\n",
			pipe_id, task_id);

	/* allocate device type context */
	sst_init_stream(&sst_drv_ctx->streams[str_id], alloc_param.codec_type,
			str_id, alloc_param.operation, 0);

	dev_dbg(sst_drv_ctx->dev, "Alloc for str %d pipe %#x\n",
			str_id, pipe_id);
	ret = sst_prepare_and_post_msg(sst_drv_ctx, task_id, IPC_CMD,
			IPC_IA_ALLOC_STREAM_MRFLD, pipe_id, sizeof(alloc_param),
			&alloc_param, &data, true, true, false, true);

	if (ret < 0) {
		dev_err(sst_drv_ctx->dev, "FW alloc failed ret %d\n", ret);
		/* alloc failed, so reset the state to uninit */
		str_info->status = STREAM_UN_INIT;
		str_id = ret;
	} else if (data) {
		response = (struct snd_sst_alloc_response *)data;
		ret = response->str_type.result;
		if (!ret)
			goto out;
		dev_err(sst_drv_ctx->dev, "FW alloc failed ret %d\n", ret);
		if (ret == SST_ERR_STREAM_IN_USE) {
			dev_err(sst_drv_ctx->dev,
				"FW not in clean state, send free for:%d\n", str_id);
			sst_free_stream(sst_drv_ctx, str_id);
		}
		str_id = -ret;
	}
out:
	kfree(data);
	return str_id;
}

/**
* sst_start_stream - Send msg for a starting stream
* @str_id:	 stream ID
*
* This function is called by any function which wants to start
* a stream.
*/
int sst_start_stream(struct intel_sst_drv *sst_drv_ctx, int str_id)
{
	int retval = 0;
	struct stream_info *str_info;
	u16 data = 0;

	dev_dbg(sst_drv_ctx->dev, "sst_start_stream for %d\n", str_id);
	str_info = get_stream_info(sst_drv_ctx, str_id);
	if (!str_info)
		return -EINVAL;
	if (str_info->status != STREAM_RUNNING)
		return -EBADRQC;

	retval = sst_prepare_and_post_msg(sst_drv_ctx, str_info->task_id,
			IPC_CMD, IPC_IA_START_STREAM_MRFLD, str_info->pipe_id,
			sizeof(u16), &data, NULL, true, true, true, false);

	return retval;
}

int sst_send_byte_stream_mrfld(struct intel_sst_drv *sst_drv_ctx,
		struct snd_sst_bytes_v2 *bytes)
{	struct ipc_post *msg = NULL;
	u32 length;
	int pvt_id, ret = 0;
	struct sst_block *block = NULL;

	dev_dbg(sst_drv_ctx->dev,
		"type:%u ipc_msg:%u block:%u task_id:%u pipe: %#x length:%#x\n",
		bytes->type, bytes->ipc_msg, bytes->block, bytes->task_id,
		bytes->pipe_id, bytes->len);

	if (sst_create_ipc_msg(&msg, true))
		return -ENOMEM;

	pvt_id = sst_assign_pvt_id(sst_drv_ctx);
	sst_fill_header_mrfld(&msg->mrfld_header, bytes->ipc_msg,
			bytes->task_id, 1, pvt_id);
	msg->mrfld_header.p.header_high.part.res_rqd = bytes->block;
	length = bytes->len;
	msg->mrfld_header.p.header_low_payload = length;
	dev_dbg(sst_drv_ctx->dev, "length is %d\n", length);
	memcpy(msg->mailbox_data, &bytes->bytes, bytes->len);
	if (bytes->block) {
		block = sst_create_block(sst_drv_ctx, bytes->ipc_msg, pvt_id);
		if (block == NULL) {
			kfree(msg);
			ret = -ENOMEM;
			goto out;
		}
	}

	sst_add_to_dispatch_list_and_post(sst_drv_ctx, msg);
	dev_dbg(sst_drv_ctx->dev, "msg->mrfld_header.p.header_low_payload:%d",
			msg->mrfld_header.p.header_low_payload);

	if (bytes->block) {
		ret = sst_wait_timeout(sst_drv_ctx, block);
		if (ret) {
			dev_err(sst_drv_ctx->dev, "fw returned err %d\n", ret);
			sst_free_block(sst_drv_ctx, block);
			goto out;
		}
	}
	if (bytes->type == SND_SST_BYTES_GET) {
		/*
		 * copy the reply and send back
		 * we need to update only sz and payload
		 */
		if (bytes->block) {
			unsigned char *r = block->data;

			dev_dbg(sst_drv_ctx->dev, "read back %d bytes",
					bytes->len);
			memcpy(bytes->bytes, r, bytes->len);
		}
	}
	if (bytes->block)
		sst_free_block(sst_drv_ctx, block);
out:
	test_and_clear_bit(pvt_id, &sst_drv_ctx->pvt_id);
	return ret;
}

/*
 * sst_pause_stream - Send msg for a pausing stream
 * @str_id:	 stream ID
 *
 * This function is called by any function which wants to pause
 * an already running stream.
 */
int sst_pause_stream(struct intel_sst_drv *sst_drv_ctx, int str_id)
{
	int retval = 0;
	struct stream_info *str_info;

	dev_dbg(sst_drv_ctx->dev, "SST DBG:sst_pause_stream for %d\n", str_id);
	str_info = get_stream_info(sst_drv_ctx, str_id);
	if (!str_info)
		return -EINVAL;
	if (str_info->status == STREAM_PAUSED)
		return 0;
	if (str_info->status == STREAM_RUNNING ||
		str_info->status == STREAM_INIT) {
		if (str_info->prev == STREAM_UN_INIT)
			return -EBADRQC;

		retval = sst_prepare_and_post_msg(sst_drv_ctx, str_info->task_id, IPC_CMD,
				IPC_IA_PAUSE_STREAM_MRFLD, str_info->pipe_id,
				0, NULL, NULL, true, true, false, true);

		if (retval == 0) {
			str_info->prev = str_info->status;
			str_info->status = STREAM_PAUSED;
		} else if (retval == SST_ERR_INVALID_STREAM_ID) {
			retval = -EINVAL;
			mutex_lock(&sst_drv_ctx->sst_lock);
			sst_clean_stream(str_info);
			mutex_unlock(&sst_drv_ctx->sst_lock);
		}
	} else {
		retval = -EBADRQC;
		dev_dbg(sst_drv_ctx->dev, "SST DBG:BADRQC for stream\n ");
	}

	return retval;
}

/**
 * sst_resume_stream - Send msg for resuming stream
 * @str_id:		stream ID
 *
 * This function is called by any function which wants to resume
 * an already paused stream.
 */
int sst_resume_stream(struct intel_sst_drv *sst_drv_ctx, int str_id)
{
	int retval = 0;
	struct stream_info *str_info;

	dev_dbg(sst_drv_ctx->dev, "SST DBG:sst_resume_stream for %d\n", str_id);
	str_info = get_stream_info(sst_drv_ctx, str_id);
	if (!str_info)
		return -EINVAL;
	if (str_info->status == STREAM_RUNNING)
			return 0;
	if (str_info->status == STREAM_PAUSED) {
		retval = sst_prepare_and_post_msg(sst_drv_ctx, str_info->task_id,
				IPC_CMD, IPC_IA_RESUME_STREAM_MRFLD,
				str_info->pipe_id, 0, NULL, NULL,
				true, true, false, true);

		if (!retval) {
			if (str_info->prev == STREAM_RUNNING)
				str_info->status = STREAM_RUNNING;
			else
				str_info->status = STREAM_INIT;
			str_info->prev = STREAM_PAUSED;
		} else if (retval == -SST_ERR_INVALID_STREAM_ID) {
			retval = -EINVAL;
			mutex_lock(&sst_drv_ctx->sst_lock);
			sst_clean_stream(str_info);
			mutex_unlock(&sst_drv_ctx->sst_lock);
		}
	} else {
		retval = -EBADRQC;
		dev_err(sst_drv_ctx->dev, "SST ERR: BADQRC for stream\n");
	}

	return retval;
}


/**
 * sst_drop_stream - Send msg for stopping stream
 * @str_id:		stream ID
 *
 * This function is called by any function which wants to stop
 * a stream.
 */
int sst_drop_stream(struct intel_sst_drv *sst_drv_ctx, int str_id)
{
	int retval = 0;
	struct stream_info *str_info;

	dev_dbg(sst_drv_ctx->dev, "SST DBG:sst_drop_stream for %d\n", str_id);
	str_info = get_stream_info(sst_drv_ctx, str_id);
	if (!str_info)
		return -EINVAL;

	if (str_info->status != STREAM_UN_INIT) {
		str_info->prev = STREAM_UN_INIT;
		str_info->status = STREAM_INIT;
		str_info->cumm_bytes = 0;
		retval = sst_prepare_and_post_msg(sst_drv_ctx, str_info->task_id,
				IPC_CMD, IPC_IA_DROP_STREAM_MRFLD,
				str_info->pipe_id, 0, NULL, NULL,
				true, true, true, false);
	} else {
		retval = -EBADRQC;
		dev_dbg(sst_drv_ctx->dev, "BADQRC for stream, state %x\n",
				str_info->status);
	}
	return retval;
}

/**
* sst_drain_stream - Send msg for draining stream
* @str_id:		stream ID
*
* This function is called by any function which wants to drain
* a stream.
*/
int sst_drain_stream(struct intel_sst_drv *sst_drv_ctx,
			int str_id, bool partial_drain)
{
	int retval = 0;
	struct stream_info *str_info;

	dev_dbg(sst_drv_ctx->dev, "SST DBG:sst_drain_stream for %d\n", str_id);
	str_info = get_stream_info(sst_drv_ctx, str_id);
	if (!str_info)
		return -EINVAL;
	if (str_info->status != STREAM_RUNNING &&
		str_info->status != STREAM_INIT &&
		str_info->status != STREAM_PAUSED) {
			dev_err(sst_drv_ctx->dev, "SST ERR: BADQRC for stream = %d\n",
				       str_info->status);
			return -EBADRQC;
	}

	retval = sst_prepare_and_post_msg(sst_drv_ctx, str_info->task_id, IPC_CMD,
			IPC_IA_DRAIN_STREAM_MRFLD, str_info->pipe_id,
			sizeof(u8), &partial_drain, NULL, true, true, false, false);
	/*
	 * with new non blocked drain implementation in core we dont need to
	 * wait for respsonse, and need to only invoke callback for drain
	 * complete
	 */

	return retval;
}

/**
 * sst_free_stream - Frees a stream
 * @str_id:		stream ID
 *
 * This function is called by any function which wants to free
 * a stream.
 */
int sst_free_stream(struct intel_sst_drv *sst_drv_ctx, int str_id)
{
	int retval = 0;
	struct stream_info *str_info;

	dev_dbg(sst_drv_ctx->dev, "SST DBG:sst_free_stream for %d\n", str_id);

	mutex_lock(&sst_drv_ctx->sst_lock);
	if (sst_drv_ctx->sst_state == SST_RESET) {
		mutex_unlock(&sst_drv_ctx->sst_lock);
		return -ENODEV;
	}
	mutex_unlock(&sst_drv_ctx->sst_lock);
	str_info = get_stream_info(sst_drv_ctx, str_id);
	if (!str_info)
		return -EINVAL;

	mutex_lock(&str_info->lock);
	if (str_info->status != STREAM_UN_INIT) {
		str_info->prev =  str_info->status;
		str_info->status = STREAM_UN_INIT;
		mutex_unlock(&str_info->lock);

		dev_dbg(sst_drv_ctx->dev, "Free for str %d pipe %#x\n",
				str_id, str_info->pipe_id);
		retval = sst_prepare_and_post_msg(sst_drv_ctx, str_info->task_id, IPC_CMD,
				IPC_IA_FREE_STREAM_MRFLD, str_info->pipe_id, 0,
				NULL, NULL, true, true, false, true);

		dev_dbg(sst_drv_ctx->dev, "sst: wait for free returned %d\n",
				retval);
		mutex_lock(&sst_drv_ctx->sst_lock);
		sst_clean_stream(str_info);
		mutex_unlock(&sst_drv_ctx->sst_lock);
		dev_dbg(sst_drv_ctx->dev, "SST DBG:Stream freed\n");
	} else {
		mutex_unlock(&str_info->lock);
		retval = -EBADRQC;
		dev_dbg(sst_drv_ctx->dev, "SST DBG:BADQRC for stream\n");
	}

	return retval;
}
