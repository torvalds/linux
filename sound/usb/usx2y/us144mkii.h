/* SPDX-License-Identifier: GPL-2.0-only */
// Copyright (c) 2025 Šerif Rami <ramiserifpersia@gmail.com>

#ifndef __US144MKII_H
#define __US144MKII_H

#include <linux/kfifo.h>
#include <linux/timer.h>
#include <linux/usb.h>
#include <linux/workqueue.h>
#include <sound/control.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/rawmidi.h>

#define DRIVER_NAME "us144mkii"

/* --- USB Device Identification --- */
#define USB_VID_TASCAM 0x0644
#define USB_PID_TASCAM_US144 0x800f
#define USB_PID_TASCAM_US144MKII 0x8020

/* --- USB Endpoints (Alternate Setting 1) --- */
#define EP_PLAYBACK_FEEDBACK 0x81
#define EP_AUDIO_OUT 0x02
#define EP_MIDI_IN 0x83
#define EP_MIDI_OUT 0x04
#define EP_AUDIO_IN 0x86

/* --- USB Control Message Protocol --- */
#define RT_H2D_CLASS_EP (USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_ENDPOINT)
#define RT_D2H_CLASS_EP (USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_ENDPOINT)
#define RT_H2D_VENDOR_DEV (USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE)
#define RT_D2H_VENDOR_DEV (USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE)

enum uac_request {
	UAC_SET_CUR = 0x01,
	UAC_GET_CUR = 0x81,
};

enum uac_control_selector {
	UAC_SAMPLING_FREQ_CONTROL = 0x0100,
};

enum tascam_vendor_request {
	VENDOR_REQ_REGISTER_WRITE = 0x41,
	VENDOR_REQ_DEEP_SLEEP = 0x44,
	VENDOR_REQ_MODE_CONTROL = 0x49,
};

enum tascam_mode_value {
	MODE_VAL_HANDSHAKE_READ = 0x0000,
	MODE_VAL_CONFIG = 0x0010,
	MODE_VAL_STREAM_START = 0x0030,
};

#define HANDSHAKE_SUCCESS_VAL 0x12

enum tascam_register {
	REG_ADDR_UNKNOWN_0D = 0x0d04,
	REG_ADDR_UNKNOWN_0E = 0x0e00,
	REG_ADDR_UNKNOWN_0F = 0x0f00,
	REG_ADDR_RATE_44100 = 0x1000,
	REG_ADDR_RATE_48000 = 0x1002,
	REG_ADDR_RATE_88200 = 0x1008,
	REG_ADDR_RATE_96000 = 0x100a,
	REG_ADDR_UNKNOWN_11 = 0x110b,
};

#define REG_VAL_ENABLE 0x0101

/* --- URB Configuration --- */
#define NUM_PLAYBACK_URBS 4
#define PLAYBACK_URB_PACKETS 8
#define NUM_FEEDBACK_URBS 4
#define FEEDBACK_URB_PACKETS 1
#define FEEDBACK_PACKET_SIZE 3
#define NUM_CAPTURE_URBS 8
#define CAPTURE_URB_SIZE 512
#define CAPTURE_RING_BUFFER_SIZE (CAPTURE_URB_SIZE * NUM_CAPTURE_URBS * 4)
#define NUM_MIDI_IN_URBS 4
#define MIDI_IN_BUF_SIZE 64
#define MIDI_IN_FIFO_SIZE (MIDI_IN_BUF_SIZE * NUM_MIDI_IN_URBS)
#define MIDI_OUT_BUF_SIZE 64
#define NUM_MIDI_OUT_URBS 4
#define USB_CTRL_TIMEOUT_MS 1000
#define FEEDBACK_SYNC_LOSS_THRESHOLD 41

/* --- Audio Format Configuration --- */
#define BYTES_PER_SAMPLE 3
#define NUM_CHANNELS 4
#define BYTES_PER_FRAME (NUM_CHANNELS * BYTES_PER_SAMPLE)
#define FEEDBACK_ACCUMULATOR_SIZE 128

