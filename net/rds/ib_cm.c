/*
 * Copyright (c) 2006, 2019 Oracle and/or its affiliates. All rights reserved.
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
 */
#include <linux/dmapool.h>
#include <linux/kernel.h>
#include <linux/in.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/ratelimit.h>
#include <net/addrconf.h>
#include <rdma/ib_cm.h>

#include "rds_single_path.h"
#include "rds.h"
#include "ib.h"
#include "ib_mr.h"

/*
 * Set the selected protocol version
 */
static void rds_ib_set_protocol(struct rds_connection *conn, unsigned int version)
{
	conn->c_version = version;
}

/*
 * Set up flow control
 */
static void rds_ib_set_flow_control(struct rds_connection *conn, u32 credits)
{
	struct rds_ib_connection *ic = conn->c_transport_data;

	if (rds_ib_sysctl_flow_control && credits != 0) {
		/* We're doing flow control */
		ic->i_flowctl = 1;
		rds_ib_send_add_credits(conn, credits);
	} else {
		ic->i_flowctl = 0;
	}
}

/*
 * Tune RNR behavior. Without flow control, we use a rather
 * low timeout, but not the absolute minimum - this should
 * be tunable.
 *
 * We already set the RNR retry count to 7 (which is the
 * smallest infinite number :-) above.
 * If flow control is off, we want to change this back to 0
 * so that we learn quickly when our credit accounting is
 * buggy.
 *
 * Caller passes in a qp_attr pointer - don't waste stack spacv
 * by allocation this twice.
 */
static void
rds_ib_tune_rnr(struct rds_ib_connection *ic, struct ib_qp_attr *attr)
{
	int ret;

	attr->min_rnr_timer = IB_RNR_TIMER_000_32;
	ret = ib_modify_qp(ic->i_cm_id->qp, attr, IB_QP_MIN_RNR_TIMER);
	if (ret)
		printk(KERN_NOTICE "ib_modify_qp(IB_QP_MIN_RNR_TIMER): err=%d\n", -ret);
}

/*
 * Connection established.
 * We get here for both outgoing and incoming connection.
 */
void rds_ib_cm_connect_complete(struct rds_connection *conn, struct rdma_cm_event *event)
{
	struct rds_ib_connection *ic = conn->c_transport_data;
	const union rds_ib_conn_priv *dp = NULL;
	struct ib_qp_attr qp_attr;
	__be64 ack_seq = 0;
	__be32 credit = 0;
	u8 major = 0;
	u8 minor = 0;
	int err;

	dp = event->param.conn.private_data;
	if (conn->c_isv6) {
		if (event->param.conn.private_data_len >=
		    sizeof(struct rds6_ib_connect_private)) {
			major = dp->ricp_v6.dp_protocol_major;
			minor = dp->ricp_v6.dp_protocol_minor;
			credit = dp->ricp_v6.dp_credit;
			/* dp structure start is not guaranteed to be 8 bytes
			 * aligned.  Since dp_ack_seq is 64-bit extended load
			 * operations can be used so go through get_unaligned
			 * to avoid unaligned errors.
			 */
			ack_seq = get_unaligned(&dp->ricp_v6.dp_ack_seq);
		}
	} else if (event->param.conn.private_data_len >=
		   sizeof(struct rds_ib_connect_private)) {
		major = dp->ricp_v4.dp_protocol_major;
		minor = dp->ricp_v4.dp_protocol_minor;
		credit = dp->ricp_v4.dp_credit;
		ack_seq = get_unaligned(&dp->ricp_v4.dp_ack_seq);
	}

	/* make sure it isn't empty data */
	if (major) {
		rds_ib_set_protocol(conn, RDS_PROTOCOL(major, minor));
		rds_ib_set_flow_control(conn, be32_to_cpu(credit));
	}

	if (conn->c_version < RDS_PROTOCOL_VERSION) {
		if (conn->c_version != RDS_PROTOCOL_COMPAT_VERSION) {
			pr_notice("RDS/IB: Connection <%pI6c,%pI6c> version %u.%u no longer supported\n",
				  &conn->c_laddr, &conn->c_faddr,
				  RDS_PROTOCOL_MAJOR(conn->c_version),
				  RDS_PROTOCOL_MINOR(conn->c_version));
			rds_conn_destroy(conn);
			return;
		}
	}

	pr_notice("RDS/IB: %s conn connected <%pI6c,%pI6c,%d> version %u.%u%s\n",
		  ic->i_active_side ? "Active" : "Passive",
		  &conn->c_laddr, &conn->c_faddr, conn->c_tos,
		  RDS_PROTOCOL_MAJOR(conn->c_version),
		  RDS_PROTOCOL_MINOR(conn->c_version),
		  ic->i_flowctl ? ", flow control" : "");

	/* receive sl from the peer */
	ic->i_sl = ic->i_cm_id->route.path_rec->sl;

	atomic_set(&ic->i_cq_quiesce, 0);

	/* Init rings and fill recv. this needs to wait until protocol
	 * negotiation is complete, since ring layout is different
	 * from 3.1 to 4.1.
	 */
	rds_ib_send_init_ring(ic);
	rds_ib_recv_init_ring(ic);
	/* Post receive buffers - as a side effect, this will update
	 * the posted credit count. */
	rds_ib_recv_refill(conn, 1, GFP_KERNEL);

	/* Tune RNR behavior */
	rds_ib_tune_rnr(ic, &qp_attr);

	qp_attr.qp_state = IB_QPS_RTS;
	err = ib_modify_qp(ic->i_cm_id->qp, &qp_attr, IB_QP_STATE);
	if (err)
		printk(KERN_NOTICE "ib_modify_qp(IB_QP_STATE, RTS): err=%d\n", err);

	/* update ib_device with this local ipaddr */
	err = rds_ib_update_ipaddr(ic->rds_ibdev, &conn->c_laddr);
	if (err)
		printk(KERN_ERR "rds_ib_update_ipaddr failed (%d)\n",
			err);

	/* If the peer gave us the last packet it saw, process this as if
	 * we had received a regular ACK. */
	if (dp) {
		if (ack_seq)
			rds_send_drop_acked(conn, be64_to_cpu(ack_seq),
					    NULL);
	}

	conn->c_proposed_version = conn->c_version;
	rds_connect_complete(conn);
}

