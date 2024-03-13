// SPDX-License-Identifier: GPL-2.0
/*
 * dice-tc_electronic.c - a part of driver for DICE based devices
 *
 * Copyright (c) 2018 Takashi Sakamoto
 */

#include "dice.h"

struct dice_tc_spec {
	unsigned int tx_pcm_chs[MAX_STREAMS][SND_DICE_RATE_MODE_COUNT];
	unsigned int rx_pcm_chs[MAX_STREAMS][SND_DICE_RATE_MODE_COUNT];
	bool has_midi;
};

static const struct dice_tc_spec desktop_konnekt6 = {
	.tx_pcm_chs = {{6, 6, 2}, {0, 0, 0} },
	.rx_pcm_chs = {{6, 6, 4}, {0, 0, 0} },
	.has_midi = false,
};

static const struct dice_tc_spec impact_twin = {
	.tx_pcm_chs = {{14, 10, 6}, {0, 0, 0} },
	.rx_pcm_chs = {{14, 10, 6}, {0, 0, 0} },
	.has_midi = true,
};

static const struct dice_tc_spec konnekt_8 = {
	.tx_pcm_chs = {{4, 4, 3}, {0, 0, 0} },
	.rx_pcm_chs = {{4, 4, 3}, {0, 0, 0} },
	.has_midi = true,
};

static const struct dice_tc_spec konnekt_24d = {
	.tx_pcm_chs = {{16, 16, 6}, {0, 0, 0} },
	.rx_pcm_chs = {{16, 16, 6}, {0, 0, 0} },
	.has_midi = true,
};

static const struct dice_tc_spec konnekt_live = {
	.tx_pcm_chs = {{16, 16, 6}, {0, 0, 0} },
	.rx_pcm_chs = {{16, 16, 6}, {0, 0, 0} },
	.has_midi = true,
};

static const struct dice_tc_spec studio_konnekt_48 = {
	.tx_pcm_chs = {{16, 16, 8}, {16, 16, 7} },
	.rx_pcm_chs = {{16, 16, 8}, {14, 14, 7} },
	.has_midi = true,
};

static const struct dice_tc_spec digital_konnekt_x32 = {
	.tx_pcm_chs = {{16, 16, 4}, {0, 0, 0} },
	.rx_pcm_chs = {{16, 16, 4}, {0, 0, 0} },
	.has_midi = false,
};

int snd_dice_detect_tcelectronic_formats(struct snd_dice *dice)
{
	static const struct {
		u32 model_id;
		const struct dice_tc_spec *spec;
	} *entry, entries[] = {
		{0x00000020, &konnekt_24d},
		{0x00000021, &konnekt_8},
		{0x00000022, &studio_konnekt_48},
		{0x00000023, &konnekt_live},
		{0x00000024, &desktop_konnekt6},
		{0x00000027, &impact_twin},
		{0x00000030, &digital_konnekt_x32},
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

	if (entry->spec->has_midi) {
		dice->tx_midi_ports[0] = 1;
		dice->rx_midi_ports[0] = 1;
	}

	return 0;
}
