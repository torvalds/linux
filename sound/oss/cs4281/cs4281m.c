/*******************************************************************************
*
*      "cs4281.c" --  Cirrus Logic-Crystal CS4281 linux audio driver.
*
*      Copyright (C) 2000,2001  Cirrus Logic Corp.  
*            -- adapted from drivers by Thomas Sailer, 
*            -- but don't bug him; Problems should go to:
*            -- tom woller (twoller@crystal.cirrus.com) or
*               (audio@crystal.cirrus.com).
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
*   none
*
*  Supported devices:
*  /dev/dsp    standard /dev/dsp device, (mostly) OSS compatible
*  /dev/mixer  standard /dev/mixer device, (mostly) OSS compatible
*  /dev/midi   simple MIDI UART interface, no ioctl
*
* Modification History
* 08/20/00 trw - silence and no stopping DAC until release
* 08/23/00 trw - added CS_DBG statements, fix interrupt hang issue on DAC stop.
* 09/18/00 trw - added 16bit only record with conversion 
* 09/24/00 trw - added Enhanced Full duplex (separate simultaneous 
*                capture/playback rates)
* 10/03/00 trw - fixed mmap (fixed GRECORD and the XMMS mmap test plugin  
*                libOSSm.so)
* 10/11/00 trw - modified for 2.4.0-test9 kernel enhancements (NR_MAP removal)
* 11/03/00 trw - fixed interrupt loss/stutter, added debug.
* 11/10/00 bkz - added __devinit to cs4281_hw_init()
* 11/10/00 trw - fixed SMP and capture spinlock hang.
* 12/04/00 trw - cleaned up CSDEBUG flags and added "defaultorder" moduleparm.
* 12/05/00 trw - fixed polling (myth2), and added underrun swptr fix.
* 12/08/00 trw - added PM support. 
* 12/14/00 trw - added wrapper code, builds under 2.4.0, 2.2.17-20, 2.2.17-8 
*		 (RH/Dell base), 2.2.18, 2.2.12.  cleaned up code mods by ident.
* 12/19/00 trw - added PM support for 2.2 base (apm_callback). other PM cleanup.
* 12/21/00 trw - added fractional "defaultorder" inputs. if >100 then use 
*		 defaultorder-100 as power of 2 for the buffer size. example:
*		 106 = 2^(106-100) = 2^6 = 64 bytes for the buffer size.
*
*******************************************************************************/

/* uncomment the following line to disable building PM support into the driver */
//#define NOT_CS4281_PM 1 

#include <linux/list.h>
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
#include <linux/interrupt.h>
#include <linux/poll.h>
#include <linux/fs.h>
#include <linux/wait.h>

#include <asm/current.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/page.h>
#include <asm/uaccess.h>

//#include "cs_dm.h"
#include "cs4281_hwdefs.h"
#include "cs4281pm.h"

struct cs4281_state;

static void stop_dac(struct cs4281_state *s);
static void stop_adc(struct cs4281_state *s);
static void start_dac(struct cs4281_state *s);
static void start_adc(struct cs4281_state *s);
#undef OSS_DOCUMENTED_MIXER_SEMANTICS

// --------------------------------------------------------------------- 

#ifndef PCI_VENDOR_ID_CIRRUS
#define PCI_VENDOR_ID_CIRRUS          0x1013
#endif
#ifndef PCI_DEVICE_ID_CRYSTAL_CS4281
#define PCI_DEVICE_ID_CRYSTAL_CS4281  0x6005
#endif

#define CS4281_MAGIC  ((PCI_DEVICE_ID_CRYSTAL_CS4281<<16) | PCI_VENDOR_ID_CIRRUS)
#define	CS4281_CFLR_DEFAULT	0x00000001  /* CFLR must be in AC97 link mode */

// buffer order determines the size of the dma buffer for the driver.
// under Linux, a smaller buffer allows more responsiveness from many of the 
// applications (e.g. games).  A larger buffer allows some of the apps (esound) 
// to not underrun the dma buffer as easily.  As default, use 32k (order=3)
// rather than 64k as some of the games work more responsively.
// log base 2( buff sz = 32k).
static unsigned long defaultorder = 3;
module_param(defaultorder, ulong, 0);

//
// Turn on/off debugging compilation by commenting out "#define CSDEBUG"
//
#define CSDEBUG 1
#if CSDEBUG
#define CSDEBUG_INTERFACE 1
#else
#undef CSDEBUG_INTERFACE
#endif
//
// cs_debugmask areas
//
#define CS_INIT	 	0x00000001	// initialization and probe functions
#define CS_ERROR 	0x00000002	// tmp debugging bit placeholder
#define CS_INTERRUPT	0x00000004	// interrupt handler (separate from all other)
#define CS_FUNCTION 	0x00000008	// enter/leave functions
#define CS_WAVE_WRITE 	0x00000010	// write information for wave
#define CS_WAVE_READ 	0x00000020	// read information for wave
#define CS_MIDI_WRITE 	0x00000040	// write information for midi
#define CS_MIDI_READ 	0x00000080	// read information for midi
#define CS_MPU401_WRITE 0x00000100	// write information for mpu401
#define CS_MPU401_READ 	0x00000200	// read information for mpu401
#define CS_OPEN		0x00000400	// all open functions in the driver
#define CS_RELEASE	0x00000800	// all release functions in the driver
#define CS_PARMS	0x00001000	// functional and operational parameters
#define CS_IOCTL	0x00002000	// ioctl (non-mixer)
#define CS_PM		0x00004000	// power management 
#define CS_TMP		0x10000000	// tmp debug mask bit

#define CS_IOCTL_CMD_SUSPEND	0x1	// suspend
#define CS_IOCTL_CMD_RESUME	0x2	// resume
//
// CSDEBUG is usual mode is set to 1, then use the
// cs_debuglevel and cs_debugmask to turn on or off debugging.
// Debug level of 1 has been defined to be kernel errors and info
// that should be printed on any released driver.
//
#if CSDEBUG
#define CS_DBGOUT(mask,level,x) if((cs_debuglevel >= (level)) && ((mask) & cs_debugmask) ) {x;}
#else
#define CS_DBGOUT(mask,level,x)
#endif

#if CSDEBUG
static unsigned long cs_debuglevel = 1;	// levels range from 1-9
static unsigned long cs_debugmask = CS_INIT | CS_ERROR;	// use CS_DBGOUT with various mask values
module_param(cs_debuglevel, ulong, 0);
module_param(cs_debugmask, ulong, 0);
#endif
#define CS_TRUE 	1
#define CS_FALSE 	0

// MIDI buffer sizes 
#define MIDIINBUF  500
#define MIDIOUTBUF 500

#define FMODE_MIDI_SHIFT 3
#define FMODE_MIDI_READ  (FMODE_READ << FMODE_MIDI_SHIFT)
#define FMODE_MIDI_WRITE (FMODE_WRITE << FMODE_MIDI_SHIFT)

#define CS4281_MAJOR_VERSION 	1
#define CS4281_MINOR_VERSION 	13
#ifdef __ia64__
#define CS4281_ARCH	     	64	//architecture key
#else
#define CS4281_ARCH	     	32	//architecture key
#endif

#define CS_TYPE_ADC 0
#define CS_TYPE_DAC 1


static const char invalid_magic[] =
    KERN_CRIT "cs4281: invalid magic value\n";

#define VALIDATE_STATE(s)                         \
({                                                \
        if (!(s) || (s)->magic != CS4281_MAGIC) { \
                printk(invalid_magic);            \
                return -ENXIO;                    \
        }                                         \
})

//LIST_HEAD(cs4281_devs);
static struct list_head cs4281_devs = { &cs4281_devs, &cs4281_devs };

struct cs4281_state; 

#include "cs4281_wrapper-24.c"

struct cs4281_state {
	// magic 
	unsigned int magic;

	// we keep the cards in a linked list 
	struct cs4281_state *next;

	// pcidev is needed to turn off the DDMA controller at driver shutdown 
	struct pci_dev *pcidev;
	struct list_head list;

	// soundcore stuff 
	int dev_audio;
	int dev_mixer;
	int dev_midi;

	// hardware resources 
	unsigned int pBA0phys, pBA1phys;
	char __iomem *pBA0;
	char __iomem *pBA1;
	unsigned int irq;

	// mixer registers 
	struct {
		unsigned short vol[10];
		unsigned int recsrc;
		unsigned int modcnt;
		unsigned short micpreamp;
	} mix;

	// wave stuff   
	struct properties {
		unsigned fmt;
		unsigned fmt_original;	// original requested format
		unsigned channels;
		unsigned rate;
		unsigned char clkdiv;
	} prop_dac, prop_adc;
	unsigned conversion:1;	// conversion from 16 to 8 bit in progress
	void *tmpbuff;		// tmp buffer for sample conversions
	unsigned ena;
	spinlock_t lock;
	struct mutex open_sem;
	struct mutex open_sem_adc;
	struct mutex open_sem_dac;
	mode_t open_mode;
	wait_queue_head_t open_wait;
	wait_queue_head_t open_wait_adc;
	wait_queue_head_t open_wait_dac;

	dma_addr_t dmaaddr_tmpbuff;
	unsigned buforder_tmpbuff;	// Log base 2 of 'rawbuf' size in bytes..
	struct dmabuf {
		void *rawbuf;	// Physical address of  
		dma_addr_t dmaaddr;
		unsigned buforder;	// Log base 2 of 'rawbuf' size in bytes..
		unsigned numfrag;	// # of 'fragments' in the buffer.
		unsigned fragshift;	// Log base 2 of fragment size.
		unsigned hwptr, swptr;
		unsigned total_bytes;	// # bytes process since open.
		unsigned blocks;	// last returned blocks value GETOPTR
		unsigned wakeup;	// interrupt occurred on block 
		int count;
		unsigned underrun;	// underrun flag
		unsigned error;	// over/underrun 
		wait_queue_head_t wait;
		// redundant, but makes calculations easier 
		unsigned fragsize;	// 2**fragshift..
		unsigned dmasize;	// 2**buforder.
		unsigned fragsamples;
		// OSS stuff 
		unsigned mapped:1;	// Buffer mapped in cs4281_mmap()?
		unsigned ready:1;	// prog_dmabuf_dac()/adc() successful?
		unsigned endcleared:1;
		unsigned type:1;	// adc or dac buffer (CS_TYPE_XXX)
		unsigned ossfragshift;
		int ossmaxfrags;
		unsigned subdivision;
	} dma_dac, dma_adc;

	// midi stuff 
	struct {
		unsigned ird, iwr, icnt;
		unsigned ord, owr, ocnt;
		wait_queue_head_t iwait;
		wait_queue_head_t owait;
		struct timer_list timer;
		unsigned char ibuf[MIDIINBUF];
		unsigned char obuf[MIDIOUTBUF];
	} midi;

	struct cs4281_pm pm;
	struct cs4281_pipeline pl[CS4281_NUMBER_OF_PIPELINES];
};

#include "cs4281pm-24.c"

#if CSDEBUG

// DEBUG ROUTINES

#define SOUND_MIXER_CS_GETDBGLEVEL 	_SIOWR('M',120, int)
#define SOUND_MIXER_CS_SETDBGLEVEL 	_SIOWR('M',121, int)
#define SOUND_MIXER_CS_GETDBGMASK 	_SIOWR('M',122, int)
#define SOUND_MIXER_CS_SETDBGMASK 	_SIOWR('M',123, int)

#define SOUND_MIXER_CS_APM	 	_SIOWR('M',124, int)


static void cs_printioctl(unsigned int x)
{
	unsigned int i;
	unsigned char vidx;
	// Index of mixtable1[] member is Device ID 
	// and must be <= SOUND_MIXER_NRDEVICES.
	// Value of array member is index into s->mix.vol[]
	static const unsigned char mixtable1[SOUND_MIXER_NRDEVICES] = {
		[SOUND_MIXER_PCM] = 1,	// voice 
		[SOUND_MIXER_LINE1] = 2,	// AUX
		[SOUND_MIXER_CD] = 3,	// CD 
		[SOUND_MIXER_LINE] = 4,	// Line 
		[SOUND_MIXER_SYNTH] = 5,	// FM
		[SOUND_MIXER_MIC] = 6,	// Mic 
		[SOUND_MIXER_SPEAKER] = 7,	// Speaker 
		[SOUND_MIXER_RECLEV] = 8,	// Recording level 
		[SOUND_MIXER_VOLUME] = 9	// Master Volume 
	};

	switch (x) {
	case SOUND_MIXER_CS_GETDBGMASK:
		CS_DBGOUT(CS_IOCTL, 4,
			  printk("SOUND_MIXER_CS_GETDBGMASK:\n"));
		break;
	case SOUND_MIXER_CS_GETDBGLEVEL:
		CS_DBGOUT(CS_IOCTL, 4,
			  printk("SOUND_MIXER_CS_GETDBGLEVEL:\n"));
		break;
	case SOUND_MIXER_CS_SETDBGMASK:
		CS_DBGOUT(CS_IOCTL, 4,
			  printk("SOUND_MIXER_CS_SETDBGMASK:\n"));
		break;
	case SOUND_MIXER_CS_SETDBGLEVEL:
		CS_DBGOUT(CS_IOCTL, 4,
			  printk("SOUND_MIXER_CS_SETDBGLEVEL:\n"));
		break;
	case OSS_GETVERSION:
		CS_DBGOUT(CS_IOCTL, 4, printk("OSS_GETVERSION:\n"));
		break;
	case SNDCTL_DSP_SYNC:
		CS_DBGOUT(CS_IOCTL, 4, printk("SNDCTL_DSP_SYNC:\n"));
		break;
	case SNDCTL_DSP_SETDUPLEX:
		CS_DBGOUT(CS_IOCTL, 4, printk("SNDCTL_DSP_SETDUPLEX:\n"));
		break;
	case SNDCTL_DSP_GETCAPS:
		CS_DBGOUT(CS_IOCTL, 4, printk("SNDCTL_DSP_GETCAPS:\n"));
		break;
	case SNDCTL_DSP_RESET:
		CS_DBGOUT(CS_IOCTL, 4, printk("SNDCTL_DSP_RESET:\n"));
		break;
	case SNDCTL_DSP_SPEED:
		CS_DBGOUT(CS_IOCTL, 4, printk("SNDCTL_DSP_SPEED:\n"));
		break;
	case SNDCTL_DSP_STEREO:
		CS_DBGOUT(CS_IOCTL, 4, printk("SNDCTL_DSP_STEREO:\n"));
		break;
	case SNDCTL_DSP_CHANNELS:
		CS_DBGOUT(CS_IOCTL, 4, printk("SNDCTL_DSP_CHANNELS:\n"));
		break;
	case SNDCTL_DSP_GETFMTS:
		CS_DBGOUT(CS_IOCTL, 4, printk("SNDCTL_DSP_GETFMTS:\n"));
		break;
	case SNDCTL_DSP_SETFMT:
		CS_DBGOUT(CS_IOCTL, 4, printk("SNDCTL_DSP_SETFMT:\n"));
		break;
	case SNDCTL_DSP_POST:
		CS_DBGOUT(CS_IOCTL, 4, printk("SNDCTL_DSP_POST:\n"));
		break;
	case SNDCTL_DSP_GETTRIGGER:
		CS_DBGOUT(CS_IOCTL, 4, printk("SNDCTL_DSP_GETTRIGGER:\n"));
		break;
	case SNDCTL_DSP_SETTRIGGER:
		CS_DBGOUT(CS_IOCTL, 4, printk("SNDCTL_DSP_SETTRIGGER:\n"));
		break;
	case SNDCTL_DSP_GETOSPACE:
		CS_DBGOUT(CS_IOCTL, 4, printk("SNDCTL_DSP_GETOSPACE:\n"));
		break;
	case SNDCTL_DSP_GETISPACE:
		CS_DBGOUT(CS_IOCTL, 4, printk("SNDCTL_DSP_GETISPACE:\n"));
		break;
	case SNDCTL_DSP_NONBLOCK:
		CS_DBGOUT(CS_IOCTL, 4, printk("SNDCTL_DSP_NONBLOCK:\n"));
		break;
	case SNDCTL_DSP_GETODELAY:
		CS_DBGOUT(CS_IOCTL, 4, printk("SNDCTL_DSP_GETODELAY:\n"));
		break;
	case SNDCTL_DSP_GETIPTR:
		CS_DBGOUT(CS_IOCTL, 4, printk("SNDCTL_DSP_GETIPTR:\n"));
		break;
	case SNDCTL_DSP_GETOPTR:
		CS_DBGOUT(CS_IOCTL, 4, printk("SNDCTL_DSP_GETOPTR:\n"));
		break;
	case SNDCTL_DSP_GETBLKSIZE:
		CS_DBGOUT(CS_IOCTL, 4, printk("SNDCTL_DSP_GETBLKSIZE:\n"));
		break;
	case SNDCTL_DSP_SETFRAGMENT:
		CS_DBGOUT(CS_IOCTL, 4,
			  printk("SNDCTL_DSP_SETFRAGMENT:\n"));
		break;
	case SNDCTL_DSP_SUBDIVIDE:
		CS_DBGOUT(CS_IOCTL, 4, printk("SNDCTL_DSP_SUBDIVIDE:\n"));
		break;
	case SOUND_PCM_READ_RATE:
		CS_DBGOUT(CS_IOCTL, 4, printk("SOUND_PCM_READ_RATE:\n"));
		break;
	case SOUND_PCM_READ_CHANNELS:
		CS_DBGOUT(CS_IOCTL, 4,
			  printk("SOUND_PCM_READ_CHANNELS:\n"));
		break;
	case SOUND_PCM_READ_BITS:
		CS_DBGOUT(CS_IOCTL, 4, printk("SOUND_PCM_READ_BITS:\n"));
		break;
	case SOUND_PCM_WRITE_FILTER:
		CS_DBGOUT(CS_IOCTL, 4,
			  printk("SOUND_PCM_WRITE_FILTER:\n"));
		break;
	case SNDCTL_DSP_SETSYNCRO:
		CS_DBGOUT(CS_IOCTL, 4, printk("SNDCTL_DSP_SETSYNCRO:\n"));
		break;
	case SOUND_PCM_READ_FILTER:
		CS_DBGOUT(CS_IOCTL, 4, printk("SOUND_PCM_READ_FILTER:\n"));
		break;
	case SOUND_MIXER_PRIVATE1:
		CS_DBGOUT(CS_IOCTL, 4, printk("SOUND_MIXER_PRIVATE1:\n"));
		break;
	case SOUND_MIXER_PRIVATE2:
		CS_DBGOUT(CS_IOCTL, 4, printk("SOUND_MIXER_PRIVATE2:\n"));
		break;
	case SOUND_MIXER_PRIVATE3:
		CS_DBGOUT(CS_IOCTL, 4, printk("SOUND_MIXER_PRIVATE3:\n"));
		break;
	case SOUND_MIXER_PRIVATE4:
		CS_DBGOUT(CS_IOCTL, 4, printk("SOUND_MIXER_PRIVATE4:\n"));
		break;
	case SOUND_MIXER_PRIVATE5:
		CS_DBGOUT(CS_IOCTL, 4, printk("SOUND_MIXER_PRIVATE5:\n"));
		break;
	case SOUND_MIXER_INFO:
		CS_DBGOUT(CS_IOCTL, 4, printk("SOUND_MIXER_INFO:\n"));
		break;
	case SOUND_OLD_MIXER_INFO:
		CS_DBGOUT(CS_IOCTL, 4, printk("SOUND_OLD_MIXER_INFO:\n"));
		break;

	default:
		switch (_IOC_NR(x)) {
		case SOUND_MIXER_VOLUME:
			CS_DBGOUT(CS_IOCTL, 4,
				  printk("SOUND_MIXER_VOLUME:\n"));
			break;
		case SOUND_MIXER_SPEAKER:
			CS_DBGOUT(CS_IOCTL, 4,
				  printk("SOUND_MIXER_SPEAKER:\n"));
			break;
		case SOUND_MIXER_RECLEV:
			CS_DBGOUT(CS_IOCTL, 4,
				  printk("SOUND_MIXER_RECLEV:\n"));
			break;
		case SOUND_MIXER_MIC:
			CS_DBGOUT(CS_IOCTL, 4,
				  printk("SOUND_MIXER_MIC:\n"));
			break;
		case SOUND_MIXER_SYNTH:
			CS_DBGOUT(CS_IOCTL, 4,
				  printk("SOUND_MIXER_SYNTH:\n"));
			break;
		case SOUND_MIXER_RECSRC:
			CS_DBGOUT(CS_IOCTL, 4,
				  printk("SOUND_MIXER_RECSRC:\n"));
			break;
		case SOUND_MIXER_DEVMASK:
			CS_DBGOUT(CS_IOCTL, 4,
				  printk("SOUND_MIXER_DEVMASK:\n"));
			break;
		case SOUND_MIXER_RECMASK:
			CS_DBGOUT(CS_IOCTL, 4,
				  printk("SOUND_MIXER_RECMASK:\n"));
			break;
		case SOUND_MIXER_STEREODEVS:
			CS_DBGOUT(CS_IOCTL, 4,
				  printk("SOUND_MIXER_STEREODEVS:\n"));
			break;
		case SOUND_MIXER_CAPS:
			CS_DBGOUT(CS_IOCTL, 4, printk("SOUND_MIXER_CAPS:\n"));
			break;
		default:
			i = _IOC_NR(x);
			if (i >= SOUND_MIXER_NRDEVICES
			    || !(vidx = mixtable1[i])) {
				CS_DBGOUT(CS_IOCTL, 4, printk
					("UNKNOWN IOCTL: 0x%.8x NR=%d\n",
						x, i));
			} else {
				CS_DBGOUT(CS_IOCTL, 4, printk
					("SOUND_MIXER_IOCTL AC9x: 0x%.8x NR=%d\n",
						x, i));
			}
			break;
		}
	}
}
#endif
static int prog_dmabuf_adc(struct cs4281_state *s);
static void prog_codec(struct cs4281_state *s, unsigned type);

// --------------------------------------------------------------------- 
//
//              Hardware Interfaces For the CS4281
//


