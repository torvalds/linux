// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 *  Routines for control of 16-bit SoundBlaster cards and clones
 *  Note: This is very ugly hardware which uses one 8-bit DMA channel and
 *        second 16-bit DMA channel. Unfortunately 8-bit DMA channel can't
 *        transfer 16-bit samples and 16-bit DMA channels can't transfer
 *        8-bit samples. This make full duplex more complicated than
 *        can be... People, don't buy these soundcards for full 16-bit
 *        duplex!!!
 *  Note: 16-bit wide is assigned to first direction which made request.
 *        With full duplex - playback is preferred with abstract layer.
 *
 *  Note: Some chip revisions have hardware bug. Changing capture
 *        channel from full-duplex 8bit DMA to 16bit DMA will block
 *        16bit DMA transfers from DSP chip (capture) until 8bit transfer
 *        to DSP chip (playback) starts. This bug can be avoided with
 *        "16bit DMA Allocation" setting set to Playback or Capture.
 */

#include <linux/io.h>
#include <asm/dma.h>
#include <linux/init.h>
#include <linux/time.h>
#include <linux/module.h>
#include <sound/core.h>
#include <sound/sb.h>
#include <sound/sb16_csp.h>
#include <sound/mpu401.h>
#include <sound/control.h>
#include <sound/info.h>

MODULE_AUTHOR("Jaroslav Kysela <perex@perex.cz>");
MODULE_DESCRIPTION("Routines for control of 16-bit SoundBlaster cards and clones");
MODULE_LICENSE("GPL");

#define runtime_format_bits(runtime) \
	((unsigned int)pcm_format_to_bits((runtime)->format))

#ifdef CONFIG_SND_SB16_CSP
static void snd_sb16_csp_playback_prepare(struct snd_sb *chip, struct snd_pcm_runtime *runtime)
{
	if (chip->hardware == SB_HW_16CSP) {
		struct snd_sb_csp *csp = chip->csp;

		if (csp->running & SNDRV_SB_CSP_ST_LOADED) {
			/* manually loaded codec */
			if ((csp->mode & SNDRV_SB_CSP_MODE_DSP_WRITE) &&
			    (runtime_format_bits(runtime) == csp->acc_format)) {
				/* Supported runtime PCM format for playback */
				if (csp->ops.csp_use(csp) == 0) {
					/* If CSP was successfully acquired */
					goto __start_CSP;
				}
			} else if ((csp->mode & SNDRV_SB_CSP_MODE_QSOUND) && (csp->q_enabled)) {
				/* QSound decoder is loaded and enabled */
				if (runtime_format_bits(runtime) & (SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_U8 |
							      SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_U16_LE)) {
					/* Only for simple PCM formats */
					if (csp->ops.csp_use(csp) == 0) {
						/* If CSP was successfully acquired */
						goto __start_CSP;
					}
				}
			}
		} else if (csp->ops.csp_use(csp) == 0) {
			/* Acquire CSP and try to autoload hardware codec */
			if (csp->ops.csp_autoload(csp, runtime->format, SNDRV_SB_CSP_MODE_DSP_WRITE)) {
				/* Unsupported format, release CSP */
				csp->ops.csp_unuse(csp);
			} else {
		      __start_CSP:
				/* Try to start CSP */
				if (csp->ops.csp_start(csp, (chip->mode & SB_MODE_PLAYBACK_16) ?
						       SNDRV_SB_CSP_SAMPLE_16BIT : SNDRV_SB_CSP_SAMPLE_8BIT,
						       (runtime->channels > 1) ?
						       SNDRV_SB_CSP_STEREO : SNDRV_SB_CSP_MONO)) {
					/* Failed, release CSP */
					csp->ops.csp_unuse(csp);
				} else {
					/* Success, CSP acquired and running */
					chip->open = SNDRV_SB_CSP_MODE_DSP_WRITE;
				}
			}
		}
	}
}

