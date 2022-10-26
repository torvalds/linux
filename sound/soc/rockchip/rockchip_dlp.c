// SPDX-License-Identifier: GPL-2.0
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
#include <linux/slab.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <linux/dma-mapping.h>
#include <linux/of.h>

#include <sound/dmaengine_pcm.h>
#include "rockchip_dlp.h"

#ifdef DLP_DBG
#define dlp_info(args...)		pr_info(args)
#else
#define dlp_info(args...)		no_printk(args)
#endif

#define SND_DMAENGINE_DLP_DRV_NAME	"snd_dmaengine_dlp"
#define PBUF_CNT			2

static unsigned int prealloc_buffer_size_kbytes = 512;
module_param(prealloc_buffer_size_kbytes, uint, 0444);
MODULE_PARM_DESC(prealloc_buffer_size_kbytes, "Preallocate DMA buffer size (KB).");

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

enum dlp_mode {
	DLP_MODE_DISABLED,
	DLP_MODE_2CH_1LP_1MIC,		/* replace cap-ch-0   with play-ch-0 */
	DLP_MODE_2CH_1MIC_1LP,		/* replace cap-ch-1   with play-ch-1 */
	DLP_MODE_2CH_1MIC_1LP_MIX,	/* replace cap-ch-1   with play-ch-all-mix */
	DLP_MODE_2CH_2LP,		/* replace cap-ch-0~1 with play-ch-0~1 */
	DLP_MODE_4CH_2MIC_2LP,		/* replace cap-ch-2~3 with play-ch-0~1 */
	DLP_MODE_4CH_2MIC_1LP_MIX,	/* replace cap-ch-3   with play-ch-all-mix */
	DLP_MODE_4CH_4LP,		/* replace cap-ch-0~3 with play-ch-0~3 */
	DLP_MODE_6CH_4MIC_2LP,		/* replace cap-ch-4~5 with play-ch-0~1 */
	DLP_MODE_6CH_4MIC_1LP_MIX,	/* replace cap-ch-4   with play-ch-all-mix */
	DLP_MODE_6CH_6LP,		/* replace cap-ch-0~5 with play-ch-0~5 */
	DLP_MODE_8CH_6MIC_2LP,		/* replace cap-ch-6~7 with play-ch-0~1 */
	DLP_MODE_8CH_6MIC_1LP_MIX,	/* replace cap-ch-6   with play-ch-all-mix */
	DLP_MODE_8CH_8LP,		/* replace cap-ch-0~7 with play-ch-0~7 */
	DLP_MODE_10CH_8MIC_2LP,		/* replace cap-ch-8~9 with play-ch-0~1 */
	DLP_MODE_10CH_8MIC_1LP_MIX,	/* replace cap-ch-8   with play-ch-all-mix */
	DLP_MODE_16CH_8MIC_8LP,		/* replace cap-ch-8~f with play-ch-8~f */
};

struct dmaengine_dlp_runtime_data;
struct dmaengine_dlp {
	struct device *dev;
	struct dma_chan *chan[SNDRV_PCM_STREAM_LAST + 1];
	const struct snd_dlp_config *config;
	struct snd_soc_component component;
	struct list_head ref_list;
	enum dlp_mode mode;
	struct dmaengine_dlp_runtime_data *pref;
	spinlock_t lock;
	spinlock_t pref_lock;
};

struct dmaengine_dlp_runtime_data {
	struct dmaengine_dlp *parent;
	struct dmaengine_dlp_runtime_data *ref;
	struct dma_chan *dma_chan;
	struct kref refcount;
	struct list_head node;
	dma_cookie_t cookie;

	char *buf;
	snd_pcm_uframes_t buf_sz;
	snd_pcm_uframes_t period_sz;
	snd_pcm_uframes_t hw_ptr;
	snd_pcm_sframes_t hw_ptr_delta; /* play-ptr - cap-ptr */
	unsigned long period_elapsed;
	unsigned int frame_bytes;
	unsigned int channels;
	unsigned int buf_ofs;
	int stream;
};

static inline void dlp_activate(struct dmaengine_dlp *dlp)
{
	spin_lock(&dlp->lock);
	dlp->component.active++;
	spin_unlock(&dlp->lock);
}

static inline void dlp_deactivate(struct dmaengine_dlp *dlp)
{
	spin_lock(&dlp->lock);
	dlp->component.active--;
	spin_unlock(&dlp->lock);
}

