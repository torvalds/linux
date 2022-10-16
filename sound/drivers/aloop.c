// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Loopback soundcard
 *
 *  Original code:
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 *
 *  More accurate positioning and full-duplex support:
 *  Copyright (c) Ahmet İnan <ainan at mathematik.uni-freiburg.de>
 *
 *  Major (almost complete) rewrite:
 *  Copyright (c) by Takashi Iwai <tiwai@suse.de>
 *
 *  A next major update in 2010 (separate timers for playback and capture):
 *  Copyright (c) Jaroslav Kysela <perex@perex.cz>
 */

#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/wait.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/info.h>
#include <sound/initval.h>
#include <sound/timer.h>

MODULE_AUTHOR("Jaroslav Kysela <perex@perex.cz>");
MODULE_DESCRIPTION("A loopback soundcard");
MODULE_LICENSE("GPL");

#define MAX_PCM_SUBSTREAMS	8

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static bool enable[SNDRV_CARDS] = {1, [1 ... (SNDRV_CARDS - 1)] = 0};
static int pcm_substreams[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 8};
static int pcm_notify[SNDRV_CARDS];
static char *timer_source[SNDRV_CARDS];

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for loopback soundcard.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for loopback soundcard.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable this loopback soundcard.");
module_param_array(pcm_substreams, int, NULL, 0444);
MODULE_PARM_DESC(pcm_substreams, "PCM substreams # (1-8) for loopback driver.");
module_param_array(pcm_notify, int, NULL, 0444);
MODULE_PARM_DESC(pcm_notify, "Break capture when PCM format/rate/channels changes.");
module_param_array(timer_source, charp, NULL, 0444);
MODULE_PARM_DESC(timer_source, "Sound card name or number and device/subdevice number of timer to be used. Empty string for jiffies timer [default].");

#define NO_PITCH 100000

#define CABLE_VALID_PLAYBACK	BIT(SNDRV_PCM_STREAM_PLAYBACK)
#define CABLE_VALID_CAPTURE	BIT(SNDRV_PCM_STREAM_CAPTURE)
#define CABLE_VALID_BOTH	(CABLE_VALID_PLAYBACK | CABLE_VALID_CAPTURE)

struct loopback_cable;
struct loopback_pcm;

struct loopback_ops {
	/* optional
	 * call in loopback->cable_lock
	 */
	int (*open)(struct loopback_pcm *dpcm);
	/* required
	 * call in cable->lock
	 */
	int (*start)(struct loopback_pcm *dpcm);
	/* required
	 * call in cable->lock
	 */
	int (*stop)(struct loopback_pcm *dpcm);
	/* optional */
	int (*stop_sync)(struct loopback_pcm *dpcm);
	/* optional */
	int (*close_substream)(struct loopback_pcm *dpcm);
	/* optional
	 * call in loopback->cable_lock
	 */
	int (*close_cable)(struct loopback_pcm *dpcm);
	/* optional
	 * call in cable->lock
	 */
	unsigned int (*pos_update)(struct loopback_cable *cable);
	/* optional */
	void (*dpcm_info)(struct loopback_pcm *dpcm,
			  struct snd_info_buffer *buffer);
};

struct loopback_cable {
	spinlock_t lock;
	struct loopback_pcm *streams[2];
	struct snd_pcm_hardware hw;
	/* flags */
	unsigned int valid;
	unsigned int running;
	unsigned int pause;
	/* timer specific */
	const struct loopback_ops *ops;
	/* If sound timer is used */
	struct {
		int stream;
		struct snd_timer_id id;
		struct work_struct event_work;
		struct snd_timer_instance *instance;
	} snd_timer;
};

struct loopback_setup {
	unsigned int notify: 1;
	unsigned int rate_shift;
	snd_pcm_format_t format;
	unsigned int rate;
	unsigned int channels;
	struct snd_ctl_elem_id active_id;
	struct snd_ctl_elem_id format_id;
	struct snd_ctl_elem_id rate_id;
	struct snd_ctl_elem_id channels_id;
};

struct loopback {
	struct snd_card *card;
	struct mutex cable_lock;
	struct loopback_cable *cables[MAX_PCM_SUBSTREAMS][2];
	struct snd_pcm *pcm[2];
	struct loopback_setup setup[MAX_PCM_SUBSTREAMS][2];
	const char *timer_source;
};

struct loopback_pcm {
	struct loopback *loopback;
	struct snd_pcm_substream *substream;
	struct loopback_cable *cable;
	unsigned int pcm_buffer_size;
	unsigned int buf_pos;	/* position in buffer */
	unsigned int silent_size;
	/* PCM parameters */
	unsigned int pcm_period_size;
	unsigned int pcm_bps;		/* bytes per second */
	unsigned int pcm_salign;	/* bytes per sample * channels */
	unsigned int pcm_rate_shift;	/* rate shift value */
	/* flags */
	unsigned int period_update_pending :1;
	/* timer stuff */
	unsigned int irq_pos;		/* fractional IRQ position in jiffies
					 * ticks
					 */
	unsigned int period_size_frac;	/* period size in jiffies ticks */
	unsigned int last_drift;
	unsigned long last_jiffies;
	/* If jiffies timer is used */
	struct timer_list timer;
};

static struct platform_device *devices[SNDRV_CARDS];

static inline unsigned int byte_pos(struct loopback_pcm *dpcm, unsigned int x)
{
	if (dpcm->pcm_rate_shift == NO_PITCH) {
		x /= HZ;
	} else {
		x = div_u64(NO_PITCH * (unsigned long long)x,
			    HZ * (unsigned long long)dpcm->pcm_rate_shift);
	}
	return x - (x % dpcm->pcm_salign);
}

static inline unsigned int frac_pos(struct loopback_pcm *dpcm, unsigned int x)
{
	if (dpcm->pcm_rate_shift == NO_PITCH) {	/* no pitch */
		return x * HZ;
	} else {
		x = div_u64(dpcm->pcm_rate_shift * (unsigned long long)x * HZ,
			    NO_PITCH);
	}
	return x;
}

