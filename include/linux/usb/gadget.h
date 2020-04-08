// SPDX-License-Identifier: GPL-2.0
/*
 * <linux/usb/gadget.h>
 *
 * We call the USB code inside a Linux-based peripheral device a "gadget"
 * driver, except for the hardware-specific bus glue.  One USB host can
 * master many USB gadgets, but the gadgets are only slaved to one host.
 *
 *
 * (C) Copyright 2002-2004 by David Brownell
 * All Rights Reserved.
 *
 * This software is licensed under the GNU GPL version 2.
 */

#ifndef __LINUX_USB_GADGET_H
#define __LINUX_USB_GADGET_H

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/scatterlist.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <linux/usb/ch9.h>

#define UDC_TRACE_STR_MAX	512

struct usb_ep;

/**
 * struct usb_request - describes one i/o request
 * @buf: Buffer used for data.  Always provide this; some controllers
 *	only use PIO, or don't use DMA for some endpoints.
 * @dma: DMA address corresponding to 'buf'.  If you don't set this
 *	field, and the usb controller needs one, it is responsible
 *	for mapping and unmapping the buffer.
 * @sg: a scatterlist for SG-capable controllers.
 * @num_sgs: number of SG entries
 * @num_mapped_sgs: number of SG entries mapped to DMA (internal)
 * @length: Length of that data
 * @stream_id: The stream id, when USB3.0 bulk streams are being used
 * @no_interrupt: If true, hints that no completion irq is needed.
 *	Helpful sometimes with deep request queues that are handled
 *	directly by DMA controllers.
 * @zero: If true, when writing data, makes the last packet be "short"
 *     by adding a zero length packet as needed;
 * @short_not_ok: When reading data, makes short packets be
 *     treated as errors (queue stops advancing till cleanup).
 * @dma_mapped: Indicates if request has been mapped to DMA (internal)
 * @complete: Function called when request completes, so this request and
 *	its buffer may be re-used.  The function will always be called with
 *	interrupts disabled, and it must not sleep.
 *	Reads terminate with a short packet, or when the buffer fills,
 *	whichever comes first.  When writes terminate, some data bytes
 *	will usually still be in flight (often in a hardware fifo).
 *	Errors (for reads or writes) stop the queue from advancing
 *	until the completion function returns, so that any transfers
 *	invalidated by the error may first be dequeued.
 * @context: For use by the completion callback
 * @list: For use by the gadget driver.
 * @status: Reports completion code, zero or a negative errno.
 *	Normally, faults block the transfer queue from advancing until
 *	the completion callback returns.
 *	Code "-ESHUTDOWN" indicates completion caused by device disconnect,
 *	or when the driver disabled the endpoint.
 * @actual: Reports bytes transferred to/from the buffer.  For reads (OUT
 *	transfers) this may be less than the requested length.  If the
 *	short_not_ok flag is set, short reads are treated as errors
 *	even when status otherwise indicates successful completion.
 *	Note that for writes (IN transfers) some data bytes may still
 *	reside in a device-side FIFO when the request is reported as
 *	complete.
 * @udc_priv: Vendor private data in usage by the UDC.
 *
 * These are allocated/freed through the endpoint they're used with.  The
 * hardware's driver can add extra per-request data to the memory it returns,
 * which often avoids separate memory allocations (potential failures),
 * later when the request is queued.
 *
 * Request flags affect request handling, such as whether a zero length
 * packet is written (the "zero" flag), whether a short read should be
 * treated as an error (blocking request queue advance, the "short_not_ok"
 * flag), or hinting that an interrupt is not required (the "no_interrupt"
 * flag, for use with deep request queues).
 *
 * Bulk endpoints can use any size buffers, and can also be used for interrupt
 * transfers. interrupt-only endpoints can be much less functional.
 *
 * NOTE:  this is analogous to 'struct urb' on the host side, except that
 * it's thinner and promotes more pre-allocation.
 */

struct usb_request {
	void			*buf;
	unsigned		length;
	dma_addr_t		dma;

	struct scatterlist	*sg;
	unsigned		num_sgs;
	unsigned		num_mapped_sgs;

	unsigned		stream_id:16;
	unsigned		no_interrupt:1;
	unsigned		zero:1;
	unsigned		short_not_ok:1;
	unsigned		dma_mapped:1;

	void			(*complete)(struct usb_ep *ep,
					struct usb_request *req);
	void			*context;
	struct list_head	list;

	int			status;
	unsigned		actual;
	unsigned int		udc_priv;
};

/*
 * @buf_base_addr: Base pointer to buffer allocated for each GSI enabled EP.
 *     TRBs point to buffers that are split from this pool. The size of the
 *     buffer is num_bufs times buf_len. num_bufs and buf_len are determined
       based on desired performance and aggregation size.
 * @dma: DMA address corresponding to buf_base_addr.
 * @num_bufs: Number of buffers associated with the GSI enabled EP. This
 *     corresponds to the number of non-zlp TRBs allocated for the EP.
 *     The value is determined based on desired performance for the EP.
 * @buf_len: Size of each individual buffer is determined based on aggregation
 *     negotiated as per the protocol. In case of no aggregation supported by
 *     the protocol, we use default values.
 * @db_reg_phs_addr_lsb: IPA channel doorbell register's physical address LSB
 * @mapped_db_reg_phs_addr_lsb: doorbell LSB IOVA address mapped with IOMMU
 * @db_reg_phs_addr_msb: IPA channel doorbell register's physical address MSB
 */
