/*****************************************************************************/

/*
 *      es1370.c  --  Ensoniq ES1370/Asahi Kasei AK4531 audio driver.
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
 *   lineout  if 1 the LINE jack is used as an output instead of an input.
 *            LINE then contains the unmixed dsp output. This can be used
 *            to make the card a four channel one: use dsp to output two
 *            channels to LINE and dac to output the other two channels to
 *            SPKR. Set the mixer to only output synth to SPKR.
 *   micbias  sets the +5V bias to the mic if using an electretmic.
 *            
 *
 *  Note: sync mode is not yet supported (i.e. running dsp and dac from the same
 *  clock source)
 *
 *  Supported devices:
 *  /dev/dsp    standard /dev/dsp device, (mostly) OSS compatible
 *  /dev/mixer  standard /dev/mixer device, (mostly) OSS compatible
 *  /dev/dsp1   additional DAC, like /dev/dsp, but output only,
 *              only 5512, 11025, 22050 and 44100 samples/s,
 *              outputs to mixer "SYNTH" setting
 *  /dev/midi   simple MIDI UART interface, no ioctl
 *
 *  NOTE: the card does not have any FM/Wavetable synthesizer, it is supposed
 *  to be done in software. That is what /dev/dac is for. By now (Q2 1998)
 *  there are several MIDI to PCM (WAV) packages, one of them is timidity.
 *
 *  Revision history
 *    26.03.1998   0.1   Initial release
 *    31.03.1998   0.2   Fix bug in GETOSPACE
 *    04.04.1998   0.3   Make it work (again) under 2.0.33
 *                       Fix mixer write operation not returning the actual
 *                       settings
 *    05.04.1998   0.4   First attempt at using the new PCI stuff
 *    29.04.1998   0.5   Fix hang when ^C is pressed on amp
 *    07.05.1998   0.6   Don't double lock around stop_*() in *_release()
 *    10.05.1998   0.7   First stab at a simple midi interface (no bells&whistles)
 *    14.05.1998   0.8   Don't allow excessive interrupt rates
 *    08.06.1998   0.9   First release using Alan Cox' soundcore instead of
 *                       miscdevice
 *    05.07.1998   0.10  Fixed the driver to correctly maintin OSS style volume
 *                       settings (not sure if this should be standard)
 *                       Fixed many references: f_flags should be f_mode
 *                       -- Gerald Britton <gbritton@mit.edu>
 *    03.08.1998   0.11  Now mixer behaviour can basically be selected between
 *                       "OSS documented" and "OSS actual" behaviour
 *                       Fixed mixer table thanks to Hakan.Lennestal@lu.erisoft.se
 *                       On module startup, set DAC2 to 11kSPS instead of 5.5kSPS,
 *                       as it produces an annoying ssssh in the lower sampling rate
 *                       Do not include modversions.h
 *    22.08.1998   0.12  Mixer registers actually have 5 instead of 4 bits
 *                       pointed out by Itai Nahshon
 *    31.08.1998   0.13  Fix realplayer problems - dac.count issues
 *    08.10.1998   0.14  Joystick support fixed
 *		         -- Oliver Neukum <c188@org.chemie.uni-muenchen.de>
 *    10.12.1998   0.15  Fix drain_dac trying to wait on not yet initialized DMA
 *    16.12.1998   0.16  Don't wake up app until there are fragsize bytes to read/write
 *    06.01.1999   0.17  remove the silly SA_INTERRUPT flag.
 *                       hopefully killed the egcs section type conflict
 *    12.03.1999   0.18  cinfo.blocks should be reset after GETxPTR ioctl.
 *                       reported by Johan Maes <joma@telindus.be>
 *    22.03.1999   0.19  return EAGAIN instead of EBUSY when O_NONBLOCK
 *                       read/write cannot be executed
 *    07.04.1999   0.20  implemented the following ioctl's: SOUND_PCM_READ_RATE, 
 *                       SOUND_PCM_READ_CHANNELS, SOUND_PCM_READ_BITS; 
 *                       Alpha fixes reported by Peter Jones <pjones@redhat.com>
 *                       Note: joystick address handling might still be wrong on archs
 *                       other than i386
 *    10.05.1999   0.21  Added support for an electret mic for SB PCI64
 *                       to the Linux kernel sound driver. This mod also straighten
 *                       out the question marks around the mic impedance setting
 *                       (micz). From Kim.Berts@fisub.mail.abb.com
 *    11.05.1999   0.22  Implemented the IMIX call to mute recording monitor.
 *                       Guenter Geiger <geiger@epy.co.at>
 *    15.06.1999   0.23  Fix bad allocation bug.
 *                       Thanks to Deti Fliegl <fliegl@in.tum.de>
 *    28.06.1999   0.24  Add pci_set_master
 *    02.08.1999   0.25  Added workaround for the "phantom write" bug first
 *                       documented by Dave Sharpless from Anchor Games
 *    03.08.1999   0.26  adapt to Linus' new __setup/__initcall
 *                       added kernel command line option "es1370=joystick[,lineout[,micbias]]"
 *                       removed CONFIG_SOUND_ES1370_JOYPORT_BOOT kludge
 *    12.08.1999   0.27  module_init/__setup fixes
 *    19.08.1999   0.28  SOUND_MIXER_IMIX fixes, reported by Gianluca <gialluca@mail.tiscalinet.it>
 *    31.08.1999   0.29  add spin_lock_init
 *                       replaced current->state = x with set_current_state(x)
 *    03.09.1999   0.30  change read semantics for MIDI to match
 *                       OSS more closely; remove possible wakeup race
 *    28.10.1999   0.31  More waitqueue races fixed
 *    08.01.2000   0.32  Prevent some ioctl's from returning bad count values on underrun/overrun;
 *                       Tim Janik's BSE (Bedevilled Sound Engine) found this
 *    07.02.2000   0.33  Use pci_alloc_consistent and pci_register_driver
 *    21.11.2000   0.34  Initialize dma buffers in poll, otherwise poll may return a bogus mask
 *    12.12.2000   0.35  More dma buffer initializations, patch from
 *                       Tjeerd Mulder <tjeerd.mulder@fujitsu-siemens.com>
 *    07.01.2001   0.36  Timeout change in wrcodec as requested by Frank Klemm <pfk@fuchs.offl.uni-jena.de>
 *    31.01.2001   0.37  Register/Unregister gameport
 *                       Fix SETTRIGGER non OSS API conformity
 *    03.01.2003   0.38  open_mode fixes from Georg Acher <acher@in.tum.de>
 *
 * some important things missing in Ensoniq documentation:
 *
 * Experimental PCLKDIV results:  play the same waveforms on both DAC1 and DAC2
 * and vary PCLKDIV to obtain zero beat.
 *  5512sps:  254
 * 44100sps:   30
 * seems to be fs = 1411200/(PCLKDIV+2)
 *
 * should find out when curr_sample_ct is cleared and
 * where exactly the CCB fetches data
 *
 * The card uses a 22.5792 MHz crystal.
 * The LINEIN jack may be converted to an AOUT jack by
 * setting pin 47 (XCTL0) of the ES1370 to high.
 * Pin 48 (XCTL1) of the ES1370 sets the +5V bias for an electretmic
 * 
 *
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
#include <linux/smp_lock.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/spinlock.h>
#include <linux/gameport.h>
#include <linux/wait.h>
#include <linux/dma-mapping.h>
#include <linux/mutex.h>

#include <asm/io.h>
#include <asm/page.h>
#include <asm/uaccess.h>

#if defined(CONFIG_GAMEPORT) || (defined(MODULE) && defined(CONFIG_GAMEPORT_MODULE))
#define SUPPORT_JOYSTICK
#endif

/* --------------------------------------------------------------------- */

#undef OSS_DOCUMENTED_MIXER_SEMANTICS
#define DBG(x) {}
/*#define DBG(x) {x}*/

/* --------------------------------------------------------------------- */

#ifndef PCI_VENDOR_ID_ENSONIQ
#define PCI_VENDOR_ID_ENSONIQ        0x1274    
#endif

#ifndef PCI_DEVICE_ID_ENSONIQ_ES1370
#define PCI_DEVICE_ID_ENSONIQ_ES1370 0x5000
#endif

#define ES1370_MAGIC  ((PCI_VENDOR_ID_ENSONIQ<<16)|PCI_DEVICE_ID_ENSONIQ_ES1370)

#define ES1370_EXTENT             0x40
#define JOY_EXTENT                8

#define ES1370_REG_CONTROL        0x00
#define ES1370_REG_STATUS         0x04
#define ES1370_REG_UART_DATA      0x08
#define ES1370_REG_UART_STATUS    0x09
#define ES1370_REG_UART_CONTROL   0x09
#define ES1370_REG_UART_TEST      0x0a
#define ES1370_REG_MEMPAGE        0x0c
#define ES1370_REG_CODEC          0x10
#define ES1370_REG_SERIAL_CONTROL 0x20
#define ES1370_REG_DAC1_SCOUNT    0x24
#define ES1370_REG_DAC2_SCOUNT    0x28
#define ES1370_REG_ADC_SCOUNT     0x2c

#define ES1370_REG_DAC1_FRAMEADR    0xc30
#define ES1370_REG_DAC1_FRAMECNT    0xc34
#define ES1370_REG_DAC2_FRAMEADR    0xc38
#define ES1370_REG_DAC2_FRAMECNT    0xc3c
#define ES1370_REG_ADC_FRAMEADR     0xd30
#define ES1370_REG_ADC_FRAMECNT     0xd34
#define ES1370_REG_PHANTOM_FRAMEADR 0xd38
#define ES1370_REG_PHANTOM_FRAMECNT 0xd3c

#define ES1370_FMT_U8_MONO     0
#define ES1370_FMT_U8_STEREO   1
#define ES1370_FMT_S16_MONO    2
#define ES1370_FMT_S16_STEREO  3
#define ES1370_FMT_STEREO      1
#define ES1370_FMT_S16         2
#define ES1370_FMT_MASK        3

static const unsigned sample_size[] = { 1, 2, 2, 4 };
static const unsigned sample_shift[] = { 0, 1, 1, 2 };

static const unsigned dac1_samplerate[] = { 5512, 11025, 22050, 44100 };

#define DAC2_SRTODIV(x) (((1411200+(x)/2)/(x))-2)
#define DAC2_DIVTOSR(x) (1411200/((x)+2))

#define CTRL_ADC_STOP   0x80000000  /* 1 = ADC stopped */
#define CTRL_XCTL1      0x40000000  /* electret mic bias */
#define CTRL_OPEN       0x20000000  /* no function, can be read and written */
#define CTRL_PCLKDIV    0x1fff0000  /* ADC/DAC2 clock divider */
#define CTRL_SH_PCLKDIV 16
#define CTRL_MSFMTSEL   0x00008000  /* MPEG serial data fmt: 0 = Sony, 1 = I2S */
#define CTRL_M_SBB      0x00004000  /* DAC2 clock: 0 = PCLKDIV, 1 = MPEG */
#define CTRL_WTSRSEL    0x00003000  /* DAC1 clock freq: 0=5512, 1=11025, 2=22050, 3=44100 */
#define CTRL_SH_WTSRSEL 12
#define CTRL_DAC_SYNC   0x00000800  /* 1 = DAC2 runs off DAC1 clock */
#define CTRL_CCB_INTRM  0x00000400  /* 1 = CCB "voice" ints enabled */
#define CTRL_M_CB       0x00000200  /* recording source: 0 = ADC, 1 = MPEG */
#define CTRL_XCTL0      0x00000100  /* 0 = Line in, 1 = Line out */
#define CTRL_BREQ       0x00000080  /* 1 = test mode (internal mem test) */
#define CTRL_DAC1_EN    0x00000040  /* enable DAC1 */
#define CTRL_DAC2_EN    0x00000020  /* enable DAC2 */
#define CTRL_ADC_EN     0x00000010  /* enable ADC */
#define CTRL_UART_EN    0x00000008  /* enable MIDI uart */
#define CTRL_JYSTK_EN   0x00000004  /* enable Joystick port (presumably at address 0x200) */
#define CTRL_CDC_EN     0x00000002  /* enable serial (CODEC) interface */
#define CTRL_SERR_DIS   0x00000001  /* 1 = disable PCI SERR signal */

