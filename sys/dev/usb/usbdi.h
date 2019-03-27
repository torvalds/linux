/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Andrew Thompson
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#ifndef _USB_USBDI_H_
#define _USB_USBDI_H_

struct usb_fifo;
struct usb_xfer;
struct usb_device;
struct usb_attach_arg;
struct usb_interface;
struct usb_endpoint;
struct usb_page_cache;
struct usb_page_search;
struct usb_process;
struct usb_proc_msg;
struct usb_mbuf;
struct usb_fs_privdata;
struct mbuf;

typedef enum {	/* keep in sync with usb_errstr_table */
	USB_ERR_NORMAL_COMPLETION = 0,
	USB_ERR_PENDING_REQUESTS,	/* 1 */
	USB_ERR_NOT_STARTED,		/* 2 */
	USB_ERR_INVAL,			/* 3 */
	USB_ERR_NOMEM,			/* 4 */
	USB_ERR_CANCELLED,		/* 5 */
	USB_ERR_BAD_ADDRESS,		/* 6 */
	USB_ERR_BAD_BUFSIZE,		/* 7 */
	USB_ERR_BAD_FLAG,		/* 8 */
	USB_ERR_NO_CALLBACK,		/* 9 */
	USB_ERR_IN_USE,			/* 10 */
	USB_ERR_NO_ADDR,		/* 11 */
	USB_ERR_NO_PIPE,		/* 12 */
	USB_ERR_ZERO_NFRAMES,		/* 13 */
	USB_ERR_ZERO_MAXP,		/* 14 */
	USB_ERR_SET_ADDR_FAILED,	/* 15 */
	USB_ERR_NO_POWER,		/* 16 */
	USB_ERR_TOO_DEEP,		/* 17 */
	USB_ERR_IOERROR,		/* 18 */
	USB_ERR_NOT_CONFIGURED,		/* 19 */
	USB_ERR_TIMEOUT,		/* 20 */
	USB_ERR_SHORT_XFER,		/* 21 */
	USB_ERR_STALLED,		/* 22 */
	USB_ERR_INTERRUPTED,		/* 23 */
	USB_ERR_DMA_LOAD_FAILED,	/* 24 */
	USB_ERR_BAD_CONTEXT,		/* 25 */
	USB_ERR_NO_ROOT_HUB,		/* 26 */
	USB_ERR_NO_INTR_THREAD,		/* 27 */
	USB_ERR_NOT_LOCKED,		/* 28 */
	USB_ERR_MAX
} usb_error_t;

/*
 * Flags for transfers
 */
#define	USB_FORCE_SHORT_XFER	0x0001	/* force a short transmit last */
#define	USB_SHORT_XFER_OK	0x0004	/* allow short reads */
#define	USB_DELAY_STATUS_STAGE	0x0010	/* insert delay before STATUS stage */
#define	USB_USER_DATA_PTR	0x0020	/* internal flag */
#define	USB_MULTI_SHORT_OK	0x0040	/* allow multiple short frames */
#define	USB_MANUAL_STATUS	0x0080	/* manual ctrl status */

#define	USB_NO_TIMEOUT 0
#define	USB_DEFAULT_TIMEOUT 5000	/* 5000 ms = 5 seconds */

#if defined(_KERNEL)
/* typedefs */

typedef void (usb_callback_t)(struct usb_xfer *, usb_error_t);
typedef void (usb_proc_callback_t)(struct usb_proc_msg *);
typedef usb_error_t (usb_handle_req_t)(struct usb_device *,
    struct usb_device_request *, const void **, uint16_t *);

typedef int (usb_fifo_open_t)(struct usb_fifo *fifo, int fflags);
typedef void (usb_fifo_close_t)(struct usb_fifo *fifo, int fflags);
typedef int (usb_fifo_ioctl_t)(struct usb_fifo *fifo, u_long cmd, void *addr, int fflags);
typedef void (usb_fifo_cmd_t)(struct usb_fifo *fifo);
typedef void (usb_fifo_filter_t)(struct usb_fifo *fifo, struct usb_mbuf *m);


