/*
 * PMac DBDMA lowlevel functions
 *
 * Copyright (c) by Takashi Iwai <tiwai@suse.de>
 * code based on dmasound.c.
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


#include <sound/driver.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <sound/core.h>
#include "pmac.h"
#include <sound/pcm_params.h>
#include <asm/pmac_feature.h>
#include <asm/pci-bridge.h>


/* fixed frequency table for awacs, screamer, burgundy, DACA (44100 max) */
static int awacs_freqs[8] = {
	44100, 29400, 22050, 17640, 14700, 11025, 8820, 7350
};
/* fixed frequency table for tumbler */
static int tumbler_freqs[1] = {
	44100
};

/*
 * allocate DBDMA command arrays
 */
static int snd_pmac_dbdma_alloc(struct snd_pmac *chip, struct pmac_dbdma *rec, int size)
{
	unsigned int rsize = sizeof(struct dbdma_cmd) * (size + 1);

	rec->space = dma_alloc_coherent(&chip->pdev->dev, rsize,
					&rec->dma_base, GFP_KERNEL);
	if (rec->space == NULL)
		return -ENOMEM;
	rec->size = size;
	memset(rec->space, 0, rsize);
	rec->cmds = (void __iomem *)DBDMA_ALIGN(rec->space);
	rec->addr = rec->dma_base + (unsigned long)((char *)rec->cmds - (char *)rec->space);

	return 0;
}

static void snd_pmac_dbdma_free(struct snd_pmac *chip, struct pmac_dbdma *rec)
{
	if (rec->space) {
		unsigned int rsize = sizeof(struct dbdma_cmd) * (rec->size + 1);

		dma_free_coherent(&chip->pdev->dev, rsize, rec->space, rec->dma_base);
	}
}


/*
 * pcm stuff
 */

/*
 * look up frequency table
 */

unsigned int snd_pmac_rate_index(struct snd_pmac *chip, struct pmac_stream *rec, unsigned int rate)
{
	int i, ok, found;

	ok = rec->cur_freqs;
	if (rate > chip->freq_table[0])
		return 0;
	found = 0;
	for (i = 0; i < chip->num_freqs; i++, ok >>= 1) {
		if (! (ok & 1)) continue;
		found = i;
		if (rate >= chip->freq_table[i])
			break;
	}
	return found;
}

/*
 * check whether another stream is active
 */
static inline int another_stream(int stream)
{
	return (stream == SNDRV_PCM_STREAM_PLAYBACK) ?
		SNDRV_PCM_STREAM_CAPTURE : SNDRV_PCM_STREAM_PLAYBACK;
}

/*
 * allocate buffers
 */
static int snd_pmac_pcm_hw_params(struct snd_pcm_substream *subs,
				  struct snd_pcm_hw_params *hw_params)
{
	return snd_pcm_lib_malloc_pages(subs, params_buffer_bytes(hw_params));
}

/*
 * release buffers
 */
static int snd_pmac_pcm_hw_free(struct snd_pcm_substream *subs)
{
	snd_pcm_lib_free_pages(subs);
	return 0;
}

/*
 * get a stream of the opposite direction
 */
static struct pmac_stream *snd_pmac_get_stream(struct snd_pmac *chip, int stream)
{
	switch (stream) {
	case SNDRV_PCM_STREAM_PLAYBACK:
		return &chip->playback;
	case SNDRV_PCM_STREAM_CAPTURE:
		return &chip->capture;
	default:
		snd_BUG();
		return NULL;
	}
}

/*
 * wait while run status is on
 */
static inline void
snd_pmac_wait_ack(struct pmac_stream *rec)
{
	int timeout = 50000;
	while ((in_le32(&rec->dma->status) & RUN) && timeout-- > 0)
		udelay(1);
}

/*
 * set the format and rate to the chip.
 * call the lowlevel function if defined (e.g. for AWACS).
 */
static void snd_pmac_pcm_set_format(struct snd_pmac *chip)
{
	/* set up frequency and format */
	out_le32(&chip->awacs->control, chip->control_mask | (chip->rate_index << 8));
	out_le32(&chip->awacs->byteswap, chip->format == SNDRV_PCM_FORMAT_S16_LE ? 1 : 0);
	if (chip->set_format)
		chip->set_format(chip);
}

/*
 * stop the DMA transfer
 */
static inline void snd_pmac_dma_stop(struct pmac_stream *rec)
{
	out_le32(&rec->dma->control, (RUN|WAKE|FLUSH|PAUSE) << 16);
	snd_pmac_wait_ack(rec);
}

/*
 * set the command pointer address
 */
static inline void snd_pmac_dma_set_command(struct pmac_stream *rec, struct pmac_dbdma *cmd)
{
	out_le32(&rec->dma->cmdptr, cmd->addr);
}

/*
 * start the DMA
 */
static inline void snd_pmac_dma_run(struct pmac_stream *rec, int status)
{
	out_le32(&rec->dma->control, status | (status << 16));
}


/*
 * prepare playback/capture stream
 */
