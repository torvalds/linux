/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright 2003 Vivien Chappelier <vivien.chappelier@linux-mips.org>
 * Copyright 2008 Thomas Bogendoerfer <tsbogend@franken.de>
 */

#ifndef __SOUND_AD1843_H
#define __SOUND_AD1843_H

struct snd_ad1843 {
	void *chip;
	int (*read)(void *chip, int reg);
	int (*write)(void *chip, int reg, int val);
};

#define AD1843_GAIN_RECLEV 0
#define AD1843_GAIN_LINE   1
#define AD1843_GAIN_LINE_2 2
#define AD1843_GAIN_MIC    3
#define AD1843_GAIN_PCM_0  4
#define AD1843_GAIN_PCM_1  5
#define AD1843_GAIN_SIZE   (AD1843_GAIN_PCM_1+1)

int ad1843_get_gain_max(struct snd_ad1843 *ad1843, int id);
int ad1843_get_gain(struct snd_ad1843 *ad1843, int id);
int ad1843_set_gain(struct snd_ad1843 *ad1843, int id, int newval);
int ad1843_get_recsrc(struct snd_ad1843 *ad1843);
int ad1843_set_recsrc(struct snd_ad1843 *ad1843, int newsrc);
void ad1843_setup_dac(struct snd_ad1843 *ad1843,
		      unsigned int id,
		      unsigned int framerate,
		      snd_pcm_format_t fmt,
		      unsigned int channels);
void ad1843_shutdown_dac(struct snd_ad1843 *ad1843,
			 unsigned int id);
void ad1843_setup_adc(struct snd_ad1843 *ad1843,
		      unsigned int framerate,
		      snd_pcm_format_t fmt,
		      unsigned int channels);
void ad1843_shutdown_adc(struct snd_ad1843 *ad1843);
int ad1843_init(struct snd_ad1843 *ad1843);

#endif /* __SOUND_AD1843_H */
