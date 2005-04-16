/*
 *
 * AD1816 lowlevel sound driver for Linux 2.6.0 and above
 *
 * Copyright (C) 1998-2003 by Thorsten Knabe <linux@thorsten-knabe.de>
 *
 * Based on the CS4232/AD1848 driver Copyright (C) by Hannu Savolainen 1993-1996
 *
 *
 * version: 1.5
 * status: beta
 * date: 2003/07/15
 *
 * Changes:
 *	Oleg Drokin: Some cleanup of load/unload functions.	1998/11/24
 *	
 *	Thorsten Knabe: attach and unload rewritten, 
 *	some argument checks added				1998/11/30
 *
 *	Thorsten Knabe: Buggy isa bridge workaround added	1999/01/16
 *	
 *	David Moews/Thorsten Knabe: Introduced options 
 *	parameter. Added slightly modified patch from 
 *	David Moews to disable dsp audio sources by setting 
 *	bit 0 of options parameter. This seems to be
 *	required by some Aztech/Newcom SC-16 cards.		1999/04/18
 *
 *	Christoph Hellwig: Adapted to module_init/module_exit.	2000/03/03
 *
 *	Christoph Hellwig: Added isapnp support			2000/03/15
 *
 *	Arnaldo Carvalho de Melo: get rid of check_region	2001/10/07
 *      
 *      Thorsten Knabe: Compiling with CONFIG_PNP enabled
 *	works again. It is now possible to use more than one 
 *	AD1816 sound card. Sample rate now may be changed during
 *	playback/capture. printk() uses log levels everywhere.
 *	SMP fixes. DMA handling fixes.
 *	Other minor code cleanup.				2003/07/15
 *
 */


#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/isapnp.h>
#include <linux/stddef.h>
#include <linux/spinlock.h>
#include "sound_config.h"

#define DEBUGNOISE(x)

#define CHECK_FOR_POWER { int timeout=100; \
  while (timeout > 0 && (inb(devc->base)&0x80)!= 0x80) {\
          timeout--; \
  } \
  if (timeout==0) {\
          printk(KERN_WARNING "ad1816: Check for power failed in %s line: %d\n",__FILE__,__LINE__); \
  } \
}

/* structure to hold device specific information */
typedef struct
{
        int            base;          /* set in attach */
	int            irq;
	int            dma_playback;
        int            dma_capture;
  
	int            opened;         /* open */
        int            speed;	
	int            channels;
	int            audio_format;
        int            audio_mode; 
  
        int            recmask;        /* setup */
	unsigned char  format_bits;
	int            supported_devices;
	int            supported_rec_devices;
	unsigned short levels[SOUND_MIXER_NRDEVICES];
					/* misc */
	struct pnp_dev *pnpdev;	 /* configured via pnp */
        int            dev_no;   /* this is the # in audio_devs and NOT 
				    in ad1816_info */
	spinlock_t	lock;  
} ad1816_info;

static int nr_ad1816_devs;
static int ad1816_clockfreq = 33000;
static int options;

/* supported audio formats */
static int  ad_format_mask =
AFMT_U8 | AFMT_S16_LE | AFMT_S16_BE | AFMT_MU_LAW | AFMT_A_LAW;

/* array of device info structures */
static ad1816_info dev_info[MAX_AUDIO_DEV];


/* ------------------------------------------------------------------- */

/* functions for easier access to inderect registers */

static int ad_read (ad1816_info * devc, int reg)
{
	int result;
	
	CHECK_FOR_POWER;
	outb ((unsigned char) (reg & 0x3f), devc->base+0);
	result = inb(devc->base+2);
	result+= inb(devc->base+3)<<8;
	return (result);
}


static void ad_write (ad1816_info * devc, int reg, int data)
{
	CHECK_FOR_POWER;
	outb ((unsigned char) (reg & 0xff), devc->base+0);
	outb ((unsigned char) (data & 0xff),devc->base+2);
	outb ((unsigned char) ((data>>8)&0xff),devc->base+3);
}

/* ------------------------------------------------------------------- */

/* function interface required by struct audio_driver */

static void ad1816_halt_input (int dev)
{
	unsigned long flags;
	ad1816_info    *devc = (ad1816_info *) audio_devs[dev]->devc;
	unsigned char buffer;
	
	DEBUGNOISE(printk(KERN_DEBUG "ad1816: halt_input called\n"));
	
	spin_lock_irqsave(&devc->lock,flags); 
	
	if(!isa_dma_bridge_buggy) {
	        disable_dma(audio_devs[dev]->dmap_in->dma);
	}
	
	buffer=inb(devc->base+9);
	if (buffer & 0x01) {
		/* disable capture */
		outb(buffer & ~0x01,devc->base+9); 
	}

	if(!isa_dma_bridge_buggy) {
	        enable_dma(audio_devs[dev]->dmap_in->dma);
	}

	/* Clear interrupt status */
	outb (~0x40, devc->base+1);	
	
	devc->audio_mode &= ~PCM_ENABLE_INPUT;
	spin_unlock_irqrestore(&devc->lock,flags);
}

static void ad1816_halt_output (int dev)
{
	unsigned long  flags;
	ad1816_info    *devc = (ad1816_info *) audio_devs[dev]->devc;
	
	unsigned char buffer;

	DEBUGNOISE(printk(KERN_DEBUG "ad1816: halt_output called!\n"));

	spin_lock_irqsave(&devc->lock,flags); 
	/* Mute pcm output */
	ad_write(devc, 4, ad_read(devc,4)|0x8080);

	if(!isa_dma_bridge_buggy) {
	        disable_dma(audio_devs[dev]->dmap_out->dma);
	}

	buffer=inb(devc->base+8);
	if (buffer & 0x01) {
		/* disable capture */
		outb(buffer & ~0x01,devc->base+8); 
	}

	if(!isa_dma_bridge_buggy) {
	        enable_dma(audio_devs[dev]->dmap_out->dma);
	}

	/* Clear interrupt status */
	outb ((unsigned char)~0x80, devc->base+1);	

	devc->audio_mode &= ~PCM_ENABLE_OUTPUT;
	spin_unlock_irqrestore(&devc->lock,flags);
}

