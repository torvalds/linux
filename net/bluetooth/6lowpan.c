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
#include <net/pkt_sched.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include <net/bluetooth/l2cap.h>

#include <net/6lowpan.h> /* for the compression support */

#define VERSION "0.1"

static struct dentry *lowpan_enable_debugfs;
static struct dentry *lowpan_control_debugfs;

#define IFACE_NAME_TEMPLATE "bt%d"

struct skb_cb {
	struct in6_addr addr;
	struct in6_addr gw;
	struct l2cap_chan *chan;
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
static DEFINE_SPINLOCK(devices_lock);

static bool enable_6lowpan;

/* We are listening incoming connections via this channel
 */
static struct l2cap_chan *listen_chan;

struct lowpan_peer {
	struct list_head list;
	struct rcu_head rcu;
	struct l2cap_chan *chan;

	/* peer addresses in various formats */
	unsigned char lladdr[ETH_ALEN];
	struct in6_addr peer_addr;
};

struct lowpan_btle_dev {
	struct list_head list;

	struct hci_dev *hdev;
	struct net_device *netdev;
	struct list_head peers;
	atomic_t peer_count; /* number of items in peers list */

	struct work_struct delete_netdev;
	struct delayed_work notify_peers;
};

static inline struct lowpan_btle_dev *
lowpan_btle_dev(const struct net_device *netdev)
{
	return (struct lowpan_btle_dev *)lowpan_dev(netdev)->priv;
}

static inline void peer_add(struct lowpan_btle_dev *dev,
			    struct lowpan_peer *peer)
{
	list_add_rcu(&peer->list, &dev->peers);
	atomic_inc(&dev->peer_count);
}

static inline bool peer_del(struct lowpan_btle_dev *dev,
			    struct lowpan_peer *peer)
{
	list_del_rcu(&peer->list);
	kfree_rcu(peer, rcu);

	module_put(THIS_MODULE);

	if (atomic_dec_and_test(&dev->peer_count)) {
		BT_DBG("last peer");
		return true;
	}

	return false;
}

static inline struct lowpan_peer *peer_lookup_ba(struct lowpan_btle_dev *dev,
						 bdaddr_t *ba, __u8 type)
{
	struct lowpan_peer *peer;

	BT_DBG("peers %d addr %pMR type %d", atomic_read(&dev->peer_count),
	       ba, type);

	rcu_read_lock();

	list_for_each_entry_rcu(peer, &dev->peers, list) {
		BT_DBG("dst addr %pMR dst type %d",
		       &peer->chan->dst, peer->chan->dst_type);

		if (bacmp(&peer->chan->dst, ba))
			continue;

		if (type == peer->chan->dst_type) {
			rcu_read_unlock();
			return peer;
		}
	}

	rcu_read_unlock();

	return NULL;
}

static inline struct lowpan_peer *
__peer_lookup_chan(struct lowpan_btle_dev *dev, struct l2cap_chan *chan)
{
	struct lowpan_peer *peer;

	list_for_each_entry_rcu(peer, &dev->peers, list) {
		if (peer->chan == chan)
			return peer;
	}

	return NULL;
}

static inline struct lowpan_peer *
__peer_lookup_conn(struct lowpan_btle_dev *dev, struct l2cap_conn *conn)
{
	struct lowpan_peer *peer;

	list_for_each_entry_rcu(peer, &dev->peers, list) {
		if (peer->chan->conn == conn)
			return peer;
	}

	return NULL;
}

static inline struct lowpan_peer *peer_lookup_dst(struct lowpan_btle_dev *dev,
						  struct in6_addr *daddr,
						  struct sk_buff *skb)
{
	struct lowpan_peer *peer;
	struct in6_addr *nexthop;
	struct rt6_info *rt = (struct rt6_info *)skb_dst(skb);
	int count = atomic_read(&dev->peer_count);

	BT_DBG("peers %d addr %pI6c rt %p", count, daddr, rt);

