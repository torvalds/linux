/*
 *  Driver for Philips UDA1341TS on Compaq iPAQ H3600 soundcard
 *  Copyright (C) 2002 Tomas Kasparek <tomas.kasparek@seznam.cz>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License.
 * 
 * History:
 *
 * 2002-03-13   Tomas Kasparek  initial release - based on h3600-uda1341.c from OSS
 * 2002-03-20   Tomas Kasparek  playback over ALSA is working
 * 2002-03-28   Tomas Kasparek  playback over OSS emulation is working
 * 2002-03-29   Tomas Kasparek  basic capture is working (native ALSA)
 * 2002-03-29   Tomas Kasparek  capture is working (OSS emulation)
 * 2002-04-04   Tomas Kasparek  better rates handling (allow non-standard rates)
 * 2003-02-14   Brian Avery     fixed full duplex mode, other updates
 * 2003-02-20   Tomas Kasparek  merged updates by Brian (except HAL)
 * 2003-04-19   Jaroslav Kysela recoded DMA stuff to follow 2.4.18rmk3-hh24 kernel
 *                              working suspend and resume
 * 2003-04-28   Tomas Kasparek  updated work by Jaroslav to compile it under 2.5.x again
 *                              merged HAL layer (patches from Brian)
 */

/* $Id: sa11xx-uda1341.c,v 1.27 2005/12/07 09:13:42 cladisch Exp $ */

/***************************************************************************************************
*
* To understand what Alsa Drivers should be doing look at "Writing an Alsa Driver" by Takashi Iwai
* available in the Alsa doc section on the website		
* 
* A few notes to make things clearer. The UDA1341 is hooked up to Serial port 4 on the SA1100.
* We are using  SSP mode to talk to the UDA1341. The UDA1341 bit & wordselect clocks are generated
* by this UART. Unfortunately, the clock only runs if the transmit buffer has something in it.
* So, if we are just recording, we feed the transmit DMA stream a bunch of 0x0000 so that the
* transmit buffer is full and the clock keeps going. The zeroes come from FLUSH_BASE_PHYS which
* is a mem loc that always decodes to 0's w/ no off chip access.
*
* Some alsa terminology:
*	frame => num_channels * sample_size  e.g stereo 16 bit is 2 * 16 = 32 bytes
*	period => the least number of bytes that will generate an interrupt e.g. we have a 1024 byte
*             buffer and 4 periods in the runtime structure this means we'll get an int every 256
*             bytes or 4 times per buffer.
*             A number of the sizes are in frames rather than bytes, use frames_to_bytes and
*             bytes_to_frames to convert.  The easiest way to tell the units is to look at the
*             type i.e. runtime-> buffer_size is in frames and its type is snd_pcm_uframes_t
*             
*	Notes about the pointer fxn:
*	The pointer fxn needs to return the offset into the dma buffer in frames.
*	Interrupts must be blocked before calling the dma_get_pos fxn to avoid race with interrupts.
*
*	Notes about pause/resume
*	Implementing this would be complicated so it's skipped.  The problem case is:
*	A full duplex connection is going, then play is paused. At this point you need to start xmitting
*	0's to keep the record active which means you cant just freeze the dma and resume it later you'd
*	need to	save off the dma info, and restore it properly on a resume.  Yeach!
*
*	Notes about transfer methods:
*	The async write calls fail.  I probably need to implement something else to support them?
* 
***************************************************************************************************/

#include <linux/config.h>
#include <sound/driver.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/errno.h>
#include <linux/ioctl.h>
#include <linux/delay.h>
#include <linux/slab.h>

#ifdef CONFIG_PM
#include <linux/pm.h>
#endif

#include <asm/hardware.h>
#include <asm/arch/h3600.h>
#include <asm/mach-types.h>
#include <asm/dma.h>

#ifdef CONFIG_H3600_HAL
#include <asm/semaphore.h>
#include <asm/uaccess.h>
#include <asm/arch/h3600_hal.h>
#endif

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/initval.h>

#include <linux/l3/l3.h>

#undef DEBUG_MODE
#undef DEBUG_FUNCTION_NAMES
#include <sound/uda1341.h>

/*
 * FIXME: Is this enough as autodetection of 2.4.X-rmkY-hhZ kernels?
 * We use DMA stuff from 2.4.18-rmk3-hh24 here to be able to compile this
 * module for Familiar 0.6.1
 */
#ifdef CONFIG_H3600_HAL
#define HH_VERSION 1
#endif

