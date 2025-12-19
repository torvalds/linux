// SPDX-License-Identifier: GPL-2.0
// dice-teac.c - a part of driver for DICE based devices
//
// Copyright (c) 2025 Takashi Sakamoto

#include "dice.h"

int snd_dice_detect_teac_formats(struct snd_dice *dice)
{
	__be32 reg;
	u32 data;
	int err;

	err = snd_dice_transaction_read_tx(dice, TX_NUMBER, &reg, sizeof(reg));
	if (err  < 0)
		return err;

	dice->tx_pcm_chs[0][SND_DICE_RATE_MODE_LOW] = 16;
	dice->tx_pcm_chs[0][SND_DICE_RATE_MODE_MIDDLE] = 16;
	dice->tx_midi_ports[0] = 1;

	data = be32_to_cpu(reg);
	if (data > 1) {
		dice->tx_pcm_chs[1][SND_DICE_RATE_MODE_LOW] = 16;
		dice->tx_pcm_chs[1][SND_DICE_RATE_MODE_MIDDLE] = 16;
	}

	err = snd_dice_transaction_read_rx(dice, RX_NUMBER, &reg, sizeof(reg));
	if (err  < 0)
		return err;

	dice->rx_pcm_chs[0][SND_DICE_RATE_MODE_LOW] = 16;
	dice->rx_pcm_chs[0][SND_DICE_RATE_MODE_MIDDLE] = 16;
	dice->rx_midi_ports[0] = 1;

	data = be32_to_cpu(reg);
	if (data > 1) {
		dice->rx_pcm_chs[1][SND_DICE_RATE_MODE_LOW] = 16;
		dice->rx_pcm_chs[1][SND_DICE_RATE_MODE_MIDDLE] = 16;
	}

	return 0;
}
