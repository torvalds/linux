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

#include <linux/netdevice.h>
#include <linux/module.h>
#include <linux/if_arp.h>
#include <linux/ieee802154.h>

#include <net/rtnetlink.h>
#include <linux/nl802154.h>
#include <net/af_ieee802154.h>
#include <net/mac802154.h>
#include <net/ieee802154_netdev.h>
#include <net/cfg802154.h>

#include "ieee802154_i.h"
#include "driver-ops.h"

static int mac802154_wpan_update_llsec(struct net_device *dev)
{
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(dev);
	struct ieee802154_mlme_ops *ops = ieee802154_mlme_ops(dev);
	int rc = 0;

	if (ops->llsec) {
		struct ieee802154_llsec_params params;
		int changed = 0;

		params.pan_id = sdata->pan_id;
		changed |= IEEE802154_LLSEC_PARAM_PAN_ID;

		params.hwaddr = sdata->extended_addr;
		changed |= IEEE802154_LLSEC_PARAM_HWADDR;

		rc = ops->llsec->set_params(dev, &params, changed);
	}

	return rc;
}

static int
mac802154_wpan_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(dev);
	struct sockaddr_ieee802154 *sa =
		(struct sockaddr_ieee802154 *)&ifr->ifr_addr;
	int err = -ENOIOCTLCMD;

	spin_lock_bh(&sdata->mib_lock);

	switch (cmd) {
	case SIOCGIFADDR:
	{
		u16 pan_id, short_addr;

		pan_id = le16_to_cpu(sdata->pan_id);
		short_addr = le16_to_cpu(sdata->short_addr);
		if (pan_id == IEEE802154_PANID_BROADCAST ||
		    short_addr == IEEE802154_ADDR_BROADCAST) {
			err = -EADDRNOTAVAIL;
			break;
		}

		sa->family = AF_IEEE802154;
		sa->addr.addr_type = IEEE802154_ADDR_SHORT;
		sa->addr.pan_id = pan_id;
		sa->addr.short_addr = short_addr;

		err = 0;
		break;
	}
	case SIOCSIFADDR:
		dev_warn(&dev->dev,
			 "Using DEBUGing ioctl SIOCSIFADDR isn't recommended!\n");
		if (sa->family != AF_IEEE802154 ||
		    sa->addr.addr_type != IEEE802154_ADDR_SHORT ||
		    sa->addr.pan_id == IEEE802154_PANID_BROADCAST ||
		    sa->addr.short_addr == IEEE802154_ADDR_BROADCAST ||
		    sa->addr.short_addr == IEEE802154_ADDR_UNDEF) {
			err = -EINVAL;
			break;
		}

		sdata->pan_id = cpu_to_le16(sa->addr.pan_id);
		sdata->short_addr = cpu_to_le16(sa->addr.short_addr);

		err = mac802154_wpan_update_llsec(dev);
		break;
	}

	spin_unlock_bh(&sdata->mib_lock);
	return err;
}

static int mac802154_wpan_mac_addr(struct net_device *dev, void *p)
{
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(dev);
	struct sockaddr *addr = p;

	if (netif_running(dev))
		return -EBUSY;

	/* FIXME: validate addr */
	memcpy(dev->dev_addr, addr->sa_data, dev->addr_len);
	sdata->extended_addr = ieee802154_netdev_to_extended_addr(dev->dev_addr);

	return mac802154_wpan_update_llsec(dev);
}

int mac802154_set_mac_params(struct net_device *dev,
			     const struct ieee802154_mac_params *params)
{
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(dev);

	mutex_lock(&sdata->local->iflist_mtx);
	sdata->mac_params = *params;
	mutex_unlock(&sdata->local->iflist_mtx);

	return 0;
}

void mac802154_get_mac_params(struct net_device *dev,
			      struct ieee802154_mac_params *params)
{
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(dev);

	mutex_lock(&sdata->local->iflist_mtx);
	*params = sdata->mac_params;
	mutex_unlock(&sdata->local->iflist_mtx);
}

static int mac802154_slave_open(struct net_device *dev)
{
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(dev);
	struct ieee802154_sub_if_data *subif;
	struct ieee802154_local *local = sdata->local;
	int res = 0;

	ASSERT_RTNL();

	if (sdata->type == IEEE802154_DEV_WPAN) {
		mutex_lock(&sdata->local->iflist_mtx);
		list_for_each_entry(subif, &sdata->local->interfaces, list) {
			if (subif != sdata && subif->type == sdata->type &&
			    ieee802154_sdata_running(subif)) {
				mutex_unlock(&sdata->local->iflist_mtx);
				return -EBUSY;
			}
		}
		mutex_unlock(&sdata->local->iflist_mtx);
	}

	set_bit(SDATA_STATE_RUNNING, &sdata->state);

	if (!local->open_count) {
		res = drv_start(local);
		WARN_ON(res);
		if (res)
			goto err;
	}

	local->open_count++;
	netif_start_queue(dev);
	return 0;
err:
	/* might already be clear but that doesn't matter */
	clear_bit(SDATA_STATE_RUNNING, &sdata->state);

	return res;
}