//******************************************************************************
// "delayus()-- Delay for the specified # of microseconds.
//******************************************************************************
static void delayus(struct cs4281_state *s, u32 delay)
{
	u32 j;
	if ((delay > 9999) && (s->pm.flags & CS4281_PM_IDLE)) {
		j = (delay * HZ) / 1000000;	/* calculate delay in jiffies  */
		if (j < 1)
			j = 1;	/* minimum one jiffy. */
		current->state = TASK_UNINTERRUPTIBLE;
		schedule_timeout(j);
	} else
		udelay(delay);
	return;
}


//******************************************************************************
// "cs4281_read_ac97" -- Reads a word from the specified location in the
//               CS4281's address space(based on the BA0 register).
//
// 1. Write ACCAD = Command Address Register = 46Ch for AC97 register address
// 2. Write ACCDA = Command Data Register = 470h for data to write to AC97 register,
//                                            0h for reads.
// 3. Write ACCTL = Control Register = 460h for initiating the write
// 4. Read ACCTL = 460h, DCV should be reset by now and 460h = 17h
// 5. if DCV not cleared, break and return error
// 6. Read ACSTS = Status Register = 464h, check VSTS bit
//****************************************************************************
static int cs4281_read_ac97(struct cs4281_state *card, u32 offset,
			    u32 * value)
{
	u32 count, status;

	// Make sure that there is not data sitting
	// around from a previous uncompleted access.
	// ACSDA = Status Data Register = 47Ch
	status = readl(card->pBA0 + BA0_ACSDA);

	// Setup the AC97 control registers on the CS4281 to send the
	// appropriate command to the AC97 to perform the read.
	// ACCAD = Command Address Register = 46Ch
	// ACCDA = Command Data Register = 470h
	// ACCTL = Control Register = 460h
	// bit DCV - will clear when process completed
	// bit CRW - Read command
	// bit VFRM - valid frame enabled
	// bit ESYN - ASYNC generation enabled

	// Get the actual AC97 register from the offset
	writel(offset - BA0_AC97_RESET, card->pBA0 + BA0_ACCAD);
	writel(0, card->pBA0 + BA0_ACCDA);
	writel(ACCTL_DCV | ACCTL_CRW | ACCTL_VFRM | ACCTL_ESYN,
	       card->pBA0 + BA0_ACCTL);

	// Wait for the read to occur.
	for (count = 0; count < 10; count++) {
		// First, we want to wait for a short time.
		udelay(25);

		// Now, check to see if the read has completed.
		// ACCTL = 460h, DCV should be reset by now and 460h = 17h
		if (!(readl(card->pBA0 + BA0_ACCTL) & ACCTL_DCV))
			break;
	}

	// Make sure the read completed.
	if (readl(card->pBA0 + BA0_ACCTL) & ACCTL_DCV)
		return 1;

	// Wait for the valid status bit to go active.
	for (count = 0; count < 10; count++) {
		// Read the AC97 status register.
		// ACSTS = Status Register = 464h
		status = readl(card->pBA0 + BA0_ACSTS);

		// See if we have valid status.
		// VSTS - Valid Status
		if (status & ACSTS_VSTS)
			break;
		// Wait for a short while.
		udelay(25);
	}

	// Make sure we got valid status.
	if (!(status & ACSTS_VSTS))
		return 1;

	// Read the data returned from the AC97 register.
	// ACSDA = Status Data Register = 474h
	*value = readl(card->pBA0 + BA0_ACSDA);

	// Success.
	return (0);
}


//****************************************************************************
//
// "cs4281_write_ac97()"-- writes a word to the specified location in the
// CS461x's address space (based on the part's base address zero register).
//
// 1. Write ACCAD = Command Address Register = 46Ch for AC97 register address
// 2. Write ACCDA = Command Data Register = 470h for data to write to AC97 reg.
// 3. Write ACCTL = Control Register = 460h for initiating the write
// 4. Read ACCTL = 460h, DCV should be reset by now and 460h = 07h
// 5. if DCV not cleared, break and return error
//
//****************************************************************************
static int cs4281_write_ac97(struct cs4281_state *card, u32 offset,
			     u32 value)
{
	u32 count, status=0;

	CS_DBGOUT(CS_FUNCTION, 2,
		  printk(KERN_INFO "cs4281: cs_4281_write_ac97()+ \n"));

	// Setup the AC97 control registers on the CS4281 to send the
	// appropriate command to the AC97 to perform the read.
	// ACCAD = Command Address Register = 46Ch
	// ACCDA = Command Data Register = 470h
	// ACCTL = Control Register = 460h
	// set DCV - will clear when process completed
	// reset CRW - Write command
	// set VFRM - valid frame enabled
	// set ESYN - ASYNC generation enabled
	// set RSTN - ARST# inactive, AC97 codec not reset

	// Get the actual AC97 register from the offset

	writel(offset - BA0_AC97_RESET, card->pBA0 + BA0_ACCAD);
	writel(value, card->pBA0 + BA0_ACCDA);
	writel(ACCTL_DCV | ACCTL_VFRM | ACCTL_ESYN,
	       card->pBA0 + BA0_ACCTL);

	// Wait for the write to occur.
	for (count = 0; count < 100; count++) {
		// First, we want to wait for a short time.
		udelay(25);
		// Now, check to see if the write has completed.
		// ACCTL = 460h, DCV should be reset by now and 460h = 07h
		status = readl(card->pBA0 + BA0_ACCTL);
		if (!(status & ACCTL_DCV))
			break;
	}

	// Make sure the write completed.
	if (status & ACCTL_DCV) {
		CS_DBGOUT(CS_ERROR, 1, printk(KERN_INFO
	      		"cs4281: cs_4281_write_ac97()- unable to write. ACCTL_DCV active\n"));
		return 1;
	}
	CS_DBGOUT(CS_FUNCTION, 2,
		  printk(KERN_INFO "cs4281: cs_4281_write_ac97()- 0\n"));
	// Success.
	return 0;
}


//******************************************************************************
// "Init4281()" -- Bring up the part.
//******************************************************************************
static __devinit int cs4281_hw_init(struct cs4281_state *card)
{
	u32 ac97_slotid;
	u32 temp1, temp2;

	CS_DBGOUT(CS_FUNCTION, 2,
		  printk(KERN_INFO "cs4281: cs4281_hw_init()+ \n"));
#ifndef NOT_CS4281_PM
	if(!card)
		return 1;
#endif
	temp2 = readl(card->pBA0 + BA0_CFLR);
	CS_DBGOUT(CS_INIT | CS_ERROR | CS_PARMS, 4, printk(KERN_INFO 
		"cs4281: cs4281_hw_init() CFLR 0x%x\n", temp2));
	if(temp2 != CS4281_CFLR_DEFAULT)
	{
		CS_DBGOUT(CS_INIT | CS_ERROR, 1, printk(KERN_INFO 
			"cs4281: cs4281_hw_init() CFLR invalid - resetting from 0x%x to 0x%x\n",
				temp2,CS4281_CFLR_DEFAULT));
		writel(CS4281_CFLR_DEFAULT, card->pBA0 + BA0_CFLR);
		temp2 = readl(card->pBA0 + BA0_CFLR);
		if(temp2 != CS4281_CFLR_DEFAULT)
		{
			CS_DBGOUT(CS_INIT | CS_ERROR, 1, printk(KERN_INFO 
				"cs4281: cs4281_hw_init() Invalid hardware - unable to configure CFLR\n"));
			return 1;
		}
	}

	//***************************************7
	//  Set up the Sound System Configuration
	//***************************************

	// Set the 'Configuration Write Protect' register
	// to 4281h.  Allows vendor-defined configuration
	// space between 0e4h and 0ffh to be written.

	writel(0x4281, card->pBA0 + BA0_CWPR);	// (3e0h)

	// (0), Blast the clock control register to zero so that the
	// PLL starts out in a known state, and blast the master serial
	// port control register to zero so that the serial ports also
	// start out in a known state.

	writel(0, card->pBA0 + BA0_CLKCR1);	// (400h)
	writel(0, card->pBA0 + BA0_SERMC);	// (420h)


	// (1), Make ESYN go to zero to turn off
	// the Sync pulse on the AC97 link.

	writel(0, card->pBA0 + BA0_ACCTL);
	udelay(50);


	// (2) Drive the ARST# pin low for a minimum of 1uS (as defined in
	// the AC97 spec) and then drive it high.  This is done for non
	// AC97 modes since there might be logic external to the CS461x
	// that uses the ARST# line for a reset.

	writel(0, card->pBA0 + BA0_SPMC);	// (3ech)
	udelay(100);
	writel(SPMC_RSTN, card->pBA0 + BA0_SPMC);
	delayus(card,50000);		// Wait 50 ms for ABITCLK to become stable.

	// (3) Turn on the Sound System Clocks.
	writel(CLKCR1_PLLP, card->pBA0 + BA0_CLKCR1);	// (400h)
	delayus(card,50000);		// Wait for the PLL to stabilize.
	// Turn on clocking of the core (CLKCR1(400h) = 0x00000030)
	writel(CLKCR1_PLLP | CLKCR1_SWCE, card->pBA0 + BA0_CLKCR1);

	// (4) Power on everything for now..
	writel(0x7E, card->pBA0 + BA0_SSPM);	// (740h)

	// (5) Wait for clock stabilization.
	for (temp1 = 0; temp1 < 1000; temp1++) {
		udelay(1000);
		if (readl(card->pBA0 + BA0_CLKCR1) & CLKCR1_DLLRDY)
			break;
	}
	if (!(readl(card->pBA0 + BA0_CLKCR1) & CLKCR1_DLLRDY)) {
		CS_DBGOUT(CS_ERROR, 1, printk(KERN_ERR 
			"cs4281: DLLRDY failed!\n"));
		return -EIO;
	}
	// (6) Enable ASYNC generation.
	writel(ACCTL_ESYN, card->pBA0 + BA0_ACCTL);	// (460h)

	// Now wait 'for a short while' to allow the  AC97
	// part to start generating bit clock. (so we don't
	// Try to start the PLL without an input clock.)
	delayus(card,50000);

	// Set the serial port timing configuration, so that the
	// clock control circuit gets its clock from the right place.
	writel(SERMC_PTC_AC97, card->pBA0 + BA0_SERMC);	// (420h)=2.

	// (7) Wait for the codec ready signal from the AC97 codec.

	for (temp1 = 0; temp1 < 1000; temp1++) {
		// Delay a mil to let things settle out and
		// to prevent retrying the read too quickly.
		udelay(1000);
		if (readl(card->pBA0 + BA0_ACSTS) & ACSTS_CRDY)	// If ready,  (464h)
			break;	//   exit the 'for' loop.
	}
	if (!(readl(card->pBA0 + BA0_ACSTS) & ACSTS_CRDY))	// If never came ready,
	{
		CS_DBGOUT(CS_FUNCTION, 2, printk(KERN_ERR
			 "cs4281: ACSTS never came ready!\n"));
		return -EIO;	//   exit initialization.
	}
	// (8) Assert the 'valid frame' signal so we can
	// begin sending commands to the AC97 codec.
	writel(ACCTL_VFRM | ACCTL_ESYN, card->pBA0 + BA0_ACCTL);	// (460h)

	// (9), Wait until CODEC calibration is finished.
	// Print an error message if it doesn't.
	for (temp1 = 0; temp1 < 1000; temp1++) {
		delayus(card,10000);
		// Read the AC97 Powerdown Control/Status Register.
		cs4281_read_ac97(card, BA0_AC97_POWERDOWN, &temp2);
		if ((temp2 & 0x0000000F) == 0x0000000F)
			break;
	}
	if ((temp2 & 0x0000000F) != 0x0000000F) {
		CS_DBGOUT(CS_FUNCTION, 2, printk(KERN_ERR
			"cs4281: Codec failed to calibrate.  Status = %.8x.\n",
				temp2));
		return -EIO;
	}
	// (10), Set the serial port timing configuration, so that the
	// clock control circuit gets its clock from the right place.
	writel(SERMC_PTC_AC97, card->pBA0 + BA0_SERMC);	// (420h)=2.


	// (11) Wait until we've sampled input slots 3 & 4 as valid, meaning
	// that the codec is pumping ADC data across the AC link.
	for (temp1 = 0; temp1 < 1000; temp1++) {
		// Delay a mil to let things settle out and
		// to prevent retrying the read too quickly.
		delayus(card,1000);	//(test)

		// Read the input slot valid register;  See
		// if input slots 3 and 4 are valid yet.
		if (
		    (readl(card->pBA0 + BA0_ACISV) &
		     (ACISV_ISV3 | ACISV_ISV4)) ==
		    (ACISV_ISV3 | ACISV_ISV4)) break;	// Exit the 'for' if slots are valid.
	}
	// If we never got valid data, exit initialization.
	if ((readl(card->pBA0 + BA0_ACISV) & (ACISV_ISV3 | ACISV_ISV4))
	    != (ACISV_ISV3 | ACISV_ISV4)) {
		CS_DBGOUT(CS_FUNCTION, 2,
			  printk(KERN_ERR
				 "cs4281: Never got valid data!\n"));
		return -EIO;	// If no valid data, exit initialization.
	}
	// (12), Start digital data transfer of audio data to the codec.
	writel(ACOSV_SLV3 | ACOSV_SLV4, card->pBA0 + BA0_ACOSV);	// (468h)


	//**************************************
	// Unmute the Master and Alternate
	// (headphone) volumes.  Set to max.
	//**************************************
	cs4281_write_ac97(card, BA0_AC97_HEADPHONE_VOLUME, 0);
	cs4281_write_ac97(card, BA0_AC97_MASTER_VOLUME, 0);

	//******************************************
	// Power on the DAC(AddDACUser()from main())
	//******************************************
	cs4281_read_ac97(card, BA0_AC97_POWERDOWN, &temp1);
	cs4281_write_ac97(card, BA0_AC97_POWERDOWN, temp1 &= 0xfdff);

	// Wait until we sample a DAC ready state.
	for (temp2 = 0; temp2 < 32; temp2++) {
		// Let's wait a mil to let things settle.
		delayus(card,1000);
		// Read the current state of the power control reg.
		cs4281_read_ac97(card, BA0_AC97_POWERDOWN, &temp1);
		// If the DAC ready state bit is set, stop waiting.
		if (temp1 & 0x2)
			break;
	}

	//******************************************
	// Power on the ADC(AddADCUser()from main())
	//******************************************
	cs4281_read_ac97(card, BA0_AC97_POWERDOWN, &temp1);
	cs4281_write_ac97(card, BA0_AC97_POWERDOWN, temp1 &= 0xfeff);

	// Wait until we sample ADC ready state.
	for (temp2 = 0; temp2 < 32; temp2++) {
		// Let's wait a mil to let things settle.
		delayus(card,1000);
		// Read the current state of the power control reg.
		cs4281_read_ac97(card, BA0_AC97_POWERDOWN, &temp1);
		// If the ADC ready state bit is set, stop waiting.
		if (temp1 & 0x1)
			break;
	}
	// Set up 4281 Register contents that
	// don't change for boot duration.

	// For playback, we map AC97 slot 3 and 4(Left
	// & Right PCM playback) to DMA Channel 0.
	// Set the fifo to be 15 bytes at offset zero.

	ac97_slotid = 0x01000f00;	// FCR0.RS[4:0]=1(=>slot4, right PCM playback).
	// FCR0.LS[4:0]=0(=>slot3, left PCM playback).
	// FCR0.SZ[6-0]=15; FCR0.OF[6-0]=0.
	writel(ac97_slotid, card->pBA0 + BA0_FCR0);	// (180h)
	writel(ac97_slotid | FCRn_FEN, card->pBA0 + BA0_FCR0);	// Turn on FIFO Enable.

	// For capture, we map AC97 slot 10 and 11(Left
	// and Right PCM Record) to DMA Channel 1.
	// Set the fifo to be 15 bytes at offset sixteen.
	ac97_slotid = 0x0B0A0f10;	// FCR1.RS[4:0]=11(=>slot11, right PCM record).
	// FCR1.LS[4:0]=10(=>slot10, left PCM record).
	// FCR1.SZ[6-0]=15; FCR1.OF[6-0]=16.
	writel(ac97_slotid | FCRn_PSH, card->pBA0 + BA0_FCR1);	// (184h)
	writel(ac97_slotid | FCRn_FEN, card->pBA0 + BA0_FCR1);	// Turn on FIFO Enable.

	// Map the Playback SRC to the same AC97 slots(3 & 4--
	// --Playback left & right)as DMA channel 0.
	// Map the record SRC to the same AC97 slots(10 & 11--
	// -- Record left & right) as DMA channel 1.

	ac97_slotid = 0x0b0a0100;	// SCRSA.PRSS[4:0]=1(=>slot4, right PCM playback).
	// SCRSA.PLSS[4:0]=0(=>slot3, left PCM playback).
	// SCRSA.CRSS[4:0]=11(=>slot11, right PCM record)
	// SCRSA.CLSS[4:0]=10(=>slot10, left PCM record).
	writel(ac97_slotid, card->pBA0 + BA0_SRCSA);	// (75ch)

	// Set 'Half Terminal Count Interrupt Enable' and 'Terminal
	// Count Interrupt Enable' in DMA Control Registers 0 & 1.
	// Set 'MSK' flag to 1 to keep the DMA engines paused.
	temp1 = (DCRn_HTCIE | DCRn_TCIE | DCRn_MSK);	// (00030001h)
	writel(temp1, card->pBA0 + BA0_DCR0);	// (154h
	writel(temp1, card->pBA0 + BA0_DCR1);	// (15ch)

	// Set 'Auto-Initialize Control' to 'enabled'; For playback,
	// set 'Transfer Type Control'(TR[1:0]) to 'read transfer',
	// for record, set Transfer Type Control to 'write transfer'.
	// All other bits set to zero;  Some will be changed @ transfer start.
	temp1 = (DMRn_DMA | DMRn_AUTO | DMRn_TR_READ);	// (20000018h)
	writel(temp1, card->pBA0 + BA0_DMR0);	// (150h)
	temp1 = (DMRn_DMA | DMRn_AUTO | DMRn_TR_WRITE);	// (20000014h)
	writel(temp1, card->pBA0 + BA0_DMR1);	// (158h)

	// Enable DMA interrupts generally, and
	// DMA0 & DMA1 interrupts specifically.
	temp1 = readl(card->pBA0 + BA0_HIMR) & 0xfffbfcff;
	writel(temp1, card->pBA0 + BA0_HIMR);

	CS_DBGOUT(CS_FUNCTION, 2,
		  printk(KERN_INFO "cs4281: cs4281_hw_init()- 0\n"));
	return 0;
}

