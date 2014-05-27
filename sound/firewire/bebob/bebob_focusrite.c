/*
 * bebob_focusrite.c - a part of driver for BeBoB based devices
 *
 * Copyright (c) 2013-2014 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "./bebob.h"

#define ANA_IN	"Analog In"
#define DIG_IN	"Digital In"
#define ANA_OUT	"Analog Out"
#define DIG_OUT	"Digital Out"
#define STM_IN	"Stream In"

#define SAFFIRE_ADDRESS_BASE			0x000100000000ULL

#define SAFFIRE_OFFSET_CLOCK_SOURCE		0x00f8
#define SAFFIREPRO_OFFSET_CLOCK_SOURCE		0x0174

/* whether sync to external device or not */
#define SAFFIRE_OFFSET_CLOCK_SYNC_EXT		0x013c
#define SAFFIRE_LE_OFFSET_CLOCK_SYNC_EXT	0x0432
#define SAFFIREPRO_OFFSET_CLOCK_SYNC_EXT	0x0164

#define SAFFIRE_CLOCK_SOURCE_INTERNAL		0
#define SAFFIRE_CLOCK_SOURCE_SPDIF		1

/* '1' is absent, why... */
#define SAFFIREPRO_CLOCK_SOURCE_INTERNAL	0
#define SAFFIREPRO_CLOCK_SOURCE_SPDIF		2
#define SAFFIREPRO_CLOCK_SOURCE_ADAT1		3
#define SAFFIREPRO_CLOCK_SOURCE_ADAT2		4
#define SAFFIREPRO_CLOCK_SOURCE_WORDCLOCK	5

/* S/PDIF, ADAT1, ADAT2 is enabled or not. three quadlets */
#define SAFFIREPRO_ENABLE_DIG_IFACES		0x01a4

/* saffirepro has its own parameter for sampling frequency */
#define SAFFIREPRO_RATE_NOREBOOT		0x01cc
/* index is the value for this register */
static const unsigned int rates[] = {
	[0] = 0,
	[1] = 44100,
	[2] = 48000,
	[3] = 88200,
	[4] = 96000,
	[5] = 176400,
	[6] = 192000
};

/* saffire(no label)/saffire LE has metering */
#define SAFFIRE_OFFSET_METER			0x0100
#define SAFFIRE_LE_OFFSET_METER			0x0168

static inline int
saffire_read_block(struct snd_bebob *bebob, u64 offset,
		   u32 *buf, unsigned int size)
{
	unsigned int i;
	int err;
	__be32 *tmp = (__be32 *)buf;

	err =  snd_fw_transaction(bebob->unit, TCODE_READ_BLOCK_REQUEST,
				  SAFFIRE_ADDRESS_BASE + offset,
				  tmp, size, 0);
	if (err < 0)
		goto end;

	for (i = 0; i < size / sizeof(u32); i++)
		buf[i] = be32_to_cpu(tmp[i]);
end:
	return err;
}

static inline int
saffire_read_quad(struct snd_bebob *bebob, u64 offset, u32 *value)
{
	int err;
	__be32 tmp;

	err = snd_fw_transaction(bebob->unit, TCODE_READ_QUADLET_REQUEST,
				 SAFFIRE_ADDRESS_BASE + offset,
				 &tmp, sizeof(__be32), 0);
	if (err < 0)
		goto end;

	*value = be32_to_cpu(tmp);
end:
	return err;
}

static inline int
saffire_write_quad(struct snd_bebob *bebob, u64 offset, u32 value)
{
	__be32 data = cpu_to_be32(value);

	return snd_fw_transaction(bebob->unit, TCODE_WRITE_QUADLET_REQUEST,
				  SAFFIRE_ADDRESS_BASE + offset,
				  &data, sizeof(__be32), 0);
}

static char *const saffirepro_26_clk_src_labels[] = {
	SND_BEBOB_CLOCK_INTERNAL, "S/PDIF", "ADAT1", "ADAT2", "Word Clock"
};

static char *const saffirepro_10_clk_src_labels[] = {
	SND_BEBOB_CLOCK_INTERNAL, "S/PDIF", "Word Clock"
};
static int
saffirepro_both_clk_freq_get(struct snd_bebob *bebob, unsigned int *rate)
{
	u32 id;
	int err;

	err = saffire_read_quad(bebob, SAFFIREPRO_RATE_NOREBOOT, &id);
	if (err < 0)
		goto end;
	if (id >= ARRAY_SIZE(rates))
		err = -EIO;
	else
		*rate = rates[id];
end:
	return err;
}
static int
saffirepro_both_clk_freq_set(struct snd_bebob *bebob, unsigned int rate)
{
	u32 id;

	for (id = 0; id < ARRAY_SIZE(rates); id++) {
		if (rates[id] == rate)
			break;
	}
	if (id == ARRAY_SIZE(rates))
		return -EINVAL;

	return saffire_write_quad(bebob, SAFFIREPRO_RATE_NOREBOOT, id);
}
static int
saffirepro_both_clk_src_get(struct snd_bebob *bebob, unsigned int *id)
{
	int err;
	u32 value;

	err = saffire_read_quad(bebob, SAFFIREPRO_OFFSET_CLOCK_SOURCE, &value);
	if (err < 0)
		goto end;

	if (bebob->spec->clock->labels == saffirepro_10_clk_src_labels) {
		if (value == SAFFIREPRO_CLOCK_SOURCE_WORDCLOCK)
			*id = 2;
		else if (value == SAFFIREPRO_CLOCK_SOURCE_SPDIF)
			*id = 1;
	} else if (value > 1) {
		*id = value - 1;
	}
end:
	return err;
}

