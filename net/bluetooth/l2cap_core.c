/*
   BlueZ - Bluetooth protocol stack for Linux
   Copyright (C) 2000-2001 Qualcomm Incorporated
   Copyright (C) 2009-2010 Gustavo F. Padovan <gustavo@padovan.org>
   Copyright (C) 2010 Google Inc.

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

/* Bluetooth L2CAP core. */

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
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/crc16.h>
#include <net/sock.h>

#include <asm/system.h>
#include <asm/unaligned.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include <net/bluetooth/l2cap.h>

int disable_ertm;

static u32 l2cap_feat_mask = L2CAP_FEAT_FIXED_CHAN;
static u8 l2cap_fixed_chan[8] = { 0x02, };

static struct workqueue_struct *_busy_wq;

struct bt_sock_list l2cap_sk_list = {
	.lock = __RW_LOCK_UNLOCKED(l2cap_sk_list.lock)
};

static void l2cap_busy_work(struct work_struct *work);

static struct sk_buff *l2cap_build_cmd(struct l2cap_conn *conn,
				u8 code, u8 ident, u16 dlen, void *data);

static int l2cap_ertm_data_rcv(struct sock *sk, struct sk_buff *skb);

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

	if (sk->sk_type == SOCK_SEQPACKET || sk->sk_type == SOCK_STREAM) {
		if (conn->hcon->type == LE_LINK) {
			/* LE connection */
			l2cap_pi(sk)->omtu = L2CAP_LE_DEFAULT_MTU;
			l2cap_pi(sk)->scid = L2CAP_CID_LE_DATA;
			l2cap_pi(sk)->dcid = L2CAP_CID_LE_DATA;
		} else {
			/* Alloc CID for connection-oriented socket */
			l2cap_pi(sk)->scid = l2cap_alloc_cid(l);
			l2cap_pi(sk)->omtu = L2CAP_DEFAULT_MTU;
		}
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
void l2cap_chan_del(struct sock *sk, int err)
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

	skb_queue_purge(TX_QUEUE(sk));

	if (l2cap_pi(sk)->mode == L2CAP_MODE_ERTM) {
		struct srej_list *l, *tmp;

		del_timer(&l2cap_pi(sk)->retrans_timer);
		del_timer(&l2cap_pi(sk)->monitor_timer);
		del_timer(&l2cap_pi(sk)->ack_timer);

		skb_queue_purge(SREJ_QUEUE(sk));
		skb_queue_purge(BUSY_QUEUE(sk));

		list_for_each_entry_safe(l, tmp, SREJ_LIST(sk), list) {
			list_del(&l->list);
			kfree(l);
		}
	}
}

static inline u8 l2cap_get_auth_type(struct sock *sk)
{
	if (sk->sk_type == SOCK_RAW) {
		switch (l2cap_pi(sk)->sec_level) {
		case BT_SECURITY_HIGH:
			return HCI_AT_DEDICATED_BONDING_MITM;
		case BT_SECURITY_MEDIUM:
			return HCI_AT_DEDICATED_BONDING;
		default:
			return HCI_AT_NO_BONDING;
		}
	} else if (l2cap_pi(sk)->psm == cpu_to_le16(0x0001)) {
		if (l2cap_pi(sk)->sec_level == BT_SECURITY_LOW)
			l2cap_pi(sk)->sec_level = BT_SECURITY_SDP;

		if (l2cap_pi(sk)->sec_level == BT_SECURITY_HIGH)
			return HCI_AT_NO_BONDING_MITM;
		else
			return HCI_AT_NO_BONDING;
	} else {
		switch (l2cap_pi(sk)->sec_level) {
		case BT_SECURITY_HIGH:
			return HCI_AT_GENERAL_BONDING_MITM;
		case BT_SECURITY_MEDIUM:
			return HCI_AT_GENERAL_BONDING;
		default:
			return HCI_AT_NO_BONDING;
		}
	}
}

/* Service level security */
static inline int l2cap_check_security(struct sock *sk)
{
	struct l2cap_conn *conn = l2cap_pi(sk)->conn;
	__u8 auth_type;

	auth_type = l2cap_get_auth_type(sk);

	return hci_conn_security(conn->hcon, l2cap_pi(sk)->sec_level,
								auth_type);
}

u8 l2cap_get_ident(struct l2cap_conn *conn)
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

void l2cap_send_cmd(struct l2cap_conn *conn, u8 ident, u8 code, u16 len, void *data)
{
	struct sk_buff *skb = l2cap_build_cmd(conn, code, ident, len, data);
	u8 flags;

	BT_DBG("code 0x%2.2x", code);

	if (!skb)
		return;

	if (lmp_no_flush_capable(conn->hcon->hdev))
		flags = ACL_START_NO_FLUSH;
	else
		flags = ACL_START;

	hci_send_acl(conn->hcon, skb, flags);
}

static inline void l2cap_send_sframe(struct l2cap_pinfo *pi, u16 control)
{
	struct sk_buff *skb;
	struct l2cap_hdr *lh;
	struct l2cap_conn *conn = pi->conn;
	struct sock *sk = (struct sock *)pi;
	int count, hlen = L2CAP_HDR_SIZE + 2;
	u8 flags;

	if (sk->sk_state != BT_CONNECTED)
		return;

	if (pi->fcs == L2CAP_FCS_CRC16)
		hlen += 2;

	BT_DBG("pi %p, control 0x%2.2x", pi, control);

	count = min_t(unsigned int, conn->mtu, hlen);
	control |= L2CAP_CTRL_FRAME_TYPE;

	if (pi->conn_state & L2CAP_CONN_SEND_FBIT) {
		control |= L2CAP_CTRL_FINAL;
		pi->conn_state &= ~L2CAP_CONN_SEND_FBIT;
	}

	if (pi->conn_state & L2CAP_CONN_SEND_PBIT) {
		control |= L2CAP_CTRL_POLL;
		pi->conn_state &= ~L2CAP_CONN_SEND_PBIT;
	}

	skb = bt_skb_alloc(count, GFP_ATOMIC);
	if (!skb)
		return;

	lh = (struct l2cap_hdr *) skb_put(skb, L2CAP_HDR_SIZE);
	lh->len = cpu_to_le16(hlen - L2CAP_HDR_SIZE);
	lh->cid = cpu_to_le16(pi->dcid);
	put_unaligned_le16(control, skb_put(skb, 2));

	if (pi->fcs == L2CAP_FCS_CRC16) {
		u16 fcs = crc16(0, (u8 *)lh, count - 2);
		put_unaligned_le16(fcs, skb_put(skb, 2));
	}

	if (lmp_no_flush_capable(conn->hcon->hdev))
		flags = ACL_START_NO_FLUSH;
	else
		flags = ACL_START;

	hci_send_acl(pi->conn->hcon, skb, flags);
}

static inline void l2cap_send_rr_or_rnr(struct l2cap_pinfo *pi, u16 control)
{
	if (pi->conn_state & L2CAP_CONN_LOCAL_BUSY) {
		control |= L2CAP_SUPER_RCV_NOT_READY;
		pi->conn_state |= L2CAP_CONN_RNR_SENT;
	} else
		control |= L2CAP_SUPER_RCV_READY;

	control |= pi->buffer_seq << L2CAP_CTRL_REQSEQ_SHIFT;

	l2cap_send_sframe(pi, control);
}

static inline int __l2cap_no_conn_pending(struct sock *sk)
{
	return !(l2cap_pi(sk)->conf_state & L2CAP_CONF_CONNECT_PEND);
}

