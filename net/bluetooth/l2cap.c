/*
   BlueZ - Bluetooth protocol stack for Linux
   Copyright (C) 2000-2001 Qualcomm Incorporated

   Written 2000,2001 by Maxim Krasnyansky <maxk@qualcomm.com>

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

/* Bluetooth L2CAP core and sockets. */

#include <linux/module.h>

#include <linux/types.h>
#include <linux/capability.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/fcntl.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/socket.h>
#include <linux/skbuff.h>
#include <linux/list.h>
#include <linux/device.h>
#include <net/sock.h>

#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/unaligned.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include <net/bluetooth/l2cap.h>

#ifndef CONFIG_BT_L2CAP_DEBUG
#undef  BT_DBG
#define BT_DBG(D...)
#endif

#define VERSION "2.8"

static const struct proto_ops l2cap_sock_ops;

static struct bt_sock_list l2cap_sk_list = {
	.lock = RW_LOCK_UNLOCKED
};

static void __l2cap_sock_close(struct sock *sk, int reason);
static void l2cap_sock_close(struct sock *sk);
static void l2cap_sock_kill(struct sock *sk);

static struct sk_buff *l2cap_build_cmd(struct l2cap_conn *conn,
				u8 code, u8 ident, u16 dlen, void *data);

/* ---- L2CAP timers ---- */
static void l2cap_sock_timeout(unsigned long arg)
{
	struct sock *sk = (struct sock *) arg;

	BT_DBG("sock %p state %d", sk, sk->sk_state);

	bh_lock_sock(sk);
	__l2cap_sock_close(sk, ETIMEDOUT);
	bh_unlock_sock(sk);

	l2cap_sock_kill(sk);
	sock_put(sk);
}

static void l2cap_sock_set_timer(struct sock *sk, long timeout)
{
	BT_DBG("sk %p state %d timeout %ld", sk, sk->sk_state, timeout);
	sk_reset_timer(sk, &sk->sk_timer, jiffies + timeout);
}

static void l2cap_sock_clear_timer(struct sock *sk)
{
	BT_DBG("sock %p state %d", sk, sk->sk_state);
	sk_stop_timer(sk, &sk->sk_timer);
}

static void l2cap_sock_init_timer(struct sock *sk)
{
	init_timer(&sk->sk_timer);
	sk->sk_timer.function = l2cap_sock_timeout;
	sk->sk_timer.data = (unsigned long)sk;
}

/* ---- L2CAP channels ---- */
static struct sock *__l2cap_get_chan_by_dcid(struct l2cap_chan_list *l, u16 cid)
{
	struct sock *s;
	for (s = l->head; s; s = l2cap_pi(s)->next_c) {
		if (l2cap_pi(s)->dcid == cid)
			break;
	}
	return s;
}

static struct sock *__l2cap_get_chan_by_scid(struct l2cap_chan_list *l, u16 cid)
{
	struct sock *s;
	for (s = l->head; s; s = l2cap_pi(s)->next_c) {
		if (l2cap_pi(s)->scid == cid)
			break;
	}
	return s;
}

/* Find channel with given SCID.
 * Returns locked socket */
static inline struct sock *l2cap_get_chan_by_scid(struct l2cap_chan_list *l, u16 cid)
{
	struct sock *s;
	read_lock(&l->lock);
	s = __l2cap_get_chan_by_scid(l, cid);
	if (s) bh_lock_sock(s);
	read_unlock(&l->lock);
	return s;
}

static struct sock *__l2cap_get_chan_by_ident(struct l2cap_chan_list *l, u8 ident)
{
	struct sock *s;
	for (s = l->head; s; s = l2cap_pi(s)->next_c) {
		if (l2cap_pi(s)->ident == ident)
			break;
	}
	return s;
}

static inline struct sock *l2cap_get_chan_by_ident(struct l2cap_chan_list *l, u8 ident)
{
	struct sock *s;
	read_lock(&l->lock);
	s = __l2cap_get_chan_by_ident(l, ident);
	if (s) bh_lock_sock(s);
	read_unlock(&l->lock);
	return s;
}

static u16 l2cap_alloc_cid(struct l2cap_chan_list *l)
{
	u16 cid = 0x0040;

	for (; cid < 0xffff; cid++) {
		if(!__l2cap_get_chan_by_scid(l, cid))
			return cid;
	}

	return 0;
}

static inline void __l2cap_chan_link(struct l2cap_chan_list *l, struct sock *sk)
{
	sock_hold(sk);

	if (l->head)
		l2cap_pi(l->head)->prev_c = sk;

	l2cap_pi(sk)->next_c = l->head;
	l2cap_pi(sk)->prev_c = NULL;
	l->head = sk;
}

static inline void l2cap_chan_unlink(struct l2cap_chan_list *l, struct sock *sk)
{
	struct sock *next = l2cap_pi(sk)->next_c, *prev = l2cap_pi(sk)->prev_c;

	write_lock_bh(&l->lock);
	if (sk == l->head)
		l->head = next;

	if (next)
		l2cap_pi(next)->prev_c = prev;
	if (prev)
		l2cap_pi(prev)->next_c = next;
	write_unlock_bh(&l->lock);

	__sock_put(sk);
}

static void __l2cap_chan_add(struct l2cap_conn *conn, struct sock *sk, struct sock *parent)
{
	struct l2cap_chan_list *l = &conn->chan_list;

	BT_DBG("conn %p, psm 0x%2.2x, dcid 0x%4.4x", conn, l2cap_pi(sk)->psm, l2cap_pi(sk)->dcid);

	l2cap_pi(sk)->conn = conn;

	if (sk->sk_type == SOCK_SEQPACKET) {
		/* Alloc CID for connection-oriented socket */
		l2cap_pi(sk)->scid = l2cap_alloc_cid(l);
	} else if (sk->sk_type == SOCK_DGRAM) {
		/* Connectionless socket */
		l2cap_pi(sk)->scid = 0x0002;
		l2cap_pi(sk)->dcid = 0x0002;
		l2cap_pi(sk)->omtu = L2CAP_DEFAULT_MTU;
	} else {
		/* Raw socket can send/recv signalling messages only */
		l2cap_pi(sk)->scid = 0x0001;
		l2cap_pi(sk)->dcid = 0x0001;
		l2cap_pi(sk)->omtu = L2CAP_DEFAULT_MTU;
	}

	__l2cap_chan_link(l, sk);

	if (parent)
		bt_accept_enqueue(parent, sk);
}

/* Delete channel.
 * Must be called on the locked socket. */
static void l2cap_chan_del(struct sock *sk, int err)
{
	struct l2cap_conn *conn = l2cap_pi(sk)->conn;
	struct sock *parent = bt_sk(sk)->parent;

	l2cap_sock_clear_timer(sk);

	BT_DBG("sk %p, conn %p, err %d", sk, conn, err);

	if (conn) {
		/* Unlink from channel list */
		l2cap_chan_unlink(&conn->chan_list, sk);
		l2cap_pi(sk)->conn = NULL;
		hci_conn_put(conn->hcon);
	}

	sk->sk_state  = BT_CLOSED;
	sock_set_flag(sk, SOCK_ZAPPED);

	if (err)
		sk->sk_err = err;

	if (parent) {
		bt_accept_unlink(sk);
		parent->sk_data_ready(parent, 0);
	} else
		sk->sk_state_change(sk);
}

/* ---- L2CAP connections ---- */
static struct l2cap_conn *l2cap_conn_add(struct hci_conn *hcon, u8 status)
{
	struct l2cap_conn *conn = hcon->l2cap_data;

	if (conn || status)
		return conn;

	conn = kzalloc(sizeof(struct l2cap_conn), GFP_ATOMIC);
	if (!conn)
		return NULL;

	hcon->l2cap_data = conn;
	conn->hcon = hcon;

	BT_DBG("hcon %p conn %p", hcon, conn);

	conn->mtu = hcon->hdev->acl_mtu;
	conn->src = &hcon->hdev->bdaddr;
	conn->dst = &hcon->dst;

	spin_lock_init(&conn->lock);
	rwlock_init(&conn->chan_list.lock);

	return conn;
}

static void l2cap_conn_del(struct hci_conn *hcon, int err)
{
	struct l2cap_conn *conn = hcon->l2cap_data;
	struct sock *sk;

	if (!conn)
		return;

	BT_DBG("hcon %p conn %p, err %d", hcon, conn, err);

	if (conn->rx_skb)
		kfree_skb(conn->rx_skb);

	/* Kill channels */
	while ((sk = conn->chan_list.head)) {
		bh_lock_sock(sk);
		l2cap_chan_del(sk, err);
		bh_unlock_sock(sk);
		l2cap_sock_kill(sk);
	}

	hcon->l2cap_data = NULL;
	kfree(conn);
}