/* --- Capture Decoding Defines --- */
#define DECODED_CHANNELS_PER_FRAME 4
#define DECODED_SAMPLE_SIZE 4
#define FRAMES_PER_DECODE_BLOCK 8
#define RAW_BYTES_PER_DECODE_BLOCK 512

/**
 * struct us144mkii_frame_pattern_observer - State for dynamic feedback
 * patterns.
 * @sample_rate_khz: The current sample rate in kHz.
 * @base_feedback_value: The nominal feedback value for the current rate.
 * @feedback_offset: An offset to align the feedback value range.
 * @full_frame_patterns: A 2D array of pre-calculated packet size patterns.
 * @current_index: The current index into the pattern array.
 * @previous_index: The previous index, used for state tracking.
 * @sync_locked: A flag indicating if the pattern has locked to the stream.
 */
struct us144mkii_frame_pattern_observer {
	unsigned int sample_rate_khz;
	unsigned int base_feedback_value;
	int feedback_offset;
	unsigned int full_frame_patterns[5][8];
	unsigned int current_index;
	unsigned int previous_index;
	bool sync_locked;
};

/**
 * struct tascam_card - Main driver data structure for the TASCAM US-144MKII.
 * @dev: Pointer to the USB device.
 * @iface0: Pointer to USB interface 0 (audio).
 * @iface1: Pointer to USB interface 1 (MIDI).
 * @card: Pointer to the ALSA sound card instance.
 * @pcm: Pointer to the ALSA PCM device.
 * @rmidi: Pointer to the ALSA rawmidi device.
 *
 * @playback_substream: Pointer to the active playback PCM substream.
 * @playback_urbs: Array of URBs for playback.
 * @playback_urb_alloc_size: Size of allocated buffer for each playback URB.
 * @feedback_urbs: Array of URBs for feedback.
 * @feedback_urb_alloc_size: Size of allocated buffer for each feedback URB.
 * @playback_active: Atomic flag indicating if playback is active.
 * @playback_frames_consumed: Total frames consumed by playback.
 * @driver_playback_pos: Current position in the ALSA playback buffer (frames).
 * @last_period_pos: Last reported period position for playback.
 *
 * @capture_substream: Pointer to the active capture PCM substream.
 * @capture_urbs: Array of URBs for capture.
 * @capture_urb_alloc_size: Size of allocated buffer for each capture URB.
 * @capture_active: Atomic flag indicating if capture is active.
 * @driver_capture_pos: Current position in the ALSA capture buffer (frames).
 * @capture_frames_processed: Total frames processed for capture.
 * @last_capture_period_pos: Last reported period position for capture.
 * @capture_ring_buffer: Ring buffer for raw capture data from USB.
 * @capture_ring_buffer_read_ptr: Read pointer for the capture ring buffer.
 * @capture_ring_buffer_write_ptr: Write pointer for the capture ring buffer.
 * @capture_decode_raw_block: Buffer for a raw 512-byte capture block.
 * @capture_decode_dst_block: Buffer for decoded 32-bit capture samples.
 * @capture_routing_buffer: Intermediate buffer for capture routing.
 * @capture_work: Work struct for deferred capture processing.
 * @stop_work: Work struct for deferred stream stopping.
 * @stop_pcm_work: Work struct for stopping PCM due to a fatal error (e.g.
 * xrun).
 *
 * @midi_in_substream: Pointer to the active MIDI input substream.
 * @midi_out_substream: Pointer to the active MIDI output substream.
 * @midi_in_urbs: Array of URBs for MIDI input.
 * @midi_out_urbs: Array of URBs for MIDI output.
 * @midi_in_active: Atomic flag indicating if MIDI input is active.
 * @midi_out_active: Atomic flag indicating if MIDI output is active.
 * @midi_in_fifo: FIFO for raw MIDI input data.
 * @midi_in_work: Work struct for deferred MIDI input processing.
 * @midi_out_work: Work struct for deferred MIDI output processing.
 * @midi_in_lock: Spinlock for MIDI input FIFO.
 * @midi_out_lock: Spinlock for MIDI output.
 * @midi_out_urbs_in_flight: Bitmap of MIDI output URBs currently in flight.
 * @midi_running_status: Stores the last MIDI status byte for running status.
 * @error_timer: Timer for MIDI error retry logic.
 *
 * @lock: Main spinlock for protecting shared driver state.
 * @active_urbs: Atomic counter for active URBs.
 * @current_rate: Currently configured sample rate of the device.
 * @line_out_source: Source for Line Outputs (0: Playback 1-2, 1: Playback 3-4).
 * @digital_out_source: Source for Digital Outputs (0: Playback 1-2, 1: Playback
 * 3-4).
 * @capture_12_source: Source for Capture channels 1-2 (0: Analog In, 1: Digital
 * In).
 * @capture_34_source: Source for Capture channels 3-4 (0: Analog In, 1: Digital
 * In).
 *
 * @feedback_accumulator_pattern: Stores the calculated frames per packet for
 * feedback.
 * @feedback_pattern_out_idx: Read index for feedback_accumulator_pattern.
 * @feedback_pattern_in_idx: Write index for feedback_accumulator_pattern.
 * @feedback_synced: Flag indicating if feedback is synced.
 * @feedback_consecutive_errors: Counter for consecutive feedback errors.
 * @feedback_urb_skip_count: Number of feedback URBs to skip initially for
 * stabilization.
 * @fpo: Holds the state for the dynamic feedback pattern generation.
 *
 * @playback_anchor: USB anchor for playback URBs.
 * @capture_anchor: USB anchor for capture URBs.
 * @feedback_anchor: USB anchor for feedback URBs.
 * @midi_in_anchor: USB anchor for MIDI input URBs.
 * @midi_out_anchor: USB anchor for MIDI output URBs.
 */
