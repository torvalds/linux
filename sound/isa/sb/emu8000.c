/*
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *     and (c) 1999 Steve Ratcliffe <steve@parabola.demon.co.uk>
 *  Copyright (C) 1999-2000 Takashi Iwai <tiwai@suse.de>
 *
 *  Routines for control of EMU8000 chip
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

#include <sound/driver.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <sound/core.h>
#include <sound/emu8000.h>
#include <sound/emu8000_reg.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <linux/init.h>
#include <sound/control.h>
#include <sound/initval.h>

/*
 * emu8000 register controls
 */

/*
 * The following routines read and write registers on the emu8000.  They
 * should always be called via the EMU8000*READ/WRITE macros and never
 * directly.  The macros handle the port number and command word.
 */
/* Write a word */
void snd_emu8000_poke(emu8000_t *emu, unsigned int port, unsigned int reg, unsigned int val)
{
	unsigned long flags;
	spin_lock_irqsave(&emu->reg_lock, flags);
	if (reg != emu->last_reg) {
		outw((unsigned short)reg, EMU8000_PTR(emu)); /* Set register */
		emu->last_reg = reg;
	}
	outw((unsigned short)val, port); /* Send data */
	spin_unlock_irqrestore(&emu->reg_lock, flags);
}

/* Read a word */
unsigned short snd_emu8000_peek(emu8000_t *emu, unsigned int port, unsigned int reg)
{
	unsigned short res;
	unsigned long flags;
	spin_lock_irqsave(&emu->reg_lock, flags);
	if (reg != emu->last_reg) {
		outw((unsigned short)reg, EMU8000_PTR(emu)); /* Set register */
		emu->last_reg = reg;
	}
	res = inw(port);	/* Read data */
	spin_unlock_irqrestore(&emu->reg_lock, flags);
	return res;
}

/* Write a double word */
void snd_emu8000_poke_dw(emu8000_t *emu, unsigned int port, unsigned int reg, unsigned int val)
{
	unsigned long flags;
	spin_lock_irqsave(&emu->reg_lock, flags);
	if (reg != emu->last_reg) {
		outw((unsigned short)reg, EMU8000_PTR(emu)); /* Set register */
		emu->last_reg = reg;
	}
	outw((unsigned short)val, port); /* Send low word of data */
	outw((unsigned short)(val>>16), port+2); /* Send high word of data */
	spin_unlock_irqrestore(&emu->reg_lock, flags);
}

/* Read a double word */
unsigned int snd_emu8000_peek_dw(emu8000_t *emu, unsigned int port, unsigned int reg)
{
	unsigned short low;
	unsigned int res;
	unsigned long flags;
	spin_lock_irqsave(&emu->reg_lock, flags);
	if (reg != emu->last_reg) {
		outw((unsigned short)reg, EMU8000_PTR(emu)); /* Set register */
		emu->last_reg = reg;
	}
	low = inw(port);	/* Read low word of data */
	res = low + (inw(port+2) << 16);
	spin_unlock_irqrestore(&emu->reg_lock, flags);
	return res;
}

/*
 * Set up / close a channel to be used for DMA.
 */
/*exported*/ void
snd_emu8000_dma_chan(emu8000_t *emu, int ch, int mode)
{
	unsigned right_bit = (mode & EMU8000_RAM_RIGHT) ? 0x01000000 : 0;
	mode &= EMU8000_RAM_MODE_MASK;
	if (mode == EMU8000_RAM_CLOSE) {
		EMU8000_CCCA_WRITE(emu, ch, 0);
		EMU8000_DCYSUSV_WRITE(emu, ch, 0x807F);
		return;
	}
	EMU8000_DCYSUSV_WRITE(emu, ch, 0x80);
	EMU8000_VTFT_WRITE(emu, ch, 0);
	EMU8000_CVCF_WRITE(emu, ch, 0);
	EMU8000_PTRX_WRITE(emu, ch, 0x40000000);
	EMU8000_CPF_WRITE(emu, ch, 0x40000000);
	EMU8000_PSST_WRITE(emu, ch, 0);
	EMU8000_CSL_WRITE(emu, ch, 0);
	if (mode == EMU8000_RAM_WRITE) /* DMA write */
		EMU8000_CCCA_WRITE(emu, ch, 0x06000000 | right_bit);
	else	   /* DMA read */
		EMU8000_CCCA_WRITE(emu, ch, 0x04000000 | right_bit);
}

/*
 */
static void __init
snd_emu8000_read_wait(emu8000_t *emu)
{
	while ((EMU8000_SMALR_READ(emu) & 0x80000000) != 0) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(1);
		if (signal_pending(current))
			break;
	}
}

/*
 */
static void __init
snd_emu8000_write_wait(emu8000_t *emu)
{
	while ((EMU8000_SMALW_READ(emu) & 0x80000000) != 0) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(1);
		if (signal_pending(current))
			break;
	}
}

/*
 * detect a card at the given port
 */
static int __init
snd_emu8000_detect(emu8000_t *emu)
{
	/* Initialise */
	EMU8000_HWCF1_WRITE(emu, 0x0059);
	EMU8000_HWCF2_WRITE(emu, 0x0020);
	EMU8000_HWCF3_WRITE(emu, 0x0000);
	/* Check for a recognisable emu8000 */
	/*
	if ((EMU8000_U1_READ(emu) & 0x000f) != 0x000c)
		return -ENODEV;
		*/
	if ((EMU8000_HWCF1_READ(emu) & 0x007e) != 0x0058)
		return -ENODEV;
	if ((EMU8000_HWCF2_READ(emu) & 0x0003) != 0x0003)
		return -ENODEV;

	snd_printdd("EMU8000 [0x%lx]: Synth chip found\n",
                    emu->port1);
	return 0;
}


/*
 * intiailize audio channels
 */
