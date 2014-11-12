/*
 *  Common functionality for the alsa driver code base for HD Audio.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 *  more details.
 */

#ifndef __SOUND_HDA_CONTROLLER_H
#define __SOUND_HDA_CONTROLLER_H

#include <sound/core.h>
#include <sound/initval.h>
#include "hda_codec.h"
#include "hda_priv.h"

/* PCM setup */
static inline struct azx_dev *get_azx_dev(struct snd_pcm_substream *substream)
{
	return substream->runtime->private_data;
}
unsigned int azx_get_position(struct azx *chip, struct azx_dev *azx_dev);
unsigned int azx_get_pos_lpib(struct azx *chip, struct azx_dev *azx_dev);
unsigned int azx_get_pos_posbuf(struct azx *chip, struct azx_dev *azx_dev);

/* Stream control. */
void azx_stream_stop(struct azx *chip, struct azx_dev *azx_dev);

/* Allocation functions. */
int azx_alloc_stream_pages(struct azx *chip);
void azx_free_stream_pages(struct azx *chip);

/* Low level azx interface */
void azx_init_chip(struct azx *chip, bool full_reset);
void azx_stop_chip(struct azx *chip);
void azx_enter_link_reset(struct azx *chip);
irqreturn_t azx_interrupt(int irq, void *dev_id);

/* Codec interface */
int azx_codec_create(struct azx *chip, const char *model,
		     unsigned int max_slots,
		     int *power_save_to);
int azx_codec_configure(struct azx *chip);
int azx_mixer_create(struct azx *chip);
int azx_init_stream(struct azx *chip);

void azx_notifier_register(struct azx *chip);
void azx_notifier_unregister(struct azx *chip);

#endif /* __SOUND_HDA_CONTROLLER_H */