static void ad1816_output_block (int dev, unsigned long buf, 
				 int count, int intrflag)
{
	unsigned long flags;
	unsigned long cnt;
	ad1816_info    *devc = (ad1816_info *) audio_devs[dev]->devc;
	
	DEBUGNOISE(printk(KERN_DEBUG "ad1816: output_block called buf=%ld count=%d flags=%d\n",buf,count,intrflag));
  
	cnt = count/4 - 1;
  
	spin_lock_irqsave(&devc->lock,flags);
	
	/* set transfer count */
	ad_write (devc, 8, cnt & 0xffff); 
	
	devc->audio_mode |= PCM_ENABLE_OUTPUT; 
	spin_unlock_irqrestore(&devc->lock,flags);
}


static void ad1816_start_input (int dev, unsigned long buf, int count,
				int intrflag)
{
	unsigned long flags;
	unsigned long  cnt;
	ad1816_info    *devc = (ad1816_info *) audio_devs[dev]->devc;
	
	DEBUGNOISE(printk(KERN_DEBUG "ad1816: start_input called buf=%ld count=%d flags=%d\n",buf,count,intrflag));

	cnt = count/4 - 1;

	spin_lock_irqsave(&devc->lock,flags);

	/* set transfer count */
	ad_write (devc, 10, cnt & 0xffff); 
	devc->audio_mode |= PCM_ENABLE_INPUT;
	spin_unlock_irqrestore(&devc->lock,flags);
}

static int ad1816_prepare_for_input (int dev, int bsize, int bcount)
{
	unsigned long flags;
	unsigned int freq;
	ad1816_info    *devc = (ad1816_info *) audio_devs[dev]->devc;
	unsigned char fmt_bits;
	
	DEBUGNOISE(printk(KERN_DEBUG "ad1816: prepare_for_input called: bsize=%d bcount=%d\n",bsize,bcount));

	spin_lock_irqsave(&devc->lock,flags);
	fmt_bits= (devc->format_bits&0x7)<<3;
	
	/* set mono/stereo mode */
	if (devc->channels > 1) {
		fmt_bits |=0x4;
	}
	/* set Mono/Stereo in playback/capture register */
	outb( (inb(devc->base+8) & ~0x3C)|fmt_bits, devc->base+8); 
	outb( (inb(devc->base+9) & ~0x3C)|fmt_bits, devc->base+9);

	freq=((unsigned int)devc->speed*33000)/ad1816_clockfreq; 

	/* write playback/capture speeds */
	ad_write (devc, 2, freq & 0xffff);	
	ad_write (devc, 3, freq & 0xffff);	

	spin_unlock_irqrestore(&devc->lock,flags);

	ad1816_halt_input(dev);
	return 0;
}

static int ad1816_prepare_for_output (int dev, int bsize, int bcount)
{
	unsigned long flags;
	unsigned int freq;
	ad1816_info    *devc = (ad1816_info *) audio_devs[dev]->devc;
	unsigned char fmt_bits;

	DEBUGNOISE(printk(KERN_DEBUG "ad1816: prepare_for_output called: bsize=%d bcount=%d\n",bsize,bcount));

	spin_lock_irqsave(&devc->lock,flags);

	fmt_bits= (devc->format_bits&0x7)<<3;
	/* set mono/stereo mode */
	if (devc->channels > 1) {
		fmt_bits |=0x4;
	}

	/* write format bits to playback/capture registers */
	outb( (inb(devc->base+8) & ~0x3C)|fmt_bits, devc->base+8); 
	outb( (inb(devc->base+9) & ~0x3C)|fmt_bits, devc->base+9);
  
	freq=((unsigned int)devc->speed*33000)/ad1816_clockfreq; 
	
	/* write playback/capture speeds */
	ad_write (devc, 2, freq & 0xffff);
	ad_write (devc, 3, freq & 0xffff);

	spin_unlock_irqrestore(&devc->lock,flags);
	
	ad1816_halt_output(dev);
	return 0;

}

static void ad1816_trigger (int dev, int state) 
{
	unsigned long flags;
	ad1816_info    *devc = (ad1816_info *) audio_devs[dev]->devc;

	DEBUGNOISE(printk(KERN_DEBUG "ad1816: trigger called! (devc=%d,devc->base=%d\n", devc, devc->base));

	/* mode may have changed */

	spin_lock_irqsave(&devc->lock,flags);

	/* mask out modes not specified on open call */
	state &= devc->audio_mode; 
				
	/* setup soundchip to new io-mode */
	if (state & PCM_ENABLE_INPUT) {
		/* enable capture */
		outb(inb(devc->base+9)|0x01, devc->base+9);
	} else {
		/* disable capture */
		outb(inb(devc->base+9)&~0x01, devc->base+9);
	}

	if (state & PCM_ENABLE_OUTPUT) {
		/* enable playback */
		outb(inb(devc->base+8)|0x01, devc->base+8);
		/* unmute pcm output */
		ad_write(devc, 4, ad_read(devc,4)&~0x8080);
	} else {
		/* mute pcm output */
		ad_write(devc, 4, ad_read(devc,4)|0x8080);
		/* disable capture */
		outb(inb(devc->base+8)&~0x01, devc->base+8);
	}
	spin_unlock_irqrestore(&devc->lock,flags);
}


/* halt input & output */
static void ad1816_halt (int dev)
{
	ad1816_halt_input(dev);
	ad1816_halt_output(dev);
}