static int mac802154_wpan_open(struct net_device *dev)
{
	int rc;
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(dev);
	struct ieee802154_local *local = sdata->local;
	struct wpan_phy *phy = sdata->local->phy;

	rc = mac802154_slave_open(dev);
	if (rc < 0)
		return rc;

	mutex_lock(&phy->pib_lock);

	if (local->hw.flags & IEEE802154_HW_PROMISCUOUS) {
		rc = drv_set_promiscuous_mode(local, sdata->promisuous_mode);
		if (rc < 0)
			goto out;
	}

	if (local->hw.flags & IEEE802154_HW_AFILT) {
		rc = drv_set_extended_addr(local, sdata->extended_addr);
		if (rc < 0)
			goto out;
	}

	if (local->hw.flags & IEEE802154_HW_TXPOWER) {
		rc = drv_set_tx_power(local, sdata->mac_params.transmit_power);
		if (rc < 0)
			goto out;
	}

	if (local->hw.flags & IEEE802154_HW_LBT) {
		rc = drv_set_lbt_mode(local, sdata->mac_params.lbt);
		if (rc < 0)
			goto out;
	}

	if (local->hw.flags & IEEE802154_HW_CCA_MODE) {
		rc = drv_set_cca_mode(local, sdata->mac_params.cca_mode);
		if (rc < 0)
			goto out;
	}

	if (local->hw.flags & IEEE802154_HW_CCA_ED_LEVEL) {
		rc = drv_set_cca_ed_level(local,
					  sdata->mac_params.cca_ed_level);
		if (rc < 0)
			goto out;
	}

	if (local->hw.flags & IEEE802154_HW_CSMA_PARAMS) {
		rc = drv_set_csma_params(local, sdata->mac_params.min_be,
					 sdata->mac_params.max_be,
					 sdata->mac_params.csma_retries);
		if (rc < 0)
			goto out;
	}

	if (local->hw.flags & IEEE802154_HW_FRAME_RETRIES) {
		rc = drv_set_max_frame_retries(local,
					       sdata->mac_params.frame_retries);
		if (rc < 0)
			goto out;
	}

	mutex_unlock(&phy->pib_lock);
	return 0;

out:
	mutex_unlock(&phy->pib_lock);
	return rc;
}

static int mac802154_slave_close(struct net_device *dev)
{
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(dev);
	struct ieee802154_local *local = sdata->local;

	ASSERT_RTNL();

	netif_stop_queue(dev);
	local->open_count--;

	clear_bit(SDATA_STATE_RUNNING, &sdata->state);

	if (!local->open_count)
		drv_stop(local);

	return 0;
}

static int mac802154_set_header_security(struct ieee802154_sub_if_data *sdata,
					 struct ieee802154_hdr *hdr,
					 const struct ieee802154_mac_cb *cb)
{
	struct ieee802154_llsec_params params;
	u8 level;

	mac802154_llsec_get_params(&sdata->sec, &params);

	if (!params.enabled && cb->secen_override && cb->secen)
		return -EINVAL;
	if (!params.enabled ||
	    (cb->secen_override && !cb->secen) ||
	    !params.out_level)
		return 0;
	if (cb->seclevel_override && !cb->seclevel)
		return -EINVAL;

	level = cb->seclevel_override ? cb->seclevel : params.out_level;

	hdr->fc.security_enabled = 1;
	hdr->sec.level = level;
	hdr->sec.key_id_mode = params.out_key.mode;
	if (params.out_key.mode == IEEE802154_SCF_KEY_SHORT_INDEX)
		hdr->sec.short_src = params.out_key.short_source;
	else if (params.out_key.mode == IEEE802154_SCF_KEY_HW_INDEX)
		hdr->sec.extended_src = params.out_key.extended_source;
	hdr->sec.key_id = params.out_key.id;

	return 0;
}

static int mac802154_header_create(struct sk_buff *skb,
				   struct net_device *dev,
				   unsigned short type,
				   const void *daddr,
				   const void *saddr,
				   unsigned len)
{
	struct ieee802154_hdr hdr;
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(dev);
	struct ieee802154_mac_cb *cb = mac_cb(skb);
	int hlen;

	if (!daddr)
		return -EINVAL;

	memset(&hdr.fc, 0, sizeof(hdr.fc));
	hdr.fc.type = cb->type;
	hdr.fc.security_enabled = cb->secen;
	hdr.fc.ack_request = cb->ackreq;
	hdr.seq = ieee802154_mlme_ops(dev)->get_dsn(dev);

	if (mac802154_set_header_security(sdata, &hdr, cb) < 0)
		return -EINVAL;

	if (!saddr) {
		spin_lock_bh(&sdata->mib_lock);

		if (sdata->short_addr == cpu_to_le16(IEEE802154_ADDR_BROADCAST) ||
		    sdata->short_addr == cpu_to_le16(IEEE802154_ADDR_UNDEF) ||
		    sdata->pan_id == cpu_to_le16(IEEE802154_PANID_BROADCAST)) {
			hdr.source.mode = IEEE802154_ADDR_LONG;
			hdr.source.extended_addr = sdata->extended_addr;
		} else {
			hdr.source.mode = IEEE802154_ADDR_SHORT;
			hdr.source.short_addr = sdata->short_addr;
		}

		hdr.source.pan_id = sdata->pan_id;

		spin_unlock_bh(&sdata->mib_lock);
	} else {
		hdr.source = *(const struct ieee802154_addr *)saddr;
	}

	hdr.dest = *(const struct ieee802154_addr *)daddr;

	hlen = ieee802154_hdr_push(skb, &hdr);
	if (hlen < 0)
		return -EINVAL;

	skb_reset_mac_header(skb);
	skb->mac_len = hlen;

	if (len > ieee802154_max_payload(&hdr))
		return -EMSGSIZE;

	return hlen;
}

