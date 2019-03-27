/* $FreeBSD$ */
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Sylvestre Gallon. All rights reserved.
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

#ifndef __LIBUSB_H__
#define	__LIBUSB_H__

#ifndef LIBUSB_GLOBAL_INCLUDE_FILE
#include <stdint.h>
#include <sys/time.h>
#include <sys/types.h>
#endif

#define	LIBUSB_API_VERSION 0x01000102

#define	LIBUSB_CALL

#ifdef __cplusplus
extern	"C" {
#endif
#if 0
}					/* indent fix */

#endif

/* libusb enums */

enum libusb_class_code {
	LIBUSB_CLASS_PER_INTERFACE = 0,
	LIBUSB_CLASS_AUDIO = 1,
	LIBUSB_CLASS_COMM = 2,
	LIBUSB_CLASS_HID = 3,
	LIBUSB_CLASS_PTP = 6,
	LIBUSB_CLASS_IMAGE = 6,
	LIBUSB_CLASS_PRINTER = 7,
	LIBUSB_CLASS_MASS_STORAGE = 8,
	LIBUSB_CLASS_HUB = 9,
	LIBUSB_CLASS_DATA = 10,
	LIBUSB_CLASS_SMART_CARD = 11,
	LIBUSB_CLASS_CONTENT_SECURITY = 13,
	LIBUSB_CLASS_VIDEO = 14,
	LIBUSB_CLASS_PERSONAL_HEALTHCARE = 15,
	LIBUSB_CLASS_DIAGNOSTIC_DEVICE = 0xdc,
	LIBUSB_CLASS_WIRELESS = 0xe0,
	LIBUSB_CLASS_APPLICATION = 0xfe,
	LIBUSB_CLASS_VENDOR_SPEC = 0xff,
};

enum libusb_descriptor_type {
	LIBUSB_DT_DEVICE = 0x01,
	LIBUSB_DT_CONFIG = 0x02,
	LIBUSB_DT_STRING = 0x03,
	LIBUSB_DT_INTERFACE = 0x04,
	LIBUSB_DT_ENDPOINT = 0x05,
	LIBUSB_DT_HID = 0x21,
	LIBUSB_DT_REPORT = 0x22,
	LIBUSB_DT_PHYSICAL = 0x23,
	LIBUSB_DT_HUB = 0x29,
	LIBUSB_DT_BOS = 0x0f,
	LIBUSB_DT_DEVICE_CAPABILITY = 0x10,
	LIBUSB_DT_SS_ENDPOINT_COMPANION = 0x30,
};

enum libusb_device_capability_type {
	LIBUSB_WIRELESS_USB_DEVICE_CAPABILITY = 0x1,
	LIBUSB_USB_2_0_EXTENSION_DEVICE_CAPABILITY = 0x2,
	LIBUSB_SS_USB_DEVICE_CAPABILITY = 0x3,
	LIBUSB_CONTAINER_ID_DEVICE_CAPABILITY = 0x4,
};

#define	LIBUSB_DT_DEVICE_SIZE		18
#define	LIBUSB_DT_CONFIG_SIZE		9
#define	LIBUSB_DT_INTERFACE_SIZE	9
#define	LIBUSB_DT_ENDPOINT_SIZE		7
#define	LIBUSB_DT_ENDPOINT_AUDIO_SIZE	9
#define	LIBUSB_DT_HUB_NONVAR_SIZE	7
#define	LIBUSB_DT_SS_ENDPOINT_COMPANION_SIZE	6
#define	LIBUSB_DT_BOS_SIZE		5
#define	LIBUSB_USB_2_0_EXTENSION_DEVICE_CAPABILITY_SIZE	7
#define	LIBUSB_SS_USB_DEVICE_CAPABILITY_SIZE	10

#define	LIBUSB_BT_USB_2_0_EXTENSION_SIZE	7
#define	LIBUSB_BT_SS_USB_DEVICE_CAPABILITY_SIZE	10
#define	LIBUSB_BT_CONTAINER_ID_SIZE		20

