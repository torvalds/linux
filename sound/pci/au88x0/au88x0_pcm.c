/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
 
/*
 * Vortex PCM ALSA driver.
 *
 * Supports ADB and WT DMA. Unfortunately, WT channels do not run yet.
 * It remains stuck,and DMA transfers do not happen. 
 */
#include <sound/asoundef.h>
#include <linux/time.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include "au88x0.h"

#define VORTEX_PCM_TYPE(x) (x->name[40])

/* hardware definition */
static struct snd_pcm_hardware snd_vortex_playback_hw_adb = {
	.info =
	    (SNDRV_PCM_INFO_MMAP | /* SNDRV_PCM_INFO_RESUME | */
	     SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_INTERLEAVED |
	     SNDRV_PCM_INFO_MMAP_VALID),
	.formats =
	    SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_U8 |
	    SNDRV_PCM_FMTBIT_MU_LAW | SNDRV_PCM_FMTBIT_A_LAW,
	.rates = SNDRV_PCM_RATE_CONTINUOUS,
	.rate_min = 5000,
	.rate_max = 48000,
	.channels_min = 1,
#ifdef CHIP_AU8830
	.channels_max = 4,
#else
	.channels_max = 2,
#endif
	.buffer_bytes_max = 0x10000,
	.period_bytes_min = 0x1,
	.period_bytes_max = 0x1000,
	.periods_min = 2,
	.periods_max = 32,
};

#ifndef CHIP_AU8820
static struct snd_pcm_hardware snd_vortex_playback_hw_a3d = {
	.info =
	    (SNDRV_PCM_INFO_MMAP | /* SNDRV_PCM_INFO_RESUME | */
	     SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_INTERLEAVED |
	     SNDRV_PCM_INFO_MMAP_VALID),
	.formats =
	    SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_U8 |
	    SNDRV_PCM_FMTBIT_MU_LAW | SNDRV_PCM_FMTBIT_A_LAW,
	.rates = SNDRV_PCM_RATE_CONTINUOUS,
	.rate_min = 5000,
	.rate_max = 48000,
	.channels_min = 1,
	.channels_max = 1,
	.buffer_bytes_max = 0x10000,
	.period_bytes_min = 0x100,
	.period_bytes_max = 0x1000,
	.periods_min = 2,
	.periods_max = 64,
};
#endif
static struct snd_pcm_hardware snd_vortex_playback_hw_spdif = {
	.info =
	    (SNDRV_PCM_INFO_MMAP | /* SNDRV_PCM_INFO_RESUME | */
	     SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_INTERLEAVED |
	     SNDRV_PCM_INFO_MMAP_VALID),
	.formats =
	    SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_U8 |
	    SNDRV_PCM_FMTBIT_IEC958_SUBFRAME_LE | SNDRV_PCM_FMTBIT_MU_LAW |
	    SNDRV_PCM_FMTBIT_A_LAW,
	.rates =
	    SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000,
	.rate_min = 32000,
	.rate_max = 48000,
	.channels_min = 1,
	.channels_max = 2,
	.buffer_bytes_max = 0x10000,
	.period_bytes_min = 0x100,
	.period_bytes_max = 0x1000,
	.periods_min = 2,
	.periods_max = 64,
};

