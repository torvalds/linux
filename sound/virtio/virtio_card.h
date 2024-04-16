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

#include "virtio_ctl_msg.h"
#include "virtio_pcm.h"

#define VIRTIO_SND_CARD_DRIVER	"virtio-snd"
#define VIRTIO_SND_CARD_NAME	"VirtIO SoundCard"
#define VIRTIO_SND_PCM_NAME	"VirtIO PCM"

struct virtio_jack;
struct virtio_pcm_substream;

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
 * struct virtio_kctl - VirtIO control element.
 * @kctl: ALSA control element.
 * @items: Items for the ENUMERATED element type.
 */
struct virtio_kctl {
	struct snd_kcontrol *kctl;
	struct virtio_snd_ctl_enum_item *items;
};

/**
 * struct virtio_snd - VirtIO sound card device.
 * @vdev: Underlying virtio device.
 * @queues: Virtqueue wrappers.
 * @card: ALSA sound card.
 * @ctl_msgs: Pending control request list.
 * @event_msgs: Device events.
 * @pcm_list: VirtIO PCM device list.
 * @jacks: VirtIO jacks.
 * @njacks: Number of jacks.
 * @substreams: VirtIO PCM substreams.
 * @nsubstreams: Number of PCM substreams.
 * @chmaps: VirtIO channel maps.
 * @nchmaps: Number of channel maps.
 * @kctl_infos: VirtIO control element information.
 * @kctls: VirtIO control elements.
 * @nkctls: Number of control elements.
 */
struct virtio_snd {
	struct virtio_device *vdev;
	struct virtio_snd_queue queues[VIRTIO_SND_VQ_MAX];
	struct snd_card *card;
	struct list_head ctl_msgs;
	struct virtio_snd_event *event_msgs;
	struct list_head pcm_list;
	struct virtio_jack *jacks;
	u32 njacks;
	struct virtio_pcm_substream *substreams;
	u32 nsubstreams;
	struct virtio_snd_chmap_info *chmaps;
	u32 nchmaps;
	struct virtio_snd_ctl_info *kctl_infos;
	struct virtio_kctl *kctls;
	u32 nkctls;
};

/* Message completion timeout in milliseconds (module parameter). */
extern u32 virtsnd_msg_timeout_ms;

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

static inline struct virtio_snd_queue *
virtsnd_pcm_queue(struct virtio_pcm_substream *vss)
{
	if (vss->direction == SNDRV_PCM_STREAM_PLAYBACK)
		return virtsnd_tx_queue(vss->snd);
	else
		return virtsnd_rx_queue(vss->snd);
}

int virtsnd_jack_parse_cfg(struct virtio_snd *snd);

int virtsnd_jack_build_devs(struct virtio_snd *snd);

void virtsnd_jack_event(struct virtio_snd *snd,
			struct virtio_snd_event *event);

int virtsnd_chmap_parse_cfg(struct virtio_snd *snd);

int virtsnd_chmap_build_devs(struct virtio_snd *snd);

int virtsnd_kctl_parse_cfg(struct virtio_snd *snd);

int virtsnd_kctl_build_devs(struct virtio_snd *snd);

void virtsnd_kctl_event(struct virtio_snd *snd, struct virtio_snd_event *event);

#endif /* VIRTIO_SND_CARD_H */
