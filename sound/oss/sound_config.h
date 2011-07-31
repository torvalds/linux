/* sound_config.h
 *
 * A driver for sound cards, misc. configuration parameters.
 */
/*
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 * OSS/Free for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 */


#ifndef  _SOUND_CONFIG_H_
#define  _SOUND_CONFIG_H_

#include <linux/fs.h>
#include <linux/sound.h>

#include "os.h"
#include "soundvers.h"


#ifndef SND_DEFAULT_ENABLE
#define SND_DEFAULT_ENABLE	1
#endif

#ifndef MAX_REALTIME_FACTOR
#define MAX_REALTIME_FACTOR	4
#endif

/*
 * Use always 64k buffer size. There is no reason to use shorter.
 */
#undef DSP_BUFFSIZE
#define DSP_BUFFSIZE		(64*1024)

#ifndef DSP_BUFFCOUNT
#define DSP_BUFFCOUNT		1	/* 1 is recommended. */
#endif

#define FM_MONO		0x388	/* This is the I/O address used by AdLib */

#ifndef CONFIG_PAS_BASE
#define CONFIG_PAS_BASE	0x388
#endif

/* SEQ_MAX_QUEUE is the maximum number of sequencer events buffered by the
   driver. (There is no need to alter this) */
#define SEQ_MAX_QUEUE	1024

#define SBFM_MAXINSTR		(256)	/* Size of the FM Instrument bank */
/* 128 instruments for general MIDI setup and 16 unassigned	 */

#define SND_NDEVS	256	/* Number of supported devices */

#define DSP_DEFAULT_SPEED	8000

#define MAX_AUDIO_DEV	5
#define MAX_MIXER_DEV	5
#define MAX_SYNTH_DEV	5
#define MAX_MIDI_DEV	6
#define MAX_TIMER_DEV	4

struct address_info {
	int io_base;
	int irq;
	int dma;
	int dma2;
	int always_detect;	/* 1=Trust me, it's there */
	char *name;
	int driver_use_1;	/* Driver defined field 1 */
	int driver_use_2;	/* Driver defined field 2 */
	int *osp;	/* OS specific info */
	int card_subtype;	/* Driver specific. Usually 0 */
	void *memptr;           /* Module memory chainer */
	int slots[6];           /* To remember driver slot ids */
};

#define SYNTH_MAX_VOICES	32

struct voice_alloc_info {
		int max_voice;
		int used_voices;
		int ptr;		/* For device specific use */
		unsigned short map[SYNTH_MAX_VOICES]; /* (ch << 8) | (note+1) */
		int timestamp;
		int alloc_times[SYNTH_MAX_VOICES];
	};

struct channel_info {
		int pgm_num;
		int bender_value;
		int bender_range;
		unsigned char controllers[128];
	};

/*
 * Process wakeup reasons
 */
#define WK_NONE		0x00
#define WK_WAKEUP	0x01
#define WK_TIMEOUT	0x02
#define WK_SIGNAL	0x04
#define WK_SLEEP	0x08
#define WK_SELECT	0x10
#define WK_ABORT	0x20

#define OPEN_READ	PCM_ENABLE_INPUT
#define OPEN_WRITE	PCM_ENABLE_OUTPUT
#define OPEN_READWRITE	(OPEN_READ|OPEN_WRITE)

static inline int translate_mode(struct file *file)
{
	if (OPEN_READ == (__force int)FMODE_READ &&
	    OPEN_WRITE == (__force int)FMODE_WRITE)
		return (__force int)(file->f_mode & (FMODE_READ | FMODE_WRITE));
	else
		return ((file->f_mode & FMODE_READ) ? OPEN_READ : 0) |
			((file->f_mode & FMODE_WRITE) ? OPEN_WRITE : 0);
}

#include "sound_calls.h"
#include "dev_table.h"

#ifndef DEB
#define DEB(x)
#endif

#ifndef DDB
#define DDB(x) do {} while (0)
#endif

#ifndef MDB
#ifdef MODULE
#define MDB(x) x
#else
#define MDB(x)
#endif
#endif

#define TIMER_ARMED	121234
#define TIMER_NOT_ARMED	1

#define MAX_MEM_BLOCKS 1024

#endif
