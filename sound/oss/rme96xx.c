/* (C) 2000 Guenter Geiger <geiger@debian.org>
   with copy/pastes from the driver of Winfried Ritsch <ritsch@iem.kug.ac.at>
   based on es1370.c



   *  10 Jan 2001: 0.1 initial version
   *  19 Jan 2001: 0.2 fixed bug in select()
   *  27 Apr 2001: 0.3 more than one card usable
   *  11 May 2001: 0.4 fixed for SMP, included into kernel source tree
   *  17 May 2001: 0.5 draining code didn't work on new cards
   *  18 May 2001: 0.6 remove synchronize_irq() call 
   *  17 Jul 2001: 0.7 updated xrmectrl to make it work for newer cards
   *   2 feb 2002: 0.8 fixed pci device handling, see below for patches from Heiko (Thanks!)
                       Marcus Meissner <Marcus.Meissner@caldera.de>

		       Modifications - Heiko Purnhagen <purnhage@tnt.uni-hannover.de>
		       HP20020108 fixed handling of "large" read()
		       HP20020116 towards REV 1.5 support, based on ALSA's card-rme9652.c
		       HP20020118 made mixer ioctl and handling of devices>1 more safe
		       HP20020201 fixed handling of "large" read() properly
		       added REV 1.5 S/P-DIF receiver support
		       SNDCTL_DSP_SPEED now returns the actual speed
   *  10 Aug 2002: added synchronize_irq() again

TODO:
   - test more than one card --- done
   - check for pci IOREGION (see es1370) in rme96xx_probe ??
   - error detection
   - mmap interface
   - mixer mmap interface
   - mixer ioctl
   - get rid of noise upon first open (why ??)
   - allow multiple open (at least for read)
   - allow multiple open for non overlapping regions
   - recheck the multiple devices part (offsets of different devices, etc)
   - do decent draining in _release --- done
   - SMP support
   - what about using fragstotal>2 for small fragsize? (HP20020118)
   - add support for AFMT_S32_LE
*/

#ifndef RMEVERSION
#define RMEVERSION "0.8"
#endif

#include <linux/module.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/sound.h>
#include <linux/soundcard.h>
#include <linux/pci.h>
#include <linux/smp_lock.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/mutex.h>

#include <asm/dma.h>
#include <asm/page.h>

#include "rme96xx.h"

#define NR_DEVICE 2

static int devices = 1;
module_param(devices, int, 0);
MODULE_PARM_DESC(devices, "number of dsp devices allocated by the driver");


MODULE_AUTHOR("Guenter Geiger, geiger@debian.org");
MODULE_DESCRIPTION("RME9652/36 \"Hammerfall\" Driver");
MODULE_LICENSE("GPL");


#ifdef DEBUG
#define DBG(x) printk("RME_DEBUG:");x
#define COMM(x) printk("RME_COMM: " x "\n");
#else
#define DBG(x) while (0) {}
#define COMM(x)
#endif

/*-------------------------------------------------------------------------- 
                        Preporcessor Macros and Definitions
 --------------------------------------------------------------------------*/

#define RME96xx_MAGIC 0x6473

/* Registers-Space in offsets from base address with 16MByte size */

#define RME96xx_IO_EXTENT     16l*1024l*1024l
#define RME96xx_CHANNELS_PER_CARD 26

/*                  Write - Register */

/* 0,4,8,12,16,20,24,28 ... hardware init (erasing fifo-pointer intern) */
#define RME96xx_num_of_init_regs   8

#define RME96xx_init_buffer       (0/4)
#define RME96xx_play_buffer       (32/4)  /* pointer to 26x64kBit RAM from mainboard */
#define RME96xx_rec_buffer        (36/4)  /* pointer to 26x64kBit RAM from mainboard */
#define RME96xx_control_register  (64/4)  /* exact meaning see below */
#define RME96xx_irq_clear         (96/4)  /* irq acknowledge */
#define RME96xx_time_code         (100/4) /* if used with alesis adat */
#define RME96xx_thru_base         (128/4) /* 132...228 Thru for 26 channels */
#define RME96xx_thru_channels     RME96xx_CHANNELS_PER_CARD

/*                     Read Register */

#define RME96xx_status_register    0     /* meaning see below */



/* Status Register: */
/* ------------------------------------------------------------------------ */
#define RME96xx_IRQ          0x0000001 /* IRQ is High if not reset by RMExx_irq_clear */
#define RME96xx_lock_2       0x0000002 /* ADAT 3-PLL: 1=locked, 0=unlocked */
#define RME96xx_lock_1       0x0000004 /* ADAT 2-PLL: 1=locked, 0=unlocked */
#define RME96xx_lock_0       0x0000008 /* ADAT 1-PLL: 1=locked, 0=unlocked */

#define RME96xx_fs48         0x0000010 /* sample rate 0 ...44.1/88.2,  1 ... 48/96 Khz */
#define RME96xx_wsel_rd      0x0000020 /* if Word-Clock is used and valid then 1 */
#define RME96xx_buf_pos1     0x0000040 /* Bit 6..15 : Position of buffer-pointer in 64Bytes-blocks */
#define RME96xx_buf_pos2     0x0000080 /* resolution +/- 1 64Byte/block (since 64Bytes bursts) */
 
#define RME96xx_buf_pos3     0x0000100 /* 10 bits = 1024 values */
#define RME96xx_buf_pos4     0x0000200 /* if we mask off the first 6 bits, we can take the status */
#define RME96xx_buf_pos5     0x0000400 /* register as sample counter in the hardware buffer */
#define RME96xx_buf_pos6     0x0000800 

#define RME96xx_buf_pos7     0x0001000 
#define RME96xx_buf_pos8     0x0002000 
#define RME96xx_buf_pos9     0x0004000
#define RME96xx_buf_pos10    0x0008000 

#define RME96xx_sync_2       0x0010000 /* if ADAT-IN3 synced to system clock */
#define RME96xx_sync_1       0x0020000 /* if ADAT-IN2 synced to system clock */
#define RME96xx_sync_0       0x0040000 /* if ADAT-IN1 synced to system clock */
#define RME96xx_DS_rd        0x0080000 /* 1=Double Speed, 0=Normal Speed */

#define RME96xx_tc_busy      0x0100000 /* 1=time-code copy in progress (960ms) */
#define RME96xx_tc_out       0x0200000 /* time-code out bit */
#define RME96xx_F_0          0x0400000 /*  000=64kHz, 100=88.2kHz, 011=96kHz  */
#define RME96xx_F_1          0x0800000 /*  111=32kHz, 110=44.1kHz, 101=48kHz, */

#define RME96xx_F_2          0x1000000 /*  001=Rev 1.5+ external Crystal Chip */
#define RME96xx_ERF          0x2000000 /* Error-Flag of SDPIF Receiver (1=No Lock)*/
#define RME96xx_buffer_id    0x4000000 /* toggles by each interrupt on rec/play */
#define RME96xx_tc_valid     0x8000000 /* 1 = a signal is detected on time-code input */
#define RME96xx_SPDIF_READ  0x10000000 /* byte available from Rev 1.5+ SPDIF interface */

/* Status Register Fields */

#define RME96xx_lock            (RME96xx_lock_0|RME96xx_lock_1|RME96xx_lock_2)
#define RME96xx_sync            (RME96xx_sync_0|RME96xx_sync_1|RME96xx_sync_2)
#define RME96xx_F               (RME96xx_F_0|RME96xx_F_1|RME96xx_F_2)
#define rme96xx_decode_spdif_rate(x) ((x)>>22)

/* Bit 6..15 : h/w buffer pointer */
#define RME96xx_buf_pos          0x000FFC0 
/* Bits 31,30,29 are bits 5,4,3 of h/w pointer position on later
   Rev G EEPROMS and Rev 1.5 cards or later.
*/ 
#define RME96xx_REV15_buf_pos(x) ((((x)&0xE0000000)>>26)|((x)&RME96xx_buf_pos))


/* Control-Register: */			    
/*--------------------------------------------------------------------------------*/

