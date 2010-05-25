/*
 * Driver for Sound Cors PDAudioCF soundcard
 *
 * Copyright (c) 2003 by Jaroslav Kysela <perex@perex.cz>
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
 */

#ifndef __PDAUDIOCF_H
#define __PDAUDIOCF_H

#include <sound/pcm.h>
#include <asm/io.h>
#include <linux/interrupt.h>
#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/ds.h>

#include <sound/ak4117.h>

/* PDAUDIOCF registers */
#define PDAUDIOCF_REG_MD	0x00	/* music data, R/O */
#define PDAUDIOCF_REG_WDP	0x02	/* write data pointer / 2, R/O */
#define PDAUDIOCF_REG_RDP	0x04	/* read data pointer / 2, R/O */
#define PDAUDIOCF_REG_TCR	0x06	/* test control register W/O */
#define PDAUDIOCF_REG_SCR	0x08	/* status and control, R/W (see bit description) */
#define PDAUDIOCF_REG_ISR	0x0a	/* interrupt status, R/O */
#define PDAUDIOCF_REG_IER	0x0c	/* interrupt enable, R/W */
#define PDAUDIOCF_REG_AK_IFR	0x0e	/* AK interface register, R/W */

/* PDAUDIOCF_REG_TCR */
#define PDAUDIOCF_ELIMAKMBIT	(1<<0)	/* simulate AKM music data */
#define PDAUDIOCF_TESTDATASEL	(1<<1)	/* test data selection, 0 = 0x55, 1 = pseudo-random */

/* PDAUDIOCF_REG_SCR */
#define PDAUDIOCF_AK_SBP	(1<<0)	/* serial port busy flag */
#define PDAUDIOCF_RST		(1<<2)	/* FPGA, AKM + SRAM buffer reset */
#define PDAUDIOCF_PDN		(1<<3)	/* power down bit */
#define PDAUDIOCF_CLKDIV0	(1<<4)	/* choose 24.576Mhz clock divided by 1,2,3 or 4 */
#define PDAUDIOCF_CLKDIV1	(1<<5)
#define PDAUDIOCF_RECORD	(1<<6)	/* start capturing to SRAM */
#define PDAUDIOCF_AK_SDD	(1<<7)	/* music data detected */
#define PDAUDIOCF_RED_LED_OFF	(1<<8)	/* red LED off override */
#define PDAUDIOCF_BLUE_LED_OFF	(1<<9)	/* blue LED off override */
#define PDAUDIOCF_DATAFMT0	(1<<10)	/* data format bits: 00 = 16-bit, 01 = 18-bit */
#define PDAUDIOCF_DATAFMT1	(1<<11)	/* 10 = 20-bit, 11 = 24-bit, all right justified */
#define PDAUDIOCF_FPGAREV(x)	((x>>12)&0x0f) /* FPGA revision */

/* PDAUDIOCF_REG_ISR */
#define PDAUDIOCF_IRQLVL	(1<<0)	/* Buffer level IRQ */
#define PDAUDIOCF_IRQOVR	(1<<1)	/* Overrun IRQ */
#define PDAUDIOCF_IRQAKM	(1<<2)	/* AKM IRQ */

/* PDAUDIOCF_REG_IER */
#define PDAUDIOCF_IRQLVLEN0	(1<<0)	/* fill threshold levels; 00 = none, 01 = 1/8th of buffer */
#define PDAUDIOCF_IRQLVLEN1	(1<<1)	/* 10 = 1/4th of buffer, 11 = 1/2th of buffer */
#define PDAUDIOCF_IRQOVREN	(1<<2)	/* enable overrun IRQ */
#define PDAUDIOCF_IRQAKMEN	(1<<3)	/* enable AKM IRQ */
#define PDAUDIOCF_BLUEDUTY0	(1<<8)	/* blue LED duty cycle; 00 = 100%, 01 = 50% */
#define PDAUDIOCF_BLUEDUTY1	(1<<9)	/* 02 = 25%, 11 = 12% */
#define PDAUDIOCF_REDDUTY0	(1<<10)	/* red LED duty cycle; 00 = 100%, 01 = 50% */
#define PDAUDIOCF_REDDUTY1	(1<<11)	/* 02 = 25%, 11 = 12% */
#define PDAUDIOCF_BLUESDD	(1<<12)	/* blue LED against SDD bit */
#define PDAUDIOCF_BLUEMODULATE	(1<<13)	/* save power when 100% duty cycle selected */
#define PDAUDIOCF_REDMODULATE	(1<<14)	/* save power when 100% duty cycle selected */
#define PDAUDIOCF_HALFRATE	(1<<15)	/* slow both LED blinks by half (also spdif detect rate) */

/* chip status */
#define PDAUDIOCF_STAT_IS_STALE	(1<<0)
#define PDAUDIOCF_STAT_IS_CONFIGURED (1<<1)
#define PDAUDIOCF_STAT_IS_SUSPENDED (1<<2)

struct snd_pdacf {
	struct snd_card *card;
	int index;

	unsigned long port;
	int irq;

	spinlock_t reg_lock;
	unsigned short regmap[8];
	unsigned short suspend_reg_scr;
	struct tasklet_struct tq;

	spinlock_t ak4117_lock;
	struct ak4117 *ak4117;

	unsigned int chip_status;

	struct snd_pcm *pcm;
	struct snd_pcm_substream *pcm_substream;
	unsigned int pcm_running: 1;
	unsigned int pcm_channels;
	unsigned int pcm_swab;
	unsigned int pcm_little;
	unsigned int pcm_frame;
	unsigned int pcm_sample;
	unsigned int pcm_xor;
	unsigned int pcm_size;
	unsigned int pcm_period;
	unsigned int pcm_tdone;
	unsigned int pcm_hwptr;
	void *pcm_area;
	
	/* pcmcia stuff */
	struct pcmcia_device	*p_dev;
};

static inline void pdacf_reg_write(struct snd_pdacf *chip, unsigned char reg, unsigned short val)
{
	outw(chip->regmap[reg>>1] = val, chip->port + reg);
}

static inline unsigned short pdacf_reg_read(struct snd_pdacf *chip, unsigned char reg)
{
	return inw(chip->port + reg);
}

struct snd_pdacf *snd_pdacf_create(struct snd_card *card);
int snd_pdacf_ak4117_create(struct snd_pdacf *pdacf);
void snd_pdacf_powerdown(struct snd_pdacf *chip);
#ifdef CONFIG_PM
int snd_pdacf_suspend(struct snd_pdacf *chip, pm_message_t state);
int snd_pdacf_resume(struct snd_pdacf *chip);
#endif
int snd_pdacf_pcm_new(struct snd_pdacf *chip);
irqreturn_t pdacf_interrupt(int irq, void *dev);
void pdacf_tasklet(unsigned long private_data);
void pdacf_reinit(struct snd_pdacf *chip, int resume);

#endif /* __PDAUDIOCF_H */
