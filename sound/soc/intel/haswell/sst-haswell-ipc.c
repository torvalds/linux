/*
 *  Intel SST Haswell/Broadwell IPC Support
 *
 * Copyright (C) 2013, Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

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

#include "sst-haswell-ipc.h"
#include "../common/sst-dsp.h"
#include "../common/sst-dsp-priv.h"
#include "../common/sst-ipc.h"

/* Global Message - Generic */
#define IPC_GLB_TYPE_SHIFT	24
#define IPC_GLB_TYPE_MASK	(0x1f << IPC_GLB_TYPE_SHIFT)
#define IPC_GLB_TYPE(x)		(x << IPC_GLB_TYPE_SHIFT)

/* Global Message - Reply */
#define IPC_GLB_REPLY_SHIFT	0
#define IPC_GLB_REPLY_MASK	(0x1f << IPC_GLB_REPLY_SHIFT)
#define IPC_GLB_REPLY_TYPE(x)	(x << IPC_GLB_REPLY_TYPE_SHIFT)

/* Stream Message - Generic */
#define IPC_STR_TYPE_SHIFT	20
#define IPC_STR_TYPE_MASK	(0xf << IPC_STR_TYPE_SHIFT)
#define IPC_STR_TYPE(x)		(x << IPC_STR_TYPE_SHIFT)
#define IPC_STR_ID_SHIFT	16
#define IPC_STR_ID_MASK		(0xf << IPC_STR_ID_SHIFT)
#define IPC_STR_ID(x)		(x << IPC_STR_ID_SHIFT)

/* Stream Message - Reply */
#define IPC_STR_REPLY_SHIFT	0
#define IPC_STR_REPLY_MASK	(0x1f << IPC_STR_REPLY_SHIFT)

/* Stream Stage Message - Generic */
#define IPC_STG_TYPE_SHIFT	12
#define IPC_STG_TYPE_MASK	(0xf << IPC_STG_TYPE_SHIFT)
#define IPC_STG_TYPE(x)		(x << IPC_STG_TYPE_SHIFT)
#define IPC_STG_ID_SHIFT	10
#define IPC_STG_ID_MASK		(0x3 << IPC_STG_ID_SHIFT)
#define IPC_STG_ID(x)		(x << IPC_STG_ID_SHIFT)

/* Stream Stage Message - Reply */
#define IPC_STG_REPLY_SHIFT	0
#define IPC_STG_REPLY_MASK	(0x1f << IPC_STG_REPLY_SHIFT)

/* Debug Log Message - Generic */
#define IPC_LOG_OP_SHIFT	20
#define IPC_LOG_OP_MASK		(0xf << IPC_LOG_OP_SHIFT)
#define IPC_LOG_OP_TYPE(x)	(x << IPC_LOG_OP_SHIFT)
#define IPC_LOG_ID_SHIFT	16
#define IPC_LOG_ID_MASK		(0xf << IPC_LOG_ID_SHIFT)
#define IPC_LOG_ID(x)		(x << IPC_LOG_ID_SHIFT)

/* Module Message */
#define IPC_MODULE_OPERATION_SHIFT	20
#define IPC_MODULE_OPERATION_MASK	(0xf << IPC_MODULE_OPERATION_SHIFT)
#define IPC_MODULE_OPERATION(x)	(x << IPC_MODULE_OPERATION_SHIFT)

#define IPC_MODULE_ID_SHIFT	16
#define IPC_MODULE_ID_MASK	(0xf << IPC_MODULE_ID_SHIFT)
#define IPC_MODULE_ID(x)	(x << IPC_MODULE_ID_SHIFT)

/* IPC message timeout (msecs) */
#define IPC_TIMEOUT_MSECS	300
#define IPC_BOOT_MSECS		200
#define IPC_MSG_WAIT		0
#define IPC_MSG_NOWAIT		1

/* Firmware Ready Message */
#define IPC_FW_READY		(0x1 << 29)
#define IPC_STATUS_MASK		(0x3 << 30)

#define IPC_EMPTY_LIST_SIZE	8
#define IPC_MAX_STREAMS		4

/* Mailbox */
#define IPC_MAX_MAILBOX_BYTES	256

#define INVALID_STREAM_HW_ID	0xffffffff

/* Global Message - Types and Replies */
enum ipc_glb_type {
	IPC_GLB_GET_FW_VERSION = 0,		/* Retrieves firmware version */
	IPC_GLB_PERFORMANCE_MONITOR = 1,	/* Performance monitoring actions */
	IPC_GLB_ALLOCATE_STREAM = 3,		/* Request to allocate new stream */
	IPC_GLB_FREE_STREAM = 4,		/* Request to free stream */
	IPC_GLB_GET_FW_CAPABILITIES = 5,	/* Retrieves firmware capabilities */
	IPC_GLB_STREAM_MESSAGE = 6,		/* Message directed to stream or its stages */
	/* Request to store firmware context during D0->D3 transition */
	IPC_GLB_REQUEST_DUMP = 7,
	/* Request to restore firmware context during D3->D0 transition */
	IPC_GLB_RESTORE_CONTEXT = 8,
	IPC_GLB_GET_DEVICE_FORMATS = 9,		/* Set device format */
	IPC_GLB_SET_DEVICE_FORMATS = 10,	/* Get device format */
	IPC_GLB_SHORT_REPLY = 11,
	IPC_GLB_ENTER_DX_STATE = 12,
	IPC_GLB_GET_MIXER_STREAM_INFO = 13,	/* Request mixer stream params */
	IPC_GLB_DEBUG_LOG_MESSAGE = 14,		/* Message to or from the debug logger. */
	IPC_GLB_MODULE_OPERATION = 15,		/* Message to loadable fw module */
	IPC_GLB_REQUEST_TRANSFER = 16, 		/* < Request Transfer for host */
	IPC_GLB_MAX_IPC_MESSAGE_TYPE = 17,	/* Maximum message number */
};

enum ipc_glb_reply {
	IPC_GLB_REPLY_SUCCESS = 0,		/* The operation was successful. */
	IPC_GLB_REPLY_ERROR_INVALID_PARAM = 1,	/* Invalid parameter was passed. */
	IPC_GLB_REPLY_UNKNOWN_MESSAGE_TYPE = 2,	/* Uknown message type was resceived. */
	IPC_GLB_REPLY_OUT_OF_RESOURCES = 3,	/* No resources to satisfy the request. */
	IPC_GLB_REPLY_BUSY = 4,			/* The system or resource is busy. */
	IPC_GLB_REPLY_PENDING = 5,		/* The action was scheduled for processing.  */
	IPC_GLB_REPLY_FAILURE = 6,		/* Critical error happened. */
	IPC_GLB_REPLY_INVALID_REQUEST = 7,	/* Request can not be completed. */
	IPC_GLB_REPLY_STAGE_UNINITIALIZED = 8,	/* Processing stage was uninitialized. */
	IPC_GLB_REPLY_NOT_FOUND = 9,		/* Required resource can not be found. */
	IPC_GLB_REPLY_SOURCE_NOT_STARTED = 10,	/* Source was not started. */
};

enum ipc_module_operation {
	IPC_MODULE_NOTIFICATION = 0,
	IPC_MODULE_ENABLE = 1,
	IPC_MODULE_DISABLE = 2,
	IPC_MODULE_GET_PARAMETER = 3,
	IPC_MODULE_SET_PARAMETER = 4,
	IPC_MODULE_GET_INFO = 5,
	IPC_MODULE_MAX_MESSAGE
};

/* Stream Message - Types */
enum ipc_str_operation {
	IPC_STR_RESET = 0,
	IPC_STR_PAUSE = 1,
	IPC_STR_RESUME = 2,
	IPC_STR_STAGE_MESSAGE = 3,
	IPC_STR_NOTIFICATION = 4,
	IPC_STR_MAX_MESSAGE
};

/* Stream Stage Message Types */
enum ipc_stg_operation {
	IPC_STG_GET_VOLUME = 0,
	IPC_STG_SET_VOLUME,
	IPC_STG_SET_WRITE_POSITION,
	IPC_STG_SET_FX_ENABLE,
	IPC_STG_SET_FX_DISABLE,
	IPC_STG_SET_FX_GET_PARAM,
	IPC_STG_SET_FX_SET_PARAM,
	IPC_STG_SET_FX_GET_INFO,
	IPC_STG_MUTE_LOOPBACK,
	IPC_STG_MAX_MESSAGE
};

/* Stream Stage Message Types For Notification*/
enum ipc_stg_operation_notify {
	IPC_POSITION_CHANGED = 0,
	IPC_STG_GLITCH,
	IPC_STG_MAX_NOTIFY
};

enum ipc_glitch_type {
	IPC_GLITCH_UNDERRUN = 1,
	IPC_GLITCH_DECODER_ERROR,
	IPC_GLITCH_DOUBLED_WRITE_POS,
	IPC_GLITCH_MAX
};

/* Debug Control */
enum ipc_debug_operation {
	IPC_DEBUG_ENABLE_LOG = 0,
	IPC_DEBUG_DISABLE_LOG = 1,
	IPC_DEBUG_REQUEST_LOG_DUMP = 2,
	IPC_DEBUG_NOTIFY_LOG_DUMP = 3,
	IPC_DEBUG_MAX_DEBUG_LOG
};

/* Firmware Ready */
struct sst_hsw_ipc_fw_ready {
	u32 inbox_offset;
	u32 outbox_offset;
	u32 inbox_size;
	u32 outbox_size;
	u32 fw_info_size;
	u8 fw_info[IPC_MAX_MAILBOX_BYTES - 5 * sizeof(u32)];
} __attribute__((packed));

struct sst_hsw_stream;
struct sst_hsw;

/* Stream infomation */
struct sst_hsw_stream {
	/* configuration */
	struct sst_hsw_ipc_stream_alloc_req request;
	struct sst_hsw_ipc_stream_alloc_reply reply;
	struct sst_hsw_ipc_stream_free_req free_req;

	/* Mixer info */
	u32 mute_volume[SST_HSW_NO_CHANNELS];
	u32 mute[SST_HSW_NO_CHANNELS];

