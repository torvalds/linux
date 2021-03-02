/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * virtio-snd: Virtio sound device
 * Copyright (C) 2021 OpenSynergy GmbH
 */
#ifndef VIRTIO_SND_CARD_H
#define VIRTIO_SND_CARD_H

#include <linux/slab.h>
#include <linux/virtio.h>
#include <sound/core.h>
#include <uapi/linux/virtio_snd.h>

#define VIRTIO_SND_CARD_DRIVER	"virtio-snd"
#define VIRTIO_SND_CARD_NAME	"VirtIO SoundCard"

/**
 * struct virtio_snd_queue - Virtqueue wrapper structure.
 * @lock: Used to synchronize access to a virtqueue.
 * @vqueue: Underlying virtqueue.
 */
struct virtio_snd_queue {
	spinlock_t lock;
	struct virtqueue *vqueue;
};

/**
 * struct virtio_snd - VirtIO sound card device.
 * @vdev: Underlying virtio device.
 * @queues: Virtqueue wrappers.
 * @card: ALSA sound card.
 * @event_msgs: Device events.
 */
struct virtio_snd {
	struct virtio_device *vdev;
	struct virtio_snd_queue queues[VIRTIO_SND_VQ_MAX];
	struct snd_card *card;
	struct virtio_snd_event *event_msgs;
};

static inline struct virtio_snd_queue *
virtsnd_control_queue(struct virtio_snd *snd)
{
	return &snd->queues[VIRTIO_SND_VQ_CONTROL];
}

static inline struct virtio_snd_queue *
virtsnd_event_queue(struct virtio_snd *snd)
{
	return &snd->queues[VIRTIO_SND_VQ_EVENT];
}

static inline struct virtio_snd_queue *
virtsnd_tx_queue(struct virtio_snd *snd)
{
	return &snd->queues[VIRTIO_SND_VQ_TX];
}

static inline struct virtio_snd_queue *
virtsnd_rx_queue(struct virtio_snd *snd)
{
	return &snd->queues[VIRTIO_SND_VQ_RX];
}

#endif /* VIRTIO_SND_CARD_H */
