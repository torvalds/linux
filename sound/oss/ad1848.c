/*
 * sound/oss/ad1848.c
 *
 * The low level driver for the AD1848/CS4248 codec chip which
 * is used for example in the MS Sound System.
 *
 * The CS4231 which is used in the GUS MAX and some other cards is
 * upwards compatible with AD1848 and this driver is able to drive it.
 *
 * CS4231A and AD1845 are upward compatible with CS4231. However
 * the new features of these chips are different.
 *
 * CS4232 is a PnP audio chip which contains a CS4231A (and SB, MPU).
 * CS4232A is an improved version of CS4232.
 *
 *
 *
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 * OSS/Free for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 *
 *
 * Thomas Sailer	: ioctl code reworked (vmalloc/vfree removed)
 *			  general sleep/wakeup clean up.
 * Alan Cox		: reformatted. Fixed SMP bugs. Moved to kernel alloc/free
 *		          of irqs. Use dev_id.
 * Christoph Hellwig	: adapted to module_init/module_exit
 * Aki Laukkanen	: added power management support
 * Arnaldo C. de Melo	: added missing restore_flags in ad1848_resume
 * Miguel Freitas       : added ISA PnP support
 * Alan Cox		: Added CS4236->4239 identification
 * Daniel T. Cobra	: Alernate config/mixer for later chips
 * Alan Cox		: Merged chip idents and config code
 *
 * TODO
 *		APM save restore assist code on IBM thinkpad
 *
 * Status:
 *		Tested. Believed fully functional.
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/stddef.h>
#include <linux/isapnp.h>
#include <linux/pnp.h>
#include <linux/spinlock.h>

#define DEB(x)
#define DEB1(x)
#include "sound_config.h"

#include "ad1848.h"
#include "ad1848_mixer.h"

typedef struct
{
	spinlock_t	lock;
	int             base;
	int             irq;
	int             dma1, dma2;
	int             dual_dma;	/* 1, when two DMA channels allocated */
	int 		subtype;
	unsigned char   MCE_bit;
	unsigned char   saved_regs[64];	/* Includes extended register space */
	int             debug_flag;

	int             audio_flags;
	int             record_dev, playback_dev;

	int             xfer_count;
	int             audio_mode;
	int             open_mode;
	int             intr_active;
	char           *chip_name, *name;
	int             model;
#define MD_1848		1
#define MD_4231		2
#define MD_4231A	3
#define MD_1845		4
#define MD_4232		5
#define MD_C930		6
#define MD_IWAVE	7
#define MD_4235         8 /* Crystal Audio CS4235  */
#define MD_1845_SSCAPE  9 /* Ensoniq Soundscape PNP*/
#define MD_4236		10 /* 4236 and higher */
#define MD_42xB		11 /* CS 42xB */
#define MD_4239		12 /* CS4239 */

	/* Mixer parameters */
	int             recmask;
	int             supported_devices, orig_devices;
	int             supported_rec_devices, orig_rec_devices;
	int            *levels;
	short           mixer_reroute[32];
	int             dev_no;
	volatile unsigned long timer_ticks;
	int             timer_running;
	int             irq_ok;
	mixer_ents     *mix_devices;
	int             mixer_output_port;
} ad1848_info;

typedef struct ad1848_port_info
{
	int             open_mode;
	int             speed;
	unsigned char   speed_bits;
	int             channels;
	int             audio_format;
	unsigned char   format_bits;
}
ad1848_port_info;

static struct address_info cfg;
static int nr_ad1848_devs;

static int deskpro_xl;
static int deskpro_m;
static int soundpro;

static volatile signed char irq2dev[17] = {
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1
};

#ifndef EXCLUDE_TIMERS
static int timer_installed = -1;
#endif

static int loaded;

static int ad_format_mask[13 /*devc->model */ ] =
{
	0,
	AFMT_U8 | AFMT_S16_LE | AFMT_MU_LAW | AFMT_A_LAW,
	AFMT_U8 | AFMT_S16_LE | AFMT_MU_LAW | AFMT_A_LAW | AFMT_S16_BE | AFMT_IMA_ADPCM,
	AFMT_U8 | AFMT_S16_LE | AFMT_MU_LAW | AFMT_A_LAW | AFMT_S16_BE | AFMT_IMA_ADPCM,
	AFMT_U8 | AFMT_S16_LE | AFMT_MU_LAW | AFMT_A_LAW,	/* AD1845 */
	AFMT_U8 | AFMT_S16_LE | AFMT_MU_LAW | AFMT_A_LAW | AFMT_S16_BE | AFMT_IMA_ADPCM,
	AFMT_U8 | AFMT_S16_LE | AFMT_MU_LAW | AFMT_A_LAW | AFMT_S16_BE | AFMT_IMA_ADPCM,
	AFMT_U8 | AFMT_S16_LE | AFMT_MU_LAW | AFMT_A_LAW | AFMT_S16_BE | AFMT_IMA_ADPCM,
	AFMT_U8 | AFMT_S16_LE /* CS4235 */,
	AFMT_U8 | AFMT_S16_LE | AFMT_MU_LAW | AFMT_A_LAW	/* Ensoniq Soundscape*/,
	AFMT_U8 | AFMT_S16_LE | AFMT_MU_LAW | AFMT_A_LAW | AFMT_S16_BE | AFMT_IMA_ADPCM,
	AFMT_U8 | AFMT_S16_LE | AFMT_MU_LAW | AFMT_A_LAW | AFMT_S16_BE | AFMT_IMA_ADPCM,
	AFMT_U8 | AFMT_S16_LE | AFMT_MU_LAW | AFMT_A_LAW | AFMT_S16_BE | AFMT_IMA_ADPCM
};

static ad1848_info adev_info[MAX_AUDIO_DEV];

#define io_Index_Addr(d)	((d)->base)
#define io_Indexed_Data(d)	((d)->base+1)
#define io_Status(d)		((d)->base+2)
#define io_Polled_IO(d)		((d)->base+3)

static struct {
     unsigned char flags;
#define CAP_F_TIMER 0x01     
} capabilities [10 /*devc->model */ ] = {
     {0}
    ,{0}           /* MD_1848  */
    ,{CAP_F_TIMER} /* MD_4231  */
    ,{CAP_F_TIMER} /* MD_4231A */
    ,{CAP_F_TIMER} /* MD_1845  */
    ,{CAP_F_TIMER} /* MD_4232  */
    ,{0}           /* MD_C930  */
    ,{CAP_F_TIMER} /* MD_IWAVE */
    ,{0}           /* MD_4235  */
    ,{CAP_F_TIMER} /* MD_1845_SSCAPE */
};

#ifdef CONFIG_PNP
static int isapnp	= 1;
static int isapnpjump;
static int reverse;

static int audio_activated;
#else
static int isapnp;
#endif



static int      ad1848_open(int dev, int mode);
static void     ad1848_close(int dev);
static void     ad1848_output_block(int dev, unsigned long buf, int count, int intrflag);
static void     ad1848_start_input(int dev, unsigned long buf, int count, int intrflag);
static int      ad1848_prepare_for_output(int dev, int bsize, int bcount);
static int      ad1848_prepare_for_input(int dev, int bsize, int bcount);
static void     ad1848_halt(int dev);
static void     ad1848_halt_input(int dev);
static void     ad1848_halt_output(int dev);
static void     ad1848_trigger(int dev, int bits);
static irqreturn_t adintr(int irq, void *dev_id);

#ifndef EXCLUDE_TIMERS
static int ad1848_tmr_install(int dev);
static void ad1848_tmr_reprogram(int dev);
#endif

static int ad_read(ad1848_info * devc, int reg)
{
	int x;
	int timeout = 900000;

	while (timeout > 0 && inb(devc->base) == 0x80)	/*Are we initializing */
		timeout--;

	if(reg < 32)
	{
		outb(((unsigned char) (reg & 0xff) | devc->MCE_bit), io_Index_Addr(devc));
		x = inb(io_Indexed_Data(devc));
	}
	else
	{
		int xreg, xra;

		xreg = (reg & 0xff) - 32;
		xra = (((xreg & 0x0f) << 4) & 0xf0) | 0x08 | ((xreg & 0x10) >> 2);
		outb(((unsigned char) (23 & 0xff) | devc->MCE_bit), io_Index_Addr(devc));
		outb(((unsigned char) (xra & 0xff)), io_Indexed_Data(devc));
		x = inb(io_Indexed_Data(devc));
	}

	return x;
}

static void ad_write(ad1848_info * devc, int reg, int data)
{
	int timeout = 900000;

	while (timeout > 0 && inb(devc->base) == 0x80)	/* Are we initializing */
		timeout--;

	if(reg < 32)
	{
		outb(((unsigned char) (reg & 0xff) | devc->MCE_bit), io_Index_Addr(devc));
		outb(((unsigned char) (data & 0xff)), io_Indexed_Data(devc));
	}
	else
	{
		int xreg, xra;
		
		xreg = (reg & 0xff) - 32;
		xra = (((xreg & 0x0f) << 4) & 0xf0) | 0x08 | ((xreg & 0x10) >> 2);
		outb(((unsigned char) (23 & 0xff) | devc->MCE_bit), io_Index_Addr(devc));
		outb(((unsigned char) (xra & 0xff)), io_Indexed_Data(devc));
		outb((unsigned char) (data & 0xff), io_Indexed_Data(devc));
	}
}

static void wait_for_calibration(ad1848_info * devc)
{
	int timeout = 0;

	/*
	 * Wait until the auto calibration process has finished.
	 *
	 * 1)       Wait until the chip becomes ready (reads don't return 0x80).
	 * 2)       Wait until the ACI bit of I11 gets on and then off.
	 */

	timeout = 100000;
	while (timeout > 0 && inb(devc->base) == 0x80)
		timeout--;
	if (inb(devc->base) & 0x80)
		printk(KERN_WARNING "ad1848: Auto calibration timed out(1).\n");

	timeout = 100;
	while (timeout > 0 && !(ad_read(devc, 11) & 0x20))
		timeout--;
	if (!(ad_read(devc, 11) & 0x20))
		return;

	timeout = 80000;
	while (timeout > 0 && (ad_read(devc, 11) & 0x20))
		timeout--;
	if (ad_read(devc, 11) & 0x20)
		if ((devc->model != MD_1845) && (devc->model != MD_1845_SSCAPE))
			printk(KERN_WARNING "ad1848: Auto calibration timed out(3).\n");
}

static void ad_mute(ad1848_info * devc)
{
	int i;
	unsigned char prev;

	/*
	 * Save old register settings and mute output channels
	 */
	 
	for (i = 6; i < 8; i++)
	{
		prev = devc->saved_regs[i] = ad_read(devc, i);
	}

}

static void ad_unmute(ad1848_info * devc)
{
}

static void ad_enter_MCE(ad1848_info * devc)
{
	int timeout = 1000;
	unsigned short prev;

	while (timeout > 0 && inb(devc->base) == 0x80)	/*Are we initializing */
		timeout--;

	devc->MCE_bit = 0x40;
	prev = inb(io_Index_Addr(devc));
	if (prev & 0x40)
	{
		return;
	}
	outb((devc->MCE_bit), io_Index_Addr(devc));
}

static void ad_leave_MCE(ad1848_info * devc)
{
	unsigned char prev, acal;
	int timeout = 1000;

	while (timeout > 0 && inb(devc->base) == 0x80)	/*Are we initializing */
		timeout--;

	acal = ad_read(devc, 9);

	devc->MCE_bit = 0x00;
	prev = inb(io_Index_Addr(devc));
	outb((0x00), io_Index_Addr(devc));	/* Clear the MCE bit */

	if ((prev & 0x40) == 0)	/* Not in MCE mode */
	{
		return;
	}
	outb((0x00), io_Index_Addr(devc));	/* Clear the MCE bit */
	if (acal & 0x08)	/* Auto calibration is enabled */
		wait_for_calibration(devc);
}

