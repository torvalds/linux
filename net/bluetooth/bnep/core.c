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

/*
 * $Id: core.c,v 1.20 2002/08/04 21:23:58 maxk Exp $
 */ 

#include <linux/config.h>
#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/init.h>
#include <linux/wait.h>
#include <linux/errno.h>
#include <linux/smp_lock.h>
#include <linux/net.h>
#include <net/sock.h>

#include <linux/socket.h>
#include <linux/file.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#include <asm/unaligned.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/l2cap.h>

#include "bnep.h"

#ifndef CONFIG_BT_BNEP_DEBUG
#undef  BT_DBG
#define BT_DBG(D...)
#endif

#define VERSION "1.2"

static LIST_HEAD(bnep_session_list);
static DECLARE_RWSEM(bnep_session_sem);

static struct bnep_session *__bnep_get_session(u8 *dst)
{
	struct bnep_session *s;
	struct list_head *p;

	BT_DBG("");

	list_for_each(p, &bnep_session_list) {
		s = list_entry(p, struct bnep_session, list);	
		if (!memcmp(dst, s->eh.h_source, ETH_ALEN))
			return s;
	}
	return NULL;
}

static void __bnep_link_session(struct bnep_session *s)
{
	/* It's safe to call __module_get() here because sessions are added
	   by the socket layer which has to hold the refference to this module.
	 */
	__module_get(THIS_MODULE);
	list_add(&s->list, &bnep_session_list);	
}

static void __bnep_unlink_session(struct bnep_session *s)
{
	list_del(&s->list);
	module_put(THIS_MODULE);
}

static int bnep_send(struct bnep_session *s, void *data, size_t len)
{
	struct socket *sock = s->sock;
	struct kvec iv = { data, len };

	return kernel_sendmsg(sock, &s->msg, &iv, 1, len);
}

static int bnep_send_rsp(struct bnep_session *s, u8 ctrl, u16 resp)
{
	struct bnep_control_rsp rsp;
	rsp.type = BNEP_CONTROL;
	rsp.ctrl = ctrl;
	rsp.resp = htons(resp);
	return bnep_send(s, &rsp, sizeof(rsp));
}

#ifdef CONFIG_BT_BNEP_PROTO_FILTER
static inline void bnep_set_default_proto_filter(struct bnep_session *s)
{
	/* (IPv4, ARP)  */
	s->proto_filter[0].start = htons(0x0800);
	s->proto_filter[0].end   = htons(0x0806);
	/* (RARP, AppleTalk) */
	s->proto_filter[1].start = htons(0x8035);
	s->proto_filter[1].end   = htons(0x80F3);
	/* (IPX, IPv6) */
	s->proto_filter[2].start = htons(0x8137);
	s->proto_filter[2].end   = htons(0x86DD);
}
#endif

static int bnep_ctrl_set_netfilter(struct bnep_session *s, u16 *data, int len)
{
	int n;

	if (len < 2)
		return -EILSEQ;

	n = ntohs(get_unaligned(data));
	data++; len -= 2;

	if (len < n)
		return -EILSEQ;

	BT_DBG("filter len %d", n);

#ifdef CONFIG_BT_BNEP_PROTO_FILTER
	n /= 4;
	if (n <= BNEP_MAX_PROTO_FILTERS) {
		struct bnep_proto_filter *f = s->proto_filter;
		int i;

		for (i = 0; i < n; i++) {
			f[i].start = get_unaligned(data++);
			f[i].end   = get_unaligned(data++);

			BT_DBG("proto filter start %d end %d",
				f[i].start, f[i].end);
		}

		if (i < BNEP_MAX_PROTO_FILTERS)
			memset(f + i, 0, sizeof(*f));

		if (n == 0)
			bnep_set_default_proto_filter(s);

		bnep_send_rsp(s, BNEP_FILTER_NET_TYPE_RSP, BNEP_SUCCESS);
	} else {
		bnep_send_rsp(s, BNEP_FILTER_NET_TYPE_RSP, BNEP_FILTER_LIMIT_REACHED);
	}
#else
	bnep_send_rsp(s, BNEP_FILTER_NET_TYPE_RSP, BNEP_FILTER_UNSUPPORTED_REQ);
#endif
	return 0;
}

