/*
 *  Digital Audio (PCM) abstract layer / OSS compatible
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
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

#if 0
#define PLUGIN_DEBUG
#endif
#if 0
#define OSS_DEBUG
#endif

#include <sound/driver.h>
#include <linux/init.h>
#include <linux/smp_lock.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/vmalloc.h>
#include <linux/moduleparam.h>
#include <linux/string.h>
#include <sound/core.h>
#include <sound/minors.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include "pcm_plugin.h"
#include <sound/info.h>
#include <linux/soundcard.h>
#include <sound/initval.h>

#define OSS_ALSAEMULVER		_SIOR ('M', 249, int)

static int dsp_map[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS-1)] = 0};
static int adsp_map[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS-1)] = 1};
static int nonblock_open = 1;

MODULE_AUTHOR("Jaroslav Kysela <perex@suse.cz>, Abramo Bagnara <abramo@alsa-project.org>");
MODULE_DESCRIPTION("PCM OSS emulation for ALSA.");
MODULE_LICENSE("GPL");
module_param_array(dsp_map, int, NULL, 0444);
MODULE_PARM_DESC(dsp_map, "PCM device number assigned to 1st OSS device.");
module_param_array(adsp_map, int, NULL, 0444);
MODULE_PARM_DESC(adsp_map, "PCM device number assigned to 2nd OSS device.");
module_param(nonblock_open, bool, 0644);
MODULE_PARM_DESC(nonblock_open, "Don't block opening busy PCM devices.");
MODULE_ALIAS_SNDRV_MINOR(SNDRV_MINOR_OSS_PCM);
MODULE_ALIAS_SNDRV_MINOR(SNDRV_MINOR_OSS_PCM1);

extern int snd_mixer_oss_ioctl_card(struct snd_card *card, unsigned int cmd, unsigned long arg);
static int snd_pcm_oss_get_rate(struct snd_pcm_oss_file *pcm_oss_file);
static int snd_pcm_oss_get_channels(struct snd_pcm_oss_file *pcm_oss_file);
static int snd_pcm_oss_get_format(struct snd_pcm_oss_file *pcm_oss_file);

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

static int snd_pcm_oss_plugin_clear(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_pcm_plugin *plugin, *next;
	
	plugin = runtime->oss.plugin_first;
	while (plugin) {
		next = plugin->next;
		snd_pcm_plugin_free(plugin);
		plugin = next;
	}
	runtime->oss.plugin_first = runtime->oss.plugin_last = NULL;
	return 0;
}

static int snd_pcm_plugin_insert(struct snd_pcm_plugin *plugin)
{
	struct snd_pcm_runtime *runtime = plugin->plug->runtime;
	plugin->next = runtime->oss.plugin_first;
	plugin->prev = NULL;
	if (runtime->oss.plugin_first) {
		runtime->oss.plugin_first->prev = plugin;
		runtime->oss.plugin_first = plugin;
	} else {
		runtime->oss.plugin_last =
		runtime->oss.plugin_first = plugin;
	}
	return 0;
}

int snd_pcm_plugin_append(struct snd_pcm_plugin *plugin)
{
	struct snd_pcm_runtime *runtime = plugin->plug->runtime;
	plugin->next = NULL;
	plugin->prev = runtime->oss.plugin_last;
	if (runtime->oss.plugin_last) {
		runtime->oss.plugin_last->next = plugin;
		runtime->oss.plugin_last = plugin;
	} else {
		runtime->oss.plugin_last =
		runtime->oss.plugin_first = plugin;
	}
	return 0;
}

static long snd_pcm_oss_bytes(struct snd_pcm_substream *substream, long frames)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	long buffer_size = snd_pcm_lib_buffer_bytes(substream);
	long bytes = frames_to_bytes(runtime, frames);
	if (buffer_size == runtime->oss.buffer_bytes)
		return bytes;
#if BITS_PER_LONG >= 64
	return runtime->oss.buffer_bytes * bytes / buffer_size;
#else
	{
		u64 bsize = (u64)runtime->oss.buffer_bytes * (u64)bytes;
		u32 rem;
		div64_32(&bsize, buffer_size, &rem);
		return (long)bsize;
	}
#endif
}

static long snd_pcm_alsa_frames(struct snd_pcm_substream *substream, long bytes)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	long buffer_size = snd_pcm_lib_buffer_bytes(substream);
	if (buffer_size == runtime->oss.buffer_bytes)
		return bytes_to_frames(runtime, bytes);
	return bytes_to_frames(runtime, (buffer_size * bytes) / runtime->oss.buffer_bytes);
}

static int snd_pcm_oss_format_from(int format)
{
	switch (format) {
	case AFMT_MU_LAW:	return SNDRV_PCM_FORMAT_MU_LAW;
	case AFMT_A_LAW:	return SNDRV_PCM_FORMAT_A_LAW;
	case AFMT_IMA_ADPCM:	return SNDRV_PCM_FORMAT_IMA_ADPCM;
	case AFMT_U8:		return SNDRV_PCM_FORMAT_U8;
	case AFMT_S16_LE:	return SNDRV_PCM_FORMAT_S16_LE;
	case AFMT_S16_BE:	return SNDRV_PCM_FORMAT_S16_BE;
	case AFMT_S8:		return SNDRV_PCM_FORMAT_S8;
	case AFMT_U16_LE:	return SNDRV_PCM_FORMAT_U16_LE;
	case AFMT_U16_BE:	return SNDRV_PCM_FORMAT_U16_BE;
	case AFMT_MPEG:		return SNDRV_PCM_FORMAT_MPEG;
	default:		return SNDRV_PCM_FORMAT_U8;
	}
}

static int snd_pcm_oss_format_to(int format)
{
	switch (format) {
	case SNDRV_PCM_FORMAT_MU_LAW:	return AFMT_MU_LAW;
	case SNDRV_PCM_FORMAT_A_LAW:	return AFMT_A_LAW;
	case SNDRV_PCM_FORMAT_IMA_ADPCM:	return AFMT_IMA_ADPCM;
	case SNDRV_PCM_FORMAT_U8:		return AFMT_U8;
	case SNDRV_PCM_FORMAT_S16_LE:	return AFMT_S16_LE;
	case SNDRV_PCM_FORMAT_S16_BE:	return AFMT_S16_BE;
	case SNDRV_PCM_FORMAT_S8:		return AFMT_S8;
	case SNDRV_PCM_FORMAT_U16_LE:	return AFMT_U16_LE;
	case SNDRV_PCM_FORMAT_U16_BE:	return AFMT_U16_BE;
	case SNDRV_PCM_FORMAT_MPEG:		return AFMT_MPEG;
	default:			return -EINVAL;
	}
}

static int snd_pcm_oss_period_size(struct snd_pcm_substream *substream, 
				   struct snd_pcm_hw_params *oss_params,
				   struct snd_pcm_hw_params *slave_params)
{
	size_t s;
	size_t oss_buffer_size, oss_period_size, oss_periods;
	size_t min_period_size, max_period_size;
	struct snd_pcm_runtime *runtime = substream->runtime;
	size_t oss_frame_size;

	oss_frame_size = snd_pcm_format_physical_width(params_format(oss_params)) *
			 params_channels(oss_params) / 8;

	oss_buffer_size = snd_pcm_plug_client_size(substream,
						   snd_pcm_hw_param_value_max(slave_params, SNDRV_PCM_HW_PARAM_BUFFER_SIZE, NULL)) * oss_frame_size;
	oss_buffer_size = 1 << ld2(oss_buffer_size);
	if (atomic_read(&runtime->mmap_count)) {
		if (oss_buffer_size > runtime->oss.mmap_bytes)
			oss_buffer_size = runtime->oss.mmap_bytes;
	}

	if (substream->oss.setup &&
	    substream->oss.setup->period_size > 16)
		oss_period_size = substream->oss.setup->period_size;
	else if (runtime->oss.fragshift) {
		oss_period_size = 1 << runtime->oss.fragshift;
		if (oss_period_size > oss_buffer_size / 2)
			oss_period_size = oss_buffer_size / 2;
	} else {
		int sd;
		size_t bytes_per_sec = params_rate(oss_params) * snd_pcm_format_physical_width(params_format(oss_params)) * params_channels(oss_params) / 8;

		oss_period_size = oss_buffer_size;
		do {
			oss_period_size /= 2;
		} while (oss_period_size > bytes_per_sec);
		if (runtime->oss.subdivision == 0) {
			sd = 4;
			if (oss_period_size / sd > 4096)
				sd *= 2;
			if (oss_period_size / sd < 4096)
				sd = 1;
		} else
			sd = runtime->oss.subdivision;
		oss_period_size /= sd;
		if (oss_period_size < 16)
			oss_period_size = 16;
	}

	min_period_size = snd_pcm_plug_client_size(substream,
						   snd_pcm_hw_param_value_min(slave_params, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, NULL));
	min_period_size *= oss_frame_size;
	min_period_size = 1 << (ld2(min_period_size - 1) + 1);
	if (oss_period_size < min_period_size)
		oss_period_size = min_period_size;

	max_period_size = snd_pcm_plug_client_size(substream,
						   snd_pcm_hw_param_value_max(slave_params, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, NULL));
	max_period_size *= oss_frame_size;
	max_period_size = 1 << ld2(max_period_size);
	if (oss_period_size > max_period_size)
		oss_period_size = max_period_size;

	oss_periods = oss_buffer_size / oss_period_size;

	if (substream->oss.setup) {
		if (substream->oss.setup->periods > 1)
			oss_periods = substream->oss.setup->periods;
	}

	s = snd_pcm_hw_param_value_max(slave_params, SNDRV_PCM_HW_PARAM_PERIODS, NULL);
	if (runtime->oss.maxfrags && s > runtime->oss.maxfrags)
		s = runtime->oss.maxfrags;
	if (oss_periods > s)
		oss_periods = s;

	s = snd_pcm_hw_param_value_min(slave_params, SNDRV_PCM_HW_PARAM_PERIODS, NULL);
	if (s < 2)
		s = 2;
	if (oss_periods < s)
		oss_periods = s;

	while (oss_period_size * oss_periods > oss_buffer_size)
		oss_period_size /= 2;

	snd_assert(oss_period_size >= 16, return -EINVAL);
	runtime->oss.period_bytes = oss_period_size;
	runtime->oss.period_frames = 1;
	runtime->oss.periods = oss_periods;
	return 0;
}

static int choose_rate(struct snd_pcm_substream *substream,
		       struct snd_pcm_hw_params *params, unsigned int best_rate)
{
	struct snd_interval *it;
	struct snd_pcm_hw_params *save;
	unsigned int rate, prev;

	save = kmalloc(sizeof(*save), GFP_KERNEL);
	if (save == NULL)
		return -ENOMEM;
	*save = *params;
	it = hw_param_interval(save, SNDRV_PCM_HW_PARAM_RATE);

	/* try multiples of the best rate */
	rate = best_rate;
	for (;;) {
		if (it->max < rate || (it->max == rate && it->openmax))
			break;
		if (it->min < rate || (it->min == rate && !it->openmin)) {
			int ret;
			ret = snd_pcm_hw_param_set(substream, params,
						   SNDRV_PCM_HW_PARAM_RATE,
						   rate, 0);
			if (ret == (int)rate) {
				kfree(save);
				return rate;
			}
			*params = *save;
		}
		prev = rate;
		rate += best_rate;
		if (rate <= prev)
			break;
	}

	/* not found, use the nearest rate */
	kfree(save);
	return snd_pcm_hw_param_near(substream, params, SNDRV_PCM_HW_PARAM_RATE, best_rate, NULL);
}

