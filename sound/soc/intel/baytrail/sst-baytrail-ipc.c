/*
 * Intel Baytrail SST IPC Support
 * Copyright (c) 2014, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
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
#include <linux/platform_device.h>
#include <linux/kthread.h>
#include <linux/firmware.h>
#include <linux/io.h>
#include <asm/div64.h>

#include "sst-baytrail-ipc.h"
#include "../common/sst-dsp.h"
#include "../common/sst-dsp-priv.h"
#include "../common/sst-ipc.h"

/* IPC message timeout */
#define IPC_TIMEOUT_MSECS	300
#define IPC_BOOT_MSECS		200

#define IPC_EMPTY_LIST_SIZE	8

/* IPC header bits */
#define IPC_HEADER_MSG_ID_MASK	0xff
#define IPC_HEADER_MSG_ID(x)	((x) & IPC_HEADER_MSG_ID_MASK)
#define IPC_HEADER_STR_ID_SHIFT	8
#define IPC_HEADER_STR_ID_MASK	0x1f
#define IPC_HEADER_STR_ID(x)	(((x) & 0x1f) << IPC_HEADER_STR_ID_SHIFT)
#define IPC_HEADER_LARGE_SHIFT	13
#define IPC_HEADER_LARGE(x)	(((x) & 0x1) << IPC_HEADER_LARGE_SHIFT)
#define IPC_HEADER_DATA_SHIFT	16
#define IPC_HEADER_DATA_MASK	0x3fff
#define IPC_HEADER_DATA(x)	(((x) & 0x3fff) << IPC_HEADER_DATA_SHIFT)

/* mask for differentiating between notification and reply message */
#define IPC_NOTIFICATION	(0x1 << 7)

/* I2L Stream config/control msgs */
#define IPC_IA_ALLOC_STREAM	0x20
#define IPC_IA_FREE_STREAM	0x21
#define IPC_IA_PAUSE_STREAM	0x24
#define IPC_IA_RESUME_STREAM	0x25
#define IPC_IA_DROP_STREAM	0x26
#define IPC_IA_START_STREAM	0x30

/* notification messages */
#define IPC_IA_FW_INIT_CMPLT	0x81
#define IPC_SST_PERIOD_ELAPSED	0x97

/* IPC messages between host and ADSP */
struct sst_byt_address_info {
	u32 addr;
	u32 size;
} __packed;

struct sst_byt_str_type {
	u8 codec_type;
	u8 str_type;
	u8 operation;
	u8 protected_str;
	u8 time_slots;
	u8 reserved;
	u16 result;
} __packed;

struct sst_byt_pcm_params {
	u8 num_chan;
	u8 pcm_wd_sz;
	u8 use_offload_path;
	u8 reserved;
	u32 sfreq;
	u8 channel_map[8];
} __packed;

struct sst_byt_frames_info {
	u16 num_entries;
	u16 rsrvd;
	u32 frag_size;
	struct sst_byt_address_info ring_buf_info[8];
} __packed;

struct sst_byt_alloc_params {
	struct sst_byt_str_type str_type;
	struct sst_byt_pcm_params pcm_params;
	struct sst_byt_frames_info frame_info;
} __packed;

struct sst_byt_alloc_response {
	struct sst_byt_str_type str_type;
	u8 reserved[88];
} __packed;

struct sst_byt_start_stream_params {
	u32 byte_offset;
} __packed;

struct sst_byt_tstamp {
	u64 ring_buffer_counter;
	u64 hardware_counter;
	u64 frames_decoded;
	u64 bytes_decoded;
	u64 bytes_copied;
	u32 sampling_frequency;
	u32 channel_peak[8];
} __packed;

struct sst_byt_fw_version {
	u8 build;
	u8 minor;
	u8 major;
	u8 type;
} __packed;

struct sst_byt_fw_build_info {
	u8 date[16];
	u8 time[16];
} __packed;

struct sst_byt_fw_init {
	struct sst_byt_fw_version fw_version;
	struct sst_byt_fw_build_info build_info;
	u16 result;
	u8 module_id;
	u8 debug_info;
} __packed;

struct sst_byt_stream;
struct sst_byt;

/* stream infomation */
struct sst_byt_stream {
	struct list_head node;

