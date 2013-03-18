/*
 * sound/oss/sb_audio.c
 *
 * Audio routines for Sound Blaster compatible cards.
 *
 *
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 * OSS/Free for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 *
 * Changes
 *	Alan Cox	:	Formatting and clean ups
 *
 * Status
 *	Mostly working. Weird uart bug causing irq storms
 *
 * Daniel J. Rodriksson: Changes to make sb16 work full duplex.
 *                       Maybe other 16 bit cards in this code could behave
 *                       the same.
 * Chris Rankin:         Use spinlocks instead of CLI/STI
 */

#include <linux/spinlock.h>

#include "sound_config.h"

#include "sb_mixer.h"
#include "sb.h"

#include "sb_ess.h"

int sb_audio_open(int dev, int mode)
{
	sb_devc *devc = audio_devs[dev]->devc;
	unsigned long flags;

	if (devc == NULL)
	{
		  printk(KERN_ERR "Sound Blaster: incomplete initialization.\n");
		  return -ENXIO;
	}
	if (devc->caps & SB_NO_RECORDING && mode & OPEN_READ)
	{
		if (mode == OPEN_READ)
			return -EPERM;
	}
	spin_lock_irqsave(&devc->lock, flags);
	if (devc->opened)
	{
		  spin_unlock_irqrestore(&devc->lock, flags);
		  return -EBUSY;
	}
	if (devc->dma16 != -1 && devc->dma16 != devc->dma8 && !devc->duplex)
	{
		if (sound_open_dma(devc->dma16, "Sound Blaster 16 bit"))
		{
		  	spin_unlock_irqrestore(&devc->lock, flags);
			return -EBUSY;
		}
	}
	devc->opened = mode;
	spin_unlock_irqrestore(&devc->lock, flags);

	devc->irq_mode = IMODE_NONE;
	devc->irq_mode_16 = IMODE_NONE;
	devc->fullduplex = devc->duplex &&
		((mode & OPEN_READ) && (mode & OPEN_WRITE));
	sb_dsp_reset(devc);

	/* At first glance this check isn't enough, some ESS chips might not 
	 * have a RECLEV. However if they don't common_mixer_set will refuse 
	 * cause devc->iomap has no register mapping for RECLEV
	 */
	if (devc->model == MDL_ESS) ess_mixer_reload (devc, SOUND_MIXER_RECLEV);

	/* The ALS007 seems to require that the DSP be removed from the output */
	/* in order for recording to be activated properly.  This is done by   */
	/* setting the appropriate bits of the output control register 4ch to  */
	/* zero.  This code assumes that the output control registers are not  */
	/* used anywhere else and therefore the DSP bits are *always* ON for   */
	/* output and OFF for sampling.                                        */

	if (devc->submodel == SUBMDL_ALS007) 
	{
		if (mode & OPEN_READ) 
			sb_setmixer(devc,ALS007_OUTPUT_CTRL2,
				sb_getmixer(devc,ALS007_OUTPUT_CTRL2) & 0xf9);
		else
			sb_setmixer(devc,ALS007_OUTPUT_CTRL2,
				sb_getmixer(devc,ALS007_OUTPUT_CTRL2) | 0x06);
	}
	return 0;
}

void sb_audio_close(int dev)
{
	sb_devc *devc = audio_devs[dev]->devc;

	/* fix things if mmap turned off fullduplex */
	if(devc->duplex
	   && !devc->fullduplex
	   && (devc->opened & OPEN_READ) && (devc->opened & OPEN_WRITE))
	{
		struct dma_buffparms *dmap_temp;
		dmap_temp = audio_devs[dev]->dmap_out;
		audio_devs[dev]->dmap_out = audio_devs[dev]->dmap_in;
		audio_devs[dev]->dmap_in = dmap_temp;
	}
	audio_devs[dev]->dmap_out->dma = devc->dma8;
	audio_devs[dev]->dmap_in->dma = ( devc->duplex ) ?
		devc->dma16 : devc->dma8;

	if (devc->dma16 != -1 && devc->dma16 != devc->dma8 && !devc->duplex)
		sound_close_dma(devc->dma16);

	/* For ALS007, turn DSP output back on if closing the device for read */
	
	if ((devc->submodel == SUBMDL_ALS007) && (devc->opened & OPEN_READ)) 
	{
		sb_setmixer(devc,ALS007_OUTPUT_CTRL2,
			sb_getmixer(devc,ALS007_OUTPUT_CTRL2) | 0x06);
	}
	devc->opened = 0;
}