#define STAT_INTR       0x80000000  /* wired or of all interrupt bits */
#define STAT_CSTAT      0x00000400  /* 1 = codec busy or codec write in progress */
#define STAT_CBUSY      0x00000200  /* 1 = codec busy */
#define STAT_CWRIP      0x00000100  /* 1 = codec write in progress */
#define STAT_VC         0x00000060  /* CCB int source, 0=DAC1, 1=DAC2, 2=ADC, 3=undef */
#define STAT_SH_VC      5
#define STAT_MCCB       0x00000010  /* CCB int pending */
#define STAT_UART       0x00000008  /* UART int pending */
#define STAT_DAC1       0x00000004  /* DAC1 int pending */
#define STAT_DAC2       0x00000002  /* DAC2 int pending */
#define STAT_ADC        0x00000001  /* ADC int pending */

#define USTAT_RXINT     0x80        /* UART rx int pending */
#define USTAT_TXINT     0x04        /* UART tx int pending */
#define USTAT_TXRDY     0x02        /* UART tx ready */
#define USTAT_RXRDY     0x01        /* UART rx ready */

#define UCTRL_RXINTEN   0x80        /* 1 = enable RX ints */
#define UCTRL_TXINTEN   0x60        /* TX int enable field mask */
#define UCTRL_ENA_TXINT 0x20        /* enable TX int */
#define UCTRL_CNTRL     0x03        /* control field */
#define UCTRL_CNTRL_SWR 0x03        /* software reset command */

#define SCTRL_P2ENDINC    0x00380000  /*  */
#define SCTRL_SH_P2ENDINC 19
#define SCTRL_P2STINC     0x00070000  /*  */
#define SCTRL_SH_P2STINC  16
#define SCTRL_R1LOOPSEL   0x00008000  /* 0 = loop mode */
#define SCTRL_P2LOOPSEL   0x00004000  /* 0 = loop mode */
#define SCTRL_P1LOOPSEL   0x00002000  /* 0 = loop mode */
#define SCTRL_P2PAUSE     0x00001000  /* 1 = pause mode */
#define SCTRL_P1PAUSE     0x00000800  /* 1 = pause mode */
#define SCTRL_R1INTEN     0x00000400  /* enable interrupt */
#define SCTRL_P2INTEN     0x00000200  /* enable interrupt */
#define SCTRL_P1INTEN     0x00000100  /* enable interrupt */
#define SCTRL_P1SCTRLD    0x00000080  /* reload sample count register for DAC1 */
#define SCTRL_P2DACSEN    0x00000040  /* 1 = DAC2 play back last sample when disabled */
#define SCTRL_R1SEB       0x00000020  /* 1 = 16bit */
#define SCTRL_R1SMB       0x00000010  /* 1 = stereo */
#define SCTRL_R1FMT       0x00000030  /* format mask */
#define SCTRL_SH_R1FMT    4
#define SCTRL_P2SEB       0x00000008  /* 1 = 16bit */
#define SCTRL_P2SMB       0x00000004  /* 1 = stereo */
#define SCTRL_P2FMT       0x0000000c  /* format mask */
#define SCTRL_SH_P2FMT    2
#define SCTRL_P1SEB       0x00000002  /* 1 = 16bit */
#define SCTRL_P1SMB       0x00000001  /* 1 = stereo */
#define SCTRL_P1FMT       0x00000003  /* format mask */
#define SCTRL_SH_P1FMT    0

/* misc stuff */

#define FMODE_DAC         4           /* slight misuse of mode_t */

/* MIDI buffer sizes */

#define MIDIINBUF  256
#define MIDIOUTBUF 256

#define FMODE_MIDI_SHIFT 3
#define FMODE_MIDI_READ  (FMODE_READ << FMODE_MIDI_SHIFT)
#define FMODE_MIDI_WRITE (FMODE_WRITE << FMODE_MIDI_SHIFT)

/* --------------------------------------------------------------------- */

struct es1370_state {
	/* magic */
	unsigned int magic;

	/* list of es1370 devices */
	struct list_head devs;

	/* the corresponding pci_dev structure */
	struct pci_dev *dev;

	/* soundcore stuff */
	int dev_audio;
	int dev_mixer;
	int dev_dac;
	int dev_midi;
	
	/* hardware resources */
	unsigned long io; /* long for SPARC */
	unsigned int irq;

	/* mixer registers; there is no HW readback */
	struct {
		unsigned short vol[10];
		unsigned int recsrc;
		unsigned int modcnt;
		unsigned short micpreamp;
	        unsigned int imix;
	} mix;

	/* wave stuff */
	unsigned ctrl;
	unsigned sctrl;

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
	} dma_dac1, dma_dac2, dma_adc;

	/* The following buffer is used to point the phantom write channel to. */
	unsigned char *bugbuf_cpu;
	dma_addr_t bugbuf_dma;

	/* midi stuff */
	struct {
		unsigned ird, iwr, icnt;
		unsigned ord, owr, ocnt;
		wait_queue_head_t iwait;
		wait_queue_head_t owait;
		unsigned char ibuf[MIDIINBUF];
		unsigned char obuf[MIDIOUTBUF];
	} midi;

#ifdef SUPPORT_JOYSTICK
	struct gameport *gameport;
#endif

	struct mutex mutex;
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

static void wrcodec(struct es1370_state *s, unsigned char idx, unsigned char data)
{
	unsigned long tmo = jiffies + HZ/10, j;
	
	do {
		j = jiffies;
		if (!(inl(s->io+ES1370_REG_STATUS) & STAT_CSTAT)) {
			outw((((unsigned short)idx)<<8)|data, s->io+ES1370_REG_CODEC);
			return;
		}
		schedule();
	} while ((signed)(tmo-j) > 0);
	printk(KERN_ERR "es1370: write to codec register timeout\n");
}

/* --------------------------------------------------------------------- */

static inline void stop_adc(struct es1370_state *s)
{
	unsigned long flags;

	spin_lock_irqsave(&s->lock, flags);
	s->ctrl &= ~CTRL_ADC_EN;
	outl(s->ctrl, s->io+ES1370_REG_CONTROL);
	spin_unlock_irqrestore(&s->lock, flags);
}	

static inline void stop_dac1(struct es1370_state *s)
{
	unsigned long flags;

	spin_lock_irqsave(&s->lock, flags);
	s->ctrl &= ~CTRL_DAC1_EN;
	outl(s->ctrl, s->io+ES1370_REG_CONTROL);
	spin_unlock_irqrestore(&s->lock, flags);
}	

static inline void stop_dac2(struct es1370_state *s)
{
	unsigned long flags;

	spin_lock_irqsave(&s->lock, flags);
	s->ctrl &= ~CTRL_DAC2_EN;
	outl(s->ctrl, s->io+ES1370_REG_CONTROL);
	spin_unlock_irqrestore(&s->lock, flags);
}	

static void start_dac1(struct es1370_state *s)
{
	unsigned long flags;
	unsigned fragremain, fshift;

	spin_lock_irqsave(&s->lock, flags);
	if (!(s->ctrl & CTRL_DAC1_EN) && (s->dma_dac1.mapped || s->dma_dac1.count > 0)
	    && s->dma_dac1.ready) {
		s->ctrl |= CTRL_DAC1_EN;
		s->sctrl = (s->sctrl & ~(SCTRL_P1LOOPSEL | SCTRL_P1PAUSE | SCTRL_P1SCTRLD)) | SCTRL_P1INTEN;
		outl(s->sctrl, s->io+ES1370_REG_SERIAL_CONTROL);
		fragremain = ((- s->dma_dac1.hwptr) & (s->dma_dac1.fragsize-1));
		fshift = sample_shift[(s->sctrl & SCTRL_P1FMT) >> SCTRL_SH_P1FMT];
		if (fragremain < 2*fshift)
			fragremain = s->dma_dac1.fragsize;
		outl((fragremain >> fshift) - 1, s->io+ES1370_REG_DAC1_SCOUNT);
		outl(s->ctrl, s->io+ES1370_REG_CONTROL);
		outl((s->dma_dac1.fragsize >> fshift) - 1, s->io+ES1370_REG_DAC1_SCOUNT);
	}
	spin_unlock_irqrestore(&s->lock, flags);
}	

static void start_dac2(struct es1370_state *s)
{
	unsigned long flags;
	unsigned fragremain, fshift;

	spin_lock_irqsave(&s->lock, flags);
	if (!(s->ctrl & CTRL_DAC2_EN) && (s->dma_dac2.mapped || s->dma_dac2.count > 0)
	    && s->dma_dac2.ready) {
		s->ctrl |= CTRL_DAC2_EN;
		s->sctrl = (s->sctrl & ~(SCTRL_P2LOOPSEL | SCTRL_P2PAUSE | SCTRL_P2DACSEN | 
					 SCTRL_P2ENDINC | SCTRL_P2STINC)) | SCTRL_P2INTEN |
			(((s->sctrl & SCTRL_P2FMT) ? 2 : 1) << SCTRL_SH_P2ENDINC) | 
			(0 << SCTRL_SH_P2STINC);
		outl(s->sctrl, s->io+ES1370_REG_SERIAL_CONTROL);
		fragremain = ((- s->dma_dac2.hwptr) & (s->dma_dac2.fragsize-1));
		fshift = sample_shift[(s->sctrl & SCTRL_P2FMT) >> SCTRL_SH_P2FMT];
		if (fragremain < 2*fshift)
			fragremain = s->dma_dac2.fragsize;
		outl((fragremain >> fshift) - 1, s->io+ES1370_REG_DAC2_SCOUNT);
		outl(s->ctrl, s->io+ES1370_REG_CONTROL);
		outl((s->dma_dac2.fragsize >> fshift) - 1, s->io+ES1370_REG_DAC2_SCOUNT);
	}
	spin_unlock_irqrestore(&s->lock, flags);
}	

static void start_adc(struct es1370_state *s)
{
	unsigned long flags;
	unsigned fragremain, fshift;

	spin_lock_irqsave(&s->lock, flags);
	if (!(s->ctrl & CTRL_ADC_EN) && (s->dma_adc.mapped || s->dma_adc.count < (signed)(s->dma_adc.dmasize - 2*s->dma_adc.fragsize))
	    && s->dma_adc.ready) {
		s->ctrl |= CTRL_ADC_EN;
		s->sctrl = (s->sctrl & ~SCTRL_R1LOOPSEL) | SCTRL_R1INTEN;
		outl(s->sctrl, s->io+ES1370_REG_SERIAL_CONTROL);
		fragremain = ((- s->dma_adc.hwptr) & (s->dma_adc.fragsize-1));
		fshift = sample_shift[(s->sctrl & SCTRL_R1FMT) >> SCTRL_SH_R1FMT];
		if (fragremain < 2*fshift)
			fragremain = s->dma_adc.fragsize;
		outl((fragremain >> fshift) - 1, s->io+ES1370_REG_ADC_SCOUNT);
		outl(s->ctrl, s->io+ES1370_REG_CONTROL);
		outl((s->dma_adc.fragsize >> fshift) - 1, s->io+ES1370_REG_ADC_SCOUNT);
	}
	spin_unlock_irqrestore(&s->lock, flags);
}	

/* --------------------------------------------------------------------- */

#define DMABUF_DEFAULTORDER (17-PAGE_SHIFT)
#define DMABUF_MINORDER 1

static inline void dealloc_dmabuf(struct es1370_state *s, struct dmabuf *db)
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

static int prog_dmabuf(struct es1370_state *s, struct dmabuf *db, unsigned rate, unsigned fmt, unsigned reg)
{
	int order;
	unsigned bytepersec;
	unsigned bufs;
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
	fmt &= ES1370_FMT_MASK;
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
	memset(db->rawbuf, (fmt & ES1370_FMT_S16) ? 0 : 0x80, db->dmasize);
	outl((reg >> 8) & 15, s->io+ES1370_REG_MEMPAGE);
	outl(db->dmaaddr, s->io+(reg & 0xff));
	outl((db->dmasize >> 2)-1, s->io+((reg + 4) & 0xff));
	db->enabled = 1;
	db->ready = 1;
	return 0;
}

