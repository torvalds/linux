// SPDX-License-Identifier: GPL-2.0
// ff-protocol-former.c - a part of driver for RME Fireface series
//
// Copyright (c) 2019 Takashi Sakamoto
//
// Licensed under the terms of the GNU General Public License, version 2.

#include <linux/delay.h>

#include "ff.h"

#define FORMER_REG_SYNC_STATUS		0x0000801c0000ull
/* For block write request. */
#define FORMER_REG_FETCH_PCM_FRAMES	0x0000801c0000ull

static int former_switch_fetching_mode(struct snd_ff *ff, bool enable)
{
	unsigned int count;
	__le32 *reg;
	int i;
	int err;

	count = 0;
	for (i = 0; i < SND_FF_STREAM_MODE_COUNT; ++i)
		count = max(count, ff->spec->pcm_playback_channels[i]);

	reg = kcalloc(count, sizeof(__le32), GFP_KERNEL);
	if (!reg)
		return -ENOMEM;

	if (!enable) {
		/*
		 * Each quadlet is corresponding to data channels in a data
		 * blocks in reverse order. Precisely, quadlets for available
		 * data channels should be enabled. Here, I take second best
		 * to fetch PCM frames from all of data channels regardless of
		 * stf.
		 */
		for (i = 0; i < count; ++i)
			reg[i] = cpu_to_le32(0x00000001);
	}

	err = snd_fw_transaction(ff->unit, TCODE_WRITE_BLOCK_REQUEST,
				 FORMER_REG_FETCH_PCM_FRAMES, reg,
				 sizeof(__le32) * count, 0);
	kfree(reg);
	return err;
}

static void dump_clock_config(struct snd_ff *ff, struct snd_info_buffer *buffer)
{
	__le32 reg;
	u32 data;
	unsigned int rate;
	const char *src;
	int err;

	err = snd_fw_transaction(ff->unit, TCODE_READ_BLOCK_REQUEST,
				 SND_FF_REG_CLOCK_CONFIG, &reg, sizeof(reg), 0);
	if (err < 0)
		return;

	data = le32_to_cpu(reg);

	snd_iprintf(buffer, "Output S/PDIF format: %s (Emphasis: %s)\n",
		    (data & 0x20) ? "Professional" : "Consumer",
		    (data & 0x40) ? "on" : "off");

	snd_iprintf(buffer, "Optical output interface format: %s\n",
		    ((data >> 8) & 0x01) ? "S/PDIF" : "ADAT");

	snd_iprintf(buffer, "Word output single speed: %s\n",
		    ((data >> 8) & 0x20) ? "on" : "off");

	snd_iprintf(buffer, "S/PDIF input interface: %s\n",
		    ((data >> 8) & 0x02) ? "Optical" : "Coaxial");

	switch ((data >> 1) & 0x03) {
	case 0x01:
		rate = 32000;
		break;
	case 0x00:
		rate = 44100;
		break;
	case 0x03:
		rate = 48000;
		break;
	case 0x02:
	default:
		return;
	}

	if (data & 0x08)
		rate *= 2;
	else if (data & 0x10)
		rate *= 4;

	snd_iprintf(buffer, "Sampling rate: %d\n", rate);

	if (data & 0x01) {
		src = "Internal";
	} else {
		switch ((data >> 10) & 0x07) {
		case 0x00:
			src = "ADAT1";
			break;
		case 0x01:
			src = "ADAT2";
			break;
		case 0x03:
			src = "S/PDIF";
			break;
		case 0x04:
			src = "Word";
			break;
		case 0x05:
			src = "LTC";
			break;
		default:
			return;
		}
	}

	snd_iprintf(buffer, "Sync to clock source: %s\n", src);
}