static int snd_pcm_oss_change_params(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_pcm_hw_params *params, *sparams;
	struct snd_pcm_sw_params *sw_params;
	ssize_t oss_buffer_size, oss_period_size;
	size_t oss_frame_size;
	int err;
	int direct;
	int format, sformat, n;
	struct snd_mask sformat_mask;
	struct snd_mask mask;

	sw_params = kmalloc(sizeof(*sw_params), GFP_KERNEL);
	params = kmalloc(sizeof(*params), GFP_KERNEL);
	sparams = kmalloc(sizeof(*sparams), GFP_KERNEL);
	if (!sw_params || !params || !sparams) {
		snd_printd("No memory\n");
		err = -ENOMEM;
		goto failure;
	}

	if (atomic_read(&runtime->mmap_count)) {
		direct = 1;
	} else {
		struct snd_pcm_oss_setup *setup = substream->oss.setup;
		direct = (setup != NULL && setup->direct);
	}

	_snd_pcm_hw_params_any(sparams);
	_snd_pcm_hw_param_setinteger(sparams, SNDRV_PCM_HW_PARAM_PERIODS);
	_snd_pcm_hw_param_min(sparams, SNDRV_PCM_HW_PARAM_PERIODS, 2, 0);
	snd_mask_none(&mask);
	if (atomic_read(&runtime->mmap_count))
		snd_mask_set(&mask, SNDRV_PCM_ACCESS_MMAP_INTERLEAVED);
	else {
		snd_mask_set(&mask, SNDRV_PCM_ACCESS_RW_INTERLEAVED);
		if (!direct)
			snd_mask_set(&mask, SNDRV_PCM_ACCESS_RW_NONINTERLEAVED);
	}
	err = snd_pcm_hw_param_mask(substream, sparams, SNDRV_PCM_HW_PARAM_ACCESS, &mask);
	if (err < 0) {
		snd_printd("No usable accesses\n");
		err = -EINVAL;
		goto failure;
	}
	choose_rate(substream, sparams, runtime->oss.rate);
	snd_pcm_hw_param_near(substream, sparams, SNDRV_PCM_HW_PARAM_CHANNELS, runtime->oss.channels, NULL);

	format = snd_pcm_oss_format_from(runtime->oss.format);

	sformat_mask = *hw_param_mask(sparams, SNDRV_PCM_HW_PARAM_FORMAT);
	if (direct)
		sformat = format;
	else
		sformat = snd_pcm_plug_slave_format(format, &sformat_mask);

	if (sformat < 0 || !snd_mask_test(&sformat_mask, sformat)) {
		for (sformat = 0; sformat <= SNDRV_PCM_FORMAT_LAST; sformat++) {
			if (snd_mask_test(&sformat_mask, sformat) &&
			    snd_pcm_oss_format_to(sformat) >= 0)
				break;
		}
		if (sformat > SNDRV_PCM_FORMAT_LAST) {
			snd_printd("Cannot find a format!!!\n");
			err = -EINVAL;
			goto failure;
		}
	}
	err = _snd_pcm_hw_param_set(sparams, SNDRV_PCM_HW_PARAM_FORMAT, sformat, 0);
	snd_assert(err >= 0, goto failure);

	if (direct) {
		memcpy(params, sparams, sizeof(*params));
	} else {
		_snd_pcm_hw_params_any(params);
		_snd_pcm_hw_param_set(params, SNDRV_PCM_HW_PARAM_ACCESS,
				      SNDRV_PCM_ACCESS_RW_INTERLEAVED, 0);
		_snd_pcm_hw_param_set(params, SNDRV_PCM_HW_PARAM_FORMAT,
				      snd_pcm_oss_format_from(runtime->oss.format), 0);
		_snd_pcm_hw_param_set(params, SNDRV_PCM_HW_PARAM_CHANNELS,
				      runtime->oss.channels, 0);
		_snd_pcm_hw_param_set(params, SNDRV_PCM_HW_PARAM_RATE,
				      runtime->oss.rate, 0);
		pdprintf("client: access = %i, format = %i, channels = %i, rate = %i\n",
			 params_access(params), params_format(params),
			 params_channels(params), params_rate(params));
	}
	pdprintf("slave: access = %i, format = %i, channels = %i, rate = %i\n",
		 params_access(sparams), params_format(sparams),
		 params_channels(sparams), params_rate(sparams));

	oss_frame_size = snd_pcm_format_physical_width(params_format(params)) *
			 params_channels(params) / 8;

	snd_pcm_oss_plugin_clear(substream);
	if (!direct) {
		/* add necessary plugins */
		snd_pcm_oss_plugin_clear(substream);
		if ((err = snd_pcm_plug_format_plugins(substream,
						       params, 
						       sparams)) < 0) {
			snd_printd("snd_pcm_plug_format_plugins failed: %i\n", err);
			snd_pcm_oss_plugin_clear(substream);
			goto failure;
		}
		if (runtime->oss.plugin_first) {
			struct snd_pcm_plugin *plugin;
			if ((err = snd_pcm_plugin_build_io(substream, sparams, &plugin)) < 0) {
				snd_printd("snd_pcm_plugin_build_io failed: %i\n", err);
				snd_pcm_oss_plugin_clear(substream);
				goto failure;
			}
			if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
				err = snd_pcm_plugin_append(plugin);
			} else {
				err = snd_pcm_plugin_insert(plugin);
			}
			if (err < 0) {
				snd_pcm_oss_plugin_clear(substream);
				goto failure;
			}
		}
	}

	err = snd_pcm_oss_period_size(substream, params, sparams);
	if (err < 0)
		goto failure;

	n = snd_pcm_plug_slave_size(substream, runtime->oss.period_bytes / oss_frame_size);
	err = snd_pcm_hw_param_near(substream, sparams, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, n, NULL);
	snd_assert(err >= 0, goto failure);

	err = snd_pcm_hw_param_near(substream, sparams, SNDRV_PCM_HW_PARAM_PERIODS,
				     runtime->oss.periods, NULL);
	snd_assert(err >= 0, goto failure);

	snd_pcm_kernel_ioctl(substream, SNDRV_PCM_IOCTL_DROP, NULL);

	if ((err = snd_pcm_kernel_ioctl(substream, SNDRV_PCM_IOCTL_HW_PARAMS, sparams)) < 0) {
		snd_printd("HW_PARAMS failed: %i\n", err);
		goto failure;
	}

	memset(sw_params, 0, sizeof(*sw_params));
	if (runtime->oss.trigger) {
		sw_params->start_threshold = 1;
	} else {
		sw_params->start_threshold = runtime->boundary;
	}
	if (atomic_read(&runtime->mmap_count) || substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		sw_params->stop_threshold = runtime->boundary;
	else
		sw_params->stop_threshold = runtime->buffer_size;
	sw_params->tstamp_mode = SNDRV_PCM_TSTAMP_NONE;
	sw_params->period_step = 1;
	sw_params->sleep_min = 0;
	sw_params->avail_min = substream->stream == SNDRV_PCM_STREAM_PLAYBACK ?
		1 : runtime->period_size;
	sw_params->xfer_align = 1;
	if (atomic_read(&runtime->mmap_count) ||
	    (substream->oss.setup && substream->oss.setup->nosilence)) {
		sw_params->silence_threshold = 0;
		sw_params->silence_size = 0;
	} else {
		snd_pcm_uframes_t frames;
		frames = runtime->period_size + 16;
		if (frames > runtime->buffer_size)
			frames = runtime->buffer_size;
		sw_params->silence_threshold = frames;
		sw_params->silence_size = frames;
	}

	if ((err = snd_pcm_kernel_ioctl(substream, SNDRV_PCM_IOCTL_SW_PARAMS, sw_params)) < 0) {
		snd_printd("SW_PARAMS failed: %i\n", err);
		goto failure;
	}

	runtime->oss.periods = params_periods(sparams);
	oss_period_size = snd_pcm_plug_client_size(substream, params_period_size(sparams));
	snd_assert(oss_period_size >= 0, err = -EINVAL; goto failure);
	if (runtime->oss.plugin_first) {
		err = snd_pcm_plug_alloc(substream, oss_period_size);
		if (err < 0)
			goto failure;
	}
	oss_period_size *= oss_frame_size;

	oss_buffer_size = oss_period_size * runtime->oss.periods;
	snd_assert(oss_buffer_size >= 0, err = -EINVAL; goto failure);

	runtime->oss.period_bytes = oss_period_size;
	runtime->oss.buffer_bytes = oss_buffer_size;

	pdprintf("oss: period bytes = %i, buffer bytes = %i\n",
		 runtime->oss.period_bytes,
		 runtime->oss.buffer_bytes);
	pdprintf("slave: period_size = %i, buffer_size = %i\n",
		 params_period_size(sparams),
		 params_buffer_size(sparams));

	runtime->oss.format = snd_pcm_oss_format_to(params_format(params));
	runtime->oss.channels = params_channels(params);
	runtime->oss.rate = params_rate(params);

	runtime->oss.params = 0;
	runtime->oss.prepare = 1;
	vfree(runtime->oss.buffer);
	runtime->oss.buffer = vmalloc(runtime->oss.period_bytes);
	runtime->oss.buffer_used = 0;
	if (runtime->dma_area)
		snd_pcm_format_set_silence(runtime->format, runtime->dma_area, bytes_to_samples(runtime, runtime->dma_bytes));

	runtime->oss.period_frames = snd_pcm_alsa_frames(substream, oss_period_size);

	err = 0;
failure:
	kfree(sw_params);
	kfree(params);
	kfree(sparams);
	return err;
}

static int snd_pcm_oss_get_active_substream(struct snd_pcm_oss_file *pcm_oss_file, struct snd_pcm_substream **r_substream)
{
	int idx, err;
	struct snd_pcm_substream *asubstream = NULL, *substream;

	for (idx = 0; idx < 2; idx++) {
		substream = pcm_oss_file->streams[idx];
		if (substream == NULL)
			continue;
		if (asubstream == NULL)
			asubstream = substream;
		if (substream->runtime->oss.params) {
			err = snd_pcm_oss_change_params(substream);
			if (err < 0)
				return err;
		}
	}
	snd_assert(asubstream != NULL, return -EIO);
	if (r_substream)
		*r_substream = asubstream;
	return 0;
}

static int snd_pcm_oss_prepare(struct snd_pcm_substream *substream)
{
	int err;
	struct snd_pcm_runtime *runtime = substream->runtime;

	err = snd_pcm_kernel_ioctl(substream, SNDRV_PCM_IOCTL_PREPARE, NULL);
	if (err < 0) {
		snd_printd("snd_pcm_oss_prepare: SNDRV_PCM_IOCTL_PREPARE failed\n");
		return err;
	}
	runtime->oss.prepare = 0;
	runtime->oss.prev_hw_ptr_interrupt = 0;
	runtime->oss.period_ptr = 0;
	runtime->oss.buffer_used = 0;

	return 0;
}

static int snd_pcm_oss_make_ready(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime;
	int err;

	if (substream == NULL)
		return 0;
	runtime = substream->runtime;
	if (runtime->oss.params) {
		err = snd_pcm_oss_change_params(substream);
		if (err < 0)
			return err;
	}
	if (runtime->oss.prepare) {
		err = snd_pcm_oss_prepare(substream);
		if (err < 0)
			return err;
	}
	return 0;
}

static int snd_pcm_oss_capture_position_fixup(struct snd_pcm_substream *substream, snd_pcm_sframes_t *delay)
{
	struct snd_pcm_runtime *runtime;
	snd_pcm_uframes_t frames;
	int err = 0;

	while (1) {
		err = snd_pcm_kernel_ioctl(substream, SNDRV_PCM_IOCTL_DELAY, delay);
		if (err < 0)
			break;
		runtime = substream->runtime;
		if (*delay <= (snd_pcm_sframes_t)runtime->buffer_size)
			break;
		/* in case of overrun, skip whole periods like OSS/Linux driver does */
		/* until avail(delay) <= buffer_size */
		frames = (*delay - runtime->buffer_size) + runtime->period_size - 1;
		frames /= runtime->period_size;
		frames *= runtime->period_size;
		err = snd_pcm_kernel_ioctl(substream, SNDRV_PCM_IOCTL_FORWARD, &frames);
		if (err < 0)
			break;
	}
	return err;
}

