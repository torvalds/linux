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
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/time.h>
#include <linux/pm_qos_params.h>
#include <linux/uio.h>
#include <sound/core.h>
#include <sound/control.h>
#include <sound/info.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/timer.h>
#include <sound/minors.h>
#include <asm/io.h>

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

DEFINE_RWLOCK(snd_pcm_link_rwlock);
EXPORT_SYMBOL(snd_pcm_link_rwlock);

static DECLARE_RWSEM(snd_pcm_link_rwsem);

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

	snd_assert(substream != NULL, return -ENXIO);
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

#undef RULES_DEBUG

#ifdef RULES_DEBUG
#define HW_PARAM(v) [SNDRV_PCM_HW_PARAM_##v] = #v
char *snd_pcm_hw_param_names[] = {
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
		printk("%s = ", snd_pcm_hw_param_names[k]);
		printk("%04x%04x%04x%04x -> ", m->bits[3], m->bits[2], m->bits[1], m->bits[0]);
#endif
		changed = snd_mask_refine(m, constrs_mask(constrs, k));
#ifdef RULES_DEBUG
		printk("%04x%04x%04x%04x\n", m->bits[3], m->bits[2], m->bits[1], m->bits[0]);
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
		printk("%s = ", snd_pcm_hw_param_names[k]);
		if (i->empty)
			printk("empty");
		else
			printk("%c%u %u%c", 
			       i->openmin ? '(' : '[', i->min,
			       i->max, i->openmax ? ')' : ']');
		printk(" -> ");
#endif
		changed = snd_interval_refine(i, constrs_interval(constrs, k));
#ifdef RULES_DEBUG
		if (i->empty)
			printk("empty\n");
		else 
			printk("%c%u %u%c\n", 
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
			printk("Rule %d [%p]: ", k, r->func);
			if (r->var >= 0) {
				printk("%s = ", snd_pcm_hw_param_names[r->var]);
				if (hw_is_mask(r->var)) {
					m = hw_param_mask(params, r->var);
					printk("%x", *m->bits);
				} else {
					i = hw_param_interval(params, r->var);
					if (i->empty)
						printk("empty");
					else
						printk("%c%u %u%c", 
						       i->openmin ? '(' : '[', i->min,
						       i->max, i->openmax ? ')' : ']');
				}
			}
#endif
			changed = r->func(params, r);
#ifdef RULES_DEBUG
			if (r->var >= 0) {
				printk(" -> ");
				if (hw_is_mask(r->var))
					printk("%x", *m->bits);
				else {
					if (i->empty)
						printk("empty");
					else
						printk("%c%u %u%c", 
						       i->openmin ? '(' : '[', i->min,
						       i->max, i->openmax ? ')' : ']');
				}
			}
			printk("\n");
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
	if (!params->info)
		params->info = hw->info;
	if (!params->fifo_size)
		params->fifo_size = hw->fifo_size;
	params->rmask = 0;
	return 0;
}

EXPORT_SYMBOL(snd_pcm_hw_refine);

static int snd_pcm_hw_refine_user(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params __user * _params)
{
	struct snd_pcm_hw_params *params;
	int err;

	params = kmalloc(sizeof(*params), GFP_KERNEL);
	if (!params) {
		err = -ENOMEM;
		goto out;
	}
	if (copy_from_user(params, _params, sizeof(*params))) {
		err = -EFAULT;
		goto out;
	}
	err = snd_pcm_hw_refine(substream, params);
	if (copy_to_user(_params, params, sizeof(*params))) {
		if (!err)
			err = -EFAULT;
	}
out:
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

static int snd_pcm_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params)
{
	struct snd_pcm_runtime *runtime;
	int err, usecs;
	unsigned int bits;
	snd_pcm_uframes_t frames;

	snd_assert(substream != NULL, return -ENXIO);
	runtime = substream->runtime;
	snd_assert(runtime != NULL, return -ENXIO);
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
#if defined(CONFIG_SND_PCM_OSS) || defined(CONFIG_SND_PCM_OSS_MODULE)
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
	runtime->status->state = SNDRV_PCM_STATE_SETUP;

	pm_qos_remove_requirement(PM_QOS_CPU_DMA_LATENCY,
				substream->latency_id);
	if ((usecs = period_to_usecs(runtime)) >= 0)
		pm_qos_add_requirement(PM_QOS_CPU_DMA_LATENCY,
					substream->latency_id, usecs);
	return 0;
 _error:
	/* hardware might be unuseable from this time,
	   so we force application to retry to set
	   the correct hardware parameter settings */
	runtime->status->state = SNDRV_PCM_STATE_OPEN;
	if (substream->ops->hw_free != NULL)
		substream->ops->hw_free(substream);
	return err;
}

static int snd_pcm_hw_params_user(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params __user * _params)
{
	struct snd_pcm_hw_params *params;
	int err;

	params = kmalloc(sizeof(*params), GFP_KERNEL);
	if (!params) {
		err = -ENOMEM;
		goto out;
	}
	if (copy_from_user(params, _params, sizeof(*params))) {
		err = -EFAULT;
		goto out;
	}
	err = snd_pcm_hw_params(substream, params);
	if (copy_to_user(_params, params, sizeof(*params))) {
		if (!err)
			err = -EFAULT;
	}
out:
	kfree(params);
	return err;
}

static int snd_pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime;
	int result = 0;

	snd_assert(substream != NULL, return -ENXIO);
	runtime = substream->runtime;
	snd_assert(runtime != NULL, return -ENXIO);
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
	runtime->status->state = SNDRV_PCM_STATE_OPEN;
	pm_qos_remove_requirement(PM_QOS_CPU_DMA_LATENCY,
		substream->latency_id);
	return result;
}

