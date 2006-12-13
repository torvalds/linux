/*
 *  Driver for A2 audio system used in SGI machines
 *  Copyright (c) 2001, 2002, 2003 Ladislav Michl <ladis@linux-mips.org>
 *  
 *  Based on Ulf Carlsson's code.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as 
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  Supported devices:
 *  /dev/dsp    standard dsp device, (mostly) OSS compatible
 *  /dev/mixer	standard mixer device, (mostly) OSS compatible
 *
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/sound.h>
#include <linux/soundcard.h>
#include <linux/mutex.h>


#include <asm/io.h>
#include <asm/sgi/hpc3.h>
#include <asm/sgi/ip22.h>

#include "hal2.h"

#if 0
#define DEBUG(args...)		printk(args)
#else
#define DEBUG(args...)
#endif

#if 0 
#define DEBUG_MIX(args...)	printk(args)
#else
#define DEBUG_MIX(args...)
#endif

/*
 * Before touching these look how it works. It is a bit unusual I know,
 * but it helps to keep things simple. This driver is considered complete
 * and I won't add any new features although hardware has many cool
 * capabilities.
 * (Historical note: HAL2 driver was first written by Ulf Carlsson - ALSA
 * 0.3 running with 2.2.x kernel. Then ALSA changed completely and it
 * seemed easier to me to write OSS driver from scratch - this one. Now
 * when ALSA is official part of 2.6 kernel it's time to write ALSA driver
 * using (hopefully) final version of ALSA interface)
 */
#define H2_BLOCK_SIZE	1024
#define H2_ADC_BUFSIZE	8192
#define H2_DAC_BUFSIZE	16834

struct hal2_pbus {
	struct hpc3_pbus_dmacregs *pbus;
	int pbusnr;
	unsigned int ctrl;		/* Current state of pbus->pbdma_ctrl */
};

struct hal2_desc {
	struct hpc_dma_desc desc;
	u32 cnt;			/* don't touch, it is also padding */
};

struct hal2_codec {
	unsigned char *buffer;
	struct hal2_desc *desc;
	int desc_count;
	int tail, head;			/* tail index, head index */
	struct hal2_pbus pbus;
	unsigned int format;		/* Audio data format */
	int voices;			/* mono/stereo */
	unsigned int sample_rate;
	unsigned int master;		/* Master frequency */
	unsigned short mod;		/* MOD value */
	unsigned short inc;		/* INC value */

	wait_queue_head_t dma_wait;
	spinlock_t lock;
	struct mutex sem;

	int usecount;			/* recording and playback are
					 * independent */
};

#define H2_MIX_OUTPUT_ATT	0
#define H2_MIX_INPUT_GAIN	1
#define H2_MIXERS		2
struct hal2_mixer {
	int modcnt;
	unsigned int master;
	unsigned int volume[H2_MIXERS];
};

struct hal2_card {
	int dev_dsp;			/* audio device */
	int dev_mixer;			/* mixer device */
	int dev_midi;			/* midi device */

	struct hal2_ctl_regs *ctl_regs;	/* HAL2 ctl registers */
	struct hal2_aes_regs *aes_regs;	/* HAL2 aes registers */
	struct hal2_vol_regs *vol_regs;	/* HAL2 vol registers */
	struct hal2_syn_regs *syn_regs;	/* HAL2 syn registers */

	struct hal2_codec dac;
	struct hal2_codec adc;
	struct hal2_mixer mixer;
};

#define H2_INDIRECT_WAIT(regs)	while (regs->isr & H2_ISR_TSTATUS);

#define H2_READ_ADDR(addr)	(addr | (1<<7))
#define H2_WRITE_ADDR(addr)	(addr)

static char *hal2str = "HAL2";

/*
 * I doubt anyone has a machine with two HAL2 cards. It's possible to
 * have two HPC's, so it is probably possible to have two HAL2 cards.
 * Try to deal with it, but note that it is not tested.
 */
#define MAXCARDS	2
static struct hal2_card* hal2_card[MAXCARDS];

static const struct {
	unsigned char idx:4, avail:1;
} mixtable[SOUND_MIXER_NRDEVICES] = {
	[SOUND_MIXER_PCM]	= { H2_MIX_OUTPUT_ATT, 1 },	/* voice */
	[SOUND_MIXER_MIC]	= { H2_MIX_INPUT_GAIN, 1 },	/* mic */
};

#define H2_SUPPORTED_FORMATS	(AFMT_S16_LE | AFMT_S16_BE)

static inline void hal2_isr_write(struct hal2_card *hal2, u16 val)
{
	hal2->ctl_regs->isr = val;
}

static inline u16 hal2_isr_look(struct hal2_card *hal2)
{
	return hal2->ctl_regs->isr;
}

static inline u16 hal2_rev_look(struct hal2_card *hal2)
{
	return hal2->ctl_regs->rev;
}

#ifdef HAL2_DUMP_REGS
static u16 hal2_i_look16(struct hal2_card *hal2, u16 addr)
{
	struct hal2_ctl_regs *regs = hal2->ctl_regs;

	regs->iar = H2_READ_ADDR(addr);
	H2_INDIRECT_WAIT(regs);
	return regs->idr0;
}
#endif

static u32 hal2_i_look32(struct hal2_card *hal2, u16 addr)
{
	u32 ret;
	struct hal2_ctl_regs *regs = hal2->ctl_regs;

	regs->iar = H2_READ_ADDR(addr);
	H2_INDIRECT_WAIT(regs);
	ret = regs->idr0 & 0xffff;
	regs->iar = H2_READ_ADDR(addr | 0x1);
	H2_INDIRECT_WAIT(regs);
	ret |= (regs->idr0 & 0xffff) << 16;
	return ret;
}

static void hal2_i_write16(struct hal2_card *hal2, u16 addr, u16 val)
{
	struct hal2_ctl_regs *regs = hal2->ctl_regs;

	regs->idr0 = val;
	regs->idr1 = 0;
	regs->idr2 = 0;
	regs->idr3 = 0;
	regs->iar = H2_WRITE_ADDR(addr);
	H2_INDIRECT_WAIT(regs);
}

static void hal2_i_write32(struct hal2_card *hal2, u16 addr, u32 val)
{
	struct hal2_ctl_regs *regs = hal2->ctl_regs;

	regs->idr0 = val & 0xffff;
	regs->idr1 = val >> 16;
	regs->idr2 = 0;
	regs->idr3 = 0;
	regs->iar = H2_WRITE_ADDR(addr);
	H2_INDIRECT_WAIT(regs);
}

static void hal2_i_setbit16(struct hal2_card *hal2, u16 addr, u16 bit)
{
	struct hal2_ctl_regs *regs = hal2->ctl_regs;

	regs->iar = H2_READ_ADDR(addr);
	H2_INDIRECT_WAIT(regs);
	regs->idr0 = (regs->idr0 & 0xffff) | bit;
	regs->idr1 = 0;
	regs->idr2 = 0;
	regs->idr3 = 0;
	regs->iar = H2_WRITE_ADDR(addr);
	H2_INDIRECT_WAIT(regs);
}

