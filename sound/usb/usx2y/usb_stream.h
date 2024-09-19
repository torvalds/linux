/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __USB_STREAM_H
#define __USB_STREAM_H

#include <uapi/sound/usb_stream.h>

#define USB_STREAM_NURBS 4
#define USB_STREAM_URBDEPTH 4

struct usb_stream_kernel {
	struct usb_stream *s;
	struct usb_device *dev;

	void *write_page;

	unsigned int n_o_ps;

	struct urb *inurb[USB_STREAM_NURBS];
	struct urb *idle_inurb;
	struct urb *completed_inurb;
	struct urb *outurb[USB_STREAM_NURBS];
	struct urb *idle_outurb;
	struct urb *completed_outurb;
	struct urb *i_urb;

	int iso_frame_balance;

	wait_queue_head_t sleep;

	unsigned int out_phase;
	unsigned int out_phase_peeked;
	unsigned int freqn;
};

struct usb_stream *usb_stream_new(struct usb_stream_kernel *sk,
				  struct usb_device *dev,
				  unsigned int in_endpoint,
				  unsigned int out_endpoint,
				  unsigned int sample_rate,
				  unsigned int use_packsize,
				  unsigned int period_frames,
				  unsigned int frame_size);
void usb_stream_free(struct usb_stream_kernel *sk);
int usb_stream_start(struct usb_stream_kernel *sk);
void usb_stream_stop(struct usb_stream_kernel *sk);

#endif /* __USB_STREAM_H */