static void l2cap_do_start(struct sock *sk)
{
	struct l2cap_conn *conn = l2cap_pi(sk)->conn;

	if (conn->info_state & L2CAP_INFO_FEAT_MASK_REQ_SENT) {
		if (!(conn->info_state & L2CAP_INFO_FEAT_MASK_REQ_DONE))
			return;

		if (l2cap_check_security(sk) && __l2cap_no_conn_pending(sk)) {
			struct l2cap_conn_req req;
			req.scid = cpu_to_le16(l2cap_pi(sk)->scid);
			req.psm  = l2cap_pi(sk)->psm;

			l2cap_pi(sk)->ident = l2cap_get_ident(conn);
			l2cap_pi(sk)->conf_state |= L2CAP_CONF_CONNECT_PEND;

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

static inline int l2cap_mode_supported(__u8 mode, __u32 feat_mask)
{
	u32 local_feat_mask = l2cap_feat_mask;
	if (!disable_ertm)
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

void l2cap_send_disconn_req(struct l2cap_conn *conn, struct sock *sk, int err)
{
	struct l2cap_disconn_req req;

	if (!conn)
		return;

	skb_queue_purge(TX_QUEUE(sk));

	if (l2cap_pi(sk)->mode == L2CAP_MODE_ERTM) {
		del_timer(&l2cap_pi(sk)->retrans_timer);
		del_timer(&l2cap_pi(sk)->monitor_timer);
		del_timer(&l2cap_pi(sk)->ack_timer);
	}

	req.dcid = cpu_to_le16(l2cap_pi(sk)->dcid);
	req.scid = cpu_to_le16(l2cap_pi(sk)->scid);
	l2cap_send_cmd(conn, l2cap_get_ident(conn),
			L2CAP_DISCONN_REQ, sizeof(req), &req);

	sk->sk_state = BT_DISCONN;
	sk->sk_err = err;
}

/* ---- L2CAP connections ---- */
static void l2cap_conn_start(struct l2cap_conn *conn)
{
	struct l2cap_chan_list *l = &conn->chan_list;
	struct sock_del_list del, *tmp1, *tmp2;
	struct sock *sk;

	BT_DBG("conn %p", conn);

	INIT_LIST_HEAD(&del.list);

	read_lock(&l->lock);

	for (sk = l->head; sk; sk = l2cap_pi(sk)->next_c) {
		bh_lock_sock(sk);

		if (sk->sk_type != SOCK_SEQPACKET &&
				sk->sk_type != SOCK_STREAM) {
			bh_unlock_sock(sk);
			continue;
		}

		if (sk->sk_state == BT_CONNECT) {
			struct l2cap_conn_req req;

			if (!l2cap_check_security(sk) ||
					!__l2cap_no_conn_pending(sk)) {
				bh_unlock_sock(sk);
				continue;
			}

			if (!l2cap_mode_supported(l2cap_pi(sk)->mode,
					conn->feat_mask)
					&& l2cap_pi(sk)->conf_state &
					L2CAP_CONF_STATE2_DEVICE) {
				tmp1 = kzalloc(sizeof(struct sock_del_list),
						GFP_ATOMIC);
				tmp1->sk = sk;
				list_add_tail(&tmp1->list, &del.list);
				bh_unlock_sock(sk);
				continue;
			}

			req.scid = cpu_to_le16(l2cap_pi(sk)->scid);
			req.psm  = l2cap_pi(sk)->psm;

			l2cap_pi(sk)->ident = l2cap_get_ident(conn);
			l2cap_pi(sk)->conf_state |= L2CAP_CONF_CONNECT_PEND;

			l2cap_send_cmd(conn, l2cap_pi(sk)->ident,
				L2CAP_CONN_REQ, sizeof(req), &req);

		} else if (sk->sk_state == BT_CONNECT2) {
			struct l2cap_conn_rsp rsp;
			char buf[128];
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

			if (l2cap_pi(sk)->conf_state & L2CAP_CONF_REQ_SENT ||
					rsp.result != L2CAP_CR_SUCCESS) {
				bh_unlock_sock(sk);
				continue;
			}

			l2cap_pi(sk)->conf_state |= L2CAP_CONF_REQ_SENT;
			l2cap_send_cmd(conn, l2cap_get_ident(conn), L2CAP_CONF_REQ,
						l2cap_build_conf_req(sk, buf), buf);
			l2cap_pi(sk)->num_conf_req++;
		}

		bh_unlock_sock(sk);
	}

	read_unlock(&l->lock);

	list_for_each_entry_safe(tmp1, tmp2, &del.list, list) {
		bh_lock_sock(tmp1->sk);
		__l2cap_sock_close(tmp1->sk, ECONNRESET);
		bh_unlock_sock(tmp1->sk);
		list_del(&tmp1->list);
		kfree(tmp1);
	}
}

/* Find socket with cid and source bdaddr.
 * Returns closest match, locked.
 */
static struct sock *l2cap_get_sock_by_scid(int state, __le16 cid, bdaddr_t *src)
{
	struct sock *s, *sk = NULL, *sk1 = NULL;
	struct hlist_node *node;

	read_lock(&l2cap_sk_list.lock);

	sk_for_each(sk, node, &l2cap_sk_list.head) {
		if (state && sk->sk_state != state)
			continue;

		if (l2cap_pi(sk)->scid == cid) {
			/* Exact match. */
			if (!bacmp(&bt_sk(sk)->src, src))
				break;

			/* Closest match */
			if (!bacmp(&bt_sk(sk)->src, BDADDR_ANY))
				sk1 = sk;
		}
	}
	s = node ? sk : sk1;
	if (s)
		bh_lock_sock(s);
	read_unlock(&l2cap_sk_list.lock);

	return s;
}

static void l2cap_le_conn_ready(struct l2cap_conn *conn)
{
	struct l2cap_chan_list *list = &conn->chan_list;
	struct sock *parent, *uninitialized_var(sk);

	BT_DBG("");

	/* Check if we have socket listening on cid */
	parent = l2cap_get_sock_by_scid(BT_LISTEN, L2CAP_CID_LE_DATA,
							conn->src);
	if (!parent)
		return;

	/* Check for backlog size */
	if (sk_acceptq_is_full(parent)) {
		BT_DBG("backlog full %d", parent->sk_ack_backlog);
		goto clean;
	}

	sk = l2cap_sock_alloc(sock_net(parent), NULL, BTPROTO_L2CAP, GFP_ATOMIC);
	if (!sk)
		goto clean;

	write_lock_bh(&list->lock);

	hci_conn_hold(conn->hcon);

	l2cap_sock_init(sk, parent);
	bacpy(&bt_sk(sk)->src, conn->src);
	bacpy(&bt_sk(sk)->dst, conn->dst);

	__l2cap_chan_add(conn, sk, parent);

	l2cap_sock_set_timer(sk, sk->sk_sndtimeo);

	sk->sk_state = BT_CONNECTED;
	parent->sk_data_ready(parent, 0);

	write_unlock_bh(&list->lock);

clean:
	bh_unlock_sock(parent);
}

static void l2cap_conn_ready(struct l2cap_conn *conn)
{
	struct l2cap_chan_list *l = &conn->chan_list;
	struct sock *sk;

	BT_DBG("conn %p", conn);

	if (!conn->hcon->out && conn->hcon->type == LE_LINK)
		l2cap_le_conn_ready(conn);

	read_lock(&l->lock);

	for (sk = l->head; sk; sk = l2cap_pi(sk)->next_c) {
		bh_lock_sock(sk);

		if (conn->hcon->type == LE_LINK) {
			l2cap_sock_clear_timer(sk);
			sk->sk_state = BT_CONNECTED;
			sk->sk_state_change(sk);
		}

		if (sk->sk_type != SOCK_SEQPACKET &&
				sk->sk_type != SOCK_STREAM) {
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

	if (hcon->hdev->le_mtu && hcon->type == LE_LINK)
		conn->mtu = hcon->hdev->le_mtu;
	else
		conn->mtu = hcon->hdev->acl_mtu;

	conn->src = &hcon->hdev->bdaddr;
	conn->dst = &hcon->dst;

	conn->feat_mask = 0;

	spin_lock_init(&conn->lock);
	rwlock_init(&conn->chan_list.lock);

	if (hcon->type != LE_LINK)
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

/* Find socket with psm and source bdaddr.
 * Returns closest match.
 */
static struct sock *l2cap_get_sock_by_psm(int state, __le16 psm, bdaddr_t *src)
{
	struct sock *sk = NULL, *sk1 = NULL;
	struct hlist_node *node;

	read_lock(&l2cap_sk_list.lock);

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

	read_unlock(&l2cap_sk_list.lock);

	return node ? sk : sk1;
}

int l2cap_do_connect(struct sock *sk)
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

	auth_type = l2cap_get_auth_type(sk);

	if (l2cap_pi(sk)->dcid == L2CAP_CID_LE_DATA)
		hcon = hci_connect(hdev, LE_LINK, dst,
					l2cap_pi(sk)->sec_level, auth_type);
	else
		hcon = hci_connect(hdev, ACL_LINK, dst,
					l2cap_pi(sk)->sec_level, auth_type);

	if (IS_ERR(hcon)) {
		err = PTR_ERR(hcon);
		goto done;
	}

	conn = l2cap_conn_add(hcon, 0);
	if (!conn) {
		hci_conn_put(hcon);
		err = -ENOMEM;
		goto done;
	}

	/* Update source addr of the socket */
	bacpy(src, conn->src);

	l2cap_chan_add(conn, sk, NULL);

	sk->sk_state = BT_CONNECT;
	l2cap_sock_set_timer(sk, sk->sk_sndtimeo);

	if (hcon->state == BT_CONNECTED) {
		if (sk->sk_type != SOCK_SEQPACKET &&
				sk->sk_type != SOCK_STREAM) {
			l2cap_sock_clear_timer(sk);
			if (l2cap_check_security(sk))
				sk->sk_state = BT_CONNECTED;
		} else
			l2cap_do_start(sk);
	}

	err = 0;

done:
	hci_dev_unlock_bh(hdev);
	hci_dev_put(hdev);
	return err;
}

int __l2cap_wait_ack(struct sock *sk)
{
	DECLARE_WAITQUEUE(wait, current);
	int err = 0;
	int timeo = HZ/5;

	add_wait_queue(sk_sleep(sk), &wait);
	while ((l2cap_pi(sk)->unacked_frames > 0 && l2cap_pi(sk)->conn)) {
		set_current_state(TASK_INTERRUPTIBLE);

		if (!timeo)
			timeo = HZ/5;

		if (signal_pending(current)) {
			err = sock_intr_errno(timeo);
			break;
		}

		release_sock(sk);
		timeo = schedule_timeout(timeo);
		lock_sock(sk);

		err = sock_error(sk);
		if (err)
			break;
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(sk_sleep(sk), &wait);
	return err;
}

static void l2cap_monitor_timeout(unsigned long arg)
{
	struct sock *sk = (void *) arg;

	BT_DBG("sk %p", sk);

	bh_lock_sock(sk);
	if (l2cap_pi(sk)->retry_count >= l2cap_pi(sk)->remote_max_tx) {
		l2cap_send_disconn_req(l2cap_pi(sk)->conn, sk, ECONNABORTED);
		bh_unlock_sock(sk);
		return;
	}

	l2cap_pi(sk)->retry_count++;
	__mod_monitor_timer();

	l2cap_send_rr_or_rnr(l2cap_pi(sk), L2CAP_CTRL_POLL);
	bh_unlock_sock(sk);
}

static void l2cap_retrans_timeout(unsigned long arg)
{
	struct sock *sk = (void *) arg;

	BT_DBG("sk %p", sk);

	bh_lock_sock(sk);
	l2cap_pi(sk)->retry_count = 1;
	__mod_monitor_timer();

	l2cap_pi(sk)->conn_state |= L2CAP_CONN_WAIT_F;

	l2cap_send_rr_or_rnr(l2cap_pi(sk), L2CAP_CTRL_POLL);
	bh_unlock_sock(sk);
}

static void l2cap_drop_acked_frames(struct sock *sk)
{
	struct sk_buff *skb;

	while ((skb = skb_peek(TX_QUEUE(sk))) &&
			l2cap_pi(sk)->unacked_frames) {
		if (bt_cb(skb)->tx_seq == l2cap_pi(sk)->expected_ack_seq)
			break;

		skb = skb_dequeue(TX_QUEUE(sk));
		kfree_skb(skb);

		l2cap_pi(sk)->unacked_frames--;
	}

	if (!l2cap_pi(sk)->unacked_frames)
		del_timer(&l2cap_pi(sk)->retrans_timer);
}

void l2cap_do_send(struct sock *sk, struct sk_buff *skb)
{
	struct l2cap_pinfo *pi = l2cap_pi(sk);
	struct hci_conn *hcon = pi->conn->hcon;
	u16 flags;

	BT_DBG("sk %p, skb %p len %d", sk, skb, skb->len);

	if (!pi->flushable && lmp_no_flush_capable(hcon->hdev))
		flags = ACL_START_NO_FLUSH;
	else
		flags = ACL_START;

	hci_send_acl(hcon, skb, flags);
}

void l2cap_streaming_send(struct sock *sk)
{
	struct sk_buff *skb;
	struct l2cap_pinfo *pi = l2cap_pi(sk);
	u16 control, fcs;

	while ((skb = skb_dequeue(TX_QUEUE(sk)))) {
		control = get_unaligned_le16(skb->data + L2CAP_HDR_SIZE);
		control |= pi->next_tx_seq << L2CAP_CTRL_TXSEQ_SHIFT;
		put_unaligned_le16(control, skb->data + L2CAP_HDR_SIZE);

		if (pi->fcs == L2CAP_FCS_CRC16) {
			fcs = crc16(0, (u8 *)skb->data, skb->len - 2);
			put_unaligned_le16(fcs, skb->data + skb->len - 2);
		}

		l2cap_do_send(sk, skb);

		pi->next_tx_seq = (pi->next_tx_seq + 1) % 64;
	}
}

static void l2cap_retransmit_one_frame(struct sock *sk, u8 tx_seq)
{
	struct l2cap_pinfo *pi = l2cap_pi(sk);
	struct sk_buff *skb, *tx_skb;
	u16 control, fcs;

	skb = skb_peek(TX_QUEUE(sk));
	if (!skb)
		return;

	do {
		if (bt_cb(skb)->tx_seq == tx_seq)
			break;

		if (skb_queue_is_last(TX_QUEUE(sk), skb))
			return;

	} while ((skb = skb_queue_next(TX_QUEUE(sk), skb)));

	if (pi->remote_max_tx &&
			bt_cb(skb)->retries == pi->remote_max_tx) {
		l2cap_send_disconn_req(pi->conn, sk, ECONNABORTED);
		return;
	}

	tx_skb = skb_clone(skb, GFP_ATOMIC);
	bt_cb(skb)->retries++;
	control = get_unaligned_le16(tx_skb->data + L2CAP_HDR_SIZE);
	control &= L2CAP_CTRL_SAR;

	if (pi->conn_state & L2CAP_CONN_SEND_FBIT) {
		control |= L2CAP_CTRL_FINAL;
		pi->conn_state &= ~L2CAP_CONN_SEND_FBIT;
	}

	control |= (pi->buffer_seq << L2CAP_CTRL_REQSEQ_SHIFT)
			| (tx_seq << L2CAP_CTRL_TXSEQ_SHIFT);

	put_unaligned_le16(control, tx_skb->data + L2CAP_HDR_SIZE);

	if (pi->fcs == L2CAP_FCS_CRC16) {
		fcs = crc16(0, (u8 *)tx_skb->data, tx_skb->len - 2);
		put_unaligned_le16(fcs, tx_skb->data + tx_skb->len - 2);
	}

	l2cap_do_send(sk, tx_skb);
}

int l2cap_ertm_send(struct sock *sk)
{
	struct sk_buff *skb, *tx_skb;
	struct l2cap_pinfo *pi = l2cap_pi(sk);
	u16 control, fcs;
	int nsent = 0;

	if (sk->sk_state != BT_CONNECTED)
		return -ENOTCONN;

	while ((skb = sk->sk_send_head) && (!l2cap_tx_window_full(sk))) {

		if (pi->remote_max_tx &&
				bt_cb(skb)->retries == pi->remote_max_tx) {
			l2cap_send_disconn_req(pi->conn, sk, ECONNABORTED);
			break;
		}

		tx_skb = skb_clone(skb, GFP_ATOMIC);

		bt_cb(skb)->retries++;

		control = get_unaligned_le16(tx_skb->data + L2CAP_HDR_SIZE);
		control &= L2CAP_CTRL_SAR;

		if (pi->conn_state & L2CAP_CONN_SEND_FBIT) {
			control |= L2CAP_CTRL_FINAL;
			pi->conn_state &= ~L2CAP_CONN_SEND_FBIT;
		}
		control |= (pi->buffer_seq << L2CAP_CTRL_REQSEQ_SHIFT)
				| (pi->next_tx_seq << L2CAP_CTRL_TXSEQ_SHIFT);
		put_unaligned_le16(control, tx_skb->data + L2CAP_HDR_SIZE);


		if (pi->fcs == L2CAP_FCS_CRC16) {
			fcs = crc16(0, (u8 *)skb->data, tx_skb->len - 2);
			put_unaligned_le16(fcs, skb->data + tx_skb->len - 2);
		}

		l2cap_do_send(sk, tx_skb);

		__mod_retrans_timer();

		bt_cb(skb)->tx_seq = pi->next_tx_seq;
		pi->next_tx_seq = (pi->next_tx_seq + 1) % 64;

		if (bt_cb(skb)->retries == 1)
			pi->unacked_frames++;

		pi->frames_sent++;

		if (skb_queue_is_last(TX_QUEUE(sk), skb))
			sk->sk_send_head = NULL;
		else
			sk->sk_send_head = skb_queue_next(TX_QUEUE(sk), skb);

		nsent++;
	}

	return nsent;
}

static int l2cap_retransmit_frames(struct sock *sk)
{
	struct l2cap_pinfo *pi = l2cap_pi(sk);
	int ret;

	if (!skb_queue_empty(TX_QUEUE(sk)))
		sk->sk_send_head = TX_QUEUE(sk)->next;

	pi->next_tx_seq = pi->expected_ack_seq;
	ret = l2cap_ertm_send(sk);
	return ret;
}

static void l2cap_send_ack(struct l2cap_pinfo *pi)
{
	struct sock *sk = (struct sock *)pi;
	u16 control = 0;

	control |= pi->buffer_seq << L2CAP_CTRL_REQSEQ_SHIFT;

	if (pi->conn_state & L2CAP_CONN_LOCAL_BUSY) {
		control |= L2CAP_SUPER_RCV_NOT_READY;
		pi->conn_state |= L2CAP_CONN_RNR_SENT;
		l2cap_send_sframe(pi, control);
		return;
	}

	if (l2cap_ertm_send(sk) > 0)
		return;

	control |= L2CAP_SUPER_RCV_READY;
	l2cap_send_sframe(pi, control);
}

static void l2cap_send_srejtail(struct sock *sk)
{
	struct srej_list *tail;
	u16 control;

	control = L2CAP_SUPER_SELECT_REJECT;
	control |= L2CAP_CTRL_FINAL;

	tail = list_entry(SREJ_LIST(sk)->prev, struct srej_list, list);
	control |= tail->tx_seq << L2CAP_CTRL_REQSEQ_SHIFT;

	l2cap_send_sframe(l2cap_pi(sk), control);
}

static inline int l2cap_skbuff_fromiovec(struct sock *sk, struct msghdr *msg, int len, int count, struct sk_buff *skb)
{
	struct l2cap_conn *conn = l2cap_pi(sk)->conn;
	struct sk_buff **frag;
	int err, sent = 0;

	if (memcpy_fromiovec(skb_put(skb, count), msg->msg_iov, count))
		return -EFAULT;

	sent += count;
	len  -= count;

	/* Continuation fragments (no L2CAP header) */
	frag = &skb_shinfo(skb)->frag_list;
	while (len) {
		count = min_t(unsigned int, conn->mtu, len);

		*frag = bt_skb_send_alloc(sk, count, msg->msg_flags & MSG_DONTWAIT, &err);
		if (!*frag)
			return err;
		if (memcpy_fromiovec(skb_put(*frag, count), msg->msg_iov, count))
			return -EFAULT;

		sent += count;
		len  -= count;

		frag = &(*frag)->next;
	}

	return sent;
}

struct sk_buff *l2cap_create_connless_pdu(struct sock *sk, struct msghdr *msg, size_t len)
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
		return ERR_PTR(err);

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

struct sk_buff *l2cap_create_basic_pdu(struct sock *sk, struct msghdr *msg, size_t len)
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
		return ERR_PTR(err);

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

struct sk_buff *l2cap_create_iframe_pdu(struct sock *sk, struct msghdr *msg, size_t len, u16 control, u16 sdulen)
{
	struct l2cap_conn *conn = l2cap_pi(sk)->conn;
	struct sk_buff *skb;
	int err, count, hlen = L2CAP_HDR_SIZE + 2;
	struct l2cap_hdr *lh;

	BT_DBG("sk %p len %d", sk, (int)len);

	if (!conn)
		return ERR_PTR(-ENOTCONN);

	if (sdulen)
		hlen += 2;

	if (l2cap_pi(sk)->fcs == L2CAP_FCS_CRC16)
		hlen += 2;

	count = min_t(unsigned int, (conn->mtu - hlen), len);
	skb = bt_skb_send_alloc(sk, count + hlen,
			msg->msg_flags & MSG_DONTWAIT, &err);
	if (!skb)
		return ERR_PTR(err);

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

int l2cap_sar_segment_sdu(struct sock *sk, struct msghdr *msg, size_t len)
{
	struct l2cap_pinfo *pi = l2cap_pi(sk);
	struct sk_buff *skb;
	struct sk_buff_head sar_queue;
	u16 control;
	size_t size = 0;

	skb_queue_head_init(&sar_queue);
	control = L2CAP_SDU_START;
	skb = l2cap_create_iframe_pdu(sk, msg, pi->remote_mps, control, len);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	__skb_queue_tail(&sar_queue, skb);
	len -= pi->remote_mps;
	size += pi->remote_mps;

	while (len > 0) {
		size_t buflen;

		if (len > pi->remote_mps) {
			control = L2CAP_SDU_CONTINUE;
			buflen = pi->remote_mps;
		} else {
			control = L2CAP_SDU_END;
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
	}
	skb_queue_splice_tail(&sar_queue, TX_QUEUE(sk));
	if (sk->sk_send_head == NULL)
		sk->sk_send_head = sar_queue.next;

	return size;
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

	if (conn->hcon->type == LE_LINK)
		lh->cid = cpu_to_le16(L2CAP_CID_LE_SIGNALING);
	else
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
		*val = get_unaligned_le16(opt->val);
		break;

	case 4:
		*val = get_unaligned_le32(opt->val);
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
		put_unaligned_le16(val, opt->val);
		break;

	case 4:
		put_unaligned_le32(val, opt->val);
		break;

	default:
		memcpy(opt->val, (void *) val, len);
		break;
	}

	*ptr += L2CAP_CONF_OPT_SIZE + len;
}

static void l2cap_ack_timeout(unsigned long arg)
{
	struct sock *sk = (void *) arg;

	bh_lock_sock(sk);
	l2cap_send_ack(l2cap_pi(sk));
	bh_unlock_sock(sk);
}

static inline void l2cap_ertm_init(struct sock *sk)
{
	l2cap_pi(sk)->expected_ack_seq = 0;
	l2cap_pi(sk)->unacked_frames = 0;
	l2cap_pi(sk)->buffer_seq = 0;
	l2cap_pi(sk)->num_acked = 0;
	l2cap_pi(sk)->frames_sent = 0;

	setup_timer(&l2cap_pi(sk)->retrans_timer,
			l2cap_retrans_timeout, (unsigned long) sk);
	setup_timer(&l2cap_pi(sk)->monitor_timer,
			l2cap_monitor_timeout, (unsigned long) sk);
	setup_timer(&l2cap_pi(sk)->ack_timer,
			l2cap_ack_timeout, (unsigned long) sk);

	__skb_queue_head_init(SREJ_QUEUE(sk));
	__skb_queue_head_init(BUSY_QUEUE(sk));

	INIT_WORK(&l2cap_pi(sk)->busy_work, l2cap_busy_work);

	sk->sk_backlog_rcv = l2cap_ertm_data_rcv;
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

int l2cap_build_conf_req(struct sock *sk, void *data)
{
	struct l2cap_pinfo *pi = l2cap_pi(sk);
	struct l2cap_conf_req *req = data;
	struct l2cap_conf_rfc rfc = { .mode = pi->mode };
	void *ptr = req->data;

	BT_DBG("sk %p", sk);

	if (pi->num_conf_req || pi->num_conf_rsp)
		goto done;

	switch (pi->mode) {
	case L2CAP_MODE_STREAMING:
	case L2CAP_MODE_ERTM:
		if (pi->conf_state & L2CAP_CONF_STATE2_DEVICE)
			break;

		/* fall through */
	default:
		pi->mode = l2cap_select_mode(rfc.mode, pi->conn->feat_mask);
		break;
	}

done:
	if (pi->imtu != L2CAP_DEFAULT_MTU)
		l2cap_add_conf_opt(&ptr, L2CAP_CONF_MTU, 2, pi->imtu);

	switch (pi->mode) {
	case L2CAP_MODE_BASIC:
		if (!(pi->conn->feat_mask & L2CAP_FEAT_ERTM) &&
				!(pi->conn->feat_mask & L2CAP_FEAT_STREAMING))
			break;

		rfc.mode            = L2CAP_MODE_BASIC;
		rfc.txwin_size      = 0;
		rfc.max_transmit    = 0;
		rfc.retrans_timeout = 0;
		rfc.monitor_timeout = 0;
		rfc.max_pdu_size    = 0;

		l2cap_add_conf_opt(&ptr, L2CAP_CONF_RFC, sizeof(rfc),
							(unsigned long) &rfc);
		break;

	case L2CAP_MODE_ERTM:
		rfc.mode            = L2CAP_MODE_ERTM;
		rfc.txwin_size      = pi->tx_win;
		rfc.max_transmit    = pi->max_tx;
		rfc.retrans_timeout = 0;
		rfc.monitor_timeout = 0;
		rfc.max_pdu_size    = cpu_to_le16(L2CAP_DEFAULT_MAX_PDU_SIZE);
		if (L2CAP_DEFAULT_MAX_PDU_SIZE > pi->conn->mtu - 10)
			rfc.max_pdu_size = cpu_to_le16(pi->conn->mtu - 10);

		l2cap_add_conf_opt(&ptr, L2CAP_CONF_RFC, sizeof(rfc),
							(unsigned long) &rfc);

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
		if (L2CAP_DEFAULT_MAX_PDU_SIZE > pi->conn->mtu - 10)
			rfc.max_pdu_size = cpu_to_le16(pi->conn->mtu - 10);

		l2cap_add_conf_opt(&ptr, L2CAP_CONF_RFC, sizeof(rfc),
							(unsigned long) &rfc);

		if (!(pi->conn->feat_mask & L2CAP_FEAT_FCS))
			break;

		if (pi->fcs == L2CAP_FCS_NONE ||
				pi->conf_state & L2CAP_CONF_NO_FCS_RECV) {
			pi->fcs = L2CAP_FCS_NONE;
			l2cap_add_conf_opt(&ptr, L2CAP_CONF_FCS, 1, pi->fcs);
		}
		break;
	}

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

	if (pi->num_conf_rsp || pi->num_conf_req > 1)
		goto done;

	switch (pi->mode) {
	case L2CAP_MODE_STREAMING:
	case L2CAP_MODE_ERTM:
		if (!(pi->conf_state & L2CAP_CONF_STATE2_DEVICE)) {
			pi->mode = l2cap_select_mode(rfc.mode,
					pi->conn->feat_mask);
			break;
		}

		if (pi->mode != rfc.mode)
			return -ECONNREFUSED;

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

			if (le16_to_cpu(rfc.max_pdu_size) > pi->conn->mtu - 10)
				rfc.max_pdu_size = cpu_to_le16(pi->conn->mtu - 10);

			pi->remote_mps = le16_to_cpu(rfc.max_pdu_size);

			rfc.retrans_timeout =
				le16_to_cpu(L2CAP_DEFAULT_RETRANS_TO);
			rfc.monitor_timeout =
				le16_to_cpu(L2CAP_DEFAULT_MONITOR_TO);

			pi->conf_state |= L2CAP_CONF_MODE_DONE;

			l2cap_add_conf_opt(&ptr, L2CAP_CONF_RFC,
					sizeof(rfc), (unsigned long) &rfc);

			break;

		case L2CAP_MODE_STREAMING:
			if (le16_to_cpu(rfc.max_pdu_size) > pi->conn->mtu - 10)
				rfc.max_pdu_size = cpu_to_le16(pi->conn->mtu - 10);

			pi->remote_mps = le16_to_cpu(rfc.max_pdu_size);

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
				pi->imtu = L2CAP_DEFAULT_MIN_MTU;
			} else
				pi->imtu = val;
			l2cap_add_conf_opt(&ptr, L2CAP_CONF_MTU, 2, pi->imtu);
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

			pi->fcs = 0;

			l2cap_add_conf_opt(&ptr, L2CAP_CONF_RFC,
					sizeof(rfc), (unsigned long) &rfc);
			break;
		}
	}

	if (pi->mode == L2CAP_MODE_BASIC && pi->mode != rfc.mode)
		return -ECONNREFUSED;

	pi->mode = rfc.mode;

	if (*result == L2CAP_CONF_SUCCESS) {
		switch (rfc.mode) {
		case L2CAP_MODE_ERTM:
			pi->retrans_timeout = le16_to_cpu(rfc.retrans_timeout);
			pi->monitor_timeout = le16_to_cpu(rfc.monitor_timeout);
			pi->mps    = le16_to_cpu(rfc.max_pdu_size);
			break;
		case L2CAP_MODE_STREAMING:
			pi->mps    = le16_to_cpu(rfc.max_pdu_size);
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

static void l2cap_conf_rfc_get(struct sock *sk, void *rsp, int len)
{
	struct l2cap_pinfo *pi = l2cap_pi(sk);
	int type, olen;
	unsigned long val;
	struct l2cap_conf_rfc rfc;

	BT_DBG("sk %p, rsp %p, len %d", sk, rsp, len);

	if ((pi->mode != L2CAP_MODE_ERTM) && (pi->mode != L2CAP_MODE_STREAMING))
		return;

	while (len >= L2CAP_CONF_OPT_SIZE) {
		len -= l2cap_get_conf_opt(&rsp, &type, &olen, &val);

		switch (type) {
		case L2CAP_CONF_RFC:
			if (olen == sizeof(rfc))
				memcpy(&rfc, (void *)val, olen);
			goto done;
		}
	}

done:
	switch (rfc.mode) {
	case L2CAP_MODE_ERTM:
		pi->retrans_timeout = le16_to_cpu(rfc.retrans_timeout);
		pi->monitor_timeout = le16_to_cpu(rfc.monitor_timeout);
		pi->mps    = le16_to_cpu(rfc.max_pdu_size);
		break;
	case L2CAP_MODE_STREAMING:
		pi->mps    = le16_to_cpu(rfc.max_pdu_size);
	}
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
	struct sock *parent, *sk = NULL;
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

	bh_lock_sock(parent);

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

	if (sk && !(l2cap_pi(sk)->conf_state & L2CAP_CONF_REQ_SENT) &&
				result == L2CAP_CR_SUCCESS) {
		u8 buf[128];
		l2cap_pi(sk)->conf_state |= L2CAP_CONF_REQ_SENT;
		l2cap_send_cmd(conn, l2cap_get_ident(conn), L2CAP_CONF_REQ,
					l2cap_build_conf_req(sk, buf), buf);
		l2cap_pi(sk)->num_conf_req++;
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
			return -EFAULT;
	} else {
		sk = l2cap_get_chan_by_ident(&conn->chan_list, cmd->ident);
		if (!sk)
			return -EFAULT;
	}

	switch (result) {
	case L2CAP_CR_SUCCESS:
		sk->sk_state = BT_CONFIG;
		l2cap_pi(sk)->ident = 0;
		l2cap_pi(sk)->dcid = dcid;
		l2cap_pi(sk)->conf_state &= ~L2CAP_CONF_CONNECT_PEND;

		if (l2cap_pi(sk)->conf_state & L2CAP_CONF_REQ_SENT)
			break;

		l2cap_pi(sk)->conf_state |= L2CAP_CONF_REQ_SENT;

		l2cap_send_cmd(conn, l2cap_get_ident(conn), L2CAP_CONF_REQ,
					l2cap_build_conf_req(sk, req), req);
		l2cap_pi(sk)->num_conf_req++;
		break;

	case L2CAP_CR_PEND:
		l2cap_pi(sk)->conf_state |= L2CAP_CONF_CONNECT_PEND;
		break;

	default:
		/* don't delete l2cap channel if sk is owned by user */
		if (sock_owned_by_user(sk)) {
			sk->sk_state = BT_DISCONN;
			l2cap_sock_clear_timer(sk);
			l2cap_sock_set_timer(sk, HZ / 5);
			break;
		}

		l2cap_chan_del(sk, ECONNREFUSED);
		break;
	}

	bh_unlock_sock(sk);
	return 0;
}

static inline void set_default_fcs(struct l2cap_pinfo *pi)
{
	/* FCS is enabled only in ERTM or streaming mode, if one or both
	 * sides request it.
	 */
	if (pi->mode != L2CAP_MODE_ERTM && pi->mode != L2CAP_MODE_STREAMING)
		pi->fcs = L2CAP_FCS_NONE;
	else if (!(pi->conf_state & L2CAP_CONF_NO_FCS_RECV))
		pi->fcs = L2CAP_FCS_CRC16;
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

	if (sk->sk_state != BT_CONFIG) {
		struct l2cap_cmd_rej rej;

		rej.reason = cpu_to_le16(0x0002);
		l2cap_send_cmd(conn, cmd->ident, L2CAP_COMMAND_REJ,
				sizeof(rej), &rej);
		goto unlock;
	}

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
		l2cap_send_disconn_req(conn, sk, ECONNRESET);
		goto unlock;
	}

	l2cap_send_cmd(conn, cmd->ident, L2CAP_CONF_RSP, len, rsp);
	l2cap_pi(sk)->num_conf_rsp++;

	/* Reset config buffer. */
	l2cap_pi(sk)->conf_len = 0;

	if (!(l2cap_pi(sk)->conf_state & L2CAP_CONF_OUTPUT_DONE))
		goto unlock;

	if (l2cap_pi(sk)->conf_state & L2CAP_CONF_INPUT_DONE) {
		set_default_fcs(l2cap_pi(sk));

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
		l2cap_pi(sk)->conf_state |= L2CAP_CONF_REQ_SENT;
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
	int len = cmd->len - sizeof(*rsp);

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
		l2cap_conf_rfc_get(sk, rsp->data, len);
		break;

	case L2CAP_CONF_UNACCEPT:
		if (l2cap_pi(sk)->num_conf_rsp <= L2CAP_CONF_MAX_CONF_RSP) {
			char req[64];

			if (len > sizeof(req) - sizeof(struct l2cap_conf_req)) {
				l2cap_send_disconn_req(conn, sk, ECONNRESET);
				goto done;
			}

			/* throw out any old stored conf requests */
			result = L2CAP_CONF_SUCCESS;
			len = l2cap_parse_conf_rsp(sk, rsp->data,
							len, req, &result);
			if (len < 0) {
				l2cap_send_disconn_req(conn, sk, ECONNRESET);
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
		sk->sk_err = ECONNRESET;
		l2cap_sock_set_timer(sk, HZ * 5);
		l2cap_send_disconn_req(conn, sk, ECONNRESET);
		goto done;
	}

	if (flags & 0x01)
		goto done;

	l2cap_pi(sk)->conf_state |= L2CAP_CONF_INPUT_DONE;

	if (l2cap_pi(sk)->conf_state & L2CAP_CONF_OUTPUT_DONE) {
		set_default_fcs(l2cap_pi(sk));

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

	/* don't delete l2cap channel if sk is owned by user */
	if (sock_owned_by_user(sk)) {
		sk->sk_state = BT_DISCONN;
		l2cap_sock_clear_timer(sk);
		l2cap_sock_set_timer(sk, HZ / 5);
		bh_unlock_sock(sk);
		return 0;
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

	/* don't delete l2cap channel if sk is owned by user */
	if (sock_owned_by_user(sk)) {
		sk->sk_state = BT_DISCONN;
		l2cap_sock_clear_timer(sk);
		l2cap_sock_set_timer(sk, HZ / 5);
		bh_unlock_sock(sk);
		return 0;
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
		if (!disable_ertm)
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

	if (result != L2CAP_IR_SUCCESS) {
		conn->info_state |= L2CAP_INFO_FEAT_MASK_REQ_DONE;
		conn->info_ident = 0;

		l2cap_conn_start(conn);

		return 0;
	}

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

static inline int l2cap_check_conn_param(u16 min, u16 max, u16 latency,
							u16 to_multiplier)
{
	u16 max_latency;

	if (min > max || min < 6 || max > 3200)
		return -EINVAL;

	if (to_multiplier < 10 || to_multiplier > 3200)
		return -EINVAL;

	if (max >= to_multiplier * 8)
		return -EINVAL;

	max_latency = (to_multiplier * 8 / max) - 1;
	if (latency > 499 || latency > max_latency)
		return -EINVAL;

	return 0;
}

static inline int l2cap_conn_param_update_req(struct l2cap_conn *conn,
					struct l2cap_cmd_hdr *cmd, u8 *data)
{
	struct hci_conn *hcon = conn->hcon;
	struct l2cap_conn_param_update_req *req;
	struct l2cap_conn_param_update_rsp rsp;
	u16 min, max, latency, to_multiplier, cmd_len;
	int err;

	if (!(hcon->link_mode & HCI_LM_MASTER))
		return -EINVAL;

	cmd_len = __le16_to_cpu(cmd->len);
	if (cmd_len != sizeof(struct l2cap_conn_param_update_req))
		return -EPROTO;

	req = (struct l2cap_conn_param_update_req *) data;
	min		= __le16_to_cpu(req->min);
	max		= __le16_to_cpu(req->max);
	latency		= __le16_to_cpu(req->latency);
	to_multiplier	= __le16_to_cpu(req->to_multiplier);

	BT_DBG("min 0x%4.4x max 0x%4.4x latency: 0x%4.4x Timeout: 0x%4.4x",
						min, max, latency, to_multiplier);

	memset(&rsp, 0, sizeof(rsp));

	err = l2cap_check_conn_param(min, max, latency, to_multiplier);
	if (err)
		rsp.result = cpu_to_le16(L2CAP_CONN_PARAM_REJECTED);
	else
		rsp.result = cpu_to_le16(L2CAP_CONN_PARAM_ACCEPTED);

	l2cap_send_cmd(conn, cmd->ident, L2CAP_CONN_PARAM_UPDATE_RSP,
							sizeof(rsp), &rsp);

	if (!err)
		hci_le_conn_update(hcon, min, max, latency, to_multiplier);

	return 0;
}

static inline int l2cap_bredr_sig_cmd(struct l2cap_conn *conn,
			struct l2cap_cmd_hdr *cmd, u16 cmd_len, u8 *data)
{
	int err = 0;

	switch (cmd->code) {
	case L2CAP_COMMAND_REJ:
		l2cap_command_rej(conn, cmd, data);
		break;

	case L2CAP_CONN_REQ:
		err = l2cap_connect_req(conn, cmd, data);
		break;

	case L2CAP_CONN_RSP:
		err = l2cap_connect_rsp(conn, cmd, data);
		break;

	case L2CAP_CONF_REQ:
		err = l2cap_config_req(conn, cmd, cmd_len, data);
		break;

	case L2CAP_CONF_RSP:
		err = l2cap_config_rsp(conn, cmd, data);
		break;

	case L2CAP_DISCONN_REQ:
		err = l2cap_disconnect_req(conn, cmd, data);
		break;

	case L2CAP_DISCONN_RSP:
		err = l2cap_disconnect_rsp(conn, cmd, data);
		break;

	case L2CAP_ECHO_REQ:
		l2cap_send_cmd(conn, cmd->ident, L2CAP_ECHO_RSP, cmd_len, data);
		break;

	case L2CAP_ECHO_RSP:
		break;

	case L2CAP_INFO_REQ:
		err = l2cap_information_req(conn, cmd, data);
		break;

	case L2CAP_INFO_RSP:
		err = l2cap_information_rsp(conn, cmd, data);
		break;

	default:
		BT_ERR("Unknown BR/EDR signaling command 0x%2.2x", cmd->code);
		err = -EINVAL;
		break;
	}

	return err;
}

static inline int l2cap_le_sig_cmd(struct l2cap_conn *conn,
					struct l2cap_cmd_hdr *cmd, u8 *data)
{
	switch (cmd->code) {
	case L2CAP_COMMAND_REJ:
		return 0;

	case L2CAP_CONN_PARAM_UPDATE_REQ:
		return l2cap_conn_param_update_req(conn, cmd, data);

	case L2CAP_CONN_PARAM_UPDATE_RSP:
		return 0;

	default:
		BT_ERR("Unknown LE signaling command 0x%2.2x", cmd->code);
		return -EINVAL;
	}
}

static inline void l2cap_sig_channel(struct l2cap_conn *conn,
							struct sk_buff *skb)
{
	u8 *data = skb->data;
	int len = skb->len;
	struct l2cap_cmd_hdr cmd;
	int err;

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

		if (conn->hcon->type == LE_LINK)
			err = l2cap_le_sig_cmd(conn, &cmd, data);
		else
			err = l2cap_bredr_sig_cmd(conn, &cmd, cmd_len, data);

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
			return -EBADMSG;
	}
	return 0;
}

static inline void l2cap_send_i_or_rr_or_rnr(struct sock *sk)
{
	struct l2cap_pinfo *pi = l2cap_pi(sk);
	u16 control = 0;

	pi->frames_sent = 0;

	control |= pi->buffer_seq << L2CAP_CTRL_REQSEQ_SHIFT;

	if (pi->conn_state & L2CAP_CONN_LOCAL_BUSY) {
		control |= L2CAP_SUPER_RCV_NOT_READY;
		l2cap_send_sframe(pi, control);
		pi->conn_state |= L2CAP_CONN_RNR_SENT;
	}

	if (pi->conn_state & L2CAP_CONN_REMOTE_BUSY)
		l2cap_retransmit_frames(sk);

	l2cap_ertm_send(sk);

	if (!(pi->conn_state & L2CAP_CONN_LOCAL_BUSY) &&
			pi->frames_sent == 0) {
		control |= L2CAP_SUPER_RCV_READY;
		l2cap_send_sframe(pi, control);
	}
}

static int l2cap_add_to_srej_queue(struct sock *sk, struct sk_buff *skb, u8 tx_seq, u8 sar)
{
	struct sk_buff *next_skb;
	struct l2cap_pinfo *pi = l2cap_pi(sk);
	int tx_seq_offset, next_tx_seq_offset;

	bt_cb(skb)->tx_seq = tx_seq;
	bt_cb(skb)->sar = sar;

	next_skb = skb_peek(SREJ_QUEUE(sk));
	if (!next_skb) {
		__skb_queue_tail(SREJ_QUEUE(sk), skb);
		return 0;
	}

	tx_seq_offset = (tx_seq - pi->buffer_seq) % 64;
	if (tx_seq_offset < 0)
		tx_seq_offset += 64;

	do {
		if (bt_cb(next_skb)->tx_seq == tx_seq)
			return -EINVAL;

		next_tx_seq_offset = (bt_cb(next_skb)->tx_seq -
						pi->buffer_seq) % 64;
		if (next_tx_seq_offset < 0)
			next_tx_seq_offset += 64;

		if (next_tx_seq_offset > tx_seq_offset) {
			__skb_queue_before(SREJ_QUEUE(sk), next_skb, skb);
			return 0;
		}

		if (skb_queue_is_last(SREJ_QUEUE(sk), next_skb))
			break;

	} while ((next_skb = skb_queue_next(SREJ_QUEUE(sk), next_skb)));

	__skb_queue_tail(SREJ_QUEUE(sk), skb);

	return 0;
}

static int l2cap_ertm_reassembly_sdu(struct sock *sk, struct sk_buff *skb, u16 control)
{
	struct l2cap_pinfo *pi = l2cap_pi(sk);
	struct sk_buff *_skb;
	int err;

	switch (control & L2CAP_CTRL_SAR) {
	case L2CAP_SDU_UNSEGMENTED:
		if (pi->conn_state & L2CAP_CONN_SAR_SDU)
			goto drop;

		err = sock_queue_rcv_skb(sk, skb);
		if (!err)
			return err;

		break;

	case L2CAP_SDU_START:
		if (pi->conn_state & L2CAP_CONN_SAR_SDU)
			goto drop;

		pi->sdu_len = get_unaligned_le16(skb->data);

		if (pi->sdu_len > pi->imtu)
			goto disconnect;

		pi->sdu = bt_skb_alloc(pi->sdu_len, GFP_ATOMIC);
		if (!pi->sdu)
			return -ENOMEM;

		/* pull sdu_len bytes only after alloc, because of Local Busy
		 * condition we have to be sure that this will be executed
		 * only once, i.e., when alloc does not fail */
		skb_pull(skb, 2);

		memcpy(skb_put(pi->sdu, skb->len), skb->data, skb->len);

		pi->conn_state |= L2CAP_CONN_SAR_SDU;
		pi->partial_sdu_len = skb->len;
		break;

	case L2CAP_SDU_CONTINUE:
		if (!(pi->conn_state & L2CAP_CONN_SAR_SDU))
			goto disconnect;

		if (!pi->sdu)
			goto disconnect;

		pi->partial_sdu_len += skb->len;
		if (pi->partial_sdu_len > pi->sdu_len)
			goto drop;

		memcpy(skb_put(pi->sdu, skb->len), skb->data, skb->len);

		break;

	case L2CAP_SDU_END:
		if (!(pi->conn_state & L2CAP_CONN_SAR_SDU))
			goto disconnect;

		if (!pi->sdu)
			goto disconnect;

		if (!(pi->conn_state & L2CAP_CONN_SAR_RETRY)) {
			pi->partial_sdu_len += skb->len;

			if (pi->partial_sdu_len > pi->imtu)
				goto drop;

			if (pi->partial_sdu_len != pi->sdu_len)
				goto drop;

			memcpy(skb_put(pi->sdu, skb->len), skb->data, skb->len);
		}

		_skb = skb_clone(pi->sdu, GFP_ATOMIC);
		if (!_skb) {
			pi->conn_state |= L2CAP_CONN_SAR_RETRY;
			return -ENOMEM;
		}

		err = sock_queue_rcv_skb(sk, _skb);
		if (err < 0) {
			kfree_skb(_skb);
			pi->conn_state |= L2CAP_CONN_SAR_RETRY;
			return err;
		}

		pi->conn_state &= ~L2CAP_CONN_SAR_RETRY;
		pi->conn_state &= ~L2CAP_CONN_SAR_SDU;

		kfree_skb(pi->sdu);
		break;
	}

	kfree_skb(skb);
	return 0;

drop:
	kfree_skb(pi->sdu);
	pi->sdu = NULL;

disconnect:
	l2cap_send_disconn_req(pi->conn, sk, ECONNRESET);
	kfree_skb(skb);
	return 0;
}

static int l2cap_try_push_rx_skb(struct sock *sk)
{
	struct l2cap_pinfo *pi = l2cap_pi(sk);
	struct sk_buff *skb;
	u16 control;
	int err;

	while ((skb = skb_dequeue(BUSY_QUEUE(sk)))) {
		control = bt_cb(skb)->sar << L2CAP_CTRL_SAR_SHIFT;
		err = l2cap_ertm_reassembly_sdu(sk, skb, control);
		if (err < 0) {
			skb_queue_head(BUSY_QUEUE(sk), skb);
			return -EBUSY;
		}

		pi->buffer_seq = (pi->buffer_seq + 1) % 64;
	}

	if (!(pi->conn_state & L2CAP_CONN_RNR_SENT))
		goto done;

	control = pi->buffer_seq << L2CAP_CTRL_REQSEQ_SHIFT;
	control |= L2CAP_SUPER_RCV_READY | L2CAP_CTRL_POLL;
	l2cap_send_sframe(pi, control);
	l2cap_pi(sk)->retry_count = 1;

	del_timer(&pi->retrans_timer);
	__mod_monitor_timer();

	l2cap_pi(sk)->conn_state |= L2CAP_CONN_WAIT_F;

done:
	pi->conn_state &= ~L2CAP_CONN_LOCAL_BUSY;
	pi->conn_state &= ~L2CAP_CONN_RNR_SENT;

	BT_DBG("sk %p, Exit local busy", sk);

	return 0;
}

static void l2cap_busy_work(struct work_struct *work)
{
	DECLARE_WAITQUEUE(wait, current);
	struct l2cap_pinfo *pi =
		container_of(work, struct l2cap_pinfo, busy_work);
	struct sock *sk = (struct sock *)pi;
	int n_tries = 0, timeo = HZ/5, err;
	struct sk_buff *skb;

	lock_sock(sk);

	add_wait_queue(sk_sleep(sk), &wait);
	while ((skb = skb_peek(BUSY_QUEUE(sk)))) {
		set_current_state(TASK_INTERRUPTIBLE);

		if (n_tries++ > L2CAP_LOCAL_BUSY_TRIES) {
			err = -EBUSY;
			l2cap_send_disconn_req(pi->conn, sk, EBUSY);
			break;
		}

		if (!timeo)
			timeo = HZ/5;

		if (signal_pending(current)) {
			err = sock_intr_errno(timeo);
			break;
		}

		release_sock(sk);
		timeo = schedule_timeout(timeo);
		lock_sock(sk);

		err = sock_error(sk);
		if (err)
			break;

		if (l2cap_try_push_rx_skb(sk) == 0)
			break;
	}

	set_current_state(TASK_RUNNING);
	remove_wait_queue(sk_sleep(sk), &wait);

	release_sock(sk);
}

static int l2cap_push_rx_skb(struct sock *sk, struct sk_buff *skb, u16 control)
{
	struct l2cap_pinfo *pi = l2cap_pi(sk);
	int sctrl, err;

	if (pi->conn_state & L2CAP_CONN_LOCAL_BUSY) {
		bt_cb(skb)->sar = control >> L2CAP_CTRL_SAR_SHIFT;
		__skb_queue_tail(BUSY_QUEUE(sk), skb);
		return l2cap_try_push_rx_skb(sk);


	}

	err = l2cap_ertm_reassembly_sdu(sk, skb, control);
	if (err >= 0) {
		pi->buffer_seq = (pi->buffer_seq + 1) % 64;
		return err;
	}

	/* Busy Condition */
	BT_DBG("sk %p, Enter local busy", sk);

	pi->conn_state |= L2CAP_CONN_LOCAL_BUSY;
	bt_cb(skb)->sar = control >> L2CAP_CTRL_SAR_SHIFT;
	__skb_queue_tail(BUSY_QUEUE(sk), skb);

	sctrl = pi->buffer_seq << L2CAP_CTRL_REQSEQ_SHIFT;
	sctrl |= L2CAP_SUPER_RCV_NOT_READY;
	l2cap_send_sframe(pi, sctrl);

	pi->conn_state |= L2CAP_CONN_RNR_SENT;

	del_timer(&pi->ack_timer);

	queue_work(_busy_wq, &pi->busy_work);

	return err;
}

static int l2cap_streaming_reassembly_sdu(struct sock *sk, struct sk_buff *skb, u16 control)
{
	struct l2cap_pinfo *pi = l2cap_pi(sk);
	struct sk_buff *_skb;
	int err = -EINVAL;

	/*
	 * TODO: We have to notify the userland if some data is lost with the
	 * Streaming Mode.
	 */

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

		if (pi->sdu_len > pi->imtu) {
			err = -EMSGSIZE;
			break;
		}

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

		if (pi->partial_sdu_len > pi->imtu)
			goto drop;

		if (pi->partial_sdu_len == pi->sdu_len) {
			_skb = skb_clone(pi->sdu, GFP_ATOMIC);
			err = sock_queue_rcv_skb(sk, _skb);
			if (err < 0)
				kfree_skb(_skb);
		}
		err = 0;

drop:
		kfree_skb(pi->sdu);
		break;
	}

	kfree_skb(skb);
	return err;
}

static void l2cap_check_srej_gap(struct sock *sk, u8 tx_seq)
{
	struct sk_buff *skb;
	u16 control;

	while ((skb = skb_peek(SREJ_QUEUE(sk)))) {
		if (bt_cb(skb)->tx_seq != tx_seq)
			break;

		skb = skb_dequeue(SREJ_QUEUE(sk));
		control = bt_cb(skb)->sar << L2CAP_CTRL_SAR_SHIFT;
		l2cap_ertm_reassembly_sdu(sk, skb, control);
		l2cap_pi(sk)->buffer_seq_srej =
			(l2cap_pi(sk)->buffer_seq_srej + 1) % 64;
		tx_seq = (tx_seq + 1) % 64;
	}
}

static void l2cap_resend_srejframe(struct sock *sk, u8 tx_seq)
{
	struct l2cap_pinfo *pi = l2cap_pi(sk);
	struct srej_list *l, *tmp;
	u16 control;

	list_for_each_entry_safe(l, tmp, SREJ_LIST(sk), list) {
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
		l2cap_send_sframe(pi, control);

		new = kzalloc(sizeof(struct srej_list), GFP_ATOMIC);
		new->tx_seq = pi->expected_tx_seq;
		pi->expected_tx_seq = (pi->expected_tx_seq + 1) % 64;
		list_add_tail(&new->list, SREJ_LIST(sk));
	}
	pi->expected_tx_seq = (pi->expected_tx_seq + 1) % 64;
}

static inline int l2cap_data_channel_iframe(struct sock *sk, u16 rx_control, struct sk_buff *skb)
{
	struct l2cap_pinfo *pi = l2cap_pi(sk);
	u8 tx_seq = __get_txseq(rx_control);
	u8 req_seq = __get_reqseq(rx_control);
	u8 sar = rx_control >> L2CAP_CTRL_SAR_SHIFT;
	int tx_seq_offset, expected_tx_seq_offset;
	int num_to_ack = (pi->tx_win/6) + 1;
	int err = 0;

	BT_DBG("sk %p len %d tx_seq %d rx_control 0x%4.4x", sk, skb->len, tx_seq,
								rx_control);

	if (L2CAP_CTRL_FINAL & rx_control &&
			l2cap_pi(sk)->conn_state & L2CAP_CONN_WAIT_F) {
		del_timer(&pi->monitor_timer);
		if (pi->unacked_frames > 0)
			__mod_retrans_timer();
		pi->conn_state &= ~L2CAP_CONN_WAIT_F;
	}

	pi->expected_ack_seq = req_seq;
	l2cap_drop_acked_frames(sk);

	if (tx_seq == pi->expected_tx_seq)
		goto expected;

	tx_seq_offset = (tx_seq - pi->buffer_seq) % 64;
	if (tx_seq_offset < 0)
		tx_seq_offset += 64;

	/* invalid tx_seq */
	if (tx_seq_offset >= pi->tx_win) {
		l2cap_send_disconn_req(pi->conn, sk, ECONNRESET);
		goto drop;
	}

	if (pi->conn_state == L2CAP_CONN_LOCAL_BUSY)
		goto drop;

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
				l2cap_send_ack(pi);
				BT_DBG("sk %p, Exit SREJ_SENT", sk);
			}
		} else {
			struct srej_list *l;

			/* duplicated tx_seq */
			if (l2cap_add_to_srej_queue(sk, skb, tx_seq, sar) < 0)
				goto drop;

			list_for_each_entry(l, SREJ_LIST(sk), list) {
				if (l->tx_seq == tx_seq) {
					l2cap_resend_srejframe(sk, tx_seq);
					return 0;
				}
			}
			l2cap_send_srejframe(sk, tx_seq);
		}
	} else {
		expected_tx_seq_offset =
			(pi->expected_tx_seq - pi->buffer_seq) % 64;
		if (expected_tx_seq_offset < 0)
			expected_tx_seq_offset += 64;

		/* duplicated tx_seq */
		if (tx_seq_offset < expected_tx_seq_offset)
			goto drop;

		pi->conn_state |= L2CAP_CONN_SREJ_SENT;

		BT_DBG("sk %p, Enter SREJ", sk);

		INIT_LIST_HEAD(SREJ_LIST(sk));
		pi->buffer_seq_srej = pi->buffer_seq;

		__skb_queue_head_init(SREJ_QUEUE(sk));
		__skb_queue_head_init(BUSY_QUEUE(sk));
		l2cap_add_to_srej_queue(sk, skb, tx_seq, sar);

		pi->conn_state |= L2CAP_CONN_SEND_PBIT;

		l2cap_send_srejframe(sk, tx_seq);

		del_timer(&pi->ack_timer);
	}
	return 0;

expected:
	pi->expected_tx_seq = (pi->expected_tx_seq + 1) % 64;

	if (pi->conn_state & L2CAP_CONN_SREJ_SENT) {
		bt_cb(skb)->tx_seq = tx_seq;
		bt_cb(skb)->sar = sar;
		__skb_queue_tail(SREJ_QUEUE(sk), skb);
		return 0;
	}

	err = l2cap_push_rx_skb(sk, skb, rx_control);
	if (err < 0)
		return 0;

	if (rx_control & L2CAP_CTRL_FINAL) {
		if (pi->conn_state & L2CAP_CONN_REJ_ACT)
			pi->conn_state &= ~L2CAP_CONN_REJ_ACT;
		else
			l2cap_retransmit_frames(sk);
	}

	__mod_ack_timer();

	pi->num_acked = (pi->num_acked + 1) % num_to_ack;
	if (pi->num_acked == num_to_ack - 1)
		l2cap_send_ack(pi);

	return 0;

drop:
	kfree_skb(skb);
	return 0;
}

static inline void l2cap_data_channel_rrframe(struct sock *sk, u16 rx_control)
{
	struct l2cap_pinfo *pi = l2cap_pi(sk);

	BT_DBG("sk %p, req_seq %d ctrl 0x%4.4x", sk, __get_reqseq(rx_control),
						rx_control);

	pi->expected_ack_seq = __get_reqseq(rx_control);
	l2cap_drop_acked_frames(sk);

	if (rx_control & L2CAP_CTRL_POLL) {
		pi->conn_state |= L2CAP_CONN_SEND_FBIT;
		if (pi->conn_state & L2CAP_CONN_SREJ_SENT) {
			if ((pi->conn_state & L2CAP_CONN_REMOTE_BUSY) &&
					(pi->unacked_frames > 0))
				__mod_retrans_timer();

			pi->conn_state &= ~L2CAP_CONN_REMOTE_BUSY;
			l2cap_send_srejtail(sk);
		} else {
			l2cap_send_i_or_rr_or_rnr(sk);
		}

	} else if (rx_control & L2CAP_CTRL_FINAL) {
		pi->conn_state &= ~L2CAP_CONN_REMOTE_BUSY;

		if (pi->conn_state & L2CAP_CONN_REJ_ACT)
			pi->conn_state &= ~L2CAP_CONN_REJ_ACT;
		else
			l2cap_retransmit_frames(sk);

	} else {
		if ((pi->conn_state & L2CAP_CONN_REMOTE_BUSY) &&
				(pi->unacked_frames > 0))
			__mod_retrans_timer();

		pi->conn_state &= ~L2CAP_CONN_REMOTE_BUSY;
		if (pi->conn_state & L2CAP_CONN_SREJ_SENT)
			l2cap_send_ack(pi);
		else
			l2cap_ertm_send(sk);
	}
}

static inline void l2cap_data_channel_rejframe(struct sock *sk, u16 rx_control)
{
	struct l2cap_pinfo *pi = l2cap_pi(sk);
	u8 tx_seq = __get_reqseq(rx_control);

	BT_DBG("sk %p, req_seq %d ctrl 0x%4.4x", sk, tx_seq, rx_control);

	pi->conn_state &= ~L2CAP_CONN_REMOTE_BUSY;

	pi->expected_ack_seq = tx_seq;
	l2cap_drop_acked_frames(sk);

	if (rx_control & L2CAP_CTRL_FINAL) {
		if (pi->conn_state & L2CAP_CONN_REJ_ACT)
			pi->conn_state &= ~L2CAP_CONN_REJ_ACT;
		else
			l2cap_retransmit_frames(sk);
	} else {
		l2cap_retransmit_frames(sk);

		if (pi->conn_state & L2CAP_CONN_WAIT_F)
			pi->conn_state |= L2CAP_CONN_REJ_ACT;
	}
}
static inline void l2cap_data_channel_srejframe(struct sock *sk, u16 rx_control)
{
	struct l2cap_pinfo *pi = l2cap_pi(sk);
	u8 tx_seq = __get_reqseq(rx_control);

	BT_DBG("sk %p, req_seq %d ctrl 0x%4.4x", sk, tx_seq, rx_control);

	pi->conn_state &= ~L2CAP_CONN_REMOTE_BUSY;

	if (rx_control & L2CAP_CTRL_POLL) {
		pi->expected_ack_seq = tx_seq;
		l2cap_drop_acked_frames(sk);

		pi->conn_state |= L2CAP_CONN_SEND_FBIT;
		l2cap_retransmit_one_frame(sk, tx_seq);

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
			l2cap_retransmit_one_frame(sk, tx_seq);
	} else {
		l2cap_retransmit_one_frame(sk, tx_seq);
		if (pi->conn_state & L2CAP_CONN_WAIT_F) {
			pi->srej_save_reqseq = tx_seq;
			pi->conn_state |= L2CAP_CONN_SREJ_ACT;
		}
	}
}

static inline void l2cap_data_channel_rnrframe(struct sock *sk, u16 rx_control)
{
	struct l2cap_pinfo *pi = l2cap_pi(sk);
	u8 tx_seq = __get_reqseq(rx_control);

	BT_DBG("sk %p, req_seq %d ctrl 0x%4.4x", sk, tx_seq, rx_control);

	pi->conn_state |= L2CAP_CONN_REMOTE_BUSY;
	pi->expected_ack_seq = tx_seq;
	l2cap_drop_acked_frames(sk);

	if (rx_control & L2CAP_CTRL_POLL)
		pi->conn_state |= L2CAP_CONN_SEND_FBIT;

	if (!(pi->conn_state & L2CAP_CONN_SREJ_SENT)) {
		del_timer(&pi->retrans_timer);
		if (rx_control & L2CAP_CTRL_POLL)
			l2cap_send_rr_or_rnr(pi, L2CAP_CTRL_FINAL);
		return;
	}

	if (rx_control & L2CAP_CTRL_POLL)
		l2cap_send_srejtail(sk);
	else
		l2cap_send_sframe(pi, L2CAP_SUPER_RCV_READY);
}

static inline int l2cap_data_channel_sframe(struct sock *sk, u16 rx_control, struct sk_buff *skb)
{
	BT_DBG("sk %p rx_control 0x%4.4x len %d", sk, rx_control, skb->len);

	if (L2CAP_CTRL_FINAL & rx_control &&
			l2cap_pi(sk)->conn_state & L2CAP_CONN_WAIT_F) {
		del_timer(&l2cap_pi(sk)->monitor_timer);
		if (l2cap_pi(sk)->unacked_frames > 0)
			__mod_retrans_timer();
		l2cap_pi(sk)->conn_state &= ~L2CAP_CONN_WAIT_F;
	}

	switch (rx_control & L2CAP_CTRL_SUPERVISE) {
	case L2CAP_SUPER_RCV_READY:
		l2cap_data_channel_rrframe(sk, rx_control);
		break;

	case L2CAP_SUPER_REJECT:
		l2cap_data_channel_rejframe(sk, rx_control);
		break;

	case L2CAP_SUPER_SELECT_REJECT:
		l2cap_data_channel_srejframe(sk, rx_control);
		break;

	case L2CAP_SUPER_RCV_NOT_READY:
		l2cap_data_channel_rnrframe(sk, rx_control);
		break;
	}

	kfree_skb(skb);
	return 0;
}

static int l2cap_ertm_data_rcv(struct sock *sk, struct sk_buff *skb)
{
	struct l2cap_pinfo *pi = l2cap_pi(sk);
	u16 control;
	u8 req_seq;
	int len, next_tx_seq_offset, req_seq_offset;

	control = get_unaligned_le16(skb->data);
	skb_pull(skb, 2);
	len = skb->len;

	/*
	 * We can just drop the corrupted I-frame here.
	 * Receiver will miss it and start proper recovery
	 * procedures and ask retransmission.
	 */
	if (l2cap_check_fcs(pi, skb))
		goto drop;

	if (__is_sar_start(control) && __is_iframe(control))
		len -= 2;

	if (pi->fcs == L2CAP_FCS_CRC16)
		len -= 2;

	if (len > pi->mps) {
		l2cap_send_disconn_req(pi->conn, sk, ECONNRESET);
		goto drop;
	}

	req_seq = __get_reqseq(control);
	req_seq_offset = (req_seq - pi->expected_ack_seq) % 64;
	if (req_seq_offset < 0)
		req_seq_offset += 64;

	next_tx_seq_offset =
		(pi->next_tx_seq - pi->expected_ack_seq) % 64;
	if (next_tx_seq_offset < 0)
		next_tx_seq_offset += 64;

	/* check for invalid req-seq */
	if (req_seq_offset > next_tx_seq_offset) {
		l2cap_send_disconn_req(pi->conn, sk, ECONNRESET);
		goto drop;
	}

	if (__is_iframe(control)) {
		if (len < 0) {
			l2cap_send_disconn_req(pi->conn, sk, ECONNRESET);
			goto drop;
		}

		l2cap_data_channel_iframe(sk, control, skb);
	} else {
		if (len != 0) {
			BT_ERR("%d", len);
			l2cap_send_disconn_req(pi->conn, sk, ECONNRESET);
			goto drop;
		}

		l2cap_data_channel_sframe(sk, control, skb);
	}

	return 0;

drop:
	kfree_skb(skb);
	return 0;
}

static inline int l2cap_data_channel(struct l2cap_conn *conn, u16 cid, struct sk_buff *skb)
{
	struct sock *sk;
	struct l2cap_pinfo *pi;
	u16 control;
	u8 tx_seq;
	int len;

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
		if (!sock_owned_by_user(sk)) {
			l2cap_ertm_data_rcv(sk, skb);
		} else {
			if (sk_add_backlog(sk, skb))
				goto drop;
		}

		goto done;

	case L2CAP_MODE_STREAMING:
		control = get_unaligned_le16(skb->data);
		skb_pull(skb, 2);
		len = skb->len;

		if (l2cap_check_fcs(pi, skb))
			goto drop;

		if (__is_sar_start(control))
			len -= 2;

		if (pi->fcs == L2CAP_FCS_CRC16)
			len -= 2;

		if (len > pi->mps || len < 0 || __is_sframe(control))
			goto drop;

		tx_seq = __get_txseq(control);

		if (pi->expected_tx_seq == tx_seq)
			pi->expected_tx_seq = (pi->expected_tx_seq + 1) % 64;
		else
			pi->expected_tx_seq = (tx_seq + 1) % 64;

		l2cap_streaming_reassembly_sdu(sk, skb, control);

		goto done;

	default:
		BT_DBG("sk %p: bad mode 0x%2.2x", sk, pi->mode);
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

	bh_lock_sock(sk);

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
	case L2CAP_CID_LE_SIGNALING:
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
		return -EINVAL;

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

	if (!(hcon->type == ACL_LINK || hcon->type == LE_LINK))
		return -EINVAL;

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

	if (!(hcon->type == ACL_LINK || hcon->type == LE_LINK))
		return -EINVAL;

	l2cap_conn_del(hcon, bt_err(reason));

	return 0;
}

static inline void l2cap_check_encryption(struct sock *sk, u8 encrypt)
{
	if (sk->sk_type != SOCK_SEQPACKET && sk->sk_type != SOCK_STREAM)
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
				l2cap_pi(sk)->conf_state |= L2CAP_CONF_CONNECT_PEND;

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

	if (!conn)
		conn = l2cap_conn_add(hcon, 0);

	if (!conn)
		goto drop;

	BT_DBG("conn %p len %d flags 0x%x", conn, skb->len, flags);

	if (!(flags & ACL_CONT)) {
		struct l2cap_hdr *hdr;
		struct sock *sk;
		u16 cid;
		int len;

		if (conn->rx_len) {
			BT_ERR("Unexpected start frame (len %d)", skb->len);
			kfree_skb(conn->rx_skb);
			conn->rx_skb = NULL;
			conn->rx_len = 0;
			l2cap_conn_unreliable(conn, ECOMM);
		}

		/* Start fragment always begin with Basic L2CAP header */
		if (skb->len < L2CAP_HDR_SIZE) {
			BT_ERR("Frame is too short (len %d)", skb->len);
			l2cap_conn_unreliable(conn, ECOMM);
			goto drop;
		}

		hdr = (struct l2cap_hdr *) skb->data;
		len = __le16_to_cpu(hdr->len) + L2CAP_HDR_SIZE;
		cid = __le16_to_cpu(hdr->cid);

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

		sk = l2cap_get_chan_by_scid(&conn->chan_list, cid);

		if (sk && l2cap_pi(sk)->imtu < len - L2CAP_HDR_SIZE) {
			BT_ERR("Frame exceeding recv MTU (len %d, MTU %d)",
					len, l2cap_pi(sk)->imtu);
			bh_unlock_sock(sk);
			l2cap_conn_unreliable(conn, ECOMM);
			goto drop;
		}

		if (sk)
			bh_unlock_sock(sk);

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

static int l2cap_debugfs_show(struct seq_file *f, void *p)
{
	struct sock *sk;
	struct hlist_node *node;

	read_lock_bh(&l2cap_sk_list.lock);

	sk_for_each(sk, node, &l2cap_sk_list.head) {
		struct l2cap_pinfo *pi = l2cap_pi(sk);

		seq_printf(f, "%s %s %d %d 0x%4.4x 0x%4.4x %d %d %d %d\n",
					batostr(&bt_sk(sk)->src),
					batostr(&bt_sk(sk)->dst),
					sk->sk_state, __le16_to_cpu(pi->psm),
					pi->scid, pi->dcid,
					pi->imtu, pi->omtu, pi->sec_level,
					pi->mode);
	}

	read_unlock_bh(&l2cap_sk_list.lock);

	return 0;
}

static int l2cap_debugfs_open(struct inode *inode, struct file *file)
{
	return single_open(file, l2cap_debugfs_show, inode->i_private);
}

static const struct file_operations l2cap_debugfs_fops = {
	.open		= l2cap_debugfs_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static struct dentry *l2cap_debugfs;

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

int __init l2cap_init(void)
{
	int err;

	err = l2cap_init_sockets();
	if (err < 0)
		return err;

	_busy_wq = create_singlethread_workqueue("l2cap");
	if (!_busy_wq) {
		err = -ENOMEM;
		goto error;
	}

	err = hci_register_proto(&l2cap_hci_proto);
	if (err < 0) {
		BT_ERR("L2CAP protocol registration failed");
		bt_sock_unregister(BTPROTO_L2CAP);
		goto error;
	}

	if (bt_debugfs) {
		l2cap_debugfs = debugfs_create_file("l2cap", 0444,
					bt_debugfs, NULL, &l2cap_debugfs_fops);
		if (!l2cap_debugfs)
			BT_ERR("Failed to create L2CAP debug file");
	}

	return 0;

error:
	destroy_workqueue(_busy_wq);
	l2cap_cleanup_sockets();
	return err;
}

void l2cap_exit(void)
{
	debugfs_remove(l2cap_debugfs);

	flush_workqueue(_busy_wq);
	destroy_workqueue(_busy_wq);

	if (hci_unregister_proto(&l2cap_hci_proto) < 0)
		BT_ERR("L2CAP protocol unregistration failed");

	l2cap_cleanup_sockets();
}

module_param(disable_ertm, bool, 0644);
MODULE_PARM_DESC(disable_ertm, "Disable enhanced retransmission mode");
