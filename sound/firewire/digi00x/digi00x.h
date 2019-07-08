/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * digi00x.h - a part of driver for Digidesign Digi 002/003 family
 *
 * Copyright (c) 2014-2015 Takashi Sakamoto
 */

#ifndef SOUND_DIGI00X_H_INCLUDED
#define SOUND_DIGI00X_H_INCLUDED

#include <linux/compat.h>
#include <linux/device.h>
#include <linux/firewire.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/sched/signal.h>

#include <sound/core.h>
#include <sound/initval.h>
#include <sound/info.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/firewire.h>
#include <sound/hwdep.h>
#include <sound/rawmidi.h>

#include "../lib.h"
#include "../iso-resources.h"
#include "../amdtp-stream.h"

struct snd_dg00x {
	struct snd_card *card;
	struct fw_unit *unit;

	struct mutex mutex;
	spinlock_t lock;

	bool registered;
	struct delayed_work dwork;

	struct amdtp_stream tx_stream;
	struct fw_iso_resources tx_resources;

	struct amdtp_stream rx_stream;
	struct fw_iso_resources rx_resources;

	unsigned int substreams_counter;

	/* for uapi */
	int dev_lock_count;
	bool dev_lock_changed;
	wait_queue_head_t hwdep_wait;

	/* For asynchronous messages. */
	struct fw_address_handler async_handler;
	u32 msg;

	/* Console models have additional MIDI ports for control surface. */
	bool is_console;
};

#define DG00X_ADDR_BASE		0xffffe0000000ull

#define DG00X_OFFSET_STREAMING_STATE	0x0000
#define DG00X_OFFSET_STREAMING_SET	0x0004
/* unknown but address in host space	0x0008 */
/* For LSB of the address		0x000c */
/* unknown				0x0010 */
#define DG00X_OFFSET_MESSAGE_ADDR	0x0014
/* For LSB of the address		0x0018 */
/* unknown				0x001c */
/* unknown				0x0020 */
/* not used			0x0024--0x00ff */
#define DG00X_OFFSET_ISOC_CHANNELS	0x0100
/* unknown				0x0104 */
/* unknown				0x0108 */
/* unknown				0x010c */
#define DG00X_OFFSET_LOCAL_RATE		0x0110
#define DG00X_OFFSET_EXTERNAL_RATE	0x0114
#define DG00X_OFFSET_CLOCK_SOURCE	0x0118
#define DG00X_OFFSET_OPT_IFACE_MODE	0x011c
/* unknown				0x0120 */
/* Mixer control on/off			0x0124 */
/* unknown				0x0128 */
#define DG00X_OFFSET_DETECT_EXTERNAL	0x012c
/* unknown				0x0138 */
#define DG00X_OFFSET_MMC		0x0400

enum snd_dg00x_rate {
	SND_DG00X_RATE_44100 = 0,
	SND_DG00X_RATE_48000,
	SND_DG00X_RATE_88200,
	SND_DG00X_RATE_96000,
	SND_DG00X_RATE_COUNT,
};

enum snd_dg00x_clock {
	SND_DG00X_CLOCK_INTERNAL = 0,
	SND_DG00X_CLOCK_SPDIF,
	SND_DG00X_CLOCK_ADAT,
	SND_DG00X_CLOCK_WORD,
	SND_DG00X_CLOCK_COUNT,
};

enum snd_dg00x_optical_mode {
	SND_DG00X_OPT_IFACE_MODE_ADAT = 0,
	SND_DG00X_OPT_IFACE_MODE_SPDIF,
	SND_DG00X_OPT_IFACE_MODE_COUNT,
};

#define DOT_MIDI_IN_PORTS	1
#define DOT_MIDI_OUT_PORTS	2

int amdtp_dot_init(struct amdtp_stream *s, struct fw_unit *unit,
		   enum amdtp_stream_direction dir);
int amdtp_dot_set_parameters(struct amdtp_stream *s, unsigned int rate,
			     unsigned int pcm_channels);
void amdtp_dot_reset(struct amdtp_stream *s);
int amdtp_dot_add_pcm_hw_constraints(struct amdtp_stream *s,
				     struct snd_pcm_runtime *runtime);
void amdtp_dot_midi_trigger(struct amdtp_stream *s, unsigned int port,
			  struct snd_rawmidi_substream *midi);

int snd_dg00x_transaction_register(struct snd_dg00x *dg00x);
int snd_dg00x_transaction_reregister(struct snd_dg00x *dg00x);
void snd_dg00x_transaction_unregister(struct snd_dg00x *dg00x);

extern const unsigned int snd_dg00x_stream_rates[SND_DG00X_RATE_COUNT];
extern const unsigned int snd_dg00x_stream_pcm_channels[SND_DG00X_RATE_COUNT];
int snd_dg00x_stream_get_external_rate(struct snd_dg00x *dg00x,
				       unsigned int *rate);
int snd_dg00x_stream_get_local_rate(struct snd_dg00x *dg00x,
				    unsigned int *rate);
int snd_dg00x_stream_set_local_rate(struct snd_dg00x *dg00x, unsigned int rate);
int snd_dg00x_stream_get_clock(struct snd_dg00x *dg00x,
			       enum snd_dg00x_clock *clock);
int snd_dg00x_stream_check_external_clock(struct snd_dg00x *dg00x,
					  bool *detect);
int snd_dg00x_stream_init_duplex(struct snd_dg00x *dg00x);
int snd_dg00x_stream_reserve_duplex(struct snd_dg00x *dg00x, unsigned int rate);
int snd_dg00x_stream_start_duplex(struct snd_dg00x *dg00x);
void snd_dg00x_stream_stop_duplex(struct snd_dg00x *dg00x);
void snd_dg00x_stream_update_duplex(struct snd_dg00x *dg00x);
void snd_dg00x_stream_destroy_duplex(struct snd_dg00x *dg00x);

void snd_dg00x_stream_lock_changed(struct snd_dg00x *dg00x);
int snd_dg00x_stream_lock_try(struct snd_dg00x *dg00x);
void snd_dg00x_stream_lock_release(struct snd_dg00x *dg00x);

void snd_dg00x_proc_init(struct snd_dg00x *dg00x);

int snd_dg00x_create_pcm_devices(struct snd_dg00x *dg00x);

int snd_dg00x_create_midi_devices(struct snd_dg00x *dg00x);

int snd_dg00x_create_hwdep_device(struct snd_dg00x *dg00x);
#endif
