// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Linux driver for M2Tech hiFace compatible devices
 *
 * Copyright 2012-2013 (C) M2TECH S.r.l and Amarula Solutions B.V.
 *
 * Authors:  Michael Trimarchi <michael@amarulasolutions.com>
 *           Antonio Ospite <ao2@amarulasolutions.com>
 *
 * The driver is based on the work done in TerraTec DMX 6Fire USB
 */

#include <linux/slab.h>
#include <sound/pcm.h>

#include "pcm.h"
#include "chip.h"

#define OUT_EP          0x2
#define PCM_N_URBS      8
#define PCM_PACKET_SIZE 4096
#define PCM_BUFFER_SIZE (2 * PCM_N_URBS * PCM_PACKET_SIZE)

struct pcm_urb {
	struct hiface_chip *chip;

	struct urb instance;
	struct usb_anchor submitted;
	u8 *buffer;
};

struct pcm_substream {
	spinlock_t lock;
	struct snd_pcm_substream *instance;

	bool active;
	snd_pcm_uframes_t dma_off;    /* current position in alsa dma_area */
	snd_pcm_uframes_t period_off; /* current position in current period */
};

enum { /* pcm streaming states */
	STREAM_DISABLED, /* no pcm streaming */
	STREAM_STARTING, /* pcm streaming requested, waiting to become ready */
	STREAM_RUNNING,  /* pcm streaming running */
	STREAM_STOPPING
};

struct pcm_runtime {
	struct hiface_chip *chip;
	struct snd_pcm *instance;

	struct pcm_substream playback;
	bool panic; /* if set driver won't do anymore pcm on device */

	struct pcm_urb out_urbs[PCM_N_URBS];

	struct mutex stream_mutex;
	u8 stream_state; /* one of STREAM_XXX */
	u8 extra_freq;
	wait_queue_head_t stream_wait_queue;
	bool stream_wait_cond;
};

static const unsigned int rates[] = { 44100, 48000, 88200, 96000, 176400, 192000,
				      352800, 384000 };
static const struct snd_pcm_hw_constraint_list constraints_extra_rates = {
	.count = ARRAY_SIZE(rates),
	.list = rates,
	.mask = 0,
};

static const struct snd_pcm_hardware pcm_hw = {
	.info = SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_PAUSE |
		SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_BATCH,

	.formats = SNDRV_PCM_FMTBIT_S32_LE,

	.rates = SNDRV_PCM_RATE_44100 |
		SNDRV_PCM_RATE_48000 |
		SNDRV_PCM_RATE_88200 |
		SNDRV_PCM_RATE_96000 |
		SNDRV_PCM_RATE_176400 |
		SNDRV_PCM_RATE_192000,

	.rate_min = 44100,
	.rate_max = 192000, /* changes in hiface_pcm_open to support extra rates */
	.channels_min = 2,
	.channels_max = 2,
	.buffer_bytes_max = PCM_BUFFER_SIZE,
	.period_bytes_min = PCM_PACKET_SIZE,
	.period_bytes_max = PCM_BUFFER_SIZE,
	.periods_min = 2,
	.periods_max = 1024
};

/* message values used to change the sample rate */
#define HIFACE_SET_RATE_REQUEST 0xb0

#define HIFACE_RATE_44100  0x43
#define HIFACE_RATE_48000  0x4b
#define HIFACE_RATE_88200  0x42
#define HIFACE_RATE_96000  0x4a
#define HIFACE_RATE_176400 0x40
#define HIFACE_RATE_192000 0x48
#define HIFACE_RATE_352800 0x58
#define HIFACE_RATE_384000 0x68

