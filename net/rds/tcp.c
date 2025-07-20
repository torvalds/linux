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
#include <linux/slab.h>
#include <linux/in.h>
#include <linux/module.h>
#include <net/tcp.h>
#include <net/net_namespace.h>
#include <net/netns/generic.h>
#include <net/addrconf.h>

#include "rds.h"
#include "tcp.h"

/* only for info exporting */
static DEFINE_SPINLOCK(rds_tcp_tc_list_lock);
static LIST_HEAD(rds_tcp_tc_list);

/* rds_tcp_tc_count counts only IPv4 connections.
 * rds6_tcp_tc_count counts both IPv4 and IPv6 connections.
 */
static unsigned int rds_tcp_tc_count;
#if IS_ENABLED(CONFIG_IPV6)
static unsigned int rds6_tcp_tc_count;
#endif

/* Track rds_tcp_connection structs so they can be cleaned up */
static DEFINE_SPINLOCK(rds_tcp_conn_lock);
static LIST_HEAD(rds_tcp_conn_list);
static atomic_t rds_tcp_unloading = ATOMIC_INIT(0);

static struct kmem_cache *rds_tcp_conn_slab;

static int rds_tcp_sndbuf_handler(const struct ctl_table *ctl, int write,
				  void *buffer, size_t *lenp, loff_t *fpos);
static int rds_tcp_rcvbuf_handler(const struct ctl_table *ctl, int write,
				  void *buffer, size_t *lenp, loff_t *fpos);

static int rds_tcp_min_sndbuf = SOCK_MIN_SNDBUF;
static int rds_tcp_min_rcvbuf = SOCK_MIN_RCVBUF;

static struct ctl_table rds_tcp_sysctl_table[] = {
#define	RDS_TCP_SNDBUF	0
	{
		.procname       = "rds_tcp_sndbuf",
		/* data is per-net pointer */
		.maxlen         = sizeof(int),
		.mode           = 0644,
		.proc_handler   = rds_tcp_sndbuf_handler,
		.extra1		= &rds_tcp_min_sndbuf,
	},
#define	RDS_TCP_RCVBUF	1
	{
		.procname       = "rds_tcp_rcvbuf",
		/* data is per-net pointer */
		.maxlen         = sizeof(int),
		.mode           = 0644,
		.proc_handler   = rds_tcp_rcvbuf_handler,
		.extra1		= &rds_tcp_min_rcvbuf,
	},
};

u32 rds_tcp_write_seq(struct rds_tcp_connection *tc)
{
	/* seq# of the last byte of data in tcp send buffer */
	return tcp_sk(tc->t_sock->sk)->write_seq;
}

u32 rds_tcp_snd_una(struct rds_tcp_connection *tc)
{
	return tcp_sk(tc->t_sock->sk)->snd_una;
}

void rds_tcp_restore_callbacks(struct socket *sock,
			       struct rds_tcp_connection *tc)
{
	rdsdebug("restoring sock %p callbacks from tc %p\n", sock, tc);
	write_lock_bh(&sock->sk->sk_callback_lock);

	/* done under the callback_lock to serialize with write_space */
	spin_lock(&rds_tcp_tc_list_lock);
	list_del_init(&tc->t_list_item);
#if IS_ENABLED(CONFIG_IPV6)
	rds6_tcp_tc_count--;
#endif
	if (!tc->t_cpath->cp_conn->c_isv6)
		rds_tcp_tc_count--;
	spin_unlock(&rds_tcp_tc_list_lock);

	tc->t_sock = NULL;

	sock->sk->sk_write_space = tc->t_orig_write_space;
	sock->sk->sk_data_ready = tc->t_orig_data_ready;
	sock->sk->sk_state_change = tc->t_orig_state_change;
	sock->sk->sk_user_data = NULL;

	write_unlock_bh(&sock->sk->sk_callback_lock);
}

/*
 * rds_tcp_reset_callbacks() switches the to the new sock and
 * returns the existing tc->t_sock.
 *
 * The only functions that set tc->t_sock are rds_tcp_set_callbacks
 * and rds_tcp_reset_callbacks.  Send and receive trust that
 * it is set.  The absence of RDS_CONN_UP bit protects those paths
 * from being called while it isn't set.
 */