static void sb_set_output_parms(int dev, unsigned long buf, int nr_bytes,
		    int intrflag)
{
	sb_devc *devc = audio_devs[dev]->devc;

	if (!devc->fullduplex || devc->bits == AFMT_S16_LE)
	{
		devc->trg_buf = buf;
		devc->trg_bytes = nr_bytes;
		devc->trg_intrflag = intrflag;
		devc->irq_mode = IMODE_OUTPUT;
	}
	else
	{
		devc->trg_buf_16 = buf;
		devc->trg_bytes_16 = nr_bytes;
		devc->trg_intrflag_16 = intrflag;
		devc->irq_mode_16 = IMODE_OUTPUT;
	}
}

static void sb_set_input_parms(int dev, unsigned long buf, int count, int intrflag)
{
	sb_devc *devc = audio_devs[dev]->devc;

	if (!devc->fullduplex || devc->bits != AFMT_S16_LE)
	{
		devc->trg_buf = buf;
		devc->trg_bytes = count;
		devc->trg_intrflag = intrflag;
		devc->irq_mode = IMODE_INPUT;
	}
	else
	{
		devc->trg_buf_16 = buf;
		devc->trg_bytes_16 = count;
		devc->trg_intrflag_16 = intrflag;
		devc->irq_mode_16 = IMODE_INPUT;
	}
}

/*
 * SB1.x compatible routines 
 */

static void sb1_audio_output_block(int dev, unsigned long buf, int nr_bytes, int intrflag)
{
	unsigned long flags;
	int count = nr_bytes;
	sb_devc *devc = audio_devs[dev]->devc;

	/* DMAbuf_start_dma (dev, buf, count, DMA_MODE_WRITE); */

	if (audio_devs[dev]->dmap_out->dma > 3)
		count >>= 1;
	count--;

	devc->irq_mode = IMODE_OUTPUT;

	spin_lock_irqsave(&devc->lock, flags);
	if (sb_dsp_command(devc, 0x14))		/* 8 bit DAC using DMA */
	{
		sb_dsp_command(devc, (unsigned char) (count & 0xff));
		sb_dsp_command(devc, (unsigned char) ((count >> 8) & 0xff));
	}
	else
		printk(KERN_WARNING "Sound Blaster:  unable to start DAC.\n");
	spin_unlock_irqrestore(&devc->lock, flags);
	devc->intr_active = 1;
}

static void sb1_audio_start_input(int dev, unsigned long buf, int nr_bytes, int intrflag)
{
	unsigned long flags;
	int count = nr_bytes;
	sb_devc *devc = audio_devs[dev]->devc;

	/*
	 * Start a DMA input to the buffer pointed by dmaqtail
	 */

	/* DMAbuf_start_dma (dev, buf, count, DMA_MODE_READ); */

	if (audio_devs[dev]->dmap_out->dma > 3)
		count >>= 1;
	count--;

	devc->irq_mode = IMODE_INPUT;

	spin_lock_irqsave(&devc->lock, flags);
	if (sb_dsp_command(devc, 0x24))		/* 8 bit ADC using DMA */
	{
		sb_dsp_command(devc, (unsigned char) (count & 0xff));
		sb_dsp_command(devc, (unsigned char) ((count >> 8) & 0xff));
	}
	else
		printk(KERN_ERR "Sound Blaster:  unable to start ADC.\n");
	spin_unlock_irqrestore(&devc->lock, flags);

	devc->intr_active = 1;
}

static void sb1_audio_trigger(int dev, int bits)
{
	sb_devc *devc = audio_devs[dev]->devc;

	bits &= devc->irq_mode;

	if (!bits)
		sb_dsp_command(devc, 0xd0);	/* Halt DMA */
	else
	{
		switch (devc->irq_mode)
		{
			case IMODE_INPUT:
				sb1_audio_start_input(dev, devc->trg_buf, devc->trg_bytes,
						devc->trg_intrflag);
				break;

			case IMODE_OUTPUT:
				sb1_audio_output_block(dev, devc->trg_buf, devc->trg_bytes,
						devc->trg_intrflag);
				break;
		}
	}
	devc->trigger_bits = bits;
}

static int sb1_audio_prepare_for_input(int dev, int bsize, int bcount)
{
	sb_devc *devc = audio_devs[dev]->devc;
	unsigned long flags;

	spin_lock_irqsave(&devc->lock, flags);
	if (sb_dsp_command(devc, 0x40))
		sb_dsp_command(devc, devc->tconst);
	sb_dsp_command(devc, DSP_CMD_SPKOFF);
	spin_unlock_irqrestore(&devc->lock, flags);

	devc->trigger_bits = 0;
	return 0;
}