static void snd_sb16_csp_capture_prepare(struct snd_sb *chip, struct snd_pcm_runtime *runtime)
{
	if (chip->hardware == SB_HW_16CSP) {
		struct snd_sb_csp *csp = chip->csp;

		if (csp->running & SNDRV_SB_CSP_ST_LOADED) {
			/* manually loaded codec */
			if ((csp->mode & SNDRV_SB_CSP_MODE_DSP_READ) &&
			    (runtime_format_bits(runtime) == csp->acc_format)) {
				/* Supported runtime PCM format for capture */
				if (csp->ops.csp_use(csp) == 0) {
					/* If CSP was successfully acquired */
					goto __start_CSP;
				}
			}
		} else if (csp->ops.csp_use(csp) == 0) {
			/* Acquire CSP and try to autoload hardware codec */
			if (csp->ops.csp_autoload(csp, runtime->format, SNDRV_SB_CSP_MODE_DSP_READ)) {
				/* Unsupported format, release CSP */
				csp->ops.csp_unuse(csp);
			} else {
		      __start_CSP:
				/* Try to start CSP */
				if (csp->ops.csp_start(csp, (chip->mode & SB_MODE_CAPTURE_16) ?
						       SNDRV_SB_CSP_SAMPLE_16BIT : SNDRV_SB_CSP_SAMPLE_8BIT,
						       (runtime->channels > 1) ?
						       SNDRV_SB_CSP_STEREO : SNDRV_SB_CSP_MONO)) {
					/* Failed, release CSP */
					csp->ops.csp_unuse(csp);
				} else {
					/* Success, CSP acquired and running */
					chip->open = SNDRV_SB_CSP_MODE_DSP_READ;
				}
			}
		}
	}
}

static void snd_sb16_csp_update(struct snd_sb *chip)
{
	if (chip->hardware == SB_HW_16CSP) {
		struct snd_sb_csp *csp = chip->csp;

		if (csp->qpos_changed) {
			spin_lock(&chip->reg_lock);
			csp->ops.csp_qsound_transfer (csp);
			spin_unlock(&chip->reg_lock);
		}
	}
}

static void snd_sb16_csp_playback_open(struct snd_sb *chip, struct snd_pcm_runtime *runtime)
{
	/* CSP decoders (QSound excluded) support only 16bit transfers */
	if (chip->hardware == SB_HW_16CSP) {
		struct snd_sb_csp *csp = chip->csp;

		if (csp->running & SNDRV_SB_CSP_ST_LOADED) {
			/* manually loaded codec */
			if (csp->mode & SNDRV_SB_CSP_MODE_DSP_WRITE) {
				runtime->hw.formats |= csp->acc_format;
			}
		} else {
			/* autoloaded codecs */
			runtime->hw.formats |= SNDRV_PCM_FMTBIT_MU_LAW | SNDRV_PCM_FMTBIT_A_LAW |
					       SNDRV_PCM_FMTBIT_IMA_ADPCM;
		}
	}
}

static void snd_sb16_csp_playback_close(struct snd_sb *chip)
{
	if ((chip->hardware == SB_HW_16CSP) && (chip->open == SNDRV_SB_CSP_MODE_DSP_WRITE)) {
		struct snd_sb_csp *csp = chip->csp;

		if (csp->ops.csp_stop(csp) == 0) {
			csp->ops.csp_unuse(csp);
			chip->open = 0;
		}
	}
}

static void snd_sb16_csp_capture_open(struct snd_sb *chip, struct snd_pcm_runtime *runtime)
{
	/* CSP coders support only 16bit transfers */
	if (chip->hardware == SB_HW_16CSP) {
		struct snd_sb_csp *csp = chip->csp;

		if (csp->running & SNDRV_SB_CSP_ST_LOADED) {
			/* manually loaded codec */
			if (csp->mode & SNDRV_SB_CSP_MODE_DSP_READ) {
				runtime->hw.formats |= csp->acc_format;
			}
		} else {
			/* autoloaded codecs */
			runtime->hw.formats |= SNDRV_PCM_FMTBIT_MU_LAW | SNDRV_PCM_FMTBIT_A_LAW |
					       SNDRV_PCM_FMTBIT_IMA_ADPCM;
		}
	}
}

static void snd_sb16_csp_capture_close(struct snd_sb *chip)
{
	if ((chip->hardware == SB_HW_16CSP) && (chip->open == SNDRV_SB_CSP_MODE_DSP_READ)) {
		struct snd_sb_csp *csp = chip->csp;

		if (csp->ops.csp_stop(csp) == 0) {
			csp->ops.csp_unuse(csp);
			chip->open = 0;
		}
	}
}
#else
#define snd_sb16_csp_playback_prepare(chip, runtime)	/*nop*/
#define snd_sb16_csp_capture_prepare(chip, runtime)	/*nop*/
#define snd_sb16_csp_update(chip)			/*nop*/
#define snd_sb16_csp_playback_open(chip, runtime)	/*nop*/
#define snd_sb16_csp_playback_close(chip)		/*nop*/
#define snd_sb16_csp_capture_open(chip, runtime)	/*nop*/
#define snd_sb16_csp_capture_close(chip)      	 	/*nop*/
#endif


