// SPDX-License-Identifier: GPL-2.0+
/*
 * virtio-snd: Virtio sound device
 * Copyright (C) 2021 OpenSynergy GmbH
 */
#include <sound/pcm_params.h>

#include "virtio_card.h"

/**
 * struct virtio_pcm_msg - VirtIO I/O message.
 * @substream: VirtIO PCM substream.
 * @xfer: Request header payload.
 * @status: Response header payload.
 * @length: Data length in bytes.
 * @sgs: Payload scatter-gather table.
 */
struct virtio_pcm_msg {
	struct virtio_pcm_substream *substream;
	struct virtio_snd_pcm_xfer xfer;
	struct virtio_snd_pcm_status status;
	size_t length;
	struct scatterlist sgs[];
};

/**
 * enum pcm_msg_sg_index - Index values for the virtio_pcm_msg->sgs field in
 *                         an I/O message.
 * @PCM_MSG_SG_XFER: Element containing a virtio_snd_pcm_xfer structure.
 * @PCM_MSG_SG_STATUS: Element containing a virtio_snd_pcm_status structure.
 * @PCM_MSG_SG_DATA: The first element containing a data buffer.
 */
enum pcm_msg_sg_index {
	PCM_MSG_SG_XFER = 0,
	PCM_MSG_SG_STATUS,
	PCM_MSG_SG_DATA
};

/**
 * virtsnd_pcm_sg_num() - Count the number of sg-elements required to represent
 *                        vmalloc'ed buffer.
 * @data: Pointer to vmalloc'ed buffer.
 * @length: Buffer size.
 *
 * Context: Any context.
 * Return: Number of physically contiguous parts in the @data.
 */
static int virtsnd_pcm_sg_num(u8 *data, unsigned int length)
{
	phys_addr_t sg_address;
	unsigned int sg_length;
	int num = 0;

	while (length) {
		struct page *pg = vmalloc_to_page(data);
		phys_addr_t pg_address = page_to_phys(pg);
		size_t pg_length;

		pg_length = PAGE_SIZE - offset_in_page(data);
		if (pg_length > length)
			pg_length = length;

		if (!num || sg_address + sg_length != pg_address) {
			sg_address = pg_address;
			sg_length = pg_length;
			num++;
		} else {
			sg_length += pg_length;
		}

		data += pg_length;
		length -= pg_length;
	}

	return num;
}

/**
 * virtsnd_pcm_sg_from() - Build sg-list from vmalloc'ed buffer.
 * @sgs: Preallocated sg-list to populate.
 * @nsgs: The maximum number of elements in the @sgs.
 * @data: Pointer to vmalloc'ed buffer.
 * @length: Buffer size.
 *
 * Splits the buffer into physically contiguous parts and makes an sg-list of
 * such parts.
 *
 * Context: Any context.
 */
static void virtsnd_pcm_sg_from(struct scatterlist *sgs, int nsgs, u8 *data,
				unsigned int length)
{
	int idx = -1;

	while (length) {
		struct page *pg = vmalloc_to_page(data);
		size_t pg_length;

		pg_length = PAGE_SIZE - offset_in_page(data);
		if (pg_length > length)
			pg_length = length;

		if (idx == -1 ||
		    sg_phys(&sgs[idx]) + sgs[idx].length != page_to_phys(pg)) {
			if (idx + 1 == nsgs)
				break;
			sg_set_page(&sgs[++idx], pg, pg_length,
				    offset_in_page(data));
		} else {
			sgs[idx].length += pg_length;
		}

		data += pg_length;
		length -= pg_length;
	}

	sg_mark_end(&sgs[idx]);
}

/**
 * virtsnd_pcm_msg_alloc() - Allocate I/O messages.
 * @vss: VirtIO PCM substream.
 * @periods: Current number of periods.
 * @period_bytes: Current period size in bytes.
 *
 * The function slices the buffer into @periods parts (each with the size of
 * @period_bytes), and creates @periods corresponding I/O messages.
 *
 * Context: Any context that permits to sleep.
 * Return: 0 on success, -ENOMEM on failure.
 */
