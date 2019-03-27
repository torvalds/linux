/* $FreeBSD$ */
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008-2009 Hans Petter Selasky. All rights reserved.
 * Copyright (c) 2007-2008 Daniel Drake.  All rights reserved.
 * Copyright (c) 2001 Johannes Erdfelt.  All rights reserved.
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

#ifndef _LIBUSB20_H_
#define	_LIBUSB20_H_

#ifndef LIBUSB_GLOBAL_INCLUDE_FILE
#include <stdint.h>
#endif

#ifdef __cplusplus
extern	"C" {
#endif
#if 0
};					/* style */

#endif

/** \ingroup misc
 * Error codes. Most libusb20 functions return 0 on success or one of
 * these codes on failure.
 */
enum libusb20_error {
	/** Success (no error) */
	LIBUSB20_SUCCESS = 0,

	/** Input/output error */
	LIBUSB20_ERROR_IO = -1,

	/** Invalid parameter */
	LIBUSB20_ERROR_INVALID_PARAM = -2,

	/** Access denied (insufficient permissions) */
	LIBUSB20_ERROR_ACCESS = -3,

	/** No such device (it may have been disconnected) */
	LIBUSB20_ERROR_NO_DEVICE = -4,

	/** Entity not found */
	LIBUSB20_ERROR_NOT_FOUND = -5,

	/** Resource busy */
	LIBUSB20_ERROR_BUSY = -6,

	/** Operation timed out */
	LIBUSB20_ERROR_TIMEOUT = -7,

	/** Overflow */
	LIBUSB20_ERROR_OVERFLOW = -8,

	/** Pipe error */
	LIBUSB20_ERROR_PIPE = -9,

	/** System call interrupted (perhaps due to signal) */
	LIBUSB20_ERROR_INTERRUPTED = -10,

	/** Insufficient memory */
	LIBUSB20_ERROR_NO_MEM = -11,

	/** Operation not supported or unimplemented on this platform */
	LIBUSB20_ERROR_NOT_SUPPORTED = -12,

	/** Other error */
	LIBUSB20_ERROR_OTHER = -99,
};

/** \ingroup asyncio
 * libusb20_tr_get_status() values */
enum libusb20_transfer_status {
	/** Transfer completed without error. Note that this does not
	 * indicate that the entire amount of requested data was
	 * transferred. */
	LIBUSB20_TRANSFER_COMPLETED,

	/** Callback code to start transfer */
	LIBUSB20_TRANSFER_START,

	/** Drain complete callback code */
	LIBUSB20_TRANSFER_DRAINED,

	/** Transfer failed */
	LIBUSB20_TRANSFER_ERROR,

	/** Transfer timed out */
	LIBUSB20_TRANSFER_TIMED_OUT,

	/** Transfer was cancelled */
	LIBUSB20_TRANSFER_CANCELLED,

	/** For bulk/interrupt endpoints: halt condition detected
	 * (endpoint stalled). For control endpoints: control request
	 * not supported. */
	LIBUSB20_TRANSFER_STALL,

	/** Device was disconnected */
	LIBUSB20_TRANSFER_NO_DEVICE,

	/** Device sent more data than requested */
	LIBUSB20_TRANSFER_OVERFLOW,
};

/** \ingroup asyncio
 * libusb20_tr_set_flags() values */
enum libusb20_transfer_flags {
	/** Report a short frame as error */
	LIBUSB20_TRANSFER_SINGLE_SHORT_NOT_OK = 0x0001,

	/** Multiple short frames are not allowed */
	LIBUSB20_TRANSFER_MULTI_SHORT_NOT_OK = 0x0002,

	/** All transmitted frames are short terminated */
	LIBUSB20_TRANSFER_FORCE_SHORT = 0x0004,

	/** Will do a clear-stall before xfer */
	LIBUSB20_TRANSFER_DO_CLEAR_STALL = 0x0008,
};

/** \ingroup misc
 * libusb20_dev_get_mode() values
 */
enum libusb20_device_mode {
	LIBUSB20_MODE_HOST,		/* default */
	LIBUSB20_MODE_DEVICE,
};

/** \ingroup misc
 * libusb20_dev_get_speed() values
 */
enum {
	LIBUSB20_SPEED_UNKNOWN,		/* default */
	LIBUSB20_SPEED_LOW,
	LIBUSB20_SPEED_FULL,
	LIBUSB20_SPEED_HIGH,
	LIBUSB20_SPEED_VARIABLE,
	LIBUSB20_SPEED_SUPER,
};

/** \ingroup misc
 * libusb20_dev_set_power() values
 */
enum {
	LIBUSB20_POWER_OFF,
	LIBUSB20_POWER_ON,
	LIBUSB20_POWER_SAVE,
	LIBUSB20_POWER_SUSPEND,
	LIBUSB20_POWER_RESUME,
};