static void snd_sb16_setup_rate(struct snd_sb *chip,
				unsigned short rate,
				int channel)
{
	unsigned long flags;

	spin_lock_irqsave(&chip->reg_lock, flags);
	if (chip->mode & (channel == SNDRV_PCM_STREAM_PLAYBACK ? SB_MODE_PLAYBACK_16 : SB_MODE_CAPTURE_16))
		snd_sb_ack_16bit(chip);
	else
		snd_sb_ack_8bit(chip);
	if (!(chip->mode & SB_RATE_LOCK)) {
		chip->locked_rate = rate;
		snd_sbdsp_command(chip, SB_DSP_SAMPLE_RATE_IN);
		snd_sbdsp_command(chip, rate >> 8);
		snd_sbdsp_command(chip, rate & 0xff);
		snd_sbdsp_command(chip, SB_DSP_SAMPLE_RATE_OUT);
		snd_sbdsp_command(chip, rate >> 8);
		snd_sbdsp_command(chip, rate & 0xff);
	}
	spin_unlock_irqrestore(&chip->reg_lock, flags);
}

static int snd_sb16_hw_params(struct snd_pcm_substream *substream,
			      struct snd_pcm_hw_params *hw_params)
{
	return snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params));
}

static int snd_sb16_hw_free(struct snd_pcm_substream *substream)
{
	snd_pcm_lib_free_pages(substream);
	return 0;
}

static int snd_sb16_playback_prepare(struct snd_pcm_substream *substream)
{
	unsigned long flags;
	struct snd_sb *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned char format;
	unsigned int size, count, dma;

	snd_sb16_csp_playback_prepare(chip, runtime);
	if (snd_pcm_format_unsigned(runtime->format) > 0) {
		format = runtime->channels > 1 ? SB_DSP4_MODE_UNS_STEREO : SB_DSP4_MODE_UNS_MONO;
	} else {
		format = runtime->channels > 1 ? SB_DSP4_MODE_SIGN_STEREO : SB_DSP4_MODE_SIGN_MONO;
	}

	snd_sb16_setup_rate(chip, runtime->rate, SNDRV_PCM_STREAM_PLAYBACK);
	size = chip->p_dma_size = snd_pcm_lib_buffer_bytes(substream);
	dma = (chip->mode & SB_MODE_PLAYBACK_8) ? chip->dma8 : chip->dma16;
	snd_dma_program(dma, runtime->dma_addr, size, DMA_MODE_WRITE | DMA_AUTOINIT);

	count = snd_pcm_lib_period_bytes(substream);
	spin_lock_irqsave(&chip->reg_lock, flags);
	if (chip->mode & SB_MODE_PLAYBACK_16) {
		count >>= 1;
		count--;
		snd_sbdsp_command(chip, SB_DSP4_OUT16_AI);
		snd_sbdsp_command(chip, format);
		snd_sbdsp_command(chip, count & 0xff);
		snd_sbdsp_command(chip, count >> 8);
		snd_sbdsp_command(chip, SB_DSP_DMA16_OFF);
	} else {
		count--;
		snd_sbdsp_command(chip, SB_DSP4_OUT8_AI);
		snd_sbdsp_command(chip, format);
		snd_sbdsp_command(chip, count & 0xff);
		snd_sbdsp_command(chip, count >> 8);
		snd_sbdsp_command(chip, SB_DSP_DMA8_OFF);
	}
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return 0;
}

