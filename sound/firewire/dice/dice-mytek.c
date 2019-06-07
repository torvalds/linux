// SPDX-License-Identifier: GPL-2.0
/*
 * dice-mytek.c - a part of driver for DICE based devices
 *
 * Copyright (c) 2018 Melvin Vermeeren
 */

#include "dice.h"

struct dice_mytek_spec {
	unsigned int tx_pcm_chs[MAX_STREAMS][SND_DICE_RATE_MODE_COUNT];
	unsigned int rx_pcm_chs[MAX_STREAMS][SND_DICE_RATE_MODE_COUNT];
};

static const struct dice_mytek_spec stereo_192_dsd_dac = {
	/* AES, TOSLINK, SPDIF, ADAT inputs on device */
	.tx_pcm_chs = {{8, 8, 8}, {0, 0, 0} },
	/* PCM 44.1-192, native DSD64/DSD128 to device */
	.rx_pcm_chs = {{4, 4, 4}, {0, 0, 0} }
};

/*
 * Mytek has a few other firewire-capable devices, though newer models appear
 * to lack the port more often than not. As I don't have access to any of them
 * they are missing here. An example is the Mytek 8x192 ADDA, which is DICE.
 */

int snd_dice_detect_mytek_formats(struct snd_dice *dice)
{
	int i;
	const struct dice_mytek_spec *dev;

	dev = &stereo_192_dsd_dac;

	memcpy(dice->tx_pcm_chs, dev->tx_pcm_chs,
	       MAX_STREAMS * SND_DICE_RATE_MODE_COUNT * sizeof(unsigned int));
	memcpy(dice->rx_pcm_chs, dev->rx_pcm_chs,
	       MAX_STREAMS * SND_DICE_RATE_MODE_COUNT * sizeof(unsigned int));

	for (i = 0; i < MAX_STREAMS; ++i) {
		dice->tx_midi_ports[i] = 0;
		dice->rx_midi_ports[i] = 0;
	}

	return 0;
}
