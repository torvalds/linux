/****************************************************************************/

/*
 *      esssolo1.c  --  ESS Technology Solo1 (ES1946) audio driver.
 *
 *      Copyright (C) 1998-2001, 2003  Thomas Sailer (t.sailer@alumni.ethz.ch)
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Module command line parameters:
 *   none so far
 *
 *  Supported devices:
 *  /dev/dsp    standard /dev/dsp device, (mostly) OSS compatible
 *  /dev/mixer  standard /dev/mixer device, (mostly) OSS compatible
 *  /dev/midi   simple MIDI UART interface, no ioctl
 *
 *  Revision history
 *    10.11.1998   0.1   Initial release (without any hardware)
 *    22.03.1999   0.2   cinfo.blocks should be reset after GETxPTR ioctl.
 *                       reported by Johan Maes <joma@telindus.be>
 *                       return EAGAIN instead of EBUSY when O_NONBLOCK
 *                       read/write cannot be executed
 *    07.04.1999   0.3   implemented the following ioctl's: SOUND_PCM_READ_RATE, 
 *                       SOUND_PCM_READ_CHANNELS, SOUND_PCM_READ_BITS; 
 *                       Alpha fixes reported by Peter Jones <pjones@redhat.com>
 *    15.06.1999   0.4   Fix bad allocation bug.
 *                       Thanks to Deti Fliegl <fliegl@in.tum.de>
 *    28.06.1999   0.5   Add pci_set_master
 *    12.08.1999   0.6   Fix MIDI UART crashing the driver
 *                       Changed mixer semantics from OSS documented
 *                       behaviour to OSS "code behaviour".
 *                       Recording might actually work now.
 *                       The real DDMA controller address register is at PCI config
 *                       0x60, while the register at 0x18 is used as a placeholder
 *                       register for BIOS address allocation. This register
 *                       is supposed to be copied into 0x60, according
 *                       to the Solo1 datasheet. When I do that, I can access
 *                       the DDMA registers except the mask bit, which
 *                       is stuck at 1. When I copy the contents of 0x18 +0x10
 *                       to the DDMA base register, everything seems to work.
 *                       The fun part is that the Windows Solo1 driver doesn't
 *                       seem to do these tricks.
 *                       Bugs remaining: plops and clicks when starting/stopping playback
 *    31.08.1999   0.7   add spin_lock_init
 *                       replaced current->state = x with set_current_state(x)
 *    03.09.1999   0.8   change read semantics for MIDI to match
 *                       OSS more closely; remove possible wakeup race
 *    07.10.1999   0.9   Fix initialization; complain if sequencer writes time out
 *                       Revised resource grabbing for the FM synthesizer
 *    28.10.1999   0.10  More waitqueue races fixed
 *    09.12.1999   0.11  Work around stupid Alpha port issue (virt_to_bus(kmalloc(GFP_DMA)) > 16M)
 *                       Disabling recording on Alpha
 *    12.01.2000   0.12  Prevent some ioctl's from returning bad count values on underrun/overrun;
 *                       Tim Janik's BSE (Bedevilled Sound Engine) found this
 *                       Integrated (aka redid 8-)) APM support patch by Zach Brown
 *    07.02.2000   0.13  Use pci_alloc_consistent and pci_register_driver
 *    19.02.2000   0.14  Use pci_dma_supported to determine if recording should be disabled
 *    13.03.2000   0.15  Reintroduce initialization of a couple of PCI config space registers
 *    21.11.2000   0.16  Initialize dma buffers in poll, otherwise poll may return a bogus mask
 *    12.12.2000   0.17  More dma buffer initializations, patch from
 *                       Tjeerd Mulder <tjeerd.mulder@fujitsu-siemens.com>
 *    31.01.2001   0.18  Register/Unregister gameport, original patch from
 *                       Nathaniel Daw <daw@cs.cmu.edu>
 *                       Fix SETTRIGGER non OSS API conformity
 *    10.03.2001         provide abs function, prevent picking up a bogus kernel macro
 *                       for abs. Bug report by Andrew Morton <andrewm@uow.edu.au>
 *    15.05.2001         pci_enable_device moved, return values in probe cleaned
 *                       up. Marcus Meissner <mm@caldera.de>
 *    22.05.2001   0.19  more cleanups, changed PM to PCI 2.4 style, got rid
 *                       of global list of devices, using pci device data.
 *                       Marcus Meissner <mm@caldera.de>
 *    03.01.2003   0.20  open_mode fixes from Georg Acher <acher@in.tum.de>
 */

/*****************************************************************************/
      
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/sound.h>
#include <linux/slab.h>
#include <linux/soundcard.h>
#include <linux/pci.h>
#include <linux/bitops.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/spinlock.h>
#include <linux/smp_lock.h>
#include <linux/gameport.h>
#include <linux/wait.h>
#include <linux/dma-mapping.h>

#include <asm/io.h>
#include <asm/page.h>
#include <asm/uaccess.h>

#include "dm.h"

/* --------------------------------------------------------------------- */

#undef OSS_DOCUMENTED_MIXER_SEMANTICS

/* --------------------------------------------------------------------- */

#ifndef PCI_VENDOR_ID_ESS
#define PCI_VENDOR_ID_ESS         0x125d
#endif
#ifndef PCI_DEVICE_ID_ESS_SOLO1
#define PCI_DEVICE_ID_ESS_SOLO1   0x1969
#endif

#define SOLO1_MAGIC  ((PCI_VENDOR_ID_ESS<<16)|PCI_DEVICE_ID_ESS_SOLO1)

#define DDMABASE_OFFSET           0    /* chip bug workaround kludge */
#define DDMABASE_EXTENT           16

#define IOBASE_EXTENT             16
#define SBBASE_EXTENT             16
#define VCBASE_EXTENT             (DDMABASE_EXTENT+DDMABASE_OFFSET)
#define MPUBASE_EXTENT            4
#define GPBASE_EXTENT             4
#define GAMEPORT_EXTENT		  4

#define FMSYNTH_EXTENT            4

/* MIDI buffer sizes */

#define MIDIINBUF  256
#define MIDIOUTBUF 256

#define FMODE_MIDI_SHIFT 3
#define FMODE_MIDI_READ  (FMODE_READ << FMODE_MIDI_SHIFT)
#define FMODE_MIDI_WRITE (FMODE_WRITE << FMODE_MIDI_SHIFT)

#define FMODE_DMFM 0x10

#if defined(CONFIG_GAMEPORT) || (defined(MODULE) && defined(CONFIG_GAMEPORT_MODULE))
#define SUPPORT_JOYSTICK 1
#endif

static struct pci_driver solo1_driver;

/* --------------------------------------------------------------------- */

struct solo1_state {
	/* magic */
	unsigned int magic;

	/* the corresponding pci_dev structure */
	struct pci_dev *dev;

	/* soundcore stuff */
	int dev_audio;
	int dev_mixer;
	int dev_midi;
	int dev_dmfm;

	/* hardware resources */
	unsigned long iobase, sbbase, vcbase, ddmabase, mpubase; /* long for SPARC */
	unsigned int irq;

	/* mixer registers */
	struct {
		unsigned short vol[10];
		unsigned int recsrc;
		unsigned int modcnt;
		unsigned short micpreamp;
	} mix;

	/* wave stuff */
	unsigned fmt;
	unsigned channels;
	unsigned rate;
	unsigned char clkdiv;
	unsigned ena;

	spinlock_t lock;
	struct semaphore open_sem;
	mode_t open_mode;
	wait_queue_head_t open_wait;

	struct dmabuf {
		void *rawbuf;
		dma_addr_t dmaaddr;
		unsigned buforder;
		unsigned numfrag;
		unsigned fragshift;
		unsigned hwptr, swptr;
		unsigned total_bytes;
		int count;
		unsigned error; /* over/underrun */
		wait_queue_head_t wait;
		/* redundant, but makes calculations easier */
		unsigned fragsize;
		unsigned dmasize;
		unsigned fragsamples;
		/* OSS stuff */
		unsigned mapped:1;
		unsigned ready:1;
		unsigned endcleared:1;
		unsigned enabled:1;
		unsigned ossfragshift;
		int ossmaxfrags;
		unsigned subdivision;
	} dma_dac, dma_adc;

	/* midi stuff */
	struct {
		unsigned ird, iwr, icnt;
		unsigned ord, owr, ocnt;
		wait_queue_head_t iwait;
		wait_queue_head_t owait;
		struct timer_list timer;
		unsigned char ibuf[MIDIINBUF];
		unsigned char obuf[MIDIOUTBUF];
	} midi;

#if SUPPORT_JOYSTICK
	struct gameport *gameport;
#endif
};

/* --------------------------------------------------------------------- */

static inline void write_seq(struct solo1_state *s, unsigned char data)
{
        int i;
	unsigned long flags;

	/* the local_irq_save stunt is to send the data within the command window */
        for (i = 0; i < 0xffff; i++) {
		local_irq_save(flags);
                if (!(inb(s->sbbase+0xc) & 0x80)) {
                        outb(data, s->sbbase+0xc);
			local_irq_restore(flags);
                        return;
                }
		local_irq_restore(flags);
	}
	printk(KERN_ERR "esssolo1: write_seq timeout\n");
	outb(data, s->sbbase+0xc);
}

static inline int read_seq(struct solo1_state *s, unsigned char *data)
{
        int i;

        if (!data)
                return 0;
        for (i = 0; i < 0xffff; i++)
                if (inb(s->sbbase+0xe) & 0x80) {
                        *data = inb(s->sbbase+0xa);
                        return 1;
                }
	printk(KERN_ERR "esssolo1: read_seq timeout\n");
        return 0;
}

static inline int reset_ctrl(struct solo1_state *s)
{
        int i;

        outb(3, s->sbbase+6); /* clear sequencer and FIFO */
        udelay(10);
        outb(0, s->sbbase+6);
        for (i = 0; i < 0xffff; i++)
                if (inb(s->sbbase+0xe) & 0x80)
                        if (inb(s->sbbase+0xa) == 0xaa) {
				write_seq(s, 0xc6); /* enter enhanced mode */
                                return 1;
			}
        return 0;
}

static void write_ctrl(struct solo1_state *s, unsigned char reg, unsigned char data)
{
	write_seq(s, reg);
	write_seq(s, data);
}

#if 0 /* unused */
static unsigned char read_ctrl(struct solo1_state *s, unsigned char reg)
{
        unsigned char r;

	write_seq(s, 0xc0);
	write_seq(s, reg);
	read_seq(s, &r);
	return r;
}
#endif /* unused */

static void write_mixer(struct solo1_state *s, unsigned char reg, unsigned char data)
{
	outb(reg, s->sbbase+4);
	outb(data, s->sbbase+5);
}

static unsigned char read_mixer(struct solo1_state *s, unsigned char reg)
{
	outb(reg, s->sbbase+4);
	return inb(s->sbbase+5);
}

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

static inline void stop_dac(struct solo1_state *s)
{
	unsigned long flags;

	spin_lock_irqsave(&s->lock, flags);
	s->ena &= ~FMODE_WRITE;
	write_mixer(s, 0x78, 0x10);
	spin_unlock_irqrestore(&s->lock, flags);
}

static void start_dac(struct solo1_state *s)
{
	unsigned long flags;

	spin_lock_irqsave(&s->lock, flags);
	if (!(s->ena & FMODE_WRITE) && (s->dma_dac.mapped || s->dma_dac.count > 0) && s->dma_dac.ready) {
		s->ena |= FMODE_WRITE;
		write_mixer(s, 0x78, 0x12);
		udelay(10);
		write_mixer(s, 0x78, 0x13);
	}
	spin_unlock_irqrestore(&s->lock, flags);
}	

static inline void stop_adc(struct solo1_state *s)
{
	unsigned long flags;

	spin_lock_irqsave(&s->lock, flags);
	s->ena &= ~FMODE_READ;
	write_ctrl(s, 0xb8, 0xe);
	spin_unlock_irqrestore(&s->lock, flags);
}

