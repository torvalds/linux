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

#ifndef _USB_DEVICE_H_
#define	_USB_DEVICE_H_

#ifndef USB_GLOBAL_INCLUDE_FILE
#include <dev/usb/usb_core.h>
#include <dev/usb/usb_busdma.h>
#include <dev/usb/usb_transfer.h>
#endif

struct usb_bus_methods;
struct usb_config_descriptor;
struct usb_device;		/* linux compat */
struct usb_fs_privdata;
struct usb_hw_ep_profile;
struct usb_symlink;		/* UGEN */

#define	USB_CTRL_XFER_MAX 2

/* "usb_config_parse()" commands */

#define	USB_CFG_ALLOC 0
#define	USB_CFG_FREE 1
#define	USB_CFG_INIT 2

/* "usb_unconfigure()" flags */

#define	USB_UNCFG_FLAG_NONE 0x00
#define	USB_UNCFG_FLAG_FREE_EP0	0x02		/* endpoint zero is freed */

struct usb_udev_msg {
	struct usb_proc_msg hdr;
	struct usb_device *udev;
};

/* The following four structures makes up a tree, where we have the
 * leaf structure, "usb_host_endpoint", first, and the root structure,
 * "usb_device", last. The four structures below mirror the structure
 * of the USB descriptors belonging to an USB configuration. Please
 * refer to the USB specification for a definition of "endpoints" and
 * "interfaces".
 */
struct usb_host_endpoint {
	struct usb_endpoint_descriptor desc;
	TAILQ_HEAD(, urb) bsd_urb_list;
	struct usb_xfer *bsd_xfer[2];
	uint8_t *extra;			/* Extra descriptors */
	usb_frlength_t fbsd_buf_size;
	uint16_t extralen;
	uint8_t	bsd_iface_index;
} __aligned(USB_HOST_ALIGN);

struct usb_host_interface {
	struct usb_interface_descriptor desc;
	/* the following array has size "desc.bNumEndpoint" */
	struct usb_host_endpoint *endpoint;
	const char *string;		/* iInterface string, if present */
	uint8_t *extra;			/* Extra descriptors */
	uint16_t extralen;
	uint8_t	bsd_iface_index;
} __aligned(USB_HOST_ALIGN);

/*
 * The following structure defines the USB device flags.
 */
struct usb_device_flags {
	enum usb_hc_mode usb_mode;	/* host or device mode */
	uint8_t	self_powered:1;		/* set if USB device is self powered */
	uint8_t	no_strings:1;		/* set if USB device does not support
					 * strings */
	uint8_t	remote_wakeup:1;	/* set if remote wakeup is enabled */
	uint8_t	uq_bus_powered:1;	/* set if BUS powered quirk is present */

	/*
	 * NOTE: Although the flags below will reach the same value
	 * over time, but the instant values may differ, and
	 * consequently the flags cannot be merged into one!
	 */
	uint8_t peer_suspended:1;	/* set if peer is suspended */
	uint8_t self_suspended:1;	/* set if self is suspended */
};

/*
 * The following structure is used for power-save purposes. The data
 * in this structure is protected by the USB BUS lock.
 */
struct usb_power_save {
	usb_ticks_t last_xfer_time;	/* copy of "ticks" */
	usb_size_t type_refs[4];	/* transfer reference count */
	usb_size_t read_refs;		/* data read references */
	usb_size_t write_refs;		/* data write references */
};

/*
 * The following structure is used when trying to allocate hardware
 * endpoints for an USB configuration in USB device side mode.
 */
struct usb_hw_ep_scratch_sub {
	const struct usb_hw_ep_profile *pf;
	uint16_t max_frame_size;
	uint8_t	hw_endpoint_out;
	uint8_t	hw_endpoint_in;
	uint8_t	needs_ep_type;
	uint8_t	needs_in:1;
	uint8_t	needs_out:1;
};

/*
 * The following structure is used when trying to allocate hardware
 * endpoints for an USB configuration in USB device side mode.
 */
struct usb_hw_ep_scratch {
	struct usb_hw_ep_scratch_sub ep[USB_EP_MAX];
	struct usb_hw_ep_scratch_sub *ep_max;
	struct usb_config_descriptor *cd;
	struct usb_device *udev;
	const struct usb_bus_methods *methods;
	uint8_t	bmOutAlloc[(USB_EP_MAX + 15) / 16];
	uint8_t	bmInAlloc[(USB_EP_MAX + 15) / 16];
};