/* {{{ Type definitions */

MODULE_AUTHOR("Tomas Kasparek <tomas.kasparek@seznam.cz>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SA1100/SA1111 + UDA1341TS driver for ALSA");
MODULE_SUPPORTED_DEVICE("{{UDA1341,iPAQ H3600 UDA1341TS}}");

static char *id = NULL;	/* ID for this card */

module_param(id, charp, 0444);
MODULE_PARM_DESC(id, "ID string for SA1100/SA1111 + UDA1341TS soundcard.");

struct audio_stream {
	char *id;		/* identification string */
	int stream_id;		/* numeric identification */	
	dma_device_t dma_dev;	/* device identifier for DMA */
#ifdef HH_VERSION
	dmach_t dmach;		/* dma channel identification */
#else
	dma_regs_t *dma_regs;	/* points to our DMA registers */
#endif
	int active:1;		/* we are using this stream for transfer now */
	int period;		/* current transfer period */
	int periods;		/* current count of periods registerd in the DMA engine */
	int tx_spin;		/* are we recoding - flag used to do DMA trans. for sync */
	unsigned int old_offset;
	spinlock_t dma_lock;	/* for locking in DMA operations (see dma-sa1100.c in the kernel) */
	struct snd_pcm_substream *stream;
};

struct sa11xx_uda1341 {
	struct snd_card *card;
	struct l3_client *uda1341;
	struct snd_pcm *pcm;
	long samplerate;
	struct audio_stream s[2];	/* playback & capture */
};

static unsigned int rates[] = {
	8000,  10666, 10985, 14647,
	16000, 21970, 22050, 24000,
	29400, 32000, 44100, 48000,
};

static struct snd_pcm_hw_constraint_list hw_constraints_rates = {
	.count	= ARRAY_SIZE(rates),
	.list	= rates,
	.mask	= 0,
};

static struct platform_device *device;

/* }}} */

/* {{{ Clock and sample rate stuff */

/*
 * Stop-gap solution until rest of hh.org HAL stuff is merged.
 */
#define GPIO_H3600_CLK_SET0		GPIO_GPIO (12)
#define GPIO_H3600_CLK_SET1		GPIO_GPIO (13)

#ifdef CONFIG_SA1100_H3XXX
#define	clr_sa11xx_uda1341_egpio(x)	clr_h3600_egpio(x)
#define set_sa11xx_uda1341_egpio(x)	set_h3600_egpio(x)
#else
#error This driver could serve H3x00 handhelds only!
#endif

static void sa11xx_uda1341_set_audio_clock(long val)
{
	switch (val) {
	case 24000: case 32000: case 48000:	/* 00: 12.288 MHz */
		GPCR = GPIO_H3600_CLK_SET0 | GPIO_H3600_CLK_SET1;
		break;

	case 22050: case 29400: case 44100:	/* 01: 11.2896 MHz */
		GPSR = GPIO_H3600_CLK_SET0;
		GPCR = GPIO_H3600_CLK_SET1;
		break;

	case 8000: case 10666: case 16000:	/* 10: 4.096 MHz */
		GPCR = GPIO_H3600_CLK_SET0;
		GPSR = GPIO_H3600_CLK_SET1;
		break;

	case 10985: case 14647: case 21970:	/* 11: 5.6245 MHz */
		GPSR = GPIO_H3600_CLK_SET0 | GPIO_H3600_CLK_SET1;
		break;
	}
}