static inline struct loopback_setup *get_setup(struct loopback_pcm *dpcm)
{
	int device = dpcm->substream->pstr->pcm->device;
	
	if (dpcm->substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		device ^= 1;
	return &dpcm->loopback->setup[dpcm->substream->number][device];
}

static inline unsigned int get_notify(struct loopback_pcm *dpcm)
{
	return get_setup(dpcm)->notify;
}

static inline unsigned int get_rate_shift(struct loopback_pcm *dpcm)
{
	return get_setup(dpcm)->rate_shift;
}

/* call in cable->lock */
static int loopback_jiffies_timer_start(struct loopback_pcm *dpcm)
{
	unsigned long tick;
	unsigned int rate_shift = get_rate_shift(dpcm);

	if (rate_shift != dpcm->pcm_rate_shift) {
		dpcm->pcm_rate_shift = rate_shift;
		dpcm->period_size_frac = frac_pos(dpcm, dpcm->pcm_period_size);
	}
	if (dpcm->period_size_frac <= dpcm->irq_pos) {
		dpcm->irq_pos %= dpcm->period_size_frac;
		dpcm->period_update_pending = 1;
	}
	tick = dpcm->period_size_frac - dpcm->irq_pos;
	tick = DIV_ROUND_UP(tick, dpcm->pcm_bps);
	mod_timer(&dpcm->timer, jiffies + tick);

	return 0;
}

/* call in cable->lock */
static int loopback_snd_timer_start(struct loopback_pcm *dpcm)
{
	struct loopback_cable *cable = dpcm->cable;
	int err;

	/* Loopback device has to use same period as timer card. Therefore
	 * wake up for each snd_pcm_period_elapsed() call of timer card.
	 */
	err = snd_timer_start(cable->snd_timer.instance, 1);
	if (err < 0) {
		/* do not report error if trying to start but already
		 * running. For example called by opposite substream
		 * of the same cable
		 */
		if (err == -EBUSY)
			return 0;

		pcm_err(dpcm->substream->pcm,
			"snd_timer_start(%d,%d,%d) failed with %d",
			cable->snd_timer.id.card,
			cable->snd_timer.id.device,
			cable->snd_timer.id.subdevice,
			err);
	}

	return err;
}

/* call in cable->lock */
static inline int loopback_jiffies_timer_stop(struct loopback_pcm *dpcm)
{
	del_timer(&dpcm->timer);
	dpcm->timer.expires = 0;

	return 0;
}

/* call in cable->lock */
static int loopback_snd_timer_stop(struct loopback_pcm *dpcm)
{
	struct loopback_cable *cable = dpcm->cable;
	int err;

	/* only stop if both devices (playback and capture) are not running */
	if (cable->running ^ cable->pause)
		return 0;

	err = snd_timer_stop(cable->snd_timer.instance);
	if (err < 0) {
		pcm_err(dpcm->substream->pcm,
			"snd_timer_stop(%d,%d,%d) failed with %d",
			cable->snd_timer.id.card,
			cable->snd_timer.id.device,
			cable->snd_timer.id.subdevice,
			err);
	}

	return err;
}

static inline int loopback_jiffies_timer_stop_sync(struct loopback_pcm *dpcm)
{
	del_timer_sync(&dpcm->timer);

	return 0;
}

/* call in loopback->cable_lock */
static int loopback_snd_timer_close_cable(struct loopback_pcm *dpcm)
{
	struct loopback_cable *cable = dpcm->cable;

	/* snd_timer was not opened */
	if (!cable->snd_timer.instance)
		return 0;

	/* will only be called from free_cable() when other stream was
	 * already closed. Other stream cannot be reopened as long as
	 * loopback->cable_lock is locked. Therefore no need to lock
	 * cable->lock;
	 */
	snd_timer_close(cable->snd_timer.instance);

	/* wait till drain work has finished if requested */
	cancel_work_sync(&cable->snd_timer.event_work);

	snd_timer_instance_free(cable->snd_timer.instance);
	memset(&cable->snd_timer, 0, sizeof(cable->snd_timer));

	return 0;
}

static int loopback_check_format(struct loopback_cable *cable, int stream)
{
	struct snd_pcm_runtime *runtime, *cruntime;
	struct loopback_setup *setup;
	struct snd_card *card;
	int check;

	if (cable->valid != CABLE_VALID_BOTH) {
		if (stream == SNDRV_PCM_STREAM_PLAYBACK)
			goto __notify;
		return 0;
	}
	runtime = cable->streams[SNDRV_PCM_STREAM_PLAYBACK]->
							substream->runtime;
	cruntime = cable->streams[SNDRV_PCM_STREAM_CAPTURE]->
							substream->runtime;
	check = runtime->format != cruntime->format ||
		runtime->rate != cruntime->rate ||
		runtime->channels != cruntime->channels;
	if (!check)
		return 0;
	if (stream == SNDRV_PCM_STREAM_CAPTURE) {
		return -EIO;
	} else {
		snd_pcm_stop(cable->streams[SNDRV_PCM_STREAM_CAPTURE]->
					substream, SNDRV_PCM_STATE_DRAINING);
	      __notify:
		runtime = cable->streams[SNDRV_PCM_STREAM_PLAYBACK]->
							substream->runtime;
		setup = get_setup(cable->streams[SNDRV_PCM_STREAM_PLAYBACK]);
		card = cable->streams[SNDRV_PCM_STREAM_PLAYBACK]->loopback->card;
		if (setup->format != runtime->format) {
			snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_VALUE,
							&setup->format_id);
			setup->format = runtime->format;
		}
		if (setup->rate != runtime->rate) {
			snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_VALUE,
							&setup->rate_id);
			setup->rate = runtime->rate;
		}
		if (setup->channels != runtime->channels) {
			snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_VALUE,
							&setup->channels_id);
			setup->channels = runtime->channels;
		}
	}
	return 0;
}

static void loopback_active_notify(struct loopback_pcm *dpcm)
{
	snd_ctl_notify(dpcm->loopback->card,
		       SNDRV_CTL_EVENT_MASK_VALUE,
		       &get_setup(dpcm)->active_id);
}

static int loopback_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct loopback_pcm *dpcm = runtime->private_data;
	struct loopback_cable *cable = dpcm->cable;
	int err = 0, stream = 1 << substream->stream;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		err = loopback_check_format(cable, substream->stream);
		if (err < 0)
			return err;
		dpcm->last_jiffies = jiffies;
		dpcm->pcm_rate_shift = 0;
		dpcm->last_drift = 0;
		spin_lock(&cable->lock);	
		cable->running |= stream;
		cable->pause &= ~stream;
		err = cable->ops->start(dpcm);
		spin_unlock(&cable->lock);
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			loopback_active_notify(dpcm);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		spin_lock(&cable->lock);	
		cable->running &= ~stream;
		cable->pause &= ~stream;
		err = cable->ops->stop(dpcm);
		spin_unlock(&cable->lock);
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			loopback_active_notify(dpcm);
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		spin_lock(&cable->lock);	
		cable->pause |= stream;
		err = cable->ops->stop(dpcm);
		spin_unlock(&cable->lock);
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			loopback_active_notify(dpcm);
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
	case SNDRV_PCM_TRIGGER_RESUME:
		spin_lock(&cable->lock);
		dpcm->last_jiffies = jiffies;
		cable->pause &= ~stream;
		err = cable->ops->start(dpcm);
		spin_unlock(&cable->lock);
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			loopback_active_notify(dpcm);
		break;
	default:
		return -EINVAL;
	}
	return err;
}

static void params_change(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct loopback_pcm *dpcm = runtime->private_data;
	struct loopback_cable *cable = dpcm->cable;

	cable->hw.formats = pcm_format_to_bits(runtime->format);
	cable->hw.rate_min = runtime->rate;
	cable->hw.rate_max = runtime->rate;
	cable->hw.channels_min = runtime->channels;
	cable->hw.channels_max = runtime->channels;

	if (cable->snd_timer.instance) {
		cable->hw.period_bytes_min =
				frames_to_bytes(runtime, runtime->period_size);
		cable->hw.period_bytes_max = cable->hw.period_bytes_min;
	}

}

static int loopback_prepare(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct loopback_pcm *dpcm = runtime->private_data;
	struct loopback_cable *cable = dpcm->cable;
	int err, bps, salign;

	if (cable->ops->stop_sync) {
		err = cable->ops->stop_sync(dpcm);
		if (err < 0)
			return err;
	}

	salign = (snd_pcm_format_physical_width(runtime->format) *
						runtime->channels) / 8;
	bps = salign * runtime->rate;
	if (bps <= 0 || salign <= 0)
		return -EINVAL;

	dpcm->buf_pos = 0;
	dpcm->pcm_buffer_size = frames_to_bytes(runtime, runtime->buffer_size);
	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		/* clear capture buffer */
		dpcm->silent_size = dpcm->pcm_buffer_size;
		snd_pcm_format_set_silence(runtime->format, runtime->dma_area,
					   runtime->buffer_size * runtime->channels);
	}

	dpcm->irq_pos = 0;
	dpcm->period_update_pending = 0;
	dpcm->pcm_bps = bps;
	dpcm->pcm_salign = salign;
	dpcm->pcm_period_size = frames_to_bytes(runtime, runtime->period_size);

	mutex_lock(&dpcm->loopback->cable_lock);
	if (!(cable->valid & ~(1 << substream->stream)) ||
            (get_setup(dpcm)->notify &&
	     substream->stream == SNDRV_PCM_STREAM_PLAYBACK))
		params_change(substream);
	cable->valid |= 1 << substream->stream;
	mutex_unlock(&dpcm->loopback->cable_lock);

	return 0;
}