static void start_adc(struct solo1_state *s)
{
	unsigned long flags;

	spin_lock_irqsave(&s->lock, flags);
	if (!(s->ena & FMODE_READ) && (s->dma_adc.mapped || s->dma_adc.count < (signed)(s->dma_adc.dmasize - 2*s->dma_adc.fragsize))
	    && s->dma_adc.ready) {
		s->ena |= FMODE_READ;
		write_ctrl(s, 0xb8, 0xf);
#if 0
		printk(KERN_DEBUG "solo1: DMAbuffer: 0x%08lx\n", (long)s->dma_adc.rawbuf);
		printk(KERN_DEBUG "solo1: DMA: mask: 0x%02x cnt: 0x%04x addr: 0x%08x  stat: 0x%02x\n", 
		       inb(s->ddmabase+0xf), inw(s->ddmabase+4), inl(s->ddmabase), inb(s->ddmabase+8));
#endif
                outb(0, s->ddmabase+0xd); /* master reset */
		outb(1, s->ddmabase+0xf);  /* mask */
		outb(0x54/*0x14*/, s->ddmabase+0xb);  /* DMA_MODE_READ | DMA_MODE_AUTOINIT */
		outl(virt_to_bus(s->dma_adc.rawbuf), s->ddmabase);
		outw(s->dma_adc.dmasize-1, s->ddmabase+4);
		outb(0, s->ddmabase+0xf);
	}
	spin_unlock_irqrestore(&s->lock, flags);
#if 0
	printk(KERN_DEBUG "solo1: start DMA: reg B8: 0x%02x  SBstat: 0x%02x\n"
	       KERN_DEBUG "solo1: DMA: stat: 0x%02x  cnt: 0x%04x  mask: 0x%02x\n", 
	       read_ctrl(s, 0xb8), inb(s->sbbase+0xc), 
	       inb(s->ddmabase+8), inw(s->ddmabase+4), inb(s->ddmabase+0xf));
	printk(KERN_DEBUG "solo1: A1: 0x%02x  A2: 0x%02x  A4: 0x%02x  A5: 0x%02x  A8: 0x%02x\n"  
	       KERN_DEBUG "solo1: B1: 0x%02x  B2: 0x%02x  B4: 0x%02x  B7: 0x%02x  B8: 0x%02x  B9: 0x%02x\n",
	       read_ctrl(s, 0xa1), read_ctrl(s, 0xa2), read_ctrl(s, 0xa4), read_ctrl(s, 0xa5), read_ctrl(s, 0xa8), 
	       read_ctrl(s, 0xb1), read_ctrl(s, 0xb2), read_ctrl(s, 0xb4), read_ctrl(s, 0xb7), read_ctrl(s, 0xb8), 
	       read_ctrl(s, 0xb9));
#endif
}

/* --------------------------------------------------------------------- */

#define DMABUF_DEFAULTORDER (15-PAGE_SHIFT)
#define DMABUF_MINORDER 1

static inline void dealloc_dmabuf(struct solo1_state *s, struct dmabuf *db)
{
	struct page *page, *pend;

	if (db->rawbuf) {
		/* undo marking the pages as reserved */
		pend = virt_to_page(db->rawbuf + (PAGE_SIZE << db->buforder) - 1);
		for (page = virt_to_page(db->rawbuf); page <= pend; page++)
			ClearPageReserved(page);
		pci_free_consistent(s->dev, PAGE_SIZE << db->buforder, db->rawbuf, db->dmaaddr);
	}
	db->rawbuf = NULL;
	db->mapped = db->ready = 0;
}

static int prog_dmabuf(struct solo1_state *s, struct dmabuf *db)
{
	int order;
	unsigned bytespersec;
	unsigned bufs, sample_shift = 0;
	struct page *page, *pend;

	db->hwptr = db->swptr = db->total_bytes = db->count = db->error = db->endcleared = 0;
	if (!db->rawbuf) {
		db->ready = db->mapped = 0;
                for (order = DMABUF_DEFAULTORDER; order >= DMABUF_MINORDER; order--)
			if ((db->rawbuf = pci_alloc_consistent(s->dev, PAGE_SIZE << order, &db->dmaaddr)))
				break;
		if (!db->rawbuf)
			return -ENOMEM;
		db->buforder = order;
		/* now mark the pages as reserved; otherwise remap_pfn_range doesn't do what we want */
		pend = virt_to_page(db->rawbuf + (PAGE_SIZE << db->buforder) - 1);
		for (page = virt_to_page(db->rawbuf); page <= pend; page++)
			SetPageReserved(page);
	}
	if (s->fmt & (AFMT_S16_LE | AFMT_U16_LE))
		sample_shift++;
	if (s->channels > 1)
		sample_shift++;
	bytespersec = s->rate << sample_shift;
	bufs = PAGE_SIZE << db->buforder;
	if (db->ossfragshift) {
		if ((1000 << db->ossfragshift) < bytespersec)
			db->fragshift = ld2(bytespersec/1000);
		else
			db->fragshift = db->ossfragshift;
	} else {
		db->fragshift = ld2(bytespersec/100/(db->subdivision ? db->subdivision : 1));
		if (db->fragshift < 3)
			db->fragshift = 3;
	}
	db->numfrag = bufs >> db->fragshift;
	while (db->numfrag < 4 && db->fragshift > 3) {
		db->fragshift--;
		db->numfrag = bufs >> db->fragshift;
	}
	db->fragsize = 1 << db->fragshift;
	if (db->ossmaxfrags >= 4 && db->ossmaxfrags < db->numfrag)
		db->numfrag = db->ossmaxfrags;
	db->fragsamples = db->fragsize >> sample_shift;
	db->dmasize = db->numfrag << db->fragshift;
	db->enabled = 1;
	return 0;
}

static inline int prog_dmabuf_adc(struct solo1_state *s)
{
	unsigned long va;
	int c;

	stop_adc(s);
	/* check if PCI implementation supports 24bit busmaster DMA */
	if (s->dev->dma_mask > 0xffffff)
		return -EIO;
	if ((c = prog_dmabuf(s, &s->dma_adc)))
		return c;
	va = s->dma_adc.dmaaddr;
	if ((va & ~((1<<24)-1)))
		panic("solo1: buffer above 16M boundary");
	outb(0, s->ddmabase+0xd);  /* clear */
	outb(1, s->ddmabase+0xf); /* mask */
	/*outb(0, s->ddmabase+8);*/  /* enable (enable is active low!) */
	outb(0x54, s->ddmabase+0xb);  /* DMA_MODE_READ | DMA_MODE_AUTOINIT */
	outl(va, s->ddmabase);
	outw(s->dma_adc.dmasize-1, s->ddmabase+4);
	c = - s->dma_adc.fragsamples;
	write_ctrl(s, 0xa4, c);
	write_ctrl(s, 0xa5, c >> 8);
	outb(0, s->ddmabase+0xf);
	s->dma_adc.ready = 1;
	return 0;
}

static int prog_dmabuf_dac(struct solo1_state *s)
{
	unsigned long va;
	int c;

	stop_dac(s);
	if ((c = prog_dmabuf(s, &s->dma_dac)))
		return c;
	memset(s->dma_dac.rawbuf, (s->fmt & (AFMT_U8 | AFMT_U16_LE)) ? 0 : 0x80, s->dma_dac.dmasize); /* almost correct for U16 */
	va = s->dma_dac.dmaaddr;
	if ((va ^ (va + s->dma_dac.dmasize - 1)) & ~((1<<20)-1))
		panic("solo1: buffer crosses 1M boundary");
	outl(va, s->iobase);
	/* warning: s->dma_dac.dmasize & 0xffff must not be zero! i.e. this limits us to a 32k buffer */
	outw(s->dma_dac.dmasize, s->iobase+4);
	c = - s->dma_dac.fragsamples;
	write_mixer(s, 0x74, c);
	write_mixer(s, 0x76, c >> 8);
	outb(0xa, s->iobase+6);
	s->dma_dac.ready = 1;
	return 0;
}

static inline void clear_advance(void *buf, unsigned bsize, unsigned bptr, unsigned len, unsigned char c)
{
	if (bptr + len > bsize) {
		unsigned x = bsize - bptr;
		memset(((char *)buf) + bptr, c, x);
		bptr = 0;
		len -= x;
	}
	memset(((char *)buf) + bptr, c, len);
}

/* call with spinlock held! */

static void solo1_update_ptr(struct solo1_state *s)
{
	int diff;
	unsigned hwptr;

	/* update ADC pointer */
	if (s->ena & FMODE_READ) {
		hwptr = (s->dma_adc.dmasize - 1 - inw(s->ddmabase+4)) % s->dma_adc.dmasize;
                diff = (s->dma_adc.dmasize + hwptr - s->dma_adc.hwptr) % s->dma_adc.dmasize;
                s->dma_adc.hwptr = hwptr;
		s->dma_adc.total_bytes += diff;
		s->dma_adc.count += diff;
#if 0
		printk(KERN_DEBUG "solo1: rd: hwptr %u swptr %u dmasize %u count %u\n",
		       s->dma_adc.hwptr, s->dma_adc.swptr, s->dma_adc.dmasize, s->dma_adc.count);
#endif
		if (s->dma_adc.mapped) {
			if (s->dma_adc.count >= (signed)s->dma_adc.fragsize)
				wake_up(&s->dma_adc.wait);
		} else {
			if (s->dma_adc.count > (signed)(s->dma_adc.dmasize - ((3 * s->dma_adc.fragsize) >> 1))) {
				s->ena &= ~FMODE_READ;
				write_ctrl(s, 0xb8, 0xe);
				s->dma_adc.error++;
			}
			if (s->dma_adc.count > 0)
				wake_up(&s->dma_adc.wait);
		}
	}
	/* update DAC pointer */
	if (s->ena & FMODE_WRITE) {
                hwptr = (s->dma_dac.dmasize - inw(s->iobase+4)) % s->dma_dac.dmasize;
                diff = (s->dma_dac.dmasize + hwptr - s->dma_dac.hwptr) % s->dma_dac.dmasize;
                s->dma_dac.hwptr = hwptr;
		s->dma_dac.total_bytes += diff;
#if 0
		printk(KERN_DEBUG "solo1: wr: hwptr %u swptr %u dmasize %u count %u\n",
		       s->dma_dac.hwptr, s->dma_dac.swptr, s->dma_dac.dmasize, s->dma_dac.count);
#endif
		if (s->dma_dac.mapped) {
			s->dma_dac.count += diff;
			if (s->dma_dac.count >= (signed)s->dma_dac.fragsize)
				wake_up(&s->dma_dac.wait);
		} else {
			s->dma_dac.count -= diff;
			if (s->dma_dac.count <= 0) {
				s->ena &= ~FMODE_WRITE;
				write_mixer(s, 0x78, 0x12);
				s->dma_dac.error++;
			} else if (s->dma_dac.count <= (signed)s->dma_dac.fragsize && !s->dma_dac.endcleared) {
				clear_advance(s->dma_dac.rawbuf, s->dma_dac.dmasize, s->dma_dac.swptr,
					      s->dma_dac.fragsize, (s->fmt & (AFMT_U8 | AFMT_U16_LE)) ? 0 : 0x80);
				s->dma_dac.endcleared = 1;
			}
			if (s->dma_dac.count < (signed)s->dma_dac.dmasize)
				wake_up(&s->dma_dac.wait);
		}
	}
}

/* --------------------------------------------------------------------- */

