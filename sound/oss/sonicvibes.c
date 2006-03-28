/*****************************************************************************/

/*
 *      sonicvibes.c  --  S3 Sonic Vibes audio driver.
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
 * Special thanks to David C. Niemi
 *
 *
 * Module command line parameters:
 *   none so far
 *
 *
 *  Supported devices:
 *  /dev/dsp    standard /dev/dsp device, (mostly) OSS compatible
 *  /dev/mixer  standard /dev/mixer device, (mostly) OSS compatible
 *  /dev/midi   simple MIDI UART interface, no ioctl
 *
 *  The card has both an FM and a Wavetable synth, but I have to figure
 *  out first how to drive them...
 *
 *  Revision history
 *    06.05.1998   0.1   Initial release
 *    10.05.1998   0.2   Fixed many bugs, esp. ADC rate calculation
 *                       First stab at a simple midi interface (no bells&whistles)
 *    13.05.1998   0.3   Fix stupid cut&paste error: set_adc_rate was called instead of
 *                       set_dac_rate in the FMODE_WRITE case in sv_open
 *                       Fix hwptr out of bounds (now mpg123 works)
 *    14.05.1998   0.4   Don't allow excessive interrupt rates
 *    08.06.1998   0.5   First release using Alan Cox' soundcore instead of miscdevice
 *    03.08.1998   0.6   Do not include modversions.h
 *                       Now mixer behaviour can basically be selected between
 *                       "OSS documented" and "OSS actual" behaviour
 *    31.08.1998   0.7   Fix realplayer problems - dac.count issues
 *    10.12.1998   0.8   Fix drain_dac trying to wait on not yet initialized DMA
 *    16.12.1998   0.9   Fix a few f_file & FMODE_ bugs
 *    06.01.1999   0.10  remove the silly SA_INTERRUPT flag.
 *                       hopefully killed the egcs section type conflict
 *    12.03.1999   0.11  cinfo.blocks should be reset after GETxPTR ioctl.
 *                       reported by Johan Maes <joma@telindus.be>
 *    22.03.1999   0.12  return EAGAIN instead of EBUSY when O_NONBLOCK
 *                       read/write cannot be executed
 *    05.04.1999   0.13  added code to sv_read and sv_write which should detect
 *                       lockups of the sound chip and revive it. This is basically
 *                       an ugly hack, but at least applications using this driver
 *                       won't hang forever. I don't know why these lockups happen,
 *                       it might well be the motherboard chipset (an early 486 PCI
 *                       board with ALI chipset), since every busmastering 100MB
 *                       ethernet card I've tried (Realtek 8139 and Macronix tulip clone)
 *                       exhibit similar behaviour (they work for a couple of packets
 *                       and then lock up and can be revived by ifconfig down/up).
 *    07.04.1999   0.14  implemented the following ioctl's: SOUND_PCM_READ_RATE, 
 *                       SOUND_PCM_READ_CHANNELS, SOUND_PCM_READ_BITS; 
 *                       Alpha fixes reported by Peter Jones <pjones@redhat.com>
 *                       Note: dmaio hack might still be wrong on archs other than i386
 *    15.06.1999   0.15  Fix bad allocation bug.
 *                       Thanks to Deti Fliegl <fliegl@in.tum.de>
 *    28.06.1999   0.16  Add pci_set_master
 *    03.08.1999   0.17  adapt to Linus' new __setup/__initcall
 *                       added kernel command line options "sonicvibes=reverb" and "sonicvibesdmaio=dmaioaddr"
 *    12.08.1999   0.18  module_init/__setup fixes
 *    24.08.1999   0.19  get rid of the dmaio kludge, replace with allocate_resource
 *    31.08.1999   0.20  add spin_lock_init
 *                       use new resource allocation to allocate DDMA IO space
 *                       replaced current->state = x with set_current_state(x)
 *    03.09.1999   0.21  change read semantics for MIDI to match
 *                       OSS more closely; remove possible wakeup race
 *    28.10.1999   0.22  More waitqueue races fixed
 *    01.12.1999   0.23  New argument to allocate_resource
 *    07.12.1999   0.24  More allocate_resource semantics change
 *    08.01.2000   0.25  Prevent some ioctl's from returning bad count values on underrun/overrun;
 *                       Tim Janik's BSE (Bedevilled Sound Engine) found this
 *                       use Martin Mares' pci_assign_resource
 *    07.02.2000   0.26  Use pci_alloc_consistent and pci_register_driver
 *    21.11.2000   0.27  Initialize dma buffers in poll, otherwise poll may return a bogus mask
 *    12.12.2000   0.28  More dma buffer initializations, patch from
 *                       Tjeerd Mulder <tjeerd.mulder@fujitsu-siemens.com>
 *    31.01.2001   0.29  Register/Unregister gameport
 *                       Fix SETTRIGGER non OSS API conformity
 *    18.05.2001   0.30  PCI probing and error values cleaned up by Marcus
 *                       Meissner <mm@caldera.de>
 *    03.01.2003   0.31  open_mode fixes from Georg Acher <acher@in.tum.de>
 *
 */

/*****************************************************************************/
      
#include <linux/module.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/sound.h>
#include <linux/slab.h>
#include <linux/soundcard.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/spinlock.h>
#include <linux/smp_lock.h>
#include <linux/gameport.h>
#include <linux/dma-mapping.h>
#include <linux/mutex.h>


#include <asm/io.h>
#include <asm/uaccess.h>

#include "dm.h"

#if defined(CONFIG_GAMEPORT) || (defined(MODULE) && defined(CONFIG_GAMEPORT_MODULE))
#define SUPPORT_JOYSTICK 1
#endif

/* --------------------------------------------------------------------- */

#undef OSS_DOCUMENTED_MIXER_SEMANTICS

/* --------------------------------------------------------------------- */

#ifndef PCI_VENDOR_ID_S3
#define PCI_VENDOR_ID_S3             0x5333
#endif
#ifndef PCI_DEVICE_ID_S3_SONICVIBES
#define PCI_DEVICE_ID_S3_SONICVIBES  0xca00
#endif

#define SV_MAGIC  ((PCI_VENDOR_ID_S3<<16)|PCI_DEVICE_ID_S3_SONICVIBES)

#define SV_EXTENT_SB      0x10
#define SV_EXTENT_ENH     0x10
#define SV_EXTENT_SYNTH   0x4
#define SV_EXTENT_MIDI    0x4
#define SV_EXTENT_GAME    0x8
#define SV_EXTENT_DMA     0x10

/*
 * we are not a bridge and thus use a resource for DDMA that is used for bridges but
 * left empty for normal devices
 */
#define RESOURCE_SB       0
#define RESOURCE_ENH      1
#define RESOURCE_SYNTH    2
#define RESOURCE_MIDI     3
#define RESOURCE_GAME     4
#define RESOURCE_DDMA     7

#define SV_MIDI_DATA      0
#define SV_MIDI_COMMAND   1
#define SV_MIDI_STATUS    1

#define SV_DMA_ADDR0      0
#define SV_DMA_ADDR1      1
#define SV_DMA_ADDR2      2
#define SV_DMA_ADDR3      3
#define SV_DMA_COUNT0     4
#define SV_DMA_COUNT1     5
#define SV_DMA_COUNT2     6
#define SV_DMA_MODE       0xb
#define SV_DMA_RESET      0xd
#define SV_DMA_MASK       0xf

/*
 * DONT reset the DMA controllers unless you understand
 * the reset semantics. Assuming reset semantics as in
 * the 8237 does not work.
 */

#define DMA_MODE_AUTOINIT 0x10
#define DMA_MODE_READ     0x44    /* I/O to memory, no autoinit, increment, single mode */
#define DMA_MODE_WRITE    0x48    /* memory to I/O, no autoinit, increment, single mode */

#define SV_CODEC_CONTROL  0
#define SV_CODEC_INTMASK  1
#define SV_CODEC_STATUS   2
#define SV_CODEC_IADDR    4
#define SV_CODEC_IDATA    5

#define SV_CCTRL_RESET      0x80
#define SV_CCTRL_INTADRIVE  0x20
#define SV_CCTRL_WAVETABLE  0x08
#define SV_CCTRL_REVERB     0x04
#define SV_CCTRL_ENHANCED   0x01

#define SV_CINTMASK_DMAA    0x01
#define SV_CINTMASK_DMAC    0x04
#define SV_CINTMASK_SPECIAL 0x08
#define SV_CINTMASK_UPDOWN  0x40
#define SV_CINTMASK_MIDI    0x80

#define SV_CSTAT_DMAA       0x01
#define SV_CSTAT_DMAC	    0x04
#define SV_CSTAT_SPECIAL    0x08
#define SV_CSTAT_UPDOWN	    0x40
#define SV_CSTAT_MIDI	    0x80

#define SV_CIADDR_TRD       0x80
#define SV_CIADDR_MCE       0x40

/* codec indirect registers */
#define SV_CIMIX_ADCINL     0x00
#define SV_CIMIX_ADCINR     0x01
#define SV_CIMIX_AUX1INL    0x02
#define SV_CIMIX_AUX1INR    0x03
#define SV_CIMIX_CDINL      0x04
#define SV_CIMIX_CDINR      0x05
#define SV_CIMIX_LINEINL    0x06
#define SV_CIMIX_LINEINR    0x07
#define SV_CIMIX_MICIN      0x08
#define SV_CIMIX_SYNTHINL   0x0A
#define SV_CIMIX_SYNTHINR   0x0B
#define SV_CIMIX_AUX2INL    0x0C
#define SV_CIMIX_AUX2INR    0x0D
#define SV_CIMIX_ANALOGINL  0x0E
#define SV_CIMIX_ANALOGINR  0x0F
#define SV_CIMIX_PCMINL     0x10
#define SV_CIMIX_PCMINR     0x11

#define SV_CIGAMECONTROL    0x09
#define SV_CIDATAFMT        0x12
#define SV_CIENABLE         0x13
#define SV_CIUPDOWN         0x14
#define SV_CIREVISION       0x15
#define SV_CIADCOUTPUT      0x16
#define SV_CIDMAABASECOUNT1 0x18
#define SV_CIDMAABASECOUNT0 0x19
#define SV_CIDMACBASECOUNT1 0x1c
#define SV_CIDMACBASECOUNT0 0x1d
#define SV_CIPCMSR0         0x1e
#define SV_CIPCMSR1         0x1f
#define SV_CISYNTHSR0       0x20
#define SV_CISYNTHSR1       0x21
#define SV_CIADCCLKSOURCE   0x22
#define SV_CIADCALTSR       0x23
#define SV_CIADCPLLM        0x24
#define SV_CIADCPLLN        0x25
#define SV_CISYNTHPLLM      0x26
#define SV_CISYNTHPLLN      0x27
#define SV_CIUARTCONTROL    0x2a
#define SV_CIDRIVECONTROL   0x2b
#define SV_CISRSSPACE       0x2c
#define SV_CISRSCENTER      0x2d
#define SV_CIWAVETABLESRC   0x2e
#define SV_CIANALOGPWRDOWN  0x30
#define SV_CIDIGITALPWRDOWN 0x31


#define SV_CIMIX_ADCSRC_CD     0x20
#define SV_CIMIX_ADCSRC_DAC    0x40
#define SV_CIMIX_ADCSRC_AUX2   0x60
#define SV_CIMIX_ADCSRC_LINE   0x80
#define SV_CIMIX_ADCSRC_AUX1   0xa0
#define SV_CIMIX_ADCSRC_MIC    0xc0
#define SV_CIMIX_ADCSRC_MIXOUT 0xe0
#define SV_CIMIX_ADCSRC_MASK   0xe0

