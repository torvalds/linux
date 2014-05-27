/*
   Copyright (c) 2013 Intel Corp.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2 and
   only version 2 as published by the Free Software Foundation.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
*/

#include <linux/if_arp.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>

#include <net/ipv6.h>
#include <net/ip6_route.h>
#include <net/addrconf.h>

#include <net/af_ieee802154.h> /* to get the address type */

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include <net/bluetooth/l2cap.h>

#include "6lowpan.h"

#include "../ieee802154/6lowpan.h" /* for the compression support */

#define IFACE_NAME_TEMPLATE "bt%d"
#define EUI64_ADDR_LEN 8

struct skb_cb {
	struct in6_addr addr;
	struct l2cap_conn *conn;
};
#define lowpan_cb(skb) ((struct skb_cb *)((skb)->cb))

/* The devices list contains those devices that we are acting
 * as a proxy. The BT 6LoWPAN device is a virtual device that
 * connects to the Bluetooth LE device. The real connection to
 * BT device is done via l2cap layer. There exists one
 * virtual device / one BT 6LoWPAN network (=hciX device).
 * The list contains struct lowpan_dev elements.
 */
static LIST_HEAD(bt_6lowpan_devices);
static DEFINE_RWLOCK(devices_lock);

struct lowpan_peer {
	struct list_head list;
	struct l2cap_conn *conn;

	/* peer addresses in various formats */
	unsigned char eui64_addr[EUI64_ADDR_LEN];
	struct in6_addr peer_addr;
};

struct lowpan_dev {
	struct list_head list;

	struct hci_dev *hdev;
	struct net_device *netdev;
	struct list_head peers;
	atomic_t peer_count; /* number of items in peers list */

	struct work_struct delete_netdev;
	struct delayed_work notify_peers;
};

static inline struct lowpan_dev *lowpan_dev(const struct net_device *netdev)
{
	return netdev_priv(netdev);
}

static inline void peer_add(struct lowpan_dev *dev, struct lowpan_peer *peer)
{
	list_add(&peer->list, &dev->peers);
	atomic_inc(&dev->peer_count);
}

static inline bool peer_del(struct lowpan_dev *dev, struct lowpan_peer *peer)
{
	list_del(&peer->list);

	if (atomic_dec_and_test(&dev->peer_count)) {
		BT_DBG("last peer");
		return true;
	}

	return false;
}

static inline struct lowpan_peer *peer_lookup_ba(struct lowpan_dev *dev,
						 bdaddr_t *ba, __u8 type)
{
	struct lowpan_peer *peer, *tmp;

	BT_DBG("peers %d addr %pMR type %d", atomic_read(&dev->peer_count),
	       ba, type);

	list_for_each_entry_safe(peer, tmp, &dev->peers, list) {
		BT_DBG("addr %pMR type %d",
		       &peer->conn->hcon->dst, peer->conn->hcon->dst_type);

		if (bacmp(&peer->conn->hcon->dst, ba))
			continue;

		if (type == peer->conn->hcon->dst_type)
			return peer;
	}

	return NULL;
}

static inline struct lowpan_peer *peer_lookup_conn(struct lowpan_dev *dev,
						   struct l2cap_conn *conn)
{
	struct lowpan_peer *peer, *tmp;

	list_for_each_entry_safe(peer, tmp, &dev->peers, list) {
		if (peer->conn == conn)
			return peer;
	}

	return NULL;
}

static struct lowpan_peer *lookup_peer(struct l2cap_conn *conn)
{
	struct lowpan_dev *entry, *tmp;
	struct lowpan_peer *peer = NULL;
	unsigned long flags;

	read_lock_irqsave(&devices_lock, flags);

	list_for_each_entry_safe(entry, tmp, &bt_6lowpan_devices, list) {
		peer = peer_lookup_conn(entry, conn);
		if (peer)
			break;
	}

	read_unlock_irqrestore(&devices_lock, flags);

	return peer;
}

static struct lowpan_dev *lookup_dev(struct l2cap_conn *conn)
{
	struct lowpan_dev *entry, *tmp;
	struct lowpan_dev *dev = NULL;
	unsigned long flags;

	read_lock_irqsave(&devices_lock, flags);

	list_for_each_entry_safe(entry, tmp, &bt_6lowpan_devices, list) {
		if (conn->hcon->hdev == entry->hdev) {
			dev = entry;
			break;
		}
	}