void rds_tcp_reset_callbacks(struct socket *sock,
			     struct rds_conn_path *cp)
{
	struct rds_tcp_connection *tc = cp->cp_transport_data;
	struct socket *osock = tc->t_sock;

	if (!osock)
		goto newsock;

	/* Need to resolve a duelling SYN between peers.
	 * We have an outstanding SYN to this peer, which may
	 * potentially have transitioned to the RDS_CONN_UP state,
	 * so we must quiesce any send threads before resetting
	 * cp_transport_data. We quiesce these threads by setting
	 * cp_state to something other than RDS_CONN_UP, and then
	 * waiting for any existing threads in rds_send_xmit to
	 * complete release_in_xmit(). (Subsequent threads entering
	 * rds_send_xmit() will bail on !rds_conn_up().
	 *
	 * However an incoming syn-ack at this point would end up
	 * marking the conn as RDS_CONN_UP, and would again permit
	 * rds_send_xmi() threads through, so ideally we would
	 * synchronize on RDS_CONN_UP after lock_sock(), but cannot
	 * do that: waiting on !RDS_IN_XMIT after lock_sock() may
	 * end up deadlocking with tcp_sendmsg(), and the RDS_IN_XMIT
	 * would not get set. As a result, we set c_state to
	 * RDS_CONN_RESETTTING, to ensure that rds_tcp_state_change
	 * cannot mark rds_conn_path_up() in the window before lock_sock()
	 */
	atomic_set(&cp->cp_state, RDS_CONN_RESETTING);
	wait_event(cp->cp_waitq, !test_bit(RDS_IN_XMIT, &cp->cp_flags));
	/* reset receive side state for rds_tcp_data_recv() for osock  */
	cancel_delayed_work_sync(&cp->cp_send_w);
	cancel_delayed_work_sync(&cp->cp_recv_w);
	lock_sock(osock->sk);
	if (tc->t_tinc) {
		rds_inc_put(&tc->t_tinc->ti_inc);
		tc->t_tinc = NULL;
	}
	tc->t_tinc_hdr_rem = sizeof(struct rds_header);
	tc->t_tinc_data_rem = 0;
	rds_tcp_restore_callbacks(osock, tc);
	release_sock(osock->sk);
	sock_release(osock);
newsock:
	rds_send_path_reset(cp);
	lock_sock(sock->sk);
	rds_tcp_set_callbacks(sock, cp);
	release_sock(sock->sk);
}

/* Add tc to rds_tcp_tc_list and set tc->t_sock. See comments
 * above rds_tcp_reset_callbacks for notes about synchronization
 * with data path
 */
void rds_tcp_set_callbacks(struct socket *sock, struct rds_conn_path *cp)
{
	struct rds_tcp_connection *tc = cp->cp_transport_data;

	rdsdebug("setting sock %p callbacks to tc %p\n", sock, tc);
	write_lock_bh(&sock->sk->sk_callback_lock);

	/* done under the callback_lock to serialize with write_space */
	spin_lock(&rds_tcp_tc_list_lock);
	list_add_tail(&tc->t_list_item, &rds_tcp_tc_list);
#if IS_ENABLED(CONFIG_IPV6)
	rds6_tcp_tc_count++;
#endif
	if (!tc->t_cpath->cp_conn->c_isv6)
		rds_tcp_tc_count++;
	spin_unlock(&rds_tcp_tc_list_lock);

	/* accepted sockets need our listen data ready undone */
	if (sock->sk->sk_data_ready == rds_tcp_listen_data_ready)
		sock->sk->sk_data_ready = sock->sk->sk_user_data;

	tc->t_sock = sock;
	tc->t_cpath = cp;
	tc->t_orig_data_ready = sock->sk->sk_data_ready;
	tc->t_orig_write_space = sock->sk->sk_write_space;
	tc->t_orig_state_change = sock->sk->sk_state_change;

	sock->sk->sk_user_data = cp;
	sock->sk->sk_data_ready = rds_tcp_data_ready;
	sock->sk->sk_write_space = rds_tcp_write_space;
	sock->sk->sk_state_change = rds_tcp_state_change;

	write_unlock_bh(&sock->sk->sk_callback_lock);
}

