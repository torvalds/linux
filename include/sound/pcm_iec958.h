/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SOUND_PCM_IEC958_H
#define __SOUND_PCM_IEC958_H

#include <linux/types.h>

int snd_pcm_create_iec958_consumer_default(u8 *cs, size_t len);

int snd_pcm_fill_iec958_consumer(struct snd_pcm_runtime *runtime, u8 *cs,
				 size_t len);

int snd_pcm_fill_iec958_consumer_hw_params(struct snd_pcm_hw_params *params,
					   u8 *cs, size_t len);

int snd_pcm_create_iec958_consumer(struct snd_pcm_runtime *runtime, u8 *cs,
	size_t len);

int snd_pcm_create_iec958_consumer_hw_params(struct snd_pcm_hw_params *params,
					     u8 *cs, size_t len);
#endif