int virtsnd_pcm_msg_alloc(struct virtio_pcm_substream *vss,
			  unsigned int periods, unsigned int period_bytes)
{
	struct snd_pcm_runtime *runtime = vss->substream->runtime;
	unsigned int i;

	vss->msgs = kcalloc(periods, sizeof(*vss->msgs), GFP_KERNEL);
	if (!vss->msgs)
		return -ENOMEM;

	vss->nmsgs = periods;

	for (i = 0; i < periods; ++i) {
		u8 *data = runtime->dma_area + period_bytes * i;
		int sg_num = virtsnd_pcm_sg_num(data, period_bytes);
		struct virtio_pcm_msg *msg;

		msg = kzalloc(struct_size(msg, sgs, sg_num + 2), GFP_KERNEL);
		if (!msg)
			return -ENOMEM;

		msg->substream = vss;
		sg_init_one(&msg->sgs[PCM_MSG_SG_XFER], &msg->xfer,
			    sizeof(msg->xfer));
		sg_init_one(&msg->sgs[PCM_MSG_SG_STATUS], &msg->status,
			    sizeof(msg->status));
		msg->length = period_bytes;
		virtsnd_pcm_sg_from(&msg->sgs[PCM_MSG_SG_DATA], sg_num, data,
				    period_bytes);

		vss->msgs[i] = msg;
	}

	return 0;
}

/**
 * virtsnd_pcm_msg_free() - Free all allocated I/O messages.
 * @vss: VirtIO PCM substream.
 *
 * Context: Any context.
 */
void virtsnd_pcm_msg_free(struct virtio_pcm_substream *vss)
{
	unsigned int i;

	for (i = 0; vss->msgs && i < vss->nmsgs; ++i)
		kfree(vss->msgs[i]);
	kfree(vss->msgs);

	vss->msgs = NULL;
	vss->nmsgs = 0;
}

/**
 * virtsnd_pcm_msg_send() - Send asynchronous I/O messages.
 * @vss: VirtIO PCM substream.
 *
 * All messages are organized in an ordered circular list. Each time the
 * function is called, all currently non-enqueued messages are added to the
 * virtqueue. For this, the function keeps track of two values:
 *
 *   msg_last_enqueued = index of the last enqueued message,
 *   msg_count = # of pending messages in the virtqueue.
 *
 * Context: Any context. Expects the tx/rx queue and the VirtIO substream
 *          spinlocks to be held by caller.
 * Return: 0 on success, -errno on failure.
 */
int virtsnd_pcm_msg_send(struct virtio_pcm_substream *vss)
{
	struct snd_pcm_runtime *runtime = vss->substream->runtime;
	struct virtio_snd *snd = vss->snd;
	struct virtio_device *vdev = snd->vdev;
	struct virtqueue *vqueue = virtsnd_pcm_queue(vss)->vqueue;
	int i;
	int n;
	bool notify = false;

	i = (vss->msg_last_enqueued + 1) % runtime->periods;
	n = runtime->periods - vss->msg_count;

	for (; n; --n, i = (i + 1) % runtime->periods) {
		struct virtio_pcm_msg *msg = vss->msgs[i];
		struct scatterlist *psgs[] = {
			&msg->sgs[PCM_MSG_SG_XFER],
			&msg->sgs[PCM_MSG_SG_DATA],
			&msg->sgs[PCM_MSG_SG_STATUS]
		};
		int rc;

		msg->xfer.stream_id = cpu_to_le32(vss->sid);
		memset(&msg->status, 0, sizeof(msg->status));

		if (vss->direction == SNDRV_PCM_STREAM_PLAYBACK)
			rc = virtqueue_add_sgs(vqueue, psgs, 2, 1, msg,
					       GFP_ATOMIC);
		else
			rc = virtqueue_add_sgs(vqueue, psgs, 1, 2, msg,
					       GFP_ATOMIC);

		if (rc) {
			dev_err(&vdev->dev,
				"SID %u: failed to send I/O message\n",
				vss->sid);
			return rc;
		}

		vss->msg_last_enqueued = i;
		vss->msg_count++;
	}

	if (!(vss->features & (1U << VIRTIO_SND_PCM_F_MSG_POLLING)))
		notify = virtqueue_kick_prepare(vqueue);

	if (notify)
		virtqueue_notify(vqueue);

	return 0;
}

/**
 * virtsnd_pcm_msg_pending_num() - Returns the number of pending I/O messages.
 * @vss: VirtIO substream.
 *
 * Context: Any context.
 * Return: Number of messages.
 */
unsigned int virtsnd_pcm_msg_pending_num(struct virtio_pcm_substream *vss)
{
	unsigned int num;
	unsigned long flags;

	spin_lock_irqsave(&vss->lock, flags);
	num = vss->msg_count;
	spin_unlock_irqrestore(&vss->lock, flags);

	return num;
}

/**
 * virtsnd_pcm_msg_complete() - Complete an I/O message.
 * @msg: I/O message.
 * @written_bytes: Number of bytes written to the message.
 *
 * Completion of the message means the elapsed period. If transmission is
 * allowed, then each completed message is immediately placed back at the end
 * of the queue.
 *
 * For the playback substream, @written_bytes is equal to sizeof(msg->status).
 *
 * For the capture substream, @written_bytes is equal to sizeof(msg->status)
 * plus the number of captured bytes.
 *
 * Context: Interrupt context. Takes and releases the VirtIO substream spinlock.
 */
