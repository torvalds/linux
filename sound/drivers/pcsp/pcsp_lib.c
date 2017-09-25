/*
 * PC-Speaker driver for Linux
 *
 * Copyright (C) 1993-1997  Michael Beck
 * Copyright (C) 1997-2001  David Woodhouse
 * Copyright (C) 2001-2008  Stas Sergeev
 */

#include <linux/module.h>
#include <linux/gfp.h>
#include <linux/moduleparam.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <sound/pcm.h>
#include "pcsp.h"

static bool nforce_wa;
module_param(nforce_wa, bool, 0444);
MODULE_PARM_DESC(nforce_wa, "Apply NForce chipset workaround "
		"(expect bad sound)");

#define DMIX_WANTS_S16	1

/*
 * Call snd_pcm_period_elapsed in a tasklet
 * This avoids spinlock messes and long-running irq contexts
 */
static void pcsp_call_pcm_elapsed(unsigned long priv)
{
	if (atomic_read(&pcsp_chip.timer_active)) {
		struct snd_pcm_substream *substream;
		substream = pcsp_chip.playback_substream;
		if (substream)
			snd_pcm_period_elapsed(substream);
	}
}

static DECLARE_TASKLET(pcsp_pcm_tasklet, pcsp_call_pcm_elapsed, 0);

/* write the port and returns the next expire time in ns;
 * called at the trigger-start and in hrtimer callback
 */
static u64 pcsp_timer_update(struct snd_pcsp *chip)
{
	unsigned char timer_cnt, val;
	u64 ns;
	struct snd_pcm_substream *substream;
	struct snd_pcm_runtime *runtime;
	unsigned long flags;

	if (chip->thalf) {
		outb(chip->val61, 0x61);
		chip->thalf = 0;
		return chip->ns_rem;
	}

	substream = chip->playback_substream;
	if (!substream)
		return 0;

	runtime = substream->runtime;
	/* assume it is mono! */
	val = runtime->dma_area[chip->playback_ptr + chip->fmt_size - 1];
	if (chip->is_signed)
		val ^= 0x80;
	timer_cnt = val * CUR_DIV() / 256;

	if (timer_cnt && chip->enable) {
		raw_spin_lock_irqsave(&i8253_lock, flags);
		if (!nforce_wa) {
			outb_p(chip->val61, 0x61);
			outb_p(timer_cnt, 0x42);
			outb(chip->val61 ^ 1, 0x61);
		} else {
			outb(chip->val61 ^ 2, 0x61);
			chip->thalf = 1;
		}
		raw_spin_unlock_irqrestore(&i8253_lock, flags);
	}

	chip->ns_rem = PCSP_PERIOD_NS();
	ns = (chip->thalf ? PCSP_CALC_NS(timer_cnt) : chip->ns_rem);
	chip->ns_rem -= ns;
	return ns;
}

static void pcsp_pointer_update(struct snd_pcsp *chip)
{
	struct snd_pcm_substream *substream;
	size_t period_bytes, buffer_bytes;
	int periods_elapsed;
	unsigned long flags;

	/* update the playback position */
	substream = chip->playback_substream;
	if (!substream)
		return;

	period_bytes = snd_pcm_lib_period_bytes(substream);
	buffer_bytes = snd_pcm_lib_buffer_bytes(substream);

	spin_lock_irqsave(&chip->substream_lock, flags);
	chip->playback_ptr += PCSP_INDEX_INC() * chip->fmt_size;
	periods_elapsed = chip->playback_ptr - chip->period_ptr;
	if (periods_elapsed < 0) {
#if PCSP_DEBUG
		printk(KERN_INFO "PCSP: buffer_bytes mod period_bytes != 0 ? "
			"(%zi %zi %zi)\n",
			chip->playback_ptr, period_bytes, buffer_bytes);
#endif
		periods_elapsed += buffer_bytes;
	}
	periods_elapsed /= period_bytes;
	/* wrap the pointer _before_ calling snd_pcm_period_elapsed(),
	 * or ALSA will BUG on us. */
	chip->playback_ptr %= buffer_bytes;

	if (periods_elapsed) {
		chip->period_ptr += periods_elapsed * period_bytes;
		chip->period_ptr %= buffer_bytes;
	}
	spin_unlock_irqrestore(&chip->substream_lock, flags);

	if (periods_elapsed)
		tasklet_schedule(&pcsp_pcm_tasklet);
}

