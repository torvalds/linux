/*
   BlueZ - Bluetooth protocol stack for Linux
   Copyright (C) 2000-2001 Qualcomm Incorporated
   Copyright (C) 2009-2010 Gustavo F. Padovan <gustavo@padovan.org>
   Copyright (C) 2010 Google Inc.
   Copyright (C) 2011 ProFUSION Embedded Systems

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
#include <net/bluetooth/smp.h>

bool disable_ertm;

static u32 l2cap_feat_mask = L2CAP_FEAT_FIXED_CHAN;
static u8 l2cap_fixed_chan[8] = { L2CAP_FC_L2CAP, };

static LIST_HEAD(chan_list);
static DEFINE_RWLOCK(chan_list_lock);

static struct sk_buff *l2cap_build_cmd(struct l2cap_conn *conn,
				u8 code, u8 ident, u16 dlen, void *data);
static void l2cap_send_cmd(struct l2cap_conn *conn, u8 ident, u8 code, u16 len,
								void *data);
static int l2cap_build_conf_req(struct l2cap_chan *chan, void *data);
static void l2cap_send_disconn_req(struct l2cap_conn *conn,
				struct l2cap_chan *chan, int err);

static int l2cap_ertm_data_rcv(struct l2cap_chan *chan, struct sk_buff *skb);

/* ---- L2CAP channels ---- */

static struct l2cap_chan *__l2cap_get_chan_by_dcid(struct l2cap_conn *conn, u16 cid)
{
	struct l2cap_chan *c, *r = NULL;

	rcu_read_lock();

	list_for_each_entry_rcu(c, &conn->chan_l, list) {
		if (c->dcid == cid) {
			r = c;
			break;
		}
	}

	rcu_read_unlock();
	return r;
}

static struct l2cap_chan *__l2cap_get_chan_by_scid(struct l2cap_conn *conn, u16 cid)
{
	struct l2cap_chan *c, *r = NULL;

	rcu_read_lock();

	list_for_each_entry_rcu(c, &conn->chan_l, list) {
		if (c->scid == cid) {
			r = c;
			break;
		}
	}

	rcu_read_unlock();
	return r;
}

/* Find channel with given SCID.
 * Returns locked socket */
static struct l2cap_chan *l2cap_get_chan_by_scid(struct l2cap_conn *conn, u16 cid)
{
	struct l2cap_chan *c;

	c = __l2cap_get_chan_by_scid(conn, cid);
	if (c)
		lock_sock(c->sk);
	return c;
}

static struct l2cap_chan *__l2cap_get_chan_by_ident(struct l2cap_conn *conn, u8 ident)
{
	struct l2cap_chan *c, *r = NULL;

	rcu_read_lock();

	list_for_each_entry_rcu(c, &conn->chan_l, list) {
		if (c->ident == ident) {
			r = c;
			break;
		}
	}

	rcu_read_unlock();
	return r;
}

static inline struct l2cap_chan *l2cap_get_chan_by_ident(struct l2cap_conn *conn, u8 ident)
{
	struct l2cap_chan *c;

	c = __l2cap_get_chan_by_ident(conn, ident);
	if (c)
		lock_sock(c->sk);
	return c;
}

static struct l2cap_chan *__l2cap_global_chan_by_addr(__le16 psm, bdaddr_t *src)
{
	struct l2cap_chan *c;

	list_for_each_entry(c, &chan_list, global_l) {
		if (c->sport == psm && !bacmp(&bt_sk(c->sk)->src, src))
			return c;
	}
	return NULL;
}

int l2cap_add_psm(struct l2cap_chan *chan, bdaddr_t *src, __le16 psm)
{
	int err;

	write_lock(&chan_list_lock);

	if (psm && __l2cap_global_chan_by_addr(psm, src)) {
		err = -EADDRINUSE;
		goto done;
	}

	if (psm) {
		chan->psm = psm;
		chan->sport = psm;
		err = 0;
	} else {
		u16 p;

		err = -EINVAL;
		for (p = 0x1001; p < 0x1100; p += 2)
			if (!__l2cap_global_chan_by_addr(cpu_to_le16(p), src)) {
				chan->psm   = cpu_to_le16(p);
				chan->sport = cpu_to_le16(p);
				err = 0;
				break;
			}
	}

done:
	write_unlock(&chan_list_lock);
	return err;
}

int l2cap_add_scid(struct l2cap_chan *chan,  __u16 scid)
{
	write_lock(&chan_list_lock);

	chan->scid = scid;

	write_unlock(&chan_list_lock);

	return 0;
}

static u16 l2cap_alloc_cid(struct l2cap_conn *conn)
{
	u16 cid = L2CAP_CID_DYN_START;

	for (; cid < L2CAP_CID_DYN_END; cid++) {
		if (!__l2cap_get_chan_by_scid(conn, cid))
			return cid;
	}

	return 0;
}

static char *state_to_string(int state)
{
	switch(state) {
	case BT_CONNECTED:
		return "BT_CONNECTED";
	case BT_OPEN:
		return "BT_OPEN";
	case BT_BOUND:
		return "BT_BOUND";
	case BT_LISTEN:
		return "BT_LISTEN";
	case BT_CONNECT:
		return "BT_CONNECT";
	case BT_CONNECT2:
		return "BT_CONNECT2";
	case BT_CONFIG:
		return "BT_CONFIG";
	case BT_DISCONN:
		return "BT_DISCONN";
	case BT_CLOSED:
		return "BT_CLOSED";
	}

	return "invalid state";
}

static void l2cap_state_change(struct l2cap_chan *chan, int state)
{
	BT_DBG("%p %s -> %s", chan, state_to_string(chan->state),
						state_to_string(state));

	chan->state = state;
	chan->ops->state_change(chan->data, state);
}

static void l2cap_chan_timeout(struct work_struct *work)
{
	struct l2cap_chan *chan = container_of(work, struct l2cap_chan,
							chan_timer.work);
	struct sock *sk = chan->sk;
	int reason;

	BT_DBG("chan %p state %d", chan, chan->state);

	lock_sock(sk);

	if (chan->state == BT_CONNECTED || chan->state == BT_CONFIG)
		reason = ECONNREFUSED;
	else if (chan->state == BT_CONNECT &&
					chan->sec_level != BT_SECURITY_SDP)
		reason = ECONNREFUSED;
	else
		reason = ETIMEDOUT;

	l2cap_chan_close(chan, reason);

	release_sock(sk);

	chan->ops->close(chan->data);
	l2cap_chan_put(chan);
}

struct l2cap_chan *l2cap_chan_create(struct sock *sk)
{
	struct l2cap_chan *chan;

	chan = kzalloc(sizeof(*chan), GFP_ATOMIC);
	if (!chan)
		return NULL;

	chan->sk = sk;

	write_lock(&chan_list_lock);
	list_add(&chan->global_l, &chan_list);
	write_unlock(&chan_list_lock);

	INIT_DELAYED_WORK(&chan->chan_timer, l2cap_chan_timeout);

	chan->state = BT_OPEN;

	atomic_set(&chan->refcnt, 1);

	BT_DBG("sk %p chan %p", sk, chan);

	return chan;
}

void l2cap_chan_destroy(struct l2cap_chan *chan)
{
	write_lock(&chan_list_lock);
	list_del(&chan->global_l);
	write_unlock(&chan_list_lock);

	l2cap_chan_put(chan);
}

static void l2cap_chan_add(struct l2cap_conn *conn, struct l2cap_chan *chan)
{
	BT_DBG("conn %p, psm 0x%2.2x, dcid 0x%4.4x", conn,
			chan->psm, chan->dcid);

	conn->disc_reason = HCI_ERROR_REMOTE_USER_TERM;

	chan->conn = conn;

	if (chan->chan_type == L2CAP_CHAN_CONN_ORIENTED) {
		if (conn->hcon->type == LE_LINK) {
			/* LE connection */
			chan->omtu = L2CAP_LE_DEFAULT_MTU;
			chan->scid = L2CAP_CID_LE_DATA;
			chan->dcid = L2CAP_CID_LE_DATA;
		} else {
			/* Alloc CID for connection-oriented socket */
			chan->scid = l2cap_alloc_cid(conn);
			chan->omtu = L2CAP_DEFAULT_MTU;
		}
	} else if (chan->chan_type == L2CAP_CHAN_CONN_LESS) {
		/* Connectionless socket */
		chan->scid = L2CAP_CID_CONN_LESS;
		chan->dcid = L2CAP_CID_CONN_LESS;
		chan->omtu = L2CAP_DEFAULT_MTU;
	} else {
		/* Raw socket can send/recv signalling messages only */
		chan->scid = L2CAP_CID_SIGNALING;
		chan->dcid = L2CAP_CID_SIGNALING;
		chan->omtu = L2CAP_DEFAULT_MTU;
	}

	chan->local_id		= L2CAP_BESTEFFORT_ID;
	chan->local_stype	= L2CAP_SERV_BESTEFFORT;
	chan->local_msdu	= L2CAP_DEFAULT_MAX_SDU_SIZE;
	chan->local_sdu_itime	= L2CAP_DEFAULT_SDU_ITIME;
	chan->local_acc_lat	= L2CAP_DEFAULT_ACC_LAT;
	chan->local_flush_to	= L2CAP_DEFAULT_FLUSH_TO;

	l2cap_chan_hold(chan);

	list_add_rcu(&chan->list, &conn->chan_l);
}

/* Delete channel.
 * Must be called on the locked socket. */
static void l2cap_chan_del(struct l2cap_chan *chan, int err)
{
	struct sock *sk = chan->sk;
	struct l2cap_conn *conn = chan->conn;
	struct sock *parent = bt_sk(sk)->parent;

	__clear_chan_timer(chan);

	BT_DBG("chan %p, conn %p, err %d", chan, conn, err);

	if (conn) {
		/* Delete from channel list */
		list_del_rcu(&chan->list);
		synchronize_rcu();

		l2cap_chan_put(chan);

		chan->conn = NULL;
		hci_conn_put(conn->hcon);
	}

	l2cap_state_change(chan, BT_CLOSED);
	sock_set_flag(sk, SOCK_ZAPPED);

	if (err)
		sk->sk_err = err;

	if (parent) {
		bt_accept_unlink(sk);
		parent->sk_data_ready(parent, 0);
	} else
		sk->sk_state_change(sk);

	if (!(test_bit(CONF_OUTPUT_DONE, &chan->conf_state) &&
			test_bit(CONF_INPUT_DONE, &chan->conf_state)))
		return;

	skb_queue_purge(&chan->tx_q);

	if (chan->mode == L2CAP_MODE_ERTM) {
		struct srej_list *l, *tmp;

		__clear_retrans_timer(chan);
		__clear_monitor_timer(chan);
		__clear_ack_timer(chan);

		skb_queue_purge(&chan->srej_q);

		list_for_each_entry_safe(l, tmp, &chan->srej_l, list) {
			list_del(&l->list);
			kfree(l);
		}
	}
}

static void l2cap_chan_cleanup_listen(struct sock *parent)
{
	struct sock *sk;

	BT_DBG("parent %p", parent);

	/* Close not yet accepted channels */
	while ((sk = bt_accept_dequeue(parent, NULL))) {
		struct l2cap_chan *chan = l2cap_pi(sk)->chan;
		__clear_chan_timer(chan);
		lock_sock(sk);
		l2cap_chan_close(chan, ECONNRESET);
		release_sock(sk);
		chan->ops->close(chan->data);
	}
}

void l2cap_chan_close(struct l2cap_chan *chan, int reason)
{
	struct l2cap_conn *conn = chan->conn;
	struct sock *sk = chan->sk;

	BT_DBG("chan %p state %d socket %p", chan, chan->state, sk->sk_socket);

	switch (chan->state) {
	case BT_LISTEN:
		l2cap_chan_cleanup_listen(sk);

		l2cap_state_change(chan, BT_CLOSED);
		sock_set_flag(sk, SOCK_ZAPPED);
		break;

	case BT_CONNECTED:
	case BT_CONFIG:
		if (chan->chan_type == L2CAP_CHAN_CONN_ORIENTED &&
					conn->hcon->type == ACL_LINK) {
			__clear_chan_timer(chan);
			__set_chan_timer(chan, sk->sk_sndtimeo);
			l2cap_send_disconn_req(conn, chan, reason);
		} else
			l2cap_chan_del(chan, reason);
		break;

	case BT_CONNECT2:
		if (chan->chan_type == L2CAP_CHAN_CONN_ORIENTED &&
					conn->hcon->type == ACL_LINK) {
			struct l2cap_conn_rsp rsp;
			__u16 result;

			if (bt_sk(sk)->defer_setup)
				result = L2CAP_CR_SEC_BLOCK;
			else
				result = L2CAP_CR_BAD_PSM;
			l2cap_state_change(chan, BT_DISCONN);

			rsp.scid   = cpu_to_le16(chan->dcid);
			rsp.dcid   = cpu_to_le16(chan->scid);
			rsp.result = cpu_to_le16(result);
			rsp.status = cpu_to_le16(L2CAP_CS_NO_INFO);
			l2cap_send_cmd(conn, chan->ident, L2CAP_CONN_RSP,
							sizeof(rsp), &rsp);
		}

		l2cap_chan_del(chan, reason);
		break;

	case BT_CONNECT:
	case BT_DISCONN:
		l2cap_chan_del(chan, reason);
		break;

	default:
		sock_set_flag(sk, SOCK_ZAPPED);
		break;
	}
}

