/*****************************************************************************/

/*
 *      es1371.c  --  Creative Ensoniq ES1371.
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
 * Special thanks to Ensoniq
 *
 *  Supported devices:
 *  /dev/dsp    standard /dev/dsp device, (mostly) OSS compatible
 *  /dev/mixer  standard /dev/mixer device, (mostly) OSS compatible
 *  /dev/dsp1   additional DAC, like /dev/dsp, but outputs to mixer "SYNTH" setting
 *  /dev/midi   simple MIDI UART interface, no ioctl
 *
 *  NOTE: the card does not have any FM/Wavetable synthesizer, it is supposed
 *  to be done in software. That is what /dev/dac is for. By now (Q2 1998)
 *  there are several MIDI to PCM (WAV) packages, one of them is timidity.
 *
 *  Revision history
 *    04.06.1998   0.1   Initial release
 *                       Mixer stuff should be overhauled; especially optional AC97 mixer bits
 *                       should be detected. This results in strange behaviour of some mixer
 *                       settings, like master volume and mic.
 *    08.06.1998   0.2   First release using Alan Cox' soundcore instead of miscdevice
 *    03.08.1998   0.3   Do not include modversions.h
 *                       Now mixer behaviour can basically be selected between
 *                       "OSS documented" and "OSS actual" behaviour
 *    31.08.1998   0.4   Fix realplayer problems - dac.count issues
 *    27.10.1998   0.5   Fix joystick support
 *                       -- Oliver Neukum (c188@org.chemie.uni-muenchen.de)
 *    10.12.1998   0.6   Fix drain_dac trying to wait on not yet initialized DMA
 *    23.12.1998   0.7   Fix a few f_file & FMODE_ bugs
 *                       Don't wake up app until there are fragsize bytes to read/write
 *    06.01.1999   0.8   remove the silly SA_INTERRUPT flag.
 *                       hopefully killed the egcs section type conflict
 *    12.03.1999   0.9   cinfo.blocks should be reset after GETxPTR ioctl.
 *                       reported by Johan Maes <joma@telindus.be>
 *    22.03.1999   0.10  return EAGAIN instead of EBUSY when O_NONBLOCK
 *                       read/write cannot be executed
 *    07.04.1999   0.11  implemented the following ioctl's: SOUND_PCM_READ_RATE, 
 *                       SOUND_PCM_READ_CHANNELS, SOUND_PCM_READ_BITS; 
 *                       Alpha fixes reported by Peter Jones <pjones@redhat.com>
 *                       Another Alpha fix (wait_src_ready in init routine)
 *                       reported by "Ivan N. Kokshaysky" <ink@jurassic.park.msu.ru>
 *                       Note: joystick address handling might still be wrong on archs
 *                       other than i386
 *    15.06.1999   0.12  Fix bad allocation bug.
 *                       Thanks to Deti Fliegl <fliegl@in.tum.de>
 *    28.06.1999   0.13  Add pci_set_master
 *    03.08.1999   0.14  adapt to Linus' new __setup/__initcall
 *                       added kernel command line option "es1371=joystickaddr"
 *                       removed CONFIG_SOUND_ES1371_JOYPORT_BOOT kludge
 *    10.08.1999   0.15  (Re)added S/PDIF module option for cards revision >= 4.
 *                       Initial version by Dave Platt <dplatt@snulbug.mtview.ca.us>.
 *                       module_init/__setup fixes
 *    08.16.1999   0.16  Joe Cotellese <joec@ensoniq.com>
 *                       Added detection for ES1371 revision ID so that we can
 *                       detect the ES1373 and later parts.
 *                       added AC97 #defines for readability
 *                       added a /proc file system for dumping hardware state
 *                       updated SRC and CODEC w/r functions to accommodate bugs
 *                       in some versions of the ES137x chips.
 *    31.08.1999   0.17  add spin_lock_init
 *                       replaced current->state = x with set_current_state(x)
 *    03.09.1999   0.18  change read semantics for MIDI to match
 *                       OSS more closely; remove possible wakeup race
 *    21.10.1999   0.19  Round sampling rates, requested by
 *                       Kasamatsu Kenichi <t29w0267@ip.media.kyoto-u.ac.jp>
 *    27.10.1999   0.20  Added SigmaTel 3D enhancement string
 *                       Codec ID printing changes
 *    28.10.1999   0.21  More waitqueue races fixed
 *                       Joe Cotellese <joec@ensoniq.com>
 *                       Changed PCI detection routine so we can more easily
 *                       detect ES137x chip and derivatives.
 *    05.01.2000   0.22  Should now work with rev7 boards; patch by
 *                       Eric Lemar, elemar@cs.washington.edu
 *    08.01.2000   0.23  Prevent some ioctl's from returning bad count values on underrun/overrun;
 *                       Tim Janik's BSE (Bedevilled Sound Engine) found this
 *    07.02.2000   0.24  Use pci_alloc_consistent and pci_register_driver
 *    07.02.2000   0.25  Use ac97_codec
 *    01.03.2000   0.26  SPDIF patch by Mikael Bouillot <mikael.bouillot@bigfoot.com>
 *                       Use pci_module_init
 *    21.11.2000   0.27  Initialize dma buffers in poll, otherwise poll may return a bogus mask
 *    12.12.2000   0.28  More dma buffer initializations, patch from
 *                       Tjeerd Mulder <tjeerd.mulder@fujitsu-siemens.com>
 *    05.01.2001   0.29  Hopefully updates will not be required anymore when Creative bumps
 *                       the CT5880 revision.
 *                       suggested by Stephan Müller <smueller@chronox.de>
 *    31.01.2001   0.30  Register/Unregister gameport
 *                       Fix SETTRIGGER non OSS API conformity
 *    14.07.2001   0.31  Add list of laptops needing amplifier control
 *    03.01.2003   0.32  open_mode fixes from Georg Acher <acher@in.tum.de>
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
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/bitops.h>
#include <linux/proc_fs.h>
#include <linux/spinlock.h>
#include <linux/smp_lock.h>
#include <linux/ac97_codec.h>
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
#define ES1371_DEBUG
#define DBG(x) {}
/*#define DBG(x) {x}*/

/* --------------------------------------------------------------------- */

#ifndef PCI_VENDOR_ID_ENSONIQ
#define PCI_VENDOR_ID_ENSONIQ        0x1274    
#endif

#ifndef PCI_VENDOR_ID_ECTIVA
#define PCI_VENDOR_ID_ECTIVA         0x1102
#endif

#ifndef PCI_DEVICE_ID_ENSONIQ_ES1371
#define PCI_DEVICE_ID_ENSONIQ_ES1371 0x1371
#endif

#ifndef PCI_DEVICE_ID_ENSONIQ_CT5880
#define PCI_DEVICE_ID_ENSONIQ_CT5880 0x5880
#endif

#ifndef PCI_DEVICE_ID_ECTIVA_EV1938
#define PCI_DEVICE_ID_ECTIVA_EV1938 0x8938
#endif

/* ES1371 chip ID */
/* This is a little confusing because all ES1371 compatible chips have the
   same DEVICE_ID, the only thing differentiating them is the REV_ID field.
   This is only significant if you want to enable features on the later parts.
   Yes, I know it's stupid and why didn't we use the sub IDs?
*/
#define ES1371REV_ES1373_A  0x04
#define ES1371REV_ES1373_B  0x06
#define ES1371REV_CT5880_A  0x07
#define CT5880REV_CT5880_C  0x02
#define CT5880REV_CT5880_D  0x03
#define ES1371REV_ES1371_B  0x09
#define EV1938REV_EV1938_A  0x00
#define ES1371REV_ES1373_8  0x08

#define ES1371_MAGIC  ((PCI_VENDOR_ID_ENSONIQ<<16)|PCI_DEVICE_ID_ENSONIQ_ES1371)

#define ES1371_EXTENT             0x40
#define JOY_EXTENT                8

#define ES1371_REG_CONTROL        0x00
#define ES1371_REG_STATUS         0x04 /* on the 5880 it is control/status */
#define ES1371_REG_UART_DATA      0x08
#define ES1371_REG_UART_STATUS    0x09
#define ES1371_REG_UART_CONTROL   0x09
#define ES1371_REG_UART_TEST      0x0a
#define ES1371_REG_MEMPAGE        0x0c
#define ES1371_REG_SRCONV         0x10
#define ES1371_REG_CODEC          0x14
#define ES1371_REG_LEGACY         0x18
#define ES1371_REG_SERIAL_CONTROL 0x20
#define ES1371_REG_DAC1_SCOUNT    0x24
#define ES1371_REG_DAC2_SCOUNT    0x28
#define ES1371_REG_ADC_SCOUNT     0x2c

#define ES1371_REG_DAC1_FRAMEADR  0xc30
#define ES1371_REG_DAC1_FRAMECNT  0xc34
#define ES1371_REG_DAC2_FRAMEADR  0xc38
#define ES1371_REG_DAC2_FRAMECNT  0xc3c
#define ES1371_REG_ADC_FRAMEADR   0xd30
#define ES1371_REG_ADC_FRAMECNT   0xd34

#define ES1371_FMT_U8_MONO     0
#define ES1371_FMT_U8_STEREO   1
#define ES1371_FMT_S16_MONO    2
#define ES1371_FMT_S16_STEREO  3
#define ES1371_FMT_STEREO      1
#define ES1371_FMT_S16         2
#define ES1371_FMT_MASK        3

static const unsigned sample_size[] = { 1, 2, 2, 4 };
static const unsigned sample_shift[] = { 0, 1, 1, 2 };

