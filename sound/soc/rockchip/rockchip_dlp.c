// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Rockchip DLP (Digital Loopback) Driver
 *
 * Copyright (c) 2022 Rockchip Electronics Co. Ltd.
 * Author: Sugar Zhang <sugar.zhang@rock-chips.com>
 *
 */

#include <linux/kref.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "rockchip_dlp.h"

#define PBUF_CNT			2

/* MUST: dlp_text should be match to enum dlp_mode */
static const char *const dlp_text[] = {
	"Disabled",
	"2CH: 1 Loopback + 1 Mic",
	"2CH: 1 Mic + 1 Loopback",
	"2CH: 1 Mic + 1 Loopback-mixed",
	"2CH: 2 Loopbacks",
	"4CH: 2 Mics + 2 Loopbacks",
	"4CH: 2 Mics + 1 Loopback-mixed",
	"4CH: 4 Loopbacks",
	"6CH: 4 Mics + 2 Loopbacks",
	"6CH: 4 Mics + 1 Loopback-mixed",
	"6CH: 6 Loopbacks",
	"8CH: 6 Mics + 2 Loopbacks",
	"8CH: 6 Mics + 1 Loopback-mixed",
	"8CH: 8 Loopbacks",
	"10CH: 8 Mics + 2 Loopbacks",
	"10CH: 8 Mics + 1 Loopback-mixed",
	"16CH: 8 Mics + 8 Loopbacks",
};

static inline void drd_buf_free(struct dlp_runtime_data *drd)
{
	if (drd && drd->buf) {
		dev_dbg(drd->parent->dev, "%s: stream[%d]: 0x%px\n",
			__func__, drd->stream, drd->buf);
		kvfree(drd->buf);
		drd->buf = NULL;
	}
}

static inline int drd_buf_alloc(struct dlp_runtime_data *drd, int size)
{
	if (drd) {
		if (snd_BUG_ON(drd->buf))
			return -EINVAL;

		drd->buf = kvzalloc(size, GFP_KERNEL);
		if (!drd->buf)
			return -ENOMEM;
		dev_dbg(drd->parent->dev, "%s: stream[%d]: 0x%px\n",
			__func__, drd->stream, drd->buf);
	}

	return 0;
}

static inline void dlp_activate(struct dlp *dlp)
{
	atomic_inc(&dlp->active);
}

static inline void dlp_deactivate(struct dlp *dlp)
{
	atomic_dec(&dlp->active);
}

static inline bool dlp_mode_channels_match(struct dlp *dlp,
					   int ch, int *expected)
{
	*expected = 0;

	switch (dlp->mode) {
	case DLP_MODE_DISABLED:
		return true;
	case DLP_MODE_2CH_1LP_1MIC:
	case DLP_MODE_2CH_1MIC_1LP:
	case DLP_MODE_2CH_1MIC_1LP_MIX:
	case DLP_MODE_2CH_2LP:
		*expected = 2;
		return (ch == 2);
	case DLP_MODE_4CH_2MIC_2LP:
	case DLP_MODE_4CH_2MIC_1LP_MIX:
	case DLP_MODE_4CH_4LP:
		*expected = 4;
		return (ch == 4);
	case DLP_MODE_6CH_4MIC_2LP:
	case DLP_MODE_6CH_4MIC_1LP_MIX:
	case DLP_MODE_6CH_6LP:
		*expected = 6;
		return (ch == 6);
	case DLP_MODE_8CH_6MIC_2LP:
	case DLP_MODE_8CH_6MIC_1LP_MIX:
	case DLP_MODE_8CH_8LP:
		*expected = 8;
		return (ch == 8);
	case DLP_MODE_10CH_8MIC_2LP:
	case DLP_MODE_10CH_8MIC_1LP_MIX:
		*expected = 10;
		return (ch == 10);
	case DLP_MODE_16CH_8MIC_8LP:
		*expected = 16;
		return (ch == 16);
	default:
		return false;
	}
}

static int dlp_get_offset_size(struct dlp_runtime_data *drd,
			       enum dlp_mode mode, int *ofs, int *size, bool *mix)
{
	bool is_playback = drd->stream == SNDRV_PCM_STREAM_PLAYBACK;
	int ret = 0;