/* Handle RDS_INFO_TCP_SOCKETS socket option.  It only returns IPv4
 * connections for backward compatibility.
 */
static void rds_tcp_tc_info(struct socket *rds_sock, unsigned int len,
			    struct rds_info_iterator *iter,
			    struct rds_info_lengths *lens)
{
	struct rds_info_tcp_socket tsinfo;
	struct rds_tcp_connection *tc;
	unsigned long flags;

	spin_lock_irqsave(&rds_tcp_tc_list_lock, flags);

	if (len / sizeof(tsinfo) < rds_tcp_tc_count)
		goto out;

	list_for_each_entry(tc, &rds_tcp_tc_list, t_list_item) {
		struct inet_sock *inet = inet_sk(tc->t_sock->sk);

		if (tc->t_cpath->cp_conn->c_isv6)
			continue;

		tsinfo.local_addr = inet->inet_saddr;
		tsinfo.local_port = inet->inet_sport;
		tsinfo.peer_addr = inet->inet_daddr;
		tsinfo.peer_port = inet->inet_dport;

		tsinfo.hdr_rem = tc->t_tinc_hdr_rem;
		tsinfo.data_rem = tc->t_tinc_data_rem;
		tsinfo.last_sent_nxt = tc->t_last_sent_nxt;
		tsinfo.last_expected_una = tc->t_last_expected_una;
		tsinfo.last_seen_una = tc->t_last_seen_una;
		tsinfo.tos = tc->t_cpath->cp_conn->c_tos;

		rds_info_copy(iter, &tsinfo, sizeof(tsinfo));
	}

out:
	lens->nr = rds_tcp_tc_count;
	lens->each = sizeof(tsinfo);

	spin_unlock_irqrestore(&rds_tcp_tc_list_lock, flags);
}

#if IS_ENABLED(CONFIG_IPV6)
/* Handle RDS6_INFO_TCP_SOCKETS socket option. It returns both IPv4 and
 * IPv6 connections. IPv4 connection address is returned in an IPv4 mapped
 * address.
 */
static void rds6_tcp_tc_info(struct socket *sock, unsigned int len,
			     struct rds_info_iterator *iter,
			     struct rds_info_lengths *lens)
{
	struct rds6_info_tcp_socket tsinfo6;
	struct rds_tcp_connection *tc;
	unsigned long flags;

	spin_lock_irqsave(&rds_tcp_tc_list_lock, flags);

	if (len / sizeof(tsinfo6) < rds6_tcp_tc_count)
		goto out;

	list_for_each_entry(tc, &rds_tcp_tc_list, t_list_item) {
		struct sock *sk = tc->t_sock->sk;
		struct inet_sock *inet = inet_sk(sk);

		tsinfo6.local_addr = sk->sk_v6_rcv_saddr;
		tsinfo6.local_port = inet->inet_sport;
		tsinfo6.peer_addr = sk->sk_v6_daddr;
		tsinfo6.peer_port = inet->inet_dport;

		tsinfo6.hdr_rem = tc->t_tinc_hdr_rem;
		tsinfo6.data_rem = tc->t_tinc_data_rem;
		tsinfo6.last_sent_nxt = tc->t_last_sent_nxt;
		tsinfo6.last_expected_una = tc->t_last_expected_una;
		tsinfo6.last_seen_una = tc->t_last_seen_una;

		rds_info_copy(iter, &tsinfo6, sizeof(tsinfo6));
	}

out:
	lens->nr = rds6_tcp_tc_count;
	lens->each = sizeof(tsinfo6);

	spin_unlock_irqrestore(&rds_tcp_tc_list_lock, flags);
}
#endif

