#ifndef __SOUND_CONTROL_H
#define __SOUND_CONTROL_H

/*
 *  Header file for control interface
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

typedef struct sndrv_aes_iec958 snd_aes_iec958_t;
typedef struct sndrv_ctl_card_info snd_ctl_card_info_t;
typedef enum sndrv_ctl_elem_type snd_ctl_elem_type_t;
typedef enum sndrv_ctl_elem_iface snd_ctl_elem_iface_t;
typedef struct sndrv_ctl_elem_id snd_ctl_elem_id_t;
typedef struct sndrv_ctl_elem_list snd_ctl_elem_list_t;
typedef struct sndrv_ctl_elem_info snd_ctl_elem_info_t;
typedef struct sndrv_ctl_elem_value snd_ctl_elem_value_t;
typedef enum sndrv_ctl_event_type snd_ctl_event_type_t;
typedef struct sndrv_ctl_event snd_ctl_event_t;

#define snd_kcontrol_chip(kcontrol) ((kcontrol)->private_data)

typedef int (snd_kcontrol_info_t) (snd_kcontrol_t * kcontrol, snd_ctl_elem_info_t * uinfo);
typedef int (snd_kcontrol_get_t) (snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol);
typedef int (snd_kcontrol_put_t) (snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol);

typedef struct _snd_kcontrol_new {
	snd_ctl_elem_iface_t iface;	/* interface identifier */
	unsigned int device;		/* device/client number */
	unsigned int subdevice;		/* subdevice (substream) number */
	unsigned char *name;		/* ASCII name of item */
	unsigned int index;		/* index of item */
	unsigned int access;		/* access rights */
	unsigned int count;		/* count of same elements */
	snd_kcontrol_info_t *info;
	snd_kcontrol_get_t *get;
	snd_kcontrol_put_t *put;
	unsigned long private_value;
} snd_kcontrol_new_t;

typedef struct _snd_kcontrol_volatile {
	snd_ctl_file_t *owner;	/* locked */
	pid_t owner_pid;
	unsigned int access;	/* access rights */
} snd_kcontrol_volatile_t;

struct _snd_kcontrol {
	struct list_head list;		/* list of controls */
	snd_ctl_elem_id_t id;
	unsigned int count;		/* count of same elements */
	snd_kcontrol_info_t *info;
	snd_kcontrol_get_t *get;
	snd_kcontrol_put_t *put;
	unsigned long private_value;
	void *private_data;
	void (*private_free)(snd_kcontrol_t *kcontrol);
	snd_kcontrol_volatile_t vd[0];	/* volatile data */
};

#define snd_kcontrol(n) list_entry(n, snd_kcontrol_t, list)

typedef struct _snd_kctl_event {
	struct list_head list;	/* list of events */
	snd_ctl_elem_id_t id;
	unsigned int mask;
} snd_kctl_event_t;

#define snd_kctl_event(n) list_entry(n, snd_kctl_event_t, list)

struct _snd_ctl_file {
	struct list_head list;		/* list of all control files */
	snd_card_t *card;
	pid_t pid;
	int prefer_pcm_subdevice;
	int prefer_rawmidi_subdevice;
	wait_queue_head_t change_sleep;
	spinlock_t read_lock;
	struct fasync_struct *fasync;
	int subscribed;			/* read interface is activated */
	struct list_head events;	/* waiting events for read */
};

#define snd_ctl_file(n) list_entry(n, snd_ctl_file_t, list)

typedef int (*snd_kctl_ioctl_func_t) (snd_card_t * card,
				 snd_ctl_file_t * control,
				 unsigned int cmd, unsigned long arg);

void snd_ctl_notify(snd_card_t * card, unsigned int mask, snd_ctl_elem_id_t * id);

snd_kcontrol_t *snd_ctl_new(snd_kcontrol_t * kcontrol, unsigned int access);
snd_kcontrol_t *snd_ctl_new1(const snd_kcontrol_new_t * kcontrolnew, void * private_data);
void snd_ctl_free_one(snd_kcontrol_t * kcontrol);
int snd_ctl_add(snd_card_t * card, snd_kcontrol_t * kcontrol);
int snd_ctl_remove(snd_card_t * card, snd_kcontrol_t * kcontrol);
int snd_ctl_remove_id(snd_card_t * card, snd_ctl_elem_id_t *id);
int snd_ctl_rename_id(snd_card_t * card, snd_ctl_elem_id_t *src_id, snd_ctl_elem_id_t *dst_id);
snd_kcontrol_t *snd_ctl_find_numid(snd_card_t * card, unsigned int numid);
snd_kcontrol_t *snd_ctl_find_id(snd_card_t * card, snd_ctl_elem_id_t *id);

int snd_ctl_create(snd_card_t *card);

int snd_ctl_register_ioctl(snd_kctl_ioctl_func_t fcn);
int snd_ctl_unregister_ioctl(snd_kctl_ioctl_func_t fcn);
#ifdef CONFIG_COMPAT
int snd_ctl_register_ioctl_compat(snd_kctl_ioctl_func_t fcn);
int snd_ctl_unregister_ioctl_compat(snd_kctl_ioctl_func_t fcn);
#else
#define snd_ctl_register_ioctl_compat(fcn)
#define snd_ctl_unregister_ioctl_compat(fcn)
#endif

int snd_ctl_elem_read(snd_card_t *card, snd_ctl_elem_value_t *control);
int snd_ctl_elem_write(snd_card_t *card, snd_ctl_file_t *file, snd_ctl_elem_value_t *control);

static inline unsigned int snd_ctl_get_ioffnum(snd_kcontrol_t *kctl, snd_ctl_elem_id_t *id)
{
	return id->numid - kctl->id.numid;
}

static inline unsigned int snd_ctl_get_ioffidx(snd_kcontrol_t *kctl, snd_ctl_elem_id_t *id)
{
	return id->index - kctl->id.index;
}

static inline unsigned int snd_ctl_get_ioff(snd_kcontrol_t *kctl, snd_ctl_elem_id_t *id)
{
	if (id->numid) {
		return snd_ctl_get_ioffnum(kctl, id);
	} else {
		return snd_ctl_get_ioffidx(kctl, id);
	}
}

static inline snd_ctl_elem_id_t *snd_ctl_build_ioff(snd_ctl_elem_id_t *dst_id,
						    snd_kcontrol_t *src_kctl,
						    unsigned int offset)
{
	*dst_id = src_kctl->id;
	dst_id->index += offset;
	dst_id->numid += offset;
	return dst_id;
}

#endif	/* __SOUND_CONTROL_H */
