// SPDX-License-Identifier: GPL-2.0-only
//
// motu-register-dsp-message-parser.c - a part of driver for MOTU FireWire series
//
// Copyright (c) 2021 Takashi Sakamoto <o-takashi@sakamocchi.jp>

// Below models allow software to configure their DSP functions by asynchronous transaction
// to access their internal registers.
// * 828 mk2
// * 896hd
// * Traveler
// * 8 pre
// * Ultralite
// * 4 pre
// * Audio Express
//
// Additionally, isochronous packets from the above models include messages to notify state of
// DSP. The messages are two set of 3 byte data in 2nd and 3rd quadlet of data block. When user
// operates hardware components such as dial and switch, corresponding messages are transferred.
// The messages include Hardware metering and MIDI messages as well.

#include "motu.h"

#define MSG_FLAG_POS                    4
#define MSG_FLAG_TYPE_MASK              0xf8
#define MSG_FLAG_MIDI_MASK              0x01
#define MSG_FLAG_MODEL_SPECIFIC_MASK    0x06
#define   MSG_FLAG_8PRE                 0x00
#define   MSG_FLAG_ULTRALITE            0x04
#define   MSG_FLAG_TRAVELER             0x04
#define   MSG_FLAG_828MK2               0x04
#define   MSG_FLAG_896HD                0x04
#define   MSG_FLAG_4PRE                 0x05 // MIDI mask is in 8th byte.
#define   MSG_FLAG_AUDIOEXPRESS         0x05 // MIDI mask is in 8th byte.
#define MSG_FLAG_TYPE_SHIFT             3
#define MSG_VALUE_POS                   5
#define MSG_MIDI_BYTE_POS		6
#define MSG_METER_IDX_POS               7

// In 4 pre and Audio express, meter index is in 6th byte. MIDI flag is in 8th byte and MIDI byte
// is in 7th byte.
#define MSG_METER_IDX_POS_4PRE_AE	6
#define MSG_MIDI_BYTE_POS_4PRE_AE	7
#define MSG_FLAG_MIDI_POS_4PRE_AE	8

enum register_dsp_msg_type {
	// Used for messages with no information.
	INVALID = 0x00,
	MIXER_SELECT = 0x01,
	MIXER_SRC_GAIN = 0x02,
	MIXER_SRC_PAN = 0x03,
	MIXER_SRC_FLAG = 0x04,
	MIXER_OUTPUT_PAIRED_VOLUME = 0x05,
	MIXER_OUTPUT_PAIRED_FLAG = 0x06,
	MAIN_OUTPUT_PAIRED_VOLUME = 0x07,
	HP_OUTPUT_PAIRED_VOLUME = 0x08,
	HP_OUTPUT_PAIRED_ASSIGNMENT = 0x09,
	// Transferred by all models but the purpose is still unknown.
	UNKNOWN_0 = 0x0a,
	// Specific to 828mk2, 896hd, Traveler.
	UNKNOWN_2 = 0x0c,
	// Specific to 828mk2, Traveler, and 896hd (not functional).
	LINE_INPUT_BOOST = 0x0d,
	// Specific to 828mk2, Traveler, and 896hd (not functional).
	LINE_INPUT_NOMINAL_LEVEL = 0x0e,
	// Specific to Ultralite, 4 pre, Audio express, and 8 pre (not functional).
	INPUT_GAIN_AND_INVERT = 0x15,
	// Specific to 4 pre, and Audio express.
	INPUT_FLAG = 0x16,
	// Specific to 4 pre, and Audio express.
	MIXER_SRC_PAIRED_BALANCE = 0x17,
	// Specific to 4 pre, and Audio express.
	MIXER_SRC_PAIRED_WIDTH = 0x18,
	// Transferred by all models. This type of message interposes the series of the other
	// messages. The message delivers signal level up to 96.0 kHz. In 828mk2, 896hd, and
	// Traveler, one of physical outputs is selected for the message. The selection is done
	// by LSB one byte in asynchronous write quadlet transaction to 0x'ffff'f000'0b2c.
	METER = 0x1f,
};