static void sa11xx_uda1341_set_samplerate(struct sa11xx_uda1341 *sa11xx_uda1341, long rate)
{
	int clk_div = 0;
	int clk=0;

	/* We don't want to mess with clocks when frames are in flight */
	Ser4SSCR0 &= ~SSCR0_SSE;
	/* wait for any frame to complete */
	udelay(125);

	/*
	 * We have the following clock sources:
	 * 4.096 MHz, 5.6245 MHz, 11.2896 MHz, 12.288 MHz
	 * Those can be divided either by 256, 384 or 512.
	 * This makes up 12 combinations for the following samplerates...
	 */
	if (rate >= 48000)
		rate = 48000;
	else if (rate >= 44100)
		rate = 44100;
	else if (rate >= 32000)
		rate = 32000;
	else if (rate >= 29400)
		rate = 29400;
	else if (rate >= 24000)
		rate = 24000;
	else if (rate >= 22050)
		rate = 22050;
	else if (rate >= 21970)
		rate = 21970;
	else if (rate >= 16000)
		rate = 16000;
	else if (rate >= 14647)
		rate = 14647;
	else if (rate >= 10985)
		rate = 10985;
	else if (rate >= 10666)
		rate = 10666;
	else
		rate = 8000;

	/* Set the external clock generator */
#ifdef CONFIG_H3600_HAL
	h3600_audio_clock(rate);
#else	
	sa11xx_uda1341_set_audio_clock(rate);
#endif

	/* Select the clock divisor */
	switch (rate) {
	case 8000:
	case 10985:
	case 22050:
	case 24000:
		clk = F512;
		clk_div = SSCR0_SerClkDiv(16);
		break;
	case 16000:
	case 21970:
	case 44100:
	case 48000:
		clk = F256;
		clk_div = SSCR0_SerClkDiv(8);
		break;
	case 10666:
	case 14647:
	case 29400:
	case 32000:
		clk = F384;
		clk_div = SSCR0_SerClkDiv(12);
		break;
	}

	/* FMT setting should be moved away when other FMTs are added (FIXME) */
	l3_command(sa11xx_uda1341->uda1341, CMD_FORMAT, (void *)LSB16);
	
	l3_command(sa11xx_uda1341->uda1341, CMD_FS, (void *)clk);        
	Ser4SSCR0 = (Ser4SSCR0 & ~0xff00) + clk_div + SSCR0_SSE;
	sa11xx_uda1341->samplerate = rate;
}

/* }}} */

/* {{{ HW init and shutdown */

static void sa11xx_uda1341_audio_init(struct sa11xx_uda1341 *sa11xx_uda1341)
{
	unsigned long flags;

	/* Setup DMA stuff */
	sa11xx_uda1341->s[SNDRV_PCM_STREAM_PLAYBACK].id = "UDA1341 out";
	sa11xx_uda1341->s[SNDRV_PCM_STREAM_PLAYBACK].stream_id = SNDRV_PCM_STREAM_PLAYBACK;
	sa11xx_uda1341->s[SNDRV_PCM_STREAM_PLAYBACK].dma_dev = DMA_Ser4SSPWr;

	sa11xx_uda1341->s[SNDRV_PCM_STREAM_CAPTURE].id = "UDA1341 in";
	sa11xx_uda1341->s[SNDRV_PCM_STREAM_CAPTURE].stream_id = SNDRV_PCM_STREAM_CAPTURE;
	sa11xx_uda1341->s[SNDRV_PCM_STREAM_CAPTURE].dma_dev = DMA_Ser4SSPRd;

	/* Initialize the UDA1341 internal state */
       
	/* Setup the uarts */
	local_irq_save(flags);
	GAFR |= (GPIO_SSP_CLK);
	GPDR &= ~(GPIO_SSP_CLK);
	Ser4SSCR0 = 0;
	Ser4SSCR0 = SSCR0_DataSize(16) + SSCR0_TI + SSCR0_SerClkDiv(8);
	Ser4SSCR1 = SSCR1_SClkIactL + SSCR1_SClk1P + SSCR1_ExtClk;
	Ser4SSCR0 |= SSCR0_SSE;
	local_irq_restore(flags);

	/* Enable the audio power */
#ifdef CONFIG_H3600_HAL
	h3600_audio_power(AUDIO_RATE_DEFAULT);
#else
	clr_sa11xx_uda1341_egpio(IPAQ_EGPIO_CODEC_NRESET);
	set_sa11xx_uda1341_egpio(IPAQ_EGPIO_AUDIO_ON);
	set_sa11xx_uda1341_egpio(IPAQ_EGPIO_QMUTE);
#endif
 
	/* Wait for the UDA1341 to wake up */
	mdelay(1); //FIXME - was removed by Perex - Why?

	/* Initialize the UDA1341 internal state */
	l3_open(sa11xx_uda1341->uda1341);
	
	/* external clock configuration (after l3_open - regs must be initialized */
	sa11xx_uda1341_set_samplerate(sa11xx_uda1341, sa11xx_uda1341->samplerate);

	/* Wait for the UDA1341 to wake up */
	set_sa11xx_uda1341_egpio(IPAQ_EGPIO_CODEC_NRESET);
	mdelay(1);	

	/* make the left and right channels unswapped (flip the WS latch) */
	Ser4SSDR = 0;

#ifdef CONFIG_H3600_HAL
	h3600_audio_mute(0);
#else	
	clr_sa11xx_uda1341_egpio(IPAQ_EGPIO_QMUTE);        
#endif     
}

