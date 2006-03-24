/*******************************************************************************
*
*      "swarm_cs4297a.c" --  Cirrus Logic-Crystal CS4297a linux audio driver.
*
*      Copyright (C) 2001  Broadcom Corporation.
*      Copyright (C) 2000,2001  Cirrus Logic Corp.  
*            -- adapted from drivers by Thomas Sailer, 
*            -- but don't bug him; Problems should go to:
*            -- tom woller (twoller@crystal.cirrus.com) or
*               (audio@crystal.cirrus.com).
*            -- adapted from cs4281 PCI driver for cs4297a on
*               BCM1250 Synchronous Serial interface
*               (Kip Walker, Broadcom Corp.)
*      Copyright (C) 2004  Maciej W. Rozycki
*      Copyright (C) 2005 Ralf Baechle (ralf@linux-mips.org)
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
* 11/10/00 bkz - added __devinit to cs4297a_hw_init()
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

#include <linux/list.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/sound.h>
#include <linux/slab.h>
#include <linux/soundcard.h>
#include <linux/ac97_codec.h>
#include <linux/pci.h>
#include <linux/bitops.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/smp_lock.h>
#include <linux/mutex.h>

#include <asm/byteorder.h>
#include <asm/dma.h>
#include <asm/io.h>
#include <asm/uaccess.h>

#include <asm/sibyte/sb1250_regs.h>
#include <asm/sibyte/sb1250_int.h>
#include <asm/sibyte/sb1250_dma.h>
#include <asm/sibyte/sb1250_scd.h>
#include <asm/sibyte/sb1250_syncser.h>
#include <asm/sibyte/sb1250_mac.h>
#include <asm/sibyte/sb1250.h>

struct cs4297a_state;

static void stop_dac(struct cs4297a_state *s);
static void stop_adc(struct cs4297a_state *s);
static void start_dac(struct cs4297a_state *s);
static void start_adc(struct cs4297a_state *s);
#undef OSS_DOCUMENTED_MIXER_SEMANTICS

// --------------------------------------------------------------------- 

#define CS4297a_MAGIC           0xf00beef1

// buffer order determines the size of the dma buffer for the driver.
// under Linux, a smaller buffer allows more responsiveness from many of the 
// applications (e.g. games).  A larger buffer allows some of the apps (esound) 
// to not underrun the dma buffer as easily.  As default, use 32k (order=3)
// rather than 64k as some of the games work more responsively.
// log base 2( buff sz = 32k).

//static unsigned long defaultorder = 3;
//MODULE_PARM(defaultorder, "i");

//
// Turn on/off debugging compilation by commenting out "#define CSDEBUG"
//
#define CSDEBUG 0
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
#define CS_AC97         0x00000040      // AC97 register access
#define CS_DESCR        0x00000080      // descriptor management
#define CS_OPEN		0x00000400	// all open functions in the driver
#define CS_RELEASE	0x00000800	// all release functions in the driver
#define CS_PARMS	0x00001000	// functional and operational parameters
#define CS_IOCTL	0x00002000	// ioctl (non-mixer)
#define CS_TMP		0x10000000	// tmp debug mask bit

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
static unsigned long cs_debuglevel = 4;	// levels range from 1-9
static unsigned long cs_debugmask = CS_INIT /*| CS_IOCTL*/;
MODULE_PARM(cs_debuglevel, "i");
MODULE_PARM(cs_debugmask, "i");
#endif
#define CS_TRUE 	1
#define CS_FALSE 	0

#define CS_TYPE_ADC 0
#define CS_TYPE_DAC 1

#define SER_BASE    (A_SER_BASE_1 + KSEG1)
#define SS_CSR(t)   (SER_BASE+t)
#define SS_TXTBL(t) (SER_BASE+R_SER_TX_TABLE_BASE+(t*8))
#define SS_RXTBL(t) (SER_BASE+R_SER_RX_TABLE_BASE+(t*8))

#define FRAME_BYTES            32
#define FRAME_SAMPLE_BYTES      4

/* Should this be variable? */
#define SAMPLE_BUF_SIZE        (16*1024)
#define SAMPLE_FRAME_COUNT     (SAMPLE_BUF_SIZE / FRAME_SAMPLE_BYTES)
/* The driver can explode/shrink the frames to/from a smaller sample
   buffer */
#define DMA_BLOAT_FACTOR       1
#define DMA_DESCR              (SAMPLE_FRAME_COUNT / DMA_BLOAT_FACTOR)
#define DMA_BUF_SIZE           (DMA_DESCR * FRAME_BYTES)

/* Use the maxmium count (255 == 5.1 ms between interrupts) */
#define DMA_INT_CNT            ((1 << S_DMA_INT_PKTCNT) - 1)

/* Figure this out: how many TX DMAs ahead to schedule a reg access */
#define REG_LATENCY            150

#define FRAME_TX_US             20

#define SERDMA_NEXTBUF(d,f) (((d)->f+1) % (d)->ringsz)

static const char invalid_magic[] =
    KERN_CRIT "cs4297a: invalid magic value\n";

#define VALIDATE_STATE(s)                          \
({                                                 \
        if (!(s) || (s)->magic != CS4297a_MAGIC) { \
                printk(invalid_magic);             \
                return -ENXIO;                     \
        }                                          \
})

struct list_head cs4297a_devs = { &cs4297a_devs, &cs4297a_devs };

typedef struct serdma_descr_s {
        u64 descr_a;
        u64 descr_b;
} serdma_descr_t;

typedef unsigned long paddr_t;

typedef struct serdma_s {
        unsigned         ringsz;
        serdma_descr_t  *descrtab;
        serdma_descr_t  *descrtab_end;
        paddr_t          descrtab_phys;
        
        serdma_descr_t  *descr_add;
        serdma_descr_t  *descr_rem;
        
        u64  *dma_buf;           // buffer for DMA contents (frames)
        paddr_t          dma_buf_phys;
        u16  *sample_buf;		// tmp buffer for sample conversions
        u16  *sb_swptr;
        u16  *sb_hwptr;
        u16  *sb_end;

        dma_addr_t dmaaddr;
//        unsigned buforder;	// Log base 2 of 'dma_buf' size in bytes..
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
        wait_queue_head_t reg_wait;
        // redundant, but makes calculations easier 
        unsigned fragsize;	// 2**fragshift..
        unsigned sbufsz;	// 2**buforder.
        unsigned fragsamples;
        // OSS stuff 
        unsigned mapped:1;	// Buffer mapped in cs4297a_mmap()?
        unsigned ready:1;	// prog_dmabuf_dac()/adc() successful?
        unsigned endcleared:1;
        unsigned type:1;	// adc or dac buffer (CS_TYPE_XXX)
        unsigned ossfragshift;
        int ossmaxfrags;
        unsigned subdivision;
} serdma_t;

struct cs4297a_state {
	// magic 
	unsigned int magic;

	struct list_head list;

	// soundcore stuff 
	int dev_audio;
	int dev_mixer;

	// hardware resources 
	unsigned int irq;

        struct {
                unsigned int rx_ovrrn; /* FIFO */
                unsigned int rx_overflow; /* staging buffer */
                unsigned int tx_underrun;
                unsigned int rx_bad;
                unsigned int rx_good;
        } stats;

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
	} prop_dac, prop_adc;
	unsigned conversion:1;	// conversion from 16 to 8 bit in progress
	unsigned ena;
	spinlock_t lock;
	struct mutex open_mutex;
	struct mutex open_sem_adc;
	struct mutex open_sem_dac;
	mode_t open_mode;
	wait_queue_head_t open_wait;
	wait_queue_head_t open_wait_adc;
	wait_queue_head_t open_wait_dac;

	dma_addr_t dmaaddr_sample_buf;
	unsigned buforder_sample_buf;	// Log base 2 of 'dma_buf' size in bytes..

        serdma_t dma_dac, dma_adc;

        volatile u16 read_value;
        volatile u16 read_reg;
        volatile u64 reg_request;
};

#if 1
#define prog_codec(a,b)
#define dealloc_dmabuf(a,b);
#endif

static int prog_dmabuf_adc(struct cs4297a_state *s)
{
	s->dma_adc.ready = 1;
	return 0;
}


static int prog_dmabuf_dac(struct cs4297a_state *s)
{
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
		"cs4297a: clear_advance(): memset %d at 0x%.8x for %d size \n",
			(unsigned)c, (unsigned)((char *) buf) + bptr, len));
	memset(((char *) buf) + bptr, c, len);
}

#if CSDEBUG

// DEBUG ROUTINES

#define SOUND_MIXER_CS_GETDBGLEVEL 	_SIOWR('M',120, int)
#define SOUND_MIXER_CS_SETDBGLEVEL 	_SIOWR('M',121, int)
#define SOUND_MIXER_CS_GETDBGMASK 	_SIOWR('M',122, int)
#define SOUND_MIXER_CS_SETDBGMASK 	_SIOWR('M',123, int)

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


static int ser_init(struct cs4297a_state *s)
{
        int i;

        CS_DBGOUT(CS_INIT, 2, 
                  printk(KERN_INFO "cs4297a: Setting up serial parameters\n"));

        __raw_writeq(M_SYNCSER_CMD_RX_RESET | M_SYNCSER_CMD_TX_RESET, SS_CSR(R_SER_CMD));

        __raw_writeq(M_SYNCSER_MSB_FIRST, SS_CSR(R_SER_MODE));
        __raw_writeq(32, SS_CSR(R_SER_MINFRM_SZ));
        __raw_writeq(32, SS_CSR(R_SER_MAXFRM_SZ));

        __raw_writeq(1, SS_CSR(R_SER_TX_RD_THRSH));
        __raw_writeq(4, SS_CSR(R_SER_TX_WR_THRSH));
        __raw_writeq(8, SS_CSR(R_SER_RX_RD_THRSH));

        /* This looks good from experimentation */
        __raw_writeq((M_SYNCSER_TXSYNC_INT | V_SYNCSER_TXSYNC_DLY(0) | M_SYNCSER_TXCLK_EXT |
               M_SYNCSER_RXSYNC_INT | V_SYNCSER_RXSYNC_DLY(1) | M_SYNCSER_RXCLK_EXT | M_SYNCSER_RXSYNC_EDGE),
              SS_CSR(R_SER_LINE_MODE));

        /* This looks good from experimentation */
        __raw_writeq(V_SYNCSER_SEQ_COUNT(14) | M_SYNCSER_SEQ_ENABLE | M_SYNCSER_SEQ_STROBE,
              SS_TXTBL(0));
        __raw_writeq(V_SYNCSER_SEQ_COUNT(15) | M_SYNCSER_SEQ_ENABLE | M_SYNCSER_SEQ_BYTE,
              SS_TXTBL(1));
        __raw_writeq(V_SYNCSER_SEQ_COUNT(13) | M_SYNCSER_SEQ_ENABLE | M_SYNCSER_SEQ_BYTE,
              SS_TXTBL(2));
        __raw_writeq(V_SYNCSER_SEQ_COUNT( 0) | M_SYNCSER_SEQ_ENABLE |
              M_SYNCSER_SEQ_STROBE | M_SYNCSER_SEQ_LAST, SS_TXTBL(3));

        __raw_writeq(V_SYNCSER_SEQ_COUNT(14) | M_SYNCSER_SEQ_ENABLE | M_SYNCSER_SEQ_STROBE,
              SS_RXTBL(0));
        __raw_writeq(V_SYNCSER_SEQ_COUNT(15) | M_SYNCSER_SEQ_ENABLE | M_SYNCSER_SEQ_BYTE,
              SS_RXTBL(1));
        __raw_writeq(V_SYNCSER_SEQ_COUNT(13) | M_SYNCSER_SEQ_ENABLE | M_SYNCSER_SEQ_BYTE,
              SS_RXTBL(2));
        __raw_writeq(V_SYNCSER_SEQ_COUNT( 0) | M_SYNCSER_SEQ_ENABLE | M_SYNCSER_SEQ_STROBE |
              M_SYNCSER_SEQ_LAST, SS_RXTBL(3));

        for (i=4; i<16; i++) {
                /* Just in case... */
                __raw_writeq(M_SYNCSER_SEQ_LAST, SS_TXTBL(i));
                __raw_writeq(M_SYNCSER_SEQ_LAST, SS_RXTBL(i));
        }

        return 0;
}