#define CTRL_RECEN_B    0x08000000  /* 1 = don't mix analog in to digital out */
#define CTRL_SPDIFEN_B  0x04000000
#define CTRL_JOY_SHIFT  24
#define CTRL_JOY_MASK   3
#define CTRL_JOY_200    0x00000000  /* joystick base address */
#define CTRL_JOY_208    0x01000000
#define CTRL_JOY_210    0x02000000
#define CTRL_JOY_218    0x03000000
#define CTRL_GPIO_IN0   0x00100000  /* general purpose inputs/outputs */
#define CTRL_GPIO_IN1   0x00200000
#define CTRL_GPIO_IN2   0x00400000
#define CTRL_GPIO_IN3   0x00800000
#define CTRL_GPIO_OUT0  0x00010000
#define CTRL_GPIO_OUT1  0x00020000
#define CTRL_GPIO_OUT2  0x00040000
#define CTRL_GPIO_OUT3  0x00080000
#define CTRL_MSFMTSEL   0x00008000  /* MPEG serial data fmt: 0 = Sony, 1 = I2S */
#define CTRL_SYNCRES    0x00004000  /* AC97 warm reset */
#define CTRL_ADCSTOP    0x00002000  /* stop ADC transfers */
#define CTRL_PWR_INTRM  0x00001000  /* 1 = power level ints enabled */
#define CTRL_M_CB       0x00000800  /* recording source: 0 = ADC, 1 = MPEG */
#define CTRL_CCB_INTRM  0x00000400  /* 1 = CCB "voice" ints enabled */
#define CTRL_PDLEV0     0x00000000  /* power down level */
#define CTRL_PDLEV1     0x00000100
#define CTRL_PDLEV2     0x00000200
#define CTRL_PDLEV3     0x00000300
#define CTRL_BREQ       0x00000080  /* 1 = test mode (internal mem test) */
#define CTRL_DAC1_EN    0x00000040  /* enable DAC1 */
#define CTRL_DAC2_EN    0x00000020  /* enable DAC2 */
#define CTRL_ADC_EN     0x00000010  /* enable ADC */
#define CTRL_UART_EN    0x00000008  /* enable MIDI uart */
#define CTRL_JYSTK_EN   0x00000004  /* enable Joystick port */
#define CTRL_XTALCLKDIS 0x00000002  /* 1 = disable crystal clock input */
#define CTRL_PCICLKDIS  0x00000001  /* 1 = disable PCI clock distribution */


#define STAT_INTR       0x80000000  /* wired or of all interrupt bits */
#define CSTAT_5880_AC97_RST 0x20000000 /* CT5880 Reset bit */
#define STAT_EN_SPDIF   0x00040000  /* enable S/PDIF circuitry */
#define STAT_TS_SPDIF   0x00020000  /* test S/PDIF circuitry */
#define STAT_TESTMODE   0x00010000  /* test ASIC */
#define STAT_SYNC_ERR   0x00000100  /* 1 = codec sync error */
#define STAT_VC         0x000000c0  /* CCB int source, 0=DAC1, 1=DAC2, 2=ADC, 3=undef */
#define STAT_SH_VC      6
#define STAT_MPWR       0x00000020  /* power level interrupt */
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

/* sample rate converter */
#define SRC_OKSTATE        1

#define SRC_RAMADDR_MASK   0xfe000000
#define SRC_RAMADDR_SHIFT  25
#define SRC_DAC1FREEZE     (1UL << 21)
#define SRC_DAC2FREEZE      (1UL << 20)
#define SRC_ADCFREEZE      (1UL << 19)


#define SRC_WE             0x01000000  /* read/write control for SRC RAM */
#define SRC_BUSY           0x00800000  /* SRC busy */
#define SRC_DIS            0x00400000  /* 1 = disable SRC */
#define SRC_DDAC1          0x00200000  /* 1 = disable accum update for DAC1 */
#define SRC_DDAC2          0x00100000  /* 1 = disable accum update for DAC2 */
#define SRC_DADC           0x00080000  /* 1 = disable accum update for ADC2 */
#define SRC_CTLMASK        0x00780000
#define SRC_RAMDATA_MASK   0x0000ffff
#define SRC_RAMDATA_SHIFT  0

#define SRCREG_ADC      0x78
#define SRCREG_DAC1     0x70
#define SRCREG_DAC2     0x74
#define SRCREG_VOL_ADC  0x6c
#define SRCREG_VOL_DAC1 0x7c
#define SRCREG_VOL_DAC2 0x7e

#define SRCREG_TRUNC_N     0x00
#define SRCREG_INT_REGS    0x01
#define SRCREG_ACCUM_FRAC  0x02
#define SRCREG_VFREQ_FRAC  0x03

#define CODEC_PIRD        0x00800000  /* 0 = write AC97 register */
#define CODEC_PIADD_MASK  0x007f0000
#define CODEC_PIADD_SHIFT 16
#define CODEC_PIDAT_MASK  0x0000ffff
#define CODEC_PIDAT_SHIFT 0

#define CODEC_RDY         0x80000000  /* AC97 read data valid */
#define CODEC_WIP         0x40000000  /* AC97 write in progress */
#define CODEC_PORD        0x00800000  /* 0 = write AC97 register */
#define CODEC_POADD_MASK  0x007f0000
#define CODEC_POADD_SHIFT 16
#define CODEC_PODAT_MASK  0x0000ffff
#define CODEC_PODAT_SHIFT 0


#define LEGACY_JFAST      0x80000000  /* fast joystick timing */
#define LEGACY_FIRQ       0x01000000  /* force IRQ */

#define SCTRL_DACTEST     0x00400000  /* 1 = DAC test, test vector generation purposes */
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
#define POLL_COUNT   0x1000
#define FMODE_DAC         4           /* slight misuse of mode_t */

/* MIDI buffer sizes */

#define MIDIINBUF  256
#define MIDIOUTBUF 256

#define FMODE_MIDI_SHIFT 3
#define FMODE_MIDI_READ  (FMODE_READ << FMODE_MIDI_SHIFT)
#define FMODE_MIDI_WRITE (FMODE_WRITE << FMODE_MIDI_SHIFT)

#define ES1371_MODULE_NAME "es1371"
#define PFX ES1371_MODULE_NAME ": "

/* --------------------------------------------------------------------- */

struct es1371_state {
	/* magic */
	unsigned int magic;

	/* list of es1371 devices */
	struct list_head devs;

	/* the corresponding pci_dev structure */
	struct pci_dev *dev;

	/* soundcore stuff */
	int dev_audio;
	int dev_dac;
	int dev_midi;
	
	/* hardware resources */
	unsigned long io; /* long for SPARC */
	unsigned int irq;

	/* PCI ID's */
	u16 vendor;
	u16 device;
        u8 rev; /* the chip revision */

	/* options */
	int spdif_volume; /* S/PDIF output is enabled if != -1 */

#ifdef ES1371_DEBUG
        /* debug /proc entry */
	struct proc_dir_entry *ps;
#endif /* ES1371_DEBUG */

	struct ac97_codec *codec;

	/* wave stuff */
	unsigned ctrl;
	unsigned sctrl;
	unsigned dac1rate, dac2rate, adcrate;

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

