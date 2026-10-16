// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Linux driver for TerraTec DMX 6Fire USB
 *
 * PCM driver
 *
 * Author:	Torsten Schenk <torsten.schenk@zoho.com>
 * Created:	Jan 01, 2011
 * Copyright:	(C) Torsten Schenk
 */

#include <linux/bitops.h>
#include <sound/asoundef.h>
#include <sound/control.h>

#include "pcm.h"
#include "chip.h"
#include "comm.h"
#include "control.h"

/*
 * Every USB frame is a 4 byte header followed by 32-bit slots, one per
 * channel: 3 bytes of sample and a marker byte. Analog slots are marked
 * 0x40. Up to 96 kHz (altsettings 1 and 2) the playback frame carries two
 * more slots, 6/7, which carry the data for the S/PDIF transmitter in
 * the FPGA. Their marker byte tells the transmitter how to frame the
 * sample: 0x10 marks the left subframe and 0x00 the right one; 0x20 marks
 * the start of the channel status block (left subframe, every 192
 * frames); 0x04 is the channel status bit of the subframe; 0x01 is the
 * validity bit, set for non-audio data; 0x08 is the parity bit, which
 * makes the number of ones in the sample, the validity and channel status
 * bits and itself even.
 * At altsetting 3 (176.4/192 kHz) the packets hold the analog slots only.
 */
enum {
	OUT_N_ANALOG = 6, N_SPDIF = 2,
	OUT_N_CHANNELS = OUT_N_ANALOG + N_SPDIF, IN_N_CHANNELS = 4,
	SPDIF_OUT_SLOT = OUT_N_ANALOG,
	SPDIF_BLOCK_FRAMES = 192
};

/* keep next two synced with
 * FW_EP_W_MAX_PACKET_SIZE[] and RATES_MAX_PACKET_SIZE
 * and CONTROL_RATE_XXX in control.h */
static const int rates_in_packet_size[] = { 228, 228, 420, 420, 404, 404 };
static const int rates_out_packet_size[] = { 228, 228, 420, 420, 604, 604 };
static const int rates[] = { 44100, 48000, 88200, 96000, 176400, 192000 };
static const u8 rates_iec958_fs[] = {
	IEC958_AES3_CON_FS_44100, IEC958_AES3_CON_FS_48000,
	IEC958_AES3_CON_FS_88200, IEC958_AES3_CON_FS_96000,
	IEC958_AES3_CON_FS_176400, IEC958_AES3_CON_FS_192000 };
/* channel status bits that "IEC958 Playback Default" sets, bytes 0 and 1 */
static const u8 spdif_ctl_mask[] = {
	IEC958_AES0_NONAUDIO | IEC958_AES0_CON_NOT_COPYRIGHT |
		IEC958_AES0_CON_EMPHASIS,
	IEC958_AES1_CON_CATEGORY | IEC958_AES1_CON_ORIGINAL };
static const int rates_alsaid[] = {
	SNDRV_PCM_RATE_44100, SNDRV_PCM_RATE_48000,
	SNDRV_PCM_RATE_88200, SNDRV_PCM_RATE_96000,
	SNDRV_PCM_RATE_176400, SNDRV_PCM_RATE_192000 };

enum { /* settings for pcm */
	OUT_EP = 6, IN_EP = 2, MAX_BUFSIZE = 128 * 1024
};

enum { /* pcm streaming states */
	STREAM_DISABLED, /* no pcm streaming */
	STREAM_STARTING, /* pcm streaming requested, waiting to become ready */
	STREAM_RUNNING, /* pcm streaming running */
	STREAM_STOPPING
};

static const struct snd_pcm_hardware pcm_hw = {
	.info = SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_BATCH,

	.formats = SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE,

	.rates = SNDRV_PCM_RATE_44100 |
		SNDRV_PCM_RATE_48000 |
		SNDRV_PCM_RATE_88200 |
		SNDRV_PCM_RATE_96000 |
		SNDRV_PCM_RATE_176400 |
		SNDRV_PCM_RATE_192000,

	.rate_min = 44100,
	.rate_max = 192000,
	.channels_min = 1,
	.channels_max = 0, /* set in pcm_open, depending on capture/playback */
	.buffer_bytes_max = MAX_BUFSIZE,
	.period_bytes_min = PCM_N_PACKETS_PER_URB * (PCM_MAX_PACKET_SIZE - 4),
	.period_bytes_max = MAX_BUFSIZE,
	.periods_min = 2,
	.periods_max = 1024
};

