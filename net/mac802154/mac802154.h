/*
 * Copyright (C) 2007-2012 Siemens AG
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Written by:
 * Pavel Smolenskiy <pavel.smolenskiy@gmail.com>
 * Maxim Gorbachyov <maxim.gorbachev@siemens.com>
 * Dmitry Eremin-Solenikov <dbaryshkov@gmail.com>
 * Alexander Smirnov <alex.bluesman.smirnov@gmail.com>
 */
#ifndef MAC802154_H
#define MAC802154_H

/* mac802154 device private data */
struct mac802154_priv {
	struct ieee802154_dev hw;
	struct ieee802154_ops *ops;

	/* ieee802154 phy */
	struct wpan_phy *phy;

	int open_count;

	/* As in mac80211 slaves list is modified:
	 * 1) under the RTNL
	 * 2) protected by slaves_mtx;
	 * 3) in an RCU manner
	 *
	 * So atomic readers can use any of this protection methods.
	 */
	struct list_head	slaves;
	struct mutex		slaves_mtx;

	/* This one is used for scanning and other jobs not to be interfered
	 * with serial driver.
	 */
	struct workqueue_struct	*dev_workqueue;

	/* SoftMAC device is registered and running. One can add subinterfaces.
	 * This flag should be modified under slaves_mtx and RTNL, so you can
	 * read them using any of protection methods.
	 */
	bool running;
};

#define	MAC802154_DEVICE_STOPPED	0x00
#define MAC802154_DEVICE_RUN		0x01

#define mac802154_to_priv(_hw)	container_of(_hw, struct mac802154_priv, hw)

#endif /* MAC802154_H */