	struct mutex sem;
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

static unsigned wait_src_ready(struct es1371_state *s)
{
	unsigned int t, r;

	for (t = 0; t < POLL_COUNT; t++) {
		if (!((r = inl(s->io + ES1371_REG_SRCONV)) & SRC_BUSY))
			return r;
		udelay(1);
	}
	printk(KERN_DEBUG PFX "sample rate converter timeout r = 0x%08x\n", r);
	return r;
}

static unsigned src_read(struct es1371_state *s, unsigned reg)
{
        unsigned int temp,i,orig;

        /* wait for ready */
        temp = wait_src_ready (s);

        /* we can only access the SRC at certain times, make sure
           we're allowed to before we read */
           
        orig = temp;
        /* expose the SRC state bits */
        outl ( (temp & SRC_CTLMASK) | (reg << SRC_RAMADDR_SHIFT) | 0x10000UL,
               s->io + ES1371_REG_SRCONV);

        /* now, wait for busy and the correct time to read */
        temp = wait_src_ready (s);

        if ( (temp & 0x00870000UL ) != ( SRC_OKSTATE << 16 )){
                /* wait for the right state */
                for (i=0; i<POLL_COUNT; i++){
                        temp = inl (s->io + ES1371_REG_SRCONV);
                        if ( (temp & 0x00870000UL ) == ( SRC_OKSTATE << 16 ))
                                break;
                }
        }

        /* hide the state bits */
        outl ((orig & SRC_CTLMASK) | (reg << SRC_RAMADDR_SHIFT), s->io + ES1371_REG_SRCONV);
        return temp;
                        
                
}

static void src_write(struct es1371_state *s, unsigned reg, unsigned data)
{
      
	unsigned int r;

	r = wait_src_ready(s) & (SRC_DIS | SRC_DDAC1 | SRC_DDAC2 | SRC_DADC);
	r |= (reg << SRC_RAMADDR_SHIFT) & SRC_RAMADDR_MASK;
	r |= (data << SRC_RAMDATA_SHIFT) & SRC_RAMDATA_MASK;
	outl(r | SRC_WE, s->io + ES1371_REG_SRCONV);

}

/* --------------------------------------------------------------------- */

/* most of the following here is black magic */
static void set_adc_rate(struct es1371_state *s, unsigned rate)
{
	unsigned long flags;
	unsigned int n, truncm, freq;

	if (rate > 48000)
		rate = 48000;
	if (rate < 4000)
		rate = 4000;
	n = rate / 3000;
	if ((1 << n) & ((1 << 15) | (1 << 13) | (1 << 11) | (1 << 9)))
		n--;
	truncm = (21 * n - 1) | 1;
        freq = ((48000UL << 15) / rate) * n;
	s->adcrate = (48000UL << 15) / (freq / n);
	spin_lock_irqsave(&s->lock, flags);
	if (rate >= 24000) {
		if (truncm > 239)
			truncm = 239;
		src_write(s, SRCREG_ADC+SRCREG_TRUNC_N, 
			  (((239 - truncm) >> 1) << 9) | (n << 4));
	} else {
		if (truncm > 119)
			truncm = 119;
		src_write(s, SRCREG_ADC+SRCREG_TRUNC_N, 
			  0x8000 | (((119 - truncm) >> 1) << 9) | (n << 4));
	}		
	src_write(s, SRCREG_ADC+SRCREG_INT_REGS, 
		  (src_read(s, SRCREG_ADC+SRCREG_INT_REGS) & 0x00ff) |
		  ((freq >> 5) & 0xfc00));
	src_write(s, SRCREG_ADC+SRCREG_VFREQ_FRAC, freq & 0x7fff);
	src_write(s, SRCREG_VOL_ADC, n << 8);
	src_write(s, SRCREG_VOL_ADC+1, n << 8);
	spin_unlock_irqrestore(&s->lock, flags);
}


static void set_dac1_rate(struct es1371_state *s, unsigned rate)
{
	unsigned long flags;
	unsigned int freq, r;

	if (rate > 48000)
		rate = 48000;
	if (rate < 4000)
		rate = 4000;
        freq = ((rate << 15) + 1500) / 3000;
	s->dac1rate = (freq * 3000 + 16384) >> 15;
	spin_lock_irqsave(&s->lock, flags);
	r = (wait_src_ready(s) & (SRC_DIS | SRC_DDAC2 | SRC_DADC)) | SRC_DDAC1;
	outl(r, s->io + ES1371_REG_SRCONV);
	src_write(s, SRCREG_DAC1+SRCREG_INT_REGS, 
		  (src_read(s, SRCREG_DAC1+SRCREG_INT_REGS) & 0x00ff) |
		  ((freq >> 5) & 0xfc00));
	src_write(s, SRCREG_DAC1+SRCREG_VFREQ_FRAC, freq & 0x7fff);
	r = (wait_src_ready(s) & (SRC_DIS | SRC_DDAC2 | SRC_DADC));
	outl(r, s->io + ES1371_REG_SRCONV);
	spin_unlock_irqrestore(&s->lock, flags);
}

static void set_dac2_rate(struct es1371_state *s, unsigned rate)
{
	unsigned long flags;
	unsigned int freq, r;

	if (rate > 48000)
		rate = 48000;
	if (rate < 4000)
		rate = 4000;
        freq = ((rate << 15) + 1500) / 3000;
	s->dac2rate = (freq * 3000 + 16384) >> 15;
	spin_lock_irqsave(&s->lock, flags);
	r = (wait_src_ready(s) & (SRC_DIS | SRC_DDAC1 | SRC_DADC)) | SRC_DDAC2;
	outl(r, s->io + ES1371_REG_SRCONV);
	src_write(s, SRCREG_DAC2+SRCREG_INT_REGS, 
		  (src_read(s, SRCREG_DAC2+SRCREG_INT_REGS) & 0x00ff) |
		  ((freq >> 5) & 0xfc00));
	src_write(s, SRCREG_DAC2+SRCREG_VFREQ_FRAC, freq & 0x7fff);
	r = (wait_src_ready(s) & (SRC_DIS | SRC_DDAC1 | SRC_DADC));
	outl(r, s->io + ES1371_REG_SRCONV);
	spin_unlock_irqrestore(&s->lock, flags);
}

/* --------------------------------------------------------------------- */

static void __devinit src_init(struct es1371_state *s)
{
        unsigned int i;

        /* before we enable or disable the SRC we need
           to wait for it to become ready */
        wait_src_ready(s);

        outl(SRC_DIS, s->io + ES1371_REG_SRCONV);

        for (i = 0; i < 0x80; i++)
                src_write(s, i, 0);

        src_write(s, SRCREG_DAC1+SRCREG_TRUNC_N, 16 << 4);
        src_write(s, SRCREG_DAC1+SRCREG_INT_REGS, 16 << 10);
        src_write(s, SRCREG_DAC2+SRCREG_TRUNC_N, 16 << 4);
        src_write(s, SRCREG_DAC2+SRCREG_INT_REGS, 16 << 10);
        src_write(s, SRCREG_VOL_ADC, 1 << 12);
        src_write(s, SRCREG_VOL_ADC+1, 1 << 12);
        src_write(s, SRCREG_VOL_DAC1, 1 << 12);
        src_write(s, SRCREG_VOL_DAC1+1, 1 << 12);
        src_write(s, SRCREG_VOL_DAC2, 1 << 12);
        src_write(s, SRCREG_VOL_DAC2+1, 1 << 12);
        set_adc_rate(s, 22050);
        set_dac1_rate(s, 22050);
        set_dac2_rate(s, 22050);

        /* WARNING:
         * enabling the sample rate converter without properly programming
         * its parameters causes the chip to lock up (the SRC busy bit will
         * be stuck high, and I've found no way to rectify this other than
         * power cycle)
         */
        wait_src_ready(s);
        outl(0, s->io+ES1371_REG_SRCONV);
}

/* --------------------------------------------------------------------- */

static void wrcodec(struct ac97_codec *codec, u8 addr, u16 data)
{
	struct es1371_state *s = (struct es1371_state *)codec->private_data;
	unsigned long flags;
	unsigned t, x;
        
	spin_lock_irqsave(&s->lock, flags);
	for (t = 0; t < POLL_COUNT; t++)
		if (!(inl(s->io+ES1371_REG_CODEC) & CODEC_WIP))
			break;

        /* save the current state for later */
        x = wait_src_ready(s);

        /* enable SRC state data in SRC mux */
	outl((x & (SRC_DIS | SRC_DDAC1 | SRC_DDAC2 | SRC_DADC)) | 0x00010000,
	     s->io+ES1371_REG_SRCONV);

        /* wait for not busy (state 0) first to avoid
           transition states */
        for (t=0; t<POLL_COUNT; t++){
                if((inl(s->io+ES1371_REG_SRCONV) & 0x00870000) ==0 )
                    break;
                udelay(1);
        }
        
        /* wait for a SAFE time to write addr/data and then do it, dammit */
        for (t=0; t<POLL_COUNT; t++){
                if((inl(s->io+ES1371_REG_SRCONV) & 0x00870000) ==0x00010000)
                    break;
                udelay(1);
        }

	outl(((addr << CODEC_POADD_SHIFT) & CODEC_POADD_MASK) |
	     ((data << CODEC_PODAT_SHIFT) & CODEC_PODAT_MASK), s->io+ES1371_REG_CODEC);

	/* restore SRC reg */
	wait_src_ready(s);
	outl(x, s->io+ES1371_REG_SRCONV);
	spin_unlock_irqrestore(&s->lock, flags);
}

static u16 rdcodec(struct ac97_codec *codec, u8 addr)
{
	struct es1371_state *s = (struct es1371_state *)codec->private_data;
	unsigned long flags;
	unsigned t, x;

	spin_lock_irqsave(&s->lock, flags);
	
        /* wait for WIP to go away */
	for (t = 0; t < 0x1000; t++)
		if (!(inl(s->io+ES1371_REG_CODEC) & CODEC_WIP))
			break;

	/* save the current state for later */
	x = (wait_src_ready(s) & (SRC_DIS | SRC_DDAC1 | SRC_DDAC2 | SRC_DADC));

	/* enable SRC state data in SRC mux */
	outl( x | 0x00010000,
              s->io+ES1371_REG_SRCONV);

        /* wait for not busy (state 0) first to avoid
           transition states */
        for (t=0; t<POLL_COUNT; t++){
                if((inl(s->io+ES1371_REG_SRCONV) & 0x00870000) ==0 )
                    break;
                udelay(1);
        }
        
        /* wait for a SAFE time to write addr/data and then do it, dammit */
        for (t=0; t<POLL_COUNT; t++){
                if((inl(s->io+ES1371_REG_SRCONV) & 0x00870000) ==0x00010000)
                    break;
                udelay(1);
        }

	outl(((addr << CODEC_POADD_SHIFT) & CODEC_POADD_MASK) | CODEC_PORD, s->io+ES1371_REG_CODEC);
	/* restore SRC reg */
	wait_src_ready(s);
	outl(x, s->io+ES1371_REG_SRCONV);

        /* wait for WIP again */
	for (t = 0; t < 0x1000; t++)
		if (!(inl(s->io+ES1371_REG_CODEC) & CODEC_WIP))
			break;
        
	/* now wait for the stinkin' data (RDY) */
	for (t = 0; t < POLL_COUNT; t++)
		if ((x = inl(s->io+ES1371_REG_CODEC)) & CODEC_RDY)
			break;
        
	spin_unlock_irqrestore(&s->lock, flags);
	return ((x & CODEC_PIDAT_MASK) >> CODEC_PIDAT_SHIFT);
}

/* --------------------------------------------------------------------- */

static inline void stop_adc(struct es1371_state *s)
{
	unsigned long flags;

	spin_lock_irqsave(&s->lock, flags);
	s->ctrl &= ~CTRL_ADC_EN;
	outl(s->ctrl, s->io+ES1371_REG_CONTROL);
	spin_unlock_irqrestore(&s->lock, flags);
}	

static inline void stop_dac1(struct es1371_state *s)
{
	unsigned long flags;

	spin_lock_irqsave(&s->lock, flags);
	s->ctrl &= ~CTRL_DAC1_EN;
	outl(s->ctrl, s->io+ES1371_REG_CONTROL);
	spin_unlock_irqrestore(&s->lock, flags);
}	

static inline void stop_dac2(struct es1371_state *s)
{
	unsigned long flags;

	spin_lock_irqsave(&s->lock, flags);
	s->ctrl &= ~CTRL_DAC2_EN;
	outl(s->ctrl, s->io+ES1371_REG_CONTROL);
	spin_unlock_irqrestore(&s->lock, flags);
}	

static void start_dac1(struct es1371_state *s)
{
	unsigned long flags;
	unsigned fragremain, fshift;

	spin_lock_irqsave(&s->lock, flags);
	if (!(s->ctrl & CTRL_DAC1_EN) && (s->dma_dac1.mapped || s->dma_dac1.count > 0)
	    && s->dma_dac1.ready) {
		s->ctrl |= CTRL_DAC1_EN;
		s->sctrl = (s->sctrl & ~(SCTRL_P1LOOPSEL | SCTRL_P1PAUSE | SCTRL_P1SCTRLD)) | SCTRL_P1INTEN;
		outl(s->sctrl, s->io+ES1371_REG_SERIAL_CONTROL);
		fragremain = ((- s->dma_dac1.hwptr) & (s->dma_dac1.fragsize-1));
		fshift = sample_shift[(s->sctrl & SCTRL_P1FMT) >> SCTRL_SH_P1FMT];
		if (fragremain < 2*fshift)
			fragremain = s->dma_dac1.fragsize;
		outl((fragremain >> fshift) - 1, s->io+ES1371_REG_DAC1_SCOUNT);
		outl(s->ctrl, s->io+ES1371_REG_CONTROL);
		outl((s->dma_dac1.fragsize >> fshift) - 1, s->io+ES1371_REG_DAC1_SCOUNT);
	}
	spin_unlock_irqrestore(&s->lock, flags);
}	

static void start_dac2(struct es1371_state *s)
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
		outl(s->sctrl, s->io+ES1371_REG_SERIAL_CONTROL);
		fragremain = ((- s->dma_dac2.hwptr) & (s->dma_dac2.fragsize-1));
		fshift = sample_shift[(s->sctrl & SCTRL_P2FMT) >> SCTRL_SH_P2FMT];
		if (fragremain < 2*fshift)
			fragremain = s->dma_dac2.fragsize;
		outl((fragremain >> fshift) - 1, s->io+ES1371_REG_DAC2_SCOUNT);
		outl(s->ctrl, s->io+ES1371_REG_CONTROL);
		outl((s->dma_dac2.fragsize >> fshift) - 1, s->io+ES1371_REG_DAC2_SCOUNT);
	}
	spin_unlock_irqrestore(&s->lock, flags);
}	