#define RME96xx_start_bit	0x0001 /* start record/play */
#define RME96xx_latency0	0x0002 /* Buffer size / latency */
#define RME96xx_latency1	0x0004 /*   buffersize = 512Bytes * 2^n */
#define RME96xx_latency2	0x0008 /*   0=64samples ... 7=8192samples */

#define RME96xx_Master		0x0010 /* Clock Mode 1=Master, 0=Slave/Auto */
#define RME96xx_IE		0x0020 /* Interupt Enable */
#define RME96xx_freq		0x0040 /* samplerate 0=44.1/88.2, 1=48/96 kHz*/
#define RME96xx_freq1		0x0080 /* samplerate 0=32 kHz, 1=other rates ??? (from ALSA, but may be wrong) */
#define RME96xx_DS              0x0100 /* double speed 0=44.1/48, 1=88.2/96 Khz */
#define RME96xx_PRO		0x0200 /* SPDIF-OUT 0=consumer, 1=professional */
#define RME96xx_EMP		0x0400 /* SPDIF-OUT emphasis 0=off, 1=on */
#define RME96xx_Dolby		0x0800 /* SPDIF-OUT non-audio bit 1=set, 0=unset */

#define RME96xx_opt_out	        0x1000 /* use 1st optical OUT as SPDIF: 1=yes, 0=no */
#define RME96xx_wsel            0x2000 /* use Wordclock as sync (overwrites master) */
#define RME96xx_inp_0           0x4000 /* SPDIF-IN 00=optical (ADAT1), */
#define RME96xx_inp_1           0x8000 /* 01=coaxial (Cinch), 10=internal CDROM */

#define RME96xx_SyncRef0       0x10000 /* preferred sync-source in autosync */
#define RME96xx_SyncRef1       0x20000 /* 00=ADAT1, 01=ADAT2, 10=ADAT3, 11=SPDIF */

#define RME96xx_SPDIF_RESET    (1<<18) /* Rev 1.5+: h/w SPDIF receiver */
#define RME96xx_SPDIF_SELECT   (1<<19)
#define RME96xx_SPDIF_CLOCK    (1<<20)
#define RME96xx_SPDIF_WRITE    (1<<21)
#define RME96xx_ADAT1_INTERNAL (1<<22) /* Rev 1.5+: if set, internal CD connector carries ADAT */


#define RME96xx_ctrl_init            (RME96xx_latency0 |\
                                     RME96xx_Master |\
                                     RME96xx_inp_1)
                              


/* Control register fields and shortcuts */

#define RME96xx_latency (RME96xx_latency0|RME96xx_latency1|RME96xx_latency2)
#define RME96xx_inp         (RME96xx_inp_0|RME96xx_inp_1)
#define RME96xx_SyncRef    (RME96xx_SyncRef0|RME96xx_SyncRef1)
#define RME96xx_mixer_allowed (RME96xx_Master|RME96xx_PRO|RME96xx_EMP|RME96xx_Dolby|RME96xx_opt_out|RME96xx_wsel|RME96xx_inp|RME96xx_SyncRef|RME96xx_ADAT1_INTERNAL)

/* latency = 512Bytes * 2^n, where n is made from Bit3 ... Bit1  (??? HP20020201) */

#define RME96xx_SET_LATENCY(x)   (((x)&0x7)<<1)
#define RME96xx_GET_LATENCY(x)   (((x)>>1)&0x7)
#define RME96xx_SET_inp(x) (((x)&0x3)<<14)
#define RME96xx_GET_inp(x)   (((x)>>14)&0x3)
#define RME96xx_SET_SyncRef(x) (((x)&0x3)<<17)
#define RME96xx_GET_SyncRef(x)   (((x)>>17)&0x3)


/* buffer sizes */
#define RME96xx_BYTES_PER_SAMPLE  4 /* sizeof(u32) */
#define RME_16K 16*1024

#define RME96xx_DMA_MAX_SAMPLES  (RME_16K)
#define RME96xx_DMA_MAX_SIZE     (RME_16K * RME96xx_BYTES_PER_SAMPLE)
#define RME96xx_DMA_MAX_SIZE_ALL (RME96xx_DMA_MAX_SIZE * RME96xx_CHANNELS_PER_CARD)

#define RME96xx_NUM_OF_FRAGMENTS     2
#define RME96xx_FRAGMENT_MAX_SIZE    (RME96xx_DMA_MAX_SIZE/2)
#define RME96xx_FRAGMENT_MAX_SAMPLES (RME96xx_DMA_MAX_SAMPLES/2)
#define RME96xx_MAX_LATENCY       7   /* 16k samples */


#define RME96xx_MAX_DEVS 4 /* we provide some OSS stereodevs */
#define RME96xx_MASK_DEVS 0x3 /* RME96xx_MAX_DEVS-1 */

#define RME_MESS "rme96xx:"
/*------------------------------------------------------------------------ 
                  Types, struct and function declarations 
 ------------------------------------------------------------------------*/


/* --------------------------------------------------------------------- */

static const char invalid_magic[] = KERN_CRIT RME_MESS" invalid magic value\n";

#define VALIDATE_STATE(s)                         \
({                                                \
	if (!(s) || (s)->magic != RME96xx_MAGIC) { \
		printk(invalid_magic);            \
		return -ENXIO;                    \
	}                                         \
})

/* --------------------------------------------------------------------- */


static struct file_operations rme96xx_audio_fops;
static struct file_operations rme96xx_mixer_fops;
static int numcards;

typedef int32_t raw_sample_t;

typedef struct _rme96xx_info {

	/* hardware settings */
	int magic;
	struct pci_dev * pcidev; /* pci_dev structure */
	unsigned long __iomem *iobase;	
	unsigned int irq;

	/* list of rme96xx devices */
	struct list_head devs;

	spinlock_t lock;

	u32 *recbuf;             /* memory for rec buffer */
	u32 *playbuf;            /* memory for play buffer */

	u32 control_register;

	u32 thru_bits; /* thru 1=on, 0=off channel 1=Bit1... channel 26= Bit26 */

	int hw_rev;             /* h/w rev * 10 (i.e. 1.5 has hw_rev = 15) */
	char *card_name;	/* hammerfall or hammerfall light names */

	int open_count;         /* unused ???   HP20020201 */

	int rate;
	int latency;
	unsigned int fragsize;
	int started;

	int hwptr; /* can be negativ because of pci burst offset  */
	unsigned int hwbufid;  /* set by interrupt, buffer which is written/read now */
	
	struct dmabuf {

		unsigned int format;
		int formatshift;
		int inchannels;       /* number of channels for device */
		int outchannels;       /* number of channels for device */
		int mono; /* if true, we play mono on 2 channels */
		int inoffset; /* which channel is considered the first one */
         	int outoffset;
		
		/* state */
		int opened;               /* open() made */
		int started;              /* first write/read */
		int mmapped;              /* mmap */
		int open_mode;

		struct _rme96xx_info *s;  

		/* pointer to read/write position in buffer */
		unsigned readptr;          
		unsigned writeptr;          

		unsigned error; /* over/underruns cleared on sync again */

		/* waiting and locking */
		wait_queue_head_t wait;
		struct mutex  open_mutex;
		wait_queue_head_t open_wait;

	} dma[RME96xx_MAX_DEVS]; 

	int dspnum[RME96xx_MAX_DEVS];  /* register with sound subsystem */ 
	int mixer;  /* register with sound subsystem */ 
} rme96xx_info;


/* fiddling with the card (first level hardware control) */

static inline void rme96xx_set_ctrl(rme96xx_info* s,int mask)
{

	s->control_register|=mask;
	writel(s->control_register,s->iobase + RME96xx_control_register);

}

static inline void rme96xx_unset_ctrl(rme96xx_info* s,int mask)
{

	s->control_register&=(~mask);
	writel(s->control_register,s->iobase + RME96xx_control_register);

}

static inline int rme96xx_get_sample_rate_status(rme96xx_info* s)
{
	int val;
	u32 status;
	status = readl(s->iobase + RME96xx_status_register);
	val = (status & RME96xx_fs48) ? 48000 : 44100;
	if (status & RME96xx_DS_rd)
		val *= 2;
	return val;
}