static inline int prog_dmabuf_adc(struct es1370_state *s)
{
	stop_adc(s);
	return prog_dmabuf(s, &s->dma_adc, DAC2_DIVTOSR((s->ctrl & CTRL_PCLKDIV) >> CTRL_SH_PCLKDIV),
			   (s->sctrl >> SCTRL_SH_R1FMT) & ES1370_FMT_MASK, ES1370_REG_ADC_FRAMEADR);
}

static inline int prog_dmabuf_dac2(struct es1370_state *s)
{
	stop_dac2(s);
	return prog_dmabuf(s, &s->dma_dac2, DAC2_DIVTOSR((s->ctrl & CTRL_PCLKDIV) >> CTRL_SH_PCLKDIV),
			   (s->sctrl >> SCTRL_SH_P2FMT) & ES1370_FMT_MASK, ES1370_REG_DAC2_FRAMEADR);
}

static inline int prog_dmabuf_dac1(struct es1370_state *s)
{
	stop_dac1(s);
	return prog_dmabuf(s, &s->dma_dac1, dac1_samplerate[(s->ctrl & CTRL_WTSRSEL) >> CTRL_SH_WTSRSEL],
			   (s->sctrl >> SCTRL_SH_P1FMT) & ES1370_FMT_MASK, ES1370_REG_DAC1_FRAMEADR);
}

static inline unsigned get_hwptr(struct es1370_state *s, struct dmabuf *db, unsigned reg)
{
	unsigned hwptr, diff;

	outl((reg >> 8) & 15, s->io+ES1370_REG_MEMPAGE);
	hwptr = (inl(s->io+(reg & 0xff)) >> 14) & 0x3fffc;
	diff = (db->dmasize + hwptr - db->hwptr) % db->dmasize;
	db->hwptr = hwptr;
	return diff;
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
static void es1370_update_ptr(struct es1370_state *s)
{
	int diff;

	/* update ADC pointer */
	if (s->ctrl & CTRL_ADC_EN) {
		diff = get_hwptr(s, &s->dma_adc, ES1370_REG_ADC_FRAMECNT);
		s->dma_adc.total_bytes += diff;
		s->dma_adc.count += diff;
		if (s->dma_adc.count >= (signed)s->dma_adc.fragsize) 
			wake_up(&s->dma_adc.wait);
		if (!s->dma_adc.mapped) {
			if (s->dma_adc.count > (signed)(s->dma_adc.dmasize - ((3 * s->dma_adc.fragsize) >> 1))) {
				s->ctrl &= ~CTRL_ADC_EN;
				outl(s->ctrl, s->io+ES1370_REG_CONTROL);
				s->dma_adc.error++;
			}
		}
	}
	/* update DAC1 pointer */
	if (s->ctrl & CTRL_DAC1_EN) {
		diff = get_hwptr(s, &s->dma_dac1, ES1370_REG_DAC1_FRAMECNT);
		s->dma_dac1.total_bytes += diff;
		if (s->dma_dac1.mapped) {
			s->dma_dac1.count += diff;
			if (s->dma_dac1.count >= (signed)s->dma_dac1.fragsize)
				wake_up(&s->dma_dac1.wait);
		} else {
			s->dma_dac1.count -= diff;
			if (s->dma_dac1.count <= 0) {
				s->ctrl &= ~CTRL_DAC1_EN;
				outl(s->ctrl, s->io+ES1370_REG_CONTROL);
				s->dma_dac1.error++;
			} else if (s->dma_dac1.count <= (signed)s->dma_dac1.fragsize && !s->dma_dac1.endcleared) {
				clear_advance(s->dma_dac1.rawbuf, s->dma_dac1.dmasize, s->dma_dac1.swptr, 
					      s->dma_dac1.fragsize, (s->sctrl & SCTRL_P1SEB) ? 0 : 0x80);
				s->dma_dac1.endcleared = 1;
			}
			if (s->dma_dac1.count + (signed)s->dma_dac1.fragsize <= (signed)s->dma_dac1.dmasize)
				wake_up(&s->dma_dac1.wait);
		}
	}
	/* update DAC2 pointer */
	if (s->ctrl & CTRL_DAC2_EN) {
		diff = get_hwptr(s, &s->dma_dac2, ES1370_REG_DAC2_FRAMECNT);
		s->dma_dac2.total_bytes += diff;
		if (s->dma_dac2.mapped) {
			s->dma_dac2.count += diff;
			if (s->dma_dac2.count >= (signed)s->dma_dac2.fragsize)
				wake_up(&s->dma_dac2.wait);
		} else {
			s->dma_dac2.count -= diff;
			if (s->dma_dac2.count <= 0) {
				s->ctrl &= ~CTRL_DAC2_EN;
				outl(s->ctrl, s->io+ES1370_REG_CONTROL);
				s->dma_dac2.error++;
			} else if (s->dma_dac2.count <= (signed)s->dma_dac2.fragsize && !s->dma_dac2.endcleared) {
				clear_advance(s->dma_dac2.rawbuf, s->dma_dac2.dmasize, s->dma_dac2.swptr, 
					      s->dma_dac2.fragsize, (s->sctrl & SCTRL_P2SEB) ? 0 : 0x80);
				s->dma_dac2.endcleared = 1;
			}
			if (s->dma_dac2.count + (signed)s->dma_dac2.fragsize <= (signed)s->dma_dac2.dmasize)
				wake_up(&s->dma_dac2.wait);
		}
	}
}

/* hold spinlock for the following! */
static void es1370_handle_midi(struct es1370_state *s)
{
	unsigned char ch;
	int wake;

	if (!(s->ctrl & CTRL_UART_EN))
		return;
	wake = 0;
	while (inb(s->io+ES1370_REG_UART_STATUS) & USTAT_RXRDY) {
		ch = inb(s->io+ES1370_REG_UART_DATA);
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
	while ((inb(s->io+ES1370_REG_UART_STATUS) & USTAT_TXRDY) && s->midi.ocnt > 0) {
		outb(s->midi.obuf[s->midi.ord], s->io+ES1370_REG_UART_DATA);
		s->midi.ord = (s->midi.ord + 1) % MIDIOUTBUF;
		s->midi.ocnt--;
		if (s->midi.ocnt < MIDIOUTBUF-16)
			wake = 1;
	}
	if (wake)
		wake_up(&s->midi.owait);
	outb((s->midi.ocnt > 0) ? UCTRL_RXINTEN | UCTRL_ENA_TXINT : UCTRL_RXINTEN, s->io+ES1370_REG_UART_CONTROL);
}

static irqreturn_t es1370_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
        struct es1370_state *s = (struct es1370_state *)dev_id;
	unsigned int intsrc, sctl;
	
	/* fastpath out, to ease interrupt sharing */
	intsrc = inl(s->io+ES1370_REG_STATUS);
	if (!(intsrc & 0x80000000))
		return IRQ_NONE;
	spin_lock(&s->lock);
	/* clear audio interrupts first */
	sctl = s->sctrl;
	if (intsrc & STAT_ADC)
		sctl &= ~SCTRL_R1INTEN;
	if (intsrc & STAT_DAC1)
		sctl &= ~SCTRL_P1INTEN;
	if (intsrc & STAT_DAC2)
		sctl &= ~SCTRL_P2INTEN;
	outl(sctl, s->io+ES1370_REG_SERIAL_CONTROL);
	outl(s->sctrl, s->io+ES1370_REG_SERIAL_CONTROL);
	es1370_update_ptr(s);
	es1370_handle_midi(s);
	spin_unlock(&s->lock);
	return IRQ_HANDLED;
}

/* --------------------------------------------------------------------- */

static const char invalid_magic[] = KERN_CRIT "es1370: invalid magic value\n";

#define VALIDATE_STATE(s)                         \
({                                                \
	if (!(s) || (s)->magic != ES1370_MAGIC) { \
		printk(invalid_magic);            \
		return -ENXIO;                    \
	}                                         \
})

/* --------------------------------------------------------------------- */

static const struct {
	unsigned volidx:4;
	unsigned left:4;
	unsigned right:4;
	unsigned stereo:1;
	unsigned recmask:13;
	unsigned avail:1;
} mixtable[SOUND_MIXER_NRDEVICES] = {
	[SOUND_MIXER_VOLUME] = { 0, 0x0, 0x1, 1, 0x0000, 1 },   /* master */
	[SOUND_MIXER_PCM]    = { 1, 0x2, 0x3, 1, 0x0400, 1 },   /* voice */
	[SOUND_MIXER_SYNTH]  = { 2, 0x4, 0x5, 1, 0x0060, 1 },   /* FM */
	[SOUND_MIXER_CD]     = { 3, 0x6, 0x7, 1, 0x0006, 1 },   /* CD */
	[SOUND_MIXER_LINE]   = { 4, 0x8, 0x9, 1, 0x0018, 1 },   /* Line */
	[SOUND_MIXER_LINE1]  = { 5, 0xa, 0xb, 1, 0x1800, 1 },   /* AUX */
	[SOUND_MIXER_LINE2]  = { 6, 0xc, 0x0, 0, 0x0100, 1 },   /* Mono1 */
	[SOUND_MIXER_LINE3]  = { 7, 0xd, 0x0, 0, 0x0200, 1 },   /* Mono2 */
	[SOUND_MIXER_MIC]    = { 8, 0xe, 0x0, 0, 0x0001, 1 },   /* Mic */
	[SOUND_MIXER_OGAIN]  = { 9, 0xf, 0x0, 0, 0x0000, 1 }    /* mono out */
};

static void set_recsrc(struct es1370_state *s, unsigned int val)
{
	unsigned int i, j;

	for (j = i = 0; i < SOUND_MIXER_NRDEVICES; i++) {
		if (!(val & (1 << i)))
			continue;
		if (!mixtable[i].recmask) {
			val &= ~(1 << i);
			continue;
		}
		j |= mixtable[i].recmask;
	}
	s->mix.recsrc = val;
	wrcodec(s, 0x12, j & 0xd5);
	wrcodec(s, 0x13, j & 0xaa);
	wrcodec(s, 0x14, (j >> 8) & 0x17);
	wrcodec(s, 0x15, (j >> 8) & 0x0f);
	i = (j & 0x37f) | ((j << 1) & 0x3000) | 0xc60;
	if (!s->mix.imix) {
		i &= 0xff60;  /* mute record and line monitor */
	}
	wrcodec(s, 0x10, i);
	wrcodec(s, 0x11, i >> 8);
}

static int mixer_ioctl(struct es1370_state *s, unsigned int cmd, unsigned long arg)
{
	unsigned long flags;
	int i, val;
	unsigned char l, r, rl, rr;
	int __user *p = (int __user *)arg;

	VALIDATE_STATE(s);
	if (cmd == SOUND_MIXER_PRIVATE1) {
		/* enable/disable/query mixer preamp */
		if (get_user(val, p))
			return -EFAULT;
		if (val != -1) {
			s->mix.micpreamp = !!val;
			wrcodec(s, 0x19, s->mix.micpreamp);
		}
		return put_user(s->mix.micpreamp, p);
	}
	if (cmd == SOUND_MIXER_PRIVATE2) {
		/* enable/disable/query use of linein as second lineout */
		if (get_user(val, p))
			return -EFAULT;
		if (val != -1) {
			spin_lock_irqsave(&s->lock, flags);
			if (val)
				s->ctrl |= CTRL_XCTL0;
			else
				s->ctrl &= ~CTRL_XCTL0;
			outl(s->ctrl, s->io+ES1370_REG_CONTROL);
			spin_unlock_irqrestore(&s->lock, flags);
		}
		return put_user((s->ctrl & CTRL_XCTL0) ? 1 : 0, p);
	}
	if (cmd == SOUND_MIXER_PRIVATE3) {
		/* enable/disable/query microphone impedance setting */
		if (get_user(val, p))
			return -EFAULT;
		if (val != -1) {
			spin_lock_irqsave(&s->lock, flags);
			if (val)
				s->ctrl |= CTRL_XCTL1;
			else
				s->ctrl &= ~CTRL_XCTL1;
			outl(s->ctrl, s->io+ES1370_REG_CONTROL);
			spin_unlock_irqrestore(&s->lock, flags);
		}
		return put_user((s->ctrl & CTRL_XCTL1) ? 1 : 0, p);
	}
        if (cmd == SOUND_MIXER_INFO) {
		mixer_info info;
		strncpy(info.id, "ES1370", sizeof(info.id));
		strncpy(info.name, "Ensoniq ES1370", sizeof(info.name));
		info.modify_counter = s->mix.modcnt;
		if (copy_to_user((void __user *)arg, &info, sizeof(info)))
			return -EFAULT;
		return 0;
	}
	if (cmd == SOUND_OLD_MIXER_INFO) {
		_old_mixer_info info;
		strncpy(info.id, "ES1370", sizeof(info.id));
		strncpy(info.name, "Ensoniq ES1370", sizeof(info.name));
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
			return put_user(s->mix.recsrc, p);
			
                case SOUND_MIXER_DEVMASK: /* Arg contains a bit for each supported device */
			val = SOUND_MASK_IMIX;
			for (i = 0; i < SOUND_MIXER_NRDEVICES; i++)
				if (mixtable[i].avail)
					val |= 1 << i;
			return put_user(val, p);

                case SOUND_MIXER_RECMASK: /* Arg contains a bit for each supported recording source */
			for (val = i = 0; i < SOUND_MIXER_NRDEVICES; i++)
				if (mixtable[i].recmask)
					val |= 1 << i;
			return put_user(val, p);
			
                case SOUND_MIXER_STEREODEVS: /* Mixer channels supporting stereo */
			for (val = i = 0; i < SOUND_MIXER_NRDEVICES; i++)
				if (mixtable[i].stereo)
					val |= 1 << i;
			return put_user(val, p);
			
                case SOUND_MIXER_CAPS:
			return put_user(0, p);
		
		case SOUND_MIXER_IMIX:
			return put_user(s->mix.imix, p);

		default:
			i = _IOC_NR(cmd);
                        if (i >= SOUND_MIXER_NRDEVICES || !mixtable[i].avail)
                                return -EINVAL;
			return put_user(s->mix.vol[mixtable[i].volidx], p);
		}
	}
        if (_SIOC_DIR(cmd) != (_SIOC_READ|_SIOC_WRITE)) 
		return -EINVAL;
	s->mix.modcnt++;
	switch (_IOC_NR(cmd)) {

	case SOUND_MIXER_IMIX:
		if (get_user(s->mix.imix, p))
			return -EFAULT;
		set_recsrc(s, s->mix.recsrc);
		return 0;

	case SOUND_MIXER_RECSRC: /* Arg contains a bit for each recording source */
		if (get_user(val, p))
			return -EFAULT;
		set_recsrc(s, val);
		return 0;

	default:
		i = _IOC_NR(cmd);
		if (i >= SOUND_MIXER_NRDEVICES || !mixtable[i].avail)
			return -EINVAL;
		if (get_user(val, p))
			return -EFAULT;
		l = val & 0xff;
		if (l > 100)
			l = 100;
		if (mixtable[i].stereo) {
			r = (val >> 8) & 0xff;
			if (r > 100)
				r = 100;
			if (l < 7) {
				rl = 0x80;
				l = 0;
			} else {
				rl = 31 - ((l - 7) / 3);
				l = (31 - rl) * 3 + 7;
			}
			if (r < 7) {
				rr = 0x80;
				r = 0;
			} else {
				rr =  31 - ((r - 7) / 3);
				r = (31 - rr) * 3 + 7;
			}
			wrcodec(s, mixtable[i].right, rr);
		} else { 
			if (mixtable[i].left == 15) {
				if (l < 2) {
					rr = rl = 0x80;
					r = l = 0;
				} else {
					rl = 7 - ((l - 2) / 14);
					r = l = (7 - rl) * 14 + 2;
				}
			} else {
				if (l < 7) {
					rl = 0x80;
					r = l = 0;
				} else {
					rl = 31 - ((l - 7) / 3);
					r = l = (31 - rl) * 3 + 7;
				}
			}
		}
		wrcodec(s, mixtable[i].left, rl);
#ifdef OSS_DOCUMENTED_MIXER_SEMANTICS
		s->mix.vol[mixtable[i].volidx] = ((unsigned int)r << 8) | l;
#else
		s->mix.vol[mixtable[i].volidx] = val;
#endif
                return put_user(s->mix.vol[mixtable[i].volidx], p);
	}
}

