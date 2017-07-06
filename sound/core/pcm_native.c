/*
 *  Digital Audio (PCM) abstract layer
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
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
 */

#include <linux/mm.h>
#include <linux/module.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/sched/signal.h>
#include <linux/time.h>
#include <linux/pm_qos.h>
#include <linux/io.h>
#include <linux/dma-mapping.h>
#include <sound/core.h>
#include <sound/control.h>
#include <sound/info.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/timer.h>
#include <sound/minors.h>
#include <linux/uio.h>

/*
 *  Compatibility
 */

struct snd_pcm_hw_params_old {
	unsigned int flags;
	unsigned int masks[SNDRV_PCM_HW_PARAM_SUBFORMAT -
			   SNDRV_PCM_HW_PARAM_ACCESS + 1];
	struct snd_interval intervals[SNDRV_PCM_HW_PARAM_TICK_TIME -
					SNDRV_PCM_HW_PARAM_SAMPLE_BITS + 1];
	unsigned int rmask;
	unsigned int cmask;
	unsigned int info;
	unsigned int msbits;
	unsigned int rate_num;
	unsigned int rate_den;
	snd_pcm_uframes_t fifo_size;
	unsigned char reserved[64];
};

#ifdef CONFIG_SND_SUPPORT_OLD_API
#define SNDRV_PCM_IOCTL_HW_REFINE_OLD _IOWR('A', 0x10, struct snd_pcm_hw_params_old)
#define SNDRV_PCM_IOCTL_HW_PARAMS_OLD _IOWR('A', 0x11, struct snd_pcm_hw_params_old)

static int snd_pcm_hw_refine_old_user(struct snd_pcm_substream *substream,
				      struct snd_pcm_hw_params_old __user * _oparams);
static int snd_pcm_hw_params_old_user(struct snd_pcm_substream *substream,
				      struct snd_pcm_hw_params_old __user * _oparams);
#endif
static int snd_pcm_open(struct file *file, struct snd_pcm *pcm, int stream);

/*
 *
 */

static DEFINE_RWLOCK(snd_pcm_link_rwlock);
static DECLARE_RWSEM(snd_pcm_link_rwsem);

/* Writer in rwsem may block readers even during its waiting in queue,
 * and this may lead to a deadlock when the code path takes read sem
 * twice (e.g. one in snd_pcm_action_nonatomic() and another in
 * snd_pcm_stream_lock()).  As a (suboptimal) workaround, let writer to
 * spin until it gets the lock.
 */
static inline void down_write_nonblock(struct rw_semaphore *lock)
{
	while (!down_write_trylock(lock))
		cond_resched();
}

/**
 * snd_pcm_stream_lock - Lock the PCM stream
 * @substream: PCM substream
 *
 * This locks the PCM stream's spinlock or mutex depending on the nonatomic
 * flag of the given substream.  This also takes the global link rw lock
 * (or rw sem), too, for avoiding the race with linked streams.
 */
void snd_pcm_stream_lock(struct snd_pcm_substream *substream)
{
	if (substream->pcm->nonatomic) {
		down_read_nested(&snd_pcm_link_rwsem, SINGLE_DEPTH_NESTING);
		mutex_lock(&substream->self_group.mutex);
	} else {
		read_lock(&snd_pcm_link_rwlock);
		spin_lock(&substream->self_group.lock);
	}
}
EXPORT_SYMBOL_GPL(snd_pcm_stream_lock);

/**
 * snd_pcm_stream_lock - Unlock the PCM stream
 * @substream: PCM substream
 *
 * This unlocks the PCM stream that has been locked via snd_pcm_stream_lock().
 */
void snd_pcm_stream_unlock(struct snd_pcm_substream *substream)
{
	if (substream->pcm->nonatomic) {
		mutex_unlock(&substream->self_group.mutex);
		up_read(&snd_pcm_link_rwsem);
	} else {
		spin_unlock(&substream->self_group.lock);
		read_unlock(&snd_pcm_link_rwlock);
	}
}
EXPORT_SYMBOL_GPL(snd_pcm_stream_unlock);

/**
 * snd_pcm_stream_lock_irq - Lock the PCM stream
 * @substream: PCM substream
 *
 * This locks the PCM stream like snd_pcm_stream_lock() and disables the local
 * IRQ (only when nonatomic is false).  In nonatomic case, this is identical
 * as snd_pcm_stream_lock().
 */
void snd_pcm_stream_lock_irq(struct snd_pcm_substream *substream)
{
	if (!substream->pcm->nonatomic)
		local_irq_disable();
	snd_pcm_stream_lock(substream);
}
EXPORT_SYMBOL_GPL(snd_pcm_stream_lock_irq);

/**
 * snd_pcm_stream_unlock_irq - Unlock the PCM stream
 * @substream: PCM substream
 *
 * This is a counter-part of snd_pcm_stream_lock_irq().
 */
void snd_pcm_stream_unlock_irq(struct snd_pcm_substream *substream)
{
	snd_pcm_stream_unlock(substream);
	if (!substream->pcm->nonatomic)
		local_irq_enable();
}
EXPORT_SYMBOL_GPL(snd_pcm_stream_unlock_irq);

unsigned long _snd_pcm_stream_lock_irqsave(struct snd_pcm_substream *substream)
{
	unsigned long flags = 0;
	if (!substream->pcm->nonatomic)
		local_irq_save(flags);
	snd_pcm_stream_lock(substream);
	return flags;
}
EXPORT_SYMBOL_GPL(_snd_pcm_stream_lock_irqsave);

/**
 * snd_pcm_stream_unlock_irqrestore - Unlock the PCM stream
 * @substream: PCM substream
 * @flags: irq flags
 *
 * This is a counter-part of snd_pcm_stream_lock_irqsave().
 */
void snd_pcm_stream_unlock_irqrestore(struct snd_pcm_substream *substream,
				      unsigned long flags)
{
	snd_pcm_stream_unlock(substream);
	if (!substream->pcm->nonatomic)
		local_irq_restore(flags);
}
EXPORT_SYMBOL_GPL(snd_pcm_stream_unlock_irqrestore);

static inline mm_segment_t snd_enter_user(void)
{
	mm_segment_t fs = get_fs();
	set_fs(get_ds());
	return fs;
}

static inline void snd_leave_user(mm_segment_t fs)
{
	set_fs(fs);
}



int snd_pcm_info(struct snd_pcm_substream *substream, struct snd_pcm_info *info)
{
	struct snd_pcm_runtime *runtime;
	struct snd_pcm *pcm = substream->pcm;
	struct snd_pcm_str *pstr = substream->pstr;

	memset(info, 0, sizeof(*info));
	info->card = pcm->card->number;
	info->device = pcm->device;
	info->stream = substream->stream;
	info->subdevice = substream->number;
	strlcpy(info->id, pcm->id, sizeof(info->id));
	strlcpy(info->name, pcm->name, sizeof(info->name));
	info->dev_class = pcm->dev_class;
	info->dev_subclass = pcm->dev_subclass;
	info->subdevices_count = pstr->substream_count;
	info->subdevices_avail = pstr->substream_count - pstr->substream_opened;
	strlcpy(info->subname, substream->name, sizeof(info->subname));
	runtime = substream->runtime;
	/* AB: FIXME!!! This is definitely nonsense */
	if (runtime) {
		info->sync = runtime->sync;
		substream->ops->ioctl(substream, SNDRV_PCM_IOCTL1_INFO, info);
	}
	return 0;
}

int snd_pcm_info_user(struct snd_pcm_substream *substream,
		      struct snd_pcm_info __user * _info)
{
	struct snd_pcm_info *info;
	int err;

	info = kmalloc(sizeof(*info), GFP_KERNEL);
	if (! info)
		return -ENOMEM;
	err = snd_pcm_info(substream, info);
	if (err >= 0) {
		if (copy_to_user(_info, info, sizeof(*info)))
			err = -EFAULT;
	}
	kfree(info);
	return err;
}

static bool hw_support_mmap(struct snd_pcm_substream *substream)
{
	if (!(substream->runtime->hw.info & SNDRV_PCM_INFO_MMAP))
		return false;
	/* check architectures that return -EINVAL from dma_mmap_coherent() */
	/* FIXME: this should be some global flag */
#if defined(CONFIG_C6X) || defined(CONFIG_FRV) || defined(CONFIG_MN10300) ||\
	defined(CONFIG_PARISC) || defined(CONFIG_XTENSA)
	if (!substream->ops->mmap &&
	    substream->dma_buffer.dev.type == SNDRV_DMA_TYPE_DEV)
		return false;
#endif
	return true;
}

#undef RULES_DEBUG

#ifdef RULES_DEBUG
#define HW_PARAM(v) [SNDRV_PCM_HW_PARAM_##v] = #v
static const char * const snd_pcm_hw_param_names[] = {
	HW_PARAM(ACCESS),
	HW_PARAM(FORMAT),
	HW_PARAM(SUBFORMAT),
	HW_PARAM(SAMPLE_BITS),
	HW_PARAM(FRAME_BITS),
	HW_PARAM(CHANNELS),
	HW_PARAM(RATE),
	HW_PARAM(PERIOD_TIME),
	HW_PARAM(PERIOD_SIZE),
	HW_PARAM(PERIOD_BYTES),
	HW_PARAM(PERIODS),
	HW_PARAM(BUFFER_TIME),
	HW_PARAM(BUFFER_SIZE),
	HW_PARAM(BUFFER_BYTES),
	HW_PARAM(TICK_TIME),
};
#endif

int snd_pcm_hw_refine(struct snd_pcm_substream *substream, 
		      struct snd_pcm_hw_params *params)
{
	unsigned int k;
	struct snd_pcm_hardware *hw;
	struct snd_interval *i = NULL;
	struct snd_mask *m = NULL;
	struct snd_pcm_hw_constraints *constrs = &substream->runtime->hw_constraints;
	unsigned int rstamps[constrs->rules_num];
	unsigned int vstamps[SNDRV_PCM_HW_PARAM_LAST_INTERVAL + 1];
	unsigned int stamp = 2;
	int changed, again;

	params->info = 0;
	params->fifo_size = 0;
	if (params->rmask & (1 << SNDRV_PCM_HW_PARAM_SAMPLE_BITS))
		params->msbits = 0;
	if (params->rmask & (1 << SNDRV_PCM_HW_PARAM_RATE)) {
		params->rate_num = 0;
		params->rate_den = 0;
	}

	for (k = SNDRV_PCM_HW_PARAM_FIRST_MASK; k <= SNDRV_PCM_HW_PARAM_LAST_MASK; k++) {
		m = hw_param_mask(params, k);
		if (snd_mask_empty(m))
			return -EINVAL;
		if (!(params->rmask & (1 << k)))
			continue;
#ifdef RULES_DEBUG
		pr_debug("%s = ", snd_pcm_hw_param_names[k]);
		pr_cont("%04x%04x%04x%04x -> ", m->bits[3], m->bits[2], m->bits[1], m->bits[0]);
#endif
		changed = snd_mask_refine(m, constrs_mask(constrs, k));
#ifdef RULES_DEBUG
		pr_cont("%04x%04x%04x%04x\n", m->bits[3], m->bits[2], m->bits[1], m->bits[0]);
#endif
		if (changed)
			params->cmask |= 1 << k;
		if (changed < 0)
			return changed;
	}

	for (k = SNDRV_PCM_HW_PARAM_FIRST_INTERVAL; k <= SNDRV_PCM_HW_PARAM_LAST_INTERVAL; k++) {
		i = hw_param_interval(params, k);
		if (snd_interval_empty(i))
			return -EINVAL;
		if (!(params->rmask & (1 << k)))
			continue;
#ifdef RULES_DEBUG
		pr_debug("%s = ", snd_pcm_hw_param_names[k]);
		if (i->empty)
			pr_cont("empty");
		else
			pr_cont("%c%u %u%c",
			       i->openmin ? '(' : '[', i->min,
			       i->max, i->openmax ? ')' : ']');
		pr_cont(" -> ");
#endif
		changed = snd_interval_refine(i, constrs_interval(constrs, k));
#ifdef RULES_DEBUG
		if (i->empty)
			pr_cont("empty\n");
		else 
			pr_cont("%c%u %u%c\n",
			       i->openmin ? '(' : '[', i->min,
			       i->max, i->openmax ? ')' : ']');
#endif
		if (changed)
			params->cmask |= 1 << k;
		if (changed < 0)
			return changed;
	}

	for (k = 0; k < constrs->rules_num; k++)
		rstamps[k] = 0;
	for (k = 0; k <= SNDRV_PCM_HW_PARAM_LAST_INTERVAL; k++) 
		vstamps[k] = (params->rmask & (1 << k)) ? 1 : 0;
	do {
		again = 0;
		for (k = 0; k < constrs->rules_num; k++) {
			struct snd_pcm_hw_rule *r = &constrs->rules[k];
			unsigned int d;
			int doit = 0;
			if (r->cond && !(r->cond & params->flags))
				continue;
			for (d = 0; r->deps[d] >= 0; d++) {
				if (vstamps[r->deps[d]] > rstamps[k]) {
					doit = 1;
					break;
				}
			}
			if (!doit)
				continue;
#ifdef RULES_DEBUG
			pr_debug("Rule %d [%p]: ", k, r->func);
			if (r->var >= 0) {
				pr_cont("%s = ", snd_pcm_hw_param_names[r->var]);
				if (hw_is_mask(r->var)) {
					m = hw_param_mask(params, r->var);
					pr_cont("%x", *m->bits);
				} else {
					i = hw_param_interval(params, r->var);
					if (i->empty)
						pr_cont("empty");
					else
						pr_cont("%c%u %u%c",
						       i->openmin ? '(' : '[', i->min,
						       i->max, i->openmax ? ')' : ']');
				}
			}
#endif
			changed = r->func(params, r);
#ifdef RULES_DEBUG
			if (r->var >= 0) {
				pr_cont(" -> ");
				if (hw_is_mask(r->var))
					pr_cont("%x", *m->bits);
				else {
					if (i->empty)
						pr_cont("empty");
					else
						pr_cont("%c%u %u%c",
						       i->openmin ? '(' : '[', i->min,
						       i->max, i->openmax ? ')' : ']');
				}
			}
			pr_cont("\n");
#endif
			rstamps[k] = stamp;
			if (changed && r->var >= 0) {
				params->cmask |= (1 << r->var);
				vstamps[r->var] = stamp;
				again = 1;
			}
			if (changed < 0)
				return changed;
			stamp++;
		}
	} while (again);
	if (!params->msbits) {
		i = hw_param_interval(params, SNDRV_PCM_HW_PARAM_SAMPLE_BITS);
		if (snd_interval_single(i))
			params->msbits = snd_interval_value(i);
	}

	if (!params->rate_den) {
		i = hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);
		if (snd_interval_single(i)) {
			params->rate_num = snd_interval_value(i);
			params->rate_den = 1;
		}
	}

	hw = &substream->runtime->hw;
	if (!params->info) {
		params->info = hw->info & ~(SNDRV_PCM_INFO_FIFO_IN_FRAMES |
					    SNDRV_PCM_INFO_DRAIN_TRIGGER);
		if (!hw_support_mmap(substream))
			params->info &= ~(SNDRV_PCM_INFO_MMAP |
					  SNDRV_PCM_INFO_MMAP_VALID);
	}
	if (!params->fifo_size) {
		m = hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT);
		i = hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
		if (snd_mask_min(m) == snd_mask_max(m) &&
                    snd_interval_min(i) == snd_interval_max(i)) {
			changed = substream->ops->ioctl(substream,
					SNDRV_PCM_IOCTL1_FIFO_SIZE, params);
			if (changed < 0)
				return changed;
		}
	}
	params->rmask = 0;
	return 0;
}

EXPORT_SYMBOL(snd_pcm_hw_refine);

static int snd_pcm_hw_refine_user(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params __user * _params)
{
	struct snd_pcm_hw_params *params;
	int err;

	params = memdup_user(_params, sizeof(*params));
	if (IS_ERR(params))
		return PTR_ERR(params);

	err = snd_pcm_hw_refine(substream, params);
	if (copy_to_user(_params, params, sizeof(*params))) {
		if (!err)
			err = -EFAULT;
	}

	kfree(params);
	return err;
}

