/***********************************************************************
 * Copyright 2001 MontaVista Software Inc.
 * Author: Jun Sun, jsun@mvista.com or jsun@junsun.net
 *
 * drivers/sound/nec_vrc5477.c
 *     AC97 sound dirver for NEC Vrc5477 chip (an integrated, 
 *     multi-function controller chip for MIPS CPUs)
 *
 * VRA support Copyright 2001 Bradley D. LaRonde <brad@ltc.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 ***********************************************************************
 */

/*
 * This code is derived from ite8172.c, which is written by Steve Longerbeam.
 *
 * Features:
 *   Currently we only support the following capabilities:
 *	. mono output to PCM L/R (line out).
 *	. stereo output to PCM L/R (line out).
 *	. mono input from PCM L (line in).
 *	. stereo output from PCM (line in).
 *	. sampling rate at 48k or variable sampling rate 
 *	. support /dev/dsp, /dev/mixer devices, standard OSS devices.
 *	. only support 16-bit PCM format (hardware limit, no software
 *	  translation) 
 *	. support duplex, but no trigger or realtime.
 *	
 *   Specifically the following are not supported:
 *	. app-set frag size.
 *	. mmap'ed buffer access
 */

/* 
 * Original comments from ite8172.c file.
 */

/*
 *
 * Notes:
 *
 *  1. Much of the OSS buffer allocation, ioctl's, and mmap'ing are
 *     taken, slightly modified or not at all, from the ES1371 driver,
 *     so refer to the credits in es1371.c for those. The rest of the
 *     code (probe, open, read, write, the ISR, etc.) is new.
 *  2. The following support is untested:
 *      * Memory mapping the audio buffers, and the ioctl controls that go
 *        with it.
 *      * S/PDIF output.
 *  3. The following is not supported:
 *      * I2S input.
 *      * legacy audio mode.
 *  4. Support for volume button interrupts is implemented but doesn't
 *     work yet.
 *
 *  Revision history
 *    02.08.2001  0.1   Initial release
 */

#include <linux/module.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/sound.h>
#include <linux/slab.h>
#include <linux/soundcard.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/bitops.h>
#include <linux/proc_fs.h>
#include <linux/spinlock.h>
#include <linux/smp_lock.h>
#include <linux/ac97_codec.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/uaccess.h>

/* -------------------debug macros -------------------------------------- */
/* #undef VRC5477_AC97_DEBUG */
#define VRC5477_AC97_DEBUG

#undef VRC5477_AC97_VERBOSE_DEBUG
/* #define VRC5477_AC97_VERBOSE_DEBUG */

#if defined(VRC5477_AC97_VERBOSE_DEBUG)
#define VRC5477_AC97_DEBUG
#endif

