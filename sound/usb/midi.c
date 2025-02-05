/*
 * usbmidi.c - ALSA USB MIDI driver
 *
 * Copyright (c) 2002-2009 Clemens Ladisch
 * All rights reserved.
 *
 * Based on the OSS usb-midi driver by NAGANO Daisuke,
 *          NetBSD's umidi driver by Takuya SHIOZAKI,
 *          the "USB Device Class Definition for MIDI Devices" by Roland
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed and/or modified under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/usb.h>
#include <linux/wait.h>
#include <linux/usb/audio.h>
#include <linux/usb/midi.h>
#include <linux/module.h>

#include <sound/core.h>
#include <sound/control.h>
#include <sound/rawmidi.h>
#include <sound/asequencer.h>
#include "usbaudio.h"
#include "midi.h"
#include "power.h"
#include "helper.h"

/*
 * define this to log all USB packets
 */
/* #define DUMP_PACKETS */

/*
 * how long to wait after some USB errors, so that hub_wq can disconnect() us
 * without too many spurious errors
 */
#define ERROR_DELAY_JIFFIES (HZ / 10)

#define OUTPUT_URBS 7
#define INPUT_URBS 7


MODULE_AUTHOR("Clemens Ladisch <clemens@ladisch.de>");
MODULE_DESCRIPTION("USB Audio/MIDI helper module");
MODULE_LICENSE("Dual BSD/GPL");

struct snd_usb_midi_in_endpoint;
struct snd_usb_midi_out_endpoint;
struct snd_usb_midi_endpoint;

struct usb_protocol_ops {
	void (*input)(struct snd_usb_midi_in_endpoint*, uint8_t*, int);
	void (*output)(struct snd_usb_midi_out_endpoint *ep, struct urb *urb);
	void (*output_packet)(struct urb*, uint8_t, uint8_t, uint8_t, uint8_t);
	void (*init_out_endpoint)(struct snd_usb_midi_out_endpoint *);
	void (*finish_out_endpoint)(struct snd_usb_midi_out_endpoint *);
};

struct snd_usb_midi {
	struct usb_device *dev;
	struct snd_card *card;
	struct usb_interface *iface;
	const struct snd_usb_audio_quirk *quirk;
	struct snd_rawmidi *rmidi;
	const struct usb_protocol_ops *usb_protocol_ops;
	struct list_head list;
	struct timer_list error_timer;
	spinlock_t disc_lock;
	struct rw_semaphore disc_rwsem;
	struct mutex mutex;
	u32 usb_id;
	int next_midi_device;

	struct snd_usb_midi_endpoint {
		struct snd_usb_midi_out_endpoint *out;
		struct snd_usb_midi_in_endpoint *in;
	} endpoints[MIDI_MAX_ENDPOINTS];
	unsigned long input_triggered;
	unsigned int opened[2];
	unsigned char disconnected;
	unsigned char input_running;

	struct snd_kcontrol *roland_load_ctl;
};

struct snd_usb_midi_out_endpoint {
	struct snd_usb_midi *umidi;
	struct out_urb_context {
		struct urb *urb;
		struct snd_usb_midi_out_endpoint *ep;
	} urbs[OUTPUT_URBS];
	unsigned int active_urbs;
	unsigned int drain_urbs;
	int max_transfer;		/* size of urb buffer */
	struct work_struct work;
	unsigned int next_urb;
	spinlock_t buffer_lock;

	struct usbmidi_out_port {
		struct snd_usb_midi_out_endpoint *ep;
		struct snd_rawmidi_substream *substream;
		int active;
		uint8_t cable;		/* cable number << 4 */
		uint8_t state;
#define STATE_UNKNOWN	0
#define STATE_1PARAM	1
#define STATE_2PARAM_1	2
#define STATE_2PARAM_2	3
#define STATE_SYSEX_0	4
#define STATE_SYSEX_1	5
#define STATE_SYSEX_2	6
		uint8_t data[2];
	} ports[0x10];
	int current_port;

	wait_queue_head_t drain_wait;
};

struct snd_usb_midi_in_endpoint {
	struct snd_usb_midi *umidi;
	struct urb *urbs[INPUT_URBS];
	struct usbmidi_in_port {
		struct snd_rawmidi_substream *substream;
		u8 running_status_length;
	} ports[0x10];
	u8 seen_f5;
	bool in_sysex;
	u8 last_cin;
	u8 error_resubmit;
	int current_port;
};

static void snd_usbmidi_do_output(struct snd_usb_midi_out_endpoint *ep);

static const uint8_t snd_usbmidi_cin_length[] = {
	0, 0, 2, 3, 3, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3, 1
};

/*
 * Submits the URB, with error handling.
 */
static int snd_usbmidi_submit_urb(struct urb *urb, gfp_t flags)
{
	int err = usb_submit_urb(urb, flags);
	if (err < 0 && err != -ENODEV)
		dev_err(&urb->dev->dev, "usb_submit_urb: %d\n", err);
	return err;
}

/*
 * Error handling for URB completion functions.
 */
static int snd_usbmidi_urb_error(const struct urb *urb)
{
	switch (urb->status) {
	/* manually unlinked, or device gone */
	case -ENOENT:
	case -ECONNRESET:
	case -ESHUTDOWN:
	case -ENODEV:
		return -ENODEV;
	/* errors that might occur during unplugging */
	case -EPROTO:
	case -ETIME:
	case -EILSEQ:
		return -EIO;
	default:
		dev_err(&urb->dev->dev, "urb status %d\n", urb->status);
		return 0; /* continue */
	}
}

/*
 * Receives a chunk of MIDI data.
 */
static void snd_usbmidi_input_data(struct snd_usb_midi_in_endpoint *ep,
				   int portidx, uint8_t *data, int length)
{
	struct usbmidi_in_port *port = &ep->ports[portidx];

	if (!port->substream) {
		dev_dbg(&ep->umidi->dev->dev, "unexpected port %d!\n", portidx);
		return;
	}
	if (!test_bit(port->substream->number, &ep->umidi->input_triggered))
		return;
	snd_rawmidi_receive(port->substream, data, length);
}

#ifdef DUMP_PACKETS
static void dump_urb(const char *type, const u8 *data, int length)
{
	pr_debug("%s packet: [", type);
	for (; length > 0; ++data, --length)
		pr_cont(" %02x", *data);
	pr_cont(" ]\n");
}
#else
#define dump_urb(type, data, length) /* nothing */
#endif

/*
 * Processes the data read from the device.
 */
static void snd_usbmidi_in_urb_complete(struct urb *urb)
{
	struct snd_usb_midi_in_endpoint *ep = urb->context;

	if (urb->status == 0) {
		dump_urb("received", urb->transfer_buffer, urb->actual_length);
		ep->umidi->usb_protocol_ops->input(ep, urb->transfer_buffer,
						   urb->actual_length);
	} else {
		int err = snd_usbmidi_urb_error(urb);
		if (err < 0) {
			if (err != -ENODEV) {
				ep->error_resubmit = 1;
				mod_timer(&ep->umidi->error_timer,
					  jiffies + ERROR_DELAY_JIFFIES);
			}
			return;
		}
	}

	urb->dev = ep->umidi->dev;
	snd_usbmidi_submit_urb(urb, GFP_ATOMIC);
}

static void snd_usbmidi_out_urb_complete(struct urb *urb)
{
	struct out_urb_context *context = urb->context;
	struct snd_usb_midi_out_endpoint *ep = context->ep;
	unsigned int urb_index;
	unsigned long flags;

	spin_lock_irqsave(&ep->buffer_lock, flags);
	urb_index = context - ep->urbs;
	ep->active_urbs &= ~(1 << urb_index);
	if (unlikely(ep->drain_urbs)) {
		ep->drain_urbs &= ~(1 << urb_index);
		wake_up(&ep->drain_wait);
	}
	spin_unlock_irqrestore(&ep->buffer_lock, flags);
	if (urb->status < 0) {
		int err = snd_usbmidi_urb_error(urb);
		if (err < 0) {
			if (err != -ENODEV)
				mod_timer(&ep->umidi->error_timer,
					  jiffies + ERROR_DELAY_JIFFIES);
			return;
		}
	}
	snd_usbmidi_do_output(ep);
}

/*
 * This is called when some data should be transferred to the device
 * (from one or more substreams).
 */
static void snd_usbmidi_do_output(struct snd_usb_midi_out_endpoint *ep)
{
	unsigned int urb_index;
	struct urb *urb;
	unsigned long flags;

	spin_lock_irqsave(&ep->buffer_lock, flags);
	if (ep->umidi->disconnected) {
		spin_unlock_irqrestore(&ep->buffer_lock, flags);
		return;
	}

	urb_index = ep->next_urb;
	for (;;) {
		if (!(ep->active_urbs & (1 << urb_index))) {
			urb = ep->urbs[urb_index].urb;
			urb->transfer_buffer_length = 0;
			ep->umidi->usb_protocol_ops->output(ep, urb);
			if (urb->transfer_buffer_length == 0)
				break;

			dump_urb("sending", urb->transfer_buffer,
				 urb->transfer_buffer_length);
			urb->dev = ep->umidi->dev;
			if (snd_usbmidi_submit_urb(urb, GFP_ATOMIC) < 0)
				break;
			ep->active_urbs |= 1 << urb_index;
		}
		if (++urb_index >= OUTPUT_URBS)
			urb_index = 0;
		if (urb_index == ep->next_urb)
			break;
	}
	ep->next_urb = urb_index;
	spin_unlock_irqrestore(&ep->buffer_lock, flags);
}

static void snd_usbmidi_out_work(struct work_struct *work)
{
	struct snd_usb_midi_out_endpoint *ep =
		container_of(work, struct snd_usb_midi_out_endpoint, work);

	snd_usbmidi_do_output(ep);
}

/* called after transfers had been interrupted due to some USB error */
static void snd_usbmidi_error_timer(struct timer_list *t)
{
	struct snd_usb_midi *umidi = from_timer(umidi, t, error_timer);
	unsigned int i, j;

	spin_lock(&umidi->disc_lock);
	if (umidi->disconnected) {
		spin_unlock(&umidi->disc_lock);
		return;
	}
	for (i = 0; i < MIDI_MAX_ENDPOINTS; ++i) {
		struct snd_usb_midi_in_endpoint *in = umidi->endpoints[i].in;
		if (in && in->error_resubmit) {
			in->error_resubmit = 0;
			for (j = 0; j < INPUT_URBS; ++j) {
				if (atomic_read(&in->urbs[j]->use_count))
					continue;
				in->urbs[j]->dev = umidi->dev;
				snd_usbmidi_submit_urb(in->urbs[j], GFP_ATOMIC);
			}
		}
		if (umidi->endpoints[i].out)
			snd_usbmidi_do_output(umidi->endpoints[i].out);
	}
	spin_unlock(&umidi->disc_lock);
}