#define	LIBUSB_ENDPOINT_ADDRESS_MASK	0x0f
#define	LIBUSB_ENDPOINT_DIR_MASK	0x80

enum libusb_endpoint_direction {
	LIBUSB_ENDPOINT_IN = 0x80,
	LIBUSB_ENDPOINT_OUT = 0x00,
};

#define	LIBUSB_TRANSFER_TYPE_MASK	0x03

enum libusb_transfer_type {
	LIBUSB_TRANSFER_TYPE_CONTROL = 0,
	LIBUSB_TRANSFER_TYPE_ISOCHRONOUS = 1,
	LIBUSB_TRANSFER_TYPE_BULK = 2,
	LIBUSB_TRANSFER_TYPE_INTERRUPT = 3,
};

enum libusb_standard_request {
	LIBUSB_REQUEST_GET_STATUS = 0x00,
	LIBUSB_REQUEST_CLEAR_FEATURE = 0x01,
	LIBUSB_REQUEST_SET_FEATURE = 0x03,
	LIBUSB_REQUEST_SET_ADDRESS = 0x05,
	LIBUSB_REQUEST_GET_DESCRIPTOR = 0x06,
	LIBUSB_REQUEST_SET_DESCRIPTOR = 0x07,
	LIBUSB_REQUEST_GET_CONFIGURATION = 0x08,
	LIBUSB_REQUEST_SET_CONFIGURATION = 0x09,
	LIBUSB_REQUEST_GET_INTERFACE = 0x0A,
	LIBUSB_REQUEST_SET_INTERFACE = 0x0B,
	LIBUSB_REQUEST_SYNCH_FRAME = 0x0C,
	LIBUSB_REQUEST_SET_SEL = 0x30,
	LIBUSB_REQUEST_SET_ISOCH_DELAY = 0x31,
};

enum libusb_request_type {
	LIBUSB_REQUEST_TYPE_STANDARD = (0x00 << 5),
	LIBUSB_REQUEST_TYPE_CLASS = (0x01 << 5),
	LIBUSB_REQUEST_TYPE_VENDOR = (0x02 << 5),
	LIBUSB_REQUEST_TYPE_RESERVED = (0x03 << 5),
};

enum libusb_request_recipient {
	LIBUSB_RECIPIENT_DEVICE = 0x00,
	LIBUSB_RECIPIENT_INTERFACE = 0x01,
	LIBUSB_RECIPIENT_ENDPOINT = 0x02,
	LIBUSB_RECIPIENT_OTHER = 0x03,
};

#define	LIBUSB_ISO_SYNC_TYPE_MASK	0x0C

enum libusb_iso_sync_type {
	LIBUSB_ISO_SYNC_TYPE_NONE = 0,
	LIBUSB_ISO_SYNC_TYPE_ASYNC = 1,
	LIBUSB_ISO_SYNC_TYPE_ADAPTIVE = 2,
	LIBUSB_ISO_SYNC_TYPE_SYNC = 3,
};

#define	LIBUSB_ISO_USAGE_TYPE_MASK 0x30

enum libusb_iso_usage_type {
	LIBUSB_ISO_USAGE_TYPE_DATA = 0,
	LIBUSB_ISO_USAGE_TYPE_FEEDBACK = 1,
	LIBUSB_ISO_USAGE_TYPE_IMPLICIT = 2,
};

enum libusb_bos_type {
	LIBUSB_BT_WIRELESS_USB_DEVICE_CAPABILITY = 1,
	LIBUSB_BT_USB_2_0_EXTENSION = 2,
	LIBUSB_BT_SS_USB_DEVICE_CAPABILITY = 3,
	LIBUSB_BT_CONTAINER_ID = 4,
};

enum libusb_error {
	LIBUSB_SUCCESS = 0,
	LIBUSB_ERROR_IO = -1,
	LIBUSB_ERROR_INVALID_PARAM = -2,
	LIBUSB_ERROR_ACCESS = -3,
	LIBUSB_ERROR_NO_DEVICE = -4,
	LIBUSB_ERROR_NOT_FOUND = -5,
	LIBUSB_ERROR_BUSY = -6,
	LIBUSB_ERROR_TIMEOUT = -7,
	LIBUSB_ERROR_OVERFLOW = -8,
	LIBUSB_ERROR_PIPE = -9,
	LIBUSB_ERROR_INTERRUPTED = -10,
	LIBUSB_ERROR_NO_MEM = -11,
	LIBUSB_ERROR_NOT_SUPPORTED = -12,
	LIBUSB_ERROR_OTHER = -99,
};

