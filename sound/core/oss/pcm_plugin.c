/*
 *  PCM Plug-In shared (kernel/library) code
 *  Copyright (c) 1999 by Jaroslav Kysela <perex@perex.cz>
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
  
#if 0
#define PLUGIN_DEBUG
#endif

#include <linux/slab.h>
#include <linux/time.h>
#include <linux/vmalloc.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include "pcm_plugin.h"

#define snd_pcm_plug_first(plug) ((plug)->runtime->oss.plugin_first)
#define snd_pcm_plug_last(plug) ((plug)->runtime->oss.plugin_last)

/*
 *  because some cards might have rates "very close", we ignore
 *  all "resampling" requests within +-5%
 */
static int rate_match(unsigned int src_rate, unsigned int dst_rate)
{
	unsigned int low = (src_rate * 95) / 100;
	unsigned int high = (src_rate * 105) / 100;
	return dst_rate >= low && dst_rate <= high;
}

static int snd_pcm_plugin_alloc(struct snd_pcm_plugin *plugin, snd_pcm_uframes_t frames)
{
	struct snd_pcm_plugin_format *format;
	ssize_t width;
	size_t size;
	unsigned int channel;
	struct snd_pcm_plugin_channel *c;

	if (plugin->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		format = &plugin->src_format;
	} else {
		format = &plugin->dst_format;
	}
	if ((width = snd_pcm_format_physical_width(format->format)) < 0)
		return width;
	size = frames * format->channels * width;
	if (snd_BUG_ON(size % 8))
		return -ENXIO;
	size /= 8;
	if (plugin->buf_frames < frames) {
		vfree(plugin->buf);
		plugin->buf = vmalloc(size);
		plugin->buf_frames = frames;
	}
	if (!plugin->buf) {
		plugin->buf_frames = 0;
		return -ENOMEM;
	}
	c = plugin->buf_channels;
	if (plugin->access == SNDRV_PCM_ACCESS_RW_INTERLEAVED) {
		for (channel = 0; channel < format->channels; channel++, c++) {
			c->frames = frames;
			c->enabled = 1;
			c->wanted = 0;
			c->area.addr = plugin->buf;
			c->area.first = channel * width;
			c->area.step = format->channels * width;
		}
	} else if (plugin->access == SNDRV_PCM_ACCESS_RW_NONINTERLEAVED) {
		if (snd_BUG_ON(size % format->channels))
			return -EINVAL;
		size /= format->channels;
		for (channel = 0; channel < format->channels; channel++, c++) {
			c->frames = frames;
			c->enabled = 1;
			c->wanted = 0;
			c->area.addr = plugin->buf + (channel * size);
			c->area.first = 0;
			c->area.step = width;
		}
	} else
		return -EINVAL;
	return 0;
}

int snd_pcm_plug_alloc(struct snd_pcm_substream *plug, snd_pcm_uframes_t frames)
{
	int err;
	if (snd_BUG_ON(!snd_pcm_plug_first(plug)))
		return -ENXIO;
	if (snd_pcm_plug_stream(plug) == SNDRV_PCM_STREAM_PLAYBACK) {
		struct snd_pcm_plugin *plugin = snd_pcm_plug_first(plug);
		while (plugin->next) {
			if (plugin->dst_frames)
				frames = plugin->dst_frames(plugin, frames);
			if (snd_BUG_ON(frames <= 0))
				return -ENXIO;
			plugin = plugin->next;
			err = snd_pcm_plugin_alloc(plugin, frames);
			if (err < 0)
				return err;
		}
	} else {
		struct snd_pcm_plugin *plugin = snd_pcm_plug_last(plug);
		while (plugin->prev) {
			if (plugin->src_frames)
				frames = plugin->src_frames(plugin, frames);
			if (snd_BUG_ON(frames <= 0))
				return -ENXIO;
			plugin = plugin->prev;
			err = snd_pcm_plugin_alloc(plugin, frames);
			if (err < 0)
				return err;
		}
	}
	return 0;
}