/* helper function to send static data that may not DMA-able */
static int send_bulk_static_data(struct snd_usb_midi_out_endpoint *ep,
				 const void *data, int len)
{
	int err = 0;
	void *buf = kmemdup(data, len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	dump_urb("sending", buf, len);
	if (ep->urbs[0].urb)
		err = usb_bulk_msg(ep->umidi->dev, ep->urbs[0].urb->pipe,
				   buf, len, NULL, 250);
	kfree(buf);
	return err;
}

/*
 * Standard USB MIDI protocol: see the spec.
 * Midiman protocol: like the standard protocol, but the control byte is the
 * fourth byte in each packet, and uses length instead of CIN.
 */

static void snd_usbmidi_standard_input(struct snd_usb_midi_in_endpoint *ep,
				       uint8_t *buffer, int buffer_length)
{
	int i;

	for (i = 0; i + 3 < buffer_length; i += 4)
		if (buffer[i] != 0) {
			int cable = buffer[i] >> 4;
			int length = snd_usbmidi_cin_length[buffer[i] & 0x0f];
			snd_usbmidi_input_data(ep, cable, &buffer[i + 1],
					       length);
		}
}

static void snd_usbmidi_midiman_input(struct snd_usb_midi_in_endpoint *ep,
				      uint8_t *buffer, int buffer_length)
{
	int i;

	for (i = 0; i + 3 < buffer_length; i += 4)
		if (buffer[i + 3] != 0) {
			int port = buffer[i + 3] >> 4;
			int length = buffer[i + 3] & 3;
			snd_usbmidi_input_data(ep, port, &buffer[i], length);
		}
}

/*
 * Buggy M-Audio device: running status on input results in a packet that has
 * the data bytes but not the status byte and that is marked with CIN 4.
 */
static void snd_usbmidi_maudio_broken_running_status_input(
					struct snd_usb_midi_in_endpoint *ep,
					uint8_t *buffer, int buffer_length)
{
	int i;

	for (i = 0; i + 3 < buffer_length; i += 4)
		if (buffer[i] != 0) {
			int cable = buffer[i] >> 4;
			u8 cin = buffer[i] & 0x0f;
			struct usbmidi_in_port *port = &ep->ports[cable];
			int length;

			length = snd_usbmidi_cin_length[cin];
			if (cin == 0xf && buffer[i + 1] >= 0xf8)
				; /* realtime msg: no running status change */
			else if (cin >= 0x8 && cin <= 0xe)
				/* channel msg */
				port->running_status_length = length - 1;
			else if (cin == 0x4 &&
				 port->running_status_length != 0 &&
				 buffer[i + 1] < 0x80)
				/* CIN 4 that is not a SysEx */
				length = port->running_status_length;
			else
				/*
				 * All other msgs cannot begin running status.
				 * (A channel msg sent as two or three CIN 0xF
				 * packets could in theory, but this device
				 * doesn't use this format.)
				 */
				port->running_status_length = 0;
			snd_usbmidi_input_data(ep, cable, &buffer[i + 1],
					       length);
		}
}

/*
 * QinHeng CH345 is buggy: every second packet inside a SysEx has not CIN 4
 * but the previously seen CIN, but still with three data bytes.
 */
static void ch345_broken_sysex_input(struct snd_usb_midi_in_endpoint *ep,
				     uint8_t *buffer, int buffer_length)
{
	unsigned int i, cin, length;

	for (i = 0; i + 3 < buffer_length; i += 4) {
		if (buffer[i] == 0 && i > 0)
			break;
		cin = buffer[i] & 0x0f;
		if (ep->in_sysex &&
		    cin == ep->last_cin &&
		    (buffer[i + 1 + (cin == 0x6)] & 0x80) == 0)
			cin = 0x4;
#if 0
		if (buffer[i + 1] == 0x90) {
			/*
			 * Either a corrupted running status or a real note-on
			 * message; impossible to detect reliably.
			 */
		}
#endif
		length = snd_usbmidi_cin_length[cin];
		snd_usbmidi_input_data(ep, 0, &buffer[i + 1], length);
		ep->in_sysex = cin == 0x4;
		if (!ep->in_sysex)
			ep->last_cin = cin;
	}
}

/*
 * CME protocol: like the standard protocol, but SysEx commands are sent as a
 * single USB packet preceded by a 0x0F byte.
 */
static void snd_usbmidi_cme_input(struct snd_usb_midi_in_endpoint *ep,
				  uint8_t *buffer, int buffer_length)
{
	if (buffer_length < 2 || (buffer[0] & 0x0f) != 0x0f)
		snd_usbmidi_standard_input(ep, buffer, buffer_length);
	else
		snd_usbmidi_input_data(ep, buffer[0] >> 4,
				       &buffer[1], buffer_length - 1);
}

/*
 * Adds one USB MIDI packet to the output buffer.
 */
static void snd_usbmidi_output_standard_packet(struct urb *urb, uint8_t p0,
					       uint8_t p1, uint8_t p2,
					       uint8_t p3)
{

	uint8_t *buf =
		(uint8_t *)urb->transfer_buffer + urb->transfer_buffer_length;
	buf[0] = p0;
	buf[1] = p1;
	buf[2] = p2;
	buf[3] = p3;
	urb->transfer_buffer_length += 4;
}

/*
 * Adds one Midiman packet to the output buffer.
 */
static void snd_usbmidi_output_midiman_packet(struct urb *urb, uint8_t p0,
					      uint8_t p1, uint8_t p2,
					      uint8_t p3)
{

	uint8_t *buf =
		(uint8_t *)urb->transfer_buffer + urb->transfer_buffer_length;
	buf[0] = p1;
	buf[1] = p2;
	buf[2] = p3;
	buf[3] = (p0 & 0xf0) | snd_usbmidi_cin_length[p0 & 0x0f];
	urb->transfer_buffer_length += 4;
}

/*
 * Converts MIDI commands to USB MIDI packets.
 */
static void snd_usbmidi_transmit_byte(struct usbmidi_out_port *port,
				      uint8_t b, struct urb *urb)
{
	uint8_t p0 = port->cable;
	void (*output_packet)(struct urb*, uint8_t, uint8_t, uint8_t, uint8_t) =
		port->ep->umidi->usb_protocol_ops->output_packet;

	if (b >= 0xf8) {
		output_packet(urb, p0 | 0x0f, b, 0, 0);
	} else if (b >= 0xf0) {
		switch (b) {
		case 0xf0:
			port->data[0] = b;
			port->state = STATE_SYSEX_1;
			break;
		case 0xf1:
		case 0xf3:
			port->data[0] = b;
			port->state = STATE_1PARAM;
			break;
		case 0xf2:
			port->data[0] = b;
			port->state = STATE_2PARAM_1;
			break;
		case 0xf4:
		case 0xf5:
			port->state = STATE_UNKNOWN;
			break;
		case 0xf6:
			output_packet(urb, p0 | 0x05, 0xf6, 0, 0);
			port->state = STATE_UNKNOWN;
			break;
		case 0xf7:
			switch (port->state) {
			case STATE_SYSEX_0:
				output_packet(urb, p0 | 0x05, 0xf7, 0, 0);
				break;
			case STATE_SYSEX_1:
				output_packet(urb, p0 | 0x06, port->data[0],
					      0xf7, 0);
				break;
			case STATE_SYSEX_2:
				output_packet(urb, p0 | 0x07, port->data[0],
					      port->data[1], 0xf7);
				break;
			}
			port->state = STATE_UNKNOWN;
			break;
		}
	} else if (b >= 0x80) {
		port->data[0] = b;
		if (b >= 0xc0 && b <= 0xdf)
			port->state = STATE_1PARAM;
		else
			port->state = STATE_2PARAM_1;
	} else { /* b < 0x80 */
		switch (port->state) {
		case STATE_1PARAM:
			if (port->data[0] < 0xf0) {
				p0 |= port->data[0] >> 4;
			} else {
				p0 |= 0x02;
				port->state = STATE_UNKNOWN;
			}
			output_packet(urb, p0, port->data[0], b, 0);
			break;
		case STATE_2PARAM_1:
			port->data[1] = b;
			port->state = STATE_2PARAM_2;
			break;
		case STATE_2PARAM_2:
			if (port->data[0] < 0xf0) {
				p0 |= port->data[0] >> 4;
				port->state = STATE_2PARAM_1;
			} else {
				p0 |= 0x03;
				port->state = STATE_UNKNOWN;
			}
			output_packet(urb, p0, port->data[0], port->data[1], b);
			break;
		case STATE_SYSEX_0:
			port->data[0] = b;
			port->state = STATE_SYSEX_1;
			break;
		case STATE_SYSEX_1:
			port->data[1] = b;
			port->state = STATE_SYSEX_2;
			break;
		case STATE_SYSEX_2:
			output_packet(urb, p0 | 0x04, port->data[0],
				      port->data[1], b);
			port->state = STATE_SYSEX_0;
			break;
		}
	}
}

static void snd_usbmidi_standard_output(struct snd_usb_midi_out_endpoint *ep,
					struct urb *urb)
{
	int p;

	/* FIXME: lower-numbered ports can starve higher-numbered ports */
	for (p = 0; p < 0x10; ++p) {
		struct usbmidi_out_port *port = &ep->ports[p];
		if (!port->active)
			continue;
		while (urb->transfer_buffer_length + 3 < ep->max_transfer) {
			uint8_t b;
			if (snd_rawmidi_transmit(port->substream, &b, 1) != 1) {
				port->active = 0;
				break;
			}
			snd_usbmidi_transmit_byte(port, b, urb);
		}
	}
}

static const struct usb_protocol_ops snd_usbmidi_standard_ops = {
	.input = snd_usbmidi_standard_input,
	.output = snd_usbmidi_standard_output,
	.output_packet = snd_usbmidi_output_standard_packet,
};

static const struct usb_protocol_ops snd_usbmidi_midiman_ops = {
	.input = snd_usbmidi_midiman_input,
	.output = snd_usbmidi_standard_output,
	.output_packet = snd_usbmidi_output_midiman_packet,
};

static const
struct usb_protocol_ops snd_usbmidi_maudio_broken_running_status_ops = {
	.input = snd_usbmidi_maudio_broken_running_status_input,
	.output = snd_usbmidi_standard_output,
	.output_packet = snd_usbmidi_output_standard_packet,
};

static const struct usb_protocol_ops snd_usbmidi_cme_ops = {
	.input = snd_usbmidi_cme_input,
	.output = snd_usbmidi_standard_output,
	.output_packet = snd_usbmidi_output_standard_packet,
};

static const struct usb_protocol_ops snd_usbmidi_ch345_broken_sysex_ops = {
	.input = ch345_broken_sysex_input,
	.output = snd_usbmidi_standard_output,
	.output_packet = snd_usbmidi_output_standard_packet,
};

/*
 * AKAI MPD16 protocol:
 *
 * For control port (endpoint 1):
 * ==============================
 * One or more chunks consisting of first byte of (0x10 | msg_len) and then a
 * SysEx message (msg_len=9 bytes long).
 *
 * For data port (endpoint 2):
 * ===========================
 * One or more chunks consisting of first byte of (0x20 | msg_len) and then a
 * MIDI message (msg_len bytes long)
 *
 * Messages sent: Active Sense, Note On, Poly Pressure, Control Change.
 */
static void snd_usbmidi_akai_input(struct snd_usb_midi_in_endpoint *ep,
				   uint8_t *buffer, int buffer_length)
{
	unsigned int pos = 0;
	unsigned int len = (unsigned int)buffer_length;
	while (pos < len) {
		unsigned int port = (buffer[pos] >> 4) - 1;
		unsigned int msg_len = buffer[pos] & 0x0f;
		pos++;
		if (pos + msg_len <= len && port < 2)
			snd_usbmidi_input_data(ep, 0, &buffer[pos], msg_len);
		pos += msg_len;
	}
}

#define MAX_AKAI_SYSEX_LEN 9

static void snd_usbmidi_akai_output(struct snd_usb_midi_out_endpoint *ep,
				    struct urb *urb)
{
	uint8_t *msg;
	int pos, end, count, buf_end;
	uint8_t tmp[MAX_AKAI_SYSEX_LEN];
	struct snd_rawmidi_substream *substream = ep->ports[0].substream;

	if (!ep->ports[0].active)
		return;

	msg = urb->transfer_buffer + urb->transfer_buffer_length;
	buf_end = ep->max_transfer - MAX_AKAI_SYSEX_LEN - 1;

	/* only try adding more data when there's space for at least 1 SysEx */
	while (urb->transfer_buffer_length < buf_end) {
		count = snd_rawmidi_transmit_peek(substream,
						  tmp, MAX_AKAI_SYSEX_LEN);
		if (!count) {
			ep->ports[0].active = 0;
			return;
		}
		/* try to skip non-SysEx data */
		for (pos = 0; pos < count && tmp[pos] != 0xF0; pos++)
			;

		if (pos > 0) {
			snd_rawmidi_transmit_ack(substream, pos);
			continue;
		}

		/* look for the start or end marker */
		for (end = 1; end < count && tmp[end] < 0xF0; end++)
			;

		/* next SysEx started before the end of current one */
		if (end < count && tmp[end] == 0xF0) {
			/* it's incomplete - drop it */
			snd_rawmidi_transmit_ack(substream, end);
			continue;
		}
		/* SysEx complete */
		if (end < count && tmp[end] == 0xF7) {
			/* queue it, ack it, and get the next one */
			count = end + 1;
			msg[0] = 0x10 | count;
			memcpy(&msg[1], tmp, count);
			snd_rawmidi_transmit_ack(substream, count);
			urb->transfer_buffer_length += count + 1;
			msg += count + 1;
			continue;
		}
		/* less than 9 bytes and no end byte - wait for more */
		if (count < MAX_AKAI_SYSEX_LEN) {
			ep->ports[0].active = 0;
			return;
		}
		/* 9 bytes and no end marker in sight - malformed, skip it */
		snd_rawmidi_transmit_ack(substream, count);
	}
}

static const struct usb_protocol_ops snd_usbmidi_akai_ops = {
	.input = snd_usbmidi_akai_input,
	.output = snd_usbmidi_akai_output,
};

/*
 * Novation USB MIDI protocol: number of data bytes is in the first byte
 * (when receiving) (+1!) or in the second byte (when sending); data begins
 * at the third byte.
 */

static void snd_usbmidi_novation_input(struct snd_usb_midi_in_endpoint *ep,
				       uint8_t *buffer, int buffer_length)
{
	if (buffer_length < 2 || !buffer[0] || buffer_length < buffer[0] + 1)
		return;
	snd_usbmidi_input_data(ep, 0, &buffer[2], buffer[0] - 1);
}

static void snd_usbmidi_novation_output(struct snd_usb_midi_out_endpoint *ep,
					struct urb *urb)
{
	uint8_t *transfer_buffer;
	int count;

	if (!ep->ports[0].active)
		return;
	transfer_buffer = urb->transfer_buffer;
	count = snd_rawmidi_transmit(ep->ports[0].substream,
				     &transfer_buffer[2],
				     ep->max_transfer - 2);
	if (count < 1) {
		ep->ports[0].active = 0;
		return;
	}
	transfer_buffer[0] = 0;
	transfer_buffer[1] = count;
	urb->transfer_buffer_length = 2 + count;
}

static const struct usb_protocol_ops snd_usbmidi_novation_ops = {
	.input = snd_usbmidi_novation_input,
	.output = snd_usbmidi_novation_output,
};

/*
 * "raw" protocol: just move raw MIDI bytes from/to the endpoint
 */

static void snd_usbmidi_raw_input(struct snd_usb_midi_in_endpoint *ep,
				  uint8_t *buffer, int buffer_length)
{
	snd_usbmidi_input_data(ep, 0, buffer, buffer_length);
}

static void snd_usbmidi_raw_output(struct snd_usb_midi_out_endpoint *ep,
				   struct urb *urb)
{
	int count;

	if (!ep->ports[0].active)
		return;
	count = snd_rawmidi_transmit(ep->ports[0].substream,
				     urb->transfer_buffer,
				     ep->max_transfer);
	if (count < 1) {
		ep->ports[0].active = 0;
		return;
	}
	urb->transfer_buffer_length = count;
}

static const struct usb_protocol_ops snd_usbmidi_raw_ops = {
	.input = snd_usbmidi_raw_input,
	.output = snd_usbmidi_raw_output,
};

/*
 * FTDI protocol: raw MIDI bytes, but input packets have two modem status bytes.
 */

static void snd_usbmidi_ftdi_input(struct snd_usb_midi_in_endpoint *ep,
				   uint8_t *buffer, int buffer_length)
{
	if (buffer_length > 2)
		snd_usbmidi_input_data(ep, 0, buffer + 2, buffer_length - 2);
}

static const struct usb_protocol_ops snd_usbmidi_ftdi_ops = {
	.input = snd_usbmidi_ftdi_input,
	.output = snd_usbmidi_raw_output,
};

static void snd_usbmidi_us122l_input(struct snd_usb_midi_in_endpoint *ep,
				     uint8_t *buffer, int buffer_length)
{
	if (buffer_length != 9)
		return;
	buffer_length = 8;
	while (buffer_length && buffer[buffer_length - 1] == 0xFD)
		buffer_length--;
	if (buffer_length)
		snd_usbmidi_input_data(ep, 0, buffer, buffer_length);
}

static void snd_usbmidi_us122l_output(struct snd_usb_midi_out_endpoint *ep,
				      struct urb *urb)
{
	int count;

	if (!ep->ports[0].active)
		return;
	switch (snd_usb_get_speed(ep->umidi->dev)) {
	case USB_SPEED_HIGH:
	case USB_SPEED_SUPER:
	case USB_SPEED_SUPER_PLUS:
		count = 1;
		break;
	default:
		count = 2;
	}
	count = snd_rawmidi_transmit(ep->ports[0].substream,
				     urb->transfer_buffer,
				     count);
	if (count < 1) {
		ep->ports[0].active = 0;
		return;
	}

	memset(urb->transfer_buffer + count, 0xFD, ep->max_transfer - count);
	urb->transfer_buffer_length = ep->max_transfer;
}

static const struct usb_protocol_ops snd_usbmidi_122l_ops = {
	.input = snd_usbmidi_us122l_input,
	.output = snd_usbmidi_us122l_output,
};

/*
 * Emagic USB MIDI protocol: raw MIDI with "F5 xx" port switching.
 */

static void snd_usbmidi_emagic_init_out(struct snd_usb_midi_out_endpoint *ep)
{
	static const u8 init_data[] = {
		/* initialization magic: "get version" */
		0xf0,
		0x00, 0x20, 0x31,	/* Emagic */
		0x64,			/* Unitor8 */
		0x0b,			/* version number request */
		0x00,			/* command version */
		0x00,			/* EEPROM, box 0 */
		0xf7
	};
	send_bulk_static_data(ep, init_data, sizeof(init_data));
	/* while we're at it, pour on more magic */
	send_bulk_static_data(ep, init_data, sizeof(init_data));
}

static void snd_usbmidi_emagic_finish_out(struct snd_usb_midi_out_endpoint *ep)
{
	static const u8 finish_data[] = {
		/* switch to patch mode with last preset */
		0xf0,
		0x00, 0x20, 0x31,	/* Emagic */
		0x64,			/* Unitor8 */
		0x10,			/* patch switch command */
		0x00,			/* command version */
		0x7f,			/* to all boxes */
		0x40,			/* last preset in EEPROM */
		0xf7
	};
	send_bulk_static_data(ep, finish_data, sizeof(finish_data));
}

static void snd_usbmidi_emagic_input(struct snd_usb_midi_in_endpoint *ep,
				     uint8_t *buffer, int buffer_length)
{
	int i;

	/* FF indicates end of valid data */
	for (i = 0; i < buffer_length; ++i)
		if (buffer[i] == 0xff) {
			buffer_length = i;
			break;
		}

	/* handle F5 at end of last buffer */
	if (ep->seen_f5)
		goto switch_port;

	while (buffer_length > 0) {
		/* determine size of data until next F5 */
		for (i = 0; i < buffer_length; ++i)
			if (buffer[i] == 0xf5)
				break;
		snd_usbmidi_input_data(ep, ep->current_port, buffer, i);
		buffer += i;
		buffer_length -= i;

		if (buffer_length <= 0)
			break;
		/* assert(buffer[0] == 0xf5); */
		ep->seen_f5 = 1;
		++buffer;
		--buffer_length;

	switch_port:
		if (buffer_length <= 0)
			break;
		if (buffer[0] < 0x80) {
			ep->current_port = (buffer[0] - 1) & 15;
			++buffer;
			--buffer_length;
		}
		ep->seen_f5 = 0;
	}
}

static void snd_usbmidi_emagic_output(struct snd_usb_midi_out_endpoint *ep,
				      struct urb *urb)
{
	int port0 = ep->current_port;
	uint8_t *buf = urb->transfer_buffer;
	int buf_free = ep->max_transfer;
	int length, i;

	for (i = 0; i < 0x10; ++i) {
		/* round-robin, starting at the last current port */
		int portnum = (port0 + i) & 15;
		struct usbmidi_out_port *port = &ep->ports[portnum];

		if (!port->active)
			continue;
		if (snd_rawmidi_transmit_peek(port->substream, buf, 1) != 1) {
			port->active = 0;
			continue;
		}

		if (portnum != ep->current_port) {
			if (buf_free < 2)
				break;
			ep->current_port = portnum;
			buf[0] = 0xf5;
			buf[1] = (portnum + 1) & 15;
			buf += 2;
			buf_free -= 2;
		}

		if (buf_free < 1)
			break;
		length = snd_rawmidi_transmit(port->substream, buf, buf_free);
		if (length > 0) {
			buf += length;
			buf_free -= length;
			if (buf_free < 1)
				break;
		}
	}
	if (buf_free < ep->max_transfer && buf_free > 0) {
		*buf = 0xff;
		--buf_free;
	}
	urb->transfer_buffer_length = ep->max_transfer - buf_free;
}

static const struct usb_protocol_ops snd_usbmidi_emagic_ops = {
	.input = snd_usbmidi_emagic_input,
	.output = snd_usbmidi_emagic_output,
	.init_out_endpoint = snd_usbmidi_emagic_init_out,
	.finish_out_endpoint = snd_usbmidi_emagic_finish_out,
};


static void update_roland_altsetting(struct snd_usb_midi *umidi)
{
	struct usb_interface *intf;
	struct usb_host_interface *hostif;
	struct usb_interface_descriptor *intfd;
	int is_light_load;

	intf = umidi->iface;
	is_light_load = intf->cur_altsetting != intf->altsetting;
	if (umidi->roland_load_ctl->private_value == is_light_load)
		return;
	hostif = &intf->altsetting[umidi->roland_load_ctl->private_value];
	intfd = get_iface_desc(hostif);
	snd_usbmidi_input_stop(&umidi->list);
	usb_set_interface(umidi->dev, intfd->bInterfaceNumber,
			  intfd->bAlternateSetting);
	snd_usbmidi_input_start(&umidi->list);
}

static int substream_open(struct snd_rawmidi_substream *substream, int dir,
			  int open)
{
	struct snd_usb_midi *umidi = substream->rmidi->private_data;
	struct snd_kcontrol *ctl;

	down_read(&umidi->disc_rwsem);
	if (umidi->disconnected) {
		up_read(&umidi->disc_rwsem);
		return open ? -ENODEV : 0;
	}

	mutex_lock(&umidi->mutex);
	if (open) {
		if (!umidi->opened[0] && !umidi->opened[1]) {
			if (umidi->roland_load_ctl) {
				ctl = umidi->roland_load_ctl;
				ctl->vd[0].access |=
					SNDRV_CTL_ELEM_ACCESS_INACTIVE;
				snd_ctl_notify(umidi->card,
				       SNDRV_CTL_EVENT_MASK_INFO, &ctl->id);
				update_roland_altsetting(umidi);
			}
		}
		umidi->opened[dir]++;
		if (umidi->opened[1])
			snd_usbmidi_input_start(&umidi->list);
	} else {
		umidi->opened[dir]--;
		if (!umidi->opened[1])
			snd_usbmidi_input_stop(&umidi->list);
		if (!umidi->opened[0] && !umidi->opened[1]) {
			if (umidi->roland_load_ctl) {
				ctl = umidi->roland_load_ctl;
				ctl->vd[0].access &=
					~SNDRV_CTL_ELEM_ACCESS_INACTIVE;
				snd_ctl_notify(umidi->card,
				       SNDRV_CTL_EVENT_MASK_INFO, &ctl->id);
			}
		}
	}
	mutex_unlock(&umidi->mutex);
	up_read(&umidi->disc_rwsem);
	return 0;
}

static int snd_usbmidi_output_open(struct snd_rawmidi_substream *substream)
{
	struct snd_usb_midi *umidi = substream->rmidi->private_data;
	struct usbmidi_out_port *port = NULL;
	int i, j;

	for (i = 0; i < MIDI_MAX_ENDPOINTS; ++i)
		if (umidi->endpoints[i].out)
			for (j = 0; j < 0x10; ++j)
				if (umidi->endpoints[i].out->ports[j].substream == substream) {
					port = &umidi->endpoints[i].out->ports[j];
					break;
				}
	if (!port)
		return -ENXIO;

	substream->runtime->private_data = port;
	port->state = STATE_UNKNOWN;
	return substream_open(substream, 0, 1);
}

static int snd_usbmidi_output_close(struct snd_rawmidi_substream *substream)
{
	struct usbmidi_out_port *port = substream->runtime->private_data;

	cancel_work_sync(&port->ep->work);
	return substream_open(substream, 0, 0);
}

static void snd_usbmidi_output_trigger(struct snd_rawmidi_substream *substream,
				       int up)
{
	struct usbmidi_out_port *port =
		(struct usbmidi_out_port *)substream->runtime->private_data;

	port->active = up;
	if (up) {
		if (port->ep->umidi->disconnected) {
			/* gobble up remaining bytes to prevent wait in
			 * snd_rawmidi_drain_output */
			snd_rawmidi_proceed(substream);
			return;
		}
		queue_work(system_highpri_wq, &port->ep->work);
	}
}

static void snd_usbmidi_output_drain(struct snd_rawmidi_substream *substream)
{
	struct usbmidi_out_port *port = substream->runtime->private_data;
	struct snd_usb_midi_out_endpoint *ep = port->ep;
	unsigned int drain_urbs;
	DEFINE_WAIT(wait);
	long timeout = msecs_to_jiffies(50);

	if (ep->umidi->disconnected)
		return;
	/*
	 * The substream buffer is empty, but some data might still be in the
	 * currently active URBs, so we have to wait for those to complete.
	 */
	spin_lock_irq(&ep->buffer_lock);
	drain_urbs = ep->active_urbs;
	if (drain_urbs) {
		ep->drain_urbs |= drain_urbs;
		do {
			prepare_to_wait(&ep->drain_wait, &wait,
					TASK_UNINTERRUPTIBLE);
			spin_unlock_irq(&ep->buffer_lock);
			timeout = schedule_timeout(timeout);
			spin_lock_irq(&ep->buffer_lock);
			drain_urbs &= ep->drain_urbs;
		} while (drain_urbs && timeout);
		finish_wait(&ep->drain_wait, &wait);
	}
	port->active = 0;
	spin_unlock_irq(&ep->buffer_lock);
}

static int snd_usbmidi_input_open(struct snd_rawmidi_substream *substream)
{
	return substream_open(substream, 1, 1);
}

static int snd_usbmidi_input_close(struct snd_rawmidi_substream *substream)
{
	return substream_open(substream, 1, 0);
}

static void snd_usbmidi_input_trigger(struct snd_rawmidi_substream *substream,
				      int up)
{
	struct snd_usb_midi *umidi = substream->rmidi->private_data;

	if (up)
		set_bit(substream->number, &umidi->input_triggered);
	else
		clear_bit(substream->number, &umidi->input_triggered);
}

static const struct snd_rawmidi_ops snd_usbmidi_output_ops = {
	.open = snd_usbmidi_output_open,
	.close = snd_usbmidi_output_close,
	.trigger = snd_usbmidi_output_trigger,
	.drain = snd_usbmidi_output_drain,
};

static const struct snd_rawmidi_ops snd_usbmidi_input_ops = {
	.open = snd_usbmidi_input_open,
	.close = snd_usbmidi_input_close,
	.trigger = snd_usbmidi_input_trigger
};

static void free_urb_and_buffer(struct snd_usb_midi *umidi, struct urb *urb,
				unsigned int buffer_length)
{
	usb_free_coherent(umidi->dev, buffer_length,
			  urb->transfer_buffer, urb->transfer_dma);
	usb_free_urb(urb);
}

/*
 * Frees an input endpoint.
 * May be called when ep hasn't been initialized completely.
 */
static void snd_usbmidi_in_endpoint_delete(struct snd_usb_midi_in_endpoint *ep)
{
	unsigned int i;

	for (i = 0; i < INPUT_URBS; ++i)
		if (ep->urbs[i])
			free_urb_and_buffer(ep->umidi, ep->urbs[i],
					    ep->urbs[i]->transfer_buffer_length);
	kfree(ep);
}

/*
 * Creates an input endpoint.
 */
static int snd_usbmidi_in_endpoint_create(struct snd_usb_midi *umidi,
					  struct snd_usb_midi_endpoint_info *ep_info,
					  struct snd_usb_midi_endpoint *rep)
{
	struct snd_usb_midi_in_endpoint *ep;
	void *buffer;
	unsigned int pipe;
	int length;
	unsigned int i;
	int err;

	rep->in = NULL;
	ep = kzalloc(sizeof(*ep), GFP_KERNEL);
	if (!ep)
		return -ENOMEM;
	ep->umidi = umidi;

	for (i = 0; i < INPUT_URBS; ++i) {
		ep->urbs[i] = usb_alloc_urb(0, GFP_KERNEL);
		if (!ep->urbs[i]) {
			err = -ENOMEM;
			goto error;
		}
	}
	if (ep_info->in_interval)
		pipe = usb_rcvintpipe(umidi->dev, ep_info->in_ep);
	else
		pipe = usb_rcvbulkpipe(umidi->dev, ep_info->in_ep);
	length = usb_maxpacket(umidi->dev, pipe);
	for (i = 0; i < INPUT_URBS; ++i) {
		buffer = usb_alloc_coherent(umidi->dev, length, GFP_KERNEL,
					    &ep->urbs[i]->transfer_dma);
		if (!buffer) {
			err = -ENOMEM;
			goto error;
		}
		if (ep_info->in_interval)
			usb_fill_int_urb(ep->urbs[i], umidi->dev,
					 pipe, buffer, length,
					 snd_usbmidi_in_urb_complete,
					 ep, ep_info->in_interval);
		else
			usb_fill_bulk_urb(ep->urbs[i], umidi->dev,
					  pipe, buffer, length,
					  snd_usbmidi_in_urb_complete, ep);
		ep->urbs[i]->transfer_flags = URB_NO_TRANSFER_DMA_MAP;
		err = usb_urb_ep_type_check(ep->urbs[i]);
		if (err < 0) {
			dev_err(&umidi->dev->dev, "invalid MIDI in EP %x\n",
				ep_info->in_ep);
			goto error;
		}
	}

	rep->in = ep;
	return 0;

 error:
	snd_usbmidi_in_endpoint_delete(ep);
	return err;
}

/*
 * Frees an output endpoint.
 * May be called when ep hasn't been initialized completely.
 */
static void snd_usbmidi_out_endpoint_clear(struct snd_usb_midi_out_endpoint *ep)
{
	unsigned int i;

	for (i = 0; i < OUTPUT_URBS; ++i)
		if (ep->urbs[i].urb) {
			free_urb_and_buffer(ep->umidi, ep->urbs[i].urb,
					    ep->max_transfer);
			ep->urbs[i].urb = NULL;
		}
}

static void snd_usbmidi_out_endpoint_delete(struct snd_usb_midi_out_endpoint *ep)
{
	snd_usbmidi_out_endpoint_clear(ep);
	kfree(ep);
}

/*
 * Creates an output endpoint, and initializes output ports.
 */
static int snd_usbmidi_out_endpoint_create(struct snd_usb_midi *umidi,
					   struct snd_usb_midi_endpoint_info *ep_info,
					   struct snd_usb_midi_endpoint *rep)
{
	struct snd_usb_midi_out_endpoint *ep;
	unsigned int i;
	unsigned int pipe;
	void *buffer;
	int err;

	rep->out = NULL;
	ep = kzalloc(sizeof(*ep), GFP_KERNEL);
	if (!ep)
		return -ENOMEM;
	ep->umidi = umidi;

	for (i = 0; i < OUTPUT_URBS; ++i) {
		ep->urbs[i].urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!ep->urbs[i].urb) {
			err = -ENOMEM;
			goto error;
		}
		ep->urbs[i].ep = ep;
	}
	if (ep_info->out_interval)
		pipe = usb_sndintpipe(umidi->dev, ep_info->out_ep);
	else
		pipe = usb_sndbulkpipe(umidi->dev, ep_info->out_ep);
	switch (umidi->usb_id) {
	default:
		ep->max_transfer = usb_maxpacket(umidi->dev, pipe);
		break;
		/*
		 * Various chips declare a packet size larger than 4 bytes, but
		 * do not actually work with larger packets:
		 */
	case USB_ID(0x0a67, 0x5011): /* Medeli DD305 */
	case USB_ID(0x0a92, 0x1020): /* ESI M4U */
	case USB_ID(0x1430, 0x474b): /* RedOctane GH MIDI INTERFACE */
	case USB_ID(0x15ca, 0x0101): /* Textech USB Midi Cable */
	case USB_ID(0x15ca, 0x1806): /* Textech USB Midi Cable */
	case USB_ID(0x1a86, 0x752d): /* QinHeng CH345 "USB2.0-MIDI" */
	case USB_ID(0xfc08, 0x0101): /* Unknown vendor Cable */
		ep->max_transfer = 4;
		break;
		/*
		 * Some devices only work with 9 bytes packet size:
		 */
	case USB_ID(0x0644, 0x800e): /* Tascam US-122L */
	case USB_ID(0x0644, 0x800f): /* Tascam US-144 */
		ep->max_transfer = 9;
		break;
	}
	for (i = 0; i < OUTPUT_URBS; ++i) {
		buffer = usb_alloc_coherent(umidi->dev,
					    ep->max_transfer, GFP_KERNEL,
					    &ep->urbs[i].urb->transfer_dma);
		if (!buffer) {
			err = -ENOMEM;
			goto error;
		}
		if (ep_info->out_interval)
			usb_fill_int_urb(ep->urbs[i].urb, umidi->dev,
					 pipe, buffer, ep->max_transfer,
					 snd_usbmidi_out_urb_complete,
					 &ep->urbs[i], ep_info->out_interval);
		else
			usb_fill_bulk_urb(ep->urbs[i].urb, umidi->dev,
					  pipe, buffer, ep->max_transfer,
					  snd_usbmidi_out_urb_complete,
					  &ep->urbs[i]);
		err = usb_urb_ep_type_check(ep->urbs[i].urb);
		if (err < 0) {
			dev_err(&umidi->dev->dev, "invalid MIDI out EP %x\n",
				ep_info->out_ep);
			goto error;
		}
		ep->urbs[i].urb->transfer_flags = URB_NO_TRANSFER_DMA_MAP;
	}

	spin_lock_init(&ep->buffer_lock);
	INIT_WORK(&ep->work, snd_usbmidi_out_work);
	init_waitqueue_head(&ep->drain_wait);

	for (i = 0; i < 0x10; ++i)
		if (ep_info->out_cables & (1 << i)) {
			ep->ports[i].ep = ep;
			ep->ports[i].cable = i << 4;
		}

	if (umidi->usb_protocol_ops->init_out_endpoint)
		umidi->usb_protocol_ops->init_out_endpoint(ep);

	rep->out = ep;
	return 0;

 error:
	snd_usbmidi_out_endpoint_delete(ep);
	return err;
}

