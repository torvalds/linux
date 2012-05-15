/*
 * Copyright (c) 2006 Oracle.  All rights reserved.
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
#include <linux/kernel.h>
#include <linux/in.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/ratelimit.h>

#include "rds.h"
#include "ib.h"

static char *rds_ib_event_type_strings[] = {
#define RDS_IB_EVENT_STRING(foo) \
		[IB_EVENT_##foo] = __stringify(IB_EVENT_##foo)
	RDS_IB_EVENT_STRING(CQ_ERR),
	RDS_IB_EVENT_STRING(QP_FATAL),
	RDS_IB_EVENT_STRING(QP_REQ_ERR),
	RDS_IB_EVENT_STRING(QP_ACCESS_ERR),
	RDS_IB_EVENT_STRING(COMM_EST),
	RDS_IB_EVENT_STRING(SQ_DRAINED),
	RDS_IB_EVENT_STRING(PATH_MIG),
	RDS_IB_EVENT_STRING(PATH_MIG_ERR),
	RDS_IB_EVENT_STRING(DEVICE_FATAL),
	RDS_IB_EVENT_STRING(PORT_ACTIVE),
	RDS_IB_EVENT_STRING(PORT_ERR),
	RDS_IB_EVENT_STRING(LID_CHANGE),
	RDS_IB_EVENT_STRING(PKEY_CHANGE),
	RDS_IB_EVENT_STRING(SM_CHANGE),
	RDS_IB_EVENT_STRING(SRQ_ERR),
	RDS_IB_EVENT_STRING(SRQ_LIMIT_REACHED),
	RDS_IB_EVENT_STRING(QP_LAST_WQE_REACHED),
	RDS_IB_EVENT_STRING(CLIENT_REREGISTER),
#undef RDS_IB_EVENT_STRING
};

static char *rds_ib_event_str(enum ib_event_type type)
{
	return rds_str_array(rds_ib_event_type_strings,
			     ARRAY_SIZE(rds_ib_event_type_strings), type);
};

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
	const struct rds_ib_connect_private *dp = NULL;
	struct rds_ib_connection *ic = conn->c_transport_data;
	struct ib_qp_attr qp_attr;
	int err;

	if (event->param.conn.private_data_len >= sizeof(*dp)) {
		dp = event->param.conn.private_data;

		/* make sure it isn't empty data */
		if (dp->dp_protocol_major) {
			rds_ib_set_protocol(conn,
				RDS_PROTOCOL(dp->dp_protocol_major,
				dp->dp_protocol_minor));
			rds_ib_set_flow_control(conn, be32_to_cpu(dp->dp_credit));
		}
	}

	if (conn->c_version < RDS_PROTOCOL(3,1)) {
		printk(KERN_NOTICE "RDS/IB: Connection to %pI4 version %u.%u failed,"
		       " no longer supported\n",
		       &conn->c_faddr,
		       RDS_PROTOCOL_MAJOR(conn->c_version),
		       RDS_PROTOCOL_MINOR(conn->c_version));
		rds_conn_destroy(conn);
		return;
	} else {
		printk(KERN_NOTICE "RDS/IB: connected to %pI4 version %u.%u%s\n",
		       &conn->c_faddr,
		       RDS_PROTOCOL_MAJOR(conn->c_version),
		       RDS_PROTOCOL_MINOR(conn->c_version),
		       ic->i_flowctl ? ", flow control" : "");
	}

	/*
	 * Init rings and fill recv. this needs to wait until protocol negotiation
	 * is complete, since ring layout is different from 3.0 to 3.1.
	 */
	rds_ib_send_init_ring(ic);
	rds_ib_recv_init_ring(ic);
	/* Post receive buffers - as a side effect, this will update
	 * the posted credit count. */
	rds_ib_recv_refill(conn, 1);

	/* Tune RNR behavior */
	rds_ib_tune_rnr(ic, &qp_attr);

	qp_attr.qp_state = IB_QPS_RTS;
	err = ib_modify_qp(ic->i_cm_id->qp, &qp_attr, IB_QP_STATE);
	if (err)
		printk(KERN_NOTICE "ib_modify_qp(IB_QP_STATE, RTS): err=%d\n", err);

	/* update ib_device with this local ipaddr */
	err = rds_ib_update_ipaddr(ic->rds_ibdev, conn->c_laddr);
	if (err)
		printk(KERN_ERR "rds_ib_update_ipaddr failed (%d)\n",
			err);

	/* If the peer gave us the last packet it saw, process this as if
	 * we had received a regular ACK. */
	if (dp && dp->dp_ack_seq)
		rds_send_drop_acked(conn, be64_to_cpu(dp->dp_ack_seq), NULL);

	rds_connect_complete(conn);
}

