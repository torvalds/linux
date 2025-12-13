/* SPDX-License-Identifier: GPL-2.0-only */
// Copyright (c) 2025 Šerif Rami <ramiserifpersia@gmail.com>

#ifndef __US144MKII_PCM_H
#define __US144MKII_PCM_H

#include "us144mkii.h"

/**
 * tascam_pcm_hw - Hardware capabilities for TASCAM US-144MKII PCM.
 *
 * Defines the supported PCM formats, rates, channels, and buffer/period sizes
 * for the TASCAM US-144MKII audio interface.
 */
extern const struct snd_pcm_hardware tascam_pcm_hw;

/**
 * tascam_playback_ops - ALSA PCM operations for playback.
 *
 * This structure defines the callback functions for playback stream operations.
 */
extern const struct snd_pcm_ops tascam_playback_ops;

/**
 * tascam_capture_ops - ALSA PCM operations for capture.
 *
 * This structure defines the callback functions for capture stream operations.
 */
extern const struct snd_pcm_ops tascam_capture_ops;

/**
 * playback_urb_complete() - Completion handler for playback isochronous URBs.
 * @urb: the completed URB
 *
 * This function runs in interrupt context. It calculates the number of bytes
 * to send in the next set of packets based on the feedback-driven clock,
 * copies the audio data from the ALSA ring buffer, and resubmits the URB.
 */
void playback_urb_complete(struct urb *urb);

/**
 * feedback_urb_complete() - Completion handler for feedback isochronous URBs.
 * @urb: the completed URB
 *
 * This is the master clock for the driver. It runs in interrupt context.
 * It reads the feedback value from the device, which indicates how many
 * samples the device has consumed. This information is used to adjust the
 * playback rate and to advance the capture stream pointer, keeping both
 * streams in sync. It then calls snd_pcm_period_elapsed if necessary and
 * resubmits itself.
 */
void feedback_urb_complete(struct urb *urb);

/**
 * capture_urb_complete() - Completion handler for capture bulk URBs.
 * @urb: the completed URB
 *
 * This function runs in interrupt context. It copies the received raw data
 * into an intermediate ring buffer and then schedules the workqueue to process
 * it. It then resubmits the URB to receive more data.
 */
void capture_urb_complete(struct urb *urb);

/**
 * tascam_stop_pcm_work_handler() - Work handler to stop PCM streams.
 * @work: Pointer to the work_struct.
 *
 * This function is scheduled to stop PCM streams (playback and capture)
 * from a workqueue context, avoiding blocking operations in interrupt context.
 */
void tascam_stop_pcm_work_handler(struct work_struct *work);

/**
 * tascam_init_pcm() - Initializes the ALSA PCM device.
 * @pcm: Pointer to the ALSA PCM device to initialize.
 *
 * This function sets up the PCM operations, adds ALSA controls for routing
 * and sample rate, and preallocates pages for the PCM buffer.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int tascam_init_pcm(struct snd_pcm *pcm);

/**
 * us144mkii_configure_device_for_rate() - Set sample rate via USB control msgs
 * @tascam: the tascam_card instance
 * @rate: the target sample rate (e.g., 44100, 96000)
 *
 * This function sends a sequence of vendor-specific and UAC control messages
 * to configure the device hardware for the specified sample rate.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int us144mkii_configure_device_for_rate(struct tascam_card *tascam, int rate);

/**
 * process_playback_routing_us144mkii() - Apply playback routing matrix
 * @tascam: The driver instance.
 * @src_buffer: Buffer containing 4 channels of S24_3LE audio from ALSA.
 * @dst_buffer: Buffer to be filled for the USB device.
 * @frames: Number of frames to process.
 */
void process_playback_routing_us144mkii(struct tascam_card *tascam,
					const u8 *src_buffer, u8 *dst_buffer,
					size_t frames);

/**
 * process_capture_routing_us144mkii() - Apply capture routing matrix
 * @tascam: The driver instance.
 * @decoded_block: Buffer containing 4 channels of S32LE decoded audio.
 * @routed_block: Buffer to be filled for ALSA.
 */
void process_capture_routing_us144mkii(struct tascam_card *tascam,
				       const s32 *decoded_block,
				       s32 *routed_block);

/**
 * tascam_pcm_hw_params() - Configures hardware parameters for PCM streams.
 * @substream: The ALSA PCM substream.
 * @params: The hardware parameters to apply.
 *
 * This function allocates pages for the PCM buffer and, for playback streams,
 * selects the appropriate feedback patterns based on the requested sample rate.
 * It also configures the device hardware for the selected sample rate if it
 * has changed.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int tascam_pcm_hw_params(struct snd_pcm_substream *substream,
			 struct snd_pcm_hw_params *params);

/**
 * tascam_pcm_hw_free() - Frees hardware parameters for PCM streams.
 * @substream: The ALSA PCM substream.
 *
 * This function is a stub for freeing hardware-related resources.
 *
 * Return: 0 on success.
 */
int tascam_pcm_hw_free(struct snd_pcm_substream *substream);

/**
 * tascam_pcm_trigger() - Triggers the start or stop of PCM streams.
 * @substream: The ALSA PCM substream.
 * @cmd: The trigger command (e.g., SNDRV_PCM_TRIGGER_START).
 *
 * This function handles starting and stopping of playback and capture streams
 * by submitting or killing the associated URBs.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int tascam_pcm_trigger(struct snd_pcm_substream *substream, int cmd);

/**
 * tascam_capture_work_handler() - Deferred work for processing capture data.
 * @work: the work_struct instance
 *
 * This function runs in a kernel thread context, not an IRQ context. It reads
 * raw data from the capture ring buffer, decodes it, applies routing, and
 * copies the final audio data into the ALSA capture ring buffer. This offloads
 * the CPU-intensive decoding from the time-sensitive URB completion handlers.
 */
void tascam_capture_work_handler(struct work_struct *work);

#endif /* __US144MKII_PCM_H */
