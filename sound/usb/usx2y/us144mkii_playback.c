// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2025 Šerif Rami <ramiserifpersia@gmail.com>

#include "us144mkii.h"

/**
 * tascam_playback_open() - Opens the PCM playback substream.
 * @substream: The ALSA PCM substream to open.
 *
 * This function sets the hardware parameters for the playback substream
 * and stores a reference to the substream in the driver's private data.
 *
 * Return: 0 on success.
 */
static int tascam_playback_open(struct snd_pcm_substream *substream)
{
	struct tascam_card *tascam = snd_pcm_substream_chip(substream);

	substream->runtime->hw = tascam_pcm_hw;
	tascam->playback_substream = substream;
	atomic_set(&tascam->playback_active, 0);

	return 0;
}

/**
 * tascam_playback_close() - Closes the PCM playback substream.
 * @substream: The ALSA PCM substream to close.
 *
 * This function clears the reference to the playback substream in the
 * driver's private data.
 *
 * Return: 0 on success.
 */
static int tascam_playback_close(struct snd_pcm_substream *substream)
{
	struct tascam_card *tascam = snd_pcm_substream_chip(substream);

	tascam->playback_substream = NULL;

	return 0;
}

/**
 * tascam_playback_prepare() - Prepares the PCM playback substream for use.
 * @substream: The ALSA PCM substream to prepare.
 *
 * This function initializes playback-related counters and flags, and configures
 * the playback URBs with appropriate packet sizes based on the nominal frame
 * rate.
 *
 * Return: 0 on success.
 */
static int tascam_playback_prepare(struct snd_pcm_substream *substream)
{
	struct tascam_card *tascam = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	int i, u;
	size_t nominal_frames_per_packet, nominal_bytes_per_packet;
	size_t total_bytes_in_urb;

	tascam->driver_playback_pos = 0;
	tascam->playback_frames_consumed = 0;
	tascam->last_period_pos = 0;
	tascam->feedback_pattern_in_idx = 0;
	tascam->feedback_pattern_out_idx = 0;
	tascam->feedback_synced = false;
	tascam->feedback_consecutive_errors = 0;
	tascam->feedback_urb_skip_count = NUM_FEEDBACK_URBS;

	nominal_frames_per_packet = runtime->rate / 8000;
	for (i = 0; i < FEEDBACK_ACCUMULATOR_SIZE; i++)
		tascam->feedback_accumulator_pattern[i] =
			nominal_frames_per_packet;

	for (i = 0; i < NUM_FEEDBACK_URBS; i++) {
		struct urb *f_urb = tascam->feedback_urbs[i];
		int j;

		f_urb->number_of_packets = FEEDBACK_URB_PACKETS;
		f_urb->transfer_buffer_length =
			FEEDBACK_URB_PACKETS * FEEDBACK_PACKET_SIZE;
		for (j = 0; j < FEEDBACK_URB_PACKETS; j++) {
			f_urb->iso_frame_desc[j].offset =
				j * FEEDBACK_PACKET_SIZE;
			f_urb->iso_frame_desc[j].length = FEEDBACK_PACKET_SIZE;
		}
	}

	nominal_bytes_per_packet = nominal_frames_per_packet * BYTES_PER_FRAME;
	total_bytes_in_urb = nominal_bytes_per_packet * PLAYBACK_URB_PACKETS;

	for (u = 0; u < NUM_PLAYBACK_URBS; u++) {
		struct urb *urb = tascam->playback_urbs[u];

		memset(urb->transfer_buffer, 0,
		       tascam->playback_urb_alloc_size);
		urb->transfer_buffer_length = total_bytes_in_urb;
		urb->number_of_packets = PLAYBACK_URB_PACKETS;
		for (i = 0; i < PLAYBACK_URB_PACKETS; i++) {
			urb->iso_frame_desc[i].offset =
				i * nominal_bytes_per_packet;
			urb->iso_frame_desc[i].length =
				nominal_bytes_per_packet;
		}
	}

	return 0;
}