	switch (mode) {
	case DLP_MODE_2CH_1LP_1MIC:
		*ofs = 0;
		*size = dlp_channels_to_bytes(drd, 1);
		break;
	case DLP_MODE_2CH_1MIC_1LP:
		*ofs = dlp_channels_to_bytes(drd, 1);
		*size = dlp_channels_to_bytes(drd, 1);
		break;
	case DLP_MODE_2CH_1MIC_1LP_MIX:
		if (is_playback) {
			*ofs = 0;
			*size = dlp_frames_to_bytes(drd, 1);
			if (mix)
				*mix = true;
		} else {
			*ofs = dlp_channels_to_bytes(drd, 1);
			*size = dlp_channels_to_bytes(drd, 1);
		}
		break;
	case DLP_MODE_2CH_2LP:
		*ofs = 0;
		*size = dlp_channels_to_bytes(drd, 2);
		break;
	case DLP_MODE_4CH_2MIC_2LP:
		if (is_playback) {
			*ofs = 0;
			*size = dlp_channels_to_bytes(drd, 2);
		} else {
			*ofs = dlp_channels_to_bytes(drd, 2);
			*size = dlp_channels_to_bytes(drd, 2);
		}
		break;
	case DLP_MODE_4CH_2MIC_1LP_MIX:
		if (is_playback) {
			*ofs = 0;
			*size = dlp_frames_to_bytes(drd, 1);
			if (mix)
				*mix = true;
		} else {
			*ofs = dlp_channels_to_bytes(drd, 2);
			*size = dlp_channels_to_bytes(drd, 1);
		}
		break;
	case DLP_MODE_4CH_4LP:
		*ofs = 0;
		*size = dlp_channels_to_bytes(drd, 4);
		break;
	case DLP_MODE_6CH_4MIC_2LP:
		if (is_playback) {
			*ofs = 0;
			*size = dlp_channels_to_bytes(drd, 2);
		} else {
			*ofs = dlp_channels_to_bytes(drd, 4);
			*size = dlp_channels_to_bytes(drd, 2);
		}
		break;
	case DLP_MODE_6CH_4MIC_1LP_MIX:
		if (is_playback) {
			*ofs = 0;
			*size = dlp_frames_to_bytes(drd, 1);
			if (mix)
				*mix = true;
		} else {
			*ofs = dlp_channels_to_bytes(drd, 4);
			*size = dlp_channels_to_bytes(drd, 1);
		}
		break;
	case DLP_MODE_6CH_6LP:
		*ofs = 0;
		*size = dlp_channels_to_bytes(drd, 6);
		break;
	case DLP_MODE_8CH_6MIC_2LP:
		if (is_playback) {
			*ofs = 0;
			*size = dlp_channels_to_bytes(drd, 2);
		} else {
			*ofs = dlp_channels_to_bytes(drd, 6);
			*size = dlp_channels_to_bytes(drd, 2);
		}
		break;
	case DLP_MODE_8CH_6MIC_1LP_MIX:
		if (is_playback) {
			*ofs = 0;
			*size = dlp_frames_to_bytes(drd, 1);
			if (mix)
				*mix = true;
		} else {
			*ofs = dlp_channels_to_bytes(drd, 6);
			*size = dlp_channels_to_bytes(drd, 1);
		}
		break;
	case DLP_MODE_8CH_8LP:
		*ofs = 0;
		*size = dlp_channels_to_bytes(drd, 8);
		break;
	case DLP_MODE_10CH_8MIC_2LP:
		if (is_playback) {
			*ofs = 0;
			*size = dlp_channels_to_bytes(drd, 2);
		} else {
			*ofs = dlp_channels_to_bytes(drd, 8);
			*size = dlp_channels_to_bytes(drd, 2);
		}
		break;
	case DLP_MODE_10CH_8MIC_1LP_MIX:
		if (is_playback) {
			*ofs = 0;
			*size = dlp_frames_to_bytes(drd, 1);
			if (mix)
				*mix = true;
		} else {
			*ofs = dlp_channels_to_bytes(drd, 8);
			*size = dlp_channels_to_bytes(drd, 1);
		}
		break;
	case DLP_MODE_16CH_8MIC_8LP:
		if (is_playback) {
			*ofs = 0;
			*size = dlp_channels_to_bytes(drd, 8);
		} else {
			*ofs = dlp_channels_to_bytes(drd, 8);
			*size = dlp_channels_to_bytes(drd, 8);
		}
		break;
	default:
		*ofs = 0;
		*size = 0;
		if (mix)
			*mix = false;
		ret = -EINVAL;
	}

	return ret;
}

