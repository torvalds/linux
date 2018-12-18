/* SPDX-License-Identifier: GPL-2.0
 *
 *  compress_driver.h - compress offload driver definations
 *
 *  Copyright (C) 2011 Intel Corporation
 *  Authors:	Vinod Koul <vinod.koul@linux.intel.com>
 *		Pierre-Louis Bossart <pierre-louis.bossart@linux.intel.com>
 */

#ifndef __COMPRESS_DRIVER_H
#define __COMPRESS_DRIVER_H

#include <linux/types.h>
#include <linux/sched.h>
#include <sound/core.h>
#include <sound/compress_offload.h>
#include <sound/asound.h>
#include <sound/pcm.h>

struct snd_compr_ops;

/**
 * struct snd_compr_runtime: runtime stream description
 * @state: stream state
 * @ops: pointer to DSP callbacks
 * @dma_buffer_p: runtime dma buffer pointer
 * @buffer: pointer to kernel buffer, valid only when not in mmap mode or
 *	DSP doesn't implement copy
 * @buffer_size: size of the above buffer
 * @fragment_size: size of buffer fragment in bytes
 * @fragments: number of such fragments
 * @total_bytes_available: cumulative number of bytes made available in
 *	the ring buffer
 * @total_bytes_transferred: cumulative bytes transferred by offload DSP
 * @sleep: poll sleep
 * @private_data: driver private data pointer
 */
struct snd_compr_runtime {
	snd_pcm_state_t state;
	struct snd_compr_ops *ops;
	struct snd_dma_buffer *dma_buffer_p;
	void *buffer;
	u64 buffer_size;
	u32 fragment_size;
	u32 fragments;
	u64 total_bytes_available;
	u64 total_bytes_transferred;
	wait_queue_head_t sleep;
	void *private_data;
};

/**
 * struct snd_compr_stream: compressed stream
 * @name: device name
 * @ops: pointer to DSP callbacks
 * @runtime: pointer to runtime structure
 * @device: device pointer
 * @error_work: delayed work used when closing the stream due to an error
 * @direction: stream direction, playback/recording
 * @metadata_set: metadata set flag, true when set
 * @next_track: has userspace signal next track transition, true when set
 * @private_data: pointer to DSP private data
 */
struct snd_compr_stream {
	const char *name;
	struct snd_compr_ops *ops;
	struct snd_compr_runtime *runtime;
	struct snd_compr *device;
	struct delayed_work error_work;
	enum snd_compr_direction direction;
	bool metadata_set;
	bool next_track;
	void *private_data;
};

/**
 * struct snd_compr_ops: compressed path DSP operations
 * @open: Open the compressed stream
 * This callback is mandatory and shall keep dsp ready to receive the stream
 * parameter
 * @free: Close the compressed stream, mandatory
 * @set_params: Sets the compressed stream parameters, mandatory
 * This can be called in during stream creation only to set codec params
 * and the stream properties
 * @get_params: retrieve the codec parameters, mandatory
 * @set_metadata: Set the metadata values for a stream
 * @get_metadata: retrieves the requested metadata values from stream
 * @trigger: Trigger operations like start, pause, resume, drain, stop.
 * This callback is mandatory
 * @pointer: Retrieve current h/w pointer information. Mandatory
 * @copy: Copy the compressed data to/from userspace, Optional
 * Can't be implemented if DSP supports mmap
 * @mmap: DSP mmap method to mmap DSP memory
 * @ack: Ack for DSP when data is written to audio buffer, Optional
 * Not valid if copy is implemented
 * @get_caps: Retrieve DSP capabilities, mandatory
 * @get_codec_caps: Retrieve capabilities for a specific codec, mandatory
 */
struct snd_compr_ops {
	int (*open)(struct snd_compr_stream *stream);
	int (*free)(struct snd_compr_stream *stream);
	int (*set_params)(struct snd_compr_stream *stream,
			struct snd_compr_params *params);
	int (*get_params)(struct snd_compr_stream *stream,
			struct snd_codec *params);
	int (*set_metadata)(struct snd_compr_stream *stream,
			struct snd_compr_metadata *metadata);
	int (*get_metadata)(struct snd_compr_stream *stream,
			struct snd_compr_metadata *metadata);
	int (*trigger)(struct snd_compr_stream *stream, int cmd);
	int (*pointer)(struct snd_compr_stream *stream,
			struct snd_compr_tstamp *tstamp);
	int (*copy)(struct snd_compr_stream *stream, char __user *buf,
		       size_t count);
	int (*mmap)(struct snd_compr_stream *stream,
			struct vm_area_struct *vma);
	int (*ack)(struct snd_compr_stream *stream, size_t bytes);
	int (*get_caps) (struct snd_compr_stream *stream,
			struct snd_compr_caps *caps);
	int (*get_codec_caps) (struct snd_compr_stream *stream,
			struct snd_compr_codec_caps *codec);
};

/**
 * struct snd_compr: Compressed device
 * @name: DSP device name
 * @dev: associated device instance
 * @ops: pointer to DSP callbacks
 * @private_data: pointer to DSP pvt data
 * @card: sound card pointer
 * @direction: Playback or capture direction
 * @lock: device lock
 * @device: device id
 */
struct snd_compr {
	const char *name;
	struct device dev;
	struct snd_compr_ops *ops;
	void *private_data;
	struct snd_card *card;
	unsigned int direction;
	struct mutex lock;
	int device;
#ifdef CONFIG_SND_VERBOSE_PROCFS
	/* private: */
	char id[64];
	struct snd_info_entry *proc_root;
	struct snd_info_entry *proc_info_entry;
#endif
};

/* compress device register APIs */
int snd_compress_register(struct snd_compr *device);
int snd_compress_deregister(struct snd_compr *device);
int snd_compress_new(struct snd_card *card, int device,
			int type, const char *id, struct snd_compr *compr);

/* dsp driver callback apis
 * For playback: driver should call snd_compress_fragment_elapsed() to let the
 * framework know that a fragment has been consumed from the ring buffer
 *
 * For recording: we want to know when a frame is available or when
 * at least one frame is available so snd_compress_frame_elapsed()
 * callback should be called when a encodeded frame is available
 */
static inline void snd_compr_fragment_elapsed(struct snd_compr_stream *stream)
{
	wake_up(&stream->runtime->sleep);
}

static inline void snd_compr_drain_notify(struct snd_compr_stream *stream)
{
	if (snd_BUG_ON(!stream))
		return;

	stream->runtime->state = SNDRV_PCM_STATE_SETUP;
	wake_up(&stream->runtime->sleep);
}

/**
 * snd_compr_set_runtime_buffer - Set the Compress runtime buffer
 * @substream: compress substream to set
 * @bufp: the buffer information, NULL to clear
 *
 * Copy the buffer information to runtime buffer when @bufp is non-NULL.
 * Otherwise it clears the current buffer information.
 */
static inline void snd_compr_set_runtime_buffer(
					struct snd_compr_stream *substream,
					struct snd_dma_buffer *bufp)
{
	struct snd_compr_runtime *runtime = substream->runtime;

	runtime->dma_buffer_p = bufp;
}

int snd_compr_stop_error(struct snd_compr_stream *stream,
			 snd_pcm_state_t state);

#endif