#define SV_CFMT_STEREO     0x01
#define SV_CFMT_16BIT      0x02
#define SV_CFMT_MASK       0x03
#define SV_CFMT_ASHIFT     0   
#define SV_CFMT_CSHIFT     4

static const unsigned sample_size[] = { 1, 2, 2, 4 };
static const unsigned sample_shift[] = { 0, 1, 1, 2 };

#define SV_CENABLE_PPE     0x4
#define SV_CENABLE_RE      0x2
#define SV_CENABLE_PE      0x1


/* MIDI buffer sizes */

#define MIDIINBUF  256
#define MIDIOUTBUF 256

#define FMODE_MIDI_SHIFT 2
#define FMODE_MIDI_READ  (FMODE_READ << FMODE_MIDI_SHIFT)
#define FMODE_MIDI_WRITE (FMODE_WRITE << FMODE_MIDI_SHIFT)

#define FMODE_DMFM 0x10

/* --------------------------------------------------------------------- */

struct sv_state {
	/* magic */
	unsigned int magic;

	/* list of sonicvibes devices */
	struct list_head devs;

	/* the corresponding pci_dev structure */
	struct pci_dev *dev;

	/* soundcore stuff */
	int dev_audio;
	int dev_mixer;
	int dev_midi;
	int dev_dmfm;

	/* hardware resources */
	unsigned long iosb, ioenh, iosynth, iomidi;  /* long for SPARC */
	unsigned int iodmaa, iodmac, irq;

        /* mixer stuff */
        struct {
                unsigned int modcnt;
#ifndef OSS_DOCUMENTED_MIXER_SEMANTICS
		unsigned short vol[13];
#endif /* OSS_DOCUMENTED_MIXER_SEMANTICS */
        } mix;

	/* wave stuff */
	unsigned int rateadc, ratedac;
	unsigned char fmt, enable;

