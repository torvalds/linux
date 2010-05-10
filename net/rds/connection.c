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
#include <linux/list.h>
#include <linux/slab.h>
#include <net/inet_hashtables.h>

#include "rds.h"
#include "loop.h"
#include "rdma.h"

#define RDS_CONNECTION_HASH_BITS 12
#define RDS_CONNECTION_HASH_ENTRIES (1 << RDS_CONNECTION_HASH_BITS)
#define RDS_CONNECTION_HASH_MASK (RDS_CONNECTION_HASH_ENTRIES - 1)

/* converting this to RCU is a chore for another day.. */
static DEFINE_SPINLOCK(rds_conn_lock);
static unsigned long rds_conn_count;
static struct hlist_head rds_conn_hash[RDS_CONNECTION_HASH_ENTRIES];
static struct kmem_cache *rds_conn_slab;

static struct hlist_head *rds_conn_bucket(__be32 laddr, __be32 faddr)
{
	/* Pass NULL, don't need struct net for hash */
	unsigned long hash = inet_ehashfn(NULL,
					  be32_to_cpu(laddr), 0,
					  be32_to_cpu(faddr), 0);
	return &rds_conn_hash[hash & RDS_CONNECTION_HASH_MASK];
}

#define rds_conn_info_set(var, test, suffix) do {		\
	if (test)						\
		var |= RDS_INFO_CONNECTION_FLAG_##suffix;	\
} while (0)

static inline int rds_conn_is_sending(struct rds_connection *conn)
{
	int ret = 0;

	if (!mutex_trylock(&conn->c_send_lock))
		ret = 1;
	else
		mutex_unlock(&conn->c_send_lock);

	return ret;
}

static struct rds_connection *rds_conn_lookup(struct hlist_head *head,
					      __be32 laddr, __be32 faddr,
					      struct rds_transport *trans)
{
	struct rds_connection *conn, *ret = NULL;
	struct hlist_node *pos;

	hlist_for_each_entry(conn, pos, head, c_hash_node) {
		if (conn->c_faddr == faddr && conn->c_laddr == laddr &&
				conn->c_trans == trans) {
			ret = conn;
			break;
		}
	}
	rdsdebug("returning conn %p for %pI4 -> %pI4\n", ret,
		 &laddr, &faddr);
	return ret;
}

/*
 * This is called by transports as they're bringing down a connection.
 * It clears partial message state so that the transport can start sending
 * and receiving over this connection again in the future.  It is up to
 * the transport to have serialized this call with its send and recv.
 */
void rds_conn_reset(struct rds_connection *conn)
{
	rdsdebug("connection %pI4 to %pI4 reset\n",
	  &conn->c_laddr, &conn->c_faddr);

	rds_stats_inc(s_conn_reset);
	rds_send_reset(conn);
	conn->c_flags = 0;

	/* Do not clear next_rx_seq here, else we cannot distinguish
	 * retransmitted packets from new packets, and will hand all
	 * of them to the application. That is not consistent with the
	 * reliability guarantees of RDS. */
}

/*
 * There is only every one 'conn' for a given pair of addresses in the
 * system at a time.  They contain messages to be retransmitted and so
 * span the lifetime of the actual underlying transport connections.
 *
 * For now they are not garbage collected once they're created.  They
 * are torn down as the module is removed, if ever.
 */
