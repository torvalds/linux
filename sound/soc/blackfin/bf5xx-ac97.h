/*
 * linux/sound/arm/bf5xx-ac97.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _BF5XX_AC97_H
#define _BF5XX_AC97_H

extern struct snd_ac97_bus_ops bf5xx_ac97_ops;
extern struct snd_ac97 *ac97;
/* Frame format in memory, only support stereo currently */
struct ac97_frame {
	u16 ac97_tag;		/* slot 0 */
	u16 ac97_addr;		/* slot 1 */
	u16 ac97_data;		/* slot 2 */
	u32 ac97_pcm;		/* slot 3 and 4: left and right pcm data */
} __attribute__ ((packed));

#define TAG_VALID		0x8000
#define TAG_CMD			0x6000
#define TAG_PCM_LEFT		0x1000
#define TAG_PCM_RIGHT		0x0800
#define TAG_PCM			(TAG_PCM_LEFT | TAG_PCM_RIGHT)

extern struct snd_soc_dai bfin_ac97_dai;

void bf5xx_pcm_to_ac97(struct ac97_frame *dst, const __u32 *src, \
		size_t count);

void bf5xx_ac97_to_pcm(const struct ac97_frame *src, __u32 *dst, \
		size_t count);

#endif