static void rds_ib_cm_fill_conn_param(struct rds_connection *conn,
				      struct rdma_conn_param *conn_param,
				      union rds_ib_conn_priv *dp,
				      u32 protocol_version,
				      u32 max_responder_resources,
				      u32 max_initiator_depth,
				      bool isv6)
{
	struct rds_ib_connection *ic = conn->c_transport_data;
	struct rds_ib_device *rds_ibdev = ic->rds_ibdev;

	memset(conn_param, 0, sizeof(struct rdma_conn_param));

	conn_param->responder_resources =
		min_t(u32, rds_ibdev->max_responder_resources, max_responder_resources);
	conn_param->initiator_depth =
		min_t(u32, rds_ibdev->max_initiator_depth, max_initiator_depth);
	conn_param->retry_count = min_t(unsigned int, rds_ib_retry_count, 7);
	conn_param->rnr_retry_count = 7;

	if (dp) {
		memset(dp, 0, sizeof(*dp));
		if (isv6) {
			dp->ricp_v6.dp_saddr = conn->c_laddr;
			dp->ricp_v6.dp_daddr = conn->c_faddr;
			dp->ricp_v6.dp_protocol_major =
			    RDS_PROTOCOL_MAJOR(protocol_version);
			dp->ricp_v6.dp_protocol_minor =
			    RDS_PROTOCOL_MINOR(protocol_version);
			dp->ricp_v6.dp_protocol_minor_mask =
			    cpu_to_be16(RDS_IB_SUPPORTED_PROTOCOLS);
			dp->ricp_v6.dp_ack_seq =
			    cpu_to_be64(rds_ib_piggyb_ack(ic));
			dp->ricp_v6.dp_cmn.ricpc_dp_toss = conn->c_tos;

			conn_param->private_data = &dp->ricp_v6;
			conn_param->private_data_len = sizeof(dp->ricp_v6);
		} else {
			dp->ricp_v4.dp_saddr = conn->c_laddr.s6_addr32[3];
			dp->ricp_v4.dp_daddr = conn->c_faddr.s6_addr32[3];
			dp->ricp_v4.dp_protocol_major =
			    RDS_PROTOCOL_MAJOR(protocol_version);
			dp->ricp_v4.dp_protocol_minor =
			    RDS_PROTOCOL_MINOR(protocol_version);
			dp->ricp_v4.dp_protocol_minor_mask =
			    cpu_to_be16(RDS_IB_SUPPORTED_PROTOCOLS);
			dp->ricp_v4.dp_ack_seq =
			    cpu_to_be64(rds_ib_piggyb_ack(ic));
			dp->ricp_v4.dp_cmn.ricpc_dp_toss = conn->c_tos;

			conn_param->private_data = &dp->ricp_v4;
			conn_param->private_data_len = sizeof(dp->ricp_v4);
		}

		/* Advertise flow control */
		if (ic->i_flowctl) {
			unsigned int credits;

			credits = IB_GET_POST_CREDITS
				(atomic_read(&ic->i_credits));
			if (isv6)
				dp->ricp_v6.dp_credit = cpu_to_be32(credits);
			else
				dp->ricp_v4.dp_credit = cpu_to_be32(credits);
			atomic_sub(IB_SET_POST_CREDITS(credits),
				   &ic->i_credits);
		}
	}
}

static void rds_ib_cq_event_handler(struct ib_event *event, void *data)
{
	rdsdebug("event %u (%s) data %p\n",
		 event->event, ib_event_msg(event->event), data);
}

/* Plucking the oldest entry from the ring can be done concurrently with
 * the thread refilling the ring.  Each ring operation is protected by
 * spinlocks and the transient state of refilling doesn't change the
 * recording of which entry is oldest.
 *
 * This relies on IB only calling one cq comp_handler for each cq so that
 * there will only be one caller of rds_recv_incoming() per RDS connection.
 */
static void rds_ib_cq_comp_handler_recv(struct ib_cq *cq, void *context)
{
	struct rds_connection *conn = context;
	struct rds_ib_connection *ic = conn->c_transport_data;

	rdsdebug("conn %p cq %p\n", conn, cq);

	rds_ib_stats_inc(s_ib_evt_handler_call);

	tasklet_schedule(&ic->i_recv_tasklet);
}

static void poll_scq(struct rds_ib_connection *ic, struct ib_cq *cq,
		     struct ib_wc *wcs)
{
	int nr, i;
	struct ib_wc *wc;

	while ((nr = ib_poll_cq(cq, RDS_IB_WC_MAX, wcs)) > 0) {
		for (i = 0; i < nr; i++) {
			wc = wcs + i;
			rdsdebug("wc wr_id 0x%llx status %u byte_len %u imm_data %u\n",
				 (unsigned long long)wc->wr_id, wc->status,
				 wc->byte_len, be32_to_cpu(wc->ex.imm_data));

			if (wc->wr_id <= ic->i_send_ring.w_nr ||
			    wc->wr_id == RDS_IB_ACK_WR_ID)
				rds_ib_send_cqe_handler(ic, wc);
			else
				rds_ib_mr_cqe_handler(ic, wc);

		}
	}
}

static void rds_ib_tasklet_fn_send(unsigned long data)
{
	struct rds_ib_connection *ic = (struct rds_ib_connection *)data;
	struct rds_connection *conn = ic->conn;

	rds_ib_stats_inc(s_ib_tasklet_call);

	/* if cq has been already reaped, ignore incoming cq event */
	if (atomic_read(&ic->i_cq_quiesce))
		return;

	poll_scq(ic, ic->i_send_cq, ic->i_send_wc);
	ib_req_notify_cq(ic->i_send_cq, IB_CQ_NEXT_COMP);
	poll_scq(ic, ic->i_send_cq, ic->i_send_wc);

	if (rds_conn_up(conn) &&
	    (!test_bit(RDS_LL_SEND_FULL, &conn->c_flags) ||
	    test_bit(0, &conn->c_map_queued)))
		rds_send_xmit(&ic->conn->c_path[0]);
}