static int ad1848_set_recmask(ad1848_info * devc, int mask)
{
	unsigned char   recdev;
	int             i, n;
	unsigned long flags;

	mask &= devc->supported_rec_devices;

	/* Rename the mixer bits if necessary */
	for (i = 0; i < 32; i++)
	{
		if (devc->mixer_reroute[i] != i)
		{
			if (mask & (1 << i))
			{
				mask &= ~(1 << i);
				mask |= (1 << devc->mixer_reroute[i]);
			}
		}
	}
	
	n = 0;
	for (i = 0; i < 32; i++)	/* Count selected device bits */
		if (mask & (1 << i))
			n++;

	spin_lock_irqsave(&devc->lock,flags);
	if (!soundpro) {
		if (n == 0)
			mask = SOUND_MASK_MIC;
		else if (n != 1) {	/* Too many devices selected */
			mask &= ~devc->recmask;	/* Filter out active settings */

			n = 0;
			for (i = 0; i < 32; i++)	/* Count selected device bits */
				if (mask & (1 << i))
					n++;

			if (n != 1)
				mask = SOUND_MASK_MIC;
		}
		switch (mask) {
		case SOUND_MASK_MIC:
			recdev = 2;
			break;

		case SOUND_MASK_LINE:
		case SOUND_MASK_LINE3:
			recdev = 0;
			break;

		case SOUND_MASK_CD:
		case SOUND_MASK_LINE1:
			recdev = 1;
			break;

		case SOUND_MASK_IMIX:
			recdev = 3;
			break;

		default:
			mask = SOUND_MASK_MIC;
			recdev = 2;
		}

		recdev <<= 6;
		ad_write(devc, 0, (ad_read(devc, 0) & 0x3f) | recdev);
		ad_write(devc, 1, (ad_read(devc, 1) & 0x3f) | recdev);
	} else { /* soundpro */
		unsigned char val;
		int set_rec_bit;
		int j;

		for (i = 0; i < 32; i++) {	/* For each bit */
			if ((devc->supported_rec_devices & (1 << i)) == 0)
				continue;	/* Device not supported */

			for (j = LEFT_CHN; j <= RIGHT_CHN; j++) {
				if (devc->mix_devices[i][j].nbits == 0) /* Inexistent channel */
					continue;

				/*
				 * This is tricky:
				 * set_rec_bit becomes 1 if the corresponding bit in mask is set
				 * then it gets flipped if the polarity is inverse
				 */
				set_rec_bit = ((mask & (1 << i)) != 0) ^ devc->mix_devices[i][j].recpol;

				val = ad_read(devc, devc->mix_devices[i][j].recreg);
				val &= ~(1 << devc->mix_devices[i][j].recpos);
				val |= (set_rec_bit << devc->mix_devices[i][j].recpos);
				ad_write(devc, devc->mix_devices[i][j].recreg, val);
			}
		}
	}
	spin_unlock_irqrestore(&devc->lock,flags);

	/* Rename the mixer bits back if necessary */
	for (i = 0; i < 32; i++)
	{
		if (devc->mixer_reroute[i] != i)
		{
			if (mask & (1 << devc->mixer_reroute[i]))
			{
				mask &= ~(1 << devc->mixer_reroute[i]);
				mask |= (1 << i);
			}
		}
	}
	devc->recmask = mask;
	return mask;
}

static void change_bits(ad1848_info * devc, unsigned char *regval,
			unsigned char *muteval, int dev, int chn, int newval)
{
	unsigned char mask;
	int shift;
	int mute;
	int mutemask;
	int set_mute_bit;

	set_mute_bit = (newval == 0) ^ devc->mix_devices[dev][chn].mutepol;

	if (devc->mix_devices[dev][chn].polarity == 1)	/* Reverse */
		newval = 100 - newval;

	mask = (1 << devc->mix_devices[dev][chn].nbits) - 1;
	shift = devc->mix_devices[dev][chn].bitpos;

	if (devc->mix_devices[dev][chn].mutepos == 8)
	{			/* if there is no mute bit */
		mute = 0;	/* No mute bit; do nothing special */
		mutemask = ~0;	/* No mute bit; do nothing special */
	}
	else
	{
		mute = (set_mute_bit << devc->mix_devices[dev][chn].mutepos);
		mutemask = ~(1 << devc->mix_devices[dev][chn].mutepos);
	}

	newval = (int) ((newval * mask) + 50) / 100;	/* Scale it */
	*regval &= ~(mask << shift);			/* Clear bits */
	*regval |= (newval & mask) << shift;		/* Set new value */

	*muteval &= mutemask;
	*muteval |= mute;
}

static int ad1848_mixer_get(ad1848_info * devc, int dev)
{
	if (!((1 << dev) & devc->supported_devices))
		return -EINVAL;

	dev = devc->mixer_reroute[dev];

	return devc->levels[dev];
}

static void ad1848_mixer_set_channel(ad1848_info *devc, int dev, int value, int channel)
{
	int regoffs, muteregoffs;
	unsigned char val, muteval;
	unsigned long flags;

	regoffs = devc->mix_devices[dev][channel].regno;
	muteregoffs = devc->mix_devices[dev][channel].mutereg;
	val = ad_read(devc, regoffs);

	if (muteregoffs != regoffs) {
		muteval = ad_read(devc, muteregoffs);
		change_bits(devc, &val, &muteval, dev, channel, value);
	}
	else
		change_bits(devc, &val, &val, dev, channel, value);

	spin_lock_irqsave(&devc->lock,flags);
	ad_write(devc, regoffs, val);
	devc->saved_regs[regoffs] = val;
	if (muteregoffs != regoffs) {
		ad_write(devc, muteregoffs, muteval);
		devc->saved_regs[muteregoffs] = muteval;
	}
	spin_unlock_irqrestore(&devc->lock,flags);
}

static int ad1848_mixer_set(ad1848_info * devc, int dev, int value)
{
	int left = value & 0x000000ff;
	int right = (value & 0x0000ff00) >> 8;
	int retvol;

	if (dev > 31)
		return -EINVAL;

	if (!(devc->supported_devices & (1 << dev)))
		return -EINVAL;

	dev = devc->mixer_reroute[dev];

	if (devc->mix_devices[dev][LEFT_CHN].nbits == 0)
		return -EINVAL;

	if (left > 100)
		left = 100;
	if (right > 100)
		right = 100;

	if (devc->mix_devices[dev][RIGHT_CHN].nbits == 0)	/* Mono control */
		right = left;

	retvol = left | (right << 8);

	/* Scale volumes */
	left = mix_cvt[left];
	right = mix_cvt[right];

	devc->levels[dev] = retvol;

	/*
	 * Set the left channel
	 */
	ad1848_mixer_set_channel(devc, dev, left, LEFT_CHN);

	/*
	 * Set the right channel
	 */
	if (devc->mix_devices[dev][RIGHT_CHN].nbits == 0)
		goto out;
	ad1848_mixer_set_channel(devc, dev, right, RIGHT_CHN);

 out:
	return retvol;
}

static void ad1848_mixer_reset(ad1848_info * devc)
{
	int i;
	char name[32];
	unsigned long flags;

	devc->mix_devices = &(ad1848_mix_devices[0]);

	sprintf(name, "%s_%d", devc->chip_name, nr_ad1848_devs);

	for (i = 0; i < 32; i++)
		devc->mixer_reroute[i] = i;

	devc->supported_rec_devices = MODE1_REC_DEVICES;

	switch (devc->model)
	{
		case MD_4231:
		case MD_4231A:
		case MD_1845:
		case MD_1845_SSCAPE:
			devc->supported_devices = MODE2_MIXER_DEVICES;
			break;

		case MD_C930:
			devc->supported_devices = C930_MIXER_DEVICES;
			devc->mix_devices = &(c930_mix_devices[0]);
			break;

		case MD_IWAVE:
			devc->supported_devices = MODE3_MIXER_DEVICES;
			devc->mix_devices = &(iwave_mix_devices[0]);
			break;

		case MD_42xB:
		case MD_4239:
			devc->mix_devices = &(cs42xb_mix_devices[0]);
			devc->supported_devices = MODE3_MIXER_DEVICES;
			break;
		case MD_4232:
		case MD_4235:
		case MD_4236:
			devc->supported_devices = MODE3_MIXER_DEVICES;
			break;

		case MD_1848:
			if (soundpro) {
				devc->supported_devices = SPRO_MIXER_DEVICES;
				devc->supported_rec_devices = SPRO_REC_DEVICES;
				devc->mix_devices = &(spro_mix_devices[0]);
				break;
			}

		default:
			devc->supported_devices = MODE1_MIXER_DEVICES;
	}

	devc->orig_devices = devc->supported_devices;
	devc->orig_rec_devices = devc->supported_rec_devices;

	devc->levels = load_mixer_volumes(name, default_mixer_levels, 1);

	for (i = 0; i < SOUND_MIXER_NRDEVICES; i++)
	{
		if (devc->supported_devices & (1 << i))
			ad1848_mixer_set(devc, i, devc->levels[i]);
	}
	
	ad1848_set_recmask(devc, SOUND_MASK_MIC);
	
	devc->mixer_output_port = devc->levels[31] | AUDIO_HEADPHONE | AUDIO_LINE_OUT;

	spin_lock_irqsave(&devc->lock,flags);
	if (!soundpro) {
		if (devc->mixer_output_port & AUDIO_SPEAKER)
			ad_write(devc, 26, ad_read(devc, 26) & ~0x40);	/* Unmute mono out */
		else
			ad_write(devc, 26, ad_read(devc, 26) | 0x40);	/* Mute mono out */
	} else {
		/*
		 * From the "wouldn't it be nice if the mixer API had (better)
		 * support for custom stuff" category
		 */
		/* Enable surround mode and SB16 mixer */
		ad_write(devc, 16, 0x60);
	}
	spin_unlock_irqrestore(&devc->lock,flags);
}

static int ad1848_mixer_ioctl(int dev, unsigned int cmd, void __user *arg)
{
	ad1848_info *devc = mixer_devs[dev]->devc;
	int val;

	if (cmd == SOUND_MIXER_PRIVATE1) 
	{
		if (get_user(val, (int __user *)arg))
			return -EFAULT;

		if (val != 0xffff) 
		{
			unsigned long flags;
			val &= (AUDIO_SPEAKER | AUDIO_HEADPHONE | AUDIO_LINE_OUT);
			devc->mixer_output_port = val;
			val |= AUDIO_HEADPHONE | AUDIO_LINE_OUT;	/* Always on */
			devc->mixer_output_port = val;
			spin_lock_irqsave(&devc->lock,flags);
			if (val & AUDIO_SPEAKER)
				ad_write(devc, 26, ad_read(devc, 26) & ~0x40);	/* Unmute mono out */
			else
				ad_write(devc, 26, ad_read(devc, 26) | 0x40);		/* Mute mono out */
			spin_unlock_irqrestore(&devc->lock,flags);
		}
		val = devc->mixer_output_port;
		return put_user(val, (int __user *)arg);
	}
	if (cmd == SOUND_MIXER_PRIVATE2)
	{
		if (get_user(val, (int __user *)arg))
			return -EFAULT;
		return(ad1848_control(AD1848_MIXER_REROUTE, val));
	}
	if (((cmd >> 8) & 0xff) == 'M') 
	{
		if (_SIOC_DIR(cmd) & _SIOC_WRITE)
		{
			switch (cmd & 0xff) 
			{
				case SOUND_MIXER_RECSRC:
					if (get_user(val, (int __user *)arg))
						return -EFAULT;
					val = ad1848_set_recmask(devc, val);
					break;
				
				default:
					if (get_user(val, (int __user *)arg))
					return -EFAULT;
					val = ad1848_mixer_set(devc, cmd & 0xff, val);
					break;
			} 
			return put_user(val, (int __user *)arg);
		}
		else
		{
			switch (cmd & 0xff) 
			{
				/*
				 * Return parameters
				 */
			    
				case SOUND_MIXER_RECSRC:
					val = devc->recmask;
					break;
				
				case SOUND_MIXER_DEVMASK:
					val = devc->supported_devices;
					break;
				
				case SOUND_MIXER_STEREODEVS:
					val = devc->supported_devices;
					if (devc->model != MD_C930)
						val &= ~(SOUND_MASK_SPEAKER | SOUND_MASK_IMIX);
					break;
				
				case SOUND_MIXER_RECMASK:
					val = devc->supported_rec_devices;
					break;

				case SOUND_MIXER_CAPS:
					val=SOUND_CAP_EXCL_INPUT;
					break;

				default:
					val = ad1848_mixer_get(devc, cmd & 0xff);
					break;
			}
			return put_user(val, (int __user *)arg);
		}
	}
	else
		return -EINVAL;
}