static void sa11xx_uda1341_audio_shutdown(struct sa11xx_uda1341 *sa11xx_uda1341)
{
	/* mute on */
#ifdef CONFIG_H3600_HAL
	h3600_audio_mute(1);
#else	
	set_sa11xx_uda1341_egpio(IPAQ_EGPIO_QMUTE);
#endif
	
	/* disable the audio power and all signals leading to the audio chip */
	l3_close(sa11xx_uda1341->uda1341);
	Ser4SSCR0 = 0;
	clr_sa11xx_uda1341_egpio(IPAQ_EGPIO_CODEC_NRESET);

	/* power off and mute off */
	/* FIXME - is muting off necesary??? */
#ifdef CONFIG_H3600_HAL
	h3600_audio_power(0);
	h3600_audio_mute(0);
#else	
	clr_sa11xx_uda1341_egpio(IPAQ_EGPIO_AUDIO_ON);
	clr_sa11xx_uda1341_egpio(IPAQ_EGPIO_QMUTE);
#endif	
}

/* }}} */

/* {{{ DMA staff */

/*
 * these are the address and sizes used to fill the xmit buffer
 * so we can get a clock in record only mode
 */
#define FORCE_CLOCK_ADDR		(dma_addr_t)FLUSH_BASE_PHYS
#define FORCE_CLOCK_SIZE		4096 // was 2048

// FIXME Why this value exactly - wrote comment
#define DMA_BUF_SIZE	8176	/* <= MAX_DMA_SIZE from asm/arch-sa1100/dma.h */

#ifdef HH_VERSION

static int audio_dma_request(struct audio_stream *s, void (*callback)(void *, int))
{
	int ret;

	ret = sa1100_request_dma(&s->dmach, s->id, s->dma_dev);
	if (ret < 0) {
		printk(KERN_ERR "unable to grab audio dma 0x%x\n", s->dma_dev);
		return ret;
	}
	sa1100_dma_set_callback(s->dmach, callback);
	return 0;
}

static inline void audio_dma_free(struct audio_stream *s)
{
	sa1100_free_dma(s->dmach);
	s->dmach = -1;
}

#else

static int audio_dma_request(struct audio_stream *s, void (*callback)(void *))
{
	int ret;

	ret = sa1100_request_dma(s->dma_dev, s->id, callback, s, &s->dma_regs);
	if (ret < 0)
		printk(KERN_ERR "unable to grab audio dma 0x%x\n", s->dma_dev);
	return ret;
}

static void audio_dma_free(struct audio_stream *s)
{
	sa1100_free_dma(s->dma_regs);
	s->dma_regs = 0;
}

#endif

static u_int audio_get_dma_pos(struct audio_stream *s)
{
	struct snd_pcm_substream *substream = s->stream;
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned int offset;
	unsigned long flags;
	dma_addr_t addr;
	
	// this must be called w/ interrupts locked out see dma-sa1100.c in the kernel
	spin_lock_irqsave(&s->dma_lock, flags);
#ifdef HH_VERSION	
	sa1100_dma_get_current(s->dmach, NULL, &addr);
#else
	addr = sa1100_get_dma_pos((s)->dma_regs);
#endif
	offset = addr - runtime->dma_addr;
	spin_unlock_irqrestore(&s->dma_lock, flags);
	
	offset = bytes_to_frames(runtime,offset);
	if (offset >= runtime->buffer_size)
		offset = 0;

	return offset;
}

/*
 * this stops the dma and clears the dma ptrs
 */
static void audio_stop_dma(struct audio_stream *s)
{
	unsigned long flags;

	spin_lock_irqsave(&s->dma_lock, flags);	
	s->active = 0;
	s->period = 0;
	/* this stops the dma channel and clears the buffer ptrs */
#ifdef HH_VERSION
	sa1100_dma_flush_all(s->dmach);
#else
	sa1100_clear_dma(s->dma_regs);	
#endif
	spin_unlock_irqrestore(&s->dma_lock, flags);
}

