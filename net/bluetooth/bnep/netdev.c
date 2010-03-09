/*
   BNEP implementation for Linux Bluetooth stack (BlueZ).
   Copyright (C) 2001-2002 Inventel Systemes
   Written 2001-2002 by
	Clément Moreau <clement.moreau@inventel.fr>
	David Libault  <david.libault@inventel.fr>

   Copyright (C) 2002 Maxim Krasnyansky <maxk@qualcomm.com>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2 as
   published by the Free Software Foundation;

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY RIGHTS.
   IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) AND AUTHOR(S) BE LIABLE FOR ANY
   CLAIM, OR ANY SPECIAL INDIRECT OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES
   WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
   ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
   OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

   ALL LIABILITY, INCLUDING LIABILITY FOR INFRINGEMENT OF ANY PATENTS,
   COPYRIGHTS, TRADEMARKS OR OTHER RIGHTS, RELATING TO USE OF THIS
   SOFTWARE IS DISCLAIMED.
*/

#include <linux/module.h>

#include <linux/socket.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/wait.h>

#include <asm/unaligned.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include <net/bluetooth/l2cap.h>

#include "bnep.h"

#define BNEP_TX_QUEUE_LEN 20

static int bnep_net_open(struct net_device *dev)
{
	netif_start_queue(dev);
	return 0;
}

static int bnep_net_close(struct net_device *dev)
{
	netif_stop_queue(dev);
	return 0;
}

static void bnep_net_set_mc_list(struct net_device *dev)
{
#ifdef CONFIG_BT_BNEP_MC_FILTER
	struct bnep_session *s = netdev_priv(dev);
	struct sock *sk = s->sock->sk;
	struct bnep_set_filter_req *r;
	struct sk_buff *skb;
	int size;

	BT_DBG("%s mc_count %d", dev->name, netdev_mc_count(dev));

	size = sizeof(*r) + (BNEP_MAX_MULTICAST_FILTERS + 1) * ETH_ALEN * 2;
	skb  = alloc_skb(size, GFP_ATOMIC);
	if (!skb) {
		BT_ERR("%s Multicast list allocation failed", dev->name);
		return;
	}

	r = (void *) skb->data;
	__skb_put(skb, sizeof(*r));

	r->type = BNEP_CONTROL;
	r->ctrl = BNEP_FILTER_MULTI_ADDR_SET;

	if (dev->flags & (IFF_PROMISC | IFF_ALLMULTI)) {
		u8 start[ETH_ALEN] = { 0x01 };

		/* Request all addresses */
		memcpy(__skb_put(skb, ETH_ALEN), start, ETH_ALEN);
		memcpy(__skb_put(skb, ETH_ALEN), dev->broadcast, ETH_ALEN);
		r->len = htons(ETH_ALEN * 2);
	} else {
		struct dev_mc_list *dmi = dev->mc_list;
		int i, len = skb->len;

		if (dev->flags & IFF_BROADCAST) {
			memcpy(__skb_put(skb, ETH_ALEN), dev->broadcast, ETH_ALEN);
			memcpy(__skb_put(skb, ETH_ALEN), dev->broadcast, ETH_ALEN);
		}

		/* FIXME: We should group addresses here. */

		for (i = 0;
		     i < netdev_mc_count(dev) && i < BNEP_MAX_MULTICAST_FILTERS;
		     i++) {
			memcpy(__skb_put(skb, ETH_ALEN), dmi->dmi_addr, ETH_ALEN);
			memcpy(__skb_put(skb, ETH_ALEN), dmi->dmi_addr, ETH_ALEN);
			dmi = dmi->next;
		}
		r->len = htons(skb->len - len);
	}

	skb_queue_tail(&sk->sk_write_queue, skb);
	wake_up_interruptible(sk->sk_sleep);
#endif
}

static int bnep_net_set_mac_addr(struct net_device *dev, void *arg)
{
	BT_DBG("%s", dev->name);
	return 0;
}

