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
#include <linux/uaccess.h>
#include <linux/crc16.h>
#include <net/sock.h>

#include <asm/system.h>
#include <asm/unaligned.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include <net/bluetooth/l2cap.h>

#define VERSION "2.14"

static int enable_ertm = 0;
static int max_transmit = L2CAP_DEFAULT_MAX_TX;

static u32 l2cap_feat_mask = L2CAP_FEAT_FIXED_CHAN;
static u8 l2cap_fixed_chan[8] = { 0x02, };

static const struct proto_ops l2cap_sock_ops;

static struct bt_sock_list l2cap_sk_list = {
	.lock = __RW_LOCK_UNLOCKED(l2cap_sk_list.lock)
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
	int reason;

	BT_DBG("sock %p state %d", sk, sk->sk_state);

	bh_lock_sock(sk);

	if (sk->sk_state == BT_CONNECTED || sk->sk_state == BT_CONFIG)
		reason = ECONNREFUSED;
	else if (sk->sk_state == BT_CONNECT &&
				l2cap_pi(sk)->sec_level != BT_SECURITY_SDP)
		reason = ECONNREFUSED;
	else
		reason = ETIMEDOUT;

	__l2cap_sock_close(sk, reason);

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
	if (s)
		bh_lock_sock(s);
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
	if (s)
		bh_lock_sock(s);
	read_unlock(&l->lock);
	return s;
}