static int sb1_audio_prepare_for_output(int dev, int bsize, int bcount)
{
	sb_devc *devc = audio_devs[dev]->devc;
	unsigned long flags;

	spin_lock_irqsave(&devc->lock, flags);
	if (sb_dsp_command(devc, 0x40))
		sb_dsp_command(devc, devc->tconst);
	sb_dsp_command(devc, DSP_CMD_SPKON);
	spin_unlock_irqrestore(&devc->lock, flags);
	devc->trigger_bits = 0;
	return 0;
}

static int sb1_audio_set_speed(int dev, int speed)
{
	int max_speed = 23000;
	sb_devc *devc = audio_devs[dev]->devc;
	int tmp;

	if (devc->opened & OPEN_READ)
		max_speed = 13000;

	if (speed > 0)
	{
		if (speed < 4000)
			speed = 4000;

		if (speed > max_speed)
			speed = max_speed;

		devc->tconst = (256 - ((1000000 + speed / 2) / speed)) & 0xff;
		tmp = 256 - devc->tconst;
		speed = (1000000 + tmp / 2) / tmp;

		devc->speed = speed;
	}
	return devc->speed;
}

static short sb1_audio_set_channels(int dev, short channels)
{
	sb_devc *devc = audio_devs[dev]->devc;
	return devc->channels = 1;
}

static unsigned int sb1_audio_set_bits(int dev, unsigned int bits)
{
	sb_devc        *devc = audio_devs[dev]->devc;
	return devc->bits = 8;
}

static void sb1_audio_halt_xfer(int dev)
{
	unsigned long flags;
	sb_devc *devc = audio_devs[dev]->devc;

	spin_lock_irqsave(&devc->lock, flags);
	sb_dsp_reset(devc);
	spin_unlock_irqrestore(&devc->lock, flags);
}

/*
 * SB 2.0 and SB 2.01 compatible routines
 */

static void sb20_audio_output_block(int dev, unsigned long buf, int nr_bytes,
			int intrflag)
{
	unsigned long flags;
	int count = nr_bytes;
	sb_devc *devc = audio_devs[dev]->devc;
	unsigned char cmd;

	/* DMAbuf_start_dma (dev, buf, count, DMA_MODE_WRITE); */

	if (audio_devs[dev]->dmap_out->dma > 3)
		count >>= 1;
	count--;

	devc->irq_mode = IMODE_OUTPUT;

	spin_lock_irqsave(&devc->lock, flags);
	if (sb_dsp_command(devc, 0x48))		/* DSP Block size */
	{
		sb_dsp_command(devc, (unsigned char) (count & 0xff));
		sb_dsp_command(devc, (unsigned char) ((count >> 8) & 0xff));

		if (devc->speed * devc->channels <= 23000)
			cmd = 0x1c;	/* 8 bit PCM output */
		else
			cmd = 0x90;	/* 8 bit high speed PCM output (SB2.01/Pro) */

		if (!sb_dsp_command(devc, cmd))
			printk(KERN_ERR "Sound Blaster:  unable to start DAC.\n");
	}
	else
		printk(KERN_ERR "Sound Blaster: unable to start DAC.\n");
	spin_unlock_irqrestore(&devc->lock, flags);
	devc->intr_active = 1;
}

static void sb20_audio_start_input(int dev, unsigned long buf, int nr_bytes, int intrflag)
{
	unsigned long flags;
	int count = nr_bytes;
	sb_devc *devc = audio_devs[dev]->devc;
	unsigned char cmd;

	/*
	 * Start a DMA input to the buffer pointed by dmaqtail
	 */

	/* DMAbuf_start_dma (dev, buf, count, DMA_MODE_READ); */

	if (audio_devs[dev]->dmap_out->dma > 3)
		count >>= 1;
	count--;

	devc->irq_mode = IMODE_INPUT;

	spin_lock_irqsave(&devc->lock, flags);
	if (sb_dsp_command(devc, 0x48))		/* DSP Block size */
	{
		sb_dsp_command(devc, (unsigned char) (count & 0xff));
		sb_dsp_command(devc, (unsigned char) ((count >> 8) & 0xff));

		if (devc->speed * devc->channels <= (devc->major == 3 ? 23000 : 13000))
			cmd = 0x2c;	/* 8 bit PCM input */
		else
			cmd = 0x98;	/* 8 bit high speed PCM input (SB2.01/Pro) */

		if (!sb_dsp_command(devc, cmd))
			printk(KERN_ERR "Sound Blaster:  unable to start ADC.\n");
	}
	else
		printk(KERN_ERR "Sound Blaster:  unable to start ADC.\n");
	spin_unlock_irqrestore(&devc->lock, flags);
	devc->intr_active = 1;
}

