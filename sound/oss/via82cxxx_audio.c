/*
 * Support for VIA 82Cxxx Audio Codecs
 * Copyright 1999,2000 Jeff Garzik
 *
 * Updated to support the VIA 8233/8235 audio subsystem
 * Alan Cox <alan@redhat.com> (C) Copyright 2002, 2003 Red Hat Inc
 *
 * Distributed under the GNU GENERAL PUBLIC LICENSE (GPL) Version 2.
 * See the "COPYING" file distributed with this software for more info.
 * NO WARRANTY
 *
 * For a list of known bugs (errata) and documentation,
 * see via-audio.pdf in Documentation/DocBook.
 * If this documentation does not exist, run "make pdfdocs".
 */


#define VIA_VERSION	"1.9.1-ac4-2.5"


#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/pci.h>
#include <linux/poison.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/proc_fs.h>
#include <linux/spinlock.h>
#include <linux/sound.h>
#include <linux/poll.h>
#include <linux/soundcard.h>
#include <linux/ac97_codec.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <linux/mutex.h>

#include "sound_config.h"
#include "dev_table.h"
#include "mpu401.h"


#undef VIA_DEBUG	/* define to enable debugging output and checks */
#ifdef VIA_DEBUG
/* note: prints function name for you */
#define DPRINTK(fmt, args...) printk(KERN_DEBUG "%s: " fmt, __FUNCTION__ , ## args)
#else
#define DPRINTK(fmt, args...)
#endif

#undef VIA_NDEBUG	/* define to disable lightweight runtime checks */
#ifdef VIA_NDEBUG
#define assert(expr)
#else
#define assert(expr) \
        if(!(expr)) {					\
        printk( "Assertion failed! %s,%s,%s,line=%d\n",	\
        #expr,__FILE__,__FUNCTION__,__LINE__);		\
        }
#endif

#define VIA_SUPPORT_MMAP 1 /* buggy, for now... */

#define MAX_CARDS	1

#define VIA_CARD_NAME	"VIA 82Cxxx Audio driver " VIA_VERSION
#define VIA_MODULE_NAME "via82cxxx"
#define PFX		VIA_MODULE_NAME ": "

#define VIA_COUNTER_LIMIT	100000

/* size of DMA buffers */
#define VIA_MAX_BUFFER_DMA_PAGES	32

/* buffering default values in ms */
#define VIA_DEFAULT_FRAG_TIME		20
#define VIA_DEFAULT_BUFFER_TIME		500

/* the hardware has a 256 fragment limit */
#define VIA_MIN_FRAG_NUMBER		2
#define VIA_MAX_FRAG_NUMBER		128

#define VIA_MAX_FRAG_SIZE		PAGE_SIZE
#define VIA_MIN_FRAG_SIZE		(VIA_MAX_BUFFER_DMA_PAGES * PAGE_SIZE / VIA_MAX_FRAG_NUMBER)


/* 82C686 function 5 (audio codec) PCI configuration registers */
#define VIA_ACLINK_STATUS	0x40
#define VIA_ACLINK_CTRL		0x41
#define VIA_FUNC_ENABLE		0x42
#define VIA_PNP_CONTROL		0x43
#define VIA_FM_NMI_CTRL		0x48

/*
 * controller base 0 (scatter-gather) registers
 *
 * NOTE: Via datasheet lists first channel as "read"
 * channel and second channel as "write" channel.
 * I changed the naming of the constants to be more
 * clear than I felt the datasheet to be.
 */

#define VIA_BASE0_PCM_OUT_CHAN	0x00 /* output PCM to user */
#define VIA_BASE0_PCM_OUT_CHAN_STATUS 0x00
#define VIA_BASE0_PCM_OUT_CHAN_CTRL	0x01
#define VIA_BASE0_PCM_OUT_CHAN_TYPE	0x02

#define VIA_BASE0_PCM_IN_CHAN		0x10 /* input PCM from user */
#define VIA_BASE0_PCM_IN_CHAN_STATUS	0x10
#define VIA_BASE0_PCM_IN_CHAN_CTRL	0x11
#define VIA_BASE0_PCM_IN_CHAN_TYPE	0x12

/* offsets from base */
#define VIA_PCM_STATUS			0x00
#define VIA_PCM_CONTROL			0x01
#define VIA_PCM_TYPE			0x02
#define VIA_PCM_LEFTVOL			0x02
#define VIA_PCM_RIGHTVOL		0x03
#define VIA_PCM_TABLE_ADDR		0x04
#define VIA_PCM_STOPRATE		0x08	/* 8233+ */
#define VIA_PCM_BLOCK_COUNT		0x0C

/* XXX unused DMA channel for FM PCM data */
#define VIA_BASE0_FM_OUT_CHAN		0x20
#define VIA_BASE0_FM_OUT_CHAN_STATUS	0x20
#define VIA_BASE0_FM_OUT_CHAN_CTRL	0x21
#define VIA_BASE0_FM_OUT_CHAN_TYPE	0x22

/* Six channel audio output on 8233 */
#define VIA_BASE0_MULTI_OUT_CHAN		0x40
#define VIA_BASE0_MULTI_OUT_CHAN_STATUS		0x40
#define VIA_BASE0_MULTI_OUT_CHAN_CTRL		0x41
#define VIA_BASE0_MULTI_OUT_CHAN_TYPE		0x42

#define VIA_BASE0_AC97_CTRL		0x80
#define VIA_BASE0_SGD_STATUS_SHADOW	0x84
#define VIA_BASE0_GPI_INT_ENABLE	0x8C
#define VIA_INTR_OUT			((1<<0) |  (1<<4) |  (1<<8))
#define VIA_INTR_IN			((1<<1) |  (1<<5) |  (1<<9))
#define VIA_INTR_FM			((1<<2) |  (1<<6) | (1<<10))
#define VIA_INTR_MASK		(VIA_INTR_OUT | VIA_INTR_IN | VIA_INTR_FM)

/* Newer VIA we need to monitor the low 3 bits of each channel. This
   mask covers the channels we don't yet use as well 
 */
 
#define VIA_NEW_INTR_MASK		0x77077777UL

/* VIA_BASE0_AUDIO_xxx_CHAN_TYPE bits */
#define VIA_IRQ_ON_FLAG			(1<<0)	/* int on each flagged scatter block */
#define VIA_IRQ_ON_EOL			(1<<1)	/* int at end of scatter list */
#define VIA_INT_SEL_PCI_LAST_LINE_READ	(0)	/* int at PCI read of last line */
#define VIA_INT_SEL_LAST_SAMPLE_SENT	(1<<2)	/* int at last sample sent */
#define VIA_INT_SEL_ONE_LINE_LEFT	(1<<3)	/* int at less than one line to send */
#define VIA_PCM_FMT_STEREO		(1<<4)	/* PCM stereo format (bit clear == mono) */
#define VIA_PCM_FMT_16BIT		(1<<5)	/* PCM 16-bit format (bit clear == 8-bit) */
#define VIA_PCM_REC_FIFO		(1<<6)	/* PCM Recording FIFO */
#define VIA_RESTART_SGD_ON_EOL		(1<<7)	/* restart scatter-gather at EOL */
#define VIA_PCM_FMT_MASK		(VIA_PCM_FMT_STEREO|VIA_PCM_FMT_16BIT)
#define VIA_CHAN_TYPE_MASK		(VIA_RESTART_SGD_ON_EOL | \
					 VIA_IRQ_ON_FLAG | \
					 VIA_IRQ_ON_EOL)
#define VIA_CHAN_TYPE_INT_SELECT	(VIA_INT_SEL_LAST_SAMPLE_SENT)

/* PCI configuration register bits and masks */
#define VIA_CR40_AC97_READY	0x01
#define VIA_CR40_AC97_LOW_POWER	0x02
#define VIA_CR40_SECONDARY_READY 0x04

#define VIA_CR41_AC97_ENABLE	0x80 /* enable AC97 codec */
#define VIA_CR41_AC97_RESET	0x40 /* clear bit to reset AC97 */
#define VIA_CR41_AC97_WAKEUP	0x20 /* wake up from power-down mode */
#define VIA_CR41_AC97_SDO	0x10 /* force Serial Data Out (SDO) high */
#define VIA_CR41_VRA		0x08 /* enable variable sample rate */
#define VIA_CR41_PCM_ENABLE	0x04 /* AC Link SGD Read Channel PCM Data Output */
#define VIA_CR41_FM_PCM_ENABLE	0x02 /* AC Link FM Channel PCM Data Out */
#define VIA_CR41_SB_PCM_ENABLE	0x01 /* AC Link SB PCM Data Output */
#define VIA_CR41_BOOT_MASK	(VIA_CR41_AC97_ENABLE | \
				 VIA_CR41_AC97_WAKEUP | \
				 VIA_CR41_AC97_SDO)
#define VIA_CR41_RUN_MASK	(VIA_CR41_AC97_ENABLE | \
				 VIA_CR41_AC97_RESET | \
				 VIA_CR41_VRA | \
				 VIA_CR41_PCM_ENABLE)

#define VIA_CR42_SB_ENABLE	0x01
#define VIA_CR42_MIDI_ENABLE	0x02
#define VIA_CR42_FM_ENABLE	0x04
#define VIA_CR42_GAME_ENABLE	0x08
#define VIA_CR42_MIDI_IRQMASK   0x40
#define VIA_CR42_MIDI_PNP	0x80

#define VIA_CR44_SECOND_CODEC_SUPPORT	(1 << 6)
#define VIA_CR44_AC_LINK_ACCESS		(1 << 7)

#define VIA_CR48_FM_TRAP_TO_NMI		(1 << 2)

/* controller base 0 register bitmasks */
#define VIA_INT_DISABLE_MASK		(~(0x01|0x02))
#define VIA_SGD_STOPPED			(1 << 2)
#define VIA_SGD_PAUSED			(1 << 6)
#define VIA_SGD_ACTIVE			(1 << 7)
#define VIA_SGD_TERMINATE		(1 << 6)
#define VIA_SGD_FLAG			(1 << 0)
#define VIA_SGD_EOL			(1 << 1)
#define VIA_SGD_START			(1 << 7)

#define VIA_CR80_FIRST_CODEC		0
#define VIA_CR80_SECOND_CODEC		(1 << 30)
#define VIA_CR80_FIRST_CODEC_VALID	(1 << 25)
#define VIA_CR80_VALID			(1 << 25)
#define VIA_CR80_SECOND_CODEC_VALID	(1 << 27)
#define VIA_CR80_BUSY			(1 << 24)
#define VIA_CR83_BUSY			(1)
#define VIA_CR83_FIRST_CODEC_VALID	(1 << 1)
#define VIA_CR80_READ			(1 << 23)
#define VIA_CR80_WRITE_MODE		0
#define VIA_CR80_REG_IDX(idx)		((((idx) & 0xFF) >> 1) << 16)

/* capabilities we announce */
#ifdef VIA_SUPPORT_MMAP
#define VIA_DSP_CAP (DSP_CAP_REVISION | DSP_CAP_DUPLEX | DSP_CAP_MMAP | \
		     DSP_CAP_TRIGGER | DSP_CAP_REALTIME)
#else
#define VIA_DSP_CAP (DSP_CAP_REVISION | DSP_CAP_DUPLEX | \
		     DSP_CAP_TRIGGER | DSP_CAP_REALTIME)
#endif

/* scatter-gather DMA table entry, exactly as passed to hardware */
struct via_sgd_table {
	u32 addr;
	u32 count;	/* includes additional VIA_xxx bits also */
};

#define VIA_EOL (1 << 31)
#define VIA_FLAG (1 << 30)
#define VIA_STOP (1 << 29)


enum via_channel_states {
	sgd_stopped = 0,
	sgd_in_progress = 1,
};


struct via_buffer_pgtbl {
	dma_addr_t handle;
	void *cpuaddr;
};


struct via_channel {
	atomic_t n_frags;
	atomic_t hw_ptr;
	wait_queue_head_t wait;

	unsigned int sw_ptr;
	unsigned int slop_len;
	unsigned int n_irqs;
	int bytes;

	unsigned is_active : 1;
	unsigned is_record : 1;
	unsigned is_mapped : 1;
	unsigned is_enabled : 1;
	unsigned is_multi: 1;	/* 8233 6 channel */
	u8 pcm_fmt;		/* VIA_PCM_FMT_xxx */
	u8 channels;		/* Channel count */

	unsigned rate;		/* sample rate */
	unsigned int frag_size;
	unsigned int frag_number;
	
	unsigned char intmask;

	volatile struct via_sgd_table *sgtable;
	dma_addr_t sgt_handle;

	unsigned int page_number;
	struct via_buffer_pgtbl pgtbl[VIA_MAX_BUFFER_DMA_PAGES];

	long iobase;

	const char *name;
};


/* data stored for each chip */
struct via_info {
	struct pci_dev *pdev;
	long baseaddr;

	struct ac97_codec *ac97;
	spinlock_t ac97_lock;
	spinlock_t lock;
	int card_num;		/* unique card number, from 0 */

	int dev_dsp;		/* /dev/dsp index from register_sound_dsp() */

	unsigned rev_h : 1;
	unsigned legacy: 1;	/* Has legacy ports */
	unsigned intmask: 1;	/* Needs int bits */
	unsigned sixchannel: 1;	/* 8233/35 with 6 channel support */
	unsigned volume: 1;

	unsigned locked_rate : 1;
	
	int mixer_vol;		/* 8233/35 volume  - not yet implemented */

	struct mutex syscall_mutex;
	struct mutex open_mutex;

	/* The 8233/8235 have 4 DX audio channels, two record and
	   one six channel out. We bind ch_in to DX 1, ch_out to multichannel
	   and ch_fm to DX 2. DX 3 and REC0/REC1 are unused at the
	   moment */
	   
	struct via_channel ch_in;
	struct via_channel ch_out;
	struct via_channel ch_fm;

#ifdef CONFIG_MIDI_VIA82CXXX
        void *midi_devc;
        struct address_info midi_info;
#endif
};


/* number of cards, used for assigning unique numbers to cards */
static unsigned via_num_cards;



/****************************************************************
 *
 * prototypes
 *
 *
 */

static int via_init_one (struct pci_dev *dev, const struct pci_device_id *id);
static void __devexit via_remove_one (struct pci_dev *pdev);

static ssize_t via_dsp_read(struct file *file, char __user *buffer, size_t count, loff_t *ppos);
static ssize_t via_dsp_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos);
static unsigned int via_dsp_poll(struct file *file, struct poll_table_struct *wait);
static int via_dsp_ioctl (struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg);
static int via_dsp_open (struct inode *inode, struct file *file);
static int via_dsp_release(struct inode *inode, struct file *file);
static int via_dsp_mmap(struct file *file, struct vm_area_struct *vma);

static u16 via_ac97_read_reg (struct ac97_codec *codec, u8 reg);
static void via_ac97_write_reg (struct ac97_codec *codec, u8 reg, u16 value);
static u8 via_ac97_wait_idle (struct via_info *card);

