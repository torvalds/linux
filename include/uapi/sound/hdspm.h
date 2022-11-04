/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
#ifndef __SOUND_HDSPM_H
#define __SOUND_HDSPM_H
/*
 *   Copyright (C) 2003 Winfried Ritsch (IEM)
 *   based on hdsp.h from Thomas Charbonnel (thomas@undata.org)
 */

#ifdef __linux__
#include <linux/types.h>
#endif

/* Maximum channels is 64 even on 56Mode you have 64playbacks to matrix */
#define HDSPM_MAX_CHANNELS      64

enum hdspm_io_type {
	MADI,
	MADIface,
	AIO,
	AES32,
	RayDAT
};

enum hdspm_speed {
	ss,
	ds,
	qs
};

/* -------------------- IOCTL Peak/RMS Meters -------------------- */

struct hdspm_peak_rms {
	__u32 input_peaks[64];
	__u32 playback_peaks[64];
	__u32 output_peaks[64];

	__u64 input_rms[64];
	__u64 playback_rms[64];
	__u64 output_rms[64];

	__u8 speed; /* enum {ss, ds, qs} */
	int status2;
};

#define SNDRV_HDSPM_IOCTL_GET_PEAK_RMS \
	_IOR('H', 0x42, struct hdspm_peak_rms)

/* ------------ CONFIG block IOCTL ---------------------- */

struct hdspm_config {
	unsigned char pref_sync_ref;
	unsigned char wordclock_sync_check;
	unsigned char madi_sync_check;
	unsigned int system_sample_rate;
	unsigned int autosync_sample_rate;
	unsigned char system_clock_mode;
	unsigned char clock_source;
	unsigned char autosync_ref;
	unsigned char line_out;
	unsigned int passthru;
	unsigned int analog_out;
};

#define SNDRV_HDSPM_IOCTL_GET_CONFIG \
	_IOR('H', 0x41, struct hdspm_config)

/*
 * If there's a TCO (TimeCode Option) board installed,
 * there are further options and status data available.
 * The hdspm_ltc structure contains the current SMPTE
 * timecode and some status information and can be
 * obtained via SNDRV_HDSPM_IOCTL_GET_LTC or in the
 * hdspm_status struct.
 */

enum hdspm_ltc_format {
	format_invalid,
	fps_24,
	fps_25,
	fps_2997,
	fps_30
};

enum hdspm_ltc_frame {
	frame_invalid,
	drop_frame,
	full_frame
};

enum hdspm_ltc_input_format {
	ntsc,
	pal,
	no_video
};

struct hdspm_ltc {
	unsigned int ltc;

	enum hdspm_ltc_format format;
	enum hdspm_ltc_frame frame;
	enum hdspm_ltc_input_format input_format;
};

#define SNDRV_HDSPM_IOCTL_GET_LTC _IOR('H', 0x46, struct hdspm_ltc)

/*
 * The status data reflects the device's current state
 * as determined by the card's configuration and
 * connection status.
 */

enum hdspm_sync {
	hdspm_sync_no_lock = 0,
	hdspm_sync_lock = 1,
	hdspm_sync_sync = 2
};

enum hdspm_madi_input {
	hdspm_input_optical = 0,
	hdspm_input_coax = 1
};

enum hdspm_madi_channel_format {
	hdspm_format_ch_64 = 0,
	hdspm_format_ch_56 = 1
};

enum hdspm_madi_frame_format {
	hdspm_frame_48 = 0,
	hdspm_frame_96 = 1
};

enum hdspm_syncsource {
	syncsource_wc = 0,
	syncsource_madi = 1,
	syncsource_tco = 2,
	syncsource_sync = 3,
	syncsource_none = 4
};

struct hdspm_status {
	__u8 card_type; /* enum hdspm_io_type */
	enum hdspm_syncsource autosync_source;

	__u64 card_clock;
	__u32 master_period;

	union {
		struct {
			__u8 sync_wc; /* enum hdspm_sync */
			__u8 sync_madi; /* enum hdspm_sync */
			__u8 sync_tco; /* enum hdspm_sync */
			__u8 sync_in; /* enum hdspm_sync */
			__u8 madi_input; /* enum hdspm_madi_input */
			__u8 channel_format; /* enum hdspm_madi_channel_format */
			__u8 frame_format; /* enum hdspm_madi_frame_format */
		} madi;
	} card_specific;
};

#define SNDRV_HDSPM_IOCTL_GET_STATUS \
	_IOR('H', 0x47, struct hdspm_status)

/*
 * Get information about the card and its add-ons.
 */

#define HDSPM_ADDON_TCO 1

struct hdspm_version {
	__u8 card_type; /* enum hdspm_io_type */
	char cardname[20];
	unsigned int serial;
	unsigned short firmware_rev;
	int addons;
};

#define SNDRV_HDSPM_IOCTL_GET_VERSION _IOR('H', 0x48, struct hdspm_version)

/* ------------- get Matrix Mixer IOCTL --------------- */

/* MADI mixer: 64inputs+64playback in 64outputs = 8192 => *4Byte =
 * 32768 Bytes
 */

/* organisation is 64 channelfader in a continuous memory block */
/* equivalent to hardware definition, maybe for future feature of mmap of
 * them
 */
/* each of 64 outputs has 64 infader and 64 outfader:
   Ins to Outs mixer[out].in[in], Outstreams to Outs mixer[out].pb[pb] */

#define HDSPM_MIXER_CHANNELS HDSPM_MAX_CHANNELS

struct hdspm_channelfader {
	unsigned int in[HDSPM_MIXER_CHANNELS];
	unsigned int pb[HDSPM_MIXER_CHANNELS];
};

struct hdspm_mixer {
	struct hdspm_channelfader ch[HDSPM_MIXER_CHANNELS];
};

struct hdspm_mixer_ioctl {
	struct hdspm_mixer *mixer;
};

/* use indirect access due to the limit of ioctl bit size */
#define SNDRV_HDSPM_IOCTL_GET_MIXER _IOR('H', 0x44, struct hdspm_mixer_ioctl)

#endif