snd_pcm_sframes_t snd_pcm_plugin_client_channels(struct snd_pcm_plugin *plugin,
				       snd_pcm_uframes_t frames,
				       struct snd_pcm_plugin_channel **channels)
{
	*channels = plugin->buf_channels;
	return frames;
}

int snd_pcm_plugin_build(struct snd_pcm_substream *plug,
			 const char *name,
			 struct snd_pcm_plugin_format *src_format,
			 struct snd_pcm_plugin_format *dst_format,
			 size_t extra,
			 struct snd_pcm_plugin **ret)
{
	struct snd_pcm_plugin *plugin;
	unsigned int channels;
	
	if (snd_BUG_ON(!plug))
		return -ENXIO;
	if (snd_BUG_ON(!src_format || !dst_format))
		return -ENXIO;
	plugin = kzalloc(sizeof(*plugin) + extra, GFP_KERNEL);
	if (plugin == NULL)
		return -ENOMEM;
	plugin->name = name;
	plugin->plug = plug;
	plugin->stream = snd_pcm_plug_stream(plug);
	plugin->access = SNDRV_PCM_ACCESS_RW_INTERLEAVED;
	plugin->src_format = *src_format;
	plugin->src_width = snd_pcm_format_physical_width(src_format->format);
	snd_BUG_ON(plugin->src_width <= 0);
	plugin->dst_format = *dst_format;
	plugin->dst_width = snd_pcm_format_physical_width(dst_format->format);
	snd_BUG_ON(plugin->dst_width <= 0);
	if (plugin->stream == SNDRV_PCM_STREAM_PLAYBACK)
		channels = src_format->channels;
	else
		channels = dst_format->channels;
	plugin->buf_channels = kcalloc(channels, sizeof(*plugin->buf_channels), GFP_KERNEL);
	if (plugin->buf_channels == NULL) {
		snd_pcm_plugin_free(plugin);
		return -ENOMEM;
	}
	plugin->client_channels = snd_pcm_plugin_client_channels;
	*ret = plugin;
	return 0;
}

int snd_pcm_plugin_free(struct snd_pcm_plugin *plugin)
{
	if (! plugin)
		return 0;
	if (plugin->private_free)
		plugin->private_free(plugin);
	kfree(plugin->buf_channels);
	vfree(plugin->buf);
	kfree(plugin);
	return 0;
}

snd_pcm_sframes_t snd_pcm_plug_client_size(struct snd_pcm_substream *plug, snd_pcm_uframes_t drv_frames)
{
	struct snd_pcm_plugin *plugin, *plugin_prev, *plugin_next;
	int stream;

	if (snd_BUG_ON(!plug))
		return -ENXIO;
	if (drv_frames == 0)
		return 0;
	stream = snd_pcm_plug_stream(plug);
	if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
		plugin = snd_pcm_plug_last(plug);
		while (plugin && drv_frames > 0) {
			plugin_prev = plugin->prev;
			if (plugin->src_frames)
				drv_frames = plugin->src_frames(plugin, drv_frames);
			plugin = plugin_prev;
		}
	} else if (stream == SNDRV_PCM_STREAM_CAPTURE) {
		plugin = snd_pcm_plug_first(plug);
		while (plugin && drv_frames > 0) {
			plugin_next = plugin->next;
			if (plugin->dst_frames)
				drv_frames = plugin->dst_frames(plugin, drv_frames);
			plugin = plugin_next;
		}
	} else
		snd_BUG();
	return drv_frames;
}