static void sb20_audio_trigger(int dev, int bits)
{
	sb_devc *devc = audio_devs[dev]->devc;
	bits &= devc->irq_mode;

	if (!bits)
		sb_dsp_command(devc, 0xd0);	/* Halt DMA */
	else
	{
		switch (devc->irq_mode)
		{
			case IMODE_INPUT:
				sb20_audio_start_input(dev, devc->trg_buf, devc->trg_bytes,
						devc->trg_intrflag);
				break;

			case IMODE_OUTPUT:
				sb20_audio_output_block(dev, devc->trg_buf, devc->trg_bytes,
						devc->trg_intrflag);
			    break;
		}
	}
	devc->trigger_bits = bits;
}

/*
 * SB2.01 specific speed setup
 */

static int sb201_audio_set_speed(int dev, int speed)
{
	sb_devc *devc = audio_devs[dev]->devc;
	int tmp;
	int s;

	if (speed > 0)
	{
		if (speed < 4000)
			speed = 4000;
		if (speed > 44100)
			speed = 44100;
		if (devc->opened & OPEN_READ && speed > 15000)
			speed = 15000;
		s = speed * devc->channels;
		devc->tconst = (256 - ((1000000 + s / 2) / s)) & 0xff;
		tmp = 256 - devc->tconst;
		speed = ((1000000 + tmp / 2) / tmp) / devc->channels;

		devc->speed = speed;
	}
	return devc->speed;
}

/*
 * SB Pro specific routines
 */

static int sbpro_audio_prepare_for_input(int dev, int bsize, int bcount)
{				/* For SB Pro and Jazz16 */
	sb_devc *devc = audio_devs[dev]->devc;
	unsigned long flags;
	unsigned char bits = 0;

	if (devc->dma16 >= 0 && devc->dma16 != devc->dma8)
		audio_devs[dev]->dmap_out->dma = audio_devs[dev]->dmap_in->dma =
			devc->bits == 16 ? devc->dma16 : devc->dma8;

	if (devc->model == MDL_JAZZ || devc->model == MDL_SMW)
		if (devc->bits == AFMT_S16_LE)
			bits = 0x04;	/* 16 bit mode */

	spin_lock_irqsave(&devc->lock, flags);
	if (sb_dsp_command(devc, 0x40))
		sb_dsp_command(devc, devc->tconst);
	sb_dsp_command(devc, DSP_CMD_SPKOFF);
	if (devc->channels == 1)
		sb_dsp_command(devc, 0xa0 | bits);	/* Mono input */
	else
		sb_dsp_command(devc, 0xa8 | bits);	/* Stereo input */
	spin_unlock_irqrestore(&devc->lock, flags);

	devc->trigger_bits = 0;
	return 0;
}

static int sbpro_audio_prepare_for_output(int dev, int bsize, int bcount)
{				/* For SB Pro and Jazz16 */
	sb_devc *devc = audio_devs[dev]->devc;
	unsigned long flags;
	unsigned char tmp;
	unsigned char bits = 0;

	if (devc->dma16 >= 0 && devc->dma16 != devc->dma8)
		audio_devs[dev]->dmap_out->dma = audio_devs[dev]->dmap_in->dma = devc->bits == 16 ? devc->dma16 : devc->dma8;
	if (devc->model == MDL_SBPRO)
		sb_mixer_set_stereo(devc, devc->channels == 2);

	spin_lock_irqsave(&devc->lock, flags);
	if (sb_dsp_command(devc, 0x40))
		sb_dsp_command(devc, devc->tconst);
	sb_dsp_command(devc, DSP_CMD_SPKON);

	if (devc->model == MDL_JAZZ || devc->model == MDL_SMW)
	{
		if (devc->bits == AFMT_S16_LE)
			bits = 0x04;	/* 16 bit mode */

		if (devc->channels == 1)
			sb_dsp_command(devc, 0xa0 | bits);	/* Mono output */
		else
			sb_dsp_command(devc, 0xa8 | bits);	/* Stereo output */
		spin_unlock_irqrestore(&devc->lock, flags);
	}
	else
	{
		spin_unlock_irqrestore(&devc->lock, flags);
		tmp = sb_getmixer(devc, 0x0e);
		if (devc->channels == 1)
			tmp &= ~0x02;
		else
			tmp |= 0x02;
		sb_setmixer(devc, 0x0e, tmp);
	}
	devc->trigger_bits = 0;
	return 0;
}

static int sbpro_audio_set_speed(int dev, int speed)
{
	sb_devc *devc = audio_devs[dev]->devc;

	if (speed > 0)
	{
		if (speed < 4000)
			speed = 4000;
		if (speed > 44100)
			speed = 44100;
		if (devc->channels > 1 && speed > 22050)
			speed = 22050;
		sb201_audio_set_speed(dev, speed);
	}
	return devc->speed;
}