static void __init
init_audio(emu8000_t *emu)
{
	int ch;

	/* turn off envelope engines */
	for (ch = 0; ch < EMU8000_CHANNELS; ch++)
		EMU8000_DCYSUSV_WRITE(emu, ch, 0x80);
  
	/* reset all other parameters to zero */
	for (ch = 0; ch < EMU8000_CHANNELS; ch++) {
		EMU8000_ENVVOL_WRITE(emu, ch, 0);
		EMU8000_ENVVAL_WRITE(emu, ch, 0);
		EMU8000_DCYSUS_WRITE(emu, ch, 0);
		EMU8000_ATKHLDV_WRITE(emu, ch, 0);
		EMU8000_LFO1VAL_WRITE(emu, ch, 0);
		EMU8000_ATKHLD_WRITE(emu, ch, 0);
		EMU8000_LFO2VAL_WRITE(emu, ch, 0);
		EMU8000_IP_WRITE(emu, ch, 0);
		EMU8000_IFATN_WRITE(emu, ch, 0);
		EMU8000_PEFE_WRITE(emu, ch, 0);
		EMU8000_FMMOD_WRITE(emu, ch, 0);
		EMU8000_TREMFRQ_WRITE(emu, ch, 0);
		EMU8000_FM2FRQ2_WRITE(emu, ch, 0);
		EMU8000_PTRX_WRITE(emu, ch, 0);
		EMU8000_VTFT_WRITE(emu, ch, 0);
		EMU8000_PSST_WRITE(emu, ch, 0);
		EMU8000_CSL_WRITE(emu, ch, 0);
		EMU8000_CCCA_WRITE(emu, ch, 0);
	}

	for (ch = 0; ch < EMU8000_CHANNELS; ch++) {
		EMU8000_CPF_WRITE(emu, ch, 0);
		EMU8000_CVCF_WRITE(emu, ch, 0);
	}
}


/*
 * initialize DMA address
 */
static void __init
init_dma(emu8000_t *emu)
{
	EMU8000_SMALR_WRITE(emu, 0);
	EMU8000_SMARR_WRITE(emu, 0);
	EMU8000_SMALW_WRITE(emu, 0);
	EMU8000_SMARW_WRITE(emu, 0);
}

/*
 * initialization arrays; from ADIP
 */
static unsigned short init1[128] /*__devinitdata*/ = {
	0x03ff, 0x0030,  0x07ff, 0x0130, 0x0bff, 0x0230,  0x0fff, 0x0330,
	0x13ff, 0x0430,  0x17ff, 0x0530, 0x1bff, 0x0630,  0x1fff, 0x0730,
	0x23ff, 0x0830,  0x27ff, 0x0930, 0x2bff, 0x0a30,  0x2fff, 0x0b30,
	0x33ff, 0x0c30,  0x37ff, 0x0d30, 0x3bff, 0x0e30,  0x3fff, 0x0f30,

	0x43ff, 0x0030,  0x47ff, 0x0130, 0x4bff, 0x0230,  0x4fff, 0x0330,
	0x53ff, 0x0430,  0x57ff, 0x0530, 0x5bff, 0x0630,  0x5fff, 0x0730,
	0x63ff, 0x0830,  0x67ff, 0x0930, 0x6bff, 0x0a30,  0x6fff, 0x0b30,
	0x73ff, 0x0c30,  0x77ff, 0x0d30, 0x7bff, 0x0e30,  0x7fff, 0x0f30,

	0x83ff, 0x0030,  0x87ff, 0x0130, 0x8bff, 0x0230,  0x8fff, 0x0330,
	0x93ff, 0x0430,  0x97ff, 0x0530, 0x9bff, 0x0630,  0x9fff, 0x0730,
	0xa3ff, 0x0830,  0xa7ff, 0x0930, 0xabff, 0x0a30,  0xafff, 0x0b30,
	0xb3ff, 0x0c30,  0xb7ff, 0x0d30, 0xbbff, 0x0e30,  0xbfff, 0x0f30,

	0xc3ff, 0x0030,  0xc7ff, 0x0130, 0xcbff, 0x0230,  0xcfff, 0x0330,
	0xd3ff, 0x0430,  0xd7ff, 0x0530, 0xdbff, 0x0630,  0xdfff, 0x0730,
	0xe3ff, 0x0830,  0xe7ff, 0x0930, 0xebff, 0x0a30,  0xefff, 0x0b30,
	0xf3ff, 0x0c30,  0xf7ff, 0x0d30, 0xfbff, 0x0e30,  0xffff, 0x0f30,
};

static unsigned short init2[128] /*__devinitdata*/ = {
	0x03ff, 0x8030, 0x07ff, 0x8130, 0x0bff, 0x8230, 0x0fff, 0x8330,
	0x13ff, 0x8430, 0x17ff, 0x8530, 0x1bff, 0x8630, 0x1fff, 0x8730,
	0x23ff, 0x8830, 0x27ff, 0x8930, 0x2bff, 0x8a30, 0x2fff, 0x8b30,
	0x33ff, 0x8c30, 0x37ff, 0x8d30, 0x3bff, 0x8e30, 0x3fff, 0x8f30,

	0x43ff, 0x8030, 0x47ff, 0x8130, 0x4bff, 0x8230, 0x4fff, 0x8330,
	0x53ff, 0x8430, 0x57ff, 0x8530, 0x5bff, 0x8630, 0x5fff, 0x8730,
	0x63ff, 0x8830, 0x67ff, 0x8930, 0x6bff, 0x8a30, 0x6fff, 0x8b30,
	0x73ff, 0x8c30, 0x77ff, 0x8d30, 0x7bff, 0x8e30, 0x7fff, 0x8f30,

	0x83ff, 0x8030, 0x87ff, 0x8130, 0x8bff, 0x8230, 0x8fff, 0x8330,
	0x93ff, 0x8430, 0x97ff, 0x8530, 0x9bff, 0x8630, 0x9fff, 0x8730,
	0xa3ff, 0x8830, 0xa7ff, 0x8930, 0xabff, 0x8a30, 0xafff, 0x8b30,
	0xb3ff, 0x8c30, 0xb7ff, 0x8d30, 0xbbff, 0x8e30, 0xbfff, 0x8f30,

	0xc3ff, 0x8030, 0xc7ff, 0x8130, 0xcbff, 0x8230, 0xcfff, 0x8330,
	0xd3ff, 0x8430, 0xd7ff, 0x8530, 0xdbff, 0x8630, 0xdfff, 0x8730,
	0xe3ff, 0x8830, 0xe7ff, 0x8930, 0xebff, 0x8a30, 0xefff, 0x8b30,
	0xf3ff, 0x8c30, 0xf7ff, 0x8d30, 0xfbff, 0x8e30, 0xffff, 0x8f30,
};

