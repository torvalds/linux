#ifndef __PCM_PLUGIN_H
#define __PCM_PLUGIN_H

/*
 *  Digital Audio (Plugin interface) abstract layer
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#ifndef ATTRIBUTE_UNUSED
#define ATTRIBUTE_UNUSED __attribute__ ((__unused__))
#endif

typedef unsigned int bitset_t;

static inline size_t bitset_size(int nbits)
{
	return (nbits + sizeof(bitset_t) * 8 - 1) / (sizeof(bitset_t) * 8);
}

static inline bitset_t *bitset_alloc(int nbits)
{
	return kcalloc(bitset_size(nbits), sizeof(bitset_t), GFP_KERNEL);
}
	
static inline void bitset_set(bitset_t *bitmap, unsigned int pos)
{
	size_t bits = sizeof(*bitmap) * 8;
	bitmap[pos / bits] |= 1 << (pos % bits);
}

static inline void bitset_reset(bitset_t *bitmap, unsigned int pos)
{
	size_t bits = sizeof(*bitmap) * 8;
	bitmap[pos / bits] &= ~(1 << (pos % bits));
}

static inline int bitset_get(bitset_t *bitmap, unsigned int pos)
{
	size_t bits = sizeof(*bitmap) * 8;
	return !!(bitmap[pos / bits] & (1 << (pos % bits)));
}

static inline void bitset_copy(bitset_t *dst, bitset_t *src, unsigned int nbits)
{
	memcpy(dst, src, bitset_size(nbits) * sizeof(bitset_t));
}

static inline void bitset_and(bitset_t *dst, bitset_t *bs, unsigned int nbits)
{
	bitset_t *end = dst + bitset_size(nbits);
	while (dst < end)
		*dst++ &= *bs++;
}

static inline void bitset_or(bitset_t *dst, bitset_t *bs, unsigned int nbits)
{
	bitset_t *end = dst + bitset_size(nbits);
	while (dst < end)
		*dst++ |= *bs++;
}

static inline void bitset_zero(bitset_t *dst, unsigned int nbits)
{
	bitset_t *end = dst + bitset_size(nbits);
	while (dst < end)
		*dst++ = 0;
}

static inline void bitset_one(bitset_t *dst, unsigned int nbits)
{
	bitset_t *end = dst + bitset_size(nbits);
	while (dst < end)
		*dst++ = ~(bitset_t)0;
}

#define snd_pcm_plug_t snd_pcm_substream_t
#define snd_pcm_plug_stream(plug) ((plug)->stream)

typedef enum {
	INIT = 0,
	PREPARE = 1,
} snd_pcm_plugin_action_t;

typedef struct _snd_pcm_channel_area {
	void *addr;			/* base address of channel samples */
	unsigned int first;		/* offset to first sample in bits */
	unsigned int step;		/* samples distance in bits */
} snd_pcm_channel_area_t;

typedef struct _snd_pcm_plugin_channel {
	void *aptr;			/* pointer to the allocated area */
	snd_pcm_channel_area_t area;
	snd_pcm_uframes_t frames;	/* allocated frames */
	unsigned int enabled:1;		/* channel need to be processed */
	unsigned int wanted:1;		/* channel is wanted */
} snd_pcm_plugin_channel_t;

typedef struct _snd_pcm_plugin_format {
	int format;
	unsigned int rate;
	unsigned int channels;
} snd_pcm_plugin_format_t;

struct _snd_pcm_plugin {
	const char *name;		/* plug-in name */
	int stream;
	snd_pcm_plugin_format_t src_format;	/* source format */
	snd_pcm_plugin_format_t dst_format;	/* destination format */
	int src_width;			/* sample width in bits */
	int dst_width;			/* sample width in bits */
	int access;
	snd_pcm_sframes_t (*src_frames)(snd_pcm_plugin_t *plugin, snd_pcm_uframes_t dst_frames);
	snd_pcm_sframes_t (*dst_frames)(snd_pcm_plugin_t *plugin, snd_pcm_uframes_t src_frames);
	snd_pcm_sframes_t (*client_channels)(snd_pcm_plugin_t *plugin,
				 snd_pcm_uframes_t frames,
				 snd_pcm_plugin_channel_t **channels);
	int (*src_channels_mask)(snd_pcm_plugin_t *plugin,
			       bitset_t *dst_vmask,
			       bitset_t **src_vmask);
	int (*dst_channels_mask)(snd_pcm_plugin_t *plugin,
			       bitset_t *src_vmask,
			       bitset_t **dst_vmask);
	snd_pcm_sframes_t (*transfer)(snd_pcm_plugin_t *plugin,
			    const snd_pcm_plugin_channel_t *src_channels,
			    snd_pcm_plugin_channel_t *dst_channels,
			    snd_pcm_uframes_t frames);
	int (*action)(snd_pcm_plugin_t *plugin,
		      snd_pcm_plugin_action_t action,
		      unsigned long data);
	snd_pcm_plugin_t *prev;
	snd_pcm_plugin_t *next;
	snd_pcm_plug_t *plug;
	void *private_data;
	void (*private_free)(snd_pcm_plugin_t *plugin);
	char *buf;
	snd_pcm_uframes_t buf_frames;
	snd_pcm_plugin_channel_t *buf_channels;
	bitset_t *src_vmask;
	bitset_t *dst_vmask;
	char extra_data[0];
};

