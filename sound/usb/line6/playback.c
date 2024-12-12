// SPDX-License-Identifier: GPL-2.0-only
/*
 * Line 6 Linux USB driver
 *
 * Copyright (C) 2004-2010 Markus Grabner (line6@grabner-graz.at)
 */

#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>

#include "capture.h"
#include "driver.h"
#include "pcm.h"
#include "playback.h"

/*
	Software stereo volume control.
*/
static void change_volume(struct urb *urb_out, int volume[],
			  int bytes_per_frame)
{
	int chn = 0;

	if (volume[0] == 256 && volume[1] == 256)
		return;		/* maximum volume - no change */

	if (bytes_per_frame == 4) {
		__le16 *p, *buf_end;

		p = (__le16 *)urb_out->transfer_buffer;
		buf_end = p + urb_out->transfer_buffer_length / sizeof(*p);

		for (; p < buf_end; ++p) {
			short pv = le16_to_cpu(*p);
			int val = (pv * volume[chn & 1]) >> 8;
			pv = clamp(val, -0x8000, 0x7fff);
			*p = cpu_to_le16(pv);
			++chn;
		}
	} else if (bytes_per_frame == 6) {
		unsigned char *p, *buf_end;

		p = (unsigned char *)urb_out->transfer_buffer;
		buf_end = p + urb_out->transfer_buffer_length;

		for (; p < buf_end; p += 3) {
			int val;

			val = p[0] + (p[1] << 8) + ((signed char)p[2] << 16);
			val = (val * volume[chn & 1]) >> 8;
			val = clamp(val, -0x800000, 0x7fffff);
			p[0] = val;
			p[1] = val >> 8;
			p[2] = val >> 16;
			++chn;
		}
	}
}

/*
	Create signal for impulse response test.
*/
static void create_impulse_test_signal(struct snd_line6_pcm *line6pcm,
				       struct urb *urb_out, int bytes_per_frame)
{
	int frames = urb_out->transfer_buffer_length / bytes_per_frame;

	if (bytes_per_frame == 4) {
		int i;
		short *pi = (short *)line6pcm->prev_fbuf;
		short *po = (short *)urb_out->transfer_buffer;

		for (i = 0; i < frames; ++i) {
			po[0] = pi[0];
			po[1] = 0;
			pi += 2;
			po += 2;
		}
	} else if (bytes_per_frame == 6) {
		int i, j;
		unsigned char *pi = line6pcm->prev_fbuf;
		unsigned char *po = urb_out->transfer_buffer;

		for (i = 0; i < frames; ++i) {
			for (j = 0; j < bytes_per_frame / 2; ++j)
				po[j] = pi[j];

			for (; j < bytes_per_frame; ++j)
				po[j] = 0;

			pi += bytes_per_frame;
			po += bytes_per_frame;
		}
	}
	if (--line6pcm->impulse_count <= 0) {
		((unsigned char *)(urb_out->transfer_buffer))[bytes_per_frame -
							      1] =
		    line6pcm->impulse_volume;
		line6pcm->impulse_count = line6pcm->impulse_period;
	}
}

/*
	Add signal to buffer for software monitoring.
*/
static void add_monitor_signal(struct urb *urb_out, unsigned char *signal,
			       int volume, int bytes_per_frame)
{
	if (volume == 0)
		return;		/* zero volume - no change */

	if (bytes_per_frame == 4) {
		__le16 *pi, *po, *buf_end;

		pi = (__le16 *)signal;
		po = (__le16 *)urb_out->transfer_buffer;
		buf_end = po + urb_out->transfer_buffer_length / sizeof(*po);

		for (; po < buf_end; ++pi, ++po) {
			short pov = le16_to_cpu(*po);
			short piv = le16_to_cpu(*pi);
			int val = pov + ((piv * volume) >> 8);
			pov = clamp(val, -0x8000, 0x7fff);
			*po = cpu_to_le16(pov);
		}
	}

	/*
	   We don't need to handle devices with 6 bytes per frame here
	   since they all support hardware monitoring.
	 */
}

