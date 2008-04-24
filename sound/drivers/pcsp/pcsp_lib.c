/*
 * PC-Speaker driver for Linux
 *
 * Copyright (C) 1993-1997  Michael Beck
 * Copyright (C) 1997-2001  David Woodhouse
 * Copyright (C) 2001-2008  Stas Sergeev
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <sound/pcm.h>
#include <linux/interrupt.h>
#include <asm/io.h>
#include "pcsp.h"

static int nforce_wa;
module_param(nforce_wa, bool, 0444);
MODULE_PARM_DESC(nforce_wa, "Apply NForce chipset workaround "
		"(expect bad sound)");

static void pcsp_start_timer(unsigned long dummy)
{
	hrtimer_start(&pcsp_chip.timer, ktime_set(0, 0), HRTIMER_MODE_REL);
}

/*
 * We need the hrtimer_start as a tasklet to avoid
 * the nasty locking problem. :(
 * The problem:
 * - The timer handler is called with the cpu_base->lock
 *   already held by hrtimer code.
 * - snd_pcm_period_elapsed() takes the
 *   substream->self_group.lock.
 * So far so good.
 * But the snd_pcsp_trigger() is called with the
 * substream->self_group.lock held, and it calls
 * hrtimer_start(), which takes the cpu_base->lock.
 * You see the problem. We have the code pathes
 * which take two locks in a reverse order. This
 * can deadlock and the lock validator complains.
 * The only solution I could find was to move the
 * hrtimer_start() into a tasklet. -stsp
 */
static DECLARE_TASKLET(pcsp_start_timer_tasklet, pcsp_start_timer, 0);

enum hrtimer_restart pcsp_do_timer(struct hrtimer *handle)
{
	unsigned long flags;
	unsigned char timer_cnt, val;
	int periods_elapsed;
	u64 ns;
	size_t period_bytes, buffer_bytes;
	struct snd_pcm_substream *substream;
	struct snd_pcm_runtime *runtime;
	struct snd_pcsp *chip = container_of(handle, struct snd_pcsp, timer);

	if (chip->thalf) {
		outb(chip->val61, 0x61);
		chip->thalf = 0;
		if (!atomic_read(&chip->timer_active))
			return HRTIMER_NORESTART;
		hrtimer_forward(&chip->timer, chip->timer.expires,
				ktime_set(0, chip->ns_rem));
		return HRTIMER_RESTART;
	}

	/* hrtimer calls us from both hardirq and softirq contexts,
	 * so irqsave :( */
	spin_lock_irqsave(&chip->substream_lock, flags);
	/* Takashi Iwai says regarding this extra lock:

	If the irq handler handles some data on the DMA buffer, it should
	do snd_pcm_stream_lock().
	That protects basically against all races among PCM callbacks, yes.
	However, there are two remaining issues:
	1. The substream pointer you try to lock isn't protected _before_
	  this lock yet.
	2. snd_pcm_period_elapsed() itself acquires the lock.
	The requirement of another lock is because of 1.  When you get
	chip->playback_substream, it's not protected.
	Keeping this lock while snd_pcm_period_elapsed() assures the substream
	is still protected (at least, not released).  And the other status is
	handled properly inside snd_pcm_stream_lock() in
	snd_pcm_period_elapsed().

	*/
	if (!chip->playback_substream)
		goto exit_nr_unlock1;
	substream = chip->playback_substream;
	snd_pcm_stream_lock(substream);
	if (!atomic_read(&chip->timer_active))
		goto exit_nr_unlock2;

	runtime = substream->runtime;
	/* assume it is u8 mono */
	val = runtime->dma_area[chip->playback_ptr];
	timer_cnt = val * CUR_DIV() / 256;

	if (timer_cnt && chip->enable) {
		spin_lock(&i8253_lock);
		if (!nforce_wa) {
			outb_p(chip->val61, 0x61);
			outb_p(timer_cnt, 0x42);
			outb(chip->val61 ^ 1, 0x61);
		} else {
			outb(chip->val61 ^ 2, 0x61);
			chip->thalf = 1;
		}
		spin_unlock(&i8253_lock);
	}

	period_bytes = snd_pcm_lib_period_bytes(substream);
	buffer_bytes = snd_pcm_lib_buffer_bytes(substream);
	chip->playback_ptr += PCSP_INDEX_INC();
	periods_elapsed = chip->playback_ptr - chip->period_ptr;
	if (periods_elapsed < 0) {
		printk(KERN_WARNING "PCSP: playback_ptr inconsistent "
			"(%zi %zi %zi)\n",
			chip->playback_ptr, period_bytes, buffer_bytes);
		periods_elapsed += buffer_bytes;
	}
	periods_elapsed /= period_bytes;
	/* wrap the pointer _before_ calling snd_pcm_period_elapsed(),
	 * or ALSA will BUG on us. */
	chip->playback_ptr %= buffer_bytes;

	snd_pcm_stream_unlock(substream);