static int snd_pcm_sw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_sw_params *params)
{
	struct snd_pcm_runtime *runtime;

	snd_assert(substream != NULL, return -ENXIO);
	runtime = substream->runtime;
	snd_assert(runtime != NULL, return -ENXIO);
	snd_pcm_stream_lock_irq(substream);
	if (runtime->status->state == SNDRV_PCM_STATE_OPEN) {
		snd_pcm_stream_unlock_irq(substream);
		return -EBADFD;
	}
	snd_pcm_stream_unlock_irq(substream);

	if (params->tstamp_mode > SNDRV_PCM_TSTAMP_LAST)
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
	snd_pcm_stream_lock_irq(substream);
	runtime->tstamp_mode = params->tstamp_mode;
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
		wake_up(&runtime->sleep);
	}
	snd_pcm_stream_unlock_irq(substream);
	return 0;
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
	status->state = runtime->status->state;
	status->suspended_state = runtime->status->suspended_state;
	if (status->state == SNDRV_PCM_STATE_OPEN)
		goto _end;
	status->trigger_tstamp = runtime->trigger_tstamp;
	if (snd_pcm_running(substream)) {
		snd_pcm_update_hw_ptr(substream);
		if (runtime->tstamp_mode == SNDRV_PCM_TSTAMP_ENABLE) {
			status->tstamp = runtime->status->tstamp;
			goto _tstamp_end;
		}
	}
	snd_pcm_gettime(runtime, &status->tstamp);
 _tstamp_end:
	status->appl_ptr = runtime->control->appl_ptr;
	status->hw_ptr = runtime->status->hw_ptr;
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		status->avail = snd_pcm_playback_avail(runtime);
		if (runtime->status->state == SNDRV_PCM_STATE_RUNNING ||
		    runtime->status->state == SNDRV_PCM_STATE_DRAINING)
			status->delay = runtime->buffer_size - status->avail;
		else
			status->delay = 0;
	} else {
		status->avail = snd_pcm_capture_avail(runtime);
		if (runtime->status->state == SNDRV_PCM_STATE_RUNNING)
			status->delay = status->avail;
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
			       struct snd_pcm_status __user * _status)
{
	struct snd_pcm_status status;
	struct snd_pcm_runtime *runtime;
	int res;
	
	snd_assert(substream != NULL, return -ENXIO);
	runtime = substream->runtime;
	memset(&status, 0, sizeof(status));
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
	
	snd_assert(substream != NULL, return -ENXIO);
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
static int snd_pcm_action_group(struct action_ops *ops,
				struct snd_pcm_substream *substream,
				int state, int do_lock)
{
	struct snd_pcm_substream *s = NULL;
	struct snd_pcm_substream *s1;
	int res = 0;

	snd_pcm_group_for_each_entry(s, substream) {
		if (do_lock && s != substream)
			spin_lock_nested(&s->self_group.lock,
					 SINGLE_DEPTH_NESTING);
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
			if (s1 != substream)
				spin_unlock(&s1->self_group.lock);
			if (s1 == s)	/* end */
				break;
		}
	}
	return res;
}

/*
 *  Note: call with stream lock
 */
static int snd_pcm_action_single(struct action_ops *ops,
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
static int snd_pcm_action(struct action_ops *ops,
			  struct snd_pcm_substream *substream,
			  int state)
{
	int res;

	if (snd_pcm_stream_linked(substream)) {
		if (!spin_trylock(&substream->group->lock)) {
			spin_unlock(&substream->self_group.lock);
			spin_lock(&substream->group->lock);
			spin_lock(&substream->self_group.lock);
		}
		res = snd_pcm_action_group(ops, substream, state, 1);
		spin_unlock(&substream->group->lock);
	} else {
		res = snd_pcm_action_single(ops, substream, state);
	}
	return res;
}

/*
 *  Note: don't use any locks before
 */
static int snd_pcm_action_lock_irq(struct action_ops *ops,
				   struct snd_pcm_substream *substream,
				   int state)
{
	int res;

	read_lock_irq(&snd_pcm_link_rwlock);
	if (snd_pcm_stream_linked(substream)) {
		spin_lock(&substream->group->lock);
		spin_lock(&substream->self_group.lock);
		res = snd_pcm_action_group(ops, substream, state, 1);
		spin_unlock(&substream->self_group.lock);
		spin_unlock(&substream->group->lock);
	} else {
		spin_lock(&substream->self_group.lock);
		res = snd_pcm_action_single(ops, substream, state);
		spin_unlock(&substream->self_group.lock);
	}
	read_unlock_irq(&snd_pcm_link_rwlock);
	return res;
}

/*
 */
static int snd_pcm_action_nonatomic(struct action_ops *ops,
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
	runtime->status->state = state;
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK &&
	    runtime->silence_size > 0)
		snd_pcm_playback_silence(substream, ULONG_MAX);
	if (substream->timer)
		snd_timer_notify(substream->timer, SNDRV_TIMER_EVENT_MSTART,
				 &runtime->trigger_tstamp);
}