static int init_serdma(serdma_t *dma)
{
        CS_DBGOUT(CS_INIT, 2,
                  printk(KERN_ERR "cs4297a: desc - %d sbufsize - %d dbufsize - %d\n",
                         DMA_DESCR, SAMPLE_BUF_SIZE, DMA_BUF_SIZE));

        /* Descriptors */
        dma->ringsz = DMA_DESCR;
        dma->descrtab = kmalloc(dma->ringsz * sizeof(serdma_descr_t), GFP_KERNEL);
        if (!dma->descrtab) {
                printk(KERN_ERR "cs4297a: kmalloc descrtab failed\n");
                return -1;
        }
        memset(dma->descrtab, 0, dma->ringsz * sizeof(serdma_descr_t));
        dma->descrtab_end = dma->descrtab + dma->ringsz;
	/* XXX bloddy mess, use proper DMA API here ...  */
	dma->descrtab_phys = CPHYSADDR((long)dma->descrtab);
        dma->descr_add = dma->descr_rem = dma->descrtab;

        /* Frame buffer area */
        dma->dma_buf = kmalloc(DMA_BUF_SIZE, GFP_KERNEL);
        if (!dma->dma_buf) {
                printk(KERN_ERR "cs4297a: kmalloc dma_buf failed\n");
                kfree(dma->descrtab);
                return -1;
        }
        memset(dma->dma_buf, 0, DMA_BUF_SIZE);
        dma->dma_buf_phys = CPHYSADDR((long)dma->dma_buf);

        /* Samples buffer area */
        dma->sbufsz = SAMPLE_BUF_SIZE;
        dma->sample_buf = kmalloc(dma->sbufsz, GFP_KERNEL);
        if (!dma->sample_buf) {
                printk(KERN_ERR "cs4297a: kmalloc sample_buf failed\n");
                kfree(dma->descrtab);
                kfree(dma->dma_buf);
                return -1;
        }
        dma->sb_swptr = dma->sb_hwptr = dma->sample_buf;
        dma->sb_end = (u16 *)((void *)dma->sample_buf + dma->sbufsz);
        dma->fragsize = dma->sbufsz >> 1;

        CS_DBGOUT(CS_INIT, 4, 
                  printk(KERN_ERR "cs4297a: descrtab - %08x dma_buf - %x sample_buf - %x\n",
                         (int)dma->descrtab, (int)dma->dma_buf, 
                         (int)dma->sample_buf));

        return 0;
}

static int dma_init(struct cs4297a_state *s)
{
        int i;

        CS_DBGOUT(CS_INIT, 2, 
                  printk(KERN_INFO "cs4297a: Setting up DMA\n"));

        if (init_serdma(&s->dma_adc) ||
            init_serdma(&s->dma_dac))
                return -1;

        if (__raw_readq(SS_CSR(R_SER_DMA_DSCR_COUNT_RX))||
            __raw_readq(SS_CSR(R_SER_DMA_DSCR_COUNT_TX))) {
                panic("DMA state corrupted?!");
        }

        /* Initialize now - the descr/buffer pairings will never
           change... */
        for (i=0; i<DMA_DESCR; i++) {
                s->dma_dac.descrtab[i].descr_a = M_DMA_SERRX_SOP | V_DMA_DSCRA_A_SIZE(1) | 
                        (s->dma_dac.dma_buf_phys + i*FRAME_BYTES);
                s->dma_dac.descrtab[i].descr_b = V_DMA_DSCRB_PKT_SIZE(FRAME_BYTES);
                s->dma_adc.descrtab[i].descr_a = V_DMA_DSCRA_A_SIZE(1) |
                        (s->dma_adc.dma_buf_phys + i*FRAME_BYTES);
                s->dma_adc.descrtab[i].descr_b = 0;
        }

        __raw_writeq((M_DMA_EOP_INT_EN | V_DMA_INT_PKTCNT(DMA_INT_CNT) |
               V_DMA_RINGSZ(DMA_DESCR) | M_DMA_TDX_EN),
              SS_CSR(R_SER_DMA_CONFIG0_RX));
        __raw_writeq(M_DMA_L2CA, SS_CSR(R_SER_DMA_CONFIG1_RX));
        __raw_writeq(s->dma_adc.descrtab_phys, SS_CSR(R_SER_DMA_DSCR_BASE_RX));

        __raw_writeq(V_DMA_RINGSZ(DMA_DESCR), SS_CSR(R_SER_DMA_CONFIG0_TX));
        __raw_writeq(M_DMA_L2CA | M_DMA_NO_DSCR_UPDT, SS_CSR(R_SER_DMA_CONFIG1_TX));
        __raw_writeq(s->dma_dac.descrtab_phys, SS_CSR(R_SER_DMA_DSCR_BASE_TX));

        /* Prep the receive DMA descriptor ring */
        __raw_writeq(DMA_DESCR, SS_CSR(R_SER_DMA_DSCR_COUNT_RX));

        __raw_writeq(M_SYNCSER_DMA_RX_EN | M_SYNCSER_DMA_TX_EN, SS_CSR(R_SER_DMA_ENABLE));

        __raw_writeq((M_SYNCSER_RX_SYNC_ERR | M_SYNCSER_RX_OVERRUN | M_SYNCSER_RX_EOP_COUNT),
              SS_CSR(R_SER_INT_MASK));

        /* Enable the rx/tx; let the codec warm up to the sync and
           start sending good frames before the receive FIFO is
           enabled */
        __raw_writeq(M_SYNCSER_CMD_TX_EN, SS_CSR(R_SER_CMD));
        udelay(1000);
        __raw_writeq(M_SYNCSER_CMD_RX_EN | M_SYNCSER_CMD_TX_EN, SS_CSR(R_SER_CMD));

        /* XXXKW is this magic? (the "1" part) */
        while ((__raw_readq(SS_CSR(R_SER_STATUS)) & 0xf1) != 1)
                ;

        CS_DBGOUT(CS_INIT, 4, 
                  printk(KERN_INFO "cs4297a: status: %08x\n",
                         (unsigned int)(__raw_readq(SS_CSR(R_SER_STATUS)) & 0xffffffff)));

        return 0;
}

static int serdma_reg_access(struct cs4297a_state *s, u64 data)
{
        serdma_t *d = &s->dma_dac;
        u64 *data_p;
        unsigned swptr;
        int flags;
        serdma_descr_t *descr;

        if (s->reg_request) {
                printk(KERN_ERR "cs4297a: attempt to issue multiple reg_access\n");
                return -1;
        }

        if (s->ena & FMODE_WRITE) {
                /* Since a writer has the DSP open, we have to mux the
                   request in */
                s->reg_request = data;
                interruptible_sleep_on(&s->dma_dac.reg_wait);
                /* XXXKW how can I deal with the starvation case where
                   the opener isn't writing? */
        } else {
                /* Be safe when changing ring pointers */
		spin_lock_irqsave(&s->lock, flags);
                if (d->hwptr != d->swptr) {
                        printk(KERN_ERR "cs4297a: reg access found bookkeeping error (hw/sw = %d/%d\n",
                               d->hwptr, d->swptr);
                        spin_unlock_irqrestore(&s->lock, flags);
                        return -1;
                }
                swptr = d->swptr;
                d->hwptr = d->swptr = (d->swptr + 1) % d->ringsz;
		spin_unlock_irqrestore(&s->lock, flags);

                descr = &d->descrtab[swptr];
                data_p = &d->dma_buf[swptr * 4];
		*data_p = cpu_to_be64(data);
                __raw_writeq(1, SS_CSR(R_SER_DMA_DSCR_COUNT_TX));
                CS_DBGOUT(CS_DESCR, 4,
                          printk(KERN_INFO "cs4297a: add_tx  %p (%x -> %x)\n",
                                 data_p, swptr, d->hwptr));
        }

        CS_DBGOUT(CS_FUNCTION, 6,
                  printk(KERN_INFO "cs4297a: serdma_reg_access()-\n"));
        
        return 0;
}

//****************************************************************************
// "cs4297a_read_ac97" -- Reads an AC97 register
//****************************************************************************
static int cs4297a_read_ac97(struct cs4297a_state *s, u32 offset,
			    u32 * value)
{
        CS_DBGOUT(CS_AC97, 1,
                  printk(KERN_INFO "cs4297a: read reg %2x\n", offset));
        if (serdma_reg_access(s, (0xCLL << 60) | (1LL << 47) | ((u64)(offset & 0x7F) << 40)))
                return -1;

        interruptible_sleep_on(&s->dma_adc.reg_wait);
        *value = s->read_value;
        CS_DBGOUT(CS_AC97, 2,
                  printk(KERN_INFO "cs4297a: rdr reg %x -> %x\n", s->read_reg, s->read_value));

        return 0;
}


//****************************************************************************
// "cs4297a_write_ac97()"-- writes an AC97 register
//****************************************************************************
static int cs4297a_write_ac97(struct cs4297a_state *s, u32 offset,
			     u32 value)
{
        CS_DBGOUT(CS_AC97, 1,
                  printk(KERN_INFO "cs4297a: write reg %2x -> %04x\n", offset, value));
        return (serdma_reg_access(s, (0xELL << 60) | ((u64)(offset & 0x7F) << 40) | ((value & 0xffff) << 12)));
}

