/*
 * Copyright (c) 2009 Oracle.  All rights reserved.
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
#include <linux/module.h>
#include <rdma/rdma_cm.h>

#include "rdma_transport.h"

static struct rdma_cm_id *rds_rdma_listen_id;

int rds_rdma_cm_event_handler(struct rdma_cm_id *cm_id,
			      struct rdma_cm_event *event)
{
	/* this can be null in the listening path */
	struct rds_connection *conn = cm_id->context;
	struct rds_transport *trans;
	int ret = 0;

	rdsdebug("conn %p id %p handling event %u (%s)\n", conn, cm_id,
		 event->event, rdma_event_msg(event->event));

	if (cm_id->device->node_type == RDMA_NODE_RNIC)
		trans = &rds_iw_transport;
	else
		trans = &rds_ib_transport;

	/* Prevent shutdown from tearing down the connection
	 * while we're executing. */
	if (conn) {
		mutex_lock(&conn->c_cm_lock);

		/* If the connection is being shut down, bail out
		 * right away. We return 0 so cm_id doesn't get
		 * destroyed prematurely */
		if (rds_conn_state(conn) == RDS_CONN_DISCONNECTING) {
			/* Reject incoming connections while we're tearing
			 * down an existing one. */
			if (event->event == RDMA_CM_EVENT_CONNECT_REQUEST)
				ret = 1;
			goto out;
		}
	}

	switch (event->event) {
	case RDMA_CM_EVENT_CONNECT_REQUEST:
		ret = trans->cm_handle_connect(cm_id, event);
		break;

	case RDMA_CM_EVENT_ADDR_RESOLVED:
		/* XXX do we need to clean up if this fails? */
		ret = rdma_resolve_route(cm_id,
					 RDS_RDMA_RESOLVE_TIMEOUT_MS);
		break;

	case RDMA_CM_EVENT_ROUTE_RESOLVED:
		/* XXX worry about racing with listen acceptance */
		ret = trans->cm_initiate_connect(cm_id);
		break;

	case RDMA_CM_EVENT_ESTABLISHED:
		trans->cm_connect_complete(conn, event);
		break;

	case RDMA_CM_EVENT_ADDR_ERROR:
	case RDMA_CM_EVENT_ROUTE_ERROR:
	case RDMA_CM_EVENT_CONNECT_ERROR:
	case RDMA_CM_EVENT_UNREACHABLE:
	case RDMA_CM_EVENT_REJECTED:
	case RDMA_CM_EVENT_DEVICE_REMOVAL:
	case RDMA_CM_EVENT_ADDR_CHANGE:
		if (conn)
			rds_conn_drop(conn);
		break;

	case RDMA_CM_EVENT_DISCONNECTED:
		rdsdebug("DISCONNECT event - dropping connection "
			"%pI4->%pI4\n", &conn->c_laddr,
			 &conn->c_faddr);
		rds_conn_drop(conn);
		break;

	default:
		/* things like device disconnect? */
		printk(KERN_ERR "RDS: unknown event %u (%s)!\n",
		       event->event, rdma_event_msg(event->event));
		break;
	}

out:
	if (conn)
		mutex_unlock(&conn->c_cm_lock);

	rdsdebug("id %p event %u (%s) handling ret %d\n", cm_id, event->event,
		 rdma_event_msg(event->event), ret);

	return ret;
}

static int rds_rdma_listen_init(void)
{
	struct sockaddr_in sin;
	struct rdma_cm_id *cm_id;
	int ret;

	cm_id = rdma_create_id(rds_rdma_cm_event_handler, NULL, RDMA_PS_TCP,
			       IB_QPT_RC);
	if (IS_ERR(cm_id)) {
		ret = PTR_ERR(cm_id);
		printk(KERN_ERR "RDS/RDMA: failed to setup listener, "
		       "rdma_create_id() returned %d\n", ret);
		return ret;
	}

	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = (__force u32)htonl(INADDR_ANY);
	sin.sin_port = (__force u16)htons(RDS_PORT);

	/*
	 * XXX I bet this binds the cm_id to a device.  If we want to support
	 * fail-over we'll have to take this into consideration.
	 */
	ret = rdma_bind_addr(cm_id, (struct sockaddr *)&sin);
	if (ret) {
		printk(KERN_ERR "RDS/RDMA: failed to setup listener, "
		       "rdma_bind_addr() returned %d\n", ret);
		goto out;
	}

	ret = rdma_listen(cm_id, 128);
	if (ret) {
		printk(KERN_ERR "RDS/RDMA: failed to setup listener, "
		       "rdma_listen() returned %d\n", ret);
		goto out;
	}

	rdsdebug("cm %p listening on port %u\n", cm_id, RDS_PORT);

	rds_rdma_listen_id = cm_id;
	cm_id = NULL;
out:
	if (cm_id)
		rdma_destroy_id(cm_id);
	return ret;
}

static void rds_rdma_listen_stop(void)
{
	if (rds_rdma_listen_id) {
		rdsdebug("cm %p\n", rds_rdma_listen_id);
		rdma_destroy_id(rds_rdma_listen_id);
		rds_rdma_listen_id = NULL;
	}
}

static int rds_rdma_init(void)
{
	int ret;

	ret = rds_rdma_listen_init();
	if (ret)
		goto out;

	ret = rds_iw_init();
	if (ret)
		goto err_iw_init;

	ret = rds_ib_init();
	if (ret)
		goto err_ib_init;

	goto out;

err_ib_init:
	rds_iw_exit();
err_iw_init:
	rds_rdma_listen_stop();
out:
	return ret;
}
module_init(rds_rdma_init);

static void rds_rdma_exit(void)
{
	/* stop listening first to ensure no new connections are attempted */
	rds_rdma_listen_stop();
	rds_ib_exit();
	rds_iw_exit();
}
module_exit(rds_rdma_exit);

MODULE_AUTHOR("Oracle Corporation <rds-devel@oss.oracle.com>");
MODULE_DESCRIPTION("RDS: IB/iWARP transport");
MODULE_LICENSE("Dual BSD/GPL");

