/* $FreeBSD$ */
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Hans Petter Selasky. All rights reserved.
 * Copyright (c) 1998 The NetBSD Foundation, Inc. All rights reserved.
 * Copyright (c) 1998 Lennart Augustsson. All rights reserved.
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

#ifndef _USB_IOCTL_H_
#define	_USB_IOCTL_H_

#ifndef USB_GLOBAL_INCLUDE_FILE
#include <sys/ioccom.h>
#include <sys/cdefs.h>

/* Building "kdump" depends on these includes */

#include <dev/usb/usb_endian.h>
#include <dev/usb/usb.h>
#endif

#define	USB_DEVICE_NAME "usbctl"
#define	USB_DEVICE_DIR "usb"
#define	USB_GENERIC_NAME "ugen"
#define	USB_TEMPLATE_SYSCTL "hw.usb.template"	/* integer type */

/*
 * Align IOCTL structures to hide differences when running 32-bit
 * programs under 64-bit kernels:
 */
#ifdef COMPAT_32BIT
#define	USB_IOCTL_STRUCT_ALIGN(n) __aligned(n)
#else
#define	USB_IOCTL_STRUCT_ALIGN(n)
#endif

/* Definition of valid template sysctl values */

enum {
	USB_TEMP_MSC,		/* USB Mass Storage */
	USB_TEMP_CDCE,		/* USB CDC Ethernet */
	USB_TEMP_MTP,		/* Message Transfer Protocol */
	USB_TEMP_MODEM,		/* USB CDC Modem */
	USB_TEMP_AUDIO,		/* USB Audio */
	USB_TEMP_KBD,		/* USB Keyboard */
	USB_TEMP_MOUSE,		/* USB Mouse */
	USB_TEMP_PHONE,		/* USB Phone */
	USB_TEMP_SERIALNET,	/* USB CDC Ethernet and Modem */
	USB_TEMP_MIDI,		/* USB MIDI */
	USB_TEMP_MULTI,		/* USB Ethernet, serial, and storage */
	USB_TEMP_MAX,
};

struct usb_read_dir {
#ifdef COMPAT_32BIT
	uint64_t urd_data;
#else
	void   *urd_data;
#endif
	uint32_t urd_startentry;
	uint32_t urd_maxlen;
} USB_IOCTL_STRUCT_ALIGN(8);

struct usb_ctl_request {
#ifdef COMPAT_32BIT
	uint64_t ucr_data;
#else
	void   *ucr_data;
#endif
	uint16_t ucr_flags;
	uint16_t ucr_actlen;		/* actual length transferred */
	uint8_t	ucr_addr;		/* zero - currently not used */
	struct usb_device_request ucr_request;
} USB_IOCTL_STRUCT_ALIGN(8);

struct usb_alt_interface {
	uint8_t	uai_interface_index;
	uint8_t	uai_alt_index;
} USB_IOCTL_STRUCT_ALIGN(1);

struct usb_gen_descriptor {
#ifdef COMPAT_32BIT
	uint64_t ugd_data;
#else
	void   *ugd_data;
#endif
	uint16_t ugd_lang_id;
	uint16_t ugd_maxlen;
	uint16_t ugd_actlen;
	uint16_t ugd_offset;
	uint8_t	ugd_config_index;
	uint8_t	ugd_string_index;
	uint8_t	ugd_iface_index;
	uint8_t	ugd_altif_index;
	uint8_t	ugd_endpt_index;
	uint8_t	ugd_report_type;
	uint8_t	reserved[8];
} USB_IOCTL_STRUCT_ALIGN(8);