static int period_to_usecs(struct snd_pcm_runtime *runtime)
{
	int usecs;

	if (! runtime->rate)
		return -1; /* invalid */

	/* take 75% of period time as the deadline */
	usecs = (750000 / runtime->rate) * runtime->period_size;
	usecs += ((750000 % runtime->rate) * runtime->period_size) /
		runtime->rate;

	return usecs;
}

static void snd_pcm_set_state(struct snd_pcm_substream *substream, int state)
{
	snd_pcm_stream_lock_irq(substream);
	if (substream->runtime->status->state != SNDRV_PCM_STATE_DISCONNECTED)
		substream->runtime->status->state = state;
	snd_pcm_stream_unlock_irq(substream);
}

static inline void snd_pcm_timer_notify(struct snd_pcm_substream *substream,
					int event)
{
#ifdef CONFIG_SND_PCM_TIMER
	if (substream->timer)
		snd_timer_notify(substream->timer, event,
					&substream->runtime->trigger_tstamp);
#endif
}

static int snd_pcm_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params)
{
	struct snd_pcm_runtime *runtime;
	int err, usecs;
	unsigned int bits;
	snd_pcm_uframes_t frames;

	if (PCM_RUNTIME_CHECK(substream))
		return -ENXIO;
	runtime = substream->runtime;
	snd_pcm_stream_lock_irq(substream);
	switch (runtime->status->state) {
	case SNDRV_PCM_STATE_OPEN:
	case SNDRV_PCM_STATE_SETUP:
	case SNDRV_PCM_STATE_PREPARED:
		break;
	default:
		snd_pcm_stream_unlock_irq(substream);
		return -EBADFD;
	}
	snd_pcm_stream_unlock_irq(substream);
#if IS_ENABLED(CONFIG_SND_PCM_OSS)
	if (!substream->oss.oss)
#endif
		if (atomic_read(&substream->mmap_count))
			return -EBADFD;

	params->rmask = ~0U;
	err = snd_pcm_hw_refine(substream, params);
	if (err < 0)
		goto _error;

	err = snd_pcm_hw_params_choose(substream, params);
	if (err < 0)
		goto _error;

	if (substream->ops->hw_params != NULL) {
		err = substream->ops->hw_params(substream, params);
		if (err < 0)
			goto _error;
	}

	runtime->access = params_access(params);
	runtime->format = params_format(params);
	runtime->subformat = params_subformat(params);
	runtime->channels = params_channels(params);
	runtime->rate = params_rate(params);
	runtime->period_size = params_period_size(params);
	runtime->periods = params_periods(params);
	runtime->buffer_size = params_buffer_size(params);
	runtime->info = params->info;
	runtime->rate_num = params->rate_num;
	runtime->rate_den = params->rate_den;
	runtime->no_period_wakeup =
			(params->info & SNDRV_PCM_INFO_NO_PERIOD_WAKEUP) &&
			(params->flags & SNDRV_PCM_HW_PARAMS_NO_PERIOD_WAKEUP);

	bits = snd_pcm_format_physical_width(runtime->format);
	runtime->sample_bits = bits;
	bits *= runtime->channels;
	runtime->frame_bits = bits;
	frames = 1;
	while (bits % 8 != 0) {
		bits *= 2;
		frames *= 2;
	}
	runtime->byte_align = bits / 8;
	runtime->min_align = frames;

	/* Default sw params */
	runtime->tstamp_mode = SNDRV_PCM_TSTAMP_NONE;
	runtime->period_step = 1;
	runtime->control->avail_min = runtime->period_size;
	runtime->start_threshold = 1;
	runtime->stop_threshold = runtime->buffer_size;
	runtime->silence_threshold = 0;
	runtime->silence_size = 0;
	runtime->boundary = runtime->buffer_size;
	while (runtime->boundary * 2 <= LONG_MAX - runtime->buffer_size)
		runtime->boundary *= 2;

	snd_pcm_timer_resolution_change(substream);
	snd_pcm_set_state(substream, SNDRV_PCM_STATE_SETUP);

	if (pm_qos_request_active(&substream->latency_pm_qos_req))
		pm_qos_remove_request(&substream->latency_pm_qos_req);
	if ((usecs = period_to_usecs(runtime)) >= 0)
		pm_qos_add_request(&substream->latency_pm_qos_req,
				   PM_QOS_CPU_DMA_LATENCY, usecs);
	return 0;
 _error:
	/* hardware might be unusable from this time,
	   so we force application to retry to set
	   the correct hardware parameter settings */
	snd_pcm_set_state(substream, SNDRV_PCM_STATE_OPEN);
	if (substream->ops->hw_free != NULL)
		substream->ops->hw_free(substream);
	return err;
}

static int snd_pcm_hw_params_user(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params __user * _params)
{
	struct snd_pcm_hw_params *params;
	int err;

	params = memdup_user(_params, sizeof(*params));
	if (IS_ERR(params))
		return PTR_ERR(params);

	err = snd_pcm_hw_params(substream, params);
	if (copy_to_user(_params, params, sizeof(*params))) {
		if (!err)
			err = -EFAULT;
	}

	kfree(params);
	return err;
}

static int snd_pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime;
	int result = 0;

	if (PCM_RUNTIME_CHECK(substream))
		return -ENXIO;
	runtime = substream->runtime;
	snd_pcm_stream_lock_irq(substream);
	switch (runtime->status->state) {
	case SNDRV_PCM_STATE_SETUP:
	case SNDRV_PCM_STATE_PREPARED:
		break;
	default:
		snd_pcm_stream_unlock_irq(substream);
		return -EBADFD;
	}
	snd_pcm_stream_unlock_irq(substream);
	if (atomic_read(&substream->mmap_count))
		return -EBADFD;
	if (substream->ops->hw_free)
		result = substream->ops->hw_free(substream);
	snd_pcm_set_state(substream, SNDRV_PCM_STATE_OPEN);
	pm_qos_remove_request(&substream->latency_pm_qos_req);
	return result;
}

static int snd_pcm_sw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_sw_params *params)
{
	struct snd_pcm_runtime *runtime;
	int err;

	if (PCM_RUNTIME_CHECK(substream))
		return -ENXIO;
	runtime = substream->runtime;
	snd_pcm_stream_lock_irq(substream);
	if (runtime->status->state == SNDRV_PCM_STATE_OPEN) {
		snd_pcm_stream_unlock_irq(substream);
		return -EBADFD;
	}
	snd_pcm_stream_unlock_irq(substream);

	if (params->tstamp_mode < 0 ||
	    params->tstamp_mode > SNDRV_PCM_TSTAMP_LAST)
		return -EINVAL;
	if (params->proto >= SNDRV_PROTOCOL_VERSION(2, 0, 12) &&
	    params->tstamp_type > SNDRV_PCM_TSTAMP_TYPE_LAST)
		return -EINVAL;
	if (params->avail_min == 0)
		return -EINVAL;
	if (params->silence_size >= runtime->boundary) {
		if (params->silence_threshold != 0)
			return -EINVAL;
	} else {
		if (params->silence_size > params->silence_threshold)
			return -EINVAL;
		if (params->silence_threshold > runtime->buffer_size)
			return -EINVAL;
	}
	err = 0;
	snd_pcm_stream_lock_irq(substream);
	runtime->tstamp_mode = params->tstamp_mode;
	if (params->proto >= SNDRV_PROTOCOL_VERSION(2, 0, 12))
		runtime->tstamp_type = params->tstamp_type;
	runtime->period_step = params->period_step;
	runtime->control->avail_min = params->avail_min;
	runtime->start_threshold = params->start_threshold;
	runtime->stop_threshold = params->stop_threshold;
	runtime->silence_threshold = params->silence_threshold;
	runtime->silence_size = params->silence_size;
        params->boundary = runtime->boundary;
	if (snd_pcm_running(substream)) {
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK &&
		    runtime->silence_size > 0)
			snd_pcm_playback_silence(substream, ULONG_MAX);
		err = snd_pcm_update_state(substream, runtime);
	}
	snd_pcm_stream_unlock_irq(substream);
	return err;
}

static int snd_pcm_sw_params_user(struct snd_pcm_substream *substream,
				  struct snd_pcm_sw_params __user * _params)
{
	struct snd_pcm_sw_params params;
	int err;
	if (copy_from_user(&params, _params, sizeof(params)))
		return -EFAULT;
	err = snd_pcm_sw_params(substream, &params);
	if (copy_to_user(_params, &params, sizeof(params)))
		return -EFAULT;
	return err;
}

int snd_pcm_status(struct snd_pcm_substream *substream,
		   struct snd_pcm_status *status)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	snd_pcm_stream_lock_irq(substream);

	snd_pcm_unpack_audio_tstamp_config(status->audio_tstamp_data,
					&runtime->audio_tstamp_config);

	/* backwards compatible behavior */
	if (runtime->audio_tstamp_config.type_requested ==
		SNDRV_PCM_AUDIO_TSTAMP_TYPE_COMPAT) {
		if (runtime->hw.info & SNDRV_PCM_INFO_HAS_WALL_CLOCK)
			runtime->audio_tstamp_config.type_requested =
				SNDRV_PCM_AUDIO_TSTAMP_TYPE_LINK;
		else
			runtime->audio_tstamp_config.type_requested =
				SNDRV_PCM_AUDIO_TSTAMP_TYPE_DEFAULT;
		runtime->audio_tstamp_report.valid = 0;
	} else
		runtime->audio_tstamp_report.valid = 1;

	status->state = runtime->status->state;
	status->suspended_state = runtime->status->suspended_state;
	if (status->state == SNDRV_PCM_STATE_OPEN)
		goto _end;
	status->trigger_tstamp = runtime->trigger_tstamp;
	if (snd_pcm_running(substream)) {
		snd_pcm_update_hw_ptr(substream);
		if (runtime->tstamp_mode == SNDRV_PCM_TSTAMP_ENABLE) {
			status->tstamp = runtime->status->tstamp;
			status->driver_tstamp = runtime->driver_tstamp;
			status->audio_tstamp =
				runtime->status->audio_tstamp;
			if (runtime->audio_tstamp_report.valid == 1)
				/* backwards compatibility, no report provided in COMPAT mode */
				snd_pcm_pack_audio_tstamp_report(&status->audio_tstamp_data,
								&status->audio_tstamp_accuracy,
								&runtime->audio_tstamp_report);

			goto _tstamp_end;
		}
	} else {
		/* get tstamp only in fallback mode and only if enabled */
		if (runtime->tstamp_mode == SNDRV_PCM_TSTAMP_ENABLE)
			snd_pcm_gettime(runtime, &status->tstamp);
	}
 _tstamp_end:
	status->appl_ptr = runtime->control->appl_ptr;
	status->hw_ptr = runtime->status->hw_ptr;
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		status->avail = snd_pcm_playback_avail(runtime);
		if (runtime->status->state == SNDRV_PCM_STATE_RUNNING ||
		    runtime->status->state == SNDRV_PCM_STATE_DRAINING) {
			status->delay = runtime->buffer_size - status->avail;
			status->delay += runtime->delay;
		} else
			status->delay = 0;
	} else {
		status->avail = snd_pcm_capture_avail(runtime);
		if (runtime->status->state == SNDRV_PCM_STATE_RUNNING)
			status->delay = status->avail + runtime->delay;
		else
			status->delay = 0;
	}
	status->avail_max = runtime->avail_max;
	status->overrange = runtime->overrange;
	runtime->avail_max = 0;
	runtime->overrange = 0;
 _end:
 	snd_pcm_stream_unlock_irq(substream);
	return 0;
}

static int snd_pcm_status_user(struct snd_pcm_substream *substream,
			       struct snd_pcm_status __user * _status,
			       bool ext)
{
	struct snd_pcm_status status;
	int res;

	memset(&status, 0, sizeof(status));
	/*
	 * with extension, parameters are read/write,
	 * get audio_tstamp_data from user,
	 * ignore rest of status structure
	 */
	if (ext && get_user(status.audio_tstamp_data,
				(u32 __user *)(&_status->audio_tstamp_data)))
		return -EFAULT;
	res = snd_pcm_status(substream, &status);
	if (res < 0)
		return res;
	if (copy_to_user(_status, &status, sizeof(status)))
		return -EFAULT;
	return 0;
}

static int snd_pcm_channel_info(struct snd_pcm_substream *substream,
				struct snd_pcm_channel_info * info)
{
	struct snd_pcm_runtime *runtime;
	unsigned int channel;
	
	channel = info->channel;
	runtime = substream->runtime;
	snd_pcm_stream_lock_irq(substream);
	if (runtime->status->state == SNDRV_PCM_STATE_OPEN) {
		snd_pcm_stream_unlock_irq(substream);
		return -EBADFD;
	}
	snd_pcm_stream_unlock_irq(substream);
	if (channel >= runtime->channels)
		return -EINVAL;
	memset(info, 0, sizeof(*info));
	info->channel = channel;
	return substream->ops->ioctl(substream, SNDRV_PCM_IOCTL1_CHANNEL_INFO, info);
}

static int snd_pcm_channel_info_user(struct snd_pcm_substream *substream,
				     struct snd_pcm_channel_info __user * _info)
{
	struct snd_pcm_channel_info info;
	int res;
	
	if (copy_from_user(&info, _info, sizeof(info)))
		return -EFAULT;
	res = snd_pcm_channel_info(substream, &info);
	if (res < 0)
		return res;
	if (copy_to_user(_info, &info, sizeof(info)))
		return -EFAULT;
	return 0;
}

static void snd_pcm_trigger_tstamp(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	if (runtime->trigger_master == NULL)
		return;
	if (runtime->trigger_master == substream) {
		if (!runtime->trigger_tstamp_latched)
			snd_pcm_gettime(runtime, &runtime->trigger_tstamp);
	} else {
		snd_pcm_trigger_tstamp(runtime->trigger_master);
		runtime->trigger_tstamp = runtime->trigger_master->runtime->trigger_tstamp;
	}
	runtime->trigger_master = NULL;
}

struct action_ops {
	int (*pre_action)(struct snd_pcm_substream *substream, int state);
	int (*do_action)(struct snd_pcm_substream *substream, int state);
	void (*undo_action)(struct snd_pcm_substream *substream, int state);
	void (*post_action)(struct snd_pcm_substream *substream, int state);
};

/*
 *  this functions is core for handling of linked stream
 *  Note: the stream state might be changed also on failure
 *  Note2: call with calling stream lock + link lock
 */
static int snd_pcm_action_group(const struct action_ops *ops,
				struct snd_pcm_substream *substream,
				int state, int do_lock)
{
	struct snd_pcm_substream *s = NULL;
	struct snd_pcm_substream *s1;
	int res = 0, depth = 1;

	snd_pcm_group_for_each_entry(s, substream) {
		if (do_lock && s != substream) {
			if (s->pcm->nonatomic)
				mutex_lock_nested(&s->self_group.mutex, depth);
			else
				spin_lock_nested(&s->self_group.lock, depth);
			depth++;
		}
		res = ops->pre_action(s, state);
		if (res < 0)
			goto _unlock;
	}
	snd_pcm_group_for_each_entry(s, substream) {
		res = ops->do_action(s, state);
		if (res < 0) {
			if (ops->undo_action) {
				snd_pcm_group_for_each_entry(s1, substream) {
					if (s1 == s) /* failed stream */
						break;
					ops->undo_action(s1, state);
				}
			}
			s = NULL; /* unlock all */
			goto _unlock;
		}
	}
	snd_pcm_group_for_each_entry(s, substream) {
		ops->post_action(s, state);
	}
 _unlock:
	if (do_lock) {
		/* unlock streams */
		snd_pcm_group_for_each_entry(s1, substream) {
			if (s1 != substream) {
				if (s1->pcm->nonatomic)
					mutex_unlock(&s1->self_group.mutex);
				else
					spin_unlock(&s1->self_group.lock);
			}
			if (s1 == s)	/* end */
				break;
		}
	}
	return res;
}