struct usb_device_info;
struct libusb20_transfer;
struct libusb20_backend;
struct libusb20_backend_methods;
struct libusb20_device;
struct libusb20_device_methods;
struct libusb20_config;
struct LIBUSB20_CONTROL_SETUP_DECODED;
struct LIBUSB20_DEVICE_DESC_DECODED;

typedef void (libusb20_tr_callback_t)(struct libusb20_transfer *xfer);

struct libusb20_quirk {
	uint16_t vid;			/* vendor ID */
	uint16_t pid;			/* product ID */
	uint16_t bcdDeviceLow;		/* low revision value, inclusive */
	uint16_t bcdDeviceHigh;		/* high revision value, inclusive */
	uint16_t reserved[2];		/* for the future */
	/* quirk name, UQ_XXX, including terminating zero */
	char	quirkname[64 - 12];
};

#define	LIBUSB20_MAX_FRAME_PRE_SCALE	(1U << 31)

/* USB transfer operations */
int	libusb20_tr_close(struct libusb20_transfer *xfer);
int	libusb20_tr_open(struct libusb20_transfer *xfer, uint32_t max_buf_size, uint32_t max_frame_count, uint8_t ep_no);
int	libusb20_tr_open_stream(struct libusb20_transfer *xfer, uint32_t max_buf_size, uint32_t max_frame_count, uint8_t ep_no, uint16_t stream_id);
struct libusb20_transfer *libusb20_tr_get_pointer(struct libusb20_device *pdev, uint16_t tr_index);
uint16_t libusb20_tr_get_time_complete(struct libusb20_transfer *xfer);
uint32_t libusb20_tr_get_actual_frames(struct libusb20_transfer *xfer);
uint32_t libusb20_tr_get_actual_length(struct libusb20_transfer *xfer);
uint32_t libusb20_tr_get_max_frames(struct libusb20_transfer *xfer);
uint32_t libusb20_tr_get_max_packet_length(struct libusb20_transfer *xfer);
uint32_t libusb20_tr_get_max_total_length(struct libusb20_transfer *xfer);
uint8_t	libusb20_tr_get_status(struct libusb20_transfer *xfer);
uint8_t	libusb20_tr_pending(struct libusb20_transfer *xfer);
void	libusb20_tr_callback_wrapper(struct libusb20_transfer *xfer);
void	libusb20_tr_clear_stall_sync(struct libusb20_transfer *xfer);
void	libusb20_tr_drain(struct libusb20_transfer *xfer);
void	libusb20_tr_set_buffer(struct libusb20_transfer *xfer, void *buffer, uint16_t fr_index);
void	libusb20_tr_set_callback(struct libusb20_transfer *xfer, libusb20_tr_callback_t *cb);
void	libusb20_tr_set_flags(struct libusb20_transfer *xfer, uint8_t flags);
uint32_t libusb20_tr_get_length(struct libusb20_transfer *xfer, uint16_t fr_index);
void	libusb20_tr_set_length(struct libusb20_transfer *xfer, uint32_t length, uint16_t fr_index);
void	libusb20_tr_set_priv_sc0(struct libusb20_transfer *xfer, void *sc0);
void	libusb20_tr_set_priv_sc1(struct libusb20_transfer *xfer, void *sc1);
void	libusb20_tr_set_timeout(struct libusb20_transfer *xfer, uint32_t timeout);
void	libusb20_tr_set_total_frames(struct libusb20_transfer *xfer, uint32_t nFrames);
void	libusb20_tr_setup_bulk(struct libusb20_transfer *xfer, void *pbuf, uint32_t length, uint32_t timeout);
void	libusb20_tr_setup_control(struct libusb20_transfer *xfer, void *psetup, void *pbuf, uint32_t timeout);
void	libusb20_tr_setup_intr(struct libusb20_transfer *xfer, void *pbuf, uint32_t length, uint32_t timeout);
void	libusb20_tr_setup_isoc(struct libusb20_transfer *xfer, void *pbuf, uint32_t length, uint16_t fr_index);
uint8_t	libusb20_tr_bulk_intr_sync(struct libusb20_transfer *xfer, void *pbuf, uint32_t length, uint32_t *pactlen, uint32_t timeout);
void	libusb20_tr_start(struct libusb20_transfer *xfer);
void	libusb20_tr_stop(struct libusb20_transfer *xfer);
void	libusb20_tr_submit(struct libusb20_transfer *xfer);
void   *libusb20_tr_get_priv_sc0(struct libusb20_transfer *xfer);
void   *libusb20_tr_get_priv_sc1(struct libusb20_transfer *xfer);


/* USB device operations */