static void start_adc(struct es1371_state *s)
{
	unsigned long flags;
	unsigned fragremain, fshift;

	spin_lock_irqsave(&s->lock, flags);
	if (!(s->ctrl & CTRL_ADC_EN) && (s->dma_adc.mapped || s->dma_adc.count < (signed)(s->dma_adc.dmasize - 2*s->dma_adc.fragsize))
	    && s->dma_adc.ready) {
		s->ctrl |= CTRL_ADC_EN;
		s->sctrl = (s->sctrl & ~SCTRL_R1LOOPSEL) | SCTRL_R1INTEN;
		outl(s->sctrl, s->io+ES1371_REG_SERIAL_CONTROL);
		fragremain = ((- s->dma_adc.hwptr) & (s->dma_adc.fragsize-1));
		fshift = sample_shift[(s->sctrl & SCTRL_R1FMT) >> SCTRL_SH_R1FMT];
		if (fragremain < 2*fshift)
			fragremain = s->dma_adc.fragsize;
		outl((fragremain >> fshift) - 1, s->io+ES1371_REG_ADC_SCOUNT);
		outl(s->ctrl, s->io+ES1371_REG_CONTROL);
		outl((s->dma_adc.fragsize >> fshift) - 1, s->io+ES1371_REG_ADC_SCOUNT);
	}
	spin_unlock_irqrestore(&s->lock, flags);
}	

/* --------------------------------------------------------------------- */

#define DMABUF_DEFAULTORDER (17-PAGE_SHIFT)
#define DMABUF_MINORDER 1


static inline void dealloc_dmabuf(struct es1371_state *s, struct dmabuf *db)
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

static int prog_dmabuf(struct es1371_state *s, struct dmabuf *db, unsigned rate, unsigned fmt, unsigned reg)
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
	fmt &= ES1371_FMT_MASK;
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
	memset(db->rawbuf, (fmt & ES1371_FMT_S16) ? 0 : 0x80, db->dmasize);
	outl((reg >> 8) & 15, s->io+ES1371_REG_MEMPAGE);
	outl(db->dmaaddr, s->io+(reg & 0xff));
	outl((db->dmasize >> 2)-1, s->io+((reg + 4) & 0xff));
	db->enabled = 1;
	db->ready = 1;
	return 0;
}

static inline int prog_dmabuf_adc(struct es1371_state *s)
{
	stop_adc(s);
	return prog_dmabuf(s, &s->dma_adc, s->adcrate, (s->sctrl >> SCTRL_SH_R1FMT) & ES1371_FMT_MASK, 
			   ES1371_REG_ADC_FRAMEADR);
}

static inline int prog_dmabuf_dac2(struct es1371_state *s)
{
	stop_dac2(s);
	return prog_dmabuf(s, &s->dma_dac2, s->dac2rate, (s->sctrl >> SCTRL_SH_P2FMT) & ES1371_FMT_MASK, 
			   ES1371_REG_DAC2_FRAMEADR);
}

static inline int prog_dmabuf_dac1(struct es1371_state *s)
{
	stop_dac1(s);
	return prog_dmabuf(s, &s->dma_dac1, s->dac1rate, (s->sctrl >> SCTRL_SH_P1FMT) & ES1371_FMT_MASK,
			   ES1371_REG_DAC1_FRAMEADR);
}