/* --------------------------------------------------------------------- */

static int es1370_open_mixdev(struct inode *inode, struct file *file)
{
	unsigned int minor = iminor(inode);
	struct list_head *list;
	struct es1370_state *s;

	for (list = devs.next; ; list = list->next) {
		if (list == &devs)
			return -ENODEV;
		s = list_entry(list, struct es1370_state, devs);
		if (s->dev_mixer == minor)
			break;
	}
       	VALIDATE_STATE(s);
	file->private_data = s;
	return nonseekable_open(inode, file);
}

static int es1370_release_mixdev(struct inode *inode, struct file *file)
{
	struct es1370_state *s = (struct es1370_state *)file->private_data;
	
	VALIDATE_STATE(s);
	return 0;
}

static int es1370_ioctl_mixdev(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	return mixer_ioctl((struct es1370_state *)file->private_data, cmd, arg);
}

static /*const*/ struct file_operations es1370_mixer_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.ioctl		= es1370_ioctl_mixdev,
	.open		= es1370_open_mixdev,
	.release	= es1370_release_mixdev,
};

/* --------------------------------------------------------------------- */

static int drain_dac1(struct es1370_state *s, int nonblock)
{
	DECLARE_WAITQUEUE(wait, current);
	unsigned long flags;
	int count, tmo;
	
	if (s->dma_dac1.mapped || !s->dma_dac1.ready)
		return 0;
        add_wait_queue(&s->dma_dac1.wait, &wait);
        for (;;) {
		__set_current_state(TASK_INTERRUPTIBLE);
                spin_lock_irqsave(&s->lock, flags);
		count = s->dma_dac1.count;
                spin_unlock_irqrestore(&s->lock, flags);
		if (count <= 0)
			break;
		if (signal_pending(current))
                        break;
                if (nonblock) {
                        remove_wait_queue(&s->dma_dac1.wait, &wait);
                        set_current_state(TASK_RUNNING);
                        return -EBUSY;
                }
		tmo = 3 * HZ * (count + s->dma_dac1.fragsize) / 2
			/ dac1_samplerate[(s->ctrl & CTRL_WTSRSEL) >> CTRL_SH_WTSRSEL];
		tmo >>= sample_shift[(s->sctrl & SCTRL_P1FMT) >> SCTRL_SH_P1FMT];
		if (!schedule_timeout(tmo + 1))
			DBG(printk(KERN_DEBUG "es1370: dma timed out??\n");)
        }
        remove_wait_queue(&s->dma_dac1.wait, &wait);
        set_current_state(TASK_RUNNING);
        if (signal_pending(current))
                return -ERESTARTSYS;
        return 0;
}

static int drain_dac2(struct es1370_state *s, int nonblock)
{
	DECLARE_WAITQUEUE(wait, current);
	unsigned long flags;
	int count, tmo;

	if (s->dma_dac2.mapped || !s->dma_dac2.ready)
		return 0;
        add_wait_queue(&s->dma_dac2.wait, &wait);
        for (;;) {
		__set_current_state(TASK_INTERRUPTIBLE);
                spin_lock_irqsave(&s->lock, flags);
		count = s->dma_dac2.count;
                spin_unlock_irqrestore(&s->lock, flags);
		if (count <= 0)
			break;
		if (signal_pending(current))
                        break;
                if (nonblock) {
                        remove_wait_queue(&s->dma_dac2.wait, &wait);
                        set_current_state(TASK_RUNNING);
                        return -EBUSY;
                }
		tmo = 3 * HZ * (count + s->dma_dac2.fragsize) / 2
			/ DAC2_DIVTOSR((s->ctrl & CTRL_PCLKDIV) >> CTRL_SH_PCLKDIV);
		tmo >>= sample_shift[(s->sctrl & SCTRL_P2FMT) >> SCTRL_SH_P2FMT];
		if (!schedule_timeout(tmo + 1))
			DBG(printk(KERN_DEBUG "es1370: dma timed out??\n");)
        }
        remove_wait_queue(&s->dma_dac2.wait, &wait);
        set_current_state(TASK_RUNNING);
        if (signal_pending(current))
                return -ERESTARTSYS;
        return 0;
}

/* --------------------------------------------------------------------- */

static ssize_t es1370_read(struct file *file, char __user *buffer, size_t count, loff_t *ppos)
{
	struct es1370_state *s = (struct es1370_state *)file->private_data;
	DECLARE_WAITQUEUE(wait, current);
	ssize_t ret = 0;
	unsigned long flags;
	unsigned swptr;
	int cnt;

	VALIDATE_STATE(s);
	if (s->dma_adc.mapped)
		return -ENXIO;
	if (!access_ok(VERIFY_WRITE, buffer, count))
		return -EFAULT;
	mutex_lock(&s->mutex);
	if (!s->dma_adc.ready && (ret = prog_dmabuf_adc(s)))
		goto out;
        
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
				goto out;
			}
			mutex_unlock(&s->mutex);
			schedule();
			if (signal_pending(current)) {
				if (!ret)
					ret = -ERESTARTSYS;
				goto out;
			}
			mutex_lock(&s->mutex);
			if (s->dma_adc.mapped)
			{
				ret = -ENXIO;
				goto out;
			}
			continue;
		}
		if (copy_to_user(buffer, s->dma_adc.rawbuf + swptr, cnt)) {
			if (!ret)
				ret = -EFAULT;
			goto out;
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
out:
	mutex_unlock(&s->mutex);
        remove_wait_queue(&s->dma_adc.wait, &wait);
	set_current_state(TASK_RUNNING);
	return ret;
}

static ssize_t es1370_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
	struct es1370_state *s = (struct es1370_state *)file->private_data;
	DECLARE_WAITQUEUE(wait, current);
	ssize_t ret = 0;
	unsigned long flags;
	unsigned swptr;
	int cnt;

	VALIDATE_STATE(s);
	if (s->dma_dac2.mapped)
		return -ENXIO;
	if (!access_ok(VERIFY_READ, buffer, count))
		return -EFAULT;
	mutex_lock(&s->mutex);
	if (!s->dma_dac2.ready && (ret = prog_dmabuf_dac2(s)))
		goto out;
	ret = 0;
        add_wait_queue(&s->dma_dac2.wait, &wait);
	while (count > 0) {
		spin_lock_irqsave(&s->lock, flags);
		if (s->dma_dac2.count < 0) {
			s->dma_dac2.count = 0;
			s->dma_dac2.swptr = s->dma_dac2.hwptr;
		}
		swptr = s->dma_dac2.swptr;
		cnt = s->dma_dac2.dmasize-swptr;
		if (s->dma_dac2.count + cnt > s->dma_dac2.dmasize)
			cnt = s->dma_dac2.dmasize - s->dma_dac2.count;
		if (cnt <= 0)
			__set_current_state(TASK_INTERRUPTIBLE);
		spin_unlock_irqrestore(&s->lock, flags);
		if (cnt > count)
			cnt = count;
		if (cnt <= 0) {
			if (s->dma_dac2.enabled)
				start_dac2(s);
			if (file->f_flags & O_NONBLOCK) {
				if (!ret)
					ret = -EAGAIN;
				goto out;
			}
			mutex_unlock(&s->mutex);
			schedule();
			if (signal_pending(current)) {
				if (!ret)
					ret = -ERESTARTSYS;
				goto out;	
			}
			mutex_lock(&s->mutex);
			if (s->dma_dac2.mapped)
			{
			ret = -ENXIO;
			goto out;
			}
			continue;
		}
		if (copy_from_user(s->dma_dac2.rawbuf + swptr, buffer, cnt)) {
			if (!ret)
				ret = -EFAULT;
			goto out;
		}
		swptr = (swptr + cnt) % s->dma_dac2.dmasize;
		spin_lock_irqsave(&s->lock, flags);
		s->dma_dac2.swptr = swptr;
		s->dma_dac2.count += cnt;
		s->dma_dac2.endcleared = 0;
		spin_unlock_irqrestore(&s->lock, flags);
		count -= cnt;
		buffer += cnt;
		ret += cnt;
		if (s->dma_dac2.enabled)
			start_dac2(s);
	}