/**
 * tascam_playback_pointer() - Returns the current playback pointer position.
 * @substream: The ALSA PCM substream.
 *
 * This function returns the current position of the playback pointer within
 * the ALSA ring buffer, in frames.
 *
 * Return: The current playback pointer position in frames.
 */
static snd_pcm_uframes_t
tascam_playback_pointer(struct snd_pcm_substream *substream)
{
	struct tascam_card *tascam = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	u64 pos;

	if (!atomic_read(&tascam->playback_active))
		return 0;

	scoped_guard(spinlock_irqsave, &tascam->lock) {
		pos = tascam->playback_frames_consumed;
	}

	if (runtime->buffer_size == 0)
		return 0;

	return do_div(pos, runtime->buffer_size);
}

/**
 * tascam_playback_ops - ALSA PCM operations for playback.
 *
 * This structure defines the callback functions for playback stream operations,
 * including open, close, ioctl, hardware parameters, hardware free, prepare,
 * trigger, and pointer.
 */
const struct snd_pcm_ops tascam_playback_ops = {
	.open = tascam_playback_open,
	.close = tascam_playback_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = tascam_pcm_hw_params,
	.hw_free = tascam_pcm_hw_free,
	.prepare = tascam_playback_prepare,
	.trigger = tascam_pcm_trigger,
	.pointer = tascam_playback_pointer,
};

void playback_urb_complete(struct urb *urb)
{
	struct tascam_card *tascam = urb->context;
	struct snd_pcm_substream *substream;
	struct snd_pcm_runtime *runtime;
	size_t total_bytes_for_urb = 0;
	snd_pcm_uframes_t offset_frames;
	snd_pcm_uframes_t frames_to_copy;
	int ret, i;

	if (urb->status) {
		if (urb->status != -ENOENT && urb->status != -ECONNRESET &&
		    urb->status != -ESHUTDOWN && urb->status != -ENODEV)
			dev_err_ratelimited(tascam->card->dev,
					    "Playback URB failed: %d\n",
					    urb->status);
		goto out;
	}
	if (!tascam || !atomic_read(&tascam->playback_active))
		goto out;

	substream = tascam->playback_substream;
	if (!substream || !substream->runtime)
		goto out;
	runtime = substream->runtime;

	scoped_guard(spinlock_irqsave, &tascam->lock) {
		for (i = 0; i < urb->number_of_packets; i++) {
			unsigned int frames_for_packet;
			size_t bytes_for_packet;

			if (tascam->feedback_synced) {
				frames_for_packet =
					tascam->feedback_accumulator_pattern
						[tascam->feedback_pattern_out_idx];
				tascam->feedback_pattern_out_idx =
					(tascam->feedback_pattern_out_idx + 1) %
					FEEDBACK_ACCUMULATOR_SIZE;
			} else {
				frames_for_packet = runtime->rate / 8000;
			}
			bytes_for_packet = frames_for_packet * BYTES_PER_FRAME;

			urb->iso_frame_desc[i].offset = total_bytes_for_urb;
			urb->iso_frame_desc[i].length = bytes_for_packet;
			total_bytes_for_urb += bytes_for_packet;
		}
		urb->transfer_buffer_length = total_bytes_for_urb;

		offset_frames = tascam->driver_playback_pos;
		frames_to_copy = bytes_to_frames(runtime, total_bytes_for_urb);
		tascam->driver_playback_pos =
			(offset_frames + frames_to_copy) % runtime->buffer_size;
	}

	if (total_bytes_for_urb > 0) {
		u8 *dst_buf = urb->transfer_buffer;

		/* Handle ring buffer wrap-around */
		if (offset_frames + frames_to_copy > runtime->buffer_size) {
			size_t first_chunk_bytes = frames_to_bytes(
				runtime, runtime->buffer_size - offset_frames);
			size_t second_chunk_bytes =
				total_bytes_for_urb - first_chunk_bytes;

			memcpy(dst_buf,
			       runtime->dma_area +
				       frames_to_bytes(runtime, offset_frames),
			       first_chunk_bytes);
			memcpy(dst_buf + first_chunk_bytes, runtime->dma_area,
			       second_chunk_bytes);
		} else {
			memcpy(dst_buf,
			       runtime->dma_area +
				       frames_to_bytes(runtime, offset_frames),
			       total_bytes_for_urb);
		}

		process_playback_routing_us144mkii(tascam, dst_buf, dst_buf,
						   frames_to_copy);
	}

	urb->dev = tascam->dev;
	usb_get_urb(urb);
	usb_anchor_urb(urb, &tascam->playback_anchor);
	ret = usb_submit_urb(urb, GFP_ATOMIC);
	if (ret < 0) {
		dev_err_ratelimited(tascam->card->dev,
				    "Failed to resubmit playback URB: %d\n",
				    ret);
		usb_unanchor_urb(urb);
		usb_put_urb(urb);
		atomic_dec(
			&tascam->active_urbs); /* Decrement on failed resubmission */
	}
out:
	usb_put_urb(urb);
}

