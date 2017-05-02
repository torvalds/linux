/*
 * motu-protocol-v3.c - a part of driver for MOTU FireWire series
 *
 * Copyright (c) 2015-2017 Takashi Sakamoto <o-takashi@sakamocchi.jp>
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include <linux/delay.h>
#include "motu.h"

#define V3_CLOCK_STATUS_OFFSET		0x0b14
#define  V3_FETCH_PCM_FRAMES		0x02000000
#define  V3_CLOCK_RATE_MASK		0x0000ff00
#define  V3_CLOCK_RATE_SHIFT		8
#define  V3_CLOCK_SOURCE_MASK		0x000000ff

#define V3_OPT_IFACE_MODE_OFFSET	0x0c94
#define  V3_ENABLE_OPT_IN_IFACE_A	0x00000001
#define  V3_ENABLE_OPT_IN_IFACE_B	0x00000002
#define  V3_ENABLE_OPT_OUT_IFACE_A	0x00000100
#define  V3_ENABLE_OPT_OUT_IFACE_B	0x00000200
#define  V3_NO_ADAT_OPT_IN_IFACE_A	0x00010000
#define  V3_NO_ADAT_OPT_IN_IFACE_B	0x00100000
#define  V3_NO_ADAT_OPT_OUT_IFACE_A	0x00040000
#define  V3_NO_ADAT_OPT_OUT_IFACE_B	0x00400000

static int v3_get_clock_rate(struct snd_motu *motu, unsigned int *rate)
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

static int v3_set_clock_rate(struct snd_motu *motu, unsigned int rate)
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
		/* Cost expensive. */
		if (msleep_interruptible(4000) > 0)
			return -EINTR;
	}

	return 0;
}

static int v3_get_clock_source(struct snd_motu *motu,
			       enum snd_motu_clock_source *src)
{
	__be32 reg;
	u32 data;
	unsigned int val;
	int err;

	err = snd_motu_transaction_read(motu, V3_CLOCK_STATUS_OFFSET, &reg,
					sizeof(reg));
	if (err < 0)
		return err;
	data = be32_to_cpu(reg);

	val = data & V3_CLOCK_SOURCE_MASK;
	if (val == 0x00) {
		*src = SND_MOTU_CLOCK_SOURCE_INTERNAL;
	} else if (val == 0x01) {
		*src = SND_MOTU_CLOCK_SOURCE_WORD_ON_BNC;
	} else if (val == 0x10) {
		*src = SND_MOTU_CLOCK_SOURCE_SPDIF_ON_COAX;
	} else if (val == 0x18 || val == 0x19) {
		err = snd_motu_transaction_read(motu, V3_OPT_IFACE_MODE_OFFSET,
						&reg, sizeof(reg));
		if (err < 0)
			return err;
		data = be32_to_cpu(reg);

		if (val == 0x18) {
			if (data & V3_NO_ADAT_OPT_IN_IFACE_A)
				*src = SND_MOTU_CLOCK_SOURCE_SPDIF_ON_OPT_A;
			else
				*src = SND_MOTU_CLOCK_SOURCE_ADAT_ON_OPT_A;
		} else {
			if (data & V3_NO_ADAT_OPT_IN_IFACE_B)
				*src = SND_MOTU_CLOCK_SOURCE_SPDIF_ON_OPT_B;
			else
				*src = SND_MOTU_CLOCK_SOURCE_ADAT_ON_OPT_B;
		}
	} else {
		*src = SND_MOTU_CLOCK_SOURCE_UNKNOWN;
	}

	return 0;
}

static int v3_switch_fetching_mode(struct snd_motu *motu, bool enable)
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
			if (flags & SND_MOTU_SPEC_SUPPORT_CLOCK_X4)
				pcm_chunks[2] += 2;
		}

		if (flags & SND_MOTU_SPEC_TX_RETURN_CHUNK) {
			pcm_chunks[0] += 2;
			pcm_chunks[1] += 2;
			if (flags & SND_MOTU_SPEC_SUPPORT_CLOCK_X4)
				pcm_chunks[2] += 2;
		}

		if (flags & SND_MOTU_SPEC_TX_REVERB_CHUNK) {
			pcm_chunks[0] += 2;
			pcm_chunks[1] += 2;
		}
	} else {
		/*
		 * Packets to v2 units transfer main-out-1/2 and phone-out-1/2.
		 */
		pcm_chunks[0] += 4;
		pcm_chunks[1] += 4;
	}

	/*
	 * At least, packets have two data chunks for S/PDIF on coaxial
	 * interface.
	 */
	pcm_chunks[0] += 2;
	pcm_chunks[1] += 2;

	/*
	 * Fixed part consists of PCM chunks multiple of 4, with msg chunks. As
	 * a result, this part can includes empty data chunks.
	 */
	formats->fixed_part_pcm_chunks[0] = round_up(2 + pcm_chunks[0], 4) - 2;
	formats->fixed_part_pcm_chunks[1] = round_up(2 + pcm_chunks[1], 4) - 2;
	if (flags & SND_MOTU_SPEC_SUPPORT_CLOCK_X4)
		formats->fixed_part_pcm_chunks[2] =
					round_up(2 + pcm_chunks[2], 4) - 2;
}