	/* runtime info */
	struct sst_hsw *hsw;
	int host_id;
	bool commited;
	bool running;

	/* Notification work */
	struct work_struct notify_work;
	u32 header;

	/* Position info from DSP */
	struct sst_hsw_ipc_stream_set_position wpos;
	struct sst_hsw_ipc_stream_get_position rpos;
	struct sst_hsw_ipc_stream_glitch_position glitch;

	/* Volume info */
	struct sst_hsw_ipc_volume_req vol_req;

	/* driver callback */
	u32 (*notify_position)(struct sst_hsw_stream *stream, void *data);
	void *pdata;

	/* record the fw read position when playback */
	snd_pcm_uframes_t old_position;
	bool play_silence;
	struct list_head node;
};

/* FW log ring information */
struct sst_hsw_log_stream {
	dma_addr_t dma_addr;
	unsigned char *dma_area;
	unsigned char *ring_descr;
	int pages;
	int size;

	/* Notification work */
	struct work_struct notify_work;
	wait_queue_head_t readers_wait_q;
	struct mutex rw_mutex;

	u32 last_pos;
	u32 curr_pos;
	u32 reader_pos;

	/* fw log config */
	u32 config[SST_HSW_FW_LOG_CONFIG_DWORDS];

	struct sst_hsw *hsw;
};

/* SST Haswell IPC data */
struct sst_hsw {
	struct device *dev;
	struct sst_dsp *dsp;
	struct platform_device *pdev_pcm;

	/* FW config */
	struct sst_hsw_ipc_fw_ready fw_ready;
	struct sst_hsw_ipc_fw_version version;
	bool fw_done;
	struct sst_fw *sst_fw;

	/* stream */
	struct list_head stream_list;

	/* global mixer */
	struct sst_hsw_ipc_stream_info_reply mixer_info;
	enum sst_hsw_volume_curve curve_type;
	u32 curve_duration;
	u32 mute[SST_HSW_NO_CHANNELS];
	u32 mute_volume[SST_HSW_NO_CHANNELS];

	/* DX */
	struct sst_hsw_ipc_dx_reply dx;
	void *dx_context;
	dma_addr_t dx_context_paddr;
	enum sst_hsw_device_id dx_dev;
	enum sst_hsw_device_mclk dx_mclk;
	enum sst_hsw_device_mode dx_mode;
	u32 dx_clock_divider;

	/* boot */
	wait_queue_head_t boot_wait;
	bool boot_complete;
	bool shutdown;

	/* IPC messaging */
	struct sst_generic_ipc ipc;

	/* FW log stream */
	struct sst_hsw_log_stream log_stream;

	/* flags bit field to track module state when resume from RTD3,
	 * each bit represent state (enabled/disabled) of single module */
	u32 enabled_modules_rtd3;

	/* buffer to store parameter lines */
	u32 param_idx_w;	/* write index */
	u32 param_idx_r;	/* read index */
	u8 param_buf[WAVES_PARAM_LINES][WAVES_PARAM_COUNT];
};

#define CREATE_TRACE_POINTS
#include <trace/events/hswadsp.h>

static inline u32 msg_get_global_type(u32 msg)
{
	return (msg & IPC_GLB_TYPE_MASK) >> IPC_GLB_TYPE_SHIFT;
}

static inline u32 msg_get_global_reply(u32 msg)
{
	return (msg & IPC_GLB_REPLY_MASK) >> IPC_GLB_REPLY_SHIFT;
}

static inline u32 msg_get_stream_type(u32 msg)
{
	return (msg & IPC_STR_TYPE_MASK) >>  IPC_STR_TYPE_SHIFT;
}

static inline u32 msg_get_stage_type(u32 msg)
{
	return (msg & IPC_STG_TYPE_MASK) >>  IPC_STG_TYPE_SHIFT;
}

static inline u32 msg_get_stream_id(u32 msg)
{
	return (msg & IPC_STR_ID_MASK) >>  IPC_STR_ID_SHIFT;
}

static inline u32 msg_get_notify_reason(u32 msg)
{
	return (msg & IPC_STG_TYPE_MASK) >> IPC_STG_TYPE_SHIFT;
}

static inline u32 msg_get_module_operation(u32 msg)
{
	return (msg & IPC_MODULE_OPERATION_MASK) >> IPC_MODULE_OPERATION_SHIFT;
}

static inline u32 msg_get_module_id(u32 msg)
{
	return (msg & IPC_MODULE_ID_MASK) >> IPC_MODULE_ID_SHIFT;
}

u32 create_channel_map(enum sst_hsw_channel_config config)
{
	switch (config) {
	case SST_HSW_CHANNEL_CONFIG_MONO:
		return (0xFFFFFFF0 | SST_HSW_CHANNEL_CENTER);
	case SST_HSW_CHANNEL_CONFIG_STEREO:
		return (0xFFFFFF00 | SST_HSW_CHANNEL_LEFT
			| (SST_HSW_CHANNEL_RIGHT << 4));
	case SST_HSW_CHANNEL_CONFIG_2_POINT_1:
		return (0xFFFFF000 | SST_HSW_CHANNEL_LEFT
			| (SST_HSW_CHANNEL_RIGHT << 4)
			| (SST_HSW_CHANNEL_LFE << 8 ));
	case SST_HSW_CHANNEL_CONFIG_3_POINT_0:
		return (0xFFFFF000 | SST_HSW_CHANNEL_LEFT
			| (SST_HSW_CHANNEL_CENTER << 4)
			| (SST_HSW_CHANNEL_RIGHT << 8));
	case SST_HSW_CHANNEL_CONFIG_3_POINT_1:
		return (0xFFFF0000 | SST_HSW_CHANNEL_LEFT
			| (SST_HSW_CHANNEL_CENTER << 4)
			| (SST_HSW_CHANNEL_RIGHT << 8)
			| (SST_HSW_CHANNEL_LFE << 12));
	case SST_HSW_CHANNEL_CONFIG_QUATRO:
		return (0xFFFF0000 | SST_HSW_CHANNEL_LEFT
			| (SST_HSW_CHANNEL_RIGHT << 4)
			| (SST_HSW_CHANNEL_LEFT_SURROUND << 8)
			| (SST_HSW_CHANNEL_RIGHT_SURROUND << 12));
	case SST_HSW_CHANNEL_CONFIG_4_POINT_0:
		return (0xFFFF0000 | SST_HSW_CHANNEL_LEFT
			| (SST_HSW_CHANNEL_CENTER << 4)
			| (SST_HSW_CHANNEL_RIGHT << 8)
			| (SST_HSW_CHANNEL_CENTER_SURROUND << 12));
	case SST_HSW_CHANNEL_CONFIG_5_POINT_0:
		return (0xFFF00000 | SST_HSW_CHANNEL_LEFT
			| (SST_HSW_CHANNEL_CENTER << 4)
			| (SST_HSW_CHANNEL_RIGHT << 8)
			| (SST_HSW_CHANNEL_LEFT_SURROUND << 12)
			| (SST_HSW_CHANNEL_RIGHT_SURROUND << 16));
	case SST_HSW_CHANNEL_CONFIG_5_POINT_1:
		return (0xFF000000 | SST_HSW_CHANNEL_CENTER
			| (SST_HSW_CHANNEL_LEFT << 4)
			| (SST_HSW_CHANNEL_RIGHT << 8)
			| (SST_HSW_CHANNEL_LEFT_SURROUND << 12)
			| (SST_HSW_CHANNEL_RIGHT_SURROUND << 16)
			| (SST_HSW_CHANNEL_LFE << 20));
	case SST_HSW_CHANNEL_CONFIG_DUAL_MONO:
		return (0xFFFFFF00 | SST_HSW_CHANNEL_LEFT
			| (SST_HSW_CHANNEL_LEFT << 4));
	default:
		return 0xFFFFFFFF;
	}
}

static struct sst_hsw_stream *get_stream_by_id(struct sst_hsw *hsw,
	int stream_id)
{
	struct sst_hsw_stream *stream;

	list_for_each_entry(stream, &hsw->stream_list, node) {
		if (stream->reply.stream_hw_id == stream_id)
			return stream;
	}

	return NULL;
}

static void hsw_fw_ready(struct sst_hsw *hsw, u32 header)
{
	struct sst_hsw_ipc_fw_ready fw_ready;
	u32 offset;
	u8 fw_info[IPC_MAX_MAILBOX_BYTES - 5 * sizeof(u32)];
	char *tmp[5], *pinfo;
	int i = 0;

	offset = (header & 0x1FFFFFFF) << 3;

	dev_dbg(hsw->dev, "ipc: DSP is ready 0x%8.8x offset %d\n",
		header, offset);

	/* copy data from the DSP FW ready offset */
	sst_dsp_read(hsw->dsp, &fw_ready, offset, sizeof(fw_ready));

	sst_dsp_mailbox_init(hsw->dsp, fw_ready.inbox_offset,
		fw_ready.inbox_size, fw_ready.outbox_offset,
		fw_ready.outbox_size);

	hsw->boot_complete = true;
	wake_up(&hsw->boot_wait);

	dev_dbg(hsw->dev, " mailbox upstream 0x%x - size 0x%x\n",
		fw_ready.inbox_offset, fw_ready.inbox_size);
	dev_dbg(hsw->dev, " mailbox downstream 0x%x - size 0x%x\n",
		fw_ready.outbox_offset, fw_ready.outbox_size);
	if (fw_ready.fw_info_size < sizeof(fw_ready.fw_info)) {
		fw_ready.fw_info[fw_ready.fw_info_size] = 0;
		dev_dbg(hsw->dev, " Firmware info: %s \n", fw_ready.fw_info);

		/* log the FW version info got from the mailbox here. */
		memcpy(fw_info, fw_ready.fw_info, fw_ready.fw_info_size);
		pinfo = &fw_info[0];
		for (i = 0; i < ARRAY_SIZE(tmp); i++)
			tmp[i] = strsep(&pinfo, " ");
		dev_info(hsw->dev, "FW loaded, mailbox readback FW info: type %s, - "
			"version: %s.%s, build %s, source commit id: %s\n",
			tmp[0], tmp[1], tmp[2], tmp[3], tmp[4]);
	}
}

