// SPDX-License-Identifier: GPL-2.0-only
// motu-protocol-v1.c - a part of driver for MOTU FireWire series
//
// Copyright (c) 2021 Takashi Sakamoto <o-takashi@sakamocchi.jp>

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
// 0x00003f00: monitor input mode.
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
// 0x00000080: enable stream input.
// 0x00000040: disable monitor input.
// 0x00000008: enable main out.
// 0x00000004: rate of sampling clock.
//   0x00000004: 48.0 kHz
//   0x00000000: 44.1 kHz
// 0x00000023: source of sampling clock.
//   0x00000003: source packet header (SPH)
//   0x00000002: S/PDIF on optical/coaxial interface.
//   0x00000021: ADAT on optical interface
//   0x00000001: ADAT on Dsub 9pin
//   0x00000000: internal

#define CLK_828_STATUS_OFFSET				0x0b00
#define  CLK_828_STATUS_MASK				0x0000ffff
#define  CLK_828_STATUS_FLAG_OPT_IN_IFACE_IS_SPDIF	0x00008000
#define  CLK_828_STATUS_FLAG_OPT_OUT_IFACE_IS_SPDIF	0x00004000
#define  CLK_828_STATUS_FLAG_FETCH_PCM_FRAMES		0x00000080
#define  CLK_828_STATUS_FLAG_ENABLE_OUTPUT		0x00000008
#define  CLK_828_STATUS_FLAG_RATE_48000			0x00000004
#define  CLK_828_STATUS_MASK_SRC			0x00000023
#define   CLK_828_STATUS_FLAG_SRC_ADAT_ON_OPT		0x00000021
#define   CLK_828_STATUS_FLAG_SRC_SPH			0x00000003
#define   CLK_828_STATUS_FLAG_SRC_SPDIF			0x00000002
#define   CLK_828_STATUS_FLAG_SRC_ADAT_ON_DSUB		0x00000001
#define   CLK_828_STATUS_FLAG_SRC_INTERNAL		0x00000000

// Status register for MOTU 896 (0x'ffff'f000'0b14).
//
// 0xf0000000: enable physical and stream input to DAC.
//   0x80000000: disable
//   0x40000000: disable
//   0x20000000: enable (prior to the other bits)
//   0x10000000: disable
//   0x00000000: disable
// 0x08000000: speed of word clock signal output on BNC interface.
//   0x00000000: force to low rate (44.1/48.0 kHz).
//   0x08000000: follow to system clock.
// 0x04000000: something relevant to clock.
// 0x03000000: enable output.
//  0x02000000: enabled irreversibly once standing unless the device voluntarily disables it.
//  0x01000000: enabled irreversibly once standing unless the device voluntarily disables it.
// 0x00ffff00: monitor input mode.
//   0x00000000: disabled
//   0x00004800: analog-1/2
//   0x00005a00: analog-3/4
//   0x00006c00: analog-5/6
//   0x00007e00: analog-7/8
//   0x00104800: AES/EBU-1/2
//   0x00004000: analog-1
//   0x00004900: analog-2
//   0x00005200: analog-3
//   0x00005b00: analog-4
//   0x00006400: analog-5
//   0x00006d00: analog-6
//   0x00007600: analog-7
//   0x00007f00: analog-8
//   0x00104000: AES/EBU-1
//   0x00104900: AES/EBU-2
// 0x00000060: sample rate conversion for AES/EBU input/output.
//   0x00000000: None
//   0x00000020: input signal is converted to system rate
//   0x00000040: output is slave to input, ignoring system rate
//   0x00000060: output is double rate than system rate
// 0x00000018: nominal rate of sampling clock.
//   0x00000000: 44.1 kHz
//   0x00000008: 48.0 kHz
//   0x00000010: 88.2 kHz
//   0x00000018: 96.0 kHz
// 0x00000007: source of sampling clock.
//   0x00000000: internal
//   0x00000001: ADAT on optical interface
//   0x00000002: AES/EBU on XLR
//   0x00000003: source packet header (SPH)
//   0x00000004: word clock on BNC
//   0x00000005: ADAT on Dsub 9pin

#define CLK_896_STATUS_OFFSET			0x0b14
#define  CLK_896_STATUS_FLAG_FETCH_ENABLE	0x20000000
#define  CLK_896_STATUS_FLAG_OUTPUT_ON		0x03000000
#define  CLK_896_STATUS_MASK_SRC		0x00000007
#define   CLK_896_STATUS_FLAG_SRC_INTERNAL	0x00000000
#define   CLK_896_STATUS_FLAG_SRC_ADAT_ON_OPT	0x00000001
#define   CLK_896_STATUS_FLAG_SRC_AESEBU	0x00000002
#define   CLK_896_STATUS_FLAG_SRC_SPH		0x00000003
#define   CLK_896_STATUS_FLAG_SRC_WORD		0x00000004
#define   CLK_896_STATUS_FLAG_SRC_ADAT_ON_DSUB	0x00000005
#define  CLK_896_STATUS_MASK_RATE		0x00000018
#define   CLK_896_STATUS_FLAG_RATE_44100	0x00000000
#define   CLK_896_STATUS_FLAG_RATE_48000	0x00000008
#define   CLK_896_STATUS_FLAG_RATE_88200	0x00000010
#define   CLK_896_STATUS_FLAG_RATE_96000	0x00000018

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