	spinlock_t lock;
	struct mutex open_mutex;
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

static LIST_HEAD(devs);
static unsigned long wavetable_mem;

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

/*
 * Why use byte IO? Nobody knows, but S3 does it also in their Windows driver.
 */

#undef DMABYTEIO

static void set_dmaa(struct sv_state *s, unsigned int addr, unsigned int count)
{
#ifdef DMABYTEIO
	unsigned io = s->iodmaa, u;

	count--;
	for (u = 4; u > 0; u--, addr >>= 8, io++)
		outb(addr & 0xff, io);
	for (u = 3; u > 0; u--, count >>= 8, io++)
		outb(count & 0xff, io);
#else /* DMABYTEIO */
	count--;
	outl(addr, s->iodmaa + SV_DMA_ADDR0);
	outl(count, s->iodmaa + SV_DMA_COUNT0);
#endif /* DMABYTEIO */
	outb(0x18, s->iodmaa + SV_DMA_MODE);
}

static void set_dmac(struct sv_state *s, unsigned int addr, unsigned int count)
{
#ifdef DMABYTEIO
	unsigned io = s->iodmac, u;

	count >>= 1;
	count--;
	for (u = 4; u > 0; u--, addr >>= 8, io++)
		outb(addr & 0xff, io);
	for (u = 3; u > 0; u--, count >>= 8, io++)
		outb(count & 0xff, io);
#else /* DMABYTEIO */
	count >>= 1;
	count--;
	outl(addr, s->iodmac + SV_DMA_ADDR0);
	outl(count, s->iodmac + SV_DMA_COUNT0);
#endif /* DMABYTEIO */
	outb(0x14, s->iodmac + SV_DMA_MODE);
}

static inline unsigned get_dmaa(struct sv_state *s)
{
#ifdef DMABYTEIO
	unsigned io = s->iodmaa+6, v = 0, u;

	for (u = 3; u > 0; u--, io--) {
		v <<= 8;
		v |= inb(io);
	}
	return v + 1;
#else /* DMABYTEIO */
	return (inl(s->iodmaa + SV_DMA_COUNT0) & 0xffffff) + 1;
#endif /* DMABYTEIO */
}

static inline unsigned get_dmac(struct sv_state *s)
{
#ifdef DMABYTEIO
	unsigned io = s->iodmac+6, v = 0, u;

	for (u = 3; u > 0; u--, io--) {
		v <<= 8;
		v |= inb(io);
	}
	return (v + 1) << 1;
#else /* DMABYTEIO */
	return ((inl(s->iodmac + SV_DMA_COUNT0) & 0xffffff) + 1) << 1;
#endif /* DMABYTEIO */
}

static void wrindir(struct sv_state *s, unsigned char idx, unsigned char data)
{
	outb(idx & 0x3f, s->ioenh + SV_CODEC_IADDR);
	udelay(10);
	outb(data, s->ioenh + SV_CODEC_IDATA);
	udelay(10);
}

static unsigned char rdindir(struct sv_state *s, unsigned char idx)
{
	unsigned char v;

	outb(idx & 0x3f, s->ioenh + SV_CODEC_IADDR);
	udelay(10);
	v = inb(s->ioenh + SV_CODEC_IDATA);
	udelay(10);
	return v;
}

static void set_fmt(struct sv_state *s, unsigned char mask, unsigned char data)
{
	unsigned long flags;

	spin_lock_irqsave(&s->lock, flags);
	outb(SV_CIDATAFMT | SV_CIADDR_MCE, s->ioenh + SV_CODEC_IADDR);
	if (mask) {
		s->fmt = inb(s->ioenh + SV_CODEC_IDATA);
		udelay(10);
	}
	s->fmt = (s->fmt & mask) | data;
	outb(s->fmt, s->ioenh + SV_CODEC_IDATA);
	udelay(10);
	outb(0, s->ioenh + SV_CODEC_IADDR);
	spin_unlock_irqrestore(&s->lock, flags);
	udelay(10);
}

static void frobindir(struct sv_state *s, unsigned char idx, unsigned char mask, unsigned char data)
{
	outb(idx & 0x3f, s->ioenh + SV_CODEC_IADDR);
	udelay(10);
	outb((inb(s->ioenh + SV_CODEC_IDATA) & mask) ^ data, s->ioenh + SV_CODEC_IDATA);
	udelay(10);
}

#define REFFREQUENCY  24576000
#define ADCMULT 512
#define FULLRATE 48000

static unsigned setpll(struct sv_state *s, unsigned char reg, unsigned rate)
{
	unsigned long flags;
	unsigned char r, m=0, n=0;
	unsigned xm, xn, xr, xd, metric = ~0U;
	/* the warnings about m and n used uninitialized are bogus and may safely be ignored */

	if (rate < 625000/ADCMULT)
		rate = 625000/ADCMULT;
	if (rate > 150000000/ADCMULT)
		rate = 150000000/ADCMULT;
	/* slight violation of specs, needed for continuous sampling rates */
	for (r = 0; rate < 75000000/ADCMULT; r += 0x20, rate <<= 1);
	for (xn = 3; xn < 35; xn++)
		for (xm = 3; xm < 130; xm++) {
			xr = REFFREQUENCY/ADCMULT * xm / xn;
			xd = abs((signed)(xr - rate));
			if (xd < metric) {
				metric = xd;
				m = xm - 2;
				n = xn - 2;
			}
		}
	reg &= 0x3f;
	spin_lock_irqsave(&s->lock, flags);
	outb(reg, s->ioenh + SV_CODEC_IADDR);
	udelay(10);
	outb(m, s->ioenh + SV_CODEC_IDATA);
	udelay(10);
	outb(reg+1, s->ioenh + SV_CODEC_IADDR);
	udelay(10);
	outb(r | n, s->ioenh + SV_CODEC_IDATA);
	spin_unlock_irqrestore(&s->lock, flags);
	udelay(10);
	return (REFFREQUENCY/ADCMULT * (m + 2) / (n + 2)) >> ((r >> 5) & 7);
}

#if 0

static unsigned getpll(struct sv_state *s, unsigned char reg)
{
	unsigned long flags;
	unsigned char m, n;

	reg &= 0x3f;
	spin_lock_irqsave(&s->lock, flags);
	outb(reg, s->ioenh + SV_CODEC_IADDR);
	udelay(10);
	m = inb(s->ioenh + SV_CODEC_IDATA);
	udelay(10);
	outb(reg+1, s->ioenh + SV_CODEC_IADDR);
	udelay(10);
	n = inb(s->ioenh + SV_CODEC_IDATA);
	spin_unlock_irqrestore(&s->lock, flags);
	udelay(10);
	return (REFFREQUENCY/ADCMULT * (m + 2) / ((n & 0x1f) + 2)) >> ((n >> 5) & 7);
}

#endif

static void set_dac_rate(struct sv_state *s, unsigned rate)
{
	unsigned div;
	unsigned long flags;

	if (rate > 48000)
		rate = 48000;
	if (rate < 4000)
		rate = 4000;
	div = (rate * 65536 + FULLRATE/2) / FULLRATE;
	if (div > 65535)
		div = 65535;
	spin_lock_irqsave(&s->lock, flags);
	wrindir(s, SV_CIPCMSR1, div >> 8);
	wrindir(s, SV_CIPCMSR0, div);
	spin_unlock_irqrestore(&s->lock, flags);
	s->ratedac = (div * FULLRATE + 32768) / 65536;
}

static void set_adc_rate(struct sv_state *s, unsigned rate)
{
	unsigned long flags;
	unsigned rate1, rate2, div;

	if (rate > 48000)
		rate = 48000;
	if (rate < 4000)
		rate = 4000;
	rate1 = setpll(s, SV_CIADCPLLM, rate);
	div = (48000 + rate/2) / rate;
	if (div > 8)
		div = 8;
	rate2 = (48000 + div/2) / div;
	spin_lock_irqsave(&s->lock, flags);
	wrindir(s, SV_CIADCALTSR, (div-1) << 4);
	if (abs((signed)(rate-rate2)) <= abs((signed)(rate-rate1))) {
		wrindir(s, SV_CIADCCLKSOURCE, 0x10);
		s->rateadc = rate2;
	} else {
		wrindir(s, SV_CIADCCLKSOURCE, 0x00);
		s->rateadc = rate1;
	}
	spin_unlock_irqrestore(&s->lock, flags);
}

/* --------------------------------------------------------------------- */

static inline void stop_adc(struct sv_state *s)
{
	unsigned long flags;

	spin_lock_irqsave(&s->lock, flags);
	s->enable &= ~SV_CENABLE_RE;
	wrindir(s, SV_CIENABLE, s->enable);
	spin_unlock_irqrestore(&s->lock, flags);
}	

static inline void stop_dac(struct sv_state *s)
{
	unsigned long flags;

	spin_lock_irqsave(&s->lock, flags);
	s->enable &= ~(SV_CENABLE_PPE | SV_CENABLE_PE);
	wrindir(s, SV_CIENABLE, s->enable);
	spin_unlock_irqrestore(&s->lock, flags);
}	

static void start_dac(struct sv_state *s)
{
	unsigned long flags;

	spin_lock_irqsave(&s->lock, flags);
	if ((s->dma_dac.mapped || s->dma_dac.count > 0) && s->dma_dac.ready) {
		s->enable = (s->enable & ~SV_CENABLE_PPE) | SV_CENABLE_PE;
		wrindir(s, SV_CIENABLE, s->enable);
	}
	spin_unlock_irqrestore(&s->lock, flags);
}	

static void start_adc(struct sv_state *s)
{
	unsigned long flags;

	spin_lock_irqsave(&s->lock, flags);
	if ((s->dma_adc.mapped || s->dma_adc.count < (signed)(s->dma_adc.dmasize - 2*s->dma_adc.fragsize)) 
	    && s->dma_adc.ready) {
		s->enable |= SV_CENABLE_RE;
		wrindir(s, SV_CIENABLE, s->enable);
	}
	spin_unlock_irqrestore(&s->lock, flags);
}	

/* --------------------------------------------------------------------- */

#define DMABUF_DEFAULTORDER (17-PAGE_SHIFT)
#define DMABUF_MINORDER 1

static void dealloc_dmabuf(struct sv_state *s, struct dmabuf *db)
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


/* DMAA is used for playback, DMAC is used for recording */

static int prog_dmabuf(struct sv_state *s, unsigned rec)
{
	struct dmabuf *db = rec ? &s->dma_adc : &s->dma_dac;
	unsigned rate = rec ? s->rateadc : s->ratedac;
	int order;
	unsigned bytepersec;
	unsigned bufs;
	struct page *page, *pend;
	unsigned char fmt;
	unsigned long flags;

	spin_lock_irqsave(&s->lock, flags);
	fmt = s->fmt;
	if (rec) {
		s->enable &= ~SV_CENABLE_RE;
		fmt >>= SV_CFMT_CSHIFT;
	} else {
		s->enable &= ~SV_CENABLE_PE;
		fmt >>= SV_CFMT_ASHIFT;
	}
	wrindir(s, SV_CIENABLE, s->enable);
	spin_unlock_irqrestore(&s->lock, flags);
	fmt &= SV_CFMT_MASK;
	db->hwptr = db->swptr = db->total_bytes = db->count = db->error = db->endcleared = 0;
	if (!db->rawbuf) {
		db->ready = db->mapped = 0;
		for (order = DMABUF_DEFAULTORDER; order >= DMABUF_MINORDER; order--)
			if ((db->rawbuf = pci_alloc_consistent(s->dev, PAGE_SIZE << order, &db->dmaaddr)))
				break;
		if (!db->rawbuf)
			return -ENOMEM;
		db->buforder = order;
		if ((virt_to_bus(db->rawbuf) ^ (virt_to_bus(db->rawbuf) + (PAGE_SIZE << db->buforder) - 1)) & ~0xffff)
			printk(KERN_DEBUG "sv: DMA buffer crosses 64k boundary: busaddr 0x%lx  size %ld\n", 
			       virt_to_bus(db->rawbuf), PAGE_SIZE << db->buforder);
		if ((virt_to_bus(db->rawbuf) + (PAGE_SIZE << db->buforder) - 1) & ~0xffffff)
			printk(KERN_DEBUG "sv: DMA buffer beyond 16MB: busaddr 0x%lx  size %ld\n", 
			       virt_to_bus(db->rawbuf), PAGE_SIZE << db->buforder);
		/* now mark the pages as reserved; otherwise remap_pfn_range doesn't do what we want */
		pend = virt_to_page(db->rawbuf + (PAGE_SIZE << db->buforder) - 1);
		for (page = virt_to_page(db->rawbuf); page <= pend; page++)
			SetPageReserved(page);
	}
	bytepersec = rate << sample_shift[fmt];
	bufs = PAGE_SIZE << db->buforder;
	if (db->ossfragshift) {
		if ((1000 << db->ossfragshift) < bytepersec)
			db->fragshift = ld2(bytepersec/1000);
		else
			db->fragshift = db->ossfragshift;
	} else {
		db->fragshift = ld2(bytepersec/100/(db->subdivision ? db->subdivision : 1));
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
	db->fragsamples = db->fragsize >> sample_shift[fmt];
	db->dmasize = db->numfrag << db->fragshift;
	memset(db->rawbuf, (fmt & SV_CFMT_16BIT) ? 0 : 0x80, db->dmasize);
	spin_lock_irqsave(&s->lock, flags);
	if (rec) {
		set_dmac(s, db->dmaaddr, db->numfrag << db->fragshift);
		/* program enhanced mode registers */
		wrindir(s, SV_CIDMACBASECOUNT1, (db->fragsamples-1) >> 8);
		wrindir(s, SV_CIDMACBASECOUNT0, db->fragsamples-1);
	} else {
		set_dmaa(s, db->dmaaddr, db->numfrag << db->fragshift);
		/* program enhanced mode registers */
		wrindir(s, SV_CIDMAABASECOUNT1, (db->fragsamples-1) >> 8);
		wrindir(s, SV_CIDMAABASECOUNT0, db->fragsamples-1);
	}
	spin_unlock_irqrestore(&s->lock, flags);
	db->enabled = 1;
	db->ready = 1;
	return 0;
}

static inline void clear_advance(struct sv_state *s)
{
	unsigned char c = (s->fmt & (SV_CFMT_16BIT << SV_CFMT_ASHIFT)) ? 0 : 0x80;
	unsigned char *buf = s->dma_dac.rawbuf;
	unsigned bsize = s->dma_dac.dmasize;
	unsigned bptr = s->dma_dac.swptr;
	unsigned len = s->dma_dac.fragsize;

	if (bptr + len > bsize) {
		unsigned x = bsize - bptr;
		memset(buf + bptr, c, x);
		bptr = 0;
		len -= x;
	}
	memset(buf + bptr, c, len);
}

/* call with spinlock held! */
static void sv_update_ptr(struct sv_state *s)
{
	unsigned hwptr;
	int diff;

	/* update ADC pointer */
	if (s->dma_adc.ready) {
		hwptr = (s->dma_adc.dmasize - get_dmac(s)) % s->dma_adc.dmasize;
		diff = (s->dma_adc.dmasize + hwptr - s->dma_adc.hwptr) % s->dma_adc.dmasize;
		s->dma_adc.hwptr = hwptr;
		s->dma_adc.total_bytes += diff;
		s->dma_adc.count += diff;
		if (s->dma_adc.count >= (signed)s->dma_adc.fragsize) 
			wake_up(&s->dma_adc.wait);
		if (!s->dma_adc.mapped) {
			if (s->dma_adc.count > (signed)(s->dma_adc.dmasize - ((3 * s->dma_adc.fragsize) >> 1))) {
				s->enable &= ~SV_CENABLE_RE;
				wrindir(s, SV_CIENABLE, s->enable);
				s->dma_adc.error++;
			}
		}
	}
	/* update DAC pointer */
	if (s->dma_dac.ready) {
		hwptr = (s->dma_dac.dmasize - get_dmaa(s)) % s->dma_dac.dmasize;
		diff = (s->dma_dac.dmasize + hwptr - s->dma_dac.hwptr) % s->dma_dac.dmasize;
		s->dma_dac.hwptr = hwptr;
		s->dma_dac.total_bytes += diff;
		if (s->dma_dac.mapped) {
			s->dma_dac.count += diff;
			if (s->dma_dac.count >= (signed)s->dma_dac.fragsize)
				wake_up(&s->dma_dac.wait);
		} else {
			s->dma_dac.count -= diff;
			if (s->dma_dac.count <= 0) {
				s->enable &= ~SV_CENABLE_PE;
				wrindir(s, SV_CIENABLE, s->enable);
				s->dma_dac.error++;
			} else if (s->dma_dac.count <= (signed)s->dma_dac.fragsize && !s->dma_dac.endcleared) {
				clear_advance(s);
				s->dma_dac.endcleared = 1;
			}
			if (s->dma_dac.count + (signed)s->dma_dac.fragsize <= (signed)s->dma_dac.dmasize)
				wake_up(&s->dma_dac.wait);
		}
	}
}

/* hold spinlock for the following! */
static void sv_handle_midi(struct sv_state *s)
{
	unsigned char ch;
	int wake;

	wake = 0;
	while (!(inb(s->iomidi+1) & 0x80)) {
		ch = inb(s->iomidi);
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
	while (!(inb(s->iomidi+1) & 0x40) && s->midi.ocnt > 0) {
		outb(s->midi.obuf[s->midi.ord], s->iomidi);
		s->midi.ord = (s->midi.ord + 1) % MIDIOUTBUF;
		s->midi.ocnt--;
		if (s->midi.ocnt < MIDIOUTBUF-16)
			wake = 1;
	}
	if (wake)
		wake_up(&s->midi.owait);
}

static irqreturn_t sv_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
        struct sv_state *s = (struct sv_state *)dev_id;
	unsigned int intsrc;
	
	/* fastpath out, to ease interrupt sharing */
	intsrc = inb(s->ioenh + SV_CODEC_STATUS);
	if (!(intsrc & (SV_CSTAT_DMAA | SV_CSTAT_DMAC | SV_CSTAT_MIDI)))
		return IRQ_NONE;
	spin_lock(&s->lock);
	sv_update_ptr(s);
	sv_handle_midi(s);
	spin_unlock(&s->lock);
	return IRQ_HANDLED;
}

static void sv_midi_timer(unsigned long data)
{
	struct sv_state *s = (struct sv_state *)data;
	unsigned long flags;
	
	spin_lock_irqsave(&s->lock, flags);
	sv_handle_midi(s);
	spin_unlock_irqrestore(&s->lock, flags);
	s->midi.timer.expires = jiffies+1;
	add_timer(&s->midi.timer);
}

/* --------------------------------------------------------------------- */

static const char invalid_magic[] = KERN_CRIT "sv: invalid magic value\n";

#define VALIDATE_STATE(s)                         \
({                                                \
	if (!(s) || (s)->magic != SV_MAGIC) { \
		printk(invalid_magic);            \
		return -ENXIO;                    \
	}                                         \
})

/* --------------------------------------------------------------------- */

#define MT_4          1
#define MT_5MUTE      2
#define MT_4MUTEMONO  3
#define MT_6MUTE      4

static const struct {
	unsigned left:5;
	unsigned right:5;
	unsigned type:3;
	unsigned rec:3;
} mixtable[SOUND_MIXER_NRDEVICES] = {
	[SOUND_MIXER_RECLEV] = { SV_CIMIX_ADCINL,    SV_CIMIX_ADCINR,    MT_4,         0 },
	[SOUND_MIXER_LINE1]  = { SV_CIMIX_AUX1INL,   SV_CIMIX_AUX1INR,   MT_5MUTE,     5 },
	[SOUND_MIXER_CD]     = { SV_CIMIX_CDINL,     SV_CIMIX_CDINR,     MT_5MUTE,     1 },
	[SOUND_MIXER_LINE]   = { SV_CIMIX_LINEINL,   SV_CIMIX_LINEINR,   MT_5MUTE,     4 },
	[SOUND_MIXER_MIC]    = { SV_CIMIX_MICIN,     SV_CIMIX_ADCINL,    MT_4MUTEMONO, 6 },
	[SOUND_MIXER_SYNTH]  = { SV_CIMIX_SYNTHINL,  SV_CIMIX_SYNTHINR,  MT_5MUTE,     2 },
	[SOUND_MIXER_LINE2]  = { SV_CIMIX_AUX2INL,   SV_CIMIX_AUX2INR,   MT_5MUTE,     3 },
	[SOUND_MIXER_VOLUME] = { SV_CIMIX_ANALOGINL, SV_CIMIX_ANALOGINR, MT_5MUTE,     7 },
	[SOUND_MIXER_PCM]    = { SV_CIMIX_PCMINL,    SV_CIMIX_PCMINR,    MT_6MUTE,     0 }
};

#ifdef OSS_DOCUMENTED_MIXER_SEMANTICS

static int return_mixval(struct sv_state *s, unsigned i, int *arg)
{
	unsigned long flags;
	unsigned char l, r, rl, rr;

	spin_lock_irqsave(&s->lock, flags);
	l = rdindir(s, mixtable[i].left);
	r = rdindir(s, mixtable[i].right);
	spin_unlock_irqrestore(&s->lock, flags);
	switch (mixtable[i].type) {
	case MT_4:
		r &= 0xf;
		l &= 0xf;
		rl = 10 + 6 * (l & 15);
		rr = 10 + 6 * (r & 15);
		break;

	case MT_4MUTEMONO:
		rl = 55 - 3 * (l & 15);
		if (r & 0x10)
			rl += 45;
		rr = rl;
		r = l;
		break;

	case MT_5MUTE:
	default:
		rl = 100 - 3 * (l & 31);
		rr = 100 - 3 * (r & 31);
		break;
				
	case MT_6MUTE:
		rl = 100 - 3 * (l & 63) / 2;
		rr = 100 - 3 * (r & 63) / 2;
		break;
	}
	if (l & 0x80)
		rl = 0;
	if (r & 0x80)
		rr = 0;
	return put_user((rr << 8) | rl, arg);
}

#else /* OSS_DOCUMENTED_MIXER_SEMANTICS */

static const unsigned char volidx[SOUND_MIXER_NRDEVICES] = 
{
	[SOUND_MIXER_RECLEV] = 1,
	[SOUND_MIXER_LINE1]  = 2,
	[SOUND_MIXER_CD]     = 3,
	[SOUND_MIXER_LINE]   = 4,
	[SOUND_MIXER_MIC]    = 5,
	[SOUND_MIXER_SYNTH]  = 6,
	[SOUND_MIXER_LINE2]  = 7,
	[SOUND_MIXER_VOLUME] = 8,
	[SOUND_MIXER_PCM]    = 9
};

#endif /* OSS_DOCUMENTED_MIXER_SEMANTICS */

static unsigned mixer_recmask(struct sv_state *s)
{
	unsigned long flags;
	int i, j;

	spin_lock_irqsave(&s->lock, flags);
	j = rdindir(s, SV_CIMIX_ADCINL) >> 5;
	spin_unlock_irqrestore(&s->lock, flags);
	j &= 7;
	for (i = 0; i < SOUND_MIXER_NRDEVICES && mixtable[i].rec != j; i++);
	return 1 << i;
}

static int mixer_ioctl(struct sv_state *s, unsigned int cmd, unsigned long arg)
{
	unsigned long flags;
	int i, val;
	unsigned char l, r, rl, rr;
	int __user *p = (int __user *)arg;

	VALIDATE_STATE(s);
        if (cmd == SOUND_MIXER_INFO) {
		mixer_info info;
		memset(&info, 0, sizeof(info));
		strlcpy(info.id, "SonicVibes", sizeof(info.id));
		strlcpy(info.name, "S3 SonicVibes", sizeof(info.name));
		info.modify_counter = s->mix.modcnt;
		if (copy_to_user((void __user *)arg, &info, sizeof(info)))
			return -EFAULT;
		return 0;
	}
	if (cmd == SOUND_OLD_MIXER_INFO) {
		_old_mixer_info info;
		memset(&info, 0, sizeof(info));
		strlcpy(info.id, "SonicVibes", sizeof(info.id));
		strlcpy(info.name, "S3 SonicVibes", sizeof(info.name));
		if (copy_to_user((void __user *)arg, &info, sizeof(info)))
			return -EFAULT;
		return 0;
	}
	if (cmd == OSS_GETVERSION)
		return put_user(SOUND_VERSION, p);
	if (cmd == SOUND_MIXER_PRIVATE1) {  /* SRS settings */
		if (get_user(val, p))
			return -EFAULT;
		spin_lock_irqsave(&s->lock, flags);
		if (val & 1) {
			if (val & 2) {
				l = 4 - ((val >> 2) & 7);
				if (l & ~3)
					l = 4;
				r = 4 - ((val >> 5) & 7);
				if (r & ~3)
					r = 4;
				wrindir(s, SV_CISRSSPACE, l);
				wrindir(s, SV_CISRSCENTER, r);
			} else
				wrindir(s, SV_CISRSSPACE, 0x80);
		}
		l = rdindir(s, SV_CISRSSPACE);
		r = rdindir(s, SV_CISRSCENTER);
		spin_unlock_irqrestore(&s->lock, flags);
		if (l & 0x80)
			return put_user(0, p);
		return put_user(((4 - (l & 7)) << 2) | ((4 - (r & 7)) << 5) | 2, p);
	}
	if (_IOC_TYPE(cmd) != 'M' || _SIOC_SIZE(cmd) != sizeof(int))
                return -EINVAL;
        if (_SIOC_DIR(cmd) == _SIOC_READ) {
                switch (_IOC_NR(cmd)) {
                case SOUND_MIXER_RECSRC: /* Arg contains a bit for each recording source */
			return put_user(mixer_recmask(s), p);
			
                case SOUND_MIXER_DEVMASK: /* Arg contains a bit for each supported device */
			for (val = i = 0; i < SOUND_MIXER_NRDEVICES; i++)
				if (mixtable[i].type)
					val |= 1 << i;
			return put_user(val, p);

                case SOUND_MIXER_RECMASK: /* Arg contains a bit for each supported recording source */
			for (val = i = 0; i < SOUND_MIXER_NRDEVICES; i++)
				if (mixtable[i].rec)
					val |= 1 << i;
			return put_user(val, p);
			
                case SOUND_MIXER_STEREODEVS: /* Mixer channels supporting stereo */
			for (val = i = 0; i < SOUND_MIXER_NRDEVICES; i++)
				if (mixtable[i].type && mixtable[i].type != MT_4MUTEMONO)
					val |= 1 << i;
			return put_user(val, p);
			
                case SOUND_MIXER_CAPS:
			return put_user(SOUND_CAP_EXCL_INPUT, p);

		default:
			i = _IOC_NR(cmd);
                        if (i >= SOUND_MIXER_NRDEVICES || !mixtable[i].type)
                                return -EINVAL;
#ifdef OSS_DOCUMENTED_MIXER_SEMANTICS
			return return_mixval(s, i, p);
#else /* OSS_DOCUMENTED_MIXER_SEMANTICS */
			if (!volidx[i])
				return -EINVAL;
			return put_user(s->mix.vol[volidx[i]-1], p);
#endif /* OSS_DOCUMENTED_MIXER_SEMANTICS */
		}
	}
        if (_SIOC_DIR(cmd) != (_SIOC_READ|_SIOC_WRITE)) 
		return -EINVAL;
	s->mix.modcnt++;
	switch (_IOC_NR(cmd)) {
	case SOUND_MIXER_RECSRC: /* Arg contains a bit for each recording source */
		if (get_user(val, p))
			return -EFAULT;
		i = hweight32(val);
		if (i == 0)
			return 0; /*val = mixer_recmask(s);*/
		else if (i > 1) 
			val &= ~mixer_recmask(s);
		for (i = 0; i < SOUND_MIXER_NRDEVICES; i++) {
			if (!(val & (1 << i)))
				continue;
			if (mixtable[i].rec)
				break;
		}
		if (i == SOUND_MIXER_NRDEVICES)
			return 0;
		spin_lock_irqsave(&s->lock, flags);
		frobindir(s, SV_CIMIX_ADCINL, 0x1f, mixtable[i].rec << 5);
		frobindir(s, SV_CIMIX_ADCINR, 0x1f, mixtable[i].rec << 5);
		spin_unlock_irqrestore(&s->lock, flags);
		return 0;

	default:
		i = _IOC_NR(cmd);
		if (i >= SOUND_MIXER_NRDEVICES || !mixtable[i].type)
			return -EINVAL;
		if (get_user(val, p))
			return -EFAULT;
		l = val & 0xff;
		r = (val >> 8) & 0xff;
		if (mixtable[i].type == MT_4MUTEMONO)
			l = (r + l) / 2;
		if (l > 100)
			l = 100;
		if (r > 100)
			r = 100;
		spin_lock_irqsave(&s->lock, flags);
		switch (mixtable[i].type) {
		case MT_4:
			if (l >= 10)
				l -= 10;
			if (r >= 10)
				r -= 10;
			frobindir(s, mixtable[i].left, 0xf0, l / 6);
			frobindir(s, mixtable[i].right, 0xf0, l / 6);
			break;

		case MT_4MUTEMONO:
			rr = 0;
			if (l < 10)
				rl = 0x80;
			else {
				if (l >= 55) {
					rr = 0x10;
					l -= 45;
				}
				rl = (55 - l) / 3;
			}
			wrindir(s, mixtable[i].left, rl);
			frobindir(s, mixtable[i].right, ~0x10, rr);
			break;
			
		case MT_5MUTE:
			if (l < 7)
				rl = 0x80;
			else
				rl = (100 - l) / 3;
			if (r < 7)
				rr = 0x80;
			else
				rr = (100 - r) / 3;
			wrindir(s, mixtable[i].left, rl);
			wrindir(s, mixtable[i].right, rr);
			break;
				
		case MT_6MUTE:
			if (l < 6)
				rl = 0x80;
			else
				rl = (100 - l) * 2 / 3;
			if (r < 6)
				rr = 0x80;
			else
				rr = (100 - r) * 2 / 3;
			wrindir(s, mixtable[i].left, rl);
			wrindir(s, mixtable[i].right, rr);
			break;
		}
		spin_unlock_irqrestore(&s->lock, flags);
#ifdef OSS_DOCUMENTED_MIXER_SEMANTICS
                return return_mixval(s, i, p);
#else /* OSS_DOCUMENTED_MIXER_SEMANTICS */
		if (!volidx[i])
			return -EINVAL;
		s->mix.vol[volidx[i]-1] = val;
		return put_user(s->mix.vol[volidx[i]-1], p);
#endif /* OSS_DOCUMENTED_MIXER_SEMANTICS */
	}
}

/* --------------------------------------------------------------------- */

static int sv_open_mixdev(struct inode *inode, struct file *file)
{
	int minor = iminor(inode);
	struct list_head *list;
	struct sv_state *s;

	for (list = devs.next; ; list = list->next) {
		if (list == &devs)
			return -ENODEV;
		s = list_entry(list, struct sv_state, devs);
		if (s->dev_mixer == minor)
			break;
	}
       	VALIDATE_STATE(s);
	file->private_data = s;
	return nonseekable_open(inode, file);
}

static int sv_release_mixdev(struct inode *inode, struct file *file)
{
	struct sv_state *s = (struct sv_state *)file->private_data;
	
	VALIDATE_STATE(s);
	return 0;
}

static int sv_ioctl_mixdev(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	return mixer_ioctl((struct sv_state *)file->private_data, cmd, arg);
}

static /*const*/ struct file_operations sv_mixer_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.ioctl		= sv_ioctl_mixdev,
	.open		= sv_open_mixdev,
	.release	= sv_release_mixdev,
};

/* --------------------------------------------------------------------- */

static int drain_dac(struct sv_state *s, int nonblock)
{
	DECLARE_WAITQUEUE(wait, current);
	unsigned long flags;
	int count, tmo;

	if (s->dma_dac.mapped || !s->dma_dac.ready)
		return 0;
        add_wait_queue(&s->dma_dac.wait, &wait);
        for (;;) {
		__set_current_state(TASK_INTERRUPTIBLE);
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
		tmo = 3 * HZ * (count + s->dma_dac.fragsize) / 2 / s->ratedac;
		tmo >>= sample_shift[(s->fmt >> SV_CFMT_ASHIFT) & SV_CFMT_MASK];
		if (!schedule_timeout(tmo + 1))
			printk(KERN_DEBUG "sv: dma timed out??\n");
        }
        remove_wait_queue(&s->dma_dac.wait, &wait);
        set_current_state(TASK_RUNNING);
        if (signal_pending(current))
                return -ERESTARTSYS;
        return 0;
}

/* --------------------------------------------------------------------- */

static ssize_t sv_read(struct file *file, char __user *buffer, size_t count, loff_t *ppos)
{
	struct sv_state *s = (struct sv_state *)file->private_data;
	DECLARE_WAITQUEUE(wait, current);
	ssize_t ret;
	unsigned long flags;
	unsigned swptr;
	int cnt;

	VALIDATE_STATE(s);
	if (s->dma_adc.mapped)
		return -ENXIO;
	if (!s->dma_adc.ready && (ret = prog_dmabuf(s, 1)))
		return ret;
	if (!access_ok(VERIFY_WRITE, buffer, count))
		return -EFAULT;
	ret = 0;
#if 0
	spin_lock_irqsave(&s->lock, flags);
	sv_update_ptr(s);
	spin_unlock_irqrestore(&s->lock, flags);
#endif
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
		if (cnt <= 0) {
			if (s->dma_adc.enabled)
				start_adc(s);
			if (file->f_flags & O_NONBLOCK) {
				if (!ret)
					ret = -EAGAIN;
				break;
			}
			if (!schedule_timeout(HZ)) {
				printk(KERN_DEBUG "sv: read: chip lockup? dmasz %u fragsz %u count %i hwptr %u swptr %u\n",
				       s->dma_adc.dmasize, s->dma_adc.fragsize, s->dma_adc.count, 
				       s->dma_adc.hwptr, s->dma_adc.swptr);
				stop_adc(s);
				spin_lock_irqsave(&s->lock, flags);
				set_dmac(s, virt_to_bus(s->dma_adc.rawbuf), s->dma_adc.numfrag << s->dma_adc.fragshift);
				/* program enhanced mode registers */
				wrindir(s, SV_CIDMACBASECOUNT1, (s->dma_adc.fragsamples-1) >> 8);
				wrindir(s, SV_CIDMACBASECOUNT0, s->dma_adc.fragsamples-1);
				s->dma_adc.count = s->dma_adc.hwptr = s->dma_adc.swptr = 0;
				spin_unlock_irqrestore(&s->lock, flags);
			}
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
	}
        remove_wait_queue(&s->dma_adc.wait, &wait);
	set_current_state(TASK_RUNNING);
	return ret;
}

static ssize_t sv_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
	struct sv_state *s = (struct sv_state *)file->private_data;
	DECLARE_WAITQUEUE(wait, current);
	ssize_t ret;
	unsigned long flags;
	unsigned swptr;
	int cnt;

