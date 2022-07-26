// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2001-2002 by David Brownell
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __USB_CORE_HCD_H
#define __USB_CORE_HCD_H

#ifdef __KERNEL__

#include <linux/rwsem.h>
#include <linux/interrupt.h>
#include <linux/idr.h>

#define MAX_TOPO_LEVEL		6

/* This file contains declarations of usbcore internals that are mostly
 * used or exposed by Host Controller Drivers.
 */

/*
 * USB Packet IDs (PIDs)
 */
#define USB_PID_EXT			0xf0	/* USB 2.0 LPM ECN */
#define USB_PID_OUT			0xe1
#define USB_PID_ACK			0xd2
#define USB_PID_DATA0			0xc3
#define USB_PID_PING			0xb4	/* USB 2.0 */
#define USB_PID_SOF			0xa5
#define USB_PID_NYET			0x96	/* USB 2.0 */
#define USB_PID_DATA2			0x87	/* USB 2.0 */
#define USB_PID_SPLIT			0x78	/* USB 2.0 */
#define USB_PID_IN			0x69
#define USB_PID_NAK			0x5a
#define USB_PID_DATA1			0x4b
#define USB_PID_PREAMBLE		0x3c	/* Token mode */
#define USB_PID_ERR			0x3c	/* USB 2.0: handshake mode */
#define USB_PID_SETUP			0x2d
#define USB_PID_STALL			0x1e
#define USB_PID_MDATA			0x0f	/* USB 2.0 */

/*-------------------------------------------------------------------------*/

/*
 * USB Host Controller Driver (usb_hcd) framework
 *
 * Since "struct usb_bus" is so thin, you can't share much code in it.
 * This framework is a layer over that, and should be more shareable.
 */

/*-------------------------------------------------------------------------*/

struct giveback_urb_bh {
	bool running;
	bool high_prio;
	spinlock_t lock;
	struct list_head  head;
	struct tasklet_struct bh;
	struct usb_host_endpoint *completing_ep;
};

enum usb_dev_authorize_policy {
	USB_DEVICE_AUTHORIZE_NONE	= 0,
	USB_DEVICE_AUTHORIZE_ALL	= 1,
	USB_DEVICE_AUTHORIZE_INTERNAL	= 2,
};

struct usb_hcd {

	/*
	 * housekeeping
	 */
	struct usb_bus		self;		/* hcd is-a bus */
	struct kref		kref;		/* reference counter */

	const char		*product_desc;	/* product/vendor string */
	int			speed;		/* Speed for this roothub.
						 * May be different from
						 * hcd->driver->flags & HCD_MASK
						 */
	char			irq_descr[24];	/* driver + bus # */

	struct timer_list	rh_timer;	/* drives root-hub polling */
	struct urb		*status_urb;	/* the current status urb */
#ifdef CONFIG_PM
	struct work_struct	wakeup_work;	/* for remote wakeup */
#endif
	struct work_struct	died_work;	/* for when the device dies */

	/*
	 * hardware info/state
	 */
	const struct hc_driver	*driver;	/* hw-specific hooks */

	/*
	 * OTG and some Host controllers need software interaction with phys;
	 * other external phys should be software-transparent
	 */
	struct usb_phy		*usb_phy;
	struct usb_phy_roothub	*phy_roothub;

	/* Flags that need to be manipulated atomically because they can
	 * change while the host controller is running.  Always use
	 * set_bit() or clear_bit() to change their values.
	 */
	unsigned long		flags;
#define HCD_FLAG_HW_ACCESSIBLE		0	/* at full power */
#define HCD_FLAG_POLL_RH		2	/* poll for rh status? */
#define HCD_FLAG_POLL_PENDING		3	/* status has changed? */
#define HCD_FLAG_WAKEUP_PENDING		4	/* root hub is resuming? */
#define HCD_FLAG_RH_RUNNING		5	/* root hub is running? */
#define HCD_FLAG_DEAD			6	/* controller has died? */
#define HCD_FLAG_INTF_AUTHORIZED	7	/* authorize interfaces? */
#define HCD_FLAG_DEFER_RH_REGISTER	8	/* Defer roothub registration */