struct tascam_card {
	/* --- Core device pointers --- */
	struct usb_device *dev;
	struct usb_interface *iface0;
	struct usb_interface *iface1;
	struct snd_card *card;
	struct snd_pcm *pcm;
	struct snd_rawmidi *rmidi;

	/* --- PCM Substreams --- */
	struct snd_pcm_substream *playback_substream;
	struct snd_pcm_substream *capture_substream;

	/* --- URBs and Anchors --- */
	struct urb *playback_urbs[NUM_PLAYBACK_URBS];
	size_t playback_urb_alloc_size;
	struct urb *feedback_urbs[NUM_FEEDBACK_URBS];
	size_t feedback_urb_alloc_size;
	struct urb *capture_urbs[NUM_CAPTURE_URBS];
	size_t capture_urb_alloc_size;
	struct urb *midi_in_urbs[NUM_MIDI_IN_URBS];
	struct urb *midi_out_urbs[NUM_MIDI_OUT_URBS];
	struct usb_anchor playback_anchor;
	struct usb_anchor capture_anchor;
	struct usb_anchor feedback_anchor;
	struct usb_anchor midi_in_anchor;
	struct usb_anchor midi_out_anchor;

	/* --- Stream State --- */
	spinlock_t lock;
	atomic_t playback_active;
	atomic_t capture_active;
	atomic_t active_urbs;
	int current_rate;

	/* --- Playback State --- */
	u64 playback_frames_consumed;
	snd_pcm_uframes_t driver_playback_pos;
	u64 last_period_pos;

	/* --- Capture State --- */
	u64 capture_frames_processed;
	snd_pcm_uframes_t driver_capture_pos;
	u64 last_capture_period_pos;
	u8 *capture_ring_buffer;
	size_t capture_ring_buffer_read_ptr;
	size_t capture_ring_buffer_write_ptr;
	u8 *capture_decode_raw_block;
	s32 *capture_decode_dst_block;
	s32 *capture_routing_buffer;