static void prog_codec(struct solo1_state *s)
{
	unsigned long flags;
	int fdiv, filter;
	unsigned char c;

	reset_ctrl(s);
	write_seq(s, 0xd3);
	/* program sampling rates */
	filter = s->rate * 9 / 20; /* Set filter roll-off to 90% of rate/2 */
	fdiv = 256 - 7160000 / (filter * 82);
	spin_lock_irqsave(&s->lock, flags);
	write_ctrl(s, 0xa1, s->clkdiv);
	write_ctrl(s, 0xa2, fdiv);
	write_mixer(s, 0x70, s->clkdiv);
	write_mixer(s, 0x72, fdiv);
	/* program ADC parameters */
	write_ctrl(s, 0xb8, 0xe);
	write_ctrl(s, 0xb9, /*0x1*/0);
	write_ctrl(s, 0xa8, (s->channels > 1) ? 0x11 : 0x12);
	c = 0xd0;
	if (s->fmt & (AFMT_S16_LE | AFMT_U16_LE))
		c |= 0x04;
	if (s->fmt & (AFMT_S16_LE | AFMT_S8))
		c |= 0x20;
	if (s->channels > 1)
		c ^= 0x48;
	write_ctrl(s, 0xb7, (c & 0x70) | 1);
	write_ctrl(s, 0xb7, c);
	write_ctrl(s, 0xb1, 0x50);
	write_ctrl(s, 0xb2, 0x50);
	/* program DAC parameters */
	c = 0x40;
	if (s->fmt & (AFMT_S16_LE | AFMT_U16_LE))
		c |= 1;
	if (s->fmt & (AFMT_S16_LE | AFMT_S8))
		c |= 4;
	if (s->channels > 1)
		c |= 2;
	write_mixer(s, 0x7a, c);
	write_mixer(s, 0x78, 0x10);
	s->ena = 0;
	spin_unlock_irqrestore(&s->lock, flags);
}

/* --------------------------------------------------------------------- */

static const char invalid_magic[] = KERN_CRIT "solo1: invalid magic value\n";

#define VALIDATE_STATE(s)                         \
({                                                \
	if (!(s) || (s)->magic != SOLO1_MAGIC) { \
		printk(invalid_magic);            \
		return -ENXIO;                    \
	}                                         \
})

/* --------------------------------------------------------------------- */

static int mixer_ioctl(struct solo1_state *s, unsigned int cmd, unsigned long arg)
{
	static const unsigned int mixer_src[8] = {
		SOUND_MASK_MIC, SOUND_MASK_MIC, SOUND_MASK_CD, SOUND_MASK_VOLUME,
		SOUND_MASK_MIC, 0, SOUND_MASK_LINE, 0
	};
	static const unsigned char mixtable1[SOUND_MIXER_NRDEVICES] = {
		[SOUND_MIXER_PCM]     = 1,   /* voice */
		[SOUND_MIXER_SYNTH]   = 2,   /* FM */
		[SOUND_MIXER_CD]      = 3,   /* CD */
		[SOUND_MIXER_LINE]    = 4,   /* Line */
		[SOUND_MIXER_LINE1]   = 5,   /* AUX */
		[SOUND_MIXER_MIC]     = 6,   /* Mic */
		[SOUND_MIXER_LINE2]   = 7,   /* Mono in */
		[SOUND_MIXER_SPEAKER] = 8,   /* Speaker */
		[SOUND_MIXER_RECLEV]  = 9,   /* Recording level */
		[SOUND_MIXER_VOLUME]  = 10   /* Master Volume */
	};
	static const unsigned char mixreg[] = {
		0x7c,   /* voice */
		0x36,   /* FM */
		0x38,   /* CD */
		0x3e,   /* Line */
		0x3a,   /* AUX */
		0x1a,   /* Mic */
		0x6d    /* Mono in */
	};
	unsigned char l, r, rl, rr, vidx;
	int i, val;
	int __user *p = (int __user *)arg;

	VALIDATE_STATE(s);

	if (cmd == SOUND_MIXER_PRIVATE1) {
		/* enable/disable/query mixer preamp */
		if (get_user(val, p))
			return -EFAULT;
		if (val != -1) {
			val = val ? 0xff : 0xf7;
			write_mixer(s, 0x7d, (read_mixer(s, 0x7d) | 0x08) & val);
		}
		val = (read_mixer(s, 0x7d) & 0x08) ? 1 : 0;
		return put_user(val, p);
	}
	if (cmd == SOUND_MIXER_PRIVATE2) {
		/* enable/disable/query spatializer */
		if (get_user(val, p))
			return -EFAULT;
		if (val != -1) {
			val &= 0x3f;
			write_mixer(s, 0x52, val);
			write_mixer(s, 0x50, val ? 0x08 : 0);
		}
		return put_user(read_mixer(s, 0x52), p);
	}
        if (cmd == SOUND_MIXER_INFO) {
		mixer_info info;
		strncpy(info.id, "Solo1", sizeof(info.id));
		strncpy(info.name, "ESS Solo1", sizeof(info.name));
		info.modify_counter = s->mix.modcnt;
		if (copy_to_user((void __user *)arg, &info, sizeof(info)))
			return -EFAULT;
		return 0;
	}
	if (cmd == SOUND_OLD_MIXER_INFO) {
		_old_mixer_info info;
		strncpy(info.id, "Solo1", sizeof(info.id));
		strncpy(info.name, "ESS Solo1", sizeof(info.name));
		if (copy_to_user((void __user *)arg, &info, sizeof(info)))
			return -EFAULT;
		return 0;
	}
	if (cmd == OSS_GETVERSION)
		return put_user(SOUND_VERSION, p);
	if (_IOC_TYPE(cmd) != 'M' || _SIOC_SIZE(cmd) != sizeof(int))
                return -EINVAL;
        if (_SIOC_DIR(cmd) == _SIOC_READ) {
                switch (_IOC_NR(cmd)) {
                case SOUND_MIXER_RECSRC: /* Arg contains a bit for each recording source */
			return put_user(mixer_src[read_mixer(s, 0x1c) & 7], p);

                case SOUND_MIXER_DEVMASK: /* Arg contains a bit for each supported device */
			return put_user(SOUND_MASK_PCM | SOUND_MASK_SYNTH | SOUND_MASK_CD |
					SOUND_MASK_LINE | SOUND_MASK_LINE1 | SOUND_MASK_MIC |
					SOUND_MASK_VOLUME | SOUND_MASK_LINE2 | SOUND_MASK_RECLEV |
					SOUND_MASK_SPEAKER, p);

                case SOUND_MIXER_RECMASK: /* Arg contains a bit for each supported recording source */
			return put_user(SOUND_MASK_LINE | SOUND_MASK_MIC | SOUND_MASK_CD | SOUND_MASK_VOLUME, p);

                case SOUND_MIXER_STEREODEVS: /* Mixer channels supporting stereo */
			return put_user(SOUND_MASK_PCM | SOUND_MASK_SYNTH | SOUND_MASK_CD |
					SOUND_MASK_LINE | SOUND_MASK_LINE1 | SOUND_MASK_MIC |
					SOUND_MASK_VOLUME | SOUND_MASK_LINE2 | SOUND_MASK_RECLEV, p);
			
                case SOUND_MIXER_CAPS:
			return put_user(SOUND_CAP_EXCL_INPUT, p);

		default:
			i = _IOC_NR(cmd);
                        if (i >= SOUND_MIXER_NRDEVICES || !(vidx = mixtable1[i]))
                                return -EINVAL;
			return put_user(s->mix.vol[vidx-1], p);
		}
	}
        if (_SIOC_DIR(cmd) != (_SIOC_READ|_SIOC_WRITE)) 
		return -EINVAL;
	s->mix.modcnt++;
	switch (_IOC_NR(cmd)) {
	case SOUND_MIXER_RECSRC: /* Arg contains a bit for each recording source */
#if 0
	        {
			static const unsigned char regs[] = {
				0x1c, 0x1a, 0x36, 0x38, 0x3a, 0x3c, 0x3e, 0x60, 0x62, 0x6d, 0x7c
			};
			int i;
			
			for (i = 0; i < sizeof(regs); i++)
				printk(KERN_DEBUG "solo1: mixer reg 0x%02x: 0x%02x\n",
				       regs[i], read_mixer(s, regs[i]));
			printk(KERN_DEBUG "solo1: ctrl reg 0x%02x: 0x%02x\n",
			       0xb4, read_ctrl(s, 0xb4));
		}
#endif
	        if (get_user(val, p))
			return -EFAULT;
                i = hweight32(val);
                if (i == 0)
                        return 0;
                else if (i > 1) 
                        val &= ~mixer_src[read_mixer(s, 0x1c) & 7];
		for (i = 0; i < 8; i++) {
			if (mixer_src[i] & val)
				break;
		}
		if (i > 7)
			return 0;
		write_mixer(s, 0x1c, i);
		return 0;

	case SOUND_MIXER_VOLUME:
		if (get_user(val, p))
			return -EFAULT;
		l = val & 0xff;
		if (l > 100)
			l = 100;
		r = (val >> 8) & 0xff;
		if (r > 100)
			r = 100;
		if (l < 6) {
			rl = 0x40;
			l = 0;
		} else {
			rl = (l * 2 - 11) / 3;
			l = (rl * 3 + 11) / 2;
		}
		if (r < 6) {
			rr = 0x40;
			r = 0;
		} else {
			rr = (r * 2 - 11) / 3;
			r = (rr * 3 + 11) / 2;
		}
		write_mixer(s, 0x60, rl);
		write_mixer(s, 0x62, rr);
#ifdef OSS_DOCUMENTED_MIXER_SEMANTICS
                s->mix.vol[9] = ((unsigned int)r << 8) | l;
#else
                s->mix.vol[9] = val;
#endif
		return put_user(s->mix.vol[9], p);

	case SOUND_MIXER_SPEAKER:
		if (get_user(val, p))
			return -EFAULT;
		l = val & 0xff;
		if (l > 100)
			l = 100;
		else if (l < 2)
			l = 2;
		rl = (l - 2) / 14;
		l = rl * 14 + 2;
		write_mixer(s, 0x3c, rl);
#ifdef OSS_DOCUMENTED_MIXER_SEMANTICS
                s->mix.vol[7] = l * 0x101;
#else
                s->mix.vol[7] = val;
#endif
		return put_user(s->mix.vol[7], p);

	case SOUND_MIXER_RECLEV:
		if (get_user(val, p))
			return -EFAULT;
		l = (val << 1) & 0x1fe;
		if (l > 200)
			l = 200;
		else if (l < 5)
			l = 5;
		r = (val >> 7) & 0x1fe;
		if (r > 200)
			r = 200;
		else if (r < 5)
			r = 5;
		rl = (l - 5) / 13;
		rr = (r - 5) / 13;
		r = (rl * 13 + 5) / 2;
		l = (rr * 13 + 5) / 2;
		write_ctrl(s, 0xb4, (rl << 4) | rr);
#ifdef OSS_DOCUMENTED_MIXER_SEMANTICS
                s->mix.vol[8] = ((unsigned int)r << 8) | l;
#else
                s->mix.vol[8] = val;
#endif
		return put_user(s->mix.vol[8], p);

	default:
		i = _IOC_NR(cmd);
		if (i >= SOUND_MIXER_NRDEVICES || !(vidx = mixtable1[i]))
			return -EINVAL;
		if (get_user(val, p))
			return -EFAULT;
		l = (val << 1) & 0x1fe;
		if (l > 200)
			l = 200;
		else if (l < 5)
			l = 5;
		r = (val >> 7) & 0x1fe;
		if (r > 200)
			r = 200;
		else if (r < 5)
			r = 5;
		rl = (l - 5) / 13;
		rr = (r - 5) / 13;
		r = (rl * 13 + 5) / 2;
		l = (rr * 13 + 5) / 2;
		write_mixer(s, mixreg[vidx-1], (rl << 4) | rr);
#ifdef OSS_DOCUMENTED_MIXER_SEMANTICS
                s->mix.vol[vidx-1] = ((unsigned int)r << 8) | l;
#else
                s->mix.vol[vidx-1] = val;
#endif
		return put_user(s->mix.vol[vidx-1], p);
	}
}

/* --------------------------------------------------------------------- */

static int solo1_open_mixdev(struct inode *inode, struct file *file)
{
	unsigned int minor = iminor(inode);
	struct solo1_state *s = NULL;
	struct pci_dev *pci_dev = NULL;

	while ((pci_dev = pci_find_device(PCI_ANY_ID, PCI_ANY_ID, pci_dev)) != NULL) {
		struct pci_driver *drvr;
		drvr = pci_dev_driver (pci_dev);
		if (drvr != &solo1_driver)
			continue;
		s = (struct solo1_state*)pci_get_drvdata(pci_dev);
		if (!s)
			continue;
		if (s->dev_mixer == minor)
			break;
	}
	if (!s)
		return -ENODEV;
       	VALIDATE_STATE(s);
	file->private_data = s;
	return nonseekable_open(inode, file);
}