static int dlp_mix_frame_buffer(struct dlp_runtime_data *drd, void *buf)
{
	int sample_bytes = dlp_channels_to_bytes(drd, 1);
	int16_t *p16 = (int16_t *)buf, v16 = 0;
	int32_t *p32 = (int32_t *)buf, v32 = 0;
	int i = 0;

	switch (sample_bytes) {
	case 2:
		for (i = 0; i < drd->channels; i++)
			v16 += (p16[i] / drd->channels);
		p16[0] = v16;
		break;
	case 4:
		for (i = 0; i < drd->channels; i++)
			v32 += (p32[i] / drd->channels);
		p32[0] = v32;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static inline int drd_init_from(struct dlp_runtime_data *drd, struct dlp_runtime_data *src)
{
	memset(drd, 0x0, sizeof(*drd));

	drd->parent = src->parent;
	drd->buf_sz = src->buf_sz;
	drd->period_sz = src->period_sz;
	drd->frame_bytes = src->frame_bytes;
	drd->channels = src->channels;
	drd->stream = src->stream;

	INIT_LIST_HEAD(&drd->node);
	kref_init(&drd->refcount);

	dev_dbg(drd->parent->dev, "%s: drd: 0x%px\n", __func__, drd);

	return 0;
}

static void drd_avl_list_add(struct dlp *dlp, struct dlp_runtime_data *drd)
{
	unsigned long flags;

	spin_lock_irqsave(&dlp->lock, flags);
	list_add(&drd->node, &dlp->drd_avl_list);
	dlp->drd_avl_count++;
	spin_unlock_irqrestore(&dlp->lock, flags);
}

static struct dlp_runtime_data *drd_avl_list_get(struct dlp *dlp)
{
	struct dlp_runtime_data *drd = NULL;
	unsigned long flags;

	spin_lock_irqsave(&dlp->lock, flags);
	if (!list_empty(&dlp->drd_avl_list)) {
		drd = list_first_entry(&dlp->drd_avl_list, struct dlp_runtime_data, node);
		list_del(&drd->node);
		dlp->drd_avl_count--;
	}
	spin_unlock_irqrestore(&dlp->lock, flags);

	return drd;
}

static void drd_release(struct kref *ref)
{
	struct dlp_runtime_data *drd =
		container_of(ref, struct dlp_runtime_data, refcount);

	dev_dbg(drd->parent->dev, "%s: drd: 0x%px\n", __func__, drd);

	drd_buf_free(drd);
	/* move to available list */
	drd_avl_list_add(drd->parent, drd);
}

static inline struct dlp_runtime_data *drd_get(struct dlp_runtime_data *drd)
{
	if (!drd)
		return NULL;

	return kref_get_unless_zero(&drd->refcount) ? drd : NULL;
}

static inline void drd_put(struct dlp_runtime_data *drd)
{
	if (!drd)
		return;

	kref_put(&drd->refcount, drd_release);
}

static void drd_rdy_list_add(struct dlp *dlp, struct dlp_runtime_data *drd)
{
	unsigned long flags;

	spin_lock_irqsave(&dlp->lock, flags);
	list_add(&drd->node, &dlp->drd_rdy_list);
	spin_unlock_irqrestore(&dlp->lock, flags);
}

static struct dlp_runtime_data *drd_rdy_list_get(struct dlp *dlp)
{
	struct dlp_runtime_data *drd = NULL;
	unsigned long flags;

	spin_lock_irqsave(&dlp->lock, flags);
	if (!list_empty(&dlp->drd_rdy_list)) {
		/* the newest one */
		drd = list_first_entry(&dlp->drd_rdy_list, struct dlp_runtime_data, node);
		list_del(&drd->node);
	}
	spin_unlock_irqrestore(&dlp->lock, flags);

	return drd;
}

static bool drd_rdy_list_found(struct dlp *dlp, struct dlp_runtime_data *drd)
{
	struct dlp_runtime_data *_drd;
	unsigned long flags;
	bool found = false;

	if (!drd)
		return false;

	spin_lock_irqsave(&dlp->lock, flags);
	list_for_each_entry(_drd, &dlp->drd_rdy_list, node) {
		if (_drd == drd) {
			found = true;
			break;
		}
	}
	spin_unlock_irqrestore(&dlp->lock, flags);

	return found;
}

static void drd_rdy_list_free(struct dlp *dlp)
{
	struct list_head drd_list;
	struct dlp_runtime_data *drd;
	unsigned long flags;

	spin_lock_irqsave(&dlp->lock, flags);
	list_replace_init(&dlp->drd_rdy_list, &drd_list);
	spin_unlock_irqrestore(&dlp->lock, flags);

	while (!list_empty(&drd_list)) {
		drd = list_first_entry(&drd_list, struct dlp_runtime_data, node);
		list_del(&drd->node);
		drd_put(drd);
	}
}

static void drd_ref_list_add(struct dlp *dlp, struct dlp_runtime_data *drd)
{
	unsigned long flags;

	/* push valid playback into ref list */
	spin_lock_irqsave(&dlp->lock, flags);
	list_add_tail(&drd->node, &dlp->drd_ref_list);
	spin_unlock_irqrestore(&dlp->lock, flags);
}

static struct dlp_runtime_data *drd_ref_list_first(struct dlp *dlp)
{
	struct dlp_runtime_data *drd = NULL;
	unsigned long flags;

	spin_lock_irqsave(&dlp->lock, flags);
	if (!list_empty(&dlp->drd_ref_list))
		drd = list_first_entry(&dlp->drd_ref_list, struct dlp_runtime_data, node);
	spin_unlock_irqrestore(&dlp->lock, flags);

	return drd;
}

static struct dlp_runtime_data *drd_ref_list_del(struct dlp *dlp,
						 struct dlp_runtime_data *drd)
{
	unsigned long flags;

	spin_lock_irqsave(&dlp->lock, flags);
	list_del(&drd->node);
	spin_unlock_irqrestore(&dlp->lock, flags);

	return drd;
}

static void drd_ref_list_free(struct dlp *dlp)
{
	struct list_head drd_list;
	struct dlp_runtime_data *drd;
	unsigned long flags;

	spin_lock_irqsave(&dlp->lock, flags);
	list_replace_init(&dlp->drd_ref_list, &drd_list);
	spin_unlock_irqrestore(&dlp->lock, flags);

	while (!list_empty(&drd_list)) {
		drd = list_first_entry(&drd_list, struct dlp_runtime_data, node);
		list_del(&drd->node);

		if (!atomic_read(&drd->stop))
			drd_rdy_list_add(dlp, drd);
		else
			drd_put(drd);
	}
}

int dlp_hw_params(struct snd_soc_component *component,
		  struct snd_pcm_substream *substream,
		  struct snd_pcm_hw_params *params)
{
	struct dlp *dlp = soc_component_to_dlp(component);
	struct dlp_runtime_data *drd = substream_to_drd(substream);
	bool is_playback = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	int ch_req = params_channels(params), ch_exp = 0;

	if (unlikely(!dlp || !drd))
		return -EINVAL;

	/* mode should match to channels */
	if (!is_playback && !dlp_mode_channels_match(dlp, ch_req, &ch_exp)) {
		dev_err(dlp->dev,
			"capture %d ch, expected: %d ch for loopback mode-%d\n",
			ch_req, ch_exp, dlp->mode);
		return -EINVAL;
	}

	drd->frame_bytes = snd_pcm_format_size(params_format(params),
					       params_channels(params));
	drd->period_sz = params_period_size(params);
	drd->buf_sz = params_buffer_size(params);
	drd->channels = params_channels(params);

	if (is_playback)
		drd->buf_sz *= PBUF_CNT;

	return 0;
}
EXPORT_SYMBOL_GPL(dlp_hw_params);

int dlp_open(struct dlp *dlp, struct dlp_runtime_data *drd,
	     struct snd_pcm_substream *substream)
{
	if (unlikely(!dlp || !drd))
		return -EINVAL;

	drd->parent = dlp;
	drd->stream = substream->stream;

	substream->runtime->private_data = drd;

	dlp_activate(dlp);

	return 0;
}
EXPORT_SYMBOL_GPL(dlp_open);

int dlp_close(struct dlp *dlp, struct dlp_runtime_data *drd,
	      struct snd_pcm_substream *substream)
{
	if (unlikely(!dlp || !drd))
		return -EINVAL;

	/*
	 * In case: open -> hw_params -> prepare -> close flow
	 * should check and free all.
	 */
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		drd_put(dlp->drd_pb_shadow);
		dlp->drd_pb_shadow = NULL;
	} else {
		drd_buf_free(drd);
	}

	dlp_deactivate(dlp);

	return 0;
}
EXPORT_SYMBOL_GPL(dlp_close);

void dlp_dma_complete(struct dlp *dlp, struct dlp_runtime_data *drd)
{
	if (unlikely(!dlp || !drd))
		return;

	atomic64_inc(&drd->period_elapsed);
}
EXPORT_SYMBOL_GPL(dlp_dma_complete);

int dlp_start(struct snd_soc_component *component,
	      struct snd_pcm_substream *substream,
	      struct device *dev,
	      dma_pointer_f dma_pointer)
{
	struct dlp *dlp = soc_component_to_dlp(component);
	int bstream = SNDRV_PCM_STREAM_LAST - substream->stream;
	struct snd_pcm_str *bro = &substream->pcm->streams[bstream];
	struct snd_pcm_substream *bsubstream = bro->substream;
	struct dlp_runtime_data *adrd = substream_to_drd(substream);
	struct dlp_runtime_data *bdrd = substream_to_drd(bsubstream);
	struct dlp_runtime_data *drd_ref;
	bool is_playback = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	uint64_t a = 0, b = 0;
	snd_pcm_uframes_t fifo_a = 0, fifo_b = 0;
	snd_pcm_sframes_t delta = 0;

	if (unlikely(!dlp || !adrd || !dma_pointer))
		return -EINVAL;

	if (dlp->mode == DLP_MODE_DISABLED)
		return -EINVAL;

	fifo_a = dlp->config->get_fifo_count(dev, substream);
	a = dma_pointer(component, substream) % adrd->period_sz;

	if (bsubstream->runtime && snd_pcm_running(bsubstream)) {
		if (unlikely(!bdrd))
			return -EINVAL;

		fifo_b = dlp->config->get_fifo_count(dev, bsubstream);
		b = dma_pointer(component, bsubstream) % bdrd->period_sz;

		drd_ref = drd_rdy_list_get(dlp);
		if (unlikely(!drd_ref)) {
			dev_err(dev, "Failed to get rdy drd\n");
			return -EINVAL;
		}

		a += (atomic64_read(&adrd->period_elapsed) * adrd->period_sz);
		b += (atomic64_read(&bdrd->period_elapsed) * bdrd->period_sz);

		fifo_a = dlp_bytes_to_frames(adrd, fifo_a * 4);
		fifo_b = dlp_bytes_to_frames(bdrd, fifo_b * 4);

		delta = is_playback ? (a - fifo_a) - (b + fifo_b) : (b - fifo_b) - (a + fifo_a);

		drd_ref->hw_ptr_delta = delta;

		drd_ref_list_add(dlp, drd_ref);
	}

	if (is_playback)
		dev_dbg(dev, "START-P: DMA-P: %llu, DMA-C: %llu, FIFO-P: %lu, FIFO-C: %lu, DELTA: %ld\n",
			a, b, fifo_a, fifo_b, delta);
	else
		dev_dbg(dev, "START-C: DMA-P: %llu, DMA-C: %llu, FIFO-P: %lu, FIFO-C: %lu, DELTA: %ld\n",
			b, a, fifo_b, fifo_a, delta);

	return 0;
}
EXPORT_SYMBOL_GPL(dlp_start);

void dlp_stop(struct snd_soc_component *component,
	      struct snd_pcm_substream *substream,
	      dma_pointer_f dma_pointer)
{
	struct dlp *dlp = soc_component_to_dlp(component);
	struct dlp_runtime_data *drd = substream_to_drd(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	uint64_t appl_ptr, hw_ptr;

	if (unlikely(!dlp || !drd || !runtime || !dma_pointer))
		return;

	if (dlp->mode == DLP_MODE_DISABLED)
		return;

	/* any data in FIFOs will be gone ,so don't care */
	appl_ptr = READ_ONCE(runtime->control->appl_ptr);
	hw_ptr = dma_pointer(component, substream) % drd->period_sz;
	hw_ptr += (atomic64_read(&drd->period_elapsed) * drd->period_sz);

	/*
	 * playback:
	 *
	 * snd_pcm_drop:  hw_ptr will be smaller than appl_ptr
	 * snd_pcm_drain, hw_ptr will be equal to appl_ptr
	 *
	 * anyway, we should use the smaller one, obviously, it's hw_ptr.
	 */
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		if (dlp->drd_pb_shadow) {
			dlp->drd_pb_shadow->hw_ptr = min(hw_ptr, appl_ptr);
			atomic_set(&dlp->drd_pb_shadow->stop, 1);
		}
		drd_rdy_list_free(dlp);
	} else {
		/* free residue playback ref list for capture when stop */
		drd_ref_list_free(dlp);
	}

	atomic64_set(&drd->period_elapsed, 0);

	dev_dbg(dlp->dev, "STOP-%s: applptr: %llu, hwptr: %llu\n",
		substream->stream ? "C" : "P", appl_ptr, hw_ptr);
}
EXPORT_SYMBOL_GPL(dlp_stop);

static int process_capture(struct snd_soc_component *component,
			   struct snd_pcm_substream *substream,
			   unsigned long hwoff,
			   void __user *buf, unsigned long bytes)
{
	struct dlp *dlp = soc_component_to_dlp(component);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct dlp_runtime_data *drd = substream_to_drd(substream);
	struct dlp_runtime_data *drd_ref = NULL;
	snd_pcm_sframes_t frames = 0;
	snd_pcm_sframes_t frames_consumed = 0, frames_residue = 0, frames_tmp = 0;
	snd_pcm_sframes_t ofs = 0;
	snd_pcm_uframes_t appl_ptr;
	int ofs_cap, ofs_play, size_cap, size_play;
	int i = 0, j = 0, ret = 0;
	bool free_ref = false, mix = false;
	char *cbuf = NULL, *pbuf = NULL;
	void *dma_ptr;

	if (unlikely(!drd || !runtime || !buf))
		return -EINVAL;

	frames = dlp_bytes_to_frames(drd, bytes);
	dma_ptr = runtime->dma_area + hwoff;
	cbuf = drd->buf;

	appl_ptr = READ_ONCE(runtime->control->appl_ptr);

	memcpy(cbuf, dma_ptr, bytes);
#ifdef DLP_DBG
	/* DBG: mark STUB in ch-REC for trace each read */
	memset(cbuf, 0x22, dlp_channels_to_bytes(drd, 1));
#endif
	ret = dlp_get_offset_size(drd, dlp->mode, &ofs_cap, &size_cap, NULL);
	if (ret) {
		dev_err(dlp->dev, "Failed to get dlp cap offset\n");
		return -EINVAL;
	}

	/* clear channel-LP_CHN */
	for (i = 0; i < frames; i++) {
		cbuf = drd->buf + dlp_frames_to_bytes(drd, i) + ofs_cap;
		memset(cbuf, 0x0, size_cap);
	}

start:
	drd_ref = drd_get(drd_ref_list_first(dlp));
	if (!drd_ref)
		return 0;

	ret = dlp_get_offset_size(drd_ref, dlp->mode, &ofs_play, &size_play, &mix);
	if (ret) {
		dev_err(dlp->dev, "Failed to get dlp play offset\n");
		goto _drd_put;
	}

	ofs = appl_ptr + drd_ref->hw_ptr_delta;

	/*
	 * if playback stop, process the data tail and then
	 * free drd_ref if data consumed.
	 */
	if (atomic_read(&drd_ref->stop)) {
		if (ofs >= drd_ref->hw_ptr) {
			drd_put(drd_ref_list_del(dlp, drd_ref));
			goto _drd_put;
		} else if ((ofs + frames) > drd_ref->hw_ptr) {
			dev_dbg(dlp->dev, "applptr: %8lu, ofs': %7ld, refhwptr: %lld, frames: %ld (*)\n",
				appl_ptr, ofs, drd_ref->hw_ptr, frames);
			/*
			 * should ignore the data that after play stop
			 * and care about if the next ref start in the
			 * same window
			 */
			frames_tmp = drd_ref->hw_ptr - ofs;
			frames_residue = frames - frames_tmp;
			frames = frames_tmp;
			free_ref = true;
		}
	}

	/*
	 * should ignore the data that before play start:
	 *
	 * frames:
	 * +---------------------------------------------+
	 * |      ofs<0       |         ofs>0            |
	 * +---------------------------------------------+
	 *
	 */
	if ((ofs + frames) <= 0)
		goto _drd_put;

	/* skip if ofs < 0 and fixup ofs */
	j = 0;
	if (ofs < 0) {
		dev_dbg(dlp->dev, "applptr: %8lu, ofs: %8ld, frames: %ld (*)\n",
			appl_ptr, ofs, frames);
		j = -ofs;
		frames += ofs;
		ofs = 0;
		appl_ptr += j;
	}

	ofs %= drd_ref->buf_sz;

	dev_dbg(dlp->dev, "applptr: %8lu, ofs: %8ld, frames: %5ld, refc: %u\n",
		appl_ptr, ofs, frames, kref_read(&drd_ref->refcount));

	for (i = 0; i < frames; i++, j++) {
		cbuf = drd->buf + dlp_frames_to_bytes(drd, j + frames_consumed) + ofs_cap;
		pbuf = drd_ref->buf + dlp_frames_to_bytes(drd_ref, ((i + ofs) % drd_ref->buf_sz)) + ofs_play;
		if (mix)
			dlp_mix_frame_buffer(drd_ref, pbuf);
		memcpy(cbuf, pbuf, size_cap);
	}

	appl_ptr += frames;
	frames_consumed += frames;

	if (free_ref) {
		drd_put(drd_ref_list_del(dlp, drd_ref));
		drd_put(drd_ref);
		drd_ref = NULL;
		free_ref = false;
		if (frames_residue) {
			frames = frames_residue;
			frames_residue = 0;
			goto start;
		}
	}

_drd_put:
	drd_put(drd_ref);
	drd_ref = NULL;

	return 0;
}

static int process_playback(struct snd_soc_component *component,
			    struct snd_pcm_substream *substream,
			    unsigned long hwoff,
			    void __user *buf, unsigned long bytes)
{
	struct dlp *dlp = soc_component_to_dlp(component);
	struct dlp_runtime_data *drd;
	char *pbuf;
	int ret = 0;

	drd = drd_get(dlp->drd_pb_shadow);
	if (!drd)
		return 0;

	pbuf = drd->buf + drd->buf_ofs;

	if (copy_from_user(pbuf, buf, bytes)) {
		ret = -EFAULT;
		goto err_put;
	}

	drd->buf_ofs += bytes;
	drd->buf_ofs %= dlp_frames_to_bytes(drd, drd->buf_sz);

err_put:
	drd_put(drd);

	return ret;
}

static int dlp_process(struct snd_soc_component *component,
		       struct snd_pcm_substream *substream,
		       unsigned long hwoff,
		       void __user *buf, unsigned long bytes)
{
	struct dlp *dlp = soc_component_to_dlp(component);
	int ret = 0;

	if (dlp->mode == DLP_MODE_DISABLED)
		return -EINVAL;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		ret = process_playback(component, substream, hwoff, buf, bytes);
	else
		ret = process_capture(component, substream, hwoff, buf, bytes);

	return ret;
}

int dlp_copy_user(struct snd_soc_component *component,
		  struct snd_pcm_substream *substream,
		  int channel, unsigned long hwoff,
		  void __user *buf, unsigned long bytes)
{
	struct dlp_runtime_data *drd = substream_to_drd(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	bool is_playback = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	void *dma_ptr;
	int ret;

	if (unlikely(!drd || !runtime || !buf))
		return -EINVAL;

	dma_ptr = runtime->dma_area + hwoff +
		  channel * (runtime->dma_bytes / runtime->channels);

	if (is_playback)
		if (copy_from_user(dma_ptr, buf, bytes))
			return -EFAULT;

	ret = dlp_process(component, substream, hwoff, buf, bytes);
	if (!ret)
		dma_ptr = drd->buf;

	if (!is_playback)
		if (copy_to_user(buf, dma_ptr, bytes))
			return -EFAULT;

	return 0;
}
EXPORT_SYMBOL_GPL(dlp_copy_user);

static SOC_ENUM_SINGLE_EXT_DECL(dlp_mode, dlp_text);

static int dlp_mode_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct dlp *dlp = soc_component_to_dlp(component);

	ucontrol->value.enumerated.item[0] = dlp->mode;

	return 0;
}

static int dlp_mode_put(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct dlp *dlp = soc_component_to_dlp(component);
	unsigned int mode = ucontrol->value.enumerated.item[0];

	/* MUST: do not update mode while stream is running */
	if (atomic_read(&dlp->active)) {
		dev_err(dlp->dev, "Should set this mode before pcm open\n");
		return -EPERM;
	}

	if (mode == dlp->mode)
		return 0;

	dlp->mode = mode;

	return 1;
}

static const struct snd_kcontrol_new dlp_controls[] = {
	SOC_ENUM_EXT("Software Digital Loopback Mode", dlp_mode,
		     dlp_mode_get, dlp_mode_put),
};

int dlp_prepare(struct snd_soc_component *component,
		struct snd_pcm_substream *substream)
{
	struct dlp *dlp = soc_component_to_dlp(component);
	struct dlp_runtime_data *drd = substream_to_drd(substream);
	struct dlp_runtime_data *drd_new = NULL;
	int buf_bytes, last_buf_bytes;
	int ret;

	if (unlikely(!dlp || !drd))
		return -EINVAL;

	if (dlp->mode == DLP_MODE_DISABLED)
		return 0;

	buf_bytes = dlp_frames_to_bytes(drd, drd->buf_sz);
	last_buf_bytes = dlp_frames_to_bytes(drd, drd->last_buf_sz);

	if (substream->runtime->status->state == SNDRV_PCM_STATE_XRUN)
		dev_dbg(dlp->dev, "stream[%d]: prepare from XRUN\n",
			substream->stream);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		dev_dbg(dlp->dev, "avl count: %d\n", dlp->drd_avl_count);
		if (snd_BUG_ON(!dlp->drd_avl_count))
			return -EINVAL;

		/*
		 * There might be multiple calls hw_params -> prepare
		 * before start stream, so, should check buf size status
		 * to determine whether to re-create buf or do nothing.
		 */
		if (drd_rdy_list_found(dlp, dlp->drd_pb_shadow)) {
			if (buf_bytes == last_buf_bytes)
				return 0;

			drd_rdy_list_free(dlp);
		}

		/* release the old one, re-create for new params */
		drd_put(dlp->drd_pb_shadow);
		dlp->drd_pb_shadow = NULL;

		drd_new = drd_avl_list_get(dlp);
		if (!drd_new)
			return -ENOMEM;

		drd_init_from(drd_new, drd);

		ret = drd_buf_alloc(drd_new, buf_bytes);
		if (ret)
			return -ENOMEM;

		if (snd_BUG_ON(!drd_get(drd_new)))
			return -EINVAL;

		drd_rdy_list_add(dlp, drd_new);

		dlp->drd_pb_shadow = drd_new;
	} else {
		/*
		 * There might be multiple calls hw_params -> prepare
		 * before start stream, so, should check buf size status
		 * to determine whether to re-create buf or do nothing.
		 */
		if (drd->buf && buf_bytes == last_buf_bytes)
			return 0;

		drd_buf_free(drd);

		ret = drd_buf_alloc(drd, buf_bytes);
		if (ret)
			return ret;
	}

	/* update last after all done success */
	drd->last_buf_sz = drd->buf_sz;

	return 0;
}
EXPORT_SYMBOL_GPL(dlp_prepare);

