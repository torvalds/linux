// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (c) 2006-2008 Daniel Mack, Karsten Wiese
*/

#include <linux/device.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
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

#define MAKE_CHECKBYTE(cdev,stream,i) \
	(stream << 1) | (~(i / (cdev->n_streams * BYTES_PER_SAMPLE_USB)) & 1)

static const struct snd_pcm_hardware snd_usb_caiaq_pcm_hardware = {
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
activate_substream(struct snd_usb_caiaqdev *cdev,
	           struct snd_pcm_substream *sub)
{
	spin_lock(&cdev->spinlock);

	if (sub->stream == SNDRV_PCM_STREAM_PLAYBACK)
		cdev->sub_playback[sub->number] = sub;
	else
		cdev->sub_capture[sub->number] = sub;

	spin_unlock(&cdev->spinlock);
}

static void
deactivate_substream(struct snd_usb_caiaqdev *cdev,
		     struct snd_pcm_substream *sub)
{
	unsigned long flags;
	spin_lock_irqsave(&cdev->spinlock, flags);

	if (sub->stream == SNDRV_PCM_STREAM_PLAYBACK)
		cdev->sub_playback[sub->number] = NULL;
	else
		cdev->sub_capture[sub->number] = NULL;

	spin_unlock_irqrestore(&cdev->spinlock, flags);
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

static int stream_start(struct snd_usb_caiaqdev *cdev)
{
	int i, ret;
	struct device *dev = caiaqdev_to_dev(cdev);

	dev_dbg(dev, "%s(%p)\n", __func__, cdev);

	if (cdev->streaming)
		return -EINVAL;

	memset(cdev->sub_playback, 0, sizeof(cdev->sub_playback));
	memset(cdev->sub_capture, 0, sizeof(cdev->sub_capture));
	cdev->input_panic = 0;
	cdev->output_panic = 0;
	cdev->first_packet = 4;
	cdev->streaming = 1;
	cdev->warned = 0;

	for (i = 0; i < N_URBS; i++) {
		ret = usb_submit_urb(cdev->data_urbs_in[i], GFP_ATOMIC);
		if (ret) {
			dev_err(dev, "unable to trigger read #%d! (ret %d)\n",
				i, ret);
			cdev->streaming = 0;
			return -EPIPE;
		}
	}

	return 0;
}

static void stream_stop(struct snd_usb_caiaqdev *cdev)
{
	int i;
	struct device *dev = caiaqdev_to_dev(cdev);

	dev_dbg(dev, "%s(%p)\n", __func__, cdev);
	if (!cdev->streaming)
		return;

	cdev->streaming = 0;

	for (i = 0; i < N_URBS; i++) {
		usb_kill_urb(cdev->data_urbs_in[i]);

		if (test_bit(i, &cdev->outurb_active_mask))
			usb_kill_urb(cdev->data_urbs_out[i]);
	}

	cdev->outurb_active_mask = 0;
}

static int snd_usb_caiaq_substream_open(struct snd_pcm_substream *substream)
{
	struct snd_usb_caiaqdev *cdev = snd_pcm_substream_chip(substream);
	struct device *dev = caiaqdev_to_dev(cdev);

	dev_dbg(dev, "%s(%p)\n", __func__, substream);
	substream->runtime->hw = cdev->pcm_info;
	snd_pcm_limit_hw_rates(substream->runtime);

	return 0;
}

static int snd_usb_caiaq_substream_close(struct snd_pcm_substream *substream)
{
	struct snd_usb_caiaqdev *cdev = snd_pcm_substream_chip(substream);
	struct device *dev = caiaqdev_to_dev(cdev);

	dev_dbg(dev, "%s(%p)\n", __func__, substream);
	if (all_substreams_zero(cdev->sub_playback) &&
	    all_substreams_zero(cdev->sub_capture)) {
		/* when the last client has stopped streaming,
		 * all sample rates are allowed again */
		stream_stop(cdev);
		cdev->pcm_info.rates = cdev->samplerates;
	}

	return 0;
}

static int snd_usb_caiaq_pcm_hw_free(struct snd_pcm_substream *sub)
{
	struct snd_usb_caiaqdev *cdev = snd_pcm_substream_chip(sub);
	deactivate_substream(cdev, sub);
	return 0;
}

/* this should probably go upstream */
#if SNDRV_PCM_RATE_5512 != 1 << 0 || SNDRV_PCM_RATE_192000 != 1 << 12
#error "Change this table"
#endif

static const unsigned int rates[] = { 5512, 8000, 11025, 16000, 22050, 32000, 44100,
				48000, 64000, 88200, 96000, 176400, 192000 };

static int snd_usb_caiaq_pcm_prepare(struct snd_pcm_substream *substream)
{
	int bytes_per_sample, bpp, ret, i;
	int index = substream->number;
	struct snd_usb_caiaqdev *cdev = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct device *dev = caiaqdev_to_dev(cdev);

	dev_dbg(dev, "%s(%p)\n", __func__, substream);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		int out_pos;

		switch (cdev->spec.data_alignment) {
		case 0:
		case 2:
			out_pos = BYTES_PER_SAMPLE + 1;
			break;
		case 3:
		default:
			out_pos = 0;
			break;
		}

		cdev->period_out_count[index] = out_pos;
		cdev->audio_out_buf_pos[index] = out_pos;
	} else {
		int in_pos;

		switch (cdev->spec.data_alignment) {
		case 0:
			in_pos = BYTES_PER_SAMPLE + 2;
			break;
		case 2:
			in_pos = BYTES_PER_SAMPLE;
			break;
		case 3:
		default:
			in_pos = 0;
			break;
		}

		cdev->period_in_count[index] = in_pos;
		cdev->audio_in_buf_pos[index] = in_pos;
	}

	if (cdev->streaming)
		return 0;

	/* the first client that opens a stream defines the sample rate
	 * setting for all subsequent calls, until the last client closed. */
	for (i=0; i < ARRAY_SIZE(rates); i++)
		if (runtime->rate == rates[i])
			cdev->pcm_info.rates = 1 << i;

	snd_pcm_limit_hw_rates(runtime);

	bytes_per_sample = BYTES_PER_SAMPLE;
	if (cdev->spec.data_alignment >= 2)
		bytes_per_sample++;

	bpp = ((runtime->rate / 8000) + CLOCK_DRIFT_TOLERANCE)
		* bytes_per_sample * CHANNELS_PER_STREAM * cdev->n_streams;

	if (bpp > MAX_ENDPOINT_SIZE)
		bpp = MAX_ENDPOINT_SIZE;

	ret = snd_usb_caiaq_set_audio_params(cdev, runtime->rate,
					     runtime->sample_bits, bpp);
	if (ret)
		return ret;

	ret = stream_start(cdev);
	if (ret)
		return ret;

	cdev->output_running = 0;
	wait_event_timeout(cdev->prepare_wait_queue, cdev->output_running, HZ);
	if (!cdev->output_running) {
		stream_stop(cdev);
		return -EPIPE;
	}

	return 0;
}

static int snd_usb_caiaq_pcm_trigger(struct snd_pcm_substream *sub, int cmd)
{
	struct snd_usb_caiaqdev *cdev = snd_pcm_substream_chip(sub);
	struct device *dev = caiaqdev_to_dev(cdev);

	dev_dbg(dev, "%s(%p) cmd %d\n", __func__, sub, cmd);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		activate_substream(cdev, sub);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		deactivate_substream(cdev, sub);
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
	struct snd_usb_caiaqdev *cdev = snd_pcm_substream_chip(sub);
	snd_pcm_uframes_t ptr;

	spin_lock(&cdev->spinlock);

	if (cdev->input_panic || cdev->output_panic) {
		ptr = SNDRV_PCM_POS_XRUN;
		goto unlock;
	}

	if (sub->stream == SNDRV_PCM_STREAM_PLAYBACK)
		ptr = bytes_to_frames(sub->runtime,
					cdev->audio_out_buf_pos[index]);
	else
		ptr = bytes_to_frames(sub->runtime,
					cdev->audio_in_buf_pos[index]);

unlock:
	spin_unlock(&cdev->spinlock);
	return ptr;
}

/* operators for both playback and capture */
static const struct snd_pcm_ops snd_usb_caiaq_ops = {
	.open =		snd_usb_caiaq_substream_open,
	.close =	snd_usb_caiaq_substream_close,
	.hw_free =	snd_usb_caiaq_pcm_hw_free,
	.prepare =	snd_usb_caiaq_pcm_prepare,
	.trigger =	snd_usb_caiaq_pcm_trigger,
	.pointer =	snd_usb_caiaq_pcm_pointer,
};

static void check_for_elapsed_periods(struct snd_usb_caiaqdev *cdev,
				      struct snd_pcm_substream **subs)
{
	int stream, pb, *cnt;
	struct snd_pcm_substream *sub;

	for (stream = 0; stream < cdev->n_streams; stream++) {
		sub = subs[stream];
		if (!sub)
			continue;

		pb = snd_pcm_lib_period_bytes(sub);
		cnt = (sub->stream == SNDRV_PCM_STREAM_PLAYBACK) ?
					&cdev->period_out_count[stream] :
					&cdev->period_in_count[stream];

		if (*cnt >= pb) {
			snd_pcm_period_elapsed(sub);
			*cnt %= pb;
		}
	}
}

static void read_in_urb_mode0(struct snd_usb_caiaqdev *cdev,
			      const struct urb *urb,
			      const struct usb_iso_packet_descriptor *iso)
{
	unsigned char *usb_buf = urb->transfer_buffer + iso->offset;
	struct snd_pcm_substream *sub;
	int stream, i;

	if (all_substreams_zero(cdev->sub_capture))
		return;

	for (i = 0; i < iso->actual_length;) {
		for (stream = 0; stream < cdev->n_streams; stream++, i++) {
			sub = cdev->sub_capture[stream];
			if (sub) {
				struct snd_pcm_runtime *rt = sub->runtime;
				char *audio_buf = rt->dma_area;
				int sz = frames_to_bytes(rt, rt->buffer_size);
				audio_buf[cdev->audio_in_buf_pos[stream]++]
					= usb_buf[i];
				cdev->period_in_count[stream]++;
				if (cdev->audio_in_buf_pos[stream] == sz)
					cdev->audio_in_buf_pos[stream] = 0;
			}
		}
	}
}

static void read_in_urb_mode2(struct snd_usb_caiaqdev *cdev,
			      const struct urb *urb,
			      const struct usb_iso_packet_descriptor *iso)
{
	unsigned char *usb_buf = urb->transfer_buffer + iso->offset;
	unsigned char check_byte;
	struct snd_pcm_substream *sub;
	int stream, i;

	for (i = 0; i < iso->actual_length;) {
		if (i % (cdev->n_streams * BYTES_PER_SAMPLE_USB) == 0) {
			for (stream = 0;
			     stream < cdev->n_streams;
			     stream++, i++) {
				if (cdev->first_packet)
					continue;

				check_byte = MAKE_CHECKBYTE(cdev, stream, i);

				if ((usb_buf[i] & 0x3f) != check_byte)
					cdev->input_panic = 1;

				if (usb_buf[i] & 0x80)
					cdev->output_panic = 1;
			}
		}
		cdev->first_packet = 0;

		for (stream = 0; stream < cdev->n_streams; stream++, i++) {
			sub = cdev->sub_capture[stream];
			if (cdev->input_panic)
				usb_buf[i] = 0;

			if (sub) {
				struct snd_pcm_runtime *rt = sub->runtime;
				char *audio_buf = rt->dma_area;
				int sz = frames_to_bytes(rt, rt->buffer_size);
				audio_buf[cdev->audio_in_buf_pos[stream]++] =
					usb_buf[i];
				cdev->period_in_count[stream]++;
				if (cdev->audio_in_buf_pos[stream] == sz)
					cdev->audio_in_buf_pos[stream] = 0;
			}
		}
	}
}

static void read_in_urb_mode3(struct snd_usb_caiaqdev *cdev,
			      const struct urb *urb,
			      const struct usb_iso_packet_descriptor *iso)
{
	unsigned char *usb_buf = urb->transfer_buffer + iso->offset;
	struct device *dev = caiaqdev_to_dev(cdev);
	int stream, i;

	/* paranoia check */
	if (iso->actual_length % (BYTES_PER_SAMPLE_USB * CHANNELS_PER_STREAM))
		return;

	for (i = 0; i < iso->actual_length;) {
		for (stream = 0; stream < cdev->n_streams; stream++) {
			struct snd_pcm_substream *sub = cdev->sub_capture[stream];
			char *audio_buf = NULL;
			int c, n, sz = 0;

			if (sub && !cdev->input_panic) {
				struct snd_pcm_runtime *rt = sub->runtime;
				audio_buf = rt->dma_area;
				sz = frames_to_bytes(rt, rt->buffer_size);
			}

			for (c = 0; c < CHANNELS_PER_STREAM; c++) {
				/* 3 audio data bytes, followed by 1 check byte */
				if (audio_buf) {
					for (n = 0; n < BYTES_PER_SAMPLE; n++) {
						audio_buf[cdev->audio_in_buf_pos[stream]++] = usb_buf[i+n];

						if (cdev->audio_in_buf_pos[stream] == sz)
							cdev->audio_in_buf_pos[stream] = 0;
					}

					cdev->period_in_count[stream] += BYTES_PER_SAMPLE;
				}

				i += BYTES_PER_SAMPLE;

				if (usb_buf[i] != ((stream << 1) | c) &&
				    !cdev->first_packet) {
					if (!cdev->input_panic)
						dev_warn(dev, " EXPECTED: %02x got %02x, c %d, stream %d, i %d\n",
							 ((stream << 1) | c), usb_buf[i], c, stream, i);
					cdev->input_panic = 1;
				}

				i++;
			}
		}
	}

	if (cdev->first_packet > 0)
		cdev->first_packet--;
}

static void read_in_urb(struct snd_usb_caiaqdev *cdev,
			const struct urb *urb,
			const struct usb_iso_packet_descriptor *iso)
{
	struct device *dev = caiaqdev_to_dev(cdev);

	if (!cdev->streaming)
		return;

	if (iso->actual_length < cdev->bpp)
		return;

	switch (cdev->spec.data_alignment) {
	case 0:
		read_in_urb_mode0(cdev, urb, iso);
		break;
	case 2:
		read_in_urb_mode2(cdev, urb, iso);
		break;
	case 3:
		read_in_urb_mode3(cdev, urb, iso);
		break;
	}

	if ((cdev->input_panic || cdev->output_panic) && !cdev->warned) {
		dev_warn(dev, "streaming error detected %s %s\n",
				cdev->input_panic ? "(input)" : "",
				cdev->output_panic ? "(output)" : "");
		cdev->warned = 1;
	}
}

static void fill_out_urb_mode_0(struct snd_usb_caiaqdev *cdev,
				struct urb *urb,
				const struct usb_iso_packet_descriptor *iso)
{
	unsigned char *usb_buf = urb->transfer_buffer + iso->offset;
	struct snd_pcm_substream *sub;
	int stream, i;

	for (i = 0; i < iso->length;) {
		for (stream = 0; stream < cdev->n_streams; stream++, i++) {
			sub = cdev->sub_playback[stream];
			if (sub) {
				struct snd_pcm_runtime *rt = sub->runtime;
				char *audio_buf = rt->dma_area;
				int sz = frames_to_bytes(rt, rt->buffer_size);
				usb_buf[i] =
					audio_buf[cdev->audio_out_buf_pos[stream]];
				cdev->period_out_count[stream]++;
				cdev->audio_out_buf_pos[stream]++;
				if (cdev->audio_out_buf_pos[stream] == sz)
					cdev->audio_out_buf_pos[stream] = 0;
			} else
				usb_buf[i] = 0;
		}

		/* fill in the check bytes */
		if (cdev->spec.data_alignment == 2 &&
		    i % (cdev->n_streams * BYTES_PER_SAMPLE_USB) ==
		        (cdev->n_streams * CHANNELS_PER_STREAM))
			for (stream = 0; stream < cdev->n_streams; stream++, i++)
				usb_buf[i] = MAKE_CHECKBYTE(cdev, stream, i);
	}
}

static void fill_out_urb_mode_3(struct snd_usb_caiaqdev *cdev,
				struct urb *urb,
				const struct usb_iso_packet_descriptor *iso)
{
	unsigned char *usb_buf = urb->transfer_buffer + iso->offset;
	int stream, i;

	for (i = 0; i < iso->length;) {
		for (stream = 0; stream < cdev->n_streams; stream++) {
			struct snd_pcm_substream *sub = cdev->sub_playback[stream];
			char *audio_buf = NULL;
			int c, n, sz = 0;

			if (sub) {
				struct snd_pcm_runtime *rt = sub->runtime;
				audio_buf = rt->dma_area;
				sz = frames_to_bytes(rt, rt->buffer_size);
			}

			for (c = 0; c < CHANNELS_PER_STREAM; c++) {
				for (n = 0; n < BYTES_PER_SAMPLE; n++) {
					if (audio_buf) {
						usb_buf[i+n] = audio_buf[cdev->audio_out_buf_pos[stream]++];

						if (cdev->audio_out_buf_pos[stream] == sz)
							cdev->audio_out_buf_pos[stream] = 0;
					} else {
						usb_buf[i+n] = 0;
					}
				}

				if (audio_buf)
					cdev->period_out_count[stream] += BYTES_PER_SAMPLE;

				i += BYTES_PER_SAMPLE;

				/* fill in the check byte pattern */
				usb_buf[i++] = (stream << 1) | c;
			}
		}
	}
}

static inline void fill_out_urb(struct snd_usb_caiaqdev *cdev,
				struct urb *urb,
				const struct usb_iso_packet_descriptor *iso)
{
	switch (cdev->spec.data_alignment) {
	case 0:
	case 2:
		fill_out_urb_mode_0(cdev, urb, iso);
		break;
	case 3:
		fill_out_urb_mode_3(cdev, urb, iso);
		break;
	}
}

static void read_completed(struct urb *urb)
{
	struct snd_usb_caiaq_cb_info *info = urb->context;
	struct snd_usb_caiaqdev *cdev;
	struct device *dev;
	struct urb *out = NULL;
	int i, frame, len, send_it = 0, outframe = 0;
	unsigned long flags;
	size_t offset = 0;

	if (urb->status || !info)
		return;

	cdev = info->cdev;
	dev = caiaqdev_to_dev(cdev);

	if (!cdev->streaming)
		return;

	/* find an unused output urb that is unused */
	for (i = 0; i < N_URBS; i++)
		if (test_and_set_bit(i, &cdev->outurb_active_mask) == 0) {
			out = cdev->data_urbs_out[i];
			break;
		}

	if (!out) {
		dev_err(dev, "Unable to find an output urb to use\n");
		goto requeue;
	}

	/* read the recently received packet and send back one which has
	 * the same layout */
	for (frame = 0; frame < FRAMES_PER_URB; frame++) {
		if (urb->iso_frame_desc[frame].status)
			continue;

		len = urb->iso_frame_desc[outframe].actual_length;
		out->iso_frame_desc[outframe].length = len;
		out->iso_frame_desc[outframe].actual_length = 0;
		out->iso_frame_desc[outframe].offset = offset;
		offset += len;

		if (len > 0) {
			spin_lock_irqsave(&cdev->spinlock, flags);
			fill_out_urb(cdev, out, &out->iso_frame_desc[outframe]);
			read_in_urb(cdev, urb, &urb->iso_frame_desc[frame]);
			spin_unlock_irqrestore(&cdev->spinlock, flags);
			check_for_elapsed_periods(cdev, cdev->sub_playback);
			check_for_elapsed_periods(cdev, cdev->sub_capture);
			send_it = 1;
		}

		outframe++;
	}

	if (send_it) {
		out->number_of_packets = outframe;
		usb_submit_urb(out, GFP_ATOMIC);
	} else {
		struct snd_usb_caiaq_cb_info *oinfo = out->context;
		clear_bit(oinfo->index, &cdev->outurb_active_mask);
	}

requeue:
	/* re-submit inbound urb */
	for (frame = 0; frame < FRAMES_PER_URB; frame++) {
		urb->iso_frame_desc[frame].offset = BYTES_PER_FRAME * frame;
		urb->iso_frame_desc[frame].length = BYTES_PER_FRAME;
		urb->iso_frame_desc[frame].actual_length = 0;
	}

	urb->number_of_packets = FRAMES_PER_URB;
	usb_submit_urb(urb, GFP_ATOMIC);
}

static void write_completed(struct urb *urb)
{
	struct snd_usb_caiaq_cb_info *info = urb->context;
	struct snd_usb_caiaqdev *cdev = info->cdev;

	if (!cdev->output_running) {
		cdev->output_running = 1;
		wake_up(&cdev->prepare_wait_queue);
	}

	clear_bit(info->index, &cdev->outurb_active_mask);
}

static struct urb **alloc_urbs(struct snd_usb_caiaqdev *cdev, int dir, int *ret)
{
	int i, frame;
	struct urb **urbs;
	struct usb_device *usb_dev = cdev->chip.dev;
	unsigned int pipe;

	pipe = (dir == SNDRV_PCM_STREAM_PLAYBACK) ?
		usb_sndisocpipe(usb_dev, ENDPOINT_PLAYBACK) :
		usb_rcvisocpipe(usb_dev, ENDPOINT_CAPTURE);

	urbs = kmalloc_array(N_URBS, sizeof(*urbs), GFP_KERNEL);
	if (!urbs) {
		*ret = -ENOMEM;
		return NULL;
	}

	for (i = 0; i < N_URBS; i++) {
		urbs[i] = usb_alloc_urb(FRAMES_PER_URB, GFP_KERNEL);
		if (!urbs[i]) {
			*ret = -ENOMEM;
			return urbs;
		}

		urbs[i]->transfer_buffer =
			kmalloc_array(BYTES_PER_FRAME, FRAMES_PER_URB,
				      GFP_KERNEL);
		if (!urbs[i]->transfer_buffer) {
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
		urbs[i]->context = &cdev->data_cb_info[i];
		urbs[i]->interval = 1;
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

int snd_usb_caiaq_audio_init(struct snd_usb_caiaqdev *cdev)
{
	int i, ret;
	struct device *dev = caiaqdev_to_dev(cdev);

	cdev->n_audio_in  = max(cdev->spec.num_analog_audio_in,
			       cdev->spec.num_digital_audio_in) /
				CHANNELS_PER_STREAM;
	cdev->n_audio_out = max(cdev->spec.num_analog_audio_out,
			       cdev->spec.num_digital_audio_out) /
				CHANNELS_PER_STREAM;
	cdev->n_streams = max(cdev->n_audio_in, cdev->n_audio_out);

	dev_dbg(dev, "cdev->n_audio_in = %d\n", cdev->n_audio_in);
	dev_dbg(dev, "cdev->n_audio_out = %d\n", cdev->n_audio_out);
	dev_dbg(dev, "cdev->n_streams = %d\n", cdev->n_streams);

	if (cdev->n_streams > MAX_STREAMS) {
		dev_err(dev, "unable to initialize device, too many streams.\n");
		return -EINVAL;
	}

	if (cdev->n_streams < 1) {
		dev_err(dev, "bogus number of streams: %d\n", cdev->n_streams);
		return -EINVAL;
	}

	ret = snd_pcm_new(cdev->chip.card, cdev->product_name, 0,
			cdev->n_audio_out, cdev->n_audio_in, &cdev->pcm);

	if (ret < 0) {
		dev_err(dev, "snd_pcm_new() returned %d\n", ret);
		return ret;
	}

	cdev->pcm->private_data = cdev;
	strlcpy(cdev->pcm->name, cdev->product_name, sizeof(cdev->pcm->name));

	memset(cdev->sub_playback, 0, sizeof(cdev->sub_playback));
	memset(cdev->sub_capture, 0, sizeof(cdev->sub_capture));

	memcpy(&cdev->pcm_info, &snd_usb_caiaq_pcm_hardware,
			sizeof(snd_usb_caiaq_pcm_hardware));

	/* setup samplerates */
	cdev->samplerates = cdev->pcm_info.rates;
	switch (cdev->chip.usb_id) {
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_AK1):
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_RIGKONTROL3):
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_SESSIONIO):
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_GUITARRIGMOBILE):
		cdev->samplerates |= SNDRV_PCM_RATE_192000;
		fallthrough;
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_AUDIO2DJ):
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_AUDIO4DJ):
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_AUDIO8DJ):
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_TRAKTORAUDIO2):
		cdev->samplerates |= SNDRV_PCM_RATE_88200;
		break;
	}

	snd_pcm_set_ops(cdev->pcm, SNDRV_PCM_STREAM_PLAYBACK,
				&snd_usb_caiaq_ops);
	snd_pcm_set_ops(cdev->pcm, SNDRV_PCM_STREAM_CAPTURE,
				&snd_usb_caiaq_ops);
	snd_pcm_set_managed_buffer_all(cdev->pcm, SNDRV_DMA_TYPE_VMALLOC,
				       NULL, 0, 0);

	cdev->data_cb_info =
		kmalloc_array(N_URBS, sizeof(struct snd_usb_caiaq_cb_info),
					GFP_KERNEL);

	if (!cdev->data_cb_info)
		return -ENOMEM;

	cdev->outurb_active_mask = 0;
	BUILD_BUG_ON(N_URBS > (sizeof(cdev->outurb_active_mask) * 8));

	for (i = 0; i < N_URBS; i++) {
		cdev->data_cb_info[i].cdev = cdev;
		cdev->data_cb_info[i].index = i;
	}

	cdev->data_urbs_in = alloc_urbs(cdev, SNDRV_PCM_STREAM_CAPTURE, &ret);
	if (ret < 0) {
		kfree(cdev->data_cb_info);
		free_urbs(cdev->data_urbs_in);
		return ret;
	}

	cdev->data_urbs_out = alloc_urbs(cdev, SNDRV_PCM_STREAM_PLAYBACK, &ret);
	if (ret < 0) {
		kfree(cdev->data_cb_info);
		free_urbs(cdev->data_urbs_in);
		free_urbs(cdev->data_urbs_out);
		return ret;
	}

	return 0;
}

void snd_usb_caiaq_audio_free(struct snd_usb_caiaqdev *cdev)
{
	struct device *dev = caiaqdev_to_dev(cdev);

	dev_dbg(dev, "%s(%p)\n", __func__, cdev);
	stream_stop(cdev);
	free_urbs(cdev->data_urbs_in);
	free_urbs(cdev->data_urbs_out);
	kfree(cdev->data_cb_info);
}