static int snd_pmac_pcm_prepare(struct snd_pmac *chip, struct pmac_stream *rec, struct snd_pcm_substream *subs)
{
	int i;
	volatile struct dbdma_cmd __iomem *cp;
	struct snd_pcm_runtime *runtime = subs->runtime;
	int rate_index;
	long offset;
	struct pmac_stream *astr;
	
	rec->dma_size = snd_pcm_lib_buffer_bytes(subs);
	rec->period_size = snd_pcm_lib_period_bytes(subs);
	rec->nperiods = rec->dma_size / rec->period_size;
	rec->cur_period = 0;
	rate_index = snd_pmac_rate_index(chip, rec, runtime->rate);

	/* set up constraints */
	astr = snd_pmac_get_stream(chip, another_stream(rec->stream));
	if (! astr)
		return -EINVAL;
	astr->cur_freqs = 1 << rate_index;
	astr->cur_formats = 1 << runtime->format;
	chip->rate_index = rate_index;
	chip->format = runtime->format;

	/* We really want to execute a DMA stop command, after the AWACS
	 * is initialized.
	 * For reasons I don't understand, it stops the hissing noise
	 * common to many PowerBook G3 systems and random noise otherwise
	 * captured on iBook2's about every third time. -ReneR
	 */
	spin_lock_irq(&chip->reg_lock);
	snd_pmac_dma_stop(rec);
	st_le16(&chip->extra_dma.cmds->command, DBDMA_STOP);
	snd_pmac_dma_set_command(rec, &chip->extra_dma);
	snd_pmac_dma_run(rec, RUN);
	spin_unlock_irq(&chip->reg_lock);
	mdelay(5);
	spin_lock_irq(&chip->reg_lock);
	/* continuous DMA memory type doesn't provide the physical address,
	 * so we need to resolve the address here...
	 */
	offset = runtime->dma_addr;
	for (i = 0, cp = rec->cmd.cmds; i < rec->nperiods; i++, cp++) {
		st_le32(&cp->phy_addr, offset);
		st_le16(&cp->req_count, rec->period_size);
		/*st_le16(&cp->res_count, 0);*/
		st_le16(&cp->xfer_status, 0);
		offset += rec->period_size;
	}
	/* make loop */
	st_le16(&cp->command, DBDMA_NOP + BR_ALWAYS);
	st_le32(&cp->cmd_dep, rec->cmd.addr);

	snd_pmac_dma_stop(rec);
	snd_pmac_dma_set_command(rec, &rec->cmd);
	spin_unlock_irq(&chip->reg_lock);

	return 0;
}


/*
 * PCM trigger/stop
 */
static int snd_pmac_pcm_trigger(struct snd_pmac *chip, struct pmac_stream *rec,
				struct snd_pcm_substream *subs, int cmd)
{
	volatile struct dbdma_cmd __iomem *cp;
	int i, command;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		if (rec->running)
			return -EBUSY;
		command = (subs->stream == SNDRV_PCM_STREAM_PLAYBACK ?
			   OUTPUT_MORE : INPUT_MORE) + INTR_ALWAYS;
		spin_lock(&chip->reg_lock);
		snd_pmac_beep_stop(chip);
		snd_pmac_pcm_set_format(chip);
		for (i = 0, cp = rec->cmd.cmds; i < rec->nperiods; i++, cp++)
			out_le16(&cp->command, command);
		snd_pmac_dma_set_command(rec, &rec->cmd);
		(void)in_le32(&rec->dma->status);
		snd_pmac_dma_run(rec, RUN|WAKE);
		rec->running = 1;
		spin_unlock(&chip->reg_lock);
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		spin_lock(&chip->reg_lock);
		rec->running = 0;
		/*printk("stopped!!\n");*/
		snd_pmac_dma_stop(rec);
		for (i = 0, cp = rec->cmd.cmds; i < rec->nperiods; i++, cp++)
			out_le16(&cp->command, DBDMA_STOP);
		spin_unlock(&chip->reg_lock);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

/*
 * return the current pointer
 */
inline
static snd_pcm_uframes_t snd_pmac_pcm_pointer(struct snd_pmac *chip,
					      struct pmac_stream *rec,
					      struct snd_pcm_substream *subs)
{
	int count = 0;

#if 1 /* hmm.. how can we get the current dma pointer?? */
	int stat;
	volatile struct dbdma_cmd __iomem *cp = &rec->cmd.cmds[rec->cur_period];
	stat = ld_le16(&cp->xfer_status);
	if (stat & (ACTIVE|DEAD)) {
		count = in_le16(&cp->res_count);
		if (count)
			count = rec->period_size - count;
	}
#endif
	count += rec->cur_period * rec->period_size;
	/*printk("pointer=%d\n", count);*/
	return bytes_to_frames(subs->runtime, count);
}

/*
 * playback
 */

static int snd_pmac_playback_prepare(struct snd_pcm_substream *subs)
{
	struct snd_pmac *chip = snd_pcm_substream_chip(subs);
	return snd_pmac_pcm_prepare(chip, &chip->playback, subs);
}

static int snd_pmac_playback_trigger(struct snd_pcm_substream *subs,
				     int cmd)
{
	struct snd_pmac *chip = snd_pcm_substream_chip(subs);
	return snd_pmac_pcm_trigger(chip, &chip->playback, subs, cmd);
}

static snd_pcm_uframes_t snd_pmac_playback_pointer(struct snd_pcm_substream *subs)
{
	struct snd_pmac *chip = snd_pcm_substream_chip(subs);
	return snd_pmac_pcm_pointer(chip, &chip->playback, subs);
}


/*
 * capture
 */

static int snd_pmac_capture_prepare(struct snd_pcm_substream *subs)
{
	struct snd_pmac *chip = snd_pcm_substream_chip(subs);
	return snd_pmac_pcm_prepare(chip, &chip->capture, subs);
}

static int snd_pmac_capture_trigger(struct snd_pcm_substream *subs,
				    int cmd)
{
	struct snd_pmac *chip = snd_pcm_substream_chip(subs);
	return snd_pmac_pcm_trigger(chip, &chip->capture, subs, cmd);
}

static snd_pcm_uframes_t snd_pmac_capture_pointer(struct snd_pcm_substream *subs)
{
	struct snd_pmac *chip = snd_pcm_substream_chip(subs);
	return snd_pmac_pcm_pointer(chip, &chip->capture, subs);
}


/*
 * update playback/capture pointer from interrupts
 */
static void snd_pmac_pcm_update(struct snd_pmac *chip, struct pmac_stream *rec)
{
	volatile struct dbdma_cmd __iomem *cp;
	int c;
	int stat;

	spin_lock(&chip->reg_lock);
	if (rec->running) {
		cp = &rec->cmd.cmds[rec->cur_period];
		for (c = 0; c < rec->nperiods; c++) { /* at most all fragments */
			stat = ld_le16(&cp->xfer_status);
			if (! (stat & ACTIVE))
				break;
			/*printk("update frag %d\n", rec->cur_period);*/
			st_le16(&cp->xfer_status, 0);
			st_le16(&cp->req_count, rec->period_size);
			/*st_le16(&cp->res_count, 0);*/
			rec->cur_period++;
			if (rec->cur_period >= rec->nperiods) {
				rec->cur_period = 0;
				cp = rec->cmd.cmds;
			} else
				cp++;
			spin_unlock(&chip->reg_lock);
			snd_pcm_period_elapsed(rec->substream);
			spin_lock(&chip->reg_lock);
		}
	}
	spin_unlock(&chip->reg_lock);
}


/*
 * hw info
 */

static struct snd_pcm_hardware snd_pmac_playback =
{
	.info =			(SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_MMAP |
				 SNDRV_PCM_INFO_MMAP_VALID |
				 SNDRV_PCM_INFO_RESUME),
	.formats =		SNDRV_PCM_FMTBIT_S16_BE | SNDRV_PCM_FMTBIT_S16_LE,
	.rates =		SNDRV_PCM_RATE_8000_44100,
	.rate_min =		7350,
	.rate_max =		44100,
	.channels_min =		2,
	.channels_max =		2,
	.buffer_bytes_max =	131072,
	.period_bytes_min =	256,
	.period_bytes_max =	16384,
	.periods_min =		3,
	.periods_max =		PMAC_MAX_FRAGS,
};

static struct snd_pcm_hardware snd_pmac_capture =
{
	.info =			(SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_MMAP |
				 SNDRV_PCM_INFO_MMAP_VALID |
				 SNDRV_PCM_INFO_RESUME),
	.formats =		SNDRV_PCM_FMTBIT_S16_BE | SNDRV_PCM_FMTBIT_S16_LE,
	.rates =		SNDRV_PCM_RATE_8000_44100,
	.rate_min =		7350,
	.rate_max =		44100,
	.channels_min =		2,
	.channels_max =		2,
	.buffer_bytes_max =	131072,
	.period_bytes_min =	256,
	.period_bytes_max =	16384,
	.periods_min =		3,
	.periods_max =		PMAC_MAX_FRAGS,
};


#if 0 // NYI
static int snd_pmac_hw_rule_rate(struct snd_pcm_hw_params *params,
				 struct snd_pcm_hw_rule *rule)
{
	struct snd_pmac *chip = rule->private;
	struct pmac_stream *rec = snd_pmac_get_stream(chip, rule->deps[0]);
	int i, freq_table[8], num_freqs;

