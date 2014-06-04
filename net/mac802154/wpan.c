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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
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

#include <net/rtnetlink.h>
#include <linux/nl802154.h>
#include <net/af_ieee802154.h>
#include <net/mac802154.h>
#include <net/ieee802154_netdev.h>
#include <net/ieee802154.h>
#include <net/wpan-phy.h>

#include "mac802154.h"

static int
mac802154_wpan_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct mac802154_sub_if_data *priv = netdev_priv(dev);
	struct sockaddr_ieee802154 *sa =
		(struct sockaddr_ieee802154 *)&ifr->ifr_addr;
	int err = -ENOIOCTLCMD;

	spin_lock_bh(&priv->mib_lock);

	switch (cmd) {
	case SIOCGIFADDR:
	{
		u16 pan_id, short_addr;

		pan_id = le16_to_cpu(priv->pan_id);
		short_addr = le16_to_cpu(priv->short_addr);
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
			 "Using DEBUGing ioctl SIOCSIFADDR isn't recommened!\n");
		if (sa->family != AF_IEEE802154 ||
		    sa->addr.addr_type != IEEE802154_ADDR_SHORT ||
		    sa->addr.pan_id == IEEE802154_PANID_BROADCAST ||
		    sa->addr.short_addr == IEEE802154_ADDR_BROADCAST ||
		    sa->addr.short_addr == IEEE802154_ADDR_UNDEF) {
			err = -EINVAL;
			break;
		}

		priv->pan_id = cpu_to_le16(sa->addr.pan_id);
		priv->short_addr = cpu_to_le16(sa->addr.short_addr);

		err = 0;
		break;
	}

	spin_unlock_bh(&priv->mib_lock);
	return err;
}

static int mac802154_wpan_mac_addr(struct net_device *dev, void *p)
{
	struct sockaddr *addr = p;

	if (netif_running(dev))
		return -EBUSY;

	/* FIXME: validate addr */
	memcpy(dev->dev_addr, addr->sa_data, dev->addr_len);
	mac802154_dev_set_ieee_addr(dev);
	return 0;
}

int mac802154_set_mac_params(struct net_device *dev,
			     const struct ieee802154_mac_params *params)
{
	struct mac802154_sub_if_data *priv = netdev_priv(dev);

	mutex_lock(&priv->hw->slaves_mtx);
	priv->mac_params = *params;
	mutex_unlock(&priv->hw->slaves_mtx);

	return 0;
}

void mac802154_get_mac_params(struct net_device *dev,
			      struct ieee802154_mac_params *params)
{
	struct mac802154_sub_if_data *priv = netdev_priv(dev);

	mutex_lock(&priv->hw->slaves_mtx);
	*params = priv->mac_params;
	mutex_unlock(&priv->hw->slaves_mtx);
}

int mac802154_wpan_open(struct net_device *dev)
{
	int rc;
	struct mac802154_sub_if_data *priv = netdev_priv(dev);
	struct wpan_phy *phy = priv->hw->phy;

	rc = mac802154_slave_open(dev);
	if (rc < 0)
		return rc;

	mutex_lock(&phy->pib_lock);

	if (phy->set_txpower) {
		rc = phy->set_txpower(phy, priv->mac_params.transmit_power);
		if (rc < 0)
			goto out;
	}

	if (phy->set_lbt) {
		rc = phy->set_lbt(phy, priv->mac_params.lbt);
		if (rc < 0)
			goto out;
	}

	if (phy->set_cca_mode) {
		rc = phy->set_cca_mode(phy, priv->mac_params.cca_mode);
		if (rc < 0)
			goto out;
	}

	if (phy->set_cca_ed_level) {
		rc = phy->set_cca_ed_level(phy, priv->mac_params.cca_ed_level);
		if (rc < 0)
			goto out;
	}

	if (phy->set_csma_params) {
		rc = phy->set_csma_params(phy, priv->mac_params.min_be,
					  priv->mac_params.max_be,
					  priv->mac_params.csma_retries);
		if (rc < 0)
			goto out;
	}

	if (phy->set_frame_retries) {
		rc = phy->set_frame_retries(phy,
					    priv->mac_params.frame_retries);
		if (rc < 0)
			goto out;
	}

	mutex_unlock(&phy->pib_lock);
	return 0;

out:
	mutex_unlock(&phy->pib_lock);
	return rc;
}