static int ad1848_set_speed(int dev, int arg)
{
	ad1848_info *devc = (ad1848_info *) audio_devs[dev]->devc;
	ad1848_port_info *portc = (ad1848_port_info *) audio_devs[dev]->portc;

	/*
	 * The sampling speed is encoded in the least significant nibble of I8. The
	 * LSB selects the clock source (0=24.576 MHz, 1=16.9344 MHz) and other
	 * three bits select the divisor (indirectly):
	 *
	 * The available speeds are in the following table. Keep the speeds in
	 * the increasing order.
	 */
	typedef struct
	{
		int             speed;
		unsigned char   bits;
	}
	speed_struct;

	static speed_struct speed_table[] =
	{
		{5510, (0 << 1) | 1},
		{5510, (0 << 1) | 1},
		{6620, (7 << 1) | 1},
		{8000, (0 << 1) | 0},
		{9600, (7 << 1) | 0},
		{11025, (1 << 1) | 1},
		{16000, (1 << 1) | 0},
		{18900, (2 << 1) | 1},
		{22050, (3 << 1) | 1},
		{27420, (2 << 1) | 0},
		{32000, (3 << 1) | 0},
		{33075, (6 << 1) | 1},
		{37800, (4 << 1) | 1},
		{44100, (5 << 1) | 1},
		{48000, (6 << 1) | 0}
	};

	int i, n, selected = -1;

	n = sizeof(speed_table) / sizeof(speed_struct);

	if (arg <= 0)
		return portc->speed;

	if (devc->model == MD_1845 || devc->model == MD_1845_SSCAPE)	/* AD1845 has different timer than others */
	{
		if (arg < 4000)
			arg = 4000;
		if (arg > 50000)
			arg = 50000;

		portc->speed = arg;
		portc->speed_bits = speed_table[3].bits;
		return portc->speed;
	}
	if (arg < speed_table[0].speed)
		selected = 0;
	if (arg > speed_table[n - 1].speed)
		selected = n - 1;

	for (i = 1 /*really */ ; selected == -1 && i < n; i++)
	{
		if (speed_table[i].speed == arg)
			selected = i;
		else if (speed_table[i].speed > arg)
		{
			int diff1, diff2;

			diff1 = arg - speed_table[i - 1].speed;
			diff2 = speed_table[i].speed - arg;

			if (diff1 < diff2)
				selected = i - 1;
			else
				selected = i;
		}
	}
	if (selected == -1)
	{
		printk(KERN_WARNING "ad1848: Can't find speed???\n");
		selected = 3;
	}
	portc->speed = speed_table[selected].speed;
	portc->speed_bits = speed_table[selected].bits;
	return portc->speed;
}

static short ad1848_set_channels(int dev, short arg)
{
	ad1848_port_info *portc = (ad1848_port_info *) audio_devs[dev]->portc;

	if (arg != 1 && arg != 2)
		return portc->channels;

	portc->channels = arg;
	return arg;
}

static unsigned int ad1848_set_bits(int dev, unsigned int arg)
{
	ad1848_info    *devc = (ad1848_info *) audio_devs[dev]->devc;
	ad1848_port_info *portc = (ad1848_port_info *) audio_devs[dev]->portc;

	static struct format_tbl
	{
		  int             format;
		  unsigned char   bits;
	}
	format2bits[] =
	{
		{
			0, 0
		}
		,
		{
			AFMT_MU_LAW, 1
		}
		,
		{
			AFMT_A_LAW, 3
		}
		,
		{
			AFMT_IMA_ADPCM, 5
		}
		,
		{
			AFMT_U8, 0
		}
		,
		{
			AFMT_S16_LE, 2
		}
		,
		{
			AFMT_S16_BE, 6
		}
		,
		{
			AFMT_S8, 0
		}
		,
		{
			AFMT_U16_LE, 0
		}
		,
		{
			AFMT_U16_BE, 0
		}
	};
	int i, n = sizeof(format2bits) / sizeof(struct format_tbl);

	if (arg == 0)
		return portc->audio_format;

	if (!(arg & ad_format_mask[devc->model]))
		arg = AFMT_U8;

	portc->audio_format = arg;

	for (i = 0; i < n; i++)
		if (format2bits[i].format == arg)
		{
			if ((portc->format_bits = format2bits[i].bits) == 0)
				return portc->audio_format = AFMT_U8;		/* Was not supported */

			return arg;
		}
	/* Still hanging here. Something must be terribly wrong */
	portc->format_bits = 0;
	return portc->audio_format = AFMT_U8;
}

static struct audio_driver ad1848_audio_driver =
{
	.owner			= THIS_MODULE,
	.open			= ad1848_open,
	.close			= ad1848_close,
	.output_block		= ad1848_output_block,
	.start_input		= ad1848_start_input,
	.prepare_for_input	= ad1848_prepare_for_input,
	.prepare_for_output	= ad1848_prepare_for_output,
	.halt_io		= ad1848_halt,
	.halt_input		= ad1848_halt_input,
	.halt_output		= ad1848_halt_output,
	.trigger		= ad1848_trigger,
	.set_speed		= ad1848_set_speed,
	.set_bits		= ad1848_set_bits,
	.set_channels		= ad1848_set_channels
};

static struct mixer_operations ad1848_mixer_operations =
{
	.owner	= THIS_MODULE,
	.id	= "SOUNDPORT",
	.name	= "AD1848/CS4248/CS4231",
	.ioctl	= ad1848_mixer_ioctl
};

static int ad1848_open(int dev, int mode)
{
	ad1848_info    *devc;
	ad1848_port_info *portc;
	unsigned long   flags;

	if (dev < 0 || dev >= num_audiodevs)
		return -ENXIO;

	devc = (ad1848_info *) audio_devs[dev]->devc;
	portc = (ad1848_port_info *) audio_devs[dev]->portc;

	/* here we don't have to protect against intr */
	spin_lock(&devc->lock);
	if (portc->open_mode || (devc->open_mode & mode))
	{
		spin_unlock(&devc->lock);
		return -EBUSY;
	}
	devc->dual_dma = 0;

	if (audio_devs[dev]->flags & DMA_DUPLEX)
	{
		devc->dual_dma = 1;
	}
	devc->intr_active = 0;
	devc->audio_mode = 0;
	devc->open_mode |= mode;
	portc->open_mode = mode;
	spin_unlock(&devc->lock);
	ad1848_trigger(dev, 0);

	if (mode & OPEN_READ)
		devc->record_dev = dev;
	if (mode & OPEN_WRITE)
		devc->playback_dev = dev;
/*
 * Mute output until the playback really starts. This decreases clicking (hope so).
 */
	spin_lock_irqsave(&devc->lock,flags);
	ad_mute(devc);
	spin_unlock_irqrestore(&devc->lock,flags);

	return 0;
}

static void ad1848_close(int dev)
{
	unsigned long   flags;
	ad1848_info    *devc = (ad1848_info *) audio_devs[dev]->devc;
	ad1848_port_info *portc = (ad1848_port_info *) audio_devs[dev]->portc;

	DEB(printk("ad1848_close(void)\n"));

	devc->intr_active = 0;
	ad1848_halt(dev);

	spin_lock_irqsave(&devc->lock,flags);

	devc->audio_mode = 0;
	devc->open_mode &= ~portc->open_mode;
	portc->open_mode = 0;

	ad_unmute(devc);
	spin_unlock_irqrestore(&devc->lock,flags);
}

static void ad1848_output_block(int dev, unsigned long buf, int count, int intrflag)
{
	unsigned long   flags, cnt;
	ad1848_info    *devc = (ad1848_info *) audio_devs[dev]->devc;
	ad1848_port_info *portc = (ad1848_port_info *) audio_devs[dev]->portc;

	cnt = count;

	if (portc->audio_format == AFMT_IMA_ADPCM)
	{
		cnt /= 4;
	}
	else
	{
		if (portc->audio_format & (AFMT_S16_LE | AFMT_S16_BE))	/* 16 bit data */
			cnt >>= 1;
	}
	if (portc->channels > 1)
		cnt >>= 1;
	cnt--;

	if ((devc->audio_mode & PCM_ENABLE_OUTPUT) && (audio_devs[dev]->flags & DMA_AUTOMODE) &&
	    intrflag &&
	    cnt == devc->xfer_count)
	{
		devc->audio_mode |= PCM_ENABLE_OUTPUT;
		devc->intr_active = 1;
		return;	/*
			 * Auto DMA mode on. No need to react
			 */
	}
	spin_lock_irqsave(&devc->lock,flags);

	ad_write(devc, 15, (unsigned char) (cnt & 0xff));
	ad_write(devc, 14, (unsigned char) ((cnt >> 8) & 0xff));

	devc->xfer_count = cnt;
	devc->audio_mode |= PCM_ENABLE_OUTPUT;
	devc->intr_active = 1;
	spin_unlock_irqrestore(&devc->lock,flags);
}

static void ad1848_start_input(int dev, unsigned long buf, int count, int intrflag)
{
	unsigned long   flags, cnt;
	ad1848_info    *devc = (ad1848_info *) audio_devs[dev]->devc;
	ad1848_port_info *portc = (ad1848_port_info *) audio_devs[dev]->portc;

	cnt = count;
	if (portc->audio_format == AFMT_IMA_ADPCM)
	{
		cnt /= 4;
	}
	else
	{
		if (portc->audio_format & (AFMT_S16_LE | AFMT_S16_BE))	/* 16 bit data */
			cnt >>= 1;
	}
	if (portc->channels > 1)
		cnt >>= 1;
	cnt--;

	if ((devc->audio_mode & PCM_ENABLE_INPUT) && (audio_devs[dev]->flags & DMA_AUTOMODE) &&
		intrflag &&
		cnt == devc->xfer_count)
	{
		devc->audio_mode |= PCM_ENABLE_INPUT;
		devc->intr_active = 1;
		return;	/*
			 * Auto DMA mode on. No need to react
			 */
	}
	spin_lock_irqsave(&devc->lock,flags);

	if (devc->model == MD_1848)
	{
		  ad_write(devc, 15, (unsigned char) (cnt & 0xff));
		  ad_write(devc, 14, (unsigned char) ((cnt >> 8) & 0xff));
	}
	else
	{
		  ad_write(devc, 31, (unsigned char) (cnt & 0xff));
		  ad_write(devc, 30, (unsigned char) ((cnt >> 8) & 0xff));
	}

	ad_unmute(devc);

	devc->xfer_count = cnt;
	devc->audio_mode |= PCM_ENABLE_INPUT;
	devc->intr_active = 1;
	spin_unlock_irqrestore(&devc->lock,flags);
}

static int ad1848_prepare_for_output(int dev, int bsize, int bcount)
{
	int             timeout;
	unsigned char   fs, old_fs, tmp = 0;
	unsigned long   flags;
	ad1848_info    *devc = (ad1848_info *) audio_devs[dev]->devc;
	ad1848_port_info *portc = (ad1848_port_info *) audio_devs[dev]->portc;

	ad_mute(devc);

	spin_lock_irqsave(&devc->lock,flags);
	fs = portc->speed_bits | (portc->format_bits << 5);

	if (portc->channels > 1)
		fs |= 0x10;

	ad_enter_MCE(devc);	/* Enables changes to the format select reg */

	if (devc->model == MD_1845 || devc->model == MD_1845_SSCAPE) /* Use alternate speed select registers */
	{
		fs &= 0xf0;	/* Mask off the rate select bits */

		ad_write(devc, 22, (portc->speed >> 8) & 0xff);	/* Speed MSB */
		ad_write(devc, 23, portc->speed & 0xff);	/* Speed LSB */
	}
	old_fs = ad_read(devc, 8);

	if (devc->model == MD_4232 || devc->model >= MD_4236)
	{
		tmp = ad_read(devc, 16);
		ad_write(devc, 16, tmp | 0x30);
	}
	if (devc->model == MD_IWAVE)
		ad_write(devc, 17, 0xc2);	/* Disable variable frequency select */

	ad_write(devc, 8, fs);

	/*
	 * Write to I8 starts resynchronization. Wait until it completes.
	 */

	timeout = 0;
	while (timeout < 100 && inb(devc->base) != 0x80)
		timeout++;
	timeout = 0;
	while (timeout < 10000 && inb(devc->base) == 0x80)
		timeout++;

	if (devc->model >= MD_4232)
		ad_write(devc, 16, tmp & ~0x30);

	ad_leave_MCE(devc);	/*
				 * Starts the calibration process.
				 */
	spin_unlock_irqrestore(&devc->lock,flags);
	devc->xfer_count = 0;

#ifndef EXCLUDE_TIMERS
	if (dev == timer_installed && devc->timer_running)
		if ((fs & 0x01) != (old_fs & 0x01))
		{
			ad1848_tmr_reprogram(dev);
		}
#endif
	ad1848_halt_output(dev);
	return 0;
}