#ifndef NOT_CS4281_PM
static void printpm(struct cs4281_state *s)
{
	CS_DBGOUT(CS_PM, 9, printk("pm struct:\n"));
	CS_DBGOUT(CS_PM, 9, printk("flags:0x%x u32CLKCR1_SAVE: 0%x u32SSPMValue: 0x%x\n",
		(unsigned)s->pm.flags,s->pm.u32CLKCR1_SAVE,s->pm.u32SSPMValue));
	CS_DBGOUT(CS_PM, 9, printk("u32PPLVCvalue: 0x%x u32PPRVCvalue: 0x%x\n",
		s->pm.u32PPLVCvalue,s->pm.u32PPRVCvalue));
	CS_DBGOUT(CS_PM, 9, printk("u32FMLVCvalue: 0x%x u32FMRVCvalue: 0x%x\n",
		s->pm.u32FMLVCvalue,s->pm.u32FMRVCvalue));
	CS_DBGOUT(CS_PM, 9, printk("u32GPIORvalue: 0x%x u32JSCTLvalue: 0x%x\n",
		s->pm.u32GPIORvalue,s->pm.u32JSCTLvalue));
	CS_DBGOUT(CS_PM, 9, printk("u32SSCR: 0x%x u32SRCSA: 0x%x\n",
		s->pm.u32SSCR,s->pm.u32SRCSA));
	CS_DBGOUT(CS_PM, 9, printk("u32DacASR: 0x%x u32AdcASR: 0x%x\n",
		s->pm.u32DacASR,s->pm.u32AdcASR));
	CS_DBGOUT(CS_PM, 9, printk("u32DacSR: 0x%x u32AdcSR: 0x%x\n",
		s->pm.u32DacSR,s->pm.u32AdcSR));
	CS_DBGOUT(CS_PM, 9, printk("u32MIDCR_Save: 0x%x\n",
		s->pm.u32MIDCR_Save));

}
static void printpipe(struct cs4281_pipeline *pl)
{

	CS_DBGOUT(CS_PM, 9, printk("pm struct:\n"));
	CS_DBGOUT(CS_PM, 9, printk("flags:0x%x number: 0%x\n",
		(unsigned)pl->flags,pl->number));
	CS_DBGOUT(CS_PM, 9, printk("u32DBAnValue: 0%x u32DBCnValue: 0x%x\n",
		pl->u32DBAnValue,pl->u32DBCnValue));
	CS_DBGOUT(CS_PM, 9, printk("u32DMRnValue: 0x%x u32DCRnValue: 0x%x\n",
		pl->u32DMRnValue,pl->u32DCRnValue));
	CS_DBGOUT(CS_PM, 9, printk("u32DBAnAddress: 0x%x u32DBCnAddress: 0x%x\n",
		pl->u32DBAnAddress,pl->u32DBCnAddress));
	CS_DBGOUT(CS_PM, 9, printk("u32DCAnAddress: 0x%x u32DCCnAddress: 0x%x\n",
		pl->u32DCCnAddress,pl->u32DCCnAddress));
	CS_DBGOUT(CS_PM, 9, printk("u32DMRnAddress: 0x%x u32DCRnAddress: 0x%x\n",
		pl->u32DMRnAddress,pl->u32DCRnAddress));
	CS_DBGOUT(CS_PM, 9, printk("u32HDSRnAddress: 0x%x u32DBAn_Save: 0x%x\n",
		pl->u32HDSRnAddress,pl->u32DBAn_Save));
	CS_DBGOUT(CS_PM, 9, printk("u32DBCn_Save: 0x%x u32DMRn_Save: 0x%x\n",
		pl->u32DBCn_Save,pl->u32DMRn_Save));
	CS_DBGOUT(CS_PM, 9, printk("u32DCRn_Save: 0x%x u32DCCn_Save: 0x%x\n",
		pl->u32DCRn_Save,pl->u32DCCn_Save));
	CS_DBGOUT(CS_PM, 9, printk("u32DCAn_Save: 0x%x\n",
		pl->u32DCAn_Save));
	CS_DBGOUT(CS_PM, 9, printk("u32FCRn_Save: 0x%x u32FSICn_Save: 0x%x\n",
		pl->u32FCRn_Save,pl->u32FSICn_Save));
	CS_DBGOUT(CS_PM, 9, printk("u32FCRnValue: 0x%x u32FSICnValue: 0x%x\n",
		pl->u32FCRnValue,pl->u32FSICnValue));
	CS_DBGOUT(CS_PM, 9, printk("u32FCRnAddress: 0x%x u32FSICnAddress: 0x%x\n",
		pl->u32FCRnAddress,pl->u32FSICnAddress));
	CS_DBGOUT(CS_PM, 9, printk("u32FPDRnValue: 0x%x u32FPDRnAddress: 0x%x\n",
		pl->u32FPDRnValue,pl->u32FPDRnAddress));
}
static void printpipelines(struct cs4281_state *s)
{
	int i;
	for(i=0;i<CS4281_NUMBER_OF_PIPELINES;i++)
	{
		if(s->pl[i].flags & CS4281_PIPELINE_VALID)
		{
			printpipe(&s->pl[i]);
		}
	}
}
/****************************************************************************
*
*  Suspend - save the ac97 regs, mute the outputs and power down the part.  
*
****************************************************************************/
static void cs4281_ac97_suspend(struct cs4281_state *s)
{
	int Count,i;

	CS_DBGOUT(CS_PM, 9, printk("cs4281: cs4281_ac97_suspend()+\n"));
/*
* change the state, save the current hwptr, then stop the dac/adc
*/
	s->pm.flags &= ~CS4281_PM_IDLE;
	s->pm.flags |= CS4281_PM_SUSPENDING;
	s->pm.u32hwptr_playback = readl(s->pBA0 + BA0_DCA0);
	s->pm.u32hwptr_capture = readl(s->pBA0 + BA0_DCA1);
	stop_dac(s);
	stop_adc(s);

	for(Count = 0x2, i=0; (Count <= CS4281_AC97_HIGHESTREGTORESTORE)
			&& (i < CS4281_AC97_NUMBER_RESTORE_REGS); 
		Count += 2, i++)
	{
		cs4281_read_ac97(s, BA0_AC97_RESET + Count, &s->pm.ac97[i]);
	}
/*
* Save the ac97 volume registers as well as the current powerdown state.
* Now, mute the all the outputs (master, headphone, and mono), as well
* as the PCM volume, in preparation for powering down the entire part.
*/ 
	cs4281_read_ac97(s, BA0_AC97_MASTER_VOLUME, &s->pm.u32AC97_master_volume);
	cs4281_read_ac97(s, BA0_AC97_HEADPHONE_VOLUME, &s->pm.u32AC97_headphone_volume);
	cs4281_read_ac97(s, BA0_AC97_MASTER_VOLUME_MONO, &s->pm.u32AC97_master_volume_mono);
	cs4281_read_ac97(s, BA0_AC97_PCM_OUT_VOLUME, &s->pm.u32AC97_pcm_out_volume);
		
	cs4281_write_ac97(s, BA0_AC97_MASTER_VOLUME, 0x8000);
	cs4281_write_ac97(s, BA0_AC97_HEADPHONE_VOLUME, 0x8000);
	cs4281_write_ac97(s, BA0_AC97_MASTER_VOLUME_MONO, 0x8000);
	cs4281_write_ac97(s, BA0_AC97_PCM_OUT_VOLUME, 0x8000);

	cs4281_read_ac97(s, BA0_AC97_POWERDOWN, &s->pm.u32AC97_powerdown);
	cs4281_read_ac97(s, BA0_AC97_GENERAL_PURPOSE, &s->pm.u32AC97_general_purpose);

/*
* And power down everything on the AC97 codec.
*/
	cs4281_write_ac97(s, BA0_AC97_POWERDOWN, 0xff00);
	CS_DBGOUT(CS_PM, 9, printk("cs4281: cs4281_ac97_suspend()-\n"));
}

/****************************************************************************
*
*  Resume - power up the part and restore its registers..  
*
****************************************************************************/
static void cs4281_ac97_resume(struct cs4281_state *s)
{
	int Count,i;

	CS_DBGOUT(CS_PM, 9, printk("cs4281: cs4281_ac97_resume()+\n"));

/* do not save the power state registers at this time
    //
    // If we saved away the power control registers, write them into the
    // shadows so those saved values get restored instead of the current
    // shadowed value.
    //
    if( bPowerStateSaved )
    {
        PokeShadow( 0x26, ulSaveReg0x26 );
        bPowerStateSaved = FALSE;
    }
*/

//
// First, we restore the state of the general purpose register.  This
// contains the mic select (mic1 or mic2) and if we restore this after
// we restore the mic volume/boost state and mic2 was selected at
// suspend time, we will end up with a brief period of time where mic1
// is selected with the volume/boost settings for mic2, causing
// acoustic feedback.  So we restore the general purpose register
// first, thereby getting the correct mic selected before we restore
// the mic volume/boost.
//
	cs4281_write_ac97(s, BA0_AC97_GENERAL_PURPOSE, s->pm.u32AC97_general_purpose);

//
// Now, while the outputs are still muted, restore the state of power
// on the AC97 part.
//
	cs4281_write_ac97(s, BA0_AC97_POWERDOWN, s->pm.u32AC97_powerdown);

/*
* Restore just the first set of registers, from register number
* 0x02 to the register number that ulHighestRegToRestore specifies.
*/
	for(	Count = 0x2, i=0; 
		(Count <= CS4281_AC97_HIGHESTREGTORESTORE)
			&& (i < CS4281_AC97_NUMBER_RESTORE_REGS); 
		Count += 2, i++)
	{
		cs4281_write_ac97(s, BA0_AC97_RESET + Count, s->pm.ac97[i]);
	}
	CS_DBGOUT(CS_PM, 9, printk("cs4281: cs4281_ac97_resume()-\n"));
}

/* do not save the power state registers at this time
****************************************************************************
*
*  SavePowerState - Save the power registers away. 
*
****************************************************************************
void 
HWAC97codec::SavePowerState(void)
{
    ENTRY(TM_OBJECTCALLS, "HWAC97codec::SavePowerState()\r\n");

    ulSaveReg0x26 = PeekShadow(0x26);

    //
    // Note that we have saved registers that need to be restored during a
    // resume instead of ulAC97Regs[].
    //
    bPowerStateSaved = TRUE;

} // SavePowerState
*/

static void cs4281_SuspendFIFO(struct cs4281_state *s, struct cs4281_pipeline *pl)
{
 /*
 * We need to save the contents of the BASIC FIFO Registers.
 */
	pl->u32FCRn_Save = readl(s->pBA0 + pl->u32FCRnAddress);
	pl->u32FSICn_Save = readl(s->pBA0 + pl->u32FSICnAddress);
}
static void cs4281_ResumeFIFO(struct cs4281_state *s, struct cs4281_pipeline *pl)
{
 /*
 * We need to restore the contents of the BASIC FIFO Registers.
 */
	writel(pl->u32FCRn_Save,s->pBA0 + pl->u32FCRnAddress);
	writel(pl->u32FSICn_Save,s->pBA0 + pl->u32FSICnAddress);
}
static void cs4281_SuspendDMAengine(struct cs4281_state *s, struct cs4281_pipeline *pl)
{
	//
	// We need to save the contents of the BASIC DMA Registers.
	//
	pl->u32DBAn_Save = readl(s->pBA0 + pl->u32DBAnAddress);
	pl->u32DBCn_Save = readl(s->pBA0 + pl->u32DBCnAddress);
	pl->u32DMRn_Save = readl(s->pBA0 + pl->u32DMRnAddress);
	pl->u32DCRn_Save = readl(s->pBA0 + pl->u32DCRnAddress);
	pl->u32DCCn_Save = readl(s->pBA0 + pl->u32DCCnAddress);
	pl->u32DCAn_Save = readl(s->pBA0 + pl->u32DCAnAddress);
}
static void cs4281_ResumeDMAengine(struct cs4281_state *s, struct cs4281_pipeline *pl)
{
	//
	// We need to save the contents of the BASIC DMA Registers.
	//
	writel( pl->u32DBAn_Save, s->pBA0 + pl->u32DBAnAddress);
	writel( pl->u32DBCn_Save, s->pBA0 + pl->u32DBCnAddress);
	writel( pl->u32DMRn_Save, s->pBA0 + pl->u32DMRnAddress);
	writel( pl->u32DCRn_Save, s->pBA0 + pl->u32DCRnAddress);
	writel( pl->u32DCCn_Save, s->pBA0 + pl->u32DCCnAddress);
	writel( pl->u32DCAn_Save, s->pBA0 + pl->u32DCAnAddress);
}

static int cs4281_suspend(struct cs4281_state *s)
{
	int i;
	u32 u32CLKCR1;
	struct cs4281_pm *pm = &s->pm;
	CS_DBGOUT(CS_PM | CS_FUNCTION, 9, 
		printk("cs4281: cs4281_suspend()+ flags=%d\n",
			(unsigned)s->pm.flags));
/*
* check the current state, only suspend if IDLE
*/
	if(!(s->pm.flags & CS4281_PM_IDLE))
	{
		CS_DBGOUT(CS_PM | CS_ERROR, 2, 
			printk("cs4281: cs4281_suspend() unable to suspend, not IDLE\n"));
		return 1;
	}
	s->pm.flags &= ~CS4281_PM_IDLE;
	s->pm.flags |= CS4281_PM_SUSPENDING;

//
// Gershwin CLKRUN - Set CKRA
//
	u32CLKCR1 = readl(s->pBA0 + BA0_CLKCR1);

	pm->u32CLKCR1_SAVE = u32CLKCR1;
	if(!(u32CLKCR1 & 0x00010000 ) )
		writel(u32CLKCR1 | 0x00010000, s->pBA0 + BA0_CLKCR1);

//
// First, turn on the clocks (yikes) to the devices, so that they will
// respond when we try to save their state.
//
	if(!(u32CLKCR1 & CLKCR1_SWCE))
	{
		writel(u32CLKCR1 | CLKCR1_SWCE , s->pBA0 + BA0_CLKCR1);
	}
    
	//
	// Save the power state
	//
	pm->u32SSPMValue = readl(s->pBA0 + BA0_SSPM);

	//
	// Disable interrupts.
	//
	writel(HICR_CHGM, s->pBA0 + BA0_HICR);

	//
	// Save the PCM Playback Left and Right Volume Control.
	//
	pm->u32PPLVCvalue = readl(s->pBA0 + BA0_PPLVC);
	pm->u32PPRVCvalue = readl(s->pBA0 + BA0_PPRVC);

	//
	// Save the FM Synthesis Left and Right Volume Control.
	//
	pm->u32FMLVCvalue = readl(s->pBA0 + BA0_FMLVC);
	pm->u32FMRVCvalue = readl(s->pBA0 + BA0_FMRVC);

	//
	// Save the GPIOR value.
	//
	pm->u32GPIORvalue = readl(s->pBA0 + BA0_GPIOR);

	//
	// Save the JSCTL value.
	//
	pm->u32JSCTLvalue = readl(s->pBA0 + BA0_GPIOR);

	//
	// Save Sound System Control Register
	//
	pm->u32SSCR = readl(s->pBA0 + BA0_SSCR);

	//
	// Save SRC Slot Assinment register
	//
	pm->u32SRCSA = readl(s->pBA0 + BA0_SRCSA);

	//
	// Save sample rate
	//
	pm->u32DacASR = readl(s->pBA0 + BA0_PASR);
	pm->u32AdcASR = readl(s->pBA0 + BA0_CASR);
	pm->u32DacSR = readl(s->pBA0 + BA0_DACSR);
	pm->u32AdcSR = readl(s->pBA0 + BA0_ADCSR);

	//
	// Loop through all of the PipeLines 
	//
	for(i = 0; i < CS4281_NUMBER_OF_PIPELINES; i++)
        {
		if(s->pl[i].flags & CS4281_PIPELINE_VALID)
		{
		//
		// Ask the DMAengines and FIFOs to Suspend.
		//
			cs4281_SuspendDMAengine(s,&s->pl[i]);
			cs4281_SuspendFIFO(s,&s->pl[i]);
		}
	}
	//
	// We need to save the contents of the Midi Control Register.
	//
	pm->u32MIDCR_Save = readl(s->pBA0 + BA0_MIDCR);
/*
* save off the AC97 part information
*/
	cs4281_ac97_suspend(s);
    
	//
	// Turn off the serial ports.
	//
	writel(0, s->pBA0 + BA0_SERMC);

	//
	// Power off FM, Joystick, AC link, 
	//
	writel(0, s->pBA0 + BA0_SSPM);

	//
	// DLL off.
	//
	writel(0, s->pBA0 + BA0_CLKCR1);

	//
	// AC link off.
	//
	writel(0, s->pBA0 + BA0_SPMC);

	//
	// Put the chip into D3(hot) state.
	//
	// PokeBA0(BA0_PMCS, 0x00000003);

	//
	// Gershwin CLKRUN - Clear CKRA
	//
	u32CLKCR1 = readl(s->pBA0 + BA0_CLKCR1);
	writel(u32CLKCR1 & 0xFFFEFFFF, s->pBA0 + BA0_CLKCR1);

#ifdef CSDEBUG
	printpm(s);
	printpipelines(s);
#endif

	s->pm.flags &= ~CS4281_PM_SUSPENDING;
	s->pm.flags |= CS4281_PM_SUSPENDED;

	CS_DBGOUT(CS_PM | CS_FUNCTION, 9, 
		printk("cs4281: cs4281_suspend()- flags=%d\n",
			(unsigned)s->pm.flags));
	return 0;
}

static int cs4281_resume(struct cs4281_state *s)
{
	int i;
	unsigned temp1;
	u32 u32CLKCR1;
	struct cs4281_pm *pm = &s->pm;
	CS_DBGOUT(CS_PM | CS_FUNCTION, 4, 
		printk( "cs4281: cs4281_resume()+ flags=%d\n",
			(unsigned)s->pm.flags));
	if(!(s->pm.flags & CS4281_PM_SUSPENDED))
	{
		CS_DBGOUT(CS_PM | CS_ERROR, 2, 
			printk("cs4281: cs4281_resume() unable to resume, not SUSPENDED\n"));
		return 1;
	}
	s->pm.flags &= ~CS4281_PM_SUSPENDED;
	s->pm.flags |= CS4281_PM_RESUMING;

//
// Gershwin CLKRUN - Set CKRA
//
	u32CLKCR1 = readl(s->pBA0 + BA0_CLKCR1);
	writel(u32CLKCR1 | 0x00010000, s->pBA0 + BA0_CLKCR1);

	//
	// set the power state.
	//
	//old PokeBA0(BA0_PMCS, 0);

	//
	// Program the clock circuit and serial ports.
	//
	temp1 = cs4281_hw_init(s);
	if (temp1) {
		CS_DBGOUT(CS_ERROR | CS_INIT, 1,
		    printk(KERN_ERR
			"cs4281: resume cs4281_hw_init() error.\n"));
		return -1;
	}

	//
	// restore the Power state
	//
	writel(pm->u32SSPMValue, s->pBA0 + BA0_SSPM);

	//
	// Set post SRC mix setting (FM or ALT48K)
	//
	writel(pm->u32SSPM_BITS, s->pBA0 + BA0_SSPM);

	//
	// Loop through all of the PipeLines 
	//
	for(i = 0; i < CS4281_NUMBER_OF_PIPELINES; i++)
        {
		if(s->pl[i].flags & CS4281_PIPELINE_VALID)
		{
		//
		// Ask the DMAengines and FIFOs to Resume.
		//
			cs4281_ResumeDMAengine(s,&s->pl[i]);
			cs4281_ResumeFIFO(s,&s->pl[i]);
		}
	}
	//
	// We need to restore the contents of the Midi Control Register.
	//
	writel(pm->u32MIDCR_Save, s->pBA0 + BA0_MIDCR);

	cs4281_ac97_resume(s);
	//
	// Restore the PCM Playback Left and Right Volume Control.
	//
	writel(pm->u32PPLVCvalue, s->pBA0 + BA0_PPLVC);
	writel(pm->u32PPRVCvalue, s->pBA0 + BA0_PPRVC);

	//
	// Restore the FM Synthesis Left and Right Volume Control.
	//
	writel(pm->u32FMLVCvalue, s->pBA0 + BA0_FMLVC);
	writel(pm->u32FMRVCvalue, s->pBA0 + BA0_FMRVC);

	//
	// Restore the JSCTL value.
	//
	writel(pm->u32JSCTLvalue, s->pBA0 + BA0_JSCTL);

	//
	// Restore the GPIOR register value.
	//
	writel(pm->u32GPIORvalue, s->pBA0 + BA0_GPIOR);

	//
	// Restore Sound System Control Register
	//
	writel(pm->u32SSCR, s->pBA0 + BA0_SSCR);

	//
	// Restore SRC Slot Assignment register
	//
	writel(pm->u32SRCSA, s->pBA0 + BA0_SRCSA);

	//
	// Restore sample rate
	//
	writel(pm->u32DacASR, s->pBA0 + BA0_PASR);
	writel(pm->u32AdcASR, s->pBA0 + BA0_CASR);
	writel(pm->u32DacSR, s->pBA0 + BA0_DACSR);
	writel(pm->u32AdcSR, s->pBA0 + BA0_ADCSR);

	// 
	// Restore CFL1/2 registers we saved to compensate for OEM bugs.
	//
	//	PokeBA0(BA0_CFLR, ulConfig);

	//
	// Gershwin CLKRUN - Clear CKRA
	//
	writel(pm->u32CLKCR1_SAVE, s->pBA0 + BA0_CLKCR1);

	//
	// Enable interrupts on the part.
	//
	writel(HICR_IEV | HICR_CHGM, s->pBA0 + BA0_HICR);

#ifdef CSDEBUG
	printpm(s);
	printpipelines(s);
#endif
/*
* change the state, restore the current hwptrs, then stop the dac/adc
*/
	s->pm.flags |= CS4281_PM_IDLE;
	s->pm.flags &= ~(CS4281_PM_SUSPENDING | CS4281_PM_SUSPENDED 
			| CS4281_PM_RESUMING | CS4281_PM_RESUMED);

	writel(s->pm.u32hwptr_playback, s->pBA0 + BA0_DCA0);
	writel(s->pm.u32hwptr_capture, s->pBA0 + BA0_DCA1);
	start_dac(s);
	start_adc(s);

	CS_DBGOUT(CS_PM | CS_FUNCTION, 9, printk("cs4281: cs4281_resume()- flags=%d\n",
		(unsigned)s->pm.flags));
	return 0;
}

#endif

//******************************************************************************
// "cs4281_play_rate()" --
//******************************************************************************
static void cs4281_play_rate(struct cs4281_state *card, u32 playrate)
{
	u32 DACSRvalue = 1;

	// Based on the sample rate, program the DACSR register.
	if (playrate == 8000)
		DACSRvalue = 5;
	if (playrate == 11025)
		DACSRvalue = 4;
	else if (playrate == 22050)
		DACSRvalue = 2;
	else if (playrate == 44100)
		DACSRvalue = 1;
	else if ((playrate <= 48000) && (playrate >= 6023))
		DACSRvalue = 24576000 / (playrate * 16);
	else if (playrate < 6023)
		// Not allowed by open.
		return;
	else if (playrate > 48000)
		// Not allowed by open.
		return;
	CS_DBGOUT(CS_WAVE_WRITE | CS_PARMS, 2, printk(KERN_INFO
		"cs4281: cs4281_play_rate(): DACSRvalue=0x%.8x playrate=%d\n",
			DACSRvalue, playrate));
	//  Write the 'sample rate select code'
	//  to the 'DAC Sample Rate' register.
	writel(DACSRvalue, card->pBA0 + BA0_DACSR);	// (744h)
}

//******************************************************************************
// "cs4281_record_rate()" -- Initialize the record sample rate converter.
//******************************************************************************
static void cs4281_record_rate(struct cs4281_state *card, u32 outrate)
{
	u32 ADCSRvalue = 1;

	//
	// Based on the sample rate, program the ADCSR register
	//
	if (outrate == 8000)
		ADCSRvalue = 5;
	if (outrate == 11025)
		ADCSRvalue = 4;
	else if (outrate == 22050)
		ADCSRvalue = 2;
	else if (outrate == 44100)
		ADCSRvalue = 1;
	else if ((outrate <= 48000) && (outrate >= 6023))
		ADCSRvalue = 24576000 / (outrate * 16);
	else if (outrate < 6023) {
		// Not allowed by open.
		return;
	} else if (outrate > 48000) {
		// Not allowed by open.
		return;
	}
	CS_DBGOUT(CS_WAVE_READ | CS_PARMS, 2, printk(KERN_INFO
		"cs4281: cs4281_record_rate(): ADCSRvalue=0x%.8x outrate=%d\n",
			ADCSRvalue, outrate));
	//  Write the 'sample rate select code
	//  to the 'ADC Sample Rate' register.
	writel(ADCSRvalue, card->pBA0 + BA0_ADCSR);	// (748h)
}



static void stop_dac(struct cs4281_state *s)
{
	unsigned long flags;
	unsigned temp1;

	CS_DBGOUT(CS_WAVE_WRITE, 3, printk(KERN_INFO "cs4281: stop_dac():\n"));
	spin_lock_irqsave(&s->lock, flags);
	s->ena &= ~FMODE_WRITE;
	temp1 = readl(s->pBA0 + BA0_DCR0) | DCRn_MSK;
	writel(temp1, s->pBA0 + BA0_DCR0);

	spin_unlock_irqrestore(&s->lock, flags);
}