int snd_pcm_plugin_build(snd_pcm_plug_t *handle,
                         const char *name,
                         snd_pcm_plugin_format_t *src_format,
                         snd_pcm_plugin_format_t *dst_format,
                         size_t extra,
                         snd_pcm_plugin_t **ret);
int snd_pcm_plugin_free(snd_pcm_plugin_t *plugin);
int snd_pcm_plugin_clear(snd_pcm_plugin_t **first);
int snd_pcm_plug_alloc(snd_pcm_plug_t *plug, snd_pcm_uframes_t frames);
snd_pcm_sframes_t snd_pcm_plug_client_size(snd_pcm_plug_t *handle, snd_pcm_uframes_t drv_size);
snd_pcm_sframes_t snd_pcm_plug_slave_size(snd_pcm_plug_t *handle, snd_pcm_uframes_t clt_size);

#define FULL ROUTE_PLUGIN_RESOLUTION
#define HALF ROUTE_PLUGIN_RESOLUTION / 2
typedef int route_ttable_entry_t;

int snd_pcm_plugin_build_io(snd_pcm_plug_t *handle,
			    snd_pcm_hw_params_t *params,
			    snd_pcm_plugin_t **r_plugin);
int snd_pcm_plugin_build_linear(snd_pcm_plug_t *handle,
				snd_pcm_plugin_format_t *src_format,
				snd_pcm_plugin_format_t *dst_format,
				snd_pcm_plugin_t **r_plugin);
int snd_pcm_plugin_build_mulaw(snd_pcm_plug_t *handle,
			       snd_pcm_plugin_format_t *src_format,
			       snd_pcm_plugin_format_t *dst_format,
			       snd_pcm_plugin_t **r_plugin);
int snd_pcm_plugin_build_rate(snd_pcm_plug_t *handle,
			      snd_pcm_plugin_format_t *src_format,
			      snd_pcm_plugin_format_t *dst_format,
			      snd_pcm_plugin_t **r_plugin);
int snd_pcm_plugin_build_route(snd_pcm_plug_t *handle,
			       snd_pcm_plugin_format_t *src_format,
			       snd_pcm_plugin_format_t *dst_format,
			       route_ttable_entry_t *ttable,
		               snd_pcm_plugin_t **r_plugin);
int snd_pcm_plugin_build_copy(snd_pcm_plug_t *handle,
			      snd_pcm_plugin_format_t *src_format,
			      snd_pcm_plugin_format_t *dst_format,
			      snd_pcm_plugin_t **r_plugin);

int snd_pcm_plug_format_plugins(snd_pcm_plug_t *substream,
				snd_pcm_hw_params_t *params,
				snd_pcm_hw_params_t *slave_params);

int snd_pcm_plug_slave_format(int format, snd_mask_t *format_mask);

int snd_pcm_plugin_append(snd_pcm_plugin_t *plugin);

snd_pcm_sframes_t snd_pcm_plug_write_transfer(snd_pcm_plug_t *handle, snd_pcm_plugin_channel_t *src_channels, snd_pcm_uframes_t size);
snd_pcm_sframes_t snd_pcm_plug_read_transfer(snd_pcm_plug_t *handle, snd_pcm_plugin_channel_t *dst_channels_final, snd_pcm_uframes_t size);

snd_pcm_sframes_t snd_pcm_plug_client_channels_buf(snd_pcm_plug_t *handle,
					 char *buf, snd_pcm_uframes_t count,
					 snd_pcm_plugin_channel_t **channels);

snd_pcm_sframes_t snd_pcm_plugin_client_channels(snd_pcm_plugin_t *plugin,
				       snd_pcm_uframes_t frames,
				       snd_pcm_plugin_channel_t **channels);

int snd_pcm_area_silence(const snd_pcm_channel_area_t *dst_channel, size_t dst_offset,
			 size_t samples, int format);
int snd_pcm_area_copy(const snd_pcm_channel_area_t *src_channel, size_t src_offset,
		      const snd_pcm_channel_area_t *dst_channel, size_t dst_offset,
		      size_t samples, int format);

void *snd_pcm_plug_buf_alloc(snd_pcm_plug_t *plug, snd_pcm_uframes_t size);
void snd_pcm_plug_buf_unlock(snd_pcm_plug_t *plug, void *ptr);
snd_pcm_sframes_t snd_pcm_oss_write3(snd_pcm_substream_t *substream, const char *ptr, snd_pcm_uframes_t size, int in_kernel);
snd_pcm_sframes_t snd_pcm_oss_read3(snd_pcm_substream_t *substream, char *ptr, snd_pcm_uframes_t size, int in_kernel);
snd_pcm_sframes_t snd_pcm_oss_writev3(snd_pcm_substream_t *substream, void **bufs, snd_pcm_uframes_t frames, int in_kernel);
snd_pcm_sframes_t snd_pcm_oss_readv3(snd_pcm_substream_t *substream, void **bufs, snd_pcm_uframes_t frames, int in_kernel);



#define ROUTE_PLUGIN_RESOLUTION 16

int getput_index(int format);
int copy_index(int format);
int conv_index(int src_format, int dst_format);

void zero_channel(snd_pcm_plugin_t *plugin,
		  const snd_pcm_plugin_channel_t *dst_channel,
		  size_t samples);

#ifdef PLUGIN_DEBUG
#define pdprintf( fmt, args... ) printk( "plugin: " fmt, ##args)
#else
#define pdprintf( fmt, args... ) 
#endif

#endif				/* __PCM_PLUGIN_H */
