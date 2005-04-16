#ifndef __SOUND_RAWMIDI_H
#define __SOUND_RAWMIDI_H

/*
 *  Abstract layer for MIDI v1.0 stream
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
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
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include <sound/asound.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <asm/semaphore.h>

#if defined(CONFIG_SND_SEQUENCER) || defined(CONFIG_SND_SEQUENCER_MODULE)
#include "seq_device.h"
#endif

/*
 *  Raw MIDI interface
 */

typedef enum sndrv_rawmidi_stream snd_rawmidi_stream_t;
typedef struct sndrv_rawmidi_info snd_rawmidi_info_t;
typedef struct sndrv_rawmidi_params snd_rawmidi_params_t;
typedef struct sndrv_rawmidi_status snd_rawmidi_status_t;

#define SNDRV_RAWMIDI_DEVICES		8

#define SNDRV_RAWMIDI_LFLG_OUTPUT	(1<<0)
#define SNDRV_RAWMIDI_LFLG_INPUT	(1<<1)
#define SNDRV_RAWMIDI_LFLG_OPEN		(3<<0)
#define SNDRV_RAWMIDI_LFLG_APPEND	(1<<2)
#define	SNDRV_RAWMIDI_LFLG_NOOPENLOCK	(1<<3)

typedef struct _snd_rawmidi_runtime snd_rawmidi_runtime_t;
typedef struct _snd_rawmidi_substream snd_rawmidi_substream_t;
typedef struct _snd_rawmidi_str snd_rawmidi_str_t;

typedef struct _snd_rawmidi_ops {
	int (*open) (snd_rawmidi_substream_t * substream);
	int (*close) (snd_rawmidi_substream_t * substream);
	void (*trigger) (snd_rawmidi_substream_t * substream, int up);
	void (*drain) (snd_rawmidi_substream_t * substream);
} snd_rawmidi_ops_t;

typedef struct _snd_rawmidi_global_ops {
	int (*dev_register) (snd_rawmidi_t * rmidi);
	int (*dev_unregister) (snd_rawmidi_t * rmidi);
} snd_rawmidi_global_ops_t;

struct _snd_rawmidi_runtime {
	unsigned int drain: 1,	/* drain stage */
		     oss: 1;	/* OSS compatible mode */
	/* midi stream buffer */
	unsigned char *buffer;	/* buffer for MIDI data */
	size_t buffer_size;	/* size of buffer */
	size_t appl_ptr;	/* application pointer */
	size_t hw_ptr;		/* hardware pointer */
	size_t avail_min;	/* min avail for wakeup */
	size_t avail;		/* max used buffer for wakeup */
	size_t xruns;		/* over/underruns counter */
	/* misc */
	spinlock_t lock;
	wait_queue_head_t sleep;
	/* event handler (new bytes, input only) */
	void (*event)(snd_rawmidi_substream_t *substream);
	/* defers calls to event [input] or ops->trigger [output] */
	struct tasklet_struct tasklet;
	/* private data */
	void *private_data;
	void (*private_free)(snd_rawmidi_substream_t *substream);
};

struct _snd_rawmidi_substream {
	struct list_head list;		/* list of all substream for given stream */
	int stream;			/* direction */
	int number;			/* substream number */
	unsigned int opened: 1,		/* open flag */
		     append: 1,		/* append flag (merge more streams) */
		     active_sensing: 1; /* send active sensing when close */
	int use_count;			/* use counter (for output) */
	size_t bytes;
	snd_rawmidi_t *rmidi;
	snd_rawmidi_str_t *pstr;
	char name[32];
	snd_rawmidi_runtime_t *runtime;
	/* hardware layer */
	snd_rawmidi_ops_t *ops;
};

typedef struct _snd_rawmidi_file {
	snd_rawmidi_t *rmidi;
	snd_rawmidi_substream_t *input;
	snd_rawmidi_substream_t *output;
} snd_rawmidi_file_t;

struct _snd_rawmidi_str {
	unsigned int substream_count;
	unsigned int substream_opened;
	struct list_head substreams;
};

struct _snd_rawmidi {
	snd_card_t *card;

	unsigned int device;		/* device number */
	unsigned int info_flags;	/* SNDRV_RAWMIDI_INFO_XXXX */
	char id[64];
	char name[80];

#ifdef CONFIG_SND_OSSEMUL
	int ossreg;
#endif

	snd_rawmidi_global_ops_t *ops;

	snd_rawmidi_str_t streams[2];

	void *private_data;
	void (*private_free) (snd_rawmidi_t *rmidi);

	struct semaphore open_mutex;
	wait_queue_head_t open_wait;

	snd_info_entry_t *dev;
	snd_info_entry_t *proc_entry;

#if defined(CONFIG_SND_SEQUENCER) || defined(CONFIG_SND_SEQUENCER_MODULE)
	snd_seq_device_t *seq_dev;
#endif
};

/* main rawmidi functions */

int snd_rawmidi_new(snd_card_t * card, char *id, int device,
		    int output_count, int input_count,
		    snd_rawmidi_t ** rmidi);
void snd_rawmidi_set_ops(snd_rawmidi_t * rmidi, int stream, snd_rawmidi_ops_t * ops);

/* callbacks */

void snd_rawmidi_receive_reset(snd_rawmidi_substream_t * substream);
int snd_rawmidi_receive(snd_rawmidi_substream_t * substream, const unsigned char *buffer, int count);
void snd_rawmidi_transmit_reset(snd_rawmidi_substream_t * substream);
int snd_rawmidi_transmit_empty(snd_rawmidi_substream_t * substream);
int snd_rawmidi_transmit_peek(snd_rawmidi_substream_t * substream, unsigned char *buffer, int count);
int snd_rawmidi_transmit_ack(snd_rawmidi_substream_t * substream, int count);
int snd_rawmidi_transmit(snd_rawmidi_substream_t * substream, unsigned char *buffer, int count);

/* main midi functions */

int snd_rawmidi_info_select(snd_card_t *card, snd_rawmidi_info_t *info);
int snd_rawmidi_kernel_open(int cardnum, int device, int subdevice, int mode, snd_rawmidi_file_t * rfile);
int snd_rawmidi_kernel_release(snd_rawmidi_file_t * rfile);
int snd_rawmidi_output_params(snd_rawmidi_substream_t * substream, snd_rawmidi_params_t * params);
int snd_rawmidi_input_params(snd_rawmidi_substream_t * substream, snd_rawmidi_params_t * params);
int snd_rawmidi_drop_output(snd_rawmidi_substream_t * substream);
int snd_rawmidi_drain_output(snd_rawmidi_substream_t * substream);
int snd_rawmidi_drain_input(snd_rawmidi_substream_t * substream);
long snd_rawmidi_kernel_read(snd_rawmidi_substream_t * substream, unsigned char *buf, long count);
long snd_rawmidi_kernel_write(snd_rawmidi_substream_t * substream, const unsigned char *buf, long count);

#endif /* __SOUND_RAWMIDI_H */