static void start_dac(struct cs4281_state *s)
{
	unsigned long flags;
	unsigned temp1;

	CS_DBGOUT(CS_FUNCTION, 3, printk(KERN_INFO "cs4281: start_dac()+\n"));
	spin_lock_irqsave(&s->lock, flags);
	if (!(s->ena & FMODE_WRITE) && (s->dma_dac.mapped ||
					(s->dma_dac.count > 0
	    				&& s->dma_dac.ready))
#ifndef NOT_CS4281_PM
	&& (s->pm.flags & CS4281_PM_IDLE))
#else
)
#endif
 {
		s->ena |= FMODE_WRITE;
		temp1 = readl(s->pBA0 + BA0_DCR0) & ~DCRn_MSK;	// Clear DMA0 channel mask.
		writel(temp1, s->pBA0 + BA0_DCR0);	// Start DMA'ing.
		writel(HICR_IEV | HICR_CHGM, s->pBA0 + BA0_HICR);	// Enable interrupts.              

		writel(7, s->pBA0 + BA0_PPRVC);
		writel(7, s->pBA0 + BA0_PPLVC);
		CS_DBGOUT(CS_WAVE_WRITE | CS_PARMS, 8, printk(KERN_INFO
			"cs4281: start_dac(): writel 0x%x start dma\n", temp1));

	}
	spin_unlock_irqrestore(&s->lock, flags);
	CS_DBGOUT(CS_FUNCTION, 3,
		  printk(KERN_INFO "cs4281: start_dac()-\n"));
}


static void stop_adc(struct cs4281_state *s)
{
	unsigned long flags;
	unsigned temp1;

	CS_DBGOUT(CS_FUNCTION, 3,
		  printk(KERN_INFO "cs4281: stop_adc()+\n"));

	spin_lock_irqsave(&s->lock, flags);
	s->ena &= ~FMODE_READ;

	if (s->conversion == 1) {
		s->conversion = 0;
		s->prop_adc.fmt = s->prop_adc.fmt_original;
	}
	temp1 = readl(s->pBA0 + BA0_DCR1) | DCRn_MSK;
	writel(temp1, s->pBA0 + BA0_DCR1);
	spin_unlock_irqrestore(&s->lock, flags);
	CS_DBGOUT(CS_FUNCTION, 3,
		  printk(KERN_INFO "cs4281: stop_adc()-\n"));
}


static void start_adc(struct cs4281_state *s)
{
	unsigned long flags;
	unsigned temp1;

	CS_DBGOUT(CS_FUNCTION, 2,
		  printk(KERN_INFO "cs4281: start_adc()+\n"));

	if (!(s->ena & FMODE_READ) &&
	    (s->dma_adc.mapped || s->dma_adc.count <=
	     (signed) (s->dma_adc.dmasize - 2 * s->dma_adc.fragsize))
	    && s->dma_adc.ready
#ifndef NOT_CS4281_PM
	&& (s->pm.flags & CS4281_PM_IDLE))
#else
) 
#endif
	{
		if (s->prop_adc.fmt & AFMT_S8 || s->prop_adc.fmt & AFMT_U8) {
			// 
			// now only use 16 bit capture, due to truncation issue
			// in the chip, noticable distortion occurs.
			// allocate buffer and then convert from 16 bit to 
			// 8 bit for the user buffer.
			//
			s->prop_adc.fmt_original = s->prop_adc.fmt;
			if (s->prop_adc.fmt & AFMT_S8) {
				s->prop_adc.fmt &= ~AFMT_S8;
				s->prop_adc.fmt |= AFMT_S16_LE;
			}
			if (s->prop_adc.fmt & AFMT_U8) {
				s->prop_adc.fmt &= ~AFMT_U8;
				s->prop_adc.fmt |= AFMT_U16_LE;
			}
			//
			// prog_dmabuf_adc performs a stop_adc() but that is
			// ok since we really haven't started the DMA yet.
			//
			prog_codec(s, CS_TYPE_ADC);

			if (prog_dmabuf_adc(s) != 0) {
				CS_DBGOUT(CS_ERROR, 2, printk(KERN_INFO
					 "cs4281: start_adc(): error in prog_dmabuf_adc\n"));
			}
			s->conversion = 1;
		}
		spin_lock_irqsave(&s->lock, flags);
		s->ena |= FMODE_READ;
		temp1 = readl(s->pBA0 + BA0_DCR1) & ~DCRn_MSK;	// Clear DMA1 channel mask bit.
		writel(temp1, s->pBA0 + BA0_DCR1);	// Start recording
		writel(HICR_IEV | HICR_CHGM, s->pBA0 + BA0_HICR);	// Enable interrupts.
		spin_unlock_irqrestore(&s->lock, flags);

		CS_DBGOUT(CS_PARMS, 6, printk(KERN_INFO
			 "cs4281: start_adc(): writel 0x%x \n", temp1));
	}
	CS_DBGOUT(CS_FUNCTION, 2,
		  printk(KERN_INFO "cs4281: start_adc()-\n"));

}


// --------------------------------------------------------------------- 

#define DMABUF_MINORDER 1	// ==> min buffer size = 8K.


static void dealloc_dmabuf(struct cs4281_state *s, struct dmabuf *db)
{
	struct page *map, *mapend;

	if (db->rawbuf) {
		// Undo prog_dmabuf()'s marking the pages as reserved 
		mapend =
		    virt_to_page(db->rawbuf + (PAGE_SIZE << db->buforder) -
				 1);
		for (map = virt_to_page(db->rawbuf); map <= mapend; map++)
			ClearPageReserved(map);
		free_dmabuf(s, db);
	}
	if (s->tmpbuff && (db->type == CS_TYPE_ADC)) {
		// Undo prog_dmabuf()'s marking the pages as reserved 
		mapend =
		    virt_to_page(s->tmpbuff +
				 (PAGE_SIZE << s->buforder_tmpbuff) - 1);
		for (map = virt_to_page(s->tmpbuff); map <= mapend; map++)
			ClearPageReserved(map);
		free_dmabuf2(s, db);
	}
	s->tmpbuff = NULL;
	db->rawbuf = NULL;
	db->mapped = db->ready = 0;
}

static int prog_dmabuf(struct cs4281_state *s, struct dmabuf *db)
{
	int order;
	unsigned bytespersec, temp1;
	unsigned bufs, sample_shift = 0;
	struct page *map, *mapend;
	unsigned long df;

	CS_DBGOUT(CS_FUNCTION, 2,
		  printk(KERN_INFO "cs4281: prog_dmabuf()+\n"));
	db->hwptr = db->swptr = db->total_bytes = db->count = db->error =
	    db->endcleared = db->blocks = db->wakeup = db->underrun = 0;
/*
* check for order within limits, but do not overwrite value, check
* later for a fractional defaultorder (i.e. 100+).
*/
	if((defaultorder > 0) && (defaultorder < 12))
		df = defaultorder;
	else
		df = 1;	

	if (!db->rawbuf) {
		db->ready = db->mapped = 0;
		for (order = df; order >= DMABUF_MINORDER; order--)
			if ( (db->rawbuf = (void *) pci_alloc_consistent(
				s->pcidev, PAGE_SIZE << order, &db-> dmaaddr)))
				    break;
		if (!db->rawbuf) {
			CS_DBGOUT(CS_ERROR, 1, printk(KERN_ERR
				"cs4281: prog_dmabuf(): unable to allocate rawbuf\n"));
			return -ENOMEM;
		}
		db->buforder = order;
		// Now mark the pages as reserved; otherwise the 
		// remap_pfn_range() in cs4281_mmap doesn't work.
		// 1. get index to last page in mem_map array for rawbuf.
		mapend = virt_to_page(db->rawbuf + 
			(PAGE_SIZE << db->buforder) - 1);

		// 2. mark each physical page in range as 'reserved'.
		for (map = virt_to_page(db->rawbuf); map <= mapend; map++)
			SetPageReserved(map);
	}
	if (!s->tmpbuff && (db->type == CS_TYPE_ADC)) {
		for (order = df; order >= DMABUF_MINORDER;
		     order--)
			if ( (s->tmpbuff = (void *) pci_alloc_consistent(
					s->pcidev, PAGE_SIZE << order, 
					&s->dmaaddr_tmpbuff)))
				    break;
		if (!s->tmpbuff) {
			CS_DBGOUT(CS_ERROR, 1, printk(KERN_ERR
				"cs4281: prog_dmabuf(): unable to allocate tmpbuff\n"));
			return -ENOMEM;
		}
		s->buforder_tmpbuff = order;
		// Now mark the pages as reserved; otherwise the 
		// remap_pfn_range() in cs4281_mmap doesn't work.
		// 1. get index to last page in mem_map array for rawbuf.
		mapend = virt_to_page(s->tmpbuff + 
				(PAGE_SIZE << s->buforder_tmpbuff) - 1);

		// 2. mark each physical page in range as 'reserved'.
		for (map = virt_to_page(s->tmpbuff); map <= mapend; map++)
			SetPageReserved(map);
	}
	if (db->type == CS_TYPE_DAC) {
		if (s->prop_dac.fmt & (AFMT_S16_LE | AFMT_U16_LE))
			sample_shift++;
		if (s->prop_dac.channels > 1)
			sample_shift++;
		bytespersec = s->prop_dac.rate << sample_shift;
	} else			// CS_TYPE_ADC
	{
		if (s->prop_adc.fmt & (AFMT_S16_LE | AFMT_U16_LE))
			sample_shift++;
		if (s->prop_adc.channels > 1)
			sample_shift++;
		bytespersec = s->prop_adc.rate << sample_shift;
	}
	bufs = PAGE_SIZE << db->buforder;

/*
* added fractional "defaultorder" inputs. if >100 then use 
* defaultorder-100 as power of 2 for the buffer size. example:
* 106 = 2^(106-100) = 2^6 = 64 bytes for the buffer size.
*/
	if(defaultorder >= 100)
	{
		bufs = 1 << (defaultorder-100);
	}

#define INTERRUPT_RATE_MS       100	// Interrupt rate in milliseconds.
	db->numfrag = 2;
/* 
* Nominal frag size(bytes/interrupt)
*/
	temp1 = bytespersec / (1000 / INTERRUPT_RATE_MS);
	db->fragshift = 8;	// Min 256 bytes.
	while (1 << db->fragshift < temp1)	// Calc power of 2 frag size.
		db->fragshift += 1;
	db->fragsize = 1 << db->fragshift;
	db->dmasize = db->fragsize * 2;
	db->fragsamples = db->fragsize >> sample_shift;	// # samples/fragment.

// If the calculated size is larger than the allocated
//  buffer, divide the allocated buffer into 2 fragments.
	if (db->dmasize > bufs) {

		db->numfrag = 2;	// Two fragments.
		db->fragsize = bufs >> 1;	// Each 1/2 the alloc'ed buffer.
		db->fragsamples = db->fragsize >> sample_shift;	// # samples/fragment.
		db->dmasize = bufs;	// Use all the alloc'ed buffer.

		db->fragshift = 0;	// Calculate 'fragshift'.
		temp1 = db->fragsize;	// update_ptr() uses it 
		while ((temp1 >>= 1) > 1)	// to calc 'total-bytes'
			db->fragshift += 1;	// returned in DSP_GETI/OPTR. 
	}
	CS_DBGOUT(CS_PARMS, 3, printk(KERN_INFO
		"cs4281: prog_dmabuf(): numfrag=%d fragsize=%d fragsamples=%d fragshift=%d bufs=%d fmt=0x%x ch=%d\n",
			db->numfrag, db->fragsize, db->fragsamples, 
			db->fragshift, bufs, 
			(db->type == CS_TYPE_DAC) ? s->prop_dac.fmt : 
				s->prop_adc.fmt, 
			(db->type == CS_TYPE_DAC) ? s->prop_dac.channels : 
				s->prop_adc.channels));
	CS_DBGOUT(CS_FUNCTION, 2,
		  printk(KERN_INFO "cs4281: prog_dmabuf()-\n"));
	return 0;
}


static int prog_dmabuf_adc(struct cs4281_state *s)
{
	unsigned long va;
	unsigned count;
	int c;
	stop_adc(s);
	s->dma_adc.type = CS_TYPE_ADC;
	if ((c = prog_dmabuf(s, &s->dma_adc)))
		return c;

	if (s->dma_adc.rawbuf) {
		memset(s->dma_adc.rawbuf,
		       (s->prop_adc.
			fmt & (AFMT_U8 | AFMT_U16_LE)) ? 0x80 : 0,
		       s->dma_adc.dmasize);
	}
	if (s->tmpbuff) {
		memset(s->tmpbuff,
		       (s->prop_adc.
			fmt & (AFMT_U8 | AFMT_U16_LE)) ? 0x80 : 0,
		       PAGE_SIZE << s->buforder_tmpbuff);
	}

	va = virt_to_bus(s->dma_adc.rawbuf);

	count = s->dma_adc.dmasize;

	if (s->prop_adc.
	    fmt & (AFMT_S16_LE | AFMT_U16_LE | AFMT_S16_BE | AFMT_U16_BE))
		    count /= 2;	// 16-bit.

	if (s->prop_adc.channels > 1)
		count /= 2;	// Assume stereo.

	CS_DBGOUT(CS_WAVE_READ, 3, printk(KERN_INFO
		"cs4281: prog_dmabuf_adc(): count=%d va=0x%.8x\n",
			count, (unsigned) va));

	writel(va, s->pBA0 + BA0_DBA1);	// Set buffer start address.
	writel(count - 1, s->pBA0 + BA0_DBC1);	// Set count. 
	s->dma_adc.ready = 1;
	return 0;
}


static int prog_dmabuf_dac(struct cs4281_state *s)
{
	unsigned long va;
	unsigned count;
	int c;
	stop_dac(s);
	s->dma_dac.type = CS_TYPE_DAC;
	if ((c = prog_dmabuf(s, &s->dma_dac)))
		return c;
	memset(s->dma_dac.rawbuf,
	       (s->prop_dac.fmt & (AFMT_U8 | AFMT_U16_LE)) ? 0x80 : 0,
	       s->dma_dac.dmasize);

	va = virt_to_bus(s->dma_dac.rawbuf);

	count = s->dma_dac.dmasize;
	if (s->prop_dac.
	    fmt & (AFMT_S16_LE | AFMT_U16_LE | AFMT_S16_BE | AFMT_U16_BE))
		    count /= 2;	// 16-bit.

	if (s->prop_dac.channels > 1)
		count /= 2;	// Assume stereo.

	writel(va, s->pBA0 + BA0_DBA0);	// Set buffer start address.
	writel(count - 1, s->pBA0 + BA0_DBC0);	// Set count.             

	CS_DBGOUT(CS_WAVE_WRITE, 3, printk(KERN_INFO
		"cs4281: prog_dmabuf_dac(): count=%d va=0x%.8x\n",
			count, (unsigned) va));

	s->dma_dac.ready = 1;
	return 0;
}


static void clear_advance(void *buf, unsigned bsize, unsigned bptr,
			  unsigned len, unsigned char c)
{
	if (bptr + len > bsize) {
		unsigned x = bsize - bptr;
		memset(((char *) buf) + bptr, c, x);
		bptr = 0;
		len -= x;
	}
	CS_DBGOUT(CS_WAVE_WRITE, 4, printk(KERN_INFO
		"cs4281: clear_advance(): memset %d at %p for %d size \n",
			(unsigned)c, ((char *) buf) + bptr, len));
	memset(((char *) buf) + bptr, c, len);
}



// call with spinlock held! 
static void cs4281_update_ptr(struct cs4281_state *s, int intflag)
{
	int diff;
	unsigned hwptr, va;

	// update ADC pointer 
	if (s->ena & FMODE_READ) {
		hwptr = readl(s->pBA0 + BA0_DCA1);	// Read capture DMA address.
		va = virt_to_bus(s->dma_adc.rawbuf);
		hwptr -= (unsigned) va;
		diff =
		    (s->dma_adc.dmasize + hwptr -
		     s->dma_adc.hwptr) % s->dma_adc.dmasize;
		s->dma_adc.hwptr = hwptr;
		s->dma_adc.total_bytes += diff;
		s->dma_adc.count += diff;
		if (s->dma_adc.count > s->dma_adc.dmasize)
			s->dma_adc.count = s->dma_adc.dmasize;
		if (s->dma_adc.mapped) {
			if (s->dma_adc.count >=
			    (signed) s->dma_adc.fragsize) wake_up(&s->
								  dma_adc.
								  wait);
		} else {
			if (s->dma_adc.count > 0)
				wake_up(&s->dma_adc.wait);
		}
		CS_DBGOUT(CS_PARMS, 8, printk(KERN_INFO
			"cs4281: cs4281_update_ptr(): s=%p hwptr=%d total_bytes=%d count=%d \n",
				s, s->dma_adc.hwptr, s->dma_adc.total_bytes, s->dma_adc.count));
	}
	// update DAC pointer 
	//
	// check for end of buffer, means that we are going to wait for another interrupt
	// to allow silence to fill the fifos on the part, to keep pops down to a minimum.
	//
	if (s->ena & FMODE_WRITE) {
		hwptr = readl(s->pBA0 + BA0_DCA0);	// Read play DMA address.
		va = virt_to_bus(s->dma_dac.rawbuf);
		hwptr -= (unsigned) va;
		diff = (s->dma_dac.dmasize + hwptr -
		     s->dma_dac.hwptr) % s->dma_dac.dmasize;
		s->dma_dac.hwptr = hwptr;
		s->dma_dac.total_bytes += diff;
		if (s->dma_dac.mapped) {
			s->dma_dac.count += diff;
			if (s->dma_dac.count >= s->dma_dac.fragsize) {
				s->dma_dac.wakeup = 1;
				wake_up(&s->dma_dac.wait);
				if (s->dma_dac.count > s->dma_dac.dmasize)
					s->dma_dac.count &=
					    s->dma_dac.dmasize - 1;
			}
		} else {
			s->dma_dac.count -= diff;
			if (s->dma_dac.count <= 0) {
				//
				// fill with silence, and do not shut down the DAC.
				// Continue to play silence until the _release.
				//
				CS_DBGOUT(CS_WAVE_WRITE, 6, printk(KERN_INFO
					"cs4281: cs4281_update_ptr(): memset %d at %p for %d size \n",
						(unsigned)(s->prop_dac.fmt & 
						(AFMT_U8 | AFMT_U16_LE)) ? 0x80 : 0, 
						s->dma_dac.rawbuf, s->dma_dac.dmasize));
				memset(s->dma_dac.rawbuf,
				       (s->prop_dac.
					fmt & (AFMT_U8 | AFMT_U16_LE)) ?
				       0x80 : 0, s->dma_dac.dmasize);
				if (s->dma_dac.count < 0) {
					s->dma_dac.underrun = 1;
					s->dma_dac.count = 0;
					CS_DBGOUT(CS_ERROR, 9, printk(KERN_INFO
					 "cs4281: cs4281_update_ptr(): underrun\n"));
				}
			} else if (s->dma_dac.count <=
				   (signed) s->dma_dac.fragsize
				   && !s->dma_dac.endcleared) {
				clear_advance(s->dma_dac.rawbuf,
					      s->dma_dac.dmasize,
					      s->dma_dac.swptr,
					      s->dma_dac.fragsize,
					      (s->prop_dac.
					       fmt & (AFMT_U8 |
						      AFMT_U16_LE)) ? 0x80
					      : 0);
				s->dma_dac.endcleared = 1;
			}
			if ( (s->dma_dac.count <= (signed) s->dma_dac.dmasize/2) ||
				intflag)
			{
				wake_up(&s->dma_dac.wait);
			}
		}
		CS_DBGOUT(CS_PARMS, 8, printk(KERN_INFO
			"cs4281: cs4281_update_ptr(): s=%p hwptr=%d total_bytes=%d count=%d \n",
				s, s->dma_dac.hwptr, s->dma_dac.total_bytes, s->dma_dac.count));
	}
}


// --------------------------------------------------------------------- 

static void prog_codec(struct cs4281_state *s, unsigned type)
{
	unsigned long flags;
	unsigned temp1, format;

	CS_DBGOUT(CS_FUNCTION, 2,
		  printk(KERN_INFO "cs4281: prog_codec()+ \n"));

	spin_lock_irqsave(&s->lock, flags);
	if (type == CS_TYPE_ADC) {
		temp1 = readl(s->pBA0 + BA0_DCR1);
		writel(temp1 | DCRn_MSK, s->pBA0 + BA0_DCR1);	// Stop capture DMA, if active.

		// program sampling rates  
		// Note, for CS4281, capture & play rates can be set independently.
		cs4281_record_rate(s, s->prop_adc.rate);

		// program ADC parameters 
		format = DMRn_DMA | DMRn_AUTO | DMRn_TR_WRITE;
		if (s->prop_adc.
		    fmt & (AFMT_S16_LE | AFMT_U16_LE | AFMT_S16_BE | AFMT_U16_BE)) {	// 16-bit
			if (s->prop_adc.fmt & (AFMT_S16_BE | AFMT_U16_BE))	// Big-endian?
				format |= DMRn_BEND;
			if (s->prop_adc.fmt & (AFMT_U16_LE | AFMT_U16_BE))
				format |= DMRn_USIGN;	// Unsigned.      
		} else
			format |= DMRn_SIZE8 | DMRn_USIGN;	// 8-bit, unsigned
		if (s->prop_adc.channels < 2)
			format |= DMRn_MONO;

		writel(format, s->pBA0 + BA0_DMR1);

		CS_DBGOUT(CS_PARMS, 2, printk(KERN_INFO
			"cs4281: prog_codec(): adc %s %s %s rate=%d DMR0 format=0x%.8x\n",
				(format & DMRn_SIZE8) ? "8" : "16",
				(format & DMRn_USIGN) ?  "Unsigned" : "Signed", 
				(format & DMRn_MONO) ? "Mono" : "Stereo", 
				s->prop_adc.rate, format));

		s->ena &= ~FMODE_READ;	// not capturing data yet
	}


	if (type == CS_TYPE_DAC) {
		temp1 = readl(s->pBA0 + BA0_DCR0);
		writel(temp1 | DCRn_MSK, s->pBA0 + BA0_DCR0);	// Stop play DMA, if active.

		// program sampling rates  
		// Note, for CS4281, capture & play rates can be set independently.
		cs4281_play_rate(s, s->prop_dac.rate);

		// program DAC parameters 
		format = DMRn_DMA | DMRn_AUTO | DMRn_TR_READ;
		if (s->prop_dac.
		    fmt & (AFMT_S16_LE | AFMT_U16_LE | AFMT_S16_BE | AFMT_U16_BE)) {	// 16-bit
			if (s->prop_dac.fmt & (AFMT_S16_BE | AFMT_U16_BE))
				format |= DMRn_BEND;	// Big Endian.
			if (s->prop_dac.fmt & (AFMT_U16_LE | AFMT_U16_BE))
				format |= DMRn_USIGN;	// Unsigned.      
		} else
			format |= DMRn_SIZE8 | DMRn_USIGN;	// 8-bit, unsigned

		if (s->prop_dac.channels < 2)
			format |= DMRn_MONO;

		writel(format, s->pBA0 + BA0_DMR0);


		CS_DBGOUT(CS_PARMS, 2, printk(KERN_INFO
			"cs4281: prog_codec(): dac %s %s %s rate=%d DMR0 format=0x%.8x\n",
				(format & DMRn_SIZE8) ? "8" : "16",
				(format & DMRn_USIGN) ?  "Unsigned" : "Signed",
				(format & DMRn_MONO) ? "Mono" : "Stereo", 
				s->prop_dac.rate, format));

		s->ena &= ~FMODE_WRITE;	// not capturing data yet

	}
	spin_unlock_irqrestore(&s->lock, flags);
	CS_DBGOUT(CS_FUNCTION, 2,
		  printk(KERN_INFO "cs4281: prog_codec()- \n"));
}