static void clear_capture_buf(struct loopback_pcm *dpcm, unsigned int bytes)
{
	struct snd_pcm_runtime *runtime = dpcm->substream->runtime;
	char *dst = runtime->dma_area;
	unsigned int dst_off = dpcm->buf_pos;

	if (dpcm->silent_size >= dpcm->pcm_buffer_size)
		return;
	if (dpcm->silent_size + bytes > dpcm->pcm_buffer_size)
		bytes = dpcm->pcm_buffer_size - dpcm->silent_size;

	for (;;) {
		unsigned int size = bytes;
		if (dst_off + size > dpcm->pcm_buffer_size)
			size = dpcm->pcm_buffer_size - dst_off;
		snd_pcm_format_set_silence(runtime->format, dst + dst_off,
					   bytes_to_frames(runtime, size) *
					   	runtime->channels);
		dpcm->silent_size += size;
		bytes -= size;
		if (!bytes)
			break;
		dst_off = 0;
	}
}

static void copy_play_buf(struct loopback_pcm *play,
			  struct loopback_pcm *capt,
			  unsigned int bytes)
{
	struct snd_pcm_runtime *runtime = play->substream->runtime;
	char *src = runtime->dma_area;
	char *dst = capt->substream->runtime->dma_area;
	unsigned int src_off = play->buf_pos;
	unsigned int dst_off = capt->buf_pos;
	unsigned int clear_bytes = 0;

	/* check if playback is draining, trim the capture copy size
	 * when our pointer is at the end of playback ring buffer */
	if (runtime->status->state == SNDRV_PCM_STATE_DRAINING &&
	    snd_pcm_playback_hw_avail(runtime) < runtime->buffer_size) { 
	    	snd_pcm_uframes_t appl_ptr, appl_ptr1, diff;
		appl_ptr = appl_ptr1 = runtime->control->appl_ptr;
		appl_ptr1 -= appl_ptr1 % runtime->buffer_size;
		appl_ptr1 += play->buf_pos / play->pcm_salign;
		if (appl_ptr < appl_ptr1)
			appl_ptr1 -= runtime->buffer_size;
		diff = (appl_ptr - appl_ptr1) * play->pcm_salign;
		if (diff < bytes) {
			clear_bytes = bytes - diff;
			bytes = diff;
		}
	}

	for (;;) {
		unsigned int size = bytes;
		if (src_off + size > play->pcm_buffer_size)
			size = play->pcm_buffer_size - src_off;
		if (dst_off + size > capt->pcm_buffer_size)
			size = capt->pcm_buffer_size - dst_off;
		memcpy(dst + dst_off, src + src_off, size);
		capt->silent_size = 0;
		bytes -= size;
		if (!bytes)
			break;
		src_off = (src_off + size) % play->pcm_buffer_size;
		dst_off = (dst_off + size) % capt->pcm_buffer_size;
	}

	if (clear_bytes > 0) {
		clear_capture_buf(capt, clear_bytes);
		capt->silent_size = 0;
	}
}

static inline unsigned int bytepos_delta(struct loopback_pcm *dpcm,
					 unsigned int jiffies_delta)
{
	unsigned long last_pos;
	unsigned int delta;

	last_pos = byte_pos(dpcm, dpcm->irq_pos);
	dpcm->irq_pos += jiffies_delta * dpcm->pcm_bps;
	delta = byte_pos(dpcm, dpcm->irq_pos) - last_pos;
	if (delta >= dpcm->last_drift)
		delta -= dpcm->last_drift;
	dpcm->last_drift = 0;
	if (dpcm->irq_pos >= dpcm->period_size_frac) {
		dpcm->irq_pos %= dpcm->period_size_frac;
		dpcm->period_update_pending = 1;
	}
	return delta;
}

static inline void bytepos_finish(struct loopback_pcm *dpcm,
				  unsigned int delta)
{
	dpcm->buf_pos += delta;
	dpcm->buf_pos %= dpcm->pcm_buffer_size;
}

/* call in cable->lock */
static unsigned int loopback_jiffies_timer_pos_update
		(struct loopback_cable *cable)
{
	struct loopback_pcm *dpcm_play =
			cable->streams[SNDRV_PCM_STREAM_PLAYBACK];
	struct loopback_pcm *dpcm_capt =
			cable->streams[SNDRV_PCM_STREAM_CAPTURE];
	unsigned long delta_play = 0, delta_capt = 0, cur_jiffies;
	unsigned int running, count1, count2;

	cur_jiffies = jiffies;
	running = cable->running ^ cable->pause;
	if (running & (1 << SNDRV_PCM_STREAM_PLAYBACK)) {
		delta_play = cur_jiffies - dpcm_play->last_jiffies;
		dpcm_play->last_jiffies += delta_play;
	}

	if (running & (1 << SNDRV_PCM_STREAM_CAPTURE)) {
		delta_capt = cur_jiffies - dpcm_capt->last_jiffies;
		dpcm_capt->last_jiffies += delta_capt;
	}

	if (delta_play == 0 && delta_capt == 0)
		goto unlock;
		
	if (delta_play > delta_capt) {
		count1 = bytepos_delta(dpcm_play, delta_play - delta_capt);
		bytepos_finish(dpcm_play, count1);
		delta_play = delta_capt;
	} else if (delta_play < delta_capt) {
		count1 = bytepos_delta(dpcm_capt, delta_capt - delta_play);
		clear_capture_buf(dpcm_capt, count1);
		bytepos_finish(dpcm_capt, count1);
		delta_capt = delta_play;
	}

	if (delta_play == 0 && delta_capt == 0)
		goto unlock;

	/* note delta_capt == delta_play at this moment */
	count1 = bytepos_delta(dpcm_play, delta_play);
	count2 = bytepos_delta(dpcm_capt, delta_capt);
	if (count1 < count2) {
		dpcm_capt->last_drift = count2 - count1;
		count1 = count2;
	} else if (count1 > count2) {
		dpcm_play->last_drift = count1 - count2;
	}
	copy_play_buf(dpcm_play, dpcm_capt, count1);
	bytepos_finish(dpcm_play, count1);
	bytepos_finish(dpcm_capt, count1);
 unlock:
	return running;
}

static void loopback_jiffies_timer_function(struct timer_list *t)
{
	struct loopback_pcm *dpcm = from_timer(dpcm, t, timer);
	unsigned long flags;

	spin_lock_irqsave(&dpcm->cable->lock, flags);
	if (loopback_jiffies_timer_pos_update(dpcm->cable) &
			(1 << dpcm->substream->stream)) {
		loopback_jiffies_timer_start(dpcm);
		if (dpcm->period_update_pending) {
			dpcm->period_update_pending = 0;
			spin_unlock_irqrestore(&dpcm->cable->lock, flags);
			/* need to unlock before calling below */
			snd_pcm_period_elapsed(dpcm->substream);
			return;
		}
	}
	spin_unlock_irqrestore(&dpcm->cable->lock, flags);
}

/* call in cable->lock */
static int loopback_snd_timer_check_resolution(struct snd_pcm_runtime *runtime,
					       unsigned long resolution)
{
	if (resolution != runtime->timer_resolution) {
		struct loopback_pcm *dpcm = runtime->private_data;
		struct loopback_cable *cable = dpcm->cable;
		/* Worst case estimation of possible values for resolution
		 * resolution <= (512 * 1024) frames / 8kHz in nsec
		 * resolution <= 65.536.000.000 nsec
		 *
		 * period_size <= 65.536.000.000 nsec / 1000nsec/usec * 192kHz +
		 *  500.000
		 * period_size <= 12.582.912.000.000  <64bit
		 *  / 1.000.000 usec/sec
		 */
		snd_pcm_uframes_t period_size_usec =
				resolution / 1000 * runtime->rate;
		/* round to nearest sample rate */
		snd_pcm_uframes_t period_size =
				(period_size_usec + 500 * 1000) / (1000 * 1000);

		pcm_err(dpcm->substream->pcm,
			"Period size (%lu frames) of loopback device is not corresponding to timer resolution (%lu nsec = %lu frames) of card timer %d,%d,%d. Use period size of %lu frames for loopback device.",
			runtime->period_size, resolution, period_size,
			cable->snd_timer.id.card,
			cable->snd_timer.id.device,
			cable->snd_timer.id.subdevice,
			period_size);
		return -EINVAL;
	}
	return 0;
}