	/* If we have multiple 6lowpan peers, then check where we should
	 * send the packet. If only one peer exists, then we can send the
	 * packet right away.
	 */
	if (count == 1) {
		rcu_read_lock();
		peer = list_first_or_null_rcu(&dev->peers, struct lowpan_peer,
					      list);
		rcu_read_unlock();
		return peer;
	}

	if (!rt) {
		nexthop = &lowpan_cb(skb)->gw;

		if (ipv6_addr_any(nexthop))
			return NULL;
	} else {
		nexthop = rt6_nexthop(rt, daddr);

		/* We need to remember the address because it is needed
		 * by bt_xmit() when sending the packet. In bt_xmit(), the
		 * destination routing info is not set.
		 */
		memcpy(&lowpan_cb(skb)->gw, nexthop, sizeof(struct in6_addr));
	}

	BT_DBG("gw %pI6c", nexthop);

	rcu_read_lock();

	list_for_each_entry_rcu(peer, &dev->peers, list) {
		BT_DBG("dst addr %pMR dst type %d ip %pI6c",
		       &peer->chan->dst, peer->chan->dst_type,
		       &peer->peer_addr);

		if (!ipv6_addr_cmp(&peer->peer_addr, nexthop)) {
			rcu_read_unlock();
			return peer;
		}
	}

	rcu_read_unlock();

	return NULL;
}

static struct lowpan_peer *lookup_peer(struct l2cap_conn *conn)
{
	struct lowpan_btle_dev *entry;
	struct lowpan_peer *peer = NULL;

	rcu_read_lock();

	list_for_each_entry_rcu(entry, &bt_6lowpan_devices, list) {
		peer = __peer_lookup_conn(entry, conn);
		if (peer)
			break;
	}

	rcu_read_unlock();

	return peer;
}

static struct lowpan_btle_dev *lookup_dev(struct l2cap_conn *conn)
{
	struct lowpan_btle_dev *entry;
	struct lowpan_btle_dev *dev = NULL;

	rcu_read_lock();

	list_for_each_entry_rcu(entry, &bt_6lowpan_devices, list) {
		if (conn->hcon->hdev == entry->hdev) {
			dev = entry;
			break;
		}
	}

	rcu_read_unlock();

	return dev;
}

static int give_skb_to_upper(struct sk_buff *skb, struct net_device *dev)
{
	struct sk_buff *skb_cp;

	skb_cp = skb_copy(skb, GFP_ATOMIC);
	if (!skb_cp)
		return NET_RX_DROP;

	return netif_rx_ni(skb_cp);
}

static int iphc_decompress(struct sk_buff *skb, struct net_device *netdev,
			   struct lowpan_peer *peer)
{
	const u8 *saddr;
	struct lowpan_btle_dev *dev;

	dev = lowpan_btle_dev(netdev);

	saddr = peer->lladdr;

	return lowpan_header_decompress(skb, netdev, netdev->dev_addr, saddr);
}

static int recv_pkt(struct sk_buff *skb, struct net_device *dev,
		    struct lowpan_peer *peer)
{
	struct sk_buff *local_skb;
	int ret;

	if (!netif_running(dev))
		goto drop;

	if (dev->type != ARPHRD_6LOWPAN || !skb->len)
		goto drop;

	skb_reset_network_header(skb);

	skb = skb_share_check(skb, GFP_ATOMIC);
	if (!skb)
		goto drop;

