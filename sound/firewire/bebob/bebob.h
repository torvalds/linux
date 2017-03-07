/*
 * bebob.h - a part of driver for BeBoB based devices
 *
 * Copyright (c) 2013-2014 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#ifndef SOUND_BEBOB_H_INCLUDED
#define SOUND_BEBOB_H_INCLUDED

#include <linux/compat.h>
#include <linux/device.h>
#include <linux/firewire.h>
#include <linux/firewire-constants.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/sched/signal.h>

#include <sound/core.h>
#include <sound/initval.h>
#include <sound/info.h>
#include <sound/rawmidi.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/firewire.h>
#include <sound/hwdep.h>

#include "../lib.h"
#include "../fcp.h"
#include "../packets-buffer.h"
#include "../iso-resources.h"
#include "../amdtp-am824.h"
#include "../cmp.h"

/* basic register addresses on DM1000/DM1100/DM1500 */
#define BEBOB_ADDR_REG_INFO	0xffffc8020000ULL
#define BEBOB_ADDR_REG_REQ	0xffffc8021000ULL

struct snd_bebob;

#define SND_BEBOB_STRM_FMT_ENTRIES	7
struct snd_bebob_stream_formation {
	unsigned int pcm;
	unsigned int midi;
};
/* this is a lookup table for index of stream formations */
extern const unsigned int snd_bebob_rate_table[SND_BEBOB_STRM_FMT_ENTRIES];

/* device specific operations */
enum snd_bebob_clock_type {
	SND_BEBOB_CLOCK_TYPE_INTERNAL = 0,
	SND_BEBOB_CLOCK_TYPE_EXTERNAL,
	SND_BEBOB_CLOCK_TYPE_SYT,
};
struct snd_bebob_clock_spec {
	unsigned int num;
	const char *const *labels;
	enum snd_bebob_clock_type *types;
	int (*get)(struct snd_bebob *bebob, unsigned int *id);
};
struct snd_bebob_rate_spec {
	int (*get)(struct snd_bebob *bebob, unsigned int *rate);
	int (*set)(struct snd_bebob *bebob, unsigned int rate);
};
struct snd_bebob_meter_spec {
	unsigned int num;
	const char *const *labels;
	int (*get)(struct snd_bebob *bebob, u32 *target, unsigned int size);
};
struct snd_bebob_spec {
	const struct snd_bebob_clock_spec *clock;
	const struct snd_bebob_rate_spec *rate;
	const struct snd_bebob_meter_spec *meter;
};

struct snd_bebob {
	struct snd_card *card;
	struct fw_unit *unit;
	int card_index;

	struct mutex mutex;
	spinlock_t lock;

	bool registered;
	struct delayed_work dwork;

	const struct ieee1394_device_id *entry;
	const struct snd_bebob_spec *spec;

	unsigned int midi_input_ports;
	unsigned int midi_output_ports;

	bool connected;

	struct amdtp_stream tx_stream;
	struct amdtp_stream rx_stream;
	struct cmp_connection out_conn;
	struct cmp_connection in_conn;
	unsigned int substreams_counter;

	struct snd_bebob_stream_formation
		tx_stream_formations[SND_BEBOB_STRM_FMT_ENTRIES];
	struct snd_bebob_stream_formation
		rx_stream_formations[SND_BEBOB_STRM_FMT_ENTRIES];

	int sync_input_plug;

	/* for uapi */
	int dev_lock_count;
	bool dev_lock_changed;
	wait_queue_head_t hwdep_wait;

	/* for M-Audio special devices */
	void *maudio_special_quirk;

	/* For BeBoB version quirk. */
	unsigned int version;
};

static inline int
snd_bebob_read_block(struct fw_unit *unit, u64 addr, void *buf, int size)
{
	return snd_fw_transaction(unit, TCODE_READ_BLOCK_REQUEST,
				  BEBOB_ADDR_REG_INFO + addr,
				  buf, size, 0);
}

static inline int
snd_bebob_read_quad(struct fw_unit *unit, u64 addr, u32 *buf)
{
	return snd_fw_transaction(unit, TCODE_READ_QUADLET_REQUEST,
				  BEBOB_ADDR_REG_INFO + addr,
				  (void *)buf, sizeof(u32), 0);
}

/* AV/C Audio Subunit Specification 1.0 (Oct 2000, 1394TA) */
int avc_audio_set_selector(struct fw_unit *unit, unsigned int subunit_id,
			   unsigned int fb_id, unsigned int num);
int avc_audio_get_selector(struct fw_unit *unit, unsigned  int subunit_id,
			   unsigned int fb_id, unsigned int *num);

/*
 * AVC command extensions, AV/C Unit and Subunit, Revision 17
 * (Nov 2003, BridgeCo)
 */
