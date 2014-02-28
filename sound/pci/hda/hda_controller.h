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
void azx_stream_stop(struct azx *chip, struct azx_dev *azx_dev);

#ifdef CONFIG_SND_HDA_DSP_LOADER
int azx_load_dsp_prepare(struct hda_bus *bus, unsigned int format,
			 unsigned int byte_size,
			 struct snd_dma_buffer *bufp);
void azx_load_dsp_trigger(struct hda_bus *bus, bool start);
void azx_load_dsp_cleanup(struct hda_bus *bus,
			  struct snd_dma_buffer *dmab);
#endif

/* Allocation functions. */
int azx_alloc_stream_pages(struct azx *chip);
void azx_free_stream_pages(struct azx *chip);

/*
 * CORB / RIRB interface
 */
int azx_alloc_cmd_io(struct azx *chip);
void azx_init_cmd_io(struct azx *chip);
void azx_free_cmd_io(struct azx *chip);
void azx_update_rirb(struct azx *chip);
int azx_send_cmd(struct hda_bus *bus, unsigned int val);
unsigned int azx_get_response(struct hda_bus *bus,
			      unsigned int addr);

#endif /* __SOUND_HDA_CONTROLLER_H */
