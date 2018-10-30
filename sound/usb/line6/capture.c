/*
 * Line 6 Linux USB driver
 *
 * Copyright (C) 2004-2010 Markus Grabner (grabner@icg.tugraz.at)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 *
 */

#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>

#include "capture.h"
#include "driver.h"
#include "pcm.h"

/*
	Find a free URB and submit it.
	must be called in line6pcm->in.lock context
*/
static int submit_audio_in_urb(struct snd_line6_pcm *line6pcm)
{
	int index;
	int i, urb_size;
	int ret;
	struct urb *urb_in;

	index = find_first_zero_bit(&line6pcm->in.active_urbs,
				    line6pcm->line6->iso_buffers);

	if (index < 0 || index >= line6pcm->line6->iso_buffers) {
		dev_err(line6pcm->line6->ifcdev, "no free URB found\n");
		return -EINVAL;
	}

	urb_in = line6pcm->in.urbs[index];
	urb_size = 0;

	for (i = 0; i < LINE6_ISO_PACKETS; ++i) {
		struct usb_iso_packet_descriptor *fin =
		    &urb_in->iso_frame_desc[i];
		fin->offset = urb_size;
		fin->length = line6pcm->max_packet_size_in;
		urb_size += line6pcm->max_packet_size_in;
	}

	urb_in->transfer_buffer =
	    line6pcm->in.buffer +
	    index * LINE6_ISO_PACKETS * line6pcm->max_packet_size_in;
	urb_in->transfer_buffer_length = urb_size;
	urb_in->context = line6pcm;

	ret = usb_submit_urb(urb_in, GFP_ATOMIC);

	if (ret == 0)
		set_bit(index, &line6pcm->in.active_urbs);
	else
		dev_err(line6pcm->line6->ifcdev,
			"URB in #%d submission failed (%d)\n", index, ret);

	return 0;
}

/*
	Submit all currently available capture URBs.
	must be called in line6pcm->in.lock context
*/
int line6_submit_audio_in_all_urbs(struct snd_line6_pcm *line6pcm)
{
	int ret = 0, i;

	for (i = 0; i < line6pcm->line6->iso_buffers; ++i) {
		ret = submit_audio_in_urb(line6pcm);
		if (ret < 0)
			break;
	}

	return ret;
}

/*
	Copy data into ALSA capture buffer.
*/
void line6_capture_copy(struct snd_line6_pcm *line6pcm, char *fbuf, int fsize)
{
	struct snd_pcm_substream *substream =
	    get_substream(line6pcm, SNDRV_PCM_STREAM_CAPTURE);
	struct snd_pcm_runtime *runtime = substream->runtime;
	const int bytes_per_frame =
		line6pcm->properties->bytes_per_channel *
		line6pcm->properties->capture_hw.channels_max;
	int frames = fsize / bytes_per_frame;

	if (runtime == NULL)
		return;

	if (line6pcm->in.pos_done + frames > runtime->buffer_size) {
		/*
		   The transferred area goes over buffer boundary,
		   copy two separate chunks.
		 */
		int len;

		len = runtime->buffer_size - line6pcm->in.pos_done;

		if (len > 0) {
			memcpy(runtime->dma_area +
			       line6pcm->in.pos_done * bytes_per_frame, fbuf,
			       len * bytes_per_frame);
			memcpy(runtime->dma_area, fbuf + len * bytes_per_frame,
			       (frames - len) * bytes_per_frame);
		} else {
			/* this is somewhat paranoid */
			dev_err(line6pcm->line6->ifcdev,
				"driver bug: len = %d\n", len);
		}
	} else {
		/* copy single chunk */
		memcpy(runtime->dma_area +
		       line6pcm->in.pos_done * bytes_per_frame, fbuf, fsize);
	}

	line6pcm->in.pos_done += frames;
	if (line6pcm->in.pos_done >= runtime->buffer_size)
		line6pcm->in.pos_done -= runtime->buffer_size;
}

void line6_capture_check_period(struct snd_line6_pcm *line6pcm, int length)
{
	struct snd_pcm_substream *substream =
	    get_substream(line6pcm, SNDRV_PCM_STREAM_CAPTURE);

	line6pcm->in.bytes += length;
	if (line6pcm->in.bytes >= line6pcm->in.period) {
		line6pcm->in.bytes %= line6pcm->in.period;
		spin_unlock(&line6pcm->in.lock);
		snd_pcm_period_elapsed(substream);
		spin_lock(&line6pcm->in.lock);
	}
}

/*
 * Callback for completed capture URB.
 */
