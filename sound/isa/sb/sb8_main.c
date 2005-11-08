/*
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *                   Uros Bizjak <uros@kss-loka.si>
 *
 *  Routines for control of 8-bit SoundBlaster cards and clones
 *  Please note: I don't have access to old SB8 soundcards.
 *
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 * --
 *
 * Thu Apr 29 20:36:17 BST 1999 George David Morrison <gdm@gedamo.demon.co.uk>
 *   DSP can't respond to commands whilst in "high speed" mode. Caused 
 *   glitching during playback. Fixed.
 *
 * Wed Jul 12 22:02:55 CEST 2000 Uros Bizjak <uros@kss-loka.si>
 *   Cleaned up and rewrote lowlevel routines.
 */

#include <sound/driver.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <linux/init.h>
#include <linux/time.h>
#include <sound/core.h>
#include <sound/sb.h>

MODULE_AUTHOR("Jaroslav Kysela <perex@suse.cz>, Uros Bizjak <uros@kss-loka.si>");
MODULE_DESCRIPTION("Routines for control of 8-bit SoundBlaster cards and clones");
MODULE_LICENSE("GPL");

#define SB8_CLOCK	1000000
#define SB8_DEN(v)	((SB8_CLOCK + (v) / 2) / (v))
#define SB8_RATE(v)	(SB8_CLOCK / SB8_DEN(v))

static ratnum_t clock = {
	.num = SB8_CLOCK,
	.den_min = 1,
	.den_max = 256,
	.den_step = 1,
};

static snd_pcm_hw_constraint_ratnums_t hw_constraints_clock = {
	.nrats = 1,
	.rats = &clock,
};

static ratnum_t stereo_clocks[] = {
	{
		.num = SB8_CLOCK,
		.den_min = SB8_DEN(22050),
		.den_max = SB8_DEN(22050),
		.den_step = 1,
	},
	{
		.num = SB8_CLOCK,
		.den_min = SB8_DEN(11025),
		.den_max = SB8_DEN(11025),
		.den_step = 1,
	}
};

static int snd_sb8_hw_constraint_rate_channels(snd_pcm_hw_params_t *params,
					       snd_pcm_hw_rule_t *rule)
{
	snd_interval_t *c = hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	if (c->min > 1) {
	  	unsigned int num = 0, den = 0;
		int err = snd_interval_ratnum(hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE),
					  2, stereo_clocks, &num, &den);
		if (err >= 0 && den) {
			params->rate_num = num;
			params->rate_den = den;
		}
		return err;
	}
	return 0;
}

static int snd_sb8_hw_constraint_channels_rate(snd_pcm_hw_params_t *params,
					       snd_pcm_hw_rule_t *rule)
{
	snd_interval_t *r = hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);
	if (r->min > SB8_RATE(22050) || r->max <= SB8_RATE(11025)) {
		snd_interval_t t = { .min = 1, .max = 1 };
		return snd_interval_refine(hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS), &t);
	}
	return 0;
}

