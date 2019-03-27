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
 * This file describes internal structures.
 */

#ifndef _LIBUSB20_INT_H_
#define	_LIBUSB20_INT_H_

#ifdef COMPAT_32BIT
#define	libusb20_pass_ptr(ptr)	((uint64_t)(uintptr_t)(ptr))
#else
#define	libusb20_pass_ptr(ptr)	(ptr)
#endif

struct libusb20_device;
struct libusb20_backend;
struct libusb20_transfer;
struct libusb20_quirk;

union libusb20_session_data {
	unsigned long session_data;
	struct timespec tv;
	uint32_t plugtime;
};

/* USB backend specific */
typedef const char *(libusb20_get_backend_name_t)(void);
typedef int (libusb20_root_get_dev_quirk_t)(struct libusb20_backend *pbe, uint16_t index, struct libusb20_quirk *pq);
typedef int (libusb20_root_get_quirk_name_t)(struct libusb20_backend *pbe, uint16_t index, struct libusb20_quirk *pq);
typedef int (libusb20_root_add_dev_quirk_t)(struct libusb20_backend *pbe, struct libusb20_quirk *pq);
typedef int (libusb20_root_remove_dev_quirk_t)(struct libusb20_backend *pbe, struct libusb20_quirk *pq);
typedef int (libusb20_close_device_t)(struct libusb20_device *pdev);
typedef int (libusb20_dev_get_info_t)(struct libusb20_device *pdev, struct usb_device_info *pinfo);
typedef int (libusb20_dev_get_iface_desc_t)(struct libusb20_device *pdev, uint8_t iface_index, char *buf, uint8_t len);
typedef int (libusb20_init_backend_t)(struct libusb20_backend *pbe);
typedef int (libusb20_open_device_t)(struct libusb20_device *pdev, uint16_t transfer_count_max);
typedef void (libusb20_exit_backend_t)(struct libusb20_backend *pbe);
typedef int (libusb20_root_set_template_t)(struct libusb20_backend *pbe, int temp);
typedef int (libusb20_root_get_template_t)(struct libusb20_backend *pbe, int *ptemp);

#define	LIBUSB20_DEFINE(n,field) \
  libusb20_##field##_t *field;

#define	LIBUSB20_DECLARE(n,field) \
  /* .field = */ n##_##field,

#define	LIBUSB20_BACKEND(m,n) \
  /* description of this backend */ \
  m(n, get_backend_name) \
  /* optional backend methods */ \
  m(n, init_backend) \
  m(n, exit_backend) \
  m(n, dev_get_info) \
  m(n, dev_get_iface_desc) \
  m(n, root_get_dev_quirk) \
  m(n, root_get_quirk_name) \
  m(n, root_add_dev_quirk) \
  m(n, root_remove_dev_quirk) \
  m(n, root_set_template) \
  m(n, root_get_template) \
  /* mandatory device methods */ \
  m(n, open_device) \
  m(n, close_device) \

struct libusb20_backend_methods {
	LIBUSB20_BACKEND(LIBUSB20_DEFINE,)
};

/* USB dummy methods */
typedef int (libusb20_dummy_int_t)(void);
typedef void (libusb20_dummy_void_t)(void);

/* USB device specific */
typedef int (libusb20_detach_kernel_driver_t)(struct libusb20_device *pdev, uint8_t iface_index);
typedef int (libusb20_do_request_sync_t)(struct libusb20_device *pdev, struct LIBUSB20_CONTROL_SETUP_DECODED *setup, void *data, uint16_t *pactlen, uint32_t timeout, uint8_t flags);
typedef int (libusb20_get_config_desc_full_t)(struct libusb20_device *pdev, uint8_t **ppbuf, uint16_t *plen, uint8_t index);
typedef int (libusb20_get_config_index_t)(struct libusb20_device *pdev, uint8_t *pindex);
typedef int (libusb20_kernel_driver_active_t)(struct libusb20_device *pdev, uint8_t iface_index);
typedef int (libusb20_process_t)(struct libusb20_device *pdev);
typedef int (libusb20_reset_device_t)(struct libusb20_device *pdev);
typedef int (libusb20_set_power_mode_t)(struct libusb20_device *pdev, uint8_t power_mode);
typedef int (libusb20_get_power_mode_t)(struct libusb20_device *pdev, uint8_t *power_mode);
typedef int (libusb20_get_power_usage_t)(struct libusb20_device *pdev, uint16_t *power_usage);
typedef int (libusb20_set_alt_index_t)(struct libusb20_device *pdev, uint8_t iface_index, uint8_t alt_index);
typedef int (libusb20_set_config_index_t)(struct libusb20_device *pdev, uint8_t index);
typedef int (libusb20_check_connected_t)(struct libusb20_device *pdev);

