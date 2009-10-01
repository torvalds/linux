/*
 * sound/soc/blackfin/bf5xx-ac97.h
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
	u16 ac97_pcm_l;		/*slot 3:front left*/
	u16 ac97_pcm_r;		/*slot 4:front left*/
#if defined(CONFIG_SND_BF5XX_MULTICHAN_SUPPORT)
	u16 ac97_mdm_l1;
	u16 ac97_center;	/*slot 6:center*/
	u16 ac97_sl;		/*slot 7:surround left*/
	u16 ac97_sr;		/*slot 8:surround right*/
	u16 ac97_lfe;		/*slot 9:lfe*/
#endif
} __attribute__ ((packed));

/* Speaker location */
#define SP_FL		0x0001
#define SP_FR		0x0010
#define SP_FC		0x0002
#define SP_LFE		0x0020
#define SP_SL		0x0004
#define SP_SR		0x0040

#define SP_STEREO	(SP_FL | SP_FR)
#define SP_2DOT1	(SP_FL | SP_FR | SP_LFE)
#define SP_QUAD		(SP_FL | SP_FR | SP_SL | SP_SR)
#define SP_5DOT1	(SP_FL | SP_FR | SP_FC | SP_LFE | SP_SL | SP_SR)

#define TAG_VALID		0x8000
#define TAG_CMD			0x6000
#define TAG_PCM_LEFT		0x1000
#define TAG_PCM_RIGHT		0x0800
#define TAG_PCM_MDM_L1		0x0400
#define TAG_PCM_CENTER		0x0200
#define TAG_PCM_SL		0x0100
#define TAG_PCM_SR		0x0080
#define TAG_PCM_LFE		0x0040

extern struct snd_soc_dai bfin_ac97_dai;

void bf5xx_pcm_to_ac97(struct ac97_frame *dst, const __u16 *src, \
		size_t count, unsigned int chan_mask);

void bf5xx_ac97_to_pcm(const struct ac97_frame *src, __u16 *dst, \
		size_t count);

#endif
