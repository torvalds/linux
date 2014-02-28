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
int azx_attach_pcm_stream(struct hda_bus *bus, struct hda_codec *codec,
			  struct hda_pcm *cpcm);
static inline struct azx_dev *get_azx_dev(struct snd_pcm_substream *substream)
{
	return substream->runtime->private_data;
}
unsigned int azx_get_position(struct azx *chip,
			      struct azx_dev *azx_dev,
			      bool with_check);

/* Stream control. */
void azx_stream_start(struct azx *chip, struct azx_dev *azx_dev);
void azx_stream_stop(struct azx *chip, struct azx_dev *azx_dev);
void azx_stream_reset(struct azx *chip, struct azx_dev *azx_dev);
int azx_setup_controller(struct azx *chip, struct azx_dev *azx_dev);
int setup_bdle(struct azx *chip,
	       struct snd_dma_buffer *dmab,
	       struct azx_dev *azx_dev, u32 **bdlp,
	       int ofs, int size, int with_ioc);

/* DSP lock helpers */
#ifdef CONFIG_SND_HDA_DSP_LOADER
#define dsp_lock_init(dev)	mutex_init(&(dev)->dsp_mutex)
#define dsp_lock(dev)		mutex_lock(&(dev)->dsp_mutex)
#define dsp_unlock(dev)		mutex_unlock(&(dev)->dsp_mutex)
#define dsp_is_locked(dev)	((dev)->locked)
#else
#define dsp_lock_init(dev)	do {} while (0)
#define dsp_lock(dev)		do {} while (0)
#define dsp_unlock(dev)		do {} while (0)
#define dsp_is_locked(dev)	0
#endif

#endif /* __SOUND_HDA_CONTROLLER_H */