static void audio_in_callback(struct urb *urb)
{
	int i, index, length = 0, shutdown = 0;
	unsigned long flags;

	struct snd_line6_pcm *line6pcm = (struct snd_line6_pcm *)urb->context;

	line6pcm->in.last_frame = urb->start_frame;

	/* find index of URB */
	for (index = 0; index < line6pcm->line6->iso_buffers; ++index)
		if (urb == line6pcm->in.urbs[index])
			break;

	spin_lock_irqsave(&line6pcm->in.lock, flags);

	for (i = 0; i < LINE6_ISO_PACKETS; ++i) {
		char *fbuf;
		int fsize;
		struct usb_iso_packet_descriptor *fin = &urb->iso_frame_desc[i];

		if (fin->status == -EXDEV) {
			shutdown = 1;
			break;
		}

		fbuf = urb->transfer_buffer + fin->offset;
		fsize = fin->actual_length;

		if (fsize > line6pcm->max_packet_size_in) {
			dev_err(line6pcm->line6->ifcdev,
				"driver and/or device bug: packet too large (%d > %d)\n",
				fsize, line6pcm->max_packet_size_in);
		}

		length += fsize;

		BUILD_BUG_ON_MSG(LINE6_ISO_PACKETS != 1,
			"The following code assumes LINE6_ISO_PACKETS == 1");
		/* TODO:
		 * Also, if iso_buffers != 2, the prev frame is almost random at
		 * playback side.
		 * This needs to be redesigned. It should be "stable", but we may
		 * experience sync problems on such high-speed configs.
		 */

		line6pcm->prev_fbuf = fbuf;
		line6pcm->prev_fsize = fsize /
			(line6pcm->properties->bytes_per_channel *
			line6pcm->properties->capture_hw.channels_max);

		if (!test_bit(LINE6_STREAM_IMPULSE, &line6pcm->in.running) &&
		    test_bit(LINE6_STREAM_PCM, &line6pcm->in.running) &&
		    fsize > 0)
			line6_capture_copy(line6pcm, fbuf, fsize);
	}

	clear_bit(index, &line6pcm->in.active_urbs);

	if (test_and_clear_bit(index, &line6pcm->in.unlink_urbs))
		shutdown = 1;

	if (!shutdown) {
		submit_audio_in_urb(line6pcm);

		if (!test_bit(LINE6_STREAM_IMPULSE, &line6pcm->in.running) &&
		    test_bit(LINE6_STREAM_PCM, &line6pcm->in.running))
			line6_capture_check_period(line6pcm, length);
	}

	spin_unlock_irqrestore(&line6pcm->in.lock, flags);
}

/* open capture callback */
static int snd_line6_capture_open(struct snd_pcm_substream *substream)
{
	int err;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_line6_pcm *line6pcm = snd_pcm_substream_chip(substream);

	err = snd_pcm_hw_constraint_ratdens(runtime, 0,
					    SNDRV_PCM_HW_PARAM_RATE,
					    &line6pcm->properties->rates);
	if (err < 0)
		return err;

	line6_pcm_acquire(line6pcm, LINE6_STREAM_CAPTURE_HELPER, false);

	runtime->hw = line6pcm->properties->capture_hw;
	return 0;
}

/* close capture callback */
static int snd_line6_capture_close(struct snd_pcm_substream *substream)
{
	struct snd_line6_pcm *line6pcm = snd_pcm_substream_chip(substream);

	line6_pcm_release(line6pcm, LINE6_STREAM_CAPTURE_HELPER);
	return 0;
}

/* capture operators */
const struct snd_pcm_ops snd_line6_capture_ops = {
	.open = snd_line6_capture_open,
	.close = snd_line6_capture_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = snd_line6_hw_params,
	.hw_free = snd_line6_hw_free,
	.prepare = snd_line6_prepare,
	.trigger = snd_line6_trigger,
	.pointer = snd_line6_pointer,
};

int line6_create_audio_in_urbs(struct snd_line6_pcm *line6pcm)
{
	struct usb_line6 *line6 = line6pcm->line6;
	int i;

	line6pcm->in.urbs = kcalloc(line6->iso_buffers, sizeof(struct urb *),
				    GFP_KERNEL);
	if (line6pcm->in.urbs == NULL)
		return -ENOMEM;

	/* create audio URBs and fill in constant values: */
	for (i = 0; i < line6->iso_buffers; ++i) {
		struct urb *urb;

		/* URB for audio in: */
		urb = line6pcm->in.urbs[i] =
		    usb_alloc_urb(LINE6_ISO_PACKETS, GFP_KERNEL);

		if (urb == NULL)
			return -ENOMEM;

		urb->dev = line6->usbdev;
		urb->pipe =
		    usb_rcvisocpipe(line6->usbdev,
				    line6->properties->ep_audio_r &
				    USB_ENDPOINT_NUMBER_MASK);
		urb->transfer_flags = URB_ISO_ASAP;
		urb->start_frame = -1;
		urb->number_of_packets = LINE6_ISO_PACKETS;
		urb->interval = LINE6_ISO_INTERVAL;
		urb->error_count = 0;
		urb->complete = audio_in_callback;
	}

	return 0;
}
