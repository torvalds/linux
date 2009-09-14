/*
 *   Copyright (c) 2006-2008 Daniel Mack, Karsten Wiese
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
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
*/

#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/usb.h>
#include <sound/core.h>
#include <sound/pcm.h>

#include "device.h"
#include "audio.h"

#define N_URBS			32
#define CLOCK_DRIFT_TOLERANCE	5
#define FRAMES_PER_URB		8
#define BYTES_PER_FRAME		512
#define CHANNELS_PER_STREAM	2
#define BYTES_PER_SAMPLE	3
#define BYTES_PER_SAMPLE_USB	4
#define MAX_BUFFER_SIZE		(128*1024)
#define MAX_ENDPOINT_SIZE	512

#define ENDPOINT_CAPTURE	2
#define ENDPOINT_PLAYBACK	6

#define MAKE_CHECKBYTE(dev,stream,i) \
	(stream << 1) | (~(i / (dev->n_streams * BYTES_PER_SAMPLE_USB)) & 1)

static struct snd_pcm_hardware snd_usb_caiaq_pcm_hardware = {
	.info 		= (SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
			   SNDRV_PCM_INFO_BLOCK_TRANSFER),
	.formats 	= SNDRV_PCM_FMTBIT_S24_3BE,
	.rates 		= (SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 |
			   SNDRV_PCM_RATE_96000),
	.rate_min	= 44100,
	.rate_max	= 0, /* will overwrite later */
	.channels_min	= CHANNELS_PER_STREAM,
	.channels_max	= CHANNELS_PER_STREAM,
	.buffer_bytes_max = MAX_BUFFER_SIZE,
	.period_bytes_min = 128,
	.period_bytes_max = MAX_BUFFER_SIZE,
	.periods_min	= 1,
	.periods_max	= 1024,
};

static void
activate_substream(struct snd_usb_caiaqdev *dev,
	           struct snd_pcm_substream *sub)
{
	if (sub->stream == SNDRV_PCM_STREAM_PLAYBACK)
		dev->sub_playback[sub->number] = sub;
	else
		dev->sub_capture[sub->number] = sub;
}

static void
deactivate_substream(struct snd_usb_caiaqdev *dev,
		     struct snd_pcm_substream *sub)
{
	unsigned long flags;
	spin_lock_irqsave(&dev->spinlock, flags);

	if (sub->stream == SNDRV_PCM_STREAM_PLAYBACK)
		dev->sub_playback[sub->number] = NULL;
	else
		dev->sub_capture[sub->number] = NULL;

	spin_unlock_irqrestore(&dev->spinlock, flags);
}

static int
all_substreams_zero(struct snd_pcm_substream **subs)
{
	int i;
	for (i = 0; i < MAX_STREAMS; i++)
		if (subs[i] != NULL)
			return 0;
	return 1;
}

static int stream_start(struct snd_usb_caiaqdev *dev)
{
	int i, ret;

	debug("%s(%p)\n", __func__, dev);

	if (dev->streaming)
		return -EINVAL;

	memset(dev->sub_playback, 0, sizeof(dev->sub_playback));
	memset(dev->sub_capture, 0, sizeof(dev->sub_capture));
	dev->input_panic = 0;
	dev->output_panic = 0;
	dev->first_packet = 1;
	dev->streaming = 1;
	dev->warned = 0;

	for (i = 0; i < N_URBS; i++) {
		ret = usb_submit_urb(dev->data_urbs_in[i], GFP_ATOMIC);
		if (ret) {
			log("unable to trigger read #%d! (ret %d)\n", i, ret);
			dev->streaming = 0;
			return -EPIPE;
		}
	}

	return 0;
}

static void stream_stop(struct snd_usb_caiaqdev *dev)
{
	int i;

	debug("%s(%p)\n", __func__, dev);
	if (!dev->streaming)
		return;

	dev->streaming = 0;

	for (i = 0; i < N_URBS; i++) {
		usb_kill_urb(dev->data_urbs_in[i]);
		usb_kill_urb(dev->data_urbs_out[i]);
	}
}