static void hsw_notification_work(struct work_struct *work)
{
	struct sst_hsw_stream *stream = container_of(work,
			struct sst_hsw_stream, notify_work);
	struct sst_hsw_ipc_stream_glitch_position *glitch = &stream->glitch;
	struct sst_hsw_ipc_stream_get_position *pos = &stream->rpos;
	struct sst_hsw *hsw = stream->hsw;
	u32 reason;

	reason = msg_get_notify_reason(stream->header);

	switch (reason) {
	case IPC_STG_GLITCH:
		trace_ipc_notification("DSP stream under/overrun",
			stream->reply.stream_hw_id);
		sst_dsp_inbox_read(hsw->dsp, glitch, sizeof(*glitch));

		dev_err(hsw->dev, "glitch %d pos 0x%x write pos 0x%x\n",
			glitch->glitch_type, glitch->present_pos,
			glitch->write_pos);
		break;

	case IPC_POSITION_CHANGED:
		trace_ipc_notification("DSP stream position changed for",
			stream->reply.stream_hw_id);
		sst_dsp_inbox_read(hsw->dsp, pos, sizeof(*pos));

		if (stream->notify_position)
			stream->notify_position(stream, stream->pdata);

		break;
	default:
		dev_err(hsw->dev, "error: unknown notification 0x%x\n",
			stream->header);
		break;
	}

	/* tell DSP that notification has been handled */
	sst_dsp_shim_update_bits(hsw->dsp, SST_IPCD,
		SST_IPCD_BUSY | SST_IPCD_DONE, SST_IPCD_DONE);

	/* unmask busy interrupt */
	sst_dsp_shim_update_bits(hsw->dsp, SST_IMRX, SST_IMRX_BUSY, 0);
}

static void hsw_stream_update(struct sst_hsw *hsw, struct ipc_message *msg)
{
	struct sst_hsw_stream *stream;
	u32 header = msg->header & ~(IPC_STATUS_MASK | IPC_GLB_REPLY_MASK);
	u32 stream_id = msg_get_stream_id(header);
	u32 stream_msg = msg_get_stream_type(header);

	stream = get_stream_by_id(hsw, stream_id);
	if (stream == NULL)
		return;

	switch (stream_msg) {
	case IPC_STR_STAGE_MESSAGE:
	case IPC_STR_NOTIFICATION:
		break;
	case IPC_STR_RESET:
		trace_ipc_notification("stream reset", stream->reply.stream_hw_id);
		break;
	case IPC_STR_PAUSE:
		stream->running = false;
		trace_ipc_notification("stream paused",
			stream->reply.stream_hw_id);
		break;
	case IPC_STR_RESUME:
		stream->running = true;
		trace_ipc_notification("stream running",
			stream->reply.stream_hw_id);
		break;
	}
}

static int hsw_process_reply(struct sst_hsw *hsw, u32 header)
{
	struct ipc_message *msg;
	u32 reply = msg_get_global_reply(header);

	trace_ipc_reply("processing -->", header);

	msg = sst_ipc_reply_find_msg(&hsw->ipc, header);
	if (msg == NULL) {
		trace_ipc_error("error: can't find message header", header);
		return -EIO;
	}

	/* first process the header */
	switch (reply) {
	case IPC_GLB_REPLY_PENDING:
		trace_ipc_pending_reply("received", header);
		msg->pending = true;
		hsw->ipc.pending = true;
		return 1;
	case IPC_GLB_REPLY_SUCCESS:
		if (msg->pending) {
			trace_ipc_pending_reply("completed", header);
			sst_dsp_inbox_read(hsw->dsp, msg->rx_data,
				msg->rx_size);
			hsw->ipc.pending = false;
		} else {
			/* copy data from the DSP */
			sst_dsp_outbox_read(hsw->dsp, msg->rx_data,
				msg->rx_size);
		}
		break;
	/* these will be rare - but useful for debug */
	case IPC_GLB_REPLY_UNKNOWN_MESSAGE_TYPE:
		trace_ipc_error("error: unknown message type", header);
		msg->errno = -EBADMSG;
		break;
	case IPC_GLB_REPLY_OUT_OF_RESOURCES:
		trace_ipc_error("error: out of resources", header);
		msg->errno = -ENOMEM;
		break;
	case IPC_GLB_REPLY_BUSY:
		trace_ipc_error("error: reply busy", header);
		msg->errno = -EBUSY;
		break;
	case IPC_GLB_REPLY_FAILURE:
		trace_ipc_error("error: reply failure", header);
		msg->errno = -EINVAL;
		break;
	case IPC_GLB_REPLY_STAGE_UNINITIALIZED:
		trace_ipc_error("error: stage uninitialized", header);
		msg->errno = -EINVAL;
		break;
	case IPC_GLB_REPLY_NOT_FOUND:
		trace_ipc_error("error: reply not found", header);
		msg->errno = -EINVAL;
		break;
	case IPC_GLB_REPLY_SOURCE_NOT_STARTED:
		trace_ipc_error("error: source not started", header);
		msg->errno = -EINVAL;
		break;
	case IPC_GLB_REPLY_INVALID_REQUEST:
		trace_ipc_error("error: invalid request", header);
		msg->errno = -EINVAL;
		break;
	case IPC_GLB_REPLY_ERROR_INVALID_PARAM:
		trace_ipc_error("error: invalid parameter", header);
		msg->errno = -EINVAL;
		break;
	default:
		trace_ipc_error("error: unknown reply", header);
		msg->errno = -EINVAL;
		break;
	}

	/* update any stream states */
	if (msg_get_global_type(header) == IPC_GLB_STREAM_MESSAGE)
		hsw_stream_update(hsw, msg);

	/* wake up and return the error if we have waiters on this message ? */
	list_del(&msg->list);
	sst_ipc_tx_msg_reply_complete(&hsw->ipc, msg);

	return 1;
}

static int hsw_module_message(struct sst_hsw *hsw, u32 header)
{
	u32 operation, module_id;
	int handled = 0;

	operation = msg_get_module_operation(header);
	module_id = msg_get_module_id(header);
	dev_dbg(hsw->dev, "received module message header: 0x%8.8x\n",
			header);
	dev_dbg(hsw->dev, "operation: 0x%8.8x module_id: 0x%8.8x\n",
			operation, module_id);

	switch (operation) {
	case IPC_MODULE_NOTIFICATION:
		dev_dbg(hsw->dev, "module notification received");
		handled = 1;
		break;
	default:
		handled = hsw_process_reply(hsw, header);
		break;
	}

	return handled;
}

static int hsw_stream_message(struct sst_hsw *hsw, u32 header)
{
	u32 stream_msg, stream_id, stage_type;
	struct sst_hsw_stream *stream;
	int handled = 0;

	stream_msg = msg_get_stream_type(header);
	stream_id = msg_get_stream_id(header);
	stage_type = msg_get_stage_type(header);

	stream = get_stream_by_id(hsw, stream_id);
	if (stream == NULL)
		return handled;

	stream->header = header;

	switch (stream_msg) {
	case IPC_STR_STAGE_MESSAGE:
		dev_err(hsw->dev, "error: stage msg not implemented 0x%8.8x\n",
			header);
		break;
	case IPC_STR_NOTIFICATION:
		schedule_work(&stream->notify_work);
		break;
	default:
		/* handle pending message complete request */
		handled = hsw_process_reply(hsw, header);
		break;
	}

	return handled;
}

static int hsw_log_message(struct sst_hsw *hsw, u32 header)
{
	u32 operation = (header & IPC_LOG_OP_MASK) >>  IPC_LOG_OP_SHIFT;
	struct sst_hsw_log_stream *stream = &hsw->log_stream;
	int ret = 1;

	if (operation != IPC_DEBUG_REQUEST_LOG_DUMP) {
		dev_err(hsw->dev,
			"error: log msg not implemented 0x%8.8x\n", header);
		return 0;
	}

	mutex_lock(&stream->rw_mutex);
	stream->last_pos = stream->curr_pos;
	sst_dsp_inbox_read(
		hsw->dsp, &stream->curr_pos, sizeof(stream->curr_pos));
	mutex_unlock(&stream->rw_mutex);

	schedule_work(&stream->notify_work);

	return ret;
}

static int hsw_process_notification(struct sst_hsw *hsw)
{
	struct sst_dsp *sst = hsw->dsp;
	u32 type, header;
	int handled = 1;

	header = sst_dsp_shim_read_unlocked(sst, SST_IPCD);
	type = msg_get_global_type(header);

	trace_ipc_request("processing -->", header);

	/* FW Ready is a special case */
	if (!hsw->boot_complete && header & IPC_FW_READY) {
		hsw_fw_ready(hsw, header);
		return handled;
	}

	switch (type) {
	case IPC_GLB_GET_FW_VERSION:
	case IPC_GLB_ALLOCATE_STREAM:
	case IPC_GLB_FREE_STREAM:
	case IPC_GLB_GET_FW_CAPABILITIES:
	case IPC_GLB_REQUEST_DUMP:
	case IPC_GLB_GET_DEVICE_FORMATS:
	case IPC_GLB_SET_DEVICE_FORMATS:
	case IPC_GLB_ENTER_DX_STATE:
	case IPC_GLB_GET_MIXER_STREAM_INFO:
	case IPC_GLB_MAX_IPC_MESSAGE_TYPE:
	case IPC_GLB_RESTORE_CONTEXT:
	case IPC_GLB_SHORT_REPLY:
		dev_err(hsw->dev, "error: message type %d header 0x%x\n",
			type, header);
		break;
	case IPC_GLB_STREAM_MESSAGE:
		handled = hsw_stream_message(hsw, header);
		break;
	case IPC_GLB_DEBUG_LOG_MESSAGE:
		handled = hsw_log_message(hsw, header);
		break;
	case IPC_GLB_MODULE_OPERATION:
		handled = hsw_module_message(hsw, header);
		break;
	default:
		dev_err(hsw->dev, "error: unexpected type %d hdr 0x%8.8x\n",
			type, header);
		break;
	}

	return handled;
}