	VALIDATE_STATE(s);
	if (s->dma_dac.mapped)
		return -ENXIO;
	if (!s->dma_dac.ready && (ret = prog_dmabuf(s, 0)))
		return ret;
	if (!access_ok(VERIFY_READ, buffer, count))
		return -EFAULT;
	ret = 0;
#if 0
	spin_lock_irqsave(&s->lock, flags);
	sv_update_ptr(s);
	spin_unlock_irqrestore(&s->lock, flags);
#endif
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
			if (!schedule_timeout(HZ)) {
				printk(KERN_DEBUG "sv: write: chip lockup? dmasz %u fragsz %u count %i hwptr %u swptr %u\n",
				       s->dma_dac.dmasize, s->dma_dac.fragsize, s->dma_dac.count, 
				       s->dma_dac.hwptr, s->dma_dac.swptr);
				stop_dac(s);
				spin_lock_irqsave(&s->lock, flags);
				set_dmaa(s, virt_to_bus(s->dma_dac.rawbuf), s->dma_dac.numfrag << s->dma_dac.fragshift);
				/* program enhanced mode registers */
				wrindir(s, SV_CIDMAABASECOUNT1, (s->dma_dac.fragsamples-1) >> 8);
				wrindir(s, SV_CIDMAABASECOUNT0, s->dma_dac.fragsamples-1);
				s->dma_dac.count = s->dma_dac.hwptr = s->dma_dac.swptr = 0;
				spin_unlock_irqrestore(&s->lock, flags);
			}
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
static unsigned int sv_poll(struct file *file, struct poll_table_struct *wait)
{
	struct sv_state *s = (struct sv_state *)file->private_data;
	unsigned long flags;
	unsigned int mask = 0;

	VALIDATE_STATE(s);
	if (file->f_mode & FMODE_WRITE) {
		if (!s->dma_dac.ready && prog_dmabuf(s, 1))
			return 0;
		poll_wait(file, &s->dma_dac.wait, wait);
	}
	if (file->f_mode & FMODE_READ) {
		if (!s->dma_adc.ready && prog_dmabuf(s, 0))
			return 0;
		poll_wait(file, &s->dma_adc.wait, wait);
	}
	spin_lock_irqsave(&s->lock, flags);
	sv_update_ptr(s);
	if (file->f_mode & FMODE_READ) {
		if (s->dma_adc.count >= (signed)s->dma_adc.fragsize)
			mask |= POLLIN | POLLRDNORM;
	}
	if (file->f_mode & FMODE_WRITE) {
		if (s->dma_dac.mapped) {
			if (s->dma_dac.count >= (signed)s->dma_dac.fragsize) 
				mask |= POLLOUT | POLLWRNORM;
		} else {
			if ((signed)s->dma_dac.dmasize >= s->dma_dac.count + (signed)s->dma_dac.fragsize)
				mask |= POLLOUT | POLLWRNORM;
		}
	}
	spin_unlock_irqrestore(&s->lock, flags);
	return mask;
}

static int sv_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct sv_state *s = (struct sv_state *)file->private_data;
	struct dmabuf *db;
	int ret = -EINVAL;
	unsigned long size;

