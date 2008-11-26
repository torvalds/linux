/*
 * File: pep-gprs.c
 *
 * GPRS over Phonet pipe end point socket
 *
 * Copyright (C) 2008 Nokia Corporation.
 *
 * Author: Rémi Denis-Courmont <remi.denis-courmont@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/if_ether.h>
#include <linux/if_arp.h>
#include <net/sock.h>

#include <linux/if_phonet.h>
#include <net/tcp_states.h>
#include <net/phonet/gprs.h>

#define GPRS_DEFAULT_MTU 1400

struct gprs_dev {
	struct sock		*sk;
	void			(*old_state_change)(struct sock *);
	void			(*old_data_ready)(struct sock *, int);
	void			(*old_write_space)(struct sock *);

	struct net_device	*net;
	struct net_device_stats	stats;

	struct sk_buff_head	tx_queue;
	struct work_struct	tx_work;
	spinlock_t		tx_lock;
	unsigned		tx_max;
};

static int gprs_type_trans(struct sk_buff *skb)
{
	const u8 *pvfc;
	u8 buf;

	pvfc = skb_header_pointer(skb, 0, 1, &buf);
	if (!pvfc)
		return 0;
	/* Look at IP version field */
	switch (*pvfc >> 4) {
	case 4:
		return htons(ETH_P_IP);
	case 6:
		return htons(ETH_P_IPV6);
	}
	return 0;
}

/*
 * Socket callbacks
 */

static void gprs_state_change(struct sock *sk)
{
	struct gprs_dev *dev = sk->sk_user_data;

	if (sk->sk_state == TCP_CLOSE_WAIT) {
		netif_stop_queue(dev->net);
		netif_carrier_off(dev->net);
	}
}

static int gprs_recv(struct gprs_dev *dev, struct sk_buff *skb)
{
	int err = 0;
	u16 protocol = gprs_type_trans(skb);

	if (!protocol) {
		err = -EINVAL;
		goto drop;
	}

	if (likely(skb_headroom(skb) & 3)) {
		struct sk_buff *rskb, *fs;
		int flen = 0;

		/* Phonet Pipe data header is misaligned (3 bytes),
		 * so wrap the IP packet as a single fragment of an head-less
		 * socket buffer. The network stack will pull what it needs,
		 * but at least, the whole IP payload is not memcpy'd. */
		rskb = netdev_alloc_skb(dev->net, 0);
		if (!rskb) {
			err = -ENOBUFS;
			goto drop;
		}
		skb_shinfo(rskb)->frag_list = skb;
		rskb->len += skb->len;
		rskb->data_len += rskb->len;
		rskb->truesize += rskb->len;

		/* Avoid nested fragments */
		for (fs = skb_shinfo(skb)->frag_list; fs; fs = fs->next)
			flen += fs->len;
		skb->next = skb_shinfo(skb)->frag_list;
		skb_shinfo(skb)->frag_list = NULL;
		skb->len -= flen;
		skb->data_len -= flen;
		skb->truesize -= flen;

		skb = rskb;
	}

	skb->protocol = protocol;
	skb_reset_mac_header(skb);
	skb->dev = dev->net;

	if (likely(dev->net->flags & IFF_UP)) {
		dev->stats.rx_packets++;
		dev->stats.rx_bytes += skb->len;
		netif_rx(skb);
		skb = NULL;
	} else
		err = -ENODEV;

drop:
	if (skb) {
		dev_kfree_skb(skb);
		dev->stats.rx_dropped++;
	}
	return err;
}

static void gprs_data_ready(struct sock *sk, int len)
{
	struct gprs_dev *dev = sk->sk_user_data;
	struct sk_buff *skb;

	while ((skb = pep_read(sk)) != NULL) {
		skb_orphan(skb);
		gprs_recv(dev, skb);
	}
}

static void gprs_write_space(struct sock *sk)
{
	struct gprs_dev *dev = sk->sk_user_data;
	unsigned credits = pep_writeable(sk);

	spin_lock_bh(&dev->tx_lock);
	dev->tx_max = credits;
	if (credits > skb_queue_len(&dev->tx_queue))
		netif_wake_queue(dev->net);
	spin_unlock_bh(&dev->tx_lock);
}

/*
 * Network device callbacks
 */

static int gprs_xmit(struct sk_buff *skb, struct net_device *net)
{
	struct gprs_dev *dev = netdev_priv(net);

	switch (skb->protocol) {
	case  htons(ETH_P_IP):
	case  htons(ETH_P_IPV6):
		break;
	default:
		dev_kfree_skb(skb);
		return 0;
	}

	spin_lock(&dev->tx_lock);
	if (likely(skb_queue_len(&dev->tx_queue) < dev->tx_max)) {
		skb_queue_tail(&dev->tx_queue, skb);
		skb = NULL;
	}
	if (skb_queue_len(&dev->tx_queue) >= dev->tx_max)
		netif_stop_queue(net);
	spin_unlock(&dev->tx_lock);

	schedule_work(&dev->tx_work);
	if (unlikely(skb))
		dev_kfree_skb(skb);
	return 0;
}

