/* USB OTG (On The Go) defines */
/*
 *
 * These APIs may be used between USB controllers.  USB device drivers
 * (for either host or peripheral roles) don't use these calls; they
 * continue to use just usb_device and usb_gadget.
 */

#ifndef __LINUX_USB_OTG_H
#define __LINUX_USB_OTG_H

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

#define USB_OTG_PULLUP_ID		(1 << 0)
#define USB_OTG_PULLDOWN_DP		(1 << 1)
#define USB_OTG_PULLDOWN_DM		(1 << 2)
#define USB_OTG_EXT_VBUS_INDICATOR	(1 << 3)
#define USB_OTG_DRV_VBUS		(1 << 4)
#define USB_OTG_DRV_VBUS_EXT		(1 << 5)

struct otg_transceiver;

/* for transceivers connected thru an ULPI interface, the user must
 * provide access ops
 */
struct otg_io_access_ops {
	int (*read)(struct otg_transceiver *otg, u32 reg);
	int (*write)(struct otg_transceiver *otg, u32 val, u32 reg);
};

/*
 * the otg driver needs to interact with both device side and host side
 * usb controllers.  it decides which controller is active at a given
 * moment, using the transceiver, ID signal, HNP and sometimes static
 * configuration information (including "board isn't wired for otg").
 */
struct otg_transceiver {
	struct device		*dev;
	const char		*label;
	unsigned int		 flags;

	u8			default_a;
	enum usb_otg_state	state;

	struct usb_bus		*host;
	struct usb_gadget	*gadget;

	struct otg_io_access_ops	*io_ops;
	void __iomem			*io_priv;

	/* to pass extra port status to the root hub */
	u16			port_status;
	u16			port_change;

	/* initialize/shutdown the OTG controller */
	int	(*init)(struct otg_transceiver *otg);
	void	(*shutdown)(struct otg_transceiver *otg);

	/* bind/unbind the host controller */
	int	(*set_host)(struct otg_transceiver *otg,
				struct usb_bus *host);

	/* bind/unbind the peripheral controller */
	int	(*set_peripheral)(struct otg_transceiver *otg,
				struct usb_gadget *gadget);

	/* effective for B devices, ignored for A-peripheral */
	int	(*set_power)(struct otg_transceiver *otg,
				unsigned mA);

	/* effective for A-peripheral, ignored for B devices */
	int	(*set_vbus)(struct otg_transceiver *otg,
				bool enabled);

	/* for non-OTG B devices: set transceiver into suspend mode */
	int	(*set_suspend)(struct otg_transceiver *otg,
				int suspend);

	/* for B devices only:  start session with A-Host */
	int	(*start_srp)(struct otg_transceiver *otg);

	/* start or continue HNP role switch */
	int	(*start_hnp)(struct otg_transceiver *otg);

};


/* for board-specific init logic */
extern int otg_set_transceiver(struct otg_transceiver *);

#if defined(CONFIG_NOP_USB_XCEIV) || defined(CONFIG_NOP_USB_XCEIV_MODULE)
/* sometimes transceivers are accessed only through e.g. ULPI */
extern void usb_nop_xceiv_register(void);
extern void usb_nop_xceiv_unregister(void);
#else
static inline void usb_nop_xceiv_register(void)
{
}

static inline void usb_nop_xceiv_unregister(void)
{
}
#endif

/* helpers for direct access thru low-level io interface */
static inline int otg_io_read(struct otg_transceiver *otg, u32 reg)
{
	if (otg->io_ops && otg->io_ops->read)
		return otg->io_ops->read(otg, reg);

	return -EINVAL;
}

static inline int otg_io_write(struct otg_transceiver *otg, u32 reg, u32 val)
{
	if (otg->io_ops && otg->io_ops->write)
		return otg->io_ops->write(otg, reg, val);

	return -EINVAL;
}

static inline int
otg_init(struct otg_transceiver *otg)
{
	if (otg->init)
		return otg->init(otg);

	return 0;
}

static inline void
otg_shutdown(struct otg_transceiver *otg)
{
	if (otg->shutdown)
		otg->shutdown(otg);
}

/* for usb host and peripheral controller drivers */
extern struct otg_transceiver *otg_get_transceiver(void);
extern void otg_put_transceiver(struct otg_transceiver *);

/* Context: can sleep */
static inline int
otg_start_hnp(struct otg_transceiver *otg)
{
	return otg->start_hnp(otg);
}

/* Context: can sleep */
static inline int
otg_set_vbus(struct otg_transceiver *otg, bool enabled)
{
	return otg->set_vbus(otg, enabled);
}

/* for HCDs */
static inline int
otg_set_host(struct otg_transceiver *otg, struct usb_bus *host)
{
	return otg->set_host(otg, host);
}

/* for usb peripheral controller drivers */

/* Context: can sleep */
static inline int
otg_set_peripheral(struct otg_transceiver *otg, struct usb_gadget *periph)
{
	return otg->set_peripheral(otg, periph);
}

static inline int
otg_set_power(struct otg_transceiver *otg, unsigned mA)
{
	return otg->set_power(otg, mA);
}

/* Context: can sleep */
static inline int
otg_set_suspend(struct otg_transceiver *otg, int suspend)
{
	if (otg->set_suspend != NULL)
		return otg->set_suspend(otg, suspend);
	else
		return 0;
}

static inline int
otg_start_srp(struct otg_transceiver *otg)
{
	return otg->start_srp(otg);
}


/* for OTG controller drivers (and maybe other stuff) */
extern int usb_bus_start_enum(struct usb_bus *bus, unsigned port_num);

#endif /* __LINUX_USB_OTG_H */