	/* configuration */
	struct sst_byt_alloc_params request;
	struct sst_byt_alloc_response reply;

	/* runtime info */
	struct sst_byt *byt;
	int str_id;
	bool commited;
	bool running;

	/* driver callback */
	u32 (*notify_position)(struct sst_byt_stream *stream, void *data);
	void *pdata;
};

/* SST Baytrail IPC data */
struct sst_byt {
	struct device *dev;
	struct sst_dsp *dsp;

	/* stream */
	struct list_head stream_list;

	/* boot */
	wait_queue_head_t boot_wait;
	bool boot_complete;
	struct sst_fw *fw;

	/* IPC messaging */
	struct sst_generic_ipc ipc;
};

static inline u64 sst_byt_header(int msg_id, int data, bool large, int str_id)
{
	u64 header;

	header = IPC_HEADER_MSG_ID(msg_id) |
		 IPC_HEADER_STR_ID(str_id) |
		 IPC_HEADER_LARGE(large) |
		 IPC_HEADER_DATA(data) |
		 SST_BYT_IPCX_BUSY;

	return header;
}

static inline u16 sst_byt_header_msg_id(u64 header)
{
	return header & IPC_HEADER_MSG_ID_MASK;
}

static inline u8 sst_byt_header_str_id(u64 header)
{
	return (header >> IPC_HEADER_STR_ID_SHIFT) & IPC_HEADER_STR_ID_MASK;
}

static inline u16 sst_byt_header_data(u64 header)
{
	return (header >> IPC_HEADER_DATA_SHIFT) & IPC_HEADER_DATA_MASK;
}

static struct sst_byt_stream *sst_byt_get_stream(struct sst_byt *byt,
						 int stream_id)
{
	struct sst_byt_stream *stream;

	list_for_each_entry(stream, &byt->stream_list, node) {
		if (stream->str_id == stream_id)
			return stream;
	}

	return NULL;
}

static void sst_byt_stream_update(struct sst_byt *byt, struct ipc_message *msg)
{
	struct sst_byt_stream *stream;
	u64 header = msg->header;
	u8 stream_id = sst_byt_header_str_id(header);
	u8 stream_msg = sst_byt_header_msg_id(header);

	stream = sst_byt_get_stream(byt, stream_id);
	if (stream == NULL)
		return;

	switch (stream_msg) {
	case IPC_IA_DROP_STREAM:
	case IPC_IA_PAUSE_STREAM:
	case IPC_IA_FREE_STREAM:
		stream->running = false;
		break;
	case IPC_IA_START_STREAM:
	case IPC_IA_RESUME_STREAM:
		stream->running = true;
		break;
	}
}

static int sst_byt_process_reply(struct sst_byt *byt, u64 header)
{
	struct ipc_message *msg;

	msg = sst_ipc_reply_find_msg(&byt->ipc, header);
	if (msg == NULL)
		return 1;

	if (header & IPC_HEADER_LARGE(true)) {
		msg->rx_size = sst_byt_header_data(header);
		sst_dsp_inbox_read(byt->dsp, msg->rx_data, msg->rx_size);
	}

	/* update any stream states */
	sst_byt_stream_update(byt, msg);

	list_del(&msg->list);
	/* wake up */
	sst_ipc_tx_msg_reply_complete(&byt->ipc, msg);

	return 1;
}

static void sst_byt_fw_ready(struct sst_byt *byt, u64 header)
{
	dev_dbg(byt->dev, "ipc: DSP is ready 0x%llX\n", header);

	byt->boot_complete = true;
	wake_up(&byt->boot_wait);
}

static int sst_byt_process_notification(struct sst_byt *byt,
					unsigned long *flags)
{
	struct sst_dsp *sst = byt->dsp;
	struct sst_byt_stream *stream;
	u64 header;
	u8 msg_id, stream_id;
	int handled = 1;

	header = sst_dsp_shim_read64_unlocked(sst, SST_IPCD);
	msg_id = sst_byt_header_msg_id(header);

	switch (msg_id) {
	case IPC_SST_PERIOD_ELAPSED:
		stream_id = sst_byt_header_str_id(header);
		stream = sst_byt_get_stream(byt, stream_id);
		if (stream && stream->running && stream->notify_position) {
			spin_unlock_irqrestore(&sst->spinlock, *flags);
			stream->notify_position(stream, stream->pdata);
			spin_lock_irqsave(&sst->spinlock, *flags);
		}
		break;
	case IPC_IA_FW_INIT_CMPLT:
		sst_byt_fw_ready(byt, header);
		break;
	}

	return handled;
}