static void audio_process_dma(struct audio_stream *s)
{
	struct snd_pcm_substream *substream = s->stream;
	struct snd_pcm_runtime *runtime;
	unsigned int dma_size;		
	unsigned int offset;
	int ret;
                
	/* we are requested to process synchronization DMA transfer */
	if (s->tx_spin) {
		snd_assert(s->stream_id == SNDRV_PCM_STREAM_PLAYBACK, return);
		/* fill the xmit dma buffers and return */
#ifdef HH_VERSION
		sa1100_dma_set_spin(s->dmach, FORCE_CLOCK_ADDR, FORCE_CLOCK_SIZE);
#else
		while (1) {
			ret = sa1100_start_dma(s->dma_regs, FORCE_CLOCK_ADDR, FORCE_CLOCK_SIZE);
			if (ret)
				return;   
		}
#endif
		return;
	}

	/* must be set here - only valid for running streams, not for forced_clock dma fills  */
	runtime = substream->runtime;
	while (s->active && s->periods < runtime->periods) {
		dma_size = frames_to_bytes(runtime, runtime->period_size);
		if (s->old_offset) {
			/* a little trick, we need resume from old position */
			offset = frames_to_bytes(runtime, s->old_offset - 1);
			s->old_offset = 0;
			s->periods = 0;
			s->period = offset / dma_size;
			offset %= dma_size;
			dma_size = dma_size - offset;
			if (!dma_size)
				continue;		/* special case */
		} else {
			offset = dma_size * s->period;
			snd_assert(dma_size <= DMA_BUF_SIZE, );
		}
#ifdef HH_VERSION
		ret = sa1100_dma_queue_buffer(s->dmach, s, runtime->dma_addr + offset, dma_size);
		if (ret)
			return; //FIXME
#else
		ret = sa1100_start_dma((s)->dma_regs, runtime->dma_addr + offset, dma_size);
		if (ret) {
			printk(KERN_ERR "audio_process_dma: cannot queue DMA buffer (%i)\n", ret);
			return;
		}
#endif

		s->period++;
		s->period %= runtime->periods;
		s->periods++;
	}
}

#ifdef HH_VERSION
static void audio_dma_callback(void *data, int size)
#else
static void audio_dma_callback(void *data)
#endif
{
	struct audio_stream *s = data;
        
	/* 
	 * If we are getting a callback for an active stream then we inform
	 * the PCM middle layer we've finished a period
	 */
 	if (s->active)
		snd_pcm_period_elapsed(s->stream);

	spin_lock(&s->dma_lock);
	if (!s->tx_spin && s->periods > 0)
		s->periods--;
	audio_process_dma(s);
	spin_unlock(&s->dma_lock);
}

/* }}} */

/* {{{ PCM setting */

/* {{{ trigger & timer */

static int snd_sa11xx_uda1341_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct sa11xx_uda1341 *chip = snd_pcm_substream_chip(substream);
	int stream_id = substream->pstr->stream;
	struct audio_stream *s = &chip->s[stream_id];
	struct audio_stream *s1 = &chip->s[stream_id ^ 1];
	int err = 0;

	/* note local interrupts are already disabled in the midlevel code */
	spin_lock(&s->dma_lock);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		/* now we need to make sure a record only stream has a clock */
		if (stream_id == SNDRV_PCM_STREAM_CAPTURE && !s1->active) {
			/* we need to force fill the xmit DMA with zeros */
			s1->tx_spin = 1;
			audio_process_dma(s1);
		}
		/* this case is when you were recording then you turn on a
		 * playback stream so we stop (also clears it) the dma first,
		 * clear the sync flag and then we let it turned on
		 */		
		else {
 			s->tx_spin = 0;
 		}

		/* requested stream startup */
		s->active = 1;
		audio_process_dma(s);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		/* requested stream shutdown */
		audio_stop_dma(s);
		
		/*
		 * now we need to make sure a record only stream has a clock
		 * so if we're stopping a playback with an active capture
		 * we need to turn the 0 fill dma on for the xmit side
		 */
		if (stream_id == SNDRV_PCM_STREAM_PLAYBACK && s1->active) {
			/* we need to force fill the xmit DMA with zeros */
			s->tx_spin = 1;
			audio_process_dma(s);
		}
		/*
		 * we killed a capture only stream, so we should also kill
		 * the zero fill transmit
		 */
		else {
			if (s1->tx_spin) {
				s1->tx_spin = 0;
				audio_stop_dma(s1);
			}
		}
		
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
		s->active = 0;
#ifdef HH_VERSION		
		sa1100_dma_stop(s->dmach);
#else
		//FIXME - DMA API
#endif		
		s->old_offset = audio_get_dma_pos(s) + 1;
