// SPDX-License-Identifier: GPL-2.0-only
//
// motu-command-dsp-message-parser.c - a part of driver for MOTU FireWire series
//
// Copyright (c) 2021 Takashi Sakamoto <o-takashi@sakamocchi.jp>

// Below models allow software to configure their DSP function by command transferred in
// asynchronous transaction:
//  * 828 mk3 (FireWire only and Hybrid)
//  * 896 mk3 (FireWire only and Hybrid)
//  * Ultralite mk3 (FireWire only and Hybrid)
//  * Traveler mk3
//  * Track 16
//
// Isochronous packets from the above models includes messages to report state of hardware meter.

#include "motu.h"

enum msg_parser_state {
	INITIALIZED,
	FRAGMENT_DETECTED,
	AVAILABLE,
};

struct msg_parser {
	spinlock_t lock;
	enum msg_parser_state state;
	unsigned int interval;
	unsigned int message_count;
	unsigned int fragment_pos;
	unsigned int value_index;
	u64 value;
	struct snd_firewire_motu_command_dsp_meter meter;
};

int snd_motu_command_dsp_message_parser_new(struct snd_motu *motu)
{
	struct msg_parser *parser;

	parser = devm_kzalloc(&motu->card->card_dev, sizeof(*parser), GFP_KERNEL);
	if (!parser)
		return -ENOMEM;
	spin_lock_init(&parser->lock);
	motu->message_parser = parser;

	return 0;
}

int snd_motu_command_dsp_message_parser_init(struct snd_motu *motu, enum cip_sfc sfc)
{
	struct msg_parser *parser = motu->message_parser;

	parser->state = INITIALIZED;

	// All of data blocks don't have messages with meaningful information.
	switch (sfc) {
	case CIP_SFC_176400:
	case CIP_SFC_192000:
		parser->interval = 4;
		break;
	case CIP_SFC_88200:
	case CIP_SFC_96000:
		parser->interval = 2;
		break;
	case CIP_SFC_32000:
	case CIP_SFC_44100:
	case CIP_SFC_48000:
	default:
		parser->interval = 1;
		break;
	}

	return 0;
}

#define FRAGMENT_POS			6
#define MIDI_BYTE_POS			7
#define MIDI_FLAG_POS			8
// One value of hardware meter consists of 4 messages.
#define FRAGMENTS_PER_VALUE		4
#define VALUES_AT_IMAGE_END		0xffffffffffffffff

void snd_motu_command_dsp_message_parser_parse(const struct amdtp_stream *s,
					const struct pkt_desc *desc, unsigned int count)
{
	struct snd_motu *motu = container_of(s, struct snd_motu, tx_stream);
	unsigned int data_block_quadlets = s->data_block_quadlets;
	struct msg_parser *parser = motu->message_parser;
	unsigned int interval = parser->interval;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&parser->lock, flags);

	for (i = 0; i < count; ++i) {
		__be32 *buffer = desc->ctx_payload;
		unsigned int data_blocks = desc->data_blocks;
		int j;

		desc = amdtp_stream_next_packet_desc(s, desc);

		for (j = 0; j < data_blocks; ++j) {
			u8 *b = (u8 *)buffer;
			buffer += data_block_quadlets;

			switch (parser->state) {
			case INITIALIZED:
			{
				u8 fragment = b[FRAGMENT_POS];

				if (fragment > 0) {
					parser->value = fragment;
					parser->message_count = 1;
					parser->state = FRAGMENT_DETECTED;
				}
				break;
			}
			case FRAGMENT_DETECTED:
			{
				if (parser->message_count % interval == 0) {
					u8 fragment = b[FRAGMENT_POS];

					parser->value >>= 8;
					parser->value |= (u64)fragment << 56;

					if (parser->value == VALUES_AT_IMAGE_END) {
						parser->state = AVAILABLE;
						parser->fragment_pos = 0;
						parser->value_index = 0;
						parser->message_count = 0;
					}
				}
				++parser->message_count;
				break;
			}
			case AVAILABLE:
			default:
			{
				if (parser->message_count % interval == 0) {
					u8 fragment = b[FRAGMENT_POS];

					parser->value >>= 8;
					parser->value |= (u64)fragment << 56;
					++parser->fragment_pos;

					if (parser->fragment_pos == 4) {
						// Skip the last two quadlets since they could be
						// invalid value (0xffffffff) as floating point
						// number.
						if (parser->value_index <
						    SNDRV_FIREWIRE_MOTU_COMMAND_DSP_METER_COUNT - 2) {
							u32 val = (u32)(parser->value >> 32);
							parser->meter.data[parser->value_index] = val;
						}
						++parser->value_index;
						parser->fragment_pos = 0;
					}

					if (parser->value == VALUES_AT_IMAGE_END) {
						parser->value_index = 0;
						parser->fragment_pos = 0;
						parser->message_count = 0;
					}
				}
				++parser->message_count;
				break;
			}
			}
		}
	}

	spin_unlock_irqrestore(&parser->lock, flags);
}

void snd_motu_command_dsp_message_parser_copy_meter(struct snd_motu *motu,
					struct snd_firewire_motu_command_dsp_meter *meter)
{
	struct msg_parser *parser = motu->message_parser;
	unsigned long flags;

	spin_lock_irqsave(&parser->lock, flags);
	memcpy(meter, &parser->meter, sizeof(*meter));
	spin_unlock_irqrestore(&parser->lock, flags);
}
