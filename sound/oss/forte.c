/*
 * forte.c - ForteMedia FM801 OSS Driver
 *
 * Written by Martin K. Petersen <mkp@mkp.net>
 * Copyright (C) 2002 Hewlett-Packard Company
 * Portions Copyright (C) 2003 Martin K. Petersen
 *
 * Latest version: http://mkp.net/forte/
 *
 * Based upon the ALSA FM801 driver by Jaroslav Kysela and OSS drivers
 * by Thomas Sailer, Alan Cox, Zach Brown, and Jeff Garzik.  Thanks
 * guys!
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 *
 */
 
#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>

#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/pci.h>

#include <linux/delay.h>
#include <linux/poll.h>

#include <linux/sound.h>
#include <linux/ac97_codec.h>
#include <linux/interrupt.h>

#include <linux/proc_fs.h>

#include <asm/uaccess.h>
#include <asm/io.h>

#define DRIVER_NAME	"forte"
#define DRIVER_VERSION 	"$Id: forte.c,v 1.63 2003/03/01 05:32:42 mkp Exp $"
#define PFX 		DRIVER_NAME ": "

#undef M_DEBUG

#ifdef M_DEBUG
#define DPRINTK(args...) printk(KERN_WARNING args)
#else
#define DPRINTK(args...)
#endif

/* Card capabilities */
#define FORTE_CAPS              (DSP_CAP_MMAP | DSP_CAP_TRIGGER)

/* Supported audio formats */
#define FORTE_FMTS		(AFMT_U8 | AFMT_S16_LE)

/* Buffers */
#define FORTE_MIN_FRAG_SIZE     256
#define FORTE_MAX_FRAG_SIZE     PAGE_SIZE
#define FORTE_DEF_FRAG_SIZE     256
#define FORTE_MIN_FRAGMENTS     2
#define FORTE_MAX_FRAGMENTS     256
#define FORTE_DEF_FRAGMENTS     2
#define FORTE_MIN_BUF_MSECS     500
#define FORTE_MAX_BUF_MSECS     1000

/* PCI BARs */
#define FORTE_PCM_VOL           0x00    /* PCM Output Volume */
#define FORTE_FM_VOL            0x02    /* FM Output Volume */
#define FORTE_I2S_VOL           0x04    /* I2S Volume */
#define FORTE_REC_SRC           0x06    /* Record Source */
#define FORTE_PLY_CTRL          0x08    /* Playback Control */
#define FORTE_PLY_COUNT         0x0a    /* Playback Count */
#define FORTE_PLY_BUF1          0x0c    /* Playback Buffer I */
#define FORTE_PLY_BUF2          0x10    /* Playback Buffer II */
#define FORTE_CAP_CTRL          0x14    /* Capture Control */
#define FORTE_CAP_COUNT         0x16    /* Capture Count */
#define FORTE_CAP_BUF1          0x18    /* Capture Buffer I */
#define FORTE_CAP_BUF2          0x1c    /* Capture Buffer II */
#define FORTE_CODEC_CTRL        0x22    /* Codec Control */
#define FORTE_I2S_MODE          0x24    /* I2S Mode Control */
#define FORTE_VOLUME            0x26    /* Volume Up/Down/Mute Status */
#define FORTE_I2C_CTRL          0x29    /* I2C Control */
#define FORTE_AC97_CMD          0x2a    /* AC'97 Command */
#define FORTE_AC97_DATA         0x2c    /* AC'97 Data */
#define FORTE_MPU401_DATA       0x30    /* MPU401 Data */
#define FORTE_MPU401_CMD        0x31    /* MPU401 Command */
#define FORTE_GPIO_CTRL         0x52    /* General Purpose I/O Control */
#define FORTE_GEN_CTRL          0x54    /* General Control */
#define FORTE_IRQ_MASK          0x56    /* Interrupt Mask */
#define FORTE_IRQ_STATUS        0x5a    /* Interrupt Status */
#define FORTE_OPL3_BANK0        0x68    /* OPL3 Status Read / Bank 0 Write */
#define FORTE_OPL3_DATA0        0x69    /* OPL3 Data 0 Write */
#define FORTE_OPL3_BANK1        0x6a    /* OPL3 Bank 1 Write */
#define FORTE_OPL3_DATA1        0x6b    /* OPL3 Bank 1 Write */
#define FORTE_POWERDOWN         0x70    /* Blocks Power Down Control */

#define FORTE_CAP_OFFSET        FORTE_CAP_CTRL - FORTE_PLY_CTRL

#define FORTE_AC97_ADDR_SHIFT   10

/* Playback and record control register bits */
#define FORTE_BUF1_LAST         (1<<1)
#define FORTE_BUF2_LAST         (1<<2)
#define FORTE_START             (1<<5)
#define FORTE_PAUSE             (1<<6)
#define FORTE_IMMED_STOP        (1<<7)
#define FORTE_RATE_SHIFT        8
#define FORTE_RATE_MASK         (15 << FORTE_RATE_SHIFT)
#define FORTE_CHANNELS_4        (1<<12) /* Playback only */
#define FORTE_CHANNELS_6        (2<<12) /* Playback only */
#define FORTE_CHANNELS_6MS      (3<<12) /* Playback only */
#define FORTE_CHANNELS_MASK     (3<<12)
#define FORTE_16BIT             (1<<14)
#define FORTE_STEREO            (1<<15)

/* IRQ status bits */
#define FORTE_IRQ_PLAYBACK      (1<<8)
#define FORTE_IRQ_CAPTURE       (1<<9)
#define FORTE_IRQ_VOLUME        (1<<14)
#define FORTE_IRQ_MPU           (1<<15)

/* CODEC control */
#define FORTE_CC_CODEC_RESET    (1<<5)
#define FORTE_CC_AC97_RESET     (1<<6)

/* AC97 cmd */
#define FORTE_AC97_WRITE        (0<<7)
#define FORTE_AC97_READ         (1<<7)
#define FORTE_AC97_DP_INVALID   (0<<8)
#define FORTE_AC97_DP_VALID     (1<<8)
#define FORTE_AC97_PORT_RDY     (0<<9)
#define FORTE_AC97_PORT_BSY     (1<<9)


struct forte_channel {
        const char 		*name;

	unsigned short		ctrl; 		/* Ctrl BAR contents */
	unsigned long 		iobase;		/* Ctrl BAR address */

	wait_queue_head_t	wait;

	void 			*buf; 		/* Buffer */
	dma_addr_t		buf_handle; 	/* Buffer handle */

        unsigned int 		record;
	unsigned int		format;
        unsigned int		rate;
	unsigned int		stereo;

	unsigned int		frag_sz; 	/* Current fragment size */
	unsigned int		frag_num; 	/* Current # of fragments */
	unsigned int		frag_msecs;     /* Milliseconds per frag */
	unsigned int		buf_sz;		/* Current buffer size */

	unsigned int		hwptr;		/* Tail */
	unsigned int		swptr; 		/* Head */
	unsigned int		filled_frags; 	/* Fragments currently full */
	unsigned int		next_buf;	/* Index of next buffer */

	unsigned int		active;		/* Channel currently in use */
	unsigned int		mapped;		/* mmap */

	unsigned int		buf_pages;	/* Real size of buffer */
	unsigned int		nr_irqs;	/* Number of interrupts */
	unsigned int		bytes;		/* Total bytes */
	unsigned int		residue;	/* Partial fragment */
};


struct forte_chip {
	struct pci_dev		*pci_dev;
	unsigned long		iobase;
	int			irq;

	struct semaphore	open_sem; 	/* Device access */
	spinlock_t		lock;		/* State */

	spinlock_t		ac97_lock;
	struct ac97_codec	*ac97;

	int			multichannel;
	int			dsp; 		/* OSS handle */
	int                     trigger;	/* mmap I/O trigger */