static struct rds_connection *__rds_conn_create(__be32 laddr, __be32 faddr,
				       struct rds_transport *trans, gfp_t gfp,
				       int is_outgoing)
{
	struct rds_connection *conn, *parent = NULL;
	struct hlist_head *head = rds_conn_bucket(laddr, faddr);
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&rds_conn_lock, flags);
	conn = rds_conn_lookup(head, laddr, faddr, trans);
	if (conn && conn->c_loopback && conn->c_trans != &rds_loop_transport &&
	    !is_outgoing) {
		/* This is a looped back IB connection, and we're
		 * called by the code handling the incoming connect.
		 * We need a second connection object into which we
		 * can stick the other QP. */
		parent = conn;
		conn = parent->c_passive;
	}
	spin_unlock_irqrestore(&rds_conn_lock, flags);
	if (conn)
		goto out;

	conn = kmem_cache_zalloc(rds_conn_slab, gfp);
	if (conn == NULL) {
		conn = ERR_PTR(-ENOMEM);
		goto out;
	}

	INIT_HLIST_NODE(&conn->c_hash_node);
	conn->c_laddr = laddr;
	conn->c_faddr = faddr;
	spin_lock_init(&conn->c_lock);
	conn->c_next_tx_seq = 1;

	mutex_init(&conn->c_send_lock);
	INIT_LIST_HEAD(&conn->c_send_queue);
	INIT_LIST_HEAD(&conn->c_retrans);

	ret = rds_cong_get_maps(conn);
	if (ret) {
		kmem_cache_free(rds_conn_slab, conn);
		conn = ERR_PTR(ret);
		goto out;
	}

	/*
	 * This is where a connection becomes loopback.  If *any* RDS sockets
	 * can bind to the destination address then we'd rather the messages
	 * flow through loopback rather than either transport.
	 */
	if (rds_trans_get_preferred(faddr)) {
		conn->c_loopback = 1;
		if (is_outgoing && trans->t_prefer_loopback) {
			/* "outgoing" connection - and the transport
			 * says it wants the connection handled by the
			 * loopback transport. This is what TCP does.
			 */
			trans = &rds_loop_transport;
		}
	}

	conn->c_trans = trans;

	ret = trans->conn_alloc(conn, gfp);
	if (ret) {
		kmem_cache_free(rds_conn_slab, conn);
		conn = ERR_PTR(ret);
		goto out;
	}

	atomic_set(&conn->c_state, RDS_CONN_DOWN);
	conn->c_reconnect_jiffies = 0;
	INIT_DELAYED_WORK(&conn->c_send_w, rds_send_worker);
	INIT_DELAYED_WORK(&conn->c_recv_w, rds_recv_worker);
	INIT_DELAYED_WORK(&conn->c_conn_w, rds_connect_worker);
	INIT_WORK(&conn->c_down_w, rds_shutdown_worker);
	mutex_init(&conn->c_cm_lock);
	conn->c_flags = 0;

	rdsdebug("allocated conn %p for %pI4 -> %pI4 over %s %s\n",
	  conn, &laddr, &faddr,
	  trans->t_name ? trans->t_name : "[unknown]",
	  is_outgoing ? "(outgoing)" : "");

	/*
	 * Since we ran without holding the conn lock, someone could
	 * have created the same conn (either normal or passive) in the
	 * interim. We check while holding the lock. If we won, we complete
	 * init and return our conn. If we lost, we rollback and return the
	 * other one.
	 */
	spin_lock_irqsave(&rds_conn_lock, flags);
	if (parent) {
		/* Creating passive conn */
		if (parent->c_passive) {
			trans->conn_free(conn->c_transport_data);
			kmem_cache_free(rds_conn_slab, conn);
			conn = parent->c_passive;
		} else {
			parent->c_passive = conn;
			rds_cong_add_conn(conn);
			rds_conn_count++;
		}
	} else {
		/* Creating normal conn */
		struct rds_connection *found;

		found = rds_conn_lookup(head, laddr, faddr, trans);
		if (found) {
			trans->conn_free(conn->c_transport_data);
			kmem_cache_free(rds_conn_slab, conn);
			conn = found;
		} else {
			hlist_add_head(&conn->c_hash_node, head);
			rds_cong_add_conn(conn);
			rds_conn_count++;
		}
	}
	spin_unlock_irqrestore(&rds_conn_lock, flags);

out:
	return conn;
}

struct rds_connection *rds_conn_create(__be32 laddr, __be32 faddr,
				       struct rds_transport *trans, gfp_t gfp)
{
	return __rds_conn_create(laddr, faddr, trans, gfp, 0);
}
EXPORT_SYMBOL_GPL(rds_conn_create);