snd_pcm_sframes_t snd_pcm_plug_slave_size(struct snd_pcm_substream *plug, snd_pcm_uframes_t clt_frames)
{
	struct snd_pcm_plugin *plugin, *plugin_prev, *plugin_next;
	snd_pcm_sframes_t frames;
	int stream = snd_pcm_plug_stream(plug);
	
	if (snd_BUG_ON(!plug))
		return -ENXIO;
	if (clt_frames == 0)
		return 0;
	frames = clt_frames;
	if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
		plugin = snd_pcm_plug_first(plug);
		while (plugin && frames > 0) {
			plugin_next = plugin->next;
			if (plugin->dst_frames) {
				frames = plugin->dst_frames(plugin, frames);
				if (frames < 0)
					return frames;
			}
			plugin = plugin_next;
		}
	} else if (stream == SNDRV_PCM_STREAM_CAPTURE) {
		plugin = snd_pcm_plug_last(plug);
		while (plugin) {
			plugin_prev = plugin->prev;
			if (plugin->src_frames) {
				frames = plugin->src_frames(plugin, frames);
				if (frames < 0)
					return frames;
			}
			plugin = plugin_prev;
		}
	} else
		snd_BUG();
	return frames;
}

static int snd_pcm_plug_formats(struct snd_mask *mask, snd_pcm_format_t format)
{
	struct snd_mask formats = *mask;
	u64 linfmts = (SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S8 |
		       SNDRV_PCM_FMTBIT_U16_LE | SNDRV_PCM_FMTBIT_S16_LE |
		       SNDRV_PCM_FMTBIT_U16_BE | SNDRV_PCM_FMTBIT_S16_BE |
		       SNDRV_PCM_FMTBIT_U24_LE | SNDRV_PCM_FMTBIT_S24_LE |
		       SNDRV_PCM_FMTBIT_U24_BE | SNDRV_PCM_FMTBIT_S24_BE |
		       SNDRV_PCM_FMTBIT_U24_3LE | SNDRV_PCM_FMTBIT_S24_3LE |
		       SNDRV_PCM_FMTBIT_U24_3BE | SNDRV_PCM_FMTBIT_S24_3BE |
		       SNDRV_PCM_FMTBIT_U32_LE | SNDRV_PCM_FMTBIT_S32_LE |
		       SNDRV_PCM_FMTBIT_U32_BE | SNDRV_PCM_FMTBIT_S32_BE);
	snd_mask_set(&formats, (__force int)SNDRV_PCM_FORMAT_MU_LAW);
	
	if (formats.bits[0] & (u32)linfmts)
		formats.bits[0] |= (u32)linfmts;
	if (formats.bits[1] & (u32)(linfmts >> 32))
		formats.bits[1] |= (u32)(linfmts >> 32);
	return snd_mask_test(&formats, (__force int)format);
}

static snd_pcm_format_t preferred_formats[] = {
	SNDRV_PCM_FORMAT_S16_LE,
	SNDRV_PCM_FORMAT_S16_BE,
	SNDRV_PCM_FORMAT_U16_LE,
	SNDRV_PCM_FORMAT_U16_BE,
	SNDRV_PCM_FORMAT_S24_3LE,
	SNDRV_PCM_FORMAT_S24_3BE,
	SNDRV_PCM_FORMAT_U24_3LE,
	SNDRV_PCM_FORMAT_U24_3BE,
	SNDRV_PCM_FORMAT_S24_LE,
	SNDRV_PCM_FORMAT_S24_BE,
	SNDRV_PCM_FORMAT_U24_LE,
	SNDRV_PCM_FORMAT_U24_BE,
	SNDRV_PCM_FORMAT_S32_LE,
	SNDRV_PCM_FORMAT_S32_BE,
	SNDRV_PCM_FORMAT_U32_LE,
	SNDRV_PCM_FORMAT_U32_BE,
	SNDRV_PCM_FORMAT_S8,
	SNDRV_PCM_FORMAT_U8
};

snd_pcm_format_t snd_pcm_plug_slave_format(snd_pcm_format_t format,
					   struct snd_mask *format_mask)
{
	int i;