static unsigned short init3[128] /*__devinitdata*/ = {
	0x0C10, 0x8470, 0x14FE, 0xB488, 0x167F, 0xA470, 0x18E7, 0x84B5,
	0x1B6E, 0x842A, 0x1F1D, 0x852A, 0x0DA3, 0x8F7C, 0x167E, 0xF254,
	0x0000, 0x842A, 0x0001, 0x852A, 0x18E6, 0x8BAA, 0x1B6D, 0xF234,
	0x229F, 0x8429, 0x2746, 0x8529, 0x1F1C, 0x86E7, 0x229E, 0xF224,

	0x0DA4, 0x8429, 0x2C29, 0x8529, 0x2745, 0x87F6, 0x2C28, 0xF254,
	0x383B, 0x8428, 0x320F, 0x8528, 0x320E, 0x8F02, 0x1341, 0xF264,
	0x3EB6, 0x8428, 0x3EB9, 0x8528, 0x383A, 0x8FA9, 0x3EB5, 0xF294,
	0x3EB7, 0x8474, 0x3EBA, 0x8575, 0x3EB8, 0xC4C3, 0x3EBB, 0xC5C3,

	0x0000, 0xA404, 0x0001, 0xA504, 0x141F, 0x8671, 0x14FD, 0x8287,
	0x3EBC, 0xE610, 0x3EC8, 0x8C7B, 0x031A, 0x87E6, 0x3EC8, 0x86F7,
	0x3EC0, 0x821E, 0x3EBE, 0xD208, 0x3EBD, 0x821F, 0x3ECA, 0x8386,
	0x3EC1, 0x8C03, 0x3EC9, 0x831E, 0x3ECA, 0x8C4C, 0x3EBF, 0x8C55,

	0x3EC9, 0xC208, 0x3EC4, 0xBC84, 0x3EC8, 0x8EAD, 0x3EC8, 0xD308,
	0x3EC2, 0x8F7E, 0x3ECB, 0x8219, 0x3ECB, 0xD26E, 0x3EC5, 0x831F,
	0x3EC6, 0xC308, 0x3EC3, 0xB2FF, 0x3EC9, 0x8265, 0x3EC9, 0x8319,
	0x1342, 0xD36E, 0x3EC7, 0xB3FF, 0x0000, 0x8365, 0x1420, 0x9570,
};

static unsigned short init4[128] /*__devinitdata*/ = {
	0x0C10, 0x8470, 0x14FE, 0xB488, 0x167F, 0xA470, 0x18E7, 0x84B5,
	0x1B6E, 0x842A, 0x1F1D, 0x852A, 0x0DA3, 0x0F7C, 0x167E, 0x7254,
	0x0000, 0x842A, 0x0001, 0x852A, 0x18E6, 0x0BAA, 0x1B6D, 0x7234,
	0x229F, 0x8429, 0x2746, 0x8529, 0x1F1C, 0x06E7, 0x229E, 0x7224,

	0x0DA4, 0x8429, 0x2C29, 0x8529, 0x2745, 0x07F6, 0x2C28, 0x7254,
	0x383B, 0x8428, 0x320F, 0x8528, 0x320E, 0x0F02, 0x1341, 0x7264,
	0x3EB6, 0x8428, 0x3EB9, 0x8528, 0x383A, 0x0FA9, 0x3EB5, 0x7294,
	0x3EB7, 0x8474, 0x3EBA, 0x8575, 0x3EB8, 0x44C3, 0x3EBB, 0x45C3,

	0x0000, 0xA404, 0x0001, 0xA504, 0x141F, 0x0671, 0x14FD, 0x0287,
	0x3EBC, 0xE610, 0x3EC8, 0x0C7B, 0x031A, 0x07E6, 0x3EC8, 0x86F7,
	0x3EC0, 0x821E, 0x3EBE, 0xD208, 0x3EBD, 0x021F, 0x3ECA, 0x0386,
	0x3EC1, 0x0C03, 0x3EC9, 0x031E, 0x3ECA, 0x8C4C, 0x3EBF, 0x0C55,

	0x3EC9, 0xC208, 0x3EC4, 0xBC84, 0x3EC8, 0x0EAD, 0x3EC8, 0xD308,
	0x3EC2, 0x8F7E, 0x3ECB, 0x0219, 0x3ECB, 0xD26E, 0x3EC5, 0x031F,
	0x3EC6, 0xC308, 0x3EC3, 0x32FF, 0x3EC9, 0x0265, 0x3EC9, 0x8319,
	0x1342, 0xD36E, 0x3EC7, 0x33FF, 0x0000, 0x8365, 0x1420, 0x9570,
};

/* send an initialization array
 * Taken from the oss driver, not obvious from the doc how this
 * is meant to work
 */
static void __init
send_array(emu8000_t *emu, unsigned short *data, int size)
{
	int i;
	unsigned short *p;

	p = data;
	for (i = 0; i < size; i++, p++)
		EMU8000_INIT1_WRITE(emu, i, *p);
	for (i = 0; i < size; i++, p++)
		EMU8000_INIT2_WRITE(emu, i, *p);
	for (i = 0; i < size; i++, p++)
		EMU8000_INIT3_WRITE(emu, i, *p);
	for (i = 0; i < size; i++, p++)
		EMU8000_INIT4_WRITE(emu, i, *p);
}


/*
 * Send initialization arrays to start up, this just follows the
 * initialisation sequence in the adip.
 */
static void __init
init_arrays(emu8000_t *emu)
{
	send_array(emu, init1, ARRAY_SIZE(init1)/4);

	msleep((1024 * 1000) / 44100); /* wait for 1024 clocks */
	send_array(emu, init2, ARRAY_SIZE(init2)/4);
	send_array(emu, init3, ARRAY_SIZE(init3)/4);

	EMU8000_HWCF4_WRITE(emu, 0);
	EMU8000_HWCF5_WRITE(emu, 0x83);
	EMU8000_HWCF6_WRITE(emu, 0x8000);

	send_array(emu, init4, ARRAY_SIZE(init4)/4);
}


#define UNIQUE_ID1	0xa5b9
#define UNIQUE_ID2	0x9d53

/*
 * Size the onboard memory.
 * This is written so as not to need arbitary delays after the write. It
 * seems that the only way to do this is to use the one channel and keep
 * reallocating between read and write.
 */
