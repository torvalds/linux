// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Support for Digigram Lola PCI-e boards
 *
 *  Copyright (c) 2011 Takashi Iwai <tiwai@suse.de>
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include "lola.h"

#define LOLA_MAX_BDL_ENTRIES	8
#define LOLA_MAX_BUF_SIZE	(1024*1024*1024)
#define LOLA_BDL_ENTRY_SIZE	(16 * 16)

static struct lola_pcm *lola_get_pcm(struct snd_pcm_substream *substream)
{
	struct lola *chip = snd_pcm_substream_chip(substream);
	return &chip->pcm[substream->stream];
}

static struct lola_stream *lola_get_stream(struct snd_pcm_substream *substream)
{
	struct lola_pcm *pcm = lola_get_pcm(substream);
	unsigned int idx = substream->number;
	return &pcm->streams[idx];
}

static unsigned int lola_get_lrc(struct lola *chip)
{
	return lola_readl(chip, BAR1, LRC);
}

static unsigned int lola_get_tstamp(struct lola *chip, bool quick_no_sync)
{
	unsigned int tstamp = lola_get_lrc(chip) >> 8;
	if (chip->granularity) {
		unsigned int wait_banks = quick_no_sync ? 0 : 8;
		tstamp += (wait_banks + 1) * chip->granularity - 1;
		tstamp -= tstamp % chip->granularity;
	}
	return tstamp << 8;
}

/* clear any pending interrupt status */
static void lola_stream_clear_pending_irq(struct lola *chip,
					  struct lola_stream *str)
{
	unsigned int val = lola_dsd_read(chip, str->dsd, STS);
	val &= LOLA_DSD_STS_DESE | LOLA_DSD_STS_BCIS;
	if (val)
		lola_dsd_write(chip, str->dsd, STS, val);
}

static void lola_stream_start(struct lola *chip, struct lola_stream *str,
			      unsigned int tstamp)
{
	lola_stream_clear_pending_irq(chip, str);
	lola_dsd_write(chip, str->dsd, CTL,
		       LOLA_DSD_CTL_SRUN |
		       LOLA_DSD_CTL_IOCE |
		       LOLA_DSD_CTL_DEIE |
		       LOLA_DSD_CTL_VLRCV |
		       tstamp);
}

static void lola_stream_stop(struct lola *chip, struct lola_stream *str,
			     unsigned int tstamp)
{
	lola_dsd_write(chip, str->dsd, CTL,
		       LOLA_DSD_CTL_IOCE |
		       LOLA_DSD_CTL_DEIE |
		       LOLA_DSD_CTL_VLRCV |
		       tstamp);
	lola_stream_clear_pending_irq(chip, str);
}

static void wait_for_srst_clear(struct lola *chip, struct lola_stream *str)
{
	unsigned long end_time = jiffies + msecs_to_jiffies(200);
	while (time_before(jiffies, end_time)) {
		unsigned int val;
		val = lola_dsd_read(chip, str->dsd, CTL);
		if (!(val & LOLA_DSD_CTL_SRST))
			return;
		msleep(1);
	}
	dev_warn(chip->card->dev, "SRST not clear (stream %d)\n", str->dsd);
}

static int lola_stream_wait_for_fifo(struct lola *chip,
				     struct lola_stream *str,
				     bool ready)
{
	unsigned int val = ready ? LOLA_DSD_STS_FIFORDY : 0;
	unsigned long end_time = jiffies + msecs_to_jiffies(200);
	while (time_before(jiffies, end_time)) {
		unsigned int reg = lola_dsd_read(chip, str->dsd, STS);
		if ((reg & LOLA_DSD_STS_FIFORDY) == val)
			return 0;
		msleep(1);
	}
	dev_warn(chip->card->dev, "FIFO not ready (stream %d)\n", str->dsd);
	return -EIO;
}

/* sync for FIFO ready/empty for all linked streams;
 * clear paused flag when FIFO gets ready again
 */
static int lola_sync_wait_for_fifo(struct lola *chip,
				   struct snd_pcm_substream *substream,
				   bool ready)
{
	unsigned int val = ready ? LOLA_DSD_STS_FIFORDY : 0;
	unsigned long end_time = jiffies + msecs_to_jiffies(200);
	struct snd_pcm_substream *s;
	int pending = 0;

	while (time_before(jiffies, end_time)) {
		pending = 0;
		snd_pcm_group_for_each_entry(s, substream) {
			struct lola_stream *str;
			if (s->pcm->card != substream->pcm->card)
				continue;
			str = lola_get_stream(s);
			if (str->prepared && str->paused) {
				unsigned int reg;
				reg = lola_dsd_read(chip, str->dsd, STS);
				if ((reg & LOLA_DSD_STS_FIFORDY) != val) {
					pending = str->dsd + 1;
					break;
				}
				if (ready)
					str->paused = 0;
			}
		}
		if (!pending)
			return 0;
		msleep(1);
	}
	dev_warn(chip->card->dev, "FIFO not ready (pending %d)\n", pending - 1);
	return -EIO;
}

