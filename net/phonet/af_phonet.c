/*
 * File: af_phonet.c
 *
 * Phonet protocols family
 *
 * Copyright (C) 2008 Nokia Corporation.
 *
 * Contact: Remi Denis-Courmont <remi.denis-courmont@nokia.com>
 * Original author: Sakari Ailus <sakari.ailus@nokia.com>
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
#include <linux/module.h>
#include <asm/unaligned.h>
#include <net/sock.h>

#include <linux/if_phonet.h>
#include <linux/phonet.h>
#include <net/phonet/phonet.h>
#include <net/phonet/pn_dev.h>

static struct net_proto_family phonet_proto_family;
static struct phonet_protocol *phonet_proto_get(int protocol);
static inline void phonet_proto_put(struct phonet_protocol *pp);

/* protocol family functions */

static int pn_socket_create(struct net *net, struct socket *sock, int protocol)
{
	struct sock *sk;
	struct pn_sock *pn;
	struct phonet_protocol *pnp;
	int err;

	if (net != &init_net)
		return -EAFNOSUPPORT;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (protocol == 0) {
		/* Default protocol selection */
		switch (sock->type) {
		case SOCK_DGRAM:
			protocol = PN_PROTO_PHONET;
			break;
		default:
			return -EPROTONOSUPPORT;
		}
	}

	pnp = phonet_proto_get(protocol);
	if (pnp == NULL)
		return -EPROTONOSUPPORT;
	if (sock->type != pnp->sock_type) {
		err = -EPROTONOSUPPORT;
		goto out;
	}

	sk = sk_alloc(net, PF_PHONET, GFP_KERNEL, pnp->prot);
	if (sk == NULL) {
		err = -ENOMEM;
		goto out;
	}

	sock_init_data(sock, sk);
	sock->state = SS_UNCONNECTED;
	sock->ops = pnp->ops;
	sk->sk_backlog_rcv = sk->sk_prot->backlog_rcv;
	sk->sk_protocol = protocol;
	pn = pn_sk(sk);
	pn->sobject = 0;
	pn->resource = 0;
	sk->sk_prot->init(sk);
	err = 0;

out:
	phonet_proto_put(pnp);
	return err;
}

static struct net_proto_family phonet_proto_family = {
	.family = AF_PHONET,
	.create = pn_socket_create,
	.owner = THIS_MODULE,
};

/* packet type functions */

/*
 * Stuff received packets to associated sockets.
 * On error, returns non-zero and releases the skb.
 */
static int phonet_rcv(struct sk_buff *skb, struct net_device *dev,
			struct packet_type *pkttype,
			struct net_device *orig_dev)
{
	struct phonethdr *ph;
	struct sock *sk;
	struct sockaddr_pn sa;
	u16 len;

	if (dev_net(dev) != &init_net)
		goto out;

	/* check we have at least a full Phonet header */
	if (!pskb_pull(skb, sizeof(struct phonethdr)))
		goto out;

	/* check that the advertised length is correct */
	ph = pn_hdr(skb);
	len = get_unaligned_be16(&ph->pn_length);
	if (len < 2)
		goto out;
	len -= 2;
	if ((len > skb->len) || pskb_trim(skb, len))
		goto out;
	skb_reset_transport_header(skb);

	pn_skb_get_dst_sockaddr(skb, &sa);
	if (pn_sockaddr_get_addr(&sa) == 0)
		goto out; /* currently, we cannot be device 0 */

	sk = pn_find_sock_by_sa(&sa);
	if (sk == NULL)
		goto out;

	/* Push data to the socket (or other sockets connected to it). */
	return sk_receive_skb(sk, skb, 0);

out:
	kfree_skb(skb);
	return NET_RX_DROP;
}

static struct packet_type phonet_packet_type = {
	.type = __constant_htons(ETH_P_PHONET),
	.dev = NULL,
	.func = phonet_rcv,
};

/* Transport protocol registration */
static struct phonet_protocol *proto_tab[PHONET_NPROTO] __read_mostly;
static DEFINE_SPINLOCK(proto_tab_lock);

int __init_or_module phonet_proto_register(int protocol,
						struct phonet_protocol *pp)
{
	int err = 0;

	if (protocol >= PHONET_NPROTO)
		return -EINVAL;

	err = proto_register(pp->prot, 1);
	if (err)
		return err;

	spin_lock(&proto_tab_lock);
	if (proto_tab[protocol])
		err = -EBUSY;
	else
		proto_tab[protocol] = pp;
	spin_unlock(&proto_tab_lock);

	return err;
}
EXPORT_SYMBOL(phonet_proto_register);

void phonet_proto_unregister(int protocol, struct phonet_protocol *pp)
{
	spin_lock(&proto_tab_lock);
	BUG_ON(proto_tab[protocol] != pp);
	proto_tab[protocol] = NULL;
	spin_unlock(&proto_tab_lock);
	proto_unregister(pp->prot);
}
EXPORT_SYMBOL(phonet_proto_unregister);

static struct phonet_protocol *phonet_proto_get(int protocol)
{
	struct phonet_protocol *pp;

	if (protocol >= PHONET_NPROTO)
		return NULL;

	spin_lock(&proto_tab_lock);
	pp = proto_tab[protocol];
	if (pp && !try_module_get(pp->prot->owner))
		pp = NULL;
	spin_unlock(&proto_tab_lock);

	return pp;
}

static inline void phonet_proto_put(struct phonet_protocol *pp)
{
	module_put(pp->prot->owner);
}

/* Module registration */
static int __init phonet_init(void)
{
	int err;

	err = sock_register(&phonet_proto_family);
	if (err) {
		printk(KERN_ALERT
			"phonet protocol family initialization failed\n");
		return err;
	}

	phonet_device_init();
	dev_add_pack(&phonet_packet_type);
	phonet_netlink_register();
	return 0;
}

static void __exit phonet_exit(void)
{
	sock_unregister(AF_PHONET);
	dev_remove_pack(&phonet_packet_type);
	phonet_device_exit();
}

module_init(phonet_init);
module_exit(phonet_exit);
MODULE_DESCRIPTION("Phonet protocol stack for Linux");
MODULE_LICENSE("GPL");
