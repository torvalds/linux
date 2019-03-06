/*
 * ff-protocol-ff400.c - a part of driver for RME Fireface series
 *
 * Copyright (c) 2015-2017 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include <linux/delay.h>
#include "ff.h"

#define FF400_STF		0x000080100500ull
#define FF400_RX_PACKET_FORMAT	0x000080100504ull
#define FF400_ISOC_COMM_START	0x000080100508ull
#define FF400_TX_PACKET_FORMAT	0x00008010050cull
#define FF400_ISOC_COMM_STOP	0x000080100510ull

/*
 * Fireface 400 manages isochronous channel number in 3 bit field. Therefore,
 * we can allocate between 0 and 7 channel.
 */
static int keep_resources(struct snd_ff *ff, unsigned int rate)
{
	enum snd_ff_stream_mode mode;
	int i;
	int err;

	// Check whether the given value is supported or not.
	for (i = 0; i < CIP_SFC_COUNT; i++) {
		if (amdtp_rate_table[i] == rate)
			break;
	}
	if (i >= CIP_SFC_COUNT)
		return -EINVAL;

	err = snd_ff_stream_get_multiplier_mode(i, &mode);
	if (err < 0)
		return err;

	/* Keep resources for in-stream. */
	ff->tx_resources.channels_mask = 0x00000000000000ffuLL;
	err = fw_iso_resources_allocate(&ff->tx_resources,
			amdtp_stream_get_max_payload(&ff->tx_stream),
			fw_parent_device(ff->unit)->max_speed);
	if (err < 0)
		return err;

	/* Keep resources for out-stream. */
	err = amdtp_ff_set_parameters(&ff->rx_stream, rate,
				      ff->spec->pcm_playback_channels[mode]);
	if (err < 0)
		return err;
	ff->rx_resources.channels_mask = 0x00000000000000ffuLL;
	err = fw_iso_resources_allocate(&ff->rx_resources,
			amdtp_stream_get_max_payload(&ff->rx_stream),
			fw_parent_device(ff->unit)->max_speed);
	if (err < 0)
		fw_iso_resources_free(&ff->tx_resources);

	return err;
}

static int ff400_begin_session(struct snd_ff *ff, unsigned int rate)
{
	__le32 reg;
	int err;

	err = keep_resources(ff, rate);
	if (err < 0)
		return err;

	/* Set the number of data blocks transferred in a second. */
	reg = cpu_to_le32(rate);
	err = snd_fw_transaction(ff->unit, TCODE_WRITE_QUADLET_REQUEST,
				 FF400_STF, &reg, sizeof(reg), 0);
	if (err < 0)
		return err;

	msleep(100);

	/*
	 * Set isochronous channel and the number of quadlets of received
	 * packets.
	 */
	reg = cpu_to_le32(((ff->rx_stream.data_block_quadlets << 3) << 8) |
			  ff->rx_resources.channel);
	err = snd_fw_transaction(ff->unit, TCODE_WRITE_QUADLET_REQUEST,
				 FF400_RX_PACKET_FORMAT, &reg, sizeof(reg), 0);
	if (err < 0)
		return err;

	/*
	 * Set isochronous channel and the number of quadlets of transmitted
	 * packet.
	 */
	/* TODO: investigate the purpose of this 0x80. */
	reg = cpu_to_le32((0x80 << 24) |
			  (ff->tx_resources.channel << 5) |
			  (ff->tx_stream.data_block_quadlets));
	err = snd_fw_transaction(ff->unit, TCODE_WRITE_QUADLET_REQUEST,
				 FF400_TX_PACKET_FORMAT, &reg, sizeof(reg), 0);
	if (err < 0)
		return err;

	/* Allow to transmit packets. */
	reg = cpu_to_le32(0x00000001);
	return snd_fw_transaction(ff->unit, TCODE_WRITE_QUADLET_REQUEST,
				 FF400_ISOC_COMM_START, &reg, sizeof(reg), 0);
}

static void ff400_finish_session(struct snd_ff *ff)
{
	__le32 reg;

	reg = cpu_to_le32(0x80000000);
	snd_fw_transaction(ff->unit, TCODE_WRITE_QUADLET_REQUEST,
			   FF400_ISOC_COMM_STOP, &reg, sizeof(reg), 0);
}

static void ff400_handle_midi_msg(struct snd_ff *ff, __le32 *buf, size_t length)
{
	int i;

	for (i = 0; i < length / 4; i++) {
		u32 quad = le32_to_cpu(buf[i]);
		u8 byte;
		unsigned int index;
		struct snd_rawmidi_substream *substream;

		/* Message in first port. */
		/*
		 * This value may represent the index of this unit when the same
		 * units are on the same IEEE 1394 bus. This driver doesn't use
		 * it.
		 */
		index = (quad >> 8) & 0xff;
		if (index > 0) {
			substream = READ_ONCE(ff->tx_midi_substreams[0]);
			if (substream != NULL) {
				byte = quad & 0xff;
				snd_rawmidi_receive(substream, &byte, 1);
			}
		}

		/* Message in second port. */
		index = (quad >> 24) & 0xff;
		if (index > 0) {
			substream = READ_ONCE(ff->tx_midi_substreams[1]);
			if (substream != NULL) {
				byte = (quad >> 16) & 0xff;
				snd_rawmidi_receive(substream, &byte, 1);
			}
		}
	}
}

const struct snd_ff_protocol snd_ff_protocol_ff400 = {
	.handle_midi_msg	= ff400_handle_midi_msg,
	.begin_session		= ff400_begin_session,
	.finish_session		= ff400_finish_session,
};