	if (! rec)
		return -EINVAL;
	num_freqs = 0;
	for (i = chip->num_freqs - 1; i >= 0; i--) {
		if (rec->cur_freqs & (1 << i))
			freq_table[num_freqs++] = chip->freq_table[i];
	}

	return snd_interval_list(hw_param_interval(params, rule->var),
				 num_freqs, freq_table, 0);
}

static int snd_pmac_hw_rule_format(struct snd_pcm_hw_params *params,
				   struct snd_pcm_hw_rule *rule)
{
	struct snd_pmac *chip = rule->private;
	struct pmac_stream *rec = snd_pmac_get_stream(chip, rule->deps[0]);

	if (! rec)
		return -EINVAL;
	return snd_mask_refine_set(hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT),
				   rec->cur_formats);
}
#endif // NYI

static int snd_pmac_pcm_open(struct snd_pmac *chip, struct pmac_stream *rec,
			     struct snd_pcm_substream *subs)
{
	struct snd_pcm_runtime *runtime = subs->runtime;
	int i, j, fflags;
	static int typical_freqs[] = {
		44100,
		22050,
		11025,
		0,
	};
	static int typical_freq_flags[] = {
		SNDRV_PCM_RATE_44100,
		SNDRV_PCM_RATE_22050,
		SNDRV_PCM_RATE_11025,
		0,
	};

	/* look up frequency table and fill bit mask */
	runtime->hw.rates = 0;
	fflags = chip->freqs_ok;
	for (i = 0; typical_freqs[i]; i++) {
		for (j = 0; j < chip->num_freqs; j++) {
			if ((chip->freqs_ok & (1 << j)) &&
			    chip->freq_table[j] == typical_freqs[i]) {
				runtime->hw.rates |= typical_freq_flags[i];
				fflags &= ~(1 << j);
				break;
			}
		}
	}
	if (fflags) /* rest */
		runtime->hw.rates |= SNDRV_PCM_RATE_KNOT;