enum libusb_speed {
	LIBUSB_SPEED_UNKNOWN = 0,
	LIBUSB_SPEED_LOW = 1,
	LIBUSB_SPEED_FULL = 2,
	LIBUSB_SPEED_HIGH = 3,
	LIBUSB_SPEED_SUPER = 4,
};

enum libusb_transfer_status {
	LIBUSB_TRANSFER_COMPLETED,
	LIBUSB_TRANSFER_ERROR,
	LIBUSB_TRANSFER_TIMED_OUT,
	LIBUSB_TRANSFER_CANCELLED,
	LIBUSB_TRANSFER_STALL,
	LIBUSB_TRANSFER_NO_DEVICE,
	LIBUSB_TRANSFER_OVERFLOW,
};

enum libusb_transfer_flags {
	LIBUSB_TRANSFER_SHORT_NOT_OK = 1 << 0,
	LIBUSB_TRANSFER_FREE_BUFFER = 1 << 1,
	LIBUSB_TRANSFER_FREE_TRANSFER = 1 << 2,
};

enum libusb_log_level {
       LIBUSB_LOG_LEVEL_NONE = 0,
       LIBUSB_LOG_LEVEL_ERROR,
       LIBUSB_LOG_LEVEL_WARNING,
       LIBUSB_LOG_LEVEL_INFO,
       LIBUSB_LOG_LEVEL_DEBUG
};

/* XXX */
/* libusb_set_debug should take parameters from libusb_log_level
 * above according to
 *   http://libusb.sourceforge.net/api-1.0/group__lib.html
 */
enum libusb_debug_level {
	LIBUSB_DEBUG_NO=0,
	LIBUSB_DEBUG_FUNCTION=1,
	LIBUSB_DEBUG_TRANSFER=2,
};

#define	LIBUSB_HOTPLUG_MATCH_ANY -1

typedef enum {
	LIBUSB_HOTPLUG_NO_FLAGS = 0,
	LIBUSB_HOTPLUG_ENUMERATE = 1 << 0,
} libusb_hotplug_flag;

typedef enum {
	LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED = 1,
	LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT = 2,
} libusb_hotplug_event;

/* libusb structures */

struct libusb_context;
struct libusb_device;
struct libusb_transfer;
struct libusb_device_handle;
struct libusb_hotplug_callback_handle_struct;

struct libusb_pollfd {
	int	fd;
	short	events;
};

struct libusb_version {
	const uint16_t major;
	const uint16_t minor;
	const uint16_t micro;
	const uint16_t nano;
	const char *rc;
	const char *describe;
};

typedef struct libusb_context libusb_context;
typedef struct libusb_device libusb_device;
typedef struct libusb_device_handle libusb_device_handle;
typedef struct libusb_pollfd libusb_pollfd;
typedef void (*libusb_pollfd_added_cb) (int fd, short events, void *user_data);
typedef void (*libusb_pollfd_removed_cb) (int fd, void *user_data);
typedef struct libusb_hotplug_callback_handle_struct *libusb_hotplug_callback_handle;

typedef struct libusb_device_descriptor {
	uint8_t	bLength;
	uint8_t	bDescriptorType;
	uint16_t bcdUSB;
	uint8_t	bDeviceClass;
	uint8_t	bDeviceSubClass;
	uint8_t	bDeviceProtocol;
	uint8_t	bMaxPacketSize0;
	uint16_t idVendor;
	uint16_t idProduct;
	uint16_t bcdDevice;
	uint8_t	iManufacturer;
	uint8_t	iProduct;
	uint8_t	iSerialNumber;
	uint8_t	bNumConfigurations;
}	libusb_device_descriptor;