static void via_chan_free (struct via_info *card, struct via_channel *chan);
static void via_chan_clear (struct via_info *card, struct via_channel *chan);
static void via_chan_pcm_fmt (struct via_channel *chan, int reset);
static void via_chan_buffer_free (struct via_info *card, struct via_channel *chan);


/****************************************************************
 *
 * Various data the driver needs
 *
 *
 */


static struct pci_device_id via_pci_tbl[] = {
	{ PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_82C686_5,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_8233_5,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 1},
	{ 0, }
};
MODULE_DEVICE_TABLE(pci,via_pci_tbl);


static struct pci_driver via_driver = {
	.name		= VIA_MODULE_NAME,
	.id_table	= via_pci_tbl,
	.probe		= via_init_one,
	.remove		= __devexit_p(via_remove_one),
};


/****************************************************************
 *
 * Low-level base 0 register read/write helpers
 *
 *
 */

/**
 *	via_chan_stop - Terminate DMA on specified PCM channel
 *	@iobase: PCI base address for SGD channel registers
 *
 *	Terminate scatter-gather DMA operation for given
 *	channel (derived from @iobase), if DMA is active.
 *
 *	Note that @iobase is not the PCI base address,
 *	but the PCI base address plus an offset to
 *	one of three PCM channels supported by the chip.
 *
 */

static inline void via_chan_stop (long iobase)
{
	if (inb (iobase + VIA_PCM_STATUS) & VIA_SGD_ACTIVE)
		outb (VIA_SGD_TERMINATE, iobase + VIA_PCM_CONTROL);
}


/**
 *	via_chan_status_clear - Clear status flags on specified DMA channel
 *	@iobase: PCI base address for SGD channel registers
 *
 *	Clear any pending status flags for the given
 *	DMA channel (derived from @iobase), if any
 *	flags are asserted.
 *
 *	Note that @iobase is not the PCI base address,
 *	but the PCI base address plus an offset to
 *	one of three PCM channels supported by the chip.
 *
 */

static inline void via_chan_status_clear (long iobase)
{
	u8 tmp = inb (iobase + VIA_PCM_STATUS);

	if (tmp != 0)
		outb (tmp, iobase + VIA_PCM_STATUS);
}


/**
 *	sg_begin - Begin recording or playback on a PCM channel
 *	@chan: Channel for which DMA operation shall begin
 *
 *	Start scatter-gather DMA for the given channel.
 *
 */

static inline void sg_begin (struct via_channel *chan)
{
	DPRINTK("Start with intmask %d\n", chan->intmask);
	DPRINTK("About to start from %d to %d\n", 
		inl(chan->iobase + VIA_PCM_BLOCK_COUNT),
		inb(chan->iobase + VIA_PCM_STOPRATE + 3));
	outb (VIA_SGD_START|chan->intmask, chan->iobase + VIA_PCM_CONTROL);
	DPRINTK("Status is now %02X\n", inb(chan->iobase + VIA_PCM_STATUS));
	DPRINTK("Control is now %02X\n", inb(chan->iobase + VIA_PCM_CONTROL));
}


static int sg_active (long iobase)
{
	u8 tmp = inb (iobase + VIA_PCM_STATUS);
	if ((tmp & VIA_SGD_STOPPED) || (tmp & VIA_SGD_PAUSED)) {
		printk(KERN_WARNING "via82cxxx warning: SG stopped or paused\n");
		return 0;
	}
	if (tmp & VIA_SGD_ACTIVE)
		return 1;
	return 0;
}

static int via_sg_offset(struct via_channel *chan)
{
	return inl (chan->iobase + VIA_PCM_BLOCK_COUNT) & 0x00FFFFFF;
}

/****************************************************************
 *
 * Miscellaneous debris
 *
 *
 */


/**
 *	via_syscall_down - down the card-specific syscell semaphore
 *	@card: Private info for specified board
 *	@nonblock: boolean, non-zero if O_NONBLOCK is set
 *
 *	Encapsulates standard method of acquiring the syscall sem.
 *
 *	Returns negative errno on error, or zero for success.
 */

static inline int via_syscall_down (struct via_info *card, int nonblock)
{
	/* Thomas Sailer:
	 * EAGAIN is supposed to be used if IO is pending,
	 * not if there is contention on some internal
	 * synchronization primitive which should be
	 * held only for a short time anyway
	 */
	nonblock = 0;

	if (nonblock) {
		if (!mutex_trylock(&card->syscall_mutex))
			return -EAGAIN;
	} else {
		if (mutex_lock_interruptible(&card->syscall_mutex))
			return -ERESTARTSYS;
	}

	return 0;
}


/**
 *	via_stop_everything - Stop all audio operations
 *	@card: Private info for specified board
 *
 *	Stops all DMA operations and interrupts, and clear
 *	any pending status bits resulting from those operations.
 */

static void via_stop_everything (struct via_info *card)
{
	u8 tmp, new_tmp;

	DPRINTK ("ENTER\n");

	assert (card != NULL);

	/*
	 * terminate any existing operations on audio read/write channels
	 */
	via_chan_stop (card->baseaddr + VIA_BASE0_PCM_OUT_CHAN);
	via_chan_stop (card->baseaddr + VIA_BASE0_PCM_IN_CHAN);
	via_chan_stop (card->baseaddr + VIA_BASE0_FM_OUT_CHAN);
	if(card->sixchannel)
		via_chan_stop (card->baseaddr + VIA_BASE0_MULTI_OUT_CHAN);

	/*
	 * clear any existing stops / flags (sanity check mainly)
	 */
	via_chan_status_clear (card->baseaddr + VIA_BASE0_PCM_OUT_CHAN);
	via_chan_status_clear (card->baseaddr + VIA_BASE0_PCM_IN_CHAN);
	via_chan_status_clear (card->baseaddr + VIA_BASE0_FM_OUT_CHAN);
	if(card->sixchannel)
		via_chan_status_clear (card->baseaddr + VIA_BASE0_MULTI_OUT_CHAN);

	/*
	 * clear any enabled interrupt bits
	 */
	tmp = inb (card->baseaddr + VIA_BASE0_PCM_OUT_CHAN_TYPE);
	new_tmp = tmp & ~(VIA_IRQ_ON_FLAG|VIA_IRQ_ON_EOL|VIA_RESTART_SGD_ON_EOL);
	if (tmp != new_tmp)
		outb (0, card->baseaddr + VIA_BASE0_PCM_OUT_CHAN_TYPE);

	tmp = inb (card->baseaddr + VIA_BASE0_PCM_IN_CHAN_TYPE);
	new_tmp = tmp & ~(VIA_IRQ_ON_FLAG|VIA_IRQ_ON_EOL|VIA_RESTART_SGD_ON_EOL);
	if (tmp != new_tmp)
		outb (0, card->baseaddr + VIA_BASE0_PCM_IN_CHAN_TYPE);

	tmp = inb (card->baseaddr + VIA_BASE0_FM_OUT_CHAN_TYPE);
	new_tmp = tmp & ~(VIA_IRQ_ON_FLAG|VIA_IRQ_ON_EOL|VIA_RESTART_SGD_ON_EOL);
	if (tmp != new_tmp)
		outb (0, card->baseaddr + VIA_BASE0_FM_OUT_CHAN_TYPE);

	if(card->sixchannel)
	{
		tmp = inb (card->baseaddr + VIA_BASE0_MULTI_OUT_CHAN_TYPE);
		new_tmp = tmp & ~(VIA_IRQ_ON_FLAG|VIA_IRQ_ON_EOL|VIA_RESTART_SGD_ON_EOL);
		if (tmp != new_tmp)
			outb (0, card->baseaddr + VIA_BASE0_MULTI_OUT_CHAN_TYPE);
	}

	udelay(10);

	/*
	 * clear any existing flags
	 */
	via_chan_status_clear (card->baseaddr + VIA_BASE0_PCM_OUT_CHAN);
	via_chan_status_clear (card->baseaddr + VIA_BASE0_PCM_IN_CHAN);
	via_chan_status_clear (card->baseaddr + VIA_BASE0_FM_OUT_CHAN);

	DPRINTK ("EXIT\n");
}


/**
 *	via_set_rate - Set PCM rate for given channel
 *	@ac97: Pointer to generic codec info struct
 *	@chan: Private info for specified channel
 *	@rate: Desired PCM sample rate, in Khz
 *
 *	Sets the PCM sample rate for a channel.
 *
 *	Values for @rate are clamped to a range of 4000 Khz through 48000 Khz,
 *	due to hardware constraints.
 */

static int via_set_rate (struct ac97_codec *ac97,
			 struct via_channel *chan, unsigned rate)
{
	struct via_info *card = ac97->private_data;
	int rate_reg;
	u32 dacp;
	u32 mast_vol, phone_vol, mono_vol, pcm_vol;
	u32 mute_vol = 0x8000;	/* The mute volume? -- Seems to work! */

	DPRINTK ("ENTER, rate = %d\n", rate);

	if (chan->rate == rate)
		goto out;
	if (card->locked_rate) {
		chan->rate = 48000;
		goto out;
	}

	if (rate > 48000)		rate = 48000;
	if (rate < 4000) 		rate = 4000;

	rate_reg = chan->is_record ? AC97_PCM_LR_ADC_RATE :
			    AC97_PCM_FRONT_DAC_RATE;

	/* Save current state */
	dacp=via_ac97_read_reg(ac97, AC97_POWER_CONTROL);
	mast_vol = via_ac97_read_reg(ac97, AC97_MASTER_VOL_STEREO);
	mono_vol = via_ac97_read_reg(ac97, AC97_MASTER_VOL_MONO);
	phone_vol = via_ac97_read_reg(ac97, AC97_HEADPHONE_VOL);
	pcm_vol = via_ac97_read_reg(ac97, AC97_PCMOUT_VOL);
	/* Mute - largely reduces popping */
	via_ac97_write_reg(ac97, AC97_MASTER_VOL_STEREO, mute_vol);
	via_ac97_write_reg(ac97, AC97_MASTER_VOL_MONO, mute_vol);
	via_ac97_write_reg(ac97, AC97_HEADPHONE_VOL, mute_vol);
       	via_ac97_write_reg(ac97, AC97_PCMOUT_VOL, mute_vol);
	/* Power down the DAC */
	via_ac97_write_reg(ac97, AC97_POWER_CONTROL, dacp|0x0200);

        /* Set new rate */
	via_ac97_write_reg (ac97, rate_reg, rate);

	/* Power DAC back up */
	via_ac97_write_reg(ac97, AC97_POWER_CONTROL, dacp);
	udelay (200); /* reduces popping */

	/* Restore volumes */
	via_ac97_write_reg(ac97, AC97_MASTER_VOL_STEREO, mast_vol);
	via_ac97_write_reg(ac97, AC97_MASTER_VOL_MONO, mono_vol);
	via_ac97_write_reg(ac97, AC97_HEADPHONE_VOL, phone_vol);
	via_ac97_write_reg(ac97, AC97_PCMOUT_VOL, pcm_vol);

	/* the hardware might return a value different than what we
	 * passed to it, so read the rate value back from hardware
	 * to see what we came up with
	 */
	chan->rate = via_ac97_read_reg (ac97, rate_reg);

	if (chan->rate == 0) {
		card->locked_rate = 1;
		chan->rate = 48000;
		printk (KERN_WARNING PFX "Codec rate locked at 48Khz\n");
	}

out:
	DPRINTK ("EXIT, returning rate %d Hz\n", chan->rate);
	return chan->rate;
}


/****************************************************************
 *
 * Channel-specific operations
 *
 *
 */


/**
 *	via_chan_init_defaults - Initialize a struct via_channel
 *	@card: Private audio chip info
 *	@chan: Channel to be initialized
 *
 *	Zero @chan, and then set all static defaults for the structure.
 */

static void via_chan_init_defaults (struct via_info *card, struct via_channel *chan)
{
	memset (chan, 0, sizeof (*chan));

	if(card->intmask)
		chan->intmask = 0x23;	/* Turn on the IRQ bits */
		
	if (chan == &card->ch_out) {
		chan->name = "PCM-OUT";
		if(card->sixchannel)
		{
			chan->iobase = card->baseaddr + VIA_BASE0_MULTI_OUT_CHAN;
			chan->is_multi = 1;
			DPRINTK("Using multichannel for pcm out\n");
		}
		else
			chan->iobase = card->baseaddr + VIA_BASE0_PCM_OUT_CHAN;
	} else if (chan == &card->ch_in) {
		chan->name = "PCM-IN";
		chan->iobase = card->baseaddr + VIA_BASE0_PCM_IN_CHAN;
		chan->is_record = 1;
	} else if (chan == &card->ch_fm) {
		chan->name = "PCM-OUT-FM";
		chan->iobase = card->baseaddr + VIA_BASE0_FM_OUT_CHAN;
	} else {
		BUG();
	}

	init_waitqueue_head (&chan->wait);

	chan->pcm_fmt = VIA_PCM_FMT_MASK;
	chan->is_enabled = 1;

	chan->frag_number = 0;
        chan->frag_size = 0;
	atomic_set(&chan->n_frags, 0);
	atomic_set (&chan->hw_ptr, 0);
}

/**
 *      via_chan_init - Initialize PCM channel
 *      @card: Private audio chip info
 *      @chan: Channel to be initialized
 *
 *      Performs some of the preparations necessary to begin
 *      using a PCM channel.
 *
 *      Currently the preparations consist of
 *      setting the PCM channel to a known state.
 */


static void via_chan_init (struct via_info *card, struct via_channel *chan)
{

        DPRINTK ("ENTER\n");

	/* bzero channel structure, and init members to defaults */
        via_chan_init_defaults (card, chan);

        /* stop any existing channel output */
        via_chan_clear (card, chan);
        via_chan_status_clear (chan->iobase);
        via_chan_pcm_fmt (chan, 1);

	DPRINTK ("EXIT\n");
}

/**
 *	via_chan_buffer_init - Initialize PCM channel buffer
 *	@card: Private audio chip info
 *	@chan: Channel to be initialized
 *
 *	Performs some of the preparations necessary to begin
 *	using a PCM channel.
 *
 *	Currently the preparations include allocating the
 *	scatter-gather DMA table and buffers,
 *	and passing the
 *	address of the DMA table to the hardware.
 *
 *	Note that special care is taken when passing the
 *	DMA table address to hardware, because it was found
 *	during driver development that the hardware did not
 *	always "take" the address.
 */

