// SPDX-License-Identifier: GPL-2.0-only
/*
 * motu-protocol-v3.c - a part of driver for MOTU FireWire series
 *
 * Copyright (c) 2015-2017 Takashi Sakamoto <o-takashi@sakamocchi.jp>
 */

#include <linux/delay.h>
#include "motu.h"

#define V3_CLOCK_STATUS_OFFSET		0x0b14
#define  V3_FETCH_PCM_FRAMES		0x02000000
#define  V3_CLOCK_RATE_MASK		0x0000ff00
#define  V3_CLOCK_RATE_SHIFT		8
#define  V3_CLOCK_SOURCE_MASK		0x000000ff
#define   V3_CLOCK_SRC_INTERNAL		0x00
#define   V3_CLOCK_SRC_WORD_ON_BNC	0x01
#define   V3_CLOCK_SRC_SPH		0x02
#define   V3_CLOCK_SRC_SPDIF_ON_COAX	0x10
#define   V3_CLOCK_SRC_OPT_IFACE_A	0x18
#define   V3_CLOCK_SRC_OPT_IFACE_B	0x19

#define V3_OPT_IFACE_MODE_OFFSET	0x0c94
#define  V3_ENABLE_OPT_IN_IFACE_A	0x00000001
#define  V3_ENABLE_OPT_IN_IFACE_B	0x00000002
#define  V3_ENABLE_OPT_OUT_IFACE_A	0x00000100
#define  V3_ENABLE_OPT_OUT_IFACE_B	0x00000200
#define  V3_NO_ADAT_OPT_IN_IFACE_A	0x00010000
#define  V3_NO_ADAT_OPT_IN_IFACE_B	0x00100000
#define  V3_NO_ADAT_OPT_OUT_IFACE_A	0x00040000
#define  V3_NO_ADAT_OPT_OUT_IFACE_B	0x00400000

#define V3_MSG_FLAG_CLK_CHANGED		0x00000002
#define V3_CLK_WAIT_MSEC		4000

int snd_motu_protocol_v3_get_clock_rate(struct snd_motu *motu,
					unsigned int *rate)
{
	__be32 reg;
	u32 data;
	int err;

	err = snd_motu_transaction_read(motu, V3_CLOCK_STATUS_OFFSET, &reg,
					sizeof(reg));
	if (err < 0)
		return err;
	data = be32_to_cpu(reg);

	data = (data & V3_CLOCK_RATE_MASK) >> V3_CLOCK_RATE_SHIFT;
	if (data >= ARRAY_SIZE(snd_motu_clock_rates))
		return -EIO;

	*rate = snd_motu_clock_rates[data];

	return 0;
}

int snd_motu_protocol_v3_set_clock_rate(struct snd_motu *motu,
					unsigned int rate)
{
	__be32 reg;
	u32 data;
	bool need_to_wait;
	int i, err;

	for (i = 0; i < ARRAY_SIZE(snd_motu_clock_rates); ++i) {
		if (snd_motu_clock_rates[i] == rate)
			break;
	}
	if (i == ARRAY_SIZE(snd_motu_clock_rates))
		return -EINVAL;

	err = snd_motu_transaction_read(motu, V3_CLOCK_STATUS_OFFSET, &reg,
					sizeof(reg));
	if (err < 0)
		return err;
	data = be32_to_cpu(reg);

	data &= ~(V3_CLOCK_RATE_MASK | V3_FETCH_PCM_FRAMES);
	data |= i << V3_CLOCK_RATE_SHIFT;

	need_to_wait = data != be32_to_cpu(reg);

	reg = cpu_to_be32(data);
	err = snd_motu_transaction_write(motu, V3_CLOCK_STATUS_OFFSET, &reg,
					 sizeof(reg));
	if (err < 0)
		return err;

	if (need_to_wait) {
		int result;

		motu->msg = 0;
		result = wait_event_interruptible_timeout(motu->hwdep_wait,
					motu->msg & V3_MSG_FLAG_CLK_CHANGED,
					msecs_to_jiffies(V3_CLK_WAIT_MSEC));
		if (result < 0)
			return result;
		if (result == 0)
			return -ETIMEDOUT;
	}

	return 0;
}

int snd_motu_protocol_v3_get_clock_source(struct snd_motu *motu,
					  enum snd_motu_clock_source *src)
{
	__be32 reg;
	u32 data;
	int err;