static void ad1816_reset (int dev)
{
	ad1816_halt (dev);
}

/* set playback speed */
static int ad1816_set_speed (int dev, int arg)
{
	unsigned long flags;
	unsigned int freq;
	int ret;

	ad1816_info    *devc = (ad1816_info *) audio_devs[dev]->devc;
	
	spin_lock_irqsave(&devc->lock, flags);
	if (arg == 0) {
		ret = devc->speed;
		spin_unlock_irqrestore(&devc->lock, flags);
		return ret;
	}
	/* range checking */
	if (arg < 4000) {
		arg = 4000;
	}
	if (arg > 55000) {
		arg = 55000;
	}
	devc->speed = arg;

	/* change speed during playback */
	freq=((unsigned int)devc->speed*33000)/ad1816_clockfreq; 
	/* write playback/capture speeds */
	ad_write (devc, 2, freq & 0xffff);	
	ad_write (devc, 3, freq & 0xffff);	

	ret = devc->speed;
	spin_unlock_irqrestore(&devc->lock, flags);
	return ret;

}

static unsigned int ad1816_set_bits (int dev, unsigned int arg)
{
	unsigned long flags;
	ad1816_info    *devc = (ad1816_info *) audio_devs[dev]->devc;
	
	static struct format_tbl {
		int             format;
		unsigned char   bits;
	} format2bits[] = {
		{ 0, 0 },
		{ AFMT_MU_LAW, 1 },
		{ AFMT_A_LAW, 3 },
		{ AFMT_IMA_ADPCM, 0 },
		{ AFMT_U8, 0 },
		{ AFMT_S16_LE, 2 },
		{ AFMT_S16_BE, 6 },
		{ AFMT_S8, 0 },
		{ AFMT_U16_LE, 0 },
		{ AFMT_U16_BE, 0 }
  	};

	int  i, n = sizeof (format2bits) / sizeof (struct format_tbl);

	spin_lock_irqsave(&devc->lock, flags);
	/* return current format */
	if (arg == 0) {
	  	arg = devc->audio_format;
		spin_unlock_irqrestore(&devc->lock, flags);
		return arg;
	}
	devc->audio_format = arg;

	/* search matching format bits */
	for (i = 0; i < n; i++)
		if (format2bits[i].format == arg) {
			devc->format_bits = format2bits[i].bits;
			devc->audio_format = arg;
			spin_unlock_irqrestore(&devc->lock, flags);
			return arg;
		}

	/* Still hanging here. Something must be terribly wrong */
	devc->format_bits = 0;
	devc->audio_format = AFMT_U8;
	spin_unlock_irqrestore(&devc->lock, flags);
	return(AFMT_U8); 
}

static short ad1816_set_channels (int dev, short arg)
{
	ad1816_info    *devc = (ad1816_info *) audio_devs[dev]->devc;

	if (arg != 1 && arg != 2)
		return devc->channels;

	devc->channels = arg;
	return arg;
}

/* open device */
static int ad1816_open (int dev, int mode) 
{
	ad1816_info    *devc = NULL;
	unsigned long   flags;

	/* is device number valid ? */
	if (dev < 0 || dev >= num_audiodevs)
		return -(ENXIO);

	/* get device info of this dev */
	devc = (ad1816_info *) audio_devs[dev]->devc; 

	/* make check if device already open atomic */
	spin_lock_irqsave(&devc->lock,flags);

	if (devc->opened) {
		spin_unlock_irqrestore(&devc->lock,flags);
		return -(EBUSY);
	}

	/* mark device as open */
	devc->opened = 1; 

	devc->audio_mode = 0;
	devc->speed = 8000;
	devc->audio_format=AFMT_U8;
	devc->channels=1;
	spin_unlock_irqrestore(&devc->lock,flags);
	ad1816_reset(devc->dev_no); /* halt all pending output */
	return 0;
}

static void ad1816_close (int dev) /* close device */
{
	unsigned long flags;
	ad1816_info    *devc = (ad1816_info *) audio_devs[dev]->devc;

	/* halt all pending output */
	ad1816_reset(devc->dev_no); 

	spin_lock_irqsave(&devc->lock,flags);
	devc->opened = 0;
	devc->audio_mode = 0;
	devc->speed = 8000;
	devc->audio_format=AFMT_U8;
	devc->format_bits = 0;
	spin_unlock_irqrestore(&devc->lock,flags);
}


/* ------------------------------------------------------------------- */

/* Audio driver structure */

static struct audio_driver ad1816_audio_driver =
{
	.owner			= THIS_MODULE,
	.open			= ad1816_open,
	.close			= ad1816_close,
	.output_block		= ad1816_output_block,
	.start_input		= ad1816_start_input,
	.prepare_for_input	= ad1816_prepare_for_input,
	.prepare_for_output	= ad1816_prepare_for_output,
	.halt_io		= ad1816_halt,
	.halt_input		= ad1816_halt_input,
	.halt_output		= ad1816_halt_output,
	.trigger		= ad1816_trigger,
	.set_speed		= ad1816_set_speed,
	.set_bits		= ad1816_set_bits,
	.set_channels		= ad1816_set_channels,
};


/* ------------------------------------------------------------------- */

/* Interrupt handler */