/*
 * Frees everything.
 */
static void snd_usbmidi_free(struct snd_usb_midi *umidi)
{
	int i;

	for (i = 0; i < MIDI_MAX_ENDPOINTS; ++i) {
		struct snd_usb_midi_endpoint *ep = &umidi->endpoints[i];
		if (ep->out)
			snd_usbmidi_out_endpoint_delete(ep->out);
		if (ep->in)
			snd_usbmidi_in_endpoint_delete(ep->in);
	}
	mutex_destroy(&umidi->mutex);
	kfree(umidi);
}

/*
 * Unlinks all URBs (must be done before the usb_device is deleted).
 */
void snd_usbmidi_disconnect(struct list_head *p)
{
	struct snd_usb_midi *umidi;
	unsigned int i, j;

	umidi = list_entry(p, struct snd_usb_midi, list);
	/*
	 * an URB's completion handler may start the timer and
	 * a timer may submit an URB. To reliably break the cycle
	 * a flag under lock must be used
	 */
	down_write(&umidi->disc_rwsem);
	spin_lock_irq(&umidi->disc_lock);
	umidi->disconnected = 1;
	spin_unlock_irq(&umidi->disc_lock);
	up_write(&umidi->disc_rwsem);

	del_timer_sync(&umidi->error_timer);

	for (i = 0; i < MIDI_MAX_ENDPOINTS; ++i) {
		struct snd_usb_midi_endpoint *ep = &umidi->endpoints[i];
		if (ep->out)
			cancel_work_sync(&ep->out->work);
		if (ep->out) {
			for (j = 0; j < OUTPUT_URBS; ++j)
				usb_kill_urb(ep->out->urbs[j].urb);
			if (umidi->usb_protocol_ops->finish_out_endpoint)
				umidi->usb_protocol_ops->finish_out_endpoint(ep->out);
			ep->out->active_urbs = 0;
			if (ep->out->drain_urbs) {
				ep->out->drain_urbs = 0;
				wake_up(&ep->out->drain_wait);
			}
		}
		if (ep->in)
			for (j = 0; j < INPUT_URBS; ++j)
				usb_kill_urb(ep->in->urbs[j]);
		/* free endpoints here; later call can result in Oops */
		if (ep->out)
			snd_usbmidi_out_endpoint_clear(ep->out);
		if (ep->in) {
			snd_usbmidi_in_endpoint_delete(ep->in);
			ep->in = NULL;
		}
	}
}
EXPORT_SYMBOL(snd_usbmidi_disconnect);