#define EVENT_QUEUE_SIZE	16

struct msg_parser {
	spinlock_t lock;
	struct snd_firewire_motu_register_dsp_meter meter;
	bool meter_pos_quirk;

	struct snd_firewire_motu_register_dsp_parameter param;
	u8 prev_mixer_src_type;
	u8 mixer_ch;
	u8 mixer_src_ch;

	u8 input_ch;
	u8 prev_msg_type;

	u32 event_queue[EVENT_QUEUE_SIZE];
	unsigned int push_pos;
	unsigned int pull_pos;
};

int snd_motu_register_dsp_message_parser_new(struct snd_motu *motu)
{
	struct msg_parser *parser;
	parser = devm_kzalloc(&motu->card->card_dev, sizeof(*parser), GFP_KERNEL);
	if (!parser)
		return -ENOMEM;
	spin_lock_init(&parser->lock);
	if (motu->spec == &snd_motu_spec_4pre || motu->spec == &snd_motu_spec_audio_express)
		parser->meter_pos_quirk = true;
	motu->message_parser = parser;
	return 0;
}

int snd_motu_register_dsp_message_parser_init(struct snd_motu *motu)
{
	struct msg_parser *parser = motu->message_parser;

	parser->prev_mixer_src_type = INVALID;
	parser->mixer_ch = 0xff;
	parser->mixer_src_ch = 0xff;
	parser->prev_msg_type = INVALID;

	return 0;
}

// Rough implementaion of queue without overrun check.
static void queue_event(struct snd_motu *motu, u8 msg_type, u8 identifier0, u8 identifier1, u8 val)
{
	struct msg_parser *parser = motu->message_parser;
	unsigned int pos = parser->push_pos;
	u32 entry;

	if (!motu->hwdep || motu->hwdep->used == 0)
		return;

	entry = (msg_type << 24) | (identifier0 << 16) | (identifier1 << 8) | val;
	parser->event_queue[pos] = entry;

	++pos;
	if (pos >= EVENT_QUEUE_SIZE)
		pos = 0;
	parser->push_pos = pos;
}

