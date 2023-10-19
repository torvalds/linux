/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * USB Raw Gadget driver.
 *
 * See Documentation/usb/raw-gadget.rst for more details.
 */

#ifndef _UAPI__LINUX_USB_RAW_GADGET_H
#define _UAPI__LINUX_USB_RAW_GADGET_H

#include <asm/ioctl.h>
#include <linux/types.h>
#include <linux/usb/ch9.h>

/* Maximum length of driver_name/device_name in the usb_raw_init struct. */
#define UDC_NAME_LENGTH_MAX 128

/*
 * struct usb_raw_init - argument for USB_RAW_IOCTL_INIT ioctl.
 * @speed: The speed of the emulated USB device, takes the same values as
 *     the usb_device_speed enum: USB_SPEED_FULL, USB_SPEED_HIGH, etc.
 * @driver_name: The name of the UDC driver.
 * @device_name: The name of a UDC instance.
 *
 * The last two fields identify a UDC the gadget driver should bind to.
 * For example, Dummy UDC has "dummy_udc" as its driver_name and "dummy_udc.N"
 * as its device_name, where N in the index of the Dummy UDC instance.
 * At the same time the dwc2 driver that is used on Raspberry Pi Zero, has
 * "20980000.usb" as both driver_name and device_name.
 */
struct usb_raw_init {
	__u8	driver_name[UDC_NAME_LENGTH_MAX];
	__u8	device_name[UDC_NAME_LENGTH_MAX];
	__u8	speed;
};

/* The type of event fetched with the USB_RAW_IOCTL_EVENT_FETCH ioctl. */
enum usb_raw_event_type {
	USB_RAW_EVENT_INVALID = 0,

	/* This event is queued when the driver has bound to a UDC. */
	USB_RAW_EVENT_CONNECT = 1,

	/* This event is queued when a new control request arrived to ep0. */
	USB_RAW_EVENT_CONTROL = 2,

	/* The list might grow in the future. */
};

/*
 * struct usb_raw_event - argument for USB_RAW_IOCTL_EVENT_FETCH ioctl.
 * @type: The type of the fetched event.
 * @length: Length of the data buffer. Updated by the driver and set to the
 *     actual length of the fetched event data.
 * @data: A buffer to store the fetched event data.
 *
 * Currently the fetched data buffer is empty for USB_RAW_EVENT_CONNECT,
 * and contains struct usb_ctrlrequest for USB_RAW_EVENT_CONTROL.
 */
struct usb_raw_event {
	__u32		type;
	__u32		length;
	__u8		data[];
};

#define USB_RAW_IO_FLAGS_ZERO	0x0001
#define USB_RAW_IO_FLAGS_MASK	0x0001

static inline int usb_raw_io_flags_valid(__u16 flags)
{
	return (flags & ~USB_RAW_IO_FLAGS_MASK) == 0;
}

static inline int usb_raw_io_flags_zero(__u16 flags)
{
	return (flags & USB_RAW_IO_FLAGS_ZERO);
}

/*
 * struct usb_raw_ep_io - argument for USB_RAW_IOCTL_EP0/EP_WRITE/READ ioctls.
 * @ep: Endpoint handle as returned by USB_RAW_IOCTL_EP_ENABLE for
 *     USB_RAW_IOCTL_EP_WRITE/READ. Ignored for USB_RAW_IOCTL_EP0_WRITE/READ.
 * @flags: When USB_RAW_IO_FLAGS_ZERO is specified, the zero flag is set on
 *     the submitted USB request, see include/linux/usb/gadget.h for details.
 * @length: Length of data.
 * @data: Data to send for USB_RAW_IOCTL_EP0/EP_WRITE. Buffer to store received
 *     data for USB_RAW_IOCTL_EP0/EP_READ.
 */
struct usb_raw_ep_io {
	__u16		ep;
	__u16		flags;
	__u32		length;
	__u8		data[];
};

/* Maximum number of non-control endpoints in struct usb_raw_eps_info. */
#define USB_RAW_EPS_NUM_MAX	30

/* Maximum length of UDC endpoint name in struct usb_raw_ep_info. */
#define USB_RAW_EP_NAME_MAX	16

/* Used as addr in struct usb_raw_ep_info if endpoint accepts any address. */
#define USB_RAW_EP_ADDR_ANY	0xff

/*
 * struct usb_raw_ep_caps - exposes endpoint capabilities from struct usb_ep
 *     (technically from its member struct usb_ep_caps).
 */
struct usb_raw_ep_caps {
	__u32	type_control	: 1;
	__u32	type_iso	: 1;
	__u32	type_bulk	: 1;
	__u32	type_int	: 1;
	__u32	dir_in		: 1;
	__u32	dir_out		: 1;
};

/*
 * struct usb_raw_ep_limits - exposes endpoint limits from struct usb_ep.
 * @maxpacket_limit: Maximum packet size value supported by this endpoint.
 * @max_streams: maximum number of streams supported by this endpoint
 *     (actual number is 2^n).
 * @reserved: Empty, reserved for potential future extensions.
 */
struct usb_raw_ep_limits {
	__u16	maxpacket_limit;
	__u16	max_streams;
	__u32	reserved;
};