snd_pcm_sframes_t snd_pcm_oss_write3(struct snd_pcm_substream *substream, const char *ptr, snd_pcm_uframes_t frames, int in_kernel)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	int ret;
	while (1) {
		if (runtime->status->state == SNDRV_PCM_STATE_XRUN ||
		    runtime->status->state == SNDRV_PCM_STATE_SUSPENDED) {
#ifdef OSS_DEBUG
			if (runtime->status->state == SNDRV_PCM_STATE_XRUN)
				printk("pcm_oss: write: recovering from XRUN\n");
			else
				printk("pcm_oss: write: recovering from SUSPEND\n");
#endif
			ret = snd_pcm_oss_prepare(substream);
			if (ret < 0)
				break;
		}
		if (in_kernel) {
			mm_segment_t fs;
			fs = snd_enter_user();
			ret = snd_pcm_lib_write(substream, (void __user *)ptr, frames);
			snd_leave_user(fs);
		} else {
			ret = snd_pcm_lib_write(substream, (void __user *)ptr, frames);
		}
		if (ret != -EPIPE && ret != -ESTRPIPE)
			break;
		/* test, if we can't store new data, because the stream */
		/* has not been started */
		if (runtime->status->state == SNDRV_PCM_STATE_PREPARED)
			return -EAGAIN;
	}
	return ret;
}

snd_pcm_sframes_t snd_pcm_oss_read3(struct snd_pcm_substream *substream, char *ptr, snd_pcm_uframes_t frames, int in_kernel)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	snd_pcm_sframes_t delay;
	int ret;
	while (1) {
		if (runtime->status->state == SNDRV_PCM_STATE_XRUN ||
		    runtime->status->state == SNDRV_PCM_STATE_SUSPENDED) {
#ifdef OSS_DEBUG
			if (runtime->status->state == SNDRV_PCM_STATE_XRUN)
				printk("pcm_oss: read: recovering from XRUN\n");
			else
				printk("pcm_oss: read: recovering from SUSPEND\n");
#endif
			ret = snd_pcm_kernel_ioctl(substream, SNDRV_PCM_IOCTL_DRAIN, NULL);
			if (ret < 0)
				break;
		} else if (runtime->status->state == SNDRV_PCM_STATE_SETUP) {
			ret = snd_pcm_oss_prepare(substream);
			if (ret < 0)
				break;
		}
		ret = snd_pcm_oss_capture_position_fixup(substream, &delay);
		if (ret < 0)
			break;
		if (in_kernel) {
			mm_segment_t fs;
			fs = snd_enter_user();
			ret = snd_pcm_lib_read(substream, (void __user *)ptr, frames);
			snd_leave_user(fs);
		} else {
			ret = snd_pcm_lib_read(substream, (void __user *)ptr, frames);
		}
		if (ret == -EPIPE) {
			if (runtime->status->state == SNDRV_PCM_STATE_DRAINING) {
				ret = snd_pcm_kernel_ioctl(substream, SNDRV_PCM_IOCTL_DROP, NULL);
				if (ret < 0)
					break;
			}
			continue;
		}
		if (ret != -ESTRPIPE)
			break;
	}
	return ret;
}

snd_pcm_sframes_t snd_pcm_oss_writev3(struct snd_pcm_substream *substream, void **bufs, snd_pcm_uframes_t frames, int in_kernel)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	int ret;
	while (1) {
		if (runtime->status->state == SNDRV_PCM_STATE_XRUN ||
		    runtime->status->state == SNDRV_PCM_STATE_SUSPENDED) {
#ifdef OSS_DEBUG
			if (runtime->status->state == SNDRV_PCM_STATE_XRUN)
				printk("pcm_oss: writev: recovering from XRUN\n");
			else
				printk("pcm_oss: writev: recovering from SUSPEND\n");
#endif
			ret = snd_pcm_oss_prepare(substream);
			if (ret < 0)
				break;
		}
		if (in_kernel) {
			mm_segment_t fs;
			fs = snd_enter_user();
			ret = snd_pcm_lib_writev(substream, (void __user **)bufs, frames);
			snd_leave_user(fs);
		} else {
			ret = snd_pcm_lib_writev(substream, (void __user **)bufs, frames);
		}
		if (ret != -EPIPE && ret != -ESTRPIPE)
			break;

		/* test, if we can't store new data, because the stream */
		/* has not been started */
		if (runtime->status->state == SNDRV_PCM_STATE_PREPARED)
			return -EAGAIN;
	}
	return ret;
}
	
snd_pcm_sframes_t snd_pcm_oss_readv3(struct snd_pcm_substream *substream, void **bufs, snd_pcm_uframes_t frames, int in_kernel)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	int ret;
	while (1) {
		if (runtime->status->state == SNDRV_PCM_STATE_XRUN ||
		    runtime->status->state == SNDRV_PCM_STATE_SUSPENDED) {
#ifdef OSS_DEBUG
			if (runtime->status->state == SNDRV_PCM_STATE_XRUN)
				printk("pcm_oss: readv: recovering from XRUN\n");
			else
				printk("pcm_oss: readv: recovering from SUSPEND\n");
#endif
			ret = snd_pcm_kernel_ioctl(substream, SNDRV_PCM_IOCTL_DRAIN, NULL);
			if (ret < 0)
				break;
		} else if (runtime->status->state == SNDRV_PCM_STATE_SETUP) {
			ret = snd_pcm_oss_prepare(substream);
			if (ret < 0)
				break;
		}
		if (in_kernel) {
			mm_segment_t fs;
			fs = snd_enter_user();
			ret = snd_pcm_lib_readv(substream, (void __user **)bufs, frames);
			snd_leave_user(fs);
		} else {
			ret = snd_pcm_lib_readv(substream, (void __user **)bufs, frames);
		}
		if (ret != -EPIPE && ret != -ESTRPIPE)
			break;
	}
	return ret;
}

static ssize_t snd_pcm_oss_write2(struct snd_pcm_substream *substream, const char *buf, size_t bytes, int in_kernel)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	snd_pcm_sframes_t frames, frames1;
	if (runtime->oss.plugin_first) {
		struct snd_pcm_plugin_channel *channels;
		size_t oss_frame_bytes = (runtime->oss.plugin_first->src_width * runtime->oss.plugin_first->src_format.channels) / 8;
		if (!in_kernel) {
			if (copy_from_user(runtime->oss.buffer, (const char __user *)buf, bytes))
				return -EFAULT;
			buf = runtime->oss.buffer;
		}
		frames = bytes / oss_frame_bytes;
		frames1 = snd_pcm_plug_client_channels_buf(substream, (char *)buf, frames, &channels);
		if (frames1 < 0)
			return frames1;
		frames1 = snd_pcm_plug_write_transfer(substream, channels, frames1);
		if (frames1 <= 0)
			return frames1;
		bytes = frames1 * oss_frame_bytes;
	} else {
		frames = bytes_to_frames(runtime, bytes);
		frames1 = snd_pcm_oss_write3(substream, buf, frames, in_kernel);
		if (frames1 <= 0)
			return frames1;
		bytes = frames_to_bytes(runtime, frames1);
	}
	return bytes;
}

static ssize_t snd_pcm_oss_write1(struct snd_pcm_substream *substream, const char __user *buf, size_t bytes)
{
	size_t xfer = 0;
	ssize_t tmp;
	struct snd_pcm_runtime *runtime = substream->runtime;

	if (atomic_read(&runtime->mmap_count))
		return -ENXIO;

	if ((tmp = snd_pcm_oss_make_ready(substream)) < 0)
		return tmp;
	while (bytes > 0) {
		if (bytes < runtime->oss.period_bytes || runtime->oss.buffer_used > 0) {
			tmp = bytes;
			if (tmp + runtime->oss.buffer_used > runtime->oss.period_bytes)
				tmp = runtime->oss.period_bytes - runtime->oss.buffer_used;
			if (tmp > 0) {
				if (copy_from_user(runtime->oss.buffer + runtime->oss.buffer_used, buf, tmp))
					return xfer > 0 ? (snd_pcm_sframes_t)xfer : -EFAULT;
			}
			runtime->oss.buffer_used += tmp;
			buf += tmp;
			bytes -= tmp;
			xfer += tmp;
			if ((substream->oss.setup != NULL && substream->oss.setup->partialfrag) ||
			    runtime->oss.buffer_used == runtime->oss.period_bytes) {
				tmp = snd_pcm_oss_write2(substream, runtime->oss.buffer + runtime->oss.period_ptr, 
							 runtime->oss.buffer_used - runtime->oss.period_ptr, 1);
				if (tmp <= 0)
					return xfer > 0 ? (snd_pcm_sframes_t)xfer : tmp;
				runtime->oss.bytes += tmp;
				runtime->oss.period_ptr += tmp;
				runtime->oss.period_ptr %= runtime->oss.period_bytes;
				if (runtime->oss.period_ptr == 0 ||
				    runtime->oss.period_ptr == runtime->oss.buffer_used)
					runtime->oss.buffer_used = 0;
				else if ((substream->ffile->f_flags & O_NONBLOCK) != 0)
					return xfer > 0 ? xfer : -EAGAIN;
			}
		} else {
			tmp = snd_pcm_oss_write2(substream,
						 (const char __force *)buf,
						 runtime->oss.period_bytes, 0);
			if (tmp <= 0)
				return xfer > 0 ? (snd_pcm_sframes_t)xfer : tmp;
			runtime->oss.bytes += tmp;
			buf += tmp;
			bytes -= tmp;
			xfer += tmp;
			if ((substream->ffile->f_flags & O_NONBLOCK) != 0 &&
			    tmp != runtime->oss.period_bytes)
				break;
		}
	}
	return xfer;
}

static ssize_t snd_pcm_oss_read2(struct snd_pcm_substream *substream, char *buf, size_t bytes, int in_kernel)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	snd_pcm_sframes_t frames, frames1;
	char __user *final_dst = (char __user *)buf;
	if (runtime->oss.plugin_first) {
		struct snd_pcm_plugin_channel *channels;
		size_t oss_frame_bytes = (runtime->oss.plugin_last->dst_width * runtime->oss.plugin_last->dst_format.channels) / 8;
		if (!in_kernel)
			buf = runtime->oss.buffer;
		frames = bytes / oss_frame_bytes;
		frames1 = snd_pcm_plug_client_channels_buf(substream, buf, frames, &channels);
		if (frames1 < 0)
			return frames1;
		frames1 = snd_pcm_plug_read_transfer(substream, channels, frames1);
		if (frames1 <= 0)
			return frames1;
		bytes = frames1 * oss_frame_bytes;
		if (!in_kernel && copy_to_user(final_dst, buf, bytes))
			return -EFAULT;
	} else {
		frames = bytes_to_frames(runtime, bytes);
		frames1 = snd_pcm_oss_read3(substream, buf, frames, in_kernel);
		if (frames1 <= 0)
			return frames1;
		bytes = frames_to_bytes(runtime, frames1);
	}
	return bytes;
}

static ssize_t snd_pcm_oss_read1(struct snd_pcm_substream *substream, char __user *buf, size_t bytes)
{
	size_t xfer = 0;
	ssize_t tmp;
	struct snd_pcm_runtime *runtime = substream->runtime;

	if (atomic_read(&runtime->mmap_count))
		return -ENXIO;

	if ((tmp = snd_pcm_oss_make_ready(substream)) < 0)
		return tmp;
	while (bytes > 0) {
		if (bytes < runtime->oss.period_bytes || runtime->oss.buffer_used > 0) {
			if (runtime->oss.buffer_used == 0) {
				tmp = snd_pcm_oss_read2(substream, runtime->oss.buffer, runtime->oss.period_bytes, 1);
				if (tmp <= 0)
					return xfer > 0 ? (snd_pcm_sframes_t)xfer : tmp;
				runtime->oss.bytes += tmp;
				runtime->oss.period_ptr = tmp;
				runtime->oss.buffer_used = tmp;
			}
			tmp = bytes;
			if ((size_t) tmp > runtime->oss.buffer_used)
				tmp = runtime->oss.buffer_used;
			if (copy_to_user(buf, runtime->oss.buffer + (runtime->oss.period_ptr - runtime->oss.buffer_used), tmp))
				return xfer > 0 ? (snd_pcm_sframes_t)xfer : -EFAULT;
			buf += tmp;
			bytes -= tmp;
			xfer += tmp;
			runtime->oss.buffer_used -= tmp;
		} else {
			tmp = snd_pcm_oss_read2(substream, (char __force *)buf,
						runtime->oss.period_bytes, 0);
			if (tmp <= 0)
				return xfer > 0 ? (snd_pcm_sframes_t)xfer : tmp;
			runtime->oss.bytes += tmp;
			buf += tmp;
			bytes -= tmp;
			xfer += tmp;
		}
	}
	return xfer;
}