static int snd_usb_caiaq_substream_open(struct snd_pcm_substream *substream)
{
	struct snd_usb_caiaqdev *dev = snd_pcm_substream_chip(substream);
	debug("%s(%p)\n", __func__, substream);
	substream->runtime->hw = dev->pcm_info;
	snd_pcm_limit_hw_rates(substream->runtime);
	return 0;
}

static int snd_usb_caiaq_substream_close(struct snd_pcm_substream *substream)
{
	struct snd_usb_caiaqdev *dev = snd_pcm_substream_chip(substream);

	debug("%s(%p)\n", __func__, substream);
	if (all_substreams_zero(dev->sub_playback) &&
	    all_substreams_zero(dev->sub_capture)) {
		/* when the last client has stopped streaming,
		 * all sample rates are allowed again */
		stream_stop(dev);
		dev->pcm_info.rates = dev->samplerates;
	}

	return 0;
}

static int snd_usb_caiaq_pcm_hw_params(struct snd_pcm_substream *sub,
			     		struct snd_pcm_hw_params *hw_params)
{
	debug("%s(%p)\n", __func__, sub);
	return snd_pcm_lib_malloc_pages(sub, params_buffer_bytes(hw_params));
}

static int snd_usb_caiaq_pcm_hw_free(struct snd_pcm_substream *sub)
{
	struct snd_usb_caiaqdev *dev = snd_pcm_substream_chip(sub);
	debug("%s(%p)\n", __func__, sub);
	deactivate_substream(dev, sub);
	return snd_pcm_lib_free_pages(sub);
}

/* this should probably go upstream */
#if SNDRV_PCM_RATE_5512 != 1 << 0 || SNDRV_PCM_RATE_192000 != 1 << 12
#error "Change this table"
#endif

static unsigned int rates[] = { 5512, 8000, 11025, 16000, 22050, 32000, 44100,
                                 48000, 64000, 88200, 96000, 176400, 192000 };

static int snd_usb_caiaq_pcm_prepare(struct snd_pcm_substream *substream)
{
	int bytes_per_sample, bpp, ret, i;
	int index = substream->number;
	struct snd_usb_caiaqdev *dev = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;

	debug("%s(%p)\n", __func__, substream);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		dev->period_out_count[index] = BYTES_PER_SAMPLE + 1;
		dev->audio_out_buf_pos[index] = BYTES_PER_SAMPLE + 1;
	} else {
		int in_pos = (dev->spec.data_alignment == 2) ? 0 : 2;
		dev->period_in_count[index] = BYTES_PER_SAMPLE + in_pos;
		dev->audio_in_buf_pos[index] = BYTES_PER_SAMPLE + in_pos;
	}

	if (dev->streaming)
		return 0;

	/* the first client that opens a stream defines the sample rate
	 * setting for all subsequent calls, until the last client closed. */
	for (i=0; i < ARRAY_SIZE(rates); i++)
		if (runtime->rate == rates[i])
			dev->pcm_info.rates = 1 << i;

	snd_pcm_limit_hw_rates(runtime);

	bytes_per_sample = BYTES_PER_SAMPLE;
	if (dev->spec.data_alignment == 2)
		bytes_per_sample++;

	bpp = ((runtime->rate / 8000) + CLOCK_DRIFT_TOLERANCE)
		* bytes_per_sample * CHANNELS_PER_STREAM * dev->n_streams;

	if (bpp > MAX_ENDPOINT_SIZE)
		bpp = MAX_ENDPOINT_SIZE;

	ret = snd_usb_caiaq_set_audio_params(dev, runtime->rate,
					     runtime->sample_bits, bpp);
	if (ret)
		return ret;

	ret = stream_start(dev);
	if (ret)
		return ret;

	dev->output_running = 0;
	wait_event_timeout(dev->prepare_wait_queue, dev->output_running, HZ);
	if (!dev->output_running) {
		stream_stop(dev);
		return -EPIPE;
	}

	return 0;
}