/* finish pause - prepare for a new resume */
static void lola_sync_pause(struct lola *chip,
			    struct snd_pcm_substream *substream)
{
	struct snd_pcm_substream *s;

	lola_sync_wait_for_fifo(chip, substream, false);
	snd_pcm_group_for_each_entry(s, substream) {
		struct lola_stream *str;
		if (s->pcm->card != substream->pcm->card)
			continue;
		str = lola_get_stream(s);
		if (str->paused && str->prepared)
			lola_dsd_write(chip, str->dsd, CTL, LOLA_DSD_CTL_SRUN |
				       LOLA_DSD_CTL_IOCE | LOLA_DSD_CTL_DEIE);
	}
	lola_sync_wait_for_fifo(chip, substream, true);
}

static void lola_stream_reset(struct lola *chip, struct lola_stream *str)
{
	if (str->prepared) {
		if (str->paused)
			lola_sync_pause(chip, str->substream);
		str->prepared = 0;
		lola_dsd_write(chip, str->dsd, CTL,
			       LOLA_DSD_CTL_IOCE | LOLA_DSD_CTL_DEIE);
		lola_stream_wait_for_fifo(chip, str, false);
		lola_stream_clear_pending_irq(chip, str);
		lola_dsd_write(chip, str->dsd, CTL, LOLA_DSD_CTL_SRST);
		lola_dsd_write(chip, str->dsd, LVI, 0);
		lola_dsd_write(chip, str->dsd, BDPU, 0);
		lola_dsd_write(chip, str->dsd, BDPL, 0);
		wait_for_srst_clear(chip, str);
	}
}

static const struct snd_pcm_hardware lola_pcm_hw = {
	.info =			(SNDRV_PCM_INFO_MMAP |
				 SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID |
				 SNDRV_PCM_INFO_PAUSE),
	.formats =		(SNDRV_PCM_FMTBIT_S16_LE |
				 SNDRV_PCM_FMTBIT_S24_LE |
				 SNDRV_PCM_FMTBIT_S32_LE |
				 SNDRV_PCM_FMTBIT_FLOAT_LE),
	.rates =		SNDRV_PCM_RATE_8000_192000,
	.rate_min =		8000,
	.rate_max =		192000,
	.channels_min =		1,
	.channels_max =		2,
	.buffer_bytes_max =	LOLA_MAX_BUF_SIZE,
	.period_bytes_min =	128,
	.period_bytes_max =	LOLA_MAX_BUF_SIZE / 2,
	.periods_min =		2,
	.periods_max =		LOLA_MAX_BDL_ENTRIES,
	.fifo_size =		0,
};

static int lola_pcm_open(struct snd_pcm_substream *substream)
{
	struct lola *chip = snd_pcm_substream_chip(substream);
	struct lola_pcm *pcm = lola_get_pcm(substream);
	struct lola_stream *str = lola_get_stream(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;

	mutex_lock(&chip->open_mutex);
	if (str->opened) {
		mutex_unlock(&chip->open_mutex);
		return -EBUSY;
	}
	str->substream = substream;
	str->master = NULL;
	str->opened = 1;
	runtime->hw = lola_pcm_hw;
	runtime->hw.channels_max = pcm->num_streams - str->index;
	if (chip->sample_rate) {
		/* sample rate is locked */
		runtime->hw.rate_min = chip->sample_rate;
		runtime->hw.rate_max = chip->sample_rate;
	} else {
		runtime->hw.rate_min = chip->sample_rate_min;
		runtime->hw.rate_max = chip->sample_rate_max;
	}
	chip->ref_count_rate++;
	snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS);
	/* period size = multiple of chip->granularity (8, 16 or 32 frames)*/
	snd_pcm_hw_constraint_step(runtime, 0, SNDRV_PCM_HW_PARAM_BUFFER_SIZE,
				   chip->granularity);
	snd_pcm_hw_constraint_step(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_SIZE,
				   chip->granularity);
	mutex_unlock(&chip->open_mutex);
	return 0;
}