static int snd_pcm_oss_reset(struct snd_pcm_oss_file *pcm_oss_file)
{
	struct snd_pcm_substream *substream;

	substream = pcm_oss_file->streams[SNDRV_PCM_STREAM_PLAYBACK];
	if (substream != NULL) {
		snd_pcm_kernel_playback_ioctl(substream, SNDRV_PCM_IOCTL_DROP, NULL);
		substream->runtime->oss.prepare = 1;
	}
	substream = pcm_oss_file->streams[SNDRV_PCM_STREAM_CAPTURE];
	if (substream != NULL) {
		snd_pcm_kernel_capture_ioctl(substream, SNDRV_PCM_IOCTL_DROP, NULL);
		substream->runtime->oss.prepare = 1;
	}
	return 0;
}

static int snd_pcm_oss_post(struct snd_pcm_oss_file *pcm_oss_file)
{
	struct snd_pcm_substream *substream;
	int err;

	substream = pcm_oss_file->streams[SNDRV_PCM_STREAM_PLAYBACK];
	if (substream != NULL) {
		if ((err = snd_pcm_oss_make_ready(substream)) < 0)
			return err;
		snd_pcm_kernel_playback_ioctl(substream, SNDRV_PCM_IOCTL_START, NULL);
	}
	/* note: all errors from the start action are ignored */
	/* OSS apps do not know, how to handle them */
	return 0;
}

static int snd_pcm_oss_sync1(struct snd_pcm_substream *substream, size_t size)
{
	struct snd_pcm_runtime *runtime;
	ssize_t result = 0;
	long res;
	wait_queue_t wait;

	runtime = substream->runtime;
	init_waitqueue_entry(&wait, current);
	add_wait_queue(&runtime->sleep, &wait);
#ifdef OSS_DEBUG
	printk("sync1: size = %li\n", size);
#endif
	while (1) {
		result = snd_pcm_oss_write2(substream, runtime->oss.buffer, size, 1);
		if (result > 0) {
			runtime->oss.buffer_used = 0;
			result = 0;
			break;
		}
		if (result != 0 && result != -EAGAIN)
			break;
		result = 0;
		set_current_state(TASK_INTERRUPTIBLE);
		snd_pcm_stream_lock_irq(substream);
		res = runtime->status->state;
		snd_pcm_stream_unlock_irq(substream);
		if (res != SNDRV_PCM_STATE_RUNNING) {
			set_current_state(TASK_RUNNING);
			break;
		}
		res = schedule_timeout(10 * HZ);
		if (signal_pending(current)) {
			result = -ERESTARTSYS;
			break;
		}
		if (res == 0) {
			snd_printk(KERN_ERR "OSS sync error - DMA timeout\n");
			result = -EIO;
			break;
		}
	}
	remove_wait_queue(&runtime->sleep, &wait);
	return result;
}

static int snd_pcm_oss_sync(struct snd_pcm_oss_file *pcm_oss_file)
{
	int err = 0;
	unsigned int saved_f_flags;
	struct snd_pcm_substream *substream;
	struct snd_pcm_runtime *runtime;
	snd_pcm_format_t format;
	unsigned long width;
	size_t size;

	substream = pcm_oss_file->streams[SNDRV_PCM_STREAM_PLAYBACK];
	if (substream != NULL) {
		runtime = substream->runtime;
		if (atomic_read(&runtime->mmap_count))
			goto __direct;
		if ((err = snd_pcm_oss_make_ready(substream)) < 0)
			return err;
		format = snd_pcm_oss_format_from(runtime->oss.format);
		width = snd_pcm_format_physical_width(format);
		if (runtime->oss.buffer_used > 0) {
#ifdef OSS_DEBUG
			printk("sync: buffer_used\n");
#endif
			size = (8 * (runtime->oss.period_bytes - runtime->oss.buffer_used) + 7) / width;
			snd_pcm_format_set_silence(format,
						   runtime->oss.buffer + runtime->oss.buffer_used,
						   size);
			err = snd_pcm_oss_sync1(substream, runtime->oss.period_bytes);
			if (err < 0)
				return err;
		} else if (runtime->oss.period_ptr > 0) {
#ifdef OSS_DEBUG
			printk("sync: period_ptr\n");
#endif
			size = runtime->oss.period_bytes - runtime->oss.period_ptr;
			snd_pcm_format_set_silence(format,
						   runtime->oss.buffer,
						   size * 8 / width);
			err = snd_pcm_oss_sync1(substream, size);
			if (err < 0)
				return err;
		}
		/*
		 * The ALSA's period might be a bit large than OSS one.
		 * Fill the remain portion of ALSA period with zeros.
		 */
		size = runtime->control->appl_ptr % runtime->period_size;
		if (size > 0) {
			size = runtime->period_size - size;
			if (runtime->access == SNDRV_PCM_ACCESS_RW_INTERLEAVED) {
				size = (runtime->frame_bits * size) / 8;
				while (size > 0) {
					mm_segment_t fs;
					size_t size1 = size < runtime->oss.period_bytes ? size : runtime->oss.period_bytes;
					size -= size1;
					size1 *= 8;
					size1 /= runtime->sample_bits;
					snd_pcm_format_set_silence(runtime->format,
								   runtime->oss.buffer,
								   size1);
					fs = snd_enter_user();
					snd_pcm_lib_write(substream, (void __user *)runtime->oss.buffer, size1);
					snd_leave_user(fs);
				}
			} else if (runtime->access == SNDRV_PCM_ACCESS_RW_NONINTERLEAVED) {
				void __user *buffers[runtime->channels];
				memset(buffers, 0, runtime->channels * sizeof(void *));
				snd_pcm_lib_writev(substream, buffers, size);
			}
		}
		/*
		 * finish sync: drain the buffer
		 */
	      __direct:
		saved_f_flags = substream->ffile->f_flags;
		substream->ffile->f_flags &= ~O_NONBLOCK;
		err = snd_pcm_kernel_playback_ioctl(substream, SNDRV_PCM_IOCTL_DRAIN, NULL);
		substream->ffile->f_flags = saved_f_flags;
		if (err < 0)
			return err;
		runtime->oss.prepare = 1;
	}

	substream = pcm_oss_file->streams[SNDRV_PCM_STREAM_CAPTURE];
	if (substream != NULL) {
		if ((err = snd_pcm_oss_make_ready(substream)) < 0)
			return err;
		runtime = substream->runtime;
		err = snd_pcm_kernel_capture_ioctl(substream, SNDRV_PCM_IOCTL_DROP, NULL);
		if (err < 0)
			return err;
		runtime->oss.buffer_used = 0;
		runtime->oss.prepare = 1;
	}
	return 0;
}

static int snd_pcm_oss_set_rate(struct snd_pcm_oss_file *pcm_oss_file, int rate)
{
	int idx;

	for (idx = 1; idx >= 0; --idx) {
		struct snd_pcm_substream *substream = pcm_oss_file->streams[idx];
		struct snd_pcm_runtime *runtime;
		if (substream == NULL)
			continue;
		runtime = substream->runtime;
		if (rate < 1000)
			rate = 1000;
		else if (rate > 192000)
			rate = 192000;
		if (runtime->oss.rate != rate) {
			runtime->oss.params = 1;
			runtime->oss.rate = rate;
		}
	}
	return snd_pcm_oss_get_rate(pcm_oss_file);
}

static int snd_pcm_oss_get_rate(struct snd_pcm_oss_file *pcm_oss_file)
{
	struct snd_pcm_substream *substream;
	int err;
	
	if ((err = snd_pcm_oss_get_active_substream(pcm_oss_file, &substream)) < 0)
		return err;
	return substream->runtime->oss.rate;
}

static int snd_pcm_oss_set_channels(struct snd_pcm_oss_file *pcm_oss_file, unsigned int channels)
{
	int idx;
	if (channels < 1)
		channels = 1;
	if (channels > 128)
		return -EINVAL;
	for (idx = 1; idx >= 0; --idx) {
		struct snd_pcm_substream *substream = pcm_oss_file->streams[idx];
		struct snd_pcm_runtime *runtime;
		if (substream == NULL)
			continue;
		runtime = substream->runtime;
		if (runtime->oss.channels != channels) {
			runtime->oss.params = 1;
			runtime->oss.channels = channels;
		}
	}
	return snd_pcm_oss_get_channels(pcm_oss_file);
}

static int snd_pcm_oss_get_channels(struct snd_pcm_oss_file *pcm_oss_file)
{
	struct snd_pcm_substream *substream;
	int err;
	
	if ((err = snd_pcm_oss_get_active_substream(pcm_oss_file, &substream)) < 0)
		return err;
	return substream->runtime->oss.channels;
}

static int snd_pcm_oss_get_block_size(struct snd_pcm_oss_file *pcm_oss_file)
{
	struct snd_pcm_substream *substream;
	int err;
	
	if ((err = snd_pcm_oss_get_active_substream(pcm_oss_file, &substream)) < 0)
		return err;
	return substream->runtime->oss.period_bytes;
}

static int snd_pcm_oss_get_formats(struct snd_pcm_oss_file *pcm_oss_file)
{
	struct snd_pcm_substream *substream;
	int err;
	int direct;
	struct snd_pcm_hw_params *params;
	unsigned int formats = 0;
	struct snd_mask format_mask;
	int fmt;

	if ((err = snd_pcm_oss_get_active_substream(pcm_oss_file, &substream)) < 0)
		return err;
	if (atomic_read(&substream->runtime->mmap_count)) {
		direct = 1;
	} else {
		struct snd_pcm_oss_setup *setup = substream->oss.setup;
		direct = (setup != NULL && setup->direct);
	}
	if (!direct)
		return AFMT_MU_LAW | AFMT_U8 |
		       AFMT_S16_LE | AFMT_S16_BE |
		       AFMT_S8 | AFMT_U16_LE |
		       AFMT_U16_BE;
	params = kmalloc(sizeof(*params), GFP_KERNEL);
	if (!params)
		return -ENOMEM;
	_snd_pcm_hw_params_any(params);
	err = snd_pcm_hw_refine(substream, params);
	format_mask = *hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT); 
	kfree(params);
	snd_assert(err >= 0, return err);
	for (fmt = 0; fmt < 32; ++fmt) {
		if (snd_mask_test(&format_mask, fmt)) {
			int f = snd_pcm_oss_format_to(fmt);
			if (f >= 0)
				formats |= f;
		}
	}
	return formats;
}

static int snd_pcm_oss_set_format(struct snd_pcm_oss_file *pcm_oss_file, int format)
{
	int formats, idx;
	
	if (format != AFMT_QUERY) {
		formats = snd_pcm_oss_get_formats(pcm_oss_file);
		if (!(formats & format))
			format = AFMT_U8;
		for (idx = 1; idx >= 0; --idx) {
			struct snd_pcm_substream *substream = pcm_oss_file->streams[idx];
			struct snd_pcm_runtime *runtime;
			if (substream == NULL)
				continue;
			runtime = substream->runtime;
			if (runtime->oss.format != format) {
				runtime->oss.params = 1;
				runtime->oss.format = format;
			}
		}
	}
	return snd_pcm_oss_get_format(pcm_oss_file);
}

static int snd_pcm_oss_get_format(struct snd_pcm_oss_file *pcm_oss_file)
{
	struct snd_pcm_substream *substream;
	int err;
	
	if ((err = snd_pcm_oss_get_active_substream(pcm_oss_file, &substream)) < 0)
		return err;
	return substream->runtime->oss.format;
}