static int ad1848_prepare_for_input(int dev, int bsize, int bcount)
{
	int timeout;
	unsigned char fs, old_fs, tmp = 0;
	unsigned long flags;
	ad1848_info *devc = (ad1848_info *) audio_devs[dev]->devc;
	ad1848_port_info *portc = (ad1848_port_info *) audio_devs[dev]->portc;

	if (devc->audio_mode)
		return 0;

	spin_lock_irqsave(&devc->lock,flags);
	fs = portc->speed_bits | (portc->format_bits << 5);

	if (portc->channels > 1)
		fs |= 0x10;

	ad_enter_MCE(devc);	/* Enables changes to the format select reg */

	if ((devc->model == MD_1845) || (devc->model == MD_1845_SSCAPE))	/* Use alternate speed select registers */
	{
		fs &= 0xf0;	/* Mask off the rate select bits */

		ad_write(devc, 22, (portc->speed >> 8) & 0xff);	/* Speed MSB */
		ad_write(devc, 23, portc->speed & 0xff);	/* Speed LSB */
	}
	if (devc->model == MD_4232)
	{
		tmp = ad_read(devc, 16);
		ad_write(devc, 16, tmp | 0x30);
	}
	if (devc->model == MD_IWAVE)
		ad_write(devc, 17, 0xc2);	/* Disable variable frequency select */

	/*
	 * If mode >= 2 (CS4231), set I28. It's the capture format register.
	 */
	
	if (devc->model != MD_1848)
	{
		old_fs = ad_read(devc, 28);
		ad_write(devc, 28, fs);

		/*
		 * Write to I28 starts resynchronization. Wait until it completes.
		 */
		
		timeout = 0;
		while (timeout < 100 && inb(devc->base) != 0x80)
			timeout++;

		timeout = 0;
		while (timeout < 10000 && inb(devc->base) == 0x80)
			timeout++;

		if (devc->model != MD_1848 && devc->model != MD_1845 && devc->model != MD_1845_SSCAPE)
		{
			/*
			 * CS4231 compatible devices don't have separate sampling rate selection
			 * register for recording an playback. The I8 register is shared so we have to
			 * set the speed encoding bits of it too.
			 */
			unsigned char   tmp = portc->speed_bits | (ad_read(devc, 8) & 0xf0);

			ad_write(devc, 8, tmp);
			/*
			 * Write to I8 starts resynchronization. Wait until it completes.
			 */
			timeout = 0;
			while (timeout < 100 && inb(devc->base) != 0x80)
				timeout++;

			timeout = 0;
			while (timeout < 10000 && inb(devc->base) == 0x80)
				timeout++;
		}
	}
	else
	{			/* For AD1848 set I8. */

		old_fs = ad_read(devc, 8);
		ad_write(devc, 8, fs);
		/*
		 * Write to I8 starts resynchronization. Wait until it completes.
		 */
		timeout = 0;
		while (timeout < 100 && inb(devc->base) != 0x80)
			timeout++;
		timeout = 0;
		while (timeout < 10000 && inb(devc->base) == 0x80)
			timeout++;
	}

	if (devc->model == MD_4232)
		ad_write(devc, 16, tmp & ~0x30);

	ad_leave_MCE(devc);	/*
				 * Starts the calibration process.
				 */
	spin_unlock_irqrestore(&devc->lock,flags);
	devc->xfer_count = 0;

#ifndef EXCLUDE_TIMERS
	if (dev == timer_installed && devc->timer_running)
	{
		if ((fs & 0x01) != (old_fs & 0x01))
		{
			ad1848_tmr_reprogram(dev);
		}
	}
#endif
	ad1848_halt_input(dev);
	return 0;
}

static void ad1848_halt(int dev)
{
	ad1848_info *devc = (ad1848_info *) audio_devs[dev]->devc;
	ad1848_port_info *portc = (ad1848_port_info *) audio_devs[dev]->portc;

	unsigned char   bits = ad_read(devc, 9);

	if (bits & 0x01 && (portc->open_mode & OPEN_WRITE))
		ad1848_halt_output(dev);

	if (bits & 0x02 && (portc->open_mode & OPEN_READ))
		ad1848_halt_input(dev);
	devc->audio_mode = 0;
}

static void ad1848_halt_input(int dev)
{
	ad1848_info    *devc = (ad1848_info *) audio_devs[dev]->devc;
	unsigned long   flags;

	if (!(ad_read(devc, 9) & 0x02))
		return;		/* Capture not enabled */

	spin_lock_irqsave(&devc->lock,flags);

	ad_mute(devc);

	{
		int             tmout;
		
		if(!isa_dma_bridge_buggy)
		        disable_dma(audio_devs[dev]->dmap_in->dma);

		for (tmout = 0; tmout < 100000; tmout++)
			if (ad_read(devc, 11) & 0x10)
				break;
		ad_write(devc, 9, ad_read(devc, 9) & ~0x02);	/* Stop capture */

		if(!isa_dma_bridge_buggy)
		        enable_dma(audio_devs[dev]->dmap_in->dma);
		devc->audio_mode &= ~PCM_ENABLE_INPUT;
	}

	outb(0, io_Status(devc));	/* Clear interrupt status */
	outb(0, io_Status(devc));	/* Clear interrupt status */

	devc->audio_mode &= ~PCM_ENABLE_INPUT;

	spin_unlock_irqrestore(&devc->lock,flags);
}

static void ad1848_halt_output(int dev)
{
	ad1848_info *devc = (ad1848_info *) audio_devs[dev]->devc;
	unsigned long flags;

	if (!(ad_read(devc, 9) & 0x01))
		return;		/* Playback not enabled */

	spin_lock_irqsave(&devc->lock,flags);

	ad_mute(devc);
	{
		int             tmout;

		if(!isa_dma_bridge_buggy)
		        disable_dma(audio_devs[dev]->dmap_out->dma);

		for (tmout = 0; tmout < 100000; tmout++)
			if (ad_read(devc, 11) & 0x10)
				break;
		ad_write(devc, 9, ad_read(devc, 9) & ~0x01);	/* Stop playback */

		if(!isa_dma_bridge_buggy)
		       enable_dma(audio_devs[dev]->dmap_out->dma);

		devc->audio_mode &= ~PCM_ENABLE_OUTPUT;
	}

	outb((0), io_Status(devc));	/* Clear interrupt status */
	outb((0), io_Status(devc));	/* Clear interrupt status */

	devc->audio_mode &= ~PCM_ENABLE_OUTPUT;

	spin_unlock_irqrestore(&devc->lock,flags);
}

static void ad1848_trigger(int dev, int state)
{
	ad1848_info    *devc = (ad1848_info *) audio_devs[dev]->devc;
	ad1848_port_info *portc = (ad1848_port_info *) audio_devs[dev]->portc;
	unsigned long   flags;
	unsigned char   tmp, old;

	spin_lock_irqsave(&devc->lock,flags);
	state &= devc->audio_mode;

	tmp = old = ad_read(devc, 9);

	if (portc->open_mode & OPEN_READ)
	{
		  if (state & PCM_ENABLE_INPUT)
			  tmp |= 0x02;
		  else
			  tmp &= ~0x02;
	}
	if (portc->open_mode & OPEN_WRITE)
	{
		if (state & PCM_ENABLE_OUTPUT)
			tmp |= 0x01;
		else
			tmp &= ~0x01;
	}
	/* ad_mute(devc); */
	if (tmp != old)
	{
		  ad_write(devc, 9, tmp);
		  ad_unmute(devc);
	}
	spin_unlock_irqrestore(&devc->lock,flags);
}

static void ad1848_init_hw(ad1848_info * devc)
{
	int i;
	int *init_values;

	/*
	 * Initial values for the indirect registers of CS4248/AD1848.
	 */
	static int      init_values_a[] =
	{
		0xa8, 0xa8, 0x08, 0x08, 0x08, 0x08, 0x00, 0x00,
		0x00, 0x0c, 0x02, 0x00, 0x8a, 0x01, 0x00, 0x00,

	/* Positions 16 to 31 just for CS4231/2 and ad1845 */
		0x80, 0x00, 0x10, 0x10, 0x00, 0x00, 0x1f, 0x40,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	};

	static int      init_values_b[] =
	{
		/* 
		   Values for the newer chips
		   Some of the register initialization values were changed. In
		   order to get rid of the click that preceded PCM playback,
		   calibration was disabled on the 10th byte. On that same byte,
		   dual DMA was enabled; on the 11th byte, ADC dithering was
		   enabled, since that is theoretically desirable; on the 13th
		   byte, Mode 3 was selected, to enable access to extended
		   registers.
		 */
		0xa8, 0xa8, 0x08, 0x08, 0x08, 0x08, 0x00, 0x00,
		0x00, 0x00, 0x06, 0x00, 0xe0, 0x01, 0x00, 0x00,
 		0x80, 0x00, 0x10, 0x10, 0x00, 0x00, 0x1f, 0x40,
 		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	};

	/*
	 *	Select initialisation data
	 */
	 
	init_values = init_values_a;
	if(devc->model >= MD_4236)
		init_values = init_values_b;

	for (i = 0; i < 16; i++)
		ad_write(devc, i, init_values[i]);


	ad_mute(devc);		/* Initialize some variables */
	ad_unmute(devc);	/* Leave it unmuted now */

	if (devc->model > MD_1848)
	{
		if (devc->model == MD_1845_SSCAPE)
			ad_write(devc, 12, ad_read(devc, 12) | 0x50);
		else 
			ad_write(devc, 12, ad_read(devc, 12) | 0x40);		/* Mode2 = enabled */

		if (devc->model == MD_IWAVE)
			ad_write(devc, 12, 0x6c);	/* Select codec mode 3 */

		if (devc->model != MD_1845_SSCAPE)
			for (i = 16; i < 32; i++)
				ad_write(devc, i, init_values[i]);

		if (devc->model == MD_IWAVE)
			ad_write(devc, 16, 0x30);	/* Playback and capture counters enabled */
	}
	if (devc->model > MD_1848)
	{
		if (devc->audio_flags & DMA_DUPLEX)
			ad_write(devc, 9, ad_read(devc, 9) & ~0x04);	/* Dual DMA mode */
		else
			ad_write(devc, 9, ad_read(devc, 9) | 0x04);	/* Single DMA mode */

		if (devc->model == MD_1845 || devc->model == MD_1845_SSCAPE)
			ad_write(devc, 27, ad_read(devc, 27) | 0x08);		/* Alternate freq select enabled */

		if (devc->model == MD_IWAVE)
		{		/* Some magic Interwave specific initialization */
			ad_write(devc, 12, 0x6c);	/* Select codec mode 3 */
			ad_write(devc, 16, 0x30);	/* Playback and capture counters enabled */
			ad_write(devc, 17, 0xc2);	/* Alternate feature enable */
		}
	}
	else
	{
		  devc->audio_flags &= ~DMA_DUPLEX;
		  ad_write(devc, 9, ad_read(devc, 9) | 0x04);	/* Single DMA mode */
		  if (soundpro)
			  ad_write(devc, 12, ad_read(devc, 12) | 0x40);	/* Mode2 = enabled */
	}

	outb((0), io_Status(devc));	/* Clear pending interrupts */

	/*
	 * Toggle the MCE bit. It completes the initialization phase.
	 */

	ad_enter_MCE(devc);	/* In case the bit was off */
	ad_leave_MCE(devc);

	ad1848_mixer_reset(devc);
}

