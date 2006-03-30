/*
 *	ALI  ali5455 and friends ICH driver for Linux
 *	LEI HU <Lei_Hu@ali.com.tw>
 *
 *  Built from:
 *	drivers/sound/i810_audio
 *
 *  	The ALi 5455 is similar but not quite identical to the Intel ICH
 *	series of controllers. Its easier to keep the driver separated from
 *	the i810 driver.
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 *	ALi 5455 theory of operation
 *
 *	The chipset provides three DMA channels that talk to an AC97
 *	CODEC (AC97 is a digital/analog mixer standard). At its simplest
 *	you get 48Khz audio with basic volume and mixer controls. At the
 *	best you get rate adaption in the codec. We set the card up so
 *	that we never take completion interrupts but instead keep the card
 *	chasing its tail around a ring buffer. This is needed for mmap
 *	mode audio and happens to work rather well for non-mmap modes too.
 *
 *	The board has one output channel for PCM audio (supported) and
 *	a stereo line in and mono microphone input. Again these are normally
 *	locked to 48Khz only. Right now recording is not finished.
 *
 *	There is no midi support, no synth support. Use timidity. To get
 *	esd working you need to use esd -r 48000 as it won't probe 48KHz
 *	by default. mpg123 can't handle 48Khz only audio so use xmms.
 *
 *	If you need to force a specific rate set the clocking= option
 *
 */

#include <linux/module.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/sound.h>
#include <linux/slab.h>
#include <linux/soundcard.h>
#include <linux/pci.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/spinlock.h>
#include <linux/smp_lock.h>
#include <linux/ac97_codec.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>

#include <asm/uaccess.h>

#ifndef PCI_DEVICE_ID_ALI_5455
#define PCI_DEVICE_ID_ALI_5455	0x5455
#endif

#ifndef PCI_VENDOR_ID_ALI
#define PCI_VENDOR_ID_ALI	0x10b9
#endif

static int strict_clocking = 0;
static unsigned int clocking = 0;
static unsigned int codec_pcmout_share_spdif_locked = 0;
static unsigned int codec_independent_spdif_locked = 0;
static unsigned int controller_pcmout_share_spdif_locked = 0;
static unsigned int controller_independent_spdif_locked = 0;
static unsigned int globel = 0;

#define ADC_RUNNING	1
#define DAC_RUNNING	2
#define CODEC_SPDIFOUT_RUNNING 8
#define CONTROLLER_SPDIFOUT_RUNNING 4

#define SPDIF_ENABLE_OUTPUT	4	/* bits 0,1 are PCM */

#define ALI5455_FMT_16BIT	1
#define ALI5455_FMT_STEREO	2
#define ALI5455_FMT_MASK	3

#define SPDIF_ON	0x0004
#define SURR_ON		0x0010
#define CENTER_LFE_ON	0x0020
#define VOL_MUTED	0x8000


#define ALI_SPDIF_OUT_CH_STATUS 0xbf
/* the 810's array of pointers to data buffers */

struct sg_item {
#define BUSADDR_MASK	0xFFFFFFFE
	u32 busaddr;
#define CON_IOC 	0x80000000	/* interrupt on completion */
#define CON_BUFPAD	0x40000000	/* pad underrun with last sample, else 0 */
#define CON_BUFLEN_MASK	0x0000ffff	/* buffer length in samples */
	u32 control;
};

/* an instance of the ali channel */
#define SG_LEN 32
struct ali_channel {
	/* these sg guys should probably be allocated
	   separately as nocache. Must be 8 byte aligned */
	struct sg_item sg[SG_LEN];	/* 32*8 */
	u32 offset;		/* 4 */
	u32 port;		/* 4 */
	u32 used;
	u32 num;
};

/*
 * we have 3 separate dma engines.  pcm in, pcm out, and mic.
 * each dma engine has controlling registers.  These goofy
 * names are from the datasheet, but make it easy to write
 * code while leafing through it.
 */

#define ENUM_ENGINE(PRE,DIG) 									\
enum {												\
	PRE##_BDBAR =	0x##DIG##0,		/* Buffer Descriptor list Base Address */	\
	PRE##_CIV =	0x##DIG##4,		/* Current Index Value */			\
	PRE##_LVI =	0x##DIG##5,		/* Last Valid Index */				\
	PRE##_SR =	0x##DIG##6,		/* Status Register */				\
	PRE##_PICB =	0x##DIG##8,		/* Position In Current Buffer */		\
	PRE##_CR =	0x##DIG##b		/* Control Register */				\
}

ENUM_ENGINE(OFF, 0);		/* Offsets */
ENUM_ENGINE(PI, 4);		/* PCM In */
ENUM_ENGINE(PO, 5);		/* PCM Out */
ENUM_ENGINE(MC, 6);		/* Mic In */
ENUM_ENGINE(CODECSPDIFOUT, 7);	/* CODEC SPDIF OUT  */
ENUM_ENGINE(CONTROLLERSPDIFIN, A);	/* CONTROLLER SPDIF In */
ENUM_ENGINE(CONTROLLERSPDIFOUT, B);	/* CONTROLLER SPDIF OUT */


enum {
	ALI_SCR = 0x00,		/* System Control Register */
	ALI_SSR = 0x04,		/* System Status Register  */
	ALI_DMACR = 0x08,	/* DMA Control Register    */
	ALI_FIFOCR1 = 0x0c,	/* FIFO Control Register 1  */
	ALI_INTERFACECR = 0x10,	/* Interface Control Register */
	ALI_INTERRUPTCR = 0x14,	/* Interrupt control Register */
	ALI_INTERRUPTSR = 0x18,	/* Interrupt  Status Register */
	ALI_FIFOCR2 = 0x1c,	/* FIFO Control Register 2   */
	ALI_CPR = 0x20,		/* Command Port Register     */
	ALI_SPR = 0x24,		/* Status Port Register      */
	ALI_FIFOCR3 = 0x2c,	/* FIFO Control Register 3  */
	ALI_TTSR = 0x30,	/* Transmit Tag Slot Register */
	ALI_RTSR = 0x34,	/* Receive Tag Slot  Register */
	ALI_CSPSR = 0x38,	/* Command/Status Port Status Register */
	ALI_CAS = 0x3c,		/* Codec Write Semaphore Register */
	ALI_SPDIFCSR = 0xf8,	/* spdif channel status register  */
	ALI_SPDIFICS = 0xfc	/* spdif interface control/status  */
};

// x-status register(x:pcm in ,pcm out, mic in,)
/* interrupts for a dma engine */
#define DMA_INT_FIFO		(1<<4)	/* fifo under/over flow */
#define DMA_INT_COMPLETE	(1<<3)	/* buffer read/write complete and ioc set */
#define DMA_INT_LVI		(1<<2)	/* last valid done */
#define DMA_INT_CELV		(1<<1)	/* last valid is current */
#define DMA_INT_DCH		(1)	/* DMA Controller Halted (happens on LVI interrupts) */	//not eqult intel
#define DMA_INT_MASK (DMA_INT_FIFO|DMA_INT_COMPLETE|DMA_INT_LVI)

/* interrupts for the whole chip */// by interrupt status register finish

#define INT_SPDIFOUT   (1<<23)	/* controller spdif out INTERRUPT */
#define INT_SPDIFIN   (1<<22)
#define INT_CODECSPDIFOUT   (1<<19)
#define INT_MICIN   (1<<18)
#define INT_PCMOUT   (1<<17)
#define INT_PCMIN   (1<<16)
#define INT_CPRAIS   (1<<7)
#define INT_SPRAIS   (1<<5)
#define INT_GPIO    (1<<1)
#define INT_MASK   (INT_SPDIFOUT|INT_CODECSPDIFOUT|INT_MICIN|INT_PCMOUT|INT_PCMIN)

#define DRIVER_VERSION "0.02ac"

/* magic numbers to protect our data structures */
#define ALI5455_CARD_MAGIC		0x5072696E	/* "Prin" */
#define ALI5455_STATE_MAGIC		0x63657373	/* "cess" */
#define ALI5455_DMA_MASK		0xffffffff	/* DMA buffer mask for pci_alloc_consist */
#define NR_HW_CH			5	//I think 5 channel

/* maxinum number of AC97 codecs connected, AC97 2.0 defined 4 */
#define NR_AC97		2

/* Please note that an 8bit mono stream is not valid on this card, you must have a 16bit */
/* stream at a minimum for this card to be happy */
static const unsigned sample_size[] = { 1, 2, 2, 4 };
/* Samples are 16bit values, so we are shifting to a word, not to a byte, hence shift */
/* values are one less than might be expected */
static const unsigned sample_shift[] = { -1, 0, 0, 1 };

#define ALI5455
static char *card_names[] = {
	"ALI 5455"
};

static struct pci_device_id ali_pci_tbl[] = {
	{PCI_VENDOR_ID_ALI, PCI_DEVICE_ID_ALI_5455,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, ALI5455},
	{0,}
};

MODULE_DEVICE_TABLE(pci, ali_pci_tbl);

#ifdef CONFIG_PM
#define PM_SUSPENDED(card) (card->pm_suspended)
#else
#define PM_SUSPENDED(card) (0)
#endif

/* "software" or virtual channel, an instance of opened /dev/dsp */
struct ali_state {
	unsigned int magic;
	struct ali_card *card;	/* Card info */

	/* single open lock mechanism, only used for recording */
	struct mutex open_mutex;
	wait_queue_head_t open_wait;

	/* file mode */
	mode_t open_mode;

	/* virtual channel number */
	int virt;

#ifdef CONFIG_PM
	unsigned int pm_saved_dac_rate, pm_saved_adc_rate;
#endif
	struct dmabuf {
		/* wave sample stuff */
		unsigned int rate;
		unsigned char fmt, enable, trigger;

		/* hardware channel */
		struct ali_channel *read_channel;
		struct ali_channel *write_channel;
		struct ali_channel *codec_spdifout_channel;
		struct ali_channel *controller_spdifout_channel;

		/* OSS buffer management stuff */
		void *rawbuf;
		dma_addr_t dma_handle;
		unsigned buforder;
		unsigned numfrag;
		unsigned fragshift;

		/* our buffer acts like a circular ring */
		unsigned hwptr;	/* where dma last started, updated by update_ptr */
		unsigned swptr;	/* where driver last clear/filled, updated by read/write */
		int count;	/* bytes to be consumed or been generated by dma machine */
		unsigned total_bytes;	/* total bytes dmaed by hardware */

		unsigned error;	/* number of over/underruns */
		wait_queue_head_t wait;	/* put process on wait queue when no more space in buffer */

		/* redundant, but makes calculations easier */
		/* what the hardware uses */
		unsigned dmasize;
		unsigned fragsize;
		unsigned fragsamples;

		/* what we tell the user to expect */
		unsigned userfrags;
		unsigned userfragsize;

		/* OSS stuff */
		unsigned mapped:1;
		unsigned ready:1;
		unsigned update_flag;
		unsigned ossfragsize;
		unsigned ossmaxfrags;
		unsigned subdivision;
	} dmabuf;
};


struct ali_card {
	struct ali_channel channel[5];
	unsigned int magic;

	/* We keep ali5455 cards in a linked list */
	struct ali_card *next;

	/* The ali has a certain amount of cross channel interaction
	   so we use a single per card lock */
	spinlock_t lock;
	spinlock_t ac97_lock;

	/* PCI device stuff */
	struct pci_dev *pci_dev;
	u16 pci_id;
#ifdef CONFIG_PM
	u16 pm_suspended;
	int pm_saved_mixer_settings[SOUND_MIXER_NRDEVICES][NR_AC97];
#endif
	/* soundcore stuff */
	int dev_audio;

	/* structures for abstraction of hardware facilities, codecs, banks and channels */
	struct ac97_codec *ac97_codec[NR_AC97];
	struct ali_state *states[NR_HW_CH];

	u16 ac97_features;
	u16 ac97_status;
	u16 channels;

	/* hardware resources */
	unsigned long iobase;

	u32 irq;

	/* Function support */
	struct ali_channel *(*alloc_pcm_channel) (struct ali_card *);
	struct ali_channel *(*alloc_rec_pcm_channel) (struct ali_card *);
	struct ali_channel *(*alloc_rec_mic_channel) (struct ali_card *);
	struct ali_channel *(*alloc_codec_spdifout_channel) (struct ali_card *);
	struct ali_channel *(*alloc_controller_spdifout_channel) (struct  ali_card *);
	void (*free_pcm_channel) (struct ali_card *, int chan);

	/* We have a *very* long init time possibly, so use this to block */
	/* attempts to open our devices before we are ready (stops oops'es) */
	int initializing;
};


static struct ali_card *devs = NULL;

static int ali_open_mixdev(struct inode *inode, struct file *file);
static int ali_ioctl_mixdev(struct inode *inode, struct file *file,
			    unsigned int cmd, unsigned long arg);
static u16 ali_ac97_get(struct ac97_codec *dev, u8 reg);
static void ali_ac97_set(struct ac97_codec *dev, u8 reg, u16 data);

static struct ali_channel *ali_alloc_pcm_channel(struct ali_card *card)
{
	if (card->channel[1].used == 1)
		return NULL;
	card->channel[1].used = 1;
	return &card->channel[1];
}

static struct ali_channel *ali_alloc_rec_pcm_channel(struct ali_card *card)
{
	if (card->channel[0].used == 1)
		return NULL;
	card->channel[0].used = 1;
	return &card->channel[0];
}

static struct ali_channel *ali_alloc_rec_mic_channel(struct ali_card *card)
{
	if (card->channel[2].used == 1)
		return NULL;
	card->channel[2].used = 1;
	return &card->channel[2];
}

static struct ali_channel *ali_alloc_codec_spdifout_channel(struct ali_card *card)
{
	if (card->channel[3].used == 1)
		return NULL;
	card->channel[3].used = 1;
	return &card->channel[3];
}

static struct ali_channel *ali_alloc_controller_spdifout_channel(struct ali_card *card)
{
	if (card->channel[4].used == 1)
		return NULL;
	card->channel[4].used = 1;
	return &card->channel[4];
}
static void ali_free_pcm_channel(struct ali_card *card, int channel)
{
	card->channel[channel].used = 0;
}


//add support  codec spdif out 
static int ali_valid_spdif_rate(struct ac97_codec *codec, int rate)
{
	unsigned long id = 0L;

	id = (ali_ac97_get(codec, AC97_VENDOR_ID1) << 16);
	id |= ali_ac97_get(codec, AC97_VENDOR_ID2) & 0xffff;
	switch (id) {
	case 0x41445361:	/* AD1886 */
		if (rate == 48000) {
			return 1;
		}
		break;
	case 0x414c4720:	/* ALC650 */
		if (rate == 48000) {
			return 1;
		}
		break;
	default:		/* all other codecs, until we know otherwiae */
		if (rate == 48000 || rate == 44100 || rate == 32000) {
			return 1;
		}
		break;
	}
	return (0);
}

/* ali_set_spdif_output
 * 
 *  Configure the S/PDIF output transmitter. When we turn on
 *  S/PDIF, we turn off the analog output. This may not be
 *  the right thing to do.
 *
 *  Assumptions:
 *     The DSP sample rate must already be set to a supported
 *     S/PDIF rate (32kHz, 44.1kHz, or 48kHz) or we abort.
 */
static void ali_set_spdif_output(struct ali_state *state, int slots,
				 int rate)
{
	int vol;
	int aud_reg;
	struct ac97_codec *codec = state->card->ac97_codec[0];

	if (!(state->card->ac97_features & 4)) {
		state->card->ac97_status &= ~SPDIF_ON;
	} else {
		if (slots == -1) {	/* Turn off S/PDIF */
			aud_reg = ali_ac97_get(codec, AC97_EXTENDED_STATUS);
			ali_ac97_set(codec, AC97_EXTENDED_STATUS, (aud_reg & ~AC97_EA_SPDIF));

			/* If the volume wasn't muted before we turned on S/PDIF, unmute it */
			if (!(state->card->ac97_status & VOL_MUTED)) {
				aud_reg = ali_ac97_get(codec, AC97_MASTER_VOL_STEREO);
				ali_ac97_set(codec, AC97_MASTER_VOL_STEREO,
					     (aud_reg & ~VOL_MUTED));
			}
			state->card->ac97_status &= ~(VOL_MUTED | SPDIF_ON);
			return;
		}

		vol = ali_ac97_get(codec, AC97_MASTER_VOL_STEREO);
		state->card->ac97_status = vol & VOL_MUTED;

		/* Set S/PDIF transmitter sample rate */
		aud_reg = ali_ac97_get(codec, AC97_SPDIF_CONTROL);
		switch (rate) {
		case 32000:
			aud_reg = (aud_reg & AC97_SC_SPSR_MASK) | AC97_SC_SPSR_32K;
			break;
		case 44100:
			aud_reg = (aud_reg & AC97_SC_SPSR_MASK) | AC97_SC_SPSR_44K;
			break;
		case 48000:
			aud_reg = (aud_reg & AC97_SC_SPSR_MASK) | AC97_SC_SPSR_48K;
			break;
		default:
			/* turn off S/PDIF */
			aud_reg = ali_ac97_get(codec, AC97_EXTENDED_STATUS);
			ali_ac97_set(codec, AC97_EXTENDED_STATUS, (aud_reg & ~AC97_EA_SPDIF));
			state->card->ac97_status &= ~SPDIF_ON;
			return;
		}

		ali_ac97_set(codec, AC97_SPDIF_CONTROL, aud_reg);

		aud_reg = ali_ac97_get(codec, AC97_EXTENDED_STATUS);
		aud_reg = (aud_reg & AC97_EA_SLOT_MASK) | slots | AC97_EA_SPDIF;
		ali_ac97_set(codec, AC97_EXTENDED_STATUS, aud_reg);

		aud_reg = ali_ac97_get(codec, AC97_POWER_CONTROL);
		aud_reg |= 0x0002;
		ali_ac97_set(codec, AC97_POWER_CONTROL, aud_reg);
		udelay(1);

		state->card->ac97_status |= SPDIF_ON;

		/* Check to make sure the configuration is valid */
		aud_reg = ali_ac97_get(codec, AC97_EXTENDED_STATUS);
		if (!(aud_reg & 0x0400)) {
			/* turn off S/PDIF */
			ali_ac97_set(codec, AC97_EXTENDED_STATUS, (aud_reg & ~AC97_EA_SPDIF));
			state->card->ac97_status &= ~SPDIF_ON;
			return;
		}
		if (codec_independent_spdif_locked > 0) {
			aud_reg = ali_ac97_get(codec, 0x6a);
			ali_ac97_set(codec, 0x6a, (aud_reg & 0xefff));
		}
		/* Mute the analog output */
		/* Should this only mute the PCM volume??? */
	}
}