static inline int rme96xx_get_sample_rate_ctrl(rme96xx_info* s)
{
	int val;
	val = (s->control_register & RME96xx_freq) ? 48000 : 44100;
	if (s->control_register & RME96xx_DS)
		val *= 2;
	return val;
}


/* code from ALSA card-rme9652.c for rev 1.5 SPDIF receiver   HP 20020201 */

static void rme96xx_spdif_set_bit (rme96xx_info* s, int mask, int onoff)
{
	if (onoff) 
		s->control_register |= mask;
	else 
		s->control_register &= ~mask;
		
	writel(s->control_register,s->iobase + RME96xx_control_register);
}

static void rme96xx_spdif_write_byte (rme96xx_info* s, const int val)
{
	long mask;
	long i;

	for (i = 0, mask = 0x80; i < 8; i++, mask >>= 1) {
		if (val & mask)
			rme96xx_spdif_set_bit (s, RME96xx_SPDIF_WRITE, 1);
		else 
			rme96xx_spdif_set_bit (s, RME96xx_SPDIF_WRITE, 0);

		rme96xx_spdif_set_bit (s, RME96xx_SPDIF_CLOCK, 1);
		rme96xx_spdif_set_bit (s, RME96xx_SPDIF_CLOCK, 0);
	}
}

static int rme96xx_spdif_read_byte (rme96xx_info* s)
{
	long mask;
	long val;
	long i;

	val = 0;

	for (i = 0, mask = 0x80;  i < 8; i++, mask >>= 1) {
		rme96xx_spdif_set_bit (s, RME96xx_SPDIF_CLOCK, 1);
		if (readl(s->iobase + RME96xx_status_register) & RME96xx_SPDIF_READ)
			val |= mask;
		rme96xx_spdif_set_bit (s, RME96xx_SPDIF_CLOCK, 0);
	}

	return val;
}

static void rme96xx_write_spdif_codec (rme96xx_info* s, const int address, const int data)
{
	rme96xx_spdif_set_bit (s, RME96xx_SPDIF_SELECT, 1);
	rme96xx_spdif_write_byte (s, 0x20);
	rme96xx_spdif_write_byte (s, address);
	rme96xx_spdif_write_byte (s, data);
	rme96xx_spdif_set_bit (s, RME96xx_SPDIF_SELECT, 0);
}


static int rme96xx_spdif_read_codec (rme96xx_info* s, const int address)
{
	int ret;

	rme96xx_spdif_set_bit (s, RME96xx_SPDIF_SELECT, 1);
	rme96xx_spdif_write_byte (s, 0x20);
	rme96xx_spdif_write_byte (s, address);
	rme96xx_spdif_set_bit (s, RME96xx_SPDIF_SELECT, 0);
	rme96xx_spdif_set_bit (s, RME96xx_SPDIF_SELECT, 1);

	rme96xx_spdif_write_byte (s, 0x21);
	ret = rme96xx_spdif_read_byte (s);
	rme96xx_spdif_set_bit (s, RME96xx_SPDIF_SELECT, 0);

	return ret;
}

static void rme96xx_initialize_spdif_receiver (rme96xx_info* s)
{
	/* XXX what unsets this ? */
	/* no idea ???   HP 20020201 */

	s->control_register |= RME96xx_SPDIF_RESET;

	rme96xx_write_spdif_codec (s, 4, 0x40);
	rme96xx_write_spdif_codec (s, 17, 0x13);
	rme96xx_write_spdif_codec (s, 6, 0x02);
}

static inline int rme96xx_spdif_sample_rate (rme96xx_info *s, int *spdifrate)
{
	unsigned int rate_bits;

	*spdifrate = 0x1;
	if (readl(s->iobase + RME96xx_status_register) & RME96xx_ERF) {
		return -1;	/* error condition */
	}
	
	if (s->hw_rev == 15) {

		int x, y, ret;
		
		x = rme96xx_spdif_read_codec (s, 30);

		if (x != 0) 
			y = 48000 * 64 / x;
		else
			y = 0;

		if      (y > 30400 && y < 33600)  {ret = 32000; *spdifrate = 0x7;}
		else if (y > 41900 && y < 46000)  {ret = 44100; *spdifrate = 0x6;}
		else if (y > 46000 && y < 50400)  {ret = 48000; *spdifrate = 0x5;}
		else if (y > 60800 && y < 67200)  {ret = 64000; *spdifrate = 0x0;}
		else if (y > 83700 && y < 92000)  {ret = 88200; *spdifrate = 0x4;}
		else if (y > 92000 && y < 100000) {ret = 96000; *spdifrate = 0x3;}
		else                              {ret = 0; *spdifrate = 0x1;}
		return ret;
	}

	rate_bits = readl(s->iobase + RME96xx_status_register) & RME96xx_F;

	switch (*spdifrate = rme96xx_decode_spdif_rate(rate_bits)) {
	case 0x7:
		return 32000;
		break;

	case 0x6:
		return 44100;
		break;

	case 0x5:
		return 48000;
		break;

	case 0x4:
		return 88200;
		break;

	case 0x3:
		return 96000;
		break;

	case 0x0:
		return 64000;
		break;

	default:
		/* was an ALSA warning ...
		  snd_printk("%s: unknown S/PDIF input rate (bits = 0x%x)\n",
		  s->card_name, rate_bits);
		*/
		return 0;
		break;
	}
}

/* end of code from ALSA card-rme9652.c */



/* the hwbuf in the status register seems to have some jitter, to get rid of
   it, we first only let the numbers grow, to be on the secure side we 
   subtract a certain amount RME96xx_BURSTBYTES from the resulting number */

/* the function returns the hardware pointer in bytes */
#define RME96xx_BURSTBYTES -64  /* bytes by which hwptr could be off */

static inline int rme96xx_gethwptr(rme96xx_info* s,int exact)
{
	unsigned long flags;
	if (exact) {
		unsigned int hwp;
/* the hwptr seems to be rather unreliable :(, so we don't use it */
		spin_lock_irqsave(&s->lock,flags);
		
		hwp  = readl(s->iobase + RME96xx_status_register) & 0xffc0;
		s->hwptr = (hwp < s->hwptr) ? s->hwptr : hwp;
//		s->hwptr = hwp;

		spin_unlock_irqrestore(&s->lock,flags);
		return (s->hwptr+RME96xx_BURSTBYTES) & ((s->fragsize<<1)-1);
	}
	return (s->hwbufid ? s->fragsize : 0);
}

static inline void rme96xx_setlatency(rme96xx_info* s,int l)
{
	s->latency = l;
	s->fragsize = 1<<(8+l);
	rme96xx_unset_ctrl(s,RME96xx_latency);
	rme96xx_set_ctrl(s,RME96xx_SET_LATENCY(l));	
}


static void rme96xx_clearbufs(struct dmabuf* dma)
{
	int i,j;
	unsigned long flags;

	/* clear dmabufs */
	for(i=0;i<devices;i++) {
		for (j=0;j<dma->outchannels + dma->mono;j++)
			memset(&dma->s->playbuf[(dma->outoffset + j)*RME96xx_DMA_MAX_SAMPLES], 
			       0, RME96xx_DMA_MAX_SIZE);
	}
	spin_lock_irqsave(&dma->s->lock,flags);
	dma->writeptr = 0;
	dma->readptr = 0;
	spin_unlock_irqrestore(&dma->s->lock,flags);
}

