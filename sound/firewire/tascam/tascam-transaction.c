/*
 * tascam-transaction.c - a part of driver for TASCAM FireWire series
 *
 * Copyright (c) 2015 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "tascam.h"

/*
 * When return minus value, given argument is not MIDI status.
 * When return 0, given argument is a beginning of system exclusive.
 * When return the others, given argument is MIDI data.
 */
static inline int calculate_message_bytes(u8 status)
{
	switch (status) {
	case 0xf6:	/* Tune request. */
	case 0xf8:	/* Timing clock. */
	case 0xfa:	/* Start. */
	case 0xfb:	/* Continue. */
	case 0xfc:	/* Stop. */
	case 0xfe:	/* Active sensing. */
	case 0xff:	/* System reset. */
		return 1;
	case 0xf1:	/* MIDI time code quarter frame. */
	case 0xf3:	/* Song select. */
		return 2;
	case 0xf2:	/* Song position pointer. */
		return 3;
	case 0xf0:	/* Exclusive. */
		return 0;
	case 0xf7:	/* End of exclusive. */
		break;
	case 0xf4:	/* Undefined. */
	case 0xf5:	/* Undefined. */
	case 0xf9:	/* Undefined. */
	case 0xfd:	/* Undefined. */
		break;
	default:
		switch (status & 0xf0) {
		case 0x80:	/* Note on. */
		case 0x90:	/* Note off. */
		case 0xa0:	/* Polyphonic key pressure. */
		case 0xb0:	/* Control change and Mode change. */
		case 0xe0:	/* Pitch bend change. */
			return 3;
		case 0xc0:	/* Program change. */
		case 0xd0:	/* Channel pressure. */
			return 2;
		default:
		break;
		}
	break;
	}

	return -EINVAL;
}

static void handle_midi_tx(struct fw_card *card, struct fw_request *request,
			   int tcode, int destination, int source,
			   int generation, unsigned long long offset,
			   void *data, size_t length, void *callback_data)
{
	struct snd_tscm *tscm = callback_data;
	u32 *buf = (u32 *)data;
	unsigned int messages;
	unsigned int i;
	unsigned int port;
	struct snd_rawmidi_substream *substream;
	u8 *b;
	int bytes;

	if (offset != tscm->async_handler.offset)
		goto end;

	messages = length / 8;
	for (i = 0; i < messages; i++) {
		b = (u8 *)(buf + i * 2);

		port = b[0] >> 4;
		/* TODO: support virtual MIDI ports. */
		if (port > tscm->spec->midi_capture_ports)
			goto end;

		/* Assume the message length. */
		bytes = calculate_message_bytes(b[1]);
		/* On MIDI data or exclusives. */
		if (bytes <= 0) {
			/* Seek the end of exclusives. */
			for (bytes = 1; bytes < 4; bytes++) {
				if (b[bytes] == 0xf7)
					break;
			}
			if (bytes == 4)
				bytes = 3;
		}

		substream = ACCESS_ONCE(tscm->tx_midi_substreams[port]);
		if (substream != NULL)
			snd_rawmidi_receive(substream, b + 1, bytes);
	}
end:
	fw_send_response(card, request, RCODE_COMPLETE);
}

int snd_tscm_transaction_register(struct snd_tscm *tscm)
{
	static const struct fw_address_region resp_register_region = {
		.start	= 0xffffe0000000ull,
		.end	= 0xffffe000ffffull,
	};
	int err;

	/*
	 * Usually, two quadlets are transferred by one transaction. The first
	 * quadlet has MIDI messages, the rest includes timestamp.
	 * Sometimes, 8 set of the data is transferred by a block transaction.
	 */
	tscm->async_handler.length = 8 * 8;
	tscm->async_handler.address_callback = handle_midi_tx;
	tscm->async_handler.callback_data = tscm;

	err = fw_core_add_address_handler(&tscm->async_handler,
					  &resp_register_region);
	if (err < 0)
		return err;

	err = snd_tscm_transaction_reregister(tscm);
	if (err < 0)
		fw_core_remove_address_handler(&tscm->async_handler);

	return err;
}

/* At bus reset, these registers are cleared. */
int snd_tscm_transaction_reregister(struct snd_tscm *tscm)
{
	struct fw_device *device = fw_parent_device(tscm->unit);
	__be32 reg;
	int err;

	/* Register messaging address. Block transaction is not allowed. */
	reg = cpu_to_be32((device->card->node_id << 16) |
			  (tscm->async_handler.offset >> 32));
	err = snd_fw_transaction(tscm->unit, TCODE_WRITE_QUADLET_REQUEST,
				 TSCM_ADDR_BASE + TSCM_OFFSET_MIDI_TX_ADDR_HI,
				 &reg, sizeof(reg), 0);
	if (err < 0)
		return err;

	reg = cpu_to_be32(tscm->async_handler.offset);
	err = snd_fw_transaction(tscm->unit, TCODE_WRITE_QUADLET_REQUEST,
				 TSCM_ADDR_BASE + TSCM_OFFSET_MIDI_TX_ADDR_LO,
				 &reg, sizeof(reg), 0);
	if (err < 0)
		return err;

	/* Turn on messaging. */
	reg = cpu_to_be32(0x00000001);
	return snd_fw_transaction(tscm->unit, TCODE_WRITE_QUADLET_REQUEST,
				  TSCM_ADDR_BASE + TSCM_OFFSET_MIDI_TX_ON,
				  &reg, sizeof(reg), 0);
}

void snd_tscm_transaction_unregister(struct snd_tscm *tscm)
{
	__be32 reg;

	/* Turn off messaging. */
	reg = cpu_to_be32(0x00000000);
	snd_fw_transaction(tscm->unit, TCODE_WRITE_QUADLET_REQUEST,
			   TSCM_ADDR_BASE + TSCM_OFFSET_MIDI_TX_ON,
			   &reg, sizeof(reg), 0);

	/* Unregister the address. */
	snd_fw_transaction(tscm->unit, TCODE_WRITE_QUADLET_REQUEST,
			   TSCM_ADDR_BASE + TSCM_OFFSET_MIDI_TX_ADDR_HI,
			   &reg, sizeof(reg), 0);
	snd_fw_transaction(tscm->unit, TCODE_WRITE_QUADLET_REQUEST,
			   TSCM_ADDR_BASE + TSCM_OFFSET_MIDI_TX_ADDR_LO,
			   &reg, sizeof(reg), 0);

	fw_core_remove_address_handler(&tscm->async_handler);
}