static int snd_sb16_playback_trigger(struct snd_pcm_substream *substream,
				     int cmd)
{
	struct snd_sb *chip = snd_pcm_substream_chip(substream);
	int result = 0;

	spin_lock(&chip->reg_lock);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		chip->mode |= SB_RATE_LOCK_PLAYBACK;
		snd_sbdsp_command(chip, chip->mode & SB_MODE_PLAYBACK_16 ? SB_DSP_DMA16_ON : SB_DSP_DMA8_ON);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		snd_sbdsp_command(chip, chip->mode & SB_MODE_PLAYBACK_16 ? SB_DSP_DMA16_OFF : SB_DSP_DMA8_OFF);
		/* next two lines are needed for some types of DSP4 (SB AWE 32 - 4.13) */
		if (chip->mode & SB_RATE_LOCK_CAPTURE)
			snd_sbdsp_command(chip, chip->mode & SB_MODE_CAPTURE_16 ? SB_DSP_DMA16_ON : SB_DSP_DMA8_ON);
		chip->mode &= ~SB_RATE_LOCK_PLAYBACK;
		break;
	default:
		result = -EINVAL;
	}
	spin_unlock(&chip->reg_lock);
	return result;
}

static int snd_sb16_capture_prepare(struct snd_pcm_substream *substream)
{
	unsigned long flags;
	struct snd_sb *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned char format;
	unsigned int size, count, dma;

	snd_sb16_csp_capture_prepare(chip, runtime);
	if (snd_pcm_format_unsigned(runtime->format) > 0) {
		format = runtime->channels > 1 ? SB_DSP4_MODE_UNS_STEREO : SB_DSP4_MODE_UNS_MONO;
	} else {
		format = runtime->channels > 1 ? SB_DSP4_MODE_SIGN_STEREO : SB_DSP4_MODE_SIGN_MONO;
	}
	snd_sb16_setup_rate(chip, runtime->rate, SNDRV_PCM_STREAM_CAPTURE);
	size = chip->c_dma_size = snd_pcm_lib_buffer_bytes(substream);
	dma = (chip->mode & SB_MODE_CAPTURE_8) ? chip->dma8 : chip->dma16;
	snd_dma_program(dma, runtime->dma_addr, size, DMA_MODE_READ | DMA_AUTOINIT);

	count = snd_pcm_lib_period_bytes(substream);
	spin_lock_irqsave(&chip->reg_lock, flags);
	if (chip->mode & SB_MODE_CAPTURE_16) {
		count >>= 1;
		count--;
		snd_sbdsp_command(chip, SB_DSP4_IN16_AI);
		snd_sbdsp_command(chip, format);
		snd_sbdsp_command(chip, count & 0xff);
		snd_sbdsp_command(chip, count >> 8);
		snd_sbdsp_command(chip, SB_DSP_DMA16_OFF);
	} else {
		count--;
		snd_sbdsp_command(chip, SB_DSP4_IN8_AI);
		snd_sbdsp_command(chip, format);
		snd_sbdsp_command(chip, count & 0xff);
		snd_sbdsp_command(chip, count >> 8);
		snd_sbdsp_command(chip, SB_DSP_DMA8_OFF);
	}
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return 0;
}

static int snd_sb16_capture_trigger(struct snd_pcm_substream *substream,
				    int cmd)
{
	struct snd_sb *chip = snd_pcm_substream_chip(substream);
	int result = 0;

	spin_lock(&chip->reg_lock);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		chip->mode |= SB_RATE_LOCK_CAPTURE;
		snd_sbdsp_command(chip, chip->mode & SB_MODE_CAPTURE_16 ? SB_DSP_DMA16_ON : SB_DSP_DMA8_ON);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		snd_sbdsp_command(chip, chip->mode & SB_MODE_CAPTURE_16 ? SB_DSP_DMA16_OFF : SB_DSP_DMA8_OFF);
		/* next two lines are needed for some types of DSP4 (SB AWE 32 - 4.13) */
		if (chip->mode & SB_RATE_LOCK_PLAYBACK)
			snd_sbdsp_command(chip, chip->mode & SB_MODE_PLAYBACK_16 ? SB_DSP_DMA16_ON : SB_DSP_DMA8_ON);
		chip->mode &= ~SB_RATE_LOCK_CAPTURE;
		break;
	default:
		result = -EINVAL;
	}
	spin_unlock(&chip->reg_lock);
	return result;
}