/*
 * The following structure is used when generating USB descriptors
 * from USB templates.
 */
struct usb_temp_setup {
	void   *buf;
	usb_size_t size;
	enum usb_dev_speed	usb_speed;
	uint8_t	self_powered;
	uint8_t	bNumEndpoints;
	uint8_t	bInterfaceNumber;
	uint8_t	bAlternateSetting;
	uint8_t	bConfigurationValue;
	usb_error_t err;
};

/* 
 * The scratch area for USB devices. Access to this structure is
 * protected by the control SX lock.
 */
union usb_device_scratch {
	struct usb_hw_ep_scratch hw_ep_scratch[1];
	struct usb_temp_setup temp_setup[1];
	struct {
		struct usb_xfer dummy;
		struct usb_setup_params parm;
	} xfer_setup[1];
	uint8_t	data[255];
};

/*
 * The following structure defines an USB device. There exists one of
 * these structures for every USB device.
 */
struct usb_device {
	/* generic clear stall message */
	struct usb_udev_msg cs_msg[2];
	struct sx enum_sx;
	struct sx sr_sx;
  	struct sx ctrl_sx;
	struct mtx device_mtx;
	struct cv ctrlreq_cv;
	struct cv ref_cv;
#if (USB_HAVE_FIXED_IFACE == 0)
	struct usb_interface *ifaces;
#else
	struct usb_interface ifaces[USB_IFACE_MAX];
#endif
	struct usb_endpoint ctrl_ep;	/* Control Endpoint 0 */
#if (USB_HAVE_FIXED_ENDPOINT == 0)
	struct usb_endpoint *endpoints;
#else
	struct usb_endpoint endpoints[USB_MAX_EP_UNITS];
#endif
	struct usb_power_save pwr_save;/* power save data */
	struct usb_bus *bus;		/* our USB BUS */
	device_t parent_dev;		/* parent device */
	struct usb_device *parent_hub;
	struct usb_device *parent_hs_hub;	/* high-speed parent HUB */
	struct usb_config_descriptor *cdesc;	/* full config descr */
	struct usb_hub *hub;		/* only if this is a hub */
	struct usb_xfer *ctrl_xfer[USB_CTRL_XFER_MAX];
	struct usb_temp_data *usb_template_ptr;
	struct usb_endpoint *ep_curr;	/* current clear stall endpoint */
#if USB_HAVE_UGEN
	struct usb_fifo *fifo[USB_FIFO_MAX];
	struct usb_symlink *ugen_symlink;	/* our generic symlink */
	struct usb_fs_privdata *ctrl_dev;	/* Control Endpoint 0 device node */
	LIST_HEAD(,usb_fs_privdata) pd_list;
	char	ugen_name[20];		/* name of ugenX.X device */
#endif
	usb_ticks_t plugtime;		/* copy of "ticks" */

	enum usb_dev_state state;
	enum usb_dev_speed speed;
	uint16_t refcount;
#define	USB_DEV_REF_MAX 0xffff

	uint16_t power;			/* mA the device uses */
	uint16_t langid;		/* language for strings */
	uint16_t autoQuirk[USB_MAX_AUTO_QUIRK];		/* dynamic quirks */

	uint8_t	address;		/* device addess */
	uint8_t	device_index;		/* device index in "bus->devices" */
	uint8_t	controller_slot_id;	/* controller specific value */
	uint8_t next_config_index;	/* used by USB_RE_ENUM_SET_CONFIG */
	uint8_t	curr_config_index;	/* current configuration index */
	uint8_t	curr_config_no;		/* current configuration number */
	uint8_t	depth;			/* distance from root HUB */
	uint8_t	port_index;		/* parent HUB port index */
	uint8_t	port_no;		/* parent HUB port number */
	uint8_t	hs_hub_addr;		/* high-speed HUB address */
	uint8_t	hs_port_no;		/* high-speed HUB port number */
	uint8_t	driver_added_refcount;	/* our driver added generation count */
	uint8_t	power_mode;		/* see USB_POWER_XXX */
	uint8_t re_enumerate_wait;	/* set if re-enum. is in progress */
#define	USB_RE_ENUM_DONE	0
#define	USB_RE_ENUM_START	1
#define	USB_RE_ENUM_PWR_OFF	2
#define	USB_RE_ENUM_SET_CONFIG	3
	uint8_t ifaces_max;		/* number of interfaces present */
	uint8_t endpoints_max;		/* number of endpoints present */

