/*
 * Copyright (c) 2006, 2018 Oracle and/or its affiliates. All rights reserved.
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
#include <linux/random.h>
#include <linux/export.h>

#include "rds.h"

/*
 * All of connection management is simplified by serializing it through
 * work queues that execute in a connection managing thread.
 *
 * TCP wants to send acks through sendpage() in response to data_ready(),
 * but it needs a process context to do so.
 *
 * The receive paths need to allocate but can't drop packets (!) so we have
 * a thread around to block allocating if the receive fast path sees an
 * allocation failure.
 */

/* Grand Unified Theory of connection life cycle:
 * At any point in time, the connection can be in one of these states:
 * DOWN, CONNECTING, UP, DISCONNECTING, ERROR
 *
 * The following transitions are possible:
 *  ANY		  -> ERROR
 *  UP		  -> DISCONNECTING
 *  ERROR	  -> DISCONNECTING
 *  DISCONNECTING -> DOWN
 *  DOWN	  -> CONNECTING
 *  CONNECTING	  -> UP
 *
 * Transition to state DISCONNECTING/DOWN:
 *  -	Inside the shutdown worker; synchronizes with xmit path
 *	through RDS_IN_XMIT, and with connection management callbacks
 *	via c_cm_lock.
 *
 *	For receive callbacks, we rely on the underlying transport
 *	(TCP, IB/RDMA) to provide the necessary synchronisation.
 */
struct workqueue_struct *rds_wq;
EXPORT_SYMBOL_GPL(rds_wq);

void rds_connect_path_complete(struct rds_conn_path *cp, int curr)
{
	if (!rds_conn_path_transition(cp, curr, RDS_CONN_UP)) {
		printk(KERN_WARNING "%s: Cannot transition to state UP, "
				"current state is %d\n",
				__func__,
				atomic_read(&cp->cp_state));
		rds_conn_path_drop(cp, false);
		return;
	}

	rdsdebug("conn %p for %pI6c to %pI6c complete\n",
		 cp->cp_conn, &cp->cp_conn->c_laddr, &cp->cp_conn->c_faddr);

	cp->cp_reconnect_jiffies = 0;
	set_bit(0, &cp->cp_conn->c_map_queued);
	rcu_read_lock();
	if (!rds_destroy_pending(cp->cp_conn)) {
		queue_delayed_work(rds_wq, &cp->cp_send_w, 0);
		queue_delayed_work(rds_wq, &cp->cp_recv_w, 0);
	}
	rcu_read_unlock();
	cp->cp_conn->c_proposed_version = RDS_PROTOCOL_VERSION;
}
EXPORT_SYMBOL_GPL(rds_connect_path_complete);

void rds_connect_complete(struct rds_connection *conn)
{
	rds_connect_path_complete(&conn->c_path[0], RDS_CONN_CONNECTING);
}
EXPORT_SYMBOL_GPL(rds_connect_complete);

/*
 * This random exponential backoff is relied on to eventually resolve racing
 * connects.
 *
 * If connect attempts race then both parties drop both connections and come
 * here to wait for a random amount of time before trying again.  Eventually
 * the backoff range will be so much greater than the time it takes to
 * establish a connection that one of the pair will establish the connection
 * before the other's random delay fires.
 *
 * Connection attempts that arrive while a connection is already established
 * are also considered to be racing connects.  This lets a connection from
 * a rebooted machine replace an existing stale connection before the transport
 * notices that the connection has failed.
 *
 * We should *always* start with a random backoff; otherwise a broken connection
 * will always take several iterations to be re-established.
 */
void rds_queue_reconnect(struct rds_conn_path *cp)
{
	unsigned long rand;
	struct rds_connection *conn = cp->cp_conn;

	rdsdebug("conn %p for %pI6c to %pI6c reconnect jiffies %lu\n",
		 conn, &conn->c_laddr, &conn->c_faddr,
		 cp->cp_reconnect_jiffies);

	/* let peer with smaller addr initiate reconnect, to avoid duels */
	if (conn->c_trans->t_type == RDS_TRANS_TCP &&
	    rds_addr_cmp(&conn->c_laddr, &conn->c_faddr) >= 0)
		return;

	set_bit(RDS_RECONNECT_PENDING, &cp->cp_flags);
	if (cp->cp_reconnect_jiffies == 0) {
		cp->cp_reconnect_jiffies = rds_sysctl_reconnect_min_jiffies;
		rcu_read_lock();
		if (!rds_destroy_pending(cp->cp_conn))
			queue_delayed_work(rds_wq, &cp->cp_conn_w, 0);
		rcu_read_unlock();
		return;
	}

	get_random_bytes(&rand, sizeof(rand));
	rdsdebug("%lu delay %lu ceil conn %p for %pI6c -> %pI6c\n",
		 rand % cp->cp_reconnect_jiffies, cp->cp_reconnect_jiffies,
		 conn, &conn->c_laddr, &conn->c_faddr);
	rcu_read_lock();
	if (!rds_destroy_pending(cp->cp_conn))
		queue_delayed_work(rds_wq, &cp->cp_conn_w,
				   rand % cp->cp_reconnect_jiffies);
	rcu_read_unlock();

	cp->cp_reconnect_jiffies = min(cp->cp_reconnect_jiffies * 2,
					rds_sysctl_reconnect_max_jiffies);
}