irqreturn_t snd_sb16dsp_interrupt(int irq, void *dev_id)
{
	struct snd_sb *chip = dev_id;
	unsigned char status;
	int ok;

	spin_lock(&chip->mixer_lock);
	status = snd_sbmixer_read(chip, SB_DSP4_IRQSTATUS);
	spin_unlock(&chip->mixer_lock);
	if ((status & SB_IRQTYPE_MPUIN) && chip->rmidi_callback)
		chip->rmidi_callback(irq, chip->rmidi->private_data);
	if (status & SB_IRQTYPE_8BIT) {
		ok = 0;
		if (chip->mode & SB_MODE_PLAYBACK_8) {
			snd_pcm_period_elapsed(chip->playback_substream);
			snd_sb16_csp_update(chip);
			ok++;
		}
		if (chip->mode & SB_MODE_CAPTURE_8) {
			snd_pcm_period_elapsed(chip->capture_substream);
			ok++;
		}
		spin_lock(&chip->reg_lock);
		if (!ok)
			snd_sbdsp_command(chip, SB_DSP_DMA8_OFF);
		snd_sb_ack_8bit(chip);
		spin_unlock(&chip->reg_lock);
	}
	if (status & SB_IRQTYPE_16BIT) {
		ok = 0;
		if (chip->mode & SB_MODE_PLAYBACK_16) {
			snd_pcm_period_elapsed(chip->playback_substream);
			snd_sb16_csp_update(chip);
			ok++;
		}
		if (chip->mode & SB_MODE_CAPTURE_16) {
			snd_pcm_period_elapsed(chip->capture_substream);
			ok++;
		}
		spin_lock(&chip->reg_lock);
		if (!ok)
			snd_sbdsp_command(chip, SB_DSP_DMA16_OFF);
		snd_sb_ack_16bit(chip);
		spin_unlock(&chip->reg_lock);
	}
	return IRQ_HANDLED;
}

/*

 */

static snd_pcm_uframes_t snd_sb16_playback_pointer(struct snd_pcm_substream *substream)
{
	struct snd_sb *chip = snd_pcm_substream_chip(substream);
	unsigned int dma;
	size_t ptr;

	dma = (chip->mode & SB_MODE_PLAYBACK_8) ? chip->dma8 : chip->dma16;
	ptr = snd_dma_pointer(dma, chip->p_dma_size);
	return bytes_to_frames(substream->runtime, ptr);
}

static snd_pcm_uframes_t snd_sb16_capture_pointer(struct snd_pcm_substream *substream)
{
	struct snd_sb *chip = snd_pcm_substream_chip(substream);
	unsigned int dma;
	size_t ptr;

	dma = (chip->mode & SB_MODE_CAPTURE_8) ? chip->dma8 : chip->dma16;
	ptr = snd_dma_pointer(dma, chip->c_dma_size);
	return bytes_to_frames(substream->runtime, ptr);
}

/*

 */

static const struct snd_pcm_hardware snd_sb16_playback =
{
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_MMAP_VALID),
	.formats =		0,
	.rates =		SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_44100,
	.rate_min =		4000,
	.rate_max =		44100,
	.channels_min =		1,
	.channels_max =		2,
	.buffer_bytes_max =	(128*1024),
	.period_bytes_min =	64,
	.period_bytes_max =	(128*1024),
	.periods_min =		1,
	.periods_max =		1024,
	.fifo_size =		0,
};

static const struct snd_pcm_hardware snd_sb16_capture =
{
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_MMAP_VALID),
	.formats =		0,
	.rates =		SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_44100,
	.rate_min =		4000,
	.rate_max =		44100,
	.channels_min =		1,
	.channels_max =		2,
	.buffer_bytes_max =	(128*1024),
	.period_bytes_min =	64,
	.period_bytes_max =	(128*1024),
	.periods_min =		1,
	.periods_max =		1024,
	.fifo_size =		0,
};

/*
 *  open/close
 */