	/* The flags can be tested using these macros; they are likely to
	 * be slightly faster than test_bit().
	 */
#define HCD_HW_ACCESSIBLE(hcd)	((hcd)->flags & (1U << HCD_FLAG_HW_ACCESSIBLE))
#define HCD_POLL_RH(hcd)	((hcd)->flags & (1U << HCD_FLAG_POLL_RH))
#define HCD_POLL_PENDING(hcd)	((hcd)->flags & (1U << HCD_FLAG_POLL_PENDING))
#define HCD_WAKEUP_PENDING(hcd)	((hcd)->flags & (1U << HCD_FLAG_WAKEUP_PENDING))
#define HCD_RH_RUNNING(hcd)	((hcd)->flags & (1U << HCD_FLAG_RH_RUNNING))
#define HCD_DEAD(hcd)		((hcd)->flags & (1U << HCD_FLAG_DEAD))
#define HCD_DEFER_RH_REGISTER(hcd) ((hcd)->flags & (1U << HCD_FLAG_DEFER_RH_REGISTER))

	/*
	 * Specifies if interfaces are authorized by default
	 * or they require explicit user space authorization; this bit is
	 * settable through /sys/class/usb_host/X/interface_authorized_default
	 */
#define HCD_INTF_AUTHORIZED(hcd) \
	((hcd)->flags & (1U << HCD_FLAG_INTF_AUTHORIZED))

	/*
	 * Specifies if devices are authorized by default
	 * or they require explicit user space authorization; this bit is
	 * settable through /sys/class/usb_host/X/authorized_default
	 */
	enum usb_dev_authorize_policy dev_policy;

	/* Flags that get set only during HCD registration or removal. */
	unsigned		rh_registered:1;/* is root hub registered? */
	unsigned		rh_pollable:1;	/* may we poll the root hub? */
	unsigned		msix_enabled:1;	/* driver has MSI-X enabled? */
	unsigned		msi_enabled:1;	/* driver has MSI enabled? */
	/*
	 * do not manage the PHY state in the HCD core, instead let the driver
	 * handle this (for example if the PHY can only be turned on after a
	 * specific event)
	 */
	unsigned		skip_phy_initialization:1;

	/* The next flag is a stopgap, to be removed when all the HCDs
	 * support the new root-hub polling mechanism. */
	unsigned		uses_new_polling:1;
	unsigned		wireless:1;	/* Wireless USB HCD */
	unsigned		has_tt:1;	/* Integrated TT in root hub */
	unsigned		amd_resume_bug:1; /* AMD remote wakeup quirk */
	unsigned		can_do_streams:1; /* HC supports streams */
	unsigned		tpl_support:1; /* OTG & EH TPL support */
	unsigned		cant_recv_wakeups:1;
			/* wakeup requests from downstream aren't received */

	unsigned int		irq;		/* irq allocated */
	void __iomem		*regs;		/* device memory/io */
	resource_size_t		rsrc_start;	/* memory/io resource start */
	resource_size_t		rsrc_len;	/* memory/io resource length */
	unsigned		power_budget;	/* in mA, 0 = no limit */

	struct giveback_urb_bh  high_prio_bh;
	struct giveback_urb_bh  low_prio_bh;

	/* bandwidth_mutex should be taken before adding or removing
	 * any new bus bandwidth constraints:
	 *   1. Before adding a configuration for a new device.
	 *   2. Before removing the configuration to put the device into
	 *      the addressed state.
	 *   3. Before selecting a different configuration.
	 *   4. Before selecting an alternate interface setting.
	 *
	 * bandwidth_mutex should be dropped after a successful control message
	 * to the device, or resetting the bandwidth after a failed attempt.
	 */
	struct mutex		*address0_mutex;
	struct mutex		*bandwidth_mutex;
	struct usb_hcd		*shared_hcd;
	struct usb_hcd		*primary_hcd;


#define HCD_BUFFER_POOLS	4
	struct dma_pool		*pool[HCD_BUFFER_POOLS];

	int			state;
#	define	__ACTIVE		0x01
#	define	__SUSPEND		0x04
#	define	__TRANSIENT		0x80

#	define	HC_STATE_HALT		0
#	define	HC_STATE_RUNNING	(__ACTIVE)
#	define	HC_STATE_QUIESCING	(__SUSPEND|__TRANSIENT|__ACTIVE)
#	define	HC_STATE_RESUMING	(__SUSPEND|__TRANSIENT)
#	define	HC_STATE_SUSPENDED	(__SUSPEND)

#define	HC_IS_RUNNING(state) ((state) & __ACTIVE)
#define	HC_IS_SUSPENDED(state) ((state) & __SUSPEND)

