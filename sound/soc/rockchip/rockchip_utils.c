// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Rockchip Utils API
 *
 * Copyright (c) 2023 Rockchip Electronics Co. Ltd.
 */

#include <linux/module.h>
#include <linux/notifier.h>
#include <soc/rockchip/rockchip_dmc.h>
#include <soc/rockchip/rockchip-system-status.h>
#include <sound/pcm_params.h>
#include <sound/dmaengine_pcm.h>
#include "rockchip_utils.h"

#define DMC_STALL_TIME_US_DEFAULT	100
#define TIME_MARGIN_US			20

static DEFINE_MUTEX(list_mutex);
static LIST_HEAD(substream_ref_list);

struct substream_ref {
	struct list_head node;
	struct snd_pcm_substream *substream;
};

static int substream_ref_new(struct snd_pcm_substream *substream)
{
	struct substream_ref *ref = NULL;
	bool found = false;
	int ret = 0;

	mutex_lock(&list_mutex);
	list_for_each_entry(ref, &substream_ref_list, node) {
		if (ref->substream == substream) {
			found = true;
			break;
		}
	}

	if (found) {
		ret = -EEXIST;
		goto _err_unlock;
	}

	ref = kzalloc(sizeof(*ref), GFP_KERNEL);
	if (!ref) {
		ret = -ENOMEM;
		goto _err_unlock;
	}

	ref->substream = substream;

	list_add(&ref->node, &substream_ref_list);

_err_unlock:
	mutex_unlock(&list_mutex);

	return ret;
}

static bool substream_ref_found(struct snd_pcm_substream *substream)
{
	struct substream_ref *ref = NULL, *_ref = NULL;
	bool found = false;

	mutex_lock(&list_mutex);
	list_for_each_entry_safe(ref, _ref, &substream_ref_list, node) {
		if (ref->substream == substream) {
			list_del(&ref->node);
			found = true;
			break;
		}
	}
	mutex_unlock(&list_mutex);

	if (found)
		kfree(ref);

	return found;
}

static bool fifo_bigger_than_stall(struct snd_pcm_substream *substream,
				   struct snd_pcm_hw_params *params,
				   struct snd_soc_dai *dai,
				   int fifo_word)
{
	unsigned int rate = params_rate(params);
	unsigned int channels = params_channels(params);
	int width = params_physical_width(params);
	int fifo_time, stall_time, data_word;

	dev_dbg(dai->dev, "stream[%d]: %px, rate: %u, channels: %u, width: %d\n",
		substream->stream, substream, rate, channels, width);

	stall_time = rockchip_dmcfreq_get_stall_time_ns() / 1000;
	if (!stall_time)
		stall_time = DMC_STALL_TIME_US_DEFAULT;

	stall_time += TIME_MARGIN_US;

	data_word = rate * channels * width / 32;

	if (!fifo_word || !data_word)
		return true;

	fifo_time = 1000000 * fifo_word / data_word;

	dev_dbg(dai->dev, "data: %d, fifo: %d, fifo time: %d us, stall time: %d us\n",
		data_word, fifo_word, fifo_time, stall_time);

	return (fifo_time > stall_time);
}

void rockchip_utils_get_performance(struct snd_pcm_substream *substream,
				    struct snd_pcm_hw_params *params,
				    struct snd_soc_dai *dai,
				    int fifo_word)
{
	might_sleep();

	if (fifo_bigger_than_stall(substream, params, dai, fifo_word))
		return;

	if (substream_ref_new(substream))
		return;

	dev_dbg(dai->dev, "%s: stream[%d]: %px\n",
		__func__, substream->stream, substream);

	rockchip_set_system_status(SYS_STATUS_PERFORMANCE);
}
EXPORT_SYMBOL_GPL(rockchip_utils_get_performance);

void rockchip_utils_put_performance(struct snd_pcm_substream *substream,
				    struct snd_soc_dai *dai)
{
	might_sleep();

	if (!substream_ref_found(substream))
		return;

	dev_dbg(dai->dev, "%s: stream[%d]: %px\n",
		__func__, substream->stream, substream);

	rockchip_clear_system_status(SYS_STATUS_PERFORMANCE);
}
EXPORT_SYMBOL_GPL(rockchip_utils_put_performance);

MODULE_LICENSE("GPL");
