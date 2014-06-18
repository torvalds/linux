/*
   Copyright (c) 2013-2014 Intel Corp.

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
#include <linux/module.h>
#include <linux/debugfs.h>

#include <net/ipv6.h>
#include <net/ip6_route.h>
#include <net/addrconf.h>

#include <net/af_ieee802154.h> /* to get the address type */

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include <net/bluetooth/l2cap.h>

#include <net/6lowpan.h> /* for the compression support */

#define VERSION "0.1"

static struct dentry *lowpan_psm_debugfs;
static struct dentry *lowpan_control_debugfs;

#define IFACE_NAME_TEMPLATE "bt%d"
#define EUI64_ADDR_LEN 8

struct skb_cb {
	struct in6_addr addr;
	struct l2cap_chan *chan;
	int status;
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

/* If psm is set to 0 (default value), then 6lowpan is disabled.
 * Other values are used to indicate a Protocol Service Multiplexer
 * value for 6lowpan.
 */
static u16 psm_6lowpan;

/* We are listening incoming connections via this channel
 */
static struct l2cap_chan *listen_chan;

struct lowpan_peer {
	struct list_head list;
	struct l2cap_chan *chan;

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
		BT_DBG("dst addr %pMR dst type %d",
		       &peer->chan->dst, peer->chan->dst_type);

		if (bacmp(&peer->chan->dst, ba))
			continue;

		if (type == peer->chan->dst_type)
			return peer;
	}

	return NULL;
}

static inline struct lowpan_peer *peer_lookup_chan(struct lowpan_dev *dev,
						   struct l2cap_chan *chan)
{
	struct lowpan_peer *peer, *tmp;

	list_for_each_entry_safe(peer, tmp, &dev->peers, list) {
		if (peer->chan == chan)
			return peer;
	}

	return NULL;
}

static inline struct lowpan_peer *peer_lookup_conn(struct lowpan_dev *dev,
						   struct l2cap_conn *conn)
{
	struct lowpan_peer *peer, *tmp;

	list_for_each_entry_safe(peer, tmp, &dev->peers, list) {
		if (peer->chan->conn == conn)
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
	if (ret < 0) {
		BT_DBG("receive skb %d", ret);
		return NET_RX_DROP;
	}

	return ret;
}

static int process_data(struct sk_buff *skb, struct net_device *netdev,
			struct l2cap_chan *chan)
{
	const u8 *saddr, *daddr;
	u8 iphc0, iphc1;
	struct lowpan_dev *dev;
	struct lowpan_peer *peer;
	unsigned long flags;

	dev = lowpan_dev(netdev);

	read_lock_irqsave(&devices_lock, flags);
	peer = peer_lookup_chan(dev, chan);
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
		    struct l2cap_chan *chan)
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

			ret = process_data(local_skb, dev, chan);
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
	dev->stats.rx_dropped++;
	kfree_skb(skb);
	return NET_RX_DROP;
}

/* Packet from BT LE device */
static int chan_recv_cb(struct l2cap_chan *chan, struct sk_buff *skb)
{
	struct lowpan_dev *dev;
	struct lowpan_peer *peer;
	int err;

	peer = lookup_peer(chan->conn);
	if (!peer)
		return -ENOENT;

	dev = lookup_dev(chan->conn);
	if (!dev || !dev->netdev)
		return -ENOENT;

	err = recv_pkt(skb, dev->netdev, chan);
	if (err) {
		BT_DBG("recv pkt %d", err);
		err = -EAGAIN;
	}

	return err;
}

static u8 get_addr_type_from_eui64(u8 byte)
{
	/* Is universal(0) or local(1) bit */
	return ((byte & 0x02) ? BDADDR_LE_RANDOM : BDADDR_LE_PUBLIC);
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
		lowpan_cb(skb)->chan = NULL;
	} else {
		unsigned long flags;

		/* Get destination BT device from skb.
		 * If there is no such peer then discard the packet.
		 */
		convert_dest_bdaddr(&hdr->daddr, &addr, &addr_type);

		BT_DBG("dest addr %pMR type %d IP %pI6c", &addr,
		       addr_type, &hdr->daddr);

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
		lowpan_cb(skb)->chan = peer->chan;
	}

	saddr = dev->netdev->dev_addr;

	return lowpan_header_compress(skb, netdev, type, daddr, saddr, len);
}

