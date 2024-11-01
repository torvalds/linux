/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 *  Copyright (c) 1999 by Uros Bizjak <uros@kss-loka.si>
 *                        Takashi Iwai <tiwai@suse.de>
 *
 *  SB16ASP/AWE32 CSP control
 */
#ifndef _UAPI__SOUND_SB16_CSP_H
#define _UAPI__SOUND_SB16_CSP_H


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
/* NOTE: struct snd_sb_csp_microcode overflows the max size (13 bits)
 * defined for some architectures like MIPS, and it leads to build errors.
 * (x86 and co have 14-bit size, thus it's valid, though.)
 * As a workaround for skipping the size-limit check, here we don't use the
 * normal _IOW() macro but _IOC() with the manual argument.
 */
#define SNDRV_SB_CSP_IOCTL_LOAD_CODE	\
	_IOC(_IOC_WRITE, 'H', 0x11, sizeof(struct snd_sb_csp_microcode))
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


#endif /* _UAPI__SOUND_SB16_CSP_H */