static inline bool dlp_mode_channels_match(struct dmaengine_dlp *dlp,
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

static inline ssize_t dlp_channels_to_bytes(struct dmaengine_dlp_runtime_data *prtd,
					    int channels)
{
	return (prtd->frame_bytes / prtd->channels) * channels;
}

static inline ssize_t dlp_frames_to_bytes(struct dmaengine_dlp_runtime_data *prtd,
					  snd_pcm_sframes_t size)
{
	return size * prtd->frame_bytes;
}

static inline snd_pcm_sframes_t dlp_bytes_to_frames(struct dmaengine_dlp_runtime_data *prtd,
						    ssize_t size)
{
	return size / prtd->frame_bytes;
}

static inline struct dmaengine_dlp *soc_component_to_dlp(struct snd_soc_component *p)
{
	return container_of(p, struct dmaengine_dlp, component);
}

static inline struct dmaengine_dlp_runtime_data *substream_to_prtd(
	const struct snd_pcm_substream *substream)
{
	if (!substream->runtime)
		return NULL;

	return substream->runtime->private_data;
}

static struct dma_chan *snd_dmaengine_dlp_get_chan(struct snd_pcm_substream *substream)
{
	struct dmaengine_dlp_runtime_data *prtd = substream_to_prtd(substream);

	return prtd->dma_chan;
}

static struct device *dmaengine_dma_dev(struct dmaengine_dlp *dlp,
	struct snd_pcm_substream *substream)
{
	if (!dlp->chan[substream->stream])
		return NULL;

	return dlp->chan[substream->stream]->device->dev;
}

static int dlp_get_offset_size(struct dmaengine_dlp_runtime_data *prtd,
			       enum dlp_mode mode, int *ofs, int *size, bool *mix)
{
	bool is_playback = prtd->stream == SNDRV_PCM_STREAM_PLAYBACK;
	int ret = 0;

	switch (mode) {
	case DLP_MODE_2CH_1LP_1MIC:
		*ofs = 0;
		*size = dlp_channels_to_bytes(prtd, 1);
		break;
	case DLP_MODE_2CH_1MIC_1LP:
		*ofs = dlp_channels_to_bytes(prtd, 1);
		*size = dlp_channels_to_bytes(prtd, 1);
		break;
	case DLP_MODE_2CH_1MIC_1LP_MIX:
		if (is_playback) {
			*ofs = 0;
			*size = dlp_frames_to_bytes(prtd, 1);
			if (mix)
				*mix = true;
		} else {
			*ofs = dlp_channels_to_bytes(prtd, 1);
			*size = dlp_channels_to_bytes(prtd, 1);
		}
		break;
	case DLP_MODE_2CH_2LP:
		*ofs = 0;
		*size = dlp_channels_to_bytes(prtd, 2);
		break;
	case DLP_MODE_4CH_2MIC_2LP:
		if (is_playback) {
			*ofs = 0;
			*size = dlp_channels_to_bytes(prtd, 2);
		} else {
			*ofs = dlp_channels_to_bytes(prtd, 2);
			*size = dlp_channels_to_bytes(prtd, 2);
		}
		break;
	case DLP_MODE_4CH_2MIC_1LP_MIX:
		if (is_playback) {
			*ofs = 0;
			*size = dlp_frames_to_bytes(prtd, 1);
			if (mix)
				*mix = true;
		} else {
			*ofs = dlp_channels_to_bytes(prtd, 2);
			*size = dlp_channels_to_bytes(prtd, 1);
		}
		break;
	case DLP_MODE_4CH_4LP:
		*ofs = 0;
		*size = dlp_channels_to_bytes(prtd, 4);
		break;
	case DLP_MODE_6CH_4MIC_2LP:
		if (is_playback) {
			*ofs = 0;
			*size = dlp_channels_to_bytes(prtd, 2);
		} else {
			*ofs = dlp_channels_to_bytes(prtd, 4);
			*size = dlp_channels_to_bytes(prtd, 2);
		}
		break;
	case DLP_MODE_6CH_4MIC_1LP_MIX:
		if (is_playback) {
			*ofs = 0;
			*size = dlp_frames_to_bytes(prtd, 1);
			if (mix)
				*mix = true;
		} else {
			*ofs = dlp_channels_to_bytes(prtd, 4);
			*size = dlp_channels_to_bytes(prtd, 1);
		}
		break;
	case DLP_MODE_6CH_6LP:
		*ofs = 0;
		*size = dlp_channels_to_bytes(prtd, 6);
		break;
	case DLP_MODE_8CH_6MIC_2LP:
		if (is_playback) {
			*ofs = 0;
			*size = dlp_channels_to_bytes(prtd, 2);
		} else {
			*ofs = dlp_channels_to_bytes(prtd, 6);
			*size = dlp_channels_to_bytes(prtd, 2);
		}
		break;
	case DLP_MODE_8CH_6MIC_1LP_MIX:
		if (is_playback) {
			*ofs = 0;
			*size = dlp_frames_to_bytes(prtd, 1);
			if (mix)
				*mix = true;
		} else {
			*ofs = dlp_channels_to_bytes(prtd, 6);
			*size = dlp_channels_to_bytes(prtd, 1);
		}
		break;
	case DLP_MODE_8CH_8LP:
		*ofs = 0;
		*size = dlp_channels_to_bytes(prtd, 8);
		break;
	case DLP_MODE_10CH_8MIC_2LP:
		if (is_playback) {
			*ofs = 0;
			*size = dlp_channels_to_bytes(prtd, 2);
		} else {
			*ofs = dlp_channels_to_bytes(prtd, 8);
			*size = dlp_channels_to_bytes(prtd, 2);
		}
		break;
	case DLP_MODE_10CH_8MIC_1LP_MIX:
		if (is_playback) {
			*ofs = 0;
			*size = dlp_frames_to_bytes(prtd, 1);
			if (mix)
				*mix = true;
		} else {
			*ofs = dlp_channels_to_bytes(prtd, 8);
			*size = dlp_channels_to_bytes(prtd, 1);
		}
		break;
	case DLP_MODE_16CH_8MIC_8LP:
		if (is_playback) {
			*ofs = 0;
			*size = dlp_channels_to_bytes(prtd, 8);
		} else {
			*ofs = dlp_channels_to_bytes(prtd, 8);
			*size = dlp_channels_to_bytes(prtd, 8);
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

static int dlp_mix_frame_buffer(struct dmaengine_dlp_runtime_data *prtd, void *buf)
{
	int sample_bytes = dlp_channels_to_bytes(prtd, 1);
	int16_t *p16 = (int16_t *)buf, v16 = 0;
	int32_t *p32 = (int32_t *)buf, v32 = 0;
	int i = 0;

	switch (sample_bytes) {
	case 2:
		for (i = 0; i < prtd->channels; i++)
			v16 += (p16[i] / prtd->channels);
		p16[0] = v16;
		break;
	case 4:
		for (i = 0; i < prtd->channels; i++)
			v32 += (p32[i] / prtd->channels);
		p32[0] = v32;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int dmaengine_dlp_hw_params(struct snd_soc_component *component,
				   struct snd_pcm_substream *substream,
				   struct snd_pcm_hw_params *params)
{
	struct dmaengine_dlp *dlp = soc_component_to_dlp(component);
	struct dmaengine_dlp_runtime_data *prtd = substream_to_prtd(substream);
	struct dma_chan *chan = snd_dmaengine_dlp_get_chan(substream);
	struct dma_slave_config slave_config;
	bool is_playback = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	int ch_req = params_channels(params), ch_exp = 0;
	int ret;

	/* mode should match to channels */
	if (!is_playback && !dlp_mode_channels_match(dlp, ch_req, &ch_exp)) {
		dev_err(dlp->dev,
			"capture %d ch, expected: %d ch for loopback mode-%d\n",
			ch_req, ch_exp, dlp->mode);
		return -EINVAL;
	}

	memset(&slave_config, 0, sizeof(slave_config));

	ret = snd_dmaengine_pcm_prepare_slave_config(substream, params, &slave_config);
	if (ret)
		return ret;

	ret = dmaengine_slave_config(chan, &slave_config);
	if (ret)
		return ret;

	prtd->frame_bytes = snd_pcm_format_size(params_format(params),
						params_channels(params));
	prtd->period_sz = params_period_size(params);
	prtd->buf_sz = params_buffer_size(params);
	prtd->channels = params_channels(params);

	if (is_playback)
		prtd->buf_sz *= PBUF_CNT;

	return 0;
}

static int
dmaengine_pcm_set_runtime_hwparams(struct snd_soc_component *component,
				   struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct dmaengine_dlp *dlp = soc_component_to_dlp(component);
	struct device *dma_dev = dmaengine_dma_dev(dlp, substream);
	struct dma_chan *chan = dlp->chan[substream->stream];
	struct snd_dmaengine_dai_dma_data *dma_data;
	struct snd_pcm_hardware hw;

	if (rtd->num_cpus > 1) {
		dev_err(rtd->dev,
			"%s doesn't support Multi CPU yet\n", __func__);
		return -EINVAL;
	}

	dma_data = snd_soc_dai_get_dma_data(asoc_rtd_to_cpu(rtd, 0), substream);

	memset(&hw, 0, sizeof(hw));
	hw.info = SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID |
			SNDRV_PCM_INFO_INTERLEAVED;
	hw.periods_min = 2;
	hw.periods_max = UINT_MAX;
	hw.period_bytes_min = 256;
	hw.period_bytes_max = dma_get_max_seg_size(dma_dev);
	hw.buffer_bytes_max = SIZE_MAX;
	hw.fifo_size = dma_data->fifo_size;

	/**
	 * FIXME: Remove the return value check to align with the code
	 * before adding snd_dmaengine_pcm_refine_runtime_hwparams
	 * function.
	 */
	snd_dmaengine_pcm_refine_runtime_hwparams(substream,
						  dma_data,
						  &hw,
						  chan);

	return snd_soc_set_runtime_hwparams(substream, &hw);
}

static int dmaengine_dlp_open(struct snd_soc_component *component,
			      struct snd_pcm_substream *substream)
{
	struct dmaengine_dlp *dlp = soc_component_to_dlp(component);
	struct dma_chan *chan = dlp->chan[substream->stream];
	struct dmaengine_dlp_runtime_data *prtd;
	int ret;

	if (!chan)
		return -ENXIO;

	ret = dmaengine_pcm_set_runtime_hwparams(component, substream);
	if (ret)
		return ret;

	ret = snd_pcm_hw_constraint_integer(substream->runtime,
					    SNDRV_PCM_HW_PARAM_PERIODS);
	if (ret < 0)
		return ret;

	prtd = kzalloc(sizeof(*prtd), GFP_KERNEL);
	if (!prtd)
		return -ENOMEM;

	dlp_info("PRTD-CREATE: 0x%px (%s)\n",
		 prtd, substream->stream ? "C" : "P");

	kref_init(&prtd->refcount);
	prtd->parent = dlp;
	prtd->stream = substream->stream;
	prtd->dma_chan = chan;

	substream->runtime->private_data = prtd;

	dlp_activate(dlp);

	return 0;
}

static void dmaengine_free_prtd(struct kref *ref)
{
	struct dmaengine_dlp_runtime_data *prtd =
		container_of(ref, struct dmaengine_dlp_runtime_data, refcount);

	dlp_info("PRTD-FREE: 0x%px\n", prtd);

	kfree(prtd->buf);
	kfree(prtd);
}

static void free_ref_list(struct snd_soc_component *component)
{
	struct dmaengine_dlp *dlp = soc_component_to_dlp(component);
	struct dmaengine_dlp_runtime_data *prtd, *_pt;

	spin_lock(&dlp->lock);
	list_for_each_entry_safe(prtd, _pt, &dlp->ref_list, node) {
		list_del(&prtd->node);
		kref_put(&prtd->refcount, dmaengine_free_prtd);
	}
	spin_unlock(&dlp->lock);
}

static int dmaengine_dlp_close(struct snd_soc_component *component,
			       struct snd_pcm_substream *substream)
{
	struct dmaengine_dlp *dlp = soc_component_to_dlp(component);
	struct dmaengine_dlp_runtime_data *prtd = substream_to_prtd(substream);

	dmaengine_synchronize(prtd->dma_chan);

	/*
	 * kref put should be after hw_ptr updated when stop,
	 * ops->trigger: SNDRV_PCM_TRIGGER_STOP -> ops->close
	 * obviously, it is!
	 */
	kref_put(&prtd->refcount, dmaengine_free_prtd);

	dlp_deactivate(dlp);

	return 0;
}

static snd_pcm_uframes_t dmaengine_dlp_pointer(
	struct snd_soc_component *component,
	struct snd_pcm_substream *substream)
{
	struct dmaengine_dlp_runtime_data *prtd = substream_to_prtd(substream);
	struct dma_tx_state state;
	unsigned int buf_size;
	unsigned int pos = 0;

	dmaengine_tx_status(prtd->dma_chan, prtd->cookie, &state);
	buf_size = snd_pcm_lib_buffer_bytes(substream);
	if (state.residue > 0 && state.residue <= buf_size)
		pos = buf_size - state.residue;

	return dlp_bytes_to_frames(prtd, pos);
}

static void dmaengine_dlp_dma_complete(void *arg)
{
	struct snd_pcm_substream *substream = arg;
	struct dmaengine_dlp_runtime_data *prtd = substream_to_prtd(substream);
	struct dmaengine_dlp *dlp = prtd->parent;

	if (!substream->runtime)
		return;

	spin_lock(&dlp->lock);
	prtd->period_elapsed++;
	prtd->hw_ptr = prtd->period_elapsed * prtd->period_sz;
	spin_unlock(&dlp->lock);
	snd_pcm_period_elapsed(substream);
}

static int dmaengine_dlp_prepare_and_submit(struct snd_pcm_substream *substream)
{
	struct dmaengine_dlp_runtime_data *prtd = substream_to_prtd(substream);
	struct dma_chan *chan = prtd->dma_chan;
	struct dma_async_tx_descriptor *desc;
	enum dma_transfer_direction direction;
	unsigned long flags = DMA_CTRL_ACK;

	direction = snd_pcm_substream_to_dma_direction(substream);

	if (!substream->runtime->no_period_wakeup)
		flags |= DMA_PREP_INTERRUPT;

	desc = dmaengine_prep_dma_cyclic(chan,
		substream->runtime->dma_addr,
		snd_pcm_lib_buffer_bytes(substream),
		snd_pcm_lib_period_bytes(substream), direction, flags);

	if (!desc)
		return -ENOMEM;

	desc->callback = dmaengine_dlp_dma_complete;
	desc->callback_param = substream;
	prtd->cookie = dmaengine_submit(desc);

	return 0;
}

static int dmaengine_dlp_setup(struct snd_soc_component *component,
			       struct snd_pcm_substream *substream)
{
	struct dmaengine_dlp *dlp = soc_component_to_dlp(component);
	int bstream = SNDRV_PCM_STREAM_LAST - substream->stream;
	struct snd_pcm_str *bro = &substream->pcm->streams[bstream];
	struct snd_pcm_substream *bsubstream = bro->substream;
	struct dmaengine_dlp_runtime_data *prtd = substream_to_prtd(substream);
	struct dmaengine_dlp_runtime_data *brtd = substream_to_prtd(bsubstream);
	struct dmaengine_dlp_runtime_data *pref = dlp->pref;
	bool is_playback = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	snd_pcm_uframes_t a = 0, b = 0, fifo_a = 0, fifo_b = 0;
	snd_pcm_sframes_t delta = 0;

	if (dlp->mode == DLP_MODE_DISABLED)
		return -EINVAL;

	fifo_a = dlp->config->get_fifo_count(dlp->dev, substream->stream);
	a = dmaengine_dlp_pointer(component, substream);

	if (bsubstream->runtime && snd_pcm_running(bsubstream)) {
		fifo_b = dlp->config->get_fifo_count(dlp->dev, bstream);
		b = dmaengine_dlp_pointer(component, bsubstream);

		spin_lock(&dlp->lock);
		if (!pref) {
			spin_unlock(&dlp->lock);
			return -EINVAL;
		}

		a = (prtd->period_elapsed * prtd->period_sz) + (a % prtd->period_sz);
		b = (brtd->period_elapsed * brtd->period_sz) + (b % brtd->period_sz);

		fifo_a = dlp_bytes_to_frames(prtd, fifo_a * 4);
		fifo_b = dlp_bytes_to_frames(brtd, fifo_b * 4);

		delta = is_playback ? (a - fifo_a) - (b + fifo_b) : (b - fifo_b) - (a + fifo_a);

		pref->hw_ptr_delta = delta;
		kref_get(&pref->refcount);
		/* push valid playback into ref list */
		list_add_tail(&pref->node, &dlp->ref_list);

		spin_unlock(&dlp->lock);
	}

	if (is_playback)
		dlp_info("START-P: DMA-P: %lu, DMA-C: %lu, FIFO-P: %lu, FIFO-C: %lu, DELTA: %ld\n",
			 a, b, fifo_a, fifo_b, delta);
	else
		dlp_info("START-C: DMA-P: %lu, DMA-C: %lu, FIFO-P: %lu, FIFO-C: %lu, DELTA: %ld\n",
			 b, a, fifo_b, fifo_a, delta);

	return 0;
}

static void dmaengine_dlp_release(struct snd_soc_component *component,
				  struct snd_pcm_substream *substream)
{
	struct dmaengine_dlp *dlp = soc_component_to_dlp(component);
	struct dmaengine_dlp_runtime_data *prtd = substream_to_prtd(substream);
	struct dmaengine_dlp_runtime_data *pref = dlp->pref;
	struct snd_pcm_runtime *runtime = substream->runtime;
	snd_pcm_uframes_t appl_ptr, hw_ptr;

	if (dlp->mode == DLP_MODE_DISABLED)
		return;

	/* any data in FIFOs will be gone ,so don't care */
	appl_ptr = READ_ONCE(runtime->control->appl_ptr);
	hw_ptr = dmaengine_dlp_pointer(component, substream);
	spin_lock(&dlp->lock);
	hw_ptr = (prtd->period_elapsed * prtd->period_sz) + (hw_ptr % prtd->period_sz);
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		pref->hw_ptr = min(hw_ptr, appl_ptr);
	prtd->period_elapsed = 0;
	prtd->hw_ptr = 0;
	spin_unlock(&dlp->lock);

	/*
	 * playback:
	 *
	 * snd_pcm_drop:  hw_ptr will be smaller than appl_ptr
	 * snd_pcm_drain, hw_ptr will be equal to appl_ptr
	 *
	 * anyway, we should use the smaller one, obviously, it's hw_ptr.
	 */
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		spin_lock(&dlp->pref_lock);
		kref_put(&pref->refcount, dmaengine_free_prtd);
		dlp->pref = NULL;
		spin_unlock(&dlp->pref_lock);
		dlp_info("STOP-P: applptr: %lu, hwptr: %lu\n", appl_ptr, hw_ptr);
	} else {
		/* free residue playback ref list for capture when stop */
		free_ref_list(component);
		dlp_info("STOP-C: applptr: %lu, hwptr: %lu\n", appl_ptr, hw_ptr);
	}
}

static int dmaengine_dlp_trigger(struct snd_soc_component *component,
				 struct snd_pcm_substream *substream, int cmd)
{
	struct dmaengine_dlp_runtime_data *prtd = substream_to_prtd(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	int ret;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		ret = dmaengine_dlp_prepare_and_submit(substream);
		if (ret)
			return ret;
		dma_async_issue_pending(prtd->dma_chan);
		dmaengine_dlp_setup(component, substream);
		break;
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		dmaengine_resume(prtd->dma_chan);
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
		if (runtime->info & SNDRV_PCM_INFO_PAUSE) {
			dmaengine_pause(prtd->dma_chan);
		} else {
			dmaengine_dlp_release(component, substream);
			dmaengine_terminate_async(prtd->dma_chan);
		}
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		dmaengine_pause(prtd->dma_chan);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		dmaengine_dlp_release(component, substream);
		dmaengine_terminate_async(prtd->dma_chan);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int dmaengine_dlp_new(struct snd_soc_component *component,
			     struct snd_soc_pcm_runtime *rtd)
{
	struct dmaengine_dlp *dlp = soc_component_to_dlp(component);
	struct snd_pcm_substream *substream;
	size_t prealloc_buffer_size;
	size_t max_buffer_size;
	unsigned int i;

	prealloc_buffer_size = prealloc_buffer_size_kbytes * 1024;
	max_buffer_size = SIZE_MAX;

	for_each_pcm_streams(i) {
		substream = rtd->pcm->streams[i].substream;
		if (!substream)
			continue;

		if (!dlp->chan[i]) {
			dev_err(component->dev,
				"Missing dma channel for stream: %d\n", i);
			return -EINVAL;
		}

		snd_pcm_set_managed_buffer(substream,
				SNDRV_DMA_TYPE_DEV_IRAM,
				dmaengine_dma_dev(dlp, substream),
				prealloc_buffer_size,
				max_buffer_size);

		if (rtd->pcm->streams[i].pcm->name[0] == '\0') {
			strscpy_pad(rtd->pcm->streams[i].pcm->name,
				    rtd->pcm->streams[i].pcm->id,
				    sizeof(rtd->pcm->streams[i].pcm->name));
		}
	}

	return 0;
}

static struct dmaengine_dlp_runtime_data *get_ref(struct snd_soc_component *component)
{
	struct dmaengine_dlp *dlp = soc_component_to_dlp(component);
	struct dmaengine_dlp_runtime_data *pref = NULL;

	spin_lock(&dlp->lock);
	if (!list_empty(&dlp->ref_list)) {
		pref = list_first_entry(&dlp->ref_list, struct dmaengine_dlp_runtime_data, node);
		list_del(&pref->node);
	}
	spin_unlock(&dlp->lock);

	return pref;
}

static int process_capture(struct snd_soc_component *component,
			   struct snd_pcm_substream *substream,
			   unsigned long hwoff,
			   void __user *buf, unsigned long bytes)
{
	struct dmaengine_dlp *dlp = soc_component_to_dlp(component);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct dmaengine_dlp_runtime_data *prtd = substream_to_prtd(substream);
	struct dmaengine_dlp_runtime_data *pref = NULL;
	void *dma_ptr = runtime->dma_area + hwoff;
	snd_pcm_sframes_t frames = dlp_bytes_to_frames(prtd, bytes);
	snd_pcm_sframes_t frames_consumed = 0, frames_residue = 0, frames_tmp = 0;
	snd_pcm_sframes_t ofs = 0;
	snd_pcm_uframes_t appl_ptr;
	char *cbuf = prtd->buf, *pbuf = NULL;
	int ofs_cap, ofs_play, size_cap, size_play;
	int i = 0, j = 0, ret = 0;
	bool free_ref = false, mix = false;

	appl_ptr = READ_ONCE(runtime->control->appl_ptr);

	memcpy(cbuf, dma_ptr, bytes);
#ifdef DLP_DBG
	/* DBG: mark STUB in ch-REC for trace each read */
	memset(cbuf, 0x22, dlp_channels_to_bytes(prtd, 1));
#endif
	ret = dlp_get_offset_size(prtd, dlp->mode, &ofs_cap, &size_cap, NULL);
	if (ret) {
		dlp_info("fail to get dlp cap offset\n");
		return -EINVAL;
	}

	/* clear channel-LP_CHN */
	for (i = 0; i < frames; i++) {
		cbuf = prtd->buf + dlp_frames_to_bytes(prtd, i) + ofs_cap;
		memset(cbuf, 0x0, size_cap);
	}

start:
	if (!prtd->ref)
		prtd->ref = get_ref(component);
	pref = prtd->ref;

	/* do nothing if play stop */
	if (!pref)
		return 0;

	ret = dlp_get_offset_size(pref, dlp->mode, &ofs_play, &size_play, &mix);
	if (ret) {
		dlp_info("fail to get dlp play offset\n");
		return 0;
	}

	ofs = appl_ptr + pref->hw_ptr_delta;

	/*
	 * if playback stop, kref_put ref, and we can check this to
	 * know if playback stopped, then free prtd->ref if data consumed.
	 *
	 */
	if (kref_read(&pref->refcount) == 1) {
		if (ofs >= pref->hw_ptr) {
			kref_put(&pref->refcount, dmaengine_free_prtd);
			prtd->ref = NULL;
			return 0;
		} else if ((ofs + frames) > pref->hw_ptr) {
			dlp_info("applptr: %8lu, ofs': %7ld, refhwptr: %lu, frames: %lu (*)\n",
				 appl_ptr, ofs, pref->hw_ptr, frames);
			/*
			 * should ignore the data that after play stop
			 * and care about if the next ref start in the
			 * same window
			 */
			frames_tmp = pref->hw_ptr - ofs;
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
		return 0;

	/* skip if ofs < 0 and fixup ofs */
	j = 0;
	if (ofs < 0) {
		dlp_info("applptr: %8lu, ofs: %8ld, frames: %lu (*)\n",
			 appl_ptr, ofs, frames);
		j = -ofs;
		frames += ofs;
		ofs = 0;
	}

	ofs %= pref->buf_sz;

	dlp_info("applptr: %8lu, ofs: %8ld, frames: %lu\n", appl_ptr, ofs, frames);

	for (i = 0; i < frames; i++, j++) {
		cbuf = prtd->buf + dlp_frames_to_bytes(prtd, j + frames_consumed) + ofs_cap;
		pbuf = pref->buf + dlp_frames_to_bytes(pref, ((i + ofs) % pref->buf_sz)) + ofs_play;
		if (mix)
			dlp_mix_frame_buffer(pref, pbuf);
		memcpy(cbuf, pbuf, size_cap);
	}

	appl_ptr += frames;
	frames_consumed += frames;

	if (free_ref) {
		kref_put(&pref->refcount, dmaengine_free_prtd);
		prtd->ref = NULL;
		free_ref = false;
		if (frames_residue) {
			frames = frames_residue;
			frames_residue = 0;
			goto start;
		}
	}

	return 0;
}

static int process_playback(struct snd_soc_component *component,
			    struct snd_pcm_substream *substream,
			    unsigned long hwoff,
			    void __user *buf, unsigned long bytes)
{
	struct dmaengine_dlp *dlp = soc_component_to_dlp(component);
	struct dmaengine_dlp_runtime_data *pref;
	char *pbuf;
	int ret = 0;

	spin_lock(&dlp->pref_lock);
	pref = dlp->pref;
	if (!pref) {
		ret = -EFAULT;
		goto err_unlock;
	}

	pbuf = pref->buf + pref->buf_ofs;

	if (copy_from_user(pbuf, buf, bytes)) {
		ret = -EFAULT;
		goto err_unlock;
	}

	pref->buf_ofs += bytes;
	pref->buf_ofs %= dlp_frames_to_bytes(pref, pref->buf_sz);

err_unlock:
	spin_unlock(&dlp->pref_lock);

	return ret;
}

static int dmaengine_dlp_process(struct snd_soc_component *component,
				 struct snd_pcm_substream *substream,
				 unsigned long hwoff,
				 void __user *buf, unsigned long bytes)
{
	struct dmaengine_dlp *dlp = soc_component_to_dlp(component);
	int ret = 0;

	if (dlp->mode == DLP_MODE_DISABLED)
		return -EINVAL;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		ret = process_playback(component, substream, hwoff, buf, bytes);
	else
		ret = process_capture(component, substream, hwoff, buf, bytes);

	return ret;
}

static int dmaengine_dlp_copy_user(struct snd_soc_component *component,
				   struct snd_pcm_substream *substream,
				   int channel, unsigned long hwoff,
				   void __user *buf, unsigned long bytes)
{
	struct dmaengine_dlp_runtime_data *prtd = substream_to_prtd(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	bool is_playback = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	void *dma_ptr = runtime->dma_area + hwoff +
			channel * (runtime->dma_bytes / runtime->channels);
	int ret;

	if (is_playback)
		if (copy_from_user(dma_ptr, buf, bytes))
			return -EFAULT;

	ret = dmaengine_dlp_process(component, substream, hwoff, buf, bytes);
	if (!ret)
		dma_ptr = prtd->buf;

	if (!is_playback)
		if (copy_to_user(buf, dma_ptr, bytes))
			return -EFAULT;

	return 0;
}

static SOC_ENUM_SINGLE_EXT_DECL(dlp_mode, dlp_text);

static int dmaengine_dlp_mode_get(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct dmaengine_dlp *dlp = soc_component_to_dlp(component);

	ucontrol->value.enumerated.item[0] = dlp->mode;

	return 0;
}

static int dmaengine_dlp_mode_put(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct dmaengine_dlp *dlp = soc_component_to_dlp(component);
	unsigned int mode = ucontrol->value.enumerated.item[0];

	/* MUST: do not update mode while stream is running */
	if (snd_soc_component_active(component))
		return -EPERM;

	if (mode == dlp->mode)
		return 0;

	dlp->mode = mode;

	return 1;
}

static const struct snd_kcontrol_new dmaengine_dlp_controls[] = {
	SOC_ENUM_EXT("Software Digital Loopback Mode", dlp_mode,
		     dmaengine_dlp_mode_get,
		     dmaengine_dlp_mode_put),
};

static int dmaengine_dlp_prepare(struct snd_soc_component *component,
				  struct snd_pcm_substream *substream)
{
	struct dmaengine_dlp *dlp = soc_component_to_dlp(component);
	struct dmaengine_dlp_runtime_data *prtd = substream_to_prtd(substream);
	struct dmaengine_dlp_runtime_data *pref = NULL;
	int buf_bytes = dlp_frames_to_bytes(prtd, prtd->buf_sz);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		pref = kmemdup(prtd, sizeof(*prtd), GFP_KERNEL);
		if (!pref)
			return -ENOMEM;

		kref_init(&pref->refcount);
		pref->buf_ofs = 0;
		pref->buf = kzalloc(buf_bytes, GFP_KERNEL);
		if (!pref->buf) {
			kfree(pref);
			return -ENOMEM;
		}

		spin_lock(&dlp->pref_lock);
		dlp->pref = pref;
		spin_unlock(&dlp->pref_lock);
		dlp_info("PREF-CREATE: 0x%px\n", pref);
	} else {
		prtd->buf = kzalloc(buf_bytes, GFP_KERNEL);
		if (!prtd->buf)
			return -ENOMEM;
	}

	return 0;
}
static const struct snd_soc_component_driver dmaengine_dlp_component = {
	.name		= SND_DMAENGINE_DLP_DRV_NAME,
	.probe_order	= SND_SOC_COMP_ORDER_LATE,
	.open		= dmaengine_dlp_open,
	.close		= dmaengine_dlp_close,
	.hw_params	= dmaengine_dlp_hw_params,
	.prepare	= dmaengine_dlp_prepare,
	.trigger	= dmaengine_dlp_trigger,
	.pointer	= dmaengine_dlp_pointer,
	.copy_user	= dmaengine_dlp_copy_user,
	.pcm_construct	= dmaengine_dlp_new,
	.controls	= dmaengine_dlp_controls,
	.num_controls	= ARRAY_SIZE(dmaengine_dlp_controls),
};

static const char * const dmaengine_pcm_dma_channel_names[] = {
	[SNDRV_PCM_STREAM_PLAYBACK] = "tx",
	[SNDRV_PCM_STREAM_CAPTURE] = "rx",
};

static int dmaengine_pcm_request_chan_of(struct dmaengine_dlp *dlp,
	struct device *dev, const struct snd_dmaengine_pcm_config *config)
{
	unsigned int i;
	const char *name;
	struct dma_chan *chan;

	for_each_pcm_streams(i) {
		name = dmaengine_pcm_dma_channel_names[i];
		chan = dma_request_chan(dev, name);
		if (IS_ERR(chan)) {
			/*
			 * Only report probe deferral errors, channels
			 * might not be present for devices that
			 * support only TX or only RX.
			 */
			if (PTR_ERR(chan) == -EPROBE_DEFER)
				return -EPROBE_DEFER;
			dlp->chan[i] = NULL;
		} else {
			dlp->chan[i] = chan;
		}
	}

	return 0;
}

static void dmaengine_pcm_release_chan(struct dmaengine_dlp *dlp)
{
	unsigned int i;

	for_each_pcm_streams(i) {
		if (!dlp->chan[i])
			continue;
		dma_release_channel(dlp->chan[i]);
	}
}

/**
 * snd_dmaengine_dlp_register - Register a dmaengine based DLP device
 * @dev: The parent device for the DLP device
 * @config: Platform specific DLP configuration
 */
static int snd_dmaengine_dlp_register(struct device *dev,
	const struct snd_dlp_config *config)
{
	const struct snd_soc_component_driver *driver;
	struct dmaengine_dlp *dlp;
	int ret;

	dlp = kzalloc(sizeof(*dlp), GFP_KERNEL);
	if (!dlp)
		return -ENOMEM;

	dlp->dev = dev;
	dlp->config = config;

	INIT_LIST_HEAD(&dlp->ref_list);
	spin_lock_init(&dlp->lock);
	spin_lock_init(&dlp->pref_lock);

#ifdef CONFIG_DEBUG_FS
	dlp->component.debugfs_prefix = "dma";
#endif
	ret = dmaengine_pcm_request_chan_of(dlp, dev, NULL);
	if (ret)
		goto err_free_dma;

	driver = &dmaengine_dlp_component;

	ret = snd_soc_component_initialize(&dlp->component, driver, dev);
	if (ret)
		goto err_free_dma;

	ret = snd_soc_add_component(&dlp->component, NULL, 0);
	if (ret)
		goto err_free_dma;

	return 0;

err_free_dma:
	dmaengine_pcm_release_chan(dlp);
	kfree(dlp);
	return ret;
}

/**
 * snd_dmaengine_dlp_unregister - Removes a dmaengine based DLP device
 * @dev: Parent device the DLP was register with
 *
 * Removes a dmaengine based DLP device previously registered with
 * snd_dmaengine_dlp_register.
 */
static void snd_dmaengine_dlp_unregister(struct device *dev)
{
	struct snd_soc_component *component;
	struct dmaengine_dlp *dlp;

	component = snd_soc_lookup_component(dev, SND_DMAENGINE_DLP_DRV_NAME);
	if (!component)
		return;

	dlp = soc_component_to_dlp(component);

	snd_soc_unregister_component_by_driver(dev, component->driver);
	dmaengine_pcm_release_chan(dlp);
	kfree(dlp);
}

static void devm_dmaengine_dlp_release(struct device *dev, void *res)
{
	snd_dmaengine_dlp_unregister(*(struct device **)res);
}

/**
 * devm_snd_dmaengine_dlp_register - resource managed dmaengine DLP registration
 * @dev: The parent device for the DLP device
 * @config: Platform specific DLP configuration
 *
 * Register a dmaengine based DLP device with automatic unregistration when the
 * device is unregistered.
 */
int devm_snd_dmaengine_dlp_register(struct device *dev,
	const struct snd_dlp_config *config)
{
	struct device **ptr;
	int ret;

	ptr = devres_alloc(devm_dmaengine_dlp_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;

	ret = snd_dmaengine_dlp_register(dev, config);
	if (ret == 0) {
		*ptr = dev;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(devm_snd_dmaengine_dlp_register);

MODULE_LICENSE("GPL");