static void bnep_net_timeout(struct net_device *dev)
{
	BT_DBG("net_timeout");
	netif_wake_queue(dev);
}

#ifdef CONFIG_BT_BNEP_MC_FILTER
static inline int bnep_net_mc_filter(struct sk_buff *skb, struct bnep_session *s)
{
	struct ethhdr *eh = (void *) skb->data;

	if ((eh->h_dest[0] & 1) && !test_bit(bnep_mc_hash(eh->h_dest), (ulong *) &s->mc_filter))
		return 1;
	return 0;
}
#endif

#ifdef CONFIG_BT_BNEP_PROTO_FILTER
/* Determine ether protocol. Based on eth_type_trans. */
static inline u16 bnep_net_eth_proto(struct sk_buff *skb)
{
	struct ethhdr *eh = (void *) skb->data;
	u16 proto = ntohs(eh->h_proto);

	if (proto >= 1536)
		return proto;

	if (get_unaligned((__be16 *) skb->data) == htons(0xFFFF))
		return ETH_P_802_3;

	return ETH_P_802_2;
}

static inline int bnep_net_proto_filter(struct sk_buff *skb, struct bnep_session *s)
{
	u16 proto = bnep_net_eth_proto(skb);
	struct bnep_proto_filter *f = s->proto_filter;
	int i;

	for (i = 0; i < BNEP_MAX_PROTO_FILTERS && f[i].end; i++) {
		if (proto >= f[i].start && proto <= f[i].end)
			return 0;
	}

	BT_DBG("BNEP: filtered skb %p, proto 0x%.4x", skb, proto);
	return 1;
}
#endif

static netdev_tx_t bnep_net_xmit(struct sk_buff *skb,
				 struct net_device *dev)
{
	struct bnep_session *s = netdev_priv(dev);
	struct sock *sk = s->sock->sk;

	BT_DBG("skb %p, dev %p", skb, dev);

#ifdef CONFIG_BT_BNEP_MC_FILTER
	if (bnep_net_mc_filter(skb, s)) {
		kfree_skb(skb);
		return NETDEV_TX_OK;
	}
#endif

#ifdef CONFIG_BT_BNEP_PROTO_FILTER
	if (bnep_net_proto_filter(skb, s)) {
		kfree_skb(skb);
		return NETDEV_TX_OK;
	}
#endif

	/*
	 * We cannot send L2CAP packets from here as we are potentially in a bh.
	 * So we have to queue them and wake up session thread which is sleeping
	 * on the sk->sk_sleep.
	 */
	dev->trans_start = jiffies;
	skb_queue_tail(&sk->sk_write_queue, skb);
	wake_up_interruptible(sk->sk_sleep);

	if (skb_queue_len(&sk->sk_write_queue) >= BNEP_TX_QUEUE_LEN) {
		BT_DBG("tx queue is full");

		/* Stop queuing.
		 * Session thread will do netif_wake_queue() */
		netif_stop_queue(dev);
	}

	return NETDEV_TX_OK;
}

static const struct net_device_ops bnep_netdev_ops = {
	.ndo_open            = bnep_net_open,
	.ndo_stop            = bnep_net_close,
	.ndo_start_xmit	     = bnep_net_xmit,
	.ndo_validate_addr   = eth_validate_addr,
	.ndo_set_multicast_list = bnep_net_set_mc_list,
	.ndo_set_mac_address = bnep_net_set_mac_addr,
	.ndo_tx_timeout      = bnep_net_timeout,
	.ndo_change_mtu	     = eth_change_mtu,

};

void bnep_net_setup(struct net_device *dev)
{

	memset(dev->broadcast, 0xff, ETH_ALEN);
	dev->addr_len = ETH_ALEN;

	ether_setup(dev);
	dev->netdev_ops = &bnep_netdev_ops;

	dev->watchdog_timeo  = HZ * 2;
}