static int
mac802154_header_parse(const struct sk_buff *skb, unsigned char *haddr)
{
	struct ieee802154_hdr hdr;
	struct ieee802154_addr *addr = (struct ieee802154_addr *)haddr;

	if (ieee802154_hdr_peek_addrs(skb, &hdr) < 0) {
		pr_debug("malformed packet\n");
		return 0;
	}

	*addr = hdr.source;
	return sizeof(*addr);
}

static struct header_ops mac802154_header_ops = {
	.create		= mac802154_header_create,
	.parse		= mac802154_header_parse,
};

static const struct net_device_ops mac802154_wpan_ops = {
	.ndo_open		= mac802154_wpan_open,
	.ndo_stop		= mac802154_slave_close,
	.ndo_start_xmit		= ieee802154_subif_start_xmit,
	.ndo_do_ioctl		= mac802154_wpan_ioctl,
	.ndo_set_mac_address	= mac802154_wpan_mac_addr,
};

static const struct net_device_ops mac802154_monitor_ops = {
	.ndo_open		= mac802154_wpan_open,
	.ndo_stop		= mac802154_slave_close,
	.ndo_start_xmit		= ieee802154_monitor_start_xmit,
};

static void mac802154_wpan_free(struct net_device *dev)
{
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(dev);

	mac802154_llsec_destroy(&sdata->sec);

	free_netdev(dev);
}

void mac802154_wpan_setup(struct net_device *dev)
{
	struct ieee802154_sub_if_data *sdata;

	dev->addr_len		= IEEE802154_ADDR_LEN;
	memset(dev->broadcast, 0xff, IEEE802154_ADDR_LEN);

	dev->hard_header_len	= MAC802154_FRAME_HARD_HEADER_LEN;
	dev->header_ops		= &mac802154_header_ops;
	dev->needed_tailroom	= 2 + 16; /* FCS + MIC */
	dev->mtu		= IEEE802154_MTU;
	dev->tx_queue_len	= 300;
	dev->type		= ARPHRD_IEEE802154;
	dev->flags		= IFF_NOARP | IFF_BROADCAST;

	dev->destructor		= mac802154_wpan_free;
	dev->netdev_ops		= &mac802154_wpan_ops;
	dev->ml_priv		= &mac802154_mlme_wpan;

	sdata = IEEE802154_DEV_TO_SUB_IF(dev);
	sdata->type = IEEE802154_DEV_WPAN;

	spin_lock_init(&sdata->mib_lock);
	mutex_init(&sdata->sec_mtx);

	get_random_bytes(&sdata->bsn, 1);
	get_random_bytes(&sdata->dsn, 1);

	/* defaults per 802.15.4-2011 */
	sdata->mac_params.min_be = 3;
	sdata->mac_params.max_be = 5;
	sdata->mac_params.csma_retries = 4;
	/* for compatibility, actual default is 3 */
	sdata->mac_params.frame_retries = -1;

	sdata->pan_id = cpu_to_le16(IEEE802154_PANID_BROADCAST);
	sdata->short_addr = cpu_to_le16(IEEE802154_ADDR_BROADCAST);

	sdata->promisuous_mode = false;

	mac802154_llsec_init(&sdata->sec);
}

void mac802154_monitor_setup(struct net_device *dev)
{
	struct ieee802154_sub_if_data *sdata;

	dev->needed_tailroom	= 2; /* room for FCS */
	dev->mtu		= IEEE802154_MTU;
	dev->tx_queue_len	= 10;
	dev->type		= ARPHRD_IEEE802154_MONITOR;
	dev->flags		= IFF_NOARP | IFF_BROADCAST;

	dev->destructor		= free_netdev;
	dev->netdev_ops		= &mac802154_monitor_ops;
	dev->ml_priv		= &mac802154_mlme_reduced;

	sdata = IEEE802154_DEV_TO_SUB_IF(dev);
	sdata->type = IEEE802154_DEV_MONITOR;

	sdata->promisuous_mode = true;
}