static int via_chan_buffer_init (struct via_info *card, struct via_channel *chan)
{
	int page, offset;
	int i;

	DPRINTK ("ENTER\n");


	chan->intmask = 0;
	if(card->intmask)
		chan->intmask = 0x23;	/* Turn on the IRQ bits */
		
	if (chan->sgtable != NULL) {
		DPRINTK ("EXIT\n");
		return 0;
	}

	/* alloc DMA-able memory for scatter-gather table */
	chan->sgtable = pci_alloc_consistent (card->pdev,
		(sizeof (struct via_sgd_table) * chan->frag_number),
		&chan->sgt_handle);
	if (!chan->sgtable) {
		printk (KERN_ERR PFX "DMA table alloc fail, aborting\n");
		DPRINTK ("EXIT\n");
		return -ENOMEM;
	}

	memset ((void*)chan->sgtable, 0,
		(sizeof (struct via_sgd_table) * chan->frag_number));

	/* alloc DMA-able memory for scatter-gather buffers */

	chan->page_number = (chan->frag_number * chan->frag_size) / PAGE_SIZE +
			    (((chan->frag_number * chan->frag_size) % PAGE_SIZE) ? 1 : 0);

	for (i = 0; i < chan->page_number; i++) {
		chan->pgtbl[i].cpuaddr = pci_alloc_consistent (card->pdev, PAGE_SIZE,
					      &chan->pgtbl[i].handle);

		if (!chan->pgtbl[i].cpuaddr) {
			chan->page_number = i;
			goto err_out_nomem;
		}

#ifndef VIA_NDEBUG
                memset (chan->pgtbl[i].cpuaddr, 0xBC, chan->frag_size);
#endif

#if 1
                DPRINTK ("dmabuf_pg #%d (h=%lx, v2p=%lx, a=%p)\n",
			i, (long)chan->pgtbl[i].handle,
			virt_to_phys(chan->pgtbl[i].cpuaddr),
			chan->pgtbl[i].cpuaddr);
#endif
	}

	for (i = 0; i < chan->frag_number; i++) {

		page = i / (PAGE_SIZE / chan->frag_size);
		offset = (i % (PAGE_SIZE / chan->frag_size)) * chan->frag_size;

		chan->sgtable[i].count = cpu_to_le32 (chan->frag_size | VIA_FLAG);
		chan->sgtable[i].addr = cpu_to_le32 (chan->pgtbl[page].handle + offset);

#if 1
		DPRINTK ("dmabuf #%d (32(h)=%lx)\n",
			 i,
			 (long)chan->sgtable[i].addr);
#endif
	}

	/* overwrite the last buffer information */
	chan->sgtable[chan->frag_number - 1].count = cpu_to_le32 (chan->frag_size | VIA_EOL);

	/* set location of DMA-able scatter-gather info table */
	DPRINTK ("outl (0x%X, 0x%04lX)\n",
		chan->sgt_handle, chan->iobase + VIA_PCM_TABLE_ADDR);

	via_ac97_wait_idle (card);
	outl (chan->sgt_handle, chan->iobase + VIA_PCM_TABLE_ADDR);
	udelay (20);
	via_ac97_wait_idle (card);
	/* load no rate adaption, stereo 16bit, set up ring slots */
	if(card->sixchannel)
	{
		if(!chan->is_multi)
		{
			outl (0xFFFFF | (0x3 << 20) | (chan->frag_number << 24), chan->iobase + VIA_PCM_STOPRATE);
			udelay (20);
			via_ac97_wait_idle (card);
		}
	}

	DPRINTK ("inl (0x%lX) = %x\n",
		chan->iobase + VIA_PCM_TABLE_ADDR,
		inl(chan->iobase + VIA_PCM_TABLE_ADDR));

	DPRINTK ("EXIT\n");
	return 0;

err_out_nomem:
	printk (KERN_ERR PFX "DMA buffer alloc fail, aborting\n");
	via_chan_buffer_free (card, chan);
	DPRINTK ("EXIT\n");
	return -ENOMEM;
}


/**
 *	via_chan_free - Release a PCM channel
 *	@card: Private audio chip info
 *	@chan: Channel to be released
 *
 *	Performs all the functions necessary to clean up
 *	an initialized channel.
 *
 *	Currently these functions include disabled any
 *	active DMA operations, setting the PCM channel
 *	back to a known state, and releasing any allocated
 *	sound buffers.
 */

static void via_chan_free (struct via_info *card, struct via_channel *chan)
{
	DPRINTK ("ENTER\n");

	spin_lock_irq (&card->lock);

	/* stop any existing channel output */
	via_chan_status_clear (chan->iobase);
	via_chan_stop (chan->iobase);
	via_chan_status_clear (chan->iobase);

	spin_unlock_irq (&card->lock);

	synchronize_irq(card->pdev->irq);

	DPRINTK ("EXIT\n");
}

static void via_chan_buffer_free (struct via_info *card, struct via_channel *chan)
{
	int i;

        DPRINTK ("ENTER\n");

	/* zero location of DMA-able scatter-gather info table */
	via_ac97_wait_idle(card);
	outl (0, chan->iobase + VIA_PCM_TABLE_ADDR);

	for (i = 0; i < chan->page_number; i++)
		if (chan->pgtbl[i].cpuaddr) {
			pci_free_consistent (card->pdev, PAGE_SIZE,
					     chan->pgtbl[i].cpuaddr,
					     chan->pgtbl[i].handle);
			chan->pgtbl[i].cpuaddr = NULL;
			chan->pgtbl[i].handle = 0;
		}

	chan->page_number = 0;

	if (chan->sgtable) {
		pci_free_consistent (card->pdev,
			(sizeof (struct via_sgd_table) * chan->frag_number),
			(void*)chan->sgtable, chan->sgt_handle);
		chan->sgtable = NULL;
	}

	DPRINTK ("EXIT\n");
}


/**
 *	via_chan_pcm_fmt - Update PCM channel settings
 *	@chan: Channel to be updated
 *	@reset: Boolean.  If non-zero, channel will be reset
 *		to 8-bit mono mode.
 *
 *	Stores the settings of the current PCM format,
 *	8-bit or 16-bit, and mono/stereo, into the
 *	hardware settings for the specified channel.
 *	If @reset is non-zero, the channel is reset
 *	to 8-bit mono mode.  Otherwise, the channel
 *	is set to the values stored in the channel
 *	information struct @chan.
 */

static void via_chan_pcm_fmt (struct via_channel *chan, int reset)
{
	DPRINTK ("ENTER, pcm_fmt=0x%02X, reset=%s\n",
		 chan->pcm_fmt, reset ? "yes" : "no");

	assert (chan != NULL);

	if (reset)
	{
		/* reset to 8-bit mono mode */
		chan->pcm_fmt = 0;
		chan->channels = 1;
	}

	/* enable interrupts on FLAG and EOL */
	chan->pcm_fmt |= VIA_CHAN_TYPE_MASK;

	/* if we are recording, enable recording fifo bit */
	if (chan->is_record)
		chan->pcm_fmt |= VIA_PCM_REC_FIFO;
	/* set interrupt select bits where applicable (PCM in & out channels) */
	if (!chan->is_record)
		chan->pcm_fmt |= VIA_CHAN_TYPE_INT_SELECT;
	
	DPRINTK("SET FMT - %02x %02x\n", chan->intmask , chan->is_multi);
	
	if(chan->intmask)
	{
		u32 m;

		/*
		 *	Channel 0x4 is up to 6 x 16bit and has to be
		 *	programmed differently 
		 */
		 		
		if(chan->is_multi)
		{
			u8 c = 0;
			
			/*
			 *	Load the type bit for num channels
			 *	and 8/16bit
			 */
			 
			if(chan->pcm_fmt & VIA_PCM_FMT_16BIT)
				c = 1 << 7;
			if(chan->pcm_fmt & VIA_PCM_FMT_STEREO)
				c |= (2<<4);
			else
				c |= (1<<4);
				
			outb(c, chan->iobase + VIA_PCM_TYPE);
			
			/*
			 *	Set the channel steering
			 *	Mono
			 *		Channel 0 to slot 3
			 *		Channel 0 to slot 4
			 *	Stereo
			 *		Channel 0 to slot 3
			 *		Channel 1 to slot 4
			 */
			 
			switch(chan->channels)
			{
				case 1:
					outl(0xFF000000 | (1<<0) | (1<<4) , chan->iobase + VIA_PCM_STOPRATE);
					break;
				case 2:
					outl(0xFF000000 | (1<<0) | (2<<4) , chan->iobase + VIA_PCM_STOPRATE);
					break;
				case 4:
					outl(0xFF000000 | (1<<0) | (2<<4) | (3<<8) | (4<<12), chan->iobase + VIA_PCM_STOPRATE);
					break;
				case 6:
					outl(0xFF000000 | (1<<0) | (2<<4) | (5<<8) | (6<<12) | (3<<16) | (4<<20), chan->iobase + VIA_PCM_STOPRATE);
					break;
			}				
		}
		else
		{
			/*
			 *	New style, turn off channel volume
			 *	control, set bits in the right register
			 */	
			outb(0x0, chan->iobase + VIA_PCM_LEFTVOL);
			outb(0x0, chan->iobase + VIA_PCM_RIGHTVOL);

			m = inl(chan->iobase + VIA_PCM_STOPRATE);
			m &= ~(3<<20);
			if(chan->pcm_fmt & VIA_PCM_FMT_STEREO)
				m |= (1 << 20);
			if(chan->pcm_fmt & VIA_PCM_FMT_16BIT)
				m |= (1 << 21);
			outl(m, chan->iobase + VIA_PCM_STOPRATE);
		}		
	}
	else
		outb (chan->pcm_fmt, chan->iobase + VIA_PCM_TYPE);


	DPRINTK ("EXIT, pcm_fmt = 0x%02X, reg = 0x%02X\n",
		 chan->pcm_fmt,
		 inb (chan->iobase + VIA_PCM_TYPE));
}


/**
 *	via_chan_clear - Stop DMA channel operation, and reset pointers
 *	@card: the chip to accessed
 *	@chan: Channel to be cleared
 *
 *	Call via_chan_stop to halt DMA operations, and then resets
 *	all software pointers which track DMA operation.
 */

static void via_chan_clear (struct via_info *card, struct via_channel *chan)
{
	DPRINTK ("ENTER\n");
	via_chan_stop (chan->iobase);
	via_chan_buffer_free(card, chan);
	chan->is_active = 0;
	chan->is_mapped = 0;
	chan->is_enabled = 1;
	chan->slop_len = 0;
	chan->sw_ptr = 0;
	chan->n_irqs = 0;
	atomic_set (&chan->hw_ptr, 0);
	DPRINTK ("EXIT\n");
}


/**
 *	via_chan_set_speed - Set PCM sample rate for given channel
 *	@card: Private info for specified board
 *	@chan: Channel whose sample rate will be adjusted
 *	@val: New sample rate, in Khz
 *
 *	Helper function for the %SNDCTL_DSP_SPEED ioctl.  OSS semantics
 *	demand that all audio operations halt (if they are not already
 *	halted) when the %SNDCTL_DSP_SPEED is given.
 *
 *	This function halts all audio operations for the given channel
 *	@chan, and then calls via_set_rate to set the audio hardware
 *	to the new rate.
 */

static int via_chan_set_speed (struct via_info *card,
			       struct via_channel *chan, int val)
{
	DPRINTK ("ENTER, requested rate = %d\n", val);

	via_chan_clear (card, chan);

	val = via_set_rate (card->ac97, chan, val);

	DPRINTK ("EXIT, returning %d\n", val);
	return val;
}


/**
 *	via_chan_set_fmt - Set PCM sample size for given channel
 *	@card: Private info for specified board
 *	@chan: Channel whose sample size will be adjusted
 *	@val: New sample size, use the %AFMT_xxx constants
 *
 *	Helper function for the %SNDCTL_DSP_SETFMT ioctl.  OSS semantics
 *	demand that all audio operations halt (if they are not already
 *	halted) when the %SNDCTL_DSP_SETFMT is given.
 *
 *	This function halts all audio operations for the given channel
 *	@chan, and then calls via_chan_pcm_fmt to set the audio hardware
 *	to the new sample size, either 8-bit or 16-bit.
 */

static int via_chan_set_fmt (struct via_info *card,
			     struct via_channel *chan, int val)
{
	DPRINTK ("ENTER, val=%s\n",
		 val == AFMT_U8 ? "AFMT_U8" :
	 	 val == AFMT_S16_LE ? "AFMT_S16_LE" :
		 "unknown");

	via_chan_clear (card, chan);

	assert (val != AFMT_QUERY); /* this case is handled elsewhere */

	switch (val) {
	case AFMT_S16_LE:
		if ((chan->pcm_fmt & VIA_PCM_FMT_16BIT) == 0) {
			chan->pcm_fmt |= VIA_PCM_FMT_16BIT;
			via_chan_pcm_fmt (chan, 0);
		}
		break;

	case AFMT_U8:
		if (chan->pcm_fmt & VIA_PCM_FMT_16BIT) {
			chan->pcm_fmt &= ~VIA_PCM_FMT_16BIT;
			via_chan_pcm_fmt (chan, 0);
		}
		break;

	default:
		DPRINTK ("unknown AFMT: 0x%X\n", val);
		val = AFMT_S16_LE;
	}

	DPRINTK ("EXIT\n");
	return val;
}


/**
 *	via_chan_set_stereo - Enable or disable stereo for a DMA channel
 *	@card: Private info for specified board
 *	@chan: Channel whose stereo setting will be adjusted
 *	@val: New sample size, use the %AFMT_xxx constants
 *
 *	Helper function for the %SNDCTL_DSP_CHANNELS and %SNDCTL_DSP_STEREO ioctls.  OSS semantics
 *	demand that all audio operations halt (if they are not already
 *	halted) when %SNDCTL_DSP_CHANNELS or SNDCTL_DSP_STEREO is given.
 *
 *	This function halts all audio operations for the given channel
 *	@chan, and then calls via_chan_pcm_fmt to set the audio hardware
 *	to enable or disable stereo.
 */

static int via_chan_set_stereo (struct via_info *card,
			        struct via_channel *chan, int val)
{
	DPRINTK ("ENTER, channels = %d\n", val);

	via_chan_clear (card, chan);

	switch (val) {

	/* mono */
	case 1:
		chan->pcm_fmt &= ~VIA_PCM_FMT_STEREO;
		chan->channels = 1;
		via_chan_pcm_fmt (chan, 0);
		break;

	/* stereo */
	case 2:
		chan->pcm_fmt |= VIA_PCM_FMT_STEREO;
		chan->channels = 2;
		via_chan_pcm_fmt (chan, 0);
		break;

	case 4:
	case 6:
		if(chan->is_multi)
		{
			chan->pcm_fmt |= VIA_PCM_FMT_STEREO;
			chan->channels = val;
			break;
		}
	/* unknown */
	default:
		val = -EINVAL;
		break;
	}

	DPRINTK ("EXIT, returning %d\n", val);
	return val;
}

static int via_chan_set_buffering (struct via_info *card,
                                struct via_channel *chan, int val)
{
	int shift;

        DPRINTK ("ENTER\n");