static void stop_dac(struct cs4297a_state *s)
{
	unsigned long flags;

	CS_DBGOUT(CS_WAVE_WRITE, 3, printk(KERN_INFO "cs4297a: stop_dac():\n"));
	spin_lock_irqsave(&s->lock, flags);
	s->ena &= ~FMODE_WRITE;
#if 0
        /* XXXKW what do I really want here?  My theory for now is
           that I just flip the "ena" bit, and the interrupt handler
           will stop processing the xmit channel */
        __raw_writeq((s->ena & FMODE_READ) ? M_SYNCSER_DMA_RX_EN : 0,
              SS_CSR(R_SER_DMA_ENABLE));
#endif

	spin_unlock_irqrestore(&s->lock, flags);
}


static void start_dac(struct cs4297a_state *s)
{
	unsigned long flags;

	CS_DBGOUT(CS_FUNCTION, 3, printk(KERN_INFO "cs4297a: start_dac()+\n"));
	spin_lock_irqsave(&s->lock, flags);
	if (!(s->ena & FMODE_WRITE) && (s->dma_dac.mapped ||
					(s->dma_dac.count > 0
	    				&& s->dma_dac.ready))) {
		s->ena |= FMODE_WRITE;
                /* XXXKW what do I really want here?  My theory for
                   now is that I just flip the "ena" bit, and the
                   interrupt handler will start processing the xmit
                   channel */

		CS_DBGOUT(CS_WAVE_WRITE | CS_PARMS, 8, printk(KERN_INFO
			"cs4297a: start_dac(): start dma\n"));

	}
	spin_unlock_irqrestore(&s->lock, flags);
	CS_DBGOUT(CS_FUNCTION, 3,
		  printk(KERN_INFO "cs4297a: start_dac()-\n"));
}


static void stop_adc(struct cs4297a_state *s)
{
	unsigned long flags;

	CS_DBGOUT(CS_FUNCTION, 3,
		  printk(KERN_INFO "cs4297a: stop_adc()+\n"));

	spin_lock_irqsave(&s->lock, flags);
	s->ena &= ~FMODE_READ;

	if (s->conversion == 1) {
		s->conversion = 0;
		s->prop_adc.fmt = s->prop_adc.fmt_original;
	}
        /* Nothing to do really, I need to keep the DMA going
           XXXKW when do I get here, and is there more I should do? */
	spin_unlock_irqrestore(&s->lock, flags);
	CS_DBGOUT(CS_FUNCTION, 3,
		  printk(KERN_INFO "cs4297a: stop_adc()-\n"));
}


static void start_adc(struct cs4297a_state *s)
{
	unsigned long flags;

	CS_DBGOUT(CS_FUNCTION, 2,
		  printk(KERN_INFO "cs4297a: start_adc()+\n"));

	if (!(s->ena & FMODE_READ) &&
	    (s->dma_adc.mapped || s->dma_adc.count <=
	     (signed) (s->dma_adc.sbufsz - 2 * s->dma_adc.fragsize))
	    && s->dma_adc.ready) {
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

                        prog_dmabuf_adc(s);
			s->conversion = 1;
		}
		spin_lock_irqsave(&s->lock, flags);
		s->ena |= FMODE_READ;
                /* Nothing to do really, I am probably already
                   DMAing...  XXXKW when do I get here, and is there
                   more I should do? */
		spin_unlock_irqrestore(&s->lock, flags);

		CS_DBGOUT(CS_PARMS, 6, printk(KERN_INFO
			 "cs4297a: start_adc(): start adc\n"));
	}
	CS_DBGOUT(CS_FUNCTION, 2,
		  printk(KERN_INFO "cs4297a: start_adc()-\n"));

}


// call with spinlock held! 
static void cs4297a_update_ptr(struct cs4297a_state *s, int intflag)
{
	int good_diff, diff, diff2;
        u64 *data_p, data;
        u32 *s_ptr;
	unsigned hwptr;
        u32 status;
        serdma_t *d;
        serdma_descr_t *descr;

	// update ADC pointer 
        status = intflag ? __raw_readq(SS_CSR(R_SER_STATUS)) : 0;

	if ((s->ena & FMODE_READ) || (status & (M_SYNCSER_RX_EOP_COUNT))) {
                d = &s->dma_adc;
                hwptr = (unsigned) (((__raw_readq(SS_CSR(R_SER_DMA_CUR_DSCR_ADDR_RX)) & M_DMA_CURDSCR_ADDR) -
                                     d->descrtab_phys) / sizeof(serdma_descr_t));

                if (s->ena & FMODE_READ) {
                        CS_DBGOUT(CS_FUNCTION, 2, 
                                  printk(KERN_INFO "cs4297a: upd_rcv sw->hw->hw %x/%x/%x (int-%d)n",
                                         d->swptr, d->hwptr, hwptr, intflag));
                        /* Number of DMA buffers available for software: */
                        diff2 = diff = (d->ringsz + hwptr - d->hwptr) % d->ringsz;
                        d->hwptr = hwptr;
                        good_diff = 0;
                        s_ptr = (u32 *)&(d->dma_buf[d->swptr*4]);
                        descr = &d->descrtab[d->swptr];
                        while (diff2--) {
				u64 data = be64_to_cpu(*(u64 *)s_ptr);
                                u64 descr_a;
                                u16 left, right;
                                descr_a = descr->descr_a;
                                descr->descr_a &= ~M_DMA_SERRX_SOP;
                                if ((descr_a & M_DMA_DSCRA_A_ADDR) != CPHYSADDR((long)s_ptr)) {
                                        printk(KERN_ERR "cs4297a: RX Bad address (read)\n");
                                }
                                if (((data & 0x9800000000000000) != 0x9800000000000000) ||
                                    (!(descr_a & M_DMA_SERRX_SOP)) ||
                                    (G_DMA_DSCRB_PKT_SIZE(descr->descr_b) != FRAME_BYTES)) {
                                        s->stats.rx_bad++;
                                        printk(KERN_DEBUG "cs4297a: RX Bad attributes (read)\n");
                                        continue;
                                }
                                s->stats.rx_good++;
                                if ((data >> 61) == 7) {
                                        s->read_value = (data >> 12) & 0xffff;
                                        s->read_reg = (data >> 40) & 0x7f;
                                        wake_up(&d->reg_wait);
                                }
                                if (d->count && (d->sb_hwptr == d->sb_swptr)) {
                                        s->stats.rx_overflow++;
                                        printk(KERN_DEBUG "cs4297a: RX overflow\n");
                                        continue;
                                }
                                good_diff++;
				left = ((be32_to_cpu(s_ptr[1]) & 0xff) << 8) |
				       ((be32_to_cpu(s_ptr[2]) >> 24) & 0xff);
				right = (be32_to_cpu(s_ptr[2]) >> 4) & 0xffff;
				*d->sb_hwptr++ = cpu_to_be16(left);
				*d->sb_hwptr++ = cpu_to_be16(right);
                                if (d->sb_hwptr == d->sb_end)
                                        d->sb_hwptr = d->sample_buf;
                                descr++;
                                if (descr == d->descrtab_end) {
                                        descr = d->descrtab;
                                        s_ptr = (u32 *)s->dma_adc.dma_buf;
                                } else {
                                        s_ptr += 8;
                                }
                        }
                        d->total_bytes += good_diff * FRAME_SAMPLE_BYTES;
                        d->count += good_diff * FRAME_SAMPLE_BYTES;
                        if (d->count > d->sbufsz) {
                                printk(KERN_ERR "cs4297a: bogus receive overflow!!\n");
                        }
                        d->swptr = (d->swptr + diff) % d->ringsz;
                        __raw_writeq(diff, SS_CSR(R_SER_DMA_DSCR_COUNT_RX));
                        if (d->mapped) {
                                if (d->count >= (signed) d->fragsize)
                                        wake_up(&d->wait);
                        } else {
                                if (d->count > 0) {
                                        CS_DBGOUT(CS_WAVE_READ, 4,
                                                  printk(KERN_INFO
                                                         "cs4297a: update count -> %d\n", d->count));
                                        wake_up(&d->wait);
                                }
                        }
                } else {
                        /* Receive is going even if no one is
                           listening (for register accesses and to
                           avoid FIFO overrun) */
                        diff2 = diff = (hwptr + d->ringsz - d->hwptr) % d->ringsz;
                        if (!diff) {
                                printk(KERN_ERR "cs4297a: RX full or empty?\n");
                        }
                        
                        descr = &d->descrtab[d->swptr];
                        data_p = &d->dma_buf[d->swptr*4];

                        /* Force this to happen at least once; I got
                           here because of an interrupt, so there must
                           be a buffer to process. */
                        do {
				data = be64_to_cpu(*data_p);
                                if ((descr->descr_a & M_DMA_DSCRA_A_ADDR) != CPHYSADDR((long)data_p)) {
                                        printk(KERN_ERR "cs4297a: RX Bad address %d (%llx %lx)\n", d->swptr,
                                               (long long)(descr->descr_a & M_DMA_DSCRA_A_ADDR),
                                               (long)CPHYSADDR((long)data_p));
                                }
                                if (!(data & (1LL << 63)) ||
                                    !(descr->descr_a & M_DMA_SERRX_SOP) ||
                                    (G_DMA_DSCRB_PKT_SIZE(descr->descr_b) != FRAME_BYTES)) {
                                        s->stats.rx_bad++;
                                        printk(KERN_DEBUG "cs4297a: RX Bad attributes\n");
                                } else {
                                        s->stats.rx_good++;
                                        if ((data >> 61) == 7) {
                                                s->read_value = (data >> 12) & 0xffff;
                                                s->read_reg = (data >> 40) & 0x7f;
                                                wake_up(&d->reg_wait);
                                        }
                                }
                                descr->descr_a &= ~M_DMA_SERRX_SOP;
                                descr++;
                                d->swptr++;
                                data_p += 4;
                                if (descr == d->descrtab_end) {
                                        descr = d->descrtab;
                                        d->swptr = 0;
                                        data_p = d->dma_buf;
                                }
                                __raw_writeq(1, SS_CSR(R_SER_DMA_DSCR_COUNT_RX));
                        } while (--diff);
                        d->hwptr = hwptr;

                        CS_DBGOUT(CS_DESCR, 6, 
                                  printk(KERN_INFO "cs4297a: hw/sw %x/%x\n", d->hwptr, d->swptr));
                }

		CS_DBGOUT(CS_PARMS, 8, printk(KERN_INFO
			"cs4297a: cs4297a_update_ptr(): s=0x%.8x hwptr=%d total_bytes=%d count=%d \n",
				(unsigned)s, d->hwptr, 
				d->total_bytes, d->count));
	}

        /* XXXKW worry about s->reg_request -- there is a starvation
           case if s->ena has FMODE_WRITE on, but the client isn't
           doing writes */

	// update DAC pointer 
	//
	// check for end of buffer, means that we are going to wait for another interrupt
	// to allow silence to fill the fifos on the part, to keep pops down to a minimum.
	//
	if (s->ena & FMODE_WRITE) {
                serdma_t *d = &s->dma_dac;
                hwptr = (unsigned) (((__raw_readq(SS_CSR(R_SER_DMA_CUR_DSCR_ADDR_TX)) & M_DMA_CURDSCR_ADDR) -
                                     d->descrtab_phys) / sizeof(serdma_descr_t));
                diff = (d->ringsz + hwptr - d->hwptr) % d->ringsz;
                CS_DBGOUT(CS_WAVE_WRITE, 4, printk(KERN_INFO
                                                   "cs4297a: cs4297a_update_ptr(): hw/hw/sw %x/%x/%x diff %d count %d\n",
                                                   d->hwptr, hwptr, d->swptr, diff, d->count));
                d->hwptr = hwptr;
                /* XXXKW stereo? conversion? Just assume 2 16-bit samples for now */
                d->total_bytes += diff * FRAME_SAMPLE_BYTES;
		if (d->mapped) {
			d->count += diff * FRAME_SAMPLE_BYTES;
			if (d->count >= d->fragsize) {
				d->wakeup = 1;
				wake_up(&d->wait);
				if (d->count > d->sbufsz)
					d->count &= d->sbufsz - 1;
			}
		} else {
			d->count -= diff * FRAME_SAMPLE_BYTES;
			if (d->count <= 0) {
				//
				// fill with silence, and do not shut down the DAC.
				// Continue to play silence until the _release.
				//
				CS_DBGOUT(CS_WAVE_WRITE, 6, printk(KERN_INFO
					"cs4297a: cs4297a_update_ptr(): memset %d at 0x%.8x for %d size \n",
						(unsigned)(s->prop_dac.fmt & 
						(AFMT_U8 | AFMT_U16_LE)) ? 0x80 : 0, 
						(unsigned)d->dma_buf, 
						d->ringsz));
				memset(d->dma_buf, 0, d->ringsz * FRAME_BYTES);
				if (d->count < 0) {
					d->underrun = 1;
                                        s->stats.tx_underrun++;
					d->count = 0;
					CS_DBGOUT(CS_ERROR, 9, printk(KERN_INFO
					 "cs4297a: cs4297a_update_ptr(): underrun\n"));
				}
			} else if (d->count <=
				   (signed) d->fragsize
				   && !d->endcleared) {
                          /* XXXKW what is this for? */
				clear_advance(d->dma_buf,
					      d->sbufsz,
					      d->swptr,
					      d->fragsize,
					      0);
				d->endcleared = 1;
			}
			if ( (d->count <= (signed) d->sbufsz/2) || intflag)
			{
                                CS_DBGOUT(CS_WAVE_WRITE, 4,
                                          printk(KERN_INFO
                                                 "cs4297a: update count -> %d\n", d->count));
				wake_up(&d->wait);
			}
		}
		CS_DBGOUT(CS_PARMS, 8, printk(KERN_INFO
			"cs4297a: cs4297a_update_ptr(): s=0x%.8x hwptr=%d total_bytes=%d count=%d \n",
				(unsigned) s, d->hwptr, 
				d->total_bytes, d->count));
	}
}