static int solo1_release_mixdev(struct inode *inode, struct file *file)
{
	struct solo1_state *s = (struct solo1_state *)file->private_data;

	VALIDATE_STATE(s);
	return 0;
}

static int solo1_ioctl_mixdev(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	return mixer_ioctl((struct solo1_state *)file->private_data, cmd, arg);
}

static /*const*/ struct file_operations solo1_mixer_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.ioctl		= solo1_ioctl_mixdev,
	.open		= solo1_open_mixdev,
	.release	= solo1_release_mixdev,
};

/* --------------------------------------------------------------------- */

static int drain_dac(struct solo1_state *s, int nonblock)
{
	DECLARE_WAITQUEUE(wait, current);
	unsigned long flags;
	int count;
	unsigned tmo;
	
	if (s->dma_dac.mapped)
		return 0;
        add_wait_queue(&s->dma_dac.wait, &wait);
        for (;;) {
		set_current_state(TASK_INTERRUPTIBLE);
                spin_lock_irqsave(&s->lock, flags);
		count = s->dma_dac.count;
                spin_unlock_irqrestore(&s->lock, flags);
		if (count <= 0)
			break;
		if (signal_pending(current))
                        break;
                if (nonblock) {
                        remove_wait_queue(&s->dma_dac.wait, &wait);
                        set_current_state(TASK_RUNNING);
                        return -EBUSY;
                }
		tmo = 3 * HZ * (count + s->dma_dac.fragsize) / 2 / s->rate;
		if (s->fmt & (AFMT_S16_LE | AFMT_U16_LE))
			tmo >>= 1;
		if (s->channels > 1)
			tmo >>= 1;
                if (!schedule_timeout(tmo + 1))
                        printk(KERN_DEBUG "solo1: dma timed out??\n");
        }
        remove_wait_queue(&s->dma_dac.wait, &wait);
        set_current_state(TASK_RUNNING);
        if (signal_pending(current))
                return -ERESTARTSYS;
        return 0;
}

/* --------------------------------------------------------------------- */

static ssize_t solo1_read(struct file *file, char __user *buffer, size_t count, loff_t *ppos)
{
	struct solo1_state *s = (struct solo1_state *)file->private_data;
	DECLARE_WAITQUEUE(wait, current);
	ssize_t ret;
	unsigned long flags;
	unsigned swptr;
	int cnt;

	VALIDATE_STATE(s);
	if (s->dma_adc.mapped)
		return -ENXIO;
	if (!s->dma_adc.ready && (ret = prog_dmabuf_adc(s)))
		return ret;
	if (!access_ok(VERIFY_WRITE, buffer, count))
		return -EFAULT;
	ret = 0;
	add_wait_queue(&s->dma_adc.wait, &wait);
	while (count > 0) {
		spin_lock_irqsave(&s->lock, flags);
		swptr = s->dma_adc.swptr;
		cnt = s->dma_adc.dmasize-swptr;
		if (s->dma_adc.count < cnt)
			cnt = s->dma_adc.count;
		if (cnt <= 0)
			__set_current_state(TASK_INTERRUPTIBLE);
		spin_unlock_irqrestore(&s->lock, flags);
		if (cnt > count)
			cnt = count;
#ifdef DEBUGREC
		printk(KERN_DEBUG "solo1_read: reg B8: 0x%02x  DMAstat: 0x%02x  DMAcnt: 0x%04x  SBstat: 0x%02x  cnt: %u\n", 
		       read_ctrl(s, 0xb8), inb(s->ddmabase+8), inw(s->ddmabase+4), inb(s->sbbase+0xc), cnt);
#endif
		if (cnt <= 0) {
			if (s->dma_adc.enabled)
				start_adc(s);
#ifdef DEBUGREC
			printk(KERN_DEBUG "solo1_read: regs: A1: 0x%02x  A2: 0x%02x  A4: 0x%02x  A5: 0x%02x  A8: 0x%02x\n"
			       KERN_DEBUG "solo1_read: regs: B1: 0x%02x  B2: 0x%02x  B7: 0x%02x  B8: 0x%02x  B9: 0x%02x\n"
			       KERN_DEBUG "solo1_read: DMA: addr: 0x%08x cnt: 0x%04x stat: 0x%02x mask: 0x%02x\n"  
			       KERN_DEBUG "solo1_read: SBstat: 0x%02x  cnt: %u\n",
			       read_ctrl(s, 0xa1), read_ctrl(s, 0xa2), read_ctrl(s, 0xa4), read_ctrl(s, 0xa5), read_ctrl(s, 0xa8), 
			       read_ctrl(s, 0xb1), read_ctrl(s, 0xb2), read_ctrl(s, 0xb7), read_ctrl(s, 0xb8), read_ctrl(s, 0xb9), 
			       inl(s->ddmabase), inw(s->ddmabase+4), inb(s->ddmabase+8), inb(s->ddmabase+15), inb(s->sbbase+0xc), cnt);
#endif
			if (inb(s->ddmabase+15) & 1)
				printk(KERN_ERR "solo1: cannot start recording, DDMA mask bit stuck at 1\n");
			if (file->f_flags & O_NONBLOCK) {
				if (!ret)
					ret = -EAGAIN;
				break;
			}
			schedule();
#ifdef DEBUGREC
			printk(KERN_DEBUG "solo1_read: regs: A1: 0x%02x  A2: 0x%02x  A4: 0x%02x  A5: 0x%02x  A8: 0x%02x\n"
			       KERN_DEBUG "solo1_read: regs: B1: 0x%02x  B2: 0x%02x  B7: 0x%02x  B8: 0x%02x  B9: 0x%02x\n"
			       KERN_DEBUG "solo1_read: DMA: addr: 0x%08x cnt: 0x%04x stat: 0x%02x mask: 0x%02x\n"  
			       KERN_DEBUG "solo1_read: SBstat: 0x%02x  cnt: %u\n",
			       read_ctrl(s, 0xa1), read_ctrl(s, 0xa2), read_ctrl(s, 0xa4), read_ctrl(s, 0xa5), read_ctrl(s, 0xa8), 
			       read_ctrl(s, 0xb1), read_ctrl(s, 0xb2), read_ctrl(s, 0xb7), read_ctrl(s, 0xb8), read_ctrl(s, 0xb9), 
			       inl(s->ddmabase), inw(s->ddmabase+4), inb(s->ddmabase+8), inb(s->ddmabase+15), inb(s->sbbase+0xc), cnt);
#endif
			if (signal_pending(current)) {
				if (!ret)
					ret = -ERESTARTSYS;
				break;
			}
			continue;
		}
		if (copy_to_user(buffer, s->dma_adc.rawbuf + swptr, cnt)) {
			if (!ret)
				ret = -EFAULT;
			break;
		}
		swptr = (swptr + cnt) % s->dma_adc.dmasize;
		spin_lock_irqsave(&s->lock, flags);
		s->dma_adc.swptr = swptr;
		s->dma_adc.count -= cnt;
		spin_unlock_irqrestore(&s->lock, flags);
		count -= cnt;
		buffer += cnt;
		ret += cnt;
		if (s->dma_adc.enabled)
			start_adc(s);
#ifdef DEBUGREC
		printk(KERN_DEBUG "solo1_read: reg B8: 0x%02x  DMAstat: 0x%02x  DMAcnt: 0x%04x  SBstat: 0x%02x\n", 
		       read_ctrl(s, 0xb8), inb(s->ddmabase+8), inw(s->ddmabase+4), inb(s->sbbase+0xc));
#endif
	}
	remove_wait_queue(&s->dma_adc.wait, &wait);
	set_current_state(TASK_RUNNING);
	return ret;
}

static ssize_t solo1_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
	struct solo1_state *s = (struct solo1_state *)file->private_data;
	DECLARE_WAITQUEUE(wait, current);
	ssize_t ret;
	unsigned long flags;
	unsigned swptr;
	int cnt;

	VALIDATE_STATE(s);
	if (s->dma_dac.mapped)
		return -ENXIO;
	if (!s->dma_dac.ready && (ret = prog_dmabuf_dac(s)))
		return ret;
	if (!access_ok(VERIFY_READ, buffer, count))
		return -EFAULT;
#if 0
	printk(KERN_DEBUG "solo1_write: reg 70: 0x%02x  71: 0x%02x  72: 0x%02x  74: 0x%02x  76: 0x%02x  78: 0x%02x  7A: 0x%02x\n"
	       KERN_DEBUG "solo1_write: DMA: addr: 0x%08x  cnt: 0x%04x  stat: 0x%02x  SBstat: 0x%02x\n", 
	       read_mixer(s, 0x70), read_mixer(s, 0x71), read_mixer(s, 0x72), read_mixer(s, 0x74), read_mixer(s, 0x76),
	       read_mixer(s, 0x78), read_mixer(s, 0x7a), inl(s->iobase), inw(s->iobase+4), inb(s->iobase+6), inb(s->sbbase+0xc));
	printk(KERN_DEBUG "solo1_write: reg 78: 0x%02x  reg 7A: 0x%02x  DMAcnt: 0x%04x  DMAstat: 0x%02x  SBstat: 0x%02x\n", 
	       read_mixer(s, 0x78), read_mixer(s, 0x7a), inw(s->iobase+4), inb(s->iobase+6), inb(s->sbbase+0xc));
#endif
	ret = 0;
	add_wait_queue(&s->dma_dac.wait, &wait);	
	while (count > 0) {
		spin_lock_irqsave(&s->lock, flags);
		if (s->dma_dac.count < 0) {
			s->dma_dac.count = 0;
			s->dma_dac.swptr = s->dma_dac.hwptr;
		}
		swptr = s->dma_dac.swptr;
		cnt = s->dma_dac.dmasize-swptr;
		if (s->dma_dac.count + cnt > s->dma_dac.dmasize)
			cnt = s->dma_dac.dmasize - s->dma_dac.count;
		if (cnt <= 0)
			__set_current_state(TASK_INTERRUPTIBLE);
		spin_unlock_irqrestore(&s->lock, flags);
		if (cnt > count)
			cnt = count;
		if (cnt <= 0) {
			if (s->dma_dac.enabled)
				start_dac(s);
			if (file->f_flags & O_NONBLOCK) {
				if (!ret)
					ret = -EAGAIN;
				break;
			}
			schedule();
			if (signal_pending(current)) {
				if (!ret)
					ret = -ERESTARTSYS;
				break;
			}
			continue;
		}
		if (copy_from_user(s->dma_dac.rawbuf + swptr, buffer, cnt)) {
			if (!ret)
				ret = -EFAULT;
			break;
		}
		swptr = (swptr + cnt) % s->dma_dac.dmasize;
		spin_lock_irqsave(&s->lock, flags);
		s->dma_dac.swptr = swptr;
		s->dma_dac.count += cnt;
		s->dma_dac.endcleared = 0;
		spin_unlock_irqrestore(&s->lock, flags);
		count -= cnt;
		buffer += cnt;
		ret += cnt;
		if (s->dma_dac.enabled)
			start_dac(s);
	}
	remove_wait_queue(&s->dma_dac.wait, &wait);
	set_current_state(TASK_RUNNING);
	return ret;
}

/* No kernel lock - we have our own spinlock */
static unsigned int solo1_poll(struct file *file, struct poll_table_struct *wait)
{
	struct solo1_state *s = (struct solo1_state *)file->private_data;
	unsigned long flags;
	unsigned int mask = 0;

	VALIDATE_STATE(s);
	if (file->f_mode & FMODE_WRITE) {
		if (!s->dma_dac.ready && prog_dmabuf_dac(s))
			return 0;
		poll_wait(file, &s->dma_dac.wait, wait);
	}
	if (file->f_mode & FMODE_READ) {
		if (!s->dma_adc.ready && prog_dmabuf_adc(s))
			return 0;
		poll_wait(file, &s->dma_adc.wait, wait);
	}
	spin_lock_irqsave(&s->lock, flags);
	solo1_update_ptr(s);
	if (file->f_mode & FMODE_READ) {
		if (s->dma_adc.mapped) {
			if (s->dma_adc.count >= (signed)s->dma_adc.fragsize)
				mask |= POLLIN | POLLRDNORM;
		} else {
			if (s->dma_adc.count > 0)
				mask |= POLLIN | POLLRDNORM;
		}
	}
	if (file->f_mode & FMODE_WRITE) {
		if (s->dma_dac.mapped) {
			if (s->dma_dac.count >= (signed)s->dma_dac.fragsize) 
				mask |= POLLOUT | POLLWRNORM;
		} else {
			if ((signed)s->dma_dac.dmasize > s->dma_dac.count)
				mask |= POLLOUT | POLLWRNORM;
		}
	}
	spin_unlock_irqrestore(&s->lock, flags);
	return mask;
}


