/* SPDX-License-Identifier: GPL-2.0-only */
// Copyright (c) 2025 Šerif Rami <ramiserifpersia@gmail.com>

#ifndef __US144MKII_H
#define __US144MKII_H

#include <linux/usb.h>
#include <sound/core.h>
#include <sound/initval.h>

#define DRIVER_NAME "us144mkii"

/* --- USB Device Identification --- */
#define USB_VID_TASCAM 0x0644
#define USB_PID_TASCAM_US144 0x800f
#define USB_PID_TASCAM_US144MKII 0x8020

/* --- USB Control Message Protocol --- */
#define RT_D2H_VENDOR_DEV (USB_DIR_IN|USB_TYPE_VENDOR|USB_RECIP_DEVICE)
#define VENDOR_REQ_MODE_CONTROL 0x49
#define MODE_VAL_HANDSHAKE_READ 0x0000
#define USB_CTRL_TIMEOUT_MS 1000

/* --- Audio Format Configuration --- */
#define BYTES_PER_SAMPLE 3
#define NUM_CHANNELS 4
#define BYTES_PER_FRAME (NUM_CHANNELS * BYTES_PER_SAMPLE)

/* --- USB Control Message Protocol --- */
#define RT_D2H_VENDOR_DEV (USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE)
#define VENDOR_REQ_MODE_CONTROL 0x49
#define MODE_VAL_HANDSHAKE_READ 0x0000
#define USB_CTRL_TIMEOUT_MS 1000

struct tascam_card;

#include "us144mkii_pcm.h"

/**
 * struct tascam_card - Main driver data structure for the TASCAM US-144MKII.
 * @dev: Pointer to the USB device.
 * @iface0: Pointer to USB interface 0 (audio).
 * @iface1: Pointer to USB interface 1 (MIDI).
 * @card: Pointer to the ALSA sound card instance.
 * @pcm: Pointer to the ALSA PCM device.
 * @playback_substream: Pointer to the active playback PCM substream.
 * @capture_substream: Pointer to the active capture PCM substream.
 * @playback_active: Atomic flag indicating if playback is active.
 * @capture_active: Atomic flag indicating if capture is active.
 * @driver_playback_pos: Current position in the ALSA playback buffer (frames).
 * @driver_capture_pos: Current position in the ALSA capture buffer (frames).
 * @playback_frames_consumed: Total frames consumed by playback.
 * @capture_frames_processed: Total frames processed for capture.
 * @current_rate: Currently configured sample rate of the device.
 * @lock: Main spinlock for protecting shared driver state.
 */
struct tascam_card {
	struct usb_device *dev;
	struct usb_interface *iface0;
	struct usb_interface *iface1;
	struct snd_card *card;
	struct snd_pcm *pcm;

	struct snd_pcm_substream *playback_substream;
	struct snd_pcm_substream *capture_substream;

	atomic_t playback_active;
	atomic_t capture_active;

	snd_pcm_uframes_t driver_playback_pos;
	snd_pcm_uframes_t driver_capture_pos;

	u64 playback_frames_consumed;
	u64 capture_frames_processed;

	int current_rate;
	spinlock_t lock;
};

#endif /* __US144MKII_H */
