#ifndef __SOUND_CS4231_H
#define __SOUND_CS4231_H

/*
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 *  Definitions for CS4231 & InterWave chips & compatible chips
 *
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

#include "control.h"
#include "pcm.h"
#include "timer.h"

#include "cs4231-regs.h"

/* defines for codec.mode */

#define CS4231_MODE_NONE	0x0000
#define CS4231_MODE_PLAY	0x0001
#define CS4231_MODE_RECORD	0x0002
#define CS4231_MODE_TIMER	0x0004
#define CS4231_MODE_OPEN	(CS4231_MODE_PLAY|CS4231_MODE_RECORD|CS4231_MODE_TIMER)

/* defines for codec.hardware */

#define CS4231_HW_DETECT        0x0000	/* let CS4231 driver detect chip */
#define CS4231_HW_DETECT3	0x0001	/* allow mode 3 */
#define CS4231_HW_TYPE_MASK	0xff00	/* type mask */
#define CS4231_HW_CS4231_MASK   0x0100	/* CS4231 serie */
#define CS4231_HW_CS4231        0x0100	/* CS4231 chip */
#define CS4231_HW_CS4231A       0x0101	/* CS4231A chip */
#define CS4231_HW_AD1845	0x0102	/* AD1845 chip */
#define CS4231_HW_CS4232_MASK   0x0200	/* CS4232 serie (has control ports) */
#define CS4231_HW_CS4232        0x0200	/* CS4232 */
#define CS4231_HW_CS4232A       0x0201	/* CS4232A */
#define CS4231_HW_CS4236	0x0202	/* CS4236 */
#define CS4231_HW_CS4236B_MASK	0x0400	/* CS4236B serie (has extended control regs) */
#define CS4231_HW_CS4235	0x0400	/* CS4235 - Crystal Clear (tm) stereo enhancement */
#define CS4231_HW_CS4236B       0x0401	/* CS4236B */
#define CS4231_HW_CS4237B       0x0402	/* CS4237B - SRS 3D */
#define CS4231_HW_CS4238B	0x0403	/* CS4238B - QSOUND 3D */
#define CS4231_HW_CS4239	0x0404	/* CS4239 - Crystal Clear (tm) stereo enhancement */
/* compatible, but clones */
#define CS4231_HW_INTERWAVE     0x1000	/* InterWave chip */
#define CS4231_HW_OPL3SA2       0x1101	/* OPL3-SA2 chip, similar to cs4231 */

/* defines for codec.hwshare */
#define CS4231_HWSHARE_IRQ	(1<<0)
#define CS4231_HWSHARE_DMA1	(1<<1)
#define CS4231_HWSHARE_DMA2	(1<<2)

struct snd_cs4231 {
	unsigned long port;		/* base i/o port */
	struct resource *res_port;
	unsigned long cport;		/* control base i/o port (CS4236) */
	struct resource *res_cport;
	int irq;			/* IRQ line */
	int dma1;			/* playback DMA */
	int dma2;			/* record DMA */
	unsigned short version;		/* version of CODEC chip */
	unsigned short mode;		/* see to CS4231_MODE_XXXX */
	unsigned short hardware;	/* see to CS4231_HW_XXXX */
	unsigned short hwshare;		/* shared resources */
	unsigned short single_dma:1,	/* forced single DMA mode (GUS 16-bit daughter board) or dma1 == dma2 */
		       ebus_flag:1;	/* SPARC: EBUS present */

	struct snd_card *card;
	struct snd_pcm *pcm;
	struct snd_pcm_substream *playback_substream;
	struct snd_pcm_substream *capture_substream;
	struct snd_timer *timer;

	unsigned char image[32];	/* registers image */
	unsigned char eimage[32];	/* extended registers image */
	unsigned char cimage[16];	/* control registers image */
	int mce_bit;
	int calibrate_mute;
	int sw_3d_bit;
	unsigned int p_dma_size;
	unsigned int c_dma_size;

	spinlock_t reg_lock;
	struct mutex mce_mutex;
	struct mutex open_mutex;

	int (*rate_constraint) (struct snd_pcm_runtime *runtime);
	void (*set_playback_format) (struct snd_cs4231 *chip, struct snd_pcm_hw_params *hw_params, unsigned char pdfr);
	void (*set_capture_format) (struct snd_cs4231 *chip, struct snd_pcm_hw_params *hw_params, unsigned char cdfr);
	void (*trigger) (struct snd_cs4231 *chip, unsigned int what, int start);
#ifdef CONFIG_PM
	void (*suspend) (struct snd_cs4231 *chip);
	void (*resume) (struct snd_cs4231 *chip);
#endif
	void *dma_private_data;
	int (*claim_dma) (struct snd_cs4231 *chip, void *dma_private_data, int dma);
	int (*release_dma) (struct snd_cs4231 *chip, void *dma_private_data, int dma);
};

/* exported functions */

void snd_cs4231_out(struct snd_cs4231 *chip, unsigned char reg, unsigned char val);
unsigned char snd_cs4231_in(struct snd_cs4231 *chip, unsigned char reg);
void snd_cs4236_ext_out(struct snd_cs4231 *chip, unsigned char reg, unsigned char val);
unsigned char snd_cs4236_ext_in(struct snd_cs4231 *chip, unsigned char reg);
void snd_cs4231_mce_up(struct snd_cs4231 *chip);
void snd_cs4231_mce_down(struct snd_cs4231 *chip);

irqreturn_t snd_cs4231_interrupt(int irq, void *dev_id);

const char *snd_cs4231_chip_id(struct snd_cs4231 *chip);

int snd_cs4231_create(struct snd_card *card,
		      unsigned long port,
		      unsigned long cport,
		      int irq, int dma1, int dma2,
		      unsigned short hardware,
		      unsigned short hwshare,
		      struct snd_cs4231 ** rchip);
int snd_cs4231_pcm(struct snd_cs4231 * chip, int device, struct snd_pcm **rpcm);
int snd_cs4231_timer(struct snd_cs4231 * chip, int device, struct snd_timer **rtimer);
int snd_cs4231_mixer(struct snd_cs4231 * chip);

int snd_cs4236_create(struct snd_card *card,
		      unsigned long port,
		      unsigned long cport,
		      int irq, int dma1, int dma2,
		      unsigned short hardware,
		      unsigned short hwshare,
		      struct snd_cs4231 ** rchip);
int snd_cs4236_pcm(struct snd_cs4231 * chip, int device, struct snd_pcm **rpcm);
int snd_cs4236_mixer(struct snd_cs4231 * chip);

/*
 *  mixer library
 */

#define CS4231_SINGLE(xname, xindex, reg, shift, mask, invert) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = xindex, \
  .info = snd_cs4231_info_single, \
  .get = snd_cs4231_get_single, .put = snd_cs4231_put_single, \
  .private_value = reg | (shift << 8) | (mask << 16) | (invert << 24) }

int snd_cs4231_info_single(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo);
int snd_cs4231_get_single(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol);
int snd_cs4231_put_single(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol);

#define CS4231_DOUBLE(xname, xindex, left_reg, right_reg, shift_left, shift_right, mask, invert) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = xindex, \
  .info = snd_cs4231_info_double, \
  .get = snd_cs4231_get_double, .put = snd_cs4231_put_double, \
  .private_value = left_reg | (right_reg << 8) | (shift_left << 16) | (shift_right << 19) | (mask << 24) | (invert << 22) }

int snd_cs4231_info_double(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo);
int snd_cs4231_get_double(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol);
int snd_cs4231_put_double(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol);

#endif /* __SOUND_CS4231_H */