struct usb_gsi_request {
	void *buf_base_addr;
	dma_addr_t dma;
	size_t num_bufs;
	size_t buf_len;
	u32 db_reg_phs_addr_lsb;
	dma_addr_t mapped_db_reg_phs_addr_lsb;
	u32 db_reg_phs_addr_msb;
	struct sg_table sgt_trb_xfer_ring;
	struct sg_table sgt_data_buff;
};

enum gsi_ep_op {
	GSI_EP_OP_CONFIG = 0,
	GSI_EP_OP_STARTXFER,
	GSI_EP_OP_STORE_DBL_INFO,
	GSI_EP_OP_ENABLE_GSI,
	GSI_EP_OP_UPDATEXFER,
	GSI_EP_OP_RING_DB,
	GSI_EP_OP_ENDXFER,
	GSI_EP_OP_GET_CH_INFO,
	GSI_EP_OP_GET_XFER_IDX,
	GSI_EP_OP_PREPARE_TRBS,
	GSI_EP_OP_FREE_TRBS,
	GSI_EP_OP_SET_CLR_BLOCK_DBL,
	GSI_EP_OP_CHECK_FOR_SUSPEND,
	GSI_EP_OP_DISABLE,
};

/*-------------------------------------------------------------------------*/

/* endpoint-specific parts of the api to the usb controller hardware.
 * unlike the urb model, (de)multiplexing layers are not required.
 * (so this api could slash overhead if used on the host side...)
 *
 * note that device side usb controllers commonly differ in how many
 * endpoints they support, as well as their capabilities.
 */
struct usb_ep_ops {
	int (*enable) (struct usb_ep *ep,
		const struct usb_endpoint_descriptor *desc);
	int (*disable) (struct usb_ep *ep);
	void (*dispose) (struct usb_ep *ep);

	struct usb_request *(*alloc_request) (struct usb_ep *ep,
		gfp_t gfp_flags);
	void (*free_request) (struct usb_ep *ep, struct usb_request *req);

	int (*queue) (struct usb_ep *ep, struct usb_request *req,
		gfp_t gfp_flags);
	int (*dequeue) (struct usb_ep *ep, struct usb_request *req);

	int (*set_halt) (struct usb_ep *ep, int value);
	int (*set_wedge) (struct usb_ep *ep);

	int (*fifo_status) (struct usb_ep *ep);
	void (*fifo_flush) (struct usb_ep *ep);
	int (*gsi_ep_op) (struct usb_ep *ep, void *op_data,
		enum gsi_ep_op op);

};

/**
 * struct usb_ep_caps - endpoint capabilities description
 * @type_control:Endpoint supports control type (reserved for ep0).
 * @type_iso:Endpoint supports isochronous transfers.
 * @type_bulk:Endpoint supports bulk transfers.
 * @type_int:Endpoint supports interrupt transfers.
 * @dir_in:Endpoint supports IN direction.
 * @dir_out:Endpoint supports OUT direction.
 */
struct usb_ep_caps {
	unsigned type_control:1;
	unsigned type_iso:1;
	unsigned type_bulk:1;
	unsigned type_int:1;
	unsigned dir_in:1;
	unsigned dir_out:1;
};

#define USB_EP_CAPS_TYPE_CONTROL     0x01
#define USB_EP_CAPS_TYPE_ISO         0x02
#define USB_EP_CAPS_TYPE_BULK        0x04
#define USB_EP_CAPS_TYPE_INT         0x08
#define USB_EP_CAPS_TYPE_ALL \
	(USB_EP_CAPS_TYPE_ISO | USB_EP_CAPS_TYPE_BULK | USB_EP_CAPS_TYPE_INT)
#define USB_EP_CAPS_DIR_IN           0x01
#define USB_EP_CAPS_DIR_OUT          0x02
#define USB_EP_CAPS_DIR_ALL  (USB_EP_CAPS_DIR_IN | USB_EP_CAPS_DIR_OUT)

#define USB_EP_CAPS(_type, _dir) \
	{ \
		.type_control = !!(_type & USB_EP_CAPS_TYPE_CONTROL), \
		.type_iso = !!(_type & USB_EP_CAPS_TYPE_ISO), \
		.type_bulk = !!(_type & USB_EP_CAPS_TYPE_BULK), \
		.type_int = !!(_type & USB_EP_CAPS_TYPE_INT), \
		.dir_in = !!(_dir & USB_EP_CAPS_DIR_IN), \
		.dir_out = !!(_dir & USB_EP_CAPS_DIR_OUT), \
	}

enum ep_type {
	EP_TYPE_NORMAL = 0,
	EP_TYPE_GSI,
};

