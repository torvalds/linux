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

void mac802154_dev_set_page_channel(struct net_device *dev, u8 page, u8 chan)
{
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(dev);
	struct ieee802154_local *local = sdata->local;
	int res;

	ASSERT_RTNL();

	BUG_ON(dev->type != ARPHRD_IEEE802154);

	res = drv_set_channel(local, page, chan);
	if (res) {
		pr_debug("set_channel failed\n");
	} else {
		local->phy->current_channel = chan;
		local->phy->current_page = page;
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