static irqreturn_t ad1816_interrupt (int irq, void *dev_id, struct pt_regs *dummy)
{
	unsigned char	status;
	ad1816_info	*devc = (ad1816_info *)dev_id;
	
	if (irq < 0 || irq > 15) {
	        printk(KERN_WARNING "ad1816: Got bogus interrupt %d\n", irq);
		return IRQ_NONE;
	}

	spin_lock(&devc->lock);

	/* read interrupt register */
	status = inb (devc->base+1); 
	/* Clear all interrupt  */
	outb (~status, devc->base+1);	

	DEBUGNOISE(printk(KERN_DEBUG "ad1816: Got interrupt subclass %d\n",status));

	if (status == 0) {
		DEBUGNOISE(printk(KERN_DEBUG "ad1816: interrupt: Got interrupt, but no source.\n"));
		spin_unlock(&devc->lock);
		return IRQ_NONE;
	}

	if (devc->opened && (devc->audio_mode & PCM_ENABLE_INPUT) && (status&64))
		DMAbuf_inputintr (devc->dev_no);

	if (devc->opened && (devc->audio_mode & PCM_ENABLE_OUTPUT) && (status & 128))
		DMAbuf_outputintr (devc->dev_no, 1);

	spin_unlock(&devc->lock);
	return IRQ_HANDLED;
}

/* ------------------------------------------------------------------- */

/* Mixer stuff */

struct mixer_def {
	unsigned int regno: 7;
	unsigned int polarity:1;	/* 0=normal, 1=reversed */
	unsigned int bitpos:4;
	unsigned int nbits:4;
};

static char mix_cvt[101] = {
	 0, 0, 3, 7,10,13,16,19,21,23,26,28,30,32,34,35,37,39,40,42,
	43,45,46,47,49,50,51,52,53,55,56,57,58,59,60,61,62,63,64,65,
	65,66,67,68,69,70,70,71,72,73,73,74,75,75,76,77,77,78,79,79,
	80,81,81,82,82,83,84,84,85,85,86,86,87,87,88,88,89,89,90,90,
	91,91,92,92,93,93,94,94,95,95,96,96,96,97,97,98,98,98,99,99,
	100
};

typedef struct mixer_def mixer_ent;

/*
 * Most of the mixer entries work in backwards. Setting the polarity field
 * makes them to work correctly.
 *
 * The channel numbering used by individual soundcards is not fixed. Some
 * cards have assigned different meanings for the AUX1, AUX2 and LINE inputs.
 * The current version doesn't try to compensate this.
 */

#define MIX_ENT(name, reg_l, pola_l, pos_l, len_l, reg_r, pola_r, pos_r, len_r)	\
  {{reg_l, pola_l, pos_l, len_l}, {reg_r, pola_r, pos_r, len_r}}


mixer_ent mix_devices[SOUND_MIXER_NRDEVICES][2] = {
MIX_ENT(SOUND_MIXER_VOLUME,	14, 1, 8, 5,	14, 1, 0, 5),
MIX_ENT(SOUND_MIXER_BASS,	 0, 0, 0, 0,	 0, 0, 0, 0),
MIX_ENT(SOUND_MIXER_TREBLE,	 0, 0, 0, 0,	 0, 0, 0, 0),
MIX_ENT(SOUND_MIXER_SYNTH,	 5, 1, 8, 6,	 5, 1, 0, 6),
MIX_ENT(SOUND_MIXER_PCM,	 4, 1, 8, 6,	 4, 1, 0, 6),
MIX_ENT(SOUND_MIXER_SPEAKER,	 0, 0, 0, 0,	 0, 0, 0, 0),
MIX_ENT(SOUND_MIXER_LINE,	18, 1, 8, 5,	18, 1, 0, 5),
MIX_ENT(SOUND_MIXER_MIC,	19, 1, 8, 5,	19, 1, 0, 5),
MIX_ENT(SOUND_MIXER_CD,	 	15, 1, 8, 5,	15, 1, 0, 5),
MIX_ENT(SOUND_MIXER_IMIX,	 0, 0, 0, 0,	 0, 0, 0, 0),
MIX_ENT(SOUND_MIXER_ALTPCM,	 0, 0, 0, 0,	 0, 0, 0, 0),
MIX_ENT(SOUND_MIXER_RECLEV,	20, 0, 8, 4,	20, 0, 0, 4),
MIX_ENT(SOUND_MIXER_IGAIN,	 0, 0, 0, 0,	 0, 0, 0, 0),
MIX_ENT(SOUND_MIXER_OGAIN,	 0, 0, 0, 0,	 0, 0, 0, 0),
MIX_ENT(SOUND_MIXER_LINE1, 	17, 1, 8, 5,	17, 1, 0, 5),
MIX_ENT(SOUND_MIXER_LINE2,	16, 1, 8, 5,	16, 1, 0, 5),
MIX_ENT(SOUND_MIXER_LINE3,      39, 0, 9, 4,    39, 1, 0, 5)
};


static unsigned short default_mixer_levels[SOUND_MIXER_NRDEVICES] =
{
	0x4343,		/* Master Volume */
	0x3232,		/* Bass */
	0x3232,		/* Treble */
	0x0000,		/* FM */
	0x4343,		/* PCM */
	0x0000,		/* PC Speaker */
	0x0000,		/* Ext Line */
	0x0000,		/* Mic */
	0x0000,		/* CD */
	0x0000,		/* Recording monitor */
	0x0000,		/* SB PCM */
	0x0000,		/* Recording level */
	0x0000,		/* Input gain */
	0x0000,		/* Output gain */
	0x0000,		/* Line1 */
	0x0000,		/* Line2 */
	0x0000		/* Line3 (usually line in)*/
};

#define LEFT_CHN	0
#define RIGHT_CHN	1