static int usb6fire_pcm_set_rate(struct pcm_runtime *rt)
{
	int ret;
	struct control_runtime *ctrl_rt = rt->chip->control;
	bool spdif;

	ctrl_rt->usb_streaming = false;
	ret = ctrl_rt->update_streaming(ctrl_rt);
	if (ret < 0) {
		dev_err(&rt->chip->dev->dev,
			"error stopping streaming while setting samplerate %d.\n",
			rates[rt->rate]);
		return ret;
	}

	ret = ctrl_rt->set_rate(ctrl_rt, rt->rate);
	if (ret < 0) {
		dev_err(&rt->chip->dev->dev,
			"error setting samplerate %d.\n",
			rates[rt->rate]);
		return ret;
	}

	spdif = rt->rate <= CONTROL_RATE_96KHZ;
	ret = ctrl_rt->set_channels(ctrl_rt, OUT_N_ANALOG, IN_N_CHANNELS,
			spdif, false);
	if (ret < 0) {
		dev_err(&rt->chip->dev->dev,
			"error initializing channels while setting samplerate %d.\n",
			rates[rt->rate]);
		return ret;
	}

	ctrl_rt->usb_streaming = true;
	ret = ctrl_rt->update_streaming(ctrl_rt);
	if (ret < 0) {
		dev_err(&rt->chip->dev->dev,
			"error starting streaming while setting samplerate %d.\n",
			rates[rt->rate]);
		/* stream_stop() will not run; clear 0x02/0x03 here */
		ctrl_rt->usb_streaming = false;
		ctrl_rt->update_streaming(ctrl_rt);
		return ret;
	}

	rt->in_n_analog = IN_N_CHANNELS;
	rt->out_n_slots = spdif ? OUT_N_CHANNELS : OUT_N_ANALOG;
	rt->in_packet_size = rates_in_packet_size[rt->rate];
	rt->out_packet_size = rates_out_packet_size[rt->rate];

	/*
	 * consumer channel status, 24 bit samples; bytes 0 and 1 are taken
	 * from rt->spdif_bits at the start of every block
	 */
	memset(rt->spdif_status, 0, sizeof(rt->spdif_status));
	rt->spdif_status[3] = rates_iec958_fs[rt->rate];
	rt->spdif_status[4] = IEC958_AES4_CON_MAX_WORDLEN_24 |
			IEC958_AES4_CON_WORDLEN_24_20;
	rt->spdif_frame = 0;
	return 0;
}

/*
 * Return the marker byte of one playback slot whose 3 sample bytes are
 * already in place. The marker is 0x40 for an analog slot and the S/PDIF
 * framing for a digital slot. With "IEC958 Playback Switch" off, the
 * function also clears the sample bytes of a digital slot. After the right
 * subframe, the function advances the channel status position.
 */
static u8 usb6fire_pcm_marker(struct pcm_runtime *rt, int slot,
		u8 *sample)
{
	bool right = slot == SPDIF_OUT_SLOT + 1;
	unsigned int n = rt->spdif_frame;
	u8 marker = right ? 0x00 : 0x10;
	u8 status;

	if (slot < SPDIF_OUT_SLOT)
		return 0x40;
	if (!right && n == 0) {
		/* a new block takes the bits of "IEC958 Playback Default" */
		u32 bits = READ_ONCE(rt->spdif_bits);

		rt->spdif_status[0] = bits & 0xff;
		rt->spdif_status[1] = bits >> 8;
		/*
		 * "IEC958 Playback Switch" off omits the block start flag and
		 * clears the sample bytes of the digital slots
		 */
		rt->spdif_on = READ_ONCE(rt->spdif_switch);
		if (rt->spdif_on)
			marker |= 0x20;
	}
	if (!rt->spdif_on)
		memset(sample, 0, 3);
	status = rt->spdif_status[n >> 3];
	/* byte 2 holds the channel number: 1 = A (left), 2 = B (right) */
	if ((n >> 3) == 2)
		status |= right ? (2 << 4) : (1 << 4);
	if (status & (1 << (n & 7)))
		marker |= 0x04;
	/* V = 1 for non-audio data (AC3, DTS), as the vendor driver does */
	if (rt->spdif_status[0] & IEC958_AES0_NONAUDIO)
		marker |= 0x01;
	/* P makes the number of ones in bits 0-27 (sample, V, U, C, P) even */
	if (parity8(sample[0] ^ sample[1] ^ sample[2] ^ (marker & 0x07)))
		marker |= 0x08;
	if (right)
		rt->spdif_frame = (n + 1) % SPDIF_BLOCK_FRAMES;
	return marker;
}