static int solo1_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct solo1_state *s = (struct solo1_state *)file->private_data;
	struct dmabuf *db;
	int ret = -EINVAL;
	unsigned long size;

	VALIDATE_STATE(s);
	lock_kernel();
	if (vma->vm_flags & VM_WRITE) {
		if ((ret = prog_dmabuf_dac(s)) != 0)
			goto out;
		db = &s->dma_dac;
	} else if (vma->vm_flags & VM_READ) {
		if ((ret = prog_dmabuf_adc(s)) != 0)
			goto out;
		db = &s->dma_adc;
	} else 
		goto out;
	ret = -EINVAL;
	if (vma->vm_pgoff != 0)
		goto out;
	size = vma->vm_end - vma->vm_start;
	if (size > (PAGE_SIZE << db->buforder))
		goto out;
	ret = -EAGAIN;
	if (remap_pfn_range(vma, vma->vm_start,
				virt_to_phys(db->rawbuf) >> PAGE_SHIFT,
				size, vma->vm_page_prot))
		goto out;
	db->mapped = 1;
	ret = 0;
out:
	unlock_kernel();
	return ret;
}

static int solo1_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	struct solo1_state *s = (struct solo1_state *)file->private_data;
	unsigned long flags;
        audio_buf_info abinfo;
        count_info cinfo;
	int val, mapped, ret, count;
        int div1, div2;
        unsigned rate1, rate2;
	void __user *argp = (void __user *)arg;
	int __user *p = argp;

	VALIDATE_STATE(s);
        mapped = ((file->f_mode & FMODE_WRITE) && s->dma_dac.mapped) ||
		((file->f_mode & FMODE_READ) && s->dma_adc.mapped);
	switch (cmd) {
	case OSS_GETVERSION:
		return put_user(SOUND_VERSION, p);

	case SNDCTL_DSP_SYNC:
		if (file->f_mode & FMODE_WRITE)
			return drain_dac(s, 0/*file->f_flags & O_NONBLOCK*/);
		return 0;
		
	case SNDCTL_DSP_SETDUPLEX:
		return 0;

	case SNDCTL_DSP_GETCAPS:
		return put_user(DSP_CAP_DUPLEX | DSP_CAP_REALTIME | DSP_CAP_TRIGGER | DSP_CAP_MMAP, p);
		
        case SNDCTL_DSP_RESET:
		if (file->f_mode & FMODE_WRITE) {
			stop_dac(s);
			synchronize_irq(s->irq);
			s->dma_dac.swptr = s->dma_dac.hwptr = s->dma_dac.count = s->dma_dac.total_bytes = 0;
		}
		if (file->f_mode & FMODE_READ) {
			stop_adc(s);
			synchronize_irq(s->irq);
			s->dma_adc.swptr = s->dma_adc.hwptr = s->dma_adc.count = s->dma_adc.total_bytes = 0;
		}
		prog_codec(s);
		return 0;

        case SNDCTL_DSP_SPEED:
                if (get_user(val, p))
			return -EFAULT;
		if (val >= 0) {
			stop_adc(s);
			stop_dac(s);
			s->dma_adc.ready = s->dma_dac.ready = 0;
			/* program sampling rates */
			if (val > 48000)
				val = 48000;
			if (val < 6300)
				val = 6300;
			div1 = (768000 + val / 2) / val;
			rate1 = (768000 + div1 / 2) / div1;
			div1 = -div1;
			div2 = (793800 + val / 2) / val;
			rate2 = (793800 + div2 / 2) / div2;
			div2 = (-div2) & 0x7f;
			if (abs(val - rate2) < abs(val - rate1)) {
				rate1 = rate2;
				div1 = div2;
			}
			s->rate = rate1;
			s->clkdiv = div1;
			prog_codec(s);
		}
		return put_user(s->rate, p);
		
        case SNDCTL_DSP_STEREO:
                if (get_user(val, p))
			return -EFAULT;
		stop_adc(s);
		stop_dac(s);
		s->dma_adc.ready = s->dma_dac.ready = 0;
		/* program channels */
		s->channels = val ? 2 : 1;
		prog_codec(s);
		return 0;

        case SNDCTL_DSP_CHANNELS:
                if (get_user(val, p))
			return -EFAULT;
		if (val != 0) {
			stop_adc(s);
			stop_dac(s);
			s->dma_adc.ready = s->dma_dac.ready = 0;
			/* program channels */
			s->channels = (val >= 2) ? 2 : 1;
			prog_codec(s);
		}
		return put_user(s->channels, p);

	case SNDCTL_DSP_GETFMTS: /* Returns a mask */
                return put_user(AFMT_S16_LE|AFMT_U16_LE|AFMT_S8|AFMT_U8, p);

	case SNDCTL_DSP_SETFMT: /* Selects ONE fmt*/
		if (get_user(val, p))
			return -EFAULT;
		if (val != AFMT_QUERY) {
			stop_adc(s);
			stop_dac(s);
			s->dma_adc.ready = s->dma_dac.ready = 0;
			/* program format */
			if (val != AFMT_S16_LE && val != AFMT_U16_LE && 
			    val != AFMT_S8 && val != AFMT_U8)
				val = AFMT_U8;
			s->fmt = val;
			prog_codec(s);
		}
		return put_user(s->fmt, p);

	case SNDCTL_DSP_POST:
                return 0;

        case SNDCTL_DSP_GETTRIGGER:
		val = 0;
		if (file->f_mode & s->ena & FMODE_READ)
			val |= PCM_ENABLE_INPUT;
		if (file->f_mode & s->ena & FMODE_WRITE)
			val |= PCM_ENABLE_OUTPUT;
		return put_user(val, p);

	case SNDCTL_DSP_SETTRIGGER:
		if (get_user(val, p))
			return -EFAULT;
		if (file->f_mode & FMODE_READ) {
			if (val & PCM_ENABLE_INPUT) {
				if (!s->dma_adc.ready && (ret = prog_dmabuf_adc(s)))
					return ret;
				s->dma_dac.enabled = 1;
				start_adc(s);
				if (inb(s->ddmabase+15) & 1)
					printk(KERN_ERR "solo1: cannot start recording, DDMA mask bit stuck at 1\n");
			} else {
				s->dma_dac.enabled = 0;
				stop_adc(s);
			}
		}
		if (file->f_mode & FMODE_WRITE) {
			if (val & PCM_ENABLE_OUTPUT) {
				if (!s->dma_dac.ready && (ret = prog_dmabuf_dac(s)))
					return ret;
				s->dma_dac.enabled = 1;
				start_dac(s);
			} else {
				s->dma_dac.enabled = 0;
				stop_dac(s);
			}
		}
		return 0;

	case SNDCTL_DSP_GETOSPACE:
		if (!(file->f_mode & FMODE_WRITE))
			return -EINVAL;
		if (!s->dma_dac.ready && (val = prog_dmabuf_dac(s)) != 0)
			return val;
		spin_lock_irqsave(&s->lock, flags);
		solo1_update_ptr(s);
		abinfo.fragsize = s->dma_dac.fragsize;
		count = s->dma_dac.count;
		if (count < 0)
			count = 0;
                abinfo.bytes = s->dma_dac.dmasize - count;
                abinfo.fragstotal = s->dma_dac.numfrag;
                abinfo.fragments = abinfo.bytes >> s->dma_dac.fragshift;      
		spin_unlock_irqrestore(&s->lock, flags);
		return copy_to_user(argp, &abinfo, sizeof(abinfo)) ? -EFAULT : 0;

	case SNDCTL_DSP_GETISPACE:
		if (!(file->f_mode & FMODE_READ))
			return -EINVAL;
		if (!s->dma_adc.ready && (val = prog_dmabuf_adc(s)) != 0)
			return val;
		spin_lock_irqsave(&s->lock, flags);
		solo1_update_ptr(s);
		abinfo.fragsize = s->dma_adc.fragsize;
                abinfo.bytes = s->dma_adc.count;
                abinfo.fragstotal = s->dma_adc.numfrag;
                abinfo.fragments = abinfo.bytes >> s->dma_adc.fragshift;      
		spin_unlock_irqrestore(&s->lock, flags);
		return copy_to_user(argp, &abinfo, sizeof(abinfo)) ? -EFAULT : 0;

        case SNDCTL_DSP_NONBLOCK:
                file->f_flags |= O_NONBLOCK;
                return 0;

        case SNDCTL_DSP_GETODELAY:
		if (!(file->f_mode & FMODE_WRITE))
			return -EINVAL;
		if (!s->dma_dac.ready && (val = prog_dmabuf_dac(s)) != 0)
			return val;
		spin_lock_irqsave(&s->lock, flags);
		solo1_update_ptr(s);
                count = s->dma_dac.count;
		spin_unlock_irqrestore(&s->lock, flags);
		if (count < 0)
			count = 0;
		return put_user(count, p);

        case SNDCTL_DSP_GETIPTR:
		if (!(file->f_mode & FMODE_READ))
			return -EINVAL;
		if (!s->dma_adc.ready && (val = prog_dmabuf_adc(s)) != 0)
			return val;
		spin_lock_irqsave(&s->lock, flags);
		solo1_update_ptr(s);
                cinfo.bytes = s->dma_adc.total_bytes;
                cinfo.blocks = s->dma_adc.count >> s->dma_adc.fragshift;
                cinfo.ptr = s->dma_adc.hwptr;
		if (s->dma_adc.mapped)
			s->dma_adc.count &= s->dma_adc.fragsize-1;
		spin_unlock_irqrestore(&s->lock, flags);
		if (copy_to_user(argp, &cinfo, sizeof(cinfo)))
			return -EFAULT;
		return 0;

        case SNDCTL_DSP_GETOPTR:
		if (!(file->f_mode & FMODE_WRITE))
			return -EINVAL;
		if (!s->dma_dac.ready && (val = prog_dmabuf_dac(s)) != 0)
			return val;
		spin_lock_irqsave(&s->lock, flags);
		solo1_update_ptr(s);
                cinfo.bytes = s->dma_dac.total_bytes;
		count = s->dma_dac.count;
		if (count < 0)
			count = 0;
                cinfo.blocks = count >> s->dma_dac.fragshift;
                cinfo.ptr = s->dma_dac.hwptr;
		if (s->dma_dac.mapped)
			s->dma_dac.count &= s->dma_dac.fragsize-1;
		spin_unlock_irqrestore(&s->lock, flags);
#if 0
		printk(KERN_DEBUG "esssolo1: GETOPTR: bytes %u blocks %u ptr %u, buforder %u numfrag %u fragshift %u\n"
		       KERN_DEBUG "esssolo1: swptr %u count %u fragsize %u dmasize %u fragsamples %u\n",
		       cinfo.bytes, cinfo.blocks, cinfo.ptr, s->dma_dac.buforder, s->dma_dac.numfrag, s->dma_dac.fragshift,
		       s->dma_dac.swptr, s->dma_dac.count, s->dma_dac.fragsize, s->dma_dac.dmasize, s->dma_dac.fragsamples);
#endif
		if (copy_to_user(argp, &cinfo, sizeof(cinfo)))
			return -EFAULT;
		return 0;

        case SNDCTL_DSP_GETBLKSIZE:
		if (file->f_mode & FMODE_WRITE) {
			if ((val = prog_dmabuf_dac(s)))
				return val;
			return put_user(s->dma_dac.fragsize, p);
		}
		if ((val = prog_dmabuf_adc(s)))
			return val;
		return put_user(s->dma_adc.fragsize, p);

        case SNDCTL_DSP_SETFRAGMENT:
                if (get_user(val, p))
			return -EFAULT;
		if (file->f_mode & FMODE_READ) {
			s->dma_adc.ossfragshift = val & 0xffff;
			s->dma_adc.ossmaxfrags = (val >> 16) & 0xffff;
			if (s->dma_adc.ossfragshift < 4)
				s->dma_adc.ossfragshift = 4;
			if (s->dma_adc.ossfragshift > 15)
				s->dma_adc.ossfragshift = 15;
			if (s->dma_adc.ossmaxfrags < 4)
				s->dma_adc.ossmaxfrags = 4;
		}
		if (file->f_mode & FMODE_WRITE) {
			s->dma_dac.ossfragshift = val & 0xffff;
			s->dma_dac.ossmaxfrags = (val >> 16) & 0xffff;
			if (s->dma_dac.ossfragshift < 4)
				s->dma_dac.ossfragshift = 4;
			if (s->dma_dac.ossfragshift > 15)
				s->dma_dac.ossfragshift = 15;
			if (s->dma_dac.ossmaxfrags < 4)
				s->dma_dac.ossmaxfrags = 4;
		}
		return 0;

        case SNDCTL_DSP_SUBDIVIDE:
		if ((file->f_mode & FMODE_READ && s->dma_adc.subdivision) ||
		    (file->f_mode & FMODE_WRITE && s->dma_dac.subdivision))
			return -EINVAL;
                if (get_user(val, p))
			return -EFAULT;
		if (val != 1 && val != 2 && val != 4)
			return -EINVAL;
		if (file->f_mode & FMODE_READ)
			s->dma_adc.subdivision = val;
		if (file->f_mode & FMODE_WRITE)
			s->dma_dac.subdivision = val;
		return 0;

        case SOUND_PCM_READ_RATE:
		return put_user(s->rate, p);

        case SOUND_PCM_READ_CHANNELS:
		return put_user(s->channels, p);

        case SOUND_PCM_READ_BITS:
		return put_user((s->fmt & (AFMT_S8|AFMT_U8)) ? 8 : 16, p);

        case SOUND_PCM_WRITE_FILTER:
        case SNDCTL_DSP_SETSYNCRO:
        case SOUND_PCM_READ_FILTER:
                return -EINVAL;
		
	}
	return mixer_ioctl(s, cmd, arg);
}