static inline unsigned get_hwptr(struct es1371_state *s, struct dmabuf *db, unsigned reg)
{
	unsigned hwptr, diff;

	outl((reg >> 8) & 15, s->io+ES1371_REG_MEMPAGE);
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
static void es1371_update_ptr(struct es1371_state *s)
{
	int diff;

	/* update ADC pointer */
	if (s->ctrl & CTRL_ADC_EN) {
		diff = get_hwptr(s, &s->dma_adc, ES1371_REG_ADC_FRAMECNT);
		s->dma_adc.total_bytes += diff;
		s->dma_adc.count += diff;
		if (s->dma_adc.count >= (signed)s->dma_adc.fragsize) 
			wake_up(&s->dma_adc.wait);
		if (!s->dma_adc.mapped) {
			if (s->dma_adc.count > (signed)(s->dma_adc.dmasize - ((3 * s->dma_adc.fragsize) >> 1))) {
				s->ctrl &= ~CTRL_ADC_EN;
				outl(s->ctrl, s->io+ES1371_REG_CONTROL);
				s->dma_adc.error++;
			}
		}
	}
	/* update DAC1 pointer */
	if (s->ctrl & CTRL_DAC1_EN) {
		diff = get_hwptr(s, &s->dma_dac1, ES1371_REG_DAC1_FRAMECNT);
		s->dma_dac1.total_bytes += diff;
		if (s->dma_dac1.mapped) {
			s->dma_dac1.count += diff;
			if (s->dma_dac1.count >= (signed)s->dma_dac1.fragsize)
				wake_up(&s->dma_dac1.wait);
		} else {
			s->dma_dac1.count -= diff;
			if (s->dma_dac1.count <= 0) {
				s->ctrl &= ~CTRL_DAC1_EN;
				outl(s->ctrl, s->io+ES1371_REG_CONTROL);
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
		diff = get_hwptr(s, &s->dma_dac2, ES1371_REG_DAC2_FRAMECNT);
		s->dma_dac2.total_bytes += diff;
		if (s->dma_dac2.mapped) {
			s->dma_dac2.count += diff;
			if (s->dma_dac2.count >= (signed)s->dma_dac2.fragsize)
				wake_up(&s->dma_dac2.wait);
		} else {
			s->dma_dac2.count -= diff;
			if (s->dma_dac2.count <= 0) {
				s->ctrl &= ~CTRL_DAC2_EN;
				outl(s->ctrl, s->io+ES1371_REG_CONTROL);
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
static void es1371_handle_midi(struct es1371_state *s)
{
	unsigned char ch;
	int wake;

	if (!(s->ctrl & CTRL_UART_EN))
		return;
	wake = 0;
	while (inb(s->io+ES1371_REG_UART_STATUS) & USTAT_RXRDY) {
		ch = inb(s->io+ES1371_REG_UART_DATA);
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
	while ((inb(s->io+ES1371_REG_UART_STATUS) & USTAT_TXRDY) && s->midi.ocnt > 0) {
		outb(s->midi.obuf[s->midi.ord], s->io+ES1371_REG_UART_DATA);
		s->midi.ord = (s->midi.ord + 1) % MIDIOUTBUF;
		s->midi.ocnt--;
		if (s->midi.ocnt < MIDIOUTBUF-16)
			wake = 1;
	}
	if (wake)
		wake_up(&s->midi.owait);
	outb((s->midi.ocnt > 0) ? UCTRL_RXINTEN | UCTRL_ENA_TXINT : UCTRL_RXINTEN, s->io+ES1371_REG_UART_CONTROL);
}

static irqreturn_t es1371_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
        struct es1371_state *s = (struct es1371_state *)dev_id;
	unsigned int intsrc, sctl;
	
	/* fastpath out, to ease interrupt sharing */
	intsrc = inl(s->io+ES1371_REG_STATUS);
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
	outl(sctl, s->io+ES1371_REG_SERIAL_CONTROL);
	outl(s->sctrl, s->io+ES1371_REG_SERIAL_CONTROL);
	es1371_update_ptr(s);
	es1371_handle_midi(s);
	spin_unlock(&s->lock);
	return IRQ_HANDLED;
}

/* --------------------------------------------------------------------- */

static const char invalid_magic[] = KERN_CRIT PFX "invalid magic value\n";

#define VALIDATE_STATE(s)                         \
({                                                \
	if (!(s) || (s)->magic != ES1371_MAGIC) { \
		printk(invalid_magic);            \
		return -ENXIO;                    \
	}                                         \
})

/* --------------------------------------------------------------------- */

/* Conversion table for S/PDIF PCM volume emulation through the SRC */
/* dB-linear table of DAC vol values; -0dB to -46.5dB with mute */
static const unsigned short DACVolTable[101] =
{
	0x1000, 0x0f2a, 0x0e60, 0x0da0, 0x0cea, 0x0c3e, 0x0b9a, 0x0aff,
	0x0a6d, 0x09e1, 0x095e, 0x08e1, 0x086a, 0x07fa, 0x078f, 0x072a,
	0x06cb, 0x0670, 0x061a, 0x05c9, 0x057b, 0x0532, 0x04ed, 0x04ab,
	0x046d, 0x0432, 0x03fa, 0x03c5, 0x0392, 0x0363, 0x0335, 0x030b,
	0x02e2, 0x02bc, 0x0297, 0x0275, 0x0254, 0x0235, 0x0217, 0x01fb,
	0x01e1, 0x01c8, 0x01b0, 0x0199, 0x0184, 0x0170, 0x015d, 0x014b,
	0x0139, 0x0129, 0x0119, 0x010b, 0x00fd, 0x00f0, 0x00e3, 0x00d7,
	0x00cc, 0x00c1, 0x00b7, 0x00ae, 0x00a5, 0x009c, 0x0094, 0x008c,
	0x0085, 0x007e, 0x0077, 0x0071, 0x006b, 0x0066, 0x0060, 0x005b,
	0x0057, 0x0052, 0x004e, 0x004a, 0x0046, 0x0042, 0x003f, 0x003c,
	0x0038, 0x0036, 0x0033, 0x0030, 0x002e, 0x002b, 0x0029, 0x0027,
	0x0025, 0x0023, 0x0021, 0x001f, 0x001e, 0x001c, 0x001b, 0x0019,
	0x0018, 0x0017, 0x0016, 0x0014, 0x0000
};

/*
 * when we are in S/PDIF mode, we want to disable any analog output so
 * we filter the mixer ioctls 
 */
static int mixdev_ioctl(struct ac97_codec *codec, unsigned int cmd, unsigned long arg)
{
	struct es1371_state *s = (struct es1371_state *)codec->private_data;
	int val;
	unsigned long flags;
	unsigned int left, right;

	VALIDATE_STATE(s);
	/* filter mixer ioctls to catch PCM and MASTER volume when in S/PDIF mode */
	if (s->spdif_volume == -1)
		return codec->mixer_ioctl(codec, cmd, arg);
	switch (cmd) {
	case SOUND_MIXER_WRITE_VOLUME:
		return 0;

	case SOUND_MIXER_WRITE_PCM:   /* use SRC for PCM volume */
		if (get_user(val, (int __user *)arg))
			return -EFAULT;
		right = ((val >> 8)  & 0xff);
		left = (val  & 0xff);
		if (right > 100)
			right = 100;
		if (left > 100)
			left = 100;
		s->spdif_volume = (right << 8) | left;
		spin_lock_irqsave(&s->lock, flags);
		src_write(s, SRCREG_VOL_DAC2, DACVolTable[100 - left]);
		src_write(s, SRCREG_VOL_DAC2+1, DACVolTable[100 - right]);
		spin_unlock_irqrestore(&s->lock, flags);
		return 0;
	
	case SOUND_MIXER_READ_PCM:
		return put_user(s->spdif_volume, (int __user *)arg);
	}
	return codec->mixer_ioctl(codec, cmd, arg);
}

/* --------------------------------------------------------------------- */

/*
 * AC97 Mixer Register to Connections mapping of the Concert 97 board
 *
 * AC97_MASTER_VOL_STEREO   Line Out
 * AC97_MASTER_VOL_MONO     TAD Output
 * AC97_PCBEEP_VOL          none
 * AC97_PHONE_VOL           TAD Input (mono)
 * AC97_MIC_VOL             MIC Input (mono)
 * AC97_LINEIN_VOL          Line Input (stereo)
 * AC97_CD_VOL              CD Input (stereo)
 * AC97_VIDEO_VOL           none
 * AC97_AUX_VOL             Aux Input (stereo)
 * AC97_PCMOUT_VOL          Wave Output (stereo)
 */

static int es1371_open_mixdev(struct inode *inode, struct file *file)
{
	int minor = iminor(inode);
	struct list_head *list;
	struct es1371_state *s;

	for (list = devs.next; ; list = list->next) {
		if (list == &devs)
			return -ENODEV;
		s = list_entry(list, struct es1371_state, devs);
		if (s->codec->dev_mixer == minor)
			break;
	}
       	VALIDATE_STATE(s);
	file->private_data = s;
	return nonseekable_open(inode, file);
}

static int es1371_release_mixdev(struct inode *inode, struct file *file)
{
	struct es1371_state *s = (struct es1371_state *)file->private_data;
	
	VALIDATE_STATE(s);
	return 0;
}

static int es1371_ioctl_mixdev(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	struct es1371_state *s = (struct es1371_state *)file->private_data;
	struct ac97_codec *codec = s->codec;

	return mixdev_ioctl(codec, cmd, arg);
}

static /*const*/ struct file_operations es1371_mixer_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.ioctl		= es1371_ioctl_mixdev,
	.open		= es1371_open_mixdev,
	.release	= es1371_release_mixdev,
};

/* --------------------------------------------------------------------- */

static int drain_dac1(struct es1371_state *s, int nonblock)
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
		tmo = 3 * HZ * (count + s->dma_dac1.fragsize) / 2 / s->dac1rate;
		tmo >>= sample_shift[(s->sctrl & SCTRL_P1FMT) >> SCTRL_SH_P1FMT];
		if (!schedule_timeout(tmo + 1))
			DBG(printk(KERN_DEBUG PFX "dac1 dma timed out??\n");)
        }
        remove_wait_queue(&s->dma_dac1.wait, &wait);
        set_current_state(TASK_RUNNING);
        if (signal_pending(current))
                return -ERESTARTSYS;
        return 0;
}

static int drain_dac2(struct es1371_state *s, int nonblock)
{
	DECLARE_WAITQUEUE(wait, current);
	unsigned long flags;
	int count, tmo;

	if (s->dma_dac2.mapped || !s->dma_dac2.ready)
		return 0;
        add_wait_queue(&s->dma_dac2.wait, &wait);
        for (;;) {
		__set_current_state(TASK_UNINTERRUPTIBLE);
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
		tmo = 3 * HZ * (count + s->dma_dac2.fragsize) / 2 / s->dac2rate;
		tmo >>= sample_shift[(s->sctrl & SCTRL_P2FMT) >> SCTRL_SH_P2FMT];
		if (!schedule_timeout(tmo + 1))
			DBG(printk(KERN_DEBUG PFX "dac2 dma timed out??\n");)
        }
        remove_wait_queue(&s->dma_dac2.wait, &wait);
        set_current_state(TASK_RUNNING);
        if (signal_pending(current))
                return -ERESTARTSYS;
        return 0;
}

/* --------------------------------------------------------------------- */

static ssize_t es1371_read(struct file *file, char __user *buffer, size_t count, loff_t *ppos)
{
	struct es1371_state *s = (struct es1371_state *)file->private_data;
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
	mutex_lock(&s->sem);
	if (!s->dma_adc.ready && (ret = prog_dmabuf_adc(s)))
		goto out2;
	
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
			mutex_unlock(&s->sem);
			schedule();
			if (signal_pending(current)) {
				if (!ret)
					ret = -ERESTARTSYS;
				goto out2;
			}
			mutex_lock(&s->sem);
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
	mutex_unlock(&s->sem);
out2:
	remove_wait_queue(&s->dma_adc.wait, &wait);
	set_current_state(TASK_RUNNING);
	return ret;
}

static ssize_t es1371_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
	struct es1371_state *s = (struct es1371_state *)file->private_data;
	DECLARE_WAITQUEUE(wait, current);
	ssize_t ret;
	unsigned long flags;
	unsigned swptr;
	int cnt;

	VALIDATE_STATE(s);
	if (s->dma_dac2.mapped)
		return -ENXIO;
	if (!access_ok(VERIFY_READ, buffer, count))
		return -EFAULT;
	mutex_lock(&s->sem);
	if (!s->dma_dac2.ready && (ret = prog_dmabuf_dac2(s)))
		goto out3;
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
			mutex_unlock(&s->sem);
			schedule();
			if (signal_pending(current)) {
				if (!ret)
					ret = -ERESTARTSYS;
				goto out2;
			}
			mutex_lock(&s->sem);
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
	mutex_unlock(&s->sem);
out2:
	remove_wait_queue(&s->dma_dac2.wait, &wait);
out3:	
	set_current_state(TASK_RUNNING);
	return ret;
}

/* No kernel lock - we have our own spinlock */
static unsigned int es1371_poll(struct file *file, struct poll_table_struct *wait)
{
	struct es1371_state *s = (struct es1371_state *)file->private_data;
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
	es1371_update_ptr(s);
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

static int es1371_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct es1371_state *s = (struct es1371_state *)file->private_data;
	struct dmabuf *db;
	int ret = 0;
	unsigned long size;

	VALIDATE_STATE(s);
	lock_kernel();
	mutex_lock(&s->sem);
	
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
	} else {
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
	mutex_unlock(&s->sem);
	unlock_kernel();
	return ret;
}

static int es1371_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	struct es1371_state *s = (struct es1371_state *)file->private_data;
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
			if (file->f_mode & FMODE_READ) {
				stop_adc(s);
				s->dma_adc.ready = 0;
				set_adc_rate(s, val);
			}
			if (file->f_mode & FMODE_WRITE) {
				stop_dac2(s);
				s->dma_dac2.ready = 0;
				set_dac2_rate(s, val);
			}
		}
		return put_user((file->f_mode & FMODE_READ) ? s->adcrate : s->dac2rate, p);

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
			outl(s->sctrl, s->io+ES1371_REG_SERIAL_CONTROL);
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
			outl(s->sctrl, s->io+ES1371_REG_SERIAL_CONTROL);
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
				outl(s->sctrl, s->io+ES1371_REG_SERIAL_CONTROL);
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
				outl(s->sctrl, s->io+ES1371_REG_SERIAL_CONTROL);
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
				outl(s->sctrl, s->io+ES1371_REG_SERIAL_CONTROL);
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
				outl(s->sctrl, s->io+ES1371_REG_SERIAL_CONTROL);
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
		es1371_update_ptr(s);
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
		es1371_update_ptr(s);
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
		es1371_update_ptr(s);
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
		es1371_update_ptr(s);
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
		es1371_update_ptr(s);
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
		return put_user((file->f_mode & FMODE_READ) ? s->adcrate : s->dac2rate, p);

        case SOUND_PCM_READ_CHANNELS:
		return put_user((s->sctrl & ((file->f_mode & FMODE_READ) ? SCTRL_R1SMB : SCTRL_P2SMB)) ? 2 : 1, p);
		
        case SOUND_PCM_READ_BITS:
		return put_user((s->sctrl & ((file->f_mode & FMODE_READ) ? SCTRL_R1SEB : SCTRL_P2SEB)) ? 16 : 8, p);

        case SOUND_PCM_WRITE_FILTER:
        case SNDCTL_DSP_SETSYNCRO:
        case SOUND_PCM_READ_FILTER:
                return -EINVAL;
		
	}
	return mixdev_ioctl(s->codec, cmd, arg);
}

