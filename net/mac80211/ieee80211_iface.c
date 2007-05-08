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

void ieee80211_if_sdata_init(struct ieee80211_sub_if_data *sdata)
{
	int i;

	/* Default values for sub-interface parameters */
	sdata->drop_unencrypted = 0;
	sdata->eapol = 1;
	for (i = 0; i < IEEE80211_FRAGMENT_MAX; i++)
		skb_queue_head_init(&sdata->fragments[i].skb_list);
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
		     struct net_device **new_dev, int type)
{
	struct net_device *ndev;
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sub_if_data *sdata = NULL;
	int ret;

	ASSERT_RTNL();
	ndev = alloc_netdev(sizeof(struct ieee80211_sub_if_data),
			    name, ieee80211_if_setup);
	if (!ndev)
		return -ENOMEM;

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
	sdata->type = IEEE80211_IF_TYPE_AP;
	sdata->dev = ndev;
	sdata->local = local;
	ieee80211_if_sdata_init(sdata);

	ret = register_netdevice(ndev);
	if (ret)
		goto fail;

	ieee80211_debugfs_add_netdev(sdata);
	ieee80211_if_set_type(ndev, type);

	write_lock_bh(&local->sub_if_lock);
	if (unlikely(local->reg_state == IEEE80211_DEV_UNREGISTERED)) {
		write_unlock_bh(&local->sub_if_lock);
		__ieee80211_if_del(local, sdata);
		return -ENODEV;
	}
	list_add(&sdata->list, &local->sub_if_list);
	if (new_dev)
		*new_dev = ndev;
	write_unlock_bh(&local->sub_if_lock);

	ieee80211_update_default_wep_only(local);

	return 0;

fail:
	free_netdev(ndev);
	return ret;
}

int ieee80211_if_add_mgmt(struct ieee80211_local *local)
{
	struct net_device *ndev;
	struct ieee80211_sub_if_data *nsdata;
	int ret;

	ASSERT_RTNL();

	ndev = alloc_netdev(sizeof(struct ieee80211_sub_if_data), "wmgmt%d",
			    ieee80211_if_mgmt_setup);
	if (!ndev)
		return -ENOMEM;
	ret = dev_alloc_name(ndev, ndev->name);
	if (ret < 0)
		goto fail;

	memcpy(ndev->dev_addr, local->hw.wiphy->perm_addr, ETH_ALEN);
	SET_NETDEV_DEV(ndev, wiphy_dev(local->hw.wiphy));

	nsdata = IEEE80211_DEV_TO_SUB_IF(ndev);
	ndev->ieee80211_ptr = &nsdata->wdev;
	nsdata->wdev.wiphy = local->hw.wiphy;
	nsdata->type = IEEE80211_IF_TYPE_MGMT;
	nsdata->dev = ndev;
	nsdata->local = local;
	ieee80211_if_sdata_init(nsdata);

	ret = register_netdevice(ndev);
	if (ret)
		goto fail;

	ieee80211_debugfs_add_netdev(nsdata);

	if (local->open_count > 0)
		dev_open(ndev);
	local->apdev = ndev;
	return 0;

fail:
	free_netdev(ndev);
	return ret;
}

void ieee80211_if_del_mgmt(struct ieee80211_local *local)
{
	struct net_device *apdev;

	ASSERT_RTNL();
	apdev = local->apdev;
	ieee80211_debugfs_remove_netdev(IEEE80211_DEV_TO_SUB_IF(apdev));
	local->apdev = NULL;
	unregister_netdevice(apdev);
}

void ieee80211_if_set_type(struct net_device *dev, int type)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	int oldtype = sdata->type;

	sdata->type = type;
	switch (type) {
	case IEEE80211_IF_TYPE_WDS:
		sdata->bss = NULL;
		break;
	case IEEE80211_IF_TYPE_VLAN:
		break;
	case IEEE80211_IF_TYPE_AP:
		sdata->u.ap.dtim_period = 2;
		sdata->u.ap.force_unicast_rateidx = -1;
		sdata->u.ap.max_ratectrl_rateidx = -1;
		skb_queue_head_init(&sdata->u.ap.ps_bc_buf);
		sdata->bss = &sdata->u.ap;
		break;
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
		ifsta->create_ibss = 1;
		ifsta->wmm_enabled = 1;
		ifsta->auto_channel_sel = 1;
		ifsta->auto_bssid_sel = 1;

		msdata = IEEE80211_DEV_TO_SUB_IF(sdata->local->mdev);
		sdata->bss = &msdata->u.ap;
		break;
	}
	case IEEE80211_IF_TYPE_MNTR:
		dev->type = ARPHRD_IEEE80211_RADIOTAP;
		break;
	default:
		printk(KERN_WARNING "%s: %s: Unknown interface type 0x%x",
		       dev->name, __FUNCTION__, type);
	}
	ieee80211_debugfs_change_if_type(sdata, oldtype);
	ieee80211_update_default_wep_only(local);
}

