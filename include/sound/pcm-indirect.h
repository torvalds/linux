/*
 * Helper functions for indirect PCM data transfer
 *
 *  Copyright (c) by Takashi Iwai <tiwai@suse.de>
 *                   Jaroslav Kysela <perex@suse.cz>
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
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __SOUND_PCM_INDIRECT_H
#define __SOUND_PCM_INDIRECT_H

#include <sound/pcm.h>

typedef struct sndrv_pcm_indirect {
	unsigned int hw_buffer_size;	/* Byte size of hardware buffer */
	unsigned int hw_queue_size;	/* Max queue size of hw buffer (0 = buffer size) */
	unsigned int hw_data;	/* Offset to next dst (or src) in hw ring buffer */
	unsigned int hw_io;	/* Ring buffer hw pointer */
	int hw_ready;		/* Bytes ready for play (or captured) in hw ring buffer */
	unsigned int sw_buffer_size;	/* Byte size of software buffer */
	unsigned int sw_data;	/* Offset to next dst (or src) in sw ring buffer */
	unsigned int sw_io;	/* Current software pointer in bytes */
	int sw_ready;		/* Bytes ready to be transferred to/from hw */
	snd_pcm_uframes_t appl_ptr;	/* Last seen appl_ptr */
} snd_pcm_indirect_t;

typedef void (*snd_pcm_indirect_copy_t)(snd_pcm_substream_t *substream,
					snd_pcm_indirect_t *rec, size_t bytes);

/*
 * helper function for playback ack callback
 */
static inline void
snd_pcm_indirect_playback_transfer(snd_pcm_substream_t *substream,
				   snd_pcm_indirect_t *rec,
				   snd_pcm_indirect_copy_t copy)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_pcm_uframes_t appl_ptr = runtime->control->appl_ptr;
	snd_pcm_sframes_t diff = appl_ptr - rec->appl_ptr;
	int qsize;

	if (diff) {
		if (diff < -(snd_pcm_sframes_t) (runtime->boundary / 2))
			diff += runtime->boundary;
		rec->sw_ready += (int)frames_to_bytes(runtime, diff);
		rec->appl_ptr = appl_ptr;
	}
	qsize = rec->hw_queue_size ? rec->hw_queue_size : rec->hw_buffer_size;
	while (rec->hw_ready < qsize && rec->sw_ready > 0) {
		unsigned int hw_to_end = rec->hw_buffer_size - rec->hw_data;
		unsigned int sw_to_end = rec->sw_buffer_size - rec->sw_data;
		unsigned int bytes = qsize - rec->hw_ready;
		if (rec->sw_ready < (int)bytes)
			bytes = rec->sw_ready;
		if (hw_to_end < bytes)
			bytes = hw_to_end;
		if (sw_to_end < bytes)
			bytes = sw_to_end;
		if (! bytes)
			break;
		copy(substream, rec, bytes);
		rec->hw_data += bytes;
		if (rec->hw_data == rec->hw_buffer_size)
			rec->hw_data = 0;
		rec->sw_data += bytes;
		if (rec->sw_data == rec->sw_buffer_size)
			rec->sw_data = 0;
		rec->hw_ready += bytes;
		rec->sw_ready -= bytes;
	}
}

/*
 * helper function for playback pointer callback
 * ptr = current byte pointer
 */
static inline snd_pcm_uframes_t
snd_pcm_indirect_playback_pointer(snd_pcm_substream_t *substream,
				  snd_pcm_indirect_t *rec, unsigned int ptr)
{
	int bytes = ptr - rec->hw_io;
	if (bytes < 0)
		bytes += rec->hw_buffer_size;
	rec->hw_io = ptr;
	rec->hw_ready -= bytes;
	rec->sw_io += bytes;
	if (rec->sw_io >= rec->sw_buffer_size)
		rec->sw_io -= rec->sw_buffer_size;
	if (substream->ops->ack)
		substream->ops->ack(substream);
	return bytes_to_frames(substream->runtime, rec->sw_io);
}


/*
 * helper function for capture ack callback
 */
static inline void
snd_pcm_indirect_capture_transfer(snd_pcm_substream_t *substream,
				  snd_pcm_indirect_t *rec,
				  snd_pcm_indirect_copy_t copy)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_pcm_uframes_t appl_ptr = runtime->control->appl_ptr;
	snd_pcm_sframes_t diff = appl_ptr - rec->appl_ptr;

	if (diff) {
		if (diff < -(snd_pcm_sframes_t) (runtime->boundary / 2))
			diff += runtime->boundary;
		rec->sw_ready -= frames_to_bytes(runtime, diff);
		rec->appl_ptr = appl_ptr;
	}
	while (rec->hw_ready > 0 && 
	       rec->sw_ready < (int)rec->sw_buffer_size) {
		size_t hw_to_end = rec->hw_buffer_size - rec->hw_data;
		size_t sw_to_end = rec->sw_buffer_size - rec->sw_data;
		size_t bytes = rec->sw_buffer_size - rec->sw_ready;
		if (rec->hw_ready < (int)bytes)
			bytes = rec->hw_ready;
		if (hw_to_end < bytes)
			bytes = hw_to_end;
		if (sw_to_end < bytes)
			bytes = sw_to_end;
		if (! bytes)
			break;
		copy(substream, rec, bytes);
		rec->hw_data += bytes;
		if ((int)rec->hw_data == rec->hw_buffer_size)
			rec->hw_data = 0;
		rec->sw_data += bytes;
		if (rec->sw_data == rec->sw_buffer_size)
			rec->sw_data = 0;
		rec->hw_ready -= bytes;
		rec->sw_ready += bytes;
	}
}

/*
 * helper function for capture pointer callback,
 * ptr = current byte pointer
 */
static inline snd_pcm_uframes_t
snd_pcm_indirect_capture_pointer(snd_pcm_substream_t *substream,
				 snd_pcm_indirect_t *rec, unsigned int ptr)
{
	int qsize;
	int bytes = ptr - rec->hw_io;
	if (bytes < 0)
		bytes += rec->hw_buffer_size;
	rec->hw_io = ptr;
	rec->hw_ready += bytes;
	qsize = rec->hw_queue_size ? rec->hw_queue_size : rec->hw_buffer_size;
	if (rec->hw_ready > qsize)
		return SNDRV_PCM_POS_XRUN;
	rec->sw_io += bytes;
	if (rec->sw_io >= rec->sw_buffer_size)
		rec->sw_io -= rec->sw_buffer_size;
	if (substream->ops->ack)
		substream->ops->ack(substream);
	return bytes_to_frames(substream->runtime, rec->sw_io);
}

#endif /* __SOUND_PCM_INDIRECT_H */