/* more than OUT_N_ANALOG playback channels need a rate up to 96 kHz */
static int usb6fire_pcm_rule_channels(struct snd_pcm_hw_params *params,
		struct snd_pcm_hw_rule *rule)
{
	const struct snd_interval *r =
		hw_param_interval_c(params, SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval t = { .min = 1, .max = OUT_N_ANALOG, .integer = 1 };

	if (r->min <= rates[CONTROL_RATE_96KHZ])
		return 0;
	return snd_interval_refine(hw_param_interval(params,
			SNDRV_PCM_HW_PARAM_CHANNELS), &t);
}

static int usb6fire_pcm_rule_rate(struct snd_pcm_hw_params *params,
		struct snd_pcm_hw_rule *rule)
{
	const struct snd_interval *c =
		hw_param_interval_c(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_interval t = {
		.min = 0, .max = rates[CONTROL_RATE_96KHZ], .integer = 1
	};

	if (c->min <= OUT_N_ANALOG)
		return 0;
	return snd_interval_refine(hw_param_interval(params,
			SNDRV_PCM_HW_PARAM_RATE), &t);
}

static struct pcm_substream *usb6fire_pcm_get_substream(
		struct snd_pcm_substream *alsa_sub)
{
	struct pcm_runtime *rt = snd_pcm_substream_chip(alsa_sub);

	if (alsa_sub->stream == SNDRV_PCM_STREAM_PLAYBACK)
		return &rt->playback;
	else if (alsa_sub->stream == SNDRV_PCM_STREAM_CAPTURE)
		return &rt->capture;
	dev_err(&rt->chip->dev->dev, "error getting pcm substream slot.\n");
	return NULL;
}

/* call with stream_mutex locked */
static void usb6fire_pcm_stream_stop(struct pcm_runtime *rt)
{
	int i;
	struct control_runtime *ctrl_rt = rt->chip->control;

	if (rt->stream_state != STREAM_DISABLED) {

		rt->stream_state = STREAM_STOPPING;

		for (i = 0; i < PCM_N_URBS; i++) {
			usb_kill_urb(rt->in_urbs[i].instance);
			usb_kill_urb(rt->out_urbs[i].instance);
		}
		ctrl_rt->usb_streaming = false;
		ctrl_rt->update_streaming(ctrl_rt);
		rt->stream_state = STREAM_DISABLED;
	}
}

/* call with stream_mutex locked */
static int usb6fire_pcm_stream_start(struct pcm_runtime *rt)
{
	int ret;
	int i;
	int k;
	struct usb_iso_packet_descriptor *packet;

	if (rt->stream_state == STREAM_DISABLED) {
		/* submit our in urbs */
		rt->stream_wait_cond = false;
		rt->stream_state = STREAM_STARTING;
		for (i = 0; i < PCM_N_URBS; i++) {
			for (k = 0; k < PCM_N_PACKETS_PER_URB; k++) {
				packet = &rt->in_urbs[i].instance->iso_frame_desc[k];
				packet->offset = k * rt->in_packet_size;
				packet->length = rt->in_packet_size;
				packet->actual_length = 0;
				packet->status = 0;
			}
			ret = usb_submit_urb(rt->in_urbs[i].instance,
					GFP_ATOMIC);
			if (ret) {
				usb6fire_pcm_stream_stop(rt);
				return ret;
			}
		}

		/* wait for first out urb to return (sent in urb handler) */
		wait_event_timeout(rt->stream_wait_queue, rt->stream_wait_cond,
				HZ);
		if (rt->stream_wait_cond)
			rt->stream_state = STREAM_RUNNING;
		else {
			usb6fire_pcm_stream_stop(rt);
			return -EIO;
		}
	}
	return 0;
}

/* call with substream locked */
static void usb6fire_pcm_capture(struct pcm_substream *sub, struct pcm_urb *urb)
{
	int i;
	int frame;
	int frame_count;
	unsigned int total_length = 0;
	struct pcm_runtime *rt = snd_pcm_substream_chip(sub->instance);
	struct snd_pcm_runtime *alsa_rt = sub->instance->runtime;
	struct usb_iso_packet_descriptor *isoc;
	u32 *src = NULL;
	u32 *dest = (u32 *) (alsa_rt->dma_area + sub->dma_off
			* (alsa_rt->frame_bits >> 3));
	u32 *dest_end = (u32 *) (alsa_rt->dma_area + alsa_rt->buffer_size
			* (alsa_rt->frame_bits >> 3));
	int bytes_per_frame = alsa_rt->channels << 2;

	for (i = 0; i < PCM_N_PACKETS_PER_URB; i++) {
		/* at least 4 header bytes for valid packet.
		 * after that: 32 bits per sample for analog channels */
		isoc = &urb->instance->iso_frame_desc[i];
		if (isoc->actual_length > 4)
			frame_count = (isoc->actual_length - 4)
					/ (rt->in_n_analog << 2);
		else
			frame_count = 0;

		if (alsa_rt->format == SNDRV_PCM_FORMAT_S24_LE)
			src = (u32 *) (urb->buffer + total_length);
		else if (alsa_rt->format == SNDRV_PCM_FORMAT_S32_LE)
			src = (u32 *) (urb->buffer - 1 + total_length);
		else
			return;
		src++; /* skip leading 4 bytes of every packet */
		total_length += isoc->length;
		for (frame = 0; frame < frame_count; frame++) {
			memcpy(dest, src, bytes_per_frame);
			dest += alsa_rt->channels;
			src += rt->in_n_analog;
			sub->dma_off++;
			sub->period_off++;
			if (dest == dest_end) {
				sub->dma_off = 0;
				dest = (u32 *) alsa_rt->dma_area;
			}
		}
	}
}

/* call with substream locked */
static void usb6fire_pcm_playback(struct pcm_substream *sub,
		struct pcm_urb *urb)
{
	int i;
	int frame;
	int frame_count;
	struct pcm_runtime *rt = snd_pcm_substream_chip(sub->instance);
	struct snd_pcm_runtime *alsa_rt = sub->instance->runtime;
	struct usb_iso_packet_descriptor *isoc;
	u32 *src = (u32 *) (alsa_rt->dma_area + sub->dma_off
			* (alsa_rt->frame_bits >> 3));
	u32 *src_end = (u32 *) (alsa_rt->dma_area + alsa_rt->buffer_size
			* (alsa_rt->frame_bits >> 3));
	u32 *dest;
	int bytes_per_frame = alsa_rt->channels << 2;