static u16 l2cap_alloc_cid(struct l2cap_chan_list *l)
{
	u16 cid = L2CAP_CID_DYN_START;

	for (; cid < L2CAP_CID_DYN_END; cid++) {
		if (!__l2cap_get_chan_by_scid(l, cid))
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

	BT_DBG("conn %p, psm 0x%2.2x, dcid 0x%4.4x", conn,
			l2cap_pi(sk)->psm, l2cap_pi(sk)->dcid);

	conn->disc_reason = 0x13;

	l2cap_pi(sk)->conn = conn;

	if (sk->sk_type == SOCK_SEQPACKET) {
		/* Alloc CID for connection-oriented socket */
		l2cap_pi(sk)->scid = l2cap_alloc_cid(l);
	} else if (sk->sk_type == SOCK_DGRAM) {
		/* Connectionless socket */
		l2cap_pi(sk)->scid = L2CAP_CID_CONN_LESS;
		l2cap_pi(sk)->dcid = L2CAP_CID_CONN_LESS;
		l2cap_pi(sk)->omtu = L2CAP_DEFAULT_MTU;
	} else {
		/* Raw socket can send/recv signalling messages only */
		l2cap_pi(sk)->scid = L2CAP_CID_SIGNALING;
		l2cap_pi(sk)->dcid = L2CAP_CID_SIGNALING;
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

	sk->sk_state = BT_CLOSED;
	sock_set_flag(sk, SOCK_ZAPPED);

	if (err)
		sk->sk_err = err;

	if (parent) {
		bt_accept_unlink(sk);
		parent->sk_data_ready(parent, 0);
	} else
		sk->sk_state_change(sk);
}

/* Service level security */
static inline int l2cap_check_security(struct sock *sk)
{
	struct l2cap_conn *conn = l2cap_pi(sk)->conn;
	__u8 auth_type;

	if (l2cap_pi(sk)->psm == cpu_to_le16(0x0001)) {
		if (l2cap_pi(sk)->sec_level == BT_SECURITY_HIGH)
			auth_type = HCI_AT_NO_BONDING_MITM;
		else
			auth_type = HCI_AT_NO_BONDING;

		if (l2cap_pi(sk)->sec_level == BT_SECURITY_LOW)
			l2cap_pi(sk)->sec_level = BT_SECURITY_SDP;
	} else {
		switch (l2cap_pi(sk)->sec_level) {
		case BT_SECURITY_HIGH:
			auth_type = HCI_AT_GENERAL_BONDING_MITM;
			break;
		case BT_SECURITY_MEDIUM:
			auth_type = HCI_AT_GENERAL_BONDING;
			break;
		default:
			auth_type = HCI_AT_NO_BONDING;
			break;
		}
	}

	return hci_conn_security(conn->hcon, l2cap_pi(sk)->sec_level,
								auth_type);
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

static inline int l2cap_send_sframe(struct l2cap_pinfo *pi, u16 control)
{
	struct sk_buff *skb;
	struct l2cap_hdr *lh;
	struct l2cap_conn *conn = pi->conn;
	int count, hlen = L2CAP_HDR_SIZE + 2;

	if (pi->fcs == L2CAP_FCS_CRC16)
		hlen += 2;

	BT_DBG("pi %p, control 0x%2.2x", pi, control);

	count = min_t(unsigned int, conn->mtu, hlen);
	control |= L2CAP_CTRL_FRAME_TYPE;

	skb = bt_skb_alloc(count, GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	lh = (struct l2cap_hdr *) skb_put(skb, L2CAP_HDR_SIZE);
	lh->len = cpu_to_le16(hlen - L2CAP_HDR_SIZE);
	lh->cid = cpu_to_le16(pi->dcid);
	put_unaligned_le16(control, skb_put(skb, 2));

	if (pi->fcs == L2CAP_FCS_CRC16) {
		u16 fcs = crc16(0, (u8 *)lh, count - 2);
		put_unaligned_le16(fcs, skb_put(skb, 2));
	}

	return hci_send_acl(pi->conn->hcon, skb, 0);
}

static inline int l2cap_send_rr_or_rnr(struct l2cap_pinfo *pi, u16 control)
{
	if (pi->conn_state & L2CAP_CONN_LOCAL_BUSY)
		control |= L2CAP_SUPER_RCV_NOT_READY;
	else
		control |= L2CAP_SUPER_RCV_READY;

	control |= pi->buffer_seq << L2CAP_CTRL_REQSEQ_SHIFT;

	return l2cap_send_sframe(pi, control);
}

static void l2cap_do_start(struct sock *sk)
{
	struct l2cap_conn *conn = l2cap_pi(sk)->conn;

	if (conn->info_state & L2CAP_INFO_FEAT_MASK_REQ_SENT) {
		if (!(conn->info_state & L2CAP_INFO_FEAT_MASK_REQ_DONE))
			return;

		if (l2cap_check_security(sk)) {
			struct l2cap_conn_req req;
			req.scid = cpu_to_le16(l2cap_pi(sk)->scid);
			req.psm  = l2cap_pi(sk)->psm;

			l2cap_pi(sk)->ident = l2cap_get_ident(conn);

			l2cap_send_cmd(conn, l2cap_pi(sk)->ident,
					L2CAP_CONN_REQ, sizeof(req), &req);
		}
	} else {
		struct l2cap_info_req req;
		req.type = cpu_to_le16(L2CAP_IT_FEAT_MASK);

		conn->info_state |= L2CAP_INFO_FEAT_MASK_REQ_SENT;
		conn->info_ident = l2cap_get_ident(conn);

		mod_timer(&conn->info_timer, jiffies +
					msecs_to_jiffies(L2CAP_INFO_TIMEOUT));

		l2cap_send_cmd(conn, conn->info_ident,
					L2CAP_INFO_REQ, sizeof(req), &req);
	}
}

static void l2cap_send_disconn_req(struct l2cap_conn *conn, struct sock *sk)
{
	struct l2cap_disconn_req req;

	req.dcid = cpu_to_le16(l2cap_pi(sk)->dcid);
	req.scid = cpu_to_le16(l2cap_pi(sk)->scid);
	l2cap_send_cmd(conn, l2cap_get_ident(conn),
			L2CAP_DISCONN_REQ, sizeof(req), &req);
}

/* ---- L2CAP connections ---- */
static void l2cap_conn_start(struct l2cap_conn *conn)
{
	struct l2cap_chan_list *l = &conn->chan_list;
	struct sock *sk;

	BT_DBG("conn %p", conn);

	read_lock(&l->lock);

	for (sk = l->head; sk; sk = l2cap_pi(sk)->next_c) {
		bh_lock_sock(sk);

		if (sk->sk_type != SOCK_SEQPACKET) {
			bh_unlock_sock(sk);
			continue;
		}

		if (sk->sk_state == BT_CONNECT) {
			if (l2cap_check_security(sk)) {
				struct l2cap_conn_req req;
				req.scid = cpu_to_le16(l2cap_pi(sk)->scid);
				req.psm  = l2cap_pi(sk)->psm;

				l2cap_pi(sk)->ident = l2cap_get_ident(conn);

				l2cap_send_cmd(conn, l2cap_pi(sk)->ident,
					L2CAP_CONN_REQ, sizeof(req), &req);
			}
		} else if (sk->sk_state == BT_CONNECT2) {
			struct l2cap_conn_rsp rsp;
			rsp.scid = cpu_to_le16(l2cap_pi(sk)->dcid);
			rsp.dcid = cpu_to_le16(l2cap_pi(sk)->scid);

			if (l2cap_check_security(sk)) {
				if (bt_sk(sk)->defer_setup) {
					struct sock *parent = bt_sk(sk)->parent;
					rsp.result = cpu_to_le16(L2CAP_CR_PEND);
					rsp.status = cpu_to_le16(L2CAP_CS_AUTHOR_PEND);
					parent->sk_data_ready(parent, 0);

				} else {
					sk->sk_state = BT_CONFIG;
					rsp.result = cpu_to_le16(L2CAP_CR_SUCCESS);
					rsp.status = cpu_to_le16(L2CAP_CS_NO_INFO);
				}
			} else {
				rsp.result = cpu_to_le16(L2CAP_CR_PEND);
				rsp.status = cpu_to_le16(L2CAP_CS_AUTHEN_PEND);
			}

			l2cap_send_cmd(conn, l2cap_pi(sk)->ident,
					L2CAP_CONN_RSP, sizeof(rsp), &rsp);
		}

		bh_unlock_sock(sk);
	}

	read_unlock(&l->lock);
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
		} else if (sk->sk_state == BT_CONNECT)
			l2cap_do_start(sk);

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
		if (l2cap_pi(sk)->force_reliable)
			sk->sk_err = err;
	}

	read_unlock(&l->lock);
}

static void l2cap_info_timeout(unsigned long arg)
{
	struct l2cap_conn *conn = (void *) arg;

	conn->info_state |= L2CAP_INFO_FEAT_MASK_REQ_DONE;
	conn->info_ident = 0;

	l2cap_conn_start(conn);
}

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

	conn->feat_mask = 0;

	spin_lock_init(&conn->lock);
	rwlock_init(&conn->chan_list.lock);

	setup_timer(&conn->info_timer, l2cap_info_timeout,
						(unsigned long) conn);

	conn->disc_reason = 0x13;

	return conn;
}

static void l2cap_conn_del(struct hci_conn *hcon, int err)
{
	struct l2cap_conn *conn = hcon->l2cap_data;
	struct sock *sk;

	if (!conn)
		return;

	BT_DBG("hcon %p conn %p, err %d", hcon, conn, err);

	kfree_skb(conn->rx_skb);

	/* Kill channels */
	while ((sk = conn->chan_list.head)) {
		bh_lock_sock(sk);
		l2cap_chan_del(sk, err);
		bh_unlock_sock(sk);
		l2cap_sock_kill(sk);
	}

	if (conn->info_state & L2CAP_INFO_FEAT_MASK_REQ_SENT)
		del_timer_sync(&conn->info_timer);

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

/* ---- Socket interface ---- */
static struct sock *__l2cap_get_sock_by_addr(__le16 psm, bdaddr_t *src)
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
static struct sock *__l2cap_get_sock_by_psm(int state, __le16 psm, bdaddr_t *src)
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
static inline struct sock *l2cap_get_sock_by_psm(int state, __le16 psm, bdaddr_t *src)
{
	struct sock *s;
	read_lock(&l2cap_sk_list.lock);
	s = __l2cap_get_sock_by_psm(state, psm, src);
	if (s)
		bh_lock_sock(s);
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

	parent->sk_state = BT_CLOSED;
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
		if (sk->sk_type == SOCK_SEQPACKET) {
			struct l2cap_conn *conn = l2cap_pi(sk)->conn;

			sk->sk_state = BT_DISCONN;
			l2cap_sock_set_timer(sk, sk->sk_sndtimeo);
			l2cap_send_disconn_req(conn, sk);
		} else
			l2cap_chan_del(sk, reason);
		break;

	case BT_CONNECT2:
		if (sk->sk_type == SOCK_SEQPACKET) {
			struct l2cap_conn *conn = l2cap_pi(sk)->conn;
			struct l2cap_conn_rsp rsp;
			__u16 result;

			if (bt_sk(sk)->defer_setup)
				result = L2CAP_CR_SEC_BLOCK;
			else
				result = L2CAP_CR_BAD_PSM;

			rsp.scid   = cpu_to_le16(l2cap_pi(sk)->dcid);
			rsp.dcid   = cpu_to_le16(l2cap_pi(sk)->scid);
			rsp.result = cpu_to_le16(result);
			rsp.status = cpu_to_le16(L2CAP_CS_NO_INFO);
			l2cap_send_cmd(conn, l2cap_pi(sk)->ident,
					L2CAP_CONN_RSP, sizeof(rsp), &rsp);
		} else
			l2cap_chan_del(sk, reason);
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
		bt_sk(sk)->defer_setup = bt_sk(parent)->defer_setup;

		pi->imtu = l2cap_pi(parent)->imtu;
		pi->omtu = l2cap_pi(parent)->omtu;
		pi->mode = l2cap_pi(parent)->mode;
		pi->fcs  = l2cap_pi(parent)->fcs;
		pi->sec_level = l2cap_pi(parent)->sec_level;
		pi->role_switch = l2cap_pi(parent)->role_switch;
		pi->force_reliable = l2cap_pi(parent)->force_reliable;
	} else {
		pi->imtu = L2CAP_DEFAULT_MTU;
		pi->omtu = 0;
		pi->mode = L2CAP_MODE_BASIC;
		pi->fcs  = L2CAP_FCS_CRC16;
		pi->sec_level = BT_SECURITY_LOW;
		pi->role_switch = 0;
		pi->force_reliable = 0;
	}

	/* Default config options */
	pi->conf_len = 0;
	pi->flush_to = L2CAP_DEFAULT_FLUSH_TO;
	skb_queue_head_init(TX_QUEUE(sk));
	skb_queue_head_init(SREJ_QUEUE(sk));
	INIT_LIST_HEAD(SREJ_LIST(sk));
}

static struct proto l2cap_proto = {
	.name		= "L2CAP",
	.owner		= THIS_MODULE,
	.obj_size	= sizeof(struct l2cap_pinfo)
};

static struct sock *l2cap_sock_alloc(struct net *net, struct socket *sock, int proto, gfp_t prio)
{
	struct sock *sk;

	sk = sk_alloc(net, PF_BLUETOOTH, prio, &l2cap_proto);
	if (!sk)
		return NULL;

	sock_init_data(sock, sk);
	INIT_LIST_HEAD(&bt_sk(sk)->accept_q);

	sk->sk_destruct = l2cap_sock_destruct;
	sk->sk_sndtimeo = msecs_to_jiffies(L2CAP_CONN_TIMEOUT);

	sock_reset_flag(sk, SOCK_ZAPPED);

	sk->sk_protocol = proto;
	sk->sk_state = BT_OPEN;

	setup_timer(&sk->sk_timer, l2cap_sock_timeout, (unsigned long) sk);

	bt_sock_link(&l2cap_sk_list, sk);
	return sk;
}

static int l2cap_sock_create(struct net *net, struct socket *sock, int protocol,
			     int kern)
{
	struct sock *sk;

	BT_DBG("sock %p", sock);

	sock->state = SS_UNCONNECTED;

	if (sock->type != SOCK_SEQPACKET &&
			sock->type != SOCK_DGRAM && sock->type != SOCK_RAW)
		return -ESOCKTNOSUPPORT;

	if (sock->type == SOCK_RAW && !kern && !capable(CAP_NET_RAW))
		return -EPERM;

	sock->ops = &l2cap_sock_ops;

	sk = l2cap_sock_alloc(net, sock, protocol, GFP_ATOMIC);
	if (!sk)
		return -ENOMEM;

	l2cap_sock_init(sk, NULL);
	return 0;
}

static int l2cap_sock_bind(struct socket *sock, struct sockaddr *addr, int alen)
{
	struct sock *sk = sock->sk;
	struct sockaddr_l2 la;
	int len, err = 0;

	BT_DBG("sk %p", sk);

	if (!addr || addr->sa_family != AF_BLUETOOTH)
		return -EINVAL;

	memset(&la, 0, sizeof(la));
	len = min_t(unsigned int, sizeof(la), alen);
	memcpy(&la, addr, len);

	if (la.l2_cid)
		return -EINVAL;

	lock_sock(sk);

	if (sk->sk_state != BT_OPEN) {
		err = -EBADFD;
		goto done;
	}

	if (la.l2_psm && __le16_to_cpu(la.l2_psm) < 0x1001 &&
				!capable(CAP_NET_BIND_SERVICE)) {
		err = -EACCES;
		goto done;
	}

	write_lock_bh(&l2cap_sk_list.lock);

	if (la.l2_psm && __l2cap_get_sock_by_addr(la.l2_psm, &la.l2_bdaddr)) {
		err = -EADDRINUSE;
	} else {
		/* Save source address */
		bacpy(&bt_sk(sk)->src, &la.l2_bdaddr);
		l2cap_pi(sk)->psm   = la.l2_psm;
		l2cap_pi(sk)->sport = la.l2_psm;
		sk->sk_state = BT_BOUND;

		if (__le16_to_cpu(la.l2_psm) == 0x0001 ||
					__le16_to_cpu(la.l2_psm) == 0x0003)
			l2cap_pi(sk)->sec_level = BT_SECURITY_SDP;
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
	__u8 auth_type;
	int err;

	BT_DBG("%s -> %s psm 0x%2.2x", batostr(src), batostr(dst),
							l2cap_pi(sk)->psm);

	hdev = hci_get_route(dst, src);
	if (!hdev)
		return -EHOSTUNREACH;

	hci_dev_lock_bh(hdev);

	err = -ENOMEM;

	if (sk->sk_type == SOCK_RAW) {
		switch (l2cap_pi(sk)->sec_level) {
		case BT_SECURITY_HIGH:
			auth_type = HCI_AT_DEDICATED_BONDING_MITM;
			break;
		case BT_SECURITY_MEDIUM:
			auth_type = HCI_AT_DEDICATED_BONDING;
			break;
		default:
			auth_type = HCI_AT_NO_BONDING;
			break;
		}
	} else if (l2cap_pi(sk)->psm == cpu_to_le16(0x0001)) {
		if (l2cap_pi(sk)->sec_level == BT_SECURITY_HIGH)
			auth_type = HCI_AT_NO_BONDING_MITM;
		else
			auth_type = HCI_AT_NO_BONDING;

		if (l2cap_pi(sk)->sec_level == BT_SECURITY_LOW)
			l2cap_pi(sk)->sec_level = BT_SECURITY_SDP;
	} else {
		switch (l2cap_pi(sk)->sec_level) {
		case BT_SECURITY_HIGH:
			auth_type = HCI_AT_GENERAL_BONDING_MITM;
			break;
		case BT_SECURITY_MEDIUM:
			auth_type = HCI_AT_GENERAL_BONDING;
			break;
		default:
			auth_type = HCI_AT_NO_BONDING;
			break;
		}
	}

	hcon = hci_connect(hdev, ACL_LINK, dst,
					l2cap_pi(sk)->sec_level, auth_type);
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
		if (sk->sk_type != SOCK_SEQPACKET) {
			l2cap_sock_clear_timer(sk);
			sk->sk_state = BT_CONNECTED;
		} else
			l2cap_do_start(sk);
	}

done:
	hci_dev_unlock_bh(hdev);
	hci_dev_put(hdev);
	return err;
}

static int l2cap_sock_connect(struct socket *sock, struct sockaddr *addr, int alen, int flags)
{
	struct sock *sk = sock->sk;
	struct sockaddr_l2 la;
	int len, err = 0;

	BT_DBG("sk %p", sk);

	if (!addr || addr->sa_family != AF_BLUETOOTH)
		return -EINVAL;

	memset(&la, 0, sizeof(la));
	len = min_t(unsigned int, sizeof(la), alen);
	memcpy(&la, addr, len);

	if (la.l2_cid)
		return -EINVAL;

	lock_sock(sk);

	if (sk->sk_type == SOCK_SEQPACKET && !la.l2_psm) {
		err = -EINVAL;
		goto done;
	}

	switch (l2cap_pi(sk)->mode) {
	case L2CAP_MODE_BASIC:
		break;
	case L2CAP_MODE_ERTM:
	case L2CAP_MODE_STREAMING:
		if (enable_ertm)
			break;
		/* fall through */
	default:
		err = -ENOTSUPP;
		goto done;
	}

	switch (sk->sk_state) {
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
	bacpy(&bt_sk(sk)->dst, &la.l2_bdaddr);
	l2cap_pi(sk)->psm = la.l2_psm;

	err = l2cap_do_connect(sk);
	if (err)
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

	switch (l2cap_pi(sk)->mode) {
	case L2CAP_MODE_BASIC:
		break;
	case L2CAP_MODE_ERTM:
	case L2CAP_MODE_STREAMING:
		if (enable_ertm)
			break;
		/* fall through */
	default:
		err = -ENOTSUPP;
		goto done;
	}

	if (!l2cap_pi(sk)->psm) {
		bdaddr_t *src = &bt_sk(sk)->src;
		u16 psm;

		err = -EINVAL;

		write_lock_bh(&l2cap_sk_list.lock);

		for (psm = 0x1001; psm < 0x1100; psm += 2)
			if (!__l2cap_get_sock_by_addr(cpu_to_le16(psm), src)) {
				l2cap_pi(sk)->psm   = cpu_to_le16(psm);
				l2cap_pi(sk)->sport = cpu_to_le16(psm);
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

	if (peer) {
		la->l2_psm = l2cap_pi(sk)->psm;
		bacpy(&la->l2_bdaddr, &bt_sk(sk)->dst);
		la->l2_cid = cpu_to_le16(l2cap_pi(sk)->dcid);
	} else {
		la->l2_psm = l2cap_pi(sk)->sport;
		bacpy(&la->l2_bdaddr, &bt_sk(sk)->src);
		la->l2_cid = cpu_to_le16(l2cap_pi(sk)->scid);
	}

	return 0;
}

static void l2cap_monitor_timeout(unsigned long arg)
{
	struct sock *sk = (void *) arg;
	u16 control;

	bh_lock_sock(sk);
	if (l2cap_pi(sk)->retry_count >= l2cap_pi(sk)->remote_max_tx) {
		l2cap_send_disconn_req(l2cap_pi(sk)->conn, sk);
		bh_unlock_sock(sk);
		return;
	}

	l2cap_pi(sk)->retry_count++;
	__mod_monitor_timer();

	control = L2CAP_CTRL_POLL;
	l2cap_send_rr_or_rnr(l2cap_pi(sk), control);
	bh_unlock_sock(sk);
}

static void l2cap_retrans_timeout(unsigned long arg)
{
	struct sock *sk = (void *) arg;
	u16 control;

	bh_lock_sock(sk);
	l2cap_pi(sk)->retry_count = 1;
	__mod_monitor_timer();

	l2cap_pi(sk)->conn_state |= L2CAP_CONN_WAIT_F;

	control = L2CAP_CTRL_POLL;
	l2cap_send_rr_or_rnr(l2cap_pi(sk), control);
	bh_unlock_sock(sk);
}

static void l2cap_drop_acked_frames(struct sock *sk)
{
	struct sk_buff *skb;

	while ((skb = skb_peek(TX_QUEUE(sk)))) {
		if (bt_cb(skb)->tx_seq == l2cap_pi(sk)->expected_ack_seq)
			break;

		skb = skb_dequeue(TX_QUEUE(sk));
		kfree_skb(skb);

		l2cap_pi(sk)->unacked_frames--;
	}

	if (!l2cap_pi(sk)->unacked_frames)
		del_timer(&l2cap_pi(sk)->retrans_timer);

	return;
}

static inline int l2cap_do_send(struct sock *sk, struct sk_buff *skb)
{
	struct l2cap_pinfo *pi = l2cap_pi(sk);
	int err;

	BT_DBG("sk %p, skb %p len %d", sk, skb, skb->len);

	err = hci_send_acl(pi->conn->hcon, skb, 0);
	if (err < 0)
		kfree_skb(skb);

	return err;
}

static int l2cap_streaming_send(struct sock *sk)
{
	struct sk_buff *skb, *tx_skb;
	struct l2cap_pinfo *pi = l2cap_pi(sk);
	u16 control, fcs;
	int err;

	while ((skb = sk->sk_send_head)) {
		tx_skb = skb_clone(skb, GFP_ATOMIC);

		control = get_unaligned_le16(tx_skb->data + L2CAP_HDR_SIZE);
		control |= pi->next_tx_seq << L2CAP_CTRL_TXSEQ_SHIFT;
		put_unaligned_le16(control, tx_skb->data + L2CAP_HDR_SIZE);

		if (l2cap_pi(sk)->fcs == L2CAP_FCS_CRC16) {
			fcs = crc16(0, (u8 *)tx_skb->data, tx_skb->len - 2);
			put_unaligned_le16(fcs, tx_skb->data + tx_skb->len - 2);
		}

		err = l2cap_do_send(sk, tx_skb);
		if (err < 0) {
			l2cap_send_disconn_req(pi->conn, sk);
			return err;
		}

		pi->next_tx_seq = (pi->next_tx_seq + 1) % 64;

		if (skb_queue_is_last(TX_QUEUE(sk), skb))
			sk->sk_send_head = NULL;
		else
			sk->sk_send_head = skb_queue_next(TX_QUEUE(sk), skb);

		skb = skb_dequeue(TX_QUEUE(sk));
		kfree_skb(skb);
	}
	return 0;
}

static int l2cap_retransmit_frame(struct sock *sk, u8 tx_seq)
{
	struct l2cap_pinfo *pi = l2cap_pi(sk);
	struct sk_buff *skb, *tx_skb;
	u16 control, fcs;
	int err;

	skb = skb_peek(TX_QUEUE(sk));
	do {
		if (bt_cb(skb)->tx_seq != tx_seq) {
			if (skb_queue_is_last(TX_QUEUE(sk), skb))
				break;
			skb = skb_queue_next(TX_QUEUE(sk), skb);
			continue;
		}

		if (pi->remote_max_tx &&
				bt_cb(skb)->retries == pi->remote_max_tx) {
			l2cap_send_disconn_req(pi->conn, sk);
			break;
		}

		tx_skb = skb_clone(skb, GFP_ATOMIC);
		bt_cb(skb)->retries++;
		control = get_unaligned_le16(tx_skb->data + L2CAP_HDR_SIZE);
		control |= (pi->buffer_seq << L2CAP_CTRL_REQSEQ_SHIFT)
				| (tx_seq << L2CAP_CTRL_TXSEQ_SHIFT);
		put_unaligned_le16(control, tx_skb->data + L2CAP_HDR_SIZE);

		if (l2cap_pi(sk)->fcs == L2CAP_FCS_CRC16) {
			fcs = crc16(0, (u8 *)tx_skb->data, tx_skb->len - 2);
			put_unaligned_le16(fcs, tx_skb->data + tx_skb->len - 2);
		}

		err = l2cap_do_send(sk, tx_skb);
		if (err < 0) {
			l2cap_send_disconn_req(pi->conn, sk);
			return err;
		}
		break;
	} while(1);
	return 0;
}

static int l2cap_ertm_send(struct sock *sk)
{
	struct sk_buff *skb, *tx_skb;
	struct l2cap_pinfo *pi = l2cap_pi(sk);
	u16 control, fcs;
	int err;

	if (pi->conn_state & L2CAP_CONN_WAIT_F)
		return 0;

	while ((skb = sk->sk_send_head) && (!l2cap_tx_window_full(sk)) &&
	       !(pi->conn_state & L2CAP_CONN_REMOTE_BUSY)) {

		if (pi->remote_max_tx &&
				bt_cb(skb)->retries == pi->remote_max_tx) {
			l2cap_send_disconn_req(pi->conn, sk);
			break;
		}

		tx_skb = skb_clone(skb, GFP_ATOMIC);

		bt_cb(skb)->retries++;

		control = get_unaligned_le16(tx_skb->data + L2CAP_HDR_SIZE);
		control |= (pi->buffer_seq << L2CAP_CTRL_REQSEQ_SHIFT)
				| (pi->next_tx_seq << L2CAP_CTRL_TXSEQ_SHIFT);
		put_unaligned_le16(control, tx_skb->data + L2CAP_HDR_SIZE);


		if (l2cap_pi(sk)->fcs == L2CAP_FCS_CRC16) {
			fcs = crc16(0, (u8 *)skb->data, tx_skb->len - 2);
			put_unaligned_le16(fcs, skb->data + tx_skb->len - 2);
		}

		err = l2cap_do_send(sk, tx_skb);
		if (err < 0) {
			l2cap_send_disconn_req(pi->conn, sk);
			return err;
		}
		__mod_retrans_timer();

		bt_cb(skb)->tx_seq = pi->next_tx_seq;
		pi->next_tx_seq = (pi->next_tx_seq + 1) % 64;

		pi->unacked_frames++;

		if (skb_queue_is_last(TX_QUEUE(sk), skb))
			sk->sk_send_head = NULL;
		else
			sk->sk_send_head = skb_queue_next(TX_QUEUE(sk), skb);
	}

	return 0;
}

static inline int l2cap_skbuff_fromiovec(struct sock *sk, struct msghdr *msg, int len, int count, struct sk_buff *skb)
{
	struct l2cap_conn *conn = l2cap_pi(sk)->conn;
	struct sk_buff **frag;
	int err, sent = 0;

	if (memcpy_fromiovec(skb_put(skb, count), msg->msg_iov, count)) {
		return -EFAULT;
	}

	sent += count;
	len  -= count;

	/* Continuation fragments (no L2CAP header) */
	frag = &skb_shinfo(skb)->frag_list;
	while (len) {
		count = min_t(unsigned int, conn->mtu, len);

		*frag = bt_skb_send_alloc(sk, count, msg->msg_flags & MSG_DONTWAIT, &err);
		if (!*frag)
			return -EFAULT;
		if (memcpy_fromiovec(skb_put(*frag, count), msg->msg_iov, count))
			return -EFAULT;

		sent += count;
		len  -= count;

		frag = &(*frag)->next;
	}

	return sent;
}

static struct sk_buff *l2cap_create_connless_pdu(struct sock *sk, struct msghdr *msg, size_t len)
{
	struct l2cap_conn *conn = l2cap_pi(sk)->conn;
	struct sk_buff *skb;
	int err, count, hlen = L2CAP_HDR_SIZE + 2;
	struct l2cap_hdr *lh;

	BT_DBG("sk %p len %d", sk, (int)len);

	count = min_t(unsigned int, (conn->mtu - hlen), len);
	skb = bt_skb_send_alloc(sk, count + hlen,
			msg->msg_flags & MSG_DONTWAIT, &err);
	if (!skb)
		return ERR_PTR(-ENOMEM);

	/* Create L2CAP header */
	lh = (struct l2cap_hdr *) skb_put(skb, L2CAP_HDR_SIZE);
	lh->cid = cpu_to_le16(l2cap_pi(sk)->dcid);
	lh->len = cpu_to_le16(len + (hlen - L2CAP_HDR_SIZE));
	put_unaligned_le16(l2cap_pi(sk)->psm, skb_put(skb, 2));

	err = l2cap_skbuff_fromiovec(sk, msg, len, count, skb);
	if (unlikely(err < 0)) {
		kfree_skb(skb);
		return ERR_PTR(err);
	}
	return skb;
}

static struct sk_buff *l2cap_create_basic_pdu(struct sock *sk, struct msghdr *msg, size_t len)
{
	struct l2cap_conn *conn = l2cap_pi(sk)->conn;
	struct sk_buff *skb;
	int err, count, hlen = L2CAP_HDR_SIZE;
	struct l2cap_hdr *lh;

	BT_DBG("sk %p len %d", sk, (int)len);

	count = min_t(unsigned int, (conn->mtu - hlen), len);
	skb = bt_skb_send_alloc(sk, count + hlen,
			msg->msg_flags & MSG_DONTWAIT, &err);
	if (!skb)
		return ERR_PTR(-ENOMEM);

	/* Create L2CAP header */
	lh = (struct l2cap_hdr *) skb_put(skb, L2CAP_HDR_SIZE);
	lh->cid = cpu_to_le16(l2cap_pi(sk)->dcid);
	lh->len = cpu_to_le16(len + (hlen - L2CAP_HDR_SIZE));

	err = l2cap_skbuff_fromiovec(sk, msg, len, count, skb);
	if (unlikely(err < 0)) {
		kfree_skb(skb);
		return ERR_PTR(err);
	}
	return skb;
}

static struct sk_buff *l2cap_create_iframe_pdu(struct sock *sk, struct msghdr *msg, size_t len, u16 control, u16 sdulen)
{
	struct l2cap_conn *conn = l2cap_pi(sk)->conn;
	struct sk_buff *skb;
	int err, count, hlen = L2CAP_HDR_SIZE + 2;
	struct l2cap_hdr *lh;

	BT_DBG("sk %p len %d", sk, (int)len);

	if (sdulen)
		hlen += 2;

	if (l2cap_pi(sk)->fcs == L2CAP_FCS_CRC16)
		hlen += 2;

	count = min_t(unsigned int, (conn->mtu - hlen), len);
	skb = bt_skb_send_alloc(sk, count + hlen,
			msg->msg_flags & MSG_DONTWAIT, &err);
	if (!skb)
		return ERR_PTR(-ENOMEM);

	/* Create L2CAP header */
	lh = (struct l2cap_hdr *) skb_put(skb, L2CAP_HDR_SIZE);
	lh->cid = cpu_to_le16(l2cap_pi(sk)->dcid);
	lh->len = cpu_to_le16(len + (hlen - L2CAP_HDR_SIZE));
	put_unaligned_le16(control, skb_put(skb, 2));
	if (sdulen)
		put_unaligned_le16(sdulen, skb_put(skb, 2));

	err = l2cap_skbuff_fromiovec(sk, msg, len, count, skb);
	if (unlikely(err < 0)) {
		kfree_skb(skb);
		return ERR_PTR(err);
	}

	if (l2cap_pi(sk)->fcs == L2CAP_FCS_CRC16)
		put_unaligned_le16(0, skb_put(skb, 2));

	bt_cb(skb)->retries = 0;
	return skb;
}

static inline int l2cap_sar_segment_sdu(struct sock *sk, struct msghdr *msg, size_t len)
{
	struct l2cap_pinfo *pi = l2cap_pi(sk);
	struct sk_buff *skb;
	struct sk_buff_head sar_queue;
	u16 control;
	size_t size = 0;

	__skb_queue_head_init(&sar_queue);
	control = L2CAP_SDU_START;
	skb = l2cap_create_iframe_pdu(sk, msg, pi->max_pdu_size, control, len);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	__skb_queue_tail(&sar_queue, skb);
	len -= pi->max_pdu_size;
	size +=pi->max_pdu_size;
	control = 0;

	while (len > 0) {
		size_t buflen;

		if (len > pi->max_pdu_size) {
			control |= L2CAP_SDU_CONTINUE;
			buflen = pi->max_pdu_size;
		} else {
			control |= L2CAP_SDU_END;
			buflen = len;
		}

		skb = l2cap_create_iframe_pdu(sk, msg, buflen, control, 0);
		if (IS_ERR(skb)) {
			skb_queue_purge(&sar_queue);
			return PTR_ERR(skb);
		}

		__skb_queue_tail(&sar_queue, skb);
		len -= buflen;
		size += buflen;
		control = 0;
	}
	skb_queue_splice_tail(&sar_queue, TX_QUEUE(sk));
	if (sk->sk_send_head == NULL)
		sk->sk_send_head = sar_queue.next;

	return size;
}

static int l2cap_sock_sendmsg(struct kiocb *iocb, struct socket *sock, struct msghdr *msg, size_t len)
{
	struct sock *sk = sock->sk;
	struct l2cap_pinfo *pi = l2cap_pi(sk);
	struct sk_buff *skb;
	u16 control;
	int err;

	BT_DBG("sock %p, sk %p", sock, sk);

	err = sock_error(sk);
	if (err)
		return err;

	if (msg->msg_flags & MSG_OOB)
		return -EOPNOTSUPP;

	/* Check outgoing MTU */
	if (sk->sk_type == SOCK_SEQPACKET && pi->mode == L2CAP_MODE_BASIC &&
	    len > pi->omtu)
		return -EINVAL;

	lock_sock(sk);

	if (sk->sk_state != BT_CONNECTED) {
		err = -ENOTCONN;
		goto done;
	}

	/* Connectionless channel */
	if (sk->sk_type == SOCK_DGRAM) {
		skb = l2cap_create_connless_pdu(sk, msg, len);
		err = l2cap_do_send(sk, skb);
		goto done;
	}

	switch (pi->mode) {
	case L2CAP_MODE_BASIC:
		/* Create a basic PDU */
		skb = l2cap_create_basic_pdu(sk, msg, len);
		if (IS_ERR(skb)) {
			err = PTR_ERR(skb);
			goto done;
		}

		err = l2cap_do_send(sk, skb);
		if (!err)
			err = len;
		break;

	case L2CAP_MODE_ERTM:
	case L2CAP_MODE_STREAMING:
		/* Entire SDU fits into one PDU */
		if (len <= pi->max_pdu_size) {
			control = L2CAP_SDU_UNSEGMENTED;
			skb = l2cap_create_iframe_pdu(sk, msg, len, control, 0);
			if (IS_ERR(skb)) {
				err = PTR_ERR(skb);
				goto done;
			}
			__skb_queue_tail(TX_QUEUE(sk), skb);
			if (sk->sk_send_head == NULL)
				sk->sk_send_head = skb;
		} else {
		/* Segment SDU into multiples PDUs */
			err = l2cap_sar_segment_sdu(sk, msg, len);
			if (err < 0)
				goto done;
		}

		if (pi->mode == L2CAP_MODE_STREAMING)
			err = l2cap_streaming_send(sk);
		else
			err = l2cap_ertm_send(sk);

		if (!err)
			err = len;
		break;

	default:
		BT_DBG("bad state %1.1x", pi->mode);
		err = -EINVAL;
	}

done:
	release_sock(sk);
	return err;
}

static int l2cap_sock_recvmsg(struct kiocb *iocb, struct socket *sock, struct msghdr *msg, size_t len, int flags)
{
	struct sock *sk = sock->sk;

	lock_sock(sk);

	if (sk->sk_state == BT_CONNECT2 && bt_sk(sk)->defer_setup) {
		struct l2cap_conn_rsp rsp;

		sk->sk_state = BT_CONFIG;

		rsp.scid   = cpu_to_le16(l2cap_pi(sk)->dcid);
		rsp.dcid   = cpu_to_le16(l2cap_pi(sk)->scid);
		rsp.result = cpu_to_le16(L2CAP_CR_SUCCESS);
		rsp.status = cpu_to_le16(L2CAP_CS_NO_INFO);
		l2cap_send_cmd(l2cap_pi(sk)->conn, l2cap_pi(sk)->ident,
					L2CAP_CONN_RSP, sizeof(rsp), &rsp);

		release_sock(sk);
		return 0;
	}

	release_sock(sk);

	return bt_sock_recvmsg(iocb, sock, msg, len, flags);
}

static int l2cap_sock_setsockopt_old(struct socket *sock, int optname, char __user *optval, unsigned int optlen)
{
	struct sock *sk = sock->sk;
	struct l2cap_options opts;
	int len, err = 0;
	u32 opt;

	BT_DBG("sk %p", sk);

	lock_sock(sk);

	switch (optname) {
	case L2CAP_OPTIONS:
		opts.imtu     = l2cap_pi(sk)->imtu;
		opts.omtu     = l2cap_pi(sk)->omtu;
		opts.flush_to = l2cap_pi(sk)->flush_to;
		opts.mode     = l2cap_pi(sk)->mode;
		opts.fcs      = l2cap_pi(sk)->fcs;

		len = min_t(unsigned int, sizeof(opts), optlen);
		if (copy_from_user((char *) &opts, optval, len)) {
			err = -EFAULT;
			break;
		}

		l2cap_pi(sk)->imtu = opts.imtu;
		l2cap_pi(sk)->omtu = opts.omtu;
		l2cap_pi(sk)->mode = opts.mode;
		l2cap_pi(sk)->fcs  = opts.fcs;
		break;

	case L2CAP_LM:
		if (get_user(opt, (u32 __user *) optval)) {
			err = -EFAULT;
			break;
		}

		if (opt & L2CAP_LM_AUTH)
			l2cap_pi(sk)->sec_level = BT_SECURITY_LOW;
		if (opt & L2CAP_LM_ENCRYPT)
			l2cap_pi(sk)->sec_level = BT_SECURITY_MEDIUM;
		if (opt & L2CAP_LM_SECURE)
			l2cap_pi(sk)->sec_level = BT_SECURITY_HIGH;

		l2cap_pi(sk)->role_switch    = (opt & L2CAP_LM_MASTER);
		l2cap_pi(sk)->force_reliable = (opt & L2CAP_LM_RELIABLE);
		break;

	default:
		err = -ENOPROTOOPT;
		break;
	}

	release_sock(sk);
	return err;
}

static int l2cap_sock_setsockopt(struct socket *sock, int level, int optname, char __user *optval, unsigned int optlen)
{
	struct sock *sk = sock->sk;
	struct bt_security sec;
	int len, err = 0;
	u32 opt;

	BT_DBG("sk %p", sk);

	if (level == SOL_L2CAP)
		return l2cap_sock_setsockopt_old(sock, optname, optval, optlen);

	if (level != SOL_BLUETOOTH)
		return -ENOPROTOOPT;

	lock_sock(sk);

	switch (optname) {
	case BT_SECURITY:
		if (sk->sk_type != SOCK_SEQPACKET && sk->sk_type != SOCK_RAW) {
			err = -EINVAL;
			break;
		}

		sec.level = BT_SECURITY_LOW;

		len = min_t(unsigned int, sizeof(sec), optlen);
		if (copy_from_user((char *) &sec, optval, len)) {
			err = -EFAULT;
			break;
		}

		if (sec.level < BT_SECURITY_LOW ||
					sec.level > BT_SECURITY_HIGH) {
			err = -EINVAL;
			break;
		}

		l2cap_pi(sk)->sec_level = sec.level;
		break;

	case BT_DEFER_SETUP:
		if (sk->sk_state != BT_BOUND && sk->sk_state != BT_LISTEN) {
			err = -EINVAL;
			break;
		}

		if (get_user(opt, (u32 __user *) optval)) {
			err = -EFAULT;
			break;
		}

		bt_sk(sk)->defer_setup = opt;
		break;

	default:
		err = -ENOPROTOOPT;
		break;
	}

	release_sock(sk);
	return err;
}

static int l2cap_sock_getsockopt_old(struct socket *sock, int optname, char __user *optval, int __user *optlen)
{
	struct sock *sk = sock->sk;
	struct l2cap_options opts;
	struct l2cap_conninfo cinfo;
	int len, err = 0;
	u32 opt;

	BT_DBG("sk %p", sk);

	if (get_user(len, optlen))
		return -EFAULT;

	lock_sock(sk);

	switch (optname) {
	case L2CAP_OPTIONS:
		opts.imtu     = l2cap_pi(sk)->imtu;
		opts.omtu     = l2cap_pi(sk)->omtu;
		opts.flush_to = l2cap_pi(sk)->flush_to;
		opts.mode     = l2cap_pi(sk)->mode;
		opts.fcs      = l2cap_pi(sk)->fcs;

		len = min_t(unsigned int, len, sizeof(opts));
		if (copy_to_user(optval, (char *) &opts, len))
			err = -EFAULT;

		break;

	case L2CAP_LM:
		switch (l2cap_pi(sk)->sec_level) {
		case BT_SECURITY_LOW:
			opt = L2CAP_LM_AUTH;
			break;
		case BT_SECURITY_MEDIUM:
			opt = L2CAP_LM_AUTH | L2CAP_LM_ENCRYPT;
			break;
		case BT_SECURITY_HIGH:
			opt = L2CAP_LM_AUTH | L2CAP_LM_ENCRYPT |
							L2CAP_LM_SECURE;
			break;
		default:
			opt = 0;
			break;
		}

		if (l2cap_pi(sk)->role_switch)
			opt |= L2CAP_LM_MASTER;

		if (l2cap_pi(sk)->force_reliable)
			opt |= L2CAP_LM_RELIABLE;

		if (put_user(opt, (u32 __user *) optval))
			err = -EFAULT;
		break;

	case L2CAP_CONNINFO:
		if (sk->sk_state != BT_CONNECTED &&
					!(sk->sk_state == BT_CONNECT2 &&
						bt_sk(sk)->defer_setup)) {
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

static int l2cap_sock_getsockopt(struct socket *sock, int level, int optname, char __user *optval, int __user *optlen)
{
	struct sock *sk = sock->sk;
	struct bt_security sec;
	int len, err = 0;

	BT_DBG("sk %p", sk);

	if (level == SOL_L2CAP)
		return l2cap_sock_getsockopt_old(sock, optname, optval, optlen);

	if (level != SOL_BLUETOOTH)
		return -ENOPROTOOPT;

	if (get_user(len, optlen))
		return -EFAULT;

	lock_sock(sk);

	switch (optname) {
	case BT_SECURITY:
		if (sk->sk_type != SOCK_SEQPACKET && sk->sk_type != SOCK_RAW) {
			err = -EINVAL;
			break;
		}

		sec.level = l2cap_pi(sk)->sec_level;

		len = min_t(unsigned int, len, sizeof(sec));
		if (copy_to_user(optval, (char *) &sec, len))
			err = -EFAULT;

		break;

	case BT_DEFER_SETUP:
		if (sk->sk_state != BT_BOUND && sk->sk_state != BT_LISTEN) {
			err = -EINVAL;
			break;
		}

		if (put_user(bt_sk(sk)->defer_setup, (u32 __user *) optval))
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
			err = bt_sock_wait_state(sk, BT_CLOSED,
							sk->sk_lingertime);
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
	struct sock *sk;

	BT_DBG("conn %p", conn);

	read_lock(&l->lock);
	for (sk = l->head; sk; sk = l2cap_pi(sk)->next_c) {
		if (sk->sk_type != SOCK_RAW)
			continue;

		/* Don't send frame to the socket it came from */
		if (skb->sk == sk)
			continue;
		nskb = skb_clone(skb, GFP_ATOMIC);
		if (!nskb)
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

	BT_DBG("conn %p, code 0x%2.2x, ident 0x%2.2x, len %d",
			conn, code, ident, dlen);

	len = L2CAP_HDR_SIZE + L2CAP_CMD_HDR_SIZE + dlen;
	count = min_t(unsigned int, conn->mtu, len);

	skb = bt_skb_alloc(count, GFP_ATOMIC);
	if (!skb)
		return NULL;

	lh = (struct l2cap_hdr *) skb_put(skb, L2CAP_HDR_SIZE);
	lh->len = cpu_to_le16(L2CAP_CMD_HDR_SIZE + dlen);
	lh->cid = cpu_to_le16(L2CAP_CID_SIGNALING);

	cmd = (struct l2cap_cmd_hdr *) skb_put(skb, L2CAP_CMD_HDR_SIZE);
	cmd->code  = code;
	cmd->ident = ident;
	cmd->len   = cpu_to_le16(dlen);

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
		*val = __le16_to_cpu(*((__le16 *) opt->val));
		break;

	case 4:
		*val = __le32_to_cpu(*((__le32 *) opt->val));
		break;

	default:
		*val = (unsigned long) opt->val;
		break;
	}

	BT_DBG("type 0x%2.2x len %d val 0x%lx", *type, opt->len, *val);
	return len;
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
		*((__le16 *) opt->val) = cpu_to_le16(val);
		break;

	case 4:
		*((__le32 *) opt->val) = cpu_to_le32(val);
		break;

	default:
		memcpy(opt->val, (void *) val, len);
		break;
	}

	*ptr += L2CAP_CONF_OPT_SIZE + len;
}

static inline void l2cap_ertm_init(struct sock *sk)
{
	l2cap_pi(sk)->expected_ack_seq = 0;
	l2cap_pi(sk)->unacked_frames = 0;
	l2cap_pi(sk)->buffer_seq = 0;
	l2cap_pi(sk)->num_to_ack = 0;

	setup_timer(&l2cap_pi(sk)->retrans_timer,
			l2cap_retrans_timeout, (unsigned long) sk);
	setup_timer(&l2cap_pi(sk)->monitor_timer,
			l2cap_monitor_timeout, (unsigned long) sk);

	__skb_queue_head_init(SREJ_QUEUE(sk));
}

static int l2cap_mode_supported(__u8 mode, __u32 feat_mask)
{
	u32 local_feat_mask = l2cap_feat_mask;
	if (enable_ertm)
		local_feat_mask |= L2CAP_FEAT_ERTM | L2CAP_FEAT_STREAMING;

	switch (mode) {
	case L2CAP_MODE_ERTM:
		return L2CAP_FEAT_ERTM & feat_mask & local_feat_mask;
	case L2CAP_MODE_STREAMING:
		return L2CAP_FEAT_STREAMING & feat_mask & local_feat_mask;
	default:
		return 0x00;
	}
}

static inline __u8 l2cap_select_mode(__u8 mode, __u16 remote_feat_mask)
{
	switch (mode) {
	case L2CAP_MODE_STREAMING:
	case L2CAP_MODE_ERTM:
		if (l2cap_mode_supported(mode, remote_feat_mask))
			return mode;
		/* fall through */
	default:
		return L2CAP_MODE_BASIC;
	}
}

static int l2cap_build_conf_req(struct sock *sk, void *data)
{
	struct l2cap_pinfo *pi = l2cap_pi(sk);
	struct l2cap_conf_req *req = data;
	struct l2cap_conf_rfc rfc = { .mode = L2CAP_MODE_BASIC };
	void *ptr = req->data;

	BT_DBG("sk %p", sk);

	if (pi->num_conf_req || pi->num_conf_rsp)
		goto done;

	switch (pi->mode) {
	case L2CAP_MODE_STREAMING:
	case L2CAP_MODE_ERTM:
		pi->conf_state |= L2CAP_CONF_STATE2_DEVICE;
		if (!l2cap_mode_supported(pi->mode, pi->conn->feat_mask))
			l2cap_send_disconn_req(pi->conn, sk);
		break;
	default:
		pi->mode = l2cap_select_mode(rfc.mode, pi->conn->feat_mask);
		break;
	}

done:
	switch (pi->mode) {
	case L2CAP_MODE_BASIC:
		if (pi->imtu != L2CAP_DEFAULT_MTU)
			l2cap_add_conf_opt(&ptr, L2CAP_CONF_MTU, 2, pi->imtu);
		break;

	case L2CAP_MODE_ERTM:
		rfc.mode            = L2CAP_MODE_ERTM;
		rfc.txwin_size      = L2CAP_DEFAULT_TX_WINDOW;
		rfc.max_transmit    = max_transmit;
		rfc.retrans_timeout = 0;
		rfc.monitor_timeout = 0;
		rfc.max_pdu_size    = cpu_to_le16(L2CAP_DEFAULT_MAX_PDU_SIZE);

		l2cap_add_conf_opt(&ptr, L2CAP_CONF_RFC,
					sizeof(rfc), (unsigned long) &rfc);

		if (!(pi->conn->feat_mask & L2CAP_FEAT_FCS))
			break;

		if (pi->fcs == L2CAP_FCS_NONE ||
				pi->conf_state & L2CAP_CONF_NO_FCS_RECV) {
			pi->fcs = L2CAP_FCS_NONE;
			l2cap_add_conf_opt(&ptr, L2CAP_CONF_FCS, 1, pi->fcs);
		}
		break;

	case L2CAP_MODE_STREAMING:
		rfc.mode            = L2CAP_MODE_STREAMING;
		rfc.txwin_size      = 0;
		rfc.max_transmit    = 0;
		rfc.retrans_timeout = 0;
		rfc.monitor_timeout = 0;
		rfc.max_pdu_size    = cpu_to_le16(L2CAP_DEFAULT_MAX_PDU_SIZE);

		l2cap_add_conf_opt(&ptr, L2CAP_CONF_RFC,
					sizeof(rfc), (unsigned long) &rfc);

		if (!(pi->conn->feat_mask & L2CAP_FEAT_FCS))
			break;

		if (pi->fcs == L2CAP_FCS_NONE ||
				pi->conf_state & L2CAP_CONF_NO_FCS_RECV) {
			pi->fcs = L2CAP_FCS_NONE;
			l2cap_add_conf_opt(&ptr, L2CAP_CONF_FCS, 1, pi->fcs);
		}
		break;
	}

	/* FIXME: Need actual value of the flush timeout */
	//if (flush_to != L2CAP_DEFAULT_FLUSH_TO)
	//   l2cap_add_conf_opt(&ptr, L2CAP_CONF_FLUSH_TO, 2, pi->flush_to);

	req->dcid  = cpu_to_le16(pi->dcid);
	req->flags = cpu_to_le16(0);

	return ptr - data;
}

static int l2cap_parse_conf_req(struct sock *sk, void *data)
{
	struct l2cap_pinfo *pi = l2cap_pi(sk);
	struct l2cap_conf_rsp *rsp = data;
	void *ptr = rsp->data;
	void *req = pi->conf_req;
	int len = pi->conf_len;
	int type, hint, olen;
	unsigned long val;
	struct l2cap_conf_rfc rfc = { .mode = L2CAP_MODE_BASIC };
	u16 mtu = L2CAP_DEFAULT_MTU;
	u16 result = L2CAP_CONF_SUCCESS;

	BT_DBG("sk %p", sk);

	while (len >= L2CAP_CONF_OPT_SIZE) {
		len -= l2cap_get_conf_opt(&req, &type, &olen, &val);

		hint  = type & L2CAP_CONF_HINT;
		type &= L2CAP_CONF_MASK;

		switch (type) {
		case L2CAP_CONF_MTU:
			mtu = val;
			break;

		case L2CAP_CONF_FLUSH_TO:
			pi->flush_to = val;
			break;

		case L2CAP_CONF_QOS:
			break;

		case L2CAP_CONF_RFC:
			if (olen == sizeof(rfc))
				memcpy(&rfc, (void *) val, olen);
			break;

		case L2CAP_CONF_FCS:
			if (val == L2CAP_FCS_NONE)
				pi->conf_state |= L2CAP_CONF_NO_FCS_RECV;

			break;

		default:
			if (hint)
				break;

			result = L2CAP_CONF_UNKNOWN;
			*((u8 *) ptr++) = type;
			break;
		}
	}

	if (pi->num_conf_rsp || pi->num_conf_req)
		goto done;

	switch (pi->mode) {
	case L2CAP_MODE_STREAMING:
	case L2CAP_MODE_ERTM:
		pi->conf_state |= L2CAP_CONF_STATE2_DEVICE;
		if (!l2cap_mode_supported(pi->mode, pi->conn->feat_mask))
			return -ECONNREFUSED;
		break;
	default:
		pi->mode = l2cap_select_mode(rfc.mode, pi->conn->feat_mask);
		break;
	}

done:
	if (pi->mode != rfc.mode) {
		result = L2CAP_CONF_UNACCEPT;
		rfc.mode = pi->mode;

		if (pi->num_conf_rsp == 1)
			return -ECONNREFUSED;

		l2cap_add_conf_opt(&ptr, L2CAP_CONF_RFC,
					sizeof(rfc), (unsigned long) &rfc);
	}


	if (result == L2CAP_CONF_SUCCESS) {
		/* Configure output options and let the other side know
		 * which ones we don't like. */

		if (mtu < L2CAP_DEFAULT_MIN_MTU)
			result = L2CAP_CONF_UNACCEPT;
		else {
			pi->omtu = mtu;
			pi->conf_state |= L2CAP_CONF_MTU_DONE;
		}
		l2cap_add_conf_opt(&ptr, L2CAP_CONF_MTU, 2, pi->omtu);

		switch (rfc.mode) {
		case L2CAP_MODE_BASIC:
			pi->fcs = L2CAP_FCS_NONE;
			pi->conf_state |= L2CAP_CONF_MODE_DONE;
			break;

		case L2CAP_MODE_ERTM:
			pi->remote_tx_win = rfc.txwin_size;
			pi->remote_max_tx = rfc.max_transmit;
			pi->max_pdu_size = rfc.max_pdu_size;

			rfc.retrans_timeout = L2CAP_DEFAULT_RETRANS_TO;
			rfc.monitor_timeout = L2CAP_DEFAULT_MONITOR_TO;

			pi->conf_state |= L2CAP_CONF_MODE_DONE;

			l2cap_add_conf_opt(&ptr, L2CAP_CONF_RFC,
					sizeof(rfc), (unsigned long) &rfc);

			break;

		case L2CAP_MODE_STREAMING:
			pi->remote_tx_win = rfc.txwin_size;
			pi->max_pdu_size = rfc.max_pdu_size;

			pi->conf_state |= L2CAP_CONF_MODE_DONE;

			l2cap_add_conf_opt(&ptr, L2CAP_CONF_RFC,
					sizeof(rfc), (unsigned long) &rfc);

			break;

		default:
			result = L2CAP_CONF_UNACCEPT;

			memset(&rfc, 0, sizeof(rfc));
			rfc.mode = pi->mode;
		}

		if (result == L2CAP_CONF_SUCCESS)
			pi->conf_state |= L2CAP_CONF_OUTPUT_DONE;
	}
	rsp->scid   = cpu_to_le16(pi->dcid);
	rsp->result = cpu_to_le16(result);
	rsp->flags  = cpu_to_le16(0x0000);

	return ptr - data;
}

static int l2cap_parse_conf_rsp(struct sock *sk, void *rsp, int len, void *data, u16 *result)
{
	struct l2cap_pinfo *pi = l2cap_pi(sk);
	struct l2cap_conf_req *req = data;
	void *ptr = req->data;
	int type, olen;
	unsigned long val;
	struct l2cap_conf_rfc rfc;

	BT_DBG("sk %p, rsp %p, len %d, req %p", sk, rsp, len, data);

	while (len >= L2CAP_CONF_OPT_SIZE) {
		len -= l2cap_get_conf_opt(&rsp, &type, &olen, &val);

		switch (type) {
		case L2CAP_CONF_MTU:
			if (val < L2CAP_DEFAULT_MIN_MTU) {
				*result = L2CAP_CONF_UNACCEPT;
				pi->omtu = L2CAP_DEFAULT_MIN_MTU;
			} else
				pi->omtu = val;
			l2cap_add_conf_opt(&ptr, L2CAP_CONF_MTU, 2, pi->omtu);
			break;

		case L2CAP_CONF_FLUSH_TO:
			pi->flush_to = val;
			l2cap_add_conf_opt(&ptr, L2CAP_CONF_FLUSH_TO,
							2, pi->flush_to);
			break;

		case L2CAP_CONF_RFC:
			if (olen == sizeof(rfc))
				memcpy(&rfc, (void *)val, olen);

			if ((pi->conf_state & L2CAP_CONF_STATE2_DEVICE) &&
							rfc.mode != pi->mode)
				return -ECONNREFUSED;

			pi->mode = rfc.mode;
			pi->fcs = 0;

			l2cap_add_conf_opt(&ptr, L2CAP_CONF_RFC,
					sizeof(rfc), (unsigned long) &rfc);
			break;
		}
	}

	if (*result == L2CAP_CONF_SUCCESS) {
		switch (rfc.mode) {
		case L2CAP_MODE_ERTM:
			pi->remote_tx_win   = rfc.txwin_size;
			pi->retrans_timeout = rfc.retrans_timeout;
			pi->monitor_timeout = rfc.monitor_timeout;
			pi->max_pdu_size    = le16_to_cpu(rfc.max_pdu_size);
			break;
		case L2CAP_MODE_STREAMING:
			pi->max_pdu_size    = le16_to_cpu(rfc.max_pdu_size);
			break;
		}
	}

	req->dcid   = cpu_to_le16(pi->dcid);
	req->flags  = cpu_to_le16(0x0000);

	return ptr - data;
}

static int l2cap_build_conf_rsp(struct sock *sk, void *data, u16 result, u16 flags)
{
	struct l2cap_conf_rsp *rsp = data;
	void *ptr = rsp->data;

	BT_DBG("sk %p", sk);

	rsp->scid   = cpu_to_le16(l2cap_pi(sk)->dcid);
	rsp->result = cpu_to_le16(result);
	rsp->flags  = cpu_to_le16(flags);

	return ptr - data;
}

static inline int l2cap_command_rej(struct l2cap_conn *conn, struct l2cap_cmd_hdr *cmd, u8 *data)
{
	struct l2cap_cmd_rej *rej = (struct l2cap_cmd_rej *) data;

	if (rej->reason != 0x0000)
		return 0;

	if ((conn->info_state & L2CAP_INFO_FEAT_MASK_REQ_SENT) &&
					cmd->ident == conn->info_ident) {
		del_timer(&conn->info_timer);

		conn->info_state |= L2CAP_INFO_FEAT_MASK_REQ_DONE;
		conn->info_ident = 0;

		l2cap_conn_start(conn);
	}

	return 0;
}

static inline int l2cap_connect_req(struct l2cap_conn *conn, struct l2cap_cmd_hdr *cmd, u8 *data)
{
	struct l2cap_chan_list *list = &conn->chan_list;
	struct l2cap_conn_req *req = (struct l2cap_conn_req *) data;
	struct l2cap_conn_rsp rsp;
	struct sock *sk, *parent;
	int result, status = L2CAP_CS_NO_INFO;

	u16 dcid = 0, scid = __le16_to_cpu(req->scid);
	__le16 psm = req->psm;

	BT_DBG("psm 0x%2.2x scid 0x%4.4x", psm, scid);

	/* Check if we have socket listening on psm */
	parent = l2cap_get_sock_by_psm(BT_LISTEN, psm, conn->src);
	if (!parent) {
		result = L2CAP_CR_BAD_PSM;
		goto sendresp;
	}

	/* Check if the ACL is secure enough (if not SDP) */
	if (psm != cpu_to_le16(0x0001) &&
				!hci_conn_check_link_mode(conn->hcon)) {
		conn->disc_reason = 0x05;
		result = L2CAP_CR_SEC_BLOCK;
		goto response;
	}

	result = L2CAP_CR_NO_MEM;

	/* Check for backlog size */
	if (sk_acceptq_is_full(parent)) {
		BT_DBG("backlog full %d", parent->sk_ack_backlog);
		goto response;
	}

	sk = l2cap_sock_alloc(sock_net(parent), NULL, BTPROTO_L2CAP, GFP_ATOMIC);
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

	l2cap_pi(sk)->ident = cmd->ident;

	if (conn->info_state & L2CAP_INFO_FEAT_MASK_REQ_DONE) {
		if (l2cap_check_security(sk)) {
			if (bt_sk(sk)->defer_setup) {
				sk->sk_state = BT_CONNECT2;
				result = L2CAP_CR_PEND;
				status = L2CAP_CS_AUTHOR_PEND;
				parent->sk_data_ready(parent, 0);
			} else {
				sk->sk_state = BT_CONFIG;
				result = L2CAP_CR_SUCCESS;
				status = L2CAP_CS_NO_INFO;
			}
		} else {
			sk->sk_state = BT_CONNECT2;
			result = L2CAP_CR_PEND;
			status = L2CAP_CS_AUTHEN_PEND;
		}
	} else {
		sk->sk_state = BT_CONNECT2;
		result = L2CAP_CR_PEND;
		status = L2CAP_CS_NO_INFO;
	}

	write_unlock_bh(&list->lock);

response:
	bh_unlock_sock(parent);

sendresp:
	rsp.scid   = cpu_to_le16(scid);
	rsp.dcid   = cpu_to_le16(dcid);
	rsp.result = cpu_to_le16(result);
	rsp.status = cpu_to_le16(status);
	l2cap_send_cmd(conn, cmd->ident, L2CAP_CONN_RSP, sizeof(rsp), &rsp);

	if (result == L2CAP_CR_PEND && status == L2CAP_CS_NO_INFO) {
		struct l2cap_info_req info;
		info.type = cpu_to_le16(L2CAP_IT_FEAT_MASK);

		conn->info_state |= L2CAP_INFO_FEAT_MASK_REQ_SENT;
		conn->info_ident = l2cap_get_ident(conn);

		mod_timer(&conn->info_timer, jiffies +
					msecs_to_jiffies(L2CAP_INFO_TIMEOUT));

		l2cap_send_cmd(conn, conn->info_ident,
					L2CAP_INFO_REQ, sizeof(info), &info);
	}

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
		sk = l2cap_get_chan_by_scid(&conn->chan_list, scid);
		if (!sk)
			return 0;
	} else {
		sk = l2cap_get_chan_by_ident(&conn->chan_list, cmd->ident);
		if (!sk)
			return 0;
	}

	switch (result) {
	case L2CAP_CR_SUCCESS:
		sk->sk_state = BT_CONFIG;
		l2cap_pi(sk)->ident = 0;
		l2cap_pi(sk)->dcid = dcid;
		l2cap_pi(sk)->conf_state |= L2CAP_CONF_REQ_SENT;

		l2cap_pi(sk)->conf_state &= ~L2CAP_CONF_CONNECT_PEND;

		l2cap_send_cmd(conn, l2cap_get_ident(conn), L2CAP_CONF_REQ,
					l2cap_build_conf_req(sk, req), req);
		l2cap_pi(sk)->num_conf_req++;
		break;

	case L2CAP_CR_PEND:
		l2cap_pi(sk)->conf_state |= L2CAP_CONF_CONNECT_PEND;
		break;

	default:
		l2cap_chan_del(sk, ECONNREFUSED);
		break;
	}

	bh_unlock_sock(sk);
	return 0;
}

static inline int l2cap_config_req(struct l2cap_conn *conn, struct l2cap_cmd_hdr *cmd, u16 cmd_len, u8 *data)
{
	struct l2cap_conf_req *req = (struct l2cap_conf_req *) data;
	u16 dcid, flags;
	u8 rsp[64];
	struct sock *sk;
	int len;

	dcid  = __le16_to_cpu(req->dcid);
	flags = __le16_to_cpu(req->flags);

	BT_DBG("dcid 0x%4.4x flags 0x%2.2x", dcid, flags);

	sk = l2cap_get_chan_by_scid(&conn->chan_list, dcid);
	if (!sk)
		return -ENOENT;

	if (sk->sk_state == BT_DISCONN)
		goto unlock;

	/* Reject if config buffer is too small. */
	len = cmd_len - sizeof(*req);
	if (l2cap_pi(sk)->conf_len + len > sizeof(l2cap_pi(sk)->conf_req)) {
		l2cap_send_cmd(conn, cmd->ident, L2CAP_CONF_RSP,
				l2cap_build_conf_rsp(sk, rsp,
					L2CAP_CONF_REJECT, flags), rsp);
		goto unlock;
	}

	/* Store config. */
	memcpy(l2cap_pi(sk)->conf_req + l2cap_pi(sk)->conf_len, req->data, len);
	l2cap_pi(sk)->conf_len += len;

	if (flags & 0x0001) {
		/* Incomplete config. Send empty response. */
		l2cap_send_cmd(conn, cmd->ident, L2CAP_CONF_RSP,
				l2cap_build_conf_rsp(sk, rsp,
					L2CAP_CONF_SUCCESS, 0x0001), rsp);
		goto unlock;
	}

	/* Complete config. */
	len = l2cap_parse_conf_req(sk, rsp);
	if (len < 0) {
		l2cap_send_disconn_req(conn, sk);
		goto unlock;
	}

	l2cap_send_cmd(conn, cmd->ident, L2CAP_CONF_RSP, len, rsp);
	l2cap_pi(sk)->num_conf_rsp++;

	/* Reset config buffer. */
	l2cap_pi(sk)->conf_len = 0;

	if (!(l2cap_pi(sk)->conf_state & L2CAP_CONF_OUTPUT_DONE))
		goto unlock;

	if (l2cap_pi(sk)->conf_state & L2CAP_CONF_INPUT_DONE) {
		if (!(l2cap_pi(sk)->conf_state & L2CAP_CONF_NO_FCS_RECV) ||
		    l2cap_pi(sk)->fcs != L2CAP_FCS_NONE)
			l2cap_pi(sk)->fcs = L2CAP_FCS_CRC16;

		sk->sk_state = BT_CONNECTED;

		l2cap_pi(sk)->next_tx_seq = 0;
		l2cap_pi(sk)->expected_tx_seq = 0;
		__skb_queue_head_init(TX_QUEUE(sk));
		if (l2cap_pi(sk)->mode == L2CAP_MODE_ERTM)
			l2cap_ertm_init(sk);

		l2cap_chan_ready(sk);
		goto unlock;
	}

	if (!(l2cap_pi(sk)->conf_state & L2CAP_CONF_REQ_SENT)) {
		u8 buf[64];
		l2cap_send_cmd(conn, l2cap_get_ident(conn), L2CAP_CONF_REQ,
					l2cap_build_conf_req(sk, buf), buf);
		l2cap_pi(sk)->num_conf_req++;
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

	BT_DBG("scid 0x%4.4x flags 0x%2.2x result 0x%2.2x",
			scid, flags, result);

	sk = l2cap_get_chan_by_scid(&conn->chan_list, scid);
	if (!sk)
		return 0;

	switch (result) {
	case L2CAP_CONF_SUCCESS:
		break;

	case L2CAP_CONF_UNACCEPT:
		if (l2cap_pi(sk)->num_conf_rsp <= L2CAP_CONF_MAX_CONF_RSP) {
			int len = cmd->len - sizeof(*rsp);
			char req[64];

			/* throw out any old stored conf requests */
			result = L2CAP_CONF_SUCCESS;
			len = l2cap_parse_conf_rsp(sk, rsp->data,
							len, req, &result);
			if (len < 0) {
				l2cap_send_disconn_req(conn, sk);
				goto done;
			}

			l2cap_send_cmd(conn, l2cap_get_ident(conn),
						L2CAP_CONF_REQ, len, req);
			l2cap_pi(sk)->num_conf_req++;
			if (result != L2CAP_CONF_SUCCESS)
				goto done;
			break;
		}

	default:
		sk->sk_state = BT_DISCONN;
		sk->sk_err = ECONNRESET;
		l2cap_sock_set_timer(sk, HZ * 5);
		l2cap_send_disconn_req(conn, sk);
		goto done;
	}

	if (flags & 0x01)
		goto done;

	l2cap_pi(sk)->conf_state |= L2CAP_CONF_INPUT_DONE;

	if (l2cap_pi(sk)->conf_state & L2CAP_CONF_OUTPUT_DONE) {
		if (!(l2cap_pi(sk)->conf_state & L2CAP_CONF_NO_FCS_RECV) ||
		    l2cap_pi(sk)->fcs != L2CAP_FCS_NONE)
			l2cap_pi(sk)->fcs = L2CAP_FCS_CRC16;

		sk->sk_state = BT_CONNECTED;
		l2cap_pi(sk)->next_tx_seq = 0;
		l2cap_pi(sk)->expected_tx_seq = 0;
		__skb_queue_head_init(TX_QUEUE(sk));
		if (l2cap_pi(sk)->mode ==  L2CAP_MODE_ERTM)
			l2cap_ertm_init(sk);

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

	sk = l2cap_get_chan_by_scid(&conn->chan_list, dcid);
	if (!sk)
		return 0;

	rsp.dcid = cpu_to_le16(l2cap_pi(sk)->scid);
	rsp.scid = cpu_to_le16(l2cap_pi(sk)->dcid);
	l2cap_send_cmd(conn, cmd->ident, L2CAP_DISCONN_RSP, sizeof(rsp), &rsp);

	sk->sk_shutdown = SHUTDOWN_MASK;

	skb_queue_purge(TX_QUEUE(sk));

	if (l2cap_pi(sk)->mode == L2CAP_MODE_ERTM) {
		skb_queue_purge(SREJ_QUEUE(sk));
		del_timer(&l2cap_pi(sk)->retrans_timer);
		del_timer(&l2cap_pi(sk)->monitor_timer);
	}

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

	sk = l2cap_get_chan_by_scid(&conn->chan_list, scid);
	if (!sk)
		return 0;

	skb_queue_purge(TX_QUEUE(sk));

	if (l2cap_pi(sk)->mode == L2CAP_MODE_ERTM) {
		skb_queue_purge(SREJ_QUEUE(sk));
		del_timer(&l2cap_pi(sk)->retrans_timer);
		del_timer(&l2cap_pi(sk)->monitor_timer);
	}

	l2cap_chan_del(sk, 0);
	bh_unlock_sock(sk);

	l2cap_sock_kill(sk);
	return 0;
}

static inline int l2cap_information_req(struct l2cap_conn *conn, struct l2cap_cmd_hdr *cmd, u8 *data)
{
	struct l2cap_info_req *req = (struct l2cap_info_req *) data;
	u16 type;

	type = __le16_to_cpu(req->type);

	BT_DBG("type 0x%4.4x", type);

	if (type == L2CAP_IT_FEAT_MASK) {
		u8 buf[8];
		u32 feat_mask = l2cap_feat_mask;
		struct l2cap_info_rsp *rsp = (struct l2cap_info_rsp *) buf;
		rsp->type   = cpu_to_le16(L2CAP_IT_FEAT_MASK);
		rsp->result = cpu_to_le16(L2CAP_IR_SUCCESS);
		if (enable_ertm)
			feat_mask |= L2CAP_FEAT_ERTM | L2CAP_FEAT_STREAMING
							 | L2CAP_FEAT_FCS;
		put_unaligned_le32(feat_mask, rsp->data);
		l2cap_send_cmd(conn, cmd->ident,
					L2CAP_INFO_RSP, sizeof(buf), buf);
	} else if (type == L2CAP_IT_FIXED_CHAN) {
		u8 buf[12];
		struct l2cap_info_rsp *rsp = (struct l2cap_info_rsp *) buf;
		rsp->type   = cpu_to_le16(L2CAP_IT_FIXED_CHAN);
		rsp->result = cpu_to_le16(L2CAP_IR_SUCCESS);
		memcpy(buf + 4, l2cap_fixed_chan, 8);
		l2cap_send_cmd(conn, cmd->ident,
					L2CAP_INFO_RSP, sizeof(buf), buf);
	} else {
		struct l2cap_info_rsp rsp;
		rsp.type   = cpu_to_le16(type);
		rsp.result = cpu_to_le16(L2CAP_IR_NOTSUPP);
		l2cap_send_cmd(conn, cmd->ident,
					L2CAP_INFO_RSP, sizeof(rsp), &rsp);
	}

	return 0;
}

static inline int l2cap_information_rsp(struct l2cap_conn *conn, struct l2cap_cmd_hdr *cmd, u8 *data)
{
	struct l2cap_info_rsp *rsp = (struct l2cap_info_rsp *) data;
	u16 type, result;

	type   = __le16_to_cpu(rsp->type);
	result = __le16_to_cpu(rsp->result);

	BT_DBG("type 0x%4.4x result 0x%2.2x", type, result);

	del_timer(&conn->info_timer);

	if (type == L2CAP_IT_FEAT_MASK) {
		conn->feat_mask = get_unaligned_le32(rsp->data);

		if (conn->feat_mask & L2CAP_FEAT_FIXED_CHAN) {
			struct l2cap_info_req req;
			req.type = cpu_to_le16(L2CAP_IT_FIXED_CHAN);

			conn->info_ident = l2cap_get_ident(conn);

			l2cap_send_cmd(conn, conn->info_ident,
					L2CAP_INFO_REQ, sizeof(req), &req);
		} else {
			conn->info_state |= L2CAP_INFO_FEAT_MASK_REQ_DONE;
			conn->info_ident = 0;

			l2cap_conn_start(conn);
		}
	} else if (type == L2CAP_IT_FIXED_CHAN) {
		conn->info_state |= L2CAP_INFO_FEAT_MASK_REQ_DONE;
		conn->info_ident = 0;

		l2cap_conn_start(conn);
	}

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
		u16 cmd_len;
		memcpy(&cmd, data, L2CAP_CMD_HDR_SIZE);
		data += L2CAP_CMD_HDR_SIZE;
		len  -= L2CAP_CMD_HDR_SIZE;

		cmd_len = le16_to_cpu(cmd.len);

		BT_DBG("code 0x%2.2x len %d id 0x%2.2x", cmd.code, cmd_len, cmd.ident);

		if (cmd_len > len || !cmd.ident) {
			BT_DBG("corrupted command");
			break;
		}

		switch (cmd.code) {
		case L2CAP_COMMAND_REJ:
			l2cap_command_rej(conn, &cmd, data);
			break;

		case L2CAP_CONN_REQ:
			err = l2cap_connect_req(conn, &cmd, data);
			break;

		case L2CAP_CONN_RSP:
			err = l2cap_connect_rsp(conn, &cmd, data);
			break;

		case L2CAP_CONF_REQ:
			err = l2cap_config_req(conn, &cmd, cmd_len, data);
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
			l2cap_send_cmd(conn, cmd.ident, L2CAP_ECHO_RSP, cmd_len, data);
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
			rej.reason = cpu_to_le16(0);
			l2cap_send_cmd(conn, cmd.ident, L2CAP_COMMAND_REJ, sizeof(rej), &rej);
		}

		data += cmd_len;
		len  -= cmd_len;
	}

	kfree_skb(skb);
}

static int l2cap_check_fcs(struct l2cap_pinfo *pi,  struct sk_buff *skb)
{
	u16 our_fcs, rcv_fcs;
	int hdr_size = L2CAP_HDR_SIZE + 2;

	if (pi->fcs == L2CAP_FCS_CRC16) {
		skb_trim(skb, skb->len - 2);
		rcv_fcs = get_unaligned_le16(skb->data + skb->len);
		our_fcs = crc16(0, skb->data - hdr_size, skb->len + hdr_size);

		if (our_fcs != rcv_fcs)
			return -EINVAL;
	}
	return 0;
}

static void l2cap_add_to_srej_queue(struct sock *sk, struct sk_buff *skb, u8 tx_seq, u8 sar)
{
	struct sk_buff *next_skb;

	bt_cb(skb)->tx_seq = tx_seq;
	bt_cb(skb)->sar = sar;

	next_skb = skb_peek(SREJ_QUEUE(sk));
	if (!next_skb) {
		__skb_queue_tail(SREJ_QUEUE(sk), skb);
		return;
	}

	do {
		if (bt_cb(next_skb)->tx_seq > tx_seq) {
			__skb_queue_before(SREJ_QUEUE(sk), next_skb, skb);
			return;
		}

		if (skb_queue_is_last(SREJ_QUEUE(sk), next_skb))
			break;

	} while((next_skb = skb_queue_next(SREJ_QUEUE(sk), next_skb)));

	__skb_queue_tail(SREJ_QUEUE(sk), skb);
}

static int l2cap_sar_reassembly_sdu(struct sock *sk, struct sk_buff *skb, u16 control)
{
	struct l2cap_pinfo *pi = l2cap_pi(sk);
	struct sk_buff *_skb;
	int err = -EINVAL;

	switch (control & L2CAP_CTRL_SAR) {
	case L2CAP_SDU_UNSEGMENTED:
		if (pi->conn_state & L2CAP_CONN_SAR_SDU) {
			kfree_skb(pi->sdu);
			break;
		}

		err = sock_queue_rcv_skb(sk, skb);
		if (!err)
			return 0;

		break;

	case L2CAP_SDU_START:
		if (pi->conn_state & L2CAP_CONN_SAR_SDU) {
			kfree_skb(pi->sdu);
			break;
		}

		pi->sdu_len = get_unaligned_le16(skb->data);
		skb_pull(skb, 2);

		pi->sdu = bt_skb_alloc(pi->sdu_len, GFP_ATOMIC);
		if (!pi->sdu) {
			err = -ENOMEM;
			break;
		}

		memcpy(skb_put(pi->sdu, skb->len), skb->data, skb->len);

		pi->conn_state |= L2CAP_CONN_SAR_SDU;
		pi->partial_sdu_len = skb->len;
		err = 0;
		break;

	case L2CAP_SDU_CONTINUE:
		if (!(pi->conn_state & L2CAP_CONN_SAR_SDU))
			break;

		memcpy(skb_put(pi->sdu, skb->len), skb->data, skb->len);

		pi->partial_sdu_len += skb->len;
		if (pi->partial_sdu_len > pi->sdu_len)
			kfree_skb(pi->sdu);
		else
			err = 0;

		break;

	case L2CAP_SDU_END:
		if (!(pi->conn_state & L2CAP_CONN_SAR_SDU))
			break;

		memcpy(skb_put(pi->sdu, skb->len), skb->data, skb->len);

		pi->conn_state &= ~L2CAP_CONN_SAR_SDU;
		pi->partial_sdu_len += skb->len;

		if (pi->partial_sdu_len == pi->sdu_len) {
			_skb = skb_clone(pi->sdu, GFP_ATOMIC);
			err = sock_queue_rcv_skb(sk, _skb);
			if (err < 0)
				kfree_skb(_skb);
		}
		kfree_skb(pi->sdu);
		err = 0;

		break;
	}

	kfree_skb(skb);
	return err;
}

static void l2cap_check_srej_gap(struct sock *sk, u8 tx_seq)
{
	struct sk_buff *skb;
	u16 control = 0;

	while((skb = skb_peek(SREJ_QUEUE(sk)))) {
		if (bt_cb(skb)->tx_seq != tx_seq)
			break;

		skb = skb_dequeue(SREJ_QUEUE(sk));
		control |= bt_cb(skb)->sar << L2CAP_CTRL_SAR_SHIFT;
		l2cap_sar_reassembly_sdu(sk, skb, control);
		l2cap_pi(sk)->buffer_seq_srej =
			(l2cap_pi(sk)->buffer_seq_srej + 1) % 64;
		tx_seq++;
	}
}

static void l2cap_resend_srejframe(struct sock *sk, u8 tx_seq)
{
	struct l2cap_pinfo *pi = l2cap_pi(sk);
	struct srej_list *l, *tmp;
	u16 control;

	list_for_each_entry_safe(l,tmp, SREJ_LIST(sk), list) {
		if (l->tx_seq == tx_seq) {
			list_del(&l->list);
			kfree(l);
			return;
		}
		control = L2CAP_SUPER_SELECT_REJECT;
		control |= l->tx_seq << L2CAP_CTRL_REQSEQ_SHIFT;
		l2cap_send_sframe(pi, control);
		list_del(&l->list);
		list_add_tail(&l->list, SREJ_LIST(sk));
	}
}

static void l2cap_send_srejframe(struct sock *sk, u8 tx_seq)
{
	struct l2cap_pinfo *pi = l2cap_pi(sk);
	struct srej_list *new;
	u16 control;

	while (tx_seq != pi->expected_tx_seq) {
		control = L2CAP_SUPER_SELECT_REJECT;
		control |= pi->expected_tx_seq << L2CAP_CTRL_REQSEQ_SHIFT;
		if (pi->conn_state & L2CAP_CONN_SEND_PBIT) {
			control |= L2CAP_CTRL_POLL;
			pi->conn_state &= ~L2CAP_CONN_SEND_PBIT;
		}
		l2cap_send_sframe(pi, control);

		new = kzalloc(sizeof(struct srej_list), GFP_ATOMIC);
		new->tx_seq = pi->expected_tx_seq++;
		list_add_tail(&new->list, SREJ_LIST(sk));
	}
	pi->expected_tx_seq++;
}

static inline int l2cap_data_channel_iframe(struct sock *sk, u16 rx_control, struct sk_buff *skb)
{
	struct l2cap_pinfo *pi = l2cap_pi(sk);
	u8 tx_seq = __get_txseq(rx_control);
	u8 req_seq = __get_reqseq(rx_control);
	u16 tx_control = 0;
	u8 sar = rx_control >> L2CAP_CTRL_SAR_SHIFT;
	int err = 0;

	BT_DBG("sk %p rx_control 0x%4.4x len %d", sk, rx_control, skb->len);

	pi->expected_ack_seq = req_seq;
	l2cap_drop_acked_frames(sk);

	if (tx_seq == pi->expected_tx_seq)
		goto expected;

	if (pi->conn_state & L2CAP_CONN_SREJ_SENT) {
		struct srej_list *first;

		first = list_first_entry(SREJ_LIST(sk),
				struct srej_list, list);
		if (tx_seq == first->tx_seq) {
			l2cap_add_to_srej_queue(sk, skb, tx_seq, sar);
			l2cap_check_srej_gap(sk, tx_seq);

			list_del(&first->list);
			kfree(first);

			if (list_empty(SREJ_LIST(sk))) {
				pi->buffer_seq = pi->buffer_seq_srej;
				pi->conn_state &= ~L2CAP_CONN_SREJ_SENT;
			}
		} else {
			struct srej_list *l;
			l2cap_add_to_srej_queue(sk, skb, tx_seq, sar);

			list_for_each_entry(l, SREJ_LIST(sk), list) {
				if (l->tx_seq == tx_seq) {
					l2cap_resend_srejframe(sk, tx_seq);
					return 0;
				}
			}
			l2cap_send_srejframe(sk, tx_seq);
		}
	} else {
		pi->conn_state |= L2CAP_CONN_SREJ_SENT;

		INIT_LIST_HEAD(SREJ_LIST(sk));
		pi->buffer_seq_srej = pi->buffer_seq;

		__skb_queue_head_init(SREJ_QUEUE(sk));
		l2cap_add_to_srej_queue(sk, skb, tx_seq, sar);

		pi->conn_state |= L2CAP_CONN_SEND_PBIT;

		l2cap_send_srejframe(sk, tx_seq);
	}
	return 0;

expected:
	pi->expected_tx_seq = (pi->expected_tx_seq + 1) % 64;

	if (pi->conn_state & L2CAP_CONN_SREJ_SENT) {
		l2cap_add_to_srej_queue(sk, skb, tx_seq, sar);
		return 0;
	}

	if (rx_control & L2CAP_CTRL_FINAL) {
		if (pi->conn_state & L2CAP_CONN_REJ_ACT)
			pi->conn_state &= ~L2CAP_CONN_REJ_ACT;
		else {
			sk->sk_send_head = TX_QUEUE(sk)->next;
			pi->next_tx_seq = pi->expected_ack_seq;
			l2cap_ertm_send(sk);
		}
	}

	pi->buffer_seq = (pi->buffer_seq + 1) % 64;

	err = l2cap_sar_reassembly_sdu(sk, skb, rx_control);
	if (err < 0)
		return err;

	pi->num_to_ack = (pi->num_to_ack + 1) % L2CAP_DEFAULT_NUM_TO_ACK;
	if (pi->num_to_ack == L2CAP_DEFAULT_NUM_TO_ACK - 1) {
		tx_control |= L2CAP_SUPER_RCV_READY;
		tx_control |= pi->buffer_seq << L2CAP_CTRL_REQSEQ_SHIFT;
		l2cap_send_sframe(pi, tx_control);
	}
	return 0;
}

static inline int l2cap_data_channel_sframe(struct sock *sk, u16 rx_control, struct sk_buff *skb)
{
	struct l2cap_pinfo *pi = l2cap_pi(sk);
	u8 tx_seq = __get_reqseq(rx_control);

	BT_DBG("sk %p rx_control 0x%4.4x len %d", sk, rx_control, skb->len);

	switch (rx_control & L2CAP_CTRL_SUPERVISE) {
	case L2CAP_SUPER_RCV_READY:
		if (rx_control & L2CAP_CTRL_POLL) {
			u16 control = L2CAP_CTRL_FINAL;
			control |= L2CAP_SUPER_RCV_READY |
				(pi->buffer_seq << L2CAP_CTRL_REQSEQ_SHIFT);
			l2cap_send_sframe(l2cap_pi(sk), control);
			pi->conn_state &= ~L2CAP_CONN_REMOTE_BUSY;

		} else if (rx_control & L2CAP_CTRL_FINAL) {
			pi->conn_state &= ~L2CAP_CONN_REMOTE_BUSY;
			pi->expected_ack_seq = tx_seq;
			l2cap_drop_acked_frames(sk);

			if (pi->conn_state & L2CAP_CONN_REJ_ACT)
				pi->conn_state &= ~L2CAP_CONN_REJ_ACT;
			else {
				sk->sk_send_head = TX_QUEUE(sk)->next;
				pi->next_tx_seq = pi->expected_ack_seq;
				l2cap_ertm_send(sk);
			}

			if (!(pi->conn_state & L2CAP_CONN_WAIT_F))
				break;

			pi->conn_state &= ~L2CAP_CONN_WAIT_F;
			del_timer(&pi->monitor_timer);

			if (pi->unacked_frames > 0)
				__mod_retrans_timer();
		} else {
			pi->expected_ack_seq = tx_seq;
			l2cap_drop_acked_frames(sk);

			if ((pi->conn_state & L2CAP_CONN_REMOTE_BUSY) &&
			    (pi->unacked_frames > 0))
				__mod_retrans_timer();

			pi->conn_state &= ~L2CAP_CONN_REMOTE_BUSY;
			l2cap_ertm_send(sk);
		}
		break;

	case L2CAP_SUPER_REJECT:
		pi->conn_state &= ~L2CAP_CONN_REMOTE_BUSY;

		pi->expected_ack_seq = __get_reqseq(rx_control);
		l2cap_drop_acked_frames(sk);

		if (rx_control & L2CAP_CTRL_FINAL) {
			if (pi->conn_state & L2CAP_CONN_REJ_ACT)
				pi->conn_state &= ~L2CAP_CONN_REJ_ACT;
			else {
				sk->sk_send_head = TX_QUEUE(sk)->next;
				pi->next_tx_seq = pi->expected_ack_seq;
				l2cap_ertm_send(sk);
			}
		} else {
			sk->sk_send_head = TX_QUEUE(sk)->next;
			pi->next_tx_seq = pi->expected_ack_seq;
			l2cap_ertm_send(sk);

			if (pi->conn_state & L2CAP_CONN_WAIT_F) {
				pi->srej_save_reqseq = tx_seq;
				pi->conn_state |= L2CAP_CONN_REJ_ACT;
			}
		}

		break;

	case L2CAP_SUPER_SELECT_REJECT:
		pi->conn_state &= ~L2CAP_CONN_REMOTE_BUSY;

		if (rx_control & L2CAP_CTRL_POLL) {
			pi->expected_ack_seq = tx_seq;
			l2cap_drop_acked_frames(sk);
			l2cap_retransmit_frame(sk, tx_seq);
			l2cap_ertm_send(sk);
			if (pi->conn_state & L2CAP_CONN_WAIT_F) {
				pi->srej_save_reqseq = tx_seq;
				pi->conn_state |= L2CAP_CONN_SREJ_ACT;
			}
		} else if (rx_control & L2CAP_CTRL_FINAL) {
			if ((pi->conn_state & L2CAP_CONN_SREJ_ACT) &&
					pi->srej_save_reqseq == tx_seq)
				pi->conn_state &= ~L2CAP_CONN_SREJ_ACT;
			else
				l2cap_retransmit_frame(sk, tx_seq);
		}
		else {
			l2cap_retransmit_frame(sk, tx_seq);
			if (pi->conn_state & L2CAP_CONN_WAIT_F) {
				pi->srej_save_reqseq = tx_seq;
				pi->conn_state |= L2CAP_CONN_SREJ_ACT;
			}
		}
		break;

	case L2CAP_SUPER_RCV_NOT_READY:
		pi->conn_state |= L2CAP_CONN_REMOTE_BUSY;
		pi->expected_ack_seq = tx_seq;
		l2cap_drop_acked_frames(sk);

		del_timer(&l2cap_pi(sk)->retrans_timer);
		if (rx_control & L2CAP_CTRL_POLL) {
			u16 control = L2CAP_CTRL_FINAL;
			l2cap_send_rr_or_rnr(l2cap_pi(sk), control);
		}
		break;
	}

	return 0;
}

static inline int l2cap_data_channel(struct l2cap_conn *conn, u16 cid, struct sk_buff *skb)
{
	struct sock *sk;
	struct l2cap_pinfo *pi;
	u16 control, len;
	u8 tx_seq;

	sk = l2cap_get_chan_by_scid(&conn->chan_list, cid);
	if (!sk) {
		BT_DBG("unknown cid 0x%4.4x", cid);
		goto drop;
	}

	pi = l2cap_pi(sk);

	BT_DBG("sk %p, len %d", sk, skb->len);

	if (sk->sk_state != BT_CONNECTED)
		goto drop;

	switch (pi->mode) {
	case L2CAP_MODE_BASIC:
		/* If socket recv buffers overflows we drop data here
		 * which is *bad* because L2CAP has to be reliable.
		 * But we don't have any other choice. L2CAP doesn't
		 * provide flow control mechanism. */

		if (pi->imtu < skb->len)
			goto drop;

		if (!sock_queue_rcv_skb(sk, skb))
			goto done;
		break;

	case L2CAP_MODE_ERTM:
		control = get_unaligned_le16(skb->data);
		skb_pull(skb, 2);
		len = skb->len;

		if (__is_sar_start(control))
			len -= 2;

		if (pi->fcs == L2CAP_FCS_CRC16)
			len -= 2;

		/*
		 * We can just drop the corrupted I-frame here.
		 * Receiver will miss it and start proper recovery
		 * procedures and ask retransmission.
		 */
		if (len > L2CAP_DEFAULT_MAX_PDU_SIZE)
			goto drop;

		if (l2cap_check_fcs(pi, skb))
			goto drop;

		if (__is_iframe(control))
			l2cap_data_channel_iframe(sk, control, skb);
		else
			l2cap_data_channel_sframe(sk, control, skb);

		goto done;

	case L2CAP_MODE_STREAMING:
		control = get_unaligned_le16(skb->data);
		skb_pull(skb, 2);
		len = skb->len;

		if (__is_sar_start(control))
			len -= 2;

		if (pi->fcs == L2CAP_FCS_CRC16)
			len -= 2;

		if (len > L2CAP_DEFAULT_MAX_PDU_SIZE || __is_sframe(control))
			goto drop;

		if (l2cap_check_fcs(pi, skb))
			goto drop;

		tx_seq = __get_txseq(control);

		if (pi->expected_tx_seq == tx_seq)
			pi->expected_tx_seq = (pi->expected_tx_seq + 1) % 64;
		else
			pi->expected_tx_seq = tx_seq + 1;

		l2cap_sar_reassembly_sdu(sk, skb, control);

		goto done;

	default:
		BT_DBG("sk %p: bad mode 0x%2.2x", sk, l2cap_pi(sk)->mode);
		break;
	}

drop:
	kfree_skb(skb);

done:
	if (sk)
		bh_unlock_sock(sk);

	return 0;
}

static inline int l2cap_conless_channel(struct l2cap_conn *conn, __le16 psm, struct sk_buff *skb)
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
	if (sk)
		bh_unlock_sock(sk);
	return 0;
}

static void l2cap_recv_frame(struct l2cap_conn *conn, struct sk_buff *skb)
{
	struct l2cap_hdr *lh = (void *) skb->data;
	u16 cid, len;
	__le16 psm;

	skb_pull(skb, L2CAP_HDR_SIZE);
	cid = __le16_to_cpu(lh->cid);
	len = __le16_to_cpu(lh->len);

	if (len != skb->len) {
		kfree_skb(skb);
		return;
	}

	BT_DBG("len %d, cid 0x%4.4x", len, cid);

	switch (cid) {
	case L2CAP_CID_SIGNALING:
		l2cap_sig_channel(conn, skb);
		break;

	case L2CAP_CID_CONN_LESS:
		psm = get_unaligned_le16(skb->data);
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
			lm1 |= HCI_LM_ACCEPT;
			if (l2cap_pi(sk)->role_switch)
				lm1 |= HCI_LM_MASTER;
			exact++;
		} else if (!bacmp(&bt_sk(sk)->src, BDADDR_ANY)) {
			lm2 |= HCI_LM_ACCEPT;
			if (l2cap_pi(sk)->role_switch)
				lm2 |= HCI_LM_MASTER;
		}
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

static int l2cap_disconn_ind(struct hci_conn *hcon)
{
	struct l2cap_conn *conn = hcon->l2cap_data;

	BT_DBG("hcon %p", hcon);

	if (hcon->type != ACL_LINK || !conn)
		return 0x13;

	return conn->disc_reason;
}

static int l2cap_disconn_cfm(struct hci_conn *hcon, u8 reason)
{
	BT_DBG("hcon %p reason %d", hcon, reason);

	if (hcon->type != ACL_LINK)
		return 0;

	l2cap_conn_del(hcon, bt_err(reason));

	return 0;
}

static inline void l2cap_check_encryption(struct sock *sk, u8 encrypt)
{
	if (sk->sk_type != SOCK_SEQPACKET)
		return;

	if (encrypt == 0x00) {
		if (l2cap_pi(sk)->sec_level == BT_SECURITY_MEDIUM) {
			l2cap_sock_clear_timer(sk);
			l2cap_sock_set_timer(sk, HZ * 5);
		} else if (l2cap_pi(sk)->sec_level == BT_SECURITY_HIGH)
			__l2cap_sock_close(sk, ECONNREFUSED);
	} else {
		if (l2cap_pi(sk)->sec_level == BT_SECURITY_MEDIUM)
			l2cap_sock_clear_timer(sk);
	}
}

static int l2cap_security_cfm(struct hci_conn *hcon, u8 status, u8 encrypt)
{
	struct l2cap_chan_list *l;
	struct l2cap_conn *conn = hcon->l2cap_data;
	struct sock *sk;

	if (!conn)
		return 0;

	l = &conn->chan_list;

	BT_DBG("conn %p", conn);

	read_lock(&l->lock);

	for (sk = l->head; sk; sk = l2cap_pi(sk)->next_c) {
		bh_lock_sock(sk);

		if (l2cap_pi(sk)->conf_state & L2CAP_CONF_CONNECT_PEND) {
			bh_unlock_sock(sk);
			continue;
		}

		if (!status && (sk->sk_state == BT_CONNECTED ||
						sk->sk_state == BT_CONFIG)) {
			l2cap_check_encryption(sk, encrypt);
			bh_unlock_sock(sk);
			continue;
		}

		if (sk->sk_state == BT_CONNECT) {
			if (!status) {
				struct l2cap_conn_req req;
				req.scid = cpu_to_le16(l2cap_pi(sk)->scid);
				req.psm  = l2cap_pi(sk)->psm;

				l2cap_pi(sk)->ident = l2cap_get_ident(conn);

				l2cap_send_cmd(conn, l2cap_pi(sk)->ident,
					L2CAP_CONN_REQ, sizeof(req), &req);
			} else {
				l2cap_sock_clear_timer(sk);
				l2cap_sock_set_timer(sk, HZ / 10);
			}
		} else if (sk->sk_state == BT_CONNECT2) {
			struct l2cap_conn_rsp rsp;
			__u16 result;

			if (!status) {
				sk->sk_state = BT_CONFIG;
				result = L2CAP_CR_SUCCESS;
			} else {
				sk->sk_state = BT_DISCONN;
				l2cap_sock_set_timer(sk, HZ / 10);
				result = L2CAP_CR_SEC_BLOCK;
			}

			rsp.scid   = cpu_to_le16(l2cap_pi(sk)->dcid);
			rsp.dcid   = cpu_to_le16(l2cap_pi(sk)->scid);
			rsp.result = cpu_to_le16(result);
			rsp.status = cpu_to_le16(L2CAP_CS_NO_INFO);
			l2cap_send_cmd(conn, l2cap_pi(sk)->ident,
					L2CAP_CONN_RSP, sizeof(rsp), &rsp);
		}

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
		conn->rx_skb = bt_skb_alloc(len, GFP_ATOMIC);
		if (!conn->rx_skb)
			goto drop;

		skb_copy_from_linear_data(skb, skb_put(conn->rx_skb, skb->len),
								skb->len);
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

		skb_copy_from_linear_data(skb, skb_put(conn->rx_skb, skb->len),
								skb->len);
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

static ssize_t l2cap_sysfs_show(struct class *dev,
				struct class_attribute *attr,
				char *buf)
{
	struct sock *sk;
	struct hlist_node *node;
	char *str = buf;

	read_lock_bh(&l2cap_sk_list.lock);

	sk_for_each(sk, node, &l2cap_sk_list.head) {
		struct l2cap_pinfo *pi = l2cap_pi(sk);

		str += sprintf(str, "%s %s %d %d 0x%4.4x 0x%4.4x %d %d %d\n",
				batostr(&bt_sk(sk)->src), batostr(&bt_sk(sk)->dst),
				sk->sk_state, __le16_to_cpu(pi->psm), pi->scid,
				pi->dcid, pi->imtu, pi->omtu, pi->sec_level);
	}

	read_unlock_bh(&l2cap_sk_list.lock);

	return str - buf;
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
	.recvmsg	= l2cap_sock_recvmsg,
	.poll		= bt_sock_poll,
	.ioctl		= bt_sock_ioctl,
	.mmap		= sock_no_mmap,
	.socketpair	= sock_no_socketpair,
	.shutdown	= l2cap_sock_shutdown,
	.setsockopt	= l2cap_sock_setsockopt,
	.getsockopt	= l2cap_sock_getsockopt
};

static const struct net_proto_family l2cap_sock_family_ops = {
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
	.disconn_cfm	= l2cap_disconn_cfm,
	.security_cfm	= l2cap_security_cfm,
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

module_param(enable_ertm, bool, 0644);
MODULE_PARM_DESC(enable_ertm, "Enable enhanced retransmission mode");

module_param(max_transmit, uint, 0644);
MODULE_PARM_DESC(max_transmit, "Max transmit value (default = 3)");

MODULE_AUTHOR("Marcel Holtmann <marcel@holtmann.org>");
MODULE_DESCRIPTION("Bluetooth L2CAP ver " VERSION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");
MODULE_ALIAS("bt-proto-0");