	read_unlock_irqrestore(&devices_lock, flags);

	return dev;
}

static int give_skb_to_upper(struct sk_buff *skb, struct net_device *dev)
{
	struct sk_buff *skb_cp;
	int ret;

	skb_cp = skb_copy(skb, GFP_ATOMIC);
	if (!skb_cp)
		return -ENOMEM;

	ret = netif_rx(skb_cp);

	BT_DBG("receive skb %d", ret);
	if (ret < 0)
		return NET_RX_DROP;

	return ret;
}

static int process_data(struct sk_buff *skb, struct net_device *netdev,
			struct l2cap_conn *conn)
{
	const u8 *saddr, *daddr;
	u8 iphc0, iphc1;
	struct lowpan_dev *dev;
	struct lowpan_peer *peer;
	unsigned long flags;

	dev = lowpan_dev(netdev);

	read_lock_irqsave(&devices_lock, flags);
	peer = peer_lookup_conn(dev, conn);
	read_unlock_irqrestore(&devices_lock, flags);
	if (!peer)
		goto drop;

	saddr = peer->eui64_addr;
	daddr = dev->netdev->dev_addr;

	/* at least two bytes will be used for the encoding */
	if (skb->len < 2)
		goto drop;

	if (lowpan_fetch_skb_u8(skb, &iphc0))
		goto drop;

	if (lowpan_fetch_skb_u8(skb, &iphc1))
		goto drop;

	return lowpan_process_data(skb, netdev,
				   saddr, IEEE802154_ADDR_LONG, EUI64_ADDR_LEN,
				   daddr, IEEE802154_ADDR_LONG, EUI64_ADDR_LEN,
				   iphc0, iphc1, give_skb_to_upper);

drop:
	kfree_skb(skb);
	return -EINVAL;
}

static int recv_pkt(struct sk_buff *skb, struct net_device *dev,
		    struct l2cap_conn *conn)
{
	struct sk_buff *local_skb;
	int ret;

	if (!netif_running(dev))
		goto drop;

	if (dev->type != ARPHRD_6LOWPAN)
		goto drop;

	/* check that it's our buffer */
	if (skb->data[0] == LOWPAN_DISPATCH_IPV6) {
		/* Copy the packet so that the IPv6 header is
		 * properly aligned.
		 */
		local_skb = skb_copy_expand(skb, NET_SKB_PAD - 1,
					    skb_tailroom(skb), GFP_ATOMIC);
		if (!local_skb)
			goto drop;

		local_skb->protocol = htons(ETH_P_IPV6);
		local_skb->pkt_type = PACKET_HOST;

		skb_reset_network_header(local_skb);
		skb_set_transport_header(local_skb, sizeof(struct ipv6hdr));

		if (give_skb_to_upper(local_skb, dev) != NET_RX_SUCCESS) {
			kfree_skb(local_skb);
			goto drop;
		}

		dev->stats.rx_bytes += skb->len;
		dev->stats.rx_packets++;

		kfree_skb(local_skb);
		kfree_skb(skb);
	} else {
		switch (skb->data[0] & 0xe0) {
		case LOWPAN_DISPATCH_IPHC:	/* ipv6 datagram */
			local_skb = skb_clone(skb, GFP_ATOMIC);
			if (!local_skb)
				goto drop;

			ret = process_data(local_skb, dev, conn);
			if (ret != NET_RX_SUCCESS)
				goto drop;

			dev->stats.rx_bytes += skb->len;
			dev->stats.rx_packets++;

			kfree_skb(skb);
			break;
		default:
			break;
		}
	}

	return NET_RX_SUCCESS;

drop:
	kfree_skb(skb);
	return NET_RX_DROP;
}

/* Packet from BT LE device */
int bt_6lowpan_recv(struct l2cap_conn *conn, struct sk_buff *skb)
{
	struct lowpan_dev *dev;
	struct lowpan_peer *peer;
	int err;

	peer = lookup_peer(conn);
	if (!peer)
		return -ENOENT;

	dev = lookup_dev(conn);
	if (!dev || !dev->netdev)
		return -ENOENT;

	err = recv_pkt(skb, dev->netdev, conn);
	BT_DBG("recv pkt %d", err);

	return err;
}

static inline int skbuff_copy(void *msg, int len, int count, int mtu,
			      struct sk_buff *skb, struct net_device *dev)
{
	struct sk_buff **frag;
	int sent = 0;