#if defined(VRC5477_AC97_DEBUG)
#define ASSERT(x)  if (!(x)) { \
	panic("assertion failed at %s:%d: %s\n", __FILE__, __LINE__, #x); }
#else
#define	ASSERT(x)
#endif /* VRC5477_AC97_DEBUG */

#if defined(VRC5477_AC97_VERBOSE_DEBUG)
static u16 inTicket; 		/* check sync between intr & write */
static u16 outTicket;
#endif

/* --------------------------------------------------------------------- */

#undef OSS_DOCUMENTED_MIXER_SEMANTICS

static const unsigned sample_shift[] = { 0, 1, 1, 2 };

#define         VRC5477_INT_CLR         0x0
#define         VRC5477_INT_STATUS	0x0
#define         VRC5477_CODEC_WR        0x4
#define         VRC5477_CODEC_RD        0x8
#define         VRC5477_CTRL            0x18
#define         VRC5477_ACLINK_CTRL     0x1c
#define         VRC5477_INT_MASK        0x24

#define		VRC5477_DAC1_CTRL	0x30
#define		VRC5477_DAC1L		0x34
#define		VRC5477_DAC1_BADDR	0x38
#define		VRC5477_DAC2_CTRL	0x3c
#define		VRC5477_DAC2L		0x40
#define		VRC5477_DAC2_BADDR	0x44
#define		VRC5477_DAC3_CTRL	0x48
#define		VRC5477_DAC3L		0x4c
#define		VRC5477_DAC3_BADDR	0x50

#define		VRC5477_ADC1_CTRL	0x54
#define		VRC5477_ADC1L		0x58
#define		VRC5477_ADC1_BADDR	0x5c
#define		VRC5477_ADC2_CTRL	0x60
#define		VRC5477_ADC2L		0x64
#define		VRC5477_ADC2_BADDR	0x68
#define		VRC5477_ADC3_CTRL	0x6c
#define		VRC5477_ADC3L		0x70
#define		VRC5477_ADC3_BADDR	0x74

#define		VRC5477_CODEC_WR_RWC	(1 << 23)

#define		VRC5477_CODEC_RD_RRDYA	(1 << 31)
#define		VRC5477_CODEC_RD_RRDYD	(1 << 30)

#define		VRC5477_ACLINK_CTRL_RST_ON	(1 << 15)
#define		VRC5477_ACLINK_CTRL_RST_TIME	0x7f
#define		VRC5477_ACLINK_CTRL_SYNC_ON	(1 << 30)
#define		VRC5477_ACLINK_CTRL_CK_STOP_ON	(1 << 31)

#define		VRC5477_CTRL_DAC2ENB		(1 << 15) 
#define		VRC5477_CTRL_ADC2ENB		(1 << 14) 
#define		VRC5477_CTRL_DAC1ENB		(1 << 13) 
#define		VRC5477_CTRL_ADC1ENB		(1 << 12) 

#define		VRC5477_INT_MASK_NMASK		(1 << 31) 
#define		VRC5477_INT_MASK_DAC1END	(1 << 5) 
#define		VRC5477_INT_MASK_DAC2END	(1 << 4) 
#define		VRC5477_INT_MASK_DAC3END	(1 << 3) 
#define		VRC5477_INT_MASK_ADC1END	(1 << 2) 
#define		VRC5477_INT_MASK_ADC2END	(1 << 1) 
#define		VRC5477_INT_MASK_ADC3END	(1 << 0) 

#define		VRC5477_DMA_ACTIVATION		(1 << 31)
#define		VRC5477_DMA_WIP			(1 << 30)


#define VRC5477_AC97_MODULE_NAME "NEC_Vrc5477_audio"
#define PFX VRC5477_AC97_MODULE_NAME ": "

/* --------------------------------------------------------------------- */

struct vrc5477_ac97_state {
	/* list of vrc5477_ac97 devices */
	struct list_head devs;

	/* the corresponding pci_dev structure */
	struct pci_dev *dev;

	/* soundcore stuff */
	int dev_audio;

	/* hardware resources */
	unsigned long io;
	unsigned int irq;

#ifdef VRC5477_AC97_DEBUG
	/* debug /proc entry */
	struct proc_dir_entry *ps;
	struct proc_dir_entry *ac97_ps;
#endif /* VRC5477_AC97_DEBUG */

	struct ac97_codec *codec;

	unsigned dacChannels, adcChannels;
	unsigned short dacRate, adcRate;
	unsigned short extended_status;

	spinlock_t lock;
	struct semaphore open_sem;
	mode_t open_mode;
	wait_queue_head_t open_wait;

	struct dmabuf {
		void *lbuf, *rbuf;
		dma_addr_t lbufDma, rbufDma;
		unsigned bufOrder;
		unsigned numFrag;
		unsigned fragShift;
		unsigned fragSize;	/* redundant */
		unsigned fragTotalSize;	/* = numFrag * fragSize(real)  */
		unsigned nextIn;
		unsigned nextOut;
		int count;
		unsigned error; /* over/underrun */
		wait_queue_head_t wait;
		/* OSS stuff */
		unsigned stopped:1;
		unsigned ready:1;
	} dma_dac, dma_adc;

	#define	WORK_BUF_SIZE	2048
	struct {
		u16 lchannel;
		u16 rchannel;
	} workBuf[WORK_BUF_SIZE/4];
};

/* --------------------------------------------------------------------- */

static LIST_HEAD(devs);

/* --------------------------------------------------------------------- */

static inline unsigned ld2(unsigned int x)
{
    unsigned r = 0;
	
    if (x >= 0x10000) {
	x >>= 16;
	r += 16;
    }
    if (x >= 0x100) {
	x >>= 8;
	r += 8;
    }
    if (x >= 0x10) {
	x >>= 4;
	r += 4;
    }
    if (x >= 4) {
	x >>= 2;
	r += 2;
    }
    if (x >= 2)
	r++;
    return r;
}

/* --------------------------------------------------------------------- */

static u16 rdcodec(struct ac97_codec *codec, u8 addr)
{
	struct vrc5477_ac97_state *s = 
		(struct vrc5477_ac97_state *)codec->private_data;
	unsigned long flags;
	u32 result;

	spin_lock_irqsave(&s->lock, flags);

	/* wait until we can access codec registers */
	while (inl(s->io + VRC5477_CODEC_WR) & 0x80000000);

	/* write the address and "read" command to codec */
	addr = addr & 0x7f;
	outl((addr << 16) | VRC5477_CODEC_WR_RWC, s->io + VRC5477_CODEC_WR);

	/* get the return result */
	udelay(100); /* workaround hardware bug */
	while ( (result = inl(s->io + VRC5477_CODEC_RD)) & 
                (VRC5477_CODEC_RD_RRDYA | VRC5477_CODEC_RD_RRDYD) ) {
		/* we get either addr or data, or both */
		if (result & VRC5477_CODEC_RD_RRDYA) {
			ASSERT(addr == ((result >> 16) & 0x7f) );
		}
		if (result & VRC5477_CODEC_RD_RRDYD) {
			break;
		}
	}

	spin_unlock_irqrestore(&s->lock, flags);

	return result & 0xffff;
}


static void wrcodec(struct ac97_codec *codec, u8 addr, u16 data)
{
	struct vrc5477_ac97_state *s = 
		(struct vrc5477_ac97_state *)codec->private_data;
	unsigned long flags;

	spin_lock_irqsave(&s->lock, flags);

	/* wait until we can access codec registers */
	while (inl(s->io + VRC5477_CODEC_WR) & 0x80000000);

	/* write the address and value to codec */
	outl((addr << 16) | data, s->io + VRC5477_CODEC_WR);

	spin_unlock_irqrestore(&s->lock, flags);
}


static void waitcodec(struct ac97_codec *codec)
{
	struct vrc5477_ac97_state *s = 
		(struct vrc5477_ac97_state *)codec->private_data;

	/* wait until we can access codec registers */
	while (inl(s->io + VRC5477_CODEC_WR) & 0x80000000);
}

static int ac97_codec_not_present(struct ac97_codec *codec)
{
	struct vrc5477_ac97_state *s = 
		(struct vrc5477_ac97_state *)codec->private_data;
	unsigned long flags;
	unsigned short count  = 0xffff; 

	spin_lock_irqsave(&s->lock, flags);

	/* wait until we can access codec registers */
	do {
	       if (!(inl(s->io + VRC5477_CODEC_WR) & 0x80000000))
		       break;
	} while (--count);

	if (count == 0) {
		spin_unlock_irqrestore(&s->lock, flags);
		return -1;
	}

	/* write 0 to reset */
	outl((AC97_RESET << 16) | 0, s->io + VRC5477_CODEC_WR);

	/* test whether we get a response from ac97 chip */
	count  = 0xffff; 
	do { 
	       if (!(inl(s->io + VRC5477_CODEC_WR) & 0x80000000))
		       break;
	} while (--count);

	if (count == 0) {
		spin_unlock_irqrestore(&s->lock, flags);
		return -1;
	}
	spin_unlock_irqrestore(&s->lock, flags);
	return 0;
}

/* --------------------------------------------------------------------- */

static void vrc5477_ac97_delay(int msec)
{
	unsigned long tmo;
	signed long tmo2;

	if (in_interrupt())
		return;
    
	tmo = jiffies + (msec*HZ)/1000;
	for (;;) {
		tmo2 = tmo - jiffies;
		if (tmo2 <= 0)
			break;
		schedule_timeout(tmo2);
	}
}


static void set_adc_rate(struct vrc5477_ac97_state *s, unsigned rate)
{
	wrcodec(s->codec, AC97_PCM_LR_ADC_RATE, rate);
	s->adcRate = rate;
}


static void set_dac_rate(struct vrc5477_ac97_state *s, unsigned rate)
{
	if(s->extended_status & AC97_EXTSTAT_VRA) {
	wrcodec(s->codec, AC97_PCM_FRONT_DAC_RATE, rate);
		s->dacRate = rdcodec(s->codec, AC97_PCM_FRONT_DAC_RATE);
	}
}

static int ac97_codec_not_present(struct ac97_codec *codec)
{
	struct vrc5477_ac97_state *s =
		(struct vrc5477_ac97_state *)codec->private_data;
	unsigned long flags;
	unsigned short count  = 0xffff;

	spin_lock_irqsave(&s->lock, flags);

	/* wait until we can access codec registers */
	do {
	       if (!(inl(s->io + VRC5477_CODEC_WR) & 0x80000000))
		       break;
	} while (--count);

	if (count == 0) {
		spin_unlock_irqrestore(&s->lock, flags);
		return -1;
	}

	/* write 0 to reset */
	outl((AC97_RESET << 16) | 0, s->io + VRC5477_CODEC_WR);

	/* test whether we get a response from ac97 chip */
	count  = 0xffff;
	do {
	       if (!(inl(s->io + VRC5477_CODEC_WR) & 0x80000000))
		       break;
	} while (--count);

	if (count == 0) {
		spin_unlock_irqrestore(&s->lock, flags);
		return -1;
	}
	spin_unlock_irqrestore(&s->lock, flags);
	return 0;
}

/* --------------------------------------------------------------------- */

static inline void
stop_dac(struct vrc5477_ac97_state *s)
{
	struct dmabuf* db = &s->dma_dac;
	unsigned long flags;
	u32 temp;
    
	spin_lock_irqsave(&s->lock, flags);

	if (db->stopped) {
		spin_unlock_irqrestore(&s->lock, flags);
		return;
	}

	/* deactivate the dma */
	outl(0, s->io + VRC5477_DAC1_CTRL);
	outl(0, s->io + VRC5477_DAC2_CTRL);

	/* wait for DAM completely stop */
	while (inl(s->io + VRC5477_DAC1_CTRL) & VRC5477_DMA_WIP);
	while (inl(s->io + VRC5477_DAC2_CTRL) & VRC5477_DMA_WIP);

	/* disable dac slots in aclink */
	temp = inl(s->io + VRC5477_CTRL);
	temp &= ~ (VRC5477_CTRL_DAC1ENB | VRC5477_CTRL_DAC2ENB);
	outl (temp, s->io + VRC5477_CTRL);

	/* disable interrupts */
	temp = inl(s->io + VRC5477_INT_MASK);
	temp &= ~ (VRC5477_INT_MASK_DAC1END | VRC5477_INT_MASK_DAC2END); 
	outl (temp, s->io + VRC5477_INT_MASK);

	/* clear pending ones */
	outl(VRC5477_INT_MASK_DAC1END | VRC5477_INT_MASK_DAC2END, 
	     s->io +  VRC5477_INT_CLR);
    
	db->stopped = 1;
    
	spin_unlock_irqrestore(&s->lock, flags);
}	

static void start_dac(struct vrc5477_ac97_state *s)
{
	struct dmabuf* db = &s->dma_dac;
	unsigned long flags;
	u32 dmaLength;
	u32 temp;

	spin_lock_irqsave(&s->lock, flags);

	if (!db->stopped) {
		spin_unlock_irqrestore(&s->lock, flags);
		return;
	}

	/* we should have some data to do the DMA trasnfer */
	ASSERT(db->count >= db->fragSize);

	/* clear pending fales interrupts */
	outl(VRC5477_INT_MASK_DAC1END | VRC5477_INT_MASK_DAC2END, 
	     s->io +  VRC5477_INT_CLR);

	/* enable interrupts */
	temp = inl(s->io + VRC5477_INT_MASK);
	temp |= VRC5477_INT_MASK_DAC1END | VRC5477_INT_MASK_DAC2END;
	outl(temp, s->io +  VRC5477_INT_MASK);

	/* setup dma base addr */
	outl(db->lbufDma + db->nextOut, s->io + VRC5477_DAC1_BADDR);
	if (s->dacChannels == 1) {
		outl(db->lbufDma + db->nextOut, s->io + VRC5477_DAC2_BADDR);
	} else {
		outl(db->rbufDma + db->nextOut, s->io + VRC5477_DAC2_BADDR);
	}

	/* set dma length, in the unit of 0x10 bytes */
	dmaLength = db->fragSize >> 4;
	outl(dmaLength, s->io + VRC5477_DAC1L);
	outl(dmaLength, s->io + VRC5477_DAC2L);

	/* activate dma */
	outl(VRC5477_DMA_ACTIVATION, s->io + VRC5477_DAC1_CTRL);
	outl(VRC5477_DMA_ACTIVATION, s->io + VRC5477_DAC2_CTRL);

	/* enable dac slots - we should hear the music now! */
	temp = inl(s->io + VRC5477_CTRL);
	temp |= (VRC5477_CTRL_DAC1ENB | VRC5477_CTRL_DAC2ENB);
	outl (temp, s->io + VRC5477_CTRL);

	/* it is time to setup next dma transfer */
	ASSERT(inl(s->io + VRC5477_DAC1_CTRL) & VRC5477_DMA_WIP);
	ASSERT(inl(s->io + VRC5477_DAC2_CTRL) & VRC5477_DMA_WIP);

	temp = db->nextOut + db->fragSize;
	if (temp >= db->fragTotalSize) {
		ASSERT(temp == db->fragTotalSize);
		temp = 0;
	}

	outl(db->lbufDma + temp, s->io + VRC5477_DAC1_BADDR);
	if (s->dacChannels == 1) {
		outl(db->lbufDma + temp, s->io + VRC5477_DAC2_BADDR);
	} else {
		outl(db->rbufDma + temp, s->io + VRC5477_DAC2_BADDR);
	}

	db->stopped = 0;

#if defined(VRC5477_AC97_VERBOSE_DEBUG)
	outTicket = *(u16*)(db->lbuf+db->nextOut);
	if (db->count > db->fragSize) {
		ASSERT((u16)(outTicket+1) == *(u16*)(db->lbuf+temp));
	}
#endif

	spin_unlock_irqrestore(&s->lock, flags);
}	

static inline void stop_adc(struct vrc5477_ac97_state *s)
{
	struct dmabuf* db = &s->dma_adc;
	unsigned long flags;
	u32 temp;
    
	spin_lock_irqsave(&s->lock, flags);

	if (db->stopped) {
		spin_unlock_irqrestore(&s->lock, flags);
		return;
	}

	/* deactivate the dma */
	outl(0, s->io + VRC5477_ADC1_CTRL);
	outl(0, s->io + VRC5477_ADC2_CTRL);

	/* disable adc slots in aclink */
	temp = inl(s->io + VRC5477_CTRL);
	temp &= ~ (VRC5477_CTRL_ADC1ENB | VRC5477_CTRL_ADC2ENB);
	outl (temp, s->io + VRC5477_CTRL);

	/* disable interrupts */
        temp = inl(s->io + VRC5477_INT_MASK);
        temp &= ~ (VRC5477_INT_MASK_ADC1END | VRC5477_INT_MASK_ADC2END); 
        outl (temp, s->io + VRC5477_INT_MASK);

	/* clear pending ones */
	outl(VRC5477_INT_MASK_ADC1END | VRC5477_INT_MASK_ADC2END, 
	     s->io +  VRC5477_INT_CLR);
    
	db->stopped = 1;

	spin_unlock_irqrestore(&s->lock, flags);
}	

static void start_adc(struct vrc5477_ac97_state *s)
{
	struct dmabuf* db = &s->dma_adc;
	unsigned long flags;
	u32 dmaLength;
	u32 temp;

	spin_lock_irqsave(&s->lock, flags);

	if (!db->stopped) {
		spin_unlock_irqrestore(&s->lock, flags);
		return;
	}

	/* we should at least have some free space in the buffer */
	ASSERT(db->count < db->fragTotalSize - db->fragSize * 2);

	/* clear pending ones */
	outl(VRC5477_INT_MASK_ADC1END | VRC5477_INT_MASK_ADC2END, 
	     s->io +  VRC5477_INT_CLR);

        /* enable interrupts */
        temp = inl(s->io + VRC5477_INT_MASK);
        temp |= VRC5477_INT_MASK_ADC1END | VRC5477_INT_MASK_ADC2END;
        outl(temp, s->io +  VRC5477_INT_MASK);

	/* setup dma base addr */
	outl(db->lbufDma + db->nextIn, s->io + VRC5477_ADC1_BADDR);
	outl(db->rbufDma + db->nextIn, s->io + VRC5477_ADC2_BADDR);

	/* setup dma length */
	dmaLength = db->fragSize >> 4;
	outl(dmaLength, s->io + VRC5477_ADC1L);
	outl(dmaLength, s->io + VRC5477_ADC2L);

	/* activate dma */
	outl(VRC5477_DMA_ACTIVATION, s->io + VRC5477_ADC1_CTRL);
	outl(VRC5477_DMA_ACTIVATION, s->io + VRC5477_ADC2_CTRL);

	/* enable adc slots */
	temp = inl(s->io + VRC5477_CTRL);
	temp |= (VRC5477_CTRL_ADC1ENB | VRC5477_CTRL_ADC2ENB);
	outl (temp, s->io + VRC5477_CTRL);

	/* it is time to setup next dma transfer */
	temp = db->nextIn + db->fragSize;
	if (temp >= db->fragTotalSize) {
		ASSERT(temp == db->fragTotalSize);
		temp = 0;
	}
	outl(db->lbufDma + temp, s->io + VRC5477_ADC1_BADDR);
	outl(db->rbufDma + temp, s->io + VRC5477_ADC2_BADDR);

	db->stopped = 0;

	spin_unlock_irqrestore(&s->lock, flags);
}	

/* --------------------------------------------------------------------- */

#define DMABUF_DEFAULTORDER (16-PAGE_SHIFT)
#define DMABUF_MINORDER 1

static inline void dealloc_dmabuf(struct vrc5477_ac97_state *s,
				  struct dmabuf *db)
{
	if (db->lbuf) {
		ASSERT(db->rbuf);
		pci_free_consistent(s->dev, PAGE_SIZE << db->bufOrder,
				    db->lbuf, db->lbufDma);
		pci_free_consistent(s->dev, PAGE_SIZE << db->bufOrder,
				    db->rbuf, db->rbufDma);
		db->lbuf = db->rbuf = NULL;
	}
	db->nextIn = db->nextOut = 0;
	db->ready = 0;
}

static int prog_dmabuf(struct vrc5477_ac97_state *s, 
		       struct dmabuf *db,
		       unsigned rate)
{
	int order;
	unsigned bufsize;

	if (!db->lbuf) {
		ASSERT(!db->rbuf);

		db->ready = 0;
		for (order = DMABUF_DEFAULTORDER; 
		     order >= DMABUF_MINORDER; 
		     order--) {
			db->lbuf = pci_alloc_consistent(s->dev,
							PAGE_SIZE << order,
							&db->lbufDma);
			db->rbuf = pci_alloc_consistent(s->dev,
							PAGE_SIZE << order,
							&db->rbufDma);
			if (db->lbuf && db->rbuf) break;
			if (db->lbuf) {
			    ASSERT(!db->rbuf);
			    pci_free_consistent(s->dev, 
						PAGE_SIZE << order,
						db->lbuf,
						db->lbufDma);
			}
		}
		if (!db->lbuf) {
			ASSERT(!db->rbuf);
			return -ENOMEM;
		}

		db->bufOrder = order;
	}

	db->count = 0;
	db->nextIn = db->nextOut = 0;
    
	bufsize = PAGE_SIZE << db->bufOrder;
	db->fragShift = ld2(rate * 2 / 100);
	if (db->fragShift < 4) db->fragShift = 4;

	db->numFrag = bufsize >> db->fragShift;
	while (db->numFrag < 4 && db->fragShift > 4) {
		db->fragShift--;
		db->numFrag = bufsize >> db->fragShift;
	}
	db->fragSize = 1 << db->fragShift;
	db->fragTotalSize = db->numFrag << db->fragShift;
	memset(db->lbuf, 0, db->fragTotalSize);
	memset(db->rbuf, 0, db->fragTotalSize);
    
	db->ready = 1;

	return 0;
}

static inline int prog_dmabuf_adc(struct vrc5477_ac97_state *s)
{
    stop_adc(s);
    return prog_dmabuf(s, &s->dma_adc, s->adcRate);
}

static inline int prog_dmabuf_dac(struct vrc5477_ac97_state *s)
{
    stop_dac(s);
    return prog_dmabuf(s, &s->dma_dac, s->dacRate);
}


/* --------------------------------------------------------------------- */
/* hold spinlock for the following! */

static inline void vrc5477_ac97_adc_interrupt(struct vrc5477_ac97_state *s)
{
	struct dmabuf* adc = &s->dma_adc;
	unsigned temp;

	/* we need two frags avaiable because one is already being used
	 * and the other will be used when next interrupt happens.
	 */
	if (adc->count >= adc->fragTotalSize - adc->fragSize) {
		stop_adc(s);
		adc->error++;
		printk(KERN_INFO PFX "adc overrun\n");
		return;
	}

	/* set the base addr for next DMA transfer */
	temp = adc->nextIn + 2*adc->fragSize;
	if (temp >= adc->fragTotalSize) {
		ASSERT( (temp == adc->fragTotalSize) ||
                             (temp == adc->fragTotalSize + adc->fragSize) );
		temp -= adc->fragTotalSize;
	}
	outl(adc->lbufDma + temp, s->io + VRC5477_ADC1_BADDR);
	outl(adc->rbufDma + temp, s->io + VRC5477_ADC2_BADDR);

	/* adjust nextIn */
	adc->nextIn += adc->fragSize;
	if (adc->nextIn >= adc->fragTotalSize) {
		ASSERT(adc->nextIn == adc->fragTotalSize);
		adc->nextIn = 0;
	}

	/* adjust count */
	adc->count += adc->fragSize;

	/* wake up anybody listening */
	if (waitqueue_active(&adc->wait)) {
		wake_up_interruptible(&adc->wait);
	}	
}

static inline void vrc5477_ac97_dac_interrupt(struct vrc5477_ac97_state *s)
{
	struct dmabuf* dac = &s->dma_dac;
	unsigned temp;

	/* next DMA transfer should already started */
	// ASSERT(inl(s->io + VRC5477_DAC1_CTRL) & VRC5477_DMA_WIP);
	// ASSERT(inl(s->io + VRC5477_DAC2_CTRL) & VRC5477_DMA_WIP);

	/* let us set for next next DMA transfer */
	temp = dac->nextOut + dac->fragSize*2;
	if (temp >= dac->fragTotalSize) {
		ASSERT( (temp == dac->fragTotalSize) || 
                             (temp == dac->fragTotalSize + dac->fragSize) );
		temp -= dac->fragTotalSize;
	}
	outl(dac->lbufDma + temp, s->io + VRC5477_DAC1_BADDR);
	if (s->dacChannels == 1) {
		outl(dac->lbufDma + temp, s->io + VRC5477_DAC2_BADDR);
	} else {
		outl(dac->rbufDma + temp, s->io + VRC5477_DAC2_BADDR);
	}

#if defined(VRC5477_AC97_VERBOSE_DEBUG)
	if (*(u16*)(dac->lbuf +  dac->nextOut) != outTicket) {
		printk("assert fail: - %d vs %d\n", 
		        *(u16*)(dac->lbuf +  dac->nextOut),
                        outTicket);
                ASSERT(1 == 0);
	}
#endif

	/* adjust nextOut pointer */
	dac->nextOut += dac->fragSize;
	if (dac->nextOut >= dac->fragTotalSize) {
		ASSERT(dac->nextOut == dac->fragTotalSize);
		dac->nextOut = 0;
	}

	/* adjust count */
	dac->count -= dac->fragSize;
	if (dac->count <=0 ) {
		/* buffer under run */
		dac->count = 0;
		dac->nextIn = dac->nextOut;
		stop_dac(s);
	}

#if defined(VRC5477_AC97_VERBOSE_DEBUG)
	if (dac->count) {
		outTicket ++;
		ASSERT(*(u16*)(dac->lbuf +  dac->nextOut) == outTicket);
	}
#endif
	
	/* we cannot have both under run and someone is waiting on us */
	ASSERT(! (waitqueue_active(&dac->wait) && (dac->count <= 0)) );

	/* wake up anybody listening */
	if (waitqueue_active(&dac->wait))
		wake_up_interruptible(&dac->wait);
}

static irqreturn_t vrc5477_ac97_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct vrc5477_ac97_state *s = (struct vrc5477_ac97_state *)dev_id;
	u32 irqStatus;
	u32 adcInterrupts, dacInterrupts;

	spin_lock(&s->lock);

	/* get irqStatus and clear the detected ones */
	irqStatus = inl(s->io + VRC5477_INT_STATUS);
	outl(irqStatus, s->io + VRC5477_INT_CLR);

	/* let us see what we get */
	dacInterrupts = VRC5477_INT_MASK_DAC1END | VRC5477_INT_MASK_DAC2END;
	adcInterrupts = VRC5477_INT_MASK_ADC1END | VRC5477_INT_MASK_ADC2END;
	if (irqStatus & dacInterrupts) {
		/* we should get both interrupts, but just in case ...  */
		if (irqStatus & VRC5477_INT_MASK_DAC1END) {
			vrc5477_ac97_dac_interrupt(s);
		}
		if ( (irqStatus & dacInterrupts) != dacInterrupts ) {
			printk(KERN_WARNING "vrc5477_ac97 : dac interrupts not in sync!!!\n");
			stop_dac(s);
			start_dac(s);
		}
	} else if (irqStatus & adcInterrupts) {
		/* we should get both interrupts, but just in case ...  */
		if(irqStatus & VRC5477_INT_MASK_ADC1END) {
			vrc5477_ac97_adc_interrupt(s);
		} 
		if ( (irqStatus & adcInterrupts) != adcInterrupts ) {
			printk(KERN_WARNING "vrc5477_ac97 : adc interrupts not in sync!!!\n");
			stop_adc(s);
			start_adc(s);
		}
	}

	spin_unlock(&s->lock);
	return IRQ_HANDLED;
}