static int snd_pcm_oss_set_subdivide1(struct snd_pcm_substream *substream, int subdivide)
{
	struct snd_pcm_runtime *runtime;

	if (substream == NULL)
		return 0;
	runtime = substream->runtime;
	if (subdivide == 0) {
		subdivide = runtime->oss.subdivision;
		if (subdivide == 0)
			subdivide = 1;
		return subdivide;
	}
	if (runtime->oss.subdivision || runtime->oss.fragshift)
		return -EINVAL;
	if (subdivide != 1 && subdivide != 2 && subdivide != 4 &&
	    subdivide != 8 && subdivide != 16)
		return -EINVAL;
	runtime->oss.subdivision = subdivide;
	runtime->oss.params = 1;
	return subdivide;
}

static int snd_pcm_oss_set_subdivide(struct snd_pcm_oss_file *pcm_oss_file, int subdivide)
{
	int err = -EINVAL, idx;

	for (idx = 1; idx >= 0; --idx) {
		struct snd_pcm_substream *substream = pcm_oss_file->streams[idx];
		if (substream == NULL)
			continue;
		if ((err = snd_pcm_oss_set_subdivide1(substream, subdivide)) < 0)
			return err;
	}
	return err;
}

static int snd_pcm_oss_set_fragment1(struct snd_pcm_substream *substream, unsigned int val)
{
	struct snd_pcm_runtime *runtime;

	if (substream == NULL)
		return 0;
	runtime = substream->runtime;
	if (runtime->oss.subdivision || runtime->oss.fragshift)
		return -EINVAL;
	runtime->oss.fragshift = val & 0xffff;
	runtime->oss.maxfrags = (val >> 16) & 0xffff;
	if (runtime->oss.fragshift < 4)		/* < 16 */
		runtime->oss.fragshift = 4;
	if (runtime->oss.maxfrags < 2)
		runtime->oss.maxfrags = 2;
	runtime->oss.params = 1;
	return 0;
}

static int snd_pcm_oss_set_fragment(struct snd_pcm_oss_file *pcm_oss_file, unsigned int val)
{
	int err = -EINVAL, idx;

	for (idx = 1; idx >= 0; --idx) {
		struct snd_pcm_substream *substream = pcm_oss_file->streams[idx];
		if (substream == NULL)
			continue;
		if ((err = snd_pcm_oss_set_fragment1(substream, val)) < 0)
			return err;
	}
	return err;
}

static int snd_pcm_oss_nonblock(struct file * file)
{
	file->f_flags |= O_NONBLOCK;
	return 0;
}

static int snd_pcm_oss_get_caps1(struct snd_pcm_substream *substream, int res)
{

	if (substream == NULL) {
		res &= ~DSP_CAP_DUPLEX;
		return res;
	}
#ifdef DSP_CAP_MULTI
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		if (substream->pstr->substream_count > 1)
			res |= DSP_CAP_MULTI;
#endif
	/* DSP_CAP_REALTIME is set all times: */
	/* all ALSA drivers can return actual pointer in ring buffer */
#if defined(DSP_CAP_REALTIME) && 0
	{
		struct snd_pcm_runtime *runtime = substream->runtime;
		if (runtime->info & (SNDRV_PCM_INFO_BLOCK_TRANSFER|SNDRV_PCM_INFO_BATCH))
			res &= ~DSP_CAP_REALTIME;
	}
#endif
	return res;
}

static int snd_pcm_oss_get_caps(struct snd_pcm_oss_file *pcm_oss_file)
{
	int result, idx;
	
	result = DSP_CAP_TRIGGER | DSP_CAP_MMAP	| DSP_CAP_DUPLEX | DSP_CAP_REALTIME;
	for (idx = 0; idx < 2; idx++) {
		struct snd_pcm_substream *substream = pcm_oss_file->streams[idx];
		result = snd_pcm_oss_get_caps1(substream, result);
	}
	result |= 0x0001;	/* revision - same as SB AWE 64 */
	return result;
}

static void snd_pcm_oss_simulate_fill(struct snd_pcm_substream *substream, snd_pcm_uframes_t hw_ptr)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	snd_pcm_uframes_t appl_ptr;
	appl_ptr = hw_ptr + runtime->buffer_size;
	appl_ptr %= runtime->boundary;
	runtime->control->appl_ptr = appl_ptr;
}

static int snd_pcm_oss_set_trigger(struct snd_pcm_oss_file *pcm_oss_file, int trigger)
{
	struct snd_pcm_runtime *runtime;
	struct snd_pcm_substream *psubstream = NULL, *csubstream = NULL;
	int err, cmd;

#ifdef OSS_DEBUG
	printk("pcm_oss: trigger = 0x%x\n", trigger);
#endif
	
	psubstream = pcm_oss_file->streams[SNDRV_PCM_STREAM_PLAYBACK];
	csubstream = pcm_oss_file->streams[SNDRV_PCM_STREAM_CAPTURE];

	if (psubstream) {
		if ((err = snd_pcm_oss_make_ready(psubstream)) < 0)
			return err;
	}
	if (csubstream) {
		if ((err = snd_pcm_oss_make_ready(csubstream)) < 0)
			return err;
	}
      	if (psubstream) {
      		runtime = psubstream->runtime;
		if (trigger & PCM_ENABLE_OUTPUT) {
			if (runtime->oss.trigger)
				goto _skip1;
			if (atomic_read(&psubstream->runtime->mmap_count))
				snd_pcm_oss_simulate_fill(psubstream, runtime->hw_ptr_interrupt);
			runtime->oss.trigger = 1;
			runtime->start_threshold = 1;
			cmd = SNDRV_PCM_IOCTL_START;
		} else {
			if (!runtime->oss.trigger)
				goto _skip1;
			runtime->oss.trigger = 0;
			runtime->start_threshold = runtime->boundary;
			cmd = SNDRV_PCM_IOCTL_DROP;
			runtime->oss.prepare = 1;
		}
		err = snd_pcm_kernel_playback_ioctl(psubstream, cmd, NULL);
		if (err < 0)
			return err;
	}
 _skip1:
	if (csubstream) {
      		runtime = csubstream->runtime;
		if (trigger & PCM_ENABLE_INPUT) {
			if (runtime->oss.trigger)
				goto _skip2;
			runtime->oss.trigger = 1;
			runtime->start_threshold = 1;
			cmd = SNDRV_PCM_IOCTL_START;
		} else {
			if (!runtime->oss.trigger)
				goto _skip2;
			runtime->oss.trigger = 0;
			runtime->start_threshold = runtime->boundary;
			cmd = SNDRV_PCM_IOCTL_DROP;
			runtime->oss.prepare = 1;
		}
		err = snd_pcm_kernel_capture_ioctl(csubstream, cmd, NULL);
		if (err < 0)
			return err;
	}
 _skip2:
	return 0;
}

static int snd_pcm_oss_get_trigger(struct snd_pcm_oss_file *pcm_oss_file)
{
	struct snd_pcm_substream *psubstream = NULL, *csubstream = NULL;
	int result = 0;

	psubstream = pcm_oss_file->streams[SNDRV_PCM_STREAM_PLAYBACK];
	csubstream = pcm_oss_file->streams[SNDRV_PCM_STREAM_CAPTURE];
	if (psubstream && psubstream->runtime && psubstream->runtime->oss.trigger)
		result |= PCM_ENABLE_OUTPUT;
	if (csubstream && csubstream->runtime && csubstream->runtime->oss.trigger)
		result |= PCM_ENABLE_INPUT;
	return result;
}

static int snd_pcm_oss_get_odelay(struct snd_pcm_oss_file *pcm_oss_file)
{
	struct snd_pcm_substream *substream;
	struct snd_pcm_runtime *runtime;
	snd_pcm_sframes_t delay;
	int err;

	substream = pcm_oss_file->streams[SNDRV_PCM_STREAM_PLAYBACK];
	if (substream == NULL)
		return -EINVAL;
	if ((err = snd_pcm_oss_make_ready(substream)) < 0)
		return err;
	runtime = substream->runtime;
	if (runtime->oss.params || runtime->oss.prepare)
		return 0;
	err = snd_pcm_kernel_playback_ioctl(substream, SNDRV_PCM_IOCTL_DELAY, &delay);
	if (err == -EPIPE)
		delay = 0;	/* hack for broken OSS applications */
	else if (err < 0)
		return err;
	return snd_pcm_oss_bytes(substream, delay);
}

static int snd_pcm_oss_get_ptr(struct snd_pcm_oss_file *pcm_oss_file, int stream, struct count_info __user * _info)
{	
	struct snd_pcm_substream *substream;
	struct snd_pcm_runtime *runtime;
	snd_pcm_sframes_t delay;
	int fixup;
	struct count_info info;
	int err;

	if (_info == NULL)
		return -EFAULT;
	substream = pcm_oss_file->streams[stream];
	if (substream == NULL)
		return -EINVAL;
	if ((err = snd_pcm_oss_make_ready(substream)) < 0)
		return err;
	runtime = substream->runtime;
	if (runtime->oss.params || runtime->oss.prepare) {
		memset(&info, 0, sizeof(info));
		if (copy_to_user(_info, &info, sizeof(info)))
			return -EFAULT;
		return 0;
	}
	if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
		err = snd_pcm_kernel_ioctl(substream, SNDRV_PCM_IOCTL_DELAY, &delay);
		if (err == -EPIPE || err == -ESTRPIPE || (! err && delay < 0)) {
			err = 0;
			delay = 0;
			fixup = 0;
		} else {
			fixup = runtime->oss.buffer_used;
		}
	} else {
		err = snd_pcm_oss_capture_position_fixup(substream, &delay);
		fixup = -runtime->oss.buffer_used;
	}
	if (err < 0)
		return err;
	info.ptr = snd_pcm_oss_bytes(substream, runtime->status->hw_ptr % runtime->buffer_size);
	if (atomic_read(&runtime->mmap_count)) {
		snd_pcm_sframes_t n;
		n = (delay = runtime->hw_ptr_interrupt) - runtime->oss.prev_hw_ptr_interrupt;
		if (n < 0)
			n += runtime->boundary;
		info.blocks = n / runtime->period_size;
		runtime->oss.prev_hw_ptr_interrupt = delay;
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			snd_pcm_oss_simulate_fill(substream, delay);
		info.bytes = snd_pcm_oss_bytes(substream, runtime->status->hw_ptr) & INT_MAX;
	} else {
		delay = snd_pcm_oss_bytes(substream, delay);
		if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
			struct snd_pcm_oss_setup *setup = substream->oss.setup;
			if (setup && setup->buggyptr)
				info.blocks = (runtime->oss.buffer_bytes - delay - fixup) / runtime->oss.period_bytes;
			else
				info.blocks = (delay + fixup) / runtime->oss.period_bytes;
			info.bytes = (runtime->oss.bytes - delay) & INT_MAX;
		} else {
			delay += fixup;
			info.blocks = delay / runtime->oss.period_bytes;
			info.bytes = (runtime->oss.bytes + delay) & INT_MAX;
		}
	}
	if (copy_to_user(_info, &info, sizeof(info)))
		return -EFAULT;
	return 0;
}

static int snd_pcm_oss_get_space(struct snd_pcm_oss_file *pcm_oss_file, int stream, struct audio_buf_info __user *_info)
{
	struct snd_pcm_substream *substream;
	struct snd_pcm_runtime *runtime;
	snd_pcm_sframes_t avail;
	int fixup;
	struct audio_buf_info info;
	int err;

	if (_info == NULL)
		return -EFAULT;
	substream = pcm_oss_file->streams[stream];
	if (substream == NULL)
		return -EINVAL;
	runtime = substream->runtime;

	if (runtime->oss.params &&
	    (err = snd_pcm_oss_change_params(substream)) < 0)
		return err;

	info.fragsize = runtime->oss.period_bytes;
	info.fragstotal = runtime->periods;
	if (runtime->oss.prepare) {
		if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
			info.bytes = runtime->oss.period_bytes * runtime->oss.periods;
			info.fragments = runtime->oss.periods;
		} else {
			info.bytes = 0;
			info.fragments = 0;
		}
	} else {
		if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
			err = snd_pcm_kernel_ioctl(substream, SNDRV_PCM_IOCTL_DELAY, &avail);
			if (err == -EPIPE || err == -ESTRPIPE || (! err && avail < 0)) {
				avail = runtime->buffer_size;
				err = 0;
				fixup = 0;
			} else {
				avail = runtime->buffer_size - avail;
				fixup = -runtime->oss.buffer_used;
			}
		} else {
			err = snd_pcm_oss_capture_position_fixup(substream, &avail);
			fixup = runtime->oss.buffer_used;
		}
		if (err < 0)
			return err;
		info.bytes = snd_pcm_oss_bytes(substream, avail) + fixup;
		info.fragments = info.bytes / runtime->oss.period_bytes;
	}