	/* memory pool for HCs having local memory, or %NULL */
	struct gen_pool         *localmem_pool;

	/* more shared queuing code would be good; it should support
	 * smarter scheduling, handle transaction translators, etc;
	 * input size of periodic table to an interrupt scheduler.
	 * (ohci 32, uhci 1024, ehci 256/512/1024).
	 */

	/* The HC driver's private data is stored at the end of
	 * this structure.
	 */
	unsigned long hcd_priv[]
			__attribute__ ((aligned(sizeof(s64))));
};

/* 2.4 does this a bit differently ... */
static inline struct usb_bus *hcd_to_bus(struct usb_hcd *hcd)
{
	return &hcd->self;
}

static inline struct usb_hcd *bus_to_hcd(struct usb_bus *bus)
{
	return container_of(bus, struct usb_hcd, self);
}

/*-------------------------------------------------------------------------*/


struct hc_driver {
	const char	*description;	/* "ehci-hcd" etc */
	const char	*product_desc;	/* product/vendor string */
	size_t		hcd_priv_size;	/* size of private data */

	/* irq handler */
	irqreturn_t	(*irq) (struct usb_hcd *hcd);

	int	flags;
#define	HCD_MEMORY	0x0001		/* HC regs use memory (else I/O) */
#define	HCD_DMA		0x0002		/* HC uses DMA */
#define	HCD_SHARED	0x0004		/* Two (or more) usb_hcds share HW */
#define	HCD_USB11	0x0010		/* USB 1.1 */
#define	HCD_USB2	0x0020		/* USB 2.0 */
#define	HCD_USB25	0x0030		/* Wireless USB 1.0 (USB 2.5)*/
#define	HCD_USB3	0x0040		/* USB 3.0 */
#define	HCD_USB31	0x0050		/* USB 3.1 */
#define	HCD_USB32	0x0060		/* USB 3.2 */
#define	HCD_MASK	0x0070
#define	HCD_BH		0x0100		/* URB complete in BH context */

	/* called to init HCD and root hub */
	int	(*reset) (struct usb_hcd *hcd);
	int	(*start) (struct usb_hcd *hcd);

	/* NOTE:  these suspend/resume calls relate to the HC as
	 * a whole, not just the root hub; they're for PCI bus glue.
	 */
	/* called after suspending the hub, before entering D3 etc */
	int	(*pci_suspend)(struct usb_hcd *hcd, bool do_wakeup);

	/* called after entering D0 (etc), before resuming the hub */
	int	(*pci_resume)(struct usb_hcd *hcd, bool hibernated);

	/* cleanly make HCD stop writing memory and doing I/O */
	void	(*stop) (struct usb_hcd *hcd);

	/* shutdown HCD */
	void	(*shutdown) (struct usb_hcd *hcd);

	/* return current frame number */
	int	(*get_frame_number) (struct usb_hcd *hcd);

	/* manage i/o requests, device state */
	int	(*urb_enqueue)(struct usb_hcd *hcd,
				struct urb *urb, gfp_t mem_flags);
	int	(*urb_dequeue)(struct usb_hcd *hcd,
				struct urb *urb, int status);

	/*
	 * (optional) these hooks allow an HCD to override the default DMA
	 * mapping and unmapping routines.  In general, they shouldn't be
	 * necessary unless the host controller has special DMA requirements,
	 * such as alignment constraints.  If these are not specified, the
	 * general usb_hcd_(un)?map_urb_for_dma functions will be used instead
	 * (and it may be a good idea to call these functions in your HCD
	 * implementation)
	 */
	int	(*map_urb_for_dma)(struct usb_hcd *hcd, struct urb *urb,
				   gfp_t mem_flags);
	void    (*unmap_urb_for_dma)(struct usb_hcd *hcd, struct urb *urb);

	/* hw synch, freeing endpoint resources that urb_dequeue can't */
	void	(*endpoint_disable)(struct usb_hcd *hcd,
			struct usb_host_endpoint *ep);

	/* (optional) reset any endpoint state such as sequence number
	   and current window */
	void	(*endpoint_reset)(struct usb_hcd *hcd,
			struct usb_host_endpoint *ep);

