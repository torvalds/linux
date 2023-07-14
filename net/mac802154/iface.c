// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2007-2012 Siemens AG
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

#include <net/nl802154.h>
#include <net/mac802154.h>
#include <net/ieee802154_netdev.h>
#include <net/cfg802154.h>

#include "ieee802154_i.h"
#include "driver-ops.h"

int mac802154_wpan_update_llsec(struct net_device *dev)
{
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(dev);
	struct ieee802154_mlme_ops *ops = ieee802154_mlme_ops(dev);
	struct wpan_dev *wpan_dev = &sdata->wpan_dev;
	int rc = 0;

	if (ops->llsec) {
		struct ieee802154_llsec_params params;
		int changed = 0;

		params.pan_id = wpan_dev->pan_id;
		changed |= IEEE802154_LLSEC_PARAM_PAN_ID;

		params.hwaddr = wpan_dev->extended_addr;
		changed |= IEEE802154_LLSEC_PARAM_HWADDR;

		rc = ops->llsec->set_params(dev, &params, changed);
	}

	return rc;
}

static int
mac802154_wpan_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(dev);
	struct wpan_dev *wpan_dev = &sdata->wpan_dev;
	struct sockaddr_ieee802154 *sa =
		(struct sockaddr_ieee802154 *)&ifr->ifr_addr;
	int err = -ENOIOCTLCMD;

	if (cmd != SIOCGIFADDR && cmd != SIOCSIFADDR)
		return err;

	rtnl_lock();

	switch (cmd) {
	case SIOCGIFADDR:
	{
		u16 pan_id, short_addr;

		pan_id = le16_to_cpu(wpan_dev->pan_id);
		short_addr = le16_to_cpu(wpan_dev->short_addr);
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
		if (netif_running(dev)) {
			rtnl_unlock();
			return -EBUSY;
		}

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

		wpan_dev->pan_id = cpu_to_le16(sa->addr.pan_id);
		wpan_dev->short_addr = cpu_to_le16(sa->addr.short_addr);

		err = mac802154_wpan_update_llsec(dev);
		break;
	}

	rtnl_unlock();
	return err;
}

static int mac802154_wpan_mac_addr(struct net_device *dev, void *p)
{
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(dev);
	struct sockaddr *addr = p;
	__le64 extended_addr;

	if (netif_running(dev))
		return -EBUSY;

	/* lowpan need to be down for update
	 * SLAAC address after ifup
	 */
	if (sdata->wpan_dev.lowpan_dev) {
		if (netif_running(sdata->wpan_dev.lowpan_dev))
			return -EBUSY;
	}

	ieee802154_be64_to_le64(&extended_addr, addr->sa_data);
	if (!ieee802154_is_valid_extended_unicast_addr(extended_addr))
		return -EINVAL;

	dev_addr_set(dev, addr->sa_data);
	sdata->wpan_dev.extended_addr = extended_addr;

	/* update lowpan interface mac address when
	 * wpan mac has been changed
	 */
	if (sdata->wpan_dev.lowpan_dev)
		dev_addr_set(sdata->wpan_dev.lowpan_dev, dev->dev_addr);

	return mac802154_wpan_update_llsec(dev);
}