	if (periods_elapsed) {
		snd_pcm_period_elapsed(substream);
		chip->period_ptr += periods_elapsed * period_bytes;
		chip->period_ptr %= buffer_bytes;
	}

	spin_unlock_irqrestore(&chip->substream_lock, flags);

	if (!atomic_read(&chip->timer_active))
		return HRTIMER_NORESTART;

	chip->ns_rem = PCSP_PERIOD_NS();
	ns = (chip->thalf ? PCSP_CALC_NS(timer_cnt) : chip->ns_rem);
	chip->ns_rem -= ns;
	hrtimer_forward(&chip->timer, chip->timer.expires, ktime_set(0, ns));
	return HRTIMER_RESTART;

exit_nr_unlock2:
	snd_pcm_stream_unlock(substream);
exit_nr_unlock1:
	spin_unlock_irqrestore(&chip->substream_lock, flags);
	return HRTIMER_NORESTART;
}

static void pcsp_start_playing(struct snd_pcsp *chip)
{
#if PCSP_DEBUG
	printk(KERN_INFO "PCSP: start_playing called\n");
#endif
	if (atomic_read(&chip->timer_active)) {
		printk(KERN_ERR "PCSP: Timer already active\n");
		return;
	}

	spin_lock(&i8253_lock);
	chip->val61 = inb(0x61) | 0x03;
	outb_p(0x92, 0x43);	/* binary, mode 1, LSB only, ch 2 */
	spin_unlock(&i8253_lock);
	atomic_set(&chip->timer_active, 1);
	chip->thalf = 0;

	tasklet_schedule(&pcsp_start_timer_tasklet);
}

static void pcsp_stop_playing(struct snd_pcsp *chip)
{
#if PCSP_DEBUG
	printk(KERN_INFO "PCSP: stop_playing called\n");
#endif
	if (!atomic_read(&chip->timer_active))
		return;

	atomic_set(&chip->timer_active, 0);
	spin_lock(&i8253_lock);
	/* restore the timer */
	outb_p(0xb6, 0x43);	/* binary, mode 3, LSB/MSB, ch 2 */
	outb(chip->val61 & 0xFC, 0x61);
	spin_unlock(&i8253_lock);
}

static int snd_pcsp_playback_close(struct snd_pcm_substream *substream)
{
	struct snd_pcsp *chip = snd_pcm_substream_chip(substream);
#if PCSP_DEBUG
	printk(KERN_INFO "PCSP: close called\n");
#endif
	if (atomic_read(&chip->timer_active)) {
		printk(KERN_ERR "PCSP: timer still active\n");
		pcsp_stop_playing(chip);
	}
	spin_lock_irq(&chip->substream_lock);
	chip->playback_substream = NULL;
	spin_unlock_irq(&chip->substream_lock);
	return 0;
}

static int snd_pcsp_playback_hw_params(struct snd_pcm_substream *substream,
				       struct snd_pcm_hw_params *hw_params)
{
	int err;
	err = snd_pcm_lib_malloc_pages(substream,
				      params_buffer_bytes(hw_params));
	if (err < 0)
		return err;
	return 0;
}

static int snd_pcsp_playback_hw_free(struct snd_pcm_substream *substream)
{
#if PCSP_DEBUG
	printk(KERN_INFO "PCSP: hw_free called\n");
#endif
	return snd_pcm_lib_free_pages(substream);
}

static int snd_pcsp_playback_prepare(struct snd_pcm_substream *substream)
{
	struct snd_pcsp *chip = snd_pcm_substream_chip(substream);
#if PCSP_DEBUG
	printk(KERN_INFO "PCSP: prepare called, "
			"size=%zi psize=%zi f=%zi f1=%i\n",
			snd_pcm_lib_buffer_bytes(substream),
			snd_pcm_lib_period_bytes(substream),
			snd_pcm_lib_buffer_bytes(substream) /
			snd_pcm_lib_period_bytes(substream),
			substream->runtime->periods);
#endif
	chip->playback_ptr = 0;
	chip->period_ptr = 0;
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
		pcsp_start_playing(chip);
		break;
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
	return bytes_to_frames(substream->runtime, chip->playback_ptr);
}

static struct snd_pcm_hardware snd_pcsp_playback = {
	.info = (SNDRV_PCM_INFO_INTERLEAVED |
		 SNDRV_PCM_INFO_HALF_DUPLEX |
		 SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID),
	.formats = SNDRV_PCM_FMTBIT_U8,
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
	spin_lock_irq(&chip->substream_lock);
	chip->playback_substream = substream;
	spin_unlock_irq(&chip->substream_lock);
	return 0;
}

static struct snd_pcm_ops snd_pcsp_playback_ops = {
	.open = snd_pcsp_playback_open,
	.close = snd_pcsp_playback_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = snd_pcsp_playback_hw_params,
	.hw_free = snd_pcsp_playback_hw_free,
	.prepare = snd_pcsp_playback_prepare,
	.trigger = snd_pcsp_trigger,
	.pointer = snd_pcsp_playback_pointer,
};

int __devinit snd_pcsp_new_pcm(struct snd_pcsp *chip)
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
