/*
 *  Attenuated route Plug-In
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
#include <linux/slab.h>
#include <linux/time.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include "pcm_plugin.h"

/* The best possible hack to support missing optimization in gcc 2.7.2.3 */
#if ROUTE_PLUGIN_RESOLUTION & (ROUTE_PLUGIN_RESOLUTION - 1) != 0
#define div(a) a /= ROUTE_PLUGIN_RESOLUTION
#elif ROUTE_PLUGIN_RESOLUTION == 16
#define div(a) a >>= 4
#else
#error "Add some code here"
#endif

typedef struct ttable_dst ttable_dst_t;
typedef struct route_private_data route_t;

typedef void (*route_channel_f)(snd_pcm_plugin_t *plugin,
			      const snd_pcm_plugin_channel_t *src_channels,
			      snd_pcm_plugin_channel_t *dst_channel,
			      ttable_dst_t* ttable, snd_pcm_uframes_t frames);

typedef struct {
	int channel;
	int as_int;
} ttable_src_t;

struct ttable_dst {
	int att;	/* Attenuated */
	unsigned int nsrcs;
	ttable_src_t* srcs;
	route_channel_f func;
};

struct route_private_data {
	enum {R_UINT32=0, R_UINT64=1} sum_type;
	int get, put;
	int conv;
	int src_sample_size;
	ttable_dst_t ttable[0];
};

typedef union {
	u_int32_t as_uint32;
	u_int64_t as_uint64;
} sum_t;


static void route_to_channel_from_zero(snd_pcm_plugin_t *plugin,
				     const snd_pcm_plugin_channel_t *src_channels ATTRIBUTE_UNUSED,
				     snd_pcm_plugin_channel_t *dst_channel,
				     ttable_dst_t* ttable ATTRIBUTE_UNUSED, snd_pcm_uframes_t frames)
{
	if (dst_channel->wanted)
		snd_pcm_area_silence(&dst_channel->area, 0, frames, plugin->dst_format.format);
	dst_channel->enabled = 0;
}

static void route_to_channel_from_one(snd_pcm_plugin_t *plugin,
				    const snd_pcm_plugin_channel_t *src_channels,
				    snd_pcm_plugin_channel_t *dst_channel,
				    ttable_dst_t* ttable, snd_pcm_uframes_t frames)
{
#define CONV_LABELS
#include "plugin_ops.h"
#undef CONV_LABELS
	route_t *data = (route_t *)plugin->extra_data;
	void *conv;
	const snd_pcm_plugin_channel_t *src_channel = NULL;
	unsigned int srcidx;
	char *src, *dst;
	int src_step, dst_step;
	for (srcidx = 0; srcidx < ttable->nsrcs; ++srcidx) {
		src_channel = &src_channels[ttable->srcs[srcidx].channel];
		if (src_channel->area.addr != NULL)
			break;
	}
	if (srcidx == ttable->nsrcs) {
		route_to_channel_from_zero(plugin, src_channels, dst_channel, ttable, frames);
		return;
	}

	dst_channel->enabled = 1;
	conv = conv_labels[data->conv];
	src = src_channel->area.addr + src_channel->area.first / 8;
	src_step = src_channel->area.step / 8;
	dst = dst_channel->area.addr + dst_channel->area.first / 8;
	dst_step = dst_channel->area.step / 8;
	while (frames-- > 0) {
		goto *conv;
#define CONV_END after
#include "plugin_ops.h"
#undef CONV_END
	after:
		src += src_step;
		dst += dst_step;
	}
}