/* USB events */
#ifndef USB_GLOBAL_INCLUDE_FILE
#include <sys/eventhandler.h>
#endif
typedef void (*usb_dev_configured_t)(void *, struct usb_device *,
    struct usb_attach_arg *);
EVENTHANDLER_DECLARE(usb_dev_configured, usb_dev_configured_t);

/*
 * The following macros are used used to convert milliseconds into
 * HZ. We use 1024 instead of 1000 milliseconds per second to save a
 * full division.
 */
#define	USB_MS_HZ 1024

#define	USB_MS_TO_TICKS(ms) \
  (((uint32_t)((((uint32_t)(ms)) * ((uint32_t)(hz))) + USB_MS_HZ - 1)) / USB_MS_HZ)

/*
 * Common queue structure for USB transfers.
 */
struct usb_xfer_queue {
	TAILQ_HEAD(, usb_xfer) head;
	struct usb_xfer *curr;		/* current USB transfer processed */
	void    (*command) (struct usb_xfer_queue *pq);
	uint8_t	recurse_1:1;
	uint8_t	recurse_2:1;
	uint8_t	recurse_3:1;
	uint8_t	reserved:5;
};

/*
 * The following structure defines an USB endpoint
 * USB endpoint.
 */
struct usb_endpoint {
	/* queue of USB transfers */
	struct usb_xfer_queue endpoint_q[USB_MAX_EP_STREAMS];

	struct usb_endpoint_descriptor *edesc;
	struct usb_endpoint_ss_comp_descriptor *ecomp;
	const struct usb_pipe_methods *methods;	/* set by HC driver */

	uint16_t isoc_next;

	uint8_t	toggle_next:1;		/* next data toggle value */
	uint8_t	is_stalled:1;		/* set if endpoint is stalled */
	uint8_t	is_synced:1;		/* set if we a synchronised */
	uint8_t	unused:5;
	uint8_t	iface_index;		/* not used by "default endpoint" */

	uint8_t refcount_alloc;		/* allocation refcount */
	uint8_t refcount_bw;		/* bandwidth refcount */
#define	USB_EP_REF_MAX 0x3f

	/* High-Speed resource allocation (valid if "refcount_bw" > 0) */

	uint8_t	usb_smask;		/* USB start mask */
	uint8_t	usb_cmask;		/* USB complete mask */
	uint8_t	usb_uframe;		/* USB microframe */

	/* USB endpoint mode, see USB_EP_MODE_XXX */

	uint8_t ep_mode;
};

/*
 * The following structure defines an USB interface.
 */
struct usb_interface {
	struct usb_interface_descriptor *idesc;
	device_t subdev;
	uint8_t	alt_index;
	uint8_t	parent_iface_index;

	/* Linux compat */
	struct usb_host_interface *altsetting;
	struct usb_host_interface *cur_altsetting;
	struct usb_device *linux_udev;
	void   *bsd_priv_sc;		/* device specific information */
	char   *pnpinfo;		/* additional PnP-info for this interface */
	uint8_t	num_altsetting;		/* number of alternate settings */
	uint8_t	bsd_iface_index;
};

/*
 * The following structure defines a set of USB transfer flags.
 */
struct usb_xfer_flags {
	uint8_t	force_short_xfer:1;	/* force a short transmit transfer
					 * last */
	uint8_t	short_xfer_ok:1;	/* allow short receive transfers */
	uint8_t	short_frames_ok:1;	/* allow short frames */
	uint8_t	pipe_bof:1;		/* block pipe on failure */
	uint8_t	proxy_buffer:1;		/* makes buffer size a factor of
					 * "max_frame_size" */
	uint8_t	ext_buffer:1;		/* uses external DMA buffer */
	uint8_t	manual_status:1;	/* non automatic status stage on
					 * control transfers */
	uint8_t	no_pipe_ok:1;		/* set if "USB_ERR_NO_PIPE" error can
					 * be ignored */
	uint8_t	stall_pipe:1;		/* set if the endpoint belonging to
					 * this USB transfer should be stalled
					 * before starting this transfer! */
	uint8_t pre_scale_frames:1;	/* "usb_config->frames" is
					 * assumed to give the
					 * buffering time in
					 * milliseconds and is
					 * converted into the nearest
					 * number of frames when the
					 * USB transfer is setup. This
					 * option only has effect for
					 * ISOCHRONOUS transfers.
					 */
};

