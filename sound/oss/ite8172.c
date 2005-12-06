/*
 *      ite8172.c  --  ITE IT8172G Sound Driver.
 *
 * Copyright 2001 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *         	stevel@mvista.com or source@mvista.com
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 * Module command line parameters:
 *
 *  Supported devices:
 *  /dev/dsp    standard OSS /dev/dsp device
 *  /dev/mixer  standard OSS /dev/mixer device
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
 *      * I2S support.
 *  3. The following is not supported:
 *      * legacy audio mode.
 *  4. Support for volume button interrupts is implemented but doesn't
 *     work yet.
 *
 *  Revision history
 *    02.08.2001  Initial release
 *    06.22.2001  Added I2S support
 *    07.30.2003  Removed initialisation to zero for static variables
 *		   (spdif[NR_DEVICE], i2s_fmt[NR_DEVICE], and devindex)
 */
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
#include <linux/interrupt.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/uaccess.h>
#include <asm/it8172/it8172.h>

/* --------------------------------------------------------------------- */

#undef OSS_DOCUMENTED_MIXER_SEMANTICS
#define IT8172_DEBUG
#undef IT8172_VERBOSE_DEBUG
#define DBG(x) {}

#define IT8172_MODULE_NAME "IT8172 audio"
#define PFX IT8172_MODULE_NAME

#ifdef IT8172_DEBUG
#define dbg(format, arg...) printk(KERN_DEBUG PFX ": " format "\n" , ## arg)
#else
#define dbg(format, arg...) do {} while (0)
#endif
#define err(format, arg...) printk(KERN_ERR PFX ": " format "\n" , ## arg)
#define info(format, arg...) printk(KERN_INFO PFX ": " format "\n" , ## arg)
#define warn(format, arg...) printk(KERN_WARNING PFX ": " format "\n" , ## arg)


#define IT8172_MODULE_NAME "IT8172 audio"
#define PFX IT8172_MODULE_NAME

#ifdef IT8172_DEBUG
#define dbg(format, arg...) printk(KERN_DEBUG PFX ": " format "\n" , ## arg)
#else
#define dbg(format, arg...) do {} while (0)
#endif
#define err(format, arg...) printk(KERN_ERR PFX ": " format "\n" , ## arg)
#define info(format, arg...) printk(KERN_INFO PFX ": " format "\n" , ## arg)
#define warn(format, arg...) printk(KERN_WARNING PFX ": " format "\n" , ## arg)


static const unsigned sample_shift[] = { 0, 1, 1, 2 };


/*
 * Audio Controller register bit definitions follow. See
 * include/asm/it8172/it8172.h for register offsets.
 */

/* PCM Out Volume Reg */
#define PCMOV_PCMOM	(1<<15)	/* PCM Out Mute default 1: mute */
#define	PCMOV_PCMRCG_BIT 8	/* PCM Right channel Gain */
#define	PCMOV_PCMRCG_MASK (0x1f<<PCMOV_PCMRCG_BIT)
#define PCMOV_PCMLCG_BIT 0	/* PCM Left channel gain  */
#define PCMOV_PCMLCG_MASK 0x1f

/* FM Out Volume Reg */
#define FMOV_FMOM       (1<<15)	/* FM Out Mute default 1: mute */
#define	FMOV_FMRCG_BIT	8	/* FM Right channel Gain */
#define	FMOV_FMRCG_MASK (0x1f<<FMOV_FMRCG_BIT)
#define FMOV_FMLCG_BIT	0	/* FM Left channel gain  */
#define FMOV_FMLCG_MASK 0x1f

/* I2S Out Volume Reg */
#define I2SV_I2SOM	 (1<<15) /* I2S Out Mute default 1: mute */
#define	I2SV_I2SRCG_BIT	 8	 /* I2S Right channel Gain */
#define	I2SV_I2SRCG_MASK (0x1f<<I2SV_I2SRCG_BIT)
#define I2SV_I2SLCG_BIT	 0	 /* I2S Left channel gain  */
#define I2SV_I2SLCG_MASK 0x1f

/* Digital Recording Source Select Reg */
#define	DRSS_BIT   0
#define	DRSS_MASK  0x07
#define   DRSS_AC97_PRIM 0
#define   DRSS_FM        1
#define   DRSS_I2S       2
#define   DRSS_PCM       3
#define   DRSS_AC97_SEC  4

/* Playback/Capture Channel Control Registers */
#define	CC_SM	        (1<<15)	/* Stereo, Mone 0: mono 1: stereo */
#define	CC_DF	        (1<<14)	/* Data Format 0: 8 bit 1: 16 bit */
#define CC_FMT_BIT      14
#define CC_FMT_MASK     (0x03<<CC_FMT_BIT)
#define CC_CF_BIT       12      /* Channel format (Playback only) */
#define CC_CF_MASK      (0x03<<CC_CF_BIT)
#define	  CC_CF_2	0
#define   CC_CF_4	(1<<CC_CF_BIT)
#define   CC_CF_6	(2<<CC_CF_BIT)
#define CC_SR_BIT       8       /* sample Rate */
#define CC_SR_MASK      (0x0f<<CC_SR_BIT)
#define	  CC_SR_5500	0
#define	  CC_SR_8000	(1<<CC_SR_BIT)
#define	  CC_SR_9600	(2<<CC_SR_BIT)
#define	  CC_SR_11025	(3<<CC_SR_BIT)
#define	  CC_SR_16000	(4<<CC_SR_BIT)
#define	  CC_SR_19200	(5<<CC_SR_BIT)
#define	  CC_SR_22050	(6<<CC_SR_BIT)
#define	  CC_SR_32000	(7<<CC_SR_BIT)
#define	  CC_SR_38400	(8<<CC_SR_BIT)
#define	  CC_SR_44100	(9<<CC_SR_BIT)
#define	  CC_SR_48000	(10<<CC_SR_BIT)
#define	CC_CSP	        (1<<7)	/* Channel stop 
				 * 0: End of Current buffer
				 * 1: Immediately stop when rec stop */
#define CC_CP	        (1<<6)	/* Channel pause 0: normal, 1: pause */
#define	CC_CA	        (1<<5)	/* Channel Action 0: Stop , 1: start */
#define	CC_CB2L         (1<<2)	/* Cur. buf. 2 xfr is last 0: No, 1: Yes */
#define CC_CB1L         (1<<1)	/* Cur. buf. 1 xfr is last 0: No, 1: Yes */
#define CC_DE	        1	/* DFC/DFIFO Data Empty 1: empty, 0: not empty
				 * (Playback only)
				 */

/* Codec Control Reg */
#define CODECC_GME	(1<<9)	/* AC97 GPIO Mode enable */
#define	CODECC_ATM	(1<<8)	/* AC97 ATE test mode 0: test 1: normal */
#define	CODECC_WR	(1<<6)	/* AC97 Warn reset 1: warm reset , 0: Normal */
#define	CODECC_CR	(1<<5)	/* AC97 Cold reset 1: Cold reset , 0: Normal */


/* I2S Control Reg	*/
#define	I2SMC_SR_BIT	 6	/* I2S Sampling rate 
				 * 00: 48KHz, 01: 44.1 KHz, 10: 32 32 KHz */
#define	I2SMC_SR_MASK    (0x03<<I2SMC_SR_BIT)
#define	  I2SMC_SR_48000 0
#define	  I2SMC_SR_44100 (1<<I2SMC_SR_BIT)
#define	  I2SMC_SR_32000 (2<<I2SMC_SR_BIT)
#define	I2SMC_SRSS	 (1<<5)	/* Sample Rate Source Select 1:S/W, 0: H/W */
#define I2SMC_I2SF_BIT	 0	/* I2S Format */
#define I2SMC_I2SF_MASK  0x03
#define   I2SMC_I2SF_DAC 0
#define   I2SMC_I2SF_ADC 2
#define   I2SMC_I2SF_I2S 3


/* Volume up, Down, Mute */
#define	VS_VMP	(1<<2)	/* Volume mute 1: pushed, 0: not */
#define	VS_VDP	(1<<1)	/* Volume Down 1: pushed, 0: not */
#define VS_VUP	1	/* Volime Up 1: pushed, 0: not */

/* SRC, Mixer test control/DFC status reg */
#define SRCS_DPUSC      (1<<5)	/* DFC Playback underrun Status/clear */
#define	SRCS_DCOSC	(1<<4)	/* DFC Capture Overrun Status/clear */
#define SRCS_SIS	(1<<3)	/* SRC input select 1: Mixer, 0: Codec I/F */
#define SRCS_CDIS_BIT	0	/* Codec Data Input Select */
#define SRCS_CDIS_MASK  0x07
#define   SRCS_CDIS_MIXER 0
#define   SRCS_CDIS_PCM   1
#define   SRCS_CDIS_I2S   2
#define   SRCS_CDIS_FM    3
#define   SRCS_CDIS_DFC   4