static int mixer_ioctl(struct cs4281_state *s, unsigned int cmd,
		       unsigned long arg)
{
	// Index to mixer_src[] is value of AC97 Input Mux Select Reg.
	// Value of array member is recording source Device ID Mask.
	static const unsigned int mixer_src[8] = {
		SOUND_MASK_MIC, SOUND_MASK_CD, 0, SOUND_MASK_LINE1,
		SOUND_MASK_LINE, SOUND_MASK_VOLUME, 0, 0
	};
	void __user *argp = (void __user *)arg;

	// Index of mixtable1[] member is Device ID 
	// and must be <= SOUND_MIXER_NRDEVICES.
	// Value of array member is index into s->mix.vol[]
	static const unsigned char mixtable1[SOUND_MIXER_NRDEVICES] = {
		[SOUND_MIXER_PCM] = 1,	// voice 
		[SOUND_MIXER_LINE1] = 2,	// AUX
		[SOUND_MIXER_CD] = 3,	// CD 
		[SOUND_MIXER_LINE] = 4,	// Line 
		[SOUND_MIXER_SYNTH] = 5,	// FM
		[SOUND_MIXER_MIC] = 6,	// Mic 
		[SOUND_MIXER_SPEAKER] = 7,	// Speaker 
		[SOUND_MIXER_RECLEV] = 8,	// Recording level 
		[SOUND_MIXER_VOLUME] = 9	// Master Volume 
	};


	static const unsigned mixreg[] = {
		BA0_AC97_PCM_OUT_VOLUME,
		BA0_AC97_AUX_VOLUME,
		BA0_AC97_CD_VOLUME,
		BA0_AC97_LINE_IN_VOLUME
	};
	unsigned char l, r, rl, rr, vidx;
	unsigned char attentbl[11] =
	    { 63, 42, 26, 17, 14, 11, 8, 6, 4, 2, 0 };
	unsigned temp1;
	int i, val;

	VALIDATE_STATE(s);
	CS_DBGOUT(CS_FUNCTION, 4, printk(KERN_INFO
		 "cs4281: mixer_ioctl(): s=%p cmd=0x%.8x\n", s, cmd));
#if CSDEBUG
	cs_printioctl(cmd);
#endif
#if CSDEBUG_INTERFACE

	if ((cmd == SOUND_MIXER_CS_GETDBGMASK) ||
	    (cmd == SOUND_MIXER_CS_SETDBGMASK) ||
	    (cmd == SOUND_MIXER_CS_GETDBGLEVEL) ||
	    (cmd == SOUND_MIXER_CS_SETDBGLEVEL) ||
	    (cmd == SOUND_MIXER_CS_APM))
	{
		switch (cmd) {

		case SOUND_MIXER_CS_GETDBGMASK:
			return put_user(cs_debugmask,
					(unsigned long __user *) argp);

		case SOUND_MIXER_CS_GETDBGLEVEL:
			return put_user(cs_debuglevel,
					(unsigned long __user *) argp);

		case SOUND_MIXER_CS_SETDBGMASK:
			if (get_user(val, (unsigned long __user *) argp))
				return -EFAULT;
			cs_debugmask = val;
			return 0;

		case SOUND_MIXER_CS_SETDBGLEVEL:
			if (get_user(val, (unsigned long __user *) argp))
				return -EFAULT;
			cs_debuglevel = val;
			return 0;
#ifndef NOT_CS4281_PM
		case SOUND_MIXER_CS_APM:
			if (get_user(val, (unsigned long __user *) argp))
				return -EFAULT;
			if(val == CS_IOCTL_CMD_SUSPEND)
				cs4281_suspend(s);
			else if(val == CS_IOCTL_CMD_RESUME)
				cs4281_resume(s);
			else
			{
				CS_DBGOUT(CS_ERROR, 1, printk(KERN_INFO
				    "cs4281: mixer_ioctl(): invalid APM cmd (%d)\n",
					val));
			}
			return 0;
#endif
		default:
			CS_DBGOUT(CS_ERROR, 1, printk(KERN_INFO
				"cs4281: mixer_ioctl(): ERROR unknown debug cmd\n"));
			return 0;
		}
	}
#endif

	if (cmd == SOUND_MIXER_PRIVATE1) {
		// enable/disable/query mixer preamp 
		if (get_user(val, (int __user *) argp))
			return -EFAULT;
		if (val != -1) {
			cs4281_read_ac97(s, BA0_AC97_MIC_VOLUME, &temp1);
			temp1 = val ? (temp1 | 0x40) : (temp1 & 0xffbf);
			cs4281_write_ac97(s, BA0_AC97_MIC_VOLUME, temp1);
		}
		cs4281_read_ac97(s, BA0_AC97_MIC_VOLUME, &temp1);
		val = (temp1 & 0x40) ? 1 : 0;
		return put_user(val, (int __user *) argp);
	}
	if (cmd == SOUND_MIXER_PRIVATE2) {
		// enable/disable/query spatializer 
		if (get_user(val, (int __user *)argp))
			return -EFAULT;
		if (val != -1) {
			temp1 = (val & 0x3f) >> 2;
			cs4281_write_ac97(s, BA0_AC97_3D_CONTROL, temp1);
			cs4281_read_ac97(s, BA0_AC97_GENERAL_PURPOSE,
					 &temp1);
			cs4281_write_ac97(s, BA0_AC97_GENERAL_PURPOSE,
					  temp1 | 0x2000);
		}
		cs4281_read_ac97(s, BA0_AC97_3D_CONTROL, &temp1);
		return put_user((temp1 << 2) | 3, (int __user *)argp);
	}
	if (cmd == SOUND_MIXER_INFO) {
		mixer_info info;
		strlcpy(info.id, "CS4281", sizeof(info.id));
		strlcpy(info.name, "Crystal CS4281", sizeof(info.name));
		info.modify_counter = s->mix.modcnt;
		if (copy_to_user(argp, &info, sizeof(info)))
			return -EFAULT;
		return 0;
	}
	if (cmd == SOUND_OLD_MIXER_INFO) {
		_old_mixer_info info;
		strlcpy(info.id, "CS4281", sizeof(info.id));
		strlcpy(info.name, "Crystal CS4281", sizeof(info.name));
		if (copy_to_user(argp, &info, sizeof(info)))
			return -EFAULT;
		return 0;
	}
	if (cmd == OSS_GETVERSION)
		return put_user(SOUND_VERSION, (int __user *) argp);

	if (_IOC_TYPE(cmd) != 'M' || _SIOC_SIZE(cmd) != sizeof(int))
		return -EINVAL;

	// If ioctl has only the SIOC_READ bit(bit 31)
	// on, process the only-read commands. 
	if (_SIOC_DIR(cmd) == _SIOC_READ) {
		switch (_IOC_NR(cmd)) {
		case SOUND_MIXER_RECSRC:	// Arg contains a bit for each recording source 
			cs4281_read_ac97(s, BA0_AC97_RECORD_SELECT, &temp1);
			return put_user(mixer_src[temp1&7], (int __user *)argp);

		case SOUND_MIXER_DEVMASK:	// Arg contains a bit for each supported device 
			return put_user(SOUND_MASK_PCM | SOUND_MASK_SYNTH |
					SOUND_MASK_CD | SOUND_MASK_LINE |
					SOUND_MASK_LINE1 | SOUND_MASK_MIC |
					SOUND_MASK_VOLUME |
					SOUND_MASK_RECLEV |
					SOUND_MASK_SPEAKER, (int __user *)argp);

		case SOUND_MIXER_RECMASK:	// Arg contains a bit for each supported recording source 
			return put_user(SOUND_MASK_LINE | SOUND_MASK_MIC |
					SOUND_MASK_CD | SOUND_MASK_VOLUME |
					SOUND_MASK_LINE1, (int __user *) argp);

		case SOUND_MIXER_STEREODEVS:	// Mixer channels supporting stereo 
			return put_user(SOUND_MASK_PCM | SOUND_MASK_SYNTH |
					SOUND_MASK_CD | SOUND_MASK_LINE |
					SOUND_MASK_LINE1 | SOUND_MASK_MIC |
					SOUND_MASK_VOLUME |
					SOUND_MASK_RECLEV, (int __user *)argp);

		case SOUND_MIXER_CAPS:
			return put_user(SOUND_CAP_EXCL_INPUT, (int __user *)argp);

		default:
			i = _IOC_NR(cmd);
			if (i >= SOUND_MIXER_NRDEVICES
			    || !(vidx = mixtable1[i]))
				return -EINVAL;
			return put_user(s->mix.vol[vidx - 1], (int __user *)argp);
		}
	}
	// If ioctl doesn't have both the SIOC_READ and 
	// the SIOC_WRITE bit set, return invalid.
	if (_SIOC_DIR(cmd) != (_SIOC_READ | _SIOC_WRITE))
		return -EINVAL;

	// Increment the count of volume writes.
	s->mix.modcnt++;

	// Isolate the command; it must be a write.
	switch (_IOC_NR(cmd)) {

	case SOUND_MIXER_RECSRC:	// Arg contains a bit for each recording source 
		if (get_user(val, (int __user *)argp))
			return -EFAULT;
		i = hweight32(val);	// i = # bits on in val.
		if (i != 1)	// One & only 1 bit must be on.
			return 0;
		for (i = 0; i < sizeof(mixer_src) / sizeof(int); i++) {
			if (val == mixer_src[i]) {
				temp1 = (i << 8) | i;
				cs4281_write_ac97(s,
						  BA0_AC97_RECORD_SELECT,
						  temp1);
				return 0;
			}
		}
		return 0;

	case SOUND_MIXER_VOLUME:
		if (get_user(val, (int __user *)argp))
			return -EFAULT;
		l = val & 0xff;
		if (l > 100)
			l = 100;	// Max soundcard.h vol is 100.
		if (l < 6) {
			rl = 63;
			l = 0;
		} else
			rl = attentbl[(10 * l) / 100];	// Convert 0-100 vol to 63-0 atten.

		r = (val >> 8) & 0xff;
		if (r > 100)
			r = 100;	// Max right volume is 100, too
		if (r < 6) {
			rr = 63;
			r = 0;
		} else
			rr = attentbl[(10 * r) / 100];	// Convert volume to attenuation.

		if ((rl > 60) && (rr > 60))	// If both l & r are 'low',          
			temp1 = 0x8000;	//  turn on the mute bit.
		else
			temp1 = 0;

		temp1 |= (rl << 8) | rr;

		cs4281_write_ac97(s, BA0_AC97_MASTER_VOLUME, temp1);
		cs4281_write_ac97(s, BA0_AC97_HEADPHONE_VOLUME, temp1);

#ifdef OSS_DOCUMENTED_MIXER_SEMANTICS
		s->mix.vol[8] = ((unsigned int) r << 8) | l;
#else
		s->mix.vol[8] = val;
#endif
		return put_user(s->mix.vol[8], (int __user *)argp);

	case SOUND_MIXER_SPEAKER:
		if (get_user(val, (int __user *)argp))
			return -EFAULT;
		l = val & 0xff;
		if (l > 100)
			l = 100;
		if (l < 3) {
			rl = 0;
			l = 0;
		} else {
			rl = (l * 2 - 5) / 13;	// Convert 0-100 range to 0-15.
			l = (rl * 13 + 5) / 2;
		}

		if (rl < 3) {
			temp1 = 0x8000;
			rl = 0;
		} else
			temp1 = 0;
		rl = 15 - rl;	// Convert volume to attenuation.
		temp1 |= rl << 1;
		cs4281_write_ac97(s, BA0_AC97_PC_BEEP_VOLUME, temp1);

#ifdef OSS_DOCUMENTED_MIXER_SEMANTICS
		s->mix.vol[6] = l << 8;
#else
		s->mix.vol[6] = val;
#endif
		return put_user(s->mix.vol[6], (int __user *)argp);

	case SOUND_MIXER_RECLEV:
		if (get_user(val, (int __user *)argp))
			return -EFAULT;
		l = val & 0xff;
		if (l > 100)
			l = 100;
		r = (val >> 8) & 0xff;
		if (r > 100)
			r = 100;
		rl = (l * 2 - 5) / 13;	// Convert 0-100 scale to 0-15.
		rr = (r * 2 - 5) / 13;
		if (rl < 3 && rr < 3)
			temp1 = 0x8000;
		else
			temp1 = 0;

		temp1 = temp1 | (rl << 8) | rr;
		cs4281_write_ac97(s, BA0_AC97_RECORD_GAIN, temp1);

#ifdef OSS_DOCUMENTED_MIXER_SEMANTICS
		s->mix.vol[7] = ((unsigned int) r << 8) | l;
#else
		s->mix.vol[7] = val;
#endif
		return put_user(s->mix.vol[7], (int __user *)argp);

	case SOUND_MIXER_MIC:
		if (get_user(val, (int __user *)argp))
			return -EFAULT;
		l = val & 0xff;
		if (l > 100)
			l = 100;
		if (l < 1) {
			l = 0;
			rl = 0;
		} else {
			rl = ((unsigned) l * 5 - 4) / 16;	// Convert 0-100 range to 0-31.
			l = (rl * 16 + 4) / 5;
		}
		cs4281_read_ac97(s, BA0_AC97_MIC_VOLUME, &temp1);
		temp1 &= 0x40;	// Isolate 20db gain bit.
		if (rl < 3) {
			temp1 |= 0x8000;
			rl = 0;
		}
		rl = 31 - rl;	// Convert volume to attenuation.
		temp1 |= rl;
		cs4281_write_ac97(s, BA0_AC97_MIC_VOLUME, temp1);

#ifdef OSS_DOCUMENTED_MIXER_SEMANTICS
		s->mix.vol[5] = val << 8;
#else
		s->mix.vol[5] = val;
#endif
		return put_user(s->mix.vol[5], (int __user *)argp);


	case SOUND_MIXER_SYNTH:
		if (get_user(val, (int __user *)argp))
			return -EFAULT;
		l = val & 0xff;
		if (l > 100)
			l = 100;
		if (get_user(val, (int __user *)argp))
			return -EFAULT;
		r = (val >> 8) & 0xff;
		if (r > 100)
			r = 100;
		rl = (l * 2 - 11) / 3;	// Convert 0-100 range to 0-63.
		rr = (r * 2 - 11) / 3;
		if (rl < 3)	// If l is low, turn on
			temp1 = 0x0080;	//  the mute bit.
		else
			temp1 = 0;

		rl = 63 - rl;	// Convert vol to attenuation.
		writel(temp1 | rl, s->pBA0 + BA0_FMLVC);
		if (rr < 3)	//  If rr is low, turn on
			temp1 = 0x0080;	//   the mute bit.
		else
			temp1 = 0;
		rr = 63 - rr;	// Convert vol to attenuation.
		writel(temp1 | rr, s->pBA0 + BA0_FMRVC);

#ifdef OSS_DOCUMENTED_MIXER_SEMANTICS
		s->mix.vol[4] = (r << 8) | l;
#else
		s->mix.vol[4] = val;
#endif
		return put_user(s->mix.vol[4], (int __user *)argp);


	default:
		CS_DBGOUT(CS_IOCTL, 4, printk(KERN_INFO
			"cs4281: mixer_ioctl(): default\n"));

		i = _IOC_NR(cmd);
		if (i >= SOUND_MIXER_NRDEVICES || !(vidx = mixtable1[i]))
			return -EINVAL;
		if (get_user(val, (int __user *)argp))
			return -EFAULT;
		l = val & 0xff;
		if (l > 100)
			l = 100;
		if (l < 1) {
			l = 0;
			rl = 31;
		} else
			rl = (attentbl[(l * 10) / 100]) >> 1;

		r = (val >> 8) & 0xff;
		if (r > 100)
			r = 100;
		if (r < 1) {
			r = 0;
			rr = 31;
		} else
			rr = (attentbl[(r * 10) / 100]) >> 1;
		if ((rl > 30) && (rr > 30))
			temp1 = 0x8000;
		else
			temp1 = 0;
		temp1 = temp1 | (rl << 8) | rr;
		cs4281_write_ac97(s, mixreg[vidx - 1], temp1);

#ifdef OSS_DOCUMENTED_MIXER_SEMANTICS
		s->mix.vol[vidx - 1] = ((unsigned int) r << 8) | l;
#else
		s->mix.vol[vidx - 1] = val;
#endif
#ifndef NOT_CS4281_PM
		CS_DBGOUT(CS_PM, 9, printk(KERN_INFO 
			"write ac97 mixreg[%d]=0x%x mix.vol[]=0x%x\n", 
				vidx-1,temp1,s->mix.vol[vidx-1]));
#endif
		return put_user(s->mix.vol[vidx - 1], (int __user *)argp);
	}
}


// --------------------------------------------------------------------- 

static int cs4281_open_mixdev(struct inode *inode, struct file *file)
{
	unsigned int minor = iminor(inode);
	struct cs4281_state *s=NULL;
	struct list_head *entry;

	CS_DBGOUT(CS_FUNCTION | CS_OPEN, 4,
		  printk(KERN_INFO "cs4281: cs4281_open_mixdev()+\n"));

	list_for_each(entry, &cs4281_devs)
	{
		s = list_entry(entry, struct cs4281_state, list);
		if(s->dev_mixer == minor)
			break;
	}
	if (!s)
	{
		CS_DBGOUT(CS_FUNCTION | CS_OPEN | CS_ERROR, 2,
			printk(KERN_INFO "cs4281: cs4281_open_mixdev()- -ENODEV\n"));
		return -ENODEV;
	}
	VALIDATE_STATE(s);
	file->private_data = s;

	CS_DBGOUT(CS_FUNCTION | CS_OPEN, 4,
		  printk(KERN_INFO "cs4281: cs4281_open_mixdev()- 0\n"));

	return nonseekable_open(inode, file);
}


static int cs4281_release_mixdev(struct inode *inode, struct file *file)
{
	struct cs4281_state *s =
	    (struct cs4281_state *) file->private_data;

	VALIDATE_STATE(s);
	return 0;
}


static int cs4281_ioctl_mixdev(struct inode *inode, struct file *file,
			       unsigned int cmd, unsigned long arg)
{
	return mixer_ioctl((struct cs4281_state *) file->private_data, cmd,
			   arg);
}


// ******************************************************************************************
//   Mixer file operations struct.
// ******************************************************************************************
static /*const */ struct file_operations cs4281_mixer_fops = {
	.owner	 = THIS_MODULE,
	.llseek	 = no_llseek,
	.ioctl	 = cs4281_ioctl_mixdev,
	.open	 = cs4281_open_mixdev,
	.release = cs4281_release_mixdev,
};

// --------------------------------------------------------------------- 


static int drain_adc(struct cs4281_state *s, int nonblock)
{
	DECLARE_WAITQUEUE(wait, current);
	unsigned long flags;
	int count;
	unsigned tmo;

	if (s->dma_adc.mapped)
		return 0;
	add_wait_queue(&s->dma_adc.wait, &wait);
	for (;;) {
		set_current_state(TASK_INTERRUPTIBLE);
		spin_lock_irqsave(&s->lock, flags);
		count = s->dma_adc.count;
		CS_DBGOUT(CS_FUNCTION, 2,
			  printk(KERN_INFO "cs4281: drain_adc() %d\n", count));
		spin_unlock_irqrestore(&s->lock, flags);
		if (count <= 0) {
			CS_DBGOUT(CS_FUNCTION, 2, printk(KERN_INFO
				 "cs4281: drain_adc() count<0\n"));
			break;
		}
		if (signal_pending(current))
			break;
		if (nonblock) {
			remove_wait_queue(&s->dma_adc.wait, &wait);
			current->state = TASK_RUNNING;
			return -EBUSY;
		}
		tmo =
		    3 * HZ * (count +
			      s->dma_adc.fragsize) / 2 / s->prop_adc.rate;
		if (s->prop_adc.fmt & (AFMT_S16_LE | AFMT_U16_LE))
			tmo >>= 1;
		if (s->prop_adc.channels > 1)
			tmo >>= 1;
		if (!schedule_timeout(tmo + 1))
			printk(KERN_DEBUG "cs4281: dma timed out??\n");
	}
	remove_wait_queue(&s->dma_adc.wait, &wait);
	current->state = TASK_RUNNING;
	if (signal_pending(current))
		return -ERESTARTSYS;
	return 0;
}

static int drain_dac(struct cs4281_state *s, int nonblock)
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
			current->state = TASK_RUNNING;
			return -EBUSY;
		}
		tmo =
		    3 * HZ * (count +
			      s->dma_dac.fragsize) / 2 / s->prop_dac.rate;
		if (s->prop_dac.fmt & (AFMT_S16_LE | AFMT_U16_LE))
			tmo >>= 1;
		if (s->prop_dac.channels > 1)
			tmo >>= 1;
		if (!schedule_timeout(tmo + 1))
			printk(KERN_DEBUG "cs4281: dma timed out??\n");
	}
	remove_wait_queue(&s->dma_dac.wait, &wait);
	current->state = TASK_RUNNING;
	if (signal_pending(current))
		return -ERESTARTSYS;
	return 0;
}