static irqreturn_t sst_byt_irq_thread(int irq, void *context)
{
	struct sst_dsp *sst = (struct sst_dsp *) context;
	struct sst_byt *byt = sst_dsp_get_thread_context(sst);
	struct sst_generic_ipc *ipc = &byt->ipc;
	u64 header;
	unsigned long flags;

	spin_lock_irqsave(&sst->spinlock, flags);

	header = sst_dsp_shim_read64_unlocked(sst, SST_IPCD);
	if (header & SST_BYT_IPCD_BUSY) {
		if (header & IPC_NOTIFICATION) {
			/* message from ADSP */
			sst_byt_process_notification(byt, &flags);
		} else {
			/* reply from ADSP */
			sst_byt_process_reply(byt, header);
		}
		/*
		 * clear IPCD BUSY bit and set DONE bit. Tell DSP we have
		 * processed the message and can accept new. Clear data part
		 * of the header
		 */
		sst_dsp_shim_update_bits64_unlocked(sst, SST_IPCD,
			SST_BYT_IPCD_DONE | SST_BYT_IPCD_BUSY |
			IPC_HEADER_DATA(IPC_HEADER_DATA_MASK),
			SST_BYT_IPCD_DONE);
		/* unmask message request interrupts */
		sst_dsp_shim_update_bits64_unlocked(sst, SST_IMRX,
			SST_BYT_IMRX_REQUEST, 0);
	}

	spin_unlock_irqrestore(&sst->spinlock, flags);

	/* continue to send any remaining messages... */
	queue_kthread_work(&ipc->kworker, &ipc->kwork);

	return IRQ_HANDLED;
}

/* stream API */
struct sst_byt_stream *sst_byt_stream_new(struct sst_byt *byt, int id,
	u32 (*notify_position)(struct sst_byt_stream *stream, void *data),
	void *data)
{
	struct sst_byt_stream *stream;
	struct sst_dsp *sst = byt->dsp;
	unsigned long flags;

	stream = kzalloc(sizeof(*stream), GFP_KERNEL);
	if (stream == NULL)
		return NULL;

	spin_lock_irqsave(&sst->spinlock, flags);
	list_add(&stream->node, &byt->stream_list);
	stream->notify_position = notify_position;
	stream->pdata = data;
	stream->byt = byt;
	stream->str_id = id;
	spin_unlock_irqrestore(&sst->spinlock, flags);

	return stream;
}

int sst_byt_stream_set_bits(struct sst_byt *byt, struct sst_byt_stream *stream,
			    int bits)
{
	stream->request.pcm_params.pcm_wd_sz = bits;
	return 0;
}

int sst_byt_stream_set_channels(struct sst_byt *byt,
				struct sst_byt_stream *stream, u8 channels)
{
	stream->request.pcm_params.num_chan = channels;
	return 0;
}

int sst_byt_stream_set_rate(struct sst_byt *byt, struct sst_byt_stream *stream,
			    unsigned int rate)
{
	stream->request.pcm_params.sfreq = rate;
	return 0;
}

/* stream sonfiguration */
int sst_byt_stream_type(struct sst_byt *byt, struct sst_byt_stream *stream,
			int codec_type, int stream_type, int operation)
{
	stream->request.str_type.codec_type = codec_type;
	stream->request.str_type.str_type = stream_type;
	stream->request.str_type.operation = operation;
	stream->request.str_type.time_slots = 0xc;

	return 0;
}

int sst_byt_stream_buffer(struct sst_byt *byt, struct sst_byt_stream *stream,
			  uint32_t buffer_addr, uint32_t buffer_size)
{
	stream->request.frame_info.num_entries = 1;
	stream->request.frame_info.ring_buf_info[0].addr = buffer_addr;
	stream->request.frame_info.ring_buf_info[0].size = buffer_size;
	/* calculate bytes per 4 ms fragment */
	stream->request.frame_info.frag_size =
		stream->request.pcm_params.sfreq *
		stream->request.pcm_params.num_chan *
		stream->request.pcm_params.pcm_wd_sz / 8 *
		4 / 1000;
	return 0;
}