static void poll_rcq(struct rds_ib_connection *ic, struct ib_cq *cq,
		     struct ib_wc *wcs,
		     struct rds_ib_ack_state *ack_state)
{
	int nr, i;
	struct ib_wc *wc;

	while ((nr = ib_poll_cq(cq, RDS_IB_WC_MAX, wcs)) > 0) {
		for (i = 0; i < nr; i++) {
			wc = wcs + i;
			rdsdebug("wc wr_id 0x%llx status %u byte_len %u imm_data %u\n",
				 (unsigned long long)wc->wr_id, wc->status,
				 wc->byte_len, be32_to_cpu(wc->ex.imm_data));

			rds_ib_recv_cqe_handler(ic, wc, ack_state);
		}
	}
}

static void rds_ib_tasklet_fn_recv(unsigned long data)
{
	struct rds_ib_connection *ic = (struct rds_ib_connection *)data;
	struct rds_connection *conn = ic->conn;
	struct rds_ib_device *rds_ibdev = ic->rds_ibdev;
	struct rds_ib_ack_state state;

	if (!rds_ibdev)
		rds_conn_drop(conn);

	rds_ib_stats_inc(s_ib_tasklet_call);

	/* if cq has been already reaped, ignore incoming cq event */
	if (atomic_read(&ic->i_cq_quiesce))
		return;

	memset(&state, 0, sizeof(state));
	poll_rcq(ic, ic->i_recv_cq, ic->i_recv_wc, &state);
	ib_req_notify_cq(ic->i_recv_cq, IB_CQ_SOLICITED);
	poll_rcq(ic, ic->i_recv_cq, ic->i_recv_wc, &state);

	if (state.ack_next_valid)
		rds_ib_set_ack(ic, state.ack_next, state.ack_required);
	if (state.ack_recv_valid && state.ack_recv > ic->i_ack_recv) {
		rds_send_drop_acked(conn, state.ack_recv, NULL);
		ic->i_ack_recv = state.ack_recv;
	}

	if (rds_conn_up(conn))
		rds_ib_attempt_ack(ic);
}

static void rds_ib_qp_event_handler(struct ib_event *event, void *data)
{
	struct rds_connection *conn = data;
	struct rds_ib_connection *ic = conn->c_transport_data;

	rdsdebug("conn %p ic %p event %u (%s)\n", conn, ic, event->event,
		 ib_event_msg(event->event));

	switch (event->event) {
	case IB_EVENT_COMM_EST:
		rdma_notify(ic->i_cm_id, IB_EVENT_COMM_EST);
		break;
	default:
		rdsdebug("Fatal QP Event %u (%s) - connection %pI6c->%pI6c, reconnecting\n",
			 event->event, ib_event_msg(event->event),
			 &conn->c_laddr, &conn->c_faddr);
		rds_conn_drop(conn);
		break;
	}
}

static void rds_ib_cq_comp_handler_send(struct ib_cq *cq, void *context)
{
	struct rds_connection *conn = context;
	struct rds_ib_connection *ic = conn->c_transport_data;

	rdsdebug("conn %p cq %p\n", conn, cq);

	rds_ib_stats_inc(s_ib_evt_handler_call);

	tasklet_schedule(&ic->i_send_tasklet);
}

static inline int ibdev_get_unused_vector(struct rds_ib_device *rds_ibdev)
{
	int min = rds_ibdev->vector_load[rds_ibdev->dev->num_comp_vectors - 1];
	int index = rds_ibdev->dev->num_comp_vectors - 1;
	int i;

	for (i = rds_ibdev->dev->num_comp_vectors - 1; i >= 0; i--) {
		if (rds_ibdev->vector_load[i] < min) {
			index = i;
			min = rds_ibdev->vector_load[i];
		}
	}

	rds_ibdev->vector_load[index]++;
	return index;
}

static inline void ibdev_put_vector(struct rds_ib_device *rds_ibdev, int index)
{
	rds_ibdev->vector_load[index]--;
}

/* Allocate DMA coherent memory to be used to store struct rds_header for
 * sending/receiving packets.  The pointers to the DMA memory and the
 * associated DMA addresses are stored in two arrays.
 *
 * @ibdev: the IB device
 * @pool: the DMA memory pool
 * @dma_addrs: pointer to the array for storing DMA addresses
 * @num_hdrs: number of headers to allocate
 *
 * It returns the pointer to the array storing the DMA memory pointers.  On
 * error, NULL pointer is returned.
 */
struct rds_header **rds_dma_hdrs_alloc(struct ib_device *ibdev,
				       struct dma_pool *pool,
				       dma_addr_t **dma_addrs, u32 num_hdrs)
{
	struct rds_header **hdrs;
	dma_addr_t *hdr_daddrs;
	u32 i;

	hdrs = kvmalloc_node(sizeof(*hdrs) * num_hdrs, GFP_KERNEL,
			     ibdev_to_node(ibdev));
	if (!hdrs)
		return NULL;

	hdr_daddrs = kvmalloc_node(sizeof(*hdr_daddrs) * num_hdrs, GFP_KERNEL,
				   ibdev_to_node(ibdev));
	if (!hdr_daddrs) {
		kvfree(hdrs);
		return NULL;
	}

	for (i = 0; i < num_hdrs; i++) {
		hdrs[i] = dma_pool_zalloc(pool, GFP_KERNEL, &hdr_daddrs[i]);
		if (!hdrs[i]) {
			rds_dma_hdrs_free(pool, hdrs, hdr_daddrs, i);
			return NULL;
		}
	}

	*dma_addrs = hdr_daddrs;
	return hdrs;
}

/* Free the DMA memory used to store struct rds_header.
 *
 * @pool: the DMA memory pool
 * @hdrs: pointer to the array storing DMA memory pointers
 * @dma_addrs: pointer to the array storing DMA addresses
 * @num_hdars: number of headers to free.
 */
void rds_dma_hdrs_free(struct dma_pool *pool, struct rds_header **hdrs,
		       dma_addr_t *dma_addrs, u32 num_hdrs)
{
	u32 i;

	for (i = 0; i < num_hdrs; i++)
		dma_pool_free(pool, hdrs[i], dma_addrs[i]);
	kvfree(hdrs);
	kvfree(dma_addrs);
}

