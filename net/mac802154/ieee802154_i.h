/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2007-2012 Siemens AG
 *
 * Written by:
 * Pavel Smolenskiy <pavel.smolenskiy@gmail.com>
 * Maxim Gorbachyov <maxim.gorbachev@siemens.com>
 * Dmitry Eremin-Solenikov <dbaryshkov@gmail.com>
 * Alexander Smirnov <alex.bluesman.smirnov@gmail.com>
 */
#ifndef __IEEE802154_I_H
#define __IEEE802154_I_H

#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/hrtimer.h>
#include <net/cfg802154.h>
#include <net/mac802154.h>
#include <net/nl802154.h>
#include <net/ieee802154_netdev.h>

#include "llsec.h"

enum ieee802154_ongoing {
	IEEE802154_IS_SCANNING = BIT(0),
	IEEE802154_IS_BEACONING = BIT(1),
	IEEE802154_IS_ASSOCIATING = BIT(2),
};

/* mac802154 device private data */
struct ieee802154_local {
	struct ieee802154_hw hw;
	const struct ieee802154_ops *ops;

	/* hardware address filter */
	struct ieee802154_hw_addr_filt addr_filt;
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

	/* Data related workqueue */
	struct workqueue_struct	*workqueue;
	/* MAC commands related workqueue */
	struct workqueue_struct	*mac_wq;

	struct hrtimer ifs_timer;

	/* Scanning */
	u8 scan_page;
	u8 scan_channel;
	struct ieee802154_beacon_req_frame scan_beacon_req;
	struct cfg802154_scan_request __rcu *scan_req;
	struct delayed_work scan_work;

	/* Beaconing */
	unsigned int beacon_interval;
	struct ieee802154_beacon_frame beacon;
	struct cfg802154_beacon_request __rcu *beacon_req;
	struct delayed_work beacon_work;

	/* Asynchronous tasks */
	struct list_head rx_beacon_list;
	struct work_struct rx_beacon_work;
	struct list_head rx_mac_cmd_list;
	struct work_struct rx_mac_cmd_work;

	/* Association */
	struct ieee802154_pan_device *assoc_dev;
	struct completion assoc_done;
	__le16 assoc_addr;
	u8 assoc_status;
	struct work_struct assoc_work;

	bool started;
	bool suspended;
	unsigned long ongoing;

	struct tasklet_struct tasklet;
	struct sk_buff_head skb_queue;

	struct sk_buff *tx_skb;
	struct work_struct sync_tx_work;
	/* A negative Linux error code or a null/positive MLME error status */
	int tx_result;
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

	struct wpan_dev wpan_dev;

	struct ieee802154_local *local;
	struct net_device *dev;

	/* Each interface starts and works in nominal state at a given filtering
	 * level given by iface_default_filtering, which is set once for all at
	 * the interface creation and should not evolve over time. For some MAC
	 * operations however, the filtering level may change temporarily, as
	 * reflected in the required_filtering field. The actual filtering at
	 * the PHY level may be different and is shown in struct wpan_phy.
	 */
	enum ieee802154_filtering_level iface_default_filtering;
	enum ieee802154_filtering_level required_filtering;

	unsigned long state;
	char name[IFNAMSIZ];

	/* protects sec from concurrent access by netlink. access by
	 * encrypt/decrypt/header_create safe without additional protection.
	 */
	struct mutex sec_mtx;

	struct mac802154_llsec sec;
};

/* utility functions/constants */
extern const void *const mac802154_wpan_phy_privid; /*  for wpan_phy privid */

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

static inline struct ieee802154_sub_if_data *
IEEE802154_WPAN_DEV_TO_SUB_IF(struct wpan_dev *wpan_dev)
{
	return container_of(wpan_dev, struct ieee802154_sub_if_data, wpan_dev);
}

static inline bool
ieee802154_sdata_running(struct ieee802154_sub_if_data *sdata)
{
	return test_bit(SDATA_STATE_RUNNING, &sdata->state);
}

static inline int ieee802154_get_mac_cmd(struct sk_buff *skb, u8 *mac_cmd)
{
	struct ieee802154_mac_cmd_pl mac_pl;
	int ret;

	if (mac_cb(skb)->type != IEEE802154_FC_TYPE_MAC_CMD)
		return -EINVAL;

	ret = ieee802154_mac_cmd_pl_pull(skb, &mac_pl);
	if (ret)
		return ret;

	*mac_cmd = mac_pl.cmd_id;
	return 0;
}

extern struct ieee802154_mlme_ops mac802154_mlme_wpan;

void ieee802154_rx(struct ieee802154_local *local, struct sk_buff *skb);
void ieee802154_xmit_sync_worker(struct work_struct *work);
int ieee802154_sync_and_hold_queue(struct ieee802154_local *local);
int ieee802154_mlme_op_pre(struct ieee802154_local *local);
int ieee802154_mlme_tx(struct ieee802154_local *local,
		       struct ieee802154_sub_if_data *sdata,
		       struct sk_buff *skb);