int rds_tcp_laddr_check(struct net *net, const struct in6_addr *addr,
			__u32 scope_id)
{
	struct net_device *dev = NULL;
#if IS_ENABLED(CONFIG_IPV6)
	int ret;
#endif

	if (ipv6_addr_v4mapped(addr)) {
		if (inet_addr_type(net, addr->s6_addr32[3]) == RTN_LOCAL)
			return 0;
		return -EADDRNOTAVAIL;
	}

	/* If the scope_id is specified, check only those addresses
	 * hosted on the specified interface.
	 */
	if (scope_id != 0) {
		rcu_read_lock();
		dev = dev_get_by_index_rcu(net, scope_id);
		/* scope_id is not valid... */
		if (!dev) {
			rcu_read_unlock();
			return -EADDRNOTAVAIL;
		}
		rcu_read_unlock();
	}
#if IS_ENABLED(CONFIG_IPV6)
	ret = ipv6_chk_addr(net, addr, dev, 0);
	if (ret)
		return 0;
#endif
	return -EADDRNOTAVAIL;
}

static void rds_tcp_conn_free(void *arg)
{
	struct rds_tcp_connection *tc = arg;
	unsigned long flags;

	rdsdebug("freeing tc %p\n", tc);

	spin_lock_irqsave(&rds_tcp_conn_lock, flags);
	if (!tc->t_tcp_node_detached)
		list_del(&tc->t_tcp_node);
	spin_unlock_irqrestore(&rds_tcp_conn_lock, flags);

	kmem_cache_free(rds_tcp_conn_slab, tc);
}

static int rds_tcp_conn_alloc(struct rds_connection *conn, gfp_t gfp)
{
	struct rds_tcp_connection *tc;
	int i, j;
	int ret = 0;

	for (i = 0; i < RDS_MPATH_WORKERS; i++) {
		tc = kmem_cache_alloc(rds_tcp_conn_slab, gfp);
		if (!tc) {
			ret = -ENOMEM;
			goto fail;
		}
		mutex_init(&tc->t_conn_path_lock);
		tc->t_sock = NULL;
		tc->t_tinc = NULL;
		tc->t_tinc_hdr_rem = sizeof(struct rds_header);
		tc->t_tinc_data_rem = 0;

		conn->c_path[i].cp_transport_data = tc;
		tc->t_cpath = &conn->c_path[i];
		tc->t_tcp_node_detached = true;

		rdsdebug("rds_conn_path [%d] tc %p\n", i,
			 conn->c_path[i].cp_transport_data);
	}
	spin_lock_irq(&rds_tcp_conn_lock);
	for (i = 0; i < RDS_MPATH_WORKERS; i++) {
		tc = conn->c_path[i].cp_transport_data;
		tc->t_tcp_node_detached = false;
		list_add_tail(&tc->t_tcp_node, &rds_tcp_conn_list);
	}
	spin_unlock_irq(&rds_tcp_conn_lock);
fail:
	if (ret) {
		for (j = 0; j < i; j++)
			rds_tcp_conn_free(conn->c_path[j].cp_transport_data);
	}
	return ret;
}

static bool list_has_conn(struct list_head *list, struct rds_connection *conn)
{
	struct rds_tcp_connection *tc, *_tc;

	list_for_each_entry_safe(tc, _tc, list, t_tcp_node) {
		if (tc->t_cpath->cp_conn == conn)
			return true;
	}
	return false;
}

static void rds_tcp_set_unloading(void)
{
	atomic_set(&rds_tcp_unloading, 1);
}

static bool rds_tcp_is_unloading(struct rds_connection *conn)
{
	return atomic_read(&rds_tcp_unloading) != 0;
}

static void rds_tcp_destroy_conns(void)
{
	struct rds_tcp_connection *tc, *_tc;
	LIST_HEAD(tmp_list);

	/* avoid calling conn_destroy with irqs off */
	spin_lock_irq(&rds_tcp_conn_lock);
	list_for_each_entry_safe(tc, _tc, &rds_tcp_conn_list, t_tcp_node) {
		if (!list_has_conn(&tmp_list, tc->t_cpath->cp_conn))
			list_move_tail(&tc->t_tcp_node, &tmp_list);
	}
	spin_unlock_irq(&rds_tcp_conn_lock);

	list_for_each_entry_safe(tc, _tc, &tmp_list, t_tcp_node)
		rds_conn_destroy(tc->t_cpath->cp_conn);
}

static void rds_tcp_exit(void);