static int
ad1816_set_recmask (ad1816_info * devc, int mask)
{
  	unsigned long 	flags;
	unsigned char   recdev;
	int             i, n;
	
	spin_lock_irqsave(&devc->lock, flags);
	mask &= devc->supported_rec_devices;
	
	n = 0;
	/* Count selected device bits */
	for (i = 0; i < 32; i++)
		if (mask & (1 << i))
			n++;
	
	if (n == 0)
		mask = SOUND_MASK_MIC;
	else if (n != 1) { /* Too many devices selected */
		/* Filter out active settings */
		mask &= ~devc->recmask;	
		
		n = 0;
		/* Count selected device bits */
		for (i = 0; i < 32; i++) 
			if (mask & (1 << i))
				n++;
		
		if (n != 1)
			mask = SOUND_MASK_MIC;
	}
	
	switch (mask) {
	case SOUND_MASK_MIC:
		recdev = 5;
		break;
		
	case SOUND_MASK_LINE:
		recdev = 0;
		break;
		
	case SOUND_MASK_CD:
		recdev = 2;
		break;
		
	case SOUND_MASK_LINE1:
		recdev = 4;
		break;
		
	case SOUND_MASK_LINE2:
		recdev = 3;
		break;
		
	case SOUND_MASK_VOLUME:
		recdev = 1;
		break;
		
	default:
		mask = SOUND_MASK_MIC;
		recdev = 5;
	}
	
	recdev <<= 4;
	ad_write (devc, 20, 
		  (ad_read (devc, 20) & 0x8f8f) | recdev | (recdev<<8));

	devc->recmask = mask;
	spin_unlock_irqrestore(&devc->lock, flags);
	return mask;
}

static void
change_bits (int *regval, int dev, int chn, int newval)
{
	unsigned char   mask;
	int             shift;
  
	/* Reverse polarity*/

	if (mix_devices[dev][chn].polarity == 1) 
		newval = 100 - newval;

	mask = (1 << mix_devices[dev][chn].nbits) - 1;
	shift = mix_devices[dev][chn].bitpos;
	/* Scale it */
	newval = (int) ((newval * mask) + 50) / 100;	
	/* Clear bits */
	*regval &= ~(mask << shift);	
	/* Set new value */
	*regval |= (newval & mask) << shift;	
}

static int
ad1816_mixer_get (ad1816_info * devc, int dev)
{
	DEBUGNOISE(printk(KERN_DEBUG "ad1816: mixer_get called!\n"));
	
	/* range check + supported mixer check */
	if (dev < 0 || dev >= SOUND_MIXER_NRDEVICES )
	        return (-(EINVAL));
	if (!((1 << dev) & devc->supported_devices))
		return -(EINVAL);
	
	return devc->levels[dev];
}

static int
ad1816_mixer_set (ad1816_info * devc, int dev, int value)
{
	int   left = value & 0x000000ff;
	int   right = (value & 0x0000ff00) >> 8;
	int   retvol;

	int   regoffs;
	int   val;
	int   valmute;
	unsigned long flags;

	DEBUGNOISE(printk(KERN_DEBUG "ad1816: mixer_set called!\n"));
	
	if (dev < 0 || dev >= SOUND_MIXER_NRDEVICES )
		return -(EINVAL);

	if (left > 100)
		left = 100;
	if (left < 0)
		left = 0;
	if (right > 100)
		right = 100;
	if (right < 0)
		right = 0;
	
	/* Mono control */
	if (mix_devices[dev][RIGHT_CHN].nbits == 0) 
		right = left;
	retvol = left | (right << 8);
	
	/* Scale it */
	
	left = mix_cvt[left];
	right = mix_cvt[right];

	/* reject all mixers that are not supported */
	if (!(devc->supported_devices & (1 << dev)))
		return -(EINVAL);
	
	/* sanity check */
	if (mix_devices[dev][LEFT_CHN].nbits == 0)
		return -(EINVAL);
	spin_lock_irqsave(&devc->lock, flags);

	/* keep precise volume internal */
	devc->levels[dev] = retvol;

	/* Set the left channel */
	regoffs = mix_devices[dev][LEFT_CHN].regno;
	val = ad_read (devc, regoffs);
	change_bits (&val, dev, LEFT_CHN, left);

	valmute=val;

	/* Mute bit masking on some registers */
	if ( regoffs==5 || regoffs==14 || regoffs==15 ||
	     regoffs==16 || regoffs==17 || regoffs==18 || 
	     regoffs==19 || regoffs==39) {
		if (left==0)
			valmute |= 0x8000;
		else
			valmute &= ~0x8000;
	}
	ad_write (devc, regoffs, valmute); /* mute */

	/*
	 * Set the right channel
	 */
 
	/* Was just a mono channel */
	if (mix_devices[dev][RIGHT_CHN].nbits == 0) {
		spin_unlock_irqrestore(&devc->lock, flags);
		return retvol;		
	}

	regoffs = mix_devices[dev][RIGHT_CHN].regno;
	val = ad_read (devc, regoffs);
	change_bits (&val, dev, RIGHT_CHN, right);

	valmute=val;
	if ( regoffs==5 || regoffs==14 || regoffs==15 ||
	     regoffs==16 || regoffs==17 || regoffs==18 || 
	     regoffs==19 || regoffs==39) {
		if (right==0)
			valmute |= 0x80;
		else
			valmute &= ~0x80;
	}
	ad_write (devc, regoffs, valmute); /* mute */
	spin_unlock_irqrestore(&devc->lock, flags);
       	return retvol;
}

#define MIXER_DEVICES ( SOUND_MASK_VOLUME | \
			SOUND_MASK_SYNTH | \
			SOUND_MASK_PCM | \
			SOUND_MASK_LINE | \
			SOUND_MASK_LINE1 | \
			SOUND_MASK_LINE2 | \
			SOUND_MASK_LINE3 | \
			SOUND_MASK_MIC | \
			SOUND_MASK_CD | \
			SOUND_MASK_RECLEV  \
			)
#define REC_DEVICES ( SOUND_MASK_LINE2 |\
		      SOUND_MASK_LINE |\
		      SOUND_MASK_LINE1 |\
		      SOUND_MASK_MIC |\
		      SOUND_MASK_CD |\
		      SOUND_MASK_VOLUME \
		      )
     