/* Codec Index Reg command Port */
#define CIRCP_CID_BIT   10
#define CIRCP_CID_MASK  (0x03<<CIRCP_CID_BIT)
#define CIRCP_CPS	(1<<9)	/* Command Port Status 0: ready, 1: busy */
#define	CIRCP_DPVF	(1<<8)	/* Data Port Valid Flag 0: invalis, 1: valid */
#define CIRCP_RWC	(1<<7)	/* Read/write command */
#define CIRCP_CIA_BIT   0
#define CIRCP_CIA_MASK  0x007F	/* Codec Index Address */

/* Test Mode Control/Test group Select Control */

/* General Control Reg */
#define GC_VDC_BIT	6	/* Volume Division Control */
#define GC_VDC_MASK     (0x03<<GC_VDC_BIT)
#define   GC_VDC_NONE   0
#define   GC_VDC_DIV2   (1<<GC_VDC_BIT)
#define   GC_VDC_DIV4   (2<<GC_VDC_BIT)
#define	GC_SOE	        (1<<2)	/* S/PDIF Output enable */
#define	GC_SWR	        1	/* Software warn reset */

/* Interrupt mask Control Reg */
#define	IMC_VCIM	(1<<6)	/* Volume CNTL interrupt mask */
#define	IMC_CCIM	(1<<1)	/* Capture Chan. iterrupt mask */
#define	IMC_PCIM	1	/* Playback Chan. interrupt mask */

/* Interrupt status/clear reg */
#define	ISC_VCI	        (1<<6)	/* Volume CNTL interrupt 1: clears */
#define	ISC_CCI	        (1<<1)	/* Capture Chan. interrupt 1: clears  */
#define	ISC_PCI	        1	/* Playback Chan. interrupt 1: clears */

/* misc stuff */
#define POLL_COUNT   0x5000


/* --------------------------------------------------------------------- */

/*
 * Define DIGITAL1 as the I2S channel, since it is not listed in
 * soundcard.h.
 */
#define SOUND_MIXER_I2S        SOUND_MIXER_DIGITAL1
#define SOUND_MASK_I2S         SOUND_MASK_DIGITAL1
#define SOUND_MIXER_READ_I2S   MIXER_READ(SOUND_MIXER_I2S)
#define SOUND_MIXER_WRITE_I2S  MIXER_WRITE(SOUND_MIXER_I2S)

/* --------------------------------------------------------------------- */

struct it8172_state {
	/* list of it8172 devices */
	struct list_head devs;

	/* the corresponding pci_dev structure */
	struct pci_dev *dev;

	/* soundcore stuff */
	int dev_audio;

	/* hardware resources */
	unsigned long io;
	unsigned int irq;

	/* PCI ID's */
	u16 vendor;
	u16 device;
	u8 rev; /* the chip revision */

	/* options */
	int spdif_volume; /* S/PDIF output is enabled if != -1 */
	int i2s_volume;   /* current I2S out volume, in OSS format */
	int i2s_recording;/* 1 = recording from I2S, 0 = not */
    
#ifdef IT8172_DEBUG
	/* debug /proc entry */
	struct proc_dir_entry *ps;
	struct proc_dir_entry *ac97_ps;
#endif /* IT8172_DEBUG */

	struct ac97_codec codec;

	unsigned short pcc, capcc;
	unsigned dacrate, adcrate;

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
		void* nextIn;
		void* nextOut;
		int count;
		int curBufPtr;
		unsigned total_bytes;
		unsigned error; /* over/underrun */
		wait_queue_head_t wait;
		/* redundant, but makes calculations easier */
		unsigned fragsize;
		unsigned dmasize;
		unsigned fragsamples;
		/* OSS stuff */
		unsigned mapped:1;
		unsigned ready:1;
		unsigned stopped:1;
		unsigned ossfragshift;
		int ossmaxfrags;
		unsigned subdivision;
	} dma_dac, dma_adc;
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

static void it8172_delay(int msec)
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


static unsigned short
get_compat_rate(unsigned* rate)
{
	unsigned rate_out = *rate;
	unsigned short sr;
    
	if (rate_out >= 46050) {
		sr = CC_SR_48000; rate_out = 48000;
	} else if (rate_out >= 41250) {
		sr = CC_SR_44100; rate_out = 44100;
	} else if (rate_out >= 35200) {
		sr = CC_SR_38400; rate_out = 38400;
	} else if (rate_out >= 27025) {
		sr = CC_SR_32000; rate_out = 32000;
	} else if (rate_out >= 20625) {
		sr = CC_SR_22050; rate_out = 22050;
	} else if (rate_out >= 17600) {
		sr = CC_SR_19200; rate_out = 19200;
	} else if (rate_out >= 13513) {
		sr = CC_SR_16000; rate_out = 16000;
	} else if (rate_out >= 10313) {
		sr = CC_SR_11025; rate_out = 11025;
	} else if (rate_out >= 8800) {
		sr = CC_SR_9600; rate_out = 9600;
	} else if (rate_out >= 6750) {
		sr = CC_SR_8000; rate_out = 8000;
	} else {
		sr = CC_SR_5500; rate_out = 5500;
	}

	*rate = rate_out;
	return sr;
}

static void set_adc_rate(struct it8172_state *s, unsigned rate)
{
	unsigned long flags;
	unsigned short sr;
    
	sr = get_compat_rate(&rate);

	spin_lock_irqsave(&s->lock, flags);
	s->capcc &= ~CC_SR_MASK;
	s->capcc |= sr;
	outw(s->capcc, s->io+IT_AC_CAPCC);
	spin_unlock_irqrestore(&s->lock, flags);

	s->adcrate = rate;
}


static void set_dac_rate(struct it8172_state *s, unsigned rate)
{
	unsigned long flags;
	unsigned short sr;
    
	sr = get_compat_rate(&rate);

	spin_lock_irqsave(&s->lock, flags);
	s->pcc &= ~CC_SR_MASK;
	s->pcc |= sr;
	outw(s->pcc, s->io+IT_AC_PCC);
	spin_unlock_irqrestore(&s->lock, flags);

	s->dacrate = rate;
}


/* --------------------------------------------------------------------- */

static u16 rdcodec(struct ac97_codec *codec, u8 addr)
{
	struct it8172_state *s = (struct it8172_state *)codec->private_data;
	unsigned long flags;
	unsigned short circp, data;
	int i;
    
	spin_lock_irqsave(&s->lock, flags);

	for (i = 0; i < POLL_COUNT; i++)
		if (!(inw(s->io+IT_AC_CIRCP) & CIRCP_CPS))
			break;
	if (i == POLL_COUNT)
		err("rdcodec: codec ready poll expired!");

	circp = addr & CIRCP_CIA_MASK;
	circp |= (codec->id << CIRCP_CID_BIT);
	circp |= CIRCP_RWC; // read command
	outw(circp, s->io+IT_AC_CIRCP);

	/* now wait for the data */
	for (i = 0; i < POLL_COUNT; i++)
		if (inw(s->io+IT_AC_CIRCP) & CIRCP_DPVF)
			break;
	if (i == POLL_COUNT)
		err("rdcodec: read poll expired!");

	data = inw(s->io+IT_AC_CIRDP);
	spin_unlock_irqrestore(&s->lock, flags);

	return data;
}


static void wrcodec(struct ac97_codec *codec, u8 addr, u16 data)
{
	struct it8172_state *s = (struct it8172_state *)codec->private_data;
	unsigned long flags;
	unsigned short circp;
	int i;
    
	spin_lock_irqsave(&s->lock, flags);

	for (i = 0; i < POLL_COUNT; i++)
		if (!(inw(s->io+IT_AC_CIRCP) & CIRCP_CPS))
			break;
	if (i == POLL_COUNT)
		err("wrcodec: codec ready poll expired!");

	circp = addr & CIRCP_CIA_MASK;
	circp |= (codec->id << CIRCP_CID_BIT);
	circp &= ~CIRCP_RWC; // write command

	outw(data,  s->io+IT_AC_CIRDP);  // send data first
	outw(circp, s->io+IT_AC_CIRCP);

	spin_unlock_irqrestore(&s->lock, flags);
}


static void waitcodec(struct ac97_codec *codec)
{
	unsigned short temp;

	/* codec_wait is used to wait for a ready state after
	   an AC97_RESET. */
	it8172_delay(10);

	temp = rdcodec(codec, 0x26);

	// If power down, power up
	if (temp & 0x3f00) {
		// Power on
		wrcodec(codec, 0x26, 0);
		it8172_delay(100);
		// Reread
		temp = rdcodec(codec, 0x26);
	}
    
	// Check if Codec REF,ANL,DAC,ADC ready***/
	if ((temp & 0x3f0f) != 0x000f) {
		err("codec reg 26 status (0x%x) not ready!!", temp);
		return;
	}
}


/* --------------------------------------------------------------------- */

static inline void stop_adc(struct it8172_state *s)
{
	struct dmabuf* db = &s->dma_adc;
	unsigned long flags;
	unsigned char imc;
    
	if (db->stopped)
		return;

	spin_lock_irqsave(&s->lock, flags);

	s->capcc &= ~(CC_CA | CC_CP | CC_CB2L | CC_CB1L);
	s->capcc |= CC_CSP;
	outw(s->capcc, s->io+IT_AC_CAPCC);
    
	// disable capture interrupt
	imc = inb(s->io+IT_AC_IMC);
	outb(imc | IMC_CCIM, s->io+IT_AC_IMC);

	db->stopped = 1;

	spin_unlock_irqrestore(&s->lock, flags);
}	