static u8 rds_tcp_get_tos_map(u8 tos)
{
	/* all user tos mapped to default 0 for TCP transport */
	return 0;
}

struct rds_transport rds_tcp_transport = {
	.laddr_check		= rds_tcp_laddr_check,
	.xmit_path_prepare	= rds_tcp_xmit_path_prepare,
	.xmit_path_complete	= rds_tcp_xmit_path_complete,
	.xmit			= rds_tcp_xmit,
	.recv_path		= rds_tcp_recv_path,
	.conn_alloc		= rds_tcp_conn_alloc,
	.conn_free		= rds_tcp_conn_free,
	.conn_path_connect	= rds_tcp_conn_path_connect,
	.conn_path_shutdown	= rds_tcp_conn_path_shutdown,
	.inc_copy_to_user	= rds_tcp_inc_copy_to_user,
	.inc_free		= rds_tcp_inc_free,
	.stats_info_copy	= rds_tcp_stats_info_copy,
	.exit			= rds_tcp_exit,
	.get_tos_map		= rds_tcp_get_tos_map,
	.t_owner		= THIS_MODULE,
	.t_name			= "tcp",
	.t_type			= RDS_TRANS_TCP,
	.t_prefer_loopback	= 1,
	.t_mp_capable		= 1,
	.t_unloading		= rds_tcp_is_unloading,
};

static unsigned int rds_tcp_netid;

/* per-network namespace private data for this module */
struct rds_tcp_net {
	struct socket *rds_tcp_listen_sock;
	struct work_struct rds_tcp_accept_w;
	struct ctl_table_header *rds_tcp_sysctl;
	struct ctl_table *ctl_table;
	int sndbuf_size;
	int rcvbuf_size;
};

/* All module specific customizations to the RDS-TCP socket should be done in
 * rds_tcp_tune() and applied after socket creation.
 */
bool rds_tcp_tune(struct socket *sock)
{
	struct sock *sk = sock->sk;
	struct net *net = sock_net(sk);
	struct rds_tcp_net *rtn;

	tcp_sock_set_nodelay(sock->sk);
	lock_sock(sk);
	/* TCP timer functions might access net namespace even after
	 * a process which created this net namespace terminated.
	 */
	if (!sk->sk_net_refcnt) {
		if (!maybe_get_net(net)) {
			release_sock(sk);
			return false;
		}
		sk_net_refcnt_upgrade(sk);
		put_net(net);
	}
	rtn = net_generic(net, rds_tcp_netid);
	if (rtn->sndbuf_size > 0) {
		sk->sk_sndbuf = rtn->sndbuf_size;
		sk->sk_userlocks |= SOCK_SNDBUF_LOCK;
	}
	if (rtn->rcvbuf_size > 0) {
		sk->sk_rcvbuf = rtn->rcvbuf_size;
		sk->sk_userlocks |= SOCK_RCVBUF_LOCK;
	}
	release_sock(sk);
	return true;
}

static void rds_tcp_accept_worker(struct work_struct *work)
{
	struct rds_tcp_net *rtn = container_of(work,
					       struct rds_tcp_net,
					       rds_tcp_accept_w);

	while (rds_tcp_accept_one(rtn->rds_tcp_listen_sock) == 0)
		cond_resched();
}

void rds_tcp_accept_work(struct sock *sk)
{
	struct net *net = sock_net(sk);
	struct rds_tcp_net *rtn = net_generic(net, rds_tcp_netid);

	queue_work(rds_wq, &rtn->rds_tcp_accept_w);
}

