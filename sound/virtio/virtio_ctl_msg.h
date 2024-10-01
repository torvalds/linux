/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * virtio-snd: Virtio sound device
 * Copyright (C) 2021 OpenSynergy GmbH
 */
#ifndef VIRTIO_SND_MSG_H
#define VIRTIO_SND_MSG_H

#include <linux/atomic.h>
#include <linux/virtio.h>

struct virtio_snd;
struct virtio_snd_msg;

void virtsnd_ctl_msg_ref(struct virtio_snd_msg *msg);

void virtsnd_ctl_msg_unref(struct virtio_snd_msg *msg);

void *virtsnd_ctl_msg_request(struct virtio_snd_msg *msg);

void *virtsnd_ctl_msg_response(struct virtio_snd_msg *msg);

struct virtio_snd_msg *virtsnd_ctl_msg_alloc(size_t request_size,
					     size_t response_size, gfp_t gfp);

int virtsnd_ctl_msg_send(struct virtio_snd *snd, struct virtio_snd_msg *msg,
			 struct scatterlist *out_sgs,
			 struct scatterlist *in_sgs, bool nowait);

/**
 * virtsnd_ctl_msg_send_sync() - Simplified sending of synchronous message.
 * @snd: VirtIO sound device.
 * @msg: Control message.
 *
 * After returning from this function, the message will be deleted. If message
 * content is still needed, the caller must additionally to
 * virtsnd_ctl_msg_ref/unref() it.
 *
 * The msg_timeout_ms module parameter defines the message completion timeout.
 * If the message is not completed within this time, the function will return an
 * error.
 *
 * Context: Any context that permits to sleep.
 * Return: 0 on success, -errno on failure.
 *
 * The return value is a message status code (VIRTIO_SND_S_XXX) converted to an
 * appropriate -errno value.
 */
static inline int virtsnd_ctl_msg_send_sync(struct virtio_snd *snd,
					    struct virtio_snd_msg *msg)
{
	return virtsnd_ctl_msg_send(snd, msg, NULL, NULL, false);
}

/**
 * virtsnd_ctl_msg_send_async() - Simplified sending of asynchronous message.
 * @snd: VirtIO sound device.
 * @msg: Control message.
 *
 * Context: Any context.
 * Return: 0 on success, -errno on failure.
 */
static inline int virtsnd_ctl_msg_send_async(struct virtio_snd *snd,
					     struct virtio_snd_msg *msg)
{
	return virtsnd_ctl_msg_send(snd, msg, NULL, NULL, true);
}

void virtsnd_ctl_msg_cancel_all(struct virtio_snd *snd);

void virtsnd_ctl_msg_complete(struct virtio_snd_msg *msg);

int virtsnd_ctl_query_info(struct virtio_snd *snd, int command, int start_id,
			   int count, size_t size, void *info);

void virtsnd_ctl_notify_cb(struct virtqueue *vqueue);

#endif /* VIRTIO_SND_MSG_H */
