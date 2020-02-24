/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __SOUND_RAWMIDI_H
#define __SOUND_RAWMIDI_H

/*
 *  Abstract layer for MIDI v1.0 stream
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 */

#include <sound/asound.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/device.h>

#if IS_ENABLED(CONFIG_SND_SEQUENCER)
#include <sound/seq_device.h>
#endif

/*
 *  Raw MIDI interface
 */

#define SNDRV_RAWMIDI_DEVICES		8

#define SNDRV_RAWMIDI_LFLG_OUTPUT	(1<<0)
#define SNDRV_RAWMIDI_LFLG_INPUT	(1<<1)
#define SNDRV_RAWMIDI_LFLG_OPEN		(3<<0)
#define SNDRV_RAWMIDI_LFLG_APPEND	(1<<2)

struct snd_rawmidi;
struct snd_rawmidi_substream;
struct snd_seq_port_info;
struct pid;

struct snd_rawmidi_ops {
	int (*open) (struct snd_rawmidi_substream * substream);
	int (*close) (struct snd_rawmidi_substream * substream);
	void (*trigger) (struct snd_rawmidi_substream * substream, int up);
	void (*drain) (struct snd_rawmidi_substream * substream);
};

struct snd_rawmidi_global_ops {
	int (*dev_register) (struct snd_rawmidi * rmidi);
	int (*dev_unregister) (struct snd_rawmidi * rmidi);
	void (*get_port_info)(struct snd_rawmidi *rmidi, int number,
			      struct snd_seq_port_info *info);
};

struct snd_rawmidi_runtime {
	struct snd_rawmidi_substream *substream;
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
	void (*event)(struct snd_rawmidi_substream *substream);
	/* defers calls to event [input] or ops->trigger [output] */
	struct work_struct event_work;
	/* private data */
	void *private_data;
	void (*private_free)(struct snd_rawmidi_substream *substream);
};

struct snd_rawmidi_substream {
	struct list_head list;		/* list of all substream for given stream */
	int stream;			/* direction */
	int number;			/* substream number */
	bool opened;			/* open flag */
	bool append;			/* append flag (merge more streams) */
	bool active_sensing;		/* send active sensing when close */
	int use_count;			/* use counter (for output) */
	size_t bytes;
	struct snd_rawmidi *rmidi;
	struct snd_rawmidi_str *pstr;
	char name[32];
	struct snd_rawmidi_runtime *runtime;
	struct pid *pid;
	/* hardware layer */
	const struct snd_rawmidi_ops *ops;
};

struct snd_rawmidi_file {
	struct snd_rawmidi *rmidi;
	struct snd_rawmidi_substream *input;
	struct snd_rawmidi_substream *output;
};

struct snd_rawmidi_str {
	unsigned int substream_count;
	unsigned int substream_opened;
	struct list_head substreams;
};

struct snd_rawmidi {
	struct snd_card *card;
	struct list_head list;
	unsigned int device;		/* device number */
	unsigned int info_flags;	/* SNDRV_RAWMIDI_INFO_XXXX */
	char id[64];
	char name[80];

#ifdef CONFIG_SND_OSSEMUL
	int ossreg;
#endif

	const struct snd_rawmidi_global_ops *ops;

	struct snd_rawmidi_str streams[2];

	void *private_data;
	void (*private_free) (struct snd_rawmidi *rmidi);

	struct mutex open_mutex;
	wait_queue_head_t open_wait;

	struct device dev;

	struct snd_info_entry *proc_entry;

#if IS_ENABLED(CONFIG_SND_SEQUENCER)
	struct snd_seq_device *seq_dev;
#endif
};

/* main rawmidi functions */

int snd_rawmidi_new(struct snd_card *card, char *id, int device,
		    int output_count, int input_count,
		    struct snd_rawmidi **rmidi);
void snd_rawmidi_set_ops(struct snd_rawmidi *rmidi, int stream,
			 const struct snd_rawmidi_ops *ops);

/* callbacks */

int snd_rawmidi_receive(struct snd_rawmidi_substream *substream,
			const unsigned char *buffer, int count);
int snd_rawmidi_transmit_empty(struct snd_rawmidi_substream *substream);
int snd_rawmidi_transmit_peek(struct snd_rawmidi_substream *substream,
			      unsigned char *buffer, int count);
int snd_rawmidi_transmit_ack(struct snd_rawmidi_substream *substream, int count);
int snd_rawmidi_transmit(struct snd_rawmidi_substream *substream,
			 unsigned char *buffer, int count);
int __snd_rawmidi_transmit_peek(struct snd_rawmidi_substream *substream,
			      unsigned char *buffer, int count);
int __snd_rawmidi_transmit_ack(struct snd_rawmidi_substream *substream,
			       int count);
int snd_rawmidi_proceed(struct snd_rawmidi_substream *substream);

/* main midi functions */

int snd_rawmidi_info_select(struct snd_card *card, struct snd_rawmidi_info *info);
int snd_rawmidi_kernel_open(struct snd_card *card, int device, int subdevice,
			    int mode, struct snd_rawmidi_file *rfile);
int snd_rawmidi_kernel_release(struct snd_rawmidi_file *rfile);
int snd_rawmidi_output_params(struct snd_rawmidi_substream *substream,
			      struct snd_rawmidi_params *params);
int snd_rawmidi_input_params(struct snd_rawmidi_substream *substream,
			     struct snd_rawmidi_params *params);
int snd_rawmidi_drop_output(struct snd_rawmidi_substream *substream);
int snd_rawmidi_drain_output(struct snd_rawmidi_substream *substream);
int snd_rawmidi_drain_input(struct snd_rawmidi_substream *substream);
long snd_rawmidi_kernel_read(struct snd_rawmidi_substream *substream,
			     unsigned char *buf, long count);
long snd_rawmidi_kernel_write(struct snd_rawmidi_substream *substream,
			      const unsigned char *buf, long count);

#endif /* __SOUND_RAWMIDI_H */