static irqreturn_t hsw_irq_thread(int irq, void *context)
{
	struct sst_dsp *sst = (struct sst_dsp *) context;
	struct sst_hsw *hsw = sst_dsp_get_thread_context(sst);
	struct sst_generic_ipc *ipc = &hsw->ipc;
	u32 ipcx, ipcd;
	unsigned long flags;

	spin_lock_irqsave(&sst->spinlock, flags);

	ipcx = sst_dsp_ipc_msg_rx(hsw->dsp);
	ipcd = sst_dsp_shim_read_unlocked(sst, SST_IPCD);

	/* reply message from DSP */
	if (ipcx & SST_IPCX_DONE) {

		/* Handle Immediate reply from DSP Core */
		hsw_process_reply(hsw, ipcx);

		/* clear DONE bit - tell DSP we have completed */
		sst_dsp_shim_update_bits_unlocked(sst, SST_IPCX,
			SST_IPCX_DONE, 0);

		/* unmask Done interrupt */
		sst_dsp_shim_update_bits_unlocked(sst, SST_IMRX,
			SST_IMRX_DONE, 0);
	}

	/* new message from DSP */
	if (ipcd & SST_IPCD_BUSY) {

		/* Handle Notification and Delayed reply from DSP Core */
		hsw_process_notification(hsw);

		/* clear BUSY bit and set DONE bit - accept new messages */
		sst_dsp_shim_update_bits_unlocked(sst, SST_IPCD,
			SST_IPCD_BUSY | SST_IPCD_DONE, SST_IPCD_DONE);

		/* unmask busy interrupt */
		sst_dsp_shim_update_bits_unlocked(sst, SST_IMRX,
			SST_IMRX_BUSY, 0);
	}

	spin_unlock_irqrestore(&sst->spinlock, flags);

	/* continue to send any remaining messages... */
	queue_kthread_work(&ipc->kworker, &ipc->kwork);

	return IRQ_HANDLED;
}

int sst_hsw_fw_get_version(struct sst_hsw *hsw,
	struct sst_hsw_ipc_fw_version *version)
{
	int ret;

	ret = sst_ipc_tx_message_wait(&hsw->ipc,
		IPC_GLB_TYPE(IPC_GLB_GET_FW_VERSION),
		NULL, 0, version, sizeof(*version));
	if (ret < 0)
		dev_err(hsw->dev, "error: get version failed\n");

	return ret;
}

/* Mixer Controls */
int sst_hsw_stream_get_volume(struct sst_hsw *hsw, struct sst_hsw_stream *stream,
	u32 stage_id, u32 channel, u32 *volume)
{
	if (channel > 1)
		return -EINVAL;

	sst_dsp_read(hsw->dsp, volume,
		stream->reply.volume_register_address[channel],
		sizeof(*volume));

	return 0;
}

/* stream volume */
int sst_hsw_stream_set_volume(struct sst_hsw *hsw,
	struct sst_hsw_stream *stream, u32 stage_id, u32 channel, u32 volume)
{
	struct sst_hsw_ipc_volume_req *req;
	u32 header;
	int ret;

	trace_ipc_request("set stream volume", stream->reply.stream_hw_id);

	if (channel >= 2 && channel != SST_HSW_CHANNELS_ALL)
		return -EINVAL;

	header = IPC_GLB_TYPE(IPC_GLB_STREAM_MESSAGE) |
		IPC_STR_TYPE(IPC_STR_STAGE_MESSAGE);
	header |= (stream->reply.stream_hw_id << IPC_STR_ID_SHIFT);
	header |= (IPC_STG_SET_VOLUME << IPC_STG_TYPE_SHIFT);
	header |= (stage_id << IPC_STG_ID_SHIFT);

	req = &stream->vol_req;
	req->target_volume = volume;

	/* set both at same time ? */
	if (channel == SST_HSW_CHANNELS_ALL) {
		if (hsw->mute[0] && hsw->mute[1]) {
			hsw->mute_volume[0] = hsw->mute_volume[1] = volume;
			return 0;
		} else if (hsw->mute[0])
			req->channel = 1;
		else if (hsw->mute[1])
			req->channel = 0;
		else
			req->channel = SST_HSW_CHANNELS_ALL;
	} else {
		/* set only 1 channel */
		if (hsw->mute[channel]) {
			hsw->mute_volume[channel] = volume;
			return 0;
		}
		req->channel = channel;
	}

	ret = sst_ipc_tx_message_wait(&hsw->ipc, header, req,
		sizeof(*req), NULL, 0);
	if (ret < 0) {
		dev_err(hsw->dev, "error: set stream volume failed\n");
		return ret;
	}

	return 0;
}

int sst_hsw_mixer_get_volume(struct sst_hsw *hsw, u32 stage_id, u32 channel,
	u32 *volume)
{
	if (channel > 1)
		return -EINVAL;

	sst_dsp_read(hsw->dsp, volume,
		hsw->mixer_info.volume_register_address[channel],
		sizeof(*volume));

	return 0;
}

/* global mixer volume */
int sst_hsw_mixer_set_volume(struct sst_hsw *hsw, u32 stage_id, u32 channel,
	u32 volume)
{
	struct sst_hsw_ipc_volume_req req;
	u32 header;
	int ret;

	trace_ipc_request("set mixer volume", volume);

	if (channel >= 2 && channel != SST_HSW_CHANNELS_ALL)
		return -EINVAL;

	/* set both at same time ? */
	if (channel == SST_HSW_CHANNELS_ALL) {
		if (hsw->mute[0] && hsw->mute[1]) {
			hsw->mute_volume[0] = hsw->mute_volume[1] = volume;
			return 0;
		} else if (hsw->mute[0])
			req.channel = 1;
		else if (hsw->mute[1])
			req.channel = 0;
		else
			req.channel = SST_HSW_CHANNELS_ALL;
	} else {
		/* set only 1 channel */
		if (hsw->mute[channel]) {
			hsw->mute_volume[channel] = volume;
			return 0;
		}
		req.channel = channel;
	}

	header = IPC_GLB_TYPE(IPC_GLB_STREAM_MESSAGE) |
		IPC_STR_TYPE(IPC_STR_STAGE_MESSAGE);
	header |= (hsw->mixer_info.mixer_hw_id << IPC_STR_ID_SHIFT);
	header |= (IPC_STG_SET_VOLUME << IPC_STG_TYPE_SHIFT);
	header |= (stage_id << IPC_STG_ID_SHIFT);

	req.curve_duration = hsw->curve_duration;
	req.curve_type = hsw->curve_type;
	req.target_volume = volume;

	ret = sst_ipc_tx_message_wait(&hsw->ipc, header, &req,
		sizeof(req), NULL, 0);
	if (ret < 0) {
		dev_err(hsw->dev, "error: set mixer volume failed\n");
		return ret;
	}

	return 0;
}

/* Stream API */
struct sst_hsw_stream *sst_hsw_stream_new(struct sst_hsw *hsw, int id,
	u32 (*notify_position)(struct sst_hsw_stream *stream, void *data),
	void *data)
{
	struct sst_hsw_stream *stream;
	struct sst_dsp *sst = hsw->dsp;
	unsigned long flags;

	stream = kzalloc(sizeof(*stream), GFP_KERNEL);
	if (stream == NULL)
		return NULL;

	spin_lock_irqsave(&sst->spinlock, flags);
	stream->reply.stream_hw_id = INVALID_STREAM_HW_ID;
	list_add(&stream->node, &hsw->stream_list);
	stream->notify_position = notify_position;
	stream->pdata = data;
	stream->hsw = hsw;
	stream->host_id = id;

	/* work to process notification messages */
	INIT_WORK(&stream->notify_work, hsw_notification_work);
	spin_unlock_irqrestore(&sst->spinlock, flags);

	return stream;
}

int sst_hsw_stream_free(struct sst_hsw *hsw, struct sst_hsw_stream *stream)
{
	u32 header;
	int ret = 0;
	struct sst_dsp *sst = hsw->dsp;
	unsigned long flags;

	if (!stream) {
		dev_warn(hsw->dev, "warning: stream is NULL, no stream to free, ignore it.\n");
		return 0;
	}

	/* dont free DSP streams that are not commited */
	if (!stream->commited)
		goto out;

	trace_ipc_request("stream free", stream->host_id);

	stream->free_req.stream_id = stream->reply.stream_hw_id;
	header = IPC_GLB_TYPE(IPC_GLB_FREE_STREAM);

	ret = sst_ipc_tx_message_wait(&hsw->ipc, header, &stream->free_req,
		sizeof(stream->free_req), NULL, 0);
	if (ret < 0) {
		dev_err(hsw->dev, "error: free stream %d failed\n",
			stream->free_req.stream_id);
		return -EAGAIN;
	}

	trace_hsw_stream_free_req(stream, &stream->free_req);

out:
	cancel_work_sync(&stream->notify_work);
	spin_lock_irqsave(&sst->spinlock, flags);
	list_del(&stream->node);
	kfree(stream);
	spin_unlock_irqrestore(&sst->spinlock, flags);

	return ret;
}

int sst_hsw_stream_set_bits(struct sst_hsw *hsw,
	struct sst_hsw_stream *stream, enum sst_hsw_bitdepth bits)
{
	if (stream->commited) {
		dev_err(hsw->dev, "error: stream committed for set bits\n");
		return -EINVAL;
	}

	stream->request.format.bitdepth = bits;
	return 0;
}

int sst_hsw_stream_set_channels(struct sst_hsw *hsw,
	struct sst_hsw_stream *stream, int channels)
{
	if (stream->commited) {
		dev_err(hsw->dev, "error: stream committed for set channels\n");
		return -EINVAL;
	}

	stream->request.format.ch_num = channels;
	return 0;
}

int sst_hsw_stream_set_rate(struct sst_hsw *hsw,
	struct sst_hsw_stream *stream, int rate)
{
	if (stream->commited) {
		dev_err(hsw->dev, "error: stream committed for set rate\n");
		return -EINVAL;
	}

