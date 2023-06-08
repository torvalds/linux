/* SPDX-License-Identifier: GPL-2.0 */
/*
 * USB PHY defines
 *
 * These APIs may be used between USB controllers.  USB device drivers
 * (for either host or peripheral roles) don't use these calls; they
 * continue to use just usb_device and usb_gadget.
 */

#ifndef __LINUX_USB_PHY_H
#define __LINUX_USB_PHY_H

#include <linux/extcon.h>
#include <linux/notifier.h>
#include <linux/usb.h>
#include <linux/android_kabi.h>
#include <uapi/linux/usb/charger.h>

enum usb_phy_interface {
	USBPHY_INTERFACE_MODE_UNKNOWN,
	USBPHY_INTERFACE_MODE_UTMI,
	USBPHY_INTERFACE_MODE_UTMIW,
	USBPHY_INTERFACE_MODE_ULPI,
	USBPHY_INTERFACE_MODE_SERIAL,
	USBPHY_INTERFACE_MODE_HSIC,
};

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

/* OTG defines lots of enumeration states before device reset */
enum usb_otg_state {
	OTG_STATE_UNDEFINED = 0,

	/* single-role peripheral, and dual-role default-b */
	OTG_STATE_B_IDLE,
	OTG_STATE_B_SRP_INIT,
	OTG_STATE_B_PERIPHERAL,

	/* extra dual-role default-b states */
	OTG_STATE_B_WAIT_ACON,
	OTG_STATE_B_HOST,

	/* dual-role default-a */
	OTG_STATE_A_IDLE,
	OTG_STATE_A_WAIT_VRISE,
	OTG_STATE_A_WAIT_BCON,
	OTG_STATE_A_HOST,
	OTG_STATE_A_SUSPEND,
	OTG_STATE_A_PERIPHERAL,
	OTG_STATE_A_WAIT_VFALL,
	OTG_STATE_A_VBUS_ERR,
};

struct usb_phy;
struct usb_otg;

/* for phys connected thru an ULPI interface, the user must
 * provide access ops
 */
struct usb_phy_io_ops {
	int (*read)(struct usb_phy *x, u32 reg);
	int (*write)(struct usb_phy *x, u32 val, u32 reg);
};

struct usb_charger_current {
	unsigned int sdp_min;
	unsigned int sdp_max;
	unsigned int dcp_min;
	unsigned int dcp_max;
	unsigned int cdp_min;
	unsigned int cdp_max;
	unsigned int aca_min;
	unsigned int aca_max;
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

	/* to support extcon device */
	struct extcon_dev	*edev;
	struct extcon_dev	*id_edev;
	struct notifier_block	vbus_nb;
	struct notifier_block	id_nb;
	struct notifier_block	type_nb;

	/* Support USB charger */
	enum usb_charger_type	chg_type;
	enum usb_charger_state	chg_state;
	struct usb_charger_current	chg_cur;
	struct work_struct		chg_work;

	/* for notification of usb_phy_events */
	struct atomic_notifier_head	notifier;

	/* to pass extra port status to the root hub */
	u16			port_status;
	u16			port_change;

	/* to support controllers that have multiple phys */
	struct list_head	head;

	/* initialize/shutdown the phy */
	int	(*init)(struct usb_phy *x);
	void	(*shutdown)(struct usb_phy *x);

	/* enable/disable VBUS */
	int	(*set_vbus)(struct usb_phy *x, int on);

	/* effective for B devices, ignored for A-peripheral */
	int	(*set_power)(struct usb_phy *x,
				unsigned mA);

	/* Set phy into suspend mode */
	int	(*set_suspend)(struct usb_phy *x,
				int suspend);

	/*
	 * Set wakeup enable for PHY, in that case, the PHY can be
	 * woken up from suspend status due to external events,
	 * like vbus change, dp/dm change and id.
	 */
	int	(*set_wakeup)(struct usb_phy *x, bool enabled);

	/* notify phy connect status change */
	int	(*notify_connect)(struct usb_phy *x,
			enum usb_device_speed speed);
	int	(*notify_disconnect)(struct usb_phy *x,
			enum usb_device_speed speed);

	/*
	 * Charger detection method can be implemented if you need to
	 * manually detect the charger type.
	 */
	enum usb_charger_type (*charger_detect)(struct usb_phy *x);

	ANDROID_KABI_RESERVE(1);
};

/* for board-specific init logic */
extern int usb_add_phy(struct usb_phy *, enum usb_phy_type type);
extern int usb_add_phy_dev(struct usb_phy *);
extern void usb_remove_phy(struct usb_phy *);

