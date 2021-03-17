/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * OSS compatible sequencer driver
 *
 * Copyright (C) 1998,99 Takashi Iwai <tiwai@suse.de>
 */

#ifndef __SEQ_OSS_DEVICE_H
#define __SEQ_OSS_DEVICE_H

#include <linux/time.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/sched/signal.h>
#include <sound/core.h>
#include <sound/seq_oss.h>
#include <sound/rawmidi.h>
#include <sound/seq_kernel.h>
#include <sound/info.h>
#include "../seq_clientmgr.h"

/* max. applications */
#define SNDRV_SEQ_OSS_MAX_CLIENTS	16
#define SNDRV_SEQ_OSS_MAX_SYNTH_DEVS	16
#define SNDRV_SEQ_OSS_MAX_MIDI_DEVS	32

/* version */
#define SNDRV_SEQ_OSS_MAJOR_VERSION	0
#define SNDRV_SEQ_OSS_MINOR_VERSION	1
#define SNDRV_SEQ_OSS_TINY_VERSION	8
#define SNDRV_SEQ_OSS_VERSION_STR	"0.1.8"

/* device and proc interface name */
#define SNDRV_SEQ_OSS_PROCNAME		"oss"


/*
 * type definitions
 */

typedef unsigned int reltime_t;
typedef unsigned int abstime_t;


/*
 * synthesizer channel information
 */
struct seq_oss_chinfo {
	int note, vel;
};

/*
 * synthesizer information
 */
struct seq_oss_synthinfo {
	struct snd_seq_oss_arg arg;
	struct seq_oss_chinfo *ch;
	struct seq_oss_synth_sysex *sysex;
	int nr_voices;
	int opened;
	int is_midi;
	int midi_mapped;
};


/*
 * sequencer client information
 */

struct seq_oss_devinfo {

	int index;	/* application index */
	int cseq;	/* sequencer client number */
	int port;	/* sequencer port number */
	int queue;	/* sequencer queue number */

	struct snd_seq_addr addr;	/* address of this device */

	int seq_mode;	/* sequencer mode */
	int file_mode;	/* file access */

	/* midi device table */
	int max_mididev;

	/* synth device table */
	int max_synthdev;
	struct seq_oss_synthinfo synths[SNDRV_SEQ_OSS_MAX_SYNTH_DEVS];
	int synth_opened;

	/* output queue */
	struct seq_oss_writeq *writeq;

	/* midi input queue */
	struct seq_oss_readq *readq;

	/* timer */
	struct seq_oss_timer *timer;
};


/*
 * function prototypes
 */

/* create/delete OSS sequencer client */
int snd_seq_oss_create_client(void);
int snd_seq_oss_delete_client(void);

/* device file interface */
int snd_seq_oss_open(struct file *file, int level);
void snd_seq_oss_release(struct seq_oss_devinfo *dp);
int snd_seq_oss_ioctl(struct seq_oss_devinfo *dp, unsigned int cmd, unsigned long arg);
int snd_seq_oss_read(struct seq_oss_devinfo *dev, char __user *buf, int count);
int snd_seq_oss_write(struct seq_oss_devinfo *dp, const char __user *buf, int count, struct file *opt);
__poll_t snd_seq_oss_poll(struct seq_oss_devinfo *dp, struct file *file, poll_table * wait);

void snd_seq_oss_reset(struct seq_oss_devinfo *dp);

/* */
void snd_seq_oss_process_queue(struct seq_oss_devinfo *dp, abstime_t time);


/* proc interface */
void snd_seq_oss_system_info_read(struct snd_info_buffer *buf);
void snd_seq_oss_midi_info_read(struct snd_info_buffer *buf);
void snd_seq_oss_synth_info_read(struct snd_info_buffer *buf);
void snd_seq_oss_readq_info_read(struct seq_oss_readq *q, struct snd_info_buffer *buf);

/* file mode macros */
#define is_read_mode(mode)	((mode) & SNDRV_SEQ_OSS_FILE_READ)
#define is_write_mode(mode)	((mode) & SNDRV_SEQ_OSS_FILE_WRITE)
#define is_nonblock_mode(mode)	((mode) & SNDRV_SEQ_OSS_FILE_NONBLOCK)

/* dispatch event */
static inline int
snd_seq_oss_dispatch(struct seq_oss_devinfo *dp, struct snd_seq_event *ev, int atomic, int hop)
{
	return snd_seq_kernel_client_dispatch(dp->cseq, ev, atomic, hop);
}

/* ioctl for writeq */
static inline int
snd_seq_oss_control(struct seq_oss_devinfo *dp, unsigned int type, void *arg)
{
	int err;

	snd_seq_client_ioctl_lock(dp->cseq);
	err = snd_seq_kernel_client_ctl(dp->cseq, type, arg);
	snd_seq_client_ioctl_unlock(dp->cseq);
	return err;
}

/* fill the addresses in header */
static inline void
snd_seq_oss_fill_addr(struct seq_oss_devinfo *dp, struct snd_seq_event *ev,
		     int dest_client, int dest_port)
{
	ev->queue = dp->queue;
	ev->source = dp->addr;
	ev->dest.client = dest_client;
	ev->dest.port = dest_port;
}


/* misc. functions for proc interface */
char *enabled_str(int bool);

#endif /* __SEQ_OSS_DEVICE_H */