static void
ad1816_mixer_reset (ad1816_info * devc)
{
	int  i;

	devc->supported_devices = MIXER_DEVICES;
	
	devc->supported_rec_devices = REC_DEVICES;

	for (i = 0; i < SOUND_MIXER_NRDEVICES; i++)
		if (devc->supported_devices & (1 << i))
			ad1816_mixer_set (devc, i, default_mixer_levels[i]);
	ad1816_set_recmask (devc, SOUND_MASK_MIC);
}

static int
ad1816_mixer_ioctl (int dev, unsigned int cmd, void __user * arg)
{
	ad1816_info    *devc = mixer_devs[dev]->devc;
	int val;
	int __user *p = arg;
  
	DEBUGNOISE(printk(KERN_DEBUG "ad1816: mixer_ioctl called!\n"));
  
	/* Mixer ioctl */
	if (((cmd >> 8) & 0xff) == 'M') { 
		
		/* set ioctl */
		if (_SIOC_DIR (cmd) & _SIOC_WRITE) { 
			switch (cmd & 0xff){
			case SOUND_MIXER_RECSRC:
				
				if (get_user(val, p))
					return -EFAULT;
				val=ad1816_set_recmask (devc, val);
				return put_user(val, p);
				break;
				
			default:
				if (get_user(val, p))
					return -EFAULT;
				if ((val=ad1816_mixer_set (devc, cmd & 0xff, val))<0)
				        return val;
				else
				        return put_user(val, p);
			}
		} else { 
			/* read ioctl */
			switch (cmd & 0xff) {
				
			case SOUND_MIXER_RECSRC:
				val=devc->recmask;
				return put_user(val, p);
				break;
				
			case SOUND_MIXER_DEVMASK:
				val=devc->supported_devices;
				return put_user(val, p);
				break;

			case SOUND_MIXER_STEREODEVS:
				val=devc->supported_devices & ~(SOUND_MASK_SPEAKER | SOUND_MASK_IMIX);
				return put_user(val, p);
				break;
				
			case SOUND_MIXER_RECMASK:
				val=devc->supported_rec_devices;
				return put_user(val, p);
				break;
				
			case SOUND_MIXER_CAPS:
				val=SOUND_CAP_EXCL_INPUT;
				return put_user(val, p);
				break;
				
			default:
			        if ((val=ad1816_mixer_get (devc, cmd & 0xff))<0)
				        return val;
				else
				        return put_user(val, p);
			}
		}
	} else
		/* not for mixer */
		return -(EINVAL);
}

/* ------------------------------------------------------------------- */

/* Mixer structure */

static struct mixer_operations ad1816_mixer_operations = {
	.owner	= THIS_MODULE,
	.id	= "AD1816",
	.name	= "AD1816 Mixer",
	.ioctl	= ad1816_mixer_ioctl
};


/* ------------------------------------------------------------------- */

/* stuff for card recognition, init and unloading PNP ...*/


/* check if AD1816 present at specified hw_config and register device with OS 
 * return 1 if initialization was successful, 0 otherwise
 */
static int __init ad1816_init_card (struct address_info *hw_config, 
	struct pnp_dev *pnp)
{
	ad1816_info    *devc = NULL;
	int tmp;
	int oss_devno = -1;

	printk(KERN_INFO "ad1816: initializing card: io=0x%x, irq=%d, dma=%d, "
			 "dma2=%d, clockfreq=%d, options=%d isadmabug=%d "
			 "%s\n",
	       hw_config->io_base,
	       hw_config->irq,
	       hw_config->dma,
	       hw_config->dma2,
	       ad1816_clockfreq,
	       options,
	       isa_dma_bridge_buggy,
	       pnp?"(PNP)":"");

	/* ad1816_info structure remaining ? */
	if (nr_ad1816_devs >= MAX_AUDIO_DEV) {
		printk(KERN_WARNING "ad1816: no more ad1816_info structures "
			"left\n");
		goto out;
	}

	devc = &dev_info[nr_ad1816_devs];
	devc->base = hw_config->io_base;
	devc->irq = hw_config->irq;
	devc->dma_playback=hw_config->dma;
	devc->dma_capture=hw_config->dma2;
	devc->opened = 0;
	devc->pnpdev = pnp;
	spin_lock_init(&devc->lock);

	if (!request_region(devc->base, 16, "AD1816 Sound")) {
		printk(KERN_WARNING "ad1816: I/O port 0x%03x not free\n",
				    devc->base);
		goto out;
	}

	printk(KERN_INFO "ad1816: Examining AD1816 at address 0x%03x.\n", 
		devc->base);
	

	/* tests for ad1816 */
	/* base+0: bit 1 must be set but not 255 */
	tmp=inb(devc->base);
	if ( (tmp&0x80)==0 || tmp==255 ) {
		printk (KERN_INFO "ad1816: Chip is not an AD1816 or chip "
			"is not active (Test 0)\n");
		goto out_release_region;
	}

	/* writes to ireg 8 are copied to ireg 9 */
	ad_write(devc,8,12345); 
	if (ad_read(devc,9)!=12345) {
		printk(KERN_INFO "ad1816: Chip is not an AD1816 (Test 1)\n");
		goto out_release_region;
	}
  
	/* writes to ireg 8 are copied to ireg 9 */
	ad_write(devc,8,54321); 
	if (ad_read(devc,9)!=54321) {
		printk(KERN_INFO "ad1816: Chip is not an AD1816 (Test 2)\n");
		goto out_release_region;
	}

	/* writes to ireg 10 are copied to ireg 11 */
	ad_write(devc,10,54321); 
	if (ad_read(devc,11)!=54321) {
		printk (KERN_INFO "ad1816: Chip is not an AD1816 (Test 3)\n");
		goto out_release_region;
	}

