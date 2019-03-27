/*-
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0
 *
 * Copyright (c) 2006 Mellanox Technologies Ltd.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * $Id$
 */
#include "sdp.h"

#define SDP_MAJV_MINV 0x22

SDP_MODPARAM_SINT(sdp_link_layer_ib_only, 1, "Support only link layer of "
		"type Infiniband");

enum {
	SDP_HH_SIZE = 76,
	SDP_HAH_SIZE = 180,
};

static void
sdp_qp_event_handler(struct ib_event *event, void *data)
{
}

static int
sdp_get_max_dev_sge(struct ib_device *dev)
{
	struct ib_device_attr *device_attr;
	static int max_sges = -1;

	if (max_sges > 0)
		goto out;

	device_attr = &dev->attrs;
	max_sges = device_attr->max_sge;

out:
	return max_sges;
}

static int
sdp_init_qp(struct socket *sk, struct rdma_cm_id *id)
{
	struct ib_qp_init_attr qp_init_attr = {
		.event_handler = sdp_qp_event_handler,
		.cap.max_send_wr = SDP_TX_SIZE,
		.cap.max_recv_wr = SDP_RX_SIZE,
        	.sq_sig_type = IB_SIGNAL_REQ_WR,
        	.qp_type = IB_QPT_RC,
	};
	struct ib_device *device = id->device;
	struct sdp_sock *ssk;
	int rc;

	sdp_dbg(sk, "%s\n", __func__);

	ssk = sdp_sk(sk);
	ssk->max_sge = sdp_get_max_dev_sge(device);
	sdp_dbg(sk, "Max sges: %d\n", ssk->max_sge);

	qp_init_attr.cap.max_send_sge = MIN(ssk->max_sge, SDP_MAX_SEND_SGES);
	sdp_dbg(sk, "Setting max send sge to: %d\n",
	    qp_init_attr.cap.max_send_sge);
		
	qp_init_attr.cap.max_recv_sge = MIN(ssk->max_sge, SDP_MAX_RECV_SGES);
	sdp_dbg(sk, "Setting max recv sge to: %d\n",
	    qp_init_attr.cap.max_recv_sge);
		
	ssk->sdp_dev = ib_get_client_data(device, &sdp_client);
	if (!ssk->sdp_dev) {
		sdp_warn(sk, "SDP not available on device %s\n", device->name);
		rc = -ENODEV;
		goto err_rx;
	}

	rc = sdp_rx_ring_create(ssk, device);
	if (rc)
		goto err_rx;

	rc = sdp_tx_ring_create(ssk, device);
	if (rc)
		goto err_tx;

	qp_init_attr.recv_cq = ssk->rx_ring.cq;
	qp_init_attr.send_cq = ssk->tx_ring.cq;

	rc = rdma_create_qp(id, ssk->sdp_dev->pd, &qp_init_attr);
	if (rc) {
		sdp_warn(sk, "Unable to create QP: %d.\n", rc);
		goto err_qp;
	}
	ssk->qp = id->qp;
	ssk->ib_device = device;
	ssk->qp_active = 1;
	ssk->context.device = device;

	sdp_dbg(sk, "%s done\n", __func__);
	return 0;

err_qp:
	sdp_tx_ring_destroy(ssk);
err_tx:
	sdp_rx_ring_destroy(ssk);
err_rx:
	return rc;
}

static int
sdp_connect_handler(struct socket *sk, struct rdma_cm_id *id,
    struct rdma_cm_event *event)
{
	struct sockaddr_in *src_addr;
	struct sockaddr_in *dst_addr;
	struct socket *child;
	const struct sdp_hh *h;
	struct sdp_sock *ssk;
	int rc;

	sdp_dbg(sk, "%s %p -> %p\n", __func__, sdp_sk(sk)->id, id);

	h = event->param.conn.private_data;
	SDP_DUMP_PACKET(sk, "RX", NULL, &h->bsdh);

	if (!h->max_adverts)
		return -EINVAL;

	child = sonewconn(sk, SS_ISCONNECTED);
	if (!child)
		return -ENOMEM;

