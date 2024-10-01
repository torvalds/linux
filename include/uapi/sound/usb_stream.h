/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * Copyright (C) 2007, 2008 Karsten Wiese <fzu@wemgehoertderstaat.de>
 */

#ifndef _UAPI__SOUND_USB_STREAM_H
#define _UAPI__SOUND_USB_STREAM_H

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
	struct usb_stream_packet inpacket[];
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

#endif /* _UAPI__SOUND_USB_STREAM_H */