int dlp_probe(struct snd_soc_component *component)
{
	snd_soc_add_component_controls(component, dlp_controls,
				       ARRAY_SIZE(dlp_controls));
	return 0;
}
EXPORT_SYMBOL_GPL(dlp_probe);

int dlp_register(struct dlp *dlp, struct device *dev,
		 const struct snd_soc_component_driver *driver,
		 const struct snd_dlp_config *config)
{
	struct dlp_runtime_data *drd;
	int ret = 0, i = 0;

	if (unlikely(!dlp || !dev || !driver || !config))
		return -EINVAL;

	dlp->dev = dev;
	dlp->config = config;

#ifdef CONFIG_DEBUG_FS
	dlp->component.debugfs_prefix = "dma";
#endif
	INIT_LIST_HEAD(&dlp->drd_avl_list);
	INIT_LIST_HEAD(&dlp->drd_rdy_list);
	INIT_LIST_HEAD(&dlp->drd_ref_list);

	dlp->drd_avl_count = ARRAY_SIZE(dlp->drds);

	for (i = 0; i < dlp->drd_avl_count; i++) {
		drd = &dlp->drds[i];
		list_add_tail(&drd->node, &dlp->drd_avl_list);
	}

	spin_lock_init(&dlp->lock);
	atomic_set(&dlp->active, 0);

	ret = snd_soc_component_initialize(&dlp->component, driver, dev);
	if (ret)
		return ret;

	ret = snd_soc_add_component(&dlp->component, NULL, 0);

	return ret;
}
EXPORT_SYMBOL_GPL(dlp_register);

MODULE_DESCRIPTION("Rockchip Digital Loopback Core Driver");
MODULE_AUTHOR("Sugar Zhang <sugar.zhang@rock-chips.com>");
MODULE_LICENSE("GPL");