static void __init
size_dram(emu8000_t *emu)
{
	int i, size;

	if (emu->dram_checked)
		return;

	size = 0;

	/* write out a magic number */
	snd_emu8000_dma_chan(emu, 0, EMU8000_RAM_WRITE);
	snd_emu8000_dma_chan(emu, 1, EMU8000_RAM_READ);
	EMU8000_SMALW_WRITE(emu, EMU8000_DRAM_OFFSET);
	EMU8000_SMLD_WRITE(emu, UNIQUE_ID1);
	snd_emu8000_init_fm(emu); /* This must really be here and not 2 lines back even */

	while (size < EMU8000_MAX_DRAM) {

		size += 512 * 1024;  /* increment 512kbytes */

		/* Write a unique data on the test address.
		 * if the address is out of range, the data is written on
		 * 0x200000(=EMU8000_DRAM_OFFSET).  Then the id word is
		 * changed by this data.
		 */
		/*snd_emu8000_dma_chan(emu, 0, EMU8000_RAM_WRITE);*/
		EMU8000_SMALW_WRITE(emu, EMU8000_DRAM_OFFSET + (size>>1));
		EMU8000_SMLD_WRITE(emu, UNIQUE_ID2);
		snd_emu8000_write_wait(emu);

		/*
		 * read the data on the just written DRAM address
		 * if not the same then we have reached the end of ram.
		 */
		/*snd_emu8000_dma_chan(emu, 0, EMU8000_RAM_READ);*/
		EMU8000_SMALR_WRITE(emu, EMU8000_DRAM_OFFSET + (size>>1));
		/*snd_emu8000_read_wait(emu);*/
		EMU8000_SMLD_READ(emu); /* discard stale data  */
		if (EMU8000_SMLD_READ(emu) != UNIQUE_ID2)
			break; /* we must have wrapped around */

		snd_emu8000_read_wait(emu);

		/*
		 * If it is the same it could be that the address just
		 * wraps back to the beginning; so check to see if the
		 * initial value has been overwritten.
		 */
		EMU8000_SMALR_WRITE(emu, EMU8000_DRAM_OFFSET);
		EMU8000_SMLD_READ(emu); /* discard stale data  */
		if (EMU8000_SMLD_READ(emu) != UNIQUE_ID1)
			break; /* we must have wrapped around */
		snd_emu8000_read_wait(emu);
	}

	/* wait until FULL bit in SMAxW register is false */
	for (i = 0; i < 10000; i++) {
		if ((EMU8000_SMALW_READ(emu) & 0x80000000) == 0)
			break;
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(1);
		if (signal_pending(current))
			break;
	}
	snd_emu8000_dma_chan(emu, 0, EMU8000_RAM_CLOSE);
	snd_emu8000_dma_chan(emu, 1, EMU8000_RAM_CLOSE);

	snd_printdd("EMU8000 [0x%lx]: %d Kb on-board memory detected\n",
		    emu->port1, size/1024);

	emu->mem_size = size;
	emu->dram_checked = 1;
}


/*
 * Initiailise the FM section.  You have to do this to use sample RAM
 * and therefore lose 2 voices.
 */
/*exported*/ void
snd_emu8000_init_fm(emu8000_t *emu)
{
	unsigned long flags;

	/* Initialize the last two channels for DRAM refresh and producing
	   the reverb and chorus effects for Yamaha OPL-3 synthesizer */

	/* 31: FM left channel, 0xffffe0-0xffffe8 */
	EMU8000_DCYSUSV_WRITE(emu, 30, 0x80);
	EMU8000_PSST_WRITE(emu, 30, 0xFFFFFFE0); /* full left */
	EMU8000_CSL_WRITE(emu, 30, 0x00FFFFE8 | (emu->fm_chorus_depth << 24));
	EMU8000_PTRX_WRITE(emu, 30, (emu->fm_reverb_depth << 8));
	EMU8000_CPF_WRITE(emu, 30, 0);
	EMU8000_CCCA_WRITE(emu, 30, 0x00FFFFE3);

	/* 32: FM right channel, 0xfffff0-0xfffff8 */
	EMU8000_DCYSUSV_WRITE(emu, 31, 0x80);
	EMU8000_PSST_WRITE(emu, 31, 0x00FFFFF0); /* full right */
	EMU8000_CSL_WRITE(emu, 31, 0x00FFFFF8 | (emu->fm_chorus_depth << 24));
	EMU8000_PTRX_WRITE(emu, 31, (emu->fm_reverb_depth << 8));
	EMU8000_CPF_WRITE(emu, 31, 0x8000);
	EMU8000_CCCA_WRITE(emu, 31, 0x00FFFFF3);

	snd_emu8000_poke((emu), EMU8000_DATA0(emu), EMU8000_CMD(1, (30)), 0);

	spin_lock_irqsave(&emu->reg_lock, flags);
	while (!(inw(EMU8000_PTR(emu)) & 0x1000))
		;
	while ((inw(EMU8000_PTR(emu)) & 0x1000))
		;
	spin_unlock_irqrestore(&emu->reg_lock, flags);
	snd_emu8000_poke((emu), EMU8000_DATA0(emu), EMU8000_CMD(1, (30)), 0x4828);
	/* this is really odd part.. */
	outb(0x3C, EMU8000_PTR(emu));
	outb(0, EMU8000_DATA1(emu));

	/* skew volume & cutoff */
	EMU8000_VTFT_WRITE(emu, 30, 0x8000FFFF);
	EMU8000_VTFT_WRITE(emu, 31, 0x8000FFFF);
}


/*
 * The main initialization routine.
 */
static void __init
snd_emu8000_init_hw(emu8000_t *emu)
{
	int i;

	emu->last_reg = 0xffff; /* reset the last register index */

	/* initialize hardware configuration */
	EMU8000_HWCF1_WRITE(emu, 0x0059);
	EMU8000_HWCF2_WRITE(emu, 0x0020);

	/* disable audio; this seems to reduce a clicking noise a bit.. */
	EMU8000_HWCF3_WRITE(emu, 0);

	/* initialize audio channels */
	init_audio(emu);

	/* initialize DMA */
	init_dma(emu);

	/* initialize init arrays */
	init_arrays(emu);

	/*
	 * Initialize the FM section of the AWE32, this is needed
	 * for DRAM refresh as well
	 */
	snd_emu8000_init_fm(emu);

	/* terminate all voices */
	for (i = 0; i < EMU8000_DRAM_VOICES; i++)
		EMU8000_DCYSUSV_WRITE(emu, 0, 0x807F);
	
	/* check DRAM memory size */
	size_dram(emu);

	/* enable audio */
	EMU8000_HWCF3_WRITE(emu, 0x4);

	/* set equzlier, chorus and reverb modes */
	snd_emu8000_update_equalizer(emu);
	snd_emu8000_update_chorus_mode(emu);
	snd_emu8000_update_reverb_mode(emu);
}


/*----------------------------------------------------------------
 * Bass/Treble Equalizer
 *----------------------------------------------------------------*/

static unsigned short bass_parm[12][3] = {
	{0xD26A, 0xD36A, 0x0000}, /* -12 dB */
	{0xD25B, 0xD35B, 0x0000}, /*  -8 */
	{0xD24C, 0xD34C, 0x0000}, /*  -6 */
	{0xD23D, 0xD33D, 0x0000}, /*  -4 */
	{0xD21F, 0xD31F, 0x0000}, /*  -2 */
	{0xC208, 0xC308, 0x0001}, /*   0 (HW default) */
	{0xC219, 0xC319, 0x0001}, /*  +2 */
	{0xC22A, 0xC32A, 0x0001}, /*  +4 */
	{0xC24C, 0xC34C, 0x0001}, /*  +6 */
	{0xC26E, 0xC36E, 0x0001}, /*  +8 */
	{0xC248, 0xC384, 0x0002}, /* +10 */
	{0xC26A, 0xC36A, 0x0002}, /* +12 dB */
};