static inline void stop_dac(struct it8172_state *s)
{
	struct dmabuf* db = &s->dma_dac;
	unsigned long flags;
	unsigned char imc;
    
	if (db->stopped)
		return;

	spin_lock_irqsave(&s->lock, flags);

	s->pcc &= ~(CC_CA | CC_CP | CC_CB2L | CC_CB1L);
	s->pcc |= CC_CSP;
	outw(s->pcc, s->io+IT_AC_PCC);
    
	// disable playback interrupt
	imc = inb(s->io+IT_AC_IMC);
	outb(imc | IMC_PCIM, s->io+IT_AC_IMC);

	db->stopped = 1;
    
	spin_unlock_irqrestore(&s->lock, flags);
}	

static void start_dac(struct it8172_state *s)
{
	struct dmabuf* db = &s->dma_dac;
	unsigned long flags;
	unsigned char imc;
	unsigned long buf1, buf2;
    
	if (!db->stopped)
		return;
    
	spin_lock_irqsave(&s->lock, flags);

	// reset Buffer 1 and 2 pointers to nextOut and nextOut+fragsize
	buf1 = virt_to_bus(db->nextOut);
	buf2 = buf1 + db->fragsize;
	if (buf2 >= db->dmaaddr + db->dmasize)
		buf2 -= db->dmasize;
    
	outl(buf1, s->io+IT_AC_PCB1STA);
	outl(buf2, s->io+IT_AC_PCB2STA);
	db->curBufPtr = IT_AC_PCB1STA;
    
	// enable playback interrupt
	imc = inb(s->io+IT_AC_IMC);
	outb(imc & ~IMC_PCIM, s->io+IT_AC_IMC);

	s->pcc &= ~(CC_CSP | CC_CP | CC_CB2L | CC_CB1L);
	s->pcc |= CC_CA;
	outw(s->pcc, s->io+IT_AC_PCC);
    
	db->stopped = 0;

	spin_unlock_irqrestore(&s->lock, flags);
}	

static void start_adc(struct it8172_state *s)
{
	struct dmabuf* db = &s->dma_adc;
	unsigned long flags;
	unsigned char imc;
	unsigned long buf1, buf2;
    
	if (!db->stopped)
		return;

	spin_lock_irqsave(&s->lock, flags);

	// reset Buffer 1 and 2 pointers to nextIn and nextIn+fragsize
	buf1 = virt_to_bus(db->nextIn);
	buf2 = buf1 + db->fragsize;
	if (buf2 >= db->dmaaddr + db->dmasize)
		buf2 -= db->dmasize;
    
	outl(buf1, s->io+IT_AC_CAPB1STA);
	outl(buf2, s->io+IT_AC_CAPB2STA);
	db->curBufPtr = IT_AC_CAPB1STA;

	// enable capture interrupt
	imc = inb(s->io+IT_AC_IMC);
	outb(imc & ~IMC_CCIM, s->io+IT_AC_IMC);

	s->capcc &= ~(CC_CSP | CC_CP | CC_CB2L | CC_CB1L);
	s->capcc |= CC_CA;
	outw(s->capcc, s->io+IT_AC_CAPCC);
    
	db->stopped = 0;

	spin_unlock_irqrestore(&s->lock, flags);
}	

/* --------------------------------------------------------------------- */

#define DMABUF_DEFAULTORDER (17-PAGE_SHIFT)
#define DMABUF_MINORDER 1

static inline void dealloc_dmabuf(struct it8172_state *s, struct dmabuf *db)
{
	struct page *page, *pend;

	if (db->rawbuf) {
		/* undo marking the pages as reserved */
		pend = virt_to_page(db->rawbuf +
				    (PAGE_SIZE << db->buforder) - 1);
		for (page = virt_to_page(db->rawbuf); page <= pend; page++)
			ClearPageReserved(page);
		pci_free_consistent(s->dev, PAGE_SIZE << db->buforder,
				    db->rawbuf, db->dmaaddr);
	}
	db->rawbuf = db->nextIn = db->nextOut = NULL;
	db->mapped = db->ready = 0;
}

static int prog_dmabuf(struct it8172_state *s, struct dmabuf *db,
		       unsigned rate, unsigned fmt, unsigned reg)
{
	int order;
	unsigned bytepersec;
	unsigned bufs;
	struct page *page, *pend;

	if (!db->rawbuf) {
		db->ready = db->mapped = 0;
		for (order = DMABUF_DEFAULTORDER;
		     order >= DMABUF_MINORDER; order--)
			if ((db->rawbuf =
			     pci_alloc_consistent(s->dev,
						  PAGE_SIZE << order,
						  &db->dmaaddr)))
				break;
		if (!db->rawbuf)
			return -ENOMEM;
		db->buforder = order;
		/* now mark the pages as reserved;
		   otherwise remap_pfn_range doesn't do what we want */
		pend = virt_to_page(db->rawbuf +
				    (PAGE_SIZE << db->buforder) - 1);
		for (page = virt_to_page(db->rawbuf); page <= pend; page++)
			SetPageReserved(page);
	}

	db->count = 0;
	db->nextIn = db->nextOut = db->rawbuf;
    
	bytepersec = rate << sample_shift[fmt];
	bufs = PAGE_SIZE << db->buforder;
	if (db->ossfragshift) {
		if ((1000 << db->ossfragshift) < bytepersec)
			db->fragshift = ld2(bytepersec/1000);
		else
			db->fragshift = db->ossfragshift;
	} else {
		db->fragshift = ld2(bytepersec/100/(db->subdivision ?
						    db->subdivision : 1));
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
	memset(db->rawbuf, (fmt & (CC_DF>>CC_FMT_BIT)) ? 0 : 0x80, bufs);
    
#ifdef IT8172_VERBOSE_DEBUG
	dbg("rate=%d, fragsize=%d, numfrag=%d, dmasize=%d",
	    rate, db->fragsize, db->numfrag, db->dmasize);
#endif

	// set data length register
	outw(db->fragsize, s->io+reg+2);
	db->ready = 1;

	return 0;
}

static inline int prog_dmabuf_adc(struct it8172_state *s)
{
	stop_adc(s);
	return prog_dmabuf(s, &s->dma_adc, s->adcrate,
			   (s->capcc & CC_FMT_MASK) >> CC_FMT_BIT,
			   IT_AC_CAPCC);
}

static inline int prog_dmabuf_dac(struct it8172_state *s)
{
	stop_dac(s);
	return prog_dmabuf(s, &s->dma_dac, s->dacrate,
			   (s->pcc & CC_FMT_MASK) >> CC_FMT_BIT,
			   IT_AC_PCC);
}


/* hold spinlock for the following! */

static irqreturn_t it8172_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct it8172_state *s = (struct it8172_state *)dev_id;
	struct dmabuf* dac = &s->dma_dac;
	struct dmabuf* adc = &s->dma_adc;
	unsigned char isc, vs;
	unsigned short vol, mute;
	unsigned long newptr;
    
	spin_lock(&s->lock);

	isc = inb(s->io+IT_AC_ISC);

	/* fastpath out, to ease interrupt sharing */
	if (!(isc & (ISC_VCI | ISC_CCI | ISC_PCI))) {
		spin_unlock(&s->lock);
		return IRQ_NONE;
	}
    
	/* clear audio interrupts first */
	outb(isc | ISC_VCI | ISC_CCI | ISC_PCI, s->io+IT_AC_ISC);
    
	/* handle volume button events (ignore if S/PDIF enabled) */
	if ((isc & ISC_VCI) && s->spdif_volume == -1) {
		vs = inb(s->io+IT_AC_VS);
		outb(0, s->io+IT_AC_VS);
		vol = inw(s->io+IT_AC_PCMOV);
		mute = vol & PCMOV_PCMOM;
		vol &= PCMOV_PCMLCG_MASK;
		if ((vs & VS_VUP) && vol > 0)
			vol--;
		if ((vs & VS_VDP) && vol < 0x1f)
			vol++;
		vol |= (vol << PCMOV_PCMRCG_BIT);
		if (vs & VS_VMP)
			vol |= (mute ^ PCMOV_PCMOM);
		outw(vol, s->io+IT_AC_PCMOV);
	}
    
	/* update capture pointers */
	if (isc & ISC_CCI) {
		if (adc->count > adc->dmasize - adc->fragsize) {
			// Overrun. Stop ADC and log the error
			stop_adc(s);
			adc->error++;
			dbg("adc overrun");
		} else {
			newptr = virt_to_bus(adc->nextIn) + 2*adc->fragsize;
			if (newptr >= adc->dmaaddr + adc->dmasize)
				newptr -= adc->dmasize;
	    
			outl(newptr, s->io+adc->curBufPtr);
			adc->curBufPtr = (adc->curBufPtr == IT_AC_CAPB1STA) ?
				IT_AC_CAPB2STA : IT_AC_CAPB1STA;
	    
			adc->nextIn += adc->fragsize;
			if (adc->nextIn >= adc->rawbuf + adc->dmasize)
				adc->nextIn -= adc->dmasize;
	    
			adc->count += adc->fragsize;
			adc->total_bytes += adc->fragsize;

			/* wake up anybody listening */
			if (waitqueue_active(&adc->wait))
				wake_up_interruptible(&adc->wait);
		}
	}
    
	/* update playback pointers */
	if (isc & ISC_PCI) {
		newptr = virt_to_bus(dac->nextOut) + 2*dac->fragsize;
		if (newptr >= dac->dmaaddr + dac->dmasize)
			newptr -= dac->dmasize;
	
		outl(newptr, s->io+dac->curBufPtr);
		dac->curBufPtr = (dac->curBufPtr == IT_AC_PCB1STA) ?
			IT_AC_PCB2STA : IT_AC_PCB1STA;
	
		dac->nextOut += dac->fragsize;
		if (dac->nextOut >= dac->rawbuf + dac->dmasize)
			dac->nextOut -= dac->dmasize;
	
		dac->count -= dac->fragsize;
		dac->total_bytes += dac->fragsize;

		/* wake up anybody listening */
		if (waitqueue_active(&dac->wait))
			wake_up_interruptible(&dac->wait);
	
		if (dac->count <= 0)
			stop_dac(s);
	}
    
	spin_unlock(&s->lock);
	return IRQ_HANDLED;
}