/* Packet to BT LE device */
static int send_pkt(struct l2cap_chan *chan, struct sk_buff *skb,
		    struct net_device *netdev)
{
	struct msghdr msg;
	struct kvec iv;
	int err;

	/* Remember the skb so that we can send EAGAIN to the caller if
	 * we run out of credits.
	 */
	chan->data = skb;

	memset(&msg, 0, sizeof(msg));
	msg.msg_iov = (struct iovec *) &iv;
	msg.msg_iovlen = 1;
	iv.iov_base = skb->data;
	iv.iov_len = skb->len;

	err = l2cap_chan_send(chan, &msg, skb->len);
	if (err > 0) {
		netdev->stats.tx_bytes += err;
		netdev->stats.tx_packets++;
		return 0;
	}

	if (!err)
		err = lowpan_cb(skb)->status;

	if (err < 0) {
		if (err == -EAGAIN)
			netdev->stats.tx_dropped++;
		else
			netdev->stats.tx_errors++;
	}

	return err;
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

			send_pkt(pentry->chan, local_skb, netdev);

			kfree_skb(local_skb);
		}
	}

	read_unlock_irqrestore(&devices_lock, flags);
}

static netdev_tx_t bt_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	int err = 0;
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
		dev = lowpan_dev(netdev);

		read_lock_irqsave(&devices_lock, flags);
		peer = peer_lookup_ba(dev, &addr, addr_type);
		read_unlock_irqrestore(&devices_lock, flags);

		BT_DBG("xmit %s to %pMR type %d IP %pI6c peer %p",
		       netdev->name, &addr, addr_type,
		       &lowpan_cb(skb)->addr, peer);

		if (peer && peer->chan)
			err = send_pkt(peer->chan, skb, netdev);
		else
			err = -ENOENT;
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
	if (addr_type == BDADDR_LE_PUBLIC)
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

	if (!psm_6lowpan)
		return false;

	return true;
}

static struct l2cap_chan *chan_create(void)
{
	struct l2cap_chan *chan;

	chan = l2cap_chan_create();
	if (!chan)
		return NULL;

	l2cap_chan_set_defaults(chan);

	chan->chan_type = L2CAP_CHAN_CONN_ORIENTED;
	chan->mode = L2CAP_MODE_LE_FLOWCTL;
	chan->omtu = 65535;
	chan->imtu = chan->omtu;

	return chan;
}

static struct l2cap_chan *chan_open(struct l2cap_chan *pchan)
{
	struct l2cap_chan *chan;

	chan = chan_create();
	if (!chan)
		return NULL;

	chan->remote_mps = chan->omtu;
	chan->mps = chan->omtu;

	chan->state = BT_CONNECTED;

	return chan;
}

static struct l2cap_chan *add_peer_chan(struct l2cap_chan *chan,
					struct lowpan_dev *dev)
{
	struct lowpan_peer *peer;
	unsigned long flags;

	peer = kzalloc(sizeof(*peer), GFP_ATOMIC);
	if (!peer)
		return NULL;

	peer->chan = chan;
	memset(&peer->peer_addr, 0, sizeof(struct in6_addr));

	/* RFC 2464 ch. 5 */
	peer->peer_addr.s6_addr[0] = 0xFE;
	peer->peer_addr.s6_addr[1] = 0x80;
	set_addr((u8 *)&peer->peer_addr.s6_addr + 8, chan->dst.b,
		 chan->dst_type);

	memcpy(&peer->eui64_addr, (u8 *)&peer->peer_addr.s6_addr + 8,
	       EUI64_ADDR_LEN);

