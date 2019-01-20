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

static int latter_begin_session(struct snd_ff *ff, unsigned int rate)
{
	static const struct {
		unsigned int stf;
		unsigned int code;
		unsigned int flag;
	} *entry, rate_table[] = {
		{ 32000,  0x00, 0x92, },
		{ 44100,  0x02, 0x92, },
		{ 48000,  0x04, 0x92, },
		{ 64000,  0x08, 0x8e, },
		{ 88200,  0x0a, 0x8e, },
		{ 96000,  0x0c, 0x8e, },
		{ 128000, 0x10, 0x8c, },
		{ 176400, 0x12, 0x8c, },
		{ 192000, 0x14, 0x8c, },
	};
	u32 data;
	__le32 reg;
	unsigned int count;
	int i;
	int err;

	for (i = 0; i < ARRAY_SIZE(rate_table); ++i) {
		entry = rate_table + i;
		if (entry->stf == rate)
			break;
	}
	if (i == ARRAY_SIZE(rate_table))
		return -EINVAL;

	reg = cpu_to_le32(entry->code);
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

	err = keep_resources(ff, rate);
	if (err < 0)
		return err;

	data = (ff->tx_resources.channel << 8) | ff->rx_resources.channel;
	reg = cpu_to_le32(data);
	err = snd_fw_transaction(ff->unit, TCODE_WRITE_QUADLET_REQUEST,
				 LATTER_ISOC_CHANNELS, &reg, sizeof(reg), 0);
	if (err < 0)
		return err;

	// Always use the maximum number of data channels in data block of
	// packet.
	reg = cpu_to_le32(entry->flag);
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

const struct snd_ff_protocol snd_ff_protocol_latter = {
	.get_clock		= latter_get_clock,
	.switch_fetching_mode	= latter_switch_fetching_mode,
	.begin_session		= latter_begin_session,
	.finish_session		= latter_finish_session,
	.dump_status		= latter_dump_status,
};