	/* root hub support */
	int	(*hub_status_data) (struct usb_hcd *hcd, char *buf);
	int	(*hub_control) (struct usb_hcd *hcd,
				u16 typeReq, u16 wValue, u16 wIndex,
				char *buf, u16 wLength);
	int	(*bus_suspend)(struct usb_hcd *);
	int	(*bus_resume)(struct usb_hcd *);
	int	(*start_port_reset)(struct usb_hcd *, unsigned port_num);
	unsigned long	(*get_resuming_ports)(struct usb_hcd *);

		/* force handover of high-speed port to full-speed companion */
	void	(*relinquish_port)(struct usb_hcd *, int);
		/* has a port been handed over to a companion? */
	int	(*port_handed_over)(struct usb_hcd *, int);

		/* CLEAR_TT_BUFFER completion callback */
	void	(*clear_tt_buffer_complete)(struct usb_hcd *,
				struct usb_host_endpoint *);

	/* xHCI specific functions */
		/* Called by usb_alloc_dev to alloc HC device structures */
	int	(*alloc_dev)(struct usb_hcd *, struct usb_device *);
		/* Called by usb_disconnect to free HC device structures */
	void	(*free_dev)(struct usb_hcd *, struct usb_device *);
	/* Change a group of bulk endpoints to support multiple stream IDs */
	int	(*alloc_streams)(struct usb_hcd *hcd, struct usb_device *udev,
		struct usb_host_endpoint **eps, unsigned int num_eps,
		unsigned int num_streams, gfp_t mem_flags);
	/* Reverts a group of bulk endpoints back to not using stream IDs.
	 * Can fail if we run out of memory.
	 */
	int	(*free_streams)(struct usb_hcd *hcd, struct usb_device *udev,
		struct usb_host_endpoint **eps, unsigned int num_eps,
		gfp_t mem_flags);

	/* Bandwidth computation functions */
	/* Note that add_endpoint() can only be called once per endpoint before
	 * check_bandwidth() or reset_bandwidth() must be called.
	 * drop_endpoint() can only be called once per endpoint also.
	 * A call to xhci_drop_endpoint() followed by a call to
	 * xhci_add_endpoint() will add the endpoint to the schedule with
	 * possibly new parameters denoted by a different endpoint descriptor
	 * in usb_host_endpoint.  A call to xhci_add_endpoint() followed by a
	 * call to xhci_drop_endpoint() is not allowed.
	 */
		/* Allocate endpoint resources and add them to a new schedule */
	int	(*add_endpoint)(struct usb_hcd *, struct usb_device *,
				struct usb_host_endpoint *);
		/* Drop an endpoint from a new schedule */
	int	(*drop_endpoint)(struct usb_hcd *, struct usb_device *,
				 struct usb_host_endpoint *);
		/* Check that a new hardware configuration, set using
		 * endpoint_enable and endpoint_disable, does not exceed bus
		 * bandwidth.  This must be called before any set configuration
		 * or set interface requests are sent to the device.
		 */
	int	(*check_bandwidth)(struct usb_hcd *, struct usb_device *);
		/* Reset the device schedule to the last known good schedule,
		 * which was set from a previous successful call to
		 * check_bandwidth().  This reverts any add_endpoint() and
		 * drop_endpoint() calls since that last successful call.
		 * Used for when a check_bandwidth() call fails due to resource
		 * or bandwidth constraints.
		 */
	void	(*reset_bandwidth)(struct usb_hcd *, struct usb_device *);
		/* Returns the hardware-chosen device address */
	int	(*address_device)(struct usb_hcd *, struct usb_device *udev);
		/* prepares the hardware to send commands to the device */
	int	(*enable_device)(struct usb_hcd *, struct usb_device *udev);
		/* Notifies the HCD after a hub descriptor is fetched.
		 * Will block.
		 */
	int	(*update_hub_device)(struct usb_hcd *, struct usb_device *hdev,
			struct usb_tt *tt, gfp_t mem_flags);
	int	(*reset_device)(struct usb_hcd *, struct usb_device *);
		/* Notifies the HCD after a device is connected and its
		 * address is set
		 */
	int	(*update_device)(struct usb_hcd *, struct usb_device *);
	int	(*set_usb2_hw_lpm)(struct usb_hcd *, struct usb_device *, int);
	/* USB 3.0 Link Power Management */
		/* Returns the USB3 hub-encoded value for the U1/U2 timeout. */
	int	(*enable_usb3_lpm_timeout)(struct usb_hcd *,
			struct usb_device *, enum usb3_link_state state);
		/* The xHCI host controller can still fail the command to
		 * disable the LPM timeouts, so this can return an error code.
		 */
	int	(*disable_usb3_lpm_timeout)(struct usb_hcd *,
			struct usb_device *, enum usb3_link_state state);
	int	(*find_raw_port_number)(struct usb_hcd *, int);
	/* Call for power on/off the port if necessary */
	int	(*port_power)(struct usb_hcd *hcd, int portnum, bool enable);
	/* Call for SINGLE_STEP_SET_FEATURE Test for USB2 EH certification */
#define EHSET_TEST_SINGLE_STEP_SET_FEATURE 0x06
	int	(*submit_single_step_set_feature)(struct usb_hcd *,
			struct urb *, int);
};