	stream->request.format.frequency = rate;
	return 0;
}

int sst_hsw_stream_set_map_config(struct sst_hsw *hsw,
	struct sst_hsw_stream *stream, u32 map,
	enum sst_hsw_channel_config config)
{
	if (stream->commited) {
		dev_err(hsw->dev, "error: stream committed for set map\n");
		return -EINVAL;
	}

	stream->request.format.map = map;
	stream->request.format.config = config;
	return 0;
}

int sst_hsw_stream_set_style(struct sst_hsw *hsw,
	struct sst_hsw_stream *stream, enum sst_hsw_interleaving style)
{
	if (stream->commited) {
		dev_err(hsw->dev, "error: stream committed for set style\n");
		return -EINVAL;
	}

	stream->request.format.style = style;
	return 0;
}

int sst_hsw_stream_set_valid(struct sst_hsw *hsw,
	struct sst_hsw_stream *stream, u32 bits)
{
	if (stream->commited) {
		dev_err(hsw->dev, "error: stream committed for set valid bits\n");
		return -EINVAL;
	}

	stream->request.format.valid_bit = bits;
	return 0;
}

/* Stream Configuration */
int sst_hsw_stream_format(struct sst_hsw *hsw, struct sst_hsw_stream *stream,
	enum sst_hsw_stream_path_id path_id,
	enum sst_hsw_stream_type stream_type,
	enum sst_hsw_stream_format format_id)
{
	if (stream->commited) {
		dev_err(hsw->dev, "error: stream committed for set format\n");
		return -EINVAL;
	}

	stream->request.path_id = path_id;
	stream->request.stream_type = stream_type;
	stream->request.format_id = format_id;

	trace_hsw_stream_alloc_request(stream, &stream->request);

	return 0;
}

int sst_hsw_stream_buffer(struct sst_hsw *hsw, struct sst_hsw_stream *stream,
	u32 ring_pt_address, u32 num_pages,
	u32 ring_size, u32 ring_offset, u32 ring_first_pfn)
{
	if (stream->commited) {
		dev_err(hsw->dev, "error: stream committed for buffer\n");
		return -EINVAL;
	}

	stream->request.ringinfo.ring_pt_address = ring_pt_address;
	stream->request.ringinfo.num_pages = num_pages;
	stream->request.ringinfo.ring_size = ring_size;
	stream->request.ringinfo.ring_offset = ring_offset;
	stream->request.ringinfo.ring_first_pfn = ring_first_pfn;

	trace_hsw_stream_buffer(stream);

	return 0;
}

int sst_hsw_stream_set_module_info(struct sst_hsw *hsw,
	struct sst_hsw_stream *stream, struct sst_module_runtime *runtime)
{
	struct sst_hsw_module_map *map = &stream->request.map;
	struct sst_dsp *dsp = sst_hsw_get_dsp(hsw);
	struct sst_module *module = runtime->module;

	if (stream->commited) {
		dev_err(hsw->dev, "error: stream committed for set module\n");
		return -EINVAL;
	}

	/* only support initial module atm */
	map->module_entries_count = 1;
	map->module_entries[0].module_id = module->id;
	map->module_entries[0].entry_point = module->entry;

	stream->request.persistent_mem.offset =
		sst_dsp_get_offset(dsp, runtime->persistent_offset, SST_MEM_DRAM);
	stream->request.persistent_mem.size = module->persistent_size;

	stream->request.scratch_mem.offset =
		sst_dsp_get_offset(dsp, dsp->scratch_offset, SST_MEM_DRAM);
	stream->request.scratch_mem.size = dsp->scratch_size;

	dev_dbg(hsw->dev, "module %d runtime %d using:\n", module->id,
		runtime->id);
	dev_dbg(hsw->dev, " persistent offset 0x%x bytes 0x%x\n",
		stream->request.persistent_mem.offset,
		stream->request.persistent_mem.size);
	dev_dbg(hsw->dev, " scratch offset 0x%x bytes 0x%x\n",
		stream->request.scratch_mem.offset,
		stream->request.scratch_mem.size);

	return 0;
}

int sst_hsw_stream_commit(struct sst_hsw *hsw, struct sst_hsw_stream *stream)
{
	struct sst_hsw_ipc_stream_alloc_req *str_req = &stream->request;
	struct sst_hsw_ipc_stream_alloc_reply *reply = &stream->reply;
	u32 header;
	int ret;

	if (!stream) {
		dev_warn(hsw->dev, "warning: stream is NULL, no stream to commit, ignore it.\n");
		return 0;
	}

	if (stream->commited) {
		dev_warn(hsw->dev, "warning: stream is already committed, ignore it.\n");
		return 0;
	}

	trace_ipc_request("stream alloc", stream->host_id);

	header = IPC_GLB_TYPE(IPC_GLB_ALLOCATE_STREAM);

	ret = sst_ipc_tx_message_wait(&hsw->ipc, header, str_req,
		sizeof(*str_req), reply, sizeof(*reply));
	if (ret < 0) {
		dev_err(hsw->dev, "error: stream commit failed\n");
		return ret;
	}

	stream->commited = 1;
	trace_hsw_stream_alloc_reply(stream);

	return 0;
}

snd_pcm_uframes_t sst_hsw_stream_get_old_position(struct sst_hsw *hsw,
	struct sst_hsw_stream *stream)
{
	return stream->old_position;
}

void sst_hsw_stream_set_old_position(struct sst_hsw *hsw,
	struct sst_hsw_stream *stream, snd_pcm_uframes_t val)
{
	stream->old_position = val;
}

bool sst_hsw_stream_get_silence_start(struct sst_hsw *hsw,
	struct sst_hsw_stream *stream)
{
	return stream->play_silence;
}

void sst_hsw_stream_set_silence_start(struct sst_hsw *hsw,
	struct sst_hsw_stream *stream, bool val)
{
	stream->play_silence = val;
}

/* Stream Information - these calls could be inline but we want the IPC
 ABI to be opaque to client PCM drivers to cope with any future ABI changes */
int sst_hsw_mixer_get_info(struct sst_hsw *hsw)
{
	struct sst_hsw_ipc_stream_info_reply *reply;
	u32 header;
	int ret;

	reply = &hsw->mixer_info;
	header = IPC_GLB_TYPE(IPC_GLB_GET_MIXER_STREAM_INFO);

	trace_ipc_request("get global mixer info", 0);

	ret = sst_ipc_tx_message_wait(&hsw->ipc, header, NULL, 0,
		reply, sizeof(*reply));
	if (ret < 0) {
		dev_err(hsw->dev, "error: get stream info failed\n");
		return ret;
	}

	trace_hsw_mixer_info_reply(reply);

	return 0;
}

/* Send stream command */
static int sst_hsw_stream_operations(struct sst_hsw *hsw, int type,
	int stream_id, int wait)
{
	u32 header;

	header = IPC_GLB_TYPE(IPC_GLB_STREAM_MESSAGE) | IPC_STR_TYPE(type);
	header |= (stream_id << IPC_STR_ID_SHIFT);

	if (wait)
		return sst_ipc_tx_message_wait(&hsw->ipc, header,
			NULL, 0, NULL, 0);
	else
		return sst_ipc_tx_message_nowait(&hsw->ipc, header, NULL, 0);
}

/* Stream ALSA trigger operations */
int sst_hsw_stream_pause(struct sst_hsw *hsw, struct sst_hsw_stream *stream,
	int wait)
{
	int ret;

	if (!stream) {
		dev_warn(hsw->dev, "warning: stream is NULL, no stream to pause, ignore it.\n");
		return 0;
	}

	trace_ipc_request("stream pause", stream->reply.stream_hw_id);

	ret = sst_hsw_stream_operations(hsw, IPC_STR_PAUSE,
		stream->reply.stream_hw_id, wait);
	if (ret < 0)
		dev_err(hsw->dev, "error: failed to pause stream %d\n",
			stream->reply.stream_hw_id);

	return ret;
}

int sst_hsw_stream_resume(struct sst_hsw *hsw, struct sst_hsw_stream *stream,
	int wait)
{
	int ret;

	if (!stream) {
		dev_warn(hsw->dev, "warning: stream is NULL, no stream to resume, ignore it.\n");
		return 0;
	}

	trace_ipc_request("stream resume", stream->reply.stream_hw_id);

	ret = sst_hsw_stream_operations(hsw, IPC_STR_RESUME,
		stream->reply.stream_hw_id, wait);
	if (ret < 0)
		dev_err(hsw->dev, "error: failed to resume stream %d\n",
			stream->reply.stream_hw_id);

	return ret;
}

int sst_hsw_stream_reset(struct sst_hsw *hsw, struct sst_hsw_stream *stream)
{
	int ret, tries = 10;

	if (!stream) {
		dev_warn(hsw->dev, "warning: stream is NULL, no stream to reset, ignore it.\n");
		return 0;
	}

	/* dont reset streams that are not commited */
	if (!stream->commited)
		return 0;

	/* wait for pause to complete before we reset the stream */
	while (stream->running && --tries)
		msleep(1);
	if (!tries) {
		dev_err(hsw->dev, "error: reset stream %d still running\n",
			stream->reply.stream_hw_id);
		return -EINVAL;
	}

	trace_ipc_request("stream reset", stream->reply.stream_hw_id);

	ret = sst_hsw_stream_operations(hsw, IPC_STR_RESET,
		stream->reply.stream_hw_id, 1);
	if (ret < 0)
		dev_err(hsw->dev, "error: failed to reset stream %d\n",
			stream->reply.stream_hw_id);
	return ret;
}

/* Stream pointer positions */
u32 sst_hsw_get_dsp_position(struct sst_hsw *hsw,
	struct sst_hsw_stream *stream)
{
	u32 rpos;

	sst_dsp_read(hsw->dsp, &rpos,
		stream->reply.read_position_register_address, sizeof(rpos));

	return rpos;
}

/* Stream presentation (monotonic) positions */
u64 sst_hsw_get_dsp_presentation_position(struct sst_hsw *hsw,
	struct sst_hsw_stream *stream)
{
	u64 ppos;