static int solo1_release(struct inode *inode, struct file *file)
{
	struct solo1_state *s = (struct solo1_state *)file->private_data;

	VALIDATE_STATE(s);
	lock_kernel();
	if (file->f_mode & FMODE_WRITE)
		drain_dac(s, file->f_flags & O_NONBLOCK);
	down(&s->open_sem);
	if (file->f_mode & FMODE_WRITE) {
		stop_dac(s);
		outb(0, s->iobase+6);  /* disable DMA */
		dealloc_dmabuf(s, &s->dma_dac);
	}
	if (file->f_mode & FMODE_READ) {
		stop_adc(s);
		outb(1, s->ddmabase+0xf); /* mask DMA channel */
		outb(0, s->ddmabase+0xd); /* DMA master clear */
		dealloc_dmabuf(s, &s->dma_adc);
	}
	s->open_mode &= ~(FMODE_READ | FMODE_WRITE);
	wake_up(&s->open_wait);
	up(&s->open_sem);
	unlock_kernel();
	return 0;
}

static int solo1_open(struct inode *inode, struct file *file)
{
	unsigned int minor = iminor(inode);
	DECLARE_WAITQUEUE(wait, current);
	struct solo1_state *s = NULL;
	struct pci_dev *pci_dev = NULL;
	
	while ((pci_dev = pci_find_device(PCI_ANY_ID, PCI_ANY_ID, pci_dev)) != NULL) {
		struct pci_driver *drvr;

		drvr = pci_dev_driver(pci_dev);
		if (drvr != &solo1_driver)
			continue;
		s = (struct solo1_state*)pci_get_drvdata(pci_dev);
		if (!s)
			continue;
		if (!((s->dev_audio ^ minor) & ~0xf))
			break;
	}
	if (!s)
		return -ENODEV;
       	VALIDATE_STATE(s);
	file->private_data = s;
	/* wait for device to become free */
	down(&s->open_sem);
	while (s->open_mode & (FMODE_READ | FMODE_WRITE)) {
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
	s->fmt = AFMT_U8;
	s->channels = 1;
	s->rate = 8000;
	s->clkdiv = 96 | 0x80;
	s->ena = 0;
	s->dma_adc.ossfragshift = s->dma_adc.ossmaxfrags = s->dma_adc.subdivision = 0;
	s->dma_adc.enabled = 1;
	s->dma_dac.ossfragshift = s->dma_dac.ossmaxfrags = s->dma_dac.subdivision = 0;
	s->dma_dac.enabled = 1;
	s->open_mode |= file->f_mode & (FMODE_READ | FMODE_WRITE);
	up(&s->open_sem);
	prog_codec(s);
	return nonseekable_open(inode, file);
}

static /*const*/ struct file_operations solo1_audio_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.read		= solo1_read,
	.write		= solo1_write,
	.poll		= solo1_poll,
	.ioctl		= solo1_ioctl,
	.mmap		= solo1_mmap,
	.open		= solo1_open,
	.release	= solo1_release,
};

/* --------------------------------------------------------------------- */

/* hold spinlock for the following! */
static void solo1_handle_midi(struct solo1_state *s)
{
	unsigned char ch;
	int wake;

	if (!(s->mpubase))
		return;
	wake = 0;
	while (!(inb(s->mpubase+1) & 0x80)) {
		ch = inb(s->mpubase);
		if (s->midi.icnt < MIDIINBUF) {
			s->midi.ibuf[s->midi.iwr] = ch;
			s->midi.iwr = (s->midi.iwr + 1) % MIDIINBUF;
			s->midi.icnt++;
		}
		wake = 1;
	}
	if (wake)
		wake_up(&s->midi.iwait);
	wake = 0;
	while (!(inb(s->mpubase+1) & 0x40) && s->midi.ocnt > 0) {
		outb(s->midi.obuf[s->midi.ord], s->mpubase);
		s->midi.ord = (s->midi.ord + 1) % MIDIOUTBUF;
		s->midi.ocnt--;
		if (s->midi.ocnt < MIDIOUTBUF-16)
			wake = 1;
	}
	if (wake)
		wake_up(&s->midi.owait);
}

static irqreturn_t solo1_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
        struct solo1_state *s = (struct solo1_state *)dev_id;
	unsigned int intsrc;
	
	/* fastpath out, to ease interrupt sharing */
	intsrc = inb(s->iobase+7); /* get interrupt source(s) */
	if (!intsrc)
		return IRQ_NONE;
	(void)inb(s->sbbase+0xe);  /* clear interrupt */
	spin_lock(&s->lock);
	/* clear audio interrupts first */
	if (intsrc & 0x20)
		write_mixer(s, 0x7a, read_mixer(s, 0x7a) & 0x7f);
	solo1_update_ptr(s);
	solo1_handle_midi(s);
	spin_unlock(&s->lock);
	return IRQ_HANDLED;
}

static void solo1_midi_timer(unsigned long data)
{
	struct solo1_state *s = (struct solo1_state *)data;
	unsigned long flags;
	
	spin_lock_irqsave(&s->lock, flags);
	solo1_handle_midi(s);
	spin_unlock_irqrestore(&s->lock, flags);
	s->midi.timer.expires = jiffies+1;
	add_timer(&s->midi.timer);
}

/* --------------------------------------------------------------------- */

static ssize_t solo1_midi_read(struct file *file, char __user *buffer, size_t count, loff_t *ppos)
{
	struct solo1_state *s = (struct solo1_state *)file->private_data;
	DECLARE_WAITQUEUE(wait, current);
	ssize_t ret;
	unsigned long flags;
	unsigned ptr;
	int cnt;

	VALIDATE_STATE(s);
	if (!access_ok(VERIFY_WRITE, buffer, count))
		return -EFAULT;
	if (count == 0)
		return 0;
	ret = 0;
	add_wait_queue(&s->midi.iwait, &wait);
	while (count > 0) {
		spin_lock_irqsave(&s->lock, flags);
		ptr = s->midi.ird;
		cnt = MIDIINBUF - ptr;
		if (s->midi.icnt < cnt)
			cnt = s->midi.icnt;
		if (cnt <= 0)
			__set_current_state(TASK_INTERRUPTIBLE);
		spin_unlock_irqrestore(&s->lock, flags);
		if (cnt > count)
			cnt = count;
		if (cnt <= 0) {
			if (file->f_flags & O_NONBLOCK) {
				if (!ret)
					ret = -EAGAIN;
				break;
			}
			schedule();
			if (signal_pending(current)) {
				if (!ret)
					ret = -ERESTARTSYS;
				break;
			}
			continue;
		}
		if (copy_to_user(buffer, s->midi.ibuf + ptr, cnt)) {
			if (!ret)
				ret = -EFAULT;
			break;
		}
		ptr = (ptr + cnt) % MIDIINBUF;
		spin_lock_irqsave(&s->lock, flags);
		s->midi.ird = ptr;
		s->midi.icnt -= cnt;
		spin_unlock_irqrestore(&s->lock, flags);
		count -= cnt;
		buffer += cnt;
		ret += cnt;
		break;
	}
	__set_current_state(TASK_RUNNING);
	remove_wait_queue(&s->midi.iwait, &wait);
	return ret;
}

static ssize_t solo1_midi_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
	struct solo1_state *s = (struct solo1_state *)file->private_data;
	DECLARE_WAITQUEUE(wait, current);
	ssize_t ret;
	unsigned long flags;
	unsigned ptr;
	int cnt;

	VALIDATE_STATE(s);
	if (!access_ok(VERIFY_READ, buffer, count))
		return -EFAULT;
	if (count == 0)
		return 0;
	ret = 0;
        add_wait_queue(&s->midi.owait, &wait);
	while (count > 0) {
		spin_lock_irqsave(&s->lock, flags);
		ptr = s->midi.owr;
		cnt = MIDIOUTBUF - ptr;
		if (s->midi.ocnt + cnt > MIDIOUTBUF)
			cnt = MIDIOUTBUF - s->midi.ocnt;
		if (cnt <= 0) {
			__set_current_state(TASK_INTERRUPTIBLE);
			solo1_handle_midi(s);
		}
		spin_unlock_irqrestore(&s->lock, flags);
		if (cnt > count)
			cnt = count;
		if (cnt <= 0) {
			if (file->f_flags & O_NONBLOCK) {
				if (!ret)
					ret = -EAGAIN;
				break;
			}
			schedule();
			if (signal_pending(current)) {
				if (!ret)
					ret = -ERESTARTSYS;
				break;
			}
			continue;
		}
		if (copy_from_user(s->midi.obuf + ptr, buffer, cnt)) {
			if (!ret)
				ret = -EFAULT;
			break;
		}
		ptr = (ptr + cnt) % MIDIOUTBUF;
		spin_lock_irqsave(&s->lock, flags);
		s->midi.owr = ptr;
		s->midi.ocnt += cnt;
		spin_unlock_irqrestore(&s->lock, flags);
		count -= cnt;
		buffer += cnt;
		ret += cnt;
		spin_lock_irqsave(&s->lock, flags);
		solo1_handle_midi(s);
		spin_unlock_irqrestore(&s->lock, flags);
	}
	__set_current_state(TASK_RUNNING);
	remove_wait_queue(&s->midi.owait, &wait);
	return ret;
}

/* No kernel lock - we have our own spinlock */
static unsigned int solo1_midi_poll(struct file *file, struct poll_table_struct *wait)
{
	struct solo1_state *s = (struct solo1_state *)file->private_data;
	unsigned long flags;
	unsigned int mask = 0;

	VALIDATE_STATE(s);
	if (file->f_flags & FMODE_WRITE)
		poll_wait(file, &s->midi.owait, wait);
	if (file->f_flags & FMODE_READ)
		poll_wait(file, &s->midi.iwait, wait);
	spin_lock_irqsave(&s->lock, flags);
	if (file->f_flags & FMODE_READ) {
		if (s->midi.icnt > 0)
			mask |= POLLIN | POLLRDNORM;
	}
	if (file->f_flags & FMODE_WRITE) {
		if (s->midi.ocnt < MIDIOUTBUF)
			mask |= POLLOUT | POLLWRNORM;
	}
	spin_unlock_irqrestore(&s->lock, flags);
	return mask;
}