const char *libusb20_dev_get_backend_name(struct libusb20_device *pdev);
const char *libusb20_dev_get_desc(struct libusb20_device *pdev);
int	libusb20_dev_close(struct libusb20_device *pdev);
int	libusb20_dev_detach_kernel_driver(struct libusb20_device *pdev, uint8_t iface_index);
int	libusb20_dev_set_config_index(struct libusb20_device *pdev, uint8_t configIndex);
int	libusb20_dev_get_debug(struct libusb20_device *pdev);
int	libusb20_dev_get_fd(struct libusb20_device *pdev);
int	libusb20_dev_kernel_driver_active(struct libusb20_device *pdev, uint8_t iface_index);
int	libusb20_dev_open(struct libusb20_device *pdev, uint16_t transfer_max);
int	libusb20_dev_process(struct libusb20_device *pdev);
int	libusb20_dev_request_sync(struct libusb20_device *pdev, struct LIBUSB20_CONTROL_SETUP_DECODED *setup, void *data, uint16_t *pactlen, uint32_t timeout, uint8_t flags);
int	libusb20_dev_req_string_sync(struct libusb20_device *pdev, uint8_t index, uint16_t langid, void *ptr, uint16_t len);
int	libusb20_dev_req_string_simple_sync(struct libusb20_device *pdev, uint8_t index, void *ptr, uint16_t len);
int	libusb20_dev_reset(struct libusb20_device *pdev);
int	libusb20_dev_check_connected(struct libusb20_device *pdev);
int	libusb20_dev_set_power_mode(struct libusb20_device *pdev, uint8_t power_mode);
uint8_t	libusb20_dev_get_power_mode(struct libusb20_device *pdev);
int	libusb20_dev_get_port_path(struct libusb20_device *pdev, uint8_t *buf, uint8_t bufsize);
uint16_t	libusb20_dev_get_power_usage(struct libusb20_device *pdev);
int	libusb20_dev_set_alt_index(struct libusb20_device *pdev, uint8_t iface_index, uint8_t alt_index);
int	libusb20_dev_get_info(struct libusb20_device *pdev, struct usb_device_info *pinfo);
int	libusb20_dev_get_iface_desc(struct libusb20_device *pdev, uint8_t iface_index, char *buf, uint8_t len);

struct LIBUSB20_DEVICE_DESC_DECODED *libusb20_dev_get_device_desc(struct libusb20_device *pdev);
struct libusb20_config *libusb20_dev_alloc_config(struct libusb20_device *pdev, uint8_t config_index);
struct libusb20_device *libusb20_dev_alloc(void);
uint8_t	libusb20_dev_get_address(struct libusb20_device *pdev);
uint8_t	libusb20_dev_get_parent_address(struct libusb20_device *pdev);
uint8_t	libusb20_dev_get_parent_port(struct libusb20_device *pdev);
uint8_t	libusb20_dev_get_bus_number(struct libusb20_device *pdev);
uint8_t	libusb20_dev_get_mode(struct libusb20_device *pdev);
uint8_t	libusb20_dev_get_speed(struct libusb20_device *pdev);
uint8_t	libusb20_dev_get_config_index(struct libusb20_device *pdev);
void	libusb20_dev_free(struct libusb20_device *pdev);
void	libusb20_dev_set_debug(struct libusb20_device *pdev, int debug);
void	libusb20_dev_wait_process(struct libusb20_device *pdev, int timeout);

/* USB global operations */

int	libusb20_be_get_dev_quirk(struct libusb20_backend *pbe, uint16_t index, struct libusb20_quirk *pq);
int	libusb20_be_get_quirk_name(struct libusb20_backend *pbe, uint16_t index, struct libusb20_quirk *pq);
int	libusb20_be_add_dev_quirk(struct libusb20_backend *pbe, struct libusb20_quirk *pq);
int	libusb20_be_remove_dev_quirk(struct libusb20_backend *pbe, struct libusb20_quirk *pq);
int	libusb20_be_get_template(struct libusb20_backend *pbe, int *ptemp);
int	libusb20_be_set_template(struct libusb20_backend *pbe, int temp);

/* USB backend operations */

struct libusb20_backend *libusb20_be_alloc(const struct libusb20_backend_methods *methods);
struct libusb20_backend *libusb20_be_alloc_default(void);
struct libusb20_backend *libusb20_be_alloc_freebsd(void);
struct libusb20_backend *libusb20_be_alloc_linux(void);
struct libusb20_backend *libusb20_be_alloc_ugen20(void);
struct libusb20_device *libusb20_be_device_foreach(struct libusb20_backend *pbe, struct libusb20_device *pdev);
void	libusb20_be_dequeue_device(struct libusb20_backend *pbe, struct libusb20_device *pdev);
void	libusb20_be_enqueue_device(struct libusb20_backend *pbe, struct libusb20_device *pdev);
void	libusb20_be_free(struct libusb20_backend *pbe);

/* USB debugging */

const char *libusb20_strerror(int);
const char *libusb20_error_name(int);

#if 0
{					/* style */
#endif
#ifdef __cplusplus
}

#endif

#endif					/* _LIBUSB20_H_ */