static void virtsnd_pcm_msg_complete(struct virtio_pcm_msg *msg,
				     size_t written_bytes)
{
	struct virtio_pcm_substream *vss = msg->substream;

	/*
	 * hw_ptr always indicates the buffer position of the first I/O message
	 * in the virtqueue. Therefore, on each completion of an I/O message,
	 * the hw_ptr value is unconditionally advanced.
	 */
	spin_lock(&vss->lock);
	/*
	 * If the capture substream returned an incorrect status, then just
	 * increase the hw_ptr by the message size.
	 */
	if (vss->direction == SNDRV_PCM_STREAM_PLAYBACK ||
	    written_bytes <= sizeof(msg->status))
		vss->hw_ptr += msg->length;
	else
		vss->hw_ptr += written_bytes - sizeof(msg->status);

	if (vss->hw_ptr >= vss->buffer_bytes)
		vss->hw_ptr -= vss->buffer_bytes;

	vss->xfer_xrun = false;
	vss->msg_count--;

	if (vss->xfer_enabled) {
		struct snd_pcm_runtime *runtime = vss->substream->runtime;

		runtime->delay =
			bytes_to_frames(runtime,
					le32_to_cpu(msg->status.latency_bytes));

		schedule_work(&vss->elapsed_period);

		virtsnd_pcm_msg_send(vss);
	} else if (!vss->msg_count) {
		wake_up_all(&vss->msg_empty);
	}
	spin_unlock(&vss->lock);
}

/**
 * virtsnd_pcm_notify_cb() - Process all completed I/O messages.
 * @queue: Underlying tx/rx virtqueue.
 *
 * Context: Interrupt context. Takes and releases the tx/rx queue spinlock.
 */
static inline void virtsnd_pcm_notify_cb(struct virtio_snd_queue *queue)
{
	struct virtio_pcm_msg *msg;
	u32 written_bytes;
	unsigned long flags;

	spin_lock_irqsave(&queue->lock, flags);
	do {
		virtqueue_disable_cb(queue->vqueue);
		while ((msg = virtqueue_get_buf(queue->vqueue, &written_bytes)))
			virtsnd_pcm_msg_complete(msg, written_bytes);
		if (unlikely(virtqueue_is_broken(queue->vqueue)))
			break;
	} while (!virtqueue_enable_cb(queue->vqueue));
	spin_unlock_irqrestore(&queue->lock, flags);
}

/**
 * virtsnd_pcm_tx_notify_cb() - Process all completed TX messages.
 * @vqueue: Underlying tx virtqueue.
 *
 * Context: Interrupt context.
 */
void virtsnd_pcm_tx_notify_cb(struct virtqueue *vqueue)
{
	struct virtio_snd *snd = vqueue->vdev->priv;

	virtsnd_pcm_notify_cb(virtsnd_tx_queue(snd));
}

/**
 * virtsnd_pcm_rx_notify_cb() - Process all completed RX messages.
 * @vqueue: Underlying rx virtqueue.
 *
 * Context: Interrupt context.
 */
void virtsnd_pcm_rx_notify_cb(struct virtqueue *vqueue)
{
	struct virtio_snd *snd = vqueue->vdev->priv;

	virtsnd_pcm_notify_cb(virtsnd_rx_queue(snd));
}

/**
 * virtsnd_pcm_ctl_msg_alloc() - Allocate and initialize the PCM device control
 *                               message for the specified substream.
 * @vss: VirtIO PCM substream.
 * @command: Control request code (VIRTIO_SND_R_PCM_XXX).
 * @gfp: Kernel flags for memory allocation.
 *
 * Context: Any context. May sleep if @gfp flags permit.
 * Return: Allocated message on success, NULL on failure.
 */
struct virtio_snd_msg *
virtsnd_pcm_ctl_msg_alloc(struct virtio_pcm_substream *vss,
			  unsigned int command, gfp_t gfp)
{
	size_t request_size = sizeof(struct virtio_snd_pcm_hdr);
	size_t response_size = sizeof(struct virtio_snd_hdr);
	struct virtio_snd_msg *msg;

	switch (command) {
	case VIRTIO_SND_R_PCM_SET_PARAMS:
		request_size = sizeof(struct virtio_snd_pcm_set_params);
		break;
	}

	msg = virtsnd_ctl_msg_alloc(request_size, response_size, gfp);
	if (msg) {
		struct virtio_snd_pcm_hdr *hdr = virtsnd_ctl_msg_request(msg);

		hdr->hdr.code = cpu_to_le32(command);
		hdr->stream_id = cpu_to_le32(vss->sid);
	}

	return msg;
}