	/* in both cases the buffer cannot be changed */
	if (chan->is_active || chan->is_mapped) {
		DPRINTK ("EXIT\n");
		return -EINVAL;
	}

	/* called outside SETFRAGMENT */
	/* set defaults or do nothing */
	if (val < 0) {

		if (chan->frag_size && chan->frag_number)
			goto out;

		DPRINTK ("\n");

		chan->frag_size = (VIA_DEFAULT_FRAG_TIME * chan->rate * chan->channels
				   * ((chan->pcm_fmt & VIA_PCM_FMT_16BIT) ? 2 : 1)) / 1000 - 1;

		shift = 0;
		while (chan->frag_size) {
			chan->frag_size >>= 1;
			shift++;
		}
		chan->frag_size = 1 << shift;

		chan->frag_number = (VIA_DEFAULT_BUFFER_TIME / VIA_DEFAULT_FRAG_TIME);

		DPRINTK ("setting default values %d %d\n", chan->frag_size, chan->frag_number);
	} else {
		chan->frag_size = 1 << (val & 0xFFFF);
		chan->frag_number = (val >> 16) & 0xFFFF;

		DPRINTK ("using user values %d %d\n", chan->frag_size, chan->frag_number);
	}

	/* quake3 wants frag_number to be a power of two */
	shift = 0;
	while (chan->frag_number) {
		chan->frag_number >>= 1;
		shift++;
	}
	chan->frag_number = 1 << shift;

	if (chan->frag_size > VIA_MAX_FRAG_SIZE)
		chan->frag_size = VIA_MAX_FRAG_SIZE;
	else if (chan->frag_size < VIA_MIN_FRAG_SIZE)
		chan->frag_size = VIA_MIN_FRAG_SIZE;

	if (chan->frag_number < VIA_MIN_FRAG_NUMBER)
                chan->frag_number = VIA_MIN_FRAG_NUMBER;
        if (chan->frag_number > VIA_MAX_FRAG_NUMBER)
        	chan->frag_number = VIA_MAX_FRAG_NUMBER;

	if ((chan->frag_number * chan->frag_size) / PAGE_SIZE > VIA_MAX_BUFFER_DMA_PAGES)
		chan->frag_number = (VIA_MAX_BUFFER_DMA_PAGES * PAGE_SIZE) / chan->frag_size;

out:
	if (chan->is_record)
		atomic_set (&chan->n_frags, 0);
	else
		atomic_set (&chan->n_frags, chan->frag_number);

	DPRINTK ("EXIT\n");

	return 0;
}

#ifdef VIA_CHAN_DUMP_BUFS
/**
 *	via_chan_dump_bufs - Display DMA table contents
 *	@chan: Channel whose DMA table will be displayed
 *
 *	Debugging function which displays the contents of the
 *	scatter-gather DMA table for the given channel @chan.
 */

static void via_chan_dump_bufs (struct via_channel *chan)
{
	int i;

	for (i = 0; i < chan->frag_number; i++) {
		DPRINTK ("#%02d: addr=%x, count=%u, flag=%d, eol=%d\n",
			 i, chan->sgtable[i].addr,
			 chan->sgtable[i].count & 0x00FFFFFF,
			 chan->sgtable[i].count & VIA_FLAG ? 1 : 0,
			 chan->sgtable[i].count & VIA_EOL ? 1 : 0);
	}
	DPRINTK ("buf_in_use = %d, nextbuf = %d\n",
		 atomic_read (&chan->buf_in_use),
		 atomic_read (&chan->sw_ptr));
}
#endif /* VIA_CHAN_DUMP_BUFS */


/**
 *	via_chan_flush_frag - Flush partially-full playback buffer to hardware
 *	@chan: Channel whose DMA table will be flushed
 *
 *	Flushes partially-full playback buffer to hardware.
 */

static void via_chan_flush_frag (struct via_channel *chan)
{
	DPRINTK ("ENTER\n");

	assert (chan->slop_len > 0);

	if (chan->sw_ptr == (chan->frag_number - 1))
		chan->sw_ptr = 0;
	else
		chan->sw_ptr++;

	chan->slop_len = 0;

	assert (atomic_read (&chan->n_frags) > 0);
	atomic_dec (&chan->n_frags);

	DPRINTK ("EXIT\n");
}



/**
 *	via_chan_maybe_start - Initiate audio hardware DMA operation
 *	@chan: Channel whose DMA is to be started
 *
 *	Initiate DMA operation, if the DMA engine for the given
 *	channel @chan is not already active.
 */

static inline void via_chan_maybe_start (struct via_channel *chan)
{
	assert (chan->is_active == sg_active(chan->iobase));

	DPRINTK ("MAYBE START %s\n", chan->name);
	if (!chan->is_active && chan->is_enabled) {
		chan->is_active = 1;
		sg_begin (chan);
		DPRINTK ("starting channel %s\n", chan->name);
	}
}


/****************************************************************
 *
 * Interface to ac97-codec module
 *
 *
 */

/**
 *	via_ac97_wait_idle - Wait until AC97 codec is not busy
 *	@card: Private info for specified board
 *
 *	Sleep until the AC97 codec is no longer busy.
 *	Returns the final value read from the SGD
 *	register being polled.
 */

static u8 via_ac97_wait_idle (struct via_info *card)
{
	u8 tmp8;
	int counter = VIA_COUNTER_LIMIT;

	DPRINTK ("ENTER/EXIT\n");

	assert (card != NULL);
	assert (card->pdev != NULL);

	do {
		udelay (15);

		tmp8 = inb (card->baseaddr + 0x83);
	} while ((tmp8 & VIA_CR83_BUSY) && (counter-- > 0));

	if (tmp8 & VIA_CR83_BUSY)
		printk (KERN_WARNING PFX "timeout waiting on AC97 codec\n");
	return tmp8;
}


/**
 *	via_ac97_read_reg - Read AC97 standard register
 *	@codec: Pointer to generic AC97 codec info
 *	@reg: Index of AC97 register to be read
 *
 *	Read the value of a single AC97 codec register,
 *	as defined by the Intel AC97 specification.
 *
 *	Defines the standard AC97 read-register operation
 *	required by the kernel's ac97_codec interface.
 *
 *	Returns the 16-bit value stored in the specified
 *	register.
 */

static u16 via_ac97_read_reg (struct ac97_codec *codec, u8 reg)
{
	unsigned long data;
	struct via_info *card;
	int counter;

	DPRINTK ("ENTER\n");

	assert (codec != NULL);
	assert (codec->private_data != NULL);

	card = codec->private_data;
	
	spin_lock(&card->ac97_lock);

	/* Every time we write to register 80 we cause a transaction.
	   The only safe way to clear the valid bit is to write it at
	   the same time as the command */
	data = (reg << 16) | VIA_CR80_READ | VIA_CR80_VALID;

	outl (data, card->baseaddr + VIA_BASE0_AC97_CTRL);
	udelay (20);

	for (counter = VIA_COUNTER_LIMIT; counter > 0; counter--) {
		udelay (1);
		if ((((data = inl(card->baseaddr + VIA_BASE0_AC97_CTRL)) &
		      (VIA_CR80_VALID|VIA_CR80_BUSY)) == VIA_CR80_VALID))
			goto out;
	}

	printk (KERN_WARNING PFX "timeout while reading AC97 codec (0x%lX)\n", data);
	goto err_out;

out:
	/* Once the valid bit has become set, we must wait a complete AC97
	   frame before the data has settled. */
	udelay(25);
	data = (unsigned long) inl (card->baseaddr + VIA_BASE0_AC97_CTRL);

	outb (0x02, card->baseaddr + 0x83);

	if (((data & 0x007F0000) >> 16) == reg) {
		DPRINTK ("EXIT, success, data=0x%lx, retval=0x%lx\n",
			 data, data & 0x0000FFFF);
		spin_unlock(&card->ac97_lock);
		return data & 0x0000FFFF;
	}

	printk (KERN_WARNING "via82cxxx_audio: not our index: reg=0x%x, newreg=0x%lx\n",
		reg, ((data & 0x007F0000) >> 16));

err_out:
	spin_unlock(&card->ac97_lock);
	DPRINTK ("EXIT, returning 0\n");
	return 0;
}


/**
 *	via_ac97_write_reg - Write AC97 standard register
 *	@codec: Pointer to generic AC97 codec info
 *	@reg: Index of AC97 register to be written
 *	@value: Value to be written to AC97 register
 *
 *	Write the value of a single AC97 codec register,
 *	as defined by the Intel AC97 specification.
 *
 *	Defines the standard AC97 write-register operation
 *	required by the kernel's ac97_codec interface.
 */

static void via_ac97_write_reg (struct ac97_codec *codec, u8 reg, u16 value)
{
	u32 data;
	struct via_info *card;
	int counter;

	DPRINTK ("ENTER\n");

	assert (codec != NULL);
	assert (codec->private_data != NULL);

	card = codec->private_data;

	spin_lock(&card->ac97_lock);
	
	data = (reg << 16) + value;
	outl (data, card->baseaddr + VIA_BASE0_AC97_CTRL);
	udelay (10);

	for (counter = VIA_COUNTER_LIMIT; counter > 0; counter--) {
		if ((inb (card->baseaddr + 0x83) & VIA_CR83_BUSY) == 0)
			goto out;

		udelay (15);
	}

	printk (KERN_WARNING PFX "timeout after AC97 codec write (0x%X, 0x%X)\n", reg, value);

out:
	spin_unlock(&card->ac97_lock);
	DPRINTK ("EXIT\n");
}


static int via_mixer_open (struct inode *inode, struct file *file)
{
	int minor = iminor(inode);
	struct via_info *card;
	struct pci_dev *pdev = NULL;
	struct pci_driver *drvr;

	DPRINTK ("ENTER\n");

	while ((pdev = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, pdev)) != NULL) {
		drvr = pci_dev_driver (pdev);
		if (drvr == &via_driver) {
			assert (pci_get_drvdata (pdev) != NULL);

			card = pci_get_drvdata (pdev);
			if (card->ac97->dev_mixer == minor)
				goto match;
		}
	}

	DPRINTK ("EXIT, returning -ENODEV\n");
	return -ENODEV;

match:
	pci_dev_put(pdev);
	file->private_data = card->ac97;

	DPRINTK ("EXIT, returning 0\n");
	return nonseekable_open(inode, file);
}

static int via_mixer_ioctl (struct inode *inode, struct file *file, unsigned int cmd,
			    unsigned long arg)
{
	struct ac97_codec *codec = file->private_data;
	struct via_info *card;
	int nonblock = (file->f_flags & O_NONBLOCK);
	int rc;

	DPRINTK ("ENTER\n");

	assert (codec != NULL);
	card = codec->private_data;
	assert (card != NULL);

	rc = via_syscall_down (card, nonblock);
	if (rc) goto out;
	
#if 0
	/*
	 *	Intercept volume control on 8233 and 8235
	 */
	if(card->volume)
	{
		switch(cmd)
		{
			case SOUND_MIXER_READ_VOLUME:
				return card->mixer_vol;
			case SOUND_MIXER_WRITE_VOLUME:
			{
				int v;
				if(get_user(v, (int *)arg))
				{
					rc = -EFAULT;
					goto out;
				}
				card->mixer_vol = v;
			}
		}
	}		
#endif
	rc = codec->mixer_ioctl(codec, cmd, arg);

	mutex_unlock(&card->syscall_mutex);

out:
	DPRINTK ("EXIT, returning %d\n", rc);
	return rc;
}


static const struct file_operations via_mixer_fops = {
	.owner		= THIS_MODULE,
	.open		= via_mixer_open,
	.llseek		= no_llseek,
	.ioctl		= via_mixer_ioctl,
};


static int __devinit via_ac97_reset (struct via_info *card)
{
	struct pci_dev *pdev = card->pdev;
	u8 tmp8;
	u16 tmp16;

	DPRINTK ("ENTER\n");

	assert (pdev != NULL);

#ifndef NDEBUG
	{
		u8 r40,r41,r42,r43,r44,r48;
		pci_read_config_byte (card->pdev, 0x40, &r40);
		pci_read_config_byte (card->pdev, 0x41, &r41);
		pci_read_config_byte (card->pdev, 0x42, &r42);
		pci_read_config_byte (card->pdev, 0x43, &r43);
		pci_read_config_byte (card->pdev, 0x44, &r44);
		pci_read_config_byte (card->pdev, 0x48, &r48);
		DPRINTK ("PCI config: %02X %02X %02X %02X %02X %02X\n",
			r40,r41,r42,r43,r44,r48);

		spin_lock_irq (&card->lock);
		DPRINTK ("regs==%02X %02X %02X %08X %08X %08X %08X\n",
			 inb (card->baseaddr + 0x00),
			 inb (card->baseaddr + 0x01),
			 inb (card->baseaddr + 0x02),
			 inl (card->baseaddr + 0x04),
			 inl (card->baseaddr + 0x0C),
			 inl (card->baseaddr + 0x80),
			 inl (card->baseaddr + 0x84));
		spin_unlock_irq (&card->lock);

	}
#endif

        /*
         * Reset AC97 controller: enable, disable, enable,
         * pausing after each command for good luck.  Only
	 * do this if the codec is not ready, because it causes
	 * loud pops and such due to such a hard codec reset.
         */
	pci_read_config_byte (pdev, VIA_ACLINK_STATUS, &tmp8);
	if ((tmp8 & VIA_CR40_AC97_READY) == 0) {
        	pci_write_config_byte (pdev, VIA_ACLINK_CTRL,
				       VIA_CR41_AC97_ENABLE |
                		       VIA_CR41_AC97_RESET |
				       VIA_CR41_AC97_WAKEUP);
        	udelay (100);

        	pci_write_config_byte (pdev, VIA_ACLINK_CTRL, 0);
        	udelay (100);

        	pci_write_config_byte (pdev, VIA_ACLINK_CTRL,
				       VIA_CR41_AC97_ENABLE |
				       VIA_CR41_PCM_ENABLE |
                		       VIA_CR41_VRA | VIA_CR41_AC97_RESET);
        	udelay (100);
	}

	/* Make sure VRA is enabled, in case we didn't do a
	 * complete codec reset, above
	 */
	pci_read_config_byte (pdev, VIA_ACLINK_CTRL, &tmp8);
	if (((tmp8 & VIA_CR41_VRA) == 0) ||
	    ((tmp8 & VIA_CR41_AC97_ENABLE) == 0) ||
	    ((tmp8 & VIA_CR41_PCM_ENABLE) == 0) ||
	    ((tmp8 & VIA_CR41_AC97_RESET) == 0)) {
        	pci_write_config_byte (pdev, VIA_ACLINK_CTRL,
				       VIA_CR41_AC97_ENABLE |
				       VIA_CR41_PCM_ENABLE |
                		       VIA_CR41_VRA | VIA_CR41_AC97_RESET);
        	udelay (100);
	}

	if(card->legacy)
	{
#if 0 /* this breaks on K7M */
		/* disable legacy stuff */
		pci_write_config_byte (pdev, 0x42, 0x00);
		udelay(10);
#endif

		/* route FM trap to IRQ, disable FM trap */
		pci_write_config_byte (pdev, 0x48, 0x05);
		udelay(10);
	}
	
	/* disable all codec GPI interrupts */
	outl (0, pci_resource_start (pdev, 0) + 0x8C);

	/* WARNING: this line is magic.  Remove this
	 * and things break. */
	/* enable variable rate */
 	tmp16 = via_ac97_read_reg (card->ac97, AC97_EXTENDED_STATUS);
 	if ((tmp16 & 1) == 0)
 		via_ac97_write_reg (card->ac97, AC97_EXTENDED_STATUS, tmp16 | 1);

	DPRINTK ("EXIT, returning 0\n");
	return 0;
}