struct usb_device_info {
	uint16_t udi_productNo;
	uint16_t udi_vendorNo;
	uint16_t udi_releaseNo;
	uint16_t udi_power;		/* power consumption in mA, 0 if
					 * selfpowered */
	uint8_t	udi_bus;
	uint8_t	udi_addr;		/* device address */
	uint8_t	udi_index;		/* device index */
	uint8_t	udi_class;
	uint8_t	udi_subclass;
	uint8_t	udi_protocol;
	uint8_t	udi_config_no;		/* current config number */
	uint8_t	udi_config_index;	/* current config index */
	uint8_t	udi_speed;		/* see "USB_SPEED_XXX" */
	uint8_t	udi_mode;		/* see "USB_MODE_XXX" */
	uint8_t	udi_nports;
	uint8_t	udi_hubaddr;		/* parent HUB address */
	uint8_t	udi_hubindex;		/* parent HUB device index */
	uint8_t	udi_hubport;		/* parent HUB port */
	uint8_t	udi_power_mode;		/* see "USB_POWER_MODE_XXX" */
	uint8_t	udi_suspended;		/* set if device is suspended */
	uint8_t	udi_reserved[16];	/* leave space for the future */
	char	udi_product[128];
	char	udi_vendor[128];
	char	udi_serial[64];
	char	udi_release[8];
} USB_IOCTL_STRUCT_ALIGN(2);

#define	USB_DEVICE_PORT_PATH_MAX 32

struct usb_device_port_path {
	uint8_t udp_bus;		/* which bus we are on */
	uint8_t udp_index;		/* which device index */
	uint8_t udp_port_level;		/* how many levels: 0, 1, 2 ... */
	uint8_t udp_port_no[USB_DEVICE_PORT_PATH_MAX];
} USB_IOCTL_STRUCT_ALIGN(1);

struct usb_device_stats {
	uint32_t uds_requests_ok[4];	/* Indexed by transfer type UE_XXX */
	uint32_t uds_requests_fail[4];	/* Indexed by transfer type UE_XXX */
} USB_IOCTL_STRUCT_ALIGN(4);

struct usb_fs_start {
	uint8_t	ep_index;
} USB_IOCTL_STRUCT_ALIGN(1);

struct usb_fs_stop {
	uint8_t	ep_index;
} USB_IOCTL_STRUCT_ALIGN(1);

struct usb_fs_complete {
	uint8_t	ep_index;
} USB_IOCTL_STRUCT_ALIGN(1);

/* This structure is used for all endpoint types */
struct usb_fs_endpoint {
	/*
	 * NOTE: isochronous USB transfer only use one buffer, but can have
	 * multiple frame lengths !
	 */
#ifdef COMPAT_32BIT
	uint64_t ppBuffer;
	uint64_t pLength;
#else
	void  **ppBuffer;		/* pointer to userland buffers */
	uint32_t *pLength;		/* pointer to frame lengths, updated
					 * to actual length */
#endif
	uint32_t nFrames;		/* number of frames */
	uint32_t aFrames;		/* actual number of frames */
	uint16_t flags;
	/* a single short frame will terminate */
#define	USB_FS_FLAG_SINGLE_SHORT_OK 0x0001
	/* multiple short frames are allowed */
#define	USB_FS_FLAG_MULTI_SHORT_OK 0x0002
	/* all frame(s) transmitted are short terminated */
#define	USB_FS_FLAG_FORCE_SHORT 0x0004
	/* will do a clear-stall before xfer */
#define	USB_FS_FLAG_CLEAR_STALL 0x0008
	uint16_t timeout;		/* in milliseconds */
	/* isocronous completion time in milliseconds - used for echo cancel */
	uint16_t isoc_time_complete;
	/* timeout value for no timeout */
#define	USB_FS_TIMEOUT_NONE 0
	int	status;			/* see USB_ERR_XXX */
} USB_IOCTL_STRUCT_ALIGN(8);

struct usb_fs_init {
	/* userland pointer to endpoints structure */
#ifdef COMPAT_32BIT
	uint64_t pEndpoints;
#else
	struct usb_fs_endpoint *pEndpoints;
#endif
	/* maximum number of endpoints */
	uint8_t	ep_index_max;
} USB_IOCTL_STRUCT_ALIGN(8);

struct usb_fs_uninit {
	uint8_t	dummy;			/* zero */
} USB_IOCTL_STRUCT_ALIGN(1);

