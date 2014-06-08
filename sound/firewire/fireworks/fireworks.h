/*
 * fireworks.h - a part of driver for Fireworks based devices
 *
 * Copyright (c) 2009-2010 Clemens Ladisch
 * Copyright (c) 2013-2014 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */
#ifndef SOUND_FIREWORKS_H_INCLUDED
#define SOUND_FIREWORKS_H_INCLUDED

#include <linux/compat.h>
#include <linux/device.h>
#include <linux/firewire.h>
#include <linux/firewire-constants.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/delay.h>
#include <linux/slab.h>

#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/info.h>
#include <sound/rawmidi.h>
#include <sound/pcm_params.h>
#include <sound/firewire.h>
#include <sound/hwdep.h>

#include "../packets-buffer.h"
#include "../iso-resources.h"
#include "../amdtp.h"
#include "../cmp.h"
#include "../lib.h"

#define SND_EFW_MAX_MIDI_OUT_PORTS	2
#define SND_EFW_MAX_MIDI_IN_PORTS	2

#define SND_EFW_MULTIPLIER_MODES	3
#define HWINFO_NAME_SIZE_BYTES		32
#define HWINFO_MAX_CAPS_GROUPS		8

/*
 * This should be greater than maximum bytes for EFW response content.
 * Currently response against command for isochronous channel mapping is
 * confirmed to be the maximum one. But for flexibility, use maximum data
 * payload for asynchronous primary packets at S100 (Cable base rate) in
 * IEEE Std 1394-1995.
 */
#define SND_EFW_RESPONSE_MAXIMUM_BYTES	0x200U

extern unsigned int snd_efw_resp_buf_size;
extern bool snd_efw_resp_buf_debug;

struct snd_efw_phys_grp {
	u8 type;	/* see enum snd_efw_grp_type */
	u8 count;
} __packed;

struct snd_efw {
	struct snd_card *card;
	struct fw_unit *unit;
	int card_index;

	struct mutex mutex;
	spinlock_t lock;

	/* for transaction */
	u32 seqnum;
	bool resp_addr_changable;

	/* for quirks */
	bool is_af9;
	u32 firmware_version;

	unsigned int midi_in_ports;
	unsigned int midi_out_ports;

	unsigned int supported_sampling_rate;
	unsigned int pcm_capture_channels[SND_EFW_MULTIPLIER_MODES];
	unsigned int pcm_playback_channels[SND_EFW_MULTIPLIER_MODES];

	struct amdtp_stream *master;
	struct amdtp_stream tx_stream;
	struct amdtp_stream rx_stream;
	struct cmp_connection out_conn;
	struct cmp_connection in_conn;
	atomic_t capture_substreams;
	atomic_t playback_substreams;

	/* hardware metering parameters */
	unsigned int phys_out;
	unsigned int phys_in;
	unsigned int phys_out_grp_count;
	unsigned int phys_in_grp_count;
	struct snd_efw_phys_grp phys_out_grps[HWINFO_MAX_CAPS_GROUPS];
	struct snd_efw_phys_grp phys_in_grps[HWINFO_MAX_CAPS_GROUPS];

	/* for uapi */
	int dev_lock_count;
	bool dev_lock_changed;
	wait_queue_head_t hwdep_wait;

	/* response queue */
	u8 *resp_buf;
	u8 *pull_ptr;
	u8 *push_ptr;
	unsigned int resp_queues;
};

int snd_efw_transaction_cmd(struct fw_unit *unit,
			    const void *cmd, unsigned int size);
int snd_efw_transaction_run(struct fw_unit *unit,
			    const void *cmd, unsigned int cmd_size,
			    void *resp, unsigned int resp_size);
int snd_efw_transaction_register(void);
void snd_efw_transaction_unregister(void);
void snd_efw_transaction_bus_reset(struct fw_unit *unit);
void snd_efw_transaction_add_instance(struct snd_efw *efw);
void snd_efw_transaction_remove_instance(struct snd_efw *efw);