static short sbpro_audio_set_channels(int dev, short channels)
{
	sb_devc *devc = audio_devs[dev]->devc;

	if (channels == 1 || channels == 2)
	{
		if (channels != devc->channels)
		{
			devc->channels = channels;
			if (devc->model == MDL_SBPRO && devc->channels == 2)
				sbpro_audio_set_speed(dev, devc->speed);
		}
	}
	return devc->channels;
}

static int jazz16_audio_set_speed(int dev, int speed)
{
	sb_devc *devc = audio_devs[dev]->devc;

	if (speed > 0)
	{
		int tmp;
		int s;

		if (speed < 5000)
			speed = 5000;
		if (speed > 44100)
			speed = 44100;

		s = speed * devc->channels;

		devc->tconst = (256 - ((1000000 + s / 2) / s)) & 0xff;

		tmp = 256 - devc->tconst;
		speed = ((1000000 + tmp / 2) / tmp) / devc->channels;

		devc->speed = speed;
	}
	return devc->speed;
}

/*
 * SB16 specific routines
 */

static int sb16_audio_set_speed(int dev, int speed)
{
	sb_devc *devc = audio_devs[dev]->devc;
	int	max_speed = devc->submodel == SUBMDL_ALS100 ? 48000 : 44100;

	if (speed > 0)
	{
		if (speed < 5000)
			speed = 5000;

		if (speed > max_speed)
			speed = max_speed;

		devc->speed = speed;
	}
	return devc->speed;
}

static unsigned int sb16_audio_set_bits(int dev, unsigned int bits)
{
	sb_devc *devc = audio_devs[dev]->devc;

	if (bits != 0)
	{
		if (bits == AFMT_U8 || bits == AFMT_S16_LE)
			devc->bits = bits;
		else
			devc->bits = AFMT_U8;
	}

	return devc->bits;
}

static int sb16_audio_prepare_for_input(int dev, int bsize, int bcount)
{
	sb_devc *devc = audio_devs[dev]->devc;

	if (!devc->fullduplex)
	{
		audio_devs[dev]->dmap_out->dma =
			audio_devs[dev]->dmap_in->dma =
				devc->bits == AFMT_S16_LE ?
					devc->dma16 : devc->dma8;
	}
	else if (devc->bits == AFMT_S16_LE)
	{
		audio_devs[dev]->dmap_out->dma = devc->dma8;
		audio_devs[dev]->dmap_in->dma = devc->dma16;
	}
	else
	{
		audio_devs[dev]->dmap_out->dma = devc->dma16;
		audio_devs[dev]->dmap_in->dma = devc->dma8;
	}

	devc->trigger_bits = 0;
	return 0;
}

static int sb16_audio_prepare_for_output(int dev, int bsize, int bcount)
{
	sb_devc *devc = audio_devs[dev]->devc;

	if (!devc->fullduplex)
	{
		audio_devs[dev]->dmap_out->dma =
			audio_devs[dev]->dmap_in->dma =
				devc->bits == AFMT_S16_LE ?
					devc->dma16 : devc->dma8;
	}
	else if (devc->bits == AFMT_S16_LE)
	{
		audio_devs[dev]->dmap_out->dma = devc->dma8;
		audio_devs[dev]->dmap_in->dma = devc->dma16;
	}
	else
	{
		audio_devs[dev]->dmap_out->dma = devc->dma16;
		audio_devs[dev]->dmap_in->dma = devc->dma8;
	}

	devc->trigger_bits = 0;
	return 0;
}

static void sb16_audio_output_block(int dev, unsigned long buf, int count,
			int intrflag)
{
	unsigned long   flags, cnt;
	sb_devc        *devc = audio_devs[dev]->devc;
	unsigned long   bits;

	if (!devc->fullduplex || devc->bits == AFMT_S16_LE)
	{
		devc->irq_mode = IMODE_OUTPUT;
		devc->intr_active = 1;
	}
	else
	{
		devc->irq_mode_16 = IMODE_OUTPUT;
		devc->intr_active_16 = 1;
	}

	/* save value */
	spin_lock_irqsave(&devc->lock, flags);
	bits = devc->bits;
	if (devc->fullduplex)
		devc->bits = (devc->bits == AFMT_S16_LE) ?
			AFMT_U8 : AFMT_S16_LE;
	spin_unlock_irqrestore(&devc->lock, flags);

	cnt = count;
	if (devc->bits == AFMT_S16_LE)
		cnt >>= 1;
	cnt--;

	spin_lock_irqsave(&devc->lock, flags);

	/* DMAbuf_start_dma (dev, buf, count, DMA_MODE_WRITE); */

	sb_dsp_command(devc, 0x41);
	sb_dsp_command(devc, (unsigned char) ((devc->speed >> 8) & 0xff));
	sb_dsp_command(devc, (unsigned char) (devc->speed & 0xff));

	sb_dsp_command(devc, (devc->bits == AFMT_S16_LE ? 0xb6 : 0xc6));
	sb_dsp_command(devc, ((devc->channels == 2 ? 0x20 : 0) +
			      (devc->bits == AFMT_S16_LE ? 0x10 : 0)));
	sb_dsp_command(devc, (unsigned char) (cnt & 0xff));
	sb_dsp_command(devc, (unsigned char) (cnt >> 8));

	/* restore real value after all programming */
	devc->bits = bits;
	spin_unlock_irqrestore(&devc->lock, flags);
}