static int mac802154_header_create(struct sk_buff *skb,
				   struct net_device *dev,
				   unsigned short type,
				   const void *daddr,
				   const void *saddr,
				   unsigned len)
{
	struct ieee802154_hdr hdr;
	struct mac802154_sub_if_data *priv = netdev_priv(dev);
	int hlen;

	if (!daddr)
		return -EINVAL;

	memset(&hdr.fc, 0, sizeof(hdr.fc));
	hdr.fc.type = mac_cb_type(skb);
	hdr.fc.security_enabled = mac_cb_is_secen(skb);
	hdr.fc.ack_request = mac_cb_is_ackreq(skb);

	if (!saddr) {
		spin_lock_bh(&priv->mib_lock);

		if (priv->short_addr == cpu_to_le16(IEEE802154_ADDR_BROADCAST) ||
		    priv->short_addr == cpu_to_le16(IEEE802154_ADDR_UNDEF) ||
		    priv->pan_id == cpu_to_le16(IEEE802154_PANID_BROADCAST)) {
			hdr.source.mode = IEEE802154_ADDR_LONG;
			hdr.source.extended_addr = priv->extended_addr;
		} else {
			hdr.source.mode = IEEE802154_ADDR_SHORT;
			hdr.source.short_addr = priv->short_addr;
		}

		hdr.source.pan_id = priv->pan_id;

		spin_unlock_bh(&priv->mib_lock);
	} else {
		hdr.source = *(const struct ieee802154_addr *)saddr;
	}

	hdr.dest = *(const struct ieee802154_addr *)daddr;

	hlen = ieee802154_hdr_push(skb, &hdr);
	if (hlen < 0)
		return -EINVAL;

	skb_reset_mac_header(skb);
	skb->mac_len = hlen;

	if (hlen + len + 2 > dev->mtu)
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

static netdev_tx_t
mac802154_wpan_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct mac802154_sub_if_data *priv;
	u8 chan, page;

	priv = netdev_priv(dev);

	spin_lock_bh(&priv->mib_lock);
	chan = priv->chan;
	page = priv->page;
	spin_unlock_bh(&priv->mib_lock);

	if (chan == MAC802154_CHAN_NONE ||
	    page >= WPAN_NUM_PAGES ||
	    chan >= WPAN_NUM_CHANNELS) {
		kfree_skb(skb);
		return NETDEV_TX_OK;
	}

	skb->skb_iif = dev->ifindex;
	dev->stats.tx_packets++;
	dev->stats.tx_bytes += skb->len;

	return mac802154_tx(priv->hw, skb, page, chan);
}

static struct header_ops mac802154_header_ops = {
	.create		= mac802154_header_create,
	.parse		= mac802154_header_parse,
};

static const struct net_device_ops mac802154_wpan_ops = {
	.ndo_open		= mac802154_wpan_open,
	.ndo_stop		= mac802154_slave_close,
	.ndo_start_xmit		= mac802154_wpan_xmit,
	.ndo_do_ioctl		= mac802154_wpan_ioctl,
	.ndo_set_mac_address	= mac802154_wpan_mac_addr,
};

void mac802154_wpan_setup(struct net_device *dev)
{
	struct mac802154_sub_if_data *priv;

	dev->addr_len		= IEEE802154_ADDR_LEN;
	memset(dev->broadcast, 0xff, IEEE802154_ADDR_LEN);

	dev->hard_header_len	= MAC802154_FRAME_HARD_HEADER_LEN;
	dev->header_ops		= &mac802154_header_ops;
	dev->needed_tailroom	= 2; /* FCS */
	dev->mtu		= IEEE802154_MTU;
	dev->tx_queue_len	= 300;
	dev->type		= ARPHRD_IEEE802154;
	dev->flags		= IFF_NOARP | IFF_BROADCAST;
	dev->watchdog_timeo	= 0;

	dev->destructor		= free_netdev;
	dev->netdev_ops		= &mac802154_wpan_ops;
	dev->ml_priv		= &mac802154_mlme_wpan;

	priv = netdev_priv(dev);
	priv->type = IEEE802154_DEV_WPAN;

	priv->chan = MAC802154_CHAN_NONE;
	priv->page = 0;

	spin_lock_init(&priv->mib_lock);

	get_random_bytes(&priv->bsn, 1);
	get_random_bytes(&priv->dsn, 1);

	/* defaults per 802.15.4-2011 */
	priv->mac_params.min_be = 3;
	priv->mac_params.max_be = 5;
	priv->mac_params.csma_retries = 4;
	priv->mac_params.frame_retries = -1; /* for compatibility, actual default is 3 */

	priv->pan_id = cpu_to_le16(IEEE802154_PANID_BROADCAST);
	priv->short_addr = cpu_to_le16(IEEE802154_ADDR_BROADCAST);
}

static int mac802154_process_data(struct net_device *dev, struct sk_buff *skb)
{
	return netif_rx_ni(skb);
}

