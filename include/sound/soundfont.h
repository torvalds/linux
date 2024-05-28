/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __SOUND_SOUNDFONT_H
#define __SOUND_SOUNDFONT_H

/*
 *  Soundfont defines and definitions.
 *
 *  Copyright (C) 1999 Steve Ratcliffe
 *  Copyright (c) 1999-2000 Takashi iwai <tiwai@suse.de>
 */

#include <sound/sfnt_info.h>
#include <sound/util_mem.h>

#define SF_MAX_INSTRUMENTS	128	/* maximum instrument number */
#define SF_MAX_PRESETS  256	/* drums are mapped from 128 to 256 */
#define SF_IS_DRUM_BANK(z) ((z) == 128)

struct snd_sf_zone {
	struct snd_sf_zone *next;	/* Link to next */
	unsigned char bank;		/* Midi bank for this zone */
	unsigned char instr;		/* Midi program for this zone */
	unsigned char mapped;		/* True if mapped to something else */

	struct soundfont_voice_info v;	/* All the soundfont parameters */
	int counter;
	struct snd_sf_sample *sample;	/* Link to sample */

	/* The following deals with preset numbers (programs) */
	struct snd_sf_zone *next_instr;	/* Next zone of this instrument */
	struct snd_sf_zone *next_zone;	/* Next zone in play list */
};

struct snd_sf_sample {
	struct soundfont_sample_info v;
	int counter;
	struct snd_util_memblk *block;	/* allocated data block */
	struct snd_sf_sample *next;
};

/*
 * This represents all the information relating to a soundfont.
 */
struct snd_soundfont {
	struct snd_soundfont *next;	/* Link to next */
	/*struct snd_soundfont *prev;*/	/* Link to previous */
	short  id;		/* file id */
	short  type;		/* font type */
	unsigned char name[SNDRV_SFNT_PATCH_NAME_LEN];	/* identifier */
	struct snd_sf_zone *zones; /* Font information */
	struct snd_sf_sample *samples; /* The sample headers */
};

/*
 * Type of the sample access callback
 */
struct snd_sf_callback {
	void *private_data;
	int (*sample_new)(void *private_data, struct snd_sf_sample *sp,
			  struct snd_util_memhdr *hdr,
			  const void __user *buf, long count);
	int (*sample_free)(void *private_data, struct snd_sf_sample *sp,
			   struct snd_util_memhdr *hdr);
	void (*sample_reset)(void *private);
};

/*
 * List of soundfonts.
 */
struct snd_sf_list {
	struct snd_soundfont *currsf; /* The currently open soundfont */
	int open_client;	/* client pointer for lock */
	int mem_used;		/* used memory size */
	struct snd_sf_zone *presets[SF_MAX_PRESETS];
	struct snd_soundfont *fonts; /* The list of soundfonts */
	int fonts_size;	/* number of fonts allocated */
	int zone_counter;	/* last allocated time for zone */
	int sample_counter;	/* last allocated time for sample */
	int zone_locked;	/* locked time for zone */
	int sample_locked;	/* locked time for sample */
	struct snd_sf_callback callback;	/* callback functions */
	int presets_locked;
	struct mutex presets_mutex;
	spinlock_t lock;
	struct snd_util_memhdr *memhdr;
};

/* Prototypes for soundfont.c */
int snd_soundfont_load(struct snd_sf_list *sflist, const void __user *data,
		       long count, int client);
int snd_soundfont_load_guspatch(struct snd_sf_list *sflist, const char __user *data,
				long count);
int snd_soundfont_close_check(struct snd_sf_list *sflist, int client);

struct snd_sf_list *snd_sf_new(struct snd_sf_callback *callback,
			       struct snd_util_memhdr *hdr);
void snd_sf_free(struct snd_sf_list *sflist);

int snd_soundfont_remove_samples(struct snd_sf_list *sflist);
int snd_soundfont_remove_unlocked(struct snd_sf_list *sflist);

int snd_soundfont_search_zone(struct snd_sf_list *sflist, int *notep, int vel,
			      int preset, int bank,
			      int def_preset, int def_bank,
			      struct snd_sf_zone **table, int max_layers);

/* Parameter conversions */
int snd_sf_calc_parm_hold(int msec);
int snd_sf_calc_parm_attack(int msec);
int snd_sf_calc_parm_decay(int msec);
#define snd_sf_calc_parm_delay(msec) (0x8000 - (msec) * 1000 / 725)
extern int snd_sf_vol_table[128];
int snd_sf_linear_to_log(unsigned int amount, int offset, int ratio);


#endif /* __SOUND_SOUNDFONT_H */