	struct forte_channel	play;
	struct forte_channel	rec;
};


static int channels[] = { 2, 4, 6, };
static int rates[]    = { 5500, 8000, 9600, 11025, 16000, 19200, 
			  22050, 32000, 38400, 44100, 48000, };

static struct forte_chip *forte;
static int found;


/* AC97 Codec -------------------------------------------------------------- */


/** 
 * forte_ac97_wait:
 * @chip:	fm801 instance whose AC97 codec to wait on
 *
 * FIXME:
 *		Stop busy-waiting
 */

static inline int
forte_ac97_wait (struct forte_chip *chip)
{
	int i = 10000;

	while ( (inw (chip->iobase + FORTE_AC97_CMD) & FORTE_AC97_PORT_BSY) 
		&& i-- )
		cpu_relax();

	return i == 0;
}


/**
 * forte_ac97_read:
 * @codec:	AC97 codec to read from
 * @reg:	register to read
 */

static u16
forte_ac97_read (struct ac97_codec *codec, u8 reg)
{
	u16 ret = 0;
	struct forte_chip *chip = codec->private_data;

	spin_lock (&chip->ac97_lock);

	/* Knock, knock */
	if (forte_ac97_wait (chip)) {
		printk (KERN_ERR PFX "ac97_read: Serial bus busy\n");
		goto out;
	}

	/* Send read command */
	outw (reg | (1<<7), chip->iobase + FORTE_AC97_CMD);

	if (forte_ac97_wait (chip)) {
		printk (KERN_ERR PFX "ac97_read: Bus busy reading reg 0x%x\n",
			reg);
		goto out;
	}
	
	/* Sanity checking */
	if (inw (chip->iobase + FORTE_AC97_CMD) & FORTE_AC97_DP_INVALID) {
		printk (KERN_ERR PFX "ac97_read: Invalid data port");
		goto out;
	}

	/* Fetch result */
	ret = inw (chip->iobase + FORTE_AC97_DATA);

 out:
	spin_unlock (&chip->ac97_lock);
	return ret;
}


/**
 * forte_ac97_write:
 * @codec:	AC97 codec to send command to
 * @reg:	register to write
 * @val:	value to write
 */

static void
forte_ac97_write (struct ac97_codec *codec, u8 reg, u16 val)
{
	struct forte_chip *chip = codec->private_data;

	spin_lock (&chip->ac97_lock);

	/* Knock, knock */
	if (forte_ac97_wait (chip)) {
		printk (KERN_ERR PFX "ac97_write: Serial bus busy\n");
		goto out;
	}

	outw (val, chip->iobase + FORTE_AC97_DATA);
	outb (reg | FORTE_AC97_WRITE, chip->iobase + FORTE_AC97_CMD);

	/* Wait for completion */
	if (forte_ac97_wait (chip)) {
		printk (KERN_ERR PFX "ac97_write: Bus busy after write\n");
		goto out;
	}

 out:
	spin_unlock (&chip->ac97_lock);
}


/* Mixer ------------------------------------------------------------------- */


/**
 * forte_mixer_open:
 * @inode:		
 * @file:		
 */

static int
forte_mixer_open (struct inode *inode, struct file *file)
{
	struct forte_chip *chip = forte;
	file->private_data = chip->ac97;
	return 0;
}


/**
 * forte_mixer_release:
 * @inode:		
 * @file:		
 */

static int
forte_mixer_release (struct inode *inode, struct file *file)
{
	/* We will welease Wodewick */
	return 0;
}


/**
 * forte_mixer_ioctl:
 * @inode:		
 * @file:		
 */

static int
forte_mixer_ioctl (struct inode *inode, struct file *file, 
		   unsigned int cmd, unsigned long arg)
{
	struct ac97_codec *codec = (struct ac97_codec *) file->private_data;

	return codec->mixer_ioctl (codec, cmd, arg);
}


static struct file_operations forte_mixer_fops = {
	.owner			= THIS_MODULE,
	.llseek         	= no_llseek,
	.ioctl          	= forte_mixer_ioctl,
	.open           	= forte_mixer_open,
	.release        	= forte_mixer_release,
};


/* Channel ----------------------------------------------------------------- */

/** 
 * forte_channel_reset:
 * @channel:	Channel to reset
 * 
 * Locking:	Must be called with lock held.
 */

static void
forte_channel_reset (struct forte_channel *channel)
{
	if (!channel || !channel->iobase)
		return;

	DPRINTK ("%s: channel = %s\n", __FUNCTION__, channel->name);

	channel->ctrl &= ~FORTE_START;
	outw (channel->ctrl, channel->iobase + FORTE_PLY_CTRL);
	
	/* We always play at least two fragments, hence these defaults */
 	channel->hwptr = channel->frag_sz;
	channel->next_buf = 1;
	channel->swptr = 0;
	channel->filled_frags = 0;
	channel->active = 0;
	channel->bytes = 0;
	channel->nr_irqs = 0;
	channel->mapped = 0;
	channel->residue = 0;
}


/** 
 * forte_channel_start:
 * @channel: 	Channel to start (record/playback)
 *
 * Locking:	Must be called with lock held.
 */

static void inline
forte_channel_start (struct forte_channel *channel)
{
	if (!channel || !channel->iobase || channel->active) 
		return;

	channel->ctrl &= ~(FORTE_PAUSE | FORTE_BUF1_LAST | FORTE_BUF2_LAST
			   | FORTE_IMMED_STOP);
	channel->ctrl |= FORTE_START;
	channel->active = 1;
	outw (channel->ctrl, channel->iobase + FORTE_PLY_CTRL);
}


/** 
 * forte_channel_stop:
 * @channel: 	Channel to stop
 *
 * Locking:	Must be called with lock held.
 */

static void inline
forte_channel_stop (struct forte_channel *channel)
{
	if (!channel || !channel->iobase) 
		return;

	channel->ctrl &= ~(FORTE_START | FORTE_PAUSE);	
	channel->ctrl |= FORTE_IMMED_STOP;

	channel->active = 0;
	outw (channel->ctrl, channel->iobase + FORTE_PLY_CTRL);
}


/** 
 * forte_channel_pause:
 * @channel: 	Channel to pause
 *
 * Locking:	Must be called with lock held.
 */

static void inline
forte_channel_pause (struct forte_channel *channel)
{
	if (!channel || !channel->iobase) 
		return;

	channel->ctrl |= FORTE_PAUSE;

	channel->active = 0;
	outw (channel->ctrl, channel->iobase + FORTE_PLY_CTRL);
}


/** 
 * forte_channel_rate:
 * @channel: 	Channel whose rate to set.  Playback and record are
 *           	independent.
 * @rate:    	Channel rate in Hz
 *
 * Locking:	Must be called with lock held.
 */

static int
forte_channel_rate (struct forte_channel *channel, unsigned int rate)
{
	int new_rate;

	if (!channel || !channel->iobase) 
		return -EINVAL;

	/* The FM801 only supports a handful of fixed frequencies.
	 * We find the value closest to what userland requested.
	 */
	if      (rate <= 6250)  { rate = 5500;  new_rate =  0; }
	else if (rate <= 8800)  { rate = 8000;  new_rate =  1; }
	else if (rate <= 10312) { rate = 9600;  new_rate =  2; }
	else if (rate <= 13512) { rate = 11025; new_rate =  3; }
	else if (rate <= 17600) { rate = 16000; new_rate =  4; }
	else if (rate <= 20625) { rate = 19200; new_rate =  5; }
	else if (rate <= 27025) { rate = 22050; new_rate =  6; }
	else if (rate <= 35200) { rate = 32000; new_rate =  7; }
	else if (rate <= 41250) { rate = 38400; new_rate =  8; }
	else if (rate <= 46050) { rate = 44100; new_rate =  9; }
	else                    { rate = 48000; new_rate = 10; }

	channel->ctrl &= ~FORTE_RATE_MASK;
	channel->ctrl |= new_rate << FORTE_RATE_SHIFT;
	channel->rate = rate;

	DPRINTK ("%s: %s rate = %d\n", __FUNCTION__, channel->name, rate);

	return rate;
}