static void route_to_channel(snd_pcm_plugin_t *plugin,
			   const snd_pcm_plugin_channel_t *src_channels,
			   snd_pcm_plugin_channel_t *dst_channel,
			   ttable_dst_t* ttable, snd_pcm_uframes_t frames)
{
#define GET_U_LABELS
#define PUT_U32_LABELS
#include "plugin_ops.h"
#undef GET_U_LABELS
#undef PUT_U32_LABELS
	static void *zero_labels[2] = { &&zero_int32, &&zero_int64 };
	/* sum_type att */
	static void *add_labels[2 * 2] = { &&add_int32_noatt, &&add_int32_att,
				    &&add_int64_noatt, &&add_int64_att,
	};
	/* sum_type att shift */
	static void *norm_labels[2 * 2 * 4] = { NULL,
					 &&norm_int32_8_noatt,
					 &&norm_int32_16_noatt,
					 &&norm_int32_24_noatt,
					 NULL,
					 &&norm_int32_8_att,
					 &&norm_int32_16_att,
					 &&norm_int32_24_att,
					 &&norm_int64_0_noatt,
					 &&norm_int64_8_noatt,
					 &&norm_int64_16_noatt,
					 &&norm_int64_24_noatt,
					 &&norm_int64_0_att,
					 &&norm_int64_8_att,
					 &&norm_int64_16_att,
					 &&norm_int64_24_att,
	};
	route_t *data = (route_t *)plugin->extra_data;
	void *zero, *get, *add, *norm, *put_u32;
	int nsrcs = ttable->nsrcs;
	char *dst;
	int dst_step;
	char *srcs[nsrcs];
	int src_steps[nsrcs];
	ttable_src_t src_tt[nsrcs];
	u_int32_t sample = 0;
	int srcidx, srcidx1 = 0;
	for (srcidx = 0; srcidx < nsrcs; ++srcidx) {
		const snd_pcm_plugin_channel_t *src_channel = &src_channels[ttable->srcs[srcidx].channel];
		if (!src_channel->enabled)
			continue;
		srcs[srcidx1] = src_channel->area.addr + src_channel->area.first / 8;
		src_steps[srcidx1] = src_channel->area.step / 8;
		src_tt[srcidx1] = ttable->srcs[srcidx];
		srcidx1++;
	}
	nsrcs = srcidx1;
	if (nsrcs == 0) {
		route_to_channel_from_zero(plugin, src_channels, dst_channel, ttable, frames);
		return;
	} else if (nsrcs == 1 && src_tt[0].as_int == ROUTE_PLUGIN_RESOLUTION) {
		route_to_channel_from_one(plugin, src_channels, dst_channel, ttable, frames);
		return;
	}

	dst_channel->enabled = 1;
	zero = zero_labels[data->sum_type];
	get = get_u_labels[data->get];
	add = add_labels[data->sum_type * 2 + ttable->att];
	norm = norm_labels[data->sum_type * 8 + ttable->att * 4 + 4 - data->src_sample_size];
	put_u32 = put_u32_labels[data->put];
	dst = dst_channel->area.addr + dst_channel->area.first / 8;
	dst_step = dst_channel->area.step / 8;

	while (frames-- > 0) {
		ttable_src_t *ttp = src_tt;
		sum_t sum;

		/* Zero sum */
		goto *zero;
	zero_int32:
		sum.as_uint32 = 0;
		goto zero_end;
	zero_int64: 
		sum.as_uint64 = 0;
		goto zero_end;
	zero_end:
		for (srcidx = 0; srcidx < nsrcs; ++srcidx) {
			char *src = srcs[srcidx];
			
			/* Get sample */
			goto *get;
#define GET_U_END after_get
#include "plugin_ops.h"
#undef GET_U_END
		after_get:

			/* Sum */
			goto *add;
		add_int32_att:
			sum.as_uint32 += sample * ttp->as_int;
			goto after_sum;
		add_int32_noatt:
			if (ttp->as_int)
				sum.as_uint32 += sample;
			goto after_sum;
		add_int64_att:
			sum.as_uint64 += (u_int64_t) sample * ttp->as_int;
			goto after_sum;
		add_int64_noatt:
			if (ttp->as_int)
				sum.as_uint64 += sample;
			goto after_sum;
		after_sum:
			srcs[srcidx] += src_steps[srcidx];
			ttp++;
		}
		
		/* Normalization */
		goto *norm;
	norm_int32_8_att:
		sum.as_uint64 = sum.as_uint32;
	norm_int64_8_att:
		sum.as_uint64 <<= 8;
	norm_int64_0_att:
		div(sum.as_uint64);
		goto norm_int;

	norm_int32_16_att:
		sum.as_uint64 = sum.as_uint32;
	norm_int64_16_att:
		sum.as_uint64 <<= 16;
		div(sum.as_uint64);
		goto norm_int;

	norm_int32_24_att:
		sum.as_uint64 = sum.as_uint32;
	norm_int64_24_att:
		sum.as_uint64 <<= 24;
		div(sum.as_uint64);
		goto norm_int;

	norm_int32_8_noatt:
		sum.as_uint64 = sum.as_uint32;
	norm_int64_8_noatt:
		sum.as_uint64 <<= 8;
		goto norm_int;

	norm_int32_16_noatt:
		sum.as_uint64 = sum.as_uint32;
	norm_int64_16_noatt:
		sum.as_uint64 <<= 16;
		goto norm_int;

	norm_int32_24_noatt:
		sum.as_uint64 = sum.as_uint32;
	norm_int64_24_noatt:
		sum.as_uint64 <<= 24;
		goto norm_int;

	norm_int64_0_noatt:
	norm_int:
		if (sum.as_uint64 > (u_int32_t)0xffffffff)
			sample = (u_int32_t)0xffffffff;
		else
			sample = sum.as_uint64;
		goto after_norm;

	after_norm:
		
		/* Put sample */
		goto *put_u32;
#define PUT_U32_END after_put_u32
#include "plugin_ops.h"
#undef PUT_U32_END
	after_put_u32:
		
		dst += dst_step;
	}
}