int ad1848_detect(struct resource *ports, int *ad_flags, int *osp)
{
	unsigned char tmp;
	ad1848_info *devc = &adev_info[nr_ad1848_devs];
	unsigned char tmp1 = 0xff, tmp2 = 0xff;
	int optiC930 = 0;	/* OPTi 82C930 flag */
	int interwave = 0;
	int ad1847_flag = 0;
	int cs4248_flag = 0;
	int sscape_flag = 0;
	int io_base = ports->start;

	int i;

	DDB(printk("ad1848_detect(%x)\n", io_base));

	if (ad_flags)
	{
		if (*ad_flags == 0x12345678)
		{
			interwave = 1;
			*ad_flags = 0;
		}
		
		if (*ad_flags == 0x87654321)
		{
			sscape_flag = 1;
			*ad_flags = 0;
		}
		
		if (*ad_flags == 0x12345677)
		{
		    cs4248_flag = 1;
		    *ad_flags = 0;
		}
	}
	if (nr_ad1848_devs >= MAX_AUDIO_DEV)
	{
		printk(KERN_ERR "ad1848 - Too many audio devices\n");
		return 0;
	}
	spin_lock_init(&devc->lock);
	devc->base = io_base;
	devc->irq_ok = 0;
	devc->timer_running = 0;
	devc->MCE_bit = 0x40;
	devc->irq = 0;
	devc->open_mode = 0;
	devc->chip_name = devc->name = "AD1848";
	devc->model = MD_1848;	/* AD1848 or CS4248 */
	devc->levels = NULL;
	devc->debug_flag = 0;

	/*
	 * Check that the I/O address is in use.
	 *
	 * The bit 0x80 of the base I/O port is known to be 0 after the
	 * chip has performed its power on initialization. Just assume
	 * this has happened before the OS is starting.
	 *
	 * If the I/O address is unused, it typically returns 0xff.
	 */

	if (inb(devc->base) == 0xff)
	{
		DDB(printk("ad1848_detect: The base I/O address appears to be dead\n"));
	}

	/*
	 * Wait for the device to stop initialization
	 */
	
	DDB(printk("ad1848_detect() - step 0\n"));

	for (i = 0; i < 10000000; i++)
	{
		unsigned char   x = inb(devc->base);

		if (x == 0xff || !(x & 0x80))
			break;
	}

	DDB(printk("ad1848_detect() - step A\n"));

	if (inb(devc->base) == 0x80)	/* Not ready. Let's wait */
		ad_leave_MCE(devc);

	if ((inb(devc->base) & 0x80) != 0x00)	/* Not a AD1848 */
	{
		DDB(printk("ad1848 detect error - step A (%02x)\n", (int) inb(devc->base)));
		return 0;
	}
	
	/*
	 * Test if it's possible to change contents of the indirect registers.
	 * Registers 0 and 1 are ADC volume registers. The bit 0x10 is read only
	 * so try to avoid using it.
	 */

	DDB(printk("ad1848_detect() - step B\n"));
	ad_write(devc, 0, 0xaa);
	ad_write(devc, 1, 0x45);	/* 0x55 with bit 0x10 clear */

	if ((tmp1 = ad_read(devc, 0)) != 0xaa || (tmp2 = ad_read(devc, 1)) != 0x45)
	{
		if (tmp2 == 0x65)	/* AD1847 has couple of bits hardcoded to 1 */
			ad1847_flag = 1;
		else
		{
			DDB(printk("ad1848 detect error - step B (%x/%x)\n", tmp1, tmp2));
			return 0;
		}
	}
	DDB(printk("ad1848_detect() - step C\n"));
	ad_write(devc, 0, 0x45);
	ad_write(devc, 1, 0xaa);

	if ((tmp1 = ad_read(devc, 0)) != 0x45 || (tmp2 = ad_read(devc, 1)) != 0xaa)
	{
		if (tmp2 == 0x8a)	/* AD1847 has few bits hardcoded to 1 */
			ad1847_flag = 1;
		else
		{
			DDB(printk("ad1848 detect error - step C (%x/%x)\n", tmp1, tmp2));
			return 0;
		}
	}

	/*
	 * The indirect register I12 has some read only bits. Let's
	 * try to change them.
	 */

	DDB(printk("ad1848_detect() - step D\n"));
	tmp = ad_read(devc, 12);
	ad_write(devc, 12, (~tmp) & 0x0f);

	if ((tmp & 0x0f) != ((tmp1 = ad_read(devc, 12)) & 0x0f))
	{
		DDB(printk("ad1848 detect error - step D (%x)\n", tmp1));
		return 0;
	}
	
	/*
	 * NOTE! Last 4 bits of the reg I12 tell the chip revision.
	 *   0x01=RevB and 0x0A=RevC.
	 */

	/*
	 * The original AD1848/CS4248 has just 15 indirect registers. This means
	 * that I0 and I16 should return the same value (etc.).
	 * However this doesn't work with CS4248. Actually it seems to be impossible
	 * to detect if the chip is a CS4231 or CS4248.
	 * Ensure that the Mode2 enable bit of I12 is 0. Otherwise this test fails
	 * with CS4231.
	 */

	/*
	 * OPTi 82C930 has mode2 control bit in another place. This test will fail
	 * with it. Accept this situation as a possible indication of this chip.
	 */

	DDB(printk("ad1848_detect() - step F\n"));
	ad_write(devc, 12, 0);	/* Mode2=disabled */

	for (i = 0; i < 16; i++)
	{
		if ((tmp1 = ad_read(devc, i)) != (tmp2 = ad_read(devc, i + 16)))
		{
			DDB(printk("ad1848 detect step F(%d/%x/%x) - OPTi chip???\n", i, tmp1, tmp2));
			if (!ad1847_flag)
				optiC930 = 1;
			break;
		}
	}

	/*
	 * Try to switch the chip to mode2 (CS4231) by setting the MODE2 bit (0x40).
	 * The bit 0x80 is always 1 in CS4248 and CS4231.
	 */

	DDB(printk("ad1848_detect() - step G\n"));

	if (ad_flags && *ad_flags == 400)
		*ad_flags = 0;
	else
		ad_write(devc, 12, 0x40);	/* Set mode2, clear 0x80 */


	if (ad_flags)
		*ad_flags = 0;

	tmp1 = ad_read(devc, 12);
	if (tmp1 & 0x80)
	{
		if (ad_flags)
			*ad_flags |= AD_F_CS4248;

		devc->chip_name = "CS4248";	/* Our best knowledge just now */
	}
	if (optiC930 || (tmp1 & 0xc0) == (0x80 | 0x40))
	{
		/*
		 *      CS4231 detected - is it?
		 *
		 *      Verify that setting I0 doesn't change I16.
		 */
		
		DDB(printk("ad1848_detect() - step H\n"));
		ad_write(devc, 16, 0);	/* Set I16 to known value */

		ad_write(devc, 0, 0x45);
		if ((tmp1 = ad_read(devc, 16)) != 0x45)	/* No change -> CS4231? */
		{
			ad_write(devc, 0, 0xaa);
			if ((tmp1 = ad_read(devc, 16)) == 0xaa)	/* Rotten bits? */
			{
				DDB(printk("ad1848 detect error - step H(%x)\n", tmp1));
				return 0;
			}
			
			/*
			 * Verify that some bits of I25 are read only.
			 */

			DDB(printk("ad1848_detect() - step I\n"));
			tmp1 = ad_read(devc, 25);	/* Original bits */
			ad_write(devc, 25, ~tmp1);	/* Invert all bits */
			if ((ad_read(devc, 25) & 0xe7) == (tmp1 & 0xe7))
			{
				int id;

				/*
				 *      It's at least CS4231
				 */

				devc->chip_name = "CS4231";
				devc->model = MD_4231;
				
				/*
				 * It could be an AD1845 or CS4231A as well.
				 * CS4231 and AD1845 report the same revision info in I25
				 * while the CS4231A reports different.
				 */

				id = ad_read(devc, 25);
				if ((id & 0xe7) == 0x80)	/* Device busy??? */
					id = ad_read(devc, 25);
				if ((id & 0xe7) == 0x80)	/* Device still busy??? */
					id = ad_read(devc, 25);
				DDB(printk("ad1848_detect() - step J (%02x/%02x)\n", id, ad_read(devc, 25)));

                                if ((id & 0xe7) == 0x80) {
					/* 
					 * It must be a CS4231 or AD1845. The register I23 of
					 * CS4231 is undefined and it appears to be read only.
					 * AD1845 uses I23 for setting sample rate. Assume
					 * the chip is AD1845 if I23 is changeable.
					 */

					unsigned char   tmp = ad_read(devc, 23);
					ad_write(devc, 23, ~tmp);

					if (interwave)
					{
						devc->model = MD_IWAVE;
						devc->chip_name = "IWave";
					}
					else if (ad_read(devc, 23) != tmp)	/* AD1845 ? */
					{
						devc->chip_name = "AD1845";
						devc->model = MD_1845;
					}
					else if (cs4248_flag)
					{
						if (ad_flags)
							  *ad_flags |= AD_F_CS4248;
						devc->chip_name = "CS4248";
						devc->model = MD_1848;
						ad_write(devc, 12, ad_read(devc, 12) & ~0x40);	/* Mode2 off */
					}
					ad_write(devc, 23, tmp);	/* Restore */
				}
				else
				{
					switch (id & 0x1f) {
					case 3: /* CS4236/CS4235/CS42xB/CS4239 */
						{
							int xid;
							ad_write(devc, 12, ad_read(devc, 12) | 0x60); /* switch to mode 3 */
							ad_write(devc, 23, 0x9c); /* select extended register 25 */
							xid = inb(io_Indexed_Data(devc));
							ad_write(devc, 12, ad_read(devc, 12) & ~0x60); /* back to mode 0 */
							switch (xid & 0x1f)
							{
								case 0x00:
									devc->chip_name = "CS4237B(B)";
									devc->model = MD_42xB;
									break;
								case 0x08:
									/* Seems to be a 4238 ?? */
									devc->chip_name = "CS4238";
									devc->model = MD_42xB;
									break;
								case 0x09:
									devc->chip_name = "CS4238B";
									devc->model = MD_42xB;
									break;
								case 0x0b:
									devc->chip_name = "CS4236B";
									devc->model = MD_4236;
									break;
								case 0x10:
									devc->chip_name = "CS4237B";
									devc->model = MD_42xB;
									break;
								case 0x1d:
									devc->chip_name = "CS4235";
									devc->model = MD_4235;
									break;
								case 0x1e:
									devc->chip_name = "CS4239";
									devc->model = MD_4239;
									break;
								default:
									printk("Chip ident is %X.\n", xid&0x1F);
									devc->chip_name = "CS42xx";
									devc->model = MD_4232;
									break;
							}
						}
						break;

					case 2: /* CS4232/CS4232A */
						devc->chip_name = "CS4232";
						devc->model = MD_4232;
						break;
				
					case 0:
						if ((id & 0xe0) == 0xa0)
						{
							devc->chip_name = "CS4231A";
							devc->model = MD_4231A;
						}
						else
						{
							devc->chip_name = "CS4321";
							devc->model = MD_4231;
						}
						break;

					default: /* maybe */
						DDB(printk("ad1848: I25 = %02x/%02x\n", ad_read(devc, 25), ad_read(devc, 25) & 0xe7));
                                                if (optiC930)
                                                {
                                                        devc->chip_name = "82C930";
                                                        devc->model = MD_C930;
                                                }
						else
						{
							devc->chip_name = "CS4231";
							devc->model = MD_4231;
						}
					}
				}
			}
			ad_write(devc, 25, tmp1);	/* Restore bits */

			DDB(printk("ad1848_detect() - step K\n"));
		}
	} else if (tmp1 == 0x0a) {
		/*
		 * Is it perhaps a SoundPro CMI8330?
		 * If so, then we should be able to change indirect registers
		 * greater than I15 after activating MODE2, even though reading
		 * back I12 does not show it.
		 */

		/*
		 * Let's try comparing register values
		 */
		for (i = 0; i < 16; i++) {
			if ((tmp1 = ad_read(devc, i)) != (tmp2 = ad_read(devc, i + 16))) {
				DDB(printk("ad1848 detect step H(%d/%x/%x) - SoundPro chip?\n", i, tmp1, tmp2));
				soundpro = 1;
				devc->chip_name = "SoundPro CMI 8330";
				break;
			}
		}
	}

	DDB(printk("ad1848_detect() - step L\n"));
	if (ad_flags)
	{
		  if (devc->model != MD_1848)
			  *ad_flags |= AD_F_CS4231;
	}
	DDB(printk("ad1848_detect() - Detected OK\n"));

	if (devc->model == MD_1848 && ad1847_flag)
		devc->chip_name = "AD1847";


	if (sscape_flag == 1)
		devc->model = MD_1845_SSCAPE;

	return 1;
}