	/* check that it's our buffer */
	if (lowpan_is_ipv6(*skb_network_header(skb))) {
		/* Pull off the 1-byte of 6lowpan header. */
		skb_pull(skb, 1);

		/* Copy the packet so that the IPv6 header is
		 * properly aligned.
		 */
		local_skb = skb_copy_expand(skb, NET_SKB_PAD - 1,
					    skb_tailroom(skb), GFP_ATOMIC);
		if (!local_skb)
			goto drop;

		local_skb->protocol = htons(ETH_P_IPV6);
		local_skb->pkt_type = PACKET_HOST;
		local_skb->dev = dev;

		skb_set_transport_header(local_skb, sizeof(struct ipv6hdr));

		if (give_skb_to_upper(local_skb, dev) != NET_RX_SUCCESS) {
			kfree_skb(local_skb);
			goto drop;
		}

		dev->stats.rx_bytes += skb->len;
		dev->stats.rx_packets++;

		consume_skb(local_skb);
		consume_skb(skb);
	} else if (lowpan_is_iphc(*skb_network_header(skb))) {
		local_skb = skb_clone(skb, GFP_ATOMIC);
		if (!local_skb)
			goto drop;

		local_skb->dev = dev;

		ret = iphc_decompress(local_skb, dev, peer);
		if (ret < 0) {
			BT_DBG("iphc_decompress failed: %d", ret);
			kfree_skb(local_skb);
			goto drop;
		}

		local_skb->protocol = htons(ETH_P_IPV6);
		local_skb->pkt_type = PACKET_HOST;

		if (give_skb_to_upper(local_skb, dev)
				!= NET_RX_SUCCESS) {
			kfree_skb(local_skb);
			goto drop;
		}

		dev->stats.rx_bytes += skb->len;
		dev->stats.rx_packets++;

		consume_skb(local_skb);
		consume_skb(skb);
	} else {
		BT_DBG("unknown packet type");
		goto drop;
	}

	return NET_RX_SUCCESS;

drop:
	dev->stats.rx_dropped++;
	return NET_RX_DROP;
}

/* Packet from BT LE device */
static int chan_recv_cb(struct l2cap_chan *chan, struct sk_buff *skb)
{
	struct lowpan_btle_dev *dev;
	struct lowpan_peer *peer;
	int err;

	peer = lookup_peer(chan->conn);
	if (!peer)
		return -ENOENT;

	dev = lookup_dev(chan->conn);
	if (!dev || !dev->netdev)
		return -ENOENT;

	err = recv_pkt(skb, dev->netdev, peer);
	if (err) {
		BT_DBG("recv pkt %d", err);
		err = -EAGAIN;
	}

	return err;
}

static int setup_header(struct sk_buff *skb, struct net_device *netdev,
			bdaddr_t *peer_addr, u8 *peer_addr_type)
{
	struct in6_addr ipv6_daddr;
	struct ipv6hdr *hdr;
	struct lowpan_btle_dev *dev;
	struct lowpan_peer *peer;
	u8 *daddr;
	int err, status = 0;

	hdr = ipv6_hdr(skb);

	dev = lowpan_btle_dev(netdev);

	memcpy(&ipv6_daddr, &hdr->daddr, sizeof(ipv6_daddr));

	if (ipv6_addr_is_multicast(&ipv6_daddr)) {
		lowpan_cb(skb)->chan = NULL;
		daddr = NULL;
	} else {
		BT_DBG("dest IP %pI6c", &ipv6_daddr);

		/* The packet might be sent to 6lowpan interface
		 * because of routing (either via default route
		 * or user set route) so get peer according to
		 * the destination address.
		 */
		peer = peer_lookup_dst(dev, &ipv6_daddr, skb);
		if (!peer) {
			BT_DBG("no such peer");
			return -ENOENT;
		}

		daddr = peer->lladdr;
		*peer_addr = peer->chan->dst;
		*peer_addr_type = peer->chan->dst_type;
		lowpan_cb(skb)->chan = peer->chan;

		status = 1;
	}

	lowpan_header_compress(skb, netdev, daddr, dev->netdev->dev_addr);

	err = dev_hard_header(skb, netdev, ETH_P_IPV6, NULL, NULL, 0);
	if (err < 0)
		return err;

	return status;
}

static int header_create(struct sk_buff *skb, struct net_device *netdev,
			 unsigned short type, const void *_daddr,
			 const void *_saddr, unsigned int len)
{
	if (type != ETH_P_IPV6)
		return -EINVAL;

	return 0;
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

	iv.iov_base = skb->data;
	iv.iov_len = skb->len;

	memset(&msg, 0, sizeof(msg));
	iov_iter_kvec(&msg.msg_iter, WRITE | ITER_KVEC, &iv, 1, skb->len);

	err = l2cap_chan_send(chan, &msg, skb->len);
	if (err > 0) {
		netdev->stats.tx_bytes += err;
		netdev->stats.tx_packets++;
		return 0;
	}

	if (err < 0)
		netdev->stats.tx_errors++;

	return err;
}

static int send_mcast_pkt(struct sk_buff *skb, struct net_device *netdev)
{
	struct sk_buff *local_skb;
	struct lowpan_btle_dev *entry;
	int err = 0;

	rcu_read_lock();

	list_for_each_entry_rcu(entry, &bt_6lowpan_devices, list) {
		struct lowpan_peer *pentry;
		struct lowpan_btle_dev *dev;

		if (entry->netdev != netdev)
			continue;

		dev = lowpan_btle_dev(entry->netdev);

		list_for_each_entry_rcu(pentry, &dev->peers, list) {
			int ret;

			local_skb = skb_clone(skb, GFP_ATOMIC);

			BT_DBG("xmit %s to %pMR type %d IP %pI6c chan %p",
			       netdev->name,
			       &pentry->chan->dst, pentry->chan->dst_type,
			       &pentry->peer_addr, pentry->chan);
			ret = send_pkt(pentry->chan, local_skb, netdev);
			if (ret < 0)
				err = ret;

			kfree_skb(local_skb);
		}
	}

	rcu_read_unlock();

	return err;
}

static netdev_tx_t bt_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	int err = 0;
	bdaddr_t addr;
	u8 addr_type;