struct snd_bebob_spec saffire_le_spec;
static char *const saffire_both_clk_src_labels[] = {
	SND_BEBOB_CLOCK_INTERNAL, "S/PDIF"
};
static int
saffire_both_clk_src_get(struct snd_bebob *bebob, unsigned int *id)
{
	int err;
	u32 value;

	err = saffire_read_quad(bebob, SAFFIRE_OFFSET_CLOCK_SOURCE, &value);
	if (err >= 0)
		*id = 0xff & value;

	return err;
};
static char *const saffire_le_meter_labels[] = {
	ANA_IN, ANA_IN, DIG_IN,
	ANA_OUT, ANA_OUT, ANA_OUT, ANA_OUT,
	STM_IN, STM_IN
};
static char *const saffire_meter_labels[] = {
	ANA_IN, ANA_IN,
	STM_IN, STM_IN, STM_IN, STM_IN, STM_IN,
};
static int
saffire_meter_get(struct snd_bebob *bebob, u32 *buf, unsigned int size)
{
	struct snd_bebob_meter_spec *spec = bebob->spec->meter;
	unsigned int channels;
	u64 offset;
	int err;

	if (spec->labels == saffire_le_meter_labels)
		offset = SAFFIRE_LE_OFFSET_METER;
	else
		offset = SAFFIRE_OFFSET_METER;

	channels = spec->num * 2;
	if (size < channels * sizeof(u32))
		return -EIO;

	err = saffire_read_block(bebob, offset, buf, size);
	if (err >= 0 && spec->labels == saffire_le_meter_labels) {
		swap(buf[1], buf[3]);
		swap(buf[2], buf[3]);
		swap(buf[3], buf[4]);

		swap(buf[7], buf[10]);
		swap(buf[8], buf[10]);
		swap(buf[9], buf[11]);
		swap(buf[11], buf[12]);

		swap(buf[15], buf[16]);
	}

	return err;
}

static struct snd_bebob_rate_spec saffirepro_both_rate_spec = {
	.get	= &saffirepro_both_clk_freq_get,
	.set	= &saffirepro_both_clk_freq_set,
};
/* Saffire Pro 26 I/O  */
static struct snd_bebob_clock_spec saffirepro_26_clk_spec = {
	.num	= ARRAY_SIZE(saffirepro_26_clk_src_labels),
	.labels	= saffirepro_26_clk_src_labels,
	.get	= &saffirepro_both_clk_src_get,
};
struct snd_bebob_spec saffirepro_26_spec = {
	.clock	= &saffirepro_26_clk_spec,
	.rate	= &saffirepro_both_rate_spec,
	.meter	= NULL
};
/* Saffire Pro 10 I/O */
static struct snd_bebob_clock_spec saffirepro_10_clk_spec = {
	.num	= ARRAY_SIZE(saffirepro_10_clk_src_labels),
	.labels	= saffirepro_10_clk_src_labels,
	.get	= &saffirepro_both_clk_src_get,
};
struct snd_bebob_spec saffirepro_10_spec = {
	.clock	= &saffirepro_10_clk_spec,
	.rate	= &saffirepro_both_rate_spec,
	.meter	= NULL
};

static struct snd_bebob_rate_spec saffire_both_rate_spec = {
	.get	= &snd_bebob_stream_get_rate,
	.set	= &snd_bebob_stream_set_rate,
};
static struct snd_bebob_clock_spec saffire_both_clk_spec = {
	.num	= ARRAY_SIZE(saffire_both_clk_src_labels),
	.labels	= saffire_both_clk_src_labels,
	.get	= &saffire_both_clk_src_get,
};
/* Saffire LE */
static struct snd_bebob_meter_spec saffire_le_meter_spec = {
	.num	= ARRAY_SIZE(saffire_le_meter_labels),
	.labels	= saffire_le_meter_labels,
	.get	= &saffire_meter_get,
};
struct snd_bebob_spec saffire_le_spec = {
	.clock	= &saffire_both_clk_spec,
	.rate	= &saffire_both_rate_spec,
	.meter	= &saffire_le_meter_spec
};
/* Saffire */
static struct snd_bebob_meter_spec saffire_meter_spec = {
	.num	= ARRAY_SIZE(saffire_meter_labels),
	.labels	= saffire_meter_labels,
	.get	= &saffire_meter_get,
};
struct snd_bebob_spec saffire_spec = {
	.clock	= &saffire_both_clk_spec,
	.rate	= &saffire_both_rate_spec,
	.meter	= &saffire_meter_spec
};