static inline int hcd_giveback_urb_in_bh(struct usb_hcd *hcd)
{
	return hcd->driver->flags & HCD_BH;
}

static inline bool hcd_periodic_completion_in_progress(struct usb_hcd *hcd,
		struct usb_host_endpoint *ep)
{
	return hcd->high_prio_bh.completing_ep == ep;
}

static inline bool hcd_uses_dma(struct usb_hcd *hcd)
{
	return IS_ENABLED(CONFIG_HAS_DMA) && (hcd->driver->flags & HCD_DMA);
}

extern int usb_hcd_link_urb_to_ep(struct usb_hcd *hcd, struct urb *urb);
extern int usb_hcd_check_unlink_urb(struct usb_hcd *hcd, struct urb *urb,
		int status);
extern void usb_hcd_unlink_urb_from_ep(struct usb_hcd *hcd, struct urb *urb);

extern int usb_hcd_submit_urb(struct urb *urb, gfp_t mem_flags);
extern int usb_hcd_unlink_urb(struct urb *urb, int status);
extern void usb_hcd_giveback_urb(struct usb_hcd *hcd, struct urb *urb,
		int status);
extern int usb_hcd_map_urb_for_dma(struct usb_hcd *hcd, struct urb *urb,
		gfp_t mem_flags);
extern void usb_hcd_unmap_urb_setup_for_dma(struct usb_hcd *, struct urb *);
extern void usb_hcd_unmap_urb_for_dma(struct usb_hcd *, struct urb *);
extern void usb_hcd_flush_endpoint(struct usb_device *udev,
		struct usb_host_endpoint *ep);
extern void usb_hcd_disable_endpoint(struct usb_device *udev,
		struct usb_host_endpoint *ep);
extern void usb_hcd_reset_endpoint(struct usb_device *udev,
		struct usb_host_endpoint *ep);
extern void usb_hcd_synchronize_unlinks(struct usb_device *udev);
extern int usb_hcd_alloc_bandwidth(struct usb_device *udev,
		struct usb_host_config *new_config,
		struct usb_host_interface *old_alt,
		struct usb_host_interface *new_alt);
extern int usb_hcd_get_frame_number(struct usb_device *udev);

struct usb_hcd *__usb_create_hcd(const struct hc_driver *driver,
		struct device *sysdev, struct device *dev, const char *bus_name,
		struct usb_hcd *primary_hcd);
extern struct usb_hcd *usb_create_hcd(const struct hc_driver *driver,
		struct device *dev, const char *bus_name);
extern struct usb_hcd *usb_create_shared_hcd(const struct hc_driver *driver,
		struct device *dev, const char *bus_name,
		struct usb_hcd *shared_hcd);
extern struct usb_hcd *usb_get_hcd(struct usb_hcd *hcd);
extern void usb_put_hcd(struct usb_hcd *hcd);
extern int usb_hcd_is_primary_hcd(struct usb_hcd *hcd);
extern int usb_add_hcd(struct usb_hcd *hcd,
		unsigned int irqnum, unsigned long irqflags);
extern void usb_remove_hcd(struct usb_hcd *hcd);
extern int usb_hcd_find_raw_port_number(struct usb_hcd *hcd, int port1);
int usb_hcd_setup_local_mem(struct usb_hcd *hcd, phys_addr_t phys_addr,
			    dma_addr_t dma, size_t size);

