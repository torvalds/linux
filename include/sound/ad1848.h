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

#include "wss.h"	/* temporary till the driver is removed */

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
#define AD1848_MISC_INFO	0x0c	/* miscellaneous information */
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

/* IBM Thinkpad specific stuff */
#define AD1848_THINKPAD_CTL_PORT1		0x15e8
#define AD1848_THINKPAD_CTL_PORT2		0x15e9
#define AD1848_THINKPAD_CS4248_ENABLE_BIT	0x02

/* exported functions */

void snd_ad1848_out(struct snd_wss *chip, unsigned char reg,
		    unsigned char value);

int snd_ad1848_create(struct snd_card *card,
		      unsigned long port,
		      int irq, int dma,
		      unsigned short hardware,
		      struct snd_wss **chip);

int snd_ad1848_pcm(struct snd_wss *chip, int device, struct snd_pcm **rpcm);
const struct snd_pcm_ops *snd_ad1848_get_pcm_ops(int direction);
int snd_ad1848_mixer(struct snd_wss *chip);

#endif /* __SOUND_AD1848_H */
