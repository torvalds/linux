/*
 *  Rate conversion Plug-In
 *  Copyright (c) 1999 by Jaroslav Kysela <perex@perex.cz>
 *
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Library General Public License as
 *   published by the Free Software Foundation; either version 2 of
 *   the License, or (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU Library General Public License for more details.
 *
 *   You should have received a copy of the GNU Library General Public
 *   License along with this library; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */
  
#include <linux/time.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include "pcm_plugin.h"

#define SHIFT	11
#define BITS	(1<<SHIFT)
#define R_MASK	(BITS-1)

/*
 *  Basic rate conversion plugin
 */

struct rate_channel {
	signed short last_S1;
	signed short last_S2;
};
 
typedef void (*rate_f)(struct snd_pcm_plugin *plugin,
		       const struct snd_pcm_plugin_channel *src_channels,
		       struct snd_pcm_plugin_channel *dst_channels,
		       int src_frames, int dst_frames);

struct rate_priv {
	unsigned int pitch;
	unsigned int pos;
	rate_f func;
	snd_pcm_sframes_t old_src_frames, old_dst_frames;
	struct rate_channel channels[0];
};

static void rate_init(struct snd_pcm_plugin *plugin)
{
	unsigned int channel;
	struct rate_priv *data = (struct rate_priv *)plugin->extra_data;
	data->pos = 0;
	for (channel = 0; channel < plugin->src_format.channels; channel++) {
		data->channels[channel].last_S1 = 0;
		data->channels[channel].last_S2 = 0;
	}
}

static void resample_expand(struct snd_pcm_plugin *plugin,
			    const struct snd_pcm_plugin_channel *src_channels,
			    struct snd_pcm_plugin_channel *dst_channels,
			    int src_frames, int dst_frames)
{
	unsigned int pos = 0;
	signed int val;
	signed short S1, S2;
	signed short *src, *dst;
	unsigned int channel;
	int src_step, dst_step;
	int src_frames1, dst_frames1;
	struct rate_priv *data = (struct rate_priv *)plugin->extra_data;
	struct rate_channel *rchannels = data->channels;
	
	for (channel = 0; channel < plugin->src_format.channels; channel++) {
		pos = data->pos;
		S1 = rchannels->last_S1;
		S2 = rchannels->last_S2;
		if (!src_channels[channel].enabled) {
			if (dst_channels[channel].wanted)
				snd_pcm_area_silence(&dst_channels[channel].area, 0, dst_frames, plugin->dst_format.format);
			dst_channels[channel].enabled = 0;
			continue;
		}
		dst_channels[channel].enabled = 1;
		src = (signed short *)src_channels[channel].area.addr +
			src_channels[channel].area.first / 8 / 2;
		dst = (signed short *)dst_channels[channel].area.addr +
			dst_channels[channel].area.first / 8 / 2;
		src_step = src_channels[channel].area.step / 8 / 2;
		dst_step = dst_channels[channel].area.step / 8 / 2;
		src_frames1 = src_frames;
		dst_frames1 = dst_frames;
		while (dst_frames1-- > 0) {
			if (pos & ~R_MASK) {
				pos &= R_MASK;
				S1 = S2;
				if (src_frames1-- > 0) {
					S2 = *src;
					src += src_step;
				}
			}
			val = S1 + ((S2 - S1) * (signed int)pos) / BITS;
			if (val < -32768)
				val = -32768;
			else if (val > 32767)
				val = 32767;
			*dst = val;
			dst += dst_step;
			pos += data->pitch;
		}
		rchannels->last_S1 = S1;
		rchannels->last_S2 = S2;
		rchannels++;
	}
	data->pos = pos;
}