enum hrtimer_restart pcsp_do_timer(struct hrtimer *handle)
{
	struct snd_pcsp *chip = container_of(handle, struct snd_pcsp, timer);
	int pointer_update;
	u64 ns;

	if (!atomic_read(&chip->timer_active) || !chip->playback_substream)
		return HRTIMER_NORESTART;

	pointer_update = !chip->thalf;
	ns = pcsp_timer_update(chip);
	if (!ns) {
		printk(KERN_WARNING "PCSP: unexpected stop\n");
		return HRTIMER_NORESTART;
	}

	if (pointer_update)
		pcsp_pointer_update(chip);

	hrtimer_forward(handle, hrtimer_get_expires(handle), ns_to_ktime(ns));

	return HRTIMER_RESTART;
}

static int pcsp_start_playing(struct snd_pcsp *chip)
{
#if PCSP_DEBUG
	printk(KERN_INFO "PCSP: start_playing called\n");
#endif
	if (atomic_read(&chip->timer_active)) {
		printk(KERN_ERR "PCSP: Timer already active\n");
		return -EIO;
	}

	raw_spin_lock(&i8253_lock);
	chip->val61 = inb(0x61) | 0x03;
	outb_p(0x92, 0x43);	/* binary, mode 1, LSB only, ch 2 */
	raw_spin_unlock(&i8253_lock);
	atomic_set(&chip->timer_active, 1);
	chip->thalf = 0;

	hrtimer_start(&pcsp_chip.timer, 0, HRTIMER_MODE_REL);
	return 0;
}

static void pcsp_stop_playing(struct snd_pcsp *chip)
{
#if PCSP_DEBUG
	printk(KERN_INFO "PCSP: stop_playing called\n");
#endif
	if (!atomic_read(&chip->timer_active))
		return;

	atomic_set(&chip->timer_active, 0);
	raw_spin_lock(&i8253_lock);
	/* restore the timer */
	outb_p(0xb6, 0x43);	/* binary, mode 3, LSB/MSB, ch 2 */
	outb(chip->val61 & 0xFC, 0x61);
	raw_spin_unlock(&i8253_lock);
}

/*
 * Force to stop and sync the stream
 */
void pcsp_sync_stop(struct snd_pcsp *chip)
{
	local_irq_disable();
	pcsp_stop_playing(chip);
	local_irq_enable();
	hrtimer_cancel(&chip->timer);
	tasklet_kill(&pcsp_pcm_tasklet);
}

static int snd_pcsp_playback_close(struct snd_pcm_substream *substream)
{
	struct snd_pcsp *chip = snd_pcm_substream_chip(substream);
#if PCSP_DEBUG
	printk(KERN_INFO "PCSP: close called\n");
#endif
	pcsp_sync_stop(chip);
	chip->playback_substream = NULL;
	return 0;
}

static int snd_pcsp_playback_hw_params(struct snd_pcm_substream *substream,
				       struct snd_pcm_hw_params *hw_params)
{
	struct snd_pcsp *chip = snd_pcm_substream_chip(substream);
	int err;
	pcsp_sync_stop(chip);
	err = snd_pcm_lib_malloc_pages(substream,
				      params_buffer_bytes(hw_params));
	if (err < 0)
		return err;
	return 0;
}

static int snd_pcsp_playback_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_pcsp *chip = snd_pcm_substream_chip(substream);
#if PCSP_DEBUG
	printk(KERN_INFO "PCSP: hw_free called\n");
#endif
	pcsp_sync_stop(chip);
	return snd_pcm_lib_free_pages(substream);
}