	if (alsa_rt->format == SNDRV_PCM_FORMAT_S32_LE)
		dest = (u32 *) (urb->buffer - 1);
	else if (alsa_rt->format == SNDRV_PCM_FORMAT_S24_LE)
		dest = (u32 *) (urb->buffer);
	else {
		dev_err(&rt->chip->dev->dev, "Unknown sample format.");
		return;
	}

	for (i = 0; i < PCM_N_PACKETS_PER_URB; i++) {
		/*
		 * at least 4 header bytes for valid packet.
		 * after that: 32 bits per slot
		 */
		isoc = &urb->instance->iso_frame_desc[i];
		if (isoc->length > 4)
			frame_count = (isoc->length - 4)
					/ (rt->out_n_slots << 2);
		else
			frame_count = 0;
		dest++; /* skip leading 4 bytes of every frame */
		for (frame = 0; frame < frame_count; frame++) {
			memcpy(dest, src, bytes_per_frame);
			src += alsa_rt->channels;
			dest += rt->out_n_slots;
			sub->dma_off++;
			sub->period_off++;
			if (src == src_end) {
				src = (u32 *) alsa_rt->dma_area;
				sub->dma_off = 0;
			}
		}
	}
}

static void usb6fire_pcm_in_urb_handler(struct urb *usb_urb)
{
	struct pcm_urb *in_urb = usb_urb->context;
	struct pcm_urb *out_urb = in_urb->peer;
	struct pcm_runtime *rt = in_urb->chip->pcm;
	struct pcm_substream *sub;
	struct usb_iso_packet_descriptor *isoc_out, *isoc_in;
	bool period_elapsed;
	int total_length = 0;
	int frame_count;
	int frame;
	int channel;
	int i;
	u8 *dest;
	unsigned int spdif_frame;

	if (usb_urb->status || rt->panic || rt->stream_state == STREAM_STOPPING)
		return;
	for (i = 0; i < PCM_N_PACKETS_PER_URB; i++) {
		isoc_in = &in_urb->instance->iso_frame_desc[i];
		if (isoc_in->status) {
			rt->panic = true;
			return;
		}
	}

	if (rt->stream_state == STREAM_DISABLED) {
		dev_err(&rt->chip->dev->dev,
			"internal error: stream disabled in in-urb handler.\n");
		return;
	}

	/* receive our capture data */
	sub = &rt->capture;
	period_elapsed = false;
	scoped_guard(spinlock_irqsave, &sub->lock) {
		if (sub->active) {
			usb6fire_pcm_capture(sub, in_urb);
			if (sub->period_off >= sub->instance->runtime->period_size) {
				sub->period_off %= sub->instance->runtime->period_size;
				period_elapsed = true;
			}
		}
	}
	if (period_elapsed)
		snd_pcm_period_elapsed(sub->instance);

	/* setup out urb structure */
	for (i = 0; i < PCM_N_PACKETS_PER_URB; i++) {
		unsigned int frames = 0;

		isoc_out = &out_urb->instance->iso_frame_desc[i];
		isoc_in = &in_urb->instance->iso_frame_desc[i];
		if (isoc_in->actual_length > 4)
			frames = (isoc_in->actual_length - 4)
					/ (rt->in_n_analog << 2);
		frames = min_t(unsigned int, frames,
				       (rt->out_packet_size - 4)
				       / (rt->out_n_slots << 2));

		isoc_out->offset = total_length;
		isoc_out->length = frames * (rt->out_n_slots << 2) + 4;
		isoc_out->status = 0;
		total_length += isoc_out->length;
	}
	memset(out_urb->buffer, 0, total_length);

	/* now send our playback data (if a free out urb was found) */
	sub = &rt->playback;
	period_elapsed = false;
	scoped_guard(spinlock_irqsave, &sub->lock) {
		if (sub->active) {
			usb6fire_pcm_playback(sub, out_urb);
			if (sub->period_off >= sub->instance->runtime->period_size) {
				sub->period_off %= sub->instance->runtime->period_size;
				period_elapsed = true;
			}
		}
	}
	if (period_elapsed)
		snd_pcm_period_elapsed(sub->instance);

	/*
	 * set the 4th byte of each sample (0x40 for analog channels,
	 * S/PDIF framing for the digital pair)
	 */
	spdif_frame = rt->spdif_frame;
	dest = out_urb->buffer;
	for (i = 0; i < PCM_N_PACKETS_PER_URB; i++) {
		isoc_out = &out_urb->instance->iso_frame_desc[i];
		if (isoc_out->length >= 4) {
			frame_count = (isoc_out->length - 4)
					/ (rt->out_n_slots << 2);
			*(dest++) = 0xaa;
			*(dest++) = 0xaa;
			*(dest++) = frame_count;
			*(dest++) = 0x00;
			for (frame = 0; frame < frame_count; frame++)
				for (channel = 0;
						channel < rt->out_n_slots;
						channel++) {
					dest[3] = usb6fire_pcm_marker(rt,
							channel, dest);
					dest += 4;
				}
		}
	}

	/*
	 * the frames of an out urb that was not submitted never reach the
	 * transmitter; the next urb continues the block at the same position
	 */
	if (usb_submit_urb(out_urb->instance, GFP_ATOMIC) < 0)
		rt->spdif_frame = spdif_frame;
	usb_submit_urb(in_urb->instance, GFP_ATOMIC);
}

static void usb6fire_pcm_out_urb_handler(struct urb *usb_urb)
{
	struct pcm_urb *urb = usb_urb->context;
	struct pcm_runtime *rt = urb->chip->pcm;

	if (rt->stream_state == STREAM_STARTING) {
		rt->stream_wait_cond = true;
		wake_up(&rt->stream_wait_queue);
	}
}

static int usb6fire_pcm_open(struct snd_pcm_substream *alsa_sub)
{
	struct pcm_runtime *rt = snd_pcm_substream_chip(alsa_sub);
	struct pcm_substream *sub = NULL;
	struct snd_pcm_runtime *alsa_rt = alsa_sub->runtime;
	int ret;

	if (rt->panic)
		return -EPIPE;

	guard(mutex)(&rt->stream_mutex);
	alsa_rt->hw = pcm_hw;

	if (alsa_sub->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		if (rt->rate < ARRAY_SIZE(rates))
			alsa_rt->hw.rates = rates_alsaid[rt->rate];
		alsa_rt->hw.channels_max = OUT_N_CHANNELS;
		ret = snd_pcm_hw_rule_add(alsa_rt, 0,
				SNDRV_PCM_HW_PARAM_CHANNELS,
				usb6fire_pcm_rule_channels, NULL,
				SNDRV_PCM_HW_PARAM_RATE, -1);
		if (ret < 0)
			return ret;
		ret = snd_pcm_hw_rule_add(alsa_rt, 0, SNDRV_PCM_HW_PARAM_RATE,
				usb6fire_pcm_rule_rate, NULL,
				SNDRV_PCM_HW_PARAM_CHANNELS, -1);
		if (ret < 0)
			return ret;
		sub = &rt->playback;
	} else if (alsa_sub->stream == SNDRV_PCM_STREAM_CAPTURE) {
		if (rt->rate < ARRAY_SIZE(rates))
			alsa_rt->hw.rates = rates_alsaid[rt->rate];
		alsa_rt->hw.channels_max = IN_N_CHANNELS;
		sub = &rt->capture;
	}

	if (!sub) {
		dev_err(&rt->chip->dev->dev, "invalid stream type.\n");
		return -EINVAL;
	}

	sub->instance = alsa_sub;
	sub->active = false;
	return 0;
}

static int usb6fire_pcm_close(struct snd_pcm_substream *alsa_sub)
{
	struct pcm_runtime *rt = snd_pcm_substream_chip(alsa_sub);
	struct pcm_substream *sub = usb6fire_pcm_get_substream(alsa_sub);

	if (rt->panic)
		return 0;

	guard(mutex)(&rt->stream_mutex);
	if (sub) {
		/* deactivate substream */
		scoped_guard(spinlock_irqsave, &sub->lock) {
			sub->instance = NULL;
			sub->active = false;
		}

		/* all substreams closed? if so, stop streaming */
		if (!rt->playback.instance && !rt->capture.instance) {
			usb6fire_pcm_stream_stop(rt);
			rt->rate = ARRAY_SIZE(rates);
		}
	}
	return 0;
}

static int usb6fire_pcm_prepare(struct snd_pcm_substream *alsa_sub)
{
	struct pcm_runtime *rt = snd_pcm_substream_chip(alsa_sub);
	struct pcm_substream *sub = usb6fire_pcm_get_substream(alsa_sub);
	struct snd_pcm_runtime *alsa_rt = alsa_sub->runtime;
	int ret;

	if (rt->panic)
		return -EPIPE;
	if (!sub)
		return -ENODEV;

	guard(mutex)(&rt->stream_mutex);
	sub->dma_off = 0;
	sub->period_off = 0;

	if (rt->stream_state == STREAM_DISABLED) {
		for (rt->rate = 0; rt->rate < ARRAY_SIZE(rates); rt->rate++)
			if (alsa_rt->rate == rates[rt->rate])
				break;
		if (rt->rate == ARRAY_SIZE(rates)) {
			dev_err(&rt->chip->dev->dev,
				"invalid rate %d in prepare.\n",
				alsa_rt->rate);
			return -EINVAL;
		}
	}
	/* the digital pair is not available at 176.4 and 192 kHz */
	if (rt->rate > CONTROL_RATE_96KHZ &&
			alsa_sub->stream == SNDRV_PCM_STREAM_PLAYBACK &&
			alsa_rt->channels > OUT_N_ANALOG) {
		dev_err(&rt->chip->dev->dev,
			"%d channels not available at %d Hz.\n",
			alsa_rt->channels, rates[rt->rate]);
		return -EINVAL;
	}
	if (rt->stream_state == STREAM_DISABLED) {
		ret = usb6fire_pcm_set_rate(rt);
		if (ret)
			return ret;
		ret = usb6fire_pcm_stream_start(rt);
		if (ret) {
			dev_err(&rt->chip->dev->dev,
				"could not start pcm stream.\n");
			return ret;
		}
	}
	return 0;
}

static int usb6fire_pcm_trigger(struct snd_pcm_substream *alsa_sub, int cmd)
{
	struct pcm_substream *sub = usb6fire_pcm_get_substream(alsa_sub);
	struct pcm_runtime *rt = snd_pcm_substream_chip(alsa_sub);

	if (rt->panic)
		return -EPIPE;
	if (!sub)
		return -ENODEV;

	guard(spinlock_irqsave)(&sub->lock);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		sub->active = true;
		return 0;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		sub->active = false;
		return 0;

	default:
		return -EINVAL;
	}
}

static snd_pcm_uframes_t usb6fire_pcm_pointer(
		struct snd_pcm_substream *alsa_sub)
{
	struct pcm_substream *sub = usb6fire_pcm_get_substream(alsa_sub);
	struct pcm_runtime *rt = snd_pcm_substream_chip(alsa_sub);
	snd_pcm_uframes_t ret;