/*
 * The following structure define an USB configuration, that basically
 * is used when setting up an USB transfer.
 */
struct usb_config {
	usb_callback_t *callback;	/* USB transfer callback */
	usb_frlength_t bufsize;	/* total pipe buffer size in bytes */
	usb_frcount_t frames;		/* maximum number of USB frames */
	usb_timeout_t interval;	/* interval in milliseconds */
#define	USB_DEFAULT_INTERVAL	0
	usb_timeout_t timeout;		/* transfer timeout in milliseconds */
	struct usb_xfer_flags flags;	/* transfer flags */
	usb_stream_t stream_id;		/* USB3.0 specific */
	enum usb_hc_mode usb_mode;	/* host or device mode */
	uint8_t	type;			/* pipe type */
	uint8_t	endpoint;		/* pipe number */
	uint8_t	direction;		/* pipe direction */
	uint8_t	ep_index;		/* pipe index match to use */
	uint8_t	if_index;		/* "ifaces" index to use */
};

/*
 * Use these macro when defining USB device ID arrays if you want to
 * have your driver module automatically loaded in host, device or
 * both modes respectively:
 */
#if USB_HAVE_ID_SECTION
#define	STRUCT_USB_HOST_ID \
    struct usb_device_id __section("usb_host_id")
#define	STRUCT_USB_DEVICE_ID \
    struct usb_device_id __section("usb_device_id")
#define	STRUCT_USB_DUAL_ID \
    struct usb_device_id __section("usb_dual_id")
#else
#define	STRUCT_USB_HOST_ID \
    struct usb_device_id
#define	STRUCT_USB_DEVICE_ID \
    struct usb_device_id
#define	STRUCT_USB_DUAL_ID \
    struct usb_device_id
#endif			/* USB_HAVE_ID_SECTION */

/*
 * The following structure is used when looking up an USB driver for
 * an USB device. It is inspired by the Linux structure called
 * "usb_device_id".
 */
struct usb_device_id {

	/* Select which fields to match against */
#if BYTE_ORDER == LITTLE_ENDIAN
	uint16_t
		match_flag_vendor:1,
		match_flag_product:1,
		match_flag_dev_lo:1,
		match_flag_dev_hi:1,

		match_flag_dev_class:1,
		match_flag_dev_subclass:1,
		match_flag_dev_protocol:1,
		match_flag_int_class:1,

		match_flag_int_subclass:1,
		match_flag_int_protocol:1,
		match_flag_unused:6;
#else
	uint16_t
		match_flag_unused:6,
		match_flag_int_protocol:1,
		match_flag_int_subclass:1,

		match_flag_int_class:1,
		match_flag_dev_protocol:1,
		match_flag_dev_subclass:1,
		match_flag_dev_class:1,

		match_flag_dev_hi:1,
		match_flag_dev_lo:1,
		match_flag_product:1,
		match_flag_vendor:1;
#endif

	/* Used for product specific matches; the BCD range is inclusive */
	uint16_t idVendor;
	uint16_t idProduct;
	uint16_t bcdDevice_lo;
	uint16_t bcdDevice_hi;

	/* Used for device class matches */
	uint8_t	bDeviceClass;
	uint8_t	bDeviceSubClass;
	uint8_t	bDeviceProtocol;