struct snd_efw_hwinfo {
	u32 flags;
	u32 guid_hi;
	u32 guid_lo;
	u32 type;
	u32 version;
	char vendor_name[HWINFO_NAME_SIZE_BYTES];
	char model_name[HWINFO_NAME_SIZE_BYTES];
	u32 supported_clocks;
	u32 amdtp_rx_pcm_channels;
	u32 amdtp_tx_pcm_channels;
	u32 phys_out;
	u32 phys_in;
	u32 phys_out_grp_count;
	struct snd_efw_phys_grp phys_out_grps[HWINFO_MAX_CAPS_GROUPS];
	u32 phys_in_grp_count;
	struct snd_efw_phys_grp phys_in_grps[HWINFO_MAX_CAPS_GROUPS];
	u32 midi_out_ports;
	u32 midi_in_ports;
	u32 max_sample_rate;
	u32 min_sample_rate;
	u32 dsp_version;
	u32 arm_version;
	u32 mixer_playback_channels;
	u32 mixer_capture_channels;
	u32 fpga_version;
	u32 amdtp_rx_pcm_channels_2x;
	u32 amdtp_tx_pcm_channels_2x;
	u32 amdtp_rx_pcm_channels_4x;
	u32 amdtp_tx_pcm_channels_4x;
	u32 reserved[16];
} __packed;
enum snd_efw_grp_type {
	SND_EFW_CH_TYPE_ANALOG			= 0,
	SND_EFW_CH_TYPE_SPDIF			= 1,
	SND_EFW_CH_TYPE_ADAT			= 2,
	SND_EFW_CH_TYPE_SPDIF_OR_ADAT		= 3,
	SND_EFW_CH_TYPE_ANALOG_MIRRORING	= 4,
	SND_EFW_CH_TYPE_HEADPHONES		= 5,
	SND_EFW_CH_TYPE_I2S			= 6,
	SND_EFW_CH_TYPE_GUITAR			= 7,
	SND_EFW_CH_TYPE_PIEZO_GUITAR		= 8,
	SND_EFW_CH_TYPE_GUITAR_STRING		= 9,
	SND_EFW_CH_TYPE_VIRTUAL			= 0x10000,
	SND_EFW_CH_TYPE_DUMMY
};
struct snd_efw_phys_meters {
	u32 status;	/* guitar state/midi signal/clock input detect */
	u32 reserved0;
	u32 reserved1;
	u32 reserved2;
	u32 reserved3;
	u32 out_meters;
	u32 in_meters;
	u32 reserved4;
	u32 reserved5;
	u32 values[0];
} __packed;
enum snd_efw_clock_source {
	SND_EFW_CLOCK_SOURCE_INTERNAL	= 0,
	SND_EFW_CLOCK_SOURCE_SYTMATCH	= 1,
	SND_EFW_CLOCK_SOURCE_WORDCLOCK	= 2,
	SND_EFW_CLOCK_SOURCE_SPDIF	= 3,
	SND_EFW_CLOCK_SOURCE_ADAT_1	= 4,
	SND_EFW_CLOCK_SOURCE_ADAT_2	= 5,
	SND_EFW_CLOCK_SOURCE_CONTINUOUS	= 6	/* internal variable clock */
};
enum snd_efw_transport_mode {
	SND_EFW_TRANSPORT_MODE_WINDOWS	= 0,
	SND_EFW_TRANSPORT_MODE_IEC61883	= 1,
};
int snd_efw_command_set_resp_addr(struct snd_efw *efw,
				  u16 addr_high, u32 addr_low);
int snd_efw_command_set_tx_mode(struct snd_efw *efw,
				enum snd_efw_transport_mode mode);
int snd_efw_command_get_hwinfo(struct snd_efw *efw,
			       struct snd_efw_hwinfo *hwinfo);
int snd_efw_command_get_phys_meters(struct snd_efw *efw,
				    struct snd_efw_phys_meters *meters,
				    unsigned int len);
int snd_efw_command_get_clock_source(struct snd_efw *efw,
				     enum snd_efw_clock_source *source);
int snd_efw_command_get_sampling_rate(struct snd_efw *efw, unsigned int *rate);
int snd_efw_command_set_sampling_rate(struct snd_efw *efw, unsigned int rate);

int snd_efw_stream_init_duplex(struct snd_efw *efw);
int snd_efw_stream_start_duplex(struct snd_efw *efw, unsigned int rate);
void snd_efw_stream_stop_duplex(struct snd_efw *efw);
void snd_efw_stream_update_duplex(struct snd_efw *efw);
void snd_efw_stream_destroy_duplex(struct snd_efw *efw);
void snd_efw_stream_lock_changed(struct snd_efw *efw);
int snd_efw_stream_lock_try(struct snd_efw *efw);
void snd_efw_stream_lock_release(struct snd_efw *efw);

void snd_efw_proc_init(struct snd_efw *efw);

int snd_efw_create_midi_devices(struct snd_efw *efw);

int snd_efw_create_pcm_devices(struct snd_efw *efw);
int snd_efw_get_multiplier_mode(unsigned int sampling_rate, unsigned int *mode);

int snd_efw_create_hwdep_device(struct snd_efw *efw);

#define SND_EFW_DEV_ENTRY(vendor, model) \
{ \
	.match_flags	= IEEE1394_MATCH_VENDOR_ID | \
			  IEEE1394_MATCH_MODEL_ID, \
	.vendor_id	= vendor,\
	.model_id	= model \
}

#endif
