/*
   Copyright (c) 2010,2011 Code Aurora Forum.  All rights reserved.
   Copyright (c) 2011,2012 Intel Corp.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2 and
   only version 2 as published by the Free Software Foundation.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
*/

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include <net/bluetooth/l2cap.h>
#include <net/bluetooth/a2mp.h>

/* A2MP build & send command helper functions */
static struct a2mp_cmd *__a2mp_build(u8 code, u8 ident, u16 len, void *data)
{
	struct a2mp_cmd *cmd;
	int plen;

	plen = sizeof(*cmd) + len;
	cmd = kzalloc(plen, GFP_KERNEL);
	if (!cmd)
		return NULL;

	cmd->code = code;
	cmd->ident = ident;
	cmd->len = cpu_to_le16(len);

	memcpy(cmd->data, data, len);

	return cmd;
}

static void a2mp_send(struct amp_mgr *mgr, u8 code, u8 ident, u16 len,
		      void *data)
{
	struct l2cap_chan *chan = mgr->a2mp_chan;
	struct a2mp_cmd *cmd;
	u16 total_len = len + sizeof(*cmd);
	struct kvec iv;
	struct msghdr msg;

	cmd = __a2mp_build(code, ident, len, data);
	if (!cmd)
		return;

	iv.iov_base = cmd;
	iv.iov_len = total_len;

	memset(&msg, 0, sizeof(msg));

	msg.msg_iov = (struct iovec *) &iv;
	msg.msg_iovlen = 1;

	l2cap_chan_send(chan, &msg, total_len, 0);

	kfree(cmd);
}

/* Processing A2MP messages */
static int a2mp_command_rej(struct amp_mgr *mgr, struct sk_buff *skb,
			    struct a2mp_cmd *hdr)
{
	struct a2mp_cmd_rej *rej = (void *) skb->data;

	if (le16_to_cpu(hdr->len) < sizeof(*rej))
		return -EINVAL;

	BT_DBG("ident %d reason %d", hdr->ident, le16_to_cpu(rej->reason));

	skb_pull(skb, sizeof(*rej));

	return 0;
}

/* Handle A2MP signalling */
static int a2mp_chan_recv_cb(struct l2cap_chan *chan, struct sk_buff *skb)
{
	struct a2mp_cmd *hdr = (void *) skb->data;
	struct amp_mgr *mgr = chan->data;
	int err = 0;

	amp_mgr_get(mgr);

	while (skb->len >= sizeof(*hdr)) {
		struct a2mp_cmd *hdr = (void *) skb->data;
		u16 len = le16_to_cpu(hdr->len);

		BT_DBG("code 0x%02x id %d len %d", hdr->code, hdr->ident, len);

		skb_pull(skb, sizeof(*hdr));

		if (len > skb->len || !hdr->ident) {
			err = -EINVAL;
			break;
		}

		mgr->ident = hdr->ident;

		switch (hdr->code) {
		case A2MP_COMMAND_REJ:
			a2mp_command_rej(mgr, skb, hdr);
			break;

		case A2MP_DISCOVER_REQ:
		case A2MP_CHANGE_NOTIFY:
		case A2MP_GETINFO_REQ:
		case A2MP_GETAMPASSOC_REQ:
		case A2MP_CREATEPHYSLINK_REQ:
		case A2MP_DISCONNPHYSLINK_REQ:
		case A2MP_CHANGE_RSP:
		case A2MP_DISCOVER_RSP:
		case A2MP_GETINFO_RSP:
		case A2MP_GETAMPASSOC_RSP:
		case A2MP_CREATEPHYSLINK_RSP:
		case A2MP_DISCONNPHYSLINK_RSP:
		default:
			BT_ERR("Unknown A2MP sig cmd 0x%2.2x", hdr->code);
			err = -EINVAL;
			break;
		}
	}

	if (err) {
		struct a2mp_cmd_rej rej;
		rej.reason = __constant_cpu_to_le16(0);

		BT_DBG("Send A2MP Rej: cmd 0x%2.2x err %d", hdr->code, err);

		a2mp_send(mgr, A2MP_COMMAND_REJ, hdr->ident, sizeof(rej),
			  &rej);
	}

	/* Always free skb and return success error code to prevent
	   from sending L2CAP Disconnect over A2MP channel */
	kfree_skb(skb);

	amp_mgr_put(mgr);

	return 0;
}

