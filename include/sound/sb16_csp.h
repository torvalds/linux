#ifndef __SOUND_SB16_CSP_H
#define __SOUND_SB16_CSP_H

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

/* CSP modes */
#define SNDRV_SB_CSP_MODE_NONE		0x00
#define SNDRV_SB_CSP_MODE_DSP_READ	0x01	/* Record from DSP */
#define SNDRV_SB_CSP_MODE_DSP_WRITE	0x02	/* Play to DSP */
#define SNDRV_SB_CSP_MODE_QSOUND		0x04	/* QSound */

/* CSP load flags */
#define SNDRV_SB_CSP_LOAD_FROMUSER	0x01
#define SNDRV_SB_CSP_LOAD_INITBLOCK	0x02

/* CSP sample width */
#define SNDRV_SB_CSP_SAMPLE_8BIT		0x01
#define SNDRV_SB_CSP_SAMPLE_16BIT		0x02

/* CSP channels */
#define SNDRV_SB_CSP_MONO			0x01
#define SNDRV_SB_CSP_STEREO		0x02

/* CSP rates */
#define SNDRV_SB_CSP_RATE_8000		0x01
#define SNDRV_SB_CSP_RATE_11025		0x02
#define SNDRV_SB_CSP_RATE_22050		0x04
#define SNDRV_SB_CSP_RATE_44100		0x08
#define SNDRV_SB_CSP_RATE_ALL		0x0f

/* CSP running state */
#define SNDRV_SB_CSP_ST_IDLE		0x00
#define SNDRV_SB_CSP_ST_LOADED		0x01
#define SNDRV_SB_CSP_ST_RUNNING		0x02
#define SNDRV_SB_CSP_ST_PAUSED		0x04
#define SNDRV_SB_CSP_ST_AUTO		0x08
#define SNDRV_SB_CSP_ST_QSOUND		0x10

/* maximum QSound value (180 degrees right) */
#define SNDRV_SB_CSP_QSOUND_MAX_RIGHT	0x20

/* maximum microcode RIFF file size */
#define SNDRV_SB_CSP_MAX_MICROCODE_FILE_SIZE	0x3000

/* microcode header */
struct snd_sb_csp_mc_header {
	char codec_name[16];		/* id name of codec */
	unsigned short func_req;	/* requested function */
};

/* microcode to be loaded */
struct snd_sb_csp_microcode {
	struct snd_sb_csp_mc_header info;
	unsigned char data[SNDRV_SB_CSP_MAX_MICROCODE_FILE_SIZE];
};

/* start CSP with sample_width in mono/stereo */
struct snd_sb_csp_start {
	int sample_width;	/* sample width, look above */
	int channels;		/* channels, look above */
};

/* CSP information */
struct snd_sb_csp_info {
	char codec_name[16];		/* id name of codec */
	unsigned short func_nr;		/* function number */
	unsigned int acc_format;	/* accepted PCM formats */
	unsigned short acc_channels;	/* accepted channels */
	unsigned short acc_width;	/* accepted sample width */
	unsigned short acc_rates;	/* accepted sample rates */
	unsigned short csp_mode;	/* CSP mode, see above */
	unsigned short run_channels;	/* current channels  */
	unsigned short run_width;	/* current sample width */
	unsigned short version;		/* version id: 0x10 - 0x1f */
	unsigned short state;		/* state bits */
};

/* HWDEP controls */
/* get CSP information */
#define SNDRV_SB_CSP_IOCTL_INFO		_IOR('H', 0x10, struct snd_sb_csp_info)
/* load microcode to CSP */
#define SNDRV_SB_CSP_IOCTL_LOAD_CODE	_IOW('H', 0x11, struct snd_sb_csp_microcode)
/* unload microcode from CSP */
#define SNDRV_SB_CSP_IOCTL_UNLOAD_CODE	_IO('H', 0x12)
/* start CSP */
#define SNDRV_SB_CSP_IOCTL_START		_IOW('H', 0x13, struct snd_sb_csp_start)
/* stop CSP */
#define SNDRV_SB_CSP_IOCTL_STOP		_IO('H', 0x14)
/* pause CSP and DMA transfer */
#define SNDRV_SB_CSP_IOCTL_PAUSE		_IO('H', 0x15)
/* restart CSP and DMA transfer */
#define SNDRV_SB_CSP_IOCTL_RESTART	_IO('H', 0x16)

#ifdef __KERNEL__
#include "sb.h"
#include "hwdep.h"
#include <linux/firmware.h>

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
#endif

#endif /* __SOUND_SB16_CSP */