static unsigned short treble_parm[12][9] = {
	{0x821E, 0xC26A, 0x031E, 0xC36A, 0x021E, 0xD208, 0x831E, 0xD308, 0x0001}, /* -12 dB */
	{0x821E, 0xC25B, 0x031E, 0xC35B, 0x021E, 0xD208, 0x831E, 0xD308, 0x0001},
	{0x821E, 0xC24C, 0x031E, 0xC34C, 0x021E, 0xD208, 0x831E, 0xD308, 0x0001},
	{0x821E, 0xC23D, 0x031E, 0xC33D, 0x021E, 0xD208, 0x831E, 0xD308, 0x0001},
	{0x821E, 0xC21F, 0x031E, 0xC31F, 0x021E, 0xD208, 0x831E, 0xD308, 0x0001},
	{0x821E, 0xD208, 0x031E, 0xD308, 0x021E, 0xD208, 0x831E, 0xD308, 0x0002},
	{0x821E, 0xD208, 0x031E, 0xD308, 0x021D, 0xD219, 0x831D, 0xD319, 0x0002},
	{0x821E, 0xD208, 0x031E, 0xD308, 0x021C, 0xD22A, 0x831C, 0xD32A, 0x0002},
	{0x821E, 0xD208, 0x031E, 0xD308, 0x021A, 0xD24C, 0x831A, 0xD34C, 0x0002},
	{0x821E, 0xD208, 0x031E, 0xD308, 0x0219, 0xD26E, 0x8319, 0xD36E, 0x0002}, /* +8 (HW default) */
	{0x821D, 0xD219, 0x031D, 0xD319, 0x0219, 0xD26E, 0x8319, 0xD36E, 0x0002},
	{0x821C, 0xD22A, 0x031C, 0xD32A, 0x0219, 0xD26E, 0x8319, 0xD36E, 0x0002}  /* +12 dB */
};


/*
 * set Emu8000 digital equalizer; from 0 to 11 [-12dB - 12dB]
 */
/*exported*/ void
snd_emu8000_update_equalizer(emu8000_t *emu)
{
	unsigned short w;
	int bass = emu->bass_level;
	int treble = emu->treble_level;

	if (bass < 0 || bass > 11 || treble < 0 || treble > 11)
		return;
	EMU8000_INIT4_WRITE(emu, 0x01, bass_parm[bass][0]);
	EMU8000_INIT4_WRITE(emu, 0x11, bass_parm[bass][1]);
	EMU8000_INIT3_WRITE(emu, 0x11, treble_parm[treble][0]);
	EMU8000_INIT3_WRITE(emu, 0x13, treble_parm[treble][1]);
	EMU8000_INIT3_WRITE(emu, 0x1b, treble_parm[treble][2]);
	EMU8000_INIT4_WRITE(emu, 0x07, treble_parm[treble][3]);
	EMU8000_INIT4_WRITE(emu, 0x0b, treble_parm[treble][4]);
	EMU8000_INIT4_WRITE(emu, 0x0d, treble_parm[treble][5]);
	EMU8000_INIT4_WRITE(emu, 0x17, treble_parm[treble][6]);
	EMU8000_INIT4_WRITE(emu, 0x19, treble_parm[treble][7]);
	w = bass_parm[bass][2] + treble_parm[treble][8];
	EMU8000_INIT4_WRITE(emu, 0x15, (unsigned short)(w + 0x0262));
	EMU8000_INIT4_WRITE(emu, 0x1d, (unsigned short)(w + 0x8362));
}


/*----------------------------------------------------------------
 * Chorus mode control
 *----------------------------------------------------------------*/

/*
 * chorus mode parameters
 */
#define SNDRV_EMU8000_CHORUS_1		0
#define	SNDRV_EMU8000_CHORUS_2		1
#define	SNDRV_EMU8000_CHORUS_3		2
#define	SNDRV_EMU8000_CHORUS_4		3
#define	SNDRV_EMU8000_CHORUS_FEEDBACK	4
#define	SNDRV_EMU8000_CHORUS_FLANGER	5
#define	SNDRV_EMU8000_CHORUS_SHORTDELAY	6
#define	SNDRV_EMU8000_CHORUS_SHORTDELAY2	7
#define SNDRV_EMU8000_CHORUS_PREDEFINED	8
/* user can define chorus modes up to 32 */
#define SNDRV_EMU8000_CHORUS_NUMBERS	32

typedef struct soundfont_chorus_fx_t {
	unsigned short feedback;	/* feedback level (0xE600-0xE6FF) */
	unsigned short delay_offset;	/* delay (0-0x0DA3) [1/44100 sec] */
	unsigned short lfo_depth;	/* LFO depth (0xBC00-0xBCFF) */
	unsigned int delay;	/* right delay (0-0xFFFFFFFF) [1/256/44100 sec] */
	unsigned int lfo_freq;		/* LFO freq LFO freq (0-0xFFFFFFFF) */
} soundfont_chorus_fx_t;

/* 5 parameters for each chorus mode; 3 x 16bit, 2 x 32bit */
static char chorus_defined[SNDRV_EMU8000_CHORUS_NUMBERS];
static soundfont_chorus_fx_t chorus_parm[SNDRV_EMU8000_CHORUS_NUMBERS] = {
	{0xE600, 0x03F6, 0xBC2C ,0x00000000, 0x0000006D}, /* chorus 1 */
	{0xE608, 0x031A, 0xBC6E, 0x00000000, 0x0000017C}, /* chorus 2 */
	{0xE610, 0x031A, 0xBC84, 0x00000000, 0x00000083}, /* chorus 3 */
	{0xE620, 0x0269, 0xBC6E, 0x00000000, 0x0000017C}, /* chorus 4 */
	{0xE680, 0x04D3, 0xBCA6, 0x00000000, 0x0000005B}, /* feedback */
	{0xE6E0, 0x044E, 0xBC37, 0x00000000, 0x00000026}, /* flanger */
	{0xE600, 0x0B06, 0xBC00, 0x0006E000, 0x00000083}, /* short delay */
	{0xE6C0, 0x0B06, 0xBC00, 0x0006E000, 0x00000083}, /* short delay + feedback */
};

/*exported*/ int
snd_emu8000_load_chorus_fx(emu8000_t *emu, int mode, const void __user *buf, long len)
{
	soundfont_chorus_fx_t rec;
	if (mode < SNDRV_EMU8000_CHORUS_PREDEFINED || mode >= SNDRV_EMU8000_CHORUS_NUMBERS) {
		snd_printk(KERN_WARNING "invalid chorus mode %d for uploading\n", mode);
		return -EINVAL;
	}
	if (len < (long)sizeof(rec) || copy_from_user(&rec, buf, sizeof(rec)))
		return -EFAULT;
	chorus_parm[mode] = rec;
	chorus_defined[mode] = 1;
	return 0;
}