static int snd_sb8_playback_prepare(snd_pcm_substream_t * substream)
{
	unsigned long flags;
	sb_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	unsigned int mixreg, rate, size, count;

	rate = runtime->rate;
	switch (chip->hardware) {
	case SB_HW_PRO:
		if (runtime->channels > 1) {
			snd_assert(rate == SB8_RATE(11025) || rate == SB8_RATE(22050), return -EINVAL);
			chip->playback_format = SB_DSP_HI_OUTPUT_AUTO;
			break;
		}
		/* fallthru */
	case SB_HW_201:
		if (rate > 23000) {
			chip->playback_format = SB_DSP_HI_OUTPUT_AUTO;
			break;
		}
		/* fallthru */
	case SB_HW_20:
		chip->playback_format = SB_DSP_LO_OUTPUT_AUTO;
		break;
	case SB_HW_10:
		chip->playback_format = SB_DSP_OUTPUT;
		break;
	default:
		return -EINVAL;
	}
	size = chip->p_dma_size = snd_pcm_lib_buffer_bytes(substream);
	count = chip->p_period_size = snd_pcm_lib_period_bytes(substream);
	spin_lock_irqsave(&chip->reg_lock, flags);
	snd_sbdsp_command(chip, SB_DSP_SPEAKER_ON);
	if (runtime->channels > 1) {
		/* set playback stereo mode */
		spin_lock(&chip->mixer_lock);
		mixreg = snd_sbmixer_read(chip, SB_DSP_STEREO_SW);
		snd_sbmixer_write(chip, SB_DSP_STEREO_SW, mixreg | 0x02);
		spin_unlock(&chip->mixer_lock);

		/* Soundblaster hardware programming reference guide, 3-23 */
		snd_sbdsp_command(chip, SB_DSP_DMA8_EXIT);
		runtime->dma_area[0] = 0x80;
		snd_dma_program(chip->dma8, runtime->dma_addr, 1, DMA_MODE_WRITE);
		/* force interrupt */
		chip->mode = SB_MODE_HALT;
		snd_sbdsp_command(chip, SB_DSP_OUTPUT);
		snd_sbdsp_command(chip, 0);
		snd_sbdsp_command(chip, 0);
	}
	snd_sbdsp_command(chip, SB_DSP_SAMPLE_RATE);
	if (runtime->channels > 1) {
		snd_sbdsp_command(chip, 256 - runtime->rate_den / 2);
		spin_lock(&chip->mixer_lock);
		/* save output filter status and turn it off */
		mixreg = snd_sbmixer_read(chip, SB_DSP_PLAYBACK_FILT);
		snd_sbmixer_write(chip, SB_DSP_PLAYBACK_FILT, mixreg | 0x20);
		spin_unlock(&chip->mixer_lock);
		/* just use force_mode16 for temporary storate... */
		chip->force_mode16 = mixreg;
	} else {
		snd_sbdsp_command(chip, 256 - runtime->rate_den);
	}
	if (chip->playback_format != SB_DSP_OUTPUT) {
		count--;
		snd_sbdsp_command(chip, SB_DSP_BLOCK_SIZE);
		snd_sbdsp_command(chip, count & 0xff);
		snd_sbdsp_command(chip, count >> 8);
	}
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	snd_dma_program(chip->dma8, runtime->dma_addr,
			size, DMA_MODE_WRITE | DMA_AUTOINIT);
	return 0;
}

static int snd_sb8_playback_trigger(snd_pcm_substream_t * substream,
				    int cmd)
{
	unsigned long flags;
	sb_t *chip = snd_pcm_substream_chip(substream);
	unsigned int count;

	spin_lock_irqsave(&chip->reg_lock, flags);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		snd_sbdsp_command(chip, chip->playback_format);
		if (chip->playback_format == SB_DSP_OUTPUT) {
			count = chip->p_period_size - 1;
			snd_sbdsp_command(chip, count & 0xff);
			snd_sbdsp_command(chip, count >> 8);
		}
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		if (chip->playback_format == SB_DSP_HI_OUTPUT_AUTO) {
			snd_pcm_runtime_t *runtime = substream->runtime;
			snd_sbdsp_reset(chip);
			if (runtime->channels > 1) {
				spin_lock(&chip->mixer_lock);
				/* restore output filter and set hardware to mono mode */ 
				snd_sbmixer_write(chip, SB_DSP_STEREO_SW, chip->force_mode16 & ~0x02);
				spin_unlock(&chip->mixer_lock);
			}
		} else {
			snd_sbdsp_command(chip, SB_DSP_DMA8_OFF);
		}
		snd_sbdsp_command(chip, SB_DSP_SPEAKER_OFF);
	}
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	chip->mode = (cmd == SNDRV_PCM_TRIGGER_START) ? SB_MODE_PLAYBACK_8 : SB_MODE_HALT;
	return 0;
}

static int snd_sb8_hw_params(snd_pcm_substream_t * substream,
			     snd_pcm_hw_params_t * hw_params)
{
	return snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params));
}