static void snd_usbmidi_rawmidi_free(struct snd_rawmidi *rmidi)
{
	struct snd_usb_midi *umidi = rmidi->private_data;
	snd_usbmidi_free(umidi);
}

static struct snd_rawmidi_substream *snd_usbmidi_find_substream(struct snd_usb_midi *umidi,
								int stream,
								int number)
{
	struct snd_rawmidi_substream *substream;

	list_for_each_entry(substream, &umidi->rmidi->streams[stream].substreams,
			    list) {
		if (substream->number == number)
			return substream;
	}
	return NULL;
}

/*
 * This list specifies names for ports that do not fit into the standard
 * "(product) MIDI (n)" schema because they aren't external MIDI ports,
 * such as internal control or synthesizer ports.
 */
static struct port_info {
	u32 id;
	short int port;
	short int voices;
	const char *name;
	unsigned int seq_flags;
} snd_usbmidi_port_info[] = {
#define PORT_INFO(vendor, product, num, name_, voices_, flags) \
	{ .id = USB_ID(vendor, product), \
	  .port = num, .voices = voices_, \
	  .name = name_, .seq_flags = flags }
#define EXTERNAL_PORT(vendor, product, num, name) \
	PORT_INFO(vendor, product, num, name, 0, \
		  SNDRV_SEQ_PORT_TYPE_MIDI_GENERIC | \
		  SNDRV_SEQ_PORT_TYPE_HARDWARE | \
		  SNDRV_SEQ_PORT_TYPE_PORT)
#define CONTROL_PORT(vendor, product, num, name) \
	PORT_INFO(vendor, product, num, name, 0, \
		  SNDRV_SEQ_PORT_TYPE_MIDI_GENERIC | \
		  SNDRV_SEQ_PORT_TYPE_HARDWARE)
#define GM_SYNTH_PORT(vendor, product, num, name, voices) \
	PORT_INFO(vendor, product, num, name, voices, \
		  SNDRV_SEQ_PORT_TYPE_MIDI_GENERIC | \
		  SNDRV_SEQ_PORT_TYPE_MIDI_GM | \
		  SNDRV_SEQ_PORT_TYPE_HARDWARE | \
		  SNDRV_SEQ_PORT_TYPE_SYNTHESIZER)
#define ROLAND_SYNTH_PORT(vendor, product, num, name, voices) \
	PORT_INFO(vendor, product, num, name, voices, \
		  SNDRV_SEQ_PORT_TYPE_MIDI_GENERIC | \
		  SNDRV_SEQ_PORT_TYPE_MIDI_GM | \
		  SNDRV_SEQ_PORT_TYPE_MIDI_GM2 | \
		  SNDRV_SEQ_PORT_TYPE_MIDI_GS | \
		  SNDRV_SEQ_PORT_TYPE_MIDI_XG | \
		  SNDRV_SEQ_PORT_TYPE_HARDWARE | \
		  SNDRV_SEQ_PORT_TYPE_SYNTHESIZER)
#define SOUNDCANVAS_PORT(vendor, product, num, name, voices) \
	PORT_INFO(vendor, product, num, name, voices, \
		  SNDRV_SEQ_PORT_TYPE_MIDI_GENERIC | \
		  SNDRV_SEQ_PORT_TYPE_MIDI_GM | \
		  SNDRV_SEQ_PORT_TYPE_MIDI_GM2 | \
		  SNDRV_SEQ_PORT_TYPE_MIDI_GS | \
		  SNDRV_SEQ_PORT_TYPE_MIDI_XG | \
		  SNDRV_SEQ_PORT_TYPE_MIDI_MT32 | \
		  SNDRV_SEQ_PORT_TYPE_HARDWARE | \
		  SNDRV_SEQ_PORT_TYPE_SYNTHESIZER)
	/* Yamaha MOTIF XF */
	GM_SYNTH_PORT(0x0499, 0x105c, 0, "%s Tone Generator", 128),
	CONTROL_PORT(0x0499, 0x105c, 1, "%s Remote Control"),
	EXTERNAL_PORT(0x0499, 0x105c, 2, "%s Thru"),
	CONTROL_PORT(0x0499, 0x105c, 3, "%s Editor"),
	/* Roland UA-100 */
	CONTROL_PORT(0x0582, 0x0000, 2, "%s Control"),
	/* Roland SC-8850 */
	SOUNDCANVAS_PORT(0x0582, 0x0003, 0, "%s Part A", 128),
	SOUNDCANVAS_PORT(0x0582, 0x0003, 1, "%s Part B", 128),
	SOUNDCANVAS_PORT(0x0582, 0x0003, 2, "%s Part C", 128),
	SOUNDCANVAS_PORT(0x0582, 0x0003, 3, "%s Part D", 128),
	EXTERNAL_PORT(0x0582, 0x0003, 4, "%s MIDI 1"),
	EXTERNAL_PORT(0x0582, 0x0003, 5, "%s MIDI 2"),
	/* Roland U-8 */
	EXTERNAL_PORT(0x0582, 0x0004, 0, "%s MIDI"),
	CONTROL_PORT(0x0582, 0x0004, 1, "%s Control"),
	/* Roland SC-8820 */
	SOUNDCANVAS_PORT(0x0582, 0x0007, 0, "%s Part A", 64),
	SOUNDCANVAS_PORT(0x0582, 0x0007, 1, "%s Part B", 64),
	EXTERNAL_PORT(0x0582, 0x0007, 2, "%s MIDI"),
	/* Roland SK-500 */
	SOUNDCANVAS_PORT(0x0582, 0x000b, 0, "%s Part A", 64),
	SOUNDCANVAS_PORT(0x0582, 0x000b, 1, "%s Part B", 64),
	EXTERNAL_PORT(0x0582, 0x000b, 2, "%s MIDI"),
	/* Roland SC-D70 */
	SOUNDCANVAS_PORT(0x0582, 0x000c, 0, "%s Part A", 64),
	SOUNDCANVAS_PORT(0x0582, 0x000c, 1, "%s Part B", 64),
	EXTERNAL_PORT(0x0582, 0x000c, 2, "%s MIDI"),
	/* Edirol UM-880 */
	CONTROL_PORT(0x0582, 0x0014, 8, "%s Control"),
	/* Edirol SD-90 */
	ROLAND_SYNTH_PORT(0x0582, 0x0016, 0, "%s Part A", 128),
	ROLAND_SYNTH_PORT(0x0582, 0x0016, 1, "%s Part B", 128),
	EXTERNAL_PORT(0x0582, 0x0016, 2, "%s MIDI 1"),
	EXTERNAL_PORT(0x0582, 0x0016, 3, "%s MIDI 2"),
	/* Edirol UM-550 */
	CONTROL_PORT(0x0582, 0x0023, 5, "%s Control"),
	/* Edirol SD-20 */
	ROLAND_SYNTH_PORT(0x0582, 0x0027, 0, "%s Part A", 64),
	ROLAND_SYNTH_PORT(0x0582, 0x0027, 1, "%s Part B", 64),
	EXTERNAL_PORT(0x0582, 0x0027, 2, "%s MIDI"),
	/* Edirol SD-80 */
	ROLAND_SYNTH_PORT(0x0582, 0x0029, 0, "%s Part A", 128),
	ROLAND_SYNTH_PORT(0x0582, 0x0029, 1, "%s Part B", 128),
	EXTERNAL_PORT(0x0582, 0x0029, 2, "%s MIDI 1"),
	EXTERNAL_PORT(0x0582, 0x0029, 3, "%s MIDI 2"),
	/* Edirol UA-700 */
	EXTERNAL_PORT(0x0582, 0x002b, 0, "%s MIDI"),
	CONTROL_PORT(0x0582, 0x002b, 1, "%s Control"),
	/* Roland VariOS */
	EXTERNAL_PORT(0x0582, 0x002f, 0, "%s MIDI"),
	EXTERNAL_PORT(0x0582, 0x002f, 1, "%s External MIDI"),
	EXTERNAL_PORT(0x0582, 0x002f, 2, "%s Sync"),
	/* Edirol PCR */
	EXTERNAL_PORT(0x0582, 0x0033, 0, "%s MIDI"),
	EXTERNAL_PORT(0x0582, 0x0033, 1, "%s 1"),
	EXTERNAL_PORT(0x0582, 0x0033, 2, "%s 2"),
	/* BOSS GS-10 */
	EXTERNAL_PORT(0x0582, 0x003b, 0, "%s MIDI"),
	CONTROL_PORT(0x0582, 0x003b, 1, "%s Control"),
	/* Edirol UA-1000 */
	EXTERNAL_PORT(0x0582, 0x0044, 0, "%s MIDI"),
	CONTROL_PORT(0x0582, 0x0044, 1, "%s Control"),
	/* Edirol UR-80 */
	EXTERNAL_PORT(0x0582, 0x0048, 0, "%s MIDI"),
	EXTERNAL_PORT(0x0582, 0x0048, 1, "%s 1"),
	EXTERNAL_PORT(0x0582, 0x0048, 2, "%s 2"),
	/* Edirol PCR-A */
	EXTERNAL_PORT(0x0582, 0x004d, 0, "%s MIDI"),
	EXTERNAL_PORT(0x0582, 0x004d, 1, "%s 1"),
	EXTERNAL_PORT(0x0582, 0x004d, 2, "%s 2"),
	/* BOSS GT-PRO */
	CONTROL_PORT(0x0582, 0x0089, 0, "%s Control"),
	/* Edirol UM-3EX */
	CONTROL_PORT(0x0582, 0x009a, 3, "%s Control"),
	/* Roland VG-99 */
	CONTROL_PORT(0x0582, 0x00b2, 0, "%s Control"),
	EXTERNAL_PORT(0x0582, 0x00b2, 1, "%s MIDI"),
	/* Cakewalk Sonar V-Studio 100 */
	EXTERNAL_PORT(0x0582, 0x00eb, 0, "%s MIDI"),
	CONTROL_PORT(0x0582, 0x00eb, 1, "%s Control"),
	/* Roland VB-99 */
	CONTROL_PORT(0x0582, 0x0102, 0, "%s Control"),
	EXTERNAL_PORT(0x0582, 0x0102, 1, "%s MIDI"),
	/* Roland A-PRO */
	EXTERNAL_PORT(0x0582, 0x010f, 0, "%s MIDI"),
	CONTROL_PORT(0x0582, 0x010f, 1, "%s 1"),
	CONTROL_PORT(0x0582, 0x010f, 2, "%s 2"),
	/* Roland SD-50 */
	ROLAND_SYNTH_PORT(0x0582, 0x0114, 0, "%s Synth", 128),
	EXTERNAL_PORT(0x0582, 0x0114, 1, "%s MIDI"),
	CONTROL_PORT(0x0582, 0x0114, 2, "%s Control"),
	/* Roland OCTA-CAPTURE */
	EXTERNAL_PORT(0x0582, 0x0120, 0, "%s MIDI"),
	CONTROL_PORT(0x0582, 0x0120, 1, "%s Control"),
	EXTERNAL_PORT(0x0582, 0x0121, 0, "%s MIDI"),
	CONTROL_PORT(0x0582, 0x0121, 1, "%s Control"),
	/* Roland SPD-SX */
	CONTROL_PORT(0x0582, 0x0145, 0, "%s Control"),
	EXTERNAL_PORT(0x0582, 0x0145, 1, "%s MIDI"),
	/* Roland A-Series */
	CONTROL_PORT(0x0582, 0x0156, 0, "%s Keyboard"),
	EXTERNAL_PORT(0x0582, 0x0156, 1, "%s MIDI"),
	/* Roland INTEGRA-7 */
	ROLAND_SYNTH_PORT(0x0582, 0x015b, 0, "%s Synth", 128),
	CONTROL_PORT(0x0582, 0x015b, 1, "%s Control"),
	/* M-Audio MidiSport 8x8 */
	CONTROL_PORT(0x0763, 0x1031, 8, "%s Control"),
	CONTROL_PORT(0x0763, 0x1033, 8, "%s Control"),
	/* MOTU Fastlane */
	EXTERNAL_PORT(0x07fd, 0x0001, 0, "%s MIDI A"),
	EXTERNAL_PORT(0x07fd, 0x0001, 1, "%s MIDI B"),
	/* Emagic Unitor8/AMT8/MT4 */
	EXTERNAL_PORT(0x086a, 0x0001, 8, "%s Broadcast"),
	EXTERNAL_PORT(0x086a, 0x0002, 8, "%s Broadcast"),
	EXTERNAL_PORT(0x086a, 0x0003, 4, "%s Broadcast"),
	/* Akai MPD16 */
	CONTROL_PORT(0x09e8, 0x0062, 0, "%s Control"),
	PORT_INFO(0x09e8, 0x0062, 1, "%s MIDI", 0,
		SNDRV_SEQ_PORT_TYPE_MIDI_GENERIC |
		SNDRV_SEQ_PORT_TYPE_HARDWARE),
	/* Access Music Virus TI */
	EXTERNAL_PORT(0x133e, 0x0815, 0, "%s MIDI"),
	PORT_INFO(0x133e, 0x0815, 1, "%s Synth", 0,
		SNDRV_SEQ_PORT_TYPE_MIDI_GENERIC |
		SNDRV_SEQ_PORT_TYPE_HARDWARE |
		SNDRV_SEQ_PORT_TYPE_SYNTHESIZER),
};