static void dump_sync_status(struct snd_ff *ff, struct snd_info_buffer *buffer)
{
	__le32 reg;
	u32 data;
	int err;

	err = snd_fw_transaction(ff->unit, TCODE_READ_QUADLET_REQUEST,
				 FORMER_REG_SYNC_STATUS, &reg, sizeof(reg), 0);
	if (err < 0)
		return;

	data = le32_to_cpu(reg);

	snd_iprintf(buffer, "External source detection:\n");

	snd_iprintf(buffer, "Word Clock:");
	if ((data >> 24) & 0x20) {
		if ((data >> 24) & 0x40)
			snd_iprintf(buffer, "sync\n");
		else
			snd_iprintf(buffer, "lock\n");
	} else {
		snd_iprintf(buffer, "none\n");
	}

	snd_iprintf(buffer, "S/PDIF:");
	if ((data >> 16) & 0x10) {
		if ((data >> 16) & 0x04)
			snd_iprintf(buffer, "sync\n");
		else
			snd_iprintf(buffer, "lock\n");
	} else {
		snd_iprintf(buffer, "none\n");
	}

	snd_iprintf(buffer, "ADAT1:");
	if ((data >> 8) & 0x04) {
		if ((data >> 8) & 0x10)
			snd_iprintf(buffer, "sync\n");
		else
			snd_iprintf(buffer, "lock\n");
	} else {
		snd_iprintf(buffer, "none\n");
	}

	snd_iprintf(buffer, "ADAT2:");
	if ((data >> 8) & 0x08) {
		if ((data >> 8) & 0x20)
			snd_iprintf(buffer, "sync\n");
		else
			snd_iprintf(buffer, "lock\n");
	} else {
		snd_iprintf(buffer, "none\n");
	}

	snd_iprintf(buffer, "\nUsed external source:\n");

	if (((data >> 22) & 0x07) == 0x07) {
		snd_iprintf(buffer, "None\n");
	} else {
		switch ((data >> 22) & 0x07) {
		case 0x00:
			snd_iprintf(buffer, "ADAT1:");
			break;
		case 0x01:
			snd_iprintf(buffer, "ADAT2:");
			break;
		case 0x03:
			snd_iprintf(buffer, "S/PDIF:");
			break;
		case 0x04:
			snd_iprintf(buffer, "Word:");
			break;
		case 0x07:
			snd_iprintf(buffer, "Nothing:");
			break;
		case 0x02:
		case 0x05:
		case 0x06:
		default:
			snd_iprintf(buffer, "unknown:");
			break;
		}

		if ((data >> 25) & 0x07) {
			switch ((data >> 25) & 0x07) {
			case 0x01:
				snd_iprintf(buffer, "32000\n");
				break;
			case 0x02:
				snd_iprintf(buffer, "44100\n");
				break;
			case 0x03:
				snd_iprintf(buffer, "48000\n");
				break;
			case 0x04:
				snd_iprintf(buffer, "64000\n");
				break;
			case 0x05:
				snd_iprintf(buffer, "88200\n");
				break;
			case 0x06:
				snd_iprintf(buffer, "96000\n");
				break;
			case 0x07:
				snd_iprintf(buffer, "128000\n");
				break;
			case 0x08:
				snd_iprintf(buffer, "176400\n");
				break;
			case 0x09:
				snd_iprintf(buffer, "192000\n");
				break;
			case 0x00:
				snd_iprintf(buffer, "unknown\n");
				break;
			}
		}
	}

	snd_iprintf(buffer, "Multiplied:");
	snd_iprintf(buffer, "%d\n", (data & 0x3ff) * 250);
}

static void former_dump_status(struct snd_ff *ff,
			       struct snd_info_buffer *buffer)
{
	dump_clock_config(ff, buffer);
	dump_sync_status(ff, buffer);
}

#define FF800_STF		0x0000fc88f000
#define FF800_RX_PACKET_FORMAT	0x0000fc88f004
#define FF800_ALLOC_TX_STREAM	0x0000fc88f008
#define FF800_ISOC_COMM_START	0x0000fc88f00c
#define   FF800_TX_S800_FLAG	0x00000800
#define FF800_ISOC_COMM_STOP	0x0000fc88f010

#define FF800_TX_PACKET_ISOC_CH	0x0000801c0008

static int allocate_rx_resources(struct snd_ff *ff)
{
	u32 data;
	__le32 reg;
	int err;

	// Controllers should allocate isochronous resources for rx stream.
	err = fw_iso_resources_allocate(&ff->rx_resources,
				amdtp_stream_get_max_payload(&ff->rx_stream),
				fw_parent_device(ff->unit)->max_speed);
	if (err < 0)
		return err;

	// Set isochronous channel and the number of quadlets of rx packets.
	data = ff->rx_stream.data_block_quadlets << 3;
	data = (data << 8) | ff->rx_resources.channel;
	reg = cpu_to_le32(data);
	return snd_fw_transaction(ff->unit, TCODE_WRITE_QUADLET_REQUEST,
				FF800_RX_PACKET_FORMAT, &reg, sizeof(reg), 0);
}