/* --------------------------------------------------------------------- */

static int it8172_open_mixdev(struct inode *inode, struct file *file)
{
	int minor = iminor(inode);
	struct list_head *list;
	struct it8172_state *s;

	for (list = devs.next; ; list = list->next) {
		if (list == &devs)
			return -ENODEV;
		s = list_entry(list, struct it8172_state, devs);
		if (s->codec.dev_mixer == minor)
			break;
	}
	file->private_data = s;
	return nonseekable_open(inode, file);
}

static int it8172_release_mixdev(struct inode *inode, struct file *file)
{
	return 0;
}


static u16
cvt_ossvol(unsigned int gain)
{
	u16 ret;
    
	if (gain == 0)
		return 0;
    
	if (gain > 100)
		gain = 100;
    
	ret = (100 - gain + 32) / 4;
	ret = ret > 31 ? 31 : ret;
	return ret;
}


static int mixdev_ioctl(struct ac97_codec *codec, unsigned int cmd,
			unsigned long arg)
{
	struct it8172_state *s = (struct it8172_state *)codec->private_data;
	unsigned int left, right;
	unsigned long flags;
	int val;
	u16 vol;
    
	/*
	 * When we are in S/PDIF mode, we want to disable any analog output so
	 * we filter the master/PCM channel volume ioctls.
	 *
	 * Also filter I2S channel, which AC'97 knows nothing about.
	 */

	switch (cmd) {
	case SOUND_MIXER_WRITE_VOLUME:
		// if not in S/PDIF mode, pass to AC'97
		if (s->spdif_volume == -1)
			break;
		return 0;
	case SOUND_MIXER_WRITE_PCM:
		// if not in S/PDIF mode, pass to AC'97
		if (s->spdif_volume == -1)
			break;
		if (get_user(val, (int *)arg))
			return -EFAULT;
		right = ((val >> 8)  & 0xff);
		left = (val  & 0xff);
		if (right > 100)
			right = 100;
		if (left > 100)
			left = 100;
		s->spdif_volume = (right << 8) | left;
		vol = cvt_ossvol(left);
		vol |= (cvt_ossvol(right) << PCMOV_PCMRCG_BIT);
		if (vol == 0)
			vol = PCMOV_PCMOM; // mute
		spin_lock_irqsave(&s->lock, flags);
		outw(vol, s->io+IT_AC_PCMOV);
		spin_unlock_irqrestore(&s->lock, flags);
		return put_user(s->spdif_volume, (int *)arg);
	case SOUND_MIXER_READ_PCM:
		// if not in S/PDIF mode, pass to AC'97
		if (s->spdif_volume == -1)
			break;
		return put_user(s->spdif_volume, (int *)arg);
	case SOUND_MIXER_WRITE_I2S:
		if (get_user(val, (int *)arg))
			return -EFAULT;
		right = ((val >> 8)  & 0xff);
		left = (val  & 0xff);
		if (right > 100)
			right = 100;
		if (left > 100)
			left = 100;
		s->i2s_volume = (right << 8) | left;
		vol = cvt_ossvol(left);
		vol |= (cvt_ossvol(right) << I2SV_I2SRCG_BIT);
		if (vol == 0)
			vol = I2SV_I2SOM; // mute
		outw(vol, s->io+IT_AC_I2SV);
		return put_user(s->i2s_volume, (int *)arg);
	case SOUND_MIXER_READ_I2S:
		return put_user(s->i2s_volume, (int *)arg);
	case SOUND_MIXER_WRITE_RECSRC:
		if (get_user(val, (int *)arg))
			return -EFAULT;
		if (val & SOUND_MASK_I2S) {
			s->i2s_recording = 1;
			outb(DRSS_I2S, s->io+IT_AC_DRSS);
			return 0;
		} else {
			s->i2s_recording = 0;
			outb(DRSS_AC97_PRIM, s->io+IT_AC_DRSS);
			// now let AC'97 select record source
			break;
		}
	case SOUND_MIXER_READ_RECSRC:
		if (s->i2s_recording)
			return put_user(SOUND_MASK_I2S, (int *)arg);
		else
			// let AC'97 report recording source
			break;
	}

	return codec->mixer_ioctl(codec, cmd, arg);
}

static int it8172_ioctl_mixdev(struct inode *inode, struct file *file,
			       unsigned int cmd, unsigned long arg)
{
	struct it8172_state *s = (struct it8172_state *)file->private_data;
	struct ac97_codec *codec = &s->codec;

	return mixdev_ioctl(codec, cmd, arg);
}

static /*const*/ struct file_operations it8172_mixer_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.ioctl		= it8172_ioctl_mixdev,
	.open		= it8172_open_mixdev,
	.release	= it8172_release_mixdev,
};

/* --------------------------------------------------------------------- */

static int drain_dac(struct it8172_state *s, int nonblock)
{
	unsigned long flags;
	int count, tmo;
	
	if (s->dma_dac.mapped || !s->dma_dac.ready || s->dma_dac.stopped)
		return 0;

	for (;;) {
		spin_lock_irqsave(&s->lock, flags);
		count = s->dma_dac.count;
		spin_unlock_irqrestore(&s->lock, flags);
		if (count <= 0)
			break;
		if (signal_pending(current))
			break;
		//if (nonblock)
		//return -EBUSY;
		tmo = 1000 * count / s->dacrate;
		tmo >>= sample_shift[(s->pcc & CC_FMT_MASK) >> CC_FMT_BIT];
		it8172_delay(tmo);
	}
	if (signal_pending(current))
		return -ERESTARTSYS;
	return 0;
}

/* --------------------------------------------------------------------- */


/*
 * Copy audio data to/from user buffer from/to dma buffer, taking care
 * that we wrap when reading/writing the dma buffer. Returns actual byte
 * count written to or read from the dma buffer.
 */
static int copy_dmabuf_user(struct dmabuf *db, char* userbuf,
			    int count, int to_user)
{
	char* bufptr = to_user ? db->nextOut : db->nextIn;
	char* bufend = db->rawbuf + db->dmasize;
	
	if (bufptr + count > bufend) {
		int partial = (int)(bufend - bufptr);
		if (to_user) {
			if (copy_to_user(userbuf, bufptr, partial))
				return -EFAULT;
			if (copy_to_user(userbuf + partial, db->rawbuf,
					 count - partial))
				return -EFAULT;
		} else {
			if (copy_from_user(bufptr, userbuf, partial))
				return -EFAULT;
			if (copy_from_user(db->rawbuf,
					   userbuf + partial,
					   count - partial))
				return -EFAULT;
		}
	} else {
		if (to_user) {
			if (copy_to_user(userbuf, bufptr, count))
				return -EFAULT;
		} else {
			if (copy_from_user(bufptr, userbuf, count))
				return -EFAULT;
		}
	}
	
	return count;
}


