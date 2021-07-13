// SPDX-License-Identifier: GPL-2.0-only
/*
 * motu-protocol-v2.c - a part of driver for MOTU FireWire series
 *
 * Copyright (c) 2015-2017 Takashi Sakamoto <o-takashi@sakamocchi.jp>
 */

#include "motu.h"

#define V2_CLOCK_STATUS_OFFSET			0x0b14
#define  V2_CLOCK_RATE_MASK			0x00000038
#define  V2_CLOCK_RATE_SHIFT			3
#define  V2_CLOCK_SRC_MASK			0x00000007
#define  V2_CLOCK_SRC_SHIFT			0
#define   V2_CLOCK_SRC_AESEBU_ON_XLR		0x07
#define   V2_CLOCK_SRC_ADAT_ON_DSUB		0x05
#define   V2_CLOCK_SRC_WORD_ON_BNC		0x04
#define   V2_CLOCK_SRC_SPH			0x03
#define   V2_CLOCK_SRC_SPDIF			0x02	// on either coaxial or optical
#define   V2_CLOCK_SRC_ADAT_ON_OPT		0x01
#define   V2_CLOCK_SRC_INTERNAL			0x00
#define  V2_CLOCK_FETCH_ENABLE			0x02000000
#define  V2_CLOCK_MODEL_SPECIFIC		0x04000000

#define V2_IN_OUT_CONF_OFFSET			0x0c04
#define  V2_OPT_OUT_IFACE_MASK			0x00000c00
#define  V2_OPT_OUT_IFACE_SHIFT			10
#define  V2_OPT_IN_IFACE_MASK			0x00000300
#define  V2_OPT_IN_IFACE_SHIFT			8
#define  V2_OPT_IFACE_MODE_NONE			0
#define  V2_OPT_IFACE_MODE_ADAT			1
#define  V2_OPT_IFACE_MODE_SPDIF		2

static int get_clock_rate(u32 data, unsigned int *rate)
{
	unsigned int index = (data & V2_CLOCK_RATE_MASK) >> V2_CLOCK_RATE_SHIFT;
	if (index >= ARRAY_SIZE(snd_motu_clock_rates))
		return -EIO;

	*rate = snd_motu_clock_rates[index];

	return 0;
}

int snd_motu_protocol_v2_get_clock_rate(struct snd_motu *motu,
					unsigned int *rate)
{
	__be32 reg;
	int err;

	err = snd_motu_transaction_read(motu, V2_CLOCK_STATUS_OFFSET, &reg,
					sizeof(reg));
	if (err < 0)
		return err;

	return get_clock_rate(be32_to_cpu(reg), rate);
}

int snd_motu_protocol_v2_set_clock_rate(struct snd_motu *motu,
					unsigned int rate)
{
	__be32 reg;
	u32 data;
	int i;
	int err;

	for (i = 0; i < ARRAY_SIZE(snd_motu_clock_rates); ++i) {
		if (snd_motu_clock_rates[i] == rate)
			break;
	}
	if (i == ARRAY_SIZE(snd_motu_clock_rates))
		return -EINVAL;

	err = snd_motu_transaction_read(motu, V2_CLOCK_STATUS_OFFSET, &reg,
					sizeof(reg));
	if (err < 0)
		return err;
	data = be32_to_cpu(reg);

	data &= ~V2_CLOCK_RATE_MASK;
	data |= i << V2_CLOCK_RATE_SHIFT;

	reg = cpu_to_be32(data);
	return snd_motu_transaction_write(motu, V2_CLOCK_STATUS_OFFSET, &reg,
					  sizeof(reg));
}