/** 
 * forte_channel_format:
 * @channel: 	Channel whose audio format to set
 * @format:  	OSS format ID
 *
 * Locking:	Must be called with lock held.
 */

static int
forte_channel_format (struct forte_channel *channel, int format)
{
	if (!channel || !channel->iobase) 
		return -EINVAL;

	switch (format) {

	case AFMT_QUERY:
		break;
	
	case AFMT_U8:
		channel->ctrl &= ~FORTE_16BIT;
		channel->format = AFMT_U8;
		break;

	case AFMT_S16_LE:
	default:
		channel->ctrl |= FORTE_16BIT;
		channel->format = AFMT_S16_LE;
		break;
	}

	DPRINTK ("%s: %s want %d format, got %d\n", __FUNCTION__, channel->name, 
		 format, channel->format);

	return channel->format;
}


/** 
 * forte_channel_stereo:
 * @channel: 	Channel to toggle
 * @stereo:  	0 for Mono, 1 for Stereo
 *
 * Locking:	Must be called with lock held.
 */

static int
forte_channel_stereo (struct forte_channel *channel, unsigned int stereo)
{
	int ret;

	if (!channel || !channel->iobase)
		return -EINVAL;

	DPRINTK ("%s: %s stereo = %d\n", __FUNCTION__, channel->name, stereo);

	switch (stereo) {

	case 0:
		channel->ctrl &= ~(FORTE_STEREO | FORTE_CHANNELS_MASK);
		channel-> stereo = stereo;
		ret = stereo;
		break;

	case 1:
		channel->ctrl &= ~FORTE_CHANNELS_MASK;
		channel->ctrl |= FORTE_STEREO;
		channel-> stereo = stereo;
		ret = stereo;
		break;

	default:
		DPRINTK ("Unsupported channel format");
		ret = -EINVAL;
		break;
	}

	return ret;
}


/** 
 * forte_channel_buffer:
 * @channel:	Channel whose buffer to set up
 *
 * Locking:	Must be called with lock held.
 */

static void
forte_channel_buffer (struct forte_channel *channel, int sz, int num)
{
	unsigned int msecs, shift;

	/* Go away, I'm busy */
	if (channel->filled_frags || channel->bytes)
		return;

	/* Fragment size must be a power of 2 */
	shift = 0; sz++;
	while (sz >>= 1)
		shift++;
	channel->frag_sz = 1 << shift;

	/* Round fragment size to something reasonable */
	if (channel->frag_sz < FORTE_MIN_FRAG_SIZE)
		channel->frag_sz = FORTE_MIN_FRAG_SIZE;

	if (channel->frag_sz > FORTE_MAX_FRAG_SIZE)
		channel->frag_sz = FORTE_MAX_FRAG_SIZE;

	/* Find fragment length in milliseconds */
	msecs = channel->frag_sz /
		(channel->format == AFMT_S16_LE ? 2 : 1) /
		(channel->stereo ? 2 : 1) /
		(channel->rate / 1000);

	channel->frag_msecs = msecs;

	/* Pick a suitable number of fragments */
	if (msecs * num < FORTE_MIN_BUF_MSECS)
	     num = FORTE_MIN_BUF_MSECS / msecs;

	if (msecs * num > FORTE_MAX_BUF_MSECS)
	     num = FORTE_MAX_BUF_MSECS / msecs;

	/* Fragment number must be a power of 2 */
	shift = 0;	
	while (num >>= 1)
		shift++;
	channel->frag_num = 1 << (shift + 1);

	/* Round fragment number to something reasonable */
	if (channel->frag_num < FORTE_MIN_FRAGMENTS)
		channel->frag_num = FORTE_MIN_FRAGMENTS;

	if (channel->frag_num > FORTE_MAX_FRAGMENTS)
		channel->frag_num = FORTE_MAX_FRAGMENTS;

	channel->buf_sz = channel->frag_sz * channel->frag_num;

	DPRINTK ("%s: %s frag_sz = %d, frag_num = %d, buf_sz = %d\n",
		 __FUNCTION__, channel->name, channel->frag_sz, 
		 channel->frag_num, channel->buf_sz);
}


/** 
 * forte_channel_prep:
 * @channel:	Channel whose buffer to prepare
 *
 * Locking:	Lock held.
 */

static void
forte_channel_prep (struct forte_channel *channel)
{
	struct page *page;
	int i;
	
	if (channel->buf)
		return;

	forte_channel_buffer (channel, channel->frag_sz, channel->frag_num);
	channel->buf_pages = channel->buf_sz >> PAGE_SHIFT;

	if (channel->buf_sz % PAGE_SIZE)
		channel->buf_pages++;

	DPRINTK ("%s: %s frag_sz = %d, frag_num = %d, buf_sz = %d, pg = %d\n", 
		 __FUNCTION__, channel->name, channel->frag_sz, 
		 channel->frag_num, channel->buf_sz, channel->buf_pages);

	/* DMA buffer */
	channel->buf = pci_alloc_consistent (forte->pci_dev, 
					     channel->buf_pages * PAGE_SIZE,
					     &channel->buf_handle);

	if (!channel->buf || !channel->buf_handle)
		BUG();

	page = virt_to_page (channel->buf);
	
	/* FIXME: can this go away ? */
	for (i = 0 ; i < channel->buf_pages ; i++)
		SetPageReserved(page++);

	/* Prep buffer registers */
	outw (channel->frag_sz - 1, channel->iobase + FORTE_PLY_COUNT);
	outl (channel->buf_handle, channel->iobase + FORTE_PLY_BUF1);
	outl (channel->buf_handle + channel->frag_sz, 
	      channel->iobase + FORTE_PLY_BUF2);

	/* Reset hwptr */
 	channel->hwptr = channel->frag_sz;
	channel->next_buf = 1;

	DPRINTK ("%s: %s buffer @ %p (%p)\n", __FUNCTION__, channel->name, 
		 channel->buf, channel->buf_handle);
}


/** 
 * forte_channel_drain:
 * @chip:	
 * @channel:	
 *
 * Locking:	Don't hold the lock.
 */

static inline int
forte_channel_drain (struct forte_channel *channel)
{
	DECLARE_WAITQUEUE (wait, current);
	unsigned long flags;

	DPRINTK ("%s\n", __FUNCTION__);

	if (channel->mapped) {
		spin_lock_irqsave (&forte->lock, flags);
		forte_channel_stop (channel);
		spin_unlock_irqrestore (&forte->lock, flags);
		return 0;
	}

	spin_lock_irqsave (&forte->lock, flags);
	add_wait_queue (&channel->wait, &wait);

	for (;;) {
		if (channel->active == 0 || channel->filled_frags == 1)
			break;

		spin_unlock_irqrestore (&forte->lock, flags);

		__set_current_state (TASK_INTERRUPTIBLE);
		schedule();

		spin_lock_irqsave (&forte->lock, flags);
	}

	forte_channel_stop (channel);
	forte_channel_reset (channel);
	set_current_state (TASK_RUNNING);
	remove_wait_queue (&channel->wait, &wait);
	spin_unlock_irqrestore (&forte->lock, flags);

	return 0;
}


/** 
 * forte_channel_init:
 * @chip: 	Forte chip instance the channel hangs off
 * @channel: 	Channel to initialize
 *
 * Description:
 *	        Initializes a channel, sets defaults, and allocates
 *	        buffers.
 *
 * Locking:	No lock held.
 */