static __net_init int rds_tcp_init_net(struct net *net)
{
	struct rds_tcp_net *rtn = net_generic(net, rds_tcp_netid);
	struct ctl_table *tbl;
	int err = 0;

	memset(rtn, 0, sizeof(*rtn));

	/* {snd, rcv}buf_size default to 0, which implies we let the
	 * stack pick the value, and permit auto-tuning of buffer size.
	 */
	if (net == &init_net) {
		tbl = rds_tcp_sysctl_table;
	} else {
		tbl = kmemdup(rds_tcp_sysctl_table,
			      sizeof(rds_tcp_sysctl_table), GFP_KERNEL);
		if (!tbl) {
			pr_warn("could not set allocate sysctl table\n");
			return -ENOMEM;
		}
		rtn->ctl_table = tbl;
	}
	tbl[RDS_TCP_SNDBUF].data = &rtn->sndbuf_size;
	tbl[RDS_TCP_RCVBUF].data = &rtn->rcvbuf_size;
	rtn->rds_tcp_sysctl = register_net_sysctl_sz(net, "net/rds/tcp", tbl,
						     ARRAY_SIZE(rds_tcp_sysctl_table));
	if (!rtn->rds_tcp_sysctl) {
		pr_warn("could not register sysctl\n");
		err = -ENOMEM;
		goto fail;
	}

#if IS_ENABLED(CONFIG_IPV6)
	rtn->rds_tcp_listen_sock = rds_tcp_listen_init(net, true);
#else
	rtn->rds_tcp_listen_sock = rds_tcp_listen_init(net, false);
#endif
	if (!rtn->rds_tcp_listen_sock) {
		pr_warn("could not set up IPv6 listen sock\n");

#if IS_ENABLED(CONFIG_IPV6)
		/* Try IPv4 as some systems disable IPv6 */
		rtn->rds_tcp_listen_sock = rds_tcp_listen_init(net, false);
		if (!rtn->rds_tcp_listen_sock) {
#endif
			unregister_net_sysctl_table(rtn->rds_tcp_sysctl);
			rtn->rds_tcp_sysctl = NULL;
			err = -EAFNOSUPPORT;
			goto fail;
#if IS_ENABLED(CONFIG_IPV6)
		}
#endif
	}
	INIT_WORK(&rtn->rds_tcp_accept_w, rds_tcp_accept_worker);
	return 0;

fail:
	if (net != &init_net)
		kfree(tbl);
	return err;
}

static void rds_tcp_kill_sock(struct net *net)
{
	struct rds_tcp_connection *tc, *_tc;
	LIST_HEAD(tmp_list);
	struct rds_tcp_net *rtn = net_generic(net, rds_tcp_netid);
	struct socket *lsock = rtn->rds_tcp_listen_sock;

	rtn->rds_tcp_listen_sock = NULL;
	rds_tcp_listen_stop(lsock, &rtn->rds_tcp_accept_w);
	spin_lock_irq(&rds_tcp_conn_lock);
	list_for_each_entry_safe(tc, _tc, &rds_tcp_conn_list, t_tcp_node) {
		struct net *c_net = read_pnet(&tc->t_cpath->cp_conn->c_net);

		if (net != c_net)
			continue;
		if (!list_has_conn(&tmp_list, tc->t_cpath->cp_conn)) {
			list_move_tail(&tc->t_tcp_node, &tmp_list);
		} else {
			list_del(&tc->t_tcp_node);
			tc->t_tcp_node_detached = true;
		}
	}
	spin_unlock_irq(&rds_tcp_conn_lock);
	list_for_each_entry_safe(tc, _tc, &tmp_list, t_tcp_node)
		rds_conn_destroy(tc->t_cpath->cp_conn);
}

static void __net_exit rds_tcp_exit_net(struct net *net)
{
	struct rds_tcp_net *rtn = net_generic(net, rds_tcp_netid);

	rds_tcp_kill_sock(net);

	if (rtn->rds_tcp_sysctl)
		unregister_net_sysctl_table(rtn->rds_tcp_sysctl);

	if (net != &init_net)
		kfree(rtn->ctl_table);
}

static struct pernet_operations rds_tcp_net_ops = {
	.init = rds_tcp_init_net,
	.exit = rds_tcp_exit_net,
	.id = &rds_tcp_netid,
	.size = sizeof(struct rds_tcp_net),
};

void *rds_tcp_listen_sock_def_readable(struct net *net)
{
	struct rds_tcp_net *rtn = net_generic(net, rds_tcp_netid);
	struct socket *lsock = rtn->rds_tcp_listen_sock;

	if (!lsock)
		return NULL;

	return lsock->sk->sk_user_data;
}

/* when sysctl is used to modify some kernel socket parameters,this
 * function  resets the RDS connections in that netns  so that we can
 * restart with new parameters.  The assumption is that such reset
 * events are few and far-between.
 */
