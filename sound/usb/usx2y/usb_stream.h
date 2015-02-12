#ifndef __USB_STREAM_H
#define __USB_STREAM_H

#include <uapi/sound/usb_stream.h>

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

#endif /* __USB_STREAM_H */