static void lola_cleanup_slave_streams(struct lola_pcm *pcm,
				       struct lola_stream *str)
{
	int i;
	for (i = str->index + 1; i < pcm->num_streams; i++) {
		struct lola_stream *s = &pcm->streams[i];
		if (s->master != str)
			break;
		s->master = NULL;
		s->opened = 0;
	}
}

static int lola_pcm_close(struct snd_pcm_substream *substream)
{
	struct lola *chip = snd_pcm_substream_chip(substream);
	struct lola_stream *str = lola_get_stream(substream);

	mutex_lock(&chip->open_mutex);
	if (str->substream == substream) {
		str->substream = NULL;
		str->opened = 0;
	}
	if (--chip->ref_count_rate == 0) {
		/* release sample rate */
		chip->sample_rate = 0;
	}
	mutex_unlock(&chip->open_mutex);
	return 0;
}

static int lola_pcm_hw_params(struct snd_pcm_substream *substream,
			      struct snd_pcm_hw_params *hw_params)
{
	struct lola_stream *str = lola_get_stream(substream);

	str->bufsize = 0;
	str->period_bytes = 0;
	str->format_verb = 0;
	return 0;
}

static int lola_pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct lola *chip = snd_pcm_substream_chip(substream);
	struct lola_pcm *pcm = lola_get_pcm(substream);
	struct lola_stream *str = lola_get_stream(substream);

	mutex_lock(&chip->open_mutex);
	lola_stream_reset(chip, str);
	lola_cleanup_slave_streams(pcm, str);
	mutex_unlock(&chip->open_mutex);
	return 0;
}

/*
 * set up a BDL entry
 */
static int setup_bdle(struct snd_pcm_substream *substream,
		      struct lola_stream *str, __le32 **bdlp,
		      int ofs, int size)
{
	__le32 *bdl = *bdlp;

	while (size > 0) {
		dma_addr_t addr;
		int chunk;

		if (str->frags >= LOLA_MAX_BDL_ENTRIES)
			return -EINVAL;

		addr = snd_pcm_sgbuf_get_addr(substream, ofs);
		/* program the address field of the BDL entry */
		bdl[0] = cpu_to_le32((u32)addr);
		bdl[1] = cpu_to_le32(upper_32_bits(addr));
		/* program the size field of the BDL entry */
		chunk = snd_pcm_sgbuf_get_chunk_size(substream, ofs, size);
		bdl[2] = cpu_to_le32(chunk);
		/* program the IOC to enable interrupt
		 * only when the whole fragment is processed
		 */
		size -= chunk;
		bdl[3] = size ? 0 : cpu_to_le32(0x01);
		bdl += 4;
		str->frags++;
		ofs += chunk;
	}
	*bdlp = bdl;
	return ofs;
}

/*
 * set up BDL entries
 */
static int lola_setup_periods(struct lola *chip, struct lola_pcm *pcm,
			      struct snd_pcm_substream *substream,
			      struct lola_stream *str)
{
	__le32 *bdl;
	int i, ofs, periods, period_bytes;

	period_bytes = str->period_bytes;
	periods = str->bufsize / period_bytes;

	/* program the initial BDL entries */
	bdl = (__le32 *)(pcm->bdl.area + LOLA_BDL_ENTRY_SIZE * str->index);
	ofs = 0;
	str->frags = 0;
	for (i = 0; i < periods; i++) {
		ofs = setup_bdle(substream, str, &bdl, ofs, period_bytes);
		if (ofs < 0)
			goto error;
	}
	return 0;

 error:
	dev_err(chip->card->dev, "Too many BDL entries: buffer=%d, period=%d\n",
		   str->bufsize, period_bytes);
	return -EINVAL;
}

static unsigned int lola_get_format_verb(struct snd_pcm_substream *substream)
{
	unsigned int verb;

	switch (substream->runtime->format) {
	case SNDRV_PCM_FORMAT_S16_LE:
		verb = 0x00000000;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		verb = 0x00000200;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		verb = 0x00000300;
		break;
	case SNDRV_PCM_FORMAT_FLOAT_LE:
		verb = 0x00001300;
		break;
	default:
		return 0;
	}
	verb |= substream->runtime->channels;
	return verb;
}

static int lola_set_stream_config(struct lola *chip,
				  struct lola_stream *str,
				  int channels)
{
	int i, err;
	unsigned int verb, val;

	/* set format info for all channels
	 * (with only one command for the first channel)
	 */
	err = lola_codec_read(chip, str->nid, LOLA_VERB_SET_STREAM_FORMAT,
			      str->format_verb, 0, &val, NULL);
	if (err < 0) {
		dev_err(chip->card->dev, "Cannot set stream format 0x%x\n",
		       str->format_verb);
		return err;
	}

	/* update stream - channel config */
	for (i = 0; i < channels; i++) {
		verb = (str->index << 6) | i;
		err = lola_codec_read(chip, str[i].nid,
				      LOLA_VERB_SET_CHANNEL_STREAMID, 0, verb,
				      &val, NULL);
		if (err < 0) {
			dev_err(chip->card->dev,
				"Cannot set stream channel %d\n", i);
			return err;
		}
	}
	return 0;
}