static int snd_sb16_playback_open(struct snd_pcm_substream *substream)
{
	unsigned long flags;
	struct snd_sb *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;

	spin_lock_irqsave(&chip->open_lock, flags);
	if (chip->mode & SB_MODE_PLAYBACK) {
		spin_unlock_irqrestore(&chip->open_lock, flags);
		return -EAGAIN;
	}
	runtime->hw = snd_sb16_playback;

	/* skip if 16 bit DMA was reserved for capture */
	if (chip->force_mode16 & SB_MODE_CAPTURE_16)
		goto __skip_16bit;

	if (chip->dma16 >= 0 && !(chip->mode & SB_MODE_CAPTURE_16)) {
		chip->mode |= SB_MODE_PLAYBACK_16;
		runtime->hw.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_U16_LE;
		/* Vibra16X hack */
		if (chip->dma16 <= 3) {
			runtime->hw.buffer_bytes_max =
			runtime->hw.period_bytes_max = 64 * 1024;
		} else {
			snd_sb16_csp_playback_open(chip, runtime);
		}
		goto __open_ok;
	}

      __skip_16bit:
	if (chip->dma8 >= 0 && !(chip->mode & SB_MODE_CAPTURE_8)) {
		chip->mode |= SB_MODE_PLAYBACK_8;
		/* DSP v 4.xx can transfer 16bit data through 8bit DMA channel, SBHWPG 2-7 */
		if (chip->dma16 < 0) {
			runtime->hw.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_U16_LE;
			chip->mode |= SB_MODE_PLAYBACK_16;
		} else {
			runtime->hw.formats = SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S8;
		}
		runtime->hw.buffer_bytes_max =
		runtime->hw.period_bytes_max = 64 * 1024;
		goto __open_ok;
	}
	spin_unlock_irqrestore(&chip->open_lock, flags);
	return -EAGAIN;

      __open_ok:
	if (chip->hardware == SB_HW_ALS100)
		runtime->hw.rate_max = 48000;
	if (chip->hardware == SB_HW_CS5530) {
		runtime->hw.buffer_bytes_max = 32 * 1024;
		runtime->hw.periods_min = 2;
		runtime->hw.rate_min = 44100;
	}
	if (chip->mode & SB_RATE_LOCK)
		runtime->hw.rate_min = runtime->hw.rate_max = chip->locked_rate;
	chip->playback_substream = substream;
	spin_unlock_irqrestore(&chip->open_lock, flags);
	return 0;
}

static int snd_sb16_playback_close(struct snd_pcm_substream *substream)
{
	unsigned long flags;
	struct snd_sb *chip = snd_pcm_substream_chip(substream);

	snd_sb16_csp_playback_close(chip);
	spin_lock_irqsave(&chip->open_lock, flags);
	chip->playback_substream = NULL;
	chip->mode &= ~SB_MODE_PLAYBACK;
	spin_unlock_irqrestore(&chip->open_lock, flags);
	return 0;
}

static int snd_sb16_capture_open(struct snd_pcm_substream *substream)
{
	unsigned long flags;
	struct snd_sb *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;

	spin_lock_irqsave(&chip->open_lock, flags);
	if (chip->mode & SB_MODE_CAPTURE) {
		spin_unlock_irqrestore(&chip->open_lock, flags);
		return -EAGAIN;
	}
	runtime->hw = snd_sb16_capture;

	/* skip if 16 bit DMA was reserved for playback */
	if (chip->force_mode16 & SB_MODE_PLAYBACK_16)
		goto __skip_16bit;

	if (chip->dma16 >= 0 && !(chip->mode & SB_MODE_PLAYBACK_16)) {
		chip->mode |= SB_MODE_CAPTURE_16;
		runtime->hw.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_U16_LE;
		/* Vibra16X hack */
		if (chip->dma16 <= 3) {
			runtime->hw.buffer_bytes_max =
			runtime->hw.period_bytes_max = 64 * 1024;
		} else {
			snd_sb16_csp_capture_open(chip, runtime);
		}
		goto __open_ok;
	}

      __skip_16bit:
	if (chip->dma8 >= 0 && !(chip->mode & SB_MODE_PLAYBACK_8)) {
		chip->mode |= SB_MODE_CAPTURE_8;
		/* DSP v 4.xx can transfer 16bit data through 8bit DMA channel, SBHWPG 2-7 */
		if (chip->dma16 < 0) {
			runtime->hw.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_U16_LE;
			chip->mode |= SB_MODE_CAPTURE_16;
		} else {
			runtime->hw.formats = SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S8;
		}
		runtime->hw.buffer_bytes_max =
		runtime->hw.period_bytes_max = 64 * 1024;
		goto __open_ok;
	}
	spin_unlock_irqrestore(&chip->open_lock, flags);
	return -EAGAIN;

      __open_ok:
	if (chip->hardware == SB_HW_ALS100)
		runtime->hw.rate_max = 48000;
	if (chip->hardware == SB_HW_CS5530) {
		runtime->hw.buffer_bytes_max = 32 * 1024;
		runtime->hw.periods_min = 2;
		runtime->hw.rate_min = 44100;
	}
	if (chip->mode & SB_RATE_LOCK)
		runtime->hw.rate_min = runtime->hw.rate_max = chip->locked_rate;
	chip->capture_substream = substream;
	spin_unlock_irqrestore(&chip->open_lock, flags);
	return 0;
}