static int mixer_ioctl(struct cs4297a_state *s, unsigned int cmd,
		       unsigned long arg)
{
	// Index to mixer_src[] is value of AC97 Input Mux Select Reg.
	// Value of array member is recording source Device ID Mask.
	static const unsigned int mixer_src[8] = {
		SOUND_MASK_MIC, SOUND_MASK_CD, 0, SOUND_MASK_LINE1,
		SOUND_MASK_LINE, SOUND_MASK_VOLUME, 0, 0
	};

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
		AC97_PCMOUT_VOL,
		AC97_AUX_VOL,
		AC97_CD_VOL,
		AC97_LINEIN_VOL
	};
	unsigned char l, r, rl, rr, vidx;
	unsigned char attentbl[11] =
	    { 63, 42, 26, 17, 14, 11, 8, 6, 4, 2, 0 };
	unsigned temp1;
	int i, val;

	VALIDATE_STATE(s);
	CS_DBGOUT(CS_FUNCTION, 4, printk(KERN_INFO
		 "cs4297a: mixer_ioctl(): s=0x%.8x cmd=0x%.8x\n",
			 (unsigned) s, cmd));
#if CSDEBUG
	cs_printioctl(cmd);
#endif
#if CSDEBUG_INTERFACE

	if ((cmd == SOUND_MIXER_CS_GETDBGMASK) ||
	    (cmd == SOUND_MIXER_CS_SETDBGMASK) ||
	    (cmd == SOUND_MIXER_CS_GETDBGLEVEL) ||
	    (cmd == SOUND_MIXER_CS_SETDBGLEVEL))
	{
		switch (cmd) {

		case SOUND_MIXER_CS_GETDBGMASK:
			return put_user(cs_debugmask,
					(unsigned long *) arg);

		case SOUND_MIXER_CS_GETDBGLEVEL:
			return put_user(cs_debuglevel,
					(unsigned long *) arg);

		case SOUND_MIXER_CS_SETDBGMASK:
			if (get_user(val, (unsigned long *) arg))
				return -EFAULT;
			cs_debugmask = val;
			return 0;

		case SOUND_MIXER_CS_SETDBGLEVEL:
			if (get_user(val, (unsigned long *) arg))
				return -EFAULT;
			cs_debuglevel = val;
			return 0;
		default:
			CS_DBGOUT(CS_ERROR, 1, printk(KERN_INFO
				"cs4297a: mixer_ioctl(): ERROR unknown debug cmd\n"));
			return 0;
		}
	}
#endif

	if (cmd == SOUND_MIXER_PRIVATE1) {
                return -EINVAL;
	}
	if (cmd == SOUND_MIXER_PRIVATE2) {
		// enable/disable/query spatializer 
		if (get_user(val, (int *) arg))
			return -EFAULT;
		if (val != -1) {
			temp1 = (val & 0x3f) >> 2;
			cs4297a_write_ac97(s, AC97_3D_CONTROL, temp1);
			cs4297a_read_ac97(s, AC97_GENERAL_PURPOSE,
					 &temp1);
			cs4297a_write_ac97(s, AC97_GENERAL_PURPOSE,
					  temp1 | 0x2000);
		}
		cs4297a_read_ac97(s, AC97_3D_CONTROL, &temp1);
		return put_user((temp1 << 2) | 3, (int *) arg);
	}
	if (cmd == SOUND_MIXER_INFO) {
		mixer_info info;
		memset(&info, 0, sizeof(info));
		strlcpy(info.id, "CS4297a", sizeof(info.id));
		strlcpy(info.name, "Crystal CS4297a", sizeof(info.name));
		info.modify_counter = s->mix.modcnt;
		if (copy_to_user((void *) arg, &info, sizeof(info)))
			return -EFAULT;
		return 0;
	}
	if (cmd == SOUND_OLD_MIXER_INFO) {
		_old_mixer_info info;
		memset(&info, 0, sizeof(info));
		strlcpy(info.id, "CS4297a", sizeof(info.id));
		strlcpy(info.name, "Crystal CS4297a", sizeof(info.name));
		if (copy_to_user((void *) arg, &info, sizeof(info)))
			return -EFAULT;
		return 0;
	}
	if (cmd == OSS_GETVERSION)
		return put_user(SOUND_VERSION, (int *) arg);

	if (_IOC_TYPE(cmd) != 'M' || _SIOC_SIZE(cmd) != sizeof(int))
		return -EINVAL;

	// If ioctl has only the SIOC_READ bit(bit 31)
	// on, process the only-read commands. 
	if (_SIOC_DIR(cmd) == _SIOC_READ) {
		switch (_IOC_NR(cmd)) {
		case SOUND_MIXER_RECSRC:	// Arg contains a bit for each recording source 
			cs4297a_read_ac97(s, AC97_RECORD_SELECT,
					 &temp1);
			return put_user(mixer_src[temp1 & 7], (int *) arg);

		case SOUND_MIXER_DEVMASK:	// Arg contains a bit for each supported device 
			return put_user(SOUND_MASK_PCM | SOUND_MASK_LINE |
					SOUND_MASK_VOLUME | SOUND_MASK_RECLEV,
                                        (int *) arg);

		case SOUND_MIXER_RECMASK:	// Arg contains a bit for each supported recording source 
			return put_user(SOUND_MASK_LINE | SOUND_MASK_VOLUME,
                                        (int *) arg);

		case SOUND_MIXER_STEREODEVS:	// Mixer channels supporting stereo 
			return put_user(SOUND_MASK_PCM | SOUND_MASK_LINE |
					SOUND_MASK_VOLUME | SOUND_MASK_RECLEV,
                                        (int *) arg);

		case SOUND_MIXER_CAPS:
			return put_user(SOUND_CAP_EXCL_INPUT, (int *) arg);

		default:
			i = _IOC_NR(cmd);
			if (i >= SOUND_MIXER_NRDEVICES
			    || !(vidx = mixtable1[i]))
				return -EINVAL;
			return put_user(s->mix.vol[vidx - 1], (int *) arg);
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
		if (get_user(val, (int *) arg))
			return -EFAULT;
		i = hweight32(val);	// i = # bits on in val.
		if (i != 1)	// One & only 1 bit must be on.
			return 0;
		for (i = 0; i < sizeof(mixer_src) / sizeof(int); i++) {
			if (val == mixer_src[i]) {
				temp1 = (i << 8) | i;
				cs4297a_write_ac97(s,
						  AC97_RECORD_SELECT,
						  temp1);
				return 0;
			}
		}
		return 0;

	case SOUND_MIXER_VOLUME:
		if (get_user(val, (int *) arg))
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

		cs4297a_write_ac97(s, AC97_MASTER_VOL_STEREO, temp1);
		cs4297a_write_ac97(s, AC97_PHONE_VOL, temp1);

#ifdef OSS_DOCUMENTED_MIXER_SEMANTICS
		s->mix.vol[8] = ((unsigned int) r << 8) | l;
#else
		s->mix.vol[8] = val;
#endif
		return put_user(s->mix.vol[8], (int *) arg);

	case SOUND_MIXER_SPEAKER:
		if (get_user(val, (int *) arg))
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
		cs4297a_write_ac97(s, AC97_PCBEEP_VOL, temp1);

#ifdef OSS_DOCUMENTED_MIXER_SEMANTICS
		s->mix.vol[6] = l << 8;
#else
		s->mix.vol[6] = val;
#endif
		return put_user(s->mix.vol[6], (int *) arg);

	case SOUND_MIXER_RECLEV:
		if (get_user(val, (int *) arg))
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
		cs4297a_write_ac97(s, AC97_RECORD_GAIN, temp1);

#ifdef OSS_DOCUMENTED_MIXER_SEMANTICS
		s->mix.vol[7] = ((unsigned int) r << 8) | l;
#else
		s->mix.vol[7] = val;
#endif
		return put_user(s->mix.vol[7], (int *) arg);

	case SOUND_MIXER_MIC:
		if (get_user(val, (int *) arg))
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
		cs4297a_read_ac97(s, AC97_MIC_VOL, &temp1);
		temp1 &= 0x40;	// Isolate 20db gain bit.
		if (rl < 3) {
			temp1 |= 0x8000;
			rl = 0;
		}
		rl = 31 - rl;	// Convert volume to attenuation.
		temp1 |= rl;
		cs4297a_write_ac97(s, AC97_MIC_VOL, temp1);

#ifdef OSS_DOCUMENTED_MIXER_SEMANTICS
		s->mix.vol[5] = val << 8;
#else
		s->mix.vol[5] = val;
#endif
		return put_user(s->mix.vol[5], (int *) arg);


	case SOUND_MIXER_SYNTH:
		if (get_user(val, (int *) arg))
			return -EFAULT;
		l = val & 0xff;
		if (l > 100)
			l = 100;
		if (get_user(val, (int *) arg))
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
//		writel(temp1 | rl, s->pBA0 + FMLVC);
		if (rr < 3)	//  If rr is low, turn on
			temp1 = 0x0080;	//   the mute bit.
		else
			temp1 = 0;
		rr = 63 - rr;	// Convert vol to attenuation.
//		writel(temp1 | rr, s->pBA0 + FMRVC);

#ifdef OSS_DOCUMENTED_MIXER_SEMANTICS
		s->mix.vol[4] = (r << 8) | l;
#else
		s->mix.vol[4] = val;
#endif
		return put_user(s->mix.vol[4], (int *) arg);


	default:
		CS_DBGOUT(CS_IOCTL, 4, printk(KERN_INFO
			"cs4297a: mixer_ioctl(): default\n"));

		i = _IOC_NR(cmd);
		if (i >= SOUND_MIXER_NRDEVICES || !(vidx = mixtable1[i]))
			return -EINVAL;
		if (get_user(val, (int *) arg))
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
		cs4297a_write_ac97(s, mixreg[vidx - 1], temp1);

#ifdef OSS_DOCUMENTED_MIXER_SEMANTICS
		s->mix.vol[vidx - 1] = ((unsigned int) r << 8) | l;
#else
		s->mix.vol[vidx - 1] = val;
#endif
		return put_user(s->mix.vol[vidx - 1], (int *) arg);
	}
}


