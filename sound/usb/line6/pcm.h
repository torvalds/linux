/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Line 6 Linux USB driver
 *
 * Copyright (C) 2004-2010 Markus Grabner (line6@grabner-graz.at)
 */

/*
	PCM interface to POD series devices.
*/

#ifndef PCM_H
#define PCM_H

#include <sound/pcm.h>

#include "driver.h"

/*
	number of USB frames per URB
	The Line 6 Windows driver always transmits two frames per packet, but
	the Linux driver performs significantly better (i.e., lower latency)
	with only one frame per packet.
*/
#define LINE6_ISO_PACKETS	1

/* in a "full speed" device (such as the PODxt Pro) this means 1ms,
 *  for "high speed" it's 1/8ms
 */
#define LINE6_ISO_INTERVAL	1

#define LINE6_IMPULSE_DEFAULT_PERIOD 100

/*
	Get substream from Line 6 PCM data structure
*/
#define get_substream(line6pcm, stream)	\
		(line6pcm->pcm->streams[stream].substream)

/*
	PCM mode bits.

	There are several features of the Line 6 USB driver which require PCM
	data to be exchanged with the device:
	*) PCM playback and capture via ALSA
	*) software monitoring (for devices without hardware monitoring)
	*) optional impulse response measurement
	However, from the device's point of view, there is just a single
	capture and playback stream, which must be shared between these
	subsystems. It is therefore necessary to maintain the state of the
	subsystems with respect to PCM usage.

	We define two bit flags, "opened" and "running", for each playback
	or capture stream.  Both can contain the bit flag corresponding to
	LINE6_STREAM_* type,
	  LINE6_STREAM_PCM = ALSA PCM playback or capture
	  LINE6_STREAM_MONITOR = software monitoring
	  IMPULSE = optional impulse response measurement
	The opened flag indicates whether the buffer is allocated while
	the running flag indicates whether the stream is running.

	For monitor or impulse operations, the driver needs to call
	line6_pcm_acquire() or line6_pcm_release() with the appropriate
	LINE6_STREAM_* flag.
*/

/* stream types */
enum {
	LINE6_STREAM_PCM,
	LINE6_STREAM_MONITOR,
	LINE6_STREAM_IMPULSE,
	LINE6_STREAM_CAPTURE_HELPER,
};

/* misc bit flags for PCM operation */
enum {
	LINE6_FLAG_PAUSE_PLAYBACK,
	LINE6_FLAG_PREPARED,
};

struct line6_pcm_properties {
	struct snd_pcm_hardware playback_hw, capture_hw;
	struct snd_pcm_hw_constraint_ratdens rates;
	int bytes_per_channel;
};

struct line6_pcm_stream {
	/* allocated URBs */
	struct urb **urbs;

	/* Temporary buffer;
	 * Since the packet size is not known in advance, this buffer is
	 * large enough to store maximum size packets.
	 */
	unsigned char *buffer;

	/* Free frame position in the buffer. */
	snd_pcm_uframes_t pos;

	/* Count processed bytes;
	 * This is modulo period size (to determine when a period is finished).
	 */
	unsigned bytes;

	/* Counter to create desired sample rate */
	unsigned count;

	/* period size in bytes */
	unsigned period;

	/* Processed frame position in the buffer;
	 * The contents of the ring buffer have been consumed by the USB
	 * subsystem (i.e., sent to the USB device) up to this position.
	 */
	snd_pcm_uframes_t pos_done;

	/* Bit mask of active URBs */
	unsigned long active_urbs;

	/* Bit mask of URBs currently being unlinked */
	unsigned long unlink_urbs;

	/* Spin lock to protect updates of the buffer positions (not contents)
	 */
	spinlock_t lock;

	/* Bit flags for operational stream types */
	unsigned long opened;

	/* Bit flags for running stream types */
	unsigned long running;

	int last_frame;
};

struct snd_line6_pcm {
	/* Pointer back to the Line 6 driver data structure */
	struct usb_line6 *line6;

	/* Properties. */
	struct line6_pcm_properties *properties;

	/* ALSA pcm stream */
	struct snd_pcm *pcm;

	/* protection to state changes of in/out streams */
	struct mutex state_mutex;

	/* Capture and playback streams */
	struct line6_pcm_stream in;
	struct line6_pcm_stream out;

	/* Previously captured frame (for software monitoring) */
	unsigned char *prev_fbuf;

	/* Size of previously captured frame (for software monitoring/sync) */
	int prev_fsize;

	/* Maximum size of USB packet */
	int max_packet_size_in;
	int max_packet_size_out;

	/* PCM playback volume (left and right) */
	int volume_playback[2];

	/* PCM monitor volume */
	int volume_monitor;

	/* Volume of impulse response test signal (if zero, test is disabled) */
	int impulse_volume;

	/* Period of impulse response test signal */
	int impulse_period;

	/* Counter for impulse response test signal */
	int impulse_count;

	/* Several status bits (see LINE6_FLAG_*) */
	unsigned long flags;
};

extern int line6_init_pcm(struct usb_line6 *line6,
			  struct line6_pcm_properties *properties);
extern int snd_line6_trigger(struct snd_pcm_substream *substream, int cmd);
extern int snd_line6_prepare(struct snd_pcm_substream *substream);
extern int snd_line6_hw_params(struct snd_pcm_substream *substream,
			       struct snd_pcm_hw_params *hw_params);
extern int snd_line6_hw_free(struct snd_pcm_substream *substream);
extern snd_pcm_uframes_t snd_line6_pointer(struct snd_pcm_substream *substream);
extern void line6_pcm_disconnect(struct snd_line6_pcm *line6pcm);
extern int line6_pcm_acquire(struct snd_line6_pcm *line6pcm, int type,
			       bool start);
extern void line6_pcm_release(struct snd_line6_pcm *line6pcm, int type);

#endif
