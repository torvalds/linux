/*
 *  Linear conversion Plug-In
 *  Copyright (c) 1999 by Jaroslav Kysela <perex@suse.cz>,
 *			  Abramo Bagnara <abramo@alsa-project.org>
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

/*
 *  Basic linear conversion plugin
 */
 
typedef struct linear_private_data {
	int conv;
} linear_t;

static void convert(snd_pcm_plugin_t *plugin,
		    const snd_pcm_plugin_channel_t *src_channels,
		    snd_pcm_plugin_channel_t *dst_channels,
		    snd_pcm_uframes_t frames)
{
#define CONV_LABELS
#include "plugin_ops.h"
#undef CONV_LABELS
	linear_t *data = (linear_t *)plugin->extra_data;
	void *conv = conv_labels[data->conv];
	int channel;
	int nchannels = plugin->src_format.channels;
	for (channel = 0; channel < nchannels; ++channel) {
		char *src;
		char *dst;
		int src_step, dst_step;
		snd_pcm_uframes_t frames1;
		if (!src_channels[channel].enabled) {
			if (dst_channels[channel].wanted)
				snd_pcm_area_silence(&dst_channels[channel].area, 0, frames, plugin->dst_format.format);
			dst_channels[channel].enabled = 0;
			continue;
		}
		dst_channels[channel].enabled = 1;
		src = src_channels[channel].area.addr + src_channels[channel].area.first / 8;
		dst = dst_channels[channel].area.addr + dst_channels[channel].area.first / 8;
		src_step = src_channels[channel].area.step / 8;
		dst_step = dst_channels[channel].area.step / 8;
		frames1 = frames;
		while (frames1-- > 0) {
			goto *conv;
#define CONV_END after
#include "plugin_ops.h"
#undef CONV_END
		after:
			src += src_step;
			dst += dst_step;
		}
	}
}

static snd_pcm_sframes_t linear_transfer(snd_pcm_plugin_t *plugin,
			       const snd_pcm_plugin_channel_t *src_channels,
			       snd_pcm_plugin_channel_t *dst_channels,
			       snd_pcm_uframes_t frames)
{
	linear_t *data;

	snd_assert(plugin != NULL && src_channels != NULL && dst_channels != NULL, return -ENXIO);
	data = (linear_t *)plugin->extra_data;
	if (frames == 0)
		return 0;
#ifdef CONFIG_SND_DEBUG
	{
		unsigned int channel;
		for (channel = 0; channel < plugin->src_format.channels; channel++) {
			snd_assert(src_channels[channel].area.first % 8 == 0 &&
				   src_channels[channel].area.step % 8 == 0,
				   return -ENXIO);
			snd_assert(dst_channels[channel].area.first % 8 == 0 &&
				   dst_channels[channel].area.step % 8 == 0,
				   return -ENXIO);
		}
	}
#endif
	convert(plugin, src_channels, dst_channels, frames);
	return frames;
}

int conv_index(int src_format, int dst_format)
{
	int src_endian, dst_endian, sign, src_width, dst_width;

	sign = (snd_pcm_format_signed(src_format) !=
		snd_pcm_format_signed(dst_format));
#ifdef SNDRV_LITTLE_ENDIAN
	src_endian = snd_pcm_format_big_endian(src_format);
	dst_endian = snd_pcm_format_big_endian(dst_format);
#else
	src_endian = snd_pcm_format_little_endian(src_format);
	dst_endian = snd_pcm_format_little_endian(dst_format);
#endif

	if (src_endian < 0)
		src_endian = 0;
	if (dst_endian < 0)
		dst_endian = 0;

	src_width = snd_pcm_format_width(src_format) / 8 - 1;
	dst_width = snd_pcm_format_width(dst_format) / 8 - 1;

	return src_width * 32 + src_endian * 16 + sign * 8 + dst_width * 2 + dst_endian;
}

int snd_pcm_plugin_build_linear(snd_pcm_plug_t *plug,
				snd_pcm_plugin_format_t *src_format,
				snd_pcm_plugin_format_t *dst_format,
				snd_pcm_plugin_t **r_plugin)
{
	int err;
	struct linear_private_data *data;
	snd_pcm_plugin_t *plugin;

	snd_assert(r_plugin != NULL, return -ENXIO);
	*r_plugin = NULL;

	snd_assert(src_format->rate == dst_format->rate, return -ENXIO);
	snd_assert(src_format->channels == dst_format->channels, return -ENXIO);
	snd_assert(snd_pcm_format_linear(src_format->format) &&
		   snd_pcm_format_linear(dst_format->format), return -ENXIO);

	err = snd_pcm_plugin_build(plug, "linear format conversion",
				   src_format, dst_format,
				   sizeof(linear_t), &plugin);
	if (err < 0)
		return err;
	data = (linear_t *)plugin->extra_data;
	data->conv = conv_index(src_format->format, dst_format->format);
	plugin->transfer = linear_transfer;
	*r_plugin = plugin;
	return 0;
}