/*
 *	This fails on the Cyrix MediaGX. If you don't have the DMA enabled
 *	before the first sample arrives it locks up. However even if you
 *	do enable the DMA in time you just get DMA timeouts and missing
 *	interrupts and stuff, so for now I've not bothered fixing this either.
 */
 
static void sb16_audio_start_input(int dev, unsigned long buf, int count, int intrflag)
{
	unsigned long   flags, cnt;
	sb_devc        *devc = audio_devs[dev]->devc;

	if (!devc->fullduplex || devc->bits != AFMT_S16_LE)
	{
		devc->irq_mode = IMODE_INPUT;
		devc->intr_active = 1;
	}
	else
	{
		devc->irq_mode_16 = IMODE_INPUT;
		devc->intr_active_16 = 1;
	}

	cnt = count;
	if (devc->bits == AFMT_S16_LE)
		cnt >>= 1;
	cnt--;

	spin_lock_irqsave(&devc->lock, flags);

	/* DMAbuf_start_dma (dev, buf, count, DMA_MODE_READ); */

	sb_dsp_command(devc, 0x42);
	sb_dsp_command(devc, (unsigned char) ((devc->speed >> 8) & 0xff));
	sb_dsp_command(devc, (unsigned char) (devc->speed & 0xff));

	sb_dsp_command(devc, (devc->bits == AFMT_S16_LE ? 0xbe : 0xce));
	sb_dsp_command(devc, ((devc->channels == 2 ? 0x20 : 0) +
			      (devc->bits == AFMT_S16_LE ? 0x10 : 0)));
	sb_dsp_command(devc, (unsigned char) (cnt & 0xff));
	sb_dsp_command(devc, (unsigned char) (cnt >> 8));

	spin_unlock_irqrestore(&devc->lock, flags);
}

static void sb16_audio_trigger(int dev, int bits)
{
	sb_devc *devc = audio_devs[dev]->devc;

	int bits_16 = bits & devc->irq_mode_16;
	bits &= devc->irq_mode;

	if (!bits && !bits_16)
		sb_dsp_command(devc, 0xd0);	/* Halt DMA */
	else
	{
		if (bits)
		{
			switch (devc->irq_mode)
			{
				case IMODE_INPUT:
					sb16_audio_start_input(dev,
							devc->trg_buf,
							devc->trg_bytes,
							devc->trg_intrflag);
					break;

				case IMODE_OUTPUT:
					sb16_audio_output_block(dev,
							devc->trg_buf,
							devc->trg_bytes,
							devc->trg_intrflag);
					break;
			}
		}
		if (bits_16)
		{
			switch (devc->irq_mode_16)
			{
				case IMODE_INPUT:
					sb16_audio_start_input(dev,
							devc->trg_buf_16,
							devc->trg_bytes_16,
							devc->trg_intrflag_16);
					break;

				case IMODE_OUTPUT:
					sb16_audio_output_block(dev,
							devc->trg_buf_16,
							devc->trg_bytes_16,
							devc->trg_intrflag_16);
					break;
			}
		}
	}

	devc->trigger_bits = bits | bits_16;
}