#ifdef HH_VERSION		
		sa1100_dma_flush_all(s->dmach);
#else
		//FIXME - DMA API
#endif		
		s->periods = 0;
		break;
	case SNDRV_PCM_TRIGGER_RESUME:
		s->active = 1;
		s->tx_spin = 0;
		audio_process_dma(s);
		if (stream_id == SNDRV_PCM_STREAM_CAPTURE && !s1->active) {
			s1->tx_spin = 1;
			audio_process_dma(s1);
		}
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
#ifdef HH_VERSION		
		sa1100_dma_stop(s->dmach);
#else
		//FIXME - DMA API
#endif
		s->active = 0;
		if (stream_id == SNDRV_PCM_STREAM_PLAYBACK) {
			if (s1->active) {
				s->tx_spin = 1;
				s->old_offset = audio_get_dma_pos(s) + 1;
#ifdef HH_VERSION				
				sa1100_dma_flush_all(s->dmach);
#else
				//FIXME - DMA API
#endif				
				audio_process_dma(s);
			}
		} else {
			if (s1->tx_spin) {
				s1->tx_spin = 0;
#ifdef HH_VERSION				
				sa1100_dma_flush_all(s1->dmach);
#else
				//FIXME - DMA API
#endif				
			}
		}
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		s->active = 1;
		if (s->old_offset) {
			s->tx_spin = 0;
			audio_process_dma(s);
			break;
		}
		if (stream_id == SNDRV_PCM_STREAM_CAPTURE && !s1->active) {
			s1->tx_spin = 1;
			audio_process_dma(s1);
		}
#ifdef HH_VERSION		
		sa1100_dma_resume(s->dmach);
#else
		//FIXME - DMA API
#endif
		break;
	default:
		err = -EINVAL;
		break;
	}
	spin_unlock(&s->dma_lock);	
	return err;
}

static int snd_sa11xx_uda1341_prepare(struct snd_pcm_substream *substream)
{
	struct sa11xx_uda1341 *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct audio_stream *s = &chip->s[substream->pstr->stream];
        
	/* set requested samplerate */
	sa11xx_uda1341_set_samplerate(chip, runtime->rate);

	/* set requestd format when available */
	/* set FMT here !!! FIXME */

	s->period = 0;
	s->periods = 0;
        
	return 0;
}

static snd_pcm_uframes_t snd_sa11xx_uda1341_pointer(struct snd_pcm_substream *substream)
{
	struct sa11xx_uda1341 *chip = snd_pcm_substream_chip(substream);
	return audio_get_dma_pos(&chip->s[substream->pstr->stream]);
}

/* }}} */

static struct snd_pcm_hardware snd_sa11xx_uda1341_capture =
{
	.info			= (SNDRV_PCM_INFO_INTERLEAVED |
				   SNDRV_PCM_INFO_BLOCK_TRANSFER |
				   SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID |
				   SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_RESUME),
	.formats		= SNDRV_PCM_FMTBIT_S16_LE,
	.rates			= (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |\
				   SNDRV_PCM_RATE_22050 | SNDRV_PCM_RATE_32000 |\
				   SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 |\
				   SNDRV_PCM_RATE_KNOT),
	.rate_min		= 8000,
	.rate_max		= 48000,
	.channels_min		= 2,
	.channels_max		= 2,
	.buffer_bytes_max	= 64*1024,
	.period_bytes_min	= 64,
	.period_bytes_max	= DMA_BUF_SIZE,
	.periods_min		= 2,
	.periods_max		= 255,
	.fifo_size		= 0,
};

static struct snd_pcm_hardware snd_sa11xx_uda1341_playback =
{
	.info			= (SNDRV_PCM_INFO_INTERLEAVED |
				   SNDRV_PCM_INFO_BLOCK_TRANSFER |
				   SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID |
				   SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_RESUME),
	.formats		= SNDRV_PCM_FMTBIT_S16_LE,
	.rates			= (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |\
                                   SNDRV_PCM_RATE_22050 | SNDRV_PCM_RATE_32000 |\
				   SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 |\
				   SNDRV_PCM_RATE_KNOT),
	.rate_min		= 8000,
	.rate_max		= 48000,
	.channels_min		= 2,
	.channels_max		= 2,
	.buffer_bytes_max	= 64*1024,
	.period_bytes_min	= 64,
	.period_bytes_max	= DMA_BUF_SIZE,
	.periods_min		= 2,
	.periods_max		= 255,
	.fifo_size		= 0,
};