out:
	mutex_unlock(&s->mutex);
        remove_wait_queue(&s->dma_dac2.wait, &wait);
	set_current_state(TASK_RUNNING);
	return ret;
}

/* No kernel lock - we have our own spinlock */
static unsigned int es1370_poll(struct file *file, struct poll_table_struct *wait)
{
	struct es1370_state *s = (struct es1370_state *)file->private_data;
	unsigned long flags;
	unsigned int mask = 0;

	VALIDATE_STATE(s);
	if (file->f_mode & FMODE_WRITE) {
		if (!s->dma_dac2.ready && prog_dmabuf_dac2(s))
			return 0;
		poll_wait(file, &s->dma_dac2.wait, wait);
	}
	if (file->f_mode & FMODE_READ) {
		if (!s->dma_adc.ready && prog_dmabuf_adc(s))
			return 0;
		poll_wait(file, &s->dma_adc.wait, wait);
	}
	spin_lock_irqsave(&s->lock, flags);
	es1370_update_ptr(s);
	if (file->f_mode & FMODE_READ) {
		if (s->dma_adc.count >= (signed)s->dma_adc.fragsize)
			mask |= POLLIN | POLLRDNORM;
	}
	if (file->f_mode & FMODE_WRITE) {
		if (s->dma_dac2.mapped) {
			if (s->dma_dac2.count >= (signed)s->dma_dac2.fragsize) 
				mask |= POLLOUT | POLLWRNORM;
		} else {
			if ((signed)s->dma_dac2.dmasize >= s->dma_dac2.count + (signed)s->dma_dac2.fragsize)
				mask |= POLLOUT | POLLWRNORM;
		}
	}
	spin_unlock_irqrestore(&s->lock, flags);
	return mask;
}

static int es1370_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct es1370_state *s = (struct es1370_state *)file->private_data;
	struct dmabuf *db;
	int ret = 0;
	unsigned long size;

	VALIDATE_STATE(s);
	lock_kernel();
	mutex_lock(&s->mutex);
	if (vma->vm_flags & VM_WRITE) {
		if ((ret = prog_dmabuf_dac2(s)) != 0) {
			goto out;
		}
		db = &s->dma_dac2;
	} else if (vma->vm_flags & VM_READ) {
		if ((ret = prog_dmabuf_adc(s)) != 0) {
			goto out;
		}
		db = &s->dma_adc;
	} else  {
		ret = -EINVAL;
		goto out;
	}
	if (vma->vm_pgoff != 0) {
		ret = -EINVAL;
		goto out;
	}
	size = vma->vm_end - vma->vm_start;
	if (size > (PAGE_SIZE << db->buforder)) {
		ret = -EINVAL;
		goto out;
	}
	if (remap_pfn_range(vma, vma->vm_start,
				virt_to_phys(db->rawbuf) >> PAGE_SHIFT,
				size, vma->vm_page_prot)) {
		ret = -EAGAIN;
		goto out;
	}
	db->mapped = 1;
out:
	mutex_unlock(&s->mutex);
	unlock_kernel();
	return ret;
}

static int es1370_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	struct es1370_state *s = (struct es1370_state *)file->private_data;
	unsigned long flags;
        audio_buf_info abinfo;
        count_info cinfo;
	int count;
	int val, mapped, ret;
	void __user *argp = (void __user *)arg;
	int __user *p = argp;

	VALIDATE_STATE(s);
        mapped = ((file->f_mode & FMODE_WRITE) && s->dma_dac2.mapped) ||
		((file->f_mode & FMODE_READ) && s->dma_adc.mapped);
	switch (cmd) {
	case OSS_GETVERSION:
		return put_user(SOUND_VERSION, p);

	case SNDCTL_DSP_SYNC:
		if (file->f_mode & FMODE_WRITE)
			return drain_dac2(s, 0/*file->f_flags & O_NONBLOCK*/);
		return 0;
		
	case SNDCTL_DSP_SETDUPLEX:
		return 0;

	case SNDCTL_DSP_GETCAPS:
		return put_user(DSP_CAP_DUPLEX | DSP_CAP_REALTIME | DSP_CAP_TRIGGER | DSP_CAP_MMAP, p);
		
        case SNDCTL_DSP_RESET:
		if (file->f_mode & FMODE_WRITE) {
			stop_dac2(s);
			synchronize_irq(s->irq);
			s->dma_dac2.swptr = s->dma_dac2.hwptr = s->dma_dac2.count = s->dma_dac2.total_bytes = 0;
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
			if (s->open_mode & (~file->f_mode) & (FMODE_READ|FMODE_WRITE))
				return -EINVAL;
			if (val < 4000)
				val = 4000;
			if (val > 50000)
				val = 50000;
			stop_adc(s);
			stop_dac2(s);
			s->dma_adc.ready = s->dma_dac2.ready = 0;
			spin_lock_irqsave(&s->lock, flags);
			s->ctrl = (s->ctrl & ~CTRL_PCLKDIV) | (DAC2_SRTODIV(val) << CTRL_SH_PCLKDIV);
			outl(s->ctrl, s->io+ES1370_REG_CONTROL);
			spin_unlock_irqrestore(&s->lock, flags);
		}
		return put_user(DAC2_DIVTOSR((s->ctrl & CTRL_PCLKDIV) >> CTRL_SH_PCLKDIV), p);
		
        case SNDCTL_DSP_STEREO:
                if (get_user(val, p))
			return -EFAULT;
		if (file->f_mode & FMODE_READ) {
			stop_adc(s);
			s->dma_adc.ready = 0;
			spin_lock_irqsave(&s->lock, flags);
			if (val)
				s->sctrl |= SCTRL_R1SMB;
			else
				s->sctrl &= ~SCTRL_R1SMB;
			outl(s->sctrl, s->io+ES1370_REG_SERIAL_CONTROL);
			spin_unlock_irqrestore(&s->lock, flags);
		}
		if (file->f_mode & FMODE_WRITE) {
			stop_dac2(s);
			s->dma_dac2.ready = 0;
			spin_lock_irqsave(&s->lock, flags);
			if (val)
				s->sctrl |= SCTRL_P2SMB;
			else
				s->sctrl &= ~SCTRL_P2SMB;
			outl(s->sctrl, s->io+ES1370_REG_SERIAL_CONTROL);
			spin_unlock_irqrestore(&s->lock, flags);
                }
		return 0;

        case SNDCTL_DSP_CHANNELS:
                if (get_user(val, p))
			return -EFAULT;
		if (val != 0) {
			if (file->f_mode & FMODE_READ) {
				stop_adc(s);
				s->dma_adc.ready = 0;
				spin_lock_irqsave(&s->lock, flags);
				if (val >= 2)
					s->sctrl |= SCTRL_R1SMB;
				else
					s->sctrl &= ~SCTRL_R1SMB;
				outl(s->sctrl, s->io+ES1370_REG_SERIAL_CONTROL);
				spin_unlock_irqrestore(&s->lock, flags);
			}
			if (file->f_mode & FMODE_WRITE) {
				stop_dac2(s);
				s->dma_dac2.ready = 0;
				spin_lock_irqsave(&s->lock, flags);
				if (val >= 2)
					s->sctrl |= SCTRL_P2SMB;
				else
					s->sctrl &= ~SCTRL_P2SMB;
				outl(s->sctrl, s->io+ES1370_REG_SERIAL_CONTROL);
				spin_unlock_irqrestore(&s->lock, flags);
			}
		}
		return put_user((s->sctrl & ((file->f_mode & FMODE_READ) ? SCTRL_R1SMB : SCTRL_P2SMB)) ? 2 : 1, p);
		
	case SNDCTL_DSP_GETFMTS: /* Returns a mask */
                return put_user(AFMT_S16_LE|AFMT_U8, p);
		
	case SNDCTL_DSP_SETFMT: /* Selects ONE fmt*/
		if (get_user(val, p))
			return -EFAULT;
		if (val != AFMT_QUERY) {
			if (file->f_mode & FMODE_READ) {
				stop_adc(s);
				s->dma_adc.ready = 0;
				spin_lock_irqsave(&s->lock, flags);
				if (val == AFMT_S16_LE)
					s->sctrl |= SCTRL_R1SEB;
				else
					s->sctrl &= ~SCTRL_R1SEB;
				outl(s->sctrl, s->io+ES1370_REG_SERIAL_CONTROL);
				spin_unlock_irqrestore(&s->lock, flags);
			}
			if (file->f_mode & FMODE_WRITE) {
				stop_dac2(s);
				s->dma_dac2.ready = 0;
				spin_lock_irqsave(&s->lock, flags);
				if (val == AFMT_S16_LE)
					s->sctrl |= SCTRL_P2SEB;
				else
					s->sctrl &= ~SCTRL_P2SEB;
				outl(s->sctrl, s->io+ES1370_REG_SERIAL_CONTROL);
				spin_unlock_irqrestore(&s->lock, flags);
			}
		}
		return put_user((s->sctrl & ((file->f_mode & FMODE_READ) ? SCTRL_R1SEB : SCTRL_P2SEB)) ? 
				AFMT_S16_LE : AFMT_U8, p);
		
	case SNDCTL_DSP_POST:
                return 0;

        case SNDCTL_DSP_GETTRIGGER:
		val = 0;
		if (file->f_mode & FMODE_READ && s->ctrl & CTRL_ADC_EN) 
			val |= PCM_ENABLE_INPUT;
		if (file->f_mode & FMODE_WRITE && s->ctrl & CTRL_DAC2_EN) 
			val |= PCM_ENABLE_OUTPUT;
		return put_user(val, p);
		
	case SNDCTL_DSP_SETTRIGGER:
		if (get_user(val, p))
			return -EFAULT;
		if (file->f_mode & FMODE_READ) {
			if (val & PCM_ENABLE_INPUT) {
				if (!s->dma_adc.ready && (ret = prog_dmabuf_adc(s)))
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
				if (!s->dma_dac2.ready && (ret = prog_dmabuf_dac2(s)))
					return ret;
				s->dma_dac2.enabled = 1;
				start_dac2(s);
			} else {
				s->dma_dac2.enabled = 0;
				stop_dac2(s);
			}
		}
		return 0;

	case SNDCTL_DSP_GETOSPACE:
		if (!(file->f_mode & FMODE_WRITE))
			return -EINVAL;
		if (!s->dma_dac2.ready && (val = prog_dmabuf_dac2(s)) != 0)
			return val;
		spin_lock_irqsave(&s->lock, flags);
		es1370_update_ptr(s);
		abinfo.fragsize = s->dma_dac2.fragsize;
		count = s->dma_dac2.count;
		if (count < 0)
			count = 0;
                abinfo.bytes = s->dma_dac2.dmasize - count;
                abinfo.fragstotal = s->dma_dac2.numfrag;
                abinfo.fragments = abinfo.bytes >> s->dma_dac2.fragshift;      
		spin_unlock_irqrestore(&s->lock, flags);
		return copy_to_user(argp, &abinfo, sizeof(abinfo)) ? -EFAULT : 0;

	case SNDCTL_DSP_GETISPACE:
		if (!(file->f_mode & FMODE_READ))
			return -EINVAL;
		if (!s->dma_adc.ready && (val = prog_dmabuf_adc(s)) != 0)
			return val;
		spin_lock_irqsave(&s->lock, flags);
		es1370_update_ptr(s);
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
		if (!s->dma_dac2.ready && (val = prog_dmabuf_dac2(s)) != 0)
			return val;
		spin_lock_irqsave(&s->lock, flags);
		es1370_update_ptr(s);
                count = s->dma_dac2.count;
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
		es1370_update_ptr(s);
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
		if (!s->dma_dac2.ready && (val = prog_dmabuf_dac2(s)) != 0)
			return val;
		spin_lock_irqsave(&s->lock, flags);
		es1370_update_ptr(s);
                cinfo.bytes = s->dma_dac2.total_bytes;
		count = s->dma_dac2.count;
		if (count < 0)
			count = 0;
                cinfo.blocks = count >> s->dma_dac2.fragshift;
                cinfo.ptr = s->dma_dac2.hwptr;
		if (s->dma_dac2.mapped)
			s->dma_dac2.count &= s->dma_dac2.fragsize-1;
		spin_unlock_irqrestore(&s->lock, flags);
		if (copy_to_user(argp, &cinfo, sizeof(cinfo)))
			return -EFAULT;
		return 0;

        case SNDCTL_DSP_GETBLKSIZE:
		if (file->f_mode & FMODE_WRITE) {
			if ((val = prog_dmabuf_dac2(s)))
				return val;
			return put_user(s->dma_dac2.fragsize, p);
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
			s->dma_dac2.ossfragshift = val & 0xffff;
			s->dma_dac2.ossmaxfrags = (val >> 16) & 0xffff;
			if (s->dma_dac2.ossfragshift < 4)
				s->dma_dac2.ossfragshift = 4;
			if (s->dma_dac2.ossfragshift > 15)
				s->dma_dac2.ossfragshift = 15;
			if (s->dma_dac2.ossmaxfrags < 4)
				s->dma_dac2.ossmaxfrags = 4;
		}
		return 0;

        case SNDCTL_DSP_SUBDIVIDE:
		if ((file->f_mode & FMODE_READ && s->dma_adc.subdivision) ||
		    (file->f_mode & FMODE_WRITE && s->dma_dac2.subdivision))
			return -EINVAL;
                if (get_user(val, p))
			return -EFAULT;
		if (val != 1 && val != 2 && val != 4)
			return -EINVAL;
		if (file->f_mode & FMODE_READ)
			s->dma_adc.subdivision = val;
		if (file->f_mode & FMODE_WRITE)
			s->dma_dac2.subdivision = val;
		return 0;

        case SOUND_PCM_READ_RATE:
		return put_user(DAC2_DIVTOSR((s->ctrl & CTRL_PCLKDIV) >> CTRL_SH_PCLKDIV), p);

        case SOUND_PCM_READ_CHANNELS:
		return put_user((s->sctrl & ((file->f_mode & FMODE_READ) ? SCTRL_R1SMB : SCTRL_P2SMB)) ?
				2 : 1, p);

        case SOUND_PCM_READ_BITS:
		return put_user((s->sctrl & ((file->f_mode & FMODE_READ) ? SCTRL_R1SEB : SCTRL_P2SEB)) ? 
				16 : 8, p);

        case SOUND_PCM_WRITE_FILTER:
        case SNDCTL_DSP_SETSYNCRO:
        case SOUND_PCM_READ_FILTER:
                return -EINVAL;
		
	}
	return mixer_ioctl(s, cmd, arg);
}