static int
mac802154_subif_frame(struct mac802154_sub_if_data *sdata, struct sk_buff *skb)
{
	__le16 span, sshort;

	pr_debug("getting packet via slave interface %s\n", sdata->dev->name);

	spin_lock_bh(&sdata->mib_lock);

	span = sdata->pan_id;
	sshort = sdata->short_addr;

	switch (mac_cb(skb)->dest.mode) {
	case IEEE802154_ADDR_NONE:
		if (mac_cb(skb)->dest.mode != IEEE802154_ADDR_NONE)
			/* FIXME: check if we are PAN coordinator */
			skb->pkt_type = PACKET_OTHERHOST;
		else
			/* ACK comes with both addresses empty */
			skb->pkt_type = PACKET_HOST;
		break;
	case IEEE802154_ADDR_LONG:
		if (mac_cb(skb)->dest.pan_id != span &&
		    mac_cb(skb)->dest.pan_id != cpu_to_le16(IEEE802154_PANID_BROADCAST))
			skb->pkt_type = PACKET_OTHERHOST;
		else if (mac_cb(skb)->dest.extended_addr == sdata->extended_addr)
			skb->pkt_type = PACKET_HOST;
		else
			skb->pkt_type = PACKET_OTHERHOST;
		break;
	case IEEE802154_ADDR_SHORT:
		if (mac_cb(skb)->dest.pan_id != span &&
		    mac_cb(skb)->dest.pan_id != cpu_to_le16(IEEE802154_PANID_BROADCAST))
			skb->pkt_type = PACKET_OTHERHOST;
		else if (mac_cb(skb)->dest.short_addr == sshort)
			skb->pkt_type = PACKET_HOST;
		else if (mac_cb(skb)->dest.short_addr ==
			  cpu_to_le16(IEEE802154_ADDR_BROADCAST))
			skb->pkt_type = PACKET_BROADCAST;
		else
			skb->pkt_type = PACKET_OTHERHOST;
		break;
	default:
		break;
	}

	spin_unlock_bh(&sdata->mib_lock);

	skb->dev = sdata->dev;

	sdata->dev->stats.rx_packets++;
	sdata->dev->stats.rx_bytes += skb->len;

	switch (mac_cb_type(skb)) {
	case IEEE802154_FC_TYPE_DATA:
		return mac802154_process_data(sdata->dev, skb);
	default:
		pr_warn("ieee802154: bad frame received (type = %d)\n",
			mac_cb_type(skb));
		kfree_skb(skb);
		return NET_RX_DROP;
	}
}

static void mac802154_print_addr(const char *name,
				 const struct ieee802154_addr *addr)
{
	if (addr->mode == IEEE802154_ADDR_NONE)
		pr_debug("%s not present\n", name);

	pr_debug("%s PAN ID: %04x\n", name, le16_to_cpu(addr->pan_id));
	if (addr->mode == IEEE802154_ADDR_SHORT) {
		pr_debug("%s is short: %04x\n", name,
			 le16_to_cpu(addr->short_addr));
	} else {
		u64 hw = swab64((__force u64) addr->extended_addr);

		pr_debug("%s is hardware: %8phC\n", name, &hw);
	}
}

static int mac802154_parse_frame_start(struct sk_buff *skb)
{
	int hlen;
	struct ieee802154_hdr hdr;

	hlen = ieee802154_hdr_pull(skb, &hdr);
	if (hlen < 0)
		return -EINVAL;

	skb->mac_len = hlen;

	pr_debug("fc: %04x dsn: %02x\n", le16_to_cpup((__le16 *)&hdr.fc),
		 hdr.seq);

	mac_cb(skb)->flags = hdr.fc.type;

	if (hdr.fc.ack_request)
		mac_cb(skb)->flags |= MAC_CB_FLAG_ACKREQ;
	if (hdr.fc.security_enabled)
		mac_cb(skb)->flags |= MAC_CB_FLAG_SECEN;

	mac802154_print_addr("destination", &hdr.dest);
	mac802154_print_addr("source", &hdr.source);

	mac_cb(skb)->source = hdr.source;
	mac_cb(skb)->dest = hdr.dest;

	if (hdr.fc.security_enabled) {
		u64 key;

		pr_debug("seclevel %i\n", hdr.sec.level);

		switch (hdr.sec.key_id_mode) {
		case IEEE802154_SCF_KEY_IMPLICIT:
			pr_debug("implicit key\n");
			break;

		case IEEE802154_SCF_KEY_INDEX:
			pr_debug("key %02x\n", hdr.sec.key_id);
			break;

		case IEEE802154_SCF_KEY_SHORT_INDEX:
			pr_debug("key %04x:%04x %02x\n",
				 le32_to_cpu(hdr.sec.short_src) >> 16,
				 le32_to_cpu(hdr.sec.short_src) & 0xffff,
				 hdr.sec.key_id);
			break;

		case IEEE802154_SCF_KEY_HW_INDEX:
			key = swab64((__force u64) hdr.sec.extended_src);
			pr_debug("key source %8phC %02x\n", &key,
				 hdr.sec.key_id);
			break;
		}

		return -EINVAL;
	}

	return 0;
}

void mac802154_wpans_rx(struct mac802154_priv *priv, struct sk_buff *skb)
{
	int ret;
	struct sk_buff *sskb;
	struct mac802154_sub_if_data *sdata;

	ret = mac802154_parse_frame_start(skb);
	if (ret) {
		pr_debug("got invalid frame\n");
		return;
	}

	rcu_read_lock();
	list_for_each_entry_rcu(sdata, &priv->slaves, list) {
		if (sdata->type != IEEE802154_DEV_WPAN)
			continue;

		sskb = skb_clone(skb, GFP_ATOMIC);
		if (sskb)
			mac802154_subif_frame(sdata, sskb);
	}
	rcu_read_unlock();
}