static void a2mp_chan_close_cb(struct l2cap_chan *chan)
{
	l2cap_chan_destroy(chan);
}

static void a2mp_chan_state_change_cb(struct l2cap_chan *chan, int state)
{
	struct amp_mgr *mgr = chan->data;

	if (!mgr)
		return;

	BT_DBG("chan %p state %s", chan, state_to_string(state));

	chan->state = state;

	switch (state) {
	case BT_CLOSED:
		if (mgr)
			amp_mgr_put(mgr);
		break;
	}
}

static struct sk_buff *a2mp_chan_alloc_skb_cb(struct l2cap_chan *chan,
					      unsigned long len, int nb)
{
	return bt_skb_alloc(len, GFP_KERNEL);
}

static struct l2cap_chan *a2mp_chan_no_new_conn_cb(struct l2cap_chan *chan)
{
	BT_ERR("new_connection for chan %p not implemented", chan);

	return NULL;
}

static void a2mp_chan_no_teardown_cb(struct l2cap_chan *chan, int err)
{
	BT_ERR("teardown for chan %p not implemented", chan);
}

static void a2mp_chan_no_ready(struct l2cap_chan *chan)
{
	BT_ERR("ready for chan %p not implemented", chan);
}

static struct l2cap_ops a2mp_chan_ops = {
	.name = "L2CAP A2MP channel",
	.recv = a2mp_chan_recv_cb,
	.close = a2mp_chan_close_cb,
	.state_change = a2mp_chan_state_change_cb,
	.alloc_skb = a2mp_chan_alloc_skb_cb,

	/* Not implemented for A2MP */
	.new_connection = a2mp_chan_no_new_conn_cb,
	.teardown = a2mp_chan_no_teardown_cb,
	.ready = a2mp_chan_no_ready,
};

static struct l2cap_chan *a2mp_chan_open(struct l2cap_conn *conn)
{
	struct l2cap_chan *chan;
	int err;

	chan = l2cap_chan_create();
	if (!chan)
		return NULL;

	BT_DBG("chan %p", chan);

	hci_conn_hold(conn->hcon);

	chan->omtu = L2CAP_A2MP_DEFAULT_MTU;
	chan->imtu = L2CAP_A2MP_DEFAULT_MTU;
	chan->flush_to = L2CAP_DEFAULT_FLUSH_TO;

	chan->ops = &a2mp_chan_ops;

	l2cap_chan_set_defaults(chan);
	chan->remote_max_tx = chan->max_tx;
	chan->remote_tx_win = chan->tx_win;

	chan->retrans_timeout = L2CAP_DEFAULT_RETRANS_TO;
	chan->monitor_timeout = L2CAP_DEFAULT_MONITOR_TO;

	skb_queue_head_init(&chan->tx_q);

	chan->mode = L2CAP_MODE_ERTM;

	err = l2cap_ertm_init(chan);
	if (err < 0) {
		l2cap_chan_del(chan, 0);
		return NULL;
	}

	chan->conf_state = 0;

	l2cap_chan_add(conn, chan);

	chan->remote_mps = chan->omtu;
	chan->mps = chan->omtu;

	chan->state = BT_CONNECTED;

	return chan;
}

/* AMP Manager functions */
void amp_mgr_get(struct amp_mgr *mgr)
{
	BT_DBG("mgr %p", mgr);

	kref_get(&mgr->kref);
}

static void amp_mgr_destroy(struct kref *kref)
{
	struct amp_mgr *mgr = container_of(kref, struct amp_mgr, kref);

	BT_DBG("mgr %p", mgr);

	kfree(mgr);
}

int amp_mgr_put(struct amp_mgr *mgr)
{
	BT_DBG("mgr %p", mgr);

	return kref_put(&mgr->kref, &amp_mgr_destroy);
}

static struct amp_mgr *amp_mgr_create(struct l2cap_conn *conn)
{
	struct amp_mgr *mgr;
	struct l2cap_chan *chan;

	mgr = kzalloc(sizeof(*mgr), GFP_KERNEL);
	if (!mgr)
		return NULL;

	BT_DBG("conn %p mgr %p", conn, mgr);

	mgr->l2cap_conn = conn;

	chan = a2mp_chan_open(conn);
	if (!chan) {
		kfree(mgr);
		return NULL;
	}

	mgr->a2mp_chan = chan;
	chan->data = mgr;

	conn->hcon->amp_mgr = mgr;

	kref_init(&mgr->kref);

	return mgr;
}