static void via_ac97_codec_wait (struct ac97_codec *codec)
{
	assert (codec->private_data != NULL);
	via_ac97_wait_idle (codec->private_data);
}


static int __devinit via_ac97_init (struct via_info *card)
{
	int rc;
	u16 tmp16;

	DPRINTK ("ENTER\n");

	assert (card != NULL);

	card->ac97 = ac97_alloc_codec();
	if(card->ac97 == NULL)
		return -ENOMEM;
		
	card->ac97->private_data = card;
	card->ac97->codec_read = via_ac97_read_reg;
	card->ac97->codec_write = via_ac97_write_reg;
	card->ac97->codec_wait = via_ac97_codec_wait;

	card->ac97->dev_mixer = register_sound_mixer (&via_mixer_fops, -1);
	if (card->ac97->dev_mixer < 0) {
		printk (KERN_ERR PFX "unable to register AC97 mixer, aborting\n");
		DPRINTK ("EXIT, returning -EIO\n");
		ac97_release_codec(card->ac97);
		return -EIO;
	}

	rc = via_ac97_reset (card);
	if (rc) {
		printk (KERN_ERR PFX "unable to reset AC97 codec, aborting\n");
		goto err_out;
	}
	
	mdelay(10);
	
	if (ac97_probe_codec (card->ac97) == 0) {
		printk (KERN_ERR PFX "unable to probe AC97 codec, aborting\n");
		rc = -EIO;
		goto err_out;
	}

	/* enable variable rate */
	tmp16 = via_ac97_read_reg (card->ac97, AC97_EXTENDED_STATUS);
	via_ac97_write_reg (card->ac97, AC97_EXTENDED_STATUS, tmp16 | 1);

 	/*
 	 * If we cannot enable VRA, we have a locked-rate codec.
 	 * We try again to enable VRA before assuming so, however.
 	 */
 	tmp16 = via_ac97_read_reg (card->ac97, AC97_EXTENDED_STATUS);
 	if ((tmp16 & 1) == 0) {
 		via_ac97_write_reg (card->ac97, AC97_EXTENDED_STATUS, tmp16 | 1);
 		tmp16 = via_ac97_read_reg (card->ac97, AC97_EXTENDED_STATUS);
 		if ((tmp16 & 1) == 0) {
 			card->locked_rate = 1;
 			printk (KERN_WARNING PFX "Codec rate locked at 48Khz\n");
 		}
 	}

	DPRINTK ("EXIT, returning 0\n");
	return 0;

err_out:
	unregister_sound_mixer (card->ac97->dev_mixer);
	DPRINTK ("EXIT, returning %d\n", rc);
	ac97_release_codec(card->ac97);
	return rc;
}


static void via_ac97_cleanup (struct via_info *card)
{
	DPRINTK ("ENTER\n");

	assert (card != NULL);
	assert (card->ac97->dev_mixer >= 0);

	unregister_sound_mixer (card->ac97->dev_mixer);
	ac97_release_codec(card->ac97);

	DPRINTK ("EXIT\n");
}



/****************************************************************
 *
 * Interrupt-related code
 *
 */

/**
 *	via_intr_channel - handle an interrupt for a single channel
 *      @card: unused
 *	@chan: handle interrupt for this channel
 *
 *	This is the "meat" of the interrupt handler,
 *	containing the actions taken each time an interrupt
 *	occurs.  All communication and coordination with
 *	userspace takes place here.
 *
 *	Locking: inside card->lock
 */

static void via_intr_channel (struct via_info *card, struct via_channel *chan)
{
	u8 status;
	int n;
	
	/* check pertinent bits of status register for action bits */
	status = inb (chan->iobase) & (VIA_SGD_FLAG | VIA_SGD_EOL | VIA_SGD_STOPPED);
	if (!status)
		return;

	/* acknowledge any flagged bits ASAP */
	outb (status, chan->iobase);

	if (!chan->sgtable) /* XXX: temporary solution */
		return;

	/* grab current h/w ptr value */
	n = atomic_read (&chan->hw_ptr);

	/* sanity check: make sure our h/w ptr doesn't have a weird value */
	assert (n >= 0);
	assert (n < chan->frag_number);

	
	/* reset SGD data structure in memory to reflect a full buffer,
	 * and advance the h/w ptr, wrapping around to zero if needed
	 */
	if (n == (chan->frag_number - 1)) {
		chan->sgtable[n].count = cpu_to_le32(chan->frag_size | VIA_EOL);
		atomic_set (&chan->hw_ptr, 0);
	} else {
		chan->sgtable[n].count = cpu_to_le32(chan->frag_size | VIA_FLAG);
		atomic_inc (&chan->hw_ptr);
	}

	/* accounting crap for SNDCTL_DSP_GETxPTR */
	chan->n_irqs++;
	chan->bytes += chan->frag_size;
	/* FIXME - signed overflow is undefined */
	if (chan->bytes < 0) /* handle overflow of 31-bit value */
		chan->bytes = chan->frag_size;
	/* all following checks only occur when not in mmap(2) mode */
	if (!chan->is_mapped)
	{
		/* If we are recording, then n_frags represents the number
		 * of fragments waiting to be handled by userspace.
		 * If we are playback, then n_frags represents the number
		 * of fragments remaining to be filled by userspace.
		 * We increment here.  If we reach max number of fragments,
		 * this indicates an underrun/overrun.  For this case under OSS,
		 * we stop the record/playback process.
		 */
		if (atomic_read (&chan->n_frags) < chan->frag_number)
			atomic_inc (&chan->n_frags);
		assert (atomic_read (&chan->n_frags) <= chan->frag_number);
		if (atomic_read (&chan->n_frags) == chan->frag_number) {
			chan->is_active = 0;
			via_chan_stop (chan->iobase);
		}
	}
	/* wake up anyone listening to see when interrupts occur */
	wake_up_all (&chan->wait);

	DPRINTK ("%s intr, status=0x%02X, hwptr=0x%lX, chan->hw_ptr=%d\n",
		 chan->name, status, (long) inl (chan->iobase + 0x04),
		 atomic_read (&chan->hw_ptr));

	DPRINTK ("%s intr, channel n_frags == %d, missed %d\n", chan->name,
		 atomic_read (&chan->n_frags), missed);
}


static irqreturn_t  via_interrupt(int irq, void *dev_id)
{
	struct via_info *card = dev_id;
	u32 status32;

	/* to minimize interrupt sharing costs, we use the SGD status
	 * shadow register to check the status of all inputs and
	 * outputs with a single 32-bit bus read.  If no interrupt
	 * conditions are flagged, we exit immediately
	 */
	status32 = inl (card->baseaddr + VIA_BASE0_SGD_STATUS_SHADOW);
	if (!(status32 & VIA_INTR_MASK))
        {
#ifdef CONFIG_MIDI_VIA82CXXX
	    	 if (card->midi_devc)
                    	uart401intr(irq, card->midi_devc);
#endif
		return IRQ_HANDLED;
    	}
	DPRINTK ("intr, status32 == 0x%08X\n", status32);

	/* synchronize interrupt handling under SMP.  this spinlock
	 * goes away completely on UP
	 */
	spin_lock (&card->lock);

	if (status32 & VIA_INTR_OUT)
		via_intr_channel (card, &card->ch_out);
	if (status32 & VIA_INTR_IN)
		via_intr_channel (card, &card->ch_in);
	if (status32 & VIA_INTR_FM)
		via_intr_channel (card, &card->ch_fm);

	spin_unlock (&card->lock);
	
	return IRQ_HANDLED;
}

static irqreturn_t via_new_interrupt(int irq, void *dev_id)
{
	struct via_info *card = dev_id;
	u32 status32;

	/* to minimize interrupt sharing costs, we use the SGD status
	 * shadow register to check the status of all inputs and
	 * outputs with a single 32-bit bus read.  If no interrupt
	 * conditions are flagged, we exit immediately
	 */
	status32 = inl (card->baseaddr + VIA_BASE0_SGD_STATUS_SHADOW);
	if (!(status32 & VIA_NEW_INTR_MASK))
		return IRQ_NONE;
	/*
	 * goes away completely on UP
	 */
	spin_lock (&card->lock);

	via_intr_channel (card, &card->ch_out);
	via_intr_channel (card, &card->ch_in);
	via_intr_channel (card, &card->ch_fm);

	spin_unlock (&card->lock);
	return IRQ_HANDLED;
}


/**
 *	via_interrupt_init - Initialize interrupt handling
 *	@card: Private info for specified board
 *
 *	Obtain and reserve IRQ for using in handling audio events.
 *	Also, disable any IRQ-generating resources, to make sure
 *	we don't get interrupts before we want them.
 */

static int via_interrupt_init (struct via_info *card)
{
	u8 tmp8;

	DPRINTK ("ENTER\n");

	assert (card != NULL);
	assert (card->pdev != NULL);

	/* check for sane IRQ number. can this ever happen? */
	if (card->pdev->irq < 2) {
		printk (KERN_ERR PFX "insane IRQ %d, aborting\n",
			card->pdev->irq);
		DPRINTK ("EXIT, returning -EIO\n");
		return -EIO;
	}

	/* VIA requires this is done */
	pci_write_config_byte(card->pdev, PCI_INTERRUPT_LINE, card->pdev->irq);
	
	if(card->legacy)
	{
		/* make sure FM irq is not routed to us */
		pci_read_config_byte (card->pdev, VIA_FM_NMI_CTRL, &tmp8);
		if ((tmp8 & VIA_CR48_FM_TRAP_TO_NMI) == 0) {
			tmp8 |= VIA_CR48_FM_TRAP_TO_NMI;
			pci_write_config_byte (card->pdev, VIA_FM_NMI_CTRL, tmp8);
		}
		if (request_irq (card->pdev->irq, via_interrupt, IRQF_SHARED, VIA_MODULE_NAME, card)) {
			printk (KERN_ERR PFX "unable to obtain IRQ %d, aborting\n",
				card->pdev->irq);
			DPRINTK ("EXIT, returning -EBUSY\n");
			return -EBUSY;
		}
	}
	else 
	{
		if (request_irq (card->pdev->irq, via_new_interrupt, IRQF_SHARED, VIA_MODULE_NAME, card)) {
			printk (KERN_ERR PFX "unable to obtain IRQ %d, aborting\n",
				card->pdev->irq);
			DPRINTK ("EXIT, returning -EBUSY\n");
			return -EBUSY;
		}
	}

	DPRINTK ("EXIT, returning 0\n");
	return 0;
}


/****************************************************************
 *
 * OSS DSP device
 *
 */

static const struct file_operations via_dsp_fops = {
	.owner		= THIS_MODULE,
	.open		= via_dsp_open,
	.release	= via_dsp_release,
	.read		= via_dsp_read,
	.write		= via_dsp_write,
	.poll		= via_dsp_poll,
	.llseek		= no_llseek,
	.ioctl		= via_dsp_ioctl,
	.mmap		= via_dsp_mmap,
};


static int __devinit via_dsp_init (struct via_info *card)
{
	u8 tmp8;

	DPRINTK ("ENTER\n");

	assert (card != NULL);

	if(card->legacy)
	{
		/* turn off legacy features, if not already */
		pci_read_config_byte (card->pdev, VIA_FUNC_ENABLE, &tmp8);
		if (tmp8 & (VIA_CR42_SB_ENABLE |  VIA_CR42_FM_ENABLE)) {
			tmp8 &= ~(VIA_CR42_SB_ENABLE | VIA_CR42_FM_ENABLE);
			pci_write_config_byte (card->pdev, VIA_FUNC_ENABLE, tmp8);
		}
	}

	via_stop_everything (card);

	card->dev_dsp = register_sound_dsp (&via_dsp_fops, -1);
	if (card->dev_dsp < 0) {
		DPRINTK ("EXIT, returning -ENODEV\n");
		return -ENODEV;
	}
	DPRINTK ("EXIT, returning 0\n");
	return 0;
}


static void via_dsp_cleanup (struct via_info *card)
{
	DPRINTK ("ENTER\n");

	assert (card != NULL);
	assert (card->dev_dsp >= 0);

	via_stop_everything (card);

	unregister_sound_dsp (card->dev_dsp);

	DPRINTK ("EXIT\n");
}


static struct page * via_mm_nopage (struct vm_area_struct * vma,
				    unsigned long address, int *type)
{
	struct via_info *card = vma->vm_private_data;
	struct via_channel *chan = &card->ch_out;
	struct page *dmapage;
	unsigned long pgoff;
	int rd, wr;

	DPRINTK ("ENTER, start %lXh, ofs %lXh, pgoff %ld, addr %lXh\n",
		 vma->vm_start,
		 address - vma->vm_start,
		 (address - vma->vm_start) >> PAGE_SHIFT,
		 address);

        if (address > vma->vm_end) {
		DPRINTK ("EXIT, returning NOPAGE_SIGBUS\n");
		return NOPAGE_SIGBUS; /* Disallow mremap */
	}
        if (!card) {
		DPRINTK ("EXIT, returning NOPAGE_SIGBUS\n");
		return NOPAGE_SIGBUS;	/* Nothing allocated */
	}

	pgoff = vma->vm_pgoff + ((address - vma->vm_start) >> PAGE_SHIFT);
	rd = card->ch_in.is_mapped;
	wr = card->ch_out.is_mapped;

#ifndef VIA_NDEBUG
	{
	unsigned long max_bufs = chan->frag_number;
	if (rd && wr) max_bufs *= 2;
	/* via_dsp_mmap() should ensure this */
	assert (pgoff < max_bufs);
	}
#endif

	/* if full-duplex (read+write) and we have two sets of bufs,
	 * then the playback buffers come first, sez soundcard.c */
	if (pgoff >= chan->page_number) {
		pgoff -= chan->page_number;
		chan = &card->ch_in;
	} else if (!wr)
		chan = &card->ch_in;

	assert ((((unsigned long)chan->pgtbl[pgoff].cpuaddr) % PAGE_SIZE) == 0);

	dmapage = virt_to_page (chan->pgtbl[pgoff].cpuaddr);
	DPRINTK ("EXIT, returning page %p for cpuaddr %lXh\n",
		 dmapage, (unsigned long) chan->pgtbl[pgoff].cpuaddr);
	get_page (dmapage);
	if (type)
		*type = VM_FAULT_MINOR;
	return dmapage;
}


