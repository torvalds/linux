/*
 * Copyright (C) 2007, 2008 Karsten Wiese <fzu@wemgehoertderstaat.de>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#define USB_STREAM_INTERFACE_VERSION 2

#define SNDRV_USB_STREAM_IOCTL_SET_PARAMS \
	_IOW('H', 0x90, struct usb_stream_config)

struct usb_stream_packet {
	unsigned offset;
	unsigned length;
};


struct usb_stream_config {
	unsigned version;
	unsigned sample_rate;
	unsigned period_frames;
	unsigned frame_size;
};

struct usb_stream {
	struct usb_stream_config cfg;
	unsigned read_size;
	unsigned write_size;

	int period_size;

	unsigned state;

	int idle_insize;
	int idle_outsize;
	int sync_packet;
	unsigned insize_done;
	unsigned periods_done;
	unsigned periods_polled;

	struct usb_stream_packet outpacket[2];
	unsigned		 inpackets;
	unsigned		 inpacket_head;
	unsigned		 inpacket_split;
	unsigned		 inpacket_split_at;
	unsigned		 next_inpacket_split;
	unsigned		 next_inpacket_split_at;
	struct usb_stream_packet inpacket[0];
};

enum usb_stream_state {
	usb_stream_invalid,
	usb_stream_stopped,
	usb_stream_sync0,
	usb_stream_sync1,
	usb_stream_ready,
	usb_stream_running,
	usb_stream_xrun,
};

#if __KERNEL__

#define USB_STREAM_NURBS 4
#define USB_STREAM_URBDEPTH 4

struct usb_stream_kernel {
	struct usb_stream *s;

	void *write_page;

	unsigned n_o_ps;

	struct urb *inurb[USB_STREAM_NURBS];
	struct urb *idle_inurb;
	struct urb *completed_inurb;
	struct urb *outurb[USB_STREAM_NURBS];
	struct urb *idle_outurb;
	struct urb *completed_outurb;
	struct urb *i_urb;

	int iso_frame_balance;

	wait_queue_head_t sleep;

	unsigned out_phase;
	unsigned out_phase_peeked;
	unsigned freqn;
};

struct usb_stream *usb_stream_new(struct usb_stream_kernel *sk,
				  struct usb_device *dev,
				  unsigned in_endpoint, unsigned out_endpoint,
				  unsigned sample_rate, unsigned use_packsize,
				  unsigned period_frames, unsigned frame_size);
void usb_stream_free(struct usb_stream_kernel *);
int usb_stream_start(struct usb_stream_kernel *);
void usb_stream_stop(struct usb_stream_kernel *);


#endif