static void hal2_i_setbit32(struct hal2_card *hal2, u16 addr, u32 bit)
{
	u32 tmp;
	struct hal2_ctl_regs *regs = hal2->ctl_regs;

	regs->iar = H2_READ_ADDR(addr);
	H2_INDIRECT_WAIT(regs);
	tmp = (regs->idr0 & 0xffff) | (regs->idr1 << 16) | bit;
	regs->idr0 = tmp & 0xffff;
	regs->idr1 = tmp >> 16;
	regs->idr2 = 0;
	regs->idr3 = 0;
	regs->iar = H2_WRITE_ADDR(addr);
	H2_INDIRECT_WAIT(regs);
}

static void hal2_i_clearbit16(struct hal2_card *hal2, u16 addr, u16 bit)
{
	struct hal2_ctl_regs *regs = hal2->ctl_regs;

	regs->iar = H2_READ_ADDR(addr);
	H2_INDIRECT_WAIT(regs);
	regs->idr0 = (regs->idr0 & 0xffff) & ~bit;
	regs->idr1 = 0;
	regs->idr2 = 0;
	regs->idr3 = 0;
	regs->iar = H2_WRITE_ADDR(addr);
	H2_INDIRECT_WAIT(regs);
}

#if 0
static void hal2_i_clearbit32(struct hal2_card *hal2, u16 addr, u32 bit)
{
	u32 tmp;
	hal2_ctl_regs_t *regs = hal2->ctl_regs;

	regs->iar = H2_READ_ADDR(addr);
	H2_INDIRECT_WAIT(regs);
	tmp = ((regs->idr0 & 0xffff) | (regs->idr1 << 16)) & ~bit;
	regs->idr0 = tmp & 0xffff;
	regs->idr1 = tmp >> 16;
	regs->idr2 = 0;
	regs->idr3 = 0;
	regs->iar = H2_WRITE_ADDR(addr);
	H2_INDIRECT_WAIT(regs);
}
#endif

#ifdef HAL2_DUMP_REGS
static void hal2_dump_regs(struct hal2_card *hal2)
{
	DEBUG("isr: %08hx ", hal2_isr_look(hal2));
	DEBUG("rev: %08hx\n", hal2_rev_look(hal2));
	DEBUG("relay: %04hx\n", hal2_i_look16(hal2, H2I_RELAY_C));
	DEBUG("port en: %04hx ", hal2_i_look16(hal2, H2I_DMA_PORT_EN));
	DEBUG("dma end: %04hx ", hal2_i_look16(hal2, H2I_DMA_END));
	DEBUG("dma drv: %04hx\n", hal2_i_look16(hal2, H2I_DMA_DRV));
	DEBUG("syn ctl: %04hx ", hal2_i_look16(hal2, H2I_SYNTH_C));
	DEBUG("aesrx ctl: %04hx ", hal2_i_look16(hal2, H2I_AESRX_C));
	DEBUG("aestx ctl: %04hx ", hal2_i_look16(hal2, H2I_AESTX_C));
	DEBUG("dac ctl1: %04hx ", hal2_i_look16(hal2, H2I_ADC_C1));
	DEBUG("dac ctl2: %08x ", hal2_i_look32(hal2, H2I_ADC_C2));
	DEBUG("adc ctl1: %04hx ", hal2_i_look16(hal2, H2I_DAC_C1));
	DEBUG("adc ctl2: %08x ", hal2_i_look32(hal2, H2I_DAC_C2));
	DEBUG("syn map: %04hx\n", hal2_i_look16(hal2, H2I_SYNTH_MAP_C));
	DEBUG("bres1 ctl1: %04hx ", hal2_i_look16(hal2, H2I_BRES1_C1));
	DEBUG("bres1 ctl2: %04x ", hal2_i_look32(hal2, H2I_BRES1_C2));
	DEBUG("bres2 ctl1: %04hx ", hal2_i_look16(hal2, H2I_BRES2_C1));
	DEBUG("bres2 ctl2: %04x ", hal2_i_look32(hal2, H2I_BRES2_C2));
	DEBUG("bres3 ctl1: %04hx ", hal2_i_look16(hal2, H2I_BRES3_C1));
	DEBUG("bres3 ctl2: %04x\n", hal2_i_look32(hal2, H2I_BRES3_C2));
}
#endif

static struct hal2_card* hal2_dsp_find_card(int minor)
{
	int i;

	for (i = 0; i < MAXCARDS; i++)
		if (hal2_card[i] != NULL && hal2_card[i]->dev_dsp == minor)
			return hal2_card[i];
	return NULL;
}

static struct hal2_card* hal2_mixer_find_card(int minor)
{
	int i;

	for (i = 0; i < MAXCARDS; i++)
		if (hal2_card[i] != NULL && hal2_card[i]->dev_mixer == minor)
			return hal2_card[i];
	return NULL;
}

static void hal2_inc_head(struct hal2_codec *codec)
{
	codec->head++;
	if (codec->head == codec->desc_count)
		codec->head = 0;
}

static void hal2_inc_tail(struct hal2_codec *codec)
{
	codec->tail++;
	if (codec->tail == codec->desc_count)
		codec->tail = 0;
}

static void hal2_dac_interrupt(struct hal2_codec *dac)
{
	int running;

	spin_lock(&dac->lock);
	/* if tail buffer contains zero samples DMA stream was already
	 * stopped */
	running = dac->desc[dac->tail].cnt;
	dac->desc[dac->tail].cnt = 0;
	dac->desc[dac->tail].desc.cntinfo = HPCDMA_XIE | HPCDMA_EOX;
	/* we just proccessed empty buffer, don't update tail pointer */
	if (running)
		hal2_inc_tail(dac);
	spin_unlock(&dac->lock);

	wake_up(&dac->dma_wait);
}

static void hal2_adc_interrupt(struct hal2_codec *adc)
{
	int running;

	spin_lock(&adc->lock);
	/* if head buffer contains nonzero samples DMA stream was already
	 * stopped */
	running = !adc->desc[adc->head].cnt;
	adc->desc[adc->head].cnt = H2_BLOCK_SIZE;
	adc->desc[adc->head].desc.cntinfo = HPCDMA_XIE | HPCDMA_EOR;
	/* we just proccessed empty buffer, don't update head pointer */
	if (running)
		hal2_inc_head(adc);
	spin_unlock(&adc->lock);

	wake_up(&adc->dma_wait);
}

static irqreturn_t hal2_interrupt(int irq, void *dev_id)
{
	struct hal2_card *hal2 = dev_id;
	irqreturn_t ret = IRQ_NONE;

	/* decide what caused this interrupt */
	if (hal2->dac.pbus.pbus->pbdma_ctrl & HPC3_PDMACTRL_INT) {
		hal2_dac_interrupt(&hal2->dac);
		ret = IRQ_HANDLED;
	}
	if (hal2->adc.pbus.pbus->pbdma_ctrl & HPC3_PDMACTRL_INT) {
		hal2_adc_interrupt(&hal2->adc);
		ret = IRQ_HANDLED;
	}
	return ret;
}