static int
forte_channel_init (struct forte_chip *chip, struct forte_channel *channel)
{
	DPRINTK ("%s: chip iobase @ %p\n", __FUNCTION__, (void *)chip->iobase);

	spin_lock_irq (&chip->lock);
	memset (channel, 0x0, sizeof (*channel));

	if (channel == &chip->play) {
		channel->name = "PCM_OUT";
		channel->iobase = chip->iobase;
		DPRINTK ("%s: PCM-OUT iobase @ %p\n", __FUNCTION__,
			 (void *) channel->iobase);
	}
	else if (channel == &chip->rec) {
		channel->name = "PCM_IN";
		channel->iobase = chip->iobase + FORTE_CAP_OFFSET;
		channel->record = 1;
		DPRINTK ("%s: PCM-IN iobase @ %p\n", __FUNCTION__, 
			 (void *) channel->iobase);
	}
	else
		BUG();

	init_waitqueue_head (&channel->wait);

	/* Defaults: 48kHz, 16-bit, stereo */
	channel->ctrl = inw (channel->iobase + FORTE_PLY_CTRL);
	forte_channel_reset (channel);
	forte_channel_stereo (channel, 1);
	forte_channel_format (channel, AFMT_S16_LE);
	forte_channel_rate (channel, 48000);
	channel->frag_sz = FORTE_DEF_FRAG_SIZE;
	channel->frag_num = FORTE_DEF_FRAGMENTS;

	chip->trigger = 0;
	spin_unlock_irq (&chip->lock);

	return 0;
}


/** 
 * forte_channel_free:
 * @chip:	Chip this channel hangs off
 * @channel:	Channel to nuke 
 *
 * Description:
 * 		Resets channel and frees buffers.
 *
 * Locking:	Hold your horses.
 */

static void
forte_channel_free (struct forte_chip *chip, struct forte_channel *channel)
{
	DPRINTK ("%s: %s\n", __FUNCTION__, channel->name);

	if (!channel->buf_handle)
		return;

	pci_free_consistent (chip->pci_dev, channel->buf_pages * PAGE_SIZE, 
			     channel->buf, channel->buf_handle);
	
	memset (channel, 0x0, sizeof (*channel));
}


/* DSP --------------------------------------------------------------------- */


/**
 * forte_dsp_ioctl:
 */

static int
forte_dsp_ioctl (struct inode *inode, struct file *file, unsigned int cmd,
		 unsigned long arg)
{
	int ival=0, ret, rval=0, rd, wr, count;
	struct forte_chip *chip;
	struct audio_buf_info abi;
	struct count_info cinfo;
	void __user *argp = (void __user *)arg;
	int __user *p = argp;

	chip = file->private_data;
	
	if (file->f_mode & FMODE_WRITE)
		wr = 1;
	else 
		wr = 0;

	if (file->f_mode & FMODE_READ)
		rd = 1;
	else
		rd = 0;

	switch (cmd) {

	case OSS_GETVERSION:
		return put_user (SOUND_VERSION, p);

	case SNDCTL_DSP_GETCAPS:
		DPRINTK ("%s: GETCAPS\n", __FUNCTION__);

		ival = FORTE_CAPS; /* DUPLEX */
		return put_user (ival, p);

	case SNDCTL_DSP_GETFMTS:
		DPRINTK ("%s: GETFMTS\n", __FUNCTION__);

		ival = FORTE_FMTS; /* U8, 16LE */
		return put_user (ival, p);

	case SNDCTL_DSP_SETFMT:	/* U8, 16LE */
		DPRINTK ("%s: SETFMT\n", __FUNCTION__);

		if (get_user (ival, p))
			return -EFAULT;

		spin_lock_irq (&chip->lock);

		if (rd) {
			forte_channel_stop (&chip->rec);
			rval = forte_channel_format (&chip->rec, ival);
		}

		if (wr) {
			forte_channel_stop (&chip->rec);
			rval = forte_channel_format (&chip->play, ival);
		}

		spin_unlock_irq (&chip->lock);
	
		return put_user (rval, p);

	case SNDCTL_DSP_STEREO:	/* 0 - mono, 1 - stereo */
		DPRINTK ("%s: STEREO\n", __FUNCTION__);

		if (get_user (ival, p))
			return -EFAULT;

		spin_lock_irq (&chip->lock);

		if (rd) {
			forte_channel_stop (&chip->rec);
			rval = forte_channel_stereo (&chip->rec, ival);
		}

		if (wr) {
			forte_channel_stop (&chip->rec);
			rval = forte_channel_stereo (&chip->play, ival);
		}

		spin_unlock_irq (&chip->lock);

                return put_user (rval, p);

	case SNDCTL_DSP_CHANNELS: /* 1 - mono, 2 - stereo */
		DPRINTK ("%s: CHANNELS\n", __FUNCTION__);

		if (get_user (ival, p))
			return -EFAULT;

		spin_lock_irq (&chip->lock);

		if (rd) {
			forte_channel_stop (&chip->rec);
			rval = forte_channel_stereo (&chip->rec, ival-1) + 1;
		}

		if (wr) {
			forte_channel_stop (&chip->play);
			rval = forte_channel_stereo (&chip->play, ival-1) + 1;
		}

		spin_unlock_irq (&chip->lock);

                return put_user (rval, p);

	case SNDCTL_DSP_SPEED:
		DPRINTK ("%s: SPEED\n", __FUNCTION__);

		if (get_user (ival, p))
                        return -EFAULT;

		spin_lock_irq (&chip->lock);

		if (rd) {
			forte_channel_stop (&chip->rec);
			rval = forte_channel_rate (&chip->rec, ival);
		}

		if (wr) {
			forte_channel_stop (&chip->play);
			rval = forte_channel_rate (&chip->play, ival);
		}

		spin_unlock_irq (&chip->lock);

                return put_user(rval, p);

	case SNDCTL_DSP_GETBLKSIZE:
		DPRINTK ("%s: GETBLKSIZE\n", __FUNCTION__);

		spin_lock_irq (&chip->lock);

		if (rd)
			ival = chip->rec.frag_sz;

		if (wr)
			ival = chip->play.frag_sz;

		spin_unlock_irq (&chip->lock);

                return put_user (ival, p);

	case SNDCTL_DSP_RESET:
		DPRINTK ("%s: RESET\n", __FUNCTION__);

		spin_lock_irq (&chip->lock);

		if (rd)
			forte_channel_reset (&chip->rec);

		if (wr)
			forte_channel_reset (&chip->play);

		spin_unlock_irq (&chip->lock);

                return 0;

	case SNDCTL_DSP_SYNC:
		DPRINTK ("%s: SYNC\n", __FUNCTION__);

		if (wr)
			ret = forte_channel_drain (&chip->play);

		return 0;

	case SNDCTL_DSP_POST:
		DPRINTK ("%s: POST\n", __FUNCTION__);

		if (wr) {
			spin_lock_irq (&chip->lock);

			if (chip->play.filled_frags)
				forte_channel_start (&chip->play);

			spin_unlock_irq (&chip->lock);
		}

                return 0;

	case SNDCTL_DSP_SETFRAGMENT:
		DPRINTK ("%s: SETFRAGMENT\n", __FUNCTION__);

		if (get_user (ival, p))
			return -EFAULT;

		spin_lock_irq (&chip->lock);

		if (rd) {
			forte_channel_buffer (&chip->rec, ival & 0xffff, 
					      (ival >> 16) & 0xffff);
			ival = (chip->rec.frag_num << 16) + chip->rec.frag_sz;
		}

		if (wr) {
			forte_channel_buffer (&chip->play, ival & 0xffff, 
					      (ival >> 16) & 0xffff);
			ival = (chip->play.frag_num << 16) +chip->play.frag_sz;
		}

		spin_unlock_irq (&chip->lock);

		return put_user (ival, p);
                
        case SNDCTL_DSP_GETISPACE:
		DPRINTK ("%s: GETISPACE\n", __FUNCTION__);

		if (!rd)
			return -EINVAL;

		spin_lock_irq (&chip->lock);

		abi.fragstotal = chip->rec.frag_num;
		abi.fragsize = chip->rec.frag_sz;
			
		if (chip->rec.mapped) {
			abi.fragments = chip->rec.frag_num - 2;
			abi.bytes = abi.fragments * abi.fragsize;
		}
		else {
			abi.fragments = chip->rec.filled_frags;
			abi.bytes = abi.fragments * abi.fragsize;
		}

		spin_unlock_irq (&chip->lock);

		return copy_to_user (argp, &abi, sizeof (abi)) ? -EFAULT : 0;

	case SNDCTL_DSP_GETIPTR:
		DPRINTK ("%s: GETIPTR\n", __FUNCTION__);

		if (!rd)
			return -EINVAL;

		spin_lock_irq (&chip->lock);

		if (chip->rec.active) 
			cinfo.ptr = chip->rec.hwptr;
		else
			cinfo.ptr = 0;

		cinfo.bytes = chip->rec.bytes;
		cinfo.blocks = chip->rec.nr_irqs;
		chip->rec.nr_irqs = 0;

		spin_unlock_irq (&chip->lock);

		return copy_to_user (argp, &cinfo, sizeof (cinfo)) ? -EFAULT : 0;

        case SNDCTL_DSP_GETOSPACE:
		if (!wr)
			return -EINVAL;
		
		spin_lock_irq (&chip->lock);

		abi.fragstotal = chip->play.frag_num;
		abi.fragsize = chip->play.frag_sz;

		if (chip->play.mapped) {
			abi.fragments = chip->play.frag_num - 2;
			abi.bytes = chip->play.buf_sz;
		}
		else {
			abi.fragments = chip->play.frag_num - 
				chip->play.filled_frags;

			if (chip->play.residue)
				abi.fragments--;

			abi.bytes = abi.fragments * abi.fragsize +
				chip->play.residue;
		}

		spin_unlock_irq (&chip->lock);
		
		return copy_to_user (argp, &abi, sizeof (abi)) ? -EFAULT : 0;

	case SNDCTL_DSP_GETOPTR:
		if (!wr)
			return -EINVAL;

		spin_lock_irq (&chip->lock);

		if (chip->play.active) 
			cinfo.ptr = chip->play.hwptr;
		else
			cinfo.ptr = 0;

		cinfo.bytes = chip->play.bytes;
		cinfo.blocks = chip->play.nr_irqs;
		chip->play.nr_irqs = 0;

		spin_unlock_irq (&chip->lock);

		return copy_to_user (argp, &cinfo, sizeof (cinfo)) ? -EFAULT : 0;

	case SNDCTL_DSP_GETODELAY:
		if (!wr)
			return -EINVAL;

		spin_lock_irq (&chip->lock);

		if (!chip->play.active) {
			ival = 0;
		}
		else if (chip->play.mapped) {
			count = inw (chip->play.iobase + FORTE_PLY_COUNT) + 1;
			ival = chip->play.frag_sz - count;
		}
		else {
			ival = chip->play.filled_frags * chip->play.frag_sz;

			if (chip->play.residue)
				ival += chip->play.frag_sz - chip->play.residue;
		}

		spin_unlock_irq (&chip->lock);

		return put_user (ival, p);

	case SNDCTL_DSP_SETDUPLEX:
		DPRINTK ("%s: SETDUPLEX\n", __FUNCTION__);

		return -EINVAL;

	case SNDCTL_DSP_GETTRIGGER:
		DPRINTK ("%s: GETTRIGGER\n", __FUNCTION__);
		
		return put_user (chip->trigger, p);
		
	case SNDCTL_DSP_SETTRIGGER:

		if (get_user (ival, p))
			return -EFAULT;

		DPRINTK ("%s: SETTRIGGER %d\n", __FUNCTION__, ival);

		if (wr) {
			spin_lock_irq (&chip->lock);

			if (ival & PCM_ENABLE_OUTPUT)
				forte_channel_start (&chip->play);
			else {		
				chip->trigger = 1;
				forte_channel_prep (&chip->play);
				forte_channel_stop (&chip->play);
			}

			spin_unlock_irq (&chip->lock);
		}
		else if (rd) {
			spin_lock_irq (&chip->lock);

			if (ival & PCM_ENABLE_INPUT)
				forte_channel_start (&chip->rec);
			else {		
				chip->trigger = 1;
				forte_channel_prep (&chip->rec);
				forte_channel_stop (&chip->rec);
			}

			spin_unlock_irq (&chip->lock);
		}

		return 0;
		
	case SOUND_PCM_READ_RATE:
		DPRINTK ("%s: PCM_READ_RATE\n", __FUNCTION__);		
		return put_user (chip->play.rate, p);

	case SOUND_PCM_READ_CHANNELS:
		DPRINTK ("%s: PCM_READ_CHANNELS\n", __FUNCTION__);
		return put_user (chip->play.stereo, p);

	case SOUND_PCM_READ_BITS:
		DPRINTK ("%s: PCM_READ_BITS\n", __FUNCTION__);		
		return put_user (chip->play.format, p);

	case SNDCTL_DSP_NONBLOCK:
		DPRINTK ("%s: DSP_NONBLOCK\n", __FUNCTION__);		
                file->f_flags |= O_NONBLOCK;
		return 0;

	default:
		DPRINTK ("Unsupported ioctl: %x (%p)\n", cmd, argp);
		break;
	}

	return -EINVAL;
}