static int snd_sb8_hw_free(snd_pcm_substream_t * substream)
{
	snd_pcm_lib_free_pages(substream);
	return 0;
}

static int snd_sb8_capture_prepare(snd_pcm_substream_t * substream)
{
	unsigned long flags;
	sb_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	unsigned int mixreg, rate, size, count;

	rate = runtime->rate;
	switch (chip->hardware) {
	case SB_HW_PRO:
		if (runtime->channels > 1) {
			snd_assert(rate == SB8_RATE(11025) || rate == SB8_RATE(22050), return -EINVAL);
			chip->capture_format = SB_DSP_HI_INPUT_AUTO;
			break;
		}
		chip->capture_format = (rate > 23000) ? SB_DSP_HI_INPUT_AUTO : SB_DSP_LO_INPUT_AUTO;
		break;
	case SB_HW_201:
		if (rate > 13000) {
			chip->capture_format = SB_DSP_HI_INPUT_AUTO;
			break;
		}
		/* fallthru */
	case SB_HW_20:
		chip->capture_format = SB_DSP_LO_INPUT_AUTO;
		break;
	case SB_HW_10:
		chip->capture_format = SB_DSP_INPUT;
		break;
	default:
		return -EINVAL;
	}
	size = chip->c_dma_size = snd_pcm_lib_buffer_bytes(substream);
	count = chip->c_period_size = snd_pcm_lib_period_bytes(substream);
	spin_lock_irqsave(&chip->reg_lock, flags);
	snd_sbdsp_command(chip, SB_DSP_SPEAKER_OFF);
	if (runtime->channels > 1)
		snd_sbdsp_command(chip, SB_DSP_STEREO_8BIT);
	snd_sbdsp_command(chip, SB_DSP_SAMPLE_RATE);
	if (runtime->channels > 1) {
		snd_sbdsp_command(chip, 256 - runtime->rate_den / 2);
		spin_lock(&chip->mixer_lock);
		/* save input filter status and turn it off */
		mixreg = snd_sbmixer_read(chip, SB_DSP_CAPTURE_FILT);
		snd_sbmixer_write(chip, SB_DSP_CAPTURE_FILT, mixreg | 0x20);
		spin_unlock(&chip->mixer_lock);
		/* just use force_mode16 for temporary storate... */
		chip->force_mode16 = mixreg;
	} else {
		snd_sbdsp_command(chip, 256 - runtime->rate_den);
	}
	if (chip->capture_format != SB_DSP_OUTPUT) {
		count--;
		snd_sbdsp_command(chip, SB_DSP_BLOCK_SIZE);
		snd_sbdsp_command(chip, count & 0xff);
		snd_sbdsp_command(chip, count >> 8);
	}
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	snd_dma_program(chip->dma8, runtime->dma_addr,
			size, DMA_MODE_READ | DMA_AUTOINIT);
	return 0;
}

static int snd_sb8_capture_trigger(snd_pcm_substream_t * substream,
				   int cmd)
{
	unsigned long flags;
	sb_t *chip = snd_pcm_substream_chip(substream);
	unsigned int count;

	spin_lock_irqsave(&chip->reg_lock, flags);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		snd_sbdsp_command(chip, chip->capture_format);
		if (chip->capture_format == SB_DSP_INPUT) {
			count = chip->c_period_size - 1;
			snd_sbdsp_command(chip, count & 0xff);
			snd_sbdsp_command(chip, count >> 8);
		}
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		if (chip->capture_format == SB_DSP_HI_INPUT_AUTO) {
			snd_pcm_runtime_t *runtime = substream->runtime;
			snd_sbdsp_reset(chip);
			if (runtime->channels > 1) {
				/* restore input filter status */
				spin_lock(&chip->mixer_lock);
				snd_sbmixer_write(chip, SB_DSP_CAPTURE_FILT, chip->force_mode16);
				spin_unlock(&chip->mixer_lock);
				/* set hardware to mono mode */
				snd_sbdsp_command(chip, SB_DSP_MONO_8BIT);
			}
		} else {
			snd_sbdsp_command(chip, SB_DSP_DMA8_OFF);
		}
		snd_sbdsp_command(chip, SB_DSP_SPEAKER_OFF);
	}
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	chip->mode = (cmd == SNDRV_PCM_TRIGGER_START) ? SB_MODE_CAPTURE_8 : SB_MODE_HALT;
	return 0;
}