struct usb_fs_open {
#define	USB_FS_MAX_BUFSIZE (1 << 18)
	uint32_t max_bufsize;
#define	USB_FS_MAX_FRAMES		(1U << 12)
#define	USB_FS_MAX_FRAMES_PRE_SCALE	(1U << 31)	/* for ISOCHRONOUS transfers */
	uint32_t max_frames;		/* read and write */
	uint16_t max_packet_length;	/* read only */
	uint8_t	dev_index;		/* currently unused */
	uint8_t	ep_index;
	uint8_t	ep_no;			/* bEndpointNumber */
} USB_IOCTL_STRUCT_ALIGN(4);

struct usb_fs_open_stream {
	struct usb_fs_open fs_open;
	uint16_t stream_id;		/* stream ID */
} USB_IOCTL_STRUCT_ALIGN(4);

struct usb_fs_close {
	uint8_t	ep_index;
} USB_IOCTL_STRUCT_ALIGN(1);

struct usb_fs_clear_stall_sync {
	uint8_t	ep_index;
} USB_IOCTL_STRUCT_ALIGN(1);

struct usb_gen_quirk {
	uint16_t index;			/* Quirk Index */
	uint16_t vid;			/* Vendor ID */
	uint16_t pid;			/* Product ID */
	uint16_t bcdDeviceLow;		/* Low Device Revision */
	uint16_t bcdDeviceHigh;		/* High Device Revision */
	uint16_t reserved[2];
	/*
	 * String version of quirk including terminating zero. See
	 * UQ_XXX in "usb_quirk.h".
	 */
	char	quirkname[64 - 14];
} USB_IOCTL_STRUCT_ALIGN(2);

/* USB controller */
#define	USB_REQUEST		_IOWR('U', 1, struct usb_ctl_request)
#define	USB_SETDEBUG		_IOW ('U', 2, int)
#define	USB_DISCOVER		_IO  ('U', 3)
#define	USB_DEVICEINFO		_IOWR('U', 4, struct usb_device_info)
#define	USB_DEVICESTATS		_IOR ('U', 5, struct usb_device_stats)
#define	USB_DEVICEENUMERATE	_IOW ('U', 6, int)

/* Generic HID device */
#define	USB_GET_REPORT_DESC	_IOWR('U', 21, struct usb_gen_descriptor)
#define	USB_SET_IMMED		_IOW ('U', 22, int)
#define	USB_GET_REPORT		_IOWR('U', 23, struct usb_gen_descriptor)
#define	USB_SET_REPORT		_IOW ('U', 24, struct usb_gen_descriptor)
#define	USB_GET_REPORT_ID	_IOR ('U', 25, int)