struct platform_device;
extern void usb_hcd_platform_shutdown(struct platform_device *dev);
#ifdef CONFIG_USB_HCD_TEST_MODE
extern int ehset_single_step_set_feature(struct usb_hcd *hcd, int port);
#else
static inline int ehset_single_step_set_feature(struct usb_hcd *hcd, int port)
{
	return 0;
}
#endif /* CONFIG_USB_HCD_TEST_MODE */

#ifdef CONFIG_USB_PCI
struct pci_dev;
struct pci_device_id;
extern int usb_hcd_pci_probe(struct pci_dev *dev,
			     const struct pci_device_id *id,
			     const struct hc_driver *driver);
extern void usb_hcd_pci_remove(struct pci_dev *dev);
extern void usb_hcd_pci_shutdown(struct pci_dev *dev);

extern int usb_hcd_amd_remote_wakeup_quirk(struct pci_dev *dev);

#ifdef CONFIG_PM
extern const struct dev_pm_ops usb_hcd_pci_pm_ops;
#endif
#endif /* CONFIG_USB_PCI */

/* pci-ish (pdev null is ok) buffer alloc/mapping support */
void usb_init_pool_max(void);
int hcd_buffer_create(struct usb_hcd *hcd);
void hcd_buffer_destroy(struct usb_hcd *hcd);

void *hcd_buffer_alloc(struct usb_bus *bus, size_t size,
	gfp_t mem_flags, dma_addr_t *dma);
void hcd_buffer_free(struct usb_bus *bus, size_t size,
	void *addr, dma_addr_t dma);

/* generic bus glue, needed for host controllers that don't use PCI */
extern irqreturn_t usb_hcd_irq(int irq, void *__hcd);

extern void usb_hc_died(struct usb_hcd *hcd);
extern void usb_hcd_poll_rh_status(struct usb_hcd *hcd);
extern void usb_wakeup_notification(struct usb_device *hdev,
		unsigned int portnum);

extern void usb_hcd_start_port_resume(struct usb_bus *bus, int portnum);
extern void usb_hcd_end_port_resume(struct usb_bus *bus, int portnum);

/* The D0/D1 toggle bits ... USE WITH CAUTION (they're almost hcd-internal) */
#define usb_gettoggle(dev, ep, out) (((dev)->toggle[out] >> (ep)) & 1)
#define	usb_dotoggle(dev, ep, out)  ((dev)->toggle[out] ^= (1 << (ep)))
#define usb_settoggle(dev, ep, out, bit) \
		((dev)->toggle[out] = ((dev)->toggle[out] & ~(1 << (ep))) | \
		 ((bit) << (ep)))

/* -------------------------------------------------------------------------- */

/* Enumeration is only for the hub driver, or HCD virtual root hubs */
extern struct usb_device *usb_alloc_dev(struct usb_device *parent,
					struct usb_bus *, unsigned port);
extern int usb_new_device(struct usb_device *dev);
extern void usb_disconnect(struct usb_device **);

extern int usb_get_configuration(struct usb_device *dev);
extern void usb_destroy_configuration(struct usb_device *dev);

/*-------------------------------------------------------------------------*/

/*
 * HCD Root Hub support
 */

#include <linux/usb/ch11.h>

/*
 * As of USB 2.0, full/low speed devices are segregated into trees.
 * One type grows from USB 1.1 host controllers (OHCI, UHCI etc).
 * The other type grows from high speed hubs when they connect to
 * full/low speed devices using "Transaction Translators" (TTs).
 *
 * TTs should only be known to the hub driver, and high speed bus
 * drivers (only EHCI for now).  They affect periodic scheduling and
 * sometimes control/bulk error recovery.
 */

struct usb_device;

struct usb_tt {
	struct usb_device	*hub;	/* upstream highspeed hub */
	int			multi;	/* true means one TT per port */
	unsigned		think_time;	/* think time in ns */
	void			*hcpriv;	/* HCD private data */

	/* for control/bulk error recovery (CLEAR_TT_BUFFER) */
	spinlock_t		lock;
	struct list_head	clear_list;	/* of usb_tt_clear */
	struct work_struct	clear_work;
};

struct usb_tt_clear {
	struct list_head	clear_list;
	unsigned		tt;
	u16			devinfo;
	struct usb_hcd		*hcd;
	struct usb_host_endpoint	*ep;
};

extern int usb_hub_clear_tt_buffer(struct urb *urb);
extern void usb_ep0_reinit(struct usb_device *);