	if (rt->panic || !sub)
		return SNDRV_PCM_POS_XRUN;

	guard(spinlock_irqsave)(&sub->lock);
	ret = sub->dma_off;
	return ret;
}

static const struct snd_pcm_ops pcm_ops = {
	.open = usb6fire_pcm_open,
	.close = usb6fire_pcm_close,
	.prepare = usb6fire_pcm_prepare,
	.trigger = usb6fire_pcm_trigger,
	.pointer = usb6fire_pcm_pointer,
};

static int usb6fire_pcm_init_urb(struct pcm_urb *urb,
				 struct sfire_chip *chip, bool in, int ep,
				 void (*handler)(struct urb *))
{
	urb->chip = chip;
	urb->instance = usb_alloc_urb(PCM_N_PACKETS_PER_URB, GFP_KERNEL);
	if (!urb->instance)
		return -ENOMEM;
	urb->instance->transfer_buffer = urb->buffer;
	urb->instance->transfer_buffer_length =
			PCM_N_PACKETS_PER_URB * PCM_MAX_PACKET_SIZE;
	urb->instance->dev = chip->dev;
	urb->instance->pipe = in ? usb_rcvisocpipe(chip->dev, ep)
			: usb_sndisocpipe(chip->dev, ep);
	urb->instance->interval = 1;
	urb->instance->complete = handler;
	urb->instance->context = urb;
	urb->instance->number_of_packets = PCM_N_PACKETS_PER_URB;
	return 0;
}

static int usb6fire_pcm_buffers_init(struct pcm_runtime *rt)
{
	int i;

	for (i = 0; i < PCM_N_URBS; i++) {
		rt->out_urbs[i].buffer = kcalloc(PCM_MAX_PACKET_SIZE,
						 PCM_N_PACKETS_PER_URB,
						 GFP_KERNEL);
		if (!rt->out_urbs[i].buffer)
			return -ENOMEM;
		rt->in_urbs[i].buffer = kcalloc(PCM_MAX_PACKET_SIZE,
						PCM_N_PACKETS_PER_URB,
						GFP_KERNEL);
		if (!rt->in_urbs[i].buffer)
			return -ENOMEM;
	}
	return 0;
}

static void usb6fire_pcm_free(struct pcm_runtime *rt)
{
	int i;

	if (!rt)
		return;

	if (rt->chip)
		rt->chip->pcm = NULL;

	for (i = 0; i < PCM_N_URBS; i++) {
		usb_free_urb(rt->out_urbs[i].instance);
		kfree(rt->out_urbs[i].buffer);
		usb_free_urb(rt->in_urbs[i].instance);
		kfree(rt->in_urbs[i].buffer);
	}
	kfree(rt);
}

static int usb6fire_pcm_spdif_info(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int usb6fire_pcm_spdif_mask_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	memset(ucontrol->value.iec958.status, 0,
	       sizeof(ucontrol->value.iec958.status));
	ucontrol->value.iec958.status[0] = spdif_ctl_mask[0];
	ucontrol->value.iec958.status[1] = spdif_ctl_mask[1];
	return 0;
}

static int usb6fire_pcm_spdif_default_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct pcm_runtime *rt = snd_kcontrol_chip(kcontrol);
	u32 bits = READ_ONCE(rt->spdif_bits);