int sst_byt_stream_commit(struct sst_byt *byt, struct sst_byt_stream *stream)
{
	struct sst_byt_alloc_params *str_req = &stream->request;
	struct sst_byt_alloc_response *reply = &stream->reply;
	u64 header;
	int ret;

	header = sst_byt_header(IPC_IA_ALLOC_STREAM,
				sizeof(*str_req) + sizeof(u32),
				true, stream->str_id);
	ret = sst_ipc_tx_message_wait(&byt->ipc, header, str_req,
				      sizeof(*str_req),
				      reply, sizeof(*reply));
	if (ret < 0) {
		dev_err(byt->dev, "ipc: error stream commit failed\n");
		return ret;
	}

	stream->commited = true;

	return 0;
}

int sst_byt_stream_free(struct sst_byt *byt, struct sst_byt_stream *stream)
{
	u64 header;
	int ret = 0;
	struct sst_dsp *sst = byt->dsp;
	unsigned long flags;

	if (!stream->commited)
		goto out;

	header = sst_byt_header(IPC_IA_FREE_STREAM, 0, false, stream->str_id);
	ret = sst_ipc_tx_message_wait(&byt->ipc, header, NULL, 0, NULL, 0);
	if (ret < 0) {
		dev_err(byt->dev, "ipc: free stream %d failed\n",
			stream->str_id);
		return -EAGAIN;
	}

	stream->commited = false;
out:
	spin_lock_irqsave(&sst->spinlock, flags);
	list_del(&stream->node);
	kfree(stream);
	spin_unlock_irqrestore(&sst->spinlock, flags);

	return ret;
}

static int sst_byt_stream_operations(struct sst_byt *byt, int type,
				     int stream_id, int wait)
{
	u64 header;

	header = sst_byt_header(type, 0, false, stream_id);
	if (wait)
		return sst_ipc_tx_message_wait(&byt->ipc, header, NULL,
						0, NULL, 0);
	else
		return sst_ipc_tx_message_nowait(&byt->ipc, header,
						NULL, 0);
}

/* stream ALSA trigger operations */
int sst_byt_stream_start(struct sst_byt *byt, struct sst_byt_stream *stream,
			 u32 start_offset)
{
	struct sst_byt_start_stream_params start_stream;
	void *tx_msg;
	size_t size;
	u64 header;
	int ret;

	start_stream.byte_offset = start_offset;
	header = sst_byt_header(IPC_IA_START_STREAM,
				sizeof(start_stream) + sizeof(u32),
				true, stream->str_id);
	tx_msg = &start_stream;
	size = sizeof(start_stream);

	ret = sst_ipc_tx_message_nowait(&byt->ipc, header, tx_msg, size);
	if (ret < 0)
		dev_err(byt->dev, "ipc: error failed to start stream %d\n",
			stream->str_id);

	return ret;
}

int sst_byt_stream_stop(struct sst_byt *byt, struct sst_byt_stream *stream)
{
	int ret;

	/* don't stop streams that are not commited */
	if (!stream->commited)
		return 0;

	ret = sst_byt_stream_operations(byt, IPC_IA_DROP_STREAM,
					stream->str_id, 0);
	if (ret < 0)
		dev_err(byt->dev, "ipc: error failed to stop stream %d\n",
			stream->str_id);
	return ret;
}

int sst_byt_stream_pause(struct sst_byt *byt, struct sst_byt_stream *stream)
{
	int ret;

	ret = sst_byt_stream_operations(byt, IPC_IA_PAUSE_STREAM,
					stream->str_id, 0);
	if (ret < 0)
		dev_err(byt->dev, "ipc: error failed to pause stream %d\n",
			stream->str_id);

	return ret;
}

int sst_byt_stream_resume(struct sst_byt *byt, struct sst_byt_stream *stream)
{
	int ret;

	ret = sst_byt_stream_operations(byt, IPC_IA_RESUME_STREAM,
					stream->str_id, 0);
	if (ret < 0)
		dev_err(byt->dev, "ipc: error failed to resume stream %d\n",
			stream->str_id);

	return ret;
}

