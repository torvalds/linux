/* USB OTG (On The Go) defines */
/*
 *
 * These APIs may be used between USB controllers.  USB device drivers
 * (for either host or peripheral roles) don't use these calls; they
 * continue to use just usb_device and usb_gadget.
 */

#ifndef __LINUX_USB_PHY_H
#define __LINUX_USB_PHY_H

#include <linux/notifier.h>

enum usb_phy_events {
	USB_EVENT_NONE,         /* no events or cable disconnected */
	USB_EVENT_VBUS,         /* vbus valid event */
	USB_EVENT_ID,           /* id was grounded */
	USB_EVENT_CHARGER,      /* usb dedicated charger */
	USB_EVENT_ENUMERATED,   /* gadget driver enumerated */
};

/* associate a type with PHY */
enum usb_phy_type {
	USB_PHY_TYPE_UNDEFINED,
	USB_PHY_TYPE_USB2,
	USB_PHY_TYPE_USB3,
};

struct usb_phy;
struct usb_otg;

/* for transceivers connected thru an ULPI interface, the user must
 * provide access ops
 */
struct usb_phy_io_ops {
	int (*read)(struct usb_phy *x, u32 reg);
	int (*write)(struct usb_phy *x, u32 val, u32 reg);
};

struct usb_phy {
	struct device		*dev;
	const char		*label;
	unsigned int		 flags;

	enum usb_phy_type	type;
	enum usb_phy_events	last_event;

	struct usb_otg		*otg;

	struct device		*io_dev;
	struct usb_phy_io_ops	*io_ops;
	void __iomem		*io_priv;

	/* for notification of usb_phy_events */
	struct atomic_notifier_head	notifier;

	/* to pass extra port status to the root hub */
	u16			port_status;
	u16			port_change;

	/* to support controllers that have multiple transceivers */
	struct list_head	head;

	/* initialize/shutdown the OTG controller */
	int	(*init)(struct usb_phy *x);
	void	(*shutdown)(struct usb_phy *x);

	/* effective for B devices, ignored for A-peripheral */
	int	(*set_power)(struct usb_phy *x,
				unsigned mA);

	/* for non-OTG B devices: set transceiver into suspend mode */
	int	(*set_suspend)(struct usb_phy *x,
				int suspend);

	/* notify phy connect status change */
	int	(*notify_connect)(struct usb_phy *x, int port);
	int	(*notify_disconnect)(struct usb_phy *x, int port);
};


/* for board-specific init logic */
extern int usb_add_phy(struct usb_phy *, enum usb_phy_type type);
extern void usb_remove_phy(struct usb_phy *);

/* helpers for direct access thru low-level io interface */
static inline int usb_phy_io_read(struct usb_phy *x, u32 reg)
{
	if (x->io_ops && x->io_ops->read)
		return x->io_ops->read(x, reg);

	return -EINVAL;
}

static inline int usb_phy_io_write(struct usb_phy *x, u32 val, u32 reg)
{
	if (x->io_ops && x->io_ops->write)
		return x->io_ops->write(x, val, reg);

	return -EINVAL;
}

static inline int
usb_phy_init(struct usb_phy *x)
{
	if (x->init)
		return x->init(x);

	return 0;
}

static inline void
usb_phy_shutdown(struct usb_phy *x)
{
	if (x->shutdown)
		x->shutdown(x);
}

/* for usb host and peripheral controller drivers */
#ifdef CONFIG_USB_OTG_UTILS
extern struct usb_phy *usb_get_phy(enum usb_phy_type type);
extern struct usb_phy *devm_usb_get_phy(struct device *dev,
	enum usb_phy_type type);
extern void usb_put_phy(struct usb_phy *);
extern void devm_usb_put_phy(struct device *dev, struct usb_phy *x);
#else
static inline struct usb_phy *usb_get_phy(enum usb_phy_type type)
{
	return NULL;
}

static inline struct usb_phy *devm_usb_get_phy(struct device *dev,
	enum usb_phy_type type)
{
	return NULL;
}

static inline void usb_put_phy(struct usb_phy *x)
{
}

static inline void devm_usb_put_phy(struct device *dev, struct usb_phy *x)
{
}

#endif

static inline int
usb_phy_set_power(struct usb_phy *x, unsigned mA)
{
	if (x && x->set_power)
		return x->set_power(x, mA);
	return 0;
}

/* Context: can sleep */
static inline int
usb_phy_set_suspend(struct usb_phy *x, int suspend)
{
	if (x->set_suspend != NULL)
		return x->set_suspend(x, suspend);
	else
		return 0;
}

static inline int
usb_phy_notify_connect(struct usb_phy *x, int port)
{
	if (x->notify_connect)
		return x->notify_connect(x, port);
	else
		return 0;
}

static inline int
usb_phy_notify_disconnect(struct usb_phy *x, int port)
{
	if (x->notify_disconnect)
		return x->notify_disconnect(x, port);
	else
		return 0;
}

/* notifiers */
static inline int
usb_register_notifier(struct usb_phy *x, struct notifier_block *nb)
{
	return atomic_notifier_chain_register(&x->notifier, nb);
}

static inline void
usb_unregister_notifier(struct usb_phy *x, struct notifier_block *nb)
{
	atomic_notifier_chain_unregister(&x->notifier, nb);
}

static inline const char *usb_phy_type_string(enum usb_phy_type type)
{
	switch (type) {
	case USB_PHY_TYPE_USB2:
		return "USB2 PHY";
	case USB_PHY_TYPE_USB3:
		return "USB3 PHY";
	default:
		return "UNKNOWN PHY TYPE";
	}
}
#endif /* __LINUX_USB_PHY_H */