/**
 * struct usb_ep - device side representation of USB endpoint
 * @name:identifier for the endpoint, such as "ep-a" or "ep9in-bulk"
 * @ops: Function pointers used to access hardware-specific operations.
 * @ep_list:the gadget's ep_list holds all of its endpoints
 * @caps:The structure describing types and directions supported by endoint.
 * @enabled: The current endpoint enabled/disabled state.
 * @claimed: True if this endpoint is claimed by a function.
 * @maxpacket:The maximum packet size used on this endpoint.  The initial
 *	value can sometimes be reduced (hardware allowing), according to
 *	the endpoint descriptor used to configure the endpoint.
 * @maxpacket_limit:The maximum packet size value which can be handled by this
 *	endpoint. It's set once by UDC driver when endpoint is initialized, and
 *	should not be changed. Should not be confused with maxpacket.
 * @max_streams: The maximum number of streams supported
 *	by this EP (0 - 16, actual number is 2^n)
 * @mult: multiplier, 'mult' value for SS Isoc EPs
 * @maxburst: the maximum number of bursts supported by this EP (for usb3)
 * @driver_data:for use by the gadget driver.
 * @address: used to identify the endpoint when finding descriptor that
 *	matches connection speed
 * @desc: endpoint descriptor.  This pointer is set before the endpoint is
 *	enabled and remains valid until the endpoint is disabled.
 * @comp_desc: In case of SuperSpeed support, this is the endpoint companion
 *	descriptor that is used to configure the endpoint
 * @ep_type: Used to specify type of EP eg. normal vs h/w accelerated.
 * @ep_num: Used EP number
 * @ep_intr_num: Interrupter number for EP.
 * @endless: In case where endless transfer is being initiated, this is set
 *      to disable usb event interrupt for few events.
 *
 * the bus controller driver lists all the general purpose endpoints in
 * gadget->ep_list.  the control endpoint (gadget->ep0) is not in that list,
 * and is accessed only in response to a driver setup() callback.
 */

struct usb_ep {
	void			*driver_data;

	const char		*name;
	const struct usb_ep_ops	*ops;
	struct list_head	ep_list;
	struct usb_ep_caps	caps;
	bool			claimed;
	bool			enabled;
	unsigned		maxpacket:16;
	unsigned		maxpacket_limit:16;
	unsigned		max_streams:16;
	unsigned		mult:2;
	unsigned		maxburst:5;
	u8			address;
	const struct usb_endpoint_descriptor	*desc;
	const struct usb_ss_ep_comp_descriptor	*comp_desc;
	enum ep_type            ep_type;
	u8                      ep_num;
	u8                      ep_intr_num;
	bool                    endless;
};

/*-------------------------------------------------------------------------*/

#if IS_ENABLED(CONFIG_USB_GADGET)
void usb_ep_set_maxpacket_limit(struct usb_ep *ep, unsigned maxpacket_limit);
int usb_ep_enable(struct usb_ep *ep);
int usb_ep_disable(struct usb_ep *ep);
struct usb_request *usb_ep_alloc_request(struct usb_ep *ep, gfp_t gfp_flags);
void usb_ep_free_request(struct usb_ep *ep, struct usb_request *req);
int usb_ep_queue(struct usb_ep *ep, struct usb_request *req, gfp_t gfp_flags);
int usb_ep_dequeue(struct usb_ep *ep, struct usb_request *req);
int usb_ep_set_halt(struct usb_ep *ep);
int usb_ep_clear_halt(struct usb_ep *ep);
int usb_ep_set_wedge(struct usb_ep *ep);
int usb_ep_fifo_status(struct usb_ep *ep);
void usb_ep_fifo_flush(struct usb_ep *ep);
int usb_gsi_ep_op(struct usb_ep *ep,
		struct usb_gsi_request *req, enum gsi_ep_op op);
#else
static inline void usb_ep_set_maxpacket_limit(struct usb_ep *ep,
		unsigned maxpacket_limit)
{ }
static inline int usb_ep_enable(struct usb_ep *ep)
{ return 0; }
static inline int usb_ep_disable(struct usb_ep *ep)
{ return 0; }
static inline struct usb_request *usb_ep_alloc_request(struct usb_ep *ep,
		gfp_t gfp_flags)
{ return NULL; }
static inline void usb_ep_free_request(struct usb_ep *ep,
		struct usb_request *req)
{ }
static inline int usb_ep_queue(struct usb_ep *ep, struct usb_request *req,
		gfp_t gfp_flags)
{ return 0; }
static inline int usb_ep_dequeue(struct usb_ep *ep, struct usb_request *req)
{ return 0; }
static inline int usb_ep_set_halt(struct usb_ep *ep)
{ return 0; }
static inline int usb_ep_clear_halt(struct usb_ep *ep)
{ return 0; }
static inline int usb_ep_set_wedge(struct usb_ep *ep)
{ return 0; }
static inline int usb_ep_fifo_status(struct usb_ep *ep)
{ return 0; }
static inline void usb_ep_fifo_flush(struct usb_ep *ep)
{ }

static int usb_gsi_ep_op(struct usb_ep *ep,
		struct usb_gsi_request *req, enum gsi_ep_op op)
{ return 0; }
#endif /* USB_GADGET */