static int es1370_open(struct inode *inode, struct file *file)
{
	unsigned int minor = iminor(inode);
	DECLARE_WAITQUEUE(wait, current);
	unsigned long flags;
	struct list_head *list;
	struct es1370_state *s;

	for (list = devs.next; ; list = list->next) {
		if (list == &devs)
			return -ENODEV;
		s = list_entry(list, struct es1370_state, devs);
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
	spin_lock_irqsave(&s->lock, flags);
	if (!(s->open_mode & (FMODE_READ|FMODE_WRITE)))
		s->ctrl = (s->ctrl & ~CTRL_PCLKDIV) | (DAC2_SRTODIV(8000) << CTRL_SH_PCLKDIV);
	if (file->f_mode & FMODE_READ) {
		s->dma_adc.ossfragshift = s->dma_adc.ossmaxfrags = s->dma_adc.subdivision = 0;
		s->dma_adc.enabled = 1;
		s->sctrl &= ~SCTRL_R1FMT;
		if ((minor & 0xf) == SND_DEV_DSP16)
			s->sctrl |= ES1370_FMT_S16_MONO << SCTRL_SH_R1FMT;
		else
			s->sctrl |= ES1370_FMT_U8_MONO << SCTRL_SH_R1FMT;
	}
	if (file->f_mode & FMODE_WRITE) {
		s->dma_dac2.ossfragshift = s->dma_dac2.ossmaxfrags = s->dma_dac2.subdivision = 0;
		s->dma_dac2.enabled = 1;
		s->sctrl &= ~SCTRL_P2FMT;
		if ((minor & 0xf) == SND_DEV_DSP16)
			s->sctrl |= ES1370_FMT_S16_MONO << SCTRL_SH_P2FMT;
		else
			s->sctrl |= ES1370_FMT_U8_MONO << SCTRL_SH_P2FMT;
	}
	outl(s->sctrl, s->io+ES1370_REG_SERIAL_CONTROL);
	outl(s->ctrl, s->io+ES1370_REG_CONTROL);
	spin_unlock_irqrestore(&s->lock, flags);
	s->open_mode |= file->f_mode & (FMODE_READ | FMODE_WRITE);
	mutex_unlock(&s->open_mutex);
	mutex_init(&s->mutex);
	return nonseekable_open(inode, file);
}

static int es1370_release(struct inode *inode, struct file *file)
{
	struct es1370_state *s = (struct es1370_state *)file->private_data;

	VALIDATE_STATE(s);
	lock_kernel();
	if (file->f_mode & FMODE_WRITE)
		drain_dac2(s, file->f_flags & O_NONBLOCK);
	mutex_lock(&s->open_mutex);
	if (file->f_mode & FMODE_WRITE) {
		stop_dac2(s);
		synchronize_irq(s->irq);
		dealloc_dmabuf(s, &s->dma_dac2);
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

static /*const*/ struct file_operations es1370_audio_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.read		= es1370_read,
	.write		= es1370_write,
	.poll		= es1370_poll,
	.ioctl		= es1370_ioctl,
	.mmap		= es1370_mmap,
	.open		= es1370_open,
	.release	= es1370_release,
};

/* --------------------------------------------------------------------- */

static ssize_t es1370_write_dac(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
	struct es1370_state *s = (struct es1370_state *)file->private_data;
	DECLARE_WAITQUEUE(wait, current);
	ssize_t ret = 0;
	unsigned long flags;
	unsigned swptr;
	int cnt;

	VALIDATE_STATE(s);
	if (s->dma_dac1.mapped)
		return -ENXIO;
	if (!s->dma_dac1.ready && (ret = prog_dmabuf_dac1(s)))
		return ret;
	if (!access_ok(VERIFY_READ, buffer, count))
		return -EFAULT;
        add_wait_queue(&s->dma_dac1.wait, &wait);
	while (count > 0) {
		spin_lock_irqsave(&s->lock, flags);
		if (s->dma_dac1.count < 0) {
			s->dma_dac1.count = 0;
			s->dma_dac1.swptr = s->dma_dac1.hwptr;
		}
		swptr = s->dma_dac1.swptr;
		cnt = s->dma_dac1.dmasize-swptr;
		if (s->dma_dac1.count + cnt > s->dma_dac1.dmasize)
			cnt = s->dma_dac1.dmasize - s->dma_dac1.count;
		if (cnt <= 0)
			__set_current_state(TASK_INTERRUPTIBLE);
		spin_unlock_irqrestore(&s->lock, flags);
		if (cnt > count)
			cnt = count;
		if (cnt <= 0) {
			if (s->dma_dac1.enabled)
				start_dac1(s);
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
		if (copy_from_user(s->dma_dac1.rawbuf + swptr, buffer, cnt)) {
			if (!ret)
				ret = -EFAULT;
			break;
		}
		swptr = (swptr + cnt) % s->dma_dac1.dmasize;
		spin_lock_irqsave(&s->lock, flags);
		s->dma_dac1.swptr = swptr;
		s->dma_dac1.count += cnt;
		s->dma_dac1.endcleared = 0;
		spin_unlock_irqrestore(&s->lock, flags);
		count -= cnt;
		buffer += cnt;
		ret += cnt;
		if (s->dma_dac1.enabled)
			start_dac1(s);
	}
        remove_wait_queue(&s->dma_dac1.wait, &wait);
	set_current_state(TASK_RUNNING);
	return ret;
}

/* No kernel lock - we have our own spinlock */
static unsigned int es1370_poll_dac(struct file *file, struct poll_table_struct *wait)
{
	struct es1370_state *s = (struct es1370_state *)file->private_data;
	unsigned long flags;
	unsigned int mask = 0;

	VALIDATE_STATE(s);
	if (!s->dma_dac1.ready && prog_dmabuf_dac1(s))
		return 0;
	poll_wait(file, &s->dma_dac1.wait, wait);
	spin_lock_irqsave(&s->lock, flags);
	es1370_update_ptr(s);
	if (s->dma_dac1.mapped) {
		if (s->dma_dac1.count >= (signed)s->dma_dac1.fragsize)
			mask |= POLLOUT | POLLWRNORM;
	} else {
		if ((signed)s->dma_dac1.dmasize >= s->dma_dac1.count + (signed)s->dma_dac1.fragsize)
			mask |= POLLOUT | POLLWRNORM;
	}
	spin_unlock_irqrestore(&s->lock, flags);
	return mask;
}

static int es1370_mmap_dac(struct file *file, struct vm_area_struct *vma)
{
	struct es1370_state *s = (struct es1370_state *)file->private_data;
	int ret;
	unsigned long size;

	VALIDATE_STATE(s);
	if (!(vma->vm_flags & VM_WRITE))
		return -EINVAL;
	lock_kernel();
	if ((ret = prog_dmabuf_dac1(s)) != 0)
		goto out;
	ret = -EINVAL;
	if (vma->vm_pgoff != 0)
		goto out;
	size = vma->vm_end - vma->vm_start;
	if (size > (PAGE_SIZE << s->dma_dac1.buforder))
		goto out;
	ret = -EAGAIN;
	if (remap_pfn_range(vma, vma->vm_start,
			virt_to_phys(s->dma_dac1.rawbuf) >> PAGE_SHIFT,
			size, vma->vm_page_prot))
		goto out;
	s->dma_dac1.mapped = 1;
	ret = 0;
out:
	unlock_kernel();
	return ret;
}

static int es1370_ioctl_dac(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	struct es1370_state *s = (struct es1370_state *)file->private_data;
	unsigned long flags;
        audio_buf_info abinfo;
        count_info cinfo;
	int count;
	unsigned ctrl;
	int val, ret;
	int __user *p = (int __user *)arg;

	VALIDATE_STATE(s);
	switch (cmd) {
	case OSS_GETVERSION:
		return put_user(SOUND_VERSION, p);

	case SNDCTL_DSP_SYNC:
		return drain_dac1(s, 0/*file->f_flags & O_NONBLOCK*/);
		
	case SNDCTL_DSP_SETDUPLEX:
		return -EINVAL;

	case SNDCTL_DSP_GETCAPS:
		return put_user(DSP_CAP_REALTIME | DSP_CAP_TRIGGER | DSP_CAP_MMAP, p);
		
        case SNDCTL_DSP_RESET:
		stop_dac1(s);
		synchronize_irq(s->irq);
		s->dma_dac1.swptr = s->dma_dac1.hwptr = s->dma_dac1.count = s->dma_dac1.total_bytes = 0;
		return 0;

        case SNDCTL_DSP_SPEED:
                if (get_user(val, p))
			return -EFAULT;
		if (val >= 0) {
			stop_dac1(s);
			s->dma_dac1.ready = 0;
			for (ctrl = 0; ctrl <= 2; ctrl++)
				if (val < (dac1_samplerate[ctrl] + dac1_samplerate[ctrl+1]) / 2)
					break;
			spin_lock_irqsave(&s->lock, flags);
			s->ctrl = (s->ctrl & ~CTRL_WTSRSEL) | (ctrl << CTRL_SH_WTSRSEL);
			outl(s->ctrl, s->io+ES1370_REG_CONTROL);
			spin_unlock_irqrestore(&s->lock, flags);
		}
		return put_user(dac1_samplerate[(s->ctrl & CTRL_WTSRSEL) >> CTRL_SH_WTSRSEL], p);
		
        case SNDCTL_DSP_STEREO:
                if (get_user(val, p))
			return -EFAULT;
		stop_dac1(s);
		s->dma_dac1.ready = 0;
		spin_lock_irqsave(&s->lock, flags);
		if (val)
			s->sctrl |= SCTRL_P1SMB;
		else
			s->sctrl &= ~SCTRL_P1SMB;
		outl(s->sctrl, s->io+ES1370_REG_SERIAL_CONTROL);
		spin_unlock_irqrestore(&s->lock, flags);
		return 0;

        case SNDCTL_DSP_CHANNELS:
                if (get_user(val, p))
			return -EFAULT;
		if (val != 0) {
			if (s->dma_dac1.mapped)
				return -EINVAL;
			stop_dac1(s);
			s->dma_dac1.ready = 0;
			spin_lock_irqsave(&s->lock, flags);
			if (val >= 2)
				s->sctrl |= SCTRL_P1SMB;
			else
				s->sctrl &= ~SCTRL_P1SMB;
			outl(s->sctrl, s->io+ES1370_REG_SERIAL_CONTROL);
			spin_unlock_irqrestore(&s->lock, flags);
		}
		return put_user((s->sctrl & SCTRL_P1SMB) ? 2 : 1, p);
		
        case SNDCTL_DSP_GETFMTS: /* Returns a mask */
                return put_user(AFMT_S16_LE|AFMT_U8, p);
		
        case SNDCTL_DSP_SETFMT: /* Selects ONE fmt*/
		if (get_user(val, p))
			return -EFAULT;
		if (val != AFMT_QUERY) {
			stop_dac1(s);
			s->dma_dac1.ready = 0;
			spin_lock_irqsave(&s->lock, flags);
			if (val == AFMT_S16_LE)
				s->sctrl |= SCTRL_P1SEB;
			else
				s->sctrl &= ~SCTRL_P1SEB;
			outl(s->sctrl, s->io+ES1370_REG_SERIAL_CONTROL);
			spin_unlock_irqrestore(&s->lock, flags);
		}
		return put_user((s->sctrl & SCTRL_P1SEB) ? AFMT_S16_LE : AFMT_U8, p);

        case SNDCTL_DSP_POST:
                return 0;

        case SNDCTL_DSP_GETTRIGGER:
		return put_user((s->ctrl & CTRL_DAC1_EN) ? PCM_ENABLE_OUTPUT : 0, p);
						
	case SNDCTL_DSP_SETTRIGGER:
		if (get_user(val, p))
			return -EFAULT;
		if (val & PCM_ENABLE_OUTPUT) {
			if (!s->dma_dac1.ready && (ret = prog_dmabuf_dac1(s)))
				return ret;
			s->dma_dac1.enabled = 1;
			start_dac1(s);
		} else {
			s->dma_dac1.enabled = 0;
			stop_dac1(s);
		}
		return 0;

	case SNDCTL_DSP_GETOSPACE:
		if (!s->dma_dac1.ready && (val = prog_dmabuf_dac1(s)) != 0)
			return val;
		spin_lock_irqsave(&s->lock, flags);
		es1370_update_ptr(s);
		abinfo.fragsize = s->dma_dac1.fragsize;
		count = s->dma_dac1.count;
		if (count < 0)
			count = 0;
                abinfo.bytes = s->dma_dac1.dmasize - count;
                abinfo.fragstotal = s->dma_dac1.numfrag;
                abinfo.fragments = abinfo.bytes >> s->dma_dac1.fragshift;      
		spin_unlock_irqrestore(&s->lock, flags);
		return copy_to_user((void __user *)arg, &abinfo, sizeof(abinfo)) ? -EFAULT : 0;

        case SNDCTL_DSP_NONBLOCK:
                file->f_flags |= O_NONBLOCK;
                return 0;

        case SNDCTL_DSP_GETODELAY:
		if (!s->dma_dac1.ready && (val = prog_dmabuf_dac1(s)) != 0)
			return val;
		spin_lock_irqsave(&s->lock, flags);
		es1370_update_ptr(s);
                count = s->dma_dac1.count;
		spin_unlock_irqrestore(&s->lock, flags);
		if (count < 0)
			count = 0;
		return put_user(count, p);

        case SNDCTL_DSP_GETOPTR:
		if (!s->dma_dac1.ready && (val = prog_dmabuf_dac1(s)) != 0)
			return val;
		spin_lock_irqsave(&s->lock, flags);
		es1370_update_ptr(s);
                cinfo.bytes = s->dma_dac1.total_bytes;
		count = s->dma_dac1.count;
		if (count < 0)
			count = 0;
                cinfo.blocks = count >> s->dma_dac1.fragshift;
                cinfo.ptr = s->dma_dac1.hwptr;
		if (s->dma_dac1.mapped)
			s->dma_dac1.count &= s->dma_dac1.fragsize-1;
		spin_unlock_irqrestore(&s->lock, flags);
		if (copy_to_user((void __user *)arg, &cinfo, sizeof(cinfo)))
			return -EFAULT;
		return 0;

        case SNDCTL_DSP_GETBLKSIZE:
		if ((val = prog_dmabuf_dac1(s)))
			return val;
                return put_user(s->dma_dac1.fragsize, p);

        case SNDCTL_DSP_SETFRAGMENT:
                if (get_user(val, p))
			return -EFAULT;
		s->dma_dac1.ossfragshift = val & 0xffff;
		s->dma_dac1.ossmaxfrags = (val >> 16) & 0xffff;
		if (s->dma_dac1.ossfragshift < 4)
			s->dma_dac1.ossfragshift = 4;
		if (s->dma_dac1.ossfragshift > 15)
			s->dma_dac1.ossfragshift = 15;
		if (s->dma_dac1.ossmaxfrags < 4)
			s->dma_dac1.ossmaxfrags = 4;
		return 0;

        case SNDCTL_DSP_SUBDIVIDE:
		if (s->dma_dac1.subdivision)
			return -EINVAL;
                if (get_user(val, p))
			return -EFAULT;
		if (val != 1 && val != 2 && val != 4)
			return -EINVAL;
		s->dma_dac1.subdivision = val;
		return 0;

        case SOUND_PCM_READ_RATE:
		return put_user(dac1_samplerate[(s->ctrl & CTRL_WTSRSEL) >> CTRL_SH_WTSRSEL], p);

        case SOUND_PCM_READ_CHANNELS:
		return put_user((s->sctrl & SCTRL_P1SMB) ? 2 : 1, p);

        case SOUND_PCM_READ_BITS:
		return put_user((s->sctrl & SCTRL_P1SEB) ? 16 : 8, p);

        case SOUND_PCM_WRITE_FILTER:
        case SNDCTL_DSP_SETSYNCRO:
        case SOUND_PCM_READ_FILTER:
                return -EINVAL;
		
	}
	return mixer_ioctl(s, cmd, arg);
}

static int es1370_open_dac(struct inode *inode, struct file *file)
{
	unsigned int minor = iminor(inode);
	DECLARE_WAITQUEUE(wait, current);
	unsigned long flags;
	struct list_head *list;
	struct es1370_state *s;

	for (list = devs.next; ; list = list->next) {
		if (list == &devs)
			return -ENODEV;
		s = list_entry(list, struct es1370_state, devs);
		if (!((s->dev_dac ^ minor) & ~0xf))
			break;
	}
       	VALIDATE_STATE(s);
       	/* we allow opening with O_RDWR, most programs do it although they will only write */
#if 0
	if (file->f_mode & FMODE_READ)
		return -EPERM;
#endif
	if (!(file->f_mode & FMODE_WRITE))
		return -EINVAL;
       	file->private_data = s;
	/* wait for device to become free */
	mutex_lock(&s->open_mutex);
	while (s->open_mode & FMODE_DAC) {
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
	s->dma_dac1.ossfragshift = s->dma_dac1.ossmaxfrags = s->dma_dac1.subdivision = 0;
	s->dma_dac1.enabled = 1;
	spin_lock_irqsave(&s->lock, flags);
	s->ctrl = (s->ctrl & ~CTRL_WTSRSEL) | (1 << CTRL_SH_WTSRSEL);
      	s->sctrl &= ~SCTRL_P1FMT;
	if ((minor & 0xf) == SND_DEV_DSP16)
		s->sctrl |= ES1370_FMT_S16_MONO << SCTRL_SH_P1FMT;
	else
		s->sctrl |= ES1370_FMT_U8_MONO << SCTRL_SH_P1FMT;
	outl(s->sctrl, s->io+ES1370_REG_SERIAL_CONTROL);
	outl(s->ctrl, s->io+ES1370_REG_CONTROL);
	spin_unlock_irqrestore(&s->lock, flags);
	s->open_mode |= FMODE_DAC;
	mutex_unlock(&s->open_mutex);
	return nonseekable_open(inode, file);
}

static int es1370_release_dac(struct inode *inode, struct file *file)
{
	struct es1370_state *s = (struct es1370_state *)file->private_data;

	VALIDATE_STATE(s);
	lock_kernel();
	drain_dac1(s, file->f_flags & O_NONBLOCK);
	mutex_lock(&s->open_mutex);
	stop_dac1(s);
	dealloc_dmabuf(s, &s->dma_dac1);
	s->open_mode &= ~FMODE_DAC;
	wake_up(&s->open_wait);
	mutex_unlock(&s->open_mutex);
	unlock_kernel();
	return 0;
}

static /*const*/ struct file_operations es1370_dac_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.write		= es1370_write_dac,
	.poll		= es1370_poll_dac,
	.ioctl		= es1370_ioctl_dac,
	.mmap		= es1370_mmap_dac,
	.open		= es1370_open_dac,
	.release	= es1370_release_dac,
};

/* --------------------------------------------------------------------- */

static ssize_t es1370_midi_read(struct file *file, char __user *buffer, size_t count, loff_t *ppos)
{
	struct es1370_state *s = (struct es1370_state *)file->private_data;
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

static ssize_t es1370_midi_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
	struct es1370_state *s = (struct es1370_state *)file->private_data;
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
			es1370_handle_midi(s);
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
		es1370_handle_midi(s);
		spin_unlock_irqrestore(&s->lock, flags);
	}
	__set_current_state(TASK_RUNNING);
        remove_wait_queue(&s->midi.owait, &wait);
	return ret;
}

/* No kernel lock - we have our own spinlock */
static unsigned int es1370_midi_poll(struct file *file, struct poll_table_struct *wait)
{
	struct es1370_state *s = (struct es1370_state *)file->private_data;
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

static int es1370_midi_open(struct inode *inode, struct file *file)
{
	unsigned int minor = iminor(inode);
	DECLARE_WAITQUEUE(wait, current);
	unsigned long flags;
	struct list_head *list;
	struct es1370_state *s;

	for (list = devs.next; ; list = list->next) {
		if (list == &devs)
			return -ENODEV;
		s = list_entry(list, struct es1370_state, devs);
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
		outb(UCTRL_CNTRL_SWR, s->io+ES1370_REG_UART_CONTROL);
		outb(0, s->io+ES1370_REG_UART_CONTROL);
		outb(0, s->io+ES1370_REG_UART_TEST);
	}
	if (file->f_mode & FMODE_READ) {
		s->midi.ird = s->midi.iwr = s->midi.icnt = 0;
	}
	if (file->f_mode & FMODE_WRITE) {
		s->midi.ord = s->midi.owr = s->midi.ocnt = 0;
	}
	s->ctrl |= CTRL_UART_EN;
	outl(s->ctrl, s->io+ES1370_REG_CONTROL);
	es1370_handle_midi(s);
	spin_unlock_irqrestore(&s->lock, flags);
	s->open_mode |= (file->f_mode << FMODE_MIDI_SHIFT) & (FMODE_MIDI_READ | FMODE_MIDI_WRITE);
	mutex_unlock(&s->open_mutex);
	return nonseekable_open(inode, file);
}

static int es1370_midi_release(struct inode *inode, struct file *file)
{
	struct es1370_state *s = (struct es1370_state *)file->private_data;
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
				DBG(printk(KERN_DEBUG "es1370: midi timed out??\n");)
		}
		remove_wait_queue(&s->midi.owait, &wait);
		set_current_state(TASK_RUNNING);
	}
	mutex_lock(&s->open_mutex);
	s->open_mode &= ~((file->f_mode << FMODE_MIDI_SHIFT) & (FMODE_MIDI_READ|FMODE_MIDI_WRITE));
	spin_lock_irqsave(&s->lock, flags);
	if (!(s->open_mode & (FMODE_MIDI_READ | FMODE_MIDI_WRITE))) {
		s->ctrl &= ~CTRL_UART_EN;
		outl(s->ctrl, s->io+ES1370_REG_CONTROL);
	}
	spin_unlock_irqrestore(&s->lock, flags);
	wake_up(&s->open_wait);
	mutex_unlock(&s->open_mutex);
	unlock_kernel();
	return 0;
}

static /*const*/ struct file_operations es1370_midi_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.read		= es1370_midi_read,
	.write		= es1370_midi_write,
	.poll		= es1370_midi_poll,
	.open		= es1370_midi_open,
	.release	= es1370_midi_release,
};