static int hal2_compute_rate(struct hal2_codec *codec, unsigned int rate)
{
	unsigned short mod;
	
	DEBUG("rate: %d\n", rate);
	
	if (rate < 4000) rate = 4000;
	else if (rate > 48000) rate = 48000;

	if (44100 % rate < 48000 % rate) {
		mod = 4 * 44100 / rate;
		codec->master = 44100;
	} else {
		mod = 4 * 48000 / rate;
		codec->master = 48000;
	}

	codec->inc = 4;
	codec->mod = mod;
	rate = 4 * codec->master / mod;

	DEBUG("real_rate: %d\n", rate);

	return rate;
}

static void hal2_set_dac_rate(struct hal2_card *hal2)
{
	unsigned int master = hal2->dac.master;
	int inc = hal2->dac.inc;
	int mod = hal2->dac.mod;

	DEBUG("master: %d inc: %d mod: %d\n", master, inc, mod);
	
	hal2_i_write16(hal2, H2I_BRES1_C1, (master == 44100) ? 1 : 0);
	hal2_i_write32(hal2, H2I_BRES1_C2, ((0xffff & (inc - mod - 1)) << 16) | inc);
}

static void hal2_set_adc_rate(struct hal2_card *hal2)
{
	unsigned int master = hal2->adc.master;
	int inc = hal2->adc.inc;
	int mod = hal2->adc.mod;

	DEBUG("master: %d inc: %d mod: %d\n", master, inc, mod);
	
	hal2_i_write16(hal2, H2I_BRES2_C1, (master == 44100) ? 1 : 0);
	hal2_i_write32(hal2, H2I_BRES2_C2, ((0xffff & (inc - mod - 1)) << 16) | inc);
}

static void hal2_setup_dac(struct hal2_card *hal2)
{
	unsigned int fifobeg, fifoend, highwater, sample_size;
	struct hal2_pbus *pbus = &hal2->dac.pbus;

	DEBUG("hal2_setup_dac\n");
	
	/* Now we set up some PBUS information. The PBUS needs information about
	 * what portion of the fifo it will use. If it's receiving or
	 * transmitting, and finally whether the stream is little endian or big
	 * endian. The information is written later, on the start call.
	 */
	sample_size = 2 * hal2->dac.voices;
	/* Fifo should be set to hold exactly four samples. Highwater mark
	 * should be set to two samples. */
	highwater = (sample_size * 2) >> 1;	/* halfwords */
	fifobeg = 0;				/* playback is first */
	fifoend = (sample_size * 4) >> 3;	/* doublewords */
	pbus->ctrl = HPC3_PDMACTRL_RT | HPC3_PDMACTRL_LD |
		     (highwater << 8) | (fifobeg << 16) | (fifoend << 24) |
		     (hal2->dac.format & AFMT_S16_LE ? HPC3_PDMACTRL_SEL : 0);
	/* We disable everything before we do anything at all */
	pbus->pbus->pbdma_ctrl = HPC3_PDMACTRL_LD;
	hal2_i_clearbit16(hal2, H2I_DMA_PORT_EN, H2I_DMA_PORT_EN_CODECTX);
	/* Setup the HAL2 for playback */
	hal2_set_dac_rate(hal2);
	/* Set endianess */
	if (hal2->dac.format & AFMT_S16_LE)
		hal2_i_setbit16(hal2, H2I_DMA_END, H2I_DMA_END_CODECTX);
	else
		hal2_i_clearbit16(hal2, H2I_DMA_END, H2I_DMA_END_CODECTX);
	/* Set DMA bus */
	hal2_i_setbit16(hal2, H2I_DMA_DRV, (1 << pbus->pbusnr));
	/* We are using 1st Bresenham clock generator for playback */
	hal2_i_write16(hal2, H2I_DAC_C1, (pbus->pbusnr << H2I_C1_DMA_SHIFT)
			| (1 << H2I_C1_CLKID_SHIFT)
			| (hal2->dac.voices << H2I_C1_DATAT_SHIFT));
}

static void hal2_setup_adc(struct hal2_card *hal2)
{
	unsigned int fifobeg, fifoend, highwater, sample_size;
	struct hal2_pbus *pbus = &hal2->adc.pbus;

	DEBUG("hal2_setup_adc\n");

	sample_size = 2 * hal2->adc.voices;
	highwater = (sample_size * 2) >> 1;		/* halfwords */
	fifobeg = (4 * 4) >> 3;				/* record is second */
	fifoend = (4 * 4 + sample_size * 4) >> 3;	/* doublewords */
	pbus->ctrl = HPC3_PDMACTRL_RT | HPC3_PDMACTRL_RCV | HPC3_PDMACTRL_LD | 
		     (highwater << 8) | (fifobeg << 16) | (fifoend << 24) |
		     (hal2->adc.format & AFMT_S16_LE ? HPC3_PDMACTRL_SEL : 0);
	pbus->pbus->pbdma_ctrl = HPC3_PDMACTRL_LD;
	hal2_i_clearbit16(hal2, H2I_DMA_PORT_EN, H2I_DMA_PORT_EN_CODECR);
	/* Setup the HAL2 for record */
	hal2_set_adc_rate(hal2);
	/* Set endianess */
	if (hal2->adc.format & AFMT_S16_LE)
		hal2_i_setbit16(hal2, H2I_DMA_END, H2I_DMA_END_CODECR);
	else
		hal2_i_clearbit16(hal2, H2I_DMA_END, H2I_DMA_END_CODECR);
	/* Set DMA bus */
	hal2_i_setbit16(hal2, H2I_DMA_DRV, (1 << pbus->pbusnr));
	/* We are using 2nd Bresenham clock generator for record */
	hal2_i_write16(hal2, H2I_ADC_C1, (pbus->pbusnr << H2I_C1_DMA_SHIFT)
			| (2 << H2I_C1_CLKID_SHIFT)
			| (hal2->adc.voices << H2I_C1_DATAT_SHIFT));
}

static dma_addr_t hal2_desc_addr(struct hal2_codec *codec, int i)
{
	if (--i < 0)
		i = codec->desc_count - 1;
	return codec->desc[i].desc.pnext;
}

static void hal2_start_dac(struct hal2_card *hal2)
{
	struct hal2_codec *dac = &hal2->dac;
	struct hal2_pbus *pbus = &dac->pbus;

	pbus->pbus->pbdma_dptr = hal2_desc_addr(dac, dac->tail);
	pbus->pbus->pbdma_ctrl = pbus->ctrl | HPC3_PDMACTRL_ACT;
	/* enable DAC */
	hal2_i_setbit16(hal2, H2I_DMA_PORT_EN, H2I_DMA_PORT_EN_CODECTX);
}

static void hal2_start_adc(struct hal2_card *hal2)
{
	struct hal2_codec *adc = &hal2->adc;
	struct hal2_pbus *pbus = &adc->pbus;

	pbus->pbus->pbdma_dptr = hal2_desc_addr(adc, adc->head);
	pbus->pbus->pbdma_ctrl = pbus->ctrl | HPC3_PDMACTRL_ACT;
	/* enable ADC */
	hal2_i_setbit16(hal2, H2I_DMA_PORT_EN, H2I_DMA_PORT_EN_CODECR);
}

static inline void hal2_stop_dac(struct hal2_card *hal2)
{
	hal2->dac.pbus.pbus->pbdma_ctrl = HPC3_PDMACTRL_LD;
	/* The HAL2 itself may remain enabled safely */
}