/*-------------------------------------------------------------------------*/

struct usb_dcd_config_params {
	__u8  bU1devExitLat;	/* U1 Device exit Latency */
#define USB_DEFAULT_U1_DEV_EXIT_LAT	0x01	/* Less then 1 microsec */
	__le16 bU2DevExitLat;	/* U2 Device exit Latency */
#define USB_DEFAULT_U2_DEV_EXIT_LAT	0x1F4	/* Less then 500 microsec */
};


struct usb_gadget;
struct usb_gadget_driver;
struct usb_udc;

/* the rest of the api to the controller hardware: device operations,
 * which don't involve endpoints (or i/o).
 */
struct usb_gadget_ops {
	int	(*get_frame)(struct usb_gadget *);
	int	(*wakeup)(struct usb_gadget *);
	int	(*func_wakeup)(struct usb_gadget *g, int interface_id);
	int	(*set_selfpowered) (struct usb_gadget *, int is_selfpowered);
	int	(*vbus_session) (struct usb_gadget *, int is_active);
	int	(*vbus_draw) (struct usb_gadget *, unsigned mA);
	int	(*pullup) (struct usb_gadget *, int is_on);
	int	(*ioctl)(struct usb_gadget *,
				unsigned code, unsigned long param);
	void	(*get_config_params)(struct usb_dcd_config_params *);
	int	(*udc_start)(struct usb_gadget *,
			struct usb_gadget_driver *);
	int	(*udc_stop)(struct usb_gadget *);
	void	(*udc_set_speed)(struct usb_gadget *, enum usb_device_speed);
	struct usb_ep *(*match_ep)(struct usb_gadget *,
			struct usb_endpoint_descriptor *,
			struct usb_ss_ep_comp_descriptor *);
	int	(*restart)(struct usb_gadget *g);
};

/**
 * struct usb_gadget - represents a usb slave device
 * @work: (internal use) Workqueue to be used for sysfs_notify()
 * @udc: struct usb_udc pointer for this gadget
 * @ops: Function pointers used to access hardware-specific operations.
 * @ep0: Endpoint zero, used when reading or writing responses to
 *	driver setup() requests
 * @ep_list: List of other endpoints supported by the device.
 * @speed: Speed of current connection to USB host.
 * @max_speed: Maximal speed the UDC can handle.  UDC must support this
 *      and all slower speeds.
 * @state: the state we are now (attached, suspended, configured, etc)
 * @name: Identifies the controller hardware type.  Used in diagnostics
 *	and sometimes configuration.
 * @dev: Driver model state for this abstract device.
 * @isoch_delay: value from Set Isoch Delay request. Only valid on SS/SSP
 * @out_epnum: last used out ep number
 * @in_epnum: last used in ep number
 * @mA: last set mA value
 * @otg_caps: OTG capabilities of this gadget.
 * @sg_supported: true if we can handle scatter-gather
 * @is_otg: True if the USB device port uses a Mini-AB jack, so that the
 *	gadget driver must provide a USB OTG descriptor.
 * @is_a_peripheral: False unless is_otg, the "A" end of a USB cable
 *	is in the Mini-AB jack, and HNP has been used to switch roles
 *	so that the "A" device currently acts as A-Peripheral, not A-Host.
 * @a_hnp_support: OTG device feature flag, indicating that the A-Host
 *	supports HNP at this port.
 * @a_alt_hnp_support: OTG device feature flag, indicating that the A-Host
 *	only supports HNP on a different root port.
 * @b_hnp_enable: OTG device feature flag, indicating that the A-Host
 *	enabled HNP support.
 * @hnp_polling_support: OTG device feature flag, indicating if the OTG device
 *	in peripheral mode can support HNP polling.
 * @host_request_flag: OTG device feature flag, indicating if A-Peripheral
 *	or B-Peripheral wants to take host role.
 * @quirk_ep_out_aligned_size: epout requires buffer size to be aligned to
 *	MaxPacketSize.
 * @quirk_altset_not_supp: UDC controller doesn't support alt settings.
 * @quirk_stall_not_supp: UDC controller doesn't support stalling.
 * @quirk_zlp_not_supp: UDC controller doesn't support ZLP.
 * @quirk_avoids_skb_reserve: udc/platform wants to avoid skb_reserve() in
 *	u_ether.c to improve performance.
 * @is_selfpowered: if the gadget is self-powered.
 * @deactivated: True if gadget is deactivated - in deactivated state it cannot
 *	be connected.
 * @connected: True if gadget is connected.
 * @lpm_capable: If the gadget max_speed is FULL or HIGH, this flag
 *	indicates that it supports LPM as per the LPM ECN & errata.
 * @remote_wakeup: Indicates if the host has enabled the remote_wakeup
 * feature.
 *
 * Gadgets have a mostly-portable "gadget driver" implementing device
 * functions, handling all usb configurations and interfaces.  Gadget
 * drivers talk to hardware-specific code indirectly, through ops vectors.
 * That insulates the gadget driver from hardware details, and packages
 * the hardware endpoints through generic i/o queues.  The "usb_gadget"
 * and "usb_ep" interfaces provide that insulation from the hardware.
 *
 * Except for the driver data, all fields in this structure are
 * read-only to the gadget driver.  That driver data is part of the
 * "driver model" infrastructure in 2.6 (and later) kernels, and for
 * earlier systems is grouped in a similar structure that's not known
 * to the rest of the kernel.
 *
 * Values of the three OTG device feature flags are updated before the
 * setup() call corresponding to USB_REQ_SET_CONFIGURATION, and before
 * driver suspend() calls.  They are valid only when is_otg, and when the
 * device is acting as a B-Peripheral (so is_a_peripheral is false).
 */