static int ieee802154_setup_hw(struct ieee802154_sub_if_data *sdata)
{
	struct ieee802154_local *local = sdata->local;
	struct wpan_dev *wpan_dev = &sdata->wpan_dev;
	int ret;

	sdata->required_filtering = sdata->iface_default_filtering;

	if (local->hw.flags & IEEE802154_HW_AFILT) {
		local->addr_filt.pan_id = wpan_dev->pan_id;
		local->addr_filt.ieee_addr = wpan_dev->extended_addr;
		local->addr_filt.short_addr = wpan_dev->short_addr;
	}

	if (local->hw.flags & IEEE802154_HW_LBT) {
		ret = drv_set_lbt_mode(local, wpan_dev->lbt);
		if (ret < 0)
			return ret;
	}

	if (local->hw.flags & IEEE802154_HW_CSMA_PARAMS) {
		ret = drv_set_csma_params(local, wpan_dev->min_be,
					  wpan_dev->max_be,
					  wpan_dev->csma_retries);
		if (ret < 0)
			return ret;
	}

	if (local->hw.flags & IEEE802154_HW_FRAME_RETRIES) {
		ret = drv_set_max_frame_retries(local, wpan_dev->frame_retries);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int mac802154_slave_open(struct net_device *dev)
{
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(dev);
	struct ieee802154_local *local = sdata->local;
	int res;

	ASSERT_RTNL();

	set_bit(SDATA_STATE_RUNNING, &sdata->state);

	if (!local->open_count) {
		res = ieee802154_setup_hw(sdata);
		if (res)
			goto err;

		res = drv_start(local, sdata->required_filtering,
				&local->addr_filt);
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

static int
ieee802154_check_mac_settings(struct ieee802154_local *local,
			      struct ieee802154_sub_if_data *sdata,
			      struct ieee802154_sub_if_data *nsdata)
{
	struct wpan_dev *nwpan_dev = &nsdata->wpan_dev;
	struct wpan_dev *wpan_dev = &sdata->wpan_dev;

	ASSERT_RTNL();

	if (sdata->iface_default_filtering != nsdata->iface_default_filtering)
		return -EBUSY;

	if (local->hw.flags & IEEE802154_HW_AFILT) {
		if (wpan_dev->pan_id != nwpan_dev->pan_id ||
		    wpan_dev->short_addr != nwpan_dev->short_addr ||
		    wpan_dev->extended_addr != nwpan_dev->extended_addr)
			return -EBUSY;
	}

	if (local->hw.flags & IEEE802154_HW_CSMA_PARAMS) {
		if (wpan_dev->min_be != nwpan_dev->min_be ||
		    wpan_dev->max_be != nwpan_dev->max_be ||
		    wpan_dev->csma_retries != nwpan_dev->csma_retries)
			return -EBUSY;
	}

	if (local->hw.flags & IEEE802154_HW_FRAME_RETRIES) {
		if (wpan_dev->frame_retries != nwpan_dev->frame_retries)
			return -EBUSY;
	}

	if (local->hw.flags & IEEE802154_HW_LBT) {
		if (wpan_dev->lbt != nwpan_dev->lbt)
			return -EBUSY;
	}

	return 0;
}

static int
ieee802154_check_concurrent_iface(struct ieee802154_sub_if_data *sdata,
				  enum nl802154_iftype iftype)
{
	struct ieee802154_local *local = sdata->local;
	struct ieee802154_sub_if_data *nsdata;

	/* we hold the RTNL here so can safely walk the list */
	list_for_each_entry(nsdata, &local->interfaces, list) {
		if (nsdata != sdata && ieee802154_sdata_running(nsdata)) {
			int ret;

			/* TODO currently we don't support multiple node/coord
			 * types we need to run skb_clone at rx path. Check if
			 * there exist really an use case if we need to support
			 * multiple node/coord types at the same time.
			 */
			if (sdata->wpan_dev.iftype != NL802154_IFTYPE_MONITOR &&
			    nsdata->wpan_dev.iftype != NL802154_IFTYPE_MONITOR)
				return -EBUSY;

			/* check all phy mac sublayer settings are the same.
			 * We have only one phy, different values makes trouble.
			 */
			ret = ieee802154_check_mac_settings(local, sdata, nsdata);
			if (ret < 0)
				return ret;
		}
	}

	return 0;
}

static int mac802154_wpan_open(struct net_device *dev)
{
	int rc;
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(dev);
	struct wpan_dev *wpan_dev = &sdata->wpan_dev;

	rc = ieee802154_check_concurrent_iface(sdata, wpan_dev->iftype);
	if (rc < 0)
		return rc;

	return mac802154_slave_open(dev);
}

static int mac802154_slave_close(struct net_device *dev)
{
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(dev);
	struct ieee802154_local *local = sdata->local;

	ASSERT_RTNL();

	if (mac802154_is_scanning(local))
		mac802154_abort_scan_locked(local, sdata);

	if (mac802154_is_beaconing(local))
		mac802154_stop_beacons_locked(local, sdata);

	netif_stop_queue(dev);
	local->open_count--;

	clear_bit(SDATA_STATE_RUNNING, &sdata->state);

	if (!local->open_count)
		ieee802154_stop_device(local);

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

static int ieee802154_header_create(struct sk_buff *skb,
				    struct net_device *dev,
				    const struct ieee802154_addr *daddr,
				    const struct ieee802154_addr *saddr,
				    unsigned len)
{
	struct ieee802154_hdr hdr;
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(dev);
	struct wpan_dev *wpan_dev = &sdata->wpan_dev;
	struct ieee802154_mac_cb *cb = mac_cb(skb);
	int hlen;

	if (!daddr)
		return -EINVAL;

	memset(&hdr.fc, 0, sizeof(hdr.fc));
	hdr.fc.type = cb->type;
	hdr.fc.security_enabled = cb->secen;
	hdr.fc.ack_request = cb->ackreq;
	hdr.seq = atomic_inc_return(&dev->ieee802154_ptr->dsn) & 0xFF;

	if (mac802154_set_header_security(sdata, &hdr, cb) < 0)
		return -EINVAL;

	if (!saddr) {
		if (wpan_dev->short_addr == cpu_to_le16(IEEE802154_ADDR_BROADCAST) ||
		    wpan_dev->short_addr == cpu_to_le16(IEEE802154_ADDR_UNDEF) ||
		    wpan_dev->pan_id == cpu_to_le16(IEEE802154_PANID_BROADCAST)) {
			hdr.source.mode = IEEE802154_ADDR_LONG;
			hdr.source.extended_addr = wpan_dev->extended_addr;
		} else {
			hdr.source.mode = IEEE802154_ADDR_SHORT;
			hdr.source.short_addr = wpan_dev->short_addr;
		}

		hdr.source.pan_id = wpan_dev->pan_id;
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

static const struct wpan_dev_header_ops ieee802154_header_ops = {
	.create		= ieee802154_header_create,
};

/* This header create functionality assumes a 8 byte array for
 * source and destination pointer at maximum. To adapt this for
 * the 802.15.4 dataframe header we use extended address handling
 * here only and intra pan connection. fc fields are mostly fallback
 * handling. For provide dev_hard_header for dgram sockets.
 */
static int mac802154_header_create(struct sk_buff *skb,
				   struct net_device *dev,
				   unsigned short type,
				   const void *daddr,
				   const void *saddr,
				   unsigned len)
{
	struct ieee802154_hdr hdr;
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(dev);
	struct wpan_dev *wpan_dev = &sdata->wpan_dev;
	struct ieee802154_mac_cb cb = { };
	int hlen;

	if (!daddr)
		return -EINVAL;

	memset(&hdr.fc, 0, sizeof(hdr.fc));
	hdr.fc.type = IEEE802154_FC_TYPE_DATA;
	hdr.fc.ack_request = wpan_dev->ackreq;
	hdr.seq = atomic_inc_return(&dev->ieee802154_ptr->dsn) & 0xFF;

	/* TODO currently a workaround to give zero cb block to set
	 * security parameters defaults according MIB.
	 */
	if (mac802154_set_header_security(sdata, &hdr, &cb) < 0)
		return -EINVAL;

	hdr.dest.pan_id = wpan_dev->pan_id;
	hdr.dest.mode = IEEE802154_ADDR_LONG;
	ieee802154_be64_to_le64(&hdr.dest.extended_addr, daddr);

	hdr.source.pan_id = hdr.dest.pan_id;
	hdr.source.mode = IEEE802154_ADDR_LONG;

	if (!saddr)
		hdr.source.extended_addr = wpan_dev->extended_addr;
	else
		ieee802154_be64_to_le64(&hdr.source.extended_addr, saddr);

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

	if (ieee802154_hdr_peek_addrs(skb, &hdr) < 0) {
		pr_debug("malformed packet\n");
		return 0;
	}

	if (hdr.source.mode == IEEE802154_ADDR_LONG) {
		ieee802154_le64_to_be64(haddr, &hdr.source.extended_addr);
		return IEEE802154_EXTENDED_ADDR_LEN;
	}

	return 0;
}

static const struct header_ops mac802154_header_ops = {
	.create         = mac802154_header_create,
	.parse          = mac802154_header_parse,
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
}

static void ieee802154_if_setup(struct net_device *dev)
{
	dev->addr_len		= IEEE802154_EXTENDED_ADDR_LEN;
	memset(dev->broadcast, 0xff, IEEE802154_EXTENDED_ADDR_LEN);

	/* Let hard_header_len set to IEEE802154_MIN_HEADER_LEN. AF_PACKET
	 * will not send frames without any payload, but ack frames
	 * has no payload, so substract one that we can send a 3 bytes
	 * frame. The xmit callback assumes at least a hard header where two
	 * bytes fc and sequence field are set.
	 */
	dev->hard_header_len	= IEEE802154_MIN_HEADER_LEN - 1;
	/* The auth_tag header is for security and places in private payload
	 * room of mac frame which stucks between payload and FCS field.
	 */
	dev->needed_tailroom	= IEEE802154_MAX_AUTH_TAG_LEN +
				  IEEE802154_FCS_LEN;
	/* The mtu size is the payload without mac header in this case.
	 * We have a dynamic length header with a minimum header length
	 * which is hard_header_len. In this case we let mtu to the size
	 * of maximum payload which is IEEE802154_MTU - IEEE802154_FCS_LEN -
	 * hard_header_len. The FCS which is set by hardware or ndo_start_xmit
	 * and the minimum mac header which can be evaluated inside driver
	 * layer. The rest of mac header will be part of payload if greater
	 * than hard_header_len.
	 */
	dev->mtu		= IEEE802154_MTU - IEEE802154_FCS_LEN -
				  dev->hard_header_len;
	dev->tx_queue_len	= 300;
	dev->flags		= IFF_NOARP | IFF_BROADCAST;
}

static int
ieee802154_setup_sdata(struct ieee802154_sub_if_data *sdata,
		       enum nl802154_iftype type)
{
	struct wpan_dev *wpan_dev = &sdata->wpan_dev;
	int ret;
	u8 tmp;

	/* set some type-dependent values */
	sdata->wpan_dev.iftype = type;

	get_random_bytes(&tmp, sizeof(tmp));
	atomic_set(&wpan_dev->bsn, tmp);
	get_random_bytes(&tmp, sizeof(tmp));
	atomic_set(&wpan_dev->dsn, tmp);

	/* defaults per 802.15.4-2011 */
	wpan_dev->min_be = 3;
	wpan_dev->max_be = 5;
	wpan_dev->csma_retries = 4;
	wpan_dev->frame_retries = 3;

	wpan_dev->pan_id = cpu_to_le16(IEEE802154_PANID_BROADCAST);
	wpan_dev->short_addr = cpu_to_le16(IEEE802154_ADDR_BROADCAST);

	switch (type) {
	case NL802154_IFTYPE_COORD:
	case NL802154_IFTYPE_NODE:
		ieee802154_be64_to_le64(&wpan_dev->extended_addr,
					sdata->dev->dev_addr);

		sdata->dev->header_ops = &mac802154_header_ops;
		sdata->dev->needs_free_netdev = true;
		sdata->dev->priv_destructor = mac802154_wpan_free;
		sdata->dev->netdev_ops = &mac802154_wpan_ops;
		sdata->dev->ml_priv = &mac802154_mlme_wpan;
		sdata->iface_default_filtering = IEEE802154_FILTERING_4_FRAME_FIELDS;
		wpan_dev->header_ops = &ieee802154_header_ops;

		mutex_init(&sdata->sec_mtx);

		mac802154_llsec_init(&sdata->sec);
		ret = mac802154_wpan_update_llsec(sdata->dev);
		if (ret < 0)
			return ret;

		break;
	case NL802154_IFTYPE_MONITOR:
		sdata->dev->needs_free_netdev = true;
		sdata->dev->netdev_ops = &mac802154_monitor_ops;
		sdata->iface_default_filtering = IEEE802154_FILTERING_NONE;
		break;
	default:
		BUG();
	}

	return 0;
}

struct net_device *
ieee802154_if_add(struct ieee802154_local *local, const char *name,
		  unsigned char name_assign_type, enum nl802154_iftype type,
		  __le64 extended_addr)
{
	u8 addr[IEEE802154_EXTENDED_ADDR_LEN];
	struct net_device *ndev = NULL;
	struct ieee802154_sub_if_data *sdata = NULL;
	int ret;

	ASSERT_RTNL();

	ndev = alloc_netdev(sizeof(*sdata), name,
			    name_assign_type, ieee802154_if_setup);
	if (!ndev)
		return ERR_PTR(-ENOMEM);

	ndev->needed_headroom = local->hw.extra_tx_headroom +
				IEEE802154_MAX_HEADER_LEN;

	ret = dev_alloc_name(ndev, ndev->name);
	if (ret < 0)
		goto err;

	ieee802154_le64_to_be64(ndev->perm_addr,
				&local->hw.phy->perm_extended_addr);
	switch (type) {
	case NL802154_IFTYPE_COORD:
	case NL802154_IFTYPE_NODE:
		ndev->type = ARPHRD_IEEE802154;
		if (ieee802154_is_valid_extended_unicast_addr(extended_addr)) {
			ieee802154_le64_to_be64(addr, &extended_addr);
			dev_addr_set(ndev, addr);
		} else {
			dev_addr_set(ndev, ndev->perm_addr);
		}
		break;
	case NL802154_IFTYPE_MONITOR:
		ndev->type = ARPHRD_IEEE802154_MONITOR;
		break;
	default:
		ret = -EINVAL;
		goto err;
	}

	/* TODO check this */
	SET_NETDEV_DEV(ndev, &local->phy->dev);
	dev_net_set(ndev, wpan_phy_net(local->hw.phy));
	sdata = netdev_priv(ndev);
	ndev->ieee802154_ptr = &sdata->wpan_dev;
	memcpy(sdata->name, ndev->name, IFNAMSIZ);
	sdata->dev = ndev;
	sdata->wpan_dev.wpan_phy = local->hw.phy;
	sdata->local = local;
	INIT_LIST_HEAD(&sdata->wpan_dev.list);

	/* setup type-dependent data */
	ret = ieee802154_setup_sdata(sdata, type);
	if (ret)
		goto err;

	ret = register_netdevice(ndev);
	if (ret < 0)
		goto err;

	mutex_lock(&local->iflist_mtx);
	list_add_tail_rcu(&sdata->list, &local->interfaces);
	mutex_unlock(&local->iflist_mtx);

	return ndev;

err:
	free_netdev(ndev);
	return ERR_PTR(ret);
}

void ieee802154_if_remove(struct ieee802154_sub_if_data *sdata)
{
	ASSERT_RTNL();

	mutex_lock(&sdata->local->iflist_mtx);
	list_del_rcu(&sdata->list);
	mutex_unlock(&sdata->local->iflist_mtx);

	synchronize_rcu();
	unregister_netdevice(sdata->dev);
}

void ieee802154_remove_interfaces(struct ieee802154_local *local)
{
	struct ieee802154_sub_if_data *sdata, *tmp;

	mutex_lock(&local->iflist_mtx);
	list_for_each_entry_safe(sdata, tmp, &local->interfaces, list) {
		list_del(&sdata->list);

		unregister_netdevice(sdata->dev);
	}
	mutex_unlock(&local->iflist_mtx);
}

static int netdev_notify(struct notifier_block *nb,
			 unsigned long state, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct ieee802154_sub_if_data *sdata;

	if (state != NETDEV_CHANGENAME)
		return NOTIFY_DONE;

	if (!dev->ieee802154_ptr || !dev->ieee802154_ptr->wpan_phy)
		return NOTIFY_DONE;

	if (dev->ieee802154_ptr->wpan_phy->privid != mac802154_wpan_phy_privid)
		return NOTIFY_DONE;

	sdata = IEEE802154_DEV_TO_SUB_IF(dev);
	memcpy(sdata->name, dev->name, IFNAMSIZ);

	return NOTIFY_OK;
}

static struct notifier_block mac802154_netdev_notifier = {
	.notifier_call = netdev_notify,
};

int ieee802154_iface_init(void)
{
	return register_netdevice_notifier(&mac802154_netdev_notifier);
}

void ieee802154_iface_exit(void)
{
	unregister_netdevice_notifier(&mac802154_netdev_notifier);
}