/* ali_set_dac_channels
 *
 *  Configure the codec's multi-channel DACs
 *
 *  The logic is backwards. Setting the bit to 1 turns off the DAC. 
 *
 *  What about the ICH? We currently configure it using the
 *  SNDCTL_DSP_CHANNELS ioctl.  If we're turnning on the DAC, 
 *  does that imply that we want the ICH set to support
 *  these channels?
 *  
 *  TODO:
 *    vailidate that the codec really supports these DACs
 *    before turning them on. 
 */
static void ali_set_dac_channels(struct ali_state *state, int channel)
{
	int aud_reg;
	struct ac97_codec *codec = state->card->ac97_codec[0];

	aud_reg = ali_ac97_get(codec, AC97_EXTENDED_STATUS);
	aud_reg |= AC97_EA_PRI | AC97_EA_PRJ | AC97_EA_PRK;
	state->card->ac97_status &= ~(SURR_ON | CENTER_LFE_ON);

	switch (channel) {
	case 2:		/* always enabled */
		break;
	case 4:
		aud_reg &= ~AC97_EA_PRJ;
		state->card->ac97_status |= SURR_ON;
		break;
	case 6:
		aud_reg &= ~(AC97_EA_PRJ | AC97_EA_PRI | AC97_EA_PRK);
		state->card->ac97_status |= SURR_ON | CENTER_LFE_ON;
		break;
	default:
		break;
	}
	ali_ac97_set(codec, AC97_EXTENDED_STATUS, aud_reg);

}

/* set playback sample rate */
static unsigned int ali_set_dac_rate(struct ali_state *state,
				     unsigned int rate)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	u32 new_rate;
	struct ac97_codec *codec = state->card->ac97_codec[0];

	if (!(state->card->ac97_features & 0x0001)) {
		dmabuf->rate = clocking;
		return clocking;
	}

	if (rate > 48000)
		rate = 48000;
	if (rate < 8000)
		rate = 8000;
	dmabuf->rate = rate;

	/*
	 *      Adjust for misclocked crap
	 */

	rate = (rate * clocking) / 48000;

	if (strict_clocking && rate < 8000) {
		rate = 8000;
		dmabuf->rate = (rate * 48000) / clocking;
	}

	new_rate = ac97_set_dac_rate(codec, rate);
	if (new_rate != rate) {
		dmabuf->rate = (new_rate * 48000) / clocking;
	}
	rate = new_rate;
	return dmabuf->rate;
}

/* set recording sample rate */
static unsigned int ali_set_adc_rate(struct ali_state *state,
				     unsigned int rate)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	u32 new_rate;
	struct ac97_codec *codec = state->card->ac97_codec[0];

	if (!(state->card->ac97_features & 0x0001)) {
		dmabuf->rate = clocking;
		return clocking;
	}

	if (rate > 48000)
		rate = 48000;
	if (rate < 8000)
		rate = 8000;
	dmabuf->rate = rate;

	/*
	 *      Adjust for misclocked crap
	 */

	rate = (rate * clocking) / 48000;
	if (strict_clocking && rate < 8000) {
		rate = 8000;
		dmabuf->rate = (rate * 48000) / clocking;
	}

	new_rate = ac97_set_adc_rate(codec, rate);

	if (new_rate != rate) {
		dmabuf->rate = (new_rate * 48000) / clocking;
		rate = new_rate;
	}
	return dmabuf->rate;
}

/* set codec independent spdifout sample rate */
static unsigned int ali_set_codecspdifout_rate(struct ali_state *state,
					       unsigned int rate)
{
	struct dmabuf *dmabuf = &state->dmabuf;

	if (!(state->card->ac97_features & 0x0001)) {
		dmabuf->rate = clocking;
		return clocking;
	}

	if (rate > 48000)
		rate = 48000;
	if (rate < 8000)
		rate = 8000;
	dmabuf->rate = rate;

	return dmabuf->rate;
}

/* set  controller independent spdif out function sample rate */
static void ali_set_spdifout_rate(struct ali_state *state,
				  unsigned int rate)
{
	unsigned char ch_st_sel;
	unsigned short status_rate;

	switch (rate) {
	case 44100:
		status_rate = 0;
		break;
	case 32000:
		status_rate = 0x300;
		break;
	case 48000:
	default:
		status_rate = 0x200;
		break;
	}

	ch_st_sel = inb(state->card->iobase + ALI_SPDIFICS) & ALI_SPDIF_OUT_CH_STATUS;	//select spdif_out

	ch_st_sel |= 0x80;	//select right
	outb(ch_st_sel, (state->card->iobase + ALI_SPDIFICS));
	outb(status_rate | 0x20, (state->card->iobase + ALI_SPDIFCSR + 2));

	ch_st_sel &= (~0x80);	//select left
	outb(ch_st_sel, (state->card->iobase + ALI_SPDIFICS));
	outw(status_rate | 0x10, (state->card->iobase + ALI_SPDIFCSR + 2));
}

/* get current playback/recording dma buffer pointer (byte offset from LBA),
   called with spinlock held! */

static inline unsigned ali_get_dma_addr(struct ali_state *state, int rec)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	unsigned int civ, offset, port, port_picb;
	unsigned int data;

	if (!dmabuf->enable)
		return 0;

	if (rec == 1)
		port = state->card->iobase + dmabuf->read_channel->port;
	else if (rec == 2)
		port = state->card->iobase + dmabuf->codec_spdifout_channel->port;
	else if (rec == 3)
		port = state->card->iobase + dmabuf->controller_spdifout_channel->port;
	else
		port = state->card->iobase + dmabuf->write_channel->port;

	port_picb = port + OFF_PICB;

	do {
		civ = inb(port + OFF_CIV) & 31;
		offset = inw(port_picb);
		/* Must have a delay here! */
		if (offset == 0)
			udelay(1);

		/* Reread both registers and make sure that that total
		 * offset from the first reading to the second is 0.
		 * There is an issue with SiS hardware where it will count
		 * picb down to 0, then update civ to the next value,
		 * then set the new picb to fragsize bytes.  We can catch
		 * it between the civ update and the picb update, making
		 * it look as though we are 1 fragsize ahead of where we
		 * are.  The next to we get the address though, it will
		 * be back in thdelay is more than long enough
		 * that we won't have to worry about the chip still being
		 * out of sync with reality ;-)
		 */
	} while (civ != (inb(port + OFF_CIV) & 31) || offset != inw(port_picb));

	data = ((civ + 1) * dmabuf->fragsize - (2 * offset)) % dmabuf->dmasize;
	if (inw(port_picb) == 0)
		data -= 2048;

	return data;
}

/* Stop recording (lock held) */
static inline void __stop_adc(struct ali_state *state)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	struct ali_card *card = state->card;

	dmabuf->enable &= ~ADC_RUNNING;

	outl((1 << 18) | (1 << 16), card->iobase + ALI_DMACR);
	udelay(1);

	outb(0, card->iobase + PI_CR);
	while (inb(card->iobase + PI_CR) != 0);

	// now clear any latent interrupt bits (like the halt bit)
	outb(inb(card->iobase + PI_SR) | 0x001e, card->iobase + PI_SR);
	outl(inl(card->iobase + ALI_INTERRUPTSR) & INT_PCMIN, card->iobase + ALI_INTERRUPTSR);
}

static void stop_adc(struct ali_state *state)
{
	struct ali_card *card = state->card;
	unsigned long flags;
	spin_lock_irqsave(&card->lock, flags);
	__stop_adc(state);
	spin_unlock_irqrestore(&card->lock, flags);
}

static inline void __start_adc(struct ali_state *state)
{
	struct dmabuf *dmabuf = &state->dmabuf;

	if (dmabuf->count < dmabuf->dmasize && dmabuf->ready
	    && !dmabuf->enable && (dmabuf->trigger & PCM_ENABLE_INPUT)) {
		dmabuf->enable |= ADC_RUNNING;
		outb((1 << 4) | (1 << 2), state->card->iobase + PI_CR);
		if (state->card->channel[0].used == 1)
			outl(1, state->card->iobase + ALI_DMACR);	// DMA CONTROL REGISTRER
		udelay(100);
		if (state->card->channel[2].used == 1)
			outl((1 << 2), state->card->iobase + ALI_DMACR);	//DMA CONTROL REGISTER
		udelay(100);
	}
}

static void start_adc(struct ali_state *state)
{
	struct ali_card *card = state->card;
	unsigned long flags;

	spin_lock_irqsave(&card->lock, flags);
	__start_adc(state);
	spin_unlock_irqrestore(&card->lock, flags);
}

/* stop playback (lock held) */
static inline void __stop_dac(struct ali_state *state)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	struct ali_card *card = state->card;

	dmabuf->enable &= ~DAC_RUNNING;
	outl(0x00020000, card->iobase + 0x08);
	outb(0, card->iobase + PO_CR);
	while (inb(card->iobase + PO_CR) != 0)
		cpu_relax();

	outb(inb(card->iobase + PO_SR) | 0x001e, card->iobase + PO_SR);

	outl(inl(card->iobase + ALI_INTERRUPTSR) & INT_PCMOUT, card->iobase + ALI_INTERRUPTSR);
}

static void stop_dac(struct ali_state *state)
{
	struct ali_card *card = state->card;
	unsigned long flags;
	spin_lock_irqsave(&card->lock, flags);
	__stop_dac(state);
	spin_unlock_irqrestore(&card->lock, flags);
}

static inline void __start_dac(struct ali_state *state)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	if (dmabuf->count > 0 && dmabuf->ready && !dmabuf->enable &&
	    (dmabuf->trigger & PCM_ENABLE_OUTPUT)) {
		dmabuf->enable |= DAC_RUNNING;
		outb((1 << 4) | (1 << 2), state->card->iobase + PO_CR);
		outl((1 << 1), state->card->iobase + 0x08);	//dma control register
	}
}

static void start_dac(struct ali_state *state)
{
	struct ali_card *card = state->card;
	unsigned long flags;
	spin_lock_irqsave(&card->lock, flags);
	__start_dac(state);
	spin_unlock_irqrestore(&card->lock, flags);
}

/* stop codec and controller spdif out  (lock held) */
static inline void __stop_spdifout(struct ali_state *state)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	struct ali_card *card = state->card;

	if (codec_independent_spdif_locked > 0) {
		dmabuf->enable &= ~CODEC_SPDIFOUT_RUNNING;
		outl((1 << 19), card->iobase + 0x08);
		outb(0, card->iobase + CODECSPDIFOUT_CR);

		while (inb(card->iobase + CODECSPDIFOUT_CR) != 0)
			cpu_relax();

		outb(inb(card->iobase + CODECSPDIFOUT_SR) | 0x001e, card->iobase + CODECSPDIFOUT_SR);
		outl(inl(card->iobase + ALI_INTERRUPTSR) & INT_CODECSPDIFOUT, card->iobase + ALI_INTERRUPTSR);
	} else {
		if (controller_independent_spdif_locked > 0) {
			dmabuf->enable &= ~CONTROLLER_SPDIFOUT_RUNNING;
			outl((1 << 23), card->iobase + 0x08);
			outb(0, card->iobase + CONTROLLERSPDIFOUT_CR);
			while (inb(card->iobase + CONTROLLERSPDIFOUT_CR) != 0)
				cpu_relax();
			outb(inb(card->iobase + CONTROLLERSPDIFOUT_SR) | 0x001e, card->iobase + CONTROLLERSPDIFOUT_SR);
			outl(inl(card->iobase + ALI_INTERRUPTSR) & INT_SPDIFOUT, card->iobase + ALI_INTERRUPTSR);
		}
	}
}

static void stop_spdifout(struct ali_state *state)
{
	struct ali_card *card = state->card;
	unsigned long flags;
	spin_lock_irqsave(&card->lock, flags);
	__stop_spdifout(state);
	spin_unlock_irqrestore(&card->lock, flags);
}

static inline void __start_spdifout(struct ali_state *state)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	if (dmabuf->count > 0 && dmabuf->ready && !dmabuf->enable &&
	    (dmabuf->trigger & SPDIF_ENABLE_OUTPUT)) {
		if (codec_independent_spdif_locked > 0) {
			dmabuf->enable |= CODEC_SPDIFOUT_RUNNING;
			outb((1 << 4) | (1 << 2), state->card->iobase + CODECSPDIFOUT_CR);
			outl((1 << 3), state->card->iobase + 0x08);	//dma control register
		} else {
			if (controller_independent_spdif_locked > 0) {
				dmabuf->enable |= CONTROLLER_SPDIFOUT_RUNNING;
				outb((1 << 4) | (1 << 2), state->card->iobase + CONTROLLERSPDIFOUT_CR);
				outl((1 << 7), state->card->iobase + 0x08);	//dma control register
			}
		}
	}
}

static void start_spdifout(struct ali_state *state)
{
	struct ali_card *card = state->card;
	unsigned long flags;
	spin_lock_irqsave(&card->lock, flags);
	__start_spdifout(state);
	spin_unlock_irqrestore(&card->lock, flags);
}

#define DMABUF_DEFAULTORDER (16-PAGE_SHIFT)
#define DMABUF_MINORDER 1

/* allocate DMA buffer, playback , recording,spdif out  buffer should be allocated separately */
static int alloc_dmabuf(struct ali_state *state)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	void *rawbuf = NULL;
	int order, size;
	struct page *page, *pend;

	/* If we don't have any oss frag params, then use our default ones */
	if (dmabuf->ossmaxfrags == 0)
		dmabuf->ossmaxfrags = 4;
	if (dmabuf->ossfragsize == 0)
		dmabuf->ossfragsize = (PAGE_SIZE << DMABUF_DEFAULTORDER) / dmabuf->ossmaxfrags;
	size = dmabuf->ossfragsize * dmabuf->ossmaxfrags;

	if (dmabuf->rawbuf && (PAGE_SIZE << dmabuf->buforder) == size)
		return 0;
	/* alloc enough to satisfy the oss params */
	for (order = DMABUF_DEFAULTORDER; order >= DMABUF_MINORDER; order--) {
		if ((PAGE_SIZE << order) > size)
			continue;
		if ((rawbuf = pci_alloc_consistent(state->card->pci_dev,
						   PAGE_SIZE << order,
						   &dmabuf->dma_handle)))
			break;
	}
	if (!rawbuf)
		return -ENOMEM;

	dmabuf->ready = dmabuf->mapped = 0;
	dmabuf->rawbuf = rawbuf;
	dmabuf->buforder = order;

	/* now mark the pages as reserved; otherwise remap_pfn_range doesn't do what we want */
	pend = virt_to_page(rawbuf + (PAGE_SIZE << order) - 1);
	for (page = virt_to_page(rawbuf); page <= pend; page++)
		SetPageReserved(page);
	return 0;
}

/* free DMA buffer */
static void dealloc_dmabuf(struct ali_state *state)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	struct page *page, *pend;

	if (dmabuf->rawbuf) {
		/* undo marking the pages as reserved */
		pend = virt_to_page(dmabuf->rawbuf + (PAGE_SIZE << dmabuf->buforder) - 1);
		for (page = virt_to_page(dmabuf->rawbuf); page <= pend; page++)
			ClearPageReserved(page);
		pci_free_consistent(state->card->pci_dev,
				    PAGE_SIZE << dmabuf->buforder,
				    dmabuf->rawbuf, dmabuf->dma_handle);
	}
	dmabuf->rawbuf = NULL;
	dmabuf->mapped = dmabuf->ready = 0;
}