/* --------------------------------------------------------------------- */

static int vrc5477_ac97_open_mixdev(struct inode *inode, struct file *file)
{
	int minor = iminor(inode);
	struct list_head *list;
	struct vrc5477_ac97_state *s;

	for (list = devs.next; ; list = list->next) {
		if (list == &devs)
			return -ENODEV;
		s = list_entry(list, struct vrc5477_ac97_state, devs);
		if (s->codec->dev_mixer == minor)
			break;
	}
	file->private_data = s;
	return nonseekable_open(inode, file);
}

static int vrc5477_ac97_release_mixdev(struct inode *inode, struct file *file)
{
	return 0;
}


static int mixdev_ioctl(struct ac97_codec *codec, unsigned int cmd,
			unsigned long arg)
{
	return codec->mixer_ioctl(codec, cmd, arg);
}

static int vrc5477_ac97_ioctl_mixdev(struct inode *inode, struct file *file,
				     unsigned int cmd, unsigned long arg)
{
    struct vrc5477_ac97_state *s = 
	    (struct vrc5477_ac97_state *)file->private_data;
    struct ac97_codec *codec = s->codec;

    return mixdev_ioctl(codec, cmd, arg);
}

static /*const*/ struct file_operations vrc5477_ac97_mixer_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.ioctl		= vrc5477_ac97_ioctl_mixdev,
	.open		= vrc5477_ac97_open_mixdev,
	.release	= vrc5477_ac97_release_mixdev,
};