/* (shifted) direction/type/recipient from the USB 2.0 spec, table 9.2 */
#define DeviceRequest \
	((USB_DIR_IN|USB_TYPE_STANDARD|USB_RECIP_DEVICE)<<8)
#define DeviceOutRequest \
	((USB_DIR_OUT|USB_TYPE_STANDARD|USB_RECIP_DEVICE)<<8)

#define InterfaceRequest \
	((USB_DIR_IN|USB_TYPE_STANDARD|USB_RECIP_INTERFACE)<<8)

#define EndpointRequest \
	((USB_DIR_IN|USB_TYPE_STANDARD|USB_RECIP_ENDPOINT)<<8)
#define EndpointOutRequest \
	((USB_DIR_OUT|USB_TYPE_STANDARD|USB_RECIP_ENDPOINT)<<8)

/* class requests from the USB 2.0 hub spec, table 11-15 */
#define HUB_CLASS_REQ(dir, type, request) ((((dir) | (type)) << 8) | (request))
/* GetBusState and SetHubDescriptor are optional, omitted */
#define ClearHubFeature		HUB_CLASS_REQ(USB_DIR_OUT, USB_RT_HUB, USB_REQ_CLEAR_FEATURE)
#define ClearPortFeature	HUB_CLASS_REQ(USB_DIR_OUT, USB_RT_PORT, USB_REQ_CLEAR_FEATURE)
#define GetHubDescriptor	HUB_CLASS_REQ(USB_DIR_IN, USB_RT_HUB, USB_REQ_GET_DESCRIPTOR)
#define GetHubStatus		HUB_CLASS_REQ(USB_DIR_IN, USB_RT_HUB, USB_REQ_GET_STATUS)
#define GetPortStatus		HUB_CLASS_REQ(USB_DIR_IN, USB_RT_PORT, USB_REQ_GET_STATUS)
#define SetHubFeature		HUB_CLASS_REQ(USB_DIR_OUT, USB_RT_HUB, USB_REQ_SET_FEATURE)
#define SetPortFeature		HUB_CLASS_REQ(USB_DIR_OUT, USB_RT_PORT, USB_REQ_SET_FEATURE)
#define ClearTTBuffer		HUB_CLASS_REQ(USB_DIR_OUT, USB_RT_PORT, HUB_CLEAR_TT_BUFFER)
#define ResetTT			HUB_CLASS_REQ(USB_DIR_OUT, USB_RT_PORT, HUB_RESET_TT)
#define GetTTState		HUB_CLASS_REQ(USB_DIR_IN, USB_RT_PORT, HUB_GET_TT_STATE)
#define StopTT			HUB_CLASS_REQ(USB_DIR_OUT, USB_RT_PORT, HUB_STOP_TT)


/*-------------------------------------------------------------------------*/

/* class requests from USB 3.1 hub spec, table 10-7 */
#define SetHubDepth		HUB_CLASS_REQ(USB_DIR_OUT, USB_RT_HUB, HUB_SET_DEPTH)
#define GetPortErrorCount	HUB_CLASS_REQ(USB_DIR_IN, USB_RT_PORT, HUB_GET_PORT_ERR_COUNT)

/*
 * Generic bandwidth allocation constants/support
 */
#define FRAME_TIME_USECS	1000L
#define BitTime(bytecount) (7 * 8 * bytecount / 6) /* with integer truncation */
		/* Trying not to use worst-case bit-stuffing
		 * of (7/6 * 8 * bytecount) = 9.33 * bytecount */
		/* bytecount = data payload byte count */

#define NS_TO_US(ns)	DIV_ROUND_UP(ns, 1000L)
			/* convert nanoseconds to microseconds, rounding up */

/*
 * Full/low speed bandwidth allocation constants/support.
 */
#define BW_HOST_DELAY	1000L		/* nanoseconds */
#define BW_HUB_LS_SETUP	333L		/* nanoseconds */
			/* 4 full-speed bit times (est.) */

#define FRAME_TIME_BITS			12000L	/* frame = 1 millisecond */
#define FRAME_TIME_MAX_BITS_ALLOC	(90L * FRAME_TIME_BITS / 100L)
#define FRAME_TIME_MAX_USECS_ALLOC	(90L * FRAME_TIME_USECS / 100L)