	VALIDATE_STATE(s);
	lock_kernel();
	if (vma->vm_flags & VM_WRITE) {
		if ((ret = prog_dmabuf(s, 1)) != 0)
			goto out;
		db = &s->dma_dac;
	} else if (vma->vm_flags & VM_READ) {
		if ((ret = prog_dmabuf(s, 0)) != 0)
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

static int sv_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	struct sv_state *s = (struct sv_state *)file->private_data;
	unsigned long flags;
        audio_buf_info abinfo;
        count_info cinfo;
	int count;
	int val, mapped, ret;
	unsigned char fmtm, fmtd;
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
		return 0;

        case SNDCTL_DSP_SPEED:
                if (get_user(val, p))
			return -EFAULT;
		if (val >= 0) {
			if (file->f_mode & FMODE_READ) {
				stop_adc(s);
				s->dma_adc.ready = 0;
				set_adc_rate(s, val);
			}
			if (file->f_mode & FMODE_WRITE) {
				stop_dac(s);
				s->dma_dac.ready = 0;
				set_dac_rate(s, val);
			}
		}
		return put_user((file->f_mode & FMODE_READ) ? s->rateadc : s->ratedac, p);
		
        case SNDCTL_DSP_STEREO:
                if (get_user(val, p))
			return -EFAULT;
		fmtd = 0;
		fmtm = ~0;
		if (file->f_mode & FMODE_READ) {
			stop_adc(s);
			s->dma_adc.ready = 0;
			if (val)
				fmtd |= SV_CFMT_STEREO << SV_CFMT_CSHIFT;
			else
				fmtm &= ~(SV_CFMT_STEREO << SV_CFMT_CSHIFT);
		}
		if (file->f_mode & FMODE_WRITE) {
			stop_dac(s);
			s->dma_dac.ready = 0;
			if (val)
				fmtd |= SV_CFMT_STEREO << SV_CFMT_ASHIFT;
			else
				fmtm &= ~(SV_CFMT_STEREO << SV_CFMT_ASHIFT);
		}
		set_fmt(s, fmtm, fmtd);
		return 0;

        case SNDCTL_DSP_CHANNELS:
                if (get_user(val, p))
			return -EFAULT;
		if (val != 0) {
			fmtd = 0;
			fmtm = ~0;
			if (file->f_mode & FMODE_READ) {
				stop_adc(s);
				s->dma_adc.ready = 0;
				if (val >= 2)
					fmtd |= SV_CFMT_STEREO << SV_CFMT_CSHIFT;
				else
					fmtm &= ~(SV_CFMT_STEREO << SV_CFMT_CSHIFT);
			}
			if (file->f_mode & FMODE_WRITE) {
				stop_dac(s);
				s->dma_dac.ready = 0;
				if (val >= 2)
					fmtd |= SV_CFMT_STEREO << SV_CFMT_ASHIFT;
				else
					fmtm &= ~(SV_CFMT_STEREO << SV_CFMT_ASHIFT);
			}
			set_fmt(s, fmtm, fmtd);
		}
		return put_user((s->fmt & ((file->f_mode & FMODE_READ) ? (SV_CFMT_STEREO << SV_CFMT_CSHIFT) 
					   : (SV_CFMT_STEREO << SV_CFMT_ASHIFT))) ? 2 : 1, p);
		
	case SNDCTL_DSP_GETFMTS: /* Returns a mask */
                return put_user(AFMT_S16_LE|AFMT_U8, p);
		
	case SNDCTL_DSP_SETFMT: /* Selects ONE fmt*/
		if (get_user(val, p))
			return -EFAULT;
		if (val != AFMT_QUERY) {
			fmtd = 0;
			fmtm = ~0;
			if (file->f_mode & FMODE_READ) {
				stop_adc(s);
				s->dma_adc.ready = 0;
				if (val == AFMT_S16_LE)
					fmtd |= SV_CFMT_16BIT << SV_CFMT_CSHIFT;
				else
					fmtm &= ~(SV_CFMT_16BIT << SV_CFMT_CSHIFT);
			}
			if (file->f_mode & FMODE_WRITE) {
				stop_dac(s);
				s->dma_dac.ready = 0;
				if (val == AFMT_S16_LE)
					fmtd |= SV_CFMT_16BIT << SV_CFMT_ASHIFT;
				else
					fmtm &= ~(SV_CFMT_16BIT << SV_CFMT_ASHIFT);
			}
			set_fmt(s, fmtm, fmtd);
		}
		return put_user((s->fmt & ((file->f_mode & FMODE_READ) ? (SV_CFMT_16BIT << SV_CFMT_CSHIFT) 
					   : (SV_CFMT_16BIT << SV_CFMT_ASHIFT))) ? AFMT_S16_LE : AFMT_U8, p);
		
	case SNDCTL_DSP_POST:
                return 0;

        case SNDCTL_DSP_GETTRIGGER:
		val = 0;
		if (file->f_mode & FMODE_READ && s->enable & SV_CENABLE_RE) 
			val |= PCM_ENABLE_INPUT;
		if (file->f_mode & FMODE_WRITE && s->enable & SV_CENABLE_PE) 
			val |= PCM_ENABLE_OUTPUT;
		return put_user(val, p);
		
	case SNDCTL_DSP_SETTRIGGER:
		if (get_user(val, p))
			return -EFAULT;
		if (file->f_mode & FMODE_READ) {
			if (val & PCM_ENABLE_INPUT) {
				if (!s->dma_adc.ready && (ret =  prog_dmabuf(s, 1)))
					return ret;
				s->dma_adc.enabled = 1;
				start_adc(s);
			} else {
				s->dma_adc.enabled = 0;
				stop_adc(s);
			}
		}
		if (file->f_mode & FMODE_WRITE) {
			if (val & PCM_ENABLE_OUTPUT) {
				if (!s->dma_dac.ready && (ret = prog_dmabuf(s, 0)))
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
		if (!s->dma_dac.ready && (val = prog_dmabuf(s, 0)) != 0)
			return val;
		spin_lock_irqsave(&s->lock, flags);
		sv_update_ptr(s);
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
		if (!s->dma_adc.ready && (val = prog_dmabuf(s, 1)) != 0)
			return val;
		spin_lock_irqsave(&s->lock, flags);
		sv_update_ptr(s);
		abinfo.fragsize = s->dma_adc.fragsize;
		count = s->dma_adc.count;
		if (count < 0)
			count = 0;
                abinfo.bytes = count;
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
		if (!s->dma_dac.ready && (val = prog_dmabuf(s, 0)) != 0)
			return val;
		spin_lock_irqsave(&s->lock, flags);
		sv_update_ptr(s);
                count = s->dma_dac.count;
		spin_unlock_irqrestore(&s->lock, flags);
		if (count < 0)
			count = 0;
		return put_user(count, p);

        case SNDCTL_DSP_GETIPTR:
		if (!(file->f_mode & FMODE_READ))
			return -EINVAL;
		if (!s->dma_adc.ready && (val = prog_dmabuf(s, 1)) != 0)
			return val;
		spin_lock_irqsave(&s->lock, flags);
		sv_update_ptr(s);
                cinfo.bytes = s->dma_adc.total_bytes;
		count = s->dma_adc.count;
		if (count < 0)
			count = 0;
                cinfo.blocks = count >> s->dma_adc.fragshift;
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
		if (!s->dma_dac.ready && (val = prog_dmabuf(s, 0)) != 0)
			return val;
		spin_lock_irqsave(&s->lock, flags);
		sv_update_ptr(s);
                cinfo.bytes = s->dma_dac.total_bytes;
		count = s->dma_dac.count;
		if (count < 0)
			count = 0;
                cinfo.blocks = count >> s->dma_dac.fragshift;
                cinfo.ptr = s->dma_dac.hwptr;
		if (s->dma_dac.mapped)
			s->dma_dac.count &= s->dma_dac.fragsize-1;
		spin_unlock_irqrestore(&s->lock, flags);
                if (copy_to_user(argp, &cinfo, sizeof(cinfo)))
			return -EFAULT;
		return 0;

        case SNDCTL_DSP_GETBLKSIZE:
		if (file->f_mode & FMODE_WRITE) {
			if ((val = prog_dmabuf(s, 0)))
				return val;
			return put_user(s->dma_dac.fragsize, p);
		}
		if ((val = prog_dmabuf(s, 1)))
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
		return put_user((file->f_mode & FMODE_READ) ? s->rateadc : s->ratedac, p);

        case SOUND_PCM_READ_CHANNELS:
		return put_user((s->fmt & ((file->f_mode & FMODE_READ) ? (SV_CFMT_STEREO << SV_CFMT_CSHIFT) 
					   : (SV_CFMT_STEREO << SV_CFMT_ASHIFT))) ? 2 : 1, p);

        case SOUND_PCM_READ_BITS:
		return put_user((s->fmt & ((file->f_mode & FMODE_READ) ? (SV_CFMT_16BIT << SV_CFMT_CSHIFT) 
					   : (SV_CFMT_16BIT << SV_CFMT_ASHIFT))) ? 16 : 8, p);

        case SOUND_PCM_WRITE_FILTER:
        case SNDCTL_DSP_SETSYNCRO:
        case SOUND_PCM_READ_FILTER:
                return -EINVAL;
		
	}
	return mixer_ioctl(s, cmd, arg);
}

static int sv_open(struct inode *inode, struct file *file)
{
	int minor = iminor(inode);
	DECLARE_WAITQUEUE(wait, current);
	unsigned char fmtm = ~0, fmts = 0;
	struct list_head *list;
	struct sv_state *s;

	for (list = devs.next; ; list = list->next) {
		if (list == &devs)
			return -ENODEV;
		s = list_entry(list, struct sv_state, devs);
		if (!((s->dev_audio ^ minor) & ~0xf))
			break;
	}
       	VALIDATE_STATE(s);
	file->private_data = s;
	/* wait for device to become free */
	mutex_lock(&s->open_mutex);
	while (s->open_mode & file->f_mode) {
		if (file->f_flags & O_NONBLOCK) {
			mutex_unlock(&s->open_mutex);
			return -EBUSY;
		}
		add_wait_queue(&s->open_wait, &wait);
		__set_current_state(TASK_INTERRUPTIBLE);
		mutex_unlock(&s->open_mutex);
		schedule();
		remove_wait_queue(&s->open_wait, &wait);
		set_current_state(TASK_RUNNING);
		if (signal_pending(current))
			return -ERESTARTSYS;
		mutex_lock(&s->open_mutex);
	}
	if (file->f_mode & FMODE_READ) {
		fmtm &= ~((SV_CFMT_STEREO | SV_CFMT_16BIT) << SV_CFMT_CSHIFT);
		if ((minor & 0xf) == SND_DEV_DSP16)
			fmts |= SV_CFMT_16BIT << SV_CFMT_CSHIFT;
		s->dma_adc.ossfragshift = s->dma_adc.ossmaxfrags = s->dma_adc.subdivision = 0;
		s->dma_adc.enabled = 1;
		set_adc_rate(s, 8000);
	}
	if (file->f_mode & FMODE_WRITE) {
		fmtm &= ~((SV_CFMT_STEREO | SV_CFMT_16BIT) << SV_CFMT_ASHIFT);
		if ((minor & 0xf) == SND_DEV_DSP16)
			fmts |= SV_CFMT_16BIT << SV_CFMT_ASHIFT;
		s->dma_dac.ossfragshift = s->dma_dac.ossmaxfrags = s->dma_dac.subdivision = 0;
		s->dma_dac.enabled = 1;
		set_dac_rate(s, 8000);
	}
	set_fmt(s, fmtm, fmts);
	s->open_mode |= file->f_mode & (FMODE_READ | FMODE_WRITE);
	mutex_unlock(&s->open_mutex);
	return nonseekable_open(inode, file);
}

static int sv_release(struct inode *inode, struct file *file)
{
	struct sv_state *s = (struct sv_state *)file->private_data;

	VALIDATE_STATE(s);
	lock_kernel();
	if (file->f_mode & FMODE_WRITE)
		drain_dac(s, file->f_flags & O_NONBLOCK);
	mutex_lock(&s->open_mutex);
	if (file->f_mode & FMODE_WRITE) {
		stop_dac(s);
		dealloc_dmabuf(s, &s->dma_dac);
	}
	if (file->f_mode & FMODE_READ) {
		stop_adc(s);
		dealloc_dmabuf(s, &s->dma_adc);
	}
	s->open_mode &= ~(file->f_mode & (FMODE_READ|FMODE_WRITE));
	wake_up(&s->open_wait);
	mutex_unlock(&s->open_mutex);
	unlock_kernel();
	return 0;
}

static /*const*/ struct file_operations sv_audio_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.read		= sv_read,
	.write		= sv_write,
	.poll		= sv_poll,
	.ioctl		= sv_ioctl,
	.mmap		= sv_mmap,
	.open		= sv_open,
	.release	= sv_release,
};

/* --------------------------------------------------------------------- */

static ssize_t sv_midi_read(struct file *file, char __user *buffer, size_t count, loff_t *ppos)
{
	struct sv_state *s = (struct sv_state *)file->private_data;
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

static ssize_t sv_midi_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
	struct sv_state *s = (struct sv_state *)file->private_data;
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
			sv_handle_midi(s);
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
		sv_handle_midi(s);
		spin_unlock_irqrestore(&s->lock, flags);
	}
	__set_current_state(TASK_RUNNING);
	remove_wait_queue(&s->midi.owait, &wait);
	return ret;
}