static int bnep_ctrl_set_mcfilter(struct bnep_session *s, u8 *data, int len)
{
	int n;

	if (len < 2)
		return -EILSEQ;

	n = ntohs(get_unaligned((u16 *) data)); 
	data += 2; len -= 2;

	if (len < n)
		return -EILSEQ;

	BT_DBG("filter len %d", n);

#ifdef CONFIG_BT_BNEP_MC_FILTER
	n /= (ETH_ALEN * 2);

	if (n > 0) {
		s->mc_filter = 0;

		/* Always send broadcast */
		set_bit(bnep_mc_hash(s->dev->broadcast), (ulong *) &s->mc_filter);

		/* Add address ranges to the multicast hash */
		for (; n > 0; n--) {
			u8 a1[6], *a2;

			memcpy(a1, data, ETH_ALEN); data += ETH_ALEN;
			a2 = data; data += ETH_ALEN;
	
			BT_DBG("mc filter %s -> %s",
				batostr((void *) a1), batostr((void *) a2));

			#define INCA(a) { int i = 5; while (i >=0 && ++a[i--] == 0); }

			/* Iterate from a1 to a2 */
			set_bit(bnep_mc_hash(a1), (ulong *) &s->mc_filter);
			while (memcmp(a1, a2, 6) < 0 && s->mc_filter != ~0LL) {
				INCA(a1);
				set_bit(bnep_mc_hash(a1), (ulong *) &s->mc_filter);
			}
		}
	}

	BT_DBG("mc filter hash 0x%llx", s->mc_filter);

	bnep_send_rsp(s, BNEP_FILTER_MULTI_ADDR_RSP, BNEP_SUCCESS);
#else
	bnep_send_rsp(s, BNEP_FILTER_MULTI_ADDR_RSP, BNEP_FILTER_UNSUPPORTED_REQ);
#endif
	return 0;
}

static int bnep_rx_control(struct bnep_session *s, void *data, int len)
{
	u8  cmd = *(u8 *)data;
	int err = 0;

	data++; len--;

	switch (cmd) {
	case BNEP_CMD_NOT_UNDERSTOOD:
	case BNEP_SETUP_CONN_REQ:
	case BNEP_SETUP_CONN_RSP:
	case BNEP_FILTER_NET_TYPE_RSP:
	case BNEP_FILTER_MULTI_ADDR_RSP:
		/* Ignore these for now */
		break;

	case BNEP_FILTER_NET_TYPE_SET:
		err = bnep_ctrl_set_netfilter(s, data, len);
		break;

	case BNEP_FILTER_MULTI_ADDR_SET:
		err = bnep_ctrl_set_mcfilter(s, data, len);
		break;

	default: {
			u8 pkt[3];
			pkt[0] = BNEP_CONTROL;
			pkt[1] = BNEP_CMD_NOT_UNDERSTOOD;
			pkt[2] = cmd;
			bnep_send(s, pkt, sizeof(pkt));
		}
		break;
	}

	return err;
}

static int bnep_rx_extension(struct bnep_session *s, struct sk_buff *skb)
{
	struct bnep_ext_hdr *h;
	int err = 0;

	do {
		h = (void *) skb->data;
		if (!skb_pull(skb, sizeof(*h))) {
			err = -EILSEQ;
			break;
		}

		BT_DBG("type 0x%x len %d", h->type, h->len);
	
		switch (h->type & BNEP_TYPE_MASK) {
		case BNEP_EXT_CONTROL:
			bnep_rx_control(s, skb->data, skb->len);
			break;

		default:
			/* Unknown extension, skip it. */
			break;
		}

		if (!skb_pull(skb, h->len)) {
			err = -EILSEQ;
			break;
		}
	} while (!err && (h->type & BNEP_EXT_HEADER));
	
	return err;
}

