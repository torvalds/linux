// SPDX-License-Identifier: GPL-2.0-only
/*
 * Line 6 Linux USB driver
 *
 * Copyright (C) 2004-2010 Markus Grabner (grabner@icg.tugraz.at)
 */

#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/export.h>
#include <sound/core.h>
#include <sound/rawmidi.h>

#include "driver.h"
#include "midi.h"

#define line6_rawmidi_substream_midi(substream) \
	((struct snd_line6_midi *)((substream)->rmidi->private_data))

static int send_midi_async(struct usb_line6 *line6, unsigned char *data,
			   int length);

/*
	Pass data received via USB to MIDI.
*/
void line6_midi_receive(struct usb_line6 *line6, unsigned char *data,
			int length)
{
	if (line6->line6midi->substream_receive)
		snd_rawmidi_receive(line6->line6midi->substream_receive,
				    data, length);
}

/*
	Read data from MIDI buffer and transmit them via USB.
*/
static void line6_midi_transmit(struct snd_rawmidi_substream *substream)
{
	struct usb_line6 *line6 =
	    line6_rawmidi_substream_midi(substream)->line6;
	struct snd_line6_midi *line6midi = line6->line6midi;
	struct midi_buffer *mb = &line6midi->midibuf_out;
	unsigned char chunk[LINE6_FALLBACK_MAXPACKETSIZE];
	int req, done;

	for (;;) {
		req = min3(line6_midibuf_bytes_free(mb), line6->max_packet_size,
			   LINE6_FALLBACK_MAXPACKETSIZE);
		done = snd_rawmidi_transmit_peek(substream, chunk, req);

		if (done == 0)
			break;

		line6_midibuf_write(mb, chunk, done);
		snd_rawmidi_transmit_ack(substream, done);
	}

	for (;;) {
		done = line6_midibuf_read(mb, chunk,
					  LINE6_FALLBACK_MAXPACKETSIZE,
					  LINE6_MIDIBUF_READ_TX);

		if (done == 0)
			break;

		send_midi_async(line6, chunk, done);
	}
}

/*
	Notification of completion of MIDI transmission.
*/
static void midi_sent(struct urb *urb)
{
	unsigned long flags;
	int status;
	int num;
	struct usb_line6 *line6 = (struct usb_line6 *)urb->context;

	status = urb->status;
	kfree(urb->transfer_buffer);
	usb_free_urb(urb);

	if (status == -ESHUTDOWN)
		return;

	spin_lock_irqsave(&line6->line6midi->lock, flags);
	num = --line6->line6midi->num_active_send_urbs;

	if (num == 0) {
		line6_midi_transmit(line6->line6midi->substream_transmit);
		num = line6->line6midi->num_active_send_urbs;
	}

	if (num == 0)
		wake_up(&line6->line6midi->send_wait);

	spin_unlock_irqrestore(&line6->line6midi->lock, flags);
}

/*
	Send an asynchronous MIDI message.
	Assumes that line6->line6midi->lock is held
	(i.e., this function is serialized).
*/
static int send_midi_async(struct usb_line6 *line6, unsigned char *data,
			   int length)
{
	struct urb *urb;
	int retval;
	unsigned char *transfer_buffer;

	urb = usb_alloc_urb(0, GFP_ATOMIC);

	if (urb == NULL)
		return -ENOMEM;

	transfer_buffer = kmemdup(data, length, GFP_ATOMIC);

	if (transfer_buffer == NULL) {
		usb_free_urb(urb);
		return -ENOMEM;
	}

	usb_fill_int_urb(urb, line6->usbdev,
			 usb_sndintpipe(line6->usbdev,
					 line6->properties->ep_ctrl_w),
			 transfer_buffer, length, midi_sent, line6,
			 line6->interval);
	urb->actual_length = 0;
	retval = usb_urb_ep_type_check(urb);
	if (retval < 0)
		goto error;

	retval = usb_submit_urb(urb, GFP_ATOMIC);
	if (retval < 0)
		goto error;

	++line6->line6midi->num_active_send_urbs;
	return 0;

 error:
	dev_err(line6->ifcdev, "usb_submit_urb failed\n");
	usb_free_urb(urb);
	return retval;
}

static int line6_midi_output_open(struct snd_rawmidi_substream *substream)
{
	return 0;
}

static int line6_midi_output_close(struct snd_rawmidi_substream *substream)
{
	return 0;
}

