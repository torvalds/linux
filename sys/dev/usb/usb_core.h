/* $FreeBSD$ */
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Hans Petter Selasky. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Including this file is mandatory for all USB related c-files in the kernel.
 */

#ifndef _USB_CORE_H_
#define	_USB_CORE_H_

/*
 * The following macro will tell if an USB transfer is currently
 * receiving or transferring data.
 */
#define	USB_GET_DATA_ISREAD(xfer) ((xfer)->flags_int.usb_mode == \
	USB_MODE_DEVICE ? (((xfer)->endpointno & UE_DIR_IN) ? 0 : 1) : \
	(((xfer)->endpointno & UE_DIR_IN) ? 1 : 0))

/* locking wrappers for BUS lock */
#define	USB_BUS_LOCK(_b)		USB_MTX_LOCK(&(_b)->bus_mtx)
#define	USB_BUS_UNLOCK(_b)		USB_MTX_UNLOCK(&(_b)->bus_mtx)
#define	USB_BUS_LOCK_ASSERT(_b, _t)	USB_MTX_ASSERT(&(_b)->bus_mtx, _t)

/* locking wrappers for BUS spin lock */
#define	USB_BUS_SPIN_LOCK(_b)		USB_MTX_LOCK_SPIN(&(_b)->bus_spin_lock)
#define	USB_BUS_SPIN_UNLOCK(_b)		USB_MTX_UNLOCK_SPIN(&(_b)->bus_spin_lock)
#define	USB_BUS_SPIN_LOCK_ASSERT(_b, _t) USB_MTX_ASSERT(&(_b)->bus_spin_lock, _t)

/* locking wrappers for XFER lock */
#define	USB_XFER_LOCK(_x)		USB_MTX_LOCK((_x)->xroot->xfer_mtx)
#define	USB_XFER_UNLOCK(_x)		USB_MTX_UNLOCK((_x)->xroot->xfer_mtx)
#define	USB_XFER_LOCK_ASSERT(_x, _t)	USB_MTX_ASSERT((_x)->xroot->xfer_mtx, _t)

/* helper for converting pointers to integers */
#define	USB_P2U(ptr) \
  (((const uint8_t *)(ptr)) - ((const uint8_t *)0))

/* helper for computing offsets */
#define	USB_ADD_BYTES(ptr,size) \
  ((void *)(USB_P2U(ptr) + (size)))

/* debug macro */
#define	USB_ASSERT KASSERT

/* structure prototypes */

struct file;
struct usb_bus;
struct usb_device;
struct usb_device_request;
struct usb_page;
struct usb_page_cache;
struct usb_xfer;
struct usb_xfer_root;
struct usb_string_lang;

/* typedefs */

/* structures */

/*
 * The following structure defines a set of internal USB transfer
 * flags.
 */
struct usb_xfer_flags_int {

	enum usb_hc_mode usb_mode;	/* shadow copy of "udev->usb_mode" */
	uint16_t control_rem;		/* remainder in bytes */

	uint8_t	open:1;			/* set if USB pipe has been opened */
	uint8_t	transferring:1;		/* set if an USB transfer is in
					 * progress */
	uint8_t	did_dma_delay:1;	/* set if we waited for HW DMA */
	uint8_t	did_close:1;		/* set if we closed the USB transfer */
	uint8_t	draining:1;		/* set if we are draining an USB
					 * transfer */
	uint8_t	started:1;		/* keeps track of started or stopped */
	uint8_t	bandwidth_reclaimed:1;
	uint8_t	control_xfr:1;		/* set if control transfer */
	uint8_t	control_hdr:1;		/* set if control header should be
					 * sent */
	uint8_t	control_act:1;		/* set if control transfer is active */
	uint8_t	control_stall:1;	/* set if control transfer should be stalled */
	uint8_t control_did_data:1;	/* set if control DATA has been transferred */

	uint8_t	short_frames_ok:1;	/* filtered version */
	uint8_t	short_xfer_ok:1;	/* filtered version */
#if USB_HAVE_BUSDMA
	uint8_t	bdma_enable:1;		/* filtered version (only set if
					 * hardware supports DMA) */
	uint8_t	bdma_no_post_sync:1;	/* set if the USB callback wrapper
					 * should not do the BUS-DMA post sync
					 * operation */
	uint8_t	bdma_setup:1;		/* set if BUS-DMA has been setup */
#endif
	uint8_t	isochronous_xfr:1;	/* set if isochronous transfer */
	uint8_t	curr_dma_set:1;		/* used by USB HC/DC driver */
	uint8_t	can_cancel_immed:1;	/* set if USB transfer can be
					 * cancelled immediately */
	uint8_t	doing_callback:1;	/* set if executing the callback */
	uint8_t maxp_was_clamped:1;	/* set if the max packet size 
					 * was outside its allowed range */
};

/*
 * The following structure defines an USB transfer.
 */
struct usb_xfer {
	struct usb_callout timeout_handle;
	TAILQ_ENTRY(usb_xfer) wait_entry;	/* used at various places */

	struct usb_page_cache *buf_fixup;	/* fixup buffer(s) */
	struct usb_xfer_queue *wait_queue;	/* pointer to queue that we
						 * are waiting on */
	struct usb_page *dma_page_ptr;
	struct usb_endpoint *endpoint;	/* our USB endpoint */
	struct usb_xfer_root *xroot;	/* used by HC driver */
	void   *qh_start[2];		/* used by HC driver */
	void   *td_start[2];		/* used by HC driver */
	void   *td_transfer_first;	/* used by HC driver */
	void   *td_transfer_last;	/* used by HC driver */
	void   *td_transfer_cache;	/* used by HC driver */
	void   *priv_sc;		/* device driver data pointer 1 */
	void   *priv_fifo;		/* device driver data pointer 2 */
	void   *local_buffer;
	usb_frlength_t *frlengths;
	struct usb_page_cache *frbuffers;
	usb_callback_t *callback;

	usb_frlength_t max_hc_frame_size;
	usb_frlength_t max_data_length;
	usb_frlength_t sumlen;		/* sum of all lengths in bytes */
	usb_frlength_t actlen;		/* actual length in bytes */
	usb_timeout_t timeout;		/* milliseconds */

	usb_frcount_t max_frame_count;	/* initial value of "nframes" after
					 * setup */
	usb_frcount_t nframes;		/* number of USB frames to transfer */
	usb_frcount_t aframes;		/* actual number of USB frames
					 * transferred */
	usb_stream_t stream_id;		/* USB3.0 specific field */

	uint16_t max_packet_size;
	uint16_t max_frame_size;
	uint16_t qh_pos;
	uint16_t isoc_time_complete;	/* in ms */
	usb_timeout_t interval;	/* milliseconds */

	uint8_t	address;		/* physical USB address */
	uint8_t	endpointno;		/* physical USB endpoint */
	uint8_t	max_packet_count;
	uint8_t	usb_state;
	uint8_t fps_shift;		/* down shift of FPS, 0..3 */

	usb_error_t error;

	struct usb_xfer_flags flags;
	struct usb_xfer_flags_int flags_int;
};

/* external variables */

extern struct mtx usb_ref_lock;
extern const struct usb_string_lang usb_string_lang_en;

/* typedefs */

typedef struct malloc_type *usb_malloc_type;

/* prototypes */

#endif					/* _USB_CORE_H_ */
