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
#include <net/tcp.h>

#include "rds.h"
#include "tcp.h"

/* only for info exporting */
static DEFINE_SPINLOCK(rds_tcp_tc_list_lock);
static LIST_HEAD(rds_tcp_tc_list);
unsigned int rds_tcp_tc_count;

/* Track rds_tcp_connection structs so they can be cleaned up */
static DEFINE_SPINLOCK(rds_tcp_conn_lock);
static LIST_HEAD(rds_tcp_conn_list);

static struct kmem_cache *rds_tcp_conn_slab;

#define RDS_TCP_DEFAULT_BUFSIZE (128 * 1024)

/* doing it this way avoids calling tcp_sk() */
void rds_tcp_nonagle(struct socket *sock)
{
	mm_segment_t oldfs = get_fs();
	int val = 1;

	set_fs(KERNEL_DS);
	sock->ops->setsockopt(sock, SOL_TCP, TCP_NODELAY, (char __user *)&val,
			      sizeof(val));
	set_fs(oldfs);
}

void rds_tcp_tune(struct socket *sock)
{
	struct sock *sk = sock->sk;

	rds_tcp_nonagle(sock);

	/*
	 * We're trying to saturate gigabit with the default,
	 * see svc_sock_setbufsize().
	 */
	lock_sock(sk);
	sk->sk_sndbuf = RDS_TCP_DEFAULT_BUFSIZE;
	sk->sk_rcvbuf = RDS_TCP_DEFAULT_BUFSIZE;
	sk->sk_userlocks |= SOCK_SNDBUF_LOCK|SOCK_RCVBUF_LOCK;
	release_sock(sk);
}

