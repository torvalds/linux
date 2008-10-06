/*
 * File: datagram.c
 *
 * Datagram (ISI) Phonet sockets
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
#include <linux/socket.h>
#include <asm/ioctls.h>
#include <net/sock.h>

#include <linux/phonet.h>
#include <net/phonet/phonet.h>

static int pn_backlog_rcv(struct sock *sk, struct sk_buff *skb);

/* associated socket ceases to exist */
static void pn_sock_close(struct sock *sk, long timeout)
{
	sk_common_release(sk);
}

static int pn_ioctl(struct sock *sk, int cmd, unsigned long arg)
{
	struct sk_buff *skb;
	int answ;

	switch (cmd) {
	case SIOCINQ:
		lock_sock(sk);
		skb = skb_peek(&sk->sk_receive_queue);
		answ = skb ? skb->len : 0;
		release_sock(sk);
		return put_user(answ, (int __user *)arg);
	}

	return -ENOIOCTLCMD;
}

/* Destroy socket. All references are gone. */
static void pn_destruct(struct sock *sk)
{
	skb_queue_purge(&sk->sk_receive_queue);
}

static int pn_init(struct sock *sk)
{
	sk->sk_destruct = pn_destruct;
	return 0;
}

static int pn_sendmsg(struct kiocb *iocb, struct sock *sk,
			struct msghdr *msg, size_t len)
{
	struct sockaddr_pn *target;
	struct sk_buff *skb;
	int err;

	if (msg->msg_flags & MSG_OOB)
		return -EOPNOTSUPP;

	if (msg->msg_name == NULL)
		return -EDESTADDRREQ;

	if (msg->msg_namelen < sizeof(struct sockaddr_pn))
		return -EINVAL;

	target = (struct sockaddr_pn *)msg->msg_name;
	if (target->spn_family != AF_PHONET)
		return -EAFNOSUPPORT;

	skb = sock_alloc_send_skb(sk, MAX_PHONET_HEADER + len,
					msg->msg_flags & MSG_DONTWAIT, &err);
	if (skb == NULL)
		return err;
	skb_reserve(skb, MAX_PHONET_HEADER);

	err = memcpy_fromiovec((void *)skb_put(skb, len), msg->msg_iov, len);
	if (err < 0) {
		kfree_skb(skb);
		return err;
	}

	/*
	 * Fill in the Phonet header and
	 * finally pass the packet forwards.
	 */
	err = pn_skb_send(sk, skb, target);

	/* If ok, return len. */
	return (err >= 0) ? len : err;
}

static int pn_recvmsg(struct kiocb *iocb, struct sock *sk,
			struct msghdr *msg, size_t len, int noblock,
			int flags, int *addr_len)
{
	struct sk_buff *skb = NULL;
	struct sockaddr_pn sa;
	int rval = -EOPNOTSUPP;
	int copylen;

	if (flags & MSG_OOB)
		goto out_nofree;

	if (addr_len)
		*addr_len = sizeof(sa);

	skb = skb_recv_datagram(sk, flags, noblock, &rval);
	if (skb == NULL)
		goto out_nofree;

	pn_skb_get_src_sockaddr(skb, &sa);

	copylen = skb->len;
	if (len < copylen) {
		msg->msg_flags |= MSG_TRUNC;
		copylen = len;
	}

	rval = skb_copy_datagram_iovec(skb, 0, msg->msg_iov, copylen);
	if (rval) {
		rval = -EFAULT;
		goto out;
	}

	rval = (flags & MSG_TRUNC) ? skb->len : copylen;

	if (msg->msg_name != NULL)
		memcpy(msg->msg_name, &sa, sizeof(struct sockaddr_pn));

out:
	skb_free_datagram(sk, skb);

out_nofree:
	return rval;
}

/* Queue an skb for a sock. */
static int pn_backlog_rcv(struct sock *sk, struct sk_buff *skb)
{
	int err = sock_queue_rcv_skb(sk, skb);
	if (err < 0)
		kfree_skb(skb);
	return err ? NET_RX_DROP : NET_RX_SUCCESS;
}

/* Module registration */
static struct proto pn_proto = {
	.close		= pn_sock_close,
	.ioctl		= pn_ioctl,
	.init		= pn_init,
	.sendmsg	= pn_sendmsg,
	.recvmsg	= pn_recvmsg,
	.backlog_rcv	= pn_backlog_rcv,
	.hash		= pn_sock_hash,
	.unhash		= pn_sock_unhash,
	.get_port	= pn_sock_get_port,
	.obj_size	= sizeof(struct pn_sock),
	.owner		= THIS_MODULE,
	.name		= "PHONET",
};

static struct phonet_protocol pn_dgram_proto = {
	.ops		= &phonet_dgram_ops,
	.prot		= &pn_proto,
	.sock_type	= SOCK_DGRAM,
};

int __init isi_register(void)
{
	return phonet_proto_register(PN_PROTO_PHONET, &pn_dgram_proto);
}

void __exit isi_unregister(void)
{
	phonet_proto_unregister(PN_PROTO_PHONET, &pn_dgram_proto);
}