static int es1371_open(struct inode *inode, struct file *file)
{
	int minor = iminor(inode);
	DECLARE_WAITQUEUE(wait, current);
	unsigned long flags;
	struct list_head *list;
	struct es1371_state *s;

	for (list = devs.next; ; list = list->next) {
		if (list == &devs)
			return -ENODEV;
		s = list_entry(list, struct es1371_state, devs);
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
		s->dma_adc.ossfragshift = s->dma_adc.ossmaxfrags = s->dma_adc.subdivision = 0;
		s->dma_adc.enabled = 1;
		set_adc_rate(s, 8000);
	}
	if (file->f_mode & FMODE_WRITE) {
		s->dma_dac2.ossfragshift = s->dma_dac2.ossmaxfrags = s->dma_dac2.subdivision = 0;
		s->dma_dac2.enabled = 1;
		set_dac2_rate(s, 8000);
	}
	spin_lock_irqsave(&s->lock, flags);
	if (file->f_mode & FMODE_READ) {
		s->sctrl &= ~SCTRL_R1FMT;
		if ((minor & 0xf) == SND_DEV_DSP16)
			s->sctrl |= ES1371_FMT_S16_MONO << SCTRL_SH_R1FMT;
		else
			s->sctrl |= ES1371_FMT_U8_MONO << SCTRL_SH_R1FMT;
	}
	if (file->f_mode & FMODE_WRITE) {
		s->sctrl &= ~SCTRL_P2FMT;
		if ((minor & 0xf) == SND_DEV_DSP16)
			s->sctrl |= ES1371_FMT_S16_MONO << SCTRL_SH_P2FMT;
		else
			s->sctrl |= ES1371_FMT_U8_MONO << SCTRL_SH_P2FMT;
	}
	outl(s->sctrl, s->io+ES1371_REG_SERIAL_CONTROL);
	spin_unlock_irqrestore(&s->lock, flags);
	s->open_mode |= file->f_mode & (FMODE_READ | FMODE_WRITE);
	mutex_unlock(&s->open_mutex);
	mutex_init(&s->sem);
	return nonseekable_open(inode, file);
}

static int es1371_release(struct inode *inode, struct file *file)
{
	struct es1371_state *s = (struct es1371_state *)file->private_data;

	VALIDATE_STATE(s);
	lock_kernel();
	if (file->f_mode & FMODE_WRITE)
		drain_dac2(s, file->f_flags & O_NONBLOCK);
	mutex_lock(&s->open_mutex);
	if (file->f_mode & FMODE_WRITE) {
		stop_dac2(s);
		dealloc_dmabuf(s, &s->dma_dac2);
	}
	if (file->f_mode & FMODE_READ) {
		stop_adc(s);
		dealloc_dmabuf(s, &s->dma_adc);
	}
	s->open_mode &= ~(file->f_mode & (FMODE_READ|FMODE_WRITE));
	mutex_unlock(&s->open_mutex);
	wake_up(&s->open_wait);
	unlock_kernel();
	return 0;
}

static /*const*/ struct file_operations es1371_audio_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.read		= es1371_read,
	.write		= es1371_write,
	.poll		= es1371_poll,
	.ioctl		= es1371_ioctl,
	.mmap		= es1371_mmap,
	.open		= es1371_open,
	.release	= es1371_release,
};

/* --------------------------------------------------------------------- */

static ssize_t es1371_write_dac(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
	struct es1371_state *s = (struct es1371_state *)file->private_data;
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
static unsigned int es1371_poll_dac(struct file *file, struct poll_table_struct *wait)
{
	struct es1371_state *s = (struct es1371_state *)file->private_data;
	unsigned long flags;
	unsigned int mask = 0;

	VALIDATE_STATE(s);
	if (!s->dma_dac1.ready && prog_dmabuf_dac1(s))
		return 0;
	poll_wait(file, &s->dma_dac1.wait, wait);
	spin_lock_irqsave(&s->lock, flags);
	es1371_update_ptr(s);
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

static int es1371_mmap_dac(struct file *file, struct vm_area_struct *vma)
{
	struct es1371_state *s = (struct es1371_state *)file->private_data;
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

static int es1371_ioctl_dac(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	struct es1371_state *s = (struct es1371_state *)file->private_data;
	unsigned long flags;
        audio_buf_info abinfo;
        count_info cinfo;
	int count;
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
			set_dac1_rate(s, val);
		}
		return put_user(s->dac1rate, p);

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
		outl(s->sctrl, s->io+ES1371_REG_SERIAL_CONTROL);
		spin_unlock_irqrestore(&s->lock, flags);
		return 0;

        case SNDCTL_DSP_CHANNELS:
                if (get_user(val, p))
			return -EFAULT;
		if (val != 0) {
			stop_dac1(s);
			s->dma_dac1.ready = 0;
			spin_lock_irqsave(&s->lock, flags);
			if (val >= 2)
				s->sctrl |= SCTRL_P1SMB;
			else
				s->sctrl &= ~SCTRL_P1SMB;
			outl(s->sctrl, s->io+ES1371_REG_SERIAL_CONTROL);
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
			outl(s->sctrl, s->io+ES1371_REG_SERIAL_CONTROL);
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
		es1371_update_ptr(s);
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
		es1371_update_ptr(s);
                count = s->dma_dac1.count;
		spin_unlock_irqrestore(&s->lock, flags);
		if (count < 0)
			count = 0;
		return put_user(count, p);

        case SNDCTL_DSP_GETOPTR:
		if (!s->dma_dac1.ready && (val = prog_dmabuf_dac1(s)) != 0)
			return val;
		spin_lock_irqsave(&s->lock, flags);
		es1371_update_ptr(s);
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
		return put_user(s->dac1rate, p);

        case SOUND_PCM_READ_CHANNELS:
		return put_user((s->sctrl & SCTRL_P1SMB) ? 2 : 1, p);

        case SOUND_PCM_READ_BITS:
		return put_user((s->sctrl & SCTRL_P1SEB) ? 16 : 8, p);

        case SOUND_PCM_WRITE_FILTER:
        case SNDCTL_DSP_SETSYNCRO:
        case SOUND_PCM_READ_FILTER:
                return -EINVAL;
		
	}
	return mixdev_ioctl(s->codec, cmd, arg);
}

static int es1371_open_dac(struct inode *inode, struct file *file)
{
	int minor = iminor(inode);
	DECLARE_WAITQUEUE(wait, current);
	unsigned long flags;
	struct list_head *list;
	struct es1371_state *s;

	for (list = devs.next; ; list = list->next) {
		if (list == &devs)
			return -ENODEV;
		s = list_entry(list, struct es1371_state, devs);
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
	set_dac1_rate(s, 8000);
	spin_lock_irqsave(&s->lock, flags);
	s->sctrl &= ~SCTRL_P1FMT;
	if ((minor & 0xf) == SND_DEV_DSP16)
		s->sctrl |= ES1371_FMT_S16_MONO << SCTRL_SH_P1FMT;
	else
		s->sctrl |= ES1371_FMT_U8_MONO << SCTRL_SH_P1FMT;
	outl(s->sctrl, s->io+ES1371_REG_SERIAL_CONTROL);
	spin_unlock_irqrestore(&s->lock, flags);
	s->open_mode |= FMODE_DAC;
	mutex_unlock(&s->open_mutex);
	return nonseekable_open(inode, file);
}

static int es1371_release_dac(struct inode *inode, struct file *file)
{
	struct es1371_state *s = (struct es1371_state *)file->private_data;

	VALIDATE_STATE(s);
	lock_kernel();
	drain_dac1(s, file->f_flags & O_NONBLOCK);
	mutex_lock(&s->open_mutex);
	stop_dac1(s);
	dealloc_dmabuf(s, &s->dma_dac1);
	s->open_mode &= ~FMODE_DAC;
	mutex_unlock(&s->open_mutex);
	wake_up(&s->open_wait);
	unlock_kernel();
	return 0;
}

static /*const*/ struct file_operations es1371_dac_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.write		= es1371_write_dac,
	.poll		= es1371_poll_dac,
	.ioctl		= es1371_ioctl_dac,
	.mmap		= es1371_mmap_dac,
	.open		= es1371_open_dac,
	.release	= es1371_release_dac,
};

/* --------------------------------------------------------------------- */

static ssize_t es1371_midi_read(struct file *file, char __user *buffer, size_t count, loff_t *ppos)
{
	struct es1371_state *s = (struct es1371_state *)file->private_data;
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

static ssize_t es1371_midi_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
	struct es1371_state *s = (struct es1371_state *)file->private_data;
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
			es1371_handle_midi(s);
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
		es1371_handle_midi(s);
		spin_unlock_irqrestore(&s->lock, flags);
	}
	__set_current_state(TASK_RUNNING);
	remove_wait_queue(&s->midi.owait, &wait);
	return ret;
}

/* No kernel lock - we have our own spinlock */
static unsigned int es1371_midi_poll(struct file *file, struct poll_table_struct *wait)
{
	struct es1371_state *s = (struct es1371_state *)file->private_data;
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

static int es1371_midi_open(struct inode *inode, struct file *file)
{
	int minor = iminor(inode);
	DECLARE_WAITQUEUE(wait, current);
	unsigned long flags;
	struct list_head *list;
	struct es1371_state *s;

	for (list = devs.next; ; list = list->next) {
		if (list == &devs)
			return -ENODEV;
		s = list_entry(list, struct es1371_state, devs);
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
		outb(UCTRL_CNTRL_SWR, s->io+ES1371_REG_UART_CONTROL);
		outb(0, s->io+ES1371_REG_UART_CONTROL);
		outb(0, s->io+ES1371_REG_UART_TEST);
	}
	if (file->f_mode & FMODE_READ) {
		s->midi.ird = s->midi.iwr = s->midi.icnt = 0;
	}
	if (file->f_mode & FMODE_WRITE) {
		s->midi.ord = s->midi.owr = s->midi.ocnt = 0;
	}
	s->ctrl |= CTRL_UART_EN;
	outl(s->ctrl, s->io+ES1371_REG_CONTROL);
	es1371_handle_midi(s);
	spin_unlock_irqrestore(&s->lock, flags);
	s->open_mode |= (file->f_mode << FMODE_MIDI_SHIFT) & (FMODE_MIDI_READ | FMODE_MIDI_WRITE);
	mutex_unlock(&s->open_mutex);
	return nonseekable_open(inode, file);
}

static int es1371_midi_release(struct inode *inode, struct file *file)
{
	struct es1371_state *s = (struct es1371_state *)file->private_data;
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
				printk(KERN_DEBUG PFX "midi timed out??\n");
		}
		remove_wait_queue(&s->midi.owait, &wait);
		set_current_state(TASK_RUNNING);
	}
	mutex_lock(&s->open_mutex);
	s->open_mode &= ~((file->f_mode << FMODE_MIDI_SHIFT) & (FMODE_MIDI_READ|FMODE_MIDI_WRITE));
	spin_lock_irqsave(&s->lock, flags);
	if (!(s->open_mode & (FMODE_MIDI_READ | FMODE_MIDI_WRITE))) {
		s->ctrl &= ~CTRL_UART_EN;
		outl(s->ctrl, s->io+ES1371_REG_CONTROL);
	}
	spin_unlock_irqrestore(&s->lock, flags);
	mutex_unlock(&s->open_mutex);
	wake_up(&s->open_wait);
	unlock_kernel();
	return 0;
}