typedef struct libusb_endpoint_descriptor {
	uint8_t	bLength;
	uint8_t	bDescriptorType;
	uint8_t	bEndpointAddress;
	uint8_t	bmAttributes;
	uint16_t wMaxPacketSize;
	uint8_t	bInterval;
	uint8_t	bRefresh;
	uint8_t	bSynchAddress;
	uint8_t *extra;
	int	extra_length;
}	libusb_endpoint_descriptor __aligned(sizeof(void *));

typedef struct libusb_ss_endpoint_companion_descriptor {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint8_t bMaxBurst;
	uint8_t bmAttributes;
	uint16_t wBytesPerInterval;
}	libusb_ss_endpoint_companion_descriptor __aligned(sizeof(void *));

typedef struct libusb_interface_descriptor {
	uint8_t	bLength;
	uint8_t	bDescriptorType;
	uint8_t	bInterfaceNumber;
	uint8_t	bAlternateSetting;
	uint8_t	bNumEndpoints;
	uint8_t	bInterfaceClass;
	uint8_t	bInterfaceSubClass;
	uint8_t	bInterfaceProtocol;
	uint8_t	iInterface;
	struct libusb_endpoint_descriptor *endpoint;
	uint8_t *extra;
	int	extra_length;
}	libusb_interface_descriptor __aligned(sizeof(void *));

typedef struct libusb_interface {
	struct libusb_interface_descriptor *altsetting;
	int	num_altsetting;
}	libusb_interface __aligned(sizeof(void *));

typedef struct libusb_config_descriptor {
	uint8_t	bLength;
	uint8_t	bDescriptorType;
	uint16_t wTotalLength;
	uint8_t	bNumInterfaces;
	uint8_t	bConfigurationValue;
	uint8_t	iConfiguration;
	uint8_t	bmAttributes;
	uint8_t	MaxPower;
	struct libusb_interface *interface;
	uint8_t *extra;
	int	extra_length;
}	libusb_config_descriptor __aligned(sizeof(void *));

typedef struct libusb_usb_2_0_device_capability_descriptor {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint8_t bDevCapabilityType;
	uint32_t bmAttributes;
#define LIBUSB_USB_2_0_CAPABILITY_LPM_SUPPORT  (1 << 1)
}	libusb_usb_2_0_device_capability_descriptor __aligned(sizeof(void *));

typedef struct libusb_ss_usb_device_capability_descriptor {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint8_t bDevCapabilityType;
	uint8_t bmAttributes;
#define LIBUSB_SS_USB_CAPABILITY_LPM_SUPPORT   (1 << 1)
	uint16_t wSpeedSupported;
#define LIBUSB_CAPABILITY_LOW_SPEED_OPERATION  (1)
#define LIBUSB_CAPABILITY_FULL_SPEED_OPERATION (1 << 1)
#define LIBUSB_CAPABILITY_HIGH_SPEED_OPERATION (1 << 2)
#define LIBUSB_CAPABILITY_5GBPS_OPERATION      (1 << 3)
	uint8_t bFunctionalitySupport;
	uint8_t bU1DevExitLat;
	uint16_t wU2DevExitLat;
}	libusb_ss_usb_device_capability_descriptor __aligned(sizeof(void *));

typedef struct libusb_bos_dev_capability_descriptor {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint8_t bDevCapabilityType;
	uint8_t dev_capability_data[0];
}	libusb_bos_dev_capability_descriptor __aligned(sizeof(void *));

typedef struct libusb_bos_descriptor {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint16_t wTotalLength;
	uint8_t bNumDeviceCapabilities;
	struct libusb_usb_2_0_device_capability_descriptor *usb_2_0_ext_cap;
	struct libusb_ss_usb_device_capability_descriptor *ss_usb_cap;
	struct libusb_bos_dev_capability_descriptor **dev_capability;
}	libusb_bos_descriptor __aligned(sizeof(void *));

typedef struct libusb_usb_2_0_extension_descriptor {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint8_t bDevCapabilityType;
	uint32_t bmAttributes;
}	libusb_usb_2_0_extension_descriptor __aligned(sizeof(void *));