	/* Used for interface class matches */
	uint8_t	bInterfaceClass;
	uint8_t	bInterfaceSubClass;
	uint8_t	bInterfaceProtocol;

#if USB_HAVE_COMPAT_LINUX
	/* which fields to match against */
	uint16_t match_flags;
#define	USB_DEVICE_ID_MATCH_VENDOR		0x0001
#define	USB_DEVICE_ID_MATCH_PRODUCT		0x0002
#define	USB_DEVICE_ID_MATCH_DEV_LO		0x0004
#define	USB_DEVICE_ID_MATCH_DEV_HI		0x0008
#define	USB_DEVICE_ID_MATCH_DEV_CLASS		0x0010
#define	USB_DEVICE_ID_MATCH_DEV_SUBCLASS	0x0020
#define	USB_DEVICE_ID_MATCH_DEV_PROTOCOL	0x0040
#define	USB_DEVICE_ID_MATCH_INT_CLASS		0x0080
#define	USB_DEVICE_ID_MATCH_INT_SUBCLASS	0x0100
#define	USB_DEVICE_ID_MATCH_INT_PROTOCOL	0x0200
#endif

	/* Hook for driver specific information */
	unsigned long driver_info;
} __aligned(32);

#define USB_STD_PNP_INFO "M16:mask;U16:vendor;U16:product;L16:release;G16:release;" \
	"U8:devclass;U8:devsubclass;U8:devproto;" \
	"U8:intclass;U8:intsubclass;U8:intprotocol;"
#define USB_STD_PNP_HOST_INFO USB_STD_PNP_INFO "T:mode=host;"
#define USB_STD_PNP_DEVICE_INFO USB_STD_PNP_INFO "T:mode=device;"
#define USB_PNP_HOST_INFO(table)					\
	MODULE_PNP_INFO(USB_STD_PNP_HOST_INFO, uhub, table, table,	\
	    sizeof(table) / sizeof(table[0]))
#define USB_PNP_DEVICE_INFO(table)					\
	MODULE_PNP_INFO(USB_STD_PNP_DEVICE_INFO, uhub, table, table,	\
	    sizeof(table) / sizeof(table[0]))
#define USB_PNP_DUAL_INFO(table)					\
	MODULE_PNP_INFO(USB_STD_PNP_INFO, uhub, table, table,		\
	    sizeof(table) / sizeof(table[0]))

/* check that the size of the structure above is correct */
extern char usb_device_id_assert[(sizeof(struct usb_device_id) == 32) ? 1 : -1];

#define	USB_VENDOR(vend)			\
  .match_flag_vendor = 1, .idVendor = (vend)

#define	USB_PRODUCT(prod)			\
  .match_flag_product = 1, .idProduct = (prod)

#define	USB_VP(vend,prod)			\
  USB_VENDOR(vend), USB_PRODUCT(prod)

#define	USB_VPI(vend,prod,info)			\
  USB_VENDOR(vend), USB_PRODUCT(prod), USB_DRIVER_INFO(info)

#define	USB_DEV_BCD_GTEQ(lo)	/* greater than or equal */ \
  .match_flag_dev_lo = 1, .bcdDevice_lo = (lo)

#define	USB_DEV_BCD_LTEQ(hi)	/* less than or equal */ \
  .match_flag_dev_hi = 1, .bcdDevice_hi = (hi)

#define	USB_DEV_CLASS(dc)			\
  .match_flag_dev_class = 1, .bDeviceClass = (dc)

#define	USB_DEV_SUBCLASS(dsc)			\
  .match_flag_dev_subclass = 1, .bDeviceSubClass = (dsc)

#define	USB_DEV_PROTOCOL(dp)			\
  .match_flag_dev_protocol = 1, .bDeviceProtocol = (dp)

#define	USB_IFACE_CLASS(ic)			\
  .match_flag_int_class = 1, .bInterfaceClass = (ic)

#define	USB_IFACE_SUBCLASS(isc)			\
  .match_flag_int_subclass = 1, .bInterfaceSubClass = (isc)

#define	USB_IFACE_PROTOCOL(ip)			\
  .match_flag_int_protocol = 1, .bInterfaceProtocol = (ip)

#define	USB_IF_CSI(class,subclass,info)			\
  USB_IFACE_CLASS(class), USB_IFACE_SUBCLASS(subclass), USB_DRIVER_INFO(info)

#define	USB_DRIVER_INFO(n)			\
  .driver_info = (n)