	sst_dsp_read(hsw->dsp, &ppos,
		stream->reply.presentation_position_register_address,
		sizeof(ppos));

	return ppos;
}

/* physical BE config */
int sst_hsw_device_set_config(struct sst_hsw *hsw,
	enum sst_hsw_device_id dev, enum sst_hsw_device_mclk mclk,
	enum sst_hsw_device_mode mode, u32 clock_divider)
{
	struct sst_hsw_ipc_device_config_req config;
	u32 header;
	int ret;

	trace_ipc_request("set device config", dev);

	hsw->dx_dev = config.ssp_interface = dev;
	hsw->dx_mclk = config.clock_frequency = mclk;
	hsw->dx_mode = config.mode = mode;
	hsw->dx_clock_divider = config.clock_divider = clock_divider;
	if (mode == SST_HSW_DEVICE_TDM_CLOCK_MASTER)
		config.channels = 4;
	else
		config.channels = 2;

	trace_hsw_device_config_req(&config);

	header = IPC_GLB_TYPE(IPC_GLB_SET_DEVICE_FORMATS);

	ret = sst_ipc_tx_message_wait(&hsw->ipc, header, &config,
		sizeof(config), NULL, 0);
	if (ret < 0)
		dev_err(hsw->dev, "error: set device formats failed\n");

	return ret;
}
EXPORT_SYMBOL_GPL(sst_hsw_device_set_config);

/* DX Config */
int sst_hsw_dx_set_state(struct sst_hsw *hsw,
	enum sst_hsw_dx_state state, struct sst_hsw_ipc_dx_reply *dx)
{
	u32 header, state_;
	int ret, item;

	header = IPC_GLB_TYPE(IPC_GLB_ENTER_DX_STATE);
	state_ = state;

	trace_ipc_request("PM enter Dx state", state);

	ret = sst_ipc_tx_message_wait(&hsw->ipc, header, &state_,
		sizeof(state_), dx, sizeof(*dx));
	if (ret < 0) {
		dev_err(hsw->dev, "ipc: error set dx state %d failed\n", state);
		return ret;
	}

	for (item = 0; item < dx->entries_no; item++) {
		dev_dbg(hsw->dev,
			"Item[%d] offset[%x] - size[%x] - source[%x]\n",
			item, dx->mem_info[item].offset,
			dx->mem_info[item].size,
			dx->mem_info[item].source);
	}
	dev_dbg(hsw->dev, "ipc: got %d entry numbers for state %d\n",
		dx->entries_no, state);

	return ret;
}

struct sst_module_runtime *sst_hsw_runtime_module_create(struct sst_hsw *hsw,
	int mod_id, int offset)
{
	struct sst_dsp *dsp = hsw->dsp;
	struct sst_module *module;
	struct sst_module_runtime *runtime;
	int err;

	module = sst_module_get_from_id(dsp, mod_id);
	if (module == NULL) {
		dev_err(dsp->dev, "error: failed to get module %d for pcm\n",
			mod_id);
		return NULL;
	}

	runtime = sst_module_runtime_new(module, mod_id, NULL);
	if (runtime == NULL) {
		dev_err(dsp->dev, "error: failed to create module %d runtime\n",
			mod_id);
		return NULL;
	}

	err = sst_module_runtime_alloc_blocks(runtime, offset);
	if (err < 0) {
		dev_err(dsp->dev, "error: failed to alloc blocks for module %d runtime\n",
			mod_id);
		sst_module_runtime_free(runtime);
		return NULL;
	}

	dev_dbg(dsp->dev, "runtime id %d created for module %d\n", runtime->id,
		mod_id);
	return runtime;
}

void sst_hsw_runtime_module_free(struct sst_module_runtime *runtime)
{
	sst_module_runtime_free_blocks(runtime);
	sst_module_runtime_free(runtime);
}

#ifdef CONFIG_PM
static int sst_hsw_dx_state_dump(struct sst_hsw *hsw)
{
	struct sst_dsp *sst = hsw->dsp;
	u32 item, offset, size;
	int ret = 0;

	trace_ipc_request("PM state dump. Items #", SST_HSW_MAX_DX_REGIONS);

	if (hsw->dx.entries_no > SST_HSW_MAX_DX_REGIONS) {
		dev_err(hsw->dev,
			"error: number of FW context regions greater than %d\n",
			SST_HSW_MAX_DX_REGIONS);
		memset(&hsw->dx, 0, sizeof(hsw->dx));
		return -EINVAL;
	}

	ret = sst_dsp_dma_get_channel(sst, 0);
	if (ret < 0) {
		dev_err(hsw->dev, "error: cant allocate dma channel %d\n", ret);
		return ret;
	}

	/* set on-demond mode on engine 0 channel 3 */
	sst_dsp_shim_update_bits(sst, SST_HMDC,
			SST_HMDC_HDDA_E0_ALLCH | SST_HMDC_HDDA_E1_ALLCH,
			SST_HMDC_HDDA_E0_ALLCH | SST_HMDC_HDDA_E1_ALLCH);

	for (item = 0; item < hsw->dx.entries_no; item++) {
		if (hsw->dx.mem_info[item].source == SST_HSW_DX_TYPE_MEMORY_DUMP
			&& hsw->dx.mem_info[item].offset > DSP_DRAM_ADDR_OFFSET
			&& hsw->dx.mem_info[item].offset <
			DSP_DRAM_ADDR_OFFSET + SST_HSW_DX_CONTEXT_SIZE) {

			offset = hsw->dx.mem_info[item].offset
					- DSP_DRAM_ADDR_OFFSET;
			size = (hsw->dx.mem_info[item].size + 3) & (~3);

			ret = sst_dsp_dma_copyfrom(sst, hsw->dx_context_paddr + offset,
				sst->addr.lpe_base + offset, size);
			if (ret < 0) {
				dev_err(hsw->dev,
					"error: FW context dump failed\n");
				memset(&hsw->dx, 0, sizeof(hsw->dx));
				goto out;
			}
		}
	}

out:
	sst_dsp_dma_put_channel(sst);
	return ret;
}

static int sst_hsw_dx_state_restore(struct sst_hsw *hsw)
{
	struct sst_dsp *sst = hsw->dsp;
	u32 item, offset, size;
	int ret;

	for (item = 0; item < hsw->dx.entries_no; item++) {
		if (hsw->dx.mem_info[item].source == SST_HSW_DX_TYPE_MEMORY_DUMP
			&& hsw->dx.mem_info[item].offset > DSP_DRAM_ADDR_OFFSET
			&& hsw->dx.mem_info[item].offset <
			DSP_DRAM_ADDR_OFFSET + SST_HSW_DX_CONTEXT_SIZE) {

			offset = hsw->dx.mem_info[item].offset
					- DSP_DRAM_ADDR_OFFSET;
			size = (hsw->dx.mem_info[item].size + 3) & (~3);

			ret = sst_dsp_dma_copyto(sst, sst->addr.lpe_base + offset,
				hsw->dx_context_paddr + offset, size);
			if (ret < 0) {
				dev_err(hsw->dev,
					"error: FW context restore failed\n");
				return ret;
			}
		}
	}

	return 0;
}

int sst_hsw_dsp_load(struct sst_hsw *hsw)
{
	struct sst_dsp *dsp = hsw->dsp;
	struct sst_fw *sst_fw, *t;
	int ret;

	dev_dbg(hsw->dev, "loading audio DSP....");

	ret = sst_dsp_wake(dsp);
	if (ret < 0) {
		dev_err(hsw->dev, "error: failed to wake audio DSP\n");
		return -ENODEV;
	}

	ret = sst_dsp_dma_get_channel(dsp, 0);
	if (ret < 0) {
		dev_err(hsw->dev, "error: cant allocate dma channel %d\n", ret);
		return ret;
	}

	list_for_each_entry_safe_reverse(sst_fw, t, &dsp->fw_list, list) {
		ret = sst_fw_reload(sst_fw);
		if (ret < 0) {
			dev_err(hsw->dev, "error: SST FW reload failed\n");
			sst_dsp_dma_put_channel(dsp);
			return -ENOMEM;
		}
	}
	ret = sst_block_alloc_scratch(hsw->dsp);
	if (ret < 0)
		return -EINVAL;

	sst_dsp_dma_put_channel(dsp);
	return 0;
}

static int sst_hsw_dsp_restore(struct sst_hsw *hsw)
{
	struct sst_dsp *dsp = hsw->dsp;
	int ret;

	dev_dbg(hsw->dev, "restoring audio DSP....");

	ret = sst_dsp_dma_get_channel(dsp, 0);
	if (ret < 0) {
		dev_err(hsw->dev, "error: cant allocate dma channel %d\n", ret);
		return ret;
	}

	ret = sst_hsw_dx_state_restore(hsw);
	if (ret < 0) {
		dev_err(hsw->dev, "error: SST FW context restore failed\n");
		sst_dsp_dma_put_channel(dsp);
		return -ENOMEM;
	}
	sst_dsp_dma_put_channel(dsp);

	/* wait for DSP boot completion */
	sst_dsp_boot(dsp);

	return ret;
}

int sst_hsw_dsp_runtime_suspend(struct sst_hsw *hsw)
{
	int ret;

	dev_dbg(hsw->dev, "audio dsp runtime suspend\n");

	ret = sst_hsw_dx_set_state(hsw, SST_HSW_DX_STATE_D3, &hsw->dx);
	if (ret < 0)
		return ret;

	sst_dsp_stall(hsw->dsp);

	ret = sst_hsw_dx_state_dump(hsw);
	if (ret < 0)
		return ret;

	sst_ipc_drop_all(&hsw->ipc);

	return 0;
}

int sst_hsw_dsp_runtime_sleep(struct sst_hsw *hsw)
{
	struct sst_fw *sst_fw, *t;
	struct sst_dsp *dsp = hsw->dsp;

	list_for_each_entry_safe(sst_fw, t, &dsp->fw_list, list) {
		sst_fw_unload(sst_fw);
	}
	sst_block_free_scratch(dsp);

	hsw->boot_complete = false;

	sst_dsp_sleep(dsp);

	return 0;
}