#ifndef CHIP_AU8810
static struct snd_pcm_hardware snd_vortex_playback_hw_wt = {
	.info = (SNDRV_PCM_INFO_MMAP |
		 SNDRV_PCM_INFO_INTERLEAVED |
		 SNDRV_PCM_INFO_BLOCK_TRANSFER | SNDRV_PCM_INFO_MMAP_VALID),
	.formats = SNDRV_PCM_FMTBIT_S16_LE,
	.rates = SNDRV_PCM_RATE_8000_48000 | SNDRV_PCM_RATE_CONTINUOUS,	// SNDRV_PCM_RATE_48000,
	.rate_min = 8000,
	.rate_max = 48000,
	.channels_min = 1,
	.channels_max = 2,
	.buffer_bytes_max = 0x10000,
	.period_bytes_min = 0x0400,
	.period_bytes_max = 0x1000,
	.periods_min = 2,
	.periods_max = 64,
};
#endif
/* open callback */
static int snd_vortex_pcm_open(struct snd_pcm_substream *substream)
{
	vortex_t *vortex = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	int err;
	
	/* Force equal size periods */
	if ((err =
	     snd_pcm_hw_constraint_integer(runtime,
					   SNDRV_PCM_HW_PARAM_PERIODS)) < 0)
		return err;
	/* Avoid PAGE_SIZE boundary to fall inside of a period. */
	if ((err =
	     snd_pcm_hw_constraint_pow2(runtime, 0,
					SNDRV_PCM_HW_PARAM_PERIOD_BYTES)) < 0)
		return err;

	if (VORTEX_PCM_TYPE(substream->pcm) != VORTEX_PCM_WT) {
#ifndef CHIP_AU8820
		if (VORTEX_PCM_TYPE(substream->pcm) == VORTEX_PCM_A3D) {
			runtime->hw = snd_vortex_playback_hw_a3d;
		}
#endif
		if (VORTEX_PCM_TYPE(substream->pcm) == VORTEX_PCM_SPDIF) {
			runtime->hw = snd_vortex_playback_hw_spdif;
			switch (vortex->spdif_sr) {
			case 32000:
				runtime->hw.rates = SNDRV_PCM_RATE_32000;
				break;
			case 44100:
				runtime->hw.rates = SNDRV_PCM_RATE_44100;
				break;
			case 48000:
				runtime->hw.rates = SNDRV_PCM_RATE_48000;
				break;
			}
		}
		if (VORTEX_PCM_TYPE(substream->pcm) == VORTEX_PCM_ADB
		    || VORTEX_PCM_TYPE(substream->pcm) == VORTEX_PCM_I2S)
			runtime->hw = snd_vortex_playback_hw_adb;
		substream->runtime->private_data = NULL;
	}
#ifndef CHIP_AU8810
	else {
		runtime->hw = snd_vortex_playback_hw_wt;
		substream->runtime->private_data = NULL;
	}
#endif
	return 0;
}

/* close callback */
static int snd_vortex_pcm_close(struct snd_pcm_substream *substream)
{
	//vortex_t *chip = snd_pcm_substream_chip(substream);
	stream_t *stream = (stream_t *) substream->runtime->private_data;

	// the hardware-specific codes will be here
	if (stream != NULL) {
		stream->substream = NULL;
		stream->nr_ch = 0;
	}
	substream->runtime->private_data = NULL;
	return 0;
}

/* hw_params callback */
static int
snd_vortex_pcm_hw_params(struct snd_pcm_substream *substream,
			 struct snd_pcm_hw_params *hw_params)
{
	vortex_t *chip = snd_pcm_substream_chip(substream);
	stream_t *stream = (stream_t *) (substream->runtime->private_data);
	struct snd_sg_buf *sgbuf;
	int err;

	// Alloc buffer memory.
	err =
	    snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params));
	if (err < 0) {
		printk(KERN_ERR "Vortex: pcm page alloc failed!\n");
		return err;
	}
	//sgbuf = (struct snd_sg_buf *) substream->runtime->dma_private;
	sgbuf = snd_pcm_substream_sgbuf(substream);
	/*
	   printk(KERN_INFO "Vortex: periods %d, period_bytes %d, channels = %d\n", params_periods(hw_params),
	   params_period_bytes(hw_params), params_channels(hw_params));
	 */
	spin_lock_irq(&chip->lock);
	// Make audio routes and config buffer DMA.
	if (VORTEX_PCM_TYPE(substream->pcm) != VORTEX_PCM_WT) {
		int dma, type = VORTEX_PCM_TYPE(substream->pcm);
		/* Dealloc any routes. */
		if (stream != NULL)
			vortex_adb_allocroute(chip, stream->dma,
					      stream->nr_ch, stream->dir,
					      stream->type);
		/* Alloc routes. */
		dma =
		    vortex_adb_allocroute(chip, -1,
					  params_channels(hw_params),
					  substream->stream, type);
		if (dma < 0) {
			spin_unlock_irq(&chip->lock);
			return dma;
		}
		stream = substream->runtime->private_data = &chip->dma_adb[dma];
		stream->substream = substream;
		/* Setup Buffers. */
		vortex_adbdma_setbuffers(chip, dma, sgbuf,
					 params_period_bytes(hw_params),
					 params_periods(hw_params));
	}