struct rds_connection *rds_conn_create_outgoing(__be32 laddr, __be32 faddr,
				       struct rds_transport *trans, gfp_t gfp)
{
	return __rds_conn_create(laddr, faddr, trans, gfp, 1);
}
EXPORT_SYMBOL_GPL(rds_conn_create_outgoing);

void rds_conn_destroy(struct rds_connection *conn)
{
	struct rds_message *rm, *rtmp;

	rdsdebug("freeing conn %p for %pI4 -> "
		 "%pI4\n", conn, &conn->c_laddr,
		 &conn->c_faddr);

	hlist_del_init(&conn->c_hash_node);

	/* wait for the rds thread to shut it down */
	atomic_set(&conn->c_state, RDS_CONN_ERROR);
	cancel_delayed_work(&conn->c_conn_w);
	queue_work(rds_wq, &conn->c_down_w);
	flush_workqueue(rds_wq);

	/* tear down queued messages */
	list_for_each_entry_safe(rm, rtmp,
				 &conn->c_send_queue,
				 m_conn_item) {
		list_del_init(&rm->m_conn_item);
		BUG_ON(!list_empty(&rm->m_sock_item));
		rds_message_put(rm);
	}
	if (conn->c_xmit_rm)
		rds_message_put(conn->c_xmit_rm);

	conn->c_trans->conn_free(conn->c_transport_data);

	/*
	 * The congestion maps aren't freed up here.  They're
	 * freed by rds_cong_exit() after all the connections
	 * have been freed.
	 */
	rds_cong_remove_conn(conn);

	BUG_ON(!list_empty(&conn->c_retrans));
	kmem_cache_free(rds_conn_slab, conn);

	rds_conn_count--;
}
EXPORT_SYMBOL_GPL(rds_conn_destroy);

static void rds_conn_message_info(struct socket *sock, unsigned int len,
				  struct rds_info_iterator *iter,
				  struct rds_info_lengths *lens,
				  int want_send)
{
	struct hlist_head *head;
	struct hlist_node *pos;
	struct list_head *list;
	struct rds_connection *conn;
	struct rds_message *rm;
	unsigned long flags;
	unsigned int total = 0;
	size_t i;

	len /= sizeof(struct rds_info_message);

	spin_lock_irqsave(&rds_conn_lock, flags);

	for (i = 0, head = rds_conn_hash; i < ARRAY_SIZE(rds_conn_hash);
	     i++, head++) {
		hlist_for_each_entry(conn, pos, head, c_hash_node) {
			if (want_send)
				list = &conn->c_send_queue;
			else
				list = &conn->c_retrans;

			spin_lock(&conn->c_lock);

			/* XXX too lazy to maintain counts.. */
			list_for_each_entry(rm, list, m_conn_item) {
				total++;
				if (total <= len)
					rds_inc_info_copy(&rm->m_inc, iter,
							  conn->c_laddr,
							  conn->c_faddr, 0);
			}

			spin_unlock(&conn->c_lock);
		}
	}

	spin_unlock_irqrestore(&rds_conn_lock, flags);

	lens->nr = total;
	lens->each = sizeof(struct rds_info_message);
}

static void rds_conn_message_info_send(struct socket *sock, unsigned int len,
				       struct rds_info_iterator *iter,
				       struct rds_info_lengths *lens)
{
	rds_conn_message_info(sock, len, iter, lens, 1);
}

static void rds_conn_message_info_retrans(struct socket *sock,
					  unsigned int len,
					  struct rds_info_iterator *iter,
					  struct rds_info_lengths *lens)
{
	rds_conn_message_info(sock, len, iter, lens, 0);
}