	memcpy(skb_put(skb, count), msg, count);

	sent += count;
	msg  += count;
	len  -= count;

	dev->stats.tx_bytes += count;
	dev->stats.tx_packets++;

	raw_dump_table(__func__, "Sending", skb->data, skb->len);

	/* Continuation fragments (no L2CAP header) */
	frag = &skb_shinfo(skb)->frag_list;
	while (len > 0) {
		struct sk_buff *tmp;

		count = min_t(unsigned int, mtu, len);

		tmp = bt_skb_alloc(count, GFP_ATOMIC);
		if (!tmp)
			return -ENOMEM;

		*frag = tmp;

		memcpy(skb_put(*frag, count), msg, count);

		raw_dump_table(__func__, "Sending fragment",
			       (*frag)->data, count);

		(*frag)->priority = skb->priority;

		sent += count;
		msg  += count;
		len  -= count;

		skb->len += (*frag)->len;
		skb->data_len += (*frag)->len;

		frag = &(*frag)->next;

		dev->stats.tx_bytes += count;
		dev->stats.tx_packets++;
	}

	return sent;
}

static struct sk_buff *create_pdu(struct l2cap_conn *conn, void *msg,
				  size_t len, u32 priority,
				  struct net_device *dev)
{
	struct sk_buff *skb;
	int err, count;
	struct l2cap_hdr *lh;

	/* FIXME: This mtu check should be not needed and atm is only used for
	 * testing purposes
	 */
	if (conn->mtu > (L2CAP_LE_MIN_MTU + L2CAP_HDR_SIZE))
		conn->mtu = L2CAP_LE_MIN_MTU + L2CAP_HDR_SIZE;

	count = min_t(unsigned int, (conn->mtu - L2CAP_HDR_SIZE), len);

	BT_DBG("conn %p len %zu mtu %d count %d", conn, len, conn->mtu, count);

	skb = bt_skb_alloc(count + L2CAP_HDR_SIZE, GFP_ATOMIC);
	if (!skb)
		return ERR_PTR(-ENOMEM);

	skb->priority = priority;

	lh = (struct l2cap_hdr *)skb_put(skb, L2CAP_HDR_SIZE);
	lh->cid = cpu_to_le16(L2CAP_FC_6LOWPAN);
	lh->len = cpu_to_le16(len);

	err = skbuff_copy(msg, len, count, conn->mtu, skb, dev);
	if (unlikely(err < 0)) {
		kfree_skb(skb);
		BT_DBG("skbuff copy %d failed", err);
		return ERR_PTR(err);
	}

	return skb;
}

static int conn_send(struct l2cap_conn *conn,
		     void *msg, size_t len, u32 priority,
		     struct net_device *dev)
{
	struct sk_buff *skb;

	skb = create_pdu(conn, msg, len, priority, dev);
	if (IS_ERR(skb))
		return -EINVAL;

	BT_DBG("conn %p skb %p len %d priority %u", conn, skb, skb->len,
	       skb->priority);

	hci_send_acl(conn->hchan, skb, ACL_START);

	return 0;
}

static u8 get_addr_type_from_eui64(u8 byte)
{
	/* Is universal(0) or local(1) bit,  */
	if (byte & 0x02)
		return ADDR_LE_DEV_RANDOM;

	return ADDR_LE_DEV_PUBLIC;
}

static void copy_to_bdaddr(struct in6_addr *ip6_daddr, bdaddr_t *addr)
{
	u8 *eui64 = ip6_daddr->s6_addr + 8;

	addr->b[0] = eui64[7];
	addr->b[1] = eui64[6];
	addr->b[2] = eui64[5];
	addr->b[3] = eui64[2];
	addr->b[4] = eui64[1];
	addr->b[5] = eui64[0];
}

static void convert_dest_bdaddr(struct in6_addr *ip6_daddr,
				bdaddr_t *addr, u8 *addr_type)
{
	copy_to_bdaddr(ip6_daddr, addr);

	/* We need to toggle the U/L bit that we got from IPv6 address
	 * so that we get the proper address and type of the BD address.
	 */
	addr->b[5] ^= 0x02;

	*addr_type = get_addr_type_from_eui64(addr->b[5]);
}