static int prog_dmabuf(struct ali_state *state, unsigned rec)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	struct ali_channel *c = NULL;
	struct sg_item *sg;
	unsigned long flags;
	int ret;
	unsigned fragint;
	int i;

	spin_lock_irqsave(&state->card->lock, flags);
	if (dmabuf->enable & DAC_RUNNING)
		__stop_dac(state);
	if (dmabuf->enable & ADC_RUNNING)
		__stop_adc(state);
	if (dmabuf->enable & CODEC_SPDIFOUT_RUNNING)
		__stop_spdifout(state);
	if (dmabuf->enable & CONTROLLER_SPDIFOUT_RUNNING)
		__stop_spdifout(state);

	dmabuf->total_bytes = 0;
	dmabuf->count = dmabuf->error = 0;
	dmabuf->swptr = dmabuf->hwptr = 0;
	spin_unlock_irqrestore(&state->card->lock, flags);

	/* allocate DMA buffer, let alloc_dmabuf determine if we are already
	 * allocated well enough or if we should replace the current buffer
	 * (assuming one is already allocated, if it isn't, then allocate it).
	 */
	if ((ret = alloc_dmabuf(state)))
		return ret;

	/* FIXME: figure out all this OSS fragment stuff */
	/* I did, it now does what it should according to the OSS API.  DL */
	/* We may not have realloced our dmabuf, but the fragment size to
	 * fragment number ratio may have changed, so go ahead and reprogram
	 * things
	 */

	dmabuf->dmasize = PAGE_SIZE << dmabuf->buforder;
	dmabuf->numfrag = SG_LEN;
	dmabuf->fragsize = dmabuf->dmasize / dmabuf->numfrag;
	dmabuf->fragsamples = dmabuf->fragsize >> 1;
	dmabuf->userfragsize = dmabuf->ossfragsize;
	dmabuf->userfrags = dmabuf->dmasize / dmabuf->ossfragsize;

	memset(dmabuf->rawbuf, 0, dmabuf->dmasize);

	if (dmabuf->ossmaxfrags == 4) {
		fragint = 8;
		dmabuf->fragshift = 2;
	} else if (dmabuf->ossmaxfrags == 8) {
		fragint = 4;
		dmabuf->fragshift = 3;
	} else if (dmabuf->ossmaxfrags == 16) {
		fragint = 2;
		dmabuf->fragshift = 4;
	} else {
		fragint = 1;
		dmabuf->fragshift = 5;
	}
	/*
	 *      Now set up the ring 
	 */

	if (rec == 1)
		c = dmabuf->read_channel;
	else if (rec == 2)
		c = dmabuf->codec_spdifout_channel;
	else if (rec == 3)
		c = dmabuf->controller_spdifout_channel;
	else if (rec == 0)
		c = dmabuf->write_channel;
	if (c != NULL) {
		sg = &c->sg[0];
		/*
		 *      Load up 32 sg entries and take an interrupt at half
		 *      way (we might want more interrupts later..) 
		 */
		for (i = 0; i < dmabuf->numfrag; i++) {
			sg->busaddr =
			    virt_to_bus(dmabuf->rawbuf +
					dmabuf->fragsize * i);
			// the card will always be doing 16bit stereo
			sg->control = dmabuf->fragsamples;
			sg->control |= CON_BUFPAD;	//I modify
			// set us up to get IOC interrupts as often as needed to
			// satisfy numfrag requirements, no more
			if (((i + 1) % fragint) == 0) {
				sg->control |= CON_IOC;
			}
			sg++;
		}
		spin_lock_irqsave(&state->card->lock, flags);
		outb(2, state->card->iobase + c->port + OFF_CR);	/* reset DMA machine */
		outl(virt_to_bus(&c->sg[0]), state->card->iobase + c->port + OFF_BDBAR);
		outb(0, state->card->iobase + c->port + OFF_CIV);
		outb(0, state->card->iobase + c->port + OFF_LVI);
		spin_unlock_irqrestore(&state->card->lock, flags);
	}
	/* set the ready flag for the dma buffer */
	dmabuf->ready = 1;
	return 0;
}

static void __ali_update_lvi(struct ali_state *state, int rec)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	int x, port;
	port = state->card->iobase;
	if (rec == 1)
		port += dmabuf->read_channel->port;
	else if (rec == 2)
		port += dmabuf->codec_spdifout_channel->port;
	else if (rec == 3)
		port += dmabuf->controller_spdifout_channel->port;
	else if (rec == 0)
		port += dmabuf->write_channel->port;
	/* if we are currently stopped, then our CIV is actually set to our
	 * *last* sg segment and we are ready to wrap to the next.  However,
	 * if we set our LVI to the last sg segment, then it won't wrap to
	 * the next sg segment, it won't even get a start.  So, instead, when
	 * we are stopped, we set both the LVI value and also we increment
	 * the CIV value to the next sg segment to be played so that when
	 * we call start_{dac,adc}, things will operate properly
	 */
	if (!dmabuf->enable && dmabuf->ready) {
		if (rec && dmabuf->count < dmabuf->dmasize && (dmabuf->trigger & PCM_ENABLE_INPUT)) {
			outb((inb(port + OFF_CIV) + 1) & 31, port + OFF_LVI);
			__start_adc(state);
			while (! (inb(port + OFF_CR) & ((1 << 4) | (1 << 2))))
				cpu_relax();
		} else if (!rec && dmabuf->count && (dmabuf->trigger & PCM_ENABLE_OUTPUT)) {
			outb((inb(port + OFF_CIV) + 1) & 31, port + OFF_LVI);
			__start_dac(state);
			while (!(inb(port + OFF_CR) & ((1 << 4) | (1 << 2))))
				cpu_relax();
		} else if (rec && dmabuf->count && (dmabuf->trigger & SPDIF_ENABLE_OUTPUT)) {
			if (codec_independent_spdif_locked > 0) {
				// outb((inb(port+OFF_CIV))&31, port+OFF_LVI);
				outb((inb(port + OFF_CIV) + 1) & 31, port + OFF_LVI);
				__start_spdifout(state);
				while (!(inb(port + OFF_CR) & ((1 << 4) | (1 << 2))))
					cpu_relax();
			} else {
				if (controller_independent_spdif_locked > 0) {
					outb((inb(port + OFF_CIV) + 1) & 31, port + OFF_LVI);
					__start_spdifout(state);
					while (!(inb(port + OFF_CR) & ((1 << 4) | (1 << 2))))
						cpu_relax();
				}
			}
		}
	}

	/* swptr - 1 is the tail of our transfer */
	x = (dmabuf->dmasize + dmabuf->swptr - 1) % dmabuf->dmasize;
	x /= dmabuf->fragsize;
	outb(x, port + OFF_LVI);
}

static void ali_update_lvi(struct ali_state *state, int rec)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	unsigned long flags;
	if (!dmabuf->ready)
		return;
	spin_lock_irqsave(&state->card->lock, flags);
	__ali_update_lvi(state, rec);
	spin_unlock_irqrestore(&state->card->lock, flags);
}

/* update buffer manangement pointers, especially, dmabuf->count and dmabuf->hwptr */
static void ali_update_ptr(struct ali_state *state)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	unsigned hwptr;
	int diff;
	
	/* error handling and process wake up for DAC */
	if (dmabuf->enable == ADC_RUNNING) {
		/* update hardware pointer */
		hwptr = ali_get_dma_addr(state, 1);
		diff = (dmabuf->dmasize + hwptr - dmabuf->hwptr) % dmabuf->dmasize;
		dmabuf->hwptr = hwptr;
		dmabuf->total_bytes += diff;
		dmabuf->count += diff;
		if (dmabuf->count > dmabuf->dmasize) {
			/* buffer underrun or buffer overrun */
			/* this is normal for the end of a read */
			/* only give an error if we went past the */
			/* last valid sg entry */
			if ((inb(state->card->iobase + PI_CIV) & 31) != (inb(state->card->iobase + PI_LVI) & 31)) {
				printk(KERN_WARNING "ali_audio: DMA overrun on read\n");
				dmabuf->error++;
			}
		}
		if (dmabuf->count > dmabuf->userfragsize)
			wake_up(&dmabuf->wait);
	}
	/* error handling and process wake up for DAC */
	if (dmabuf->enable == DAC_RUNNING) {
		/* update hardware pointer */
		hwptr = ali_get_dma_addr(state, 0);
		diff =
		    (dmabuf->dmasize + hwptr -
		     dmabuf->hwptr) % dmabuf->dmasize;
#if defined(DEBUG_INTERRUPTS) || defined(DEBUG_MMAP)
		printk("DAC HWP %d,%d,%d\n", hwptr, dmabuf->hwptr, diff);
#endif
		dmabuf->hwptr = hwptr;
		dmabuf->total_bytes += diff;
		dmabuf->count -= diff;
		if (dmabuf->count < 0) {
			/* buffer underrun or buffer overrun */
			/* this is normal for the end of a write */
			/* only give an error if we went past the */
			/* last valid sg entry */
			if ((inb(state->card->iobase + PO_CIV) & 31) != (inb(state->card->iobase + PO_LVI) & 31)) {
				printk(KERN_WARNING "ali_audio: DMA overrun on write\n");
				printk(KERN_DEBUG "ali_audio: CIV %d, LVI %d, hwptr %x, count %d\n",
				     			inb(state->card->iobase + PO_CIV) & 31,
				     			inb(state->card->iobase + PO_LVI) & 31, 
							dmabuf->hwptr,
							dmabuf->count);
				dmabuf->error++;
			}
		}
		if (dmabuf->count < (dmabuf->dmasize - dmabuf->userfragsize))
		    	wake_up(&dmabuf->wait);
	}

	/* error handling and process wake up for CODEC SPDIF OUT */
	if (dmabuf->enable == CODEC_SPDIFOUT_RUNNING) {
		/* update hardware pointer */
		hwptr = ali_get_dma_addr(state, 2);
		diff = (dmabuf->dmasize + hwptr - dmabuf->hwptr) % dmabuf->dmasize;
		dmabuf->hwptr = hwptr;
		dmabuf->total_bytes += diff;
		dmabuf->count -= diff;
		if (dmabuf->count < 0) {
			/* buffer underrun or buffer overrun */
			/* this is normal for the end of a write */
			/* only give an error if we went past the */
			/* last valid sg entry */
			if ((inb(state->card->iobase + CODECSPDIFOUT_CIV) & 31) != (inb(state->card->iobase + CODECSPDIFOUT_LVI) & 31)) {
				printk(KERN_WARNING "ali_audio: DMA overrun on write\n");
				printk(KERN_DEBUG "ali_audio: CIV %d, LVI %d, hwptr %x, count %d\n", 
				        inb(state->card->iobase + CODECSPDIFOUT_CIV) & 31,
					inb(state->card->iobase + CODECSPDIFOUT_LVI) & 31,
					dmabuf->hwptr, dmabuf->count);
				dmabuf->error++;
			}
		}
		if (dmabuf->count < (dmabuf->dmasize - dmabuf->userfragsize))
			wake_up(&dmabuf->wait);
	}
	/* error handling and process wake up for CONTROLLER SPDIF OUT */
	if (dmabuf->enable == CONTROLLER_SPDIFOUT_RUNNING) {
		/* update hardware pointer */
		hwptr = ali_get_dma_addr(state, 3);
		diff = (dmabuf->dmasize + hwptr - dmabuf->hwptr) % dmabuf->dmasize;
		dmabuf->hwptr = hwptr;
		dmabuf->total_bytes += diff;
		dmabuf->count -= diff;
		if (dmabuf->count < 0) {
			/* buffer underrun or buffer overrun */
			/* this is normal for the end of a write */
			/* only give an error if we went past the */
			/* last valid sg entry */
			if ((inb(state->card->iobase + CONTROLLERSPDIFOUT_CIV) & 31) != (inb(state->card->iobase + CONTROLLERSPDIFOUT_LVI) & 31)) {
				printk(KERN_WARNING
				       "ali_audio: DMA overrun on write\n");
				printk("ali_audio: CIV %d, LVI %d, hwptr %x, "
					"count %d\n",
				     		inb(state->card->iobase + CONTROLLERSPDIFOUT_CIV) & 31,
				     		inb(state->card->iobase + CONTROLLERSPDIFOUT_LVI) & 31,
				     		dmabuf->hwptr, dmabuf->count);
				dmabuf->error++;
			}
		}
		if (dmabuf->count < (dmabuf->dmasize - dmabuf->userfragsize))
			wake_up(&dmabuf->wait);
	}
}

static inline int ali_get_free_write_space(struct
					   ali_state
					   *state)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	int free;

	if (dmabuf->count < 0) {
		dmabuf->count = 0;
		dmabuf->swptr = dmabuf->hwptr;
	}
	free = dmabuf->dmasize - dmabuf->swptr;
	if ((dmabuf->count + free) > dmabuf->dmasize){
		free = dmabuf->dmasize - dmabuf->count;
	}
	return free;
}

static inline int ali_get_available_read_data(struct
					      ali_state
					      *state)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	int avail;
	ali_update_ptr(state);
	// catch overruns during record
	if (dmabuf->count > dmabuf->dmasize) {
		dmabuf->count = dmabuf->dmasize;
		dmabuf->swptr = dmabuf->hwptr;
	}
	avail = dmabuf->count;
	avail -= (dmabuf->hwptr % dmabuf->fragsize);
	if (avail < 0)
		return (0);
	return (avail);
}

static int drain_dac(struct ali_state *state, int signals_allowed)
{

	DECLARE_WAITQUEUE(wait, current);
	struct dmabuf *dmabuf = &state->dmabuf;
	unsigned long flags;
	unsigned long tmo;
	int count;
	if (!dmabuf->ready)
		return 0;
	if (dmabuf->mapped) {
		stop_dac(state);
		return 0;
	}
	add_wait_queue(&dmabuf->wait, &wait);
	for (;;) {

		spin_lock_irqsave(&state->card->lock, flags);
		ali_update_ptr(state);
		count = dmabuf->count;
		spin_unlock_irqrestore(&state->card->lock, flags);
		if (count <= 0)
			break;
		/* 
		 * This will make sure that our LVI is correct, that our
		 * pointer is updated, and that the DAC is running.  We
		 * have to force the setting of dmabuf->trigger to avoid
		 * any possible deadlocks.
		 */
		if (!dmabuf->enable) {
			dmabuf->trigger = PCM_ENABLE_OUTPUT;
			ali_update_lvi(state, 0);
		}
		if (signal_pending(current) && signals_allowed) {
			break;
		}

		/* It seems that we have to set the current state to
		 * TASK_INTERRUPTIBLE every time to make the process
		 * really go to sleep.  This also has to be *after* the
		 * update_ptr() call because update_ptr is likely to
		 * do a wake_up() which will unset this before we ever
		 * try to sleep, resuling in a tight loop in this code
		 * instead of actually sleeping and waiting for an
		 * interrupt to wake us up!
		 */
		set_current_state(TASK_INTERRUPTIBLE);
		/*
		 * set the timeout to significantly longer than it *should*
		 * take for the DAC to drain the DMA buffer
		 */
		tmo = (count * HZ) / (dmabuf->rate);
		if (!schedule_timeout(tmo >= 2 ? tmo : 2)) {
			printk(KERN_ERR "ali_audio: drain_dac, dma timeout?\n");
			count = 0;
			break;
		}
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&dmabuf->wait, &wait);
	if (count > 0 && signal_pending(current) && signals_allowed)
		return -ERESTARTSYS;
	stop_dac(state);
	return 0;
}


static int drain_spdifout(struct ali_state *state, int signals_allowed)
{

	DECLARE_WAITQUEUE(wait, current);
	struct dmabuf *dmabuf = &state->dmabuf;
	unsigned long flags;
	unsigned long tmo;
	int count;
	if (!dmabuf->ready)
		return 0;
	if (dmabuf->mapped) {
		stop_spdifout(state);
		return 0;
	}
	add_wait_queue(&dmabuf->wait, &wait);
	for (;;) {

		spin_lock_irqsave(&state->card->lock, flags);
		ali_update_ptr(state);
		count = dmabuf->count;
		spin_unlock_irqrestore(&state->card->lock, flags);
		if (count <= 0)
			break;
		/* 
		 * This will make sure that our LVI is correct, that our
		 * pointer is updated, and that the DAC is running.  We
		 * have to force the setting of dmabuf->trigger to avoid
		 * any possible deadlocks.
		 */
		if (!dmabuf->enable) {
			if (codec_independent_spdif_locked > 0) {
				dmabuf->trigger = SPDIF_ENABLE_OUTPUT;
				ali_update_lvi(state, 2);
			} else {
				if (controller_independent_spdif_locked > 0) {
					dmabuf->trigger = SPDIF_ENABLE_OUTPUT;
					ali_update_lvi(state, 3);
				}
			}
		}
		if (signal_pending(current) && signals_allowed) {
			break;
		}

		/* It seems that we have to set the current state to
		 * TASK_INTERRUPTIBLE every time to make the process
		 * really go to sleep.  This also has to be *after* the
		 * update_ptr() call because update_ptr is likely to
		 * do a wake_up() which will unset this before we ever
		 * try to sleep, resuling in a tight loop in this code
		 * instead of actually sleeping and waiting for an
		 * interrupt to wake us up!
		 */
		set_current_state(TASK_INTERRUPTIBLE);
		/*
		 * set the timeout to significantly longer than it *should*
		 * take for the DAC to drain the DMA buffer
		 */
		tmo = (count * HZ) / (dmabuf->rate);
		if (!schedule_timeout(tmo >= 2 ? tmo : 2)) {
			printk(KERN_ERR "ali_audio: drain_spdifout, dma timeout?\n");
			count = 0;
			break;
		}
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&dmabuf->wait, &wait);
	if (count > 0 && signal_pending(current) && signals_allowed)
		return -ERESTARTSYS;
	stop_spdifout(state);
	return 0;
}