static int hiface_pcm_set_rate(struct pcm_runtime *rt, unsigned int rate)
{
	struct usb_device *device = rt->chip->dev;
	u16 rate_value;
	int ret;

	/* We are already sure that the rate is supported here thanks to
	 * ALSA constraints
	 */
	switch (rate) {
	case 44100:
		rate_value = HIFACE_RATE_44100;
		break;
	case 48000:
		rate_value = HIFACE_RATE_48000;
		break;
	case 88200:
		rate_value = HIFACE_RATE_88200;
		break;
	case 96000:
		rate_value = HIFACE_RATE_96000;
		break;
	case 176400:
		rate_value = HIFACE_RATE_176400;
		break;
	case 192000:
		rate_value = HIFACE_RATE_192000;
		break;
	case 352800:
		rate_value = HIFACE_RATE_352800;
		break;
	case 384000:
		rate_value = HIFACE_RATE_384000;
		break;
	default:
		dev_err(&device->dev, "Unsupported rate %d\n", rate);
		return -EINVAL;
	}

	/*
	 * USBIO: Vendor 0xb0(wValue=0x0043, wIndex=0x0000)
	 * 43 b0 43 00 00 00 00 00
	 * USBIO: Vendor 0xb0(wValue=0x004b, wIndex=0x0000)
	 * 43 b0 4b 00 00 00 00 00
	 * This control message doesn't have any ack from the
	 * other side
	 */
	ret = usb_control_msg(device, usb_sndctrlpipe(device, 0),
			      HIFACE_SET_RATE_REQUEST,
			      USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_OTHER,
			      rate_value, 0, NULL, 0, 100);
	if (ret < 0) {
		dev_err(&device->dev, "Error setting samplerate %d.\n", rate);
		return ret;
	}

	return 0;
}

static struct pcm_substream *hiface_pcm_get_substream(struct snd_pcm_substream
						      *alsa_sub)
{
	struct pcm_runtime *rt = snd_pcm_substream_chip(alsa_sub);
	struct device *device = &rt->chip->dev->dev;

	if (alsa_sub->stream == SNDRV_PCM_STREAM_PLAYBACK)
		return &rt->playback;

	dev_err(device, "Error getting pcm substream slot.\n");
	return NULL;
}

/* call with stream_mutex locked */
static void hiface_pcm_stream_stop(struct pcm_runtime *rt)
{
	int i, time;

	if (rt->stream_state != STREAM_DISABLED) {
		rt->stream_state = STREAM_STOPPING;

		for (i = 0; i < PCM_N_URBS; i++) {
			time = usb_wait_anchor_empty_timeout(
					&rt->out_urbs[i].submitted, 100);
			if (!time)
				usb_kill_anchored_urbs(
					&rt->out_urbs[i].submitted);
			usb_kill_urb(&rt->out_urbs[i].instance);
		}

		rt->stream_state = STREAM_DISABLED;
	}
}

/* call with stream_mutex locked */
static int hiface_pcm_stream_start(struct pcm_runtime *rt)
{
	int ret = 0;
	int i;

	if (rt->stream_state == STREAM_DISABLED) {

		/* reset panic state when starting a new stream */
		rt->panic = false;

		/* submit our out urbs zero init */
		rt->stream_state = STREAM_STARTING;
		for (i = 0; i < PCM_N_URBS; i++) {
			memset(rt->out_urbs[i].buffer, 0, PCM_PACKET_SIZE);
			usb_anchor_urb(&rt->out_urbs[i].instance,
				       &rt->out_urbs[i].submitted);
			ret = usb_submit_urb(&rt->out_urbs[i].instance,
					     GFP_ATOMIC);
			if (ret) {
				hiface_pcm_stream_stop(rt);
				return ret;
			}
		}

		/* wait for first out urb to return (sent in in urb handler) */
		wait_event_timeout(rt->stream_wait_queue, rt->stream_wait_cond,
				   HZ);
		if (rt->stream_wait_cond) {
			struct device *device = &rt->chip->dev->dev;
			dev_dbg(device, "%s: Stream is running wakeup event\n",
				 __func__);
			rt->stream_state = STREAM_RUNNING;
		} else {
			hiface_pcm_stream_stop(rt);
			return -EIO;
		}
	}
	return ret;
}

/* The hardware wants word-swapped 32-bit values */
static void memcpy_swahw32(u8 *dest, u8 *src, unsigned int n)
{
	unsigned int i;

	for (i = 0; i < n / 4; i++)
		((u32 *)dest)[i] = swahw32(((u32 *)src)[i]);
}