#define	USB_GET_DRIVER_INFO(did)		\
  (did)->driver_info

/*
 * The following structure keeps information that is used to match
 * against an array of "usb_device_id" elements.
 */
struct usbd_lookup_info {
	uint16_t idVendor;
	uint16_t idProduct;
	uint16_t bcdDevice;
	uint8_t	bDeviceClass;
	uint8_t	bDeviceSubClass;
	uint8_t	bDeviceProtocol;
	uint8_t	bInterfaceClass;
	uint8_t	bInterfaceSubClass;
	uint8_t	bInterfaceProtocol;
	uint8_t	bIfaceIndex;
	uint8_t	bIfaceNum;
	uint8_t	bConfigIndex;
	uint8_t	bConfigNum;
};

/* Structure used by probe and attach */

struct usb_attach_arg {
	struct usbd_lookup_info info;
	device_t temp_dev;		/* for internal use */
	unsigned long driver_info;	/* for internal use */
	void *driver_ivar;
	struct usb_device *device;	/* current device */
	struct usb_interface *iface;	/* current interface */
	enum usb_hc_mode usb_mode;	/* host or device mode */
	uint8_t	port;
	uint8_t dev_state;
#define UAA_DEV_READY		0
#define UAA_DEV_DISABLED	1
#define UAA_DEV_EJECTING	2
};

/*
 * General purpose locking wrappers to ease supporting
 * USB polled mode:
 */
#ifdef INVARIANTS
#define	USB_MTX_ASSERT(_m, _t) do {		\
	if (!USB_IN_POLLING_MODE_FUNC())	\
		mtx_assert(_m, _t);		\
} while (0)
#else
#define	USB_MTX_ASSERT(_m, _t) do { } while (0)
#endif

#define	USB_MTX_LOCK(_m) do {			\
	if (!USB_IN_POLLING_MODE_FUNC())	\
		mtx_lock(_m);			\
} while (0)

#define	USB_MTX_UNLOCK(_m) do {			\
	if (!USB_IN_POLLING_MODE_FUNC())	\
		mtx_unlock(_m);			\
} while (0)

#define	USB_MTX_LOCK_SPIN(_m) do {		\
	if (!USB_IN_POLLING_MODE_FUNC())	\
		mtx_lock_spin(_m);		\
} while (0)

#define	USB_MTX_UNLOCK_SPIN(_m) do {		\
	if (!USB_IN_POLLING_MODE_FUNC())	\
		mtx_unlock_spin(_m);		\
} while (0)

/*
 * The following is a wrapper for the callout structure to ease
 * porting the code to other platforms.
 */
struct usb_callout {
	struct callout co;
};
#define	usb_callout_init_mtx(c,m,f) callout_init_mtx(&(c)->co,m,f)
#define	usb_callout_reset(c,...) do {			\
	if (!USB_IN_POLLING_MODE_FUNC())		\
		callout_reset(&(c)->co, __VA_ARGS__);	\
} while (0)
#define	usb_callout_reset_sbt(c,...) do {			\
	if (!USB_IN_POLLING_MODE_FUNC())			\
		callout_reset_sbt(&(c)->co, __VA_ARGS__);	\
} while (0)
#define	usb_callout_stop(c) do {			\
	if (!USB_IN_POLLING_MODE_FUNC()) {		\
		callout_stop(&(c)->co);			\
	} else {					\
		/*					\
		 * Cannot stop callout when		\
		 * polling. Set dummy callback		\
		 * function instead:			\
		 */					\
		(c)->co.c_func = &usbd_dummy_timeout;	\
	}						\
} while (0)
#define	usb_callout_drain(c) callout_drain(&(c)->co)
#define	usb_callout_pending(c) callout_pending(&(c)->co)

/* USB transfer states */

#define	USB_ST_SETUP       0
#define	USB_ST_TRANSFERRED 1
#define	USB_ST_ERROR       2

/* USB handle request states */
#define	USB_HR_NOT_COMPLETE	0
#define	USB_HR_COMPLETE_OK	1
#define	USB_HR_COMPLETE_ERR	2