struct usb_gadget {
	struct work_struct		work;
	struct usb_udc			*udc;
	/* readonly to gadget driver */
	const struct usb_gadget_ops	*ops;
	struct usb_ep			*ep0;
	struct list_head		ep_list;	/* of usb_ep */
	enum usb_device_speed		speed;
	enum usb_device_speed		max_speed;
	enum usb_device_state		state;
	const char			*name;
	struct device			dev;
	unsigned			isoch_delay;
	unsigned			out_epnum;
	unsigned			in_epnum;
	unsigned			mA;
	struct usb_otg_caps		*otg_caps;

	unsigned			sg_supported:1;
	unsigned			is_otg:1;
	unsigned			is_a_peripheral:1;
	unsigned			b_hnp_enable:1;
	unsigned			a_hnp_support:1;
	unsigned			a_alt_hnp_support:1;
	unsigned			hnp_polling_support:1;
	unsigned			host_request_flag:1;
	unsigned			quirk_ep_out_aligned_size:1;
	unsigned			quirk_altset_not_supp:1;
	unsigned			quirk_stall_not_supp:1;
	unsigned			quirk_zlp_not_supp:1;
	unsigned			quirk_avoids_skb_reserve:1;
	unsigned			is_selfpowered:1;
	unsigned			deactivated:1;
	unsigned			connected:1;
	unsigned			lpm_capable:1;
	unsigned			remote_wakeup:1;
};
#define work_to_gadget(w)	(container_of((w), struct usb_gadget, work))

static inline void set_gadget_data(struct usb_gadget *gadget, void *data)
	{ dev_set_drvdata(&gadget->dev, data); }
static inline void *get_gadget_data(struct usb_gadget *gadget)
	{ return dev_get_drvdata(&gadget->dev); }
static inline struct usb_gadget *dev_to_usb_gadget(struct device *dev)
{
	return container_of(dev, struct usb_gadget, dev);
}

/* iterates the non-control endpoints; 'tmp' is a struct usb_ep pointer */
#define gadget_for_each_ep(tmp, gadget) \
	list_for_each_entry(tmp, &(gadget)->ep_list, ep_list)

/**
 * usb_ep_align - returns @len aligned to ep's maxpacketsize.
 * @ep: the endpoint whose maxpacketsize is used to align @len
 * @len: buffer size's length to align to @ep's maxpacketsize
 *
 * This helper is used to align buffer's size to an ep's maxpacketsize.
 */
static inline size_t usb_ep_align(struct usb_ep *ep, size_t len)
{
	int max_packet_size = (size_t)usb_endpoint_maxp(ep->desc) & 0x7ff;

	return round_up(len, max_packet_size);
}

/**
 * usb_ep_align_maybe - returns @len aligned to ep's maxpacketsize if gadget
 *	requires quirk_ep_out_aligned_size, otherwise returns len.
 * @g: controller to check for quirk
 * @ep: the endpoint whose maxpacketsize is used to align @len
 * @len: buffer size's length to align to @ep's maxpacketsize
 *
 * This helper is used in case it's required for any reason to check and maybe
 * align buffer's size to an ep's maxpacketsize.
 */
static inline size_t
usb_ep_align_maybe(struct usb_gadget *g, struct usb_ep *ep, size_t len)
{
	return g->quirk_ep_out_aligned_size ? usb_ep_align(ep, len) : len;
}

/**
 * gadget_is_altset_supported - return true iff the hardware supports
 *	altsettings
 * @g: controller to check for quirk
 */
static inline int gadget_is_altset_supported(struct usb_gadget *g)
{
	return !g->quirk_altset_not_supp;
}

/**
 * gadget_is_stall_supported - return true iff the hardware supports stalling
 * @g: controller to check for quirk
 */
static inline int gadget_is_stall_supported(struct usb_gadget *g)
{
	return !g->quirk_stall_not_supp;
}

/**
 * gadget_is_zlp_supported - return true iff the hardware supports zlp
 * @g: controller to check for quirk
 */
static inline int gadget_is_zlp_supported(struct usb_gadget *g)
{
	return !g->quirk_zlp_not_supp;
}

/**
 * gadget_avoids_skb_reserve - return true iff the hardware would like to avoid
 *	skb_reserve to improve performance.
 * @g: controller to check for quirk
 */
static inline int gadget_avoids_skb_reserve(struct usb_gadget *g)
{
	return g->quirk_avoids_skb_reserve;
}

/**
 * gadget_is_dualspeed - return true iff the hardware handles high speed
 * @g: controller that might support both high and full speeds
 */
