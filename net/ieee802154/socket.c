/*
 * IEEE802154.4 socket interface
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
 * Written by:
 * Sergey Lapin <slapin@ossfans.org>
 * Maxim Gorbachyov <maxim.gorbachev@siemens.com>
 */

#include <linux/net.h>
#include <linux/capability.h>
#include <linux/module.h>
#include <linux/if_arp.h>
#include <linux/if.h>
#include <linux/termios.h>	/* For TIOCOUTQ/INQ */
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/socket.h>
#include <net/datalink.h>
#include <net/psnap.h>
#include <net/sock.h>
#include <net/tcp_states.h>
#include <net/route.h>

#include <net/af_ieee802154.h>
#include <net/ieee802154_netdev.h>

/* Utility function for families */
static struct net_device*
ieee802154_get_dev(struct net *net, const struct ieee802154_addr *addr)
{
	struct net_device *dev = NULL;
	struct net_device *tmp;
	__le16 pan_id, short_addr;
	u8 hwaddr[IEEE802154_ADDR_LEN];

	switch (addr->mode) {
	case IEEE802154_ADDR_LONG:
		ieee802154_devaddr_to_raw(hwaddr, addr->extended_addr);
		rcu_read_lock();
		dev = dev_getbyhwaddr_rcu(net, ARPHRD_IEEE802154, hwaddr);
		if (dev)
			dev_hold(dev);
		rcu_read_unlock();
		break;
	case IEEE802154_ADDR_SHORT:
		if (addr->pan_id == cpu_to_le16(IEEE802154_PANID_BROADCAST) ||
		    addr->short_addr == cpu_to_le16(IEEE802154_ADDR_UNDEF) ||
		    addr->short_addr == cpu_to_le16(IEEE802154_ADDR_BROADCAST))
			break;

		rtnl_lock();

		for_each_netdev(net, tmp) {
			if (tmp->type != ARPHRD_IEEE802154)
				continue;

			pan_id = tmp->ieee802154_ptr->pan_id;
			short_addr = tmp->ieee802154_ptr->short_addr;
			if (pan_id == addr->pan_id &&
			    short_addr == addr->short_addr) {
				dev = tmp;
				dev_hold(dev);
				break;
			}
		}

		rtnl_unlock();
		break;
	default:
		pr_warn("Unsupported ieee802154 address type: %d\n",
			addr->mode);
		break;
	}

	return dev;
}

static int ieee802154_sock_release(struct socket *sock)
{
	struct sock *sk = sock->sk;

	if (sk) {
		sock->sk = NULL;
		sk->sk_prot->close(sk, 0);
	}
	return 0;
}

static int ieee802154_sock_sendmsg(struct socket *sock, struct msghdr *msg,
				   size_t len)
{
	struct sock *sk = sock->sk;

	return sk->sk_prot->sendmsg(sk, msg, len);
}

static int ieee802154_sock_bind(struct socket *sock, struct sockaddr *uaddr,
				int addr_len)
{
	struct sock *sk = sock->sk;

	if (sk->sk_prot->bind)
		return sk->sk_prot->bind(sk, uaddr, addr_len);

	return sock_no_bind(sock, uaddr, addr_len);
}

static int ieee802154_sock_connect(struct socket *sock, struct sockaddr *uaddr,
				   int addr_len, int flags)
{
	struct sock *sk = sock->sk;

	if (addr_len < sizeof(uaddr->sa_family))
		return -EINVAL;

	if (uaddr->sa_family == AF_UNSPEC)
		return sk->sk_prot->disconnect(sk, flags);

	return sk->sk_prot->connect(sk, uaddr, addr_len);
}

static int ieee802154_dev_ioctl(struct sock *sk, struct ifreq __user *arg,
				unsigned int cmd)
{
	struct ifreq ifr;
	int ret = -ENOIOCTLCMD;
	struct net_device *dev;

	if (copy_from_user(&ifr, arg, sizeof(struct ifreq)))
		return -EFAULT;

	ifr.ifr_name[IFNAMSIZ-1] = 0;

	dev_load(sock_net(sk), ifr.ifr_name);
	dev = dev_get_by_name(sock_net(sk), ifr.ifr_name);

	if (!dev)
		return -ENODEV;

	if (dev->type == ARPHRD_IEEE802154 && dev->netdev_ops->ndo_do_ioctl)
		ret = dev->netdev_ops->ndo_do_ioctl(dev, &ifr, cmd);

	if (!ret && copy_to_user(arg, &ifr, sizeof(struct ifreq)))
		ret = -EFAULT;
	dev_put(dev);

	return ret;
}