static inline void hal2_stop_adc(struct hal2_card *hal2)
{
	hal2->adc.pbus.pbus->pbdma_ctrl = HPC3_PDMACTRL_LD;
}

static int hal2_alloc_dmabuf(struct hal2_codec *codec, int size,
			     int count, int cntinfo, int dir)
{
	struct hal2_desc *desc, *dma_addr;
	int i;

	DEBUG("allocating %dk DMA buffer.\n", size / 1024);

	codec->buffer = (unsigned char *)__get_free_pages(GFP_KERNEL | GFP_DMA,
							  get_order(size));
	if (!codec->buffer)
		return -ENOMEM;
	desc = dma_alloc_coherent(NULL, count * sizeof(struct hal2_desc),
				  (dma_addr_t *)&dma_addr, GFP_KERNEL);
	if (!desc) {
		free_pages((unsigned long)codec->buffer, get_order(size));
		return -ENOMEM;
	}
	codec->desc = desc;
	for (i = 0; i < count; i++) {
		desc->desc.pbuf = dma_map_single(NULL,
			(void *)(codec->buffer + i * H2_BLOCK_SIZE),
			H2_BLOCK_SIZE, dir);
		desc->desc.cntinfo = cntinfo;
		desc->desc.pnext = (i == count - 1) ?
				   (u32)dma_addr : (u32)(dma_addr + i + 1);
		desc->cnt = 0;
		desc++;
	}
	codec->desc_count = count;
	codec->head = codec->tail = 0;
	return 0;
}

static int hal2_alloc_dac_dmabuf(struct hal2_codec *codec)
{
	return hal2_alloc_dmabuf(codec, H2_DAC_BUFSIZE,
				 H2_DAC_BUFSIZE / H2_BLOCK_SIZE,
				 HPCDMA_XIE | HPCDMA_EOX,
				 DMA_TO_DEVICE);
}

static int hal2_alloc_adc_dmabuf(struct hal2_codec *codec)
{
	return hal2_alloc_dmabuf(codec, H2_ADC_BUFSIZE,
				 H2_ADC_BUFSIZE / H2_BLOCK_SIZE,
				 HPCDMA_XIE | H2_BLOCK_SIZE,
				 DMA_TO_DEVICE);
}

static void hal2_free_dmabuf(struct hal2_codec *codec, int size, int dir)
{
	dma_addr_t dma_addr;
	int i;

	dma_addr = codec->desc[codec->desc_count - 1].desc.pnext;
	for (i = 0; i < codec->desc_count; i++)
		dma_unmap_single(NULL, codec->desc[i].desc.pbuf,
				 H2_BLOCK_SIZE, dir);
	dma_free_coherent(NULL, codec->desc_count * sizeof(struct hal2_desc),
			  (void *)codec->desc, dma_addr);
	free_pages((unsigned long)codec->buffer, get_order(size));
}

static void hal2_free_dac_dmabuf(struct hal2_codec *codec)
{
	return hal2_free_dmabuf(codec, H2_DAC_BUFSIZE, DMA_TO_DEVICE);
}

static void hal2_free_adc_dmabuf(struct hal2_codec *codec)
{
	return hal2_free_dmabuf(codec, H2_ADC_BUFSIZE, DMA_FROM_DEVICE);
}

/* 
 * Add 'count' bytes to 'buffer' from DMA ring buffers. Return number of
 * bytes added or -EFAULT if copy_from_user failed.
 */
static int hal2_get_buffer(struct hal2_card *hal2, char *buffer, int count)
{
	unsigned long flags;
	int size, ret = 0;
	unsigned char *buf;
	struct hal2_desc *tail;
	struct hal2_codec *adc = &hal2->adc;

	DEBUG("getting %d bytes ", count);

	spin_lock_irqsave(&adc->lock, flags);
	tail = &adc->desc[adc->tail];
	/* enable DMA stream if there are no data */
	if (!tail->cnt && !(adc->pbus.pbus->pbdma_ctrl & HPC3_PDMACTRL_ISACT))
		hal2_start_adc(hal2);
	while (tail->cnt > 0 && count > 0) {
		size = min((int)tail->cnt, count);
		buf = &adc->buffer[(adc->tail + 1) * H2_BLOCK_SIZE - tail->cnt];
		spin_unlock_irqrestore(&adc->lock, flags);
		dma_sync_single(NULL, tail->desc.pbuf, size, DMA_FROM_DEVICE);
		if (copy_to_user(buffer, buf, size)) {
			ret = -EFAULT;
			goto out;
		}
		spin_lock_irqsave(&adc->lock, flags);
		tail->cnt -= size;
		/* buffer is empty, update tail pointer */
		if (tail->cnt == 0) {
			tail->desc.cntinfo = HPCDMA_XIE | H2_BLOCK_SIZE;
			hal2_inc_tail(adc);
			tail = &adc->desc[adc->tail];
			/* enable DMA stream again if needed */
			if (!(adc->pbus.pbus->pbdma_ctrl & HPC3_PDMACTRL_ISACT))
				hal2_start_adc(hal2);
		}
		buffer += size;
		ret += size;
		count -= size;

		DEBUG("(%d) ", size);
	}
	spin_unlock_irqrestore(&adc->lock, flags);
out:
	DEBUG("\n");

	return ret;
} 

/* 
 * Add 'count' bytes from 'buffer' to DMA ring buffers. Return number of
 * bytes added or -EFAULT if copy_from_user failed.
 */
static int hal2_add_buffer(struct hal2_card *hal2, char *buffer, int count)
{
	unsigned long flags;
	unsigned char *buf;
	int size, ret = 0;
	struct hal2_desc *head;
	struct hal2_codec *dac = &hal2->dac;

	DEBUG("adding %d bytes ", count);

	spin_lock_irqsave(&dac->lock, flags);
	head = &dac->desc[dac->head];
	while (head->cnt == 0 && count > 0) {
		size = min((int)H2_BLOCK_SIZE, count);
		buf = &dac->buffer[dac->head * H2_BLOCK_SIZE];
		spin_unlock_irqrestore(&dac->lock, flags);
		if (copy_from_user(buf, buffer, size)) {
			ret = -EFAULT;
			goto out;
		}
		dma_sync_single(NULL, head->desc.pbuf, size, DMA_TO_DEVICE);
		spin_lock_irqsave(&dac->lock, flags);
		head->desc.cntinfo = size | HPCDMA_XIE;
		head->cnt = size;
		buffer += size;
		ret += size;
		count -= size;
		hal2_inc_head(dac);
		head = &dac->desc[dac->head];

		DEBUG("(%d) ", size);
	}
	if (!(dac->pbus.pbus->pbdma_ctrl & HPC3_PDMACTRL_ISACT) && ret > 0)
		hal2_start_dac(hal2);
	spin_unlock_irqrestore(&dac->lock, flags);
out:
	DEBUG("\n");

	return ret;
}

