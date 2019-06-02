// SPDX-License-Identifier: GPL-2.0
// ff-protocol-latter - a part of driver for RME Fireface series
//
// Copyright (c) 2019 Takashi Sakamoto
//
// Licensed under the terms of the GNU General Public License, version 2.

#include <linux/delay.h>

#include "ff.h"

#define LATTER_STF		0xffff00000004
#define LATTER_ISOC_CHANNELS	0xffff00000008
#define LATTER_ISOC_START	0xffff0000000c
#define LATTER_FETCH_MODE	0xffff00000010
#define LATTER_SYNC_STATUS	0x0000801c0000

static int parse_clock_bits(u32 data, unsigned int *rate,
			    enum snd_ff_clock_src *src)
{
	static const struct {
		unsigned int rate;
		u32 flag;
	} *rate_entry, rate_entries[] = {
		{ 32000,	0x00000000, },
		{ 44100,	0x01000000, },
		{ 48000,	0x02000000, },
		{ 64000,	0x04000000, },
		{ 88200,	0x05000000, },
		{ 96000,	0x06000000, },
		{ 128000,	0x08000000, },
		{ 176400,	0x09000000, },
		{ 192000,	0x0a000000, },
	};
	static const struct {
		enum snd_ff_clock_src src;
		u32 flag;
	} *clk_entry, clk_entries[] = {
		{ SND_FF_CLOCK_SRC_SPDIF,	0x00000200, },
		{ SND_FF_CLOCK_SRC_ADAT1,	0x00000400, },
		{ SND_FF_CLOCK_SRC_WORD,	0x00000600, },
		{ SND_FF_CLOCK_SRC_INTERNAL,	0x00000e00, },
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(rate_entries); ++i) {
		rate_entry = rate_entries + i;
		if ((data & 0x0f000000) == rate_entry->flag) {
			*rate = rate_entry->rate;
			break;
		}
	}
	if (i == ARRAY_SIZE(rate_entries))
		return -EIO;

	for (i = 0; i < ARRAY_SIZE(clk_entries); ++i) {
		clk_entry = clk_entries + i;
		if ((data & 0x000e00) == clk_entry->flag) {
			*src = clk_entry->src;
			break;
		}
	}
	if (i == ARRAY_SIZE(clk_entries))
		return -EIO;

	return 0;
}

static int latter_get_clock(struct snd_ff *ff, unsigned int *rate,
			   enum snd_ff_clock_src *src)
{
	__le32 reg;
	u32 data;
	int err;

	err = snd_fw_transaction(ff->unit, TCODE_READ_QUADLET_REQUEST,
				 LATTER_SYNC_STATUS, &reg, sizeof(reg), 0);
	if (err < 0)
		return err;
	data = le32_to_cpu(reg);

	return parse_clock_bits(data, rate, src);
}

static int latter_switch_fetching_mode(struct snd_ff *ff, bool enable)
{
	u32 data;
	__le32 reg;

	if (enable)
		data = 0x00000000;
	else
		data = 0xffffffff;
	reg = cpu_to_le32(data);

	return snd_fw_transaction(ff->unit, TCODE_WRITE_QUADLET_REQUEST,
				  LATTER_FETCH_MODE, &reg, sizeof(reg), 0);
}