static int ieee802154_sock_ioctl(struct socket *sock, unsigned int cmd,
				 unsigned long arg)
{
	struct sock *sk = sock->sk;

	switch (cmd) {
	case SIOCGIFADDR:
	case SIOCSIFADDR:
		return ieee802154_dev_ioctl(sk, (struct ifreq __user *)arg,
				cmd);
	default:
		if (!sk->sk_prot->ioctl)
			return -ENOIOCTLCMD;
		return sk->sk_prot->ioctl(sk, cmd, arg);
	}
}

/* RAW Sockets (802.15.4 created in userspace) */
static HLIST_HEAD(raw_head);
static DEFINE_RWLOCK(raw_lock);

static int raw_hash(struct sock *sk)
{
	write_lock_bh(&raw_lock);
	sk_add_node(sk, &raw_head);
	sock_prot_inuse_add(sock_net(sk), sk->sk_prot, 1);
	write_unlock_bh(&raw_lock);

	return 0;
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

static int raw_bind(struct sock *sk, struct sockaddr *_uaddr, int len)
{
	struct ieee802154_addr addr;
	struct sockaddr_ieee802154 *uaddr = (struct sockaddr_ieee802154 *)_uaddr;
	int err = 0;
	struct net_device *dev = NULL;

	if (len < sizeof(*uaddr))
		return -EINVAL;

	uaddr = (struct sockaddr_ieee802154 *)_uaddr;
	if (uaddr->family != AF_IEEE802154)
		return -EINVAL;

	lock_sock(sk);

	ieee802154_addr_from_sa(&addr, &uaddr->addr);
	dev = ieee802154_get_dev(sock_net(sk), &addr);
	if (!dev) {
		err = -ENODEV;
		goto out;
	}

	sk->sk_bound_dev_if = dev->ifindex;
	sk_dst_reset(sk);

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

static int raw_sendmsg(struct sock *sk, struct msghdr *msg, size_t size)
{
	struct net_device *dev;
	unsigned int mtu;
	struct sk_buff *skb;
	int hlen, tlen;
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

	mtu = IEEE802154_MTU;
	pr_debug("name = %s, mtu = %u\n", dev->name, mtu);

	if (size > mtu) {
		pr_debug("size = %zu, mtu = %u\n", size, mtu);
		err = -EMSGSIZE;
		goto out_dev;
	}

	hlen = LL_RESERVED_SPACE(dev);
	tlen = dev->needed_tailroom;
	skb = sock_alloc_send_skb(sk, hlen + tlen + size,
				  msg->msg_flags & MSG_DONTWAIT, &err);
	if (!skb)
		goto out_dev;

	skb_reserve(skb, hlen);

	skb_reset_mac_header(skb);
	skb_reset_network_header(skb);

	err = memcpy_from_msg(skb_put(skb, size), msg, size);
	if (err < 0)
		goto out_skb;

	skb->dev = dev;
	skb->protocol = htons(ETH_P_IEEE802154);

	err = dev_queue_xmit(skb);
	if (err > 0)
		err = net_xmit_errno(err);

	dev_put(dev);

	return err ?: size;

out_skb:
	kfree_skb(skb);
out_dev:
	dev_put(dev);
out:
	return err;
}

static int raw_recvmsg(struct sock *sk, struct msghdr *msg, size_t len,
		       int noblock, int flags, int *addr_len)
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

	err = skb_copy_datagram_msg(skb, 0, msg, copied);
	if (err)
		goto done;

	sock_recv_ts_and_drops(msg, sk, skb);

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
	skb = skb_share_check(skb, GFP_ATOMIC);
	if (!skb)
		return NET_RX_DROP;

	if (sock_queue_rcv_skb(sk, skb) < 0) {
		kfree_skb(skb);
		return NET_RX_DROP;
	}

	return NET_RX_SUCCESS;
}

static void ieee802154_raw_deliver(struct net_device *dev, struct sk_buff *skb)
{
	struct sock *sk;

	read_lock(&raw_lock);
	sk_for_each(sk, &raw_head) {
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

static int raw_getsockopt(struct sock *sk, int level, int optname,
			  char __user *optval, int __user *optlen)
{
	return -EOPNOTSUPP;
}

static int raw_setsockopt(struct sock *sk, int level, int optname,
			  char __user *optval, unsigned int optlen)
{
	return -EOPNOTSUPP;
}

static struct proto ieee802154_raw_prot = {
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
	.getsockopt	= raw_getsockopt,
	.setsockopt	= raw_setsockopt,
};

static const struct proto_ops ieee802154_raw_ops = {
	.family		   = PF_IEEE802154,
	.owner		   = THIS_MODULE,
	.release	   = ieee802154_sock_release,
	.bind		   = ieee802154_sock_bind,
	.connect	   = ieee802154_sock_connect,
	.socketpair	   = sock_no_socketpair,
	.accept		   = sock_no_accept,
	.getname	   = sock_no_getname,
	.poll		   = datagram_poll,
	.ioctl		   = ieee802154_sock_ioctl,
	.gettstamp	   = sock_gettstamp,
	.listen		   = sock_no_listen,
	.shutdown	   = sock_no_shutdown,
	.setsockopt	   = sock_common_setsockopt,
	.getsockopt	   = sock_common_getsockopt,
	.sendmsg	   = ieee802154_sock_sendmsg,
	.recvmsg	   = sock_common_recvmsg,
	.mmap		   = sock_no_mmap,
	.sendpage	   = sock_no_sendpage,
#ifdef CONFIG_COMPAT
	.compat_setsockopt = compat_sock_common_setsockopt,
	.compat_getsockopt = compat_sock_common_getsockopt,
#endif
};

/* DGRAM Sockets (802.15.4 dataframes) */
static HLIST_HEAD(dgram_head);
static DEFINE_RWLOCK(dgram_lock);

struct dgram_sock {
	struct sock sk;

	struct ieee802154_addr src_addr;
	struct ieee802154_addr dst_addr;

	unsigned int bound:1;
	unsigned int connected:1;
	unsigned int want_ack:1;
	unsigned int want_lqi:1;
	unsigned int secen:1;
	unsigned int secen_override:1;
	unsigned int seclevel:3;
	unsigned int seclevel_override:1;
};

static inline struct dgram_sock *dgram_sk(const struct sock *sk)
{
	return container_of(sk, struct dgram_sock, sk);
}

static int dgram_hash(struct sock *sk)
{
	write_lock_bh(&dgram_lock);
	sk_add_node(sk, &dgram_head);
	sock_prot_inuse_add(sock_net(sk), sk->sk_prot, 1);
	write_unlock_bh(&dgram_lock);

	return 0;
}

static void dgram_unhash(struct sock *sk)
{
	write_lock_bh(&dgram_lock);
	if (sk_del_node_init(sk))
		sock_prot_inuse_add(sock_net(sk), sk->sk_prot, -1);
	write_unlock_bh(&dgram_lock);
}

static int dgram_init(struct sock *sk)
{
	struct dgram_sock *ro = dgram_sk(sk);

	ro->want_ack = 1;
	ro->want_lqi = 0;
	return 0;
}

static void dgram_close(struct sock *sk, long timeout)
{
	sk_common_release(sk);
}

static int dgram_bind(struct sock *sk, struct sockaddr *uaddr, int len)
{
	struct sockaddr_ieee802154 *addr = (struct sockaddr_ieee802154 *)uaddr;
	struct ieee802154_addr haddr;
	struct dgram_sock *ro = dgram_sk(sk);
	int err = -EINVAL;
	struct net_device *dev;

	lock_sock(sk);

	ro->bound = 0;

	if (len < sizeof(*addr))
		goto out;

	if (addr->family != AF_IEEE802154)
		goto out;

	ieee802154_addr_from_sa(&haddr, &addr->addr);
	dev = ieee802154_get_dev(sock_net(sk), &haddr);
	if (!dev) {
		err = -ENODEV;
		goto out;
	}

	if (dev->type != ARPHRD_IEEE802154) {
		err = -ENODEV;
		goto out_put;
	}

	ro->src_addr = haddr;

	ro->bound = 1;
	err = 0;
out_put:
	dev_put(dev);
out:
	release_sock(sk);

	return err;
}

static int dgram_ioctl(struct sock *sk, int cmd, unsigned long arg)
{
	switch (cmd) {
	case SIOCOUTQ:
	{
		int amount = sk_wmem_alloc_get(sk);

		return put_user(amount, (int __user *)arg);
	}

	case SIOCINQ:
	{
		struct sk_buff *skb;
		unsigned long amount;

		amount = 0;
		spin_lock_bh(&sk->sk_receive_queue.lock);
		skb = skb_peek(&sk->sk_receive_queue);
		if (skb) {
			/* We will only return the amount
			 * of this packet since that is all
			 * that will be read.
			 */
			amount = skb->len - ieee802154_hdr_length(skb);
		}
		spin_unlock_bh(&sk->sk_receive_queue.lock);
		return put_user(amount, (int __user *)arg);
	}
	}

	return -ENOIOCTLCMD;
}

/* FIXME: autobind */
static int dgram_connect(struct sock *sk, struct sockaddr *uaddr,
			 int len)
{
	struct sockaddr_ieee802154 *addr = (struct sockaddr_ieee802154 *)uaddr;
	struct dgram_sock *ro = dgram_sk(sk);
	int err = 0;

	if (len < sizeof(*addr))
		return -EINVAL;

	if (addr->family != AF_IEEE802154)
		return -EINVAL;

	lock_sock(sk);

	if (!ro->bound) {
		err = -ENETUNREACH;
		goto out;
	}

	ieee802154_addr_from_sa(&ro->dst_addr, &addr->addr);
	ro->connected = 1;

out:
	release_sock(sk);
	return err;
}

static int dgram_disconnect(struct sock *sk, int flags)
{
	struct dgram_sock *ro = dgram_sk(sk);

	lock_sock(sk);
	ro->connected = 0;
	release_sock(sk);

	return 0;
}

static int dgram_sendmsg(struct sock *sk, struct msghdr *msg, size_t size)
{
	struct net_device *dev;
	unsigned int mtu;
	struct sk_buff *skb;
	struct ieee802154_mac_cb *cb;
	struct dgram_sock *ro = dgram_sk(sk);
	struct ieee802154_addr dst_addr;
	int hlen, tlen;
	int err;

	if (msg->msg_flags & MSG_OOB) {
		pr_debug("msg->msg_flags = 0x%x\n", msg->msg_flags);
		return -EOPNOTSUPP;
	}

	if (!ro->connected && !msg->msg_name)
		return -EDESTADDRREQ;
	else if (ro->connected && msg->msg_name)
		return -EISCONN;

	if (!ro->bound)
		dev = dev_getfirstbyhwtype(sock_net(sk), ARPHRD_IEEE802154);
	else
		dev = ieee802154_get_dev(sock_net(sk), &ro->src_addr);

	if (!dev) {
		pr_debug("no dev\n");
		err = -ENXIO;
		goto out;
	}
	mtu = IEEE802154_MTU;
	pr_debug("name = %s, mtu = %u\n", dev->name, mtu);

	if (size > mtu) {
		pr_debug("size = %zu, mtu = %u\n", size, mtu);
		err = -EMSGSIZE;
		goto out_dev;
	}

	hlen = LL_RESERVED_SPACE(dev);
	tlen = dev->needed_tailroom;
	skb = sock_alloc_send_skb(sk, hlen + tlen + size,
				  msg->msg_flags & MSG_DONTWAIT,
				  &err);
	if (!skb)
		goto out_dev;

	skb_reserve(skb, hlen);

	skb_reset_network_header(skb);

	cb = mac_cb_init(skb);
	cb->type = IEEE802154_FC_TYPE_DATA;
	cb->ackreq = ro->want_ack;

	if (msg->msg_name) {
		DECLARE_SOCKADDR(struct sockaddr_ieee802154*,
				 daddr, msg->msg_name);

		ieee802154_addr_from_sa(&dst_addr, &daddr->addr);
	} else {
		dst_addr = ro->dst_addr;
	}

	cb->secen = ro->secen;
	cb->secen_override = ro->secen_override;
	cb->seclevel = ro->seclevel;
	cb->seclevel_override = ro->seclevel_override;

	err = wpan_dev_hard_header(skb, dev, &dst_addr,
				   ro->bound ? &ro->src_addr : NULL, size);
	if (err < 0)
		goto out_skb;

	err = memcpy_from_msg(skb_put(skb, size), msg, size);
	if (err < 0)
		goto out_skb;

	skb->dev = dev;
	skb->protocol = htons(ETH_P_IEEE802154);

	err = dev_queue_xmit(skb);
	if (err > 0)
		err = net_xmit_errno(err);

	dev_put(dev);

	return err ?: size;

out_skb:
	kfree_skb(skb);
out_dev:
	dev_put(dev);
out:
	return err;
}

static int dgram_recvmsg(struct sock *sk, struct msghdr *msg, size_t len,
			 int noblock, int flags, int *addr_len)
{
	size_t copied = 0;
	int err = -EOPNOTSUPP;
	struct sk_buff *skb;
	struct dgram_sock *ro = dgram_sk(sk);
	DECLARE_SOCKADDR(struct sockaddr_ieee802154 *, saddr, msg->msg_name);

	skb = skb_recv_datagram(sk, flags, noblock, &err);
	if (!skb)
		goto out;

	copied = skb->len;
	if (len < copied) {
		msg->msg_flags |= MSG_TRUNC;
		copied = len;
	}

	/* FIXME: skip headers if necessary ?! */
	err = skb_copy_datagram_msg(skb, 0, msg, copied);
	if (err)
		goto done;

	sock_recv_ts_and_drops(msg, sk, skb);

	if (saddr) {
		/* Clear the implicit padding in struct sockaddr_ieee802154
		 * (16 bits between 'family' and 'addr') and in struct
		 * ieee802154_addr_sa (16 bits at the end of the structure).
		 */
		memset(saddr, 0, sizeof(*saddr));

		saddr->family = AF_IEEE802154;
		ieee802154_addr_to_sa(&saddr->addr, &mac_cb(skb)->source);
		*addr_len = sizeof(*saddr);
	}

	if (ro->want_lqi) {
		err = put_cmsg(msg, SOL_IEEE802154, WPAN_WANTLQI,
			       sizeof(uint8_t), &(mac_cb(skb)->lqi));
		if (err)
			goto done;
	}

	if (flags & MSG_TRUNC)
		copied = skb->len;
done:
	skb_free_datagram(sk, skb);
out:
	if (err)
		return err;
	return copied;
}

static int dgram_rcv_skb(struct sock *sk, struct sk_buff *skb)
{
	skb = skb_share_check(skb, GFP_ATOMIC);
	if (!skb)
		return NET_RX_DROP;

	if (sock_queue_rcv_skb(sk, skb) < 0) {
		kfree_skb(skb);
		return NET_RX_DROP;
	}

	return NET_RX_SUCCESS;
}

static inline bool
ieee802154_match_sock(__le64 hw_addr, __le16 pan_id, __le16 short_addr,
		      struct dgram_sock *ro)
{
	if (!ro->bound)
		return true;

	if (ro->src_addr.mode == IEEE802154_ADDR_LONG &&
	    hw_addr == ro->src_addr.extended_addr)
		return true;

	if (ro->src_addr.mode == IEEE802154_ADDR_SHORT &&
	    pan_id == ro->src_addr.pan_id &&
	    short_addr == ro->src_addr.short_addr)
		return true;

	return false;
}

static int ieee802154_dgram_deliver(struct net_device *dev, struct sk_buff *skb)
{
	struct sock *sk, *prev = NULL;
	int ret = NET_RX_SUCCESS;
	__le16 pan_id, short_addr;
	__le64 hw_addr;

	/* Data frame processing */
	BUG_ON(dev->type != ARPHRD_IEEE802154);

	pan_id = dev->ieee802154_ptr->pan_id;
	short_addr = dev->ieee802154_ptr->short_addr;
	hw_addr = dev->ieee802154_ptr->extended_addr;

	read_lock(&dgram_lock);
	sk_for_each(sk, &dgram_head) {
		if (ieee802154_match_sock(hw_addr, pan_id, short_addr,
					  dgram_sk(sk))) {
			if (prev) {
				struct sk_buff *clone;

				clone = skb_clone(skb, GFP_ATOMIC);
				if (clone)
					dgram_rcv_skb(prev, clone);
			}

			prev = sk;
		}
	}

	if (prev) {
		dgram_rcv_skb(prev, skb);
	} else {
		kfree_skb(skb);
		ret = NET_RX_DROP;
	}
	read_unlock(&dgram_lock);

	return ret;
}

static int dgram_getsockopt(struct sock *sk, int level, int optname,
			    char __user *optval, int __user *optlen)
{
	struct dgram_sock *ro = dgram_sk(sk);

	int val, len;

	if (level != SOL_IEEE802154)
		return -EOPNOTSUPP;

	if (get_user(len, optlen))
		return -EFAULT;

	len = min_t(unsigned int, len, sizeof(int));

	switch (optname) {
	case WPAN_WANTACK:
		val = ro->want_ack;
		break;
	case WPAN_WANTLQI:
		val = ro->want_lqi;
		break;
	case WPAN_SECURITY:
		if (!ro->secen_override)
			val = WPAN_SECURITY_DEFAULT;
		else if (ro->secen)
			val = WPAN_SECURITY_ON;
		else
			val = WPAN_SECURITY_OFF;
		break;
	case WPAN_SECURITY_LEVEL:
		if (!ro->seclevel_override)
			val = WPAN_SECURITY_LEVEL_DEFAULT;
		else
			val = ro->seclevel;
		break;
	default:
		return -ENOPROTOOPT;
	}

	if (put_user(len, optlen))
		return -EFAULT;
	if (copy_to_user(optval, &val, len))
		return -EFAULT;
	return 0;
}

static int dgram_setsockopt(struct sock *sk, int level, int optname,
			    char __user *optval, unsigned int optlen)
{
	struct dgram_sock *ro = dgram_sk(sk);
	struct net *net = sock_net(sk);
	int val;
	int err = 0;

	if (optlen < sizeof(int))
		return -EINVAL;

	if (get_user(val, (int __user *)optval))
		return -EFAULT;

	lock_sock(sk);

	switch (optname) {
	case WPAN_WANTACK:
		ro->want_ack = !!val;
		break;
	case WPAN_WANTLQI:
		ro->want_lqi = !!val;
		break;
	case WPAN_SECURITY:
		if (!ns_capable(net->user_ns, CAP_NET_ADMIN) &&
		    !ns_capable(net->user_ns, CAP_NET_RAW)) {
			err = -EPERM;
			break;
		}

		switch (val) {
		case WPAN_SECURITY_DEFAULT:
			ro->secen_override = 0;
			break;
		case WPAN_SECURITY_ON:
			ro->secen_override = 1;
			ro->secen = 1;
			break;
		case WPAN_SECURITY_OFF:
			ro->secen_override = 1;
			ro->secen = 0;
			break;
		default:
			err = -EINVAL;
			break;
		}
		break;
	case WPAN_SECURITY_LEVEL:
		if (!ns_capable(net->user_ns, CAP_NET_ADMIN) &&
		    !ns_capable(net->user_ns, CAP_NET_RAW)) {
			err = -EPERM;
			break;
		}

		if (val < WPAN_SECURITY_LEVEL_DEFAULT ||
		    val > IEEE802154_SCF_SECLEVEL_ENC_MIC128) {
			err = -EINVAL;
		} else if (val == WPAN_SECURITY_LEVEL_DEFAULT) {
			ro->seclevel_override = 0;
		} else {
			ro->seclevel_override = 1;
			ro->seclevel = val;
		}
		break;
	default:
		err = -ENOPROTOOPT;
		break;
	}

	release_sock(sk);
	return err;
}

static struct proto ieee802154_dgram_prot = {
	.name		= "IEEE-802.15.4-MAC",
	.owner		= THIS_MODULE,
	.obj_size	= sizeof(struct dgram_sock),
	.init		= dgram_init,
	.close		= dgram_close,
	.bind		= dgram_bind,
	.sendmsg	= dgram_sendmsg,
	.recvmsg	= dgram_recvmsg,
	.hash		= dgram_hash,
	.unhash		= dgram_unhash,
	.connect	= dgram_connect,
	.disconnect	= dgram_disconnect,
	.ioctl		= dgram_ioctl,
	.getsockopt	= dgram_getsockopt,
	.setsockopt	= dgram_setsockopt,
};

static const struct proto_ops ieee802154_dgram_ops = {
	.family		   = PF_IEEE802154,
	.owner		   = THIS_MODULE,
	.release	   = ieee802154_sock_release,
	.bind		   = ieee802154_sock_bind,
	.connect	   = ieee802154_sock_connect,
	.socketpair	   = sock_no_socketpair,
	.accept		   = sock_no_accept,
	.getname	   = sock_no_getname,
	.poll		   = datagram_poll,
	.ioctl		   = ieee802154_sock_ioctl,
	.gettstamp	   = sock_gettstamp,
	.listen		   = sock_no_listen,
	.shutdown	   = sock_no_shutdown,
	.setsockopt	   = sock_common_setsockopt,
	.getsockopt	   = sock_common_getsockopt,
	.sendmsg	   = ieee802154_sock_sendmsg,
	.recvmsg	   = sock_common_recvmsg,
	.mmap		   = sock_no_mmap,
	.sendpage	   = sock_no_sendpage,
#ifdef CONFIG_COMPAT
	.compat_setsockopt = compat_sock_common_setsockopt,
	.compat_getsockopt = compat_sock_common_getsockopt,
#endif
};

/* Create a socket. Initialise the socket, blank the addresses
 * set the state.
 */
static int ieee802154_create(struct net *net, struct socket *sock,
			     int protocol, int kern)
{
	struct sock *sk;
	int rc;
	struct proto *proto;
	const struct proto_ops *ops;

	if (!net_eq(net, &init_net))
		return -EAFNOSUPPORT;

	switch (sock->type) {
	case SOCK_RAW:
		proto = &ieee802154_raw_prot;
		ops = &ieee802154_raw_ops;
		break;
	case SOCK_DGRAM:
		proto = &ieee802154_dgram_prot;
		ops = &ieee802154_dgram_ops;
		break;
	default:
		rc = -ESOCKTNOSUPPORT;
		goto out;
	}

	rc = -ENOMEM;
	sk = sk_alloc(net, PF_IEEE802154, GFP_KERNEL, proto, kern);
	if (!sk)
		goto out;
	rc = 0;

	sock->ops = ops;

	sock_init_data(sock, sk);
	/* FIXME: sk->sk_destruct */
	sk->sk_family = PF_IEEE802154;

	/* Checksums on by default */
	sock_set_flag(sk, SOCK_ZAPPED);

	if (sk->sk_prot->hash) {
		rc = sk->sk_prot->hash(sk);
		if (rc) {
			sk_common_release(sk);
			goto out;
		}
	}

	if (sk->sk_prot->init) {
		rc = sk->sk_prot->init(sk);
		if (rc)
			sk_common_release(sk);
	}
out:
	return rc;
}

static const struct net_proto_family ieee802154_family_ops = {
	.family		= PF_IEEE802154,
	.create		= ieee802154_create,
	.owner		= THIS_MODULE,
};

static int ieee802154_rcv(struct sk_buff *skb, struct net_device *dev,
			  struct packet_type *pt, struct net_device *orig_dev)
{
	if (!netif_running(dev))
		goto drop;
	pr_debug("got frame, type %d, dev %p\n", dev->type, dev);
#ifdef DEBUG
	print_hex_dump_bytes("ieee802154_rcv ",
			     DUMP_PREFIX_NONE, skb->data, skb->len);
#endif

	if (!net_eq(dev_net(dev), &init_net))
		goto drop;

	ieee802154_raw_deliver(dev, skb);

	if (dev->type != ARPHRD_IEEE802154)
		goto drop;

	if (skb->pkt_type != PACKET_OTHERHOST)
		return ieee802154_dgram_deliver(dev, skb);

drop:
	kfree_skb(skb);
	return NET_RX_DROP;
}

static struct packet_type ieee802154_packet_type = {
	.type = htons(ETH_P_IEEE802154),
	.func = ieee802154_rcv,
};

static int __init af_ieee802154_init(void)
{
	int rc = -EINVAL;

	rc = proto_register(&ieee802154_raw_prot, 1);
	if (rc)
		goto out;

	rc = proto_register(&ieee802154_dgram_prot, 1);
	if (rc)
		goto err_dgram;

	/* Tell SOCKET that we are alive */
	rc = sock_register(&ieee802154_family_ops);
	if (rc)
		goto err_sock;
	dev_add_pack(&ieee802154_packet_type);

	rc = 0;
	goto out;

err_sock:
	proto_unregister(&ieee802154_dgram_prot);
err_dgram:
	proto_unregister(&ieee802154_raw_prot);
out:
	return rc;
}

static void __exit af_ieee802154_remove(void)
{
	dev_remove_pack(&ieee802154_packet_type);
	sock_unregister(PF_IEEE802154);
	proto_unregister(&ieee802154_dgram_prot);
	proto_unregister(&ieee802154_raw_prot);
}

module_init(af_ieee802154_init);
module_exit(af_ieee802154_remove);

MODULE_LICENSE("GPL");
MODULE_ALIAS_NETPROTO(PF_IEEE802154);