#ifndef VM_RESERVED
static int via_mm_swapout (struct page *page, struct file *filp)
{
	return 0;
}
#endif /* VM_RESERVED */


static struct vm_operations_struct via_mm_ops = {
	.nopage		= via_mm_nopage,

#ifndef VM_RESERVED
	.swapout	= via_mm_swapout,
#endif
};


static int via_dsp_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct via_info *card;
	int nonblock = (file->f_flags & O_NONBLOCK);
	int rc = -EINVAL, rd=0, wr=0;
	unsigned long max_size, size, start, offset;

	assert (file != NULL);
	assert (vma != NULL);
	card = file->private_data;
	assert (card != NULL);

	DPRINTK ("ENTER, start %lXh, size %ld, pgoff %ld\n",
		 vma->vm_start,
		 vma->vm_end - vma->vm_start,
		 vma->vm_pgoff);

	max_size = 0;
	if (vma->vm_flags & VM_READ) {
		rd = 1;
		via_chan_set_buffering(card, &card->ch_in, -1);
		via_chan_buffer_init (card, &card->ch_in);
		max_size += card->ch_in.page_number << PAGE_SHIFT;
	}
	if (vma->vm_flags & VM_WRITE) {
		wr = 1;
		via_chan_set_buffering(card, &card->ch_out, -1);
		via_chan_buffer_init (card, &card->ch_out);
		max_size += card->ch_out.page_number << PAGE_SHIFT;
	}

	start = vma->vm_start;
	offset = (vma->vm_pgoff << PAGE_SHIFT);
	size = vma->vm_end - vma->vm_start;

	/* some basic size/offset sanity checks */
	if (size > max_size)
		goto out;
	if (offset > max_size - size)
		goto out;

	rc = via_syscall_down (card, nonblock);
	if (rc) goto out;

	vma->vm_ops = &via_mm_ops;
	vma->vm_private_data = card;

#ifdef VM_RESERVED
	vma->vm_flags |= VM_RESERVED;
#endif

	if (rd)
		card->ch_in.is_mapped = 1;
	if (wr)
		card->ch_out.is_mapped = 1;

	mutex_unlock(&card->syscall_mutex);
	rc = 0;

out:
	DPRINTK ("EXIT, returning %d\n", rc);
	return rc;
}


static ssize_t via_dsp_do_read (struct via_info *card,
				char __user *userbuf, size_t count,
				int nonblock)
{
        DECLARE_WAITQUEUE(wait, current);
	const char __user *orig_userbuf = userbuf;
	struct via_channel *chan = &card->ch_in;
	size_t size;
	int n, tmp;
	ssize_t ret = 0;

	/* if SGD has not yet been started, start it */
	via_chan_maybe_start (chan);

handle_one_block:
	/* just to be a nice neighbor */
	/* Thomas Sailer:
	 * But also to ourselves, release semaphore if we do so */
	if (need_resched()) {
		mutex_unlock(&card->syscall_mutex);
		schedule ();
		ret = via_syscall_down (card, nonblock);
		if (ret)
			goto out;
	}

	/* grab current channel software pointer.  In the case of
	 * recording, this is pointing to the next buffer that
	 * will receive data from the audio hardware.
	 */
	n = chan->sw_ptr;

	/* n_frags represents the number of fragments waiting
	 * to be copied to userland.  sleep until at least
	 * one buffer has been read from the audio hardware.
	 */
	add_wait_queue(&chan->wait, &wait);
	for (;;) {
		__set_current_state(TASK_INTERRUPTIBLE);
		tmp = atomic_read (&chan->n_frags);
		assert (tmp >= 0);
		assert (tmp <= chan->frag_number);
		if (tmp)
			break;
		if (nonblock || !chan->is_active) {
			ret = -EAGAIN;
			break;
		}

		mutex_unlock(&card->syscall_mutex);

		DPRINTK ("Sleeping on block %d\n", n);
		schedule();

		ret = via_syscall_down (card, nonblock);
		if (ret)
			break;

		if (signal_pending (current)) {
			ret = -ERESTARTSYS;
			break;
		}
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&chan->wait, &wait);
	if (ret)
		goto out;

	/* Now that we have a buffer we can read from, send
	 * as much as sample data possible to userspace.
	 */
	while ((count > 0) && (chan->slop_len < chan->frag_size)) {
		size_t slop_left = chan->frag_size - chan->slop_len;
		void *base = chan->pgtbl[n / (PAGE_SIZE / chan->frag_size)].cpuaddr;
		unsigned ofs = (n % (PAGE_SIZE / chan->frag_size)) * chan->frag_size;

		size = (count < slop_left) ? count : slop_left;
		if (copy_to_user (userbuf,
				  base + ofs + chan->slop_len,
				  size)) {
			ret = -EFAULT;
			goto out;
		}

		count -= size;
		chan->slop_len += size;
		userbuf += size;
	}

	/* If we didn't copy the buffer completely to userspace,
	 * stop now.
	 */
	if (chan->slop_len < chan->frag_size)
		goto out;

	/*
	 * If we get to this point, we copied one buffer completely
	 * to userspace, give the buffer back to the hardware.
	 */

	/* advance channel software pointer to point to
	 * the next buffer from which we will copy
	 */
	if (chan->sw_ptr == (chan->frag_number - 1))
		chan->sw_ptr = 0;
	else
		chan->sw_ptr++;

	/* mark one less buffer waiting to be processed */
	assert (atomic_read (&chan->n_frags) > 0);
	atomic_dec (&chan->n_frags);

	/* we are at a block boundary, there is no fragment data */
	chan->slop_len = 0;

	DPRINTK ("Flushed block %u, sw_ptr now %u, n_frags now %d\n",
		n, chan->sw_ptr, atomic_read (&chan->n_frags));

	DPRINTK ("regs==%02X %02X %02X %08X %08X %08X %08X\n",
		 inb (card->baseaddr + 0x00),
		 inb (card->baseaddr + 0x01),
		 inb (card->baseaddr + 0x02),
		 inl (card->baseaddr + 0x04),
		 inl (card->baseaddr + 0x0C),
		 inl (card->baseaddr + 0x80),
		 inl (card->baseaddr + 0x84));

	if (count > 0)
		goto handle_one_block;

out:
	return (userbuf != orig_userbuf) ? (userbuf - orig_userbuf) : ret;
}


static ssize_t via_dsp_read(struct file *file, char __user *buffer, size_t count, loff_t *ppos)
{
	struct via_info *card;
	int nonblock = (file->f_flags & O_NONBLOCK);
	int rc;

	DPRINTK ("ENTER, file=%p, buffer=%p, count=%u, ppos=%lu\n",
		 file, buffer, count, ppos ? ((unsigned long)*ppos) : 0);

	assert (file != NULL);
	card = file->private_data;
	assert (card != NULL);

	rc = via_syscall_down (card, nonblock);
	if (rc) goto out;

	if (card->ch_in.is_mapped) {
		rc = -ENXIO;
		goto out_up;
	}

	via_chan_set_buffering(card, &card->ch_in, -1);
        rc = via_chan_buffer_init (card, &card->ch_in);

	if (rc)
		goto out_up;

	rc = via_dsp_do_read (card, buffer, count, nonblock);

out_up:
	mutex_unlock(&card->syscall_mutex);
out:
	DPRINTK ("EXIT, returning %ld\n",(long) rc);
	return rc;
}


static ssize_t via_dsp_do_write (struct via_info *card,
				 const char __user *userbuf, size_t count,
				 int nonblock)
{
        DECLARE_WAITQUEUE(wait, current);
	const char __user *orig_userbuf = userbuf;
	struct via_channel *chan = &card->ch_out;
	volatile struct via_sgd_table *sgtable = chan->sgtable;
	size_t size;
	int n, tmp;
	ssize_t ret = 0;

handle_one_block:
	/* just to be a nice neighbor */
	/* Thomas Sailer:
	 * But also to ourselves, release semaphore if we do so */
	if (need_resched()) {
		mutex_unlock(&card->syscall_mutex);
		schedule ();
		ret = via_syscall_down (card, nonblock);
		if (ret)
			goto out;
	}

	/* grab current channel fragment pointer.  In the case of
	 * playback, this is pointing to the next fragment that
	 * should receive data from userland.
	 */
	n = chan->sw_ptr;

	/* n_frags represents the number of fragments remaining
	 * to be filled by userspace.  Sleep until
	 * at least one fragment is available for our use.
	 */
	add_wait_queue(&chan->wait, &wait);
	for (;;) {
		__set_current_state(TASK_INTERRUPTIBLE);
		tmp = atomic_read (&chan->n_frags);
		assert (tmp >= 0);
		assert (tmp <= chan->frag_number);
		if (tmp)
			break;
		if (nonblock || !chan->is_active) {
			ret = -EAGAIN;
			break;
		}

		mutex_unlock(&card->syscall_mutex);

		DPRINTK ("Sleeping on page %d, tmp==%d, ir==%d\n", n, tmp, chan->is_record);
		schedule();

		ret = via_syscall_down (card, nonblock);
		if (ret)
			break;

		if (signal_pending (current)) {
			ret = -ERESTARTSYS;
			break;
		}
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&chan->wait, &wait);
	if (ret)
		goto out;

	/* Now that we have at least one fragment we can write to, fill the buffer
	 * as much as possible with data from userspace.
	 */
	while ((count > 0) && (chan->slop_len < chan->frag_size)) {
		size_t slop_left = chan->frag_size - chan->slop_len;

		size = (count < slop_left) ? count : slop_left;
		if (copy_from_user (chan->pgtbl[n / (PAGE_SIZE / chan->frag_size)].cpuaddr + (n % (PAGE_SIZE / chan->frag_size)) * chan->frag_size + chan->slop_len,
				    userbuf, size)) {
			ret = -EFAULT;
			goto out;
		}

		count -= size;
		chan->slop_len += size;
		userbuf += size;
	}

	/* If we didn't fill up the buffer with data, stop now.
         * Put a 'stop' marker in the DMA table too, to tell the
         * audio hardware to stop if it gets here.
         */
	if (chan->slop_len < chan->frag_size) {
		sgtable[n].count = cpu_to_le32 (chan->slop_len | VIA_EOL | VIA_STOP);
		goto out;
	}

	/*
         * If we get to this point, we have filled a buffer with
         * audio data, flush the buffer to audio hardware.
         */

	/* Record the true size for the audio hardware to notice */
        if (n == (chan->frag_number - 1))
                sgtable[n].count = cpu_to_le32 (chan->frag_size | VIA_EOL);
        else
                sgtable[n].count = cpu_to_le32 (chan->frag_size | VIA_FLAG);

	/* advance channel software pointer to point to
	 * the next buffer we will fill with data
	 */
	if (chan->sw_ptr == (chan->frag_number - 1))
		chan->sw_ptr = 0;
	else
		chan->sw_ptr++;

	/* mark one less buffer as being available for userspace consumption */
	assert (atomic_read (&chan->n_frags) > 0);
	atomic_dec (&chan->n_frags);

	/* we are at a block boundary, there is no fragment data */
	chan->slop_len = 0;

	/* if SGD has not yet been started, start it */
	via_chan_maybe_start (chan);

	DPRINTK ("Flushed block %u, sw_ptr now %u, n_frags now %d\n",
		n, chan->sw_ptr, atomic_read (&chan->n_frags));

	DPRINTK ("regs==S=%02X C=%02X TP=%02X BP=%08X RT=%08X SG=%08X CC=%08X SS=%08X\n",
		 inb (card->baseaddr + 0x00),
		 inb (card->baseaddr + 0x01),
		 inb (card->baseaddr + 0x02),
		 inl (card->baseaddr + 0x04),
		 inl (card->baseaddr + 0x08),
		 inl (card->baseaddr + 0x0C),
		 inl (card->baseaddr + 0x80),
		 inl (card->baseaddr + 0x84));

	if (count > 0)
		goto handle_one_block;

out:
	if (userbuf - orig_userbuf)
		return userbuf - orig_userbuf;
	else
		return ret;
}


static ssize_t via_dsp_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
	struct via_info *card;
	ssize_t rc;
	int nonblock = (file->f_flags & O_NONBLOCK);

	DPRINTK ("ENTER, file=%p, buffer=%p, count=%u, ppos=%lu\n",
		 file, buffer, count, ppos ? ((unsigned long)*ppos) : 0);

	assert (file != NULL);
	card = file->private_data;
	assert (card != NULL);

	rc = via_syscall_down (card, nonblock);
	if (rc) goto out;

	if (card->ch_out.is_mapped) {
		rc = -ENXIO;
		goto out_up;
	}

	via_chan_set_buffering(card, &card->ch_out, -1);
	rc = via_chan_buffer_init (card, &card->ch_out);

	if (rc)
		goto out_up;

	rc = via_dsp_do_write (card, buffer, count, nonblock);

out_up:
	mutex_unlock(&card->syscall_mutex);
out:
	DPRINTK ("EXIT, returning %ld\n",(long) rc);
	return rc;
}


static unsigned int via_dsp_poll(struct file *file, struct poll_table_struct *wait)
{
	struct via_info *card;
	struct via_channel *chan;
	unsigned int mask = 0;

	DPRINTK ("ENTER\n");

	assert (file != NULL);
	card = file->private_data;
	assert (card != NULL);

	if (file->f_mode & FMODE_READ) {
		chan = &card->ch_in;
		if (sg_active (chan->iobase))
	                poll_wait(file, &chan->wait, wait);
		if (atomic_read (&chan->n_frags) > 0)
			mask |= POLLIN | POLLRDNORM;
	}

	if (file->f_mode & FMODE_WRITE) {
		chan = &card->ch_out;
		if (sg_active (chan->iobase))
	                poll_wait(file, &chan->wait, wait);
		if (atomic_read (&chan->n_frags) > 0)
			mask |= POLLOUT | POLLWRNORM;
	}

	DPRINTK ("EXIT, returning %u\n", mask);
	return mask;
}


/**
 *	via_dsp_drain_playback - sleep until all playback samples are flushed
 *	@card: Private info for specified board
 *	@chan: Channel to drain
 *	@nonblock: boolean, non-zero if O_NONBLOCK is set
 *
 *	Sleeps until all playback has been flushed to the audio
 *	hardware.
 *
 *	Locking: inside card->syscall_mutex
 */

static int via_dsp_drain_playback (struct via_info *card,
				   struct via_channel *chan, int nonblock)
{
        DECLARE_WAITQUEUE(wait, current);
	int ret = 0;

	DPRINTK ("ENTER, nonblock = %d\n", nonblock);

	if (chan->slop_len > 0)
		via_chan_flush_frag (chan);