	write_lock_irqsave(&devices_lock, flags);
	INIT_LIST_HEAD(&peer->list);
	peer_add(dev, peer);
	write_unlock_irqrestore(&devices_lock, flags);

	/* Notifying peers about us needs to be done without locks held */
	INIT_DELAYED_WORK(&dev->notify_peers, do_notify_peers);
	schedule_delayed_work(&dev->notify_peers, msecs_to_jiffies(100));

	return peer->chan;
}

static int setup_netdev(struct l2cap_chan *chan, struct lowpan_dev **dev)
{
	struct net_device *netdev;
	int err = 0;
	unsigned long flags;

	netdev = alloc_netdev(sizeof(struct lowpan_dev), IFACE_NAME_TEMPLATE,
			      netdev_setup);
	if (!netdev)
		return -ENOMEM;

	set_dev_addr(netdev, &chan->src, chan->src_type);

	netdev->netdev_ops = &netdev_ops;
	SET_NETDEV_DEV(netdev, &chan->conn->hcon->dev);
	SET_NETDEV_DEVTYPE(netdev, &bt_type);

	err = register_netdev(netdev);
	if (err < 0) {
		BT_INFO("register_netdev failed %d", err);
		free_netdev(netdev);
		goto out;
	}

	BT_DBG("ifindex %d peer bdaddr %pMR type %d my addr %pMR type %d",
	       netdev->ifindex, &chan->dst, chan->dst_type,
	       &chan->src, chan->src_type);
	set_bit(__LINK_STATE_PRESENT, &netdev->state);

	*dev = netdev_priv(netdev);
	(*dev)->netdev = netdev;
	(*dev)->hdev = chan->conn->hcon->hdev;
	INIT_LIST_HEAD(&(*dev)->peers);

	write_lock_irqsave(&devices_lock, flags);
	INIT_LIST_HEAD(&(*dev)->list);
	list_add(&(*dev)->list, &bt_6lowpan_devices);
	write_unlock_irqrestore(&devices_lock, flags);

	return 0;

out:
	return err;
}

static inline void chan_ready_cb(struct l2cap_chan *chan)
{
	struct lowpan_dev *dev;

	dev = lookup_dev(chan->conn);

	BT_DBG("chan %p conn %p dev %p", chan, chan->conn, dev);

	if (!dev) {
		if (setup_netdev(chan, &dev) < 0) {
			l2cap_chan_del(chan, -ENOENT);
			return;
		}
	}

	add_peer_chan(chan, dev);
	ifup(dev->netdev);
}

static inline struct l2cap_chan *chan_new_conn_cb(struct l2cap_chan *chan)
{
	struct l2cap_chan *pchan;

	pchan = chan_open(chan);
	pchan->ops = chan->ops;

	BT_DBG("chan %p pchan %p", chan, pchan);

	return pchan;
}

static void delete_netdev(struct work_struct *work)
{
	struct lowpan_dev *entry = container_of(work, struct lowpan_dev,
						delete_netdev);

	unregister_netdev(entry->netdev);

	/* The entry pointer is deleted in device_event() */
}

static void chan_close_cb(struct l2cap_chan *chan)
{
	struct lowpan_dev *entry, *tmp;
	struct lowpan_dev *dev = NULL;
	struct lowpan_peer *peer;
	int err = -ENOENT;
	unsigned long flags;
	bool last = false, removed = true;

	BT_DBG("chan %p conn %p", chan, chan->conn);

	if (chan->conn && chan->conn->hcon) {
		if (!is_bt_6lowpan(chan->conn->hcon))
			return;

		/* If conn is set, then the netdev is also there and we should
		 * not remove it.
		 */
		removed = false;
	}

	write_lock_irqsave(&devices_lock, flags);

	list_for_each_entry_safe(entry, tmp, &bt_6lowpan_devices, list) {
		dev = lowpan_dev(entry->netdev);
		peer = peer_lookup_chan(dev, chan);
		if (peer) {
			last = peer_del(dev, peer);
			err = 0;

			BT_DBG("dev %p removing %speer %p", dev,
			       last ? "last " : "1 ", peer);
			BT_DBG("chan %p orig refcnt %d", chan,
			       atomic_read(&chan->kref.refcount));

			l2cap_chan_put(chan);
			kfree(peer);
			break;
		}
	}

	if (!err && last && dev && !atomic_read(&dev->peer_count)) {
		write_unlock_irqrestore(&devices_lock, flags);

		cancel_delayed_work_sync(&dev->notify_peers);

		if (!removed) {
			INIT_WORK(&entry->delete_netdev, delete_netdev);
			schedule_work(&entry->delete_netdev);
		}
	} else {
		write_unlock_irqrestore(&devices_lock, flags);
	}

	return;
}

