/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Linux driver for TerraTec DMX 6Fire USB
 *
 * Author:	Torsten Schenk <torsten.schenk@zoho.com>
 * Created:	Jan 01, 2011
 * Copyright:	(C) Torsten Schenk
 */

#ifndef USB6FIRE_PCM_H
#define USB6FIRE_PCM_H

#include <sound/pcm.h>
#include <linux/mutex.h>

#include "common.h"

enum /* settings for pcm */
{
	/* maximum of EP_W_MAX_PACKET_SIZE[] (see firmware.c) */
	PCM_N_URBS = 16, PCM_N_PACKETS_PER_URB = 8, PCM_MAX_PACKET_SIZE = 604
};

struct pcm_urb {
	struct sfire_chip *chip;

	/* BEGIN DO NOT SEPARATE */
	struct urb instance;
	struct usb_iso_packet_descriptor packets[PCM_N_PACKETS_PER_URB];
	/* END DO NOT SEPARATE */
	u8 *buffer;

	struct pcm_urb *peer;
};

struct pcm_substream {
	spinlock_t lock;
	struct snd_pcm_substream *instance;

	bool active;

	snd_pcm_uframes_t dma_off; /* current position in alsa dma_area */
	snd_pcm_uframes_t period_off; /* current position in current period */
};

struct pcm_runtime {
	struct sfire_chip *chip;
	struct snd_pcm *instance;

	struct pcm_substream playback;
	struct pcm_substream capture;
	bool panic; /* if set driver won't do anymore pcm on device */

	struct pcm_urb in_urbs[PCM_N_URBS];
	struct pcm_urb out_urbs[PCM_N_URBS];
	int in_packet_size;
	int out_packet_size;
	int in_n_analog; /* number of analog channels soundcard sends */
	int out_n_analog; /* number of analog channels soundcard receives */

	struct mutex stream_mutex;
	u8 stream_state; /* one of STREAM_XXX (pcm.c) */
	u8 rate; /* one of PCM_RATE_XXX */
	wait_queue_head_t stream_wait_queue;
	bool stream_wait_cond;
};

int usb6fire_pcm_init(struct sfire_chip *chip);
void usb6fire_pcm_abort(struct sfire_chip *chip);
void usb6fire_pcm_destroy(struct sfire_chip *chip);
#endif /* USB6FIRE_PCM_H */