static int header_create(struct sk_buff *skb, struct net_device *netdev,
		         unsigned short type, const void *_daddr,
		         const void *_saddr, unsigned int len)
{
	struct ipv6hdr *hdr;
	struct lowpan_dev *dev;
	struct lowpan_peer *peer;
	bdaddr_t addr, *any = BDADDR_ANY;
	u8 *saddr, *daddr = any->b;
	u8 addr_type;

	if (type != ETH_P_IPV6)
		return -EINVAL;

	hdr = ipv6_hdr(skb);

	dev = lowpan_dev(netdev);

	if (ipv6_addr_is_multicast(&hdr->daddr)) {
		memcpy(&lowpan_cb(skb)->addr, &hdr->daddr,
		       sizeof(struct in6_addr));
		lowpan_cb(skb)->conn = NULL;
	} else {
		unsigned long flags;

		/* Get destination BT device from skb.
		 * If there is no such peer then discard the packet.
		 */
		convert_dest_bdaddr(&hdr->daddr, &addr, &addr_type);

		BT_DBG("dest addr %pMR type %s IP %pI6c", &addr,
		       addr_type == ADDR_LE_DEV_PUBLIC ? "PUBLIC" : "RANDOM",
		       &hdr->daddr);

		read_lock_irqsave(&devices_lock, flags);
		peer = peer_lookup_ba(dev, &addr, addr_type);
		read_unlock_irqrestore(&devices_lock, flags);

		if (!peer) {
			BT_DBG("no such peer %pMR found", &addr);
			return -ENOENT;
		}

		daddr = peer->eui64_addr;

		memcpy(&lowpan_cb(skb)->addr, &hdr->daddr,
		       sizeof(struct in6_addr));
		lowpan_cb(skb)->conn = peer->conn;
	}

	saddr = dev->netdev->dev_addr;

	return lowpan_header_compress(skb, netdev, type, daddr, saddr, len);
}

/* Packet to BT LE device */
static int send_pkt(struct l2cap_conn *conn, const void *saddr,
		    const void *daddr, struct sk_buff *skb,
		    struct net_device *netdev)
{
	raw_dump_table(__func__, "raw skb data dump before fragmentation",
		       skb->data, skb->len);

	return conn_send(conn, skb->data, skb->len, 0, netdev);
}

static void send_mcast_pkt(struct sk_buff *skb, struct net_device *netdev)
{
	struct sk_buff *local_skb;
	struct lowpan_dev *entry, *tmp;
	unsigned long flags;

	read_lock_irqsave(&devices_lock, flags);

	list_for_each_entry_safe(entry, tmp, &bt_6lowpan_devices, list) {
		struct lowpan_peer *pentry, *ptmp;
		struct lowpan_dev *dev;

		if (entry->netdev != netdev)
			continue;

		dev = lowpan_dev(entry->netdev);

		list_for_each_entry_safe(pentry, ptmp, &dev->peers, list) {
			local_skb = skb_clone(skb, GFP_ATOMIC);

			send_pkt(pentry->conn, netdev->dev_addr,
				 pentry->eui64_addr, local_skb, netdev);

			kfree_skb(local_skb);
		}
	}

	read_unlock_irqrestore(&devices_lock, flags);
}

static netdev_tx_t bt_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	int err = 0;
	unsigned char *eui64_addr;
	struct lowpan_dev *dev;
	struct lowpan_peer *peer;
	bdaddr_t addr;
	u8 addr_type;

	if (ipv6_addr_is_multicast(&lowpan_cb(skb)->addr)) {
		/* We need to send the packet to every device
		 * behind this interface.
		 */
		send_mcast_pkt(skb, netdev);
	} else {
		unsigned long flags;

		convert_dest_bdaddr(&lowpan_cb(skb)->addr, &addr, &addr_type);
		eui64_addr = lowpan_cb(skb)->addr.s6_addr + 8;
		dev = lowpan_dev(netdev);

		read_lock_irqsave(&devices_lock, flags);
		peer = peer_lookup_ba(dev, &addr, addr_type);
		read_unlock_irqrestore(&devices_lock, flags);

		BT_DBG("xmit %s to %pMR type %s IP %pI6c peer %p",
		       netdev->name, &addr,
		       addr_type == ADDR_LE_DEV_PUBLIC ? "PUBLIC" : "RANDOM",
		       &lowpan_cb(skb)->addr, peer);

		if (peer && peer->conn)
			err = send_pkt(peer->conn, netdev->dev_addr,
				       eui64_addr, skb, netdev);
	}
	dev_kfree_skb(skb);

	if (err)
		BT_DBG("ERROR: xmit failed (%d)", err);

	return (err < 0) ? NET_XMIT_DROP : err;
}