/*
 * The following macro will return the current state of an USB
 * transfer like defined by the "USB_ST_XXX" enums.
 */
#define	USB_GET_STATE(xfer) (usbd_xfer_state(xfer))

/*
 * The following structure defines the USB process message header.
 */
struct usb_proc_msg {
	TAILQ_ENTRY(usb_proc_msg) pm_qentry;
	usb_proc_callback_t *pm_callback;
	usb_size_t pm_num;
};

#define	USB_FIFO_TX 0
#define	USB_FIFO_RX 1

/*
 * Locking note for the following functions.  All the
 * "usb_fifo_cmd_t" and "usb_fifo_filter_t" functions are called
 * locked. The others are called unlocked.
 */
struct usb_fifo_methods {
	usb_fifo_open_t *f_open;
	usb_fifo_close_t *f_close;
	usb_fifo_ioctl_t *f_ioctl;
	/*
	 * NOTE: The post-ioctl callback is called after the USB reference
	 * gets locked in the IOCTL handler:
	 */
	usb_fifo_ioctl_t *f_ioctl_post;
	usb_fifo_cmd_t *f_start_read;
	usb_fifo_cmd_t *f_stop_read;
	usb_fifo_cmd_t *f_start_write;
	usb_fifo_cmd_t *f_stop_write;
	usb_fifo_filter_t *f_filter_read;
	usb_fifo_filter_t *f_filter_write;
	const char *basename[4];
	const char *postfix[4];
};

struct usb_fifo_sc {
	struct usb_fifo *fp[2];
	struct usb_fs_privdata *dev;
};

const char *usbd_errstr(usb_error_t error);
void	*usbd_find_descriptor(struct usb_device *udev, void *id,
	    uint8_t iface_index, uint8_t type, uint8_t type_mask,
	    uint8_t subtype, uint8_t subtype_mask);
struct usb_config_descriptor *usbd_get_config_descriptor(
	    struct usb_device *udev);
struct usb_device_descriptor *usbd_get_device_descriptor(
	    struct usb_device *udev);
struct usb_interface *usbd_get_iface(struct usb_device *udev,
	    uint8_t iface_index);
struct usb_interface_descriptor *usbd_get_interface_descriptor(
	    struct usb_interface *iface);
struct usb_endpoint *usbd_get_endpoint(struct usb_device *udev, uint8_t iface_index,
		    const struct usb_config *setup);
struct usb_endpoint *usbd_get_ep_by_addr(struct usb_device *udev, uint8_t ea_val);
usb_error_t	usbd_interface_count(struct usb_device *udev, uint8_t *count);
enum usb_hc_mode usbd_get_mode(struct usb_device *udev);
enum usb_dev_speed usbd_get_speed(struct usb_device *udev);
void	device_set_usb_desc(device_t dev);
void	usb_pause_mtx(struct mtx *mtx, int _ticks);
usb_error_t	usbd_set_pnpinfo(struct usb_device *udev,
			uint8_t iface_index, const char *pnpinfo);
usb_error_t	usbd_add_dynamic_quirk(struct usb_device *udev,
			uint16_t quirk);
usb_error_t	usbd_set_endpoint_mode(struct usb_device *udev,
			struct usb_endpoint *ep, uint8_t ep_mode);
uint8_t		usbd_get_endpoint_mode(struct usb_device *udev,
			struct usb_endpoint *ep);

const struct usb_device_id *usbd_lookup_id_by_info(
	    const struct usb_device_id *id, usb_size_t sizeof_id,
	    const struct usbd_lookup_info *info);
int	usbd_lookup_id_by_uaa(const struct usb_device_id *id,
	    usb_size_t sizeof_id, struct usb_attach_arg *uaa);

usb_error_t usbd_do_request_flags(struct usb_device *udev, struct mtx *mtx,
		    struct usb_device_request *req, void *data, uint16_t flags,
		    uint16_t *actlen, usb_timeout_t timeout);