/*
 * This needs to be very careful to not leave IS_ERR pointers around for
 * cleanup to trip over.
 */
static int rds_ib_setup_qp(struct rds_connection *conn)
{
	struct rds_ib_connection *ic = conn->c_transport_data;
	struct ib_device *dev = ic->i_cm_id->device;
	struct ib_qp_init_attr attr;
	struct ib_cq_init_attr cq_attr = {};
	struct rds_ib_device *rds_ibdev;
	unsigned long max_wrs;
	int ret, fr_queue_space;
	struct dma_pool *pool;

	/*
	 * It's normal to see a null device if an incoming connection races
	 * with device removal, so we don't print a warning.
	 */
	rds_ibdev = rds_ib_get_client_data(dev);
	if (!rds_ibdev)
		return -EOPNOTSUPP;

	/* The fr_queue_space is currently set to 512, to add extra space on
	 * completion queue and send queue. This extra space is used for FRWR
	 * registration and invalidation work requests
	 */
	fr_queue_space = RDS_IB_DEFAULT_FR_WR;

	/* add the conn now so that connection establishment has the dev */
	rds_ib_add_conn(rds_ibdev, conn);

	max_wrs = rds_ibdev->max_wrs < rds_ib_sysctl_max_send_wr + 1 ?
		rds_ibdev->max_wrs - 1 : rds_ib_sysctl_max_send_wr;
	if (ic->i_send_ring.w_nr != max_wrs)
		rds_ib_ring_resize(&ic->i_send_ring, max_wrs);

	max_wrs = rds_ibdev->max_wrs < rds_ib_sysctl_max_recv_wr + 1 ?
		rds_ibdev->max_wrs - 1 : rds_ib_sysctl_max_recv_wr;
	if (ic->i_recv_ring.w_nr != max_wrs)
		rds_ib_ring_resize(&ic->i_recv_ring, max_wrs);

	/* Protection domain and memory range */
	ic->i_pd = rds_ibdev->pd;

	ic->i_scq_vector = ibdev_get_unused_vector(rds_ibdev);
	cq_attr.cqe = ic->i_send_ring.w_nr + fr_queue_space + 1;
	cq_attr.comp_vector = ic->i_scq_vector;
	ic->i_send_cq = ib_create_cq(dev, rds_ib_cq_comp_handler_send,
				     rds_ib_cq_event_handler, conn,
				     &cq_attr);
	if (IS_ERR(ic->i_send_cq)) {
		ret = PTR_ERR(ic->i_send_cq);
		ic->i_send_cq = NULL;
		ibdev_put_vector(rds_ibdev, ic->i_scq_vector);
		rdsdebug("ib_create_cq send failed: %d\n", ret);
		goto rds_ibdev_out;
	}

	ic->i_rcq_vector = ibdev_get_unused_vector(rds_ibdev);
	cq_attr.cqe = ic->i_recv_ring.w_nr;
	cq_attr.comp_vector = ic->i_rcq_vector;
	ic->i_recv_cq = ib_create_cq(dev, rds_ib_cq_comp_handler_recv,
				     rds_ib_cq_event_handler, conn,
				     &cq_attr);
	if (IS_ERR(ic->i_recv_cq)) {
		ret = PTR_ERR(ic->i_recv_cq);
		ic->i_recv_cq = NULL;
		ibdev_put_vector(rds_ibdev, ic->i_rcq_vector);
		rdsdebug("ib_create_cq recv failed: %d\n", ret);
		goto send_cq_out;
	}

	ret = ib_req_notify_cq(ic->i_send_cq, IB_CQ_NEXT_COMP);
	if (ret) {
		rdsdebug("ib_req_notify_cq send failed: %d\n", ret);
		goto recv_cq_out;
	}

	ret = ib_req_notify_cq(ic->i_recv_cq, IB_CQ_SOLICITED);
	if (ret) {
		rdsdebug("ib_req_notify_cq recv failed: %d\n", ret);
		goto recv_cq_out;
	}

	/* XXX negotiate max send/recv with remote? */
	memset(&attr, 0, sizeof(attr));
	attr.event_handler = rds_ib_qp_event_handler;
	attr.qp_context = conn;
	/* + 1 to allow for the single ack message */
	attr.cap.max_send_wr = ic->i_send_ring.w_nr + fr_queue_space + 1;
	attr.cap.max_recv_wr = ic->i_recv_ring.w_nr + 1;
	attr.cap.max_send_sge = rds_ibdev->max_sge;
	attr.cap.max_recv_sge = RDS_IB_RECV_SGE;
	attr.sq_sig_type = IB_SIGNAL_REQ_WR;
	attr.qp_type = IB_QPT_RC;
	attr.send_cq = ic->i_send_cq;
	attr.recv_cq = ic->i_recv_cq;

	/*
	 * XXX this can fail if max_*_wr is too large?  Are we supposed
	 * to back off until we get a value that the hardware can support?
	 */
	ret = rdma_create_qp(ic->i_cm_id, ic->i_pd, &attr);
	if (ret) {
		rdsdebug("rdma_create_qp failed: %d\n", ret);
		goto recv_cq_out;
	}

	pool = rds_ibdev->rid_hdrs_pool;
	ic->i_send_hdrs = rds_dma_hdrs_alloc(dev, pool, &ic->i_send_hdrs_dma,
					     ic->i_send_ring.w_nr);
	if (!ic->i_send_hdrs) {
		ret = -ENOMEM;
		rdsdebug("DMA send hdrs alloc failed\n");
		goto qp_out;
	}

	ic->i_recv_hdrs = rds_dma_hdrs_alloc(dev, pool, &ic->i_recv_hdrs_dma,
					     ic->i_recv_ring.w_nr);
	if (!ic->i_recv_hdrs) {
		ret = -ENOMEM;
		rdsdebug("DMA recv hdrs alloc failed\n");
		goto send_hdrs_dma_out;
	}

	ic->i_ack = dma_pool_zalloc(pool, GFP_KERNEL,
				    &ic->i_ack_dma);
	if (!ic->i_ack) {
		ret = -ENOMEM;
		rdsdebug("DMA ack header alloc failed\n");
		goto recv_hdrs_dma_out;
	}

	ic->i_sends = vzalloc_node(array_size(sizeof(struct rds_ib_send_work),
					      ic->i_send_ring.w_nr),
				   ibdev_to_node(dev));
	if (!ic->i_sends) {
		ret = -ENOMEM;
		rdsdebug("send allocation failed\n");
		goto ack_dma_out;
	}

	ic->i_recvs = vzalloc_node(array_size(sizeof(struct rds_ib_recv_work),
					      ic->i_recv_ring.w_nr),
				   ibdev_to_node(dev));
	if (!ic->i_recvs) {
		ret = -ENOMEM;
		rdsdebug("recv allocation failed\n");
		goto sends_out;
	}

	rds_ib_recv_init_ack(ic);

	rdsdebug("conn %p pd %p cq %p %p\n", conn, ic->i_pd,
		 ic->i_send_cq, ic->i_recv_cq);

	goto out;

sends_out:
	vfree(ic->i_sends);

ack_dma_out:
	dma_pool_free(pool, ic->i_ack, ic->i_ack_dma);
	ic->i_ack = NULL;

recv_hdrs_dma_out:
	rds_dma_hdrs_free(pool, ic->i_recv_hdrs, ic->i_recv_hdrs_dma,
			  ic->i_recv_ring.w_nr);
	ic->i_recv_hdrs = NULL;
	ic->i_recv_hdrs_dma = NULL;

send_hdrs_dma_out:
	rds_dma_hdrs_free(pool, ic->i_send_hdrs, ic->i_send_hdrs_dma,
			  ic->i_send_ring.w_nr);
	ic->i_send_hdrs = NULL;
	ic->i_send_hdrs_dma = NULL;

qp_out:
	rdma_destroy_qp(ic->i_cm_id);
recv_cq_out:
	ib_destroy_cq(ic->i_recv_cq);
	ic->i_recv_cq = NULL;
send_cq_out:
	ib_destroy_cq(ic->i_send_cq);
	ic->i_send_cq = NULL;
rds_ibdev_out:
	rds_ib_remove_conn(rds_ibdev, conn);
out:
	rds_ib_dev_put(rds_ibdev);

	return ret;
}