int sst_byt_get_dsp_position(struct sst_byt *byt,
			     struct sst_byt_stream *stream, int buffer_size)
{
	struct sst_dsp *sst = byt->dsp;
	struct sst_byt_tstamp fw_tstamp;
	u8 str_id = stream->str_id;
	u32 tstamp_offset;

	tstamp_offset = SST_BYT_TIMESTAMP_OFFSET + str_id * sizeof(fw_tstamp);
	memcpy_fromio(&fw_tstamp,
		      sst->addr.lpe + tstamp_offset, sizeof(fw_tstamp));

	return do_div(fw_tstamp.ring_buffer_counter, buffer_size);
}

struct sst_dsp *sst_byt_get_dsp(struct sst_byt *byt)
{
	return byt->dsp;
}

static struct sst_dsp_device byt_dev = {
	.thread = sst_byt_irq_thread,
	.ops = &sst_byt_ops,
};

int sst_byt_dsp_suspend_late(struct device *dev, struct sst_pdata *pdata)
{
	struct sst_byt *byt = pdata->dsp;

	dev_dbg(byt->dev, "dsp reset\n");
	sst_dsp_reset(byt->dsp);
	sst_ipc_drop_all(&byt->ipc);
	dev_dbg(byt->dev, "dsp in reset\n");

	dev_dbg(byt->dev, "free all blocks and unload fw\n");
	sst_fw_unload(byt->fw);

	return 0;
}
EXPORT_SYMBOL_GPL(sst_byt_dsp_suspend_late);

int sst_byt_dsp_boot(struct device *dev, struct sst_pdata *pdata)
{
	struct sst_byt *byt = pdata->dsp;
	int ret;

	dev_dbg(byt->dev, "reload dsp fw\n");

	sst_dsp_reset(byt->dsp);

	ret = sst_fw_reload(byt->fw);
	if (ret <  0) {
		dev_err(dev, "error: failed to reload firmware\n");
		return ret;
	}

	/* wait for DSP boot completion */
	byt->boot_complete = false;
	sst_dsp_boot(byt->dsp);
	dev_dbg(byt->dev, "dsp booting...\n");

	return 0;
}
EXPORT_SYMBOL_GPL(sst_byt_dsp_boot);

int sst_byt_dsp_wait_for_ready(struct device *dev, struct sst_pdata *pdata)
{
	struct sst_byt *byt = pdata->dsp;
	int err;

	dev_dbg(byt->dev, "wait for dsp reboot\n");

	err = wait_event_timeout(byt->boot_wait, byt->boot_complete,
				 msecs_to_jiffies(IPC_BOOT_MSECS));
	if (err == 0) {
		dev_err(byt->dev, "ipc: error DSP boot timeout\n");
		return -EIO;
	}

	dev_dbg(byt->dev, "dsp rebooted\n");
	return 0;
}
EXPORT_SYMBOL_GPL(sst_byt_dsp_wait_for_ready);

static void byt_tx_msg(struct sst_generic_ipc *ipc, struct ipc_message *msg)
{
	if (msg->header & IPC_HEADER_LARGE(true))
		sst_dsp_outbox_write(ipc->dsp, msg->tx_data, msg->tx_size);

	sst_dsp_shim_write64_unlocked(ipc->dsp, SST_IPCX, msg->header);
}

static void byt_shim_dbg(struct sst_generic_ipc *ipc, const char *text)
{
	struct sst_dsp *sst = ipc->dsp;
	u64 isr, ipcd, imrx, ipcx;

	ipcx = sst_dsp_shim_read64_unlocked(sst, SST_IPCX);
	isr = sst_dsp_shim_read64_unlocked(sst, SST_ISRX);
	ipcd = sst_dsp_shim_read64_unlocked(sst, SST_IPCD);
	imrx = sst_dsp_shim_read64_unlocked(sst, SST_IMRX);

	dev_err(ipc->dev,
		"ipc: --%s-- ipcx 0x%llx isr 0x%llx ipcd 0x%llx imrx 0x%llx\n",
		text, ipcx, isr, ipcd, imrx);
}