static inline u8 l2cap_get_auth_type(struct l2cap_chan *chan)
{
	if (chan->chan_type == L2CAP_CHAN_RAW) {
		switch (chan->sec_level) {
		case BT_SECURITY_HIGH:
			return HCI_AT_DEDICATED_BONDING_MITM;
		case BT_SECURITY_MEDIUM:
			return HCI_AT_DEDICATED_BONDING;
		default:
			return HCI_AT_NO_BONDING;
		}
	} else if (chan->psm == cpu_to_le16(0x0001)) {
		if (chan->sec_level == BT_SECURITY_LOW)
			chan->sec_level = BT_SECURITY_SDP;

		if (chan->sec_level == BT_SECURITY_HIGH)
			return HCI_AT_NO_BONDING_MITM;
		else
			return HCI_AT_NO_BONDING;
	} else {
		switch (chan->sec_level) {
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
int l2cap_chan_check_security(struct l2cap_chan *chan)
{
	struct l2cap_conn *conn = chan->conn;
	__u8 auth_type;

	auth_type = l2cap_get_auth_type(chan);

	return hci_conn_security(conn->hcon, chan->sec_level, auth_type);
}

static u8 l2cap_get_ident(struct l2cap_conn *conn)
{
	u8 id;

	/* Get next available identificator.
	 *    1 - 128 are used by kernel.
	 *  129 - 199 are reserved.
	 *  200 - 254 are used by utilities like l2ping, etc.
	 */

	spin_lock(&conn->lock);

	if (++conn->tx_ident > 128)
		conn->tx_ident = 1;

	id = conn->tx_ident;

	spin_unlock(&conn->lock);

	return id;
}

static void l2cap_send_cmd(struct l2cap_conn *conn, u8 ident, u8 code, u16 len, void *data)
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

	bt_cb(skb)->force_active = BT_POWER_FORCE_ACTIVE_ON;
	skb->priority = HCI_PRIO_MAX;

	hci_send_acl(conn->hchan, skb, flags);
}

static void l2cap_do_send(struct l2cap_chan *chan, struct sk_buff *skb)
{
	struct hci_conn *hcon = chan->conn->hcon;
	u16 flags;

	BT_DBG("chan %p, skb %p len %d priority %u", chan, skb, skb->len,
							skb->priority);

	if (!test_bit(FLAG_FLUSHABLE, &chan->flags) &&
					lmp_no_flush_capable(hcon->hdev))
		flags = ACL_START_NO_FLUSH;
	else
		flags = ACL_START;

	bt_cb(skb)->force_active = test_bit(FLAG_FORCE_ACTIVE, &chan->flags);
	hci_send_acl(chan->conn->hchan, skb, flags);
}

static inline void l2cap_send_sframe(struct l2cap_chan *chan, u32 control)
{
	struct sk_buff *skb;
	struct l2cap_hdr *lh;
	struct l2cap_conn *conn = chan->conn;
	int count, hlen;

	if (chan->state != BT_CONNECTED)
		return;

	if (test_bit(FLAG_EXT_CTRL, &chan->flags))
		hlen = L2CAP_EXT_HDR_SIZE;
	else
		hlen = L2CAP_ENH_HDR_SIZE;

	if (chan->fcs == L2CAP_FCS_CRC16)
		hlen += L2CAP_FCS_SIZE;

	BT_DBG("chan %p, control 0x%8.8x", chan, control);

	count = min_t(unsigned int, conn->mtu, hlen);

	control |= __set_sframe(chan);

	if (test_and_clear_bit(CONN_SEND_FBIT, &chan->conn_state))
		control |= __set_ctrl_final(chan);

	if (test_and_clear_bit(CONN_SEND_PBIT, &chan->conn_state))
		control |= __set_ctrl_poll(chan);

	skb = bt_skb_alloc(count, GFP_ATOMIC);
	if (!skb)
		return;

	lh = (struct l2cap_hdr *) skb_put(skb, L2CAP_HDR_SIZE);
	lh->len = cpu_to_le16(hlen - L2CAP_HDR_SIZE);
	lh->cid = cpu_to_le16(chan->dcid);

	__put_control(chan, control, skb_put(skb, __ctrl_size(chan)));

	if (chan->fcs == L2CAP_FCS_CRC16) {
		u16 fcs = crc16(0, (u8 *)lh, count - L2CAP_FCS_SIZE);
		put_unaligned_le16(fcs, skb_put(skb, L2CAP_FCS_SIZE));
	}

	skb->priority = HCI_PRIO_MAX;
	l2cap_do_send(chan, skb);
}

static inline void l2cap_send_rr_or_rnr(struct l2cap_chan *chan, u32 control)
{
	if (test_bit(CONN_LOCAL_BUSY, &chan->conn_state)) {
		control |= __set_ctrl_super(chan, L2CAP_SUPER_RNR);
		set_bit(CONN_RNR_SENT, &chan->conn_state);
	} else
		control |= __set_ctrl_super(chan, L2CAP_SUPER_RR);

	control |= __set_reqseq(chan, chan->buffer_seq);

	l2cap_send_sframe(chan, control);
}

static inline int __l2cap_no_conn_pending(struct l2cap_chan *chan)
{
	return !test_bit(CONF_CONNECT_PEND, &chan->conf_state);
}

static void l2cap_do_start(struct l2cap_chan *chan)
{
	struct l2cap_conn *conn = chan->conn;

	if (conn->info_state & L2CAP_INFO_FEAT_MASK_REQ_SENT) {
		if (!(conn->info_state & L2CAP_INFO_FEAT_MASK_REQ_DONE))
			return;

		if (l2cap_chan_check_security(chan) &&
				__l2cap_no_conn_pending(chan)) {
			struct l2cap_conn_req req;
			req.scid = cpu_to_le16(chan->scid);
			req.psm  = chan->psm;

			chan->ident = l2cap_get_ident(conn);
			set_bit(CONF_CONNECT_PEND, &chan->conf_state);

			l2cap_send_cmd(conn, chan->ident, L2CAP_CONN_REQ,
							sizeof(req), &req);
		}
	} else {
		struct l2cap_info_req req;
		req.type = cpu_to_le16(L2CAP_IT_FEAT_MASK);

		conn->info_state |= L2CAP_INFO_FEAT_MASK_REQ_SENT;
		conn->info_ident = l2cap_get_ident(conn);

		schedule_delayed_work(&conn->info_timer,
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

static void l2cap_send_disconn_req(struct l2cap_conn *conn, struct l2cap_chan *chan, int err)
{
	struct sock *sk;
	struct l2cap_disconn_req req;

	if (!conn)
		return;

	sk = chan->sk;

	if (chan->mode == L2CAP_MODE_ERTM) {
		__clear_retrans_timer(chan);
		__clear_monitor_timer(chan);
		__clear_ack_timer(chan);
	}

	req.dcid = cpu_to_le16(chan->dcid);
	req.scid = cpu_to_le16(chan->scid);
	l2cap_send_cmd(conn, l2cap_get_ident(conn),
			L2CAP_DISCONN_REQ, sizeof(req), &req);

	l2cap_state_change(chan, BT_DISCONN);
	sk->sk_err = err;
}

/* ---- L2CAP connections ---- */
static void l2cap_conn_start(struct l2cap_conn *conn)
{
	struct l2cap_chan *chan;

	BT_DBG("conn %p", conn);

	rcu_read_lock();

	list_for_each_entry_rcu(chan, &conn->chan_l, list) {
		struct sock *sk = chan->sk;

		bh_lock_sock(sk);

		if (chan->chan_type != L2CAP_CHAN_CONN_ORIENTED) {
			bh_unlock_sock(sk);
			continue;
		}

		if (chan->state == BT_CONNECT) {
			struct l2cap_conn_req req;

			if (!l2cap_chan_check_security(chan) ||
					!__l2cap_no_conn_pending(chan)) {
				bh_unlock_sock(sk);
				continue;
			}

			if (!l2cap_mode_supported(chan->mode, conn->feat_mask)
					&& test_bit(CONF_STATE2_DEVICE,
					&chan->conf_state)) {
				/* l2cap_chan_close() calls list_del(chan)
				 * so release the lock */
				l2cap_chan_close(chan, ECONNRESET);
				bh_unlock_sock(sk);
				continue;
			}

			req.scid = cpu_to_le16(chan->scid);
			req.psm  = chan->psm;

			chan->ident = l2cap_get_ident(conn);
			set_bit(CONF_CONNECT_PEND, &chan->conf_state);

			l2cap_send_cmd(conn, chan->ident, L2CAP_CONN_REQ,
							sizeof(req), &req);

		} else if (chan->state == BT_CONNECT2) {
			struct l2cap_conn_rsp rsp;
			char buf[128];
			rsp.scid = cpu_to_le16(chan->dcid);
			rsp.dcid = cpu_to_le16(chan->scid);

			if (l2cap_chan_check_security(chan)) {
				if (bt_sk(sk)->defer_setup) {
					struct sock *parent = bt_sk(sk)->parent;
					rsp.result = cpu_to_le16(L2CAP_CR_PEND);
					rsp.status = cpu_to_le16(L2CAP_CS_AUTHOR_PEND);
					if (parent)
						parent->sk_data_ready(parent, 0);

				} else {
					l2cap_state_change(chan, BT_CONFIG);
					rsp.result = cpu_to_le16(L2CAP_CR_SUCCESS);
					rsp.status = cpu_to_le16(L2CAP_CS_NO_INFO);
				}
			} else {
				rsp.result = cpu_to_le16(L2CAP_CR_PEND);
				rsp.status = cpu_to_le16(L2CAP_CS_AUTHEN_PEND);
			}

			l2cap_send_cmd(conn, chan->ident, L2CAP_CONN_RSP,
							sizeof(rsp), &rsp);

			if (test_bit(CONF_REQ_SENT, &chan->conf_state) ||
					rsp.result != L2CAP_CR_SUCCESS) {
				bh_unlock_sock(sk);
				continue;
			}

			set_bit(CONF_REQ_SENT, &chan->conf_state);
			l2cap_send_cmd(conn, l2cap_get_ident(conn), L2CAP_CONF_REQ,
						l2cap_build_conf_req(chan, buf), buf);
			chan->num_conf_req++;
		}

		bh_unlock_sock(sk);
	}

	rcu_read_unlock();
}

/* Find socket with cid and source bdaddr.
 * Returns closest match, locked.
 */
static struct l2cap_chan *l2cap_global_chan_by_scid(int state, __le16 cid, bdaddr_t *src)
{
	struct l2cap_chan *c, *c1 = NULL;

	read_lock(&chan_list_lock);

	list_for_each_entry(c, &chan_list, global_l) {
		struct sock *sk = c->sk;

		if (state && c->state != state)
			continue;

		if (c->scid == cid) {
			/* Exact match. */
			if (!bacmp(&bt_sk(sk)->src, src)) {
				read_unlock(&chan_list_lock);
				return c;
			}

			/* Closest match */
			if (!bacmp(&bt_sk(sk)->src, BDADDR_ANY))
				c1 = c;
		}
	}

	read_unlock(&chan_list_lock);

	return c1;
}

static void l2cap_le_conn_ready(struct l2cap_conn *conn)
{
	struct sock *parent, *sk;
	struct l2cap_chan *chan, *pchan;

	BT_DBG("");

	/* Check if we have socket listening on cid */
	pchan = l2cap_global_chan_by_scid(BT_LISTEN, L2CAP_CID_LE_DATA,
							conn->src);
	if (!pchan)
		return;

	parent = pchan->sk;

	lock_sock(parent);

	/* Check for backlog size */
	if (sk_acceptq_is_full(parent)) {
		BT_DBG("backlog full %d", parent->sk_ack_backlog);
		goto clean;
	}

	chan = pchan->ops->new_connection(pchan->data);
	if (!chan)
		goto clean;

	sk = chan->sk;

	hci_conn_hold(conn->hcon);

	bacpy(&bt_sk(sk)->src, conn->src);
	bacpy(&bt_sk(sk)->dst, conn->dst);

	bt_accept_enqueue(parent, sk);

	l2cap_chan_add(conn, chan);

	__set_chan_timer(chan, sk->sk_sndtimeo);

	l2cap_state_change(chan, BT_CONNECTED);
	parent->sk_data_ready(parent, 0);

clean:
	release_sock(parent);
}

static void l2cap_chan_ready(struct sock *sk)
{
	struct l2cap_chan *chan = l2cap_pi(sk)->chan;
	struct sock *parent = bt_sk(sk)->parent;

	BT_DBG("sk %p, parent %p", sk, parent);

	chan->conf_state = 0;
	__clear_chan_timer(chan);

	l2cap_state_change(chan, BT_CONNECTED);
	sk->sk_state_change(sk);

	if (parent)
		parent->sk_data_ready(parent, 0);
}

static void l2cap_conn_ready(struct l2cap_conn *conn)
{
	struct l2cap_chan *chan;

	BT_DBG("conn %p", conn);

	if (!conn->hcon->out && conn->hcon->type == LE_LINK)
		l2cap_le_conn_ready(conn);

	if (conn->hcon->out && conn->hcon->type == LE_LINK)
		smp_conn_security(conn, conn->hcon->pending_sec_level);

	rcu_read_lock();

	list_for_each_entry_rcu(chan, &conn->chan_l, list) {
		struct sock *sk = chan->sk;

		bh_lock_sock(sk);

		if (conn->hcon->type == LE_LINK) {
			if (smp_conn_security(conn, chan->sec_level))
				l2cap_chan_ready(sk);

		} else if (chan->chan_type != L2CAP_CHAN_CONN_ORIENTED) {
			__clear_chan_timer(chan);
			l2cap_state_change(chan, BT_CONNECTED);
			sk->sk_state_change(sk);

		} else if (chan->state == BT_CONNECT)
			l2cap_do_start(chan);

		bh_unlock_sock(sk);
	}

	rcu_read_unlock();
}

/* Notify sockets that we cannot guaranty reliability anymore */
static void l2cap_conn_unreliable(struct l2cap_conn *conn, int err)
{
	struct l2cap_chan *chan;

	BT_DBG("conn %p", conn);

	rcu_read_lock();

	list_for_each_entry_rcu(chan, &conn->chan_l, list) {
		struct sock *sk = chan->sk;

		if (test_bit(FLAG_FORCE_RELIABLE, &chan->flags))
			sk->sk_err = err;
	}

	rcu_read_unlock();
}

static void l2cap_info_timeout(struct work_struct *work)
{
	struct l2cap_conn *conn = container_of(work, struct l2cap_conn,
							info_timer.work);

	conn->info_state |= L2CAP_INFO_FEAT_MASK_REQ_DONE;
	conn->info_ident = 0;

	l2cap_conn_start(conn);
}

static void l2cap_conn_del(struct hci_conn *hcon, int err)
{
	struct l2cap_conn *conn = hcon->l2cap_data;
	struct l2cap_chan *chan, *l;
	struct sock *sk;

	if (!conn)
		return;

	BT_DBG("hcon %p conn %p, err %d", hcon, conn, err);

	kfree_skb(conn->rx_skb);

	/* Kill channels */
	list_for_each_entry_safe(chan, l, &conn->chan_l, list) {
		sk = chan->sk;
		lock_sock(sk);
		l2cap_chan_del(chan, err);
		release_sock(sk);
		chan->ops->close(chan->data);
	}

	hci_chan_del(conn->hchan);

	if (conn->info_state & L2CAP_INFO_FEAT_MASK_REQ_SENT)
		__cancel_delayed_work(&conn->info_timer);

	if (test_and_clear_bit(HCI_CONN_LE_SMP_PEND, &hcon->flags)) {
		__cancel_delayed_work(&conn->security_timer);
		smp_chan_destroy(conn);
	}

	hcon->l2cap_data = NULL;
	kfree(conn);
}

static void security_timeout(struct work_struct *work)
{
	struct l2cap_conn *conn = container_of(work, struct l2cap_conn,
						security_timer.work);

	l2cap_conn_del(conn->hcon, ETIMEDOUT);
}

static struct l2cap_conn *l2cap_conn_add(struct hci_conn *hcon, u8 status)
{
	struct l2cap_conn *conn = hcon->l2cap_data;
	struct hci_chan *hchan;

	if (conn || status)
		return conn;

	hchan = hci_chan_create(hcon);
	if (!hchan)
		return NULL;

	conn = kzalloc(sizeof(struct l2cap_conn), GFP_ATOMIC);
	if (!conn) {
		hci_chan_del(hchan);
		return NULL;
	}

	hcon->l2cap_data = conn;
	conn->hcon = hcon;
	conn->hchan = hchan;

	BT_DBG("hcon %p conn %p hchan %p", hcon, conn, hchan);

	if (hcon->hdev->le_mtu && hcon->type == LE_LINK)
		conn->mtu = hcon->hdev->le_mtu;
	else
		conn->mtu = hcon->hdev->acl_mtu;

	conn->src = &hcon->hdev->bdaddr;
	conn->dst = &hcon->dst;

	conn->feat_mask = 0;

	spin_lock_init(&conn->lock);

	INIT_LIST_HEAD(&conn->chan_l);

	if (hcon->type == LE_LINK)
		INIT_DELAYED_WORK(&conn->security_timer, security_timeout);
	else
		INIT_DELAYED_WORK(&conn->info_timer, l2cap_info_timeout);

	conn->disc_reason = HCI_ERROR_REMOTE_USER_TERM;

	return conn;
}

/* ---- Socket interface ---- */

/* Find socket with psm and source bdaddr.
 * Returns closest match.
 */
static struct l2cap_chan *l2cap_global_chan_by_psm(int state, __le16 psm, bdaddr_t *src)
{
	struct l2cap_chan *c, *c1 = NULL;

	read_lock(&chan_list_lock);

	list_for_each_entry(c, &chan_list, global_l) {
		struct sock *sk = c->sk;

		if (state && c->state != state)
			continue;

		if (c->psm == psm) {
			/* Exact match. */
			if (!bacmp(&bt_sk(sk)->src, src)) {
				read_unlock(&chan_list_lock);
				return c;
			}

			/* Closest match */
			if (!bacmp(&bt_sk(sk)->src, BDADDR_ANY))
				c1 = c;
		}
	}

	read_unlock(&chan_list_lock);

	return c1;
}

int l2cap_chan_connect(struct l2cap_chan *chan, __le16 psm, u16 cid, bdaddr_t *dst)
{
	struct sock *sk = chan->sk;
	bdaddr_t *src = &bt_sk(sk)->src;
	struct l2cap_conn *conn;
	struct hci_conn *hcon;
	struct hci_dev *hdev;
	__u8 auth_type;
	int err;

	BT_DBG("%s -> %s psm 0x%2.2x", batostr(src), batostr(dst),
							chan->psm);

	hdev = hci_get_route(dst, src);
	if (!hdev)
		return -EHOSTUNREACH;

	hci_dev_lock(hdev);

	lock_sock(sk);

	/* PSM must be odd and lsb of upper byte must be 0 */
	if ((__le16_to_cpu(psm) & 0x0101) != 0x0001 && !cid &&
					chan->chan_type != L2CAP_CHAN_RAW) {
		err = -EINVAL;
		goto done;
	}

	if (chan->chan_type == L2CAP_CHAN_CONN_ORIENTED && !(psm || cid)) {
		err = -EINVAL;
		goto done;
	}

	switch (chan->mode) {
	case L2CAP_MODE_BASIC:
		break;
	case L2CAP_MODE_ERTM:
	case L2CAP_MODE_STREAMING:
		if (!disable_ertm)
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
		err = 0;
		goto done;

	case BT_CONNECTED:
		/* Already connected */
		err = -EISCONN;
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
	bacpy(&bt_sk(sk)->dst, dst);
	chan->psm = psm;
	chan->dcid = cid;

	auth_type = l2cap_get_auth_type(chan);

	if (chan->dcid == L2CAP_CID_LE_DATA)
		hcon = hci_connect(hdev, LE_LINK, dst,
					chan->sec_level, auth_type);
	else
		hcon = hci_connect(hdev, ACL_LINK, dst,
					chan->sec_level, auth_type);

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

	l2cap_chan_add(conn, chan);

	l2cap_state_change(chan, BT_CONNECT);
	__set_chan_timer(chan, sk->sk_sndtimeo);

	if (hcon->state == BT_CONNECTED) {
		if (chan->chan_type != L2CAP_CHAN_CONN_ORIENTED) {
			__clear_chan_timer(chan);
			if (l2cap_chan_check_security(chan))
				l2cap_state_change(chan, BT_CONNECTED);
		} else
			l2cap_do_start(chan);
	}

	err = 0;

done:
	hci_dev_unlock(hdev);
	hci_dev_put(hdev);
	return err;
}

int __l2cap_wait_ack(struct sock *sk)
{
	struct l2cap_chan *chan = l2cap_pi(sk)->chan;
	DECLARE_WAITQUEUE(wait, current);
	int err = 0;
	int timeo = HZ/5;

	add_wait_queue(sk_sleep(sk), &wait);
	set_current_state(TASK_INTERRUPTIBLE);
	while (chan->unacked_frames > 0 && chan->conn) {
		if (!timeo)
			timeo = HZ/5;

		if (signal_pending(current)) {
			err = sock_intr_errno(timeo);
			break;
		}

		release_sock(sk);
		timeo = schedule_timeout(timeo);
		lock_sock(sk);
		set_current_state(TASK_INTERRUPTIBLE);

		err = sock_error(sk);
		if (err)
			break;
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(sk_sleep(sk), &wait);
	return err;
}

static void l2cap_monitor_timeout(struct work_struct *work)
{
	struct l2cap_chan *chan = container_of(work, struct l2cap_chan,
							monitor_timer.work);
	struct sock *sk = chan->sk;

	BT_DBG("chan %p", chan);

	lock_sock(sk);
	if (chan->retry_count >= chan->remote_max_tx) {
		l2cap_send_disconn_req(chan->conn, chan, ECONNABORTED);
		release_sock(sk);
		return;
	}

	chan->retry_count++;
	__set_monitor_timer(chan);

	l2cap_send_rr_or_rnr(chan, L2CAP_CTRL_POLL);
	release_sock(sk);
}

static void l2cap_retrans_timeout(struct work_struct *work)
{
	struct l2cap_chan *chan = container_of(work, struct l2cap_chan,
							retrans_timer.work);
	struct sock *sk = chan->sk;

	BT_DBG("chan %p", chan);

	lock_sock(sk);
	chan->retry_count = 1;
	__set_monitor_timer(chan);

	set_bit(CONN_WAIT_F, &chan->conn_state);

	l2cap_send_rr_or_rnr(chan, L2CAP_CTRL_POLL);
	release_sock(sk);
}

static void l2cap_drop_acked_frames(struct l2cap_chan *chan)
{
	struct sk_buff *skb;

	while ((skb = skb_peek(&chan->tx_q)) &&
			chan->unacked_frames) {
		if (bt_cb(skb)->tx_seq == chan->expected_ack_seq)
			break;

		skb = skb_dequeue(&chan->tx_q);
		kfree_skb(skb);

		chan->unacked_frames--;
	}

	if (!chan->unacked_frames)
		__clear_retrans_timer(chan);
}

static void l2cap_streaming_send(struct l2cap_chan *chan)
{
	struct sk_buff *skb;
	u32 control;
	u16 fcs;

	while ((skb = skb_dequeue(&chan->tx_q))) {
		control = __get_control(chan, skb->data + L2CAP_HDR_SIZE);
		control |= __set_txseq(chan, chan->next_tx_seq);
		__put_control(chan, control, skb->data + L2CAP_HDR_SIZE);

		if (chan->fcs == L2CAP_FCS_CRC16) {
			fcs = crc16(0, (u8 *)skb->data,
						skb->len - L2CAP_FCS_SIZE);
			put_unaligned_le16(fcs,
					skb->data + skb->len - L2CAP_FCS_SIZE);
		}

		l2cap_do_send(chan, skb);

		chan->next_tx_seq = __next_seq(chan, chan->next_tx_seq);
	}
}

static void l2cap_retransmit_one_frame(struct l2cap_chan *chan, u16 tx_seq)
{
	struct sk_buff *skb, *tx_skb;
	u16 fcs;
	u32 control;

	skb = skb_peek(&chan->tx_q);
	if (!skb)
		return;

	while (bt_cb(skb)->tx_seq != tx_seq) {
		if (skb_queue_is_last(&chan->tx_q, skb))
			return;

		skb = skb_queue_next(&chan->tx_q, skb);
	}

	if (chan->remote_max_tx &&
			bt_cb(skb)->retries == chan->remote_max_tx) {
		l2cap_send_disconn_req(chan->conn, chan, ECONNABORTED);
		return;
	}

	tx_skb = skb_clone(skb, GFP_ATOMIC);
	bt_cb(skb)->retries++;

	control = __get_control(chan, tx_skb->data + L2CAP_HDR_SIZE);
	control &= __get_sar_mask(chan);

	if (test_and_clear_bit(CONN_SEND_FBIT, &chan->conn_state))
		control |= __set_ctrl_final(chan);

	control |= __set_reqseq(chan, chan->buffer_seq);
	control |= __set_txseq(chan, tx_seq);

	__put_control(chan, control, tx_skb->data + L2CAP_HDR_SIZE);

	if (chan->fcs == L2CAP_FCS_CRC16) {
		fcs = crc16(0, (u8 *)tx_skb->data,
						tx_skb->len - L2CAP_FCS_SIZE);
		put_unaligned_le16(fcs,
				tx_skb->data + tx_skb->len - L2CAP_FCS_SIZE);
	}

	l2cap_do_send(chan, tx_skb);
}

static int l2cap_ertm_send(struct l2cap_chan *chan)
{
	struct sk_buff *skb, *tx_skb;
	u16 fcs;
	u32 control;
	int nsent = 0;

	if (chan->state != BT_CONNECTED)
		return -ENOTCONN;

	while ((skb = chan->tx_send_head) && (!l2cap_tx_window_full(chan))) {

		if (chan->remote_max_tx &&
				bt_cb(skb)->retries == chan->remote_max_tx) {
			l2cap_send_disconn_req(chan->conn, chan, ECONNABORTED);
			break;
		}

		tx_skb = skb_clone(skb, GFP_ATOMIC);

		bt_cb(skb)->retries++;

		control = __get_control(chan, tx_skb->data + L2CAP_HDR_SIZE);
		control &= __get_sar_mask(chan);

		if (test_and_clear_bit(CONN_SEND_FBIT, &chan->conn_state))
			control |= __set_ctrl_final(chan);

		control |= __set_reqseq(chan, chan->buffer_seq);
		control |= __set_txseq(chan, chan->next_tx_seq);

		__put_control(chan, control, tx_skb->data + L2CAP_HDR_SIZE);

		if (chan->fcs == L2CAP_FCS_CRC16) {
			fcs = crc16(0, (u8 *)skb->data,
						tx_skb->len - L2CAP_FCS_SIZE);
			put_unaligned_le16(fcs, skb->data +
						tx_skb->len - L2CAP_FCS_SIZE);
		}

		l2cap_do_send(chan, tx_skb);

		__set_retrans_timer(chan);

		bt_cb(skb)->tx_seq = chan->next_tx_seq;

		chan->next_tx_seq = __next_seq(chan, chan->next_tx_seq);

		if (bt_cb(skb)->retries == 1)
			chan->unacked_frames++;

		chan->frames_sent++;

		if (skb_queue_is_last(&chan->tx_q, skb))
			chan->tx_send_head = NULL;
		else
			chan->tx_send_head = skb_queue_next(&chan->tx_q, skb);

		nsent++;
	}

	return nsent;
}

static int l2cap_retransmit_frames(struct l2cap_chan *chan)
{
	int ret;

	if (!skb_queue_empty(&chan->tx_q))
		chan->tx_send_head = chan->tx_q.next;

	chan->next_tx_seq = chan->expected_ack_seq;
	ret = l2cap_ertm_send(chan);
	return ret;
}

static void __l2cap_send_ack(struct l2cap_chan *chan)
{
	u32 control = 0;

	control |= __set_reqseq(chan, chan->buffer_seq);

	if (test_bit(CONN_LOCAL_BUSY, &chan->conn_state)) {
		control |= __set_ctrl_super(chan, L2CAP_SUPER_RNR);
		set_bit(CONN_RNR_SENT, &chan->conn_state);
		l2cap_send_sframe(chan, control);
		return;
	}

	if (l2cap_ertm_send(chan) > 0)
		return;

	control |= __set_ctrl_super(chan, L2CAP_SUPER_RR);
	l2cap_send_sframe(chan, control);
}

static void l2cap_send_ack(struct l2cap_chan *chan)
{
	__clear_ack_timer(chan);
	__l2cap_send_ack(chan);
}

static void l2cap_send_srejtail(struct l2cap_chan *chan)
{
	struct srej_list *tail;
	u32 control;

	control = __set_ctrl_super(chan, L2CAP_SUPER_SREJ);
	control |= __set_ctrl_final(chan);

	tail = list_entry((&chan->srej_l)->prev, struct srej_list, list);
	control |= __set_reqseq(chan, tail->tx_seq);

	l2cap_send_sframe(chan, control);
}

static inline int l2cap_skbuff_fromiovec(struct l2cap_chan *chan, struct msghdr *msg, int len, int count, struct sk_buff *skb)
{
	struct sock *sk = chan->sk;
	struct l2cap_conn *conn = chan->conn;
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

		(*frag)->priority = skb->priority;

		sent += count;
		len  -= count;

		frag = &(*frag)->next;
	}

	return sent;
}

static struct sk_buff *l2cap_create_connless_pdu(struct l2cap_chan *chan,
						struct msghdr *msg, size_t len,
						u32 priority)
{
	struct sock *sk = chan->sk;
	struct l2cap_conn *conn = chan->conn;
	struct sk_buff *skb;
	int err, count, hlen = L2CAP_HDR_SIZE + L2CAP_PSMLEN_SIZE;
	struct l2cap_hdr *lh;

	BT_DBG("sk %p len %d priority %u", sk, (int)len, priority);

	count = min_t(unsigned int, (conn->mtu - hlen), len);
	skb = bt_skb_send_alloc(sk, count + hlen,
			msg->msg_flags & MSG_DONTWAIT, &err);
	if (!skb)
		return ERR_PTR(err);

	skb->priority = priority;

	/* Create L2CAP header */
	lh = (struct l2cap_hdr *) skb_put(skb, L2CAP_HDR_SIZE);
	lh->cid = cpu_to_le16(chan->dcid);
	lh->len = cpu_to_le16(len + (hlen - L2CAP_HDR_SIZE));
	put_unaligned_le16(chan->psm, skb_put(skb, 2));

	err = l2cap_skbuff_fromiovec(chan, msg, len, count, skb);
	if (unlikely(err < 0)) {
		kfree_skb(skb);
		return ERR_PTR(err);
	}
	return skb;
}

static struct sk_buff *l2cap_create_basic_pdu(struct l2cap_chan *chan,
						struct msghdr *msg, size_t len,
						u32 priority)
{
	struct sock *sk = chan->sk;
	struct l2cap_conn *conn = chan->conn;
	struct sk_buff *skb;
	int err, count, hlen = L2CAP_HDR_SIZE;
	struct l2cap_hdr *lh;

	BT_DBG("sk %p len %d", sk, (int)len);

	count = min_t(unsigned int, (conn->mtu - hlen), len);
	skb = bt_skb_send_alloc(sk, count + hlen,
			msg->msg_flags & MSG_DONTWAIT, &err);
	if (!skb)
		return ERR_PTR(err);

	skb->priority = priority;

	/* Create L2CAP header */
	lh = (struct l2cap_hdr *) skb_put(skb, L2CAP_HDR_SIZE);
	lh->cid = cpu_to_le16(chan->dcid);
	lh->len = cpu_to_le16(len + (hlen - L2CAP_HDR_SIZE));

	err = l2cap_skbuff_fromiovec(chan, msg, len, count, skb);
	if (unlikely(err < 0)) {
		kfree_skb(skb);
		return ERR_PTR(err);
	}
	return skb;
}

static struct sk_buff *l2cap_create_iframe_pdu(struct l2cap_chan *chan,
						struct msghdr *msg, size_t len,
						u32 control, u16 sdulen)
{
	struct sock *sk = chan->sk;
	struct l2cap_conn *conn = chan->conn;
	struct sk_buff *skb;
	int err, count, hlen;
	struct l2cap_hdr *lh;

	BT_DBG("sk %p len %d", sk, (int)len);

	if (!conn)
		return ERR_PTR(-ENOTCONN);

	if (test_bit(FLAG_EXT_CTRL, &chan->flags))
		hlen = L2CAP_EXT_HDR_SIZE;
	else
		hlen = L2CAP_ENH_HDR_SIZE;

	if (sdulen)
		hlen += L2CAP_SDULEN_SIZE;

	if (chan->fcs == L2CAP_FCS_CRC16)
		hlen += L2CAP_FCS_SIZE;

	count = min_t(unsigned int, (conn->mtu - hlen), len);
	skb = bt_skb_send_alloc(sk, count + hlen,
			msg->msg_flags & MSG_DONTWAIT, &err);
	if (!skb)
		return ERR_PTR(err);

	/* Create L2CAP header */
	lh = (struct l2cap_hdr *) skb_put(skb, L2CAP_HDR_SIZE);
	lh->cid = cpu_to_le16(chan->dcid);
	lh->len = cpu_to_le16(len + (hlen - L2CAP_HDR_SIZE));

	__put_control(chan, control, skb_put(skb, __ctrl_size(chan)));

	if (sdulen)
		put_unaligned_le16(sdulen, skb_put(skb, L2CAP_SDULEN_SIZE));

	err = l2cap_skbuff_fromiovec(chan, msg, len, count, skb);
	if (unlikely(err < 0)) {
		kfree_skb(skb);
		return ERR_PTR(err);
	}

	if (chan->fcs == L2CAP_FCS_CRC16)
		put_unaligned_le16(0, skb_put(skb, L2CAP_FCS_SIZE));

	bt_cb(skb)->retries = 0;
	return skb;
}

static int l2cap_sar_segment_sdu(struct l2cap_chan *chan, struct msghdr *msg, size_t len)
{
	struct sk_buff *skb;
	struct sk_buff_head sar_queue;
	u32 control;
	size_t size = 0;

	skb_queue_head_init(&sar_queue);
	control = __set_ctrl_sar(chan, L2CAP_SAR_START);
	skb = l2cap_create_iframe_pdu(chan, msg, chan->remote_mps, control, len);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	__skb_queue_tail(&sar_queue, skb);
	len -= chan->remote_mps;
	size += chan->remote_mps;

	while (len > 0) {
		size_t buflen;

		if (len > chan->remote_mps) {
			control = __set_ctrl_sar(chan, L2CAP_SAR_CONTINUE);
			buflen = chan->remote_mps;
		} else {
			control = __set_ctrl_sar(chan, L2CAP_SAR_END);
			buflen = len;
		}

		skb = l2cap_create_iframe_pdu(chan, msg, buflen, control, 0);
		if (IS_ERR(skb)) {
			skb_queue_purge(&sar_queue);
			return PTR_ERR(skb);
		}

		__skb_queue_tail(&sar_queue, skb);
		len -= buflen;
		size += buflen;
	}
	skb_queue_splice_tail(&sar_queue, &chan->tx_q);
	if (chan->tx_send_head == NULL)
		chan->tx_send_head = sar_queue.next;

	return size;
}

int l2cap_chan_send(struct l2cap_chan *chan, struct msghdr *msg, size_t len,
								u32 priority)
{
	struct sk_buff *skb;
	u32 control;
	int err;

	/* Connectionless channel */
	if (chan->chan_type == L2CAP_CHAN_CONN_LESS) {
		skb = l2cap_create_connless_pdu(chan, msg, len, priority);
		if (IS_ERR(skb))
			return PTR_ERR(skb);

		l2cap_do_send(chan, skb);
		return len;
	}

	switch (chan->mode) {
	case L2CAP_MODE_BASIC:
		/* Check outgoing MTU */
		if (len > chan->omtu)
			return -EMSGSIZE;

		/* Create a basic PDU */
		skb = l2cap_create_basic_pdu(chan, msg, len, priority);
		if (IS_ERR(skb))
			return PTR_ERR(skb);

		l2cap_do_send(chan, skb);
		err = len;
		break;

	case L2CAP_MODE_ERTM:
	case L2CAP_MODE_STREAMING:
		/* Entire SDU fits into one PDU */
		if (len <= chan->remote_mps) {
			control = __set_ctrl_sar(chan, L2CAP_SAR_UNSEGMENTED);
			skb = l2cap_create_iframe_pdu(chan, msg, len, control,
									0);
			if (IS_ERR(skb))
				return PTR_ERR(skb);

			__skb_queue_tail(&chan->tx_q, skb);

			if (chan->tx_send_head == NULL)
				chan->tx_send_head = skb;

		} else {
			/* Segment SDU into multiples PDUs */
			err = l2cap_sar_segment_sdu(chan, msg, len);
			if (err < 0)
				return err;
		}

		if (chan->mode == L2CAP_MODE_STREAMING) {
			l2cap_streaming_send(chan);
			err = len;
			break;
		}

		if (test_bit(CONN_REMOTE_BUSY, &chan->conn_state) &&
				test_bit(CONN_WAIT_F, &chan->conn_state)) {
			err = len;
			break;
		}

		err = l2cap_ertm_send(chan);
		if (err >= 0)
			err = len;

		break;

	default:
		BT_DBG("bad state %1.1x", chan->mode);
		err = -EBADFD;
	}

	return err;
}

/* Copy frame to all raw sockets on that connection */
static void l2cap_raw_recv(struct l2cap_conn *conn, struct sk_buff *skb)
{
	struct sk_buff *nskb;
	struct l2cap_chan *chan;

	BT_DBG("conn %p", conn);

	rcu_read_lock();

	list_for_each_entry_rcu(chan, &conn->chan_l, list) {
		struct sock *sk = chan->sk;
		if (chan->chan_type != L2CAP_CHAN_RAW)
			continue;

		/* Don't send frame to the socket it came from */
		if (skb->sk == sk)
			continue;
		nskb = skb_clone(skb, GFP_ATOMIC);
		if (!nskb)
			continue;

		if (chan->ops->recv(chan->data, nskb))
			kfree_skb(nskb);
	}

	rcu_read_unlock();
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

static void l2cap_add_opt_efs(void **ptr, struct l2cap_chan *chan)
{
	struct l2cap_conf_efs efs;

	switch (chan->mode) {
	case L2CAP_MODE_ERTM:
		efs.id		= chan->local_id;
		efs.stype	= chan->local_stype;
		efs.msdu	= cpu_to_le16(chan->local_msdu);
		efs.sdu_itime	= cpu_to_le32(chan->local_sdu_itime);
		efs.acc_lat	= cpu_to_le32(L2CAP_DEFAULT_ACC_LAT);
		efs.flush_to	= cpu_to_le32(L2CAP_DEFAULT_FLUSH_TO);
		break;

	case L2CAP_MODE_STREAMING:
		efs.id		= 1;
		efs.stype	= L2CAP_SERV_BESTEFFORT;
		efs.msdu	= cpu_to_le16(chan->local_msdu);
		efs.sdu_itime	= cpu_to_le32(chan->local_sdu_itime);
		efs.acc_lat	= 0;
		efs.flush_to	= 0;
		break;

	default:
		return;
	}

	l2cap_add_conf_opt(ptr, L2CAP_CONF_EFS, sizeof(efs),
							(unsigned long) &efs);
}

static void l2cap_ack_timeout(struct work_struct *work)
{
	struct l2cap_chan *chan = container_of(work, struct l2cap_chan,
							ack_timer.work);

	BT_DBG("chan %p", chan);

	lock_sock(chan->sk);
	__l2cap_send_ack(chan);
	release_sock(chan->sk);

	l2cap_chan_put(chan);
}

static inline void l2cap_ertm_init(struct l2cap_chan *chan)
{
	chan->expected_ack_seq = 0;
	chan->unacked_frames = 0;
	chan->buffer_seq = 0;
	chan->num_acked = 0;
	chan->frames_sent = 0;

	INIT_DELAYED_WORK(&chan->retrans_timer, l2cap_retrans_timeout);
	INIT_DELAYED_WORK(&chan->monitor_timer, l2cap_monitor_timeout);
	INIT_DELAYED_WORK(&chan->ack_timer, l2cap_ack_timeout);

	skb_queue_head_init(&chan->srej_q);

	INIT_LIST_HEAD(&chan->srej_l);
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

static inline bool __l2cap_ews_supported(struct l2cap_chan *chan)
{
	return enable_hs && chan->conn->feat_mask & L2CAP_FEAT_EXT_WINDOW;
}

static inline bool __l2cap_efs_supported(struct l2cap_chan *chan)
{
	return enable_hs && chan->conn->feat_mask & L2CAP_FEAT_EXT_FLOW;
}

static inline void l2cap_txwin_setup(struct l2cap_chan *chan)
{
	if (chan->tx_win > L2CAP_DEFAULT_TX_WINDOW &&
						__l2cap_ews_supported(chan)) {
		/* use extended control field */
		set_bit(FLAG_EXT_CTRL, &chan->flags);
		chan->tx_win_max = L2CAP_DEFAULT_EXT_WINDOW;
	} else {
		chan->tx_win = min_t(u16, chan->tx_win,
						L2CAP_DEFAULT_TX_WINDOW);
		chan->tx_win_max = L2CAP_DEFAULT_TX_WINDOW;
	}
}

static int l2cap_build_conf_req(struct l2cap_chan *chan, void *data)
{
	struct l2cap_conf_req *req = data;
	struct l2cap_conf_rfc rfc = { .mode = chan->mode };
	void *ptr = req->data;
	u16 size;

	BT_DBG("chan %p", chan);

	if (chan->num_conf_req || chan->num_conf_rsp)
		goto done;

	switch (chan->mode) {
	case L2CAP_MODE_STREAMING:
	case L2CAP_MODE_ERTM:
		if (test_bit(CONF_STATE2_DEVICE, &chan->conf_state))
			break;

		if (__l2cap_efs_supported(chan))
			set_bit(FLAG_EFS_ENABLE, &chan->flags);

		/* fall through */
	default:
		chan->mode = l2cap_select_mode(rfc.mode, chan->conn->feat_mask);
		break;
	}

done:
	if (chan->imtu != L2CAP_DEFAULT_MTU)
		l2cap_add_conf_opt(&ptr, L2CAP_CONF_MTU, 2, chan->imtu);

	switch (chan->mode) {
	case L2CAP_MODE_BASIC:
		if (!(chan->conn->feat_mask & L2CAP_FEAT_ERTM) &&
				!(chan->conn->feat_mask & L2CAP_FEAT_STREAMING))
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
		rfc.max_transmit    = chan->max_tx;
		rfc.retrans_timeout = 0;
		rfc.monitor_timeout = 0;

		size = min_t(u16, L2CAP_DEFAULT_MAX_PDU_SIZE, chan->conn->mtu -
						L2CAP_EXT_HDR_SIZE -
						L2CAP_SDULEN_SIZE -
						L2CAP_FCS_SIZE);
		rfc.max_pdu_size = cpu_to_le16(size);

		l2cap_txwin_setup(chan);

		rfc.txwin_size = min_t(u16, chan->tx_win,
						L2CAP_DEFAULT_TX_WINDOW);

		l2cap_add_conf_opt(&ptr, L2CAP_CONF_RFC, sizeof(rfc),
							(unsigned long) &rfc);

		if (test_bit(FLAG_EFS_ENABLE, &chan->flags))
			l2cap_add_opt_efs(&ptr, chan);

		if (!(chan->conn->feat_mask & L2CAP_FEAT_FCS))
			break;

		if (chan->fcs == L2CAP_FCS_NONE ||
				test_bit(CONF_NO_FCS_RECV, &chan->conf_state)) {
			chan->fcs = L2CAP_FCS_NONE;
			l2cap_add_conf_opt(&ptr, L2CAP_CONF_FCS, 1, chan->fcs);
		}

		if (test_bit(FLAG_EXT_CTRL, &chan->flags))
			l2cap_add_conf_opt(&ptr, L2CAP_CONF_EWS, 2,
								chan->tx_win);
		break;

	case L2CAP_MODE_STREAMING:
		rfc.mode            = L2CAP_MODE_STREAMING;
		rfc.txwin_size      = 0;
		rfc.max_transmit    = 0;
		rfc.retrans_timeout = 0;
		rfc.monitor_timeout = 0;

		size = min_t(u16, L2CAP_DEFAULT_MAX_PDU_SIZE, chan->conn->mtu -
						L2CAP_EXT_HDR_SIZE -
						L2CAP_SDULEN_SIZE -
						L2CAP_FCS_SIZE);
		rfc.max_pdu_size = cpu_to_le16(size);

		l2cap_add_conf_opt(&ptr, L2CAP_CONF_RFC, sizeof(rfc),
							(unsigned long) &rfc);

		if (test_bit(FLAG_EFS_ENABLE, &chan->flags))
			l2cap_add_opt_efs(&ptr, chan);

		if (!(chan->conn->feat_mask & L2CAP_FEAT_FCS))
			break;

		if (chan->fcs == L2CAP_FCS_NONE ||
				test_bit(CONF_NO_FCS_RECV, &chan->conf_state)) {
			chan->fcs = L2CAP_FCS_NONE;
			l2cap_add_conf_opt(&ptr, L2CAP_CONF_FCS, 1, chan->fcs);
		}
		break;
	}

	req->dcid  = cpu_to_le16(chan->dcid);
	req->flags = cpu_to_le16(0);

	return ptr - data;
}

static int l2cap_parse_conf_req(struct l2cap_chan *chan, void *data)
{
	struct l2cap_conf_rsp *rsp = data;
	void *ptr = rsp->data;
	void *req = chan->conf_req;
	int len = chan->conf_len;
	int type, hint, olen;
	unsigned long val;
	struct l2cap_conf_rfc rfc = { .mode = L2CAP_MODE_BASIC };
	struct l2cap_conf_efs efs;
	u8 remote_efs = 0;
	u16 mtu = L2CAP_DEFAULT_MTU;
	u16 result = L2CAP_CONF_SUCCESS;
	u16 size;

	BT_DBG("chan %p", chan);

	while (len >= L2CAP_CONF_OPT_SIZE) {
		len -= l2cap_get_conf_opt(&req, &type, &olen, &val);

		hint  = type & L2CAP_CONF_HINT;
		type &= L2CAP_CONF_MASK;

		switch (type) {
		case L2CAP_CONF_MTU:
			mtu = val;
			break;

		case L2CAP_CONF_FLUSH_TO:
			chan->flush_to = val;
			break;

		case L2CAP_CONF_QOS:
			break;

		case L2CAP_CONF_RFC:
			if (olen == sizeof(rfc))
				memcpy(&rfc, (void *) val, olen);
			break;

		case L2CAP_CONF_FCS:
			if (val == L2CAP_FCS_NONE)
				set_bit(CONF_NO_FCS_RECV, &chan->conf_state);
			break;

		case L2CAP_CONF_EFS:
			remote_efs = 1;
			if (olen == sizeof(efs))
				memcpy(&efs, (void *) val, olen);
			break;

		case L2CAP_CONF_EWS:
			if (!enable_hs)
				return -ECONNREFUSED;

			set_bit(FLAG_EXT_CTRL, &chan->flags);
			set_bit(CONF_EWS_RECV, &chan->conf_state);
			chan->tx_win_max = L2CAP_DEFAULT_EXT_WINDOW;
			chan->remote_tx_win = val;
			break;

		default:
			if (hint)
				break;

			result = L2CAP_CONF_UNKNOWN;
			*((u8 *) ptr++) = type;
			break;
		}
	}

	if (chan->num_conf_rsp || chan->num_conf_req > 1)
		goto done;

	switch (chan->mode) {
	case L2CAP_MODE_STREAMING:
	case L2CAP_MODE_ERTM:
		if (!test_bit(CONF_STATE2_DEVICE, &chan->conf_state)) {
			chan->mode = l2cap_select_mode(rfc.mode,
					chan->conn->feat_mask);
			break;
		}

		if (remote_efs) {
			if (__l2cap_efs_supported(chan))
				set_bit(FLAG_EFS_ENABLE, &chan->flags);
			else
				return -ECONNREFUSED;
		}

		if (chan->mode != rfc.mode)
			return -ECONNREFUSED;

		break;
	}

done:
	if (chan->mode != rfc.mode) {
		result = L2CAP_CONF_UNACCEPT;
		rfc.mode = chan->mode;

		if (chan->num_conf_rsp == 1)
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
			chan->omtu = mtu;
			set_bit(CONF_MTU_DONE, &chan->conf_state);
		}
		l2cap_add_conf_opt(&ptr, L2CAP_CONF_MTU, 2, chan->omtu);

		if (remote_efs) {
			if (chan->local_stype != L2CAP_SERV_NOTRAFIC &&
					efs.stype != L2CAP_SERV_NOTRAFIC &&
					efs.stype != chan->local_stype) {

				result = L2CAP_CONF_UNACCEPT;

				if (chan->num_conf_req >= 1)
					return -ECONNREFUSED;

				l2cap_add_conf_opt(&ptr, L2CAP_CONF_EFS,
							sizeof(efs),
							(unsigned long) &efs);
			} else {
				/* Send PENDING Conf Rsp */
				result = L2CAP_CONF_PENDING;
				set_bit(CONF_LOC_CONF_PEND, &chan->conf_state);
			}
		}

		switch (rfc.mode) {
		case L2CAP_MODE_BASIC:
			chan->fcs = L2CAP_FCS_NONE;
			set_bit(CONF_MODE_DONE, &chan->conf_state);
			break;

		case L2CAP_MODE_ERTM:
			if (!test_bit(CONF_EWS_RECV, &chan->conf_state))
				chan->remote_tx_win = rfc.txwin_size;
			else
				rfc.txwin_size = L2CAP_DEFAULT_TX_WINDOW;

			chan->remote_max_tx = rfc.max_transmit;

			size = min_t(u16, le16_to_cpu(rfc.max_pdu_size),
						chan->conn->mtu -
						L2CAP_EXT_HDR_SIZE -
						L2CAP_SDULEN_SIZE -
						L2CAP_FCS_SIZE);
			rfc.max_pdu_size = cpu_to_le16(size);
			chan->remote_mps = size;

			rfc.retrans_timeout =
				le16_to_cpu(L2CAP_DEFAULT_RETRANS_TO);
			rfc.monitor_timeout =
				le16_to_cpu(L2CAP_DEFAULT_MONITOR_TO);

			set_bit(CONF_MODE_DONE, &chan->conf_state);

			l2cap_add_conf_opt(&ptr, L2CAP_CONF_RFC,
					sizeof(rfc), (unsigned long) &rfc);

			if (test_bit(FLAG_EFS_ENABLE, &chan->flags)) {
				chan->remote_id = efs.id;
				chan->remote_stype = efs.stype;
				chan->remote_msdu = le16_to_cpu(efs.msdu);
				chan->remote_flush_to =
						le32_to_cpu(efs.flush_to);
				chan->remote_acc_lat =
						le32_to_cpu(efs.acc_lat);
				chan->remote_sdu_itime =
					le32_to_cpu(efs.sdu_itime);
				l2cap_add_conf_opt(&ptr, L2CAP_CONF_EFS,
					sizeof(efs), (unsigned long) &efs);
			}
			break;

		case L2CAP_MODE_STREAMING:
			size = min_t(u16, le16_to_cpu(rfc.max_pdu_size),
						chan->conn->mtu -
						L2CAP_EXT_HDR_SIZE -
						L2CAP_SDULEN_SIZE -
						L2CAP_FCS_SIZE);
			rfc.max_pdu_size = cpu_to_le16(size);
			chan->remote_mps = size;

			set_bit(CONF_MODE_DONE, &chan->conf_state);

			l2cap_add_conf_opt(&ptr, L2CAP_CONF_RFC,
					sizeof(rfc), (unsigned long) &rfc);

			break;

		default:
			result = L2CAP_CONF_UNACCEPT;

			memset(&rfc, 0, sizeof(rfc));
			rfc.mode = chan->mode;
		}

		if (result == L2CAP_CONF_SUCCESS)
			set_bit(CONF_OUTPUT_DONE, &chan->conf_state);
	}
	rsp->scid   = cpu_to_le16(chan->dcid);
	rsp->result = cpu_to_le16(result);
	rsp->flags  = cpu_to_le16(0x0000);

	return ptr - data;
}

static int l2cap_parse_conf_rsp(struct l2cap_chan *chan, void *rsp, int len, void *data, u16 *result)
{
	struct l2cap_conf_req *req = data;
	void *ptr = req->data;
	int type, olen;
	unsigned long val;
	struct l2cap_conf_rfc rfc = { .mode = L2CAP_MODE_BASIC };
	struct l2cap_conf_efs efs;

	BT_DBG("chan %p, rsp %p, len %d, req %p", chan, rsp, len, data);

	while (len >= L2CAP_CONF_OPT_SIZE) {
		len -= l2cap_get_conf_opt(&rsp, &type, &olen, &val);

		switch (type) {
		case L2CAP_CONF_MTU:
			if (val < L2CAP_DEFAULT_MIN_MTU) {
				*result = L2CAP_CONF_UNACCEPT;
				chan->imtu = L2CAP_DEFAULT_MIN_MTU;
			} else
				chan->imtu = val;
			l2cap_add_conf_opt(&ptr, L2CAP_CONF_MTU, 2, chan->imtu);
			break;

		case L2CAP_CONF_FLUSH_TO:
			chan->flush_to = val;
			l2cap_add_conf_opt(&ptr, L2CAP_CONF_FLUSH_TO,
							2, chan->flush_to);
			break;

		case L2CAP_CONF_RFC:
			if (olen == sizeof(rfc))
				memcpy(&rfc, (void *)val, olen);

			if (test_bit(CONF_STATE2_DEVICE, &chan->conf_state) &&
							rfc.mode != chan->mode)
				return -ECONNREFUSED;

			chan->fcs = 0;

			l2cap_add_conf_opt(&ptr, L2CAP_CONF_RFC,
					sizeof(rfc), (unsigned long) &rfc);
			break;

		case L2CAP_CONF_EWS:
			chan->tx_win = min_t(u16, val,
						L2CAP_DEFAULT_EXT_WINDOW);
			l2cap_add_conf_opt(&ptr, L2CAP_CONF_EWS, 2,
							chan->tx_win);
			break;

		case L2CAP_CONF_EFS:
			if (olen == sizeof(efs))
				memcpy(&efs, (void *)val, olen);

			if (chan->local_stype != L2CAP_SERV_NOTRAFIC &&
					efs.stype != L2CAP_SERV_NOTRAFIC &&
					efs.stype != chan->local_stype)
				return -ECONNREFUSED;

			l2cap_add_conf_opt(&ptr, L2CAP_CONF_EFS,
					sizeof(efs), (unsigned long) &efs);
			break;
		}
	}

	if (chan->mode == L2CAP_MODE_BASIC && chan->mode != rfc.mode)
		return -ECONNREFUSED;

	chan->mode = rfc.mode;

	if (*result == L2CAP_CONF_SUCCESS || *result == L2CAP_CONF_PENDING) {
		switch (rfc.mode) {
		case L2CAP_MODE_ERTM:
			chan->retrans_timeout = le16_to_cpu(rfc.retrans_timeout);
			chan->monitor_timeout = le16_to_cpu(rfc.monitor_timeout);
			chan->mps    = le16_to_cpu(rfc.max_pdu_size);

			if (test_bit(FLAG_EFS_ENABLE, &chan->flags)) {
				chan->local_msdu = le16_to_cpu(efs.msdu);
				chan->local_sdu_itime =
						le32_to_cpu(efs.sdu_itime);
				chan->local_acc_lat = le32_to_cpu(efs.acc_lat);
				chan->local_flush_to =
						le32_to_cpu(efs.flush_to);
			}
			break;

		case L2CAP_MODE_STREAMING:
			chan->mps    = le16_to_cpu(rfc.max_pdu_size);
		}
	}

	req->dcid   = cpu_to_le16(chan->dcid);
	req->flags  = cpu_to_le16(0x0000);

	return ptr - data;
}

static int l2cap_build_conf_rsp(struct l2cap_chan *chan, void *data, u16 result, u16 flags)
{
	struct l2cap_conf_rsp *rsp = data;
	void *ptr = rsp->data;

	BT_DBG("chan %p", chan);

	rsp->scid   = cpu_to_le16(chan->dcid);
	rsp->result = cpu_to_le16(result);
	rsp->flags  = cpu_to_le16(flags);

	return ptr - data;
}

void __l2cap_connect_rsp_defer(struct l2cap_chan *chan)
{
	struct l2cap_conn_rsp rsp;
	struct l2cap_conn *conn = chan->conn;
	u8 buf[128];

	rsp.scid   = cpu_to_le16(chan->dcid);
	rsp.dcid   = cpu_to_le16(chan->scid);
	rsp.result = cpu_to_le16(L2CAP_CR_SUCCESS);
	rsp.status = cpu_to_le16(L2CAP_CS_NO_INFO);
	l2cap_send_cmd(conn, chan->ident,
				L2CAP_CONN_RSP, sizeof(rsp), &rsp);

	if (test_and_set_bit(CONF_REQ_SENT, &chan->conf_state))
		return;

	l2cap_send_cmd(conn, l2cap_get_ident(conn), L2CAP_CONF_REQ,
			l2cap_build_conf_req(chan, buf), buf);
	chan->num_conf_req++;
}

static void l2cap_conf_rfc_get(struct l2cap_chan *chan, void *rsp, int len)
{
	int type, olen;
	unsigned long val;
	struct l2cap_conf_rfc rfc;

	BT_DBG("chan %p, rsp %p, len %d", chan, rsp, len);

	if ((chan->mode != L2CAP_MODE_ERTM) && (chan->mode != L2CAP_MODE_STREAMING))
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

	/* Use sane default values in case a misbehaving remote device
	 * did not send an RFC option.
	 */
	rfc.mode = chan->mode;
	rfc.retrans_timeout = cpu_to_le16(L2CAP_DEFAULT_RETRANS_TO);
	rfc.monitor_timeout = cpu_to_le16(L2CAP_DEFAULT_MONITOR_TO);
	rfc.max_pdu_size = cpu_to_le16(chan->imtu);

	BT_ERR("Expected RFC option was not found, using defaults");

done:
	switch (rfc.mode) {
	case L2CAP_MODE_ERTM:
		chan->retrans_timeout = le16_to_cpu(rfc.retrans_timeout);
		chan->monitor_timeout = le16_to_cpu(rfc.monitor_timeout);
		chan->mps    = le16_to_cpu(rfc.max_pdu_size);
		break;
	case L2CAP_MODE_STREAMING:
		chan->mps    = le16_to_cpu(rfc.max_pdu_size);
	}
}

static inline int l2cap_command_rej(struct l2cap_conn *conn, struct l2cap_cmd_hdr *cmd, u8 *data)
{
	struct l2cap_cmd_rej_unk *rej = (struct l2cap_cmd_rej_unk *) data;

	if (rej->reason != L2CAP_REJ_NOT_UNDERSTOOD)
		return 0;

	if ((conn->info_state & L2CAP_INFO_FEAT_MASK_REQ_SENT) &&
					cmd->ident == conn->info_ident) {
		__cancel_delayed_work(&conn->info_timer);

		conn->info_state |= L2CAP_INFO_FEAT_MASK_REQ_DONE;
		conn->info_ident = 0;

		l2cap_conn_start(conn);
	}

	return 0;
}

static inline int l2cap_connect_req(struct l2cap_conn *conn, struct l2cap_cmd_hdr *cmd, u8 *data)
{
	struct l2cap_conn_req *req = (struct l2cap_conn_req *) data;
	struct l2cap_conn_rsp rsp;
	struct l2cap_chan *chan = NULL, *pchan;
	struct sock *parent, *sk = NULL;
	int result, status = L2CAP_CS_NO_INFO;

	u16 dcid = 0, scid = __le16_to_cpu(req->scid);
	__le16 psm = req->psm;

	BT_DBG("psm 0x%2.2x scid 0x%4.4x", psm, scid);

	/* Check if we have socket listening on psm */
	pchan = l2cap_global_chan_by_psm(BT_LISTEN, psm, conn->src);
	if (!pchan) {
		result = L2CAP_CR_BAD_PSM;
		goto sendresp;
	}

	parent = pchan->sk;

	lock_sock(parent);

	/* Check if the ACL is secure enough (if not SDP) */
	if (psm != cpu_to_le16(0x0001) &&
				!hci_conn_check_link_mode(conn->hcon)) {
		conn->disc_reason = HCI_ERROR_AUTH_FAILURE;
		result = L2CAP_CR_SEC_BLOCK;
		goto response;
	}

	result = L2CAP_CR_NO_MEM;

	/* Check for backlog size */
	if (sk_acceptq_is_full(parent)) {
		BT_DBG("backlog full %d", parent->sk_ack_backlog);
		goto response;
	}

	chan = pchan->ops->new_connection(pchan->data);
	if (!chan)
		goto response;

	sk = chan->sk;

	/* Check if we already have channel with that dcid */
	if (__l2cap_get_chan_by_dcid(conn, scid)) {
		sock_set_flag(sk, SOCK_ZAPPED);
		chan->ops->close(chan->data);
		goto response;
	}

	hci_conn_hold(conn->hcon);

	bacpy(&bt_sk(sk)->src, conn->src);
	bacpy(&bt_sk(sk)->dst, conn->dst);
	chan->psm  = psm;
	chan->dcid = scid;

	bt_accept_enqueue(parent, sk);

	l2cap_chan_add(conn, chan);

	dcid = chan->scid;

	__set_chan_timer(chan, sk->sk_sndtimeo);

	chan->ident = cmd->ident;

	if (conn->info_state & L2CAP_INFO_FEAT_MASK_REQ_DONE) {
		if (l2cap_chan_check_security(chan)) {
			if (bt_sk(sk)->defer_setup) {
				l2cap_state_change(chan, BT_CONNECT2);
				result = L2CAP_CR_PEND;
				status = L2CAP_CS_AUTHOR_PEND;
				parent->sk_data_ready(parent, 0);
			} else {
				l2cap_state_change(chan, BT_CONFIG);
				result = L2CAP_CR_SUCCESS;
				status = L2CAP_CS_NO_INFO;
			}
		} else {
			l2cap_state_change(chan, BT_CONNECT2);
			result = L2CAP_CR_PEND;
			status = L2CAP_CS_AUTHEN_PEND;
		}
	} else {
		l2cap_state_change(chan, BT_CONNECT2);
		result = L2CAP_CR_PEND;
		status = L2CAP_CS_NO_INFO;
	}

response:
	release_sock(parent);

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

		schedule_delayed_work(&conn->info_timer,
					msecs_to_jiffies(L2CAP_INFO_TIMEOUT));

		l2cap_send_cmd(conn, conn->info_ident,
					L2CAP_INFO_REQ, sizeof(info), &info);
	}

	if (chan && !test_bit(CONF_REQ_SENT, &chan->conf_state) &&
				result == L2CAP_CR_SUCCESS) {
		u8 buf[128];
		set_bit(CONF_REQ_SENT, &chan->conf_state);
		l2cap_send_cmd(conn, l2cap_get_ident(conn), L2CAP_CONF_REQ,
					l2cap_build_conf_req(chan, buf), buf);
		chan->num_conf_req++;
	}

	return 0;
}

static inline int l2cap_connect_rsp(struct l2cap_conn *conn, struct l2cap_cmd_hdr *cmd, u8 *data)
{
	struct l2cap_conn_rsp *rsp = (struct l2cap_conn_rsp *) data;
	u16 scid, dcid, result, status;
	struct l2cap_chan *chan;
	struct sock *sk;
	u8 req[128];

	scid   = __le16_to_cpu(rsp->scid);
	dcid   = __le16_to_cpu(rsp->dcid);
	result = __le16_to_cpu(rsp->result);
	status = __le16_to_cpu(rsp->status);

	BT_DBG("dcid 0x%4.4x scid 0x%4.4x result 0x%2.2x status 0x%2.2x", dcid, scid, result, status);

	if (scid) {
		chan = l2cap_get_chan_by_scid(conn, scid);
		if (!chan)
			return -EFAULT;
	} else {
		chan = l2cap_get_chan_by_ident(conn, cmd->ident);
		if (!chan)
			return -EFAULT;
	}

	sk = chan->sk;

	switch (result) {
	case L2CAP_CR_SUCCESS:
		l2cap_state_change(chan, BT_CONFIG);
		chan->ident = 0;
		chan->dcid = dcid;
		clear_bit(CONF_CONNECT_PEND, &chan->conf_state);

		if (test_and_set_bit(CONF_REQ_SENT, &chan->conf_state))
			break;

		l2cap_send_cmd(conn, l2cap_get_ident(conn), L2CAP_CONF_REQ,
					l2cap_build_conf_req(chan, req), req);
		chan->num_conf_req++;
		break;

	case L2CAP_CR_PEND:
		set_bit(CONF_CONNECT_PEND, &chan->conf_state);
		break;

	default:
		l2cap_chan_del(chan, ECONNREFUSED);
		break;
	}

	release_sock(sk);
	return 0;
}

static inline void set_default_fcs(struct l2cap_chan *chan)
{
	/* FCS is enabled only in ERTM or streaming mode, if one or both
	 * sides request it.
	 */
	if (chan->mode != L2CAP_MODE_ERTM && chan->mode != L2CAP_MODE_STREAMING)
		chan->fcs = L2CAP_FCS_NONE;
	else if (!test_bit(CONF_NO_FCS_RECV, &chan->conf_state))
		chan->fcs = L2CAP_FCS_CRC16;
}

static inline int l2cap_config_req(struct l2cap_conn *conn, struct l2cap_cmd_hdr *cmd, u16 cmd_len, u8 *data)
{
	struct l2cap_conf_req *req = (struct l2cap_conf_req *) data;
	u16 dcid, flags;
	u8 rsp[64];
	struct l2cap_chan *chan;
	struct sock *sk;
	int len;

	dcid  = __le16_to_cpu(req->dcid);
	flags = __le16_to_cpu(req->flags);

	BT_DBG("dcid 0x%4.4x flags 0x%2.2x", dcid, flags);

	chan = l2cap_get_chan_by_scid(conn, dcid);
	if (!chan)
		return -ENOENT;

	sk = chan->sk;

	if (chan->state != BT_CONFIG && chan->state != BT_CONNECT2) {
		struct l2cap_cmd_rej_cid rej;

		rej.reason = cpu_to_le16(L2CAP_REJ_INVALID_CID);
		rej.scid = cpu_to_le16(chan->scid);
		rej.dcid = cpu_to_le16(chan->dcid);

		l2cap_send_cmd(conn, cmd->ident, L2CAP_COMMAND_REJ,
				sizeof(rej), &rej);
		goto unlock;
	}

	/* Reject if config buffer is too small. */
	len = cmd_len - sizeof(*req);
	if (len < 0 || chan->conf_len + len > sizeof(chan->conf_req)) {
		l2cap_send_cmd(conn, cmd->ident, L2CAP_CONF_RSP,
				l2cap_build_conf_rsp(chan, rsp,
					L2CAP_CONF_REJECT, flags), rsp);
		goto unlock;
	}

	/* Store config. */
	memcpy(chan->conf_req + chan->conf_len, req->data, len);
	chan->conf_len += len;

	if (flags & 0x0001) {
		/* Incomplete config. Send empty response. */
		l2cap_send_cmd(conn, cmd->ident, L2CAP_CONF_RSP,
				l2cap_build_conf_rsp(chan, rsp,
					L2CAP_CONF_SUCCESS, 0x0001), rsp);
		goto unlock;
	}

	/* Complete config. */
	len = l2cap_parse_conf_req(chan, rsp);
	if (len < 0) {
		l2cap_send_disconn_req(conn, chan, ECONNRESET);
		goto unlock;
	}

	l2cap_send_cmd(conn, cmd->ident, L2CAP_CONF_RSP, len, rsp);
	chan->num_conf_rsp++;

	/* Reset config buffer. */
	chan->conf_len = 0;

	if (!test_bit(CONF_OUTPUT_DONE, &chan->conf_state))
		goto unlock;

	if (test_bit(CONF_INPUT_DONE, &chan->conf_state)) {
		set_default_fcs(chan);

		l2cap_state_change(chan, BT_CONNECTED);

		chan->next_tx_seq = 0;
		chan->expected_tx_seq = 0;
		skb_queue_head_init(&chan->tx_q);
		if (chan->mode == L2CAP_MODE_ERTM)
			l2cap_ertm_init(chan);

		l2cap_chan_ready(sk);
		goto unlock;
	}

	if (!test_and_set_bit(CONF_REQ_SENT, &chan->conf_state)) {
		u8 buf[64];
		l2cap_send_cmd(conn, l2cap_get_ident(conn), L2CAP_CONF_REQ,
					l2cap_build_conf_req(chan, buf), buf);
		chan->num_conf_req++;
	}

	/* Got Conf Rsp PENDING from remote side and asume we sent
	   Conf Rsp PENDING in the code above */
	if (test_bit(CONF_REM_CONF_PEND, &chan->conf_state) &&
			test_bit(CONF_LOC_CONF_PEND, &chan->conf_state)) {

		/* check compatibility */

		clear_bit(CONF_LOC_CONF_PEND, &chan->conf_state);
		set_bit(CONF_OUTPUT_DONE, &chan->conf_state);

		l2cap_send_cmd(conn, cmd->ident, L2CAP_CONF_RSP,
					l2cap_build_conf_rsp(chan, rsp,
					L2CAP_CONF_SUCCESS, 0x0000), rsp);
	}

unlock:
	release_sock(sk);
	return 0;
}

static inline int l2cap_config_rsp(struct l2cap_conn *conn, struct l2cap_cmd_hdr *cmd, u8 *data)
{
	struct l2cap_conf_rsp *rsp = (struct l2cap_conf_rsp *)data;
	u16 scid, flags, result;
	struct l2cap_chan *chan;
	struct sock *sk;
	int len = cmd->len - sizeof(*rsp);

	scid   = __le16_to_cpu(rsp->scid);
	flags  = __le16_to_cpu(rsp->flags);
	result = __le16_to_cpu(rsp->result);

	BT_DBG("scid 0x%4.4x flags 0x%2.2x result 0x%2.2x",
			scid, flags, result);

	chan = l2cap_get_chan_by_scid(conn, scid);
	if (!chan)
		return 0;

	sk = chan->sk;

	switch (result) {
	case L2CAP_CONF_SUCCESS:
		l2cap_conf_rfc_get(chan, rsp->data, len);
		clear_bit(CONF_REM_CONF_PEND, &chan->conf_state);
		break;

	case L2CAP_CONF_PENDING:
		set_bit(CONF_REM_CONF_PEND, &chan->conf_state);

		if (test_bit(CONF_LOC_CONF_PEND, &chan->conf_state)) {
			char buf[64];

			len = l2cap_parse_conf_rsp(chan, rsp->data, len,
								buf, &result);
			if (len < 0) {
				l2cap_send_disconn_req(conn, chan, ECONNRESET);
				goto done;
			}

			/* check compatibility */

			clear_bit(CONF_LOC_CONF_PEND, &chan->conf_state);
			set_bit(CONF_OUTPUT_DONE, &chan->conf_state);

			l2cap_send_cmd(conn, cmd->ident, L2CAP_CONF_RSP,
						l2cap_build_conf_rsp(chan, buf,
						L2CAP_CONF_SUCCESS, 0x0000), buf);
		}
		goto done;

	case L2CAP_CONF_UNACCEPT:
		if (chan->num_conf_rsp <= L2CAP_CONF_MAX_CONF_RSP) {
			char req[64];

			if (len > sizeof(req) - sizeof(struct l2cap_conf_req)) {
				l2cap_send_disconn_req(conn, chan, ECONNRESET);
				goto done;
			}

			/* throw out any old stored conf requests */
			result = L2CAP_CONF_SUCCESS;
			len = l2cap_parse_conf_rsp(chan, rsp->data, len,
								req, &result);
			if (len < 0) {
				l2cap_send_disconn_req(conn, chan, ECONNRESET);
				goto done;
			}

			l2cap_send_cmd(conn, l2cap_get_ident(conn),
						L2CAP_CONF_REQ, len, req);
			chan->num_conf_req++;
			if (result != L2CAP_CONF_SUCCESS)
				goto done;
			break;
		}

	default:
		sk->sk_err = ECONNRESET;
		__set_chan_timer(chan,
				msecs_to_jiffies(L2CAP_DISC_REJ_TIMEOUT));
		l2cap_send_disconn_req(conn, chan, ECONNRESET);
		goto done;
	}

	if (flags & 0x01)
		goto done;

	set_bit(CONF_INPUT_DONE, &chan->conf_state);

	if (test_bit(CONF_OUTPUT_DONE, &chan->conf_state)) {
		set_default_fcs(chan);

		l2cap_state_change(chan, BT_CONNECTED);
		chan->next_tx_seq = 0;
		chan->expected_tx_seq = 0;
		skb_queue_head_init(&chan->tx_q);
		if (chan->mode ==  L2CAP_MODE_ERTM)
			l2cap_ertm_init(chan);

		l2cap_chan_ready(sk);
	}

done:
	release_sock(sk);
	return 0;
}

static inline int l2cap_disconnect_req(struct l2cap_conn *conn, struct l2cap_cmd_hdr *cmd, u8 *data)
{
	struct l2cap_disconn_req *req = (struct l2cap_disconn_req *) data;
	struct l2cap_disconn_rsp rsp;
	u16 dcid, scid;
	struct l2cap_chan *chan;
	struct sock *sk;

	scid = __le16_to_cpu(req->scid);
	dcid = __le16_to_cpu(req->dcid);

	BT_DBG("scid 0x%4.4x dcid 0x%4.4x", scid, dcid);

	chan = l2cap_get_chan_by_scid(conn, dcid);
	if (!chan)
		return 0;

	sk = chan->sk;

	rsp.dcid = cpu_to_le16(chan->scid);
	rsp.scid = cpu_to_le16(chan->dcid);
	l2cap_send_cmd(conn, cmd->ident, L2CAP_DISCONN_RSP, sizeof(rsp), &rsp);

	sk->sk_shutdown = SHUTDOWN_MASK;

	l2cap_chan_del(chan, ECONNRESET);
	release_sock(sk);

	chan->ops->close(chan->data);
	return 0;
}

static inline int l2cap_disconnect_rsp(struct l2cap_conn *conn, struct l2cap_cmd_hdr *cmd, u8 *data)
{
	struct l2cap_disconn_rsp *rsp = (struct l2cap_disconn_rsp *) data;
	u16 dcid, scid;
	struct l2cap_chan *chan;
	struct sock *sk;

	scid = __le16_to_cpu(rsp->scid);
	dcid = __le16_to_cpu(rsp->dcid);

	BT_DBG("dcid 0x%4.4x scid 0x%4.4x", dcid, scid);

	chan = l2cap_get_chan_by_scid(conn, scid);
	if (!chan)
		return 0;

	sk = chan->sk;

	l2cap_chan_del(chan, 0);
	release_sock(sk);

	chan->ops->close(chan->data);
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
		if (enable_hs)
			feat_mask |= L2CAP_FEAT_EXT_FLOW
						| L2CAP_FEAT_EXT_WINDOW;

		put_unaligned_le32(feat_mask, rsp->data);
		l2cap_send_cmd(conn, cmd->ident,
					L2CAP_INFO_RSP, sizeof(buf), buf);
	} else if (type == L2CAP_IT_FIXED_CHAN) {
		u8 buf[12];
		struct l2cap_info_rsp *rsp = (struct l2cap_info_rsp *) buf;

		if (enable_hs)
			l2cap_fixed_chan[0] |= L2CAP_FC_A2MP;
		else
			l2cap_fixed_chan[0] &= ~L2CAP_FC_A2MP;

		rsp->type   = cpu_to_le16(L2CAP_IT_FIXED_CHAN);
		rsp->result = cpu_to_le16(L2CAP_IR_SUCCESS);
		memcpy(rsp->data, l2cap_fixed_chan, sizeof(l2cap_fixed_chan));
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

	/* L2CAP Info req/rsp are unbound to channels, add extra checks */
	if (cmd->ident != conn->info_ident ||
			conn->info_state & L2CAP_INFO_FEAT_MASK_REQ_DONE)
		return 0;

	__cancel_delayed_work(&conn->info_timer);

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

static inline int l2cap_create_channel_req(struct l2cap_conn *conn,
					struct l2cap_cmd_hdr *cmd, u16 cmd_len,
					void *data)
{
	struct l2cap_create_chan_req *req = data;
	struct l2cap_create_chan_rsp rsp;
	u16 psm, scid;

	if (cmd_len != sizeof(*req))
		return -EPROTO;

	if (!enable_hs)
		return -EINVAL;

	psm = le16_to_cpu(req->psm);
	scid = le16_to_cpu(req->scid);

	BT_DBG("psm %d, scid %d, amp_id %d", psm, scid, req->amp_id);

	/* Placeholder: Always reject */
	rsp.dcid = 0;
	rsp.scid = cpu_to_le16(scid);
	rsp.result = L2CAP_CR_NO_MEM;
	rsp.status = L2CAP_CS_NO_INFO;

	l2cap_send_cmd(conn, cmd->ident, L2CAP_CREATE_CHAN_RSP,
		       sizeof(rsp), &rsp);

	return 0;
}

static inline int l2cap_create_channel_rsp(struct l2cap_conn *conn,
					struct l2cap_cmd_hdr *cmd, void *data)
{
	BT_DBG("conn %p", conn);

	return l2cap_connect_rsp(conn, cmd, data);
}

static void l2cap_send_move_chan_rsp(struct l2cap_conn *conn, u8 ident,
							u16 icid, u16 result)
{
	struct l2cap_move_chan_rsp rsp;

	BT_DBG("icid %d, result %d", icid, result);

	rsp.icid = cpu_to_le16(icid);
	rsp.result = cpu_to_le16(result);

	l2cap_send_cmd(conn, ident, L2CAP_MOVE_CHAN_RSP, sizeof(rsp), &rsp);
}

static void l2cap_send_move_chan_cfm(struct l2cap_conn *conn,
				struct l2cap_chan *chan, u16 icid, u16 result)
{
	struct l2cap_move_chan_cfm cfm;
	u8 ident;

	BT_DBG("icid %d, result %d", icid, result);

	ident = l2cap_get_ident(conn);
	if (chan)
		chan->ident = ident;

	cfm.icid = cpu_to_le16(icid);
	cfm.result = cpu_to_le16(result);

	l2cap_send_cmd(conn, ident, L2CAP_MOVE_CHAN_CFM, sizeof(cfm), &cfm);
}

static void l2cap_send_move_chan_cfm_rsp(struct l2cap_conn *conn, u8 ident,
								u16 icid)
{
	struct l2cap_move_chan_cfm_rsp rsp;

	BT_DBG("icid %d", icid);

	rsp.icid = cpu_to_le16(icid);
	l2cap_send_cmd(conn, ident, L2CAP_MOVE_CHAN_CFM_RSP, sizeof(rsp), &rsp);
}

static inline int l2cap_move_channel_req(struct l2cap_conn *conn,
			struct l2cap_cmd_hdr *cmd, u16 cmd_len, void *data)
{
	struct l2cap_move_chan_req *req = data;
	u16 icid = 0;
	u16 result = L2CAP_MR_NOT_ALLOWED;

	if (cmd_len != sizeof(*req))
		return -EPROTO;

	icid = le16_to_cpu(req->icid);

	BT_DBG("icid %d, dest_amp_id %d", icid, req->dest_amp_id);

	if (!enable_hs)
		return -EINVAL;

	/* Placeholder: Always refuse */
	l2cap_send_move_chan_rsp(conn, cmd->ident, icid, result);

	return 0;
}

static inline int l2cap_move_channel_rsp(struct l2cap_conn *conn,
			struct l2cap_cmd_hdr *cmd, u16 cmd_len, void *data)
{
	struct l2cap_move_chan_rsp *rsp = data;
	u16 icid, result;

	if (cmd_len != sizeof(*rsp))
		return -EPROTO;

	icid = le16_to_cpu(rsp->icid);
	result = le16_to_cpu(rsp->result);

	BT_DBG("icid %d, result %d", icid, result);

	/* Placeholder: Always unconfirmed */
	l2cap_send_move_chan_cfm(conn, NULL, icid, L2CAP_MC_UNCONFIRMED);

	return 0;
}

static inline int l2cap_move_channel_confirm(struct l2cap_conn *conn,
			struct l2cap_cmd_hdr *cmd, u16 cmd_len, void *data)
{
	struct l2cap_move_chan_cfm *cfm = data;
	u16 icid, result;

	if (cmd_len != sizeof(*cfm))
		return -EPROTO;

	icid = le16_to_cpu(cfm->icid);
	result = le16_to_cpu(cfm->result);

	BT_DBG("icid %d, result %d", icid, result);

	l2cap_send_move_chan_cfm_rsp(conn, cmd->ident, icid);

	return 0;
}

static inline int l2cap_move_channel_confirm_rsp(struct l2cap_conn *conn,
			struct l2cap_cmd_hdr *cmd, u16 cmd_len, void *data)
{
	struct l2cap_move_chan_cfm_rsp *rsp = data;
	u16 icid;

	if (cmd_len != sizeof(*rsp))
		return -EPROTO;

	icid = le16_to_cpu(rsp->icid);

	BT_DBG("icid %d", icid);

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

	case L2CAP_CREATE_CHAN_REQ:
		err = l2cap_create_channel_req(conn, cmd, cmd_len, data);
		break;

	case L2CAP_CREATE_CHAN_RSP:
		err = l2cap_create_channel_rsp(conn, cmd, data);
		break;

	case L2CAP_MOVE_CHAN_REQ:
		err = l2cap_move_channel_req(conn, cmd, cmd_len, data);
		break;

	case L2CAP_MOVE_CHAN_RSP:
		err = l2cap_move_channel_rsp(conn, cmd, cmd_len, data);
		break;

	case L2CAP_MOVE_CHAN_CFM:
		err = l2cap_move_channel_confirm(conn, cmd, cmd_len, data);
		break;

	case L2CAP_MOVE_CHAN_CFM_RSP:
		err = l2cap_move_channel_confirm_rsp(conn, cmd, cmd_len, data);
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
			struct l2cap_cmd_rej_unk rej;

			BT_ERR("Wrong link type (%d)", err);

			/* FIXME: Map err to a valid reason */
			rej.reason = cpu_to_le16(L2CAP_REJ_NOT_UNDERSTOOD);
			l2cap_send_cmd(conn, cmd.ident, L2CAP_COMMAND_REJ, sizeof(rej), &rej);
		}

		data += cmd_len;
		len  -= cmd_len;
	}

	kfree_skb(skb);
}

static int l2cap_check_fcs(struct l2cap_chan *chan,  struct sk_buff *skb)
{
	u16 our_fcs, rcv_fcs;
	int hdr_size;

	if (test_bit(FLAG_EXT_CTRL, &chan->flags))
		hdr_size = L2CAP_EXT_HDR_SIZE;
	else
		hdr_size = L2CAP_ENH_HDR_SIZE;

	if (chan->fcs == L2CAP_FCS_CRC16) {
		skb_trim(skb, skb->len - L2CAP_FCS_SIZE);
		rcv_fcs = get_unaligned_le16(skb->data + skb->len);
		our_fcs = crc16(0, skb->data - hdr_size, skb->len + hdr_size);

		if (our_fcs != rcv_fcs)
			return -EBADMSG;
	}
	return 0;
}

static inline void l2cap_send_i_or_rr_or_rnr(struct l2cap_chan *chan)
{
	u32 control = 0;

	chan->frames_sent = 0;

	control |= __set_reqseq(chan, chan->buffer_seq);

	if (test_bit(CONN_LOCAL_BUSY, &chan->conn_state)) {
		control |= __set_ctrl_super(chan, L2CAP_SUPER_RNR);
		l2cap_send_sframe(chan, control);
		set_bit(CONN_RNR_SENT, &chan->conn_state);
	}

	if (test_bit(CONN_REMOTE_BUSY, &chan->conn_state))
		l2cap_retransmit_frames(chan);

	l2cap_ertm_send(chan);

	if (!test_bit(CONN_LOCAL_BUSY, &chan->conn_state) &&
			chan->frames_sent == 0) {
		control |= __set_ctrl_super(chan, L2CAP_SUPER_RR);
		l2cap_send_sframe(chan, control);
	}
}

static int l2cap_add_to_srej_queue(struct l2cap_chan *chan, struct sk_buff *skb, u16 tx_seq, u8 sar)
{
	struct sk_buff *next_skb;
	int tx_seq_offset, next_tx_seq_offset;

	bt_cb(skb)->tx_seq = tx_seq;
	bt_cb(skb)->sar = sar;

	next_skb = skb_peek(&chan->srej_q);

	tx_seq_offset = __seq_offset(chan, tx_seq, chan->buffer_seq);

	while (next_skb) {
		if (bt_cb(next_skb)->tx_seq == tx_seq)
			return -EINVAL;

		next_tx_seq_offset = __seq_offset(chan,
				bt_cb(next_skb)->tx_seq, chan->buffer_seq);

		if (next_tx_seq_offset > tx_seq_offset) {
			__skb_queue_before(&chan->srej_q, next_skb, skb);
			return 0;
		}

		if (skb_queue_is_last(&chan->srej_q, next_skb))
			next_skb = NULL;
		else
			next_skb = skb_queue_next(&chan->srej_q, next_skb);
	}

	__skb_queue_tail(&chan->srej_q, skb);

	return 0;
}

static void append_skb_frag(struct sk_buff *skb,
			struct sk_buff *new_frag, struct sk_buff **last_frag)
{
	/* skb->len reflects data in skb as well as all fragments
	 * skb->data_len reflects only data in fragments
	 */
	if (!skb_has_frag_list(skb))
		skb_shinfo(skb)->frag_list = new_frag;

	new_frag->next = NULL;

	(*last_frag)->next = new_frag;
	*last_frag = new_frag;

	skb->len += new_frag->len;
	skb->data_len += new_frag->len;
	skb->truesize += new_frag->truesize;
}

static int l2cap_reassemble_sdu(struct l2cap_chan *chan, struct sk_buff *skb, u32 control)
{
	int err = -EINVAL;

	switch (__get_ctrl_sar(chan, control)) {
	case L2CAP_SAR_UNSEGMENTED:
		if (chan->sdu)
			break;

		err = chan->ops->recv(chan->data, skb);
		break;

	case L2CAP_SAR_START:
		if (chan->sdu)
			break;

		chan->sdu_len = get_unaligned_le16(skb->data);
		skb_pull(skb, L2CAP_SDULEN_SIZE);

		if (chan->sdu_len > chan->imtu) {
			err = -EMSGSIZE;
			break;
		}

		if (skb->len >= chan->sdu_len)
			break;

		chan->sdu = skb;
		chan->sdu_last_frag = skb;

		skb = NULL;
		err = 0;
		break;

	case L2CAP_SAR_CONTINUE:
		if (!chan->sdu)
			break;

		append_skb_frag(chan->sdu, skb,
				&chan->sdu_last_frag);
		skb = NULL;

		if (chan->sdu->len >= chan->sdu_len)
			break;

		err = 0;
		break;

	case L2CAP_SAR_END:
		if (!chan->sdu)
			break;

		append_skb_frag(chan->sdu, skb,
				&chan->sdu_last_frag);
		skb = NULL;

		if (chan->sdu->len != chan->sdu_len)
			break;

		err = chan->ops->recv(chan->data, chan->sdu);

		if (!err) {
			/* Reassembly complete */
			chan->sdu = NULL;
			chan->sdu_last_frag = NULL;
			chan->sdu_len = 0;
		}
		break;
	}

	if (err) {
		kfree_skb(skb);
		kfree_skb(chan->sdu);
		chan->sdu = NULL;
		chan->sdu_last_frag = NULL;
		chan->sdu_len = 0;
	}

	return err;
}

static void l2cap_ertm_enter_local_busy(struct l2cap_chan *chan)
{
	BT_DBG("chan %p, Enter local busy", chan);

	set_bit(CONN_LOCAL_BUSY, &chan->conn_state);

	__set_ack_timer(chan);
}

static void l2cap_ertm_exit_local_busy(struct l2cap_chan *chan)
{
	u32 control;

	if (!test_bit(CONN_RNR_SENT, &chan->conn_state))
		goto done;

	control = __set_reqseq(chan, chan->buffer_seq);
	control |= __set_ctrl_poll(chan);
	control |= __set_ctrl_super(chan, L2CAP_SUPER_RR);
	l2cap_send_sframe(chan, control);
	chan->retry_count = 1;

	__clear_retrans_timer(chan);
	__set_monitor_timer(chan);

	set_bit(CONN_WAIT_F, &chan->conn_state);

done:
	clear_bit(CONN_LOCAL_BUSY, &chan->conn_state);
	clear_bit(CONN_RNR_SENT, &chan->conn_state);

	BT_DBG("chan %p, Exit local busy", chan);
}

void l2cap_chan_busy(struct l2cap_chan *chan, int busy)
{
	if (chan->mode == L2CAP_MODE_ERTM) {
		if (busy)
			l2cap_ertm_enter_local_busy(chan);
		else
			l2cap_ertm_exit_local_busy(chan);
	}
}

static void l2cap_check_srej_gap(struct l2cap_chan *chan, u16 tx_seq)
{
	struct sk_buff *skb;
	u32 control;

	while ((skb = skb_peek(&chan->srej_q)) &&
			!test_bit(CONN_LOCAL_BUSY, &chan->conn_state)) {
		int err;

		if (bt_cb(skb)->tx_seq != tx_seq)
			break;

		skb = skb_dequeue(&chan->srej_q);
		control = __set_ctrl_sar(chan, bt_cb(skb)->sar);
		err = l2cap_reassemble_sdu(chan, skb, control);

		if (err < 0) {
			l2cap_send_disconn_req(chan->conn, chan, ECONNRESET);
			break;
		}

		chan->buffer_seq_srej = __next_seq(chan, chan->buffer_seq_srej);
		tx_seq = __next_seq(chan, tx_seq);
	}
}

static void l2cap_resend_srejframe(struct l2cap_chan *chan, u16 tx_seq)
{
	struct srej_list *l, *tmp;
	u32 control;

	list_for_each_entry_safe(l, tmp, &chan->srej_l, list) {
		if (l->tx_seq == tx_seq) {
			list_del(&l->list);
			kfree(l);
			return;
		}
		control = __set_ctrl_super(chan, L2CAP_SUPER_SREJ);
		control |= __set_reqseq(chan, l->tx_seq);
		l2cap_send_sframe(chan, control);
		list_del(&l->list);
		list_add_tail(&l->list, &chan->srej_l);
	}
}

static int l2cap_send_srejframe(struct l2cap_chan *chan, u16 tx_seq)
{
	struct srej_list *new;
	u32 control;

	while (tx_seq != chan->expected_tx_seq) {
		control = __set_ctrl_super(chan, L2CAP_SUPER_SREJ);
		control |= __set_reqseq(chan, chan->expected_tx_seq);
		l2cap_send_sframe(chan, control);

		new = kzalloc(sizeof(struct srej_list), GFP_ATOMIC);
		if (!new)
			return -ENOMEM;

		new->tx_seq = chan->expected_tx_seq;

		chan->expected_tx_seq = __next_seq(chan, chan->expected_tx_seq);

		list_add_tail(&new->list, &chan->srej_l);
	}

	chan->expected_tx_seq = __next_seq(chan, chan->expected_tx_seq);

	return 0;
}

static inline int l2cap_data_channel_iframe(struct l2cap_chan *chan, u32 rx_control, struct sk_buff *skb)
{
	u16 tx_seq = __get_txseq(chan, rx_control);
	u16 req_seq = __get_reqseq(chan, rx_control);
	u8 sar = __get_ctrl_sar(chan, rx_control);
	int tx_seq_offset, expected_tx_seq_offset;
	int num_to_ack = (chan->tx_win/6) + 1;
	int err = 0;

	BT_DBG("chan %p len %d tx_seq %d rx_control 0x%8.8x", chan, skb->len,
							tx_seq, rx_control);

	if (__is_ctrl_final(chan, rx_control) &&
			test_bit(CONN_WAIT_F, &chan->conn_state)) {
		__clear_monitor_timer(chan);
		if (chan->unacked_frames > 0)
			__set_retrans_timer(chan);
		clear_bit(CONN_WAIT_F, &chan->conn_state);
	}

	chan->expected_ack_seq = req_seq;
	l2cap_drop_acked_frames(chan);

	tx_seq_offset = __seq_offset(chan, tx_seq, chan->buffer_seq);

	/* invalid tx_seq */
	if (tx_seq_offset >= chan->tx_win) {
		l2cap_send_disconn_req(chan->conn, chan, ECONNRESET);
		goto drop;
	}

	if (test_bit(CONN_LOCAL_BUSY, &chan->conn_state)) {
		if (!test_bit(CONN_RNR_SENT, &chan->conn_state))
			l2cap_send_ack(chan);
		goto drop;
	}

	if (tx_seq == chan->expected_tx_seq)
		goto expected;

	if (test_bit(CONN_SREJ_SENT, &chan->conn_state)) {
		struct srej_list *first;

		first = list_first_entry(&chan->srej_l,
				struct srej_list, list);
		if (tx_seq == first->tx_seq) {
			l2cap_add_to_srej_queue(chan, skb, tx_seq, sar);
			l2cap_check_srej_gap(chan, tx_seq);

			list_del(&first->list);
			kfree(first);

			if (list_empty(&chan->srej_l)) {
				chan->buffer_seq = chan->buffer_seq_srej;
				clear_bit(CONN_SREJ_SENT, &chan->conn_state);
				l2cap_send_ack(chan);
				BT_DBG("chan %p, Exit SREJ_SENT", chan);
			}
		} else {
			struct srej_list *l;

			/* duplicated tx_seq */
			if (l2cap_add_to_srej_queue(chan, skb, tx_seq, sar) < 0)
				goto drop;

			list_for_each_entry(l, &chan->srej_l, list) {
				if (l->tx_seq == tx_seq) {
					l2cap_resend_srejframe(chan, tx_seq);
					return 0;
				}
			}

			err = l2cap_send_srejframe(chan, tx_seq);
			if (err < 0) {
				l2cap_send_disconn_req(chan->conn, chan, -err);
				return err;
			}
		}
	} else {
		expected_tx_seq_offset = __seq_offset(chan,
				chan->expected_tx_seq, chan->buffer_seq);

		/* duplicated tx_seq */
		if (tx_seq_offset < expected_tx_seq_offset)
			goto drop;

		set_bit(CONN_SREJ_SENT, &chan->conn_state);

		BT_DBG("chan %p, Enter SREJ", chan);

		INIT_LIST_HEAD(&chan->srej_l);
		chan->buffer_seq_srej = chan->buffer_seq;

		__skb_queue_head_init(&chan->srej_q);
		l2cap_add_to_srej_queue(chan, skb, tx_seq, sar);

		/* Set P-bit only if there are some I-frames to ack. */
		if (__clear_ack_timer(chan))
			set_bit(CONN_SEND_PBIT, &chan->conn_state);

		err = l2cap_send_srejframe(chan, tx_seq);
		if (err < 0) {
			l2cap_send_disconn_req(chan->conn, chan, -err);
			return err;
		}
	}
	return 0;

expected:
	chan->expected_tx_seq = __next_seq(chan, chan->expected_tx_seq);

	if (test_bit(CONN_SREJ_SENT, &chan->conn_state)) {
		bt_cb(skb)->tx_seq = tx_seq;
		bt_cb(skb)->sar = sar;
		__skb_queue_tail(&chan->srej_q, skb);
		return 0;
	}

	err = l2cap_reassemble_sdu(chan, skb, rx_control);
	chan->buffer_seq = __next_seq(chan, chan->buffer_seq);

	if (err < 0) {
		l2cap_send_disconn_req(chan->conn, chan, ECONNRESET);
		return err;
	}

	if (__is_ctrl_final(chan, rx_control)) {
		if (!test_and_clear_bit(CONN_REJ_ACT, &chan->conn_state))
			l2cap_retransmit_frames(chan);
	}


	chan->num_acked = (chan->num_acked + 1) % num_to_ack;
	if (chan->num_acked == num_to_ack - 1)
		l2cap_send_ack(chan);
	else
		__set_ack_timer(chan);

	return 0;

drop:
	kfree_skb(skb);
	return 0;
}

static inline void l2cap_data_channel_rrframe(struct l2cap_chan *chan, u32 rx_control)
{
	BT_DBG("chan %p, req_seq %d ctrl 0x%8.8x", chan,
				__get_reqseq(chan, rx_control), rx_control);

	chan->expected_ack_seq = __get_reqseq(chan, rx_control);
	l2cap_drop_acked_frames(chan);

	if (__is_ctrl_poll(chan, rx_control)) {
		set_bit(CONN_SEND_FBIT, &chan->conn_state);
		if (test_bit(CONN_SREJ_SENT, &chan->conn_state)) {
			if (test_bit(CONN_REMOTE_BUSY, &chan->conn_state) &&
					(chan->unacked_frames > 0))
				__set_retrans_timer(chan);

			clear_bit(CONN_REMOTE_BUSY, &chan->conn_state);
			l2cap_send_srejtail(chan);
		} else {
			l2cap_send_i_or_rr_or_rnr(chan);
		}

	} else if (__is_ctrl_final(chan, rx_control)) {
		clear_bit(CONN_REMOTE_BUSY, &chan->conn_state);

		if (!test_and_clear_bit(CONN_REJ_ACT, &chan->conn_state))
			l2cap_retransmit_frames(chan);

	} else {
		if (test_bit(CONN_REMOTE_BUSY, &chan->conn_state) &&
				(chan->unacked_frames > 0))
			__set_retrans_timer(chan);

		clear_bit(CONN_REMOTE_BUSY, &chan->conn_state);
		if (test_bit(CONN_SREJ_SENT, &chan->conn_state))
			l2cap_send_ack(chan);
		else
			l2cap_ertm_send(chan);
	}
}

static inline void l2cap_data_channel_rejframe(struct l2cap_chan *chan, u32 rx_control)
{
	u16 tx_seq = __get_reqseq(chan, rx_control);

	BT_DBG("chan %p, req_seq %d ctrl 0x%8.8x", chan, tx_seq, rx_control);

	clear_bit(CONN_REMOTE_BUSY, &chan->conn_state);

	chan->expected_ack_seq = tx_seq;
	l2cap_drop_acked_frames(chan);

	if (__is_ctrl_final(chan, rx_control)) {
		if (!test_and_clear_bit(CONN_REJ_ACT, &chan->conn_state))
			l2cap_retransmit_frames(chan);
	} else {
		l2cap_retransmit_frames(chan);

		if (test_bit(CONN_WAIT_F, &chan->conn_state))
			set_bit(CONN_REJ_ACT, &chan->conn_state);
	}
}
static inline void l2cap_data_channel_srejframe(struct l2cap_chan *chan, u32 rx_control)
{
	u16 tx_seq = __get_reqseq(chan, rx_control);

	BT_DBG("chan %p, req_seq %d ctrl 0x%8.8x", chan, tx_seq, rx_control);

	clear_bit(CONN_REMOTE_BUSY, &chan->conn_state);

	if (__is_ctrl_poll(chan, rx_control)) {
		chan->expected_ack_seq = tx_seq;
		l2cap_drop_acked_frames(chan);

		set_bit(CONN_SEND_FBIT, &chan->conn_state);
		l2cap_retransmit_one_frame(chan, tx_seq);

		l2cap_ertm_send(chan);

		if (test_bit(CONN_WAIT_F, &chan->conn_state)) {
			chan->srej_save_reqseq = tx_seq;
			set_bit(CONN_SREJ_ACT, &chan->conn_state);
		}
	} else if (__is_ctrl_final(chan, rx_control)) {
		if (test_bit(CONN_SREJ_ACT, &chan->conn_state) &&
				chan->srej_save_reqseq == tx_seq)
			clear_bit(CONN_SREJ_ACT, &chan->conn_state);
		else
			l2cap_retransmit_one_frame(chan, tx_seq);
	} else {
		l2cap_retransmit_one_frame(chan, tx_seq);
		if (test_bit(CONN_WAIT_F, &chan->conn_state)) {
			chan->srej_save_reqseq = tx_seq;
			set_bit(CONN_SREJ_ACT, &chan->conn_state);
		}
	}
}

static inline void l2cap_data_channel_rnrframe(struct l2cap_chan *chan, u32 rx_control)
{
	u16 tx_seq = __get_reqseq(chan, rx_control);

	BT_DBG("chan %p, req_seq %d ctrl 0x%8.8x", chan, tx_seq, rx_control);

	set_bit(CONN_REMOTE_BUSY, &chan->conn_state);
	chan->expected_ack_seq = tx_seq;
	l2cap_drop_acked_frames(chan);

	if (__is_ctrl_poll(chan, rx_control))
		set_bit(CONN_SEND_FBIT, &chan->conn_state);

	if (!test_bit(CONN_SREJ_SENT, &chan->conn_state)) {
		__clear_retrans_timer(chan);
		if (__is_ctrl_poll(chan, rx_control))
			l2cap_send_rr_or_rnr(chan, L2CAP_CTRL_FINAL);
		return;
	}

	if (__is_ctrl_poll(chan, rx_control)) {
		l2cap_send_srejtail(chan);
	} else {
		rx_control = __set_ctrl_super(chan, L2CAP_SUPER_RR);
		l2cap_send_sframe(chan, rx_control);
	}
}

static inline int l2cap_data_channel_sframe(struct l2cap_chan *chan, u32 rx_control, struct sk_buff *skb)
{
	BT_DBG("chan %p rx_control 0x%8.8x len %d", chan, rx_control, skb->len);

	if (__is_ctrl_final(chan, rx_control) &&
			test_bit(CONN_WAIT_F, &chan->conn_state)) {
		__clear_monitor_timer(chan);
		if (chan->unacked_frames > 0)
			__set_retrans_timer(chan);
		clear_bit(CONN_WAIT_F, &chan->conn_state);
	}

	switch (__get_ctrl_super(chan, rx_control)) {
	case L2CAP_SUPER_RR:
		l2cap_data_channel_rrframe(chan, rx_control);
		break;

	case L2CAP_SUPER_REJ:
		l2cap_data_channel_rejframe(chan, rx_control);
		break;

	case L2CAP_SUPER_SREJ:
		l2cap_data_channel_srejframe(chan, rx_control);
		break;

	case L2CAP_SUPER_RNR:
		l2cap_data_channel_rnrframe(chan, rx_control);
		break;
	}

	kfree_skb(skb);
	return 0;
}

int l2cap_ertm_data_rcv(struct l2cap_chan *chan, struct sk_buff *skb)
{
	u32 control;
	u16 req_seq;
	int len, next_tx_seq_offset, req_seq_offset;

	control = __get_control(chan, skb->data);
	skb_pull(skb, __ctrl_size(chan));
	len = skb->len;

	/*
	 * We can just drop the corrupted I-frame here.
	 * Receiver will miss it and start proper recovery
	 * procedures and ask retransmission.
	 */
	if (l2cap_check_fcs(chan, skb))
		goto drop;

	if (__is_sar_start(chan, control) && !__is_sframe(chan, control))
		len -= L2CAP_SDULEN_SIZE;

	if (chan->fcs == L2CAP_FCS_CRC16)
		len -= L2CAP_FCS_SIZE;

	if (len > chan->mps) {
		l2cap_send_disconn_req(chan->conn, chan, ECONNRESET);
		goto drop;
	}

	req_seq = __get_reqseq(chan, control);

	req_seq_offset = __seq_offset(chan, req_seq, chan->expected_ack_seq);

	next_tx_seq_offset = __seq_offset(chan, chan->next_tx_seq,
						chan->expected_ack_seq);

	/* check for invalid req-seq */
	if (req_seq_offset > next_tx_seq_offset) {
		l2cap_send_disconn_req(chan->conn, chan, ECONNRESET);
		goto drop;
	}

	if (!__is_sframe(chan, control)) {
		if (len < 0) {
			l2cap_send_disconn_req(chan->conn, chan, ECONNRESET);
			goto drop;
		}

		l2cap_data_channel_iframe(chan, control, skb);
	} else {
		if (len != 0) {
			BT_ERR("%d", len);
			l2cap_send_disconn_req(chan->conn, chan, ECONNRESET);
			goto drop;
		}

		l2cap_data_channel_sframe(chan, control, skb);
	}

	return 0;

drop:
	kfree_skb(skb);
	return 0;
}

static inline int l2cap_data_channel(struct l2cap_conn *conn, u16 cid, struct sk_buff *skb)
{
	struct l2cap_chan *chan;
	struct sock *sk = NULL;
	u32 control;
	u16 tx_seq;
	int len;

	chan = l2cap_get_chan_by_scid(conn, cid);
	if (!chan) {
		BT_DBG("unknown cid 0x%4.4x", cid);
		goto drop;
	}

	sk = chan->sk;

	BT_DBG("chan %p, len %d", chan, skb->len);

	if (chan->state != BT_CONNECTED)
		goto drop;

	switch (chan->mode) {
	case L2CAP_MODE_BASIC:
		/* If socket recv buffers overflows we drop data here
		 * which is *bad* because L2CAP has to be reliable.
		 * But we don't have any other choice. L2CAP doesn't
		 * provide flow control mechanism. */

		if (chan->imtu < skb->len)
			goto drop;

		if (!chan->ops->recv(chan->data, skb))
			goto done;
		break;

	case L2CAP_MODE_ERTM:
		l2cap_ertm_data_rcv(chan, skb);

		goto done;

	case L2CAP_MODE_STREAMING:
		control = __get_control(chan, skb->data);
		skb_pull(skb, __ctrl_size(chan));
		len = skb->len;

		if (l2cap_check_fcs(chan, skb))
			goto drop;

		if (__is_sar_start(chan, control))
			len -= L2CAP_SDULEN_SIZE;

		if (chan->fcs == L2CAP_FCS_CRC16)
			len -= L2CAP_FCS_SIZE;

		if (len > chan->mps || len < 0 || __is_sframe(chan, control))
			goto drop;

		tx_seq = __get_txseq(chan, control);

		if (chan->expected_tx_seq != tx_seq) {
			/* Frame(s) missing - must discard partial SDU */
			kfree_skb(chan->sdu);
			chan->sdu = NULL;
			chan->sdu_last_frag = NULL;
			chan->sdu_len = 0;

			/* TODO: Notify userland of missing data */
		}

		chan->expected_tx_seq = __next_seq(chan, tx_seq);

		if (l2cap_reassemble_sdu(chan, skb, control) == -EMSGSIZE)
			l2cap_send_disconn_req(chan->conn, chan, ECONNRESET);

		goto done;

	default:
		BT_DBG("chan %p: bad mode 0x%2.2x", chan, chan->mode);
		break;
	}

drop:
	kfree_skb(skb);

done:
	if (sk)
		release_sock(sk);

	return 0;
}

static inline int l2cap_conless_channel(struct l2cap_conn *conn, __le16 psm, struct sk_buff *skb)
{
	struct sock *sk = NULL;
	struct l2cap_chan *chan;

	chan = l2cap_global_chan_by_psm(0, psm, conn->src);
	if (!chan)
		goto drop;

	sk = chan->sk;

	lock_sock(sk);

	BT_DBG("sk %p, len %d", sk, skb->len);

	if (chan->state != BT_BOUND && chan->state != BT_CONNECTED)
		goto drop;

	if (chan->imtu < skb->len)
		goto drop;

	if (!chan->ops->recv(chan->data, skb))
		goto done;

drop:
	kfree_skb(skb);

done:
	if (sk)
		release_sock(sk);
	return 0;
}

static inline int l2cap_att_channel(struct l2cap_conn *conn, __le16 cid, struct sk_buff *skb)
{
	struct sock *sk = NULL;
	struct l2cap_chan *chan;

	chan = l2cap_global_chan_by_scid(0, cid, conn->src);
	if (!chan)
		goto drop;

	sk = chan->sk;

	lock_sock(sk);

	BT_DBG("sk %p, len %d", sk, skb->len);

	if (chan->state != BT_BOUND && chan->state != BT_CONNECTED)
		goto drop;

	if (chan->imtu < skb->len)
		goto drop;

	if (!chan->ops->recv(chan->data, skb))
		goto done;

drop:
	kfree_skb(skb);

done:
	if (sk)
		release_sock(sk);
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

	case L2CAP_CID_LE_DATA:
		l2cap_att_channel(conn, cid, skb);
		break;

	case L2CAP_CID_SMP:
		if (smp_sig_channel(conn, skb))
			l2cap_conn_del(conn->hcon, EACCES);
		break;

	default:
		l2cap_data_channel(conn, cid, skb);
		break;
	}
}

/* ---- L2CAP interface with lower layer (HCI) ---- */

int l2cap_connect_ind(struct hci_dev *hdev, bdaddr_t *bdaddr)
{
	int exact = 0, lm1 = 0, lm2 = 0;
	struct l2cap_chan *c;

	BT_DBG("hdev %s, bdaddr %s", hdev->name, batostr(bdaddr));

	/* Find listening sockets and check their link_mode */
	read_lock(&chan_list_lock);
	list_for_each_entry(c, &chan_list, global_l) {
		struct sock *sk = c->sk;

		if (c->state != BT_LISTEN)
			continue;

		if (!bacmp(&bt_sk(sk)->src, &hdev->bdaddr)) {
			lm1 |= HCI_LM_ACCEPT;
			if (test_bit(FLAG_ROLE_SWITCH, &c->flags))
				lm1 |= HCI_LM_MASTER;
			exact++;
		} else if (!bacmp(&bt_sk(sk)->src, BDADDR_ANY)) {
			lm2 |= HCI_LM_ACCEPT;
			if (test_bit(FLAG_ROLE_SWITCH, &c->flags))
				lm2 |= HCI_LM_MASTER;
		}
	}
	read_unlock(&chan_list_lock);

	return exact ? lm1 : lm2;
}

int l2cap_connect_cfm(struct hci_conn *hcon, u8 status)
{
	struct l2cap_conn *conn;

	BT_DBG("hcon %p bdaddr %s status %d", hcon, batostr(&hcon->dst), status);

	if (!status) {
		conn = l2cap_conn_add(hcon, status);
		if (conn)
			l2cap_conn_ready(conn);
	} else
		l2cap_conn_del(hcon, bt_to_errno(status));

	return 0;
}

int l2cap_disconn_ind(struct hci_conn *hcon)
{
	struct l2cap_conn *conn = hcon->l2cap_data;

	BT_DBG("hcon %p", hcon);

	if (!conn)
		return HCI_ERROR_REMOTE_USER_TERM;
	return conn->disc_reason;
}

int l2cap_disconn_cfm(struct hci_conn *hcon, u8 reason)
{
	BT_DBG("hcon %p reason %d", hcon, reason);

	l2cap_conn_del(hcon, bt_to_errno(reason));
	return 0;
}

static inline void l2cap_check_encryption(struct l2cap_chan *chan, u8 encrypt)
{
	if (chan->chan_type != L2CAP_CHAN_CONN_ORIENTED)
		return;

	if (encrypt == 0x00) {
		if (chan->sec_level == BT_SECURITY_MEDIUM) {
			__clear_chan_timer(chan);
			__set_chan_timer(chan,
					msecs_to_jiffies(L2CAP_ENC_TIMEOUT));
		} else if (chan->sec_level == BT_SECURITY_HIGH)
			l2cap_chan_close(chan, ECONNREFUSED);
	} else {
		if (chan->sec_level == BT_SECURITY_MEDIUM)
			__clear_chan_timer(chan);
	}
}

int l2cap_security_cfm(struct hci_conn *hcon, u8 status, u8 encrypt)
{
	struct l2cap_conn *conn = hcon->l2cap_data;
	struct l2cap_chan *chan;

	if (!conn)
		return 0;

	BT_DBG("conn %p", conn);

	if (hcon->type == LE_LINK) {
		smp_distribute_keys(conn, 0);
		__cancel_delayed_work(&conn->security_timer);
	}

	rcu_read_lock();

	list_for_each_entry_rcu(chan, &conn->chan_l, list) {
		struct sock *sk = chan->sk;

		bh_lock_sock(sk);

		BT_DBG("chan->scid %d", chan->scid);

		if (chan->scid == L2CAP_CID_LE_DATA) {
			if (!status && encrypt) {
				chan->sec_level = hcon->sec_level;
				l2cap_chan_ready(sk);
			}

			bh_unlock_sock(sk);
			continue;
		}

		if (test_bit(CONF_CONNECT_PEND, &chan->conf_state)) {
			bh_unlock_sock(sk);
			continue;
		}

		if (!status && (chan->state == BT_CONNECTED ||
						chan->state == BT_CONFIG)) {
			l2cap_check_encryption(chan, encrypt);
			bh_unlock_sock(sk);
			continue;
		}

		if (chan->state == BT_CONNECT) {
			if (!status) {
				struct l2cap_conn_req req;
				req.scid = cpu_to_le16(chan->scid);
				req.psm  = chan->psm;

				chan->ident = l2cap_get_ident(conn);
				set_bit(CONF_CONNECT_PEND, &chan->conf_state);

				l2cap_send_cmd(conn, chan->ident,
					L2CAP_CONN_REQ, sizeof(req), &req);
			} else {
				__clear_chan_timer(chan);
				__set_chan_timer(chan,
					msecs_to_jiffies(L2CAP_DISC_TIMEOUT));
			}
		} else if (chan->state == BT_CONNECT2) {
			struct l2cap_conn_rsp rsp;
			__u16 res, stat;

			if (!status) {
				if (bt_sk(sk)->defer_setup) {
					struct sock *parent = bt_sk(sk)->parent;
					res = L2CAP_CR_PEND;
					stat = L2CAP_CS_AUTHOR_PEND;
					if (parent)
						parent->sk_data_ready(parent, 0);
				} else {
					l2cap_state_change(chan, BT_CONFIG);
					res = L2CAP_CR_SUCCESS;
					stat = L2CAP_CS_NO_INFO;
				}
			} else {
				l2cap_state_change(chan, BT_DISCONN);
				__set_chan_timer(chan,
					msecs_to_jiffies(L2CAP_DISC_TIMEOUT));
				res = L2CAP_CR_SEC_BLOCK;
				stat = L2CAP_CS_NO_INFO;
			}

			rsp.scid   = cpu_to_le16(chan->dcid);
			rsp.dcid   = cpu_to_le16(chan->scid);
			rsp.result = cpu_to_le16(res);
			rsp.status = cpu_to_le16(stat);
			l2cap_send_cmd(conn, chan->ident, L2CAP_CONN_RSP,
							sizeof(rsp), &rsp);
		}

		bh_unlock_sock(sk);
	}

	rcu_read_unlock();

	return 0;
}

int l2cap_recv_acldata(struct hci_conn *hcon, struct sk_buff *skb, u16 flags)
{
	struct l2cap_conn *conn = hcon->l2cap_data;

	if (!conn)
		conn = l2cap_conn_add(hcon, 0);

	if (!conn)
		goto drop;

	BT_DBG("conn %p len %d flags 0x%x", conn, skb->len, flags);

	if (!(flags & ACL_CONT)) {
		struct l2cap_hdr *hdr;
		struct l2cap_chan *chan;
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

		chan = l2cap_get_chan_by_scid(conn, cid);

		if (chan && chan->sk) {
			struct sock *sk = chan->sk;

			if (chan->imtu < len - L2CAP_HDR_SIZE) {
				BT_ERR("Frame exceeding recv MTU (len %d, "
							"MTU %d)", len,
							chan->imtu);
				release_sock(sk);
				l2cap_conn_unreliable(conn, ECOMM);
				goto drop;
			}
			release_sock(sk);
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

static int l2cap_debugfs_show(struct seq_file *f, void *p)
{
	struct l2cap_chan *c;

	read_lock(&chan_list_lock);

	list_for_each_entry(c, &chan_list, global_l) {
		struct sock *sk = c->sk;

		seq_printf(f, "%s %s %d %d 0x%4.4x 0x%4.4x %d %d %d %d\n",
					batostr(&bt_sk(sk)->src),
					batostr(&bt_sk(sk)->dst),
					c->state, __le16_to_cpu(c->psm),
					c->scid, c->dcid, c->imtu, c->omtu,
					c->sec_level, c->mode);
	}

	read_unlock(&chan_list_lock);

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

int __init l2cap_init(void)
{
	int err;

	err = l2cap_init_sockets();
	if (err < 0)
		return err;

	if (bt_debugfs) {
		l2cap_debugfs = debugfs_create_file("l2cap", 0444,
					bt_debugfs, NULL, &l2cap_debugfs_fops);
		if (!l2cap_debugfs)
			BT_ERR("Failed to create L2CAP debug file");
	}

	return 0;
}

void l2cap_exit(void)
{
	debugfs_remove(l2cap_debugfs);
	l2cap_cleanup_sockets();
}

module_param(disable_ertm, bool, 0644);
MODULE_PARM_DESC(disable_ertm, "Disable enhanced retransmission mode");