int sst_hsw_dsp_runtime_resume(struct sst_hsw *hsw)
{
	struct device *dev = hsw->dev;
	int ret;

	dev_dbg(dev, "audio dsp runtime resume\n");

	if (hsw->boot_complete)
		return 1; /* tell caller no action is required */

	ret = sst_hsw_dsp_restore(hsw);
	if (ret < 0)
		dev_err(dev, "error: audio DSP boot failure\n");

	sst_hsw_init_module_state(hsw);

	ret = wait_event_timeout(hsw->boot_wait, hsw->boot_complete,
		msecs_to_jiffies(IPC_BOOT_MSECS));
	if (ret == 0) {
		dev_err(hsw->dev, "error: audio DSP boot timeout IPCD 0x%x IPCX 0x%x\n",
			sst_dsp_shim_read_unlocked(hsw->dsp, SST_IPCD),
			sst_dsp_shim_read_unlocked(hsw->dsp, SST_IPCX));
		return -EIO;
	}

	/* Set ADSP SSP port settings - sadly the FW does not store SSP port
	   settings as part of the PM context. */
	ret = sst_hsw_device_set_config(hsw, hsw->dx_dev, hsw->dx_mclk,
					hsw->dx_mode, hsw->dx_clock_divider);
	if (ret < 0)
		dev_err(dev, "error: SSP re-initialization failed\n");

	return ret;
}
#endif

struct sst_dsp *sst_hsw_get_dsp(struct sst_hsw *hsw)
{
	return hsw->dsp;
}

void sst_hsw_init_module_state(struct sst_hsw *hsw)
{
	struct sst_module *module;
	enum sst_hsw_module_id id;

	/* the base fw contains several modules */
	for (id = SST_HSW_MODULE_BASE_FW; id < SST_HSW_MAX_MODULE_ID; id++) {
		module = sst_module_get_from_id(hsw->dsp, id);
		if (module) {
			/* module waves is active only after being enabled */
			if (id == SST_HSW_MODULE_WAVES)
				module->state = SST_MODULE_STATE_INITIALIZED;
			else
				module->state = SST_MODULE_STATE_ACTIVE;
		}
	}
}

bool sst_hsw_is_module_loaded(struct sst_hsw *hsw, u32 module_id)
{
	struct sst_module *module;

	module = sst_module_get_from_id(hsw->dsp, module_id);
	if (module == NULL || module->state == SST_MODULE_STATE_UNLOADED)
		return false;
	else
		return true;
}

bool sst_hsw_is_module_active(struct sst_hsw *hsw, u32 module_id)
{
	struct sst_module *module;

	module = sst_module_get_from_id(hsw->dsp, module_id);
	if (module != NULL && module->state == SST_MODULE_STATE_ACTIVE)
		return true;
	else
		return false;
}

void sst_hsw_set_module_enabled_rtd3(struct sst_hsw *hsw, u32 module_id)
{
	hsw->enabled_modules_rtd3 |= (1 << module_id);
}

void sst_hsw_set_module_disabled_rtd3(struct sst_hsw *hsw, u32 module_id)
{
	hsw->enabled_modules_rtd3 &= ~(1 << module_id);
}

bool sst_hsw_is_module_enabled_rtd3(struct sst_hsw *hsw, u32 module_id)
{
	return hsw->enabled_modules_rtd3 & (1 << module_id);
}

void sst_hsw_reset_param_buf(struct sst_hsw *hsw)
{
	hsw->param_idx_w = 0;
	hsw->param_idx_r = 0;
	memset((void *)hsw->param_buf, 0, sizeof(hsw->param_buf));
}

int sst_hsw_store_param_line(struct sst_hsw *hsw, u8 *buf)
{
	/* save line to the first available position of param buffer */
	if (hsw->param_idx_w > WAVES_PARAM_LINES - 1) {
		dev_warn(hsw->dev, "warning: param buffer overflow!\n");
		return -EPERM;
	}
	memcpy(hsw->param_buf[hsw->param_idx_w], buf, WAVES_PARAM_COUNT);
	hsw->param_idx_w++;
	return 0;
}

int sst_hsw_load_param_line(struct sst_hsw *hsw, u8 *buf)
{
	u8 id = 0;

	/* read the first matching line from param buffer */
	while (hsw->param_idx_r < WAVES_PARAM_LINES) {
		id = hsw->param_buf[hsw->param_idx_r][0];
		hsw->param_idx_r++;
		if (buf[0] == id) {
			memcpy(buf, hsw->param_buf[hsw->param_idx_r],
				WAVES_PARAM_COUNT);
			break;
		}
	}
	if (hsw->param_idx_r > WAVES_PARAM_LINES - 1) {
		dev_dbg(hsw->dev, "end of buffer, roll to the beginning\n");
		hsw->param_idx_r = 0;
		return 0;
	}
	return 0;
}

int sst_hsw_launch_param_buf(struct sst_hsw *hsw)
{
	int ret, idx;

	if (!sst_hsw_is_module_active(hsw, SST_HSW_MODULE_WAVES)) {
		dev_dbg(hsw->dev, "module waves is not active\n");
		return 0;
	}

	/* put all param lines to DSP through ipc */
	for (idx = 0; idx < hsw->param_idx_w; idx++) {
		ret = sst_hsw_module_set_param(hsw,
			SST_HSW_MODULE_WAVES, 0, hsw->param_buf[idx][0],
			WAVES_PARAM_COUNT, hsw->param_buf[idx]);
		if (ret < 0)
			return ret;
	}
	return 0;
}

int sst_hsw_module_load(struct sst_hsw *hsw,
	u32 module_id, u32 instance_id, char *name)
{
	int ret = 0;
	const struct firmware *fw = NULL;
	struct sst_fw *hsw_sst_fw;
	struct sst_module *module;
	struct device *dev = hsw->dev;
	struct sst_dsp *dsp = hsw->dsp;

	dev_dbg(dev, "sst_hsw_module_load id=%d, name='%s'", module_id, name);

	module = sst_module_get_from_id(dsp, module_id);
	if (module == NULL) {
		/* loading for the first time */
		if (module_id == SST_HSW_MODULE_BASE_FW) {
			/* for base module: use fw requested in acpi probe */
			fw = dsp->pdata->fw;
			if (!fw) {
				dev_err(dev, "request Base fw failed\n");
				return -ENODEV;
			}
		} else {
			/* try and load any other optional modules if they are
			 * available. Use dev_info instead of dev_err in case
			 * request firmware failed */
			ret = request_firmware(&fw, name, dev);
			if (ret) {
				dev_info(dev, "fw image %s not available(%d)\n",
						name, ret);
				return ret;
			}
		}
		hsw_sst_fw = sst_fw_new(dsp, fw, hsw);
		if (hsw_sst_fw  == NULL) {
			dev_err(dev, "error: failed to load firmware\n");
			ret = -ENOMEM;
			goto out;
		}
		module = sst_module_get_from_id(dsp, module_id);
		if (module == NULL) {
			dev_err(dev, "error: no module %d in firmware %s\n",
					module_id, name);
		}
	} else
		dev_info(dev, "module %d (%s) already loaded\n",
				module_id, name);
out:
	/* release fw, but base fw should be released by acpi driver */
	if (fw && module_id != SST_HSW_MODULE_BASE_FW)
		release_firmware(fw);

	return ret;
}

int sst_hsw_module_enable(struct sst_hsw *hsw,
	u32 module_id, u32 instance_id)
{
	int ret;
	u32 header = 0;
	struct sst_hsw_ipc_module_config config;
	struct sst_module *module;
	struct sst_module_runtime *runtime;
	struct device *dev = hsw->dev;
	struct sst_dsp *dsp = hsw->dsp;

	if (!sst_hsw_is_module_loaded(hsw, module_id)) {
		dev_dbg(dev, "module %d not loaded\n", module_id);
		return 0;
	}

	if (sst_hsw_is_module_active(hsw, module_id)) {
		dev_info(dev, "module %d already enabled\n", module_id);
		return 0;
	}

	module = sst_module_get_from_id(dsp, module_id);
	if (module == NULL) {
		dev_err(dev, "module %d not valid\n", module_id);
		return -ENXIO;
	}

	runtime = sst_module_runtime_get_from_id(module, module_id);
	if (runtime == NULL) {
		dev_err(dev, "runtime %d not valid", module_id);
		return -ENXIO;
	}

	header = IPC_GLB_TYPE(IPC_GLB_MODULE_OPERATION) |
			IPC_MODULE_OPERATION(IPC_MODULE_ENABLE) |
			IPC_MODULE_ID(module_id);
	dev_dbg(dev, "module enable header: %x\n", header);

	config.map.module_entries_count = 1;
	config.map.module_entries[0].module_id = module->id;
	config.map.module_entries[0].entry_point = module->entry;

	config.persistent_mem.offset =
		sst_dsp_get_offset(dsp,
			runtime->persistent_offset, SST_MEM_DRAM);
	config.persistent_mem.size = module->persistent_size;

	config.scratch_mem.offset =
		sst_dsp_get_offset(dsp,
			dsp->scratch_offset, SST_MEM_DRAM);
	config.scratch_mem.size = module->scratch_size;
	dev_dbg(dev, "mod %d enable p:%d @ %x, s:%d @ %x, ep: %x",
		config.map.module_entries[0].module_id,
		config.persistent_mem.size,
		config.persistent_mem.offset,
		config.scratch_mem.size, config.scratch_mem.offset,
		config.map.module_entries[0].entry_point);

	ret = sst_ipc_tx_message_wait(&hsw->ipc, header,
			&config, sizeof(config), NULL, 0);
	if (ret < 0)
		dev_err(dev, "ipc: module enable failed - %d\n", ret);
	else
		module->state = SST_MODULE_STATE_ACTIVE;

	return ret;
}