irqreturn_t snd_sb8dsp_interrupt(sb_t *chip)
{
	snd_pcm_substream_t *substream;
	snd_pcm_runtime_t *runtime;

	snd_sb_ack_8bit(chip);
	switch (chip->mode) {
	case SB_MODE_PLAYBACK_8:	/* ok.. playback is active */
		substream = chip->playback_substream;
		runtime = substream->runtime;
		if (chip->playback_format == SB_DSP_OUTPUT)
		    	snd_sb8_playback_trigger(substream, SNDRV_PCM_TRIGGER_START);
		snd_pcm_period_elapsed(substream);
		break;
	case SB_MODE_CAPTURE_8:
		substream = chip->capture_substream;
		runtime = substream->runtime;
		if (chip->capture_format == SB_DSP_INPUT)
		    	snd_sb8_capture_trigger(substream, SNDRV_PCM_TRIGGER_START);
		snd_pcm_period_elapsed(substream);
		break;
	}
	return IRQ_HANDLED;
}

static snd_pcm_uframes_t snd_sb8_playback_pointer(snd_pcm_substream_t * substream)
{
	sb_t *chip = snd_pcm_substream_chip(substream);
	size_t ptr;

	if (chip->mode != SB_MODE_PLAYBACK_8)
		return 0;
	ptr = snd_dma_pointer(chip->dma8, chip->p_dma_size);
	return bytes_to_frames(substream->runtime, ptr);
}

static snd_pcm_uframes_t snd_sb8_capture_pointer(snd_pcm_substream_t * substream)
{
	sb_t *chip = snd_pcm_substream_chip(substream);
	size_t ptr;

	if (chip->mode != SB_MODE_CAPTURE_8)
		return 0;
	ptr = snd_dma_pointer(chip->dma8, chip->c_dma_size);
	return bytes_to_frames(substream->runtime, ptr);
}

/*

 */

static snd_pcm_hardware_t snd_sb8_playback =
{
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_MMAP_VALID),
	.formats =		 SNDRV_PCM_FMTBIT_U8,
	.rates =		(SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000 |
				 SNDRV_PCM_RATE_11025 | SNDRV_PCM_RATE_22050),
	.rate_min =		4000,
	.rate_max =		23000,
	.channels_min =		1,
	.channels_max =		1,
	.buffer_bytes_max =	65536,
	.period_bytes_min =	64,
	.period_bytes_max =	65536,
	.periods_min =		1,
	.periods_max =		1024,
	.fifo_size =		0,
};

static snd_pcm_hardware_t snd_sb8_capture =
{
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_MMAP_VALID),
	.formats =		SNDRV_PCM_FMTBIT_U8,
	.rates =		(SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000 |
				 SNDRV_PCM_RATE_11025),
	.rate_min =		4000,
	.rate_max =		13000,
	.channels_min =		1,
	.channels_max =		1,
	.buffer_bytes_max =	65536,
	.period_bytes_min =	64,
	.period_bytes_max =	65536,
	.periods_min =		1,
	.periods_max =		1024,
	.fifo_size =		0,
};

/*
 *
 */
 
