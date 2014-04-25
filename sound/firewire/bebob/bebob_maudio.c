/*
 * bebob_maudio.c - a part of driver for BeBoB based devices
 *
 * Copyright (c) 2013-2014 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "./bebob.h"

/*
 * Just powering on, Firewire 410/Audiophile wait to
 * download firmware blob. To enable these devices, drivers should upload
 * firmware blob and send a command to initialize configuration to factory
 * settings when completing uploading. Then these devices generate bus reset
 * and are recognized as new devices with the firmware.
 *
 * For streaming, both of output and input streams are needed for Firewire 410
 * and Ozonic. The single stream is OK for the other devices even if the clock
 * source is not SYT-Match (I note no devices use SYT-Match).
 *
 * Without streaming, the devices except for Firewire Audiophile can mix any
 * input and output. For this reason, Audiophile cannot be used as standalone
 * mixer.
 */

#define MAUDIO_SPECIFIC_ADDRESS	0xffc700000000

#define METER_OFFSET		0x00600000

/* some device has sync info after metering data */
#define METER_SIZE_FW410	76	/* with sync info */
#define METER_SIZE_AUDIOPHILE	60	/* with sync info */
#define METER_SIZE_SOLO		52	/* with sync info */
#define METER_SIZE_OZONIC	48
#define METER_SIZE_NRV10	80

/* labels for metering */
#define ANA_IN		"Analog In"
#define ANA_OUT		"Analog Out"
#define DIG_IN		"Digital In"
#define SPDIF_IN	"S/PDIF In"
#define ADAT_IN		"ADAT In"
#define DIG_OUT		"Digital Out"
#define SPDIF_OUT	"S/PDIF Out"
#define ADAT_OUT	"ADAT Out"
#define STRM_IN		"Stream In"
#define AUX_OUT		"Aux Out"
#define HP_OUT		"HP Out"
/* for NRV */
#define UNKNOWN_METER	"Unknown"

static inline int
get_meter(struct snd_bebob *bebob, void *buf, unsigned int size)
{
	return snd_fw_transaction(bebob->unit, TCODE_READ_BLOCK_REQUEST,
				  MAUDIO_SPECIFIC_ADDRESS + METER_OFFSET,
				  buf, size, 0);
}

/* last 4 bytes are omitted because it's clock info. */
static char *const fw410_meter_labels[] = {
	ANA_IN, DIG_IN,
	ANA_OUT, ANA_OUT, ANA_OUT, ANA_OUT, DIG_OUT,
	HP_OUT
};
static char *const audiophile_meter_labels[] = {
	ANA_IN, DIG_IN,
	ANA_OUT, ANA_OUT, DIG_OUT,
	HP_OUT, AUX_OUT,
};
static char *const solo_meter_labels[] = {
	ANA_IN, DIG_IN,
	STRM_IN, STRM_IN,
	ANA_OUT, DIG_OUT
};

/* no clock info */
static char *const ozonic_meter_labels[] = {
	ANA_IN, ANA_IN,
	STRM_IN, STRM_IN,
	ANA_OUT, ANA_OUT
};
/* TODO: need testers. these positions are based on authour's assumption */
static char *const nrv10_meter_labels[] = {
	ANA_IN, ANA_IN, ANA_IN, ANA_IN,
	DIG_IN,
	ANA_OUT, ANA_OUT, ANA_OUT, ANA_OUT,
	DIG_IN
};
static int
normal_meter_get(struct snd_bebob *bebob, u32 *buf, unsigned int size)
{
	struct snd_bebob_meter_spec *spec = bebob->spec->meter;
	unsigned int c, channels;
	int err;

	channels = spec->num * 2;
	if (size < channels * sizeof(u32))
		return -EINVAL;

	err = get_meter(bebob, (void *)buf, size);
	if (err < 0)
		goto end;

	for (c = 0; c < channels; c++)
		be32_to_cpus(&buf[c]);

	/* swap stream channels because inverted */
	if (spec->labels == solo_meter_labels) {
		swap(buf[4], buf[6]);
		swap(buf[5], buf[7]);
	}
end:
	return err;
}

/* Firewire 410 specification */
static struct snd_bebob_rate_spec usual_rate_spec = {
	.get	= &snd_bebob_stream_get_rate,
	.set	= &snd_bebob_stream_set_rate,
};
static struct snd_bebob_meter_spec fw410_meter_spec = {
	.num	= ARRAY_SIZE(fw410_meter_labels),
	.labels	= fw410_meter_labels,
	.get	= &normal_meter_get
};
struct snd_bebob_spec maudio_fw410_spec = {
	.clock	= NULL,
	.rate	= &usual_rate_spec,
	.meter	= &fw410_meter_spec
};

/* Firewire Audiophile specification */
static struct snd_bebob_meter_spec audiophile_meter_spec = {
	.num	= ARRAY_SIZE(audiophile_meter_labels),
	.labels	= audiophile_meter_labels,
	.get	= &normal_meter_get
};
struct snd_bebob_spec maudio_audiophile_spec = {
	.clock	= NULL,
	.rate	= &usual_rate_spec,
	.meter	= &audiophile_meter_spec
};

/* Firewire Solo specification */
static struct snd_bebob_meter_spec solo_meter_spec = {
	.num	= ARRAY_SIZE(solo_meter_labels),
	.labels	= solo_meter_labels,
	.get	= &normal_meter_get
};
struct snd_bebob_spec maudio_solo_spec = {
	.clock	= NULL,
	.rate	= &usual_rate_spec,
	.meter	= &solo_meter_spec
};

/* Ozonic specification */
static struct snd_bebob_meter_spec ozonic_meter_spec = {
	.num	= ARRAY_SIZE(ozonic_meter_labels),
	.labels	= ozonic_meter_labels,
	.get	= &normal_meter_get
};
struct snd_bebob_spec maudio_ozonic_spec = {
	.clock	= NULL,
	.rate	= &usual_rate_spec,
	.meter	= &ozonic_meter_spec
};

/* NRV10 specification */
static struct snd_bebob_meter_spec nrv10_meter_spec = {
	.num	= ARRAY_SIZE(nrv10_meter_labels),
	.labels	= nrv10_meter_labels,
	.get	= &normal_meter_get
};
struct snd_bebob_spec maudio_nrv10_spec = {
	.clock	= NULL,
	.rate	= &usual_rate_spec,
	.meter	= &nrv10_meter_spec
};