/*exported*/ void
snd_emu8000_update_chorus_mode(emu8000_t *emu)
{
	int effect = emu->chorus_mode;
	if (effect < 0 || effect >= SNDRV_EMU8000_CHORUS_NUMBERS ||
	    (effect >= SNDRV_EMU8000_CHORUS_PREDEFINED && !chorus_defined[effect]))
		return;
	EMU8000_INIT3_WRITE(emu, 0x09, chorus_parm[effect].feedback);
	EMU8000_INIT3_WRITE(emu, 0x0c, chorus_parm[effect].delay_offset);
	EMU8000_INIT4_WRITE(emu, 0x03, chorus_parm[effect].lfo_depth);
	EMU8000_HWCF4_WRITE(emu, chorus_parm[effect].delay);
	EMU8000_HWCF5_WRITE(emu, chorus_parm[effect].lfo_freq);
	EMU8000_HWCF6_WRITE(emu, 0x8000);
	EMU8000_HWCF7_WRITE(emu, 0x0000);
}

/*----------------------------------------------------------------
 * Reverb mode control
 *----------------------------------------------------------------*/

/*
 * reverb mode parameters
 */
#define	SNDRV_EMU8000_REVERB_ROOM1	0
#define SNDRV_EMU8000_REVERB_ROOM2	1
#define	SNDRV_EMU8000_REVERB_ROOM3	2
#define	SNDRV_EMU8000_REVERB_HALL1	3
#define	SNDRV_EMU8000_REVERB_HALL2	4
#define	SNDRV_EMU8000_REVERB_PLATE	5
#define	SNDRV_EMU8000_REVERB_DELAY	6
#define	SNDRV_EMU8000_REVERB_PANNINGDELAY 7
#define SNDRV_EMU8000_REVERB_PREDEFINED	8
/* user can define reverb modes up to 32 */
#define SNDRV_EMU8000_REVERB_NUMBERS	32

typedef struct soundfont_reverb_fx_t {
	unsigned short parms[28];
} soundfont_reverb_fx_t;

/* reverb mode settings; write the following 28 data of 16 bit length
 *   on the corresponding ports in the reverb_cmds array
 */
static char reverb_defined[SNDRV_EMU8000_CHORUS_NUMBERS];
static soundfont_reverb_fx_t reverb_parm[SNDRV_EMU8000_REVERB_NUMBERS] = {
{{  /* room 1 */
	0xB488, 0xA450, 0x9550, 0x84B5, 0x383A, 0x3EB5, 0x72F4,
	0x72A4, 0x7254, 0x7204, 0x7204, 0x7204, 0x4416, 0x4516,
	0xA490, 0xA590, 0x842A, 0x852A, 0x842A, 0x852A, 0x8429,
	0x8529, 0x8429, 0x8529, 0x8428, 0x8528, 0x8428, 0x8528,
}},
{{  /* room 2 */
	0xB488, 0xA458, 0x9558, 0x84B5, 0x383A, 0x3EB5, 0x7284,
	0x7254, 0x7224, 0x7224, 0x7254, 0x7284, 0x4448, 0x4548,
	0xA440, 0xA540, 0x842A, 0x852A, 0x842A, 0x852A, 0x8429,
	0x8529, 0x8429, 0x8529, 0x8428, 0x8528, 0x8428, 0x8528,
}},
{{  /* room 3 */
	0xB488, 0xA460, 0x9560, 0x84B5, 0x383A, 0x3EB5, 0x7284,
	0x7254, 0x7224, 0x7224, 0x7254, 0x7284, 0x4416, 0x4516,
	0xA490, 0xA590, 0x842C, 0x852C, 0x842C, 0x852C, 0x842B,
	0x852B, 0x842B, 0x852B, 0x842A, 0x852A, 0x842A, 0x852A,
}},
{{  /* hall 1 */
	0xB488, 0xA470, 0x9570, 0x84B5, 0x383A, 0x3EB5, 0x7284,
	0x7254, 0x7224, 0x7224, 0x7254, 0x7284, 0x4448, 0x4548,
	0xA440, 0xA540, 0x842B, 0x852B, 0x842B, 0x852B, 0x842A,
	0x852A, 0x842A, 0x852A, 0x8429, 0x8529, 0x8429, 0x8529,
}},
{{  /* hall 2 */
	0xB488, 0xA470, 0x9570, 0x84B5, 0x383A, 0x3EB5, 0x7254,
	0x7234, 0x7224, 0x7254, 0x7264, 0x7294, 0x44C3, 0x45C3,
	0xA404, 0xA504, 0x842A, 0x852A, 0x842A, 0x852A, 0x8429,
	0x8529, 0x8429, 0x8529, 0x8428, 0x8528, 0x8428, 0x8528,
}},
{{  /* plate */
	0xB4FF, 0xA470, 0x9570, 0x84B5, 0x383A, 0x3EB5, 0x7234,
	0x7234, 0x7234, 0x7234, 0x7234, 0x7234, 0x4448, 0x4548,
	0xA440, 0xA540, 0x842A, 0x852A, 0x842A, 0x852A, 0x8429,
	0x8529, 0x8429, 0x8529, 0x8428, 0x8528, 0x8428, 0x8528,
}},
{{  /* delay */
	0xB4FF, 0xA470, 0x9500, 0x84B5, 0x333A, 0x39B5, 0x7204,
	0x7204, 0x7204, 0x7204, 0x7204, 0x72F4, 0x4400, 0x4500,
	0xA4FF, 0xA5FF, 0x8420, 0x8520, 0x8420, 0x8520, 0x8420,
	0x8520, 0x8420, 0x8520, 0x8420, 0x8520, 0x8420, 0x8520,
}},
{{  /* panning delay */
	0xB4FF, 0xA490, 0x9590, 0x8474, 0x333A, 0x39B5, 0x7204,
	0x7204, 0x7204, 0x7204, 0x7204, 0x72F4, 0x4400, 0x4500,
	0xA4FF, 0xA5FF, 0x8420, 0x8520, 0x8420, 0x8520, 0x8420,
	0x8520, 0x8420, 0x8520, 0x8420, 0x8520, 0x8420, 0x8520,
}},
};

enum { DATA1, DATA2 };
#define AWE_INIT1(c)	EMU8000_CMD(2,c), DATA1
#define AWE_INIT2(c)	EMU8000_CMD(2,c), DATA2
#define AWE_INIT3(c)	EMU8000_CMD(3,c), DATA1
#define AWE_INIT4(c)	EMU8000_CMD(3,c), DATA2