static struct action_ops snd_pcm_action_start = {
	.pre_action = snd_pcm_pre_start,
	.do_action = snd_pcm_do_start,
	.undo_action = snd_pcm_undo_start,
	.post_action = snd_pcm_post_start
};

/**
 * snd_pcm_start
 * @substream: the PCM substream instance
 *
 * Start all linked streams.
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
		if (substream->timer)
			snd_timer_notify(substream->timer, SNDRV_TIMER_EVENT_MSTOP,
					 &runtime->trigger_tstamp);
		runtime->status->state = state;
	}
	wake_up(&runtime->sleep);
}

static struct action_ops snd_pcm_action_stop = {
	.pre_action = snd_pcm_pre_stop,
	.do_action = snd_pcm_do_stop,
	.post_action = snd_pcm_post_stop
};

/**
 * snd_pcm_stop
 * @substream: the PCM substream instance
 * @state: PCM state after stopping the stream
 *
 * Try to stop all running streams in the substream group.
 * The state of each stream is changed to the given value after that unconditionally.
 */
int snd_pcm_stop(struct snd_pcm_substream *substream, int state)
{
	return snd_pcm_action(&snd_pcm_action_stop, substream, state);
}

EXPORT_SYMBOL(snd_pcm_stop);

/**
 * snd_pcm_drain_done
 * @substream: the PCM substream
 *
 * Stop the DMA only when the given stream is playback.
 * The state is changed to SETUP.
 * Unlike snd_pcm_stop(), this affects only the given stream.
 */
int snd_pcm_drain_done(struct snd_pcm_substream *substream)
{
	return snd_pcm_action_single(&snd_pcm_action_stop, substream,
				     SNDRV_PCM_STATE_SETUP);
}

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
		if (substream->timer)
			snd_timer_notify(substream->timer,
					 SNDRV_TIMER_EVENT_MPAUSE,
					 &runtime->trigger_tstamp);
		wake_up(&runtime->sleep);
	} else {
		runtime->status->state = SNDRV_PCM_STATE_RUNNING;
		if (substream->timer)
			snd_timer_notify(substream->timer,
					 SNDRV_TIMER_EVENT_MCONTINUE,
					 &runtime->trigger_tstamp);
	}
}

static struct action_ops snd_pcm_action_pause = {
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
	if (substream->timer)
		snd_timer_notify(substream->timer, SNDRV_TIMER_EVENT_MSUSPEND,
				 &runtime->trigger_tstamp);
	runtime->status->suspended_state = runtime->status->state;
	runtime->status->state = SNDRV_PCM_STATE_SUSPENDED;
	wake_up(&runtime->sleep);
}

static struct action_ops snd_pcm_action_suspend = {
	.pre_action = snd_pcm_pre_suspend,
	.do_action = snd_pcm_do_suspend,
	.post_action = snd_pcm_post_suspend
};

/**
 * snd_pcm_suspend
 * @substream: the PCM substream
 *
 * Trigger SUSPEND to all linked streams.
 * After this call, all streams are changed to SUSPENDED state.
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
 * snd_pcm_suspend_all
 * @pcm: the PCM instance
 *
 * Trigger SUSPEND to all substreams in the given pcm.
 * After this call, all streams are changed to SUSPENDED state.
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
	if (substream->timer)
		snd_timer_notify(substream->timer, SNDRV_TIMER_EVENT_MRESUME,
				 &runtime->trigger_tstamp);
	runtime->status->state = runtime->status->suspended_state;
}

static struct action_ops snd_pcm_action_resume = {
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
	// snd_assert(runtime->status->hw_ptr < runtime->buffer_size, );
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

static struct action_ops snd_pcm_action_reset = {
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
	runtime->status->state = SNDRV_PCM_STATE_PREPARED;
}

static struct action_ops snd_pcm_action_prepare = {
	.pre_action = snd_pcm_pre_prepare,
	.do_action = snd_pcm_do_prepare,
	.post_action = snd_pcm_post_prepare
};

/**
 * snd_pcm_prepare
 * @substream: the PCM substream instance
 * @file: file to refer f_flags
 *
 * Prepare the PCM substream to be triggerable.
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
	if (substream->f_flags & O_NONBLOCK)
		return -EAGAIN;
	substream->runtime->trigger_master = substream;
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
			}
			break;
		case SNDRV_PCM_STATE_RUNNING:
			runtime->status->state = SNDRV_PCM_STATE_DRAINING;
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
	return 0;
}

static void snd_pcm_post_drain_init(struct snd_pcm_substream *substream, int state)
{
}

static struct action_ops snd_pcm_action_drain_init = {
	.pre_action = snd_pcm_pre_drain_init,
	.do_action = snd_pcm_do_drain_init,
	.post_action = snd_pcm_post_drain_init
};

struct drain_rec {
	struct snd_pcm_substream *substream;
	wait_queue_t wait;
	snd_pcm_uframes_t stop_threshold;
};

static int snd_pcm_drop(struct snd_pcm_substream *substream);

/*
 * Drain the stream(s).
 * When the substream is linked, sync until the draining of all playback streams
 * is finished.
 * After this call, all streams are supposed to be either SETUP or DRAINING
 * (capture only) state.
 */