int ad1848_init (char *name, struct resource *ports, int irq, int dma_playback,
		int dma_capture, int share_dma, int *osp, struct module *owner)
{
	/*
	 * NOTE! If irq < 0, there is another driver which has allocated the IRQ
	 *   so that this driver doesn't need to allocate/deallocate it.
	 *   The actually used IRQ is ABS(irq).
	 */

	int my_dev;
	char dev_name[100];
	int e;

	ad1848_info  *devc = &adev_info[nr_ad1848_devs];

	ad1848_port_info *portc = NULL;

	devc->irq = (irq > 0) ? irq : 0;
	devc->open_mode = 0;
	devc->timer_ticks = 0;
	devc->dma1 = dma_playback;
	devc->dma2 = dma_capture;
	devc->subtype = cfg.card_subtype;
	devc->audio_flags = DMA_AUTOMODE;
	devc->playback_dev = devc->record_dev = 0;
	if (name != NULL)
		devc->name = name;

	if (name != NULL && name[0] != 0)
		sprintf(dev_name,
			"%s (%s)", name, devc->chip_name);
	else
		sprintf(dev_name,
			"Generic audio codec (%s)", devc->chip_name);

	rename_region(ports, devc->name);

	conf_printf2(dev_name, devc->base, devc->irq, dma_playback, dma_capture);

	if (devc->model == MD_1848 || devc->model == MD_C930)
		devc->audio_flags |= DMA_HARDSTOP;

	if (devc->model > MD_1848)
	{
		if (devc->dma1 == devc->dma2 || devc->dma2 == -1 || devc->dma1 == -1)
			devc->audio_flags &= ~DMA_DUPLEX;
		else
			devc->audio_flags |= DMA_DUPLEX;
	}

	portc = kmalloc(sizeof(ad1848_port_info), GFP_KERNEL);
	if(portc==NULL) {
		release_region(devc->base, 4);
		return -1;
	}

	if ((my_dev = sound_install_audiodrv(AUDIO_DRIVER_VERSION,
					     dev_name,
					     &ad1848_audio_driver,
					     sizeof(struct audio_driver),
					     devc->audio_flags,
					     ad_format_mask[devc->model],
					     devc,
					     dma_playback,
					     dma_capture)) < 0)
	{
		release_region(devc->base, 4);
		kfree(portc);
		return -1;
	}
	
	audio_devs[my_dev]->portc = portc;
	audio_devs[my_dev]->mixer_dev = -1;
	if (owner)
		audio_devs[my_dev]->d->owner = owner;
	memset((char *) portc, 0, sizeof(*portc));

	nr_ad1848_devs++;

	ad1848_init_hw(devc);

	if (irq > 0)
	{
		devc->dev_no = my_dev;
		if (request_irq(devc->irq, adintr, 0, devc->name,
				(void *)(long)my_dev) < 0)
		{
			printk(KERN_WARNING "ad1848: Unable to allocate IRQ\n");
			/* Don't free it either then.. */
			devc->irq = 0;
		}
		if (capabilities[devc->model].flags & CAP_F_TIMER)
		{
#ifndef CONFIG_SMP
			int x;
			unsigned char tmp = ad_read(devc, 16);
#endif			

			devc->timer_ticks = 0;

			ad_write(devc, 21, 0x00);	/* Timer MSB */
			ad_write(devc, 20, 0x10);	/* Timer LSB */
#ifndef CONFIG_SMP
			ad_write(devc, 16, tmp | 0x40);	/* Enable timer */
			for (x = 0; x < 100000 && devc->timer_ticks == 0; x++);
			ad_write(devc, 16, tmp & ~0x40);	/* Disable timer */

			if (devc->timer_ticks == 0)
				printk(KERN_WARNING "ad1848: Interrupt test failed (IRQ%d)\n", irq);
			else
			{
				DDB(printk("Interrupt test OK\n"));
				devc->irq_ok = 1;
			}
#else
			devc->irq_ok = 1;
#endif			
		}
		else
			devc->irq_ok = 1;	/* Couldn't test. assume it's OK */
	} else if (irq < 0)
		irq2dev[-irq] = devc->dev_no = my_dev;

#ifndef EXCLUDE_TIMERS
	if ((capabilities[devc->model].flags & CAP_F_TIMER) &&
	    devc->irq_ok)
		ad1848_tmr_install(my_dev);
#endif

	if (!share_dma)
	{
		if (sound_alloc_dma(dma_playback, devc->name))
			printk(KERN_WARNING "ad1848.c: Can't allocate DMA%d\n", dma_playback);

		if (dma_capture != dma_playback)
			if (sound_alloc_dma(dma_capture, devc->name))
				printk(KERN_WARNING "ad1848.c: Can't allocate DMA%d\n", dma_capture);
	}

	if ((e = sound_install_mixer(MIXER_DRIVER_VERSION,
				     dev_name,
				     &ad1848_mixer_operations,
				     sizeof(struct mixer_operations),
				     devc)) >= 0)
	{
		audio_devs[my_dev]->mixer_dev = e;
		if (owner)
			mixer_devs[e]->owner = owner;
	}
	return my_dev;
}

int ad1848_control(int cmd, int arg)
{
	ad1848_info *devc;
	unsigned long flags;

	if (nr_ad1848_devs < 1)
		return -ENODEV;

	devc = &adev_info[nr_ad1848_devs - 1];

	switch (cmd)
	{
		case AD1848_SET_XTAL:	/* Change clock frequency of AD1845 (only ) */
			if (devc->model != MD_1845 && devc->model != MD_1845_SSCAPE)
				return -EINVAL;
			spin_lock_irqsave(&devc->lock,flags);
			ad_enter_MCE(devc);
			ad_write(devc, 29, (ad_read(devc, 29) & 0x1f) | (arg << 5));
			ad_leave_MCE(devc);
			spin_unlock_irqrestore(&devc->lock,flags);
			break;

		case AD1848_MIXER_REROUTE:
		{
			int o = (arg >> 8) & 0xff;
			int n = arg & 0xff;

			if (o < 0 || o >= SOUND_MIXER_NRDEVICES)
				return -EINVAL;

			if (!(devc->supported_devices & (1 << o)) &&
			    !(devc->supported_rec_devices & (1 << o)))
				return -EINVAL;

			if (n == SOUND_MIXER_NONE)
			{	/* Just hide this control */
				ad1848_mixer_set(devc, o, 0);	/* Shut up it */
				devc->supported_devices &= ~(1 << o);
				devc->supported_rec_devices &= ~(1 << o);
				break;
			}

			/* Make the mixer control identified by o to appear as n */
			if (n < 0 || n >= SOUND_MIXER_NRDEVICES)
				return -EINVAL;

			devc->mixer_reroute[n] = o;	/* Rename the control */
			if (devc->supported_devices & (1 << o))
				devc->supported_devices |= (1 << n);
			if (devc->supported_rec_devices & (1 << o))
				devc->supported_rec_devices |= (1 << n);

			devc->supported_devices &= ~(1 << o);
			devc->supported_rec_devices &= ~(1 << o);
		}
		break;
	}
	return 0;
}

void ad1848_unload(int io_base, int irq, int dma_playback, int dma_capture, int share_dma)
{
	int i, mixer, dev = 0;
	ad1848_info *devc = NULL;

	for (i = 0; devc == NULL && i < nr_ad1848_devs; i++)
	{
		if (adev_info[i].base == io_base)
		{
			devc = &adev_info[i];
			dev = devc->dev_no;
		}
	}
		
	if (devc != NULL)
	{
		kfree(audio_devs[dev]->portc);
		release_region(devc->base, 4);

		if (!share_dma)
		{
			if (devc->irq > 0) /* There is no point in freeing irq, if it wasn't allocated */
				free_irq(devc->irq, (void *)(long)devc->dev_no);

			sound_free_dma(dma_playback);

			if (dma_playback != dma_capture)
				sound_free_dma(dma_capture);

		}
		mixer = audio_devs[devc->dev_no]->mixer_dev;
		if(mixer>=0)
			sound_unload_mixerdev(mixer);

		nr_ad1848_devs--;
		for ( ; i < nr_ad1848_devs ; i++)
			adev_info[i] = adev_info[i+1];
	}
	else
		printk(KERN_ERR "ad1848: Can't find device to be unloaded. Base=%x\n", io_base);
}

static irqreturn_t adintr(int irq, void *dev_id)
{
	unsigned char status;
	ad1848_info *devc;
	int dev;
	int alt_stat = 0xff;
	unsigned char c930_stat = 0;
	int cnt = 0;

	dev = (long)dev_id;
	devc = (ad1848_info *) audio_devs[dev]->devc;

interrupt_again:		/* Jump back here if int status doesn't reset */

	status = inb(io_Status(devc));

	if (status == 0x80)
		printk(KERN_DEBUG "adintr: Why?\n");
	if (devc->model == MD_1848)
		outb((0), io_Status(devc));	/* Clear interrupt status */

	if (status & 0x01)
	{
		if (devc->model == MD_C930)
		{		/* 82C930 has interrupt status register in MAD16 register MC11 */

			spin_lock(&devc->lock);

			/* 0xe0e is C930 address port
			 * 0xe0f is C930 data port
			 */
			outb(11, 0xe0e);
			c930_stat = inb(0xe0f);
			outb((~c930_stat), 0xe0f);

			spin_unlock(&devc->lock);

			alt_stat = (c930_stat << 2) & 0x30;
		}
		else if (devc->model != MD_1848)
		{
			spin_lock(&devc->lock);
			alt_stat = ad_read(devc, 24);
			ad_write(devc, 24, ad_read(devc, 24) & ~alt_stat);	/* Selective ack */
			spin_unlock(&devc->lock);
		}

		if ((devc->open_mode & OPEN_READ) && (devc->audio_mode & PCM_ENABLE_INPUT) && (alt_stat & 0x20))
		{
			DMAbuf_inputintr(devc->record_dev);
		}
		if ((devc->open_mode & OPEN_WRITE) && (devc->audio_mode & PCM_ENABLE_OUTPUT) &&
		      (alt_stat & 0x10))
		{
			DMAbuf_outputintr(devc->playback_dev, 1);
		}
		if (devc->model != MD_1848 && (alt_stat & 0x40))	/* Timer interrupt */
		{
			devc->timer_ticks++;
#ifndef EXCLUDE_TIMERS
			if (timer_installed == dev && devc->timer_running)
				sound_timer_interrupt();
#endif
		}
	}
/*
 * Sometimes playback or capture interrupts occur while a timer interrupt
 * is being handled. The interrupt will not be retriggered if we don't
 * handle it now. Check if an interrupt is still pending and restart
 * the handler in this case.
 */
	if (inb(io_Status(devc)) & 0x01 && cnt++ < 4)
	{
		  goto interrupt_again;
	}
	return IRQ_HANDLED;
}

/*
 *	Experimental initialization sequence for the integrated sound system
 *	of the Compaq Deskpro M.
 */

static int init_deskpro_m(struct address_info *hw_config)
{
	unsigned char   tmp;

	if ((tmp = inb(0xc44)) == 0xff)
	{
		DDB(printk("init_deskpro_m: Dead port 0xc44\n"));
		return 0;
	}

	outb(0x10, 0xc44);
	outb(0x40, 0xc45);
	outb(0x00, 0xc46);
	outb(0xe8, 0xc47);
	outb(0x14, 0xc44);
	outb(0x40, 0xc45);
	outb(0x00, 0xc46);
	outb(0xe8, 0xc47);
	outb(0x10, 0xc44);

	return 1;
}

/*
 *	Experimental initialization sequence for the integrated sound system
 *	of Compaq Deskpro XL.
 */