static /*const*/ struct file_operations es1371_midi_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.read		= es1371_midi_read,
	.write		= es1371_midi_write,
	.poll		= es1371_midi_poll,
	.open		= es1371_midi_open,
	.release	= es1371_midi_release,
};

/* --------------------------------------------------------------------- */

/*
 * for debugging purposes, we'll create a proc device that dumps the
 * CODEC chipstate
 */

#ifdef ES1371_DEBUG
static int proc_es1371_dump (char *buf, char **start, off_t fpos, int length, int *eof, void *data)
{
	struct es1371_state *s;
        int cnt, len = 0;

	if (list_empty(&devs))
		return 0;
	s = list_entry(devs.next, struct es1371_state, devs);
        /* print out header */
        len += sprintf(buf + len, "\t\tCreative ES137x Debug Dump-o-matic\n");

        /* print out CODEC state */
        len += sprintf (buf + len, "AC97 CODEC state\n");
	for (cnt=0; cnt <= 0x7e; cnt = cnt +2)
                len+= sprintf (buf + len, "reg:0x%02x  val:0x%04x\n", cnt, rdcodec(s->codec, cnt));

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
#endif /* ES1371_DEBUG */

/* --------------------------------------------------------------------- */

/* maximum number of devices; only used for command line params */
#define NR_DEVICE 5

static int spdif[NR_DEVICE];
static int nomix[NR_DEVICE];
static int amplifier[NR_DEVICE];

static unsigned int devindex;

module_param_array(spdif, bool, NULL, 0);
MODULE_PARM_DESC(spdif, "if 1 the output is in S/PDIF digital mode");
module_param_array(nomix, bool, NULL, 0);
MODULE_PARM_DESC(nomix, "if 1 no analog audio is mixed to the digital output");
module_param_array(amplifier, bool, NULL, 0);
MODULE_PARM_DESC(amplifier, "Set to 1 if the machine needs the amp control enabling (many laptops)");

MODULE_AUTHOR("Thomas M. Sailer, sailer@ife.ee.ethz.ch, hb9jnx@hb9w.che.eu");
MODULE_DESCRIPTION("ES1371 AudioPCI97 Driver");
MODULE_LICENSE("GPL");


/* --------------------------------------------------------------------- */

static struct initvol {
	int mixch;
	int vol;
} initvol[] __devinitdata = {
	{ SOUND_MIXER_WRITE_LINE, 0x4040 },
	{ SOUND_MIXER_WRITE_CD, 0x4040 },
	{ MIXER_WRITE(SOUND_MIXER_VIDEO), 0x4040 },
	{ SOUND_MIXER_WRITE_LINE1, 0x4040 },
	{ SOUND_MIXER_WRITE_PCM, 0x4040 },
	{ SOUND_MIXER_WRITE_VOLUME, 0x4040 },
	{ MIXER_WRITE(SOUND_MIXER_PHONEOUT), 0x4040 },
	{ SOUND_MIXER_WRITE_OGAIN, 0x4040 },
	{ MIXER_WRITE(SOUND_MIXER_PHONEIN), 0x4040 },
	{ SOUND_MIXER_WRITE_SPEAKER, 0x4040 },
	{ SOUND_MIXER_WRITE_MIC, 0x4040 },
	{ SOUND_MIXER_WRITE_RECLEV, 0x4040 },
	{ SOUND_MIXER_WRITE_IGAIN, 0x4040 }
};

static struct
{
	short svid, sdid;
} amplifier_needed[] = 
{
	{ 0x107B, 0x2150 },		/* Gateway Solo 2150 */
	{ 0x13BD, 0x100C },		/* Mebius PC-MJ100V */
	{ 0x1102, 0x5938 },		/* Targa Xtender 300 */
	{ 0x1102, 0x8938 },		/* IPC notebook */
	{ PCI_ANY_ID, PCI_ANY_ID }
};

#ifdef SUPPORT_JOYSTICK

static int __devinit es1371_register_gameport(struct es1371_state *s)
{
	struct gameport *gp;
	int gpio;

	for (gpio = 0x218; gpio >= 0x200; gpio -= 0x08)
		if (request_region(gpio, JOY_EXTENT, "es1371"))
			break;

	if (gpio < 0x200) {
		printk(KERN_ERR PFX "no free joystick address found\n");
		return -EBUSY;
	}

	s->gameport = gp = gameport_allocate_port();
	if (!gp) {
		printk(KERN_ERR PFX "can not allocate memory for gameport\n");
		release_region(gpio, JOY_EXTENT);
		return -ENOMEM;
	}

	gameport_set_name(gp, "ESS1371 Gameport");
	gameport_set_phys(gp, "isa%04x/gameport0", gpio);
	gp->dev.parent = &s->dev->dev;
	gp->io = gpio;

	s->ctrl |= CTRL_JYSTK_EN | (((gpio >> 3) & CTRL_JOY_MASK) << CTRL_JOY_SHIFT);
	outl(s->ctrl, s->io + ES1371_REG_CONTROL);

	gameport_register_port(gp);

	return 0;
}

static inline void es1371_unregister_gameport(struct es1371_state *s)
{
	if (s->gameport) {
		int gpio = s->gameport->io;
		gameport_unregister_port(s->gameport);
		release_region(gpio, JOY_EXTENT);

	}
}

#else
static inline int es1371_register_gameport(struct es1371_state *s) { return -ENOSYS; }
static inline void es1371_unregister_gameport(struct es1371_state *s) { }
#endif /* SUPPORT_JOYSTICK */


static int __devinit es1371_probe(struct pci_dev *pcidev, const struct pci_device_id *pciid)
{
	struct es1371_state *s;
	mm_segment_t fs;
	int i, val, res = -1;
	int idx;
	unsigned long tmo;
	signed long tmo2;
	unsigned int cssr;

	if ((res=pci_enable_device(pcidev)))
		return res;

	if (!(pci_resource_flags(pcidev, 0) & IORESOURCE_IO))
		return -ENODEV;
	if (pcidev->irq == 0) 
		return -ENODEV;
	i = pci_set_dma_mask(pcidev, DMA_32BIT_MASK);
	if (i) {
		printk(KERN_WARNING "es1371: architecture does not support 32bit PCI busmaster DMA\n");
		return i;
	}
	if (!(s = kmalloc(sizeof(struct es1371_state), GFP_KERNEL))) {
		printk(KERN_WARNING PFX "out of memory\n");
		return -ENOMEM;
	}
	memset(s, 0, sizeof(struct es1371_state));
	
	s->codec = ac97_alloc_codec();
	if(s->codec == NULL)
		goto err_codec;
		
	init_waitqueue_head(&s->dma_adc.wait);
	init_waitqueue_head(&s->dma_dac1.wait);
	init_waitqueue_head(&s->dma_dac2.wait);
	init_waitqueue_head(&s->open_wait);
	init_waitqueue_head(&s->midi.iwait);
	init_waitqueue_head(&s->midi.owait);
	mutex_init(&s->open_mutex);
	spin_lock_init(&s->lock);
	s->magic = ES1371_MAGIC;
	s->dev = pcidev;
	s->io = pci_resource_start(pcidev, 0);
	s->irq = pcidev->irq;
	s->vendor = pcidev->vendor;
	s->device = pcidev->device;
	pci_read_config_byte(pcidev, PCI_REVISION_ID, &s->rev);
	s->codec->private_data = s;
	s->codec->id = 0;
	s->codec->codec_read = rdcodec;
	s->codec->codec_write = wrcodec;
	printk(KERN_INFO PFX "found chip, vendor id 0x%04x device id 0x%04x revision 0x%02x\n",
	       s->vendor, s->device, s->rev);
	if (!request_region(s->io, ES1371_EXTENT, "es1371")) {
		printk(KERN_ERR PFX "io ports %#lx-%#lx in use\n", s->io, s->io+ES1371_EXTENT-1);
		res = -EBUSY;
		goto err_region;
	}
	if ((res=request_irq(s->irq, es1371_interrupt, SA_SHIRQ, "es1371",s))) {
		printk(KERN_ERR PFX "irq %u in use\n", s->irq);
		goto err_irq;
	}
	printk(KERN_INFO PFX "found es1371 rev %d at io %#lx irq %u\n",
	       s->rev, s->io, s->irq);
	/* register devices */
	if ((res=(s->dev_audio = register_sound_dsp(&es1371_audio_fops,-1)))<0)
		goto err_dev1;
	if ((res=(s->codec->dev_mixer = register_sound_mixer(&es1371_mixer_fops, -1))) < 0)
		goto err_dev2;
	if ((res=(s->dev_dac = register_sound_dsp(&es1371_dac_fops, -1))) < 0)
		goto err_dev3;
	if ((res=(s->dev_midi = register_sound_midi(&es1371_midi_fops, -1)))<0 )
		goto err_dev4;
#ifdef ES1371_DEBUG
	/* initialize the debug proc device */
	s->ps = create_proc_read_entry("es1371",0,NULL,proc_es1371_dump,NULL);
#endif /* ES1371_DEBUG */
	
	/* initialize codec registers */
	s->ctrl = 0;

	/* Check amplifier requirements */
	
	if (amplifier[devindex])
		s->ctrl |= CTRL_GPIO_OUT0;
	else for(idx = 0; amplifier_needed[idx].svid != PCI_ANY_ID; idx++)
	{
		if(pcidev->subsystem_vendor == amplifier_needed[idx].svid &&
		   pcidev->subsystem_device == amplifier_needed[idx].sdid)
		{
                    	s->ctrl |= CTRL_GPIO_OUT0;   /* turn internal amplifier on */
                    	printk(KERN_INFO PFX "Enabling internal amplifier.\n");
		}
	}

	s->sctrl = 0;
	cssr = 0;
	s->spdif_volume = -1;
	/* check to see if s/pdif mode is being requested */
	if (spdif[devindex]) {
		if (s->rev >= 4) {
			printk(KERN_INFO PFX "enabling S/PDIF output\n");
			s->spdif_volume = 0;
			cssr |= STAT_EN_SPDIF;
			s->ctrl |= CTRL_SPDIFEN_B;
			if (nomix[devindex]) /* don't mix analog inputs to s/pdif output */
				s->ctrl |= CTRL_RECEN_B;
		} else {
			printk(KERN_ERR PFX "revision %d does not support S/PDIF\n", s->rev);
		}
	}
	/* initialize the chips */
	outl(s->ctrl, s->io+ES1371_REG_CONTROL);
	outl(s->sctrl, s->io+ES1371_REG_SERIAL_CONTROL);
	outl(LEGACY_JFAST, s->io+ES1371_REG_LEGACY);
	pci_set_master(pcidev);  /* enable bus mastering */
	/* if we are a 5880 turn on the AC97 */
	if (s->vendor == PCI_VENDOR_ID_ENSONIQ &&
	    ((s->device == PCI_DEVICE_ID_ENSONIQ_CT5880 && s->rev >= CT5880REV_CT5880_C) || 
	     (s->device == PCI_DEVICE_ID_ENSONIQ_ES1371 && s->rev == ES1371REV_CT5880_A) || 
	     (s->device == PCI_DEVICE_ID_ENSONIQ_ES1371 && s->rev == ES1371REV_ES1373_8))) { 
		cssr |= CSTAT_5880_AC97_RST;
		outl(cssr, s->io+ES1371_REG_STATUS);
		/* need to delay around 20ms(bleech) to give
		   some CODECs enough time to wakeup */
		tmo = jiffies + (HZ / 50) + 1;
		for (;;) {
			tmo2 = tmo - jiffies;
			if (tmo2 <= 0)
				break;
			schedule_timeout(tmo2);
		}
	}
	/* AC97 warm reset to start the bitclk */
	outl(s->ctrl | CTRL_SYNCRES, s->io+ES1371_REG_CONTROL);
	udelay(2);
	outl(s->ctrl, s->io+ES1371_REG_CONTROL);
	/* init the sample rate converter */
	src_init(s);
	/* codec init */
	if (!ac97_probe_codec(s->codec)) {
		res = -ENODEV;
		goto err_gp;
	}
	/* set default values */

	fs = get_fs();
	set_fs(KERNEL_DS);
	val = SOUND_MASK_LINE;
	mixdev_ioctl(s->codec, SOUND_MIXER_WRITE_RECSRC, (unsigned long)&val);
	for (i = 0; i < sizeof(initvol)/sizeof(initvol[0]); i++) {
		val = initvol[i].vol;
		mixdev_ioctl(s->codec, initvol[i].mixch, (unsigned long)&val);
	}
	/* mute master and PCM when in S/PDIF mode */
	if (s->spdif_volume != -1) {
		val = 0x0000;
		s->codec->mixer_ioctl(s->codec, SOUND_MIXER_WRITE_VOLUME, (unsigned long)&val);
		s->codec->mixer_ioctl(s->codec, SOUND_MIXER_WRITE_PCM, (unsigned long)&val);
	}
	set_fs(fs);
	/* turn on S/PDIF output driver if requested */
	outl(cssr, s->io+ES1371_REG_STATUS);

	es1371_register_gameport(s);

	/* store it in the driver field */
	pci_set_drvdata(pcidev, s);
	/* put it into driver list */
	list_add_tail(&s->devs, &devs);
	/* increment devindex */
	if (devindex < NR_DEVICE-1)
		devindex++;
	return 0;

 err_gp:
#ifdef ES1371_DEBUG
	if (s->ps)
		remove_proc_entry("es1371", NULL);
#endif
	unregister_sound_midi(s->dev_midi);
 err_dev4:
	unregister_sound_dsp(s->dev_dac);
 err_dev3:
	unregister_sound_mixer(s->codec->dev_mixer);
 err_dev2:
	unregister_sound_dsp(s->dev_audio);
 err_dev1:
	printk(KERN_ERR PFX "cannot register misc device\n");
	free_irq(s->irq, s);
 err_irq:
	release_region(s->io, ES1371_EXTENT);
 err_region:
 err_codec:
	ac97_release_codec(s->codec);
	kfree(s);
	return res;
}

static void __devexit es1371_remove(struct pci_dev *dev)
{
	struct es1371_state *s = pci_get_drvdata(dev);

	if (!s)
		return;
	list_del(&s->devs);
#ifdef ES1371_DEBUG
	if (s->ps)
		remove_proc_entry("es1371", NULL);
#endif /* ES1371_DEBUG */
	outl(0, s->io+ES1371_REG_CONTROL); /* switch everything off */
	outl(0, s->io+ES1371_REG_SERIAL_CONTROL); /* clear serial interrupts */
	synchronize_irq(s->irq);
	free_irq(s->irq, s);
	es1371_unregister_gameport(s);
	release_region(s->io, ES1371_EXTENT);
	unregister_sound_dsp(s->dev_audio);
	unregister_sound_mixer(s->codec->dev_mixer);
	unregister_sound_dsp(s->dev_dac);
	unregister_sound_midi(s->dev_midi);
	ac97_release_codec(s->codec);
	kfree(s);
	pci_set_drvdata(dev, NULL);
}

static struct pci_device_id id_table[] = {
	{ PCI_VENDOR_ID_ENSONIQ, PCI_DEVICE_ID_ENSONIQ_ES1371, PCI_ANY_ID, PCI_ANY_ID, 0, 0 },
	{ PCI_VENDOR_ID_ENSONIQ, PCI_DEVICE_ID_ENSONIQ_CT5880, PCI_ANY_ID, PCI_ANY_ID, 0, 0 },
	{ PCI_VENDOR_ID_ECTIVA, PCI_DEVICE_ID_ECTIVA_EV1938, PCI_ANY_ID, PCI_ANY_ID, 0, 0 },
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, id_table);

static struct pci_driver es1371_driver = {
	.name		= "es1371",
	.id_table	= id_table,
	.probe		= es1371_probe,
	.remove		= __devexit_p(es1371_remove),
};

static int __init init_es1371(void)
{
	printk(KERN_INFO PFX "version v0.32 time " __TIME__ " " __DATE__ "\n");
	return pci_register_driver(&es1371_driver);
}

static void __exit cleanup_es1371(void)
{
	printk(KERN_INFO PFX "unloading\n");
	pci_unregister_driver(&es1371_driver);
}

module_init(init_es1371);
module_exit(cleanup_es1371);

/* --------------------------------------------------------------------- */

#ifndef MODULE

/* format is: es1371=[spdif,[nomix,[amplifier]]] */

static int __init es1371_setup(char *str)
{
	static unsigned __initdata nr_dev = 0;

	if (nr_dev >= NR_DEVICE)
		return 0;

	(void)
        ((get_option(&str, &spdif[nr_dev]) == 2)
         && (get_option(&str, &nomix[nr_dev]) == 2)
         && (get_option(&str, &amplifier[nr_dev])));

	nr_dev++;
	return 1;
}

__setup("es1371=", es1371_setup);

#endif /* MODULE */