/*
 * set up the SD for streaming
 */
static int lola_setup_controller(struct lola *chip, struct lola_pcm *pcm,
				 struct lola_stream *str)
{
	dma_addr_t bdl;

	if (str->prepared)
		return -EINVAL;

	/* set up BDL */
	bdl = pcm->bdl.addr + LOLA_BDL_ENTRY_SIZE * str->index;
	lola_dsd_write(chip, str->dsd, BDPL, (u32)bdl);
	lola_dsd_write(chip, str->dsd, BDPU, upper_32_bits(bdl));
	/* program the stream LVI (last valid index) of the BDL */
	lola_dsd_write(chip, str->dsd, LVI, str->frags - 1);
	lola_stream_clear_pending_irq(chip, str);

 	lola_dsd_write(chip, str->dsd, CTL,
		       LOLA_DSD_CTL_IOCE | LOLA_DSD_CTL_DEIE | LOLA_DSD_CTL_SRUN);

	str->prepared = 1;

	return lola_stream_wait_for_fifo(chip, str, true);
}

static int lola_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct lola *chip = snd_pcm_substream_chip(substream);
	struct lola_pcm *pcm = lola_get_pcm(substream);
	struct lola_stream *str = lola_get_stream(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned int bufsize, period_bytes, format_verb;
	int i, err;

	mutex_lock(&chip->open_mutex);
	lola_stream_reset(chip, str);
	lola_cleanup_slave_streams(pcm, str);
	if (str->index + runtime->channels > pcm->num_streams) {
		mutex_unlock(&chip->open_mutex);
		return -EINVAL;
	}
	for (i = 1; i < runtime->channels; i++) {
		str[i].master = str;
		str[i].opened = 1;
	}
	mutex_unlock(&chip->open_mutex);

	bufsize = snd_pcm_lib_buffer_bytes(substream);
	period_bytes = snd_pcm_lib_period_bytes(substream);
	format_verb = lola_get_format_verb(substream);

	str->bufsize = bufsize;
	str->period_bytes = period_bytes;
	str->format_verb = format_verb;

	err = lola_setup_periods(chip, pcm, substream, str);
	if (err < 0)
		return err;

	err = lola_set_sample_rate(chip, runtime->rate);
	if (err < 0)
		return err;
	chip->sample_rate = runtime->rate;	/* sample rate gets locked */

	err = lola_set_stream_config(chip, str, runtime->channels);
	if (err < 0)
		return err;

	err = lola_setup_controller(chip, pcm, str);
	if (err < 0) {
		lola_stream_reset(chip, str);
		return err;
	}

	return 0;
}

static int lola_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct lola *chip = snd_pcm_substream_chip(substream);
	struct lola_stream *str;
	struct snd_pcm_substream *s;
	unsigned int start;
	unsigned int tstamp;
	bool sync_streams;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
	case SNDRV_PCM_TRIGGER_RESUME:
		start = 1;
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_STOP:
		start = 0;
		break;
	default:
		return -EINVAL;
	}

	/*
	 * sample correct synchronization is only needed starting several
	 * streams. On stop or if only one stream do as quick as possible
	 */
	sync_streams = (start && snd_pcm_stream_linked(substream));
	tstamp = lola_get_tstamp(chip, !sync_streams);
	spin_lock(&chip->reg_lock);
	snd_pcm_group_for_each_entry(s, substream) {
		if (s->pcm->card != substream->pcm->card)
			continue;
		str = lola_get_stream(s);
		if (start)
			lola_stream_start(chip, str, tstamp);
		else
			lola_stream_stop(chip, str, tstamp);
		str->running = start;
		str->paused = !start;
		snd_pcm_trigger_done(s, substream);
	}
	spin_unlock(&chip->reg_lock);
	return 0;
}

static snd_pcm_uframes_t lola_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct lola *chip = snd_pcm_substream_chip(substream);
	struct lola_stream *str = lola_get_stream(substream);
	unsigned int pos = lola_dsd_read(chip, str->dsd, LPIB);

	if (pos >= str->bufsize)
		pos = 0;
	return bytes_to_frames(substream->runtime, pos);
}

