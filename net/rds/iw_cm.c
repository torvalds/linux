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

#include "rds.h"
#include "iw.h"

/*
 * Set the selected protocol version
 */
static void rds_iw_set_protocol(struct rds_connection *conn, unsigned int version)
{
	conn->c_version = version;
}

/*
 * Set up flow control
 */
static void rds_iw_set_flow_control(struct rds_connection *conn, u32 credits)
{
	struct rds_iw_connection *ic = conn->c_transport_data;

	if (rds_iw_sysctl_flow_control && credits != 0) {
		/* We're doing flow control */
		ic->i_flowctl = 1;
		rds_iw_send_add_credits(conn, credits);
	} else {
		ic->i_flowctl = 0;
	}
}

/*
 * Connection established.
 * We get here for both outgoing and incoming connection.
 */
void rds_iw_cm_connect_complete(struct rds_connection *conn, struct rdma_cm_event *event)
{
	const struct rds_iw_connect_private *dp = NULL;
	struct rds_iw_connection *ic = conn->c_transport_data;
	struct rds_iw_device *rds_iwdev;
	int err;

	if (event->param.conn.private_data_len) {
		dp = event->param.conn.private_data;

		rds_iw_set_protocol(conn,
				RDS_PROTOCOL(dp->dp_protocol_major,
					dp->dp_protocol_minor));
		rds_iw_set_flow_control(conn, be32_to_cpu(dp->dp_credit));
	}

	/* update ib_device with this local ipaddr & conn */
	rds_iwdev = ib_get_client_data(ic->i_cm_id->device, &rds_iw_client);
	err = rds_iw_update_cm_id(rds_iwdev, ic->i_cm_id);
	if (err)
		printk(KERN_ERR "rds_iw_update_ipaddr failed (%d)\n", err);
	rds_iw_add_conn(rds_iwdev, conn);

	/* If the peer gave us the last packet it saw, process this as if
	 * we had received a regular ACK. */
	if (dp && dp->dp_ack_seq)
		rds_send_drop_acked(conn, be64_to_cpu(dp->dp_ack_seq), NULL);

	printk(KERN_NOTICE "RDS/IW: connected to %pI4<->%pI4 version %u.%u%s\n",
			&conn->c_laddr, &conn->c_faddr,
			RDS_PROTOCOL_MAJOR(conn->c_version),
			RDS_PROTOCOL_MINOR(conn->c_version),
			ic->i_flowctl ? ", flow control" : "");

	rds_connect_complete(conn);
}