	/* We must take a copy of the skb before we modify/replace the ipv6
	 * header as the header could be used elsewhere
	 */
	skb = skb_unshare(skb, GFP_ATOMIC);
	if (!skb)
		return NET_XMIT_DROP;

	/* Return values from setup_header()
	 *  <0 - error, packet is dropped
	 *   0 - this is a multicast packet
	 *   1 - this is unicast packet
	 */
	err = setup_header(skb, netdev, &addr, &addr_type);
	if (err < 0) {
		kfree_skb(skb);
		return NET_XMIT_DROP;
	}

	if (err) {
		if (lowpan_cb(skb)->chan) {
			BT_DBG("xmit %s to %pMR type %d IP %pI6c chan %p",
			       netdev->name, &addr, addr_type,
			       &lowpan_cb(skb)->addr, lowpan_cb(skb)->chan);
			err = send_pkt(lowpan_cb(skb)->chan, skb, netdev);
		} else {
			err = -ENOENT;
		}
	} else {
		/* We need to send the packet to every device behind this
		 * interface.
		 */
		err = send_mcast_pkt(skb, netdev);
	}

	dev_kfree_skb(skb);

	if (err)
		BT_DBG("ERROR: xmit failed (%d)", err);

	return err < 0 ? NET_XMIT_DROP : err;
}

static int bt_dev_init(struct net_device *dev)
{
	netdev_lockdep_set_classes(dev);

	return 0;
}

static const struct net_device_ops netdev_ops = {
	.ndo_init		= bt_dev_init,
	.ndo_start_xmit		= bt_xmit,
};

static struct header_ops header_ops = {
	.create	= header_create,
};

static void netdev_setup(struct net_device *dev)
{
	dev->hard_header_len	= 0;
	dev->needed_tailroom	= 0;
	dev->flags		= IFF_RUNNING | IFF_MULTICAST;
	dev->watchdog_timeo	= 0;
	dev->tx_queue_len	= DEFAULT_TX_QUEUE_LEN;

	dev->netdev_ops		= &netdev_ops;
	dev->header_ops		= &header_ops;
	dev->needs_free_netdev	= true;
}