	err = snd_motu_transaction_read(motu, V3_CLOCK_STATUS_OFFSET, &reg,
					sizeof(reg));
	if (err < 0)
		return err;
	data = be32_to_cpu(reg) & V3_CLOCK_SOURCE_MASK;

	switch (data) {
	case V3_CLOCK_SRC_INTERNAL:
		*src = SND_MOTU_CLOCK_SOURCE_INTERNAL;
		break;
	case V3_CLOCK_SRC_WORD_ON_BNC:
		*src = SND_MOTU_CLOCK_SOURCE_WORD_ON_BNC;
		break;
	case V3_CLOCK_SRC_SPH:
		*src = SND_MOTU_CLOCK_SOURCE_SPH;
		break;
	case V3_CLOCK_SRC_SPDIF_ON_COAX:
		*src = SND_MOTU_CLOCK_SOURCE_SPDIF_ON_COAX;
		break;
	case V3_CLOCK_SRC_OPT_IFACE_A:
	case V3_CLOCK_SRC_OPT_IFACE_B:
	{
		__be32 reg;
		u32 options;

		err = snd_motu_transaction_read(motu,
				V3_OPT_IFACE_MODE_OFFSET, &reg, sizeof(reg));
		if (err < 0)
			return err;
		options = be32_to_cpu(reg);

		if (data == V3_CLOCK_SRC_OPT_IFACE_A) {
			if (options & V3_NO_ADAT_OPT_IN_IFACE_A)
				*src = SND_MOTU_CLOCK_SOURCE_SPDIF_ON_OPT_A;
			else
				*src = SND_MOTU_CLOCK_SOURCE_ADAT_ON_OPT_A;
		} else {
			if (options & V3_NO_ADAT_OPT_IN_IFACE_B)
				*src = SND_MOTU_CLOCK_SOURCE_SPDIF_ON_OPT_B;
			else
				*src = SND_MOTU_CLOCK_SOURCE_ADAT_ON_OPT_B;
		}
		break;
	}
	default:
		*src = SND_MOTU_CLOCK_SOURCE_UNKNOWN;
		break;
	}

	return 0;
}

int snd_motu_protocol_v3_switch_fetching_mode(struct snd_motu *motu,
					      bool enable)
{
	__be32 reg;
	u32 data;
	int err;

	err = snd_motu_transaction_read(motu, V3_CLOCK_STATUS_OFFSET, &reg,
					sizeof(reg));
	if (err < 0)
		return 0;
	data = be32_to_cpu(reg);

	if (enable)
		data |= V3_FETCH_PCM_FRAMES;
	else
		data &= ~V3_FETCH_PCM_FRAMES;

	reg = cpu_to_be32(data);
	return snd_motu_transaction_write(motu, V3_CLOCK_STATUS_OFFSET, &reg,
					  sizeof(reg));
}

static int detect_packet_formats_828mk3(struct snd_motu *motu, u32 data)
{
	if (data & V3_ENABLE_OPT_IN_IFACE_A) {
		if (data & V3_NO_ADAT_OPT_IN_IFACE_A) {
			motu->tx_packet_formats.pcm_chunks[0] += 4;
			motu->tx_packet_formats.pcm_chunks[1] += 4;
		} else {
			motu->tx_packet_formats.pcm_chunks[0] += 8;
			motu->tx_packet_formats.pcm_chunks[1] += 4;
		}
	}

	if (data & V3_ENABLE_OPT_IN_IFACE_B) {
		if (data & V3_NO_ADAT_OPT_IN_IFACE_B) {
			motu->tx_packet_formats.pcm_chunks[0] += 4;
			motu->tx_packet_formats.pcm_chunks[1] += 4;
		} else {
			motu->tx_packet_formats.pcm_chunks[0] += 8;
			motu->tx_packet_formats.pcm_chunks[1] += 4;
		}
	}

	if (data & V3_ENABLE_OPT_OUT_IFACE_A) {
		if (data & V3_NO_ADAT_OPT_OUT_IFACE_A) {
			motu->rx_packet_formats.pcm_chunks[0] += 4;
			motu->rx_packet_formats.pcm_chunks[1] += 4;
		} else {
			motu->rx_packet_formats.pcm_chunks[0] += 8;
			motu->rx_packet_formats.pcm_chunks[1] += 4;
		}
	}

	if (data & V3_ENABLE_OPT_OUT_IFACE_B) {
		if (data & V3_NO_ADAT_OPT_OUT_IFACE_B) {
			motu->rx_packet_formats.pcm_chunks[0] += 4;
			motu->rx_packet_formats.pcm_chunks[1] += 4;
		} else {
			motu->rx_packet_formats.pcm_chunks[0] += 8;
			motu->rx_packet_formats.pcm_chunks[1] += 4;
		}
	}

	return 0;
}