static void ali_channel_interrupt(struct ali_card *card)
{
	int i, count;
	
	for (i = 0; i < NR_HW_CH; i++) {
		struct ali_state *state = card->states[i];
		struct ali_channel *c = NULL;
		struct dmabuf *dmabuf;
		unsigned long port = card->iobase;
		u16 status;
		if (!state)
			continue;
		if (!state->dmabuf.ready)
			continue;
		dmabuf = &state->dmabuf;
		if (codec_independent_spdif_locked > 0) {
			if (dmabuf->enable & CODEC_SPDIFOUT_RUNNING) {
				c = dmabuf->codec_spdifout_channel;
			}
		} else {
			if (controller_independent_spdif_locked > 0) {
				if (dmabuf->enable & CONTROLLER_SPDIFOUT_RUNNING)
					c = dmabuf->controller_spdifout_channel;
			} else {
				if (dmabuf->enable & DAC_RUNNING) {
					c = dmabuf->write_channel;
				} else if (dmabuf->enable & ADC_RUNNING) {
					c = dmabuf->read_channel;
				} else
					continue;
			}
		}
		port += c->port;

		status = inw(port + OFF_SR);

		if (status & DMA_INT_COMPLETE) {
			/* only wake_up() waiters if this interrupt signals
			 * us being beyond a userfragsize of data open or
			 * available, and ali_update_ptr() does that for
			 * us
			 */
			ali_update_ptr(state);
		}

		if (status & DMA_INT_LVI) {
			ali_update_ptr(state);
			wake_up(&dmabuf->wait);

			if (dmabuf->enable & DAC_RUNNING)
				count = dmabuf->count;
			else if (dmabuf->enable & ADC_RUNNING)
				count = dmabuf->dmasize - dmabuf->count;
			else if (dmabuf->enable & CODEC_SPDIFOUT_RUNNING)
				count = dmabuf->count;
			else if (dmabuf->enable & CONTROLLER_SPDIFOUT_RUNNING)
				count = dmabuf->count;
			else count = 0;

			if (count > 0) {
				if (dmabuf->enable & DAC_RUNNING)
					outl((1 << 1), state->card->iobase + ALI_DMACR);
				else if (dmabuf->enable & CODEC_SPDIFOUT_RUNNING)
						outl((1 << 3), state->card->iobase + ALI_DMACR);
				else if (dmabuf->enable & CONTROLLER_SPDIFOUT_RUNNING)
					outl((1 << 7), state->card->iobase + ALI_DMACR);
			} else {
				if (dmabuf->enable & DAC_RUNNING)
					__stop_dac(state);
				if (dmabuf->enable & ADC_RUNNING)
					__stop_adc(state);
				if (dmabuf->enable & CODEC_SPDIFOUT_RUNNING)
					__stop_spdifout(state);
				if (dmabuf->enable & CONTROLLER_SPDIFOUT_RUNNING)
					__stop_spdifout(state);
				dmabuf->enable = 0;
				wake_up(&dmabuf->wait);
			}

		}
		if (!(status & DMA_INT_DCH)) {
			ali_update_ptr(state);
			wake_up(&dmabuf->wait);
			if (dmabuf->enable & DAC_RUNNING)
				count = dmabuf->count;
			else if (dmabuf->enable & ADC_RUNNING)
				count = dmabuf->dmasize - dmabuf->count;
			else if (dmabuf->enable & CODEC_SPDIFOUT_RUNNING)
				count = dmabuf->count;
			else if (dmabuf->enable & CONTROLLER_SPDIFOUT_RUNNING)
				count = dmabuf->count;
			else
				count = 0;

			if (count > 0) {
				if (dmabuf->enable & DAC_RUNNING)
					outl((1 << 1), state->card->iobase + ALI_DMACR);
				else if (dmabuf->enable & CODEC_SPDIFOUT_RUNNING)
					outl((1 << 3), state->card->iobase + ALI_DMACR);
				else if (dmabuf->enable & CONTROLLER_SPDIFOUT_RUNNING)
					outl((1 << 7), state->card->iobase + ALI_DMACR);
			} else {
				if (dmabuf->enable & DAC_RUNNING)
					__stop_dac(state);
				if (dmabuf->enable & ADC_RUNNING)
					__stop_adc(state);
				if (dmabuf->enable & CODEC_SPDIFOUT_RUNNING)
					__stop_spdifout(state);
				if (dmabuf->enable & CONTROLLER_SPDIFOUT_RUNNING)
					__stop_spdifout(state);
				dmabuf->enable = 0;
				wake_up(&dmabuf->wait);
			}
		}
		outw(status & DMA_INT_MASK, port + OFF_SR);
	}
}

static irqreturn_t ali_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct ali_card *card = (struct ali_card *) dev_id;
	u32 status;
	u16 status2;

	spin_lock(&card->lock);
	status = inl(card->iobase + ALI_INTERRUPTSR);
	if (!(status & INT_MASK)) {
		spin_unlock(&card->lock);
		return IRQ_NONE;		/* not for us */
	}

	if (codec_independent_spdif_locked > 0) {
		if (globel == 0) {
			globel += 1;
			status2 = inw(card->iobase + 0x76);
			outw(status2 | 0x000c, card->iobase + 0x76);
		} else {
			if (status & (INT_PCMOUT | INT_PCMIN | INT_MICIN | INT_SPDIFOUT | INT_CODECSPDIFOUT))
				ali_channel_interrupt(card);
		}
	} else {
		if (status & (INT_PCMOUT | INT_PCMIN | INT_MICIN | INT_SPDIFOUT | INT_CODECSPDIFOUT))
			ali_channel_interrupt(card);
	}

	/* clear 'em */
	outl(status & INT_MASK, card->iobase + ALI_INTERRUPTSR);
	spin_unlock(&card->lock);
	return IRQ_HANDLED;
}

/* in this loop, dmabuf.count signifies the amount of data that is
   waiting to be copied to the user's buffer.  It is filled by the dma
   machine and drained by this loop. */

static ssize_t ali_read(struct file *file, char __user *buffer,
			size_t count, loff_t * ppos)
{
	struct ali_state *state = (struct ali_state *) file->private_data;
	struct ali_card *card = state ? state->card : NULL;
	struct dmabuf *dmabuf = &state->dmabuf;
	ssize_t ret;
	unsigned long flags;
	unsigned int swptr;
	int cnt;
	DECLARE_WAITQUEUE(waita, current);
#ifdef DEBUG2
	printk("ali_audio: ali_read called, count = %d\n", count);
#endif
	if (dmabuf->mapped)
		return -ENXIO;
	if (dmabuf->enable & DAC_RUNNING)
		return -ENODEV;
	if (!dmabuf->read_channel) {
		dmabuf->ready = 0;
		dmabuf->read_channel = card->alloc_rec_pcm_channel(card);
		if (!dmabuf->read_channel) {
			return -EBUSY;
		}
	}
	if (!dmabuf->ready && (ret = prog_dmabuf(state, 1)))
		return ret;
	if (!access_ok(VERIFY_WRITE, buffer, count))
		return -EFAULT;
	ret = 0;
	add_wait_queue(&dmabuf->wait, &waita);
	while (count > 0) {
		set_current_state(TASK_INTERRUPTIBLE);
		spin_lock_irqsave(&card->lock, flags);
		if (PM_SUSPENDED(card)) {
			spin_unlock_irqrestore(&card->lock, flags);
			schedule();
			if (signal_pending(current)) {
				if (!ret)
					ret = -EAGAIN;
				break;
			}
			continue;
		}
		swptr = dmabuf->swptr;
		cnt = ali_get_available_read_data(state);
		// this is to make the copy_to_user simpler below
		if (cnt > (dmabuf->dmasize - swptr))
			cnt = dmabuf->dmasize - swptr;
		spin_unlock_irqrestore(&card->lock, flags);
		if (cnt > count)
			cnt = count;
		/* Lop off the last two bits to force the code to always
		 * write in full samples.  This keeps software that sets
		 * O_NONBLOCK but doesn't check the return value of the
		 * write call from getting things out of state where they
		 * think a full 4 byte sample was written when really only
		 * a portion was, resulting in odd sound and stereo
		 * hysteresis.
		 */
		cnt &= ~0x3;
		if (cnt <= 0) {
			unsigned long tmo;
			/*
			 * Don't let us deadlock.  The ADC won't start if
			 * dmabuf->trigger isn't set.  A call to SETTRIGGER
			 * could have turned it off after we set it to on
			 * previously.
			 */
			dmabuf->trigger = PCM_ENABLE_INPUT;
			/*
			 * This does three things.  Updates LVI to be correct,
			 * makes sure the ADC is running, and updates the
			 * hwptr.
			 */
			ali_update_lvi(state, 1);
			if (file->f_flags & O_NONBLOCK) {
				if (!ret)
					ret = -EAGAIN;
				goto done;
			}
			/* Set the timeout to how long it would take to fill
			 * two of our buffers.  If we haven't been woke up
			 * by then, then we know something is wrong.
			 */
			tmo = (dmabuf->dmasize * HZ * 2) / (dmabuf->rate * 4);
			    
			/* There are two situations when sleep_on_timeout returns, one is when
			   the interrupt is serviced correctly and the process is waked up by
			   ISR ON TIME. Another is when timeout is expired, which means that
			   either interrupt is NOT serviced correctly (pending interrupt) or it
			   is TOO LATE for the process to be scheduled to run (scheduler latency)
			   which results in a (potential) buffer overrun. And worse, there is
			   NOTHING we can do to prevent it. */
			if (!schedule_timeout(tmo >= 2 ? tmo : 2)) {
				printk(KERN_ERR
				       "ali_audio: recording schedule timeout, "
				       "dmasz %u fragsz %u count %i hwptr %u swptr %u\n",
				       dmabuf->dmasize, dmabuf->fragsize,
				       dmabuf->count, dmabuf->hwptr,
				       dmabuf->swptr);
				/* a buffer overrun, we delay the recovery until next time the
				   while loop begin and we REALLY have space to record */
			}
			if (signal_pending(current)) {
				ret = ret ? ret : -ERESTARTSYS;
				goto done;
			}
			continue;
		}

		if (copy_to_user(buffer, dmabuf->rawbuf + swptr, cnt)) {
			if (!ret)
				ret = -EFAULT;
			goto done;
		}

		swptr = (swptr + cnt) % dmabuf->dmasize;
		spin_lock_irqsave(&card->lock, flags);
		if (PM_SUSPENDED(card)) {
			spin_unlock_irqrestore(&card->lock, flags);
			continue;
		}
		dmabuf->swptr = swptr;
		dmabuf->count -= cnt;
		spin_unlock_irqrestore(&card->lock, flags);
		count -= cnt;
		buffer += cnt;
		ret += cnt;
	}
done:
	ali_update_lvi(state, 1);
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&dmabuf->wait, &waita);
	return ret;
}

/* in this loop, dmabuf.count signifies the amount of data that is waiting to be dma to
   the soundcard.  it is drained by the dma machine and filled by this loop. */
static ssize_t ali_write(struct file *file,
			 const char __user *buffer, size_t count, loff_t * ppos)
{
	struct ali_state *state = (struct ali_state *) file->private_data;
	struct ali_card *card = state ? state->card : NULL;
	struct dmabuf *dmabuf = &state->dmabuf;
	ssize_t ret;
	unsigned long flags;
	unsigned int swptr = 0;
	int cnt, x;
	DECLARE_WAITQUEUE(waita, current);
#ifdef DEBUG2
	printk("ali_audio: ali_write called, count = %d\n", count);
#endif
	if (dmabuf->mapped)
		return -ENXIO;
	if (dmabuf->enable & ADC_RUNNING)
		return -ENODEV;
	if (codec_independent_spdif_locked > 0) {
		if (!dmabuf->codec_spdifout_channel) {
			dmabuf->ready = 0;
			dmabuf->codec_spdifout_channel = card->alloc_codec_spdifout_channel(card);
			if (!dmabuf->codec_spdifout_channel)
				return -EBUSY;
		}
	} else {
		if (controller_independent_spdif_locked > 0) {
			if (!dmabuf->controller_spdifout_channel) {
				dmabuf->ready = 0;
				dmabuf->controller_spdifout_channel = card->alloc_controller_spdifout_channel(card);
				if (!dmabuf->controller_spdifout_channel)
					return -EBUSY;
			}
		} else {
			if (!dmabuf->write_channel) {
				dmabuf->ready = 0;
				dmabuf->write_channel =
				    card->alloc_pcm_channel(card);
				if (!dmabuf->write_channel)
					return -EBUSY;
			}
		}
	}

	if (codec_independent_spdif_locked > 0) {
		if (!dmabuf->ready && (ret = prog_dmabuf(state, 2)))
			return ret;
	} else {
		if (controller_independent_spdif_locked > 0) {
			if (!dmabuf->ready && (ret = prog_dmabuf(state, 3)))
				return ret;
		} else {

			if (!dmabuf->ready && (ret = prog_dmabuf(state, 0)))
				return ret;
		}
	}
	if (!access_ok(VERIFY_READ, buffer, count))
		return -EFAULT;
	ret = 0;
	add_wait_queue(&dmabuf->wait, &waita);
	while (count > 0) {
		set_current_state(TASK_INTERRUPTIBLE);
		spin_lock_irqsave(&state->card->lock, flags);
		if (PM_SUSPENDED(card)) {
			spin_unlock_irqrestore(&card->lock, flags);
			schedule();
			if (signal_pending(current)) {
				if (!ret)
					ret = -EAGAIN;
				break;
			}
			continue;
		}

		swptr = dmabuf->swptr;
		cnt = ali_get_free_write_space(state);
		/* Bound the maximum size to how much we can copy to the
		 * dma buffer before we hit the end.  If we have more to
		 * copy then it will get done in a second pass of this
		 * loop starting from the beginning of the buffer.
		 */
		if (cnt > (dmabuf->dmasize - swptr))
			cnt = dmabuf->dmasize - swptr;
		spin_unlock_irqrestore(&state->card->lock, flags);
#ifdef DEBUG2
		printk(KERN_INFO
		       "ali_audio: ali_write: %d bytes available space\n",
		       cnt);
#endif
		if (cnt > count)
			cnt = count;
		/* Lop off the last two bits to force the code to always
		 * write in full samples.  This keeps software that sets
		 * O_NONBLOCK but doesn't check the return value of the
		 * write call from getting things out of state where they
		 * think a full 4 byte sample was written when really only
		 * a portion was, resulting in odd sound and stereo
		 * hysteresis.
		 */
		cnt &= ~0x3;
		if (cnt <= 0) {
			unsigned long tmo;
			// There is data waiting to be played
			/*
			 * Force the trigger setting since we would
			 * deadlock with it set any other way
			 */
			if (codec_independent_spdif_locked > 0) {
				dmabuf->trigger = SPDIF_ENABLE_OUTPUT;
				ali_update_lvi(state, 2);
			} else {
				if (controller_independent_spdif_locked > 0) {
					dmabuf->trigger = SPDIF_ENABLE_OUTPUT;
					ali_update_lvi(state, 3);
				} else {

					dmabuf->trigger = PCM_ENABLE_OUTPUT;
					ali_update_lvi(state, 0);
				}
			}
			if (file->f_flags & O_NONBLOCK) {
				if (!ret)
					ret = -EAGAIN;
				goto ret;
			}
			/* Not strictly correct but works */
			tmo = (dmabuf->dmasize * HZ * 2) / (dmabuf->rate * 4);
			/* There are two situations when sleep_on_timeout returns, one is when
			   the interrupt is serviced correctly and the process is waked up by
			   ISR ON TIME. Another is when timeout is expired, which means that
			   either interrupt is NOT serviced correctly (pending interrupt) or it
			   is TOO LATE for the process to be scheduled to run (scheduler latency)
			   which results in a (potential) buffer underrun. And worse, there is
			   NOTHING we can do to prevent it. */
			   
			/* FIXME - do timeout handling here !! */
			schedule_timeout(tmo >= 2 ? tmo : 2);

			if (signal_pending(current)) {
				if (!ret)
					ret = -ERESTARTSYS;
				goto ret;
			}
			continue;
		}
		if (copy_from_user(dmabuf->rawbuf + swptr, buffer, cnt)) {
			if (!ret)
				ret = -EFAULT;
			goto ret;
		}

		swptr = (swptr + cnt) % dmabuf->dmasize;
		spin_lock_irqsave(&state->card->lock, flags);
		if (PM_SUSPENDED(card)) {
			spin_unlock_irqrestore(&card->lock, flags);
			continue;
		}

		dmabuf->swptr = swptr;
		dmabuf->count += cnt;
		count -= cnt;
		buffer += cnt;
		ret += cnt;
		spin_unlock_irqrestore(&state->card->lock, flags);
	}
	if (swptr % dmabuf->fragsize) {
		x = dmabuf->fragsize - (swptr % dmabuf->fragsize);
		memset(dmabuf->rawbuf + swptr, '\0', x);
	}
ret:
	if (codec_independent_spdif_locked > 0) {
		ali_update_lvi(state, 2);
	} else {
		if (controller_independent_spdif_locked > 0) {
			ali_update_lvi(state, 3);
		} else {
			ali_update_lvi(state, 0);
		}
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&dmabuf->wait, &waita);
	return ret;
}

/* No kernel lock - we have our own spinlock */
static unsigned int ali_poll(struct file *file, struct poll_table_struct
			     *wait)
{
	struct ali_state *state = (struct ali_state *) file->private_data;
	struct dmabuf *dmabuf = &state->dmabuf;
	unsigned long flags;
	unsigned int mask = 0;
	if (!dmabuf->ready)
		return 0;
	poll_wait(file, &dmabuf->wait, wait);
	spin_lock_irqsave(&state->card->lock, flags);
	ali_update_ptr(state);
	if (file->f_mode & FMODE_READ && dmabuf->enable & ADC_RUNNING) {
		if (dmabuf->count >= (signed) dmabuf->fragsize)
			mask |= POLLIN | POLLRDNORM;
	}
	if (file->f_mode & FMODE_WRITE  && (dmabuf->enable & (DAC_RUNNING|CODEC_SPDIFOUT_RUNNING|CONTROLLER_SPDIFOUT_RUNNING))) {
		if ((signed) dmabuf->dmasize >= dmabuf->count + (signed) dmabuf->fragsize)
			mask |= POLLOUT | POLLWRNORM;
	}
	spin_unlock_irqrestore(&state->card->lock, flags);
	return mask;
}