static const struct net_device_ops netdev_ops = {
	.ndo_start_xmit		= bt_xmit,
};

static struct header_ops header_ops = {
	.create	= header_create,
};

static void netdev_setup(struct net_device *dev)
{
	dev->addr_len		= EUI64_ADDR_LEN;
	dev->type		= ARPHRD_6LOWPAN;

	dev->hard_header_len	= 0;
	dev->needed_tailroom	= 0;
	dev->mtu		= IPV6_MIN_MTU;
	dev->tx_queue_len	= 0;
	dev->flags		= IFF_RUNNING | IFF_POINTOPOINT;
	dev->watchdog_timeo	= 0;

	dev->netdev_ops		= &netdev_ops;
	dev->header_ops		= &header_ops;
	dev->destructor		= free_netdev;
}

static struct device_type bt_type = {
	.name	= "bluetooth",
};

static void set_addr(u8 *eui, u8 *addr, u8 addr_type)
{
	/* addr is the BT address in little-endian format */
	eui[0] = addr[5];
	eui[1] = addr[4];
	eui[2] = addr[3];
	eui[3] = 0xFF;
	eui[4] = 0xFE;
	eui[5] = addr[2];
	eui[6] = addr[1];
	eui[7] = addr[0];

	/* Universal/local bit set, BT 6lowpan draft ch. 3.2.1 */
	if (addr_type == ADDR_LE_DEV_PUBLIC)
		eui[0] &= ~0x02;
	else
		eui[0] |= 0x02;

	BT_DBG("type %d addr %*phC", addr_type, 8, eui);
}

static void set_dev_addr(struct net_device *netdev, bdaddr_t *addr,
		         u8 addr_type)
{
	netdev->addr_assign_type = NET_ADDR_PERM;
	set_addr(netdev->dev_addr, addr->b, addr_type);
}

static void ifup(struct net_device *netdev)
{
	int err;

	rtnl_lock();
	err = dev_open(netdev);
	if (err < 0)
		BT_INFO("iface %s cannot be opened (%d)", netdev->name, err);
	rtnl_unlock();
}

static void do_notify_peers(struct work_struct *work)
{
	struct lowpan_dev *dev = container_of(work, struct lowpan_dev,
					      notify_peers.work);

	netdev_notify_peers(dev->netdev); /* send neighbour adv at startup */
}

static bool is_bt_6lowpan(struct hci_conn *hcon)
{
	if (hcon->type != LE_LINK)
		return false;

	return test_bit(HCI_CONN_6LOWPAN, &hcon->flags);
}

static int add_peer_conn(struct l2cap_conn *conn, struct lowpan_dev *dev)
{
	struct lowpan_peer *peer;
	unsigned long flags;

	peer = kzalloc(sizeof(*peer), GFP_ATOMIC);
	if (!peer)
		return -ENOMEM;

	peer->conn = conn;
	memset(&peer->peer_addr, 0, sizeof(struct in6_addr));

	/* RFC 2464 ch. 5 */
	peer->peer_addr.s6_addr[0] = 0xFE;
	peer->peer_addr.s6_addr[1] = 0x80;
	set_addr((u8 *)&peer->peer_addr.s6_addr + 8, conn->hcon->dst.b,
	         conn->hcon->dst_type);

	memcpy(&peer->eui64_addr, (u8 *)&peer->peer_addr.s6_addr + 8,
	       EUI64_ADDR_LEN);

	write_lock_irqsave(&devices_lock, flags);
	INIT_LIST_HEAD(&peer->list);
	peer_add(dev, peer);
	write_unlock_irqrestore(&devices_lock, flags);

	/* Notifying peers about us needs to be done without locks held */
	INIT_DELAYED_WORK(&dev->notify_peers, do_notify_peers);
	schedule_delayed_work(&dev->notify_peers, msecs_to_jiffies(100));

	return 0;
}

/* This gets called when BT LE 6LoWPAN device is connected. We then
 * create network device that acts as a proxy between BT LE device
 * and kernel network stack.
 */