/* Generic USB device */
#define	USB_GET_CONFIG		_IOR ('U', 100, int)
#define	USB_SET_CONFIG		_IOW ('U', 101, int)
#define	USB_GET_ALTINTERFACE	_IOWR('U', 102, struct usb_alt_interface)
#define	USB_SET_ALTINTERFACE	_IOWR('U', 103, struct usb_alt_interface)
#define	USB_GET_DEVICE_DESC	_IOR ('U', 105, struct usb_device_descriptor)
#define	USB_GET_CONFIG_DESC	_IOR ('U', 106, struct usb_config_descriptor)
#define	USB_GET_RX_INTERFACE_DESC _IOR ('U', 107, struct usb_interface_descriptor)
#define	USB_GET_RX_ENDPOINT_DESC _IOR ('U', 108, struct usb_endpoint_descriptor)
#define	USB_GET_FULL_DESC	_IOWR('U', 109, struct usb_gen_descriptor)
#define	USB_GET_STRING_DESC	_IOWR('U', 110, struct usb_gen_descriptor)
#define	USB_DO_REQUEST		_IOWR('U', 111, struct usb_ctl_request)
#define	USB_GET_DEVICEINFO	_IOR ('U', 112, struct usb_device_info)
#define	USB_SET_RX_SHORT_XFER	_IOW ('U', 113, int)
#define	USB_SET_RX_TIMEOUT	_IOW ('U', 114, int)
#define	USB_GET_RX_FRAME_SIZE	_IOR ('U', 115, int)
#define	USB_GET_RX_BUFFER_SIZE	_IOR ('U', 117, int)
#define	USB_SET_RX_BUFFER_SIZE	_IOW ('U', 118, int)
#define	USB_SET_RX_STALL_FLAG	_IOW ('U', 119, int)
#define	USB_SET_TX_STALL_FLAG	_IOW ('U', 120, int)
#define	USB_GET_IFACE_DRIVER	_IOWR('U', 121, struct usb_gen_descriptor)
#define	USB_CLAIM_INTERFACE	_IOW ('U', 122, int)
#define	USB_RELEASE_INTERFACE	_IOW ('U', 123, int)
#define	USB_IFACE_DRIVER_ACTIVE	_IOW ('U', 124, int)
#define	USB_IFACE_DRIVER_DETACH	_IOW ('U', 125, int)
#define	USB_GET_PLUGTIME	_IOR ('U', 126, uint32_t)
#define	USB_READ_DIR		_IOW ('U', 127, struct usb_read_dir)
/* 128 - 133 unused */
#define	USB_GET_DEV_PORT_PATH	_IOR ('U', 134, struct usb_device_port_path)
#define	USB_GET_POWER_USAGE	_IOR ('U', 135, int)
#define	USB_SET_TX_FORCE_SHORT	_IOW ('U', 136, int)
#define	USB_SET_TX_TIMEOUT	_IOW ('U', 137, int)
#define	USB_GET_TX_FRAME_SIZE	_IOR ('U', 138, int)
#define	USB_GET_TX_BUFFER_SIZE	_IOR ('U', 139, int)
#define	USB_SET_TX_BUFFER_SIZE	_IOW ('U', 140, int)
#define	USB_GET_TX_INTERFACE_DESC _IOR ('U', 141, struct usb_interface_descriptor)
#define	USB_GET_TX_ENDPOINT_DESC _IOR ('U', 142, struct usb_endpoint_descriptor)
#define	USB_SET_PORT_ENABLE	_IOW ('U', 143, int)
#define	USB_SET_PORT_DISABLE	_IOW ('U', 144, int)
#define	USB_SET_POWER_MODE	_IOW ('U', 145, int)
#define	USB_GET_POWER_MODE	_IOR ('U', 146, int)
#define	USB_SET_TEMPLATE	_IOW ('U', 147, int)
#define	USB_GET_TEMPLATE	_IOR ('U', 148, int)

/* Modem device */
#define	USB_GET_CM_OVER_DATA	_IOR ('U', 180, int)
#define	USB_SET_CM_OVER_DATA	_IOW ('U', 181, int)

/* GPIO control */
#define	USB_GET_GPIO		_IOR ('U', 182, int)
#define	USB_SET_GPIO		_IOW ('U', 183, int)

/* USB file system interface */
#define	USB_FS_START		_IOW ('U', 192, struct usb_fs_start)
#define	USB_FS_STOP		_IOW ('U', 193, struct usb_fs_stop)
#define	USB_FS_COMPLETE		_IOR ('U', 194, struct usb_fs_complete)
#define	USB_FS_INIT		_IOW ('U', 195, struct usb_fs_init)
#define	USB_FS_UNINIT		_IOW ('U', 196, struct usb_fs_uninit)
#define	USB_FS_OPEN		_IOWR('U', 197, struct usb_fs_open)
#define	USB_FS_CLOSE		_IOW ('U', 198, struct usb_fs_close)
#define	USB_FS_CLEAR_STALL_SYNC _IOW ('U', 199, struct usb_fs_clear_stall_sync)
#define	USB_FS_OPEN_STREAM	_IOWR('U', 200, struct usb_fs_open_stream)

/* USB quirk system interface */
#define	USB_DEV_QUIRK_GET	_IOWR('Q', 0, struct usb_gen_quirk)
#define	USB_QUIRK_NAME_GET	_IOWR('Q', 1, struct usb_gen_quirk)
#define	USB_DEV_QUIRK_ADD	_IOW ('Q', 2, struct usb_gen_quirk)
#define	USB_DEV_QUIRK_REMOVE	_IOW ('Q', 3, struct usb_gen_quirk)

#endif					/* _USB_IOCTL_H_ */