	if (snd_mask_test(format_mask, (__force int)format))
		return format;
	if (!snd_pcm_plug_formats(format_mask, format))
		return (__force snd_pcm_format_t)-EINVAL;
	if (snd_pcm_format_linear(format)) {
		unsigned int width = snd_pcm_format_width(format);
		int unsignd = snd_pcm_format_unsigned(format) > 0;
		int big = snd_pcm_format_big_endian(format) > 0;
		unsigned int badness, best = -1;
		snd_pcm_format_t best_format = (__force snd_pcm_format_t)-1;
		for (i = 0; i < ARRAY_SIZE(preferred_formats); i++) {
			snd_pcm_format_t f = preferred_formats[i];
			unsigned int w;
			if (!snd_mask_test(format_mask, (__force int)f))
				continue;
			w = snd_pcm_format_width(f);
			if (w >= width)
				badness = w - width;
			else
				badness = width - w + 32;
			badness += snd_pcm_format_unsigned(f) != unsignd;
			badness += snd_pcm_format_big_endian(f) != big;
			if (badness < best) {
				best_format = f;
				best = badness;
			}
		}
		if ((__force int)best_format >= 0)
			return best_format;
		else
			return (__force snd_pcm_format_t)-EINVAL;
	} else {
		switch (format) {
		case SNDRV_PCM_FORMAT_MU_LAW:
			for (i = 0; i < ARRAY_SIZE(preferred_formats); ++i) {
				snd_pcm_format_t format1 = preferred_formats[i];
				if (snd_mask_test(format_mask, (__force int)format1))
					return format1;
			}
		default:
			return (__force snd_pcm_format_t)-EINVAL;
		}
	}
}

int snd_pcm_plug_format_plugins(struct snd_pcm_substream *plug,
				struct snd_pcm_hw_params *params,
				struct snd_pcm_hw_params *slave_params)
{
	struct snd_pcm_plugin_format tmpformat;
	struct snd_pcm_plugin_format dstformat;
	struct snd_pcm_plugin_format srcformat;
	snd_pcm_access_t src_access, dst_access;
	struct snd_pcm_plugin *plugin = NULL;
	int err;
	int stream = snd_pcm_plug_stream(plug);
	int slave_interleaved = (params_channels(slave_params) == 1 ||
				 params_access(slave_params) == SNDRV_PCM_ACCESS_RW_INTERLEAVED);

	switch (stream) {
	case SNDRV_PCM_STREAM_PLAYBACK:
		dstformat.format = params_format(slave_params);
		dstformat.rate = params_rate(slave_params);
		dstformat.channels = params_channels(slave_params);
		srcformat.format = params_format(params);
		srcformat.rate = params_rate(params);
		srcformat.channels = params_channels(params);
		src_access = SNDRV_PCM_ACCESS_RW_INTERLEAVED;
		dst_access = (slave_interleaved ? SNDRV_PCM_ACCESS_RW_INTERLEAVED :
						  SNDRV_PCM_ACCESS_RW_NONINTERLEAVED);
		break;
	case SNDRV_PCM_STREAM_CAPTURE:
		dstformat.format = params_format(params);
		dstformat.rate = params_rate(params);
		dstformat.channels = params_channels(params);
		srcformat.format = params_format(slave_params);
		srcformat.rate = params_rate(slave_params);
		srcformat.channels = params_channels(slave_params);
		src_access = (slave_interleaved ? SNDRV_PCM_ACCESS_RW_INTERLEAVED :
						  SNDRV_PCM_ACCESS_RW_NONINTERLEAVED);
		dst_access = SNDRV_PCM_ACCESS_RW_INTERLEAVED;
		break;
	default:
		snd_BUG();
		return -EINVAL;
	}
	tmpformat = srcformat;
		
	pdprintf("srcformat: format=%i, rate=%i, channels=%i\n", 
		 srcformat.format,
		 srcformat.rate,
		 srcformat.channels);
	pdprintf("dstformat: format=%i, rate=%i, channels=%i\n", 
		 dstformat.format,
		 dstformat.rate,
		 dstformat.channels);