static int snd_pcsp_playback_prepare(struct snd_pcm_substream *substream)
{
	struct snd_pcsp *chip = snd_pcm_substream_chip(substream);
	pcsp_sync_stop(chip);
	chip->playback_ptr = 0;
	chip->period_ptr = 0;
	chip->fmt_size =
		snd_pcm_format_physical_width(substream->runtime->format) >> 3;
	chip->is_signed = snd_pcm_format_signed(substream->runtime->format);
#if PCSP_DEBUG
	printk(KERN_INFO "PCSP: prepare called, "
			"size=%zi psize=%zi f=%zi f1=%i fsize=%i\n",
			snd_pcm_lib_buffer_bytes(substream),
			snd_pcm_lib_period_bytes(substream),
			snd_pcm_lib_buffer_bytes(substream) /
			snd_pcm_lib_period_bytes(substream),
			substream->runtime->periods,
			chip->fmt_size);
#endif
	return 0;
}

static int snd_pcsp_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_pcsp *chip = snd_pcm_substream_chip(substream);
#if PCSP_DEBUG
	printk(KERN_INFO "PCSP: trigger called\n");
#endif
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		return pcsp_start_playing(chip);
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		pcsp_stop_playing(chip);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static snd_pcm_uframes_t snd_pcsp_playback_pointer(struct snd_pcm_substream
						   *substream)
{
	struct snd_pcsp *chip = snd_pcm_substream_chip(substream);
	unsigned int pos;
	spin_lock(&chip->substream_lock);
	pos = chip->playback_ptr;
	spin_unlock(&chip->substream_lock);
	return bytes_to_frames(substream->runtime, pos);
}

static const struct snd_pcm_hardware snd_pcsp_playback = {
	.info = (SNDRV_PCM_INFO_INTERLEAVED |
		 SNDRV_PCM_INFO_HALF_DUPLEX |
		 SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID),
	.formats = (SNDRV_PCM_FMTBIT_U8
#if DMIX_WANTS_S16
		    | SNDRV_PCM_FMTBIT_S16_LE
#endif
	    ),
	.rates = SNDRV_PCM_RATE_KNOT,
	.rate_min = PCSP_DEFAULT_SRATE,
	.rate_max = PCSP_DEFAULT_SRATE,
	.channels_min = 1,
	.channels_max = 1,
	.buffer_bytes_max = PCSP_BUFFER_SIZE,
	.period_bytes_min = 64,
	.period_bytes_max = PCSP_MAX_PERIOD_SIZE,
	.periods_min = 2,
	.periods_max = PCSP_MAX_PERIODS,
	.fifo_size = 0,
};

static int snd_pcsp_playback_open(struct snd_pcm_substream *substream)
{
	struct snd_pcsp *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
#if PCSP_DEBUG
	printk(KERN_INFO "PCSP: open called\n");
#endif
	if (atomic_read(&chip->timer_active)) {
		printk(KERN_ERR "PCSP: still active!!\n");
		return -EBUSY;
	}
	runtime->hw = snd_pcsp_playback;
	chip->playback_substream = substream;
	return 0;
}

static const struct snd_pcm_ops snd_pcsp_playback_ops = {
	.open = snd_pcsp_playback_open,
	.close = snd_pcsp_playback_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = snd_pcsp_playback_hw_params,
	.hw_free = snd_pcsp_playback_hw_free,
	.prepare = snd_pcsp_playback_prepare,
	.trigger = snd_pcsp_trigger,
	.pointer = snd_pcsp_playback_pointer,
};

int snd_pcsp_new_pcm(struct snd_pcsp *chip)
{
	int err;

	err = snd_pcm_new(chip->card, "pcspeaker", 0, 1, 0, &chip->pcm);
	if (err < 0)
		return err;

	snd_pcm_set_ops(chip->pcm, SNDRV_PCM_STREAM_PLAYBACK,
			&snd_pcsp_playback_ops);

	chip->pcm->private_data = chip;
	chip->pcm->info_flags = SNDRV_PCM_INFO_HALF_DUPLEX;
	strcpy(chip->pcm->name, "pcsp");

	snd_pcm_lib_preallocate_pages_for_all(chip->pcm,
					      SNDRV_DMA_TYPE_CONTINUOUS,
					      snd_dma_continuous_data
					      (GFP_KERNEL), PCSP_BUFFER_SIZE,
					      PCSP_BUFFER_SIZE);

	return 0;
}