static int snd_pcm_drain(struct snd_pcm_substream *substream)
{
	struct snd_card *card;
	struct snd_pcm_runtime *runtime;
	struct snd_pcm_substream *s;
	int result = 0;
	int i, num_drecs;
	struct drain_rec *drec, drec_tmp, *d;

	snd_assert(substream != NULL, return -ENXIO);
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

	/* allocate temporary record for drain sync */
	down_read(&snd_pcm_link_rwsem);
	if (snd_pcm_stream_linked(substream)) {
		drec = kmalloc(substream->group->count * sizeof(*drec), GFP_KERNEL);
		if (! drec) {
			up_read(&snd_pcm_link_rwsem);
			snd_power_unlock(card);
			return -ENOMEM;
		}
	} else
		drec = &drec_tmp;

	/* count only playback streams */
	num_drecs = 0;
	snd_pcm_group_for_each_entry(s, substream) {
		runtime = s->runtime;
		if (s->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			d = &drec[num_drecs++];
			d->substream = s;
			init_waitqueue_entry(&d->wait, current);
			add_wait_queue(&runtime->sleep, &d->wait);
			/* stop_threshold fixup to avoid endless loop when
			 * stop_threshold > buffer_size
			 */
			d->stop_threshold = runtime->stop_threshold;
			if (runtime->stop_threshold > runtime->buffer_size)
				runtime->stop_threshold = runtime->buffer_size;
		}
	}
	up_read(&snd_pcm_link_rwsem);

	snd_pcm_stream_lock_irq(substream);
	/* resume pause */
	if (substream->runtime->status->state == SNDRV_PCM_STATE_PAUSED)
		snd_pcm_pause(substream, 0);

	/* pre-start/stop - all running streams are changed to DRAINING state */
	result = snd_pcm_action(&snd_pcm_action_drain_init, substream, 0);
	if (result < 0) {
		snd_pcm_stream_unlock_irq(substream);
		goto _error;
	}

	for (;;) {
		long tout;
		if (signal_pending(current)) {
			result = -ERESTARTSYS;
			break;
		}
		/* all finished? */
		for (i = 0; i < num_drecs; i++) {
			runtime = drec[i].substream->runtime;
			if (runtime->status->state == SNDRV_PCM_STATE_DRAINING)
				break;
		}
		if (i == num_drecs)
			break; /* yes, all drained */

		set_current_state(TASK_INTERRUPTIBLE);
		snd_pcm_stream_unlock_irq(substream);
		snd_power_unlock(card);
		tout = schedule_timeout(10 * HZ);
		snd_power_lock(card);
		snd_pcm_stream_lock_irq(substream);
		if (tout == 0) {
			if (substream->runtime->status->state == SNDRV_PCM_STATE_SUSPENDED)
				result = -ESTRPIPE;
			else {
				snd_printd("playback drain error (DMA or IRQ trouble?)\n");
				snd_pcm_stop(substream, SNDRV_PCM_STATE_SETUP);
				result = -EIO;
			}
			break;
		}
	}

	snd_pcm_stream_unlock_irq(substream);

 _error:
	for (i = 0; i < num_drecs; i++) {
		d = &drec[i];
		runtime = d->substream->runtime;
		remove_wait_queue(&runtime->sleep, &d->wait);
		runtime->stop_threshold = d->stop_threshold;
	}

	if (drec != &drec_tmp)
		kfree(drec);
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
	struct snd_card *card;
	int result = 0;
	
	snd_assert(substream != NULL, return -ENXIO);
	runtime = substream->runtime;
	card = substream->pcm->card;

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


/* WARNING: Don't forget to fput back the file */
static struct file *snd_pcm_file_fd(int fd)
{
	struct file *file;
	struct inode *inode;
	unsigned int minor;

	file = fget(fd);
	if (!file)
		return NULL;
	inode = file->f_path.dentry->d_inode;
	if (!S_ISCHR(inode->i_mode) ||
	    imajor(inode) != snd_major) {
		fput(file);
		return NULL;
	}
	minor = iminor(inode);
	if (!snd_lookup_minor_data(minor, SNDRV_DEVICE_TYPE_PCM_PLAYBACK) &&
	    !snd_lookup_minor_data(minor, SNDRV_DEVICE_TYPE_PCM_CAPTURE)) {
		fput(file);
		return NULL;
	}
	return file;
}

/*
 * PCM link handling
 */
static int snd_pcm_link(struct snd_pcm_substream *substream, int fd)
{
	int res = 0;
	struct file *file;
	struct snd_pcm_file *pcm_file;
	struct snd_pcm_substream *substream1;

	file = snd_pcm_file_fd(fd);
	if (!file)
		return -EBADFD;
	pcm_file = file->private_data;
	substream1 = pcm_file->substream;
	down_write(&snd_pcm_link_rwsem);
	write_lock_irq(&snd_pcm_link_rwlock);
	if (substream->runtime->status->state == SNDRV_PCM_STATE_OPEN ||
	    substream->runtime->status->state != substream1->runtime->status->state) {
		res = -EBADFD;
		goto _end;
	}
	if (snd_pcm_stream_linked(substream1)) {
		res = -EALREADY;
		goto _end;
	}
	if (!snd_pcm_stream_linked(substream)) {
		substream->group = kmalloc(sizeof(struct snd_pcm_group), GFP_ATOMIC);
		if (substream->group == NULL) {
			res = -ENOMEM;
			goto _end;
		}
		spin_lock_init(&substream->group->lock);
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
	fput(file);
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

	down_write(&snd_pcm_link_rwsem);
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
	if (hw->info & SNDRV_PCM_INFO_MMAP) {
		if (hw->info & SNDRV_PCM_INFO_INTERLEAVED)
			mask |= 1 << SNDRV_PCM_ACCESS_MMAP_INTERLEAVED;
		if (hw->info & SNDRV_PCM_INFO_NONINTERLEAVED)
			mask |= 1 << SNDRV_PCM_ACCESS_MMAP_NONINTERLEAVED;
		if (hw->info & SNDRV_PCM_INFO_COMPLEX)
			mask |= 1 << SNDRV_PCM_ACCESS_MMAP_COMPLEX;
	}
	err = snd_pcm_hw_constraint_mask(runtime, SNDRV_PCM_HW_PARAM_ACCESS, mask);
	snd_assert(err >= 0, return -EINVAL);

	err = snd_pcm_hw_constraint_mask64(runtime, SNDRV_PCM_HW_PARAM_FORMAT, hw->formats);
	snd_assert(err >= 0, return -EINVAL);

	err = snd_pcm_hw_constraint_mask(runtime, SNDRV_PCM_HW_PARAM_SUBFORMAT, 1 << SNDRV_PCM_SUBFORMAT_STD);
	snd_assert(err >= 0, return -EINVAL);

	err = snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_CHANNELS,
					   hw->channels_min, hw->channels_max);
	snd_assert(err >= 0, return -EINVAL);

	err = snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_RATE,
					   hw->rate_min, hw->rate_max);
	snd_assert(err >= 0, return -EINVAL);

	err = snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_PERIOD_BYTES,
					   hw->period_bytes_min, hw->period_bytes_max);
	snd_assert(err >= 0, return -EINVAL);

	err = snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_PERIODS,
					   hw->periods_min, hw->periods_max);
	snd_assert(err >= 0, return -EINVAL);

	err = snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_BUFFER_BYTES,
					   hw->period_bytes_min, hw->buffer_bytes_max);
	snd_assert(err >= 0, return -EINVAL);

	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_BUFFER_BYTES, 
				  snd_pcm_hw_rule_buffer_bytes_max, substream,
				  SNDRV_PCM_HW_PARAM_BUFFER_BYTES, -1);
	if (err < 0)
		return err;

	/* FIXME: remove */
	if (runtime->dma_bytes) {
		err = snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_BUFFER_BYTES, 0, runtime->dma_bytes);
		snd_assert(err >= 0, return -EINVAL);
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
		if (substream->ops->hw_free != NULL)
			substream->ops->hw_free(substream);
		substream->ops->close(substream);
		substream->hw_opened = 0;
	}
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
		snd_printd("snd_pcm_hw_constraints_init failed\n");
		goto error;
	}

	if ((err = substream->ops->open(substream)) < 0)
		goto error;

	substream->hw_opened = 1;

	err = snd_pcm_hw_constraints_complete(substream);
	if (err < 0) {
		snd_printd("snd_pcm_hw_constraints_complete failed\n");
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
			     int stream,
			     struct snd_pcm_file **rpcm_file)
{
	struct snd_pcm_file *pcm_file;
	struct snd_pcm_substream *substream;
	struct snd_pcm_str *str;
	int err;

