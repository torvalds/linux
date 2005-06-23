#ifndef __SOUND_HDSPM_H		/* -*- linux-c -*- */
#define __SOUND_HDSPM_H
/*
 *   Copyright (C) 2003 Winfried Ritsch (IEM)
 *   based on hdsp.h from Thomas Charbonnel (thomas@undata.org)
 *                      
 *    
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* Maximum channels is 64 even on 56Mode you have 64playbacks to matrix */
#define HDSPM_MAX_CHANNELS      64

/* -------------------- IOCTL Peak/RMS Meters -------------------- */

typedef struct _snd_hdspm_peak_rms hdspm_peak_rms_t;

/* peam rms level structure like we get from hardware 
  
   maybe in future we can memory map it so I just copy it
   to user on ioctl call now an dont change anything
   rms are made out of low and high values
   where (long) ????_rms = (????_rms_l >> 8) + ((????_rms_h & 0xFFFFFF00)<<24)
   (i asume so from the code)
*/

struct _snd_hdspm_peak_rms {

	unsigned int level_offset[1024];

	unsigned int input_peak[64];
	unsigned int playback_peak[64];
	unsigned int output_peak[64];
	unsigned int xxx_peak[64];	/* not used */

	unsigned int reserved[256];	/* not used */

	unsigned int input_rms_l[64];
	unsigned int playback_rms_l[64];
	unsigned int output_rms_l[64];
	unsigned int xxx_rms_l[64];	/* not used */

	unsigned int input_rms_h[64];
	unsigned int playback_rms_h[64];
	unsigned int output_rms_h[64];
	unsigned int xxx_rms_h[64];	/* not used */
};

struct sndrv_hdspm_peak_rms_ioctl {
	hdspm_peak_rms_t *peak;
};

/* use indirect access due to the limit of ioctl bit size */
#define SNDRV_HDSPM_IOCTL_GET_PEAK_RMS _IOR('H', 0x40, struct sndrv_hdspm_peak_rms_ioctl)

/* ------------ CONFIG block IOCTL ---------------------- */

typedef struct _snd_hdspm_config_info hdspm_config_info_t;

struct _snd_hdspm_config_info {
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

#define SNDRV_HDSPM_IOCTL_GET_CONFIG_INFO _IOR('H', 0x41, hdspm_config_info_t)


/* get Soundcard Version */

typedef struct _snd_hdspm_version hdspm_version_t;

struct _snd_hdspm_version {
	unsigned short firmware_rev;
};

#define SNDRV_HDSPM_IOCTL_GET_VERSION _IOR('H', 0x43, hdspm_version_t)


/* ------------- get Matrix Mixer IOCTL --------------- */

/* MADI mixer: 64inputs+64playback in 64outputs = 8192 => *4Byte = 32768 Bytes */

/* organisation is 64 channelfader in a continous memory block */
/* equivalent to hardware definition, maybe for future feature of mmap of them */
/* each of 64 outputs has 64 infader and 64 outfader: 
   Ins to Outs mixer[out].in[in], Outstreams to Outs mixer[out].pb[pb] */

#define HDSPM_MIXER_CHANNELS HDSPM_MAX_CHANNELS

typedef struct _snd_hdspm_channelfader snd_hdspm_channelfader_t;

struct _snd_hdspm_channelfader {
	unsigned int in[HDSPM_MIXER_CHANNELS];
	unsigned int pb[HDSPM_MIXER_CHANNELS];
};

typedef struct _snd_hdspm_mixer hdspm_mixer_t;

struct _snd_hdspm_mixer {
	snd_hdspm_channelfader_t ch[HDSPM_MIXER_CHANNELS];
};

struct sndrv_hdspm_mixer_ioctl {
	hdspm_mixer_t *mixer;
};

/* use indirect access due to the limit of ioctl bit size */
#define SNDRV_HDSPM_IOCTL_GET_MIXER _IOR('H', 0x44, struct sndrv_hdspm_mixer_ioctl)

#endif				/* __SOUND_HDSPM_H */