static u32 rds_ib_protocol_compatible(struct rdma_cm_event *event, bool isv6)
{
	const union rds_ib_conn_priv *dp = event->param.conn.private_data;
	u8 data_len, major, minor;
	u32 version = 0;
	__be16 mask;
	u16 common;

	/*
	 * rdma_cm private data is odd - when there is any private data in the
	 * request, we will be given a pretty large buffer without telling us the
	 * original size. The only way to tell the difference is by looking at
	 * the contents, which are initialized to zero.
	 * If the protocol version fields aren't set, this is a connection attempt
	 * from an older version. This could be 3.0 or 2.0 - we can't tell.
	 * We really should have changed this for OFED 1.3 :-(
	 */

	/* Be paranoid. RDS always has privdata */
	if (!event->param.conn.private_data_len) {
		printk(KERN_NOTICE "RDS incoming connection has no private data, "
			"rejecting\n");
		return 0;
	}

	if (isv6) {
		data_len = sizeof(struct rds6_ib_connect_private);
		major = dp->ricp_v6.dp_protocol_major;
		minor = dp->ricp_v6.dp_protocol_minor;
		mask = dp->ricp_v6.dp_protocol_minor_mask;
	} else {
		data_len = sizeof(struct rds_ib_connect_private);
		major = dp->ricp_v4.dp_protocol_major;
		minor = dp->ricp_v4.dp_protocol_minor;
		mask = dp->ricp_v4.dp_protocol_minor_mask;
	}

	/* Even if len is crap *now* I still want to check it. -ASG */
	if (event->param.conn.private_data_len < data_len || major == 0)
		return RDS_PROTOCOL_4_0;

	common = be16_to_cpu(mask) & RDS_IB_SUPPORTED_PROTOCOLS;
	if (major == 4 && common) {
		version = RDS_PROTOCOL_4_0;
		while ((common >>= 1) != 0)
			version++;
	} else if (RDS_PROTOCOL_COMPAT_VERSION ==
		   RDS_PROTOCOL(major, minor)) {
		version = RDS_PROTOCOL_COMPAT_VERSION;
	} else {
		if (isv6)
			printk_ratelimited(KERN_NOTICE "RDS: Connection from %pI6c using incompatible protocol version %u.%u\n",
					   &dp->ricp_v6.dp_saddr, major, minor);
		else
			printk_ratelimited(KERN_NOTICE "RDS: Connection from %pI4 using incompatible protocol version %u.%u\n",
					   &dp->ricp_v4.dp_saddr, major, minor);
	}
	return version;
}

#if IS_ENABLED(CONFIG_IPV6)
/* Given an IPv6 address, find the net_device which hosts that address and
 * return its index.  This is used by the rds_ib_cm_handle_connect() code to
 * find the interface index of where an incoming request comes from when
 * the request is using a link local address.
 *
 * Note one problem in this search.  It is possible that two interfaces have
 * the same link local address.  Unfortunately, this cannot be solved unless
 * the underlying layer gives us the interface which an incoming RDMA connect
 * request comes from.
 */
static u32 __rds_find_ifindex(struct net *net, const struct in6_addr *addr)
{
	struct net_device *dev;
	int idx = 0;

	rcu_read_lock();
	for_each_netdev_rcu(net, dev) {
		if (ipv6_chk_addr(net, addr, dev, 1)) {
			idx = dev->ifindex;
			break;
		}
	}
	rcu_read_unlock();

	return idx;
}
#endif

int rds_ib_cm_handle_connect(struct rdma_cm_id *cm_id,
			     struct rdma_cm_event *event, bool isv6)
{
	__be64 lguid = cm_id->route.path_rec->sgid.global.interface_id;
	__be64 fguid = cm_id->route.path_rec->dgid.global.interface_id;
	const struct rds_ib_conn_priv_cmn *dp_cmn;
	struct rds_connection *conn = NULL;
	struct rds_ib_connection *ic = NULL;
	struct rdma_conn_param conn_param;
	const union rds_ib_conn_priv *dp;
	union rds_ib_conn_priv dp_rep;
	struct in6_addr s_mapped_addr;
	struct in6_addr d_mapped_addr;
	const struct in6_addr *saddr6;
	const struct in6_addr *daddr6;
	int destroy = 1;
	u32 ifindex = 0;
	u32 version;
	int err = 1;

