#ifndef __SOUND_AK4XXX_ADDA_H
#define __SOUND_AK4XXX_ADDA_H

/*
 *   ALSA driver for AK4524 / AK4528 / AK4529 / AK4355 / AK4381
 *   AD and DA converters
 *
 *	Copyright (c) 2000 Jaroslav Kysela <perex@suse.cz>
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

#ifndef AK4XXX_MAX_CHIPS
#define AK4XXX_MAX_CHIPS	4
#endif

struct snd_akm4xxx;

struct snd_ak4xxx_ops {
	void (*lock)(struct snd_akm4xxx *ak, int chip);
	void (*unlock)(struct snd_akm4xxx *ak, int chip);
	void (*write)(struct snd_akm4xxx *ak, int chip, unsigned char reg, unsigned char val);
	// unsigned char (*read)(struct snd_akm4xxx *ak, int chip, unsigned char reg);
	void (*set_rate_val)(struct snd_akm4xxx *ak, unsigned int rate);
};

#define AK4XXX_IMAGE_SIZE	(AK4XXX_MAX_CHIPS * 16)	/* 64 bytes */

struct snd_akm4xxx {
	struct snd_card *card;
	unsigned int num_adcs;				/* AK4524 or AK4528 ADCs */
	unsigned int num_dacs;				/* AK4524 or AK4528 DACs */
	unsigned char images[AK4XXX_IMAGE_SIZE];	/* saved register image */
	unsigned char ipga_gain[AK4XXX_MAX_CHIPS][2];	/* saved register image for IPGA (AK4528) */
	unsigned long private_value[AK4XXX_MAX_CHIPS];	/* helper for driver */
	void *private_data[AK4XXX_MAX_CHIPS];		/* helper for driver */
	/* template should fill the following fields */
	unsigned int idx_offset;			/* control index offset */
	enum {
		SND_AK4524, SND_AK4528, SND_AK4529,
		SND_AK4355, SND_AK4358, SND_AK4381
	} type;
	struct snd_ak4xxx_ops ops;
};

void snd_akm4xxx_write(struct snd_akm4xxx *ak, int chip, unsigned char reg, unsigned char val);
void snd_akm4xxx_reset(struct snd_akm4xxx *ak, int state);
void snd_akm4xxx_init(struct snd_akm4xxx *ak);
int snd_akm4xxx_build_controls(struct snd_akm4xxx *ak);

#define snd_akm4xxx_get(ak,chip,reg) (ak)->images[(chip) * 16 + (reg)]
#define snd_akm4xxx_set(ak,chip,reg,val) ((ak)->images[(chip) * 16 + (reg)] = (val))
#define snd_akm4xxx_get_ipga(ak,chip,reg) (ak)->ipga_gain[chip][(reg)-4]
#define snd_akm4xxx_set_ipga(ak,chip,reg,val) ((ak)->ipga_gain[chip][(reg)-4] = (val))

#endif /* __SOUND_AK4XXX_ADDA_H */