static u8 __bnep_rx_hlen[] = {
	ETH_HLEN,     /* BNEP_GENERAL */
	0,            /* BNEP_CONTROL */
	2,            /* BNEP_COMPRESSED */
	ETH_ALEN + 2, /* BNEP_COMPRESSED_SRC_ONLY */
	ETH_ALEN + 2  /* BNEP_COMPRESSED_DST_ONLY */
};
#define BNEP_RX_TYPES	(sizeof(__bnep_rx_hlen) - 1)

static inline int bnep_rx_frame(struct bnep_session *s, struct sk_buff *skb)
{
	struct net_device *dev = s->dev;
	struct sk_buff *nskb;
	u8 type;

	dev->last_rx = jiffies;
	s->stats.rx_bytes += skb->len;

	type = *(u8 *) skb->data; skb_pull(skb, 1);

	if ((type & BNEP_TYPE_MASK) > BNEP_RX_TYPES)
		goto badframe;
	
	if ((type & BNEP_TYPE_MASK) == BNEP_CONTROL) {
		bnep_rx_control(s, skb->data, skb->len);
		kfree_skb(skb);
		return 0;
	}

	skb->mac.raw = skb->data;

	/* Verify and pull out header */
	if (!skb_pull(skb, __bnep_rx_hlen[type & BNEP_TYPE_MASK]))
		goto badframe;

	s->eh.h_proto = get_unaligned((u16 *) (skb->data - 2));

	if (type & BNEP_EXT_HEADER) {
		if (bnep_rx_extension(s, skb) < 0)
			goto badframe;
	}

	/* Strip 802.1p header */
	if (ntohs(s->eh.h_proto) == 0x8100) {
		if (!skb_pull(skb, 4))
			goto badframe;
		s->eh.h_proto = get_unaligned((u16 *) (skb->data - 2));
	}
	
	/* We have to alloc new skb and copy data here :(. Because original skb
	 * may not be modified and because of the alignment requirements. */
	nskb = alloc_skb(2 + ETH_HLEN + skb->len, GFP_KERNEL);
	if (!nskb) {
		s->stats.rx_dropped++;
		kfree_skb(skb);
		return -ENOMEM;
	}
	skb_reserve(nskb, 2);

	/* Decompress header and construct ether frame */
	switch (type & BNEP_TYPE_MASK) {
	case BNEP_COMPRESSED:
		memcpy(__skb_put(nskb, ETH_HLEN), &s->eh, ETH_HLEN);
		break;
	
	case BNEP_COMPRESSED_SRC_ONLY:
		memcpy(__skb_put(nskb, ETH_ALEN), s->eh.h_dest, ETH_ALEN);
		memcpy(__skb_put(nskb, ETH_ALEN), skb->mac.raw, ETH_ALEN);
		put_unaligned(s->eh.h_proto, (u16 *) __skb_put(nskb, 2));
		break;

	case BNEP_COMPRESSED_DST_ONLY:
		memcpy(__skb_put(nskb, ETH_ALEN), skb->mac.raw, ETH_ALEN);
		memcpy(__skb_put(nskb, ETH_ALEN + 2), s->eh.h_source, ETH_ALEN + 2);
		break;

	case BNEP_GENERAL:
		memcpy(__skb_put(nskb, ETH_ALEN * 2), skb->mac.raw, ETH_ALEN * 2);
		put_unaligned(s->eh.h_proto, (u16 *) __skb_put(nskb, 2));
		break;
	}

	memcpy(__skb_put(nskb, skb->len), skb->data, skb->len);
	kfree_skb(skb);
	
	s->stats.rx_packets++;
	nskb->dev       = dev;
	nskb->ip_summed = CHECKSUM_NONE;
	nskb->protocol  = eth_type_trans(nskb, dev);
	netif_rx_ni(nskb);
	return 0;

badframe:
	s->stats.rx_errors++;
	kfree_skb(skb);
	return 0;
}

static u8 __bnep_tx_types[] = {
	BNEP_GENERAL,
	BNEP_COMPRESSED_SRC_ONLY,
	BNEP_COMPRESSED_DST_ONLY,
	BNEP_COMPRESSED
};