/**
 * forte_dsp_open:
 */

static int 
forte_dsp_open (struct inode *inode, struct file *file)
{
	struct forte_chip *chip = forte; /* FIXME: HACK FROM HELL! */

	if (file->f_flags & O_NONBLOCK) {
		if (down_trylock (&chip->open_sem)) {
			DPRINTK ("%s: returning -EAGAIN\n", __FUNCTION__);
			return -EAGAIN;
		}
	}
	else {
		if (down_interruptible (&chip->open_sem)) {
			DPRINTK ("%s: returning -ERESTARTSYS\n", __FUNCTION__);
			return -ERESTARTSYS;
		}
	}

	file->private_data = forte;

	DPRINTK ("%s: dsp opened by %d\n", __FUNCTION__, current->pid);

	if (file->f_mode & FMODE_WRITE)
		forte_channel_init (forte, &forte->play);

	if (file->f_mode & FMODE_READ)
		forte_channel_init (forte, &forte->rec);

	return nonseekable_open(inode, file);
}


/**
 * forte_dsp_release:
 */

static int 
forte_dsp_release (struct inode *inode, struct file *file)
{
	struct forte_chip *chip = file->private_data;
	int ret = 0;

	DPRINTK ("%s: chip @ %p\n", __FUNCTION__, chip);

	if (file->f_mode & FMODE_WRITE) {
		forte_channel_drain (&chip->play);

		spin_lock_irq (&chip->lock);

 		forte_channel_free (chip, &chip->play);

		spin_unlock_irq (&chip->lock);
        }

	if (file->f_mode & FMODE_READ) {
		while (chip->rec.filled_frags > 0)
			interruptible_sleep_on (&chip->rec.wait);

		spin_lock_irq (&chip->lock);

		forte_channel_stop (&chip->rec);
		forte_channel_free (chip, &chip->rec);

		spin_unlock_irq (&chip->lock);
	}

	up (&chip->open_sem);

	return ret;
}


/**
 * forte_dsp_poll:
 *
 */