//****************************************************************************
//
// CopySamples copies 16-bit stereo samples from the source to the
// destination, possibly converting down to either 8-bit or mono or both.
// count specifies the number of output bytes to write.
//
//  Arguments:
//
//  dst             - Pointer to a destination buffer.
//  src             - Pointer to a source buffer
//  count           - The number of bytes to copy into the destination buffer.
//  iChannels       - Stereo - 2
//                    Mono   - 1
//  fmt             - AFMT_xxx (soundcard.h formats)
//
// NOTES: only call this routine for conversion to 8bit from 16bit
//
//****************************************************************************
static void CopySamples(char *dst, char *src, int count, int iChannels,
			unsigned fmt)
{

	unsigned short *psSrc;
	long lAudioSample;

	CS_DBGOUT(CS_FUNCTION, 2,
		  printk(KERN_INFO "cs4281: CopySamples()+ "));
	CS_DBGOUT(CS_WAVE_READ, 8, printk(KERN_INFO
		 " dst=%p src=%p count=%d iChannels=%d fmt=0x%x\n",
			 dst, src, (unsigned) count, (unsigned) iChannels, (unsigned) fmt));

	// Gershwin does format conversion in hardware so normally
	// we don't do any host based coversion. The data formatter
	// truncates 16 bit data to 8 bit and that causes some hiss.
	// We have already forced the HW to do 16 bit sampling and 
	// 2 channel so that we can use software to round instead 
	// of truncate

	//
	// See if the data should be output as 8-bit unsigned stereo.
	// or if the data should be output at 8-bit unsigned mono.
	//
	if ( ((iChannels == 2) && (fmt & AFMT_U8)) ||
		((iChannels == 1) && (fmt & AFMT_U8)) ) {
		//
		// Convert each 16-bit unsigned stereo sample to 8-bit unsigned 
		// stereo using rounding.
		//
		psSrc = (unsigned short *) src;
		count = count / 2;
		while (count--) {
			lAudioSample = (long) psSrc[count] + (long) 0x80;
			if (lAudioSample > 0xffff) {
				lAudioSample = 0xffff;
			}
			dst[count] = (char) (lAudioSample >> 8);
		}
	}
	//
	// check for 8-bit signed stereo.
	//
	else if ((iChannels == 2) && (fmt & AFMT_S8)) {
		//
		// Convert each 16-bit stereo sample to 8-bit stereo using rounding.
		//
		psSrc = (short *) src;
		while (count--) {
			lAudioSample =
			    (((long) psSrc[0] + (long) psSrc[1]) / 2);
			psSrc += 2;
			*dst++ = (char) ((short) lAudioSample >> 8);
		}
	}
	//
	// Otherwise, the data should be output as 8-bit signed mono.
	//
	else if ((iChannels == 1) && (fmt & AFMT_S8)) {
		//
		// Convert each 16-bit signed mono sample to 8-bit signed mono 
		// using rounding.
		//
		psSrc = (short *) src;
		count = count / 2;
		while (count--) {
			lAudioSample =
			    (((long) psSrc[0] + (long) psSrc[1]) / 2);
			if (lAudioSample > 0x7fff) {
				lAudioSample = 0x7fff;
			}
			psSrc += 2;
			*dst++ = (char) ((short) lAudioSample >> 8);
		}
	}
}

//
// cs_copy_to_user()
// replacement for the standard copy_to_user, to allow for a conversion from
// 16 bit to 8 bit if the record conversion is active.  the cs4281 has some
// issues with 8 bit capture, so the driver always captures data in 16 bit
// and then if the user requested 8 bit, converts from 16 to 8 bit.
//
static unsigned cs_copy_to_user(struct cs4281_state *s, void __user *dest,
				unsigned *hwsrc, unsigned cnt,
				unsigned *copied)
{
	void *src = hwsrc;	//default to the standard destination buffer addr

	CS_DBGOUT(CS_FUNCTION, 6, printk(KERN_INFO
		"cs_copy_to_user()+ fmt=0x%x fmt_o=0x%x cnt=%d dest=%p\n",
			s->prop_adc.fmt, s->prop_adc.fmt_original,
			(unsigned) cnt, dest));

	if (cnt > s->dma_adc.dmasize) {
		cnt = s->dma_adc.dmasize;
	}
	if (!cnt) {
		*copied = 0;
		return 0;
	}
	if (s->conversion) {
		if (!s->tmpbuff) {
			*copied = cnt / 2;
			return 0;
		}
		CopySamples(s->tmpbuff, (void *) hwsrc, cnt,
			    (unsigned) s->prop_adc.channels,
			    s->prop_adc.fmt_original);
		src = s->tmpbuff;
		cnt = cnt / 2;
	}

	if (copy_to_user(dest, src, cnt)) {
		*copied = 0;
		return -EFAULT;
	}
	*copied = cnt;
	CS_DBGOUT(CS_FUNCTION, 2, printk(KERN_INFO
		"cs4281: cs_copy_to_user()- copied bytes is %d \n", cnt));
	return 0;
}

// --------------------------------------------------------------------- 

static ssize_t cs4281_read(struct file *file, char __user *buffer, size_t count,
			   loff_t * ppos)
{
	struct cs4281_state *s =
	    (struct cs4281_state *) file->private_data;
	ssize_t ret;
	unsigned long flags;
	unsigned swptr;
	int cnt;
	unsigned copied = 0;

	CS_DBGOUT(CS_FUNCTION | CS_WAVE_READ, 2,
		  printk(KERN_INFO "cs4281: cs4281_read()+ %Zu \n", count));

	VALIDATE_STATE(s);
	if (s->dma_adc.mapped)
		return -ENXIO;
	if (!s->dma_adc.ready && (ret = prog_dmabuf_adc(s)))
		return ret;
	if (!access_ok(VERIFY_WRITE, buffer, count))
		return -EFAULT;
	ret = 0;
//
// "count" is the amount of bytes to read (from app), is decremented each loop
//      by the amount of bytes that have been returned to the user buffer.
// "cnt" is the running total of each read from the buffer (changes each loop)
// "buffer" points to the app's buffer
// "ret" keeps a running total of the amount of bytes that have been copied
//      to the user buffer.
// "copied" is the total bytes copied into the user buffer for each loop.
//
	while (count > 0) {
		CS_DBGOUT(CS_WAVE_READ, 8, printk(KERN_INFO
			"_read() count>0 count=%Zu .count=%d .swptr=%d .hwptr=%d \n",
				count, s->dma_adc.count,
				s->dma_adc.swptr, s->dma_adc.hwptr));
		spin_lock_irqsave(&s->lock, flags);

		// get the current copy point of the sw buffer
		swptr = s->dma_adc.swptr;

		// cnt is the amount of unread bytes from the end of the 
		// hw buffer to the current sw pointer
		cnt = s->dma_adc.dmasize - swptr;

		// dma_adc.count is the current total bytes that have not been read.
		// if the amount of unread bytes from the current sw pointer to the
		// end of the buffer is greater than the current total bytes that
		// have not been read, then set the "cnt" (unread bytes) to the
		// amount of unread bytes.  

		if (s->dma_adc.count < cnt)
			cnt = s->dma_adc.count;
		spin_unlock_irqrestore(&s->lock, flags);
		//
		// if we are converting from 8/16 then we need to copy
		// twice the number of 16 bit bytes then 8 bit bytes.
		// 
		if (s->conversion) {
			if (cnt > (count * 2))
				cnt = (count * 2);
		} else {
			if (cnt > count)
				cnt = count;
		}
		//
		// "cnt" NOW is the smaller of the amount that will be read,
		// and the amount that is requested in this read (or partial).
		// if there are no bytes in the buffer to read, then start the
		// ADC and wait for the interrupt handler to wake us up.
		//
		if (cnt <= 0) {

			// start up the dma engine and then continue back to the top of
			// the loop when wake up occurs.
			start_adc(s);
			if (file->f_flags & O_NONBLOCK)
				return ret ? ret : -EAGAIN;
			interruptible_sleep_on(&s->dma_adc.wait);
			if (signal_pending(current))
				return ret ? ret : -ERESTARTSYS;
			continue;
		}
		// there are bytes in the buffer to read.
		// copy from the hw buffer over to the user buffer.
		// user buffer is designated by "buffer"
		// virtual address to copy from is rawbuf+swptr
		// the "cnt" is the number of bytes to read.

		CS_DBGOUT(CS_WAVE_READ, 2, printk(KERN_INFO
			"_read() copy_to cnt=%d count=%Zu ", cnt, count));
		CS_DBGOUT(CS_WAVE_READ, 8, printk(KERN_INFO
			 " .dmasize=%d .count=%d buffer=%p ret=%Zd\n",
				 s->dma_adc.dmasize, s->dma_adc.count, buffer, ret));

		if (cs_copy_to_user
		    (s, buffer, s->dma_adc.rawbuf + swptr, cnt, &copied))
			return ret ? ret : -EFAULT;
		swptr = (swptr + cnt) % s->dma_adc.dmasize;
		spin_lock_irqsave(&s->lock, flags);
		s->dma_adc.swptr = swptr;
		s->dma_adc.count -= cnt;
		spin_unlock_irqrestore(&s->lock, flags);
		count -= copied;
		buffer += copied;
		ret += copied;
		start_adc(s);
	}
	CS_DBGOUT(CS_FUNCTION | CS_WAVE_READ, 2,
		  printk(KERN_INFO "cs4281: cs4281_read()- %Zd\n", ret));
	return ret;
}


static ssize_t cs4281_write(struct file *file, const char __user *buffer,
			    size_t count, loff_t * ppos)
{
	struct cs4281_state *s =
	    (struct cs4281_state *) file->private_data;
	ssize_t ret;
	unsigned long flags;
	unsigned swptr, hwptr, busaddr;
	int cnt;

	CS_DBGOUT(CS_FUNCTION | CS_WAVE_WRITE, 2,
		  printk(KERN_INFO "cs4281: cs4281_write()+ count=%Zu\n",
			 count));
	VALIDATE_STATE(s);

	if (s->dma_dac.mapped)
		return -ENXIO;
	if (!s->dma_dac.ready && (ret = prog_dmabuf_dac(s)))
		return ret;
	if (!access_ok(VERIFY_READ, buffer, count))
		return -EFAULT;
	ret = 0;
	while (count > 0) {
		spin_lock_irqsave(&s->lock, flags);
		if (s->dma_dac.count < 0) {
			s->dma_dac.count = 0;
			s->dma_dac.swptr = s->dma_dac.hwptr;
		}
		if (s->dma_dac.underrun) {
			s->dma_dac.underrun = 0;
			hwptr = readl(s->pBA0 + BA0_DCA0);
			busaddr = virt_to_bus(s->dma_dac.rawbuf);
			hwptr -= (unsigned) busaddr;
			s->dma_dac.swptr = s->dma_dac.hwptr = hwptr;
		}
		swptr = s->dma_dac.swptr;
		cnt = s->dma_dac.dmasize - swptr;
		if (s->dma_dac.count + cnt > s->dma_dac.dmasize)
			cnt = s->dma_dac.dmasize - s->dma_dac.count;
		spin_unlock_irqrestore(&s->lock, flags);
		if (cnt > count)
			cnt = count;
		if (cnt <= 0) {
			start_dac(s);
			if (file->f_flags & O_NONBLOCK)
				return ret ? ret : -EAGAIN;
			interruptible_sleep_on(&s->dma_dac.wait);
			if (signal_pending(current))
				return ret ? ret : -ERESTARTSYS;
			continue;
		}
		if (copy_from_user(s->dma_dac.rawbuf + swptr, buffer, cnt))
			return ret ? ret : -EFAULT;
		swptr = (swptr + cnt) % s->dma_dac.dmasize;
		spin_lock_irqsave(&s->lock, flags);
		s->dma_dac.swptr = swptr;
		s->dma_dac.count += cnt;
		s->dma_dac.endcleared = 0;
		spin_unlock_irqrestore(&s->lock, flags);
		count -= cnt;
		buffer += cnt;
		ret += cnt;
		start_dac(s);
	}
	CS_DBGOUT(CS_FUNCTION | CS_WAVE_WRITE, 2,
		  printk(KERN_INFO "cs4281: cs4281_write()- %Zd\n", ret));
	return ret;
}


static unsigned int cs4281_poll(struct file *file,
				struct poll_table_struct *wait)
{
	struct cs4281_state *s =
	    (struct cs4281_state *) file->private_data;
	unsigned long flags;
	unsigned int mask = 0;

	CS_DBGOUT(CS_FUNCTION | CS_WAVE_WRITE | CS_WAVE_READ, 4,
		  printk(KERN_INFO "cs4281: cs4281_poll()+\n"));
	VALIDATE_STATE(s);
	if (file->f_mode & FMODE_WRITE) {
		CS_DBGOUT(CS_FUNCTION | CS_WAVE_WRITE | CS_WAVE_READ, 4,
			  printk(KERN_INFO
				 "cs4281: cs4281_poll() wait on FMODE_WRITE\n"));
		if(!s->dma_dac.ready && prog_dmabuf_dac(s))
			return 0;
		poll_wait(file, &s->dma_dac.wait, wait);
	}
	if (file->f_mode & FMODE_READ) {
		CS_DBGOUT(CS_FUNCTION | CS_WAVE_WRITE | CS_WAVE_READ, 4,
			  printk(KERN_INFO
				 "cs4281: cs4281_poll() wait on FMODE_READ\n"));
		if(!s->dma_dac.ready && prog_dmabuf_adc(s))
			return 0;
		poll_wait(file, &s->dma_adc.wait, wait);
	}
	spin_lock_irqsave(&s->lock, flags);
	cs4281_update_ptr(s,CS_FALSE);
	if (file->f_mode & FMODE_WRITE) {
		if (s->dma_dac.mapped) {
			if (s->dma_dac.count >=
			    (signed) s->dma_dac.fragsize) {
				if (s->dma_dac.wakeup)
					mask |= POLLOUT | POLLWRNORM;
				else
					mask = 0;
				s->dma_dac.wakeup = 0;
			}
		} else {
			if ((signed) (s->dma_dac.dmasize/2) >= s->dma_dac.count)
				mask |= POLLOUT | POLLWRNORM;
		}
	} else if (file->f_mode & FMODE_READ) {
		if (s->dma_adc.mapped) {
			if (s->dma_adc.count >= (signed) s->dma_adc.fragsize) 
				mask |= POLLIN | POLLRDNORM;
		} else {
			if (s->dma_adc.count > 0)
				mask |= POLLIN | POLLRDNORM;
		}
	}
	spin_unlock_irqrestore(&s->lock, flags);
	CS_DBGOUT(CS_FUNCTION | CS_WAVE_WRITE | CS_WAVE_READ, 4,
		  printk(KERN_INFO "cs4281: cs4281_poll()- 0x%.8x\n",
			 mask));
	return mask;
}


static int cs4281_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct cs4281_state *s =
	    (struct cs4281_state *) file->private_data;
	struct dmabuf *db;
	int ret;
	unsigned long size;

	CS_DBGOUT(CS_FUNCTION | CS_PARMS | CS_OPEN, 4,
		  printk(KERN_INFO "cs4281: cs4281_mmap()+\n"));

	VALIDATE_STATE(s);
	if (vma->vm_flags & VM_WRITE) {
		if ((ret = prog_dmabuf_dac(s)) != 0)
			return ret;
		db = &s->dma_dac;
	} else if (vma->vm_flags & VM_READ) {
		if ((ret = prog_dmabuf_adc(s)) != 0)
			return ret;
		db = &s->dma_adc;
	} else
		return -EINVAL;
//
// only support PLAYBACK for now
//
	db = &s->dma_dac;

	if (cs4x_pgoff(vma) != 0)
		return -EINVAL;
	size = vma->vm_end - vma->vm_start;
	if (size > (PAGE_SIZE << db->buforder))
		return -EINVAL;
	if (remap_pfn_range(vma, vma->vm_start,
				virt_to_phys(db->rawbuf) >> PAGE_SHIFT,
				size, vma->vm_page_prot))
		return -EAGAIN;
	db->mapped = 1;

	CS_DBGOUT(CS_FUNCTION | CS_PARMS | CS_OPEN, 4,
		  printk(KERN_INFO "cs4281: cs4281_mmap()- 0 size=%d\n",
			 (unsigned) size));

	return 0;
}