void feedback_urb_complete(struct urb *urb)
{
	struct tascam_card *tascam = urb->context;
	struct snd_pcm_substream *playback_ss, *capture_ss;
	struct snd_pcm_runtime *playback_rt, *capture_rt;
	u64 total_frames_in_urb = 0;
	int ret, p;
	unsigned int old_in_idx, new_in_idx;
	bool playback_period_elapsed = false;
	bool capture_period_elapsed = false;

	if (urb->status) {
		if (urb->status != -ENOENT && urb->status != -ECONNRESET &&
		    urb->status != -ESHUTDOWN && urb->status != -ENODEV) {
			dev_err_ratelimited(tascam->card->dev,
					    "Feedback URB failed: %d\n",
					    urb->status);
			atomic_dec(
				&tascam->active_urbs); /* Decrement on failed resubmission */
		}
		goto out;
	}
	if (!tascam || !atomic_read(&tascam->playback_active))
		goto out;

	playback_ss = tascam->playback_substream;
	if (!playback_ss || !playback_ss->runtime)
		goto out;
	playback_rt = playback_ss->runtime;

	capture_ss = tascam->capture_substream;
	capture_rt = capture_ss ? capture_ss->runtime : NULL;

	scoped_guard(spinlock_irqsave, &tascam->lock) {
		if (tascam->feedback_urb_skip_count > 0) {
			tascam->feedback_urb_skip_count--;
			break;
		}

		old_in_idx = tascam->feedback_pattern_in_idx;

		for (p = 0; p < urb->number_of_packets; p++) {
			u8 feedback_value = 0;
			const unsigned int *pattern;
			bool packet_ok =
				(urb->iso_frame_desc[p].status == 0 &&
				 urb->iso_frame_desc[p].actual_length >= 1);

			if (packet_ok)
				feedback_value =
					*((u8 *)urb->transfer_buffer +
					  urb->iso_frame_desc[p].offset);

			if (packet_ok) {
				int delta = feedback_value -
						    tascam->fpo.base_feedback_value +
						    tascam->fpo.feedback_offset;
				int pattern_idx;

				if (delta < 0) {
					pattern_idx =
						0; // Clamp to the lowest pattern
				} else if (delta >= 5) {
					pattern_idx =
						4; // Clamp to the highest pattern
				} else {
					pattern_idx = delta;
				}

				pattern =
					tascam->fpo
						.full_frame_patterns[pattern_idx];
				tascam->feedback_consecutive_errors = 0;
				int i;

				for (i = 0; i < 8; i++) {
					unsigned int in_idx =
						(tascam->feedback_pattern_in_idx +
						 i) %
						FEEDBACK_ACCUMULATOR_SIZE;

					tascam->feedback_accumulator_pattern
						[in_idx] = pattern[i];
					total_frames_in_urb += pattern[i];
				}
			} else {
				unsigned int nominal_frames =
					playback_rt->rate / 8000;
				int i;

				if (tascam->feedback_synced) {
					tascam->feedback_consecutive_errors++;
					if (tascam->feedback_consecutive_errors >
					    FEEDBACK_SYNC_LOSS_THRESHOLD) {
						dev_err(tascam->card->dev,
							"Fatal: Feedback sync lost. Stopping stream.\n");
						schedule_work(
								&tascam->stop_pcm_work);
						tascam->feedback_synced = false;
						break;
					}
				}
				for (i = 0; i < 8; i++) {
					unsigned int in_idx =
						(tascam->feedback_pattern_in_idx +
						 i) %
						FEEDBACK_ACCUMULATOR_SIZE;

					tascam->feedback_accumulator_pattern
						[in_idx] = nominal_frames;
					total_frames_in_urb += nominal_frames;
				}
			}
			tascam->feedback_pattern_in_idx =
				(tascam->feedback_pattern_in_idx + 8) %
				FEEDBACK_ACCUMULATOR_SIZE;
		}

		new_in_idx = tascam->feedback_pattern_in_idx;

		if (!tascam->feedback_synced) {
			unsigned int out_idx = tascam->feedback_pattern_out_idx;
			bool is_ahead = (new_in_idx - out_idx) %
						FEEDBACK_ACCUMULATOR_SIZE <
					(FEEDBACK_ACCUMULATOR_SIZE / 2);
			bool was_behind = (old_in_idx - out_idx) %
						FEEDBACK_ACCUMULATOR_SIZE >=
					(FEEDBACK_ACCUMULATOR_SIZE / 2);

			if (is_ahead && was_behind) {
				dev_dbg(tascam->card->dev,
					"Sync Acquired! (in: %u, out: %u)\n",
					new_in_idx, out_idx);
				tascam->feedback_synced = true;
				tascam->feedback_consecutive_errors = 0;
			}
		}

		if (total_frames_in_urb > 0) {
			tascam->playback_frames_consumed += total_frames_in_urb;
			if (atomic_read(&tascam->capture_active))
				tascam->capture_frames_processed +=
					total_frames_in_urb;
		}

		if (playback_rt->period_size > 0) {
			u64 current_period =
				div_u64(tascam->playback_frames_consumed,
						playback_rt->period_size);

			if (current_period > tascam->last_period_pos) {
				tascam->last_period_pos = current_period;
				playback_period_elapsed = true;
			}
		}

		if (atomic_read(&tascam->capture_active) && capture_rt &&
		    capture_rt->period_size > 0) {
			u64 current_capture_period =
				div_u64(tascam->capture_frames_processed,
						capture_rt->period_size);

			if (current_capture_period >
			    tascam->last_capture_period_pos) {
				tascam->last_capture_period_pos =
					current_capture_period;
				capture_period_elapsed = true;
			}
		}
	}
	if (playback_period_elapsed)
		snd_pcm_period_elapsed(playback_ss);
	if (capture_period_elapsed)
		snd_pcm_period_elapsed(capture_ss);

	urb->dev = tascam->dev;
	usb_get_urb(urb);
	usb_anchor_urb(urb, &tascam->feedback_anchor);
	ret = usb_submit_urb(urb, GFP_ATOMIC);
	if (ret < 0) {
		dev_err_ratelimited(tascam->card->dev,
				    "Failed to resubmit feedback URB: %d\n",
				    ret);
		usb_unanchor_urb(urb);
		usb_put_urb(urb);
	}
out:
	usb_put_urb(urb);
}

void tascam_stop_pcm_work_handler(struct work_struct *work)
{
	struct tascam_card *tascam =
		container_of(work, struct tascam_card, stop_pcm_work);

	if (tascam->playback_substream)
		snd_pcm_stop(tascam->playback_substream, SNDRV_PCM_STATE_XRUN);
	if (tascam->capture_substream)
		snd_pcm_stop(tascam->capture_substream, SNDRV_PCM_STATE_XRUN);
}