static void rds_tcp_sysctl_reset(struct net *net)
{
	struct rds_tcp_connection *tc, *_tc;

	spin_lock_irq(&rds_tcp_conn_lock);
	list_for_each_entry_safe(tc, _tc, &rds_tcp_conn_list, t_tcp_node) {
		struct net *c_net = read_pnet(&tc->t_cpath->cp_conn->c_net);

		if (net != c_net || !tc->t_sock)
			continue;

		/* reconnect with new parameters */
		rds_conn_path_drop(tc->t_cpath, false);
	}
	spin_unlock_irq(&rds_tcp_conn_lock);
}

static int rds_tcp_skbuf_handler(struct rds_tcp_net *rtn,
				 const struct ctl_table *ctl, int write,
				 void *buffer, size_t *lenp, loff_t *fpos)
{
	int err;

	err = proc_dointvec_minmax(ctl, write, buffer, lenp, fpos);
	if (err < 0) {
		pr_warn("Invalid input. Must be >= %d\n",
			*(int *)(ctl->extra1));
		return err;
	}

	if (write && rtn->rds_tcp_listen_sock && rtn->rds_tcp_listen_sock->sk) {
		struct net *net = sock_net(rtn->rds_tcp_listen_sock->sk);

		rds_tcp_sysctl_reset(net);
	}

	return 0;
}

static int rds_tcp_sndbuf_handler(const struct ctl_table *ctl, int write,
				  void *buffer, size_t *lenp, loff_t *fpos)
{
	struct rds_tcp_net *rtn = container_of(ctl->data, struct rds_tcp_net,
					       sndbuf_size);

	return rds_tcp_skbuf_handler(rtn, ctl, write, buffer, lenp, fpos);
}

static int rds_tcp_rcvbuf_handler(const struct ctl_table *ctl, int write,
				  void *buffer, size_t *lenp, loff_t *fpos)
{
	struct rds_tcp_net *rtn = container_of(ctl->data, struct rds_tcp_net,
					       rcvbuf_size);

	return rds_tcp_skbuf_handler(rtn, ctl, write, buffer, lenp, fpos);
}

static void rds_tcp_exit(void)
{
	rds_tcp_set_unloading();
	synchronize_rcu();
	rds_info_deregister_func(RDS_INFO_TCP_SOCKETS, rds_tcp_tc_info);
#if IS_ENABLED(CONFIG_IPV6)
	rds_info_deregister_func(RDS6_INFO_TCP_SOCKETS, rds6_tcp_tc_info);
#endif
	unregister_pernet_device(&rds_tcp_net_ops);
	rds_tcp_destroy_conns();
	rds_trans_unregister(&rds_tcp_transport);
	rds_tcp_recv_exit();
	kmem_cache_destroy(rds_tcp_conn_slab);
}
module_exit(rds_tcp_exit);

static int __init rds_tcp_init(void)
{
	int ret;

	rds_tcp_conn_slab = KMEM_CACHE(rds_tcp_connection, 0);
	if (!rds_tcp_conn_slab) {
		ret = -ENOMEM;
		goto out;
	}

	ret = rds_tcp_recv_init();
	if (ret)
		goto out_slab;

	ret = register_pernet_device(&rds_tcp_net_ops);
	if (ret)
		goto out_recv;

	rds_trans_register(&rds_tcp_transport);

	rds_info_register_func(RDS_INFO_TCP_SOCKETS, rds_tcp_tc_info);
#if IS_ENABLED(CONFIG_IPV6)
	rds_info_register_func(RDS6_INFO_TCP_SOCKETS, rds6_tcp_tc_info);
#endif

	goto out;
out_recv:
	rds_tcp_recv_exit();
out_slab:
	kmem_cache_destroy(rds_tcp_conn_slab);
out:
	return ret;
}
module_init(rds_tcp_init);

MODULE_AUTHOR("Oracle Corporation <rds-devel@oss.oracle.com>");
MODULE_DESCRIPTION("RDS: TCP transport");
MODULE_LICENSE("Dual BSD/GPL");
