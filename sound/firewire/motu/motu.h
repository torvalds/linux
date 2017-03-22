/*
 * motu.h - a part of driver for MOTU FireWire series
 *
 * Copyright (c) 2015-2017 Takashi Sakamoto <o-takashi@sakamocchi.jp>
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#ifndef SOUND_FIREWIRE_MOTU_H_INCLUDED
#define SOUND_FIREWIRE_MOTU_H_INCLUDED

#include <linux/device.h>
#include <linux/firewire.h>
#include <linux/firewire-constants.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/mutex.h>
#include <linux/slab.h>

#include <sound/control.h>
#include <sound/core.h>
#include <sound/pcm.h>

#include "../lib.h"
#include "../amdtp-stream.h"

struct snd_motu_packet_format {
	unsigned char pcm_byte_offset;

	unsigned char msg_chunks;
	unsigned char fixed_part_pcm_chunks[3];
	unsigned char differed_part_pcm_chunks[3];
};

struct snd_motu {
	struct snd_card *card;
	struct fw_unit *unit;
	struct mutex mutex;

	bool registered;
	struct delayed_work dwork;

	/* Model dependent information. */
	const struct snd_motu_spec *spec;

	/* For packet streaming */
	struct snd_motu_packet_format tx_packet_formats;
	struct snd_motu_packet_format rx_packet_formats;
	struct amdtp_stream tx_stream;
	struct amdtp_stream rx_stream;
};

enum snd_motu_spec_flags {
	SND_MOTU_SPEC_SUPPORT_CLOCK_X2	= 0x0001,
	SND_MOTU_SPEC_SUPPORT_CLOCK_X4	= 0x0002,
	SND_MOTU_SPEC_TX_MICINST_CHUNK	= 0x0004,
	SND_MOTU_SPEC_TX_RETURN_CHUNK	= 0x0008,
	SND_MOTU_SPEC_TX_REVERB_CHUNK	= 0x0010,
	SND_MOTU_SPEC_TX_AESEBU_CHUNK	= 0x0020,
	SND_MOTU_SPEC_HAS_OPT_IFACE_A	= 0x0040,
	SND_MOTU_SPEC_HAS_OPT_IFACE_B	= 0x0080,
	SND_MOTU_SPEC_HAS_MIDI		= 0x0100,
};

#define SND_MOTU_CLOCK_RATE_COUNT	6
extern const unsigned int snd_motu_clock_rates[SND_MOTU_CLOCK_RATE_COUNT];

enum snd_motu_clock_source {
	SND_MOTU_CLOCK_SOURCE_INTERNAL,
	SND_MOTU_CLOCK_SOURCE_ADAT_ON_DSUB,
	SND_MOTU_CLOCK_SOURCE_ADAT_ON_OPT,
	SND_MOTU_CLOCK_SOURCE_ADAT_ON_OPT_A,
	SND_MOTU_CLOCK_SOURCE_ADAT_ON_OPT_B,
	SND_MOTU_CLOCK_SOURCE_SPDIF_ON_OPT,
	SND_MOTU_CLOCK_SOURCE_SPDIF_ON_OPT_A,
	SND_MOTU_CLOCK_SOURCE_SPDIF_ON_OPT_B,
	SND_MOTU_CLOCK_SOURCE_SPDIF_ON_COAX,
	SND_MOTU_CLOCK_SOURCE_AESEBU_ON_XLR,
	SND_MOTU_CLOCK_SOURCE_WORD_ON_BNC,
	SND_MOTU_CLOCK_SOURCE_UNKNOWN,
};

struct snd_motu_protocol {
	int (*get_clock_rate)(struct snd_motu *motu, unsigned int *rate);
	int (*set_clock_rate)(struct snd_motu *motu, unsigned int rate);
	int (*get_clock_source)(struct snd_motu *motu,
				enum snd_motu_clock_source *source);
	int (*switch_fetching_mode)(struct snd_motu *motu, bool enable);
	int (*cache_packet_formats)(struct snd_motu *motu);
};

struct snd_motu_spec {
	const char *const name;
	enum snd_motu_spec_flags flags;

	unsigned char analog_in_ports;
	unsigned char analog_out_ports;

	const struct snd_motu_protocol *const protocol;
};

int amdtp_motu_init(struct amdtp_stream *s, struct fw_unit *unit,
		    enum amdtp_stream_direction dir,
		    const struct snd_motu_protocol *const protocol);
int amdtp_motu_set_parameters(struct amdtp_stream *s, unsigned int rate,
			      struct snd_motu_packet_format *formats);
int amdtp_motu_add_pcm_hw_constraints(struct amdtp_stream *s,
				      struct snd_pcm_runtime *runtime);
#endif