/* --------------------------------------------------------------------- */

static int drain_dac(struct vrc5477_ac97_state *s, int nonblock)
{
	unsigned long flags;
	int count, tmo;
	
	if (!s->dma_dac.ready)
		return 0;

	for (;;) {
		spin_lock_irqsave(&s->lock, flags);
		count = s->dma_dac.count;
		spin_unlock_irqrestore(&s->lock, flags);
		if (count <= 0)
			break;
		if (signal_pending(current))
			break;
		if (nonblock)
			return -EBUSY;
		tmo = 1000 * count / s->dacRate / 2;
		vrc5477_ac97_delay(tmo);
	}
	if (signal_pending(current))
		return -ERESTARTSYS;
	return 0;
}

/* --------------------------------------------------------------------- */

static inline int
copy_two_channel_adc_to_user(struct vrc5477_ac97_state *s, 
		             char *buffer, 
			     int copyCount)
{
	struct dmabuf *db = &s->dma_adc;
	int bufStart = db->nextOut;
	for (; copyCount > 0; ) {
		int i;
		int count = copyCount;
		if (count > WORK_BUF_SIZE/2) count = WORK_BUF_SIZE/2;
		for (i=0; i< count/2; i++) {
			s->workBuf[i].lchannel = 
				*(u16*)(db->lbuf + bufStart + i*2);
			s->workBuf[i].rchannel = 
				*(u16*)(db->rbuf + bufStart + i*2);
		}
		if (copy_to_user(buffer, s->workBuf, count*2)) {
			return -1;
		}

		copyCount -= count;
		bufStart += count;
		ASSERT(bufStart <= db->fragTotalSize);
		buffer += count *2;
	}
	return 0;
}

/* return the total bytes that is copied */
static inline int
copy_adc_to_user(struct vrc5477_ac97_state *s,
		 char * buffer,
		 size_t count,
		 int avail)
{
	struct dmabuf *db = &s->dma_adc;
	int copyCount=0;
	int copyFragCount=0;
	int totalCopyCount = 0;
	int totalCopyFragCount = 0;
	unsigned long flags;

	/* adjust count to signel channel byte count */
	count >>= s->adcChannels - 1;

	/* we may have to "copy" twice as ring buffer wraps around */
	for (; (avail > 0) && (count > 0); ) {
		/* determine max possible copy count for single channel */
		copyCount = count;
		if (copyCount > avail) {
			copyCount = avail;
		}
		if (copyCount + db->nextOut > db->fragTotalSize) {
			copyCount = db->fragTotalSize - db->nextOut;
			ASSERT((copyCount % db->fragSize) == 0);
		}

		copyFragCount = (copyCount-1) >> db->fragShift;
		copyFragCount = (copyFragCount+1) << db->fragShift;
		ASSERT(copyFragCount >= copyCount);

		/* we copy differently based on adc channels */
		if (s->adcChannels == 1) {
			if (copy_to_user(buffer, 
					 db->lbuf + db->nextOut, 
					 copyCount)) 
				return -1;
		} else {
			/* *sigh* we have to mix two streams into one  */
			if (copy_two_channel_adc_to_user(s, buffer, copyCount))
				return -1;
		}	

		count -= copyCount;
		totalCopyCount += copyCount;
		avail -= copyFragCount;
		totalCopyFragCount += copyFragCount;

		buffer += copyCount << (s->adcChannels-1);

		db->nextOut += copyFragCount;
		if (db->nextOut >= db->fragTotalSize) {
			ASSERT(db->nextOut == db->fragTotalSize);
			db->nextOut = 0;
		}

		ASSERT((copyFragCount % db->fragSize) == 0);
		ASSERT( (count == 0) || (copyCount == copyFragCount));
	}

	spin_lock_irqsave(&s->lock, flags);
        db->count -= totalCopyFragCount;
        spin_unlock_irqrestore(&s->lock, flags);

	return totalCopyCount << (s->adcChannels-1);
}

static ssize_t 
vrc5477_ac97_read(struct file *file, 
		  char *buffer,
		  size_t count, 
		  loff_t *ppos)
{
	struct vrc5477_ac97_state *s = 
		(struct vrc5477_ac97_state *)file->private_data;
	struct dmabuf *db = &s->dma_adc;
	ssize_t ret = 0;
	unsigned long flags;
	int copyCount;
	size_t avail;

	if (!access_ok(VERIFY_WRITE, buffer, count))
		return -EFAULT;

	ASSERT(db->ready);

	while (count > 0) {
		// wait for samples in capture buffer
		do {
			spin_lock_irqsave(&s->lock, flags);
			if (db->stopped)
				start_adc(s);
			avail = db->count;
			spin_unlock_irqrestore(&s->lock, flags);
			if (avail <= 0) {
				if (file->f_flags & O_NONBLOCK) {
					if (!ret)
						ret = -EAGAIN;
					return ret;
				}
				interruptible_sleep_on(&db->wait);
				if (signal_pending(current)) {
					if (!ret)
						ret = -ERESTARTSYS;
					return ret;
				}
			}
		} while (avail <= 0);

		ASSERT( (avail % db->fragSize) == 0);
		copyCount = copy_adc_to_user(s, buffer, count, avail);
		if (copyCount <=0 ) {
			if (!ret) ret = -EFAULT;
			return ret;
		}

		count -= copyCount;
		buffer += copyCount;
		ret += copyCount;
	} // while (count > 0)

	return ret;
}