	/* Check whether the remote protocol version matches ours. */
	version = rds_ib_protocol_compatible(event, isv6);
	if (!version) {
		err = RDS_RDMA_REJ_INCOMPAT;
		goto out;
	}

	dp = event->param.conn.private_data;
	if (isv6) {
#if IS_ENABLED(CONFIG_IPV6)
		dp_cmn = &dp->ricp_v6.dp_cmn;
		saddr6 = &dp->ricp_v6.dp_saddr;
		daddr6 = &dp->ricp_v6.dp_daddr;
		/* If either address is link local, need to find the
		 * interface index in order to create a proper RDS
		 * connection.
		 */
		if (ipv6_addr_type(daddr6) & IPV6_ADDR_LINKLOCAL) {
			/* Using init_net for now ..  */
			ifindex = __rds_find_ifindex(&init_net, daddr6);
			/* No index found...  Need to bail out. */
			if (ifindex == 0) {
				err = -EOPNOTSUPP;
				goto out;
			}
		} else if (ipv6_addr_type(saddr6) & IPV6_ADDR_LINKLOCAL) {
			/* Use our address to find the correct index. */
			ifindex = __rds_find_ifindex(&init_net, daddr6);
			/* No index found...  Need to bail out. */
			if (ifindex == 0) {
				err = -EOPNOTSUPP;
				goto out;
			}
		}
#else
		err = -EOPNOTSUPP;
		goto out;
#endif
	} else {
		dp_cmn = &dp->ricp_v4.dp_cmn;
		ipv6_addr_set_v4mapped(dp->ricp_v4.dp_saddr, &s_mapped_addr);
		ipv6_addr_set_v4mapped(dp->ricp_v4.dp_daddr, &d_mapped_addr);
		saddr6 = &s_mapped_addr;
		daddr6 = &d_mapped_addr;
	}

	rdsdebug("saddr %pI6c daddr %pI6c RDSv%u.%u lguid 0x%llx fguid 0x%llx, tos:%d\n",
		 saddr6, daddr6, RDS_PROTOCOL_MAJOR(version),
		 RDS_PROTOCOL_MINOR(version),
		 (unsigned long long)be64_to_cpu(lguid),
		 (unsigned long long)be64_to_cpu(fguid), dp_cmn->ricpc_dp_toss);

	/* RDS/IB is not currently netns aware, thus init_net */
	conn = rds_conn_create(&init_net, daddr6, saddr6,
			       &rds_ib_transport, dp_cmn->ricpc_dp_toss,
			       GFP_KERNEL, ifindex);
	if (IS_ERR(conn)) {
		rdsdebug("rds_conn_create failed (%ld)\n", PTR_ERR(conn));
		conn = NULL;
		goto out;
	}

	/*
	 * The connection request may occur while the
	 * previous connection exist, e.g. in case of failover.
	 * But as connections may be initiated simultaneously
	 * by both hosts, we have a random backoff mechanism -
	 * see the comment above rds_queue_reconnect()
	 */
	mutex_lock(&conn->c_cm_lock);
	if (!rds_conn_transition(conn, RDS_CONN_DOWN, RDS_CONN_CONNECTING)) {
		if (rds_conn_state(conn) == RDS_CONN_UP) {
			rdsdebug("incoming connect while connecting\n");
			rds_conn_drop(conn);
			rds_ib_stats_inc(s_ib_listen_closed_stale);
		} else
		if (rds_conn_state(conn) == RDS_CONN_CONNECTING) {
			/* Wait and see - our connect may still be succeeding */
			rds_ib_stats_inc(s_ib_connect_raced);
		}
		goto out;
	}

	ic = conn->c_transport_data;

	rds_ib_set_protocol(conn, version);
	rds_ib_set_flow_control(conn, be32_to_cpu(dp_cmn->ricpc_credit));

	/* If the peer gave us the last packet it saw, process this as if
	 * we had received a regular ACK. */
	if (dp_cmn->ricpc_ack_seq)
		rds_send_drop_acked(conn, be64_to_cpu(dp_cmn->ricpc_ack_seq),
				    NULL);

	BUG_ON(cm_id->context);
	BUG_ON(ic->i_cm_id);

	ic->i_cm_id = cm_id;
	cm_id->context = conn;

	/* We got halfway through setting up the ib_connection, if we
	 * fail now, we have to take the long route out of this mess. */
	destroy = 0;

	err = rds_ib_setup_qp(conn);
	if (err) {
		rds_ib_conn_error(conn, "rds_ib_setup_qp failed (%d)\n", err);
		goto out;
	}

	rds_ib_cm_fill_conn_param(conn, &conn_param, &dp_rep, version,
				  event->param.conn.responder_resources,
				  event->param.conn.initiator_depth, isv6);

	/* rdma_accept() calls rdma_reject() internally if it fails */
	if (rdma_accept(cm_id, &conn_param))
		rds_ib_conn_error(conn, "rdma_accept failed\n");

out:
	if (conn)
		mutex_unlock(&conn->c_cm_lock);
	if (err)
		rdma_reject(cm_id, &err, sizeof(int),
			    IB_CM_REJ_CONSUMER_DEFINED);
	return destroy;
}


