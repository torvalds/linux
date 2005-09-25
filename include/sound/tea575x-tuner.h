#ifndef __SOUND_TEA575X_TUNER_H
#define __SOUND_TEA575X_TUNER_H

/*
 *   ALSA driver for TEA5757/5759 Philips AM/FM tuner chips
 *
 *	Copyright (c) 2004 Jaroslav Kysela <perex@suse.cz>
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

#include <linux/videodev.h>

typedef struct snd_tea575x tea575x_t;

struct snd_tea575x_ops {
	void (*write)(tea575x_t *tea, unsigned int val);
	unsigned int (*read)(tea575x_t *tea);
};

struct snd_tea575x {
	snd_card_t *card;
	struct video_device vd;		/* video device */
	struct file_operations fops;
	int dev_nr;			/* requested device number + 1 */
	int vd_registered;		/* video device is registered */
	int tea5759;			/* 5759 chip is present */
	unsigned int freq_fixup;	/* crystal onboard */
	unsigned int val;		/* hw value */
	unsigned long freq;		/* frequency */
	struct snd_tea575x_ops *ops;
	void *private_data;
};

void snd_tea575x_init(tea575x_t *tea);
void snd_tea575x_exit(tea575x_t *tea);

#endif /* __SOUND_TEA575X_TUNER_H */