	snd_assert(rpcm_file != NULL, return -EINVAL);
	*rpcm_file = NULL;

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
		str = substream->pstr;
		substream->file = pcm_file;
		substream->pcm_release = pcm_release_private;
	}
	file->private_data = pcm_file;
	*rpcm_file = pcm_file;
	return 0;
}

static int snd_pcm_playback_open(struct inode *inode, struct file *file)
{
	struct snd_pcm *pcm;

	pcm = snd_lookup_minor_data(iminor(inode),
				    SNDRV_DEVICE_TYPE_PCM_PLAYBACK);
	return snd_pcm_open(file, pcm, SNDRV_PCM_STREAM_PLAYBACK);
}

static int snd_pcm_capture_open(struct inode *inode, struct file *file)
{
	struct snd_pcm *pcm;

	pcm = snd_lookup_minor_data(iminor(inode),
				    SNDRV_DEVICE_TYPE_PCM_CAPTURE);
	return snd_pcm_open(file, pcm, SNDRV_PCM_STREAM_CAPTURE);
}

static int snd_pcm_open(struct file *file, struct snd_pcm *pcm, int stream)
{
	int err;
	struct snd_pcm_file *pcm_file;
	wait_queue_t wait;

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
		err = snd_pcm_open_file(file, pcm, stream, &pcm_file);
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
	snd_assert(substream != NULL, return -ENXIO);
	pcm = substream->pcm;
	fasync_helper(-1, file, 0, &substream->runtime->fasync);
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
	case SNDRV_PCM_STATE_RUNNING:
		if ((err = snd_pcm_update_hw_ptr(substream)) < 0)
			break;
		/* Fall through */
	case SNDRV_PCM_STATE_PREPARED:
	case SNDRV_PCM_STATE_SUSPENDED:
		err = 0;
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
	runtime->tstamp_type = SNDRV_PCM_TSTAMP_TYPE_GETTIMEOFDAY;
	if (arg == SNDRV_PCM_TSTAMP_TYPE_MONOTONIC)
		runtime->tstamp_type = SNDRV_PCM_TSTAMP_TYPE_MONOTONIC;
	return 0;
}
		