/* call with substream locked */
/* returns true if a period elapsed */
static bool hiface_pcm_playback(struct pcm_substream *sub, struct pcm_urb *urb)
{
	struct snd_pcm_runtime *alsa_rt = sub->instance->runtime;
	struct device *device = &urb->chip->dev->dev;
	u8 *source;
	unsigned int pcm_buffer_size;

	WARN_ON(alsa_rt->format != SNDRV_PCM_FORMAT_S32_LE);

	pcm_buffer_size = snd_pcm_lib_buffer_bytes(sub->instance);

	if (sub->dma_off + PCM_PACKET_SIZE <= pcm_buffer_size) {
		dev_dbg(device, "%s: (1) buffer_size %#x dma_offset %#x\n", __func__,
			 (unsigned int) pcm_buffer_size,
			 (unsigned int) sub->dma_off);

		source = alsa_rt->dma_area + sub->dma_off;
		memcpy_swahw32(urb->buffer, source, PCM_PACKET_SIZE);
	} else {
		/* wrap around at end of ring buffer */
		unsigned int len;

		dev_dbg(device, "%s: (2) buffer_size %#x dma_offset %#x\n", __func__,
			 (unsigned int) pcm_buffer_size,
			 (unsigned int) sub->dma_off);

		len = pcm_buffer_size - sub->dma_off;

		source = alsa_rt->dma_area + sub->dma_off;
		memcpy_swahw32(urb->buffer, source, len);

		source = alsa_rt->dma_area;
		memcpy_swahw32(urb->buffer + len, source,
			       PCM_PACKET_SIZE - len);
	}
	sub->dma_off += PCM_PACKET_SIZE;
	if (sub->dma_off >= pcm_buffer_size)
		sub->dma_off -= pcm_buffer_size;

	sub->period_off += PCM_PACKET_SIZE;
	if (sub->period_off >= alsa_rt->period_size) {
		sub->period_off %= alsa_rt->period_size;
		return true;
	}
	return false;
}

static void hiface_pcm_out_urb_handler(struct urb *usb_urb)
{
	struct pcm_urb *out_urb = usb_urb->context;
	struct pcm_runtime *rt = out_urb->chip->pcm;
	struct pcm_substream *sub;
	bool do_period_elapsed = false;
	unsigned long flags;
	int ret;

	if (rt->panic || rt->stream_state == STREAM_STOPPING)
		return;

	if (unlikely(usb_urb->status == -ENOENT ||	/* unlinked */
		     usb_urb->status == -ENODEV ||	/* device removed */
		     usb_urb->status == -ECONNRESET ||	/* unlinked */
		     usb_urb->status == -ESHUTDOWN)) {	/* device disabled */
		goto out_fail;
	}

	if (rt->stream_state == STREAM_STARTING) {
		rt->stream_wait_cond = true;
		wake_up(&rt->stream_wait_queue);
	}

	/* now send our playback data (if a free out urb was found) */
	sub = &rt->playback;
	spin_lock_irqsave(&sub->lock, flags);
	if (sub->active)
		do_period_elapsed = hiface_pcm_playback(sub, out_urb);
	else
		memset(out_urb->buffer, 0, PCM_PACKET_SIZE);

	spin_unlock_irqrestore(&sub->lock, flags);

	if (do_period_elapsed)
		snd_pcm_period_elapsed(sub->instance);

	ret = usb_submit_urb(&out_urb->instance, GFP_ATOMIC);
	if (ret < 0)
		goto out_fail;

	return;

out_fail:
	rt->panic = true;
}