static int route_src_channels_mask(snd_pcm_plugin_t *plugin,
				   bitset_t *dst_vmask,
				   bitset_t **src_vmask)
{
	route_t *data = (route_t *)plugin->extra_data;
	int schannels = plugin->src_format.channels;
	int dchannels = plugin->dst_format.channels;
	bitset_t *vmask = plugin->src_vmask;
	int channel;
	ttable_dst_t *dp = data->ttable;
	bitset_zero(vmask, schannels);
	for (channel = 0; channel < dchannels; channel++, dp++) {
		unsigned int src;
		ttable_src_t *sp;
		if (!bitset_get(dst_vmask, channel))
			continue;
		sp = dp->srcs;
		for (src = 0; src < dp->nsrcs; src++, sp++)
			bitset_set(vmask, sp->channel);
	}
	*src_vmask = vmask;
	return 0;
}

static int route_dst_channels_mask(snd_pcm_plugin_t *plugin,
				   bitset_t *src_vmask,
				   bitset_t **dst_vmask)
{
	route_t *data = (route_t *)plugin->extra_data;
	int dchannels = plugin->dst_format.channels;
	bitset_t *vmask = plugin->dst_vmask;
	int channel;
	ttable_dst_t *dp = data->ttable;
	bitset_zero(vmask, dchannels);
	for (channel = 0; channel < dchannels; channel++, dp++) {
		unsigned int src;
		ttable_src_t *sp;
		sp = dp->srcs;
		for (src = 0; src < dp->nsrcs; src++, sp++) {
			if (bitset_get(src_vmask, sp->channel)) {
				bitset_set(vmask, channel);
				break;
			}
		}
	}
	*dst_vmask = vmask;
	return 0;
}

static void route_free(snd_pcm_plugin_t *plugin)
{
	route_t *data = (route_t *)plugin->extra_data;
	unsigned int dst_channel;
	for (dst_channel = 0; dst_channel < plugin->dst_format.channels; ++dst_channel) {
		kfree(data->ttable[dst_channel].srcs);
	}
}

static int route_load_ttable(snd_pcm_plugin_t *plugin, 
			     const route_ttable_entry_t* src_ttable)
{
	route_t *data;
	unsigned int src_channel, dst_channel;
	const route_ttable_entry_t *sptr;
	ttable_dst_t *dptr;
	if (src_ttable == NULL)
		return 0;
	data = (route_t *)plugin->extra_data;
	dptr = data->ttable;
	sptr = src_ttable;
	plugin->private_free = route_free;
	for (dst_channel = 0; dst_channel < plugin->dst_format.channels; ++dst_channel) {
		route_ttable_entry_t t = 0;
		int att = 0;
		int nsrcs = 0;
		ttable_src_t srcs[plugin->src_format.channels];
		for (src_channel = 0; src_channel < plugin->src_format.channels; ++src_channel) {
			snd_assert(*sptr >= 0 || *sptr <= FULL, return -ENXIO);
			if (*sptr != 0) {
				srcs[nsrcs].channel = src_channel;
				srcs[nsrcs].as_int = *sptr;
				if (*sptr != FULL)
					att = 1;
				t += *sptr;
				nsrcs++;
			}
			sptr++;
		}
		dptr->att = att;
		dptr->nsrcs = nsrcs;
		if (nsrcs == 0)
			dptr->func = route_to_channel_from_zero;
		else if (nsrcs == 1 && !att)
			dptr->func = route_to_channel_from_one;
		else
			dptr->func = route_to_channel;
		if (nsrcs > 0) {
                        int srcidx;
			dptr->srcs = kcalloc(nsrcs, sizeof(*srcs), GFP_KERNEL);
                        for(srcidx = 0; srcidx < nsrcs; srcidx++)
				dptr->srcs[srcidx] = srcs[srcidx];
		} else
			dptr->srcs = NULL;
		dptr++;
	}
	return 0;
}

