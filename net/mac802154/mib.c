/*
 * Copyright 2007-2012 Siemens AG
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
 * Dmitry Eremin-Solenikov <dbaryshkov@gmail.com>
 * Sergey Lapin <slapin@ossfans.org>
 * Maxim Gorbachyov <maxim.gorbachev@siemens.com>
 * Alexander Smirnov <alex.bluesman.smirnov@gmail.com>
 */

#include <linux/if_arp.h>

#include <net/mac802154.h>
#include <net/ieee802154_netdev.h>
#include <net/cfg802154.h>

#include "ieee802154_i.h"
#include "driver-ops.h"

struct hw_addr_filt_notify_work {
	struct work_struct work;
	struct net_device *dev;
	unsigned long changed;
};

static struct ieee802154_local *mac802154_slave_get_priv(struct net_device *dev)
{
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(dev);

	BUG_ON(dev->type != ARPHRD_IEEE802154);

	return sdata->local;
}

static void hw_addr_notify(struct work_struct *work)
{
	struct hw_addr_filt_notify_work *nw = container_of(work,
			struct hw_addr_filt_notify_work, work);
	struct ieee802154_local *local = mac802154_slave_get_priv(nw->dev);
	int res;

	res = local->ops->set_hw_addr_filt(&local->hw, &local->hw.hw_filt,
					   nw->changed);
	if (res)
		pr_debug("failed changed mask %lx\n", nw->changed);

	kfree(nw);
}

static void set_hw_addr_filt(struct net_device *dev, unsigned long changed)
{
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(dev);
	struct hw_addr_filt_notify_work *work;

	work = kzalloc(sizeof(*work), GFP_ATOMIC);
	if (!work)
		return;

	INIT_WORK(&work->work, hw_addr_notify);
	work->dev = dev;
	work->changed = changed;
	queue_work(sdata->local->workqueue, &work->work);
}

void mac802154_dev_set_short_addr(struct net_device *dev, __le16 val)
{
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(dev);

	BUG_ON(dev->type != ARPHRD_IEEE802154);

	spin_lock_bh(&sdata->mib_lock);
	sdata->short_addr = val;
	spin_unlock_bh(&sdata->mib_lock);

	if ((sdata->local->ops->set_hw_addr_filt) &&
	    (sdata->local->hw.hw_filt.short_addr != sdata->short_addr)) {
		sdata->local->hw.hw_filt.short_addr = sdata->short_addr;
		set_hw_addr_filt(dev, IEEE802154_AFILT_SADDR_CHANGED);
	}
}

__le16 mac802154_dev_get_short_addr(const struct net_device *dev)
{
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(dev);
	__le16 ret;

	BUG_ON(dev->type != ARPHRD_IEEE802154);

	spin_lock_bh(&sdata->mib_lock);
	ret = sdata->short_addr;
	spin_unlock_bh(&sdata->mib_lock);

	return ret;
}

void mac802154_dev_set_ieee_addr(struct net_device *dev)
{
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(dev);
	struct ieee802154_local *local = sdata->local;

	sdata->extended_addr = ieee802154_devaddr_from_raw(dev->dev_addr);

	if (local->ops->set_hw_addr_filt &&
	    local->hw.hw_filt.ieee_addr != sdata->extended_addr) {
		local->hw.hw_filt.ieee_addr = sdata->extended_addr;
		set_hw_addr_filt(dev, IEEE802154_AFILT_IEEEADDR_CHANGED);
	}
}

__le16 mac802154_dev_get_pan_id(const struct net_device *dev)
{
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(dev);
	__le16 ret;

	BUG_ON(dev->type != ARPHRD_IEEE802154);

	spin_lock_bh(&sdata->mib_lock);
	ret = sdata->pan_id;
	spin_unlock_bh(&sdata->mib_lock);

	return ret;
}

void mac802154_dev_set_pan_id(struct net_device *dev, __le16 val)
{
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(dev);

	BUG_ON(dev->type != ARPHRD_IEEE802154);

	spin_lock_bh(&sdata->mib_lock);
	sdata->pan_id = val;
	spin_unlock_bh(&sdata->mib_lock);

	if ((sdata->local->ops->set_hw_addr_filt) &&
	    (sdata->local->hw.hw_filt.pan_id != sdata->pan_id)) {
		sdata->local->hw.hw_filt.pan_id = sdata->pan_id;
		set_hw_addr_filt(dev, IEEE802154_AFILT_PANID_CHANGED);
	}
}

u8 mac802154_dev_get_dsn(const struct net_device *dev)
{
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(dev);

	BUG_ON(dev->type != ARPHRD_IEEE802154);

	return sdata->dsn++;
}

void mac802154_dev_set_page_channel(struct net_device *dev, u8 page, u8 chan)
{
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(dev);
	struct ieee802154_local *local = sdata->local;
	int res;

	BUG_ON(dev->type != ARPHRD_IEEE802154);

	res = drv_set_channel(local, page, chan);
	if (res) {
		pr_debug("set_channel failed\n");
	} else {
		mutex_lock(&local->phy->pib_lock);
		local->phy->current_channel = chan;
		local->phy->current_page = page;
		mutex_unlock(&local->phy->pib_lock);
	}
}