typedef struct libusb_container_id_descriptor {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint8_t bDevCapabilityType;
	uint8_t	bReserved;
	uint8_t ContainerID[16];
}	libusb_container_id_descriptor __aligned(sizeof(void *));

typedef struct libusb_control_setup {
	uint8_t	bmRequestType;
	uint8_t	bRequest;
	uint16_t wValue;
	uint16_t wIndex;
	uint16_t wLength;
}	libusb_control_setup;

#define	LIBUSB_CONTROL_SETUP_SIZE	8	/* bytes */

typedef struct libusb_iso_packet_descriptor {
	uint32_t length;
	uint32_t actual_length;
	enum libusb_transfer_status status;
}	libusb_iso_packet_descriptor __aligned(sizeof(void *));

typedef void (*libusb_transfer_cb_fn) (struct libusb_transfer *transfer);

typedef struct libusb_transfer {
	libusb_device_handle *dev_handle;
	uint8_t	flags;
	uint8_t endpoint;
	uint8_t type;
	uint32_t timeout;
	enum libusb_transfer_status status;
	int	length;
	int	actual_length;
	libusb_transfer_cb_fn callback;
	void   *user_data;
	uint8_t *buffer;
	int	num_iso_packets;
	struct libusb_iso_packet_descriptor iso_packet_desc[0];
}	libusb_transfer __aligned(sizeof(void *));

/* Library initialisation */

void	libusb_set_debug(libusb_context * ctx, int level);
const struct libusb_version *libusb_get_version(void);
const char *libusb_strerror(int code);
const char *libusb_error_name(int code);
int	libusb_init(libusb_context ** context);
void	libusb_exit(struct libusb_context *ctx);

/* Device handling and enumeration */

ssize_t libusb_get_device_list(libusb_context * ctx, libusb_device *** list);
void	libusb_free_device_list(libusb_device ** list, int unref_devices);
uint8_t	libusb_get_bus_number(libusb_device * dev);
uint8_t	libusb_get_port_number(libusb_device * dev);
int	libusb_get_port_numbers(libusb_device *dev, uint8_t *buf, uint8_t bufsize);
int	libusb_get_port_path(libusb_context *ctx, libusb_device *dev, uint8_t *buf, uint8_t bufsize);
uint8_t	libusb_get_device_address(libusb_device * dev);
enum libusb_speed libusb_get_device_speed(libusb_device * dev);
int	libusb_clear_halt(libusb_device_handle *devh, uint8_t endpoint);
int	libusb_get_max_packet_size(libusb_device * dev, uint8_t endpoint);
int	libusb_get_max_iso_packet_size(libusb_device * dev, uint8_t endpoint);
libusb_device *libusb_ref_device(libusb_device * dev);
void	libusb_unref_device(libusb_device * dev);
int	libusb_open(libusb_device * dev, libusb_device_handle ** devh);
libusb_device_handle *libusb_open_device_with_vid_pid(libusb_context * ctx, uint16_t vendor_id, uint16_t product_id);
void	libusb_close(libusb_device_handle * devh);
libusb_device *libusb_get_device(libusb_device_handle * devh);
int	libusb_get_configuration(libusb_device_handle * devh, int *config);
int	libusb_set_configuration(libusb_device_handle * devh, int configuration);
int	libusb_claim_interface(libusb_device_handle * devh, int interface_number);
int	libusb_release_interface(libusb_device_handle * devh, int interface_number);
int	libusb_reset_device(libusb_device_handle * devh);
int	libusb_check_connected(libusb_device_handle * devh);
int 	libusb_kernel_driver_active(libusb_device_handle * devh, int interface);
int	libusb_get_driver_np(libusb_device_handle * devh, int interface, char *name, int namelen);
int	libusb_get_driver(libusb_device_handle * devh, int interface, char *name, int namelen);
int 	libusb_detach_kernel_driver_np(libusb_device_handle * devh, int interface);
int 	libusb_detach_kernel_driver(libusb_device_handle * devh, int interface);
int 	libusb_attach_kernel_driver(libusb_device_handle * devh, int interface);
int	libusb_set_auto_detach_kernel_driver(libusb_device_handle *dev, int enable);
int	libusb_set_interface_alt_setting(libusb_device_handle * devh, int interface_number, int alternate_setting);