#ifdef OSS_DEBUG
	printk("pcm_oss: space: bytes = %i, fragments = %i, fragstotal = %i, fragsize = %i\n", info.bytes, info.fragments, info.fragstotal, info.fragsize);
#endif
	if (copy_to_user(_info, &info, sizeof(info)))
		return -EFAULT;
	return 0;
}

static int snd_pcm_oss_get_mapbuf(struct snd_pcm_oss_file *pcm_oss_file, int stream, struct buffmem_desc __user * _info)
{
	// it won't be probably implemented
	// snd_printd("TODO: snd_pcm_oss_get_mapbuf\n");
	return -EINVAL;
}

static struct snd_pcm_oss_setup *snd_pcm_oss_look_for_setup(struct snd_pcm *pcm, int stream, const char *task_name)
{
	const char *ptr, *ptrl;
	struct snd_pcm_oss_setup *setup;

	down(&pcm->streams[stream].oss.setup_mutex);
	for (setup = pcm->streams[stream].oss.setup_list; setup; setup = setup->next) {
		if (!strcmp(setup->task_name, task_name)) {
			up(&pcm->streams[stream].oss.setup_mutex);
			return setup;
		}
	}
	ptr = ptrl = task_name;
	while (*ptr) {
		if (*ptr == '/')
			ptrl = ptr + 1;
		ptr++;
	}
	if (ptrl == task_name) {
		goto __not_found;
		return NULL;
	}
	for (setup = pcm->streams[stream].oss.setup_list; setup; setup = setup->next) {
		if (!strcmp(setup->task_name, ptrl)) {
			up(&pcm->streams[stream].oss.setup_mutex);
			return setup;
		}
	}
      __not_found:
	up(&pcm->streams[stream].oss.setup_mutex);
	return NULL;
}

static void snd_pcm_oss_init_substream(struct snd_pcm_substream *substream,
				       struct snd_pcm_oss_setup *setup,
				       int minor)
{
	struct snd_pcm_runtime *runtime;

	substream->oss.oss = 1;
	substream->oss.setup = setup;
	runtime = substream->runtime;
	runtime->oss.params = 1;
	runtime->oss.trigger = 1;
	runtime->oss.rate = 8000;
	switch (SNDRV_MINOR_OSS_DEVICE(minor)) {
	case SNDRV_MINOR_OSS_PCM_8:
		runtime->oss.format = AFMT_U8;
		break;
	case SNDRV_MINOR_OSS_PCM_16:
		runtime->oss.format = AFMT_S16_LE;
		break;
	default:
		runtime->oss.format = AFMT_MU_LAW;
	}
	runtime->oss.channels = 1;
	runtime->oss.fragshift = 0;
	runtime->oss.maxfrags = 0;
	runtime->oss.subdivision = 0;
}

static void snd_pcm_oss_release_substream(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime;
	runtime = substream->runtime;
	vfree(runtime->oss.buffer);
	snd_pcm_oss_plugin_clear(substream);
	substream->oss.file = NULL;
	substream->oss.oss = 0;
}

static int snd_pcm_oss_release_file(struct snd_pcm_oss_file *pcm_oss_file)
{
	int cidx;
	snd_assert(pcm_oss_file != NULL, return -ENXIO);
	for (cidx = 0; cidx < 2; ++cidx) {
		struct snd_pcm_substream *substream = pcm_oss_file->streams[cidx];
		struct snd_pcm_runtime *runtime;
		if (substream == NULL)
			continue;
		runtime = substream->runtime;
		
		snd_pcm_stream_lock_irq(substream);
		if (snd_pcm_running(substream))
			snd_pcm_stop(substream, SNDRV_PCM_STATE_SETUP);
		snd_pcm_stream_unlock_irq(substream);
		if (substream->ffile != NULL) {
			if (substream->ops->hw_free != NULL)
				substream->ops->hw_free(substream);
			substream->ops->close(substream);
			substream->ffile = NULL;
		}
		snd_pcm_oss_release_substream(substream);
		snd_pcm_release_substream(substream);
	}
	kfree(pcm_oss_file);
	return 0;
}

static int snd_pcm_oss_open_file(struct file *file,
				 struct snd_pcm *pcm,
				 struct snd_pcm_oss_file **rpcm_oss_file,
				 int minor,
				 struct snd_pcm_oss_setup *psetup,
				 struct snd_pcm_oss_setup *csetup)
{
	int err = 0;
	struct snd_pcm_oss_file *pcm_oss_file;
	struct snd_pcm_substream *psubstream = NULL, *csubstream = NULL;
	unsigned int f_mode = file->f_mode;

	snd_assert(rpcm_oss_file != NULL, return -EINVAL);
	*rpcm_oss_file = NULL;

	pcm_oss_file = kzalloc(sizeof(*pcm_oss_file), GFP_KERNEL);
	if (pcm_oss_file == NULL)
		return -ENOMEM;

	if ((f_mode & (FMODE_WRITE|FMODE_READ)) == (FMODE_WRITE|FMODE_READ) &&
	    (pcm->info_flags & SNDRV_PCM_INFO_HALF_DUPLEX))
		f_mode = FMODE_WRITE;
	if ((f_mode & FMODE_WRITE) && !(psetup && psetup->disable)) {
		if ((err = snd_pcm_open_substream(pcm, SNDRV_PCM_STREAM_PLAYBACK,
					       &psubstream)) < 0) {
			snd_pcm_oss_release_file(pcm_oss_file);
			return err;
		}
		pcm_oss_file->streams[SNDRV_PCM_STREAM_PLAYBACK] = psubstream;
	}
	if ((f_mode & FMODE_READ) && !(csetup && csetup->disable)) {
		if ((err = snd_pcm_open_substream(pcm, SNDRV_PCM_STREAM_CAPTURE, 
					       &csubstream)) < 0) {
			if (!(f_mode & FMODE_WRITE) || err != -ENODEV) {
				snd_pcm_oss_release_file(pcm_oss_file);
				return err;
			} else {
				csubstream = NULL;
			}
		}
		pcm_oss_file->streams[SNDRV_PCM_STREAM_CAPTURE] = csubstream;
	}
	
	if (psubstream == NULL && csubstream == NULL) {
		snd_pcm_oss_release_file(pcm_oss_file);
		return -EINVAL;
	}
	if (psubstream != NULL) {
		psubstream->oss.file = pcm_oss_file;
		err = snd_pcm_hw_constraints_init(psubstream);
		if (err < 0) {
			snd_printd("snd_pcm_hw_constraint_init failed\n");
			snd_pcm_oss_release_file(pcm_oss_file);
			return err;
		}
		if ((err = psubstream->ops->open(psubstream)) < 0) {
			snd_pcm_oss_release_file(pcm_oss_file);
			return err;
		}
		psubstream->ffile = file;
		err = snd_pcm_hw_constraints_complete(psubstream);
		if (err < 0) {
			snd_printd("snd_pcm_hw_constraint_complete failed\n");
			snd_pcm_oss_release_file(pcm_oss_file);
			return err;
		}
		snd_pcm_oss_init_substream(psubstream, psetup, minor);
	}
	if (csubstream != NULL) {
		csubstream->oss.file = pcm_oss_file;
		err = snd_pcm_hw_constraints_init(csubstream);
		if (err < 0) {
			snd_printd("snd_pcm_hw_constraint_init failed\n");
			snd_pcm_oss_release_file(pcm_oss_file);
			return err;
		}
		if ((err = csubstream->ops->open(csubstream)) < 0) {
			snd_pcm_oss_release_file(pcm_oss_file);
			return err;
		}
		csubstream->ffile = file;
		err = snd_pcm_hw_constraints_complete(csubstream);
		if (err < 0) {
			snd_printd("snd_pcm_hw_constraint_complete failed\n");
			snd_pcm_oss_release_file(pcm_oss_file);
			return err;
		}
		snd_pcm_oss_init_substream(csubstream, csetup, minor);
	}

	file->private_data = pcm_oss_file;
	*rpcm_oss_file = pcm_oss_file;
	return 0;
}


static int snd_task_name(struct task_struct *task, char *name, size_t size)
{
	unsigned int idx;

	snd_assert(task != NULL && name != NULL && size >= 2, return -EINVAL);
	for (idx = 0; idx < sizeof(task->comm) && idx + 1 < size; idx++)
		name[idx] = task->comm[idx];
	name[idx] = '\0';
	return 0;
}