	if (atomic_read (&chan->n_frags) == chan->frag_number)
		goto out;

	via_chan_maybe_start (chan);

	add_wait_queue(&chan->wait, &wait);
	for (;;) {
		DPRINTK ("FRAGS %d FRAGNUM %d\n", atomic_read(&chan->n_frags), chan->frag_number);
		__set_current_state(TASK_INTERRUPTIBLE);
		if (atomic_read (&chan->n_frags) >= chan->frag_number)
			break;

		if (nonblock) {
			DPRINTK ("EXIT, returning -EAGAIN\n");
			ret = -EAGAIN;
			break;
		}

#ifdef VIA_DEBUG
		{
		u8 r40,r41,r42,r43,r44,r48;
		pci_read_config_byte (card->pdev, 0x40, &r40);
		pci_read_config_byte (card->pdev, 0x41, &r41);
		pci_read_config_byte (card->pdev, 0x42, &r42);
		pci_read_config_byte (card->pdev, 0x43, &r43);
		pci_read_config_byte (card->pdev, 0x44, &r44);
		pci_read_config_byte (card->pdev, 0x48, &r48);
		DPRINTK ("PCI config: %02X %02X %02X %02X %02X %02X\n",
			r40,r41,r42,r43,r44,r48);

		DPRINTK ("regs==%02X %02X %02X %08X %08X %08X %08X\n",
			 inb (card->baseaddr + 0x00),
			 inb (card->baseaddr + 0x01),
			 inb (card->baseaddr + 0x02),
			 inl (card->baseaddr + 0x04),
			 inl (card->baseaddr + 0x0C),
			 inl (card->baseaddr + 0x80),
			 inl (card->baseaddr + 0x84));
		}

		if (!chan->is_active)
			printk (KERN_ERR "sleeping but not active\n");
#endif

		mutex_unlock(&card->syscall_mutex);

		DPRINTK ("sleeping, nbufs=%d\n", atomic_read (&chan->n_frags));
		schedule();

		if ((ret = via_syscall_down (card, nonblock)))
			break;

		if (signal_pending (current)) {
			DPRINTK ("EXIT, returning -ERESTARTSYS\n");
			ret = -ERESTARTSYS;
			break;
		}
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&chan->wait, &wait);

#ifdef VIA_DEBUG
	{
		u8 r40,r41,r42,r43,r44,r48;
		pci_read_config_byte (card->pdev, 0x40, &r40);
		pci_read_config_byte (card->pdev, 0x41, &r41);
		pci_read_config_byte (card->pdev, 0x42, &r42);
		pci_read_config_byte (card->pdev, 0x43, &r43);
		pci_read_config_byte (card->pdev, 0x44, &r44);
		pci_read_config_byte (card->pdev, 0x48, &r48);
		DPRINTK ("PCI config: %02X %02X %02X %02X %02X %02X\n",
			r40,r41,r42,r43,r44,r48);

		DPRINTK ("regs==%02X %02X %02X %08X %08X %08X %08X\n",
			 inb (card->baseaddr + 0x00),
			 inb (card->baseaddr + 0x01),
			 inb (card->baseaddr + 0x02),
			 inl (card->baseaddr + 0x04),
			 inl (card->baseaddr + 0x0C),
			 inl (card->baseaddr + 0x80),
			 inl (card->baseaddr + 0x84));

		DPRINTK ("final nbufs=%d\n", atomic_read (&chan->n_frags));
	}
#endif

out:
	DPRINTK ("EXIT, returning %d\n", ret);
	return ret;
}


/**
 *	via_dsp_ioctl_space - get information about channel buffering
 *	@card: Private info for specified board
 *	@chan: pointer to channel-specific info
 *	@arg: user buffer for returned information
 *
 *	Handles SNDCTL_DSP_GETISPACE and SNDCTL_DSP_GETOSPACE.
 *
 *	Locking: inside card->syscall_mutex
 */

static int via_dsp_ioctl_space (struct via_info *card,
				struct via_channel *chan,
				void __user *arg)
{
	audio_buf_info info;

	via_chan_set_buffering(card, chan, -1);

	info.fragstotal = chan->frag_number;
	info.fragsize = chan->frag_size;

	/* number of full fragments we can read/write without blocking */
	info.fragments = atomic_read (&chan->n_frags);

	if ((chan->slop_len % chan->frag_size > 0) && (info.fragments > 0))
		info.fragments--;

	/* number of bytes that can be read or written immediately
	 * without blocking.
	 */
	info.bytes = (info.fragments * chan->frag_size);
	if (chan->slop_len % chan->frag_size > 0)
		info.bytes += chan->frag_size - (chan->slop_len % chan->frag_size);

	DPRINTK ("EXIT, returning fragstotal=%d, fragsize=%d, fragments=%d, bytes=%d\n",
		info.fragstotal,
		info.fragsize,
		info.fragments,
		info.bytes);

	return copy_to_user (arg, &info, sizeof (info))?-EFAULT:0;
}


/**
 *	via_dsp_ioctl_ptr - get information about hardware buffer ptr
 *	@card: Private info for specified board
 *	@chan: pointer to channel-specific info
 *	@arg: user buffer for returned information
 *
 *	Handles SNDCTL_DSP_GETIPTR and SNDCTL_DSP_GETOPTR.
 *
 *	Locking: inside card->syscall_mutex
 */

static int via_dsp_ioctl_ptr (struct via_info *card,
				struct via_channel *chan,
				void __user *arg)
{
	count_info info;

	spin_lock_irq (&card->lock);

	info.bytes = chan->bytes;
	info.blocks = chan->n_irqs;
	chan->n_irqs = 0;

	spin_unlock_irq (&card->lock);

	if (chan->is_active) {
		unsigned long extra;
		info.ptr = atomic_read (&chan->hw_ptr) * chan->frag_size;
		extra = chan->frag_size - via_sg_offset(chan);
		info.ptr += extra;
		info.bytes += extra;
	} else {
		info.ptr = 0;
	}

	DPRINTK ("EXIT, returning bytes=%d, blocks=%d, ptr=%d\n",
		info.bytes,
		info.blocks,
		info.ptr);

	return copy_to_user (arg, &info, sizeof (info))?-EFAULT:0;
}


static int via_dsp_ioctl_trigger (struct via_channel *chan, int val)
{
	int enable, do_something;

	if (chan->is_record)
		enable = (val & PCM_ENABLE_INPUT);
	else
		enable = (val & PCM_ENABLE_OUTPUT);

	if (!chan->is_enabled && enable) {
		do_something = 1;
	} else if (chan->is_enabled && !enable) {
		do_something = -1;
	} else {
		do_something = 0;
	}

	DPRINTK ("enable=%d, do_something=%d\n",
		 enable, do_something);

	if (chan->is_active && do_something)
		return -EINVAL;

	if (do_something == 1) {
		chan->is_enabled = 1;
		via_chan_maybe_start (chan);
		DPRINTK ("Triggering input\n");
	}

	else if (do_something == -1) {
		chan->is_enabled = 0;
		DPRINTK ("Setup input trigger\n");
	}

	return 0;
}


static int via_dsp_ioctl (struct inode *inode, struct file *file,
			  unsigned int cmd, unsigned long arg)
{
	int rc, rd=0, wr=0, val=0;
	struct via_info *card;
	struct via_channel *chan;
	int nonblock = (file->f_flags & O_NONBLOCK);
	int __user *ip = (int __user *)arg;
	void __user *p = (void __user *)arg;

	assert (file != NULL);
	card = file->private_data;
	assert (card != NULL);

	if (file->f_mode & FMODE_WRITE)
		wr = 1;
	if (file->f_mode & FMODE_READ)
		rd = 1;

	rc = via_syscall_down (card, nonblock);
	if (rc)
		return rc;
	rc = -EINVAL;

	switch (cmd) {

	/* OSS API version.  XXX unverified */
	case OSS_GETVERSION:
		DPRINTK ("ioctl OSS_GETVERSION, EXIT, returning SOUND_VERSION\n");
		rc = put_user (SOUND_VERSION, ip);
		break;

	/* list of supported PCM data formats */
	case SNDCTL_DSP_GETFMTS:
		DPRINTK ("DSP_GETFMTS, EXIT, returning AFMT U8|S16_LE\n");
                rc = put_user (AFMT_U8 | AFMT_S16_LE, ip);
		break;

	/* query or set current channel's PCM data format */
	case SNDCTL_DSP_SETFMT:
		if (get_user(val, ip)) {
			rc = -EFAULT;
			break;
		}
		DPRINTK ("DSP_SETFMT, val==%d\n", val);
		if (val != AFMT_QUERY) {
			rc = 0;

			if (rd)
				rc = via_chan_set_fmt (card, &card->ch_in, val);

			if (rc >= 0 && wr)
				rc = via_chan_set_fmt (card, &card->ch_out, val);

			if (rc < 0)
				break;

			val = rc;
		} else {
			if ((rd && (card->ch_in.pcm_fmt & VIA_PCM_FMT_16BIT)) ||
			    (wr && (card->ch_out.pcm_fmt & VIA_PCM_FMT_16BIT)))
				val = AFMT_S16_LE;
			else
				val = AFMT_U8;
		}
		DPRINTK ("SETFMT EXIT, returning %d\n", val);
                rc = put_user (val, ip);
		break;

	/* query or set number of channels (1=mono, 2=stereo, 4/6 for multichannel) */
        case SNDCTL_DSP_CHANNELS:
		if (get_user(val, ip)) {
			rc = -EFAULT;
			break;
		}
		DPRINTK ("DSP_CHANNELS, val==%d\n", val);
		if (val != 0) {
			rc = 0;

			if (rd)
				rc = via_chan_set_stereo (card, &card->ch_in, val);

			if (rc >= 0 && wr)
				rc = via_chan_set_stereo (card, &card->ch_out, val);

			if (rc < 0)
				break;

			val = rc;
		} else {
			if (rd)
				val = card->ch_in.channels;
			else
				val = card->ch_out.channels;
		}
		DPRINTK ("CHANNELS EXIT, returning %d\n", val);
                rc = put_user (val, ip);
		break;

	/* enable (val is not zero) or disable (val == 0) stereo */
        case SNDCTL_DSP_STEREO:
		if (get_user(val, ip)) {
			rc = -EFAULT;
			break;
		}
		DPRINTK ("DSP_STEREO, val==%d\n", val);
		rc = 0;

		if (rd)
			rc = via_chan_set_stereo (card, &card->ch_in, val ? 2 : 1);
		if (rc >= 0 && wr)
			rc = via_chan_set_stereo (card, &card->ch_out, val ? 2 : 1);

		if (rc < 0)
			break;

		val = rc - 1;

		DPRINTK ("STEREO EXIT, returning %d\n", val);
		rc = put_user(val, ip);
		break;

	/* query or set sampling rate */
        case SNDCTL_DSP_SPEED:
		if (get_user(val, ip)) {
			rc = -EFAULT;
			break;
		}
		DPRINTK ("DSP_SPEED, val==%d\n", val);
		if (val < 0) {
			rc = -EINVAL;
			break;
		}
		if (val > 0) {
			rc = 0;

			if (rd)
				rc = via_chan_set_speed (card, &card->ch_in, val);
			if (rc >= 0 && wr)
				rc = via_chan_set_speed (card, &card->ch_out, val);

			if (rc < 0)
				break;

			val = rc;
		} else {
			if (rd)
				val = card->ch_in.rate;
			else if (wr)
				val = card->ch_out.rate;
			else
				val = 0;
		}
		DPRINTK ("SPEED EXIT, returning %d\n", val);
                rc = put_user (val, ip);
		break;

	/* wait until all buffers have been played, and then stop device */
	case SNDCTL_DSP_SYNC:
		DPRINTK ("DSP_SYNC\n");
		rc = 0;
		if (wr) {
			DPRINTK ("SYNC EXIT (after calling via_dsp_drain_playback)\n");
			rc = via_dsp_drain_playback (card, &card->ch_out, nonblock);
		}
		break;

	/* stop recording/playback immediately */
        case SNDCTL_DSP_RESET:
		DPRINTK ("DSP_RESET\n");
		if (rd) {
			via_chan_clear (card, &card->ch_in);
			card->ch_in.frag_number = 0;
			card->ch_in.frag_size = 0;
			atomic_set(&card->ch_in.n_frags, 0);
		}

		if (wr) {
			via_chan_clear (card, &card->ch_out);
			card->ch_out.frag_number = 0;
			card->ch_out.frag_size = 0;
			atomic_set(&card->ch_out.n_frags, 0);
		}

		rc = 0;
		break;

	case SNDCTL_DSP_NONBLOCK:
		file->f_flags |= O_NONBLOCK;
		rc = 0;
		break;

	/* obtain bitmask of device capabilities, such as mmap, full duplex, etc. */
	case SNDCTL_DSP_GETCAPS:
		DPRINTK ("DSP_GETCAPS\n");
		rc = put_user(VIA_DSP_CAP, ip);
		break;

	/* obtain buffer fragment size */
	case SNDCTL_DSP_GETBLKSIZE:
		DPRINTK ("DSP_GETBLKSIZE\n");

		if (rd) {
			via_chan_set_buffering(card, &card->ch_in, -1);
			rc = put_user(card->ch_in.frag_size, ip);
		} else if (wr) {
			via_chan_set_buffering(card, &card->ch_out, -1);
			rc = put_user(card->ch_out.frag_size, ip);
		}
		break;

	/* obtain information about input buffering */
	case SNDCTL_DSP_GETISPACE:
		DPRINTK ("DSP_GETISPACE\n");
		if (rd)
			rc = via_dsp_ioctl_space (card, &card->ch_in, p);
		break;

	/* obtain information about output buffering */
	case SNDCTL_DSP_GETOSPACE:
		DPRINTK ("DSP_GETOSPACE\n");
		if (wr)
			rc = via_dsp_ioctl_space (card, &card->ch_out, p);
		break;

	/* obtain information about input hardware pointer */
	case SNDCTL_DSP_GETIPTR:
		DPRINTK ("DSP_GETIPTR\n");
		if (rd)
			rc = via_dsp_ioctl_ptr (card, &card->ch_in, p);
		break;

	/* obtain information about output hardware pointer */
	case SNDCTL_DSP_GETOPTR:
		DPRINTK ("DSP_GETOPTR\n");
		if (wr)
			rc = via_dsp_ioctl_ptr (card, &card->ch_out, p);
		break;

	/* return number of bytes remaining to be played by DMA engine */
	case SNDCTL_DSP_GETODELAY:
		{
		DPRINTK ("DSP_GETODELAY\n");

		chan = &card->ch_out;

		if (!wr)
			break;

		if (chan->is_active) {

			val = chan->frag_number - atomic_read (&chan->n_frags);

			assert(val >= 0);
				
			if (val > 0) {
				val *= chan->frag_size;
				val -= chan->frag_size - via_sg_offset(chan);
			}
			val += chan->slop_len % chan->frag_size;
		} else
			val = 0;

		assert (val <= (chan->frag_size * chan->frag_number));

		DPRINTK ("GETODELAY EXIT, val = %d bytes\n", val);
                rc = put_user (val, ip);
		break;
		}

	/* handle the quick-start of a channel,
	 * or the notification that a quick-start will
	 * occur in the future
	 */
	case SNDCTL_DSP_SETTRIGGER:
		if (get_user(val, ip)) {
			rc = -EFAULT;
			break;
		}
		DPRINTK ("DSP_SETTRIGGER, rd=%d, wr=%d, act=%d/%d, en=%d/%d\n",
			rd, wr, card->ch_in.is_active, card->ch_out.is_active,
			card->ch_in.is_enabled, card->ch_out.is_enabled);

		rc = 0;

		if (rd)
			rc = via_dsp_ioctl_trigger (&card->ch_in, val);

		if (!rc && wr)
			rc = via_dsp_ioctl_trigger (&card->ch_out, val);

		break;

	case SNDCTL_DSP_GETTRIGGER:
		val = 0;
		if ((file->f_mode & FMODE_READ) && card->ch_in.is_enabled)
			val |= PCM_ENABLE_INPUT;
		if ((file->f_mode & FMODE_WRITE) && card->ch_out.is_enabled)
			val |= PCM_ENABLE_OUTPUT;
		rc = put_user(val, ip);
		break;

	/* Enable full duplex.  Since we do this as soon as we are opened
	 * with O_RDWR, this is mainly a no-op that always returns success.
	 */
	case SNDCTL_DSP_SETDUPLEX:
		DPRINTK ("DSP_SETDUPLEX\n");
		if (!rd || !wr)
			break;
		rc = 0;
		break;

	/* set fragment size.  implemented as a successful no-op for now */
	case SNDCTL_DSP_SETFRAGMENT:
		if (get_user(val, ip)) {
			rc = -EFAULT;
			break;
		}
		DPRINTK ("DSP_SETFRAGMENT, val==%d\n", val);

		if (rd)
			rc = via_chan_set_buffering(card, &card->ch_in, val);

		if (wr)
			rc = via_chan_set_buffering(card, &card->ch_out, val);

		DPRINTK ("SNDCTL_DSP_SETFRAGMENT (fragshift==0x%04X (%d), maxfrags==0x%04X (%d))\n",
			 val & 0xFFFF,
			 val & 0xFFFF,
			 (val >> 16) & 0xFFFF,
			 (val >> 16) & 0xFFFF);

		rc = 0;
		break;

	/* inform device of an upcoming pause in input (or output). */
	case SNDCTL_DSP_POST:
		DPRINTK ("DSP_POST\n");
		if (wr) {
			if (card->ch_out.slop_len > 0)
				via_chan_flush_frag (&card->ch_out);
			via_chan_maybe_start (&card->ch_out);
		}

		rc = 0;
		break;

	/* not implemented */
	default:
		DPRINTK ("unhandled ioctl, cmd==%u, arg==%p\n",
			 cmd, p);
		break;
	}

	mutex_unlock(&card->syscall_mutex);
	DPRINTK ("EXIT, returning %d\n", rc);
	return rc;
}