static inline int
copy_two_channel_dac_from_user(struct vrc5477_ac97_state *s, 
			       const char *buffer, 
			       int copyCount)
{
	struct dmabuf *db = &s->dma_dac;
	int bufStart = db->nextIn;

	ASSERT(db->ready);

        for (; copyCount > 0; ) {
                int i;
                int count = copyCount;
                if (count > WORK_BUF_SIZE/2) count = WORK_BUF_SIZE/2;
                if (copy_from_user(s->workBuf, buffer, count*2)) {
                        return -1;
                }
                for (i=0; i< count/2; i++) {
			*(u16*)(db->lbuf + bufStart + i*2) = 
				s->workBuf[i].lchannel;
			*(u16*)(db->rbuf + bufStart + i*2) = 
				s->workBuf[i].rchannel;
                }

                copyCount -= count;
		bufStart += count;
		ASSERT(bufStart <= db->fragTotalSize);
                buffer += count *2;
        }
        return 0;

}

/* return the total bytes that is copied */
static inline int
copy_dac_from_user(struct vrc5477_ac97_state *s, 
		   const char *buffer, 
		   size_t count, 
		   int avail)
{	
        struct dmabuf *db = &s->dma_dac;
        int copyCount=0;
        int copyFragCount=0;
        int totalCopyCount = 0;
        int totalCopyFragCount = 0;
        unsigned long flags;
#if defined(VRC5477_AC97_VERBOSE_DEBUG)
	int i;
#endif

        /* adjust count to signel channel byte count */
        count >>= s->dacChannels - 1;

        /* we may have to "copy" twice as ring buffer wraps around */
        for (; (avail > 0) && (count > 0); ) {
                /* determine max possible copy count for single channel */
                copyCount = count;
                if (copyCount > avail) {
                        copyCount = avail;
		}
                if (copyCount + db->nextIn > db->fragTotalSize) {
                        copyCount = db->fragTotalSize - db->nextIn;
                        ASSERT(copyCount > 0);
                }

		copyFragCount = copyCount;
		ASSERT(copyFragCount >= copyCount);

		/* we copy differently based on the number channels */
		if (s->dacChannels == 1) {
			if (copy_from_user(db->lbuf + db->nextIn,
					   buffer,
					   copyCount)) 
				return -1;
			/* fill gaps with 0 */
			memset(db->lbuf + db->nextIn + copyCount,
			       0,
			       copyFragCount - copyCount);
		} else {
			/* we have demux the stream into two separate ones */
			if (copy_two_channel_dac_from_user(s, buffer, copyCount))
				return -1;
			/* fill gaps with 0 */
			memset(db->lbuf + db->nextIn + copyCount,
			       0,
			       copyFragCount - copyCount);
			memset(db->rbuf + db->nextIn + copyCount,
			       0,
			       copyFragCount - copyCount);
		}

#if defined(VRC5477_AC97_VERBOSE_DEBUG)
		for (i=0; i< copyFragCount; i+= db->fragSize) {
			*(u16*)(db->lbuf + db->nextIn + i) = inTicket ++;
		}
#endif

		count -= copyCount;
		totalCopyCount += copyCount;
		avail -= copyFragCount;
		totalCopyFragCount += copyFragCount;

		buffer += copyCount << (s->dacChannels - 1);

		db->nextIn += copyFragCount;
		if (db->nextIn >= db->fragTotalSize) {
			ASSERT(db->nextIn == db->fragTotalSize);
			db->nextIn = 0;
		}

		ASSERT( (count == 0) || (copyCount == copyFragCount));
	}

	spin_lock_irqsave(&s->lock, flags);
        db->count += totalCopyFragCount;
	if (db->stopped) {
		start_dac(s);
	}

	/* nextIn should not be equal to nextOut unless we are full */
	ASSERT( ( (db->count == db->fragTotalSize) && 
                       (db->nextIn == db->nextOut) ) ||
                     ( (db->count < db->fragTotalSize) &&
                       (db->nextIn != db->nextOut) ) );

        spin_unlock_irqrestore(&s->lock, flags);

        return totalCopyCount << (s->dacChannels-1);

}

static ssize_t vrc5477_ac97_write(struct file *file, const char *buffer,
				  size_t count, loff_t *ppos)
{
	struct vrc5477_ac97_state *s = 
		(struct vrc5477_ac97_state *)file->private_data;
	struct dmabuf *db = &s->dma_dac;
	ssize_t ret;
	unsigned long flags;
	int copyCount, avail;

	if (!access_ok(VERIFY_READ, buffer, count))
		return -EFAULT;
	ret = 0;
    
	while (count > 0) {
		// wait for space in playback buffer
		do {
			spin_lock_irqsave(&s->lock, flags);
			avail = db->fragTotalSize - db->count;
			spin_unlock_irqrestore(&s->lock, flags);
			if (avail <= 0) {
				if (file->f_flags & O_NONBLOCK) {
					if (!ret)
						ret = -EAGAIN;
					return ret;
				}
				interruptible_sleep_on(&db->wait);
				if (signal_pending(current)) {
					if (!ret)
						ret = -ERESTARTSYS;
					return ret;
				}
			}
		} while (avail <= 0);
	
		copyCount = copy_dac_from_user(s, buffer, count, avail);
		if (copyCount < 0) {
			if (!ret) ret = -EFAULT;
			return ret;
		}

		count -= copyCount;
		buffer += copyCount;
		ret += copyCount;
	} // while (count > 0)
	
	return ret;
}

/* No kernel lock - we have our own spinlock */
static unsigned int vrc5477_ac97_poll(struct file *file,
				      struct poll_table_struct *wait)
{
	struct vrc5477_ac97_state *s = (struct vrc5477_ac97_state *)file->private_data;
	unsigned long flags;
	unsigned int mask = 0;

	if (file->f_mode & FMODE_WRITE)
		poll_wait(file, &s->dma_dac.wait, wait);
	if (file->f_mode & FMODE_READ)
		poll_wait(file, &s->dma_adc.wait, wait);
	spin_lock_irqsave(&s->lock, flags);
	if (file->f_mode & FMODE_READ) {
		if (s->dma_adc.count >= (signed)s->dma_adc.fragSize)
			mask |= POLLIN | POLLRDNORM;
	}
	if (file->f_mode & FMODE_WRITE) {
		if ((signed)s->dma_dac.fragTotalSize >=
		    s->dma_dac.count + (signed)s->dma_dac.fragSize)
			mask |= POLLOUT | POLLWRNORM;
	}
	spin_unlock_irqrestore(&s->lock, flags);
	return mask;
}

#ifdef VRC5477_AC97_DEBUG
static struct ioctl_str_t {
    unsigned int cmd;
    const char* str;
} ioctl_str[] = {
    {SNDCTL_DSP_RESET, "SNDCTL_DSP_RESET"},
    {SNDCTL_DSP_SYNC, "SNDCTL_DSP_SYNC"},
    {SNDCTL_DSP_SPEED, "SNDCTL_DSP_SPEED"},
    {SNDCTL_DSP_STEREO, "SNDCTL_DSP_STEREO"},
    {SNDCTL_DSP_GETBLKSIZE, "SNDCTL_DSP_GETBLKSIZE"},
    {SNDCTL_DSP_SETFMT, "SNDCTL_DSP_SETFMT"},
    {SNDCTL_DSP_SAMPLESIZE, "SNDCTL_DSP_SAMPLESIZE"},
    {SNDCTL_DSP_CHANNELS, "SNDCTL_DSP_CHANNELS"},
    {SOUND_PCM_WRITE_CHANNELS, "SOUND_PCM_WRITE_CHANNELS"},
    {SOUND_PCM_WRITE_FILTER, "SOUND_PCM_WRITE_FILTER"},
    {SNDCTL_DSP_POST, "SNDCTL_DSP_POST"},
    {SNDCTL_DSP_SUBDIVIDE, "SNDCTL_DSP_SUBDIVIDE"},
    {SNDCTL_DSP_SETFRAGMENT, "SNDCTL_DSP_SETFRAGMENT"},
    {SNDCTL_DSP_GETFMTS, "SNDCTL_DSP_GETFMTS"},
    {SNDCTL_DSP_GETOSPACE, "SNDCTL_DSP_GETOSPACE"},
    {SNDCTL_DSP_GETISPACE, "SNDCTL_DSP_GETISPACE"},
    {SNDCTL_DSP_NONBLOCK, "SNDCTL_DSP_NONBLOCK"},
    {SNDCTL_DSP_GETCAPS, "SNDCTL_DSP_GETCAPS"},
    {SNDCTL_DSP_GETTRIGGER, "SNDCTL_DSP_GETTRIGGER"},
    {SNDCTL_DSP_SETTRIGGER, "SNDCTL_DSP_SETTRIGGER"},
    {SNDCTL_DSP_GETIPTR, "SNDCTL_DSP_GETIPTR"},
    {SNDCTL_DSP_GETOPTR, "SNDCTL_DSP_GETOPTR"},
    {SNDCTL_DSP_MAPINBUF, "SNDCTL_DSP_MAPINBUF"},
    {SNDCTL_DSP_MAPOUTBUF, "SNDCTL_DSP_MAPOUTBUF"},
    {SNDCTL_DSP_SETSYNCRO, "SNDCTL_DSP_SETSYNCRO"},
    {SNDCTL_DSP_SETDUPLEX, "SNDCTL_DSP_SETDUPLEX"},
    {SNDCTL_DSP_GETODELAY, "SNDCTL_DSP_GETODELAY"},
    {SNDCTL_DSP_GETCHANNELMASK, "SNDCTL_DSP_GETCHANNELMASK"},
    {SNDCTL_DSP_BIND_CHANNEL, "SNDCTL_DSP_BIND_CHANNEL"},
    {OSS_GETVERSION, "OSS_GETVERSION"},
    {SOUND_PCM_READ_RATE, "SOUND_PCM_READ_RATE"},
    {SOUND_PCM_READ_CHANNELS, "SOUND_PCM_READ_CHANNELS"},
    {SOUND_PCM_READ_BITS, "SOUND_PCM_READ_BITS"},
    {SOUND_PCM_READ_FILTER, "SOUND_PCM_READ_FILTER"}
};
#endif    

