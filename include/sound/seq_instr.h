#ifndef __SOUND_SEQ_INSTR_H
#define __SOUND_SEQ_INSTR_H

/*
 *  Main kernel header file for the ALSA sequencer
 *  Copyright (c) 1999 by Jaroslav Kysela <perex@suse.cz>
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
#include "seq_kernel.h"

/* Instrument cluster */
typedef struct _snd_seq_kcluster {
	snd_seq_instr_cluster_t cluster;
	char name[32];
	int priority;
	struct _snd_seq_kcluster *next;
} snd_seq_kcluster_t;

/* return pointer to private data */
#define KINSTR_DATA(kinstr)	(void *)(((char *)kinstr) + sizeof(snd_seq_kinstr_t))

typedef struct snd_seq_kinstr_ops snd_seq_kinstr_ops_t;

/* Instrument structure */
typedef struct _snd_seq_kinstr {
	snd_seq_instr_t instr;
	char name[32];
	int type;			/* instrument type */
	int use;			/* use count */
	int busy;			/* not useable */
	int add_len;			/* additional length */
	snd_seq_kinstr_ops_t *ops;	/* operations */
	struct _snd_seq_kinstr *next;
} snd_seq_kinstr_t;

#define SNDRV_SEQ_INSTR_HASH_SIZE		32

/* Instrument flags */
#define SNDRV_SEQ_INSTR_FLG_DIRECT	(1<<0)	/* accept only direct events */

/* List of all instruments */
typedef struct {
	snd_seq_kinstr_t *hash[SNDRV_SEQ_INSTR_HASH_SIZE];
	int count;			/* count of all instruments */
	
	snd_seq_kcluster_t *chash[SNDRV_SEQ_INSTR_HASH_SIZE];
	int ccount;			/* count of all clusters */

	int owner;			/* current owner of the instrument list */
	unsigned int flags;

	spinlock_t lock;
	spinlock_t ops_lock;
	struct semaphore ops_mutex;
	unsigned long ops_flags;
} snd_seq_kinstr_list_t;

#define SNDRV_SEQ_INSTR_NOTIFY_REMOVE	0
#define SNDRV_SEQ_INSTR_NOTIFY_CHANGE	1

struct snd_seq_kinstr_ops {
	void *private_data;
	long add_len;			/* additional length */
	char *instr_type;
	int (*info)(void *private_data, char *info_data, long len);
	int (*put)(void *private_data, snd_seq_kinstr_t *kinstr,
		   char __user *instr_data, long len, int atomic, int cmd);
	int (*get)(void *private_data, snd_seq_kinstr_t *kinstr,
		   char __user *instr_data, long len, int atomic, int cmd);
	int (*get_size)(void *private_data, snd_seq_kinstr_t *kinstr, long *size);
	int (*remove)(void *private_data, snd_seq_kinstr_t *kinstr, int atomic);
	void (*notify)(void *private_data, snd_seq_kinstr_t *kinstr, int what);
	struct snd_seq_kinstr_ops *next;
};


/* instrument operations */
snd_seq_kinstr_list_t *snd_seq_instr_list_new(void);
void snd_seq_instr_list_free(snd_seq_kinstr_list_t **list);
int snd_seq_instr_list_free_cond(snd_seq_kinstr_list_t *list,
				 snd_seq_instr_header_t *ifree,
				 int client,
				 int atomic);
snd_seq_kinstr_t *snd_seq_instr_find(snd_seq_kinstr_list_t *list,
				     snd_seq_instr_t *instr,
				     int exact,
				     int follow_alias);
void snd_seq_instr_free_use(snd_seq_kinstr_list_t *list,
			    snd_seq_kinstr_t *instr);
int snd_seq_instr_event(snd_seq_kinstr_ops_t *ops,
			snd_seq_kinstr_list_t *list,
			snd_seq_event_t *ev,
			int client,
			int atomic,
			int hop);

#endif /* __SOUND_SEQ_INSTR_H */
