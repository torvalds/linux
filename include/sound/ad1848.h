#ifndef __SOUND_AD1848_H
#define __SOUND_AD1848_H

/*
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 *  Definitions for AD1847/AD1848/CS4248 chips
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

#include "pcm.h"
#include <linux/interrupt.h>

/* IO ports */

#define AD1848P( chip, x ) ( (chip) -> port + c_d_c_AD1848##x )

#define c_d_c_AD1848REGSEL	0
#define c_d_c_AD1848REG		1
#define c_d_c_AD1848STATUS	2
#define c_d_c_AD1848PIO		3

/* codec registers */

#define AD1848_LEFT_INPUT	0x00	/* left input control */
#define AD1848_RIGHT_INPUT	0x01	/* right input control */
#define AD1848_AUX1_LEFT_INPUT	0x02	/* left AUX1 input control */
#define AD1848_AUX1_RIGHT_INPUT	0x03	/* right AUX1 input control */
#define AD1848_AUX2_LEFT_INPUT	0x04	/* left AUX2 input control */
#define AD1848_AUX2_RIGHT_INPUT	0x05	/* right AUX2 input control */
#define AD1848_LEFT_OUTPUT	0x06	/* left output control register */
#define AD1848_RIGHT_OUTPUT	0x07	/* right output control register */
#define AD1848_DATA_FORMAT	0x08	/* clock and data format - playback/capture - bits 7-0 MCE */
#define AD1848_IFACE_CTRL	0x09	/* interface control - bits 7-2 MCE */
#define AD1848_PIN_CTRL		0x0a	/* pin control */
#define AD1848_TEST_INIT	0x0b	/* test and initialization */
#define AD1848_MISC_INFO	0x0c	/* miscellaneaous information */
#define AD1848_LOOPBACK		0x0d	/* loopback control */
#define AD1848_DATA_UPR_CNT	0x0e	/* playback/capture upper base count */
#define AD1848_DATA_LWR_CNT	0x0f	/* playback/capture lower base count */

/* definitions for codec register select port - CODECP( REGSEL ) */

#define AD1848_INIT		0x80	/* CODEC is initializing */
#define AD1848_MCE		0x40	/* mode change enable */
#define AD1848_TRD		0x20	/* transfer request disable */

/* definitions for codec status register - CODECP( STATUS ) */

#define AD1848_GLOBALIRQ	0x01	/* IRQ is active */

/* definitions for AD1848_LEFT_INPUT and AD1848_RIGHT_INPUT registers */

#define AD1848_ENABLE_MIC_GAIN	0x20

#define AD1848_MIXS_LINE1	0x00
#define AD1848_MIXS_AUX1	0x40
#define AD1848_MIXS_LINE2	0x80
#define AD1848_MIXS_ALL		0xc0

/* definitions for clock and data format register - AD1848_PLAYBK_FORMAT */

#define AD1848_LINEAR_8		0x00	/* 8-bit unsigned data */
#define AD1848_ALAW_8		0x60	/* 8-bit A-law companded */
#define AD1848_ULAW_8		0x20	/* 8-bit U-law companded */
#define AD1848_LINEAR_16	0x40	/* 16-bit twos complement data - little endian */
#define AD1848_STEREO		0x10	/* stereo mode */
/* bits 3-1 define frequency divisor */
#define AD1848_XTAL1		0x00	/* 24.576 crystal */
#define AD1848_XTAL2		0x01	/* 16.9344 crystal */

/* definitions for interface control register - AD1848_IFACE_CTRL */

#define AD1848_CAPTURE_PIO	0x80	/* capture PIO enable */
#define AD1848_PLAYBACK_PIO	0x40	/* playback PIO enable */
#define AD1848_CALIB_MODE	0x18	/* calibration mode bits */
#define AD1848_AUTOCALIB	0x08	/* auto calibrate */
#define AD1848_SINGLE_DMA	0x04	/* use single DMA channel */
#define AD1848_CAPTURE_ENABLE	0x02	/* capture enable */
#define AD1848_PLAYBACK_ENABLE	0x01	/* playback enable */

/* definitions for pin control register - AD1848_PIN_CTRL */

#define AD1848_IRQ_ENABLE	0x02	/* enable IRQ */
#define AD1848_XCTL1		0x40	/* external control #1 */
#define AD1848_XCTL0		0x80	/* external control #0 */

/* definitions for test and init register - AD1848_TEST_INIT */

#define AD1848_CALIB_IN_PROGRESS 0x20	/* auto calibrate in progress */
#define AD1848_DMA_REQUEST	0x10	/* DMA request in progress */

/* defines for codec.mode */

#define AD1848_MODE_NONE	0x0000
#define AD1848_MODE_PLAY	0x0001
#define AD1848_MODE_CAPTURE	0x0002
#define AD1848_MODE_TIMER	0x0004
#define AD1848_MODE_OPEN	(AD1848_MODE_PLAY|AD1848_MODE_CAPTURE|AD1848_MODE_TIMER)
#define AD1848_MODE_RUNNING	0x0010