	/* check for minimum and maximum rates */
	for (i = 0; i < chip->num_freqs; i++) {
		if (chip->freqs_ok & (1 << i)) {
			runtime->hw.rate_max = chip->freq_table[i];
			break;
		}
	}
	for (i = chip->num_freqs - 1; i >= 0; i--) {
		if (chip->freqs_ok & (1 << i)) {
			runtime->hw.rate_min = chip->freq_table[i];
			break;
		}
	}
	runtime->hw.formats = chip->formats_ok;
	if (chip->can_capture) {
		if (! chip->can_duplex)
			runtime->hw.info |= SNDRV_PCM_INFO_HALF_DUPLEX;
		runtime->hw.info |= SNDRV_PCM_INFO_JOINT_DUPLEX;
	}
	runtime->private_data = rec;
	rec->substream = subs;

#if 0 /* FIXME: still under development.. */
	snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
			    snd_pmac_hw_rule_rate, chip, rec->stream, -1);
	snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_FORMAT,
			    snd_pmac_hw_rule_format, chip, rec->stream, -1);
#endif

	runtime->hw.periods_max = rec->cmd.size - 1;

	if (chip->can_duplex)
		snd_pcm_set_sync(subs);

	/* constraints to fix choppy sound */
	snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS);
	return 0;
}

static int snd_pmac_pcm_close(struct snd_pmac *chip, struct pmac_stream *rec,
			      struct snd_pcm_substream *subs)
{
	struct pmac_stream *astr;

	snd_pmac_dma_stop(rec);

	astr = snd_pmac_get_stream(chip, another_stream(rec->stream));
	if (! astr)
		return -EINVAL;

	/* reset constraints */
	astr->cur_freqs = chip->freqs_ok;
	astr->cur_formats = chip->formats_ok;
	
	return 0;
}

static int snd_pmac_playback_open(struct snd_pcm_substream *subs)
{
	struct snd_pmac *chip = snd_pcm_substream_chip(subs);

	subs->runtime->hw = snd_pmac_playback;
	return snd_pmac_pcm_open(chip, &chip->playback, subs);
}

static int snd_pmac_capture_open(struct snd_pcm_substream *subs)
{
	struct snd_pmac *chip = snd_pcm_substream_chip(subs);

	subs->runtime->hw = snd_pmac_capture;
	return snd_pmac_pcm_open(chip, &chip->capture, subs);
}

static int snd_pmac_playback_close(struct snd_pcm_substream *subs)
{
	struct snd_pmac *chip = snd_pcm_substream_chip(subs);

	return snd_pmac_pcm_close(chip, &chip->playback, subs);
}

static int snd_pmac_capture_close(struct snd_pcm_substream *subs)
{
	struct snd_pmac *chip = snd_pcm_substream_chip(subs);

	return snd_pmac_pcm_close(chip, &chip->capture, subs);
}

/*
 */

static struct snd_pcm_ops snd_pmac_playback_ops = {
	.open =		snd_pmac_playback_open,
	.close =	snd_pmac_playback_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_pmac_pcm_hw_params,
	.hw_free =	snd_pmac_pcm_hw_free,
	.prepare =	snd_pmac_playback_prepare,
	.trigger =	snd_pmac_playback_trigger,
	.pointer =	snd_pmac_playback_pointer,
};

static struct snd_pcm_ops snd_pmac_capture_ops = {
	.open =		snd_pmac_capture_open,
	.close =	snd_pmac_capture_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_pmac_pcm_hw_params,
	.hw_free =	snd_pmac_pcm_hw_free,
	.prepare =	snd_pmac_capture_prepare,
	.trigger =	snd_pmac_capture_trigger,
	.pointer =	snd_pmac_capture_pointer,
};

int __init snd_pmac_pcm_new(struct snd_pmac *chip)
{
	struct snd_pcm *pcm;
	int err;
	int num_captures = 1;

	if (! chip->can_capture)
		num_captures = 0;
	err = snd_pcm_new(chip->card, chip->card->driver, 0, 1, num_captures, &pcm);
	if (err < 0)
		return err;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_pmac_playback_ops);
	if (chip->can_capture)
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_pmac_capture_ops);

	pcm->private_data = chip;
	pcm->info_flags = SNDRV_PCM_INFO_JOINT_DUPLEX;
	strcpy(pcm->name, chip->card->shortname);
	chip->pcm = pcm;

	chip->formats_ok = SNDRV_PCM_FMTBIT_S16_BE;
	if (chip->can_byte_swap)
		chip->formats_ok |= SNDRV_PCM_FMTBIT_S16_LE;

	chip->playback.cur_formats = chip->formats_ok;
	chip->capture.cur_formats = chip->formats_ok;
	chip->playback.cur_freqs = chip->freqs_ok;
	chip->capture.cur_freqs = chip->freqs_ok;

	/* preallocate 64k buffer */
	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
					      &chip->pdev->dev,
					      64 * 1024, 64 * 1024);

	return 0;
}


static void snd_pmac_dbdma_reset(struct snd_pmac *chip)
{
	out_le32(&chip->playback.dma->control, (RUN|PAUSE|FLUSH|WAKE|DEAD) << 16);
	snd_pmac_wait_ack(&chip->playback);
	out_le32(&chip->capture.dma->control, (RUN|PAUSE|FLUSH|WAKE|DEAD) << 16);
	snd_pmac_wait_ack(&chip->capture);
}


/*
 * handling beep
 */
