/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for generic time stamping devices on MII buses.
 * Copyright (C) 2018 Richard Cochran <richardcochran@gmail.com>
 */
#ifndef _LINUX_MII_TIMESTAMPER_H
#define _LINUX_MII_TIMESTAMPER_H

#include <linux/device.h>
#include <linux/ethtool.h>
#include <linux/skbuff.h>
#include <linux/net_tstamp.h>

struct phy_device;

/**
 * struct mii_timestamper - Callback interface to MII time stamping devices.
 *
 * @rxtstamp:	Requests a Rx timestamp for 'skb'.  If the skb is accepted,
 *		the MII time stamping device promises to deliver it using
 *		netif_rx() as soon as a timestamp becomes available. One of
 *		the PTP_CLASS_ values is passed in 'type'.  The function
 *		must return true if the skb is accepted for delivery.
 *
 * @txtstamp:	Requests a Tx timestamp for 'skb'.  The MII time stamping
 *		device promises to deliver it using skb_complete_tx_timestamp()
 *		as soon as a timestamp becomes available. One of the PTP_CLASS_
 *		values is passed in 'type'.
 *
 * @hwtstamp:	Handles SIOCSHWTSTAMP ioctl for hardware time stamping.
 *
 * @link_state: Allows the device to respond to changes in the link
 *		state.  The caller invokes this function while holding
 *		the phy_device mutex.
 *
 * @ts_info:	Handles ethtool queries for hardware time stamping.
 * @device:	Remembers the device to which the instance belongs.
 *
 * Drivers for PHY time stamping devices should embed their
 * mii_timestamper within a private structure, obtaining a reference
 * to it using container_of().
 *
 * Drivers for non-PHY time stamping devices should return a pointer
 * to a mii_timestamper from the probe_channel() callback of their
 * mii_timestamping_ctrl interface.
 */
struct mii_timestamper {
	bool (*rxtstamp)(struct mii_timestamper *mii_ts,
			 struct sk_buff *skb, int type);

	void (*txtstamp)(struct mii_timestamper *mii_ts,
			 struct sk_buff *skb, int type);

	int  (*hwtstamp)(struct mii_timestamper *mii_ts,
			 struct kernel_hwtstamp_config *kernel_config,
			 struct netlink_ext_ack *extack);

	void (*link_state)(struct mii_timestamper *mii_ts,
			   struct phy_device *phydev);

	int  (*ts_info)(struct mii_timestamper *mii_ts,
			struct kernel_ethtool_ts_info *ts_info);

	struct device *device;
};

/**
 * struct mii_timestamping_ctrl - MII time stamping controller interface.
 *
 * @probe_channel:	Callback into the controller driver announcing the
 *			presence of the 'port' channel.  The 'device' field
 *			had been passed to register_mii_tstamp_controller().
 *			The driver must return either a pointer to a valid
 *			MII timestamper instance or PTR_ERR.
 *
 * @release_channel:	Releases an instance obtained via .probe_channel.
 */
struct mii_timestamping_ctrl {
	struct mii_timestamper *(*probe_channel)(struct device *device,
						 unsigned int port);
	void (*release_channel)(struct device *device,
				struct mii_timestamper *mii_ts);
};

#ifdef CONFIG_NETWORK_PHY_TIMESTAMPING

int register_mii_tstamp_controller(struct device *device,
				   struct mii_timestamping_ctrl *ctrl);

void unregister_mii_tstamp_controller(struct device *device);

struct mii_timestamper *register_mii_timestamper(struct device_node *node,
						 unsigned int port);

void unregister_mii_timestamper(struct mii_timestamper *mii_ts);

#else

static inline
int register_mii_tstamp_controller(struct device *device,
				   struct mii_timestamping_ctrl *ctrl)
{
	return -EOPNOTSUPP;
}

static inline void unregister_mii_tstamp_controller(struct device *device)
{
}

static inline
struct mii_timestamper *register_mii_timestamper(struct device_node *node,
						 unsigned int port)
{
	return NULL;
}

static inline void unregister_mii_timestamper(struct mii_timestamper *mii_ts)
{
}

#endif

#endif
