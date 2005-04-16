#ifndef __SOUND_HWDEP_H
#define __SOUND_HWDEP_H

/*
 *  Hardware dependent layer 
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
#include <linux/poll.h>

typedef enum sndrv_hwdep_iface snd_hwdep_iface_t;
typedef struct sndrv_hwdep_info snd_hwdep_info_t;
typedef struct sndrv_hwdep_dsp_status snd_hwdep_dsp_status_t;
typedef struct sndrv_hwdep_dsp_image snd_hwdep_dsp_image_t;

typedef struct _snd_hwdep_ops {
	long long (*llseek) (snd_hwdep_t *hw, struct file * file, long long offset, int orig);
	long (*read) (snd_hwdep_t * hw, char __user *buf, long count, loff_t *offset);
	long (*write) (snd_hwdep_t * hw, const char __user *buf, long count, loff_t *offset);
	int (*open) (snd_hwdep_t * hw, struct file * file);
	int (*release) (snd_hwdep_t * hw, struct file * file);
	unsigned int (*poll) (snd_hwdep_t * hw, struct file * file, poll_table * wait);
	int (*ioctl) (snd_hwdep_t * hw, struct file * file, unsigned int cmd, unsigned long arg);
	int (*ioctl_compat) (snd_hwdep_t * hw, struct file * file, unsigned int cmd, unsigned long arg);
	int (*mmap) (snd_hwdep_t * hw, struct file * file, struct vm_area_struct * vma);
	int (*dsp_status) (snd_hwdep_t * hw, snd_hwdep_dsp_status_t * status);
	int (*dsp_load) (snd_hwdep_t * hw, snd_hwdep_dsp_image_t * image);
} snd_hwdep_ops_t;

struct _snd_hwdep {
	snd_card_t *card;
	int device;
	char id[32];
	char name[80];
	int iface;

#ifdef CONFIG_SND_OSSEMUL
	char oss_dev[32];
	int oss_type;
	int ossreg;
#endif

	snd_hwdep_ops_t ops;
	wait_queue_head_t open_wait;
	void *private_data;
	void (*private_free) (snd_hwdep_t *hwdep);

	struct semaphore open_mutex;
	int used;
	unsigned int dsp_loaded;
	unsigned int exclusive: 1;
};

extern int snd_hwdep_new(snd_card_t * card, char *id, int device, snd_hwdep_t ** rhwdep);

#endif /* __SOUND_HWDEP_H */