static unsigned int 
forte_dsp_poll (struct file *file, struct poll_table_struct *wait)
{
	struct forte_chip *chip;
	struct forte_channel *channel;
	unsigned int mask = 0;

	chip = file->private_data;

	if (file->f_mode & FMODE_WRITE) {
		channel = &chip->play;

		if (channel->active)
			poll_wait (file, &channel->wait, wait);

		spin_lock_irq (&chip->lock);

		if (channel->frag_num - channel->filled_frags > 0)
			mask |= POLLOUT | POLLWRNORM;

		spin_unlock_irq (&chip->lock);
	}

	if (file->f_mode & FMODE_READ) {
		channel = &chip->rec;

		if (channel->active)
			poll_wait (file, &channel->wait, wait);

		spin_lock_irq (&chip->lock);

		if (channel->filled_frags > 0)
			mask |= POLLIN | POLLRDNORM;

		spin_unlock_irq (&chip->lock);
	}

	return mask;
}


/**
 * forte_dsp_mmap:
 */

static int
forte_dsp_mmap (struct file *file, struct vm_area_struct *vma)
{
	struct forte_chip *chip;
	struct forte_channel *channel;
	unsigned long size;
	int ret;

	chip = file->private_data;

	DPRINTK ("%s: start %lXh, size %ld, pgoff %ld\n", __FUNCTION__,
                 vma->vm_start, vma->vm_end - vma->vm_start, vma->vm_pgoff);

	spin_lock_irq (&chip->lock);

	if (vma->vm_flags & VM_WRITE && chip->play.active) {
		ret = -EBUSY;
		goto out;
	}

        if (vma->vm_flags & VM_READ && chip->rec.active) {
		ret = -EBUSY;
		goto out;
        }

	if (file->f_mode & FMODE_WRITE)
		channel = &chip->play;
	else if (file->f_mode & FMODE_READ)
		channel = &chip->rec;
	else {
		ret = -EINVAL;
		goto out;
	}

	forte_channel_prep (channel);
	channel->mapped = 1;

        if (vma->vm_pgoff != 0) {
		ret = -EINVAL;
                goto out;
	}

        size = vma->vm_end - vma->vm_start;

        if (size > channel->buf_pages * PAGE_SIZE) {
		DPRINTK ("%s: size (%ld) > buf_sz (%d) \n", __FUNCTION__,
			 size, channel->buf_sz);
		ret = -EINVAL;
                goto out;
	}

        if (remap_pfn_range(vma, vma->vm_start,
			      virt_to_phys(channel->buf) >> PAGE_SHIFT,
			      size, vma->vm_page_prot)) {
		DPRINTK ("%s: remap el a no worko\n", __FUNCTION__);
		ret = -EAGAIN;
                goto out;
	}

        ret = 0;

 out:
	spin_unlock_irq (&chip->lock);
        return ret;
}


/**
 * forte_dsp_write:
 */

static ssize_t 
forte_dsp_write (struct file *file, const char __user *buffer, size_t bytes, 
		 loff_t *ppos)
{
	struct forte_chip *chip;
	struct forte_channel *channel;
	unsigned int i = bytes, sz = 0;
	unsigned long flags;

	if (!access_ok (VERIFY_READ, buffer, bytes))
		return -EFAULT;

	chip = (struct forte_chip *) file->private_data;

	if (!chip)
		BUG();

	channel = &chip->play;

	if (!channel)
		BUG();

	spin_lock_irqsave (&chip->lock, flags);

	/* Set up buffers with the right fragment size */
	forte_channel_prep (channel);

	while (i) {
		/* All fragment buffers in use -> wait */
		if (channel->frag_num - channel->filled_frags == 0) {
			DECLARE_WAITQUEUE (wait, current);

			/* For trigger or non-blocking operation, get out */
			if (chip->trigger || file->f_flags & O_NONBLOCK) {
				spin_unlock_irqrestore (&chip->lock, flags);
				return -EAGAIN;
			}

			/* Otherwise wait for buffers */
			add_wait_queue (&channel->wait, &wait);

			for (;;) {
				spin_unlock_irqrestore (&chip->lock, flags);

				set_current_state (TASK_INTERRUPTIBLE);
				schedule();

				spin_lock_irqsave (&chip->lock, flags);

				if (channel->frag_num - channel->filled_frags)
					break;
			}

			remove_wait_queue (&channel->wait, &wait);
			set_current_state (TASK_RUNNING);

			if (signal_pending (current)) {
				spin_unlock_irqrestore (&chip->lock, flags);
				return -ERESTARTSYS;
			}
		}

		if (channel->residue)
			sz = channel->residue;
		else if (i > channel->frag_sz)
			sz = channel->frag_sz;
		else
			sz = i;

		spin_unlock_irqrestore (&chip->lock, flags);

		if (copy_from_user ((void *) channel->buf + channel->swptr, buffer, sz))
			return -EFAULT;

		spin_lock_irqsave (&chip->lock, flags);

		/* Advance software pointer */
		buffer += sz;
		channel->swptr += sz;
		channel->swptr %= channel->buf_sz;
		i -= sz;

		/* Only bump filled_frags if a full fragment has been written */
		if (channel->swptr % channel->frag_sz == 0) {
			channel->filled_frags++;
			channel->residue = 0;
		}
		else
			channel->residue = channel->frag_sz - sz;

		/* If playback isn't active, start it */
		if (channel->active == 0 && chip->trigger == 0)
			forte_channel_start (channel);
	}

	spin_unlock_irqrestore (&chip->lock, flags);

	return bytes - i;
}


/**
 * forte_dsp_read:
 */

static ssize_t 
forte_dsp_read (struct file *file, char __user *buffer, size_t bytes, 
		loff_t *ppos)
{
	struct forte_chip *chip;
	struct forte_channel *channel;
	unsigned int i = bytes, sz;
	unsigned long flags;

	if (!access_ok (VERIFY_WRITE, buffer, bytes))
		return -EFAULT;

	chip = (struct forte_chip *) file->private_data;

	if (!chip)
		BUG();

	channel = &chip->rec;

	if (!channel)
		BUG();

	spin_lock_irqsave (&chip->lock, flags);

	/* Set up buffers with the right fragment size */
	forte_channel_prep (channel);

	/* Start recording */
	if (!chip->trigger)
		forte_channel_start (channel);

	while (i) {
		/* No fragment buffers in use -> wait */
		if (channel->filled_frags == 0) {
			DECLARE_WAITQUEUE (wait, current);

			/* For trigger mode operation, get out */
			if (chip->trigger) {
				spin_unlock_irqrestore (&chip->lock, flags);
				return -EAGAIN;
			}

			add_wait_queue (&channel->wait, &wait);

			for (;;) {
				if (channel->active == 0)
					break;

				if (channel->filled_frags)
					break;
						
				spin_unlock_irqrestore (&chip->lock, flags);

				set_current_state (TASK_INTERRUPTIBLE);
				schedule();

				spin_lock_irqsave (&chip->lock, flags);
			}

			set_current_state (TASK_RUNNING);
			remove_wait_queue (&channel->wait, &wait);
		}

		if (i > channel->frag_sz)
			sz = channel->frag_sz;
		else
			sz = i;

		spin_unlock_irqrestore (&chip->lock, flags);

		if (copy_to_user (buffer, (void *)channel->buf+channel->swptr, sz)) {
			DPRINTK ("%s: copy_to_user failed\n", __FUNCTION__);
			return -EFAULT;
		}

		spin_lock_irqsave (&chip->lock, flags);

		/* Advance software pointer */
		buffer += sz;
		if (channel->filled_frags > 0)
			channel->filled_frags--;
		channel->swptr += channel->frag_sz;
		channel->swptr %= channel->buf_sz;
		i -= sz;
	}

	spin_unlock_irqrestore (&chip->lock, flags);

	return bytes - i;
}


static struct file_operations forte_dsp_fops = {
	.owner			= THIS_MODULE,
	.llseek     		= &no_llseek,
	.read       		= &forte_dsp_read,
	.write      		= &forte_dsp_write,
	.poll       		= &forte_dsp_poll,
	.ioctl      		= &forte_dsp_ioctl,
	.open       		= &forte_dsp_open,
	.release    		= &forte_dsp_release,
	.mmap			= &forte_dsp_mmap,
};