int snd_motu_protocol_v3_cache_packet_formats(struct snd_motu *motu)
{
	__be32 reg;
	u32 data;
	int err;

	motu->tx_packet_formats.pcm_byte_offset = 10;
	motu->rx_packet_formats.pcm_byte_offset = 10;

	motu->tx_packet_formats.msg_chunks = 2;
	motu->rx_packet_formats.msg_chunks = 2;

	err = snd_motu_transaction_read(motu, V3_OPT_IFACE_MODE_OFFSET, &reg,
					sizeof(reg));
	if (err < 0)
		return err;
	data = be32_to_cpu(reg);

	memcpy(motu->tx_packet_formats.pcm_chunks,
	       motu->spec->tx_fixed_pcm_chunks,
	       sizeof(motu->tx_packet_formats.pcm_chunks));
	memcpy(motu->rx_packet_formats.pcm_chunks,
	       motu->spec->rx_fixed_pcm_chunks,
	       sizeof(motu->rx_packet_formats.pcm_chunks));

	if (motu->spec == &snd_motu_spec_828mk3_fw || motu->spec == &snd_motu_spec_828mk3_hybrid)
		return detect_packet_formats_828mk3(motu, data);
	else
		return 0;
}

const struct snd_motu_spec snd_motu_spec_828mk3_fw = {
	.name = "828mk3",
	.protocol_version = SND_MOTU_PROTOCOL_V3,
	.flags = SND_MOTU_SPEC_RX_MIDI_3RD_Q |
		 SND_MOTU_SPEC_TX_MIDI_3RD_Q |
		 SND_MOTU_SPEC_COMMAND_DSP,
	.tx_fixed_pcm_chunks = {18, 18, 14},
	.rx_fixed_pcm_chunks = {14, 14, 10},
};

const struct snd_motu_spec snd_motu_spec_828mk3_hybrid = {
	.name = "828mk3",
	.protocol_version = SND_MOTU_PROTOCOL_V3,
	.flags = SND_MOTU_SPEC_RX_MIDI_3RD_Q |
		 SND_MOTU_SPEC_TX_MIDI_3RD_Q |
		 SND_MOTU_SPEC_COMMAND_DSP,
	.tx_fixed_pcm_chunks = {18, 18, 14},
	.rx_fixed_pcm_chunks = {14, 14, 14},	// Additional 4 dummy chunks at higher rate.
};

const struct snd_motu_spec snd_motu_spec_ultralite_mk3 = {
	.name = "UltraLiteMk3",
	.protocol_version = SND_MOTU_PROTOCOL_V3,
	.flags = SND_MOTU_SPEC_RX_MIDI_3RD_Q |
		 SND_MOTU_SPEC_TX_MIDI_3RD_Q |
		 SND_MOTU_SPEC_COMMAND_DSP,
	.tx_fixed_pcm_chunks = {18, 14, 10},
	.rx_fixed_pcm_chunks = {14, 14, 14},
};

const struct snd_motu_spec snd_motu_spec_audio_express = {
	.name = "AudioExpress",
	.protocol_version = SND_MOTU_PROTOCOL_V3,
	.flags = SND_MOTU_SPEC_RX_MIDI_2ND_Q |
		 SND_MOTU_SPEC_TX_MIDI_3RD_Q |
		 SND_MOTU_SPEC_REGISTER_DSP,
	.tx_fixed_pcm_chunks = {10, 10, 0},
	.rx_fixed_pcm_chunks = {10, 10, 0},
};

const struct snd_motu_spec snd_motu_spec_4pre = {
	.name = "4pre",
	.protocol_version = SND_MOTU_PROTOCOL_V3,
	.flags = SND_MOTU_SPEC_REGISTER_DSP,
	.tx_fixed_pcm_chunks = {10, 10, 0},
	.rx_fixed_pcm_chunks = {10, 10, 0},
};
