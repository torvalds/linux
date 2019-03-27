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

#ifndef _USB_TRANSFER_H_
#define	_USB_TRANSFER_H_

/*
 * Definition of internal USB transfer states:
 * ===========================================
 *
 * The main reason there are many USB states is that we are allowed to
 * cancel USB transfers, then start the USB transfer again and that
 * this state transaction cannot always be done in a single atomic
 * operation without blocking the calling thread. One reason for this
 * is that the USB hardware sometimes needs to wait for DMA
 * controllers to finish which is done asynchronously and grows the
 * statemachine.
 *
 * When extending the following statemachine there are basically two
 * things you should think about: Which states should be executed or
 * modified in case of USB transfer stop and which states should be
 * executed or modified in case of USB transfer start. Also respect
 * the "can_cancel_immed" flag which basically tells if you can go
 * directly from a wait state to the cancelling states.
 */

enum {
	/* XFER start execute state */

	/* USB_ST_SETUP = 0 (already defined) */

	/* XFER transferred execute state */

	/* USB_ST_TRANSFERRED = 1 (already defined) */

	/* XFER error execute state */

	/* USB_ST_ERROR = 2 (already defined) */

	/* XFER restart after error execute state */

	USB_ST_RESTART = 8,

	/* XFER transfer idle state */

	USB_ST_WAIT_SETUP,

	/* Other XFER execute states */

	USB_ST_PIPE_OPEN = 16,
	USB_ST_PIPE_OPEN_ERROR,
	USB_ST_PIPE_OPEN_RESTART,

	USB_ST_BDMA_LOAD,
	USB_ST_BDMA_LOAD_ERROR,
	USB_ST_BDMA_LOAD_RESTART,

	USB_ST_IVAL_DLY,
	USB_ST_IVAL_DLY_ERROR,
	USB_ST_IVAL_DLY_RESTART,

	USB_ST_PIPE_STALL,
	USB_ST_PIPE_STALL_ERROR,
	USB_ST_PIPE_STALL_RESTART,

	USB_ST_ENTER,
	USB_ST_ENTER_ERROR,
	USB_ST_ENTER_RESTART,

	USB_ST_START,
	USB_ST_START_ERROR,
	USB_ST_START_RESTART,

	USB_ST_PIPE_CLOSE,
	USB_ST_PIPE_CLOSE_ERROR,
	USB_ST_PIPE_CLOSE_RESTART,

	USB_ST_BDMA_DLY,
	USB_ST_BDMA_DLY_ERROR,
	USB_ST_BDMA_DLY_RESTART,

	/* XFER transfer wait states */

	USB_ST_WAIT_PIPE_OPEN = 64,
	USB_ST_WAIT_PIPE_OPEN_ERROR,
	USB_ST_WAIT_PIPE_OPEN_RESTART,

	USB_ST_WAIT_BDMA_LOAD,
	USB_ST_WAIT_BDMA_LOAD_ERROR,
	USB_ST_WAIT_BDMA_LOAD_RESTART,

	USB_ST_WAIT_IVAL_DLY,
	USB_ST_WAIT_IVAL_DLY_ERROR,
	USB_ST_WAIT_IVAL_DLY_RESTART,

	USB_ST_WAIT_PIPE_STALL,
	USB_ST_WAIT_PIPE_STALL_ERROR,
	USB_ST_WAIT_PIPE_STALL_RESTART,

	USB_ST_WAIT_ENTER,
	USB_ST_WAIT_ENTER_ERROR,
	USB_ST_WAIT_ENTER_RESTART,

	USB_ST_WAIT_START,
	USB_ST_WAIT_START_ERROR,
	USB_ST_WAIT_START_RESTART,

	USB_ST_WAIT_PIPE_CLOSE,
	USB_ST_WAIT_PIPE_CLOSE_ERROR,
	USB_ST_WAIT_PIPE_CLOSE_RESTART,

	USB_ST_WAIT_BDMA_DLY,
	USB_ST_WAIT_BDMA_DLY_ERROR,
	USB_ST_WAIT_BDMA_DLY_RESTART,

	USB_ST_WAIT_TRANSFERRED,
	USB_ST_WAIT_TRANSFERRED_ERROR,
	USB_ST_WAIT_TRANSFERRED_RESTART,
};

/*
 * The following structure defines the messages that is used to signal
 * the "done_p" USB process.
 */
struct usb_done_msg {
	struct usb_proc_msg hdr;
	struct usb_xfer_root *xroot;
};