static int solo1_midi_open(struct inode *inode, struct file *file)
{
	unsigned int minor = iminor(inode);
	DECLARE_WAITQUEUE(wait, current);
	unsigned long flags;
	struct solo1_state *s = NULL;
	struct pci_dev *pci_dev = NULL;

	while ((pci_dev = pci_find_device(PCI_ANY_ID, PCI_ANY_ID, pci_dev)) != NULL) {
		struct pci_driver *drvr;

		drvr = pci_dev_driver(pci_dev);
		if (drvr != &solo1_driver)
			continue;
		s = (struct solo1_state*)pci_get_drvdata(pci_dev);
		if (!s)
			continue;
		if (s->dev_midi == minor)
			break;
	}
	if (!s)
		return -ENODEV;
       	VALIDATE_STATE(s);
	file->private_data = s;
	/* wait for device to become free */
	down(&s->open_sem);
	while (s->open_mode & (file->f_mode << FMODE_MIDI_SHIFT)) {
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
	if (!(s->open_mode & (FMODE_MIDI_READ | FMODE_MIDI_WRITE))) {
		s->midi.ird = s->midi.iwr = s->midi.icnt = 0;
		s->midi.ord = s->midi.owr = s->midi.ocnt = 0;
		outb(0xff, s->mpubase+1); /* reset command */
		outb(0x3f, s->mpubase+1); /* uart command */
		if (!(inb(s->mpubase+1) & 0x80))
			inb(s->mpubase);
		s->midi.ird = s->midi.iwr = s->midi.icnt = 0;
		outb(0xb0, s->iobase + 7); /* enable A1, A2, MPU irq's */
		init_timer(&s->midi.timer);
		s->midi.timer.expires = jiffies+1;
		s->midi.timer.data = (unsigned long)s;
		s->midi.timer.function = solo1_midi_timer;
		add_timer(&s->midi.timer);
	}
	if (file->f_mode & FMODE_READ) {
		s->midi.ird = s->midi.iwr = s->midi.icnt = 0;
	}
	if (file->f_mode & FMODE_WRITE) {
		s->midi.ord = s->midi.owr = s->midi.ocnt = 0;
	}
	spin_unlock_irqrestore(&s->lock, flags);
	s->open_mode |= (file->f_mode << FMODE_MIDI_SHIFT) & (FMODE_MIDI_READ | FMODE_MIDI_WRITE);
	up(&s->open_sem);
	return nonseekable_open(inode, file);
}

static int solo1_midi_release(struct inode *inode, struct file *file)
{
	struct solo1_state *s = (struct solo1_state *)file->private_data;
	DECLARE_WAITQUEUE(wait, current);
	unsigned long flags;
	unsigned count, tmo;

	VALIDATE_STATE(s);

	lock_kernel();
	if (file->f_mode & FMODE_WRITE) {
		add_wait_queue(&s->midi.owait, &wait);
		for (;;) {
			__set_current_state(TASK_INTERRUPTIBLE);
			spin_lock_irqsave(&s->lock, flags);
			count = s->midi.ocnt;
			spin_unlock_irqrestore(&s->lock, flags);
			if (count <= 0)
				break;
			if (signal_pending(current))
				break;
			if (file->f_flags & O_NONBLOCK)
				break;
			tmo = (count * HZ) / 3100;
			if (!schedule_timeout(tmo ? : 1) && tmo)
				printk(KERN_DEBUG "solo1: midi timed out??\n");
		}
		remove_wait_queue(&s->midi.owait, &wait);
		set_current_state(TASK_RUNNING);
	}
	down(&s->open_sem);
	s->open_mode &= ~((file->f_mode << FMODE_MIDI_SHIFT) & (FMODE_MIDI_READ|FMODE_MIDI_WRITE));
	spin_lock_irqsave(&s->lock, flags);
	if (!(s->open_mode & (FMODE_MIDI_READ | FMODE_MIDI_WRITE))) {
		outb(0x30, s->iobase + 7); /* enable A1, A2 irq's */
		del_timer(&s->midi.timer);		
	}
	spin_unlock_irqrestore(&s->lock, flags);
	wake_up(&s->open_wait);
	up(&s->open_sem);
	unlock_kernel();
	return 0;
}

static /*const*/ struct file_operations solo1_midi_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.read		= solo1_midi_read,
	.write		= solo1_midi_write,
	.poll		= solo1_midi_poll,
	.open		= solo1_midi_open,
	.release	= solo1_midi_release,
};

/* --------------------------------------------------------------------- */

static int solo1_dmfm_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	static const unsigned char op_offset[18] = {
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
		0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D,
		0x10, 0x11, 0x12, 0x13, 0x14, 0x15
	};
	struct solo1_state *s = (struct solo1_state *)file->private_data;
	struct dm_fm_voice v;
	struct dm_fm_note n;
	struct dm_fm_params p;
	unsigned int io;
	unsigned int regb;

	switch (cmd) {		
	case FM_IOCTL_RESET:
		for (regb = 0xb0; regb < 0xb9; regb++) {
			outb(regb, s->sbbase);
			outb(0, s->sbbase+1);
			outb(regb, s->sbbase+2);
			outb(0, s->sbbase+3);
		}
		return 0;

	case FM_IOCTL_PLAY_NOTE:
		if (copy_from_user(&n, (void __user *)arg, sizeof(n)))
			return -EFAULT;
		if (n.voice >= 18)
			return -EINVAL;
		if (n.voice >= 9) {
			regb = n.voice - 9;
			io = s->sbbase+2;
		} else {
			regb = n.voice;
			io = s->sbbase;
		}
		outb(0xa0 + regb, io);
		outb(n.fnum & 0xff, io+1);
		outb(0xb0 + regb, io);
		outb(((n.fnum >> 8) & 3) | ((n.octave & 7) << 2) | ((n.key_on & 1) << 5), io+1);
		return 0;

	case FM_IOCTL_SET_VOICE:
		if (copy_from_user(&v, (void __user *)arg, sizeof(v)))
			return -EFAULT;
		if (v.voice >= 18)
			return -EINVAL;
		regb = op_offset[v.voice];
		io = s->sbbase + ((v.op & 1) << 1);
		outb(0x20 + regb, io);
		outb(((v.am & 1) << 7) | ((v.vibrato & 1) << 6) | ((v.do_sustain & 1) << 5) | 
		     ((v.kbd_scale & 1) << 4) | (v.harmonic & 0xf), io+1);
		outb(0x40 + regb, io);
		outb(((v.scale_level & 0x3) << 6) | (v.volume & 0x3f), io+1);
		outb(0x60 + regb, io);
		outb(((v.attack & 0xf) << 4) | (v.decay & 0xf), io+1);
		outb(0x80 + regb, io);
		outb(((v.sustain & 0xf) << 4) | (v.release & 0xf), io+1);
		outb(0xe0 + regb, io);
		outb(v.waveform & 0x7, io+1);
		if (n.voice >= 9) {
			regb = n.voice - 9;
			io = s->sbbase+2;
		} else {
			regb = n.voice;
			io = s->sbbase;
		}
		outb(0xc0 + regb, io);
		outb(((v.right & 1) << 5) | ((v.left & 1) << 4) | ((v.feedback & 7) << 1) |
		     (v.connection & 1), io+1);
		return 0;
		
	case FM_IOCTL_SET_PARAMS:
		if (copy_from_user(&p, (void __user *)arg, sizeof(p)))
			return -EFAULT;
		outb(0x08, s->sbbase);
		outb((p.kbd_split & 1) << 6, s->sbbase+1);
		outb(0xbd, s->sbbase);
		outb(((p.am_depth & 1) << 7) | ((p.vib_depth & 1) << 6) | ((p.rhythm & 1) << 5) | ((p.bass & 1) << 4) |
		     ((p.snare & 1) << 3) | ((p.tomtom & 1) << 2) | ((p.cymbal & 1) << 1) | (p.hihat & 1), s->sbbase+1);
		return 0;

	case FM_IOCTL_SET_OPL:
		outb(4, s->sbbase+2);
		outb(arg, s->sbbase+3);
		return 0;

	case FM_IOCTL_SET_MODE:
		outb(5, s->sbbase+2);
		outb(arg & 1, s->sbbase+3);
		return 0;

	default:
		return -EINVAL;
	}
}

static int solo1_dmfm_open(struct inode *inode, struct file *file)
{
	unsigned int minor = iminor(inode);
	DECLARE_WAITQUEUE(wait, current);
	struct solo1_state *s = NULL;
	struct pci_dev *pci_dev = NULL;

	while ((pci_dev = pci_find_device(PCI_ANY_ID, PCI_ANY_ID, pci_dev)) != NULL) {
		struct pci_driver *drvr;

		drvr = pci_dev_driver(pci_dev);
		if (drvr != &solo1_driver)
			continue;
		s = (struct solo1_state*)pci_get_drvdata(pci_dev);
		if (!s)
			continue;
		if (s->dev_dmfm == minor)
			break;
	}
	if (!s)
		return -ENODEV;
       	VALIDATE_STATE(s);
	file->private_data = s;
	/* wait for device to become free */
	down(&s->open_sem);
	while (s->open_mode & FMODE_DMFM) {
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
	if (!request_region(s->sbbase, FMSYNTH_EXTENT, "ESS Solo1")) {
		up(&s->open_sem);
		printk(KERN_ERR "solo1: FM synth io ports in use, opl3 loaded?\n");
		return -EBUSY;
	}
	/* init the stuff */
	outb(1, s->sbbase);
	outb(0x20, s->sbbase+1); /* enable waveforms */
	outb(4, s->sbbase+2);
	outb(0, s->sbbase+3);  /* no 4op enabled */
	outb(5, s->sbbase+2);
	outb(1, s->sbbase+3);  /* enable OPL3 */
	s->open_mode |= FMODE_DMFM;
	up(&s->open_sem);
	return nonseekable_open(inode, file);
}

static int solo1_dmfm_release(struct inode *inode, struct file *file)
{
	struct solo1_state *s = (struct solo1_state *)file->private_data;
	unsigned int regb;

	VALIDATE_STATE(s);
	lock_kernel();
	down(&s->open_sem);
	s->open_mode &= ~FMODE_DMFM;
	for (regb = 0xb0; regb < 0xb9; regb++) {
		outb(regb, s->sbbase);
		outb(0, s->sbbase+1);
		outb(regb, s->sbbase+2);
		outb(0, s->sbbase+3);
	}
	release_region(s->sbbase, FMSYNTH_EXTENT);
	wake_up(&s->open_wait);
	up(&s->open_sem);
	unlock_kernel();
	return 0;
}

static /*const*/ struct file_operations solo1_dmfm_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.ioctl		= solo1_dmfm_ioctl,
	.open		= solo1_dmfm_open,
	.release	= solo1_dmfm_release,
};

/* --------------------------------------------------------------------- */

static struct initvol {
	int mixch;
	int vol;
} initvol[] __devinitdata = {
	{ SOUND_MIXER_WRITE_VOLUME, 0x4040 },
	{ SOUND_MIXER_WRITE_PCM, 0x4040 },
	{ SOUND_MIXER_WRITE_SYNTH, 0x4040 },
	{ SOUND_MIXER_WRITE_CD, 0x4040 },
	{ SOUND_MIXER_WRITE_LINE, 0x4040 },
	{ SOUND_MIXER_WRITE_LINE1, 0x4040 },
	{ SOUND_MIXER_WRITE_LINE2, 0x4040 },
	{ SOUND_MIXER_WRITE_RECLEV, 0x4040 },
	{ SOUND_MIXER_WRITE_SPEAKER, 0x4040 },
	{ SOUND_MIXER_WRITE_MIC, 0x4040 }
};