// --------------------------------------------------------------------- 

static int cs4297a_open_mixdev(struct inode *inode, struct file *file)
{
	int minor = iminor(inode);
	struct cs4297a_state *s=NULL;
	struct list_head *entry;

	CS_DBGOUT(CS_FUNCTION | CS_OPEN, 4,
		  printk(KERN_INFO "cs4297a: cs4297a_open_mixdev()+\n"));

	list_for_each(entry, &cs4297a_devs)
	{
		s = list_entry(entry, struct cs4297a_state, list);
		if(s->dev_mixer == minor)
			break;
	}
	if (!s)
	{
		CS_DBGOUT(CS_FUNCTION | CS_OPEN | CS_ERROR, 2,
			printk(KERN_INFO "cs4297a: cs4297a_open_mixdev()- -ENODEV\n"));
		return -ENODEV;
	}
	VALIDATE_STATE(s);
	file->private_data = s;

	CS_DBGOUT(CS_FUNCTION | CS_OPEN, 4,
		  printk(KERN_INFO "cs4297a: cs4297a_open_mixdev()- 0\n"));

	return nonseekable_open(inode, file);
}


static int cs4297a_release_mixdev(struct inode *inode, struct file *file)
{
	struct cs4297a_state *s =
	    (struct cs4297a_state *) file->private_data;

	VALIDATE_STATE(s);
	return 0;
}


static int cs4297a_ioctl_mixdev(struct inode *inode, struct file *file,
			       unsigned int cmd, unsigned long arg)
{
	return mixer_ioctl((struct cs4297a_state *) file->private_data, cmd,
			   arg);
}


// ******************************************************************************************
//   Mixer file operations struct.
// ******************************************************************************************
static /*const */ struct file_operations cs4297a_mixer_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.ioctl		= cs4297a_ioctl_mixdev,
	.open		= cs4297a_open_mixdev,
	.release	= cs4297a_release_mixdev,
};

// --------------------------------------------------------------------- 


static int drain_adc(struct cs4297a_state *s, int nonblock)
{
        /* This routine serves no purpose currently - any samples
           sitting in the receive queue will just be processed by the
           background consumer.  This would be different if DMA
           actually stopped when there were no clients. */
	return 0;
}

static int drain_dac(struct cs4297a_state *s, int nonblock)
{
	DECLARE_WAITQUEUE(wait, current);
	unsigned long flags;
        unsigned hwptr;
	unsigned tmo;
	int count;

	if (s->dma_dac.mapped)
		return 0;
        if (nonblock)
                return -EBUSY;
	add_wait_queue(&s->dma_dac.wait, &wait);
        while ((count = __raw_readq(SS_CSR(R_SER_DMA_DSCR_COUNT_TX))) ||
               (s->dma_dac.count > 0)) {
                if (!signal_pending(current)) {
                        set_current_state(TASK_INTERRUPTIBLE);
                        /* XXXKW is this calculation working? */
                        tmo = ((count * FRAME_TX_US) * HZ) / 1000000;
                        schedule_timeout(tmo + 1);
                } else {
                        /* XXXKW do I care if there is a signal pending? */
                }
        }
        spin_lock_irqsave(&s->lock, flags);
        /* Reset the bookkeeping */
        hwptr = (int)(((__raw_readq(SS_CSR(R_SER_DMA_CUR_DSCR_ADDR_TX)) & M_DMA_CURDSCR_ADDR) -
                       s->dma_dac.descrtab_phys) / sizeof(serdma_descr_t));
        s->dma_dac.hwptr = s->dma_dac.swptr = hwptr;
        spin_unlock_irqrestore(&s->lock, flags);
	remove_wait_queue(&s->dma_dac.wait, &wait);
	current->state = TASK_RUNNING;
	return 0;
}


// --------------------------------------------------------------------- 

static ssize_t cs4297a_read(struct file *file, char *buffer, size_t count,
			   loff_t * ppos)
{
	struct cs4297a_state *s =
	    (struct cs4297a_state *) file->private_data;
	ssize_t ret;
	unsigned long flags;
	int cnt, count_fr, cnt_by;
	unsigned copied = 0;

	CS_DBGOUT(CS_FUNCTION | CS_WAVE_READ, 2,
		  printk(KERN_INFO "cs4297a: cs4297a_read()+ %d \n", count));

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
			"_read() count>0 count=%d .count=%d .swptr=%d .hwptr=%d \n",
				count, s->dma_adc.count,
				s->dma_adc.swptr, s->dma_adc.hwptr));
		spin_lock_irqsave(&s->lock, flags);

                /* cnt will be the number of available samples (16-bit
                   stereo); it starts out as the maxmimum consequetive
                   samples */
		cnt = (s->dma_adc.sb_end - s->dma_adc.sb_swptr) / 2;
                count_fr = s->dma_adc.count / FRAME_SAMPLE_BYTES;

		// dma_adc.count is the current total bytes that have not been read.
		// if the amount of unread bytes from the current sw pointer to the
		// end of the buffer is greater than the current total bytes that
		// have not been read, then set the "cnt" (unread bytes) to the
		// amount of unread bytes.  

		if (count_fr < cnt)
			cnt = count_fr;
                cnt_by = cnt * FRAME_SAMPLE_BYTES;
		spin_unlock_irqrestore(&s->lock, flags);
		//
		// if we are converting from 8/16 then we need to copy
		// twice the number of 16 bit bytes then 8 bit bytes.
		// 
		if (s->conversion) {
			if (cnt_by > (count * 2)) {
				cnt = (count * 2) / FRAME_SAMPLE_BYTES;
                                cnt_by = count * 2;
                        }
		} else {
			if (cnt_by > count) {
				cnt = count / FRAME_SAMPLE_BYTES;
                                cnt_by = count;
                        }
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
		// virtual address to copy from is dma_buf+swptr
		// the "cnt" is the number of bytes to read.

		CS_DBGOUT(CS_WAVE_READ, 2, printk(KERN_INFO
			"_read() copy_to cnt=%d count=%d ", cnt_by, count));
		CS_DBGOUT(CS_WAVE_READ, 8, printk(KERN_INFO
			 " .sbufsz=%d .count=%d buffer=0x%.8x ret=%d\n",
				 s->dma_adc.sbufsz, s->dma_adc.count,
				 (unsigned) buffer, ret));

		if (copy_to_user (buffer, ((void *)s->dma_adc.sb_swptr), cnt_by))
			return ret ? ret : -EFAULT;
                copied = cnt_by;

                /* Return the descriptors */
		spin_lock_irqsave(&s->lock, flags);
                CS_DBGOUT(CS_FUNCTION, 2, 
                          printk(KERN_INFO "cs4297a: upd_rcv sw->hw %x/%x\n", s->dma_adc.swptr, s->dma_adc.hwptr));
		s->dma_adc.count -= cnt_by;
                s->dma_adc.sb_swptr += cnt * 2;
                if (s->dma_adc.sb_swptr == s->dma_adc.sb_end)
                        s->dma_adc.sb_swptr = s->dma_adc.sample_buf;
		spin_unlock_irqrestore(&s->lock, flags);
		count -= copied;
		buffer += copied;
		ret += copied;
		start_adc(s);
	}
	CS_DBGOUT(CS_FUNCTION | CS_WAVE_READ, 2,
		  printk(KERN_INFO "cs4297a: cs4297a_read()- %d\n", ret));
	return ret;
}