	/* the "flags" field is write-protected by "bus->mtx" */

	struct usb_device_flags flags;

	struct usb_endpoint_descriptor ctrl_ep_desc;	/* for endpoint 0 */
	struct usb_endpoint_ss_comp_descriptor ctrl_ep_comp_desc;	/* for endpoint 0 */
	struct usb_device_descriptor ddesc;	/* device descriptor */

	char	*serial;		/* serial number, can be NULL */
	char	*manufacturer;		/* manufacturer string, can be NULL */
	char	*product;		/* product string, can be NULL */

#if USB_HAVE_COMPAT_LINUX
	/* Linux compat */
	struct usb_device_descriptor descriptor;
	struct usb_host_endpoint ep0;
	struct usb_interface *linux_iface_start;
	struct usb_interface *linux_iface_end;
	struct usb_host_endpoint *linux_endpoint_start;
	struct usb_host_endpoint *linux_endpoint_end;
	uint16_t devnum;
#endif

	uint32_t clear_stall_errors;	/* number of clear-stall failures */

	union usb_device_scratch scratch;

#if (USB_HAVE_FIXED_CONFIG != 0)
	uint32_t config_data[(USB_CONFIG_MAX + 3) / 4];
#endif
};

/* globals */

extern int usb_template;

/* function prototypes */

const char *usb_statestr(enum usb_dev_state state);
struct usb_device *usb_alloc_device(device_t parent_dev, struct usb_bus *bus,
		    struct usb_device *parent_hub, uint8_t depth,
		    uint8_t port_index, uint8_t port_no,
		    enum usb_dev_speed speed, enum usb_hc_mode mode);
#if USB_HAVE_UGEN
struct usb_fs_privdata *usb_make_dev(struct usb_device *, const char *,
		    int, int, int, uid_t, gid_t, int);
void	usb_destroy_dev(struct usb_fs_privdata *);
void	usb_destroy_dev_sync(struct usb_fs_privdata *);
#endif
usb_error_t	usb_probe_and_attach(struct usb_device *udev,
		    uint8_t iface_index);
void		usb_detach_device(struct usb_device *, uint8_t, uint8_t);
usb_error_t	usb_reset_iface_endpoints(struct usb_device *udev,
		    uint8_t iface_index);
usb_error_t	usbd_set_config_index(struct usb_device *udev, uint8_t index);
usb_error_t	usbd_set_endpoint_stall(struct usb_device *udev,
		    struct usb_endpoint *ep, uint8_t do_stall);
usb_error_t	usb_suspend_resume(struct usb_device *udev,
		    uint8_t do_suspend);
void	usb_devinfo(struct usb_device *udev, char *dst_ptr, uint16_t dst_len);
void	usb_free_device(struct usb_device *, uint8_t);
void	usb_linux_free_device(struct usb_device *dev);
uint8_t	usb_peer_can_wakeup(struct usb_device *udev);
struct usb_endpoint *usb_endpoint_foreach(struct usb_device *udev, struct usb_endpoint *ep);
void	usb_set_device_state(struct usb_device *, enum usb_dev_state);
enum usb_dev_state usb_get_device_state(struct usb_device *);

uint8_t	usbd_enum_lock(struct usb_device *);
#if USB_HAVE_UGEN
uint8_t	usbd_enum_lock_sig(struct usb_device *);
#endif
void	usbd_enum_unlock(struct usb_device *);
void	usbd_sr_lock(struct usb_device *);
void	usbd_sr_unlock(struct usb_device *);
uint8_t	usbd_ctrl_lock(struct usb_device *);
void	usbd_ctrl_unlock(struct usb_device *);
uint8_t usbd_enum_is_locked(struct usb_device *);

#if USB_HAVE_TT_SUPPORT
void	uhub_tt_buffer_reset_async_locked(struct usb_device *, struct usb_endpoint *);
#endif

uint8_t uhub_count_active_host_ports(struct usb_device *, enum usb_dev_speed);

#endif					/* _USB_DEVICE_H_ */
