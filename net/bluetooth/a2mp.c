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

static struct l2cap_ops a2mp_chan_ops = {
	.name = "L2CAP A2MP channel",
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
