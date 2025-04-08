/*
   BlueZ - Bluetooth protocol stack for Linux
   Copyright (C) 2000-2001 Qualcomm Incorporated
   Copyright (C) 2009-2010 Gustavo F. Padovan <gustavo@padovan.org>
   Copyright (C) 2010 Google Inc.
   Copyright (C) 2011 ProFUSION Embedded Systems
   Copyright (c) 2012 Code Aurora Forum.  All rights reserved.

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

#include <linux/debugfs.h>
#include <linux/crc16.h>
#include <linux/filter.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include <net/bluetooth/l2cap.h>

#include "smp.h"

#define LE_FLOWCTL_MAX_CREDITS 65535

bool disable_ertm;
bool enable_ecred = IS_ENABLED(CONFIG_BT_LE_L2CAP_ECRED);

static u32 l2cap_feat_mask = L2CAP_FEAT_FIXED_CHAN | L2CAP_FEAT_UCD;

static LIST_HEAD(chan_list);
static DEFINE_RWLOCK(chan_list_lock);

static struct sk_buff *l2cap_build_cmd(struct l2cap_conn *conn,
				       u8 code, u8 ident, u16 dlen, void *data);
static void l2cap_send_cmd(struct l2cap_conn *conn, u8 ident, u8 code, u16 len,
			   void *data);
static int l2cap_build_conf_req(struct l2cap_chan *chan, void *data, size_t data_size);
static void l2cap_send_disconn_req(struct l2cap_chan *chan, int err);

static void l2cap_tx(struct l2cap_chan *chan, struct l2cap_ctrl *control,
		     struct sk_buff_head *skbs, u8 event);
static void l2cap_retrans_timeout(struct work_struct *work);
static void l2cap_monitor_timeout(struct work_struct *work);
static void l2cap_ack_timeout(struct work_struct *work);

static inline u8 bdaddr_type(u8 link_type, u8 bdaddr_type)
{
	if (link_type == LE_LINK) {
		if (bdaddr_type == ADDR_LE_DEV_PUBLIC)
			return BDADDR_LE_PUBLIC;
		else
			return BDADDR_LE_RANDOM;
	}

	return BDADDR_BREDR;
}

static inline u8 bdaddr_src_type(struct hci_conn *hcon)
{
	return bdaddr_type(hcon->type, hcon->src_type);
}

static inline u8 bdaddr_dst_type(struct hci_conn *hcon)
{
	return bdaddr_type(hcon->type, hcon->dst_type);
}

/* ---- L2CAP channels ---- */

static struct l2cap_chan *__l2cap_get_chan_by_dcid(struct l2cap_conn *conn,
						   u16 cid)
{
	struct l2cap_chan *c;

	list_for_each_entry(c, &conn->chan_l, list) {
		if (c->dcid == cid)
			return c;
	}
	return NULL;
}

static struct l2cap_chan *__l2cap_get_chan_by_scid(struct l2cap_conn *conn,
						   u16 cid)
{
	struct l2cap_chan *c;

	list_for_each_entry(c, &conn->chan_l, list) {
		if (c->scid == cid)
			return c;
	}
	return NULL;
}

/* Find channel with given SCID.
 * Returns a reference locked channel.
 */
static struct l2cap_chan *l2cap_get_chan_by_scid(struct l2cap_conn *conn,
						 u16 cid)
{
	struct l2cap_chan *c;

	c = __l2cap_get_chan_by_scid(conn, cid);
	if (c) {
		/* Only lock if chan reference is not 0 */
		c = l2cap_chan_hold_unless_zero(c);
		if (c)
			l2cap_chan_lock(c);
	}

	return c;
}

/* Find channel with given DCID.
 * Returns a reference locked channel.
 */
static struct l2cap_chan *l2cap_get_chan_by_dcid(struct l2cap_conn *conn,
						 u16 cid)
{
	struct l2cap_chan *c;

	c = __l2cap_get_chan_by_dcid(conn, cid);
	if (c) {
		/* Only lock if chan reference is not 0 */
		c = l2cap_chan_hold_unless_zero(c);
		if (c)
			l2cap_chan_lock(c);
	}

	return c;
}

static struct l2cap_chan *__l2cap_get_chan_by_ident(struct l2cap_conn *conn,
						    u8 ident)
{
	struct l2cap_chan *c;

	list_for_each_entry(c, &conn->chan_l, list) {
		if (c->ident == ident)
			return c;
	}
	return NULL;
}

static struct l2cap_chan *__l2cap_global_chan_by_addr(__le16 psm, bdaddr_t *src,
						      u8 src_type)
{
	struct l2cap_chan *c;

	list_for_each_entry(c, &chan_list, global_l) {
		if (src_type == BDADDR_BREDR && c->src_type != BDADDR_BREDR)
			continue;

		if (src_type != BDADDR_BREDR && c->src_type == BDADDR_BREDR)
			continue;

		if (c->sport == psm && !bacmp(&c->src, src))
			return c;
	}
	return NULL;
}