static int vrc5477_ac97_ioctl(struct inode *inode, struct file *file,
			unsigned int cmd, unsigned long arg)
{
	struct vrc5477_ac97_state *s = (struct vrc5477_ac97_state *)file->private_data;
	unsigned long flags;
	audio_buf_info abinfo;
	int count;
	int val, ret;

#ifdef VRC5477_AC97_DEBUG
	for (count=0; count<sizeof(ioctl_str)/sizeof(ioctl_str[0]); count++) {
		if (ioctl_str[count].cmd == cmd)
			break;
	}
	if (count < sizeof(ioctl_str)/sizeof(ioctl_str[0]))
		printk(KERN_INFO PFX "ioctl %s\n", ioctl_str[count].str);
	else
		printk(KERN_INFO PFX "ioctl unknown, 0x%x\n", cmd);
#endif
    
	switch (cmd) {
	case OSS_GETVERSION:
		return put_user(SOUND_VERSION, (int *)arg);

	case SNDCTL_DSP_SYNC:
		if (file->f_mode & FMODE_WRITE)
			return drain_dac(s, file->f_flags & O_NONBLOCK);
		return 0;
		
	case SNDCTL_DSP_SETDUPLEX:
		return 0;

	case SNDCTL_DSP_GETCAPS:
		return put_user(DSP_CAP_DUPLEX, (int *)arg);
		
	case SNDCTL_DSP_RESET:
		if (file->f_mode & FMODE_WRITE) {
			stop_dac(s);
			synchronize_irq(s->irq);
			s->dma_dac.count = 0;
			s->dma_dac.nextIn = s->dma_dac.nextOut = 0;
		}
		if (file->f_mode & FMODE_READ) {
			stop_adc(s);
			synchronize_irq(s->irq);
			s->dma_adc.count = 0;
			s->dma_adc.nextIn = s->dma_adc.nextOut = 0;
		}
		return 0;

	case SNDCTL_DSP_SPEED:
		if (get_user(val, (int *)arg))
			return -EFAULT;
		if (val >= 0) {
			if (file->f_mode & FMODE_READ) {
				stop_adc(s);
				set_adc_rate(s, val);
				if ((ret = prog_dmabuf_adc(s)))
					return ret;
			}
			if (file->f_mode & FMODE_WRITE) {
				stop_dac(s);
				set_dac_rate(s, val);
				if ((ret = prog_dmabuf_dac(s)))
					return ret;
			}
		}
		return put_user((file->f_mode & FMODE_READ) ?
				s->adcRate : s->dacRate, (int *)arg);

	case SNDCTL_DSP_STEREO:
		if (get_user(val, (int *)arg))
			return -EFAULT;
		if (file->f_mode & FMODE_READ) {
			stop_adc(s);
			if (val)
				s->adcChannels = 2;
			else
				s->adcChannels = 1;
			if ((ret = prog_dmabuf_adc(s)))
				return ret;
		}
		if (file->f_mode & FMODE_WRITE) {
			stop_dac(s);
			if (val)
				s->dacChannels = 2;
			else
				s->dacChannels = 1;
			if ((ret = prog_dmabuf_dac(s)))
				return ret;
		}
		return 0;

	case SNDCTL_DSP_CHANNELS:
		if (get_user(val, (int *)arg))
			return -EFAULT;
		if (val != 0) {
			if ( (val != 1) && (val != 2)) val = 2;

			if (file->f_mode & FMODE_READ) {
				stop_adc(s);
				s->dacChannels = val;
				if ((ret = prog_dmabuf_adc(s)))
					return ret;
			}
			if (file->f_mode & FMODE_WRITE) {
				stop_dac(s);
				s->dacChannels = val;
				if ((ret = prog_dmabuf_dac(s)))
					return ret;
			}
		}
		return put_user(val, (int *)arg);
		
	case SNDCTL_DSP_GETFMTS: /* Returns a mask */
		return put_user(AFMT_S16_LE, (int *)arg);
		
	case SNDCTL_DSP_SETFMT: /* Selects ONE fmt*/
		if (get_user(val, (int *)arg))
			return -EFAULT;
		if (val != AFMT_QUERY) {
			if (val != AFMT_S16_LE) return -EINVAL;
			if (file->f_mode & FMODE_READ) {
				stop_adc(s);
				if ((ret = prog_dmabuf_adc(s)))
					return ret;
			}
			if (file->f_mode & FMODE_WRITE) {
				stop_dac(s);
				if ((ret = prog_dmabuf_dac(s)))
					return ret;
			}
		} else {
			val = AFMT_S16_LE;
		}
		return put_user(val, (int *)arg);
		
	case SNDCTL_DSP_POST:
		return 0;

	case SNDCTL_DSP_GETTRIGGER:
	case SNDCTL_DSP_SETTRIGGER:
		/* NO trigger */
		return -EINVAL;

	case SNDCTL_DSP_GETOSPACE:
		if (!(file->f_mode & FMODE_WRITE))
			return -EINVAL;
		abinfo.fragsize = s->dma_dac.fragSize << (s->dacChannels-1);
		spin_lock_irqsave(&s->lock, flags);
		count = s->dma_dac.count;
		spin_unlock_irqrestore(&s->lock, flags);
		abinfo.bytes = (s->dma_dac.fragTotalSize - count) << 
			(s->dacChannels-1);
		abinfo.fragstotal = s->dma_dac.numFrag;
		abinfo.fragments = abinfo.bytes >> s->dma_dac.fragShift >> 
			(s->dacChannels-1);      
		return copy_to_user((void *)arg, &abinfo, sizeof(abinfo)) ? -EFAULT : 0;

	case SNDCTL_DSP_GETISPACE:
		if (!(file->f_mode & FMODE_READ))
			return -EINVAL;
		abinfo.fragsize = s->dma_adc.fragSize << (s->adcChannels-1);
		spin_lock_irqsave(&s->lock, flags);
		count = s->dma_adc.count;
		spin_unlock_irqrestore(&s->lock, flags);
		if (count < 0)
			count = 0;
		abinfo.bytes = count << (s->adcChannels-1);
		abinfo.fragstotal = s->dma_adc.numFrag;
		abinfo.fragments = (abinfo.bytes >> s->dma_adc.fragShift) >>
			(s->adcChannels-1);      
		return copy_to_user((void *)arg, &abinfo, sizeof(abinfo)) ? -EFAULT : 0;
		
	case SNDCTL_DSP_NONBLOCK:
		file->f_flags |= O_NONBLOCK;
		return 0;

	case SNDCTL_DSP_GETODELAY:
		if (!(file->f_mode & FMODE_WRITE))
			return -EINVAL;
		spin_lock_irqsave(&s->lock, flags);
		count = s->dma_dac.count;
		spin_unlock_irqrestore(&s->lock, flags);
		return put_user(count, (int *)arg);

	case SNDCTL_DSP_GETIPTR:
	case SNDCTL_DSP_GETOPTR:
		/* we cannot get DMA ptr */
		return -EINVAL;

	case SNDCTL_DSP_GETBLKSIZE:
		if (file->f_mode & FMODE_WRITE)
			return put_user(s->dma_dac.fragSize << (s->dacChannels-1), (int *)arg);
		else
			return put_user(s->dma_adc.fragSize << (s->adcChannels-1), (int *)arg);

	case SNDCTL_DSP_SETFRAGMENT:
		/* we ignore fragment size request */
		return 0;

	case SNDCTL_DSP_SUBDIVIDE:
		/* what is this for? [jsun] */
		return 0;

	case SOUND_PCM_READ_RATE:
		return put_user((file->f_mode & FMODE_READ) ?
				s->adcRate : s->dacRate, (int *)arg);

	case SOUND_PCM_READ_CHANNELS:
		if (file->f_mode & FMODE_READ)
			return put_user(s->adcChannels, (int *)arg);
		else
			return put_user(s->dacChannels ? 2 : 1, (int *)arg);
	    
	case SOUND_PCM_READ_BITS:
		return put_user(16, (int *)arg);

	case SOUND_PCM_WRITE_FILTER:
	case SNDCTL_DSP_SETSYNCRO:
	case SOUND_PCM_READ_FILTER:
		return -EINVAL;
	}

	return mixdev_ioctl(s->codec, cmd, arg);
}