#define hal2_reset_dac_pointer(hal2)	hal2_reset_pointer(hal2, 1)
#define hal2_reset_adc_pointer(hal2)	hal2_reset_pointer(hal2, 0)
static void hal2_reset_pointer(struct hal2_card *hal2, int is_dac)
{
	int i;
	struct hal2_codec *codec = (is_dac) ? &hal2->dac : &hal2->adc;

	DEBUG("hal2_reset_pointer\n");

	for (i = 0; i < codec->desc_count; i++) {
		codec->desc[i].cnt = 0;
		codec->desc[i].desc.cntinfo = HPCDMA_XIE | (is_dac) ?
					      HPCDMA_EOX : H2_BLOCK_SIZE;
	}
	codec->head = codec->tail = 0;
}

static int hal2_sync_dac(struct hal2_card *hal2)
{
	DECLARE_WAITQUEUE(wait, current);
	struct hal2_codec *dac = &hal2->dac;
	int ret = 0;
	unsigned long flags;
	signed long timeout = 1000 * H2_BLOCK_SIZE * 2 * dac->voices *
			      HZ / dac->sample_rate / 900;

	while (dac->pbus.pbus->pbdma_ctrl & HPC3_PDMACTRL_ISACT) {
		add_wait_queue(&dac->dma_wait, &wait);
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(timeout);
		spin_lock_irqsave(&dac->lock, flags);
		if (dac->desc[dac->tail].cnt)
			ret = -ETIME;
		spin_unlock_irqrestore(&dac->lock, flags);
		if (signal_pending(current))
			ret = -ERESTARTSYS;
		if (ret) {
			hal2_stop_dac(hal2);
			hal2_reset_dac_pointer(hal2);
		}
		remove_wait_queue(&dac->dma_wait, &wait);
	}

	return ret;
}

static int hal2_write_mixer(struct hal2_card *hal2, int index, int vol)
{
	unsigned int l, r, tmp;

	DEBUG_MIX("mixer %d write\n", index);

	if (index >= SOUND_MIXER_NRDEVICES || !mixtable[index].avail)
		return -EINVAL;

	r = (vol >> 8) & 0xff;
	if (r > 100)
		r = 100;
	l = vol & 0xff;
	if (l > 100)
		l = 100;

	hal2->mixer.volume[mixtable[index].idx] = l | (r << 8);

	switch (mixtable[index].idx) {
	case H2_MIX_OUTPUT_ATT:

		DEBUG_MIX("output attenuator %d,%d\n", l, r);

		if (r | l) {
			tmp = hal2_i_look32(hal2, H2I_DAC_C2);
			tmp &= ~(H2I_C2_L_ATT_M | H2I_C2_R_ATT_M | H2I_C2_MUTE);

			/* Attenuator has five bits */
			l = 31 * (100 - l) / 99;
			r = 31 * (100 - r) / 99;

			DEBUG_MIX("left: %d, right %d\n", l, r);

			tmp |= (l << H2I_C2_L_ATT_SHIFT) & H2I_C2_L_ATT_M;
			tmp |= (r << H2I_C2_R_ATT_SHIFT) & H2I_C2_R_ATT_M;
			hal2_i_write32(hal2, H2I_DAC_C2, tmp);
		} else 
			hal2_i_setbit32(hal2, H2I_DAC_C2, H2I_C2_MUTE);
		break;
	case H2_MIX_INPUT_GAIN:

		DEBUG_MIX("input gain %d,%d\n", l, r);

		tmp = hal2_i_look32(hal2, H2I_ADC_C2);
		tmp &= ~(H2I_C2_L_GAIN_M | H2I_C2_R_GAIN_M);

		/* Gain control has four bits */
		l = 16 * l / 100;
		r = 16 * r / 100;

		DEBUG_MIX("left: %d, right %d\n", l, r);

		tmp |= (l << H2I_C2_L_GAIN_SHIFT) & H2I_C2_L_GAIN_M;
		tmp |= (r << H2I_C2_R_GAIN_SHIFT) & H2I_C2_R_GAIN_M;
		hal2_i_write32(hal2, H2I_ADC_C2, tmp);

		break;
	}

	return 0;
}

static void hal2_init_mixer(struct hal2_card *hal2)
{
	int i;

	for (i = 0; i < SOUND_MIXER_NRDEVICES; i++)
		if (mixtable[i].avail)
			hal2->mixer.volume[mixtable[i].idx] = 100 | (100 << 8);

	/* disable attenuator */
	hal2_i_write32(hal2, H2I_DAC_C2, 0);
	/* set max input gain */
	hal2_i_write32(hal2, H2I_ADC_C2, H2I_C2_MUTE |
			(H2I_C2_L_GAIN_M << H2I_C2_L_GAIN_SHIFT) |
			(H2I_C2_R_GAIN_M << H2I_C2_R_GAIN_SHIFT));
	/* set max volume */
	hal2->mixer.master = 0xff;
	hal2->vol_regs->left = 0xff;
	hal2->vol_regs->right = 0xff;
}

/*
 * XXX: later i'll implement mixer for main volume which will be disabled
 * by default. enabling it users will be allowed to have master volume level
 * control on panel in their favourite X desktop
 */
static void hal2_volume_control(int direction)
{
	unsigned int master = hal2_card[0]->mixer.master;
	struct hal2_vol_regs *vol = hal2_card[0]->vol_regs;

	/* volume up */
	if (direction > 0 && master < 0xff)
		master++;
	/* volume down */
	else if (direction < 0 && master > 0)
		master--;
	/* TODO: mute/unmute */
	vol->left = master;
	vol->right = master;
	hal2_card[0]->mixer.master = master;
}

static int hal2_mixer_ioctl(struct hal2_card *hal2, unsigned int cmd,
			    unsigned long arg)
{
	int val;

        if (cmd == SOUND_MIXER_INFO) {
		mixer_info info;

		memset(&info, 0, sizeof(info));
		strlcpy(info.id, hal2str, sizeof(info.id));
		strlcpy(info.name, hal2str, sizeof(info.name));
		info.modify_counter = hal2->mixer.modcnt;
		if (copy_to_user((void *)arg, &info, sizeof(info)))
			return -EFAULT;
		return 0;
	}
	if (cmd == SOUND_OLD_MIXER_INFO) {
		_old_mixer_info info;

		memset(&info, 0, sizeof(info));
		strlcpy(info.id, hal2str, sizeof(info.id));
		strlcpy(info.name, hal2str, sizeof(info.name));
		if (copy_to_user((void *)arg, &info, sizeof(info)))
			return -EFAULT;
		return 0;
	}
	if (cmd == OSS_GETVERSION)
		return put_user(SOUND_VERSION, (int *)arg);

	if (_IOC_TYPE(cmd) != 'M' || _IOC_SIZE(cmd) != sizeof(int))
                return -EINVAL;

        if (_IOC_DIR(cmd) == _IOC_READ) {
                switch (_IOC_NR(cmd)) {
		/* Give the current record source */
		case SOUND_MIXER_RECSRC:
			val = 0;	/* FIXME */
			break;
		/* Give the supported mixers, all of them support stereo */
                case SOUND_MIXER_DEVMASK:
                case SOUND_MIXER_STEREODEVS: {
			int i;

			for (val = i = 0; i < SOUND_MIXER_NRDEVICES; i++)
				if (mixtable[i].avail)
					val |= 1 << i;
			break;
			}
		/* Arg contains a bit for each supported recording source */
                case SOUND_MIXER_RECMASK:
			val = 0;
			break;
                case SOUND_MIXER_CAPS:
			val = 0;
			break;
		/* Read a specific mixer */
		default: {
			int i = _IOC_NR(cmd);

			if (i >= SOUND_MIXER_NRDEVICES || !mixtable[i].avail)
				return -EINVAL;
			val = hal2->mixer.volume[mixtable[i].idx];
			break;
			}
		}
		return put_user(val, (int *)arg);
	}

        if (_IOC_DIR(cmd) != (_IOC_WRITE|_IOC_READ))
		return -EINVAL;

	hal2->mixer.modcnt++;

	if (get_user(val, (int *)arg))
		return -EFAULT;

	switch (_IOC_NR(cmd)) {
	/* Arg contains a bit for each recording source */
	case SOUND_MIXER_RECSRC:
		return 0;	/* FIXME */
	default:
		return hal2_write_mixer(hal2, _IOC_NR(cmd), val);
	}

	return 0;
}