void snd_motu_register_dsp_message_parser_parse(const struct amdtp_stream *s,
						const struct pkt_desc *desc, unsigned int count)
{
	struct snd_motu *motu = container_of(s, struct snd_motu, tx_stream);
	unsigned int data_block_quadlets = s->data_block_quadlets;
	struct msg_parser *parser = motu->message_parser;
	bool meter_pos_quirk = parser->meter_pos_quirk;
	unsigned int pos = parser->push_pos;
	int i;

	guard(spinlock_irqsave)(&parser->lock);

	for (i = 0; i < count; ++i) {
		__be32 *buffer = desc->ctx_payload;
		unsigned int data_blocks = desc->data_blocks;
		int j;

		desc = amdtp_stream_next_packet_desc(s, desc);

		for (j = 0; j < data_blocks; ++j) {
			u8 *b = (u8 *)buffer;
			u8 msg_type = (b[MSG_FLAG_POS] & MSG_FLAG_TYPE_MASK) >> MSG_FLAG_TYPE_SHIFT;
			u8 val = b[MSG_VALUE_POS];

			buffer += data_block_quadlets;

			switch (msg_type) {
			case MIXER_SELECT:
			{
				u8 mixer_ch = val / 0x20;
				if (mixer_ch < SNDRV_FIREWIRE_MOTU_REGISTER_DSP_MIXER_COUNT) {
					parser->mixer_src_ch = 0;
					parser->mixer_ch = mixer_ch;
				}
				break;
			}
			case MIXER_SRC_GAIN:
			case MIXER_SRC_PAN:
			case MIXER_SRC_FLAG:
			case MIXER_SRC_PAIRED_BALANCE:
			case MIXER_SRC_PAIRED_WIDTH:
			{
				struct snd_firewire_motu_register_dsp_parameter *param = &parser->param;
				u8 mixer_ch = parser->mixer_ch;
				u8 mixer_src_ch = parser->mixer_src_ch;

				if (msg_type != parser->prev_mixer_src_type)
					mixer_src_ch = 0;
				else
					++mixer_src_ch;
				parser->prev_mixer_src_type = msg_type;

				if (mixer_ch < SNDRV_FIREWIRE_MOTU_REGISTER_DSP_MIXER_COUNT &&
				    mixer_src_ch < SNDRV_FIREWIRE_MOTU_REGISTER_DSP_MIXER_SRC_COUNT) {
					u8 mixer_ch = parser->mixer_ch;

					switch (msg_type) {
					case MIXER_SRC_GAIN:
						if (param->mixer.source[mixer_ch].gain[mixer_src_ch] != val) {
							queue_event(motu, msg_type, mixer_ch, mixer_src_ch, val);
							param->mixer.source[mixer_ch].gain[mixer_src_ch] = val;
						}
						break;
					case MIXER_SRC_PAN:
						if (param->mixer.source[mixer_ch].pan[mixer_src_ch] != val) {
							queue_event(motu, msg_type, mixer_ch, mixer_src_ch, val);
							param->mixer.source[mixer_ch].pan[mixer_src_ch] = val;
						}
						break;
					case MIXER_SRC_FLAG:
						if (param->mixer.source[mixer_ch].flag[mixer_src_ch] != val) {
							queue_event(motu, msg_type, mixer_ch, mixer_src_ch, val);
							param->mixer.source[mixer_ch].flag[mixer_src_ch] = val;
						}
						break;
					case MIXER_SRC_PAIRED_BALANCE:
						if (param->mixer.source[mixer_ch].paired_balance[mixer_src_ch] != val) {
							queue_event(motu, msg_type, mixer_ch, mixer_src_ch, val);
							param->mixer.source[mixer_ch].paired_balance[mixer_src_ch] = val;
						}
						break;
					case MIXER_SRC_PAIRED_WIDTH:
						if (param->mixer.source[mixer_ch].paired_width[mixer_src_ch] != val) {
							queue_event(motu, msg_type, mixer_ch, mixer_src_ch, val);
							param->mixer.source[mixer_ch].paired_width[mixer_src_ch] = val;
						}
						break;
					default:
						break;
					}

					parser->mixer_src_ch = mixer_src_ch;
				}
				break;
			}
			case MIXER_OUTPUT_PAIRED_VOLUME:
			case MIXER_OUTPUT_PAIRED_FLAG:
			{
				struct snd_firewire_motu_register_dsp_parameter *param = &parser->param;
				u8 mixer_ch = parser->mixer_ch;

				if (mixer_ch < SNDRV_FIREWIRE_MOTU_REGISTER_DSP_MIXER_COUNT) {
					switch (msg_type) {
					case MIXER_OUTPUT_PAIRED_VOLUME:
						if (param->mixer.output.paired_volume[mixer_ch] != val) {
							queue_event(motu, msg_type, mixer_ch, 0, val);
							param->mixer.output.paired_volume[mixer_ch] = val;
						}
						break;
					case MIXER_OUTPUT_PAIRED_FLAG:
						if (param->mixer.output.paired_flag[mixer_ch] != val) {
							queue_event(motu, msg_type, mixer_ch, 0, val);
							param->mixer.output.paired_flag[mixer_ch] = val;
						}
						break;
					default:
						break;
					}
				}
				break;
			}
			case MAIN_OUTPUT_PAIRED_VOLUME:
				if (parser->param.output.main_paired_volume != val) {
					queue_event(motu, msg_type, 0, 0, val);
					parser->param.output.main_paired_volume = val;
				}
				break;
			case HP_OUTPUT_PAIRED_VOLUME:
				if (parser->param.output.hp_paired_volume != val) {
					queue_event(motu, msg_type, 0, 0, val);
					parser->param.output.hp_paired_volume = val;
				}
				break;
			case HP_OUTPUT_PAIRED_ASSIGNMENT:
				if (parser->param.output.hp_paired_assignment != val) {
					queue_event(motu, msg_type, 0, 0, val);
					parser->param.output.hp_paired_assignment = val;
				}
				break;
			case LINE_INPUT_BOOST:
				if (parser->param.line_input.boost_flag != val) {
					queue_event(motu, msg_type, 0, 0, val);
					parser->param.line_input.boost_flag = val;
				}
				break;
			case LINE_INPUT_NOMINAL_LEVEL:
				if (parser->param.line_input.nominal_level_flag != val) {
					queue_event(motu, msg_type, 0, 0, val);
					parser->param.line_input.nominal_level_flag = val;
				}
				break;
			case INPUT_GAIN_AND_INVERT:
			case INPUT_FLAG:
			{
				struct snd_firewire_motu_register_dsp_parameter *param = &parser->param;
				u8 input_ch = parser->input_ch;

				if (parser->prev_msg_type != msg_type)
					input_ch = 0;
				else
					++input_ch;

				if (input_ch < SNDRV_FIREWIRE_MOTU_REGISTER_DSP_INPUT_COUNT) {
					switch (msg_type) {
					case INPUT_GAIN_AND_INVERT:
						if (param->input.gain_and_invert[input_ch] != val) {
							queue_event(motu, msg_type, input_ch, 0, val);
							param->input.gain_and_invert[input_ch] = val;
						}
						break;
					case INPUT_FLAG:
						if (param->input.flag[input_ch] != val) {
							queue_event(motu, msg_type, input_ch, 0, val);
							param->input.flag[input_ch] = val;
						}
						break;
					default:
						break;
					}
					parser->input_ch = input_ch;
				}
				break;
			}
			case UNKNOWN_0:
			case UNKNOWN_2:
				break;
			case METER:
			{
				u8 pos;

				if (!meter_pos_quirk)
					pos = b[MSG_METER_IDX_POS];
				else
					pos = b[MSG_METER_IDX_POS_4PRE_AE];

				if (pos < SNDRV_FIREWIRE_MOTU_REGISTER_DSP_METER_INPUT_COUNT) {
					parser->meter.data[pos] = val;
				} else if (pos >= 0x80) {
					pos -= (0x80 - SNDRV_FIREWIRE_MOTU_REGISTER_DSP_METER_INPUT_COUNT);

					if (pos < SNDRV_FIREWIRE_MOTU_REGISTER_DSP_METER_COUNT)
						parser->meter.data[pos] = val;
				}

				// The message for meter is interruptible to the series of other
				// types of messages. Don't cache it.
				fallthrough;
			}
			case INVALID:
			default:
				// Don't cache it.
				continue;
			}

			parser->prev_msg_type = msg_type;
		}
	}

	if (pos != parser->push_pos)
		wake_up(&motu->hwdep_wait);
}