static int snd_sb16_capture_close(struct snd_pcm_substream *substream)
{
	unsigned long flags;
	struct snd_sb *chip = snd_pcm_substream_chip(substream);

	snd_sb16_csp_capture_close(chip);
	spin_lock_irqsave(&chip->open_lock, flags);
	chip->capture_substream = NULL;
	chip->mode &= ~SB_MODE_CAPTURE;
	spin_unlock_irqrestore(&chip->open_lock, flags);
	return 0;
}

/*
 *  DMA control interface
 */

static int snd_sb16_set_dma_mode(struct snd_sb *chip, int what)
{
	if (chip->dma8 < 0 || chip->dma16 < 0) {
		if (snd_BUG_ON(what))
			return -EINVAL;
		return 0;
	}
	if (what == 0) {
		chip->force_mode16 = 0;
	} else if (what == 1) {
		chip->force_mode16 = SB_MODE_PLAYBACK_16;
	} else if (what == 2) {
		chip->force_mode16 = SB_MODE_CAPTURE_16;
	} else {
		return -EINVAL;
	}
	return 0;
}

static int snd_sb16_get_dma_mode(struct snd_sb *chip)
{
	if (chip->dma8 < 0 || chip->dma16 < 0)
		return 0;
	switch (chip->force_mode16) {
	case SB_MODE_PLAYBACK_16:
		return 1;
	case SB_MODE_CAPTURE_16:
		return 2;
	default:
		return 0;
	}
}

static int snd_sb16_dma_control_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static const char * const texts[3] = {
		"Auto", "Playback", "Capture"
	};

	return snd_ctl_enum_info(uinfo, 1, 3, texts);
}

static int snd_sb16_dma_control_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_sb *chip = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	
	spin_lock_irqsave(&chip->reg_lock, flags);
	ucontrol->value.enumerated.item[0] = snd_sb16_get_dma_mode(chip);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return 0;
}

static int snd_sb16_dma_control_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_sb *chip = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	unsigned char nval, oval;
	int change;
	
	if ((nval = ucontrol->value.enumerated.item[0]) > 2)
		return -EINVAL;
	spin_lock_irqsave(&chip->reg_lock, flags);
	oval = snd_sb16_get_dma_mode(chip);
	change = nval != oval;
	snd_sb16_set_dma_mode(chip, nval);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return change;
}

static const struct snd_kcontrol_new snd_sb16_dma_control = {
	.iface = SNDRV_CTL_ELEM_IFACE_CARD,
	.name = "16-bit DMA Allocation",
	.info = snd_sb16_dma_control_info,
	.get = snd_sb16_dma_control_get,
	.put = snd_sb16_dma_control_put
};

/*
 *  Initialization part
 */
 