static int latter_allocate_resources(struct snd_ff *ff, unsigned int rate)
{
	enum snd_ff_stream_mode mode;
	unsigned int code;
	__le32 reg;
	unsigned int count;
	int i;
	int err;

	// Set the number of data blocks transferred in a second.
	if (rate % 32000 == 0)
		code = 0x00;
	else if (rate % 44100 == 0)
		code = 0x02;
	else if (rate % 48000 == 0)
		code = 0x04;
	else
		return -EINVAL;

	if (rate >= 64000 && rate < 128000)
		code |= 0x08;
	else if (rate >= 128000 && rate < 192000)
		code |= 0x10;

	reg = cpu_to_le32(code);
	err = snd_fw_transaction(ff->unit, TCODE_WRITE_QUADLET_REQUEST,
				 LATTER_STF, &reg, sizeof(reg), 0);
	if (err < 0)
		return err;

	// Confirm to shift transmission clock.
	count = 0;
	while (count++ < 10) {
		unsigned int curr_rate;
		enum snd_ff_clock_src src;

		err = latter_get_clock(ff, &curr_rate, &src);
		if (err < 0)
			return err;

		if (curr_rate == rate)
			break;
	}
	if (count == 10)
		return -ETIMEDOUT;

	for (i = 0; i < ARRAY_SIZE(amdtp_rate_table); ++i) {
		if (rate == amdtp_rate_table[i])
			break;
	}
	if (i == ARRAY_SIZE(amdtp_rate_table))
		return -EINVAL;

	err = snd_ff_stream_get_multiplier_mode(i, &mode);
	if (err < 0)
		return err;

	// Keep resources for in-stream.
	ff->tx_resources.channels_mask = 0x00000000000000ffuLL;
	err = fw_iso_resources_allocate(&ff->tx_resources,
			amdtp_stream_get_max_payload(&ff->tx_stream),
			fw_parent_device(ff->unit)->max_speed);
	if (err < 0)
		return err;

	// Keep resources for out-stream.
	ff->rx_resources.channels_mask = 0x00000000000000ffuLL;
	err = fw_iso_resources_allocate(&ff->rx_resources,
			amdtp_stream_get_max_payload(&ff->rx_stream),
			fw_parent_device(ff->unit)->max_speed);
	if (err < 0)
		fw_iso_resources_free(&ff->tx_resources);

	return err;
}

static int latter_begin_session(struct snd_ff *ff, unsigned int rate)
{
	unsigned int generation = ff->rx_resources.generation;
	unsigned int flag;
	u32 data;
	__le32 reg;
	int err;

	if (rate >= 32000 && rate <= 48000)
		flag = 0x92;
	else if (rate >= 64000 && rate <= 96000)
		flag = 0x8e;
	else if (rate >= 128000 && rate <= 192000)
		flag = 0x8c;
	else
		return -EINVAL;

	if (generation != fw_parent_device(ff->unit)->card->generation) {
		err = fw_iso_resources_update(&ff->tx_resources);
		if (err < 0)
			return err;

		err = fw_iso_resources_update(&ff->rx_resources);
		if (err < 0)
			return err;
	}

	data = (ff->tx_resources.channel << 8) | ff->rx_resources.channel;
	reg = cpu_to_le32(data);
	err = snd_fw_transaction(ff->unit, TCODE_WRITE_QUADLET_REQUEST,
				 LATTER_ISOC_CHANNELS, &reg, sizeof(reg), 0);
	if (err < 0)
		return err;

	// Always use the maximum number of data channels in data block of
	// packet.
	reg = cpu_to_le32(flag);
	return snd_fw_transaction(ff->unit, TCODE_WRITE_QUADLET_REQUEST,
				  LATTER_ISOC_START, &reg, sizeof(reg), 0);
}

static void latter_finish_session(struct snd_ff *ff)
{
	__le32 reg;

	reg = cpu_to_le32(0x00000000);
	snd_fw_transaction(ff->unit, TCODE_WRITE_QUADLET_REQUEST,
			   LATTER_ISOC_START, &reg, sizeof(reg), 0);
}

static void latter_dump_status(struct snd_ff *ff, struct snd_info_buffer *buffer)
{
	static const struct {
		char *const label;
		u32 locked_mask;
		u32 synced_mask;
	} *clk_entry, clk_entries[] = {
		{ "S/PDIF",	0x00000001, 0x00000010, },
		{ "ADAT",	0x00000002, 0x00000020, },
		{ "WDClk",	0x00000004, 0x00000040, },
	};
	__le32 reg;
	u32 data;
	unsigned int rate;
	enum snd_ff_clock_src src;
	const char *label;
	int i;
	int err;

	err = snd_fw_transaction(ff->unit, TCODE_READ_QUADLET_REQUEST,
				 LATTER_SYNC_STATUS, &reg, sizeof(reg), 0);
	if (err < 0)
		return;
	data = le32_to_cpu(reg);

	snd_iprintf(buffer, "External source detection:\n");

	for (i = 0; i < ARRAY_SIZE(clk_entries); ++i) {
		clk_entry = clk_entries + i;
		snd_iprintf(buffer, "%s: ", clk_entry->label);
		if (data & clk_entry->locked_mask) {
			if (data & clk_entry->synced_mask)
				snd_iprintf(buffer, "sync\n");
			else
				snd_iprintf(buffer, "lock\n");
		} else {
			snd_iprintf(buffer, "none\n");
		}
	}

	err = parse_clock_bits(data, &rate, &src);
	if (err < 0)
		return;
	label = snd_ff_proc_get_clk_label(src);
	if (!label)
		return;

	snd_iprintf(buffer, "Referred clock: %s %d\n", label, rate);
}