static inline void l2cap_chan_add(struct l2cap_conn *conn, struct sock *sk, struct sock *parent)
{
	struct l2cap_chan_list *l = &conn->chan_list;
	write_lock_bh(&l->lock);
	__l2cap_chan_add(conn, sk, parent);
	write_unlock_bh(&l->lock);
}

static inline u8 l2cap_get_ident(struct l2cap_conn *conn)
{
	u8 id;

	/* Get next available identificator.
	 *    1 - 128 are used by kernel.
	 *  129 - 199 are reserved.
	 *  200 - 254 are used by utilities like l2ping, etc.
	 */

	spin_lock_bh(&conn->lock);

	if (++conn->tx_ident > 128)
		conn->tx_ident = 1;

	id = conn->tx_ident;

	spin_unlock_bh(&conn->lock);

	return id;
}

static inline int l2cap_send_cmd(struct l2cap_conn *conn, u8 ident, u8 code, u16 len, void *data)
{
	struct sk_buff *skb = l2cap_build_cmd(conn, code, ident, len, data);

	BT_DBG("code 0x%2.2x", code);

	if (!skb)
		return -ENOMEM;

	return hci_send_acl(conn->hcon, skb, 0);
}

/* ---- Socket interface ---- */
static struct sock *__l2cap_get_sock_by_addr(u16 psm, bdaddr_t *src)
{
	struct sock *sk;
	struct hlist_node *node;
	sk_for_each(sk, node, &l2cap_sk_list.head)
		if (l2cap_pi(sk)->sport == psm && !bacmp(&bt_sk(sk)->src, src))
			goto found;
	sk = NULL;
found:
	return sk;
}

/* Find socket with psm and source bdaddr.
 * Returns closest match.
 */
static struct sock *__l2cap_get_sock_by_psm(int state, u16 psm, bdaddr_t *src)
{
	struct sock *sk = NULL, *sk1 = NULL;
	struct hlist_node *node;

	sk_for_each(sk, node, &l2cap_sk_list.head) {
		if (state && sk->sk_state != state)
			continue;

		if (l2cap_pi(sk)->psm == psm) {
			/* Exact match. */
			if (!bacmp(&bt_sk(sk)->src, src))
				break;

			/* Closest match */
			if (!bacmp(&bt_sk(sk)->src, BDADDR_ANY))
				sk1 = sk;
		}
	}
	return node ? sk : sk1;
}

/* Find socket with given address (psm, src).
 * Returns locked socket */
static inline struct sock *l2cap_get_sock_by_psm(int state, u16 psm, bdaddr_t *src)
{
	struct sock *s;
	read_lock(&l2cap_sk_list.lock);
	s = __l2cap_get_sock_by_psm(state, psm, src);
	if (s) bh_lock_sock(s);
	read_unlock(&l2cap_sk_list.lock);
	return s;
}

static void l2cap_sock_destruct(struct sock *sk)
{
	BT_DBG("sk %p", sk);

	skb_queue_purge(&sk->sk_receive_queue);
	skb_queue_purge(&sk->sk_write_queue);
}

static void l2cap_sock_cleanup_listen(struct sock *parent)
{
	struct sock *sk;

	BT_DBG("parent %p", parent);

	/* Close not yet accepted channels */
	while ((sk = bt_accept_dequeue(parent, NULL)))
		l2cap_sock_close(sk);

	parent->sk_state  = BT_CLOSED;
	sock_set_flag(parent, SOCK_ZAPPED);
}

/* Kill socket (only if zapped and orphan)
 * Must be called on unlocked socket.
 */
static void l2cap_sock_kill(struct sock *sk)
{
	if (!sock_flag(sk, SOCK_ZAPPED) || sk->sk_socket)
		return;

	BT_DBG("sk %p state %d", sk, sk->sk_state);

	/* Kill poor orphan */
	bt_sock_unlink(&l2cap_sk_list, sk);
	sock_set_flag(sk, SOCK_DEAD);
	sock_put(sk);
}

static void __l2cap_sock_close(struct sock *sk, int reason)
{
	BT_DBG("sk %p state %d socket %p", sk, sk->sk_state, sk->sk_socket);

	switch (sk->sk_state) {
	case BT_LISTEN:
		l2cap_sock_cleanup_listen(sk);
		break;

	case BT_CONNECTED:
	case BT_CONFIG:
	case BT_CONNECT2:
		if (sk->sk_type == SOCK_SEQPACKET) {
			struct l2cap_conn *conn = l2cap_pi(sk)->conn;
			struct l2cap_disconn_req req;

			sk->sk_state = BT_DISCONN;
			l2cap_sock_set_timer(sk, sk->sk_sndtimeo);

			req.dcid = __cpu_to_le16(l2cap_pi(sk)->dcid);
			req.scid = __cpu_to_le16(l2cap_pi(sk)->scid);
			l2cap_send_cmd(conn, l2cap_get_ident(conn),
					L2CAP_DISCONN_REQ, sizeof(req), &req);
		} else {
			l2cap_chan_del(sk, reason);
		}
		break;

	case BT_CONNECT:
	case BT_DISCONN:
		l2cap_chan_del(sk, reason);
		break;

	default:
		sock_set_flag(sk, SOCK_ZAPPED);
		break;
	}
}

/* Must be called on unlocked socket. */
static void l2cap_sock_close(struct sock *sk)
{
	l2cap_sock_clear_timer(sk);
	lock_sock(sk);
	__l2cap_sock_close(sk, ECONNRESET);
	release_sock(sk);
	l2cap_sock_kill(sk);
}

static void l2cap_sock_init(struct sock *sk, struct sock *parent)
{
	struct l2cap_pinfo *pi = l2cap_pi(sk);

	BT_DBG("sk %p", sk);

	if (parent) {
		sk->sk_type = parent->sk_type;
		pi->imtu = l2cap_pi(parent)->imtu;
		pi->omtu = l2cap_pi(parent)->omtu;
		pi->link_mode = l2cap_pi(parent)->link_mode;
	} else {
		pi->imtu = L2CAP_DEFAULT_MTU;
		pi->omtu = 0;
		pi->link_mode = 0;
	}

	/* Default config options */
	pi->conf_mtu = L2CAP_DEFAULT_MTU;
	pi->flush_to = L2CAP_DEFAULT_FLUSH_TO;
}

static struct proto l2cap_proto = {
	.name		= "L2CAP",
	.owner		= THIS_MODULE,
	.obj_size	= sizeof(struct l2cap_pinfo)
};

static struct sock *l2cap_sock_alloc(struct socket *sock, int proto, gfp_t prio)
{
	struct sock *sk;

	sk = sk_alloc(PF_BLUETOOTH, prio, &l2cap_proto, 1);
	if (!sk)
		return NULL;

	sock_init_data(sock, sk);
	INIT_LIST_HEAD(&bt_sk(sk)->accept_q);

	sk->sk_destruct = l2cap_sock_destruct;
	sk->sk_sndtimeo = L2CAP_CONN_TIMEOUT;

	sock_reset_flag(sk, SOCK_ZAPPED);

	sk->sk_protocol = proto;
	sk->sk_state    = BT_OPEN;

	l2cap_sock_init_timer(sk);

	bt_sock_link(&l2cap_sk_list, sk);
	return sk;
}

static int l2cap_sock_create(struct socket *sock, int protocol)
{
	struct sock *sk;

	BT_DBG("sock %p", sock);

	sock->state = SS_UNCONNECTED;

	if (sock->type != SOCK_SEQPACKET &&
			sock->type != SOCK_DGRAM && sock->type != SOCK_RAW)
		return -ESOCKTNOSUPPORT;

	if (sock->type == SOCK_RAW && !capable(CAP_NET_RAW))
		return -EPERM;

	sock->ops = &l2cap_sock_ops;

	sk = l2cap_sock_alloc(sock, protocol, GFP_ATOMIC);
	if (!sk)
		return -ENOMEM;

	l2cap_sock_init(sk, NULL);
	return 0;
}

static int l2cap_sock_bind(struct socket *sock, struct sockaddr *addr, int addr_len)
{
	struct sockaddr_l2 *la = (struct sockaddr_l2 *) addr;
	struct sock *sk = sock->sk;
	int err = 0;

	BT_DBG("sk %p, %s %d", sk, batostr(&la->l2_bdaddr), la->l2_psm);

	if (!addr || addr->sa_family != AF_BLUETOOTH)
		return -EINVAL;

	lock_sock(sk);

	if (sk->sk_state != BT_OPEN) {
		err = -EBADFD;
		goto done;
	}

	if (la->l2_psm > 0 && btohs(la->l2_psm) < 0x1001 &&
				!capable(CAP_NET_BIND_SERVICE)) {
		err = -EACCES;
		goto done;
	}

	write_lock_bh(&l2cap_sk_list.lock);

	if (la->l2_psm && __l2cap_get_sock_by_addr(la->l2_psm, &la->l2_bdaddr)) {
		err = -EADDRINUSE;
	} else {
		/* Save source address */
		bacpy(&bt_sk(sk)->src, &la->l2_bdaddr);
		l2cap_pi(sk)->psm   = la->l2_psm;
		l2cap_pi(sk)->sport = la->l2_psm;
		sk->sk_state = BT_BOUND;
	}

	write_unlock_bh(&l2cap_sk_list.lock);

done:
	release_sock(sk);
	return err;
}