void snd_pmac_beep_dma_start(struct snd_pmac *chip, int bytes, unsigned long addr, int speed)
{
	struct pmac_stream *rec = &chip->playback;

	snd_pmac_dma_stop(rec);
	st_le16(&chip->extra_dma.cmds->req_count, bytes);
	st_le16(&chip->extra_dma.cmds->xfer_status, 0);
	st_le32(&chip->extra_dma.cmds->cmd_dep, chip->extra_dma.addr);
	st_le32(&chip->extra_dma.cmds->phy_addr, addr);
	st_le16(&chip->extra_dma.cmds->command, OUTPUT_MORE + BR_ALWAYS);
	out_le32(&chip->awacs->control,
		 (in_le32(&chip->awacs->control) & ~0x1f00)
		 | (speed << 8));
	out_le32(&chip->awacs->byteswap, 0);
	snd_pmac_dma_set_command(rec, &chip->extra_dma);
	snd_pmac_dma_run(rec, RUN);
}

void snd_pmac_beep_dma_stop(struct snd_pmac *chip)
{
	snd_pmac_dma_stop(&chip->playback);
	st_le16(&chip->extra_dma.cmds->command, DBDMA_STOP);
	snd_pmac_pcm_set_format(chip); /* reset format */
}


/*
 * interrupt handlers
 */
static irqreturn_t
snd_pmac_tx_intr(int irq, void *devid)
{
	struct snd_pmac *chip = devid;
	snd_pmac_pcm_update(chip, &chip->playback);
	return IRQ_HANDLED;
}


static irqreturn_t
snd_pmac_rx_intr(int irq, void *devid)
{
	struct snd_pmac *chip = devid;
	snd_pmac_pcm_update(chip, &chip->capture);
	return IRQ_HANDLED;
}


static irqreturn_t
snd_pmac_ctrl_intr(int irq, void *devid)
{
	struct snd_pmac *chip = devid;
	int ctrl = in_le32(&chip->awacs->control);

	/*printk("pmac: control interrupt.. 0x%x\n", ctrl);*/
	if (ctrl & MASK_PORTCHG) {
		/* do something when headphone is plugged/unplugged? */
		if (chip->update_automute)
			chip->update_automute(chip, 1);
	}
	if (ctrl & MASK_CNTLERR) {
		int err = (in_le32(&chip->awacs->codec_stat) & MASK_ERRCODE) >> 16;
		if (err && chip->model <= PMAC_SCREAMER)
			snd_printk(KERN_DEBUG "error %x\n", err);
	}
	/* Writing 1s to the CNTLERR and PORTCHG bits clears them... */
	out_le32(&chip->awacs->control, ctrl);
	return IRQ_HANDLED;
}


/*
 * a wrapper to feature call for compatibility
 */
static void snd_pmac_sound_feature(struct snd_pmac *chip, int enable)
{
	if (ppc_md.feature_call)
		ppc_md.feature_call(PMAC_FTR_SOUND_CHIP_ENABLE, chip->node, 0, enable);
}

/*
 * release resources
 */

static int snd_pmac_free(struct snd_pmac *chip)
{
	/* stop sounds */
	if (chip->initialized) {
		snd_pmac_dbdma_reset(chip);
		/* disable interrupts from awacs interface */
		out_le32(&chip->awacs->control, in_le32(&chip->awacs->control) & 0xfff);
	}

	snd_pmac_sound_feature(chip, 0);

	/* clean up mixer if any */
	if (chip->mixer_free)
		chip->mixer_free(chip);

	snd_pmac_detach_beep(chip);

	/* release resources */
	if (chip->irq >= 0)
		free_irq(chip->irq, (void*)chip);
	if (chip->tx_irq >= 0)
		free_irq(chip->tx_irq, (void*)chip);
	if (chip->rx_irq >= 0)
		free_irq(chip->rx_irq, (void*)chip);
	snd_pmac_dbdma_free(chip, &chip->playback.cmd);
	snd_pmac_dbdma_free(chip, &chip->capture.cmd);
	snd_pmac_dbdma_free(chip, &chip->extra_dma);
	if (chip->macio_base)
		iounmap(chip->macio_base);
	if (chip->latch_base)
		iounmap(chip->latch_base);
	if (chip->awacs)
		iounmap(chip->awacs);
	if (chip->playback.dma)
		iounmap(chip->playback.dma);
	if (chip->capture.dma)
		iounmap(chip->capture.dma);

	if (chip->node) {
		int i;
		for (i = 0; i < 3; i++) {
			if (chip->requested & (1 << i))
				release_mem_region(chip->rsrc[i].start,
						   chip->rsrc[i].end -
						   chip->rsrc[i].start + 1);
		}
	}

	if (chip->pdev)
		pci_dev_put(chip->pdev);
	kfree(chip);
	return 0;
}


/*
 * free the device
 */
static int snd_pmac_dev_free(struct snd_device *device)
{
	struct snd_pmac *chip = device->device_data;
	return snd_pmac_free(chip);
}


/*
 * check the machine support byteswap (little-endian)
 */

static void __init detect_byte_swap(struct snd_pmac *chip)
{
	struct device_node *mio;

	/* if seems that Keylargo can't byte-swap  */
	for (mio = chip->node->parent; mio; mio = mio->parent) {
		if (strcmp(mio->name, "mac-io") == 0) {
			if (device_is_compatible(mio, "Keylargo"))
				chip->can_byte_swap = 0;
			break;
		}
	}

	/* it seems the Pismo & iBook can't byte-swap in hardware. */
	if (machine_is_compatible("PowerBook3,1") ||
	    machine_is_compatible("PowerBook2,1"))
		chip->can_byte_swap = 0 ;

	if (machine_is_compatible("PowerBook2,1"))
		chip->can_duplex = 0;
}


/*
 * detect a sound chip
 */
