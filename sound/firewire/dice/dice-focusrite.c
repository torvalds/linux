// SPDX-License-Identifier: GPL-2.0
// dice-focusrite.c - a part of driver for DICE based devices
//
// Copyright (c) 2022 Takashi Sakamoto

#include "dice.h"

int snd_dice_detect_focusrite_pro40_tcd3070_formats(struct snd_dice *dice)
{
	// Focusrite shipped several variants of Saffire Pro 40. One of them is based on TCD3070-CH
	// apart from the others with TCD2220. It doesn't support TCAT protocol extension.
	dice->tx_pcm_chs[0][0] = 20;
	dice->tx_midi_ports[0] = 1;
	dice->rx_pcm_chs[0][0] = 20;
	dice->rx_midi_ports[0] = 1;

	dice->tx_pcm_chs[0][1] = 16;
	dice->tx_midi_ports[1] = 1;
	dice->rx_pcm_chs[0][1] = 16;
	dice->rx_midi_ports[1] = 1;

	return 0;
}