static int hiface_pcm_open(struct snd_pcm_substream *alsa_sub)
{
	struct pcm_runtime *rt = snd_pcm_substream_chip(alsa_sub);
	struct pcm_substream *sub = NULL;
	struct snd_pcm_runtime *alsa_rt = alsa_sub->runtime;
	int ret;

	if (rt->panic)
		return -EPIPE;

	mutex_lock(&rt->stream_mutex);
	alsa_rt->hw = pcm_hw;

	if (alsa_sub->stream == SNDRV_PCM_STREAM_PLAYBACK)
		sub = &rt->playback;

	if (!sub) {
		struct device *device = &rt->chip->dev->dev;
		mutex_unlock(&rt->stream_mutex);
		dev_err(device, "Invalid stream type\n");
		return -EINVAL;
	}

	if (rt->extra_freq) {
		alsa_rt->hw.rates |= SNDRV_PCM_RATE_KNOT;
		alsa_rt->hw.rate_max = 384000;

		/* explicit constraints needed as we added SNDRV_PCM_RATE_KNOT */
		ret = snd_pcm_hw_constraint_list(alsa_sub->runtime, 0,
						 SNDRV_PCM_HW_PARAM_RATE,
						 &constraints_extra_rates);
		if (ret < 0) {
			mutex_unlock(&rt->stream_mutex);
			return ret;
		}
	}

	sub->instance = alsa_sub;
	sub->active = false;
	mutex_unlock(&rt->stream_mutex);
	return 0;
}

static int hiface_pcm_close(struct snd_pcm_substream *alsa_sub)
{
	struct pcm_runtime *rt = snd_pcm_substream_chip(alsa_sub);
	struct pcm_substream *sub = hiface_pcm_get_substream(alsa_sub);
	unsigned long flags;

	if (rt->panic)
		return 0;

	mutex_lock(&rt->stream_mutex);
	if (sub) {
		hiface_pcm_stream_stop(rt);

		/* deactivate substream */
		spin_lock_irqsave(&sub->lock, flags);
		sub->instance = NULL;
		sub->active = false;
		spin_unlock_irqrestore(&sub->lock, flags);

	}
	mutex_unlock(&rt->stream_mutex);
	return 0;
}

static int hiface_pcm_hw_params(struct snd_pcm_substream *alsa_sub,
				struct snd_pcm_hw_params *hw_params)
{
	return snd_pcm_lib_malloc_pages(alsa_sub,
					params_buffer_bytes(hw_params));
}

static int hiface_pcm_hw_free(struct snd_pcm_substream *alsa_sub)
{
	return snd_pcm_lib_free_pages(alsa_sub);
}

static int hiface_pcm_prepare(struct snd_pcm_substream *alsa_sub)
{
	struct pcm_runtime *rt = snd_pcm_substream_chip(alsa_sub);
	struct pcm_substream *sub = hiface_pcm_get_substream(alsa_sub);
	struct snd_pcm_runtime *alsa_rt = alsa_sub->runtime;
	int ret;

	if (rt->panic)
		return -EPIPE;
	if (!sub)
		return -ENODEV;

	mutex_lock(&rt->stream_mutex);

	hiface_pcm_stream_stop(rt);

	sub->dma_off = 0;
	sub->period_off = 0;

	if (rt->stream_state == STREAM_DISABLED) {

		ret = hiface_pcm_set_rate(rt, alsa_rt->rate);
		if (ret) {
			mutex_unlock(&rt->stream_mutex);
			return ret;
		}
		ret = hiface_pcm_stream_start(rt);
		if (ret) {
			mutex_unlock(&rt->stream_mutex);
			return ret;
		}
	}
	mutex_unlock(&rt->stream_mutex);
	return 0;
}

static int hiface_pcm_trigger(struct snd_pcm_substream *alsa_sub, int cmd)
{
	struct pcm_substream *sub = hiface_pcm_get_substream(alsa_sub);
	struct pcm_runtime *rt = snd_pcm_substream_chip(alsa_sub);

	if (rt->panic)
		return -EPIPE;
	if (!sub)
		return -ENODEV;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		spin_lock_irq(&sub->lock);
		sub->active = true;
		spin_unlock_irq(&sub->lock);
		return 0;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		spin_lock_irq(&sub->lock);
		sub->active = false;
		spin_unlock_irq(&sub->lock);
		return 0;

	default:
		return -EINVAL;
	}
}