static int __init snd_pmac_detect(struct snd_pmac *chip)
{
	struct device_node *sound = NULL;
	unsigned int *prop, l;
	struct macio_chip* macio;

	if (!machine_is(powermac))
		return -ENODEV;

	chip->subframe = 0;
	chip->revision = 0;
	chip->freqs_ok = 0xff; /* all ok */
	chip->model = PMAC_AWACS;
	chip->can_byte_swap = 1;
	chip->can_duplex = 1;
	chip->can_capture = 1;
	chip->num_freqs = ARRAY_SIZE(awacs_freqs);
	chip->freq_table = awacs_freqs;
	chip->pdev = NULL;

	chip->control_mask = MASK_IEPC | MASK_IEE | 0x11; /* default */

	/* check machine type */
	if (machine_is_compatible("AAPL,3400/2400")
	    || machine_is_compatible("AAPL,3500"))
		chip->is_pbook_3400 = 1;
	else if (machine_is_compatible("PowerBook1,1")
		 || machine_is_compatible("AAPL,PowerBook1998"))
		chip->is_pbook_G3 = 1;
	chip->node = find_devices("awacs");
	if (chip->node)
		sound = chip->node;

	/*
	 * powermac G3 models have a node called "davbus"
	 * with a child called "sound".
	 */
	if (!chip->node)
		chip->node = find_devices("davbus");
	/*
	 * if we didn't find a davbus device, try 'i2s-a' since
	 * this seems to be what iBooks have
	 */
	if (! chip->node) {
		chip->node = find_devices("i2s-a");
		if (chip->node && chip->node->parent &&
		    chip->node->parent->parent) {
			if (device_is_compatible(chip->node->parent->parent,
						 "K2-Keylargo"))
				chip->is_k2 = 1;
		}
	}
	if (! chip->node)
		return -ENODEV;

	if (!sound) {
		sound = find_devices("sound");
		while (sound && sound->parent != chip->node)
			sound = sound->next;
	}
	if (! sound)
		return -ENODEV;
	prop = (unsigned int *) get_property(sound, "sub-frame", NULL);
	if (prop && *prop < 16)
		chip->subframe = *prop;
	prop = (unsigned int *) get_property(sound, "layout-id", NULL);
	if (prop) {
		/* partly deprecate snd-powermac, for those machines
		 * that have a layout-id property for now */
		printk(KERN_INFO "snd-powermac no longer handles any "
				 "machines with a layout-id property "
				 "in the device-tree, use snd-aoa.\n");
		return -ENODEV;
	}
	/* This should be verified on older screamers */
	if (device_is_compatible(sound, "screamer")) {
		chip->model = PMAC_SCREAMER;
		// chip->can_byte_swap = 0; /* FIXME: check this */
	}
	if (device_is_compatible(sound, "burgundy")) {
		chip->model = PMAC_BURGUNDY;
		chip->control_mask = MASK_IEPC | 0x11; /* disable IEE */
	}
	if (device_is_compatible(sound, "daca")) {
		chip->model = PMAC_DACA;
		chip->can_capture = 0;  /* no capture */
		chip->can_duplex = 0;
		// chip->can_byte_swap = 0; /* FIXME: check this */
		chip->control_mask = MASK_IEPC | 0x11; /* disable IEE */
	}
	if (device_is_compatible(sound, "tumbler")) {
		chip->model = PMAC_TUMBLER;
		chip->can_capture = 0;  /* no capture */
		chip->can_duplex = 0;
		// chip->can_byte_swap = 0; /* FIXME: check this */
		chip->num_freqs = ARRAY_SIZE(tumbler_freqs);
		chip->freq_table = tumbler_freqs;
		chip->control_mask = MASK_IEPC | 0x11; /* disable IEE */
	}
	if (device_is_compatible(sound, "snapper")) {
		chip->model = PMAC_SNAPPER;
		// chip->can_byte_swap = 0; /* FIXME: check this */
		chip->num_freqs = ARRAY_SIZE(tumbler_freqs);
		chip->freq_table = tumbler_freqs;
		chip->control_mask = MASK_IEPC | 0x11; /* disable IEE */
	}
	prop = (unsigned int *)get_property(sound, "device-id", NULL);
	if (prop)
		chip->device_id = *prop;
	chip->has_iic = (find_devices("perch") != NULL);

	/* We need the PCI device for DMA allocations, let's use a crude method
	 * for now ...
	 */
	macio = macio_find(chip->node, macio_unknown);
	if (macio == NULL)
		printk(KERN_WARNING "snd-powermac: can't locate macio !\n");
	else {
		struct pci_dev *pdev = NULL;

		for_each_pci_dev(pdev) {
			struct device_node *np = pci_device_to_OF_node(pdev);
			if (np && np == macio->of_node) {
				chip->pdev = pdev;
				break;
			}
		}
	}
	if (chip->pdev == NULL)
		printk(KERN_WARNING "snd-powermac: can't locate macio PCI"
		       " device !\n");

	detect_byte_swap(chip);

	/* look for a property saying what sample rates
	   are available */
	prop = (unsigned int *) get_property(sound, "sample-rates", &l);
	if (! prop)
		prop = (unsigned int *) get_property(sound,
						     "output-frame-rates", &l);
	if (prop) {
		int i;
		chip->freqs_ok = 0;
		for (l /= sizeof(int); l > 0; --l) {
			unsigned int r = *prop++;
			/* Apple 'Fixed' format */
			if (r >= 0x10000)
				r >>= 16;
			for (i = 0; i < chip->num_freqs; ++i) {
				if (r == chip->freq_table[i]) {
					chip->freqs_ok |= (1 << i);
					break;
				}
			}
		}
	} else {
		/* assume only 44.1khz */
		chip->freqs_ok = 1;
	}

	return 0;
}