void rds_connect_worker(struct work_struct *work)
{
	struct rds_conn_path *cp = container_of(work,
						struct rds_conn_path,
						cp_conn_w.work);
	struct rds_connection *conn = cp->cp_conn;
	int ret;

	if (cp->cp_index > 0 &&
	    rds_addr_cmp(&cp->cp_conn->c_laddr, &cp->cp_conn->c_faddr) >= 0)
		return;
	clear_bit(RDS_RECONNECT_PENDING, &cp->cp_flags);
	ret = rds_conn_path_transition(cp, RDS_CONN_DOWN, RDS_CONN_CONNECTING);
	if (ret) {
		ret = conn->c_trans->conn_path_connect(cp);
		rdsdebug("conn %p for %pI6c to %pI6c dispatched, ret %d\n",
			 conn, &conn->c_laddr, &conn->c_faddr, ret);

		if (ret) {
			if (rds_conn_path_transition(cp,
						     RDS_CONN_CONNECTING,
						     RDS_CONN_DOWN))
				rds_queue_reconnect(cp);
			else
				rds_conn_path_error(cp, "connect failed\n");
		}
	}
}

void rds_send_worker(struct work_struct *work)
{
	struct rds_conn_path *cp = container_of(work,
						struct rds_conn_path,
						cp_send_w.work);
	int ret;

	if (rds_conn_path_state(cp) == RDS_CONN_UP) {
		clear_bit(RDS_LL_SEND_FULL, &cp->cp_flags);
		ret = rds_send_xmit(cp);
		cond_resched();
		rdsdebug("conn %p ret %d\n", cp->cp_conn, ret);
		switch (ret) {
		case -EAGAIN:
			rds_stats_inc(s_send_immediate_retry);
			queue_delayed_work(rds_wq, &cp->cp_send_w, 0);
			break;
		case -ENOMEM:
			rds_stats_inc(s_send_delayed_retry);
			queue_delayed_work(rds_wq, &cp->cp_send_w, 2);
			break;
		default:
			break;
		}
	}
}

void rds_recv_worker(struct work_struct *work)
{
	struct rds_conn_path *cp = container_of(work,
						struct rds_conn_path,
						cp_recv_w.work);
	int ret;

	if (rds_conn_path_state(cp) == RDS_CONN_UP) {
		ret = cp->cp_conn->c_trans->recv_path(cp);
		rdsdebug("conn %p ret %d\n", cp->cp_conn, ret);
		switch (ret) {
		case -EAGAIN:
			rds_stats_inc(s_recv_immediate_retry);
			queue_delayed_work(rds_wq, &cp->cp_recv_w, 0);
			break;
		case -ENOMEM:
			rds_stats_inc(s_recv_delayed_retry);
			queue_delayed_work(rds_wq, &cp->cp_recv_w, 2);
			break;
		default:
			break;
		}
	}
}

void rds_shutdown_worker(struct work_struct *work)
{
	struct rds_conn_path *cp = container_of(work,
						struct rds_conn_path,
						cp_down_w);

	rds_conn_shutdown(cp);
}

void rds_threads_exit(void)
{
	destroy_workqueue(rds_wq);
}

int rds_threads_init(void)
{
	rds_wq = create_singlethread_workqueue("krdsd");
	if (!rds_wq)
		return -ENOMEM;

	return 0;
}

/* Compare two IPv6 addresses.  Return 0 if the two addresses are equal.
 * Return 1 if the first is greater.  Return -1 if the second is greater.
 */
int rds_addr_cmp(const struct in6_addr *addr1,
		 const struct in6_addr *addr2)
{
#if defined(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS) && BITS_PER_LONG == 64
	const __be64 *a1, *a2;
	u64 x, y;

	a1 = (__be64 *)addr1;
	a2 = (__be64 *)addr2;

	if (*a1 != *a2) {
		if (be64_to_cpu(*a1) < be64_to_cpu(*a2))
			return -1;
		else
			return 1;
	} else {
		x = be64_to_cpu(*++a1);
		y = be64_to_cpu(*++a2);
		if (x < y)
			return -1;
		else if (x > y)
			return 1;
		else
			return 0;
	}
#else
	u32 a, b;
	int i;

	for (i = 0; i < 4; i++) {
		if (addr1->s6_addr32[i] != addr2->s6_addr32[i]) {
			a = ntohl(addr1->s6_addr32[i]);
			b = ntohl(addr2->s6_addr32[i]);
			if (a < b)
				return -1;
			else if (a > b)
				return 1;
		}
	}
	return 0;
#endif
}
EXPORT_SYMBOL_GPL(rds_addr_cmp);