static unsigned char lbuf8[2048];
static signed short *lbuf16 = (signed short *)lbuf8;
#define LBUFCOPYSIZE 1024
static void
sb16_copy_from_user(int dev,
		char *localbuf, int localoffs,
		const char __user *userbuf, int useroffs,
		int max_in, int max_out,
		int *used, int *returned,
		int len)
{
	sb_devc       *devc = audio_devs[dev]->devc;
	int           i, c, p, locallen;
	unsigned char *buf8;
	signed short  *buf16;

	/* if not duplex no conversion */
	if (!devc->fullduplex)
	{
		if (copy_from_user(localbuf + localoffs,
				   userbuf + useroffs, len))
			return;
		*used = len;
		*returned = len;
	}
	else if (devc->bits == AFMT_S16_LE)
	{
		/* 16 -> 8 */
		/* max_in >> 1, max number of samples in ( 16 bits ) */
		/* max_out, max number of samples out ( 8 bits ) */
		/* len, number of samples that will be taken ( 16 bits )*/
		/* c, count of samples remaining in buffer ( 16 bits )*/
		/* p, count of samples already processed ( 16 bits )*/
		len = ( (max_in >> 1) > max_out) ? max_out : (max_in >> 1);
		c = len;
		p = 0;
		buf8 = (unsigned char *)(localbuf + localoffs);
		while (c)
		{
			locallen = (c >= LBUFCOPYSIZE ? LBUFCOPYSIZE : c);
			/* << 1 in order to get 16 bit samples */
			if (copy_from_user(lbuf16,
					   userbuf + useroffs + (p << 1),
					   locallen << 1))
				return;
			for (i = 0; i < locallen; i++)
			{
				buf8[p+i] = ~((lbuf16[i] >> 8) & 0xff) ^ 0x80;
			}
			c -= locallen; p += locallen;
		}
		/* used = ( samples * 16 bits size ) */
		*used =  max_in  > ( max_out << 1) ? (max_out << 1) : max_in;
		/* returned = ( samples * 8 bits size ) */
		*returned = len;
	}
	else
	{
		/* 8 -> 16 */
		/* max_in, max number of samples in ( 8 bits ) */
		/* max_out >> 1, max number of samples out ( 16 bits ) */
		/* len, number of samples that will be taken ( 8 bits )*/
		/* c, count of samples remaining in buffer ( 8 bits )*/
		/* p, count of samples already processed ( 8 bits )*/
		len = max_in > (max_out >> 1) ? (max_out >> 1) : max_in;
		c = len;
		p = 0;
		buf16 = (signed short *)(localbuf + localoffs);
		while (c)
		{
			locallen = (c >= LBUFCOPYSIZE ? LBUFCOPYSIZE : c);
			if (copy_from_user(lbuf8,
					   userbuf+useroffs + p,
					   locallen))
				return;
			for (i = 0; i < locallen; i++)
			{
				buf16[p+i] = (~lbuf8[i] ^ 0x80) << 8;
			}
	      		c -= locallen; p += locallen;
		}
		/* used = ( samples * 8 bits size ) */
		*used = len;
		/* returned = ( samples * 16 bits size ) */
		*returned = len << 1;
	}
}

static void
sb16_audio_mmap(int dev)
{
	sb_devc       *devc = audio_devs[dev]->devc;
	devc->fullduplex = 0;
}

static struct audio_driver sb1_audio_driver =	/* SB1.x */
{
	.owner			= THIS_MODULE,
	.open			= sb_audio_open,
	.close			= sb_audio_close,
	.output_block		= sb_set_output_parms,
	.start_input		= sb_set_input_parms,
	.prepare_for_input	= sb1_audio_prepare_for_input,
	.prepare_for_output	= sb1_audio_prepare_for_output,
	.halt_io		= sb1_audio_halt_xfer,
	.trigger		= sb1_audio_trigger,
	.set_speed		= sb1_audio_set_speed,
	.set_bits		= sb1_audio_set_bits,
	.set_channels		= sb1_audio_set_channels
};

static struct audio_driver sb20_audio_driver =	/* SB2.0 */
{
	.owner			= THIS_MODULE,
	.open			= sb_audio_open,
	.close			= sb_audio_close,
	.output_block		= sb_set_output_parms,
	.start_input		= sb_set_input_parms,
	.prepare_for_input	= sb1_audio_prepare_for_input,
	.prepare_for_output	= sb1_audio_prepare_for_output,
	.halt_io		= sb1_audio_halt_xfer,
	.trigger		= sb20_audio_trigger,
	.set_speed		= sb1_audio_set_speed,
	.set_bits		= sb1_audio_set_bits,
	.set_channels		= sb1_audio_set_channels
};

static struct audio_driver sb201_audio_driver =		/* SB2.01 */
{
	.owner			= THIS_MODULE,
	.open			= sb_audio_open,
	.close			= sb_audio_close,
	.output_block		= sb_set_output_parms,
	.start_input		= sb_set_input_parms,
	.prepare_for_input	= sb1_audio_prepare_for_input,
	.prepare_for_output	= sb1_audio_prepare_for_output,
	.halt_io		= sb1_audio_halt_xfer,
	.trigger		= sb20_audio_trigger,
	.set_speed		= sb201_audio_set_speed,
	.set_bits		= sb1_audio_set_bits,
	.set_channels		= sb1_audio_set_channels
};