static int get_clock_source(struct snd_motu *motu, u32 data,
			    enum snd_motu_clock_source *src)
{
	switch (data & V2_CLOCK_SRC_MASK) {
	case V2_CLOCK_SRC_INTERNAL:
		*src = SND_MOTU_CLOCK_SOURCE_INTERNAL;
		break;
	case V2_CLOCK_SRC_ADAT_ON_OPT:
		*src = SND_MOTU_CLOCK_SOURCE_ADAT_ON_OPT;
		break;
	case V2_CLOCK_SRC_SPDIF:
	{
		bool support_iec60958_on_opt = (motu->spec == &snd_motu_spec_828mk2 ||
						motu->spec == &snd_motu_spec_traveler);

		if (!support_iec60958_on_opt) {
			*src = SND_MOTU_CLOCK_SOURCE_SPDIF_ON_COAX;
		} else {
			__be32 reg;

			// To check the configuration of optical interface.
			int err = snd_motu_transaction_read(motu, V2_IN_OUT_CONF_OFFSET, &reg,
							    sizeof(reg));
			if (err < 0)
				return err;

			if (((data & V2_OPT_IN_IFACE_MASK) >> V2_OPT_IN_IFACE_SHIFT) ==
			    V2_OPT_IFACE_MODE_SPDIF)
				*src = SND_MOTU_CLOCK_SOURCE_SPDIF_ON_OPT;
			else
				*src = SND_MOTU_CLOCK_SOURCE_SPDIF_ON_COAX;
		}
		break;
	}
	case V2_CLOCK_SRC_SPH:
		*src = SND_MOTU_CLOCK_SOURCE_SPH;
		break;
	case V2_CLOCK_SRC_WORD_ON_BNC:
		*src = SND_MOTU_CLOCK_SOURCE_WORD_ON_BNC;
		break;
	case V2_CLOCK_SRC_ADAT_ON_DSUB:
		*src = SND_MOTU_CLOCK_SOURCE_ADAT_ON_DSUB;
		break;
	case V2_CLOCK_SRC_AESEBU_ON_XLR:
		*src = SND_MOTU_CLOCK_SOURCE_AESEBU_ON_XLR;
		break;
	default:
		*src = SND_MOTU_CLOCK_SOURCE_UNKNOWN;
		break;
	}

	return 0;
}

int snd_motu_protocol_v2_get_clock_source(struct snd_motu *motu,
					  enum snd_motu_clock_source *src)
{
	__be32 reg;
	int err;

	err = snd_motu_transaction_read(motu, V2_CLOCK_STATUS_OFFSET, &reg,
					sizeof(reg));
	if (err < 0)
		return err;

	return get_clock_source(motu, be32_to_cpu(reg), src);
}

// Expected for Traveler and 896HD, which implements Altera Cyclone EP1C3.
static int switch_fetching_mode_cyclone(struct snd_motu *motu, u32 *data,
					bool enable)
{
	*data |= V2_CLOCK_MODEL_SPECIFIC;

	return 0;
}

// For UltraLite and 8pre, which implements Xilinx Spartan XC3S200.
static int switch_fetching_mode_spartan(struct snd_motu *motu, u32 *data,
					bool enable)
{
	unsigned int rate;
	enum snd_motu_clock_source src;
	int err;

	err = get_clock_source(motu, *data, &src);
	if (err < 0)
		return err;

	err = get_clock_rate(*data, &rate);
	if (err < 0)
		return err;

	if (src == SND_MOTU_CLOCK_SOURCE_SPH && rate > 48000)
		*data |= V2_CLOCK_MODEL_SPECIFIC;

	return 0;
}

int snd_motu_protocol_v2_switch_fetching_mode(struct snd_motu *motu,
					      bool enable)
{
	if (motu->spec == &snd_motu_spec_828mk2) {
		// 828mkII implements Altera ACEX 1K EP1K30. Nothing to do.
		return 0;
	} else {
		__be32 reg;
		u32 data;
		int err;

		err = snd_motu_transaction_read(motu, V2_CLOCK_STATUS_OFFSET,
						&reg, sizeof(reg));
		if (err < 0)
			return err;
		data = be32_to_cpu(reg);

		data &= ~(V2_CLOCK_FETCH_ENABLE | V2_CLOCK_MODEL_SPECIFIC);
		if (enable)
			data |= V2_CLOCK_FETCH_ENABLE;

		if (motu->spec == &snd_motu_spec_traveler)
			err = switch_fetching_mode_cyclone(motu, &data, enable);
		else
			err = switch_fetching_mode_spartan(motu, &data, enable);
		if (err < 0)
			return err;

		reg = cpu_to_be32(data);
		return snd_motu_transaction_write(motu, V2_CLOCK_STATUS_OFFSET,
						  &reg, sizeof(reg));
	}
}