static snd_pcm_sframes_t route_transfer(snd_pcm_plugin_t *plugin,
			      const snd_pcm_plugin_channel_t *src_channels,
			      snd_pcm_plugin_channel_t *dst_channels,
			      snd_pcm_uframes_t frames)
{
	route_t *data;
	int src_nchannels, dst_nchannels;
	int dst_channel;
	ttable_dst_t *ttp;
	snd_pcm_plugin_channel_t *dvp;

	snd_assert(plugin != NULL && src_channels != NULL && dst_channels != NULL, return -ENXIO);
	if (frames == 0)
		return 0;
	data = (route_t *)plugin->extra_data;

	src_nchannels = plugin->src_format.channels;
	dst_nchannels = plugin->dst_format.channels;

#ifdef CONFIG_SND_DEBUG
	{
		int src_channel;
		for (src_channel = 0; src_channel < src_nchannels; ++src_channel) {
			snd_assert(src_channels[src_channel].area.first % 8 == 0 ||
				   src_channels[src_channel].area.step % 8 == 0,
				   return -ENXIO);
		}
		for (dst_channel = 0; dst_channel < dst_nchannels; ++dst_channel) {
			snd_assert(dst_channels[dst_channel].area.first % 8 == 0 ||
				   dst_channels[dst_channel].area.step % 8 == 0,
				   return -ENXIO);
		}
	}
#endif

	ttp = data->ttable;
	dvp = dst_channels;
	for (dst_channel = 0; dst_channel < dst_nchannels; ++dst_channel) {
		ttp->func(plugin, src_channels, dvp, ttp, frames);
		dvp++;
		ttp++;
	}
	return frames;
}

int getput_index(int format)
{
	int sign, width, endian;
	sign = !snd_pcm_format_signed(format);
	width = snd_pcm_format_width(format) / 8 - 1;
	if (width < 0 || width > 3) {
		snd_printk(KERN_ERR "snd-pcm-oss: invalid format %d\n", format);
		width = 0;
	}
#ifdef SNDRV_LITTLE_ENDIAN
	endian = snd_pcm_format_big_endian(format);
#else
	endian = snd_pcm_format_little_endian(format);
#endif
	if (endian < 0)
		endian = 0;
	return width * 4 + endian * 2 + sign;
}

int snd_pcm_plugin_build_route(snd_pcm_plug_t *plug,
			       snd_pcm_plugin_format_t *src_format,
			       snd_pcm_plugin_format_t *dst_format,
			       route_ttable_entry_t *ttable,
			       snd_pcm_plugin_t **r_plugin)
{
	route_t *data;
	snd_pcm_plugin_t *plugin;
	int err;

	snd_assert(r_plugin != NULL, return -ENXIO);
	*r_plugin = NULL;
	snd_assert(src_format->rate == dst_format->rate, return -ENXIO);
	snd_assert(snd_pcm_format_linear(src_format->format) != 0 &&
		   snd_pcm_format_linear(dst_format->format) != 0,
		   return -ENXIO);

	err = snd_pcm_plugin_build(plug, "attenuated route conversion",
				   src_format, dst_format,
				   sizeof(route_t) + sizeof(data->ttable[0]) * dst_format->channels,
				   &plugin);
	if (err < 0)
		return err;

	data = (route_t *) plugin->extra_data;

	data->get = getput_index(src_format->format);
	snd_assert(data->get >= 0 && data->get < 4*2*2, return -EINVAL);
	data->put = getput_index(dst_format->format);
	snd_assert(data->get >= 0 && data->get < 4*2*2, return -EINVAL);
	data->conv = conv_index(src_format->format, dst_format->format);

	if (snd_pcm_format_width(src_format->format) == 32)
		data->sum_type = R_UINT64;
	else
		data->sum_type = R_UINT32;
	data->src_sample_size = snd_pcm_format_width(src_format->format) / 8;

	if ((err = route_load_ttable(plugin, ttable)) < 0) {
		snd_pcm_plugin_free(plugin);
		return err;
	}
	plugin->transfer = route_transfer;
	plugin->src_channels_mask = route_src_channels_mask;
	plugin->dst_channels_mask = route_dst_channels_mask;
	*r_plugin = plugin;
	return 0;
}