static ssize_t it8172_read(struct file *file, char *buffer,
			   size_t count, loff_t *ppos)
{
	struct it8172_state *s = (struct it8172_state *)file->private_data;
	struct dmabuf *db = &s->dma_adc;
	ssize_t ret;
	unsigned long flags;
	int cnt, remainder, avail;

	if (db->mapped)
		return -ENXIO;
	if (!access_ok(VERIFY_WRITE, buffer, count))
		return -EFAULT;
	ret = 0;

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

		// copy from nextOut to user
		if ((cnt = copy_dmabuf_user(db, buffer, count > avail ?
					    avail : count, 1)) < 0) {
			if (!ret)
				ret = -EFAULT;
			return ret;
		}

		spin_lock_irqsave(&s->lock, flags);
		db->count -= cnt;
		spin_unlock_irqrestore(&s->lock, flags);

		db->nextOut += cnt;
		if (db->nextOut >= db->rawbuf + db->dmasize)
			db->nextOut -= db->dmasize;	

		count -= cnt;
		buffer += cnt;
		ret += cnt;
	} // while (count > 0)

	/*
	 * See if the dma buffer count after this read call is
	 * aligned on a fragsize boundary. If not, read from
	 * buffer until we reach a boundary, and let's hope this
	 * is just the last remainder of an audio record. If not
	 * it means the user is not reading in fragsize chunks, in
	 * which case it's his/her fault that there are audio gaps
	 * in their record.
	 */
	spin_lock_irqsave(&s->lock, flags);
	remainder = db->count % db->fragsize;
	if (remainder) {
		db->nextOut += remainder;
		if (db->nextOut >= db->rawbuf + db->dmasize)
			db->nextOut -= db->dmasize;
		db->count -= remainder;
	}
	spin_unlock_irqrestore(&s->lock, flags);

	return ret;
}

static ssize_t it8172_write(struct file *file, const char *buffer,
			    size_t count, loff_t *ppos)
{
	struct it8172_state *s = (struct it8172_state *)file->private_data;
	struct dmabuf *db = &s->dma_dac;
	ssize_t ret;
	unsigned long flags;
	int cnt, remainder, avail;

	if (db->mapped)
		return -ENXIO;
	if (!access_ok(VERIFY_READ, buffer, count))
		return -EFAULT;
	ret = 0;
    
	while (count > 0) {
		// wait for space in playback buffer
		do {
			spin_lock_irqsave(&s->lock, flags);
			avail = db->dmasize - db->count;
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
	
		// copy to nextIn
		if ((cnt = copy_dmabuf_user(db, (char*)buffer,
					    count > avail ?
					    avail : count, 0)) < 0) {
			if (!ret)
				ret = -EFAULT;
			return ret;
		}

		spin_lock_irqsave(&s->lock, flags);
		db->count += cnt;
		if (db->stopped)
			start_dac(s);
		spin_unlock_irqrestore(&s->lock, flags);
	
		db->nextIn += cnt;
		if (db->nextIn >= db->rawbuf + db->dmasize)
			db->nextIn -= db->dmasize;
	
		count -= cnt;
		buffer += cnt;
		ret += cnt;
	} // while (count > 0)
	
	/*
	 * See if the dma buffer count after this write call is
	 * aligned on a fragsize boundary. If not, fill buffer
	 * with silence to the next boundary, and let's hope this
	 * is just the last remainder of an audio playback. If not
	 * it means the user is not sending us fragsize chunks, in
	 * which case it's his/her fault that there are audio gaps
	 * in their playback.
	 */
	spin_lock_irqsave(&s->lock, flags);
	remainder = db->count % db->fragsize;
	if (remainder) {
		int fill_cnt = db->fragsize - remainder;
		memset(db->nextIn, 0, fill_cnt);
		db->nextIn += fill_cnt;
		if (db->nextIn >= db->rawbuf + db->dmasize)
			db->nextIn -= db->dmasize;
		db->count += fill_cnt;
	}
	spin_unlock_irqrestore(&s->lock, flags);

	return ret;
}

/* No kernel lock - we have our own spinlock */
static unsigned int it8172_poll(struct file *file,
				struct poll_table_struct *wait)
{
	struct it8172_state *s = (struct it8172_state *)file->private_data;
	unsigned long flags;
	unsigned int mask = 0;

	if (file->f_mode & FMODE_WRITE) {
		if (!s->dma_dac.ready)
			return 0;
		poll_wait(file, &s->dma_dac.wait, wait);
	}
	if (file->f_mode & FMODE_READ) {
		if (!s->dma_adc.ready)
			return 0;
		poll_wait(file, &s->dma_adc.wait, wait);
	}
	
	spin_lock_irqsave(&s->lock, flags);
	if (file->f_mode & FMODE_READ) {
		if (s->dma_adc.count >= (signed)s->dma_adc.fragsize)
			mask |= POLLIN | POLLRDNORM;
	}
	if (file->f_mode & FMODE_WRITE) {
		if (s->dma_dac.mapped) {
			if (s->dma_dac.count >= (signed)s->dma_dac.fragsize) 
				mask |= POLLOUT | POLLWRNORM;
		} else {
			if ((signed)s->dma_dac.dmasize >=
			    s->dma_dac.count + (signed)s->dma_dac.fragsize)
				mask |= POLLOUT | POLLWRNORM;
		}
	}
	spin_unlock_irqrestore(&s->lock, flags);
	return mask;
}

static int it8172_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct it8172_state *s = (struct it8172_state *)file->private_data;
	struct dmabuf *db;
	unsigned long size;

	lock_kernel();
	if (vma->vm_flags & VM_WRITE)
		db = &s->dma_dac;
	else if (vma->vm_flags & VM_READ)
		db = &s->dma_adc;
	else {
		unlock_kernel();
		return -EINVAL;
	}
	if (vma->vm_pgoff != 0) {
		unlock_kernel();
		return -EINVAL;
	}
	size = vma->vm_end - vma->vm_start;
	if (size > (PAGE_SIZE << db->buforder)) {
		unlock_kernel();
		return -EINVAL;
	}
	if (remap_pfn_range(vma, vma->vm_start,
			     virt_to_phys(db->rawbuf) >> PAGE_SHIFT,
			     size, vma->vm_page_prot)) {
		unlock_kernel();
		return -EAGAIN;
	}
	db->mapped = 1;
	unlock_kernel();
	return 0;
}