/*
 *  Note: call with stream lock
 */
static int snd_pcm_action_single(const struct action_ops *ops,
				 struct snd_pcm_substream *substream,
				 int state)
{
	int res;
	
	res = ops->pre_action(substream, state);
	if (res < 0)
		return res;
	res = ops->do_action(substream, state);
	if (res == 0)
		ops->post_action(substream, state);
	else if (ops->undo_action)
		ops->undo_action(substream, state);
	return res;
}

/*
 *  Note: call with stream lock
 */
static int snd_pcm_action(const struct action_ops *ops,
			  struct snd_pcm_substream *substream,
			  int state)
{
	int res;

	if (!snd_pcm_stream_linked(substream))
		return snd_pcm_action_single(ops, substream, state);

	if (substream->pcm->nonatomic) {
		if (!mutex_trylock(&substream->group->mutex)) {
			mutex_unlock(&substream->self_group.mutex);
			mutex_lock(&substream->group->mutex);
			mutex_lock(&substream->self_group.mutex);
		}
		res = snd_pcm_action_group(ops, substream, state, 1);
		mutex_unlock(&substream->group->mutex);
	} else {
		if (!spin_trylock(&substream->group->lock)) {
			spin_unlock(&substream->self_group.lock);
			spin_lock(&substream->group->lock);
			spin_lock(&substream->self_group.lock);
		}
		res = snd_pcm_action_group(ops, substream, state, 1);
		spin_unlock(&substream->group->lock);
	}
	return res;
}

/*
 *  Note: don't use any locks before
 */
static int snd_pcm_action_lock_irq(const struct action_ops *ops,
				   struct snd_pcm_substream *substream,
				   int state)
{
	int res;

	snd_pcm_stream_lock_irq(substream);
	res = snd_pcm_action(ops, substream, state);
	snd_pcm_stream_unlock_irq(substream);
	return res;
}

/*
 */
static int snd_pcm_action_nonatomic(const struct action_ops *ops,
				    struct snd_pcm_substream *substream,
				    int state)
{
	int res;

	down_read(&snd_pcm_link_rwsem);
	if (snd_pcm_stream_linked(substream))
		res = snd_pcm_action_group(ops, substream, state, 0);
	else
		res = snd_pcm_action_single(ops, substream, state);
	up_read(&snd_pcm_link_rwsem);
	return res;
}

/*
 * start callbacks
 */
static int snd_pcm_pre_start(struct snd_pcm_substream *substream, int state)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	if (runtime->status->state != SNDRV_PCM_STATE_PREPARED)
		return -EBADFD;
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK &&
	    !snd_pcm_playback_data(substream))
		return -EPIPE;
	runtime->trigger_tstamp_latched = false;
	runtime->trigger_master = substream;
	return 0;
}

static int snd_pcm_do_start(struct snd_pcm_substream *substream, int state)
{
	if (substream->runtime->trigger_master != substream)
		return 0;
	return substream->ops->trigger(substream, SNDRV_PCM_TRIGGER_START);
}

static void snd_pcm_undo_start(struct snd_pcm_substream *substream, int state)
{
	if (substream->runtime->trigger_master == substream)
		substream->ops->trigger(substream, SNDRV_PCM_TRIGGER_STOP);
}

static void snd_pcm_post_start(struct snd_pcm_substream *substream, int state)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	snd_pcm_trigger_tstamp(substream);
	runtime->hw_ptr_jiffies = jiffies;
	runtime->hw_ptr_buffer_jiffies = (runtime->buffer_size * HZ) / 
							    runtime->rate;
	runtime->status->state = state;
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK &&
	    runtime->silence_size > 0)
		snd_pcm_playback_silence(substream, ULONG_MAX);
	snd_pcm_timer_notify(substream, SNDRV_TIMER_EVENT_MSTART);
}

static const struct action_ops snd_pcm_action_start = {
	.pre_action = snd_pcm_pre_start,
	.do_action = snd_pcm_do_start,
	.undo_action = snd_pcm_undo_start,
	.post_action = snd_pcm_post_start
};

/**
 * snd_pcm_start - start all linked streams
 * @substream: the PCM substream instance
 *
 * Return: Zero if successful, or a negative error code.
 */
int snd_pcm_start(struct snd_pcm_substream *substream)
{
	return snd_pcm_action(&snd_pcm_action_start, substream,
			      SNDRV_PCM_STATE_RUNNING);
}

/*
 * stop callbacks
 */
static int snd_pcm_pre_stop(struct snd_pcm_substream *substream, int state)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	if (runtime->status->state == SNDRV_PCM_STATE_OPEN)
		return -EBADFD;
	runtime->trigger_master = substream;
	return 0;
}

static int snd_pcm_do_stop(struct snd_pcm_substream *substream, int state)
{
	if (substream->runtime->trigger_master == substream &&
	    snd_pcm_running(substream))
		substream->ops->trigger(substream, SNDRV_PCM_TRIGGER_STOP);
	return 0; /* unconditonally stop all substreams */
}

static void snd_pcm_post_stop(struct snd_pcm_substream *substream, int state)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	if (runtime->status->state != state) {
		snd_pcm_trigger_tstamp(substream);
		runtime->status->state = state;
		snd_pcm_timer_notify(substream, SNDRV_TIMER_EVENT_MSTOP);
	}
	wake_up(&runtime->sleep);
	wake_up(&runtime->tsleep);
}

static const struct action_ops snd_pcm_action_stop = {
	.pre_action = snd_pcm_pre_stop,
	.do_action = snd_pcm_do_stop,
	.post_action = snd_pcm_post_stop
};

/**
 * snd_pcm_stop - try to stop all running streams in the substream group
 * @substream: the PCM substream instance
 * @state: PCM state after stopping the stream
 *
 * The state of each stream is then changed to the given state unconditionally.
 *
 * Return: Zero if successful, or a negative error code.
 */
int snd_pcm_stop(struct snd_pcm_substream *substream, snd_pcm_state_t state)
{
	return snd_pcm_action(&snd_pcm_action_stop, substream, state);
}

EXPORT_SYMBOL(snd_pcm_stop);

/**
 * snd_pcm_drain_done - stop the DMA only when the given stream is playback
 * @substream: the PCM substream
 *
 * After stopping, the state is changed to SETUP.
 * Unlike snd_pcm_stop(), this affects only the given stream.
 *
 * Return: Zero if succesful, or a negative error code.
 */
int snd_pcm_drain_done(struct snd_pcm_substream *substream)
{
	return snd_pcm_action_single(&snd_pcm_action_stop, substream,
				     SNDRV_PCM_STATE_SETUP);
}

/**
 * snd_pcm_stop_xrun - stop the running streams as XRUN
 * @substream: the PCM substream instance
 *
 * This stops the given running substream (and all linked substreams) as XRUN.
 * Unlike snd_pcm_stop(), this function takes the substream lock by itself.
 *
 * Return: Zero if successful, or a negative error code.
 */
int snd_pcm_stop_xrun(struct snd_pcm_substream *substream)
{
	unsigned long flags;
	int ret = 0;

	snd_pcm_stream_lock_irqsave(substream, flags);
	if (snd_pcm_running(substream))
		ret = snd_pcm_stop(substream, SNDRV_PCM_STATE_XRUN);
	snd_pcm_stream_unlock_irqrestore(substream, flags);
	return ret;
}
EXPORT_SYMBOL_GPL(snd_pcm_stop_xrun);

/*
 * pause callbacks
 */
static int snd_pcm_pre_pause(struct snd_pcm_substream *substream, int push)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	if (!(runtime->info & SNDRV_PCM_INFO_PAUSE))
		return -ENOSYS;
	if (push) {
		if (runtime->status->state != SNDRV_PCM_STATE_RUNNING)
			return -EBADFD;
	} else if (runtime->status->state != SNDRV_PCM_STATE_PAUSED)
		return -EBADFD;
	runtime->trigger_master = substream;
	return 0;
}

static int snd_pcm_do_pause(struct snd_pcm_substream *substream, int push)
{
	if (substream->runtime->trigger_master != substream)
		return 0;
	/* some drivers might use hw_ptr to recover from the pause -
	   update the hw_ptr now */
	if (push)
		snd_pcm_update_hw_ptr(substream);
	/* The jiffies check in snd_pcm_update_hw_ptr*() is done by
	 * a delta between the current jiffies, this gives a large enough
	 * delta, effectively to skip the check once.
	 */
	substream->runtime->hw_ptr_jiffies = jiffies - HZ * 1000;
	return substream->ops->trigger(substream,
				       push ? SNDRV_PCM_TRIGGER_PAUSE_PUSH :
					      SNDRV_PCM_TRIGGER_PAUSE_RELEASE);
}

static void snd_pcm_undo_pause(struct snd_pcm_substream *substream, int push)
{
	if (substream->runtime->trigger_master == substream)
		substream->ops->trigger(substream,
					push ? SNDRV_PCM_TRIGGER_PAUSE_RELEASE :
					SNDRV_PCM_TRIGGER_PAUSE_PUSH);
}

static void snd_pcm_post_pause(struct snd_pcm_substream *substream, int push)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	snd_pcm_trigger_tstamp(substream);
	if (push) {
		runtime->status->state = SNDRV_PCM_STATE_PAUSED;
		snd_pcm_timer_notify(substream, SNDRV_TIMER_EVENT_MPAUSE);
		wake_up(&runtime->sleep);
		wake_up(&runtime->tsleep);
	} else {
		runtime->status->state = SNDRV_PCM_STATE_RUNNING;
		snd_pcm_timer_notify(substream, SNDRV_TIMER_EVENT_MCONTINUE);
	}
}

static const struct action_ops snd_pcm_action_pause = {
	.pre_action = snd_pcm_pre_pause,
	.do_action = snd_pcm_do_pause,
	.undo_action = snd_pcm_undo_pause,
	.post_action = snd_pcm_post_pause
};

/*
 * Push/release the pause for all linked streams.
 */
static int snd_pcm_pause(struct snd_pcm_substream *substream, int push)
{
	return snd_pcm_action(&snd_pcm_action_pause, substream, push);
}

#ifdef CONFIG_PM
/* suspend */

static int snd_pcm_pre_suspend(struct snd_pcm_substream *substream, int state)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	if (runtime->status->state == SNDRV_PCM_STATE_SUSPENDED)
		return -EBUSY;
	runtime->trigger_master = substream;
	return 0;
}

static int snd_pcm_do_suspend(struct snd_pcm_substream *substream, int state)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	if (runtime->trigger_master != substream)
		return 0;
	if (! snd_pcm_running(substream))
		return 0;
	substream->ops->trigger(substream, SNDRV_PCM_TRIGGER_SUSPEND);
	return 0; /* suspend unconditionally */
}

static void snd_pcm_post_suspend(struct snd_pcm_substream *substream, int state)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	snd_pcm_trigger_tstamp(substream);
	runtime->status->suspended_state = runtime->status->state;
	runtime->status->state = SNDRV_PCM_STATE_SUSPENDED;
	snd_pcm_timer_notify(substream, SNDRV_TIMER_EVENT_MSUSPEND);
	wake_up(&runtime->sleep);
	wake_up(&runtime->tsleep);
}

static const struct action_ops snd_pcm_action_suspend = {
	.pre_action = snd_pcm_pre_suspend,
	.do_action = snd_pcm_do_suspend,
	.post_action = snd_pcm_post_suspend
};

/**
 * snd_pcm_suspend - trigger SUSPEND to all linked streams
 * @substream: the PCM substream
 *
 * After this call, all streams are changed to SUSPENDED state.
 *
 * Return: Zero if successful (or @substream is %NULL), or a negative error
 * code.
 */
int snd_pcm_suspend(struct snd_pcm_substream *substream)
{
	int err;
	unsigned long flags;

	if (! substream)
		return 0;

	snd_pcm_stream_lock_irqsave(substream, flags);
	err = snd_pcm_action(&snd_pcm_action_suspend, substream, 0);
	snd_pcm_stream_unlock_irqrestore(substream, flags);
	return err;
}

EXPORT_SYMBOL(snd_pcm_suspend);

/**
 * snd_pcm_suspend_all - trigger SUSPEND to all substreams in the given pcm
 * @pcm: the PCM instance
 *
 * After this call, all streams are changed to SUSPENDED state.
 *
 * Return: Zero if successful (or @pcm is %NULL), or a negative error code.
 */
int snd_pcm_suspend_all(struct snd_pcm *pcm)
{
	struct snd_pcm_substream *substream;
	int stream, err = 0;

	if (! pcm)
		return 0;

	for (stream = 0; stream < 2; stream++) {
		for (substream = pcm->streams[stream].substream;
		     substream; substream = substream->next) {
			/* FIXME: the open/close code should lock this as well */
			if (substream->runtime == NULL)
				continue;
			err = snd_pcm_suspend(substream);
			if (err < 0 && err != -EBUSY)
				return err;
		}
	}
	return 0;
}

EXPORT_SYMBOL(snd_pcm_suspend_all);

/* resume */

static int snd_pcm_pre_resume(struct snd_pcm_substream *substream, int state)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	if (!(runtime->info & SNDRV_PCM_INFO_RESUME))
		return -ENOSYS;
	runtime->trigger_master = substream;
	return 0;
}

static int snd_pcm_do_resume(struct snd_pcm_substream *substream, int state)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	if (runtime->trigger_master != substream)
		return 0;
	/* DMA not running previously? */
	if (runtime->status->suspended_state != SNDRV_PCM_STATE_RUNNING &&
	    (runtime->status->suspended_state != SNDRV_PCM_STATE_DRAINING ||
	     substream->stream != SNDRV_PCM_STREAM_PLAYBACK))
		return 0;
	return substream->ops->trigger(substream, SNDRV_PCM_TRIGGER_RESUME);
}

static void snd_pcm_undo_resume(struct snd_pcm_substream *substream, int state)
{
	if (substream->runtime->trigger_master == substream &&
	    snd_pcm_running(substream))
		substream->ops->trigger(substream, SNDRV_PCM_TRIGGER_SUSPEND);
}

static void snd_pcm_post_resume(struct snd_pcm_substream *substream, int state)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	snd_pcm_trigger_tstamp(substream);
	runtime->status->state = runtime->status->suspended_state;
	snd_pcm_timer_notify(substream, SNDRV_TIMER_EVENT_MRESUME);
}

static const struct action_ops snd_pcm_action_resume = {
	.pre_action = snd_pcm_pre_resume,
	.do_action = snd_pcm_do_resume,
	.undo_action = snd_pcm_undo_resume,
	.post_action = snd_pcm_post_resume
};

static int snd_pcm_resume(struct snd_pcm_substream *substream)
{
	struct snd_card *card = substream->pcm->card;
	int res;

	snd_power_lock(card);
	if ((res = snd_power_wait(card, SNDRV_CTL_POWER_D0)) >= 0)
		res = snd_pcm_action_lock_irq(&snd_pcm_action_resume, substream, 0);
	snd_power_unlock(card);
	return res;
}

#else

static int snd_pcm_resume(struct snd_pcm_substream *substream)
{
	return -ENOSYS;
}

#endif /* CONFIG_PM */

/*
 * xrun ioctl
 *
 * Change the RUNNING stream(s) to XRUN state.
 */
static int snd_pcm_xrun(struct snd_pcm_substream *substream)
{
	struct snd_card *card = substream->pcm->card;
	struct snd_pcm_runtime *runtime = substream->runtime;
	int result;

	snd_power_lock(card);
	if (runtime->status->state == SNDRV_PCM_STATE_SUSPENDED) {
		result = snd_power_wait(card, SNDRV_CTL_POWER_D0);
		if (result < 0)
			goto _unlock;
	}

	snd_pcm_stream_lock_irq(substream);
	switch (runtime->status->state) {
	case SNDRV_PCM_STATE_XRUN:
		result = 0;	/* already there */
		break;
	case SNDRV_PCM_STATE_RUNNING:
		result = snd_pcm_stop(substream, SNDRV_PCM_STATE_XRUN);
		break;
	default:
		result = -EBADFD;
	}
	snd_pcm_stream_unlock_irq(substream);
 _unlock:
	snd_power_unlock(card);
	return result;
}

