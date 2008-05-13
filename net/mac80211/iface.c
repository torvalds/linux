/*
 * Copyright 2002-2005, Instant802 Networks, Inc.
 * Copyright 2005-2006, Devicescape Software, Inc.
 * Copyright (c) 2006 Jiri Benc <jbenc@suse.cz>
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

void ieee80211_if_sdata_init(struct ieee80211_sub_if_data *sdata)
{
	int i;

	/* Default values for sub-interface parameters */
	sdata->drop_unencrypted = 0;
	for (i = 0; i < IEEE80211_FRAGMENT_MAX; i++)
		skb_queue_head_init(&sdata->fragments[i].skb_list);

	INIT_LIST_HEAD(&sdata->key_list);
}

static void ieee80211_if_sdata_deinit(struct ieee80211_sub_if_data *sdata)
{
	int i;

	for (i = 0; i < IEEE80211_FRAGMENT_MAX; i++) {
		__skb_queue_purge(&sdata->fragments[i].skb_list);
	}
}

/* Must be called with rtnl lock held. */
int ieee80211_if_add(struct net_device *dev, const char *name,
		     struct net_device **new_dev, int type,
		     struct vif_params *params)
{
	struct net_device *ndev;
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sub_if_data *sdata = NULL;
	int ret;

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
	ndev->base_addr = dev->base_addr;
	ndev->irq = dev->irq;
	ndev->mem_start = dev->mem_start;
	ndev->mem_end = dev->mem_end;
	SET_NETDEV_DEV(ndev, wiphy_dev(local->hw.wiphy));

	sdata = IEEE80211_DEV_TO_SUB_IF(ndev);
	ndev->ieee80211_ptr = &sdata->wdev;
	sdata->wdev.wiphy = local->hw.wiphy;
	sdata->vif.type = IEEE80211_IF_TYPE_AP;
	sdata->dev = ndev;
	sdata->local = local;
	ieee80211_if_sdata_init(sdata);

	ret = register_netdevice(ndev);
	if (ret)
		goto fail;

	ieee80211_debugfs_add_netdev(sdata);
	ieee80211_if_set_type(ndev, type);

	if (ieee80211_vif_is_mesh(&sdata->vif) &&
	    params && params->mesh_id_len)
		ieee80211_if_sta_set_mesh_id(&sdata->u.sta,
					     params->mesh_id_len,
					     params->mesh_id);

	/* we're under RTNL so all this is fine */
	if (unlikely(local->reg_state == IEEE80211_DEV_UNREGISTERED)) {
		__ieee80211_if_del(local, sdata);
		return -ENODEV;
	}
	list_add_tail_rcu(&sdata->list, &local->interfaces);

	if (new_dev)
		*new_dev = ndev;

	return 0;

fail:
	free_netdev(ndev);
	return ret;
}

void ieee80211_if_set_type(struct net_device *dev, int type)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	int oldtype = sdata->vif.type;

	/*
	 * We need to call this function on the master interface
	 * which already has a hard_start_xmit routine assigned
	 * which must not be changed.
	 */
	if (dev != sdata->local->mdev)
		dev->hard_start_xmit = ieee80211_subif_start_xmit;

	/*
	 * Called even when register_netdevice fails, it would
	 * oops if assigned before initialising the rest.
	 */
	dev->uninit = ieee80211_if_reinit;

	/* most have no BSS pointer */
	sdata->bss = NULL;
	sdata->vif.type = type;

	sdata->basic_rates = 0;

	switch (type) {
	case IEEE80211_IF_TYPE_WDS:
		/* nothing special */
		break;
	case IEEE80211_IF_TYPE_VLAN:
		sdata->u.vlan.ap = NULL;
		break;
	case IEEE80211_IF_TYPE_AP:
		sdata->u.ap.force_unicast_rateidx = -1;
		sdata->u.ap.max_ratectrl_rateidx = -1;
		skb_queue_head_init(&sdata->u.ap.ps_bc_buf);
		sdata->bss = &sdata->u.ap;
		INIT_LIST_HEAD(&sdata->u.ap.vlans);
		break;
	case IEEE80211_IF_TYPE_MESH_POINT:
	case IEEE80211_IF_TYPE_STA:
	case IEEE80211_IF_TYPE_IBSS: {
		struct ieee80211_sub_if_data *msdata;
		struct ieee80211_if_sta *ifsta;

		ifsta = &sdata->u.sta;
		INIT_WORK(&ifsta->work, ieee80211_sta_work);
		setup_timer(&ifsta->timer, ieee80211_sta_timer,
			    (unsigned long) sdata);
		skb_queue_head_init(&ifsta->skb_queue);

		ifsta->capab = WLAN_CAPABILITY_ESS;
		ifsta->auth_algs = IEEE80211_AUTH_ALG_OPEN |
			IEEE80211_AUTH_ALG_SHARED_KEY;
		ifsta->flags |= IEEE80211_STA_CREATE_IBSS |
			IEEE80211_STA_WMM_ENABLED |
			IEEE80211_STA_AUTO_BSSID_SEL |
			IEEE80211_STA_AUTO_CHANNEL_SEL;

		msdata = IEEE80211_DEV_TO_SUB_IF(sdata->local->mdev);
		sdata->bss = &msdata->u.ap;

		if (ieee80211_vif_is_mesh(&sdata->vif))
			ieee80211_mesh_init_sdata(sdata);
		break;
	}
	case IEEE80211_IF_TYPE_MNTR:
		dev->type = ARPHRD_IEEE80211_RADIOTAP;
		dev->hard_start_xmit = ieee80211_monitor_start_xmit;
		sdata->u.mntr_flags = MONITOR_FLAG_CONTROL |
				      MONITOR_FLAG_OTHER_BSS;
		break;
	default:
		printk(KERN_WARNING "%s: %s: Unknown interface type 0x%x",
		       dev->name, __func__, type);
	}
	ieee80211_debugfs_change_if_type(sdata, oldtype);
}