	ssk = sdp_sk(child);
	rc = sdp_init_qp(child, id);
	if (rc)
		return rc;
	SDP_WLOCK(ssk);
	id->context = ssk;
	ssk->id = id;
	ssk->socket = child;
	ssk->cred = crhold(child->so_cred);
	dst_addr = (struct sockaddr_in *)&id->route.addr.dst_addr;
	src_addr = (struct sockaddr_in *)&id->route.addr.src_addr;
	ssk->fport = dst_addr->sin_port;
	ssk->faddr = dst_addr->sin_addr.s_addr;
	ssk->lport = src_addr->sin_port;
	ssk->max_bufs = ntohs(h->bsdh.bufs);
	atomic_set(&ssk->tx_ring.credits, ssk->max_bufs);
	ssk->min_bufs = tx_credits(ssk) / 4;
	ssk->xmit_size_goal = ntohl(h->localrcvsz) - sizeof(struct sdp_bsdh);
	sdp_init_buffers(ssk, rcvbuf_initial_size);
	ssk->state = TCPS_SYN_RECEIVED;
	SDP_WUNLOCK(ssk);

	return 0;
}

static int
sdp_response_handler(struct socket *sk, struct rdma_cm_id *id,
    struct rdma_cm_event *event)
{
	const struct sdp_hah *h;
	struct sockaddr_in *dst_addr;
	struct sdp_sock *ssk;
	sdp_dbg(sk, "%s\n", __func__);

	ssk = sdp_sk(sk);
	SDP_WLOCK(ssk);
	ssk->state = TCPS_ESTABLISHED;
	sdp_set_default_moderation(ssk);
	if (ssk->flags & SDP_DROPPED) {
		SDP_WUNLOCK(ssk);
		return 0;
	}
	if (sk->so_options & SO_KEEPALIVE)
		sdp_start_keepalive_timer(sk);
	h = event->param.conn.private_data;
	SDP_DUMP_PACKET(sk, "RX", NULL, &h->bsdh);
	ssk->max_bufs = ntohs(h->bsdh.bufs);
	atomic_set(&ssk->tx_ring.credits, ssk->max_bufs);
	ssk->min_bufs = tx_credits(ssk) / 4;
	ssk->xmit_size_goal =
		ntohl(h->actrcvsz) - sizeof(struct sdp_bsdh);
	ssk->poll_cq = 1;

	dst_addr = (struct sockaddr_in *)&id->route.addr.dst_addr;
	ssk->fport = dst_addr->sin_port;
	ssk->faddr = dst_addr->sin_addr.s_addr;
	soisconnected(sk);
	SDP_WUNLOCK(ssk);

	return 0;
}

static int
sdp_connected_handler(struct socket *sk, struct rdma_cm_event *event)
{
	struct sdp_sock *ssk;

	sdp_dbg(sk, "%s\n", __func__);

	ssk = sdp_sk(sk);
	SDP_WLOCK(ssk);
	ssk->state = TCPS_ESTABLISHED;

	sdp_set_default_moderation(ssk);

	if (sk->so_options & SO_KEEPALIVE)
		sdp_start_keepalive_timer(sk);

	if ((ssk->flags & SDP_DROPPED) == 0)
		soisconnected(sk);
	SDP_WUNLOCK(ssk);
	return 0;
}

static int
sdp_disconnected_handler(struct socket *sk)
{
	struct sdp_sock *ssk;

	ssk = sdp_sk(sk);
	sdp_dbg(sk, "%s\n", __func__);

	SDP_WLOCK_ASSERT(ssk);
	if (sdp_sk(sk)->state == TCPS_SYN_RECEIVED) {
		sdp_connected_handler(sk, NULL);

		if (rcv_nxt(ssk))
			return 0;
	}

	return -ECONNRESET;
}