static void line6_midi_output_trigger(struct snd_rawmidi_substream *substream,
				      int up)
{
	unsigned long flags;
	struct usb_line6 *line6 =
	    line6_rawmidi_substream_midi(substream)->line6;

	line6->line6midi->substream_transmit = substream;
	spin_lock_irqsave(&line6->line6midi->lock, flags);

	if (line6->line6midi->num_active_send_urbs == 0)
		line6_midi_transmit(substream);

	spin_unlock_irqrestore(&line6->line6midi->lock, flags);
}

static void line6_midi_output_drain(struct snd_rawmidi_substream *substream)
{
	struct usb_line6 *line6 =
	    line6_rawmidi_substream_midi(substream)->line6;
	struct snd_line6_midi *midi = line6->line6midi;

	wait_event_interruptible(midi->send_wait,
				 midi->num_active_send_urbs == 0);
}

static int line6_midi_input_open(struct snd_rawmidi_substream *substream)
{
	return 0;
}

static int line6_midi_input_close(struct snd_rawmidi_substream *substream)
{
	return 0;
}

static void line6_midi_input_trigger(struct snd_rawmidi_substream *substream,
				     int up)
{
	struct usb_line6 *line6 =
	    line6_rawmidi_substream_midi(substream)->line6;

	if (up)
		line6->line6midi->substream_receive = substream;
	else
		line6->line6midi->substream_receive = NULL;
}

static const struct snd_rawmidi_ops line6_midi_output_ops = {
	.open = line6_midi_output_open,
	.close = line6_midi_output_close,
	.trigger = line6_midi_output_trigger,
	.drain = line6_midi_output_drain,
};

static const struct snd_rawmidi_ops line6_midi_input_ops = {
	.open = line6_midi_input_open,
	.close = line6_midi_input_close,
	.trigger = line6_midi_input_trigger,
};

/* Create a MIDI device */
static int snd_line6_new_midi(struct usb_line6 *line6,
			      struct snd_rawmidi **rmidi_ret)
{
	struct snd_rawmidi *rmidi;
	int err;

	err = snd_rawmidi_new(line6->card, "Line 6 MIDI", 0, 1, 1, rmidi_ret);
	if (err < 0)
		return err;

	rmidi = *rmidi_ret;
	strcpy(rmidi->id, line6->properties->id);
	strcpy(rmidi->name, line6->properties->name);

	rmidi->info_flags =
	    SNDRV_RAWMIDI_INFO_OUTPUT |
	    SNDRV_RAWMIDI_INFO_INPUT | SNDRV_RAWMIDI_INFO_DUPLEX;

	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_OUTPUT,
			    &line6_midi_output_ops);
	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_INPUT,
			    &line6_midi_input_ops);
	return 0;
}

/* MIDI device destructor */
static void snd_line6_midi_free(struct snd_rawmidi *rmidi)
{
	struct snd_line6_midi *line6midi = rmidi->private_data;

	line6_midibuf_destroy(&line6midi->midibuf_in);
	line6_midibuf_destroy(&line6midi->midibuf_out);
	kfree(line6midi);
}

/*
	Initialize the Line 6 MIDI subsystem.
*/
int line6_init_midi(struct usb_line6 *line6)
{
	int err;
	struct snd_rawmidi *rmidi;
	struct snd_line6_midi *line6midi;

	if (!(line6->properties->capabilities & LINE6_CAP_CONTROL_MIDI)) {
		/* skip MIDI initialization and report success */
		return 0;
	}

	err = snd_line6_new_midi(line6, &rmidi);
	if (err < 0)
		return err;

	line6midi = kzalloc(sizeof(struct snd_line6_midi), GFP_KERNEL);
	if (!line6midi)
		return -ENOMEM;

	rmidi->private_data = line6midi;
	rmidi->private_free = snd_line6_midi_free;

	init_waitqueue_head(&line6midi->send_wait);
	spin_lock_init(&line6midi->lock);
	line6midi->line6 = line6;

	err = line6_midibuf_init(&line6midi->midibuf_in, MIDI_BUFFER_SIZE, 0);
	if (err < 0)
		return err;

	err = line6_midibuf_init(&line6midi->midibuf_out, MIDI_BUFFER_SIZE, 1);
	if (err < 0)
		return err;

	line6->line6midi = line6midi;
	return 0;
}
EXPORT_SYMBOL_GPL(line6_init_midi);