/* USB transfer specific */
typedef int (libusb20_tr_open_t)(struct libusb20_transfer *xfer, uint32_t MaxBufSize, uint32_t MaxFrameCount, uint8_t ep_no, uint16_t stream_id, uint8_t pre_scale);
typedef int (libusb20_tr_close_t)(struct libusb20_transfer *xfer);
typedef int (libusb20_tr_clear_stall_sync_t)(struct libusb20_transfer *xfer);
typedef void (libusb20_tr_submit_t)(struct libusb20_transfer *xfer);
typedef void (libusb20_tr_cancel_async_t)(struct libusb20_transfer *xfer);

#define	LIBUSB20_DEVICE(m,n) \
  m(n, detach_kernel_driver) \
  m(n, do_request_sync) \
  m(n, get_config_desc_full) \
  m(n, get_config_index) \
  m(n, kernel_driver_active) \
  m(n, process) \
  m(n, reset_device) \
  m(n, check_connected) \
  m(n, set_power_mode) \
  m(n, get_power_mode) \
  m(n, get_power_usage) \
  m(n, set_alt_index) \
  m(n, set_config_index) \
  m(n, tr_cancel_async) \
  m(n, tr_clear_stall_sync) \
  m(n, tr_close) \
  m(n, tr_open) \
  m(n, tr_submit) \

struct libusb20_device_methods {
	LIBUSB20_DEVICE(LIBUSB20_DEFINE,)
};

struct libusb20_backend {
	TAILQ_HEAD(, libusb20_device) usb_devs;
	const struct libusb20_backend_methods *methods;
};

struct libusb20_transfer {
	struct libusb20_device *pdev;	/* the USB device we belong to */
	libusb20_tr_callback_t *callback;
	void   *priv_sc0;		/* private client data */
	void   *priv_sc1;		/* private client data */
	/*
	 * Pointer to a list of buffer pointers:
	 */
#ifdef COMPAT_32BIT
	uint64_t *ppBuffer;
#else
	void  **ppBuffer;
#endif
	/*
	 * Pointer to frame lengths, which are updated to actual length
	 * after the USB transfer completes:
	 */
	uint32_t *pLength;
	uint32_t maxTotalLength;
	uint32_t maxFrames;		/* total number of frames */
	uint32_t nFrames;		/* total number of frames */
	uint32_t aFrames;		/* actual number of frames */
	uint32_t timeout;
	/* isochronous completion time in milliseconds */
	uint16_t timeComplete;
	uint16_t trIndex;
	uint16_t maxPacketLen;
	uint8_t	flags;			/* see LIBUSB20_TRANSFER_XXX */
	uint8_t	status;			/* see LIBUSB20_TRANSFER_XXX */
	uint8_t	is_opened;
	uint8_t	is_pending;
	uint8_t	is_cancel;
	uint8_t	is_draining;
	uint8_t	is_restart;
};

struct libusb20_device {

	/* device descriptor */
	struct LIBUSB20_DEVICE_DESC_DECODED ddesc;

	/* device timestamp */
	union libusb20_session_data session_data;

	/* our device entry */
	TAILQ_ENTRY(libusb20_device) dev_entry;

	/* device methods */
	const struct libusb20_device_methods *methods;

	/* backend methods */
	const struct libusb20_backend_methods *beMethods;

	/* list of USB transfers */
	struct libusb20_transfer *pTransfer;

	/* private backend data */
	void   *privBeData;

	/* libUSB v0.1 and v1.0 compat data */
	void   *privLuData;

	/* claimed interface */
	uint8_t claimed_interface;

	/* auto detach kernel driver */
	uint8_t auto_detach;
  
	/* device file handle */
	int	file;

	/* device file handle (control transfers only) */
	int	file_ctrl;

	/* debugging level */
	int	debug;

	/* number of USB transfers */
	uint16_t nTransfer;

	uint8_t	bus_number;
	uint8_t	device_address;
	uint8_t	usb_mode;
	uint8_t	usb_speed;
	uint8_t	is_opened;
	uint8_t parent_address;
	uint8_t parent_port;
	uint8_t port_level;

	char	usb_desc[96];
#define	LIBUSB20_DEVICE_PORT_PATH_MAX	32
	uint8_t	port_path[LIBUSB20_DEVICE_PORT_PATH_MAX];
};

extern const struct libusb20_backend_methods libusb20_ugen20_backend;
extern const struct libusb20_backend_methods libusb20_linux_backend;

#endif					/* _LIBUSB20_INT_H_ */