static int rme96xx_startcard(rme96xx_info *s,int stop)
{
	int i;
	unsigned long flags;

	COMM       ("startcard");
	if(s->control_register & RME96xx_IE){
		/* disable interrupt first */
		
		rme96xx_unset_ctrl( s,RME96xx_start_bit );
		udelay(10);
		rme96xx_unset_ctrl( s,RME96xx_IE);
		spin_lock_irqsave(&s->lock,flags); /* timing is critical */
		s->started = 0;
		spin_unlock_irqrestore(&s->lock,flags);
		if (stop) {
		     COMM("Sound card stopped");
		     return 1;
		}
	}
	COMM       ("interrupt disabled");
	/* first initialize all pointers on card */
	for(i=0;i<RME96xx_num_of_init_regs;i++){
		writel(0,s->iobase + i);
		udelay(10); /* ?? */
	}
	COMM       ("regs cleaned");

	spin_lock_irqsave(&s->lock,flags); /* timing is critical */
	udelay(10);
	s->started = 1;
	s->hwptr = 0;
	spin_unlock_irqrestore(&s->lock,flags);

	rme96xx_set_ctrl( s, RME96xx_IE | RME96xx_start_bit);


	COMM("Sound card started");
  
	return 1;
}


static inline int rme96xx_getospace(struct dmabuf * dma, unsigned int hwp)
{
	int cnt;
	int  swptr;
	unsigned long flags;

	spin_lock_irqsave(&dma->s->lock,flags); 
	swptr = dma->writeptr;
	cnt = (hwp - swptr);
	
	if (cnt < 0) {
	     cnt = ((dma->s->fragsize<<1) - swptr);
	}
	spin_unlock_irqrestore(&dma->s->lock,flags);
	return cnt;
}

static inline int rme96xx_getispace(struct dmabuf * dma, unsigned int hwp)
{
	int cnt;
	int  swptr;
	unsigned long flags;

	spin_lock_irqsave(&dma->s->lock,flags); 
	swptr = dma->readptr;
	cnt = (hwp - swptr);
	 
	if (cnt < 0) {
		cnt = ((dma->s->fragsize<<1) - swptr);
	}
	spin_unlock_irqrestore(&dma->s->lock,flags);
	return cnt;
}


static inline int rme96xx_copyfromuser(struct dmabuf* dma,const char __user * buffer,int count,int hop)
{
	int swptr = dma->writeptr;
	switch (dma->format) {
	case AFMT_S32_BLOCKED:
	{
	     char __user * buf = (char __user *)buffer;
	     int cnt = count/dma->outchannels;
	     int i;
	     for (i=0;i < dma->outchannels;i++) {
		  char* hwbuf =(char*) &dma->s->playbuf[(dma->outoffset + i)*RME96xx_DMA_MAX_SAMPLES];
		  hwbuf+=swptr;

		  if (copy_from_user(hwbuf,buf, cnt))
		       return -1;
		  buf+=hop;
	     }
	     swptr+=cnt;
	     break;
	}
	case AFMT_S16_LE:
	{
	     int i,j;
	     int cnt = count/dma->outchannels;
	     for (i=0;i < dma->outchannels + dma->mono;i++) {
		     short __user * sbuf = (short __user *)buffer + i*(!dma->mono);
		     short* hwbuf =(short*) &dma->s->playbuf[(dma->outoffset + i)*RME96xx_DMA_MAX_SAMPLES];	     
		     hwbuf+=(swptr>>1);
		     for (j=0;j<(cnt>>1);j++) {
			     hwbuf++; /* skip the low 16 bits */
			     __get_user(*hwbuf++,sbuf++);
			     sbuf+=(dma->outchannels-1);
		     }
	     }
	     swptr += (cnt<<1);
	     break;
	}
	default:
	     printk(RME_MESS" unsupported format\n");
	     return -1;
	} /* switch */

	swptr&=((dma->s->fragsize<<1) -1);
	dma->writeptr = swptr;

	return 0;
}

/* The count argument is the number of bytes */
static inline int rme96xx_copytouser(struct dmabuf* dma,const char __user* buffer,int count,int hop)
{
	int swptr = dma->readptr;
	switch (dma->format) {
	case AFMT_S32_BLOCKED:
	{
	     char __user * buf = (char __user *)buffer;
	     int cnt = count/dma->inchannels;
	     int i;

	     for (i=0;i < dma->inchannels;i++) {
		  char* hwbuf =(char*) &dma->s->recbuf[(dma->inoffset + i)*RME96xx_DMA_MAX_SAMPLES];
		  hwbuf+=swptr;

		  if (copy_to_user(buf,hwbuf,cnt))
		       return -1;
		  buf+=hop;
	     }
	     swptr+=cnt;
	     break;
	}
	case AFMT_S16_LE:
	{
	     int i,j;
	     int cnt = count/dma->inchannels;
	     for (i=0;i < dma->inchannels;i++) {
		  short __user * sbuf = (short __user *)buffer + i;
		  short* hwbuf =(short*) &dma->s->recbuf[(dma->inoffset + i)*RME96xx_DMA_MAX_SAMPLES];	     
		  hwbuf+=(swptr>>1);
		  for (j=0;j<(cnt>>1);j++) {
		       hwbuf++;
		       __put_user(*hwbuf++,sbuf++);
		       sbuf+=(dma->inchannels-1);
		  }
	     }
	     swptr += (cnt<<1);
	     break;
	}
	default:
	     printk(RME_MESS" unsupported format\n");
	     return -1;
	} /* switch */
	
	swptr&=((dma->s->fragsize<<1) -1);	
	dma->readptr = swptr;
	return 0;
}


static irqreturn_t rme96xx_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	int i;
	rme96xx_info *s = (rme96xx_info *)dev_id;
	struct dmabuf *db;
	u32 status;
	unsigned long flags;

	status = readl(s->iobase + RME96xx_status_register);
	if (!(status & RME96xx_IRQ)) {
		return IRQ_NONE;
	}

	spin_lock_irqsave(&s->lock,flags);
	writel(0,s->iobase + RME96xx_irq_clear);

	s->hwbufid = (status & RME96xx_buffer_id)>>26;	
	if ((status & 0xffc0) <= 256) s->hwptr = 0; 
	for(i=0;i<devices;i++)
	{
		db = &(s->dma[i]);
		if(db->started > 0)
			wake_up(&(db->wait));		
	}  
	spin_unlock_irqrestore(&s->lock,flags);
	return IRQ_HANDLED;
}



/*---------------------------------------------------------------------------- 
 PCI detection and module initialization stuff 
 ----------------------------------------------------------------------------*/

static void* busmaster_malloc(int size) {
     int pg; /* 2 s exponent of memory size */
        char *buf;

        DBG(printk("kernel malloc pages ..\n"));
        
        for (pg = 0; PAGE_SIZE * (1 << pg) < size; pg++);

        buf = (char *) __get_free_pages(GFP_KERNEL | GFP_DMA, pg);

        if (buf) {
                struct page* page, *last_page;

                page = virt_to_page(buf);
                last_page = page + (1 << pg);
                DBG(printk("setting reserved bit\n"));
                while (page < last_page) {
			SetPageReserved(page);
                        page++;
                }
		return buf;
        }
	DBG(printk("allocated %ld",(long)buf));
	return NULL;
}

static void busmaster_free(void* ptr,int size) {
        int pg;
	struct page* page, *last_page;

        if (ptr == NULL)
                return;

        for (pg = 0; PAGE_SIZE * (1 << pg) < size; pg++);

        page = virt_to_page(ptr);
        last_page = page + (1 << pg);
        while (page < last_page) {
		ClearPageReserved(page);
		page++;
	}
	DBG(printk("freeing pages\n"));
        free_pages((unsigned long) ptr, pg);
	DBG(printk("done\n"));
}

/* initialize those parts of the info structure which are not pci detectable resources */

static int rme96xx_dmabuf_init(rme96xx_info * s,struct dmabuf* dma,int ioffset,int ooffset) {

	mutex_init(&dma->open_mutex);
	init_waitqueue_head(&dma->open_wait);
	init_waitqueue_head(&dma->wait);
	dma->s = s; 
	dma->error = 0;

	dma->format = AFMT_S32_BLOCKED;
	dma->formatshift = 0;
	dma->inchannels = dma->outchannels = 1;
	dma->inoffset = ioffset;
	dma->outoffset = ooffset;

	dma->opened=0;
	dma->started=0;
	dma->mmapped=0;
	dma->open_mode=0;
	dma->mono=0;

	rme96xx_clearbufs(dma);
	return 0;
}