#define	USB_DMATAG_TO_XROOT(dpt)				\
  ((struct usb_xfer_root *)(					\
   ((uint8_t *)(dpt)) -						\
   ((uint8_t *)&((struct usb_xfer_root *)0)->dma_parent_tag)))

/*
 * The following structure is used to keep information about memory
 * that should be automatically freed at the moment all USB transfers
 * have been freed.
 */
struct usb_xfer_root {
	struct usb_dma_parent_tag dma_parent_tag;
#if USB_HAVE_BUSDMA
	struct usb_xfer_queue dma_q;
#endif
	struct usb_xfer_queue done_q;
	struct usb_done_msg done_m[2];
	struct cv cv_drain;

	struct usb_process *done_p;	/* pointer to callback process */
	void   *memory_base;
	struct mtx *xfer_mtx;	/* cannot be changed during operation */
#if USB_HAVE_BUSDMA
	struct usb_page_cache *dma_page_cache_start;
	struct usb_page_cache *dma_page_cache_end;
#endif
	struct usb_page_cache *xfer_page_cache_start;
	struct usb_page_cache *xfer_page_cache_end;
	struct usb_bus *bus;		/* pointer to USB bus (cached) */
	struct usb_device *udev;	/* pointer to USB device */

	usb_size_t memory_size;
	usb_size_t setup_refcount;
#if USB_HAVE_BUSDMA
	usb_frcount_t dma_nframes;	/* number of page caches to load */
	usb_frcount_t dma_currframe;	/* currect page cache number */
	usb_frlength_t dma_frlength_0;	/* length of page cache zero */
	uint8_t	dma_error;		/* set if virtual memory could not be
					 * loaded */
#endif
	uint8_t	done_sleep;		/* set if done thread is sleeping */
};

/*
 * The following structure is used when setting up an array of USB
 * transfers.
 */
struct usb_setup_params {
	struct usb_dma_tag *dma_tag_p;
	struct usb_page *dma_page_ptr;
	struct usb_page_cache *dma_page_cache_ptr;	/* these will be
							 * auto-freed */
	struct usb_page_cache *xfer_page_cache_ptr;	/* these will not be
							 * auto-freed */
	struct usb_device *udev;
	struct usb_xfer *curr_xfer;
	const struct usb_config *curr_setup;
	const struct usb_pipe_methods *methods;
	void   *buf;
	usb_frlength_t *xfer_length_ptr;

	usb_size_t size[7];
	usb_frlength_t bufsize;
	usb_frlength_t bufsize_max;

	uint32_t hc_max_frame_size;
	uint16_t hc_max_packet_size;
	uint8_t	hc_max_packet_count;
	enum usb_dev_speed speed;
	uint8_t	dma_tag_max;
	usb_error_t err;
};

/* function prototypes */

uint8_t	usbd_transfer_setup_sub_malloc(struct usb_setup_params *parm,
	    struct usb_page_cache **ppc, usb_size_t size, usb_size_t align,
	    usb_size_t count);
void	usb_dma_delay_done_cb(struct usb_xfer *);
void	usb_command_wrapper(struct usb_xfer_queue *pq,
	    struct usb_xfer *xfer);
void	usbd_pipe_enter(struct usb_xfer *xfer);
void	usbd_pipe_start(struct usb_xfer_queue *pq);
void	usbd_transfer_dequeue(struct usb_xfer *xfer);
void	usbd_transfer_done(struct usb_xfer *xfer, usb_error_t error);
void	usbd_transfer_enqueue(struct usb_xfer_queue *pq,
	    struct usb_xfer *xfer);
void	usbd_transfer_setup_sub(struct usb_setup_params *parm);
void	usbd_ctrl_transfer_setup(struct usb_device *udev);
void	usbd_clear_stall_locked(struct usb_device *udev,
	    struct usb_endpoint *ep);
void	usbd_clear_data_toggle(struct usb_device *udev,
	    struct usb_endpoint *ep);
usb_callback_t usbd_do_request_callback;
usb_callback_t usb_handle_request_callback;
usb_callback_t usb_do_clear_stall_callback;
void	usbd_transfer_timeout_ms(struct usb_xfer *xfer,
	    void (*cb) (void *arg), usb_timeout_t ms);
usb_timeout_t usbd_get_dma_delay(struct usb_device *udev);
void	usbd_transfer_power_ref(struct usb_xfer *xfer, int val);

#endif					/* _USB_TRANSFER_H_ */