static ssize_t cs4297a_write(struct file *file, const char *buffer,
			    size_t count, loff_t * ppos)
{
	struct cs4297a_state *s =
	    (struct cs4297a_state *) file->private_data;
	ssize_t ret;
	unsigned long flags;
	unsigned swptr, hwptr;
	int cnt;

	CS_DBGOUT(CS_FUNCTION | CS_WAVE_WRITE, 2,
		  printk(KERN_INFO "cs4297a: cs4297a_write()+ count=%d\n",
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
                serdma_t *d = &s->dma_dac;
                int copy_cnt;
                u32 *s_tmpl;
                u32 *t_tmpl;
                u32 left, right;
                int swap = (s->prop_dac.fmt == AFMT_S16_LE) || (s->prop_dac.fmt == AFMT_U16_LE);
                
                /* XXXXXX this is broken for BLOAT_FACTOR */
		spin_lock_irqsave(&s->lock, flags);
		if (d->count < 0) {
			d->count = 0;
			d->swptr = d->hwptr;
		}
		if (d->underrun) {
			d->underrun = 0;
                        hwptr = (unsigned) (((__raw_readq(SS_CSR(R_SER_DMA_CUR_DSCR_ADDR_TX)) & M_DMA_CURDSCR_ADDR) -
                                             d->descrtab_phys) / sizeof(serdma_descr_t));
			d->swptr = d->hwptr = hwptr;
		}
		swptr = d->swptr;
		cnt = d->sbufsz - (swptr * FRAME_SAMPLE_BYTES);
                /* Will this write fill up the buffer? */
		if (d->count + cnt > d->sbufsz)
			cnt = d->sbufsz - d->count;
		spin_unlock_irqrestore(&s->lock, flags);
		if (cnt > count)
			cnt = count;
		if (cnt <= 0) {
			start_dac(s);
			if (file->f_flags & O_NONBLOCK)
				return ret ? ret : -EAGAIN;
			interruptible_sleep_on(&d->wait);
			if (signal_pending(current))
				return ret ? ret : -ERESTARTSYS;
			continue;
		}
		if (copy_from_user(d->sample_buf, buffer, cnt))
			return ret ? ret : -EFAULT;

                copy_cnt = cnt;
                s_tmpl = (u32 *)d->sample_buf;
                t_tmpl = (u32 *)(d->dma_buf + (swptr * 4));

                /* XXXKW assuming 16-bit stereo! */
                do {
			u32 tmp;

			t_tmpl[0] = cpu_to_be32(0x98000000);

			tmp = be32_to_cpu(s_tmpl[0]);
			left = tmp & 0xffff;
			right = tmp >> 16;
			if (swap) {
				left = swab16(left);
				right = swab16(right);
			}
			t_tmpl[1] = cpu_to_be32(left >> 8);
			t_tmpl[2] = cpu_to_be32(((left & 0xff) << 24) |
						(right << 4));

                        s_tmpl++;
                        t_tmpl += 8;
                        copy_cnt -= 4;
                } while (copy_cnt);

                /* Mux in any pending read/write accesses */
                if (s->reg_request) {
			*(u64 *)(d->dma_buf + (swptr * 4)) |=
				cpu_to_be64(s->reg_request);
                        s->reg_request = 0;
                        wake_up(&s->dma_dac.reg_wait);
                }

                CS_DBGOUT(CS_WAVE_WRITE, 4,
                          printk(KERN_INFO
                                 "cs4297a: copy in %d to swptr %x\n", cnt, swptr));

		swptr = (swptr + (cnt/FRAME_SAMPLE_BYTES)) % d->ringsz;
                __raw_writeq(cnt/FRAME_SAMPLE_BYTES, SS_CSR(R_SER_DMA_DSCR_COUNT_TX));
		spin_lock_irqsave(&s->lock, flags);
		d->swptr = swptr;
		d->count += cnt;
		d->endcleared = 0;
		spin_unlock_irqrestore(&s->lock, flags);
		count -= cnt;
		buffer += cnt;
		ret += cnt;
		start_dac(s);
	}
	CS_DBGOUT(CS_FUNCTION | CS_WAVE_WRITE, 2,
		  printk(KERN_INFO "cs4297a: cs4297a_write()- %d\n", ret));
	return ret;
}


static unsigned int cs4297a_poll(struct file *file,
				struct poll_table_struct *wait)
{
	struct cs4297a_state *s =
	    (struct cs4297a_state *) file->private_data;
	unsigned long flags;
	unsigned int mask = 0;

	CS_DBGOUT(CS_FUNCTION | CS_WAVE_WRITE | CS_WAVE_READ, 4,
		  printk(KERN_INFO "cs4297a: cs4297a_poll()+\n"));
	VALIDATE_STATE(s);
	if (file->f_mode & FMODE_WRITE) {
		CS_DBGOUT(CS_FUNCTION | CS_WAVE_WRITE | CS_WAVE_READ, 4,
			  printk(KERN_INFO
				 "cs4297a: cs4297a_poll() wait on FMODE_WRITE\n"));
		if(!s->dma_dac.ready && prog_dmabuf_dac(s))
			return 0;
		poll_wait(file, &s->dma_dac.wait, wait);
	}
	if (file->f_mode & FMODE_READ) {
		CS_DBGOUT(CS_FUNCTION | CS_WAVE_WRITE | CS_WAVE_READ, 4,
			  printk(KERN_INFO
				 "cs4297a: cs4297a_poll() wait on FMODE_READ\n"));
		if(!s->dma_dac.ready && prog_dmabuf_adc(s))
			return 0;
		poll_wait(file, &s->dma_adc.wait, wait);
	}
	spin_lock_irqsave(&s->lock, flags);
	cs4297a_update_ptr(s,CS_FALSE);
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
			if ((signed) (s->dma_dac.sbufsz/2) >= s->dma_dac.count)
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
		  printk(KERN_INFO "cs4297a: cs4297a_poll()- 0x%.8x\n",
			 mask));
	return mask;
}


static int cs4297a_mmap(struct file *file, struct vm_area_struct *vma)
{
        /* XXXKW currently no mmap support */
        return -EINVAL;
	return 0;
}