#ifndef CHIP_AU8810
	else {
		/* if (stream != NULL)
		   vortex_wt_allocroute(chip, substream->number, 0); */
		vortex_wt_allocroute(chip, substream->number,
				     params_channels(hw_params));
		stream = substream->runtime->private_data =
		    &chip->dma_wt[substream->number];
		stream->dma = substream->number;
		stream->substream = substream;
		vortex_wtdma_setbuffers(chip, substream->number, sgbuf,
					params_period_bytes(hw_params),
					params_periods(hw_params));
	}
#endif
	spin_unlock_irq(&chip->lock);
	return 0;
}

/* hw_free callback */
static int snd_vortex_pcm_hw_free(struct snd_pcm_substream *substream)
{
	vortex_t *chip = snd_pcm_substream_chip(substream);
	stream_t *stream = (stream_t *) (substream->runtime->private_data);

	spin_lock_irq(&chip->lock);
	// Delete audio routes.
	if (VORTEX_PCM_TYPE(substream->pcm) != VORTEX_PCM_WT) {
		if (stream != NULL)
			vortex_adb_allocroute(chip, stream->dma,
					      stream->nr_ch, stream->dir,
					      stream->type);
	}
#ifndef CHIP_AU8810
	else {
		if (stream != NULL)
			vortex_wt_allocroute(chip, stream->dma, 0);
	}
#endif
	substream->runtime->private_data = NULL;
	spin_unlock_irq(&chip->lock);

	return snd_pcm_lib_free_pages(substream);
}

/* prepare callback */
static int snd_vortex_pcm_prepare(struct snd_pcm_substream *substream)
{
	vortex_t *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	stream_t *stream = (stream_t *) substream->runtime->private_data;
	int dma = stream->dma, fmt, dir;

	// set up the hardware with the current configuration.
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		dir = 1;
	else
		dir = 0;
	fmt = vortex_alsafmt_aspfmt(runtime->format);
	spin_lock_irq(&chip->lock);
	if (VORTEX_PCM_TYPE(substream->pcm) != VORTEX_PCM_WT) {
		vortex_adbdma_setmode(chip, dma, 1, dir, fmt, 0 /*? */ ,
				      0);
		vortex_adbdma_setstartbuffer(chip, dma, 0);
		if (VORTEX_PCM_TYPE(substream->pcm) != VORTEX_PCM_SPDIF)
			vortex_adb_setsrc(chip, dma, runtime->rate, dir);
	}
#ifndef CHIP_AU8810
	else {
		vortex_wtdma_setmode(chip, dma, 1, fmt, 0, 0);
		// FIXME: Set rate (i guess using vortex_wt_writereg() somehow).
		vortex_wtdma_setstartbuffer(chip, dma, 0);
	}
#endif
	spin_unlock_irq(&chip->lock);
	return 0;
}

/* trigger callback */
static int snd_vortex_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	vortex_t *chip = snd_pcm_substream_chip(substream);
	stream_t *stream = (stream_t *) substream->runtime->private_data;
	int dma = stream->dma;

	spin_lock(&chip->lock);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		// do something to start the PCM engine
		//printk(KERN_INFO "vortex: start %d\n", dma);
		stream->fifo_enabled = 1;
		if (VORTEX_PCM_TYPE(substream->pcm) != VORTEX_PCM_WT) {
			vortex_adbdma_resetup(chip, dma);
			vortex_adbdma_startfifo(chip, dma);
		}
#ifndef CHIP_AU8810
		else {
			printk(KERN_INFO "vortex: wt start %d\n", dma);
			vortex_wtdma_startfifo(chip, dma);
		}