static int setup_solo1(struct solo1_state *s)
{
	struct pci_dev *pcidev = s->dev;
	mm_segment_t fs;
	int i, val;

	/* initialize DDMA base address */
	printk(KERN_DEBUG "solo1: ddma base address: 0x%lx\n", s->ddmabase);
	pci_write_config_word(pcidev, 0x60, (s->ddmabase & (~0xf)) | 1);
	/* set DMA policy to DDMA, IRQ emulation off (CLKRUN disabled for now) */
	pci_write_config_dword(pcidev, 0x50, 0);
	/* disable legacy audio address decode */
	pci_write_config_word(pcidev, 0x40, 0x907f);

	/* initialize the chips */
	if (!reset_ctrl(s)) {
		printk(KERN_ERR "esssolo1: cannot reset controller\n");
		return -1;
	}
	outb(0xb0, s->iobase+7); /* enable A1, A2, MPU irq's */
	
	/* initialize mixer regs */
	write_mixer(s, 0x7f, 0); /* disable music digital recording */
	write_mixer(s, 0x7d, 0x0c); /* enable mic preamp, MONO_OUT is 2nd DAC right channel */
	write_mixer(s, 0x64, 0x45); /* volume control */
	write_mixer(s, 0x48, 0x10); /* enable music DAC/ES6xx interface */
	write_mixer(s, 0x50, 0);  /* disable spatializer */
	write_mixer(s, 0x52, 0);
	write_mixer(s, 0x14, 0);  /* DAC1 minimum volume */
	write_mixer(s, 0x71, 0x20); /* enable new 0xA1 reg format */
	outb(0, s->ddmabase+0xd); /* DMA master clear */
	outb(1, s->ddmabase+0xf); /* mask channel */
	/*outb(0, s->ddmabase+0x8);*/ /* enable controller (enable is low active!!) */

	pci_set_master(pcidev);  /* enable bus mastering */
	
	fs = get_fs();
	set_fs(KERNEL_DS);
	val = SOUND_MASK_LINE;
	mixer_ioctl(s, SOUND_MIXER_WRITE_RECSRC, (unsigned long)&val);
	for (i = 0; i < sizeof(initvol)/sizeof(initvol[0]); i++) {
		val = initvol[i].vol;
		mixer_ioctl(s, initvol[i].mixch, (unsigned long)&val);
	}
	val = 1; /* enable mic preamp */
	mixer_ioctl(s, SOUND_MIXER_PRIVATE1, (unsigned long)&val);
	set_fs(fs);
	return 0;
}

static int
solo1_suspend(struct pci_dev *pci_dev, pm_message_t state) {
	struct solo1_state *s = (struct solo1_state*)pci_get_drvdata(pci_dev);
	if (!s)
		return 1;
	outb(0, s->iobase+6);
	/* DMA master clear */
	outb(0, s->ddmabase+0xd); 
	/* reset sequencer and FIFO */
	outb(3, s->sbbase+6); 
	/* turn off DDMA controller address space */
	pci_write_config_word(s->dev, 0x60, 0); 
	return 0;
}

static int
solo1_resume(struct pci_dev *pci_dev) {
	struct solo1_state *s = (struct solo1_state*)pci_get_drvdata(pci_dev);
	if (!s)
		return 1;
	setup_solo1(s);
	return 0;
}

#ifdef SUPPORT_JOYSTICK
static int __devinit solo1_register_gameport(struct solo1_state *s, int io_port)
{
	struct gameport *gp;

	if (!request_region(io_port, GAMEPORT_EXTENT, "ESS Solo1")) {
		printk(KERN_ERR "solo1: gameport io ports are in use\n");
		return -EBUSY;
	}

	s->gameport = gp = gameport_allocate_port();
	if (!gp) {
		printk(KERN_ERR "solo1: can not allocate memory for gameport\n");
		release_region(io_port, GAMEPORT_EXTENT);
		return -ENOMEM;
	}

	gameport_set_name(gp, "ESS Solo1 Gameport");
	gameport_set_phys(gp, "isa%04x/gameport0", io_port);
	gp->dev.parent = &s->dev->dev;
	gp->io = io_port;

	gameport_register_port(gp);

	return 0;
}

static inline void solo1_unregister_gameport(struct solo1_state *s)
{
	if (s->gameport) {
		int gpio = s->gameport->io;
		gameport_unregister_port(s->gameport);
		release_region(gpio, GAMEPORT_EXTENT);
	}
}
#else
static inline int solo1_register_gameport(struct solo1_state *s, int io_port) { return -ENOSYS; }
static inline void solo1_unregister_gameport(struct solo1_state *s) { }
#endif /* SUPPORT_JOYSTICK */

static int __devinit solo1_probe(struct pci_dev *pcidev, const struct pci_device_id *pciid)
{
	struct solo1_state *s;
	int gpio;
	int ret;

 	if ((ret=pci_enable_device(pcidev)))
		return ret;
	if (!(pci_resource_flags(pcidev, 0) & IORESOURCE_IO) ||
	    !(pci_resource_flags(pcidev, 1) & IORESOURCE_IO) ||
	    !(pci_resource_flags(pcidev, 2) & IORESOURCE_IO) ||
	    !(pci_resource_flags(pcidev, 3) & IORESOURCE_IO))
		return -ENODEV;
	if (pcidev->irq == 0)
		return -ENODEV;

	/* Recording requires 24-bit DMA, so attempt to set dma mask
	 * to 24 bits first, then 32 bits (playback only) if that fails.
	 */
	if (pci_set_dma_mask(pcidev, 0x00ffffff) &&
	    pci_set_dma_mask(pcidev, DMA_32BIT_MASK)) {
		printk(KERN_WARNING "solo1: architecture does not support 24bit or 32bit PCI busmaster DMA\n");
		return -ENODEV;
	}

	if (!(s = kmalloc(sizeof(struct solo1_state), GFP_KERNEL))) {
		printk(KERN_WARNING "solo1: out of memory\n");
		return -ENOMEM;
	}
	memset(s, 0, sizeof(struct solo1_state));
	init_waitqueue_head(&s->dma_adc.wait);
	init_waitqueue_head(&s->dma_dac.wait);
	init_waitqueue_head(&s->open_wait);
	init_waitqueue_head(&s->midi.iwait);
	init_waitqueue_head(&s->midi.owait);
	init_MUTEX(&s->open_sem);
	spin_lock_init(&s->lock);
	s->magic = SOLO1_MAGIC;
	s->dev = pcidev;
	s->iobase = pci_resource_start(pcidev, 0);
	s->sbbase = pci_resource_start(pcidev, 1);
	s->vcbase = pci_resource_start(pcidev, 2);
	s->ddmabase = s->vcbase + DDMABASE_OFFSET;
	s->mpubase = pci_resource_start(pcidev, 3);
	gpio = pci_resource_start(pcidev, 4);
	s->irq = pcidev->irq;
	ret = -EBUSY;
	if (!request_region(s->iobase, IOBASE_EXTENT, "ESS Solo1")) {
		printk(KERN_ERR "solo1: io ports in use\n");
		goto err_region1;
	}
	if (!request_region(s->sbbase+FMSYNTH_EXTENT, SBBASE_EXTENT-FMSYNTH_EXTENT, "ESS Solo1")) {
		printk(KERN_ERR "solo1: io ports in use\n");
		goto err_region2;
	}
	if (!request_region(s->ddmabase, DDMABASE_EXTENT, "ESS Solo1")) {
		printk(KERN_ERR "solo1: io ports in use\n");
		goto err_region3;
	}
	if (!request_region(s->mpubase, MPUBASE_EXTENT, "ESS Solo1")) {
		printk(KERN_ERR "solo1: io ports in use\n");
		goto err_region4;
	}
	if ((ret=request_irq(s->irq,solo1_interrupt,SA_SHIRQ,"ESS Solo1",s))) {
		printk(KERN_ERR "solo1: irq %u in use\n", s->irq);
		goto err_irq;
	}
	/* register devices */
	if ((s->dev_audio = register_sound_dsp(&solo1_audio_fops, -1)) < 0) {
		ret = s->dev_audio;
		goto err_dev1;
	}
	if ((s->dev_mixer = register_sound_mixer(&solo1_mixer_fops, -1)) < 0) {
		ret = s->dev_mixer;
		goto err_dev2;
	}
	if ((s->dev_midi = register_sound_midi(&solo1_midi_fops, -1)) < 0) {
		ret = s->dev_midi;
		goto err_dev3;
	}
	if ((s->dev_dmfm = register_sound_special(&solo1_dmfm_fops, 15 /* ?? */)) < 0) {
		ret = s->dev_dmfm;
		goto err_dev4;
	}
	if (setup_solo1(s)) {
		ret = -EIO;
		goto err;
	}
	/* register gameport */
	solo1_register_gameport(s, gpio);
	/* store it in the driver field */
	pci_set_drvdata(pcidev, s);
	return 0;

 err:
	unregister_sound_special(s->dev_dmfm);
 err_dev4:
	unregister_sound_midi(s->dev_midi);
 err_dev3:
	unregister_sound_mixer(s->dev_mixer);
 err_dev2:
	unregister_sound_dsp(s->dev_audio);
 err_dev1:
	printk(KERN_ERR "solo1: initialisation error\n");
	free_irq(s->irq, s);
 err_irq:
	release_region(s->mpubase, MPUBASE_EXTENT);
 err_region4:
	release_region(s->ddmabase, DDMABASE_EXTENT);
 err_region3:
	release_region(s->sbbase+FMSYNTH_EXTENT, SBBASE_EXTENT-FMSYNTH_EXTENT);
 err_region2:
	release_region(s->iobase, IOBASE_EXTENT);
 err_region1:
	kfree(s);
	return ret;
}

static void __devexit solo1_remove(struct pci_dev *dev)
{
	struct solo1_state *s = pci_get_drvdata(dev);
	
	if (!s)
		return;
	/* stop DMA controller */
	outb(0, s->iobase+6);
	outb(0, s->ddmabase+0xd); /* DMA master clear */
	outb(3, s->sbbase+6); /* reset sequencer and FIFO */
	synchronize_irq(s->irq);
	pci_write_config_word(s->dev, 0x60, 0); /* turn off DDMA controller address space */
	free_irq(s->irq, s);
	solo1_unregister_gameport(s);
	release_region(s->iobase, IOBASE_EXTENT);
	release_region(s->sbbase+FMSYNTH_EXTENT, SBBASE_EXTENT-FMSYNTH_EXTENT);
	release_region(s->ddmabase, DDMABASE_EXTENT);
	release_region(s->mpubase, MPUBASE_EXTENT);
	unregister_sound_dsp(s->dev_audio);
	unregister_sound_mixer(s->dev_mixer);
	unregister_sound_midi(s->dev_midi);
	unregister_sound_special(s->dev_dmfm);
	kfree(s);
	pci_set_drvdata(dev, NULL);
}

static struct pci_device_id id_table[] = {
	{ PCI_VENDOR_ID_ESS, PCI_DEVICE_ID_ESS_SOLO1, PCI_ANY_ID, PCI_ANY_ID, 0, 0 },
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, id_table);

static struct pci_driver solo1_driver = {
	.name		= "ESS Solo1",
	.id_table	= id_table,
	.probe		= solo1_probe,
	.remove		= __devexit_p(solo1_remove),
	.suspend	= solo1_suspend,
	.resume		= solo1_resume,
};


static int __init init_solo1(void)
{
	printk(KERN_INFO "solo1: version v0.20 time " __TIME__ " " __DATE__ "\n");
	return pci_register_driver(&solo1_driver);
}

/* --------------------------------------------------------------------- */

MODULE_AUTHOR("Thomas M. Sailer, sailer@ife.ee.ethz.ch, hb9jnx@hb9w.che.eu");
MODULE_DESCRIPTION("ESS Solo1 Driver");
MODULE_LICENSE("GPL");


static void __exit cleanup_solo1(void)
{
	printk(KERN_INFO "solo1: unloading\n");
	pci_unregister_driver(&solo1_driver);
}

/* --------------------------------------------------------------------- */

module_init(init_solo1);
module_exit(cleanup_solo1);