	/* --- MIDI State --- */
	struct snd_rawmidi_substream *midi_in_substream;
	struct snd_rawmidi_substream *midi_out_substream;
	atomic_t midi_in_active;
	atomic_t midi_out_active;
	struct kfifo midi_in_fifo;
	spinlock_t midi_in_lock;
	spinlock_t midi_out_lock;
	unsigned long midi_out_urbs_in_flight;
	u8 midi_running_status;
	struct timer_list error_timer;
	struct completion midi_out_drain_completion;

	/* --- Feedback Sync State --- */
	unsigned int feedback_accumulator_pattern[FEEDBACK_ACCUMULATOR_SIZE];
	unsigned int feedback_pattern_out_idx;
	unsigned int feedback_pattern_in_idx;
	bool feedback_synced;
	unsigned int feedback_consecutive_errors;
	unsigned int feedback_urb_skip_count;
	struct us144mkii_frame_pattern_observer fpo;

	/* --- Workqueues --- */
	struct work_struct stop_work;
	struct work_struct stop_pcm_work;
	struct work_struct capture_work;
	struct work_struct midi_in_work;
	struct work_struct midi_out_work;

	/* --- Mixer/Routing State --- */
	unsigned int line_out_source;
	unsigned int digital_out_source;
	unsigned int capture_12_source;
	unsigned int capture_34_source;
};

/* main.c */
/**
 * tascam_free_urbs() - Free all allocated URBs and associated buffers.
 * @tascam: the tascam_card instance
 *
 * This function kills, unlinks, and frees all playback, feedback, capture,
 * and MIDI URBs, along with their transfer buffers and the capture
 * ring/decode buffers.
 */
void tascam_free_urbs(struct tascam_card *tascam);

/**
 * tascam_alloc_urbs() - Allocate all URBs and associated buffers.
 * @tascam: the tascam_card instance
 *
 * This function allocates and initializes all URBs for playback, feedback,
 * capture, and MIDI, as well as the necessary buffers for data processing.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int tascam_alloc_urbs(struct tascam_card *tascam);

/**
 * tascam_stop_work_handler() - Work handler to stop all active streams.
 * @work: Pointer to the work_struct.
 *
 * This function is scheduled to stop all active URBs (playback, feedback,
 * capture) and reset the active_urbs counter.
 */
void tascam_stop_work_handler(struct work_struct *work);

/* us144mkii_pcm.h */
#include "us144mkii_pcm.h"

/* us144mkii_midi.c */
/**
 * tascam_midi_in_urb_complete() - Completion handler for MIDI IN URBs
 * @urb: The completed URB.
 *
 * This function runs in interrupt context. It places the raw data from the
 * USB endpoint into a kfifo and schedules a work item to process it later,
 * ensuring the interrupt handler remains fast.
 */
void tascam_midi_in_urb_complete(struct urb *urb);

/**
 * tascam_midi_out_urb_complete() - Completion handler for MIDI OUT bulk URB.
 * @urb: The completed URB.
 *
 * This function runs in interrupt context. It marks the output URB as no
 * longer in-flight. It then re-schedules the work handler to check for and
 * send any more data waiting in the ALSA buffer. This is a safe, non-blocking
 * way to continue the data transmission chain.
 */
void tascam_midi_out_urb_complete(struct urb *urb);

/**
 * tascam_create_midi() - Create and initialize the ALSA rawmidi device.
 * @tascam: The driver instance.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int tascam_create_midi(struct tascam_card *tascam);

/* us144mkii_controls.c */
/**
 * tascam_create_controls() - Creates and adds ALSA mixer controls for the
 * device.
 * @tascam: The driver instance.
 *
 * This function registers custom ALSA controls for managing audio routing
 * (line out source, digital out source, capture 1-2 source, capture 3-4 source)
 * and displaying the current sample rate.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int tascam_create_controls(struct tascam_card *tascam);

#endif /* __US144MKII_H */