static int ali_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct ali_state *state = (struct ali_state *) file->private_data;
	struct dmabuf *dmabuf = &state->dmabuf;
	int ret = -EINVAL;
	unsigned long size;
	lock_kernel();
	if (vma->vm_flags & VM_WRITE) {
		if (!dmabuf->write_channel && (dmabuf->write_channel = state->card->alloc_pcm_channel(state->card)) == NULL) {
			ret = -EBUSY;
			goto out;
		}
	}
	if (vma->vm_flags & VM_READ) {
		if (!dmabuf->read_channel && (dmabuf->read_channel = state->card->alloc_rec_pcm_channel(state->card)) == NULL) {
			ret = -EBUSY;
			goto out;
		}
	}
	if ((ret = prog_dmabuf(state, 0)) != 0)
		goto out;
	ret = -EINVAL;
	if (vma->vm_pgoff != 0)
		goto out;
	size = vma->vm_end - vma->vm_start;
	if (size > (PAGE_SIZE << dmabuf->buforder))
		goto out;
	ret = -EAGAIN;
	if (remap_pfn_range(vma, vma->vm_start,
				virt_to_phys(dmabuf->rawbuf) >> PAGE_SHIFT,
				size, vma->vm_page_prot))
		goto out;
	dmabuf->mapped = 1;
	dmabuf->trigger = 0;
	ret = 0;
out:
	unlock_kernel();
	return ret;
}

static int ali_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	struct ali_state *state = (struct ali_state *) file->private_data;
	struct ali_channel *c = NULL;
	struct dmabuf *dmabuf = &state->dmabuf;
	unsigned long flags;
	audio_buf_info abinfo;
	count_info cinfo;
	unsigned int i_scr;
	int val = 0, ret;
	struct ac97_codec *codec = state->card->ac97_codec[0];
	void __user *argp = (void __user *)arg;
	int __user *p = argp;

#ifdef DEBUG
	printk("ali_audio: ali_ioctl, arg=0x%x, cmd=",
	       arg ? *p : 0);