static int snd_pcm_oss_open(struct inode *inode, struct file *file)
{
	int err;
	char task_name[32];
	struct snd_pcm *pcm;
	struct snd_pcm_oss_file *pcm_oss_file;
	struct snd_pcm_oss_setup *psetup = NULL, *csetup = NULL;
	int nonblock;
	wait_queue_t wait;

	pcm = snd_lookup_oss_minor_data(iminor(inode),
					SNDRV_OSS_DEVICE_TYPE_PCM);
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
	if (snd_task_name(current, task_name, sizeof(task_name)) < 0) {
		err = -EFAULT;
		goto __error;
	}
	if (file->f_mode & FMODE_WRITE)
		psetup = snd_pcm_oss_look_for_setup(pcm, SNDRV_PCM_STREAM_PLAYBACK, task_name);
	if (file->f_mode & FMODE_READ)
		csetup = snd_pcm_oss_look_for_setup(pcm, SNDRV_PCM_STREAM_CAPTURE, task_name);

	nonblock = !!(file->f_flags & O_NONBLOCK);
	if (psetup && !psetup->disable) {
		if (psetup->nonblock)
			nonblock = 1;
		else if (psetup->block)
			nonblock = 0;
	} else if (csetup && !csetup->disable) {
		if (csetup->nonblock)
			nonblock = 1;
		else if (csetup->block)
			nonblock = 0;
	}
	if (!nonblock)
		nonblock = nonblock_open;

	init_waitqueue_entry(&wait, current);
	add_wait_queue(&pcm->open_wait, &wait);
	down(&pcm->open_mutex);
	while (1) {
		err = snd_pcm_oss_open_file(file, pcm, &pcm_oss_file,
					    iminor(inode), psetup, csetup);
		if (err >= 0)
			break;
		if (err == -EAGAIN) {
			if (nonblock) {
				err = -EBUSY;
				break;
			}
		} else
			break;
		set_current_state(TASK_INTERRUPTIBLE);
		up(&pcm->open_mutex);
		schedule();
		down(&pcm->open_mutex);
		if (signal_pending(current)) {
			err = -ERESTARTSYS;
			break;
		}
	}
	remove_wait_queue(&pcm->open_wait, &wait);
	up(&pcm->open_mutex);
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

static int snd_pcm_oss_release(struct inode *inode, struct file *file)
{
	struct snd_pcm *pcm;
	struct snd_pcm_substream *substream;
	struct snd_pcm_oss_file *pcm_oss_file;

	pcm_oss_file = file->private_data;
	substream = pcm_oss_file->streams[SNDRV_PCM_STREAM_PLAYBACK];
	if (substream == NULL)
		substream = pcm_oss_file->streams[SNDRV_PCM_STREAM_CAPTURE];
	snd_assert(substream != NULL, return -ENXIO);
	pcm = substream->pcm;
	snd_pcm_oss_sync(pcm_oss_file);
	down(&pcm->open_mutex);
	snd_pcm_oss_release_file(pcm_oss_file);
	up(&pcm->open_mutex);
	wake_up(&pcm->open_wait);
	module_put(pcm->card->module);
	snd_card_file_remove(pcm->card, file);
	return 0;
}

static long snd_pcm_oss_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct snd_pcm_oss_file *pcm_oss_file;
	int __user *p = (int __user *)arg;
	int res;

	pcm_oss_file = file->private_data;
	if (cmd == OSS_GETVERSION)
		return put_user(SNDRV_OSS_VERSION, p);
	if (cmd == OSS_ALSAEMULVER)
		return put_user(1, p);
#if defined(CONFIG_SND_MIXER_OSS) || (defined(MODULE) && defined(CONFIG_SND_MIXER_OSS_MODULE))
	if (((cmd >> 8) & 0xff) == 'M')	{	/* mixer ioctl - for OSS compatibility */
		struct snd_pcm_substream *substream;
		int idx;
		for (idx = 0; idx < 2; ++idx) {
			substream = pcm_oss_file->streams[idx];
			if (substream != NULL)
				break;
		}
		snd_assert(substream != NULL, return -ENXIO);
		return snd_mixer_oss_ioctl_card(substream->pcm->card, cmd, arg);
	}
#endif
	if (((cmd >> 8) & 0xff) != 'P')
		return -EINVAL;
#ifdef OSS_DEBUG
	printk("pcm_oss: ioctl = 0x%x\n", cmd);
#endif
	switch (cmd) {
	case SNDCTL_DSP_RESET:
		return snd_pcm_oss_reset(pcm_oss_file);
	case SNDCTL_DSP_SYNC:
		return snd_pcm_oss_sync(pcm_oss_file);
	case SNDCTL_DSP_SPEED:
		if (get_user(res, p))
			return -EFAULT;
		if ((res = snd_pcm_oss_set_rate(pcm_oss_file, res))<0)
			return res;
		return put_user(res, p);
	case SOUND_PCM_READ_RATE:
		res = snd_pcm_oss_get_rate(pcm_oss_file);
		if (res < 0)
			return res;
		return put_user(res, p);
	case SNDCTL_DSP_STEREO:
		if (get_user(res, p))
			return -EFAULT;
		res = res > 0 ? 2 : 1;
		if ((res = snd_pcm_oss_set_channels(pcm_oss_file, res)) < 0)
			return res;
		return put_user(--res, p);
	case SNDCTL_DSP_GETBLKSIZE:
		res = snd_pcm_oss_get_block_size(pcm_oss_file);
		if (res < 0)
			return res;
		return put_user(res, p);
	case SNDCTL_DSP_SETFMT:
		if (get_user(res, p))
			return -EFAULT;
		res = snd_pcm_oss_set_format(pcm_oss_file, res);
		if (res < 0)
			return res;
		return put_user(res, p);
	case SOUND_PCM_READ_BITS:
		res = snd_pcm_oss_get_format(pcm_oss_file);
		if (res < 0)
			return res;
		return put_user(res, p);
	case SNDCTL_DSP_CHANNELS:
		if (get_user(res, p))
			return -EFAULT;
		res = snd_pcm_oss_set_channels(pcm_oss_file, res);
		if (res < 0)
			return res;
		return put_user(res, p);
	case SOUND_PCM_READ_CHANNELS:
		res = snd_pcm_oss_get_channels(pcm_oss_file);
		if (res < 0)
			return res;
		return put_user(res, p);
	case SOUND_PCM_WRITE_FILTER:
	case SOUND_PCM_READ_FILTER:
		return -EIO;
	case SNDCTL_DSP_POST:
		return snd_pcm_oss_post(pcm_oss_file);
	case SNDCTL_DSP_SUBDIVIDE:
		if (get_user(res, p))
			return -EFAULT;
		res = snd_pcm_oss_set_subdivide(pcm_oss_file, res);
		if (res < 0)
			return res;
		return put_user(res, p);
	case SNDCTL_DSP_SETFRAGMENT:
		if (get_user(res, p))
			return -EFAULT;
		return snd_pcm_oss_set_fragment(pcm_oss_file, res);
	case SNDCTL_DSP_GETFMTS:
		res = snd_pcm_oss_get_formats(pcm_oss_file);
		if (res < 0)
			return res;
		return put_user(res, p);
	case SNDCTL_DSP_GETOSPACE:
	case SNDCTL_DSP_GETISPACE:
		return snd_pcm_oss_get_space(pcm_oss_file,
			cmd == SNDCTL_DSP_GETISPACE ?
				SNDRV_PCM_STREAM_CAPTURE : SNDRV_PCM_STREAM_PLAYBACK,
			(struct audio_buf_info __user *) arg);
	case SNDCTL_DSP_NONBLOCK:
		return snd_pcm_oss_nonblock(file);
	case SNDCTL_DSP_GETCAPS:
		res = snd_pcm_oss_get_caps(pcm_oss_file);
		if (res < 0)
			return res;
		return put_user(res, p);
	case SNDCTL_DSP_GETTRIGGER:
		res = snd_pcm_oss_get_trigger(pcm_oss_file);
		if (res < 0)
			return res;
		return put_user(res, p);
	case SNDCTL_DSP_SETTRIGGER:
		if (get_user(res, p))
			return -EFAULT;
		return snd_pcm_oss_set_trigger(pcm_oss_file, res);
	case SNDCTL_DSP_GETIPTR:
	case SNDCTL_DSP_GETOPTR:
		return snd_pcm_oss_get_ptr(pcm_oss_file,
			cmd == SNDCTL_DSP_GETIPTR ?
				SNDRV_PCM_STREAM_CAPTURE : SNDRV_PCM_STREAM_PLAYBACK,
			(struct count_info __user *) arg);
	case SNDCTL_DSP_MAPINBUF:
	case SNDCTL_DSP_MAPOUTBUF:
		return snd_pcm_oss_get_mapbuf(pcm_oss_file,
			cmd == SNDCTL_DSP_MAPINBUF ?
				SNDRV_PCM_STREAM_CAPTURE : SNDRV_PCM_STREAM_PLAYBACK,
			(struct buffmem_desc __user *) arg);
	case SNDCTL_DSP_SETSYNCRO:
		/* stop DMA now.. */
		return 0;
	case SNDCTL_DSP_SETDUPLEX:
		if (snd_pcm_oss_get_caps(pcm_oss_file) & DSP_CAP_DUPLEX)
			return 0;
		return -EIO;
	case SNDCTL_DSP_GETODELAY:
		res = snd_pcm_oss_get_odelay(pcm_oss_file);
		if (res < 0) {
			/* it's for sure, some broken apps don't check for error codes */
			put_user(0, p);
			return res;
		}
		return put_user(res, p);
	case SNDCTL_DSP_PROFILE:
		return 0;	/* silently ignore */
	default:
		snd_printd("pcm_oss: unknown command = 0x%x\n", cmd);
	}
	return -EINVAL;
}

#ifdef CONFIG_COMPAT
/* all compatible */
#define snd_pcm_oss_ioctl_compat	snd_pcm_oss_ioctl
#else
#define snd_pcm_oss_ioctl_compat	NULL
#endif

static ssize_t snd_pcm_oss_read(struct file *file, char __user *buf, size_t count, loff_t *offset)
{
	struct snd_pcm_oss_file *pcm_oss_file;
	struct snd_pcm_substream *substream;

	pcm_oss_file = file->private_data;
	substream = pcm_oss_file->streams[SNDRV_PCM_STREAM_CAPTURE];
	if (substream == NULL)
		return -ENXIO;
#ifndef OSS_DEBUG
	return snd_pcm_oss_read1(substream, buf, count);
#else
	{
		ssize_t res = snd_pcm_oss_read1(substream, buf, count);
		printk("pcm_oss: read %li bytes (returned %li bytes)\n", (long)count, (long)res);
		return res;
	}
#endif
}

static ssize_t snd_pcm_oss_write(struct file *file, const char __user *buf, size_t count, loff_t *offset)
{
	struct snd_pcm_oss_file *pcm_oss_file;
	struct snd_pcm_substream *substream;
	long result;

	pcm_oss_file = file->private_data;
	substream = pcm_oss_file->streams[SNDRV_PCM_STREAM_PLAYBACK];
	if (substream == NULL)
		return -ENXIO;
	result = snd_pcm_oss_write1(substream, buf, count);
#ifdef OSS_DEBUG
	printk("pcm_oss: write %li bytes (wrote %li bytes)\n", (long)count, (long)result);
#endif
	return result;
}

static int snd_pcm_oss_playback_ready(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	if (atomic_read(&runtime->mmap_count))
		return runtime->oss.prev_hw_ptr_interrupt != runtime->hw_ptr_interrupt;
	else
		return snd_pcm_playback_avail(runtime) >= runtime->oss.period_frames;
}

static int snd_pcm_oss_capture_ready(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	if (atomic_read(&runtime->mmap_count))
		return runtime->oss.prev_hw_ptr_interrupt != runtime->hw_ptr_interrupt;
	else
		return snd_pcm_capture_avail(runtime) >= runtime->oss.period_frames;
}

static unsigned int snd_pcm_oss_poll(struct file *file, poll_table * wait)
{
	struct snd_pcm_oss_file *pcm_oss_file;
	unsigned int mask;
	struct snd_pcm_substream *psubstream = NULL, *csubstream = NULL;
	
	pcm_oss_file = file->private_data;

	psubstream = pcm_oss_file->streams[SNDRV_PCM_STREAM_PLAYBACK];
	csubstream = pcm_oss_file->streams[SNDRV_PCM_STREAM_CAPTURE];

	mask = 0;
	if (psubstream != NULL) {
		struct snd_pcm_runtime *runtime = psubstream->runtime;
		poll_wait(file, &runtime->sleep, wait);
		snd_pcm_stream_lock_irq(psubstream);
		if (runtime->status->state != SNDRV_PCM_STATE_DRAINING &&
		    (runtime->status->state != SNDRV_PCM_STATE_RUNNING ||
		     snd_pcm_oss_playback_ready(psubstream)))
			mask |= POLLOUT | POLLWRNORM;
		snd_pcm_stream_unlock_irq(psubstream);
	}
	if (csubstream != NULL) {
		struct snd_pcm_runtime *runtime = csubstream->runtime;
		snd_pcm_state_t ostate;
		poll_wait(file, &runtime->sleep, wait);
		snd_pcm_stream_lock_irq(csubstream);
		if ((ostate = runtime->status->state) != SNDRV_PCM_STATE_RUNNING ||
		    snd_pcm_oss_capture_ready(csubstream))
			mask |= POLLIN | POLLRDNORM;
		snd_pcm_stream_unlock_irq(csubstream);
		if (ostate != SNDRV_PCM_STATE_RUNNING && runtime->oss.trigger) {
			struct snd_pcm_oss_file ofile;
			memset(&ofile, 0, sizeof(ofile));
			ofile.streams[SNDRV_PCM_STREAM_CAPTURE] = pcm_oss_file->streams[SNDRV_PCM_STREAM_CAPTURE];
			runtime->oss.trigger = 0;
			snd_pcm_oss_set_trigger(&ofile, PCM_ENABLE_INPUT);
		}
	}

	return mask;
}