static void byt_tx_data_copy(struct ipc_message *msg, char *tx_data,
	size_t tx_size)
{
	/* msg content = lower 32-bit of the header + data */
	*(u32 *)msg->tx_data = (u32)(msg->header & (u32)-1);
	memcpy(msg->tx_data + sizeof(u32), tx_data, tx_size);
	msg->tx_size += sizeof(u32);
}

static u64 byt_reply_msg_match(u64 header, u64 *mask)
{
	/* match reply to message sent based on msg and stream IDs */
	*mask = IPC_HEADER_MSG_ID_MASK |
	       IPC_HEADER_STR_ID_MASK << IPC_HEADER_STR_ID_SHIFT;
	header &= *mask;

	return header;
}

int sst_byt_dsp_init(struct device *dev, struct sst_pdata *pdata)
{
	struct sst_byt *byt;
	struct sst_generic_ipc *ipc;
	struct sst_fw *byt_sst_fw;
	struct sst_byt_fw_init init;
	int err;

	dev_dbg(dev, "initialising Byt DSP IPC\n");

	byt = devm_kzalloc(dev, sizeof(*byt), GFP_KERNEL);
	if (byt == NULL)
		return -ENOMEM;

	ipc = &byt->ipc;
	ipc->dev = dev;
	ipc->ops.tx_msg = byt_tx_msg;
	ipc->ops.shim_dbg = byt_shim_dbg;
	ipc->ops.tx_data_copy = byt_tx_data_copy;
	ipc->ops.reply_msg_match = byt_reply_msg_match;

	err = sst_ipc_init(ipc);
	if (err != 0)
		goto ipc_init_err;

	INIT_LIST_HEAD(&byt->stream_list);
	init_waitqueue_head(&byt->boot_wait);
	byt_dev.thread_context = byt;

	/* init SST shim */
	byt->dsp = sst_dsp_new(dev, &byt_dev, pdata);
	if (byt->dsp == NULL) {
		err = -ENODEV;
		goto dsp_new_err;
	}

	ipc->dsp = byt->dsp;

	/* keep the DSP in reset state for base FW loading */
	sst_dsp_reset(byt->dsp);

	byt_sst_fw = sst_fw_new(byt->dsp, pdata->fw, byt);
	if (byt_sst_fw  == NULL) {
		err = -ENODEV;
		dev_err(dev, "error: failed to load firmware\n");
		goto fw_err;
	}

	/* wait for DSP boot completion */
	sst_dsp_boot(byt->dsp);
	err = wait_event_timeout(byt->boot_wait, byt->boot_complete,
				 msecs_to_jiffies(IPC_BOOT_MSECS));
	if (err == 0) {
		err = -EIO;
		dev_err(byt->dev, "ipc: error DSP boot timeout\n");
		goto boot_err;
	}

	/* show firmware information */
	sst_dsp_inbox_read(byt->dsp, &init, sizeof(init));
	dev_info(byt->dev, "FW version: %02x.%02x.%02x.%02x\n",
		 init.fw_version.major, init.fw_version.minor,
		 init.fw_version.build, init.fw_version.type);
	dev_info(byt->dev, "Build type: %x\n", init.fw_version.type);
	dev_info(byt->dev, "Build date: %s %s\n",
		 init.build_info.date, init.build_info.time);

	pdata->dsp = byt;
	byt->fw = byt_sst_fw;

	return 0;

boot_err:
	sst_dsp_reset(byt->dsp);
	sst_fw_free(byt_sst_fw);
fw_err:
	sst_dsp_free(byt->dsp);
dsp_new_err:
	sst_ipc_fini(ipc);
ipc_init_err:

	return err;
}
EXPORT_SYMBOL_GPL(sst_byt_dsp_init);

void sst_byt_dsp_free(struct device *dev, struct sst_pdata *pdata)
{
	struct sst_byt *byt = pdata->dsp;

	sst_dsp_reset(byt->dsp);
	sst_fw_free_all(byt->dsp);
	sst_dsp_free(byt->dsp);
	sst_ipc_fini(&byt->ipc);
}
EXPORT_SYMBOL_GPL(sst_byt_dsp_free);