#ifdef IT8172_VERBOSE_DEBUG
static struct ioctl_str_t {
	unsigned int cmd;
	const char* str;
} ioctl_str[] = {
	{SNDCTL_DSP_RESET, "SNDCTL_DSP_RESET"},
	{SNDCTL_DSP_SYNC, "SNDCTL_DSP_SYNC"},
	{SNDCTL_DSP_SPEED, "SNDCTL_DSP_SPEED"},
	{SNDCTL_DSP_STEREO, "SNDCTL_DSP_STEREO"},
	{SNDCTL_DSP_GETBLKSIZE, "SNDCTL_DSP_GETBLKSIZE"},
	{SNDCTL_DSP_SAMPLESIZE, "SNDCTL_DSP_SAMPLESIZE"},
	{SNDCTL_DSP_CHANNELS, "SNDCTL_DSP_CHANNELS"},
	{SOUND_PCM_WRITE_CHANNELS, "SOUND_PCM_WRITE_CHANNELS"},
	{SOUND_PCM_WRITE_FILTER, "SOUND_PCM_WRITE_FILTER"},
	{SNDCTL_DSP_POST, "SNDCTL_DSP_POST"},
	{SNDCTL_DSP_SUBDIVIDE, "SNDCTL_DSP_SUBDIVIDE"},
	{SNDCTL_DSP_SETFRAGMENT, "SNDCTL_DSP_SETFRAGMENT"},
	{SNDCTL_DSP_GETFMTS, "SNDCTL_DSP_GETFMTS"},
	{SNDCTL_DSP_SETFMT, "SNDCTL_DSP_SETFMT"},
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

static int it8172_ioctl(struct inode *inode, struct file *file,
			unsigned int cmd, unsigned long arg)
{
	struct it8172_state *s = (struct it8172_state *)file->private_data;
	unsigned long flags;
	audio_buf_info abinfo;
	count_info cinfo;
	int count;
	int val, mapped, ret, diff;

	mapped = ((file->f_mode & FMODE_WRITE) && s->dma_dac.mapped) ||
		((file->f_mode & FMODE_READ) && s->dma_adc.mapped);

#ifdef IT8172_VERBOSE_DEBUG
	for (count=0; count<sizeof(ioctl_str)/sizeof(ioctl_str[0]); count++) {
		if (ioctl_str[count].cmd == cmd)
			break;
	}
	if (count < sizeof(ioctl_str)/sizeof(ioctl_str[0]))
		dbg("ioctl %s, arg=0x%08x",
		    ioctl_str[count].str, (unsigned int)arg);
	else
		dbg("ioctl unknown, 0x%x", cmd);
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
		return put_user(DSP_CAP_DUPLEX | DSP_CAP_REALTIME |
				DSP_CAP_TRIGGER | DSP_CAP_MMAP, (int *)arg);
		
	case SNDCTL_DSP_RESET:
		if (file->f_mode & FMODE_WRITE) {
			stop_dac(s);
			synchronize_irq(s->irq);
			s->dma_dac.count = s->dma_dac.total_bytes = 0;
			s->dma_dac.nextIn = s->dma_dac.nextOut =
				s->dma_dac.rawbuf;
		}
		if (file->f_mode & FMODE_READ) {
			stop_adc(s);
			synchronize_irq(s->irq);
			s->dma_adc.count = s->dma_adc.total_bytes = 0;
			s->dma_adc.nextIn = s->dma_adc.nextOut =
				s->dma_adc.rawbuf;
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
				s->adcrate : s->dacrate, (int *)arg);

	case SNDCTL_DSP_STEREO:
		if (get_user(val, (int *)arg))
			return -EFAULT;
		if (file->f_mode & FMODE_READ) {
			stop_adc(s);
			if (val)
				s->capcc |= CC_SM;
			else
				s->capcc &= ~CC_SM;
			outw(s->capcc, s->io+IT_AC_CAPCC);
			if ((ret = prog_dmabuf_adc(s)))
				return ret;
		}
		if (file->f_mode & FMODE_WRITE) {
			stop_dac(s);
			if (val)
				s->pcc |= CC_SM;
			else
				s->pcc &= ~CC_SM;
			outw(s->pcc, s->io+IT_AC_PCC);
			if ((ret = prog_dmabuf_dac(s)))
				return ret;
		}
		return 0;

	case SNDCTL_DSP_CHANNELS:
		if (get_user(val, (int *)arg))
			return -EFAULT;
		if (val != 0) {
			if (file->f_mode & FMODE_READ) {
				stop_adc(s);
				if (val >= 2) {
					val = 2;
					s->capcc |= CC_SM;
				}
				else
					s->capcc &= ~CC_SM;
				outw(s->capcc, s->io+IT_AC_CAPCC);
				if ((ret = prog_dmabuf_adc(s)))
					return ret;
			}
			if (file->f_mode & FMODE_WRITE) {
				stop_dac(s);
				switch (val) {
				case 1:
					s->pcc &= ~CC_SM;
					break;
				case 2:
					s->pcc |= CC_SM;
					break;
				default:
					// FIX! support multichannel???
					val = 2;
					s->pcc |= CC_SM;
					break;
				}
				outw(s->pcc, s->io+IT_AC_PCC);
				if ((ret = prog_dmabuf_dac(s)))
					return ret;
			}
		}
		return put_user(val, (int *)arg);
		
	case SNDCTL_DSP_GETFMTS: /* Returns a mask */
		return put_user(AFMT_S16_LE|AFMT_U8, (int *)arg);
		
	case SNDCTL_DSP_SETFMT: /* Selects ONE fmt*/
		if (get_user(val, (int *)arg))
			return -EFAULT;
		if (val != AFMT_QUERY) {
			if (file->f_mode & FMODE_READ) {
				stop_adc(s);
				if (val == AFMT_S16_LE)
					s->capcc |= CC_DF;
				else {
					val = AFMT_U8;
					s->capcc &= ~CC_DF;
				}
				outw(s->capcc, s->io+IT_AC_CAPCC);
				if ((ret = prog_dmabuf_adc(s)))
					return ret;
			}
			if (file->f_mode & FMODE_WRITE) {
				stop_dac(s);
				if (val == AFMT_S16_LE)
					s->pcc |= CC_DF;
				else {
					val = AFMT_U8;
					s->pcc &= ~CC_DF;
				}
				outw(s->pcc, s->io+IT_AC_PCC);
				if ((ret = prog_dmabuf_dac(s)))
					return ret;
			}
		} else {
			if (file->f_mode & FMODE_READ)
				val = (s->capcc & CC_DF) ?
					AFMT_S16_LE : AFMT_U8;
			else
				val = (s->pcc & CC_DF) ?
					AFMT_S16_LE : AFMT_U8;
		}
		return put_user(val, (int *)arg);
		
	case SNDCTL_DSP_POST:
		return 0;

	case SNDCTL_DSP_GETTRIGGER:
		val = 0;
		spin_lock_irqsave(&s->lock, flags);
		if (file->f_mode & FMODE_READ && !s->dma_adc.stopped)
			val |= PCM_ENABLE_INPUT;
		if (file->f_mode & FMODE_WRITE && !s->dma_dac.stopped)
			val |= PCM_ENABLE_OUTPUT;
		spin_unlock_irqrestore(&s->lock, flags);
		return put_user(val, (int *)arg);
		
	case SNDCTL_DSP_SETTRIGGER:
		if (get_user(val, (int *)arg))
			return -EFAULT;
		if (file->f_mode & FMODE_READ) {
			if (val & PCM_ENABLE_INPUT)
				start_adc(s);
			else
				stop_adc(s);
		}
		if (file->f_mode & FMODE_WRITE) {
			if (val & PCM_ENABLE_OUTPUT)
				start_dac(s);
			else
				stop_dac(s);
		}
		return 0;

	case SNDCTL_DSP_GETOSPACE:
		if (!(file->f_mode & FMODE_WRITE))
			return -EINVAL;
		abinfo.fragsize = s->dma_dac.fragsize;
		spin_lock_irqsave(&s->lock, flags);
		count = s->dma_dac.count;
		if (!s->dma_dac.stopped)
			count -= (s->dma_dac.fragsize -
				  inw(s->io+IT_AC_PCDL));
		spin_unlock_irqrestore(&s->lock, flags);
		if (count < 0)
			count = 0;
		abinfo.bytes = s->dma_dac.dmasize - count;
		abinfo.fragstotal = s->dma_dac.numfrag;
		abinfo.fragments = abinfo.bytes >> s->dma_dac.fragshift;      
		return copy_to_user((void *)arg, &abinfo, sizeof(abinfo)) ?
			-EFAULT : 0;

	case SNDCTL_DSP_GETISPACE:
		if (!(file->f_mode & FMODE_READ))
			return -EINVAL;
		abinfo.fragsize = s->dma_adc.fragsize;
		spin_lock_irqsave(&s->lock, flags);
		count = s->dma_adc.count;
		if (!s->dma_adc.stopped)
			count += (s->dma_adc.fragsize -
				  inw(s->io+IT_AC_CAPCDL));
		spin_unlock_irqrestore(&s->lock, flags);
		if (count < 0)
			count = 0;
		abinfo.bytes = count;
		abinfo.fragstotal = s->dma_adc.numfrag;
		abinfo.fragments = abinfo.bytes >> s->dma_adc.fragshift;      
		return copy_to_user((void *)arg, &abinfo, sizeof(abinfo)) ?
			-EFAULT : 0;
		
	case SNDCTL_DSP_NONBLOCK:
		file->f_flags |= O_NONBLOCK;
		return 0;

	case SNDCTL_DSP_GETODELAY:
		if (!(file->f_mode & FMODE_WRITE))
			return -EINVAL;
		spin_lock_irqsave(&s->lock, flags);
		count = s->dma_dac.count;
		if (!s->dma_dac.stopped)
			count -= (s->dma_dac.fragsize -
				  inw(s->io+IT_AC_PCDL));
		spin_unlock_irqrestore(&s->lock, flags);
		if (count < 0)
			count = 0;
		return put_user(count, (int *)arg);

	case SNDCTL_DSP_GETIPTR:
		if (!(file->f_mode & FMODE_READ))
			return -EINVAL;
		spin_lock_irqsave(&s->lock, flags);
		cinfo.bytes = s->dma_adc.total_bytes;
		count = s->dma_adc.count;
		if (!s->dma_adc.stopped) {
			diff = s->dma_adc.fragsize - inw(s->io+IT_AC_CAPCDL);
			count += diff;
			cinfo.bytes += diff;
			cinfo.ptr = inl(s->io+s->dma_adc.curBufPtr) -
				s->dma_adc.dmaaddr;
		} else
			cinfo.ptr = virt_to_bus(s->dma_adc.nextIn) -
				s->dma_adc.dmaaddr;
		if (s->dma_adc.mapped)
			s->dma_adc.count &= s->dma_adc.fragsize-1;
		spin_unlock_irqrestore(&s->lock, flags);
		if (count < 0)
			count = 0;
		cinfo.blocks = count >> s->dma_adc.fragshift;
		if (copy_to_user((void *)arg, &cinfo, sizeof(cinfo)))
			return -EFAULT;
		return 0;

	case SNDCTL_DSP_GETOPTR:
		if (!(file->f_mode & FMODE_READ))
			return -EINVAL;
		spin_lock_irqsave(&s->lock, flags);
		cinfo.bytes = s->dma_dac.total_bytes;
		count = s->dma_dac.count;
		if (!s->dma_dac.stopped) {
			diff = s->dma_dac.fragsize - inw(s->io+IT_AC_CAPCDL);
			count -= diff;
			cinfo.bytes += diff;
			cinfo.ptr = inl(s->io+s->dma_dac.curBufPtr) -
				s->dma_dac.dmaaddr;
		} else
			cinfo.ptr = virt_to_bus(s->dma_dac.nextOut) -
				s->dma_dac.dmaaddr;
		if (s->dma_dac.mapped)
			s->dma_dac.count &= s->dma_dac.fragsize-1;
		spin_unlock_irqrestore(&s->lock, flags);
		if (count < 0)
			count = 0;
		cinfo.blocks = count >> s->dma_dac.fragshift;
		if (copy_to_user((void *)arg, &cinfo, sizeof(cinfo)))
			return -EFAULT;
		return 0;

	case SNDCTL_DSP_GETBLKSIZE:
		if (file->f_mode & FMODE_WRITE)
			return put_user(s->dma_dac.fragsize, (int *)arg);
		else
			return put_user(s->dma_adc.fragsize, (int *)arg);

	case SNDCTL_DSP_SETFRAGMENT:
		if (get_user(val, (int *)arg))
			return -EFAULT;
		if (file->f_mode & FMODE_READ) {
			stop_adc(s);
			s->dma_adc.ossfragshift = val & 0xffff;
			s->dma_adc.ossmaxfrags = (val >> 16) & 0xffff;
			if (s->dma_adc.ossfragshift < 4)
				s->dma_adc.ossfragshift = 4;
			if (s->dma_adc.ossfragshift > 15)
				s->dma_adc.ossfragshift = 15;
			if (s->dma_adc.ossmaxfrags < 4)
				s->dma_adc.ossmaxfrags = 4;
			if ((ret = prog_dmabuf_adc(s)))
				return ret;
		}
		if (file->f_mode & FMODE_WRITE) {
			stop_dac(s);
			s->dma_dac.ossfragshift = val & 0xffff;
			s->dma_dac.ossmaxfrags = (val >> 16) & 0xffff;
			if (s->dma_dac.ossfragshift < 4)
				s->dma_dac.ossfragshift = 4;
			if (s->dma_dac.ossfragshift > 15)
				s->dma_dac.ossfragshift = 15;
			if (s->dma_dac.ossmaxfrags < 4)
				s->dma_dac.ossmaxfrags = 4;
			if ((ret = prog_dmabuf_dac(s)))
				return ret;
		}
		return 0;

	case SNDCTL_DSP_SUBDIVIDE:
		if ((file->f_mode & FMODE_READ && s->dma_adc.subdivision) ||
		    (file->f_mode & FMODE_WRITE && s->dma_dac.subdivision))
			return -EINVAL;
		if (get_user(val, (int *)arg))
			return -EFAULT;
		if (val != 1 && val != 2 && val != 4)
			return -EINVAL;
		if (file->f_mode & FMODE_READ) {
			stop_adc(s);
			s->dma_adc.subdivision = val;
			if ((ret = prog_dmabuf_adc(s)))
				return ret;
		}
		if (file->f_mode & FMODE_WRITE) {
			stop_dac(s);
			s->dma_dac.subdivision = val;
			if ((ret = prog_dmabuf_dac(s)))
				return ret;
		}
		return 0;

	case SOUND_PCM_READ_RATE:
		return put_user((file->f_mode & FMODE_READ) ?
				s->adcrate : s->dacrate, (int *)arg);

	case SOUND_PCM_READ_CHANNELS:
		if (file->f_mode & FMODE_READ)
			return put_user((s->capcc & CC_SM) ? 2 : 1,
					(int *)arg);
		else
			return put_user((s->pcc & CC_SM) ? 2 : 1,
					(int *)arg);
	    
	case SOUND_PCM_READ_BITS:
		if (file->f_mode & FMODE_READ)
			return put_user((s->capcc & CC_DF) ? 16 : 8,
					(int *)arg);
		else
			return put_user((s->pcc & CC_DF) ? 16 : 8,
					(int *)arg);

	case SOUND_PCM_WRITE_FILTER:
	case SNDCTL_DSP_SETSYNCRO:
	case SOUND_PCM_READ_FILTER:
		return -EINVAL;
	}

	return mixdev_ioctl(&s->codec, cmd, arg);
}