	/* Format change (linearization) */
	if (! rate_match(srcformat.rate, dstformat.rate) &&
	    ! snd_pcm_format_linear(srcformat.format)) {
		if (srcformat.format != SNDRV_PCM_FORMAT_MU_LAW)
			return -EINVAL;
		tmpformat.format = SNDRV_PCM_FORMAT_S16;
		err = snd_pcm_plugin_build_mulaw(plug,
						 &srcformat, &tmpformat,
						 &plugin);
		if (err < 0)
			return err;
		err = snd_pcm_plugin_append(plugin);
		if (err < 0) {
			snd_pcm_plugin_free(plugin);
			return err;
		}
		srcformat = tmpformat;
		src_access = dst_access;
	}

	/* channels reduction */
	if (srcformat.channels > dstformat.channels) {
		tmpformat.channels = dstformat.channels;
		err = snd_pcm_plugin_build_route(plug, &srcformat, &tmpformat, &plugin);
		pdprintf("channels reduction: src=%i, dst=%i returns %i\n", srcformat.channels, tmpformat.channels, err);
		if (err < 0)
			return err;
		err = snd_pcm_plugin_append(plugin);
		if (err < 0) {
			snd_pcm_plugin_free(plugin);
			return err;
		}
		srcformat = tmpformat;
		src_access = dst_access;
	}

	/* rate resampling */
	if (!rate_match(srcformat.rate, dstformat.rate)) {
		if (srcformat.format != SNDRV_PCM_FORMAT_S16) {
			/* convert to S16 for resampling */
			tmpformat.format = SNDRV_PCM_FORMAT_S16;
			err = snd_pcm_plugin_build_linear(plug,
							  &srcformat, &tmpformat,
							  &plugin);
			if (err < 0)
				return err;
			err = snd_pcm_plugin_append(plugin);
			if (err < 0) {
				snd_pcm_plugin_free(plugin);
				return err;
			}
			srcformat = tmpformat;
			src_access = dst_access;
		}
		tmpformat.rate = dstformat.rate;
        	err = snd_pcm_plugin_build_rate(plug,
        					&srcformat, &tmpformat,
						&plugin);
		pdprintf("rate down resampling: src=%i, dst=%i returns %i\n", srcformat.rate, tmpformat.rate, err);
		if (err < 0)
			return err;
		err = snd_pcm_plugin_append(plugin);
		if (err < 0) {
			snd_pcm_plugin_free(plugin);
			return err;
		}
		srcformat = tmpformat;
		src_access = dst_access;
        }

	/* format change */
	if (srcformat.format != dstformat.format) {
		tmpformat.format = dstformat.format;
		if (srcformat.format == SNDRV_PCM_FORMAT_MU_LAW ||
		    tmpformat.format == SNDRV_PCM_FORMAT_MU_LAW) {
			err = snd_pcm_plugin_build_mulaw(plug,
							 &srcformat, &tmpformat,
							 &plugin);
		}
		else if (snd_pcm_format_linear(srcformat.format) &&
			 snd_pcm_format_linear(tmpformat.format)) {
			err = snd_pcm_plugin_build_linear(plug,
							  &srcformat, &tmpformat,
							  &plugin);
		}
		else
			return -EINVAL;
		pdprintf("format change: src=%i, dst=%i returns %i\n", srcformat.format, tmpformat.format, err);
		if (err < 0)
			return err;
		err = snd_pcm_plugin_append(plugin);
		if (err < 0) {
			snd_pcm_plugin_free(plugin);
			return err;
		}
		srcformat = tmpformat;
		src_access = dst_access;
	}

	/* channels extension */
	if (srcformat.channels < dstformat.channels) {
		tmpformat.channels = dstformat.channels;
		err = snd_pcm_plugin_build_route(plug, &srcformat, &tmpformat, &plugin);
		pdprintf("channels extension: src=%i, dst=%i returns %i\n", srcformat.channels, tmpformat.channels, err);
		if (err < 0)
			return err;
		err = snd_pcm_plugin_append(plugin);
		if (err < 0) {
			snd_pcm_plugin_free(plugin);
			return err;
		}
		srcformat = tmpformat;
		src_access = dst_access;
	}

