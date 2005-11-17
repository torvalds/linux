/*
 *  Linear conversion Plug-In
 *  Copyright (c) 2000 by Abramo Bagnara <abramo@alsa-project.org>
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

#include <sound/driver.h>
#include <linux/time.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include "pcm_plugin.h"

static snd_pcm_sframes_t copy_transfer(struct snd_pcm_plugin *plugin,
			     const struct snd_pcm_plugin_channel *src_channels,
			     struct snd_pcm_plugin_channel *dst_channels,
			     snd_pcm_uframes_t frames)
{
	unsigned int channel;
	unsigned int nchannels;

	snd_assert(plugin != NULL && src_channels != NULL && dst_channels != NULL, return -ENXIO);
	if (frames == 0)
		return 0;
	nchannels = plugin->src_format.channels;
	for (channel = 0; channel < nchannels; channel++) {
		snd_assert(src_channels->area.first % 8 == 0 &&
			   src_channels->area.step % 8 == 0,
			   return -ENXIO);
		snd_assert(dst_channels->area.first % 8 == 0 &&
			   dst_channels->area.step % 8 == 0,
			   return -ENXIO);
		if (!src_channels->enabled) {
			if (dst_channels->wanted)
				snd_pcm_area_silence(&dst_channels->area, 0, frames, plugin->dst_format.format);
			dst_channels->enabled = 0;
			continue;
		}
		dst_channels->enabled = 1;
		snd_pcm_area_copy(&src_channels->area, 0, &dst_channels->area, 0, frames, plugin->src_format.format);
		src_channels++;
		dst_channels++;
	}
	return frames;
}

int snd_pcm_plugin_build_copy(struct snd_pcm_substream *plug,
			      struct snd_pcm_plugin_format *src_format,
			      struct snd_pcm_plugin_format *dst_format,
			      struct snd_pcm_plugin **r_plugin)
{
	int err;
	struct snd_pcm_plugin *plugin;
	int width;

	snd_assert(r_plugin != NULL, return -ENXIO);
	*r_plugin = NULL;

	snd_assert(src_format->format == dst_format->format, return -ENXIO);
	snd_assert(src_format->rate == dst_format->rate, return -ENXIO);
	snd_assert(src_format->channels == dst_format->channels, return -ENXIO);

	width = snd_pcm_format_physical_width(src_format->format);
	snd_assert(width > 0, return -ENXIO);

	err = snd_pcm_plugin_build(plug, "copy", src_format, dst_format,
				   0, &plugin);
	if (err < 0)
		return err;
	plugin->transfer = copy_transfer;
	*r_plugin = plugin;
	return 0;
}