static int allocate_tx_resources(struct snd_ff *ff)
{
	__le32 reg;
	unsigned int count;
	unsigned int tx_isoc_channel;
	int err;

	reg = cpu_to_le32(ff->tx_stream.data_block_quadlets);
	err = snd_fw_transaction(ff->unit, TCODE_WRITE_QUADLET_REQUEST,
				 FF800_ALLOC_TX_STREAM, &reg, sizeof(reg), 0);
	if (err < 0)
		return err;

	// Wait till the format of tx packet is available.
	count = 0;
	while (count++ < 10) {
		u32 data;
		err = snd_fw_transaction(ff->unit, TCODE_READ_QUADLET_REQUEST,
				FF800_TX_PACKET_ISOC_CH, &reg, sizeof(reg), 0);
		if (err < 0)
			return err;

		data = le32_to_cpu(reg);
		if (data != 0xffffffff) {
			tx_isoc_channel = data;
			break;
		}

		msleep(50);
	}
	if (count >= 10)
		return -ETIMEDOUT;

	// NOTE: this is a makeshift to start OHCI 1394 IR context in the
	// channel. On the other hand, 'struct fw_iso_resources.allocated' is
	// not true and it's not deallocated at stop.
	ff->tx_resources.channel = tx_isoc_channel;

	return 0;
}

static int ff800_begin_session(struct snd_ff *ff, unsigned int rate)
{
	__le32 reg;
	int err;

	reg = cpu_to_le32(rate);
	err = snd_fw_transaction(ff->unit, TCODE_WRITE_QUADLET_REQUEST,
				 FF800_STF, &reg, sizeof(reg), 0);
	if (err < 0)
		return err;

	// If starting isochronous communication immediately, change of STF has
	// no effect. In this case, the communication runs based on former STF.
	// Let's sleep for a bit.
	msleep(100);

	err = allocate_rx_resources(ff);
	if (err < 0)
		return err;

	err = allocate_tx_resources(ff);
	if (err < 0)
		return err;

	reg = cpu_to_le32(0x80000000);
	reg |= cpu_to_le32(ff->tx_stream.data_block_quadlets);
	if (fw_parent_device(ff->unit)->max_speed == SCODE_800)
		reg |= cpu_to_le32(FF800_TX_S800_FLAG);
	return snd_fw_transaction(ff->unit, TCODE_WRITE_QUADLET_REQUEST,
				 FF800_ISOC_COMM_START, &reg, sizeof(reg), 0);
}

static void ff800_finish_session(struct snd_ff *ff)
{
	__le32 reg;

	reg = cpu_to_le32(0x80000000);
	snd_fw_transaction(ff->unit, TCODE_WRITE_QUADLET_REQUEST,
			   FF800_ISOC_COMM_STOP, &reg, sizeof(reg), 0);
}

static void ff800_handle_midi_msg(struct snd_ff *ff, __le32 *buf, size_t length)
{
	int i;

	for (i = 0; i < length / 4; i++) {
		u8 byte = le32_to_cpu(buf[i]) & 0xff;
		struct snd_rawmidi_substream *substream;

		substream = READ_ONCE(ff->tx_midi_substreams[0]);
		if (substream)
			snd_rawmidi_receive(substream, &byte, 1);
	}
}

const struct snd_ff_protocol snd_ff_protocol_ff800 = {
	.handle_midi_msg	= ff800_handle_midi_msg,
	.switch_fetching_mode	= former_switch_fetching_mode,
	.begin_session		= ff800_begin_session,
	.finish_session		= ff800_finish_session,
	.dump_status		= former_dump_status,
};

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
	.switch_fetching_mode	= former_switch_fetching_mode,
	.begin_session		= ff400_begin_session,
	.finish_session		= ff400_finish_session,
	.dump_status		= former_dump_status,
};