	/* de-interleave */
	if (src_access != dst_access) {
		err = snd_pcm_plugin_build_copy(plug,
						&srcformat,
						&tmpformat,
						&plugin);
		pdprintf("interleave change (copy: returns %i)\n", err);
		if (err < 0)
			return err;
		err = snd_pcm_plugin_append(plugin);
		if (err < 0) {
			snd_pcm_plugin_free(plugin);
			return err;
		}
	}

	return 0;
}

snd_pcm_sframes_t snd_pcm_plug_client_channels_buf(struct snd_pcm_substream *plug,
					 char *buf,
					 snd_pcm_uframes_t count,
					 struct snd_pcm_plugin_channel **channels)
{
	struct snd_pcm_plugin *plugin;
	struct snd_pcm_plugin_channel *v;
	struct snd_pcm_plugin_format *format;
	int width, nchannels, channel;
	int stream = snd_pcm_plug_stream(plug);

	if (snd_BUG_ON(!buf))
		return -ENXIO;
	if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
		plugin = snd_pcm_plug_first(plug);
		format = &plugin->src_format;
	} else {
		plugin = snd_pcm_plug_last(plug);
		format = &plugin->dst_format;
	}
	v = plugin->buf_channels;
	*channels = v;
	if ((width = snd_pcm_format_physical_width(format->format)) < 0)
		return width;
	nchannels = format->channels;
	if (snd_BUG_ON(plugin->access != SNDRV_PCM_ACCESS_RW_INTERLEAVED &&
		       format->channels > 1))
		return -ENXIO;
	for (channel = 0; channel < nchannels; channel++, v++) {
		v->frames = count;
		v->enabled = 1;
		v->wanted = (stream == SNDRV_PCM_STREAM_CAPTURE);
		v->area.addr = buf;
		v->area.first = channel * width;
		v->area.step = nchannels * width;
	}
	return count;
}

snd_pcm_sframes_t snd_pcm_plug_write_transfer(struct snd_pcm_substream *plug, struct snd_pcm_plugin_channel *src_channels, snd_pcm_uframes_t size)
{
	struct snd_pcm_plugin *plugin, *next;
	struct snd_pcm_plugin_channel *dst_channels;
	int err;
	snd_pcm_sframes_t frames = size;

	plugin = snd_pcm_plug_first(plug);
	while (plugin && frames > 0) {
		if ((next = plugin->next) != NULL) {
			snd_pcm_sframes_t frames1 = frames;
			if (plugin->dst_frames)
				frames1 = plugin->dst_frames(plugin, frames);
			if ((err = next->client_channels(next, frames1, &dst_channels)) < 0) {
				return err;
			}
			if (err != frames1) {
				frames = err;
				if (plugin->src_frames)
					frames = plugin->src_frames(plugin, frames1);
			}
		} else
			dst_channels = NULL;
		pdprintf("write plugin: %s, %li\n", plugin->name, frames);
		if ((frames = plugin->transfer(plugin, src_channels, dst_channels, frames)) < 0)
			return frames;
		src_channels = dst_channels;
		plugin = next;
	}
	return snd_pcm_plug_client_size(plug, frames);
}

snd_pcm_sframes_t snd_pcm_plug_read_transfer(struct snd_pcm_substream *plug, struct snd_pcm_plugin_channel *dst_channels_final, snd_pcm_uframes_t size)
{
	struct snd_pcm_plugin *plugin, *next;
	struct snd_pcm_plugin_channel *src_channels, *dst_channels;
	snd_pcm_sframes_t frames = size;
	int err;

	frames = snd_pcm_plug_slave_size(plug, frames);
	if (frames < 0)
		return frames;

	src_channels = NULL;
	plugin = snd_pcm_plug_first(plug);
	while (plugin && frames > 0) {
		if ((next = plugin->next) != NULL) {
			if ((err = plugin->client_channels(plugin, frames, &dst_channels)) < 0) {
				return err;
			}
			frames = err;
		} else {
			dst_channels = dst_channels_final;
		}
		pdprintf("read plugin: %s, %li\n", plugin->name, frames);
		if ((frames = plugin->transfer(plugin, src_channels, dst_channels, frames)) < 0)
			return frames;
		plugin = next;
		src_channels = dst_channels;
	}
	return frames;
}

