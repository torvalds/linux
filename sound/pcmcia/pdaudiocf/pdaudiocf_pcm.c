/*
 * Driver for Sound Core PDAudioCF soundcards
 *
 * PCM part
 *
 * Copyright (c) 2003 by Jaroslav Kysela <perex@perex.cz>
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
 */

#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <sound/core.h>
#include <sound/asoundef.h>
#include "pdaudiocf.h"


/*
 * we use a vmalloc'ed (sg-)buffer
 */

/* get the physical page pointer on the given offset */
static struct page *snd_pcm_get_vmalloc_page(struct snd_pcm_substream *subs, unsigned long offset)
{
	void *pageptr = subs->runtime->dma_area + offset;
	return vmalloc_to_page(pageptr);
}

/*
 * hw_params callback
 * NOTE: this may be called not only once per pcm open!
 */
static int snd_pcm_alloc_vmalloc_buffer(struct snd_pcm_substream *subs, size_t size)
{
	struct snd_pcm_runtime *runtime = subs->runtime;
	if (runtime->dma_area) {
		if (runtime->dma_bytes >= size)
			return 0; /* already enough large */
		vfree(runtime->dma_area);
	}
	runtime->dma_area = vmalloc_32(size);
	if (! runtime->dma_area)
		return -ENOMEM;
	runtime->dma_bytes = size;
	return 0;
}

/*
 * hw_free callback
 * NOTE: this may be called not only once per pcm open!
 */
static int snd_pcm_free_vmalloc_buffer(struct snd_pcm_substream *subs)
{
	struct snd_pcm_runtime *runtime = subs->runtime;

	vfree(runtime->dma_area);
	runtime->dma_area = NULL;
	return 0;
}

/*
 * clear the SRAM contents
 */
static int pdacf_pcm_clear_sram(struct snd_pdacf *chip)
{
	int max_loop = 64 * 1024;

	while (inw(chip->port + PDAUDIOCF_REG_RDP) != inw(chip->port + PDAUDIOCF_REG_WDP)) {
		if (max_loop-- < 0)
			return -EIO;
		inw(chip->port + PDAUDIOCF_REG_MD);
	}
	return 0;
}

/*
 * pdacf_pcm_trigger - trigger callback for capture
 */
static int pdacf_pcm_trigger(struct snd_pcm_substream *subs, int cmd)
{
	struct snd_pdacf *chip = snd_pcm_substream_chip(subs);
	struct snd_pcm_runtime *runtime = subs->runtime;
	int inc, ret = 0, rate;
	unsigned short mask, val, tmp;

	if (chip->chip_status & PDAUDIOCF_STAT_IS_STALE)
		return -EBUSY;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		chip->pcm_hwptr = 0;
		chip->pcm_tdone = 0;
		/* fall thru */
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
	case SNDRV_PCM_TRIGGER_RESUME:
		mask = 0;
		val = PDAUDIOCF_RECORD;
		inc = 1;
		rate = snd_ak4117_check_rate_and_errors(chip->ak4117, AK4117_CHECK_NO_STAT|AK4117_CHECK_NO_RATE);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		mask = PDAUDIOCF_RECORD;
		val = 0;
		inc = -1;
		rate = 0;
		break;
	default:
		return -EINVAL;
	}
	spin_lock(&chip->reg_lock);
	chip->pcm_running += inc;
	tmp = pdacf_reg_read(chip, PDAUDIOCF_REG_SCR);
	if (chip->pcm_running) {
		if ((chip->ak4117->rcs0 & AK4117_UNLCK) || runtime->rate != rate) {
			chip->pcm_running -= inc;
			ret = -EIO;
			goto __end;
		}
	}
	tmp &= ~mask;
	tmp |= val;
	pdacf_reg_write(chip, PDAUDIOCF_REG_SCR, tmp);
      __end:
	spin_unlock(&chip->reg_lock);
	snd_ak4117_check_rate_and_errors(chip->ak4117, AK4117_CHECK_NO_RATE);
	return ret;
}

/*
 * pdacf_pcm_hw_params - hw_params callback for playback and capture
 */
static int pdacf_pcm_hw_params(struct snd_pcm_substream *subs,
				     struct snd_pcm_hw_params *hw_params)
{
	return snd_pcm_alloc_vmalloc_buffer(subs, params_buffer_bytes(hw_params));
}

/*
 * pdacf_pcm_hw_free - hw_free callback for playback and capture
 */
static int pdacf_pcm_hw_free(struct snd_pcm_substream *subs)
{
	return snd_pcm_free_vmalloc_buffer(subs);
}

/*
 * pdacf_pcm_prepare - prepare callback for playback and capture
 */