static void loopback_snd_timer_period_elapsed(struct loopback_cable *cable,
					      int event,
					      unsigned long resolution)
{
	struct loopback_pcm *dpcm_play, *dpcm_capt;
	struct snd_pcm_substream *substream_play, *substream_capt;
	struct snd_pcm_runtime *valid_runtime;
	unsigned int running, elapsed_bytes;
	unsigned long flags;

	spin_lock_irqsave(&cable->lock, flags);
	running = cable->running ^ cable->pause;
	/* no need to do anything if no stream is running */
	if (!running) {
		spin_unlock_irqrestore(&cable->lock, flags);
		return;
	}

	dpcm_play = cable->streams[SNDRV_PCM_STREAM_PLAYBACK];
	dpcm_capt = cable->streams[SNDRV_PCM_STREAM_CAPTURE];

	if (event == SNDRV_TIMER_EVENT_MSTOP) {
		if (!dpcm_play ||
		    dpcm_play->substream->runtime->status->state !=
				SNDRV_PCM_STATE_DRAINING) {
			spin_unlock_irqrestore(&cable->lock, flags);
			return;
		}
	}

	substream_play = (running & (1 << SNDRV_PCM_STREAM_PLAYBACK)) ?
			dpcm_play->substream : NULL;
	substream_capt = (running & (1 << SNDRV_PCM_STREAM_CAPTURE)) ?
			dpcm_capt->substream : NULL;
	valid_runtime = (running & (1 << SNDRV_PCM_STREAM_PLAYBACK)) ?
				dpcm_play->substream->runtime :
				dpcm_capt->substream->runtime;

	/* resolution is only valid for SNDRV_TIMER_EVENT_TICK events */
	if (event == SNDRV_TIMER_EVENT_TICK) {
		/* The hardware rules guarantee that playback and capture period
		 * are the same. Therefore only one device has to be checked
		 * here.
		 */
		if (loopback_snd_timer_check_resolution(valid_runtime,
							resolution) < 0) {
			spin_unlock_irqrestore(&cable->lock, flags);
			if (substream_play)
				snd_pcm_stop_xrun(substream_play);
			if (substream_capt)
				snd_pcm_stop_xrun(substream_capt);
			return;
		}
	}

	elapsed_bytes = frames_to_bytes(valid_runtime,
					valid_runtime->period_size);
	/* The same timer interrupt is used for playback and capture device */
	if ((running & (1 << SNDRV_PCM_STREAM_PLAYBACK)) &&
	    (running & (1 << SNDRV_PCM_STREAM_CAPTURE))) {
		copy_play_buf(dpcm_play, dpcm_capt, elapsed_bytes);
		bytepos_finish(dpcm_play, elapsed_bytes);
		bytepos_finish(dpcm_capt, elapsed_bytes);
	} else if (running & (1 << SNDRV_PCM_STREAM_PLAYBACK)) {
		bytepos_finish(dpcm_play, elapsed_bytes);
	} else if (running & (1 << SNDRV_PCM_STREAM_CAPTURE)) {
		clear_capture_buf(dpcm_capt, elapsed_bytes);
		bytepos_finish(dpcm_capt, elapsed_bytes);
	}
	spin_unlock_irqrestore(&cable->lock, flags);

	if (substream_play)
		snd_pcm_period_elapsed(substream_play);
	if (substream_capt)
		snd_pcm_period_elapsed(substream_capt);
}

static void loopback_snd_timer_function(struct snd_timer_instance *timeri,
					unsigned long resolution,
					unsigned long ticks)
{
	struct loopback_cable *cable = timeri->callback_data;

	loopback_snd_timer_period_elapsed(cable, SNDRV_TIMER_EVENT_TICK,
					  resolution);
}

static void loopback_snd_timer_work(struct work_struct *work)
{
	struct loopback_cable *cable;

	cable = container_of(work, struct loopback_cable, snd_timer.event_work);
	loopback_snd_timer_period_elapsed(cable, SNDRV_TIMER_EVENT_MSTOP, 0);
}

static void loopback_snd_timer_event(struct snd_timer_instance *timeri,
				     int event,
				     struct timespec64 *tstamp,
				     unsigned long resolution)
{
	/* Do not lock cable->lock here because timer->lock is already hold.
	 * There are other functions which first lock cable->lock and than
	 * timer->lock e.g.
	 * loopback_trigger()
	 * spin_lock(&cable->lock)
	 * loopback_snd_timer_start()
	 * snd_timer_start()
	 * spin_lock(&timer->lock)
	 * Therefore when using the oposit order of locks here it could result
	 * in a deadlock.
	 */

	if (event == SNDRV_TIMER_EVENT_MSTOP) {
		struct loopback_cable *cable = timeri->callback_data;

		/* sound card of the timer was stopped. Therefore there will not
		 * be any further timer callbacks. Due to this forward audio
		 * data from here if in draining state. When still in running
		 * state the streaming will be aborted by the usual timeout. It
		 * should not be aborted here because may be the timer sound
		 * card does only a recovery and the timer is back soon.
		 * This work triggers loopback_snd_timer_work()
		 */
		schedule_work(&cable->snd_timer.event_work);
	}
}

static void loopback_jiffies_timer_dpcm_info(struct loopback_pcm *dpcm,
					     struct snd_info_buffer *buffer)
{
	snd_iprintf(buffer, "    update_pending:\t%u\n",
		    dpcm->period_update_pending);
	snd_iprintf(buffer, "    irq_pos:\t\t%u\n", dpcm->irq_pos);
	snd_iprintf(buffer, "    period_frac:\t%u\n", dpcm->period_size_frac);
	snd_iprintf(buffer, "    last_jiffies:\t%lu (%lu)\n",
		    dpcm->last_jiffies, jiffies);
	snd_iprintf(buffer, "    timer_expires:\t%lu\n", dpcm->timer.expires);
}

static void loopback_snd_timer_dpcm_info(struct loopback_pcm *dpcm,
					 struct snd_info_buffer *buffer)
{
	struct loopback_cable *cable = dpcm->cable;

	snd_iprintf(buffer, "    sound timer:\thw:%d,%d,%d\n",
		    cable->snd_timer.id.card,
		    cable->snd_timer.id.device,
		    cable->snd_timer.id.subdevice);
	snd_iprintf(buffer, "    timer open:\t\t%s\n",
		    (cable->snd_timer.stream == SNDRV_PCM_STREAM_CAPTURE) ?
			    "capture" : "playback");
}

static snd_pcm_uframes_t loopback_pointer(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct loopback_pcm *dpcm = runtime->private_data;
	snd_pcm_uframes_t pos;

	spin_lock(&dpcm->cable->lock);
	if (dpcm->cable->ops->pos_update)
		dpcm->cable->ops->pos_update(dpcm->cable);
	pos = dpcm->buf_pos;
	spin_unlock(&dpcm->cable->lock);
	return bytes_to_frames(runtime, pos);
}