/* Common ------------------------------------------------------------------ */


/**
 * forte_interrupt:
 */

static irqreturn_t
forte_interrupt (int irq, void *dev_id, struct pt_regs *regs)
{
	struct forte_chip *chip = dev_id;
	struct forte_channel *channel = NULL;
	u16 status, count; 

	status = inw (chip->iobase + FORTE_IRQ_STATUS);

	/* If this is not for us, get outta here ASAP */
	if ((status & (FORTE_IRQ_PLAYBACK | FORTE_IRQ_CAPTURE)) == 0)
		return IRQ_NONE;
	
	if (status & FORTE_IRQ_PLAYBACK) {
		channel = &chip->play;

		spin_lock (&chip->lock);

		if (channel->frag_sz == 0)
			goto pack;

		/* Declare a fragment done */
		if (channel->filled_frags > 0)
			channel->filled_frags--;
		channel->bytes += channel->frag_sz;
		channel->nr_irqs++;
		
		/* Flip-flop between buffer I and II */
		channel->next_buf ^= 1;

		/* Advance hardware pointer by fragment size and wrap around */
		channel->hwptr += channel->frag_sz;
		channel->hwptr %= channel->buf_sz;

		/* Buffer I or buffer II BAR */
                outl (channel->buf_handle + channel->hwptr, 
		      channel->next_buf == 0 ?
		      channel->iobase + FORTE_PLY_BUF1 :
		      channel->iobase + FORTE_PLY_BUF2);

		/* If the currently playing fragment is last, schedule pause */
		if (channel->filled_frags == 1) 
			forte_channel_pause (channel);

	pack:
		/* Acknowledge interrupt */
                outw (FORTE_IRQ_PLAYBACK, chip->iobase + FORTE_IRQ_STATUS);

		if (waitqueue_active (&channel->wait)) 
			wake_up_all (&channel->wait);

		spin_unlock (&chip->lock);
	}

	if (status & FORTE_IRQ_CAPTURE) {
		channel = &chip->rec;
		spin_lock (&chip->lock);

		/* One fragment filled */
		channel->filled_frags++;

		/* Get # of completed bytes */
		count = inw (channel->iobase + FORTE_PLY_COUNT) + 1;

		if (count == 0) {
			DPRINTK ("%s: last, filled_frags = %d\n", __FUNCTION__,
				 channel->filled_frags);
			channel->filled_frags = 0;
			goto rack;
		}

		/* Buffer I or buffer II BAR */
                outl (channel->buf_handle + channel->hwptr, 
		      channel->next_buf == 0 ?
		      channel->iobase + FORTE_PLY_BUF1 :
		      channel->iobase + FORTE_PLY_BUF2);

		/* Flip-flop between buffer I and II */
		channel->next_buf ^= 1;

		/* Advance hardware pointer by fragment size and wrap around */
		channel->hwptr += channel->frag_sz;
		channel->hwptr %= channel->buf_sz;

		/* Out of buffers */
		if (channel->filled_frags == channel->frag_num - 1)
			forte_channel_stop (channel);
	rack:
		/* Acknowledge interrupt */
                outw (FORTE_IRQ_CAPTURE, chip->iobase + FORTE_IRQ_STATUS);

		spin_unlock (&chip->lock);

		if (waitqueue_active (&channel->wait))
			wake_up_all (&channel->wait);		
	}

	return IRQ_HANDLED;
}


/**
 * forte_proc_read:
 */

static int
forte_proc_read (char *page, char **start, off_t off, int count, 
		 int *eof, void *data)
{
	int i = 0, p_rate, p_chan, r_rate;
	unsigned short p_reg, r_reg;

	i += sprintf (page, "ForteMedia FM801 OSS Lite driver\n%s\n \n", 
		      DRIVER_VERSION);

	if (!forte->iobase)
		return i;

	p_rate = p_chan = -1;
	p_reg  = inw (forte->iobase + FORTE_PLY_CTRL);
	p_rate = (p_reg >> 8) & 15;
	p_chan = (p_reg >> 12) & 3;

 	if (p_rate >= 0 || p_rate <= 10)
		p_rate = rates[p_rate];

	if (p_chan >= 0 || p_chan <= 2)
		p_chan = channels[p_chan];

	r_rate = -1;
	r_reg  = inw (forte->iobase + FORTE_CAP_CTRL);
	r_rate = (r_reg >> 8) & 15;

 	if (r_rate >= 0 || r_rate <= 10)
		r_rate = rates[r_rate]; 

	i += sprintf (page + i,
		      "             Playback  Capture\n"
		      "FIFO empty : %-3s       %-3s\n"
		      "Buf1 Last  : %-3s       %-3s\n"
		      "Buf2 Last  : %-3s       %-3s\n"
		      "Started    : %-3s       %-3s\n"
		      "Paused     : %-3s       %-3s\n"
		      "Immed Stop : %-3s       %-3s\n"
		      "Rate       : %-5d     %-5d\n"
		      "Channels   : %-5d     -\n"
		      "16-bit     : %-3s       %-3s\n"
		      "Stereo     : %-3s       %-3s\n"
		      " \n"
		      "Buffer Sz  : %-6d    %-6d\n"
		      "Frag Sz    : %-6d    %-6d\n"
		      "Frag Num   : %-6d    %-6d\n"
		      "Frag msecs : %-6d    %-6d\n"
		      "Used Frags : %-6d    %-6d\n"
		      "Mapped     : %-3s       %-3s\n",
		      p_reg & 1<<0  ? "yes" : "no",
		      r_reg & 1<<0  ? "yes" : "no",
		      p_reg & 1<<1  ? "yes" : "no",
		      r_reg & 1<<1  ? "yes" : "no",
		      p_reg & 1<<2  ? "yes" : "no",
		      r_reg & 1<<2  ? "yes" : "no",
		      p_reg & 1<<5  ? "yes" : "no",
		      r_reg & 1<<5  ? "yes" : "no",
		      p_reg & 1<<6  ? "yes" : "no",
		      r_reg & 1<<6  ? "yes" : "no",
		      p_reg & 1<<7  ? "yes" : "no",
		      r_reg & 1<<7  ? "yes" : "no",
		      p_rate, r_rate,
		      p_chan,
		      p_reg & 1<<14 ? "yes" : "no",
		      r_reg & 1<<14 ? "yes" : "no",
		      p_reg & 1<<15 ? "yes" : "no",
		      r_reg & 1<<15 ? "yes" : "no",
		      forte->play.buf_sz,       forte->rec.buf_sz,
		      forte->play.frag_sz,      forte->rec.frag_sz,
		      forte->play.frag_num,     forte->rec.frag_num,
		      forte->play.frag_msecs,   forte->rec.frag_msecs,
		      forte->play.filled_frags, forte->rec.filled_frags,
		      forte->play.mapped ? "yes" : "no",
		      forte->rec.mapped ? "yes" : "no"
		);

	return i;
}


/**
 * forte_proc_init:
 *
 * Creates driver info entries in /proc
 */

static int __init 
forte_proc_init (void)
{
	if (!proc_mkdir ("driver/forte", NULL))
		return -EIO;

	if (!create_proc_read_entry ("driver/forte/chip", 0, NULL, forte_proc_read, forte)) {
		remove_proc_entry ("driver/forte", NULL);
		return -EIO;
	}

	if (!create_proc_read_entry("driver/forte/ac97", 0, NULL, ac97_read_proc, forte->ac97)) {
		remove_proc_entry ("driver/forte/chip", NULL);
		remove_proc_entry ("driver/forte", NULL);
		return -EIO;
	}

	return 0;
}


/**
 * forte_proc_remove:
 *
 * Removes driver info entries in /proc
 */