static struct port_info *find_port_info(struct snd_usb_midi *umidi, int number)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(snd_usbmidi_port_info); ++i) {
		if (snd_usbmidi_port_info[i].id == umidi->usb_id &&
		    snd_usbmidi_port_info[i].port == number)
			return &snd_usbmidi_port_info[i];
	}
	return NULL;
}

static void snd_usbmidi_get_port_info(struct snd_rawmidi *rmidi, int number,
				      struct snd_seq_port_info *seq_port_info)
{
	struct snd_usb_midi *umidi = rmidi->private_data;
	struct port_info *port_info;

	/* TODO: read port flags from descriptors */
	port_info = find_port_info(umidi, number);
	if (port_info) {
		seq_port_info->type = port_info->seq_flags;
		seq_port_info->midi_voices = port_info->voices;
	}
}

/* return iJack for the corresponding jackID */
static int find_usb_ijack(struct usb_host_interface *hostif, uint8_t jack_id)
{
	unsigned char *extra = hostif->extra;
	int extralen = hostif->extralen;
	struct usb_descriptor_header *h;
	struct usb_midi_out_jack_descriptor *outjd;
	struct usb_midi_in_jack_descriptor *injd;
	size_t sz;

	while (extralen > 4) {
		h = (struct usb_descriptor_header *)extra;
		if (h->bDescriptorType != USB_DT_CS_INTERFACE)
			goto next;

		outjd = (struct usb_midi_out_jack_descriptor *)h;
		if (h->bLength >= sizeof(*outjd) &&
		    outjd->bDescriptorSubtype == UAC_MIDI_OUT_JACK &&
		    outjd->bJackID == jack_id) {
			sz = USB_DT_MIDI_OUT_SIZE(outjd->bNrInputPins);
			if (outjd->bLength < sz)
				goto next;
			return *(extra + sz - 1);
		}

		injd = (struct usb_midi_in_jack_descriptor *)h;
		if (injd->bLength >= sizeof(*injd) &&
		    injd->bDescriptorSubtype == UAC_MIDI_IN_JACK &&
		    injd->bJackID == jack_id)
			return injd->iJack;

next:
		if (!extra[0])
			break;
		extralen -= extra[0];
		extra += extra[0];
	}
	return 0;
}