static inline int gadget_is_dualspeed(struct usb_gadget *g)
{
	return g->max_speed >= USB_SPEED_HIGH;
}

/**
 * gadget_is_superspeed() - return true if the hardware handles superspeed
 * @g: controller that might support superspeed
 */
static inline int gadget_is_superspeed(struct usb_gadget *g)
{
	return g->max_speed >= USB_SPEED_SUPER;
}

/**
 * gadget_is_superspeed_plus() - return true if the hardware handles
 *	superspeed plus
 * @g: controller that might support superspeed plus
 */
static inline int gadget_is_superspeed_plus(struct usb_gadget *g)
{
	return g->max_speed >= USB_SPEED_SUPER_PLUS;
}

/**
 * gadget_is_otg - return true iff the hardware is OTG-ready
 * @g: controller that might have a Mini-AB connector
 *
 * This is a runtime test, since kernels with a USB-OTG stack sometimes
 * run on boards which only have a Mini-B (or Mini-A) connector.
 */
static inline int gadget_is_otg(struct usb_gadget *g)
{
#ifdef CONFIG_USB_OTG
	return g->is_otg;
#else
	return 0;
#endif
}

/*-------------------------------------------------------------------------*/

#if IS_ENABLED(CONFIG_USB_GADGET)
int usb_gadget_frame_number(struct usb_gadget *gadget);
int usb_gadget_wakeup(struct usb_gadget *gadget);
int usb_gadget_func_wakeup(struct usb_gadget *gadget, int interface_id);
int usb_gadget_set_selfpowered(struct usb_gadget *gadget);
int usb_gadget_clear_selfpowered(struct usb_gadget *gadget);
int usb_gadget_vbus_connect(struct usb_gadget *gadget);
int usb_gadget_vbus_draw(struct usb_gadget *gadget, unsigned mA);
int usb_gadget_vbus_disconnect(struct usb_gadget *gadget);
int usb_gadget_connect(struct usb_gadget *gadget);
int usb_gadget_disconnect(struct usb_gadget *gadget);
int usb_gadget_deactivate(struct usb_gadget *gadget);
int usb_gadget_activate(struct usb_gadget *gadget);
#else
static inline int usb_gadget_frame_number(struct usb_gadget *gadget)
{ return 0; }
static inline int usb_gadget_wakeup(struct usb_gadget *gadget)
{ return 0; }
static int usb_gadget_func_wakeup(struct usb_gadget *gadget, int interface_id)
{ return 0; }
static inline int usb_gadget_set_selfpowered(struct usb_gadget *gadget)
{ return 0; }
static inline int usb_gadget_clear_selfpowered(struct usb_gadget *gadget)
{ return 0; }
static inline int usb_gadget_vbus_connect(struct usb_gadget *gadget)
{ return 0; }
static inline int usb_gadget_vbus_draw(struct usb_gadget *gadget, unsigned mA)
{ return 0; }
static inline int usb_gadget_vbus_disconnect(struct usb_gadget *gadget)
{ return 0; }
static inline int usb_gadget_connect(struct usb_gadget *gadget)
{ return 0; }
static inline int usb_gadget_disconnect(struct usb_gadget *gadget)
{ return 0; }
static inline int usb_gadget_deactivate(struct usb_gadget *gadget)
{ return 0; }
static inline int usb_gadget_activate(struct usb_gadget *gadget)
{ return 0; }
#endif /* CONFIG_USB_GADGET */

/*-------------------------------------------------------------------------*/