static int cs4281_ioctl(struct inode *inode, struct file *file,
			unsigned int cmd, unsigned long arg)
{
	struct cs4281_state *s =
	    (struct cs4281_state *) file->private_data;
	unsigned long flags;
	audio_buf_info abinfo;
	count_info cinfo;
	int val, mapped, ret;
	int __user *p = (int __user *)arg;

	CS_DBGOUT(CS_FUNCTION, 4, printk(KERN_INFO
		 "cs4281: cs4281_ioctl(): file=%p cmd=0x%.8x\n", file, cmd));
#if CSDEBUG
	cs_printioctl(cmd);
#endif
	VALIDATE_STATE(s);
	mapped = ((file->f_mode & FMODE_WRITE) && s->dma_dac.mapped) ||
	    ((file->f_mode & FMODE_READ) && s->dma_adc.mapped);
	switch (cmd) {
	case OSS_GETVERSION:
		CS_DBGOUT(CS_IOCTL | CS_PARMS, 4, printk(KERN_INFO
			"cs4281: cs4281_ioctl(): SOUND_VERSION=0x%.8x\n",
				 SOUND_VERSION));
		return put_user(SOUND_VERSION, p);

	case SNDCTL_DSP_SYNC:
		CS_DBGOUT(CS_IOCTL, 4, printk(KERN_INFO
			 "cs4281: cs4281_ioctl(): DSP_SYNC\n"));
		if (file->f_mode & FMODE_WRITE)
			return drain_dac(s,
					 0 /*file->f_flags & O_NONBLOCK */
					 );
		return 0;

	case SNDCTL_DSP_SETDUPLEX:
		return 0;

	case SNDCTL_DSP_GETCAPS:
		return put_user(DSP_CAP_DUPLEX | DSP_CAP_REALTIME |
				DSP_CAP_TRIGGER | DSP_CAP_MMAP,
				p);

	case SNDCTL_DSP_RESET:
		CS_DBGOUT(CS_IOCTL, 4, printk(KERN_INFO
			 "cs4281: cs4281_ioctl(): DSP_RESET\n"));
		if (file->f_mode & FMODE_WRITE) {
			stop_dac(s);
			synchronize_irq(s->irq);
			s->dma_dac.swptr = s->dma_dac.hwptr =
			    s->dma_dac.count = s->dma_dac.total_bytes =
			    s->dma_dac.blocks = s->dma_dac.wakeup = 0;
			prog_codec(s, CS_TYPE_DAC);
		}
		if (file->f_mode & FMODE_READ) {
			stop_adc(s);
			synchronize_irq(s->irq);
			s->dma_adc.swptr = s->dma_adc.hwptr =
			    s->dma_adc.count = s->dma_adc.total_bytes =
			    s->dma_adc.blocks = s->dma_dac.wakeup = 0;
			prog_codec(s, CS_TYPE_ADC);
		}
		return 0;

	case SNDCTL_DSP_SPEED:
		if (get_user(val, p))
			return -EFAULT;
		CS_DBGOUT(CS_IOCTL | CS_PARMS, 4, printk(KERN_INFO
			 "cs4281: cs4281_ioctl(): DSP_SPEED val=%d\n", val));
		//
		// support independent capture and playback channels
		// assume that the file mode bit determines the 
		// direction of the data flow.
		//
		if (file->f_mode & FMODE_READ) {
			if (val >= 0) {
				stop_adc(s);
				s->dma_adc.ready = 0;
				// program sampling rates 
				if (val > 48000)
					val = 48000;
				if (val < 6300)
					val = 6300;
				s->prop_adc.rate = val;
				prog_codec(s, CS_TYPE_ADC);
			}
		}
		if (file->f_mode & FMODE_WRITE) {
			if (val >= 0) {
				stop_dac(s);
				s->dma_dac.ready = 0;
				// program sampling rates 
				if (val > 48000)
					val = 48000;
				if (val < 6300)
					val = 6300;
				s->prop_dac.rate = val;
				prog_codec(s, CS_TYPE_DAC);
			}
		}

		if (file->f_mode & FMODE_WRITE)
			val = s->prop_dac.rate;
		else if (file->f_mode & FMODE_READ)
			val = s->prop_adc.rate;

		return put_user(val, p);

	case SNDCTL_DSP_STEREO:
		if (get_user(val, p))
			return -EFAULT;
		CS_DBGOUT(CS_IOCTL | CS_PARMS, 4, printk(KERN_INFO
			 "cs4281: cs4281_ioctl(): DSP_STEREO val=%d\n", val));
		if (file->f_mode & FMODE_READ) {
			stop_adc(s);
			s->dma_adc.ready = 0;
			s->prop_adc.channels = val ? 2 : 1;
			prog_codec(s, CS_TYPE_ADC);
		}
		if (file->f_mode & FMODE_WRITE) {
			stop_dac(s);
			s->dma_dac.ready = 0;
			s->prop_dac.channels = val ? 2 : 1;
			prog_codec(s, CS_TYPE_DAC);
		}
		return 0;

	case SNDCTL_DSP_CHANNELS:
		if (get_user(val, p))
			return -EFAULT;
		CS_DBGOUT(CS_IOCTL | CS_PARMS, 4, printk(KERN_INFO
			 "cs4281: cs4281_ioctl(): DSP_CHANNELS val=%d\n",
				 val));
		if (val != 0) {
			if (file->f_mode & FMODE_READ) {
				stop_adc(s);
				s->dma_adc.ready = 0;
				if (val >= 2)
					s->prop_adc.channels = 2;
				else
					s->prop_adc.channels = 1;
				prog_codec(s, CS_TYPE_ADC);
			}
			if (file->f_mode & FMODE_WRITE) {
				stop_dac(s);
				s->dma_dac.ready = 0;
				if (val >= 2)
					s->prop_dac.channels = 2;
				else
					s->prop_dac.channels = 1;
				prog_codec(s, CS_TYPE_DAC);
			}
		}

		if (file->f_mode & FMODE_WRITE)
			val = s->prop_dac.channels;
		else if (file->f_mode & FMODE_READ)
			val = s->prop_adc.channels;

		return put_user(val, p);

	case SNDCTL_DSP_GETFMTS:	// Returns a mask 
		CS_DBGOUT(CS_IOCTL | CS_PARMS, 4, printk(KERN_INFO
			"cs4281: cs4281_ioctl(): DSP_GETFMT val=0x%.8x\n",
				 AFMT_S16_LE | AFMT_U16_LE | AFMT_S8 |
				 AFMT_U8));
		return put_user(AFMT_S16_LE | AFMT_U16_LE | AFMT_S8 |
				AFMT_U8, p);

	case SNDCTL_DSP_SETFMT:
		if (get_user(val, p))
			return -EFAULT;
		CS_DBGOUT(CS_IOCTL | CS_PARMS, 4, printk(KERN_INFO
			 "cs4281: cs4281_ioctl(): DSP_SETFMT val=0x%.8x\n",
				 val));
		if (val != AFMT_QUERY) {
			if (file->f_mode & FMODE_READ) {
				stop_adc(s);
				s->dma_adc.ready = 0;
				if (val != AFMT_S16_LE
				    && val != AFMT_U16_LE && val != AFMT_S8
				    && val != AFMT_U8)
					val = AFMT_U8;
				s->prop_adc.fmt = val;
				s->prop_adc.fmt_original = s->prop_adc.fmt;
				prog_codec(s, CS_TYPE_ADC);
			}
			if (file->f_mode & FMODE_WRITE) {
				stop_dac(s);
				s->dma_dac.ready = 0;
				if (val != AFMT_S16_LE
				    && val != AFMT_U16_LE && val != AFMT_S8
				    && val != AFMT_U8)
					val = AFMT_U8;
				s->prop_dac.fmt = val;
				s->prop_dac.fmt_original = s->prop_dac.fmt;
				prog_codec(s, CS_TYPE_DAC);
			}
		} else {
			if (file->f_mode & FMODE_WRITE)
				val = s->prop_dac.fmt_original;
			else if (file->f_mode & FMODE_READ)
				val = s->prop_adc.fmt_original;
		}
		CS_DBGOUT(CS_IOCTL | CS_PARMS, 4, printk(KERN_INFO
		  "cs4281: cs4281_ioctl(): DSP_SETFMT return val=0x%.8x\n", 
			val));
		return put_user(val, p);

	case SNDCTL_DSP_POST:
		CS_DBGOUT(CS_IOCTL, 4, printk(KERN_INFO
			 "cs4281: cs4281_ioctl(): DSP_POST\n"));
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
				if (!s->dma_adc.ready
				    && (ret = prog_dmabuf_adc(s)))
					return ret;
				start_adc(s);
			} else
				stop_adc(s);
		}
		if (file->f_mode & FMODE_WRITE) {
			if (val & PCM_ENABLE_OUTPUT) {
				if (!s->dma_dac.ready
				    && (ret = prog_dmabuf_dac(s)))
					return ret;
				start_dac(s);
			} else
				stop_dac(s);
		}
		return 0;

	case SNDCTL_DSP_GETOSPACE:
		if (!(file->f_mode & FMODE_WRITE))
			return -EINVAL;
		if (!s->dma_dac.ready && (val = prog_dmabuf_dac(s)))
			return val;
		spin_lock_irqsave(&s->lock, flags);
		cs4281_update_ptr(s,CS_FALSE);
		abinfo.fragsize = s->dma_dac.fragsize;
		if (s->dma_dac.mapped)
			abinfo.bytes = s->dma_dac.dmasize;
		else
			abinfo.bytes =
			    s->dma_dac.dmasize - s->dma_dac.count;
		abinfo.fragstotal = s->dma_dac.numfrag;
		abinfo.fragments = abinfo.bytes >> s->dma_dac.fragshift;
		CS_DBGOUT(CS_FUNCTION | CS_PARMS, 4, printk(KERN_INFO
			"cs4281: cs4281_ioctl(): GETOSPACE .fragsize=%d .bytes=%d .fragstotal=%d .fragments=%d\n",
				abinfo.fragsize,abinfo.bytes,abinfo.fragstotal,
				abinfo.fragments));
		spin_unlock_irqrestore(&s->lock, flags);
		return copy_to_user(p, &abinfo,
				    sizeof(abinfo)) ? -EFAULT : 0;

	case SNDCTL_DSP_GETISPACE:
		if (!(file->f_mode & FMODE_READ))
			return -EINVAL;
		if (!s->dma_adc.ready && (val = prog_dmabuf_adc(s)))
			return val;
		spin_lock_irqsave(&s->lock, flags);
		cs4281_update_ptr(s,CS_FALSE);
		if (s->conversion) {
			abinfo.fragsize = s->dma_adc.fragsize / 2;
			abinfo.bytes = s->dma_adc.count / 2;
			abinfo.fragstotal = s->dma_adc.numfrag;
			abinfo.fragments =
			    abinfo.bytes >> (s->dma_adc.fragshift - 1);
		} else {
			abinfo.fragsize = s->dma_adc.fragsize;
			abinfo.bytes = s->dma_adc.count;
			abinfo.fragstotal = s->dma_adc.numfrag;
			abinfo.fragments =
			    abinfo.bytes >> s->dma_adc.fragshift;
		}
		spin_unlock_irqrestore(&s->lock, flags);
		return copy_to_user(p, &abinfo,
				    sizeof(abinfo)) ? -EFAULT : 0;

	case SNDCTL_DSP_NONBLOCK:
		file->f_flags |= O_NONBLOCK;
		return 0;

	case SNDCTL_DSP_GETODELAY:
		if (!(file->f_mode & FMODE_WRITE))
			return -EINVAL;
		if(!s->dma_dac.ready && prog_dmabuf_dac(s))
			return 0;
		spin_lock_irqsave(&s->lock, flags);
		cs4281_update_ptr(s,CS_FALSE);
		val = s->dma_dac.count;
		spin_unlock_irqrestore(&s->lock, flags);
		return put_user(val, p);

	case SNDCTL_DSP_GETIPTR:
		if (!(file->f_mode & FMODE_READ))
			return -EINVAL;
		if(!s->dma_adc.ready && prog_dmabuf_adc(s))
			return 0;
		spin_lock_irqsave(&s->lock, flags);
		cs4281_update_ptr(s,CS_FALSE);
		cinfo.bytes = s->dma_adc.total_bytes;
		if (s->dma_adc.mapped) {
			cinfo.blocks =
			    (cinfo.bytes >> s->dma_adc.fragshift) -
			    s->dma_adc.blocks;
			s->dma_adc.blocks =
			    cinfo.bytes >> s->dma_adc.fragshift;
		} else {
			if (s->conversion) {
				cinfo.blocks =
				    s->dma_adc.count /
				    2 >> (s->dma_adc.fragshift - 1);
			} else
				cinfo.blocks =
				    s->dma_adc.count >> s->dma_adc.
				    fragshift;
		}
		if (s->conversion)
			cinfo.ptr = s->dma_adc.hwptr / 2;
		else
			cinfo.ptr = s->dma_adc.hwptr;
		if (s->dma_adc.mapped)
			s->dma_adc.count &= s->dma_adc.fragsize - 1;
		spin_unlock_irqrestore(&s->lock, flags);
		if (copy_to_user(p, &cinfo, sizeof(cinfo)))
			return -EFAULT;
		return 0;

	case SNDCTL_DSP_GETOPTR:
		if (!(file->f_mode & FMODE_WRITE))
			return -EINVAL;
		if(!s->dma_dac.ready && prog_dmabuf_dac(s))
			return 0;
		spin_lock_irqsave(&s->lock, flags);
		cs4281_update_ptr(s,CS_FALSE);
		cinfo.bytes = s->dma_dac.total_bytes;
		if (s->dma_dac.mapped) {
			cinfo.blocks =
			    (cinfo.bytes >> s->dma_dac.fragshift) -
			    s->dma_dac.blocks;
			s->dma_dac.blocks =
			    cinfo.bytes >> s->dma_dac.fragshift;
		} else {
			cinfo.blocks =
			    s->dma_dac.count >> s->dma_dac.fragshift;
		}
		cinfo.ptr = s->dma_dac.hwptr;
		if (s->dma_dac.mapped)
			s->dma_dac.count &= s->dma_dac.fragsize - 1;
		spin_unlock_irqrestore(&s->lock, flags);
		if (copy_to_user(p, &cinfo, sizeof(cinfo)))
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
		if (s->conversion)
			return put_user(s->dma_adc.fragsize / 2, p);
		else
			return put_user(s->dma_adc.fragsize, p);

	case SNDCTL_DSP_SETFRAGMENT:
		if (get_user(val, p))
			return -EFAULT;
		return 0;	// Say OK, but do nothing.

	case SNDCTL_DSP_SUBDIVIDE:
		if ((file->f_mode & FMODE_READ && s->dma_adc.subdivision)
		    || (file->f_mode & FMODE_WRITE
			&& s->dma_dac.subdivision)) return -EINVAL;
		if (get_user(val, p))
			return -EFAULT;
		if (val != 1 && val != 2 && val != 4)
			return -EINVAL;
		if (file->f_mode & FMODE_READ)
			s->dma_adc.subdivision = val;
		else if (file->f_mode & FMODE_WRITE)
			s->dma_dac.subdivision = val;
		return 0;

	case SOUND_PCM_READ_RATE:
		if (file->f_mode & FMODE_READ)
			return put_user(s->prop_adc.rate, p);
		else if (file->f_mode & FMODE_WRITE)
			return put_user(s->prop_dac.rate, p);

	case SOUND_PCM_READ_CHANNELS:
		if (file->f_mode & FMODE_READ)
			return put_user(s->prop_adc.channels, p);
		else if (file->f_mode & FMODE_WRITE)
			return put_user(s->prop_dac.channels, p);

	case SOUND_PCM_READ_BITS:
		if (file->f_mode & FMODE_READ)
			return
			    put_user(
				     (s->prop_adc.
				      fmt & (AFMT_S8 | AFMT_U8)) ? 8 : 16,
				     p);
		else if (file->f_mode & FMODE_WRITE)
			return
			    put_user(
				     (s->prop_dac.
				      fmt & (AFMT_S8 | AFMT_U8)) ? 8 : 16,
				     p);

	case SOUND_PCM_WRITE_FILTER:
	case SNDCTL_DSP_SETSYNCRO:
	case SOUND_PCM_READ_FILTER:
		return -EINVAL;
	}
	return mixer_ioctl(s, cmd, arg);
}


static int cs4281_release(struct inode *inode, struct file *file)
{
	struct cs4281_state *s =
	    (struct cs4281_state *) file->private_data;

	CS_DBGOUT(CS_FUNCTION | CS_RELEASE, 2, printk(KERN_INFO
		 "cs4281: cs4281_release(): inode=%p file=%p f_mode=%d\n",
			 inode, file, file->f_mode));

	VALIDATE_STATE(s);

	if (file->f_mode & FMODE_WRITE) {
		drain_dac(s, file->f_flags & O_NONBLOCK);
		mutex_lock(&s->open_sem_dac);
		stop_dac(s);
		dealloc_dmabuf(s, &s->dma_dac);
		s->open_mode &= ~FMODE_WRITE;
		mutex_unlock(&s->open_sem_dac);
		wake_up(&s->open_wait_dac);
	}
	if (file->f_mode & FMODE_READ) {
		drain_adc(s, file->f_flags & O_NONBLOCK);
		mutex_lock(&s->open_sem_adc);
		stop_adc(s);
		dealloc_dmabuf(s, &s->dma_adc);
		s->open_mode &= ~FMODE_READ;
		mutex_unlock(&s->open_sem_adc);
		wake_up(&s->open_wait_adc);
	}
	return 0;
}

static int cs4281_open(struct inode *inode, struct file *file)
{
	unsigned int minor = iminor(inode);
	struct cs4281_state *s=NULL;
	struct list_head *entry;

	CS_DBGOUT(CS_FUNCTION | CS_OPEN, 2, printk(KERN_INFO
		"cs4281: cs4281_open(): inode=%p file=%p f_mode=0x%x\n",
			inode, file, file->f_mode));

	list_for_each(entry, &cs4281_devs)
	{
		s = list_entry(entry, struct cs4281_state, list);

		if (!((s->dev_audio ^ minor) & ~0xf))
			break;
	}
	if (entry == &cs4281_devs)
		return -ENODEV;
	if (!s) {
		CS_DBGOUT(CS_FUNCTION | CS_OPEN, 2, printk(KERN_INFO
			"cs4281: cs4281_open(): Error - unable to find audio state struct\n"));
		return -ENODEV;
	}
	VALIDATE_STATE(s);
	file->private_data = s;

	// wait for device to become free 
	if (!(file->f_mode & (FMODE_WRITE | FMODE_READ))) {
		CS_DBGOUT(CS_FUNCTION | CS_OPEN | CS_ERROR, 2, printk(KERN_INFO
			 "cs4281: cs4281_open(): Error - must open READ and/or WRITE\n"));
		return -ENODEV;
	}
	if (file->f_mode & FMODE_WRITE) {
		mutex_lock(&s->open_sem_dac);
		while (s->open_mode & FMODE_WRITE) {
			if (file->f_flags & O_NONBLOCK) {
				mutex_unlock(&s->open_sem_dac);
				return -EBUSY;
			}
			mutex_unlock(&s->open_sem_dac);
			interruptible_sleep_on(&s->open_wait_dac);

			if (signal_pending(current))
				return -ERESTARTSYS;
			mutex_lock(&s->open_sem_dac);
		}
	}
	if (file->f_mode & FMODE_READ) {
		mutex_lock(&s->open_sem_adc);
		while (s->open_mode & FMODE_READ) {
			if (file->f_flags & O_NONBLOCK) {
				mutex_unlock(&s->open_sem_adc);
				return -EBUSY;
			}
			mutex_unlock(&s->open_sem_adc);
			interruptible_sleep_on(&s->open_wait_adc);

			if (signal_pending(current))
				return -ERESTARTSYS;
			mutex_lock(&s->open_sem_adc);
		}
	}
	s->open_mode |= file->f_mode & (FMODE_READ | FMODE_WRITE);
	if (file->f_mode & FMODE_READ) {
		s->prop_adc.fmt = AFMT_U8;
		s->prop_adc.fmt_original = s->prop_adc.fmt;
		s->prop_adc.channels = 1;
		s->prop_adc.rate = 8000;
		s->prop_adc.clkdiv = 96 | 0x80;
		s->conversion = 0;
		s->ena &= ~FMODE_READ;
		s->dma_adc.ossfragshift = s->dma_adc.ossmaxfrags =
		    s->dma_adc.subdivision = 0;
		mutex_unlock(&s->open_sem_adc);

		if (prog_dmabuf_adc(s)) {
			CS_DBGOUT(CS_OPEN | CS_ERROR, 2, printk(KERN_ERR
				"cs4281: adc Program dmabufs failed.\n"));
			cs4281_release(inode, file);
			return -ENOMEM;
		}
		prog_codec(s, CS_TYPE_ADC);
	}
	if (file->f_mode & FMODE_WRITE) {
		s->prop_dac.fmt = AFMT_U8;
		s->prop_dac.fmt_original = s->prop_dac.fmt;
		s->prop_dac.channels = 1;
		s->prop_dac.rate = 8000;
		s->prop_dac.clkdiv = 96 | 0x80;
		s->conversion = 0;
		s->ena &= ~FMODE_WRITE;
		s->dma_dac.ossfragshift = s->dma_dac.ossmaxfrags =
		    s->dma_dac.subdivision = 0;
		mutex_unlock(&s->open_sem_dac);

		if (prog_dmabuf_dac(s)) {
			CS_DBGOUT(CS_OPEN | CS_ERROR, 2, printk(KERN_ERR
				"cs4281: dac Program dmabufs failed.\n"));
			cs4281_release(inode, file);
			return -ENOMEM;
		}
		prog_codec(s, CS_TYPE_DAC);
	}
	CS_DBGOUT(CS_FUNCTION | CS_OPEN, 2,
		  printk(KERN_INFO "cs4281: cs4281_open()- 0\n"));
	return nonseekable_open(inode, file);
}


// ******************************************************************************************
//   Wave (audio) file operations struct.
// ******************************************************************************************
static /*const */ struct file_operations cs4281_audio_fops = {
	.owner	 = THIS_MODULE,
	.llseek	 = no_llseek,
	.read	 = cs4281_read,
	.write	 = cs4281_write,
	.poll	 = cs4281_poll,
	.ioctl	 = cs4281_ioctl,
	.mmap	 = cs4281_mmap,
	.open	 = cs4281_open,
	.release = cs4281_release,
};

// --------------------------------------------------------------------- 

// hold spinlock for the following! 
static void cs4281_handle_midi(struct cs4281_state *s)
{
	unsigned char ch;
	int wake;
	unsigned temp1;

	wake = 0;
	while (!(readl(s->pBA0 + BA0_MIDSR) & 0x80)) {
		ch = readl(s->pBA0 + BA0_MIDRP);
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
	while (!(readl(s->pBA0 + BA0_MIDSR) & 0x40) && s->midi.ocnt > 0) {
		temp1 = (s->midi.obuf[s->midi.ord]) & 0x000000ff;
		writel(temp1, s->pBA0 + BA0_MIDWP);
		s->midi.ord = (s->midi.ord + 1) % MIDIOUTBUF;
		s->midi.ocnt--;
		if (s->midi.ocnt < MIDIOUTBUF - 16)
			wake = 1;
	}
	if (wake)
		wake_up(&s->midi.owait);
}



static irqreturn_t cs4281_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct cs4281_state *s = (struct cs4281_state *) dev_id;
	unsigned int temp1;

	// fastpath out, to ease interrupt sharing 
	temp1 = readl(s->pBA0 + BA0_HISR);	// Get Int Status reg.

	CS_DBGOUT(CS_INTERRUPT, 6, printk(KERN_INFO
		  "cs4281: cs4281_interrupt() BA0_HISR=0x%.8x\n", temp1));
/*
* If not DMA or MIDI interrupt, then just return.
*/
	if (!(temp1 & (HISR_DMA0 | HISR_DMA1 | HISR_MIDI))) {
		writel(HICR_IEV | HICR_CHGM, s->pBA0 + BA0_HICR);
		CS_DBGOUT(CS_INTERRUPT, 9, printk(KERN_INFO
			"cs4281: cs4281_interrupt(): returning not cs4281 interrupt.\n"));
		return IRQ_NONE;
	}

	if (temp1 & HISR_DMA0)	// If play interrupt,
		readl(s->pBA0 + BA0_HDSR0);	//   clear the source.

	if (temp1 & HISR_DMA1)	// Same for play.
		readl(s->pBA0 + BA0_HDSR1);
	writel(HICR_IEV | HICR_CHGM, s->pBA0 + BA0_HICR);	// Local EOI

	spin_lock(&s->lock);
	cs4281_update_ptr(s,CS_TRUE);
	cs4281_handle_midi(s);
	spin_unlock(&s->lock);
	return IRQ_HANDLED;
}

// **************************************************************************

static void cs4281_midi_timer(unsigned long data)
{
	struct cs4281_state *s = (struct cs4281_state *) data;
	unsigned long flags;

	spin_lock_irqsave(&s->lock, flags);
	cs4281_handle_midi(s);
	spin_unlock_irqrestore(&s->lock, flags);
	s->midi.timer.expires = jiffies + 1;
	add_timer(&s->midi.timer);
}


// --------------------------------------------------------------------- 

static ssize_t cs4281_midi_read(struct file *file, char __user *buffer,
				size_t count, loff_t * ppos)
{
	struct cs4281_state *s =
	    (struct cs4281_state *) file->private_data;
	ssize_t ret;
	unsigned long flags;
	unsigned ptr;
	int cnt;

	VALIDATE_STATE(s);
	if (!access_ok(VERIFY_WRITE, buffer, count))
		return -EFAULT;
	ret = 0;
	while (count > 0) {
		spin_lock_irqsave(&s->lock, flags);
		ptr = s->midi.ird;
		cnt = MIDIINBUF - ptr;
		if (s->midi.icnt < cnt)
			cnt = s->midi.icnt;
		spin_unlock_irqrestore(&s->lock, flags);
		if (cnt > count)
			cnt = count;
		if (cnt <= 0) {
			if (file->f_flags & O_NONBLOCK)
				return ret ? ret : -EAGAIN;
			interruptible_sleep_on(&s->midi.iwait);
			if (signal_pending(current))
				return ret ? ret : -ERESTARTSYS;
			continue;
		}
		if (copy_to_user(buffer, s->midi.ibuf + ptr, cnt))
			return ret ? ret : -EFAULT;
		ptr = (ptr + cnt) % MIDIINBUF;
		spin_lock_irqsave(&s->lock, flags);
		s->midi.ird = ptr;
		s->midi.icnt -= cnt;
		spin_unlock_irqrestore(&s->lock, flags);
		count -= cnt;
		buffer += cnt;
		ret += cnt;
	}
	return ret;
}