int snd_sb16dsp_configure(struct snd_sb * chip)
{
	unsigned long flags;
	unsigned char irqreg = 0, dmareg = 0, mpureg;
	unsigned char realirq, realdma, realmpureg;
	/* note: mpu register should be present only on SB16 Vibra soundcards */

	// printk(KERN_DEBUG "codec->irq=%i, codec->dma8=%i, codec->dma16=%i\n", chip->irq, chip->dma8, chip->dma16);
	spin_lock_irqsave(&chip->mixer_lock, flags);
	mpureg = snd_sbmixer_read(chip, SB_DSP4_MPUSETUP) & ~0x06;
	spin_unlock_irqrestore(&chip->mixer_lock, flags);
	switch (chip->irq) {
	case 2:
	case 9:
		irqreg |= SB_IRQSETUP_IRQ9;
		break;
	case 5:
		irqreg |= SB_IRQSETUP_IRQ5;
		break;
	case 7:
		irqreg |= SB_IRQSETUP_IRQ7;
		break;
	case 10:
		irqreg |= SB_IRQSETUP_IRQ10;
		break;
	default:
		return -EINVAL;
	}
	if (chip->dma8 >= 0) {
		switch (chip->dma8) {
		case 0:
			dmareg |= SB_DMASETUP_DMA0;
			break;
		case 1:
			dmareg |= SB_DMASETUP_DMA1;
			break;
		case 3:
			dmareg |= SB_DMASETUP_DMA3;
			break;
		default:
			return -EINVAL;
		}
	}
	if (chip->dma16 >= 0 && chip->dma16 != chip->dma8) {
		switch (chip->dma16) {
		case 5:
			dmareg |= SB_DMASETUP_DMA5;
			break;
		case 6:
			dmareg |= SB_DMASETUP_DMA6;
			break;
		case 7:
			dmareg |= SB_DMASETUP_DMA7;
			break;
		default:
			return -EINVAL;
		}
	}
	switch (chip->mpu_port) {
	case 0x300:
		mpureg |= 0x04;
		break;
	case 0x330:
		mpureg |= 0x00;
		break;
	default:
		mpureg |= 0x02;	/* disable MPU */
	}
	spin_lock_irqsave(&chip->mixer_lock, flags);

	snd_sbmixer_write(chip, SB_DSP4_IRQSETUP, irqreg);
	realirq = snd_sbmixer_read(chip, SB_DSP4_IRQSETUP);

	snd_sbmixer_write(chip, SB_DSP4_DMASETUP, dmareg);
	realdma = snd_sbmixer_read(chip, SB_DSP4_DMASETUP);

	snd_sbmixer_write(chip, SB_DSP4_MPUSETUP, mpureg);
	realmpureg = snd_sbmixer_read(chip, SB_DSP4_MPUSETUP);

	spin_unlock_irqrestore(&chip->mixer_lock, flags);
	if ((~realirq) & irqreg || (~realdma) & dmareg) {
		snd_printk(KERN_ERR "SB16 [0x%lx]: unable to set DMA & IRQ (PnP device?)\n", chip->port);
		snd_printk(KERN_ERR "SB16 [0x%lx]: wanted: irqreg=0x%x, dmareg=0x%x, mpureg = 0x%x\n", chip->port, realirq, realdma, realmpureg);
		snd_printk(KERN_ERR "SB16 [0x%lx]:    got: irqreg=0x%x, dmareg=0x%x, mpureg = 0x%x\n", chip->port, irqreg, dmareg, mpureg);
		return -ENODEV;
	}
	return 0;
}

static const struct snd_pcm_ops snd_sb16_playback_ops = {
	.open =		snd_sb16_playback_open,
	.close =	snd_sb16_playback_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_sb16_hw_params,
	.hw_free =	snd_sb16_hw_free,
	.prepare =	snd_sb16_playback_prepare,
	.trigger =	snd_sb16_playback_trigger,
	.pointer =	snd_sb16_playback_pointer,
};

static const struct snd_pcm_ops snd_sb16_capture_ops = {
	.open =		snd_sb16_capture_open,
	.close =	snd_sb16_capture_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_sb16_hw_params,
	.hw_free =	snd_sb16_hw_free,
	.prepare =	snd_sb16_capture_prepare,
	.trigger =	snd_sb16_capture_trigger,
	.pointer =	snd_sb16_capture_pointer,
};

int snd_sb16dsp_pcm(struct snd_sb *chip, int device)
{
	struct snd_card *card = chip->card;
	struct snd_pcm *pcm;
	int err;

	if ((err = snd_pcm_new(card, "SB16 DSP", device, 1, 1, &pcm)) < 0)
		return err;
	sprintf(pcm->name, "DSP v%i.%i", chip->version >> 8, chip->version & 0xff);
	pcm->info_flags = SNDRV_PCM_INFO_JOINT_DUPLEX;
	pcm->private_data = chip;
	chip->pcm = pcm;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_sb16_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_sb16_capture_ops);

	if (chip->dma16 >= 0 && chip->dma8 != chip->dma16) {
		err = snd_ctl_add(card, snd_ctl_new1(
					&snd_sb16_dma_control, chip));
		if (err)
			return err;
	} else {
		pcm->info_flags = SNDRV_PCM_INFO_HALF_DUPLEX;
	}

	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
					      card->dev,
					      64*1024, 128*1024);
	return 0;
}

const struct snd_pcm_ops *snd_sb16dsp_get_pcm_ops(int direction)
{
	return direction == SNDRV_PCM_STREAM_PLAYBACK ?
		&snd_sb16_playback_ops : &snd_sb16_capture_ops;
}

EXPORT_SYMBOL(snd_sb16dsp_pcm);
EXPORT_SYMBOL(snd_sb16dsp_get_pcm_ops);
EXPORT_SYMBOL(snd_sb16dsp_configure);
EXPORT_SYMBOL(snd_sb16dsp_interrupt);