static struct reverb_cmd_pair {
	unsigned short cmd, port;
} reverb_cmds[28] = {
  {AWE_INIT1(0x03)}, {AWE_INIT1(0x05)}, {AWE_INIT4(0x1F)}, {AWE_INIT1(0x07)},
  {AWE_INIT2(0x14)}, {AWE_INIT2(0x16)}, {AWE_INIT1(0x0F)}, {AWE_INIT1(0x17)},
  {AWE_INIT1(0x1F)}, {AWE_INIT2(0x07)}, {AWE_INIT2(0x0F)}, {AWE_INIT2(0x17)},
  {AWE_INIT2(0x1D)}, {AWE_INIT2(0x1F)}, {AWE_INIT3(0x01)}, {AWE_INIT3(0x03)},
  {AWE_INIT1(0x09)}, {AWE_INIT1(0x0B)}, {AWE_INIT1(0x11)}, {AWE_INIT1(0x13)},
  {AWE_INIT1(0x19)}, {AWE_INIT1(0x1B)}, {AWE_INIT2(0x01)}, {AWE_INIT2(0x03)},
  {AWE_INIT2(0x09)}, {AWE_INIT2(0x0B)}, {AWE_INIT2(0x11)}, {AWE_INIT2(0x13)},
};

/*exported*/ int
snd_emu8000_load_reverb_fx(emu8000_t *emu, int mode, const void __user *buf, long len)
{
	soundfont_reverb_fx_t rec;

	if (mode < SNDRV_EMU8000_REVERB_PREDEFINED || mode >= SNDRV_EMU8000_REVERB_NUMBERS) {
		snd_printk(KERN_WARNING "invalid reverb mode %d for uploading\n", mode);
		return -EINVAL;
	}
	if (len < (long)sizeof(rec) || copy_from_user(&rec, buf, sizeof(rec)))
		return -EFAULT;
	reverb_parm[mode] = rec;
	reverb_defined[mode] = 1;
	return 0;
}

/*exported*/ void
snd_emu8000_update_reverb_mode(emu8000_t *emu)
{
	int effect = emu->reverb_mode;
	int i;

	if (effect < 0 || effect >= SNDRV_EMU8000_REVERB_NUMBERS ||
	    (effect >= SNDRV_EMU8000_REVERB_PREDEFINED && !reverb_defined[effect]))
		return;
	for (i = 0; i < 28; i++) {
		int port;
		if (reverb_cmds[i].port == DATA1)
			port = EMU8000_DATA1(emu);
		else
			port = EMU8000_DATA2(emu);
		snd_emu8000_poke(emu, port, reverb_cmds[i].cmd, reverb_parm[effect].parms[i]);
	}
}


/*----------------------------------------------------------------
 * mixer interface
 *----------------------------------------------------------------*/

/*
 * bass/treble
 */
static int mixer_bass_treble_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 11;
	return 0;
}

static int mixer_bass_treble_get(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	emu8000_t *emu = snd_kcontrol_chip(kcontrol);
	
	ucontrol->value.integer.value[0] = kcontrol->private_value ? emu->treble_level : emu->bass_level;
	return 0;
}

static int mixer_bass_treble_put(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	emu8000_t *emu = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int change;
	unsigned short val1;
	
	val1 = ucontrol->value.integer.value[0] % 12;
	spin_lock_irqsave(&emu->control_lock, flags);
	if (kcontrol->private_value) {
		change = val1 != emu->treble_level;
		emu->treble_level = val1;
	} else {
		change = val1 != emu->bass_level;
		emu->bass_level = val1;
	}
	spin_unlock_irqrestore(&emu->control_lock, flags);
	snd_emu8000_update_equalizer(emu);
	return change;
}

static snd_kcontrol_new_t mixer_bass_control =
{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Synth Tone Control - Bass",
	.info = mixer_bass_treble_info,
	.get = mixer_bass_treble_get,
	.put = mixer_bass_treble_put,
	.private_value = 0,
};

static snd_kcontrol_new_t mixer_treble_control =
{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Synth Tone Control - Treble",
	.info = mixer_bass_treble_info,
	.get = mixer_bass_treble_get,
	.put = mixer_bass_treble_put,
	.private_value = 1,
};

/*
 * chorus/reverb mode
 */
static int mixer_chorus_reverb_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = kcontrol->private_value ? (SNDRV_EMU8000_CHORUS_NUMBERS-1) : (SNDRV_EMU8000_REVERB_NUMBERS-1);
	return 0;
}

static int mixer_chorus_reverb_get(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	emu8000_t *emu = snd_kcontrol_chip(kcontrol);
	
	ucontrol->value.integer.value[0] = kcontrol->private_value ? emu->chorus_mode : emu->reverb_mode;
	return 0;
}

static int mixer_chorus_reverb_put(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	emu8000_t *emu = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int change;
	unsigned short val1;
	
	spin_lock_irqsave(&emu->control_lock, flags);
	if (kcontrol->private_value) {
		val1 = ucontrol->value.integer.value[0] % SNDRV_EMU8000_CHORUS_NUMBERS;
		change = val1 != emu->chorus_mode;
		emu->chorus_mode = val1;
	} else {
		val1 = ucontrol->value.integer.value[0] % SNDRV_EMU8000_REVERB_NUMBERS;
		change = val1 != emu->reverb_mode;
		emu->reverb_mode = val1;
	}
	spin_unlock_irqrestore(&emu->control_lock, flags);
	if (change) {
		if (kcontrol->private_value)
			snd_emu8000_update_chorus_mode(emu);
		else
			snd_emu8000_update_reverb_mode(emu);
	}
	return change;
}

static snd_kcontrol_new_t mixer_chorus_mode_control =
{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Chorus Mode",
	.info = mixer_chorus_reverb_info,
	.get = mixer_chorus_reverb_get,
	.put = mixer_chorus_reverb_put,
	.private_value = 1,
};

static snd_kcontrol_new_t mixer_reverb_mode_control =
{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Reverb Mode",
	.info = mixer_chorus_reverb_info,
	.get = mixer_chorus_reverb_get,
	.put = mixer_chorus_reverb_put,
	.private_value = 0,
};

/*
 * FM OPL3 chorus/reverb depth
 */
static int mixer_fm_depth_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 255;
	return 0;
}

static int mixer_fm_depth_get(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	emu8000_t *emu = snd_kcontrol_chip(kcontrol);
	
	ucontrol->value.integer.value[0] = kcontrol->private_value ? emu->fm_chorus_depth : emu->fm_reverb_depth;
	return 0;
}