#endif
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		// do something to stop the PCM engine
		//printk(KERN_INFO "vortex: stop %d\n", dma);
		stream->fifo_enabled = 0;
		if (VORTEX_PCM_TYPE(substream->pcm) != VORTEX_PCM_WT)
			vortex_adbdma_pausefifo(chip, dma);
		//vortex_adbdma_stopfifo(chip, dma);
#ifndef CHIP_AU8810
		else {
			printk(KERN_INFO "vortex: wt stop %d\n", dma);
			vortex_wtdma_stopfifo(chip, dma);
		}
#endif
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		//printk(KERN_INFO "vortex: pause %d\n", dma);
		if (VORTEX_PCM_TYPE(substream->pcm) != VORTEX_PCM_WT)
			vortex_adbdma_pausefifo(chip, dma);
#ifndef CHIP_AU8810
		else
			vortex_wtdma_pausefifo(chip, dma);
#endif
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		//printk(KERN_INFO "vortex: resume %d\n", dma);
		if (VORTEX_PCM_TYPE(substream->pcm) != VORTEX_PCM_WT)
			vortex_adbdma_resumefifo(chip, dma);
#ifndef CHIP_AU8810
		else
			vortex_wtdma_resumefifo(chip, dma);
#endif
		break;
	default:
		spin_unlock(&chip->lock);
		return -EINVAL;
	}
	spin_unlock(&chip->lock);
	return 0;
}

/* pointer callback */
static snd_pcm_uframes_t snd_vortex_pcm_pointer(struct snd_pcm_substream *substream)
{
	vortex_t *chip = snd_pcm_substream_chip(substream);
	stream_t *stream = (stream_t *) substream->runtime->private_data;
	int dma = stream->dma;
	snd_pcm_uframes_t current_ptr = 0;

	spin_lock(&chip->lock);
	if (VORTEX_PCM_TYPE(substream->pcm) != VORTEX_PCM_WT)
		current_ptr = vortex_adbdma_getlinearpos(chip, dma);
#ifndef CHIP_AU8810
	else
		current_ptr = vortex_wtdma_getlinearpos(chip, dma);
#endif
	//printk(KERN_INFO "vortex: pointer = 0x%x\n", current_ptr);
	spin_unlock(&chip->lock);
	return (bytes_to_frames(substream->runtime, current_ptr));
}

/* Page callback. */
/*
static struct page *snd_pcm_sgbuf_ops_page(struct snd_pcm_substream *substream, unsigned long offset) {
	
	
}
*/
/* operators */
static struct snd_pcm_ops snd_vortex_playback_ops = {
	.open = snd_vortex_pcm_open,
	.close = snd_vortex_pcm_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = snd_vortex_pcm_hw_params,
	.hw_free = snd_vortex_pcm_hw_free,
	.prepare = snd_vortex_pcm_prepare,
	.trigger = snd_vortex_pcm_trigger,
	.pointer = snd_vortex_pcm_pointer,
	.page = snd_pcm_sgbuf_ops_page,
};

/*
*  definitions of capture are omitted here...
*/

static char *vortex_pcm_prettyname[VORTEX_PCM_LAST] = {
	"AU88x0 ADB",
	"AU88x0 SPDIF",
	"AU88x0 A3D",
	"AU88x0 WT",
	"AU88x0 I2S",
};
static char *vortex_pcm_name[VORTEX_PCM_LAST] = {
	"adb",
	"spdif",
	"a3d",
	"wt",
	"i2s",
};

/* SPDIF kcontrol */

static int snd_vortex_spdif_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int snd_vortex_spdif_mask_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.iec958.status[0] = 0xff;
	ucontrol->value.iec958.status[1] = 0xff;
	ucontrol->value.iec958.status[2] = 0xff;
	ucontrol->value.iec958.status[3] = IEC958_AES3_CON_FS;
	return 0;
}