	/* writes to ireg 10 are copied to ireg 11 */
	ad_write(devc,10,12345); 
	if (ad_read(devc,11)!=12345) {
		printk (KERN_INFO "ad1816: Chip is not an AD1816 (Test 4)\n");
		goto out_release_region;
	}

	/* bit in base +1 cannot be set to 1 */
	tmp=inb(devc->base+1);
	outb(0xff,devc->base+1); 
	if (inb(devc->base+1)!=tmp) {
		printk(KERN_INFO "ad1816: Chip is not an AD1816 (Test 5)\n");
		goto out_release_region;
	}
  
	printk(KERN_INFO "ad1816: AD1816 (version %d) successfully detected!\n",
		ad_read(devc,45));

	/* disable all interrupts */
	ad_write(devc,1,0);     

	/* Clear pending interrupts */
	outb (0, devc->base+1);	

	/* allocate irq */
	if (devc->irq < 0 || devc->irq > 15)
		goto out_release_region;
	if (request_irq(devc->irq, ad1816_interrupt,0,
			"SoundPort", devc) < 0)	{
	        printk(KERN_WARNING "ad1816: IRQ in use\n");
		goto out_release_region;
	}

	/* DMA stuff */
	if (sound_alloc_dma (devc->dma_playback, "Sound System")) {
		printk(KERN_WARNING "ad1816: Can't allocate DMA%d\n",
				    devc->dma_playback);
		goto out_free_irq;
	}
	
	if ( devc->dma_capture >= 0 && 
	  	devc->dma_capture != devc->dma_playback) {
		if (sound_alloc_dma(devc->dma_capture,
				    "Sound System (capture)")) {
			printk(KERN_WARNING "ad1816: Can't allocate DMA%d\n",
					    devc->dma_capture);
			goto out_free_dma;
		}
		devc->audio_mode=DMA_AUTOMODE|DMA_DUPLEX;
	} else {
	  	printk(KERN_WARNING "ad1816: Only one DMA channel "
			"available/configured. No duplex operation possible\n");
		devc->audio_mode=DMA_AUTOMODE;
	}

	conf_printf2 ("AD1816 audio driver",
		      devc->base, devc->irq, devc->dma_playback, 
		      devc->dma_capture);

	/* register device */
	if ((oss_devno = sound_install_audiodrv (AUDIO_DRIVER_VERSION,
					      "AD1816 audio driver",
					      &ad1816_audio_driver,
					      sizeof (struct audio_driver),
					      devc->audio_mode,
					      ad_format_mask,
					      devc,
					      devc->dma_playback, 
					      devc->dma_capture)) < 0) {
		printk(KERN_WARNING "ad1816: Can't install sound driver\n");
		goto out_free_dma_2;
	}


	ad_write(devc,32,0x80f0); /* sound system mode */
	if (options&1) {
	        ad_write(devc,33,0); /* disable all audiosources for dsp */
	} else {
	        ad_write(devc,33,0x03f8); /* enable all audiosources for dsp */
	}
	ad_write(devc,4,0x8080);  /* default values for volumes (muted)*/
	ad_write(devc,5,0x8080);
	ad_write(devc,6,0x8080);
	ad_write(devc,7,0x8080);
	ad_write(devc,15,0x8888);
	ad_write(devc,16,0x8888);
	ad_write(devc,17,0x8888);
	ad_write(devc,18,0x8888);
	ad_write(devc,19,0xc888); /* +20db mic active */
	ad_write(devc,14,0x0000); /* Master volume unmuted */
	ad_write(devc,39,0x009f); /* 3D effect on 0% phone out muted */
	ad_write(devc,44,0x0080); /* everything on power, 3d enabled for d/a */
	outb(0x10,devc->base+8); /* set dma mode */
	outb(0x10,devc->base+9);
  
	/* enable capture + playback interrupt */
	ad_write(devc,1,0xc000); 
	
	/* set mixer defaults */
	ad1816_mixer_reset (devc); 
  
	/* register mixer */
	if ((audio_devs[oss_devno]->mixer_dev=sound_install_mixer(
				       MIXER_DRIVER_VERSION,
				       "AD1816 audio driver",
				       &ad1816_mixer_operations,
				       sizeof (struct mixer_operations),
				       devc)) < 0) {
	  	printk(KERN_WARNING "Can't install mixer\n");
	}
	/* make ad1816_info active */
	nr_ad1816_devs++;
	printk(KERN_INFO "ad1816: card successfully installed!\n");
	return 1;
	/* error handling */
out_free_dma_2:
	if (devc->dma_capture >= 0 && devc->dma_capture != devc->dma_playback)
	        sound_free_dma(devc->dma_capture);
out_free_dma:
	sound_free_dma(devc->dma_playback);
out_free_irq:
	free_irq(devc->irq, devc);
out_release_region:
	release_region(devc->base, 16);
out:
	return 0;
}

static void __exit unload_card(ad1816_info *devc)
{
	int  mixer, dev = 0;
	
	if (devc != NULL) {
		printk("ad1816: Unloading card at address 0x%03x\n",devc->base);
		
		dev = devc->dev_no;
		mixer = audio_devs[dev]->mixer_dev;

		/* unreg mixer*/
		if(mixer>=0) {
			sound_unload_mixerdev(mixer);
		}
		/* unreg audiodev */
		sound_unload_audiodev(dev);
		
		/* free dma channels */
		if (devc->dma_capture>=0 && 
		  	devc->dma_capture != devc->dma_playback) {
			sound_free_dma(devc->dma_capture);
		}
		sound_free_dma (devc->dma_playback);
		/* free irq */
		free_irq(devc->irq, devc);
		/* free io */
		release_region (devc->base, 16);
#ifdef __ISAPNP__
		if (devc->pnpdev) {
		  	pnp_disable_dev(devc->pnpdev);
			pnp_device_detach(devc->pnpdev);
		}
#endif
		
	} else
		printk(KERN_WARNING "ad1816: no device/card specified\n");
}

