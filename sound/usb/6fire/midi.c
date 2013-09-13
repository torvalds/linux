/*
 * Linux driver for TerraTec DMX 6Fire USB
 *
 * Rawmidi driver
 *
 * Author:	Torsten Schenk <torsten.schenk@zoho.com>
 * Created:	Jan 01, 2011
 * Copyright:	(C) Torsten Schenk
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <sound/rawmidi.h>

#include "midi.h"
#include "chip.h"
#include "comm.h"

enum {
	MIDI_BUFSIZE = 64
};

static void usb6fire_midi_out_handler(struct urb *urb)
{
	struct midi_runtime *rt = urb->context;
	int ret;
	unsigned long flags;

	spin_lock_irqsave(&rt->out_lock, flags);

	if (rt->out) {
		ret = snd_rawmidi_transmit(rt->out, rt->out_buffer + 4,
				MIDI_BUFSIZE - 4);
		if (ret > 0) { /* more data available, send next packet */
			rt->out_buffer[1] = ret + 2;
			rt->out_buffer[3] = rt->out_serial++;
			urb->transfer_buffer_length = ret + 4;

			ret = usb_submit_urb(urb, GFP_ATOMIC);
			if (ret < 0)
				snd_printk(KERN_ERR PREFIX "midi out urb "
						"submit failed: %d\n", ret);
		} else /* no more data to transmit */
			rt->out = NULL;
	}
	spin_unlock_irqrestore(&rt->out_lock, flags);
}

static void usb6fire_midi_in_received(
		struct midi_runtime *rt, u8 *data, int length)
{
	unsigned long flags;

	spin_lock_irqsave(&rt->in_lock, flags);
	if (rt->in)
		snd_rawmidi_receive(rt->in, data, length);
	spin_unlock_irqrestore(&rt->in_lock, flags);
}

static int usb6fire_midi_out_open(struct snd_rawmidi_substream *alsa_sub)
{
	return 0;
}

static int usb6fire_midi_out_close(struct snd_rawmidi_substream *alsa_sub)
{
	return 0;
}

static void usb6fire_midi_out_trigger(
		struct snd_rawmidi_substream *alsa_sub, int up)
{
	struct midi_runtime *rt = alsa_sub->rmidi->private_data;
	struct urb *urb = &rt->out_urb;
	__s8 ret;
	unsigned long flags;

	spin_lock_irqsave(&rt->out_lock, flags);
	if (up) { /* start transfer */
		if (rt->out) { /* we are already transmitting so just return */
			spin_unlock_irqrestore(&rt->out_lock, flags);
			return;
		}

		ret = snd_rawmidi_transmit(alsa_sub, rt->out_buffer + 4,
				MIDI_BUFSIZE - 4);
		if (ret > 0) {
			rt->out_buffer[1] = ret + 2;
			rt->out_buffer[3] = rt->out_serial++;
			urb->transfer_buffer_length = ret + 4;

			ret = usb_submit_urb(urb, GFP_ATOMIC);
			if (ret < 0)
				snd_printk(KERN_ERR PREFIX "midi out urb "
						"submit failed: %d\n", ret);
			else
				rt->out = alsa_sub;
		}
	} else if (rt->out == alsa_sub)
		rt->out = NULL;
	spin_unlock_irqrestore(&rt->out_lock, flags);
}

static void usb6fire_midi_out_drain(struct snd_rawmidi_substream *alsa_sub)
{
	struct midi_runtime *rt = alsa_sub->rmidi->private_data;
	int retry = 0;

	while (rt->out && retry++ < 100)
		msleep(10);
}

static int usb6fire_midi_in_open(struct snd_rawmidi_substream *alsa_sub)
{
	return 0;
}

static int usb6fire_midi_in_close(struct snd_rawmidi_substream *alsa_sub)
{
	return 0;
}

static void usb6fire_midi_in_trigger(
		struct snd_rawmidi_substream *alsa_sub, int up)
{
	struct midi_runtime *rt = alsa_sub->rmidi->private_data;
	unsigned long flags;

	spin_lock_irqsave(&rt->in_lock, flags);
	if (up)
		rt->in = alsa_sub;
	else
		rt->in = NULL;
	spin_unlock_irqrestore(&rt->in_lock, flags);
}

static struct snd_rawmidi_ops out_ops = {
	.open = usb6fire_midi_out_open,
	.close = usb6fire_midi_out_close,
	.trigger = usb6fire_midi_out_trigger,
	.drain = usb6fire_midi_out_drain
};

static struct snd_rawmidi_ops in_ops = {
	.open = usb6fire_midi_in_open,
	.close = usb6fire_midi_in_close,
	.trigger = usb6fire_midi_in_trigger
};

int usb6fire_midi_init(struct sfire_chip *chip)
{
	int ret;
	struct midi_runtime *rt = kzalloc(sizeof(struct midi_runtime),
			GFP_KERNEL);
	struct comm_runtime *comm_rt = chip->comm;

	if (!rt)
		return -ENOMEM;

	rt->out_buffer = kzalloc(MIDI_BUFSIZE, GFP_KERNEL);
	if (!rt->out_buffer) {
		kfree(rt);
		return -ENOMEM;
	}

	rt->chip = chip;
	rt->in_received = usb6fire_midi_in_received;
	rt->out_buffer[0] = 0x80; /* 'send midi' command */
	rt->out_buffer[1] = 0x00; /* size of data */
	rt->out_buffer[2] = 0x00; /* always 0 */
	spin_lock_init(&rt->in_lock);
	spin_lock_init(&rt->out_lock);

	comm_rt->init_urb(comm_rt, &rt->out_urb, rt->out_buffer, rt,
			usb6fire_midi_out_handler);

	ret = snd_rawmidi_new(chip->card, "6FireUSB", 0, 1, 1, &rt->instance);
	if (ret < 0) {
		kfree(rt->out_buffer);
		kfree(rt);
		snd_printk(KERN_ERR PREFIX "unable to create midi.\n");
		return ret;
	}
	rt->instance->private_data = rt;
	strcpy(rt->instance->name, "DMX6FireUSB MIDI");
	rt->instance->info_flags = SNDRV_RAWMIDI_INFO_OUTPUT |
			SNDRV_RAWMIDI_INFO_INPUT |
			SNDRV_RAWMIDI_INFO_DUPLEX;
	snd_rawmidi_set_ops(rt->instance, SNDRV_RAWMIDI_STREAM_OUTPUT,
			&out_ops);
	snd_rawmidi_set_ops(rt->instance, SNDRV_RAWMIDI_STREAM_INPUT,
			&in_ops);

	chip->midi = rt;
	return 0;
}

void usb6fire_midi_abort(struct sfire_chip *chip)
{
	struct midi_runtime *rt = chip->midi;

	if (rt)
		usb_poison_urb(&rt->out_urb);
}

void usb6fire_midi_destroy(struct sfire_chip *chip)
{
	struct midi_runtime *rt = chip->midi;

	kfree(rt->out_buffer);
	kfree(rt);
	chip->midi = NULL;
}