static int snd_card_sa11xx_uda1341_open(struct snd_pcm_substream *substream)
{
	struct sa11xx_uda1341 *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	int stream_id = substream->pstr->stream;
	int err;

	chip->s[stream_id].stream = substream;

	if (stream_id == SNDRV_PCM_STREAM_PLAYBACK)
		runtime->hw = snd_sa11xx_uda1341_playback;
	else
		runtime->hw = snd_sa11xx_uda1341_capture;
	if ((err = snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS)) < 0)
		return err;
	if ((err = snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE, &hw_constraints_rates)) < 0)
		return err;
        
	return 0;
}

static int snd_card_sa11xx_uda1341_close(struct snd_pcm_substream *substream)
{
	struct sa11xx_uda1341 *chip = snd_pcm_substream_chip(substream);

	chip->s[substream->pstr->stream].stream = NULL;
	return 0;
}

/* {{{ HW params & free */

static int snd_sa11xx_uda1341_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *hw_params)
{
        
	return snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params));
}

static int snd_sa11xx_uda1341_hw_free(struct snd_pcm_substream *substream)
{
	return snd_pcm_lib_free_pages(substream);
}

/* }}} */

static struct snd_pcm_ops snd_card_sa11xx_uda1341_playback_ops = {
	.open			= snd_card_sa11xx_uda1341_open,
	.close			= snd_card_sa11xx_uda1341_close,
	.ioctl			= snd_pcm_lib_ioctl,
	.hw_params	        = snd_sa11xx_uda1341_hw_params,
	.hw_free	        = snd_sa11xx_uda1341_hw_free,
	.prepare		= snd_sa11xx_uda1341_prepare,
	.trigger		= snd_sa11xx_uda1341_trigger,
	.pointer		= snd_sa11xx_uda1341_pointer,
};

static struct snd_pcm_ops snd_card_sa11xx_uda1341_capture_ops = {
	.open			= snd_card_sa11xx_uda1341_open,
	.close			= snd_card_sa11xx_uda1341_close,
	.ioctl			= snd_pcm_lib_ioctl,
	.hw_params	        = snd_sa11xx_uda1341_hw_params,
	.hw_free	        = snd_sa11xx_uda1341_hw_free,
	.prepare		= snd_sa11xx_uda1341_prepare,
	.trigger		= snd_sa11xx_uda1341_trigger,
	.pointer		= snd_sa11xx_uda1341_pointer,
};

static int __init snd_card_sa11xx_uda1341_pcm(struct sa11xx_uda1341 *sa11xx_uda1341, int device)
{
	struct snd_pcm *pcm;
	int err;

	if ((err = snd_pcm_new(sa11xx_uda1341->card, "UDA1341 PCM", device, 1, 1, &pcm)) < 0)
		return err;

	/*
	 * this sets up our initial buffers and sets the dma_type to isa.
	 * isa works but I'm not sure why (or if) it's the right choice
	 * this may be too large, trying it for now
	 */
	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV, 
					      snd_dma_isa_data(),
					      64*1024, 64*1024);

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_card_sa11xx_uda1341_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_card_sa11xx_uda1341_capture_ops);
	pcm->private_data = sa11xx_uda1341;
	pcm->info_flags = 0;
	strcpy(pcm->name, "UDA1341 PCM");

	sa11xx_uda1341_audio_init(sa11xx_uda1341);

	/* setup DMA controller */
	audio_dma_request(&sa11xx_uda1341->s[SNDRV_PCM_STREAM_PLAYBACK], audio_dma_callback);
	audio_dma_request(&sa11xx_uda1341->s[SNDRV_PCM_STREAM_CAPTURE], audio_dma_callback);

	sa11xx_uda1341->pcm = pcm;

	return 0;
}

/* }}} */

/* {{{ module init & exit */

#ifdef CONFIG_PM

static int snd_sa11xx_uda1341_suspend(struct platform_device *devptr,
				      pm_message_t state)
{
	struct snd_card *card = platform_get_drvdata(devptr);
	struct sa11xx_uda1341 *chip = card->private_data;

	snd_power_change_state(card, SNDRV_CTL_POWER_D3hot);
	snd_pcm_suspend_all(chip->pcm);
#ifdef HH_VERSION
	sa1100_dma_sleep(chip->s[SNDRV_PCM_STREAM_PLAYBACK].dmach);
	sa1100_dma_sleep(chip->s[SNDRV_PCM_STREAM_CAPTURE].dmach);
#else
	//FIXME
#endif
	l3_command(chip->uda1341, CMD_SUSPEND, NULL);
	sa11xx_uda1341_audio_shutdown(chip);

	return 0;
}

