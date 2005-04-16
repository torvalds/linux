#ifndef __SOUND_MIXER_OSS_H
#define __SOUND_MIXER_OSS_H

/*
 *  OSS MIXER API
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

#if defined(CONFIG_SND_MIXER_OSS) || defined(CONFIG_SND_MIXER_OSS_MODULE)

typedef struct _snd_oss_mixer_slot snd_mixer_oss_slot_t;
typedef struct _snd_oss_file snd_mixer_oss_file_t;

typedef int (*snd_mixer_oss_get_volume_t)(snd_mixer_oss_file_t *fmixer, snd_mixer_oss_slot_t *chn, int *left, int *right);
typedef int (*snd_mixer_oss_put_volume_t)(snd_mixer_oss_file_t *fmixer, snd_mixer_oss_slot_t *chn, int left, int right);
typedef int (*snd_mixer_oss_get_recsrc_t)(snd_mixer_oss_file_t *fmixer, snd_mixer_oss_slot_t *chn, int *active);
typedef int (*snd_mixer_oss_put_recsrc_t)(snd_mixer_oss_file_t *fmixer, snd_mixer_oss_slot_t *chn, int active);
typedef int (*snd_mixer_oss_get_recsrce_t)(snd_mixer_oss_file_t *fmixer, unsigned int *active_index);
typedef int (*snd_mixer_oss_put_recsrce_t)(snd_mixer_oss_file_t *fmixer, unsigned int active_index);

#define SNDRV_OSS_MAX_MIXERS	32

struct _snd_oss_mixer_slot {
	int number;
	unsigned int stereo: 1;
	snd_mixer_oss_get_volume_t get_volume;
	snd_mixer_oss_put_volume_t put_volume;
	snd_mixer_oss_get_recsrc_t get_recsrc;
	snd_mixer_oss_put_recsrc_t put_recsrc;
	unsigned long private_value;
	void *private_data;
	void (*private_free)(snd_mixer_oss_slot_t *slot);
	int volume[2];
};

struct _snd_oss_mixer {
	snd_card_t *card;
	char id[16];
	char name[32];
	snd_mixer_oss_slot_t slots[SNDRV_OSS_MAX_MIXERS]; /* OSS mixer slots */
	unsigned int mask_recsrc;		/* exclusive recsrc mask */
	snd_mixer_oss_get_recsrce_t get_recsrc;
	snd_mixer_oss_put_recsrce_t put_recsrc;
	void *private_data_recsrc;
	void (*private_free_recsrc)(snd_mixer_oss_t *mixer);
	struct semaphore reg_mutex;
	snd_info_entry_t *proc_entry;
	int oss_dev_alloc;
	/* --- */
	int oss_recsrc;
};

struct _snd_oss_file {
	snd_card_t *card;
	snd_mixer_oss_t *mixer;
};

#endif /* CONFIG_SND_MIXER_OSS */

#endif /* __SOUND_MIXER_OSS_H */