int
sdp_cma_handler(struct rdma_cm_id *id, struct rdma_cm_event *event)
{
	struct rdma_conn_param conn_param;
	struct socket *sk;
	struct sdp_sock *ssk;
	struct sdp_hah hah;
	struct sdp_hh hh;

	int rc = 0;

	ssk = id->context;
	sk = NULL;
	if (ssk)
		sk = ssk->socket;
	if (!ssk || !sk || !ssk->id) {
		sdp_dbg(sk,
		    "cm_id is being torn down, event %d, ssk %p, sk %p, id %p\n",
		       	event->event, ssk, sk, id);
		return event->event == RDMA_CM_EVENT_CONNECT_REQUEST ?
			-EINVAL : 0;
	}

	sdp_dbg(sk, "%s event %d id %p\n", __func__, event->event, id);
	switch (event->event) {
	case RDMA_CM_EVENT_ADDR_RESOLVED:
		sdp_dbg(sk, "RDMA_CM_EVENT_ADDR_RESOLVED\n");

		if (sdp_link_layer_ib_only &&
			rdma_node_get_transport(id->device->node_type) == 
				RDMA_TRANSPORT_IB &&
			rdma_port_get_link_layer(id->device, id->port_num) !=
				IB_LINK_LAYER_INFINIBAND) {
			sdp_dbg(sk, "Link layer is: %d. Only IB link layer "
				"is allowed\n",
				rdma_port_get_link_layer(id->device, id->port_num));
			rc = -ENETUNREACH;
			break;
		}

		rc = rdma_resolve_route(id, SDP_ROUTE_TIMEOUT);
		break;
	case RDMA_CM_EVENT_ADDR_ERROR:
		sdp_dbg(sk, "RDMA_CM_EVENT_ADDR_ERROR\n");
		rc = -ENETUNREACH;
		break;
	case RDMA_CM_EVENT_ROUTE_RESOLVED:
		sdp_dbg(sk, "RDMA_CM_EVENT_ROUTE_RESOLVED : %p\n", id);
		rc = sdp_init_qp(sk, id);
		if (rc)
			break;
		atomic_set(&sdp_sk(sk)->remote_credits,
				rx_ring_posted(sdp_sk(sk)));
		memset(&hh, 0, sizeof hh);
		hh.bsdh.mid = SDP_MID_HELLO;
		hh.bsdh.len = htonl(sizeof(struct sdp_hh));
		hh.max_adverts = 1;
		hh.ipv_cap = 0x40;
		hh.majv_minv = SDP_MAJV_MINV;
		sdp_init_buffers(sdp_sk(sk), rcvbuf_initial_size);
		hh.bsdh.bufs = htons(rx_ring_posted(sdp_sk(sk)));
		hh.localrcvsz = hh.desremrcvsz = htonl(sdp_sk(sk)->recv_bytes);
		hh.max_adverts = 0x1;
		sdp_sk(sk)->laddr = 
			((struct sockaddr_in *)&id->route.addr.src_addr)->sin_addr.s_addr;
		memset(&conn_param, 0, sizeof conn_param);
		conn_param.private_data_len = sizeof hh;
		conn_param.private_data = &hh;
		conn_param.responder_resources = 4 /* TODO */;
		conn_param.initiator_depth = 4 /* TODO */;
		conn_param.retry_count = SDP_RETRY_COUNT;
		SDP_DUMP_PACKET(NULL, "TX", NULL, &hh.bsdh);
		rc = rdma_connect(id, &conn_param);
		break;
	case RDMA_CM_EVENT_ROUTE_ERROR:
		sdp_dbg(sk, "RDMA_CM_EVENT_ROUTE_ERROR : %p\n", id);
		rc = -ETIMEDOUT;
		break;
	case RDMA_CM_EVENT_CONNECT_REQUEST:
		sdp_dbg(sk, "RDMA_CM_EVENT_CONNECT_REQUEST\n");
		rc = sdp_connect_handler(sk, id, event);
		if (rc) {
			sdp_dbg(sk, "Destroying qp\n");
			rdma_reject(id, NULL, 0);
			break;
		}
		ssk = id->context;
		atomic_set(&ssk->remote_credits, rx_ring_posted(ssk));
		memset(&hah, 0, sizeof hah);
		hah.bsdh.mid = SDP_MID_HELLO_ACK;
		hah.bsdh.bufs = htons(rx_ring_posted(ssk));
		hah.bsdh.len = htonl(sizeof(struct sdp_hah));
		hah.majv_minv = SDP_MAJV_MINV;
		hah.ext_max_adverts = 1; /* Doesn't seem to be mandated by spec,
					    but just in case */
		hah.actrcvsz = htonl(ssk->recv_bytes);
		memset(&conn_param, 0, sizeof conn_param);
		conn_param.private_data_len = sizeof hah;
		conn_param.private_data = &hah;
		conn_param.responder_resources = 4 /* TODO */;
		conn_param.initiator_depth = 4 /* TODO */;
		conn_param.retry_count = SDP_RETRY_COUNT;
		SDP_DUMP_PACKET(sk, "TX", NULL, &hah.bsdh);
		rc = rdma_accept(id, &conn_param);
		if (rc) {
			ssk->id = NULL;
			id->qp = NULL;
			id->context = NULL;
		}
		break;
	case RDMA_CM_EVENT_CONNECT_RESPONSE:
		sdp_dbg(sk, "RDMA_CM_EVENT_CONNECT_RESPONSE\n");
		rc = sdp_response_handler(sk, id, event);
		if (rc) {
			sdp_dbg(sk, "Destroying qp\n");
			rdma_reject(id, NULL, 0);
		} else
			rc = rdma_accept(id, NULL);
		break;
	case RDMA_CM_EVENT_CONNECT_ERROR:
		sdp_dbg(sk, "RDMA_CM_EVENT_CONNECT_ERROR\n");
		rc = -ETIMEDOUT;
		break;
	case RDMA_CM_EVENT_UNREACHABLE:
		sdp_dbg(sk, "RDMA_CM_EVENT_UNREACHABLE\n");
		rc = -ENETUNREACH;
		break;
	case RDMA_CM_EVENT_REJECTED:
		sdp_dbg(sk, "RDMA_CM_EVENT_REJECTED\n");
		rc = -ECONNREFUSED;
		break;
	case RDMA_CM_EVENT_ESTABLISHED:
		sdp_dbg(sk, "RDMA_CM_EVENT_ESTABLISHED\n");
		sdp_sk(sk)->laddr = 
			((struct sockaddr_in *)&id->route.addr.src_addr)->sin_addr.s_addr;
		rc = sdp_connected_handler(sk, event);
		break;
	case RDMA_CM_EVENT_DISCONNECTED: /* This means DREQ/DREP received */
		sdp_dbg(sk, "RDMA_CM_EVENT_DISCONNECTED\n");

		SDP_WLOCK(ssk);
		if (ssk->state == TCPS_LAST_ACK) {
			sdp_cancel_dreq_wait_timeout(ssk);

			sdp_dbg(sk, "%s: waiting for Infiniband tear down\n",
				__func__);
		}
		ssk->qp_active = 0;
		SDP_WUNLOCK(ssk);
		rdma_disconnect(id);
		SDP_WLOCK(ssk);
		if (ssk->state != TCPS_TIME_WAIT) {
			if (ssk->state == TCPS_CLOSE_WAIT) {
				sdp_dbg(sk, "IB teardown while in "
					"TCPS_CLOSE_WAIT taking reference to "
					"let close() finish the work\n");
			}
			rc = sdp_disconnected_handler(sk);
			if (rc)
				rc = -EPIPE;
		}
		SDP_WUNLOCK(ssk);
		break;
	case RDMA_CM_EVENT_TIMEWAIT_EXIT:
		sdp_dbg(sk, "RDMA_CM_EVENT_TIMEWAIT_EXIT\n");
		SDP_WLOCK(ssk);
		rc = sdp_disconnected_handler(sk);
		SDP_WUNLOCK(ssk);
		break;
	case RDMA_CM_EVENT_DEVICE_REMOVAL:
		sdp_dbg(sk, "RDMA_CM_EVENT_DEVICE_REMOVAL\n");
		rc = -ENETRESET;
		break;
	default:
		printk(KERN_ERR "SDP: Unexpected CMA event: %d\n",
		       event->event);
		rc = -ECONNABORTED;
		break;
	}

	sdp_dbg(sk, "event %d done. status %d\n", event->event, rc);

	if (rc) {
		SDP_WLOCK(ssk);
		if (ssk->id == id) {
			ssk->id = NULL;
			id->qp = NULL;
			id->context = NULL;
			if (sdp_notify(ssk, -rc))
				SDP_WUNLOCK(ssk);
		} else
			SDP_WUNLOCK(ssk);
	}

	return rc;
}
