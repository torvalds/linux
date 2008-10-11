/*
 *  Linear conversion Plug-In
 *  Copyright (c) 1999 by Jaroslav Kysela <perex@perex.cz>,
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

#include <linux/time.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include "pcm_plugin.h"

/*
 *  Basic linear conversion plugin
 */
 
struct linear_priv {
	int cvt_endian;		/* need endian conversion? */
	unsigned int src_ofs;	/* byte offset in source format */
	unsigned int dst_ofs;	/* byte soffset in destination format */
	unsigned int copy_ofs;	/* byte offset in temporary u32 data */
	unsigned int dst_bytes;		/* byte size of destination format */
	unsigned int copy_bytes;	/* bytes to copy per conversion */
	unsigned int flip; /* MSB flip for signeness, done after endian conv */
};

static inline void do_convert(struct linear_priv *data,
			      unsigned char *dst, unsigned char *src)
{
	unsigned int tmp = 0;
	unsigned char *p = (unsigned char *)&tmp;

	memcpy(p + data->copy_ofs, src + data->src_ofs, data->copy_bytes);
	if (data->cvt_endian)
		tmp = swab32(tmp);
	tmp ^= data->flip;
	memcpy(dst, p + data->dst_ofs, data->dst_bytes);
}

static void convert(struct snd_pcm_plugin *plugin,
		    const struct snd_pcm_plugin_channel *src_channels,
		    struct snd_pcm_plugin_channel *dst_channels,
		    snd_pcm_uframes_t frames)
{
	struct linear_priv *data = (struct linear_priv *)plugin->extra_data;
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
			do_convert(data, dst, src);
			src += src_step;
			dst += dst_step;
		}
	}
}

static snd_pcm_sframes_t linear_transfer(struct snd_pcm_plugin *plugin,
			       const struct snd_pcm_plugin_channel *src_channels,
			       struct snd_pcm_plugin_channel *dst_channels,
			       snd_pcm_uframes_t frames)
{
	struct linear_priv *data;

	if (snd_BUG_ON(!plugin || !src_channels || !dst_channels))
		return -ENXIO;
	data = (struct linear_priv *)plugin->extra_data;
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
	convert(plugin, src_channels, dst_channels, frames);
	return frames;
}

static void init_data(struct linear_priv *data, int src_format, int dst_format)
{
	int src_le, dst_le, src_bytes, dst_bytes;

	src_bytes = snd_pcm_format_width(src_format) / 8;
	dst_bytes = snd_pcm_format_width(dst_format) / 8;
	src_le = snd_pcm_format_little_endian(src_format) > 0;
	dst_le = snd_pcm_format_little_endian(dst_format) > 0;

	data->dst_bytes = dst_bytes;
	data->cvt_endian = src_le != dst_le;
	data->copy_bytes = src_bytes < dst_bytes ? src_bytes : dst_bytes;
	if (src_le) {
		data->copy_ofs = 4 - data->copy_bytes;
		data->src_ofs = src_bytes - data->copy_bytes;
	} else
		data->src_ofs = snd_pcm_format_physical_width(src_format) / 8 -
			src_bytes;
	if (dst_le)
		data->dst_ofs = 4 - data->dst_bytes;
	else
		data->dst_ofs = snd_pcm_format_physical_width(dst_format) / 8 -
			dst_bytes;
	if (snd_pcm_format_signed(src_format) !=
	    snd_pcm_format_signed(dst_format)) {
		if (dst_le)
			data->flip = cpu_to_le32(0x80000000);
		else
			data->flip = cpu_to_be32(0x80000000);
	}
}

int snd_pcm_plugin_build_linear(struct snd_pcm_substream *plug,
				struct snd_pcm_plugin_format *src_format,
				struct snd_pcm_plugin_format *dst_format,
				struct snd_pcm_plugin **r_plugin)
{
	int err;
	struct linear_priv *data;
	struct snd_pcm_plugin *plugin;

	if (snd_BUG_ON(!r_plugin))
		return -ENXIO;
	*r_plugin = NULL;

	if (snd_BUG_ON(src_format->rate != dst_format->rate))
		return -ENXIO;
	if (snd_BUG_ON(src_format->channels != dst_format->channels))
		return -ENXIO;
	if (snd_BUG_ON(!snd_pcm_format_linear(src_format->format) ||
		       !snd_pcm_format_linear(dst_format->format)))
		return -ENXIO;

	err = snd_pcm_plugin_build(plug, "linear format conversion",
				   src_format, dst_format,
				   sizeof(struct linear_priv), &plugin);
	if (err < 0)
		return err;
	data = (struct linear_priv *)plugin->extra_data;
	init_data(data, src_format->format, dst_format->format);
	plugin->transfer = linear_transfer;
	*r_plugin = plugin;
	return 0;
}