static inline int bnep_tx_frame(struct bnep_session *s, struct sk_buff *skb)
{
	struct ethhdr *eh = (void *) skb->data;
	struct socket *sock = s->sock;
	struct kvec iv[3];
	int len = 0, il = 0;
	u8 type = 0;

	BT_DBG("skb %p dev %p type %d", skb, skb->dev, skb->pkt_type);

	if (!skb->dev) {
		/* Control frame sent by us */
		goto send;
	}

	iv[il++] = (struct kvec) { &type, 1 };
	len++;

	if (!memcmp(eh->h_dest, s->eh.h_source, ETH_ALEN))
		type |= 0x01;

	if (!memcmp(eh->h_source, s->eh.h_dest, ETH_ALEN))
		type |= 0x02;

	if (type)
		skb_pull(skb, ETH_ALEN * 2);

	type = __bnep_tx_types[type];
	switch (type) {
	case BNEP_COMPRESSED_SRC_ONLY:
		iv[il++] = (struct kvec) { eh->h_source, ETH_ALEN };
		len += ETH_ALEN;
		break;
		
	case BNEP_COMPRESSED_DST_ONLY:
		iv[il++] = (struct kvec) { eh->h_dest, ETH_ALEN };
		len += ETH_ALEN;
		break;
	}

send:
	iv[il++] = (struct kvec) { skb->data, skb->len };
	len += skb->len;
	
	/* FIXME: linearize skb */
	{
		len = kernel_sendmsg(sock, &s->msg, iv, il, len);
	}
	kfree_skb(skb);

	if (len > 0) {
		s->stats.tx_bytes += len;
		s->stats.tx_packets++;
		return 0;
	}

	return len;
}

