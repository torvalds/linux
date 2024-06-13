/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __SOUND_SEQ_OSS_H
#define __SOUND_SEQ_OSS_H

/*
 * OSS compatible sequencer driver
 *
 * Copyright (C) 1998,99 Takashi Iwai
 */

#include <sound/asequencer.h>
#include <sound/seq_kernel.h>

/*
 * argument structure for synthesizer operations
 */
struct snd_seq_oss_arg {
	/* given by OSS sequencer */
	int app_index;	/* application unique index */
	int file_mode;	/* file mode - see below */
	int seq_mode;	/* sequencer mode - see below */

	/* following must be initialized in open callback */
	struct snd_seq_addr addr;	/* opened port address */
	void *private_data;	/* private data for lowlevel drivers */

	/* note-on event passing mode: initially given by OSS seq,
	 * but configurable by drivers - see below
	 */
	int event_passing;
};


/*
 * synthesizer operation callbacks
 */
struct snd_seq_oss_callback {
	struct module *owner;
	int (*open)(struct snd_seq_oss_arg *p, void *closure);
	int (*close)(struct snd_seq_oss_arg *p);
	int (*ioctl)(struct snd_seq_oss_arg *p, unsigned int cmd, unsigned long arg);
	int (*load_patch)(struct snd_seq_oss_arg *p, int format, const char __user *buf, int offs, int count);
	int (*reset)(struct snd_seq_oss_arg *p);
	int (*raw_event)(struct snd_seq_oss_arg *p, unsigned char *data);
};

/* flag: file_mode */
#define SNDRV_SEQ_OSS_FILE_ACMODE		3
#define SNDRV_SEQ_OSS_FILE_READ		1
#define SNDRV_SEQ_OSS_FILE_WRITE		2
#define SNDRV_SEQ_OSS_FILE_NONBLOCK	4

/* flag: seq_mode */
#define SNDRV_SEQ_OSS_MODE_SYNTH		0
#define SNDRV_SEQ_OSS_MODE_MUSIC		1

/* flag: event_passing */
#define SNDRV_SEQ_OSS_PROCESS_EVENTS	0	/* key == 255 is processed as velocity change */
#define SNDRV_SEQ_OSS_PASS_EVENTS		1	/* pass all events to callback */
#define SNDRV_SEQ_OSS_PROCESS_KEYPRESS	2	/* key >= 128 will be processed as key-pressure */

/* default control rate: fixed */
#define SNDRV_SEQ_OSS_CTRLRATE		100

/* default max queue length: configurable by module option */
#define SNDRV_SEQ_OSS_MAX_QLEN		1024


/*
 * data pointer to snd_seq_register_device
 */
struct snd_seq_oss_reg {
	int type;
	int subtype;
	int nvoices;
	struct snd_seq_oss_callback oper;
	void *private_data;
};

/* device id */
#define SNDRV_SEQ_DEV_ID_OSS		"seq-oss"

#endif /* __SOUND_SEQ_OSS_H */