static int rme96xx_init(rme96xx_info* s)
{
	int i;
	int status;
	unsigned short rev;

	DBG(printk("%s\n", __FUNCTION__));
	numcards++;

	s->magic = RME96xx_MAGIC; 

	spin_lock_init(&s->lock);

	COMM            ("setup busmaster memory")
	s->recbuf = busmaster_malloc(RME96xx_DMA_MAX_SIZE_ALL);
	s->playbuf = busmaster_malloc(RME96xx_DMA_MAX_SIZE_ALL);

	if (!s->recbuf || !s->playbuf) {
		printk(KERN_ERR RME_MESS" Unable to allocate busmaster memory\n");
		return -ENODEV;
	}

	COMM            ("setting rec and playbuffers")

	writel((u32) virt_to_bus(s->recbuf),s->iobase + RME96xx_rec_buffer);
  	writel((u32) virt_to_bus(s->playbuf),s->iobase + RME96xx_play_buffer);

	COMM             ("initializing control register")
	rme96xx_unset_ctrl(s,0xffffffff);
	rme96xx_set_ctrl(s,RME96xx_ctrl_init);


	COMM              ("setup devices")	
	for (i=0;i < devices;i++) {
		struct dmabuf * dma = &s->dma[i];
		rme96xx_dmabuf_init(s,dma,2*i,2*i);
	}

	/* code from ALSA card-rme9652.c   HP 20020201 */
        /* Determine the h/w rev level of the card. This seems like
	   a particularly kludgy way to encode it, but its what RME
	   chose to do, so we follow them ...
	*/

	status = readl(s->iobase + RME96xx_status_register);
	if (rme96xx_decode_spdif_rate(status&RME96xx_F) == 1) {
		s->hw_rev = 15;
	} else {
		s->hw_rev = 11;
	}

	/* Differentiate between the standard Hammerfall, and the
	   "Light", which does not have the expansion board. This
	   method comes from information received from Mathhias
	   Clausen at RME. Display the EEPROM and h/w revID where
	   relevant.  
	*/

	pci_read_config_word(s->pcidev, PCI_CLASS_REVISION, &rev);
	switch (rev & 0xff) {
	case 8: /* original eprom */
		if (s->hw_rev == 15) {
			s->card_name = "RME Digi9636 (Rev 1.5)";
		} else {
			s->card_name = "RME Digi9636";
		}
		break;
	case 9: /* W36_G EPROM */
		s->card_name = "RME Digi9636 (Rev G)";
		break;
	case 4: /* W52_G EPROM */
		s->card_name = "RME Digi9652 (Rev G)";
		break;
	default:
	case 3: /* original eprom */
		if (s->hw_rev == 15) {
			s->card_name = "RME Digi9652 (Rev 1.5)";
		} else {
			s->card_name = "RME Digi9652";
		}
		break;
	}

	printk(KERN_INFO RME_MESS" detected %s (hw_rev %d)\n",s->card_name,s->hw_rev); 

	if (s->hw_rev == 15)
		rme96xx_initialize_spdif_receiver (s);

	s->started = 0;
	rme96xx_setlatency(s,7);

	printk(KERN_INFO RME_MESS" card %d initialized\n",numcards); 
	return 0;
}


/* open uses this to figure out which device was opened .. this seems to be 
   unnecessary complex */

static LIST_HEAD(devs);

static int __devinit rme96xx_probe(struct pci_dev *pcidev, const struct pci_device_id *pciid)
{
	int i;
	rme96xx_info *s;

	DBG(printk("%s\n", __FUNCTION__));
	
	if (pcidev->irq == 0) 
		return -1;
	if (!pci_dma_supported(pcidev, 0xffffffff)) {
		printk(KERN_WARNING RME_MESS" architecture does not support 32bit PCI busmaster DMA\n");
		return -1;
	}
	if (!(s = kmalloc(sizeof(rme96xx_info), GFP_KERNEL))) {
		printk(KERN_WARNING RME_MESS" out of memory\n");
		return -1;
	}
	memset(s, 0, sizeof(rme96xx_info));

	s->pcidev = pcidev;
	s->iobase = ioremap(pci_resource_start(pcidev, 0),RME96xx_IO_EXTENT);
	s->irq = pcidev->irq;

        DBG(printk("remapped iobase: %lx irq %d\n",(long)s->iobase,s->irq));

	if (pci_enable_device(pcidev))
		goto err_irq;
	if (request_irq(s->irq, rme96xx_interrupt, SA_SHIRQ, "rme96xx", s)) {
		printk(KERN_ERR RME_MESS" irq %u in use\n", s->irq);
		goto err_irq;
	}
	
	/* initialize the card */

	i = 0;
	if (rme96xx_init(s) < 0) {
		printk(KERN_ERR RME_MESS" initialization failed\n");
		goto err_devices;
	}
	for (i=0;i<devices;i++) {
		if ((s->dspnum[i] = register_sound_dsp(&rme96xx_audio_fops, -1)) < 0)
			goto err_devices;
	}

	if ((s->mixer = register_sound_mixer(&rme96xx_mixer_fops, -1)) < 0)
		goto err_devices;

	pci_set_drvdata(pcidev, s);
	pcidev->dma_mask = 0xffffffff; /* ????? */
	/* put it into driver list */
	list_add_tail(&s->devs, &devs);

	DBG(printk("initialization successful\n"));
	return 0;

	/* error handler */
 err_devices:
	while (i--) 
		unregister_sound_dsp(s->dspnum[i]);
	free_irq(s->irq,s);
 err_irq:
	kfree(s);
	return -1;
}


static void __devexit rme96xx_remove(struct pci_dev *dev)
{
	int i;
	rme96xx_info *s = pci_get_drvdata(dev);

	if (!s) {
		printk(KERN_ERR"device structure not valid\n");
		return ;
	}

	if (s->started) rme96xx_startcard(s,0);

	i = devices;
	while (i) {
		i--;
		unregister_sound_dsp(s->dspnum[i]);
	}
	
	unregister_sound_mixer(s->mixer);
	synchronize_irq(s->irq);
	free_irq(s->irq,s);
	busmaster_free(s->recbuf,RME96xx_DMA_MAX_SIZE_ALL);
	busmaster_free(s->playbuf,RME96xx_DMA_MAX_SIZE_ALL);
	kfree(s);
	pci_set_drvdata(dev, NULL);
}


#ifndef PCI_VENDOR_ID_RME 
#define PCI_VENDOR_ID_RME 0x10ee
#endif
#ifndef PCI_DEVICE_ID_RME9652
#define PCI_DEVICE_ID_RME9652 0x3fc4
#endif
#ifndef PCI_ANY_ID
#define PCI_ANY_ID 0
#endif

static struct pci_device_id id_table[] = {
	{
		.vendor	   = PCI_VENDOR_ID_RME,
		.device	   = PCI_DEVICE_ID_RME9652,
		.subvendor = PCI_ANY_ID,
		.subdevice = PCI_ANY_ID,
	},
	{ 0, },
};

MODULE_DEVICE_TABLE(pci, id_table);

static struct pci_driver rme96xx_driver = {
	.name	  =  "rme96xx",
	.id_table = id_table,
	.probe	  = rme96xx_probe,
	.remove	  = __devexit_p(rme96xx_remove),
};

static int __init init_rme96xx(void)
{
	printk(KERN_INFO RME_MESS" version "RMEVERSION" time " __TIME__ " " __DATE__ "\n");
	devices = ((devices-1) & RME96xx_MASK_DEVS) + 1;
	printk(KERN_INFO RME_MESS" reserving %d dsp device(s)\n",devices);
        numcards = 0;
	return pci_register_driver(&rme96xx_driver);
}

static void __exit cleanup_rme96xx(void)
{
	printk(KERN_INFO RME_MESS" unloading\n");
	pci_unregister_driver(&rme96xx_driver);
}

module_init(init_rme96xx);
module_exit(cleanup_rme96xx);





/*-------------------------------------------------------------------------- 
   Implementation of file operations 
---------------------------------------------------------------------------*/

