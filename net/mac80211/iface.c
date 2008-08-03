/*
 * Copyright 2002-2005, Instant802 Networks, Inc.
 * Copyright 2005-2006, Devicescape Software, Inc.
 * Copyright (c) 2006 Jiri Benc <jbenc@suse.cz>
 * Copyright 2008, Johannes Berg <johannes@sipsolutions.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/if_arp.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include <net/mac80211.h>
#include "ieee80211_i.h"
#include "sta_info.h"
#include "debugfs_netdev.h"
#include "mesh.h"

/*
 * Called when the netdev is removed or, by the code below, before
 * the interface type changes.
 */
static void ieee80211_teardown_sdata(struct net_device *dev)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_local *local = sdata->local;
	struct beacon_data *beacon;
	struct sk_buff *skb;
	int flushed;
	int i;

	ieee80211_debugfs_remove_netdev(sdata);

	/* free extra data */
	ieee80211_free_keys(sdata);

	for (i = 0; i < IEEE80211_FRAGMENT_MAX; i++)
		__skb_queue_purge(&sdata->fragments[i].skb_list);
	sdata->fragment_next = 0;

	switch (sdata->vif.type) {
	case IEEE80211_IF_TYPE_AP:
		beacon = sdata->u.ap.beacon;
		rcu_assign_pointer(sdata->u.ap.beacon, NULL);
		synchronize_rcu();
		kfree(beacon);

		while ((skb = skb_dequeue(&sdata->u.ap.ps_bc_buf))) {
			local->total_ps_buffered--;
			dev_kfree_skb(skb);
		}

		break;
	case IEEE80211_IF_TYPE_MESH_POINT:
		/* Allow compiler to elide mesh_rmc_free call. */
		if (ieee80211_vif_is_mesh(&sdata->vif))
			mesh_rmc_free(sdata);
		/* fall through */
	case IEEE80211_IF_TYPE_STA:
	case IEEE80211_IF_TYPE_IBSS:
		kfree(sdata->u.sta.extra_ie);
		kfree(sdata->u.sta.assocreq_ies);
		kfree(sdata->u.sta.assocresp_ies);
		kfree_skb(sdata->u.sta.probe_resp);
		break;
	case IEEE80211_IF_TYPE_WDS:
	case IEEE80211_IF_TYPE_VLAN:
	case IEEE80211_IF_TYPE_MNTR:
		break;
	case IEEE80211_IF_TYPE_INVALID:
		BUG();
		break;
	}

	flushed = sta_info_flush(local, sdata);
	WARN_ON(flushed);
}

/*
 * Helper function to initialise an interface to a specific type.
 */
static void ieee80211_setup_sdata(struct ieee80211_sub_if_data *sdata,
				  enum ieee80211_if_types type)
{
	struct ieee80211_if_sta *ifsta;

	/* clear type-dependent union */
	memset(&sdata->u, 0, sizeof(sdata->u));

	/* and set some type-dependent values */
	sdata->vif.type = type;

	/* only monitor differs */
	sdata->dev->type = ARPHRD_ETHER;

	switch (type) {
	case IEEE80211_IF_TYPE_AP:
		skb_queue_head_init(&sdata->u.ap.ps_bc_buf);
		INIT_LIST_HEAD(&sdata->u.ap.vlans);
		break;
	case IEEE80211_IF_TYPE_MESH_POINT:
	case IEEE80211_IF_TYPE_STA:
	case IEEE80211_IF_TYPE_IBSS:
		ifsta = &sdata->u.sta;
		INIT_WORK(&ifsta->work, ieee80211_sta_work);
		setup_timer(&ifsta->timer, ieee80211_sta_timer,
			    (unsigned long) sdata);
		skb_queue_head_init(&ifsta->skb_queue);

		ifsta->capab = WLAN_CAPABILITY_ESS;
		ifsta->auth_algs = IEEE80211_AUTH_ALG_OPEN |
			IEEE80211_AUTH_ALG_SHARED_KEY;
		ifsta->flags |= IEEE80211_STA_CREATE_IBSS |
			IEEE80211_STA_AUTO_BSSID_SEL |
			IEEE80211_STA_AUTO_CHANNEL_SEL;
		if (ieee80211_num_regular_queues(&sdata->local->hw) >= 4)
			ifsta->flags |= IEEE80211_STA_WMM_ENABLED;

		if (ieee80211_vif_is_mesh(&sdata->vif))
			ieee80211_mesh_init_sdata(sdata);
		break;
	case IEEE80211_IF_TYPE_MNTR:
		sdata->dev->type = ARPHRD_IEEE80211_RADIOTAP;
		sdata->dev->hard_start_xmit = ieee80211_monitor_start_xmit;
		sdata->u.mntr_flags = MONITOR_FLAG_CONTROL |
				      MONITOR_FLAG_OTHER_BSS;
		break;
	case IEEE80211_IF_TYPE_WDS:
	case IEEE80211_IF_TYPE_VLAN:
		break;
	case IEEE80211_IF_TYPE_INVALID:
		BUG();
		break;
	}

