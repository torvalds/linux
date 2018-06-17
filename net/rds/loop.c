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
#include <linux/slab.h>
#include <linux/in.h>

#include "rds_single_path.h"
#include "rds.h"
#include "loop.h"

static DEFINE_SPINLOCK(loop_conns_lock);
static LIST_HEAD(loop_conns);

/*
 * This 'loopback' transport is a special case for flows that originate
 * and terminate on the same machine.
 *
 * Connection build-up notices if the destination address is thought of
 * as a local address by a transport.  At that time it decides to use the
 * loopback transport instead of the bound transport of the sending socket.
 *
 * The loopback transport's sending path just hands the sent rds_message
 * straight to the receiving path via an embedded rds_incoming.
 */

/*
 * Usually a message transits both the sender and receiver's conns as it
 * flows to the receiver.  In the loopback case, though, the receive path
 * is handed the sending conn so the sense of the addresses is reversed.
 */
static int rds_loop_xmit(struct rds_connection *conn, struct rds_message *rm,
			 unsigned int hdr_off, unsigned int sg,
			 unsigned int off)
{
	struct scatterlist *sgp = &rm->data.op_sg[sg];
	int ret = sizeof(struct rds_header) +
			be32_to_cpu(rm->m_inc.i_hdr.h_len);

	/* Do not send cong updates to loopback */
	if (rm->m_inc.i_hdr.h_flags & RDS_FLAG_CONG_BITMAP) {
		rds_cong_map_updated(conn->c_fcong, ~(u64) 0);
		ret = min_t(int, ret, sgp->length - conn->c_xmit_data_off);
		goto out;
	}

	BUG_ON(hdr_off || sg || off);

	rds_inc_init(&rm->m_inc, conn, conn->c_laddr);
	/* For the embedded inc. Matching put is in loop_inc_free() */
	rds_message_addref(rm);

	rds_recv_incoming(conn, conn->c_laddr, conn->c_faddr, &rm->m_inc,
			  GFP_KERNEL);

	rds_send_drop_acked(conn, be64_to_cpu(rm->m_inc.i_hdr.h_sequence),
			    NULL);

	rds_inc_put(&rm->m_inc);
out:
	return ret;
}

/*
 * See rds_loop_xmit(). Since our inc is embedded in the rm, we
 * make sure the rm lives at least until the inc is done.
 */
static void rds_loop_inc_free(struct rds_incoming *inc)
{
	struct rds_message *rm = container_of(inc, struct rds_message, m_inc);

	rds_message_put(rm);
}

/* we need to at least give the thread something to succeed */
static int rds_loop_recv_path(struct rds_conn_path *cp)
{
	return 0;
}

struct rds_loop_connection {
	struct list_head loop_node;
	struct rds_connection *conn;
};

/*
 * Even the loopback transport needs to keep track of its connections,
 * so it can call rds_conn_destroy() on them on exit. N.B. there are
 * 1+ loopback addresses (127.*.*.*) so it's not a bug to have
 * multiple loopback conns allocated, although rather useless.
 */
static int rds_loop_conn_alloc(struct rds_connection *conn, gfp_t gfp)
{
	struct rds_loop_connection *lc;
	unsigned long flags;

	lc = kzalloc(sizeof(struct rds_loop_connection), gfp);
	if (!lc)
		return -ENOMEM;

	INIT_LIST_HEAD(&lc->loop_node);
	lc->conn = conn;
	conn->c_transport_data = lc;

	spin_lock_irqsave(&loop_conns_lock, flags);
	list_add_tail(&lc->loop_node, &loop_conns);
	spin_unlock_irqrestore(&loop_conns_lock, flags);

	return 0;
}

static void rds_loop_conn_free(void *arg)
{
	struct rds_loop_connection *lc = arg;
	unsigned long flags;

	rdsdebug("lc %p\n", lc);
	spin_lock_irqsave(&loop_conns_lock, flags);
	list_del(&lc->loop_node);
	spin_unlock_irqrestore(&loop_conns_lock, flags);
	kfree(lc);
}

static int rds_loop_conn_path_connect(struct rds_conn_path *cp)
{
	rds_connect_complete(cp->cp_conn);
	return 0;
}

static void rds_loop_conn_path_shutdown(struct rds_conn_path *cp)
{
}

void rds_loop_exit(void)
{
	struct rds_loop_connection *lc, *_lc;
	LIST_HEAD(tmp_list);

	/* avoid calling conn_destroy with irqs off */
	spin_lock_irq(&loop_conns_lock);
	list_splice(&loop_conns, &tmp_list);
	INIT_LIST_HEAD(&loop_conns);
	spin_unlock_irq(&loop_conns_lock);

	list_for_each_entry_safe(lc, _lc, &tmp_list, loop_node) {
		WARN_ON(lc->conn->c_passive);
		rds_conn_destroy(lc->conn);
	}
}

/*
 * This is missing .xmit_* because loop doesn't go through generic
 * rds_send_xmit() and doesn't call rds_recv_incoming().  .listen_stop and
 * .laddr_check are missing because transport.c doesn't iterate over
 * rds_loop_transport.
 */
struct rds_transport rds_loop_transport = {
	.xmit			= rds_loop_xmit,
	.recv_path		= rds_loop_recv_path,
	.conn_alloc		= rds_loop_conn_alloc,
	.conn_free		= rds_loop_conn_free,
	.conn_path_connect	= rds_loop_conn_path_connect,
	.conn_path_shutdown	= rds_loop_conn_path_shutdown,
	.inc_copy_to_user	= rds_message_inc_copy_to_user,
	.inc_free		= rds_loop_inc_free,
	.t_name			= "loopback",
	.t_type			= RDS_TRANS_LOOP,
};