static void rds_ib_cm_fill_conn_param(struct rds_connection *conn,
			struct rdma_conn_param *conn_param,
			struct rds_ib_connect_private *dp,
			u32 protocol_version,
			u32 max_responder_resources,
			u32 max_initiator_depth)
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
		dp->dp_saddr = conn->c_laddr;
		dp->dp_daddr = conn->c_faddr;
		dp->dp_protocol_major = RDS_PROTOCOL_MAJOR(protocol_version);
		dp->dp_protocol_minor = RDS_PROTOCOL_MINOR(protocol_version);
		dp->dp_protocol_minor_mask = cpu_to_be16(RDS_IB_SUPPORTED_PROTOCOLS);
		dp->dp_ack_seq = rds_ib_piggyb_ack(ic);

		/* Advertise flow control */
		if (ic->i_flowctl) {
			unsigned int credits;

			credits = IB_GET_POST_CREDITS(atomic_read(&ic->i_credits));
			dp->dp_credit = cpu_to_be32(credits);
			atomic_sub(IB_SET_POST_CREDITS(credits), &ic->i_credits);
		}

		conn_param->private_data = dp;
		conn_param->private_data_len = sizeof(*dp);
	}
}

static void rds_ib_cq_event_handler(struct ib_event *event, void *data)
{
	rdsdebug("event %u (%s) data %p\n",
		 event->event, rds_ib_event_str(event->event), data);
}