static const struct snd_pcm_hardware loopback_pcm_hardware =
{
	.info =		(SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_MMAP |
			 SNDRV_PCM_INFO_MMAP_VALID | SNDRV_PCM_INFO_PAUSE |
			 SNDRV_PCM_INFO_RESUME),
	.formats =	(SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S16_BE |
			 SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S24_BE |
			 SNDRV_PCM_FMTBIT_S24_3LE | SNDRV_PCM_FMTBIT_S24_3BE |
			 SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_S32_BE |
			 SNDRV_PCM_FMTBIT_FLOAT_LE | SNDRV_PCM_FMTBIT_FLOAT_BE),
	.rates =	SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_192000,
	.rate_min =		8000,
	.rate_max =		192000,
	.channels_min =		1,
	.channels_max =		32,
	.buffer_bytes_max =	2 * 1024 * 1024,
	.period_bytes_min =	64,
	/* note check overflow in frac_pos() using pcm_rate_shift before
	   changing period_bytes_max value */
	.period_bytes_max =	1024 * 1024,
	.periods_min =		1,
	.periods_max =		1024,
	.fifo_size =		0,
};

static void loopback_runtime_free(struct snd_pcm_runtime *runtime)
{
	struct loopback_pcm *dpcm = runtime->private_data;
	kfree(dpcm);
}

static int loopback_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct loopback_pcm *dpcm = runtime->private_data;
	struct loopback_cable *cable = dpcm->cable;

	mutex_lock(&dpcm->loopback->cable_lock);
	cable->valid &= ~(1 << substream->stream);
	mutex_unlock(&dpcm->loopback->cable_lock);
	return 0;
}

static unsigned int get_cable_index(struct snd_pcm_substream *substream)
{
	if (!substream->pcm->device)
		return substream->stream;
	else
		return !substream->stream;
}

static int rule_format(struct snd_pcm_hw_params *params,
		       struct snd_pcm_hw_rule *rule)
{
	struct loopback_pcm *dpcm = rule->private;
	struct loopback_cable *cable = dpcm->cable;
	struct snd_mask m;

	snd_mask_none(&m);
	mutex_lock(&dpcm->loopback->cable_lock);
	m.bits[0] = (u_int32_t)cable->hw.formats;
	m.bits[1] = (u_int32_t)(cable->hw.formats >> 32);
	mutex_unlock(&dpcm->loopback->cable_lock);
	return snd_mask_refine(hw_param_mask(params, rule->var), &m);
}

static int rule_rate(struct snd_pcm_hw_params *params,
		     struct snd_pcm_hw_rule *rule)
{
	struct loopback_pcm *dpcm = rule->private;
	struct loopback_cable *cable = dpcm->cable;
	struct snd_interval t;

	mutex_lock(&dpcm->loopback->cable_lock);
	t.min = cable->hw.rate_min;
	t.max = cable->hw.rate_max;
	mutex_unlock(&dpcm->loopback->cable_lock);
        t.openmin = t.openmax = 0;
        t.integer = 0;
	return snd_interval_refine(hw_param_interval(params, rule->var), &t);
}

static int rule_channels(struct snd_pcm_hw_params *params,
			 struct snd_pcm_hw_rule *rule)
{
	struct loopback_pcm *dpcm = rule->private;
	struct loopback_cable *cable = dpcm->cable;
	struct snd_interval t;

	mutex_lock(&dpcm->loopback->cable_lock);
	t.min = cable->hw.channels_min;
	t.max = cable->hw.channels_max;
	mutex_unlock(&dpcm->loopback->cable_lock);
        t.openmin = t.openmax = 0;
        t.integer = 0;
	return snd_interval_refine(hw_param_interval(params, rule->var), &t);
}

static int rule_period_bytes(struct snd_pcm_hw_params *params,
			     struct snd_pcm_hw_rule *rule)
{
	struct loopback_pcm *dpcm = rule->private;
	struct loopback_cable *cable = dpcm->cable;
	struct snd_interval t;

	mutex_lock(&dpcm->loopback->cable_lock);
	t.min = cable->hw.period_bytes_min;
	t.max = cable->hw.period_bytes_max;
	mutex_unlock(&dpcm->loopback->cable_lock);
	t.openmin = 0;
	t.openmax = 0;
	t.integer = 0;
	return snd_interval_refine(hw_param_interval(params, rule->var), &t);
}

static void free_cable(struct snd_pcm_substream *substream)
{
	struct loopback *loopback = substream->private_data;
	int dev = get_cable_index(substream);
	struct loopback_cable *cable;

	cable = loopback->cables[substream->number][dev];
	if (!cable)
		return;
	if (cable->streams[!substream->stream]) {
		/* other stream is still alive */
		spin_lock_irq(&cable->lock);
		cable->streams[substream->stream] = NULL;
		spin_unlock_irq(&cable->lock);
	} else {
		struct loopback_pcm *dpcm = substream->runtime->private_data;

		if (cable->ops && cable->ops->close_cable && dpcm)
			cable->ops->close_cable(dpcm);
		/* free the cable */
		loopback->cables[substream->number][dev] = NULL;
		kfree(cable);
	}
}

static int loopback_jiffies_timer_open(struct loopback_pcm *dpcm)
{
	timer_setup(&dpcm->timer, loopback_jiffies_timer_function, 0);

	return 0;
}

static const struct loopback_ops loopback_jiffies_timer_ops = {
	.open = loopback_jiffies_timer_open,
	.start = loopback_jiffies_timer_start,
	.stop = loopback_jiffies_timer_stop,
	.stop_sync = loopback_jiffies_timer_stop_sync,
	.close_substream = loopback_jiffies_timer_stop_sync,
	.pos_update = loopback_jiffies_timer_pos_update,
	.dpcm_info = loopback_jiffies_timer_dpcm_info,
};

static int loopback_parse_timer_id(const char *str,
				   struct snd_timer_id *tid)
{
	/* [<pref>:](<card name>|<card idx>)[{.,}<dev idx>[{.,}<subdev idx>]] */
	const char * const sep_dev = ".,";
	const char * const sep_pref = ":";
	const char *name = str;
	char *sep, save = '\0';
	int card_idx = 0, dev = 0, subdev = 0;
	int err;

	sep = strpbrk(str, sep_pref);
	if (sep)
		name = sep + 1;
	sep = strpbrk(name, sep_dev);
	if (sep) {
		save = *sep;
		*sep = '\0';
	}
	err = kstrtoint(name, 0, &card_idx);
	if (err == -EINVAL) {
		/* Must be the name, not number */
		for (card_idx = 0; card_idx < snd_ecards_limit; card_idx++) {
			struct snd_card *card = snd_card_ref(card_idx);

			if (card) {
				if (!strcmp(card->id, name))
					err = 0;
				snd_card_unref(card);
			}
			if (!err)
				break;
		}
	}
	if (sep) {
		*sep = save;
		if (!err) {
			char *sep2, save2 = '\0';

			sep2 = strpbrk(sep + 1, sep_dev);
			if (sep2) {
				save2 = *sep2;
				*sep2 = '\0';
			}
			err = kstrtoint(sep + 1, 0, &dev);
			if (sep2) {
				*sep2 = save2;
				if (!err)
					err = kstrtoint(sep2 + 1, 0, &subdev);
			}
		}
	}
	if (!err && tid) {
		tid->card = card_idx;
		tid->device = dev;
		tid->subdevice = subdev;
	}
	return err;
}