static void snd_usbmidi_init_substream(struct snd_usb_midi *umidi,
				       int stream, int number, int jack_id,
				       struct snd_rawmidi_substream **rsubstream)
{
	struct port_info *port_info;
	const char *name_format;
	struct usb_interface *intf;
	struct usb_host_interface *hostif;
	uint8_t jack_name_buf[32];
	uint8_t *default_jack_name = "MIDI";
	uint8_t *jack_name = default_jack_name;
	uint8_t iJack;
	int res;

	struct snd_rawmidi_substream *substream =
		snd_usbmidi_find_substream(umidi, stream, number);
	if (!substream) {
		dev_err(&umidi->dev->dev, "substream %d:%d not found\n", stream,
			number);
		return;
	}

	intf = umidi->iface;
	if (intf && jack_id >= 0) {
		hostif = intf->cur_altsetting;
		iJack = find_usb_ijack(hostif, jack_id);
		if (iJack != 0) {
			res = usb_string(umidi->dev, iJack, jack_name_buf,
			  ARRAY_SIZE(jack_name_buf));
			if (res)
				jack_name = jack_name_buf;
		}
	}

	port_info = find_port_info(umidi, number);
	name_format = port_info ? port_info->name :
		(jack_name != default_jack_name  ? "%s %s" : "%s %s %d");
	snprintf(substream->name, sizeof(substream->name),
		 name_format, umidi->card->shortname, jack_name, number + 1);

	*rsubstream = substream;
}

/*
 * Creates the endpoints and their ports.
 */
static int snd_usbmidi_create_endpoints(struct snd_usb_midi *umidi,
					struct snd_usb_midi_endpoint_info *endpoints)
{
	int i, j, err;
	int out_ports = 0, in_ports = 0;

	for (i = 0; i < MIDI_MAX_ENDPOINTS; ++i) {
		if (endpoints[i].out_cables) {
			err = snd_usbmidi_out_endpoint_create(umidi,
							      &endpoints[i],
							      &umidi->endpoints[i]);
			if (err < 0)
				return err;
		}
		if (endpoints[i].in_cables) {
			err = snd_usbmidi_in_endpoint_create(umidi,
							     &endpoints[i],
							     &umidi->endpoints[i]);
			if (err < 0)
				return err;
		}

		for (j = 0; j < 0x10; ++j) {
			if (endpoints[i].out_cables & (1 << j)) {
				snd_usbmidi_init_substream(umidi,
							   SNDRV_RAWMIDI_STREAM_OUTPUT,
							   out_ports,
							   endpoints[i].assoc_out_jacks[j],
							   &umidi->endpoints[i].out->ports[j].substream);
				++out_ports;
			}
			if (endpoints[i].in_cables & (1 << j)) {
				snd_usbmidi_init_substream(umidi,
							   SNDRV_RAWMIDI_STREAM_INPUT,
							   in_ports,
							   endpoints[i].assoc_in_jacks[j],
							   &umidi->endpoints[i].in->ports[j].substream);
				++in_ports;
			}
		}
	}
	dev_dbg(&umidi->dev->dev, "created %d output and %d input ports\n",
		    out_ports, in_ports);
	return 0;
}

static struct usb_ms_endpoint_descriptor *find_usb_ms_endpoint_descriptor(
					struct usb_host_endpoint *hostep)
{
	unsigned char *extra = hostep->extra;
	int extralen = hostep->extralen;

	while (extralen > 3) {
		struct usb_ms_endpoint_descriptor *ms_ep =
				(struct usb_ms_endpoint_descriptor *)extra;

		if (ms_ep->bLength > 3 &&
		    ms_ep->bDescriptorType == USB_DT_CS_ENDPOINT &&
		    ms_ep->bDescriptorSubtype == UAC_MS_GENERAL)
			return ms_ep;
		if (!extra[0])
			break;
		extralen -= extra[0];
		extra += extra[0];
	}
	return NULL;
}

/*
 * Returns MIDIStreaming device capabilities.
 */
static int snd_usbmidi_get_ms_info(struct snd_usb_midi *umidi,
				   struct snd_usb_midi_endpoint_info *endpoints)
{
	struct usb_interface *intf;
	struct usb_host_interface *hostif;
	struct usb_interface_descriptor *intfd;
	struct usb_ms_header_descriptor *ms_header;
	struct usb_host_endpoint *hostep;
	struct usb_endpoint_descriptor *ep;
	struct usb_ms_endpoint_descriptor *ms_ep;
	int i, j, epidx;

	intf = umidi->iface;
	if (!intf)
		return -ENXIO;
	hostif = &intf->altsetting[0];
	intfd = get_iface_desc(hostif);
	ms_header = (struct usb_ms_header_descriptor *)hostif->extra;
	if (hostif->extralen >= 7 &&
	    ms_header->bLength >= 7 &&
	    ms_header->bDescriptorType == USB_DT_CS_INTERFACE &&
	    ms_header->bDescriptorSubtype == UAC_HEADER)
		dev_dbg(&umidi->dev->dev, "MIDIStreaming version %02x.%02x\n",
			    ((uint8_t *)&ms_header->bcdMSC)[1], ((uint8_t *)&ms_header->bcdMSC)[0]);
	else
		dev_warn(&umidi->dev->dev,
			 "MIDIStreaming interface descriptor not found\n");

	epidx = 0;
	for (i = 0; i < intfd->bNumEndpoints; ++i) {
		hostep = &hostif->endpoint[i];
		ep = get_ep_desc(hostep);
		if (!usb_endpoint_xfer_bulk(ep) && !usb_endpoint_xfer_int(ep))
			continue;
		ms_ep = find_usb_ms_endpoint_descriptor(hostep);
		if (!ms_ep)
			continue;
		if (ms_ep->bLength <= sizeof(*ms_ep))
			continue;
		if (ms_ep->bNumEmbMIDIJack > 0x10)
			continue;
		if (ms_ep->bLength < sizeof(*ms_ep) + ms_ep->bNumEmbMIDIJack)
			continue;
		if (usb_endpoint_dir_out(ep)) {
			if (endpoints[epidx].out_ep) {
				if (++epidx >= MIDI_MAX_ENDPOINTS) {
					dev_warn(&umidi->dev->dev,
						 "too many endpoints\n");
					break;
				}
			}
			endpoints[epidx].out_ep = usb_endpoint_num(ep);
			if (usb_endpoint_xfer_int(ep))
				endpoints[epidx].out_interval = ep->bInterval;
			else if (snd_usb_get_speed(umidi->dev) == USB_SPEED_LOW)
				/*
				 * Low speed bulk transfers don't exist, so
				 * force interrupt transfers for devices like
				 * ESI MIDI Mate that try to use them anyway.
				 */
				endpoints[epidx].out_interval = 1;
			endpoints[epidx].out_cables =
				(1 << ms_ep->bNumEmbMIDIJack) - 1;
			for (j = 0; j < ms_ep->bNumEmbMIDIJack; ++j)
				endpoints[epidx].assoc_out_jacks[j] = ms_ep->baAssocJackID[j];
			for (; j < ARRAY_SIZE(endpoints[epidx].assoc_out_jacks); ++j)
				endpoints[epidx].assoc_out_jacks[j] = -1;
			dev_dbg(&umidi->dev->dev, "EP %02X: %d jack(s)\n",
				ep->bEndpointAddress, ms_ep->bNumEmbMIDIJack);
		} else {
			if (endpoints[epidx].in_ep) {
				if (++epidx >= MIDI_MAX_ENDPOINTS) {
					dev_warn(&umidi->dev->dev,
						 "too many endpoints\n");
					break;
				}
			}
			endpoints[epidx].in_ep = usb_endpoint_num(ep);
			if (usb_endpoint_xfer_int(ep))
				endpoints[epidx].in_interval = ep->bInterval;
			else if (snd_usb_get_speed(umidi->dev) == USB_SPEED_LOW)
				endpoints[epidx].in_interval = 1;
			endpoints[epidx].in_cables =
				(1 << ms_ep->bNumEmbMIDIJack) - 1;
			for (j = 0; j < ms_ep->bNumEmbMIDIJack; ++j)
				endpoints[epidx].assoc_in_jacks[j] = ms_ep->baAssocJackID[j];
			for (; j < ARRAY_SIZE(endpoints[epidx].assoc_in_jacks); ++j)
				endpoints[epidx].assoc_in_jacks[j] = -1;
			dev_dbg(&umidi->dev->dev, "EP %02X: %d jack(s)\n",
				ep->bEndpointAddress, ms_ep->bNumEmbMIDIJack);
		}
	}
	return 0;
}