int rds_ib_cm_initiate_connect(struct rdma_cm_id *cm_id, bool isv6)
{
	struct rds_connection *conn = cm_id->context;
	struct rds_ib_connection *ic = conn->c_transport_data;
	struct rdma_conn_param conn_param;
	union rds_ib_conn_priv dp;
	int ret;

	/* If the peer doesn't do protocol negotiation, we must
	 * default to RDSv3.0 */
	rds_ib_set_protocol(conn, RDS_PROTOCOL_4_1);
	ic->i_flowctl = rds_ib_sysctl_flow_control;	/* advertise flow control */

	ret = rds_ib_setup_qp(conn);
	if (ret) {
		rds_ib_conn_error(conn, "rds_ib_setup_qp failed (%d)\n", ret);
		goto out;
	}

	rds_ib_cm_fill_conn_param(conn, &conn_param, &dp,
				  conn->c_proposed_version,
				  UINT_MAX, UINT_MAX, isv6);
	ret = rdma_connect(cm_id, &conn_param);
	if (ret)
		rds_ib_conn_error(conn, "rdma_connect failed (%d)\n", ret);

out:
	/* Beware - returning non-zero tells the rdma_cm to destroy
	 * the cm_id. We should certainly not do it as long as we still
	 * "own" the cm_id. */
	if (ret) {
		if (ic->i_cm_id == cm_id)
			ret = 0;
	}
	ic->i_active_side = true;
	return ret;
}

int rds_ib_conn_path_connect(struct rds_conn_path *cp)
{
	struct rds_connection *conn = cp->cp_conn;
	struct sockaddr_storage src, dest;
	rdma_cm_event_handler handler;
	struct rds_ib_connection *ic;
	int ret;

	ic = conn->c_transport_data;

	/* XXX I wonder what affect the port space has */
	/* delegate cm event handler to rdma_transport */
#if IS_ENABLED(CONFIG_IPV6)
	if (conn->c_isv6)
		handler = rds6_rdma_cm_event_handler;
	else
#endif
		handler = rds_rdma_cm_event_handler;
	ic->i_cm_id = rdma_create_id(&init_net, handler, conn,
				     RDMA_PS_TCP, IB_QPT_RC);
	if (IS_ERR(ic->i_cm_id)) {
		ret = PTR_ERR(ic->i_cm_id);
		ic->i_cm_id = NULL;
		rdsdebug("rdma_create_id() failed: %d\n", ret);
		goto out;
	}

	rdsdebug("created cm id %p for conn %p\n", ic->i_cm_id, conn);

	if (ipv6_addr_v4mapped(&conn->c_faddr)) {
		struct sockaddr_in *sin;

		sin = (struct sockaddr_in *)&src;
		sin->sin_family = AF_INET;
		sin->sin_addr.s_addr = conn->c_laddr.s6_addr32[3];
		sin->sin_port = 0;

		sin = (struct sockaddr_in *)&dest;
		sin->sin_family = AF_INET;
		sin->sin_addr.s_addr = conn->c_faddr.s6_addr32[3];
		sin->sin_port = htons(RDS_PORT);
	} else {
		struct sockaddr_in6 *sin6;

		sin6 = (struct sockaddr_in6 *)&src;
		sin6->sin6_family = AF_INET6;
		sin6->sin6_addr = conn->c_laddr;
		sin6->sin6_port = 0;
		sin6->sin6_scope_id = conn->c_dev_if;

		sin6 = (struct sockaddr_in6 *)&dest;
		sin6->sin6_family = AF_INET6;
		sin6->sin6_addr = conn->c_faddr;
		sin6->sin6_port = htons(RDS_CM_PORT);
		sin6->sin6_scope_id = conn->c_dev_if;
	}

	ret = rdma_resolve_addr(ic->i_cm_id, (struct sockaddr *)&src,
				(struct sockaddr *)&dest,
				RDS_RDMA_RESOLVE_TIMEOUT_MS);
	if (ret) {
		rdsdebug("addr resolve failed for cm id %p: %d\n", ic->i_cm_id,
			 ret);
		rdma_destroy_id(ic->i_cm_id);
		ic->i_cm_id = NULL;
	}

out:
	return ret;
}

/*
 * This is so careful about only cleaning up resources that were built up
 * so that it can be called at any point during startup.  In fact it
 * can be called multiple times for a given connection.
 */