void lola_pcm_update(struct lola *chip, struct lola_pcm *pcm, unsigned int bits)
{
	int i;

	for (i = 0; bits && i < pcm->num_streams; i++) {
		if (bits & (1 << i)) {
			struct lola_stream *str = &pcm->streams[i];
			if (str->substream && str->running)
				snd_pcm_period_elapsed(str->substream);
			bits &= ~(1 << i);
		}
	}
}

static const struct snd_pcm_ops lola_pcm_ops = {
	.open = lola_pcm_open,
	.close = lola_pcm_close,
	.hw_params = lola_pcm_hw_params,
	.hw_free = lola_pcm_hw_free,
	.prepare = lola_pcm_prepare,
	.trigger = lola_pcm_trigger,
	.pointer = lola_pcm_pointer,
};

int lola_create_pcm(struct lola *chip)
{
	struct snd_pcm *pcm;
	int i, err;

	for (i = 0; i < 2; i++) {
		err = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV,
					  &chip->pci->dev,
					  PAGE_SIZE, &chip->pcm[i].bdl);
		if (err < 0)
			return err;
	}

	err = snd_pcm_new(chip->card, "Digigram Lola", 0,
			  chip->pcm[SNDRV_PCM_STREAM_PLAYBACK].num_streams,
			  chip->pcm[SNDRV_PCM_STREAM_CAPTURE].num_streams,
			  &pcm);
	if (err < 0)
		return err;
	strlcpy(pcm->name, "Digigram Lola", sizeof(pcm->name));
	pcm->private_data = chip;
	for (i = 0; i < 2; i++) {
		if (chip->pcm[i].num_streams)
			snd_pcm_set_ops(pcm, i, &lola_pcm_ops);
	}
	/* buffer pre-allocation */
	snd_pcm_set_managed_buffer_all(pcm, SNDRV_DMA_TYPE_DEV_SG,
				       &chip->pci->dev,
				       1024 * 64, 32 * 1024 * 1024);
	return 0;
}

void lola_free_pcm(struct lola *chip)
{
	snd_dma_free_pages(&chip->pcm[0].bdl);
	snd_dma_free_pages(&chip->pcm[1].bdl);
}

/*
 */

static int lola_init_stream(struct lola *chip, struct lola_stream *str,
			    int idx, int nid, int dir)
{
	unsigned int val;
	int err;

	str->nid = nid;
	str->index = idx;
	str->dsd = idx;
	if (dir == PLAY)
		str->dsd += MAX_STREAM_IN_COUNT;
	err = lola_read_param(chip, nid, LOLA_PAR_AUDIO_WIDGET_CAP, &val);
	if (err < 0) {
		dev_err(chip->card->dev, "Can't read wcaps for 0x%x\n", nid);
		return err;
	}
	if (dir == PLAY) {
		/* test TYPE and bits 0..11 (no test bit9 : Digital = 0/1) */
		if ((val & 0x00f00dff) != 0x00000010) {
			dev_err(chip->card->dev,
				"Invalid wcaps 0x%x for 0x%x\n",
			       val, nid);
			return -EINVAL;
		}
	} else {
		/* test TYPE and bits 0..11 (no test bit9 : Digital = 0/1)
		 * (bug : ignore bit8: Conn list = 0/1)
		 */
		if ((val & 0x00f00cff) != 0x00100010) {
			dev_err(chip->card->dev,
				"Invalid wcaps 0x%x for 0x%x\n",
			       val, nid);
			return -EINVAL;
		}
		/* test bit9:DIGITAL and bit12:SRC_PRESENT*/
		if ((val & 0x00001200) == 0x00001200)
			chip->input_src_caps_mask |= (1 << idx);
	}

	err = lola_read_param(chip, nid, LOLA_PAR_STREAM_FORMATS, &val);
	if (err < 0) {
		dev_err(chip->card->dev, "Can't read FORMATS 0x%x\n", nid);
		return err;
	}
	val &= 3;
	if (val == 3)
		str->can_float = true;
	if (!(val & 1)) {
		dev_err(chip->card->dev,
			"Invalid formats 0x%x for 0x%x", val, nid);
		return -EINVAL;
	}
	return 0;
}

int lola_init_pcm(struct lola *chip, int dir, int *nidp)
{
	struct lola_pcm *pcm = &chip->pcm[dir];
	int i, nid, err;

	nid = *nidp;
	for (i = 0; i < pcm->num_streams; i++, nid++) {
		err = lola_init_stream(chip, &pcm->streams[i], i, nid, dir);
		if (err < 0)
			return err;
	}
	*nidp = nid;
	return 0;
}