static int snd_sb8_open(snd_pcm_substream_t *substream)
{
	sb_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	unsigned long flags;

	spin_lock_irqsave(&chip->open_lock, flags);
	if (chip->open) {
		spin_unlock_irqrestore(&chip->open_lock, flags);
		return -EAGAIN;
	}
	chip->open |= SB_OPEN_PCM;
	spin_unlock_irqrestore(&chip->open_lock, flags);
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		chip->playback_substream = substream;
		runtime->hw = snd_sb8_playback;
	} else {
		chip->capture_substream = substream;
		runtime->hw = snd_sb8_capture;
	}
	switch (chip->hardware) {
	case SB_HW_PRO:
		runtime->hw.rate_max = 44100;
		runtime->hw.channels_max = 2;
		snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				    snd_sb8_hw_constraint_rate_channels, NULL,
				    SNDRV_PCM_HW_PARAM_CHANNELS,
				    SNDRV_PCM_HW_PARAM_RATE, -1);
		snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
				     snd_sb8_hw_constraint_channels_rate, NULL,
				     SNDRV_PCM_HW_PARAM_RATE, -1);
		break;
	case SB_HW_201:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			runtime->hw.rate_max = 44100;
		} else {
			runtime->hw.rate_max = 15000;
		}
	default:
		break;
	}
	snd_pcm_hw_constraint_ratnums(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				      &hw_constraints_clock);
	return 0;	
}

static int snd_sb8_close(snd_pcm_substream_t *substream)
{
	unsigned long flags;
	sb_t *chip = snd_pcm_substream_chip(substream);

	chip->playback_substream = NULL;
	chip->capture_substream = NULL;
	spin_lock_irqsave(&chip->open_lock, flags);
	chip->open &= ~SB_OPEN_PCM;
	spin_unlock_irqrestore(&chip->open_lock, flags);
	return 0;
}

/*
 *  Initialization part
 */
 
static snd_pcm_ops_t snd_sb8_playback_ops = {
	.open =			snd_sb8_open,
	.close =		snd_sb8_close,
	.ioctl =		snd_pcm_lib_ioctl,
	.hw_params =		snd_sb8_hw_params,
	.hw_free =		snd_sb8_hw_free,
	.prepare =		snd_sb8_playback_prepare,
	.trigger =		snd_sb8_playback_trigger,
	.pointer =		snd_sb8_playback_pointer,
};

static snd_pcm_ops_t snd_sb8_capture_ops = {
	.open =			snd_sb8_open,
	.close =		snd_sb8_close,
	.ioctl =		snd_pcm_lib_ioctl,
	.hw_params =		snd_sb8_hw_params,
	.hw_free =		snd_sb8_hw_free,
	.prepare =		snd_sb8_capture_prepare,
	.trigger =		snd_sb8_capture_trigger,
	.pointer =		snd_sb8_capture_pointer,
};

static void snd_sb8dsp_pcm_free(snd_pcm_t *pcm)
{
	snd_pcm_lib_preallocate_free_for_all(pcm);
}

int snd_sb8dsp_pcm(sb_t *chip, int device, snd_pcm_t ** rpcm)
{
	snd_card_t *card = chip->card;
	snd_pcm_t *pcm;
	int err;

	if (rpcm)
		*rpcm = NULL;
	if ((err = snd_pcm_new(card, "SB8 DSP", device, 1, 1, &pcm)) < 0)
		return err;
	sprintf(pcm->name, "DSP v%i.%i", chip->version >> 8, chip->version & 0xff);
	pcm->info_flags = SNDRV_PCM_INFO_HALF_DUPLEX;
	pcm->private_data = chip;
	pcm->private_free = snd_sb8dsp_pcm_free;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_sb8_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_sb8_capture_ops);

	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
					      snd_dma_isa_data(),
					      64*1024, 64*1024);

	if (rpcm)
		*rpcm = pcm;
	return 0;
}

EXPORT_SYMBOL(snd_sb8dsp_pcm);
EXPORT_SYMBOL(snd_sb8dsp_interrupt);
  /* sb8_midi.c */
EXPORT_SYMBOL(snd_sb8dsp_midi_interrupt);
EXPORT_SYMBOL(snd_sb8dsp_midi);

/*
 *  INIT part
 */

static int __init alsa_sb8_init(void)
{
	return 0;
}

static void __exit alsa_sb8_exit(void)
{
}

module_init(alsa_sb8_init)
module_exit(alsa_sb8_exit)