static void rds_ib_qp_event_handler(struct ib_event *event, void *data)
{
	struct rds_connection *conn = data;
	struct rds_ib_connection *ic = conn->c_transport_data;

	rdsdebug("conn %p ic %p event %u (%s)\n", conn, ic, event->event,
		 rds_ib_event_str(event->event));

	switch (event->event) {
	case IB_EVENT_COMM_EST:
		rdma_notify(ic->i_cm_id, IB_EVENT_COMM_EST);
		break;
	default:
		rdsdebug("Fatal QP Event %u (%s) "
			"- connection %pI4->%pI4, reconnecting\n",
			event->event, rds_ib_event_str(event->event),
			&conn->c_laddr, &conn->c_faddr);
		rds_conn_drop(conn);
		break;
	}
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
	struct rds_ib_device *rds_ibdev;
	int ret;

	/*
	 * It's normal to see a null device if an incoming connection races
	 * with device removal, so we don't print a warning.
	 */
	rds_ibdev = rds_ib_get_client_data(dev);
	if (!rds_ibdev)
		return -EOPNOTSUPP;

	/* add the conn now so that connection establishment has the dev */
	rds_ib_add_conn(rds_ibdev, conn);

	if (rds_ibdev->max_wrs < ic->i_send_ring.w_nr + 1)
		rds_ib_ring_resize(&ic->i_send_ring, rds_ibdev->max_wrs - 1);
	if (rds_ibdev->max_wrs < ic->i_recv_ring.w_nr + 1)
		rds_ib_ring_resize(&ic->i_recv_ring, rds_ibdev->max_wrs - 1);

	/* Protection domain and memory range */
	ic->i_pd = rds_ibdev->pd;
	ic->i_mr = rds_ibdev->mr;

	ic->i_send_cq = ib_create_cq(dev, rds_ib_send_cq_comp_handler,
				     rds_ib_cq_event_handler, conn,
				     ic->i_send_ring.w_nr + 1, 0);
	if (IS_ERR(ic->i_send_cq)) {
		ret = PTR_ERR(ic->i_send_cq);
		ic->i_send_cq = NULL;
		rdsdebug("ib_create_cq send failed: %d\n", ret);
		goto out;
	}

	ic->i_recv_cq = ib_create_cq(dev, rds_ib_recv_cq_comp_handler,
				     rds_ib_cq_event_handler, conn,
				     ic->i_recv_ring.w_nr, 0);
	if (IS_ERR(ic->i_recv_cq)) {
		ret = PTR_ERR(ic->i_recv_cq);
		ic->i_recv_cq = NULL;
		rdsdebug("ib_create_cq recv failed: %d\n", ret);
		goto out;
	}

	ret = ib_req_notify_cq(ic->i_send_cq, IB_CQ_NEXT_COMP);
	if (ret) {
		rdsdebug("ib_req_notify_cq send failed: %d\n", ret);
		goto out;
	}

	ret = ib_req_notify_cq(ic->i_recv_cq, IB_CQ_SOLICITED);
	if (ret) {
		rdsdebug("ib_req_notify_cq recv failed: %d\n", ret);
		goto out;
	}

	/* XXX negotiate max send/recv with remote? */
	memset(&attr, 0, sizeof(attr));
	attr.event_handler = rds_ib_qp_event_handler;
	attr.qp_context = conn;
	/* + 1 to allow for the single ack message */
	attr.cap.max_send_wr = ic->i_send_ring.w_nr + 1;
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
		goto out;
	}

	ic->i_send_hdrs = ib_dma_alloc_coherent(dev,
					   ic->i_send_ring.w_nr *
						sizeof(struct rds_header),
					   &ic->i_send_hdrs_dma, GFP_KERNEL);
	if (!ic->i_send_hdrs) {
		ret = -ENOMEM;
		rdsdebug("ib_dma_alloc_coherent send failed\n");
		goto out;
	}

	ic->i_recv_hdrs = ib_dma_alloc_coherent(dev,
					   ic->i_recv_ring.w_nr *
						sizeof(struct rds_header),
					   &ic->i_recv_hdrs_dma, GFP_KERNEL);
	if (!ic->i_recv_hdrs) {
		ret = -ENOMEM;
		rdsdebug("ib_dma_alloc_coherent recv failed\n");
		goto out;
	}

	ic->i_ack = ib_dma_alloc_coherent(dev, sizeof(struct rds_header),
				       &ic->i_ack_dma, GFP_KERNEL);
	if (!ic->i_ack) {
		ret = -ENOMEM;
		rdsdebug("ib_dma_alloc_coherent ack failed\n");
		goto out;
	}

	ic->i_sends = vzalloc_node(ic->i_send_ring.w_nr * sizeof(struct rds_ib_send_work),
				   ibdev_to_node(dev));
	if (!ic->i_sends) {
		ret = -ENOMEM;
		rdsdebug("send allocation failed\n");
		goto out;
	}

	ic->i_recvs = vzalloc_node(ic->i_recv_ring.w_nr * sizeof(struct rds_ib_recv_work),
				   ibdev_to_node(dev));
	if (!ic->i_recvs) {
		ret = -ENOMEM;
		rdsdebug("recv allocation failed\n");
		goto out;
	}

	rds_ib_recv_init_ack(ic);

	rdsdebug("conn %p pd %p mr %p cq %p %p\n", conn, ic->i_pd, ic->i_mr,
		 ic->i_send_cq, ic->i_recv_cq);

out:
	rds_ib_dev_put(rds_ibdev);
	return ret;
}

static u32 rds_ib_protocol_compatible(struct rdma_cm_event *event)
{
	const struct rds_ib_connect_private *dp = event->param.conn.private_data;
	u16 common;
	u32 version = 0;

	/*
	 * rdma_cm private data is odd - when there is any private data in the
	 * request, we will be given a pretty large buffer without telling us the
	 * original size. The only way to tell the difference is by looking at
	 * the contents, which are initialized to zero.
	 * If the protocol version fields aren't set, this is a connection attempt
	 * from an older version. This could could be 3.0 or 2.0 - we can't tell.
	 * We really should have changed this for OFED 1.3 :-(
	 */

	/* Be paranoid. RDS always has privdata */
	if (!event->param.conn.private_data_len) {
		printk(KERN_NOTICE "RDS incoming connection has no private data, "
			"rejecting\n");
		return 0;
	}

	/* Even if len is crap *now* I still want to check it. -ASG */
	if (event->param.conn.private_data_len < sizeof (*dp) ||
	    dp->dp_protocol_major == 0)
		return RDS_PROTOCOL_3_0;

	common = be16_to_cpu(dp->dp_protocol_minor_mask) & RDS_IB_SUPPORTED_PROTOCOLS;
	if (dp->dp_protocol_major == 3 && common) {
		version = RDS_PROTOCOL_3_0;
		while ((common >>= 1) != 0)
			version++;
	}
	printk_ratelimited(KERN_NOTICE "RDS: Connection from %pI4 using "
			"incompatible protocol version %u.%u\n",
			&dp->dp_saddr,
			dp->dp_protocol_major,
			dp->dp_protocol_minor);
	return version;
}