static int snd_usb_caiaq_pcm_trigger(struct snd_pcm_substream *sub, int cmd)
{
	struct snd_usb_caiaqdev *dev = snd_pcm_substream_chip(sub);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		activate_substream(dev, sub);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		deactivate_substream(dev, sub);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static snd_pcm_uframes_t
snd_usb_caiaq_pcm_pointer(struct snd_pcm_substream *sub)
{
	int index = sub->number;
	struct snd_usb_caiaqdev *dev = snd_pcm_substream_chip(sub);

	if (dev->input_panic || dev->output_panic)
		return SNDRV_PCM_POS_XRUN;

	if (sub->stream == SNDRV_PCM_STREAM_PLAYBACK)
		return bytes_to_frames(sub->runtime,
					dev->audio_out_buf_pos[index]);
	else
		return bytes_to_frames(sub->runtime,
					dev->audio_in_buf_pos[index]);
}

/* operators for both playback and capture */
static struct snd_pcm_ops snd_usb_caiaq_ops = {
	.open =		snd_usb_caiaq_substream_open,
	.close =	snd_usb_caiaq_substream_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_usb_caiaq_pcm_hw_params,
	.hw_free =	snd_usb_caiaq_pcm_hw_free,
	.prepare =	snd_usb_caiaq_pcm_prepare,
	.trigger =	snd_usb_caiaq_pcm_trigger,
	.pointer =	snd_usb_caiaq_pcm_pointer
};

static void check_for_elapsed_periods(struct snd_usb_caiaqdev *dev,
				      struct snd_pcm_substream **subs)
{
	int stream, pb, *cnt;
	struct snd_pcm_substream *sub;

	for (stream = 0; stream < dev->n_streams; stream++) {
		sub = subs[stream];
		if (!sub)
			continue;

		pb = snd_pcm_lib_period_bytes(sub);
		cnt = (sub->stream == SNDRV_PCM_STREAM_PLAYBACK) ?
					&dev->period_out_count[stream] :
					&dev->period_in_count[stream];

		if (*cnt >= pb) {
			snd_pcm_period_elapsed(sub);
			*cnt %= pb;
		}
	}
}

static void read_in_urb_mode0(struct snd_usb_caiaqdev *dev,
			      const struct urb *urb,
			      const struct usb_iso_packet_descriptor *iso)
{
	unsigned char *usb_buf = urb->transfer_buffer + iso->offset;
	struct snd_pcm_substream *sub;
	int stream, i;

	if (all_substreams_zero(dev->sub_capture))
		return;

	for (i = 0; i < iso->actual_length;) {
		for (stream = 0; stream < dev->n_streams; stream++, i++) {
			sub = dev->sub_capture[stream];
			if (sub) {
				struct snd_pcm_runtime *rt = sub->runtime;
				char *audio_buf = rt->dma_area;
				int sz = frames_to_bytes(rt, rt->buffer_size);
				audio_buf[dev->audio_in_buf_pos[stream]++]
					= usb_buf[i];
				dev->period_in_count[stream]++;
				if (dev->audio_in_buf_pos[stream] == sz)
					dev->audio_in_buf_pos[stream] = 0;
			}
		}
	}
}

static void read_in_urb_mode2(struct snd_usb_caiaqdev *dev,
			      const struct urb *urb,
			      const struct usb_iso_packet_descriptor *iso)
{
	unsigned char *usb_buf = urb->transfer_buffer + iso->offset;
	unsigned char check_byte;
	struct snd_pcm_substream *sub;
	int stream, i;

	for (i = 0; i < iso->actual_length;) {
		if (i % (dev->n_streams * BYTES_PER_SAMPLE_USB) == 0) {
			for (stream = 0;
			     stream < dev->n_streams;
			     stream++, i++) {
				if (dev->first_packet)
					continue;

				check_byte = MAKE_CHECKBYTE(dev, stream, i);

				if ((usb_buf[i] & 0x3f) != check_byte)
					dev->input_panic = 1;

				if (usb_buf[i] & 0x80)
					dev->output_panic = 1;
			}
		}
		dev->first_packet = 0;

		for (stream = 0; stream < dev->n_streams; stream++, i++) {
			sub = dev->sub_capture[stream];
			if (dev->input_panic)
				usb_buf[i] = 0;

			if (sub) {
				struct snd_pcm_runtime *rt = sub->runtime;
				char *audio_buf = rt->dma_area;
				int sz = frames_to_bytes(rt, rt->buffer_size);
				audio_buf[dev->audio_in_buf_pos[stream]++] =
					usb_buf[i];
				dev->period_in_count[stream]++;
				if (dev->audio_in_buf_pos[stream] == sz)
					dev->audio_in_buf_pos[stream] = 0;
			}
		}
	}
}

static void read_in_urb(struct snd_usb_caiaqdev *dev,
			const struct urb *urb,
			const struct usb_iso_packet_descriptor *iso)
{
	if (!dev->streaming)
		return;

	if (iso->actual_length < dev->bpp)
		return;

	switch (dev->spec.data_alignment) {
	case 0:
		read_in_urb_mode0(dev, urb, iso);
		break;
	case 2:
		read_in_urb_mode2(dev, urb, iso);
		break;
	}

	if ((dev->input_panic || dev->output_panic) && !dev->warned) {
		debug("streaming error detected %s %s\n",
				dev->input_panic ? "(input)" : "",
				dev->output_panic ? "(output)" : "");
		dev->warned = 1;
	}
}

static void fill_out_urb(struct snd_usb_caiaqdev *dev,
			 struct urb *urb,
			 const struct usb_iso_packet_descriptor *iso)
{
	unsigned char *usb_buf = urb->transfer_buffer + iso->offset;
	struct snd_pcm_substream *sub;
	int stream, i;

	for (i = 0; i < iso->length;) {
		for (stream = 0; stream < dev->n_streams; stream++, i++) {
			sub = dev->sub_playback[stream];
			if (sub) {
				struct snd_pcm_runtime *rt = sub->runtime;
				char *audio_buf = rt->dma_area;
				int sz = frames_to_bytes(rt, rt->buffer_size);
				usb_buf[i] =
					audio_buf[dev->audio_out_buf_pos[stream]];
				dev->period_out_count[stream]++;
				dev->audio_out_buf_pos[stream]++;
				if (dev->audio_out_buf_pos[stream] == sz)
					dev->audio_out_buf_pos[stream] = 0;
			} else
				usb_buf[i] = 0;
		}

		/* fill in the check bytes */
		if (dev->spec.data_alignment == 2 &&
		    i % (dev->n_streams * BYTES_PER_SAMPLE_USB) ==
		    	(dev->n_streams * CHANNELS_PER_STREAM))
		    for (stream = 0; stream < dev->n_streams; stream++, i++)
		    	usb_buf[i] = MAKE_CHECKBYTE(dev, stream, i);
	}
}

static void read_completed(struct urb *urb)
{
	struct snd_usb_caiaq_cb_info *info = urb->context;
	struct snd_usb_caiaqdev *dev;
	struct urb *out;
	int frame, len, send_it = 0, outframe = 0;

	if (urb->status || !info)
		return;

	dev = info->dev;

	if (!dev->streaming)
		return;

	out = dev->data_urbs_out[info->index];

	/* read the recently received packet and send back one which has
	 * the same layout */
	for (frame = 0; frame < FRAMES_PER_URB; frame++) {
		if (urb->iso_frame_desc[frame].status)
			continue;

		len = urb->iso_frame_desc[outframe].actual_length;
		out->iso_frame_desc[outframe].length = len;
		out->iso_frame_desc[outframe].actual_length = 0;
		out->iso_frame_desc[outframe].offset = BYTES_PER_FRAME * frame;

		if (len > 0) {
			spin_lock(&dev->spinlock);
			fill_out_urb(dev, out, &out->iso_frame_desc[outframe]);
			read_in_urb(dev, urb, &urb->iso_frame_desc[frame]);
			spin_unlock(&dev->spinlock);
			check_for_elapsed_periods(dev, dev->sub_playback);
			check_for_elapsed_periods(dev, dev->sub_capture);
			send_it = 1;
		}

		outframe++;
	}

	if (send_it) {
		out->number_of_packets = FRAMES_PER_URB;
		out->transfer_flags = URB_ISO_ASAP;
		usb_submit_urb(out, GFP_ATOMIC);
	}

	/* re-submit inbound urb */
	for (frame = 0; frame < FRAMES_PER_URB; frame++) {
		urb->iso_frame_desc[frame].offset = BYTES_PER_FRAME * frame;
		urb->iso_frame_desc[frame].length = BYTES_PER_FRAME;
		urb->iso_frame_desc[frame].actual_length = 0;
	}

	urb->number_of_packets = FRAMES_PER_URB;
	urb->transfer_flags = URB_ISO_ASAP;
	usb_submit_urb(urb, GFP_ATOMIC);
}

static void write_completed(struct urb *urb)
{
	struct snd_usb_caiaq_cb_info *info = urb->context;
	struct snd_usb_caiaqdev *dev = info->dev;

	if (!dev->output_running) {
		dev->output_running = 1;
		wake_up(&dev->prepare_wait_queue);
	}
}

static struct urb **alloc_urbs(struct snd_usb_caiaqdev *dev, int dir, int *ret)
{
	int i, frame;
	struct urb **urbs;
	struct usb_device *usb_dev = dev->chip.dev;
	unsigned int pipe;

	pipe = (dir == SNDRV_PCM_STREAM_PLAYBACK) ?
		usb_sndisocpipe(usb_dev, ENDPOINT_PLAYBACK) :
		usb_rcvisocpipe(usb_dev, ENDPOINT_CAPTURE);

	urbs = kmalloc(N_URBS * sizeof(*urbs), GFP_KERNEL);
	if (!urbs) {
		log("unable to kmalloc() urbs, OOM!?\n");
		*ret = -ENOMEM;
		return NULL;
	}

	for (i = 0; i < N_URBS; i++) {
		urbs[i] = usb_alloc_urb(FRAMES_PER_URB, GFP_KERNEL);
		if (!urbs[i]) {
			log("unable to usb_alloc_urb(), OOM!?\n");
			*ret = -ENOMEM;
			return urbs;
		}

		urbs[i]->transfer_buffer =
			kmalloc(FRAMES_PER_URB * BYTES_PER_FRAME, GFP_KERNEL);
		if (!urbs[i]->transfer_buffer) {
			log("unable to kmalloc() transfer buffer, OOM!?\n");
			*ret = -ENOMEM;
			return urbs;
		}

		for (frame = 0; frame < FRAMES_PER_URB; frame++) {
			struct usb_iso_packet_descriptor *iso =
				&urbs[i]->iso_frame_desc[frame];

			iso->offset = BYTES_PER_FRAME * frame;
			iso->length = BYTES_PER_FRAME;
		}

		urbs[i]->dev = usb_dev;
		urbs[i]->pipe = pipe;
		urbs[i]->transfer_buffer_length = FRAMES_PER_URB
						* BYTES_PER_FRAME;
		urbs[i]->context = &dev->data_cb_info[i];
		urbs[i]->interval = 1;
		urbs[i]->transfer_flags = URB_ISO_ASAP;
		urbs[i]->number_of_packets = FRAMES_PER_URB;
		urbs[i]->complete = (dir == SNDRV_PCM_STREAM_CAPTURE) ?
					read_completed : write_completed;
	}

	*ret = 0;
	return urbs;
}

static void free_urbs(struct urb **urbs)
{
	int i;

	if (!urbs)
		return;

	for (i = 0; i < N_URBS; i++) {
		if (!urbs[i])
			continue;

		usb_kill_urb(urbs[i]);
		kfree(urbs[i]->transfer_buffer);
		usb_free_urb(urbs[i]);
	}

	kfree(urbs);
}

int snd_usb_caiaq_audio_init(struct snd_usb_caiaqdev *dev)
{
	int i, ret;

	dev->n_audio_in  = max(dev->spec.num_analog_audio_in,
			       dev->spec.num_digital_audio_in) /
				CHANNELS_PER_STREAM;
	dev->n_audio_out = max(dev->spec.num_analog_audio_out,
			       dev->spec.num_digital_audio_out) /
				CHANNELS_PER_STREAM;
	dev->n_streams = max(dev->n_audio_in, dev->n_audio_out);

	debug("dev->n_audio_in = %d\n", dev->n_audio_in);
	debug("dev->n_audio_out = %d\n", dev->n_audio_out);
	debug("dev->n_streams = %d\n", dev->n_streams);

	if (dev->n_streams > MAX_STREAMS) {
		log("unable to initialize device, too many streams.\n");
		return -EINVAL;
	}

	ret = snd_pcm_new(dev->chip.card, dev->product_name, 0,
			dev->n_audio_out, dev->n_audio_in, &dev->pcm);

	if (ret < 0) {
		log("snd_pcm_new() returned %d\n", ret);
		return ret;
	}

	dev->pcm->private_data = dev;
	strcpy(dev->pcm->name, dev->product_name);

	memset(dev->sub_playback, 0, sizeof(dev->sub_playback));
	memset(dev->sub_capture, 0, sizeof(dev->sub_capture));

	memcpy(&dev->pcm_info, &snd_usb_caiaq_pcm_hardware,
			sizeof(snd_usb_caiaq_pcm_hardware));

	/* setup samplerates */
	dev->samplerates = dev->pcm_info.rates;
	switch (dev->chip.usb_id) {
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_AK1):
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_RIGKONTROL3):
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_SESSIONIO):
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_GUITARRIGMOBILE):
		dev->samplerates |= SNDRV_PCM_RATE_192000;
		/* fall thru */
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_AUDIO2DJ):
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_AUDIO4DJ):
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_AUDIO8DJ):
		dev->samplerates |= SNDRV_PCM_RATE_88200;
		break;
	}

	snd_pcm_set_ops(dev->pcm, SNDRV_PCM_STREAM_PLAYBACK,
				&snd_usb_caiaq_ops);
	snd_pcm_set_ops(dev->pcm, SNDRV_PCM_STREAM_CAPTURE,
				&snd_usb_caiaq_ops);

	snd_pcm_lib_preallocate_pages_for_all(dev->pcm,
					SNDRV_DMA_TYPE_CONTINUOUS,
					snd_dma_continuous_data(GFP_KERNEL),
					MAX_BUFFER_SIZE, MAX_BUFFER_SIZE);

	dev->data_cb_info =
		kmalloc(sizeof(struct snd_usb_caiaq_cb_info) * N_URBS,
					GFP_KERNEL);

	if (!dev->data_cb_info)
		return -ENOMEM;

	for (i = 0; i < N_URBS; i++) {
		dev->data_cb_info[i].dev = dev;
		dev->data_cb_info[i].index = i;
	}

	dev->data_urbs_in = alloc_urbs(dev, SNDRV_PCM_STREAM_CAPTURE, &ret);
	if (ret < 0) {
		kfree(dev->data_cb_info);
		free_urbs(dev->data_urbs_in);
		return ret;
	}

	dev->data_urbs_out = alloc_urbs(dev, SNDRV_PCM_STREAM_PLAYBACK, &ret);
	if (ret < 0) {
		kfree(dev->data_cb_info);
		free_urbs(dev->data_urbs_in);
		free_urbs(dev->data_urbs_out);
		return ret;
	}

	return 0;
}

void snd_usb_caiaq_audio_free(struct snd_usb_caiaqdev *dev)
{
	debug("%s(%p)\n", __func__, dev);
	stream_stop(dev);
	free_urbs(dev->data_urbs_in);
	free_urbs(dev->data_urbs_out);
	kfree(dev->data_cb_info);
}

