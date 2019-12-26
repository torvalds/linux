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
 *
 * Drivers for PHY time stamping devices should embed their
 * mii_timestamper within a private structure, obtaining a reference
 * to it using container_of().
 */
struct mii_timestamper {
	bool (*rxtstamp)(struct mii_timestamper *mii_ts,
			 struct sk_buff *skb, int type);

	void (*txtstamp)(struct mii_timestamper *mii_ts,
			 struct sk_buff *skb, int type);

	int  (*hwtstamp)(struct mii_timestamper *mii_ts,
			 struct ifreq *ifreq);

	void (*link_state)(struct mii_timestamper *mii_ts,
			   struct phy_device *phydev);

	int  (*ts_info)(struct mii_timestamper *mii_ts,
			struct ethtool_ts_info *ts_info);
};

#endif
