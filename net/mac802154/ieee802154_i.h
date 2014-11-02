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
 * Written by:
 * Pavel Smolenskiy <pavel.smolenskiy@gmail.com>
 * Maxim Gorbachyov <maxim.gorbachev@siemens.com>
 * Dmitry Eremin-Solenikov <dbaryshkov@gmail.com>
 * Alexander Smirnov <alex.bluesman.smirnov@gmail.com>
 */
#ifndef __IEEE802154_I_H
#define __IEEE802154_I_H

#include <linux/mutex.h>
#include <net/mac802154.h>
#include <net/ieee802154_netdev.h>

#include "llsec.h"

/* mac802154 device private data */
struct ieee802154_local {
	struct ieee802154_hw hw;
	const struct ieee802154_ops *ops;

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
	struct list_head	interfaces;
	struct mutex		iflist_mtx;

	/* This one is used for scanning and other jobs not to be interfered
	 * with serial driver.
	 */
	struct workqueue_struct	*workqueue;

	bool started;

	struct tasklet_struct tasklet;
	struct sk_buff_head skb_queue;
};

enum {
	IEEE802154_RX_MSG        = 1,
};

enum ieee802154_sdata_state_bits {
	SDATA_STATE_RUNNING,
};

/* Slave interface definition.
 *
 * Slaves represent typical network interfaces available from userspace.
 * Each ieee802154 device/transceiver may have several slaves and able
 * to be associated with several networks at the same time.
 */
struct ieee802154_sub_if_data {
	struct list_head list; /* the ieee802154_priv->slaves list */

	struct ieee802154_local *local;
	struct net_device *dev;

	int type;
	unsigned long state;

	spinlock_t mib_lock;

	__le16 pan_id;
	__le16 short_addr;
	__le64 extended_addr;
	bool promisuous_mode;

	struct ieee802154_mac_params mac_params;

	/* MAC BSN field */
	u8 bsn;
	/* MAC DSN field */
	u8 dsn;

	/* protects sec from concurrent access by netlink. access by
	 * encrypt/decrypt/header_create safe without additional protection.
	 */
	struct mutex sec_mtx;

	struct mac802154_llsec sec;
};

#define MAC802154_CHAN_NONE		0xff /* No channel is assigned */

static inline struct ieee802154_local *
hw_to_local(struct ieee802154_hw *hw)
{
	return container_of(hw, struct ieee802154_local, hw);
}

static inline struct ieee802154_sub_if_data *
IEEE802154_DEV_TO_SUB_IF(const struct net_device *dev)
{
	return netdev_priv(dev);
}

static inline bool
ieee802154_sdata_running(struct ieee802154_sub_if_data *sdata)
{
	return test_bit(SDATA_STATE_RUNNING, &sdata->state);
}

extern struct ieee802154_reduced_mlme_ops mac802154_mlme_reduced;
extern struct ieee802154_mlme_ops mac802154_mlme_wpan;

void mac802154_monitor_setup(struct net_device *dev);
netdev_tx_t
ieee802154_monitor_start_xmit(struct sk_buff *skb, struct net_device *dev);

void mac802154_wpan_setup(struct net_device *dev);
netdev_tx_t
ieee802154_subif_start_xmit(struct sk_buff *skb, struct net_device *dev);

/* MIB callbacks */
void mac802154_dev_set_short_addr(struct net_device *dev, __le16 val);
__le16 mac802154_dev_get_short_addr(const struct net_device *dev);
__le16 mac802154_dev_get_pan_id(const struct net_device *dev);
void mac802154_dev_set_pan_id(struct net_device *dev, __le16 val);
void mac802154_dev_set_page_channel(struct net_device *dev, u8 page, u8 chan);
u8 mac802154_dev_get_dsn(const struct net_device *dev);

int mac802154_get_params(struct net_device *dev,
			 struct ieee802154_llsec_params *params);
int mac802154_set_params(struct net_device *dev,
			 const struct ieee802154_llsec_params *params,
			 int changed);

int mac802154_add_key(struct net_device *dev,
		      const struct ieee802154_llsec_key_id *id,
		      const struct ieee802154_llsec_key *key);
int mac802154_del_key(struct net_device *dev,
		      const struct ieee802154_llsec_key_id *id);

int mac802154_add_dev(struct net_device *dev,
		      const struct ieee802154_llsec_device *llsec_dev);
int mac802154_del_dev(struct net_device *dev, __le64 dev_addr);

int mac802154_add_devkey(struct net_device *dev,
			 __le64 device_addr,
			 const struct ieee802154_llsec_device_key *key);
int mac802154_del_devkey(struct net_device *dev,
			 __le64 device_addr,
			 const struct ieee802154_llsec_device_key *key);

int mac802154_add_seclevel(struct net_device *dev,
			   const struct ieee802154_llsec_seclevel *sl);
int mac802154_del_seclevel(struct net_device *dev,
			   const struct ieee802154_llsec_seclevel *sl);

void mac802154_lock_table(struct net_device *dev);
void mac802154_get_table(struct net_device *dev,
			 struct ieee802154_llsec_table **t);
void mac802154_unlock_table(struct net_device *dev);

struct net_device *
mac802154_add_iface(struct wpan_phy *phy, const char *name, int type);
void mac802154_del_iface(struct wpan_phy *phy, struct net_device *dev);

#endif /* __IEEE802154_I_H */