static void gprs_tx(struct work_struct *work)
{
	struct gprs_dev *dev = container_of(work, struct gprs_dev, tx_work);
	struct sock *sk = dev->sk;
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&dev->tx_queue)) != NULL) {
		int err;

		dev->stats.tx_bytes += skb->len;
		dev->stats.tx_packets++;

		skb_orphan(skb);
		skb_set_owner_w(skb, sk);

		lock_sock(sk);
		err = pep_write(sk, skb);
		if (err) {
			LIMIT_NETDEBUG(KERN_WARNING"%s: TX error (%d)\n",
					dev->net->name, err);
			dev->stats.tx_aborted_errors++;
			dev->stats.tx_errors++;
		}
		release_sock(sk);
	}

	lock_sock(sk);
	gprs_write_space(sk);
	release_sock(sk);
}

static int gprs_set_mtu(struct net_device *net, int new_mtu)
{
	if ((new_mtu < 576) || (new_mtu > (PHONET_MAX_MTU - 11)))
		return -EINVAL;

	net->mtu = new_mtu;
	return 0;
}

static struct net_device_stats *gprs_get_stats(struct net_device *net)
{
	struct gprs_dev *dev = netdev_priv(net);

	return &dev->stats;
}

static void gprs_setup(struct net_device *net)
{
	net->features		= NETIF_F_FRAGLIST;
	net->type		= ARPHRD_NONE;
	net->flags		= IFF_POINTOPOINT | IFF_NOARP;
	net->mtu		= GPRS_DEFAULT_MTU;
	net->hard_header_len	= 0;
	net->addr_len		= 0;
	net->tx_queue_len	= 10;

	net->destructor		= free_netdev;
	net->hard_start_xmit	= gprs_xmit; /* mandatory */
	net->change_mtu		= gprs_set_mtu;
	net->get_stats		= gprs_get_stats;
}

/*
 * External interface
 */

/*
 * Attach a GPRS interface to a datagram socket.
 * Returns the interface index on success, negative error code on error.
 */
int gprs_attach(struct sock *sk)
{
	static const char ifname[] = "gprs%d";
	struct gprs_dev *dev;
	struct net_device *net;
	int err;

	if (unlikely(sk->sk_type == SOCK_STREAM))
		return -EINVAL; /* need packet boundaries */

	/* Create net device */
	net = alloc_netdev(sizeof(*dev), ifname, gprs_setup);
	if (!net)
		return -ENOMEM;
	dev = netdev_priv(net);
	dev->net = net;
	dev->tx_max = 0;
	spin_lock_init(&dev->tx_lock);
	skb_queue_head_init(&dev->tx_queue);
	INIT_WORK(&dev->tx_work, gprs_tx);

	netif_stop_queue(net);
	err = register_netdev(net);
	if (err) {
		free_netdev(net);
		return err;
	}

	lock_sock(sk);
	if (unlikely(sk->sk_user_data)) {
		err = -EBUSY;
		goto out_rel;
	}
	if (unlikely((1 << sk->sk_state & (TCPF_CLOSE|TCPF_LISTEN)) ||
			sock_flag(sk, SOCK_DEAD))) {
		err = -EINVAL;
		goto out_rel;
	}
	sk->sk_user_data	= dev;
	dev->old_state_change	= sk->sk_state_change;
	dev->old_data_ready	= sk->sk_data_ready;
	dev->old_write_space	= sk->sk_write_space;
	sk->sk_state_change	= gprs_state_change;
	sk->sk_data_ready	= gprs_data_ready;
	sk->sk_write_space	= gprs_write_space;
	release_sock(sk);

	sock_hold(sk);
	dev->sk = sk;

	printk(KERN_DEBUG"%s: attached\n", net->name);
	gprs_write_space(sk); /* kick off TX */
	return net->ifindex;

out_rel:
	release_sock(sk);
	unregister_netdev(net);
	return err;
}

void gprs_detach(struct sock *sk)
{
	struct gprs_dev *dev = sk->sk_user_data;
	struct net_device *net = dev->net;

	lock_sock(sk);
	sk->sk_user_data	= NULL;
	sk->sk_state_change	= dev->old_state_change;
	sk->sk_data_ready	= dev->old_data_ready;
	sk->sk_write_space	= dev->old_write_space;
	release_sock(sk);

	printk(KERN_DEBUG"%s: detached\n", net->name);
	unregister_netdev(net);
	flush_scheduled_work();
	sock_put(sk);
	skb_queue_purge(&dev->tx_queue);
}