static struct audio_driver sbpro_audio_driver =		/* SB Pro */
{
	.owner			= THIS_MODULE,
	.open			= sb_audio_open,
	.close			= sb_audio_close,
	.output_block		= sb_set_output_parms,
	.start_input		= sb_set_input_parms,
	.prepare_for_input	= sbpro_audio_prepare_for_input,
	.prepare_for_output	= sbpro_audio_prepare_for_output,
	.halt_io		= sb1_audio_halt_xfer,
	.trigger		= sb20_audio_trigger,
	.set_speed		= sbpro_audio_set_speed,
	.set_bits		= sb1_audio_set_bits,
	.set_channels		= sbpro_audio_set_channels
};

static struct audio_driver jazz16_audio_driver =	/* Jazz16 and SM Wave */
{
	.owner			= THIS_MODULE,
	.open			= sb_audio_open,
	.close			= sb_audio_close,
	.output_block		= sb_set_output_parms,
	.start_input		= sb_set_input_parms,
	.prepare_for_input	= sbpro_audio_prepare_for_input,
	.prepare_for_output	= sbpro_audio_prepare_for_output,
	.halt_io		= sb1_audio_halt_xfer,
	.trigger		= sb20_audio_trigger,
	.set_speed		= jazz16_audio_set_speed,
	.set_bits		= sb16_audio_set_bits,
	.set_channels		= sbpro_audio_set_channels
};

static struct audio_driver sb16_audio_driver =	/* SB16 */
{
	.owner			= THIS_MODULE,
	.open			= sb_audio_open,
	.close			= sb_audio_close,
	.output_block		= sb_set_output_parms,
	.start_input		= sb_set_input_parms,
	.prepare_for_input	= sb16_audio_prepare_for_input,
	.prepare_for_output	= sb16_audio_prepare_for_output,
	.halt_io		= sb1_audio_halt_xfer,
	.copy_user		= sb16_copy_from_user,
	.trigger		= sb16_audio_trigger,
	.set_speed		= sb16_audio_set_speed,
	.set_bits		= sb16_audio_set_bits,
	.set_channels		= sbpro_audio_set_channels,
	.mmap			= sb16_audio_mmap
};

void sb_audio_init(sb_devc * devc, char *name, struct module *owner)
{
	int audio_flags = 0;
	int format_mask = AFMT_U8;

	struct audio_driver *driver = &sb1_audio_driver;

	switch (devc->model)
	{
		case MDL_SB1:	/* SB1.0 or SB 1.5 */
			DDB(printk("Will use standard SB1.x driver\n"));
			audio_flags = DMA_HARDSTOP;
			break;

		case MDL_SB2:
			DDB(printk("Will use SB2.0 driver\n"));
			audio_flags = DMA_AUTOMODE;
			driver = &sb20_audio_driver;
			break;

		case MDL_SB201:
			DDB(printk("Will use SB2.01 (high speed) driver\n"));
			audio_flags = DMA_AUTOMODE;
			driver = &sb201_audio_driver;
			break;

		case MDL_JAZZ:
		case MDL_SMW:
			DDB(printk("Will use Jazz16 driver\n"));
			audio_flags = DMA_AUTOMODE;
			format_mask |= AFMT_S16_LE;
			driver = &jazz16_audio_driver;
			break;

		case MDL_ESS:
			DDB(printk("Will use ESS ES688/1688 driver\n"));
			driver = ess_audio_init (devc, &audio_flags, &format_mask);
			break;

		case MDL_SB16:
			DDB(printk("Will use SB16 driver\n"));
			audio_flags = DMA_AUTOMODE;
			format_mask |= AFMT_S16_LE;
			if (devc->dma8 != devc->dma16 && devc->dma16 != -1)
			{
				audio_flags |= DMA_DUPLEX;
				devc->duplex = 1;
			}
			driver = &sb16_audio_driver;
			break;

		default:
			DDB(printk("Will use SB Pro driver\n"));
			audio_flags = DMA_AUTOMODE;
			driver = &sbpro_audio_driver;
	}

	if (owner)
			driver->owner = owner;
	
	if ((devc->dev = sound_install_audiodrv(AUDIO_DRIVER_VERSION,
				name,driver, sizeof(struct audio_driver),
				audio_flags, format_mask, devc,
				devc->dma8,
				devc->duplex ? devc->dma16 : devc->dma8)) < 0)
	{
		  printk(KERN_ERR "Sound Blaster:  unable to install audio.\n");
		  return;
	}
	audio_devs[devc->dev]->mixer_dev = devc->my_mixerdev;
	audio_devs[devc->dev]->min_fragment = 5;
}