int snd_pcm_area_silence(const struct snd_pcm_channel_area *dst_area, size_t dst_offset,
			 size_t samples, snd_pcm_format_t format)
{
	/* FIXME: sub byte resolution and odd dst_offset */
	unsigned char *dst;
	unsigned int dst_step;
	int width;
	const unsigned char *silence;
	if (!dst_area->addr)
		return 0;
	dst = dst_area->addr + (dst_area->first + dst_area->step * dst_offset) / 8;
	width = snd_pcm_format_physical_width(format);
	if (width <= 0)
		return -EINVAL;
	if (dst_area->step == (unsigned int) width && width >= 8)
		return snd_pcm_format_set_silence(format, dst, samples);
	silence = snd_pcm_format_silence_64(format);
	if (! silence)
		return -EINVAL;
	dst_step = dst_area->step / 8;
	if (width == 4) {
		/* Ima ADPCM */
		int dstbit = dst_area->first % 8;
		int dstbit_step = dst_area->step % 8;
		while (samples-- > 0) {
			if (dstbit)
				*dst &= 0xf0;
			else
				*dst &= 0x0f;
			dst += dst_step;
			dstbit += dstbit_step;
			if (dstbit == 8) {
				dst++;
				dstbit = 0;
			}
		}
	} else {
		width /= 8;
		while (samples-- > 0) {
			memcpy(dst, silence, width);
			dst += dst_step;
		}
	}
	return 0;
}

int snd_pcm_area_copy(const struct snd_pcm_channel_area *src_area, size_t src_offset,
		      const struct snd_pcm_channel_area *dst_area, size_t dst_offset,
		      size_t samples, snd_pcm_format_t format)
{
	/* FIXME: sub byte resolution and odd dst_offset */
	char *src, *dst;
	int width;
	int src_step, dst_step;
	src = src_area->addr + (src_area->first + src_area->step * src_offset) / 8;
	if (!src_area->addr)
		return snd_pcm_area_silence(dst_area, dst_offset, samples, format);
	dst = dst_area->addr + (dst_area->first + dst_area->step * dst_offset) / 8;
	if (!dst_area->addr)
		return 0;
	width = snd_pcm_format_physical_width(format);
	if (width <= 0)
		return -EINVAL;
	if (src_area->step == (unsigned int) width &&
	    dst_area->step == (unsigned int) width && width >= 8) {
		size_t bytes = samples * width / 8;
		memcpy(dst, src, bytes);
		return 0;
	}
	src_step = src_area->step / 8;
	dst_step = dst_area->step / 8;
	if (width == 4) {
		/* Ima ADPCM */
		int srcbit = src_area->first % 8;
		int srcbit_step = src_area->step % 8;
		int dstbit = dst_area->first % 8;
		int dstbit_step = dst_area->step % 8;
		while (samples-- > 0) {
			unsigned char srcval;
			if (srcbit)
				srcval = *src & 0x0f;
			else
				srcval = (*src & 0xf0) >> 4;
			if (dstbit)
				*dst = (*dst & 0xf0) | srcval;
			else
				*dst = (*dst & 0x0f) | (srcval << 4);
			src += src_step;
			srcbit += srcbit_step;
			if (srcbit == 8) {
				src++;
				srcbit = 0;
			}
			dst += dst_step;
			dstbit += dstbit_step;
			if (dstbit == 8) {
				dst++;
				dstbit = 0;
			}
		}
	} else {
		width /= 8;
		while (samples-- > 0) {
			memcpy(dst, src, width);
			src += src_step;
			dst += dst_step;
		}
	}
	return 0;
}
