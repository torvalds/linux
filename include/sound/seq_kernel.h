/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __SOUND_SEQ_KERNEL_H
#define __SOUND_SEQ_KERNEL_H

/*
 *  Main kernel header file for the ALSA sequencer
 *  Copyright (c) 1998 by Frank van de Pol <fvdpol@coil.demon.nl>
 */
#include <linux/time.h>
#include <sound/asequencer.h>

typedef struct snd_seq_real_time snd_seq_real_time_t;
typedef union snd_seq_timestamp snd_seq_timestamp_t;

/* maximum number of queues */
#define SNDRV_SEQ_MAX_QUEUES		32

/* max number of concurrent clients */
#define SNDRV_SEQ_MAX_CLIENTS 		192

/* max number of concurrent ports */
#define SNDRV_SEQ_MAX_PORTS 		254

/* max number of events in memory pool */
#define SNDRV_SEQ_MAX_EVENTS		2000

/* default number of events in memory pool */
#define SNDRV_SEQ_DEFAULT_EVENTS	500

/* max number of events in memory pool for one client (outqueue) */
#define SNDRV_SEQ_MAX_CLIENT_EVENTS	2000

/* default number of events in memory pool for one client (outqueue) */
#define SNDRV_SEQ_DEFAULT_CLIENT_EVENTS	200

/* max delivery path length */
/* NOTE: this shouldn't be greater than MAX_LOCKDEP_SUBCLASSES */
#define SNDRV_SEQ_MAX_HOPS		8

/* max size of event size */
#define SNDRV_SEQ_MAX_EVENT_LEN		0x3fffffff

/* call-backs for kernel port */
struct snd_seq_port_callback {
	struct module *owner;
	void *private_data;
	int (*subscribe)(void *private_data, struct snd_seq_port_subscribe *info);
	int (*unsubscribe)(void *private_data, struct snd_seq_port_subscribe *info);
	int (*use)(void *private_data, struct snd_seq_port_subscribe *info);
	int (*unuse)(void *private_data, struct snd_seq_port_subscribe *info);
	int (*event_input)(struct snd_seq_event *ev, int direct, void *private_data, int atomic, int hop);
	void (*private_free)(void *private_data);
	/*...*/
};

/* interface for kernel client */
__printf(3, 4)
int snd_seq_create_kernel_client(struct snd_card *card, int client_index,
				 const char *name_fmt, ...);
int snd_seq_delete_kernel_client(int client);
int snd_seq_kernel_client_enqueue(int client, struct snd_seq_event *ev,
				  struct file *file, bool blocking);
int snd_seq_kernel_client_dispatch(int client, struct snd_seq_event *ev, int atomic, int hop);
int snd_seq_kernel_client_ctl(int client, unsigned int cmd, void *arg);

#define SNDRV_SEQ_EXT_MASK	0xc0000000
#define SNDRV_SEQ_EXT_USRPTR	0x80000000
#define SNDRV_SEQ_EXT_CHAINED	0x40000000

typedef int (*snd_seq_dump_func_t)(void *ptr, void *buf, int count);
int snd_seq_expand_var_event(const struct snd_seq_event *event, int count, char *buf,
			     int in_kernel, int size_aligned);
int snd_seq_expand_var_event_at(const struct snd_seq_event *event, int count,
				char *buf, int offset);
int snd_seq_dump_var_event(const struct snd_seq_event *event,
			   snd_seq_dump_func_t func, void *private_data);

/* size of the event packet; it can be greater than snd_seq_event size */
static inline size_t snd_seq_event_packet_size(struct snd_seq_event *ev)
{
	if (snd_seq_ev_is_ump(ev))
		return sizeof(struct snd_seq_ump_event);
	return sizeof(struct snd_seq_event);
}

/* interface for OSS emulation */
int snd_seq_set_queue_tempo(int client, struct snd_seq_queue_tempo *tempo);

/* port attach/detach */
int snd_seq_event_port_attach(int client, struct snd_seq_port_callback *pcbp,
			      int cap, int type, int midi_channels, int midi_voices, char *portname);
int snd_seq_event_port_detach(int client, int port);

#ifdef CONFIG_MODULES
void snd_seq_autoload_init(void);
void snd_seq_autoload_exit(void);
#else
#define snd_seq_autoload_init()
#define snd_seq_autoload_exit()
#endif

#endif /* __SOUND_SEQ_KERNEL_H */