/*
	Find a free URB, prepare audio data, and submit URB.
	must be called in line6pcm->out.lock context
*/
static int submit_audio_out_urb(struct snd_line6_pcm *line6pcm)
{
	int index;
	int i, urb_size, urb_frames;
	int ret;
	const int bytes_per_frame =
		line6pcm->properties->bytes_per_channel *
		line6pcm->properties->playback_hw.channels_max;
	const int frame_increment =
		line6pcm->properties->rates.rats[0].num_min;
	const int frame_factor =
		line6pcm->properties->rates.rats[0].den *
		(line6pcm->line6->intervals_per_second / LINE6_ISO_INTERVAL);
	struct urb *urb_out;

	index = find_first_zero_bit(&line6pcm->out.active_urbs,
				    line6pcm->line6->iso_buffers);

	if (index < 0 || index >= line6pcm->line6->iso_buffers) {
		dev_err(line6pcm->line6->ifcdev, "no free URB found\n");
		return -EINVAL;
	}

	urb_out = line6pcm->out.urbs[index];
	urb_size = 0;

	/* TODO: this may not work for LINE6_ISO_PACKETS != 1 */
	for (i = 0; i < LINE6_ISO_PACKETS; ++i) {
		/* compute frame size for given sampling rate */
		int fsize = 0;
		struct usb_iso_packet_descriptor *fout =
		    &urb_out->iso_frame_desc[i];

		fsize = line6pcm->prev_fsize;
		if (fsize == 0) {
			int n;

			line6pcm->out.count += frame_increment;
			n = line6pcm->out.count / frame_factor;
			line6pcm->out.count -= n * frame_factor;
			fsize = n;
		}

		fsize *= bytes_per_frame;

		fout->offset = urb_size;
		fout->length = fsize;
		urb_size += fsize;
	}

	if (urb_size == 0) {
		/* can't determine URB size */
		dev_err(line6pcm->line6->ifcdev, "driver bug: urb_size = 0\n");
		return -EINVAL;
	}

	urb_frames = urb_size / bytes_per_frame;
	urb_out->transfer_buffer =
	    line6pcm->out.buffer +
	    index * LINE6_ISO_PACKETS * line6pcm->max_packet_size_out;
	urb_out->transfer_buffer_length = urb_size;
	urb_out->context = line6pcm;

	if (test_bit(LINE6_STREAM_PCM, &line6pcm->out.running) &&
	    !test_bit(LINE6_FLAG_PAUSE_PLAYBACK, &line6pcm->flags)) {
		struct snd_pcm_runtime *runtime =
		    get_substream(line6pcm, SNDRV_PCM_STREAM_PLAYBACK)->runtime;

		if (line6pcm->out.pos + urb_frames > runtime->buffer_size) {
			/*
			   The transferred area goes over buffer boundary,
			   copy the data to the temp buffer.
			 */
			int len;

			len = runtime->buffer_size - line6pcm->out.pos;

			if (len > 0) {
				memcpy(urb_out->transfer_buffer,
				       runtime->dma_area +
				       line6pcm->out.pos * bytes_per_frame,
				       len * bytes_per_frame);
				memcpy(urb_out->transfer_buffer +
				       len * bytes_per_frame, runtime->dma_area,
				       (urb_frames - len) * bytes_per_frame);
			} else
				dev_err(line6pcm->line6->ifcdev, "driver bug: len = %d\n",
					len);
		} else {
			memcpy(urb_out->transfer_buffer,
			       runtime->dma_area +
			       line6pcm->out.pos * bytes_per_frame,
			       urb_out->transfer_buffer_length);
		}

		line6pcm->out.pos += urb_frames;
		if (line6pcm->out.pos >= runtime->buffer_size)
			line6pcm->out.pos -= runtime->buffer_size;

		change_volume(urb_out, line6pcm->volume_playback,
			      bytes_per_frame);
	} else {
		memset(urb_out->transfer_buffer, 0,
		       urb_out->transfer_buffer_length);
	}

	spin_lock_nested(&line6pcm->in.lock, SINGLE_DEPTH_NESTING);
	if (line6pcm->prev_fbuf) {
		if (test_bit(LINE6_STREAM_IMPULSE, &line6pcm->out.running)) {
			create_impulse_test_signal(line6pcm, urb_out,
						   bytes_per_frame);
			if (test_bit(LINE6_STREAM_PCM, &line6pcm->in.running)) {
				line6_capture_copy(line6pcm,
						   urb_out->transfer_buffer,
						   urb_out->
						   transfer_buffer_length);
				line6_capture_check_period(line6pcm,
					urb_out->transfer_buffer_length);
			}
		} else {
			if (!(line6pcm->line6->properties->capabilities & LINE6_CAP_HWMON)
			    && line6pcm->out.running && line6pcm->in.running)
				add_monitor_signal(urb_out, line6pcm->prev_fbuf,
						   line6pcm->volume_monitor,
						   bytes_per_frame);
		}
		line6pcm->prev_fbuf = NULL;
		line6pcm->prev_fsize = 0;
	}
	spin_unlock(&line6pcm->in.lock);

	ret = usb_submit_urb(urb_out, GFP_ATOMIC);

	if (ret == 0)
		set_bit(index, &line6pcm->out.active_urbs);
	else
		dev_err(line6pcm->line6->ifcdev,
			"URB out #%d submission failed (%d)\n", index, ret);

	return 0;
}

/*
	Submit all currently available playback URBs.
	must be called in line6pcm->out.lock context
 */
int line6_submit_audio_out_all_urbs(struct snd_line6_pcm *line6pcm)
{
	int ret = 0, i;

	for (i = 0; i < line6pcm->line6->iso_buffers; ++i) {
		ret = submit_audio_out_urb(line6pcm);
		if (ret < 0)
			break;
	}

	return ret;
}