int l2cap_add_psm(struct l2cap_chan *chan, bdaddr_t *src, __le16 psm)
{
	int err;

	write_lock(&chan_list_lock);

	if (psm && __l2cap_global_chan_by_addr(psm, src, chan->src_type)) {
		err = -EADDRINUSE;
		goto done;
	}

	if (psm) {
		chan->psm = psm;
		chan->sport = psm;
		err = 0;
	} else {
		u16 p, start, end, incr;

		if (chan->src_type == BDADDR_BREDR) {
			start = L2CAP_PSM_DYN_START;
			end = L2CAP_PSM_AUTO_END;
			incr = 2;
		} else {
			start = L2CAP_PSM_LE_DYN_START;
			end = L2CAP_PSM_LE_DYN_END;
			incr = 1;
		}

		err = -EINVAL;
		for (p = start; p <= end; p += incr)
			if (!__l2cap_global_chan_by_addr(cpu_to_le16(p), src,
							 chan->src_type)) {
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
EXPORT_SYMBOL_GPL(l2cap_add_psm);

int l2cap_add_scid(struct l2cap_chan *chan,  __u16 scid)
{
	write_lock(&chan_list_lock);

	/* Override the defaults (which are for conn-oriented) */
	chan->omtu = L2CAP_DEFAULT_MTU;
	chan->chan_type = L2CAP_CHAN_FIXED;

	chan->scid = scid;

	write_unlock(&chan_list_lock);

	return 0;
}

static u16 l2cap_alloc_cid(struct l2cap_conn *conn)
{
	u16 cid, dyn_end;

	if (conn->hcon->type == LE_LINK)
		dyn_end = L2CAP_CID_LE_DYN_END;
	else
		dyn_end = L2CAP_CID_DYN_END;

	for (cid = L2CAP_CID_DYN_START; cid <= dyn_end; cid++) {
		if (!__l2cap_get_chan_by_scid(conn, cid))
			return cid;
	}

	return 0;
}

static void l2cap_state_change(struct l2cap_chan *chan, int state)
{
	BT_DBG("chan %p %s -> %s", chan, state_to_string(chan->state),
	       state_to_string(state));

	chan->state = state;
	chan->ops->state_change(chan, state, 0);
}

static inline void l2cap_state_change_and_error(struct l2cap_chan *chan,
						int state, int err)
{
	chan->state = state;
	chan->ops->state_change(chan, chan->state, err);
}

static inline void l2cap_chan_set_err(struct l2cap_chan *chan, int err)
{
	chan->ops->state_change(chan, chan->state, err);
}

static void __set_retrans_timer(struct l2cap_chan *chan)
{
	if (!delayed_work_pending(&chan->monitor_timer) &&
	    chan->retrans_timeout) {
		l2cap_set_timer(chan, &chan->retrans_timer,
				secs_to_jiffies(chan->retrans_timeout));
	}
}

static void __set_monitor_timer(struct l2cap_chan *chan)
{
	__clear_retrans_timer(chan);
	if (chan->monitor_timeout) {
		l2cap_set_timer(chan, &chan->monitor_timer,
				secs_to_jiffies(chan->monitor_timeout));
	}
}

static struct sk_buff *l2cap_ertm_seq_in_queue(struct sk_buff_head *head,
					       u16 seq)
{
	struct sk_buff *skb;

	skb_queue_walk(head, skb) {
		if (bt_cb(skb)->l2cap.txseq == seq)
			return skb;
	}

	return NULL;
}

/* ---- L2CAP sequence number lists ---- */

/* For ERTM, ordered lists of sequence numbers must be tracked for
 * SREJ requests that are received and for frames that are to be
 * retransmitted. These seq_list functions implement a singly-linked
 * list in an array, where membership in the list can also be checked
 * in constant time. Items can also be added to the tail of the list
 * and removed from the head in constant time, without further memory
 * allocs or frees.
 */

static int l2cap_seq_list_init(struct l2cap_seq_list *seq_list, u16 size)
{
	size_t alloc_size, i;

	/* Allocated size is a power of 2 to map sequence numbers
	 * (which may be up to 14 bits) in to a smaller array that is
	 * sized for the negotiated ERTM transmit windows.
	 */
	alloc_size = roundup_pow_of_two(size);

	seq_list->list = kmalloc_array(alloc_size, sizeof(u16), GFP_KERNEL);
	if (!seq_list->list)
		return -ENOMEM;

	seq_list->mask = alloc_size - 1;
	seq_list->head = L2CAP_SEQ_LIST_CLEAR;
	seq_list->tail = L2CAP_SEQ_LIST_CLEAR;
	for (i = 0; i < alloc_size; i++)
		seq_list->list[i] = L2CAP_SEQ_LIST_CLEAR;

	return 0;
}

static inline void l2cap_seq_list_free(struct l2cap_seq_list *seq_list)
{
	kfree(seq_list->list);
}

static inline bool l2cap_seq_list_contains(struct l2cap_seq_list *seq_list,
					   u16 seq)
{
	/* Constant-time check for list membership */
	return seq_list->list[seq & seq_list->mask] != L2CAP_SEQ_LIST_CLEAR;
}

static inline u16 l2cap_seq_list_pop(struct l2cap_seq_list *seq_list)
{
	u16 seq = seq_list->head;
	u16 mask = seq_list->mask;

	seq_list->head = seq_list->list[seq & mask];
	seq_list->list[seq & mask] = L2CAP_SEQ_LIST_CLEAR;

	if (seq_list->head == L2CAP_SEQ_LIST_TAIL) {
		seq_list->head = L2CAP_SEQ_LIST_CLEAR;
		seq_list->tail = L2CAP_SEQ_LIST_CLEAR;
	}

	return seq;
}

static void l2cap_seq_list_clear(struct l2cap_seq_list *seq_list)
{
	u16 i;

	if (seq_list->head == L2CAP_SEQ_LIST_CLEAR)
		return;

	for (i = 0; i <= seq_list->mask; i++)
		seq_list->list[i] = L2CAP_SEQ_LIST_CLEAR;

	seq_list->head = L2CAP_SEQ_LIST_CLEAR;
	seq_list->tail = L2CAP_SEQ_LIST_CLEAR;
}

static void l2cap_seq_list_append(struct l2cap_seq_list *seq_list, u16 seq)
{
	u16 mask = seq_list->mask;

	/* All appends happen in constant time */

	if (seq_list->list[seq & mask] != L2CAP_SEQ_LIST_CLEAR)
		return;

	if (seq_list->tail == L2CAP_SEQ_LIST_CLEAR)
		seq_list->head = seq;
	else
		seq_list->list[seq_list->tail & mask] = seq;

	seq_list->tail = seq;
	seq_list->list[seq & mask] = L2CAP_SEQ_LIST_TAIL;
}

static void l2cap_chan_timeout(struct work_struct *work)
{
	struct l2cap_chan *chan = container_of(work, struct l2cap_chan,
					       chan_timer.work);
	struct l2cap_conn *conn = chan->conn;
	int reason;

	BT_DBG("chan %p state %s", chan, state_to_string(chan->state));

	if (!conn)
		return;

	mutex_lock(&conn->lock);
	/* __set_chan_timer() calls l2cap_chan_hold(chan) while scheduling
	 * this work. No need to call l2cap_chan_hold(chan) here again.
	 */
	l2cap_chan_lock(chan);

	if (chan->state == BT_CONNECTED || chan->state == BT_CONFIG)
		reason = ECONNREFUSED;
	else if (chan->state == BT_CONNECT &&
		 chan->sec_level != BT_SECURITY_SDP)
		reason = ECONNREFUSED;
	else
		reason = ETIMEDOUT;

	l2cap_chan_close(chan, reason);

	chan->ops->close(chan);

	l2cap_chan_unlock(chan);
	l2cap_chan_put(chan);

	mutex_unlock(&conn->lock);
}

struct l2cap_chan *l2cap_chan_create(void)
{
	struct l2cap_chan *chan;

	chan = kzalloc(sizeof(*chan), GFP_ATOMIC);
	if (!chan)
		return NULL;

	skb_queue_head_init(&chan->tx_q);
	skb_queue_head_init(&chan->srej_q);
	mutex_init(&chan->lock);

	/* Set default lock nesting level */
	atomic_set(&chan->nesting, L2CAP_NESTING_NORMAL);

	/* Available receive buffer space is initially unknown */
	chan->rx_avail = -1;

	write_lock(&chan_list_lock);
	list_add(&chan->global_l, &chan_list);
	write_unlock(&chan_list_lock);

	INIT_DELAYED_WORK(&chan->chan_timer, l2cap_chan_timeout);
	INIT_DELAYED_WORK(&chan->retrans_timer, l2cap_retrans_timeout);
	INIT_DELAYED_WORK(&chan->monitor_timer, l2cap_monitor_timeout);
	INIT_DELAYED_WORK(&chan->ack_timer, l2cap_ack_timeout);

	chan->state = BT_OPEN;

	kref_init(&chan->kref);

	/* This flag is cleared in l2cap_chan_ready() */
	set_bit(CONF_NOT_COMPLETE, &chan->conf_state);

	BT_DBG("chan %p", chan);

	return chan;
}
EXPORT_SYMBOL_GPL(l2cap_chan_create);

static void l2cap_chan_destroy(struct kref *kref)
{
	struct l2cap_chan *chan = container_of(kref, struct l2cap_chan, kref);

	BT_DBG("chan %p", chan);

	write_lock(&chan_list_lock);
	list_del(&chan->global_l);
	write_unlock(&chan_list_lock);

	kfree(chan);
}

void l2cap_chan_hold(struct l2cap_chan *c)
{
	BT_DBG("chan %p orig refcnt %u", c, kref_read(&c->kref));

	kref_get(&c->kref);
}

struct l2cap_chan *l2cap_chan_hold_unless_zero(struct l2cap_chan *c)
{
	BT_DBG("chan %p orig refcnt %u", c, kref_read(&c->kref));

	if (!kref_get_unless_zero(&c->kref))
		return NULL;

	return c;
}

void l2cap_chan_put(struct l2cap_chan *c)
{
	BT_DBG("chan %p orig refcnt %u", c, kref_read(&c->kref));

	kref_put(&c->kref, l2cap_chan_destroy);
}
EXPORT_SYMBOL_GPL(l2cap_chan_put);

void l2cap_chan_set_defaults(struct l2cap_chan *chan)
{
	chan->fcs  = L2CAP_FCS_CRC16;
	chan->max_tx = L2CAP_DEFAULT_MAX_TX;
	chan->tx_win = L2CAP_DEFAULT_TX_WINDOW;
	chan->tx_win_max = L2CAP_DEFAULT_TX_WINDOW;
	chan->remote_max_tx = chan->max_tx;
	chan->remote_tx_win = chan->tx_win;
	chan->ack_win = L2CAP_DEFAULT_TX_WINDOW;
	chan->sec_level = BT_SECURITY_LOW;
	chan->flush_to = L2CAP_DEFAULT_FLUSH_TO;
	chan->retrans_timeout = L2CAP_DEFAULT_RETRANS_TO;
	chan->monitor_timeout = L2CAP_DEFAULT_MONITOR_TO;

	chan->conf_state = 0;
	set_bit(CONF_NOT_COMPLETE, &chan->conf_state);

	set_bit(FLAG_FORCE_ACTIVE, &chan->flags);
}
EXPORT_SYMBOL_GPL(l2cap_chan_set_defaults);

static __u16 l2cap_le_rx_credits(struct l2cap_chan *chan)
{
	size_t sdu_len = chan->sdu ? chan->sdu->len : 0;

	if (chan->mps == 0)
		return 0;

	/* If we don't know the available space in the receiver buffer, give
	 * enough credits for a full packet.
	 */
	if (chan->rx_avail == -1)
		return (chan->imtu / chan->mps) + 1;

	/* If we know how much space is available in the receive buffer, give
	 * out as many credits as would fill the buffer.
	 */
	if (chan->rx_avail <= sdu_len)
		return 0;

	return DIV_ROUND_UP(chan->rx_avail - sdu_len, chan->mps);
}

static void l2cap_le_flowctl_init(struct l2cap_chan *chan, u16 tx_credits)
{
	chan->sdu = NULL;
	chan->sdu_last_frag = NULL;
	chan->sdu_len = 0;
	chan->tx_credits = tx_credits;
	/* Derive MPS from connection MTU to stop HCI fragmentation */
	chan->mps = min_t(u16, chan->imtu, chan->conn->mtu - L2CAP_HDR_SIZE);
	chan->rx_credits = l2cap_le_rx_credits(chan);

	skb_queue_head_init(&chan->tx_q);
}

static void l2cap_ecred_init(struct l2cap_chan *chan, u16 tx_credits)
{
	l2cap_le_flowctl_init(chan, tx_credits);

	/* L2CAP implementations shall support a minimum MPS of 64 octets */
	if (chan->mps < L2CAP_ECRED_MIN_MPS) {
		chan->mps = L2CAP_ECRED_MIN_MPS;
		chan->rx_credits = l2cap_le_rx_credits(chan);
	}
}

void __l2cap_chan_add(struct l2cap_conn *conn, struct l2cap_chan *chan)
{
	BT_DBG("conn %p, psm 0x%2.2x, dcid 0x%4.4x", conn,
	       __le16_to_cpu(chan->psm), chan->dcid);

	conn->disc_reason = HCI_ERROR_REMOTE_USER_TERM;

	chan->conn = conn;

	switch (chan->chan_type) {
	case L2CAP_CHAN_CONN_ORIENTED:
		/* Alloc CID for connection-oriented socket */
		chan->scid = l2cap_alloc_cid(conn);
		if (conn->hcon->type == ACL_LINK)
			chan->omtu = L2CAP_DEFAULT_MTU;
		break;

	case L2CAP_CHAN_CONN_LESS:
		/* Connectionless socket */
		chan->scid = L2CAP_CID_CONN_LESS;
		chan->dcid = L2CAP_CID_CONN_LESS;
		chan->omtu = L2CAP_DEFAULT_MTU;
		break;

	case L2CAP_CHAN_FIXED:
		/* Caller will set CID and CID specific MTU values */
		break;

	default:
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
	chan->local_flush_to	= L2CAP_EFS_DEFAULT_FLUSH_TO;

	l2cap_chan_hold(chan);

	/* Only keep a reference for fixed channels if they requested it */
	if (chan->chan_type != L2CAP_CHAN_FIXED ||
	    test_bit(FLAG_HOLD_HCI_CONN, &chan->flags))
		hci_conn_hold(conn->hcon);

	/* Append to the list since the order matters for ECRED */
	list_add_tail(&chan->list, &conn->chan_l);
}

void l2cap_chan_add(struct l2cap_conn *conn, struct l2cap_chan *chan)
{
	mutex_lock(&conn->lock);
	__l2cap_chan_add(conn, chan);
	mutex_unlock(&conn->lock);
}

void l2cap_chan_del(struct l2cap_chan *chan, int err)
{
	struct l2cap_conn *conn = chan->conn;

	__clear_chan_timer(chan);

	BT_DBG("chan %p, conn %p, err %d, state %s", chan, conn, err,
	       state_to_string(chan->state));

	chan->ops->teardown(chan, err);

	if (conn) {
		/* Delete from channel list */
		list_del(&chan->list);

		l2cap_chan_put(chan);

		chan->conn = NULL;

		/* Reference was only held for non-fixed channels or
		 * fixed channels that explicitly requested it using the
		 * FLAG_HOLD_HCI_CONN flag.
		 */
		if (chan->chan_type != L2CAP_CHAN_FIXED ||
		    test_bit(FLAG_HOLD_HCI_CONN, &chan->flags))
			hci_conn_drop(conn->hcon);
	}

	if (test_bit(CONF_NOT_COMPLETE, &chan->conf_state))
		return;

	switch (chan->mode) {
	case L2CAP_MODE_BASIC:
		break;

	case L2CAP_MODE_LE_FLOWCTL:
	case L2CAP_MODE_EXT_FLOWCTL:
		skb_queue_purge(&chan->tx_q);
		break;

	case L2CAP_MODE_ERTM:
		__clear_retrans_timer(chan);
		__clear_monitor_timer(chan);
		__clear_ack_timer(chan);

		skb_queue_purge(&chan->srej_q);

		l2cap_seq_list_free(&chan->srej_list);
		l2cap_seq_list_free(&chan->retrans_list);
		fallthrough;

	case L2CAP_MODE_STREAMING:
		skb_queue_purge(&chan->tx_q);
		break;
	}
}
EXPORT_SYMBOL_GPL(l2cap_chan_del);

static void __l2cap_chan_list_id(struct l2cap_conn *conn, u16 id,
				 l2cap_chan_func_t func, void *data)
{
	struct l2cap_chan *chan, *l;

	list_for_each_entry_safe(chan, l, &conn->chan_l, list) {
		if (chan->ident == id)
			func(chan, data);
	}
}

static void __l2cap_chan_list(struct l2cap_conn *conn, l2cap_chan_func_t func,
			      void *data)
{
	struct l2cap_chan *chan;

	list_for_each_entry(chan, &conn->chan_l, list) {
		func(chan, data);
	}
}

void l2cap_chan_list(struct l2cap_conn *conn, l2cap_chan_func_t func,
		     void *data)
{
	if (!conn)
		return;

	mutex_lock(&conn->lock);
	__l2cap_chan_list(conn, func, data);
	mutex_unlock(&conn->lock);
}

EXPORT_SYMBOL_GPL(l2cap_chan_list);

static void l2cap_conn_update_id_addr(struct work_struct *work)
{
	struct l2cap_conn *conn = container_of(work, struct l2cap_conn,
					       id_addr_timer.work);
	struct hci_conn *hcon = conn->hcon;
	struct l2cap_chan *chan;

	mutex_lock(&conn->lock);

	list_for_each_entry(chan, &conn->chan_l, list) {
		l2cap_chan_lock(chan);
		bacpy(&chan->dst, &hcon->dst);
		chan->dst_type = bdaddr_dst_type(hcon);
		l2cap_chan_unlock(chan);
	}

	mutex_unlock(&conn->lock);
}

static void l2cap_chan_le_connect_reject(struct l2cap_chan *chan)
{
	struct l2cap_conn *conn = chan->conn;
	struct l2cap_le_conn_rsp rsp;
	u16 result;

	if (test_bit(FLAG_DEFER_SETUP, &chan->flags))
		result = L2CAP_CR_LE_AUTHORIZATION;
	else
		result = L2CAP_CR_LE_BAD_PSM;

	l2cap_state_change(chan, BT_DISCONN);

	rsp.dcid    = cpu_to_le16(chan->scid);
	rsp.mtu     = cpu_to_le16(chan->imtu);
	rsp.mps     = cpu_to_le16(chan->mps);
	rsp.credits = cpu_to_le16(chan->rx_credits);
	rsp.result  = cpu_to_le16(result);

	l2cap_send_cmd(conn, chan->ident, L2CAP_LE_CONN_RSP, sizeof(rsp),
		       &rsp);
}

static void l2cap_chan_ecred_connect_reject(struct l2cap_chan *chan)
{
	l2cap_state_change(chan, BT_DISCONN);

	__l2cap_ecred_conn_rsp_defer(chan);
}

static void l2cap_chan_connect_reject(struct l2cap_chan *chan)
{
	struct l2cap_conn *conn = chan->conn;
	struct l2cap_conn_rsp rsp;
	u16 result;

	if (test_bit(FLAG_DEFER_SETUP, &chan->flags))
		result = L2CAP_CR_SEC_BLOCK;
	else
		result = L2CAP_CR_BAD_PSM;

	l2cap_state_change(chan, BT_DISCONN);

	rsp.scid   = cpu_to_le16(chan->dcid);
	rsp.dcid   = cpu_to_le16(chan->scid);
	rsp.result = cpu_to_le16(result);
	rsp.status = cpu_to_le16(L2CAP_CS_NO_INFO);

	l2cap_send_cmd(conn, chan->ident, L2CAP_CONN_RSP, sizeof(rsp), &rsp);
}

void l2cap_chan_close(struct l2cap_chan *chan, int reason)
{
	struct l2cap_conn *conn = chan->conn;

	BT_DBG("chan %p state %s", chan, state_to_string(chan->state));

	switch (chan->state) {
	case BT_LISTEN:
		chan->ops->teardown(chan, 0);
		break;

	case BT_CONNECTED:
	case BT_CONFIG:
		if (chan->chan_type == L2CAP_CHAN_CONN_ORIENTED) {
			__set_chan_timer(chan, chan->ops->get_sndtimeo(chan));
			l2cap_send_disconn_req(chan, reason);
		} else
			l2cap_chan_del(chan, reason);
		break;

	case BT_CONNECT2:
		if (chan->chan_type == L2CAP_CHAN_CONN_ORIENTED) {
			if (conn->hcon->type == ACL_LINK)
				l2cap_chan_connect_reject(chan);
			else if (conn->hcon->type == LE_LINK) {
				switch (chan->mode) {
				case L2CAP_MODE_LE_FLOWCTL:
					l2cap_chan_le_connect_reject(chan);
					break;
				case L2CAP_MODE_EXT_FLOWCTL:
					l2cap_chan_ecred_connect_reject(chan);
					return;
				}
			}
		}

		l2cap_chan_del(chan, reason);
		break;

	case BT_CONNECT:
	case BT_DISCONN:
		l2cap_chan_del(chan, reason);
		break;

	default:
		chan->ops->teardown(chan, 0);
		break;
	}
}
EXPORT_SYMBOL(l2cap_chan_close);

static inline u8 l2cap_get_auth_type(struct l2cap_chan *chan)
{
	switch (chan->chan_type) {
	case L2CAP_CHAN_RAW:
		switch (chan->sec_level) {
		case BT_SECURITY_HIGH:
		case BT_SECURITY_FIPS:
			return HCI_AT_DEDICATED_BONDING_MITM;
		case BT_SECURITY_MEDIUM:
			return HCI_AT_DEDICATED_BONDING;
		default:
			return HCI_AT_NO_BONDING;
		}
		break;
	case L2CAP_CHAN_CONN_LESS:
		if (chan->psm == cpu_to_le16(L2CAP_PSM_3DSP)) {
			if (chan->sec_level == BT_SECURITY_LOW)
				chan->sec_level = BT_SECURITY_SDP;
		}
		if (chan->sec_level == BT_SECURITY_HIGH ||
		    chan->sec_level == BT_SECURITY_FIPS)
			return HCI_AT_NO_BONDING_MITM;
		else
			return HCI_AT_NO_BONDING;
		break;
	case L2CAP_CHAN_CONN_ORIENTED:
		if (chan->psm == cpu_to_le16(L2CAP_PSM_SDP)) {
			if (chan->sec_level == BT_SECURITY_LOW)
				chan->sec_level = BT_SECURITY_SDP;

			if (chan->sec_level == BT_SECURITY_HIGH ||
			    chan->sec_level == BT_SECURITY_FIPS)
				return HCI_AT_NO_BONDING_MITM;
			else
				return HCI_AT_NO_BONDING;
		}
		fallthrough;

	default:
		switch (chan->sec_level) {
		case BT_SECURITY_HIGH:
		case BT_SECURITY_FIPS:
			return HCI_AT_GENERAL_BONDING_MITM;
		case BT_SECURITY_MEDIUM:
			return HCI_AT_GENERAL_BONDING;
		default:
			return HCI_AT_NO_BONDING;
		}
		break;
	}
}

/* Service level security */
int l2cap_chan_check_security(struct l2cap_chan *chan, bool initiator)
{
	struct l2cap_conn *conn = chan->conn;
	__u8 auth_type;

	if (conn->hcon->type == LE_LINK)
		return smp_conn_security(conn->hcon, chan->sec_level);

	auth_type = l2cap_get_auth_type(chan);

	return hci_conn_security(conn->hcon, chan->sec_level, auth_type,
				 initiator);
}

static u8 l2cap_get_ident(struct l2cap_conn *conn)
{
	u8 id;

	/* Get next available identificator.
	 *    1 - 128 are used by kernel.
	 *  129 - 199 are reserved.
	 *  200 - 254 are used by utilities like l2ping, etc.
	 */

	mutex_lock(&conn->ident_lock);

	if (++conn->tx_ident > 128)
		conn->tx_ident = 1;

	id = conn->tx_ident;

	mutex_unlock(&conn->ident_lock);

	return id;
}

static void l2cap_send_acl(struct l2cap_conn *conn, struct sk_buff *skb,
			   u8 flags)
{
	/* Check if the hcon still valid before attempting to send */
	if (hci_conn_valid(conn->hcon->hdev, conn->hcon))
		hci_send_acl(conn->hchan, skb, flags);
	else
		kfree_skb(skb);
}

static void l2cap_send_cmd(struct l2cap_conn *conn, u8 ident, u8 code, u16 len,
			   void *data)
{
	struct sk_buff *skb = l2cap_build_cmd(conn, code, ident, len, data);
	u8 flags;

	BT_DBG("code 0x%2.2x", code);

	if (!skb)
		return;

	/* Use NO_FLUSH if supported or we have an LE link (which does
	 * not support auto-flushing packets) */
	if (lmp_no_flush_capable(conn->hcon->hdev) ||
	    conn->hcon->type == LE_LINK)
		flags = ACL_START_NO_FLUSH;
	else
		flags = ACL_START;

	bt_cb(skb)->force_active = BT_POWER_FORCE_ACTIVE_ON;
	skb->priority = HCI_PRIO_MAX;

	l2cap_send_acl(conn, skb, flags);
}

static void l2cap_do_send(struct l2cap_chan *chan, struct sk_buff *skb)
{
	struct hci_conn *hcon = chan->conn->hcon;
	u16 flags;

	BT_DBG("chan %p, skb %p len %d priority %u", chan, skb, skb->len,
	       skb->priority);

	/* Use NO_FLUSH for LE links (where this is the only option) or
	 * if the BR/EDR link supports it and flushing has not been
	 * explicitly requested (through FLAG_FLUSHABLE).
	 */
	if (hcon->type == LE_LINK ||
	    (!test_bit(FLAG_FLUSHABLE, &chan->flags) &&
	     lmp_no_flush_capable(hcon->hdev)))
		flags = ACL_START_NO_FLUSH;
	else
		flags = ACL_START;

	bt_cb(skb)->force_active = test_bit(FLAG_FORCE_ACTIVE, &chan->flags);
	hci_send_acl(chan->conn->hchan, skb, flags);
}

static void __unpack_enhanced_control(u16 enh, struct l2cap_ctrl *control)
{
	control->reqseq = (enh & L2CAP_CTRL_REQSEQ) >> L2CAP_CTRL_REQSEQ_SHIFT;
	control->final = (enh & L2CAP_CTRL_FINAL) >> L2CAP_CTRL_FINAL_SHIFT;

	if (enh & L2CAP_CTRL_FRAME_TYPE) {
		/* S-Frame */
		control->sframe = 1;
		control->poll = (enh & L2CAP_CTRL_POLL) >> L2CAP_CTRL_POLL_SHIFT;
		control->super = (enh & L2CAP_CTRL_SUPERVISE) >> L2CAP_CTRL_SUPER_SHIFT;

		control->sar = 0;
		control->txseq = 0;
	} else {
		/* I-Frame */
		control->sframe = 0;
		control->sar = (enh & L2CAP_CTRL_SAR) >> L2CAP_CTRL_SAR_SHIFT;
		control->txseq = (enh & L2CAP_CTRL_TXSEQ) >> L2CAP_CTRL_TXSEQ_SHIFT;

		control->poll = 0;
		control->super = 0;
	}
}

static void __unpack_extended_control(u32 ext, struct l2cap_ctrl *control)
{
	control->reqseq = (ext & L2CAP_EXT_CTRL_REQSEQ) >> L2CAP_EXT_CTRL_REQSEQ_SHIFT;
	control->final = (ext & L2CAP_EXT_CTRL_FINAL) >> L2CAP_EXT_CTRL_FINAL_SHIFT;

	if (ext & L2CAP_EXT_CTRL_FRAME_TYPE) {
		/* S-Frame */
		control->sframe = 1;
		control->poll = (ext & L2CAP_EXT_CTRL_POLL) >> L2CAP_EXT_CTRL_POLL_SHIFT;
		control->super = (ext & L2CAP_EXT_CTRL_SUPERVISE) >> L2CAP_EXT_CTRL_SUPER_SHIFT;

		control->sar = 0;
		control->txseq = 0;
	} else {
		/* I-Frame */
		control->sframe = 0;
		control->sar = (ext & L2CAP_EXT_CTRL_SAR) >> L2CAP_EXT_CTRL_SAR_SHIFT;
		control->txseq = (ext & L2CAP_EXT_CTRL_TXSEQ) >> L2CAP_EXT_CTRL_TXSEQ_SHIFT;

		control->poll = 0;
		control->super = 0;
	}
}

static inline void __unpack_control(struct l2cap_chan *chan,
				    struct sk_buff *skb)
{
	if (test_bit(FLAG_EXT_CTRL, &chan->flags)) {
		__unpack_extended_control(get_unaligned_le32(skb->data),
					  &bt_cb(skb)->l2cap);
		skb_pull(skb, L2CAP_EXT_CTRL_SIZE);
	} else {
		__unpack_enhanced_control(get_unaligned_le16(skb->data),
					  &bt_cb(skb)->l2cap);
		skb_pull(skb, L2CAP_ENH_CTRL_SIZE);
	}
}

static u32 __pack_extended_control(struct l2cap_ctrl *control)
{
	u32 packed;

	packed = control->reqseq << L2CAP_EXT_CTRL_REQSEQ_SHIFT;
	packed |= control->final << L2CAP_EXT_CTRL_FINAL_SHIFT;

	if (control->sframe) {
		packed |= control->poll << L2CAP_EXT_CTRL_POLL_SHIFT;
		packed |= control->super << L2CAP_EXT_CTRL_SUPER_SHIFT;
		packed |= L2CAP_EXT_CTRL_FRAME_TYPE;
	} else {
		packed |= control->sar << L2CAP_EXT_CTRL_SAR_SHIFT;
		packed |= control->txseq << L2CAP_EXT_CTRL_TXSEQ_SHIFT;
	}

	return packed;
}

static u16 __pack_enhanced_control(struct l2cap_ctrl *control)
{
	u16 packed;

	packed = control->reqseq << L2CAP_CTRL_REQSEQ_SHIFT;
	packed |= control->final << L2CAP_CTRL_FINAL_SHIFT;

	if (control->sframe) {
		packed |= control->poll << L2CAP_CTRL_POLL_SHIFT;
		packed |= control->super << L2CAP_CTRL_SUPER_SHIFT;
		packed |= L2CAP_CTRL_FRAME_TYPE;
	} else {
		packed |= control->sar << L2CAP_CTRL_SAR_SHIFT;
		packed |= control->txseq << L2CAP_CTRL_TXSEQ_SHIFT;
	}

	return packed;
}

static inline void __pack_control(struct l2cap_chan *chan,
				  struct l2cap_ctrl *control,
				  struct sk_buff *skb)
{
	if (test_bit(FLAG_EXT_CTRL, &chan->flags)) {
		put_unaligned_le32(__pack_extended_control(control),
				   skb->data + L2CAP_HDR_SIZE);
	} else {
		put_unaligned_le16(__pack_enhanced_control(control),
				   skb->data + L2CAP_HDR_SIZE);
	}
}

static inline unsigned int __ertm_hdr_size(struct l2cap_chan *chan)
{
	if (test_bit(FLAG_EXT_CTRL, &chan->flags))
		return L2CAP_EXT_HDR_SIZE;
	else
		return L2CAP_ENH_HDR_SIZE;
}

static struct sk_buff *l2cap_create_sframe_pdu(struct l2cap_chan *chan,
					       u32 control)
{
	struct sk_buff *skb;
	struct l2cap_hdr *lh;
	int hlen = __ertm_hdr_size(chan);

	if (chan->fcs == L2CAP_FCS_CRC16)
		hlen += L2CAP_FCS_SIZE;

	skb = bt_skb_alloc(hlen, GFP_KERNEL);

	if (!skb)
		return ERR_PTR(-ENOMEM);

	lh = skb_put(skb, L2CAP_HDR_SIZE);
	lh->len = cpu_to_le16(hlen - L2CAP_HDR_SIZE);
	lh->cid = cpu_to_le16(chan->dcid);

	if (test_bit(FLAG_EXT_CTRL, &chan->flags))
		put_unaligned_le32(control, skb_put(skb, L2CAP_EXT_CTRL_SIZE));
	else
		put_unaligned_le16(control, skb_put(skb, L2CAP_ENH_CTRL_SIZE));

	if (chan->fcs == L2CAP_FCS_CRC16) {
		u16 fcs = crc16(0, (u8 *)skb->data, skb->len);
		put_unaligned_le16(fcs, skb_put(skb, L2CAP_FCS_SIZE));
	}

	skb->priority = HCI_PRIO_MAX;
	return skb;
}

static void l2cap_send_sframe(struct l2cap_chan *chan,
			      struct l2cap_ctrl *control)
{
	struct sk_buff *skb;
	u32 control_field;

	BT_DBG("chan %p, control %p", chan, control);

	if (!control->sframe)
		return;

	if (test_and_clear_bit(CONN_SEND_FBIT, &chan->conn_state) &&
	    !control->poll)
		control->final = 1;

	if (control->super == L2CAP_SUPER_RR)
		clear_bit(CONN_RNR_SENT, &chan->conn_state);
	else if (control->super == L2CAP_SUPER_RNR)
		set_bit(CONN_RNR_SENT, &chan->conn_state);

	if (control->super != L2CAP_SUPER_SREJ) {
		chan->last_acked_seq = control->reqseq;
		__clear_ack_timer(chan);
	}

	BT_DBG("reqseq %d, final %d, poll %d, super %d", control->reqseq,
	       control->final, control->poll, control->super);

	if (test_bit(FLAG_EXT_CTRL, &chan->flags))
		control_field = __pack_extended_control(control);
	else
		control_field = __pack_enhanced_control(control);

	skb = l2cap_create_sframe_pdu(chan, control_field);
	if (!IS_ERR(skb))
		l2cap_do_send(chan, skb);
}

static void l2cap_send_rr_or_rnr(struct l2cap_chan *chan, bool poll)
{
	struct l2cap_ctrl control;

	BT_DBG("chan %p, poll %d", chan, poll);

	memset(&control, 0, sizeof(control));
	control.sframe = 1;
	control.poll = poll;

	if (test_bit(CONN_LOCAL_BUSY, &chan->conn_state))
		control.super = L2CAP_SUPER_RNR;
	else
		control.super = L2CAP_SUPER_RR;

	control.reqseq = chan->buffer_seq;
	l2cap_send_sframe(chan, &control);
}

static inline int __l2cap_no_conn_pending(struct l2cap_chan *chan)
{
	if (chan->chan_type != L2CAP_CHAN_CONN_ORIENTED)
		return true;

	return !test_bit(CONF_CONNECT_PEND, &chan->conf_state);
}

void l2cap_send_conn_req(struct l2cap_chan *chan)
{
	struct l2cap_conn *conn = chan->conn;
	struct l2cap_conn_req req;

	req.scid = cpu_to_le16(chan->scid);
	req.psm  = chan->psm;

	chan->ident = l2cap_get_ident(conn);

	set_bit(CONF_CONNECT_PEND, &chan->conf_state);

	l2cap_send_cmd(conn, chan->ident, L2CAP_CONN_REQ, sizeof(req), &req);
}

static void l2cap_chan_ready(struct l2cap_chan *chan)
{
	/* The channel may have already been flagged as connected in
	 * case of receiving data before the L2CAP info req/rsp
	 * procedure is complete.
	 */
	if (chan->state == BT_CONNECTED)
		return;

	/* This clears all conf flags, including CONF_NOT_COMPLETE */
	chan->conf_state = 0;
	__clear_chan_timer(chan);

	switch (chan->mode) {
	case L2CAP_MODE_LE_FLOWCTL:
	case L2CAP_MODE_EXT_FLOWCTL:
		if (!chan->tx_credits)
			chan->ops->suspend(chan);
		break;
	}

	chan->state = BT_CONNECTED;

	chan->ops->ready(chan);
}

static void l2cap_le_connect(struct l2cap_chan *chan)
{
	struct l2cap_conn *conn = chan->conn;
	struct l2cap_le_conn_req req;

	if (test_and_set_bit(FLAG_LE_CONN_REQ_SENT, &chan->flags))
		return;

	if (!chan->imtu)
		chan->imtu = chan->conn->mtu;

	l2cap_le_flowctl_init(chan, 0);

	memset(&req, 0, sizeof(req));
	req.psm     = chan->psm;
	req.scid    = cpu_to_le16(chan->scid);
	req.mtu     = cpu_to_le16(chan->imtu);
	req.mps     = cpu_to_le16(chan->mps);
	req.credits = cpu_to_le16(chan->rx_credits);

	chan->ident = l2cap_get_ident(conn);

	l2cap_send_cmd(conn, chan->ident, L2CAP_LE_CONN_REQ,
		       sizeof(req), &req);
}

struct l2cap_ecred_conn_data {
	struct {
		struct l2cap_ecred_conn_req_hdr req;
		__le16 scid[5];
	} __packed pdu;
	struct l2cap_chan *chan;
	struct pid *pid;
	int count;
};

static void l2cap_ecred_defer_connect(struct l2cap_chan *chan, void *data)
{
	struct l2cap_ecred_conn_data *conn = data;
	struct pid *pid;

	if (chan == conn->chan)
		return;

	if (!test_and_clear_bit(FLAG_DEFER_SETUP, &chan->flags))
		return;

	pid = chan->ops->get_peer_pid(chan);

	/* Only add deferred channels with the same PID/PSM */
	if (conn->pid != pid || chan->psm != conn->chan->psm || chan->ident ||
	    chan->mode != L2CAP_MODE_EXT_FLOWCTL || chan->state != BT_CONNECT)
		return;

	if (test_and_set_bit(FLAG_ECRED_CONN_REQ_SENT, &chan->flags))
		return;

	l2cap_ecred_init(chan, 0);

	/* Set the same ident so we can match on the rsp */
	chan->ident = conn->chan->ident;

	/* Include all channels deferred */
	conn->pdu.scid[conn->count] = cpu_to_le16(chan->scid);

	conn->count++;
}

static void l2cap_ecred_connect(struct l2cap_chan *chan)
{
	struct l2cap_conn *conn = chan->conn;
	struct l2cap_ecred_conn_data data;

	if (test_bit(FLAG_DEFER_SETUP, &chan->flags))
		return;

	if (test_and_set_bit(FLAG_ECRED_CONN_REQ_SENT, &chan->flags))
		return;

	l2cap_ecred_init(chan, 0);

	memset(&data, 0, sizeof(data));
	data.pdu.req.psm     = chan->psm;
	data.pdu.req.mtu     = cpu_to_le16(chan->imtu);
	data.pdu.req.mps     = cpu_to_le16(chan->mps);
	data.pdu.req.credits = cpu_to_le16(chan->rx_credits);
	data.pdu.scid[0]     = cpu_to_le16(chan->scid);

	chan->ident = l2cap_get_ident(conn);

	data.count = 1;
	data.chan = chan;
	data.pid = chan->ops->get_peer_pid(chan);

	__l2cap_chan_list(conn, l2cap_ecred_defer_connect, &data);

	l2cap_send_cmd(conn, chan->ident, L2CAP_ECRED_CONN_REQ,
		       sizeof(data.pdu.req) + data.count * sizeof(__le16),
		       &data.pdu);
}

static void l2cap_le_start(struct l2cap_chan *chan)
{
	struct l2cap_conn *conn = chan->conn;

	if (!smp_conn_security(conn->hcon, chan->sec_level))
		return;

	if (!chan->psm) {
		l2cap_chan_ready(chan);
		return;
	}

	if (chan->state == BT_CONNECT) {
		if (chan->mode == L2CAP_MODE_EXT_FLOWCTL)
			l2cap_ecred_connect(chan);
		else
			l2cap_le_connect(chan);
	}
}

static void l2cap_start_connection(struct l2cap_chan *chan)
{
	if (chan->conn->hcon->type == LE_LINK) {
		l2cap_le_start(chan);
	} else {
		l2cap_send_conn_req(chan);
	}
}

static void l2cap_request_info(struct l2cap_conn *conn)
{
	struct l2cap_info_req req;

	if (conn->info_state & L2CAP_INFO_FEAT_MASK_REQ_SENT)
		return;

	req.type = cpu_to_le16(L2CAP_IT_FEAT_MASK);

	conn->info_state |= L2CAP_INFO_FEAT_MASK_REQ_SENT;
	conn->info_ident = l2cap_get_ident(conn);

	schedule_delayed_work(&conn->info_timer, L2CAP_INFO_TIMEOUT);

	l2cap_send_cmd(conn, conn->info_ident, L2CAP_INFO_REQ,
		       sizeof(req), &req);
}

static bool l2cap_check_enc_key_size(struct hci_conn *hcon)
{
	/* The minimum encryption key size needs to be enforced by the
	 * host stack before establishing any L2CAP connections. The
	 * specification in theory allows a minimum of 1, but to align
	 * BR/EDR and LE transports, a minimum of 7 is chosen.
	 *
	 * This check might also be called for unencrypted connections
	 * that have no key size requirements. Ensure that the link is
	 * actually encrypted before enforcing a key size.
	 */
	int min_key_size = hcon->hdev->min_enc_key_size;

	/* On FIPS security level, key size must be 16 bytes */
	if (hcon->sec_level == BT_SECURITY_FIPS)
		min_key_size = 16;

	return (!test_bit(HCI_CONN_ENCRYPT, &hcon->flags) ||
		hcon->enc_key_size >= min_key_size);
}

static void l2cap_do_start(struct l2cap_chan *chan)
{
	struct l2cap_conn *conn = chan->conn;

	if (conn->hcon->type == LE_LINK) {
		l2cap_le_start(chan);
		return;
	}

	if (!(conn->info_state & L2CAP_INFO_FEAT_MASK_REQ_SENT)) {
		l2cap_request_info(conn);
		return;
	}

	if (!(conn->info_state & L2CAP_INFO_FEAT_MASK_REQ_DONE))
		return;

	if (!l2cap_chan_check_security(chan, true) ||
	    !__l2cap_no_conn_pending(chan))
		return;

	if (l2cap_check_enc_key_size(conn->hcon))
		l2cap_start_connection(chan);
	else
		__set_chan_timer(chan, L2CAP_DISC_TIMEOUT);
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

static void l2cap_send_disconn_req(struct l2cap_chan *chan, int err)
{
	struct l2cap_conn *conn = chan->conn;
	struct l2cap_disconn_req req;

	if (!conn)
		return;

	if (chan->mode == L2CAP_MODE_ERTM && chan->state == BT_CONNECTED) {
		__clear_retrans_timer(chan);
		__clear_monitor_timer(chan);
		__clear_ack_timer(chan);
	}

	req.dcid = cpu_to_le16(chan->dcid);
	req.scid = cpu_to_le16(chan->scid);
	l2cap_send_cmd(conn, l2cap_get_ident(conn), L2CAP_DISCONN_REQ,
		       sizeof(req), &req);

	l2cap_state_change_and_error(chan, BT_DISCONN, err);
}

/* ---- L2CAP connections ---- */
static void l2cap_conn_start(struct l2cap_conn *conn)
{
	struct l2cap_chan *chan, *tmp;

	BT_DBG("conn %p", conn);

	list_for_each_entry_safe(chan, tmp, &conn->chan_l, list) {
		l2cap_chan_lock(chan);

		if (chan->chan_type != L2CAP_CHAN_CONN_ORIENTED) {
			l2cap_chan_ready(chan);
			l2cap_chan_unlock(chan);
			continue;
		}

		if (chan->state == BT_CONNECT) {
			if (!l2cap_chan_check_security(chan, true) ||
			    !__l2cap_no_conn_pending(chan)) {
				l2cap_chan_unlock(chan);
				continue;
			}

			if (!l2cap_mode_supported(chan->mode, conn->feat_mask)
			    && test_bit(CONF_STATE2_DEVICE,
					&chan->conf_state)) {
				l2cap_chan_close(chan, ECONNRESET);
				l2cap_chan_unlock(chan);
				continue;
			}

			if (l2cap_check_enc_key_size(conn->hcon))
				l2cap_start_connection(chan);
			else
				l2cap_chan_close(chan, ECONNREFUSED);

		} else if (chan->state == BT_CONNECT2) {
			struct l2cap_conn_rsp rsp;
			char buf[128];
			rsp.scid = cpu_to_le16(chan->dcid);
			rsp.dcid = cpu_to_le16(chan->scid);

			if (l2cap_chan_check_security(chan, false)) {
				if (test_bit(FLAG_DEFER_SETUP, &chan->flags)) {
					rsp.result = cpu_to_le16(L2CAP_CR_PEND);
					rsp.status = cpu_to_le16(L2CAP_CS_AUTHOR_PEND);
					chan->ops->defer(chan);

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
				l2cap_chan_unlock(chan);
				continue;
			}

			set_bit(CONF_REQ_SENT, &chan->conf_state);
			l2cap_send_cmd(conn, l2cap_get_ident(conn), L2CAP_CONF_REQ,
				       l2cap_build_conf_req(chan, buf, sizeof(buf)), buf);
			chan->num_conf_req++;
		}

		l2cap_chan_unlock(chan);
	}
}

static void l2cap_le_conn_ready(struct l2cap_conn *conn)
{
	struct hci_conn *hcon = conn->hcon;
	struct hci_dev *hdev = hcon->hdev;

	BT_DBG("%s conn %p", hdev->name, conn);

	/* For outgoing pairing which doesn't necessarily have an
	 * associated socket (e.g. mgmt_pair_device).
	 */
	if (hcon->out)
		smp_conn_security(hcon, hcon->pending_sec_level);

	/* For LE peripheral connections, make sure the connection interval
	 * is in the range of the minimum and maximum interval that has
	 * been configured for this connection. If not, then trigger
	 * the connection update procedure.
	 */
	if (hcon->role == HCI_ROLE_SLAVE &&
	    (hcon->le_conn_interval < hcon->le_conn_min_interval ||
	     hcon->le_conn_interval > hcon->le_conn_max_interval)) {
		struct l2cap_conn_param_update_req req;

		req.min = cpu_to_le16(hcon->le_conn_min_interval);
		req.max = cpu_to_le16(hcon->le_conn_max_interval);
		req.latency = cpu_to_le16(hcon->le_conn_latency);
		req.to_multiplier = cpu_to_le16(hcon->le_supv_timeout);

		l2cap_send_cmd(conn, l2cap_get_ident(conn),
			       L2CAP_CONN_PARAM_UPDATE_REQ, sizeof(req), &req);
	}
}

static void l2cap_conn_ready(struct l2cap_conn *conn)
{
	struct l2cap_chan *chan;
	struct hci_conn *hcon = conn->hcon;

	BT_DBG("conn %p", conn);

	if (hcon->type == ACL_LINK)
		l2cap_request_info(conn);

	mutex_lock(&conn->lock);

	list_for_each_entry(chan, &conn->chan_l, list) {

		l2cap_chan_lock(chan);

		if (hcon->type == LE_LINK) {
			l2cap_le_start(chan);
		} else if (chan->chan_type != L2CAP_CHAN_CONN_ORIENTED) {
			if (conn->info_state & L2CAP_INFO_FEAT_MASK_REQ_DONE)
				l2cap_chan_ready(chan);
		} else if (chan->state == BT_CONNECT) {
			l2cap_do_start(chan);
		}

		l2cap_chan_unlock(chan);
	}

	mutex_unlock(&conn->lock);

	if (hcon->type == LE_LINK)
		l2cap_le_conn_ready(conn);

	queue_work(hcon->hdev->workqueue, &conn->pending_rx_work);
}

/* Notify sockets that we cannot guaranty reliability anymore */
static void l2cap_conn_unreliable(struct l2cap_conn *conn, int err)
{
	struct l2cap_chan *chan;

	BT_DBG("conn %p", conn);

	list_for_each_entry(chan, &conn->chan_l, list) {
		if (test_bit(FLAG_FORCE_RELIABLE, &chan->flags))
			l2cap_chan_set_err(chan, err);
	}
}

static void l2cap_info_timeout(struct work_struct *work)
{
	struct l2cap_conn *conn = container_of(work, struct l2cap_conn,
					       info_timer.work);

	conn->info_state |= L2CAP_INFO_FEAT_MASK_REQ_DONE;
	conn->info_ident = 0;

	mutex_lock(&conn->lock);
	l2cap_conn_start(conn);
	mutex_unlock(&conn->lock);
}

/*
 * l2cap_user
 * External modules can register l2cap_user objects on l2cap_conn. The ->probe
 * callback is called during registration. The ->remove callback is called
 * during unregistration.
 * An l2cap_user object can either be explicitly unregistered or when the
 * underlying l2cap_conn object is deleted. This guarantees that l2cap->hcon,
 * l2cap->hchan, .. are valid as long as the remove callback hasn't been called.
 * External modules must own a reference to the l2cap_conn object if they intend
 * to call l2cap_unregister_user(). The l2cap_conn object might get destroyed at
 * any time if they don't.
 */

int l2cap_register_user(struct l2cap_conn *conn, struct l2cap_user *user)
{
	struct hci_dev *hdev = conn->hcon->hdev;
	int ret;

	/* We need to check whether l2cap_conn is registered. If it is not, we
	 * must not register the l2cap_user. l2cap_conn_del() is unregisters
	 * l2cap_conn objects, but doesn't provide its own locking. Instead, it
	 * relies on the parent hci_conn object to be locked. This itself relies
	 * on the hci_dev object to be locked. So we must lock the hci device
	 * here, too. */

	hci_dev_lock(hdev);

	if (!list_empty(&user->list)) {
		ret = -EINVAL;
		goto out_unlock;
	}

	/* conn->hchan is NULL after l2cap_conn_del() was called */
	if (!conn->hchan) {
		ret = -ENODEV;
		goto out_unlock;
	}

	ret = user->probe(conn, user);
	if (ret)
		goto out_unlock;

	list_add(&user->list, &conn->users);
	ret = 0;

out_unlock:
	hci_dev_unlock(hdev);
	return ret;
}
EXPORT_SYMBOL(l2cap_register_user);

void l2cap_unregister_user(struct l2cap_conn *conn, struct l2cap_user *user)
{
	struct hci_dev *hdev = conn->hcon->hdev;

	hci_dev_lock(hdev);

	if (list_empty(&user->list))
		goto out_unlock;

	list_del_init(&user->list);
	user->remove(conn, user);

out_unlock:
	hci_dev_unlock(hdev);
}
EXPORT_SYMBOL(l2cap_unregister_user);

static void l2cap_unregister_all_users(struct l2cap_conn *conn)
{
	struct l2cap_user *user;

	while (!list_empty(&conn->users)) {
		user = list_first_entry(&conn->users, struct l2cap_user, list);
		list_del_init(&user->list);
		user->remove(conn, user);
	}
}

static void l2cap_conn_del(struct hci_conn *hcon, int err)
{
	struct l2cap_conn *conn = hcon->l2cap_data;
	struct l2cap_chan *chan, *l;

	if (!conn)
		return;

	BT_DBG("hcon %p conn %p, err %d", hcon, conn, err);

	mutex_lock(&conn->lock);

	kfree_skb(conn->rx_skb);

	skb_queue_purge(&conn->pending_rx);

	/* We can not call flush_work(&conn->pending_rx_work) here since we
	 * might block if we are running on a worker from the same workqueue
	 * pending_rx_work is waiting on.
	 */
	if (work_pending(&conn->pending_rx_work))
		cancel_work_sync(&conn->pending_rx_work);

	cancel_delayed_work_sync(&conn->id_addr_timer);

	l2cap_unregister_all_users(conn);

	/* Force the connection to be immediately dropped */
	hcon->disc_timeout = 0;

	/* Kill channels */
	list_for_each_entry_safe(chan, l, &conn->chan_l, list) {
		l2cap_chan_hold(chan);
		l2cap_chan_lock(chan);

		l2cap_chan_del(chan, err);

		chan->ops->close(chan);

		l2cap_chan_unlock(chan);
		l2cap_chan_put(chan);
	}

	if (conn->info_state & L2CAP_INFO_FEAT_MASK_REQ_SENT)
		cancel_delayed_work_sync(&conn->info_timer);

	hci_chan_del(conn->hchan);
	conn->hchan = NULL;

	hcon->l2cap_data = NULL;
	mutex_unlock(&conn->lock);
	l2cap_conn_put(conn);
}

static void l2cap_conn_free(struct kref *ref)
{
	struct l2cap_conn *conn = container_of(ref, struct l2cap_conn, ref);

	hci_conn_put(conn->hcon);
	kfree(conn);
}

struct l2cap_conn *l2cap_conn_get(struct l2cap_conn *conn)
{
	kref_get(&conn->ref);
	return conn;
}
EXPORT_SYMBOL(l2cap_conn_get);

void l2cap_conn_put(struct l2cap_conn *conn)
{
	kref_put(&conn->ref, l2cap_conn_free);
}
EXPORT_SYMBOL(l2cap_conn_put);

/* ---- Socket interface ---- */

/* Find socket with psm and source / destination bdaddr.
 * Returns closest match.
 */
static struct l2cap_chan *l2cap_global_chan_by_psm(int state, __le16 psm,
						   bdaddr_t *src,
						   bdaddr_t *dst,
						   u8 link_type)
{
	struct l2cap_chan *c, *tmp, *c1 = NULL;

	read_lock(&chan_list_lock);

	list_for_each_entry_safe(c, tmp, &chan_list, global_l) {
		if (state && c->state != state)
			continue;

		if (link_type == ACL_LINK && c->src_type != BDADDR_BREDR)
			continue;

		if (link_type == LE_LINK && c->src_type == BDADDR_BREDR)
			continue;

		if (c->chan_type != L2CAP_CHAN_FIXED && c->psm == psm) {
			int src_match, dst_match;
			int src_any, dst_any;

			/* Exact match. */
			src_match = !bacmp(&c->src, src);
			dst_match = !bacmp(&c->dst, dst);
			if (src_match && dst_match) {
				if (!l2cap_chan_hold_unless_zero(c))
					continue;

				read_unlock(&chan_list_lock);
				return c;
			}

			/* Closest match */
			src_any = !bacmp(&c->src, BDADDR_ANY);
			dst_any = !bacmp(&c->dst, BDADDR_ANY);
			if ((src_match && dst_any) || (src_any && dst_match) ||
			    (src_any && dst_any))
				c1 = c;
		}
	}

	if (c1)
		c1 = l2cap_chan_hold_unless_zero(c1);

	read_unlock(&chan_list_lock);

	return c1;
}

static void l2cap_monitor_timeout(struct work_struct *work)
{
	struct l2cap_chan *chan = container_of(work, struct l2cap_chan,
					       monitor_timer.work);

	BT_DBG("chan %p", chan);

	l2cap_chan_lock(chan);

	if (!chan->conn) {
		l2cap_chan_unlock(chan);
		l2cap_chan_put(chan);
		return;
	}

	l2cap_tx(chan, NULL, NULL, L2CAP_EV_MONITOR_TO);

	l2cap_chan_unlock(chan);
	l2cap_chan_put(chan);
}

static void l2cap_retrans_timeout(struct work_struct *work)
{
	struct l2cap_chan *chan = container_of(work, struct l2cap_chan,
					       retrans_timer.work);

	BT_DBG("chan %p", chan);

	l2cap_chan_lock(chan);

	if (!chan->conn) {
		l2cap_chan_unlock(chan);
		l2cap_chan_put(chan);
		return;
	}

	l2cap_tx(chan, NULL, NULL, L2CAP_EV_RETRANS_TO);
	l2cap_chan_unlock(chan);
	l2cap_chan_put(chan);
}

static void l2cap_streaming_send(struct l2cap_chan *chan,
				 struct sk_buff_head *skbs)
{
	struct sk_buff *skb;
	struct l2cap_ctrl *control;

	BT_DBG("chan %p, skbs %p", chan, skbs);

	skb_queue_splice_tail_init(skbs, &chan->tx_q);

	while (!skb_queue_empty(&chan->tx_q)) {

		skb = skb_dequeue(&chan->tx_q);

		bt_cb(skb)->l2cap.retries = 1;
		control = &bt_cb(skb)->l2cap;

		control->reqseq = 0;
		control->txseq = chan->next_tx_seq;

		__pack_control(chan, control, skb);

		if (chan->fcs == L2CAP_FCS_CRC16) {
			u16 fcs = crc16(0, (u8 *) skb->data, skb->len);
			put_unaligned_le16(fcs, skb_put(skb, L2CAP_FCS_SIZE));
		}

		l2cap_do_send(chan, skb);

		BT_DBG("Sent txseq %u", control->txseq);

		chan->next_tx_seq = __next_seq(chan, chan->next_tx_seq);
		chan->frames_sent++;
	}
}

static int l2cap_ertm_send(struct l2cap_chan *chan)
{
	struct sk_buff *skb, *tx_skb;
	struct l2cap_ctrl *control;
	int sent = 0;

	BT_DBG("chan %p", chan);

	if (chan->state != BT_CONNECTED)
		return -ENOTCONN;

	if (test_bit(CONN_REMOTE_BUSY, &chan->conn_state))
		return 0;

	while (chan->tx_send_head &&
	       chan->unacked_frames < chan->remote_tx_win &&
	       chan->tx_state == L2CAP_TX_STATE_XMIT) {

		skb = chan->tx_send_head;

		bt_cb(skb)->l2cap.retries = 1;
		control = &bt_cb(skb)->l2cap;

		if (test_and_clear_bit(CONN_SEND_FBIT, &chan->conn_state))
			control->final = 1;

		control->reqseq = chan->buffer_seq;
		chan->last_acked_seq = chan->buffer_seq;
		control->txseq = chan->next_tx_seq;

		__pack_control(chan, control, skb);

		if (chan->fcs == L2CAP_FCS_CRC16) {
			u16 fcs = crc16(0, (u8 *) skb->data, skb->len);
			put_unaligned_le16(fcs, skb_put(skb, L2CAP_FCS_SIZE));
		}

		/* Clone after data has been modified. Data is assumed to be
		   read-only (for locking purposes) on cloned sk_buffs.
		 */
		tx_skb = skb_clone(skb, GFP_KERNEL);

		if (!tx_skb)
			break;

		__set_retrans_timer(chan);

		chan->next_tx_seq = __next_seq(chan, chan->next_tx_seq);
		chan->unacked_frames++;
		chan->frames_sent++;
		sent++;

		if (skb_queue_is_last(&chan->tx_q, skb))
			chan->tx_send_head = NULL;
		else
			chan->tx_send_head = skb_queue_next(&chan->tx_q, skb);

		l2cap_do_send(chan, tx_skb);
		BT_DBG("Sent txseq %u", control->txseq);
	}

	BT_DBG("Sent %d, %u unacked, %u in ERTM queue", sent,
	       chan->unacked_frames, skb_queue_len(&chan->tx_q));

	return sent;
}

static void l2cap_ertm_resend(struct l2cap_chan *chan)
{
	struct l2cap_ctrl control;
	struct sk_buff *skb;
	struct sk_buff *tx_skb;
	u16 seq;

	BT_DBG("chan %p", chan);

	if (test_bit(CONN_REMOTE_BUSY, &chan->conn_state))
		return;

	while (chan->retrans_list.head != L2CAP_SEQ_LIST_CLEAR) {
		seq = l2cap_seq_list_pop(&chan->retrans_list);

		skb = l2cap_ertm_seq_in_queue(&chan->tx_q, seq);
		if (!skb) {
			BT_DBG("Error: Can't retransmit seq %d, frame missing",
			       seq);
			continue;
		}

		bt_cb(skb)->l2cap.retries++;
		control = bt_cb(skb)->l2cap;

		if (chan->max_tx != 0 &&
		    bt_cb(skb)->l2cap.retries > chan->max_tx) {
			BT_DBG("Retry limit exceeded (%d)", chan->max_tx);
			l2cap_send_disconn_req(chan, ECONNRESET);
			l2cap_seq_list_clear(&chan->retrans_list);
			break;
		}

		control.reqseq = chan->buffer_seq;
		if (test_and_clear_bit(CONN_SEND_FBIT, &chan->conn_state))
			control.final = 1;
		else
			control.final = 0;

		if (skb_cloned(skb)) {
			/* Cloned sk_buffs are read-only, so we need a
			 * writeable copy
			 */
			tx_skb = skb_copy(skb, GFP_KERNEL);
		} else {
			tx_skb = skb_clone(skb, GFP_KERNEL);
		}

		if (!tx_skb) {
			l2cap_seq_list_clear(&chan->retrans_list);
			break;
		}

		/* Update skb contents */
		if (test_bit(FLAG_EXT_CTRL, &chan->flags)) {
			put_unaligned_le32(__pack_extended_control(&control),
					   tx_skb->data + L2CAP_HDR_SIZE);
		} else {
			put_unaligned_le16(__pack_enhanced_control(&control),
					   tx_skb->data + L2CAP_HDR_SIZE);
		}

		/* Update FCS */
		if (chan->fcs == L2CAP_FCS_CRC16) {
			u16 fcs = crc16(0, (u8 *) tx_skb->data,
					tx_skb->len - L2CAP_FCS_SIZE);
			put_unaligned_le16(fcs, skb_tail_pointer(tx_skb) -
						L2CAP_FCS_SIZE);
		}

		l2cap_do_send(chan, tx_skb);

		BT_DBG("Resent txseq %d", control.txseq);

		chan->last_acked_seq = chan->buffer_seq;
	}
}

static void l2cap_retransmit(struct l2cap_chan *chan,
			     struct l2cap_ctrl *control)
{
	BT_DBG("chan %p, control %p", chan, control);

	l2cap_seq_list_append(&chan->retrans_list, control->reqseq);
	l2cap_ertm_resend(chan);
}

static void l2cap_retransmit_all(struct l2cap_chan *chan,
				 struct l2cap_ctrl *control)
{
	struct sk_buff *skb;

	BT_DBG("chan %p, control %p", chan, control);

	if (control->poll)
		set_bit(CONN_SEND_FBIT, &chan->conn_state);

	l2cap_seq_list_clear(&chan->retrans_list);

	if (test_bit(CONN_REMOTE_BUSY, &chan->conn_state))
		return;

	if (chan->unacked_frames) {
		skb_queue_walk(&chan->tx_q, skb) {
			if (bt_cb(skb)->l2cap.txseq == control->reqseq ||
			    skb == chan->tx_send_head)
				break;
		}

		skb_queue_walk_from(&chan->tx_q, skb) {
			if (skb == chan->tx_send_head)
				break;

			l2cap_seq_list_append(&chan->retrans_list,
					      bt_cb(skb)->l2cap.txseq);
		}

		l2cap_ertm_resend(chan);
	}
}

static void l2cap_send_ack(struct l2cap_chan *chan)
{
	struct l2cap_ctrl control;
	u16 frames_to_ack = __seq_offset(chan, chan->buffer_seq,
					 chan->last_acked_seq);
	int threshold;

	BT_DBG("chan %p last_acked_seq %d buffer_seq %d",
	       chan, chan->last_acked_seq, chan->buffer_seq);

	memset(&control, 0, sizeof(control));
	control.sframe = 1;

	if (test_bit(CONN_LOCAL_BUSY, &chan->conn_state) &&
	    chan->rx_state == L2CAP_RX_STATE_RECV) {
		__clear_ack_timer(chan);
		control.super = L2CAP_SUPER_RNR;
		control.reqseq = chan->buffer_seq;
		l2cap_send_sframe(chan, &control);
	} else {
		if (!test_bit(CONN_REMOTE_BUSY, &chan->conn_state)) {
			l2cap_ertm_send(chan);
			/* If any i-frames were sent, they included an ack */
			if (chan->buffer_seq == chan->last_acked_seq)
				frames_to_ack = 0;
		}

		/* Ack now if the window is 3/4ths full.
		 * Calculate without mul or div
		 */
		threshold = chan->ack_win;
		threshold += threshold << 1;
		threshold >>= 2;

		BT_DBG("frames_to_ack %u, threshold %d", frames_to_ack,
		       threshold);

		if (frames_to_ack >= threshold) {
			__clear_ack_timer(chan);
			control.super = L2CAP_SUPER_RR;
			control.reqseq = chan->buffer_seq;
			l2cap_send_sframe(chan, &control);
			frames_to_ack = 0;
		}

		if (frames_to_ack)
			__set_ack_timer(chan);
	}
}

static inline int l2cap_skbuff_fromiovec(struct l2cap_chan *chan,
					 struct msghdr *msg, int len,
					 int count, struct sk_buff *skb)
{
	struct l2cap_conn *conn = chan->conn;
	struct sk_buff **frag;
	int sent = 0;

	if (!copy_from_iter_full(skb_put(skb, count), count, &msg->msg_iter))
		return -EFAULT;

	sent += count;
	len  -= count;

	/* Continuation fragments (no L2CAP header) */
	frag = &skb_shinfo(skb)->frag_list;
	while (len) {
		struct sk_buff *tmp;

		count = min_t(unsigned int, conn->mtu, len);

		tmp = chan->ops->alloc_skb(chan, 0, count,
					   msg->msg_flags & MSG_DONTWAIT);
		if (IS_ERR(tmp))
			return PTR_ERR(tmp);

		*frag = tmp;

		if (!copy_from_iter_full(skb_put(*frag, count), count,
				   &msg->msg_iter))
			return -EFAULT;

		sent += count;
		len  -= count;

		skb->len += (*frag)->len;
		skb->data_len += (*frag)->len;

		frag = &(*frag)->next;
	}

	return sent;
}

static struct sk_buff *l2cap_create_connless_pdu(struct l2cap_chan *chan,
						 struct msghdr *msg, size_t len)
{
	struct l2cap_conn *conn = chan->conn;
	struct sk_buff *skb;
	int err, count, hlen = L2CAP_HDR_SIZE + L2CAP_PSMLEN_SIZE;
	struct l2cap_hdr *lh;

	BT_DBG("chan %p psm 0x%2.2x len %zu", chan,
	       __le16_to_cpu(chan->psm), len);

	count = min_t(unsigned int, (conn->mtu - hlen), len);

	skb = chan->ops->alloc_skb(chan, hlen, count,
				   msg->msg_flags & MSG_DONTWAIT);
	if (IS_ERR(skb))
		return skb;

	/* Create L2CAP header */
	lh = skb_put(skb, L2CAP_HDR_SIZE);
	lh->cid = cpu_to_le16(chan->dcid);
	lh->len = cpu_to_le16(len + L2CAP_PSMLEN_SIZE);
	put_unaligned(chan->psm, (__le16 *) skb_put(skb, L2CAP_PSMLEN_SIZE));

	err = l2cap_skbuff_fromiovec(chan, msg, len, count, skb);
	if (unlikely(err < 0)) {
		kfree_skb(skb);
		return ERR_PTR(err);
	}
	return skb;
}

static struct sk_buff *l2cap_create_basic_pdu(struct l2cap_chan *chan,
					      struct msghdr *msg, size_t len)
{
	struct l2cap_conn *conn = chan->conn;
	struct sk_buff *skb;
	int err, count;
	struct l2cap_hdr *lh;

	BT_DBG("chan %p len %zu", chan, len);

	count = min_t(unsigned int, (conn->mtu - L2CAP_HDR_SIZE), len);

	skb = chan->ops->alloc_skb(chan, L2CAP_HDR_SIZE, count,
				   msg->msg_flags & MSG_DONTWAIT);
	if (IS_ERR(skb))
		return skb;

	/* Create L2CAP header */
	lh = skb_put(skb, L2CAP_HDR_SIZE);
	lh->cid = cpu_to_le16(chan->dcid);
	lh->len = cpu_to_le16(len);

	err = l2cap_skbuff_fromiovec(chan, msg, len, count, skb);
	if (unlikely(err < 0)) {
		kfree_skb(skb);
		return ERR_PTR(err);
	}
	return skb;
}

static struct sk_buff *l2cap_create_iframe_pdu(struct l2cap_chan *chan,
					       struct msghdr *msg, size_t len,
					       u16 sdulen)
{
	struct l2cap_conn *conn = chan->conn;
	struct sk_buff *skb;
	int err, count, hlen;
	struct l2cap_hdr *lh;

	BT_DBG("chan %p len %zu", chan, len);

	if (!conn)
		return ERR_PTR(-ENOTCONN);

	hlen = __ertm_hdr_size(chan);

	if (sdulen)
		hlen += L2CAP_SDULEN_SIZE;

	if (chan->fcs == L2CAP_FCS_CRC16)
		hlen += L2CAP_FCS_SIZE;

	count = min_t(unsigned int, (conn->mtu - hlen), len);

	skb = chan->ops->alloc_skb(chan, hlen, count,
				   msg->msg_flags & MSG_DONTWAIT);
	if (IS_ERR(skb))
		return skb;

	/* Create L2CAP header */
	lh = skb_put(skb, L2CAP_HDR_SIZE);
	lh->cid = cpu_to_le16(chan->dcid);
	lh->len = cpu_to_le16(len + (hlen - L2CAP_HDR_SIZE));

	/* Control header is populated later */
	if (test_bit(FLAG_EXT_CTRL, &chan->flags))
		put_unaligned_le32(0, skb_put(skb, L2CAP_EXT_CTRL_SIZE));
	else
		put_unaligned_le16(0, skb_put(skb, L2CAP_ENH_CTRL_SIZE));

	if (sdulen)
		put_unaligned_le16(sdulen, skb_put(skb, L2CAP_SDULEN_SIZE));

	err = l2cap_skbuff_fromiovec(chan, msg, len, count, skb);
	if (unlikely(err < 0)) {
		kfree_skb(skb);
		return ERR_PTR(err);
	}

	bt_cb(skb)->l2cap.fcs = chan->fcs;
	bt_cb(skb)->l2cap.retries = 0;
	return skb;
}

static int l2cap_segment_sdu(struct l2cap_chan *chan,
			     struct sk_buff_head *seg_queue,
			     struct msghdr *msg, size_t len)
{
	struct sk_buff *skb;
	u16 sdu_len;
	size_t pdu_len;
	u8 sar;

	BT_DBG("chan %p, msg %p, len %zu", chan, msg, len);

	/* It is critical that ERTM PDUs fit in a single HCI fragment,
	 * so fragmented skbs are not used.  The HCI layer's handling
	 * of fragmented skbs is not compatible with ERTM's queueing.
	 */

	/* PDU size is derived from the HCI MTU */
	pdu_len = chan->conn->mtu;

	/* Constrain PDU size for BR/EDR connections */
	pdu_len = min_t(size_t, pdu_len, L2CAP_BREDR_MAX_PAYLOAD);

	/* Adjust for largest possible L2CAP overhead. */
	if (chan->fcs)
		pdu_len -= L2CAP_FCS_SIZE;

	pdu_len -= __ertm_hdr_size(chan);

	/* Remote device may have requested smaller PDUs */
	pdu_len = min_t(size_t, pdu_len, chan->remote_mps);

	if (len <= pdu_len) {
		sar = L2CAP_SAR_UNSEGMENTED;
		sdu_len = 0;
		pdu_len = len;
	} else {
		sar = L2CAP_SAR_START;
		sdu_len = len;
	}

	while (len > 0) {
		skb = l2cap_create_iframe_pdu(chan, msg, pdu_len, sdu_len);

		if (IS_ERR(skb)) {
			__skb_queue_purge(seg_queue);
			return PTR_ERR(skb);
		}

		bt_cb(skb)->l2cap.sar = sar;
		__skb_queue_tail(seg_queue, skb);

		len -= pdu_len;
		if (sdu_len)
			sdu_len = 0;

		if (len <= pdu_len) {
			sar = L2CAP_SAR_END;
			pdu_len = len;
		} else {
			sar = L2CAP_SAR_CONTINUE;
		}
	}

	return 0;
}

static struct sk_buff *l2cap_create_le_flowctl_pdu(struct l2cap_chan *chan,
						   struct msghdr *msg,
						   size_t len, u16 sdulen)
{
	struct l2cap_conn *conn = chan->conn;
	struct sk_buff *skb;
	int err, count, hlen;
	struct l2cap_hdr *lh;

	BT_DBG("chan %p len %zu", chan, len);

	if (!conn)
		return ERR_PTR(-ENOTCONN);

	hlen = L2CAP_HDR_SIZE;

	if (sdulen)
		hlen += L2CAP_SDULEN_SIZE;

	count = min_t(unsigned int, (conn->mtu - hlen), len);

	skb = chan->ops->alloc_skb(chan, hlen, count,
				   msg->msg_flags & MSG_DONTWAIT);
	if (IS_ERR(skb))
		return skb;

	/* Create L2CAP header */
	lh = skb_put(skb, L2CAP_HDR_SIZE);
	lh->cid = cpu_to_le16(chan->dcid);
	lh->len = cpu_to_le16(len + (hlen - L2CAP_HDR_SIZE));

	if (sdulen)
		put_unaligned_le16(sdulen, skb_put(skb, L2CAP_SDULEN_SIZE));

	err = l2cap_skbuff_fromiovec(chan, msg, len, count, skb);
	if (unlikely(err < 0)) {
		kfree_skb(skb);
		return ERR_PTR(err);
	}

	return skb;
}

static int l2cap_segment_le_sdu(struct l2cap_chan *chan,
				struct sk_buff_head *seg_queue,
				struct msghdr *msg, size_t len)
{
	struct sk_buff *skb;
	size_t pdu_len;
	u16 sdu_len;

	BT_DBG("chan %p, msg %p, len %zu", chan, msg, len);

	sdu_len = len;
	pdu_len = chan->remote_mps - L2CAP_SDULEN_SIZE;

	while (len > 0) {
		if (len <= pdu_len)
			pdu_len = len;

		skb = l2cap_create_le_flowctl_pdu(chan, msg, pdu_len, sdu_len);
		if (IS_ERR(skb)) {
			__skb_queue_purge(seg_queue);
			return PTR_ERR(skb);
		}

		__skb_queue_tail(seg_queue, skb);

		len -= pdu_len;

		if (sdu_len) {
			sdu_len = 0;
			pdu_len += L2CAP_SDULEN_SIZE;
		}
	}

	return 0;
}

static void l2cap_le_flowctl_send(struct l2cap_chan *chan)
{
	int sent = 0;

	BT_DBG("chan %p", chan);

	while (chan->tx_credits && !skb_queue_empty(&chan->tx_q)) {
		l2cap_do_send(chan, skb_dequeue(&chan->tx_q));
		chan->tx_credits--;
		sent++;
	}

	BT_DBG("Sent %d credits %u queued %u", sent, chan->tx_credits,
	       skb_queue_len(&chan->tx_q));
}

static void l2cap_tx_timestamp(struct sk_buff *skb,
			       const struct sockcm_cookie *sockc,
			       size_t len)
{
	struct sock *sk = skb ? skb->sk : NULL;

	if (sk && sk->sk_type == SOCK_STREAM)
		hci_setup_tx_timestamp(skb, len, sockc);
	else
		hci_setup_tx_timestamp(skb, 1, sockc);
}

static void l2cap_tx_timestamp_seg(struct sk_buff_head *queue,
				   const struct sockcm_cookie *sockc,
				   size_t len)
{
	struct sk_buff *skb = skb_peek(queue);
	struct sock *sk = skb ? skb->sk : NULL;

	if (sk && sk->sk_type == SOCK_STREAM)
		l2cap_tx_timestamp(skb_peek_tail(queue), sockc, len);
	else
		l2cap_tx_timestamp(skb, sockc, len);
}

int l2cap_chan_send(struct l2cap_chan *chan, struct msghdr *msg, size_t len,
		    const struct sockcm_cookie *sockc)
{
	struct sk_buff *skb;
	int err;
	struct sk_buff_head seg_queue;

	if (!chan->conn)
		return -ENOTCONN;

	/* Connectionless channel */
	if (chan->chan_type == L2CAP_CHAN_CONN_LESS) {
		skb = l2cap_create_connless_pdu(chan, msg, len);
		if (IS_ERR(skb))
			return PTR_ERR(skb);

		l2cap_tx_timestamp(skb, sockc, len);

		l2cap_do_send(chan, skb);
		return len;
	}

	switch (chan->mode) {
	case L2CAP_MODE_LE_FLOWCTL:
	case L2CAP_MODE_EXT_FLOWCTL:
		/* Check outgoing MTU */
		if (len > chan->omtu)
			return -EMSGSIZE;

		__skb_queue_head_init(&seg_queue);

		err = l2cap_segment_le_sdu(chan, &seg_queue, msg, len);

		if (chan->state != BT_CONNECTED) {
			__skb_queue_purge(&seg_queue);
			err = -ENOTCONN;
		}

		if (err)
			return err;

		l2cap_tx_timestamp_seg(&seg_queue, sockc, len);

		skb_queue_splice_tail_init(&seg_queue, &chan->tx_q);

		l2cap_le_flowctl_send(chan);

		if (!chan->tx_credits)
			chan->ops->suspend(chan);

		err = len;

		break;

	case L2CAP_MODE_BASIC:
		/* Check outgoing MTU */
		if (len > chan->omtu)
			return -EMSGSIZE;

		/* Create a basic PDU */
		skb = l2cap_create_basic_pdu(chan, msg, len);
		if (IS_ERR(skb))
			return PTR_ERR(skb);

		l2cap_tx_timestamp(skb, sockc, len);

		l2cap_do_send(chan, skb);
		err = len;
		break;

	case L2CAP_MODE_ERTM:
	case L2CAP_MODE_STREAMING:
		/* Check outgoing MTU */
		if (len > chan->omtu) {
			err = -EMSGSIZE;
			break;
		}

		__skb_queue_head_init(&seg_queue);

		/* Do segmentation before calling in to the state machine,
		 * since it's possible to block while waiting for memory
		 * allocation.
		 */
		err = l2cap_segment_sdu(chan, &seg_queue, msg, len);

		if (err)
			break;

		if (chan->mode == L2CAP_MODE_ERTM) {
			/* TODO: ERTM mode timestamping */
			l2cap_tx(chan, NULL, &seg_queue, L2CAP_EV_DATA_REQUEST);
		} else {
			l2cap_tx_timestamp_seg(&seg_queue, sockc, len);
			l2cap_streaming_send(chan, &seg_queue);
		}

		err = len;

		/* If the skbs were not queued for sending, they'll still be in
		 * seg_queue and need to be purged.
		 */
		__skb_queue_purge(&seg_queue);
		break;

	default:
		BT_DBG("bad state %1.1x", chan->mode);
		err = -EBADFD;
	}

	return err;
}
EXPORT_SYMBOL_GPL(l2cap_chan_send);

static void l2cap_send_srej(struct l2cap_chan *chan, u16 txseq)
{
	struct l2cap_ctrl control;
	u16 seq;

	BT_DBG("chan %p, txseq %u", chan, txseq);

	memset(&control, 0, sizeof(control));
	control.sframe = 1;
	control.super = L2CAP_SUPER_SREJ;

	for (seq = chan->expected_tx_seq; seq != txseq;
	     seq = __next_seq(chan, seq)) {
		if (!l2cap_ertm_seq_in_queue(&chan->srej_q, seq)) {
			control.reqseq = seq;
			l2cap_send_sframe(chan, &control);
			l2cap_seq_list_append(&chan->srej_list, seq);
		}
	}

	chan->expected_tx_seq = __next_seq(chan, txseq);
}

static void l2cap_send_srej_tail(struct l2cap_chan *chan)
{
	struct l2cap_ctrl control;

	BT_DBG("chan %p", chan);

	if (chan->srej_list.tail == L2CAP_SEQ_LIST_CLEAR)
		return;

	memset(&control, 0, sizeof(control));
	control.sframe = 1;
	control.super = L2CAP_SUPER_SREJ;
	control.reqseq = chan->srej_list.tail;
	l2cap_send_sframe(chan, &control);
}

static void l2cap_send_srej_list(struct l2cap_chan *chan, u16 txseq)
{
	struct l2cap_ctrl control;
	u16 initial_head;
	u16 seq;

	BT_DBG("chan %p, txseq %u", chan, txseq);

	memset(&control, 0, sizeof(control));
	control.sframe = 1;
	control.super = L2CAP_SUPER_SREJ;

	/* Capture initial list head to allow only one pass through the list. */
	initial_head = chan->srej_list.head;

	do {
		seq = l2cap_seq_list_pop(&chan->srej_list);
		if (seq == txseq || seq == L2CAP_SEQ_LIST_CLEAR)
			break;

		control.reqseq = seq;
		l2cap_send_sframe(chan, &control);
		l2cap_seq_list_append(&chan->srej_list, seq);
	} while (chan->srej_list.head != initial_head);
}

static void l2cap_process_reqseq(struct l2cap_chan *chan, u16 reqseq)
{
	struct sk_buff *acked_skb;
	u16 ackseq;

	BT_DBG("chan %p, reqseq %u", chan, reqseq);

	if (chan->unacked_frames == 0 || reqseq == chan->expected_ack_seq)
		return;

	BT_DBG("expected_ack_seq %u, unacked_frames %u",
	       chan->expected_ack_seq, chan->unacked_frames);

	for (ackseq = chan->expected_ack_seq; ackseq != reqseq;
	     ackseq = __next_seq(chan, ackseq)) {

		acked_skb = l2cap_ertm_seq_in_queue(&chan->tx_q, ackseq);
		if (acked_skb) {
			skb_unlink(acked_skb, &chan->tx_q);
			kfree_skb(acked_skb);
			chan->unacked_frames--;
		}
	}

	chan->expected_ack_seq = reqseq;

	if (chan->unacked_frames == 0)
		__clear_retrans_timer(chan);

	BT_DBG("unacked_frames %u", chan->unacked_frames);
}

static void l2cap_abort_rx_srej_sent(struct l2cap_chan *chan)
{
	BT_DBG("chan %p", chan);

	chan->expected_tx_seq = chan->buffer_seq;
	l2cap_seq_list_clear(&chan->srej_list);
	skb_queue_purge(&chan->srej_q);
	chan->rx_state = L2CAP_RX_STATE_RECV;
}

static void l2cap_tx_state_xmit(struct l2cap_chan *chan,
				struct l2cap_ctrl *control,
				struct sk_buff_head *skbs, u8 event)
{
	BT_DBG("chan %p, control %p, skbs %p, event %d", chan, control, skbs,
	       event);

	switch (event) {
	case L2CAP_EV_DATA_REQUEST:
		if (chan->tx_send_head == NULL)
			chan->tx_send_head = skb_peek(skbs);

		skb_queue_splice_tail_init(skbs, &chan->tx_q);
		l2cap_ertm_send(chan);
		break;
	case L2CAP_EV_LOCAL_BUSY_DETECTED:
		BT_DBG("Enter LOCAL_BUSY");
		set_bit(CONN_LOCAL_BUSY, &chan->conn_state);

		if (chan->rx_state == L2CAP_RX_STATE_SREJ_SENT) {
			/* The SREJ_SENT state must be aborted if we are to
			 * enter the LOCAL_BUSY state.
			 */
			l2cap_abort_rx_srej_sent(chan);
		}

		l2cap_send_ack(chan);

		break;
	case L2CAP_EV_LOCAL_BUSY_CLEAR:
		BT_DBG("Exit LOCAL_BUSY");
		clear_bit(CONN_LOCAL_BUSY, &chan->conn_state);

		if (test_bit(CONN_RNR_SENT, &chan->conn_state)) {
			struct l2cap_ctrl local_control;

			memset(&local_control, 0, sizeof(local_control));
			local_control.sframe = 1;
			local_control.super = L2CAP_SUPER_RR;
			local_control.poll = 1;
			local_control.reqseq = chan->buffer_seq;
			l2cap_send_sframe(chan, &local_control);

			chan->retry_count = 1;
			__set_monitor_timer(chan);
			chan->tx_state = L2CAP_TX_STATE_WAIT_F;
		}
		break;
	case L2CAP_EV_RECV_REQSEQ_AND_FBIT:
		l2cap_process_reqseq(chan, control->reqseq);
		break;
	case L2CAP_EV_EXPLICIT_POLL:
		l2cap_send_rr_or_rnr(chan, 1);
		chan->retry_count = 1;
		__set_monitor_timer(chan);
		__clear_ack_timer(chan);
		chan->tx_state = L2CAP_TX_STATE_WAIT_F;
		break;
	case L2CAP_EV_RETRANS_TO:
		l2cap_send_rr_or_rnr(chan, 1);
		chan->retry_count = 1;
		__set_monitor_timer(chan);
		chan->tx_state = L2CAP_TX_STATE_WAIT_F;
		break;
	case L2CAP_EV_RECV_FBIT:
		/* Nothing to process */
		break;
	default:
		break;
	}
}

static void l2cap_tx_state_wait_f(struct l2cap_chan *chan,
				  struct l2cap_ctrl *control,
				  struct sk_buff_head *skbs, u8 event)
{
	BT_DBG("chan %p, control %p, skbs %p, event %d", chan, control, skbs,
	       event);

	switch (event) {
	case L2CAP_EV_DATA_REQUEST:
		if (chan->tx_send_head == NULL)
			chan->tx_send_head = skb_peek(skbs);
		/* Queue data, but don't send. */
		skb_queue_splice_tail_init(skbs, &chan->tx_q);
		break;
	case L2CAP_EV_LOCAL_BUSY_DETECTED:
		BT_DBG("Enter LOCAL_BUSY");
		set_bit(CONN_LOCAL_BUSY, &chan->conn_state);

		if (chan->rx_state == L2CAP_RX_STATE_SREJ_SENT) {
			/* The SREJ_SENT state must be aborted if we are to
			 * enter the LOCAL_BUSY state.
			 */
			l2cap_abort_rx_srej_sent(chan);
		}

		l2cap_send_ack(chan);

		break;
	case L2CAP_EV_LOCAL_BUSY_CLEAR:
		BT_DBG("Exit LOCAL_BUSY");
		clear_bit(CONN_LOCAL_BUSY, &chan->conn_state);

		if (test_bit(CONN_RNR_SENT, &chan->conn_state)) {
			struct l2cap_ctrl local_control;
			memset(&local_control, 0, sizeof(local_control));
			local_control.sframe = 1;
			local_control.super = L2CAP_SUPER_RR;
			local_control.poll = 1;
			local_control.reqseq = chan->buffer_seq;
			l2cap_send_sframe(chan, &local_control);

			chan->retry_count = 1;
			__set_monitor_timer(chan);
			chan->tx_state = L2CAP_TX_STATE_WAIT_F;
		}
		break;
	case L2CAP_EV_RECV_REQSEQ_AND_FBIT:
		l2cap_process_reqseq(chan, control->reqseq);
		fallthrough;

	case L2CAP_EV_RECV_FBIT:
		if (control && control->final) {
			__clear_monitor_timer(chan);
			if (chan->unacked_frames > 0)
				__set_retrans_timer(chan);
			chan->retry_count = 0;
			chan->tx_state = L2CAP_TX_STATE_XMIT;
			BT_DBG("recv fbit tx_state 0x2.2%x", chan->tx_state);
		}
		break;
	case L2CAP_EV_EXPLICIT_POLL:
		/* Ignore */
		break;
	case L2CAP_EV_MONITOR_TO:
		if (chan->max_tx == 0 || chan->retry_count < chan->max_tx) {
			l2cap_send_rr_or_rnr(chan, 1);
			__set_monitor_timer(chan);
			chan->retry_count++;
		} else {
			l2cap_send_disconn_req(chan, ECONNABORTED);
		}
		break;
	default:
		break;
	}
}

static void l2cap_tx(struct l2cap_chan *chan, struct l2cap_ctrl *control,
		     struct sk_buff_head *skbs, u8 event)
{
	BT_DBG("chan %p, control %p, skbs %p, event %d, state %d",
	       chan, control, skbs, event, chan->tx_state);

	switch (chan->tx_state) {
	case L2CAP_TX_STATE_XMIT:
		l2cap_tx_state_xmit(chan, control, skbs, event);
		break;
	case L2CAP_TX_STATE_WAIT_F:
		l2cap_tx_state_wait_f(chan, control, skbs, event);
		break;
	default:
		/* Ignore event */
		break;
	}
}

static void l2cap_pass_to_tx(struct l2cap_chan *chan,
			     struct l2cap_ctrl *control)
{
	BT_DBG("chan %p, control %p", chan, control);
	l2cap_tx(chan, control, NULL, L2CAP_EV_RECV_REQSEQ_AND_FBIT);
}

static void l2cap_pass_to_tx_fbit(struct l2cap_chan *chan,
				  struct l2cap_ctrl *control)
{
	BT_DBG("chan %p, control %p", chan, control);
	l2cap_tx(chan, control, NULL, L2CAP_EV_RECV_FBIT);
}

/* Copy frame to all raw sockets on that connection */
static void l2cap_raw_recv(struct l2cap_conn *conn, struct sk_buff *skb)
{
	struct sk_buff *nskb;
	struct l2cap_chan *chan;

	BT_DBG("conn %p", conn);

	list_for_each_entry(chan, &conn->chan_l, list) {
		if (chan->chan_type != L2CAP_CHAN_RAW)
			continue;

		/* Don't send frame to the channel it came from */
		if (bt_cb(skb)->l2cap.chan == chan)
			continue;

		nskb = skb_clone(skb, GFP_KERNEL);
		if (!nskb)
			continue;
		if (chan->ops->recv(chan, nskb))
			kfree_skb(nskb);
	}
}

/* ---- L2CAP signalling commands ---- */
static struct sk_buff *l2cap_build_cmd(struct l2cap_conn *conn, u8 code,
				       u8 ident, u16 dlen, void *data)
{
	struct sk_buff *skb, **frag;
	struct l2cap_cmd_hdr *cmd;
	struct l2cap_hdr *lh;
	int len, count;

	BT_DBG("conn %p, code 0x%2.2x, ident 0x%2.2x, len %u",
	       conn, code, ident, dlen);

	if (conn->mtu < L2CAP_HDR_SIZE + L2CAP_CMD_HDR_SIZE)
		return NULL;

	len = L2CAP_HDR_SIZE + L2CAP_CMD_HDR_SIZE + dlen;
	count = min_t(unsigned int, conn->mtu, len);

	skb = bt_skb_alloc(count, GFP_KERNEL);
	if (!skb)
		return NULL;

	lh = skb_put(skb, L2CAP_HDR_SIZE);
	lh->len = cpu_to_le16(L2CAP_CMD_HDR_SIZE + dlen);

	if (conn->hcon->type == LE_LINK)
		lh->cid = cpu_to_le16(L2CAP_CID_LE_SIGNALING);
	else
		lh->cid = cpu_to_le16(L2CAP_CID_SIGNALING);

	cmd = skb_put(skb, L2CAP_CMD_HDR_SIZE);
	cmd->code  = code;
	cmd->ident = ident;
	cmd->len   = cpu_to_le16(dlen);

	if (dlen) {
		count -= L2CAP_HDR_SIZE + L2CAP_CMD_HDR_SIZE;
		skb_put_data(skb, data, count);
		data += count;
	}

	len -= skb->len;

	/* Continuation fragments (no L2CAP header) */
	frag = &skb_shinfo(skb)->frag_list;
	while (len) {
		count = min_t(unsigned int, conn->mtu, len);

		*frag = bt_skb_alloc(count, GFP_KERNEL);
		if (!*frag)
			goto fail;

		skb_put_data(*frag, data, count);

		len  -= count;
		data += count;

		frag = &(*frag)->next;
	}

	return skb;

fail:
	kfree_skb(skb);
	return NULL;
}

static inline int l2cap_get_conf_opt(void **ptr, int *type, int *olen,
				     unsigned long *val)
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

	BT_DBG("type 0x%2.2x len %u val 0x%lx", *type, opt->len, *val);
	return len;
}

static void l2cap_add_conf_opt(void **ptr, u8 type, u8 len, unsigned long val, size_t size)
{
	struct l2cap_conf_opt *opt = *ptr;

	BT_DBG("type 0x%2.2x len %u val 0x%lx", type, len, val);

	if (size < L2CAP_CONF_OPT_SIZE + len)
		return;

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

static void l2cap_add_opt_efs(void **ptr, struct l2cap_chan *chan, size_t size)
{
	struct l2cap_conf_efs efs;

	switch (chan->mode) {
	case L2CAP_MODE_ERTM:
		efs.id		= chan->local_id;
		efs.stype	= chan->local_stype;
		efs.msdu	= cpu_to_le16(chan->local_msdu);
		efs.sdu_itime	= cpu_to_le32(chan->local_sdu_itime);
		efs.acc_lat	= cpu_to_le32(L2CAP_DEFAULT_ACC_LAT);
		efs.flush_to	= cpu_to_le32(L2CAP_EFS_DEFAULT_FLUSH_TO);
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
			   (unsigned long) &efs, size);
}

static void l2cap_ack_timeout(struct work_struct *work)
{
	struct l2cap_chan *chan = container_of(work, struct l2cap_chan,
					       ack_timer.work);
	u16 frames_to_ack;

	BT_DBG("chan %p", chan);

	l2cap_chan_lock(chan);

	frames_to_ack = __seq_offset(chan, chan->buffer_seq,
				     chan->last_acked_seq);

	if (frames_to_ack)
		l2cap_send_rr_or_rnr(chan, 0);

	l2cap_chan_unlock(chan);
	l2cap_chan_put(chan);
}

int l2cap_ertm_init(struct l2cap_chan *chan)
{
	int err;

	chan->next_tx_seq = 0;
	chan->expected_tx_seq = 0;
	chan->expected_ack_seq = 0;
	chan->unacked_frames = 0;
	chan->buffer_seq = 0;
	chan->frames_sent = 0;
	chan->last_acked_seq = 0;
	chan->sdu = NULL;
	chan->sdu_last_frag = NULL;
	chan->sdu_len = 0;

	skb_queue_head_init(&chan->tx_q);

	if (chan->mode != L2CAP_MODE_ERTM)
		return 0;

	chan->rx_state = L2CAP_RX_STATE_RECV;
	chan->tx_state = L2CAP_TX_STATE_XMIT;

	skb_queue_head_init(&chan->srej_q);

	err = l2cap_seq_list_init(&chan->srej_list, chan->tx_win);
	if (err < 0)
		return err;

	err = l2cap_seq_list_init(&chan->retrans_list, chan->remote_tx_win);
	if (err < 0)
		l2cap_seq_list_free(&chan->srej_list);

	return err;
}

static inline __u8 l2cap_select_mode(__u8 mode, __u16 remote_feat_mask)
{
	switch (mode) {
	case L2CAP_MODE_STREAMING:
	case L2CAP_MODE_ERTM:
		if (l2cap_mode_supported(mode, remote_feat_mask))
			return mode;
		fallthrough;
	default:
		return L2CAP_MODE_BASIC;
	}
}

static inline bool __l2cap_ews_supported(struct l2cap_conn *conn)
{
	return (conn->feat_mask & L2CAP_FEAT_EXT_WINDOW);
}

static inline bool __l2cap_efs_supported(struct l2cap_conn *conn)
{
	return (conn->feat_mask & L2CAP_FEAT_EXT_FLOW);
}

static void __l2cap_set_ertm_timeouts(struct l2cap_chan *chan,
				      struct l2cap_conf_rfc *rfc)
{
	rfc->retrans_timeout = cpu_to_le16(L2CAP_DEFAULT_RETRANS_TO);
	rfc->monitor_timeout = cpu_to_le16(L2CAP_DEFAULT_MONITOR_TO);
}

static inline void l2cap_txwin_setup(struct l2cap_chan *chan)
{
	if (chan->tx_win > L2CAP_DEFAULT_TX_WINDOW &&
	    __l2cap_ews_supported(chan->conn)) {
		/* use extended control field */
		set_bit(FLAG_EXT_CTRL, &chan->flags);
		chan->tx_win_max = L2CAP_DEFAULT_EXT_WINDOW;
	} else {
		chan->tx_win = min_t(u16, chan->tx_win,
				     L2CAP_DEFAULT_TX_WINDOW);
		chan->tx_win_max = L2CAP_DEFAULT_TX_WINDOW;
	}
	chan->ack_win = chan->tx_win;
}

static void l2cap_mtu_auto(struct l2cap_chan *chan)
{
	struct hci_conn *conn = chan->conn->hcon;

	chan->imtu = L2CAP_DEFAULT_MIN_MTU;

	/* The 2-DH1 packet has between 2 and 56 information bytes
	 * (including the 2-byte payload header)
	 */
	if (!(conn->pkt_type & HCI_2DH1))
		chan->imtu = 54;

	/* The 3-DH1 packet has between 2 and 85 information bytes
	 * (including the 2-byte payload header)
	 */
	if (!(conn->pkt_type & HCI_3DH1))
		chan->imtu = 83;

	/* The 2-DH3 packet has between 2 and 369 information bytes
	 * (including the 2-byte payload header)
	 */
	if (!(conn->pkt_type & HCI_2DH3))
		chan->imtu = 367;

	/* The 3-DH3 packet has between 2 and 554 information bytes
	 * (including the 2-byte payload header)
	 */
	if (!(conn->pkt_type & HCI_3DH3))
		chan->imtu = 552;

	/* The 2-DH5 packet has between 2 and 681 information bytes
	 * (including the 2-byte payload header)
	 */
	if (!(conn->pkt_type & HCI_2DH5))
		chan->imtu = 679;

	/* The 3-DH5 packet has between 2 and 1023 information bytes
	 * (including the 2-byte payload header)
	 */
	if (!(conn->pkt_type & HCI_3DH5))
		chan->imtu = 1021;
}

static int l2cap_build_conf_req(struct l2cap_chan *chan, void *data, size_t data_size)
{
	struct l2cap_conf_req *req = data;
	struct l2cap_conf_rfc rfc = { .mode = chan->mode };
	void *ptr = req->data;
	void *endptr = data + data_size;
	u16 size;

	BT_DBG("chan %p", chan);

	if (chan->num_conf_req || chan->num_conf_rsp)
		goto done;

	switch (chan->mode) {
	case L2CAP_MODE_STREAMING:
	case L2CAP_MODE_ERTM:
		if (test_bit(CONF_STATE2_DEVICE, &chan->conf_state))
			break;

		if (__l2cap_efs_supported(chan->conn))
			set_bit(FLAG_EFS_ENABLE, &chan->flags);

		fallthrough;
	default:
		chan->mode = l2cap_select_mode(rfc.mode, chan->conn->feat_mask);
		break;
	}

done:
	if (chan->imtu != L2CAP_DEFAULT_MTU) {
		if (!chan->imtu)
			l2cap_mtu_auto(chan);
		l2cap_add_conf_opt(&ptr, L2CAP_CONF_MTU, 2, chan->imtu,
				   endptr - ptr);
	}

	switch (chan->mode) {
	case L2CAP_MODE_BASIC:
		if (disable_ertm)
			break;

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
				   (unsigned long) &rfc, endptr - ptr);
		break;

	case L2CAP_MODE_ERTM:
		rfc.mode            = L2CAP_MODE_ERTM;
		rfc.max_transmit    = chan->max_tx;

		__l2cap_set_ertm_timeouts(chan, &rfc);

		size = min_t(u16, L2CAP_DEFAULT_MAX_PDU_SIZE, chan->conn->mtu -
			     L2CAP_EXT_HDR_SIZE - L2CAP_SDULEN_SIZE -
			     L2CAP_FCS_SIZE);
		rfc.max_pdu_size = cpu_to_le16(size);

		l2cap_txwin_setup(chan);

		rfc.txwin_size = min_t(u16, chan->tx_win,
				       L2CAP_DEFAULT_TX_WINDOW);

		l2cap_add_conf_opt(&ptr, L2CAP_CONF_RFC, sizeof(rfc),
				   (unsigned long) &rfc, endptr - ptr);

		if (test_bit(FLAG_EFS_ENABLE, &chan->flags))
			l2cap_add_opt_efs(&ptr, chan, endptr - ptr);

		if (test_bit(FLAG_EXT_CTRL, &chan->flags))
			l2cap_add_conf_opt(&ptr, L2CAP_CONF_EWS, 2,
					   chan->tx_win, endptr - ptr);

		if (chan->conn->feat_mask & L2CAP_FEAT_FCS)
			if (chan->fcs == L2CAP_FCS_NONE ||
			    test_bit(CONF_RECV_NO_FCS, &chan->conf_state)) {
				chan->fcs = L2CAP_FCS_NONE;
				l2cap_add_conf_opt(&ptr, L2CAP_CONF_FCS, 1,
						   chan->fcs, endptr - ptr);
			}
		break;

	case L2CAP_MODE_STREAMING:
		l2cap_txwin_setup(chan);
		rfc.mode            = L2CAP_MODE_STREAMING;
		rfc.txwin_size      = 0;
		rfc.max_transmit    = 0;
		rfc.retrans_timeout = 0;
		rfc.monitor_timeout = 0;

		size = min_t(u16, L2CAP_DEFAULT_MAX_PDU_SIZE, chan->conn->mtu -
			     L2CAP_EXT_HDR_SIZE - L2CAP_SDULEN_SIZE -
			     L2CAP_FCS_SIZE);
		rfc.max_pdu_size = cpu_to_le16(size);

		l2cap_add_conf_opt(&ptr, L2CAP_CONF_RFC, sizeof(rfc),
				   (unsigned long) &rfc, endptr - ptr);

		if (test_bit(FLAG_EFS_ENABLE, &chan->flags))
			l2cap_add_opt_efs(&ptr, chan, endptr - ptr);

		if (chan->conn->feat_mask & L2CAP_FEAT_FCS)
			if (chan->fcs == L2CAP_FCS_NONE ||
			    test_bit(CONF_RECV_NO_FCS, &chan->conf_state)) {
				chan->fcs = L2CAP_FCS_NONE;
				l2cap_add_conf_opt(&ptr, L2CAP_CONF_FCS, 1,
						   chan->fcs, endptr - ptr);
			}
		break;
	}

	req->dcid  = cpu_to_le16(chan->dcid);
	req->flags = cpu_to_le16(0);

	return ptr - data;
}

static int l2cap_parse_conf_req(struct l2cap_chan *chan, void *data, size_t data_size)
{
	struct l2cap_conf_rsp *rsp = data;
	void *ptr = rsp->data;
	void *endptr = data + data_size;
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
		if (len < 0)
			break;

		hint  = type & L2CAP_CONF_HINT;
		type &= L2CAP_CONF_MASK;

		switch (type) {
		case L2CAP_CONF_MTU:
			if (olen != 2)
				break;
			mtu = val;
			break;

		case L2CAP_CONF_FLUSH_TO:
			if (olen != 2)
				break;
			chan->flush_to = val;
			break;

		case L2CAP_CONF_QOS:
			break;

		case L2CAP_CONF_RFC:
			if (olen != sizeof(rfc))
				break;
			memcpy(&rfc, (void *) val, olen);
			break;

		case L2CAP_CONF_FCS:
			if (olen != 1)
				break;
			if (val == L2CAP_FCS_NONE)
				set_bit(CONF_RECV_NO_FCS, &chan->conf_state);
			break;

		case L2CAP_CONF_EFS:
			if (olen != sizeof(efs))
				break;
			remote_efs = 1;
			memcpy(&efs, (void *) val, olen);
			break;

		case L2CAP_CONF_EWS:
			if (olen != 2)
				break;
			return -ECONNREFUSED;

		default:
			if (hint)
				break;
			result = L2CAP_CONF_UNKNOWN;
			l2cap_add_conf_opt(&ptr, (u8)type, sizeof(u8), type, endptr - ptr);
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
			if (__l2cap_efs_supported(chan->conn))
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

		l2cap_add_conf_opt(&ptr, L2CAP_CONF_RFC, sizeof(rfc),
				   (unsigned long) &rfc, endptr - ptr);
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
		l2cap_add_conf_opt(&ptr, L2CAP_CONF_MTU, 2, chan->omtu, endptr - ptr);

		if (remote_efs) {
			if (chan->local_stype != L2CAP_SERV_NOTRAFIC &&
			    efs.stype != L2CAP_SERV_NOTRAFIC &&
			    efs.stype != chan->local_stype) {

				result = L2CAP_CONF_UNACCEPT;

				if (chan->num_conf_req >= 1)
					return -ECONNREFUSED;

				l2cap_add_conf_opt(&ptr, L2CAP_CONF_EFS,
						   sizeof(efs),
						   (unsigned long) &efs, endptr - ptr);
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
				     chan->conn->mtu - L2CAP_EXT_HDR_SIZE -
				     L2CAP_SDULEN_SIZE - L2CAP_FCS_SIZE);
			rfc.max_pdu_size = cpu_to_le16(size);
			chan->remote_mps = size;

			__l2cap_set_ertm_timeouts(chan, &rfc);

			set_bit(CONF_MODE_DONE, &chan->conf_state);

			l2cap_add_conf_opt(&ptr, L2CAP_CONF_RFC,
					   sizeof(rfc), (unsigned long) &rfc, endptr - ptr);

			if (remote_efs &&
			    test_bit(FLAG_EFS_ENABLE, &chan->flags)) {
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
						   sizeof(efs),
						   (unsigned long) &efs, endptr - ptr);
			}
			break;

		case L2CAP_MODE_STREAMING:
			size = min_t(u16, le16_to_cpu(rfc.max_pdu_size),
				     chan->conn->mtu - L2CAP_EXT_HDR_SIZE -
				     L2CAP_SDULEN_SIZE - L2CAP_FCS_SIZE);
			rfc.max_pdu_size = cpu_to_le16(size);
			chan->remote_mps = size;

			set_bit(CONF_MODE_DONE, &chan->conf_state);

			l2cap_add_conf_opt(&ptr, L2CAP_CONF_RFC, sizeof(rfc),
					   (unsigned long) &rfc, endptr - ptr);

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
	rsp->flags  = cpu_to_le16(0);

	return ptr - data;
}

static int l2cap_parse_conf_rsp(struct l2cap_chan *chan, void *rsp, int len,
				void *data, size_t size, u16 *result)
{
	struct l2cap_conf_req *req = data;
	void *ptr = req->data;
	void *endptr = data + size;
	int type, olen;
	unsigned long val;
	struct l2cap_conf_rfc rfc = { .mode = L2CAP_MODE_BASIC };
	struct l2cap_conf_efs efs;

	BT_DBG("chan %p, rsp %p, len %d, req %p", chan, rsp, len, data);

	while (len >= L2CAP_CONF_OPT_SIZE) {
		len -= l2cap_get_conf_opt(&rsp, &type, &olen, &val);
		if (len < 0)
			break;

		switch (type) {
		case L2CAP_CONF_MTU:
			if (olen != 2)
				break;
			if (val < L2CAP_DEFAULT_MIN_MTU) {
				*result = L2CAP_CONF_UNACCEPT;
				chan->imtu = L2CAP_DEFAULT_MIN_MTU;
			} else
				chan->imtu = val;
			l2cap_add_conf_opt(&ptr, L2CAP_CONF_MTU, 2, chan->imtu,
					   endptr - ptr);
			break;

		case L2CAP_CONF_FLUSH_TO:
			if (olen != 2)
				break;
			chan->flush_to = val;
			l2cap_add_conf_opt(&ptr, L2CAP_CONF_FLUSH_TO, 2,
					   chan->flush_to, endptr - ptr);
			break;

		case L2CAP_CONF_RFC:
			if (olen != sizeof(rfc))
				break;
			memcpy(&rfc, (void *)val, olen);
			if (test_bit(CONF_STATE2_DEVICE, &chan->conf_state) &&
			    rfc.mode != chan->mode)
				return -ECONNREFUSED;
			chan->fcs = 0;
			l2cap_add_conf_opt(&ptr, L2CAP_CONF_RFC, sizeof(rfc),
					   (unsigned long) &rfc, endptr - ptr);
			break;

		case L2CAP_CONF_EWS:
			if (olen != 2)
				break;
			chan->ack_win = min_t(u16, val, chan->ack_win);
			l2cap_add_conf_opt(&ptr, L2CAP_CONF_EWS, 2,
					   chan->tx_win, endptr - ptr);
			break;

		case L2CAP_CONF_EFS:
			if (olen != sizeof(efs))
				break;
			memcpy(&efs, (void *)val, olen);
			if (chan->local_stype != L2CAP_SERV_NOTRAFIC &&
			    efs.stype != L2CAP_SERV_NOTRAFIC &&
			    efs.stype != chan->local_stype)
				return -ECONNREFUSED;
			l2cap_add_conf_opt(&ptr, L2CAP_CONF_EFS, sizeof(efs),
					   (unsigned long) &efs, endptr - ptr);
			break;

		case L2CAP_CONF_FCS:
			if (olen != 1)
				break;
			if (*result == L2CAP_CONF_PENDING)
				if (val == L2CAP_FCS_NONE)
					set_bit(CONF_RECV_NO_FCS,
						&chan->conf_state);
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
			if (!test_bit(FLAG_EXT_CTRL, &chan->flags))
				chan->ack_win = min_t(u16, chan->ack_win,
						      rfc.txwin_size);

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
	req->flags  = cpu_to_le16(0);

	return ptr - data;
}

static int l2cap_build_conf_rsp(struct l2cap_chan *chan, void *data,
				u16 result, u16 flags)
{
	struct l2cap_conf_rsp *rsp = data;
	void *ptr = rsp->data;

	BT_DBG("chan %p", chan);

	rsp->scid   = cpu_to_le16(chan->dcid);
	rsp->result = cpu_to_le16(result);
	rsp->flags  = cpu_to_le16(flags);

	return ptr - data;
}

void __l2cap_le_connect_rsp_defer(struct l2cap_chan *chan)
{
	struct l2cap_le_conn_rsp rsp;
	struct l2cap_conn *conn = chan->conn;

	BT_DBG("chan %p", chan);

	rsp.dcid    = cpu_to_le16(chan->scid);
	rsp.mtu     = cpu_to_le16(chan->imtu);
	rsp.mps     = cpu_to_le16(chan->mps);
	rsp.credits = cpu_to_le16(chan->rx_credits);
	rsp.result  = cpu_to_le16(L2CAP_CR_LE_SUCCESS);

	l2cap_send_cmd(conn, chan->ident, L2CAP_LE_CONN_RSP, sizeof(rsp),
		       &rsp);
}

static void l2cap_ecred_list_defer(struct l2cap_chan *chan, void *data)
{
	int *result = data;

	if (*result || test_bit(FLAG_ECRED_CONN_REQ_SENT, &chan->flags))
		return;

	switch (chan->state) {
	case BT_CONNECT2:
		/* If channel still pending accept add to result */
		(*result)++;
		return;
	case BT_CONNECTED:
		return;
	default:
		/* If not connected or pending accept it has been refused */
		*result = -ECONNREFUSED;
		return;
	}
}

struct l2cap_ecred_rsp_data {
	struct {
		struct l2cap_ecred_conn_rsp_hdr rsp;
		__le16 scid[L2CAP_ECRED_MAX_CID];
	} __packed pdu;
	int count;
};

static void l2cap_ecred_rsp_defer(struct l2cap_chan *chan, void *data)
{
	struct l2cap_ecred_rsp_data *rsp = data;
	struct l2cap_ecred_conn_rsp *rsp_flex =
		container_of(&rsp->pdu.rsp, struct l2cap_ecred_conn_rsp, hdr);

	/* Check if channel for outgoing connection or if it wasn't deferred
	 * since in those cases it must be skipped.
	 */
	if (test_bit(FLAG_ECRED_CONN_REQ_SENT, &chan->flags) ||
	    !test_and_clear_bit(FLAG_DEFER_SETUP, &chan->flags))
		return;

	/* Reset ident so only one response is sent */
	chan->ident = 0;

	/* Include all channels pending with the same ident */
	if (!rsp->pdu.rsp.result)
		rsp_flex->dcid[rsp->count++] = cpu_to_le16(chan->scid);
	else
		l2cap_chan_del(chan, ECONNRESET);
}

void __l2cap_ecred_conn_rsp_defer(struct l2cap_chan *chan)
{
	struct l2cap_conn *conn = chan->conn;
	struct l2cap_ecred_rsp_data data;
	u16 id = chan->ident;
	int result = 0;

	if (!id)
		return;

	BT_DBG("chan %p id %d", chan, id);

	memset(&data, 0, sizeof(data));

	data.pdu.rsp.mtu     = cpu_to_le16(chan->imtu);
	data.pdu.rsp.mps     = cpu_to_le16(chan->mps);
	data.pdu.rsp.credits = cpu_to_le16(chan->rx_credits);
	data.pdu.rsp.result  = cpu_to_le16(L2CAP_CR_LE_SUCCESS);

	/* Verify that all channels are ready */
	__l2cap_chan_list_id(conn, id, l2cap_ecred_list_defer, &result);

	if (result > 0)
		return;

	if (result < 0)
		data.pdu.rsp.result = cpu_to_le16(L2CAP_CR_LE_AUTHORIZATION);

	/* Build response */
	__l2cap_chan_list_id(conn, id, l2cap_ecred_rsp_defer, &data);

	l2cap_send_cmd(conn, id, L2CAP_ECRED_CONN_RSP,
		       sizeof(data.pdu.rsp) + (data.count * sizeof(__le16)),
		       &data.pdu);
}

void __l2cap_connect_rsp_defer(struct l2cap_chan *chan)
{
	struct l2cap_conn_rsp rsp;
	struct l2cap_conn *conn = chan->conn;
	u8 buf[128];
	u8 rsp_code;

	rsp.scid   = cpu_to_le16(chan->dcid);
	rsp.dcid   = cpu_to_le16(chan->scid);
	rsp.result = cpu_to_le16(L2CAP_CR_SUCCESS);
	rsp.status = cpu_to_le16(L2CAP_CS_NO_INFO);
	rsp_code = L2CAP_CONN_RSP;

	BT_DBG("chan %p rsp_code %u", chan, rsp_code);

	l2cap_send_cmd(conn, chan->ident, rsp_code, sizeof(rsp), &rsp);

	if (test_and_set_bit(CONF_REQ_SENT, &chan->conf_state))
		return;

	l2cap_send_cmd(conn, l2cap_get_ident(conn), L2CAP_CONF_REQ,
		       l2cap_build_conf_req(chan, buf, sizeof(buf)), buf);
	chan->num_conf_req++;
}

static void l2cap_conf_rfc_get(struct l2cap_chan *chan, void *rsp, int len)
{
	int type, olen;
	unsigned long val;
	/* Use sane default values in case a misbehaving remote device
	 * did not send an RFC or extended window size option.
	 */
	u16 txwin_ext = chan->ack_win;
	struct l2cap_conf_rfc rfc = {
		.mode = chan->mode,
		.retrans_timeout = cpu_to_le16(L2CAP_DEFAULT_RETRANS_TO),
		.monitor_timeout = cpu_to_le16(L2CAP_DEFAULT_MONITOR_TO),
		.max_pdu_size = cpu_to_le16(chan->imtu),
		.txwin_size = min_t(u16, chan->ack_win, L2CAP_DEFAULT_TX_WINDOW),
	};

	BT_DBG("chan %p, rsp %p, len %d", chan, rsp, len);

	if ((chan->mode != L2CAP_MODE_ERTM) && (chan->mode != L2CAP_MODE_STREAMING))
		return;

	while (len >= L2CAP_CONF_OPT_SIZE) {
		len -= l2cap_get_conf_opt(&rsp, &type, &olen, &val);
		if (len < 0)
			break;

		switch (type) {
		case L2CAP_CONF_RFC:
			if (olen != sizeof(rfc))
				break;
			memcpy(&rfc, (void *)val, olen);
			break;
		case L2CAP_CONF_EWS:
			if (olen != 2)
				break;
			txwin_ext = val;
			break;
		}
	}

	switch (rfc.mode) {
	case L2CAP_MODE_ERTM:
		chan->retrans_timeout = le16_to_cpu(rfc.retrans_timeout);
		chan->monitor_timeout = le16_to_cpu(rfc.monitor_timeout);
		chan->mps = le16_to_cpu(rfc.max_pdu_size);
		if (test_bit(FLAG_EXT_CTRL, &chan->flags))
			chan->ack_win = min_t(u16, chan->ack_win, txwin_ext);
		else
			chan->ack_win = min_t(u16, chan->ack_win,
					      rfc.txwin_size);
		break;
	case L2CAP_MODE_STREAMING:
		chan->mps    = le16_to_cpu(rfc.max_pdu_size);
	}
}

static inline int l2cap_command_rej(struct l2cap_conn *conn,
				    struct l2cap_cmd_hdr *cmd, u16 cmd_len,
				    u8 *data)
{
	struct l2cap_cmd_rej_unk *rej = (struct l2cap_cmd_rej_unk *) data;

	if (cmd_len < sizeof(*rej))
		return -EPROTO;

	if (rej->reason != L2CAP_REJ_NOT_UNDERSTOOD)
		return 0;

	if ((conn->info_state & L2CAP_INFO_FEAT_MASK_REQ_SENT) &&
	    cmd->ident == conn->info_ident) {
		cancel_delayed_work(&conn->info_timer);

		conn->info_state |= L2CAP_INFO_FEAT_MASK_REQ_DONE;
		conn->info_ident = 0;

		l2cap_conn_start(conn);
	}

	return 0;
}

static void l2cap_connect(struct l2cap_conn *conn, struct l2cap_cmd_hdr *cmd,
			  u8 *data, u8 rsp_code)
{
	struct l2cap_conn_req *req = (struct l2cap_conn_req *) data;
	struct l2cap_conn_rsp rsp;
	struct l2cap_chan *chan = NULL, *pchan = NULL;
	int result, status = L2CAP_CS_NO_INFO;

	u16 dcid = 0, scid = __le16_to_cpu(req->scid);
	__le16 psm = req->psm;

	BT_DBG("psm 0x%2.2x scid 0x%4.4x", __le16_to_cpu(psm), scid);

	/* Check if we have socket listening on psm */
	pchan = l2cap_global_chan_by_psm(BT_LISTEN, psm, &conn->hcon->src,
					 &conn->hcon->dst, ACL_LINK);
	if (!pchan) {
		result = L2CAP_CR_BAD_PSM;
		goto response;
	}

	l2cap_chan_lock(pchan);

	/* Check if the ACL is secure enough (if not SDP) */
	if (psm != cpu_to_le16(L2CAP_PSM_SDP) &&
	    !hci_conn_check_link_mode(conn->hcon)) {
		conn->disc_reason = HCI_ERROR_AUTH_FAILURE;
		result = L2CAP_CR_SEC_BLOCK;
		goto response;
	}

	result = L2CAP_CR_NO_MEM;

	/* Check for valid dynamic CID range (as per Erratum 3253) */
	if (scid < L2CAP_CID_DYN_START || scid > L2CAP_CID_DYN_END) {
		result = L2CAP_CR_INVALID_SCID;
		goto response;
	}

	/* Check if we already have channel with that dcid */
	if (__l2cap_get_chan_by_dcid(conn, scid)) {
		result = L2CAP_CR_SCID_IN_USE;
		goto response;
	}

	chan = pchan->ops->new_connection(pchan);
	if (!chan)
		goto response;

	/* For certain devices (ex: HID mouse), support for authentication,
	 * pairing and bonding is optional. For such devices, inorder to avoid
	 * the ACL alive for too long after L2CAP disconnection, reset the ACL
	 * disc_timeout back to HCI_DISCONN_TIMEOUT during L2CAP connect.
	 */
	conn->hcon->disc_timeout = HCI_DISCONN_TIMEOUT;

	bacpy(&chan->src, &conn->hcon->src);
	bacpy(&chan->dst, &conn->hcon->dst);
	chan->src_type = bdaddr_src_type(conn->hcon);
	chan->dst_type = bdaddr_dst_type(conn->hcon);
	chan->psm  = psm;
	chan->dcid = scid;

	__l2cap_chan_add(conn, chan);

	dcid = chan->scid;

	__set_chan_timer(chan, chan->ops->get_sndtimeo(chan));

	chan->ident = cmd->ident;

	if (conn->info_state & L2CAP_INFO_FEAT_MASK_REQ_DONE) {
		if (l2cap_chan_check_security(chan, false)) {
			if (test_bit(FLAG_DEFER_SETUP, &chan->flags)) {
				l2cap_state_change(chan, BT_CONNECT2);
				result = L2CAP_CR_PEND;
				status = L2CAP_CS_AUTHOR_PEND;
				chan->ops->defer(chan);
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
	rsp.scid   = cpu_to_le16(scid);
	rsp.dcid   = cpu_to_le16(dcid);
	rsp.result = cpu_to_le16(result);
	rsp.status = cpu_to_le16(status);
	l2cap_send_cmd(conn, cmd->ident, rsp_code, sizeof(rsp), &rsp);

	if (!pchan)
		return;

	if (result == L2CAP_CR_PEND && status == L2CAP_CS_NO_INFO) {
		struct l2cap_info_req info;
		info.type = cpu_to_le16(L2CAP_IT_FEAT_MASK);

		conn->info_state |= L2CAP_INFO_FEAT_MASK_REQ_SENT;
		conn->info_ident = l2cap_get_ident(conn);

		schedule_delayed_work(&conn->info_timer, L2CAP_INFO_TIMEOUT);

		l2cap_send_cmd(conn, conn->info_ident, L2CAP_INFO_REQ,
			       sizeof(info), &info);
	}

	if (chan && !test_bit(CONF_REQ_SENT, &chan->conf_state) &&
	    result == L2CAP_CR_SUCCESS) {
		u8 buf[128];
		set_bit(CONF_REQ_SENT, &chan->conf_state);
		l2cap_send_cmd(conn, l2cap_get_ident(conn), L2CAP_CONF_REQ,
			       l2cap_build_conf_req(chan, buf, sizeof(buf)), buf);
		chan->num_conf_req++;
	}

	l2cap_chan_unlock(pchan);
	l2cap_chan_put(pchan);
}

static int l2cap_connect_req(struct l2cap_conn *conn,
			     struct l2cap_cmd_hdr *cmd, u16 cmd_len, u8 *data)
{
	if (cmd_len < sizeof(struct l2cap_conn_req))
		return -EPROTO;

	l2cap_connect(conn, cmd, data, L2CAP_CONN_RSP);
	return 0;
}

static int l2cap_connect_create_rsp(struct l2cap_conn *conn,
				    struct l2cap_cmd_hdr *cmd, u16 cmd_len,
				    u8 *data)
{
	struct l2cap_conn_rsp *rsp = (struct l2cap_conn_rsp *) data;
	u16 scid, dcid, result, status;
	struct l2cap_chan *chan;
	u8 req[128];
	int err;

	if (cmd_len < sizeof(*rsp))
		return -EPROTO;

	scid   = __le16_to_cpu(rsp->scid);
	dcid   = __le16_to_cpu(rsp->dcid);
	result = __le16_to_cpu(rsp->result);
	status = __le16_to_cpu(rsp->status);

	if (result == L2CAP_CR_SUCCESS && (dcid < L2CAP_CID_DYN_START ||
					   dcid > L2CAP_CID_DYN_END))
		return -EPROTO;

	BT_DBG("dcid 0x%4.4x scid 0x%4.4x result 0x%2.2x status 0x%2.2x",
	       dcid, scid, result, status);

	if (scid) {
		chan = __l2cap_get_chan_by_scid(conn, scid);
		if (!chan)
			return -EBADSLT;
	} else {
		chan = __l2cap_get_chan_by_ident(conn, cmd->ident);
		if (!chan)
			return -EBADSLT;
	}

	chan = l2cap_chan_hold_unless_zero(chan);
	if (!chan)
		return -EBADSLT;

	err = 0;

	l2cap_chan_lock(chan);

	switch (result) {
	case L2CAP_CR_SUCCESS:
		if (__l2cap_get_chan_by_dcid(conn, dcid)) {
			err = -EBADSLT;
			break;
		}

		l2cap_state_change(chan, BT_CONFIG);
		chan->ident = 0;
		chan->dcid = dcid;
		clear_bit(CONF_CONNECT_PEND, &chan->conf_state);

		if (test_and_set_bit(CONF_REQ_SENT, &chan->conf_state))
			break;

		l2cap_send_cmd(conn, l2cap_get_ident(conn), L2CAP_CONF_REQ,
			       l2cap_build_conf_req(chan, req, sizeof(req)), req);
		chan->num_conf_req++;
		break;

	case L2CAP_CR_PEND:
		set_bit(CONF_CONNECT_PEND, &chan->conf_state);
		break;

	default:
		l2cap_chan_del(chan, ECONNREFUSED);
		break;
	}

	l2cap_chan_unlock(chan);
	l2cap_chan_put(chan);

	return err;
}

static inline void set_default_fcs(struct l2cap_chan *chan)
{
	/* FCS is enabled only in ERTM or streaming mode, if one or both
	 * sides request it.
	 */
	if (chan->mode != L2CAP_MODE_ERTM && chan->mode != L2CAP_MODE_STREAMING)
		chan->fcs = L2CAP_FCS_NONE;
	else if (!test_bit(CONF_RECV_NO_FCS, &chan->conf_state))
		chan->fcs = L2CAP_FCS_CRC16;
}

static void l2cap_send_efs_conf_rsp(struct l2cap_chan *chan, void *data,
				    u8 ident, u16 flags)
{
	struct l2cap_conn *conn = chan->conn;

	BT_DBG("conn %p chan %p ident %d flags 0x%4.4x", conn, chan, ident,
	       flags);

	clear_bit(CONF_LOC_CONF_PEND, &chan->conf_state);
	set_bit(CONF_OUTPUT_DONE, &chan->conf_state);

	l2cap_send_cmd(conn, ident, L2CAP_CONF_RSP,
		       l2cap_build_conf_rsp(chan, data,
					    L2CAP_CONF_SUCCESS, flags), data);
}

static void cmd_reject_invalid_cid(struct l2cap_conn *conn, u8 ident,
				   u16 scid, u16 dcid)
{
	struct l2cap_cmd_rej_cid rej;

	rej.reason = cpu_to_le16(L2CAP_REJ_INVALID_CID);
	rej.scid = __cpu_to_le16(scid);
	rej.dcid = __cpu_to_le16(dcid);

	l2cap_send_cmd(conn, ident, L2CAP_COMMAND_REJ, sizeof(rej), &rej);
}

static inline int l2cap_config_req(struct l2cap_conn *conn,
				   struct l2cap_cmd_hdr *cmd, u16 cmd_len,
				   u8 *data)
{
	struct l2cap_conf_req *req = (struct l2cap_conf_req *) data;
	u16 dcid, flags;
	u8 rsp[64];
	struct l2cap_chan *chan;
	int len, err = 0;

	if (cmd_len < sizeof(*req))
		return -EPROTO;

	dcid  = __le16_to_cpu(req->dcid);
	flags = __le16_to_cpu(req->flags);

	BT_DBG("dcid 0x%4.4x flags 0x%2.2x", dcid, flags);

	chan = l2cap_get_chan_by_scid(conn, dcid);
	if (!chan) {
		cmd_reject_invalid_cid(conn, cmd->ident, dcid, 0);
		return 0;
	}

	if (chan->state != BT_CONFIG && chan->state != BT_CONNECT2 &&
	    chan->state != BT_CONNECTED) {
		cmd_reject_invalid_cid(conn, cmd->ident, chan->scid,
				       chan->dcid);
		goto unlock;
	}

	/* Reject if config buffer is too small. */
	len = cmd_len - sizeof(*req);
	if (chan->conf_len + len > sizeof(chan->conf_req)) {
		l2cap_send_cmd(conn, cmd->ident, L2CAP_CONF_RSP,
			       l2cap_build_conf_rsp(chan, rsp,
			       L2CAP_CONF_REJECT, flags), rsp);
		goto unlock;
	}

	/* Store config. */
	memcpy(chan->conf_req + chan->conf_len, req->data, len);
	chan->conf_len += len;

	if (flags & L2CAP_CONF_FLAG_CONTINUATION) {
		/* Incomplete config. Send empty response. */
		l2cap_send_cmd(conn, cmd->ident, L2CAP_CONF_RSP,
			       l2cap_build_conf_rsp(chan, rsp,
			       L2CAP_CONF_SUCCESS, flags), rsp);
		goto unlock;
	}

	/* Complete config. */
	len = l2cap_parse_conf_req(chan, rsp, sizeof(rsp));
	if (len < 0) {
		l2cap_send_disconn_req(chan, ECONNRESET);
		goto unlock;
	}

	chan->ident = cmd->ident;
	l2cap_send_cmd(conn, cmd->ident, L2CAP_CONF_RSP, len, rsp);
	if (chan->num_conf_rsp < L2CAP_CONF_MAX_CONF_RSP)
		chan->num_conf_rsp++;

	/* Reset config buffer. */
	chan->conf_len = 0;

	if (!test_bit(CONF_OUTPUT_DONE, &chan->conf_state))
		goto unlock;

	if (test_bit(CONF_INPUT_DONE, &chan->conf_state)) {
		set_default_fcs(chan);

		if (chan->mode == L2CAP_MODE_ERTM ||
		    chan->mode == L2CAP_MODE_STREAMING)
			err = l2cap_ertm_init(chan);

		if (err < 0)
			l2cap_send_disconn_req(chan, -err);
		else
			l2cap_chan_ready(chan);

		goto unlock;
	}

	if (!test_and_set_bit(CONF_REQ_SENT, &chan->conf_state)) {
		u8 buf[64];
		l2cap_send_cmd(conn, l2cap_get_ident(conn), L2CAP_CONF_REQ,
			       l2cap_build_conf_req(chan, buf, sizeof(buf)), buf);
		chan->num_conf_req++;
	}

	/* Got Conf Rsp PENDING from remote side and assume we sent
	   Conf Rsp PENDING in the code above */
	if (test_bit(CONF_REM_CONF_PEND, &chan->conf_state) &&
	    test_bit(CONF_LOC_CONF_PEND, &chan->conf_state)) {

		/* check compatibility */

		/* Send rsp for BR/EDR channel */
		l2cap_send_efs_conf_rsp(chan, rsp, cmd->ident, flags);
	}

unlock:
	l2cap_chan_unlock(chan);
	l2cap_chan_put(chan);
	return err;
}

static inline int l2cap_config_rsp(struct l2cap_conn *conn,
				   struct l2cap_cmd_hdr *cmd, u16 cmd_len,
				   u8 *data)
{
	struct l2cap_conf_rsp *rsp = (struct l2cap_conf_rsp *)data;
	u16 scid, flags, result;
	struct l2cap_chan *chan;
	int len = cmd_len - sizeof(*rsp);
	int err = 0;

	if (cmd_len < sizeof(*rsp))
		return -EPROTO;

	scid   = __le16_to_cpu(rsp->scid);
	flags  = __le16_to_cpu(rsp->flags);
	result = __le16_to_cpu(rsp->result);

	BT_DBG("scid 0x%4.4x flags 0x%2.2x result 0x%2.2x len %d", scid, flags,
	       result, len);

	chan = l2cap_get_chan_by_scid(conn, scid);
	if (!chan)
		return 0;

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
						   buf, sizeof(buf), &result);
			if (len < 0) {
				l2cap_send_disconn_req(chan, ECONNRESET);
				goto done;
			}

			l2cap_send_efs_conf_rsp(chan, buf, cmd->ident, 0);
		}
		goto done;

	case L2CAP_CONF_UNKNOWN:
	case L2CAP_CONF_UNACCEPT:
		if (chan->num_conf_rsp <= L2CAP_CONF_MAX_CONF_RSP) {
			char req[64];

			if (len > sizeof(req) - sizeof(struct l2cap_conf_req)) {
				l2cap_send_disconn_req(chan, ECONNRESET);
				goto done;
			}

			/* throw out any old stored conf requests */
			result = L2CAP_CONF_SUCCESS;
			len = l2cap_parse_conf_rsp(chan, rsp->data, len,
						   req, sizeof(req), &result);
			if (len < 0) {
				l2cap_send_disconn_req(chan, ECONNRESET);
				goto done;
			}

			l2cap_send_cmd(conn, l2cap_get_ident(conn),
				       L2CAP_CONF_REQ, len, req);
			chan->num_conf_req++;
			if (result != L2CAP_CONF_SUCCESS)
				goto done;
			break;
		}
		fallthrough;

	default:
		l2cap_chan_set_err(chan, ECONNRESET);

		__set_chan_timer(chan, L2CAP_DISC_REJ_TIMEOUT);
		l2cap_send_disconn_req(chan, ECONNRESET);
		goto done;
	}

	if (flags & L2CAP_CONF_FLAG_CONTINUATION)
		goto done;

	set_bit(CONF_INPUT_DONE, &chan->conf_state);

	if (test_bit(CONF_OUTPUT_DONE, &chan->conf_state)) {
		set_default_fcs(chan);

		if (chan->mode == L2CAP_MODE_ERTM ||
		    chan->mode == L2CAP_MODE_STREAMING)
			err = l2cap_ertm_init(chan);

		if (err < 0)
			l2cap_send_disconn_req(chan, -err);
		else
			l2cap_chan_ready(chan);
	}

done:
	l2cap_chan_unlock(chan);
	l2cap_chan_put(chan);
	return err;
}

static inline int l2cap_disconnect_req(struct l2cap_conn *conn,
				       struct l2cap_cmd_hdr *cmd, u16 cmd_len,
				       u8 *data)
{
	struct l2cap_disconn_req *req = (struct l2cap_disconn_req *) data;
	struct l2cap_disconn_rsp rsp;
	u16 dcid, scid;
	struct l2cap_chan *chan;

	if (cmd_len != sizeof(*req))
		return -EPROTO;

	scid = __le16_to_cpu(req->scid);
	dcid = __le16_to_cpu(req->dcid);

	BT_DBG("scid 0x%4.4x dcid 0x%4.4x", scid, dcid);

	chan = l2cap_get_chan_by_scid(conn, dcid);
	if (!chan) {
		cmd_reject_invalid_cid(conn, cmd->ident, dcid, scid);
		return 0;
	}

	rsp.dcid = cpu_to_le16(chan->scid);
	rsp.scid = cpu_to_le16(chan->dcid);
	l2cap_send_cmd(conn, cmd->ident, L2CAP_DISCONN_RSP, sizeof(rsp), &rsp);

	chan->ops->set_shutdown(chan);

	l2cap_chan_del(chan, ECONNRESET);

	chan->ops->close(chan);

	l2cap_chan_unlock(chan);
	l2cap_chan_put(chan);

	return 0;
}

static inline int l2cap_disconnect_rsp(struct l2cap_conn *conn,
				       struct l2cap_cmd_hdr *cmd, u16 cmd_len,
				       u8 *data)
{
	struct l2cap_disconn_rsp *rsp = (struct l2cap_disconn_rsp *) data;
	u16 dcid, scid;
	struct l2cap_chan *chan;

	if (cmd_len != sizeof(*rsp))
		return -EPROTO;

	scid = __le16_to_cpu(rsp->scid);
	dcid = __le16_to_cpu(rsp->dcid);

	BT_DBG("dcid 0x%4.4x scid 0x%4.4x", dcid, scid);

	chan = l2cap_get_chan_by_scid(conn, scid);
	if (!chan) {
		return 0;
	}

	if (chan->state != BT_DISCONN) {
		l2cap_chan_unlock(chan);
		l2cap_chan_put(chan);
		return 0;
	}

	l2cap_chan_del(chan, 0);

	chan->ops->close(chan);

	l2cap_chan_unlock(chan);
	l2cap_chan_put(chan);

	return 0;
}

static inline int l2cap_information_req(struct l2cap_conn *conn,
					struct l2cap_cmd_hdr *cmd, u16 cmd_len,
					u8 *data)
{
	struct l2cap_info_req *req = (struct l2cap_info_req *) data;
	u16 type;

	if (cmd_len != sizeof(*req))
		return -EPROTO;

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
		l2cap_send_cmd(conn, cmd->ident, L2CAP_INFO_RSP, sizeof(buf),
			       buf);
	} else if (type == L2CAP_IT_FIXED_CHAN) {
		u8 buf[12];
		struct l2cap_info_rsp *rsp = (struct l2cap_info_rsp *) buf;

		rsp->type   = cpu_to_le16(L2CAP_IT_FIXED_CHAN);
		rsp->result = cpu_to_le16(L2CAP_IR_SUCCESS);
		rsp->data[0] = conn->local_fixed_chan;
		memset(rsp->data + 1, 0, 7);
		l2cap_send_cmd(conn, cmd->ident, L2CAP_INFO_RSP, sizeof(buf),
			       buf);
	} else {
		struct l2cap_info_rsp rsp;
		rsp.type   = cpu_to_le16(type);
		rsp.result = cpu_to_le16(L2CAP_IR_NOTSUPP);
		l2cap_send_cmd(conn, cmd->ident, L2CAP_INFO_RSP, sizeof(rsp),
			       &rsp);
	}

	return 0;
}

static inline int l2cap_information_rsp(struct l2cap_conn *conn,
					struct l2cap_cmd_hdr *cmd, u16 cmd_len,
					u8 *data)
{
	struct l2cap_info_rsp *rsp = (struct l2cap_info_rsp *) data;
	u16 type, result;

	if (cmd_len < sizeof(*rsp))
		return -EPROTO;

	type   = __le16_to_cpu(rsp->type);
	result = __le16_to_cpu(rsp->result);

	BT_DBG("type 0x%4.4x result 0x%2.2x", type, result);

	/* L2CAP Info req/rsp are unbound to channels, add extra checks */
	if (cmd->ident != conn->info_ident ||
	    conn->info_state & L2CAP_INFO_FEAT_MASK_REQ_DONE)
		return 0;

	cancel_delayed_work(&conn->info_timer);

	if (result != L2CAP_IR_SUCCESS) {
		conn->info_state |= L2CAP_INFO_FEAT_MASK_REQ_DONE;
		conn->info_ident = 0;

		l2cap_conn_start(conn);

		return 0;
	}

	switch (type) {
	case L2CAP_IT_FEAT_MASK:
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
		break;

	case L2CAP_IT_FIXED_CHAN:
		conn->remote_fixed_chan = rsp->data[0];
		conn->info_state |= L2CAP_INFO_FEAT_MASK_REQ_DONE;
		conn->info_ident = 0;

		l2cap_conn_start(conn);
		break;
	}

	return 0;
}

static inline int l2cap_conn_param_update_req(struct l2cap_conn *conn,
					      struct l2cap_cmd_hdr *cmd,
					      u16 cmd_len, u8 *data)
{
	struct hci_conn *hcon = conn->hcon;
	struct l2cap_conn_param_update_req *req;
	struct l2cap_conn_param_update_rsp rsp;
	u16 min, max, latency, to_multiplier;
	int err;

	if (hcon->role != HCI_ROLE_MASTER)
		return -EINVAL;

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

	err = hci_check_conn_params(min, max, latency, to_multiplier);
	if (err)
		rsp.result = cpu_to_le16(L2CAP_CONN_PARAM_REJECTED);
	else
		rsp.result = cpu_to_le16(L2CAP_CONN_PARAM_ACCEPTED);

	l2cap_send_cmd(conn, cmd->ident, L2CAP_CONN_PARAM_UPDATE_RSP,
		       sizeof(rsp), &rsp);

	if (!err) {
		u8 store_hint;

		store_hint = hci_le_conn_update(hcon, min, max, latency,
						to_multiplier);
		mgmt_new_conn_param(hcon->hdev, &hcon->dst, hcon->dst_type,
				    store_hint, min, max, latency,
				    to_multiplier);

	}

	return 0;
}

static int l2cap_le_connect_rsp(struct l2cap_conn *conn,
				struct l2cap_cmd_hdr *cmd, u16 cmd_len,
				u8 *data)
{
	struct l2cap_le_conn_rsp *rsp = (struct l2cap_le_conn_rsp *) data;
	struct hci_conn *hcon = conn->hcon;
	u16 dcid, mtu, mps, credits, result;
	struct l2cap_chan *chan;
	int err, sec_level;

	if (cmd_len < sizeof(*rsp))
		return -EPROTO;

	dcid    = __le16_to_cpu(rsp->dcid);
	mtu     = __le16_to_cpu(rsp->mtu);
	mps     = __le16_to_cpu(rsp->mps);
	credits = __le16_to_cpu(rsp->credits);
	result  = __le16_to_cpu(rsp->result);

	if (result == L2CAP_CR_LE_SUCCESS && (mtu < 23 || mps < 23 ||
					   dcid < L2CAP_CID_DYN_START ||
					   dcid > L2CAP_CID_LE_DYN_END))
		return -EPROTO;

	BT_DBG("dcid 0x%4.4x mtu %u mps %u credits %u result 0x%2.2x",
	       dcid, mtu, mps, credits, result);

	chan = __l2cap_get_chan_by_ident(conn, cmd->ident);
	if (!chan)
		return -EBADSLT;

	err = 0;

	l2cap_chan_lock(chan);

	switch (result) {
	case L2CAP_CR_LE_SUCCESS:
		if (__l2cap_get_chan_by_dcid(conn, dcid)) {
			err = -EBADSLT;
			break;
		}

		chan->ident = 0;
		chan->dcid = dcid;
		chan->omtu = mtu;
		chan->remote_mps = mps;
		chan->tx_credits = credits;
		l2cap_chan_ready(chan);
		break;

	case L2CAP_CR_LE_AUTHENTICATION:
	case L2CAP_CR_LE_ENCRYPTION:
		/* If we already have MITM protection we can't do
		 * anything.
		 */
		if (hcon->sec_level > BT_SECURITY_MEDIUM) {
			l2cap_chan_del(chan, ECONNREFUSED);
			break;
		}

		sec_level = hcon->sec_level + 1;
		if (chan->sec_level < sec_level)
			chan->sec_level = sec_level;

		/* We'll need to send a new Connect Request */
		clear_bit(FLAG_LE_CONN_REQ_SENT, &chan->flags);

		smp_conn_security(hcon, chan->sec_level);
		break;

	default:
		l2cap_chan_del(chan, ECONNREFUSED);
		break;
	}

	l2cap_chan_unlock(chan);

	return err;
}

static inline int l2cap_bredr_sig_cmd(struct l2cap_conn *conn,
				      struct l2cap_cmd_hdr *cmd, u16 cmd_len,
				      u8 *data)
{
	int err = 0;

	switch (cmd->code) {
	case L2CAP_COMMAND_REJ:
		l2cap_command_rej(conn, cmd, cmd_len, data);
		break;

	case L2CAP_CONN_REQ:
		err = l2cap_connect_req(conn, cmd, cmd_len, data);
		break;

	case L2CAP_CONN_RSP:
		l2cap_connect_create_rsp(conn, cmd, cmd_len, data);
		break;

	case L2CAP_CONF_REQ:
		err = l2cap_config_req(conn, cmd, cmd_len, data);
		break;

	case L2CAP_CONF_RSP:
		l2cap_config_rsp(conn, cmd, cmd_len, data);
		break;

	case L2CAP_DISCONN_REQ:
		err = l2cap_disconnect_req(conn, cmd, cmd_len, data);
		break;

	case L2CAP_DISCONN_RSP:
		l2cap_disconnect_rsp(conn, cmd, cmd_len, data);
		break;

	case L2CAP_ECHO_REQ:
		l2cap_send_cmd(conn, cmd->ident, L2CAP_ECHO_RSP, cmd_len, data);
		break;

	case L2CAP_ECHO_RSP:
		break;

	case L2CAP_INFO_REQ:
		err = l2cap_information_req(conn, cmd, cmd_len, data);
		break;

	case L2CAP_INFO_RSP:
		l2cap_information_rsp(conn, cmd, cmd_len, data);
		break;

	default:
		BT_ERR("Unknown BR/EDR signaling command 0x%2.2x", cmd->code);
		err = -EINVAL;
		break;
	}

	return err;
}

static int l2cap_le_connect_req(struct l2cap_conn *conn,
				struct l2cap_cmd_hdr *cmd, u16 cmd_len,
				u8 *data)
{
	struct l2cap_le_conn_req *req = (struct l2cap_le_conn_req *) data;
	struct l2cap_le_conn_rsp rsp;
	struct l2cap_chan *chan, *pchan;
	u16 dcid, scid, credits, mtu, mps;
	__le16 psm;
	u8 result;

	if (cmd_len != sizeof(*req))
		return -EPROTO;

	scid = __le16_to_cpu(req->scid);
	mtu  = __le16_to_cpu(req->mtu);
	mps  = __le16_to_cpu(req->mps);
	psm  = req->psm;
	dcid = 0;
	credits = 0;

	if (mtu < 23 || mps < 23)
		return -EPROTO;

	BT_DBG("psm 0x%2.2x scid 0x%4.4x mtu %u mps %u", __le16_to_cpu(psm),
	       scid, mtu, mps);

	/* BLUETOOTH CORE SPECIFICATION Version 5.3 | Vol 3, Part A
	 * page 1059:
	 *
	 * Valid range: 0x0001-0x00ff
	 *
	 * Table 4.15: L2CAP_LE_CREDIT_BASED_CONNECTION_REQ SPSM ranges
	 */
	if (!psm || __le16_to_cpu(psm) > L2CAP_PSM_LE_DYN_END) {
		result = L2CAP_CR_LE_BAD_PSM;
		chan = NULL;
		goto response;
	}

	/* Check if we have socket listening on psm */
	pchan = l2cap_global_chan_by_psm(BT_LISTEN, psm, &conn->hcon->src,
					 &conn->hcon->dst, LE_LINK);
	if (!pchan) {
		result = L2CAP_CR_LE_BAD_PSM;
		chan = NULL;
		goto response;
	}

	l2cap_chan_lock(pchan);

	if (!smp_sufficient_security(conn->hcon, pchan->sec_level,
				     SMP_ALLOW_STK)) {
		result = L2CAP_CR_LE_AUTHENTICATION;
		chan = NULL;
		goto response_unlock;
	}

	/* Check for valid dynamic CID range */
	if (scid < L2CAP_CID_DYN_START || scid > L2CAP_CID_LE_DYN_END) {
		result = L2CAP_CR_LE_INVALID_SCID;
		chan = NULL;
		goto response_unlock;
	}

	/* Check if we already have channel with that dcid */
	if (__l2cap_get_chan_by_dcid(conn, scid)) {
		result = L2CAP_CR_LE_SCID_IN_USE;
		chan = NULL;
		goto response_unlock;
	}

	chan = pchan->ops->new_connection(pchan);
	if (!chan) {
		result = L2CAP_CR_LE_NO_MEM;
		goto response_unlock;
	}

	bacpy(&chan->src, &conn->hcon->src);
	bacpy(&chan->dst, &conn->hcon->dst);
	chan->src_type = bdaddr_src_type(conn->hcon);
	chan->dst_type = bdaddr_dst_type(conn->hcon);
	chan->psm  = psm;
	chan->dcid = scid;
	chan->omtu = mtu;
	chan->remote_mps = mps;

	__l2cap_chan_add(conn, chan);

	l2cap_le_flowctl_init(chan, __le16_to_cpu(req->credits));

	dcid = chan->scid;
	credits = chan->rx_credits;

	__set_chan_timer(chan, chan->ops->get_sndtimeo(chan));

	chan->ident = cmd->ident;

	if (test_bit(FLAG_DEFER_SETUP, &chan->flags)) {
		l2cap_state_change(chan, BT_CONNECT2);
		/* The following result value is actually not defined
		 * for LE CoC but we use it to let the function know
		 * that it should bail out after doing its cleanup
		 * instead of sending a response.
		 */
		result = L2CAP_CR_PEND;
		chan->ops->defer(chan);
	} else {
		l2cap_chan_ready(chan);
		result = L2CAP_CR_LE_SUCCESS;
	}

response_unlock:
	l2cap_chan_unlock(pchan);
	l2cap_chan_put(pchan);

	if (result == L2CAP_CR_PEND)
		return 0;

response:
	if (chan) {
		rsp.mtu = cpu_to_le16(chan->imtu);
		rsp.mps = cpu_to_le16(chan->mps);
	} else {
		rsp.mtu = 0;
		rsp.mps = 0;
	}

	rsp.dcid    = cpu_to_le16(dcid);
	rsp.credits = cpu_to_le16(credits);
	rsp.result  = cpu_to_le16(result);

	l2cap_send_cmd(conn, cmd->ident, L2CAP_LE_CONN_RSP, sizeof(rsp), &rsp);

	return 0;
}

static inline int l2cap_le_credits(struct l2cap_conn *conn,
				   struct l2cap_cmd_hdr *cmd, u16 cmd_len,
				   u8 *data)
{
	struct l2cap_le_credits *pkt;
	struct l2cap_chan *chan;
	u16 cid, credits, max_credits;

	if (cmd_len != sizeof(*pkt))
		return -EPROTO;

	pkt = (struct l2cap_le_credits *) data;
	cid	= __le16_to_cpu(pkt->cid);
	credits	= __le16_to_cpu(pkt->credits);

	BT_DBG("cid 0x%4.4x credits 0x%4.4x", cid, credits);

	chan = l2cap_get_chan_by_dcid(conn, cid);
	if (!chan)
		return -EBADSLT;

	max_credits = LE_FLOWCTL_MAX_CREDITS - chan->tx_credits;
	if (credits > max_credits) {
		BT_ERR("LE credits overflow");
		l2cap_send_disconn_req(chan, ECONNRESET);

		/* Return 0 so that we don't trigger an unnecessary
		 * command reject packet.
		 */
		goto unlock;
	}

	chan->tx_credits += credits;

	/* Resume sending */
	l2cap_le_flowctl_send(chan);

	if (chan->tx_credits)
		chan->ops->resume(chan);

unlock:
	l2cap_chan_unlock(chan);
	l2cap_chan_put(chan);

	return 0;
}

static inline int l2cap_ecred_conn_req(struct l2cap_conn *conn,
				       struct l2cap_cmd_hdr *cmd, u16 cmd_len,
				       u8 *data)
{
	struct l2cap_ecred_conn_req *req = (void *) data;
	DEFINE_RAW_FLEX(struct l2cap_ecred_conn_rsp, pdu, dcid, L2CAP_ECRED_MAX_CID);
	struct l2cap_chan *chan, *pchan;
	u16 mtu, mps;
	__le16 psm;
	u8 result, len = 0;
	int i, num_scid;
	bool defer = false;

	if (!enable_ecred)
		return -EINVAL;

	if (cmd_len < sizeof(*req) || (cmd_len - sizeof(*req)) % sizeof(u16)) {
		result = L2CAP_CR_LE_INVALID_PARAMS;
		goto response;
	}

	cmd_len -= sizeof(*req);
	num_scid = cmd_len / sizeof(u16);

	if (num_scid > L2CAP_ECRED_MAX_CID) {
		result = L2CAP_CR_LE_INVALID_PARAMS;
		goto response;
	}

	mtu  = __le16_to_cpu(req->mtu);
	mps  = __le16_to_cpu(req->mps);

	if (mtu < L2CAP_ECRED_MIN_MTU || mps < L2CAP_ECRED_MIN_MPS) {
		result = L2CAP_CR_LE_UNACCEPT_PARAMS;
		goto response;
	}

	psm  = req->psm;

	/* BLUETOOTH CORE SPECIFICATION Version 5.3 | Vol 3, Part A
	 * page 1059:
	 *
	 * Valid range: 0x0001-0x00ff
	 *
	 * Table 4.15: L2CAP_LE_CREDIT_BASED_CONNECTION_REQ SPSM ranges
	 */
	if (!psm || __le16_to_cpu(psm) > L2CAP_PSM_LE_DYN_END) {
		result = L2CAP_CR_LE_BAD_PSM;
		goto response;
	}

	BT_DBG("psm 0x%2.2x mtu %u mps %u", __le16_to_cpu(psm), mtu, mps);

	memset(pdu, 0, sizeof(*pdu));

	/* Check if we have socket listening on psm */
	pchan = l2cap_global_chan_by_psm(BT_LISTEN, psm, &conn->hcon->src,
					 &conn->hcon->dst, LE_LINK);
	if (!pchan) {
		result = L2CAP_CR_LE_BAD_PSM;
		goto response;
	}

	l2cap_chan_lock(pchan);

	if (!smp_sufficient_security(conn->hcon, pchan->sec_level,
				     SMP_ALLOW_STK)) {
		result = L2CAP_CR_LE_AUTHENTICATION;
		goto unlock;
	}

	result = L2CAP_CR_LE_SUCCESS;

	for (i = 0; i < num_scid; i++) {
		u16 scid = __le16_to_cpu(req->scid[i]);

		BT_DBG("scid[%d] 0x%4.4x", i, scid);

		pdu->dcid[i] = 0x0000;
		len += sizeof(*pdu->dcid);

		/* Check for valid dynamic CID range */
		if (scid < L2CAP_CID_DYN_START || scid > L2CAP_CID_LE_DYN_END) {
			result = L2CAP_CR_LE_INVALID_SCID;
			continue;
		}

		/* Check if we already have channel with that dcid */
		if (__l2cap_get_chan_by_dcid(conn, scid)) {
			result = L2CAP_CR_LE_SCID_IN_USE;
			continue;
		}

		chan = pchan->ops->new_connection(pchan);
		if (!chan) {
			result = L2CAP_CR_LE_NO_MEM;
			continue;
		}

		bacpy(&chan->src, &conn->hcon->src);
		bacpy(&chan->dst, &conn->hcon->dst);
		chan->src_type = bdaddr_src_type(conn->hcon);
		chan->dst_type = bdaddr_dst_type(conn->hcon);
		chan->psm  = psm;
		chan->dcid = scid;
		chan->omtu = mtu;
		chan->remote_mps = mps;

		__l2cap_chan_add(conn, chan);

		l2cap_ecred_init(chan, __le16_to_cpu(req->credits));

		/* Init response */
		if (!pdu->credits) {
			pdu->mtu = cpu_to_le16(chan->imtu);
			pdu->mps = cpu_to_le16(chan->mps);
			pdu->credits = cpu_to_le16(chan->rx_credits);
		}

		pdu->dcid[i] = cpu_to_le16(chan->scid);

		__set_chan_timer(chan, chan->ops->get_sndtimeo(chan));

		chan->ident = cmd->ident;
		chan->mode = L2CAP_MODE_EXT_FLOWCTL;

		if (test_bit(FLAG_DEFER_SETUP, &chan->flags)) {
			l2cap_state_change(chan, BT_CONNECT2);
			defer = true;
			chan->ops->defer(chan);
		} else {
			l2cap_chan_ready(chan);
		}
	}

unlock:
	l2cap_chan_unlock(pchan);
	l2cap_chan_put(pchan);

response:
	pdu->result = cpu_to_le16(result);

	if (defer)
		return 0;

	l2cap_send_cmd(conn, cmd->ident, L2CAP_ECRED_CONN_RSP,
		       sizeof(*pdu) + len, pdu);

	return 0;
}

static inline int l2cap_ecred_conn_rsp(struct l2cap_conn *conn,
				       struct l2cap_cmd_hdr *cmd, u16 cmd_len,
				       u8 *data)
{
	struct l2cap_ecred_conn_rsp *rsp = (void *) data;
	struct hci_conn *hcon = conn->hcon;
	u16 mtu, mps, credits, result;
	struct l2cap_chan *chan, *tmp;
	int err = 0, sec_level;
	int i = 0;

	if (cmd_len < sizeof(*rsp))
		return -EPROTO;

	mtu     = __le16_to_cpu(rsp->mtu);
	mps     = __le16_to_cpu(rsp->mps);
	credits = __le16_to_cpu(rsp->credits);
	result  = __le16_to_cpu(rsp->result);

	BT_DBG("mtu %u mps %u credits %u result 0x%4.4x", mtu, mps, credits,
	       result);

	cmd_len -= sizeof(*rsp);

	list_for_each_entry_safe(chan, tmp, &conn->chan_l, list) {
		u16 dcid;

		if (chan->ident != cmd->ident ||
		    chan->mode != L2CAP_MODE_EXT_FLOWCTL ||
		    chan->state == BT_CONNECTED)
			continue;

		l2cap_chan_lock(chan);

		/* Check that there is a dcid for each pending channel */
		if (cmd_len < sizeof(dcid)) {
			l2cap_chan_del(chan, ECONNREFUSED);
			l2cap_chan_unlock(chan);
			continue;
		}

		dcid = __le16_to_cpu(rsp->dcid[i++]);
		cmd_len -= sizeof(u16);

		BT_DBG("dcid[%d] 0x%4.4x", i, dcid);

		/* Check if dcid is already in use */
		if (dcid && __l2cap_get_chan_by_dcid(conn, dcid)) {
			/* If a device receives a
			 * L2CAP_CREDIT_BASED_CONNECTION_RSP packet with an
			 * already-assigned Destination CID, then both the
			 * original channel and the new channel shall be
			 * immediately discarded and not used.
			 */
			l2cap_chan_del(chan, ECONNREFUSED);
			l2cap_chan_unlock(chan);
			chan = __l2cap_get_chan_by_dcid(conn, dcid);
			l2cap_chan_lock(chan);
			l2cap_chan_del(chan, ECONNRESET);
			l2cap_chan_unlock(chan);
			continue;
		}

		switch (result) {
		case L2CAP_CR_LE_AUTHENTICATION:
		case L2CAP_CR_LE_ENCRYPTION:
			/* If we already have MITM protection we can't do
			 * anything.
			 */
			if (hcon->sec_level > BT_SECURITY_MEDIUM) {
				l2cap_chan_del(chan, ECONNREFUSED);
				break;
			}

			sec_level = hcon->sec_level + 1;
			if (chan->sec_level < sec_level)
				chan->sec_level = sec_level;

			/* We'll need to send a new Connect Request */
			clear_bit(FLAG_ECRED_CONN_REQ_SENT, &chan->flags);

			smp_conn_security(hcon, chan->sec_level);
			break;

		case L2CAP_CR_LE_BAD_PSM:
			l2cap_chan_del(chan, ECONNREFUSED);
			break;

		default:
			/* If dcid was not set it means channels was refused */
			if (!dcid) {
				l2cap_chan_del(chan, ECONNREFUSED);
				break;
			}

			chan->ident = 0;
			chan->dcid = dcid;
			chan->omtu = mtu;
			chan->remote_mps = mps;
			chan->tx_credits = credits;
			l2cap_chan_ready(chan);
			break;
		}

		l2cap_chan_unlock(chan);
	}

	return err;
}

static inline int l2cap_ecred_reconf_req(struct l2cap_conn *conn,
					 struct l2cap_cmd_hdr *cmd, u16 cmd_len,
					 u8 *data)
{
	struct l2cap_ecred_reconf_req *req = (void *) data;
	struct l2cap_ecred_reconf_rsp rsp;
	u16 mtu, mps, result;
	struct l2cap_chan *chan;
	int i, num_scid;

	if (!enable_ecred)
		return -EINVAL;

	if (cmd_len < sizeof(*req) || cmd_len - sizeof(*req) % sizeof(u16)) {
		result = L2CAP_CR_LE_INVALID_PARAMS;
		goto respond;
	}

	mtu = __le16_to_cpu(req->mtu);
	mps = __le16_to_cpu(req->mps);

	BT_DBG("mtu %u mps %u", mtu, mps);

	if (mtu < L2CAP_ECRED_MIN_MTU) {
		result = L2CAP_RECONF_INVALID_MTU;
		goto respond;
	}

	if (mps < L2CAP_ECRED_MIN_MPS) {
		result = L2CAP_RECONF_INVALID_MPS;
		goto respond;
	}

	cmd_len -= sizeof(*req);
	num_scid = cmd_len / sizeof(u16);
	result = L2CAP_RECONF_SUCCESS;

	for (i = 0; i < num_scid; i++) {
		u16 scid;

		scid = __le16_to_cpu(req->scid[i]);
		if (!scid)
			return -EPROTO;

		chan = __l2cap_get_chan_by_dcid(conn, scid);
		if (!chan)
			continue;

		/* If the MTU value is decreased for any of the included
		 * channels, then the receiver shall disconnect all
		 * included channels.
		 */
		if (chan->omtu > mtu) {
			BT_ERR("chan %p decreased MTU %u -> %u", chan,
			       chan->omtu, mtu);
			result = L2CAP_RECONF_INVALID_MTU;
		}

		chan->omtu = mtu;
		chan->remote_mps = mps;
	}

respond:
	rsp.result = cpu_to_le16(result);

	l2cap_send_cmd(conn, cmd->ident, L2CAP_ECRED_RECONF_RSP, sizeof(rsp),
		       &rsp);

	return 0;
}

static inline int l2cap_ecred_reconf_rsp(struct l2cap_conn *conn,
					 struct l2cap_cmd_hdr *cmd, u16 cmd_len,
					 u8 *data)
{
	struct l2cap_chan *chan, *tmp;
	struct l2cap_ecred_conn_rsp *rsp = (void *) data;
	u16 result;

	if (cmd_len < sizeof(*rsp))
		return -EPROTO;

	result = __le16_to_cpu(rsp->result);

	BT_DBG("result 0x%4.4x", rsp->result);

	if (!result)
		return 0;

	list_for_each_entry_safe(chan, tmp, &conn->chan_l, list) {
		if (chan->ident != cmd->ident)
			continue;

		l2cap_chan_del(chan, ECONNRESET);
	}

	return 0;
}

static inline int l2cap_le_command_rej(struct l2cap_conn *conn,
				       struct l2cap_cmd_hdr *cmd, u16 cmd_len,
				       u8 *data)
{
	struct l2cap_cmd_rej_unk *rej = (struct l2cap_cmd_rej_unk *) data;
	struct l2cap_chan *chan;

	if (cmd_len < sizeof(*rej))
		return -EPROTO;

	chan = __l2cap_get_chan_by_ident(conn, cmd->ident);
	if (!chan)
		goto done;

	chan = l2cap_chan_hold_unless_zero(chan);
	if (!chan)
		goto done;

	l2cap_chan_lock(chan);
	l2cap_chan_del(chan, ECONNREFUSED);
	l2cap_chan_unlock(chan);
	l2cap_chan_put(chan);

done:
	return 0;
}

static inline int l2cap_le_sig_cmd(struct l2cap_conn *conn,
				   struct l2cap_cmd_hdr *cmd, u16 cmd_len,
				   u8 *data)
{
	int err = 0;

	switch (cmd->code) {
	case L2CAP_COMMAND_REJ:
		l2cap_le_command_rej(conn, cmd, cmd_len, data);
		break;

	case L2CAP_CONN_PARAM_UPDATE_REQ:
		err = l2cap_conn_param_update_req(conn, cmd, cmd_len, data);
		break;

	case L2CAP_CONN_PARAM_UPDATE_RSP:
		break;

	case L2CAP_LE_CONN_RSP:
		l2cap_le_connect_rsp(conn, cmd, cmd_len, data);
		break;

	case L2CAP_LE_CONN_REQ:
		err = l2cap_le_connect_req(conn, cmd, cmd_len, data);
		break;

	case L2CAP_LE_CREDITS:
		err = l2cap_le_credits(conn, cmd, cmd_len, data);
		break;

	case L2CAP_ECRED_CONN_REQ:
		err = l2cap_ecred_conn_req(conn, cmd, cmd_len, data);
		break;

	case L2CAP_ECRED_CONN_RSP:
		err = l2cap_ecred_conn_rsp(conn, cmd, cmd_len, data);
		break;

	case L2CAP_ECRED_RECONF_REQ:
		err = l2cap_ecred_reconf_req(conn, cmd, cmd_len, data);
		break;

	case L2CAP_ECRED_RECONF_RSP:
		err = l2cap_ecred_reconf_rsp(conn, cmd, cmd_len, data);
		break;

	case L2CAP_DISCONN_REQ:
		err = l2cap_disconnect_req(conn, cmd, cmd_len, data);
		break;

	case L2CAP_DISCONN_RSP:
		l2cap_disconnect_rsp(conn, cmd, cmd_len, data);
		break;

	default:
		BT_ERR("Unknown LE signaling command 0x%2.2x", cmd->code);
		err = -EINVAL;
		break;
	}

	return err;
}

static inline void l2cap_le_sig_channel(struct l2cap_conn *conn,
					struct sk_buff *skb)
{
	struct hci_conn *hcon = conn->hcon;
	struct l2cap_cmd_hdr *cmd;
	u16 len;
	int err;

	if (hcon->type != LE_LINK)
		goto drop;

	if (skb->len < L2CAP_CMD_HDR_SIZE)
		goto drop;

	cmd = (void *) skb->data;
	skb_pull(skb, L2CAP_CMD_HDR_SIZE);

	len = le16_to_cpu(cmd->len);

	BT_DBG("code 0x%2.2x len %d id 0x%2.2x", cmd->code, len, cmd->ident);

	if (len != skb->len || !cmd->ident) {
		BT_DBG("corrupted command");
		goto drop;
	}

	err = l2cap_le_sig_cmd(conn, cmd, len, skb->data);
	if (err) {
		struct l2cap_cmd_rej_unk rej;

		BT_ERR("Wrong link type (%d)", err);

		rej.reason = cpu_to_le16(L2CAP_REJ_NOT_UNDERSTOOD);
		l2cap_send_cmd(conn, cmd->ident, L2CAP_COMMAND_REJ,
			       sizeof(rej), &rej);
	}

drop:
	kfree_skb(skb);
}

static inline void l2cap_sig_send_rej(struct l2cap_conn *conn, u16 ident)
{
	struct l2cap_cmd_rej_unk rej;

	rej.reason = cpu_to_le16(L2CAP_REJ_NOT_UNDERSTOOD);
	l2cap_send_cmd(conn, ident, L2CAP_COMMAND_REJ, sizeof(rej), &rej);
}

static inline void l2cap_sig_channel(struct l2cap_conn *conn,
				     struct sk_buff *skb)
{
	struct hci_conn *hcon = conn->hcon;
	struct l2cap_cmd_hdr *cmd;
	int err;

	l2cap_raw_recv(conn, skb);

	if (hcon->type != ACL_LINK)
		goto drop;

	while (skb->len >= L2CAP_CMD_HDR_SIZE) {
		u16 len;

		cmd = (void *) skb->data;
		skb_pull(skb, L2CAP_CMD_HDR_SIZE);

		len = le16_to_cpu(cmd->len);

		BT_DBG("code 0x%2.2x len %d id 0x%2.2x", cmd->code, len,
		       cmd->ident);

		if (len > skb->len || !cmd->ident) {
			BT_DBG("corrupted command");
			l2cap_sig_send_rej(conn, cmd->ident);
			skb_pull(skb, len > skb->len ? skb->len : len);
			continue;
		}

		err = l2cap_bredr_sig_cmd(conn, cmd, len, skb->data);
		if (err) {
			BT_ERR("Wrong link type (%d)", err);
			l2cap_sig_send_rej(conn, cmd->ident);
		}

		skb_pull(skb, len);
	}

	if (skb->len > 0) {
		BT_DBG("corrupted command");
		l2cap_sig_send_rej(conn, 0);
	}

drop:
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

static void l2cap_send_i_or_rr_or_rnr(struct l2cap_chan *chan)
{
	struct l2cap_ctrl control;

	BT_DBG("chan %p", chan);

	memset(&control, 0, sizeof(control));
	control.sframe = 1;
	control.final = 1;
	control.reqseq = chan->buffer_seq;
	set_bit(CONN_SEND_FBIT, &chan->conn_state);

	if (test_bit(CONN_LOCAL_BUSY, &chan->conn_state)) {
		control.super = L2CAP_SUPER_RNR;
		l2cap_send_sframe(chan, &control);
	}

	if (test_and_clear_bit(CONN_REMOTE_BUSY, &chan->conn_state) &&
	    chan->unacked_frames > 0)
		__set_retrans_timer(chan);

	/* Send pending iframes */
	l2cap_ertm_send(chan);

	if (!test_bit(CONN_LOCAL_BUSY, &chan->conn_state) &&
	    test_bit(CONN_SEND_FBIT, &chan->conn_state)) {
		/* F-bit wasn't sent in an s-frame or i-frame yet, so
		 * send it now.
		 */
		control.super = L2CAP_SUPER_RR;
		l2cap_send_sframe(chan, &control);
	}
}

static void append_skb_frag(struct sk_buff *skb, struct sk_buff *new_frag,
			    struct sk_buff **last_frag)
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

static int l2cap_reassemble_sdu(struct l2cap_chan *chan, struct sk_buff *skb,
				struct l2cap_ctrl *control)
{
	int err = -EINVAL;

	switch (control->sar) {
	case L2CAP_SAR_UNSEGMENTED:
		if (chan->sdu)
			break;

		err = chan->ops->recv(chan, skb);
		break;

	case L2CAP_SAR_START:
		if (chan->sdu)
			break;

		if (!pskb_may_pull(skb, L2CAP_SDULEN_SIZE))
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

		err = chan->ops->recv(chan, chan->sdu);

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

static int l2cap_resegment(struct l2cap_chan *chan)
{
	/* Placeholder */
	return 0;
}

void l2cap_chan_busy(struct l2cap_chan *chan, int busy)
{
	u8 event;

	if (chan->mode != L2CAP_MODE_ERTM)
		return;

	event = busy ? L2CAP_EV_LOCAL_BUSY_DETECTED : L2CAP_EV_LOCAL_BUSY_CLEAR;
	l2cap_tx(chan, NULL, NULL, event);
}

static int l2cap_rx_queued_iframes(struct l2cap_chan *chan)
{
	int err = 0;
	/* Pass sequential frames to l2cap_reassemble_sdu()
	 * until a gap is encountered.
	 */

	BT_DBG("chan %p", chan);

	while (!test_bit(CONN_LOCAL_BUSY, &chan->conn_state)) {
		struct sk_buff *skb;
		BT_DBG("Searching for skb with txseq %d (queue len %d)",
		       chan->buffer_seq, skb_queue_len(&chan->srej_q));

		skb = l2cap_ertm_seq_in_queue(&chan->srej_q, chan->buffer_seq);

		if (!skb)
			break;

		skb_unlink(skb, &chan->srej_q);
		chan->buffer_seq = __next_seq(chan, chan->buffer_seq);
		err = l2cap_reassemble_sdu(chan, skb, &bt_cb(skb)->l2cap);
		if (err)
			break;
	}

	if (skb_queue_empty(&chan->srej_q)) {
		chan->rx_state = L2CAP_RX_STATE_RECV;
		l2cap_send_ack(chan);
	}

	return err;
}

static void l2cap_handle_srej(struct l2cap_chan *chan,
			      struct l2cap_ctrl *control)
{
	struct sk_buff *skb;

	BT_DBG("chan %p, control %p", chan, control);

	if (control->reqseq == chan->next_tx_seq) {
		BT_DBG("Invalid reqseq %d, disconnecting", control->reqseq);
		l2cap_send_disconn_req(chan, ECONNRESET);
		return;
	}

	skb = l2cap_ertm_seq_in_queue(&chan->tx_q, control->reqseq);

	if (skb == NULL) {
		BT_DBG("Seq %d not available for retransmission",
		       control->reqseq);
		return;
	}

	if (chan->max_tx != 0 && bt_cb(skb)->l2cap.retries >= chan->max_tx) {
		BT_DBG("Retry limit exceeded (%d)", chan->max_tx);
		l2cap_send_disconn_req(chan, ECONNRESET);
		return;
	}

	clear_bit(CONN_REMOTE_BUSY, &chan->conn_state);

	if (control->poll) {
		l2cap_pass_to_tx(chan, control);

		set_bit(CONN_SEND_FBIT, &chan->conn_state);
		l2cap_retransmit(chan, control);
		l2cap_ertm_send(chan);

		if (chan->tx_state == L2CAP_TX_STATE_WAIT_F) {
			set_bit(CONN_SREJ_ACT, &chan->conn_state);
			chan->srej_save_reqseq = control->reqseq;
		}
	} else {
		l2cap_pass_to_tx_fbit(chan, control);

		if (control->final) {
			if (chan->srej_save_reqseq != control->reqseq ||
			    !test_and_clear_bit(CONN_SREJ_ACT,
						&chan->conn_state))
				l2cap_retransmit(chan, control);
		} else {
			l2cap_retransmit(chan, control);
			if (chan->tx_state == L2CAP_TX_STATE_WAIT_F) {
				set_bit(CONN_SREJ_ACT, &chan->conn_state);
				chan->srej_save_reqseq = control->reqseq;
			}
		}
	}
}

static void l2cap_handle_rej(struct l2cap_chan *chan,
			     struct l2cap_ctrl *control)
{
	struct sk_buff *skb;

	BT_DBG("chan %p, control %p", chan, control);

	if (control->reqseq == chan->next_tx_seq) {
		BT_DBG("Invalid reqseq %d, disconnecting", control->reqseq);
		l2cap_send_disconn_req(chan, ECONNRESET);
		return;
	}

	skb = l2cap_ertm_seq_in_queue(&chan->tx_q, control->reqseq);

	if (chan->max_tx && skb &&
	    bt_cb(skb)->l2cap.retries >= chan->max_tx) {
		BT_DBG("Retry limit exceeded (%d)", chan->max_tx);
		l2cap_send_disconn_req(chan, ECONNRESET);
		return;
	}

	clear_bit(CONN_REMOTE_BUSY, &chan->conn_state);

	l2cap_pass_to_tx(chan, control);

	if (control->final) {
		if (!test_and_clear_bit(CONN_REJ_ACT, &chan->conn_state))
			l2cap_retransmit_all(chan, control);
	} else {
		l2cap_retransmit_all(chan, control);
		l2cap_ertm_send(chan);
		if (chan->tx_state == L2CAP_TX_STATE_WAIT_F)
			set_bit(CONN_REJ_ACT, &chan->conn_state);
	}
}

static u8 l2cap_classify_txseq(struct l2cap_chan *chan, u16 txseq)
{
	BT_DBG("chan %p, txseq %d", chan, txseq);

	BT_DBG("last_acked_seq %d, expected_tx_seq %d", chan->last_acked_seq,
	       chan->expected_tx_seq);

	if (chan->rx_state == L2CAP_RX_STATE_SREJ_SENT) {
		if (__seq_offset(chan, txseq, chan->last_acked_seq) >=
		    chan->tx_win) {
			/* See notes below regarding "double poll" and
			 * invalid packets.
			 */
			if (chan->tx_win <= ((chan->tx_win_max + 1) >> 1)) {
				BT_DBG("Invalid/Ignore - after SREJ");
				return L2CAP_TXSEQ_INVALID_IGNORE;
			} else {
				BT_DBG("Invalid - in window after SREJ sent");
				return L2CAP_TXSEQ_INVALID;
			}
		}

		if (chan->srej_list.head == txseq) {
			BT_DBG("Expected SREJ");
			return L2CAP_TXSEQ_EXPECTED_SREJ;
		}

		if (l2cap_ertm_seq_in_queue(&chan->srej_q, txseq)) {
			BT_DBG("Duplicate SREJ - txseq already stored");
			return L2CAP_TXSEQ_DUPLICATE_SREJ;
		}

		if (l2cap_seq_list_contains(&chan->srej_list, txseq)) {
			BT_DBG("Unexpected SREJ - not requested");
			return L2CAP_TXSEQ_UNEXPECTED_SREJ;
		}
	}

	if (chan->expected_tx_seq == txseq) {
		if (__seq_offset(chan, txseq, chan->last_acked_seq) >=
		    chan->tx_win) {
			BT_DBG("Invalid - txseq outside tx window");
			return L2CAP_TXSEQ_INVALID;
		} else {
			BT_DBG("Expected");
			return L2CAP_TXSEQ_EXPECTED;
		}
	}

	if (__seq_offset(chan, txseq, chan->last_acked_seq) <
	    __seq_offset(chan, chan->expected_tx_seq, chan->last_acked_seq)) {
		BT_DBG("Duplicate - expected_tx_seq later than txseq");
		return L2CAP_TXSEQ_DUPLICATE;
	}

	if (__seq_offset(chan, txseq, chan->last_acked_seq) >= chan->tx_win) {
		/* A source of invalid packets is a "double poll" condition,
		 * where delays cause us to send multiple poll packets.  If
		 * the remote stack receives and processes both polls,
		 * sequence numbers can wrap around in such a way that a
		 * resent frame has a sequence number that looks like new data
		 * with a sequence gap.  This would trigger an erroneous SREJ
		 * request.
		 *
		 * Fortunately, this is impossible with a tx window that's
		 * less than half of the maximum sequence number, which allows
		 * invalid frames to be safely ignored.
		 *
		 * With tx window sizes greater than half of the tx window
		 * maximum, the frame is invalid and cannot be ignored.  This
		 * causes a disconnect.
		 */

		if (chan->tx_win <= ((chan->tx_win_max + 1) >> 1)) {
			BT_DBG("Invalid/Ignore - txseq outside tx window");
			return L2CAP_TXSEQ_INVALID_IGNORE;
		} else {
			BT_DBG("Invalid - txseq outside tx window");
			return L2CAP_TXSEQ_INVALID;
		}
	} else {
		BT_DBG("Unexpected - txseq indicates missing frames");
		return L2CAP_TXSEQ_UNEXPECTED;
	}
}

static int l2cap_rx_state_recv(struct l2cap_chan *chan,
			       struct l2cap_ctrl *control,
			       struct sk_buff *skb, u8 event)
{
	struct l2cap_ctrl local_control;
	int err = 0;
	bool skb_in_use = false;

	BT_DBG("chan %p, control %p, skb %p, event %d", chan, control, skb,
	       event);

	switch (event) {
	case L2CAP_EV_RECV_IFRAME:
		switch (l2cap_classify_txseq(chan, control->txseq)) {
		case L2CAP_TXSEQ_EXPECTED:
			l2cap_pass_to_tx(chan, control);

			if (test_bit(CONN_LOCAL_BUSY, &chan->conn_state)) {
				BT_DBG("Busy, discarding expected seq %d",
				       control->txseq);
				break;
			}

			chan->expected_tx_seq = __next_seq(chan,
							   control->txseq);

			chan->buffer_seq = chan->expected_tx_seq;
			skb_in_use = true;

			/* l2cap_reassemble_sdu may free skb, hence invalidate
			 * control, so make a copy in advance to use it after
			 * l2cap_reassemble_sdu returns and to avoid the race
			 * condition, for example:
			 *
			 * The current thread calls:
			 *   l2cap_reassemble_sdu
			 *     chan->ops->recv == l2cap_sock_recv_cb
			 *       __sock_queue_rcv_skb
			 * Another thread calls:
			 *   bt_sock_recvmsg
			 *     skb_recv_datagram
			 *     skb_free_datagram
			 * Then the current thread tries to access control, but
			 * it was freed by skb_free_datagram.
			 */
			local_control = *control;
			err = l2cap_reassemble_sdu(chan, skb, control);
			if (err)
				break;

			if (local_control.final) {
				if (!test_and_clear_bit(CONN_REJ_ACT,
							&chan->conn_state)) {
					local_control.final = 0;
					l2cap_retransmit_all(chan, &local_control);
					l2cap_ertm_send(chan);
				}
			}

			if (!test_bit(CONN_LOCAL_BUSY, &chan->conn_state))
				l2cap_send_ack(chan);
			break;
		case L2CAP_TXSEQ_UNEXPECTED:
			l2cap_pass_to_tx(chan, control);

			/* Can't issue SREJ frames in the local busy state.
			 * Drop this frame, it will be seen as missing
			 * when local busy is exited.
			 */
			if (test_bit(CONN_LOCAL_BUSY, &chan->conn_state)) {
				BT_DBG("Busy, discarding unexpected seq %d",
				       control->txseq);
				break;
			}

			/* There was a gap in the sequence, so an SREJ
			 * must be sent for each missing frame.  The
			 * current frame is stored for later use.
			 */
			skb_queue_tail(&chan->srej_q, skb);
			skb_in_use = true;
			BT_DBG("Queued %p (queue len %d)", skb,
			       skb_queue_len(&chan->srej_q));

			clear_bit(CONN_SREJ_ACT, &chan->conn_state);
			l2cap_seq_list_clear(&chan->srej_list);
			l2cap_send_srej(chan, control->txseq);

			chan->rx_state = L2CAP_RX_STATE_SREJ_SENT;
			break;
		case L2CAP_TXSEQ_DUPLICATE:
			l2cap_pass_to_tx(chan, control);
			break;
		case L2CAP_TXSEQ_INVALID_IGNORE:
			break;
		case L2CAP_TXSEQ_INVALID:
		default:
			l2cap_send_disconn_req(chan, ECONNRESET);
			break;
		}
		break;
	case L2CAP_EV_RECV_RR:
		l2cap_pass_to_tx(chan, control);
		if (control->final) {
			clear_bit(CONN_REMOTE_BUSY, &chan->conn_state);

			if (!test_and_clear_bit(CONN_REJ_ACT,
						&chan->conn_state)) {
				control->final = 0;
				l2cap_retransmit_all(chan, control);
			}

			l2cap_ertm_send(chan);
		} else if (control->poll) {
			l2cap_send_i_or_rr_or_rnr(chan);
		} else {
			if (test_and_clear_bit(CONN_REMOTE_BUSY,
					       &chan->conn_state) &&
			    chan->unacked_frames)
				__set_retrans_timer(chan);

			l2cap_ertm_send(chan);
		}
		break;
	case L2CAP_EV_RECV_RNR:
		set_bit(CONN_REMOTE_BUSY, &chan->conn_state);
		l2cap_pass_to_tx(chan, control);
		if (control && control->poll) {
			set_bit(CONN_SEND_FBIT, &chan->conn_state);
			l2cap_send_rr_or_rnr(chan, 0);
		}
		__clear_retrans_timer(chan);
		l2cap_seq_list_clear(&chan->retrans_list);
		break;
	case L2CAP_EV_RECV_REJ:
		l2cap_handle_rej(chan, control);
		break;
	case L2CAP_EV_RECV_SREJ:
		l2cap_handle_srej(chan, control);
		break;
	default:
		break;
	}

	if (skb && !skb_in_use) {
		BT_DBG("Freeing %p", skb);
		kfree_skb(skb);
	}

	return err;
}

static int l2cap_rx_state_srej_sent(struct l2cap_chan *chan,
				    struct l2cap_ctrl *control,
				    struct sk_buff *skb, u8 event)
{
	int err = 0;
	u16 txseq = control->txseq;
	bool skb_in_use = false;

	BT_DBG("chan %p, control %p, skb %p, event %d", chan, control, skb,
	       event);

	switch (event) {
	case L2CAP_EV_RECV_IFRAME:
		switch (l2cap_classify_txseq(chan, txseq)) {
		case L2CAP_TXSEQ_EXPECTED:
			/* Keep frame for reassembly later */
			l2cap_pass_to_tx(chan, control);
			skb_queue_tail(&chan->srej_q, skb);
			skb_in_use = true;
			BT_DBG("Queued %p (queue len %d)", skb,
			       skb_queue_len(&chan->srej_q));

			chan->expected_tx_seq = __next_seq(chan, txseq);
			break;
		case L2CAP_TXSEQ_EXPECTED_SREJ:
			l2cap_seq_list_pop(&chan->srej_list);

			l2cap_pass_to_tx(chan, control);
			skb_queue_tail(&chan->srej_q, skb);
			skb_in_use = true;
			BT_DBG("Queued %p (queue len %d)", skb,
			       skb_queue_len(&chan->srej_q));

			err = l2cap_rx_queued_iframes(chan);
			if (err)
				break;

			break;
		case L2CAP_TXSEQ_UNEXPECTED:
			/* Got a frame that can't be reassembled yet.
			 * Save it for later, and send SREJs to cover
			 * the missing frames.
			 */
			skb_queue_tail(&chan->srej_q, skb);
			skb_in_use = true;
			BT_DBG("Queued %p (queue len %d)", skb,
			       skb_queue_len(&chan->srej_q));

			l2cap_pass_to_tx(chan, control);
			l2cap_send_srej(chan, control->txseq);
			break;
		case L2CAP_TXSEQ_UNEXPECTED_SREJ:
			/* This frame was requested with an SREJ, but
			 * some expected retransmitted frames are
			 * missing.  Request retransmission of missing
			 * SREJ'd frames.
			 */
			skb_queue_tail(&chan->srej_q, skb);
			skb_in_use = true;
			BT_DBG("Queued %p (queue len %d)", skb,
			       skb_queue_len(&chan->srej_q));

			l2cap_pass_to_tx(chan, control);
			l2cap_send_srej_list(chan, control->txseq);
			break;
		case L2CAP_TXSEQ_DUPLICATE_SREJ:
			/* We've already queued this frame.  Drop this copy. */
			l2cap_pass_to_tx(chan, control);
			break;
		case L2CAP_TXSEQ_DUPLICATE:
			/* Expecting a later sequence number, so this frame
			 * was already received.  Ignore it completely.
			 */
			break;
		case L2CAP_TXSEQ_INVALID_IGNORE:
			break;
		case L2CAP_TXSEQ_INVALID:
		default:
			l2cap_send_disconn_req(chan, ECONNRESET);
			break;
		}
		break;
	case L2CAP_EV_RECV_RR:
		l2cap_pass_to_tx(chan, control);
		if (control->final) {
			clear_bit(CONN_REMOTE_BUSY, &chan->conn_state);

			if (!test_and_clear_bit(CONN_REJ_ACT,
						&chan->conn_state)) {
				control->final = 0;
				l2cap_retransmit_all(chan, control);
			}

			l2cap_ertm_send(chan);
		} else if (control->poll) {
			if (test_and_clear_bit(CONN_REMOTE_BUSY,
					       &chan->conn_state) &&
			    chan->unacked_frames) {
				__set_retrans_timer(chan);
			}

			set_bit(CONN_SEND_FBIT, &chan->conn_state);
			l2cap_send_srej_tail(chan);
		} else {
			if (test_and_clear_bit(CONN_REMOTE_BUSY,
					       &chan->conn_state) &&
			    chan->unacked_frames)
				__set_retrans_timer(chan);

			l2cap_send_ack(chan);
		}
		break;
	case L2CAP_EV_RECV_RNR:
		set_bit(CONN_REMOTE_BUSY, &chan->conn_state);
		l2cap_pass_to_tx(chan, control);
		if (control->poll) {
			l2cap_send_srej_tail(chan);
		} else {
			struct l2cap_ctrl rr_control;
			memset(&rr_control, 0, sizeof(rr_control));
			rr_control.sframe = 1;
			rr_control.super = L2CAP_SUPER_RR;
			rr_control.reqseq = chan->buffer_seq;
			l2cap_send_sframe(chan, &rr_control);
		}

		break;
	case L2CAP_EV_RECV_REJ:
		l2cap_handle_rej(chan, control);
		break;
	case L2CAP_EV_RECV_SREJ:
		l2cap_handle_srej(chan, control);
		break;
	}

	if (skb && !skb_in_use) {
		BT_DBG("Freeing %p", skb);
		kfree_skb(skb);
	}

	return err;
}

static int l2cap_finish_move(struct l2cap_chan *chan)
{
	BT_DBG("chan %p", chan);

	chan->rx_state = L2CAP_RX_STATE_RECV;
	chan->conn->mtu = chan->conn->hcon->mtu;

	return l2cap_resegment(chan);
}

static int l2cap_rx_state_wait_p(struct l2cap_chan *chan,
				 struct l2cap_ctrl *control,
				 struct sk_buff *skb, u8 event)
{
	int err;

	BT_DBG("chan %p, control %p, skb %p, event %d", chan, control, skb,
	       event);

	if (!control->poll)
		return -EPROTO;

	l2cap_process_reqseq(chan, control->reqseq);

	if (!skb_queue_empty(&chan->tx_q))
		chan->tx_send_head = skb_peek(&chan->tx_q);
	else
		chan->tx_send_head = NULL;

	/* Rewind next_tx_seq to the point expected
	 * by the receiver.
	 */
	chan->next_tx_seq = control->reqseq;
	chan->unacked_frames = 0;

	err = l2cap_finish_move(chan);
	if (err)
		return err;

	set_bit(CONN_SEND_FBIT, &chan->conn_state);
	l2cap_send_i_or_rr_or_rnr(chan);

	if (event == L2CAP_EV_RECV_IFRAME)
		return -EPROTO;

	return l2cap_rx_state_recv(chan, control, NULL, event);
}

static int l2cap_rx_state_wait_f(struct l2cap_chan *chan,
				 struct l2cap_ctrl *control,
				 struct sk_buff *skb, u8 event)
{
	int err;

	if (!control->final)
		return -EPROTO;

	clear_bit(CONN_REMOTE_BUSY, &chan->conn_state);

	chan->rx_state = L2CAP_RX_STATE_RECV;
	l2cap_process_reqseq(chan, control->reqseq);

	if (!skb_queue_empty(&chan->tx_q))
		chan->tx_send_head = skb_peek(&chan->tx_q);
	else
		chan->tx_send_head = NULL;

	/* Rewind next_tx_seq to the point expected
	 * by the receiver.
	 */
	chan->next_tx_seq = control->reqseq;
	chan->unacked_frames = 0;
	chan->conn->mtu = chan->conn->hcon->mtu;

	err = l2cap_resegment(chan);

	if (!err)
		err = l2cap_rx_state_recv(chan, control, skb, event);

	return err;
}

static bool __valid_reqseq(struct l2cap_chan *chan, u16 reqseq)
{
	/* Make sure reqseq is for a packet that has been sent but not acked */
	u16 unacked;

	unacked = __seq_offset(chan, chan->next_tx_seq, chan->expected_ack_seq);
	return __seq_offset(chan, chan->next_tx_seq, reqseq) <= unacked;
}

static int l2cap_rx(struct l2cap_chan *chan, struct l2cap_ctrl *control,
		    struct sk_buff *skb, u8 event)
{
	int err = 0;

	BT_DBG("chan %p, control %p, skb %p, event %d, state %d", chan,
	       control, skb, event, chan->rx_state);

	if (__valid_reqseq(chan, control->reqseq)) {
		switch (chan->rx_state) {
		case L2CAP_RX_STATE_RECV:
			err = l2cap_rx_state_recv(chan, control, skb, event);
			break;
		case L2CAP_RX_STATE_SREJ_SENT:
			err = l2cap_rx_state_srej_sent(chan, control, skb,
						       event);
			break;
		case L2CAP_RX_STATE_WAIT_P:
			err = l2cap_rx_state_wait_p(chan, control, skb, event);
			break;
		case L2CAP_RX_STATE_WAIT_F:
			err = l2cap_rx_state_wait_f(chan, control, skb, event);
			break;
		default:
			/* shut it down */
			break;
		}
	} else {
		BT_DBG("Invalid reqseq %d (next_tx_seq %d, expected_ack_seq %d",
		       control->reqseq, chan->next_tx_seq,
		       chan->expected_ack_seq);
		l2cap_send_disconn_req(chan, ECONNRESET);
	}

	return err;
}

static int l2cap_stream_rx(struct l2cap_chan *chan, struct l2cap_ctrl *control,
			   struct sk_buff *skb)
{
	/* l2cap_reassemble_sdu may free skb, hence invalidate control, so store
	 * the txseq field in advance to use it after l2cap_reassemble_sdu
	 * returns and to avoid the race condition, for example:
	 *
	 * The current thread calls:
	 *   l2cap_reassemble_sdu
	 *     chan->ops->recv == l2cap_sock_recv_cb
	 *       __sock_queue_rcv_skb
	 * Another thread calls:
	 *   bt_sock_recvmsg
	 *     skb_recv_datagram
	 *     skb_free_datagram
	 * Then the current thread tries to access control, but it was freed by
	 * skb_free_datagram.
	 */
	u16 txseq = control->txseq;

	BT_DBG("chan %p, control %p, skb %p, state %d", chan, control, skb,
	       chan->rx_state);

	if (l2cap_classify_txseq(chan, txseq) == L2CAP_TXSEQ_EXPECTED) {
		l2cap_pass_to_tx(chan, control);

		BT_DBG("buffer_seq %u->%u", chan->buffer_seq,
		       __next_seq(chan, chan->buffer_seq));

		chan->buffer_seq = __next_seq(chan, chan->buffer_seq);

		l2cap_reassemble_sdu(chan, skb, control);
	} else {
		if (chan->sdu) {
			kfree_skb(chan->sdu);
			chan->sdu = NULL;
		}
		chan->sdu_last_frag = NULL;
		chan->sdu_len = 0;

		if (skb) {
			BT_DBG("Freeing %p", skb);
			kfree_skb(skb);
		}
	}

	chan->last_acked_seq = txseq;
	chan->expected_tx_seq = __next_seq(chan, txseq);

	return 0;
}

static int l2cap_data_rcv(struct l2cap_chan *chan, struct sk_buff *skb)
{
	struct l2cap_ctrl *control = &bt_cb(skb)->l2cap;
	u16 len;
	u8 event;

	__unpack_control(chan, skb);

	len = skb->len;

	/*
	 * We can just drop the corrupted I-frame here.
	 * Receiver will miss it and start proper recovery
	 * procedures and ask for retransmission.
	 */
	if (l2cap_check_fcs(chan, skb))
		goto drop;

	if (!control->sframe && control->sar == L2CAP_SAR_START)
		len -= L2CAP_SDULEN_SIZE;

	if (chan->fcs == L2CAP_FCS_CRC16)
		len -= L2CAP_FCS_SIZE;

	if (len > chan->mps) {
		l2cap_send_disconn_req(chan, ECONNRESET);
		goto drop;
	}

	if (chan->ops->filter) {
		if (chan->ops->filter(chan, skb))
			goto drop;
	}

	if (!control->sframe) {
		int err;

		BT_DBG("iframe sar %d, reqseq %d, final %d, txseq %d",
		       control->sar, control->reqseq, control->final,
		       control->txseq);

		/* Validate F-bit - F=0 always valid, F=1 only
		 * valid in TX WAIT_F
		 */
		if (control->final && chan->tx_state != L2CAP_TX_STATE_WAIT_F)
			goto drop;

		if (chan->mode != L2CAP_MODE_STREAMING) {
			event = L2CAP_EV_RECV_IFRAME;
			err = l2cap_rx(chan, control, skb, event);
		} else {
			err = l2cap_stream_rx(chan, control, skb);
		}

		if (err)
			l2cap_send_disconn_req(chan, ECONNRESET);
	} else {
		const u8 rx_func_to_event[4] = {
			L2CAP_EV_RECV_RR, L2CAP_EV_RECV_REJ,
			L2CAP_EV_RECV_RNR, L2CAP_EV_RECV_SREJ
		};

		/* Only I-frames are expected in streaming mode */
		if (chan->mode == L2CAP_MODE_STREAMING)
			goto drop;

		BT_DBG("sframe reqseq %d, final %d, poll %d, super %d",
		       control->reqseq, control->final, control->poll,
		       control->super);

		if (len != 0) {
			BT_ERR("Trailing bytes: %d in sframe", len);
			l2cap_send_disconn_req(chan, ECONNRESET);
			goto drop;
		}

		/* Validate F and P bits */
		if (control->final && (control->poll ||
				       chan->tx_state != L2CAP_TX_STATE_WAIT_F))
			goto drop;

		event = rx_func_to_event[control->super];
		if (l2cap_rx(chan, control, skb, event))
			l2cap_send_disconn_req(chan, ECONNRESET);
	}

	return 0;

drop:
	kfree_skb(skb);
	return 0;
}

static void l2cap_chan_le_send_credits(struct l2cap_chan *chan)
{
	struct l2cap_conn *conn = chan->conn;
	struct l2cap_le_credits pkt;
	u16 return_credits = l2cap_le_rx_credits(chan);

	if (chan->rx_credits >= return_credits)
		return;

	return_credits -= chan->rx_credits;

	BT_DBG("chan %p returning %u credits to sender", chan, return_credits);

	chan->rx_credits += return_credits;

	pkt.cid     = cpu_to_le16(chan->scid);
	pkt.credits = cpu_to_le16(return_credits);

	chan->ident = l2cap_get_ident(conn);

	l2cap_send_cmd(conn, chan->ident, L2CAP_LE_CREDITS, sizeof(pkt), &pkt);
}

void l2cap_chan_rx_avail(struct l2cap_chan *chan, ssize_t rx_avail)
{
	if (chan->rx_avail == rx_avail)
		return;

	BT_DBG("chan %p has %zd bytes avail for rx", chan, rx_avail);

	chan->rx_avail = rx_avail;

	if (chan->state == BT_CONNECTED)
		l2cap_chan_le_send_credits(chan);
}

static int l2cap_ecred_recv(struct l2cap_chan *chan, struct sk_buff *skb)
{
	int err;

	BT_DBG("SDU reassemble complete: chan %p skb->len %u", chan, skb->len);

	/* Wait recv to confirm reception before updating the credits */
	err = chan->ops->recv(chan, skb);

	if (err < 0 && chan->rx_avail != -1) {
		BT_ERR("Queueing received LE L2CAP data failed");
		l2cap_send_disconn_req(chan, ECONNRESET);
		return err;
	}

	/* Update credits whenever an SDU is received */
	l2cap_chan_le_send_credits(chan);

	return err;
}

static int l2cap_ecred_data_rcv(struct l2cap_chan *chan, struct sk_buff *skb)
{
	int err;

	if (!chan->rx_credits) {
		BT_ERR("No credits to receive LE L2CAP data");
		l2cap_send_disconn_req(chan, ECONNRESET);
		return -ENOBUFS;
	}

	if (chan->imtu < skb->len) {
		BT_ERR("Too big LE L2CAP PDU");
		return -ENOBUFS;
	}

	chan->rx_credits--;
	BT_DBG("chan %p: rx_credits %u -> %u",
	       chan, chan->rx_credits + 1, chan->rx_credits);

	/* Update if remote had run out of credits, this should only happens
	 * if the remote is not using the entire MPS.
	 */
	if (!chan->rx_credits)
		l2cap_chan_le_send_credits(chan);

	err = 0;

	if (!chan->sdu) {
		u16 sdu_len;

		sdu_len = get_unaligned_le16(skb->data);
		skb_pull(skb, L2CAP_SDULEN_SIZE);

		BT_DBG("Start of new SDU. sdu_len %u skb->len %u imtu %u",
		       sdu_len, skb->len, chan->imtu);

		if (sdu_len > chan->imtu) {
			BT_ERR("Too big LE L2CAP SDU length received");
			err = -EMSGSIZE;
			goto failed;
		}

		if (skb->len > sdu_len) {
			BT_ERR("Too much LE L2CAP data received");
			err = -EINVAL;
			goto failed;
		}

		if (skb->len == sdu_len)
			return l2cap_ecred_recv(chan, skb);

		chan->sdu = skb;
		chan->sdu_len = sdu_len;
		chan->sdu_last_frag = skb;

		/* Detect if remote is not able to use the selected MPS */
		if (skb->len + L2CAP_SDULEN_SIZE < chan->mps) {
			u16 mps_len = skb->len + L2CAP_SDULEN_SIZE;

			/* Adjust the number of credits */
			BT_DBG("chan->mps %u -> %u", chan->mps, mps_len);
			chan->mps = mps_len;
			l2cap_chan_le_send_credits(chan);
		}

		return 0;
	}

	BT_DBG("SDU fragment. chan->sdu->len %u skb->len %u chan->sdu_len %u",
	       chan->sdu->len, skb->len, chan->sdu_len);

	if (chan->sdu->len + skb->len > chan->sdu_len) {
		BT_ERR("Too much LE L2CAP data received");
		err = -EINVAL;
		goto failed;
	}

	append_skb_frag(chan->sdu, skb, &chan->sdu_last_frag);
	skb = NULL;

	if (chan->sdu->len == chan->sdu_len) {
		err = l2cap_ecred_recv(chan, chan->sdu);
		if (!err) {
			chan->sdu = NULL;
			chan->sdu_last_frag = NULL;
			chan->sdu_len = 0;
		}
	}

failed:
	if (err) {
		kfree_skb(skb);
		kfree_skb(chan->sdu);
		chan->sdu = NULL;
		chan->sdu_last_frag = NULL;
		chan->sdu_len = 0;
	}

	/* We can't return an error here since we took care of the skb
	 * freeing internally. An error return would cause the caller to
	 * do a double-free of the skb.
	 */
	return 0;
}

static void l2cap_data_channel(struct l2cap_conn *conn, u16 cid,
			       struct sk_buff *skb)
{
	struct l2cap_chan *chan;

	chan = l2cap_get_chan_by_scid(conn, cid);
	if (!chan) {
		BT_DBG("unknown cid 0x%4.4x", cid);
		/* Drop packet and return */
		kfree_skb(skb);
		return;
	}

	BT_DBG("chan %p, len %d", chan, skb->len);

	/* If we receive data on a fixed channel before the info req/rsp
	 * procedure is done simply assume that the channel is supported
	 * and mark it as ready.
	 */
	if (chan->chan_type == L2CAP_CHAN_FIXED)
		l2cap_chan_ready(chan);

	if (chan->state != BT_CONNECTED)
		goto drop;

	switch (chan->mode) {
	case L2CAP_MODE_LE_FLOWCTL:
	case L2CAP_MODE_EXT_FLOWCTL:
		if (l2cap_ecred_data_rcv(chan, skb) < 0)
			goto drop;

		goto done;

	case L2CAP_MODE_BASIC:
		/* If socket recv buffers overflows we drop data here
		 * which is *bad* because L2CAP has to be reliable.
		 * But we don't have any other choice. L2CAP doesn't
		 * provide flow control mechanism. */

		if (chan->imtu < skb->len) {
			BT_ERR("Dropping L2CAP data: receive buffer overflow");
			goto drop;
		}

		if (!chan->ops->recv(chan, skb))
			goto done;
		break;

	case L2CAP_MODE_ERTM:
	case L2CAP_MODE_STREAMING:
		l2cap_data_rcv(chan, skb);
		goto done;

	default:
		BT_DBG("chan %p: bad mode 0x%2.2x", chan, chan->mode);
		break;
	}

drop:
	kfree_skb(skb);

done:
	l2cap_chan_unlock(chan);
	l2cap_chan_put(chan);
}

static void l2cap_conless_channel(struct l2cap_conn *conn, __le16 psm,
				  struct sk_buff *skb)
{
	struct hci_conn *hcon = conn->hcon;
	struct l2cap_chan *chan;

	if (hcon->type != ACL_LINK)
		goto free_skb;

	chan = l2cap_global_chan_by_psm(0, psm, &hcon->src, &hcon->dst,
					ACL_LINK);
	if (!chan)
		goto free_skb;

	BT_DBG("chan %p, len %d", chan, skb->len);

	l2cap_chan_lock(chan);

	if (chan->state != BT_BOUND && chan->state != BT_CONNECTED)
		goto drop;

	if (chan->imtu < skb->len)
		goto drop;

	/* Store remote BD_ADDR and PSM for msg_name */
	bacpy(&bt_cb(skb)->l2cap.bdaddr, &hcon->dst);
	bt_cb(skb)->l2cap.psm = psm;

	if (!chan->ops->recv(chan, skb)) {
		l2cap_chan_unlock(chan);
		l2cap_chan_put(chan);
		return;
	}

drop:
	l2cap_chan_unlock(chan);
	l2cap_chan_put(chan);
free_skb:
	kfree_skb(skb);
}

static void l2cap_recv_frame(struct l2cap_conn *conn, struct sk_buff *skb)
{
	struct l2cap_hdr *lh = (void *) skb->data;
	struct hci_conn *hcon = conn->hcon;
	u16 cid, len;
	__le16 psm;

	if (hcon->state != BT_CONNECTED) {
		BT_DBG("queueing pending rx skb");
		skb_queue_tail(&conn->pending_rx, skb);
		return;
	}

	skb_pull(skb, L2CAP_HDR_SIZE);
	cid = __le16_to_cpu(lh->cid);
	len = __le16_to_cpu(lh->len);

	if (len != skb->len) {
		kfree_skb(skb);
		return;
	}

	/* Since we can't actively block incoming LE connections we must
	 * at least ensure that we ignore incoming data from them.
	 */
	if (hcon->type == LE_LINK &&
	    hci_bdaddr_list_lookup(&hcon->hdev->reject_list, &hcon->dst,
				   bdaddr_dst_type(hcon))) {
		kfree_skb(skb);
		return;
	}

	BT_DBG("len %d, cid 0x%4.4x", len, cid);

	switch (cid) {
	case L2CAP_CID_SIGNALING:
		l2cap_sig_channel(conn, skb);
		break;

	case L2CAP_CID_CONN_LESS:
		psm = get_unaligned((__le16 *) skb->data);
		skb_pull(skb, L2CAP_PSMLEN_SIZE);
		l2cap_conless_channel(conn, psm, skb);
		break;

	case L2CAP_CID_LE_SIGNALING:
		l2cap_le_sig_channel(conn, skb);
		break;

	default:
		l2cap_data_channel(conn, cid, skb);
		break;
	}
}

static void process_pending_rx(struct work_struct *work)
{
	struct l2cap_conn *conn = container_of(work, struct l2cap_conn,
					       pending_rx_work);
	struct sk_buff *skb;

	BT_DBG("");

	mutex_lock(&conn->lock);

	while ((skb = skb_dequeue(&conn->pending_rx)))
		l2cap_recv_frame(conn, skb);

	mutex_unlock(&conn->lock);
}

static struct l2cap_conn *l2cap_conn_add(struct hci_conn *hcon)
{
	struct l2cap_conn *conn = hcon->l2cap_data;
	struct hci_chan *hchan;

	if (conn)
		return conn;

	hchan = hci_chan_create(hcon);
	if (!hchan)
		return NULL;

	conn = kzalloc(sizeof(*conn), GFP_KERNEL);
	if (!conn) {
		hci_chan_del(hchan);
		return NULL;
	}

	kref_init(&conn->ref);
	hcon->l2cap_data = conn;
	conn->hcon = hci_conn_get(hcon);
	conn->hchan = hchan;

	BT_DBG("hcon %p conn %p hchan %p", hcon, conn, hchan);

	conn->mtu = hcon->mtu;
	conn->feat_mask = 0;

	conn->local_fixed_chan = L2CAP_FC_SIG_BREDR | L2CAP_FC_CONNLESS;

	if (hci_dev_test_flag(hcon->hdev, HCI_LE_ENABLED) &&
	    (bredr_sc_enabled(hcon->hdev) ||
	     hci_dev_test_flag(hcon->hdev, HCI_FORCE_BREDR_SMP)))
		conn->local_fixed_chan |= L2CAP_FC_SMP_BREDR;

	mutex_init(&conn->ident_lock);
	mutex_init(&conn->lock);

	INIT_LIST_HEAD(&conn->chan_l);
	INIT_LIST_HEAD(&conn->users);

	INIT_DELAYED_WORK(&conn->info_timer, l2cap_info_timeout);

	skb_queue_head_init(&conn->pending_rx);
	INIT_WORK(&conn->pending_rx_work, process_pending_rx);
	INIT_DELAYED_WORK(&conn->id_addr_timer, l2cap_conn_update_id_addr);

	conn->disc_reason = HCI_ERROR_REMOTE_USER_TERM;

	return conn;
}

static bool is_valid_psm(u16 psm, u8 dst_type)
{
	if (!psm)
		return false;

	if (bdaddr_type_is_le(dst_type))
		return (psm <= 0x00ff);

	/* PSM must be odd and lsb of upper byte must be 0 */
	return ((psm & 0x0101) == 0x0001);
}

struct l2cap_chan_data {
	struct l2cap_chan *chan;
	struct pid *pid;
	int count;
};

static void l2cap_chan_by_pid(struct l2cap_chan *chan, void *data)
{
	struct l2cap_chan_data *d = data;
	struct pid *pid;

	if (chan == d->chan)
		return;

	if (!test_bit(FLAG_DEFER_SETUP, &chan->flags))
		return;

	pid = chan->ops->get_peer_pid(chan);

	/* Only count deferred channels with the same PID/PSM */
	if (d->pid != pid || chan->psm != d->chan->psm || chan->ident ||
	    chan->mode != L2CAP_MODE_EXT_FLOWCTL || chan->state != BT_CONNECT)
		return;

	d->count++;
}

int l2cap_chan_connect(struct l2cap_chan *chan, __le16 psm, u16 cid,
		       bdaddr_t *dst, u8 dst_type, u16 timeout)
{
	struct l2cap_conn *conn;
	struct hci_conn *hcon;
	struct hci_dev *hdev;
	int err;

	BT_DBG("%pMR -> %pMR (type %u) psm 0x%4.4x mode 0x%2.2x", &chan->src,
	       dst, dst_type, __le16_to_cpu(psm), chan->mode);

	hdev = hci_get_route(dst, &chan->src, chan->src_type);
	if (!hdev)
		return -EHOSTUNREACH;

	hci_dev_lock(hdev);

	if (!is_valid_psm(__le16_to_cpu(psm), dst_type) && !cid &&
	    chan->chan_type != L2CAP_CHAN_RAW) {
		err = -EINVAL;
		goto done;
	}

	if (chan->chan_type == L2CAP_CHAN_CONN_ORIENTED && !psm) {
		err = -EINVAL;
		goto done;
	}

	if (chan->chan_type == L2CAP_CHAN_FIXED && !cid) {
		err = -EINVAL;
		goto done;
	}

	switch (chan->mode) {
	case L2CAP_MODE_BASIC:
		break;
	case L2CAP_MODE_LE_FLOWCTL:
		break;
	case L2CAP_MODE_EXT_FLOWCTL:
		if (!enable_ecred) {
			err = -EOPNOTSUPP;
			goto done;
		}
		break;
	case L2CAP_MODE_ERTM:
	case L2CAP_MODE_STREAMING:
		if (!disable_ertm)
			break;
		fallthrough;
	default:
		err = -EOPNOTSUPP;
		goto done;
	}

	switch (chan->state) {
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
	bacpy(&chan->dst, dst);
	chan->dst_type = dst_type;

	chan->psm = psm;
	chan->dcid = cid;

	if (bdaddr_type_is_le(dst_type)) {
		/* Convert from L2CAP channel address type to HCI address type
		 */
		if (dst_type == BDADDR_LE_PUBLIC)
			dst_type = ADDR_LE_DEV_PUBLIC;
		else
			dst_type = ADDR_LE_DEV_RANDOM;

		if (hci_dev_test_flag(hdev, HCI_ADVERTISING))
			hcon = hci_connect_le(hdev, dst, dst_type, false,
					      chan->sec_level, timeout,
					      HCI_ROLE_SLAVE, 0, 0);
		else
			hcon = hci_connect_le_scan(hdev, dst, dst_type,
						   chan->sec_level, timeout,
						   CONN_REASON_L2CAP_CHAN);

	} else {
		u8 auth_type = l2cap_get_auth_type(chan);
		hcon = hci_connect_acl(hdev, dst, chan->sec_level, auth_type,
				       CONN_REASON_L2CAP_CHAN, timeout);
	}

	if (IS_ERR(hcon)) {
		err = PTR_ERR(hcon);
		goto done;
	}

	conn = l2cap_conn_add(hcon);
	if (!conn) {
		hci_conn_drop(hcon);
		err = -ENOMEM;
		goto done;
	}

	if (chan->mode == L2CAP_MODE_EXT_FLOWCTL) {
		struct l2cap_chan_data data;

		data.chan = chan;
		data.pid = chan->ops->get_peer_pid(chan);
		data.count = 1;

		l2cap_chan_list(conn, l2cap_chan_by_pid, &data);

		/* Check if there isn't too many channels being connected */
		if (data.count > L2CAP_ECRED_CONN_SCID_MAX) {
			hci_conn_drop(hcon);
			err = -EPROTO;
			goto done;
		}
	}

	mutex_lock(&conn->lock);
	l2cap_chan_lock(chan);

	if (cid && __l2cap_get_chan_by_dcid(conn, cid)) {
		hci_conn_drop(hcon);
		err = -EBUSY;
		goto chan_unlock;
	}

	/* Update source addr of the socket */
	bacpy(&chan->src, &hcon->src);
	chan->src_type = bdaddr_src_type(hcon);

	__l2cap_chan_add(conn, chan);

	/* l2cap_chan_add takes its own ref so we can drop this one */
	hci_conn_drop(hcon);

	l2cap_state_change(chan, BT_CONNECT);
	__set_chan_timer(chan, chan->ops->get_sndtimeo(chan));

	/* Release chan->sport so that it can be reused by other
	 * sockets (as it's only used for listening sockets).
	 */
	write_lock(&chan_list_lock);
	chan->sport = 0;
	write_unlock(&chan_list_lock);

	if (hcon->state == BT_CONNECTED) {
		if (chan->chan_type != L2CAP_CHAN_CONN_ORIENTED) {
			__clear_chan_timer(chan);
			if (l2cap_chan_check_security(chan, true))
				l2cap_state_change(chan, BT_CONNECTED);
		} else
			l2cap_do_start(chan);
	}

	err = 0;

chan_unlock:
	l2cap_chan_unlock(chan);
	mutex_unlock(&conn->lock);
done:
	hci_dev_unlock(hdev);
	hci_dev_put(hdev);
	return err;
}
EXPORT_SYMBOL_GPL(l2cap_chan_connect);

static void l2cap_ecred_reconfigure(struct l2cap_chan *chan)
{
	struct l2cap_conn *conn = chan->conn;
	DEFINE_RAW_FLEX(struct l2cap_ecred_reconf_req, pdu, scid, 1);

	pdu->mtu = cpu_to_le16(chan->imtu);
	pdu->mps = cpu_to_le16(chan->mps);
	pdu->scid[0] = cpu_to_le16(chan->scid);

	chan->ident = l2cap_get_ident(conn);

	l2cap_send_cmd(conn, chan->ident, L2CAP_ECRED_RECONF_REQ,
		       sizeof(pdu), &pdu);
}

int l2cap_chan_reconfigure(struct l2cap_chan *chan, __u16 mtu)
{
	if (chan->imtu > mtu)
		return -EINVAL;

	BT_DBG("chan %p mtu 0x%4.4x", chan, mtu);

	chan->imtu = mtu;

	l2cap_ecred_reconfigure(chan);

	return 0;
}

/* ---- L2CAP interface with lower layer (HCI) ---- */

int l2cap_connect_ind(struct hci_dev *hdev, bdaddr_t *bdaddr)
{
	int exact = 0, lm1 = 0, lm2 = 0;
	struct l2cap_chan *c;

	BT_DBG("hdev %s, bdaddr %pMR", hdev->name, bdaddr);

	/* Find listening sockets and check their link_mode */
	read_lock(&chan_list_lock);
	list_for_each_entry(c, &chan_list, global_l) {
		if (c->state != BT_LISTEN)
			continue;

		if (!bacmp(&c->src, &hdev->bdaddr)) {
			lm1 |= HCI_LM_ACCEPT;
			if (test_bit(FLAG_ROLE_SWITCH, &c->flags))
				lm1 |= HCI_LM_MASTER;
			exact++;
		} else if (!bacmp(&c->src, BDADDR_ANY)) {
			lm2 |= HCI_LM_ACCEPT;
			if (test_bit(FLAG_ROLE_SWITCH, &c->flags))
				lm2 |= HCI_LM_MASTER;
		}
	}
	read_unlock(&chan_list_lock);

	return exact ? lm1 : lm2;
}

/* Find the next fixed channel in BT_LISTEN state, continue iteration
 * from an existing channel in the list or from the beginning of the
 * global list (by passing NULL as first parameter).
 */
static struct l2cap_chan *l2cap_global_fixed_chan(struct l2cap_chan *c,
						  struct hci_conn *hcon)
{
	u8 src_type = bdaddr_src_type(hcon);

	read_lock(&chan_list_lock);

	if (c)
		c = list_next_entry(c, global_l);
	else
		c = list_entry(chan_list.next, typeof(*c), global_l);

	list_for_each_entry_from(c, &chan_list, global_l) {
		if (c->chan_type != L2CAP_CHAN_FIXED)
			continue;
		if (c->state != BT_LISTEN)
			continue;
		if (bacmp(&c->src, &hcon->src) && bacmp(&c->src, BDADDR_ANY))
			continue;
		if (src_type != c->src_type)
			continue;

		c = l2cap_chan_hold_unless_zero(c);
		read_unlock(&chan_list_lock);
		return c;
	}

	read_unlock(&chan_list_lock);

	return NULL;
}

static void l2cap_connect_cfm(struct hci_conn *hcon, u8 status)
{
	struct hci_dev *hdev = hcon->hdev;
	struct l2cap_conn *conn;
	struct l2cap_chan *pchan;
	u8 dst_type;

	if (hcon->type != ACL_LINK && hcon->type != LE_LINK)
		return;

	BT_DBG("hcon %p bdaddr %pMR status %d", hcon, &hcon->dst, status);

	if (status) {
		l2cap_conn_del(hcon, bt_to_errno(status));
		return;
	}

	conn = l2cap_conn_add(hcon);
	if (!conn)
		return;

	dst_type = bdaddr_dst_type(hcon);

	/* If device is blocked, do not create channels for it */
	if (hci_bdaddr_list_lookup(&hdev->reject_list, &hcon->dst, dst_type))
		return;

	/* Find fixed channels and notify them of the new connection. We
	 * use multiple individual lookups, continuing each time where
	 * we left off, because the list lock would prevent calling the
	 * potentially sleeping l2cap_chan_lock() function.
	 */
	pchan = l2cap_global_fixed_chan(NULL, hcon);
	while (pchan) {
		struct l2cap_chan *chan, *next;

		/* Client fixed channels should override server ones */
		if (__l2cap_get_chan_by_dcid(conn, pchan->scid))
			goto next;

		l2cap_chan_lock(pchan);
		chan = pchan->ops->new_connection(pchan);
		if (chan) {
			bacpy(&chan->src, &hcon->src);
			bacpy(&chan->dst, &hcon->dst);
			chan->src_type = bdaddr_src_type(hcon);
			chan->dst_type = dst_type;

			__l2cap_chan_add(conn, chan);
		}

		l2cap_chan_unlock(pchan);
next:
		next = l2cap_global_fixed_chan(pchan, hcon);
		l2cap_chan_put(pchan);
		pchan = next;
	}

	l2cap_conn_ready(conn);
}

int l2cap_disconn_ind(struct hci_conn *hcon)
{
	struct l2cap_conn *conn = hcon->l2cap_data;

	BT_DBG("hcon %p", hcon);

	if (!conn)
		return HCI_ERROR_REMOTE_USER_TERM;
	return conn->disc_reason;
}

static void l2cap_disconn_cfm(struct hci_conn *hcon, u8 reason)
{
	if (hcon->type != ACL_LINK && hcon->type != LE_LINK)
		return;

	BT_DBG("hcon %p reason %d", hcon, reason);

	l2cap_conn_del(hcon, bt_to_errno(reason));
}

static inline void l2cap_check_encryption(struct l2cap_chan *chan, u8 encrypt)
{
	if (chan->chan_type != L2CAP_CHAN_CONN_ORIENTED)
		return;

	if (encrypt == 0x00) {
		if (chan->sec_level == BT_SECURITY_MEDIUM) {
			__set_chan_timer(chan, L2CAP_ENC_TIMEOUT);
		} else if (chan->sec_level == BT_SECURITY_HIGH ||
			   chan->sec_level == BT_SECURITY_FIPS)
			l2cap_chan_close(chan, ECONNREFUSED);
	} else {
		if (chan->sec_level == BT_SECURITY_MEDIUM)
			__clear_chan_timer(chan);
	}
}

static void l2cap_security_cfm(struct hci_conn *hcon, u8 status, u8 encrypt)
{
	struct l2cap_conn *conn = hcon->l2cap_data;
	struct l2cap_chan *chan;

	if (!conn)
		return;

	BT_DBG("conn %p status 0x%2.2x encrypt %u", conn, status, encrypt);

	mutex_lock(&conn->lock);

	list_for_each_entry(chan, &conn->chan_l, list) {
		l2cap_chan_lock(chan);

		BT_DBG("chan %p scid 0x%4.4x state %s", chan, chan->scid,
		       state_to_string(chan->state));

		if (!status && encrypt)
			chan->sec_level = hcon->sec_level;

		if (!__l2cap_no_conn_pending(chan)) {
			l2cap_chan_unlock(chan);
			continue;
		}

		if (!status && (chan->state == BT_CONNECTED ||
				chan->state == BT_CONFIG)) {
			chan->ops->resume(chan);
			l2cap_check_encryption(chan, encrypt);
			l2cap_chan_unlock(chan);
			continue;
		}

		if (chan->state == BT_CONNECT) {
			if (!status && l2cap_check_enc_key_size(hcon))
				l2cap_start_connection(chan);
			else
				__set_chan_timer(chan, L2CAP_DISC_TIMEOUT);
		} else if (chan->state == BT_CONNECT2 &&
			   !(chan->mode == L2CAP_MODE_EXT_FLOWCTL ||
			     chan->mode == L2CAP_MODE_LE_FLOWCTL)) {
			struct l2cap_conn_rsp rsp;
			__u16 res, stat;

			if (!status && l2cap_check_enc_key_size(hcon)) {
				if (test_bit(FLAG_DEFER_SETUP, &chan->flags)) {
					res = L2CAP_CR_PEND;
					stat = L2CAP_CS_AUTHOR_PEND;
					chan->ops->defer(chan);
				} else {
					l2cap_state_change(chan, BT_CONFIG);
					res = L2CAP_CR_SUCCESS;
					stat = L2CAP_CS_NO_INFO;
				}
			} else {
				l2cap_state_change(chan, BT_DISCONN);
				__set_chan_timer(chan, L2CAP_DISC_TIMEOUT);
				res = L2CAP_CR_SEC_BLOCK;
				stat = L2CAP_CS_NO_INFO;
			}

			rsp.scid   = cpu_to_le16(chan->dcid);
			rsp.dcid   = cpu_to_le16(chan->scid);
			rsp.result = cpu_to_le16(res);
			rsp.status = cpu_to_le16(stat);
			l2cap_send_cmd(conn, chan->ident, L2CAP_CONN_RSP,
				       sizeof(rsp), &rsp);

			if (!test_bit(CONF_REQ_SENT, &chan->conf_state) &&
			    res == L2CAP_CR_SUCCESS) {
				char buf[128];
				set_bit(CONF_REQ_SENT, &chan->conf_state);
				l2cap_send_cmd(conn, l2cap_get_ident(conn),
					       L2CAP_CONF_REQ,
					       l2cap_build_conf_req(chan, buf, sizeof(buf)),
					       buf);
				chan->num_conf_req++;
			}
		}

		l2cap_chan_unlock(chan);
	}

	mutex_unlock(&conn->lock);
}

/* Append fragment into frame respecting the maximum len of rx_skb */
static int l2cap_recv_frag(struct l2cap_conn *conn, struct sk_buff *skb,
			   u16 len)
{
	if (!conn->rx_skb) {
		/* Allocate skb for the complete frame (with header) */
		conn->rx_skb = bt_skb_alloc(len, GFP_KERNEL);
		if (!conn->rx_skb)
			return -ENOMEM;
		/* Init rx_len */
		conn->rx_len = len;
	}

	/* Copy as much as the rx_skb can hold */
	len = min_t(u16, len, skb->len);
	skb_copy_from_linear_data(skb, skb_put(conn->rx_skb, len), len);
	skb_pull(skb, len);
	conn->rx_len -= len;

	return len;
}

static int l2cap_recv_len(struct l2cap_conn *conn, struct sk_buff *skb)
{
	struct sk_buff *rx_skb;
	int len;

	/* Append just enough to complete the header */
	len = l2cap_recv_frag(conn, skb, L2CAP_LEN_SIZE - conn->rx_skb->len);

	/* If header could not be read just continue */
	if (len < 0 || conn->rx_skb->len < L2CAP_LEN_SIZE)
		return len;

	rx_skb = conn->rx_skb;
	len = get_unaligned_le16(rx_skb->data);

	/* Check if rx_skb has enough space to received all fragments */
	if (len + (L2CAP_HDR_SIZE - L2CAP_LEN_SIZE) <= skb_tailroom(rx_skb)) {
		/* Update expected len */
		conn->rx_len = len + (L2CAP_HDR_SIZE - L2CAP_LEN_SIZE);
		return L2CAP_LEN_SIZE;
	}

	/* Reset conn->rx_skb since it will need to be reallocated in order to
	 * fit all fragments.
	 */
	conn->rx_skb = NULL;

	/* Reallocates rx_skb using the exact expected length */
	len = l2cap_recv_frag(conn, rx_skb,
			      len + (L2CAP_HDR_SIZE - L2CAP_LEN_SIZE));
	kfree_skb(rx_skb);

	return len;
}

static void l2cap_recv_reset(struct l2cap_conn *conn)
{
	kfree_skb(conn->rx_skb);
	conn->rx_skb = NULL;
	conn->rx_len = 0;
}

struct l2cap_conn *l2cap_conn_hold_unless_zero(struct l2cap_conn *c)
{
	if (!c)
		return NULL;

	BT_DBG("conn %p orig refcnt %u", c, kref_read(&c->ref));

	if (!kref_get_unless_zero(&c->ref))
		return NULL;

	return c;
}

void l2cap_recv_acldata(struct hci_conn *hcon, struct sk_buff *skb, u16 flags)
{
	struct l2cap_conn *conn;
	int len;

	/* Lock hdev to access l2cap_data to avoid race with l2cap_conn_del */
	hci_dev_lock(hcon->hdev);

	conn = hcon->l2cap_data;

	if (!conn)
		conn = l2cap_conn_add(hcon);

	conn = l2cap_conn_hold_unless_zero(conn);

	hci_dev_unlock(hcon->hdev);

	if (!conn) {
		kfree_skb(skb);
		return;
	}

	BT_DBG("conn %p len %u flags 0x%x", conn, skb->len, flags);

	mutex_lock(&conn->lock);

	switch (flags) {
	case ACL_START:
	case ACL_START_NO_FLUSH:
	case ACL_COMPLETE:
		if (conn->rx_skb) {
			BT_ERR("Unexpected start frame (len %d)", skb->len);
			l2cap_recv_reset(conn);
			l2cap_conn_unreliable(conn, ECOMM);
		}

		/* Start fragment may not contain the L2CAP length so just
		 * copy the initial byte when that happens and use conn->mtu as
		 * expected length.
		 */
		if (skb->len < L2CAP_LEN_SIZE) {
			l2cap_recv_frag(conn, skb, conn->mtu);
			break;
		}

		len = get_unaligned_le16(skb->data) + L2CAP_HDR_SIZE;

		if (len == skb->len) {
			/* Complete frame received */
			l2cap_recv_frame(conn, skb);
			goto unlock;
		}

		BT_DBG("Start: total len %d, frag len %u", len, skb->len);

		if (skb->len > len) {
			BT_ERR("Frame is too long (len %u, expected len %d)",
			       skb->len, len);
			l2cap_conn_unreliable(conn, ECOMM);
			goto drop;
		}

		/* Append fragment into frame (with header) */
		if (l2cap_recv_frag(conn, skb, len) < 0)
			goto drop;

		break;

	case ACL_CONT:
		BT_DBG("Cont: frag len %u (expecting %u)", skb->len, conn->rx_len);

		if (!conn->rx_skb) {
			BT_ERR("Unexpected continuation frame (len %d)", skb->len);
			l2cap_conn_unreliable(conn, ECOMM);
			goto drop;
		}

		/* Complete the L2CAP length if it has not been read */
		if (conn->rx_skb->len < L2CAP_LEN_SIZE) {
			if (l2cap_recv_len(conn, skb) < 0) {
				l2cap_conn_unreliable(conn, ECOMM);
				goto drop;
			}

			/* Header still could not be read just continue */
			if (conn->rx_skb->len < L2CAP_LEN_SIZE)
				break;
		}

		if (skb->len > conn->rx_len) {
			BT_ERR("Fragment is too long (len %u, expected %u)",
			       skb->len, conn->rx_len);
			l2cap_recv_reset(conn);
			l2cap_conn_unreliable(conn, ECOMM);
			goto drop;
		}

		/* Append fragment into frame (with header) */
		l2cap_recv_frag(conn, skb, skb->len);

		if (!conn->rx_len) {
			/* Complete frame received. l2cap_recv_frame
			 * takes ownership of the skb so set the global
			 * rx_skb pointer to NULL first.
			 */
			struct sk_buff *rx_skb = conn->rx_skb;
			conn->rx_skb = NULL;
			l2cap_recv_frame(conn, rx_skb);
		}
		break;
	}

drop:
	kfree_skb(skb);
unlock:
	mutex_unlock(&conn->lock);
	l2cap_conn_put(conn);
}

static struct hci_cb l2cap_cb = {
	.name		= "L2CAP",
	.connect_cfm	= l2cap_connect_cfm,
	.disconn_cfm	= l2cap_disconn_cfm,
	.security_cfm	= l2cap_security_cfm,
};

static int l2cap_debugfs_show(struct seq_file *f, void *p)
{
	struct l2cap_chan *c;

	read_lock(&chan_list_lock);

	list_for_each_entry(c, &chan_list, global_l) {
		seq_printf(f, "%pMR (%u) %pMR (%u) %d %d 0x%4.4x 0x%4.4x %d %d %d %d\n",
			   &c->src, c->src_type, &c->dst, c->dst_type,
			   c->state, __le16_to_cpu(c->psm),
			   c->scid, c->dcid, c->imtu, c->omtu,
			   c->sec_level, c->mode);
	}

	read_unlock(&chan_list_lock);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(l2cap_debugfs);

static struct dentry *l2cap_debugfs;

int __init l2cap_init(void)
{
	int err;

	err = l2cap_init_sockets();
	if (err < 0)
		return err;

	hci_register_cb(&l2cap_cb);

	if (IS_ERR_OR_NULL(bt_debugfs))
		return 0;

	l2cap_debugfs = debugfs_create_file("l2cap", 0444, bt_debugfs,
					    NULL, &l2cap_debugfs_fops);

	return 0;
}

void l2cap_exit(void)
{
	debugfs_remove(l2cap_debugfs);
	hci_unregister_cb(&l2cap_cb);
	l2cap_cleanup_sockets();
}

module_param(disable_ertm, bool, 0644);
MODULE_PARM_DESC(disable_ertm, "Disable enhanced retransmission mode");

module_param(enable_ecred, bool, 0644);
MODULE_PARM_DESC(enable_ecred, "Enable enhanced credit flow control mode");