/*
 * exported - boolean info callbacks for ease of programming
 */
int snd_pmac_boolean_stereo_info(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

int snd_pmac_boolean_mono_info(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

#ifdef PMAC_SUPPORT_AUTOMUTE
/*
 * auto-mute
 */
static int pmac_auto_mute_get(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_pmac *chip = snd_kcontrol_chip(kcontrol);
	ucontrol->value.integer.value[0] = chip->auto_mute;
	return 0;
}

static int pmac_auto_mute_put(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_pmac *chip = snd_kcontrol_chip(kcontrol);
	if (ucontrol->value.integer.value[0] != chip->auto_mute) {
		chip->auto_mute = ucontrol->value.integer.value[0];
		if (chip->update_automute)
			chip->update_automute(chip, 1);
		return 1;
	}
	return 0;
}

static int pmac_hp_detect_get(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_pmac *chip = snd_kcontrol_chip(kcontrol);
	if (chip->detect_headphone)
		ucontrol->value.integer.value[0] = chip->detect_headphone(chip);
	else
		ucontrol->value.integer.value[0] = 0;
	return 0;
}

static struct snd_kcontrol_new auto_mute_controls[] __initdata = {
	{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	  .name = "Auto Mute Switch",
	  .info = snd_pmac_boolean_mono_info,
	  .get = pmac_auto_mute_get,
	  .put = pmac_auto_mute_put,
	},
	{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	  .name = "Headphone Detection",
	  .access = SNDRV_CTL_ELEM_ACCESS_READ,
	  .info = snd_pmac_boolean_mono_info,
	  .get = pmac_hp_detect_get,
	},
};

int __init snd_pmac_add_automute(struct snd_pmac *chip)
{
	int err;
	chip->auto_mute = 1;
	err = snd_ctl_add(chip->card, snd_ctl_new1(&auto_mute_controls[0], chip));
	if (err < 0) {
		printk(KERN_ERR "snd-powermac: Failed to add automute control\n");
		return err;
	}
	chip->hp_detect_ctl = snd_ctl_new1(&auto_mute_controls[1], chip);
	return snd_ctl_add(chip->card, chip->hp_detect_ctl);
}
#endif /* PMAC_SUPPORT_AUTOMUTE */

/*
 * create and detect a pmac chip record
 */
int __init snd_pmac_new(struct snd_card *card, struct snd_pmac **chip_return)
{
	struct snd_pmac *chip;
	struct device_node *np;
	int i, err;
	unsigned int irq;
	unsigned long ctrl_addr, txdma_addr, rxdma_addr;
	static struct snd_device_ops ops = {
		.dev_free =	snd_pmac_dev_free,
	};

	*chip_return = NULL;

	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (chip == NULL)
		return -ENOMEM;
	chip->card = card;

	spin_lock_init(&chip->reg_lock);
	chip->irq = chip->tx_irq = chip->rx_irq = -1;

	chip->playback.stream = SNDRV_PCM_STREAM_PLAYBACK;
	chip->capture.stream = SNDRV_PCM_STREAM_CAPTURE;

	if ((err = snd_pmac_detect(chip)) < 0)
		goto __error;

	if (snd_pmac_dbdma_alloc(chip, &chip->playback.cmd, PMAC_MAX_FRAGS + 1) < 0 ||
	    snd_pmac_dbdma_alloc(chip, &chip->capture.cmd, PMAC_MAX_FRAGS + 1) < 0 ||
	    snd_pmac_dbdma_alloc(chip, &chip->extra_dma, 2) < 0) {
		err = -ENOMEM;
		goto __error;
	}

	np = chip->node;
	chip->requested = 0;
	if (chip->is_k2) {
		static char *rnames[] = {
			"Sound Control", "Sound DMA" };
		for (i = 0; i < 2; i ++) {
			if (of_address_to_resource(np->parent, i,
						   &chip->rsrc[i])) {
				printk(KERN_ERR "snd: can't translate rsrc "
				       " %d (%s)\n", i, rnames[i]);
				err = -ENODEV;
				goto __error;
			}
			if (request_mem_region(chip->rsrc[i].start,
					       chip->rsrc[i].end -
					       chip->rsrc[i].start + 1,
					       rnames[i]) == NULL) {
				printk(KERN_ERR "snd: can't request rsrc "
				       " %d (%s: 0x%016llx:%016llx)\n",
				       i, rnames[i],
				       (unsigned long long)chip->rsrc[i].start,
				       (unsigned long long)chip->rsrc[i].end);
				err = -ENODEV;
				goto __error;
			}
			chip->requested |= (1 << i);
		}
		ctrl_addr = chip->rsrc[0].start;
		txdma_addr = chip->rsrc[1].start;
		rxdma_addr = txdma_addr + 0x100;
	} else {
		static char *rnames[] = {
			"Sound Control", "Sound Tx DMA", "Sound Rx DMA" };
		for (i = 0; i < 3; i ++) {
			if (of_address_to_resource(np, i,
						   &chip->rsrc[i])) {
				printk(KERN_ERR "snd: can't translate rsrc "
				       " %d (%s)\n", i, rnames[i]);
				err = -ENODEV;
				goto __error;
			}
			if (request_mem_region(chip->rsrc[i].start,
					       chip->rsrc[i].end -
					       chip->rsrc[i].start + 1,
					       rnames[i]) == NULL) {
				printk(KERN_ERR "snd: can't request rsrc "
				       " %d (%s: 0x%016llx:%016llx)\n",
				       i, rnames[i],
				       (unsigned long long)chip->rsrc[i].start,
				       (unsigned long long)chip->rsrc[i].end);
				err = -ENODEV;
				goto __error;
			}
			chip->requested |= (1 << i);
		}
		ctrl_addr = chip->rsrc[0].start;
		txdma_addr = chip->rsrc[1].start;
		rxdma_addr = chip->rsrc[2].start;
	}

	chip->awacs = ioremap(ctrl_addr, 0x1000);
	chip->playback.dma = ioremap(txdma_addr, 0x100);
	chip->capture.dma = ioremap(rxdma_addr, 0x100);
	if (chip->model <= PMAC_BURGUNDY) {
		irq = irq_of_parse_and_map(np, 0);
		if (request_irq(irq, snd_pmac_ctrl_intr, 0,
				"PMac", (void*)chip)) {
			snd_printk(KERN_ERR "pmac: unable to grab IRQ %d\n",
				   irq);
			err = -EBUSY;
			goto __error;
		}
		chip->irq = irq;
	}
	irq = irq_of_parse_and_map(np, 1);
	if (request_irq(irq, snd_pmac_tx_intr, 0, "PMac Output", (void*)chip)){
		snd_printk(KERN_ERR "pmac: unable to grab IRQ %d\n", irq);
		err = -EBUSY;
		goto __error;
	}
	chip->tx_irq = irq;
	irq = irq_of_parse_and_map(np, 2);
	if (request_irq(irq, snd_pmac_rx_intr, 0, "PMac Input", (void*)chip)) {
		snd_printk(KERN_ERR "pmac: unable to grab IRQ %d\n", irq);
		err = -EBUSY;
		goto __error;
	}
	chip->rx_irq = irq;

	snd_pmac_sound_feature(chip, 1);

	/* reset */
	if (chip->model == PMAC_AWACS)
		out_le32(&chip->awacs->control, 0x11);

	/* Powerbooks have odd ways of enabling inputs such as
	   an expansion-bay CD or sound from an internal modem
	   or a PC-card modem. */
	if (chip->is_pbook_3400) {
		/* Enable CD and PC-card sound inputs. */
		/* This is done by reading from address
		 * f301a000, + 0x10 to enable the expansion-bay
		 * CD sound input, + 0x80 to enable the PC-card
		 * sound input.  The 0x100 enables the SCSI bus
		 * terminator power.
		 */
		chip->latch_base = ioremap (0xf301a000, 0x1000);
		in_8(chip->latch_base + 0x190);
	} else if (chip->is_pbook_G3) {
		struct device_node* mio;
		for (mio = chip->node->parent; mio; mio = mio->parent) {
			if (strcmp(mio->name, "mac-io") == 0) {
				struct resource r;
				if (of_address_to_resource(mio, 0, &r) == 0)
					chip->macio_base =
						ioremap(r.start, 0x40);
				break;
			}
		}
		/* Enable CD sound input. */
		/* The relevant bits for writing to this byte are 0x8f.
		 * I haven't found out what the 0x80 bit does.
		 * For the 0xf bits, writing 3 or 7 enables the CD
		 * input, any other value disables it.  Values
		 * 1, 3, 5, 7 enable the microphone.  Values 0, 2,
		 * 4, 6, 8 - f enable the input from the modem.
		 */
		if (chip->macio_base)
			out_8(chip->macio_base + 0x37, 3);
	}

	/* Reset dbdma channels */
	snd_pmac_dbdma_reset(chip);

	if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops)) < 0)
		goto __error;

	*chip_return = chip;
	return 0;

 __error:
	if (chip->pdev)
		pci_dev_put(chip->pdev);
	snd_pmac_free(chip);
	return err;
}