static void chan_state_change_cb(struct l2cap_chan *chan, int state, int err)
{
	BT_DBG("chan %p conn %p state %s err %d", chan, chan->conn,
	       state_to_string(state), err);
}

static struct sk_buff *chan_alloc_skb_cb(struct l2cap_chan *chan,
					 unsigned long hdr_len,
					 unsigned long len, int nb)
{
	/* Note that we must allocate using GFP_ATOMIC here as
	 * this function is called originally from netdev hard xmit
	 * function in atomic context.
	 */
	return bt_skb_alloc(hdr_len + len, GFP_ATOMIC);
}

static void chan_suspend_cb(struct l2cap_chan *chan)
{
	struct sk_buff *skb = chan->data;

	BT_DBG("chan %p conn %p skb %p", chan, chan->conn, skb);

	lowpan_cb(skb)->status = -EAGAIN;
}

static void chan_resume_cb(struct l2cap_chan *chan)
{
	struct sk_buff *skb = chan->data;

	BT_DBG("chan %p conn %p skb %p", chan, chan->conn, skb);

	lowpan_cb(skb)->status = 0;
}

static long chan_get_sndtimeo_cb(struct l2cap_chan *chan)
{
	return msecs_to_jiffies(1000);
}

static const struct l2cap_ops bt_6lowpan_chan_ops = {
	.name			= "L2CAP 6LoWPAN channel",
	.new_connection		= chan_new_conn_cb,
	.recv			= chan_recv_cb,
	.close			= chan_close_cb,
	.state_change		= chan_state_change_cb,
	.ready			= chan_ready_cb,
	.resume			= chan_resume_cb,
	.suspend		= chan_suspend_cb,
	.get_sndtimeo		= chan_get_sndtimeo_cb,
	.alloc_skb		= chan_alloc_skb_cb,
	.memcpy_fromiovec	= l2cap_chan_no_memcpy_fromiovec,

	.teardown		= l2cap_chan_no_teardown,
	.defer			= l2cap_chan_no_defer,
	.set_shutdown		= l2cap_chan_no_set_shutdown,
};

static inline __u8 bdaddr_type(__u8 type)
{
	if (type == ADDR_LE_DEV_PUBLIC)
		return BDADDR_LE_PUBLIC;
	else
		return BDADDR_LE_RANDOM;
}

static struct l2cap_chan *chan_get(void)
{
	struct l2cap_chan *pchan;

	pchan = chan_create();
	if (!pchan)
		return NULL;

	pchan->ops = &bt_6lowpan_chan_ops;

	return pchan;
}

static int bt_6lowpan_connect(bdaddr_t *addr, u8 dst_type)
{
	struct l2cap_chan *pchan;
	int err;

	pchan = chan_get();
	if (!pchan)
		return -EINVAL;

	err = l2cap_chan_connect(pchan, cpu_to_le16(psm_6lowpan), 0,
				 addr, dst_type);

	BT_DBG("chan %p err %d", pchan, err);
	if (err < 0)
		l2cap_chan_put(pchan);

	return err;
}

