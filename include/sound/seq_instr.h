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
struct snd_seq_kcluster {
	snd_seq_instr_cluster_t cluster;
	char name[32];
	int priority;
	struct snd_seq_kcluster *next;
};

/* return pointer to private data */
#define KINSTR_DATA(kinstr)	(void *)(((char *)kinstr) + sizeof(struct snd_seq_kinstr))

/* Instrument structure */
struct snd_seq_kinstr {
	struct snd_seq_instr instr;
	char name[32];
	int type;			/* instrument type */
	int use;			/* use count */
	int busy;			/* not useable */
	int add_len;			/* additional length */
	struct snd_seq_kinstr_ops *ops;	/* operations */
	struct snd_seq_kinstr *next;
};

#define SNDRV_SEQ_INSTR_HASH_SIZE		32

/* Instrument flags */
#define SNDRV_SEQ_INSTR_FLG_DIRECT	(1<<0)	/* accept only direct events */

/* List of all instruments */
struct snd_seq_kinstr_list {
	struct snd_seq_kinstr *hash[SNDRV_SEQ_INSTR_HASH_SIZE];
	int count;			/* count of all instruments */
	
	struct snd_seq_kcluster *chash[SNDRV_SEQ_INSTR_HASH_SIZE];
	int ccount;			/* count of all clusters */

	int owner;			/* current owner of the instrument list */
	unsigned int flags;

	spinlock_t lock;
	spinlock_t ops_lock;
	struct mutex ops_mutex;
	unsigned long ops_flags;
};

#define SNDRV_SEQ_INSTR_NOTIFY_REMOVE	0
#define SNDRV_SEQ_INSTR_NOTIFY_CHANGE	1

struct snd_seq_kinstr_ops {
	void *private_data;
	long add_len;			/* additional length */
	char *instr_type;
	int (*info)(void *private_data, char *info_data, long len);
	int (*put)(void *private_data, struct snd_seq_kinstr *kinstr,
		   char __user *instr_data, long len, int atomic, int cmd);
	int (*get)(void *private_data, struct snd_seq_kinstr *kinstr,
		   char __user *instr_data, long len, int atomic, int cmd);
	int (*get_size)(void *private_data, struct snd_seq_kinstr *kinstr, long *size);
	int (*remove)(void *private_data, struct snd_seq_kinstr *kinstr, int atomic);
	void (*notify)(void *private_data, struct snd_seq_kinstr *kinstr, int what);
	struct snd_seq_kinstr_ops *next;
};


/* instrument operations */
struct snd_seq_kinstr_list *snd_seq_instr_list_new(void);
void snd_seq_instr_list_free(struct snd_seq_kinstr_list **list);
int snd_seq_instr_list_free_cond(struct snd_seq_kinstr_list *list,
				 struct snd_seq_instr_header *ifree,
				 int client,
				 int atomic);
struct snd_seq_kinstr *snd_seq_instr_find(struct snd_seq_kinstr_list *list,
					  struct snd_seq_instr *instr,
					  int exact,
					  int follow_alias);
void snd_seq_instr_free_use(struct snd_seq_kinstr_list *list,
			    struct snd_seq_kinstr *instr);
int snd_seq_instr_event(struct snd_seq_kinstr_ops *ops,
			struct snd_seq_kinstr_list *list,
			struct snd_seq_event *ev,
			int client,
			int atomic,
			int hop);

#endif /* __SOUND_SEQ_INSTR_H */