static int roland_load_info(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_info *info)
{
	static const char *const names[] = { "High Load", "Light Load" };

	return snd_ctl_enum_info(info, 1, 2, names);
}

static int roland_load_get(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *value)
{
	value->value.enumerated.item[0] = kcontrol->private_value;
	return 0;
}

static int roland_load_put(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *value)
{
	struct snd_usb_midi *umidi = kcontrol->private_data;
	int changed;

	if (value->value.enumerated.item[0] > 1)
		return -EINVAL;
	mutex_lock(&umidi->mutex);
	changed = value->value.enumerated.item[0] != kcontrol->private_value;
	if (changed)
		kcontrol->private_value = value->value.enumerated.item[0];
	mutex_unlock(&umidi->mutex);
	return changed;
}

static const struct snd_kcontrol_new roland_load_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "MIDI Input Mode",
	.info = roland_load_info,
	.get = roland_load_get,
	.put = roland_load_put,
	.private_value = 1,
};

/*
 * On Roland devices, use the second alternate setting to be able to use
 * the interrupt input endpoint.
 */
static void snd_usbmidi_switch_roland_altsetting(struct snd_usb_midi *umidi)
{
	struct usb_interface *intf;
	struct usb_host_interface *hostif;
	struct usb_interface_descriptor *intfd;

	intf = umidi->iface;
	if (!intf || intf->num_altsetting != 2)
		return;

	hostif = &intf->altsetting[1];
	intfd = get_iface_desc(hostif);
       /* If either or both of the endpoints support interrupt transfer,
        * then use the alternate setting
        */
	if (intfd->bNumEndpoints != 2 ||
	    !((get_endpoint(hostif, 0)->bmAttributes &
	       USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_INT ||
	      (get_endpoint(hostif, 1)->bmAttributes &
	       USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_INT))
		return;

	dev_dbg(&umidi->dev->dev, "switching to altsetting %d with int ep\n",
		    intfd->bAlternateSetting);
	usb_set_interface(umidi->dev, intfd->bInterfaceNumber,
			  intfd->bAlternateSetting);

	umidi->roland_load_ctl = snd_ctl_new1(&roland_load_ctl, umidi);
	if (snd_ctl_add(umidi->card, umidi->roland_load_ctl) < 0)
		umidi->roland_load_ctl = NULL;
}

/*
 * Try to find any usable endpoints in the interface.
 */
static int snd_usbmidi_detect_endpoints(struct snd_usb_midi *umidi,
					struct snd_usb_midi_endpoint_info *endpoint,
					int max_endpoints)
{
	struct usb_interface *intf;
	struct usb_host_interface *hostif;
	struct usb_interface_descriptor *intfd;
	struct usb_endpoint_descriptor *epd;
	int i, out_eps = 0, in_eps = 0;

	if (USB_ID_VENDOR(umidi->usb_id) == 0x0582)
		snd_usbmidi_switch_roland_altsetting(umidi);

	if (endpoint[0].out_ep || endpoint[0].in_ep)
		return 0;

	intf = umidi->iface;
	if (!intf || intf->num_altsetting < 1)
		return -ENOENT;
	hostif = intf->cur_altsetting;
	intfd = get_iface_desc(hostif);

	for (i = 0; i < intfd->bNumEndpoints; ++i) {
		epd = get_endpoint(hostif, i);
		if (!usb_endpoint_xfer_bulk(epd) &&
		    !usb_endpoint_xfer_int(epd))
			continue;
		if (out_eps < max_endpoints &&
		    usb_endpoint_dir_out(epd)) {
			endpoint[out_eps].out_ep = usb_endpoint_num(epd);
			if (usb_endpoint_xfer_int(epd))
				endpoint[out_eps].out_interval = epd->bInterval;
			++out_eps;
		}
		if (in_eps < max_endpoints &&
		    usb_endpoint_dir_in(epd)) {
			endpoint[in_eps].in_ep = usb_endpoint_num(epd);
			if (usb_endpoint_xfer_int(epd))
				endpoint[in_eps].in_interval = epd->bInterval;
			++in_eps;
		}
	}
	return (out_eps || in_eps) ? 0 : -ENOENT;
}

/*
 * Detects the endpoints for one-port-per-endpoint protocols.
 */
static int snd_usbmidi_detect_per_port_endpoints(struct snd_usb_midi *umidi,
						 struct snd_usb_midi_endpoint_info *endpoints)
{
	int err, i;

	err = snd_usbmidi_detect_endpoints(umidi, endpoints, MIDI_MAX_ENDPOINTS);
	for (i = 0; i < MIDI_MAX_ENDPOINTS; ++i) {
		if (endpoints[i].out_ep)
			endpoints[i].out_cables = 0x0001;
		if (endpoints[i].in_ep)
			endpoints[i].in_cables = 0x0001;
	}
	return err;
}

/*
 * Detects the endpoints and ports of Yamaha devices.
 */
static int snd_usbmidi_detect_yamaha(struct snd_usb_midi *umidi,
				     struct snd_usb_midi_endpoint_info *endpoint)
{
	struct usb_interface *intf;
	struct usb_host_interface *hostif;
	struct usb_interface_descriptor *intfd;
	uint8_t *cs_desc;

	intf = umidi->iface;
	if (!intf)
		return -ENOENT;
	hostif = intf->altsetting;
	intfd = get_iface_desc(hostif);
	if (intfd->bNumEndpoints < 1)
		return -ENOENT;

	/*
	 * For each port there is one MIDI_IN/OUT_JACK descriptor, not
	 * necessarily with any useful contents.  So simply count 'em.
	 */
	for (cs_desc = hostif->extra;
	     cs_desc < hostif->extra + hostif->extralen && cs_desc[0] >= 2;
	     cs_desc += cs_desc[0]) {
		if (cs_desc[1] == USB_DT_CS_INTERFACE) {
			if (cs_desc[2] == UAC_MIDI_IN_JACK)
				endpoint->in_cables =
					(endpoint->in_cables << 1) | 1;
			else if (cs_desc[2] == UAC_MIDI_OUT_JACK)
				endpoint->out_cables =
					(endpoint->out_cables << 1) | 1;
		}
	}
	if (!endpoint->in_cables && !endpoint->out_cables)
		return -ENOENT;

	return snd_usbmidi_detect_endpoints(umidi, endpoint, 1);
}

/*
 * Detects the endpoints and ports of Roland devices.
 */
static int snd_usbmidi_detect_roland(struct snd_usb_midi *umidi,
				     struct snd_usb_midi_endpoint_info *endpoint)
{
	struct usb_interface *intf;
	struct usb_host_interface *hostif;
	u8 *cs_desc;

	intf = umidi->iface;
	if (!intf)
		return -ENOENT;
	hostif = intf->altsetting;
	/*
	 * Some devices have a descriptor <06 24 F1 02 <inputs> <outputs>>,
	 * some have standard class descriptors, or both kinds, or neither.
	 */
	for (cs_desc = hostif->extra;
	     cs_desc < hostif->extra + hostif->extralen && cs_desc[0] >= 2;
	     cs_desc += cs_desc[0]) {
		if (cs_desc[0] >= 6 &&
		    cs_desc[1] == USB_DT_CS_INTERFACE &&
		    cs_desc[2] == 0xf1 &&
		    cs_desc[3] == 0x02) {
			if (cs_desc[4] > 0x10 || cs_desc[5] > 0x10)
				continue;
			endpoint->in_cables  = (1 << cs_desc[4]) - 1;
			endpoint->out_cables = (1 << cs_desc[5]) - 1;
			return snd_usbmidi_detect_endpoints(umidi, endpoint, 1);
		} else if (cs_desc[0] >= 7 &&
			   cs_desc[1] == USB_DT_CS_INTERFACE &&
			   cs_desc[2] == UAC_HEADER) {
			return snd_usbmidi_get_ms_info(umidi, endpoint);
		}
	}

	return -ENODEV;
}

/*
 * Creates the endpoints and their ports for Midiman devices.
 */
static int snd_usbmidi_create_endpoints_midiman(struct snd_usb_midi *umidi,
						struct snd_usb_midi_endpoint_info *endpoint)
{
	struct snd_usb_midi_endpoint_info ep_info;
	struct usb_interface *intf;
	struct usb_host_interface *hostif;
	struct usb_interface_descriptor *intfd;
	struct usb_endpoint_descriptor *epd;
	int cable, err;

	intf = umidi->iface;
	if (!intf)
		return -ENOENT;
	hostif = intf->altsetting;
	intfd = get_iface_desc(hostif);
	/*
	 * The various MidiSport devices have more or less random endpoint
	 * numbers, so we have to identify the endpoints by their index in
	 * the descriptor array, like the driver for that other OS does.
	 *
	 * There is one interrupt input endpoint for all input ports, one
	 * bulk output endpoint for even-numbered ports, and one for odd-
	 * numbered ports.  Both bulk output endpoints have corresponding
	 * input bulk endpoints (at indices 1 and 3) which aren't used.
	 */
	if (intfd->bNumEndpoints < (endpoint->out_cables > 0x0001 ? 5 : 3)) {
		dev_dbg(&umidi->dev->dev, "not enough endpoints\n");
		return -ENOENT;
	}

	epd = get_endpoint(hostif, 0);
	if (!usb_endpoint_dir_in(epd) || !usb_endpoint_xfer_int(epd)) {
		dev_dbg(&umidi->dev->dev, "endpoint[0] isn't interrupt\n");
		return -ENXIO;
	}
	epd = get_endpoint(hostif, 2);
	if (!usb_endpoint_dir_out(epd) || !usb_endpoint_xfer_bulk(epd)) {
		dev_dbg(&umidi->dev->dev, "endpoint[2] isn't bulk output\n");
		return -ENXIO;
	}
	if (endpoint->out_cables > 0x0001) {
		epd = get_endpoint(hostif, 4);
		if (!usb_endpoint_dir_out(epd) ||
		    !usb_endpoint_xfer_bulk(epd)) {
			dev_dbg(&umidi->dev->dev,
				"endpoint[4] isn't bulk output\n");
			return -ENXIO;
		}
	}

	ep_info.out_ep = get_endpoint(hostif, 2)->bEndpointAddress &
		USB_ENDPOINT_NUMBER_MASK;
	ep_info.out_interval = 0;
	ep_info.out_cables = endpoint->out_cables & 0x5555;
	err = snd_usbmidi_out_endpoint_create(umidi, &ep_info,
					      &umidi->endpoints[0]);
	if (err < 0)
		return err;

	ep_info.in_ep = get_endpoint(hostif, 0)->bEndpointAddress &
		USB_ENDPOINT_NUMBER_MASK;
	ep_info.in_interval = get_endpoint(hostif, 0)->bInterval;
	ep_info.in_cables = endpoint->in_cables;
	err = snd_usbmidi_in_endpoint_create(umidi, &ep_info,
					     &umidi->endpoints[0]);
	if (err < 0)
		return err;

	if (endpoint->out_cables > 0x0001) {
		ep_info.out_ep = get_endpoint(hostif, 4)->bEndpointAddress &
			USB_ENDPOINT_NUMBER_MASK;
		ep_info.out_cables = endpoint->out_cables & 0xaaaa;
		err = snd_usbmidi_out_endpoint_create(umidi, &ep_info,
						      &umidi->endpoints[1]);
		if (err < 0)
			return err;
	}

	for (cable = 0; cable < 0x10; ++cable) {
		if (endpoint->out_cables & (1 << cable))
			snd_usbmidi_init_substream(umidi,
						   SNDRV_RAWMIDI_STREAM_OUTPUT,
						   cable,
						   -1 /* prevent trying to find jack */,
						   &umidi->endpoints[cable & 1].out->ports[cable].substream);
		if (endpoint->in_cables & (1 << cable))
			snd_usbmidi_init_substream(umidi,
						   SNDRV_RAWMIDI_STREAM_INPUT,
						   cable,
						   -1 /* prevent trying to find jack */,
						   &umidi->endpoints[0].in->ports[cable].substream);
	}
	return 0;
}

static const struct snd_rawmidi_global_ops snd_usbmidi_ops = {
	.get_port_info = snd_usbmidi_get_port_info,
};

static int snd_usbmidi_create_rawmidi(struct snd_usb_midi *umidi,
				      int out_ports, int in_ports)
{
	struct snd_rawmidi *rmidi;
	int err;

	err = snd_rawmidi_new(umidi->card, "USB MIDI",
			      umidi->next_midi_device++,
			      out_ports, in_ports, &rmidi);
	if (err < 0)
		return err;
	strcpy(rmidi->name, umidi->card->shortname);
	rmidi->info_flags = SNDRV_RAWMIDI_INFO_OUTPUT |
			    SNDRV_RAWMIDI_INFO_INPUT |
			    SNDRV_RAWMIDI_INFO_DUPLEX;
	rmidi->ops = &snd_usbmidi_ops;
	rmidi->private_data = umidi;
	rmidi->private_free = snd_usbmidi_rawmidi_free;
	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_OUTPUT,
			    &snd_usbmidi_output_ops);
	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_INPUT,
			    &snd_usbmidi_input_ops);

	umidi->rmidi = rmidi;
	return 0;
}