	ieee80211_debugfs_add_netdev(sdata);
}

int ieee80211_if_change_type(struct ieee80211_sub_if_data *sdata,
			     enum ieee80211_if_types type)
{
	ASSERT_RTNL();

	if (type == sdata->vif.type)
		return 0;

	/*
	 * We could, here, on changes between IBSS/STA/MESH modes,
	 * invoke an MLME function instead that disassociates etc.
	 * and goes into the requested mode.
	 */

	if (netif_running(sdata->dev))
		return -EBUSY;

	/* Purge and reset type-dependent state. */
	ieee80211_teardown_sdata(sdata->dev);
	ieee80211_setup_sdata(sdata, type);

	/* reset some values that shouldn't be kept across type changes */
	sdata->basic_rates = 0;
	sdata->drop_unencrypted = 0;

	return 0;
}

int ieee80211_if_add(struct ieee80211_local *local, const char *name,
		     struct net_device **new_dev, enum ieee80211_if_types type,
		     struct vif_params *params)
{
	struct net_device *ndev;
	struct ieee80211_sub_if_data *sdata = NULL;
	int ret, i;

	ASSERT_RTNL();

	ndev = alloc_netdev(sizeof(*sdata) + local->hw.vif_data_size,
			    name, ieee80211_if_setup);
	if (!ndev)
		return -ENOMEM;

	ndev->needed_headroom = local->tx_headroom +
				4*6 /* four MAC addresses */
				+ 2 + 2 + 2 + 2 /* ctl, dur, seq, qos */
				+ 6 /* mesh */
				+ 8 /* rfc1042/bridge tunnel */
				- ETH_HLEN /* ethernet hard_header_len */
				+ IEEE80211_ENCRYPT_HEADROOM;
	ndev->needed_tailroom = IEEE80211_ENCRYPT_TAILROOM;

	ret = dev_alloc_name(ndev, ndev->name);
	if (ret < 0)
		goto fail;

	memcpy(ndev->dev_addr, local->hw.wiphy->perm_addr, ETH_ALEN);
	SET_NETDEV_DEV(ndev, wiphy_dev(local->hw.wiphy));

	/* don't use IEEE80211_DEV_TO_SUB_IF because it checks too much */
	sdata = netdev_priv(ndev);
	ndev->ieee80211_ptr = &sdata->wdev;

	/* initialise type-independent data */
	sdata->wdev.wiphy = local->hw.wiphy;
	sdata->local = local;
	sdata->dev = ndev;

	for (i = 0; i < IEEE80211_FRAGMENT_MAX; i++)
		skb_queue_head_init(&sdata->fragments[i].skb_list);

	INIT_LIST_HEAD(&sdata->key_list);

	sdata->force_unicast_rateidx = -1;
	sdata->max_ratectrl_rateidx = -1;

	/* setup type-dependent data */
	ieee80211_setup_sdata(sdata, type);

	ret = register_netdevice(ndev);
	if (ret)
		goto fail;

	ndev->uninit = ieee80211_teardown_sdata;

	if (ieee80211_vif_is_mesh(&sdata->vif) &&
	    params && params->mesh_id_len)
		ieee80211_if_sta_set_mesh_id(&sdata->u.sta,
					     params->mesh_id_len,
					     params->mesh_id);

	list_add_tail_rcu(&sdata->list, &local->interfaces);

	if (new_dev)
		*new_dev = ndev;

	return 0;

 fail:
	free_netdev(ndev);
	return ret;
}

void ieee80211_if_remove(struct ieee80211_sub_if_data *sdata)
{
	ASSERT_RTNL();

	list_del_rcu(&sdata->list);
	synchronize_rcu();
	unregister_netdevice(sdata->dev);
}

/*
 * Remove all interfaces, may only be called at hardware unregistration
 * time because it doesn't do RCU-safe list removals.
 */
void ieee80211_remove_interfaces(struct ieee80211_local *local)
{
	struct ieee80211_sub_if_data *sdata, *tmp;

	ASSERT_RTNL();

	list_for_each_entry_safe(sdata, tmp, &local->interfaces, list) {
		list_del(&sdata->list);
		unregister_netdevice(sdata->dev);
	}
}