static void resample_shrink(struct snd_pcm_plugin *plugin,
			    const struct snd_pcm_plugin_channel *src_channels,
			    struct snd_pcm_plugin_channel *dst_channels,
			    int src_frames, int dst_frames)
{
	unsigned int pos = 0;
	signed int val;
	signed short S1, S2;
	signed short *src, *dst;
	unsigned int channel;
	int src_step, dst_step;
	int src_frames1, dst_frames1;
	struct rate_priv *data = (struct rate_priv *)plugin->extra_data;
	struct rate_channel *rchannels = data->channels;

	for (channel = 0; channel < plugin->src_format.channels; ++channel) {
		pos = data->pos;
		S1 = rchannels->last_S1;
		S2 = rchannels->last_S2;
		if (!src_channels[channel].enabled) {
			if (dst_channels[channel].wanted)
				snd_pcm_area_silence(&dst_channels[channel].area, 0, dst_frames, plugin->dst_format.format);
			dst_channels[channel].enabled = 0;
			continue;
		}
		dst_channels[channel].enabled = 1;
		src = (signed short *)src_channels[channel].area.addr +
			src_channels[channel].area.first / 8 / 2;
		dst = (signed short *)dst_channels[channel].area.addr +
			dst_channels[channel].area.first / 8 / 2;
		src_step = src_channels[channel].area.step / 8 / 2;
		dst_step = dst_channels[channel].area.step / 8 / 2;
		src_frames1 = src_frames;
		dst_frames1 = dst_frames;
		while (dst_frames1 > 0) {
			S1 = S2;
			if (src_frames1-- > 0) {
				S2 = *src;
				src += src_step;
			}
			if (pos & ~R_MASK) {
				pos &= R_MASK;
				val = S1 + ((S2 - S1) * (signed int)pos) / BITS;
				if (val < -32768)
					val = -32768;
				else if (val > 32767)
					val = 32767;
				*dst = val;
				dst += dst_step;
				dst_frames1--;
			}
			pos += data->pitch;
		}
		rchannels->last_S1 = S1;
		rchannels->last_S2 = S2;
		rchannels++;
	}
	data->pos = pos;
}

static snd_pcm_sframes_t rate_src_frames(struct snd_pcm_plugin *plugin, snd_pcm_uframes_t frames)
{
	struct rate_priv *data;
	snd_pcm_sframes_t res;

	if (snd_BUG_ON(!plugin))
		return -ENXIO;
	if (frames == 0)
		return 0;
	data = (struct rate_priv *)plugin->extra_data;
	if (plugin->src_format.rate < plugin->dst_format.rate) {
		res = (((frames * data->pitch) + (BITS/2)) >> SHIFT);
	} else {
		res = (((frames << SHIFT) + (data->pitch / 2)) / data->pitch);		
	}
	if (data->old_src_frames > 0) {
		snd_pcm_sframes_t frames1 = frames, res1 = data->old_dst_frames;
		while (data->old_src_frames < frames1) {
			frames1 >>= 1;
			res1 <<= 1;
		}
		while (data->old_src_frames > frames1) {
			frames1 <<= 1;
			res1 >>= 1;
		}
		if (data->old_src_frames == frames1)
			return res1;
	}
	data->old_src_frames = frames;
	data->old_dst_frames = res;
	return res;
}

static snd_pcm_sframes_t rate_dst_frames(struct snd_pcm_plugin *plugin, snd_pcm_uframes_t frames)
{
	struct rate_priv *data;
	snd_pcm_sframes_t res;

	if (snd_BUG_ON(!plugin))
		return -ENXIO;
	if (frames == 0)
		return 0;
	data = (struct rate_priv *)plugin->extra_data;
	if (plugin->src_format.rate < plugin->dst_format.rate) {
		res = (((frames << SHIFT) + (data->pitch / 2)) / data->pitch);
	} else {
		res = (((frames * data->pitch) + (BITS/2)) >> SHIFT);
	}
	if (data->old_dst_frames > 0) {
		snd_pcm_sframes_t frames1 = frames, res1 = data->old_src_frames;
		while (data->old_dst_frames < frames1) {
			frames1 >>= 1;
			res1 <<= 1;
		}
		while (data->old_dst_frames > frames1) {
			frames1 <<= 1;
			res1 >>= 1;
		}
		if (data->old_dst_frames == frames1)
			return res1;
	}
	data->old_dst_frames = frames;
	data->old_src_frames = res;
	return res;
}