static int bt_6lowpan_disconnect(struct l2cap_conn *conn, u8 dst_type)
{
	struct lowpan_peer *peer;

	BT_DBG("conn %p dst type %d", conn, dst_type);

	peer = lookup_peer(conn);
	if (!peer)
		return -ENOENT;

	BT_DBG("peer %p chan %p", peer, peer->chan);

	l2cap_chan_close(peer->chan, ENOENT);

	return 0;
}

static struct l2cap_chan *bt_6lowpan_listen(void)
{
	bdaddr_t *addr = BDADDR_ANY;
	struct l2cap_chan *pchan;
	int err;

	if (psm_6lowpan == 0)
		return NULL;

	pchan = chan_get();
	if (!pchan)
		return NULL;

	pchan->state = BT_LISTEN;
	pchan->src_type = BDADDR_LE_PUBLIC;

	BT_DBG("psm 0x%04x chan %p src type %d", psm_6lowpan, pchan,
	       pchan->src_type);

	err = l2cap_add_psm(pchan, addr, cpu_to_le16(psm_6lowpan));
	if (err) {
		l2cap_chan_put(pchan);
		BT_ERR("psm cannot be added err %d", err);
		return NULL;
	}

	return pchan;
}

static int get_l2cap_conn(char *buf, bdaddr_t *addr, u8 *addr_type,
			  struct l2cap_conn **conn)
{
	struct hci_conn *hcon;
	struct hci_dev *hdev;
	bdaddr_t *src = BDADDR_ANY;
	int n;

	n = sscanf(buf, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx %hhu",
		   &addr->b[5], &addr->b[4], &addr->b[3],
		   &addr->b[2], &addr->b[1], &addr->b[0],
		   addr_type);

	if (n < 7)
		return -EINVAL;

	hdev = hci_get_route(addr, src);
	if (!hdev)
		return -ENOENT;

	hci_dev_lock(hdev);
	hcon = hci_conn_hash_lookup_ba(hdev, LE_LINK, addr);
	hci_dev_unlock(hdev);

	if (!hcon)
		return -ENOENT;

	*conn = (struct l2cap_conn *)hcon->l2cap_data;

	BT_DBG("conn %p dst %pMR type %d", *conn, &hcon->dst, hcon->dst_type);

	return 0;
}

static void disconnect_all_peers(void)
{
	struct lowpan_dev *entry, *tmp_dev;
	struct lowpan_peer *peer, *tmp_peer, *new_peer;
	struct list_head peers;
	unsigned long flags;

	INIT_LIST_HEAD(&peers);

	/* We make a separate list of peers as the close_cb() will
	 * modify the device peers list so it is better not to mess
	 * with the same list at the same time.
	 */

	read_lock_irqsave(&devices_lock, flags);

	list_for_each_entry_safe(entry, tmp_dev, &bt_6lowpan_devices, list) {
		list_for_each_entry_safe(peer, tmp_peer, &entry->peers, list) {
			new_peer = kmalloc(sizeof(*new_peer), GFP_ATOMIC);
			if (!new_peer)
				break;

			new_peer->chan = peer->chan;
			INIT_LIST_HEAD(&new_peer->list);

			list_add(&new_peer->list, &peers);
		}
	}

	read_unlock_irqrestore(&devices_lock, flags);

	list_for_each_entry_safe(peer, tmp_peer, &peers, list) {
		l2cap_chan_close(peer->chan, ENOENT);
		kfree(peer);
	}
}

static int lowpan_psm_set(void *data, u64 val)
{
	u16 psm;

	psm = val;
	if (psm == 0 || psm_6lowpan != psm)
		/* Disconnect existing connections if 6lowpan is
		 * disabled (psm = 0), or if psm changes.
		 */
		disconnect_all_peers();

	psm_6lowpan = psm;

	if (listen_chan) {
		l2cap_chan_close(listen_chan, 0);
		l2cap_chan_put(listen_chan);
	}

	listen_chan = bt_6lowpan_listen();

	return 0;
}