/* No kernel lock - we have our own spinlock */
static unsigned int sv_midi_poll(struct file *file, struct poll_table_struct *wait)
{
	struct sv_state *s = (struct sv_state *)file->private_data;
	unsigned long flags;
	unsigned int mask = 0;

	VALIDATE_STATE(s);
	if (file->f_mode & FMODE_WRITE)
		poll_wait(file, &s->midi.owait, wait);
	if (file->f_mode & FMODE_READ)
		poll_wait(file, &s->midi.iwait, wait);
	spin_lock_irqsave(&s->lock, flags);
	if (file->f_mode & FMODE_READ) {
		if (s->midi.icnt > 0)
			mask |= POLLIN | POLLRDNORM;
	}
	if (file->f_mode & FMODE_WRITE) {
		if (s->midi.ocnt < MIDIOUTBUF)
			mask |= POLLOUT | POLLWRNORM;
	}
	spin_unlock_irqrestore(&s->lock, flags);
	return mask;
}

static int sv_midi_open(struct inode *inode, struct file *file)
{
	int minor = iminor(inode);
	DECLARE_WAITQUEUE(wait, current);
	unsigned long flags;
	struct list_head *list;
	struct sv_state *s;

	for (list = devs.next; ; list = list->next) {
		if (list == &devs)
			return -ENODEV;
		s = list_entry(list, struct sv_state, devs);
		if (s->dev_midi == minor)
			break;
	}
       	VALIDATE_STATE(s);
	file->private_data = s;
	/* wait for device to become free */
	mutex_lock(&s->open_mutex);
	while (s->open_mode & (file->f_mode << FMODE_MIDI_SHIFT)) {
		if (file->f_flags & O_NONBLOCK) {
			mutex_unlock(&s->open_mutex);
			return -EBUSY;
		}
		add_wait_queue(&s->open_wait, &wait);
		__set_current_state(TASK_INTERRUPTIBLE);
		mutex_unlock(&s->open_mutex);
		schedule();
		remove_wait_queue(&s->open_wait, &wait);
		set_current_state(TASK_RUNNING);
		if (signal_pending(current))
			return -ERESTARTSYS;
		mutex_lock(&s->open_mutex);
	}
	spin_lock_irqsave(&s->lock, flags);
	if (!(s->open_mode & (FMODE_MIDI_READ | FMODE_MIDI_WRITE))) {
		s->midi.ird = s->midi.iwr = s->midi.icnt = 0;
		s->midi.ord = s->midi.owr = s->midi.ocnt = 0;
		//outb(inb(s->ioenh + SV_CODEC_CONTROL) | SV_CCTRL_WAVETABLE, s->ioenh + SV_CODEC_CONTROL);
		outb(inb(s->ioenh + SV_CODEC_INTMASK) | SV_CINTMASK_MIDI, s->ioenh + SV_CODEC_INTMASK);
		wrindir(s, SV_CIUARTCONTROL, 5); /* output MIDI data to external and internal synth */
		wrindir(s, SV_CIWAVETABLESRC, 1); /* Wavetable in PC RAM */
		outb(0xff, s->iomidi+1); /* reset command */
		outb(0x3f, s->iomidi+1); /* uart command */
		if (!(inb(s->iomidi+1) & 0x80))
			inb(s->iomidi);
		s->midi.ird = s->midi.iwr = s->midi.icnt = 0;
		init_timer(&s->midi.timer);
		s->midi.timer.expires = jiffies+1;
		s->midi.timer.data = (unsigned long)s;
		s->midi.timer.function = sv_midi_timer;
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
	mutex_unlock(&s->open_mutex);
	return nonseekable_open(inode, file);
}

static int sv_midi_release(struct inode *inode, struct file *file)
{
	struct sv_state *s = (struct sv_state *)file->private_data;
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
			if (file->f_flags & O_NONBLOCK) {
				remove_wait_queue(&s->midi.owait, &wait);
				set_current_state(TASK_RUNNING);
				unlock_kernel();
				return -EBUSY;
			}
			tmo = (count * HZ) / 3100;
			if (!schedule_timeout(tmo ? : 1) && tmo)
				printk(KERN_DEBUG "sv: midi timed out??\n");
		}
		remove_wait_queue(&s->midi.owait, &wait);
		set_current_state(TASK_RUNNING);
	}
	mutex_lock(&s->open_mutex);
	s->open_mode &= ~((file->f_mode << FMODE_MIDI_SHIFT) & (FMODE_MIDI_READ|FMODE_MIDI_WRITE));
	spin_lock_irqsave(&s->lock, flags);
	if (!(s->open_mode & (FMODE_MIDI_READ | FMODE_MIDI_WRITE))) {
		outb(inb(s->ioenh + SV_CODEC_INTMASK) & ~SV_CINTMASK_MIDI, s->ioenh + SV_CODEC_INTMASK);
		del_timer(&s->midi.timer);		
	}
	spin_unlock_irqrestore(&s->lock, flags);
	wake_up(&s->open_wait);
	mutex_unlock(&s->open_mutex);
	unlock_kernel();
	return 0;
}