void rds_for_each_conn_info(struct socket *sock, unsigned int len,
			  struct rds_info_iterator *iter,
			  struct rds_info_lengths *lens,
			  int (*visitor)(struct rds_connection *, void *),
			  size_t item_len)
{
	uint64_t buffer[(item_len + 7) / 8];
	struct hlist_head *head;
	struct hlist_node *pos;
	struct hlist_node *tmp;
	struct rds_connection *conn;
	unsigned long flags;
	size_t i;

	spin_lock_irqsave(&rds_conn_lock, flags);

	lens->nr = 0;
	lens->each = item_len;

	for (i = 0, head = rds_conn_hash; i < ARRAY_SIZE(rds_conn_hash);
	     i++, head++) {
		hlist_for_each_entry_safe(conn, pos, tmp, head, c_hash_node) {

			/* XXX no c_lock usage.. */
			if (!visitor(conn, buffer))
				continue;

			/* We copy as much as we can fit in the buffer,
			 * but we count all items so that the caller
			 * can resize the buffer. */
			if (len >= item_len) {
				rds_info_copy(iter, buffer, item_len);
				len -= item_len;
			}
			lens->nr++;
		}
	}

	spin_unlock_irqrestore(&rds_conn_lock, flags);
}
EXPORT_SYMBOL_GPL(rds_for_each_conn_info);

static int rds_conn_info_visitor(struct rds_connection *conn,
				  void *buffer)
{
	struct rds_info_connection *cinfo = buffer;

	cinfo->next_tx_seq = conn->c_next_tx_seq;
	cinfo->next_rx_seq = conn->c_next_rx_seq;
	cinfo->laddr = conn->c_laddr;
	cinfo->faddr = conn->c_faddr;
	strncpy(cinfo->transport, conn->c_trans->t_name,
		sizeof(cinfo->transport));
	cinfo->flags = 0;

	rds_conn_info_set(cinfo->flags,
			  rds_conn_is_sending(conn), SENDING);
	/* XXX Future: return the state rather than these funky bits */
	rds_conn_info_set(cinfo->flags,
			  atomic_read(&conn->c_state) == RDS_CONN_CONNECTING,
			  CONNECTING);
	rds_conn_info_set(cinfo->flags,
			  atomic_read(&conn->c_state) == RDS_CONN_UP,
			  CONNECTED);
	return 1;
}

static void rds_conn_info(struct socket *sock, unsigned int len,
			  struct rds_info_iterator *iter,
			  struct rds_info_lengths *lens)
{
	rds_for_each_conn_info(sock, len, iter, lens,
				rds_conn_info_visitor,
				sizeof(struct rds_info_connection));
}

int __init rds_conn_init(void)
{
	rds_conn_slab = kmem_cache_create("rds_connection",
					  sizeof(struct rds_connection),
					  0, 0, NULL);
	if (rds_conn_slab == NULL)
		return -ENOMEM;

	rds_info_register_func(RDS_INFO_CONNECTIONS, rds_conn_info);
	rds_info_register_func(RDS_INFO_SEND_MESSAGES,
			       rds_conn_message_info_send);
	rds_info_register_func(RDS_INFO_RETRANS_MESSAGES,
			       rds_conn_message_info_retrans);

	return 0;
}

void rds_conn_exit(void)
{
	rds_loop_exit();

	WARN_ON(!hlist_empty(rds_conn_hash));

	kmem_cache_destroy(rds_conn_slab);

	rds_info_deregister_func(RDS_INFO_CONNECTIONS, rds_conn_info);
	rds_info_deregister_func(RDS_INFO_SEND_MESSAGES,
				 rds_conn_message_info_send);
	rds_info_deregister_func(RDS_INFO_RETRANS_MESSAGES,
				 rds_conn_message_info_retrans);
}

/*
 * Force a disconnect
 */
void rds_conn_drop(struct rds_connection *conn)
{
	atomic_set(&conn->c_state, RDS_CONN_ERROR);
	queue_work(rds_wq, &conn->c_down_w);
}
EXPORT_SYMBOL_GPL(rds_conn_drop);

/*
 * An error occurred on the connection
 */
void
__rds_conn_error(struct rds_connection *conn, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vprintk(fmt, ap);
	va_end(ap);

	rds_conn_drop(conn);
}