/*
	Callback for completed playback URB.
*/
static void audio_out_callback(struct urb *urb)
{
	int i, index, length = 0, shutdown = 0;
	unsigned long flags;
	struct snd_line6_pcm *line6pcm = (struct snd_line6_pcm *)urb->context;
	struct snd_pcm_substream *substream =
	    get_substream(line6pcm, SNDRV_PCM_STREAM_PLAYBACK);
	const int bytes_per_frame =
		line6pcm->properties->bytes_per_channel *
		line6pcm->properties->playback_hw.channels_max;

#if USE_CLEAR_BUFFER_WORKAROUND
	memset(urb->transfer_buffer, 0, urb->transfer_buffer_length);
#endif

	line6pcm->out.last_frame = urb->start_frame;

	/* find index of URB */
	for (index = 0; index < line6pcm->line6->iso_buffers; index++)
		if (urb == line6pcm->out.urbs[index])
			break;

	if (index >= line6pcm->line6->iso_buffers)
		return;		/* URB has been unlinked asynchronously */

	for (i = 0; i < LINE6_ISO_PACKETS; i++)
		length += urb->iso_frame_desc[i].length;

	spin_lock_irqsave(&line6pcm->out.lock, flags);

	if (test_bit(LINE6_STREAM_PCM, &line6pcm->out.running)) {
		struct snd_pcm_runtime *runtime = substream->runtime;

		line6pcm->out.pos_done +=
		    length / bytes_per_frame;

		if (line6pcm->out.pos_done >= runtime->buffer_size)
			line6pcm->out.pos_done -= runtime->buffer_size;
	}

	clear_bit(index, &line6pcm->out.active_urbs);

	for (i = 0; i < LINE6_ISO_PACKETS; i++)
		if (urb->iso_frame_desc[i].status == -EXDEV) {
			shutdown = 1;
			break;
		}

	if (test_and_clear_bit(index, &line6pcm->out.unlink_urbs))
		shutdown = 1;

	if (!shutdown) {
		submit_audio_out_urb(line6pcm);

		if (test_bit(LINE6_STREAM_PCM, &line6pcm->out.running)) {
			line6pcm->out.bytes += length;
			if (line6pcm->out.bytes >= line6pcm->out.period) {
				line6pcm->out.bytes %= line6pcm->out.period;
				spin_unlock(&line6pcm->out.lock);
				snd_pcm_period_elapsed(substream);
				spin_lock(&line6pcm->out.lock);
			}
		}
	}
	spin_unlock_irqrestore(&line6pcm->out.lock, flags);
}

/* open playback callback */
static int snd_line6_playback_open(struct snd_pcm_substream *substream)
{
	int err;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_line6_pcm *line6pcm = snd_pcm_substream_chip(substream);

	err = snd_pcm_hw_constraint_ratdens(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
					    &line6pcm->properties->rates);
	if (err < 0)
		return err;

	runtime->hw = line6pcm->properties->playback_hw;
	return 0;
}

/* close playback callback */
static int snd_line6_playback_close(struct snd_pcm_substream *substream)
{
	return 0;
}

/* playback operators */
const struct snd_pcm_ops snd_line6_playback_ops = {
	.open = snd_line6_playback_open,
	.close = snd_line6_playback_close,
	.hw_params = snd_line6_hw_params,
	.hw_free = snd_line6_hw_free,
	.prepare = snd_line6_prepare,
	.trigger = snd_line6_trigger,
	.pointer = snd_line6_pointer,
};

int line6_create_audio_out_urbs(struct snd_line6_pcm *line6pcm)
{
	struct usb_line6 *line6 = line6pcm->line6;
	int i;

	line6pcm->out.urbs = kcalloc(line6->iso_buffers, sizeof(struct urb *),
				     GFP_KERNEL);
	if (line6pcm->out.urbs == NULL)
		return -ENOMEM;

	/* create audio URBs and fill in constant values: */
	for (i = 0; i < line6->iso_buffers; ++i) {
		struct urb *urb;

		/* URB for audio out: */
		urb = line6pcm->out.urbs[i] =
		    usb_alloc_urb(LINE6_ISO_PACKETS, GFP_KERNEL);

		if (urb == NULL)
			return -ENOMEM;

		urb->dev = line6->usbdev;
		urb->pipe =
		    usb_sndisocpipe(line6->usbdev,
				    line6->properties->ep_audio_w &
				    USB_ENDPOINT_NUMBER_MASK);
		urb->transfer_flags = URB_ISO_ASAP;
		urb->start_frame = -1;
		urb->number_of_packets = LINE6_ISO_PACKETS;
		urb->interval = LINE6_ISO_INTERVAL;
		urb->error_count = 0;
		urb->complete = audio_out_callback;
		if (usb_urb_ep_type_check(urb))
			return -EINVAL;
	}

	return 0;
}
