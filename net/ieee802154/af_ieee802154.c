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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
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
#include <net/datalink.h>
#include <net/psnap.h>
#include <net/sock.h>
#include <net/tcp_states.h>
#include <net/route.h>

#include <net/af_ieee802154.h>
#include <net/ieee802154_netdev.h>

#include "af802154.h"

/*
 * Utility function for families
 */
struct net_device *ieee802154_get_dev(struct net *net,
		struct ieee802154_addr *addr)
{
	struct net_device *dev = NULL;
	struct net_device *tmp;
	u16 pan_id, short_addr;

	switch (addr->addr_type) {
	case IEEE802154_ADDR_LONG:
		rtnl_lock();
		dev = dev_getbyhwaddr(net, ARPHRD_IEEE802154, addr->hwaddr);
		if (dev)
			dev_hold(dev);
		rtnl_unlock();
		break;
	case IEEE802154_ADDR_SHORT:
		if (addr->pan_id == 0xffff ||
		    addr->short_addr == IEEE802154_ADDR_UNDEF ||
		    addr->short_addr == 0xffff)
			break;

		rtnl_lock();

		for_each_netdev(net, tmp) {
			if (tmp->type != ARPHRD_IEEE802154)
				continue;

			pan_id = ieee802154_mlme_ops(tmp)->get_pan_id(tmp);
			short_addr =
				ieee802154_mlme_ops(tmp)->get_short_addr(tmp);

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
		pr_warning("Unsupported ieee802154 address type: %d\n",
				addr->addr_type);
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
static int ieee802154_sock_sendmsg(struct kiocb *iocb, struct socket *sock,
		struct msghdr *msg, size_t len)
{
	struct sock *sk = sock->sk;

	return sk->sk_prot->sendmsg(iocb, sk, msg, len);
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
	case SIOCGSTAMP:
		return sock_get_timestamp(sk, (struct timeval __user *)arg);
	case SIOCGSTAMPNS:
		return sock_get_timestampns(sk, (struct timespec __user *)arg);
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


/*
 * Create a socket. Initialise the socket, blank the addresses
 * set the state.
 */
static int ieee802154_create(struct net *net, struct socket *sock,
		int protocol)
{
	struct sock *sk;
	int rc;
	struct proto *proto;
	const struct proto_ops *ops;

	if (net != &init_net)
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
	sk = sk_alloc(net, PF_IEEE802154, GFP_KERNEL, proto);
	if (!sk)
		goto out;
	rc = 0;

	sock->ops = ops;

	sock_init_data(sock, sk);
	/* FIXME: sk->sk_destruct */
	sk->sk_family = PF_IEEE802154;

	/* Checksums on by default */
	sock_set_flag(sk, SOCK_ZAPPED);

	if (sk->sk_prot->hash)
		sk->sk_prot->hash(sk);

	if (sk->sk_prot->init) {
		rc = sk->sk_prot->init(sk);
		if (rc)
			sk_common_release(sk);
	}
out:
	return rc;
}

static struct net_proto_family ieee802154_family_ops = {
	.family		= PF_IEEE802154,
	.create		= ieee802154_create,
	.owner		= THIS_MODULE,
};

static int ieee802154_rcv(struct sk_buff *skb, struct net_device *dev,
	struct packet_type *pt, struct net_device *orig_dev)
{
	if (!netif_running(dev))
		return -ENODEV;
	pr_debug("got frame, type %d, dev %p\n", dev->type, dev);
#ifdef DEBUG
	print_hex_dump_bytes("ieee802154_rcv ", DUMP_PREFIX_NONE, skb->data, skb->len);
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
	.type = __constant_htons(ETH_P_IEEE802154),
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
