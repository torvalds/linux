// SPDX-License-Identifier: LGPL-2.0+
/*
 *  Route Plug-In
 *  Copyright (c) 2000 by Abramo Bagnara <abramo@alsa-project.org>
 */

#include <linux/time.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include "pcm_plugin.h"

static void zero_areas(struct snd_pcm_plugin_channel *dvp, int ndsts,
		       snd_pcm_uframes_t frames, snd_pcm_format_t format)
{
	int dst = 0;
	for (; dst < ndsts; ++dst) {
		if (dvp->wanted)
			snd_pcm_area_silence(&dvp->area, 0, frames, format);
		dvp->enabled = 0;
		dvp++;
	}
}

static inline void copy_area(const struct snd_pcm_plugin_channel *src_channel,
			     struct snd_pcm_plugin_channel *dst_channel,
			     snd_pcm_uframes_t frames, snd_pcm_format_t format)
{
	dst_channel->enabled = 1;
	snd_pcm_area_copy(&src_channel->area, 0, &dst_channel->area, 0, frames, format);
}

static snd_pcm_sframes_t route_transfer(struct snd_pcm_plugin *plugin,
					const struct snd_pcm_plugin_channel *src_channels,
					struct snd_pcm_plugin_channel *dst_channels,
					snd_pcm_uframes_t frames)
{
	int nsrcs, ndsts, dst;
	struct snd_pcm_plugin_channel *dvp;
	snd_pcm_format_t format;

	if (snd_BUG_ON(!plugin || !src_channels || !dst_channels))
		return -ENXIO;
	if (frames == 0)
		return 0;
	if (frames > dst_channels[0].frames)
		frames = dst_channels[0].frames;

	nsrcs = plugin->src_format.channels;
	ndsts = plugin->dst_format.channels;

	format = plugin->dst_format.format;
	dvp = dst_channels;
	if (nsrcs <= 1) {
		/* expand to all channels */
		for (dst = 0; dst < ndsts; ++dst) {
			copy_area(src_channels, dvp, frames, format);
			dvp++;
		}
		return frames;
	}

	for (dst = 0; dst < ndsts && dst < nsrcs; ++dst) {
		copy_area(src_channels, dvp, frames, format);
		dvp++;
		src_channels++;
	}
	if (dst < ndsts)
		zero_areas(dvp, ndsts - dst, frames, format);
	return frames;
}

int snd_pcm_plugin_build_route(struct snd_pcm_substream *plug,
			       struct snd_pcm_plugin_format *src_format,
			       struct snd_pcm_plugin_format *dst_format,
			       struct snd_pcm_plugin **r_plugin)
{
	struct snd_pcm_plugin *plugin;
	int err;

	if (snd_BUG_ON(!r_plugin))
		return -ENXIO;
	*r_plugin = NULL;
	if (snd_BUG_ON(src_format->rate != dst_format->rate))
		return -ENXIO;
	if (snd_BUG_ON(src_format->format != dst_format->format))
		return -ENXIO;

	err = snd_pcm_plugin_build(plug, "route conversion",
				   src_format, dst_format, 0, &plugin);
	if (err < 0)
		return err;

	plugin->transfer = route_transfer;
	*r_plugin = plugin;
	return 0;
}