/* USB Descriptors */

int	libusb_get_device_descriptor(libusb_device * dev, struct libusb_device_descriptor *desc);
int	libusb_get_active_config_descriptor(libusb_device * dev, struct libusb_config_descriptor **config);
int	libusb_get_config_descriptor(libusb_device * dev, uint8_t config_index, struct libusb_config_descriptor **config);
int	libusb_get_config_descriptor_by_value(libusb_device * dev, uint8_t bConfigurationValue, struct libusb_config_descriptor **config);
void	libusb_free_config_descriptor(struct libusb_config_descriptor *config);
int	libusb_get_ss_endpoint_companion_descriptor(struct libusb_context *ctx, const struct libusb_endpoint_descriptor *endpoint, struct libusb_ss_endpoint_companion_descriptor **ep_comp);
void	libusb_free_ss_endpoint_companion_descriptor(struct libusb_ss_endpoint_companion_descriptor *ep_comp);
int	libusb_get_string_descriptor(libusb_device_handle * devh, uint8_t desc_index, uint16_t langid, unsigned char *data, int length);
int	libusb_get_string_descriptor_ascii(libusb_device_handle * devh, uint8_t desc_index, uint8_t *data, int length);
int	libusb_get_descriptor(libusb_device_handle * devh, uint8_t desc_type, uint8_t desc_index, uint8_t *data, int length);
int	libusb_parse_ss_endpoint_comp(const void *buf, int len, struct libusb_ss_endpoint_companion_descriptor **ep_comp);
void	libusb_free_ss_endpoint_comp(struct libusb_ss_endpoint_companion_descriptor *ep_comp);
int	libusb_parse_bos_descriptor(const void *buf, int len, struct libusb_bos_descriptor **bos);
void	libusb_free_bos_descriptor(struct libusb_bos_descriptor *bos);
int	libusb_get_bos_descriptor(libusb_device_handle *handle, struct libusb_bos_descriptor **bos);
int	libusb_get_usb_2_0_extension_descriptor(struct libusb_context *ctx, struct libusb_bos_dev_capability_descriptor *dev_cap, struct libusb_usb_2_0_extension_descriptor **usb_2_0_extension);
void	libusb_free_usb_2_0_extension_descriptor(struct libusb_usb_2_0_extension_descriptor *usb_2_0_extension);
int	libusb_get_ss_usb_device_capability_descriptor(struct libusb_context *ctx, struct libusb_bos_dev_capability_descriptor *dev_cap, struct libusb_ss_usb_device_capability_descriptor **ss_usb_device_capability);
void	libusb_free_ss_usb_device_capability_descriptor(struct libusb_ss_usb_device_capability_descriptor *ss_usb_device_capability);
int	libusb_get_container_id_descriptor(struct libusb_context *ctx, struct libusb_bos_dev_capability_descriptor *dev_cap, struct libusb_container_id_descriptor **container_id);
void	libusb_free_container_id_descriptor(struct libusb_container_id_descriptor *container_id);

/* Asynchronous device I/O */

