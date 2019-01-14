// SPDX-License-Identifier: GPL-2.0
/*
 * dice-extension.c - a part of driver for DICE based devices
 *
 * Copyright (c) 2018 Takashi Sakamoto
 */

#include "dice.h"

/* For TCD2210/2220, TCAT defines extension of application protocol. */

#define DICE_EXT_APP_SPACE		0xffffe0200000uLL

#define DICE_EXT_APP_CAPS_OFFSET	0x00
#define DICE_EXT_APP_CAPS_SIZE		0x04
#define DICE_EXT_APP_CMD_OFFSET		0x08
#define DICE_EXT_APP_CMD_SIZE		0x0c
#define DICE_EXT_APP_MIXER_OFFSET	0x10
#define DICE_EXT_APP_MIXER_SIZE		0x14
#define DICE_EXT_APP_PEAK_OFFSET	0x18
#define DICE_EXT_APP_PEAK_SIZE		0x1c
#define DICE_EXT_APP_ROUTER_OFFSET	0x20
#define DICE_EXT_APP_ROUTER_SIZE	0x24
#define DICE_EXT_APP_STREAM_OFFSET	0x28
#define DICE_EXT_APP_STREAM_SIZE	0x2c
#define DICE_EXT_APP_CURRENT_OFFSET	0x30
#define DICE_EXT_APP_CURRENT_SIZE	0x34
#define DICE_EXT_APP_STANDALONE_OFFSET	0x38
#define DICE_EXT_APP_STANDALONE_SIZE	0x3c
#define DICE_EXT_APP_APPLICATION_OFFSET	0x40
#define DICE_EXT_APP_APPLICATION_SIZE	0x44

#define EXT_APP_STREAM_TX_NUMBER	0x0000
#define EXT_APP_STREAM_RX_NUMBER	0x0004
#define EXT_APP_STREAM_ENTRIES		0x0008
#define EXT_APP_STREAM_ENTRY_SIZE	0x010c
#define  EXT_APP_NUMBER_AUDIO		0x0000
#define  EXT_APP_NUMBER_MIDI		0x0004
#define  EXT_APP_NAMES			0x0008
#define   EXT_APP_NAMES_SIZE		256
#define  EXT_APP_AC3			0x0108

#define EXT_APP_CONFIG_LOW_ROUTER	0x0000
#define EXT_APP_CONFIG_LOW_STREAM	0x1000
#define EXT_APP_CONFIG_MIDDLE_ROUTER	0x2000
#define EXT_APP_CONFIG_MIDDLE_STREAM	0x3000
#define EXT_APP_CONFIG_HIGH_ROUTER	0x4000
#define EXT_APP_CONFIG_HIGH_STREAM	0x5000

static inline int read_transaction(struct snd_dice *dice, u64 section_addr,
				   u32 offset, void *buf, size_t len)
{
	return snd_fw_transaction(dice->unit,
				  len == 4 ? TCODE_READ_QUADLET_REQUEST :
					     TCODE_READ_BLOCK_REQUEST,
				  section_addr + offset, buf, len, 0);
}

static int read_stream_entries(struct snd_dice *dice, u64 section_addr,
			       u32 base_offset, unsigned int stream_count,
			       unsigned int mode,
			       unsigned int pcm_channels[MAX_STREAMS][3],
			       unsigned int midi_ports[MAX_STREAMS])
{
	u32 entry_offset;
	__be32 reg[2];
	int err;
	int i;

	for (i = 0; i < stream_count; ++i) {
		entry_offset = base_offset + i * EXT_APP_STREAM_ENTRY_SIZE;
		err = read_transaction(dice, section_addr,
				    entry_offset + EXT_APP_NUMBER_AUDIO,
				    reg, sizeof(reg));
		if (err < 0)
			return err;
		pcm_channels[i][mode] = be32_to_cpu(reg[0]);
		midi_ports[i] = max(midi_ports[i], be32_to_cpu(reg[1]));
	}

	return 0;
}

static int detect_stream_formats(struct snd_dice *dice, u64 section_addr)
{
	u32 base_offset;
	__be32 reg[2];
	unsigned int stream_count;
	int mode;
	int err = 0;

	for (mode = 0; mode < SND_DICE_RATE_MODE_COUNT; ++mode) {
		unsigned int cap;

		/*
		 * Some models report stream formats at highest mode, however
		 * they don't support the mode. Check clock capabilities.
		 */
		if (mode == 2) {
			cap = CLOCK_CAP_RATE_176400 | CLOCK_CAP_RATE_192000;
		} else if (mode == 1) {
			cap = CLOCK_CAP_RATE_88200 | CLOCK_CAP_RATE_96000;
		} else {
			cap = CLOCK_CAP_RATE_32000 | CLOCK_CAP_RATE_44100 |
			      CLOCK_CAP_RATE_48000;
		}
		if (!(cap & dice->clock_caps))
			continue;

		base_offset = 0x2000 * mode + 0x1000;

		err = read_transaction(dice, section_addr,
				       base_offset + EXT_APP_STREAM_TX_NUMBER,
				       &reg, sizeof(reg));
		if (err < 0)
			break;

		base_offset += EXT_APP_STREAM_ENTRIES;
		stream_count = be32_to_cpu(reg[0]);
		err = read_stream_entries(dice, section_addr, base_offset,
					  stream_count, mode,
					  dice->tx_pcm_chs,
					  dice->tx_midi_ports);
		if (err < 0)
			break;

		base_offset += stream_count * EXT_APP_STREAM_ENTRY_SIZE;
		stream_count = be32_to_cpu(reg[1]);
		err = read_stream_entries(dice, section_addr, base_offset,
					  stream_count,
					  mode, dice->rx_pcm_chs,
					  dice->rx_midi_ports);
		if (err < 0)
			break;
	}

	return err;
}

int snd_dice_detect_extension_formats(struct snd_dice *dice)
{
	__be32 *pointers;
	unsigned int i;
	u64 section_addr;
	int err;

	pointers = kmalloc_array(9, sizeof(__be32) * 2, GFP_KERNEL);
	if (pointers == NULL)
		return -ENOMEM;

	err = snd_fw_transaction(dice->unit, TCODE_READ_BLOCK_REQUEST,
				 DICE_EXT_APP_SPACE, pointers,
				 9 * sizeof(__be32) * 2, 0);
	if (err < 0)
		goto end;

	/* Check two of them for offset have the same value or not. */
	for (i = 0; i < 9; ++i) {
		int j;

		for (j = i + 1; j < 9; ++j) {
			if (pointers[i * 2] == pointers[j * 2])
				goto end;
		}
	}

	section_addr = DICE_EXT_APP_SPACE + be32_to_cpu(pointers[12]) * 4;
	err = detect_stream_formats(dice, section_addr);
end:
	kfree(pointers);
	return err;
}