/**
 * struct usb_gadget_driver - driver for usb 'slave' devices
 * @function: String describing the gadget's function
 * @max_speed: Highest speed the driver handles.
 * @setup: Invoked for ep0 control requests that aren't handled by
 *	the hardware level driver. Most calls must be handled by
 *	the gadget driver, including descriptor and configuration
 *	management.  The 16 bit members of the setup data are in
 *	USB byte order. Called in_interrupt; this may not sleep.  Driver
 *	queues a response to ep0, or returns negative to stall.
 * @disconnect: Invoked after all transfers have been stopped,
 *	when the host is disconnected.  May be called in_interrupt; this
 *	may not sleep.  Some devices can't detect disconnect, so this might
 *	not be called except as part of controller shutdown.
 * @bind: the driver's bind callback
 * @unbind: Invoked when the driver is unbound from a gadget,
 *	usually from rmmod (after a disconnect is reported).
 *	Called in a context that permits sleeping.
 * @suspend: Invoked on USB suspend.  May be called in_interrupt.
 * @resume: Invoked on USB resume.  May be called in_interrupt.
 * @reset: Invoked on USB bus reset. It is mandatory for all gadget drivers
 *	and should be called in_interrupt.
 * @driver: Driver model state for this driver.
 * @udc_name: A name of UDC this driver should be bound to. If udc_name is NULL,
 *	this driver will be bound to any available UDC.
 * @pending: UDC core private data used for deferred probe of this driver.
 * @match_existing_only: If udc is not found, return an error and don't add this
 *      gadget driver to list of pending driver
 *
 * Devices are disabled till a gadget driver successfully bind()s, which
 * means the driver will handle setup() requests needed to enumerate (and
 * meet "chapter 9" requirements) then do some useful work.
 *
 * If gadget->is_otg is true, the gadget driver must provide an OTG
 * descriptor during enumeration, or else fail the bind() call.  In such
 * cases, no USB traffic may flow until both bind() returns without
 * having called usb_gadget_disconnect(), and the USB host stack has
 * initialized.
 *
 * Drivers use hardware-specific knowledge to configure the usb hardware.
 * endpoint addressing is only one of several hardware characteristics that
 * are in descriptors the ep0 implementation returns from setup() calls.
 *
 * Except for ep0 implementation, most driver code shouldn't need change to
 * run on top of different usb controllers.  It'll use endpoints set up by
 * that ep0 implementation.
 *
 * The usb controller driver handles a few standard usb requests.  Those
 * include set_address, and feature flags for devices, interfaces, and
 * endpoints (the get_status, set_feature, and clear_feature requests).
 *
 * Accordingly, the driver's setup() callback must always implement all
 * get_descriptor requests, returning at least a device descriptor and
 * a configuration descriptor.  Drivers must make sure the endpoint
 * descriptors match any hardware constraints. Some hardware also constrains
 * other descriptors. (The pxa250 allows only configurations 1, 2, or 3).
 *
 * The driver's setup() callback must also implement set_configuration,
 * and should also implement set_interface, get_configuration, and
 * get_interface.  Setting a configuration (or interface) is where
 * endpoints should be activated or (config 0) shut down.
 *
 * (Note that only the default control endpoint is supported.  Neither
 * hosts nor devices generally support control traffic except to ep0.)
 *
 * Most devices will ignore USB suspend/resume operations, and so will
 * not provide those callbacks.  However, some may need to change modes
 * when the host is not longer directing those activities.  For example,
 * local controls (buttons, dials, etc) may need to be re-enabled since
 * the (remote) host can't do that any longer; or an error state might
 * be cleared, to make the device behave identically whether or not
 * power is maintained.
 */
struct usb_gadget_driver {
	char			*function;
	enum usb_device_speed	max_speed;
	int			(*bind)(struct usb_gadget *gadget,
					struct usb_gadget_driver *driver);
	void			(*unbind)(struct usb_gadget *);
	int			(*setup)(struct usb_gadget *,
					const struct usb_ctrlrequest *);
	void			(*disconnect)(struct usb_gadget *);
	void			(*suspend)(struct usb_gadget *);
	void			(*resume)(struct usb_gadget *);
	void			(*reset)(struct usb_gadget *);

	/* FIXME support safe rmmod */
	struct device_driver	driver;

	char			*udc_name;
	struct list_head	pending;
	unsigned                match_existing_only:1;
};



/*-------------------------------------------------------------------------*/

/* driver modules register and unregister, as usual.
 * these calls must be made in a context that can sleep.
 *
 * these will usually be implemented directly by the hardware-dependent
 * usb bus interface driver, which will only support a single driver.
 */

/**
 * usb_gadget_probe_driver - probe a gadget driver
 * @driver: the driver being registered
 * Context: can sleep
 *
 * Call this in your gadget driver's module initialization function,
 * to tell the underlying usb controller driver about your driver.
 * The @bind() function will be called to bind it to a gadget before this
 * registration call returns.  It's expected that the @bind() function will
 * be in init sections.
 */
int usb_gadget_probe_driver(struct usb_gadget_driver *driver);

/**
 * usb_gadget_unregister_driver - unregister a gadget driver
 * @driver:the driver being unregistered
 * Context: can sleep
 *
 * Call this in your gadget driver's module cleanup function,
 * to tell the underlying usb controller that your driver is
 * going away.  If the controller is connected to a USB host,
 * it will first disconnect().  The driver is also requested
 * to unbind() and clean up any device state, before this procedure
 * finally returns.  It's expected that the unbind() functions
 * will in in exit sections, so may not be linked in some kernels.
 */
int usb_gadget_unregister_driver(struct usb_gadget_driver *driver);

extern int usb_add_gadget_udc_release(struct device *parent,
		struct usb_gadget *gadget, void (*release)(struct device *dev));
extern int usb_add_gadget_udc(struct device *parent, struct usb_gadget *gadget);
extern void usb_del_gadget_udc(struct usb_gadget *gadget);
extern char *usb_get_gadget_udc_name(void);

/*-------------------------------------------------------------------------*/

/* utility to simplify dealing with string descriptors */

/**
 * struct usb_string - wraps a C string and its USB id
 * @id:the (nonzero) ID for this string
 * @s:the string, in UTF-8 encoding
 *
 * If you're using usb_gadget_get_string(), use this to wrap a string
 * together with its ID.
 */
struct usb_string {
	u8			id;
	const char		*s;
};

/**
 * struct usb_gadget_strings - a set of USB strings in a given language
 * @language:identifies the strings' language (0x0409 for en-us)
 * @strings:array of strings with their ids
 *
 * If you're using usb_gadget_get_string(), use this to wrap all the
 * strings for a given language.
 */
struct usb_gadget_strings {
	u16			language;	/* 0x0409 for en-us */
	struct usb_string	*strings;
};