static int snd_pcm_common_ioctl1(struct file *file,
				 struct snd_pcm_substream *substream,
				 unsigned int cmd, void __user *arg)
{
	snd_assert(substream != NULL, return -ENXIO);

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
		return snd_pcm_status_user(substream, arg);
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
		return snd_pcm_drain(substream);
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
	snd_printd("unknown ioctl = 0x%x\n", cmd);
	return -ENOTTY;
}

static int snd_pcm_playback_ioctl1(struct file *file,
				   struct snd_pcm_substream *substream,
				   unsigned int cmd, void __user *arg)
{
	snd_assert(substream != NULL, return -ENXIO);
	snd_assert(substream->stream == SNDRV_PCM_STREAM_PLAYBACK, return -EINVAL);
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
		bufs = kmalloc(sizeof(void *) * runtime->channels, GFP_KERNEL);
		if (bufs == NULL)
			return -ENOMEM;
		if (copy_from_user(bufs, xfern.bufs, sizeof(void *) * runtime->channels)) {
			kfree(bufs);
			return -EFAULT;
		}
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
	snd_assert(substream != NULL, return -ENXIO);
	snd_assert(substream->stream == SNDRV_PCM_STREAM_CAPTURE, return -EINVAL);
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
		bufs = kmalloc(sizeof(void *) * runtime->channels, GFP_KERNEL);
		if (bufs == NULL)
			return -ENOMEM;
		if (copy_from_user(bufs, xfern.bufs, sizeof(void *) * runtime->channels)) {
			kfree(bufs);
			return -EFAULT;
		}
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
	snd_assert(substream != NULL, return -ENXIO);
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
	snd_assert(substream != NULL, result = -ENXIO; goto end);
	runtime = substream->runtime;
	if (runtime->status->state == SNDRV_PCM_STATE_OPEN) {
		result = -EBADFD;
		goto end;
	}
	if (!frame_aligned(runtime, count)) {
		result = -EINVAL;
		goto end;
	}
	count = bytes_to_frames(runtime, count);
	result = snd_pcm_lib_write(substream, buf, count);
	if (result > 0)
		result = frames_to_bytes(runtime, result);
 end:
	return result;
}

static ssize_t snd_pcm_aio_read(struct kiocb *iocb, const struct iovec *iov,
			     unsigned long nr_segs, loff_t pos)

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
	snd_assert(substream != NULL, return -ENXIO);
	runtime = substream->runtime;
	if (runtime->status->state == SNDRV_PCM_STATE_OPEN)
		return -EBADFD;
	if (nr_segs > 1024 || nr_segs != runtime->channels)
		return -EINVAL;
	if (!frame_aligned(runtime, iov->iov_len))
		return -EINVAL;
	frames = bytes_to_samples(runtime, iov->iov_len);
	bufs = kmalloc(sizeof(void *) * nr_segs, GFP_KERNEL);
	if (bufs == NULL)
		return -ENOMEM;
	for (i = 0; i < nr_segs; ++i)
		bufs[i] = iov[i].iov_base;
	result = snd_pcm_lib_readv(substream, bufs, frames);
	if (result > 0)
		result = frames_to_bytes(runtime, result);
	kfree(bufs);
	return result;
}