/*
 * sleep notify for powerbook
 */

#ifdef CONFIG_PM

/*
 * Save state when going to sleep, restore it afterwards.
 */

void snd_pmac_suspend(struct snd_pmac *chip)
{
	unsigned long flags;

	snd_power_change_state(chip->card, SNDRV_CTL_POWER_D3hot);
	if (chip->suspend)
		chip->suspend(chip);
	snd_pcm_suspend_all(chip->pcm);
	spin_lock_irqsave(&chip->reg_lock, flags);
	snd_pmac_beep_stop(chip);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	if (chip->irq >= 0)
		disable_irq(chip->irq);
	if (chip->tx_irq >= 0)
		disable_irq(chip->tx_irq);
	if (chip->rx_irq >= 0)
		disable_irq(chip->rx_irq);
	snd_pmac_sound_feature(chip, 0);
}

void snd_pmac_resume(struct snd_pmac *chip)
{
	snd_pmac_sound_feature(chip, 1);
	if (chip->resume)
		chip->resume(chip);
	/* enable CD sound input */
	if (chip->macio_base && chip->is_pbook_G3)
		out_8(chip->macio_base + 0x37, 3);
	else if (chip->is_pbook_3400)
		in_8(chip->latch_base + 0x190);

	snd_pmac_pcm_set_format(chip);

	if (chip->irq >= 0)
		enable_irq(chip->irq);
	if (chip->tx_irq >= 0)
		enable_irq(chip->tx_irq);
	if (chip->rx_irq >= 0)
		enable_irq(chip->rx_irq);

	snd_power_change_state(chip->card, SNDRV_CTL_POWER_D0);
}

#endif /* CONFIG_PM */