#define	usbd_do_request(u,m,r,d) \
  usbd_do_request_flags(u,m,r,d,0,NULL,USB_DEFAULT_TIMEOUT)

uint8_t	usbd_clear_stall_callback(struct usb_xfer *xfer1,
	    struct usb_xfer *xfer2);
uint8_t	usbd_get_interface_altindex(struct usb_interface *iface);
usb_error_t usbd_set_alt_interface_index(struct usb_device *udev,
	    uint8_t iface_index, uint8_t alt_index);
uint32_t usbd_get_isoc_fps(struct usb_device *udev);
usb_error_t usbd_transfer_setup(struct usb_device *udev,
	    const uint8_t *ifaces, struct usb_xfer **pxfer,
	    const struct usb_config *setup_start, uint16_t n_setup,
	    void *priv_sc, struct mtx *priv_mtx);
void	usbd_transfer_submit(struct usb_xfer *xfer);
void	usbd_transfer_clear_stall(struct usb_xfer *xfer);
void	usbd_transfer_drain(struct usb_xfer *xfer);
uint8_t	usbd_transfer_pending(struct usb_xfer *xfer);
void	usbd_transfer_start(struct usb_xfer *xfer);
void	usbd_transfer_stop(struct usb_xfer *xfer);
void	usbd_transfer_unsetup(struct usb_xfer **pxfer, uint16_t n_setup);
void	usbd_transfer_poll(struct usb_xfer **ppxfer, uint16_t max);
void	usbd_set_parent_iface(struct usb_device *udev, uint8_t iface_index,
	    uint8_t parent_index);
uint8_t	usbd_get_bus_index(struct usb_device *udev);
uint8_t	usbd_get_device_index(struct usb_device *udev);
void	usbd_set_power_mode(struct usb_device *udev, uint8_t power_mode);
uint8_t	usbd_filter_power_mode(struct usb_device *udev, uint8_t power_mode);
uint8_t	usbd_device_attached(struct usb_device *udev);

usb_frlength_t
	usbd_xfer_old_frame_length(struct usb_xfer *xfer, usb_frcount_t frindex);
void	usbd_xfer_status(struct usb_xfer *xfer, int *actlen, int *sumlen,
	    int *aframes, int *nframes);
struct usb_page_cache *usbd_xfer_get_frame(struct usb_xfer *, usb_frcount_t);
void	*usbd_xfer_get_frame_buffer(struct usb_xfer *, usb_frcount_t);
void	*usbd_xfer_softc(struct usb_xfer *xfer);
void	*usbd_xfer_get_priv(struct usb_xfer *xfer);
void	usbd_xfer_set_priv(struct usb_xfer *xfer, void *);
void	usbd_xfer_set_interval(struct usb_xfer *xfer, int);
uint8_t	usbd_xfer_state(struct usb_xfer *xfer);
void	usbd_xfer_set_frame_data(struct usb_xfer *xfer, usb_frcount_t frindex,
	    void *ptr, usb_frlength_t len);
void	usbd_xfer_frame_data(struct usb_xfer *xfer, usb_frcount_t frindex,
	    void **ptr, int *len);
void	usbd_xfer_set_frame_offset(struct usb_xfer *xfer, usb_frlength_t offset,
	    usb_frcount_t frindex);
usb_frlength_t usbd_xfer_max_len(struct usb_xfer *xfer);
usb_frlength_t usbd_xfer_max_framelen(struct usb_xfer *xfer);
usb_frcount_t usbd_xfer_max_frames(struct usb_xfer *xfer);
uint8_t	usbd_xfer_get_fps_shift(struct usb_xfer *xfer);
usb_frlength_t usbd_xfer_frame_len(struct usb_xfer *xfer,
	    usb_frcount_t frindex);
void	usbd_xfer_set_frame_len(struct usb_xfer *xfer, usb_frcount_t frindex,
	    usb_frlength_t len);