void rds_ib_conn_path_shutdown(struct rds_conn_path *cp)
{
	struct rds_connection *conn = cp->cp_conn;
	struct rds_ib_connection *ic = conn->c_transport_data;
	int err = 0;

	rdsdebug("cm %p pd %p cq %p %p qp %p\n", ic->i_cm_id,
		 ic->i_pd, ic->i_send_cq, ic->i_recv_cq,
		 ic->i_cm_id ? ic->i_cm_id->qp : NULL);

	if (ic->i_cm_id) {
		rdsdebug("disconnecting cm %p\n", ic->i_cm_id);
		err = rdma_disconnect(ic->i_cm_id);
		if (err) {
			/* Actually this may happen quite frequently, when
			 * an outgoing connect raced with an incoming connect.
			 */
			rdsdebug("failed to disconnect, cm: %p err %d\n",
				ic->i_cm_id, err);
		}

		/* kick off "flush_worker" for all pools in order to reap
		 * all FRMR registrations that are still marked "FRMR_IS_INUSE"
		 */
		rds_ib_flush_mrs();

		/*
		 * We want to wait for tx and rx completion to finish
		 * before we tear down the connection, but we have to be
		 * careful not to get stuck waiting on a send ring that
		 * only has unsignaled sends in it.  We've shutdown new
		 * sends before getting here so by waiting for signaled
		 * sends to complete we're ensured that there will be no
		 * more tx processing.
		 */
		wait_event(rds_ib_ring_empty_wait,
			   rds_ib_ring_empty(&ic->i_recv_ring) &&
			   (atomic_read(&ic->i_signaled_sends) == 0) &&
			   (atomic_read(&ic->i_fastreg_inuse_count) == 0) &&
			   (atomic_read(&ic->i_fastreg_wrs) == RDS_IB_DEFAULT_FR_WR));
		tasklet_kill(&ic->i_send_tasklet);
		tasklet_kill(&ic->i_recv_tasklet);

		atomic_set(&ic->i_cq_quiesce, 1);

		/* first destroy the ib state that generates callbacks */
		if (ic->i_cm_id->qp)
			rdma_destroy_qp(ic->i_cm_id);
		if (ic->i_send_cq) {
			if (ic->rds_ibdev)
				ibdev_put_vector(ic->rds_ibdev, ic->i_scq_vector);
			ib_destroy_cq(ic->i_send_cq);
		}

		if (ic->i_recv_cq) {
			if (ic->rds_ibdev)
				ibdev_put_vector(ic->rds_ibdev, ic->i_rcq_vector);
			ib_destroy_cq(ic->i_recv_cq);
		}

		if (ic->rds_ibdev) {
			struct dma_pool *pool;

			pool = ic->rds_ibdev->rid_hdrs_pool;

			/* then free the resources that ib callbacks use */
			if (ic->i_send_hdrs) {
				rds_dma_hdrs_free(pool, ic->i_send_hdrs,
						  ic->i_send_hdrs_dma,
						  ic->i_send_ring.w_nr);
				ic->i_send_hdrs = NULL;
				ic->i_send_hdrs_dma = NULL;
			}

			if (ic->i_recv_hdrs) {
				rds_dma_hdrs_free(pool, ic->i_recv_hdrs,
						  ic->i_recv_hdrs_dma,
						  ic->i_recv_ring.w_nr);
				ic->i_recv_hdrs = NULL;
				ic->i_recv_hdrs_dma = NULL;
			}

			if (ic->i_ack) {
				dma_pool_free(pool, ic->i_ack, ic->i_ack_dma);
				ic->i_ack = NULL;
			}
		} else {
			WARN_ON(ic->i_send_hdrs);
			WARN_ON(ic->i_send_hdrs_dma);
			WARN_ON(ic->i_recv_hdrs);
			WARN_ON(ic->i_recv_hdrs_dma);
			WARN_ON(ic->i_ack);
		}

		if (ic->i_sends)
			rds_ib_send_clear_ring(ic);
		if (ic->i_recvs)
			rds_ib_recv_clear_ring(ic);

		rdma_destroy_id(ic->i_cm_id);

		/*
		 * Move connection back to the nodev list.
		 */
		if (ic->rds_ibdev)
			rds_ib_remove_conn(ic->rds_ibdev, conn);

		ic->i_cm_id = NULL;
		ic->i_pd = NULL;
		ic->i_send_cq = NULL;
		ic->i_recv_cq = NULL;
	}
	BUG_ON(ic->rds_ibdev);

	/* Clear pending transmit */
	if (ic->i_data_op) {
		struct rds_message *rm;

		rm = container_of(ic->i_data_op, struct rds_message, data);
		rds_message_put(rm);
		ic->i_data_op = NULL;
	}

	/* Clear the ACK state */
	clear_bit(IB_ACK_IN_FLIGHT, &ic->i_ack_flags);
#ifdef KERNEL_HAS_ATOMIC64
	atomic64_set(&ic->i_ack_next, 0);
#else
	ic->i_ack_next = 0;
#endif
	ic->i_ack_recv = 0;

	/* Clear flow control state */
	ic->i_flowctl = 0;
	atomic_set(&ic->i_credits, 0);

	/* Re-init rings, but retain sizes. */
	rds_ib_ring_init(&ic->i_send_ring, ic->i_send_ring.w_nr);
	rds_ib_ring_init(&ic->i_recv_ring, ic->i_recv_ring.w_nr);

	if (ic->i_ibinc) {
		rds_inc_put(&ic->i_ibinc->ii_inc);
		ic->i_ibinc = NULL;
	}

	vfree(ic->i_sends);
	ic->i_sends = NULL;
	vfree(ic->i_recvs);
	ic->i_recvs = NULL;
	ic->i_active_side = false;
}

int rds_ib_conn_alloc(struct rds_connection *conn, gfp_t gfp)
{
	struct rds_ib_connection *ic;
	unsigned long flags;
	int ret;

	/* XXX too lazy? */
	ic = kzalloc(sizeof(struct rds_ib_connection), gfp);
	if (!ic)
		return -ENOMEM;

	ret = rds_ib_recv_alloc_caches(ic, gfp);
	if (ret) {
		kfree(ic);
		return ret;
	}

	INIT_LIST_HEAD(&ic->ib_node);
	tasklet_init(&ic->i_send_tasklet, rds_ib_tasklet_fn_send,
		     (unsigned long)ic);
	tasklet_init(&ic->i_recv_tasklet, rds_ib_tasklet_fn_recv,
		     (unsigned long)ic);
	mutex_init(&ic->i_recv_mutex);
#ifndef KERNEL_HAS_ATOMIC64
	spin_lock_init(&ic->i_ack_lock);
#endif
	atomic_set(&ic->i_signaled_sends, 0);
	atomic_set(&ic->i_fastreg_wrs, RDS_IB_DEFAULT_FR_WR);

	/*
	 * rds_ib_conn_shutdown() waits for these to be emptied so they
	 * must be initialized before it can be called.
	 */
	rds_ib_ring_init(&ic->i_send_ring, 0);
	rds_ib_ring_init(&ic->i_recv_ring, 0);

	ic->conn = conn;
	conn->c_transport_data = ic;

	spin_lock_irqsave(&ib_nodev_conns_lock, flags);
	list_add_tail(&ic->ib_node, &ib_nodev_conns);
	spin_unlock_irqrestore(&ib_nodev_conns_lock, flags);


	rdsdebug("conn %p conn ic %p\n", conn, conn->c_transport_data);
	return 0;
}

/*
 * Free a connection. Connection must be shut down and not set for reconnect.
 */
void rds_ib_conn_free(void *arg)
{
	struct rds_ib_connection *ic = arg;
	spinlock_t	*lock_ptr;

	rdsdebug("ic %p\n", ic);

	/*
	 * Conn is either on a dev's list or on the nodev list.
	 * A race with shutdown() or connect() would cause problems
	 * (since rds_ibdev would change) but that should never happen.
	 */
	lock_ptr = ic->rds_ibdev ? &ic->rds_ibdev->spinlock : &ib_nodev_conns_lock;

	spin_lock_irq(lock_ptr);
	list_del(&ic->ib_node);
	spin_unlock_irq(lock_ptr);

	rds_ib_recv_free_caches(ic);

	kfree(ic);
}


/*
 * An error occurred on the connection
 */
void
__rds_ib_conn_error(struct rds_connection *conn, const char *fmt, ...)
{
	va_list ap;

	rds_conn_drop(conn);

	va_start(ap, fmt);
	vprintk(fmt, ap);
	va_end(ap);
}