/* helpers for direct access thru low-level io interface */
static inline int usb_phy_io_read(struct usb_phy *x, u32 reg)
{
	if (x && x->io_ops && x->io_ops->read)
		return x->io_ops->read(x, reg);

	return -EINVAL;
}

static inline int usb_phy_io_write(struct usb_phy *x, u32 val, u32 reg)
{
	if (x && x->io_ops && x->io_ops->write)
		return x->io_ops->write(x, val, reg);

	return -EINVAL;
}

static inline int
usb_phy_init(struct usb_phy *x)
{
	if (x && x->init)
		return x->init(x);

	return 0;
}

static inline void
usb_phy_shutdown(struct usb_phy *x)
{
	if (x && x->shutdown)
		x->shutdown(x);
}

static inline int
usb_phy_vbus_on(struct usb_phy *x)
{
	if (!x || !x->set_vbus)
		return 0;

	return x->set_vbus(x, true);
}

static inline int
usb_phy_vbus_off(struct usb_phy *x)
{
	if (!x || !x->set_vbus)
		return 0;

	return x->set_vbus(x, false);
}

/* for usb host and peripheral controller drivers */
#if IS_ENABLED(CONFIG_USB_PHY)
extern struct usb_phy *usb_get_phy(enum usb_phy_type type);
extern struct usb_phy *devm_usb_get_phy(struct device *dev,
	enum usb_phy_type type);
extern struct usb_phy *devm_usb_get_phy_by_phandle(struct device *dev,
	const char *phandle, u8 index);
extern struct usb_phy *devm_usb_get_phy_by_node(struct device *dev,
	struct device_node *node, struct notifier_block *nb);
extern void usb_put_phy(struct usb_phy *);
extern void devm_usb_put_phy(struct device *dev, struct usb_phy *x);
extern void usb_phy_set_event(struct usb_phy *x, unsigned long event);
extern void usb_phy_set_charger_current(struct usb_phy *usb_phy,
					unsigned int mA);
extern void usb_phy_get_charger_current(struct usb_phy *usb_phy,
					unsigned int *min, unsigned int *max);
extern void usb_phy_set_charger_state(struct usb_phy *usb_phy,
				      enum usb_charger_state state);
#else
static inline struct usb_phy *usb_get_phy(enum usb_phy_type type)
{
	return ERR_PTR(-ENXIO);
}

static inline struct usb_phy *devm_usb_get_phy(struct device *dev,
	enum usb_phy_type type)
{
	return ERR_PTR(-ENXIO);
}

static inline struct usb_phy *devm_usb_get_phy_by_phandle(struct device *dev,
	const char *phandle, u8 index)
{
	return ERR_PTR(-ENXIO);
}

static inline struct usb_phy *devm_usb_get_phy_by_node(struct device *dev,
	struct device_node *node, struct notifier_block *nb)
{
	return ERR_PTR(-ENXIO);
}

static inline void usb_put_phy(struct usb_phy *x)
{
}

static inline void devm_usb_put_phy(struct device *dev, struct usb_phy *x)
{
}

static inline void usb_phy_set_event(struct usb_phy *x, unsigned long event)
{
}

static inline void usb_phy_set_charger_current(struct usb_phy *usb_phy,
					       unsigned int mA)
{
}

static inline void usb_phy_get_charger_current(struct usb_phy *usb_phy,
					       unsigned int *min,
					       unsigned int *max)
{
}

static inline void usb_phy_set_charger_state(struct usb_phy *usb_phy,
					     enum usb_charger_state state)
{
}
#endif

static inline int
usb_phy_set_power(struct usb_phy *x, unsigned mA)
{
	if (!x)
		return 0;

	usb_phy_set_charger_current(x, mA);

	if (x->set_power)
		return x->set_power(x, mA);
	return 0;
}

/* Context: can sleep */
static inline int
usb_phy_set_suspend(struct usb_phy *x, int suspend)
{
	if (x && x->set_suspend != NULL)
		return x->set_suspend(x, suspend);
	else
		return 0;
}

static inline int
usb_phy_set_wakeup(struct usb_phy *x, bool enabled)
{
	if (x && x->set_wakeup)
		return x->set_wakeup(x, enabled);
	else
		return 0;
}

static inline int
usb_phy_notify_connect(struct usb_phy *x, enum usb_device_speed speed)
{
	if (x && x->notify_connect)
		return x->notify_connect(x, speed);
	else
		return 0;
}

static inline int
usb_phy_notify_disconnect(struct usb_phy *x, enum usb_device_speed speed)
{
	if (x && x->notify_disconnect)
		return x->notify_disconnect(x, speed);
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