static struct device_type bt_type = {
	.name	= "bluetooth",
};

static void ifup(struct net_device *netdev)
{
	int err;

	rtnl_lock();
	err = dev_open(netdev);
	if (err < 0)
		BT_INFO("iface %s cannot be opened (%d)", netdev->name, err);
	rtnl_unlock();
}

static void ifdown(struct net_device *netdev)
{
	rtnl_lock();
	dev_close(netdev);
	rtnl_unlock();
}

static void do_notify_peers(struct work_struct *work)
{
	struct lowpan_btle_dev *dev = container_of(work, struct lowpan_btle_dev,
						   notify_peers.work);

	netdev_notify_peers(dev->netdev); /* send neighbour adv at startup */
}

static bool is_bt_6lowpan(struct hci_conn *hcon)
{
	if (hcon->type != LE_LINK)
		return false;

	if (!enable_6lowpan)
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
	chan->imtu = 1280;

	return chan;
}

static struct l2cap_chan *add_peer_chan(struct l2cap_chan *chan,
					struct lowpan_btle_dev *dev,
					bool new_netdev)
{
	struct lowpan_peer *peer;

	peer = kzalloc(sizeof(*peer), GFP_ATOMIC);
	if (!peer)
		return NULL;

	peer->chan = chan;
	memset(&peer->peer_addr, 0, sizeof(struct in6_addr));

	baswap((void *)peer->lladdr, &chan->dst);

	lowpan_iphc_uncompress_eui48_lladdr(&peer->peer_addr, peer->lladdr);

	spin_lock(&devices_lock);
	INIT_LIST_HEAD(&peer->list);
	peer_add(dev, peer);
	spin_unlock(&devices_lock);

	/* Notifying peers about us needs to be done without locks held */
	if (new_netdev)
		INIT_DELAYED_WORK(&dev->notify_peers, do_notify_peers);
	schedule_delayed_work(&dev->notify_peers, msecs_to_jiffies(100));

	return peer->chan;
}

static int setup_netdev(struct l2cap_chan *chan, struct lowpan_btle_dev **dev)
{
	struct net_device *netdev;
	int err = 0;

	netdev = alloc_netdev(LOWPAN_PRIV_SIZE(sizeof(struct lowpan_btle_dev)),
			      IFACE_NAME_TEMPLATE, NET_NAME_UNKNOWN,
			      netdev_setup);
	if (!netdev)
		return -ENOMEM;

	netdev->addr_assign_type = NET_ADDR_PERM;
	baswap((void *)netdev->dev_addr, &chan->src);

	netdev->netdev_ops = &netdev_ops;
	SET_NETDEV_DEV(netdev, &chan->conn->hcon->hdev->dev);
	SET_NETDEV_DEVTYPE(netdev, &bt_type);

	*dev = lowpan_btle_dev(netdev);
	(*dev)->netdev = netdev;
	(*dev)->hdev = chan->conn->hcon->hdev;
	INIT_LIST_HEAD(&(*dev)->peers);

	spin_lock(&devices_lock);
	INIT_LIST_HEAD(&(*dev)->list);
	list_add_rcu(&(*dev)->list, &bt_6lowpan_devices);
	spin_unlock(&devices_lock);

	err = lowpan_register_netdev(netdev, LOWPAN_LLTYPE_BTLE);
	if (err < 0) {
		BT_INFO("register_netdev failed %d", err);
		spin_lock(&devices_lock);
		list_del_rcu(&(*dev)->list);
		spin_unlock(&devices_lock);
		free_netdev(netdev);
		goto out;
	}

	BT_DBG("ifindex %d peer bdaddr %pMR type %d my addr %pMR type %d",
	       netdev->ifindex, &chan->dst, chan->dst_type,
	       &chan->src, chan->src_type);
	set_bit(__LINK_STATE_PRESENT, &netdev->state);

	return 0;

out:
	return err;
}