	memset(ucontrol->value.iec958.status, 0,
	       sizeof(ucontrol->value.iec958.status));
	ucontrol->value.iec958.status[0] = bits & 0xff;
	ucontrol->value.iec958.status[1] = bits >> 8;
	return 0;
}

static int usb6fire_pcm_spdif_default_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct pcm_runtime *rt = snd_kcontrol_chip(kcontrol);
	u32 bits = (ucontrol->value.iec958.status[0] & spdif_ctl_mask[0]) |
		(ucontrol->value.iec958.status[1] & spdif_ctl_mask[1]) << 8;

	/* the control core serializes puts; the urb handler uses READ_ONCE */
	if (rt->spdif_bits == bits)
		return 0;
	WRITE_ONCE(rt->spdif_bits, bits);
	return 1;
}

static int usb6fire_pcm_spdif_switch_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct pcm_runtime *rt = snd_kcontrol_chip(kcontrol);

	ucontrol->value.integer.value[0] = READ_ONCE(rt->spdif_switch);
	return 0;
}

static int usb6fire_pcm_spdif_switch_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct pcm_runtime *rt = snd_kcontrol_chip(kcontrol);
	bool on = ucontrol->value.integer.value[0];

	/* the control core serializes puts; the urb handler uses READ_ONCE */
	if (rt->spdif_switch == on)
		return 0;
	WRITE_ONCE(rt->spdif_switch, on);
	return 1;
}