// NOTE: transactions are transferred within 0x00-0x7f in allocated range of
// address. This seems to be for check of discontinuity in receiver side.
//
// Like Fireface 400, drivers can select one of 4 options for lower 4 bytes of
// destination address by bit flags in quadlet register (little endian) at
// 0x'ffff'0000'0014:
//
// bit flags: offset of destination address
// - 0x00002000: 0x'....'....'0000'0000
// - 0x00004000: 0x'....'....'0000'0080
// - 0x00008000: 0x'....'....'0000'0100
// - 0x00010000: 0x'....'....'0000'0180
//
// Drivers can suppress the device to transfer asynchronous transactions by
// clear these bit flags.
//
// Actually, the register is write-only and includes the other settings such as
// input attenuation. This driver allocates for the first option
// (0x'....'....'0000'0000) and expects userspace application to configure the
// register for it.
static void latter_handle_midi_msg(struct snd_ff *ff, unsigned int offset,
				   __le32 *buf, size_t length)
{
	u32 data = le32_to_cpu(*buf);
	unsigned int index = (data & 0x000000f0) >> 4;
	u8 byte[3];
	struct snd_rawmidi_substream *substream;
	unsigned int len;

	if (index >= ff->spec->midi_in_ports)
		return;

	switch (data & 0x0000000f) {
	case 0x00000008:
	case 0x00000009:
	case 0x0000000a:
	case 0x0000000b:
	case 0x0000000e:
		len = 3;
		break;
	case 0x0000000c:
	case 0x0000000d:
		len = 2;
		break;
	default:
		len = data & 0x00000003;
		if (len == 0)
			len = 3;
		break;
	}

	byte[0] = (data & 0x0000ff00) >> 8;
	byte[1] = (data & 0x00ff0000) >> 16;
	byte[2] = (data & 0xff000000) >> 24;

	substream = READ_ONCE(ff->tx_midi_substreams[index]);
	if (substream)
		snd_rawmidi_receive(substream, byte, len);
}

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

static int latter_fill_midi_msg(struct snd_ff *ff,
				struct snd_rawmidi_substream *substream,
				unsigned int port)
{
	u32 data = {0};
	u8 *buf = (u8 *)&data;
	int consumed;

	buf[0] = port << 4;
	consumed = snd_rawmidi_transmit_peek(substream, buf + 1, 3);
	if (consumed <= 0)
		return consumed;

	if (!ff->on_sysex[port]) {
		if (buf[1] != 0xf0) {
			if (consumed < calculate_message_bytes(buf[1]))
				return 0;
		} else {
			// The beginning of exclusives.
			ff->on_sysex[port] = true;
		}

		buf[0] |= consumed;
	} else {
		if (buf[1] != 0xf7) {
			if (buf[2] == 0xf7 || buf[3] == 0xf7) {
				// Transfer end code at next time.
				consumed -= 1;
			}

			buf[0] |= consumed;
		} else {
			// The end of exclusives.
			ff->on_sysex[port] = false;
			consumed = 1;
			buf[0] |= 0x0f;
		}
	}

	ff->msg_buf[port][0] = cpu_to_le32(data);
	ff->rx_bytes[port] = consumed;

	return 1;
}

const struct snd_ff_protocol snd_ff_protocol_latter = {
	.handle_midi_msg	= latter_handle_midi_msg,
	.fill_midi_msg		= latter_fill_midi_msg,
	.get_clock		= latter_get_clock,
	.switch_fetching_mode	= latter_switch_fetching_mode,
	.allocate_resources	= latter_allocate_resources,
	.begin_session		= latter_begin_session,
	.finish_session		= latter_finish_session,
	.dump_status		= latter_dump_status,
};
