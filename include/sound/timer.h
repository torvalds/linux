#ifndef __SOUND_TIMER_H
#define __SOUND_TIMER_H

/*
 *  Timer abstract layer
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>,
 *		     Abramo Bagnara <abramo@alsa-project.org>
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

typedef enum sndrv_timer_class snd_timer_class_t;
typedef enum sndrv_timer_slave_class snd_timer_slave_class_t;
typedef enum sndrv_timer_global snd_timer_global_t;
typedef struct sndrv_timer_id snd_timer_id_t;
typedef struct sndrv_timer_ginfo snd_timer_ginfo_t;
typedef struct sndrv_timer_gparams snd_timer_gparams_t;
typedef struct sndrv_timer_gstatus snd_timer_gstatus_t;
typedef struct sndrv_timer_select snd_timer_select_t;
typedef struct sndrv_timer_info snd_timer_info_t;
typedef struct sndrv_timer_params snd_timer_params_t;
typedef struct sndrv_timer_status snd_timer_status_t;
typedef struct sndrv_timer_read snd_timer_read_t;
typedef struct sndrv_timer_tread snd_timer_tread_t;

#define snd_timer_chip(timer) ((timer)->private_data)

#define SNDRV_TIMER_DEVICES	16

#define SNDRV_TIMER_DEV_FLG_PCM	0x10000000

#define SNDRV_TIMER_HW_AUTO	0x00000001	/* auto trigger is supported */
#define SNDRV_TIMER_HW_STOP	0x00000002	/* call stop before start */
#define SNDRV_TIMER_HW_SLAVE	0x00000004	/* only slave timer (variable resolution) */
#define SNDRV_TIMER_HW_FIRST	0x00000008	/* first tick can be incomplete */
#define SNDRV_TIMER_HW_TASKLET	0x00000010	/* timer is called from tasklet */

#define SNDRV_TIMER_IFLG_SLAVE	  0x00000001
#define SNDRV_TIMER_IFLG_RUNNING  0x00000002
#define SNDRV_TIMER_IFLG_START	  0x00000004
#define SNDRV_TIMER_IFLG_AUTO	  0x00000008	/* auto restart */
#define SNDRV_TIMER_IFLG_FAST	  0x00000010	/* fast callback (do not use tasklet) */
#define SNDRV_TIMER_IFLG_CALLBACK 0x00000020	/* timer callback is active */
#define SNDRV_TIMER_IFLG_EXCLUSIVE 0x00000040	/* exclusive owner - no more instances */
#define SNDRV_TIMER_IFLG_EARLY_EVENT 0x00000080	/* write early event to the poll queue */

#define SNDRV_TIMER_FLG_CHANGE	0x00000001
#define SNDRV_TIMER_FLG_RESCHED	0x00000002	/* need reschedule */

typedef void (*snd_timer_callback_t) (snd_timer_instance_t * timeri, unsigned long ticks, unsigned long resolution);
typedef void (*snd_timer_ccallback_t) (snd_timer_instance_t * timeri, enum sndrv_timer_event event,
				       struct timespec * tstamp, unsigned long resolution);

struct _snd_timer_hardware {
	/* -- must be filled with low-level driver */
	unsigned int flags;		/* various flags */
	unsigned long resolution;	/* average timer resolution for one tick in nsec */
	unsigned long resolution_min;	/* minimal resolution */
	unsigned long resolution_max;	/* maximal resolution */
	unsigned long ticks;		/* max timer ticks per interrupt */
	/* -- low-level functions -- */
	int (*open) (snd_timer_t * timer);
	int (*close) (snd_timer_t * timer);
	unsigned long (*c_resolution) (snd_timer_t * timer);
	int (*start) (snd_timer_t * timer);
	int (*stop) (snd_timer_t * timer);
	int (*set_period) (snd_timer_t * timer, unsigned long period_num, unsigned long period_den);
	int (*precise_resolution) (snd_timer_t * timer, unsigned long *num, unsigned long *den);
};

struct _snd_timer {
	snd_timer_class_t tmr_class;
	snd_card_t *card;
	int tmr_device;
	int tmr_subdevice;
	char id[64];
	char name[80];
	unsigned int flags;
	int running;			/* running instances */
	unsigned long sticks;		/* schedule ticks */
	void *private_data;
	void (*private_free) (snd_timer_t *timer);
	struct _snd_timer_hardware hw;
	spinlock_t lock;
	struct list_head device_list;
	struct list_head open_list_head;
	struct list_head active_list_head;
	struct list_head ack_list_head;
	struct list_head sack_list_head; /* slow ack list head */
	struct tasklet_struct task_queue;
};

struct _snd_timer_instance {
	snd_timer_t * timer;
	char *owner;
	unsigned int flags;
	void *private_data;
	void (*private_free) (snd_timer_instance_t *ti);
	snd_timer_callback_t callback;
	snd_timer_ccallback_t ccallback;
	void *callback_data;
	unsigned long ticks;		/* auto-load ticks when expired */
	unsigned long cticks;		/* current ticks */
	unsigned long pticks;		/* accumulated ticks for callback */
	unsigned long resolution;	/* current resolution for tasklet */
	unsigned long lost;		/* lost ticks */
	snd_timer_slave_class_t slave_class;
	unsigned int slave_id;
	struct list_head open_list;
	struct list_head active_list;
	struct list_head ack_list;
	struct list_head slave_list_head;
	struct list_head slave_active_head;
	snd_timer_instance_t *master;
};

/*
 *  Registering
 */

extern int snd_timer_new(snd_card_t *card, char *id, snd_timer_id_t *tid, snd_timer_t ** rtimer);
extern void snd_timer_notify(snd_timer_t *timer, enum sndrv_timer_event event, struct timespec *tstamp);
extern int snd_timer_global_new(char *id, int device, snd_timer_t **rtimer);
extern int snd_timer_global_free(snd_timer_t *timer);
extern int snd_timer_global_register(snd_timer_t *timer);
extern int snd_timer_global_unregister(snd_timer_t *timer);

extern int snd_timer_open(snd_timer_instance_t ** ti, char *owner, snd_timer_id_t *tid, unsigned int slave_id);
extern int snd_timer_close(snd_timer_instance_t * timeri);
extern unsigned long snd_timer_resolution(snd_timer_instance_t * timeri);
extern int snd_timer_start(snd_timer_instance_t * timeri, unsigned int ticks);
extern int snd_timer_stop(snd_timer_instance_t * timeri);
extern int snd_timer_continue(snd_timer_instance_t * timeri);
extern int snd_timer_pause(snd_timer_instance_t * timeri);

extern void snd_timer_interrupt(snd_timer_t * timer, unsigned long ticks_left);

extern unsigned int snd_timer_system_resolution(void);

#endif /* __SOUND_TIMER_H */