#define RME96xx_FMT (AFMT_S16_LE|AFMT_U8|AFMT_S32_BLOCKED)
/* AFTM_U8 is not (yet?) supported ...  HP20020201 */

static int rme96xx_ioctl(struct inode *in, struct file *file, unsigned int cmd, unsigned long arg)
{
	struct dmabuf * dma = (struct dmabuf *)file->private_data; 
	rme96xx_info *s = dma->s;
	unsigned long flags;
        audio_buf_info abinfo;
        count_info cinfo;
	int count;
	int val = 0;
	void __user *argp = (void __user *)arg;
	int __user *p = argp;

	VALIDATE_STATE(s);

	DBG(printk("ioctl %ud\n",cmd));

	switch (cmd) {
	case OSS_GETVERSION:
		return put_user(SOUND_VERSION, p);

	case SNDCTL_DSP_SYNC:
#if 0
		if (file->f_mode & FMODE_WRITE)
			return drain_dac2(s, 0/*file->f_flags & O_NONBLOCK*/);
#endif
		return 0;
		
	case SNDCTL_DSP_SETDUPLEX:
		return 0;

	case SNDCTL_DSP_GETCAPS:
		return put_user(DSP_CAP_DUPLEX | DSP_CAP_REALTIME | DSP_CAP_TRIGGER | DSP_CAP_MMAP, p);
		
        case SNDCTL_DSP_RESET:
//		rme96xx_clearbufs(dma);
		return 0;

        case SNDCTL_DSP_SPEED:
                if (get_user(val, p))
			return -EFAULT;
		if (val >= 0) {
/* generally it's not a problem if we change the speed 
			if (dma->open_mode & (~file->f_mode) & (FMODE_READ|FMODE_WRITE))
				return -EINVAL;
*/
			spin_lock_irqsave(&s->lock, flags);

			switch (val) {
			case 44100:
			case 88200:
				rme96xx_unset_ctrl(s,RME96xx_freq);
				break;
			case 48000: 
			case 96000: 
				rme96xx_set_ctrl(s,RME96xx_freq);
				break;
			/* just report current rate as default
			   e.g. use 0 to "select" current digital input rate
			default:
				rme96xx_unset_ctrl(s,RME96xx_freq);
				val = 44100;
			*/
			}
			if (val > 50000)
				rme96xx_set_ctrl(s,RME96xx_DS);
			else
				rme96xx_unset_ctrl(s,RME96xx_DS);
			/* set val to actual value  HP 20020201 */
			/* NOTE: if not "Sync Master", reported rate might be not yet "updated" ... but I don't want to insert a long udelay() here */
			if ((s->control_register & RME96xx_Master) && !(s->control_register & RME96xx_wsel))
				val = rme96xx_get_sample_rate_ctrl(s);
			else
				val = rme96xx_get_sample_rate_status(s);
			s->rate = val;
			spin_unlock_irqrestore(&s->lock, flags);
		}
		DBG(printk("speed set to %d\n",val));
		return put_user(val, p);
		
        case SNDCTL_DSP_STEREO: /* this plays a mono file on two channels */
                if (get_user(val, p))
			return -EFAULT;
		
		if (!val) {
			DBG(printk("setting to mono\n")); 
			dma->mono=1; 
			dma->inchannels = 1;
			dma->outchannels = 1;
		}
		else {
			DBG(printk("setting to stereo\n")); 
			dma->mono = 0;
			dma->inchannels = 2;
			dma->outchannels = 2;
		}
		return 0;
        case SNDCTL_DSP_CHANNELS:
		/* remember to check for resonable offset/channel pairs here */
                if (get_user(val, p))
			return -EFAULT;

		if (file->f_mode & FMODE_WRITE) { 			
			if (val > 0 && (dma->outoffset + val) <= RME96xx_CHANNELS_PER_CARD) 
				dma->outchannels = val;
			else
				dma->outchannels = val = 2;
			DBG(printk("setting to outchannels %d\n",val)); 
		}
		if (file->f_mode & FMODE_READ) {
			if (val > 0 && (dma->inoffset + val) <= RME96xx_CHANNELS_PER_CARD) 
				dma->inchannels = val;
			else
				dma->inchannels = val = 2;
			DBG(printk("setting to inchannels %d\n",val)); 
		}

		dma->mono=0;

		return put_user(val, p);
		
	case SNDCTL_DSP_GETFMTS: /* Returns a mask */
                return put_user(RME96xx_FMT, p);
		
	case SNDCTL_DSP_SETFMT: /* Selects ONE fmt*/
		DBG(printk("setting to format %x\n",val)); 
		if (get_user(val, p))
			return -EFAULT;
		if (val != AFMT_QUERY) {
			if (val & RME96xx_FMT)
				dma->format = val;
			switch (dma->format) {
			case AFMT_S16_LE:
				dma->formatshift=1;
				break;
			case AFMT_S32_BLOCKED:
				dma->formatshift=0;
				break;
			}
		}
		return put_user(dma->format, p);
		
	case SNDCTL_DSP_POST:
                return 0;

        case SNDCTL_DSP_GETTRIGGER:
		val = 0;
#if 0
		if (file->f_mode & FMODE_READ && s->ctrl & CTRL_ADC_EN) 
			val |= PCM_ENABLE_INPUT;
		if (file->f_mode & FMODE_WRITE && s->ctrl & CTRL_DAC2_EN) 
			val |= PCM_ENABLE_OUTPUT;
#endif
		return put_user(val, p);
		
	case SNDCTL_DSP_SETTRIGGER:
		if (get_user(val, p))
			return -EFAULT;
#if 0
		if (file->f_mode & FMODE_READ) {
			if (val & PCM_ENABLE_INPUT) {
				if (!s->dma_adc.ready && (ret = prog_dmabuf_adc(s)))
					return ret;
				start_adc(s);
			} else
				stop_adc(s);
		}
		if (file->f_mode & FMODE_WRITE) {
			if (val & PCM_ENABLE_OUTPUT) {
				if (!s->dma_dac2.ready && (ret = prog_dmabuf_dac2(s)))
					return ret;
				start_dac2(s);
			} else
				stop_dac2(s);
		}
#endif
		return 0;

	case SNDCTL_DSP_GETOSPACE:
		if (!(file->f_mode & FMODE_WRITE))
			return -EINVAL;

		val = rme96xx_gethwptr(dma->s,0);


		count = rme96xx_getospace(dma,val);
		if (!s->started) count = s->fragsize*2;
		abinfo.fragsize =(s->fragsize*dma->outchannels)>>dma->formatshift;
                abinfo.bytes = (count*dma->outchannels)>>dma->formatshift;
                abinfo.fragstotal = 2;
                abinfo.fragments = (count > s->fragsize); 

		return copy_to_user(argp, &abinfo, sizeof(abinfo)) ? -EFAULT : 0;

	case SNDCTL_DSP_GETISPACE:
		if (!(file->f_mode & FMODE_READ))
			return -EINVAL;

		val = rme96xx_gethwptr(dma->s,0);

		count = rme96xx_getispace(dma,val);

		abinfo.fragsize = (s->fragsize*dma->inchannels)>>dma->formatshift;
                abinfo.bytes = (count*dma->inchannels)>>dma->formatshift;
                abinfo.fragstotal = 2;
                abinfo.fragments = count > s->fragsize; 
		return copy_to_user(argp, &abinfo, sizeof(abinfo)) ? -EFAULT : 0;
		
        case SNDCTL_DSP_NONBLOCK:
                file->f_flags |= O_NONBLOCK;
                return 0;

        case SNDCTL_DSP_GETODELAY: /* What should this exactly do ? ,
				      ATM it is just abinfo.bytes */
		if (!(file->f_mode & FMODE_WRITE))
			return -EINVAL;

		val = rme96xx_gethwptr(dma->s,0);
		count = val - dma->readptr;
		if (count < 0)
			count += s->fragsize<<1;

		return put_user(count, p);


/* check out how to use mmaped mode (can only be blocked !!!) */
        case SNDCTL_DSP_GETIPTR:
		if (!(file->f_mode & FMODE_READ))
			return -EINVAL;
		val = rme96xx_gethwptr(dma->s,0);
		spin_lock_irqsave(&s->lock,flags);
                cinfo.bytes = s->fragsize<<1;
		count = val - dma->readptr;
		if (count < 0)
			count += s->fragsize<<1;

                cinfo.blocks = (count > s->fragsize); 
                cinfo.ptr = val;
		if (dma->mmapped)
			dma->readptr &= s->fragsize<<1;
		spin_unlock_irqrestore(&s->lock,flags);

                if (copy_to_user(argp, &cinfo, sizeof(cinfo)))
			return -EFAULT;
		return 0;

        case SNDCTL_DSP_GETOPTR:
		if (!(file->f_mode & FMODE_READ))
			return -EINVAL;
		val = rme96xx_gethwptr(dma->s,0);
		spin_lock_irqsave(&s->lock,flags);
                cinfo.bytes = s->fragsize<<1;
		count = val - dma->writeptr;
		if (count < 0)
			count += s->fragsize<<1;

                cinfo.blocks = (count > s->fragsize); 
                cinfo.ptr = val;
		if (dma->mmapped)
			dma->writeptr &= s->fragsize<<1;
		spin_unlock_irqrestore(&s->lock,flags);
                if (copy_to_user(argp, &cinfo, sizeof(cinfo)))
			return -EFAULT;
		return 0;
        case SNDCTL_DSP_GETBLKSIZE:
	     return put_user(s->fragsize, p);

        case SNDCTL_DSP_SETFRAGMENT:
                if (get_user(val, p))
			return -EFAULT;
		val&=0xffff;
		val -= 7;
		if (val < 0) val = 0;
		if (val > 7) val = 7;
		rme96xx_setlatency(s,val);
		return 0;

        case SNDCTL_DSP_SUBDIVIDE:
#if 0
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
#endif		
		return 0;

        case SOUND_PCM_READ_RATE:
		/* HP20020201 */
		s->rate = rme96xx_get_sample_rate_status(s);
		return put_user(s->rate, p);

        case SOUND_PCM_READ_CHANNELS:
		return put_user(dma->outchannels, p);

        case SOUND_PCM_READ_BITS:
		switch (dma->format) {
			case AFMT_S32_BLOCKED:
				val = 32;
				break;
			case AFMT_S16_LE:
				val = 16;
				break;
		}
		return put_user(val, p);

        case SOUND_PCM_WRITE_FILTER:
        case SNDCTL_DSP_SETSYNCRO:
        case SOUND_PCM_READ_FILTER:
                return -EINVAL;
		
	}


	return -ENODEV;
}



