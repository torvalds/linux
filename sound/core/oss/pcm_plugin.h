/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __PCM_PLUGIN_H
#define __PCM_PLUGIN_H

/*
 *  Digital Audio (Plugin interface) abstract layer
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 */

#ifdef CONFIG_SND_PCM_OSS_PLUGINS

#define snd_pcm_plug_stream(plug) ((plug)->stream)

enum snd_pcm_plugin_action {
	INIT = 0,
	PREPARE = 1,
};

struct snd_pcm_channel_area {
	void *addr;			/* base address of channel samples */
	unsigned int first;		/* offset to first sample in bits */
	unsigned int step;		/* samples distance in bits */
};

struct snd_pcm_plugin_channel {
	void *aptr;			/* pointer to the allocated area */
	struct snd_pcm_channel_area area;
	snd_pcm_uframes_t frames;	/* allocated frames */
	unsigned int enabled:1;		/* channel need to be processed */
	unsigned int wanted:1;		/* channel is wanted */
};

struct snd_pcm_plugin_format {
	snd_pcm_format_t format;
	unsigned int rate;
	unsigned int channels;
};

struct snd_pcm_plugin {
	const char *name;		/* plug-in name */
	int stream;
	struct snd_pcm_plugin_format src_format;	/* source format */
	struct snd_pcm_plugin_format dst_format;	/* destination format */
	int src_width;			/* sample width in bits */
	int dst_width;			/* sample width in bits */
	snd_pcm_access_t access;
	snd_pcm_sframes_t (*src_frames)(struct snd_pcm_plugin *plugin, snd_pcm_uframes_t dst_frames);
	snd_pcm_sframes_t (*dst_frames)(struct snd_pcm_plugin *plugin, snd_pcm_uframes_t src_frames);
	snd_pcm_sframes_t (*client_channels)(struct snd_pcm_plugin *plugin,
					     snd_pcm_uframes_t frames,
					     struct snd_pcm_plugin_channel **channels);
	snd_pcm_sframes_t (*transfer)(struct snd_pcm_plugin *plugin,
				      const struct snd_pcm_plugin_channel *src_channels,
				      struct snd_pcm_plugin_channel *dst_channels,
				      snd_pcm_uframes_t frames);
	int (*action)(struct snd_pcm_plugin *plugin,
		      enum snd_pcm_plugin_action action,
		      unsigned long data);
	struct snd_pcm_plugin *prev;
	struct snd_pcm_plugin *next;
	struct snd_pcm_substream *plug;
	void *private_data;
	void (*private_free)(struct snd_pcm_plugin *plugin);
	char *buf;
	snd_pcm_uframes_t buf_frames;
	struct snd_pcm_plugin_channel *buf_channels;
	char extra_data[0];
};

int snd_pcm_plugin_build(struct snd_pcm_substream *handle,
                         const char *name,
                         struct snd_pcm_plugin_format *src_format,
                         struct snd_pcm_plugin_format *dst_format,
                         size_t extra,
                         struct snd_pcm_plugin **ret);
int snd_pcm_plugin_free(struct snd_pcm_plugin *plugin);
int snd_pcm_plugin_clear(struct snd_pcm_plugin **first);
int snd_pcm_plug_alloc(struct snd_pcm_substream *plug, snd_pcm_uframes_t frames);
snd_pcm_sframes_t snd_pcm_plug_client_size(struct snd_pcm_substream *handle, snd_pcm_uframes_t drv_size);
snd_pcm_sframes_t snd_pcm_plug_slave_size(struct snd_pcm_substream *handle, snd_pcm_uframes_t clt_size);

#define FULL ROUTE_PLUGIN_RESOLUTION
#define HALF ROUTE_PLUGIN_RESOLUTION / 2

int snd_pcm_plugin_build_io(struct snd_pcm_substream *handle,
			    struct snd_pcm_hw_params *params,
			    struct snd_pcm_plugin **r_plugin);
int snd_pcm_plugin_build_linear(struct snd_pcm_substream *handle,
				struct snd_pcm_plugin_format *src_format,
				struct snd_pcm_plugin_format *dst_format,
				struct snd_pcm_plugin **r_plugin);