int mac802154_get_params(struct net_device *dev,
			 struct ieee802154_llsec_params *params)
{
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(dev);
	int res;

	BUG_ON(dev->type != ARPHRD_IEEE802154);

	mutex_lock(&sdata->sec_mtx);
	res = mac802154_llsec_get_params(&sdata->sec, params);
	mutex_unlock(&sdata->sec_mtx);

	return res;
}

int mac802154_set_params(struct net_device *dev,
			 const struct ieee802154_llsec_params *params,
			 int changed)
{
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(dev);
	int res;

	BUG_ON(dev->type != ARPHRD_IEEE802154);

	mutex_lock(&sdata->sec_mtx);
	res = mac802154_llsec_set_params(&sdata->sec, params, changed);
	mutex_unlock(&sdata->sec_mtx);

	return res;
}


int mac802154_add_key(struct net_device *dev,
		      const struct ieee802154_llsec_key_id *id,
		      const struct ieee802154_llsec_key *key)
{
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(dev);
	int res;

	BUG_ON(dev->type != ARPHRD_IEEE802154);

	mutex_lock(&sdata->sec_mtx);
	res = mac802154_llsec_key_add(&sdata->sec, id, key);
	mutex_unlock(&sdata->sec_mtx);

	return res;
}

int mac802154_del_key(struct net_device *dev,
		      const struct ieee802154_llsec_key_id *id)
{
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(dev);
	int res;

	BUG_ON(dev->type != ARPHRD_IEEE802154);

	mutex_lock(&sdata->sec_mtx);
	res = mac802154_llsec_key_del(&sdata->sec, id);
	mutex_unlock(&sdata->sec_mtx);

	return res;
}


int mac802154_add_dev(struct net_device *dev,
		      const struct ieee802154_llsec_device *llsec_dev)
{
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(dev);
	int res;

	BUG_ON(dev->type != ARPHRD_IEEE802154);

	mutex_lock(&sdata->sec_mtx);
	res = mac802154_llsec_dev_add(&sdata->sec, llsec_dev);
	mutex_unlock(&sdata->sec_mtx);

	return res;
}

int mac802154_del_dev(struct net_device *dev, __le64 dev_addr)
{
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(dev);
	int res;

	BUG_ON(dev->type != ARPHRD_IEEE802154);

	mutex_lock(&sdata->sec_mtx);
	res = mac802154_llsec_dev_del(&sdata->sec, dev_addr);
	mutex_unlock(&sdata->sec_mtx);

	return res;
}


int mac802154_add_devkey(struct net_device *dev,
			 __le64 device_addr,
			 const struct ieee802154_llsec_device_key *key)
{
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(dev);
	int res;

	BUG_ON(dev->type != ARPHRD_IEEE802154);

	mutex_lock(&sdata->sec_mtx);
	res = mac802154_llsec_devkey_add(&sdata->sec, device_addr, key);
	mutex_unlock(&sdata->sec_mtx);

	return res;
}

int mac802154_del_devkey(struct net_device *dev,
			 __le64 device_addr,
			 const struct ieee802154_llsec_device_key *key)
{
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(dev);
	int res;

	BUG_ON(dev->type != ARPHRD_IEEE802154);

	mutex_lock(&sdata->sec_mtx);
	res = mac802154_llsec_devkey_del(&sdata->sec, device_addr, key);
	mutex_unlock(&sdata->sec_mtx);

	return res;
}


int mac802154_add_seclevel(struct net_device *dev,
			   const struct ieee802154_llsec_seclevel *sl)
{
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(dev);
	int res;

	BUG_ON(dev->type != ARPHRD_IEEE802154);

	mutex_lock(&sdata->sec_mtx);
	res = mac802154_llsec_seclevel_add(&sdata->sec, sl);
	mutex_unlock(&sdata->sec_mtx);

	return res;
}

int mac802154_del_seclevel(struct net_device *dev,
			   const struct ieee802154_llsec_seclevel *sl)
{
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(dev);
	int res;

	BUG_ON(dev->type != ARPHRD_IEEE802154);

	mutex_lock(&sdata->sec_mtx);
	res = mac802154_llsec_seclevel_del(&sdata->sec, sl);
	mutex_unlock(&sdata->sec_mtx);

	return res;
}


void mac802154_lock_table(struct net_device *dev)
{
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(dev);

	BUG_ON(dev->type != ARPHRD_IEEE802154);

	mutex_lock(&sdata->sec_mtx);
}

void mac802154_get_table(struct net_device *dev,
			 struct ieee802154_llsec_table **t)
{
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(dev);

	BUG_ON(dev->type != ARPHRD_IEEE802154);

	*t = &sdata->sec.table;
}

void mac802154_unlock_table(struct net_device *dev)
{
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(dev);

	BUG_ON(dev->type != ARPHRD_IEEE802154);

	mutex_unlock(&sdata->sec_mtx);
}