static int cs4297a_ioctl(struct inode *inode, struct file *file,
			unsigned int cmd, unsigned long arg)
{
	struct cs4297a_state *s =
	    (struct cs4297a_state *) file->private_data;
	unsigned long flags;
	audio_buf_info abinfo;
	count_info cinfo;
	int val, mapped, ret;

	CS_DBGOUT(CS_FUNCTION|CS_IOCTL, 4, printk(KERN_INFO
		 "cs4297a: cs4297a_ioctl(): file=0x%.8x cmd=0x%.8x\n",
			 (unsigned) file, cmd));
#if CSDEBUG
	cs_printioctl(cmd);
#endif
	VALIDATE_STATE(s);
	mapped = ((file->f_mode & FMODE_WRITE) && s->dma_dac.mapped) ||
	    ((file->f_mode & FMODE_READ) && s->dma_adc.mapped);
	switch (cmd) {
	case OSS_GETVERSION:
		CS_DBGOUT(CS_IOCTL | CS_PARMS, 4, printk(KERN_INFO
			"cs4297a: cs4297a_ioctl(): SOUND_VERSION=0x%.8x\n",
				 SOUND_VERSION));
		return put_user(SOUND_VERSION, (int *) arg);

	case SNDCTL_DSP_SYNC:
		CS_DBGOUT(CS_IOCTL, 4, printk(KERN_INFO
			 "cs4297a: cs4297a_ioctl(): DSP_SYNC\n"));
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
				(int *) arg);

	case SNDCTL_DSP_RESET:
		CS_DBGOUT(CS_IOCTL, 4, printk(KERN_INFO
			 "cs4297a: cs4297a_ioctl(): DSP_RESET\n"));
		if (file->f_mode & FMODE_WRITE) {
			stop_dac(s);
			synchronize_irq(s->irq);
                        s->dma_dac.count = s->dma_dac.total_bytes =
                                s->dma_dac.blocks = s->dma_dac.wakeup = 0;
			s->dma_dac.swptr = s->dma_dac.hwptr =
                                (int)(((__raw_readq(SS_CSR(R_SER_DMA_CUR_DSCR_ADDR_TX)) & M_DMA_CURDSCR_ADDR) -
                                       s->dma_dac.descrtab_phys) / sizeof(serdma_descr_t));
		}
		if (file->f_mode & FMODE_READ) {
			stop_adc(s);
			synchronize_irq(s->irq);
                        s->dma_adc.count = s->dma_adc.total_bytes =
                                s->dma_adc.blocks = s->dma_dac.wakeup = 0;
			s->dma_adc.swptr = s->dma_adc.hwptr =
                                (int)(((__raw_readq(SS_CSR(R_SER_DMA_CUR_DSCR_ADDR_RX)) & M_DMA_CURDSCR_ADDR) -
                                       s->dma_adc.descrtab_phys) / sizeof(serdma_descr_t));
		}
		return 0;

	case SNDCTL_DSP_SPEED:
		if (get_user(val, (int *) arg))
			return -EFAULT;
		CS_DBGOUT(CS_IOCTL | CS_PARMS, 4, printk(KERN_INFO
			 "cs4297a: cs4297a_ioctl(): DSP_SPEED val=%d -> 48000\n", val));
                val = 48000;
                return put_user(val, (int *) arg);

	case SNDCTL_DSP_STEREO:
		if (get_user(val, (int *) arg))
			return -EFAULT;
		CS_DBGOUT(CS_IOCTL | CS_PARMS, 4, printk(KERN_INFO
			 "cs4297a: cs4297a_ioctl(): DSP_STEREO val=%d\n", val));
		if (file->f_mode & FMODE_READ) {
			stop_adc(s);
			s->dma_adc.ready = 0;
			s->prop_adc.channels = val ? 2 : 1;
		}
		if (file->f_mode & FMODE_WRITE) {
			stop_dac(s);
			s->dma_dac.ready = 0;
			s->prop_dac.channels = val ? 2 : 1;
		}
		return 0;

	case SNDCTL_DSP_CHANNELS:
		if (get_user(val, (int *) arg))
			return -EFAULT;
		CS_DBGOUT(CS_IOCTL | CS_PARMS, 4, printk(KERN_INFO
			 "cs4297a: cs4297a_ioctl(): DSP_CHANNELS val=%d\n",
				 val));
		if (val != 0) {
			if (file->f_mode & FMODE_READ) {
				stop_adc(s);
				s->dma_adc.ready = 0;
				if (val >= 2)
					s->prop_adc.channels = 2;
				else
					s->prop_adc.channels = 1;
			}
			if (file->f_mode & FMODE_WRITE) {
				stop_dac(s);
				s->dma_dac.ready = 0;
				if (val >= 2)
					s->prop_dac.channels = 2;
				else
					s->prop_dac.channels = 1;
			}
		}

		if (file->f_mode & FMODE_WRITE)
			val = s->prop_dac.channels;
		else if (file->f_mode & FMODE_READ)
			val = s->prop_adc.channels;

		return put_user(val, (int *) arg);

	case SNDCTL_DSP_GETFMTS:	// Returns a mask 
		CS_DBGOUT(CS_IOCTL | CS_PARMS, 4, printk(KERN_INFO
			"cs4297a: cs4297a_ioctl(): DSP_GETFMT val=0x%.8x\n",
				 AFMT_S16_LE | AFMT_U16_LE | AFMT_S8 |
				 AFMT_U8));
		return put_user(AFMT_S16_LE | AFMT_U16_LE | AFMT_S8 |
				AFMT_U8, (int *) arg);

	case SNDCTL_DSP_SETFMT:
		if (get_user(val, (int *) arg))
			return -EFAULT;
		CS_DBGOUT(CS_IOCTL | CS_PARMS, 4, printk(KERN_INFO
			 "cs4297a: cs4297a_ioctl(): DSP_SETFMT val=0x%.8x\n",
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
			}
		} else {
			if (file->f_mode & FMODE_WRITE)
				val = s->prop_dac.fmt_original;
			else if (file->f_mode & FMODE_READ)
				val = s->prop_adc.fmt_original;
		}
		CS_DBGOUT(CS_IOCTL | CS_PARMS, 4, printk(KERN_INFO
		  "cs4297a: cs4297a_ioctl(): DSP_SETFMT return val=0x%.8x\n", 
			val));
		return put_user(val, (int *) arg);

	case SNDCTL_DSP_POST:
		CS_DBGOUT(CS_IOCTL, 4, printk(KERN_INFO
			 "cs4297a: cs4297a_ioctl(): DSP_POST\n"));
		return 0;

	case SNDCTL_DSP_GETTRIGGER:
		val = 0;
		if (file->f_mode & s->ena & FMODE_READ)
			val |= PCM_ENABLE_INPUT;
		if (file->f_mode & s->ena & FMODE_WRITE)
			val |= PCM_ENABLE_OUTPUT;
		return put_user(val, (int *) arg);

	case SNDCTL_DSP_SETTRIGGER:
		if (get_user(val, (int *) arg))
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
		cs4297a_update_ptr(s,CS_FALSE);
		abinfo.fragsize = s->dma_dac.fragsize;
		if (s->dma_dac.mapped)
			abinfo.bytes = s->dma_dac.sbufsz;
		else
			abinfo.bytes =
			    s->dma_dac.sbufsz - s->dma_dac.count;
		abinfo.fragstotal = s->dma_dac.numfrag;
		abinfo.fragments = abinfo.bytes >> s->dma_dac.fragshift;
		CS_DBGOUT(CS_FUNCTION | CS_PARMS, 4, printk(KERN_INFO
			"cs4297a: cs4297a_ioctl(): GETOSPACE .fragsize=%d .bytes=%d .fragstotal=%d .fragments=%d\n",
				abinfo.fragsize,abinfo.bytes,abinfo.fragstotal,
				abinfo.fragments));
		spin_unlock_irqrestore(&s->lock, flags);
		return copy_to_user((void *) arg, &abinfo,
				    sizeof(abinfo)) ? -EFAULT : 0;

	case SNDCTL_DSP_GETISPACE:
		if (!(file->f_mode & FMODE_READ))
			return -EINVAL;
		if (!s->dma_adc.ready && (val = prog_dmabuf_adc(s)))
			return val;
		spin_lock_irqsave(&s->lock, flags);
		cs4297a_update_ptr(s,CS_FALSE);
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
		return copy_to_user((void *) arg, &abinfo,
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
		cs4297a_update_ptr(s,CS_FALSE);
		val = s->dma_dac.count;
		spin_unlock_irqrestore(&s->lock, flags);
		return put_user(val, (int *) arg);

	case SNDCTL_DSP_GETIPTR:
		if (!(file->f_mode & FMODE_READ))
			return -EINVAL;
		if(!s->dma_adc.ready && prog_dmabuf_adc(s))
			return 0;
		spin_lock_irqsave(&s->lock, flags);
		cs4297a_update_ptr(s,CS_FALSE);
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
		return copy_to_user((void *) arg, &cinfo, sizeof(cinfo)) ? -EFAULT : 0;

	case SNDCTL_DSP_GETOPTR:
		if (!(file->f_mode & FMODE_WRITE))
			return -EINVAL;
		if(!s->dma_dac.ready && prog_dmabuf_dac(s))
			return 0;
		spin_lock_irqsave(&s->lock, flags);
		cs4297a_update_ptr(s,CS_FALSE);
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
		return copy_to_user((void *) arg, &cinfo, sizeof(cinfo)) ? -EFAULT : 0;

	case SNDCTL_DSP_GETBLKSIZE:
		if (file->f_mode & FMODE_WRITE) {
			if ((val = prog_dmabuf_dac(s)))
				return val;
			return put_user(s->dma_dac.fragsize, (int *) arg);
		}
		if ((val = prog_dmabuf_adc(s)))
			return val;
		if (s->conversion)
			return put_user(s->dma_adc.fragsize / 2,
					(int *) arg);
		else
			return put_user(s->dma_adc.fragsize, (int *) arg);

	case SNDCTL_DSP_SETFRAGMENT:
		if (get_user(val, (int *) arg))
			return -EFAULT;
		return 0;	// Say OK, but do nothing.

	case SNDCTL_DSP_SUBDIVIDE:
		if ((file->f_mode & FMODE_READ && s->dma_adc.subdivision)
		    || (file->f_mode & FMODE_WRITE
			&& s->dma_dac.subdivision)) return -EINVAL;
		if (get_user(val, (int *) arg))
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
			return put_user(s->prop_adc.rate, (int *) arg);
		else if (file->f_mode & FMODE_WRITE)
			return put_user(s->prop_dac.rate, (int *) arg);

	case SOUND_PCM_READ_CHANNELS:
		if (file->f_mode & FMODE_READ)
			return put_user(s->prop_adc.channels, (int *) arg);
		else if (file->f_mode & FMODE_WRITE)
			return put_user(s->prop_dac.channels, (int *) arg);

	case SOUND_PCM_READ_BITS:
		if (file->f_mode & FMODE_READ)
			return
			    put_user(
				     (s->prop_adc.
				      fmt & (AFMT_S8 | AFMT_U8)) ? 8 : 16,
				     (int *) arg);
		else if (file->f_mode & FMODE_WRITE)
			return
			    put_user(
				     (s->prop_dac.
				      fmt & (AFMT_S8 | AFMT_U8)) ? 8 : 16,
				     (int *) arg);

	case SOUND_PCM_WRITE_FILTER:
	case SNDCTL_DSP_SETSYNCRO:
	case SOUND_PCM_READ_FILTER:
		return -EINVAL;
	}
	return mixer_ioctl(s, cmd, arg);
}


static int cs4297a_release(struct inode *inode, struct file *file)
{
	struct cs4297a_state *s =
	    (struct cs4297a_state *) file->private_data;

        CS_DBGOUT(CS_FUNCTION | CS_RELEASE, 2, printk(KERN_INFO
		 "cs4297a: cs4297a_release(): inode=0x%.8x file=0x%.8x f_mode=0x%x\n",
			 (unsigned) inode, (unsigned) file, file->f_mode));
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

static int cs4297a_open(struct inode *inode, struct file *file)
{
	int minor = iminor(inode);
	struct cs4297a_state *s=NULL;
	struct list_head *entry;

	CS_DBGOUT(CS_FUNCTION | CS_OPEN, 2, printk(KERN_INFO
		"cs4297a: cs4297a_open(): inode=0x%.8x file=0x%.8x f_mode=0x%x\n",
			(unsigned) inode, (unsigned) file, file->f_mode));
	CS_DBGOUT(CS_FUNCTION | CS_OPEN, 2, printk(KERN_INFO
                "cs4297a: status = %08x\n", (int)__raw_readq(SS_CSR(R_SER_STATUS_DEBUG))));

	list_for_each(entry, &cs4297a_devs)
	{
		s = list_entry(entry, struct cs4297a_state, list);

		if (!((s->dev_audio ^ minor) & ~0xf))
			break;
	}
	if (entry == &cs4297a_devs)
		return -ENODEV;
	if (!s) {
		CS_DBGOUT(CS_FUNCTION | CS_OPEN, 2, printk(KERN_INFO
			"cs4297a: cs4297a_open(): Error - unable to find audio state struct\n"));
		return -ENODEV;
	}
	VALIDATE_STATE(s);
	file->private_data = s;

	// wait for device to become free 
	if (!(file->f_mode & (FMODE_WRITE | FMODE_READ))) {
		CS_DBGOUT(CS_FUNCTION | CS_OPEN | CS_ERROR, 2, printk(KERN_INFO
			 "cs4297a: cs4297a_open(): Error - must open READ and/or WRITE\n"));
		return -ENODEV;
	}
	if (file->f_mode & FMODE_WRITE) {
                if (__raw_readq(SS_CSR(R_SER_DMA_DSCR_COUNT_TX)) != 0) {
                        printk(KERN_ERR "cs4297a: TX pipe needs to drain\n");
                        while (__raw_readq(SS_CSR(R_SER_DMA_DSCR_COUNT_TX)))
                                ;
                }
          
		mutex_lock(&s->open_sem_dac);
		while (s->open_mode & FMODE_WRITE) {
			if (file->f_flags & O_NONBLOCK) {
				mutex_unlock(&s->open_sem_dac);
				return -EBUSY;
			}
			mutex_unlock(&s->open_sem_dac);
			interruptible_sleep_on(&s->open_wait_dac);

			if (signal_pending(current)) {
                                printk("open - sig pending\n");
				return -ERESTARTSYS;
                        }
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

			if (signal_pending(current)) {
                                printk("open - sig pending\n");
				return -ERESTARTSYS;
                        }
			mutex_lock(&s->open_sem_adc);
		}
	}
	s->open_mode |= file->f_mode & (FMODE_READ | FMODE_WRITE);
	if (file->f_mode & FMODE_READ) {
		s->prop_adc.fmt = AFMT_S16_BE;
		s->prop_adc.fmt_original = s->prop_adc.fmt;
		s->prop_adc.channels = 2;
		s->prop_adc.rate = 48000;
		s->conversion = 0;
		s->ena &= ~FMODE_READ;
		s->dma_adc.ossfragshift = s->dma_adc.ossmaxfrags =
		    s->dma_adc.subdivision = 0;
		mutex_unlock(&s->open_sem_adc);

		if (prog_dmabuf_adc(s)) {
			CS_DBGOUT(CS_OPEN | CS_ERROR, 2, printk(KERN_ERR
				"cs4297a: adc Program dmabufs failed.\n"));
			cs4297a_release(inode, file);
			return -ENOMEM;
		}
	}
	if (file->f_mode & FMODE_WRITE) {
		s->prop_dac.fmt = AFMT_S16_BE;
		s->prop_dac.fmt_original = s->prop_dac.fmt;
		s->prop_dac.channels = 2;
		s->prop_dac.rate = 48000;
		s->conversion = 0;
		s->ena &= ~FMODE_WRITE;
		s->dma_dac.ossfragshift = s->dma_dac.ossmaxfrags =
		    s->dma_dac.subdivision = 0;
		mutex_unlock(&s->open_sem_dac);

		if (prog_dmabuf_dac(s)) {
			CS_DBGOUT(CS_OPEN | CS_ERROR, 2, printk(KERN_ERR
				"cs4297a: dac Program dmabufs failed.\n"));
			cs4297a_release(inode, file);
			return -ENOMEM;
		}
	}
	CS_DBGOUT(CS_FUNCTION | CS_OPEN, 2,
		  printk(KERN_INFO "cs4297a: cs4297a_open()- 0\n"));
	return nonseekable_open(inode, file);
}


// ******************************************************************************************
//   Wave (audio) file operations struct.
// ******************************************************************************************
static /*const */ struct file_operations cs4297a_audio_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.read		= cs4297a_read,
	.write		= cs4297a_write,
	.poll		= cs4297a_poll,
	.ioctl		= cs4297a_ioctl,
	.mmap		= cs4297a_mmap,
	.open		= cs4297a_open,
	.release	= cs4297a_release,
};