/*
 * struct usb_raw_ep_info - stores information about a gadget endpoint.
 * @name: Name of the endpoint as it is defined in the UDC driver.
 * @addr: Address of the endpoint that must be specified in the endpoint
 *     descriptor passed to USB_RAW_IOCTL_EP_ENABLE ioctl.
 * @caps: Endpoint capabilities.
 * @limits: Endpoint limits.
 */
struct usb_raw_ep_info {
	__u8				name[USB_RAW_EP_NAME_MAX];
	__u32				addr;
	struct usb_raw_ep_caps		caps;
	struct usb_raw_ep_limits	limits;
};

/*
 * struct usb_raw_eps_info - argument for USB_RAW_IOCTL_EPS_INFO ioctl.
 * eps: Structures that store information about non-control endpoints.
 */
struct usb_raw_eps_info {
	struct usb_raw_ep_info	eps[USB_RAW_EPS_NUM_MAX];
};

/*
 * Initializes a Raw Gadget instance.
 * Accepts a pointer to the usb_raw_init struct as an argument.
 * Returns 0 on success or negative error code on failure.
 */
#define USB_RAW_IOCTL_INIT		_IOW('U', 0, struct usb_raw_init)

/*
 * Instructs Raw Gadget to bind to a UDC and start emulating a USB device.
 * Returns 0 on success or negative error code on failure.
 */
#define USB_RAW_IOCTL_RUN		_IO('U', 1)

/*
 * A blocking ioctl that waits for an event and returns fetched event data to
 * the user.
 * Accepts a pointer to the usb_raw_event struct.
 * Returns 0 on success or negative error code on failure.
 */
#define USB_RAW_IOCTL_EVENT_FETCH	_IOR('U', 2, struct usb_raw_event)

/*
 * Queues an IN (OUT for READ) request as a response to the last setup request
 * received on endpoint 0 (provided that was an IN (OUT for READ) request), and
 * waits until the request is completed. Copies received data to user for READ.
 * Accepts a pointer to the usb_raw_ep_io struct as an argument.
 * Returns length of transferred data on success or negative error code on
 * failure.
 */
#define USB_RAW_IOCTL_EP0_WRITE		_IOW('U', 3, struct usb_raw_ep_io)
#define USB_RAW_IOCTL_EP0_READ		_IOWR('U', 4, struct usb_raw_ep_io)

/*
 * Finds an endpoint that satisfies the parameters specified in the provided
 * descriptors (address, transfer type, etc.) and enables it.
 * Accepts a pointer to the usb_raw_ep_descs struct as an argument.
 * Returns enabled endpoint handle on success or negative error code on failure.
 */
#define USB_RAW_IOCTL_EP_ENABLE		_IOW('U', 5, struct usb_endpoint_descriptor)

/*
 * Disables specified endpoint.
 * Accepts endpoint handle as an argument.
 * Returns 0 on success or negative error code on failure.
 */
#define USB_RAW_IOCTL_EP_DISABLE	_IOW('U', 6, __u32)

/*
 * Queues an IN (OUT for READ) request as a response to the last setup request
 * received on endpoint usb_raw_ep_io.ep (provided that was an IN (OUT for READ)
 * request), and waits until the request is completed. Copies received data to
 * user for READ.
 * Accepts a pointer to the usb_raw_ep_io struct as an argument.
 * Returns length of transferred data on success or negative error code on
 * failure.
 */
#define USB_RAW_IOCTL_EP_WRITE		_IOW('U', 7, struct usb_raw_ep_io)
#define USB_RAW_IOCTL_EP_READ		_IOWR('U', 8, struct usb_raw_ep_io)

/*
 * Switches the gadget into the configured state.
 * Returns 0 on success or negative error code on failure.
 */
#define USB_RAW_IOCTL_CONFIGURE		_IO('U', 9)

/*
 * Constrains UDC VBUS power usage.
 * Accepts current limit in 2 mA units as an argument.
 * Returns 0 on success or negative error code on failure.
 */
#define USB_RAW_IOCTL_VBUS_DRAW		_IOW('U', 10, __u32)

/*
 * Fills in the usb_raw_eps_info structure with information about non-control
 * endpoints available for the currently connected UDC.
 * Returns the number of available endpoints on success or negative error code
 * on failure.
 */
#define USB_RAW_IOCTL_EPS_INFO		_IOR('U', 11, struct usb_raw_eps_info)

/*
 * Stalls a pending control request on endpoint 0.
 * Returns 0 on success or negative error code on failure.
 */
#define USB_RAW_IOCTL_EP0_STALL		_IO('U', 12)

/*
 * Sets or clears halt or wedge status of the endpoint.
 * Accepts endpoint handle as an argument.
 * Returns 0 on success or negative error code on failure.
 */
#define USB_RAW_IOCTL_EP_SET_HALT	_IOW('U', 13, __u32)
#define USB_RAW_IOCTL_EP_CLEAR_HALT	_IOW('U', 14, __u32)
#define USB_RAW_IOCTL_EP_SET_WEDGE	_IOW('U', 15, __u32)

#endif /* _UAPI__LINUX_USB_RAW_GADGET_H */
