// SPDX-License-Identifier: GPL-2.0
/*
 * dice-alesis.c - a part of driver for DICE based devices
 *
 * Copyright (c) 2018 Takashi Sakamoto
 */

#include "dice.h"

static const unsigned int
alesis_io14_tx_pcm_chs[MAX_STREAMS][SND_DICE_RATE_MODE_COUNT] = {
	{6, 6, 4},	/* Tx0 = Analog + S/PDIF. */
	{8, 4, 0},	/* Tx1 = ADAT1. */
};

static const unsigned int
alesis_io26_tx_pcm_chs[MAX_STREAMS][SND_DICE_RATE_MODE_COUNT] = {
	{10, 10, 4},	/* Tx0 = Analog + S/PDIF. */
	{16, 8, 0},	/* Tx1 = ADAT1 + ADAT2. */
};

int snd_dice_detect_alesis_formats(struct snd_dice *dice)
{
	__be32 reg;
	u32 data;
	int i;
	int err;

	err = snd_dice_transaction_read_tx(dice, TX_NUMBER_AUDIO, &reg,
					   sizeof(reg));
	if (err < 0)
		return err;
	data = be32_to_cpu(reg);

	if (data == 4 || data == 6) {
		memcpy(dice->tx_pcm_chs, alesis_io14_tx_pcm_chs,
				MAX_STREAMS * SND_DICE_RATE_MODE_COUNT *
				sizeof(unsigned int));
	} else {
		memcpy(dice->tx_pcm_chs, alesis_io26_tx_pcm_chs,
				MAX_STREAMS * SND_DICE_RATE_MODE_COUNT *
				sizeof(unsigned int));
	}

	for (i = 0; i < SND_DICE_RATE_MODE_COUNT; ++i)
		dice->rx_pcm_chs[0][i] = 8;

	dice->tx_midi_ports[0] = 1;
	dice->rx_midi_ports[0] = 1;

	return 0;
}

int snd_dice_detect_alesis_mastercontrol_formats(struct snd_dice *dice)
{
	int i;

	dice->tx_pcm_chs[0][SND_DICE_RATE_MODE_LOW]	= 16;
	dice->tx_pcm_chs[1][SND_DICE_RATE_MODE_LOW]	= 12;
	dice->tx_pcm_chs[0][SND_DICE_RATE_MODE_MIDDLE]	= 12;
	dice->tx_pcm_chs[1][SND_DICE_RATE_MODE_MIDDLE]	= 4;
	dice->tx_pcm_chs[0][SND_DICE_RATE_MODE_HIGH]	= 8;
	dice->tx_pcm_chs[1][SND_DICE_RATE_MODE_HIGH]	= 0;

	for (i = 0; i < SND_DICE_RATE_MODE_COUNT; ++i) {
		dice->rx_pcm_chs[0][i] = 6;
		dice->rx_pcm_chs[1][i] = 0;
	}

	for (i = 0; i < MAX_STREAMS; ++i) {
		dice->tx_midi_ports[i] = 2;
		dice->rx_midi_ports[i] = 2;
	}

	return 0;
}