static int snd_pcm_oss_mmap(struct file *file, struct vm_area_struct *area)
{
	struct snd_pcm_oss_file *pcm_oss_file;
	struct snd_pcm_substream *substream = NULL;
	struct snd_pcm_runtime *runtime;
	int err;

#ifdef OSS_DEBUG
	printk("pcm_oss: mmap begin\n");
#endif
	pcm_oss_file = file->private_data;
	switch ((area->vm_flags & (VM_READ | VM_WRITE))) {
	case VM_READ | VM_WRITE:
		substream = pcm_oss_file->streams[SNDRV_PCM_STREAM_PLAYBACK];
		if (substream)
			break;
		/* Fall through */
	case VM_READ:
		substream = pcm_oss_file->streams[SNDRV_PCM_STREAM_CAPTURE];
		break;
	case VM_WRITE:
		substream = pcm_oss_file->streams[SNDRV_PCM_STREAM_PLAYBACK];
		break;
	default:
		return -EINVAL;
	}
	/* set VM_READ access as well to fix memset() routines that do
	   reads before writes (to improve performance) */
	area->vm_flags |= VM_READ;
	if (substream == NULL)
		return -ENXIO;
	runtime = substream->runtime;
	if (!(runtime->info & SNDRV_PCM_INFO_MMAP_VALID))
		return -EIO;
	if (runtime->info & SNDRV_PCM_INFO_INTERLEAVED)
		runtime->access = SNDRV_PCM_ACCESS_MMAP_INTERLEAVED;
	else
		return -EIO;
	
	if (runtime->oss.params) {
		if ((err = snd_pcm_oss_change_params(substream)) < 0)
			return err;
	}
	if (runtime->oss.plugin_first != NULL)
		return -EIO;

	if (area->vm_pgoff != 0)
		return -EINVAL;

	err = snd_pcm_mmap_data(substream, file, area);
	if (err < 0)
		return err;
	runtime->oss.mmap_bytes = area->vm_end - area->vm_start;
	runtime->silence_threshold = 0;
	runtime->silence_size = 0;
#ifdef OSS_DEBUG
	printk("pcm_oss: mmap ok, bytes = 0x%x\n", runtime->oss.mmap_bytes);
#endif
	/* In mmap mode we never stop */
	runtime->stop_threshold = runtime->boundary;

	return 0;
}

#ifdef CONFIG_PROC_FS
/*
 *  /proc interface
 */

static void snd_pcm_oss_proc_read(struct snd_info_entry *entry,
				  struct snd_info_buffer *buffer)
{
	struct snd_pcm_str *pstr = entry->private_data;
	struct snd_pcm_oss_setup *setup = pstr->oss.setup_list;
	down(&pstr->oss.setup_mutex);
	while (setup) {
		snd_iprintf(buffer, "%s %u %u%s%s%s%s%s%s\n",
			    setup->task_name,
			    setup->periods,
			    setup->period_size,
			    setup->disable ? " disable" : "",
			    setup->direct ? " direct" : "",
			    setup->block ? " block" : "",
			    setup->nonblock ? " non-block" : "",
			    setup->partialfrag ? " partial-frag" : "",
			    setup->nosilence ? " no-silence" : "");
		setup = setup->next;
	}
	up(&pstr->oss.setup_mutex);
}

static void snd_pcm_oss_proc_free_setup_list(struct snd_pcm_str * pstr)
{
	unsigned int idx;
	struct snd_pcm_substream *substream;
	struct snd_pcm_oss_setup *setup, *setupn;

	for (idx = 0, substream = pstr->substream;
	     idx < pstr->substream_count; idx++, substream = substream->next)
		substream->oss.setup = NULL;
	for (setup = pstr->oss.setup_list, pstr->oss.setup_list = NULL;
	     setup; setup = setupn) {
		setupn = setup->next;
		kfree(setup->task_name);
		kfree(setup);
	}
	pstr->oss.setup_list = NULL;
}

static void snd_pcm_oss_proc_write(struct snd_info_entry *entry,
				   struct snd_info_buffer *buffer)
{
	struct snd_pcm_str *pstr = entry->private_data;
	char line[128], str[32], task_name[32], *ptr;
	int idx1;
	struct snd_pcm_oss_setup *setup, *setup1, template;

	while (!snd_info_get_line(buffer, line, sizeof(line))) {
		down(&pstr->oss.setup_mutex);
		memset(&template, 0, sizeof(template));
		ptr = snd_info_get_str(task_name, line, sizeof(task_name));
		if (!strcmp(task_name, "clear") || !strcmp(task_name, "erase")) {
			snd_pcm_oss_proc_free_setup_list(pstr);
			up(&pstr->oss.setup_mutex);
			continue;
		}
		for (setup = pstr->oss.setup_list; setup; setup = setup->next) {
			if (!strcmp(setup->task_name, task_name)) {
				template = *setup;
				break;
			}
		}
		ptr = snd_info_get_str(str, ptr, sizeof(str));
		template.periods = simple_strtoul(str, NULL, 10);
		ptr = snd_info_get_str(str, ptr, sizeof(str));
		template.period_size = simple_strtoul(str, NULL, 10);
		for (idx1 = 31; idx1 >= 0; idx1--)
			if (template.period_size & (1 << idx1))
				break;
		for (idx1--; idx1 >= 0; idx1--)
			template.period_size &= ~(1 << idx1);
		do {
			ptr = snd_info_get_str(str, ptr, sizeof(str));
			if (!strcmp(str, "disable")) {
				template.disable = 1;
			} else if (!strcmp(str, "direct")) {
				template.direct = 1;
			} else if (!strcmp(str, "block")) {
				template.block = 1;
			} else if (!strcmp(str, "non-block")) {
				template.nonblock = 1;
			} else if (!strcmp(str, "partial-frag")) {
				template.partialfrag = 1;
			} else if (!strcmp(str, "no-silence")) {
				template.nosilence = 1;
			} else if (!strcmp(str, "buggy-ptr")) {
				template.buggyptr = 1;
			}
		} while (*str);
		if (setup == NULL) {
			setup = kmalloc(sizeof(struct snd_pcm_oss_setup), GFP_KERNEL);
			if (setup) {
				if (pstr->oss.setup_list == NULL) {
					pstr->oss.setup_list = setup;
				} else {
					for (setup1 = pstr->oss.setup_list; setup1->next; setup1 = setup1->next);
					setup1->next = setup;
				}
				template.task_name = kstrdup(task_name, GFP_KERNEL);
			} else {
				buffer->error = -ENOMEM;
			}
		}
		if (setup)
			*setup = template;
		up(&pstr->oss.setup_mutex);
	}
}

static void snd_pcm_oss_proc_init(struct snd_pcm *pcm)
{
	int stream;
	for (stream = 0; stream < 2; ++stream) {
		struct snd_info_entry *entry;
		struct snd_pcm_str *pstr = &pcm->streams[stream];
		if (pstr->substream_count == 0)
			continue;
		if ((entry = snd_info_create_card_entry(pcm->card, "oss", pstr->proc_root)) != NULL) {
			entry->content = SNDRV_INFO_CONTENT_TEXT;
			entry->mode = S_IFREG | S_IRUGO | S_IWUSR;
			entry->c.text.read_size = 8192;
			entry->c.text.read = snd_pcm_oss_proc_read;
			entry->c.text.write_size = 8192;
			entry->c.text.write = snd_pcm_oss_proc_write;
			entry->private_data = pstr;
			if (snd_info_register(entry) < 0) {
				snd_info_free_entry(entry);
				entry = NULL;
			}
		}
		pstr->oss.proc_entry = entry;
	}
}

static void snd_pcm_oss_proc_done(struct snd_pcm *pcm)
{
	int stream;
	for (stream = 0; stream < 2; ++stream) {
		struct snd_pcm_str *pstr = &pcm->streams[stream];
		if (pstr->oss.proc_entry) {
			snd_info_unregister(pstr->oss.proc_entry);
			pstr->oss.proc_entry = NULL;
			snd_pcm_oss_proc_free_setup_list(pstr);
		}
	}
}
#else /* !CONFIG_PROC_FS */
#define snd_pcm_oss_proc_init(pcm)
#define snd_pcm_oss_proc_done(pcm)
#endif /* CONFIG_PROC_FS */

/*
 *  ENTRY functions
 */

static struct file_operations snd_pcm_oss_f_reg =
{
	.owner =	THIS_MODULE,
	.read =		snd_pcm_oss_read,
	.write =	snd_pcm_oss_write,
	.open =		snd_pcm_oss_open,
	.release =	snd_pcm_oss_release,
	.poll =		snd_pcm_oss_poll,
	.unlocked_ioctl =	snd_pcm_oss_ioctl,
	.compat_ioctl =	snd_pcm_oss_ioctl_compat,
	.mmap =		snd_pcm_oss_mmap,
};

static void register_oss_dsp(struct snd_pcm *pcm, int index)
{
	char name[128];
	sprintf(name, "dsp%i%i", pcm->card->number, pcm->device);
	if (snd_register_oss_device(SNDRV_OSS_DEVICE_TYPE_PCM,
				    pcm->card, index, &snd_pcm_oss_f_reg,
				    pcm, name) < 0) {
		snd_printk(KERN_ERR "unable to register OSS PCM device %i:%i\n",
			   pcm->card->number, pcm->device);
	}
}

static int snd_pcm_oss_register_minor(struct snd_pcm *pcm)
{
	pcm->oss.reg = 0;
	if (dsp_map[pcm->card->number] == (int)pcm->device) {
		char name[128];
		int duplex;
		register_oss_dsp(pcm, 0);
		duplex = (pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream_count > 0 && 
			      pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream_count && 
			      !(pcm->info_flags & SNDRV_PCM_INFO_HALF_DUPLEX));
		sprintf(name, "%s%s", pcm->name, duplex ? " (DUPLEX)" : "");
#ifdef SNDRV_OSS_INFO_DEV_AUDIO
		snd_oss_info_register(SNDRV_OSS_INFO_DEV_AUDIO,
				      pcm->card->number,
				      name);
#endif
		pcm->oss.reg++;
		pcm->oss.reg_mask |= 1;
	}
	if (adsp_map[pcm->card->number] == (int)pcm->device) {
		register_oss_dsp(pcm, 1);
		pcm->oss.reg++;
		pcm->oss.reg_mask |= 2;
	}

	if (pcm->oss.reg)
		snd_pcm_oss_proc_init(pcm);

	return 0;
}

static int snd_pcm_oss_disconnect_minor(struct snd_pcm *pcm)
{
	if (pcm->oss.reg) {
		if (pcm->oss.reg_mask & 1) {
			pcm->oss.reg_mask &= ~1;
			snd_unregister_oss_device(SNDRV_OSS_DEVICE_TYPE_PCM,
						  pcm->card, 0);
		}
		if (pcm->oss.reg_mask & 2) {
			pcm->oss.reg_mask &= ~2;
			snd_unregister_oss_device(SNDRV_OSS_DEVICE_TYPE_PCM,
						  pcm->card, 1);
		}
	}
	return 0;
}

static int snd_pcm_oss_unregister_minor(struct snd_pcm *pcm)
{
	snd_pcm_oss_disconnect_minor(pcm);
	if (pcm->oss.reg) {
		if (dsp_map[pcm->card->number] == (int)pcm->device) {
#ifdef SNDRV_OSS_INFO_DEV_AUDIO
			snd_oss_info_unregister(SNDRV_OSS_INFO_DEV_AUDIO, pcm->card->number);
#endif
		}
		pcm->oss.reg = 0;
		snd_pcm_oss_proc_done(pcm);
	}
	return 0;
}

static struct snd_pcm_notify snd_pcm_oss_notify =
{
	.n_register =	snd_pcm_oss_register_minor,
	.n_disconnect = snd_pcm_oss_disconnect_minor,
	.n_unregister =	snd_pcm_oss_unregister_minor,
};

static int __init alsa_pcm_oss_init(void)
{
	int i;
	int err;

	/* check device map table */
	for (i = 0; i < SNDRV_CARDS; i++) {
		if (dsp_map[i] < 0 || dsp_map[i] >= SNDRV_PCM_DEVICES) {
			snd_printk(KERN_ERR "invalid dsp_map[%d] = %d\n",
				   i, dsp_map[i]);
			dsp_map[i] = 0;
		}
		if (adsp_map[i] < 0 || adsp_map[i] >= SNDRV_PCM_DEVICES) {
			snd_printk(KERN_ERR "invalid adsp_map[%d] = %d\n",
				   i, adsp_map[i]);
			adsp_map[i] = 1;
		}
	}
	if ((err = snd_pcm_notify(&snd_pcm_oss_notify, 0)) < 0)
		return err;
	return 0;
}

static void __exit alsa_pcm_oss_exit(void)
{
	snd_pcm_notify(&snd_pcm_oss_notify, 1);
}

module_init(alsa_pcm_oss_init)
module_exit(alsa_pcm_oss_exit)