static inline void chan_ready_cb(struct l2cap_chan *chan)
{
	struct lowpan_btle_dev *dev;
	bool new_netdev = false;

	dev = lookup_dev(chan->conn);

	BT_DBG("chan %p conn %p dev %p", chan, chan->conn, dev);

	if (!dev) {
		if (setup_netdev(chan, &dev) < 0) {
			l2cap_chan_del(chan, -ENOENT);
			return;
		}
		new_netdev = true;
	}

	if (!try_module_get(THIS_MODULE))
		return;

	add_peer_chan(chan, dev, new_netdev);
	ifup(dev->netdev);
}

static inline struct l2cap_chan *chan_new_conn_cb(struct l2cap_chan *pchan)
{
	struct l2cap_chan *chan;

	chan = chan_create();
	if (!chan)
		return NULL;

	chan->ops = pchan->ops;

	BT_DBG("chan %p pchan %p", chan, pchan);

	return chan;
}

static void delete_netdev(struct work_struct *work)
{
	struct lowpan_btle_dev *entry = container_of(work,
						     struct lowpan_btle_dev,
						     delete_netdev);

	lowpan_unregister_netdev(entry->netdev);

	/* The entry pointer is deleted by the netdev destructor. */
}

static void chan_close_cb(struct l2cap_chan *chan)
{
	struct lowpan_btle_dev *entry;
	struct lowpan_btle_dev *dev = NULL;
	struct lowpan_peer *peer;
	int err = -ENOENT;
	bool last = false, remove = true;

	BT_DBG("chan %p conn %p", chan, chan->conn);

	if (chan->conn && chan->conn->hcon) {
		if (!is_bt_6lowpan(chan->conn->hcon))
			return;

		/* If conn is set, then the netdev is also there and we should
		 * not remove it.
		 */
		remove = false;
	}

	spin_lock(&devices_lock);

	list_for_each_entry_rcu(entry, &bt_6lowpan_devices, list) {
		dev = lowpan_btle_dev(entry->netdev);
		peer = __peer_lookup_chan(dev, chan);
		if (peer) {
			last = peer_del(dev, peer);
			err = 0;

			BT_DBG("dev %p removing %speer %p", dev,
			       last ? "last " : "1 ", peer);
			BT_DBG("chan %p orig refcnt %d", chan,
			       kref_read(&chan->kref));

			l2cap_chan_put(chan);
			break;
		}
	}

	if (!err && last && dev && !atomic_read(&dev->peer_count)) {
		spin_unlock(&devices_lock);

		cancel_delayed_work_sync(&dev->notify_peers);

		ifdown(dev->netdev);

		if (remove) {
			INIT_WORK(&entry->delete_netdev, delete_netdev);
			schedule_work(&entry->delete_netdev);
		}
	} else {
		spin_unlock(&devices_lock);
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
	struct lowpan_btle_dev *dev;

	BT_DBG("chan %p suspend", chan);

	dev = lookup_dev(chan->conn);
	if (!dev || !dev->netdev)
		return;

	netif_stop_queue(dev->netdev);
}

static void chan_resume_cb(struct l2cap_chan *chan)
{
	struct lowpan_btle_dev *dev;

	BT_DBG("chan %p resume", chan);

	dev = lookup_dev(chan->conn);
	if (!dev || !dev->netdev)
		return;

	netif_wake_queue(dev->netdev);
}

static long chan_get_sndtimeo_cb(struct l2cap_chan *chan)
{
	return L2CAP_CONN_TIMEOUT;
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

static int bt_6lowpan_connect(bdaddr_t *addr, u8 dst_type)
{
	struct l2cap_chan *chan;
	int err;

	chan = chan_create();
	if (!chan)
		return -EINVAL;

	chan->ops = &bt_6lowpan_chan_ops;

	err = l2cap_chan_connect(chan, cpu_to_le16(L2CAP_PSM_IPSP), 0,
				 addr, dst_type);

	BT_DBG("chan %p err %d", chan, err);
	if (err < 0)
		l2cap_chan_put(chan);

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
	struct l2cap_chan *chan;
	int err;

	if (!enable_6lowpan)
		return NULL;

	chan = chan_create();
	if (!chan)
		return NULL;

	chan->ops = &bt_6lowpan_chan_ops;
	chan->state = BT_LISTEN;
	chan->src_type = BDADDR_LE_PUBLIC;

	atomic_set(&chan->nesting, L2CAP_NESTING_PARENT);

	BT_DBG("chan %p src type %d", chan, chan->src_type);

	err = l2cap_add_psm(chan, addr, cpu_to_le16(L2CAP_PSM_IPSP));
	if (err) {
		l2cap_chan_put(chan);
		BT_ERR("psm cannot be added err %d", err);
		return NULL;
	}

	return chan;
}

static int get_l2cap_conn(char *buf, bdaddr_t *addr, u8 *addr_type,
			  struct l2cap_conn **conn)
{
	struct hci_conn *hcon;
	struct hci_dev *hdev;
	int n;

	n = sscanf(buf, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx %hhu",
		   &addr->b[5], &addr->b[4], &addr->b[3],
		   &addr->b[2], &addr->b[1], &addr->b[0],
		   addr_type);

	if (n < 7)
		return -EINVAL;

	/* The LE_PUBLIC address type is ignored because of BDADDR_ANY */
	hdev = hci_get_route(addr, BDADDR_ANY, BDADDR_LE_PUBLIC);
	if (!hdev)
		return -ENOENT;

	hci_dev_lock(hdev);
	hcon = hci_conn_hash_lookup_le(hdev, addr, *addr_type);
	hci_dev_unlock(hdev);

	if (!hcon)
		return -ENOENT;

	*conn = (struct l2cap_conn *)hcon->l2cap_data;

	BT_DBG("conn %p dst %pMR type %d", *conn, &hcon->dst, hcon->dst_type);

	return 0;
}

static void disconnect_all_peers(void)
{
	struct lowpan_btle_dev *entry;
	struct lowpan_peer *peer, *tmp_peer, *new_peer;
	struct list_head peers;

	INIT_LIST_HEAD(&peers);

	/* We make a separate list of peers as the close_cb() will
	 * modify the device peers list so it is better not to mess
	 * with the same list at the same time.
	 */

	rcu_read_lock();

	list_for_each_entry_rcu(entry, &bt_6lowpan_devices, list) {
		list_for_each_entry_rcu(peer, &entry->peers, list) {
			new_peer = kmalloc(sizeof(*new_peer), GFP_ATOMIC);
			if (!new_peer)
				break;

			new_peer->chan = peer->chan;
			INIT_LIST_HEAD(&new_peer->list);

			list_add(&new_peer->list, &peers);
		}
	}

	rcu_read_unlock();

	spin_lock(&devices_lock);
	list_for_each_entry_safe(peer, tmp_peer, &peers, list) {
		l2cap_chan_close(peer->chan, ENOENT);

		list_del_rcu(&peer->list);
		kfree_rcu(peer, rcu);
	}
	spin_unlock(&devices_lock);
}

struct set_enable {
	struct work_struct work;
	bool flag;
};

static void do_enable_set(struct work_struct *work)
{
	struct set_enable *set_enable = container_of(work,
						     struct set_enable, work);

	if (!set_enable->flag || enable_6lowpan != set_enable->flag)
		/* Disconnect existing connections if 6lowpan is
		 * disabled
		 */
		disconnect_all_peers();

	enable_6lowpan = set_enable->flag;

	if (listen_chan) {
		l2cap_chan_close(listen_chan, 0);
		l2cap_chan_put(listen_chan);
	}

	listen_chan = bt_6lowpan_listen();

	kfree(set_enable);
}

static int lowpan_enable_set(void *data, u64 val)
{
	struct set_enable *set_enable;

	set_enable = kzalloc(sizeof(*set_enable), GFP_KERNEL);
	if (!set_enable)
		return -ENOMEM;

	set_enable->flag = !!val;
	INIT_WORK(&set_enable->work, do_enable_set);

	schedule_work(&set_enable->work);

	return 0;
}

static int lowpan_enable_get(void *data, u64 *val)
{
	*val = enable_6lowpan;
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(lowpan_enable_fops, lowpan_enable_get,
			lowpan_enable_set, "%llu\n");

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
	struct lowpan_btle_dev *entry;
	struct lowpan_peer *peer;

	spin_lock(&devices_lock);

	list_for_each_entry(entry, &bt_6lowpan_devices, list) {
		list_for_each_entry(peer, &entry->peers, list)
			seq_printf(f, "%pMR (type %u)\n",
				   &peer->chan->dst, peer->chan->dst_type);
	}

	spin_unlock(&devices_lock);

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

static void disconnect_devices(void)
{
	struct lowpan_btle_dev *entry, *tmp, *new_dev;
	struct list_head devices;

	INIT_LIST_HEAD(&devices);

	/* We make a separate list of devices because the unregister_netdev()
	 * will call device_event() which will also want to modify the same
	 * devices list.
	 */

	rcu_read_lock();

	list_for_each_entry_rcu(entry, &bt_6lowpan_devices, list) {
		new_dev = kmalloc(sizeof(*new_dev), GFP_ATOMIC);
		if (!new_dev)
			break;

		new_dev->netdev = entry->netdev;
		INIT_LIST_HEAD(&new_dev->list);

		list_add_rcu(&new_dev->list, &devices);
	}

	rcu_read_unlock();

	list_for_each_entry_safe(entry, tmp, &devices, list) {
		ifdown(entry->netdev);
		BT_DBG("Unregistering netdev %s %p",
		       entry->netdev->name, entry->netdev);
		lowpan_unregister_netdev(entry->netdev);
		kfree(entry);
	}
}

static int device_event(struct notifier_block *unused,
			unsigned long event, void *ptr)
{
	struct net_device *netdev = netdev_notifier_info_to_dev(ptr);
	struct lowpan_btle_dev *entry;

	if (netdev->type != ARPHRD_6LOWPAN)
		return NOTIFY_DONE;

	switch (event) {
	case NETDEV_UNREGISTER:
		spin_lock(&devices_lock);
		list_for_each_entry(entry, &bt_6lowpan_devices, list) {
			if (entry->netdev == netdev) {
				BT_DBG("Unregistered netdev %s %p",
				       netdev->name, netdev);
				list_del(&entry->list);
				break;
			}
		}
		spin_unlock(&devices_lock);
		break;
	}

	return NOTIFY_DONE;
}

static struct notifier_block bt_6lowpan_dev_notifier = {
	.notifier_call = device_event,
};

static int __init bt_6lowpan_init(void)
{
	lowpan_enable_debugfs = debugfs_create_file("6lowpan_enable", 0644,
						    bt_debugfs, NULL,
						    &lowpan_enable_fops);
	lowpan_control_debugfs = debugfs_create_file("6lowpan_control", 0644,
						     bt_debugfs, NULL,
						     &lowpan_control_fops);

	return register_netdevice_notifier(&bt_6lowpan_dev_notifier);
}

static void __exit bt_6lowpan_exit(void)
{
	debugfs_remove(lowpan_enable_debugfs);
	debugfs_remove(lowpan_control_debugfs);

	if (listen_chan) {
		l2cap_chan_close(listen_chan, 0);
		l2cap_chan_put(listen_chan);
	}

	disconnect_devices();

	unregister_netdevice_notifier(&bt_6lowpan_dev_notifier);
}

module_init(bt_6lowpan_init);
module_exit(bt_6lowpan_exit);

MODULE_AUTHOR("Jukka Rissanen <jukka.rissanen@linux.intel.com>");
MODULE_DESCRIPTION("Bluetooth 6LoWPAN");
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");