static int rme96xx_open(struct inode *in, struct file *f)
{
	int minor = iminor(in);
	struct list_head *list;
	int devnum;
	rme96xx_info *s;
	struct dmabuf* dma;
	DECLARE_WAITQUEUE(wait, current); 

	DBG(printk("device num %d open\n",devnum));

	nonseekable_open(in, f);
	for (list = devs.next; ; list = list->next) {
		if (list == &devs)
			return -ENODEV;
		s = list_entry(list, rme96xx_info, devs);
		for (devnum=0; devnum<devices; devnum++)
			if (!((s->dspnum[devnum] ^ minor) & ~0xf)) 
				break;
		if (devnum<devices)
			break;
	}
       	VALIDATE_STATE(s);

	dma = &s->dma[devnum];
	f->private_data = dma;
	/* wait for device to become free */
	mutex_lock(&dma->open_mutex);
	while (dma->open_mode & f->f_mode) {
		if (f->f_flags & O_NONBLOCK) {
			mutex_unlock(&dma->open_mutex);
			return -EBUSY;
		}
		add_wait_queue(&dma->open_wait, &wait);
		__set_current_state(TASK_INTERRUPTIBLE);
		mutex_unlock(&dma->open_mutex);
		schedule();
		remove_wait_queue(&dma->open_wait, &wait);
		set_current_state(TASK_RUNNING);
		if (signal_pending(current))
			return -ERESTARTSYS;
		mutex_lock(&dma->open_mutex);
	}

	COMM                ("hardware open")

	if (!dma->opened) rme96xx_dmabuf_init(dma->s,dma,dma->inoffset,dma->outoffset);

	dma->open_mode |= (f->f_mode & (FMODE_READ | FMODE_WRITE));
	dma->opened = 1;
	mutex_unlock(&dma->open_mutex);

	DBG(printk("device num %d open finished\n",devnum));
	return 0;
}

static int rme96xx_release(struct inode *in, struct file *file)
{
	struct dmabuf * dma = (struct dmabuf*) file->private_data;
	/* int hwp;  ... was unused   HP20020201 */
	DBG(printk("%s\n", __FUNCTION__));

	COMM          ("draining")
	if (dma->open_mode & FMODE_WRITE) {
#if 0 /* Why doesn't this work with some cards ?? */
	     hwp = rme96xx_gethwptr(dma->s,0);
	     while (rme96xx_getospace(dma,hwp)) {
		  interruptible_sleep_on(&(dma->wait));
		  hwp = rme96xx_gethwptr(dma->s,0);
	     }
#endif
	     rme96xx_clearbufs(dma);
	}

	dma->open_mode &= (~file->f_mode) & (FMODE_READ|FMODE_WRITE);

	if (!(dma->open_mode & (FMODE_READ|FMODE_WRITE))) {
	     dma->opened = 0;
	     if (dma->s->started) rme96xx_startcard(dma->s,1);
	}

	wake_up(&dma->open_wait);
	mutex_unlock(&dma->open_mutex);

	return 0;
}


static ssize_t rme96xx_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
	struct dmabuf *dma = (struct dmabuf *)file->private_data;
	ssize_t ret = 0;
	int cnt; /* number of bytes from "buffer" that will/can be used */
	int hop = count/dma->outchannels;
	int hwp;
	int exact = (file->f_flags & O_NONBLOCK); 


	if(dma == NULL || (dma->s) == NULL) 
		return -ENXIO;

	if (dma->mmapped || !dma->opened)
		return -ENXIO;

	if (!access_ok(VERIFY_READ, buffer, count))
		return -EFAULT;

	if (! (dma->open_mode  & FMODE_WRITE))
                return -ENXIO;

	if (!dma->s->started) rme96xx_startcard(dma->s,exact);
	hwp = rme96xx_gethwptr(dma->s,0);

	if(!(dma->started)){		 
		COMM          ("first write")
			
		dma->readptr = hwp;
		dma->writeptr = hwp;
		dma->started = 1;
	}

  	while (count > 0) {
		cnt = rme96xx_getospace(dma,hwp);		
		cnt>>=dma->formatshift;
		cnt*=dma->outchannels;
		if (cnt > count)
			cnt = count;

		if (cnt != 0) {
		        if (rme96xx_copyfromuser(dma,buffer,cnt,hop))
				return ret ? ret : -EFAULT;
			count -= cnt;
			buffer += cnt;
			ret += cnt;
			if (count == 0) return ret;
		}
		if (file->f_flags & O_NONBLOCK)
			return ret ? ret : -EAGAIN;
		
		if ((hwp - dma->writeptr) <= 0) {
			interruptible_sleep_on(&(dma->wait));
			
			if (signal_pending(current))
				return ret ? ret : -ERESTARTSYS;
		}			

		hwp = rme96xx_gethwptr(dma->s,exact);

	}; /* count > 0 */

	return ret;
}