/*
 * reset ioctl
 */
static int snd_pcm_pre_reset(struct snd_pcm_substream *substream, int state)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	switch (runtime->status->state) {
	case SNDRV_PCM_STATE_RUNNING:
	case SNDRV_PCM_STATE_PREPARED:
	case SNDRV_PCM_STATE_PAUSED:
	case SNDRV_PCM_STATE_SUSPENDED:
		return 0;
	default:
		return -EBADFD;
	}
}

static int snd_pcm_do_reset(struct snd_pcm_substream *substream, int state)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	int err = substream->ops->ioctl(substream, SNDRV_PCM_IOCTL1_RESET, NULL);
	if (err < 0)
		return err;
	runtime->hw_ptr_base = 0;
	runtime->hw_ptr_interrupt = runtime->status->hw_ptr -
		runtime->status->hw_ptr % runtime->period_size;
	runtime->silence_start = runtime->status->hw_ptr;
	runtime->silence_filled = 0;
	return 0;
}

static void snd_pcm_post_reset(struct snd_pcm_substream *substream, int state)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	runtime->control->appl_ptr = runtime->status->hw_ptr;
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK &&
	    runtime->silence_size > 0)
		snd_pcm_playback_silence(substream, ULONG_MAX);
}

static const struct action_ops snd_pcm_action_reset = {
	.pre_action = snd_pcm_pre_reset,
	.do_action = snd_pcm_do_reset,
	.post_action = snd_pcm_post_reset
};

static int snd_pcm_reset(struct snd_pcm_substream *substream)
{
	return snd_pcm_action_nonatomic(&snd_pcm_action_reset, substream, 0);
}

/*
 * prepare ioctl
 */
/* we use the second argument for updating f_flags */
static int snd_pcm_pre_prepare(struct snd_pcm_substream *substream,
			       int f_flags)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	if (runtime->status->state == SNDRV_PCM_STATE_OPEN ||
	    runtime->status->state == SNDRV_PCM_STATE_DISCONNECTED)
		return -EBADFD;
	if (snd_pcm_running(substream))
		return -EBUSY;
	substream->f_flags = f_flags;
	return 0;
}

static int snd_pcm_do_prepare(struct snd_pcm_substream *substream, int state)
{
	int err;
	err = substream->ops->prepare(substream);
	if (err < 0)
		return err;
	return snd_pcm_do_reset(substream, 0);
}

static void snd_pcm_post_prepare(struct snd_pcm_substream *substream, int state)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	runtime->control->appl_ptr = runtime->status->hw_ptr;
	snd_pcm_set_state(substream, SNDRV_PCM_STATE_PREPARED);
}

static const struct action_ops snd_pcm_action_prepare = {
	.pre_action = snd_pcm_pre_prepare,
	.do_action = snd_pcm_do_prepare,
	.post_action = snd_pcm_post_prepare
};

/**
 * snd_pcm_prepare - prepare the PCM substream to be triggerable
 * @substream: the PCM substream instance
 * @file: file to refer f_flags
 *
 * Return: Zero if successful, or a negative error code.
 */
static int snd_pcm_prepare(struct snd_pcm_substream *substream,
			   struct file *file)
{
	int res;
	struct snd_card *card = substream->pcm->card;
	int f_flags;

	if (file)
		f_flags = file->f_flags;
	else
		f_flags = substream->f_flags;

	snd_power_lock(card);
	if ((res = snd_power_wait(card, SNDRV_CTL_POWER_D0)) >= 0)
		res = snd_pcm_action_nonatomic(&snd_pcm_action_prepare,
					       substream, f_flags);
	snd_power_unlock(card);
	return res;
}

/*
 * drain ioctl
 */

static int snd_pcm_pre_drain_init(struct snd_pcm_substream *substream, int state)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	switch (runtime->status->state) {
	case SNDRV_PCM_STATE_OPEN:
	case SNDRV_PCM_STATE_DISCONNECTED:
	case SNDRV_PCM_STATE_SUSPENDED:
		return -EBADFD;
	}
	runtime->trigger_master = substream;
	return 0;
}

static int snd_pcm_do_drain_init(struct snd_pcm_substream *substream, int state)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		switch (runtime->status->state) {
		case SNDRV_PCM_STATE_PREPARED:
			/* start playback stream if possible */
			if (! snd_pcm_playback_empty(substream)) {
				snd_pcm_do_start(substream, SNDRV_PCM_STATE_DRAINING);
				snd_pcm_post_start(substream, SNDRV_PCM_STATE_DRAINING);
			} else {
				runtime->status->state = SNDRV_PCM_STATE_SETUP;
			}
			break;
		case SNDRV_PCM_STATE_RUNNING:
			runtime->status->state = SNDRV_PCM_STATE_DRAINING;
			break;
		case SNDRV_PCM_STATE_XRUN:
			runtime->status->state = SNDRV_PCM_STATE_SETUP;
			break;
		default:
			break;
		}
	} else {
		/* stop running stream */
		if (runtime->status->state == SNDRV_PCM_STATE_RUNNING) {
			int new_state = snd_pcm_capture_avail(runtime) > 0 ?
				SNDRV_PCM_STATE_DRAINING : SNDRV_PCM_STATE_SETUP;
			snd_pcm_do_stop(substream, new_state);
			snd_pcm_post_stop(substream, new_state);
		}
	}

	if (runtime->status->state == SNDRV_PCM_STATE_DRAINING &&
	    runtime->trigger_master == substream &&
	    (runtime->hw.info & SNDRV_PCM_INFO_DRAIN_TRIGGER))
		return substream->ops->trigger(substream,
					       SNDRV_PCM_TRIGGER_DRAIN);

	return 0;
}

static void snd_pcm_post_drain_init(struct snd_pcm_substream *substream, int state)
{
}

static const struct action_ops snd_pcm_action_drain_init = {
	.pre_action = snd_pcm_pre_drain_init,
	.do_action = snd_pcm_do_drain_init,
	.post_action = snd_pcm_post_drain_init
};

static int snd_pcm_drop(struct snd_pcm_substream *substream);

/*
 * Drain the stream(s).
 * When the substream is linked, sync until the draining of all playback streams
 * is finished.
 * After this call, all streams are supposed to be either SETUP or DRAINING
 * (capture only) state.
 */
static int snd_pcm_drain(struct snd_pcm_substream *substream,
			 struct file *file)
{
	struct snd_card *card;
	struct snd_pcm_runtime *runtime;
	struct snd_pcm_substream *s;
	wait_queue_entry_t wait;
	int result = 0;
	int nonblock = 0;

	card = substream->pcm->card;
	runtime = substream->runtime;

	if (runtime->status->state == SNDRV_PCM_STATE_OPEN)
		return -EBADFD;

	snd_power_lock(card);
	if (runtime->status->state == SNDRV_PCM_STATE_SUSPENDED) {
		result = snd_power_wait(card, SNDRV_CTL_POWER_D0);
		if (result < 0) {
			snd_power_unlock(card);
			return result;
		}
	}

	if (file) {
		if (file->f_flags & O_NONBLOCK)
			nonblock = 1;
	} else if (substream->f_flags & O_NONBLOCK)
		nonblock = 1;

	down_read(&snd_pcm_link_rwsem);
	snd_pcm_stream_lock_irq(substream);
	/* resume pause */
	if (runtime->status->state == SNDRV_PCM_STATE_PAUSED)
		snd_pcm_pause(substream, 0);

	/* pre-start/stop - all running streams are changed to DRAINING state */
	result = snd_pcm_action(&snd_pcm_action_drain_init, substream, 0);
	if (result < 0)
		goto unlock;
	/* in non-blocking, we don't wait in ioctl but let caller poll */
	if (nonblock) {
		result = -EAGAIN;
		goto unlock;
	}

	for (;;) {
		long tout;
		struct snd_pcm_runtime *to_check;
		if (signal_pending(current)) {
			result = -ERESTARTSYS;
			break;
		}
		/* find a substream to drain */
		to_check = NULL;
		snd_pcm_group_for_each_entry(s, substream) {
			if (s->stream != SNDRV_PCM_STREAM_PLAYBACK)
				continue;
			runtime = s->runtime;
			if (runtime->status->state == SNDRV_PCM_STATE_DRAINING) {
				to_check = runtime;
				break;
			}
		}
		if (!to_check)
			break; /* all drained */
		init_waitqueue_entry(&wait, current);
		add_wait_queue(&to_check->sleep, &wait);
		snd_pcm_stream_unlock_irq(substream);
		up_read(&snd_pcm_link_rwsem);
		snd_power_unlock(card);
		if (runtime->no_period_wakeup)
			tout = MAX_SCHEDULE_TIMEOUT;
		else {
			tout = 10;
			if (runtime->rate) {
				long t = runtime->period_size * 2 / runtime->rate;
				tout = max(t, tout);
			}
			tout = msecs_to_jiffies(tout * 1000);
		}
		tout = schedule_timeout_interruptible(tout);
		snd_power_lock(card);
		down_read(&snd_pcm_link_rwsem);
		snd_pcm_stream_lock_irq(substream);
		remove_wait_queue(&to_check->sleep, &wait);
		if (card->shutdown) {
			result = -ENODEV;
			break;
		}
		if (tout == 0) {
			if (substream->runtime->status->state == SNDRV_PCM_STATE_SUSPENDED)
				result = -ESTRPIPE;
			else {
				dev_dbg(substream->pcm->card->dev,
					"playback drain error (DMA or IRQ trouble?)\n");
				snd_pcm_stop(substream, SNDRV_PCM_STATE_SETUP);
				result = -EIO;
			}
			break;
		}
	}

 unlock:
	snd_pcm_stream_unlock_irq(substream);
	up_read(&snd_pcm_link_rwsem);
	snd_power_unlock(card);

	return result;
}

/*
 * drop ioctl
 *
 * Immediately put all linked substreams into SETUP state.
 */
static int snd_pcm_drop(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime;
	int result = 0;
	
	if (PCM_RUNTIME_CHECK(substream))
		return -ENXIO;
	runtime = substream->runtime;

	if (runtime->status->state == SNDRV_PCM_STATE_OPEN ||
	    runtime->status->state == SNDRV_PCM_STATE_DISCONNECTED ||
	    runtime->status->state == SNDRV_PCM_STATE_SUSPENDED)
		return -EBADFD;

	snd_pcm_stream_lock_irq(substream);
	/* resume pause */
	if (runtime->status->state == SNDRV_PCM_STATE_PAUSED)
		snd_pcm_pause(substream, 0);

	snd_pcm_stop(substream, SNDRV_PCM_STATE_SETUP);
	/* runtime->control->appl_ptr = runtime->status->hw_ptr; */
	snd_pcm_stream_unlock_irq(substream);

	return result;
}


static bool is_pcm_file(struct file *file)
{
	struct inode *inode = file_inode(file);
	unsigned int minor;

	if (!S_ISCHR(inode->i_mode) || imajor(inode) != snd_major)
		return false;
	minor = iminor(inode);
	return snd_lookup_minor_data(minor, SNDRV_DEVICE_TYPE_PCM_PLAYBACK) ||
		snd_lookup_minor_data(minor, SNDRV_DEVICE_TYPE_PCM_CAPTURE);
}

/*
 * PCM link handling
 */
static int snd_pcm_link(struct snd_pcm_substream *substream, int fd)
{
	int res = 0;
	struct snd_pcm_file *pcm_file;
	struct snd_pcm_substream *substream1;
	struct snd_pcm_group *group;
	struct fd f = fdget(fd);

	if (!f.file)
		return -EBADFD;
	if (!is_pcm_file(f.file)) {
		res = -EBADFD;
		goto _badf;
	}
	pcm_file = f.file->private_data;
	substream1 = pcm_file->substream;
	group = kmalloc(sizeof(*group), GFP_KERNEL);
	if (!group) {
		res = -ENOMEM;
		goto _nolock;
	}
	down_write_nonblock(&snd_pcm_link_rwsem);
	write_lock_irq(&snd_pcm_link_rwlock);
	if (substream->runtime->status->state == SNDRV_PCM_STATE_OPEN ||
	    substream->runtime->status->state != substream1->runtime->status->state ||
	    substream->pcm->nonatomic != substream1->pcm->nonatomic) {
		res = -EBADFD;
		goto _end;
	}
	if (snd_pcm_stream_linked(substream1)) {
		res = -EALREADY;
		goto _end;
	}
	if (!snd_pcm_stream_linked(substream)) {
		substream->group = group;
		group = NULL;
		spin_lock_init(&substream->group->lock);
		mutex_init(&substream->group->mutex);
		INIT_LIST_HEAD(&substream->group->substreams);
		list_add_tail(&substream->link_list, &substream->group->substreams);
		substream->group->count = 1;
	}
	list_add_tail(&substream1->link_list, &substream->group->substreams);
	substream->group->count++;
	substream1->group = substream->group;
 _end:
	write_unlock_irq(&snd_pcm_link_rwlock);
	up_write(&snd_pcm_link_rwsem);
 _nolock:
	snd_card_unref(substream1->pcm->card);
	kfree(group);
 _badf:
	fdput(f);
	return res;
}

static void relink_to_local(struct snd_pcm_substream *substream)
{
	substream->group = &substream->self_group;
	INIT_LIST_HEAD(&substream->self_group.substreams);
	list_add_tail(&substream->link_list, &substream->self_group.substreams);
}

static int snd_pcm_unlink(struct snd_pcm_substream *substream)
{
	struct snd_pcm_substream *s;
	int res = 0;

	down_write_nonblock(&snd_pcm_link_rwsem);
	write_lock_irq(&snd_pcm_link_rwlock);
	if (!snd_pcm_stream_linked(substream)) {
		res = -EALREADY;
		goto _end;
	}
	list_del(&substream->link_list);
	substream->group->count--;
	if (substream->group->count == 1) {	/* detach the last stream, too */
		snd_pcm_group_for_each_entry(s, substream) {
			relink_to_local(s);
			break;
		}
		kfree(substream->group);
	}
	relink_to_local(substream);
       _end:
	write_unlock_irq(&snd_pcm_link_rwlock);
	up_write(&snd_pcm_link_rwsem);
	return res;
}

/*
 * hw configurator
 */
static int snd_pcm_hw_rule_mul(struct snd_pcm_hw_params *params,
			       struct snd_pcm_hw_rule *rule)
{
	struct snd_interval t;
	snd_interval_mul(hw_param_interval_c(params, rule->deps[0]),
		     hw_param_interval_c(params, rule->deps[1]), &t);
	return snd_interval_refine(hw_param_interval(params, rule->var), &t);
}

static int snd_pcm_hw_rule_div(struct snd_pcm_hw_params *params,
			       struct snd_pcm_hw_rule *rule)
{
	struct snd_interval t;
	snd_interval_div(hw_param_interval_c(params, rule->deps[0]),
		     hw_param_interval_c(params, rule->deps[1]), &t);
	return snd_interval_refine(hw_param_interval(params, rule->var), &t);
}

static int snd_pcm_hw_rule_muldivk(struct snd_pcm_hw_params *params,
				   struct snd_pcm_hw_rule *rule)
{
	struct snd_interval t;
	snd_interval_muldivk(hw_param_interval_c(params, rule->deps[0]),
			 hw_param_interval_c(params, rule->deps[1]),
			 (unsigned long) rule->private, &t);
	return snd_interval_refine(hw_param_interval(params, rule->var), &t);
}

static int snd_pcm_hw_rule_mulkdiv(struct snd_pcm_hw_params *params,
				   struct snd_pcm_hw_rule *rule)
{
	struct snd_interval t;
	snd_interval_mulkdiv(hw_param_interval_c(params, rule->deps[0]),
			 (unsigned long) rule->private,
			 hw_param_interval_c(params, rule->deps[1]), &t);
	return snd_interval_refine(hw_param_interval(params, rule->var), &t);
}

