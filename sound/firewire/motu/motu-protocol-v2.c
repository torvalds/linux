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

static int v2_get_clock_rate(struct snd_motu *motu, unsigned int *rate)
{
	__be32 reg;
	int err;

	err = snd_motu_transaction_read(motu, V2_CLOCK_STATUS_OFFSET, &reg,
					sizeof(reg));
	if (err < 0)
		return err;

	return get_clock_rate(be32_to_cpu(reg), rate);
}

static int v2_set_clock_rate(struct snd_motu *motu, unsigned int rate)
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
	unsigned int index = data & V2_CLOCK_SRC_MASK;
	if (index > 5)
		return -EIO;

	switch (index) {
	case 0:
		*src = SND_MOTU_CLOCK_SOURCE_INTERNAL;
		break;
	case 1:
	{
		__be32 reg;

		// To check the configuration of optical interface.
		int err = snd_motu_transaction_read(motu, V2_IN_OUT_CONF_OFFSET,
						    &reg, sizeof(reg));
		if (err < 0)
			return err;

		if (be32_to_cpu(reg) & 0x00000200)
			*src = SND_MOTU_CLOCK_SOURCE_SPDIF_ON_OPT;
		else
			*src = SND_MOTU_CLOCK_SOURCE_ADAT_ON_OPT;
		break;
	}
	case 2:
		*src = SND_MOTU_CLOCK_SOURCE_SPDIF_ON_COAX;
		break;
	case 3:
		*src = SND_MOTU_CLOCK_SOURCE_SPH;
		break;
	case 4:
		*src = SND_MOTU_CLOCK_SOURCE_WORD_ON_BNC;
		break;
	case 5:
		*src = SND_MOTU_CLOCK_SOURCE_ADAT_ON_DSUB;
		break;
	default:
		return -EIO;
	}

	return 0;
}