int sst_hsw_module_disable(struct sst_hsw *hsw,
	u32 module_id, u32 instance_id)
{
	int ret;
	u32 header;
	struct sst_module *module;
	struct device *dev = hsw->dev;
	struct sst_dsp *dsp = hsw->dsp;

	if (!sst_hsw_is_module_loaded(hsw, module_id)) {
		dev_dbg(dev, "module %d not loaded\n", module_id);
		return 0;
	}

	if (!sst_hsw_is_module_active(hsw, module_id)) {
		dev_info(dev, "module %d already disabled\n", module_id);
		return 0;
	}

	module = sst_module_get_from_id(dsp, module_id);
	if (module == NULL) {
		dev_err(dev, "module %d not valid\n", module_id);
		return -ENXIO;
	}

	header = IPC_GLB_TYPE(IPC_GLB_MODULE_OPERATION) |
			IPC_MODULE_OPERATION(IPC_MODULE_DISABLE) |
			IPC_MODULE_ID(module_id);

	ret = sst_ipc_tx_message_wait(&hsw->ipc, header,  NULL, 0, NULL, 0);
	if (ret < 0)
		dev_err(dev, "module disable failed - %d\n", ret);
	else
		module->state = SST_MODULE_STATE_INITIALIZED;

	return ret;
}

int sst_hsw_module_set_param(struct sst_hsw *hsw,
	u32 module_id, u32 instance_id, u32 parameter_id,
	u32 param_size, char *param)
{
	int ret;
	unsigned char *data = NULL;
	u32 header = 0;
	u32 payload_size = 0, transfer_parameter_size = 0;
	dma_addr_t dma_addr = 0;
	struct sst_hsw_transfer_parameter *parameter;
	struct device *dev = hsw->dev;

	header = IPC_GLB_TYPE(IPC_GLB_MODULE_OPERATION) |
			IPC_MODULE_OPERATION(IPC_MODULE_SET_PARAMETER) |
			IPC_MODULE_ID(module_id);
	dev_dbg(dev, "sst_hsw_module_set_param header=%x\n", header);

	payload_size = param_size +
		sizeof(struct sst_hsw_transfer_parameter) -
		sizeof(struct sst_hsw_transfer_list);
	dev_dbg(dev, "parameter size : %d\n", param_size);
	dev_dbg(dev, "payload size   : %d\n", payload_size);

	if (payload_size <= SST_HSW_IPC_MAX_SHORT_PARAMETER_SIZE) {
		/* short parameter, mailbox can contain data */
		dev_dbg(dev, "transfer parameter size : %d\n",
			transfer_parameter_size);

		transfer_parameter_size = ALIGN(payload_size, 4);
		dev_dbg(dev, "transfer parameter aligned size : %d\n",
			transfer_parameter_size);

		parameter = kzalloc(transfer_parameter_size, GFP_KERNEL);
		if (parameter == NULL)
			return -ENOMEM;

		memcpy(parameter->data, param, param_size);
	} else {
		dev_warn(dev, "transfer parameter size too large!");
		return 0;
	}

	parameter->parameter_id = parameter_id;
	parameter->data_size = param_size;

	ret = sst_ipc_tx_message_wait(&hsw->ipc, header,
		parameter, transfer_parameter_size , NULL, 0);
	if (ret < 0)
		dev_err(dev, "ipc: module set parameter failed - %d\n", ret);

	kfree(parameter);

	if (data)
		dma_free_coherent(hsw->dsp->dma_dev,
			param_size, (void *)data, dma_addr);

	return ret;
}

static struct sst_dsp_device hsw_dev = {
	.thread = hsw_irq_thread,
	.ops = &haswell_ops,
};

static void hsw_tx_msg(struct sst_generic_ipc *ipc, struct ipc_message *msg)
{
	/* send the message */
	sst_dsp_outbox_write(ipc->dsp, msg->tx_data, msg->tx_size);
	sst_dsp_ipc_msg_tx(ipc->dsp, msg->header);
}

static void hsw_shim_dbg(struct sst_generic_ipc *ipc, const char *text)
{
	struct sst_dsp *sst = ipc->dsp;
	u32 isr, ipcd, imrx, ipcx;

	ipcx = sst_dsp_shim_read_unlocked(sst, SST_IPCX);
	isr = sst_dsp_shim_read_unlocked(sst, SST_ISRX);
	ipcd = sst_dsp_shim_read_unlocked(sst, SST_IPCD);
	imrx = sst_dsp_shim_read_unlocked(sst, SST_IMRX);

	dev_err(ipc->dev,
		"ipc: --%s-- ipcx 0x%8.8x isr 0x%8.8x ipcd 0x%8.8x imrx 0x%8.8x\n",
		text, ipcx, isr, ipcd, imrx);
}

static void hsw_tx_data_copy(struct ipc_message *msg, char *tx_data,
	size_t tx_size)
{
	memcpy(msg->tx_data, tx_data, tx_size);
}

static u64 hsw_reply_msg_match(u64 header, u64 *mask)
{
	/* clear reply bits & status bits */
	header &= ~(IPC_STATUS_MASK | IPC_GLB_REPLY_MASK);
	*mask = (u64)-1;

	return header;
}

static bool hsw_is_dsp_busy(struct sst_dsp *dsp)
{
	u64 ipcx;

	ipcx = sst_dsp_shim_read_unlocked(dsp, SST_IPCX);
	return (ipcx & (SST_IPCX_BUSY | SST_IPCX_DONE));
}

int sst_hsw_dsp_init(struct device *dev, struct sst_pdata *pdata)
{
	struct sst_hsw_ipc_fw_version version;
	struct sst_hsw *hsw;
	struct sst_generic_ipc *ipc;
	int ret;

	dev_dbg(dev, "initialising Audio DSP IPC\n");

	hsw = devm_kzalloc(dev, sizeof(*hsw), GFP_KERNEL);
	if (hsw == NULL)
		return -ENOMEM;

	hsw->dev = dev;

	ipc = &hsw->ipc;
	ipc->dev = dev;
	ipc->ops.tx_msg = hsw_tx_msg;
	ipc->ops.shim_dbg = hsw_shim_dbg;
	ipc->ops.tx_data_copy = hsw_tx_data_copy;
	ipc->ops.reply_msg_match = hsw_reply_msg_match;
	ipc->ops.is_dsp_busy = hsw_is_dsp_busy;

	ipc->tx_data_max_size = IPC_MAX_MAILBOX_BYTES;
	ipc->rx_data_max_size = IPC_MAX_MAILBOX_BYTES;

	ret = sst_ipc_init(ipc);
	if (ret != 0)
		goto ipc_init_err;

	INIT_LIST_HEAD(&hsw->stream_list);
	init_waitqueue_head(&hsw->boot_wait);
	hsw_dev.thread_context = hsw;

	/* init SST shim */
	hsw->dsp = sst_dsp_new(dev, &hsw_dev, pdata);
	if (hsw->dsp == NULL) {
		ret = -ENODEV;
		goto dsp_new_err;
	}

	ipc->dsp = hsw->dsp;

	/* allocate DMA buffer for context storage */
	hsw->dx_context = dma_alloc_coherent(hsw->dsp->dma_dev,
		SST_HSW_DX_CONTEXT_SIZE, &hsw->dx_context_paddr, GFP_KERNEL);
	if (hsw->dx_context == NULL) {
		ret = -ENOMEM;
		goto dma_err;
	}

	/* keep the DSP in reset state for base FW loading */
	sst_dsp_reset(hsw->dsp);

	/* load base module and other modules in base firmware image */
	ret = sst_hsw_module_load(hsw, SST_HSW_MODULE_BASE_FW, 0, "Base");
	if (ret < 0)
		goto fw_err;

	/* try to load module waves */
	sst_hsw_module_load(hsw, SST_HSW_MODULE_WAVES, 0, "intel/IntcPP01.bin");

	/* allocate scratch mem regions */
	ret = sst_block_alloc_scratch(hsw->dsp);
	if (ret < 0)
		goto boot_err;

	/* init param buffer */
	sst_hsw_reset_param_buf(hsw);

	/* wait for DSP boot completion */
	sst_dsp_boot(hsw->dsp);
	ret = wait_event_timeout(hsw->boot_wait, hsw->boot_complete,
		msecs_to_jiffies(IPC_BOOT_MSECS));
	if (ret == 0) {
		ret = -EIO;
		dev_err(hsw->dev, "error: audio DSP boot timeout IPCD 0x%x IPCX 0x%x\n",
			sst_dsp_shim_read_unlocked(hsw->dsp, SST_IPCD),
			sst_dsp_shim_read_unlocked(hsw->dsp, SST_IPCX));
		goto boot_err;
	}

	/* init module state after boot */
	sst_hsw_init_module_state(hsw);

	/* get the FW version */
	sst_hsw_fw_get_version(hsw, &version);

	/* get the globalmixer */
	ret = sst_hsw_mixer_get_info(hsw);
	if (ret < 0) {
		dev_err(hsw->dev, "error: failed to get stream info\n");
		goto boot_err;
	}

	pdata->dsp = hsw;
	return 0;

boot_err:
	sst_dsp_reset(hsw->dsp);
	sst_fw_free_all(hsw->dsp);
fw_err:
	dma_free_coherent(hsw->dsp->dma_dev, SST_HSW_DX_CONTEXT_SIZE,
			hsw->dx_context, hsw->dx_context_paddr);
dma_err:
	sst_dsp_free(hsw->dsp);
dsp_new_err:
	sst_ipc_fini(ipc);
ipc_init_err:
	return ret;
}
EXPORT_SYMBOL_GPL(sst_hsw_dsp_init);

void sst_hsw_dsp_free(struct device *dev, struct sst_pdata *pdata)
{
	struct sst_hsw *hsw = pdata->dsp;

	sst_dsp_reset(hsw->dsp);
	sst_fw_free_all(hsw->dsp);
	dma_free_coherent(hsw->dsp->dma_dev, SST_HSW_DX_CONTEXT_SIZE,
			hsw->dx_context, hsw->dx_context_paddr);
	sst_dsp_free(hsw->dsp);
	sst_ipc_fini(&hsw->ipc);
}
EXPORT_SYMBOL_GPL(sst_hsw_dsp_free);