static int l2cap_do_connect(struct sock *sk)
{
	bdaddr_t *src = &bt_sk(sk)->src;
	bdaddr_t *dst = &bt_sk(sk)->dst;
	struct l2cap_conn *conn;
	struct hci_conn *hcon;
	struct hci_dev *hdev;
	int err = 0;

	BT_DBG("%s -> %s psm 0x%2.2x", batostr(src), batostr(dst), l2cap_pi(sk)->psm);

	if (!(hdev = hci_get_route(dst, src)))
		return -EHOSTUNREACH;

	hci_dev_lock_bh(hdev);

	err = -ENOMEM;

	hcon = hci_connect(hdev, ACL_LINK, dst);
	if (!hcon)
		goto done;

	conn = l2cap_conn_add(hcon, 0);
	if (!conn) {
		hci_conn_put(hcon);
		goto done;
	}

	err = 0;

	/* Update source addr of the socket */
	bacpy(src, conn->src);

	l2cap_chan_add(conn, sk, NULL);

	sk->sk_state = BT_CONNECT;
	l2cap_sock_set_timer(sk, sk->sk_sndtimeo);

	if (hcon->state == BT_CONNECTED) {
		if (sk->sk_type == SOCK_SEQPACKET) {
			struct l2cap_conn_req req;
			l2cap_pi(sk)->ident = l2cap_get_ident(conn);
			req.scid = __cpu_to_le16(l2cap_pi(sk)->scid);
			req.psm  = l2cap_pi(sk)->psm;
			l2cap_send_cmd(conn, l2cap_pi(sk)->ident,
					L2CAP_CONN_REQ, sizeof(req), &req);
		} else {
			l2cap_sock_clear_timer(sk);
			sk->sk_state = BT_CONNECTED;
		}
	}

done:
	hci_dev_unlock_bh(hdev);
	hci_dev_put(hdev);
	return err;
}

static int l2cap_sock_connect(struct socket *sock, struct sockaddr *addr, int alen, int flags)
{
	struct sockaddr_l2 *la = (struct sockaddr_l2 *) addr;
	struct sock *sk = sock->sk;
	int err = 0;

	lock_sock(sk);

	BT_DBG("sk %p", sk);

	if (addr->sa_family != AF_BLUETOOTH || alen < sizeof(struct sockaddr_l2)) {
		err = -EINVAL;
		goto done;
	}

	if (sk->sk_type == SOCK_SEQPACKET && !la->l2_psm) {
		err = -EINVAL;
		goto done;
	}

	switch(sk->sk_state) {
	case BT_CONNECT:
	case BT_CONNECT2:
	case BT_CONFIG:
		/* Already connecting */
		goto wait;

	case BT_CONNECTED:
		/* Already connected */
		goto done;

	case BT_OPEN:
	case BT_BOUND:
		/* Can connect */
		break;

	default:
		err = -EBADFD;
		goto done;
	}

	/* Set destination address and psm */
	bacpy(&bt_sk(sk)->dst, &la->l2_bdaddr);
	l2cap_pi(sk)->psm = la->l2_psm;

	if ((err = l2cap_do_connect(sk)))
		goto done;

wait:
	err = bt_sock_wait_state(sk, BT_CONNECTED,
			sock_sndtimeo(sk, flags & O_NONBLOCK));
done:
	release_sock(sk);
	return err;
}

static int l2cap_sock_listen(struct socket *sock, int backlog)
{
	struct sock *sk = sock->sk;
	int err = 0;

	BT_DBG("sk %p backlog %d", sk, backlog);

	lock_sock(sk);

	if (sk->sk_state != BT_BOUND || sock->type != SOCK_SEQPACKET) {
		err = -EBADFD;
		goto done;
	}

	if (!l2cap_pi(sk)->psm) {
		bdaddr_t *src = &bt_sk(sk)->src;
		u16 psm;

		err = -EINVAL;

		write_lock_bh(&l2cap_sk_list.lock);

		for (psm = 0x1001; psm < 0x1100; psm += 2)
			if (!__l2cap_get_sock_by_addr(psm, src)) {
				l2cap_pi(sk)->psm   = htobs(psm);
				l2cap_pi(sk)->sport = htobs(psm);
				err = 0;
				break;
			}

		write_unlock_bh(&l2cap_sk_list.lock);

		if (err < 0)
			goto done;
	}

	sk->sk_max_ack_backlog = backlog;
	sk->sk_ack_backlog = 0;
	sk->sk_state = BT_LISTEN;

done:
	release_sock(sk);
	return err;
}