int bt_6lowpan_add_conn(struct l2cap_conn *conn)
{
	struct lowpan_peer *peer = NULL;
	struct lowpan_dev *dev;
	struct net_device *netdev;
	int err = 0;
	unsigned long flags;

	if (!is_bt_6lowpan(conn->hcon))
		return 0;

	peer = lookup_peer(conn);
	if (peer)
		return -EEXIST;

	dev = lookup_dev(conn);
	if (dev)
		return add_peer_conn(conn, dev);

	netdev = alloc_netdev(sizeof(*dev), IFACE_NAME_TEMPLATE, netdev_setup);
	if (!netdev)
		return -ENOMEM;

	set_dev_addr(netdev, &conn->hcon->src, conn->hcon->src_type);

	netdev->netdev_ops = &netdev_ops;
	SET_NETDEV_DEV(netdev, &conn->hcon->dev);
	SET_NETDEV_DEVTYPE(netdev, &bt_type);

	err = register_netdev(netdev);
	if (err < 0) {
		BT_INFO("register_netdev failed %d", err);
		free_netdev(netdev);
		goto out;
	}

	BT_DBG("ifindex %d peer bdaddr %pMR my addr %pMR",
	       netdev->ifindex, &conn->hcon->dst, &conn->hcon->src);
	set_bit(__LINK_STATE_PRESENT, &netdev->state);

	dev = netdev_priv(netdev);
	dev->netdev = netdev;
	dev->hdev = conn->hcon->hdev;
	INIT_LIST_HEAD(&dev->peers);

	write_lock_irqsave(&devices_lock, flags);
	INIT_LIST_HEAD(&dev->list);
	list_add(&dev->list, &bt_6lowpan_devices);
	write_unlock_irqrestore(&devices_lock, flags);

	ifup(netdev);

	return add_peer_conn(conn, dev);

out:
	return err;
}

static void delete_netdev(struct work_struct *work)
{
	struct lowpan_dev *entry = container_of(work, struct lowpan_dev,
						delete_netdev);

	unregister_netdev(entry->netdev);

	/* The entry pointer is deleted in device_event() */
}

int bt_6lowpan_del_conn(struct l2cap_conn *conn)
{
	struct lowpan_dev *entry, *tmp;
	struct lowpan_dev *dev = NULL;
	struct lowpan_peer *peer;
	int err = -ENOENT;
	unsigned long flags;
	bool last = false;

	if (!conn || !is_bt_6lowpan(conn->hcon))
		return 0;

	write_lock_irqsave(&devices_lock, flags);

	list_for_each_entry_safe(entry, tmp, &bt_6lowpan_devices, list) {
		dev = lowpan_dev(entry->netdev);
		peer = peer_lookup_conn(dev, conn);
		if (peer) {
			last = peer_del(dev, peer);
			err = 0;
			break;
		}
	}

	if (!err && last && dev && !atomic_read(&dev->peer_count)) {
		write_unlock_irqrestore(&devices_lock, flags);

		cancel_delayed_work_sync(&dev->notify_peers);

		/* bt_6lowpan_del_conn() is called with hci dev lock held which
		 * means that we must delete the netdevice in worker thread.
		 */
		INIT_WORK(&entry->delete_netdev, delete_netdev);
		schedule_work(&entry->delete_netdev);
	} else {
		write_unlock_irqrestore(&devices_lock, flags);
	}

	return err;
}

static int device_event(struct notifier_block *unused,
			unsigned long event, void *ptr)
{
	struct net_device *netdev = netdev_notifier_info_to_dev(ptr);
	struct lowpan_dev *entry, *tmp;
	unsigned long flags;

	if (netdev->type != ARPHRD_6LOWPAN)
		return NOTIFY_DONE;

	switch (event) {
	case NETDEV_UNREGISTER:
		write_lock_irqsave(&devices_lock, flags);
		list_for_each_entry_safe(entry, tmp, &bt_6lowpan_devices,
					 list) {
			if (entry->netdev == netdev) {
				list_del(&entry->list);
				kfree(entry);
				break;
			}
		}
		write_unlock_irqrestore(&devices_lock, flags);
		break;
	}

	return NOTIFY_DONE;
}

static struct notifier_block bt_6lowpan_dev_notifier = {
	.notifier_call = device_event,
};

int bt_6lowpan_init(void)
{
	return register_netdevice_notifier(&bt_6lowpan_dev_notifier);
}

void bt_6lowpan_cleanup(void)
{
	unregister_netdevice_notifier(&bt_6lowpan_dev_notifier);
}