#define	AVC_BRIDGECO_ADDR_BYTES	6
enum avc_bridgeco_plug_dir {
	AVC_BRIDGECO_PLUG_DIR_IN	= 0x00,
	AVC_BRIDGECO_PLUG_DIR_OUT	= 0x01
};
enum avc_bridgeco_plug_mode {
	AVC_BRIDGECO_PLUG_MODE_UNIT		= 0x00,
	AVC_BRIDGECO_PLUG_MODE_SUBUNIT		= 0x01,
	AVC_BRIDGECO_PLUG_MODE_FUNCTION_BLOCK	= 0x02
};
enum avc_bridgeco_plug_unit {
	AVC_BRIDGECO_PLUG_UNIT_ISOC	= 0x00,
	AVC_BRIDGECO_PLUG_UNIT_EXT	= 0x01,
	AVC_BRIDGECO_PLUG_UNIT_ASYNC	= 0x02
};
enum avc_bridgeco_plug_type {
	AVC_BRIDGECO_PLUG_TYPE_ISOC	= 0x00,
	AVC_BRIDGECO_PLUG_TYPE_ASYNC	= 0x01,
	AVC_BRIDGECO_PLUG_TYPE_MIDI	= 0x02,
	AVC_BRIDGECO_PLUG_TYPE_SYNC	= 0x03,
	AVC_BRIDGECO_PLUG_TYPE_ANA	= 0x04,
	AVC_BRIDGECO_PLUG_TYPE_DIG	= 0x05,
	AVC_BRIDGECO_PLUG_TYPE_ADDITION	= 0x06
};
static inline void
avc_bridgeco_fill_unit_addr(u8 buf[AVC_BRIDGECO_ADDR_BYTES],
			    enum avc_bridgeco_plug_dir dir,
			    enum avc_bridgeco_plug_unit unit,
			    unsigned int pid)
{
	buf[0] = 0xff;	/* Unit */
	buf[1] = dir;
	buf[2] = AVC_BRIDGECO_PLUG_MODE_UNIT;
	buf[3] = unit;
	buf[4] = 0xff & pid;
	buf[5] = 0xff;	/* reserved */
}
static inline void
avc_bridgeco_fill_msu_addr(u8 buf[AVC_BRIDGECO_ADDR_BYTES],
			   enum avc_bridgeco_plug_dir dir,
			   unsigned int pid)
{
	buf[0] = 0x60;	/* Music subunit */
	buf[1] = dir;
	buf[2] = AVC_BRIDGECO_PLUG_MODE_SUBUNIT;
	buf[3] = 0xff & pid;
	buf[4] = 0xff;	/* reserved */
	buf[5] = 0xff;	/* reserved */
}
int avc_bridgeco_get_plug_ch_pos(struct fw_unit *unit,
				 u8 addr[AVC_BRIDGECO_ADDR_BYTES],
				 u8 *buf, unsigned int len);
int avc_bridgeco_get_plug_type(struct fw_unit *unit,
			       u8 addr[AVC_BRIDGECO_ADDR_BYTES],
			       enum avc_bridgeco_plug_type *type);
int avc_bridgeco_get_plug_section_type(struct fw_unit *unit,
				       u8 addr[AVC_BRIDGECO_ADDR_BYTES],
				       unsigned int id, u8 *type);
int avc_bridgeco_get_plug_input(struct fw_unit *unit,
				u8 addr[AVC_BRIDGECO_ADDR_BYTES],
				u8 input[7]);
int avc_bridgeco_get_plug_strm_fmt(struct fw_unit *unit,
				   u8 addr[AVC_BRIDGECO_ADDR_BYTES], u8 *buf,
				   unsigned int *len, unsigned int eid);

/* for AMDTP streaming */
int snd_bebob_stream_get_rate(struct snd_bebob *bebob, unsigned int *rate);
int snd_bebob_stream_set_rate(struct snd_bebob *bebob, unsigned int rate);
int snd_bebob_stream_get_clock_src(struct snd_bebob *bebob,
				   enum snd_bebob_clock_type *src);
int snd_bebob_stream_discover(struct snd_bebob *bebob);
int snd_bebob_stream_init_duplex(struct snd_bebob *bebob);
int snd_bebob_stream_start_duplex(struct snd_bebob *bebob, unsigned int rate);
void snd_bebob_stream_stop_duplex(struct snd_bebob *bebob);
void snd_bebob_stream_destroy_duplex(struct snd_bebob *bebob);

void snd_bebob_stream_lock_changed(struct snd_bebob *bebob);
int snd_bebob_stream_lock_try(struct snd_bebob *bebob);
void snd_bebob_stream_lock_release(struct snd_bebob *bebob);

void snd_bebob_proc_init(struct snd_bebob *bebob);

int snd_bebob_create_midi_devices(struct snd_bebob *bebob);

int snd_bebob_create_pcm_devices(struct snd_bebob *bebob);

int snd_bebob_create_hwdep_device(struct snd_bebob *bebob);

/* model specific operations */
extern const struct snd_bebob_spec phase88_rack_spec;
extern const struct snd_bebob_spec yamaha_terratec_spec;
extern const struct snd_bebob_spec saffirepro_26_spec;
extern const struct snd_bebob_spec saffirepro_10_spec;
extern const struct snd_bebob_spec saffire_le_spec;
extern const struct snd_bebob_spec saffire_spec;
extern const struct snd_bebob_spec maudio_fw410_spec;
extern const struct snd_bebob_spec maudio_audiophile_spec;
extern const struct snd_bebob_spec maudio_solo_spec;
extern const struct snd_bebob_spec maudio_ozonic_spec;
extern const struct snd_bebob_spec maudio_nrv10_spec;
extern const struct snd_bebob_spec maudio_special_spec;
int snd_bebob_maudio_special_discover(struct snd_bebob *bebob, bool is1814);
int snd_bebob_maudio_load_firmware(struct fw_unit *unit);

#define SND_BEBOB_DEV_ENTRY(vendor, model, data) \
{ \
	.match_flags	= IEEE1394_MATCH_VENDOR_ID | \
			  IEEE1394_MATCH_MODEL_ID, \
	.vendor_id	= vendor, \
	.model_id	= model, \
	.driver_data	= (kernel_ulong_t)data \
}

#endif