int rds_ib_cm_handle_connect(struct rdma_cm_id *cm_id,
				    struct rdma_cm_event *event)
{
	__be64 lguid = cm_id->route.path_rec->sgid.global.interface_id;
	__be64 fguid = cm_id->route.path_rec->dgid.global.interface_id;
	const struct rds_ib_connect_private *dp = event->param.conn.private_data;
	struct rds_ib_connect_private dp_rep;
	struct rds_connection *conn = NULL;
	struct rds_ib_connection *ic = NULL;
	struct rdma_conn_param conn_param;
	u32 version;
	int err = 1, destroy = 1;

	/* Check whether the remote protocol version matches ours. */
	version = rds_ib_protocol_compatible(event);
	if (!version)
		goto out;

	rdsdebug("saddr %pI4 daddr %pI4 RDSv%u.%u lguid 0x%llx fguid "
		 "0x%llx\n", &dp->dp_saddr, &dp->dp_daddr,
		 RDS_PROTOCOL_MAJOR(version), RDS_PROTOCOL_MINOR(version),
		 (unsigned long long)be64_to_cpu(lguid),
		 (unsigned long long)be64_to_cpu(fguid));

	conn = rds_conn_create(dp->dp_daddr, dp->dp_saddr, &rds_ib_transport,
			       GFP_KERNEL);
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
	rds_ib_set_flow_control(conn, be32_to_cpu(dp->dp_credit));

	/* If the peer gave us the last packet it saw, process this as if
	 * we had received a regular ACK. */
	if (dp->dp_ack_seq)
		rds_send_drop_acked(conn, be64_to_cpu(dp->dp_ack_seq), NULL);

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
		event->param.conn.initiator_depth);

	/* rdma_accept() calls rdma_reject() internally if it fails */
	err = rdma_accept(cm_id, &conn_param);
	if (err)
		rds_ib_conn_error(conn, "rdma_accept failed (%d)\n", err);

out:
	if (conn)
		mutex_unlock(&conn->c_cm_lock);
	if (err)
		rdma_reject(cm_id, NULL, 0);
	return destroy;
}


int rds_ib_cm_initiate_connect(struct rdma_cm_id *cm_id)
{
	struct rds_connection *conn = cm_id->context;
	struct rds_ib_connection *ic = conn->c_transport_data;
	struct rdma_conn_param conn_param;
	struct rds_ib_connect_private dp;
	int ret;

	/* If the peer doesn't do protocol negotiation, we must
	 * default to RDSv3.0 */
	rds_ib_set_protocol(conn, RDS_PROTOCOL_3_0);
	ic->i_flowctl = rds_ib_sysctl_flow_control;	/* advertise flow control */

	ret = rds_ib_setup_qp(conn);
	if (ret) {
		rds_ib_conn_error(conn, "rds_ib_setup_qp failed (%d)\n", ret);
		goto out;
	}

	rds_ib_cm_fill_conn_param(conn, &conn_param, &dp, RDS_PROTOCOL_VERSION,
		UINT_MAX, UINT_MAX);
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
	return ret;
}