static int parse_clock_rate_896(u32 data, unsigned int *rate)
{
	switch (data & CLK_896_STATUS_MASK_RATE) {
	case CLK_896_STATUS_FLAG_RATE_44100:
		*rate = 44100;
		break;
	case CLK_896_STATUS_FLAG_RATE_48000:
		*rate = 48000;
		break;
	case CLK_896_STATUS_FLAG_RATE_88200:
		*rate = 88200;
		break;
	case CLK_896_STATUS_FLAG_RATE_96000:
		*rate = 96000;
		break;
	default:
		return -ENXIO;
	}

	return 0;
}

static int get_clock_rate_896(struct snd_motu *motu, unsigned int *rate)
{
	__be32 reg;
	int err;

	err = snd_motu_transaction_read(motu, CLK_896_STATUS_OFFSET, &reg, sizeof(reg));
	if (err < 0)
		return err;
	return parse_clock_rate_896(be32_to_cpu(reg), rate);
}

int snd_motu_protocol_v1_get_clock_rate(struct snd_motu *motu, unsigned int *rate)
{
	if (motu->spec == &snd_motu_spec_828)
		return get_clock_rate_828(motu, rate);
	else if (motu->spec == &snd_motu_spec_896)
		return get_clock_rate_896(motu, rate);
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

static int set_clock_rate_896(struct snd_motu *motu, unsigned int rate)
{
	unsigned int flag;
	__be32 reg;
	u32 data;
	int err;

	err = snd_motu_transaction_read(motu, CLK_896_STATUS_OFFSET, &reg, sizeof(reg));
	if (err < 0)
		return err;
	data = be32_to_cpu(reg);

	switch (rate) {
	case 44100:
		flag = CLK_896_STATUS_FLAG_RATE_44100;
		break;
	case 48000:
		flag = CLK_896_STATUS_FLAG_RATE_48000;
		break;
	case 88200:
		flag = CLK_896_STATUS_FLAG_RATE_88200;
		break;
	case 96000:
		flag = CLK_896_STATUS_FLAG_RATE_96000;
		break;
	default:
		return -EINVAL;
	}

	data &= ~CLK_896_STATUS_MASK_RATE;
	data |= flag;

	reg = cpu_to_be32(data);
	return snd_motu_transaction_write(motu, CLK_896_STATUS_OFFSET, &reg, sizeof(reg));
}

int snd_motu_protocol_v1_set_clock_rate(struct snd_motu *motu, unsigned int rate)
{
	if (motu->spec == &snd_motu_spec_828)
		return set_clock_rate_828(motu, rate);
	else if (motu->spec == &snd_motu_spec_896)
		return set_clock_rate_896(motu, rate);
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

	switch (data & CLK_828_STATUS_MASK_SRC) {
	case CLK_828_STATUS_FLAG_SRC_ADAT_ON_OPT:
		*src = SND_MOTU_CLOCK_SOURCE_ADAT_ON_OPT;
		break;
	case CLK_828_STATUS_FLAG_SRC_SPH:
		*src = SND_MOTU_CLOCK_SOURCE_SPH;
		break;
	case CLK_828_STATUS_FLAG_SRC_SPDIF:
	{
		if (data & CLK_828_STATUS_FLAG_OPT_IN_IFACE_IS_SPDIF)
			*src = SND_MOTU_CLOCK_SOURCE_SPDIF_ON_COAX;
		else
			*src = SND_MOTU_CLOCK_SOURCE_SPDIF_ON_OPT;
		break;
	}
	case CLK_828_STATUS_FLAG_SRC_ADAT_ON_DSUB:
		*src = SND_MOTU_CLOCK_SOURCE_ADAT_ON_DSUB;
		break;
	case CLK_828_STATUS_FLAG_SRC_INTERNAL:
		*src = SND_MOTU_CLOCK_SOURCE_INTERNAL;
		break;
	default:
		return -ENXIO;
	}

	return 0;
}

static int get_clock_source_896(struct snd_motu *motu, enum snd_motu_clock_source *src)
{
	__be32 reg;
	u32 data;
	int err;

	err = snd_motu_transaction_read(motu, CLK_896_STATUS_OFFSET, &reg, sizeof(reg));
	if (err < 0)
		return err;
	data = be32_to_cpu(reg);

	switch (data & CLK_896_STATUS_MASK_SRC) {
	case CLK_896_STATUS_FLAG_SRC_INTERNAL:
		*src = SND_MOTU_CLOCK_SOURCE_INTERNAL;
		break;
	case CLK_896_STATUS_FLAG_SRC_ADAT_ON_OPT:
		*src = SND_MOTU_CLOCK_SOURCE_ADAT_ON_OPT;
		break;
	case CLK_896_STATUS_FLAG_SRC_AESEBU:
		*src = SND_MOTU_CLOCK_SOURCE_AESEBU_ON_XLR;
		break;
	case CLK_896_STATUS_FLAG_SRC_SPH:
		*src = SND_MOTU_CLOCK_SOURCE_SPH;
		break;
	case CLK_896_STATUS_FLAG_SRC_WORD:
		*src = SND_MOTU_CLOCK_SOURCE_WORD_ON_BNC;
		break;
	case CLK_896_STATUS_FLAG_SRC_ADAT_ON_DSUB:
		*src = SND_MOTU_CLOCK_SOURCE_ADAT_ON_DSUB;
		break;
	default:
		return -ENXIO;
	}

	return 0;
}

int snd_motu_protocol_v1_get_clock_source(struct snd_motu *motu, enum snd_motu_clock_source *src)
{
	if (motu->spec == &snd_motu_spec_828)
		return get_clock_source_828(motu, src);
	else if (motu->spec == &snd_motu_spec_896)
		return get_clock_source_896(motu, src);
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

	data &= ~(CLK_828_STATUS_FLAG_FETCH_PCM_FRAMES | CLK_828_STATUS_FLAG_ENABLE_OUTPUT);
	if (enable) {
		// This transaction should be initiated after the device receives batch of packets
		// since the device voluntarily mutes outputs. As a workaround, yield processor over
		// 100 msec.
		msleep(100);
		data |= CLK_828_STATUS_FLAG_FETCH_PCM_FRAMES | CLK_828_STATUS_FLAG_ENABLE_OUTPUT;
	}

	reg = cpu_to_be32(data);
	return snd_motu_transaction_write(motu, CLK_828_STATUS_OFFSET, &reg, sizeof(reg));
}

static int switch_fetching_mode_896(struct snd_motu *motu, bool enable)
{
	__be32 reg;
	u32 data;
	int err;

	err = snd_motu_transaction_read(motu, CLK_896_STATUS_OFFSET, &reg, sizeof(reg));
	if (err < 0)
		return err;
	data = be32_to_cpu(reg);

	data &= ~CLK_896_STATUS_FLAG_FETCH_ENABLE;
	if (enable)
		data |= CLK_896_STATUS_FLAG_FETCH_ENABLE | CLK_896_STATUS_FLAG_OUTPUT_ON;

	reg = cpu_to_be32(data);
	return snd_motu_transaction_write(motu, CLK_896_STATUS_OFFSET, &reg, sizeof(reg));
}

int snd_motu_protocol_v1_switch_fetching_mode(struct snd_motu *motu, bool enable)
{
	if (motu->spec == &snd_motu_spec_828)
		return switch_fetching_mode_828(motu, enable);
	else if (motu->spec == &snd_motu_spec_896)
		return switch_fetching_mode_896(motu, enable);
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

static int detect_packet_formats_896(struct snd_motu *motu)
{
	// 24bit PCM frames follow to source packet header without message chunk.
	motu->tx_packet_formats.pcm_byte_offset = 4;
	motu->rx_packet_formats.pcm_byte_offset = 4;

	// No message chunk in data block.
	motu->tx_packet_formats.msg_chunks = 0;
	motu->rx_packet_formats.msg_chunks = 0;

	// Always enable optical interface for ADAT signal since the device have no registers
	// to refer to current configuration.
	motu->tx_packet_formats.pcm_chunks[0] += 8;
	motu->tx_packet_formats.pcm_chunks[1] += 8;

	motu->rx_packet_formats.pcm_chunks[0] += 8;
	motu->rx_packet_formats.pcm_chunks[1] += 8;

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
	else if (motu->spec == &snd_motu_spec_896)
		return detect_packet_formats_896(motu);
	else
		return 0;
}

const struct snd_motu_spec snd_motu_spec_828 = {
	.name = "828",
	.protocol_version = SND_MOTU_PROTOCOL_V1,
	.tx_fixed_pcm_chunks = {10, 0, 0},
	.rx_fixed_pcm_chunks = {10, 0, 0},
};

const struct snd_motu_spec snd_motu_spec_896 = {
	.name = "896",
	.tx_fixed_pcm_chunks = {10, 10, 0},
	.rx_fixed_pcm_chunks = {10, 10, 0},
};