/*
 * playback channel map: channel n goes to analog output n, which the
 * manual of the card assigns to FL FR FC LFE RL RR for n = 1..6; a
 * 1-channel stream is MONO, as in snd_pcm_std_chmaps, and plays on
 * analog output 1 only; channels 7 and 8 are the S/PDIF pair and have
 * no position
 */
static const struct snd_pcm_chmap_elem usb6fire_pcm_chmaps[] = {
	{ .channels = 1, .map = { SNDRV_CHMAP_MONO } },
	{ .channels = 2, .map = { SNDRV_CHMAP_FL, SNDRV_CHMAP_FR } },
	{ .channels = 3, .map = { SNDRV_CHMAP_FL, SNDRV_CHMAP_FR,
				  SNDRV_CHMAP_FC } },
	{ .channels = 4, .map = { SNDRV_CHMAP_FL, SNDRV_CHMAP_FR,
				  SNDRV_CHMAP_FC, SNDRV_CHMAP_LFE } },
	{ .channels = 5, .map = { SNDRV_CHMAP_FL, SNDRV_CHMAP_FR,
				  SNDRV_CHMAP_FC, SNDRV_CHMAP_LFE,
				  SNDRV_CHMAP_RL } },
	{ .channels = 6, .map = { SNDRV_CHMAP_FL, SNDRV_CHMAP_FR,
				  SNDRV_CHMAP_FC, SNDRV_CHMAP_LFE,
				  SNDRV_CHMAP_RL, SNDRV_CHMAP_RR } },
	{ .channels = 7, .map = { SNDRV_CHMAP_FL, SNDRV_CHMAP_FR,
				  SNDRV_CHMAP_FC, SNDRV_CHMAP_LFE,
				  SNDRV_CHMAP_RL, SNDRV_CHMAP_RR,
				  SNDRV_CHMAP_UNKNOWN } },
	{ .channels = 8, .map = { SNDRV_CHMAP_FL, SNDRV_CHMAP_FR,
				  SNDRV_CHMAP_FC, SNDRV_CHMAP_LFE,
				  SNDRV_CHMAP_RL, SNDRV_CHMAP_RR,
				  SNDRV_CHMAP_UNKNOWN, SNDRV_CHMAP_UNKNOWN } },
	{ }
};