static int snd_sa11xx_uda1341_resume(struct platform_device *devptr)
{
	struct snd_card *card = platform_get_drvdata(devptr);
	struct sa11xx_uda1341 *chip = card->private_data;

	sa11xx_uda1341_audio_init(chip);
	l3_command(chip->uda1341, CMD_RESUME, NULL);
#ifdef HH_VERSION	
	sa1100_dma_wakeup(chip->s[SNDRV_PCM_STREAM_PLAYBACK].dmach);
	sa1100_dma_wakeup(chip->s[SNDRV_PCM_STREAM_CAPTURE].dmach);
#else
	//FIXME
#endif
	snd_power_change_state(card, SNDRV_CTL_POWER_D0);
	return 0;
}
#endif /* COMFIG_PM */

void snd_sa11xx_uda1341_free(struct snd_card *card)
{
	struct sa11xx_uda1341 *chip = card->private_data;

	audio_dma_free(&chip->s[SNDRV_PCM_STREAM_PLAYBACK]);
	audio_dma_free(&chip->s[SNDRV_PCM_STREAM_CAPTURE]);
}

static int __init sa11xx_uda1341_probe(struct platform_device *devptr)
{
	int err;
	struct snd_card *card;
	struct sa11xx_uda1341 *chip;

	/* register the soundcard */
	card = snd_card_new(-1, id, THIS_MODULE, sizeof(struct sa11xx_uda1341));
	if (card == NULL)
		return -ENOMEM;

	chip = card->private_data;
	spin_lock_init(&chip->s[0].dma_lock);
	spin_lock_init(&chip->s[1].dma_lock);

	card->private_free = snd_sa11xx_uda1341_free;
	chip->card = card;
	chip->samplerate = AUDIO_RATE_DEFAULT;

	// mixer
	if ((err = snd_chip_uda1341_mixer_new(card, &chip->uda1341)))
		goto nodev;

	// PCM
	if ((err = snd_card_sa11xx_uda1341_pcm(chip, 0)) < 0)
		goto nodev;
        
	strcpy(card->driver, "UDA1341");
	strcpy(card->shortname, "H3600 UDA1341TS");
	sprintf(card->longname, "Compaq iPAQ H3600 with Philips UDA1341TS");
        
	snd_card_set_dev(card, &devptr->dev);

	if ((err = snd_card_register(card)) == 0) {
		printk( KERN_INFO "iPAQ audio support initialized\n" );
		platform_set_drvdata(devptr, card);
		return 0;
	}
        
 nodev:
	snd_card_free(card);
	return err;
}

static int __devexit sa11xx_uda1341_remove(struct platform_device *devptr)
{
	snd_card_free(platform_get_drvdata(devptr));
	platform_set_drvdata(devptr, NULL);
	return 0;
}

#define SA11XX_UDA1341_DRIVER	"sa11xx_uda1341"

static struct platform_driver sa11xx_uda1341_driver = {
	.probe		= sa11xx_uda1341_probe,
	.remove		= __devexit_p(sa11xx_uda1341_remove),
#ifdef CONFIG_PM
	.suspend	= snd_sa11xx_uda1341_suspend,
	.resume		= snd_sa11xx_uda1341_resume,
#endif
	.driver		= {
		.name	= SA11XX_UDA1341_DRIVER,
	},
};

static int __init sa11xx_uda1341_init(void)
{
	int err;

	if (!machine_is_h3xxx())
		return -ENODEV;
	if ((err = platform_driver_register(&sa11xx_uda1341_driver)) < 0)
		return err;
	device = platform_device_register_simple(SA11XX_UDA1341_DRIVER, -1, NULL, 0);
	if (IS_ERR(device)) {
		platform_driver_unregister(&sa11xx_uda1341_driver);
		return PTR_ERR(device);
	}
	return 0;
}

static void __exit sa11xx_uda1341_exit(void)
{
	platform_device_unregister(device);
	platform_driver_unregister(&sa11xx_uda1341_driver);
}

module_init(sa11xx_uda1341_init);
module_exit(sa11xx_uda1341_exit);

/* }}} */

/*
 * Local variables:
 * indent-tabs-mode: t
 * End:
 */