static int hal2_open_mixdev(struct inode *inode, struct file *file)
{
	struct hal2_card *hal2 = hal2_mixer_find_card(iminor(inode));

	if (hal2) {
		file->private_data = hal2;
		return nonseekable_open(inode, file);
	}
	return -ENODEV;
}

static int hal2_release_mixdev(struct inode *inode, struct file *file)
{
	return 0;
}

static int hal2_ioctl_mixdev(struct inode *inode, struct file *file,
			     unsigned int cmd, unsigned long arg)
{
	return hal2_mixer_ioctl((struct hal2_card *)file->private_data, cmd, arg);
}

static int hal2_ioctl(struct inode *inode, struct file *file, 
		      unsigned int cmd, unsigned long arg)
{
	int val;
	struct hal2_card *hal2 = (struct hal2_card *) file->private_data;

	switch (cmd) {
	case OSS_GETVERSION:
		return put_user(SOUND_VERSION, (int *)arg);

	case SNDCTL_DSP_SYNC:
		if (file->f_mode & FMODE_WRITE)
			return hal2_sync_dac(hal2);
		return 0;

	case SNDCTL_DSP_SETDUPLEX:
		return 0;

	case SNDCTL_DSP_GETCAPS:
		return put_user(DSP_CAP_DUPLEX | DSP_CAP_MULTI, (int *)arg);

	case SNDCTL_DSP_RESET:
		if (file->f_mode & FMODE_READ) {
			hal2_stop_adc(hal2);
			hal2_reset_adc_pointer(hal2);
		}
		if (file->f_mode & FMODE_WRITE) {
			hal2_stop_dac(hal2);
			hal2_reset_dac_pointer(hal2);
		}
		return 0;

 	case SNDCTL_DSP_SPEED:
		if (get_user(val, (int *)arg))
			return -EFAULT;
		if (file->f_mode & FMODE_READ) {
			hal2_stop_adc(hal2);
			val = hal2_compute_rate(&hal2->adc, val);
			hal2->adc.sample_rate = val;
			hal2_set_adc_rate(hal2);
		}
		if (file->f_mode & FMODE_WRITE) {
			hal2_stop_dac(hal2);
			val = hal2_compute_rate(&hal2->dac, val);
			hal2->dac.sample_rate = val;
			hal2_set_dac_rate(hal2);
		}
		return put_user(val, (int *)arg);

	case SNDCTL_DSP_STEREO:
		if (get_user(val, (int *)arg))
			return -EFAULT;
		if (file->f_mode & FMODE_READ) {
			hal2_stop_adc(hal2);
			hal2->adc.voices = (val) ? 2 : 1;
			hal2_setup_adc(hal2);
		}
		if (file->f_mode & FMODE_WRITE) {
			hal2_stop_dac(hal2);
			hal2->dac.voices = (val) ? 2 : 1;
			hal2_setup_dac(hal2);
                }
		return 0;

	case SNDCTL_DSP_CHANNELS:
		if (get_user(val, (int *)arg))
			return -EFAULT;
		if (val != 0) {
			if (file->f_mode & FMODE_READ) {
				hal2_stop_adc(hal2);
				hal2->adc.voices = (val == 1) ? 1 : 2;
				hal2_setup_adc(hal2);
			}
			if (file->f_mode & FMODE_WRITE) {
				hal2_stop_dac(hal2);
				hal2->dac.voices = (val == 1) ? 1 : 2;
				hal2_setup_dac(hal2);
			}
		}
		val = -EINVAL;
		if (file->f_mode & FMODE_READ)
			val = hal2->adc.voices;
		if (file->f_mode & FMODE_WRITE)
			val = hal2->dac.voices;
		return put_user(val, (int *)arg);

	case SNDCTL_DSP_GETFMTS: /* Returns a mask */
                return put_user(H2_SUPPORTED_FORMATS, (int *)arg);

	case SNDCTL_DSP_SETFMT: /* Selects ONE fmt*/
		if (get_user(val, (int *)arg))
			return -EFAULT;
		if (val != AFMT_QUERY) {
			if (!(val & H2_SUPPORTED_FORMATS))
				return -EINVAL;
			if (file->f_mode & FMODE_READ) {
				hal2_stop_adc(hal2);
				hal2->adc.format = val;
				hal2_setup_adc(hal2);
			}
			if (file->f_mode & FMODE_WRITE) {
				hal2_stop_dac(hal2);
				hal2->dac.format = val;
				hal2_setup_dac(hal2);
			}
		} else {
			val = -EINVAL;
			if (file->f_mode & FMODE_READ)
				val = hal2->adc.format;
			if (file->f_mode & FMODE_WRITE)
				val = hal2->dac.format;
		}
		return put_user(val, (int *)arg);

	case SNDCTL_DSP_POST:
		return 0;

	case SNDCTL_DSP_GETOSPACE: {
		audio_buf_info info;
		int i;
		unsigned long flags;
		struct hal2_codec *dac = &hal2->dac;

		if (!(file->f_mode & FMODE_WRITE))
			return -EINVAL;
		info.fragments = 0;
		spin_lock_irqsave(&dac->lock, flags);
		for (i = 0; i < dac->desc_count; i++)
			if (dac->desc[i].cnt == 0)
				info.fragments++;
		spin_unlock_irqrestore(&dac->lock, flags);
		info.fragstotal = dac->desc_count;
		info.fragsize = H2_BLOCK_SIZE;
                info.bytes = info.fragsize * info.fragments;

		return copy_to_user((void *)arg, &info, sizeof(info)) ? -EFAULT : 0;
	}

	case SNDCTL_DSP_GETISPACE: {
		audio_buf_info info;
		int i;
		unsigned long flags;
		struct hal2_codec *adc = &hal2->adc;

		if (!(file->f_mode & FMODE_READ))
			return -EINVAL;
		info.fragments = 0;
		info.bytes = 0;
		spin_lock_irqsave(&adc->lock, flags);
		for (i = 0; i < adc->desc_count; i++)
			if (adc->desc[i].cnt > 0) {
				info.fragments++;
				info.bytes += adc->desc[i].cnt;
			}
		spin_unlock_irqrestore(&adc->lock, flags);
		info.fragstotal = adc->desc_count;
		info.fragsize = H2_BLOCK_SIZE;

		return copy_to_user((void *)arg, &info, sizeof(info)) ? -EFAULT : 0;
	}

	case SNDCTL_DSP_NONBLOCK:
		file->f_flags |= O_NONBLOCK;
		return 0;

	case SNDCTL_DSP_GETBLKSIZE:
		return put_user(H2_BLOCK_SIZE, (int *)arg);

	case SNDCTL_DSP_SETFRAGMENT:
		return 0;

	case SOUND_PCM_READ_RATE:
		val = -EINVAL;
		if (file->f_mode & FMODE_READ)
			val = hal2->adc.sample_rate;
		if (file->f_mode & FMODE_WRITE)
			val = hal2->dac.sample_rate;
		return put_user(val, (int *)arg);

	case SOUND_PCM_READ_CHANNELS:
		val = -EINVAL;
		if (file->f_mode & FMODE_READ)
			val = hal2->adc.voices;
		if (file->f_mode & FMODE_WRITE)
			val = hal2->dac.voices;
		return put_user(val, (int *)arg);

	case SOUND_PCM_READ_BITS:
		return put_user(16, (int *)arg);
	}

	return hal2_mixer_ioctl(hal2, cmd, arg);
}