/* --------------------------------------------------------------------- */

/* maximum number of devices; only used for command line params */
#define NR_DEVICE 5

static int lineout[NR_DEVICE];
static int micbias[NR_DEVICE];

static unsigned int devindex;

module_param_array(lineout, bool, NULL, 0);
MODULE_PARM_DESC(lineout, "if 1 the LINE input is converted to LINE out");
module_param_array(micbias, bool, NULL, 0);
MODULE_PARM_DESC(micbias, "sets the +5V bias for an electret microphone");

MODULE_AUTHOR("Thomas M. Sailer, sailer@ife.ee.ethz.ch, hb9jnx@hb9w.che.eu");
MODULE_DESCRIPTION("ES1370 AudioPCI Driver");
MODULE_LICENSE("GPL");


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
	{ SOUND_MIXER_WRITE_LINE3, 0x4040 },
	{ SOUND_MIXER_WRITE_MIC, 0x4040 },
	{ SOUND_MIXER_WRITE_OGAIN, 0x4040 }
};

#ifdef SUPPORT_JOYSTICK

static int __devinit es1370_register_gameport(struct es1370_state *s)
{
	struct gameport *gp;

	if (!request_region(0x200, JOY_EXTENT, "es1370")) {
		printk(KERN_ERR "es1370: joystick io port 0x200 in use\n");
		return -EBUSY;
	}

	s->gameport = gp = gameport_allocate_port();
	if (!gp) {
		printk(KERN_ERR "es1370: can not allocate memory for gameport\n");
		release_region(0x200, JOY_EXTENT);
		return -ENOMEM;
	}

	gameport_set_name(gp, "ESS1370");
	gameport_set_phys(gp, "pci%s/gameport0", pci_name(s->dev));
	gp->dev.parent = &s->dev->dev;
	gp->io = 0x200;

	s->ctrl |= CTRL_JYSTK_EN;
	outl(s->ctrl, s->io + ES1370_REG_CONTROL);

	gameport_register_port(gp);

	return 0;
}