u32 rds_tcp_snd_nxt(struct rds_tcp_connection *tc)
{
	return tcp_sk(tc->t_sock->sk)->snd_nxt;
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
 * This is the only path that sets tc->t_sock.  Send and receive trust that
 * it is set.  The RDS_CONN_CONNECTED bit protects those paths from being
 * called while it isn't set.
 */
void rds_tcp_set_callbacks(struct socket *sock, struct rds_connection *conn)
{
	struct rds_tcp_connection *tc = conn->c_transport_data;

	rdsdebug("setting sock %p callbacks to tc %p\n", sock, tc);
	write_lock_bh(&sock->sk->sk_callback_lock);

	/* done under the callback_lock to serialize with write_space */
	spin_lock(&rds_tcp_tc_list_lock);
	list_add_tail(&tc->t_list_item, &rds_tcp_tc_list);
	rds_tcp_tc_count++;
	spin_unlock(&rds_tcp_tc_list_lock);

	/* accepted sockets need our listen data ready undone */
	if (sock->sk->sk_data_ready == rds_tcp_listen_data_ready)
		sock->sk->sk_data_ready = sock->sk->sk_user_data;

	tc->t_sock = sock;
	tc->conn = conn;
	tc->t_orig_data_ready = sock->sk->sk_data_ready;
	tc->t_orig_write_space = sock->sk->sk_write_space;
	tc->t_orig_state_change = sock->sk->sk_state_change;

	sock->sk->sk_user_data = conn;
	sock->sk->sk_data_ready = rds_tcp_data_ready;
	sock->sk->sk_write_space = rds_tcp_write_space;
	sock->sk->sk_state_change = rds_tcp_state_change;

	write_unlock_bh(&sock->sk->sk_callback_lock);
}

static void rds_tcp_tc_info(struct socket *sock, unsigned int len,
			    struct rds_info_iterator *iter,
			    struct rds_info_lengths *lens)
{
	struct rds_info_tcp_socket tsinfo;
	struct rds_tcp_connection *tc;
	unsigned long flags;
	struct sockaddr_in sin;
	int sinlen;

	spin_lock_irqsave(&rds_tcp_tc_list_lock, flags);

	if (len / sizeof(tsinfo) < rds_tcp_tc_count)
		goto out;

	list_for_each_entry(tc, &rds_tcp_tc_list, t_list_item) {

		sock->ops->getname(sock, (struct sockaddr *)&sin, &sinlen, 0);
		tsinfo.local_addr = sin.sin_addr.s_addr;
		tsinfo.local_port = sin.sin_port;
		sock->ops->getname(sock, (struct sockaddr *)&sin, &sinlen, 1);
		tsinfo.peer_addr = sin.sin_addr.s_addr;
		tsinfo.peer_port = sin.sin_port;

		tsinfo.hdr_rem = tc->t_tinc_hdr_rem;
		tsinfo.data_rem = tc->t_tinc_data_rem;
		tsinfo.last_sent_nxt = tc->t_last_sent_nxt;
		tsinfo.last_expected_una = tc->t_last_expected_una;
		tsinfo.last_seen_una = tc->t_last_seen_una;

		rds_info_copy(iter, &tsinfo, sizeof(tsinfo));
	}

out:
	lens->nr = rds_tcp_tc_count;
	lens->each = sizeof(tsinfo);

	spin_unlock_irqrestore(&rds_tcp_tc_list_lock, flags);
}

static int rds_tcp_laddr_check(__be32 addr)
{
	if (inet_addr_type(&init_net, addr) == RTN_LOCAL)
		return 0;
	return -EADDRNOTAVAIL;
}

static int rds_tcp_conn_alloc(struct rds_connection *conn, gfp_t gfp)
{
	struct rds_tcp_connection *tc;

	tc = kmem_cache_alloc(rds_tcp_conn_slab, gfp);
	if (tc == NULL)
		return -ENOMEM;

	tc->t_sock = NULL;
	tc->t_tinc = NULL;
	tc->t_tinc_hdr_rem = sizeof(struct rds_header);
	tc->t_tinc_data_rem = 0;

	conn->c_transport_data = tc;

	spin_lock_irq(&rds_tcp_conn_lock);
	list_add_tail(&tc->t_tcp_node, &rds_tcp_conn_list);
	spin_unlock_irq(&rds_tcp_conn_lock);

	rdsdebug("alloced tc %p\n", conn->c_transport_data);
	return 0;
}

static void rds_tcp_conn_free(void *arg)
{
	struct rds_tcp_connection *tc = arg;
	rdsdebug("freeing tc %p\n", tc);
	kmem_cache_free(rds_tcp_conn_slab, tc);
}

static void rds_tcp_destroy_conns(void)
{
	struct rds_tcp_connection *tc, *_tc;
	LIST_HEAD(tmp_list);

	/* avoid calling conn_destroy with irqs off */
	spin_lock_irq(&rds_tcp_conn_lock);
	list_splice(&rds_tcp_conn_list, &tmp_list);
	INIT_LIST_HEAD(&rds_tcp_conn_list);
	spin_unlock_irq(&rds_tcp_conn_lock);

	list_for_each_entry_safe(tc, _tc, &tmp_list, t_tcp_node) {
		if (tc->conn->c_passive)
			rds_conn_destroy(tc->conn->c_passive);
		rds_conn_destroy(tc->conn);
	}
}

void rds_tcp_exit(void)
{
	rds_info_deregister_func(RDS_INFO_TCP_SOCKETS, rds_tcp_tc_info);
	rds_tcp_listen_stop();
	rds_tcp_destroy_conns();
	rds_trans_unregister(&rds_tcp_transport);
	rds_tcp_recv_exit();
	kmem_cache_destroy(rds_tcp_conn_slab);
}
module_exit(rds_tcp_exit);

struct rds_transport rds_tcp_transport = {
	.laddr_check		= rds_tcp_laddr_check,
	.xmit_prepare		= rds_tcp_xmit_prepare,
	.xmit_complete		= rds_tcp_xmit_complete,
	.xmit_cong_map		= rds_tcp_xmit_cong_map,
	.xmit			= rds_tcp_xmit,
	.recv			= rds_tcp_recv,
	.conn_alloc		= rds_tcp_conn_alloc,
	.conn_free		= rds_tcp_conn_free,
	.conn_connect		= rds_tcp_conn_connect,
	.conn_shutdown		= rds_tcp_conn_shutdown,
	.inc_copy_to_user	= rds_tcp_inc_copy_to_user,
	.inc_purge		= rds_tcp_inc_purge,
	.inc_free		= rds_tcp_inc_free,
	.stats_info_copy	= rds_tcp_stats_info_copy,
	.exit			= rds_tcp_exit,
	.t_owner		= THIS_MODULE,
	.t_name			= "tcp",
	.t_type			= RDS_TRANS_TCP,
	.t_prefer_loopback	= 1,
};

int __init rds_tcp_init(void)
{
	int ret;

	rds_tcp_conn_slab = kmem_cache_create("rds_tcp_connection",
					      sizeof(struct rds_tcp_connection),
					      0, 0, NULL);
	if (rds_tcp_conn_slab == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	ret = rds_tcp_recv_init();
	if (ret)
		goto out_slab;

	ret = rds_trans_register(&rds_tcp_transport);
	if (ret)
		goto out_recv;

	ret = rds_tcp_listen_init();
	if (ret)
		goto out_register;

	rds_info_register_func(RDS_INFO_TCP_SOCKETS, rds_tcp_tc_info);

	goto out;

out_register:
	rds_trans_unregister(&rds_tcp_transport);
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

