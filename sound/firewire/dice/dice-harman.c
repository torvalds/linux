// SPDX-License-Identifier: GPL-2.0
// dice-harman.c - a part of driver for DICE based devices
//
// Copyright (c) 2021 Takashi Sakamoto

#include "dice.h"

int snd_dice_detect_harman_formats(struct snd_dice *dice)
{
	int i;

	// Lexicon I-ONYX FW810s supports sampling transfer frequency up to
	// 96.0 kHz, 12 PCM channels and 1 MIDI channel in its first tx stream
	// , 10 PCM channels and 1 MIDI channel in its first rx stream for all
	// of the frequencies.
	for (i = 0; i < 2; ++i) {
		dice->tx_pcm_chs[0][i] = 12;
		dice->tx_midi_ports[0] = 1;
		dice->rx_pcm_chs[0][i] = 10;
		dice->rx_midi_ports[0] = 1;
	}

	return 0;
}