/* Must be called with rtnl lock held. */
void ieee80211_if_reinit(struct net_device *dev)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct sk_buff *skb;
	int flushed;

	ASSERT_RTNL();

	ieee80211_free_keys(sdata);

	ieee80211_if_sdata_deinit(sdata);

	/* Need to handle mesh specially to allow eliding the function call */
	if (ieee80211_vif_is_mesh(&sdata->vif))
		mesh_rmc_free(dev);

	switch (sdata->vif.type) {
	case IEEE80211_IF_TYPE_INVALID:
		/* cannot happen */
		WARN_ON(1);
		break;
	case IEEE80211_IF_TYPE_AP: {
		/* Remove all virtual interfaces that use this BSS
		 * as their sdata->bss */
		struct ieee80211_sub_if_data *tsdata, *n;
		struct beacon_data *beacon;

		list_for_each_entry_safe(tsdata, n, &local->interfaces, list) {
			if (tsdata != sdata && tsdata->bss == &sdata->u.ap) {
				printk(KERN_DEBUG "%s: removing virtual "
				       "interface %s because its BSS interface"
				       " is being removed\n",
				       sdata->dev->name, tsdata->dev->name);
				list_del_rcu(&tsdata->list);
				/*
				 * We have lots of time and can afford
				 * to sync for each interface
				 */
				synchronize_rcu();
				__ieee80211_if_del(local, tsdata);
			}
		}

		beacon = sdata->u.ap.beacon;
		rcu_assign_pointer(sdata->u.ap.beacon, NULL);
		synchronize_rcu();
		kfree(beacon);

		while ((skb = skb_dequeue(&sdata->u.ap.ps_bc_buf))) {
			local->total_ps_buffered--;
			dev_kfree_skb(skb);
		}

		break;
	}
	case IEEE80211_IF_TYPE_WDS:
		/* nothing to do */
		break;
	case IEEE80211_IF_TYPE_MESH_POINT:
	case IEEE80211_IF_TYPE_STA:
	case IEEE80211_IF_TYPE_IBSS:
		kfree(sdata->u.sta.extra_ie);
		sdata->u.sta.extra_ie = NULL;
		kfree(sdata->u.sta.assocreq_ies);
		sdata->u.sta.assocreq_ies = NULL;
		kfree(sdata->u.sta.assocresp_ies);
		sdata->u.sta.assocresp_ies = NULL;
		if (sdata->u.sta.probe_resp) {
			dev_kfree_skb(sdata->u.sta.probe_resp);
			sdata->u.sta.probe_resp = NULL;
		}

		break;
	case IEEE80211_IF_TYPE_MNTR:
		dev->type = ARPHRD_ETHER;
		break;
	case IEEE80211_IF_TYPE_VLAN:
		sdata->u.vlan.ap = NULL;
		break;
	}

	flushed = sta_info_flush(local, sdata);
	WARN_ON(flushed);

	memset(&sdata->u, 0, sizeof(sdata->u));
	ieee80211_if_sdata_init(sdata);
}

/* Must be called with rtnl lock held. */
void __ieee80211_if_del(struct ieee80211_local *local,
			struct ieee80211_sub_if_data *sdata)
{
	struct net_device *dev = sdata->dev;

	ieee80211_debugfs_remove_netdev(sdata);
	unregister_netdevice(dev);
	/* Except master interface, the net_device will be freed by
	 * net_device->destructor (i. e. ieee80211_if_free). */
}

/* Must be called with rtnl lock held. */
int ieee80211_if_remove(struct net_device *dev, const char *name, int id)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sub_if_data *sdata, *n;

	ASSERT_RTNL();

	list_for_each_entry_safe(sdata, n, &local->interfaces, list) {
		if ((sdata->vif.type == id || id == -1) &&
		    strcmp(name, sdata->dev->name) == 0 &&
		    sdata->dev != local->mdev) {
			list_del_rcu(&sdata->list);
			synchronize_rcu();
			__ieee80211_if_del(local, sdata);
			return 0;
		}
	}
	return -ENODEV;
}

void ieee80211_if_free(struct net_device *dev)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	ieee80211_if_sdata_deinit(sdata);
	free_netdev(dev);
}