static int vrc5477_ac97_open(struct inode *inode, struct file *file)
{
	int minor = iminor(inode);
	DECLARE_WAITQUEUE(wait, current);
	unsigned long flags;
	struct list_head *list;
	struct vrc5477_ac97_state *s;
	int ret=0;

	nonseekable_open(inode, file);    
	for (list = devs.next; ; list = list->next) {
		if (list == &devs)
			return -ENODEV;
		s = list_entry(list, struct vrc5477_ac97_state, devs);
		if (!((s->dev_audio ^ minor) & ~0xf))
			break;
	}
	file->private_data = s;

	/* wait for device to become free */
	down(&s->open_sem);
	while (s->open_mode & file->f_mode) {

		if (file->f_flags & O_NONBLOCK) {
			up(&s->open_sem);
			return -EBUSY;
		}
		add_wait_queue(&s->open_wait, &wait);
		__set_current_state(TASK_INTERRUPTIBLE);
		up(&s->open_sem);
		schedule();
		remove_wait_queue(&s->open_wait, &wait);
		set_current_state(TASK_RUNNING);
		if (signal_pending(current))
			return -ERESTARTSYS;
		down(&s->open_sem);
	}

	spin_lock_irqsave(&s->lock, flags);

	if (file->f_mode & FMODE_READ) {
		/* set default settings */
		set_adc_rate(s, 48000);
		s->adcChannels = 2;

		ret = prog_dmabuf_adc(s);
		if (ret) goto bailout;
	}
	if (file->f_mode & FMODE_WRITE) {
		/* set default settings */
		set_dac_rate(s, 48000);
		s->dacChannels = 2;

		ret = prog_dmabuf_dac(s);
		if (ret) goto bailout;
	}

	s->open_mode |= file->f_mode & (FMODE_READ | FMODE_WRITE);

 bailout:
	spin_unlock_irqrestore(&s->lock, flags);

	up(&s->open_sem);
	return ret;
}

static int vrc5477_ac97_release(struct inode *inode, struct file *file)
{
	struct vrc5477_ac97_state *s = 
		(struct vrc5477_ac97_state *)file->private_data;

	lock_kernel();
	if (file->f_mode & FMODE_WRITE)
		drain_dac(s, file->f_flags & O_NONBLOCK);
	down(&s->open_sem);
	if (file->f_mode & FMODE_WRITE) {
		stop_dac(s);
		dealloc_dmabuf(s, &s->dma_dac);
	}
	if (file->f_mode & FMODE_READ) {
		stop_adc(s);
		dealloc_dmabuf(s, &s->dma_adc);
	}
	s->open_mode &= (~file->f_mode) & (FMODE_READ|FMODE_WRITE);
	up(&s->open_sem);
	wake_up(&s->open_wait);
	unlock_kernel();
	return 0;
}

static /*const*/ struct file_operations vrc5477_ac97_audio_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.read		= vrc5477_ac97_read,
	.write		= vrc5477_ac97_write,
	.poll		= vrc5477_ac97_poll,
	.ioctl		= vrc5477_ac97_ioctl,
	// .mmap	= vrc5477_ac97_mmap,
	.open		= vrc5477_ac97_open,
	.release	= vrc5477_ac97_release,
};


/* --------------------------------------------------------------------- */


/* --------------------------------------------------------------------- */

/*
 * for debugging purposes, we'll create a proc device that dumps the
 * CODEC chipstate
 */

#ifdef VRC5477_AC97_DEBUG

struct {
       const char *regname;
       unsigned regaddr;
} vrc5477_ac97_regs[] = {
	{"VRC5477_INT_STATUS", VRC5477_INT_STATUS},
	{"VRC5477_CODEC_WR", VRC5477_CODEC_WR},
	{"VRC5477_CODEC_RD", VRC5477_CODEC_RD},
	{"VRC5477_CTRL", VRC5477_CTRL},
	{"VRC5477_ACLINK_CTRL", VRC5477_ACLINK_CTRL},
	{"VRC5477_INT_MASK", VRC5477_INT_MASK},
	{"VRC5477_DAC1_CTRL", VRC5477_DAC1_CTRL},
	{"VRC5477_DAC1L", VRC5477_DAC1L},
	{"VRC5477_DAC1_BADDR", VRC5477_DAC1_BADDR},
	{"VRC5477_DAC2_CTRL", VRC5477_DAC2_CTRL},
	{"VRC5477_DAC2L", VRC5477_DAC2L},
	{"VRC5477_DAC2_BADDR", VRC5477_DAC2_BADDR},
	{"VRC5477_DAC3_CTRL", VRC5477_DAC3_CTRL},
	{"VRC5477_DAC3L", VRC5477_DAC3L},
	{"VRC5477_DAC3_BADDR", VRC5477_DAC3_BADDR},
	{"VRC5477_ADC1_CTRL", VRC5477_ADC1_CTRL},
	{"VRC5477_ADC1L", VRC5477_ADC1L},
	{"VRC5477_ADC1_BADDR", VRC5477_ADC1_BADDR},
	{"VRC5477_ADC2_CTRL", VRC5477_ADC2_CTRL},
	{"VRC5477_ADC2L", VRC5477_ADC2L},
	{"VRC5477_ADC2_BADDR", VRC5477_ADC2_BADDR},
	{"VRC5477_ADC3_CTRL", VRC5477_ADC3_CTRL},
	{"VRC5477_ADC3L", VRC5477_ADC3L},
	{"VRC5477_ADC3_BADDR", VRC5477_ADC3_BADDR},
	{NULL, 0x0}
};

static int proc_vrc5477_ac97_dump (char *buf, char **start, off_t fpos,
				   int length, int *eof, void *data)
{
	struct vrc5477_ac97_state *s;
	int cnt, len = 0;

	if (list_empty(&devs))
		return 0;
	s = list_entry(devs.next, struct vrc5477_ac97_state, devs);

	/* print out header */
	len += sprintf(buf + len, "\n\t\tVrc5477 Audio Debug\n\n");

	// print out digital controller state
	len += sprintf (buf + len, "NEC Vrc5477 Audio Controller registers\n");
	len += sprintf (buf + len, "---------------------------------\n");
	for (cnt=0; vrc5477_ac97_regs[cnt].regname != NULL; cnt++) {
		len+= sprintf (buf + len, "%-20s = %08x\n",
			       vrc5477_ac97_regs[cnt].regname,
			       inl(s->io + vrc5477_ac97_regs[cnt].regaddr));
	}
   
	/* print out driver state */
	len += sprintf (buf + len, "NEC Vrc5477 Audio driver states\n");
	len += sprintf (buf + len, "---------------------------------\n");
	len += sprintf (buf + len, "dacChannels  = %d\n", s->dacChannels);
	len += sprintf (buf + len, "adcChannels  = %d\n", s->adcChannels);
	len += sprintf (buf + len, "dacRate  = %d\n", s->dacRate);
	len += sprintf (buf + len, "adcRate  = %d\n", s->adcRate);

	len += sprintf (buf + len, "dma_dac is %s ready\n",  
	                s->dma_dac.ready? "" : "not");
        if (s->dma_dac.ready) {
		len += sprintf (buf + len, "dma_dac is %s stopped.\n",  
	                        s->dma_dac.stopped? "" : "not");
		len += sprintf (buf + len, "dma_dac.fragSize = %x\n", 
                                s->dma_dac.fragSize);
		len += sprintf (buf + len, "dma_dac.fragShift = %x\n", 
                                s->dma_dac.fragShift);
		len += sprintf (buf + len, "dma_dac.numFrag = %x\n", 
                                s->dma_dac.numFrag);
		len += sprintf (buf + len, "dma_dac.fragTotalSize = %x\n", 
                                s->dma_dac.fragTotalSize);
		len += sprintf (buf + len, "dma_dac.nextIn = %x\n", 
                                s->dma_dac.nextIn);
		len += sprintf (buf + len, "dma_dac.nextOut = %x\n", 
                                s->dma_dac.nextOut);
		len += sprintf (buf + len, "dma_dac.count = %x\n", 
                                s->dma_dac.count);
	}

	len += sprintf (buf + len, "dma_adc is %s ready\n",  
	                s->dma_adc.ready? "" : "not");
        if (s->dma_adc.ready) {
		len += sprintf (buf + len, "dma_adc is %s stopped.\n",  
	                        s->dma_adc.stopped? "" : "not");
		len += sprintf (buf + len, "dma_adc.fragSize = %x\n", 
                                s->dma_adc.fragSize);
		len += sprintf (buf + len, "dma_adc.fragShift = %x\n", 
                                s->dma_adc.fragShift);
		len += sprintf (buf + len, "dma_adc.numFrag = %x\n", 
                                s->dma_adc.numFrag);
		len += sprintf (buf + len, "dma_adc.fragTotalSize = %x\n", 
                                s->dma_adc.fragTotalSize);
		len += sprintf (buf + len, "dma_adc.nextIn = %x\n", 
                                s->dma_adc.nextIn);
		len += sprintf (buf + len, "dma_adc.nextOut = %x\n", 
                                s->dma_adc.nextOut);
		len += sprintf (buf + len, "dma_adc.count = %x\n", 
                                s->dma_adc.count);
	}
	 
	/* print out CODEC state */
	len += sprintf (buf + len, "\nAC97 CODEC registers\n");
	len += sprintf (buf + len, "----------------------\n");
	for (cnt=0; cnt <= 0x7e; cnt = cnt +2)
		len+= sprintf (buf + len, "reg %02x = %04x\n",
			       cnt, rdcodec(s->codec, cnt));

	if (fpos >=len){
		*start = buf;
		*eof =1;
		return 0;
	}
	*start = buf + fpos;
	if ((len -= fpos) > length)
		return length;
	*eof =1;
	return len;

}
#endif /* VRC5477_AC97_DEBUG */