int snd_motu_protocol_v2_cache_packet_formats(struct snd_motu *motu)
{
	bool has_two_opt_ifaces = (motu->spec == &snd_motu_spec_8pre);
	__be32 reg;
	u32 data;
	int err;

	motu->tx_packet_formats.pcm_byte_offset = 10;
	motu->rx_packet_formats.pcm_byte_offset = 10;

	motu->tx_packet_formats.msg_chunks = 2;
	motu->rx_packet_formats.msg_chunks = 2;

	err = snd_motu_transaction_read(motu, V2_IN_OUT_CONF_OFFSET, &reg,
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

	if (((data & V2_OPT_IN_IFACE_MASK) >> V2_OPT_IN_IFACE_SHIFT) == V2_OPT_IFACE_MODE_ADAT) {
		motu->tx_packet_formats.pcm_chunks[0] += 8;

		if (!has_two_opt_ifaces)
			motu->tx_packet_formats.pcm_chunks[1] += 4;
		else
			motu->tx_packet_formats.pcm_chunks[1] += 8;
	}

	if (((data & V2_OPT_OUT_IFACE_MASK) >> V2_OPT_OUT_IFACE_SHIFT) == V2_OPT_IFACE_MODE_ADAT) {
		motu->rx_packet_formats.pcm_chunks[0] += 8;

		if (!has_two_opt_ifaces)
			motu->rx_packet_formats.pcm_chunks[1] += 4;
		else
			motu->rx_packet_formats.pcm_chunks[1] += 8;
	}

	return 0;
}

const struct snd_motu_spec snd_motu_spec_828mk2 = {
	.name = "828mk2",
	.protocol_version = SND_MOTU_PROTOCOL_V2,
	.flags = SND_MOTU_SPEC_RX_MIDI_2ND_Q |
		 SND_MOTU_SPEC_TX_MIDI_2ND_Q,
	.tx_fixed_pcm_chunks = {14, 14, 0},
	.rx_fixed_pcm_chunks = {14, 14, 0},
};

const struct snd_motu_spec snd_motu_spec_traveler = {
	.name = "Traveler",
	.protocol_version = SND_MOTU_PROTOCOL_V2,
	.flags = SND_MOTU_SPEC_RX_MIDI_2ND_Q |
		 SND_MOTU_SPEC_TX_MIDI_2ND_Q,
	.tx_fixed_pcm_chunks = {14, 14, 8},
	.rx_fixed_pcm_chunks = {14, 14, 8},
};

const struct snd_motu_spec snd_motu_spec_ultralite = {
	.name = "UltraLite",
	.protocol_version = SND_MOTU_PROTOCOL_V2,
	.flags = SND_MOTU_SPEC_RX_MIDI_2ND_Q |
		 SND_MOTU_SPEC_TX_MIDI_2ND_Q,
	.tx_fixed_pcm_chunks = {14, 14, 0},
	.rx_fixed_pcm_chunks = {14, 14, 0},
};

const struct snd_motu_spec snd_motu_spec_8pre = {
	.name = "8pre",
	.protocol_version = SND_MOTU_PROTOCOL_V2,
	.flags = SND_MOTU_SPEC_RX_MIDI_2ND_Q |
		 SND_MOTU_SPEC_TX_MIDI_2ND_Q,
	// Two dummy chunks always in the end of data block.
	.tx_fixed_pcm_chunks = {10, 10, 0},
	.rx_fixed_pcm_chunks = {6, 6, 0},
};