/*
 * Temporarily stop input.
 */
void snd_usbmidi_input_stop(struct list_head *p)
{
	struct snd_usb_midi *umidi;
	unsigned int i, j;

	umidi = list_entry(p, struct snd_usb_midi, list);
	if (!umidi->input_running)
		return;
	for (i = 0; i < MIDI_MAX_ENDPOINTS; ++i) {
		struct snd_usb_midi_endpoint *ep = &umidi->endpoints[i];
		if (ep->in)
			for (j = 0; j < INPUT_URBS; ++j)
				usb_kill_urb(ep->in->urbs[j]);
	}
	umidi->input_running = 0;
}
EXPORT_SYMBOL(snd_usbmidi_input_stop);

static void snd_usbmidi_input_start_ep(struct snd_usb_midi *umidi,
				       struct snd_usb_midi_in_endpoint *ep)
{
	unsigned int i;
	unsigned long flags;

	if (!ep)
		return;
	for (i = 0; i < INPUT_URBS; ++i) {
		struct urb *urb = ep->urbs[i];
		spin_lock_irqsave(&umidi->disc_lock, flags);
		if (!atomic_read(&urb->use_count)) {
			urb->dev = ep->umidi->dev;
			snd_usbmidi_submit_urb(urb, GFP_ATOMIC);
		}
		spin_unlock_irqrestore(&umidi->disc_lock, flags);
	}
}

/*
 * Resume input after a call to snd_usbmidi_input_stop().
 */
void snd_usbmidi_input_start(struct list_head *p)
{
	struct snd_usb_midi *umidi;
	int i;

	umidi = list_entry(p, struct snd_usb_midi, list);
	if (umidi->input_running || !umidi->opened[1])
		return;
	for (i = 0; i < MIDI_MAX_ENDPOINTS; ++i)
		snd_usbmidi_input_start_ep(umidi, umidi->endpoints[i].in);
	umidi->input_running = 1;
}
EXPORT_SYMBOL(snd_usbmidi_input_start);

/*
 * Prepare for suspend. Typically called from the USB suspend callback.
 */
void snd_usbmidi_suspend(struct list_head *p)
{
	struct snd_usb_midi *umidi;

	umidi = list_entry(p, struct snd_usb_midi, list);
	mutex_lock(&umidi->mutex);
	snd_usbmidi_input_stop(p);
	mutex_unlock(&umidi->mutex);
}
EXPORT_SYMBOL(snd_usbmidi_suspend);

/*
 * Resume. Typically called from the USB resume callback.
 */
void snd_usbmidi_resume(struct list_head *p)
{
	struct snd_usb_midi *umidi;

	umidi = list_entry(p, struct snd_usb_midi, list);
	mutex_lock(&umidi->mutex);
	snd_usbmidi_input_start(p);
	mutex_unlock(&umidi->mutex);
}
EXPORT_SYMBOL(snd_usbmidi_resume);

/*
 * Creates and registers everything needed for a MIDI streaming interface.
 */
int __snd_usbmidi_create(struct snd_card *card,
			 struct usb_interface *iface,
			 struct list_head *midi_list,
			 const struct snd_usb_audio_quirk *quirk,
			 unsigned int usb_id,
			 unsigned int *num_rawmidis)
{
	struct snd_usb_midi *umidi;
	struct snd_usb_midi_endpoint_info endpoints[MIDI_MAX_ENDPOINTS];
	int out_ports, in_ports;
	int i, err;

	umidi = kzalloc(sizeof(*umidi), GFP_KERNEL);
	if (!umidi)
		return -ENOMEM;
	umidi->dev = interface_to_usbdev(iface);
	umidi->card = card;
	umidi->iface = iface;
	umidi->quirk = quirk;
	umidi->usb_protocol_ops = &snd_usbmidi_standard_ops;
	if (num_rawmidis)
		umidi->next_midi_device = *num_rawmidis;
	spin_lock_init(&umidi->disc_lock);
	init_rwsem(&umidi->disc_rwsem);
	mutex_init(&umidi->mutex);
	if (!usb_id)
		usb_id = USB_ID(le16_to_cpu(umidi->dev->descriptor.idVendor),
			       le16_to_cpu(umidi->dev->descriptor.idProduct));
	umidi->usb_id = usb_id;
	timer_setup(&umidi->error_timer, snd_usbmidi_error_timer, 0);

	/* detect the endpoint(s) to use */
	memset(endpoints, 0, sizeof(endpoints));
	switch (quirk ? quirk->type : QUIRK_MIDI_STANDARD_INTERFACE) {
	case QUIRK_MIDI_STANDARD_INTERFACE:
		err = snd_usbmidi_get_ms_info(umidi, endpoints);
		if (umidi->usb_id == USB_ID(0x0763, 0x0150)) /* M-Audio Uno */
			umidi->usb_protocol_ops =
				&snd_usbmidi_maudio_broken_running_status_ops;
		break;
	case QUIRK_MIDI_US122L:
		umidi->usb_protocol_ops = &snd_usbmidi_122l_ops;
		fallthrough;
	case QUIRK_MIDI_FIXED_ENDPOINT:
		memcpy(&endpoints[0], quirk->data,
		       sizeof(struct snd_usb_midi_endpoint_info));
		err = snd_usbmidi_detect_endpoints(umidi, &endpoints[0], 1);
		break;
	case QUIRK_MIDI_YAMAHA:
		err = snd_usbmidi_detect_yamaha(umidi, &endpoints[0]);
		break;
	case QUIRK_MIDI_ROLAND:
		err = snd_usbmidi_detect_roland(umidi, &endpoints[0]);
		break;
	case QUIRK_MIDI_MIDIMAN:
		umidi->usb_protocol_ops = &snd_usbmidi_midiman_ops;
		memcpy(&endpoints[0], quirk->data,
		       sizeof(struct snd_usb_midi_endpoint_info));
		err = 0;
		break;
	case QUIRK_MIDI_NOVATION:
		umidi->usb_protocol_ops = &snd_usbmidi_novation_ops;
		err = snd_usbmidi_detect_per_port_endpoints(umidi, endpoints);
		break;
	case QUIRK_MIDI_RAW_BYTES:
		umidi->usb_protocol_ops = &snd_usbmidi_raw_ops;
		/*
		 * Interface 1 contains isochronous endpoints, but with the same
		 * numbers as in interface 0.  Since it is interface 1 that the
		 * USB core has most recently seen, these descriptors are now
		 * associated with the endpoint numbers.  This will foul up our
		 * attempts to submit bulk/interrupt URBs to the endpoints in
		 * interface 0, so we have to make sure that the USB core looks
		 * again at interface 0 by calling usb_set_interface() on it.
		 */
		if (umidi->usb_id == USB_ID(0x07fd, 0x0001)) /* MOTU Fastlane */
			usb_set_interface(umidi->dev, 0, 0);
		err = snd_usbmidi_detect_per_port_endpoints(umidi, endpoints);
		break;
	case QUIRK_MIDI_EMAGIC:
		umidi->usb_protocol_ops = &snd_usbmidi_emagic_ops;
		memcpy(&endpoints[0], quirk->data,
		       sizeof(struct snd_usb_midi_endpoint_info));
		err = snd_usbmidi_detect_endpoints(umidi, &endpoints[0], 1);
		break;
	case QUIRK_MIDI_CME:
		umidi->usb_protocol_ops = &snd_usbmidi_cme_ops;
		err = snd_usbmidi_detect_per_port_endpoints(umidi, endpoints);
		break;
	case QUIRK_MIDI_AKAI:
		umidi->usb_protocol_ops = &snd_usbmidi_akai_ops;
		err = snd_usbmidi_detect_per_port_endpoints(umidi, endpoints);
		/* endpoint 1 is input-only */
		endpoints[1].out_cables = 0;
		break;
	case QUIRK_MIDI_FTDI:
		umidi->usb_protocol_ops = &snd_usbmidi_ftdi_ops;

		/* set baud rate to 31250 (48 MHz / 16 / 96) */
		err = usb_control_msg(umidi->dev, usb_sndctrlpipe(umidi->dev, 0),
				      3, 0x40, 0x60, 0, NULL, 0, 1000);
		if (err < 0)
			break;

		err = snd_usbmidi_detect_per_port_endpoints(umidi, endpoints);
		break;
	case QUIRK_MIDI_CH345:
		umidi->usb_protocol_ops = &snd_usbmidi_ch345_broken_sysex_ops;
		err = snd_usbmidi_detect_per_port_endpoints(umidi, endpoints);
		break;
	default:
		dev_err(&umidi->dev->dev, "invalid quirk type %d\n",
			quirk->type);
		err = -ENXIO;
		break;
	}
	if (err < 0)
		goto free_midi;

	/* create rawmidi device */
	out_ports = 0;
	in_ports = 0;
	for (i = 0; i < MIDI_MAX_ENDPOINTS; ++i) {
		out_ports += hweight16(endpoints[i].out_cables);
		in_ports += hweight16(endpoints[i].in_cables);
	}
	err = snd_usbmidi_create_rawmidi(umidi, out_ports, in_ports);
	if (err < 0)
		goto free_midi;

	/* create endpoint/port structures */
	if (quirk && quirk->type == QUIRK_MIDI_MIDIMAN)
		err = snd_usbmidi_create_endpoints_midiman(umidi, &endpoints[0]);
	else
		err = snd_usbmidi_create_endpoints(umidi, endpoints);
	if (err < 0)
		goto exit;

	usb_autopm_get_interface_no_resume(umidi->iface);

	list_add_tail(&umidi->list, midi_list);
	if (num_rawmidis)
		*num_rawmidis = umidi->next_midi_device;
	return 0;

free_midi:
	kfree(umidi);
exit:
	return err;
}
EXPORT_SYMBOL(__snd_usbmidi_create);