static int via_dsp_open (struct inode *inode, struct file *file)
{
	int minor = iminor(inode);
	struct via_info *card;
	struct pci_dev *pdev = NULL;
	struct via_channel *chan;
	struct pci_driver *drvr;
	int nonblock = (file->f_flags & O_NONBLOCK);

	DPRINTK ("ENTER, minor=%d, file->f_mode=0x%x\n", minor, file->f_mode);

	if (!(file->f_mode & (FMODE_READ | FMODE_WRITE))) {
		DPRINTK ("EXIT, returning -EINVAL\n");
		return -EINVAL;
	}

	card = NULL;
	while ((pdev = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, pdev)) != NULL) {
		drvr = pci_dev_driver (pdev);
		if (drvr == &via_driver) {
			assert (pci_get_drvdata (pdev) != NULL);

			card = pci_get_drvdata (pdev);
			DPRINTK ("dev_dsp = %d, minor = %d, assn = %d\n",
				 card->dev_dsp, minor,
				 (card->dev_dsp ^ minor) & ~0xf);

			if (((card->dev_dsp ^ minor) & ~0xf) == 0)
				goto match;
		}
	}

	DPRINTK ("no matching %s found\n", card ? "minor" : "driver");
	return -ENODEV;

match:
	pci_dev_put(pdev);
	if (nonblock) {
		if (!mutex_trylock(&card->open_mutex)) {
			DPRINTK ("EXIT, returning -EAGAIN\n");
			return -EAGAIN;
		}
	} else {
		if (mutex_lock_interruptible(&card->open_mutex)) {
			DPRINTK ("EXIT, returning -ERESTARTSYS\n");
			return -ERESTARTSYS;
		}
	}

	file->private_data = card;
	DPRINTK ("file->f_mode == 0x%x\n", file->f_mode);

	/* handle input from analog source */
	if (file->f_mode & FMODE_READ) {
		chan = &card->ch_in;

		via_chan_init (card, chan);

		/* why is this forced to 16-bit stereo in all drivers? */
		chan->pcm_fmt = VIA_PCM_FMT_16BIT | VIA_PCM_FMT_STEREO;
		chan->channels = 2;

		// TO DO - use FIFO: via_capture_fifo(card, 1);
		via_chan_pcm_fmt (chan, 0);
		via_set_rate (card->ac97, chan, 44100);
	}

	/* handle output to analog source */
	if (file->f_mode & FMODE_WRITE) {
		chan = &card->ch_out;

		via_chan_init (card, chan);

		if (file->f_mode & FMODE_READ) {
			/* if in duplex mode make the recording and playback channels
			   have the same settings */
			chan->pcm_fmt = VIA_PCM_FMT_16BIT | VIA_PCM_FMT_STEREO;
			chan->channels = 2;
			via_chan_pcm_fmt (chan, 0);
                        via_set_rate (card->ac97, chan, 44100);
		} else {
			 if ((minor & 0xf) == SND_DEV_DSP16) {
				chan->pcm_fmt = VIA_PCM_FMT_16BIT;
				via_chan_pcm_fmt (chan, 0);
				via_set_rate (card->ac97, chan, 44100);
			} else {
				via_chan_pcm_fmt (chan, 1);
				via_set_rate (card->ac97, chan, 8000);
			}
		}
	}

	DPRINTK ("EXIT, returning 0\n");
	return nonseekable_open(inode, file);
}


static int via_dsp_release(struct inode *inode, struct file *file)
{
	struct via_info *card;
	int nonblock = (file->f_flags & O_NONBLOCK);
	int rc;

	DPRINTK ("ENTER\n");

	assert (file != NULL);
	card = file->private_data;
	assert (card != NULL);

	rc = via_syscall_down (card, nonblock);
	if (rc) {
		DPRINTK ("EXIT (syscall_down error), rc=%d\n", rc);
		return rc;
	}

	if (file->f_mode & FMODE_WRITE) {
		rc = via_dsp_drain_playback (card, &card->ch_out, nonblock);
		if (rc && rc != -ERESTARTSYS)	/* Nobody needs to know about ^C */
			printk (KERN_DEBUG "via_audio: ignoring drain playback error %d\n", rc);

		via_chan_free (card, &card->ch_out);
		via_chan_buffer_free(card, &card->ch_out);
	}

	if (file->f_mode & FMODE_READ) {
		via_chan_free (card, &card->ch_in);
		via_chan_buffer_free (card, &card->ch_in);
	}

	mutex_unlock(&card->syscall_mutex);
	mutex_unlock(&card->open_mutex);

	DPRINTK ("EXIT, returning 0\n");
	return 0;
}


/****************************************************************
 *
 * Chip setup and kernel registration
 *
 *
 */

static int __devinit via_init_one (struct pci_dev *pdev, const struct pci_device_id *id)
{
#ifdef CONFIG_MIDI_VIA82CXXX
	u8 r42;
#endif
	int rc;
	struct via_info *card;
	static int printed_version;

	DPRINTK ("ENTER\n");

	if (printed_version++ == 0)
		printk (KERN_INFO "Via 686a/8233/8235 audio driver " VIA_VERSION "\n");

	rc = pci_enable_device (pdev);
	if (rc)
		goto err_out;

	rc = pci_request_regions (pdev, "via82cxxx_audio");
	if (rc)
		goto err_out_disable;

	rc = pci_set_dma_mask(pdev, DMA_32BIT_MASK);
	if (rc)
		goto err_out_res;
	rc = pci_set_consistent_dma_mask(pdev, DMA_32BIT_MASK);
	if (rc)
		goto err_out_res;

	card = kmalloc (sizeof (*card), GFP_KERNEL);
	if (!card) {
		printk (KERN_ERR PFX "out of memory, aborting\n");
		rc = -ENOMEM;
		goto err_out_res;
	}

	pci_set_drvdata (pdev, card);

	memset (card, 0, sizeof (*card));
	card->pdev = pdev;
	card->baseaddr = pci_resource_start (pdev, 0);
	card->card_num = via_num_cards++;
	spin_lock_init (&card->lock);
	spin_lock_init (&card->ac97_lock);
	mutex_init(&card->syscall_mutex);
	mutex_init(&card->open_mutex);

	/* we must init these now, in case the intr handler needs them */
	via_chan_init_defaults (card, &card->ch_out);
	via_chan_init_defaults (card, &card->ch_in);
	via_chan_init_defaults (card, &card->ch_fm);

	/* if BAR 2 is present, chip is Rev H or later,
	 * which means it has a few extra features */
	if (pci_resource_start (pdev, 2) > 0)
		card->rev_h = 1;
		
	/* Overkill for now, but more flexible done right */
	
	card->intmask = id->driver_data;
	card->legacy = !card->intmask;
	card->sixchannel = id->driver_data;
	
	if(card->sixchannel)
		printk(KERN_INFO PFX "Six channel audio available\n");
	if (pdev->irq < 1) {
		printk (KERN_ERR PFX "invalid PCI IRQ %d, aborting\n", pdev->irq);
		rc = -ENODEV;
		goto err_out_kfree;
	}

	if (!(pci_resource_flags (pdev, 0) & IORESOURCE_IO)) {
		printk (KERN_ERR PFX "unable to locate I/O resources, aborting\n");
		rc = -ENODEV;
		goto err_out_kfree;
	}

	pci_set_master(pdev);
	
	/*
	 * init AC97 mixer and codec
	 */
	rc = via_ac97_init (card);
	if (rc) {
		printk (KERN_ERR PFX "AC97 init failed, aborting\n");
		goto err_out_kfree;
	}

	/*
	 * init DSP device
	 */
	rc = via_dsp_init (card);
	if (rc) {
		printk (KERN_ERR PFX "DSP device init failed, aborting\n");
		goto err_out_have_mixer;
	}

	/*
	 * init and turn on interrupts, as the last thing we do
	 */
	rc = via_interrupt_init (card);
	if (rc) {
		printk (KERN_ERR PFX "interrupt init failed, aborting\n");
		goto err_out_have_dsp;
	}

	printk (KERN_INFO PFX "board #%d at 0x%04lX, IRQ %d\n",
		card->card_num + 1, card->baseaddr, pdev->irq);

#ifdef CONFIG_MIDI_VIA82CXXX
	/* Disable by default */
	card->midi_info.io_base = 0;

	if(card->legacy)
	{
		pci_read_config_byte (pdev, 0x42, &r42);
		/* Disable MIDI interrupt */
		pci_write_config_byte (pdev, 0x42, r42 | VIA_CR42_MIDI_IRQMASK);
		if (r42 & VIA_CR42_MIDI_ENABLE)
		{
			if (r42 & VIA_CR42_MIDI_PNP) /* Address selected by iobase 2 - not tested */
				card->midi_info.io_base = pci_resource_start (pdev, 2);
			else /* Address selected by byte 0x43 */
			{
				u8 r43;
				pci_read_config_byte (pdev, 0x43, &r43);
				card->midi_info.io_base = 0x300 + ((r43 & 0x0c) << 2);
			}

			card->midi_info.irq = -pdev->irq;
			if (probe_uart401(& card->midi_info, THIS_MODULE))
			{
				card->midi_devc=midi_devs[card->midi_info.slots[4]]->devc;
				pci_write_config_byte(pdev, 0x42, r42 & ~VIA_CR42_MIDI_IRQMASK);
				printk("Enabled Via MIDI\n");
			}
		}
	}
#endif

	DPRINTK ("EXIT, returning 0\n");
	return 0;

err_out_have_dsp:
	via_dsp_cleanup (card);

err_out_have_mixer:
	via_ac97_cleanup (card);

err_out_kfree:
#ifndef VIA_NDEBUG
	memset (card, OSS_POISON_FREE, sizeof (*card)); /* poison memory */
#endif
	kfree (card);

err_out_res:
	pci_release_regions (pdev);

err_out_disable:
	pci_disable_device (pdev);

err_out:
	pci_set_drvdata (pdev, NULL);
	DPRINTK ("EXIT - returning %d\n", rc);
	return rc;
}


static void __devexit via_remove_one (struct pci_dev *pdev)
{
	struct via_info *card;

	DPRINTK ("ENTER\n");

	assert (pdev != NULL);
	card = pci_get_drvdata (pdev);
	assert (card != NULL);

#ifdef CONFIG_MIDI_VIA82CXXX
	if (card->midi_info.io_base)
		unload_uart401(&card->midi_info);
#endif

	free_irq (card->pdev->irq, card);
	via_dsp_cleanup (card);
	via_ac97_cleanup (card);

#ifndef VIA_NDEBUG
	memset (card, OSS_POISON_FREE, sizeof (*card)); /* poison memory */
#endif
	kfree (card);

	pci_set_drvdata (pdev, NULL);

	pci_release_regions (pdev);
	pci_disable_device (pdev);
	pci_set_power_state (pdev, 3); /* ...zzzzzz */

	DPRINTK ("EXIT\n");
	return;
}


/****************************************************************
 *
 * Driver initialization and cleanup
 *
 *
 */

static int __init init_via82cxxx_audio(void)
{
	int rc;

	DPRINTK ("ENTER\n");

	rc = pci_register_driver (&via_driver);
	if (rc) {
		DPRINTK ("EXIT, returning %d\n", rc);
		return rc;
	}

	DPRINTK ("EXIT, returning 0\n");
	return 0;
}


static void __exit cleanup_via82cxxx_audio(void)
{
	DPRINTK ("ENTER\n");

	pci_unregister_driver (&via_driver);

	DPRINTK ("EXIT\n");
}


module_init(init_via82cxxx_audio);
module_exit(cleanup_via82cxxx_audio);

MODULE_AUTHOR("Jeff Garzik");
MODULE_DESCRIPTION("DSP audio and mixer driver for Via 82Cxxx audio devices");
MODULE_LICENSE("GPL");