static int init_deskpro(struct address_info *hw_config)
{
	unsigned char   tmp;

	if ((tmp = inb(0xc44)) == 0xff)
	{
		DDB(printk("init_deskpro: Dead port 0xc44\n"));
		return 0;
	}
	outb((tmp | 0x04), 0xc44);	/* Select bank 1 */
	if (inb(0xc44) != 0x04)
	{
		DDB(printk("init_deskpro: Invalid bank1 signature in port 0xc44\n"));
		return 0;
	}
	/*
	 * OK. It looks like a Deskpro so let's proceed.
	 */

	/*
	 * I/O port 0xc44 Audio configuration register.
	 *
	 * bits 0xc0:   Audio revision bits
	 *              0x00 = Compaq Business Audio
	 *              0x40 = MS Sound System Compatible (reset default)
	 *              0x80 = Reserved
	 *              0xc0 = Reserved
	 * bit 0x20:    No Wait State Enable
	 *              0x00 = Disabled (reset default, DMA mode)
	 *              0x20 = Enabled (programmed I/O mode)
	 * bit 0x10:    MS Sound System Decode Enable
	 *              0x00 = Decoding disabled (reset default)
	 *              0x10 = Decoding enabled
	 * bit 0x08:    FM Synthesis Decode Enable
	 *              0x00 = Decoding Disabled (reset default)
	 *              0x08 = Decoding enabled
	 * bit 0x04     Bank select
	 *              0x00 = Bank 0
	 *              0x04 = Bank 1
	 * bits 0x03    MSS Base address
	 *              0x00 = 0x530 (reset default)
	 *              0x01 = 0x604
	 *              0x02 = 0xf40
	 *              0x03 = 0xe80
	 */

#ifdef DEBUGXL
	/* Debug printing */
	printk("Port 0xc44 (before): ");
	outb((tmp & ~0x04), 0xc44);
	printk("%02x ", inb(0xc44));
	outb((tmp | 0x04), 0xc44);
	printk("%02x\n", inb(0xc44));
#endif

	/* Set bank 1 of the register */
	tmp = 0x58;		/* MSS Mode, MSS&FM decode enabled */

	switch (hw_config->io_base)
	{
		case 0x530:
			tmp |= 0x00;
			break;
		case 0x604:
			tmp |= 0x01;
			break;
		case 0xf40:
			tmp |= 0x02;
			break;
		case 0xe80:
			tmp |= 0x03;
			break;
		default:
			DDB(printk("init_deskpro: Invalid MSS port %x\n", hw_config->io_base));
			return 0;
	}
	outb((tmp & ~0x04), 0xc44);	/* Write to bank=0 */

#ifdef DEBUGXL
	/* Debug printing */
	printk("Port 0xc44 (after): ");
	outb((tmp & ~0x04), 0xc44);	/* Select bank=0 */
	printk("%02x ", inb(0xc44));
	outb((tmp | 0x04), 0xc44);	/* Select bank=1 */
	printk("%02x\n", inb(0xc44));
#endif

	/*
	 * I/O port 0xc45 FM Address Decode/MSS ID Register.
	 *
	 * bank=0, bits 0xfe:   FM synthesis Decode Compare bits 7:1 (default=0x88)
	 * bank=0, bit 0x01:    SBIC Power Control Bit
	 *                      0x00 = Powered up
	 *                      0x01 = Powered down
	 * bank=1, bits 0xfc:   MSS ID (default=0x40)
	 */

#ifdef DEBUGXL
	/* Debug printing */
	printk("Port 0xc45 (before): ");
	outb((tmp & ~0x04), 0xc44);	/* Select bank=0 */
	printk("%02x ", inb(0xc45));
	outb((tmp | 0x04), 0xc44);	/* Select bank=1 */
	printk("%02x\n", inb(0xc45));
#endif

	outb((tmp & ~0x04), 0xc44);	/* Select bank=0 */
	outb((0x88), 0xc45);	/* FM base 7:0 = 0x88 */
	outb((tmp | 0x04), 0xc44);	/* Select bank=1 */
	outb((0x10), 0xc45);	/* MSS ID = 0x10 (MSS port returns 0x04) */

#ifdef DEBUGXL
	/* Debug printing */
	printk("Port 0xc45 (after): ");
	outb((tmp & ~0x04), 0xc44);	/* Select bank=0 */
	printk("%02x ", inb(0xc45));
	outb((tmp | 0x04), 0xc44);	/* Select bank=1 */
	printk("%02x\n", inb(0xc45));
#endif


	/*
	 * I/O port 0xc46 FM Address Decode/Address ASIC Revision Register.
	 *
	 * bank=0, bits 0xff:   FM synthesis Decode Compare bits 15:8 (default=0x03)
	 * bank=1, bits 0xff:   Audio addressing ASIC id
	 */

#ifdef DEBUGXL
	/* Debug printing */
	printk("Port 0xc46 (before): ");
	outb((tmp & ~0x04), 0xc44);	/* Select bank=0 */
	printk("%02x ", inb(0xc46));
	outb((tmp | 0x04), 0xc44);	/* Select bank=1 */
	printk("%02x\n", inb(0xc46));
#endif

	outb((tmp & ~0x04), 0xc44);	/* Select bank=0 */
	outb((0x03), 0xc46);	/* FM base 15:8 = 0x03 */
	outb((tmp | 0x04), 0xc44);	/* Select bank=1 */
	outb((0x11), 0xc46);	/* ASIC ID = 0x11 */

#ifdef DEBUGXL
	/* Debug printing */
	printk("Port 0xc46 (after): ");
	outb((tmp & ~0x04), 0xc44);	/* Select bank=0 */
	printk("%02x ", inb(0xc46));
	outb((tmp | 0x04), 0xc44);	/* Select bank=1 */
	printk("%02x\n", inb(0xc46));
#endif

	/*
	 * I/O port 0xc47 FM Address Decode Register.
	 *
	 * bank=0, bits 0xff:   Decode enable selection for various FM address bits
	 * bank=1, bits 0xff:   Reserved
	 */

#ifdef DEBUGXL
	/* Debug printing */
	printk("Port 0xc47 (before): ");
	outb((tmp & ~0x04), 0xc44);	/* Select bank=0 */
	printk("%02x ", inb(0xc47));
	outb((tmp | 0x04), 0xc44);	/* Select bank=1 */
	printk("%02x\n", inb(0xc47));
#endif

	outb((tmp & ~0x04), 0xc44);	/* Select bank=0 */
	outb((0x7c), 0xc47);	/* FM decode enable bits = 0x7c */
	outb((tmp | 0x04), 0xc44);	/* Select bank=1 */
	outb((0x00), 0xc47);	/* Reserved bank1 = 0x00 */

#ifdef DEBUGXL
	/* Debug printing */
	printk("Port 0xc47 (after): ");
	outb((tmp & ~0x04), 0xc44);	/* Select bank=0 */
	printk("%02x ", inb(0xc47));
	outb((tmp | 0x04), 0xc44);	/* Select bank=1 */
	printk("%02x\n", inb(0xc47));
#endif

	/*
	 * I/O port 0xc6f = Audio Disable Function Register
	 */

#ifdef DEBUGXL
	printk("Port 0xc6f (before) = %02x\n", inb(0xc6f));
#endif

	outb((0x80), 0xc6f);

#ifdef DEBUGXL
	printk("Port 0xc6f (after) = %02x\n", inb(0xc6f));
#endif

	return 1;
}

int probe_ms_sound(struct address_info *hw_config, struct resource *ports)
{
	unsigned char   tmp;

	DDB(printk("Entered probe_ms_sound(%x, %d)\n", hw_config->io_base, hw_config->card_subtype));

	if (hw_config->card_subtype == 1)	/* Has no IRQ/DMA registers */
	{
		/* check_opl3(0x388, hw_config); */
		return ad1848_detect(ports, NULL, hw_config->osp);
	}

	if (deskpro_xl && hw_config->card_subtype == 2)	/* Compaq Deskpro XL */
	{
		if (!init_deskpro(hw_config))
			return 0;
	}

	if (deskpro_m)	/* Compaq Deskpro M */
	{
		if (!init_deskpro_m(hw_config))
			return 0;
	}

	/*
	   * Check if the IO port returns valid signature. The original MS Sound
	   * system returns 0x04 while some cards (AudioTrix Pro for example)
	   * return 0x00 or 0x0f.
	 */

	if ((tmp = inb(hw_config->io_base + 3)) == 0xff)	/* Bus float */
	{
		  int             ret;

		  DDB(printk("I/O address is inactive (%x)\n", tmp));
		  if (!(ret = ad1848_detect(ports, NULL, hw_config->osp)))
			  return 0;
		  return 1;
	}
	DDB(printk("MSS signature = %x\n", tmp & 0x3f));
	if ((tmp & 0x3f) != 0x04 &&
	    (tmp & 0x3f) != 0x0f &&
	    (tmp & 0x3f) != 0x00)
	{
		int ret;

		MDB(printk(KERN_ERR "No MSS signature detected on port 0x%x (0x%x)\n", hw_config->io_base, (int) inb(hw_config->io_base + 3)));
		DDB(printk("Trying to detect codec anyway but IRQ/DMA may not work\n"));
		if (!(ret = ad1848_detect(ports, NULL, hw_config->osp)))
			return 0;

		hw_config->card_subtype = 1;
		return 1;
	}
	if ((hw_config->irq != 5)  &&
	    (hw_config->irq != 7)  &&
	    (hw_config->irq != 9)  &&
	    (hw_config->irq != 10) &&
	    (hw_config->irq != 11) &&
	    (hw_config->irq != 12))
	{
		printk(KERN_ERR "MSS: Bad IRQ %d\n", hw_config->irq);
		return 0;
	}
	if (hw_config->dma != 0 && hw_config->dma != 1 && hw_config->dma != 3)
	{
		  printk(KERN_ERR "MSS: Bad DMA %d\n", hw_config->dma);
		  return 0;
	}
	/*
	 * Check that DMA0 is not in use with a 8 bit board.
	 */

	if (hw_config->dma == 0 && inb(hw_config->io_base + 3) & 0x80)
	{
		printk(KERN_ERR "MSS: Can't use DMA0 with a 8 bit card/slot\n");
		return 0;
	}
	if (hw_config->irq > 7 && hw_config->irq != 9 && inb(hw_config->io_base + 3) & 0x80)
	{
		printk(KERN_ERR "MSS: Can't use IRQ%d with a 8 bit card/slot\n", hw_config->irq);
		return 0;
	}
	return ad1848_detect(ports, NULL, hw_config->osp);
}

void attach_ms_sound(struct address_info *hw_config, struct resource *ports, struct module *owner)
{
	static signed char interrupt_bits[12] =
	{
		-1, -1, -1, -1, -1, 0x00, -1, 0x08, -1, 0x10, 0x18, 0x20
	};
	signed char     bits;
	char            dma2_bit = 0;

	static char     dma_bits[4] =
	{
		1, 2, 0, 3
	};

	int config_port = hw_config->io_base + 0;
	int version_port = hw_config->io_base + 3;
	int dma = hw_config->dma;
	int dma2 = hw_config->dma2;

	if (hw_config->card_subtype == 1)	/* Has no IRQ/DMA registers */
	{
		hw_config->slots[0] = ad1848_init("MS Sound System", ports,
						    hw_config->irq,
						    hw_config->dma,
						    hw_config->dma2, 0, 
						    hw_config->osp,
						    owner);
		return;
	}
	/*
	 * Set the IRQ and DMA addresses.
	 */

	bits = interrupt_bits[hw_config->irq];
	if (bits == -1)
	{
		printk(KERN_ERR "MSS: Bad IRQ %d\n", hw_config->irq);
		release_region(ports->start, 4);
		release_region(ports->start - 4, 4);
		return;
	}
	outb((bits | 0x40), config_port);
	if ((inb(version_port) & 0x40) == 0)
		printk(KERN_ERR "[MSS: IRQ Conflict?]\n");

/*
 * Handle the capture DMA channel
 */

	if (dma2 != -1 && dma2 != dma)
	{
		if (!((dma == 0 && dma2 == 1) ||
			(dma == 1 && dma2 == 0) ||
			(dma == 3 && dma2 == 0)))
		{	/* Unsupported combination. Try to swap channels */
			int tmp = dma;

			dma = dma2;
			dma2 = tmp;
		}
		if ((dma == 0 && dma2 == 1) ||
			(dma == 1 && dma2 == 0) ||
			(dma == 3 && dma2 == 0))
		{
			dma2_bit = 0x04;	/* Enable capture DMA */
		}
		else
		{
			printk(KERN_WARNING "MSS: Invalid capture DMA\n");
			dma2 = dma;
		}
	}
	else
	{
		dma2 = dma;
	}

	hw_config->dma = dma;
	hw_config->dma2 = dma2;

	outb((bits | dma_bits[dma] | dma2_bit), config_port);	/* Write IRQ+DMA setup */

	hw_config->slots[0] = ad1848_init("MS Sound System", ports,
					  hw_config->irq,
					  dma, dma2, 0,
					  hw_config->osp,
					  THIS_MODULE);
}

void unload_ms_sound(struct address_info *hw_config)
{
	ad1848_unload(hw_config->io_base + 4,
		      hw_config->irq,
		      hw_config->dma,
		      hw_config->dma2, 0);
	sound_unload_audiodev(hw_config->slots[0]);
	release_region(hw_config->io_base, 4);
}

#ifndef EXCLUDE_TIMERS

/*
 * Timer stuff (for /dev/music).
 */

static unsigned int current_interval;

