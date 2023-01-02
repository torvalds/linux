/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2021, Linaro Ltd <loic.poulain@linaro.org> */

#ifndef __WWAN_H
#define __WWAN_H

#include <linux/poll.h>
#include <linux/netdevice.h>
#include <linux/types.h>

/**
 * enum wwan_port_type - WWAN port types
 * @WWAN_PORT_AT: AT commands
 * @WWAN_PORT_MBIM: Mobile Broadband Interface Model control
 * @WWAN_PORT_QMI: Qcom modem/MSM interface for modem control
 * @WWAN_PORT_QCDM: Qcom Modem diagnostic interface
 * @WWAN_PORT_FIREHOSE: XML based command protocol
 * @WWAN_PORT_XMMRPC: Control protocol for Intel XMM modems
 *
 * @WWAN_PORT_MAX: Highest supported port types
 * @WWAN_PORT_UNKNOWN: Special value to indicate an unknown port type
 * @__WWAN_PORT_MAX: Internal use
 */
enum wwan_port_type {
	WWAN_PORT_AT,
	WWAN_PORT_MBIM,
	WWAN_PORT_QMI,
	WWAN_PORT_QCDM,
	WWAN_PORT_FIREHOSE,
	WWAN_PORT_XMMRPC,

	/* Add new port types above this line */

	__WWAN_PORT_MAX,
	WWAN_PORT_MAX = __WWAN_PORT_MAX - 1,
	WWAN_PORT_UNKNOWN,
};

struct device;
struct file;
struct netlink_ext_ack;
struct sk_buff;
struct wwan_port;

/** struct wwan_port_ops - The WWAN port operations
 * @start: The routine for starting the WWAN port device.
 * @stop: The routine for stopping the WWAN port device.
 * @tx: Non-blocking routine that sends WWAN port protocol data to the device.
 * @tx_blocking: Optional blocking routine that sends WWAN port protocol data
 *               to the device.
 * @tx_poll: Optional routine that sets additional TX poll flags.
 *
 * The wwan_port_ops structure contains a list of low-level operations
 * that control a WWAN port device. All functions are mandatory unless specified.
 */
struct wwan_port_ops {
	int (*start)(struct wwan_port *port);
	void (*stop)(struct wwan_port *port);
	int (*tx)(struct wwan_port *port, struct sk_buff *skb);

	/* Optional operations */
	int (*tx_blocking)(struct wwan_port *port, struct sk_buff *skb);
	__poll_t (*tx_poll)(struct wwan_port *port, struct file *filp,
			    poll_table *wait);
};

/**
 * wwan_create_port - Add a new WWAN port
 * @parent: Device to use as parent and shared by all WWAN ports
 * @type: WWAN port type
 * @ops: WWAN port operations
 * @drvdata: Pointer to caller driver data
 *
 * Allocate and register a new WWAN port. The port will be automatically exposed
 * to user as a character device and attached to the right virtual WWAN device,
 * based on the parent pointer. The parent pointer is the device shared by all
 * components of a same WWAN modem (e.g. USB dev, PCI dev, MHI controller...).
 *
 * drvdata will be placed in the WWAN port device driver data and can be
 * retrieved with wwan_port_get_drvdata().
 *
 * This function must be balanced with a call to wwan_remove_port().
 *
 * Returns a valid pointer to wwan_port on success or PTR_ERR on failure
 */
struct wwan_port *wwan_create_port(struct device *parent,
				   enum wwan_port_type type,
				   const struct wwan_port_ops *ops,
				   void *drvdata);

/**
 * wwan_remove_port - Remove a WWAN port
 * @port: WWAN port to remove
 *
 * Remove a previously created port.
 */
void wwan_remove_port(struct wwan_port *port);

/**
 * wwan_port_rx - Receive data from the WWAN port
 * @port: WWAN port for which data is received
 * @skb: Pointer to the rx buffer
 *
 * A port driver calls this function upon data reception (MBIM, AT...).
 */
void wwan_port_rx(struct wwan_port *port, struct sk_buff *skb);

/**
 * wwan_port_txoff - Stop TX on WWAN port
 * @port: WWAN port for which TX must be stopped
 *
 * Used for TX flow control, a port driver calls this function to indicate TX
 * is temporary unavailable (e.g. due to ring buffer fullness).
 */
void wwan_port_txoff(struct wwan_port *port);


/**
 * wwan_port_txon - Restart TX on WWAN port
 * @port: WWAN port for which TX must be restarted
 *
 * Used for TX flow control, a port driver calls this function to indicate TX
 * is available again.
 */
void wwan_port_txon(struct wwan_port *port);

/**
 * wwan_port_get_drvdata - Retrieve driver data from a WWAN port
 * @port: Related WWAN port
 */
void *wwan_port_get_drvdata(struct wwan_port *port);

/**
 * struct wwan_netdev_priv - WWAN core network device private data
 * @link_id: WWAN device data link id
 * @drv_priv: driver private data area, size is determined in &wwan_ops
 */
struct wwan_netdev_priv {
	u32 link_id;

	/* must be last */
	u8 drv_priv[] __aligned(sizeof(void *));
};

static inline void *wwan_netdev_drvpriv(struct net_device *dev)
{
	return ((struct wwan_netdev_priv *)netdev_priv(dev))->drv_priv;
}

/*
 * Used to indicate that the WWAN core should not create a default network
 * link.
 */
#define WWAN_NO_DEFAULT_LINK		U32_MAX

/**
 * struct wwan_ops - WWAN device ops
 * @priv_size: size of private netdev data area
 * @setup: set up a new netdev
 * @newlink: register the new netdev
 * @dellink: remove the given netdev
 */
struct wwan_ops {
	unsigned int priv_size;
	void (*setup)(struct net_device *dev);
	int (*newlink)(void *ctxt, struct net_device *dev,
		       u32 if_id, struct netlink_ext_ack *extack);
	void (*dellink)(void *ctxt, struct net_device *dev,
			struct list_head *head);
};

int wwan_register_ops(struct device *parent, const struct wwan_ops *ops,
		      void *ctxt, u32 def_link_id);

void wwan_unregister_ops(struct device *parent);

#ifdef CONFIG_WWAN_DEBUGFS
struct dentry *wwan_get_debugfs_dir(struct device *parent);
void wwan_put_debugfs_dir(struct dentry *dir);
#else
static inline struct dentry *wwan_get_debugfs_dir(struct device *parent)
{
	return ERR_PTR(-ENODEV);
}
static inline void wwan_put_debugfs_dir(struct dentry *dir) {}
#endif

#endif /* __WWAN_H */