static void cs4297a_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct cs4297a_state *s = (struct cs4297a_state *) dev_id;
        u32 status;

        status = __raw_readq(SS_CSR(R_SER_STATUS_DEBUG));

        CS_DBGOUT(CS_INTERRUPT, 6, printk(KERN_INFO
                 "cs4297a: cs4297a_interrupt() HISR=0x%.8x\n", status));

#if 0
        /* XXXKW what check *should* be done here? */
        if (!(status & (M_SYNCSER_RX_EOP_COUNT | M_SYNCSER_RX_OVERRUN | M_SYNCSER_RX_SYNC_ERR))) {
                status = __raw_readq(SS_CSR(R_SER_STATUS));
                printk(KERN_ERR "cs4297a: unexpected interrupt (status %08x)\n", status);
                return;
        }
#endif

        if (status & M_SYNCSER_RX_SYNC_ERR) {
                status = __raw_readq(SS_CSR(R_SER_STATUS));
                printk(KERN_ERR "cs4297a: rx sync error (status %08x)\n", status);
                return;
        }

        if (status & M_SYNCSER_RX_OVERRUN) {
                int newptr, i;
                s->stats.rx_ovrrn++;
                printk(KERN_ERR "cs4297a: receive FIFO overrun\n");

                /* Fix things up: get the receive descriptor pool
                   clean and give them back to the hardware */
                while (__raw_readq(SS_CSR(R_SER_DMA_DSCR_COUNT_RX)))
                        ;
                newptr = (unsigned) (((__raw_readq(SS_CSR(R_SER_DMA_CUR_DSCR_ADDR_RX)) & M_DMA_CURDSCR_ADDR) -
                                     s->dma_adc.descrtab_phys) / sizeof(serdma_descr_t));
                for (i=0; i<DMA_DESCR; i++) {
                        s->dma_adc.descrtab[i].descr_a &= ~M_DMA_SERRX_SOP;
                }
                s->dma_adc.swptr = s->dma_adc.hwptr = newptr;
                s->dma_adc.count = 0;
                s->dma_adc.sb_swptr = s->dma_adc.sb_hwptr = s->dma_adc.sample_buf;
                __raw_writeq(DMA_DESCR, SS_CSR(R_SER_DMA_DSCR_COUNT_RX));
        }

	spin_lock(&s->lock);
	cs4297a_update_ptr(s,CS_TRUE);
	spin_unlock(&s->lock);

	CS_DBGOUT(CS_INTERRUPT, 6, printk(KERN_INFO
		  "cs4297a: cs4297a_interrupt()-\n"));
}

#if 0
static struct initvol {
	int mixch;
	int vol;
} initvol[] __initdata = {

  	{SOUND_MIXER_WRITE_VOLUME, 0x4040},
        {SOUND_MIXER_WRITE_PCM, 0x4040},
        {SOUND_MIXER_WRITE_SYNTH, 0x4040},
	{SOUND_MIXER_WRITE_CD, 0x4040},
	{SOUND_MIXER_WRITE_LINE, 0x4040},
	{SOUND_MIXER_WRITE_LINE1, 0x4040},
	{SOUND_MIXER_WRITE_RECLEV, 0x0000},
	{SOUND_MIXER_WRITE_SPEAKER, 0x4040},
	{SOUND_MIXER_WRITE_MIC, 0x0000}
};
#endif

static int __init cs4297a_init(void)
{
	struct cs4297a_state *s;
	u32 pwr, id;
	mm_segment_t fs;
	int rval;
#ifndef CONFIG_BCM_CS4297A_CSWARM
	u64 cfg;
	int mdio_val;
#endif

	CS_DBGOUT(CS_INIT | CS_FUNCTION, 2, printk(KERN_INFO 
		"cs4297a: cs4297a_init_module()+ \n"));

#ifndef CONFIG_BCM_CS4297A_CSWARM
        mdio_val = __raw_readq(KSEG1 + A_MAC_REGISTER(2, R_MAC_MDIO)) &
                (M_MAC_MDIO_DIR|M_MAC_MDIO_OUT);

        /* Check syscfg for synchronous serial on port 1 */
        cfg = __raw_readq(KSEG1 + A_SCD_SYSTEM_CFG);
        if (!(cfg & M_SYS_SER1_ENABLE)) {
                __raw_writeq(cfg | M_SYS_SER1_ENABLE, KSEG1+A_SCD_SYSTEM_CFG);
                cfg = __raw_readq(KSEG1 + A_SCD_SYSTEM_CFG);
                if (!(cfg & M_SYS_SER1_ENABLE)) {
                  printk(KERN_INFO "cs4297a: serial port 1 not configured for synchronous operation\n");
                  return -1;
                }

                printk(KERN_INFO "cs4297a: serial port 1 switching to synchronous operation\n");
                
                /* Force the codec (on SWARM) to reset by clearing
                   GENO, preserving MDIO (no effect on CSWARM) */
                __raw_writeq(mdio_val, KSEG1+A_MAC_REGISTER(2, R_MAC_MDIO));
                udelay(10);
        }

        /* Now set GENO */
        __raw_writeq(mdio_val | M_MAC_GENC, KSEG1+A_MAC_REGISTER(2, R_MAC_MDIO));
        /* Give the codec some time to finish resetting (start the bit clock) */
        udelay(100);
#endif

	if (!(s = kmalloc(sizeof(struct cs4297a_state), GFP_KERNEL))) {
		CS_DBGOUT(CS_ERROR, 1, printk(KERN_ERR
		      "cs4297a: probe() no memory for state struct.\n"));
		return -1;
	}
	memset(s, 0, sizeof(struct cs4297a_state));
        s->magic = CS4297a_MAGIC;
	init_waitqueue_head(&s->dma_adc.wait);
	init_waitqueue_head(&s->dma_dac.wait);
	init_waitqueue_head(&s->dma_adc.reg_wait);
	init_waitqueue_head(&s->dma_dac.reg_wait);
	init_waitqueue_head(&s->open_wait);
	init_waitqueue_head(&s->open_wait_adc);
	init_waitqueue_head(&s->open_wait_dac);
	mutex_init(&s->open_sem_adc);
	mutex_init(&s->open_sem_dac);
	spin_lock_init(&s->lock);

        s->irq = K_INT_SER_1;

	if (request_irq
	    (s->irq, cs4297a_interrupt, 0, "Crystal CS4297a", s)) {
		CS_DBGOUT(CS_INIT | CS_ERROR, 1,
			  printk(KERN_ERR "cs4297a: irq %u in use\n", s->irq));
		goto err_irq;
	}
	if ((s->dev_audio = register_sound_dsp(&cs4297a_audio_fops, -1)) <
	    0) {
		CS_DBGOUT(CS_INIT | CS_ERROR, 1, printk(KERN_ERR
			 "cs4297a: probe() register_sound_dsp() failed.\n"));
		goto err_dev1;
	}
	if ((s->dev_mixer = register_sound_mixer(&cs4297a_mixer_fops, -1)) <
	    0) {
		CS_DBGOUT(CS_INIT | CS_ERROR, 1, printk(KERN_ERR
			 "cs4297a: probe() register_sound_mixer() failed.\n"));
		goto err_dev2;
	}

        if (ser_init(s) || dma_init(s)) {
		CS_DBGOUT(CS_INIT | CS_ERROR, 1, printk(KERN_ERR
			 "cs4297a: ser_init failed.\n"));
		goto err_dev3;
        }

        do {
                udelay(4000);
                rval = cs4297a_read_ac97(s, AC97_POWER_CONTROL, &pwr);
        } while (!rval && (pwr != 0xf));

        if (!rval) {
		char *sb1250_duart_present;

                fs = get_fs();
                set_fs(KERNEL_DS);
#if 0
                val = SOUND_MASK_LINE;
                mixer_ioctl(s, SOUND_MIXER_WRITE_RECSRC, (unsigned long) &val);
                for (i = 0; i < sizeof(initvol) / sizeof(initvol[0]); i++) {
                        val = initvol[i].vol;
                        mixer_ioctl(s, initvol[i].mixch, (unsigned long) &val);
                }
//                cs4297a_write_ac97(s, 0x18, 0x0808);
#else
                //                cs4297a_write_ac97(s, 0x5e, 0x180);
                cs4297a_write_ac97(s, 0x02, 0x0808);
                cs4297a_write_ac97(s, 0x18, 0x0808);
#endif
                set_fs(fs);

                list_add(&s->list, &cs4297a_devs);

                cs4297a_read_ac97(s, AC97_VENDOR_ID1, &id);

		sb1250_duart_present = symbol_get(sb1250_duart_present);
		if (sb1250_duart_present)
			sb1250_duart_present[1] = 0;

                printk(KERN_INFO "cs4297a: initialized (vendor id = %x)\n", id);

                CS_DBGOUT(CS_INIT | CS_FUNCTION, 2,
                          printk(KERN_INFO "cs4297a: cs4297a_init_module()-\n"));
                
                return 0;
        }

 err_dev3:
	unregister_sound_mixer(s->dev_mixer);
 err_dev2:
	unregister_sound_dsp(s->dev_audio);
 err_dev1:
	free_irq(s->irq, s);
 err_irq:
	kfree(s);

        printk(KERN_INFO "cs4297a: initialization failed\n");

        return -1;
}

static void __exit cs4297a_cleanup(void)
{
        /*
          XXXKW 
           disable_irq, free_irq
           drain DMA queue
           disable DMA
           disable TX/RX
           free memory
        */
	CS_DBGOUT(CS_INIT | CS_FUNCTION, 2,
		  printk(KERN_INFO "cs4297a: cleanup_cs4297a() finished\n"));
}

// --------------------------------------------------------------------- 

MODULE_AUTHOR("Kip Walker, Broadcom Corp.");
MODULE_DESCRIPTION("Cirrus Logic CS4297a Driver for Broadcom SWARM board");

// --------------------------------------------------------------------- 

module_init(cs4297a_init);
module_exit(cs4297a_cleanup);