static int snd_pcm_hw_rule_format(struct snd_pcm_hw_params *params,
				  struct snd_pcm_hw_rule *rule)
{
	unsigned int k;
	struct snd_interval *i = hw_param_interval(params, rule->deps[0]);
	struct snd_mask m;
	struct snd_mask *mask = hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT);
	snd_mask_any(&m);
	for (k = 0; k <= SNDRV_PCM_FORMAT_LAST; ++k) {
		int bits;
		if (! snd_mask_test(mask, k))
			continue;
		bits = snd_pcm_format_physical_width(k);
		if (bits <= 0)
			continue; /* ignore invalid formats */
		if ((unsigned)bits < i->min || (unsigned)bits > i->max)
			snd_mask_reset(&m, k);
	}
	return snd_mask_refine(mask, &m);
}

static int snd_pcm_hw_rule_sample_bits(struct snd_pcm_hw_params *params,
				       struct snd_pcm_hw_rule *rule)
{
	struct snd_interval t;
	unsigned int k;
	t.min = UINT_MAX;
	t.max = 0;
	t.openmin = 0;
	t.openmax = 0;
	for (k = 0; k <= SNDRV_PCM_FORMAT_LAST; ++k) {
		int bits;
		if (! snd_mask_test(hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT), k))
			continue;
		bits = snd_pcm_format_physical_width(k);
		if (bits <= 0)
			continue; /* ignore invalid formats */
		if (t.min > (unsigned)bits)
			t.min = bits;
		if (t.max < (unsigned)bits)
			t.max = bits;
	}
	t.integer = 1;
	return snd_interval_refine(hw_param_interval(params, rule->var), &t);
}

#if SNDRV_PCM_RATE_5512 != 1 << 0 || SNDRV_PCM_RATE_192000 != 1 << 12
#error "Change this table"
#endif

static unsigned int rates[] = { 5512, 8000, 11025, 16000, 22050, 32000, 44100,
                                 48000, 64000, 88200, 96000, 176400, 192000 };

const struct snd_pcm_hw_constraint_list snd_pcm_known_rates = {
	.count = ARRAY_SIZE(rates),
	.list = rates,
};

static int snd_pcm_hw_rule_rate(struct snd_pcm_hw_params *params,
				struct snd_pcm_hw_rule *rule)
{
	struct snd_pcm_hardware *hw = rule->private;
	return snd_interval_list(hw_param_interval(params, rule->var),
				 snd_pcm_known_rates.count,
				 snd_pcm_known_rates.list, hw->rates);
}		

static int snd_pcm_hw_rule_buffer_bytes_max(struct snd_pcm_hw_params *params,
					    struct snd_pcm_hw_rule *rule)
{
	struct snd_interval t;
	struct snd_pcm_substream *substream = rule->private;
	t.min = 0;
	t.max = substream->buffer_bytes_max;
	t.openmin = 0;
	t.openmax = 0;
	t.integer = 1;
	return snd_interval_refine(hw_param_interval(params, rule->var), &t);
}		

int snd_pcm_hw_constraints_init(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_pcm_hw_constraints *constrs = &runtime->hw_constraints;
	int k, err;

	for (k = SNDRV_PCM_HW_PARAM_FIRST_MASK; k <= SNDRV_PCM_HW_PARAM_LAST_MASK; k++) {
		snd_mask_any(constrs_mask(constrs, k));
	}

	for (k = SNDRV_PCM_HW_PARAM_FIRST_INTERVAL; k <= SNDRV_PCM_HW_PARAM_LAST_INTERVAL; k++) {
		snd_interval_any(constrs_interval(constrs, k));
	}

	snd_interval_setinteger(constrs_interval(constrs, SNDRV_PCM_HW_PARAM_CHANNELS));
	snd_interval_setinteger(constrs_interval(constrs, SNDRV_PCM_HW_PARAM_BUFFER_SIZE));
	snd_interval_setinteger(constrs_interval(constrs, SNDRV_PCM_HW_PARAM_BUFFER_BYTES));
	snd_interval_setinteger(constrs_interval(constrs, SNDRV_PCM_HW_PARAM_SAMPLE_BITS));
	snd_interval_setinteger(constrs_interval(constrs, SNDRV_PCM_HW_PARAM_FRAME_BITS));

	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_FORMAT,
				   snd_pcm_hw_rule_format, NULL,
				   SNDRV_PCM_HW_PARAM_SAMPLE_BITS, -1);
	if (err < 0)
		return err;
	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_SAMPLE_BITS, 
				  snd_pcm_hw_rule_sample_bits, NULL,
				  SNDRV_PCM_HW_PARAM_FORMAT, 
				  SNDRV_PCM_HW_PARAM_SAMPLE_BITS, -1);
	if (err < 0)
		return err;
	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_SAMPLE_BITS, 
				  snd_pcm_hw_rule_div, NULL,
				  SNDRV_PCM_HW_PARAM_FRAME_BITS, SNDRV_PCM_HW_PARAM_CHANNELS, -1);
	if (err < 0)
		return err;
	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_FRAME_BITS, 
				  snd_pcm_hw_rule_mul, NULL,
				  SNDRV_PCM_HW_PARAM_SAMPLE_BITS, SNDRV_PCM_HW_PARAM_CHANNELS, -1);
	if (err < 0)
		return err;
	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_FRAME_BITS, 
				  snd_pcm_hw_rule_mulkdiv, (void*) 8,
				  SNDRV_PCM_HW_PARAM_PERIOD_BYTES, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, -1);
	if (err < 0)
		return err;
	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_FRAME_BITS, 
				  snd_pcm_hw_rule_mulkdiv, (void*) 8,
				  SNDRV_PCM_HW_PARAM_BUFFER_BYTES, SNDRV_PCM_HW_PARAM_BUFFER_SIZE, -1);
	if (err < 0)
		return err;
	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS, 
				  snd_pcm_hw_rule_div, NULL,
				  SNDRV_PCM_HW_PARAM_FRAME_BITS, SNDRV_PCM_HW_PARAM_SAMPLE_BITS, -1);
	if (err < 0)
		return err;
	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_RATE, 
				  snd_pcm_hw_rule_mulkdiv, (void*) 1000000,
				  SNDRV_PCM_HW_PARAM_PERIOD_SIZE, SNDRV_PCM_HW_PARAM_PERIOD_TIME, -1);
	if (err < 0)
		return err;
	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_RATE, 
				  snd_pcm_hw_rule_mulkdiv, (void*) 1000000,
				  SNDRV_PCM_HW_PARAM_BUFFER_SIZE, SNDRV_PCM_HW_PARAM_BUFFER_TIME, -1);
	if (err < 0)
		return err;
	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_PERIODS, 
				  snd_pcm_hw_rule_div, NULL,
				  SNDRV_PCM_HW_PARAM_BUFFER_SIZE, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, -1);
	if (err < 0)
		return err;
	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, 
				  snd_pcm_hw_rule_div, NULL,
				  SNDRV_PCM_HW_PARAM_BUFFER_SIZE, SNDRV_PCM_HW_PARAM_PERIODS, -1);
	if (err < 0)
		return err;
	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, 
				  snd_pcm_hw_rule_mulkdiv, (void*) 8,
				  SNDRV_PCM_HW_PARAM_PERIOD_BYTES, SNDRV_PCM_HW_PARAM_FRAME_BITS, -1);
	if (err < 0)
		return err;
	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, 
				  snd_pcm_hw_rule_muldivk, (void*) 1000000,
				  SNDRV_PCM_HW_PARAM_PERIOD_TIME, SNDRV_PCM_HW_PARAM_RATE, -1);
	if (err < 0)
		return err;
	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_BUFFER_SIZE, 
				  snd_pcm_hw_rule_mul, NULL,
				  SNDRV_PCM_HW_PARAM_PERIOD_SIZE, SNDRV_PCM_HW_PARAM_PERIODS, -1);
	if (err < 0)
		return err;
	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_BUFFER_SIZE, 
				  snd_pcm_hw_rule_mulkdiv, (void*) 8,
				  SNDRV_PCM_HW_PARAM_BUFFER_BYTES, SNDRV_PCM_HW_PARAM_FRAME_BITS, -1);
	if (err < 0)
		return err;
	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_BUFFER_SIZE, 
				  snd_pcm_hw_rule_muldivk, (void*) 1000000,
				  SNDRV_PCM_HW_PARAM_BUFFER_TIME, SNDRV_PCM_HW_PARAM_RATE, -1);
	if (err < 0)
		return err;
	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_BYTES, 
				  snd_pcm_hw_rule_muldivk, (void*) 8,
				  SNDRV_PCM_HW_PARAM_PERIOD_SIZE, SNDRV_PCM_HW_PARAM_FRAME_BITS, -1);
	if (err < 0)
		return err;
	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_BUFFER_BYTES, 
				  snd_pcm_hw_rule_muldivk, (void*) 8,
				  SNDRV_PCM_HW_PARAM_BUFFER_SIZE, SNDRV_PCM_HW_PARAM_FRAME_BITS, -1);
	if (err < 0)
		return err;
	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_TIME, 
				  snd_pcm_hw_rule_mulkdiv, (void*) 1000000,
				  SNDRV_PCM_HW_PARAM_PERIOD_SIZE, SNDRV_PCM_HW_PARAM_RATE, -1);
	if (err < 0)
		return err;
	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_BUFFER_TIME, 
				  snd_pcm_hw_rule_mulkdiv, (void*) 1000000,
				  SNDRV_PCM_HW_PARAM_BUFFER_SIZE, SNDRV_PCM_HW_PARAM_RATE, -1);
	if (err < 0)
		return err;
	return 0;
}

int snd_pcm_hw_constraints_complete(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_pcm_hardware *hw = &runtime->hw;
	int err;
	unsigned int mask = 0;

        if (hw->info & SNDRV_PCM_INFO_INTERLEAVED)
		mask |= 1 << SNDRV_PCM_ACCESS_RW_INTERLEAVED;
        if (hw->info & SNDRV_PCM_INFO_NONINTERLEAVED)
		mask |= 1 << SNDRV_PCM_ACCESS_RW_NONINTERLEAVED;
	if (hw_support_mmap(substream)) {
		if (hw->info & SNDRV_PCM_INFO_INTERLEAVED)
			mask |= 1 << SNDRV_PCM_ACCESS_MMAP_INTERLEAVED;
		if (hw->info & SNDRV_PCM_INFO_NONINTERLEAVED)
			mask |= 1 << SNDRV_PCM_ACCESS_MMAP_NONINTERLEAVED;
		if (hw->info & SNDRV_PCM_INFO_COMPLEX)
			mask |= 1 << SNDRV_PCM_ACCESS_MMAP_COMPLEX;
	}
	err = snd_pcm_hw_constraint_mask(runtime, SNDRV_PCM_HW_PARAM_ACCESS, mask);
	if (err < 0)
		return err;

	err = snd_pcm_hw_constraint_mask64(runtime, SNDRV_PCM_HW_PARAM_FORMAT, hw->formats);
	if (err < 0)
		return err;

	err = snd_pcm_hw_constraint_mask(runtime, SNDRV_PCM_HW_PARAM_SUBFORMAT, 1 << SNDRV_PCM_SUBFORMAT_STD);
	if (err < 0)
		return err;

	err = snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_CHANNELS,
					   hw->channels_min, hw->channels_max);
	if (err < 0)
		return err;

	err = snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_RATE,
					   hw->rate_min, hw->rate_max);
	if (err < 0)
		return err;

	err = snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_PERIOD_BYTES,
					   hw->period_bytes_min, hw->period_bytes_max);
	if (err < 0)
		return err;

	err = snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_PERIODS,
					   hw->periods_min, hw->periods_max);
	if (err < 0)
		return err;

	err = snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_BUFFER_BYTES,
					   hw->period_bytes_min, hw->buffer_bytes_max);
	if (err < 0)
		return err;

	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_BUFFER_BYTES, 
				  snd_pcm_hw_rule_buffer_bytes_max, substream,
				  SNDRV_PCM_HW_PARAM_BUFFER_BYTES, -1);
	if (err < 0)
		return err;

	/* FIXME: remove */
	if (runtime->dma_bytes) {
		err = snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_BUFFER_BYTES, 0, runtime->dma_bytes);
		if (err < 0)
			return err;
	}

	if (!(hw->rates & (SNDRV_PCM_RATE_KNOT | SNDRV_PCM_RATE_CONTINUOUS))) {
		err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_RATE, 
					  snd_pcm_hw_rule_rate, hw,
					  SNDRV_PCM_HW_PARAM_RATE, -1);
		if (err < 0)
			return err;
	}

	/* FIXME: this belong to lowlevel */
	snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIOD_SIZE);

	return 0;
}

static void pcm_release_private(struct snd_pcm_substream *substream)
{
	snd_pcm_unlink(substream);
}

void snd_pcm_release_substream(struct snd_pcm_substream *substream)
{
	substream->ref_count--;
	if (substream->ref_count > 0)
		return;

	snd_pcm_drop(substream);
	if (substream->hw_opened) {
		if (substream->ops->hw_free &&
		    substream->runtime->status->state != SNDRV_PCM_STATE_OPEN)
			substream->ops->hw_free(substream);
		substream->ops->close(substream);
		substream->hw_opened = 0;
	}
	if (pm_qos_request_active(&substream->latency_pm_qos_req))
		pm_qos_remove_request(&substream->latency_pm_qos_req);
	if (substream->pcm_release) {
		substream->pcm_release(substream);
		substream->pcm_release = NULL;
	}
	snd_pcm_detach_substream(substream);
}

EXPORT_SYMBOL(snd_pcm_release_substream);

int snd_pcm_open_substream(struct snd_pcm *pcm, int stream,
			   struct file *file,
			   struct snd_pcm_substream **rsubstream)
{
	struct snd_pcm_substream *substream;
	int err;

	err = snd_pcm_attach_substream(pcm, stream, file, &substream);
	if (err < 0)
		return err;
	if (substream->ref_count > 1) {
		*rsubstream = substream;
		return 0;
	}

	err = snd_pcm_hw_constraints_init(substream);
	if (err < 0) {
		pcm_dbg(pcm, "snd_pcm_hw_constraints_init failed\n");
		goto error;
	}

	if ((err = substream->ops->open(substream)) < 0)
		goto error;

	substream->hw_opened = 1;

	err = snd_pcm_hw_constraints_complete(substream);
	if (err < 0) {
		pcm_dbg(pcm, "snd_pcm_hw_constraints_complete failed\n");
		goto error;
	}

	*rsubstream = substream;
	return 0;

 error:
	snd_pcm_release_substream(substream);
	return err;
}

EXPORT_SYMBOL(snd_pcm_open_substream);

static int snd_pcm_open_file(struct file *file,
			     struct snd_pcm *pcm,
			     int stream)
{
	struct snd_pcm_file *pcm_file;
	struct snd_pcm_substream *substream;
	int err;

	err = snd_pcm_open_substream(pcm, stream, file, &substream);
	if (err < 0)
		return err;

	pcm_file = kzalloc(sizeof(*pcm_file), GFP_KERNEL);
	if (pcm_file == NULL) {
		snd_pcm_release_substream(substream);
		return -ENOMEM;
	}
	pcm_file->substream = substream;
	if (substream->ref_count == 1) {
		substream->file = pcm_file;
		substream->pcm_release = pcm_release_private;
	}
	file->private_data = pcm_file;

	return 0;
}

static int snd_pcm_playback_open(struct inode *inode, struct file *file)
{
	struct snd_pcm *pcm;
	int err = nonseekable_open(inode, file);
	if (err < 0)
		return err;
	pcm = snd_lookup_minor_data(iminor(inode),
				    SNDRV_DEVICE_TYPE_PCM_PLAYBACK);
	err = snd_pcm_open(file, pcm, SNDRV_PCM_STREAM_PLAYBACK);
	if (pcm)
		snd_card_unref(pcm->card);
	return err;
}