static ssize_t hal2_read(struct file *file, char *buffer,
			 size_t count, loff_t *ppos)
{
	ssize_t err;
	struct hal2_card *hal2 = (struct hal2_card *) file->private_data;
	struct hal2_codec *adc = &hal2->adc;

	if (!count)
		return 0;
	if (mutex_lock_interruptible(&adc->sem))
		return -EINTR;
	if (file->f_flags & O_NONBLOCK) {
		err = hal2_get_buffer(hal2, buffer, count);
		err = err == 0 ? -EAGAIN : err;
	} else {
		do {
			/* ~10% longer */
			signed long timeout = 1000 * H2_BLOCK_SIZE *
				2 * adc->voices * HZ / adc->sample_rate / 900;
			unsigned long flags;
			DECLARE_WAITQUEUE(wait, current);
			ssize_t cnt = 0;

			err = hal2_get_buffer(hal2, buffer, count);
			if (err > 0) {
				count -= err;
				cnt += err;
				buffer += err;
				err = cnt;
			}
			if (count > 0 && err >= 0) {
				add_wait_queue(&adc->dma_wait, &wait);
				set_current_state(TASK_INTERRUPTIBLE);
				schedule_timeout(timeout);
				spin_lock_irqsave(&adc->lock, flags);
				if (!adc->desc[adc->tail].cnt)
					err = -EAGAIN;
				spin_unlock_irqrestore(&adc->lock, flags);
				if (signal_pending(current))
					err = -ERESTARTSYS;
				remove_wait_queue(&adc->dma_wait, &wait);
				if (err < 0) {
					hal2_stop_adc(hal2);
					hal2_reset_adc_pointer(hal2);
				}
			}
		} while (count > 0 && err >= 0);
	}
	mutex_unlock(&adc->sem);

	return err;
}

static ssize_t hal2_write(struct file *file, const char *buffer,
			  size_t count, loff_t *ppos)
{
	ssize_t err;
	char *buf = (char*) buffer;
	struct hal2_card *hal2 = (struct hal2_card *) file->private_data;
	struct hal2_codec *dac = &hal2->dac;

	if (!count)
		return 0;
	if (mutex_lock_interruptible(&dac->sem))
		return -EINTR;
	if (file->f_flags & O_NONBLOCK) {
		err = hal2_add_buffer(hal2, buf, count);
		err = err == 0 ? -EAGAIN : err;
	} else {
		do {
			/* ~10% longer */
			signed long timeout = 1000 * H2_BLOCK_SIZE *
				2 * dac->voices * HZ / dac->sample_rate / 900;
			unsigned long flags;
			DECLARE_WAITQUEUE(wait, current);
			ssize_t cnt = 0;

			err = hal2_add_buffer(hal2, buf, count);
			if (err > 0) {
				count -= err;
				cnt += err;
				buf += err;
				err = cnt;
			}
			if (count > 0 && err >= 0) {
				add_wait_queue(&dac->dma_wait, &wait);
				set_current_state(TASK_INTERRUPTIBLE);
				schedule_timeout(timeout);
				spin_lock_irqsave(&dac->lock, flags);
				if (dac->desc[dac->head].cnt)
					err = -EAGAIN;
				spin_unlock_irqrestore(&dac->lock, flags);
				if (signal_pending(current))
					err = -ERESTARTSYS;
				remove_wait_queue(&dac->dma_wait, &wait);
				if (err < 0) {
					hal2_stop_dac(hal2);
					hal2_reset_dac_pointer(hal2);
				}
			}
		} while (count > 0 && err >= 0);
	}
	mutex_unlock(&dac->sem);

	return err;
}

static unsigned int hal2_poll(struct file *file, struct poll_table_struct *wait)
{
	unsigned long flags;
	unsigned int mask = 0;
	struct hal2_card *hal2 = (struct hal2_card *) file->private_data;

	if (file->f_mode & FMODE_READ) {
		struct hal2_codec *adc = &hal2->adc;

		poll_wait(file, &adc->dma_wait, wait);
		spin_lock_irqsave(&adc->lock, flags);
		if (adc->desc[adc->tail].cnt > 0)
			mask |= POLLIN;
		spin_unlock_irqrestore(&adc->lock, flags);
	}

	if (file->f_mode & FMODE_WRITE) {
		struct hal2_codec *dac = &hal2->dac;

		poll_wait(file, &dac->dma_wait, wait);
		spin_lock_irqsave(&dac->lock, flags);
		if (dac->desc[dac->head].cnt == 0)
			mask |= POLLOUT;
		spin_unlock_irqrestore(&dac->lock, flags);
	}

	return mask;
}

static int hal2_open(struct inode *inode, struct file *file)
{
	int err;
	struct hal2_card *hal2 = hal2_dsp_find_card(iminor(inode));

	if (!hal2)
		return -ENODEV;
	file->private_data = hal2;
	if (file->f_mode & FMODE_READ) {
		struct hal2_codec *adc = &hal2->adc;

		if (adc->usecount)
			return -EBUSY;
		/* OSS spec wanted us to use 8 bit, 8 kHz mono by default,
		 * but HAL2 can't do 8bit audio */
		adc->format = AFMT_S16_BE;
		adc->voices = 1;
		adc->sample_rate = hal2_compute_rate(adc, 8000);
		hal2_set_adc_rate(hal2);
		err = hal2_alloc_adc_dmabuf(adc);
		if (err)
			return err;
		hal2_setup_adc(hal2);
		adc->usecount++;
	}
	if (file->f_mode & FMODE_WRITE) {
		struct hal2_codec *dac = &hal2->dac;

		if (dac->usecount)
			return -EBUSY;
		dac->format = AFMT_S16_BE;
		dac->voices = 1;
		dac->sample_rate = hal2_compute_rate(dac, 8000);
		hal2_set_dac_rate(hal2);
		err = hal2_alloc_dac_dmabuf(dac);
		if (err)
			return err;
		hal2_setup_dac(hal2);
		dac->usecount++;
	}

	return nonseekable_open(inode, file);
}