static int v2_get_clock_source(struct snd_motu *motu,
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

static int v2_switch_fetching_mode(struct snd_motu *motu, bool enable)
{
	enum snd_motu_clock_source src;
	__be32 reg;
	u32 data;
	int err = 0;

	// 828mkII implements Altera ACEX 1K EP1K30. Nothing to do.
	if (motu->spec == &snd_motu_spec_828mk2)
		return 0;

	err = snd_motu_transaction_read(motu, V2_CLOCK_STATUS_OFFSET, &reg,
					sizeof(reg));
	if (err < 0)
		return err;
	data = be32_to_cpu(reg);

	err = get_clock_source(motu, data, &src);
	if (err < 0)
		return err;

	data &= ~(V2_CLOCK_FETCH_ENABLE | V2_CLOCK_MODEL_SPECIFIC);
	if (enable)
		data |= V2_CLOCK_FETCH_ENABLE;

	if (motu->spec->flags & SND_MOTU_SPEC_SUPPORT_CLOCK_X4) {
		// Expected for Traveler and 896HD, which implements Altera
		// Cyclone EP1C3.
		data |= V2_CLOCK_MODEL_SPECIFIC;
	} else {
		// For UltraLite and 8pre, which implements Xilinx Spartan
		// XC3S200.
		unsigned int rate;

		err = get_clock_rate(data, &rate);
		if (err < 0)
			return err;

		if (src == SND_MOTU_CLOCK_SOURCE_SPH && rate > 48000)
			data |= V2_CLOCK_MODEL_SPECIFIC;
	}

	reg = cpu_to_be32(data);
	return snd_motu_transaction_write(motu, V2_CLOCK_STATUS_OFFSET, &reg,
					  sizeof(reg));
}

static void calculate_fixed_part(struct snd_motu_packet_format *formats,
				 enum amdtp_stream_direction dir,
				 enum snd_motu_spec_flags flags,
				 unsigned char analog_ports)
{
	unsigned char pcm_chunks[3] = {0, 0, 0};

	formats->msg_chunks = 2;

	pcm_chunks[0] = analog_ports;
	pcm_chunks[1] = analog_ports;
	if (flags & SND_MOTU_SPEC_SUPPORT_CLOCK_X4)
		pcm_chunks[2] = analog_ports;

	if (dir == AMDTP_IN_STREAM) {
		if (flags & SND_MOTU_SPEC_TX_MICINST_CHUNK) {
			pcm_chunks[0] += 2;
			pcm_chunks[1] += 2;
		}
		if (flags & SND_MOTU_SPEC_TX_RETURN_CHUNK) {
			pcm_chunks[0] += 2;
			pcm_chunks[1] += 2;
		}
	} else {
		if (flags & SND_MOTU_SPEC_RX_SEPARATED_MAIN) {
			pcm_chunks[0] += 2;
			pcm_chunks[1] += 2;
		}

		// Packets to v2 units include 2 chunks for phone 1/2, except
		// for 176.4/192.0 kHz.
		pcm_chunks[0] += 2;
		pcm_chunks[1] += 2;
	}

	if (flags & SND_MOTU_SPEC_HAS_AESEBU_IFACE) {
		pcm_chunks[0] += 2;
		pcm_chunks[1] += 2;
	}

	/*
	 * All of v2 models have a pair of coaxial interfaces for digital in/out
	 * port. At 44.1/48.0/88.2/96.0 kHz, packets includes PCM from these
	 * ports.
	 */
	pcm_chunks[0] += 2;
	pcm_chunks[1] += 2;

	formats->fixed_part_pcm_chunks[0] = pcm_chunks[0];
	formats->fixed_part_pcm_chunks[1] = pcm_chunks[1];
	formats->fixed_part_pcm_chunks[2] = pcm_chunks[2];
}

static void calculate_differed_part(struct snd_motu_packet_format *formats,
				    enum snd_motu_spec_flags flags,
				    u32 data, u32 mask, u32 shift)
{
	unsigned char pcm_chunks[2] = {0, 0};

	/*
	 * When optical interfaces are configured for S/PDIF (TOSLINK),
	 * the above PCM frames come from them, instead of coaxial
	 * interfaces.
	 */
	data = (data & mask) >> shift;
	if (data == V2_OPT_IFACE_MODE_ADAT) {
		if (flags & SND_MOTU_SPEC_HAS_OPT_IFACE_A) {
			pcm_chunks[0] += 8;
			pcm_chunks[1] += 4;
		}
		// 8pre has two sets of optical interface and doesn't reduce
		// chunks for ADAT signals.
		if (flags & SND_MOTU_SPEC_HAS_OPT_IFACE_B) {
			pcm_chunks[1] += 4;
		}
	}

	/* At mode x4, no data chunks are supported in this part. */
	formats->differed_part_pcm_chunks[0] = pcm_chunks[0];
	formats->differed_part_pcm_chunks[1] = pcm_chunks[1];
}

static int v2_cache_packet_formats(struct snd_motu *motu)
{
	__be32 reg;
	u32 data;
	int err;

	err = snd_motu_transaction_read(motu, V2_IN_OUT_CONF_OFFSET, &reg,
					sizeof(reg));
	if (err < 0)
		return err;
	data = be32_to_cpu(reg);

	calculate_fixed_part(&motu->tx_packet_formats, AMDTP_IN_STREAM,
			     motu->spec->flags, motu->spec->analog_in_ports);
	calculate_differed_part(&motu->tx_packet_formats, motu->spec->flags,
			data, V2_OPT_IN_IFACE_MASK, V2_OPT_IN_IFACE_SHIFT);

	calculate_fixed_part(&motu->rx_packet_formats, AMDTP_OUT_STREAM,
			     motu->spec->flags, motu->spec->analog_out_ports);
	calculate_differed_part(&motu->rx_packet_formats, motu->spec->flags,
			data, V2_OPT_OUT_IFACE_MASK, V2_OPT_OUT_IFACE_SHIFT);

	motu->tx_packet_formats.pcm_byte_offset = 10;
	motu->rx_packet_formats.pcm_byte_offset = 10;

	return 0;
}

const struct snd_motu_protocol snd_motu_protocol_v2 = {
	.get_clock_rate		= v2_get_clock_rate,
	.set_clock_rate		= v2_set_clock_rate,
	.get_clock_source	= v2_get_clock_source,
	.switch_fetching_mode	= v2_switch_fetching_mode,
	.cache_packet_formats	= v2_cache_packet_formats,
};