static int lowpan_psm_get(void *data, u64 *val)
{
	*val = psm_6lowpan;
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(lowpan_psm_fops, lowpan_psm_get,
			lowpan_psm_set, "%llu\n");

static ssize_t lowpan_control_write(struct file *fp,
				    const char __user *user_buffer,
				    size_t count,
				    loff_t *position)
{
	char buf[32];
	size_t buf_size = min(count, sizeof(buf) - 1);
	int ret;
	bdaddr_t addr;
	u8 addr_type;
	struct l2cap_conn *conn = NULL;

	if (copy_from_user(buf, user_buffer, buf_size))
		return -EFAULT;

	buf[buf_size] = '\0';

	if (memcmp(buf, "connect ", 8) == 0) {
		ret = get_l2cap_conn(&buf[8], &addr, &addr_type, &conn);
		if (ret == -EINVAL)
			return ret;

		if (listen_chan) {
			l2cap_chan_close(listen_chan, 0);
			l2cap_chan_put(listen_chan);
			listen_chan = NULL;
		}

		if (conn) {
			struct lowpan_peer *peer;

			if (!is_bt_6lowpan(conn->hcon))
				return -EINVAL;

			peer = lookup_peer(conn);
			if (peer) {
				BT_DBG("6LoWPAN connection already exists");
				return -EALREADY;
			}

			BT_DBG("conn %p dst %pMR type %d user %d", conn,
			       &conn->hcon->dst, conn->hcon->dst_type,
			       addr_type);
		}

		ret = bt_6lowpan_connect(&addr, addr_type);
		if (ret < 0)
			return ret;

		return count;
	}

	if (memcmp(buf, "disconnect ", 11) == 0) {
		ret = get_l2cap_conn(&buf[11], &addr, &addr_type, &conn);
		if (ret < 0)
			return ret;

		ret = bt_6lowpan_disconnect(conn, addr_type);
		if (ret < 0)
			return ret;

		return count;
	}

	return count;
}

static int lowpan_control_show(struct seq_file *f, void *ptr)
{
	struct lowpan_dev *entry, *tmp_dev;
	struct lowpan_peer *peer, *tmp_peer;
	unsigned long flags;

	read_lock_irqsave(&devices_lock, flags);

	list_for_each_entry_safe(entry, tmp_dev, &bt_6lowpan_devices, list) {
		list_for_each_entry_safe(peer, tmp_peer, &entry->peers, list)
			seq_printf(f, "%pMR (type %u)\n",
				   &peer->chan->dst, peer->chan->dst_type);
	}

	read_unlock_irqrestore(&devices_lock, flags);

	return 0;
}

static int lowpan_control_open(struct inode *inode, struct file *file)
{
	return single_open(file, lowpan_control_show, inode->i_private);
}

static const struct file_operations lowpan_control_fops = {
	.open		= lowpan_control_open,
	.read		= seq_read,
	.write		= lowpan_control_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

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

static int __init bt_6lowpan_init(void)
{
	lowpan_psm_debugfs = debugfs_create_file("6lowpan_psm", 0644,
						 bt_debugfs, NULL,
						 &lowpan_psm_fops);
	lowpan_control_debugfs = debugfs_create_file("6lowpan_control", 0644,
						     bt_debugfs, NULL,
						     &lowpan_control_fops);

	return register_netdevice_notifier(&bt_6lowpan_dev_notifier);
}

static void __exit bt_6lowpan_exit(void)
{
	debugfs_remove(lowpan_psm_debugfs);
	debugfs_remove(lowpan_control_debugfs);

	if (listen_chan) {
		l2cap_chan_close(listen_chan, 0);
		l2cap_chan_put(listen_chan);
	}

	unregister_netdevice_notifier(&bt_6lowpan_dev_notifier);
}

module_init(bt_6lowpan_init);
module_exit(bt_6lowpan_exit);

MODULE_AUTHOR("Jukka Rissanen <jukka.rissanen@linux.intel.com>");
MODULE_DESCRIPTION("Bluetooth 6LoWPAN");
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");