static inline void es1370_unregister_gameport(struct es1370_state *s)
{
	if (s->gameport) {
		int gpio = s->gameport->io;
		gameport_unregister_port(s->gameport);
		release_region(gpio, JOY_EXTENT);

	}
}

#else
static inline int es1370_register_gameport(struct es1370_state *s) { return -ENOSYS; }
static inline void es1370_unregister_gameport(struct es1370_state *s) { }
#endif /* SUPPORT_JOYSTICK */

static int __devinit es1370_probe(struct pci_dev *pcidev, const struct pci_device_id *pciid)
{
	struct es1370_state *s;
	mm_segment_t fs;
	int i, val, ret;

	if ((ret=pci_enable_device(pcidev)))
		return ret;

	if ( !(pci_resource_flags(pcidev, 0) & IORESOURCE_IO) ||
	     !pci_resource_start(pcidev, 0)
	)
		return -ENODEV;
	if (pcidev->irq == 0) 
		return -ENODEV;
	i = pci_set_dma_mask(pcidev, DMA_32BIT_MASK);
	if (i) {
		printk(KERN_WARNING "es1370: architecture does not support 32bit PCI busmaster DMA\n");
		return i;
	}
	if (!(s = kmalloc(sizeof(struct es1370_state), GFP_KERNEL))) {
		printk(KERN_WARNING "es1370: out of memory\n");
		return -ENOMEM;
	}
	memset(s, 0, sizeof(struct es1370_state));
	init_waitqueue_head(&s->dma_adc.wait);
	init_waitqueue_head(&s->dma_dac1.wait);
	init_waitqueue_head(&s->dma_dac2.wait);
	init_waitqueue_head(&s->open_wait);
	init_waitqueue_head(&s->midi.iwait);
	init_waitqueue_head(&s->midi.owait);
	mutex_init(&s->open_mutex);
	spin_lock_init(&s->lock);
	s->magic = ES1370_MAGIC;
	s->dev = pcidev;
	s->io = pci_resource_start(pcidev, 0);
	s->irq = pcidev->irq;
	if (!request_region(s->io, ES1370_EXTENT, "es1370")) {
		printk(KERN_ERR "es1370: io ports %#lx-%#lx in use\n", s->io, s->io+ES1370_EXTENT-1);
		ret = -EBUSY;
		goto err_region;
	}
	if ((ret=request_irq(s->irq, es1370_interrupt, SA_SHIRQ, "es1370",s))) {
		printk(KERN_ERR "es1370: irq %u in use\n", s->irq);
		goto err_irq;
	}

	/* initialize codec registers */
	/* note: setting CTRL_SERR_DIS is reported to break
	 * mic bias setting (by Kim.Berts@fisub.mail.abb.com) */
	s->ctrl = CTRL_CDC_EN | (DAC2_SRTODIV(8000) << CTRL_SH_PCLKDIV) | (1 << CTRL_SH_WTSRSEL);
	if (lineout[devindex])
		s->ctrl |= CTRL_XCTL0;
	if (micbias[devindex])
		s->ctrl |= CTRL_XCTL1;
	s->sctrl = 0;
	printk(KERN_INFO "es1370: adapter at io %#lx irq %u, line %s, mic impedance %s\n",
	       s->io, s->irq, (s->ctrl & CTRL_XCTL0) ? "out" : "in",
	       (s->ctrl & CTRL_XCTL1) ? "1" : "0");
	/* register devices */
	if ((s->dev_audio = register_sound_dsp(&es1370_audio_fops, -1)) < 0) {
		ret = s->dev_audio;
		goto err_dev1;
	}
	if ((s->dev_mixer = register_sound_mixer(&es1370_mixer_fops, -1)) < 0) {
		ret = s->dev_mixer;
		goto err_dev2;
	}
	if ((s->dev_dac = register_sound_dsp(&es1370_dac_fops, -1)) < 0) {
		ret = s->dev_dac;
		goto err_dev3;
	}
	if ((s->dev_midi = register_sound_midi(&es1370_midi_fops, -1)) < 0) {
		ret = s->dev_midi;
		goto err_dev4;
	}
	/* initialize the chips */
	outl(s->ctrl, s->io+ES1370_REG_CONTROL);
	outl(s->sctrl, s->io+ES1370_REG_SERIAL_CONTROL);
	/* point phantom write channel to "bugbuf" */
	s->bugbuf_cpu = pci_alloc_consistent(pcidev,16,&s->bugbuf_dma);
	if (!s->bugbuf_cpu) {
		ret = -ENOMEM;
		goto err_dev5;
	}
	outl((ES1370_REG_PHANTOM_FRAMEADR >> 8) & 15, s->io+ES1370_REG_MEMPAGE);
	outl(s->bugbuf_dma, s->io+(ES1370_REG_PHANTOM_FRAMEADR & 0xff));
	outl(0, s->io+(ES1370_REG_PHANTOM_FRAMECNT & 0xff));
	pci_set_master(pcidev);  /* enable bus mastering */
	wrcodec(s, 0x16, 3); /* no RST, PD */
	wrcodec(s, 0x17, 0); /* CODEC ADC and CODEC DAC use {LR,B}CLK2 and run off the LRCLK2 PLL; program DAC_SYNC=0!!  */
	wrcodec(s, 0x18, 0); /* recording source is mixer */
	wrcodec(s, 0x19, s->mix.micpreamp = 1); /* turn on MIC preamp */
	s->mix.imix = 1;
	fs = get_fs();
	set_fs(KERNEL_DS);
	val = SOUND_MASK_LINE|SOUND_MASK_SYNTH|SOUND_MASK_CD;
	mixer_ioctl(s, SOUND_MIXER_WRITE_RECSRC, (unsigned long)&val);
	for (i = 0; i < sizeof(initvol)/sizeof(initvol[0]); i++) {
		val = initvol[i].vol;
		mixer_ioctl(s, initvol[i].mixch, (unsigned long)&val);
	}
	set_fs(fs);

	es1370_register_gameport(s);

	/* store it in the driver field */
	pci_set_drvdata(pcidev, s);
	/* put it into driver list */
	list_add_tail(&s->devs, &devs);
	/* increment devindex */
	if (devindex < NR_DEVICE-1)
		devindex++;
	return 0;

 err_dev5:
	unregister_sound_midi(s->dev_midi);
 err_dev4:
	unregister_sound_dsp(s->dev_dac);
 err_dev3:
	unregister_sound_mixer(s->dev_mixer);
 err_dev2:
	unregister_sound_dsp(s->dev_audio);
 err_dev1:
	printk(KERN_ERR "es1370: cannot register misc device\n");
	free_irq(s->irq, s);
 err_irq:
	release_region(s->io, ES1370_EXTENT);
 err_region:
	kfree(s);
	return ret;
}

static void __devexit es1370_remove(struct pci_dev *dev)
{
	struct es1370_state *s = pci_get_drvdata(dev);

	if (!s)
		return;
	list_del(&s->devs);
	outl(CTRL_SERR_DIS | (1 << CTRL_SH_WTSRSEL), s->io+ES1370_REG_CONTROL); /* switch everything off */
	outl(0, s->io+ES1370_REG_SERIAL_CONTROL); /* clear serial interrupts */
	synchronize_irq(s->irq);
	free_irq(s->irq, s);
	es1370_unregister_gameport(s);
	release_region(s->io, ES1370_EXTENT);
	unregister_sound_dsp(s->dev_audio);
	unregister_sound_mixer(s->dev_mixer);
	unregister_sound_dsp(s->dev_dac);
	unregister_sound_midi(s->dev_midi);
	pci_free_consistent(dev, 16, s->bugbuf_cpu, s->bugbuf_dma);
	kfree(s);
	pci_set_drvdata(dev, NULL);
}

static struct pci_device_id id_table[] = {
	{ PCI_VENDOR_ID_ENSONIQ, PCI_DEVICE_ID_ENSONIQ_ES1370, PCI_ANY_ID, PCI_ANY_ID, 0, 0 },
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, id_table);

static struct pci_driver es1370_driver = {
	.name		= "es1370",
	.id_table	= id_table,
	.probe		= es1370_probe,
	.remove		= __devexit_p(es1370_remove),
};

static int __init init_es1370(void)
{
	printk(KERN_INFO "es1370: version v0.38 time " __TIME__ " " __DATE__ "\n");
	return pci_register_driver(&es1370_driver);
}

static void __exit cleanup_es1370(void)
{
	printk(KERN_INFO "es1370: unloading\n");
	pci_unregister_driver(&es1370_driver);
}

module_init(init_es1370);
module_exit(cleanup_es1370);

/* --------------------------------------------------------------------- */

#ifndef MODULE

/* format is: es1370=lineout[,micbias]] */

static int __init es1370_setup(char *str)
{
	static unsigned __initdata nr_dev = 0;

	if (nr_dev >= NR_DEVICE)
		return 0;

	(void)
	((get_option(&str,&lineout [nr_dev]) == 2)
	 && get_option(&str,&micbias [nr_dev])
	);

	nr_dev++;
	return 1;
}

__setup("es1370=", es1370_setup);

#endif /* MODULE */