static int bnep_session(void *arg)
{
	struct bnep_session *s = arg;
	struct net_device *dev = s->dev;
	struct sock *sk = s->sock->sk;
	struct sk_buff *skb;
	wait_queue_t wait;

	BT_DBG("");

        daemonize("kbnepd %s", dev->name);
	set_user_nice(current, -15);
	current->flags |= PF_NOFREEZE;

	init_waitqueue_entry(&wait, current);
	add_wait_queue(sk->sk_sleep, &wait);
	while (!atomic_read(&s->killed)) {
		set_current_state(TASK_INTERRUPTIBLE);

		// RX
		while ((skb = skb_dequeue(&sk->sk_receive_queue))) {
			skb_orphan(skb);
			bnep_rx_frame(s, skb);
		}

		if (sk->sk_state != BT_CONNECTED)
			break;
	
		// TX
		while ((skb = skb_dequeue(&sk->sk_write_queue)))
			if (bnep_tx_frame(s, skb))
				break;
		netif_wake_queue(dev);
	
		schedule();
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(sk->sk_sleep, &wait);

	/* Cleanup session */
	down_write(&bnep_session_sem);

	/* Delete network device */
	unregister_netdev(dev);

	/* Release the socket */
	fput(s->sock->file);

	__bnep_unlink_session(s);

	up_write(&bnep_session_sem);
	free_netdev(dev);
	return 0;
}

int bnep_add_connection(struct bnep_connadd_req *req, struct socket *sock)
{
	struct net_device *dev;
	struct bnep_session *s, *ss;
	u8 dst[ETH_ALEN], src[ETH_ALEN];
	int err;

	BT_DBG("");

	baswap((void *) dst, &bt_sk(sock->sk)->dst);
	baswap((void *) src, &bt_sk(sock->sk)->src);

	/* session struct allocated as private part of net_device */
	dev = alloc_netdev(sizeof(struct bnep_session),
			   (*req->device) ? req->device : "bnep%d",
			   bnep_net_setup);
	if (!dev) 
		return ENOMEM;


	down_write(&bnep_session_sem);

	ss = __bnep_get_session(dst);
	if (ss && ss->state == BT_CONNECTED) {
		err = -EEXIST;
		goto failed;
	}

	s = dev->priv;

	/* This is rx header therefore addresses are swapped.
	 * ie eh.h_dest is our local address. */
	memcpy(s->eh.h_dest,   &src, ETH_ALEN);
	memcpy(s->eh.h_source, &dst, ETH_ALEN);
	memcpy(dev->dev_addr, s->eh.h_dest, ETH_ALEN);

	s->dev = dev;
	s->sock  = sock;
	s->role  = req->role;
	s->state = BT_CONNECTED;
	
	s->msg.msg_flags = MSG_NOSIGNAL;

#ifdef CONFIG_BT_BNEP_MC_FILTER
	/* Set default mc filter */
	set_bit(bnep_mc_hash(dev->broadcast), (ulong *) &s->mc_filter);
#endif

#ifdef CONFIG_BT_BNEP_PROTO_FILTER
	/* Set default protocol filter */
	bnep_set_default_proto_filter(s);
#endif

	err = register_netdev(dev);
	if (err) {
		goto failed;
	}

	__bnep_link_session(s);
	
	err = kernel_thread(bnep_session, s, CLONE_KERNEL);
	if (err < 0) {
		/* Session thread start failed, gotta cleanup. */
		unregister_netdev(dev);
		__bnep_unlink_session(s);
		goto failed;
	}

	up_write(&bnep_session_sem);
	strcpy(req->device, dev->name);
	return 0;

failed:
	up_write(&bnep_session_sem);
	free_netdev(dev);
	return err;
}

int bnep_del_connection(struct bnep_conndel_req *req)
{
	struct bnep_session *s;
	int  err = 0;

	BT_DBG("");

	down_read(&bnep_session_sem);

	s = __bnep_get_session(req->dst);
	if (s) {
		/* Wakeup user-space which is polling for socket errors.
		 * This is temporary hack untill we have shutdown in L2CAP */
		s->sock->sk->sk_err = EUNATCH;
		
		/* Kill session thread */
		atomic_inc(&s->killed);
		wake_up_interruptible(s->sock->sk->sk_sleep);
	} else
		err = -ENOENT;

	up_read(&bnep_session_sem);
	return err;
}

static void __bnep_copy_ci(struct bnep_conninfo *ci, struct bnep_session *s)
{
	memcpy(ci->dst, s->eh.h_source, ETH_ALEN);
	strcpy(ci->device, s->dev->name);
	ci->flags = s->flags;
	ci->state = s->state;
	ci->role  = s->role;
}

int bnep_get_connlist(struct bnep_connlist_req *req)
{
	struct list_head *p;
	int err = 0, n = 0;

	down_read(&bnep_session_sem);

	list_for_each(p, &bnep_session_list) {
		struct bnep_session *s;
		struct bnep_conninfo ci;

		s = list_entry(p, struct bnep_session, list);

		__bnep_copy_ci(&ci, s);
		
		if (copy_to_user(req->ci, &ci, sizeof(ci))) {
			err = -EFAULT;
			break;
		}

		if (++n >= req->cnum)
			break;

		req->ci++;
	}
	req->cnum = n;

	up_read(&bnep_session_sem);
	return err;
}

int bnep_get_conninfo(struct bnep_conninfo *ci)
{
	struct bnep_session *s;
	int err = 0;

	down_read(&bnep_session_sem);

	s = __bnep_get_session(ci->dst);
	if (s)
		__bnep_copy_ci(ci, s);
	else
		err = -ENOENT;

	up_read(&bnep_session_sem);
	return err;
}

static int __init bnep_init(void)
{	
	char flt[50] = "";

	l2cap_load();

#ifdef CONFIG_BT_BNEP_PROTO_FILTER
	strcat(flt, "protocol ");
#endif

#ifdef CONFIG_BT_BNEP_MC_FILTER
	strcat(flt, "multicast");
#endif

	BT_INFO("BNEP (Ethernet Emulation) ver %s", VERSION);
	if (flt[0])
		BT_INFO("BNEP filters: %s", flt);

	bnep_sock_init();
	return 0;
}

static void __exit bnep_exit(void)
{
	bnep_sock_cleanup();
}

module_init(bnep_init);
module_exit(bnep_exit);

MODULE_AUTHOR("David Libault <david.libault@inventel.fr>, Maxim Krasnyansky <maxk@qualcomm.com>");
MODULE_DESCRIPTION("Bluetooth BNEP ver " VERSION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");
MODULE_ALIAS("bt-proto-4");
