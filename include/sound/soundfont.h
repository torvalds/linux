#ifndef __SOUND_SOUNDFONT_H
#define __SOUND_SOUNDFONT_H

/*
 *  Soundfont defines and definitions.
 *
 *  Copyright (C) 1999 Steve Ratcliffe
 *  Copyright (c) 1999-2000 Takashi iwai <tiwai@suse.de>
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
 */

#include "sfnt_info.h"
#include "util_mem.h"

#define SF_MAX_INSTRUMENTS	128	/* maximum instrument number */
#define SF_MAX_PRESETS  256	/* drums are mapped from 128 to 256 */
#define SF_IS_DRUM_BANK(z) ((z) == 128)

typedef struct snd_sf_zone {
	struct snd_sf_zone *next;	/* Link to next */
	unsigned char bank;		/* Midi bank for this zone */
	unsigned char instr;		/* Midi program for this zone */
	unsigned char mapped;		/* True if mapped to something else */

	soundfont_voice_info_t v;	/* All the soundfont parameters */
	int counter;
	struct snd_sf_sample *sample;	/* Link to sample */

	/* The following deals with preset numbers (programs) */
	struct snd_sf_zone *next_instr;	/* Next zone of this instrument */
	struct snd_sf_zone *next_zone;	/* Next zone in play list */
} snd_sf_zone_t;

typedef struct snd_sf_sample {
	soundfont_sample_info_t v;
	int counter;
	snd_util_memblk_t *block;	/* allocated data block */
	struct snd_sf_sample *next;
} snd_sf_sample_t;

/*
 * This represents all the information relating to a soundfont.
 */
typedef struct snd_soundfont {
	struct snd_soundfont *next;	/* Link to next */
	/*struct snd_soundfont *prev;*/	/* Link to previous */
	short  id;		/* file id */
	short  type;		/* font type */
	unsigned char name[SNDRV_SFNT_PATCH_NAME_LEN];	/* identifier */
	snd_sf_zone_t *zones; /* Font information */
	snd_sf_sample_t *samples; /* The sample headers */
} snd_soundfont_t;

/*
 * Type of the sample access callback
 */
typedef int (*snd_sf_sample_new_t)(void *private_data, snd_sf_sample_t *sp,
				   snd_util_memhdr_t *hdr, const void __user *buf, long count);
typedef int (*snd_sf_sample_free_t)(void *private_data, snd_sf_sample_t *sp,
				    snd_util_memhdr_t *hdr);
typedef void (*snd_sf_sample_reset_t)(void *private);

typedef struct snd_sf_callback {
	void *private_data;
	snd_sf_sample_new_t sample_new;
	snd_sf_sample_free_t sample_free;
	snd_sf_sample_reset_t sample_reset;
} snd_sf_callback_t;

/*
 * List of soundfonts.
 */
typedef struct snd_sf_list {
	snd_soundfont_t *currsf; /* The currently open soundfont */
	int open_client;	/* client pointer for lock */
	int mem_used;		/* used memory size */
	snd_sf_zone_t *presets[SF_MAX_PRESETS];
	snd_soundfont_t *fonts; /* The list of soundfonts */
	int fonts_size;	/* number of fonts allocated */
	int zone_counter;	/* last allocated time for zone */
	int sample_counter;	/* last allocated time for sample */
	int zone_locked;	/* locked time for zone */
	int sample_locked;	/* locked time for sample */
	snd_sf_callback_t callback;	/* callback functions */
	int presets_locked;
	struct semaphore presets_mutex;
	spinlock_t lock;
	snd_util_memhdr_t *memhdr;
} snd_sf_list_t;

/* Prototypes for soundfont.c */
int snd_soundfont_load(snd_sf_list_t *sflist, const void __user *data, long count, int client);
int snd_soundfont_load_guspatch(snd_sf_list_t *sflist, const char __user *data,
				long count, int client);
int snd_soundfont_close_check(snd_sf_list_t *sflist, int client);

snd_sf_list_t *snd_sf_new(snd_sf_callback_t *callback, snd_util_memhdr_t *hdr);
void snd_sf_free(snd_sf_list_t *sflist);

int snd_soundfont_remove_samples(snd_sf_list_t *sflist);
int snd_soundfont_remove_unlocked(snd_sf_list_t *sflist);

int snd_soundfont_search_zone(snd_sf_list_t *sflist, int *notep, int vel,
			      int preset, int bank,
			      int def_preset, int def_bank,
			      snd_sf_zone_t **table, int max_layers);

/* Parameter conversions */
int snd_sf_calc_parm_hold(int msec);
int snd_sf_calc_parm_attack(int msec);
int snd_sf_calc_parm_decay(int msec);
#define snd_sf_calc_parm_delay(msec) (0x8000 - (msec) * 1000 / 725);
extern int snd_sf_vol_table[128];
int snd_sf_linear_to_log(unsigned int amount, int offset, int ratio);


#endif /* __SOUND_SOUNDFONT_H */