#endif
	switch (cmd) {
	case OSS_GETVERSION:
#ifdef DEBUG
		printk("OSS_GETVERSION\n");
#endif
		return put_user(SOUND_VERSION, p);
	case SNDCTL_DSP_RESET:
#ifdef DEBUG
		printk("SNDCTL_DSP_RESET\n");
#endif
		spin_lock_irqsave(&state->card->lock, flags);
		if (dmabuf->enable == DAC_RUNNING) {
			c = dmabuf->write_channel;
			__stop_dac(state);
		}
		if (dmabuf->enable == ADC_RUNNING) {
			c = dmabuf->read_channel;
			__stop_adc(state);
		}
		if (dmabuf->enable == CODEC_SPDIFOUT_RUNNING) {
			c = dmabuf->codec_spdifout_channel;
			__stop_spdifout(state);
		}
		if (dmabuf->enable == CONTROLLER_SPDIFOUT_RUNNING) {
			c = dmabuf->controller_spdifout_channel;
			__stop_spdifout(state);
		}
		if (c != NULL) {
			outb(2, state->card->iobase + c->port + OFF_CR);	/* reset DMA machine */
			outl(virt_to_bus(&c->sg[0]),
			     state->card->iobase + c->port + OFF_BDBAR);
			outb(0, state->card->iobase + c->port + OFF_CIV);
			outb(0, state->card->iobase + c->port + OFF_LVI);
		}

		spin_unlock_irqrestore(&state->card->lock, flags);
		synchronize_irq(state->card->pci_dev->irq);
		dmabuf->ready = 0;
		dmabuf->swptr = dmabuf->hwptr = 0;
		dmabuf->count = dmabuf->total_bytes = 0;
		return 0;
	case SNDCTL_DSP_SYNC:
#ifdef DEBUG
		printk("SNDCTL_DSP_SYNC\n");
#endif
		if (codec_independent_spdif_locked > 0) {
			if (dmabuf->enable != CODEC_SPDIFOUT_RUNNING
			    || file->f_flags & O_NONBLOCK)
				return 0;
			if ((val = drain_spdifout(state, 1)))
				return val;
		} else {
			if (controller_independent_spdif_locked > 0) {
				if (dmabuf->enable !=
				    CONTROLLER_SPDIFOUT_RUNNING
				    || file->f_flags & O_NONBLOCK)
					return 0;
				if ((val = drain_spdifout(state, 1)))
					return val;
			} else {
				if (dmabuf->enable != DAC_RUNNING
				    || file->f_flags & O_NONBLOCK)
					return 0;
				if ((val = drain_dac(state, 1)))
					return val;
			}
		}
		dmabuf->total_bytes = 0;
		return 0;
	case SNDCTL_DSP_SPEED:	/* set smaple rate */
#ifdef DEBUG
		printk("SNDCTL_DSP_SPEED\n");
#endif
		if (get_user(val, p))
			return -EFAULT;
		if (val >= 0) {
			if (file->f_mode & FMODE_WRITE) {
				if ((state->card->ac97_status & SPDIF_ON)) {	/* S/PDIF Enabled */
					/* RELTEK ALC650 only support 48000, need to check that */
					if (ali_valid_spdif_rate(codec, val)) {
						if (codec_independent_spdif_locked > 0) {
							ali_set_spdif_output(state, -1, 0);
							stop_spdifout(state);
							dmabuf->ready = 0;
							/* I add test codec independent spdif out */
							spin_lock_irqsave(&state->card->lock, flags);
							ali_set_codecspdifout_rate(state, val);	// I modified
							spin_unlock_irqrestore(&state->card->lock, flags);
							/* Set S/PDIF transmitter rate. */
							i_scr = inl(state->card->iobase + ALI_SCR);
							if ((i_scr & 0x00300000) == 0x00100000) {
								ali_set_spdif_output(state, AC97_EA_SPSA_7_8, codec_independent_spdif_locked);
							} else {
								if ((i_scr&0x00300000)  == 0x00200000)
								{
									ali_set_spdif_output(state, AC97_EA_SPSA_6_9, codec_independent_spdif_locked);
								} else {
									if ((i_scr & 0x00300000) == 0x00300000) {
										ali_set_spdif_output(state, AC97_EA_SPSA_10_11, codec_independent_spdif_locked);
									} else {
										ali_set_spdif_output(state, AC97_EA_SPSA_7_8, codec_independent_spdif_locked);
									}
								}
							}

							if (!(state->card->ac97_status & SPDIF_ON)) {
								val = dmabuf->rate;
							}
						} else {
							if (controller_independent_spdif_locked > 0) 
							{
								stop_spdifout(state);
								dmabuf->ready = 0;
								spin_lock_irqsave(&state->card->lock, flags);
								ali_set_spdifout_rate(state, controller_independent_spdif_locked);
								spin_unlock_irqrestore(&state->card->lock, flags);
							} else {
								/* Set DAC rate */
								ali_set_spdif_output(state, -1, 0);
								stop_dac(state);
								dmabuf->ready = 0;
								spin_lock_irqsave(&state->card->lock, flags);
								ali_set_dac_rate(state, val);
								spin_unlock_irqrestore(&state->card->lock, flags);
								/* Set S/PDIF transmitter rate. */
								ali_set_spdif_output(state, AC97_EA_SPSA_3_4, val);
								if (!(state->card->ac97_status & SPDIF_ON))
								{
									val = dmabuf->rate;
								}
							}
						}
					} else {	/* Not a valid rate for S/PDIF, ignore it */
						val = dmabuf->rate;
					}
				} else {
					stop_dac(state);
					dmabuf->ready = 0;
					spin_lock_irqsave(&state->card->lock, flags);
					ali_set_dac_rate(state, val);
					spin_unlock_irqrestore(&state->card->lock, flags);
				}
			}
			if (file->f_mode & FMODE_READ) {
				stop_adc(state);
				dmabuf->ready = 0;
				spin_lock_irqsave(&state->card->lock, flags);
				ali_set_adc_rate(state, val);
				spin_unlock_irqrestore(&state->card->lock, flags);
			}
		}
		return put_user(dmabuf->rate, p);
	case SNDCTL_DSP_STEREO:	/* set stereo or mono channel */
#ifdef DEBUG
		printk("SNDCTL_DSP_STEREO\n");
#endif
		if (dmabuf->enable & DAC_RUNNING) {
			stop_dac(state);
		}
		if (dmabuf->enable & ADC_RUNNING) {
			stop_adc(state);
		}
		if (dmabuf->enable & CODEC_SPDIFOUT_RUNNING) {
			stop_spdifout(state);
		}
		if (dmabuf->enable & CONTROLLER_SPDIFOUT_RUNNING) {
			stop_spdifout(state);
		}
		return put_user(1, p);
	case SNDCTL_DSP_GETBLKSIZE:
		if (file->f_mode & FMODE_WRITE) {
			if (codec_independent_spdif_locked > 0) {
				if (!dmabuf->ready && (val = prog_dmabuf(state, 2)))
					return val;
			} else {
				if (controller_independent_spdif_locked > 0) {
					if (!dmabuf->ready && (val = prog_dmabuf(state, 3)))
						return val;
				} else {
					if (!dmabuf->ready && (val = prog_dmabuf(state, 0)))
						return val;
				}
			}
		}

		if (file->f_mode & FMODE_READ) {
			if (!dmabuf->ready && (val = prog_dmabuf(state, 1)))
				return val;
		}
#ifdef DEBUG
		printk("SNDCTL_DSP_GETBLKSIZE %d\n", dmabuf->userfragsize);
#endif
		return put_user(dmabuf->userfragsize, p);
	case SNDCTL_DSP_GETFMTS:	/* Returns a mask of supported sample format */
#ifdef DEBUG
		printk("SNDCTL_DSP_GETFMTS\n");
#endif
		return put_user(AFMT_S16_LE, p);
	case SNDCTL_DSP_SETFMT:	/* Select sample format */
#ifdef DEBUG
		printk("SNDCTL_DSP_SETFMT\n");
#endif
		return put_user(AFMT_S16_LE, p);
	case SNDCTL_DSP_CHANNELS:	// add support 4,6 channel 
#ifdef DEBUG
		printk("SNDCTL_DSP_CHANNELS\n");
#endif
		if (get_user(val, p))
			return -EFAULT;
		if (val > 0) {
			if (dmabuf->enable & DAC_RUNNING) {
				stop_dac(state);
			}
			if (dmabuf->enable & CODEC_SPDIFOUT_RUNNING) {
				stop_spdifout(state);
			}
			if (dmabuf->enable & CONTROLLER_SPDIFOUT_RUNNING) {
				stop_spdifout(state);
			}
			if (dmabuf->enable & ADC_RUNNING) {
				stop_adc(state);
			}
		} else {
			return put_user(state->card->channels, p);
		}

		i_scr = inl(state->card->iobase + ALI_SCR);
		/* Current # of channels enabled */
		if (i_scr & 0x00000100)
			ret = 4;
		else if (i_scr & 0x00000200)
			ret = 6;
		else
			ret = 2;
		switch (val) {
		case 2:	/* 2 channels is always supported */
			if (codec_independent_spdif_locked > 0) {
				outl(((i_scr & 0xfffffcff) | 0x00100000), (state->card->iobase + ALI_SCR));
			} else
				outl((i_scr & 0xfffffcff), (state->card->iobase + ALI_SCR));
			/* Do we need to change mixer settings????  */
			break;
		case 4:	/* Supported on some chipsets, better check first */
			if (codec_independent_spdif_locked > 0) {
				outl(((i_scr & 0xfffffcff) | 0x00000100 | 0x00200000), (state->card->iobase + ALI_SCR));
			} else
				outl(((i_scr & 0xfffffcff) | 0x00000100), (state->card->iobase + ALI_SCR));
			break;
		case 6:	/* Supported on some chipsets, better check first */
			if (codec_independent_spdif_locked > 0) {
				outl(((i_scr & 0xfffffcff) | 0x00000200 | 0x00008000 | 0x00300000), (state->card->iobase + ALI_SCR));
			} else
				outl(((i_scr & 0xfffffcff) | 0x00000200 | 0x00008000), (state->card->iobase + ALI_SCR));
			break;
		default:	/* nothing else is ever supported by the chipset */
			val = ret;
			break;
		}
		return put_user(val, p);
	case SNDCTL_DSP_POST:	/* the user has sent all data and is notifying us */
		/* we update the swptr to the end of the last sg segment then return */
#ifdef DEBUG
		printk("SNDCTL_DSP_POST\n");
#endif
		if (codec_independent_spdif_locked > 0) {
			if (!dmabuf->ready || (dmabuf->enable != CODEC_SPDIFOUT_RUNNING))
				return 0;
		} else {
			if (controller_independent_spdif_locked > 0) {
				if (!dmabuf->ready || (dmabuf->enable != CONTROLLER_SPDIFOUT_RUNNING))
					return 0;
			} else {
				if (!dmabuf->ready || (dmabuf->enable != DAC_RUNNING))
					return 0;
			}
		}
		if ((dmabuf->swptr % dmabuf->fragsize) != 0) {
			val = dmabuf->fragsize - (dmabuf->swptr % dmabuf->fragsize);
			dmabuf->swptr += val;
			dmabuf->count += val;
		}
		return 0;
	case SNDCTL_DSP_SUBDIVIDE:
		if (dmabuf->subdivision)
			return -EINVAL;
		if (get_user(val, p))
			return -EFAULT;
		if (val != 1 && val != 2 && val != 4)
			return -EINVAL;
#ifdef DEBUG
		printk("SNDCTL_DSP_SUBDIVIDE %d\n", val);
#endif
		dmabuf->subdivision = val;
		dmabuf->ready = 0;
		return 0;
	case SNDCTL_DSP_SETFRAGMENT:
		if (get_user(val, p))
			return -EFAULT;
		dmabuf->ossfragsize = 1 << (val & 0xffff);
		dmabuf->ossmaxfrags = (val >> 16) & 0xffff;
		if (!dmabuf->ossfragsize || !dmabuf->ossmaxfrags)
			return -EINVAL;
		/*
		 * Bound the frag size into our allowed range of 256 - 4096
		 */
		if (dmabuf->ossfragsize < 256)
			dmabuf->ossfragsize = 256;
		else if (dmabuf->ossfragsize > 4096)
			dmabuf->ossfragsize = 4096;
		/*
		 * The numfrags could be something reasonable, or it could
		 * be 0xffff meaning "Give me as much as possible".  So,
		 * we check the numfrags * fragsize doesn't exceed our
		 * 64k buffer limit, nor is it less than our 8k minimum.
		 * If it fails either one of these checks, then adjust the
		 * number of fragments, not the size of them.  It's OK if
		 * our number of fragments doesn't equal 32 or anything
		 * like our hardware based number now since we are using
		 * a different frag count for the hardware.  Before we get
		 * into this though, bound the maxfrags to avoid overflow
		 * issues.  A reasonable bound would be 64k / 256 since our
		 * maximum buffer size is 64k and our minimum frag size is
		 * 256.  On the other end, our minimum buffer size is 8k and
		 * our maximum frag size is 4k, so the lower bound should
		 * be 2.
		 */
		if (dmabuf->ossmaxfrags > 256)
			dmabuf->ossmaxfrags = 256;
		else if (dmabuf->ossmaxfrags < 2)
			dmabuf->ossmaxfrags = 2;
		val = dmabuf->ossfragsize * dmabuf->ossmaxfrags;
		while (val < 8192) {
			val <<= 1;
			dmabuf->ossmaxfrags <<= 1;
		}
		while (val > 65536) {
			val >>= 1;
			dmabuf->ossmaxfrags >>= 1;
		}
		dmabuf->ready = 0;
#ifdef DEBUG
		printk("SNDCTL_DSP_SETFRAGMENT 0x%x, %d, %d\n", val,
		       dmabuf->ossfragsize, dmabuf->ossmaxfrags);
#endif
		return 0;
	case SNDCTL_DSP_GETOSPACE:
		if (!(file->f_mode & FMODE_WRITE))
			return -EINVAL;
		if (codec_independent_spdif_locked > 0) {
			if (!dmabuf->ready && (val = prog_dmabuf(state, 2)) != 0)
				return val;
		} else {
			if (controller_independent_spdif_locked > 0) {
				if (!dmabuf->ready && (val = prog_dmabuf(state, 3)) != 0)
					return val;
			} else {
				if (!dmabuf->ready && (val = prog_dmabuf(state, 0)) != 0)
					return val;
			}
		}
		spin_lock_irqsave(&state->card->lock, flags);
		ali_update_ptr(state);
		abinfo.fragsize = dmabuf->userfragsize;
		abinfo.fragstotal = dmabuf->userfrags;
		if (dmabuf->mapped)
			abinfo.bytes = dmabuf->dmasize;
		else
			abinfo.bytes = ali_get_free_write_space(state);
		abinfo.fragments = abinfo.bytes / dmabuf->userfragsize;
		spin_unlock_irqrestore(&state->card->lock, flags);
#if defined(DEBUG) || defined(DEBUG_MMAP)
		printk("SNDCTL_DSP_GETOSPACE %d, %d, %d, %d\n",
		       abinfo.bytes, abinfo.fragsize, abinfo.fragments,
		       abinfo.fragstotal);
#endif
		return copy_to_user(argp, &abinfo,
				    sizeof(abinfo)) ? -EFAULT : 0;
	case SNDCTL_DSP_GETOPTR:
		if (!(file->f_mode & FMODE_WRITE))
			return -EINVAL;
		if (codec_independent_spdif_locked > 0) {
			if (!dmabuf->ready && (val = prog_dmabuf(state, 2)) != 0)
				return val;
		} else {
			if (controller_independent_spdif_locked > 0) {
				if (!dmabuf->ready && (val = prog_dmabuf(state, 3)) != 0)
					return val;
			} else {
				if (!dmabuf->ready && (val = prog_dmabuf(state, 0)) != 0)
					return val;
			}
		}
		spin_lock_irqsave(&state->card->lock, flags);
		val = ali_get_free_write_space(state);
		cinfo.bytes = dmabuf->total_bytes;
		cinfo.ptr = dmabuf->hwptr;
		cinfo.blocks = val / dmabuf->userfragsize;
		if (codec_independent_spdif_locked > 0) {
			if (dmabuf->mapped && (dmabuf->trigger & SPDIF_ENABLE_OUTPUT)) {
				dmabuf->count += val;
				dmabuf->swptr = (dmabuf->swptr + val) % dmabuf->dmasize;
				__ali_update_lvi(state, 2);
			}
		} else {
			if (controller_independent_spdif_locked > 0) {
				if (dmabuf->mapped && (dmabuf->trigger & SPDIF_ENABLE_OUTPUT)) {
					dmabuf->count += val;
					dmabuf->swptr = (dmabuf->swptr + val) % dmabuf->dmasize;
					__ali_update_lvi(state, 3);
				}
			} else {
				if (dmabuf->mapped && (dmabuf->trigger & PCM_ENABLE_OUTPUT)) {
					dmabuf->count += val;
					dmabuf->swptr = (dmabuf->swptr + val) % dmabuf->dmasize;
					__ali_update_lvi(state, 0);
				}
			}
		}
		spin_unlock_irqrestore(&state->card->lock, flags);
#if defined(DEBUG) || defined(DEBUG_MMAP)
		printk("SNDCTL_DSP_GETOPTR %d, %d, %d, %d\n", cinfo.bytes,
		       cinfo.blocks, cinfo.ptr, dmabuf->count);
#endif
		return copy_to_user(argp, &cinfo, sizeof(cinfo))? -EFAULT : 0;
	case SNDCTL_DSP_GETISPACE:
		if (!(file->f_mode & FMODE_READ))
			return -EINVAL;
		if (!dmabuf->ready && (val = prog_dmabuf(state, 1)) != 0)
			return val;
		spin_lock_irqsave(&state->card->lock, flags);
		abinfo.bytes = ali_get_available_read_data(state);
		abinfo.fragsize = dmabuf->userfragsize;
		abinfo.fragstotal = dmabuf->userfrags;
		abinfo.fragments = abinfo.bytes / dmabuf->userfragsize;
		spin_unlock_irqrestore(&state->card->lock, flags);
#if defined(DEBUG) || defined(DEBUG_MMAP)
		printk("SNDCTL_DSP_GETISPACE %d, %d, %d, %d\n",
		       abinfo.bytes, abinfo.fragsize, abinfo.fragments,
		       abinfo.fragstotal);
#endif
		return copy_to_user(argp, &abinfo,
				    sizeof(abinfo)) ? -EFAULT : 0;
	case SNDCTL_DSP_GETIPTR:
		if (!(file->f_mode & FMODE_READ))
			return -EINVAL;
		if (!dmabuf->ready && (val = prog_dmabuf(state, 0)) != 0)
			return val;
		spin_lock_irqsave(&state->card->lock, flags);
		val = ali_get_available_read_data(state);
		cinfo.bytes = dmabuf->total_bytes;
		cinfo.blocks = val / dmabuf->userfragsize;
		cinfo.ptr = dmabuf->hwptr;
		if (dmabuf->mapped && (dmabuf->trigger & PCM_ENABLE_INPUT)) {
			dmabuf->count -= val;
			dmabuf->swptr = (dmabuf->swptr + val) % dmabuf->dmasize;
			__ali_update_lvi(state, 1);
		}
		spin_unlock_irqrestore(&state->card->lock, flags);
#if defined(DEBUG) || defined(DEBUG_MMAP)
		printk("SNDCTL_DSP_GETIPTR %d, %d, %d, %d\n", cinfo.bytes,
		       cinfo.blocks, cinfo.ptr, dmabuf->count);
#endif
		return copy_to_user(argp, &cinfo, sizeof(cinfo))? -EFAULT: 0;
	case SNDCTL_DSP_NONBLOCK:
#ifdef DEBUG
		printk("SNDCTL_DSP_NONBLOCK\n");
#endif
		file->f_flags |= O_NONBLOCK;
		return 0;
	case SNDCTL_DSP_GETCAPS:
#ifdef DEBUG
		printk("SNDCTL_DSP_GETCAPS\n");
#endif
		return put_user(DSP_CAP_REALTIME | DSP_CAP_TRIGGER |
				DSP_CAP_MMAP | DSP_CAP_BIND, p);
	case SNDCTL_DSP_GETTRIGGER:
		val = 0;
#ifdef DEBUG
		printk("SNDCTL_DSP_GETTRIGGER 0x%x\n", dmabuf->trigger);
#endif
		return put_user(dmabuf->trigger, p);
	case SNDCTL_DSP_SETTRIGGER:
		if (get_user(val, p))
			return -EFAULT;
#if defined(DEBUG) || defined(DEBUG_MMAP)
		printk("SNDCTL_DSP_SETTRIGGER 0x%x\n", val);
#endif
		if (!(val & PCM_ENABLE_INPUT) && dmabuf->enable == ADC_RUNNING) {
			stop_adc(state);
		}
		if (!(val & PCM_ENABLE_OUTPUT) && dmabuf->enable == DAC_RUNNING) {
			stop_dac(state);
		}
		if (!(val & SPDIF_ENABLE_OUTPUT) && dmabuf->enable == CODEC_SPDIFOUT_RUNNING) {
			stop_spdifout(state);
		}
		if (!(val & SPDIF_ENABLE_OUTPUT) && dmabuf->enable == CONTROLLER_SPDIFOUT_RUNNING) {
			stop_spdifout(state);
		}
		dmabuf->trigger = val;
		if (val & PCM_ENABLE_OUTPUT && !(dmabuf->enable & DAC_RUNNING)) {
			if (!dmabuf->write_channel) {
				dmabuf->ready = 0;
				dmabuf->write_channel = state->card->alloc_pcm_channel(state->card);
				if (!dmabuf->write_channel)
					return -EBUSY;
			}
			if (!dmabuf->ready && (ret = prog_dmabuf(state, 0)))
				return ret;
			if (dmabuf->mapped) {
				spin_lock_irqsave(&state->card->lock, flags);
				ali_update_ptr(state);
				dmabuf->count = 0;
				dmabuf->swptr = dmabuf->hwptr;
				dmabuf->count = ali_get_free_write_space(state);
				dmabuf->swptr = (dmabuf->swptr + dmabuf->count) % dmabuf->dmasize;
				__ali_update_lvi(state, 0);
				spin_unlock_irqrestore(&state->card->lock,
						       flags);
			} else
				start_dac(state);
		}
		if (val & SPDIF_ENABLE_OUTPUT && !(dmabuf->enable & CODEC_SPDIFOUT_RUNNING)) {
			if (!dmabuf->codec_spdifout_channel) {
				dmabuf->ready = 0;
				dmabuf->codec_spdifout_channel = state->card->alloc_codec_spdifout_channel(state->card);
				if (!dmabuf->codec_spdifout_channel)
					return -EBUSY;
			}
			if (!dmabuf->ready && (ret = prog_dmabuf(state, 2)))
				return ret;
			if (dmabuf->mapped) {
				spin_lock_irqsave(&state->card->lock, flags);
				ali_update_ptr(state);
				dmabuf->count = 0;
				dmabuf->swptr = dmabuf->hwptr;
				dmabuf->count = ali_get_free_write_space(state);
				dmabuf->swptr = (dmabuf->swptr + dmabuf->count) % dmabuf->dmasize;
				__ali_update_lvi(state, 2);
				spin_unlock_irqrestore(&state->card->lock,
						       flags);
			} else
				start_spdifout(state);
		}
		if (val & SPDIF_ENABLE_OUTPUT && !(dmabuf->enable & CONTROLLER_SPDIFOUT_RUNNING)) {
			if (!dmabuf->controller_spdifout_channel) {
				dmabuf->ready = 0;
				dmabuf->controller_spdifout_channel = state->card->alloc_controller_spdifout_channel(state->card);
				if (!dmabuf->controller_spdifout_channel)
					return -EBUSY;
			}
			if (!dmabuf->ready && (ret = prog_dmabuf(state, 3)))
				return ret;
			if (dmabuf->mapped) {
				spin_lock_irqsave(&state->card->lock, flags);
				ali_update_ptr(state);
				dmabuf->count = 0;
				dmabuf->swptr = dmabuf->hwptr;
				dmabuf->count = ali_get_free_write_space(state);
				dmabuf->swptr = (dmabuf->swptr + dmabuf->count) % dmabuf->dmasize;
				__ali_update_lvi(state, 3);
				spin_unlock_irqrestore(&state->card->lock, flags);
			} else
				start_spdifout(state);
		}
		if (val & PCM_ENABLE_INPUT && !(dmabuf->enable & ADC_RUNNING)) {
			if (!dmabuf->read_channel) {
				dmabuf->ready = 0;
				dmabuf->read_channel = state->card->alloc_rec_pcm_channel(state->card);
				if (!dmabuf->read_channel)
					return -EBUSY;
			}
			if (!dmabuf->ready && (ret = prog_dmabuf(state, 1)))
				return ret;
			if (dmabuf->mapped) {
				spin_lock_irqsave(&state->card->lock,
						  flags);
				ali_update_ptr(state);
				dmabuf->swptr = dmabuf->hwptr;
				dmabuf->count = 0;
				spin_unlock_irqrestore(&state->card->lock, flags);
			}
			ali_update_lvi(state, 1);
			start_adc(state);
		}
		return 0;
	case SNDCTL_DSP_SETDUPLEX:
#ifdef DEBUG
		printk("SNDCTL_DSP_SETDUPLEX\n");
#endif
		return -EINVAL;
	case SNDCTL_DSP_GETODELAY:
		if (!(file->f_mode & FMODE_WRITE))
			return -EINVAL;
		spin_lock_irqsave(&state->card->lock, flags);
		ali_update_ptr(state);
		val = dmabuf->count;
		spin_unlock_irqrestore(&state->card->lock, flags);
#ifdef DEBUG
		printk("SNDCTL_DSP_GETODELAY %d\n", dmabuf->count);
#endif
		return put_user(val, p);
	case SOUND_PCM_READ_RATE:
#ifdef DEBUG
		printk("SOUND_PCM_READ_RATE %d\n", dmabuf->rate);
#endif
		return put_user(dmabuf->rate, p);
	case SOUND_PCM_READ_CHANNELS:
#ifdef DEBUG
		printk("SOUND_PCM_READ_CHANNELS\n");
#endif
		return put_user(2, p);
	case SOUND_PCM_READ_BITS:
#ifdef DEBUG
		printk("SOUND_PCM_READ_BITS\n");
#endif
		return put_user(AFMT_S16_LE, p);
	case SNDCTL_DSP_SETSPDIF:	/* Set S/PDIF Control register */
#ifdef DEBUG
		printk("SNDCTL_DSP_SETSPDIF\n");
#endif
		if (get_user(val, p))
			return -EFAULT;
		/* Check to make sure the codec supports S/PDIF transmitter */
		if ((state->card->ac97_features & 4)) {
			/* mask out the transmitter speed bits so the user can't set them */
			val &= ~0x3000;
			/* Add the current transmitter speed bits to the passed value */
			ret = ali_ac97_get(codec, AC97_SPDIF_CONTROL);
			val |= (ret & 0x3000);
			ali_ac97_set(codec, AC97_SPDIF_CONTROL, val);
			if (ali_ac97_get(codec, AC97_SPDIF_CONTROL) != val) {
				printk(KERN_ERR "ali_audio: Unable to set S/PDIF configuration to 0x%04x.\n", val);
				return -EFAULT;
			}
		}
#ifdef DEBUG
		else
			printk(KERN_WARNING "ali_audio: S/PDIF transmitter not avalible.\n");
#endif
		return put_user(val, p);
	case SNDCTL_DSP_GETSPDIF:	/* Get S/PDIF Control register */
#ifdef DEBUG
		printk("SNDCTL_DSP_GETSPDIF\n");
#endif
		if (get_user(val, p))
			return -EFAULT;
		/* Check to make sure the codec supports S/PDIF transmitter */
		if (!(state->card->ac97_features & 4)) {
#ifdef DEBUG
			printk(KERN_WARNING "ali_audio: S/PDIF transmitter not avalible.\n");
#endif
			val = 0;
		} else {
			val = ali_ac97_get(codec, AC97_SPDIF_CONTROL);
		}

		return put_user(val, p);
//end add support spdif out
//add support 4,6 channel
	case SNDCTL_DSP_GETCHANNELMASK:
#ifdef DEBUG
		printk("SNDCTL_DSP_GETCHANNELMASK\n");
#endif
		if (get_user(val, p))
			return -EFAULT;
		/* Based on AC'97 DAC support, not ICH hardware */
		val = DSP_BIND_FRONT;
		if (state->card->ac97_features & 0x0004)
			val |= DSP_BIND_SPDIF;
		if (state->card->ac97_features & 0x0080)
			val |= DSP_BIND_SURR;
		if (state->card->ac97_features & 0x0140)
			val |= DSP_BIND_CENTER_LFE;
		return put_user(val, p);
	case SNDCTL_DSP_BIND_CHANNEL:
#ifdef DEBUG
		printk("SNDCTL_DSP_BIND_CHANNEL\n");
#endif
		if (get_user(val, p))
			return -EFAULT;
		if (val == DSP_BIND_QUERY) {
			val = DSP_BIND_FRONT;	/* Always report this as being enabled */
			if (state->card->ac97_status & SPDIF_ON)
				val |= DSP_BIND_SPDIF;
			else {
				if (state->card->ac97_status & SURR_ON)
					val |= DSP_BIND_SURR;
				if (state->card->
				    ac97_status & CENTER_LFE_ON)
					val |= DSP_BIND_CENTER_LFE;
			}
		} else {	/* Not a query, set it */
			if (!(file->f_mode & FMODE_WRITE))
				return -EINVAL;
			if (dmabuf->enable == DAC_RUNNING) {
				stop_dac(state);
			}
			if (val & DSP_BIND_SPDIF) {	/* Turn on SPDIF */
				/*  Ok, this should probably define what slots
				 *  to use. For now, we'll only set it to the
				 *  defaults:
				 * 
				 *   non multichannel codec maps to slots 3&4
				 *   2 channel codec maps to slots 7&8
				 *   4 channel codec maps to slots 6&9
				 *   6 channel codec maps to slots 10&11
				 *
				 *  there should be some way for the app to
				 *  select the slot assignment.
				 */
				i_scr = inl(state->card->iobase + ALI_SCR);
				if (codec_independent_spdif_locked > 0) {

					if ((i_scr & 0x00300000) == 0x00100000) {
						ali_set_spdif_output(state, AC97_EA_SPSA_7_8, codec_independent_spdif_locked);
					} else {
						if ((i_scr & 0x00300000) == 0x00200000) {
							ali_set_spdif_output(state, AC97_EA_SPSA_6_9, codec_independent_spdif_locked);
						} else {
							if ((i_scr & 0x00300000) == 0x00300000) {
								ali_set_spdif_output(state, AC97_EA_SPSA_10_11, codec_independent_spdif_locked);
							}
						}
					}
				} else {	/* codec spdif out (pcm out share ) */
					ali_set_spdif_output(state, AC97_EA_SPSA_3_4, dmabuf->rate);	//I do not modify
				}

				if (!(state->card->ac97_status & SPDIF_ON))
					val &= ~DSP_BIND_SPDIF;
			} else {
				int mask;
				int channels;
				/* Turn off S/PDIF if it was on */
				if (state->card->ac97_status & SPDIF_ON)
					ali_set_spdif_output(state, -1, 0);
				mask =
				    val & (DSP_BIND_FRONT | DSP_BIND_SURR |
					   DSP_BIND_CENTER_LFE);
				switch (mask) {
				case DSP_BIND_FRONT:
					channels = 2;
					break;
				case DSP_BIND_FRONT | DSP_BIND_SURR:
					channels = 4;
					break;
				case DSP_BIND_FRONT | DSP_BIND_SURR | DSP_BIND_CENTER_LFE:
					channels = 6;
					break;
				default:
					val = DSP_BIND_FRONT;
					channels = 2;
					break;
				}
				ali_set_dac_channels(state, channels);
				/* check that they really got turned on */
				if (!state->card->ac97_status & SURR_ON)
					val &= ~DSP_BIND_SURR;
				if (!state->card->
				    ac97_status & CENTER_LFE_ON)
					val &= ~DSP_BIND_CENTER_LFE;
			}
		}
		return put_user(val, p);
	case SNDCTL_DSP_MAPINBUF:
	case SNDCTL_DSP_MAPOUTBUF:
	case SNDCTL_DSP_SETSYNCRO:
	case SOUND_PCM_WRITE_FILTER:
	case SOUND_PCM_READ_FILTER:
		return -EINVAL;
	}
	return -EINVAL;
}