static snd_pcm_sframes_t rate_transfer(struct snd_pcm_plugin *plugin,
			     const struct snd_pcm_plugin_channel *src_channels,
			     struct snd_pcm_plugin_channel *dst_channels,
			     snd_pcm_uframes_t frames)
{
	snd_pcm_uframes_t dst_frames;
	struct rate_priv *data;

	if (snd_BUG_ON(!plugin || !src_channels || !dst_channels))
		return -ENXIO;
	if (frames == 0)
		return 0;
#ifdef CONFIG_SND_DEBUG
	{
		unsigned int channel;
		for (channel = 0; channel < plugin->src_format.channels; channel++) {
			if (snd_BUG_ON(src_channels[channel].area.first % 8 ||
				       src_channels[channel].area.step % 8))
				return -ENXIO;
			if (snd_BUG_ON(dst_channels[channel].area.first % 8 ||
				       dst_channels[channel].area.step % 8))
				return -ENXIO;
		}
	}
#endif

	dst_frames = rate_dst_frames(plugin, frames);
	if (dst_frames > dst_channels[0].frames)
		dst_frames = dst_channels[0].frames;
	data = (struct rate_priv *)plugin->extra_data;
	data->func(plugin, src_channels, dst_channels, frames, dst_frames);
	return dst_frames;
}

static int rate_action(struct snd_pcm_plugin *plugin,
		       enum snd_pcm_plugin_action action,
		       unsigned long udata)
{
	if (snd_BUG_ON(!plugin))
		return -ENXIO;
	switch (action) {
	case INIT:
	case PREPARE:
		rate_init(plugin);
		break;
	default:
		break;
	}
	return 0;	/* silenty ignore other actions */
}

int snd_pcm_plugin_build_rate(struct snd_pcm_substream *plug,
			      struct snd_pcm_plugin_format *src_format,
			      struct snd_pcm_plugin_format *dst_format,
			      struct snd_pcm_plugin **r_plugin)
{
	int err;
	struct rate_priv *data;
	struct snd_pcm_plugin *plugin;

	if (snd_BUG_ON(!r_plugin))
		return -ENXIO;
	*r_plugin = NULL;

	if (snd_BUG_ON(src_format->channels != dst_format->channels))
		return -ENXIO;
	if (snd_BUG_ON(src_format->channels <= 0))
		return -ENXIO;
	if (snd_BUG_ON(src_format->format != SNDRV_PCM_FORMAT_S16))
		return -ENXIO;
	if (snd_BUG_ON(dst_format->format != SNDRV_PCM_FORMAT_S16))
		return -ENXIO;
	if (snd_BUG_ON(src_format->rate == dst_format->rate))
		return -ENXIO;

	err = snd_pcm_plugin_build(plug, "rate conversion",
				   src_format, dst_format,
				   sizeof(struct rate_priv) +
				   src_format->channels * sizeof(struct rate_channel),
				   &plugin);
	if (err < 0)
		return err;
	data = (struct rate_priv *)plugin->extra_data;
	if (src_format->rate < dst_format->rate) {
		data->pitch = ((src_format->rate << SHIFT) + (dst_format->rate >> 1)) / dst_format->rate;
		data->func = resample_expand;
	} else {
		data->pitch = ((dst_format->rate << SHIFT) + (src_format->rate >> 1)) / src_format->rate;
		data->func = resample_shrink;
	}
	data->pos = 0;
	rate_init(plugin);
	data->old_src_frames = data->old_dst_frames = 0;
	plugin->transfer = rate_transfer;
	plugin->src_frames = rate_src_frames;
	plugin->dst_frames = rate_dst_frames;
	plugin->action = rate_action;
	*r_plugin = plugin;
	return 0;
}