int rds_ib_conn_connect(struct rds_connection *conn)
{
	struct rds_ib_connection *ic = conn->c_transport_data;
	struct sockaddr_in src, dest;
	int ret;

	/* XXX I wonder what affect the port space has */
	/* delegate cm event handler to rdma_transport */
	ic->i_cm_id = rdma_create_id(rds_rdma_cm_event_handler, conn,
				     RDMA_PS_TCP, IB_QPT_RC);
	if (IS_ERR(ic->i_cm_id)) {
		ret = PTR_ERR(ic->i_cm_id);
		ic->i_cm_id = NULL;
		rdsdebug("rdma_create_id() failed: %d\n", ret);
		goto out;
	}

	rdsdebug("created cm id %p for conn %p\n", ic->i_cm_id, conn);

	src.sin_family = AF_INET;
	src.sin_addr.s_addr = (__force u32)conn->c_laddr;
	src.sin_port = (__force u16)htons(0);

	dest.sin_family = AF_INET;
	dest.sin_addr.s_addr = (__force u32)conn->c_faddr;
	dest.sin_port = (__force u16)htons(RDS_PORT);

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
void rds_ib_conn_shutdown(struct rds_connection *conn)
{
	struct rds_ib_connection *ic = conn->c_transport_data;
	int err = 0;

	rdsdebug("cm %p pd %p cq %p %p qp %p\n", ic->i_cm_id,
		 ic->i_pd, ic->i_send_cq, ic->i_recv_cq,
		 ic->i_cm_id ? ic->i_cm_id->qp : NULL);

	if (ic->i_cm_id) {
		struct ib_device *dev = ic->i_cm_id->device;

		rdsdebug("disconnecting cm %p\n", ic->i_cm_id);
		err = rdma_disconnect(ic->i_cm_id);
		if (err) {
			/* Actually this may happen quite frequently, when
			 * an outgoing connect raced with an incoming connect.
			 */
			rdsdebug("failed to disconnect, cm: %p err %d\n",
				ic->i_cm_id, err);
		}

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
			   (atomic_read(&ic->i_signaled_sends) == 0));
		tasklet_kill(&ic->i_recv_tasklet);

		if (ic->i_send_hdrs)
			ib_dma_free_coherent(dev,
					   ic->i_send_ring.w_nr *
						sizeof(struct rds_header),
					   ic->i_send_hdrs,
					   ic->i_send_hdrs_dma);

		if (ic->i_recv_hdrs)
			ib_dma_free_coherent(dev,
					   ic->i_recv_ring.w_nr *
						sizeof(struct rds_header),
					   ic->i_recv_hdrs,
					   ic->i_recv_hdrs_dma);

		if (ic->i_ack)
			ib_dma_free_coherent(dev, sizeof(struct rds_header),
					     ic->i_ack, ic->i_ack_dma);

		if (ic->i_sends)
			rds_ib_send_clear_ring(ic);
		if (ic->i_recvs)
			rds_ib_recv_clear_ring(ic);

		if (ic->i_cm_id->qp)
			rdma_destroy_qp(ic->i_cm_id);
		if (ic->i_send_cq)
			ib_destroy_cq(ic->i_send_cq);
		if (ic->i_recv_cq)
			ib_destroy_cq(ic->i_recv_cq);
		rdma_destroy_id(ic->i_cm_id);

		/*
		 * Move connection back to the nodev list.
		 */
		if (ic->rds_ibdev)
			rds_ib_remove_conn(ic->rds_ibdev, conn);

		ic->i_cm_id = NULL;
		ic->i_pd = NULL;
		ic->i_mr = NULL;
		ic->i_send_cq = NULL;
		ic->i_recv_cq = NULL;
		ic->i_send_hdrs = NULL;
		ic->i_recv_hdrs = NULL;
		ic->i_ack = NULL;
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

	rds_ib_ring_init(&ic->i_send_ring, rds_ib_sysctl_max_send_wr);
	rds_ib_ring_init(&ic->i_recv_ring, rds_ib_sysctl_max_recv_wr);

	if (ic->i_ibinc) {
		rds_inc_put(&ic->i_ibinc->ii_inc);
		ic->i_ibinc = NULL;
	}

	vfree(ic->i_sends);
	ic->i_sends = NULL;
	vfree(ic->i_recvs);
	ic->i_recvs = NULL;
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

	ret = rds_ib_recv_alloc_caches(ic);
	if (ret) {
		kfree(ic);
		return ret;
	}

	INIT_LIST_HEAD(&ic->ib_node);
	tasklet_init(&ic->i_recv_tasklet, rds_ib_recv_tasklet_fn,
		     (unsigned long) ic);
	mutex_init(&ic->i_recv_mutex);
#ifndef KERNEL_HAS_ATOMIC64
	spin_lock_init(&ic->i_ack_lock);
#endif
	atomic_set(&ic->i_signaled_sends, 0);

	/*
	 * rds_ib_conn_shutdown() waits for these to be emptied so they
	 * must be initialized before it can be called.
	 */
	rds_ib_ring_init(&ic->i_send_ring, rds_ib_sysctl_max_send_wr);
	rds_ib_ring_init(&ic->i_recv_ring, rds_ib_sysctl_max_recv_wr);

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