static int ali_open(struct inode *inode, struct file *file)
{
	int i = 0;
	struct ali_card *card = devs;
	struct ali_state *state = NULL;
	struct dmabuf *dmabuf = NULL;
	unsigned int i_scr;
	
	/* find an available virtual channel (instance of /dev/dsp) */
	
	while (card != NULL) {

		/*
		 * If we are initializing and then fail, card could go
		 * away unuexpectedly while we are in the for() loop.
		 * So, check for card on each iteration before we check
		 * for card->initializing to avoid a possible oops.
		 * This usually only matters for times when the driver is
		 * autoloaded by kmod.
		 */
		for (i = 0; i < 50 && card && card->initializing; i++) {
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(HZ / 20);
		}

		for (i = 0; i < NR_HW_CH && card && !card->initializing; i++) {
			if (card->states[i] == NULL) {
				state = card->states[i] = (struct ali_state *) kmalloc(sizeof(struct ali_state), GFP_KERNEL);
				if (state == NULL)
					return -ENOMEM;
				memset(state, 0, sizeof(struct ali_state));
				dmabuf = &state->dmabuf;
				goto found_virt;
			}
		}
		card = card->next;
	}

	/* no more virtual channel avaiable */
	if (!state)
		return -ENODEV;
found_virt:
	/* initialize the virtual channel */

	state->virt = i;
	state->card = card;
	state->magic = ALI5455_STATE_MAGIC;
	init_waitqueue_head(&dmabuf->wait);
	mutex_init(&state->open_mutex);
	file->private_data = state;
	dmabuf->trigger = 0;
	/* allocate hardware channels */
	if (file->f_mode & FMODE_READ) {
		if ((dmabuf->read_channel =
		     card->alloc_rec_pcm_channel(card)) == NULL) {
			kfree(card->states[i]);
			card->states[i] = NULL;
			return -EBUSY;
		}
		dmabuf->trigger |= PCM_ENABLE_INPUT;
		ali_set_adc_rate(state, 8000);
	}
	if (file->f_mode & FMODE_WRITE) {
		if (codec_independent_spdif_locked > 0) {
			if ((dmabuf->codec_spdifout_channel = card->alloc_codec_spdifout_channel(card)) == NULL) {
				kfree(card->states[i]);
				card->states[i] = NULL;
				return -EBUSY;
			}
			dmabuf->trigger |= SPDIF_ENABLE_OUTPUT;
			ali_set_codecspdifout_rate(state, codec_independent_spdif_locked);	//It must add
			i_scr = inl(state->card->iobase + ALI_SCR);
			if ((i_scr & 0x00300000) == 0x00100000) {
				ali_set_spdif_output(state, AC97_EA_SPSA_7_8, codec_independent_spdif_locked);
			} else {
				if ((i_scr & 0x00300000) == 0x00200000) {
					ali_set_spdif_output(state, AC97_EA_SPSA_6_9, codec_independent_spdif_locked);
				} else {
					if ((i_scr & 0x00300000) == 0x00300000) {
						ali_set_spdif_output(state, AC97_EA_SPSA_10_11, codec_independent_spdif_locked);
					} else {
						ali_set_spdif_output(state, AC97_EA_SPSA_7_8, codec_independent_spdif_locked);
					}
				}

			}
		} else {
			if (controller_independent_spdif_locked > 0) {
				if ((dmabuf->controller_spdifout_channel = card->alloc_controller_spdifout_channel(card)) == NULL) {
					kfree(card->states[i]);
					card->states[i] = NULL;
					return -EBUSY;
				}
				dmabuf->trigger |= SPDIF_ENABLE_OUTPUT;
				ali_set_spdifout_rate(state, controller_independent_spdif_locked);
			} else {
				if ((dmabuf->write_channel = card->alloc_pcm_channel(card)) == NULL) {
					kfree(card->states[i]);
					card->states[i] = NULL;
					return -EBUSY;
				}
				/* Initialize to 8kHz?  What if we don't support 8kHz? */
				/*  Let's change this to check for S/PDIF stuff */

				dmabuf->trigger |= PCM_ENABLE_OUTPUT;
				if (codec_pcmout_share_spdif_locked) {
					ali_set_dac_rate(state, codec_pcmout_share_spdif_locked);
					ali_set_spdif_output(state, AC97_EA_SPSA_3_4, codec_pcmout_share_spdif_locked);
				} else {
					ali_set_dac_rate(state, 8000);
				}
			}

		}
	}

	/* set default sample format. According to OSS Programmer's Guide  /dev/dsp
	   should be default to unsigned 8-bits, mono, with sample rate 8kHz and
	   /dev/dspW will accept 16-bits sample, but we don't support those so we
	   set it immediately to stereo and 16bit, which is all we do support */
	dmabuf->fmt |= ALI5455_FMT_16BIT | ALI5455_FMT_STEREO;
	dmabuf->ossfragsize = 0;
	dmabuf->ossmaxfrags = 0;
	dmabuf->subdivision = 0;
	state->open_mode |= file->f_mode & (FMODE_READ | FMODE_WRITE);
	outl(0x00000000, card->iobase + ALI_INTERRUPTCR);
	outl(0x00000000, card->iobase + ALI_INTERRUPTSR);
	return nonseekable_open(inode, file);
}

static int ali_release(struct inode *inode, struct file *file)
{
	struct ali_state *state = (struct ali_state *) file->private_data;
	struct ali_card *card = state->card;
	struct dmabuf *dmabuf = &state->dmabuf;
	unsigned long flags;
	lock_kernel();
	
	/* stop DMA state machine and free DMA buffers/channels */
	if (dmabuf->trigger & PCM_ENABLE_OUTPUT)
		drain_dac(state, 0);

	if (dmabuf->trigger & SPDIF_ENABLE_OUTPUT)
		drain_spdifout(state, 0);
	
	if (dmabuf->trigger & PCM_ENABLE_INPUT)
		stop_adc(state);
	
	spin_lock_irqsave(&card->lock, flags);
	dealloc_dmabuf(state);
	if (file->f_mode & FMODE_WRITE) {
		if (codec_independent_spdif_locked > 0) {
			state->card->free_pcm_channel(state->card, dmabuf->codec_spdifout_channel->num);
		} else {
			if (controller_independent_spdif_locked > 0)
				state->card->free_pcm_channel(state->card,
							      dmabuf->controller_spdifout_channel->num);
			else state->card->free_pcm_channel(state->card,
							      dmabuf->write_channel->num);
		}
	}
	if (file->f_mode & FMODE_READ)
		state->card->free_pcm_channel(state->card, dmabuf->read_channel->num);

	state->card->states[state->virt] = NULL;
	kfree(state);
	spin_unlock_irqrestore(&card->lock, flags);
	unlock_kernel();
	return 0;
}

static /*const */ struct file_operations ali_audio_fops = {
	.owner		= THIS_MODULE, 
	.llseek		= no_llseek, 
	.read		= ali_read,
	.write		= ali_write, 
	.poll		= ali_poll,
	.ioctl		= ali_ioctl,
	.mmap		= ali_mmap,
	.open		= ali_open,
	.release	= ali_release,
};

/* Read AC97 codec registers */
static u16 ali_ac97_get(struct ac97_codec *dev, u8 reg)
{
	struct ali_card *card = dev->private_data;
	int count1 = 100;
	char val;
	unsigned short int data = 0, count, addr1, addr2 = 0;

	spin_lock(&card->ac97_lock);
	while (count1-- && (inl(card->iobase + ALI_CAS) & 0x80000000))
		udelay(1);

	addr1 = reg;
	reg |= 0x0080;
	for (count = 0; count < 0x7f; count++) {
		val = inb(card->iobase + ALI_CSPSR);
		if (val & 0x08)
			break;
	}
	if (count == 0x7f)
	{
		spin_unlock(&card->ac97_lock);
		return -1;
	}
	outw(reg, (card->iobase + ALI_CPR) + 2);
	for (count = 0; count < 0x7f; count++) {
		val = inb(card->iobase + ALI_CSPSR);
		if (val & 0x02) {
			data = inw(card->iobase + ALI_SPR);
			addr2 = inw((card->iobase + ALI_SPR) + 2);
			break;
		}
	}
	spin_unlock(&card->ac97_lock);
	if (count == 0x7f)
		return -1;
	if (addr2 != addr1)
		return -1;
	return ((u16) data);
}

/* write ac97 codec register   */

static void ali_ac97_set(struct ac97_codec *dev, u8 reg, u16 data)
{
	struct ali_card *card = dev->private_data;
	int count1 = 100;
	char val;
	unsigned short int count;

	spin_lock(&card->ac97_lock);
	while (count1-- && (inl(card->iobase + ALI_CAS) & 0x80000000))
		udelay(1);

	for (count = 0; count < 0x7f; count++) {
		val = inb(card->iobase + ALI_CSPSR);
		if (val & 0x08)
			break;
	}
	if (count == 0x7f) {
		printk(KERN_WARNING "ali_ac97_set: AC97 codec register access timed out. \n");
		spin_unlock(&card->ac97_lock);
		return;
	}
	outw(data, (card->iobase + ALI_CPR));
	outb(reg, (card->iobase + ALI_CPR) + 2);
	for (count = 0; count < 0x7f; count++) {
		val = inb(card->iobase + ALI_CSPSR);
		if (val & 0x01)
			break;
	}
	spin_unlock(&card->ac97_lock);
	if (count == 0x7f)
		printk(KERN_WARNING "ali_ac97_set: AC97 codec register access timed out. \n");
	return;
}

/* OSS /dev/mixer file operation methods */

static int ali_open_mixdev(struct inode *inode, struct file *file)
{
	int i;
	int minor = iminor(inode);
	struct ali_card *card = devs;
	for (card = devs; card != NULL; card = card->next) {
		/*
		 * If we are initializing and then fail, card could go
		 * away unuexpectedly while we are in the for() loop.
		 * So, check for card on each iteration before we check
		 * for card->initializing to avoid a possible oops.
		 * This usually only matters for times when the driver is
		 * autoloaded by kmod.
		 */
		for (i = 0; i < 50 && card && card->initializing; i++) {
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(HZ / 20);
		}
		for (i = 0; i < NR_AC97 && card && !card->initializing; i++)
			if (card->ac97_codec[i] != NULL
			    && card->ac97_codec[i]->dev_mixer == minor) {
				file->private_data = card->ac97_codec[i];
				return nonseekable_open(inode, file);
			}
	}
	return -ENODEV;
}

static int ali_ioctl_mixdev(struct inode *inode,
			    struct file *file,
			    unsigned int cmd, unsigned long arg)
{
	struct ac97_codec *codec = (struct ac97_codec *) file->private_data;
	return codec->mixer_ioctl(codec, cmd, arg);
}

static /*const */ struct file_operations ali_mixer_fops = {
	.owner	= THIS_MODULE, 
	.llseek	= no_llseek, 
	.ioctl	= ali_ioctl_mixdev,
	.open	= ali_open_mixdev,
};

/* AC97 codec initialisation.  These small functions exist so we don't
   duplicate code between module init and apm resume */

static inline int ali_ac97_exists(struct ali_card *card, int ac97_number)
{
	unsigned int i = 1;
	u32 reg = inl(card->iobase + ALI_RTSR);
	if (ac97_number) {
		while (i < 100) {

			reg = inl(card->iobase + ALI_RTSR);
			if (reg & 0x40) {
				break;
			} else {
				outl(reg | 0x00000040,
				     card->iobase + 0x34);
				udelay(1);
			}
			i++;
		}

	} else {
		while (i < 100) {
			reg = inl(card->iobase + ALI_RTSR);
			if (reg & 0x80) {
				break;
			} else {
				outl(reg | 0x00000080,
				     card->iobase + 0x34);
				udelay(1);
			}
			i++;
		}
	}

	if (ac97_number)
		return reg & 0x40;
	else
		return reg & 0x80;
}

static inline int ali_ac97_enable_variable_rate(struct ac97_codec *codec)
{
	ali_ac97_set(codec, AC97_EXTENDED_STATUS, 9);
	ali_ac97_set(codec, AC97_EXTENDED_STATUS, ali_ac97_get(codec, AC97_EXTENDED_STATUS) | 0xE800);
	return (ali_ac97_get(codec, AC97_EXTENDED_STATUS) & 1);
}


static int ali_ac97_probe_and_powerup(struct ali_card *card, struct ac97_codec *codec)
{
	/* Returns 0 on failure */
	int i;
	u16 addr;
	if (ac97_probe_codec(codec) == 0)
		return 0;
	/* ac97_probe_codec is success ,then begin to init codec */
	ali_ac97_set(codec, AC97_RESET, 0xffff);
	if (card->channel[0].used == 1) {
		ali_ac97_set(codec, AC97_RECORD_SELECT, 0x0000);
		ali_ac97_set(codec, AC97_LINEIN_VOL, 0x0808);
		ali_ac97_set(codec, AC97_RECORD_GAIN, 0x0F0F);
	}

	if (card->channel[2].used == 1)	//if MICin then init codec
	{
		ali_ac97_set(codec, AC97_RECORD_SELECT, 0x0000);
		ali_ac97_set(codec, AC97_MIC_VOL, 0x8808);
		ali_ac97_set(codec, AC97_RECORD_GAIN, 0x0F0F);
		ali_ac97_set(codec, AC97_RECORD_GAIN_MIC, 0x0000);
	}

	ali_ac97_set(codec, AC97_MASTER_VOL_STEREO, 0x0000);
	ali_ac97_set(codec, AC97_HEADPHONE_VOL, 0x0000);
	ali_ac97_set(codec, AC97_PCMOUT_VOL, 0x0000);
	ali_ac97_set(codec, AC97_CD_VOL, 0x0808);
	ali_ac97_set(codec, AC97_VIDEO_VOL, 0x0808);
	ali_ac97_set(codec, AC97_AUX_VOL, 0x0808);
	ali_ac97_set(codec, AC97_PHONE_VOL, 0x8048);
	ali_ac97_set(codec, AC97_PCBEEP_VOL, 0x0000);
	ali_ac97_set(codec, AC97_GENERAL_PURPOSE, AC97_GP_MIX);
	ali_ac97_set(codec, AC97_MASTER_VOL_MONO, 0x0000);
	ali_ac97_set(codec, 0x38, 0x0000);
	addr = ali_ac97_get(codec, 0x2a);
	ali_ac97_set(codec, 0x2a, addr | 0x0001);
	addr = ali_ac97_get(codec, 0x2a);
	addr = ali_ac97_get(codec, 0x28);
	ali_ac97_set(codec, 0x2c, 0xbb80);
	addr = ali_ac97_get(codec, 0x2c);
	/* power it all up */
	ali_ac97_set(codec, AC97_POWER_CONTROL,
		     ali_ac97_get(codec, AC97_POWER_CONTROL) & ~0x7f00);
	/* wait for analog ready */
	for (i = 10; i && ((ali_ac97_get(codec, AC97_POWER_CONTROL) & 0xf) != 0xf); i--) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(HZ / 20);
	}
	/* FIXME !! */
	i++;
	return i;
}


/* I clone ali5455(2.4.7 )  not clone i810_audio(2.4.18)  */

static int ali_reset_5455(struct ali_card *card)
{
	outl(0x80000003, card->iobase + ALI_SCR);
	outl(0x83838383, card->iobase + ALI_FIFOCR1);
	outl(0x83838383, card->iobase + ALI_FIFOCR2);
	if (controller_pcmout_share_spdif_locked > 0) {
		outl((inl(card->iobase + ALI_SPDIFICS) | 0x00000001),
		     card->iobase + ALI_SPDIFICS);
		outl(0x0408000a, card->iobase + ALI_INTERFACECR);
	} else {
		if (codec_independent_spdif_locked > 0) {
			outl((inl(card->iobase + ALI_SCR) | 0x00100000), card->iobase + ALI_SCR);	// now I select slot 7 & 8
			outl(0x00200000, card->iobase + ALI_INTERFACECR);	//enable codec independent spdifout 
		} else
			outl(0x04080002, card->iobase + ALI_INTERFACECR);
	}

	outl(0x00000000, card->iobase + ALI_INTERRUPTCR);
	outl(0x00000000, card->iobase + ALI_INTERRUPTSR);
	if (controller_independent_spdif_locked > 0)
		outl((inl(card->iobase + ALI_SPDIFICS) | 0x00000001),
		     card->iobase + ALI_SPDIFICS);
	return 1;
}


static int ali_ac97_random_init_stuff(struct ali_card
				      *card)
{
	u32 reg = inl(card->iobase + ALI_SCR);
	int i = 0;
	reg = inl(card->iobase + ALI_SCR);
	if ((reg & 2) == 0)	/* Cold required */
		reg |= 2;
	else
		reg |= 1;	/* Warm */
	reg &= ~0x80000000;	/* ACLink on */
	outl(reg, card->iobase + ALI_SCR);

	while (i < 10) {
		if ((inl(card->iobase + 0x18) & (1 << 1)) == 0)
			break;
		current->state = TASK_UNINTERRUPTIBLE;
		schedule_timeout(HZ / 20);
		i++;
	}
	if (i == 10) {
		printk(KERN_ERR "ali_audio: AC'97 reset failed.\n");
		return 0;
	}

	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout(HZ / 2);
	return 1;
}

/* AC97 codec initialisation. */