static int snd_pcm_capture_open(struct inode *inode, struct file *file)
{
	struct snd_pcm *pcm;
	int err = nonseekable_open(inode, file);
	if (err < 0)
		return err;
	pcm = snd_lookup_minor_data(iminor(inode),
				    SNDRV_DEVICE_TYPE_PCM_CAPTURE);
	err = snd_pcm_open(file, pcm, SNDRV_PCM_STREAM_CAPTURE);
	if (pcm)
		snd_card_unref(pcm->card);
	return err;
}

static int snd_pcm_open(struct file *file, struct snd_pcm *pcm, int stream)
{
	int err;
	wait_queue_entry_t wait;

	if (pcm == NULL) {
		err = -ENODEV;
		goto __error1;
	}
	err = snd_card_file_add(pcm->card, file);
	if (err < 0)
		goto __error1;
	if (!try_module_get(pcm->card->module)) {
		err = -EFAULT;
		goto __error2;
	}
	init_waitqueue_entry(&wait, current);
	add_wait_queue(&pcm->open_wait, &wait);
	mutex_lock(&pcm->open_mutex);
	while (1) {
		err = snd_pcm_open_file(file, pcm, stream);
		if (err >= 0)
			break;
		if (err == -EAGAIN) {
			if (file->f_flags & O_NONBLOCK) {
				err = -EBUSY;
				break;
			}
		} else
			break;
		set_current_state(TASK_INTERRUPTIBLE);
		mutex_unlock(&pcm->open_mutex);
		schedule();
		mutex_lock(&pcm->open_mutex);
		if (pcm->card->shutdown) {
			err = -ENODEV;
			break;
		}
		if (signal_pending(current)) {
			err = -ERESTARTSYS;
			break;
		}
	}
	remove_wait_queue(&pcm->open_wait, &wait);
	mutex_unlock(&pcm->open_mutex);
	if (err < 0)
		goto __error;
	return err;

      __error:
	module_put(pcm->card->module);
      __error2:
      	snd_card_file_remove(pcm->card, file);
      __error1:
      	return err;
}

static int snd_pcm_release(struct inode *inode, struct file *file)
{
	struct snd_pcm *pcm;
	struct snd_pcm_substream *substream;
	struct snd_pcm_file *pcm_file;

	pcm_file = file->private_data;
	substream = pcm_file->substream;
	if (snd_BUG_ON(!substream))
		return -ENXIO;
	pcm = substream->pcm;
	mutex_lock(&pcm->open_mutex);
	snd_pcm_release_substream(substream);
	kfree(pcm_file);
	mutex_unlock(&pcm->open_mutex);
	wake_up(&pcm->open_wait);
	module_put(pcm->card->module);
	snd_card_file_remove(pcm->card, file);
	return 0;
}

static snd_pcm_sframes_t snd_pcm_playback_rewind(struct snd_pcm_substream *substream,
						 snd_pcm_uframes_t frames)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	snd_pcm_sframes_t appl_ptr;
	snd_pcm_sframes_t ret;
	snd_pcm_sframes_t hw_avail;

	if (frames == 0)
		return 0;

	snd_pcm_stream_lock_irq(substream);
	switch (runtime->status->state) {
	case SNDRV_PCM_STATE_PREPARED:
		break;
	case SNDRV_PCM_STATE_DRAINING:
	case SNDRV_PCM_STATE_RUNNING:
		if (snd_pcm_update_hw_ptr(substream) >= 0)
			break;
		/* Fall through */
	case SNDRV_PCM_STATE_XRUN:
		ret = -EPIPE;
		goto __end;
	case SNDRV_PCM_STATE_SUSPENDED:
		ret = -ESTRPIPE;
		goto __end;
	default:
		ret = -EBADFD;
		goto __end;
	}

	hw_avail = snd_pcm_playback_hw_avail(runtime);
	if (hw_avail <= 0) {
		ret = 0;
		goto __end;
	}
	if (frames > (snd_pcm_uframes_t)hw_avail)
		frames = hw_avail;
	appl_ptr = runtime->control->appl_ptr - frames;
	if (appl_ptr < 0)
		appl_ptr += runtime->boundary;
	runtime->control->appl_ptr = appl_ptr;
	ret = frames;
 __end:
	snd_pcm_stream_unlock_irq(substream);
	return ret;
}

static snd_pcm_sframes_t snd_pcm_capture_rewind(struct snd_pcm_substream *substream,
						snd_pcm_uframes_t frames)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	snd_pcm_sframes_t appl_ptr;
	snd_pcm_sframes_t ret;
	snd_pcm_sframes_t hw_avail;

	if (frames == 0)
		return 0;

	snd_pcm_stream_lock_irq(substream);
	switch (runtime->status->state) {
	case SNDRV_PCM_STATE_PREPARED:
	case SNDRV_PCM_STATE_DRAINING:
		break;
	case SNDRV_PCM_STATE_RUNNING:
		if (snd_pcm_update_hw_ptr(substream) >= 0)
			break;
		/* Fall through */
	case SNDRV_PCM_STATE_XRUN:
		ret = -EPIPE;
		goto __end;
	case SNDRV_PCM_STATE_SUSPENDED:
		ret = -ESTRPIPE;
		goto __end;
	default:
		ret = -EBADFD;
		goto __end;
	}

	hw_avail = snd_pcm_capture_hw_avail(runtime);
	if (hw_avail <= 0) {
		ret = 0;
		goto __end;
	}
	if (frames > (snd_pcm_uframes_t)hw_avail)
		frames = hw_avail;
	appl_ptr = runtime->control->appl_ptr - frames;
	if (appl_ptr < 0)
		appl_ptr += runtime->boundary;
	runtime->control->appl_ptr = appl_ptr;
	ret = frames;
 __end:
	snd_pcm_stream_unlock_irq(substream);
	return ret;
}

static snd_pcm_sframes_t snd_pcm_playback_forward(struct snd_pcm_substream *substream,
						  snd_pcm_uframes_t frames)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	snd_pcm_sframes_t appl_ptr;
	snd_pcm_sframes_t ret;
	snd_pcm_sframes_t avail;

	if (frames == 0)
		return 0;

	snd_pcm_stream_lock_irq(substream);
	switch (runtime->status->state) {
	case SNDRV_PCM_STATE_PREPARED:
	case SNDRV_PCM_STATE_PAUSED:
		break;
	case SNDRV_PCM_STATE_DRAINING:
	case SNDRV_PCM_STATE_RUNNING:
		if (snd_pcm_update_hw_ptr(substream) >= 0)
			break;
		/* Fall through */
	case SNDRV_PCM_STATE_XRUN:
		ret = -EPIPE;
		goto __end;
	case SNDRV_PCM_STATE_SUSPENDED:
		ret = -ESTRPIPE;
		goto __end;
	default:
		ret = -EBADFD;
		goto __end;
	}

	avail = snd_pcm_playback_avail(runtime);
	if (avail <= 0) {
		ret = 0;
		goto __end;
	}
	if (frames > (snd_pcm_uframes_t)avail)
		frames = avail;
	appl_ptr = runtime->control->appl_ptr + frames;
	if (appl_ptr >= (snd_pcm_sframes_t)runtime->boundary)
		appl_ptr -= runtime->boundary;
	runtime->control->appl_ptr = appl_ptr;
	ret = frames;
 __end:
	snd_pcm_stream_unlock_irq(substream);
	return ret;
}

static snd_pcm_sframes_t snd_pcm_capture_forward(struct snd_pcm_substream *substream,
						 snd_pcm_uframes_t frames)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	snd_pcm_sframes_t appl_ptr;
	snd_pcm_sframes_t ret;
	snd_pcm_sframes_t avail;

	if (frames == 0)
		return 0;

	snd_pcm_stream_lock_irq(substream);
	switch (runtime->status->state) {
	case SNDRV_PCM_STATE_PREPARED:
	case SNDRV_PCM_STATE_DRAINING:
	case SNDRV_PCM_STATE_PAUSED:
		break;
	case SNDRV_PCM_STATE_RUNNING:
		if (snd_pcm_update_hw_ptr(substream) >= 0)
			break;
		/* Fall through */
	case SNDRV_PCM_STATE_XRUN:
		ret = -EPIPE;
		goto __end;
	case SNDRV_PCM_STATE_SUSPENDED:
		ret = -ESTRPIPE;
		goto __end;
	default:
		ret = -EBADFD;
		goto __end;
	}

	avail = snd_pcm_capture_avail(runtime);
	if (avail <= 0) {
		ret = 0;
		goto __end;
	}
	if (frames > (snd_pcm_uframes_t)avail)
		frames = avail;
	appl_ptr = runtime->control->appl_ptr + frames;
	if (appl_ptr >= (snd_pcm_sframes_t)runtime->boundary)
		appl_ptr -= runtime->boundary;
	runtime->control->appl_ptr = appl_ptr;
	ret = frames;
 __end:
	snd_pcm_stream_unlock_irq(substream);
	return ret;
}

static int snd_pcm_hwsync(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	int err;

	snd_pcm_stream_lock_irq(substream);
	switch (runtime->status->state) {
	case SNDRV_PCM_STATE_DRAINING:
		if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
			goto __badfd;
		/* Fall through */
	case SNDRV_PCM_STATE_RUNNING:
		if ((err = snd_pcm_update_hw_ptr(substream)) < 0)
			break;
		/* Fall through */
	case SNDRV_PCM_STATE_PREPARED:
		err = 0;
		break;
	case SNDRV_PCM_STATE_SUSPENDED:
		err = -ESTRPIPE;
		break;
	case SNDRV_PCM_STATE_XRUN:
		err = -EPIPE;
		break;
	default:
	      __badfd:
		err = -EBADFD;
		break;
	}
	snd_pcm_stream_unlock_irq(substream);
	return err;
}
		
static int snd_pcm_delay(struct snd_pcm_substream *substream,
			 snd_pcm_sframes_t __user *res)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	int err;
	snd_pcm_sframes_t n = 0;

	snd_pcm_stream_lock_irq(substream);
	switch (runtime->status->state) {
	case SNDRV_PCM_STATE_DRAINING:
		if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
			goto __badfd;
		/* Fall through */
	case SNDRV_PCM_STATE_RUNNING:
		if ((err = snd_pcm_update_hw_ptr(substream)) < 0)
			break;
		/* Fall through */
	case SNDRV_PCM_STATE_PREPARED:
	case SNDRV_PCM_STATE_SUSPENDED:
		err = 0;
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			n = snd_pcm_playback_hw_avail(runtime);
		else
			n = snd_pcm_capture_avail(runtime);
		n += runtime->delay;
		break;
	case SNDRV_PCM_STATE_XRUN:
		err = -EPIPE;
		break;
	default:
	      __badfd:
		err = -EBADFD;
		break;
	}
	snd_pcm_stream_unlock_irq(substream);
	if (!err)
		if (put_user(n, res))
			err = -EFAULT;
	return err;
}
		
static int snd_pcm_sync_ptr(struct snd_pcm_substream *substream,
			    struct snd_pcm_sync_ptr __user *_sync_ptr)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_pcm_sync_ptr sync_ptr;
	volatile struct snd_pcm_mmap_status *status;
	volatile struct snd_pcm_mmap_control *control;
	int err;

	memset(&sync_ptr, 0, sizeof(sync_ptr));
	if (get_user(sync_ptr.flags, (unsigned __user *)&(_sync_ptr->flags)))
		return -EFAULT;
	if (copy_from_user(&sync_ptr.c.control, &(_sync_ptr->c.control), sizeof(struct snd_pcm_mmap_control)))
		return -EFAULT;	
	status = runtime->status;
	control = runtime->control;
	if (sync_ptr.flags & SNDRV_PCM_SYNC_PTR_HWSYNC) {
		err = snd_pcm_hwsync(substream);
		if (err < 0)
			return err;
	}
	snd_pcm_stream_lock_irq(substream);
	if (!(sync_ptr.flags & SNDRV_PCM_SYNC_PTR_APPL))
		control->appl_ptr = sync_ptr.c.control.appl_ptr;
	else
		sync_ptr.c.control.appl_ptr = control->appl_ptr;
	if (!(sync_ptr.flags & SNDRV_PCM_SYNC_PTR_AVAIL_MIN))
		control->avail_min = sync_ptr.c.control.avail_min;
	else
		sync_ptr.c.control.avail_min = control->avail_min;
	sync_ptr.s.status.state = status->state;
	sync_ptr.s.status.hw_ptr = status->hw_ptr;
	sync_ptr.s.status.tstamp = status->tstamp;
	sync_ptr.s.status.suspended_state = status->suspended_state;
	snd_pcm_stream_unlock_irq(substream);
	if (copy_to_user(_sync_ptr, &sync_ptr, sizeof(sync_ptr)))
		return -EFAULT;
	return 0;
}

static int snd_pcm_tstamp(struct snd_pcm_substream *substream, int __user *_arg)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	int arg;
	
	if (get_user(arg, _arg))
		return -EFAULT;
	if (arg < 0 || arg > SNDRV_PCM_TSTAMP_TYPE_LAST)
		return -EINVAL;
	runtime->tstamp_type = arg;
	return 0;
}
		
static int snd_pcm_common_ioctl1(struct file *file,
				 struct snd_pcm_substream *substream,
				 unsigned int cmd, void __user *arg)
{
	switch (cmd) {
	case SNDRV_PCM_IOCTL_PVERSION:
		return put_user(SNDRV_PCM_VERSION, (int __user *)arg) ? -EFAULT : 0;
	case SNDRV_PCM_IOCTL_INFO:
		return snd_pcm_info_user(substream, arg);
	case SNDRV_PCM_IOCTL_TSTAMP:	/* just for compatibility */
		return 0;
	case SNDRV_PCM_IOCTL_TTSTAMP:
		return snd_pcm_tstamp(substream, arg);
	case SNDRV_PCM_IOCTL_HW_REFINE:
		return snd_pcm_hw_refine_user(substream, arg);
	case SNDRV_PCM_IOCTL_HW_PARAMS:
		return snd_pcm_hw_params_user(substream, arg);
	case SNDRV_PCM_IOCTL_HW_FREE:
		return snd_pcm_hw_free(substream);
	case SNDRV_PCM_IOCTL_SW_PARAMS:
		return snd_pcm_sw_params_user(substream, arg);
	case SNDRV_PCM_IOCTL_STATUS:
		return snd_pcm_status_user(substream, arg, false);
	case SNDRV_PCM_IOCTL_STATUS_EXT:
		return snd_pcm_status_user(substream, arg, true);
	case SNDRV_PCM_IOCTL_CHANNEL_INFO:
		return snd_pcm_channel_info_user(substream, arg);
	case SNDRV_PCM_IOCTL_PREPARE:
		return snd_pcm_prepare(substream, file);
	case SNDRV_PCM_IOCTL_RESET:
		return snd_pcm_reset(substream);
	case SNDRV_PCM_IOCTL_START:
		return snd_pcm_action_lock_irq(&snd_pcm_action_start, substream, SNDRV_PCM_STATE_RUNNING);
	case SNDRV_PCM_IOCTL_LINK:
		return snd_pcm_link(substream, (int)(unsigned long) arg);
	case SNDRV_PCM_IOCTL_UNLINK:
		return snd_pcm_unlink(substream);
	case SNDRV_PCM_IOCTL_RESUME:
		return snd_pcm_resume(substream);
	case SNDRV_PCM_IOCTL_XRUN:
		return snd_pcm_xrun(substream);
	case SNDRV_PCM_IOCTL_HWSYNC:
		return snd_pcm_hwsync(substream);
	case SNDRV_PCM_IOCTL_DELAY:
		return snd_pcm_delay(substream, arg);
	case SNDRV_PCM_IOCTL_SYNC_PTR:
		return snd_pcm_sync_ptr(substream, arg);
#ifdef CONFIG_SND_SUPPORT_OLD_API
	case SNDRV_PCM_IOCTL_HW_REFINE_OLD:
		return snd_pcm_hw_refine_old_user(substream, arg);
	case SNDRV_PCM_IOCTL_HW_PARAMS_OLD:
		return snd_pcm_hw_params_old_user(substream, arg);
#endif
	case SNDRV_PCM_IOCTL_DRAIN:
		return snd_pcm_drain(substream, file);
	case SNDRV_PCM_IOCTL_DROP:
		return snd_pcm_drop(substream);
	case SNDRV_PCM_IOCTL_PAUSE:
	{
		int res;
		snd_pcm_stream_lock_irq(substream);
		res = snd_pcm_pause(substream, (int)(unsigned long)arg);
		snd_pcm_stream_unlock_irq(substream);
		return res;
	}
	}
	pcm_dbg(substream->pcm, "unknown ioctl = 0x%x\n", cmd);
	return -ENOTTY;
}

