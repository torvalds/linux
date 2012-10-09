/*
 *  Copyright (c) 1999 by Uros Bizjak <uros@kss-loka.si>
 *                        Takashi Iwai <tiwai@suse.de>
 *
 *  SB16ASP/AWE32 CSP control
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
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */
#ifndef __SOUND_SB16_CSP_H
#define __SOUND_SB16_CSP_H

#include <sound/sb.h>
#include <sound/hwdep.h>
#include <linux/firmware.h>
#include <uapi/sound/sb16_csp.h>

struct snd_sb_csp;

/* indices for the known CSP programs */
enum {
	CSP_PROGRAM_MULAW,
	CSP_PROGRAM_ALAW,
	CSP_PROGRAM_ADPCM_INIT,
	CSP_PROGRAM_ADPCM_PLAYBACK,
	CSP_PROGRAM_ADPCM_CAPTURE,

	CSP_PROGRAM_COUNT
};

/*
 * CSP operators
 */
struct snd_sb_csp_ops {
	int (*csp_use) (struct snd_sb_csp * p);
	int (*csp_unuse) (struct snd_sb_csp * p);
	int (*csp_autoload) (struct snd_sb_csp * p, int pcm_sfmt, int play_rec_mode);
	int (*csp_start) (struct snd_sb_csp * p, int sample_width, int channels);
	int (*csp_stop) (struct snd_sb_csp * p);
	int (*csp_qsound_transfer) (struct snd_sb_csp * p);
};

/*
 * CSP private data
 */
struct snd_sb_csp {
	struct snd_sb *chip;		/* SB16 DSP */
	int used;		/* usage flag - exclusive */
	char codec_name[16];	/* name of codec */
	unsigned short func_nr;	/* function number */
	unsigned int acc_format;	/* accepted PCM formats */
	int acc_channels;	/* accepted channels */
	int acc_width;		/* accepted sample width */
	int acc_rates;		/* accepted sample rates */
	int mode;		/* MODE */
	int run_channels;	/* current CSP channels */
	int run_width;		/* current sample width */
	int version;		/* CSP version (0x10 - 0x1f) */
	int running;		/* running state */

	struct snd_sb_csp_ops ops;	/* operators */

	spinlock_t q_lock;	/* locking */
	int q_enabled;		/* enabled flag */
	int qpos_left;		/* left position */
	int qpos_right;		/* right position */
	int qpos_changed;	/* position changed flag */

	struct snd_kcontrol *qsound_switch;
	struct snd_kcontrol *qsound_space;

	struct mutex access_mutex;	/* locking */

	const struct firmware *csp_programs[CSP_PROGRAM_COUNT];
};

int snd_sb_csp_new(struct snd_sb *chip, int device, struct snd_hwdep ** rhwdep);
#endif /* __SOUND_SB16_CSP */