void snd_motu_register_dsp_message_parser_copy_meter(struct snd_motu *motu,
						struct snd_firewire_motu_register_dsp_meter *meter)
{
	struct msg_parser *parser = motu->message_parser;

	guard(spinlock_irqsave)(&parser->lock);
	memcpy(meter, &parser->meter, sizeof(*meter));
}

void snd_motu_register_dsp_message_parser_copy_parameter(struct snd_motu *motu,
					struct snd_firewire_motu_register_dsp_parameter *param)
{
	struct msg_parser *parser = motu->message_parser;

	guard(spinlock_irqsave)(&parser->lock);
	memcpy(param, &parser->param, sizeof(*param));
}

unsigned int snd_motu_register_dsp_message_parser_count_event(struct snd_motu *motu)
{
	struct msg_parser *parser = motu->message_parser;

	if (parser->pull_pos > parser->push_pos)
		return EVENT_QUEUE_SIZE - parser->pull_pos + parser->push_pos;
	else
		return parser->push_pos - parser->pull_pos;
}

bool snd_motu_register_dsp_message_parser_copy_event(struct snd_motu *motu, u32 *event)
{
	struct msg_parser *parser = motu->message_parser;
	unsigned int pos = parser->pull_pos;

	if (pos == parser->push_pos)
		return false;

	guard(spinlock_irqsave)(&parser->lock);

	*event = parser->event_queue[pos];

	++pos;
	if (pos >= EVENT_QUEUE_SIZE)
		pos = 0;
	parser->pull_pos = pos;

	return true;
}