static unsigned int ad1848_tmr_start(int dev, unsigned int usecs)
{
	unsigned long   flags;
	ad1848_info    *devc = (ad1848_info *) audio_devs[dev]->devc;
	unsigned long   xtal_nsecs;	/* nanoseconds per xtal oscillator tick */
	unsigned long   divider;

	spin_lock_irqsave(&devc->lock,flags);

	/*
	 * Length of the timer interval (in nanoseconds) depends on the
	 * selected crystal oscillator. Check this from bit 0x01 of I8.
	 *
	 * AD1845 has just one oscillator which has cycle time of 10.050 us
	 * (when a 24.576 MHz xtal oscillator is used).
	 *
	 * Convert requested interval to nanoseconds before computing
	 * the timer divider.
	 */

	if (devc->model == MD_1845 || devc->model == MD_1845_SSCAPE)
		xtal_nsecs = 10050;
	else if (ad_read(devc, 8) & 0x01)
		xtal_nsecs = 9920;
	else
		xtal_nsecs = 9969;

	divider = (usecs * 1000 + xtal_nsecs / 2) / xtal_nsecs;

	if (divider < 100)	/* Don't allow shorter intervals than about 1ms */
		divider = 100;

	if (divider > 65535)	/* Overflow check */
		divider = 65535;

	ad_write(devc, 21, (divider >> 8) & 0xff);	/* Set upper bits */
	ad_write(devc, 20, divider & 0xff);	/* Set lower bits */
	ad_write(devc, 16, ad_read(devc, 16) | 0x40);	/* Start the timer */
	devc->timer_running = 1;
	spin_unlock_irqrestore(&devc->lock,flags);

	return current_interval = (divider * xtal_nsecs + 500) / 1000;
}

static void ad1848_tmr_reprogram(int dev)
{
	/*
	 *    Audio driver has changed sampling rate so that a different xtal
	 *      oscillator was selected. We have to reprogram the timer rate.
	 */

	ad1848_tmr_start(dev, current_interval);
	sound_timer_syncinterval(current_interval);
}

static void ad1848_tmr_disable(int dev)
{
	unsigned long   flags;
	ad1848_info    *devc = (ad1848_info *) audio_devs[dev]->devc;

	spin_lock_irqsave(&devc->lock,flags);
	ad_write(devc, 16, ad_read(devc, 16) & ~0x40);
	devc->timer_running = 0;
	spin_unlock_irqrestore(&devc->lock,flags);
}

static void ad1848_tmr_restart(int dev)
{
	unsigned long   flags;
	ad1848_info    *devc = (ad1848_info *) audio_devs[dev]->devc;

	if (current_interval == 0)
		return;

	spin_lock_irqsave(&devc->lock,flags);
	ad_write(devc, 16, ad_read(devc, 16) | 0x40);
	devc->timer_running = 1;
	spin_unlock_irqrestore(&devc->lock,flags);
}

static struct sound_lowlev_timer ad1848_tmr =
{
	0,
	2,
	ad1848_tmr_start,
	ad1848_tmr_disable,
	ad1848_tmr_restart
};

static int ad1848_tmr_install(int dev)
{
	if (timer_installed != -1)
		return 0;	/* Don't install another timer */

	timer_installed = ad1848_tmr.dev = dev;
	sound_timer_init(&ad1848_tmr, audio_devs[dev]->name);

	return 1;
}
#endif /* EXCLUDE_TIMERS */

EXPORT_SYMBOL(ad1848_detect);
EXPORT_SYMBOL(ad1848_init);
EXPORT_SYMBOL(ad1848_unload);
EXPORT_SYMBOL(ad1848_control);
EXPORT_SYMBOL(probe_ms_sound);
EXPORT_SYMBOL(attach_ms_sound);
EXPORT_SYMBOL(unload_ms_sound);

static int __initdata io = -1;
static int __initdata irq = -1;
static int __initdata dma = -1;
static int __initdata dma2 = -1;
static int __initdata type = 0;

module_param(io, int, 0);		/* I/O for a raw AD1848 card */
module_param(irq, int, 0);		/* IRQ to use */
module_param(dma, int, 0);		/* First DMA channel */
module_param(dma2, int, 0);		/* Second DMA channel */
module_param(type, int, 0);		/* Card type */
module_param(deskpro_xl, bool, 0);	/* Special magic for Deskpro XL boxen */
module_param(deskpro_m, bool, 0);	/* Special magic for Deskpro M box */
module_param(soundpro, bool, 0);	/* More special magic for SoundPro chips */

#ifdef CONFIG_PNP
module_param(isapnp, int, 0);
module_param(isapnpjump, int, 0);
module_param(reverse, bool, 0);
MODULE_PARM_DESC(isapnp,	"When set to 0, Plug & Play support will be disabled");
MODULE_PARM_DESC(isapnpjump,	"Jumps to a specific slot in the driver's PnP table. Use the source, Luke.");
MODULE_PARM_DESC(reverse,	"When set to 1, will reverse ISAPnP search order");

static struct pnp_dev	*ad1848_dev  = NULL;

/* Please add new entries at the end of the table */
static struct {
	char *name;
	unsigned short	card_vendor, card_device,
			vendor, function;
	short mss_io, irq, dma, dma2;   /* index into isapnp table */
        int type;
} ad1848_isapnp_list[] __initdata = {
	{"CMI 8330 SoundPRO",
		ISAPNP_VENDOR('C','M','I'), ISAPNP_DEVICE(0x0001),
		ISAPNP_VENDOR('@','@','@'), ISAPNP_FUNCTION(0x0001),
		0, 0, 0,-1, 0},
        {"CS4232 based card",
                ISAPNP_ANY_ID, ISAPNP_ANY_ID,
		ISAPNP_VENDOR('C','S','C'), ISAPNP_FUNCTION(0x0000),
		0, 0, 0, 1, 0},
        {"CS4232 based card",
                ISAPNP_ANY_ID, ISAPNP_ANY_ID,
		ISAPNP_VENDOR('C','S','C'), ISAPNP_FUNCTION(0x0100),
		0, 0, 0, 1, 0},
        {"OPL3-SA2 WSS mode",
        	ISAPNP_ANY_ID, ISAPNP_ANY_ID,
		ISAPNP_VENDOR('Y','M','H'), ISAPNP_FUNCTION(0x0021),
                1, 0, 0, 1, 1},
	{"Advanced Gravis InterWave Audio",
		ISAPNP_VENDOR('G','R','V'), ISAPNP_DEVICE(0x0001),
		ISAPNP_VENDOR('G','R','V'), ISAPNP_FUNCTION(0x0000),
		0, 0, 0, 1, 0},
	{NULL}
};

static struct isapnp_device_id id_table[] __devinitdata = {
	{	ISAPNP_VENDOR('C','M','I'), ISAPNP_DEVICE(0x0001),
		ISAPNP_VENDOR('@','@','@'), ISAPNP_FUNCTION(0x0001), 0 },
        {       ISAPNP_ANY_ID, ISAPNP_ANY_ID,
		ISAPNP_VENDOR('C','S','C'), ISAPNP_FUNCTION(0x0000), 0 },
        {       ISAPNP_ANY_ID, ISAPNP_ANY_ID,
		ISAPNP_VENDOR('C','S','C'), ISAPNP_FUNCTION(0x0100), 0 },
	/* The main driver for this card is opl3sa2
        {       ISAPNP_ANY_ID, ISAPNP_ANY_ID,
		ISAPNP_VENDOR('Y','M','H'), ISAPNP_FUNCTION(0x0021), 0 },
	*/
	{	ISAPNP_VENDOR('G','R','V'), ISAPNP_DEVICE(0x0001),
		ISAPNP_VENDOR('G','R','V'), ISAPNP_FUNCTION(0x0000), 0 },
	{0}
};

MODULE_DEVICE_TABLE(isapnp, id_table);

static struct pnp_dev *activate_dev(char *devname, char *resname, struct pnp_dev *dev)
{
	int err;

	err = pnp_device_attach(dev);
	if (err < 0)
		return(NULL);

	if((err = pnp_activate_dev(dev)) < 0) {
		printk(KERN_ERR "ad1848: %s %s config failed (out of resources?)[%d]\n", devname, resname, err);

		pnp_device_detach(dev);

		return(NULL);
	}
	audio_activated = 1;
	return(dev);
}

static struct pnp_dev __init *ad1848_init_generic(struct pnp_card *bus,
				struct address_info *hw_config, int slot)
{

	/* Configure Audio device */
	if((ad1848_dev = pnp_find_dev(bus, ad1848_isapnp_list[slot].vendor, ad1848_isapnp_list[slot].function, NULL)))
	{
		if((ad1848_dev = activate_dev(ad1848_isapnp_list[slot].name, "ad1848", ad1848_dev)))
		{
			hw_config->io_base 	= pnp_port_start(ad1848_dev, ad1848_isapnp_list[slot].mss_io);
			hw_config->irq 		= pnp_irq(ad1848_dev, ad1848_isapnp_list[slot].irq);
			hw_config->dma 		= pnp_dma(ad1848_dev, ad1848_isapnp_list[slot].dma);
			if(ad1848_isapnp_list[slot].dma2 != -1)
				hw_config->dma2 = pnp_dma(ad1848_dev, ad1848_isapnp_list[slot].dma2);
			else
				hw_config->dma2 = -1;
                        hw_config->card_subtype = ad1848_isapnp_list[slot].type;
		} else
			return(NULL);
	} else
		return(NULL);

	return(ad1848_dev);
}

static int __init ad1848_isapnp_init(struct address_info *hw_config, struct pnp_card *bus, int slot)
{
	char *busname = bus->name[0] ? bus->name : ad1848_isapnp_list[slot].name;

	/* Initialize this baby. */

	if(ad1848_init_generic(bus, hw_config, slot)) {
		/* We got it. */

		printk(KERN_NOTICE "ad1848: PnP reports '%s' at i/o %#x, irq %d, dma %d, %d\n",
		       busname,
		       hw_config->io_base, hw_config->irq, hw_config->dma,
		       hw_config->dma2);
		return 1;
	}
	return 0;
}

static int __init ad1848_isapnp_probe(struct address_info *hw_config)
{
	static int first = 1;
	int i;

	/* Count entries in sb_isapnp_list */
	for (i = 0; ad1848_isapnp_list[i].card_vendor != 0; i++);
	i--;

	/* Check and adjust isapnpjump */
	if( isapnpjump < 0 || isapnpjump > i) {
		isapnpjump = reverse ? i : 0;
		printk(KERN_ERR "ad1848: Valid range for isapnpjump is 0-%d. Adjusted to %d.\n", i, isapnpjump);
	}

	if(!first || !reverse)
		i = isapnpjump;
	first = 0;
	while(ad1848_isapnp_list[i].card_vendor != 0) {
		static struct pnp_card *bus = NULL;

		while ((bus = pnp_find_card(
				ad1848_isapnp_list[i].card_vendor,
				ad1848_isapnp_list[i].card_device,
				bus))) {

			if(ad1848_isapnp_init(hw_config, bus, i)) {
				isapnpjump = i; /* start next search from here */
				return 0;
			}
		}
		i += reverse ? -1 : 1;
	}

	return -ENODEV;
}
#endif


static int __init init_ad1848(void)
{
	printk(KERN_INFO "ad1848/cs4248 codec driver Copyright (C) by Hannu Savolainen 1993-1996\n");

#ifdef CONFIG_PNP
	if(isapnp && (ad1848_isapnp_probe(&cfg) < 0) ) {
		printk(KERN_NOTICE "ad1848: No ISAPnP cards found, trying standard ones...\n");
		isapnp = 0;
	}
#endif

	if(io != -1) {
		struct resource *ports;
	        if( isapnp == 0 )
	        {
			if(irq == -1 || dma == -1) {
				printk(KERN_WARNING "ad1848: must give I/O , IRQ and DMA.\n");
				return -EINVAL;
			}

			cfg.irq = irq;
			cfg.io_base = io;
			cfg.dma = dma;
			cfg.dma2 = dma2;
			cfg.card_subtype = type;
	        }

		ports = request_region(io + 4, 4, "ad1848");

		if (!ports)
			return -EBUSY;

		if (!request_region(io, 4, "WSS config")) {
			release_region(io + 4, 4);
			return -EBUSY;
		}

		if (!probe_ms_sound(&cfg, ports)) {
			release_region(io + 4, 4);
			release_region(io, 4);
			return -ENODEV;
		}
		attach_ms_sound(&cfg, ports, THIS_MODULE);
		loaded = 1;
	}
	return 0;
}

static void __exit cleanup_ad1848(void)
{
	if(loaded)
		unload_ms_sound(&cfg);

#ifdef CONFIG_PNP
	if(ad1848_dev){
		if(audio_activated)
			pnp_device_detach(ad1848_dev);
	}
#endif
}

module_init(init_ad1848);
module_exit(cleanup_ad1848);

#ifndef MODULE
static int __init setup_ad1848(char *str)
{
        /* io, irq, dma, dma2, type */
	int ints[6];
	
	str = get_options(str, ARRAY_SIZE(ints), ints);

	io	= ints[1];
	irq	= ints[2];
	dma	= ints[3];
	dma2	= ints[4];
	type	= ints[5];

	return 1;
}

__setup("ad1848=", setup_ad1848);	
#endif
MODULE_LICENSE("GPL");