void	usbd_xfer_set_timeout(struct usb_xfer *xfer, int timeout);
void	usbd_xfer_set_frames(struct usb_xfer *xfer, usb_frcount_t n);
void	usbd_xfer_set_stall(struct usb_xfer *xfer);
int	usbd_xfer_is_stalled(struct usb_xfer *xfer);
void	usbd_xfer_set_flag(struct usb_xfer *xfer, int flag);
void	usbd_xfer_clr_flag(struct usb_xfer *xfer, int flag);
uint16_t usbd_xfer_get_timestamp(struct usb_xfer *xfer);
uint8_t usbd_xfer_maxp_was_clamped(struct usb_xfer *xfer);

void	usbd_copy_in(struct usb_page_cache *cache, usb_frlength_t offset,
	    const void *ptr, usb_frlength_t len);
int	usbd_copy_in_user(struct usb_page_cache *cache, usb_frlength_t offset,
	    const void *ptr, usb_frlength_t len);
void	usbd_copy_out(struct usb_page_cache *cache, usb_frlength_t offset,
	    void *ptr, usb_frlength_t len);
int	usbd_copy_out_user(struct usb_page_cache *cache, usb_frlength_t offset,
	    void *ptr, usb_frlength_t len);
void	usbd_get_page(struct usb_page_cache *pc, usb_frlength_t offset,
	    struct usb_page_search *res);
void	usbd_m_copy_in(struct usb_page_cache *cache, usb_frlength_t dst_offset,
	    struct mbuf *m, usb_size_t src_offset, usb_frlength_t src_len);
void	usbd_frame_zero(struct usb_page_cache *cache, usb_frlength_t offset,
	    usb_frlength_t len);
void	usbd_start_re_enumerate(struct usb_device *udev);
usb_error_t
	usbd_start_set_config(struct usb_device *, uint8_t);
int	usbd_in_polling_mode(void);
void	usbd_dummy_timeout(void *);

int	usb_fifo_attach(struct usb_device *udev, void *priv_sc,
	    struct mtx *priv_mtx, struct usb_fifo_methods *pm,
	    struct usb_fifo_sc *f_sc, uint16_t unit, int16_t subunit,
	    uint8_t iface_index, uid_t uid, gid_t gid, int mode);
void	usb_fifo_detach(struct usb_fifo_sc *f_sc);
int	usb_fifo_alloc_buffer(struct usb_fifo *f, uint32_t bufsize,
	    uint16_t nbuf);
void	usb_fifo_free_buffer(struct usb_fifo *f);
uint32_t usb_fifo_put_bytes_max(struct usb_fifo *fifo);
void	usb_fifo_put_data(struct usb_fifo *fifo, struct usb_page_cache *pc,
	    usb_frlength_t offset, usb_frlength_t len, uint8_t what);
void	usb_fifo_put_data_linear(struct usb_fifo *fifo, void *ptr,
	    usb_size_t len, uint8_t what);
uint8_t	usb_fifo_put_data_buffer(struct usb_fifo *f, void *ptr, usb_size_t len);
void	usb_fifo_put_data_error(struct usb_fifo *fifo);
uint8_t	usb_fifo_get_data(struct usb_fifo *fifo, struct usb_page_cache *pc,
	    usb_frlength_t offset, usb_frlength_t len, usb_frlength_t *actlen,
	    uint8_t what);
uint8_t	usb_fifo_get_data_linear(struct usb_fifo *fifo, void *ptr,
	    usb_size_t len, usb_size_t *actlen, uint8_t what);
uint8_t	usb_fifo_get_data_buffer(struct usb_fifo *f, void **pptr,
	    usb_size_t *plen);
void	usb_fifo_reset(struct usb_fifo *f);
void	usb_fifo_wakeup(struct usb_fifo *f);
void	usb_fifo_get_data_error(struct usb_fifo *fifo);
void	*usb_fifo_softc(struct usb_fifo *fifo);
void	usb_fifo_set_close_zlp(struct usb_fifo *, uint8_t);
void	usb_fifo_set_write_defrag(struct usb_fifo *, uint8_t);
void	usb_fifo_free(struct usb_fifo *f);
#endif /* _KERNEL */
#endif /* _USB_USBDI_H_ */