static ssize_t rme96xx_read(struct file *file, char __user *buffer, size_t count, loff_t *ppos)
{ 
	struct dmabuf *dma = (struct dmabuf *)file->private_data;
	ssize_t ret = 0;
	int cnt; /* number of bytes from "buffer" that will/can be used */
	int hop = count/dma->inchannels;
	int hwp;
	int exact = (file->f_flags & O_NONBLOCK); 


	if(dma == NULL || (dma->s) == NULL) 
		return -ENXIO;

	if (dma->mmapped || !dma->opened)
		return -ENXIO;

	if (!access_ok(VERIFY_WRITE, buffer, count))
		return -EFAULT;

	if (! (dma->open_mode  & FMODE_READ))
                return -ENXIO;

	if (!dma->s->started) rme96xx_startcard(dma->s,exact);
	hwp = rme96xx_gethwptr(dma->s,0);

	if(!(dma->started)){		 
		COMM          ("first read")
		     
		dma->writeptr = hwp;
		dma->readptr = hwp;
		dma->started = 1;
	}

  	while (count > 0) {
		cnt = rme96xx_getispace(dma,hwp);		
		cnt>>=dma->formatshift;
		cnt*=dma->inchannels;

		if (cnt > count)
			cnt = count;

		if (cnt != 0) {
		        
			if (rme96xx_copytouser(dma,buffer,cnt,hop))
				return ret ? ret : -EFAULT;
			
			count -= cnt;
			buffer += cnt;
			ret += cnt;
			if (count == 0) return ret;
		}
		if (file->f_flags & O_NONBLOCK)
			return ret ? ret : -EAGAIN;
		
		if ((hwp - dma->readptr) <= 0) {
			interruptible_sleep_on(&(dma->wait));
			
			if (signal_pending(current))
				return ret ? ret : -ERESTARTSYS;
		}			
		hwp = rme96xx_gethwptr(dma->s,exact);

	}; /* count > 0 */

	return ret;
}

static int rm96xx_mmap(struct file *file, struct vm_area_struct *vma) {
	struct dmabuf *dma = (struct dmabuf *)file->private_data;
	rme96xx_info* s = dma->s;
	unsigned long size;

	VALIDATE_STATE(s);
	lock_kernel();

	if (vma->vm_pgoff != 0) {
		unlock_kernel();
		return -EINVAL;
	}
	size = vma->vm_end - vma->vm_start;
	if (size > RME96xx_DMA_MAX_SIZE) {
		unlock_kernel();
		return -EINVAL;
	}


	if (vma->vm_flags & VM_WRITE) {
		if (!s->started) rme96xx_startcard(s,1);

		if (remap_pfn_range(vma, vma->vm_start, virt_to_phys(s->playbuf + dma->outoffset*RME96xx_DMA_MAX_SIZE) >> PAGE_SHIFT, size, vma->vm_page_prot)) {
			unlock_kernel();
			return -EAGAIN;
		}
	} 
	else if (vma->vm_flags & VM_READ) {
		if (!s->started) rme96xx_startcard(s,1);
		if (remap_pfn_range(vma, vma->vm_start, virt_to_phys(s->playbuf + dma->inoffset*RME96xx_DMA_MAX_SIZE) >> PAGE_SHIFT, size, vma->vm_page_prot)) {
			unlock_kernel();
			return -EAGAIN;
		}
	} else  {
		unlock_kernel();
		return -EINVAL;
	}


/* this is the mapping */
	vma->vm_flags &= ~VM_IO;
	dma->mmapped = 1;
	unlock_kernel();
	return 0;
}

static unsigned int rme96xx_poll(struct file *file, struct poll_table_struct *wait)
{
	struct dmabuf *dma = (struct dmabuf *)file->private_data;
	rme96xx_info* s = dma->s;
	unsigned int mask = 0;
	unsigned int hwp,cnt;

        DBG(printk("rme96xx poll_wait ...\n"));
	VALIDATE_STATE(s);

	if (!s->started) {
		  mask |= POLLOUT | POLLWRNORM;
	}
	poll_wait(file, &dma->wait, wait);

	hwp = rme96xx_gethwptr(dma->s,0);

        DBG(printk("rme96xx poll: ..cnt %d > %d\n",cnt,s->fragsize));	

	cnt = rme96xx_getispace(dma,hwp);

	if (file->f_mode & FMODE_READ) 
	     if (cnt > 0)
		  mask |= POLLIN | POLLRDNORM;



	cnt = rme96xx_getospace(dma,hwp);

	if (file->f_mode & FMODE_WRITE) 
	     if (cnt > 0)
		  mask |= POLLOUT | POLLWRNORM;


//        printk("rme96xx poll_wait ...%d > %d\n",rme96xx_getospace(dma,hwp),rme96xx_getispace(dma,hwp));

	return mask;
}


static struct file_operations rme96xx_audio_fops = {
	.owner	 = THIS_MODULE,
	.read	 = rme96xx_read,
	.write	 = rme96xx_write,
	.poll	 = rme96xx_poll,
	.ioctl	 = rme96xx_ioctl,  
	.mmap	 = rm96xx_mmap,
	.open	 = rme96xx_open,  
	.release = rme96xx_release 
};

static int rme96xx_mixer_open(struct inode *inode, struct file *file)
{
	int minor = iminor(inode);
	struct list_head *list;
	rme96xx_info *s;

	COMM  ("mixer open");

	nonseekable_open(inode, file);
	for (list = devs.next; ; list = list->next) {
		if (list == &devs)
			return -ENODEV;
		s = list_entry(list, rme96xx_info, devs);
		if (s->mixer== minor)
			break;
	}
       	VALIDATE_STATE(s);
	file->private_data = s;

	COMM                       ("mixer opened")
	return 0;
}

static int rme96xx_mixer_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	rme96xx_info *s = (rme96xx_info *)file->private_data;
	u32 status;
	int spdifrate;
	void __user *argp = (void __user *)arg;
	int __user *p = argp;

	status = readl(s->iobase + RME96xx_status_register);
	/* hack to convert rev 1.5 SPDIF rate to "crystalrate" format   HP 20020201 */
	rme96xx_spdif_sample_rate(s,&spdifrate);
	status = (status & ~RME96xx_F) | ((spdifrate<<22) & RME96xx_F);

	VALIDATE_STATE(s);
	if (cmd == SOUND_MIXER_PRIVATE1) {
		rme_mixer mixer;
		if (copy_from_user(&mixer,argp,sizeof(mixer)))
			return -EFAULT;
		
		mixer.devnr &= RME96xx_MASK_DEVS;
		if (mixer.devnr >= devices)
			mixer.devnr = devices-1;
		if (file->f_mode & FMODE_WRITE && !s->dma[mixer.devnr].opened) {
			/* modify only if device not open */
			if (mixer.o_offset < 0)
				mixer.o_offset = 0;
			if (mixer.o_offset >= RME96xx_CHANNELS_PER_CARD)
				mixer.o_offset = RME96xx_CHANNELS_PER_CARD-1;
			if (mixer.i_offset < 0)
				mixer.i_offset = 0;
			if (mixer.i_offset >= RME96xx_CHANNELS_PER_CARD)
				mixer.i_offset = RME96xx_CHANNELS_PER_CARD-1;
			s->dma[mixer.devnr].outoffset = mixer.o_offset;
			s->dma[mixer.devnr].inoffset = mixer.i_offset;
		}

		mixer.o_offset = s->dma[mixer.devnr].outoffset;
		mixer.i_offset = s->dma[mixer.devnr].inoffset;

		return copy_to_user(argp, &mixer, sizeof(mixer)) ? -EFAULT : 0;
	}
	if (cmd == SOUND_MIXER_PRIVATE2) {
		return put_user(status, p);
	}
	if (cmd == SOUND_MIXER_PRIVATE3) {
		u32 control;
		if (copy_from_user(&control,argp,sizeof(control)))
			return -EFAULT;
		if (file->f_mode & FMODE_WRITE) {
			s->control_register &= ~RME96xx_mixer_allowed;
			s->control_register |= control & RME96xx_mixer_allowed;
			writel(control,s->iobase + RME96xx_control_register);
		}

	     return put_user(s->control_register, p);
	}
	return -1;
}



static int rme96xx_mixer_release(struct inode *inode, struct file *file)
{
	return 0;
}

static /*const*/ struct file_operations rme96xx_mixer_fops = {
	.owner	 = THIS_MODULE,
	.ioctl	 = rme96xx_mixer_ioctl,
	.open	 = rme96xx_mixer_open,
	.release = rme96xx_mixer_release,
};
