// SPDX-License-Identifier: GPL-2.0-only

// motu-protocol-v1.c - a part of driver for MOTU FireWire series
//
// Copyright (c) 2021 Takashi Sakamoto <o-takashi@sakamocchi.jp>
//
// Licensed under the terms of the GNU General Public License, version 2.

#include "motu.h"

#include <linux/delay.h>

// Status register for MOTU 828 (0x'ffff'f000'0b00).
//
// 0xffff0000: ISOC_COMM_CONTROL_MASK in motu-stream.c.
// 0x00008000: mode of optical input interface.
//   0x00008000: for S/PDIF signal.
//   0x00000000: disabled or for ADAT signal.
// 0x00004000: mode of optical output interface.
//   0x00004000: for S/PDIF signal.
//   0x00000000: disabled or for ADAT signal.
// 0x00003f40: monitor input mode.
//   0x00000800: analog-1/2
//   0x00001a00: analog-3/4
//   0x00002c00: analog-5/6
//   0x00003e00: analog-7/8
//   0x00000000: analog-1
//   0x00000900: analog-2
//   0x00001200: analog-3
//   0x00001b00: analog-4
//   0x00002400: analog-5
//   0x00002d00: analog-6
//   0x00003600: analog-7
//   0x00003f00: analog-8
//   0x00000040: disabled
// 0x00000004: rate of sampling clock.
//   0x00000004: 48.0 kHz
//   0x00000000: 44.1 kHz
// 0x00000023: source of sampling clock.
//   0x00000002: S/PDIF on optical/coaxial interface.
//   0x00000021: ADAT on optical interface
//   0x00000001: ADAT on Dsub 9pin
//   0x00000000: internal or SMPTE

#define CLK_828_STATUS_OFFSET				0x0b00
#define  CLK_828_STATUS_MASK				0x0000ffff
#define  CLK_828_STATUS_FLAG_OPT_IN_IFACE_IS_SPDIF	0x00008000
#define  CLK_828_STATUS_FLAG_OPT_OUT_IFACE_IS_SPDIF	0x00004000
#define  CLK_828_STATUS_FLAG_FETCH_PCM_FRAMES		0x00000080
#define  CLK_828_STATUS_FLAG_SRC_IS_NOT_FROM_ADAT_DSUB	0x00000020
#define  CLK_828_STATUS_FLAG_OUTPUT_MUTE		0x00000008
#define  CLK_828_STATUS_FLAG_RATE_48000			0x00000004
#define  CLK_828_STATUS_FLAG_SRC_SPDIF_ON_OPT_OR_COAX	0x00000002
#define  CLK_828_STATUS_FLAG_SRC_ADAT_ON_OPT_OR_DSUB	0x00000001

static void parse_clock_rate_828(u32 data, unsigned int *rate)
{
	if (data & CLK_828_STATUS_FLAG_RATE_48000)
		*rate = 48000;
	else
		*rate = 44100;
}

static int get_clock_rate_828(struct snd_motu *motu, unsigned int *rate)
{
	__be32 reg;
	int err;

	err = snd_motu_transaction_read(motu, CLK_828_STATUS_OFFSET, &reg, sizeof(reg));
	if (err < 0)
		return err;
	parse_clock_rate_828(be32_to_cpu(reg), rate);

	return 0;
}

int snd_motu_protocol_v1_get_clock_rate(struct snd_motu *motu, unsigned int *rate)
{
	if (motu->spec == &snd_motu_spec_828)
		return get_clock_rate_828(motu, rate);
	else
		return -ENXIO;
}

static int set_clock_rate_828(struct snd_motu *motu, unsigned int rate)
{
	__be32 reg;
	u32 data;
	int err;

	err = snd_motu_transaction_read(motu, CLK_828_STATUS_OFFSET, &reg, sizeof(reg));
	if (err < 0)
		return err;
	data = be32_to_cpu(reg) & CLK_828_STATUS_MASK;

	data &= ~CLK_828_STATUS_FLAG_RATE_48000;
	if (rate == 48000)
		data |= CLK_828_STATUS_FLAG_RATE_48000;

	reg = cpu_to_be32(data);
	return snd_motu_transaction_write(motu, CLK_828_STATUS_OFFSET, &reg, sizeof(reg));
}

int snd_motu_protocol_v1_set_clock_rate(struct snd_motu *motu, unsigned int rate)
{
	if (motu->spec == &snd_motu_spec_828)
		return set_clock_rate_828(motu, rate);
	else
		return -ENXIO;
}