struct usb_gadget_string_container {
	struct list_head        list;
	u8                      *stash[0];
};

/* put descriptor for string with that id into buf (buflen >= 256) */
int usb_gadget_get_string(const struct usb_gadget_strings *table, int id, u8 *buf);

/*-------------------------------------------------------------------------*/

/* utility to simplify managing config descriptors */

/* write vector of descriptors into buffer */
int usb_descriptor_fillbuf(void *, unsigned,
		const struct usb_descriptor_header **);

/* build config descriptor from single descriptor vector */
int usb_gadget_config_buf(const struct usb_config_descriptor *config,
	void *buf, unsigned buflen, const struct usb_descriptor_header **desc);

/* copy a NULL-terminated vector of descriptors */
struct usb_descriptor_header **usb_copy_descriptors(
		struct usb_descriptor_header **);

/**
 * usb_free_descriptors - free descriptors returned by usb_copy_descriptors()
 * @v: vector of descriptors
 */
static inline void usb_free_descriptors(struct usb_descriptor_header **v)
{
	kfree(v);
}

struct usb_function;
int usb_assign_descriptors(struct usb_function *f,
		struct usb_descriptor_header **fs,
		struct usb_descriptor_header **hs,
		struct usb_descriptor_header **ss,
		struct usb_descriptor_header **ssp);
void usb_free_all_descriptors(struct usb_function *f);

struct usb_descriptor_header *usb_otg_descriptor_alloc(
				struct usb_gadget *gadget);
int usb_otg_descriptor_init(struct usb_gadget *gadget,
		struct usb_descriptor_header *otg_desc);
/*-------------------------------------------------------------------------*/

int usb_func_ep_queue(struct usb_function *func, struct usb_ep *ep,
				struct usb_request *req, gfp_t gfp_flags);

/*-------------------------------------------------------------------------*/

/* utility to simplify map/unmap of usb_requests to/from DMA */

#ifdef	CONFIG_HAS_DMA
extern int usb_gadget_map_request_by_dev(struct device *dev,
		struct usb_request *req, int is_in);
extern int usb_gadget_map_request(struct usb_gadget *gadget,
		struct usb_request *req, int is_in);

extern void usb_gadget_unmap_request_by_dev(struct device *dev,
		struct usb_request *req, int is_in);
extern void usb_gadget_unmap_request(struct usb_gadget *gadget,
		struct usb_request *req, int is_in);
#else /* !CONFIG_HAS_DMA */
static inline int usb_gadget_map_request_by_dev(struct device *dev,
		struct usb_request *req, int is_in) { return -ENOSYS; }
static inline int usb_gadget_map_request(struct usb_gadget *gadget,
		struct usb_request *req, int is_in) { return -ENOSYS; }

static inline void usb_gadget_unmap_request_by_dev(struct device *dev,
		struct usb_request *req, int is_in) { }
static inline void usb_gadget_unmap_request(struct usb_gadget *gadget,
		struct usb_request *req, int is_in) { }
#endif /* !CONFIG_HAS_DMA */

/*-------------------------------------------------------------------------*/

/* utility to set gadget state properly */

extern void usb_gadget_set_state(struct usb_gadget *gadget,
		enum usb_device_state state);

/*-------------------------------------------------------------------------*/

/* utility to tell udc core that the bus reset occurs */
extern void usb_gadget_udc_reset(struct usb_gadget *gadget,
		struct usb_gadget_driver *driver);

/*-------------------------------------------------------------------------*/

/* utility to give requests back to the gadget layer */

extern void usb_gadget_giveback_request(struct usb_ep *ep,
		struct usb_request *req);

/*-------------------------------------------------------------------------*/

/* utility to find endpoint by name */

extern struct usb_ep *gadget_find_ep_by_name(struct usb_gadget *g,
		const char *name);

/*-------------------------------------------------------------------------*/

/* utility to check if endpoint caps match descriptor needs */

extern int usb_gadget_ep_match_desc(struct usb_gadget *gadget,
		struct usb_ep *ep, struct usb_endpoint_descriptor *desc,
		struct usb_ss_ep_comp_descriptor *ep_comp);

/*-------------------------------------------------------------------------*/

/* utility to update vbus status for udc core, it may be scheduled */
extern void usb_udc_vbus_handler(struct usb_gadget *gadget, bool status);

/*-------------------------------------------------------------------------*/

/* utility wrapping a simple endpoint selection policy */

extern struct usb_ep *usb_ep_autoconfig(struct usb_gadget *,
			struct usb_endpoint_descriptor *);


extern struct usb_ep *usb_ep_autoconfig_ss(struct usb_gadget *,
			struct usb_endpoint_descriptor *,
			struct usb_ss_ep_comp_descriptor *);

extern void usb_ep_autoconfig_release(struct usb_ep *);

extern void usb_ep_autoconfig_reset(struct usb_gadget *);
extern struct usb_ep *usb_ep_autoconfig_by_name(struct usb_gadget *gadget,
			struct usb_endpoint_descriptor *desc,
			const char *ep_name);

#endif /* __LINUX_USB_GADGET_H */