static int snd_vortex_spdif_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	vortex_t *vortex = snd_kcontrol_chip(kcontrol);
	ucontrol->value.iec958.status[0] = 0x00;
	ucontrol->value.iec958.status[1] = IEC958_AES1_CON_ORIGINAL|IEC958_AES1_CON_DIGDIGCONV_ID;
	ucontrol->value.iec958.status[2] = 0x00;
	switch (vortex->spdif_sr) {
	case 32000: ucontrol->value.iec958.status[3] = IEC958_AES3_CON_FS_32000; break;
	case 44100: ucontrol->value.iec958.status[3] = IEC958_AES3_CON_FS_44100; break;
	case 48000: ucontrol->value.iec958.status[3] = IEC958_AES3_CON_FS_48000; break;
	}
	return 0;
}

static int snd_vortex_spdif_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	vortex_t *vortex = snd_kcontrol_chip(kcontrol);
	int spdif_sr = 48000;
	switch (ucontrol->value.iec958.status[3] & IEC958_AES3_CON_FS) {
	case IEC958_AES3_CON_FS_32000: spdif_sr = 32000; break;
	case IEC958_AES3_CON_FS_44100: spdif_sr = 44100; break;
	case IEC958_AES3_CON_FS_48000: spdif_sr = 48000; break;
	}
	if (spdif_sr == vortex->spdif_sr)
		return 0;
	vortex->spdif_sr = spdif_sr;
	vortex_spdif_init(vortex, vortex->spdif_sr, 1);
	return 1;
}

/* spdif controls */
static struct snd_kcontrol_new snd_vortex_mixer_spdif[] __devinitdata = {
	{
		.iface =	SNDRV_CTL_ELEM_IFACE_PCM,
		.name =		SNDRV_CTL_NAME_IEC958("",PLAYBACK,DEFAULT),
		.info =		snd_vortex_spdif_info,
		.get =		snd_vortex_spdif_get,
		.put =		snd_vortex_spdif_put,
	},
	{
		.access =	SNDRV_CTL_ELEM_ACCESS_READ,
		.iface =	SNDRV_CTL_ELEM_IFACE_PCM,
		.name =		SNDRV_CTL_NAME_IEC958("",PLAYBACK,CON_MASK),
		.info =		snd_vortex_spdif_info,
		.get =		snd_vortex_spdif_mask_get
	},
};

/* create a pcm device */
static int __devinit snd_vortex_new_pcm(vortex_t * chip, int idx, int nr)
{
	struct snd_pcm *pcm;
	struct snd_kcontrol *kctl;
	int i;
	int err, nr_capt;

	if ((chip == 0) || (idx < 0) || (idx >= VORTEX_PCM_LAST))
		return -ENODEV;

	/* idx indicates which kind of PCM device. ADB, SPDIF, I2S and A3D share the 
	 * same dma engine. WT uses it own separate dma engine whcih cant capture. */
	if (idx == VORTEX_PCM_ADB)
		nr_capt = nr;
	else
		nr_capt = 0;
	if ((err =
	     snd_pcm_new(chip->card, vortex_pcm_prettyname[idx], idx, nr,
			 nr_capt, &pcm)) < 0)
		return err;
	strcpy(pcm->name, vortex_pcm_name[idx]);
	chip->pcm[idx] = pcm;
	// This is an evil hack, but it saves a lot of duplicated code.
	VORTEX_PCM_TYPE(pcm) = idx;
	pcm->private_data = chip;
	/* set operators */
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK,
			&snd_vortex_playback_ops);
	if (idx == VORTEX_PCM_ADB)
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE,
				&snd_vortex_playback_ops);
	
	/* pre-allocation of Scatter-Gather buffers */
	
	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV_SG,
					      snd_dma_pci_data(chip->pci_dev),
					      0x10000, 0x10000);

	if (VORTEX_PCM_TYPE(pcm) == VORTEX_PCM_SPDIF) {
		for (i = 0; i < ARRAY_SIZE(snd_vortex_mixer_spdif); i++) {
			kctl = snd_ctl_new1(&snd_vortex_mixer_spdif[i], chip);
			if (!kctl)
				return -ENOMEM;
			if ((err = snd_ctl_add(chip->card, kctl)) < 0)
				return err;
		}
	}
	return 0;
}