static int it8172_open(struct inode *inode, struct file *file)
{
	int minor = iminor(inode);
	DECLARE_WAITQUEUE(wait, current);
	unsigned long flags;
	struct list_head *list;
	struct it8172_state *s;
	int ret;
    
#ifdef IT8172_VERBOSE_DEBUG
	if (file->f_flags & O_NONBLOCK)
		dbg("%s: non-blocking", __FUNCTION__);
	else
		dbg("%s: blocking", __FUNCTION__);
#endif
	
	for (list = devs.next; ; list = list->next) {
		if (list == &devs)
			return -ENODEV;
		s = list_entry(list, struct it8172_state, devs);
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
		s->dma_adc.ossfragshift = s->dma_adc.ossmaxfrags =
			s->dma_adc.subdivision = s->dma_adc.total_bytes = 0;
		s->capcc &= ~(CC_SM | CC_DF);
		set_adc_rate(s, 8000);
		if ((minor & 0xf) == SND_DEV_DSP16)
			s->capcc |= CC_DF;
		outw(s->capcc, s->io+IT_AC_CAPCC);
		if ((ret = prog_dmabuf_adc(s))) {
			spin_unlock_irqrestore(&s->lock, flags);
			return ret;
		}
	}
	if (file->f_mode & FMODE_WRITE) {
		s->dma_dac.ossfragshift = s->dma_dac.ossmaxfrags =
			s->dma_dac.subdivision = s->dma_dac.total_bytes = 0;
		s->pcc &= ~(CC_SM | CC_DF);
		set_dac_rate(s, 8000);
		if ((minor & 0xf) == SND_DEV_DSP16)
			s->pcc |= CC_DF;
		outw(s->pcc, s->io+IT_AC_PCC);
		if ((ret = prog_dmabuf_dac(s))) {
			spin_unlock_irqrestore(&s->lock, flags);
			return ret;
		}
	}
    
	spin_unlock_irqrestore(&s->lock, flags);

	s->open_mode |= (file->f_mode & (FMODE_READ | FMODE_WRITE));
	up(&s->open_sem);
	return nonseekable_open(inode, file);
}

static int it8172_release(struct inode *inode, struct file *file)
{
	struct it8172_state *s = (struct it8172_state *)file->private_data;

#ifdef IT8172_VERBOSE_DEBUG
	dbg("%s", __FUNCTION__);
#endif
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
	s->open_mode &= ((~file->f_mode) & (FMODE_READ|FMODE_WRITE));
	up(&s->open_sem);
	wake_up(&s->open_wait);
	unlock_kernel();
	return 0;
}

static /*const*/ struct file_operations it8172_audio_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.read		= it8172_read,
	.write		= it8172_write,
	.poll		= it8172_poll,
	.ioctl		= it8172_ioctl,
	.mmap		= it8172_mmap,
	.open		= it8172_open,
	.release	= it8172_release,
};


/* --------------------------------------------------------------------- */


/* --------------------------------------------------------------------- */

/*
 * for debugging purposes, we'll create a proc device that dumps the
 * CODEC chipstate
 */