static int snd_pcm_playback_ioctl1(struct file *file,
				   struct snd_pcm_substream *substream,
				   unsigned int cmd, void __user *arg)
{
	if (snd_BUG_ON(!substream))
		return -ENXIO;
	if (snd_BUG_ON(substream->stream != SNDRV_PCM_STREAM_PLAYBACK))
		return -EINVAL;
	switch (cmd) {
	case SNDRV_PCM_IOCTL_WRITEI_FRAMES:
	{
		struct snd_xferi xferi;
		struct snd_xferi __user *_xferi = arg;
		struct snd_pcm_runtime *runtime = substream->runtime;
		snd_pcm_sframes_t result;
		if (runtime->status->state == SNDRV_PCM_STATE_OPEN)
			return -EBADFD;
		if (put_user(0, &_xferi->result))
			return -EFAULT;
		if (copy_from_user(&xferi, _xferi, sizeof(xferi)))
			return -EFAULT;
		result = snd_pcm_lib_write(substream, xferi.buf, xferi.frames);
		__put_user(result, &_xferi->result);
		return result < 0 ? result : 0;
	}
	case SNDRV_PCM_IOCTL_WRITEN_FRAMES:
	{
		struct snd_xfern xfern;
		struct snd_xfern __user *_xfern = arg;
		struct snd_pcm_runtime *runtime = substream->runtime;
		void __user **bufs;
		snd_pcm_sframes_t result;
		if (runtime->status->state == SNDRV_PCM_STATE_OPEN)
			return -EBADFD;
		if (runtime->channels > 128)
			return -EINVAL;
		if (put_user(0, &_xfern->result))
			return -EFAULT;
		if (copy_from_user(&xfern, _xfern, sizeof(xfern)))
			return -EFAULT;

		bufs = memdup_user(xfern.bufs,
				   sizeof(void *) * runtime->channels);
		if (IS_ERR(bufs))
			return PTR_ERR(bufs);
		result = snd_pcm_lib_writev(substream, bufs, xfern.frames);
		kfree(bufs);
		__put_user(result, &_xfern->result);
		return result < 0 ? result : 0;
	}
	case SNDRV_PCM_IOCTL_REWIND:
	{
		snd_pcm_uframes_t frames;
		snd_pcm_uframes_t __user *_frames = arg;
		snd_pcm_sframes_t result;
		if (get_user(frames, _frames))
			return -EFAULT;
		if (put_user(0, _frames))
			return -EFAULT;
		result = snd_pcm_playback_rewind(substream, frames);
		__put_user(result, _frames);
		return result < 0 ? result : 0;
	}
	case SNDRV_PCM_IOCTL_FORWARD:
	{
		snd_pcm_uframes_t frames;
		snd_pcm_uframes_t __user *_frames = arg;
		snd_pcm_sframes_t result;
		if (get_user(frames, _frames))
			return -EFAULT;
		if (put_user(0, _frames))
			return -EFAULT;
		result = snd_pcm_playback_forward(substream, frames);
		__put_user(result, _frames);
		return result < 0 ? result : 0;
	}
	}
	return snd_pcm_common_ioctl1(file, substream, cmd, arg);
}

static int snd_pcm_capture_ioctl1(struct file *file,
				  struct snd_pcm_substream *substream,
				  unsigned int cmd, void __user *arg)
{
	if (snd_BUG_ON(!substream))
		return -ENXIO;
	if (snd_BUG_ON(substream->stream != SNDRV_PCM_STREAM_CAPTURE))
		return -EINVAL;
	switch (cmd) {
	case SNDRV_PCM_IOCTL_READI_FRAMES:
	{
		struct snd_xferi xferi;
		struct snd_xferi __user *_xferi = arg;
		struct snd_pcm_runtime *runtime = substream->runtime;
		snd_pcm_sframes_t result;
		if (runtime->status->state == SNDRV_PCM_STATE_OPEN)
			return -EBADFD;
		if (put_user(0, &_xferi->result))
			return -EFAULT;
		if (copy_from_user(&xferi, _xferi, sizeof(xferi)))
			return -EFAULT;
		result = snd_pcm_lib_read(substream, xferi.buf, xferi.frames);
		__put_user(result, &_xferi->result);
		return result < 0 ? result : 0;
	}
	case SNDRV_PCM_IOCTL_READN_FRAMES:
	{
		struct snd_xfern xfern;
		struct snd_xfern __user *_xfern = arg;
		struct snd_pcm_runtime *runtime = substream->runtime;
		void *bufs;
		snd_pcm_sframes_t result;
		if (runtime->status->state == SNDRV_PCM_STATE_OPEN)
			return -EBADFD;
		if (runtime->channels > 128)
			return -EINVAL;
		if (put_user(0, &_xfern->result))
			return -EFAULT;
		if (copy_from_user(&xfern, _xfern, sizeof(xfern)))
			return -EFAULT;

		bufs = memdup_user(xfern.bufs,
				   sizeof(void *) * runtime->channels);
		if (IS_ERR(bufs))
			return PTR_ERR(bufs);
		result = snd_pcm_lib_readv(substream, bufs, xfern.frames);
		kfree(bufs);
		__put_user(result, &_xfern->result);
		return result < 0 ? result : 0;
	}
	case SNDRV_PCM_IOCTL_REWIND:
	{
		snd_pcm_uframes_t frames;
		snd_pcm_uframes_t __user *_frames = arg;
		snd_pcm_sframes_t result;
		if (get_user(frames, _frames))
			return -EFAULT;
		if (put_user(0, _frames))
			return -EFAULT;
		result = snd_pcm_capture_rewind(substream, frames);
		__put_user(result, _frames);
		return result < 0 ? result : 0;
	}
	case SNDRV_PCM_IOCTL_FORWARD:
	{
		snd_pcm_uframes_t frames;
		snd_pcm_uframes_t __user *_frames = arg;
		snd_pcm_sframes_t result;
		if (get_user(frames, _frames))
			return -EFAULT;
		if (put_user(0, _frames))
			return -EFAULT;
		result = snd_pcm_capture_forward(substream, frames);
		__put_user(result, _frames);
		return result < 0 ? result : 0;
	}
	}
	return snd_pcm_common_ioctl1(file, substream, cmd, arg);
}

static long snd_pcm_playback_ioctl(struct file *file, unsigned int cmd,
				   unsigned long arg)
{
	struct snd_pcm_file *pcm_file;

	pcm_file = file->private_data;

	if (((cmd >> 8) & 0xff) != 'A')
		return -ENOTTY;

	return snd_pcm_playback_ioctl1(file, pcm_file->substream, cmd,
				       (void __user *)arg);
}

static long snd_pcm_capture_ioctl(struct file *file, unsigned int cmd,
				  unsigned long arg)
{
	struct snd_pcm_file *pcm_file;

	pcm_file = file->private_data;

	if (((cmd >> 8) & 0xff) != 'A')
		return -ENOTTY;

	return snd_pcm_capture_ioctl1(file, pcm_file->substream, cmd,
				      (void __user *)arg);
}

int snd_pcm_kernel_ioctl(struct snd_pcm_substream *substream,
			 unsigned int cmd, void *arg)
{
	mm_segment_t fs;
	int result;
	
	fs = snd_enter_user();
	switch (substream->stream) {
	case SNDRV_PCM_STREAM_PLAYBACK:
		result = snd_pcm_playback_ioctl1(NULL, substream, cmd,
						 (void __user *)arg);
		break;
	case SNDRV_PCM_STREAM_CAPTURE:
		result = snd_pcm_capture_ioctl1(NULL, substream, cmd,
						(void __user *)arg);
		break;
	default:
		result = -EINVAL;
		break;
	}
	snd_leave_user(fs);
	return result;
}

EXPORT_SYMBOL(snd_pcm_kernel_ioctl);

static ssize_t snd_pcm_read(struct file *file, char __user *buf, size_t count,
			    loff_t * offset)
{
	struct snd_pcm_file *pcm_file;
	struct snd_pcm_substream *substream;
	struct snd_pcm_runtime *runtime;
	snd_pcm_sframes_t result;

	pcm_file = file->private_data;
	substream = pcm_file->substream;
	if (PCM_RUNTIME_CHECK(substream))
		return -ENXIO;
	runtime = substream->runtime;
	if (runtime->status->state == SNDRV_PCM_STATE_OPEN)
		return -EBADFD;
	if (!frame_aligned(runtime, count))
		return -EINVAL;
	count = bytes_to_frames(runtime, count);
	result = snd_pcm_lib_read(substream, buf, count);
	if (result > 0)
		result = frames_to_bytes(runtime, result);
	return result;
}

static ssize_t snd_pcm_write(struct file *file, const char __user *buf,
			     size_t count, loff_t * offset)
{
	struct snd_pcm_file *pcm_file;
	struct snd_pcm_substream *substream;
	struct snd_pcm_runtime *runtime;
	snd_pcm_sframes_t result;

	pcm_file = file->private_data;
	substream = pcm_file->substream;
	if (PCM_RUNTIME_CHECK(substream))
		return -ENXIO;
	runtime = substream->runtime;
	if (runtime->status->state == SNDRV_PCM_STATE_OPEN)
		return -EBADFD;
	if (!frame_aligned(runtime, count))
		return -EINVAL;
	count = bytes_to_frames(runtime, count);
	result = snd_pcm_lib_write(substream, buf, count);
	if (result > 0)
		result = frames_to_bytes(runtime, result);
	return result;
}

static ssize_t snd_pcm_readv(struct kiocb *iocb, struct iov_iter *to)
{
	struct snd_pcm_file *pcm_file;
	struct snd_pcm_substream *substream;
	struct snd_pcm_runtime *runtime;
	snd_pcm_sframes_t result;
	unsigned long i;
	void __user **bufs;
	snd_pcm_uframes_t frames;

	pcm_file = iocb->ki_filp->private_data;
	substream = pcm_file->substream;
	if (PCM_RUNTIME_CHECK(substream))
		return -ENXIO;
	runtime = substream->runtime;
	if (runtime->status->state == SNDRV_PCM_STATE_OPEN)
		return -EBADFD;
	if (!iter_is_iovec(to))
		return -EINVAL;
	if (to->nr_segs > 1024 || to->nr_segs != runtime->channels)
		return -EINVAL;
	if (!frame_aligned(runtime, to->iov->iov_len))
		return -EINVAL;
	frames = bytes_to_samples(runtime, to->iov->iov_len);
	bufs = kmalloc(sizeof(void *) * to->nr_segs, GFP_KERNEL);
	if (bufs == NULL)
		return -ENOMEM;
	for (i = 0; i < to->nr_segs; ++i)
		bufs[i] = to->iov[i].iov_base;
	result = snd_pcm_lib_readv(substream, bufs, frames);
	if (result > 0)
		result = frames_to_bytes(runtime, result);
	kfree(bufs);
	return result;
}

static ssize_t snd_pcm_writev(struct kiocb *iocb, struct iov_iter *from)
{
	struct snd_pcm_file *pcm_file;
	struct snd_pcm_substream *substream;
	struct snd_pcm_runtime *runtime;
	snd_pcm_sframes_t result;
	unsigned long i;
	void __user **bufs;
	snd_pcm_uframes_t frames;

	pcm_file = iocb->ki_filp->private_data;
	substream = pcm_file->substream;
	if (PCM_RUNTIME_CHECK(substream))
		return -ENXIO;
	runtime = substream->runtime;
	if (runtime->status->state == SNDRV_PCM_STATE_OPEN)
		return -EBADFD;
	if (!iter_is_iovec(from))
		return -EINVAL;
	if (from->nr_segs > 128 || from->nr_segs != runtime->channels ||
	    !frame_aligned(runtime, from->iov->iov_len))
		return -EINVAL;
	frames = bytes_to_samples(runtime, from->iov->iov_len);
	bufs = kmalloc(sizeof(void *) * from->nr_segs, GFP_KERNEL);
	if (bufs == NULL)
		return -ENOMEM;
	for (i = 0; i < from->nr_segs; ++i)
		bufs[i] = from->iov[i].iov_base;
	result = snd_pcm_lib_writev(substream, bufs, frames);
	if (result > 0)
		result = frames_to_bytes(runtime, result);
	kfree(bufs);
	return result;
}

static unsigned int snd_pcm_playback_poll(struct file *file, poll_table * wait)
{
	struct snd_pcm_file *pcm_file;
	struct snd_pcm_substream *substream;
	struct snd_pcm_runtime *runtime;
        unsigned int mask;
	snd_pcm_uframes_t avail;

	pcm_file = file->private_data;

	substream = pcm_file->substream;
	if (PCM_RUNTIME_CHECK(substream))
		return POLLOUT | POLLWRNORM | POLLERR;
	runtime = substream->runtime;

	poll_wait(file, &runtime->sleep, wait);

	snd_pcm_stream_lock_irq(substream);
	avail = snd_pcm_playback_avail(runtime);
	switch (runtime->status->state) {
	case SNDRV_PCM_STATE_RUNNING:
	case SNDRV_PCM_STATE_PREPARED:
	case SNDRV_PCM_STATE_PAUSED:
		if (avail >= runtime->control->avail_min) {
			mask = POLLOUT | POLLWRNORM;
			break;
		}
		/* Fall through */
	case SNDRV_PCM_STATE_DRAINING:
		mask = 0;
		break;
	default:
		mask = POLLOUT | POLLWRNORM | POLLERR;
		break;
	}
	snd_pcm_stream_unlock_irq(substream);
	return mask;
}

static unsigned int snd_pcm_capture_poll(struct file *file, poll_table * wait)
{
	struct snd_pcm_file *pcm_file;
	struct snd_pcm_substream *substream;
	struct snd_pcm_runtime *runtime;
        unsigned int mask;
	snd_pcm_uframes_t avail;

	pcm_file = file->private_data;

	substream = pcm_file->substream;
	if (PCM_RUNTIME_CHECK(substream))
		return POLLIN | POLLRDNORM | POLLERR;
	runtime = substream->runtime;

	poll_wait(file, &runtime->sleep, wait);

	snd_pcm_stream_lock_irq(substream);
	avail = snd_pcm_capture_avail(runtime);
	switch (runtime->status->state) {
	case SNDRV_PCM_STATE_RUNNING:
	case SNDRV_PCM_STATE_PREPARED:
	case SNDRV_PCM_STATE_PAUSED:
		if (avail >= runtime->control->avail_min) {
			mask = POLLIN | POLLRDNORM;
			break;
		}
		mask = 0;
		break;
	case SNDRV_PCM_STATE_DRAINING:
		if (avail > 0) {
			mask = POLLIN | POLLRDNORM;
			break;
		}
		/* Fall through */
	default:
		mask = POLLIN | POLLRDNORM | POLLERR;
		break;
	}
	snd_pcm_stream_unlock_irq(substream);
	return mask;
}

/*
 * mmap support
 */

/*
 * Only on coherent architectures, we can mmap the status and the control records
 * for effcient data transfer.  On others, we have to use HWSYNC ioctl...
 */
#if defined(CONFIG_X86) || defined(CONFIG_PPC) || defined(CONFIG_ALPHA)
/*
 * mmap status record
 */
static int snd_pcm_mmap_status_fault(struct vm_fault *vmf)
{
	struct snd_pcm_substream *substream = vmf->vma->vm_private_data;
	struct snd_pcm_runtime *runtime;
	
	if (substream == NULL)
		return VM_FAULT_SIGBUS;
	runtime = substream->runtime;
	vmf->page = virt_to_page(runtime->status);
	get_page(vmf->page);
	return 0;
}