static ssize_t snd_pcm_aio_write(struct kiocb *iocb, const struct iovec *iov,
			      unsigned long nr_segs, loff_t pos)
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
	snd_assert(substream != NULL, result = -ENXIO; goto end);
	runtime = substream->runtime;
	if (runtime->status->state == SNDRV_PCM_STATE_OPEN) {
		result = -EBADFD;
		goto end;
	}
	if (nr_segs > 128 || nr_segs != runtime->channels ||
	    !frame_aligned(runtime, iov->iov_len)) {
		result = -EINVAL;
		goto end;
	}
	frames = bytes_to_samples(runtime, iov->iov_len);
	bufs = kmalloc(sizeof(void *) * nr_segs, GFP_KERNEL);
	if (bufs == NULL)
		return -ENOMEM;
	for (i = 0; i < nr_segs; ++i)
		bufs[i] = iov[i].iov_base;
	result = snd_pcm_lib_writev(substream, bufs, frames);
	if (result > 0)
		result = frames_to_bytes(runtime, result);
	kfree(bufs);
 end:
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
	snd_assert(substream != NULL, return -ENXIO);
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
	snd_assert(substream != NULL, return -ENXIO);
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
static int snd_pcm_mmap_status_fault(struct vm_area_struct *area,
						struct vm_fault *vmf)
{
	struct snd_pcm_substream *substream = area->vm_private_data;
	struct snd_pcm_runtime *runtime;
	
	if (substream == NULL)
		return VM_FAULT_SIGBUS;
	runtime = substream->runtime;
	vmf->page = virt_to_page(runtime->status);
	get_page(vmf->page);
	return 0;
}

static struct vm_operations_struct snd_pcm_vm_ops_status =
{
	.fault =	snd_pcm_mmap_status_fault,
};

static int snd_pcm_mmap_status(struct snd_pcm_substream *substream, struct file *file,
			       struct vm_area_struct *area)
{
	struct snd_pcm_runtime *runtime;
	long size;
	if (!(area->vm_flags & VM_READ))
		return -EINVAL;
	runtime = substream->runtime;
	snd_assert(runtime != NULL, return -EAGAIN);
	size = area->vm_end - area->vm_start;
	if (size != PAGE_ALIGN(sizeof(struct snd_pcm_mmap_status)))
		return -EINVAL;
	area->vm_ops = &snd_pcm_vm_ops_status;
	area->vm_private_data = substream;
	area->vm_flags |= VM_RESERVED;
	return 0;
}

/*
 * mmap control record
 */
static int snd_pcm_mmap_control_fault(struct vm_area_struct *area,
						struct vm_fault *vmf)
{
	struct snd_pcm_substream *substream = area->vm_private_data;
	struct snd_pcm_runtime *runtime;
	
	if (substream == NULL)
		return VM_FAULT_SIGBUS;
	runtime = substream->runtime;
	vmf->page = virt_to_page(runtime->control);
	get_page(vmf->page);
	return 0;
}

static struct vm_operations_struct snd_pcm_vm_ops_control =
{
	.fault =	snd_pcm_mmap_control_fault,
};

static int snd_pcm_mmap_control(struct snd_pcm_substream *substream, struct file *file,
				struct vm_area_struct *area)
{
	struct snd_pcm_runtime *runtime;
	long size;
	if (!(area->vm_flags & VM_READ))
		return -EINVAL;
	runtime = substream->runtime;
	snd_assert(runtime != NULL, return -EAGAIN);
	size = area->vm_end - area->vm_start;
	if (size != PAGE_ALIGN(sizeof(struct snd_pcm_mmap_control)))
		return -EINVAL;
	area->vm_ops = &snd_pcm_vm_ops_control;
	area->vm_private_data = substream;
	area->vm_flags |= VM_RESERVED;
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

/*
 * fault callback for mmapping a RAM page
 */
static int snd_pcm_mmap_data_fault(struct vm_area_struct *area,
						struct vm_fault *vmf)
{
	struct snd_pcm_substream *substream = area->vm_private_data;
	struct snd_pcm_runtime *runtime;
	unsigned long offset;
	struct page * page;
	void *vaddr;
	size_t dma_bytes;
	
	if (substream == NULL)
		return VM_FAULT_SIGBUS;
	runtime = substream->runtime;
	offset = vmf->pgoff << PAGE_SHIFT;
	dma_bytes = PAGE_ALIGN(runtime->dma_bytes);
	if (offset > dma_bytes - PAGE_SIZE)
		return VM_FAULT_SIGBUS;
	if (substream->ops->page) {
		page = substream->ops->page(substream, offset);
		if (!page)
			return VM_FAULT_SIGBUS;
	} else {
		vaddr = runtime->dma_area + offset;
		page = virt_to_page(vaddr);
	}
	get_page(page);
	vmf->page = page;
	return 0;
}

static struct vm_operations_struct snd_pcm_vm_ops_data =
{
	.open =		snd_pcm_mmap_data_open,
	.close =	snd_pcm_mmap_data_close,
	.fault =	snd_pcm_mmap_data_fault,
};

/*
 * mmap the DMA buffer on RAM
 */
static int snd_pcm_default_mmap(struct snd_pcm_substream *substream,
				struct vm_area_struct *area)
{
	area->vm_ops = &snd_pcm_vm_ops_data;
	area->vm_private_data = substream;
	area->vm_flags |= VM_RESERVED;
	atomic_inc(&substream->mmap_count);
	return 0;
}

/*
 * mmap the DMA buffer on I/O memory area
 */
#if SNDRV_PCM_INFO_MMAP_IOMEM
static struct vm_operations_struct snd_pcm_vm_ops_data_mmio =
{
	.open =		snd_pcm_mmap_data_open,
	.close =	snd_pcm_mmap_data_close,
};

int snd_pcm_lib_mmap_iomem(struct snd_pcm_substream *substream,
			   struct vm_area_struct *area)
{
	long size;
	unsigned long offset;

#ifdef pgprot_noncached
	area->vm_page_prot = pgprot_noncached(area->vm_page_prot);
#endif
	area->vm_ops = &snd_pcm_vm_ops_data_mmio;
	area->vm_private_data = substream;
	area->vm_flags |= VM_IO;
	size = area->vm_end - area->vm_start;
	offset = area->vm_pgoff << PAGE_SHIFT;
	if (io_remap_pfn_range(area, area->vm_start,
				(substream->runtime->dma_addr + offset) >> PAGE_SHIFT,
				size, area->vm_page_prot))
		return -EAGAIN;
	atomic_inc(&substream->mmap_count);
	return 0;
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

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		if (!(area->vm_flags & (VM_WRITE|VM_READ)))
			return -EINVAL;
	} else {
		if (!(area->vm_flags & VM_READ))
			return -EINVAL;
	}
	runtime = substream->runtime;
	snd_assert(runtime != NULL, return -EAGAIN);
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