/* defines for codec.hardware */

#define AD1848_HW_DETECT	0x0000	/* let AD1848 driver detect chip */
#define AD1848_HW_AD1847	0x0001	/* AD1847 chip */
#define AD1848_HW_AD1848	0x0002	/* AD1848 chip */
#define AD1848_HW_CS4248	0x0003	/* CS4248 chip */
#define AD1848_HW_CMI8330	0x0004	/* CMI8330 chip */
#define AD1848_HW_THINKPAD	0x0005	/* Thinkpad 360/750/755 */

/* IBM Thinkpad specific stuff */
#define AD1848_THINKPAD_CTL_PORT1		0x15e8
#define AD1848_THINKPAD_CTL_PORT2		0x15e9
#define AD1848_THINKPAD_CS4248_ENABLE_BIT	0x02

struct snd_ad1848 {
	unsigned long port;		/* i/o port */
	struct resource *res_port;
	int irq;			/* IRQ line */
	int dma;			/* data DMA */
	unsigned short version;		/* version of CODEC chip */
	unsigned short mode;		/* see to AD1848_MODE_XXXX */
	unsigned short hardware;	/* see to AD1848_HW_XXXX */
	unsigned short single_dma:1;	/* forced single DMA mode (GUS 16-bit daughter board) or dma1 == dma2 */

	struct snd_pcm *pcm;
	struct snd_pcm_substream *playback_substream;
	struct snd_pcm_substream *capture_substream;
	struct snd_card *card;

	unsigned char image[32];	/* SGalaxy needs an access to extended registers */
	int mce_bit;
	int calibrate_mute;
	int dma_size;
	int thinkpad_flag;		/* Thinkpad CS4248 needs some extra help */

#ifdef CONFIG_PM
	void (*suspend)(struct snd_ad1848 *chip);
	void (*resume)(struct snd_ad1848 *chip);
#endif

	spinlock_t reg_lock;
};

/* exported functions */

void snd_ad1848_out(struct snd_ad1848 *chip, unsigned char reg, unsigned char value);

int snd_ad1848_create(struct snd_card *card,
		      unsigned long port,
		      int irq, int dma,
		      unsigned short hardware,
		      struct snd_ad1848 ** chip);

int snd_ad1848_pcm(struct snd_ad1848 * chip, int device, struct snd_pcm **rpcm);
const struct snd_pcm_ops *snd_ad1848_get_pcm_ops(int direction);
int snd_ad1848_mixer(struct snd_ad1848 * chip);

/* exported mixer stuffs */
enum { AD1848_MIX_SINGLE, AD1848_MIX_DOUBLE, AD1848_MIX_CAPTURE };

#define AD1848_MIXVAL_SINGLE(reg, shift, mask, invert) \
	((reg) | ((shift) << 8) | ((mask) << 16) | ((invert) << 24))
#define AD1848_MIXVAL_DOUBLE(left_reg, right_reg, shift_left, shift_right, mask, invert) \
	((left_reg) | ((right_reg) << 8) | ((shift_left) << 16) | ((shift_right) << 19) | ((mask) << 24) | ((invert) << 22))

/* for ease of use */
struct ad1848_mix_elem {
	const char *name;
	int index;
	int type;
	unsigned long private_value;
	const unsigned int *tlv;
};

#define AD1848_SINGLE(xname, xindex, reg, shift, mask, invert) \
{ .name = xname, \
  .index = xindex, \
  .type = AD1848_MIX_SINGLE, \
  .private_value = AD1848_MIXVAL_SINGLE(reg, shift, mask, invert) }

#define AD1848_SINGLE_TLV(xname, xindex, reg, shift, mask, invert, xtlv) \
{ .name = xname, \
  .index = xindex, \
  .type = AD1848_MIX_SINGLE, \
  .private_value = AD1848_MIXVAL_SINGLE(reg, shift, mask, invert), \
  .tlv = xtlv }

#define AD1848_DOUBLE(xname, xindex, left_reg, right_reg, shift_left, shift_right, mask, invert) \
{ .name = xname, \
  .index = xindex, \
  .type = AD1848_MIX_DOUBLE, \
  .private_value = AD1848_MIXVAL_DOUBLE(left_reg, right_reg, shift_left, shift_right, mask, invert) }

#define AD1848_DOUBLE_TLV(xname, xindex, left_reg, right_reg, shift_left, shift_right, mask, invert, xtlv) \
{ .name = xname, \
  .index = xindex, \
  .type = AD1848_MIX_DOUBLE, \
  .private_value = AD1848_MIXVAL_DOUBLE(left_reg, right_reg, shift_left, shift_right, mask, invert), \
  .tlv = xtlv }

int snd_ad1848_add_ctl_elem(struct snd_ad1848 *chip, const struct ad1848_mix_elem *c);

#endif /* __SOUND_AD1848_H */