static int get_clock_source_828(struct snd_motu *motu, enum snd_motu_clock_source *src)
{
	__be32 reg;
	u32 data;
	int err;

	err = snd_motu_transaction_read(motu, CLK_828_STATUS_OFFSET, &reg, sizeof(reg));
	if (err < 0)
		return err;
	data = be32_to_cpu(reg) & CLK_828_STATUS_MASK;

	if (data & CLK_828_STATUS_FLAG_SRC_ADAT_ON_OPT_OR_DSUB) {
		if (data & CLK_828_STATUS_FLAG_SRC_IS_NOT_FROM_ADAT_DSUB)
			*src = SND_MOTU_CLOCK_SOURCE_ADAT_ON_OPT;
		else
			*src = SND_MOTU_CLOCK_SOURCE_ADAT_ON_DSUB;
	} else if (data & CLK_828_STATUS_FLAG_SRC_SPDIF_ON_OPT_OR_COAX) {
		if (data & CLK_828_STATUS_FLAG_OPT_IN_IFACE_IS_SPDIF)
			*src = SND_MOTU_CLOCK_SOURCE_SPDIF_ON_OPT;
		else
			*src = SND_MOTU_CLOCK_SOURCE_SPDIF_ON_COAX;
	} else {
		*src = SND_MOTU_CLOCK_SOURCE_INTERNAL;
	}

	return 0;
}

int snd_motu_protocol_v1_get_clock_source(struct snd_motu *motu, enum snd_motu_clock_source *src)
{
	if (motu->spec == &snd_motu_spec_828)
		return get_clock_source_828(motu, src);
	else
		return -ENXIO;
}

static int switch_fetching_mode_828(struct snd_motu *motu, bool enable)
{
	__be32 reg;
	u32 data;
	int err;

	err = snd_motu_transaction_read(motu, CLK_828_STATUS_OFFSET, &reg, sizeof(reg));
	if (err < 0)
		return err;
	data = be32_to_cpu(reg) & CLK_828_STATUS_MASK;

	data &= ~(CLK_828_STATUS_FLAG_FETCH_PCM_FRAMES | CLK_828_STATUS_FLAG_OUTPUT_MUTE);
	if (enable) {
		// This transaction should be initiated after the device receives batch of packets
		// since the device voluntarily mutes outputs. As a workaround, yield processor over
		// 100 msec.
		msleep(100);
		data |= CLK_828_STATUS_FLAG_FETCH_PCM_FRAMES | CLK_828_STATUS_FLAG_OUTPUT_MUTE;
	}

	reg = cpu_to_be32(data);
	return snd_motu_transaction_write(motu, CLK_828_STATUS_OFFSET, &reg, sizeof(reg));
}

int snd_motu_protocol_v1_switch_fetching_mode(struct snd_motu *motu, bool enable)
{
	if (motu->spec == &snd_motu_spec_828)
		return switch_fetching_mode_828(motu, enable);
	else
		return -ENXIO;
}

static int detect_packet_formats_828(struct snd_motu *motu)
{
	__be32 reg;
	u32 data;
	int err;

	motu->tx_packet_formats.pcm_byte_offset = 4;
	motu->tx_packet_formats.msg_chunks = 2;

	motu->rx_packet_formats.pcm_byte_offset = 4;
	motu->rx_packet_formats.msg_chunks = 0;

	err = snd_motu_transaction_read(motu, CLK_828_STATUS_OFFSET, &reg, sizeof(reg));
	if (err < 0)
		return err;
	data = be32_to_cpu(reg) & CLK_828_STATUS_MASK;

	// The number of chunks is just reduced when SPDIF is activated.
	if (!(data & CLK_828_STATUS_FLAG_OPT_IN_IFACE_IS_SPDIF))
		motu->tx_packet_formats.pcm_chunks[0] += 8;

	if (!(data & CLK_828_STATUS_FLAG_OPT_OUT_IFACE_IS_SPDIF))
		motu->rx_packet_formats.pcm_chunks[0] += 8;

	return 0;
}

int snd_motu_protocol_v1_cache_packet_formats(struct snd_motu *motu)
{
	memcpy(motu->tx_packet_formats.pcm_chunks, motu->spec->tx_fixed_pcm_chunks,
	       sizeof(motu->tx_packet_formats.pcm_chunks));
	memcpy(motu->rx_packet_formats.pcm_chunks, motu->spec->rx_fixed_pcm_chunks,
	       sizeof(motu->rx_packet_formats.pcm_chunks));

	if (motu->spec == &snd_motu_spec_828)
		return detect_packet_formats_828(motu);
	else
		return 0;
}

const struct snd_motu_spec snd_motu_spec_828 = {
	.name = "828",
	.protocol_version = SND_MOTU_PROTOCOL_V1,
	.tx_fixed_pcm_chunks = {10, 0, 0},
	.rx_fixed_pcm_chunks = {10, 0, 0},
};