/* call in loopback->cable_lock */
static int loopback_snd_timer_open(struct loopback_pcm *dpcm)
{
	int err = 0;
	struct snd_timer_id tid = {
		.dev_class = SNDRV_TIMER_CLASS_PCM,
		.dev_sclass = SNDRV_TIMER_SCLASS_APPLICATION,
	};
	struct snd_timer_instance *timeri;
	struct loopback_cable *cable = dpcm->cable;

	/* check if timer was already opened. It is only opened once
	 * per playback and capture subdevice (aka cable).
	 */
	if (cable->snd_timer.instance)
		goto exit;

	err = loopback_parse_timer_id(dpcm->loopback->timer_source, &tid);
	if (err < 0) {
		pcm_err(dpcm->substream->pcm,
			"Parsing timer source \'%s\' failed with %d",
			dpcm->loopback->timer_source, err);
		goto exit;
	}

	cable->snd_timer.stream = dpcm->substream->stream;
	cable->snd_timer.id = tid;

	timeri = snd_timer_instance_new(dpcm->loopback->card->id);
	if (!timeri) {
		err = -ENOMEM;
		goto exit;
	}
	/* The callback has to be called from another work. If
	 * SNDRV_TIMER_IFLG_FAST is specified it will be called from the
	 * snd_pcm_period_elapsed() call of the selected sound card.
	 * snd_pcm_period_elapsed() helds snd_pcm_stream_lock_irqsave().
	 * Due to our callback loopback_snd_timer_function() also calls
	 * snd_pcm_period_elapsed() which calls snd_pcm_stream_lock_irqsave().
	 * This would end up in a dead lock.
	 */
	timeri->flags |= SNDRV_TIMER_IFLG_AUTO;
	timeri->callback = loopback_snd_timer_function;
	timeri->callback_data = (void *)cable;
	timeri->ccallback = loopback_snd_timer_event;

	/* initialise a work used for draining */
	INIT_WORK(&cable->snd_timer.event_work, loopback_snd_timer_work);

	/* The mutex loopback->cable_lock is kept locked.
	 * Therefore snd_timer_open() cannot be called a second time
	 * by the other device of the same cable.
	 * Therefore the following issue cannot happen:
	 * [proc1] Call loopback_timer_open() ->
	 *	   Unlock cable->lock for snd_timer_close/open() call
	 * [proc2] Call loopback_timer_open() -> snd_timer_open(),
	 *	   snd_timer_start()
	 * [proc1] Call snd_timer_open() and overwrite running timer
	 *	   instance
	 */
	err = snd_timer_open(timeri, &cable->snd_timer.id, current->pid);
	if (err < 0) {
		pcm_err(dpcm->substream->pcm,
			"snd_timer_open (%d,%d,%d) failed with %d",
			cable->snd_timer.id.card,
			cable->snd_timer.id.device,
			cable->snd_timer.id.subdevice,
			err);
		snd_timer_instance_free(timeri);
		goto exit;
	}

	cable->snd_timer.instance = timeri;

exit:
	return err;
}

/* stop_sync() is not required for sound timer because it does not need to be
 * restarted in loopback_prepare() on Xrun recovery
 */
static const struct loopback_ops loopback_snd_timer_ops = {
	.open = loopback_snd_timer_open,
	.start = loopback_snd_timer_start,
	.stop = loopback_snd_timer_stop,
	.close_cable = loopback_snd_timer_close_cable,
	.dpcm_info = loopback_snd_timer_dpcm_info,
};

static int loopback_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct loopback *loopback = substream->private_data;
	struct loopback_pcm *dpcm;
	struct loopback_cable *cable = NULL;
	int err = 0;
	int dev = get_cable_index(substream);

	mutex_lock(&loopback->cable_lock);
	dpcm = kzalloc(sizeof(*dpcm), GFP_KERNEL);
	if (!dpcm) {
		err = -ENOMEM;
		goto unlock;
	}
	dpcm->loopback = loopback;
	dpcm->substream = substream;

	cable = loopback->cables[substream->number][dev];
	if (!cable) {
		cable = kzalloc(sizeof(*cable), GFP_KERNEL);
		if (!cable) {
			err = -ENOMEM;
			goto unlock;
		}
		spin_lock_init(&cable->lock);
		cable->hw = loopback_pcm_hardware;
		if (loopback->timer_source)
			cable->ops = &loopback_snd_timer_ops;
		else
			cable->ops = &loopback_jiffies_timer_ops;
		loopback->cables[substream->number][dev] = cable;
	}
	dpcm->cable = cable;
	runtime->private_data = dpcm;

	if (cable->ops->open) {
		err = cable->ops->open(dpcm);
		if (err < 0)
			goto unlock;
	}

	snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS);

	/* use dynamic rules based on actual runtime->hw values */
	/* note that the default rules created in the PCM midlevel code */
	/* are cached -> they do not reflect the actual state */
	err = snd_pcm_hw_rule_add(runtime, 0,
				  SNDRV_PCM_HW_PARAM_FORMAT,
				  rule_format, dpcm,
				  SNDRV_PCM_HW_PARAM_FORMAT, -1);
	if (err < 0)
		goto unlock;
	err = snd_pcm_hw_rule_add(runtime, 0,
				  SNDRV_PCM_HW_PARAM_RATE,
				  rule_rate, dpcm,
				  SNDRV_PCM_HW_PARAM_RATE, -1);
	if (err < 0)
		goto unlock;
	err = snd_pcm_hw_rule_add(runtime, 0,
				  SNDRV_PCM_HW_PARAM_CHANNELS,
				  rule_channels, dpcm,
				  SNDRV_PCM_HW_PARAM_CHANNELS, -1);
	if (err < 0)
		goto unlock;

	/* In case of sound timer the period time of both devices of the same
	 * loop has to be the same.
	 * This rule only takes effect if a sound timer was chosen
	 */
	if (cable->snd_timer.instance) {
		err = snd_pcm_hw_rule_add(runtime, 0,
					  SNDRV_PCM_HW_PARAM_PERIOD_BYTES,
					  rule_period_bytes, dpcm,
					  SNDRV_PCM_HW_PARAM_PERIOD_BYTES, -1);
		if (err < 0)
			goto unlock;
	}

	/* loopback_runtime_free() has not to be called if kfree(dpcm) was
	 * already called here. Otherwise it will end up with a double free.
	 */
	runtime->private_free = loopback_runtime_free;
	if (get_notify(dpcm))
		runtime->hw = loopback_pcm_hardware;
	else
		runtime->hw = cable->hw;

	spin_lock_irq(&cable->lock);
	cable->streams[substream->stream] = dpcm;
	spin_unlock_irq(&cable->lock);

 unlock:
	if (err < 0) {
		free_cable(substream);
		kfree(dpcm);
	}
	mutex_unlock(&loopback->cable_lock);
	return err;
}

static int loopback_close(struct snd_pcm_substream *substream)
{
	struct loopback *loopback = substream->private_data;
	struct loopback_pcm *dpcm = substream->runtime->private_data;
	int err = 0;

	if (dpcm->cable->ops->close_substream)
		err = dpcm->cable->ops->close_substream(dpcm);
	mutex_lock(&loopback->cable_lock);
	free_cable(substream);
	mutex_unlock(&loopback->cable_lock);
	return err;
}

static const struct snd_pcm_ops loopback_pcm_ops = {
	.open =		loopback_open,
	.close =	loopback_close,
	.hw_free =	loopback_hw_free,
	.prepare =	loopback_prepare,
	.trigger =	loopback_trigger,
	.pointer =	loopback_pointer,
};

static int loopback_pcm_new(struct loopback *loopback,
			    int device, int substreams)
{
	struct snd_pcm *pcm;
	int err;

	err = snd_pcm_new(loopback->card, "Loopback PCM", device,
			  substreams, substreams, &pcm);
	if (err < 0)
		return err;
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &loopback_pcm_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &loopback_pcm_ops);
	snd_pcm_set_managed_buffer_all(pcm, SNDRV_DMA_TYPE_VMALLOC, NULL, 0, 0);

	pcm->private_data = loopback;
	pcm->info_flags = 0;
	strcpy(pcm->name, "Loopback PCM");

	loopback->pcm[device] = pcm;
	return 0;
}

static int loopback_rate_shift_info(struct snd_kcontrol *kcontrol,   
				    struct snd_ctl_elem_info *uinfo) 
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 80000;
	uinfo->value.integer.max = 120000;
	uinfo->value.integer.step = 1;
	return 0;
}                                  