/*
 * Ceiling [nano/micro]seconds (typical) for that many bytes at high speed
 * ISO is a bit less, no ACK ... from USB 2.0 spec, 5.11.3 (and needed
 * to preallocate bandwidth)
 */
#define USB2_HOST_DELAY	5	/* nsec, guess */
#define HS_NSECS(bytes) (((55 * 8 * 2083) \
	+ (2083UL * (3 + BitTime(bytes))))/1000 \
	+ USB2_HOST_DELAY)
#define HS_NSECS_ISO(bytes) (((38 * 8 * 2083) \
	+ (2083UL * (3 + BitTime(bytes))))/1000 \
	+ USB2_HOST_DELAY)
#define HS_USECS(bytes)		NS_TO_US(HS_NSECS(bytes))
#define HS_USECS_ISO(bytes)	NS_TO_US(HS_NSECS_ISO(bytes))

extern long usb_calc_bus_time(int speed, int is_input,
			int isoc, int bytecount);

/*-------------------------------------------------------------------------*/

extern void usb_set_device_state(struct usb_device *udev,
		enum usb_device_state new_state);

/*-------------------------------------------------------------------------*/

/* exported only within usbcore */

extern struct idr usb_bus_idr;
extern struct mutex usb_bus_idr_lock;
extern wait_queue_head_t usb_kill_urb_queue;


#define usb_endpoint_out(ep_dir)	(!((ep_dir) & USB_DIR_IN))

#ifdef CONFIG_PM
extern unsigned usb_wakeup_enabled_descendants(struct usb_device *udev);
extern void usb_root_hub_lost_power(struct usb_device *rhdev);
extern int hcd_bus_suspend(struct usb_device *rhdev, pm_message_t msg);
extern int hcd_bus_resume(struct usb_device *rhdev, pm_message_t msg);
extern void usb_hcd_resume_root_hub(struct usb_hcd *hcd);
#else
static inline unsigned usb_wakeup_enabled_descendants(struct usb_device *udev)
{
	return 0;
}
static inline void usb_hcd_resume_root_hub(struct usb_hcd *hcd)
{
	return;
}
#endif /* CONFIG_PM */

/*-------------------------------------------------------------------------*/

#if defined(CONFIG_USB_MON) || defined(CONFIG_USB_MON_MODULE)

struct usb_mon_operations {
	void (*urb_submit)(struct usb_bus *bus, struct urb *urb);
	void (*urb_submit_error)(struct usb_bus *bus, struct urb *urb, int err);
	void (*urb_complete)(struct usb_bus *bus, struct urb *urb, int status);
	/* void (*urb_unlink)(struct usb_bus *bus, struct urb *urb); */
};

extern const struct usb_mon_operations *mon_ops;

static inline void usbmon_urb_submit(struct usb_bus *bus, struct urb *urb)
{
	if (bus->monitored)
		(*mon_ops->urb_submit)(bus, urb);
}

static inline void usbmon_urb_submit_error(struct usb_bus *bus, struct urb *urb,
    int error)
{
	if (bus->monitored)
		(*mon_ops->urb_submit_error)(bus, urb, error);
}

static inline void usbmon_urb_complete(struct usb_bus *bus, struct urb *urb,
		int status)
{
	if (bus->monitored)
		(*mon_ops->urb_complete)(bus, urb, status);
}

int usb_mon_register(const struct usb_mon_operations *ops);
void usb_mon_deregister(void);

#else

static inline void usbmon_urb_submit(struct usb_bus *bus, struct urb *urb) {}
static inline void usbmon_urb_submit_error(struct usb_bus *bus, struct urb *urb,
    int error) {}
static inline void usbmon_urb_complete(struct usb_bus *bus, struct urb *urb,
		int status) {}

#endif /* CONFIG_USB_MON || CONFIG_USB_MON_MODULE */

/*-------------------------------------------------------------------------*/

/* random stuff */

/* This rwsem is for use only by the hub driver and ehci-hcd.
 * Nobody else should touch it.
 */
extern struct rw_semaphore ehci_cf_port_reset_rwsem;

/* Keep track of which host controller drivers are loaded */
#define USB_UHCI_LOADED		0
#define USB_OHCI_LOADED		1
#define USB_EHCI_LOADED		2
extern unsigned long usb_hcds_loaded;

#endif /* __KERNEL__ */

#endif /* __USB_CORE_HCD_H */