	if (substream->ops->mmap)
		return substream->ops->mmap(substream, area);
	else
		return snd_pcm_default_mmap(substream, area);
}

EXPORT_SYMBOL(snd_pcm_mmap_data);

static int snd_pcm_mmap(struct file *file, struct vm_area_struct *area)
{
	struct snd_pcm_file * pcm_file;
	struct snd_pcm_substream *substream;	
	unsigned long offset;
	
	pcm_file = file->private_data;
	substream = pcm_file->substream;
	snd_assert(substream != NULL, return -ENXIO);

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
	int err = -ENXIO;

	lock_kernel();
	pcm_file = file->private_data;
	substream = pcm_file->substream;
	snd_assert(substream != NULL, goto out);
	runtime = substream->runtime;

	err = fasync_helper(fd, file, on, &runtime->fasync);
out:
	unlock_kernel();
	if (err < 0)
		return err;
	return 0;
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
	if (!params) {
		err = -ENOMEM;
		goto out;
	}
	oparams = kmalloc(sizeof(*oparams), GFP_KERNEL);
	if (!oparams) {
		err = -ENOMEM;
		goto out;
	}

	if (copy_from_user(oparams, _oparams, sizeof(*oparams))) {
		err = -EFAULT;
		goto out;
	}
	snd_pcm_hw_convert_from_old_params(params, oparams);
	err = snd_pcm_hw_refine(substream, params);
	snd_pcm_hw_convert_to_old_params(oparams, params);
	if (copy_to_user(_oparams, oparams, sizeof(*oparams))) {
		if (!err)
			err = -EFAULT;
	}
out:
	kfree(params);
	kfree(oparams);
	return err;
}

static int snd_pcm_hw_params_old_user(struct snd_pcm_substream *substream,
				      struct snd_pcm_hw_params_old __user * _oparams)
{
	struct snd_pcm_hw_params *params;
	struct snd_pcm_hw_params_old *oparams = NULL;
	int err;

	params = kmalloc(sizeof(*params), GFP_KERNEL);
	if (!params) {
		err = -ENOMEM;
		goto out;
	}
	oparams = kmalloc(sizeof(*oparams), GFP_KERNEL);
	if (!oparams) {
		err = -ENOMEM;
		goto out;
	}
	if (copy_from_user(oparams, _oparams, sizeof(*oparams))) {
		err = -EFAULT;
		goto out;
	}
	snd_pcm_hw_convert_from_old_params(params, oparams);
	err = snd_pcm_hw_params(substream, params);
	snd_pcm_hw_convert_to_old_params(oparams, params);
	if (copy_to_user(_oparams, oparams, sizeof(*oparams))) {
		if (!err)
			err = -EFAULT;
	}
out:
	kfree(params);
	kfree(oparams);
	return err;
}
#endif /* CONFIG_SND_SUPPORT_OLD_API */

/*
 *  Register section
 */

const struct file_operations snd_pcm_f_ops[2] = {
	{
		.owner =		THIS_MODULE,
		.write =		snd_pcm_write,
		.aio_write =		snd_pcm_aio_write,
		.open =			snd_pcm_playback_open,
		.release =		snd_pcm_release,
		.poll =			snd_pcm_playback_poll,
		.unlocked_ioctl =	snd_pcm_playback_ioctl,
		.compat_ioctl = 	snd_pcm_ioctl_compat,
		.mmap =			snd_pcm_mmap,
		.fasync =		snd_pcm_fasync,
	},
	{
		.owner =		THIS_MODULE,
		.read =			snd_pcm_read,
		.aio_read =		snd_pcm_aio_read,
		.open =			snd_pcm_capture_open,
		.release =		snd_pcm_release,
		.poll =			snd_pcm_capture_poll,
		.unlocked_ioctl =	snd_pcm_capture_ioctl,
		.compat_ioctl = 	snd_pcm_ioctl_compat,
		.mmap =			snd_pcm_mmap,
		.fasync =		snd_pcm_fasync,
	}
};