struct libusb_transfer *libusb_alloc_transfer(int iso_packets);
void	libusb_free_transfer(struct libusb_transfer *transfer);
int	libusb_submit_transfer(struct libusb_transfer *transfer);
int	libusb_cancel_transfer(struct libusb_transfer *transfer);
uint8_t *libusb_get_iso_packet_buffer(struct libusb_transfer *transfer, uint32_t index);
uint8_t *libusb_get_iso_packet_buffer_simple(struct libusb_transfer *transfer, uint32_t index);
void	libusb_set_iso_packet_lengths(struct libusb_transfer *transfer, uint32_t length);
uint8_t *libusb_control_transfer_get_data(struct libusb_transfer *transfer);
struct libusb_control_setup *libusb_control_transfer_get_setup(struct libusb_transfer *transfer);
void	libusb_fill_control_setup(uint8_t *buf, uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, uint16_t wLength);
void	libusb_fill_control_transfer(struct libusb_transfer *transfer, libusb_device_handle *devh, uint8_t *buf, libusb_transfer_cb_fn callback, void *user_data, uint32_t timeout);
void	libusb_fill_bulk_transfer(struct libusb_transfer *transfer, libusb_device_handle *devh, uint8_t endpoint, uint8_t *buf, int length, libusb_transfer_cb_fn callback, void *user_data, uint32_t timeout);
void	libusb_fill_interrupt_transfer(struct libusb_transfer *transfer, libusb_device_handle *devh, uint8_t endpoint, uint8_t *buf, int length, libusb_transfer_cb_fn callback, void *user_data, uint32_t timeout);
void	libusb_fill_iso_transfer(struct libusb_transfer *transfer, libusb_device_handle *devh, uint8_t endpoint, uint8_t *buf, int length, int npacket, libusb_transfer_cb_fn callback, void *user_data, uint32_t timeout);

/* Polling and timing */

int	libusb_try_lock_events(libusb_context * ctx);
void	libusb_lock_events(libusb_context * ctx);
void	libusb_unlock_events(libusb_context * ctx);
int	libusb_event_handling_ok(libusb_context * ctx);
int	libusb_event_handler_active(libusb_context * ctx);
void	libusb_lock_event_waiters(libusb_context * ctx);
void	libusb_unlock_event_waiters(libusb_context * ctx);
int	libusb_wait_for_event(libusb_context * ctx, struct timeval *tv);
int	libusb_handle_events_timeout_completed(libusb_context * ctx, struct timeval *tv, int *completed);
int	libusb_handle_events_completed(libusb_context * ctx, int *completed);
int	libusb_handle_events_timeout(libusb_context * ctx, struct timeval *tv);
int	libusb_handle_events(libusb_context * ctx);
int	libusb_handle_events_locked(libusb_context * ctx, struct timeval *tv);
int	libusb_get_next_timeout(libusb_context * ctx, struct timeval *tv);
void	libusb_set_pollfd_notifiers(libusb_context * ctx, libusb_pollfd_added_cb added_cb, libusb_pollfd_removed_cb removed_cb, void *user_data);
const struct libusb_pollfd **libusb_get_pollfds(libusb_context * ctx);

/* Synchronous device I/O */

int	libusb_control_transfer(libusb_device_handle * devh, uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, uint8_t *data, uint16_t wLength, uint32_t timeout);
int	libusb_bulk_transfer(libusb_device_handle * devh, uint8_t endpoint, uint8_t *data, int length, int *transferred, uint32_t timeout);
int	libusb_interrupt_transfer(libusb_device_handle * devh, uint8_t endpoint, uint8_t *data, int length, int *transferred, uint32_t timeout);

/* Byte-order */

uint16_t libusb_cpu_to_le16(uint16_t x);
uint16_t libusb_le16_to_cpu(uint16_t x);

/* Hotplug support */

typedef int (*libusb_hotplug_callback_fn)(libusb_context *ctx,
    libusb_device *device, libusb_hotplug_event event, void *user_data);

int	libusb_hotplug_register_callback(libusb_context *ctx, libusb_hotplug_event events, libusb_hotplug_flag flags, int vendor_id, int product_id, int dev_class, libusb_hotplug_callback_fn cb_fn, void *user_data, libusb_hotplug_callback_handle *handle);
void	libusb_hotplug_deregister_callback(libusb_context *ctx, libusb_hotplug_callback_handle handle);

/* Streams support */

int	libusb_alloc_streams(libusb_device_handle *dev, uint32_t num_streams, unsigned char *endpoints, int num_endpoints);
int	libusb_free_streams(libusb_device_handle *dev, unsigned char *endpoints, int num_endpoints);
void	libusb_transfer_set_stream_id(struct libusb_transfer *transfer, uint32_t stream_id);
uint32_t libusb_transfer_get_stream_id(struct libusb_transfer *transfer);

#if 0
{					/* indent fix */
#endif
#ifdef __cplusplus
}

#endif

#endif					/* __LIBUSB_H__ */