static int hal2_release(struct inode *inode, struct file *file)
{
	struct hal2_card *hal2 = (struct hal2_card *) file->private_data;

	if (file->f_mode & FMODE_READ) {
		struct hal2_codec *adc = &hal2->adc;

		mutex_lock(&adc->sem);
		hal2_stop_adc(hal2);
		hal2_free_adc_dmabuf(adc);
		adc->usecount--;
		mutex_unlock(&adc->sem);
	}
	if (file->f_mode & FMODE_WRITE) {
		struct hal2_codec *dac = &hal2->dac;

		mutex_lock(&dac->sem);
		hal2_sync_dac(hal2);
		hal2_free_dac_dmabuf(dac);
		dac->usecount--;
		mutex_unlock(&dac->sem);
	}

	return 0;
}

static struct file_operations hal2_audio_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.read		= hal2_read,
	.write		= hal2_write,
	.poll		= hal2_poll,
	.ioctl		= hal2_ioctl,
	.open		= hal2_open,
	.release	= hal2_release,
};

static struct file_operations hal2_mixer_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.ioctl		= hal2_ioctl_mixdev,
	.open		= hal2_open_mixdev,
	.release	= hal2_release_mixdev,
};

static void hal2_init_codec(struct hal2_codec *codec, struct hpc3_regs *hpc3,
			    int index)
{
	codec->pbus.pbusnr = index;
	codec->pbus.pbus = &hpc3->pbdma[index];
	init_waitqueue_head(&codec->dma_wait);
	mutex_init(&codec->sem);
	spin_lock_init(&codec->lock);
}

static int hal2_detect(struct hal2_card *hal2)
{
	unsigned short board, major, minor;
	unsigned short rev;

	/* reset HAL2 */
	hal2_isr_write(hal2, 0);
	/* release reset */
	hal2_isr_write(hal2, H2_ISR_GLOBAL_RESET_N | H2_ISR_CODEC_RESET_N);

	hal2_i_write16(hal2, H2I_RELAY_C, H2I_RELAY_C_STATE); 
	if ((rev = hal2_rev_look(hal2)) & H2_REV_AUDIO_PRESENT)
		return -ENODEV;

	board = (rev & H2_REV_BOARD_M) >> 12;
	major = (rev & H2_REV_MAJOR_CHIP_M) >> 4;
	minor = (rev & H2_REV_MINOR_CHIP_M);

	printk(KERN_INFO "SGI HAL2 revision %i.%i.%i\n",
	       board, major, minor);

	return 0;
}

static int hal2_init_card(struct hal2_card **phal2, struct hpc3_regs *hpc3)
{
	int ret = 0;
	struct hal2_card *hal2;

	hal2 = kmalloc(sizeof(struct hal2_card), GFP_KERNEL);
	if (!hal2)
		return -ENOMEM;
	memset(hal2, 0, sizeof(struct hal2_card));

	hal2->ctl_regs = (struct hal2_ctl_regs *)hpc3->pbus_extregs[0];
	hal2->aes_regs = (struct hal2_aes_regs *)hpc3->pbus_extregs[1];
	hal2->vol_regs = (struct hal2_vol_regs *)hpc3->pbus_extregs[2];
	hal2->syn_regs = (struct hal2_syn_regs *)hpc3->pbus_extregs[3];

	if (hal2_detect(hal2) < 0) {
		ret = -ENODEV;
		goto free_card;
	}

	hal2_init_codec(&hal2->dac, hpc3, 0);
	hal2_init_codec(&hal2->adc, hpc3, 1);

	/*
	 * All DMA channel interfaces in HAL2 are designed to operate with
	 * PBUS programmed for 2 cycles in D3, 2 cycles in D4 and 2 cycles
	 * in D5. HAL2 is a 16-bit device which can accept both big and little
	 * endian format. It assumes that even address bytes are on high
	 * portion of PBUS (15:8) and assumes that HPC3 is programmed to
	 * accept a live (unsynchronized) version of P_DREQ_N from HAL2.
	 */
#define HAL2_PBUS_DMACFG ((0 << HPC3_DMACFG_D3R_SHIFT) | \
			  (2 << HPC3_DMACFG_D4R_SHIFT) | \
			  (2 << HPC3_DMACFG_D5R_SHIFT) | \
			  (0 << HPC3_DMACFG_D3W_SHIFT) | \
			  (2 << HPC3_DMACFG_D4W_SHIFT) | \
			  (2 << HPC3_DMACFG_D5W_SHIFT) | \
				HPC3_DMACFG_DS16 | \
				HPC3_DMACFG_EVENHI | \
				HPC3_DMACFG_RTIME | \
			  (8 << HPC3_DMACFG_BURST_SHIFT) | \
				HPC3_DMACFG_DRQLIVE)
	/*
	 * Ignore what's mentioned in the specification and write value which
	 * works in The Real World (TM)
	 */
	hpc3->pbus_dmacfg[hal2->dac.pbus.pbusnr][0] = 0x8208844;
	hpc3->pbus_dmacfg[hal2->adc.pbus.pbusnr][0] = 0x8208844;

	if (request_irq(SGI_HPCDMA_IRQ, hal2_interrupt, IRQF_SHARED,
			hal2str, hal2)) {
		printk(KERN_ERR "HAL2: Can't get irq %d\n", SGI_HPCDMA_IRQ);
		ret = -EAGAIN;
		goto free_card;
	}

	hal2->dev_dsp = register_sound_dsp(&hal2_audio_fops, -1);
	if (hal2->dev_dsp < 0) {
		ret = hal2->dev_dsp;
		goto free_irq;
	}

	hal2->dev_mixer = register_sound_mixer(&hal2_mixer_fops, -1);
	if (hal2->dev_mixer < 0) {
		ret = hal2->dev_mixer;
		goto unregister_dsp;
	}

	hal2_init_mixer(hal2);

	*phal2 = hal2;
	return 0;
unregister_dsp:
	unregister_sound_dsp(hal2->dev_dsp);
free_irq:
	free_irq(SGI_HPCDMA_IRQ, hal2);
free_card:
	kfree(hal2);

	return ret;
}

extern void (*indy_volume_button)(int);

/* 
 * Assuming only one HAL2 card. Mail me if you ever meet machine with
 * more than one.
 */
static int __init init_hal2(void)
{
	int i, error;

	for (i = 0; i < MAXCARDS; i++)
		hal2_card[i] = NULL;

	error = hal2_init_card(&hal2_card[0], hpc3c0);

	/* let Indy's volume buttons work */
	if (!error && !ip22_is_fullhouse())
		indy_volume_button = hal2_volume_control;

	return error;

}

static void __exit exit_hal2(void)
{
	int i;

	/* unregister volume butons callback function */
	indy_volume_button = NULL;
	
	for (i = 0; i < MAXCARDS; i++)
		if (hal2_card[i]) {
			free_irq(SGI_HPCDMA_IRQ, hal2_card[i]);
			unregister_sound_dsp(hal2_card[i]->dev_dsp);
			unregister_sound_mixer(hal2_card[i]->dev_mixer);
			kfree(hal2_card[i]);
	}
}

module_init(init_hal2);
module_exit(exit_hal2);

MODULE_DESCRIPTION("OSS compatible driver for SGI HAL2 audio");
MODULE_AUTHOR("Ladislav Michl");
MODULE_LICENSE("GPL");