static ssize_t cs4281_midi_write(struct file *file, const char __user *buffer,
				 size_t count, loff_t * ppos)
{
	struct cs4281_state *s =
	    (struct cs4281_state *) file->private_data;
	ssize_t ret;
	unsigned long flags;
	unsigned ptr;
	int cnt;

	VALIDATE_STATE(s);
	if (!access_ok(VERIFY_READ, buffer, count))
		return -EFAULT;
	ret = 0;
	while (count > 0) {
		spin_lock_irqsave(&s->lock, flags);
		ptr = s->midi.owr;
		cnt = MIDIOUTBUF - ptr;
		if (s->midi.ocnt + cnt > MIDIOUTBUF)
			cnt = MIDIOUTBUF - s->midi.ocnt;
		if (cnt <= 0)
			cs4281_handle_midi(s);
		spin_unlock_irqrestore(&s->lock, flags);
		if (cnt > count)
			cnt = count;
		if (cnt <= 0) {
			if (file->f_flags & O_NONBLOCK)
				return ret ? ret : -EAGAIN;
			interruptible_sleep_on(&s->midi.owait);
			if (signal_pending(current))
				return ret ? ret : -ERESTARTSYS;
			continue;
		}
		if (copy_from_user(s->midi.obuf + ptr, buffer, cnt))
			return ret ? ret : -EFAULT;
		ptr = (ptr + cnt) % MIDIOUTBUF;
		spin_lock_irqsave(&s->lock, flags);
		s->midi.owr = ptr;
		s->midi.ocnt += cnt;
		spin_unlock_irqrestore(&s->lock, flags);
		count -= cnt;
		buffer += cnt;
		ret += cnt;
		spin_lock_irqsave(&s->lock, flags);
		cs4281_handle_midi(s);
		spin_unlock_irqrestore(&s->lock, flags);
	}
	return ret;
}


static unsigned int cs4281_midi_poll(struct file *file,
				     struct poll_table_struct *wait)
{
	struct cs4281_state *s =
	    (struct cs4281_state *) file->private_data;
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


static int cs4281_midi_open(struct inode *inode, struct file *file)
{
	unsigned long flags, temp1;
	unsigned int minor = iminor(inode);
	struct cs4281_state *s=NULL;
	struct list_head *entry;
	list_for_each(entry, &cs4281_devs)
	{
		s = list_entry(entry, struct cs4281_state, list);

		if (s->dev_midi == minor)
			break;
	}

	if (entry == &cs4281_devs)
		return -ENODEV;
	if (!s)
	{
		CS_DBGOUT(CS_FUNCTION | CS_OPEN, 2, printk(KERN_INFO
			"cs4281: cs4281_open(): Error - unable to find audio state struct\n"));
		return -ENODEV;
	}
	VALIDATE_STATE(s);
	file->private_data = s;
	// wait for device to become free 
	mutex_lock(&s->open_sem);
	while (s->open_mode & (file->f_mode << FMODE_MIDI_SHIFT)) {
		if (file->f_flags & O_NONBLOCK) {
			mutex_unlock(&s->open_sem);
			return -EBUSY;
		}
		mutex_unlock(&s->open_sem);
		interruptible_sleep_on(&s->open_wait);
		if (signal_pending(current))
			return -ERESTARTSYS;
		mutex_lock(&s->open_sem);
	}
	spin_lock_irqsave(&s->lock, flags);
	if (!(s->open_mode & (FMODE_MIDI_READ | FMODE_MIDI_WRITE))) {
		s->midi.ird = s->midi.iwr = s->midi.icnt = 0;
		s->midi.ord = s->midi.owr = s->midi.ocnt = 0;
		writel(1, s->pBA0 + BA0_MIDCR);	// Reset the interface.
		writel(0, s->pBA0 + BA0_MIDCR);	// Return to normal mode.
		s->midi.ird = s->midi.iwr = s->midi.icnt = 0;
		writel(0x0000000f, s->pBA0 + BA0_MIDCR);	// Enable transmit, record, ints.
		temp1 = readl(s->pBA0 + BA0_HIMR);
		writel(temp1 & 0xffbfffff, s->pBA0 + BA0_HIMR);	// Enable midi int. recognition.
		writel(HICR_IEV | HICR_CHGM, s->pBA0 + BA0_HICR);	// Enable interrupts
		init_timer(&s->midi.timer);
		s->midi.timer.expires = jiffies + 1;
		s->midi.timer.data = (unsigned long) s;
		s->midi.timer.function = cs4281_midi_timer;
		add_timer(&s->midi.timer);
	}
	if (file->f_mode & FMODE_READ) {
		s->midi.ird = s->midi.iwr = s->midi.icnt = 0;
	}
	if (file->f_mode & FMODE_WRITE) {
		s->midi.ord = s->midi.owr = s->midi.ocnt = 0;
	}
	spin_unlock_irqrestore(&s->lock, flags);
	s->open_mode |=
	    (file->
	     f_mode << FMODE_MIDI_SHIFT) & (FMODE_MIDI_READ |
					    FMODE_MIDI_WRITE);
	mutex_unlock(&s->open_sem);
	return nonseekable_open(inode, file);
}


static int cs4281_midi_release(struct inode *inode, struct file *file)
{
	struct cs4281_state *s =
	    (struct cs4281_state *) file->private_data;
	DECLARE_WAITQUEUE(wait, current);
	unsigned long flags;
	unsigned count, tmo;

	VALIDATE_STATE(s);

	if (file->f_mode & FMODE_WRITE) {
		add_wait_queue(&s->midi.owait, &wait);
		for (;;) {
			set_current_state(TASK_INTERRUPTIBLE);
			spin_lock_irqsave(&s->lock, flags);
			count = s->midi.ocnt;
			spin_unlock_irqrestore(&s->lock, flags);
			if (count <= 0)
				break;
			if (signal_pending(current))
				break;
			if (file->f_flags & O_NONBLOCK) {
				remove_wait_queue(&s->midi.owait, &wait);
				current->state = TASK_RUNNING;
				return -EBUSY;
			}
			tmo = (count * HZ) / 3100;
			if (!schedule_timeout(tmo ? : 1) && tmo)
				printk(KERN_DEBUG
				       "cs4281: midi timed out??\n");
		}
		remove_wait_queue(&s->midi.owait, &wait);
		current->state = TASK_RUNNING;
	}
	mutex_lock(&s->open_sem);
	s->open_mode &=
	    (~(file->f_mode << FMODE_MIDI_SHIFT)) & (FMODE_MIDI_READ |
						     FMODE_MIDI_WRITE);
	spin_lock_irqsave(&s->lock, flags);
	if (!(s->open_mode & (FMODE_MIDI_READ | FMODE_MIDI_WRITE))) {
		writel(0, s->pBA0 + BA0_MIDCR);	// Disable Midi interrupts.  
		del_timer(&s->midi.timer);
	}
	spin_unlock_irqrestore(&s->lock, flags);
	mutex_unlock(&s->open_sem);
	wake_up(&s->open_wait);
	return 0;
}

// ******************************************************************************************
//   Midi file operations struct.
// ******************************************************************************************
static /*const */ struct file_operations cs4281_midi_fops = {
	.owner	 = THIS_MODULE,
	.llseek	 = no_llseek,
	.read	 = cs4281_midi_read,
	.write	 = cs4281_midi_write,
	.poll	 = cs4281_midi_poll,
	.open	 = cs4281_midi_open,
	.release = cs4281_midi_release,
};


// --------------------------------------------------------------------- 

// maximum number of devices 
#define NR_DEVICE 8		// Only eight devices supported currently.

// --------------------------------------------------------------------- 

static struct initvol {
	int mixch;
	int vol;
} initvol[] __devinitdata = {

	{
	SOUND_MIXER_WRITE_VOLUME, 0x4040}, {
	SOUND_MIXER_WRITE_PCM, 0x4040}, {
	SOUND_MIXER_WRITE_SYNTH, 0x4040}, {
	SOUND_MIXER_WRITE_CD, 0x4040}, {
	SOUND_MIXER_WRITE_LINE, 0x4040}, {
	SOUND_MIXER_WRITE_LINE1, 0x4040}, {
	SOUND_MIXER_WRITE_RECLEV, 0x0000}, {
	SOUND_MIXER_WRITE_SPEAKER, 0x4040}, {
	SOUND_MIXER_WRITE_MIC, 0x0000}
};


#ifndef NOT_CS4281_PM
static void __devinit cs4281_BuildFIFO(
	struct cs4281_pipeline *p, 
	struct cs4281_state *s)
{
	switch(p->number)
	{
		case 0:  /* playback */
		{
			p->u32FCRnAddress  =  BA0_FCR0;
			p->u32FSICnAddress = BA0_FSIC0;
			p->u32FPDRnAddress = BA0_FPDR0;
			break;
		}
		case 1:  /* capture */
		{
			p->u32FCRnAddress  =  BA0_FCR1;
			p->u32FSICnAddress = BA0_FSIC1;
			p->u32FPDRnAddress = BA0_FPDR1;
			break;
		}

		case 2: 
		{
			p->u32FCRnAddress  =  BA0_FCR2;
			p->u32FSICnAddress = BA0_FSIC2;
			p->u32FPDRnAddress = BA0_FPDR2;
			break;
		}
		case 3: 
		{
			p->u32FCRnAddress  =  BA0_FCR3;
			p->u32FSICnAddress = BA0_FSIC3;
			p->u32FPDRnAddress = BA0_FPDR3;
			break;
		}
		default:
			break;
	}
	//
	// first read the hardware to initialize the member variables
	//
	p->u32FCRnValue = readl(s->pBA0 + p->u32FCRnAddress);
	p->u32FSICnValue = readl(s->pBA0 + p->u32FSICnAddress);
	p->u32FPDRnValue = readl(s->pBA0 + p->u32FPDRnAddress);

}

static void __devinit cs4281_BuildDMAengine(
	struct cs4281_pipeline *p, 
	struct cs4281_state *s)
{
/*
* initialize all the addresses of this pipeline dma info.
*/
	switch(p->number)
	{
		case 0:  /* playback */
		{
			p->u32DBAnAddress = BA0_DBA0;
			p->u32DCAnAddress = BA0_DCA0;
			p->u32DBCnAddress = BA0_DBC0;
			p->u32DCCnAddress = BA0_DCC0;
			p->u32DMRnAddress = BA0_DMR0;
			p->u32DCRnAddress = BA0_DCR0;
			p->u32HDSRnAddress = BA0_HDSR0;
			break;
		}

		case 1: /* capture */
		{
			p->u32DBAnAddress = BA0_DBA1;
			p->u32DCAnAddress = BA0_DCA1;
			p->u32DBCnAddress = BA0_DBC1;
			p->u32DCCnAddress = BA0_DCC1;
			p->u32DMRnAddress = BA0_DMR1;
			p->u32DCRnAddress = BA0_DCR1;
			p->u32HDSRnAddress = BA0_HDSR1;
			break;
		}

		case 2:
		{
			p->u32DBAnAddress = BA0_DBA2;
			p->u32DCAnAddress = BA0_DCA2;
			p->u32DBCnAddress = BA0_DBC2;
			p->u32DCCnAddress = BA0_DCC2;
			p->u32DMRnAddress = BA0_DMR2;
			p->u32DCRnAddress = BA0_DCR2;
			p->u32HDSRnAddress = BA0_HDSR2;
			break;
		}

		case 3:
		{
			p->u32DBAnAddress = BA0_DBA3;
			p->u32DCAnAddress = BA0_DCA3;
			p->u32DBCnAddress = BA0_DBC3;
			p->u32DCCnAddress = BA0_DCC3;
			p->u32DMRnAddress = BA0_DMR3;
			p->u32DCRnAddress = BA0_DCR3;
			p->u32HDSRnAddress = BA0_HDSR3;
			break;
		}
		default:
			break;
	}

//
// Initialize the dma values for this pipeline
//
	p->u32DBAnValue = readl(s->pBA0 + p->u32DBAnAddress);
	p->u32DBCnValue = readl(s->pBA0 + p->u32DBCnAddress);
	p->u32DMRnValue = readl(s->pBA0 + p->u32DMRnAddress);
	p->u32DCRnValue = readl(s->pBA0 + p->u32DCRnAddress);

}

static void __devinit cs4281_InitPM(struct cs4281_state *s)
{
	int i;
	struct cs4281_pipeline *p;

	for(i=0;i<CS4281_NUMBER_OF_PIPELINES;i++)
	{
		p = &s->pl[i];
		p->number = i;
		cs4281_BuildDMAengine(p,s);
		cs4281_BuildFIFO(p,s);
	/*
	* currently only  2 pipelines are used
	* so, only set the valid bit on the playback and capture.
	*/
		if( (i == CS4281_PLAYBACK_PIPELINE_NUMBER) || 
			(i == CS4281_CAPTURE_PIPELINE_NUMBER))
			p->flags |= CS4281_PIPELINE_VALID;
	}
	s->pm.u32SSPM_BITS = 0x7e;  /* rev c, use 0x7c for rev a or b */
}
#endif

static int __devinit cs4281_probe(struct pci_dev *pcidev,
				  const struct pci_device_id *pciid)
{
	struct cs4281_state *s;
	dma_addr_t dma_mask;
	mm_segment_t fs;
	int i, val;
	unsigned int temp1, temp2;

	CS_DBGOUT(CS_FUNCTION | CS_INIT, 2,
		  printk(KERN_INFO "cs4281: probe()+\n"));

	if (pci_enable_device(pcidev)) {
		CS_DBGOUT(CS_INIT | CS_ERROR, 1, printk(KERN_ERR
			 "cs4281: pci_enable_device() failed\n"));
		return -1;
	}
	if (!(pci_resource_flags(pcidev, 0) & IORESOURCE_MEM) ||
	    !(pci_resource_flags(pcidev, 1) & IORESOURCE_MEM)) {
		CS_DBGOUT(CS_ERROR, 1, printk(KERN_ERR
			 "cs4281: probe()- Memory region not assigned\n"));
		return -ENODEV;
	}
	if (pcidev->irq == 0) {
		CS_DBGOUT(CS_ERROR, 1, printk(KERN_ERR
			 "cs4281: probe() IRQ not assigned\n"));
		return -ENODEV;
	}
	dma_mask = 0xffffffff;	/* this enables playback and recording */
	i = pci_set_dma_mask(pcidev, dma_mask);
	if (i) {
		CS_DBGOUT(CS_ERROR, 1, printk(KERN_ERR
		      "cs4281: probe() architecture does not support 32bit PCI busmaster DMA\n"));
		return i;
	}
	if (!(s = kmalloc(sizeof(struct cs4281_state), GFP_KERNEL))) {
		CS_DBGOUT(CS_ERROR, 1, printk(KERN_ERR
		      "cs4281: probe() no memory for state struct.\n"));
		return -1;
	}
	memset(s, 0, sizeof(struct cs4281_state));
	init_waitqueue_head(&s->dma_adc.wait);
	init_waitqueue_head(&s->dma_dac.wait);
	init_waitqueue_head(&s->open_wait);
	init_waitqueue_head(&s->open_wait_adc);
	init_waitqueue_head(&s->open_wait_dac);
	init_waitqueue_head(&s->midi.iwait);
	init_waitqueue_head(&s->midi.owait);
	mutex_init(&s->open_sem);
	mutex_init(&s->open_sem_adc);
	mutex_init(&s->open_sem_dac);
	spin_lock_init(&s->lock);
	s->pBA0phys = pci_resource_start(pcidev, 0);
	s->pBA1phys = pci_resource_start(pcidev, 1);

	/* Convert phys to linear. */
	s->pBA0 = ioremap_nocache(s->pBA0phys, 4096);
	if (!s->pBA0) {
		CS_DBGOUT(CS_ERROR | CS_INIT, 1, printk(KERN_ERR
			 "cs4281: BA0 I/O mapping failed. Skipping part.\n"));
		goto err_free;
	}
	s->pBA1 = ioremap_nocache(s->pBA1phys, 65536);
	if (!s->pBA1) {
		CS_DBGOUT(CS_ERROR | CS_INIT, 1, printk(KERN_ERR
			 "cs4281: BA1 I/O mapping failed. Skipping part.\n"));
		goto err_unmap;
	}

	temp1 = readl(s->pBA0 + BA0_PCICFG00);
	temp2 = readl(s->pBA0 + BA0_PCICFG04);

	CS_DBGOUT(CS_INIT, 2,
		  printk(KERN_INFO
			 "cs4281: probe() BA0=0x%.8x BA1=0x%.8x pBA0=%p pBA1=%p \n",
			 (unsigned) temp1, (unsigned) temp2, s->pBA0, s->pBA1));
	CS_DBGOUT(CS_INIT, 2,
		  printk(KERN_INFO
			 "cs4281: probe() pBA0phys=0x%.8x pBA1phys=0x%.8x\n",
			 (unsigned) s->pBA0phys, (unsigned) s->pBA1phys));

#ifndef NOT_CS4281_PM
	s->pm.flags = CS4281_PM_IDLE;
#endif
	temp1 = cs4281_hw_init(s);
	if (temp1) {
		CS_DBGOUT(CS_ERROR | CS_INIT, 1, printk(KERN_ERR
			 "cs4281: cs4281_hw_init() failed. Skipping part.\n"));
		goto err_irq;
	}
	s->magic = CS4281_MAGIC;
	s->pcidev = pcidev;
	s->irq = pcidev->irq;
	if (request_irq
	    (s->irq, cs4281_interrupt, SA_SHIRQ, "Crystal CS4281", s)) {
		CS_DBGOUT(CS_INIT | CS_ERROR, 1,
			  printk(KERN_ERR "cs4281: irq %u in use\n", s->irq));
		goto err_irq;
	}
	if ((s->dev_audio = register_sound_dsp(&cs4281_audio_fops, -1)) <
	    0) {
		CS_DBGOUT(CS_INIT | CS_ERROR, 1, printk(KERN_ERR
			 "cs4281: probe() register_sound_dsp() failed.\n"));
		goto err_dev1;
	}
	if ((s->dev_mixer = register_sound_mixer(&cs4281_mixer_fops, -1)) <
	    0) {
		CS_DBGOUT(CS_INIT | CS_ERROR, 1, printk(KERN_ERR
			 "cs4281: probe() register_sound_mixer() failed.\n"));
		goto err_dev2;
	}
	if ((s->dev_midi = register_sound_midi(&cs4281_midi_fops, -1)) < 0) {
		CS_DBGOUT(CS_INIT | CS_ERROR, 1, printk(KERN_ERR
			 "cs4281: probe() register_sound_midi() failed.\n"));
		goto err_dev3;
	}
#ifndef NOT_CS4281_PM
	cs4281_InitPM(s);
	s->pm.flags |= CS4281_PM_NOT_REGISTERED;
#endif

	pci_set_master(pcidev);	// enable bus mastering 

	fs = get_fs();
	set_fs(KERNEL_DS);
	val = SOUND_MASK_LINE;
	mixer_ioctl(s, SOUND_MIXER_WRITE_RECSRC, (unsigned long) &val);
	for (i = 0; i < sizeof(initvol) / sizeof(initvol[0]); i++) {
		val = initvol[i].vol;
		mixer_ioctl(s, initvol[i].mixch, (unsigned long) &val);
	}
	val = 1;		// enable mic preamp 
	mixer_ioctl(s, SOUND_MIXER_PRIVATE1, (unsigned long) &val);
	set_fs(fs);

	pci_set_drvdata(pcidev, s);
	list_add(&s->list, &cs4281_devs);
	CS_DBGOUT(CS_INIT | CS_FUNCTION, 2, printk(KERN_INFO
		"cs4281: probe()- device allocated successfully\n"));
	return 0;

      err_dev3:
	unregister_sound_mixer(s->dev_mixer);
      err_dev2:
	unregister_sound_dsp(s->dev_audio);
      err_dev1:
	free_irq(s->irq, s);
      err_irq:
	iounmap(s->pBA1);
      err_unmap:
	iounmap(s->pBA0);
      err_free:
	kfree(s);

	CS_DBGOUT(CS_INIT | CS_ERROR, 1, printk(KERN_INFO
		"cs4281: probe()- no device allocated\n"));
	return -ENODEV;
} // probe_cs4281


// --------------------------------------------------------------------- 

static void __devexit cs4281_remove(struct pci_dev *pci_dev)
{
	struct cs4281_state *s = pci_get_drvdata(pci_dev);
	// stop DMA controller 
	synchronize_irq(s->irq);
	free_irq(s->irq, s);
	unregister_sound_dsp(s->dev_audio);
	unregister_sound_mixer(s->dev_mixer);
	unregister_sound_midi(s->dev_midi);
	iounmap(s->pBA1);
	iounmap(s->pBA0);
	pci_set_drvdata(pci_dev,NULL);
	list_del(&s->list);
	kfree(s);
	CS_DBGOUT(CS_INIT | CS_FUNCTION, 2, printk(KERN_INFO
		 "cs4281: cs4281_remove()-: remove successful\n"));
}

static struct pci_device_id cs4281_pci_tbl[] = {
	{
		.vendor    = PCI_VENDOR_ID_CIRRUS,
		.device    = PCI_DEVICE_ID_CRYSTAL_CS4281,
		.subvendor = PCI_ANY_ID,
		.subdevice = PCI_ANY_ID,
	},
	{ 0, },
};

MODULE_DEVICE_TABLE(pci, cs4281_pci_tbl);

static struct pci_driver cs4281_pci_driver = {
	.name	  = "cs4281",
	.id_table = cs4281_pci_tbl,
	.probe	  = cs4281_probe,
	.remove	  = __devexit_p(cs4281_remove),
	.suspend  = CS4281_SUSPEND_TBL,
	.resume	  = CS4281_RESUME_TBL,
};

static int __init cs4281_init_module(void)
{
	int rtn = 0;
	CS_DBGOUT(CS_INIT | CS_FUNCTION, 2, printk(KERN_INFO 
		"cs4281: cs4281_init_module()+ \n"));
	printk(KERN_INFO "cs4281: version v%d.%02d.%d time " __TIME__ " "
	       __DATE__ "\n", CS4281_MAJOR_VERSION, CS4281_MINOR_VERSION,
	       CS4281_ARCH);
	rtn = pci_register_driver(&cs4281_pci_driver);

	CS_DBGOUT(CS_INIT | CS_FUNCTION, 2,
		  printk(KERN_INFO "cs4281: cs4281_init_module()- (%d)\n",rtn));
	return rtn;
}

static void __exit cs4281_cleanup_module(void)
{
	pci_unregister_driver(&cs4281_pci_driver);
	CS_DBGOUT(CS_INIT | CS_FUNCTION, 2,
		  printk(KERN_INFO "cs4281: cleanup_cs4281() finished\n"));
}
// --------------------------------------------------------------------- 

MODULE_AUTHOR("gw boynton, audio@crystal.cirrus.com");
MODULE_DESCRIPTION("Cirrus Logic CS4281 Driver");
MODULE_LICENSE("GPL");

// --------------------------------------------------------------------- 

module_init(cs4281_init_module);
module_exit(cs4281_cleanup_module);