static void
forte_proc_remove (void)
{
	remove_proc_entry ("driver/forte/ac97", NULL);
	remove_proc_entry ("driver/forte/chip", NULL);
	remove_proc_entry ("driver/forte", NULL);	
}


/**
 * forte_chip_init:
 * @chip:	Chip instance to initialize
 *
 * Description:
 * 		Resets chip, configures codec and registers the driver with
 * 		the sound subsystem.
 *
 * 		Press and hold Start for 8 secs, then switch on Run
 * 		and hold for 4 seconds.  Let go of Start.  Numbers
 * 		assume a properly oiled TWG.
 */

static int __devinit
forte_chip_init (struct forte_chip *chip)
{
	u8 revision;
	u16 cmdw;
	struct ac97_codec *codec;

	pci_read_config_byte (chip->pci_dev, PCI_REVISION_ID, &revision);

	if (revision >= 0xB1) {
		chip->multichannel = 1;
		printk (KERN_INFO PFX "Multi-channel device detected.\n");
	}

	/* Reset chip */
	outw (FORTE_CC_CODEC_RESET | FORTE_CC_AC97_RESET, 
	      chip->iobase + FORTE_CODEC_CTRL);
	udelay(100);
	outw (0, chip->iobase + FORTE_CODEC_CTRL);

	/* Request read from AC97 */
	outw (FORTE_AC97_READ | (0 << FORTE_AC97_ADDR_SHIFT), 
	      chip->iobase + FORTE_AC97_CMD);
	mdelay(750);

	if ((inw (chip->iobase + FORTE_AC97_CMD) & (3<<8)) != (1<<8)) {
		printk (KERN_INFO PFX "AC97 codec not responding");
		return -EIO;
	}

	/* Init volume */
	outw (0x0808, chip->iobase + FORTE_PCM_VOL);
	outw (0x9f1f, chip->iobase + FORTE_FM_VOL);
	outw (0x8808, chip->iobase + FORTE_I2S_VOL);

	/* I2S control - I2S mode */
	outw (0x0003, chip->iobase + FORTE_I2S_MODE);

	/* Interrupt setup - unmask PLAYBACK & CAPTURE */
	cmdw = inw (chip->iobase + FORTE_IRQ_MASK);
	cmdw &= ~0x0003;
	outw (cmdw, chip->iobase + FORTE_IRQ_MASK);

	/* Interrupt clear */
	outw (FORTE_IRQ_PLAYBACK|FORTE_IRQ_CAPTURE, 
	      chip->iobase + FORTE_IRQ_STATUS);

	/* Set up the AC97 codec */
	if ((codec = ac97_alloc_codec()) == NULL)
		return -ENOMEM;
	codec->private_data = chip;
	codec->codec_read = forte_ac97_read;
	codec->codec_write = forte_ac97_write;
	codec->id = 0;

	if (ac97_probe_codec (codec) == 0) {
		printk (KERN_ERR PFX "codec probe failed\n");
		ac97_release_codec(codec);
		return -1;
	}

	/* Register mixer */
	if ((codec->dev_mixer = 
	     register_sound_mixer (&forte_mixer_fops, -1)) < 0) {
		printk (KERN_ERR PFX "couldn't register mixer!\n");
		ac97_release_codec(codec);
		return -1;
	}

	chip->ac97 = codec;

	/* Register DSP */
	if ((chip->dsp = register_sound_dsp (&forte_dsp_fops, -1) ) < 0) {
		printk (KERN_ERR PFX "couldn't register dsp!\n");
		return -1;
	}

	/* Register with /proc */
	if (forte_proc_init()) {
		printk (KERN_ERR PFX "couldn't add entries to /proc!\n");
		return -1;
	}

	return 0;
}


/**
 * forte_probe:
 * @pci_dev:	PCI struct for probed device
 * @pci_id:	
 *
 * Description:
 *		Allocates chip instance, I/O region, and IRQ
 */
static int __init 
forte_probe (struct pci_dev *pci_dev, const struct pci_device_id *pci_id)
{
	struct forte_chip *chip;
	int ret = 0;

	/* FIXME: Support more than one chip */
	if (found++)
		return -EIO;

	/* Ignition */
	if (pci_enable_device (pci_dev))
		return -EIO;

	pci_set_master (pci_dev);

	/* Allocate chip instance and configure */
	forte = (struct forte_chip *) 
		kmalloc (sizeof (struct forte_chip), GFP_KERNEL);
	chip = forte;

	if (chip == NULL) {
		printk (KERN_WARNING PFX "Out of memory");
		return -ENOMEM;
	}

	memset (chip, 0, sizeof (struct forte_chip));
	chip->pci_dev = pci_dev;

	init_MUTEX(&chip->open_sem);
	spin_lock_init (&chip->lock);
	spin_lock_init (&chip->ac97_lock);

	if (! request_region (pci_resource_start (pci_dev, 0),
			      pci_resource_len (pci_dev, 0), DRIVER_NAME)) {
		printk (KERN_WARNING PFX "Unable to reserve I/O space");
		ret = -ENOMEM;
		goto error;
	}

	chip->iobase = pci_resource_start (pci_dev, 0);
	chip->irq = pci_dev->irq;

	if (request_irq (chip->irq, forte_interrupt, SA_SHIRQ, DRIVER_NAME,
			 chip)) {
		printk (KERN_WARNING PFX "Unable to reserve IRQ");
		ret = -EIO;
		goto error;
	}		
	
	pci_set_drvdata (pci_dev, chip);

	printk (KERN_INFO PFX "FM801 chip found at 0x%04lX-0x%04lX IRQ %u\n", 
		chip->iobase, pci_resource_end (pci_dev, 0), chip->irq);

	/* Power it up */
	if ((ret = forte_chip_init (chip)) == 0)
		return 0;

 error:
	if (chip->irq)
		free_irq (chip->irq, chip);

	if (chip->iobase) 
		release_region (pci_resource_start (pci_dev, 0),
				pci_resource_len (pci_dev, 0));
		
	kfree (chip);

	return ret;
}


/**
 * forte_remove:
 * @pci_dev:	PCI device to unclaim
 *
 */

static void 
forte_remove (struct pci_dev *pci_dev)
{
	struct forte_chip *chip = pci_get_drvdata (pci_dev);

	if (chip == NULL)
		return;

	/* Turn volume down to avoid popping */
	outw (0x1f1f, chip->iobase + FORTE_PCM_VOL);
	outw (0x1f1f, chip->iobase + FORTE_FM_VOL);
	outw (0x1f1f, chip->iobase + FORTE_I2S_VOL);

	forte_proc_remove();
	free_irq (chip->irq, chip);
	release_region (chip->iobase, pci_resource_len (pci_dev, 0));

	unregister_sound_dsp (chip->dsp);
	unregister_sound_mixer (chip->ac97->dev_mixer);
	ac97_release_codec(chip->ac97);
	kfree (chip);

	printk (KERN_INFO PFX "driver released\n");
}


static struct pci_device_id forte_pci_ids[] = {
	{ 0x1319, 0x0801, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, },
	{ 0, }
};


static struct pci_driver forte_pci_driver = {
	.name			= DRIVER_NAME,
	.id_table		= forte_pci_ids,
	.probe	 		= forte_probe,
	.remove			= forte_remove,

};


/**
 * forte_init_module:
 *
 */

static int __init
forte_init_module (void)
{
	printk (KERN_INFO PFX DRIVER_VERSION "\n");

	return pci_register_driver (&forte_pci_driver);
}


/**
 * forte_cleanup_module:
 *
 */

static void __exit 
forte_cleanup_module (void)
{
	pci_unregister_driver (&forte_pci_driver);
}


module_init(forte_init_module);
module_exit(forte_cleanup_module);

MODULE_AUTHOR("Martin K. Petersen <mkp@mkp.net>");
MODULE_DESCRIPTION("ForteMedia FM801 OSS Driver");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE (pci, forte_pci_ids);