static int __initdata io = -1;
static int __initdata irq = -1;
static int __initdata dma = -1;
static int __initdata dma2 = -1;

#ifdef __ISAPNP__
/* use isapnp for configuration */
static int isapnp	= 1;
static int isapnpjump;
module_param(isapnp, bool, 0);
module_param(isapnpjump, int, 0);
#endif

module_param(io, int, 0);
module_param(irq, int, 0);
module_param(dma, int, 0);
module_param(dma2, int, 0);
module_param(ad1816_clockfreq, int, 0);
module_param(options, int, 0);

#ifdef __ISAPNP__
static struct {
	unsigned short card_vendor, card_device;
	unsigned short vendor;
	unsigned short function;
	struct ad1816_data *data;
} isapnp_ad1816_list[] __initdata = {
	{	ISAPNP_ANY_ID, ISAPNP_ANY_ID,
		ISAPNP_VENDOR('A','D','S'), ISAPNP_FUNCTION(0x7150), 
		NULL },
	{	ISAPNP_ANY_ID, ISAPNP_ANY_ID,
		ISAPNP_VENDOR('A','D','S'), ISAPNP_FUNCTION(0x7180),
		NULL },
	{0}
};

MODULE_DEVICE_TABLE(isapnp, isapnp_ad1816_list);


static void __init ad1816_config_pnp_card(struct pnp_card *card,
					  unsigned short vendor,
					  unsigned short function)
{
	struct address_info cfg;
  	struct pnp_dev *card_dev = pnp_find_dev(card, vendor, function, NULL);
	if (!card_dev) return;
	if (pnp_device_attach(card_dev) < 0) {
	  	printk(KERN_WARNING "ad1816: Failed to attach PnP device\n");
		return;
	}
	if (pnp_activate_dev(card_dev) < 0) {
		printk(KERN_WARNING "ad1816: Failed to activate PnP device\n");
		pnp_device_detach(card_dev);
		return;
	}
	cfg.io_base = pnp_port_start(card_dev, 2);
	cfg.irq = pnp_irq(card_dev, 0);
	cfg.dma = pnp_irq(card_dev, 0);
	cfg.dma2 = pnp_irq(card_dev, 1);
	if (!ad1816_init_card(&cfg, card_dev)) {
	  	pnp_disable_dev(card_dev);
		pnp_device_detach(card_dev);
	}
}

static void __init ad1816_config_pnp_cards(void)
{
	int nr_pnp_cfg;
	int i;
	
	/* Count entries in isapnp_ad1816_list */
	for (nr_pnp_cfg = 0; isapnp_ad1816_list[nr_pnp_cfg].card_vendor != 0; 
		nr_pnp_cfg++);
	/* Check and adjust isapnpjump */
	if( isapnpjump < 0 || isapnpjump >= nr_pnp_cfg) {
		printk(KERN_WARNING 
			"ad1816: Valid range for isapnpjump is 0-%d. "
			"Adjusted to 0.\n", nr_pnp_cfg-1);
		isapnpjump = 0;
	}
	for (i = isapnpjump; isapnp_ad1816_list[i].card_vendor != 0; i++) {
	 	struct pnp_card *card = NULL;
		/* iterate over all pnp cards */		
		while ((card = pnp_find_card(isapnp_ad1816_list[i].card_vendor,
		              	isapnp_ad1816_list[i].card_device, card))) 
			ad1816_config_pnp_card(card, 
				isapnp_ad1816_list[i].vendor,
				isapnp_ad1816_list[i].function);
	}
}
#endif

/* module initialization */
static int __init init_ad1816(void)
{
	printk(KERN_INFO "ad1816: AD1816 sounddriver "
			 "Copyright (C) 1998-2003 by Thorsten Knabe and "
			 "others\n");
#ifdef AD1816_CLOCK 
	/* set ad1816_clockfreq if set during compilation */
	ad1816_clockfreq=AD1816_CLOCK;
#endif
	if (ad1816_clockfreq<5000 || ad1816_clockfreq>100000) {
		ad1816_clockfreq=33000;
	}

#ifdef __ISAPNP__
	/* configure PnP cards */
	if(isapnp) ad1816_config_pnp_cards();
#endif
	/* configure card by module params */
	if (io != -1 && irq != -1 && dma != -1) {
		struct address_info cfg;
		cfg.io_base = io;
		cfg.irq = irq;
		cfg.dma = dma;
		cfg.dma2 = dma2;
		ad1816_init_card(&cfg, NULL);
	}
	if (nr_ad1816_devs <= 0)
	  	return -ENODEV;
	return 0;
}

/* module cleanup */
static void __exit cleanup_ad1816 (void)
{
	int          i;
	ad1816_info  *devc = NULL;
  
	/* remove any soundcard */
	for (i = 0;  i < nr_ad1816_devs; i++) {
		devc = &dev_info[i];
		unload_card(devc);
	}     
	nr_ad1816_devs=0;
	printk(KERN_INFO "ad1816: driver unloaded!\n");
}

module_init(init_ad1816);
module_exit(cleanup_ad1816);

#ifndef MODULE
/* kernel command line parameter evaluation */
static int __init setup_ad1816(char *str)
{
	/* io, irq, dma, dma2 */
	int ints[5];
	
	str = get_options(str, ARRAY_SIZE(ints), ints);
	
	io	= ints[1];
	irq	= ints[2];
	dma	= ints[3];
	dma2	= ints[4];
	return 1;
}

__setup("ad1816=", setup_ad1816);
#endif
MODULE_LICENSE("GPL");
