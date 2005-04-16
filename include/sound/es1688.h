#ifndef __SOUND_ES1688_H
#define __SOUND_ES1688_H

/*
 *  Header file for ES488/ES1688
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
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
#include <linux/interrupt.h>

#define ES1688_HW_AUTO		0x0000
#define ES1688_HW_688		0x0001
#define ES1688_HW_1688		0x0002

struct _snd_es1688 {
	unsigned long port;		/* port of ESS chip */
	struct resource *res_port;
	unsigned long mpu_port;		/* MPU-401 port of ESS chip */
	int irq;			/* IRQ number of ESS chip */
	int mpu_irq;			/* MPU IRQ */
	int dma8;			/* 8-bit DMA */
	unsigned short version;		/* version of ESS chip */
	unsigned short hardware;	/* see to ES1688_HW_XXXX */

	unsigned short trigger_value;
	unsigned char pad;
	unsigned int dma_size;

	snd_card_t *card;
	snd_pcm_t *pcm;
	snd_pcm_substream_t *playback_substream;
	snd_pcm_substream_t *capture_substream;

	spinlock_t reg_lock;
	spinlock_t mixer_lock;
};

typedef struct _snd_es1688 es1688_t;

/* I/O ports */

#define ES1688P(codec, x) ((codec)->port + e_s_s_ESS1688##x)

#define e_s_s_ESS1688RESET	0x6
#define e_s_s_ESS1688READ	0xa
#define e_s_s_ESS1688WRITE	0xc
#define e_s_s_ESS1688COMMAND	0xc
#define e_s_s_ESS1688STATUS	0xc
#define e_s_s_ESS1688DATA_AVAIL	0xe
#define e_s_s_ESS1688DATA_AVAIL_16 0xf
#define e_s_s_ESS1688MIXER_ADDR	0x4
#define e_s_s_ESS1688MIXER_DATA	0x5
#define e_s_s_ESS1688OPL3_LEFT	0x0
#define e_s_s_ESS1688OPL3_RIGHT	0x2
#define e_s_s_ESS1688OPL3_BOTH	0x8
#define e_s_s_ESS1688ENABLE0	0x0
#define e_s_s_ESS1688ENABLE1	0x9
#define e_s_s_ESS1688ENABLE2	0xb
#define e_s_s_ESS1688INIT1	0x7

#define ES1688_DSP_CMD_DMAOFF	0xd0
#define ES1688_DSP_CMD_SPKON	0xd1
#define ES1688_DSP_CMD_SPKOFF	0xd3
#define ES1688_DSP_CMD_DMAON	0xd4

#define ES1688_PCM_DEV		0x14
#define ES1688_MIC_DEV		0x1a
#define ES1688_REC_DEV		0x1c
#define ES1688_MASTER_DEV	0x32
#define ES1688_FM_DEV		0x36
#define ES1688_CD_DEV		0x38
#define ES1688_AUX_DEV		0x3a
#define ES1688_SPEAKER_DEV	0x3c
#define ES1688_LINE_DEV		0x3e
#define ES1688_RECLEV_DEV	0xb4

#define ES1688_MIXS_MASK	0x17
#define ES1688_MIXS_MIC		0x00
#define ES1688_MIXS_MIC_MASTER	0x01
#define ES1688_MIXS_CD		0x02
#define ES1688_MIXS_AOUT	0x03
#define ES1688_MIXS_MIC1	0x04
#define ES1688_MIXS_REC_MIX	0x05
#define ES1688_MIXS_LINE	0x06
#define ES1688_MIXS_MASTER	0x07
#define ES1688_MIXS_MUTE	0x10

/*

 */

void snd_es1688_mixer_write(es1688_t *chip, unsigned char reg, unsigned char data);

int snd_es1688_create(snd_card_t * card,
		      unsigned long port,
		      unsigned long mpu_port,
		      int irq,
		      int mpu_irq,
		      int dma8,
		      unsigned short hardware,
		      es1688_t ** rchip);
int snd_es1688_pcm(es1688_t *chip, int device, snd_pcm_t ** rpcm);
int snd_es1688_mixer(es1688_t *chip);

#endif /* __SOUND_ES1688_H */