static const struct snd_kcontrol_new usb6fire_pcm_spdif_controls[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name = SNDRV_CTL_NAME_IEC958("", PLAYBACK, DEFAULT),
		.info = usb6fire_pcm_spdif_info,
		.get = usb6fire_pcm_spdif_default_get,
		.put = usb6fire_pcm_spdif_default_put,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name = SNDRV_CTL_NAME_IEC958("", PLAYBACK, CON_MASK),
		.access = SNDRV_CTL_ELEM_ACCESS_READ,
		.info = usb6fire_pcm_spdif_info,
		.get = usb6fire_pcm_spdif_mask_get,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = SNDRV_CTL_NAME_IEC958("", PLAYBACK, SWITCH),
		.info = snd_ctl_boolean_mono_info,
		.get = usb6fire_pcm_spdif_switch_get,
		.put = usb6fire_pcm_spdif_switch_put,
	},
};

int usb6fire_pcm_init(struct sfire_chip *chip)
{
	int i;
	int ret;
	struct snd_pcm *pcm;
	struct pcm_runtime *rt =
			kzalloc_obj(struct pcm_runtime);

	if (!rt)
		return -ENOMEM;

	ret = usb6fire_pcm_buffers_init(rt);
	if (ret)
		goto error;

	rt->chip = chip;
	rt->stream_state = STREAM_DISABLED;
	rt->rate = ARRAY_SIZE(rates);
	rt->spdif_switch = true;
	rt->spdif_bits = IEC958_AES0_CON_NOT_COPYRIGHT |
			IEC958_AES1_CON_GENERAL << 8;
	init_waitqueue_head(&rt->stream_wait_queue);
	mutex_init(&rt->stream_mutex);

	spin_lock_init(&rt->playback.lock);
	spin_lock_init(&rt->capture.lock);

	for (i = 0; i < PCM_N_URBS; i++) {
		ret = usb6fire_pcm_init_urb(&rt->in_urbs[i], chip, true, IN_EP,
					    usb6fire_pcm_in_urb_handler);
		if (ret < 0)
			goto error;
		ret = usb6fire_pcm_init_urb(&rt->out_urbs[i], chip, false, OUT_EP,
					    usb6fire_pcm_out_urb_handler);
		if (ret < 0)
			goto error;

		rt->in_urbs[i].peer = &rt->out_urbs[i];
		rt->out_urbs[i].peer = &rt->in_urbs[i];
	}

	ret = snd_pcm_new(chip->card, "DMX6FireUSB", 0, 1, 1, &pcm);
	if (ret < 0) {
		dev_err(&chip->dev->dev, "cannot create pcm instance.\n");
		goto error;
	}

	pcm->private_data = rt;
	strscpy(pcm->name, "DMX 6Fire USB");
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &pcm_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &pcm_ops);
	snd_pcm_set_managed_buffer_all(pcm, SNDRV_DMA_TYPE_VMALLOC, NULL, 0, 0);

	rt->instance = pcm;

	chip->pcm = rt;

	/* on error, rt and the controls added so far are freed with the card */
	for (i = 0; i < ARRAY_SIZE(usb6fire_pcm_spdif_controls); i++) {
		ret = snd_ctl_add(chip->card,
				snd_ctl_new1(&usb6fire_pcm_spdif_controls[i], rt));
		if (ret < 0) {
			dev_err(&chip->dev->dev, "cannot add S/PDIF control.\n");
			return ret;
		}
	}
	ret = snd_pcm_add_chmap_ctls(pcm, SNDRV_PCM_STREAM_PLAYBACK,
			usb6fire_pcm_chmaps, OUT_N_CHANNELS, 0, NULL);
	if (ret < 0) {
		dev_err(&chip->dev->dev, "cannot add channel map.\n");
		return ret;
	}
	return 0;

 error:
	usb6fire_pcm_free(rt);
	return ret;
}

void usb6fire_pcm_abort(struct sfire_chip *chip)
{
	struct pcm_runtime *rt = chip->pcm;
	int i;

	if (rt) {
		rt->panic = true;

		if (rt->playback.instance)
			snd_pcm_stop_xrun(rt->playback.instance);

		if (rt->capture.instance)
			snd_pcm_stop_xrun(rt->capture.instance);

		for (i = 0; i < PCM_N_URBS; i++) {
			usb_poison_urb(rt->in_urbs[i].instance);
			usb_poison_urb(rt->out_urbs[i].instance);
		}

	}
}

void usb6fire_pcm_destroy(struct sfire_chip *chip)
{
	usb6fire_pcm_free(chip->pcm);
}