static void calculate_differed_part(struct snd_motu_packet_format *formats,
				    enum snd_motu_spec_flags flags, u32 data,
				    u32 a_enable_mask, u32 a_no_adat_mask,
				    u32 b_enable_mask, u32 b_no_adat_mask)
{
	unsigned char pcm_chunks[3] = {0, 0, 0};
	int i;

	if ((flags & SND_MOTU_SPEC_HAS_OPT_IFACE_A) && (data & a_enable_mask)) {
		if (data & a_no_adat_mask) {
			/*
			 * Additional two data chunks for S/PDIF on optical
			 * interface A. This includes empty data chunks.
			 */
			pcm_chunks[0] += 4;
			pcm_chunks[1] += 4;
		} else {
			/*
			 * Additional data chunks for ADAT on optical interface
			 * A.
			 */
			pcm_chunks[0] += 8;
			pcm_chunks[1] += 4;
		}
	}

	if ((flags & SND_MOTU_SPEC_HAS_OPT_IFACE_B) && (data & b_enable_mask)) {
		if (data & b_no_adat_mask) {
			/*
			 * Additional two data chunks for S/PDIF on optical
			 * interface B. This includes empty data chunks.
			 */
			pcm_chunks[0] += 4;
			pcm_chunks[1] += 4;
		} else {
			/*
			 * Additional data chunks for ADAT on optical interface
			 * B.
			 */
			pcm_chunks[0] += 8;
			pcm_chunks[1] += 4;
		}
	}

	for (i = 0; i < 3; ++i) {
		if (pcm_chunks[i] > 0)
			pcm_chunks[i] = round_up(pcm_chunks[i], 4);

		formats->differed_part_pcm_chunks[i] = pcm_chunks[i];
	}
}

static int v3_cache_packet_formats(struct snd_motu *motu)
{
	__be32 reg;
	u32 data;
	int err;

	err = snd_motu_transaction_read(motu, V3_OPT_IFACE_MODE_OFFSET, &reg,
					sizeof(reg));
	if (err < 0)
		return err;
	data = be32_to_cpu(reg);

	calculate_fixed_part(&motu->tx_packet_formats, AMDTP_IN_STREAM,
			     motu->spec->flags, motu->spec->analog_in_ports);
	calculate_differed_part(&motu->tx_packet_formats,
			motu->spec->flags, data,
			V3_ENABLE_OPT_IN_IFACE_A, V3_NO_ADAT_OPT_IN_IFACE_A,
			V3_ENABLE_OPT_IN_IFACE_B, V3_NO_ADAT_OPT_IN_IFACE_B);

	calculate_fixed_part(&motu->rx_packet_formats, AMDTP_OUT_STREAM,
			     motu->spec->flags, motu->spec->analog_out_ports);
	calculate_differed_part(&motu->rx_packet_formats,
			motu->spec->flags, data,
			V3_ENABLE_OPT_OUT_IFACE_A, V3_NO_ADAT_OPT_OUT_IFACE_A,
			V3_ENABLE_OPT_OUT_IFACE_B, V3_NO_ADAT_OPT_OUT_IFACE_B);

	motu->tx_packet_formats.midi_flag_offset = 8;
	motu->tx_packet_formats.midi_byte_offset = 7;
	motu->tx_packet_formats.pcm_byte_offset = 10;

	motu->rx_packet_formats.midi_flag_offset = 8;
	motu->rx_packet_formats.midi_byte_offset = 7;
	motu->rx_packet_formats.pcm_byte_offset = 10;

	return 0;
}

const struct snd_motu_protocol snd_motu_protocol_v3 = {
	.get_clock_rate		= v3_get_clock_rate,
	.set_clock_rate		= v3_set_clock_rate,
	.get_clock_source	= v3_get_clock_source,
	.switch_fetching_mode	= v3_switch_fetching_mode,
	.cache_packet_formats	= v3_cache_packet_formats,
};