int ieee802154_mlme_tx_locked(struct ieee802154_local *local,
			      struct ieee802154_sub_if_data *sdata,
			      struct sk_buff *skb);
void ieee802154_mlme_op_post(struct ieee802154_local *local);
int ieee802154_mlme_tx_one_locked(struct ieee802154_local *local,
				  struct ieee802154_sub_if_data *sdata,
				  struct sk_buff *skb);
netdev_tx_t
ieee802154_monitor_start_xmit(struct sk_buff *skb, struct net_device *dev);
netdev_tx_t
ieee802154_subif_start_xmit(struct sk_buff *skb, struct net_device *dev);
enum hrtimer_restart ieee802154_xmit_ifs_timer(struct hrtimer *timer);

/**
 * ieee802154_hold_queue - hold ieee802154 queue
 * @local: main mac object
 *
 * Hold a queue by incrementing an atomic counter and requesting the netif
 * queues to be stopped. The queues cannot be woken up while the counter has not
 * been reset with as any ieee802154_release_queue() calls as needed.
 */
void ieee802154_hold_queue(struct ieee802154_local *local);

/**
 * ieee802154_release_queue - release ieee802154 queue
 * @local: main mac object
 *
 * Release a queue which is held by decrementing an atomic counter and wake it
 * up only if the counter reaches 0.
 */
void ieee802154_release_queue(struct ieee802154_local *local);

/**
 * ieee802154_disable_queue - disable ieee802154 queue
 * @local: main mac object
 *
 * When trying to sync the Tx queue, we cannot just stop the queue
 * (which is basically a bit being set without proper lock handling)
 * because it would be racy. We actually need to call netif_tx_disable()
 * instead, which is done by this helper. Restarting the queue can
 * however still be done with a regular wake call.
 */
void ieee802154_disable_queue(struct ieee802154_local *local);

/* MIB callbacks */
void mac802154_dev_set_page_channel(struct net_device *dev, u8 page, u8 chan);

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

int mac802154_wpan_update_llsec(struct net_device *dev);

/* PAN management handling */
void mac802154_scan_worker(struct work_struct *work);
int mac802154_trigger_scan_locked(struct ieee802154_sub_if_data *sdata,
				  struct cfg802154_scan_request *request);
int mac802154_abort_scan_locked(struct ieee802154_local *local,
				struct ieee802154_sub_if_data *sdata);
int mac802154_process_beacon(struct ieee802154_local *local,
			     struct sk_buff *skb,
			     u8 page, u8 channel);
void mac802154_rx_beacon_worker(struct work_struct *work);

static inline bool mac802154_is_scanning(struct ieee802154_local *local)
{
	return test_bit(IEEE802154_IS_SCANNING, &local->ongoing);
}

void mac802154_beacon_worker(struct work_struct *work);
int mac802154_send_beacons_locked(struct ieee802154_sub_if_data *sdata,
				  struct cfg802154_beacon_request *request);
int mac802154_stop_beacons_locked(struct ieee802154_local *local,
				  struct ieee802154_sub_if_data *sdata);

static inline bool mac802154_is_beaconing(struct ieee802154_local *local)
{
	return test_bit(IEEE802154_IS_BEACONING, &local->ongoing);
}

void mac802154_rx_mac_cmd_worker(struct work_struct *work);

int mac802154_perform_association(struct ieee802154_sub_if_data *sdata,
				  struct ieee802154_pan_device *coord,
				  __le16 *short_addr);
int mac802154_process_association_resp(struct ieee802154_sub_if_data *sdata,
				       struct sk_buff *skb);

static inline bool mac802154_is_associating(struct ieee802154_local *local)
{
	return test_bit(IEEE802154_IS_ASSOCIATING, &local->ongoing);
}

int mac802154_send_disassociation_notif(struct ieee802154_sub_if_data *sdata,
					struct ieee802154_pan_device *target,
					u8 reason);
int mac802154_process_disassociation_notif(struct ieee802154_sub_if_data *sdata,
					   struct sk_buff *skb);
int mac802154_process_association_req(struct ieee802154_sub_if_data *sdata,
				      struct sk_buff *skb);

/* interface handling */
int ieee802154_iface_init(void);
void ieee802154_iface_exit(void);
void ieee802154_if_remove(struct ieee802154_sub_if_data *sdata);
struct net_device *
ieee802154_if_add(struct ieee802154_local *local, const char *name,
		  unsigned char name_assign_type, enum nl802154_iftype type,
		  __le64 extended_addr);
void ieee802154_remove_interfaces(struct ieee802154_local *local);
void ieee802154_stop_device(struct ieee802154_local *local);

#endif /* __IEEE802154_I_H */