/* Must be called with rtnl lock held. */
void ieee80211_if_reinit(struct net_device *dev)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct sta_info *sta;
	int i;

	ASSERT_RTNL();
	ieee80211_if_sdata_deinit(sdata);
	for (i = 0; i < NUM_DEFAULT_KEYS; i++) {
		if (!sdata->keys[i])
			continue;
#if 0
		/* The interface is down at the moment, so there is not
		 * really much point in disabling the keys at this point. */
		memset(addr, 0xff, ETH_ALEN);
		if (local->ops->set_key)
			local->ops->set_key(local_to_hw(local), DISABLE_KEY, addr,
					    local->keys[i], 0);
#endif
		ieee80211_key_free(sdata->keys[i]);
		sdata->keys[i] = NULL;
	}

	switch (sdata->type) {
	case IEEE80211_IF_TYPE_AP: {
		/* Remove all virtual interfaces that use this BSS
		 * as their sdata->bss */
		struct ieee80211_sub_if_data *tsdata, *n;
		LIST_HEAD(tmp_list);

		write_lock_bh(&local->sub_if_lock);
		list_for_each_entry_safe(tsdata, n, &local->sub_if_list, list) {
			if (tsdata != sdata && tsdata->bss == &sdata->u.ap) {
				printk(KERN_DEBUG "%s: removing virtual "
				       "interface %s because its BSS interface"
				       " is being removed\n",
				       sdata->dev->name, tsdata->dev->name);
				list_move_tail(&tsdata->list, &tmp_list);
			}
		}
		write_unlock_bh(&local->sub_if_lock);

		list_for_each_entry_safe(tsdata, n, &tmp_list, list)
			__ieee80211_if_del(local, tsdata);

		kfree(sdata->u.ap.beacon_head);
		kfree(sdata->u.ap.beacon_tail);
		kfree(sdata->u.ap.generic_elem);

		if (dev != local->mdev) {
			struct sk_buff *skb;
			while ((skb = skb_dequeue(&sdata->u.ap.ps_bc_buf))) {
				local->total_ps_buffered--;
				dev_kfree_skb(skb);
			}
		}

		break;
	}
	case IEEE80211_IF_TYPE_WDS:
		sta = sta_info_get(local, sdata->u.wds.remote_addr);
		if (sta) {
			sta_info_put(sta);
			sta_info_free(sta, 0);
		} else {
#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
			printk(KERN_DEBUG "%s: Someone had deleted my STA "
			       "entry for the WDS link\n", dev->name);
#endif /* CONFIG_MAC80211_VERBOSE_DEBUG */
		}
		break;
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
	}

	/* remove all STAs that are bound to this virtual interface */
	sta_info_flush(local, dev);

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

	write_lock_bh(&local->sub_if_lock);
	list_for_each_entry_safe(sdata, n, &local->sub_if_list, list) {
		if ((sdata->type == id || id == -1) &&
		    strcmp(name, sdata->dev->name) == 0 &&
		    sdata->dev != local->mdev) {
			list_del(&sdata->list);
			write_unlock_bh(&local->sub_if_lock);
			__ieee80211_if_del(local, sdata);
			ieee80211_update_default_wep_only(local);
			return 0;
		}
	}
	write_unlock_bh(&local->sub_if_lock);
	return -ENODEV;
}

void ieee80211_if_free(struct net_device *dev)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	/* local->apdev must be NULL when freeing management interface */
	BUG_ON(dev == local->apdev);
	ieee80211_if_sdata_deinit(sdata);
	free_netdev(dev);
}
