/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * oxfw.h - a part of driver for OXFW970/971 based devices
 *
 * Copyright (c) Clemens Ladisch <clemens@ladisch.de>
 */

#include <linux/device.h>
#include <linux/firewire.h>
#include <linux/firewire-constants.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/compat.h>
#include <linux/sched/signal.h>

#include <sound/control.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/info.h>
#include <sound/rawmidi.h>
#include <sound/firewire.h>
#include <sound/hwdep.h>

#include "../lib.h"
#include "../fcp.h"
#include "../packets-buffer.h"
#include "../iso-resources.h"
#include "../amdtp-am824.h"
#include "../cmp.h"

enum snd_oxfw_quirk {
	// Postpone transferring packets during handling asynchronous transaction. As a result,
	// next isochronous packet includes more events than one packet can include.
	SND_OXFW_QUIRK_JUMBO_PAYLOAD = 0x01,
	// The dbs field of CIP header in tx packet is wrong.
	SND_OXFW_QUIRK_WRONG_DBS = 0x02,
	// Blocking transmission mode is used.
	SND_OXFW_QUIRK_BLOCKING_TRANSMISSION = 0x04,
	// Stanton SCS1.d and SCS1.m support unique transaction.
	SND_OXFW_QUIRK_SCS_TRANSACTION = 0x08,
};

/* This is an arbitrary number for convinience. */
#define	SND_OXFW_STREAM_FORMAT_ENTRIES	10
struct snd_oxfw {
	struct snd_card *card;
	struct fw_unit *unit;
	struct mutex mutex;
	spinlock_t lock;

	// The combination of snd_oxfw_quirk enumeration-constants.
	unsigned int quirks;
	bool has_output;
	bool has_input;
	u8 *tx_stream_formats[SND_OXFW_STREAM_FORMAT_ENTRIES];
	u8 *rx_stream_formats[SND_OXFW_STREAM_FORMAT_ENTRIES];
	bool assumed;
	struct cmp_connection out_conn;
	struct cmp_connection in_conn;
	struct amdtp_stream tx_stream;
	struct amdtp_stream rx_stream;
	unsigned int substreams_count;

	unsigned int midi_input_ports;
	unsigned int midi_output_ports;

	int dev_lock_count;
	bool dev_lock_changed;
	wait_queue_head_t hwdep_wait;

	void *spec;

	struct amdtp_domain domain;
};

/*
 * AV/C Stream Format Information Specification 1.1 Working Draft
 * (Apr 2005, 1394TA)
 */
int avc_stream_set_format(struct fw_unit *unit, enum avc_general_plug_dir dir,
			  unsigned int pid, u8 *format, unsigned int len);
int avc_stream_get_format(struct fw_unit *unit,
			  enum avc_general_plug_dir dir, unsigned int pid,
			  u8 *buf, unsigned int *len, unsigned int eid);
static inline int
avc_stream_get_format_single(struct fw_unit *unit,
			     enum avc_general_plug_dir dir, unsigned int pid,
			     u8 *buf, unsigned int *len)
{
	return avc_stream_get_format(unit, dir, pid, buf, len, 0xff);
}
static inline int
avc_stream_get_format_list(struct fw_unit *unit,
			   enum avc_general_plug_dir dir, unsigned int pid,
			   u8 *buf, unsigned int *len,
			   unsigned int eid)
{
	return avc_stream_get_format(unit, dir, pid, buf, len, eid);
}

/*
 * AV/C Digital Interface Command Set General Specification 4.2
 * (Sep 2004, 1394TA)
 */
int avc_general_inquiry_sig_fmt(struct fw_unit *unit, unsigned int rate,
				enum avc_general_plug_dir dir,
				unsigned short pid);

int snd_oxfw_stream_init_duplex(struct snd_oxfw *oxfw);
int snd_oxfw_stream_reserve_duplex(struct snd_oxfw *oxfw,
				   struct amdtp_stream *stream,
				   unsigned int rate, unsigned int pcm_channels,
				   unsigned int frames_per_period,
				   unsigned int frames_per_buffer);
int snd_oxfw_stream_start_duplex(struct snd_oxfw *oxfw);
void snd_oxfw_stream_stop_duplex(struct snd_oxfw *oxfw);
void snd_oxfw_stream_destroy_duplex(struct snd_oxfw *oxfw);
void snd_oxfw_stream_update_duplex(struct snd_oxfw *oxfw);

struct snd_oxfw_stream_formation {
	unsigned int rate;
	unsigned int pcm;
	unsigned int midi;
};
int snd_oxfw_stream_parse_format(u8 *format,
				 struct snd_oxfw_stream_formation *formation);
int snd_oxfw_stream_get_current_formation(struct snd_oxfw *oxfw,
				enum avc_general_plug_dir dir,
				struct snd_oxfw_stream_formation *formation);

int snd_oxfw_stream_discover(struct snd_oxfw *oxfw);

void snd_oxfw_stream_lock_changed(struct snd_oxfw *oxfw);
int snd_oxfw_stream_lock_try(struct snd_oxfw *oxfw);
void snd_oxfw_stream_lock_release(struct snd_oxfw *oxfw);

int snd_oxfw_create_pcm(struct snd_oxfw *oxfw);

void snd_oxfw_proc_init(struct snd_oxfw *oxfw);

int snd_oxfw_create_midi(struct snd_oxfw *oxfw);

int snd_oxfw_create_hwdep(struct snd_oxfw *oxfw);

int snd_oxfw_add_spkr(struct snd_oxfw *oxfw, bool is_lacie);
int snd_oxfw_scs1x_add(struct snd_oxfw *oxfw);
void snd_oxfw_scs1x_update(struct snd_oxfw *oxfw);