static snd_pcm_uframes_t hiface_pcm_pointer(struct snd_pcm_substream *alsa_sub)
{
	struct pcm_substream *sub = hiface_pcm_get_substream(alsa_sub);
	struct pcm_runtime *rt = snd_pcm_substream_chip(alsa_sub);
	unsigned long flags;
	snd_pcm_uframes_t dma_offset;

	if (rt->panic || !sub)
		return SNDRV_PCM_POS_XRUN;

	spin_lock_irqsave(&sub->lock, flags);
	dma_offset = sub->dma_off;
	spin_unlock_irqrestore(&sub->lock, flags);
	return bytes_to_frames(alsa_sub->runtime, dma_offset);
}

static const struct snd_pcm_ops pcm_ops = {
	.open = hiface_pcm_open,
	.close = hiface_pcm_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = hiface_pcm_hw_params,
	.hw_free = hiface_pcm_hw_free,
	.prepare = hiface_pcm_prepare,
	.trigger = hiface_pcm_trigger,
	.pointer = hiface_pcm_pointer,
};

static int hiface_pcm_init_urb(struct pcm_urb *urb,
			       struct hiface_chip *chip,
			       unsigned int ep,
			       void (*handler)(struct urb *))
{
	urb->chip = chip;
	usb_init_urb(&urb->instance);

	urb->buffer = kzalloc(PCM_PACKET_SIZE, GFP_KERNEL);
	if (!urb->buffer)
		return -ENOMEM;

	usb_fill_bulk_urb(&urb->instance, chip->dev,
			  usb_sndbulkpipe(chip->dev, ep), (void *)urb->buffer,
			  PCM_PACKET_SIZE, handler, urb);
	if (usb_urb_ep_type_check(&urb->instance))
		return -EINVAL;
	init_usb_anchor(&urb->submitted);

	return 0;
}

void hiface_pcm_abort(struct hiface_chip *chip)
{
	struct pcm_runtime *rt = chip->pcm;

	if (rt) {
		rt->panic = true;

		mutex_lock(&rt->stream_mutex);
		hiface_pcm_stream_stop(rt);
		mutex_unlock(&rt->stream_mutex);
	}
}

static void hiface_pcm_destroy(struct hiface_chip *chip)
{
	struct pcm_runtime *rt = chip->pcm;
	int i;

	for (i = 0; i < PCM_N_URBS; i++)
		kfree(rt->out_urbs[i].buffer);

	kfree(chip->pcm);
	chip->pcm = NULL;
}

static void hiface_pcm_free(struct snd_pcm *pcm)
{
	struct pcm_runtime *rt = pcm->private_data;

	if (rt)
		hiface_pcm_destroy(rt->chip);
}

int hiface_pcm_init(struct hiface_chip *chip, u8 extra_freq)
{
	int i;
	int ret;
	struct snd_pcm *pcm;
	struct pcm_runtime *rt;

	rt = kzalloc(sizeof(*rt), GFP_KERNEL);
	if (!rt)
		return -ENOMEM;

	rt->chip = chip;
	rt->stream_state = STREAM_DISABLED;
	if (extra_freq)
		rt->extra_freq = 1;

	init_waitqueue_head(&rt->stream_wait_queue);
	mutex_init(&rt->stream_mutex);
	spin_lock_init(&rt->playback.lock);

	for (i = 0; i < PCM_N_URBS; i++) {
		ret = hiface_pcm_init_urb(&rt->out_urbs[i], chip, OUT_EP,
				    hiface_pcm_out_urb_handler);
		if (ret < 0)
			goto error;
	}

	ret = snd_pcm_new(chip->card, "USB-SPDIF Audio", 0, 1, 0, &pcm);
	if (ret < 0) {
		dev_err(&chip->dev->dev, "Cannot create pcm instance\n");
		goto error;
	}

	pcm->private_data = rt;
	pcm->private_free = hiface_pcm_free;

	strlcpy(pcm->name, "USB-SPDIF Audio", sizeof(pcm->name));
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &pcm_ops);
	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_VMALLOC,
					      NULL, 0, 0);

	rt->instance = pcm;

	chip->pcm = rt;
	return 0;

error:
	for (i = 0; i < PCM_N_URBS; i++)
		kfree(rt->out_urbs[i].buffer);
	kfree(rt);
	return ret;
}