#ifdef IT8172_DEBUG
static int proc_it8172_dump (char *buf, char **start, off_t fpos,
			     int length, int *eof, void *data)
{
	struct it8172_state *s;
	int cnt, len = 0;

	if (list_empty(&devs))
		return 0;
	s = list_entry(devs.next, struct it8172_state, devs);

	/* print out header */
	len += sprintf(buf + len, "\n\t\tIT8172 Audio Debug\n\n");

	// print out digital controller state
	len += sprintf (buf + len, "IT8172 Audio Controller registers\n");
	len += sprintf (buf + len, "---------------------------------\n");
	cnt=0;
	while (cnt < 0x72) {
		if (cnt == IT_AC_PCB1STA || cnt == IT_AC_PCB2STA ||
		    cnt == IT_AC_CAPB1STA || cnt == IT_AC_CAPB2STA ||
		    cnt == IT_AC_PFDP) {
			len+= sprintf (buf + len, "reg %02x = %08x\n",
				       cnt, inl(s->io+cnt));
			cnt += 4;
		} else {
			len+= sprintf (buf + len, "reg %02x = %04x\n",
				       cnt, inw(s->io+cnt));
			cnt += 2;
		}
	}
    
	/* print out CODEC state */
	len += sprintf (buf + len, "\nAC97 CODEC registers\n");
	len += sprintf (buf + len, "----------------------\n");
	for (cnt=0; cnt <= 0x7e; cnt = cnt +2)
		len+= sprintf (buf + len, "reg %02x = %04x\n",
			       cnt, rdcodec(&s->codec, cnt));

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
#endif /* IT8172_DEBUG */

/* --------------------------------------------------------------------- */

/* maximum number of devices; only used for command line params */
#define NR_DEVICE 5

static int spdif[NR_DEVICE];
static int i2s_fmt[NR_DEVICE];

static unsigned int devindex;

MODULE_PARM(spdif, "1-" __MODULE_STRING(NR_DEVICE) "i");
MODULE_PARM_DESC(spdif, "if 1 the S/PDIF digital output is enabled");
MODULE_PARM(i2s_fmt, "1-" __MODULE_STRING(NR_DEVICE) "i");
MODULE_PARM_DESC(i2s_fmt, "the format of I2S");

MODULE_AUTHOR("Monta Vista Software, stevel@mvista.com");
MODULE_DESCRIPTION("IT8172 Audio Driver");

/* --------------------------------------------------------------------- */

static int __devinit it8172_probe(struct pci_dev *pcidev,
				  const struct pci_device_id *pciid)
{
	struct it8172_state *s;
	int i, val;
	unsigned short pcisr, vol;
	unsigned char legacy, imc;
	char proc_str[80];
    
	if (pcidev->irq == 0) 
		return -1;

	if (!(s = kmalloc(sizeof(struct it8172_state), GFP_KERNEL))) {
		err("alloc of device struct failed");
		return -1;
	}
	
	memset(s, 0, sizeof(struct it8172_state));
	init_waitqueue_head(&s->dma_adc.wait);
	init_waitqueue_head(&s->dma_dac.wait);
	init_waitqueue_head(&s->open_wait);
	init_MUTEX(&s->open_sem);
	spin_lock_init(&s->lock);
	s->dev = pcidev;
	s->io = pci_resource_start(pcidev, 0);
	s->irq = pcidev->irq;
	s->vendor = pcidev->vendor;
	s->device = pcidev->device;
	pci_read_config_byte(pcidev, PCI_REVISION_ID, &s->rev);
	s->codec.private_data = s;
	s->codec.id = 0;
	s->codec.codec_read = rdcodec;
	s->codec.codec_write = wrcodec;
	s->codec.codec_wait = waitcodec;

	if (!request_region(s->io, pci_resource_len(pcidev,0),
			    IT8172_MODULE_NAME)) {
		err("io ports %#lx->%#lx in use",
		    s->io, s->io + pci_resource_len(pcidev,0)-1);
		goto err_region;
	}
	if (request_irq(s->irq, it8172_interrupt, SA_INTERRUPT,
			IT8172_MODULE_NAME, s)) {
		err("irq %u in use", s->irq);
		goto err_irq;
	}

	info("IO at %#lx, IRQ %d", s->io, s->irq);

	/* register devices */
	if ((s->dev_audio = register_sound_dsp(&it8172_audio_fops, -1)) < 0)
		goto err_dev1;
	if ((s->codec.dev_mixer =
	     register_sound_mixer(&it8172_mixer_fops, -1)) < 0)
		goto err_dev2;

#ifdef IT8172_DEBUG
	/* initialize the debug proc device */
	s->ps = create_proc_read_entry(IT8172_MODULE_NAME, 0, NULL,
				       proc_it8172_dump, NULL);
#endif /* IT8172_DEBUG */
	
	/*
	 * Reset the Audio device using the IT8172 PCI Reset register. This
	 * creates an audible double click on a speaker connected to Line-out.
	 */
	IT_IO_READ16(IT_PM_PCISR, pcisr);
	pcisr |= IT_PM_PCISR_ACSR;
	IT_IO_WRITE16(IT_PM_PCISR, pcisr);
	/* wait up to 100msec for reset to complete */
	for (i=0; pcisr & IT_PM_PCISR_ACSR; i++) {
		it8172_delay(10);
		if (i == 10)
			break;
		IT_IO_READ16(IT_PM_PCISR, pcisr);
	}
	if (i == 10) {
		err("chip reset timeout!");
		goto err_dev3;
	}
    
	/* enable pci io and bus mastering */
	if (pci_enable_device(pcidev))
		goto err_dev3;
	pci_set_master(pcidev);

	/* get out of legacy mode */
	pci_read_config_byte (pcidev, 0x40, &legacy);
	pci_write_config_byte (pcidev, 0x40, legacy & ~1);
    
	s->spdif_volume = -1;
	/* check to see if s/pdif mode is being requested */
	if (spdif[devindex]) {
		info("enabling S/PDIF output");
		s->spdif_volume = 0;
		outb(GC_SOE, s->io+IT_AC_GC);
	} else {
		info("disabling S/PDIF output");
		outb(0, s->io+IT_AC_GC);
	}
    
	/* check to see if I2S format requested */
	if (i2s_fmt[devindex]) {
		info("setting I2S format to 0x%02x", i2s_fmt[devindex]);
		outb(i2s_fmt[devindex], s->io+IT_AC_I2SMC);
	} else {
		outb(I2SMC_I2SF_I2S, s->io+IT_AC_I2SMC);
	}

	/* cold reset the AC97 */
	outw(CODECC_CR, s->io+IT_AC_CODECC);
	udelay(1000);
	outw(0, s->io+IT_AC_CODECC);
	/* need to delay around 500msec(bleech) to give
	   some CODECs enough time to wakeup */
	it8172_delay(500);
    
	/* AC97 warm reset to start the bitclk */
	outw(CODECC_WR, s->io+IT_AC_CODECC);
	udelay(1000);
	outw(0, s->io+IT_AC_CODECC);
    
	/* codec init */
	if (!ac97_probe_codec(&s->codec))
		goto err_dev3;

	/* add I2S as allowable recording source */
	s->codec.record_sources |= SOUND_MASK_I2S;
	
	/* Enable Volume button interrupts */
	imc = inb(s->io+IT_AC_IMC);
	outb(imc & ~IMC_VCIM, s->io+IT_AC_IMC);

	/* Un-mute PCM and FM out on the controller */
	vol = inw(s->io+IT_AC_PCMOV);
	outw(vol & ~PCMOV_PCMOM, s->io+IT_AC_PCMOV);
	vol = inw(s->io+IT_AC_FMOV);
	outw(vol & ~FMOV_FMOM, s->io+IT_AC_FMOV);
    
	/* set channel defaults to 8-bit, mono, 8 Khz */
	s->pcc = 0;
	s->capcc = 0;
	set_dac_rate(s, 8000);
	set_adc_rate(s, 8000);

	/* set mic to be the recording source */
	val = SOUND_MASK_MIC;
	mixdev_ioctl(&s->codec, SOUND_MIXER_WRITE_RECSRC,
		     (unsigned long)&val);

	/* mute AC'97 master and PCM when in S/PDIF mode */
	if (s->spdif_volume != -1) {
		val = 0x0000;
		s->codec.mixer_ioctl(&s->codec, SOUND_MIXER_WRITE_VOLUME,
				     (unsigned long)&val);
		s->codec.mixer_ioctl(&s->codec, SOUND_MIXER_WRITE_PCM,
				     (unsigned long)&val);
	}
    
#ifdef IT8172_DEBUG
	sprintf(proc_str, "driver/%s/%d/ac97", IT8172_MODULE_NAME,
		s->codec.id);
	s->ac97_ps = create_proc_read_entry (proc_str, 0, NULL,
					     ac97_read_proc, &s->codec);
#endif
    
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
	unregister_sound_mixer(s->codec.dev_mixer);
 err_dev2:
	unregister_sound_dsp(s->dev_audio);
 err_dev1:
	err("cannot register misc device");
	free_irq(s->irq, s);
 err_irq:
	release_region(s->io, pci_resource_len(pcidev,0));
 err_region:
	kfree(s);
	return -1;
}

static void __devexit it8172_remove(struct pci_dev *dev)
{
	struct it8172_state *s = pci_get_drvdata(dev);

	if (!s)
		return;
	list_del(&s->devs);
#ifdef IT8172_DEBUG
	if (s->ps)
		remove_proc_entry(IT8172_MODULE_NAME, NULL);
#endif /* IT8172_DEBUG */
	synchronize_irq(s->irq);
	free_irq(s->irq, s);
	release_region(s->io, pci_resource_len(dev,0));
	unregister_sound_dsp(s->dev_audio);
	unregister_sound_mixer(s->codec.dev_mixer);
	kfree(s);
	pci_set_drvdata(dev, NULL);
}



static struct pci_device_id id_table[] = {
	{ PCI_VENDOR_ID_ITE, PCI_DEVICE_ID_ITE_IT8172G_AUDIO, PCI_ANY_ID,
	  PCI_ANY_ID, 0, 0 },
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, id_table);

static struct pci_driver it8172_driver = {
	.name = IT8172_MODULE_NAME,
	.id_table = id_table,
	.probe = it8172_probe,
	.remove = __devexit_p(it8172_remove)
};

static int __init init_it8172(void)
{
	info("version v0.5 time " __TIME__ " " __DATE__);
	return pci_module_init(&it8172_driver);
}

static void __exit cleanup_it8172(void)
{
	info("unloading");
	pci_unregister_driver(&it8172_driver);
}

module_init(init_it8172);
module_exit(cleanup_it8172);

/* --------------------------------------------------------------------- */

#ifndef MODULE

/* format is: it8172=[spdif],[i2s:<I2S format>] */

static int __init it8172_setup(char *options)
{
	char* this_opt;
	static unsigned __initdata nr_dev = 0;

	if (nr_dev >= NR_DEVICE)
		return 0;

	if (!options || !*options)
		return 0;

	while (this_opt = strsep(&options, ",")) {
		if (!*this_opt)
			continue;
		if (!strncmp(this_opt, "spdif", 5)) {
			spdif[nr_dev] = 1;
		} else if (!strncmp(this_opt, "i2s:", 4)) {
			if (!strncmp(this_opt+4, "dac", 3))
				i2s_fmt[nr_dev] = I2SMC_I2SF_DAC;
			else if (!strncmp(this_opt+4, "adc", 3))
				i2s_fmt[nr_dev] = I2SMC_I2SF_ADC;
			else if (!strncmp(this_opt+4, "i2s", 3))
				i2s_fmt[nr_dev] = I2SMC_I2SF_I2S;
		}
	}

	nr_dev++;
	return 1;
}

__setup("it8172=", it8172_setup);

#endif /* MODULE */