static /*const*/ struct file_operations sv_midi_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.read		= sv_midi_read,
	.write		= sv_midi_write,
	.poll		= sv_midi_poll,
	.open		= sv_midi_open,
	.release	= sv_midi_release,
};

/* --------------------------------------------------------------------- */

static int sv_dmfm_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	static const unsigned char op_offset[18] = {
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
		0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D,
		0x10, 0x11, 0x12, 0x13, 0x14, 0x15
	};
	struct sv_state *s = (struct sv_state *)file->private_data;
	struct dm_fm_voice v;
	struct dm_fm_note n;
	struct dm_fm_params p;
	unsigned int io;
	unsigned int regb;

	switch (cmd) {		
	case FM_IOCTL_RESET:
		for (regb = 0xb0; regb < 0xb9; regb++) {
			outb(regb, s->iosynth);
			outb(0, s->iosynth+1);
			outb(regb, s->iosynth+2);
			outb(0, s->iosynth+3);
		}
		return 0;

	case FM_IOCTL_PLAY_NOTE:
		if (copy_from_user(&n, (void __user *)arg, sizeof(n)))
			return -EFAULT;
		if (n.voice >= 18)
			return -EINVAL;
		if (n.voice >= 9) {
			regb = n.voice - 9;
			io = s->iosynth+2;
		} else {
			regb = n.voice;
			io = s->iosynth;
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
		io = s->iosynth + ((v.op & 1) << 1);
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
			io = s->iosynth+2;
		} else {
			regb = n.voice;
			io = s->iosynth;
		}
		outb(0xc0 + regb, io);
		outb(((v.right & 1) << 5) | ((v.left & 1) << 4) | ((v.feedback & 7) << 1) |
		     (v.connection & 1), io+1);
		return 0;
		
	case FM_IOCTL_SET_PARAMS:
		if (copy_from_user(&p, (void *__user )arg, sizeof(p)))
			return -EFAULT;
		outb(0x08, s->iosynth);
		outb((p.kbd_split & 1) << 6, s->iosynth+1);
		outb(0xbd, s->iosynth);
		outb(((p.am_depth & 1) << 7) | ((p.vib_depth & 1) << 6) | ((p.rhythm & 1) << 5) | ((p.bass & 1) << 4) |
		     ((p.snare & 1) << 3) | ((p.tomtom & 1) << 2) | ((p.cymbal & 1) << 1) | (p.hihat & 1), s->iosynth+1);
		return 0;

	case FM_IOCTL_SET_OPL:
		outb(4, s->iosynth+2);
		outb(arg, s->iosynth+3);
		return 0;

	case FM_IOCTL_SET_MODE:
		outb(5, s->iosynth+2);
		outb(arg & 1, s->iosynth+3);
		return 0;

	default:
		return -EINVAL;
	}
}

static int sv_dmfm_open(struct inode *inode, struct file *file)
{
	int minor = iminor(inode);
	DECLARE_WAITQUEUE(wait, current);
	struct list_head *list;
	struct sv_state *s;

	for (list = devs.next; ; list = list->next) {
		if (list == &devs)
			return -ENODEV;
		s = list_entry(list, struct sv_state, devs);
		if (s->dev_dmfm == minor)
			break;
	}
       	VALIDATE_STATE(s);
	file->private_data = s;
	/* wait for device to become free */
	mutex_lock(&s->open_mutex);
	while (s->open_mode & FMODE_DMFM) {
		if (file->f_flags & O_NONBLOCK) {
			mutex_unlock(&s->open_mutex);
			return -EBUSY;
		}
		add_wait_queue(&s->open_wait, &wait);
		__set_current_state(TASK_INTERRUPTIBLE);
		mutex_unlock(&s->open_mutex);
		schedule();
		remove_wait_queue(&s->open_wait, &wait);
		set_current_state(TASK_RUNNING);
		if (signal_pending(current))
			return -ERESTARTSYS;
		mutex_lock(&s->open_mutex);
	}
	/* init the stuff */
	outb(1, s->iosynth);
	outb(0x20, s->iosynth+1); /* enable waveforms */
	outb(4, s->iosynth+2);
	outb(0, s->iosynth+3);  /* no 4op enabled */
	outb(5, s->iosynth+2);
	outb(1, s->iosynth+3);  /* enable OPL3 */
	s->open_mode |= FMODE_DMFM;
	mutex_unlock(&s->open_mutex);
	return nonseekable_open(inode, file);
}

static int sv_dmfm_release(struct inode *inode, struct file *file)
{
	struct sv_state *s = (struct sv_state *)file->private_data;
	unsigned int regb;

	VALIDATE_STATE(s);
	lock_kernel();
	mutex_lock(&s->open_mutex);
	s->open_mode &= ~FMODE_DMFM;
	for (regb = 0xb0; regb < 0xb9; regb++) {
		outb(regb, s->iosynth);
		outb(0, s->iosynth+1);
		outb(regb, s->iosynth+2);
		outb(0, s->iosynth+3);
	}
	wake_up(&s->open_wait);
	mutex_unlock(&s->open_mutex);
	unlock_kernel();
	return 0;
}

static /*const*/ struct file_operations sv_dmfm_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.ioctl		= sv_dmfm_ioctl,
	.open		= sv_dmfm_open,
	.release	= sv_dmfm_release,
};

/* --------------------------------------------------------------------- */

/* maximum number of devices; only used for command line params */
#define NR_DEVICE 5

static int reverb[NR_DEVICE];

#if 0
static int wavetable[NR_DEVICE];
#endif

static unsigned int devindex;

module_param_array(reverb, bool, NULL, 0);
MODULE_PARM_DESC(reverb, "if 1 enables the reverb circuitry. NOTE: your card must have the reverb RAM");
#if 0
MODULE_PARM(wavetable, "1-" __MODULE_STRING(NR_DEVICE) "i");
MODULE_PARM_DESC(wavetable, "if 1 the wavetable synth is enabled");
#endif

MODULE_AUTHOR("Thomas M. Sailer, sailer@ife.ee.ethz.ch, hb9jnx@hb9w.che.eu");
MODULE_DESCRIPTION("S3 SonicVibes Driver");
MODULE_LICENSE("GPL");


/* --------------------------------------------------------------------- */

static struct initvol {
	int mixch;
	int vol;
} initvol[] __devinitdata = {
	{ SOUND_MIXER_WRITE_RECLEV, 0x4040 },
	{ SOUND_MIXER_WRITE_LINE1, 0x4040 },
	{ SOUND_MIXER_WRITE_CD, 0x4040 },
	{ SOUND_MIXER_WRITE_LINE, 0x4040 },
	{ SOUND_MIXER_WRITE_MIC, 0x4040 },
	{ SOUND_MIXER_WRITE_SYNTH, 0x4040 },
	{ SOUND_MIXER_WRITE_LINE2, 0x4040 },
	{ SOUND_MIXER_WRITE_VOLUME, 0x4040 },
	{ SOUND_MIXER_WRITE_PCM, 0x4040 }
};

#define RSRCISIOREGION(dev,num) (pci_resource_start((dev), (num)) != 0 && \
				 (pci_resource_flags((dev), (num)) & IORESOURCE_IO))

#ifdef SUPPORT_JOYSTICK
static int __devinit sv_register_gameport(struct sv_state *s, int io_port)
{
	struct gameport *gp;

	if (!request_region(io_port, SV_EXTENT_GAME, "S3 SonicVibes Gameport")) {
		printk(KERN_ERR "sv: gameport io ports are in use\n");
		return -EBUSY;
	}

	s->gameport = gp = gameport_allocate_port();
	if (!gp) {
		printk(KERN_ERR "sv: can not allocate memory for gameport\n");
		release_region(io_port, SV_EXTENT_GAME);
		return -ENOMEM;
	}

	gameport_set_name(gp, "S3 SonicVibes Gameport");
	gameport_set_phys(gp, "isa%04x/gameport0", io_port);
	gp->dev.parent = &s->dev->dev;
	gp->io = io_port;

	gameport_register_port(gp);

	return 0;
}

static inline void sv_unregister_gameport(struct sv_state *s)
{
	if (s->gameport) {
		int gpio = s->gameport->io;
		gameport_unregister_port(s->gameport);
		release_region(gpio, SV_EXTENT_GAME);
	}
}
#else
static inline int sv_register_gameport(struct sv_state *s, int io_port) { return -ENOSYS; }
static inline void sv_unregister_gameport(struct sv_state *s) { }
#endif /* SUPPORT_JOYSTICK */