/* --------------------------------------------------------------------- */

/* maximum number of devices; only used for command line params */
#define NR_DEVICE 5

static unsigned int devindex;

MODULE_AUTHOR("Monta Vista Software, jsun@mvista.com or jsun@junsun.net");
MODULE_DESCRIPTION("NEC Vrc5477 audio (AC97) Driver");
MODULE_LICENSE("GPL");

static int __devinit vrc5477_ac97_probe(struct pci_dev *pcidev,
					const struct pci_device_id *pciid)
{
	struct vrc5477_ac97_state *s;
#ifdef VRC5477_AC97_DEBUG
	char proc_str[80];
#endif

	if (pcidev->irq == 0) 
		return -1;

	if (!(s = kmalloc(sizeof(struct vrc5477_ac97_state), GFP_KERNEL))) {
		printk(KERN_ERR PFX "alloc of device struct failed\n");
		return -1;
	}
	memset(s, 0, sizeof(struct vrc5477_ac97_state));

	init_waitqueue_head(&s->dma_adc.wait);
	init_waitqueue_head(&s->dma_dac.wait);
	init_waitqueue_head(&s->open_wait);
	init_MUTEX(&s->open_sem);
	spin_lock_init(&s->lock);

	s->dev = pcidev;
	s->io = pci_resource_start(pcidev, 0);
	s->irq = pcidev->irq;
	
	s->codec = ac97_alloc_codec();

	s->codec->private_data = s;
	s->codec->id = 0;
	s->codec->codec_read = rdcodec;
	s->codec->codec_write = wrcodec;
	s->codec->codec_wait = waitcodec;

	/* setting some other default values such as
	 * adcChannels, adcRate is done in open() so that
         * no persistent state across file opens.
	 */

	/* test if get response from ac97, if not return */
        if (ac97_codec_not_present(s->codec)) {
		printk(KERN_ERR PFX "no ac97 codec\n");
		goto err_region;

        }

	/* test if get response from ac97, if not return */
        if (ac97_codec_not_present(&(s->codec))) {
		printk(KERN_ERR PFX "no ac97 codec\n");
		goto err_region;

        }

	if (!request_region(s->io, pci_resource_len(pcidev,0),
			    VRC5477_AC97_MODULE_NAME)) {
		printk(KERN_ERR PFX "io ports %#lx->%#lx in use\n",
		       s->io, s->io + pci_resource_len(pcidev,0)-1);
		goto err_region;
	}
	if (request_irq(s->irq, vrc5477_ac97_interrupt, SA_INTERRUPT,
			VRC5477_AC97_MODULE_NAME, s)) {
		printk(KERN_ERR PFX "irq %u in use\n", s->irq);
		goto err_irq;
	}

	printk(KERN_INFO PFX "IO at %#lx, IRQ %d\n", s->io, s->irq);

	/* register devices */
	if ((s->dev_audio = register_sound_dsp(&vrc5477_ac97_audio_fops, -1)) < 0)
		goto err_dev1;
	if ((s->codec->dev_mixer =
	     register_sound_mixer(&vrc5477_ac97_mixer_fops, -1)) < 0)
		goto err_dev2;

#ifdef VRC5477_AC97_DEBUG
	/* initialize the debug proc device */
	s->ps = create_proc_read_entry(VRC5477_AC97_MODULE_NAME, 0, NULL,
				       proc_vrc5477_ac97_dump, NULL);
#endif /* VRC5477_AC97_DEBUG */
	
	/* enable pci io and bus mastering */
	if (pci_enable_device(pcidev))
		goto err_dev3;
	pci_set_master(pcidev);

	/* cold reset the AC97 */
	outl(VRC5477_ACLINK_CTRL_RST_ON | VRC5477_ACLINK_CTRL_RST_TIME,
	     s->io + VRC5477_ACLINK_CTRL);
	while (inl(s->io + VRC5477_ACLINK_CTRL) & VRC5477_ACLINK_CTRL_RST_ON);

	/* codec init */
	if (!ac97_probe_codec(s->codec))
		goto err_dev3;

#ifdef VRC5477_AC97_DEBUG
	sprintf(proc_str, "driver/%s/%d/ac97", 
		VRC5477_AC97_MODULE_NAME, s->codec->id);
	s->ac97_ps = create_proc_read_entry (proc_str, 0, NULL,
					     ac97_read_proc, s->codec);
	/* TODO : why this proc file does not show up? */
#endif

	/* Try to enable variable rate audio mode. */
	wrcodec(s->codec, AC97_EXTENDED_STATUS,
		rdcodec(s->codec, AC97_EXTENDED_STATUS) | AC97_EXTSTAT_VRA);
	/* Did we enable it? */
	if(rdcodec(s->codec, AC97_EXTENDED_STATUS) & AC97_EXTSTAT_VRA)
		s->extended_status |= AC97_EXTSTAT_VRA;
	else {
		s->dacRate = 48000;
		printk(KERN_INFO PFX "VRA mode not enabled; rate fixed at %d.",
			s->dacRate);
	}

        /* let us get the default volumne louder */
        wrcodec(s->codec, 0x2, 0x1010);	/* master volume, middle */
        wrcodec(s->codec, 0xc, 0x10);		/* phone volume, middle */
        // wrcodec(s->codec, 0xe, 0x10);		/* misc volume, middle */
	wrcodec(s->codec, 0x10, 0x8000);	/* line-in 2 line-out disable */
        wrcodec(s->codec, 0x18, 0x0707);	/* PCM out (line out) middle */


	/* by default we select line in the input */
	wrcodec(s->codec, 0x1a, 0x0404);
	wrcodec(s->codec, 0x1c, 0x0f0f);
	wrcodec(s->codec, 0x1e, 0x07);

	/* enable the master interrupt but disable all others */
	outl(VRC5477_INT_MASK_NMASK, s->io + VRC5477_INT_MASK);

	/* store it in the driver field */
	pci_set_drvdata(pcidev, s);
	pcidev->dma_mask = 0xffffffff;
	/* put it into driver list */
	list_add_tail(&s->devs, &devs);
	/* increment devindex */
	if (devindex < NR_DEVICE-1)
		devindex++;
	return 0;

 err_dev3:
	unregister_sound_mixer(s->codec->dev_mixer);
 err_dev2:
	unregister_sound_dsp(s->dev_audio);
 err_dev1:
	printk(KERN_ERR PFX "cannot register misc device\n");
	free_irq(s->irq, s);
 err_irq:
	release_region(s->io, pci_resource_len(pcidev,0));
 err_region:
 	ac97_release_codec(codec);
	kfree(s);
	return -1;
}

static void __devexit vrc5477_ac97_remove(struct pci_dev *dev)
{
	struct vrc5477_ac97_state *s = pci_get_drvdata(dev);

	if (!s)
		return;
	list_del(&s->devs);

#ifdef VRC5477_AC97_DEBUG
	if (s->ps)
		remove_proc_entry(VRC5477_AC97_MODULE_NAME, NULL);
#endif /* VRC5477_AC97_DEBUG */

	synchronize_irq();
	free_irq(s->irq, s);
	release_region(s->io, pci_resource_len(dev,0));
	unregister_sound_dsp(s->dev_audio);
	unregister_sound_mixer(s->codec->dev_mixer);
	ac97_release_codec(s->codec);
	kfree(s);
	pci_set_drvdata(dev, NULL);
}


static struct pci_device_id id_table[] = {
    { PCI_VENDOR_ID_NEC, PCI_DEVICE_ID_NEC_VRC5477_AC97, 
      PCI_ANY_ID, PCI_ANY_ID, 0, 0 },
    { 0, }
};

MODULE_DEVICE_TABLE(pci, id_table);

static struct pci_driver vrc5477_ac97_driver = {
	.name		= VRC5477_AC97_MODULE_NAME,
	.id_table	= id_table,
	.probe		= vrc5477_ac97_probe,
	.remove		= __devexit_p(vrc5477_ac97_remove)
};

static int __init init_vrc5477_ac97(void)
{
	printk("Vrc5477 AC97 driver: version v0.2 time " __TIME__ " " __DATE__ " by Jun Sun\n");
	return pci_register_driver(&vrc5477_ac97_driver);
}

static void __exit cleanup_vrc5477_ac97(void)
{
	printk(KERN_INFO PFX "unloading\n");
	pci_unregister_driver(&vrc5477_ac97_driver);
}

module_init(init_vrc5477_ac97);
module_exit(cleanup_vrc5477_ac97);