static int __devinit ali_ac97_init(struct ali_card *card)
{
	int num_ac97 = 0;
	int total_channels = 0;
	struct ac97_codec *codec;
	u16 eid;

	if (!ali_ac97_random_init_stuff(card))
		return 0;

	/* Number of channels supported */
	/* What about the codec?  Just because the ICH supports */
	/* multiple channels doesn't mean the codec does.       */
	/* we'll have to modify this in the codec section below */
	/* to reflect what the codec has.                       */
	/* ICH and ICH0 only support 2 channels so don't bother */
	/* to check....                                         */
	inl(card->iobase + ALI_CPR);
	card->channels = 2;

	for (num_ac97 = 0; num_ac97 < NR_AC97; num_ac97++) {

		/* Assume codec isn't available until we go through the
		 * gauntlet below */
		card->ac97_codec[num_ac97] = NULL;
		/* The ICH programmer's reference says you should   */
		/* check the ready status before probing. So we chk */
		/*   What do we do if it's not ready?  Wait and try */
		/*   again, or abort?                               */
		if (!ali_ac97_exists(card, num_ac97)) {
			if (num_ac97 == 0)
				printk(KERN_ERR "ali_audio: Primary codec not ready.\n");
			break;
		}

		if ((codec = ac97_alloc_codec()) == NULL)
			return -ENOMEM;
		/* initialize some basic codec information, other fields will be filled
		   in ac97_probe_codec */
		codec->private_data = card;
		codec->id = num_ac97;
		codec->codec_read = ali_ac97_get;
		codec->codec_write = ali_ac97_set;
		if (!ali_ac97_probe_and_powerup(card, codec)) {
			printk(KERN_ERR "ali_audio: timed out waiting for codec %d analog ready",
			     num_ac97);
			kfree(codec);
			break;	/* it didn't work */
		}
		
		/* Store state information about S/PDIF transmitter */
		card->ac97_status = 0;
		/* Don't attempt to get eid until powerup is complete */
		eid = ali_ac97_get(codec, AC97_EXTENDED_ID);
		if (eid == 0xFFFF) {
			printk(KERN_ERR "ali_audio: no codec attached ?\n");
			kfree(codec);
			break;
		}

		card->ac97_features = eid;
		/* Now check the codec for useful features to make up for
		   the dumbness of the ali5455 hardware engine */
		if (!(eid & 0x0001))
			printk(KERN_WARNING
			       "ali_audio: only 48Khz playback available.\n");
		else {
			if (!ali_ac97_enable_variable_rate(codec)) {
				printk(KERN_WARNING
				       "ali_audio: Codec refused to allow VRA, using 48Khz only.\n");
				card->ac97_features &= ~1;
			}
		}

		/* Determine how many channels the codec(s) support   */
		/*   - The primary codec always supports 2            */
		/*   - If the codec supports AMAP, surround DACs will */
		/*     automaticlly get assigned to slots.            */
		/*     * Check for surround DACs and increment if     */
		/*       found.                                       */
		/*   - Else check if the codec is revision 2.2        */
		/*     * If surround DACs exist, assign them to slots */
		/*       and increment channel count.                 */

		/* All of this only applies to ICH2 and above. ICH    */
		/* and ICH0 only support 2 channels.  ICH2 will only  */
		/* support multiple codecs in a "split audio" config. */
		/* as described above.                                */

		/* TODO: Remove all the debugging messages!           */

		if ((eid & 0xc000) == 0)	/* primary codec */
			total_channels += 2;
		if ((codec->dev_mixer = register_sound_mixer(&ali_mixer_fops, -1)) < 0) {
			printk(KERN_ERR "ali_audio: couldn't register mixer!\n");
			kfree(codec);
			break;
		}
		card->ac97_codec[num_ac97] = codec;
	}
	/* pick the minimum of channels supported by ICHx or codec(s) */
	card->channels = (card->channels > total_channels) ? total_channels : card->channels;
	return num_ac97;
}

static void __devinit ali_configure_clocking(void)
{
	struct ali_card *card;
	struct ali_state *state;
	struct dmabuf *dmabuf;
	unsigned int i, offset, new_offset;
	unsigned long flags;
	card = devs;

	/* We could try to set the clocking for multiple cards, but can you even have
	 * more than one ali in a machine?  Besides, clocking is global, so unless
	 * someone actually thinks more than one ali in a machine is possible and
	 * decides to rewrite that little bit, setting the rate for more than one card
	 * is a waste of time.
	 */
	if (card != NULL) {
		state = card->states[0] = (struct ali_state *)
		    kmalloc(sizeof(struct ali_state), GFP_KERNEL);
		if (state == NULL)
			return;
		memset(state, 0, sizeof(struct ali_state));
		dmabuf = &state->dmabuf;
		dmabuf->write_channel = card->alloc_pcm_channel(card);
		state->virt = 0;
		state->card = card;
		state->magic = ALI5455_STATE_MAGIC;
		init_waitqueue_head(&dmabuf->wait);
		mutex_init(&state->open_mutex);
		dmabuf->fmt = ALI5455_FMT_STEREO | ALI5455_FMT_16BIT;
		dmabuf->trigger = PCM_ENABLE_OUTPUT;
		ali_set_dac_rate(state, 48000);
		if (prog_dmabuf(state, 0) != 0)
			goto config_out_nodmabuf;
		
		if (dmabuf->dmasize < 16384)
			goto config_out;
		
		dmabuf->count = dmabuf->dmasize;
		outb(31, card->iobase + dmabuf->write_channel->port + OFF_LVI);

		local_irq_save(flags);
		start_dac(state);
		offset = ali_get_dma_addr(state, 0);
		mdelay(50);
		new_offset = ali_get_dma_addr(state, 0);
		stop_dac(state);
		
		outb(2, card->iobase + dmabuf->write_channel->port + OFF_CR);
		local_irq_restore(flags);

		i = new_offset - offset;

		if (i == 0)
			goto config_out;
		i = i / 4 * 20;
		if (i > 48500 || i < 47500) {
			clocking = clocking * clocking / i;
		}
config_out:
		dealloc_dmabuf(state);
config_out_nodmabuf:
		state->card->free_pcm_channel(state->card, state->dmabuf. write_channel->num);
		kfree(state);
		card->states[0] = NULL;
	}
}

/* install the driver, we do not allocate hardware channel nor DMA buffer now, they are defered 
   until "ACCESS" time (in prog_dmabuf called by open/read/write/ioctl/mmap) */

static int __devinit ali_probe(struct pci_dev *pci_dev,
			       const struct pci_device_id *pci_id)
{
	struct ali_card *card;
	if (pci_enable_device(pci_dev))
		return -EIO;
	if (pci_set_dma_mask(pci_dev, ALI5455_DMA_MASK)) {
		printk(KERN_ERR "ali5455: architecture does not support"
		       " 32bit PCI busmaster DMA\n");
		return -ENODEV;
	}

	if ((card = kmalloc(sizeof(struct ali_card), GFP_KERNEL)) == NULL) {
		printk(KERN_ERR "ali_audio: out of memory\n");
		return -ENOMEM;
	}
	memset(card, 0, sizeof(*card));
	card->initializing = 1;
	card->iobase = pci_resource_start(pci_dev, 0);
	card->pci_dev = pci_dev;
	card->pci_id = pci_id->device;
	card->irq = pci_dev->irq;
	card->next = devs;
	card->magic = ALI5455_CARD_MAGIC;
#ifdef CONFIG_PM
	card->pm_suspended = 0;
#endif
	spin_lock_init(&card->lock);
	spin_lock_init(&card->ac97_lock);
	devs = card;
	pci_set_master(pci_dev);
	printk(KERN_INFO "ali: %s found at IO 0x%04lx, IRQ %d\n",
	       card_names[pci_id->driver_data], card->iobase, card->irq);
	card->alloc_pcm_channel = ali_alloc_pcm_channel;
	card->alloc_rec_pcm_channel = ali_alloc_rec_pcm_channel;
	card->alloc_rec_mic_channel = ali_alloc_rec_mic_channel;
	card->alloc_codec_spdifout_channel = ali_alloc_codec_spdifout_channel;
	card->alloc_controller_spdifout_channel = ali_alloc_controller_spdifout_channel;
	card->free_pcm_channel = ali_free_pcm_channel;
	card->channel[0].offset = 0;
	card->channel[0].port = 0x40;
	card->channel[0].num = 0;
	card->channel[1].offset = 0;
	card->channel[1].port = 0x50;
	card->channel[1].num = 1;
	card->channel[2].offset = 0;
	card->channel[2].port = 0x60;
	card->channel[2].num = 2;
	card->channel[3].offset = 0;
	card->channel[3].port = 0x70;
	card->channel[3].num = 3;
	card->channel[4].offset = 0;
	card->channel[4].port = 0xb0;
	card->channel[4].num = 4;
	/* claim our iospace and irq */
	request_region(card->iobase, 256, card_names[pci_id->driver_data]);
	if (request_irq(card->irq, &ali_interrupt, SA_SHIRQ,
			card_names[pci_id->driver_data], card)) {
		printk(KERN_ERR "ali_audio: unable to allocate irq %d\n",
		       card->irq);
		release_region(card->iobase, 256);
		kfree(card);
		return -ENODEV;
	}

	if (ali_reset_5455(card) <= 0) {
		unregister_sound_dsp(card->dev_audio);
		release_region(card->iobase, 256);
		free_irq(card->irq, card);
		kfree(card);
		return -ENODEV;
	}

	/* initialize AC97 codec and register /dev/mixer */
	if (ali_ac97_init(card) < 0) {
		release_region(card->iobase, 256);
		free_irq(card->irq, card);
		kfree(card);
		return -ENODEV;
	}
	
	pci_set_drvdata(pci_dev, card);
	
	if (clocking == 0) {
		clocking = 48000;
		ali_configure_clocking();
	}

	/* register /dev/dsp */
	if ((card->dev_audio = register_sound_dsp(&ali_audio_fops, -1)) < 0) {
		int i;
		printk(KERN_ERR"ali_audio: couldn't register DSP device!\n");
		release_region(card->iobase, 256);
		free_irq(card->irq, card);
		for (i = 0; i < NR_AC97; i++)
			if (card->ac97_codec[i] != NULL) {
				unregister_sound_mixer(card->ac97_codec[i]->dev_mixer);
				kfree(card->ac97_codec[i]);
			}
		kfree(card);
		return -ENODEV;
	}
	card->initializing = 0;
	return 0;
}

static void __devexit ali_remove(struct pci_dev *pci_dev)
{
	int i;
	struct ali_card *card = pci_get_drvdata(pci_dev);
	/* free hardware resources */
	free_irq(card->irq, devs);
	release_region(card->iobase, 256);
	/* unregister audio devices */
	for (i = 0; i < NR_AC97; i++)
		if (card->ac97_codec[i] != NULL) {
			unregister_sound_mixer(card->ac97_codec[i]->
					       dev_mixer);
			ac97_release_codec(card->ac97_codec[i]);
			card->ac97_codec[i] = NULL;
		}
	unregister_sound_dsp(card->dev_audio);
	kfree(card);
}

#ifdef CONFIG_PM
static int ali_pm_suspend(struct pci_dev *dev, pm_message_t pm_state)
{
	struct ali_card *card = pci_get_drvdata(dev);
	struct ali_state *state;
	unsigned long flags;
	struct dmabuf *dmabuf;
	int i, num_ac97;

	if (!card)
		return 0;
	spin_lock_irqsave(&card->lock, flags);
	card->pm_suspended = 1;
	for (i = 0; i < NR_HW_CH; i++) {
		state = card->states[i];
		if (!state)
			continue;
		/* this happens only if there are open files */
		dmabuf = &state->dmabuf;
		if (dmabuf->enable & DAC_RUNNING ||
		    (dmabuf->count
		     && (dmabuf->trigger & PCM_ENABLE_OUTPUT))) {
			state->pm_saved_dac_rate = dmabuf->rate;
			stop_dac(state);
		} else {
			state->pm_saved_dac_rate = 0;
		}
		if (dmabuf->enable & ADC_RUNNING) {
			state->pm_saved_adc_rate = dmabuf->rate;
			stop_adc(state);
		} else {
			state->pm_saved_adc_rate = 0;
		}
		dmabuf->ready = 0;
		dmabuf->swptr = dmabuf->hwptr = 0;
		dmabuf->count = dmabuf->total_bytes = 0;
	}

	spin_unlock_irqrestore(&card->lock, flags);
	/* save mixer settings */
	for (num_ac97 = 0; num_ac97 < NR_AC97; num_ac97++) {
		struct ac97_codec *codec = card->ac97_codec[num_ac97];
		if (!codec)
			continue;
		for (i = 0; i < SOUND_MIXER_NRDEVICES; i++) {
			if ((supported_mixer(codec, i)) && (codec->read_mixer)) {
				card->pm_saved_mixer_settings[i][num_ac97] = codec->read_mixer(codec, i);
			}
		}
	}
	pci_save_state(dev);	/* XXX do we need this? */
	pci_disable_device(dev);	/* disable busmastering */
	pci_set_power_state(dev, 3);	/* Zzz. */
	return 0;
}


static int ali_pm_resume(struct pci_dev *dev)
{
	int num_ac97, i = 0;
	struct ali_card *card = pci_get_drvdata(dev);
	pci_enable_device(dev);
	pci_restore_state(dev);
	/* observation of a toshiba portege 3440ct suggests that the 
	   hardware has to be more or less completely reinitialized from
	   scratch after an apm suspend.  Works For Me.   -dan */
	ali_ac97_random_init_stuff(card);
	for (num_ac97 = 0; num_ac97 < NR_AC97; num_ac97++) {
		struct ac97_codec *codec = card->ac97_codec[num_ac97];
		/* check they haven't stolen the hardware while we were
		   away */
		if (!codec || !ali_ac97_exists(card, num_ac97)) {
			if (num_ac97)
				continue;
			else
				BUG();
		}
		if (!ali_ac97_probe_and_powerup(card, codec))
			BUG();
		if ((card->ac97_features & 0x0001)) {
			/* at probe time we found we could do variable
			   rates, but APM suspend has made it forget
			   its magical powers */
			if (!ali_ac97_enable_variable_rate(codec))
				BUG();
		}
		/* we lost our mixer settings, so restore them */
		for (i = 0; i < SOUND_MIXER_NRDEVICES; i++) {
			if (supported_mixer(codec, i)) {
				int val = card->pm_saved_mixer_settings[i][num_ac97];
				codec->mixer_state[i] = val;
				codec->write_mixer(codec, i,
						   (val & 0xff),
						   ((val >> 8) & 0xff));
			}
		}
	}

	/* we need to restore the sample rate from whatever it was */
	for (i = 0; i < NR_HW_CH; i++) {
		struct ali_state *state = card->states[i];
		if (state) {
			if (state->pm_saved_adc_rate)
				ali_set_adc_rate(state, state->pm_saved_adc_rate);
			if (state->pm_saved_dac_rate)
				ali_set_dac_rate(state, state->pm_saved_dac_rate);
		}
	}

	card->pm_suspended = 0;
	/* any processes that were reading/writing during the suspend
	   probably ended up here */
	for (i = 0; i < NR_HW_CH; i++) {
		struct ali_state *state = card->states[i];
		if (state)
			wake_up(&state->dmabuf.wait);
	}
	return 0;
}
#endif				/* CONFIG_PM */

MODULE_AUTHOR("");
MODULE_DESCRIPTION("ALI 5455 audio support");
MODULE_LICENSE("GPL");
module_param(clocking, int, 0);
/* FIXME: bool? */
module_param(strict_clocking, uint, 0);
module_param(codec_pcmout_share_spdif_locked, uint, 0);
module_param(codec_independent_spdif_locked, uint, 0);
module_param(controller_pcmout_share_spdif_locked, uint, 0);
module_param(controller_independent_spdif_locked, uint, 0);
#define ALI5455_MODULE_NAME "ali5455"
static struct pci_driver ali_pci_driver = {
	.name		= ALI5455_MODULE_NAME,
	.id_table	= ali_pci_tbl,
	.probe		= ali_probe,
	.remove		= __devexit_p(ali_remove),
#ifdef CONFIG_PM
	.suspend	= ali_pm_suspend,
	.resume		= ali_pm_resume,
#endif				/* CONFIG_PM */
};

static int __init ali_init_module(void)
{
	printk(KERN_INFO "ALI 5455 + AC97 Audio, version "
	       DRIVER_VERSION ", " __TIME__ " " __DATE__ "\n");

	if (codec_independent_spdif_locked > 0) {
		if (codec_independent_spdif_locked == 32000
		    || codec_independent_spdif_locked == 44100
		    || codec_independent_spdif_locked == 48000) {
			printk(KERN_INFO "ali_audio: Enabling S/PDIF at sample rate %dHz.\n", codec_independent_spdif_locked);
		} else {
			printk(KERN_INFO "ali_audio: S/PDIF can only be locked to 32000, 44100, or 48000Hz.\n");
			codec_independent_spdif_locked = 0;
		}
	}
	if (controller_independent_spdif_locked > 0) {
		if (controller_independent_spdif_locked == 32000
		    || controller_independent_spdif_locked == 44100
		    || controller_independent_spdif_locked == 48000) {
			printk(KERN_INFO "ali_audio: Enabling S/PDIF at sample rate %dHz.\n", controller_independent_spdif_locked);
		} else {
			printk(KERN_INFO "ali_audio: S/PDIF can only be locked to 32000, 44100, or 48000Hz.\n");
			controller_independent_spdif_locked = 0;
		}
	}

	if (codec_pcmout_share_spdif_locked > 0) {
		if (codec_pcmout_share_spdif_locked == 32000
		    || codec_pcmout_share_spdif_locked == 44100
		    || codec_pcmout_share_spdif_locked == 48000) {
			printk(KERN_INFO "ali_audio: Enabling S/PDIF at sample rate %dHz.\n", codec_pcmout_share_spdif_locked);
		} else {
			printk(KERN_INFO "ali_audio: S/PDIF can only be locked to 32000, 44100, or 48000Hz.\n");
			codec_pcmout_share_spdif_locked = 0;
		}
	}
	if (controller_pcmout_share_spdif_locked > 0) {
		if (controller_pcmout_share_spdif_locked == 32000
		    || controller_pcmout_share_spdif_locked == 44100
		    || controller_pcmout_share_spdif_locked == 48000) {
			printk(KERN_INFO "ali_audio: Enabling controller S/PDIF at sample rate %dHz.\n", controller_pcmout_share_spdif_locked);
		} else {
			printk(KERN_INFO "ali_audio: S/PDIF can only be locked to 32000, 44100, or 48000Hz.\n");
			controller_pcmout_share_spdif_locked = 0;
		}
	}
	return pci_register_driver(&ali_pci_driver);
}

static void __exit ali_cleanup_module(void)
{
	pci_unregister_driver(&ali_pci_driver);
}

module_init(ali_init_module);
module_exit(ali_cleanup_module);
/*
Local Variables:
c-basic-offset: 8
End:
*/