static const struct vm_operations_struct snd_pcm_vm_ops_status =
{
	.fault =	snd_pcm_mmap_status_fault,
};

static int snd_pcm_mmap_status(struct snd_pcm_substream *substream, struct file *file,
			       struct vm_area_struct *area)
{
	long size;
	if (!(area->vm_flags & VM_READ))
		return -EINVAL;
	size = area->vm_end - area->vm_start;
	if (size != PAGE_ALIGN(sizeof(struct snd_pcm_mmap_status)))
		return -EINVAL;
	area->vm_ops = &snd_pcm_vm_ops_status;
	area->vm_private_data = substream;
	area->vm_flags |= VM_DONTEXPAND | VM_DONTDUMP;
	return 0;
}

/*
 * mmap control record
 */
static int snd_pcm_mmap_control_fault(struct vm_fault *vmf)
{
	struct snd_pcm_substream *substream = vmf->vma->vm_private_data;
	struct snd_pcm_runtime *runtime;
	
	if (substream == NULL)
		return VM_FAULT_SIGBUS;
	runtime = substream->runtime;
	vmf->page = virt_to_page(runtime->control);
	get_page(vmf->page);
	return 0;
}

static const struct vm_operations_struct snd_pcm_vm_ops_control =
{
	.fault =	snd_pcm_mmap_control_fault,
};

static int snd_pcm_mmap_control(struct snd_pcm_substream *substream, struct file *file,
				struct vm_area_struct *area)
{
	long size;
	if (!(area->vm_flags & VM_READ))
		return -EINVAL;
	size = area->vm_end - area->vm_start;
	if (size != PAGE_ALIGN(sizeof(struct snd_pcm_mmap_control)))
		return -EINVAL;
	area->vm_ops = &snd_pcm_vm_ops_control;
	area->vm_private_data = substream;
	area->vm_flags |= VM_DONTEXPAND | VM_DONTDUMP;
	return 0;
}
#else /* ! coherent mmap */
/*
 * don't support mmap for status and control records.
 */
static int snd_pcm_mmap_status(struct snd_pcm_substream *substream, struct file *file,
			       struct vm_area_struct *area)
{
	return -ENXIO;
}
static int snd_pcm_mmap_control(struct snd_pcm_substream *substream, struct file *file,
				struct vm_area_struct *area)
{
	return -ENXIO;
}
#endif /* coherent mmap */

static inline struct page *
snd_pcm_default_page_ops(struct snd_pcm_substream *substream, unsigned long ofs)
{
	void *vaddr = substream->runtime->dma_area + ofs;
	return virt_to_page(vaddr);
}

/*
 * fault callback for mmapping a RAM page
 */
static int snd_pcm_mmap_data_fault(struct vm_fault *vmf)
{
	struct snd_pcm_substream *substream = vmf->vma->vm_private_data;
	struct snd_pcm_runtime *runtime;
	unsigned long offset;
	struct page * page;
	size_t dma_bytes;
	
	if (substream == NULL)
		return VM_FAULT_SIGBUS;
	runtime = substream->runtime;
	offset = vmf->pgoff << PAGE_SHIFT;
	dma_bytes = PAGE_ALIGN(runtime->dma_bytes);
	if (offset > dma_bytes - PAGE_SIZE)
		return VM_FAULT_SIGBUS;
	if (substream->ops->page)
		page = substream->ops->page(substream, offset);
	else
		page = snd_pcm_default_page_ops(substream, offset);
	if (!page)
		return VM_FAULT_SIGBUS;
	get_page(page);
	vmf->page = page;
	return 0;
}

static const struct vm_operations_struct snd_pcm_vm_ops_data = {
	.open =		snd_pcm_mmap_data_open,
	.close =	snd_pcm_mmap_data_close,
};

static const struct vm_operations_struct snd_pcm_vm_ops_data_fault = {
	.open =		snd_pcm_mmap_data_open,
	.close =	snd_pcm_mmap_data_close,
	.fault =	snd_pcm_mmap_data_fault,
};

/*
 * mmap the DMA buffer on RAM
 */

/**
 * snd_pcm_lib_default_mmap - Default PCM data mmap function
 * @substream: PCM substream
 * @area: VMA
 *
 * This is the default mmap handler for PCM data.  When mmap pcm_ops is NULL,
 * this function is invoked implicitly.
 */
int snd_pcm_lib_default_mmap(struct snd_pcm_substream *substream,
			     struct vm_area_struct *area)
{
	area->vm_flags |= VM_DONTEXPAND | VM_DONTDUMP;
#ifdef CONFIG_GENERIC_ALLOCATOR
	if (substream->dma_buffer.dev.type == SNDRV_DMA_TYPE_DEV_IRAM) {
		area->vm_page_prot = pgprot_writecombine(area->vm_page_prot);
		return remap_pfn_range(area, area->vm_start,
				substream->dma_buffer.addr >> PAGE_SHIFT,
				area->vm_end - area->vm_start, area->vm_page_prot);
	}
#endif /* CONFIG_GENERIC_ALLOCATOR */
#ifndef CONFIG_X86 /* for avoiding warnings arch/x86/mm/pat.c */
	if (!substream->ops->page &&
	    substream->dma_buffer.dev.type == SNDRV_DMA_TYPE_DEV)
		return dma_mmap_coherent(substream->dma_buffer.dev.dev,
					 area,
					 substream->runtime->dma_area,
					 substream->runtime->dma_addr,
					 area->vm_end - area->vm_start);
#endif /* CONFIG_X86 */
	/* mmap with fault handler */
	area->vm_ops = &snd_pcm_vm_ops_data_fault;
	return 0;
}
EXPORT_SYMBOL_GPL(snd_pcm_lib_default_mmap);

/*
 * mmap the DMA buffer on I/O memory area
 */
#if SNDRV_PCM_INFO_MMAP_IOMEM
/**
 * snd_pcm_lib_mmap_iomem - Default PCM data mmap function for I/O mem
 * @substream: PCM substream
 * @area: VMA
 *
 * When your hardware uses the iomapped pages as the hardware buffer and
 * wants to mmap it, pass this function as mmap pcm_ops.  Note that this
 * is supposed to work only on limited architectures.
 */
int snd_pcm_lib_mmap_iomem(struct snd_pcm_substream *substream,
			   struct vm_area_struct *area)
{
	struct snd_pcm_runtime *runtime = substream->runtime;;

	area->vm_page_prot = pgprot_noncached(area->vm_page_prot);
	return vm_iomap_memory(area, runtime->dma_addr, runtime->dma_bytes);
}

EXPORT_SYMBOL(snd_pcm_lib_mmap_iomem);
#endif /* SNDRV_PCM_INFO_MMAP */

/*
 * mmap DMA buffer
 */
int snd_pcm_mmap_data(struct snd_pcm_substream *substream, struct file *file,
		      struct vm_area_struct *area)
{
	struct snd_pcm_runtime *runtime;
	long size;
	unsigned long offset;
	size_t dma_bytes;
	int err;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		if (!(area->vm_flags & (VM_WRITE|VM_READ)))
			return -EINVAL;
	} else {
		if (!(area->vm_flags & VM_READ))
			return -EINVAL;
	}
	runtime = substream->runtime;
	if (runtime->status->state == SNDRV_PCM_STATE_OPEN)
		return -EBADFD;
	if (!(runtime->info & SNDRV_PCM_INFO_MMAP))
		return -ENXIO;
	if (runtime->access == SNDRV_PCM_ACCESS_RW_INTERLEAVED ||
	    runtime->access == SNDRV_PCM_ACCESS_RW_NONINTERLEAVED)
		return -EINVAL;
	size = area->vm_end - area->vm_start;
	offset = area->vm_pgoff << PAGE_SHIFT;
	dma_bytes = PAGE_ALIGN(runtime->dma_bytes);
	if ((size_t)size > dma_bytes)
		return -EINVAL;
	if (offset > dma_bytes - size)
		return -EINVAL;

	area->vm_ops = &snd_pcm_vm_ops_data;
	area->vm_private_data = substream;
	if (substream->ops->mmap)
		err = substream->ops->mmap(substream, area);
	else
		err = snd_pcm_lib_default_mmap(substream, area);
	if (!err)
		atomic_inc(&substream->mmap_count);
	return err;
}

EXPORT_SYMBOL(snd_pcm_mmap_data);

static int snd_pcm_mmap(struct file *file, struct vm_area_struct *area)
{
	struct snd_pcm_file * pcm_file;
	struct snd_pcm_substream *substream;	
	unsigned long offset;
	
	pcm_file = file->private_data;
	substream = pcm_file->substream;
	if (PCM_RUNTIME_CHECK(substream))
		return -ENXIO;

	offset = area->vm_pgoff << PAGE_SHIFT;
	switch (offset) {
	case SNDRV_PCM_MMAP_OFFSET_STATUS:
		if (pcm_file->no_compat_mmap)
			return -ENXIO;
		return snd_pcm_mmap_status(substream, file, area);
	case SNDRV_PCM_MMAP_OFFSET_CONTROL:
		if (pcm_file->no_compat_mmap)
			return -ENXIO;
		return snd_pcm_mmap_control(substream, file, area);
	default:
		return snd_pcm_mmap_data(substream, file, area);
	}
	return 0;
}

static int snd_pcm_fasync(int fd, struct file * file, int on)
{
	struct snd_pcm_file * pcm_file;
	struct snd_pcm_substream *substream;
	struct snd_pcm_runtime *runtime;

	pcm_file = file->private_data;
	substream = pcm_file->substream;
	if (PCM_RUNTIME_CHECK(substream))
		return -ENXIO;
	runtime = substream->runtime;
	return fasync_helper(fd, file, on, &runtime->fasync);
}

/*
 * ioctl32 compat
 */
#ifdef CONFIG_COMPAT
#include "pcm_compat.c"
#else
#define snd_pcm_ioctl_compat	NULL
#endif

/*
 *  To be removed helpers to keep binary compatibility
 */

#ifdef CONFIG_SND_SUPPORT_OLD_API
#define __OLD_TO_NEW_MASK(x) ((x&7)|((x&0x07fffff8)<<5))
#define __NEW_TO_OLD_MASK(x) ((x&7)|((x&0xffffff00)>>5))

static void snd_pcm_hw_convert_from_old_params(struct snd_pcm_hw_params *params,
					       struct snd_pcm_hw_params_old *oparams)
{
	unsigned int i;

	memset(params, 0, sizeof(*params));
	params->flags = oparams->flags;
	for (i = 0; i < ARRAY_SIZE(oparams->masks); i++)
		params->masks[i].bits[0] = oparams->masks[i];
	memcpy(params->intervals, oparams->intervals, sizeof(oparams->intervals));
	params->rmask = __OLD_TO_NEW_MASK(oparams->rmask);
	params->cmask = __OLD_TO_NEW_MASK(oparams->cmask);
	params->info = oparams->info;
	params->msbits = oparams->msbits;
	params->rate_num = oparams->rate_num;
	params->rate_den = oparams->rate_den;
	params->fifo_size = oparams->fifo_size;
}

static void snd_pcm_hw_convert_to_old_params(struct snd_pcm_hw_params_old *oparams,
					     struct snd_pcm_hw_params *params)
{
	unsigned int i;

	memset(oparams, 0, sizeof(*oparams));
	oparams->flags = params->flags;
	for (i = 0; i < ARRAY_SIZE(oparams->masks); i++)
		oparams->masks[i] = params->masks[i].bits[0];
	memcpy(oparams->intervals, params->intervals, sizeof(oparams->intervals));
	oparams->rmask = __NEW_TO_OLD_MASK(params->rmask);
	oparams->cmask = __NEW_TO_OLD_MASK(params->cmask);
	oparams->info = params->info;
	oparams->msbits = params->msbits;
	oparams->rate_num = params->rate_num;
	oparams->rate_den = params->rate_den;
	oparams->fifo_size = params->fifo_size;
}

static int snd_pcm_hw_refine_old_user(struct snd_pcm_substream *substream,
				      struct snd_pcm_hw_params_old __user * _oparams)
{
	struct snd_pcm_hw_params *params;
	struct snd_pcm_hw_params_old *oparams = NULL;
	int err;

	params = kmalloc(sizeof(*params), GFP_KERNEL);
	if (!params)
		return -ENOMEM;

	oparams = memdup_user(_oparams, sizeof(*oparams));
	if (IS_ERR(oparams)) {
		err = PTR_ERR(oparams);
		goto out;
	}
	snd_pcm_hw_convert_from_old_params(params, oparams);
	err = snd_pcm_hw_refine(substream, params);
	snd_pcm_hw_convert_to_old_params(oparams, params);
	if (copy_to_user(_oparams, oparams, sizeof(*oparams))) {
		if (!err)
			err = -EFAULT;
	}

	kfree(oparams);
out:
	kfree(params);
	return err;
}

static int snd_pcm_hw_params_old_user(struct snd_pcm_substream *substream,
				      struct snd_pcm_hw_params_old __user * _oparams)
{
	struct snd_pcm_hw_params *params;
	struct snd_pcm_hw_params_old *oparams = NULL;
	int err;

	params = kmalloc(sizeof(*params), GFP_KERNEL);
	if (!params)
		return -ENOMEM;

	oparams = memdup_user(_oparams, sizeof(*oparams));
	if (IS_ERR(oparams)) {
		err = PTR_ERR(oparams);
		goto out;
	}
	snd_pcm_hw_convert_from_old_params(params, oparams);
	err = snd_pcm_hw_params(substream, params);
	snd_pcm_hw_convert_to_old_params(oparams, params);
	if (copy_to_user(_oparams, oparams, sizeof(*oparams))) {
		if (!err)
			err = -EFAULT;
	}

	kfree(oparams);
out:
	kfree(params);
	return err;
}
#endif /* CONFIG_SND_SUPPORT_OLD_API */

#ifndef CONFIG_MMU
static unsigned long snd_pcm_get_unmapped_area(struct file *file,
					       unsigned long addr,
					       unsigned long len,
					       unsigned long pgoff,
					       unsigned long flags)
{
	struct snd_pcm_file *pcm_file = file->private_data;
	struct snd_pcm_substream *substream = pcm_file->substream;
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned long offset = pgoff << PAGE_SHIFT;

	switch (offset) {
	case SNDRV_PCM_MMAP_OFFSET_STATUS:
		return (unsigned long)runtime->status;
	case SNDRV_PCM_MMAP_OFFSET_CONTROL:
		return (unsigned long)runtime->control;
	default:
		return (unsigned long)runtime->dma_area + offset;
	}
}
#else
# define snd_pcm_get_unmapped_area NULL
#endif

/*
 *  Register section
 */

const struct file_operations snd_pcm_f_ops[2] = {
	{
		.owner =		THIS_MODULE,
		.write =		snd_pcm_write,
		.write_iter =		snd_pcm_writev,
		.open =			snd_pcm_playback_open,
		.release =		snd_pcm_release,
		.llseek =		no_llseek,
		.poll =			snd_pcm_playback_poll,
		.unlocked_ioctl =	snd_pcm_playback_ioctl,
		.compat_ioctl = 	snd_pcm_ioctl_compat,
		.mmap =			snd_pcm_mmap,
		.fasync =		snd_pcm_fasync,
		.get_unmapped_area =	snd_pcm_get_unmapped_area,
	},
	{
		.owner =		THIS_MODULE,
		.read =			snd_pcm_read,
		.read_iter =		snd_pcm_readv,
		.open =			snd_pcm_capture_open,
		.release =		snd_pcm_release,
		.llseek =		no_llseek,
		.poll =			snd_pcm_capture_poll,
		.unlocked_ioctl =	snd_pcm_capture_ioctl,
		.compat_ioctl = 	snd_pcm_ioctl_compat,
		.mmap =			snd_pcm_mmap,
		.fasync =		snd_pcm_fasync,
		.get_unmapped_area =	snd_pcm_get_unmapped_area,
	}
};