static int l2cap_sock_accept(struct socket *sock, struct socket *newsock, int flags)
{
	DECLARE_WAITQUEUE(wait, current);
	struct sock *sk = sock->sk, *nsk;
	long timeo;
	int err = 0;

	lock_sock_nested(sk, SINGLE_DEPTH_NESTING);

	if (sk->sk_state != BT_LISTEN) {
		err = -EBADFD;
		goto done;
	}

	timeo = sock_rcvtimeo(sk, flags & O_NONBLOCK);

	BT_DBG("sk %p timeo %ld", sk, timeo);

	/* Wait for an incoming connection. (wake-one). */
	add_wait_queue_exclusive(sk->sk_sleep, &wait);
	while (!(nsk = bt_accept_dequeue(sk, newsock))) {
		set_current_state(TASK_INTERRUPTIBLE);
		if (!timeo) {
			err = -EAGAIN;
			break;
		}

		release_sock(sk);
		timeo = schedule_timeout(timeo);
		lock_sock_nested(sk, SINGLE_DEPTH_NESTING);

		if (sk->sk_state != BT_LISTEN) {
			err = -EBADFD;
			break;
		}

		if (signal_pending(current)) {
			err = sock_intr_errno(timeo);
			break;
		}
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(sk->sk_sleep, &wait);

	if (err)
		goto done;

	newsock->state = SS_CONNECTED;

	BT_DBG("new socket %p", nsk);

done:
	release_sock(sk);
	return err;
}

static int l2cap_sock_getname(struct socket *sock, struct sockaddr *addr, int *len, int peer)
{
	struct sockaddr_l2 *la = (struct sockaddr_l2 *) addr;
	struct sock *sk = sock->sk;

	BT_DBG("sock %p, sk %p", sock, sk);

	addr->sa_family = AF_BLUETOOTH;
	*len = sizeof(struct sockaddr_l2);

	if (peer)
		bacpy(&la->l2_bdaddr, &bt_sk(sk)->dst);
	else
		bacpy(&la->l2_bdaddr, &bt_sk(sk)->src);

	la->l2_psm = l2cap_pi(sk)->psm;
	return 0;
}

static inline int l2cap_do_send(struct sock *sk, struct msghdr *msg, int len)
{
	struct l2cap_conn *conn = l2cap_pi(sk)->conn;
	struct sk_buff *skb, **frag;
	int err, hlen, count, sent=0;
	struct l2cap_hdr *lh;

	BT_DBG("sk %p len %d", sk, len);

	/* First fragment (with L2CAP header) */
	if (sk->sk_type == SOCK_DGRAM)
		hlen = L2CAP_HDR_SIZE + 2;
	else
		hlen = L2CAP_HDR_SIZE;

	count = min_t(unsigned int, (conn->mtu - hlen), len);

	skb = bt_skb_send_alloc(sk, hlen + count,
			msg->msg_flags & MSG_DONTWAIT, &err);
	if (!skb)
		return err;

	/* Create L2CAP header */
	lh = (struct l2cap_hdr *) skb_put(skb, L2CAP_HDR_SIZE);
	lh->cid = __cpu_to_le16(l2cap_pi(sk)->dcid);
	lh->len = __cpu_to_le16(len + (hlen - L2CAP_HDR_SIZE));

	if (sk->sk_type == SOCK_DGRAM)
		put_unaligned(l2cap_pi(sk)->psm, (u16 *) skb_put(skb, 2));

	if (memcpy_fromiovec(skb_put(skb, count), msg->msg_iov, count)) {
		err = -EFAULT;
		goto fail;
	}

	sent += count;
	len  -= count;

	/* Continuation fragments (no L2CAP header) */
	frag = &skb_shinfo(skb)->frag_list;
	while (len) {
		count = min_t(unsigned int, conn->mtu, len);

		*frag = bt_skb_send_alloc(sk, count, msg->msg_flags & MSG_DONTWAIT, &err);
		if (!*frag)
			goto fail;

		if (memcpy_fromiovec(skb_put(*frag, count), msg->msg_iov, count)) {
			err = -EFAULT;
			goto fail;
		}

		sent += count;
		len  -= count;

		frag = &(*frag)->next;
	}

	if ((err = hci_send_acl(conn->hcon, skb, 0)) < 0)
		goto fail;

	return sent;

fail:
	kfree_skb(skb);
	return err;
}

static int l2cap_sock_sendmsg(struct kiocb *iocb, struct socket *sock, struct msghdr *msg, size_t len)
{
	struct sock *sk = sock->sk;
	int err = 0;

	BT_DBG("sock %p, sk %p", sock, sk);

	err = sock_error(sk);
	if (err)
		return err;

	if (msg->msg_flags & MSG_OOB)
		return -EOPNOTSUPP;

	/* Check outgoing MTU */
	if (sk->sk_type != SOCK_RAW && len > l2cap_pi(sk)->omtu)
		return -EINVAL;

	lock_sock(sk);

	if (sk->sk_state == BT_CONNECTED)
		err = l2cap_do_send(sk, msg, len);
	else
		err = -ENOTCONN;

	release_sock(sk);
	return err;
}

static int l2cap_sock_setsockopt(struct socket *sock, int level, int optname, char __user *optval, int optlen)
{
	struct sock *sk = sock->sk;
	struct l2cap_options opts;
	int err = 0, len;
	u32 opt;

	BT_DBG("sk %p", sk);

	lock_sock(sk);

	switch (optname) {
	case L2CAP_OPTIONS:
		len = min_t(unsigned int, sizeof(opts), optlen);
		if (copy_from_user((char *) &opts, optval, len)) {
			err = -EFAULT;
			break;
		}
		l2cap_pi(sk)->imtu  = opts.imtu;
		l2cap_pi(sk)->omtu  = opts.omtu;
		break;

	case L2CAP_LM:
		if (get_user(opt, (u32 __user *) optval)) {
			err = -EFAULT;
			break;
		}

		l2cap_pi(sk)->link_mode = opt;
		break;

	default:
		err = -ENOPROTOOPT;
		break;
	}

	release_sock(sk);
	return err;
}

static int l2cap_sock_getsockopt(struct socket *sock, int level, int optname, char __user *optval, int __user *optlen)
{
	struct sock *sk = sock->sk;
	struct l2cap_options opts;
	struct l2cap_conninfo cinfo;
	int len, err = 0;

	BT_DBG("sk %p", sk);

	if (get_user(len, optlen))
		return -EFAULT;

	lock_sock(sk);

	switch (optname) {
	case L2CAP_OPTIONS:
		opts.imtu     = l2cap_pi(sk)->imtu;
		opts.omtu     = l2cap_pi(sk)->omtu;
		opts.flush_to = l2cap_pi(sk)->flush_to;
		opts.mode     = 0x00;

		len = min_t(unsigned int, len, sizeof(opts));
		if (copy_to_user(optval, (char *) &opts, len))
			err = -EFAULT;

		break;

	case L2CAP_LM:
		if (put_user(l2cap_pi(sk)->link_mode, (u32 __user *) optval))
			err = -EFAULT;
		break;

	case L2CAP_CONNINFO:
		if (sk->sk_state != BT_CONNECTED) {
			err = -ENOTCONN;
			break;
		}

		cinfo.hci_handle = l2cap_pi(sk)->conn->hcon->handle;
		memcpy(cinfo.dev_class, l2cap_pi(sk)->conn->hcon->dev_class, 3);

		len = min_t(unsigned int, len, sizeof(cinfo));
		if (copy_to_user(optval, (char *) &cinfo, len))
			err = -EFAULT;

		break;

	default:
		err = -ENOPROTOOPT;
		break;
	}

	release_sock(sk);
	return err;
}

static int l2cap_sock_shutdown(struct socket *sock, int how)
{
	struct sock *sk = sock->sk;
	int err = 0;

	BT_DBG("sock %p, sk %p", sock, sk);

	if (!sk)
		return 0;

	lock_sock(sk);
	if (!sk->sk_shutdown) {
		sk->sk_shutdown = SHUTDOWN_MASK;
		l2cap_sock_clear_timer(sk);
		__l2cap_sock_close(sk, 0);

		if (sock_flag(sk, SOCK_LINGER) && sk->sk_lingertime)
			err = bt_sock_wait_state(sk, BT_CLOSED, sk->sk_lingertime);
	}
	release_sock(sk);
	return err;
}

static int l2cap_sock_release(struct socket *sock)
{
	struct sock *sk = sock->sk;
	int err;

	BT_DBG("sock %p, sk %p", sock, sk);

	if (!sk)
		return 0;

	err = l2cap_sock_shutdown(sock, 2);

	sock_orphan(sk);
	l2cap_sock_kill(sk);
	return err;
}

static void l2cap_conn_ready(struct l2cap_conn *conn)
{
	struct l2cap_chan_list *l = &conn->chan_list;
	struct sock *sk;

	BT_DBG("conn %p", conn);

	read_lock(&l->lock);

	for (sk = l->head; sk; sk = l2cap_pi(sk)->next_c) {
		bh_lock_sock(sk);

		if (sk->sk_type != SOCK_SEQPACKET) {
			l2cap_sock_clear_timer(sk);
			sk->sk_state = BT_CONNECTED;
			sk->sk_state_change(sk);
		} else if (sk->sk_state == BT_CONNECT) {
			struct l2cap_conn_req req;
			l2cap_pi(sk)->ident = l2cap_get_ident(conn);
			req.scid = __cpu_to_le16(l2cap_pi(sk)->scid);
			req.psm  = l2cap_pi(sk)->psm;
			l2cap_send_cmd(conn, l2cap_pi(sk)->ident, L2CAP_CONN_REQ, sizeof(req), &req);
		}

		bh_unlock_sock(sk);
	}

	read_unlock(&l->lock);
}

/* Notify sockets that we cannot guaranty reliability anymore */
static void l2cap_conn_unreliable(struct l2cap_conn *conn, int err)
{
	struct l2cap_chan_list *l = &conn->chan_list;
	struct sock *sk;

	BT_DBG("conn %p", conn);

	read_lock(&l->lock);
	for (sk = l->head; sk; sk = l2cap_pi(sk)->next_c) {
		if (l2cap_pi(sk)->link_mode & L2CAP_LM_RELIABLE)
			sk->sk_err = err;
	}
	read_unlock(&l->lock);
}

static void l2cap_chan_ready(struct sock *sk)
{
	struct sock *parent = bt_sk(sk)->parent;

	BT_DBG("sk %p, parent %p", sk, parent);

	l2cap_pi(sk)->conf_state = 0;
	l2cap_sock_clear_timer(sk);

	if (!parent) {
		/* Outgoing channel.
		 * Wake up socket sleeping on connect.
		 */
		sk->sk_state = BT_CONNECTED;
		sk->sk_state_change(sk);
	} else {
		/* Incoming channel.
		 * Wake up socket sleeping on accept.
		 */
		parent->sk_data_ready(parent, 0);
	}
}

/* Copy frame to all raw sockets on that connection */
static void l2cap_raw_recv(struct l2cap_conn *conn, struct sk_buff *skb)
{
	struct l2cap_chan_list *l = &conn->chan_list;
	struct sk_buff *nskb;
	struct sock * sk;

	BT_DBG("conn %p", conn);

	read_lock(&l->lock);
	for (sk = l->head; sk; sk = l2cap_pi(sk)->next_c) {
		if (sk->sk_type != SOCK_RAW)
			continue;

		/* Don't send frame to the socket it came from */
		if (skb->sk == sk)
			continue;

		if (!(nskb = skb_clone(skb, GFP_ATOMIC)))
			continue;

		if (sock_queue_rcv_skb(sk, nskb))
			kfree_skb(nskb);
	}
	read_unlock(&l->lock);
}

/* ---- L2CAP signalling commands ---- */
static struct sk_buff *l2cap_build_cmd(struct l2cap_conn *conn,
				u8 code, u8 ident, u16 dlen, void *data)
{
	struct sk_buff *skb, **frag;
	struct l2cap_cmd_hdr *cmd;
	struct l2cap_hdr *lh;
	int len, count;

	BT_DBG("conn %p, code 0x%2.2x, ident 0x%2.2x, len %d", conn, code, ident, dlen);

	len = L2CAP_HDR_SIZE + L2CAP_CMD_HDR_SIZE + dlen;
	count = min_t(unsigned int, conn->mtu, len);

	skb = bt_skb_alloc(count, GFP_ATOMIC);
	if (!skb)
		return NULL;

	lh = (struct l2cap_hdr *) skb_put(skb, L2CAP_HDR_SIZE);
	lh->len = __cpu_to_le16(L2CAP_CMD_HDR_SIZE + dlen);
	lh->cid = __cpu_to_le16(0x0001);

	cmd = (struct l2cap_cmd_hdr *) skb_put(skb, L2CAP_CMD_HDR_SIZE);
	cmd->code  = code;
	cmd->ident = ident;
	cmd->len   = __cpu_to_le16(dlen);

	if (dlen) {
		count -= L2CAP_HDR_SIZE + L2CAP_CMD_HDR_SIZE;
		memcpy(skb_put(skb, count), data, count);
		data += count;
	}

	len -= skb->len;

	/* Continuation fragments (no L2CAP header) */
	frag = &skb_shinfo(skb)->frag_list;
	while (len) {
		count = min_t(unsigned int, conn->mtu, len);

		*frag = bt_skb_alloc(count, GFP_ATOMIC);
		if (!*frag)
			goto fail;

		memcpy(skb_put(*frag, count), data, count);

		len  -= count;
		data += count;

		frag = &(*frag)->next;
	}

	return skb;

fail:
	kfree_skb(skb);
	return NULL;
}

static inline int l2cap_get_conf_opt(void **ptr, int *type, int *olen, unsigned long *val)
{
	struct l2cap_conf_opt *opt = *ptr;
	int len;

	len = L2CAP_CONF_OPT_SIZE + opt->len;
	*ptr += len;

	*type = opt->type;
	*olen = opt->len;

	switch (opt->len) {
	case 1:
		*val = *((u8 *) opt->val);
		break;

	case 2:
		*val = __le16_to_cpu(*((u16 *)opt->val));
		break;

	case 4:
		*val = __le32_to_cpu(*((u32 *)opt->val));
		break;

	default:
		*val = (unsigned long) opt->val;
		break;
	}

	BT_DBG("type 0x%2.2x len %d val 0x%lx", *type, opt->len, *val);
	return len;
}

static inline void l2cap_parse_conf_req(struct sock *sk, void *data, int len)
{
	int type, hint, olen;
	unsigned long val;
	void *ptr = data;

	BT_DBG("sk %p len %d", sk, len);

	while (len >= L2CAP_CONF_OPT_SIZE) {
		len -= l2cap_get_conf_opt(&ptr, &type, &olen, &val);

		hint  = type & 0x80;
		type &= 0x7f;

		switch (type) {
		case L2CAP_CONF_MTU:
			l2cap_pi(sk)->conf_mtu = val;
			break;

		case L2CAP_CONF_FLUSH_TO:
			l2cap_pi(sk)->flush_to = val;
			break;

		case L2CAP_CONF_QOS:
			break;

		default:
			if (hint)
				break;

			/* FIXME: Reject unknown option */
			break;
		}
	}
}

static void l2cap_add_conf_opt(void **ptr, u8 type, u8 len, unsigned long val)
{
	struct l2cap_conf_opt *opt = *ptr;

	BT_DBG("type 0x%2.2x len %d val 0x%lx", type, len, val);

	opt->type = type;
	opt->len  = len;

	switch (len) {
	case 1:
		*((u8 *) opt->val)  = val;
		break;

	case 2:
		*((u16 *) opt->val) = __cpu_to_le16(val);
		break;

	case 4:
		*((u32 *) opt->val) = __cpu_to_le32(val);
		break;

	default:
		memcpy(opt->val, (void *) val, len);
		break;
	}

	*ptr += L2CAP_CONF_OPT_SIZE + len;
}

static int l2cap_build_conf_req(struct sock *sk, void *data)
{
	struct l2cap_pinfo *pi = l2cap_pi(sk);
	struct l2cap_conf_req *req = data;
	void *ptr = req->data;

	BT_DBG("sk %p", sk);

	if (pi->imtu != L2CAP_DEFAULT_MTU)
		l2cap_add_conf_opt(&ptr, L2CAP_CONF_MTU, 2, pi->imtu);

	/* FIXME: Need actual value of the flush timeout */
	//if (flush_to != L2CAP_DEFAULT_FLUSH_TO)
	//   l2cap_add_conf_opt(&ptr, L2CAP_CONF_FLUSH_TO, 2, pi->flush_to);

	req->dcid  = __cpu_to_le16(pi->dcid);
	req->flags = __cpu_to_le16(0);

	return ptr - data;
}

static inline int l2cap_conf_output(struct sock *sk, void **ptr)
{
	struct l2cap_pinfo *pi = l2cap_pi(sk);
	int result = 0;

	/* Configure output options and let the other side know
	 * which ones we don't like. */
	if (pi->conf_mtu < pi->omtu)
		result = L2CAP_CONF_UNACCEPT;
	else
		pi->omtu = pi->conf_mtu;

	l2cap_add_conf_opt(ptr, L2CAP_CONF_MTU, 2, pi->omtu);

	BT_DBG("sk %p result %d", sk, result);
	return result;
}

static int l2cap_build_conf_rsp(struct sock *sk, void *data, int *result)
{
	struct l2cap_conf_rsp *rsp = data;
	void *ptr = rsp->data;
	u16 flags = 0;

	BT_DBG("sk %p complete %d", sk, result ? 1 : 0);

	if (result)
		*result = l2cap_conf_output(sk, &ptr);
	else
		flags = 0x0001;

	rsp->scid   = __cpu_to_le16(l2cap_pi(sk)->dcid);
	rsp->result = __cpu_to_le16(result ? *result : 0);
	rsp->flags  = __cpu_to_le16(flags);

	return ptr - data;
}

static inline int l2cap_connect_req(struct l2cap_conn *conn, struct l2cap_cmd_hdr *cmd, u8 *data)
{
	struct l2cap_chan_list *list = &conn->chan_list;
	struct l2cap_conn_req *req = (struct l2cap_conn_req *) data;
	struct l2cap_conn_rsp rsp;
	struct sock *sk, *parent;
	int result = 0, status = 0;

	u16 dcid = 0, scid = __le16_to_cpu(req->scid);
	u16 psm  = req->psm;

	BT_DBG("psm 0x%2.2x scid 0x%4.4x", psm, scid);

	/* Check if we have socket listening on psm */
	parent = l2cap_get_sock_by_psm(BT_LISTEN, psm, conn->src);
	if (!parent) {
		result = L2CAP_CR_BAD_PSM;
		goto sendresp;
	}

	result = L2CAP_CR_NO_MEM;

	/* Check for backlog size */
	if (sk_acceptq_is_full(parent)) {
		BT_DBG("backlog full %d", parent->sk_ack_backlog);
		goto response;
	}

	sk = l2cap_sock_alloc(NULL, BTPROTO_L2CAP, GFP_ATOMIC);
	if (!sk)
		goto response;

	write_lock_bh(&list->lock);

	/* Check if we already have channel with that dcid */
	if (__l2cap_get_chan_by_dcid(list, scid)) {
		write_unlock_bh(&list->lock);
		sock_set_flag(sk, SOCK_ZAPPED);
		l2cap_sock_kill(sk);
		goto response;
	}

	hci_conn_hold(conn->hcon);

	l2cap_sock_init(sk, parent);
	bacpy(&bt_sk(sk)->src, conn->src);
	bacpy(&bt_sk(sk)->dst, conn->dst);
	l2cap_pi(sk)->psm  = psm;
	l2cap_pi(sk)->dcid = scid;

	__l2cap_chan_add(conn, sk, parent);
	dcid = l2cap_pi(sk)->scid;

	l2cap_sock_set_timer(sk, sk->sk_sndtimeo);

	/* Service level security */
	result = L2CAP_CR_PEND;
	status = L2CAP_CS_AUTHEN_PEND;
	sk->sk_state = BT_CONNECT2;
	l2cap_pi(sk)->ident = cmd->ident;

	if ((l2cap_pi(sk)->link_mode & L2CAP_LM_ENCRYPT) ||
			(l2cap_pi(sk)->link_mode & L2CAP_LM_SECURE)) {
		if (!hci_conn_encrypt(conn->hcon))
			goto done;
	} else if (l2cap_pi(sk)->link_mode & L2CAP_LM_AUTH) {
		if (!hci_conn_auth(conn->hcon))
			goto done;
	}

	sk->sk_state = BT_CONFIG;
	result = status = 0;

done:
	write_unlock_bh(&list->lock);

response:
	bh_unlock_sock(parent);

sendresp:
	rsp.scid   = __cpu_to_le16(scid);
	rsp.dcid   = __cpu_to_le16(dcid);
	rsp.result = __cpu_to_le16(result);
	rsp.status = __cpu_to_le16(status);
	l2cap_send_cmd(conn, cmd->ident, L2CAP_CONN_RSP, sizeof(rsp), &rsp);
	return 0;
}

static inline int l2cap_connect_rsp(struct l2cap_conn *conn, struct l2cap_cmd_hdr *cmd, u8 *data)
{
	struct l2cap_conn_rsp *rsp = (struct l2cap_conn_rsp *) data;
	u16 scid, dcid, result, status;
	struct sock *sk;
	u8 req[128];

	scid   = __le16_to_cpu(rsp->scid);
	dcid   = __le16_to_cpu(rsp->dcid);
	result = __le16_to_cpu(rsp->result);
	status = __le16_to_cpu(rsp->status);

	BT_DBG("dcid 0x%4.4x scid 0x%4.4x result 0x%2.2x status 0x%2.2x", dcid, scid, result, status);

	if (scid) {
		if (!(sk = l2cap_get_chan_by_scid(&conn->chan_list, scid)))
			return 0;
	} else {
		if (!(sk = l2cap_get_chan_by_ident(&conn->chan_list, cmd->ident)))
			return 0;
	}

	switch (result) {
	case L2CAP_CR_SUCCESS:
		sk->sk_state = BT_CONFIG;
		l2cap_pi(sk)->ident = 0;
		l2cap_pi(sk)->dcid = dcid;
		l2cap_pi(sk)->conf_state |= L2CAP_CONF_REQ_SENT;

		l2cap_send_cmd(conn, l2cap_get_ident(conn), L2CAP_CONF_REQ,
					l2cap_build_conf_req(sk, req), req);
		break;

	case L2CAP_CR_PEND:
		break;

	default:
		l2cap_chan_del(sk, ECONNREFUSED);
		break;
	}

	bh_unlock_sock(sk);
	return 0;
}

static inline int l2cap_config_req(struct l2cap_conn *conn, struct l2cap_cmd_hdr *cmd, u8 *data)
{
	struct l2cap_conf_req *req = (struct l2cap_conf_req *) data;
	u16 dcid, flags;
	u8 rsp[64];
	struct sock *sk;
	int result;

	dcid  = __le16_to_cpu(req->dcid);
	flags = __le16_to_cpu(req->flags);

	BT_DBG("dcid 0x%4.4x flags 0x%2.2x", dcid, flags);

	if (!(sk = l2cap_get_chan_by_scid(&conn->chan_list, dcid)))
		return -ENOENT;

	if (sk->sk_state == BT_DISCONN)
		goto unlock;

	l2cap_parse_conf_req(sk, req->data, cmd->len - sizeof(*req));

	if (flags & 0x0001) {
		/* Incomplete config. Send empty response. */
		l2cap_send_cmd(conn, cmd->ident, L2CAP_CONF_RSP,
				l2cap_build_conf_rsp(sk, rsp, NULL), rsp);
		goto unlock;
	}

	/* Complete config. */
	l2cap_send_cmd(conn, cmd->ident, L2CAP_CONF_RSP,
			l2cap_build_conf_rsp(sk, rsp, &result), rsp);

	if (result)
		goto unlock;

	/* Output config done */
	l2cap_pi(sk)->conf_state |= L2CAP_CONF_OUTPUT_DONE;

	if (l2cap_pi(sk)->conf_state & L2CAP_CONF_INPUT_DONE) {
		sk->sk_state = BT_CONNECTED;
		l2cap_chan_ready(sk);
	} else if (!(l2cap_pi(sk)->conf_state & L2CAP_CONF_REQ_SENT)) {
		u8 req[64];
		l2cap_send_cmd(conn, l2cap_get_ident(conn), L2CAP_CONF_REQ,
					l2cap_build_conf_req(sk, req), req);
	}

unlock:
	bh_unlock_sock(sk);
	return 0;
}

static inline int l2cap_config_rsp(struct l2cap_conn *conn, struct l2cap_cmd_hdr *cmd, u8 *data)
{
	struct l2cap_conf_rsp *rsp = (struct l2cap_conf_rsp *)data;
	u16 scid, flags, result;
	struct sock *sk;

	scid   = __le16_to_cpu(rsp->scid);
	flags  = __le16_to_cpu(rsp->flags);
	result = __le16_to_cpu(rsp->result);

	BT_DBG("scid 0x%4.4x flags 0x%2.2x result 0x%2.2x", scid, flags, result);

	if (!(sk = l2cap_get_chan_by_scid(&conn->chan_list, scid)))
		return 0;

	switch (result) {
	case L2CAP_CONF_SUCCESS:
		break;

	case L2CAP_CONF_UNACCEPT:
		if (++l2cap_pi(sk)->conf_retry < L2CAP_CONF_MAX_RETRIES) {
			char req[128];
			/* It does not make sense to adjust L2CAP parameters
			 * that are currently defined in the spec. We simply
			 * resend config request that we sent earlier. It is
			 * stupid, but it helps qualification testing which
			 * expects at least some response from us. */
			l2cap_send_cmd(conn, l2cap_get_ident(conn), L2CAP_CONF_REQ,
						l2cap_build_conf_req(sk, req), req);
			goto done;
		}

	default:
		sk->sk_state = BT_DISCONN;
		sk->sk_err   = ECONNRESET;
		l2cap_sock_set_timer(sk, HZ * 5);
		{
			struct l2cap_disconn_req req;
			req.dcid = __cpu_to_le16(l2cap_pi(sk)->dcid);
			req.scid = __cpu_to_le16(l2cap_pi(sk)->scid);
			l2cap_send_cmd(conn, l2cap_get_ident(conn),
					L2CAP_DISCONN_REQ, sizeof(req), &req);
		}
		goto done;
	}

	if (flags & 0x01)
		goto done;

	/* Input config done */
	l2cap_pi(sk)->conf_state |= L2CAP_CONF_INPUT_DONE;

	if (l2cap_pi(sk)->conf_state & L2CAP_CONF_OUTPUT_DONE) {
		sk->sk_state = BT_CONNECTED;
		l2cap_chan_ready(sk);
	}

done:
	bh_unlock_sock(sk);
	return 0;
}

static inline int l2cap_disconnect_req(struct l2cap_conn *conn, struct l2cap_cmd_hdr *cmd, u8 *data)
{
	struct l2cap_disconn_req *req = (struct l2cap_disconn_req *) data;
	struct l2cap_disconn_rsp rsp;
	u16 dcid, scid;
	struct sock *sk;

	scid = __le16_to_cpu(req->scid);
	dcid = __le16_to_cpu(req->dcid);

	BT_DBG("scid 0x%4.4x dcid 0x%4.4x", scid, dcid);

	if (!(sk = l2cap_get_chan_by_scid(&conn->chan_list, dcid)))
		return 0;

	rsp.dcid = __cpu_to_le16(l2cap_pi(sk)->scid);
	rsp.scid = __cpu_to_le16(l2cap_pi(sk)->dcid);
	l2cap_send_cmd(conn, cmd->ident, L2CAP_DISCONN_RSP, sizeof(rsp), &rsp);

	sk->sk_shutdown = SHUTDOWN_MASK;

	l2cap_chan_del(sk, ECONNRESET);
	bh_unlock_sock(sk);

	l2cap_sock_kill(sk);
	return 0;
}

static inline int l2cap_disconnect_rsp(struct l2cap_conn *conn, struct l2cap_cmd_hdr *cmd, u8 *data)
{
	struct l2cap_disconn_rsp *rsp = (struct l2cap_disconn_rsp *) data;
	u16 dcid, scid;
	struct sock *sk;

	scid = __le16_to_cpu(rsp->scid);
	dcid = __le16_to_cpu(rsp->dcid);

	BT_DBG("dcid 0x%4.4x scid 0x%4.4x", dcid, scid);

	if (!(sk = l2cap_get_chan_by_scid(&conn->chan_list, scid)))
		return 0;

	l2cap_chan_del(sk, 0);
	bh_unlock_sock(sk);

	l2cap_sock_kill(sk);
	return 0;
}

static inline int l2cap_information_req(struct l2cap_conn *conn, struct l2cap_cmd_hdr *cmd, u8 *data)
{
	struct l2cap_info_req *req = (struct l2cap_info_req *) data;
	struct l2cap_info_rsp rsp;
	u16 type;

	type = __le16_to_cpu(req->type);

	BT_DBG("type 0x%4.4x", type);

	rsp.type   = __cpu_to_le16(type);
	rsp.result = __cpu_to_le16(L2CAP_IR_NOTSUPP);
	l2cap_send_cmd(conn, cmd->ident, L2CAP_INFO_RSP, sizeof(rsp), &rsp);

	return 0;
}

static inline int l2cap_information_rsp(struct l2cap_conn *conn, struct l2cap_cmd_hdr *cmd, u8 *data)
{
	struct l2cap_info_rsp *rsp = (struct l2cap_info_rsp *) data;
	u16 type, result;

	type   = __le16_to_cpu(rsp->type);
	result = __le16_to_cpu(rsp->result);

	BT_DBG("type 0x%4.4x result 0x%2.2x", type, result);

	return 0;
}

static inline void l2cap_sig_channel(struct l2cap_conn *conn, struct sk_buff *skb)
{
	u8 *data = skb->data;
	int len = skb->len;
	struct l2cap_cmd_hdr cmd;
	int err = 0;

	l2cap_raw_recv(conn, skb);

	while (len >= L2CAP_CMD_HDR_SIZE) {
		memcpy(&cmd, data, L2CAP_CMD_HDR_SIZE);
		data += L2CAP_CMD_HDR_SIZE;
		len  -= L2CAP_CMD_HDR_SIZE;

		cmd.len = __le16_to_cpu(cmd.len);

		BT_DBG("code 0x%2.2x len %d id 0x%2.2x", cmd.code, cmd.len, cmd.ident);

		if (cmd.len > len || !cmd.ident) {
			BT_DBG("corrupted command");
			break;
		}

		switch (cmd.code) {
		case L2CAP_COMMAND_REJ:
			/* FIXME: We should process this */
			break;

		case L2CAP_CONN_REQ:
			err = l2cap_connect_req(conn, &cmd, data);
			break;

		case L2CAP_CONN_RSP:
			err = l2cap_connect_rsp(conn, &cmd, data);
			break;

		case L2CAP_CONF_REQ:
			err = l2cap_config_req(conn, &cmd, data);
			break;

		case L2CAP_CONF_RSP:
			err = l2cap_config_rsp(conn, &cmd, data);
			break;

		case L2CAP_DISCONN_REQ:
			err = l2cap_disconnect_req(conn, &cmd, data);
			break;

		case L2CAP_DISCONN_RSP:
			err = l2cap_disconnect_rsp(conn, &cmd, data);
			break;

		case L2CAP_ECHO_REQ:
			l2cap_send_cmd(conn, cmd.ident, L2CAP_ECHO_RSP, cmd.len, data);
			break;

		case L2CAP_ECHO_RSP:
			break;

		case L2CAP_INFO_REQ:
			err = l2cap_information_req(conn, &cmd, data);
			break;

		case L2CAP_INFO_RSP:
			err = l2cap_information_rsp(conn, &cmd, data);
			break;

		default:
			BT_ERR("Unknown signaling command 0x%2.2x", cmd.code);
			err = -EINVAL;
			break;
		}

		if (err) {
			struct l2cap_cmd_rej rej;
			BT_DBG("error %d", err);

			/* FIXME: Map err to a valid reason */
			rej.reason = __cpu_to_le16(0);
			l2cap_send_cmd(conn, cmd.ident, L2CAP_COMMAND_REJ, sizeof(rej), &rej);
		}

		data += cmd.len;
		len  -= cmd.len;
	}

	kfree_skb(skb);
}

static inline int l2cap_data_channel(struct l2cap_conn *conn, u16 cid, struct sk_buff *skb)
{
	struct sock *sk;

	sk = l2cap_get_chan_by_scid(&conn->chan_list, cid);
	if (!sk) {
		BT_DBG("unknown cid 0x%4.4x", cid);
		goto drop;
	}

	BT_DBG("sk %p, len %d", sk, skb->len);

	if (sk->sk_state != BT_CONNECTED)
		goto drop;

	if (l2cap_pi(sk)->imtu < skb->len)
		goto drop;

	/* If socket recv buffers overflows we drop data here
	 * which is *bad* because L2CAP has to be reliable.
	 * But we don't have any other choice. L2CAP doesn't
	 * provide flow control mechanism. */

	if (!sock_queue_rcv_skb(sk, skb))
		goto done;

drop:
	kfree_skb(skb);

done:
	if (sk)
		bh_unlock_sock(sk);

	return 0;
}

static inline int l2cap_conless_channel(struct l2cap_conn *conn, u16 psm, struct sk_buff *skb)
{
	struct sock *sk;

	sk = l2cap_get_sock_by_psm(0, psm, conn->src);
	if (!sk)
		goto drop;

	BT_DBG("sk %p, len %d", sk, skb->len);

	if (sk->sk_state != BT_BOUND && sk->sk_state != BT_CONNECTED)
		goto drop;

	if (l2cap_pi(sk)->imtu < skb->len)
		goto drop;

	if (!sock_queue_rcv_skb(sk, skb))
		goto done;

drop:
	kfree_skb(skb);

done:
	if (sk) bh_unlock_sock(sk);
	return 0;
}

static void l2cap_recv_frame(struct l2cap_conn *conn, struct sk_buff *skb)
{
	struct l2cap_hdr *lh = (void *) skb->data;
	u16 cid, psm, len;

	skb_pull(skb, L2CAP_HDR_SIZE);
	cid = __le16_to_cpu(lh->cid);
	len = __le16_to_cpu(lh->len);

	BT_DBG("len %d, cid 0x%4.4x", len, cid);

	switch (cid) {
	case 0x0001:
		l2cap_sig_channel(conn, skb);
		break;

	case 0x0002:
		psm = get_unaligned((u16 *) skb->data);
		skb_pull(skb, 2);
		l2cap_conless_channel(conn, psm, skb);
		break;

	default:
		l2cap_data_channel(conn, cid, skb);
		break;
	}
}

/* ---- L2CAP interface with lower layer (HCI) ---- */

static int l2cap_connect_ind(struct hci_dev *hdev, bdaddr_t *bdaddr, u8 type)
{
	int exact = 0, lm1 = 0, lm2 = 0;
	register struct sock *sk;
	struct hlist_node *node;

	if (type != ACL_LINK)
		return 0;

	BT_DBG("hdev %s, bdaddr %s", hdev->name, batostr(bdaddr));

	/* Find listening sockets and check their link_mode */
	read_lock(&l2cap_sk_list.lock);
	sk_for_each(sk, node, &l2cap_sk_list.head) {
		if (sk->sk_state != BT_LISTEN)
			continue;

		if (!bacmp(&bt_sk(sk)->src, &hdev->bdaddr)) {
			lm1 |= (HCI_LM_ACCEPT | l2cap_pi(sk)->link_mode);
			exact++;
		} else if (!bacmp(&bt_sk(sk)->src, BDADDR_ANY))
			lm2 |= (HCI_LM_ACCEPT | l2cap_pi(sk)->link_mode);
	}
	read_unlock(&l2cap_sk_list.lock);

	return exact ? lm1 : lm2;
}

static int l2cap_connect_cfm(struct hci_conn *hcon, u8 status)
{
	struct l2cap_conn *conn;

	BT_DBG("hcon %p bdaddr %s status %d", hcon, batostr(&hcon->dst), status);

	if (hcon->type != ACL_LINK)
		return 0;

	if (!status) {
		conn = l2cap_conn_add(hcon, status);
		if (conn)
			l2cap_conn_ready(conn);
	} else
		l2cap_conn_del(hcon, bt_err(status));

	return 0;
}

static int l2cap_disconn_ind(struct hci_conn *hcon, u8 reason)
{
	BT_DBG("hcon %p reason %d", hcon, reason);

	if (hcon->type != ACL_LINK)
		return 0;

	l2cap_conn_del(hcon, bt_err(reason));

	return 0;
}

static int l2cap_auth_cfm(struct hci_conn *hcon, u8 status)
{
	struct l2cap_chan_list *l;
	struct l2cap_conn *conn = conn = hcon->l2cap_data;
	struct l2cap_conn_rsp rsp;
	struct sock *sk;
	int result;

	if (!conn)
		return 0;

	l = &conn->chan_list;

	BT_DBG("conn %p", conn);

	read_lock(&l->lock);

	for (sk = l->head; sk; sk = l2cap_pi(sk)->next_c) {
		bh_lock_sock(sk);

		if (sk->sk_state != BT_CONNECT2 ||
				(l2cap_pi(sk)->link_mode & L2CAP_LM_ENCRYPT) ||
				(l2cap_pi(sk)->link_mode & L2CAP_LM_SECURE)) {
			bh_unlock_sock(sk);
			continue;
		}

		if (!status) {
			sk->sk_state = BT_CONFIG;
			result = 0;
		} else {
			sk->sk_state = BT_DISCONN;
			l2cap_sock_set_timer(sk, HZ/10);
			result = L2CAP_CR_SEC_BLOCK;
		}

		rsp.scid   = __cpu_to_le16(l2cap_pi(sk)->dcid);
		rsp.dcid   = __cpu_to_le16(l2cap_pi(sk)->scid);
		rsp.result = __cpu_to_le16(result);
		rsp.status = __cpu_to_le16(0);
		l2cap_send_cmd(conn, l2cap_pi(sk)->ident,
				L2CAP_CONN_RSP, sizeof(rsp), &rsp);

		bh_unlock_sock(sk);
	}

	read_unlock(&l->lock);
	return 0;
}

static int l2cap_encrypt_cfm(struct hci_conn *hcon, u8 status)
{
	struct l2cap_chan_list *l;
	struct l2cap_conn *conn = hcon->l2cap_data;
	struct l2cap_conn_rsp rsp;
	struct sock *sk;
	int result;

	if (!conn)
		return 0;

	l = &conn->chan_list;

	BT_DBG("conn %p", conn);

	read_lock(&l->lock);

	for (sk = l->head; sk; sk = l2cap_pi(sk)->next_c) {
		bh_lock_sock(sk);

		if (sk->sk_state != BT_CONNECT2) {
			bh_unlock_sock(sk);
			continue;
		}

		if (!status) {
			sk->sk_state = BT_CONFIG;
			result = 0;
		} else {
			sk->sk_state = BT_DISCONN;
			l2cap_sock_set_timer(sk, HZ/10);
			result = L2CAP_CR_SEC_BLOCK;
		}

		rsp.scid   = __cpu_to_le16(l2cap_pi(sk)->dcid);
		rsp.dcid   = __cpu_to_le16(l2cap_pi(sk)->scid);
		rsp.result = __cpu_to_le16(result);
		rsp.status = __cpu_to_le16(0);
		l2cap_send_cmd(conn, l2cap_pi(sk)->ident,
				L2CAP_CONN_RSP, sizeof(rsp), &rsp);

		if (l2cap_pi(sk)->link_mode & L2CAP_LM_SECURE)
			hci_conn_change_link_key(hcon);

		bh_unlock_sock(sk);
	}

	read_unlock(&l->lock);
	return 0;
}

static int l2cap_recv_acldata(struct hci_conn *hcon, struct sk_buff *skb, u16 flags)
{
	struct l2cap_conn *conn = hcon->l2cap_data;

	if (!conn && !(conn = l2cap_conn_add(hcon, 0)))
		goto drop;

	BT_DBG("conn %p len %d flags 0x%x", conn, skb->len, flags);

	if (flags & ACL_START) {
		struct l2cap_hdr *hdr;
		int len;

		if (conn->rx_len) {
			BT_ERR("Unexpected start frame (len %d)", skb->len);
			kfree_skb(conn->rx_skb);
			conn->rx_skb = NULL;
			conn->rx_len = 0;
			l2cap_conn_unreliable(conn, ECOMM);
		}

		if (skb->len < 2) {
			BT_ERR("Frame is too short (len %d)", skb->len);
			l2cap_conn_unreliable(conn, ECOMM);
			goto drop;
		}

		hdr = (struct l2cap_hdr *) skb->data;
		len = __le16_to_cpu(hdr->len) + L2CAP_HDR_SIZE;

		if (len == skb->len) {
			/* Complete frame received */
			l2cap_recv_frame(conn, skb);
			return 0;
		}

		BT_DBG("Start: total len %d, frag len %d", len, skb->len);

		if (skb->len > len) {
			BT_ERR("Frame is too long (len %d, expected len %d)",
				skb->len, len);
			l2cap_conn_unreliable(conn, ECOMM);
			goto drop;
		}

		/* Allocate skb for the complete frame (with header) */
		if (!(conn->rx_skb = bt_skb_alloc(len, GFP_ATOMIC)))
			goto drop;

		memcpy(skb_put(conn->rx_skb, skb->len), skb->data, skb->len);
		conn->rx_len = len - skb->len;
	} else {
		BT_DBG("Cont: frag len %d (expecting %d)", skb->len, conn->rx_len);

		if (!conn->rx_len) {
			BT_ERR("Unexpected continuation frame (len %d)", skb->len);
			l2cap_conn_unreliable(conn, ECOMM);
			goto drop;
		}

		if (skb->len > conn->rx_len) {
			BT_ERR("Fragment is too long (len %d, expected %d)",
					skb->len, conn->rx_len);
			kfree_skb(conn->rx_skb);
			conn->rx_skb = NULL;
			conn->rx_len = 0;
			l2cap_conn_unreliable(conn, ECOMM);
			goto drop;
		}

		memcpy(skb_put(conn->rx_skb, skb->len), skb->data, skb->len);
		conn->rx_len -= skb->len;

		if (!conn->rx_len) {
			/* Complete frame received */
			l2cap_recv_frame(conn, conn->rx_skb);
			conn->rx_skb = NULL;
		}
	}

drop:
	kfree_skb(skb);
	return 0;
}

static ssize_t l2cap_sysfs_show(struct class *dev, char *buf)
{
	struct sock *sk;
	struct hlist_node *node;
	char *str = buf;

	read_lock_bh(&l2cap_sk_list.lock);

	sk_for_each(sk, node, &l2cap_sk_list.head) {
		struct l2cap_pinfo *pi = l2cap_pi(sk);

		str += sprintf(str, "%s %s %d %d 0x%4.4x 0x%4.4x %d %d 0x%x\n",
				batostr(&bt_sk(sk)->src), batostr(&bt_sk(sk)->dst),
				sk->sk_state, btohs(pi->psm), pi->scid, pi->dcid,
				pi->imtu, pi->omtu, pi->link_mode);
	}

	read_unlock_bh(&l2cap_sk_list.lock);

	return (str - buf);
}

static CLASS_ATTR(l2cap, S_IRUGO, l2cap_sysfs_show, NULL);

static const struct proto_ops l2cap_sock_ops = {
	.family		= PF_BLUETOOTH,
	.owner		= THIS_MODULE,
	.release	= l2cap_sock_release,
	.bind		= l2cap_sock_bind,
	.connect	= l2cap_sock_connect,
	.listen		= l2cap_sock_listen,
	.accept		= l2cap_sock_accept,
	.getname	= l2cap_sock_getname,
	.sendmsg	= l2cap_sock_sendmsg,
	.recvmsg	= bt_sock_recvmsg,
	.poll		= bt_sock_poll,
	.mmap		= sock_no_mmap,
	.socketpair	= sock_no_socketpair,
	.ioctl		= sock_no_ioctl,
	.shutdown	= l2cap_sock_shutdown,
	.setsockopt	= l2cap_sock_setsockopt,
	.getsockopt	= l2cap_sock_getsockopt
};

static struct net_proto_family l2cap_sock_family_ops = {
	.family	= PF_BLUETOOTH,
	.owner	= THIS_MODULE,
	.create	= l2cap_sock_create,
};

static struct hci_proto l2cap_hci_proto = {
	.name		= "L2CAP",
	.id		= HCI_PROTO_L2CAP,
	.connect_ind	= l2cap_connect_ind,
	.connect_cfm	= l2cap_connect_cfm,
	.disconn_ind	= l2cap_disconn_ind,
	.auth_cfm	= l2cap_auth_cfm,
	.encrypt_cfm	= l2cap_encrypt_cfm,
	.recv_acldata	= l2cap_recv_acldata
};

static int __init l2cap_init(void)
{
	int err;

	err = proto_register(&l2cap_proto, 0);
	if (err < 0)
		return err;

	err = bt_sock_register(BTPROTO_L2CAP, &l2cap_sock_family_ops);
	if (err < 0) {
		BT_ERR("L2CAP socket registration failed");
		goto error;
	}

	err = hci_register_proto(&l2cap_hci_proto);
	if (err < 0) {
		BT_ERR("L2CAP protocol registration failed");
		bt_sock_unregister(BTPROTO_L2CAP);
		goto error;
	}

	if (class_create_file(bt_class, &class_attr_l2cap) < 0)
		BT_ERR("Failed to create L2CAP info file");

	BT_INFO("L2CAP ver %s", VERSION);
	BT_INFO("L2CAP socket layer initialized");

	return 0;

error:
	proto_unregister(&l2cap_proto);
	return err;
}

static void __exit l2cap_exit(void)
{
	class_remove_file(bt_class, &class_attr_l2cap);

	if (bt_sock_unregister(BTPROTO_L2CAP) < 0)
		BT_ERR("L2CAP socket unregistration failed");

	if (hci_unregister_proto(&l2cap_hci_proto) < 0)
		BT_ERR("L2CAP protocol unregistration failed");

	proto_unregister(&l2cap_proto);
}

void l2cap_load(void)
{
	/* Dummy function to trigger automatic L2CAP module loading by
	 * other modules that use L2CAP sockets but don't use any other
	 * symbols from it. */
	return;
}
EXPORT_SYMBOL(l2cap_load);

module_init(l2cap_init);
module_exit(l2cap_exit);

MODULE_AUTHOR("Maxim Krasnyansky <maxk@qualcomm.com>, Marcel Holtmann <marcel@holtmann.org>");
MODULE_DESCRIPTION("Bluetooth L2CAP ver " VERSION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");
MODULE_ALIAS("bt-proto-0");