static int loopback_rate_shift_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct loopback *loopback = snd_kcontrol_chip(kcontrol);
	
	mutex_lock(&loopback->cable_lock);
	ucontrol->value.integer.value[0] =
		loopback->setup[kcontrol->id.subdevice]
			       [kcontrol->id.device].rate_shift;
	mutex_unlock(&loopback->cable_lock);
	return 0;
}

static int loopback_rate_shift_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct loopback *loopback = snd_kcontrol_chip(kcontrol);
	unsigned int val;
	int change = 0;

	val = ucontrol->value.integer.value[0];
	if (val < 80000)
		val = 80000;
	if (val > 120000)
		val = 120000;	
	mutex_lock(&loopback->cable_lock);
	if (val != loopback->setup[kcontrol->id.subdevice]
				  [kcontrol->id.device].rate_shift) {
		loopback->setup[kcontrol->id.subdevice]
			       [kcontrol->id.device].rate_shift = val;
		change = 1;
	}
	mutex_unlock(&loopback->cable_lock);
	return change;
}

static int loopback_notify_get(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct loopback *loopback = snd_kcontrol_chip(kcontrol);
	
	mutex_lock(&loopback->cable_lock);
	ucontrol->value.integer.value[0] =
		loopback->setup[kcontrol->id.subdevice]
			       [kcontrol->id.device].notify;
	mutex_unlock(&loopback->cable_lock);
	return 0;
}

static int loopback_notify_put(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct loopback *loopback = snd_kcontrol_chip(kcontrol);
	unsigned int val;
	int change = 0;

	val = ucontrol->value.integer.value[0] ? 1 : 0;
	mutex_lock(&loopback->cable_lock);
	if (val != loopback->setup[kcontrol->id.subdevice]
				[kcontrol->id.device].notify) {
		loopback->setup[kcontrol->id.subdevice]
			[kcontrol->id.device].notify = val;
		change = 1;
	}
	mutex_unlock(&loopback->cable_lock);
	return change;
}

static int loopback_active_get(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct loopback *loopback = snd_kcontrol_chip(kcontrol);
	struct loopback_cable *cable;

	unsigned int val = 0;

	mutex_lock(&loopback->cable_lock);
	cable = loopback->cables[kcontrol->id.subdevice][kcontrol->id.device ^ 1];
	if (cable != NULL) {
		unsigned int running = cable->running ^ cable->pause;

		val = (running & (1 << SNDRV_PCM_STREAM_PLAYBACK)) ? 1 : 0;
	}
	mutex_unlock(&loopback->cable_lock);
	ucontrol->value.integer.value[0] = val;
	return 0;
}

static int loopback_format_info(struct snd_kcontrol *kcontrol,   
				struct snd_ctl_elem_info *uinfo) 
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = (__force int)SNDRV_PCM_FORMAT_LAST;
	uinfo->value.integer.step = 1;
	return 0;
}                                  

static int loopback_format_get(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct loopback *loopback = snd_kcontrol_chip(kcontrol);
	
	ucontrol->value.integer.value[0] =
		(__force int)loopback->setup[kcontrol->id.subdevice]
			       [kcontrol->id.device].format;
	return 0;
}

static int loopback_rate_info(struct snd_kcontrol *kcontrol,   
			      struct snd_ctl_elem_info *uinfo) 
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 192000;
	uinfo->value.integer.step = 1;
	return 0;
}                                  

static int loopback_rate_get(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct loopback *loopback = snd_kcontrol_chip(kcontrol);
	
	mutex_lock(&loopback->cable_lock);
	ucontrol->value.integer.value[0] =
		loopback->setup[kcontrol->id.subdevice]
			       [kcontrol->id.device].rate;
	mutex_unlock(&loopback->cable_lock);
	return 0;
}

static int loopback_channels_info(struct snd_kcontrol *kcontrol,   
				  struct snd_ctl_elem_info *uinfo) 
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 1;
	uinfo->value.integer.max = 1024;
	uinfo->value.integer.step = 1;
	return 0;
}                                  

static int loopback_channels_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct loopback *loopback = snd_kcontrol_chip(kcontrol);
	
	mutex_lock(&loopback->cable_lock);
	ucontrol->value.integer.value[0] =
		loopback->setup[kcontrol->id.subdevice]
			       [kcontrol->id.device].channels;
	mutex_unlock(&loopback->cable_lock);
	return 0;
}

static const struct snd_kcontrol_new loopback_controls[]  = {
{
	.iface =        SNDRV_CTL_ELEM_IFACE_PCM,
	.name =         "PCM Rate Shift 100000",
	.info =         loopback_rate_shift_info,
	.get =          loopback_rate_shift_get,
	.put =          loopback_rate_shift_put,
},
{
	.iface =        SNDRV_CTL_ELEM_IFACE_PCM,
	.name =         "PCM Notify",
	.info =         snd_ctl_boolean_mono_info,
	.get =          loopback_notify_get,
	.put =          loopback_notify_put,
},
#define ACTIVE_IDX 2
{
	.access =	SNDRV_CTL_ELEM_ACCESS_READ,
	.iface =        SNDRV_CTL_ELEM_IFACE_PCM,
	.name =         "PCM Slave Active",
	.info =         snd_ctl_boolean_mono_info,
	.get =          loopback_active_get,
},
#define FORMAT_IDX 3
{
	.access =	SNDRV_CTL_ELEM_ACCESS_READ,
	.iface =        SNDRV_CTL_ELEM_IFACE_PCM,
	.name =         "PCM Slave Format",
	.info =         loopback_format_info,
	.get =          loopback_format_get
},
#define RATE_IDX 4
{
	.access =	SNDRV_CTL_ELEM_ACCESS_READ,
	.iface =        SNDRV_CTL_ELEM_IFACE_PCM,
	.name =         "PCM Slave Rate",
	.info =         loopback_rate_info,
	.get =          loopback_rate_get
},
#define CHANNELS_IDX 5
{
	.access =	SNDRV_CTL_ELEM_ACCESS_READ,
	.iface =        SNDRV_CTL_ELEM_IFACE_PCM,
	.name =         "PCM Slave Channels",
	.info =         loopback_channels_info,
	.get =          loopback_channels_get
}
};

static int loopback_mixer_new(struct loopback *loopback, int notify)
{
	struct snd_card *card = loopback->card;
	struct snd_pcm *pcm;
	struct snd_kcontrol *kctl;
	struct loopback_setup *setup;
	int err, dev, substr, substr_count, idx;

	strcpy(card->mixername, "Loopback Mixer");
	for (dev = 0; dev < 2; dev++) {
		pcm = loopback->pcm[dev];
		substr_count =
		    pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream_count;
		for (substr = 0; substr < substr_count; substr++) {
			setup = &loopback->setup[substr][dev];
			setup->notify = notify;
			setup->rate_shift = NO_PITCH;
			setup->format = SNDRV_PCM_FORMAT_S16_LE;
			setup->rate = 48000;
			setup->channels = 2;
			for (idx = 0; idx < ARRAY_SIZE(loopback_controls);
									idx++) {
				kctl = snd_ctl_new1(&loopback_controls[idx],
						    loopback);
				if (!kctl)
					return -ENOMEM;
				kctl->id.device = dev;
				kctl->id.subdevice = substr;

				/* Add the control before copying the id so that
				 * the numid field of the id is set in the copy.
				 */
				err = snd_ctl_add(card, kctl);
				if (err < 0)
					return err;

				switch (idx) {
				case ACTIVE_IDX:
					setup->active_id = kctl->id;
					break;
				case FORMAT_IDX:
					setup->format_id = kctl->id;
					break;
				case RATE_IDX:
					setup->rate_id = kctl->id;
					break;
				case CHANNELS_IDX:
					setup->channels_id = kctl->id;
					break;
				default:
					break;
				}
			}
		}
	}
	return 0;
}

