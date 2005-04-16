/*
 *  PCM I/O Plug-In Interface
 *  Copyright (c) 1999 by Jaroslav Kysela <perex@suse.cz>
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
#include <sound/pcm_params.h>
#include "pcm_plugin.h"

#define pcm_write(plug,buf,count) snd_pcm_oss_write3(plug,buf,count,1)
#define pcm_writev(plug,vec,count) snd_pcm_oss_writev3(plug,vec,count,1)
#define pcm_read(plug,buf,count) snd_pcm_oss_read3(plug,buf,count,1)
#define pcm_readv(plug,vec,count) snd_pcm_oss_readv3(plug,vec,count,1)

/*
 *  Basic io plugin
 */
 
static snd_pcm_sframes_t io_playback_transfer(snd_pcm_plugin_t *plugin,
				    const snd_pcm_plugin_channel_t *src_channels,
				    snd_pcm_plugin_channel_t *dst_channels ATTRIBUTE_UNUSED,
				    snd_pcm_uframes_t frames)
{
	snd_assert(plugin != NULL, return -ENXIO);
	snd_assert(src_channels != NULL, return -ENXIO);
	if (plugin->access == SNDRV_PCM_ACCESS_RW_INTERLEAVED) {
		return pcm_write(plugin->plug, src_channels->area.addr, frames);
	} else {
		int channel, channels = plugin->dst_format.channels;
		void **bufs = (void**)plugin->extra_data;
		snd_assert(bufs != NULL, return -ENXIO);
		for (channel = 0; channel < channels; channel++) {
			if (src_channels[channel].enabled)
				bufs[channel] = src_channels[channel].area.addr;
			else
				bufs[channel] = NULL;
		}
		return pcm_writev(plugin->plug, bufs, frames);
	}
}
 
static snd_pcm_sframes_t io_capture_transfer(snd_pcm_plugin_t *plugin,
				   const snd_pcm_plugin_channel_t *src_channels ATTRIBUTE_UNUSED,
				   snd_pcm_plugin_channel_t *dst_channels,
				   snd_pcm_uframes_t frames)
{
	snd_assert(plugin != NULL, return -ENXIO);
	snd_assert(dst_channels != NULL, return -ENXIO);
	if (plugin->access == SNDRV_PCM_ACCESS_RW_INTERLEAVED) {
		return pcm_read(plugin->plug, dst_channels->area.addr, frames);
	} else {
		int channel, channels = plugin->dst_format.channels;
		void **bufs = (void**)plugin->extra_data;
		snd_assert(bufs != NULL, return -ENXIO);
		for (channel = 0; channel < channels; channel++) {
			if (dst_channels[channel].enabled)
				bufs[channel] = dst_channels[channel].area.addr;
			else
				bufs[channel] = NULL;
		}
		return pcm_readv(plugin->plug, bufs, frames);
	}
	return 0;
}
 
static snd_pcm_sframes_t io_src_channels(snd_pcm_plugin_t *plugin,
			     snd_pcm_uframes_t frames,
			     snd_pcm_plugin_channel_t **channels)
{
	int err;
	unsigned int channel;
	snd_pcm_plugin_channel_t *v;
	err = snd_pcm_plugin_client_channels(plugin, frames, &v);
	if (err < 0)
		return err;
	*channels = v;
	if (plugin->access == SNDRV_PCM_ACCESS_RW_INTERLEAVED) {
		for (channel = 0; channel < plugin->src_format.channels; ++channel, ++v)
			v->wanted = 1;
	}
	return frames;
}

int snd_pcm_plugin_build_io(snd_pcm_plug_t *plug,
			    snd_pcm_hw_params_t *params,
			    snd_pcm_plugin_t **r_plugin)
{
	int err;
	snd_pcm_plugin_format_t format;
	snd_pcm_plugin_t *plugin;

	snd_assert(r_plugin != NULL, return -ENXIO);
	*r_plugin = NULL;
	snd_assert(plug != NULL && params != NULL, return -ENXIO);
	format.format = params_format(params);
	format.rate = params_rate(params);
	format.channels = params_channels(params);
	err = snd_pcm_plugin_build(plug, "I/O io",
				   &format, &format,
				   sizeof(void *) * format.channels,
				   &plugin);
	if (err < 0)
		return err;
	plugin->access = params_access(params);
	if (snd_pcm_plug_stream(plug) == SNDRV_PCM_STREAM_PLAYBACK) {
		plugin->transfer = io_playback_transfer;
		if (plugin->access == SNDRV_PCM_ACCESS_RW_INTERLEAVED)
			plugin->client_channels = io_src_channels;
	} else {
		plugin->transfer = io_capture_transfer;
	}

	*r_plugin = plugin;
	return 0;
}