static int __devinit sv_probe(struct pci_dev *pcidev, const struct pci_device_id *pciid)
{
	static char __devinitdata sv_ddma_name[] = "S3 Inc. SonicVibes DDMA Controller";
       	struct sv_state *s;
	mm_segment_t fs;
	int i, val, ret;
	int gpio;
	char *ddmaname;
	unsigned ddmanamelen;

	if ((ret=pci_enable_device(pcidev)))
		return ret;

	if (!RSRCISIOREGION(pcidev, RESOURCE_SB) ||
	    !RSRCISIOREGION(pcidev, RESOURCE_ENH) ||
	    !RSRCISIOREGION(pcidev, RESOURCE_SYNTH) ||
	    !RSRCISIOREGION(pcidev, RESOURCE_MIDI) ||
	    !RSRCISIOREGION(pcidev, RESOURCE_GAME))
		return -ENODEV;
	if (pcidev->irq == 0)
		return -ENODEV;
	if (pci_set_dma_mask(pcidev, DMA_24BIT_MASK)) {
		printk(KERN_WARNING "sonicvibes: architecture does not support 24bit PCI busmaster DMA\n");
		return -ENODEV;
	}
	/* try to allocate a DDMA resource if not already available */
	if (!RSRCISIOREGION(pcidev, RESOURCE_DDMA)) {
		pcidev->resource[RESOURCE_DDMA].start = 0;
		pcidev->resource[RESOURCE_DDMA].end = 2*SV_EXTENT_DMA-1;
		pcidev->resource[RESOURCE_DDMA].flags = PCI_BASE_ADDRESS_SPACE_IO | IORESOURCE_IO;
		ddmanamelen = strlen(sv_ddma_name)+1;
		if (!(ddmaname = kmalloc(ddmanamelen, GFP_KERNEL)))
			return -1;
		memcpy(ddmaname, sv_ddma_name, ddmanamelen);
		pcidev->resource[RESOURCE_DDMA].name = ddmaname;
		if (pci_assign_resource(pcidev, RESOURCE_DDMA)) {
			pcidev->resource[RESOURCE_DDMA].name = NULL;
			kfree(ddmaname);
			printk(KERN_ERR "sv: cannot allocate DDMA controller io ports\n");
			return -EBUSY;
		}
	}
	if (!(s = kmalloc(sizeof(struct sv_state), GFP_KERNEL))) {
		printk(KERN_WARNING "sv: out of memory\n");
		return -ENOMEM;
	}
	memset(s, 0, sizeof(struct sv_state));
	init_waitqueue_head(&s->dma_adc.wait);
	init_waitqueue_head(&s->dma_dac.wait);
	init_waitqueue_head(&s->open_wait);
	init_waitqueue_head(&s->midi.iwait);
	init_waitqueue_head(&s->midi.owait);
	mutex_init(&s->open_mutex);
	spin_lock_init(&s->lock);
	s->magic = SV_MAGIC;
	s->dev = pcidev;
	s->iosb = pci_resource_start(pcidev, RESOURCE_SB);
	s->ioenh = pci_resource_start(pcidev, RESOURCE_ENH);
	s->iosynth = pci_resource_start(pcidev, RESOURCE_SYNTH);
	s->iomidi = pci_resource_start(pcidev, RESOURCE_MIDI);
	s->iodmaa = pci_resource_start(pcidev, RESOURCE_DDMA);
	s->iodmac = pci_resource_start(pcidev, RESOURCE_DDMA) + SV_EXTENT_DMA;
	gpio = pci_resource_start(pcidev, RESOURCE_GAME);
	pci_write_config_dword(pcidev, 0x40, s->iodmaa | 9);  /* enable and use extended mode */
	pci_write_config_dword(pcidev, 0x48, s->iodmac | 9);  /* enable */
	printk(KERN_DEBUG "sv: io ports: %#lx %#lx %#lx %#lx %#x %#x %#x\n",
	       s->iosb, s->ioenh, s->iosynth, s->iomidi, gpio, s->iodmaa, s->iodmac);
	s->irq = pcidev->irq;
	
	/* hack */
	pci_write_config_dword(pcidev, 0x60, wavetable_mem >> 12);  /* wavetable base address */

	ret = -EBUSY;
	if (!request_region(s->ioenh, SV_EXTENT_ENH, "S3 SonicVibes PCM")) {
		printk(KERN_ERR "sv: io ports %#lx-%#lx in use\n", s->ioenh, s->ioenh+SV_EXTENT_ENH-1);
		goto err_region5;
	}
	if (!request_region(s->iodmaa, SV_EXTENT_DMA, "S3 SonicVibes DMAA")) {
		printk(KERN_ERR "sv: io ports %#x-%#x in use\n", s->iodmaa, s->iodmaa+SV_EXTENT_DMA-1);
		goto err_region4;
	}
	if (!request_region(s->iodmac, SV_EXTENT_DMA, "S3 SonicVibes DMAC")) {
		printk(KERN_ERR "sv: io ports %#x-%#x in use\n", s->iodmac, s->iodmac+SV_EXTENT_DMA-1);
		goto err_region3;
	}
	if (!request_region(s->iomidi, SV_EXTENT_MIDI, "S3 SonicVibes Midi")) {
		printk(KERN_ERR "sv: io ports %#lx-%#lx in use\n", s->iomidi, s->iomidi+SV_EXTENT_MIDI-1);
		goto err_region2;
	}
	if (!request_region(s->iosynth, SV_EXTENT_SYNTH, "S3 SonicVibes Synth")) {
		printk(KERN_ERR "sv: io ports %#lx-%#lx in use\n", s->iosynth, s->iosynth+SV_EXTENT_SYNTH-1);
		goto err_region1;
	}

	/* initialize codec registers */
	outb(0x80, s->ioenh + SV_CODEC_CONTROL); /* assert reset */
	udelay(50);
	outb(0x00, s->ioenh + SV_CODEC_CONTROL); /* deassert reset */
	udelay(50);
	outb(SV_CCTRL_INTADRIVE | SV_CCTRL_ENHANCED /*| SV_CCTRL_WAVETABLE */
	     | (reverb[devindex] ? SV_CCTRL_REVERB : 0), s->ioenh + SV_CODEC_CONTROL);
	inb(s->ioenh + SV_CODEC_STATUS); /* clear ints */
	wrindir(s, SV_CIDRIVECONTROL, 0);  /* drive current 16mA */
	wrindir(s, SV_CIENABLE, s->enable = 0);  /* disable DMAA and DMAC */
	outb(~(SV_CINTMASK_DMAA | SV_CINTMASK_DMAC), s->ioenh + SV_CODEC_INTMASK);
	/* outb(0xff, s->iodmaa + SV_DMA_RESET); */
	/* outb(0xff, s->iodmac + SV_DMA_RESET); */
	inb(s->ioenh + SV_CODEC_STATUS); /* ack interrupts */
	wrindir(s, SV_CIADCCLKSOURCE, 0); /* use pll as ADC clock source */
	wrindir(s, SV_CIANALOGPWRDOWN, 0); /* power up the analog parts of the device */
	wrindir(s, SV_CIDIGITALPWRDOWN, 0); /* power up the digital parts of the device */
	setpll(s, SV_CIADCPLLM, 8000);
	wrindir(s, SV_CISRSSPACE, 0x80); /* SRS off */
	wrindir(s, SV_CIPCMSR0, (8000 * 65536 / FULLRATE) & 0xff);
	wrindir(s, SV_CIPCMSR1, ((8000 * 65536 / FULLRATE) >> 8) & 0xff);
	wrindir(s, SV_CIADCOUTPUT, 0);
	/* request irq */
	if ((ret=request_irq(s->irq,sv_interrupt,SA_SHIRQ,"S3 SonicVibes",s))) {
		printk(KERN_ERR "sv: irq %u in use\n", s->irq);
		goto err_irq;
	}
	printk(KERN_INFO "sv: found adapter at io %#lx irq %u dmaa %#06x dmac %#06x revision %u\n",
	       s->ioenh, s->irq, s->iodmaa, s->iodmac, rdindir(s, SV_CIREVISION));
	/* register devices */
	if ((s->dev_audio = register_sound_dsp(&sv_audio_fops, -1)) < 0) {
		ret = s->dev_audio;
		goto err_dev1;
	}
	if ((s->dev_mixer = register_sound_mixer(&sv_mixer_fops, -1)) < 0) {
		ret = s->dev_mixer;
		goto err_dev2;
	}
	if ((s->dev_midi = register_sound_midi(&sv_midi_fops, -1)) < 0) {
		ret = s->dev_midi;
		goto err_dev3;
	}
	if ((s->dev_dmfm = register_sound_special(&sv_dmfm_fops, 15 /* ?? */)) < 0) {
		ret = s->dev_dmfm;
		goto err_dev4;
	}
	pci_set_master(pcidev);  /* enable bus mastering */
	/* initialize the chips */
	fs = get_fs();
	set_fs(KERNEL_DS);
	val = SOUND_MASK_LINE|SOUND_MASK_SYNTH;
	mixer_ioctl(s, SOUND_MIXER_WRITE_RECSRC, (unsigned long)&val);
	for (i = 0; i < sizeof(initvol)/sizeof(initvol[0]); i++) {
		val = initvol[i].vol;
		mixer_ioctl(s, initvol[i].mixch, (unsigned long)&val);
	}
	set_fs(fs);
	/* register gameport */
	sv_register_gameport(s, gpio);
	/* store it in the driver field */
	pci_set_drvdata(pcidev, s);
	/* put it into driver list */
	list_add_tail(&s->devs, &devs);
	/* increment devindex */
	if (devindex < NR_DEVICE-1)
		devindex++;
	return 0;

 err_dev4:
	unregister_sound_midi(s->dev_midi);
 err_dev3:
	unregister_sound_mixer(s->dev_mixer);
 err_dev2:
	unregister_sound_dsp(s->dev_audio);
 err_dev1:
	printk(KERN_ERR "sv: cannot register misc device\n");
	free_irq(s->irq, s);
 err_irq:
	release_region(s->iosynth, SV_EXTENT_SYNTH);
 err_region1:
	release_region(s->iomidi, SV_EXTENT_MIDI);
 err_region2:
	release_region(s->iodmac, SV_EXTENT_DMA);
 err_region3:
	release_region(s->iodmaa, SV_EXTENT_DMA);
 err_region4:
	release_region(s->ioenh, SV_EXTENT_ENH);
 err_region5:
	kfree(s);
	return ret;
}

static void __devexit sv_remove(struct pci_dev *dev)
{
	struct sv_state *s = pci_get_drvdata(dev);

	if (!s)
		return;
	list_del(&s->devs);
	outb(~0, s->ioenh + SV_CODEC_INTMASK);  /* disable ints */
	synchronize_irq(s->irq);
	inb(s->ioenh + SV_CODEC_STATUS); /* ack interrupts */
	wrindir(s, SV_CIENABLE, 0);     /* disable DMAA and DMAC */
	/*outb(0, s->iodmaa + SV_DMA_RESET);*/
	/*outb(0, s->iodmac + SV_DMA_RESET);*/
	free_irq(s->irq, s);
	sv_unregister_gameport(s);
	release_region(s->iodmac, SV_EXTENT_DMA);
	release_region(s->iodmaa, SV_EXTENT_DMA);
	release_region(s->ioenh, SV_EXTENT_ENH);
	release_region(s->iomidi, SV_EXTENT_MIDI);
	release_region(s->iosynth, SV_EXTENT_SYNTH);
	unregister_sound_dsp(s->dev_audio);
	unregister_sound_mixer(s->dev_mixer);
	unregister_sound_midi(s->dev_midi);
	unregister_sound_special(s->dev_dmfm);
	kfree(s);
	pci_set_drvdata(dev, NULL);
}

static struct pci_device_id id_table[] = {
       { PCI_VENDOR_ID_S3, PCI_DEVICE_ID_S3_SONICVIBES, PCI_ANY_ID, PCI_ANY_ID, 0, 0 },
       { 0, }
};

MODULE_DEVICE_TABLE(pci, id_table);

static struct pci_driver sv_driver = {
       .name		= "sonicvibes",
       .id_table	= id_table,
       .probe		= sv_probe,
       .remove		= __devexit_p(sv_remove),
};
 
static int __init init_sonicvibes(void)
{
	printk(KERN_INFO "sv: version v0.31 time " __TIME__ " " __DATE__ "\n");
#if 0
	if (!(wavetable_mem = __get_free_pages(GFP_KERNEL, 20-PAGE_SHIFT)))
		printk(KERN_INFO "sv: cannot allocate 1MB of contiguous nonpageable memory for wavetable data\n");
#endif
	return pci_register_driver(&sv_driver);
}

static void __exit cleanup_sonicvibes(void)
{
	printk(KERN_INFO "sv: unloading\n");
	pci_unregister_driver(&sv_driver);
 	if (wavetable_mem)
		free_pages(wavetable_mem, 20-PAGE_SHIFT);
}

module_init(init_sonicvibes);
module_exit(cleanup_sonicvibes);

/* --------------------------------------------------------------------- */

#ifndef MODULE

/* format is: sonicvibes=[reverb] sonicvibesdmaio=dmaioaddr */

static int __init sonicvibes_setup(char *str)
{
	static unsigned __initdata nr_dev = 0;

	if (nr_dev >= NR_DEVICE)
		return 0;
#if 0
	if (get_option(&str, &reverb[nr_dev]) == 2)
		(void)get_option(&str, &wavetable[nr_dev]);
#else
	(void)get_option(&str, &reverb[nr_dev]);
#endif

	nr_dev++;
	return 1;
}

__setup("sonicvibes=", sonicvibes_setup);

#endif /* MODULE */
