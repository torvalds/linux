/*
 * Raw IEEE 802.15.4 sockets
 *
 * Copyright 2007, 2008 Siemens AG
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
 * Sergey Lapin <slapin@ossfans.org>
 * Dmitry Eremin-Solenikov <dbaryshkov@gmail.com>
 */

#include <linux/net.h>
#include <linux/module.h>
#include <linux/if_arp.h>
#include <linux/list.h>
#include <net/sock.h>
#include <net/ieee802154/af_ieee802154.h>

#include "af802154.h"

static HLIST_HEAD(raw_head);
static DEFINE_RWLOCK(raw_lock);

static void raw_hash(struct sock *sk)
{
	write_lock_bh(&raw_lock);
	sk_add_node(sk, &raw_head);
	sock_prot_inuse_add(sock_net(sk), sk->sk_prot, 1);
	write_unlock_bh(&raw_lock);
}

static void raw_unhash(struct sock *sk)
{
	write_lock_bh(&raw_lock);
	if (sk_del_node_init(sk))
		sock_prot_inuse_add(sock_net(sk), sk->sk_prot, -1);
	write_unlock_bh(&raw_lock);
}

static void raw_close(struct sock *sk, long timeout)
{
	sk_common_release(sk);
}

static int raw_bind(struct sock *sk, struct sockaddr *uaddr, int len)
{
	struct sockaddr_ieee802154 *addr = (struct sockaddr_ieee802154 *)uaddr;
	int err = 0;
	struct net_device *dev = NULL;

	if (len < sizeof(*addr))
		return -EINVAL;

	if (addr->family != AF_IEEE802154)
		return -EINVAL;

	lock_sock(sk);

	dev = ieee802154_get_dev(sock_net(sk), &addr->addr);
	if (!dev) {
		err = -ENODEV;
		goto out;
	}

	if (dev->type != ARPHRD_IEEE802154_PHY &&
	    dev->type != ARPHRD_IEEE802154) {
		err = -ENODEV;
		goto out_put;
	}

	sk->sk_bound_dev_if = dev->ifindex;
	sk_dst_reset(sk);

out_put:
	dev_put(dev);
out:
	release_sock(sk);

	return err;
}

static int raw_connect(struct sock *sk, struct sockaddr *uaddr,
			int addr_len)
{
	return -ENOTSUPP;
}

static int raw_disconnect(struct sock *sk, int flags)
{
	return 0;
}

static int raw_sendmsg(struct kiocb *iocb, struct sock *sk, struct msghdr *msg,
		       size_t size)
{
	struct net_device *dev;
	unsigned mtu;
	struct sk_buff *skb;
	int err;

	if (msg->msg_flags & MSG_OOB) {
		pr_debug("msg->msg_flags = 0x%x\n", msg->msg_flags);
		return -EOPNOTSUPP;
	}

	lock_sock(sk);
	if (!sk->sk_bound_dev_if)
		dev = dev_getfirstbyhwtype(sock_net(sk), ARPHRD_IEEE802154);
	else
		dev = dev_get_by_index(sock_net(sk), sk->sk_bound_dev_if);
	release_sock(sk);

	if (!dev) {
		pr_debug("no dev\n");
		err = -ENXIO;
		goto out;
	}

	mtu = dev->mtu;
	pr_debug("name = %s, mtu = %u\n", dev->name, mtu);

	if (size > mtu) {
		pr_debug("size = %Zu, mtu = %u\n", size, mtu);
		err = -EINVAL;
		goto out_dev;
	}

	skb = sock_alloc_send_skb(sk, LL_ALLOCATED_SPACE(dev) + size,
			msg->msg_flags & MSG_DONTWAIT, &err);
	if (!skb)
		goto out_dev;

	skb_reserve(skb, LL_RESERVED_SPACE(dev));

	skb_reset_mac_header(skb);
	skb_reset_network_header(skb);

	err = memcpy_fromiovec(skb_put(skb, size), msg->msg_iov, size);
	if (err < 0)
		goto out_skb;

	skb->dev = dev;
	skb->sk  = sk;
	skb->protocol = htons(ETH_P_IEEE802154);

	dev_put(dev);

	err = dev_queue_xmit(skb);
	if (err > 0)
		err = net_xmit_errno(err);

	return err ?: size;

out_skb:
	kfree_skb(skb);
out_dev:
	dev_put(dev);
out:
	return err;
}

static int raw_recvmsg(struct kiocb *iocb, struct sock *sk, struct msghdr *msg,
		       size_t len, int noblock, int flags, int *addr_len)
{
	size_t copied = 0;
	int err = -EOPNOTSUPP;
	struct sk_buff *skb;

	skb = skb_recv_datagram(sk, flags, noblock, &err);
	if (!skb)
		goto out;

	copied = skb->len;
	if (len < copied) {
		msg->msg_flags |= MSG_TRUNC;
		copied = len;
	}

	err = skb_copy_datagram_iovec(skb, 0, msg->msg_iov, copied);
	if (err)
		goto done;

	sock_recv_timestamp(msg, sk, skb);

	if (flags & MSG_TRUNC)
		copied = skb->len;
done:
	skb_free_datagram(sk, skb);
out:
	if (err)
		return err;
	return copied;
}

static int raw_rcv_skb(struct sock *sk, struct sk_buff *skb)
{
	if (sock_queue_rcv_skb(sk, skb) < 0) {
		atomic_inc(&sk->sk_drops);
		kfree_skb(skb);
		return NET_RX_DROP;
	}

	return NET_RX_SUCCESS;
}


void ieee802154_raw_deliver(struct net_device *dev, struct sk_buff *skb)
{
	struct sock *sk;
	struct hlist_node *node;

	read_lock(&raw_lock);
	sk_for_each(sk, node, &raw_head) {
		bh_lock_sock(sk);
		if (!sk->sk_bound_dev_if ||
		    sk->sk_bound_dev_if == dev->ifindex) {

			struct sk_buff *clone;

			clone = skb_clone(skb, GFP_ATOMIC);
			if (clone)
				raw_rcv_skb(sk, clone);
		}
		bh_unlock_sock(sk);
	}
	read_unlock(&raw_lock);
}

struct proto ieee802154_raw_prot = {
	.name		= "IEEE-802.15.4-RAW",
	.owner		= THIS_MODULE,
	.obj_size	= sizeof(struct sock),
	.close		= raw_close,
	.bind		= raw_bind,
	.sendmsg	= raw_sendmsg,
	.recvmsg	= raw_recvmsg,
	.hash		= raw_hash,
	.unhash		= raw_unhash,
	.connect	= raw_connect,
	.disconnect	= raw_disconnect,
};