static void print_dpcm_info(struct snd_info_buffer *buffer,
			    struct loopback_pcm *dpcm,
			    const char *id)
{
	snd_iprintf(buffer, "  %s\n", id);
	if (dpcm == NULL) {
		snd_iprintf(buffer, "    inactive\n");
		return;
	}
	snd_iprintf(buffer, "    buffer_size:\t%u\n", dpcm->pcm_buffer_size);
	snd_iprintf(buffer, "    buffer_pos:\t\t%u\n", dpcm->buf_pos);
	snd_iprintf(buffer, "    silent_size:\t%u\n", dpcm->silent_size);
	snd_iprintf(buffer, "    period_size:\t%u\n", dpcm->pcm_period_size);
	snd_iprintf(buffer, "    bytes_per_sec:\t%u\n", dpcm->pcm_bps);
	snd_iprintf(buffer, "    sample_align:\t%u\n", dpcm->pcm_salign);
	snd_iprintf(buffer, "    rate_shift:\t\t%u\n", dpcm->pcm_rate_shift);
	if (dpcm->cable->ops->dpcm_info)
		dpcm->cable->ops->dpcm_info(dpcm, buffer);
}

static void print_substream_info(struct snd_info_buffer *buffer,
				 struct loopback *loopback,
				 int sub,
				 int num)
{
	struct loopback_cable *cable = loopback->cables[sub][num];

	snd_iprintf(buffer, "Cable %i substream %i:\n", num, sub);
	if (cable == NULL) {
		snd_iprintf(buffer, "  inactive\n");
		return;
	}
	snd_iprintf(buffer, "  valid: %u\n", cable->valid);
	snd_iprintf(buffer, "  running: %u\n", cable->running);
	snd_iprintf(buffer, "  pause: %u\n", cable->pause);
	print_dpcm_info(buffer, cable->streams[0], "Playback");
	print_dpcm_info(buffer, cable->streams[1], "Capture");
}

static void print_cable_info(struct snd_info_entry *entry,
			     struct snd_info_buffer *buffer)
{
	struct loopback *loopback = entry->private_data;
	int sub, num;

	mutex_lock(&loopback->cable_lock);
	num = entry->name[strlen(entry->name)-1];
	num = num == '0' ? 0 : 1;
	for (sub = 0; sub < MAX_PCM_SUBSTREAMS; sub++)
		print_substream_info(buffer, loopback, sub, num);
	mutex_unlock(&loopback->cable_lock);
}

static int loopback_cable_proc_new(struct loopback *loopback, int cidx)
{
	char name[32];

	snprintf(name, sizeof(name), "cable#%d", cidx);
	return snd_card_ro_proc_new(loopback->card, name, loopback,
				    print_cable_info);
}

static void loopback_set_timer_source(struct loopback *loopback,
				      const char *value)
{
	if (loopback->timer_source) {
		devm_kfree(loopback->card->dev, loopback->timer_source);
		loopback->timer_source = NULL;
	}
	if (value && *value)
		loopback->timer_source = devm_kstrdup(loopback->card->dev,
						      value, GFP_KERNEL);
}

static void print_timer_source_info(struct snd_info_entry *entry,
				    struct snd_info_buffer *buffer)
{
	struct loopback *loopback = entry->private_data;

	mutex_lock(&loopback->cable_lock);
	snd_iprintf(buffer, "%s\n",
		    loopback->timer_source ? loopback->timer_source : "");
	mutex_unlock(&loopback->cable_lock);
}

static void change_timer_source_info(struct snd_info_entry *entry,
				     struct snd_info_buffer *buffer)
{
	struct loopback *loopback = entry->private_data;
	char line[64];

	mutex_lock(&loopback->cable_lock);
	if (!snd_info_get_line(buffer, line, sizeof(line)))
		loopback_set_timer_source(loopback, strim(line));
	mutex_unlock(&loopback->cable_lock);
}

static int loopback_timer_source_proc_new(struct loopback *loopback)
{
	return snd_card_rw_proc_new(loopback->card, "timer_source", loopback,
				    print_timer_source_info,
				    change_timer_source_info);
}

static int loopback_probe(struct platform_device *devptr)
{
	struct snd_card *card;
	struct loopback *loopback;
	int dev = devptr->id;
	int err;

	err = snd_devm_card_new(&devptr->dev, index[dev], id[dev], THIS_MODULE,
				sizeof(struct loopback), &card);
	if (err < 0)
		return err;
	loopback = card->private_data;

	if (pcm_substreams[dev] < 1)
		pcm_substreams[dev] = 1;
	if (pcm_substreams[dev] > MAX_PCM_SUBSTREAMS)
		pcm_substreams[dev] = MAX_PCM_SUBSTREAMS;
	
	loopback->card = card;
	loopback_set_timer_source(loopback, timer_source[dev]);

	mutex_init(&loopback->cable_lock);

	err = loopback_pcm_new(loopback, 0, pcm_substreams[dev]);
	if (err < 0)
		return err;
	err = loopback_pcm_new(loopback, 1, pcm_substreams[dev]);
	if (err < 0)
		return err;
	err = loopback_mixer_new(loopback, pcm_notify[dev] ? 1 : 0);
	if (err < 0)
		return err;
	loopback_cable_proc_new(loopback, 0);
	loopback_cable_proc_new(loopback, 1);
	loopback_timer_source_proc_new(loopback);
	strcpy(card->driver, "Loopback");
	strcpy(card->shortname, "Loopback");
	sprintf(card->longname, "Loopback %i", dev + 1);
	err = snd_card_register(card);
	if (err < 0)
		return err;
	platform_set_drvdata(devptr, card);
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int loopback_suspend(struct device *pdev)
{
	struct snd_card *card = dev_get_drvdata(pdev);

	snd_power_change_state(card, SNDRV_CTL_POWER_D3hot);
	return 0;
}
	
static int loopback_resume(struct device *pdev)
{
	struct snd_card *card = dev_get_drvdata(pdev);

	snd_power_change_state(card, SNDRV_CTL_POWER_D0);
	return 0;
}

static SIMPLE_DEV_PM_OPS(loopback_pm, loopback_suspend, loopback_resume);
#define LOOPBACK_PM_OPS	&loopback_pm
#else
#define LOOPBACK_PM_OPS	NULL
#endif

#define SND_LOOPBACK_DRIVER	"snd_aloop"

static struct platform_driver loopback_driver = {
	.probe		= loopback_probe,
	.driver		= {
		.name	= SND_LOOPBACK_DRIVER,
		.pm	= LOOPBACK_PM_OPS,
	},
};

static void loopback_unregister_all(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(devices); ++i)
		platform_device_unregister(devices[i]);
	platform_driver_unregister(&loopback_driver);
}

static int __init alsa_card_loopback_init(void)
{
	int i, err, cards;

	err = platform_driver_register(&loopback_driver);
	if (err < 0)
		return err;


	cards = 0;
	for (i = 0; i < SNDRV_CARDS; i++) {
		struct platform_device *device;
		if (!enable[i])
			continue;
		device = platform_device_register_simple(SND_LOOPBACK_DRIVER,
							 i, NULL, 0);
		if (IS_ERR(device))
			continue;
		if (!platform_get_drvdata(device)) {
			platform_device_unregister(device);
			continue;
		}
		devices[i] = device;
		cards++;
	}
	if (!cards) {
#ifdef MODULE
		printk(KERN_ERR "aloop: No loopback enabled\n");
#endif
		loopback_unregister_all();
		return -ENODEV;
	}
	return 0;
}

static void __exit alsa_card_loopback_exit(void)
{
	loopback_unregister_all();
}

module_init(alsa_card_loopback_init)
module_exit(alsa_card_loopback_exit)