static int mixer_fm_depth_put(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	emu8000_t *emu = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int change;
	unsigned short val1;
	
	val1 = ucontrol->value.integer.value[0] % 256;
	spin_lock_irqsave(&emu->control_lock, flags);
	if (kcontrol->private_value) {
		change = val1 != emu->fm_chorus_depth;
		emu->fm_chorus_depth = val1;
	} else {
		change = val1 != emu->fm_reverb_depth;
		emu->fm_reverb_depth = val1;
	}
	spin_unlock_irqrestore(&emu->control_lock, flags);
	if (change)
		snd_emu8000_init_fm(emu);
	return change;
}

static snd_kcontrol_new_t mixer_fm_chorus_depth_control =
{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "FM Chorus Depth",
	.info = mixer_fm_depth_info,
	.get = mixer_fm_depth_get,
	.put = mixer_fm_depth_put,
	.private_value = 1,
};

static snd_kcontrol_new_t mixer_fm_reverb_depth_control =
{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "FM Reverb Depth",
	.info = mixer_fm_depth_info,
	.get = mixer_fm_depth_get,
	.put = mixer_fm_depth_put,
	.private_value = 0,
};


static snd_kcontrol_new_t *mixer_defs[EMU8000_NUM_CONTROLS] = {
	&mixer_bass_control,
	&mixer_treble_control,
	&mixer_chorus_mode_control,
	&mixer_reverb_mode_control,
	&mixer_fm_chorus_depth_control,
	&mixer_fm_reverb_depth_control,
};

/*
 * create and attach mixer elements for WaveTable treble/bass controls
 */
static int __init
snd_emu8000_create_mixer(snd_card_t *card, emu8000_t *emu)
{
	int i, err = 0;

	snd_assert(emu != NULL && card != NULL, return -EINVAL);

	spin_lock_init(&emu->control_lock);

	memset(emu->controls, 0, sizeof(emu->controls));
	for (i = 0; i < EMU8000_NUM_CONTROLS; i++) {
		if ((err = snd_ctl_add(card, emu->controls[i] = snd_ctl_new1(mixer_defs[i], emu))) < 0)
			goto __error;
	}
	return 0;

__error:
	for (i = 0; i < EMU8000_NUM_CONTROLS; i++) {
		down_write(&card->controls_rwsem);
		if (emu->controls[i])
			snd_ctl_remove(card, emu->controls[i]);
		up_write(&card->controls_rwsem);
	}
	return err;
}


/*
 * free resources
 */
static int snd_emu8000_free(emu8000_t *hw)
{
	if (hw->res_port1) {
		release_resource(hw->res_port1);
		kfree_nocheck(hw->res_port1);
	}
	if (hw->res_port2) {
		release_resource(hw->res_port2);
		kfree_nocheck(hw->res_port2);
	}
	if (hw->res_port3) {
		release_resource(hw->res_port3);
		kfree_nocheck(hw->res_port3);
	}
	kfree(hw);
	return 0;
}

/*
 */
static int snd_emu8000_dev_free(snd_device_t *device)
{
	emu8000_t *hw = device->device_data;
	return snd_emu8000_free(hw);
}

/*
 * initialize and register emu8000 synth device.
 */
int __init
snd_emu8000_new(snd_card_t *card, int index, long port, int seq_ports, snd_seq_device_t **awe_ret)
{
	snd_seq_device_t *awe;
	emu8000_t *hw;
	int err;
	static snd_device_ops_t ops = {
		.dev_free = snd_emu8000_dev_free,
	};

	if (awe_ret)
		*awe_ret = NULL;

	if (seq_ports <= 0)
		return 0;

	hw = kzalloc(sizeof(*hw), GFP_KERNEL);
	if (hw == NULL)
		return -ENOMEM;
	spin_lock_init(&hw->reg_lock);
	hw->index = index;
	hw->port1 = port;
	hw->port2 = port + 0x400;
	hw->port3 = port + 0x800;
	if (!(hw->res_port1 = request_region(hw->port1, 4, "Emu8000-1")) ||
	    !(hw->res_port2 = request_region(hw->port2, 4, "Emu8000-2")) ||
	    !(hw->res_port3 = request_region(hw->port3, 4, "Emu8000-3"))) {
		snd_printk(KERN_ERR "sbawe: can't grab ports 0x%lx, 0x%lx, 0x%lx\n", hw->port1, hw->port2, hw->port3);
		snd_emu8000_free(hw);
		return -EBUSY;
	}
	hw->mem_size = 0;
	hw->card = card;
	hw->seq_ports = seq_ports;
	hw->bass_level = 5;
	hw->treble_level = 9;
	hw->chorus_mode = 2;
	hw->reverb_mode = 4;
	hw->fm_chorus_depth = 0;
	hw->fm_reverb_depth = 0;

	if (snd_emu8000_detect(hw) < 0) {
		snd_emu8000_free(hw);
		return -ENODEV;
	}

	snd_emu8000_init_hw(hw);
	if ((err = snd_emu8000_create_mixer(card, hw)) < 0) {
		snd_emu8000_free(hw);
		return err;
	}
	
	if ((err = snd_device_new(card, SNDRV_DEV_CODEC, hw, &ops)) < 0) {
		snd_emu8000_free(hw);
		return err;
	}
#if defined(CONFIG_SND_SEQUENCER) || (defined(MODULE) && defined(CONFIG_SND_SEQUENCER_MODULE))
	if (snd_seq_device_new(card, index, SNDRV_SEQ_DEV_ID_EMU8000,
			       sizeof(emu8000_t*), &awe) >= 0) {
		strcpy(awe->name, "EMU-8000");
		*(emu8000_t**)SNDRV_SEQ_DEVICE_ARGPTR(awe) = hw;
	}
#else
	awe = NULL;
#endif
	if (awe_ret)
		*awe_ret = awe;

	return 0;
}


/*
 * exported stuff
 */

EXPORT_SYMBOL(snd_emu8000_poke);
EXPORT_SYMBOL(snd_emu8000_peek);
EXPORT_SYMBOL(snd_emu8000_poke_dw);
EXPORT_SYMBOL(snd_emu8000_peek_dw);
EXPORT_SYMBOL(snd_emu8000_dma_chan);
EXPORT_SYMBOL(snd_emu8000_init_fm);
EXPORT_SYMBOL(snd_emu8000_load_chorus_fx);
EXPORT_SYMBOL(snd_emu8000_load_reverb_fx);
EXPORT_SYMBOL(snd_emu8000_update_chorus_mode);
EXPORT_SYMBOL(snd_emu8000_update_reverb_mode);
EXPORT_SYMBOL(snd_emu8000_update_equalizer);
