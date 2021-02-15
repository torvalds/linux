// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (c) 2006,2007 Daniel Mack
*/

#include <linux/device.h>
#include <linux/usb.h>
#include <linux/gfp.h>
#include <sound/rawmidi.h>
#include <sound/core.h>
#include <sound/pcm.h>

#include "device.h"
#include "midi.h"

static int snd_usb_caiaq_midi_input_open(struct snd_rawmidi_substream *substream)
{
	return 0;
}

static int snd_usb_caiaq_midi_input_close(struct snd_rawmidi_substream *substream)
{
	return 0;
}

static void snd_usb_caiaq_midi_input_trigger(struct snd_rawmidi_substream *substream, int up)
{
	struct snd_usb_caiaqdev *cdev = substream->rmidi->private_data;

	if (!cdev)
		return;

	cdev->midi_receive_substream = up ? substream : NULL;
}


static int snd_usb_caiaq_midi_output_open(struct snd_rawmidi_substream *substream)
{
	return 0;
}

static int snd_usb_caiaq_midi_output_close(struct snd_rawmidi_substream *substream)
{
	struct snd_usb_caiaqdev *cdev = substream->rmidi->private_data;
	if (cdev->midi_out_active) {
		usb_kill_urb(&cdev->midi_out_urb);
		cdev->midi_out_active = 0;
	}
	return 0;
}

static void snd_usb_caiaq_midi_send(struct snd_usb_caiaqdev *cdev,
				    struct snd_rawmidi_substream *substream)
{
	int len, ret;
	struct device *dev = caiaqdev_to_dev(cdev);

	cdev->midi_out_buf[0] = EP1_CMD_MIDI_WRITE;
	cdev->midi_out_buf[1] = 0; /* port */
	len = snd_rawmidi_transmit(substream, cdev->midi_out_buf + 3,
				   EP1_BUFSIZE - 3);

	if (len <= 0)
		return;

	cdev->midi_out_buf[2] = len;
	cdev->midi_out_urb.transfer_buffer_length = len+3;

	ret = usb_submit_urb(&cdev->midi_out_urb, GFP_ATOMIC);
	if (ret < 0)
		dev_err(dev,
			"snd_usb_caiaq_midi_send(%p): usb_submit_urb() failed,"
			"ret=%d, len=%d\n", substream, ret, len);
	else
		cdev->midi_out_active = 1;
}

static void snd_usb_caiaq_midi_output_trigger(struct snd_rawmidi_substream *substream, int up)
{
	struct snd_usb_caiaqdev *cdev = substream->rmidi->private_data;

	if (up) {
		cdev->midi_out_substream = substream;
		if (!cdev->midi_out_active)
			snd_usb_caiaq_midi_send(cdev, substream);
	} else {
		cdev->midi_out_substream = NULL;
	}
}


static const struct snd_rawmidi_ops snd_usb_caiaq_midi_output =
{
	.open =		snd_usb_caiaq_midi_output_open,
	.close =	snd_usb_caiaq_midi_output_close,
	.trigger =      snd_usb_caiaq_midi_output_trigger,
};

static const struct snd_rawmidi_ops snd_usb_caiaq_midi_input =
{
	.open =		snd_usb_caiaq_midi_input_open,
	.close =	snd_usb_caiaq_midi_input_close,
	.trigger =      snd_usb_caiaq_midi_input_trigger,
};

void snd_usb_caiaq_midi_handle_input(struct snd_usb_caiaqdev *cdev,
				     int port, const char *buf, int len)
{
	if (!cdev->midi_receive_substream)
		return;

	snd_rawmidi_receive(cdev->midi_receive_substream, buf, len);
}

int snd_usb_caiaq_midi_init(struct snd_usb_caiaqdev *device)
{
	int ret;
	struct snd_rawmidi *rmidi;

	ret = snd_rawmidi_new(device->chip.card, device->product_name, 0,
					device->spec.num_midi_out,
					device->spec.num_midi_in,
					&rmidi);

	if (ret < 0)
		return ret;

	strscpy(rmidi->name, device->product_name, sizeof(rmidi->name));

	rmidi->info_flags = SNDRV_RAWMIDI_INFO_DUPLEX;
	rmidi->private_data = device;

	if (device->spec.num_midi_out > 0) {
		rmidi->info_flags |= SNDRV_RAWMIDI_INFO_OUTPUT;
		snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_OUTPUT,
				    &snd_usb_caiaq_midi_output);
	}

	if (device->spec.num_midi_in > 0) {
		rmidi->info_flags |= SNDRV_RAWMIDI_INFO_INPUT;
		snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_INPUT,
				    &snd_usb_caiaq_midi_input);
	}

	device->rmidi = rmidi;

	return 0;
}

void snd_usb_caiaq_midi_output_done(struct urb* urb)
{
	struct snd_usb_caiaqdev *cdev = urb->context;

	cdev->midi_out_active = 0;
	if (urb->status != 0)
		return;

	if (!cdev->midi_out_substream)
		return;

	snd_usb_caiaq_midi_send(cdev, cdev->midi_out_substream);
}