static void rds_iw_cm_fill_conn_param(struct rds_connection *conn,
			struct rdma_conn_param *conn_param,
			struct rds_iw_connect_private *dp,
			u32 protocol_version)
{
	struct rds_iw_connection *ic = conn->c_transport_data;

	memset(conn_param, 0, sizeof(struct rdma_conn_param));
	/* XXX tune these? */
	conn_param->responder_resources = 1;
	conn_param->initiator_depth = 1;

	if (dp) {
		memset(dp, 0, sizeof(*dp));
		dp->dp_saddr = conn->c_laddr;
		dp->dp_daddr = conn->c_faddr;
		dp->dp_protocol_major = RDS_PROTOCOL_MAJOR(protocol_version);
		dp->dp_protocol_minor = RDS_PROTOCOL_MINOR(protocol_version);
		dp->dp_protocol_minor_mask = cpu_to_be16(RDS_IW_SUPPORTED_PROTOCOLS);
		dp->dp_ack_seq = rds_iw_piggyb_ack(ic);

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

static void rds_iw_cq_event_handler(struct ib_event *event, void *data)
{
	rdsdebug("event %u data %p\n", event->event, data);
}

static void rds_iw_qp_event_handler(struct ib_event *event, void *data)
{
	struct rds_connection *conn = data;
	struct rds_iw_connection *ic = conn->c_transport_data;

	rdsdebug("conn %p ic %p event %u\n", conn, ic, event->event);

	switch (event->event) {
	case IB_EVENT_COMM_EST:
		rdma_notify(ic->i_cm_id, IB_EVENT_COMM_EST);
		break;
	case IB_EVENT_QP_REQ_ERR:
	case IB_EVENT_QP_FATAL:
	default:
		rdsdebug("Fatal QP Event %u "
			"- connection %pI4->%pI4, reconnecting\n",
			event->event, &conn->c_laddr,
			&conn->c_faddr);
		rds_conn_drop(conn);
		break;
	}
}

/*
 * Create a QP
 */
static int rds_iw_init_qp_attrs(struct ib_qp_init_attr *attr,
		struct rds_iw_device *rds_iwdev,
		struct rds_iw_work_ring *send_ring,
		void (*send_cq_handler)(struct ib_cq *, void *),
		struct rds_iw_work_ring *recv_ring,
		void (*recv_cq_handler)(struct ib_cq *, void *),
		void *context)
{
	struct ib_device *dev = rds_iwdev->dev;
	unsigned int send_size, recv_size;
	int ret;

	/* The offset of 1 is to accomodate the additional ACK WR. */
	send_size = min_t(unsigned int, rds_iwdev->max_wrs, rds_iw_sysctl_max_send_wr + 1);
	recv_size = min_t(unsigned int, rds_iwdev->max_wrs, rds_iw_sysctl_max_recv_wr + 1);
	rds_iw_ring_resize(send_ring, send_size - 1);
	rds_iw_ring_resize(recv_ring, recv_size - 1);

	memset(attr, 0, sizeof(*attr));
	attr->event_handler = rds_iw_qp_event_handler;
	attr->qp_context = context;
	attr->cap.max_send_wr = send_size;
	attr->cap.max_recv_wr = recv_size;
	attr->cap.max_send_sge = rds_iwdev->max_sge;
	attr->cap.max_recv_sge = RDS_IW_RECV_SGE;
	attr->sq_sig_type = IB_SIGNAL_REQ_WR;
	attr->qp_type = IB_QPT_RC;

	attr->send_cq = ib_create_cq(dev, send_cq_handler,
				     rds_iw_cq_event_handler,
				     context, send_size, 0);
	if (IS_ERR(attr->send_cq)) {
		ret = PTR_ERR(attr->send_cq);
		attr->send_cq = NULL;
		rdsdebug("ib_create_cq send failed: %d\n", ret);
		goto out;
	}

	attr->recv_cq = ib_create_cq(dev, recv_cq_handler,
				     rds_iw_cq_event_handler,
				     context, recv_size, 0);
	if (IS_ERR(attr->recv_cq)) {
		ret = PTR_ERR(attr->recv_cq);
		attr->recv_cq = NULL;
		rdsdebug("ib_create_cq send failed: %d\n", ret);
		goto out;
	}

	ret = ib_req_notify_cq(attr->send_cq, IB_CQ_NEXT_COMP);
	if (ret) {
		rdsdebug("ib_req_notify_cq send failed: %d\n", ret);
		goto out;
	}

	ret = ib_req_notify_cq(attr->recv_cq, IB_CQ_SOLICITED);
	if (ret) {
		rdsdebug("ib_req_notify_cq recv failed: %d\n", ret);
		goto out;
	}

out:
	if (ret) {
		if (attr->send_cq)
			ib_destroy_cq(attr->send_cq);
		if (attr->recv_cq)
			ib_destroy_cq(attr->recv_cq);
	}
	return ret;
}

/*
 * This needs to be very careful to not leave IS_ERR pointers around for
 * cleanup to trip over.
 */
static int rds_iw_setup_qp(struct rds_connection *conn)
{
	struct rds_iw_connection *ic = conn->c_transport_data;
	struct ib_device *dev = ic->i_cm_id->device;
	struct ib_qp_init_attr attr;
	struct rds_iw_device *rds_iwdev;
	int ret;

	/* rds_iw_add_one creates a rds_iw_device object per IB device,
	 * and allocates a protection domain, memory range and MR pool
	 * for each.  If that fails for any reason, it will not register
	 * the rds_iwdev at all.
	 */
	rds_iwdev = ib_get_client_data(dev, &rds_iw_client);
	if (!rds_iwdev) {
		if (printk_ratelimit())
			printk(KERN_NOTICE "RDS/IW: No client_data for device %s\n",
					dev->name);
		return -EOPNOTSUPP;
	}

	/* Protection domain and memory range */
	ic->i_pd = rds_iwdev->pd;
	ic->i_mr = rds_iwdev->mr;

	ret = rds_iw_init_qp_attrs(&attr, rds_iwdev,
			&ic->i_send_ring, rds_iw_send_cq_comp_handler,
			&ic->i_recv_ring, rds_iw_recv_cq_comp_handler,
			conn);
	if (ret < 0)
		goto out;

	ic->i_send_cq = attr.send_cq;
	ic->i_recv_cq = attr.recv_cq;

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

	ic->i_sends = vmalloc(ic->i_send_ring.w_nr * sizeof(struct rds_iw_send_work));
	if (!ic->i_sends) {
		ret = -ENOMEM;
		rdsdebug("send allocation failed\n");
		goto out;
	}
	rds_iw_send_init_ring(ic);

	ic->i_recvs = vmalloc(ic->i_recv_ring.w_nr * sizeof(struct rds_iw_recv_work));
	if (!ic->i_recvs) {
		ret = -ENOMEM;
		rdsdebug("recv allocation failed\n");
		goto out;
	}

	rds_iw_recv_init_ring(ic);
	rds_iw_recv_init_ack(ic);

	/* Post receive buffers - as a side effect, this will update
	 * the posted credit count. */
	rds_iw_recv_refill(conn, GFP_KERNEL, GFP_HIGHUSER, 1);

	rdsdebug("conn %p pd %p mr %p cq %p %p\n", conn, ic->i_pd, ic->i_mr,
		 ic->i_send_cq, ic->i_recv_cq);

out:
	return ret;
}

static u32 rds_iw_protocol_compatible(const struct rds_iw_connect_private *dp)
{
	u16 common;
	u32 version = 0;

	/* rdma_cm private data is odd - when there is any private data in the
	 * request, we will be given a pretty large buffer without telling us the
	 * original size. The only way to tell the difference is by looking at
	 * the contents, which are initialized to zero.
	 * If the protocol version fields aren't set, this is a connection attempt
	 * from an older version. This could could be 3.0 or 2.0 - we can't tell.
	 * We really should have changed this for OFED 1.3 :-( */
	if (dp->dp_protocol_major == 0)
		return RDS_PROTOCOL_3_0;

	common = be16_to_cpu(dp->dp_protocol_minor_mask) & RDS_IW_SUPPORTED_PROTOCOLS;
	if (dp->dp_protocol_major == 3 && common) {
		version = RDS_PROTOCOL_3_0;
		while ((common >>= 1) != 0)
			version++;
	} else if (printk_ratelimit()) {
		printk(KERN_NOTICE "RDS: Connection from %pI4 using "
			"incompatible protocol version %u.%u\n",
			&dp->dp_saddr,
			dp->dp_protocol_major,
			dp->dp_protocol_minor);
	}
	return version;
}

int rds_iw_cm_handle_connect(struct rdma_cm_id *cm_id,
				    struct rdma_cm_event *event)
{
	const struct rds_iw_connect_private *dp = event->param.conn.private_data;
	struct rds_iw_connect_private dp_rep;
	struct rds_connection *conn = NULL;
	struct rds_iw_connection *ic = NULL;
	struct rdma_conn_param conn_param;
	struct rds_iw_device *rds_iwdev;
	u32 version;
	int err, destroy = 1;

	/* Check whether the remote protocol version matches ours. */
	version = rds_iw_protocol_compatible(dp);
	if (!version)
		goto out;

	rdsdebug("saddr %pI4 daddr %pI4 RDSv%u.%u\n",
		 &dp->dp_saddr, &dp->dp_daddr,
		 RDS_PROTOCOL_MAJOR(version), RDS_PROTOCOL_MINOR(version));

	conn = rds_conn_create(dp->dp_daddr, dp->dp_saddr, &rds_iw_transport,
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
			rds_iw_stats_inc(s_iw_listen_closed_stale);
		} else
		if (rds_conn_state(conn) == RDS_CONN_CONNECTING) {
			/* Wait and see - our connect may still be succeeding */
			rds_iw_stats_inc(s_iw_connect_raced);
		}
		mutex_unlock(&conn->c_cm_lock);
		goto out;
	}

	ic = conn->c_transport_data;

	rds_iw_set_protocol(conn, version);
	rds_iw_set_flow_control(conn, be32_to_cpu(dp->dp_credit));

	/* If the peer gave us the last packet it saw, process this as if
	 * we had received a regular ACK. */
	if (dp->dp_ack_seq)
		rds_send_drop_acked(conn, be64_to_cpu(dp->dp_ack_seq), NULL);

	BUG_ON(cm_id->context);
	BUG_ON(ic->i_cm_id);

	ic->i_cm_id = cm_id;
	cm_id->context = conn;

	rds_iwdev = ib_get_client_data(cm_id->device, &rds_iw_client);
	ic->i_dma_local_lkey = rds_iwdev->dma_local_lkey;

	/* We got halfway through setting up the ib_connection, if we
	 * fail now, we have to take the long route out of this mess. */
	destroy = 0;

	err = rds_iw_setup_qp(conn);
	if (err) {
		rds_iw_conn_error(conn, "rds_iw_setup_qp failed (%d)\n", err);
		mutex_unlock(&conn->c_cm_lock);
		goto out;
	}

	rds_iw_cm_fill_conn_param(conn, &conn_param, &dp_rep, version);

	/* rdma_accept() calls rdma_reject() internally if it fails */
	err = rdma_accept(cm_id, &conn_param);
	mutex_unlock(&conn->c_cm_lock);
	if (err) {
		rds_iw_conn_error(conn, "rdma_accept failed (%d)\n", err);
		goto out;
	}

	return 0;

out:
	rdma_reject(cm_id, NULL, 0);
	return destroy;
}


int rds_iw_cm_initiate_connect(struct rdma_cm_id *cm_id)
{
	struct rds_connection *conn = cm_id->context;
	struct rds_iw_connection *ic = conn->c_transport_data;
	struct rdma_conn_param conn_param;
	struct rds_iw_connect_private dp;
	int ret;

	/* If the peer doesn't do protocol negotiation, we must
	 * default to RDSv3.0 */
	rds_iw_set_protocol(conn, RDS_PROTOCOL_3_0);
	ic->i_flowctl = rds_iw_sysctl_flow_control;	/* advertise flow control */

	ret = rds_iw_setup_qp(conn);
	if (ret) {
		rds_iw_conn_error(conn, "rds_iw_setup_qp failed (%d)\n", ret);
		goto out;
	}

	rds_iw_cm_fill_conn_param(conn, &conn_param, &dp, RDS_PROTOCOL_VERSION);

	ret = rdma_connect(cm_id, &conn_param);
	if (ret)
		rds_iw_conn_error(conn, "rdma_connect failed (%d)\n", ret);

out:
	/* Beware - returning non-zero tells the rdma_cm to destroy
	 * the cm_id. We should certainly not do it as long as we still
	 * "own" the cm_id. */
	if (ret) {
		struct rds_iw_connection *ic = conn->c_transport_data;

		if (ic->i_cm_id == cm_id)
			ret = 0;
	}
	return ret;
}

int rds_iw_conn_connect(struct rds_connection *conn)
{
	struct rds_iw_connection *ic = conn->c_transport_data;
	struct rds_iw_device *rds_iwdev;
	struct sockaddr_in src, dest;
	int ret;

	/* XXX I wonder what affect the port space has */
	/* delegate cm event handler to rdma_transport */
	ic->i_cm_id = rdma_create_id(rds_rdma_cm_event_handler, conn,
				     RDMA_PS_TCP);
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

	/* First, bind to the local address and device. */
	ret = rdma_bind_addr(ic->i_cm_id, (struct sockaddr *) &src);
	if (ret) {
		rdsdebug("rdma_bind_addr(%pI4) failed: %d\n",
				&conn->c_laddr, ret);
		rdma_destroy_id(ic->i_cm_id);
		ic->i_cm_id = NULL;
		goto out;
	}

	rds_iwdev = ib_get_client_data(ic->i_cm_id->device, &rds_iw_client);
	ic->i_dma_local_lkey = rds_iwdev->dma_local_lkey;

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
void rds_iw_conn_shutdown(struct rds_connection *conn)
{
	struct rds_iw_connection *ic = conn->c_transport_data;
	int err = 0;
	struct ib_qp_attr qp_attr;

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
			rdsdebug("rds_iw_conn_shutdown: failed to disconnect,"
				   " cm: %p err %d\n", ic->i_cm_id, err);
		}

		if (ic->i_cm_id->qp) {
			qp_attr.qp_state = IB_QPS_ERR;
			ib_modify_qp(ic->i_cm_id->qp, &qp_attr, IB_QP_STATE);
		}

		wait_event(rds_iw_ring_empty_wait,
			rds_iw_ring_empty(&ic->i_send_ring) &&
			rds_iw_ring_empty(&ic->i_recv_ring));

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
			rds_iw_send_clear_ring(ic);
		if (ic->i_recvs)
			rds_iw_recv_clear_ring(ic);

		if (ic->i_cm_id->qp)
			rdma_destroy_qp(ic->i_cm_id);
		if (ic->i_send_cq)
			ib_destroy_cq(ic->i_send_cq);
		if (ic->i_recv_cq)
			ib_destroy_cq(ic->i_recv_cq);

		/*
		 * If associated with an rds_iw_device:
		 * 	Move connection back to the nodev list.
		 * 	Remove cm_id from the device cm_id list.
		 */
		if (ic->rds_iwdev)
			rds_iw_remove_conn(ic->rds_iwdev, conn);

		rdma_destroy_id(ic->i_cm_id);

		ic->i_cm_id = NULL;
		ic->i_pd = NULL;
		ic->i_mr = NULL;
		ic->i_send_cq = NULL;
		ic->i_recv_cq = NULL;
		ic->i_send_hdrs = NULL;
		ic->i_recv_hdrs = NULL;
		ic->i_ack = NULL;
	}
	BUG_ON(ic->rds_iwdev);

	/* Clear pending transmit */
	if (ic->i_rm) {
		rds_message_put(ic->i_rm);
		ic->i_rm = NULL;
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

	rds_iw_ring_init(&ic->i_send_ring, rds_iw_sysctl_max_send_wr);
	rds_iw_ring_init(&ic->i_recv_ring, rds_iw_sysctl_max_recv_wr);

	if (ic->i_iwinc) {
		rds_inc_put(&ic->i_iwinc->ii_inc);
		ic->i_iwinc = NULL;
	}

	vfree(ic->i_sends);
	ic->i_sends = NULL;
	vfree(ic->i_recvs);
	ic->i_recvs = NULL;
	rdsdebug("shutdown complete\n");
}

int rds_iw_conn_alloc(struct rds_connection *conn, gfp_t gfp)
{
	struct rds_iw_connection *ic;
	unsigned long flags;

	/* XXX too lazy? */
	ic = kzalloc(sizeof(struct rds_iw_connection), GFP_KERNEL);
	if (!ic)
		return -ENOMEM;

	INIT_LIST_HEAD(&ic->iw_node);
	tasklet_init(&ic->i_recv_tasklet, rds_iw_recv_tasklet_fn,
		     (unsigned long) ic);
	mutex_init(&ic->i_recv_mutex);
#ifndef KERNEL_HAS_ATOMIC64
	spin_lock_init(&ic->i_ack_lock);
#endif

	/*
	 * rds_iw_conn_shutdown() waits for these to be emptied so they
	 * must be initialized before it can be called.
	 */
	rds_iw_ring_init(&ic->i_send_ring, rds_iw_sysctl_max_send_wr);
	rds_iw_ring_init(&ic->i_recv_ring, rds_iw_sysctl_max_recv_wr);

	ic->conn = conn;
	conn->c_transport_data = ic;

	spin_lock_irqsave(&iw_nodev_conns_lock, flags);
	list_add_tail(&ic->iw_node, &iw_nodev_conns);
	spin_unlock_irqrestore(&iw_nodev_conns_lock, flags);


	rdsdebug("conn %p conn ic %p\n", conn, conn->c_transport_data);
	return 0;
}

/*
 * Free a connection. Connection must be shut down and not set for reconnect.
 */
void rds_iw_conn_free(void *arg)
{
	struct rds_iw_connection *ic = arg;
	spinlock_t	*lock_ptr;

	rdsdebug("ic %p\n", ic);

	/*
	 * Conn is either on a dev's list or on the nodev list.
	 * A race with shutdown() or connect() would cause problems
	 * (since rds_iwdev would change) but that should never happen.
	 */
	lock_ptr = ic->rds_iwdev ? &ic->rds_iwdev->spinlock : &iw_nodev_conns_lock;

	spin_lock_irq(lock_ptr);
	list_del(&ic->iw_node);
	spin_unlock_irq(lock_ptr);

	kfree(ic);
}

/*
 * An error occurred on the connection
 */
void
__rds_iw_conn_error(struct rds_connection *conn, const char *fmt, ...)
{
	va_list ap;

	rds_conn_drop(conn);

	va_start(ap, fmt);
	vprintk(fmt, ap);
	va_end(ap);
}