int snd_pcm_plugin_build_mulaw(struct snd_pcm_substream *handle,
			       struct snd_pcm_plugin_format *src_format,
			       struct snd_pcm_plugin_format *dst_format,
			       struct snd_pcm_plugin **r_plugin);
int snd_pcm_plugin_build_rate(struct snd_pcm_substream *handle,
			      struct snd_pcm_plugin_format *src_format,
			      struct snd_pcm_plugin_format *dst_format,
			      struct snd_pcm_plugin **r_plugin);
int snd_pcm_plugin_build_route(struct snd_pcm_substream *handle,
			       struct snd_pcm_plugin_format *src_format,
			       struct snd_pcm_plugin_format *dst_format,
		               struct snd_pcm_plugin **r_plugin);
int snd_pcm_plugin_build_copy(struct snd_pcm_substream *handle,
			      struct snd_pcm_plugin_format *src_format,
			      struct snd_pcm_plugin_format *dst_format,
			      struct snd_pcm_plugin **r_plugin);

int snd_pcm_plug_format_plugins(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_pcm_hw_params *slave_params);

snd_pcm_format_t snd_pcm_plug_slave_format(snd_pcm_format_t format,
					   const struct snd_mask *format_mask);

int snd_pcm_plugin_append(struct snd_pcm_plugin *plugin);

snd_pcm_sframes_t snd_pcm_plug_write_transfer(struct snd_pcm_substream *handle,
					      struct snd_pcm_plugin_channel *src_channels,
					      snd_pcm_uframes_t size);
snd_pcm_sframes_t snd_pcm_plug_read_transfer(struct snd_pcm_substream *handle,
					     struct snd_pcm_plugin_channel *dst_channels_final,
					     snd_pcm_uframes_t size);

snd_pcm_sframes_t snd_pcm_plug_client_channels_buf(struct snd_pcm_substream *handle,
						   char *buf, snd_pcm_uframes_t count,
						   struct snd_pcm_plugin_channel **channels);

snd_pcm_sframes_t snd_pcm_plugin_client_channels(struct snd_pcm_plugin *plugin,
						 snd_pcm_uframes_t frames,
						 struct snd_pcm_plugin_channel **channels);

int snd_pcm_area_silence(const struct snd_pcm_channel_area *dst_channel,
			 size_t dst_offset,
			 size_t samples, snd_pcm_format_t format);
int snd_pcm_area_copy(const struct snd_pcm_channel_area *src_channel,
		      size_t src_offset,
		      const struct snd_pcm_channel_area *dst_channel,
		      size_t dst_offset,
		      size_t samples, snd_pcm_format_t format);

void *snd_pcm_plug_buf_alloc(struct snd_pcm_substream *plug, snd_pcm_uframes_t size);
void snd_pcm_plug_buf_unlock(struct snd_pcm_substream *plug, void *ptr);
snd_pcm_sframes_t snd_pcm_oss_write3(struct snd_pcm_substream *substream,
				     const char *ptr, snd_pcm_uframes_t size,
				     int in_kernel);
snd_pcm_sframes_t snd_pcm_oss_read3(struct snd_pcm_substream *substream,
				    char *ptr, snd_pcm_uframes_t size, int in_kernel);
snd_pcm_sframes_t snd_pcm_oss_writev3(struct snd_pcm_substream *substream,
				      void **bufs, snd_pcm_uframes_t frames);
snd_pcm_sframes_t snd_pcm_oss_readv3(struct snd_pcm_substream *substream,
				     void **bufs, snd_pcm_uframes_t frames);

#else

static inline snd_pcm_sframes_t snd_pcm_plug_client_size(struct snd_pcm_substream *handle, snd_pcm_uframes_t drv_size) { return drv_size; }
static inline snd_pcm_sframes_t snd_pcm_plug_slave_size(struct snd_pcm_substream *handle, snd_pcm_uframes_t clt_size) { return clt_size; }
static inline int snd_pcm_plug_slave_format(int format, const struct snd_mask *format_mask) { return format; }

#endif

#ifdef PLUGIN_DEBUG
#define pdprintf(fmt, args...) printk(KERN_DEBUG "plugin: " fmt, ##args)
#else
#define pdprintf(fmt, args...)
#endif

#endif				/* __PCM_PLUGIN_H */
