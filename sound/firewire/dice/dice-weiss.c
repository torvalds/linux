// SPDX-License-Identifier: GPL-2.0
// dice-weiss.c - a part of driver for DICE based devices
//
// Copyright (c) 2023 Rolf Anderegg and Michele Perrone

#include "dice.h"

struct dice_weiss_spec {
	unsigned int tx_pcm_chs[MAX_STREAMS][SND_DICE_RATE_MODE_COUNT];
	unsigned int rx_pcm_chs[MAX_STREAMS][SND_DICE_RATE_MODE_COUNT];
};

// Weiss DAC202: 192kHz 2-channel DAC
static const struct dice_weiss_spec dac202 = {
	.tx_pcm_chs = {{2, 2, 2}, {0, 0, 0} },
	.rx_pcm_chs = {{2, 2, 2}, {0, 0, 0} },
};

// Weiss MAN301: 192kHz 2-channel music archive network player
static const struct dice_weiss_spec man301 = {
	.tx_pcm_chs = {{2, 2, 2}, {0, 0, 0} },
	.rx_pcm_chs = {{2, 2, 2}, {0, 0, 0} },
};

// Weiss INT202: 192kHz unidirectional 2-channel digital Firewire nterface
static const struct dice_weiss_spec int202 = {
	.tx_pcm_chs = {{2, 2, 2}, {0, 0, 0} },
	.rx_pcm_chs = {{2, 2, 2}, {0, 0, 0} },
};

// Weiss INT203: 192kHz bidirectional 2-channel digital Firewire nterface
static const struct dice_weiss_spec int203 = {
	.tx_pcm_chs = {{2, 2, 2}, {0, 0, 0} },
	.rx_pcm_chs = {{2, 2, 2}, {0, 0, 0} },
};

// Weiss ADC2: 192kHz A/D converter with microphone preamps and line nputs
static const struct dice_weiss_spec adc2 = {
	.tx_pcm_chs = {{2, 2, 2}, {0, 0, 0} },
	.rx_pcm_chs = {{2, 2, 2}, {0, 0, 0} },
};

// Weiss DAC2/Minerva: 192kHz 2-channel DAC
static const struct dice_weiss_spec dac2_minerva = {
	.tx_pcm_chs = {{2, 2, 2}, {0, 0, 0} },
	.rx_pcm_chs = {{2, 2, 2}, {0, 0, 0} },
};

// Weiss Vesta: 192kHz 2-channel Firewire to AES/EBU interface
static const struct dice_weiss_spec vesta = {
	.tx_pcm_chs = {{2, 2, 2}, {0, 0, 0} },
	.rx_pcm_chs = {{2, 2, 2}, {0, 0, 0} },
};

// Weiss AFI1: 192kHz 24-channel Firewire to ADAT or AES/EBU interface
static const struct dice_weiss_spec afi1 = {
	.tx_pcm_chs = {{24, 16, 8}, {0, 0, 0} },
	.rx_pcm_chs = {{24, 16, 8}, {0, 0, 0} },
};

int snd_dice_detect_weiss_formats(struct snd_dice *dice)
{
	static const struct {
		u32 model_id;
		const struct dice_weiss_spec *spec;
	} *entry, entries[] = {
		{0x000007, &dac202},
		{0x000008, &dac202}, // Maya edition: same audio I/O as DAC202.
		{0x000006, &int202},
		{0x00000a, &int203},
		{0x00000b, &man301},
		{0x000001, &adc2},
		{0x000003, &dac2_minerva},
		{0x000002, &vesta},
		{0x000004, &afi1},
	};
	struct fw_csr_iterator it;
	int key, val, model_id;
	int i;

	model_id = 0;
	fw_csr_iterator_init(&it, dice->unit->directory);
	while (fw_csr_iterator_next(&it, &key, &val)) {
		if (key == CSR_MODEL) {
			model_id = val;
			break;
		}
	}

	for (i = 0; i < ARRAY_SIZE(entries); ++i) {
		entry = entries + i;
		if (entry->model_id == model_id)
			break;
	}
	if (i == ARRAY_SIZE(entries))
		return -ENODEV;

	memcpy(dice->tx_pcm_chs, entry->spec->tx_pcm_chs,
	       MAX_STREAMS * SND_DICE_RATE_MODE_COUNT * sizeof(unsigned int));
	memcpy(dice->rx_pcm_chs, entry->spec->rx_pcm_chs,
	       MAX_STREAMS * SND_DICE_RATE_MODE_COUNT * sizeof(unsigned int));

	return 0;
}