static int pdacf_pcm_prepare(struct snd_pcm_substream *subs)
{
	struct snd_pdacf *chip = snd_pcm_substream_chip(subs);
	struct snd_pcm_runtime *runtime = subs->runtime;
	u16 val, nval, aval;

	if (chip->chip_status & PDAUDIOCF_STAT_IS_STALE)
		return -EBUSY;

	chip->pcm_channels = runtime->channels;

	chip->pcm_little = snd_pcm_format_little_endian(runtime->format) > 0;
#ifdef SNDRV_LITTLE_ENDIAN
	chip->pcm_swab = snd_pcm_format_big_endian(runtime->format) > 0;
#else
	chip->pcm_swab = chip->pcm_little;
#endif

	if (snd_pcm_format_unsigned(runtime->format))
		chip->pcm_xor = 0x80008000;

	if (pdacf_pcm_clear_sram(chip) < 0)
		return -EIO;
	
	val = nval = pdacf_reg_read(chip, PDAUDIOCF_REG_SCR);
	nval &= ~(PDAUDIOCF_DATAFMT0|PDAUDIOCF_DATAFMT1);
	switch (runtime->format) {
	case SNDRV_PCM_FORMAT_S16_LE:
	case SNDRV_PCM_FORMAT_S16_BE:
		break;
	default: /* 24-bit */
		nval |= PDAUDIOCF_DATAFMT0 | PDAUDIOCF_DATAFMT1;
		break;
	}
	aval = 0;
	chip->pcm_sample = 4;
	switch (runtime->format) {
	case SNDRV_PCM_FORMAT_S16_LE:
	case SNDRV_PCM_FORMAT_S16_BE:
		aval = AK4117_DIF_16R;
		chip->pcm_frame = 2;
		chip->pcm_sample = 2;
		break;
	case SNDRV_PCM_FORMAT_S24_3LE:
	case SNDRV_PCM_FORMAT_S24_3BE:
		chip->pcm_sample = 3;
		/* fall through */
	default: /* 24-bit */
		aval = AK4117_DIF_24R;
		chip->pcm_frame = 3;
		chip->pcm_xor &= 0xffff0000;
		break;
	}

	if (val != nval) {
		snd_ak4117_reg_write(chip->ak4117, AK4117_REG_IO, AK4117_DIF2|AK4117_DIF1|AK4117_DIF0, aval);
		pdacf_reg_write(chip, PDAUDIOCF_REG_SCR, nval);
	}

	val = pdacf_reg_read(chip,  PDAUDIOCF_REG_IER);
	val &= ~(PDAUDIOCF_IRQLVLEN1);
	val |= PDAUDIOCF_IRQLVLEN0;
	pdacf_reg_write(chip, PDAUDIOCF_REG_IER, val);

	chip->pcm_size = runtime->buffer_size;
	chip->pcm_period = runtime->period_size;
	chip->pcm_area = runtime->dma_area;

	return 0;
}


/*
 * capture hw information
 */

static struct snd_pcm_hardware pdacf_pcm_capture_hw = {
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_RESUME |
				 SNDRV_PCM_INFO_MMAP_VALID),
	.formats =		SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S16_BE |
				SNDRV_PCM_FMTBIT_S24_3LE | SNDRV_PCM_FMTBIT_S24_3BE |
				SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_S32_BE,
	.rates =		SNDRV_PCM_RATE_32000 |
				SNDRV_PCM_RATE_44100 |
				SNDRV_PCM_RATE_48000 |
				SNDRV_PCM_RATE_88200 |
				SNDRV_PCM_RATE_96000 |
				SNDRV_PCM_RATE_176400 |
				SNDRV_PCM_RATE_192000,
	.rate_min =		32000,
	.rate_max =		192000,
	.channels_min =		1,
	.channels_max =		2,
	.buffer_bytes_max =	(512*1024),
	.period_bytes_min =	8*1024,
	.period_bytes_max =	(64*1024),
	.periods_min =		2,
	.periods_max =		128,
	.fifo_size =		0,
};


/*
 * pdacf_pcm_capture_open - open callback for capture
 */
static int pdacf_pcm_capture_open(struct snd_pcm_substream *subs)
{
	struct snd_pcm_runtime *runtime = subs->runtime;
	struct snd_pdacf *chip = snd_pcm_substream_chip(subs);

	if (chip->chip_status & PDAUDIOCF_STAT_IS_STALE)
		return -EBUSY;

	runtime->hw = pdacf_pcm_capture_hw;
	runtime->private_data = chip;
	chip->pcm_substream = subs;

	return 0;
}

/*
 * pdacf_pcm_capture_close - close callback for capture
 */
static int pdacf_pcm_capture_close(struct snd_pcm_substream *subs)
{
	struct snd_pdacf *chip = snd_pcm_substream_chip(subs);

	if (!chip)
		return -EINVAL;
	pdacf_reinit(chip, 0);
	chip->pcm_substream = NULL;
	return 0;
}


/*
 * pdacf_pcm_capture_pointer - pointer callback for capture
 */
static snd_pcm_uframes_t pdacf_pcm_capture_pointer(struct snd_pcm_substream *subs)
{
	struct snd_pdacf *chip = snd_pcm_substream_chip(subs);
	return chip->pcm_hwptr;
}

/*
 * operators for PCM capture
 */
static struct snd_pcm_ops pdacf_pcm_capture_ops = {
	.open =		pdacf_pcm_capture_open,
	.close =	pdacf_pcm_capture_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	pdacf_pcm_hw_params,
	.hw_free =	pdacf_pcm_hw_free,
	.prepare =	pdacf_pcm_prepare,
	.trigger =	pdacf_pcm_trigger,
	.pointer =	pdacf_pcm_capture_pointer,
	.page =		snd_pcm_get_vmalloc_page,
};


/*
 * snd_pdacf_pcm_new - create and initialize a pcm
 */
int snd_pdacf_pcm_new(struct snd_pdacf *chip)
{
	struct snd_pcm *pcm;
	int err;

	err = snd_pcm_new(chip->card, "PDAudioCF", 0, 0, 1, &pcm);
	if (err < 0)
		return err;
		
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &pdacf_pcm_capture_ops);

	pcm->private_data = chip;
	pcm->info_flags = 0;
	strcpy(pcm->name, chip->card->shortname);
	chip->pcm = pcm;
	
	err = snd_ak4117_build(chip->ak4117, pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream);
	if (err < 0)
		return err;

	return 0;
}
