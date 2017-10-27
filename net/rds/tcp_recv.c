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
#include <net/tcp.h>

#include "rds.h"
#include "tcp.h"

static struct kmem_cache *rds_tcp_incoming_slab;

static void rds_tcp_inc_purge(struct rds_incoming *inc)
{
	struct rds_tcp_incoming *tinc;
	tinc = container_of(inc, struct rds_tcp_incoming, ti_inc);
	rdsdebug("purging tinc %p inc %p\n", tinc, inc);
	skb_queue_purge(&tinc->ti_skb_list);
}

void rds_tcp_inc_free(struct rds_incoming *inc)
{
	struct rds_tcp_incoming *tinc;
	tinc = container_of(inc, struct rds_tcp_incoming, ti_inc);
	rds_tcp_inc_purge(inc);
	rdsdebug("freeing tinc %p inc %p\n", tinc, inc);
	kmem_cache_free(rds_tcp_incoming_slab, tinc);
}

/*
 * this is pretty lame, but, whatever.
 */
int rds_tcp_inc_copy_to_user(struct rds_incoming *inc, struct iov_iter *to)
{
	struct rds_tcp_incoming *tinc;
	struct sk_buff *skb;
	int ret = 0;

	if (!iov_iter_count(to))
		goto out;

	tinc = container_of(inc, struct rds_tcp_incoming, ti_inc);

	skb_queue_walk(&tinc->ti_skb_list, skb) {
		unsigned long to_copy, skb_off;
		for (skb_off = 0; skb_off < skb->len; skb_off += to_copy) {
			to_copy = iov_iter_count(to);
			to_copy = min(to_copy, skb->len - skb_off);

			if (skb_copy_datagram_iter(skb, skb_off, to, to_copy))
				return -EFAULT;

			rds_stats_add(s_copy_to_user, to_copy);
			ret += to_copy;

			if (!iov_iter_count(to))
				goto out;
		}
	}
out:
	return ret;
}

/*
 * We have a series of skbs that have fragmented pieces of the congestion
 * bitmap.  They must add up to the exact size of the congestion bitmap.  We
 * use the skb helpers to copy those into the pages that make up the in-memory
 * congestion bitmap for the remote address of this connection.  We then tell
 * the congestion core that the bitmap has been changed so that it can wake up
 * sleepers.
 *
 * This is racing with sending paths which are using test_bit to see if the
 * bitmap indicates that their recipient is congested.
 */

static void rds_tcp_cong_recv(struct rds_connection *conn,
			      struct rds_tcp_incoming *tinc)
{
	struct sk_buff *skb;
	unsigned int to_copy, skb_off;
	unsigned int map_off;
	unsigned int map_page;
	struct rds_cong_map *map;
	int ret;

	/* catch completely corrupt packets */
	if (be32_to_cpu(tinc->ti_inc.i_hdr.h_len) != RDS_CONG_MAP_BYTES)
		return;

	map_page = 0;
	map_off = 0;
	map = conn->c_fcong;

	skb_queue_walk(&tinc->ti_skb_list, skb) {
		skb_off = 0;
		while (skb_off < skb->len) {
			to_copy = min_t(unsigned int, PAGE_SIZE - map_off,
					skb->len - skb_off);

			BUG_ON(map_page >= RDS_CONG_MAP_PAGES);

			/* only returns 0 or -error */
			ret = skb_copy_bits(skb, skb_off,
				(void *)map->m_page_addrs[map_page] + map_off,
				to_copy);
			BUG_ON(ret != 0);

			skb_off += to_copy;
			map_off += to_copy;
			if (map_off == PAGE_SIZE) {
				map_off = 0;
				map_page++;
			}
		}
	}

	rds_cong_map_updated(map, ~(u64) 0);
}

struct rds_tcp_desc_arg {
	struct rds_conn_path *conn_path;
	gfp_t gfp;
};

static int rds_tcp_data_recv(read_descriptor_t *desc, struct sk_buff *skb,
			     unsigned int offset, size_t len)
{
	struct rds_tcp_desc_arg *arg = desc->arg.data;
	struct rds_conn_path *cp = arg->conn_path;
	struct rds_tcp_connection *tc = cp->cp_transport_data;
	struct rds_tcp_incoming *tinc = tc->t_tinc;
	struct sk_buff *clone;
	size_t left = len, to_copy;

	rdsdebug("tcp data tc %p skb %p offset %u len %zu\n", tc, skb, offset,
		 len);

	/*
	 * tcp_read_sock() interprets partial progress as an indication to stop
	 * processing.
	 */
	while (left) {
		if (!tinc) {
			tinc = kmem_cache_alloc(rds_tcp_incoming_slab,
						arg->gfp);
			if (!tinc) {
				desc->error = -ENOMEM;
				goto out;
			}
			tc->t_tinc = tinc;
			rdsdebug("alloced tinc %p\n", tinc);
			rds_inc_path_init(&tinc->ti_inc, cp,
					  cp->cp_conn->c_faddr);
			tinc->ti_inc.i_rx_lat_trace[RDS_MSG_RX_HDR] =
					local_clock();

			/*
			 * XXX * we might be able to use the __ variants when
			 * we've already serialized at a higher level.
			 */
			skb_queue_head_init(&tinc->ti_skb_list);
		}

		if (left && tc->t_tinc_hdr_rem) {
			to_copy = min(tc->t_tinc_hdr_rem, left);
			rdsdebug("copying %zu header from skb %p\n", to_copy,
				 skb);
			skb_copy_bits(skb, offset,
				      (char *)&tinc->ti_inc.i_hdr +
						sizeof(struct rds_header) -
						tc->t_tinc_hdr_rem,
				      to_copy);
			tc->t_tinc_hdr_rem -= to_copy;
			left -= to_copy;
			offset += to_copy;

			if (tc->t_tinc_hdr_rem == 0) {
				/* could be 0 for a 0 len message */
				tc->t_tinc_data_rem =
					be32_to_cpu(tinc->ti_inc.i_hdr.h_len);
				tinc->ti_inc.i_rx_lat_trace[RDS_MSG_RX_START] =
					local_clock();
			}
		}

		if (left && tc->t_tinc_data_rem) {
			to_copy = min(tc->t_tinc_data_rem, left);

			clone = pskb_extract(skb, offset, to_copy, arg->gfp);
			if (!clone) {
				desc->error = -ENOMEM;
				goto out;
			}

			skb_queue_tail(&tinc->ti_skb_list, clone);

			rdsdebug("skb %p data %p len %d off %u to_copy %zu -> "
				 "clone %p data %p len %d\n",
				 skb, skb->data, skb->len, offset, to_copy,
				 clone, clone->data, clone->len);

			tc->t_tinc_data_rem -= to_copy;
			left -= to_copy;
			offset += to_copy;
		}

		if (tc->t_tinc_hdr_rem == 0 && tc->t_tinc_data_rem == 0) {
			struct rds_connection *conn = cp->cp_conn;

			if (tinc->ti_inc.i_hdr.h_flags == RDS_FLAG_CONG_BITMAP)
				rds_tcp_cong_recv(conn, tinc);
			else
				rds_recv_incoming(conn, conn->c_faddr,
						  conn->c_laddr, &tinc->ti_inc,
						  arg->gfp);

			tc->t_tinc_hdr_rem = sizeof(struct rds_header);
			tc->t_tinc_data_rem = 0;
			tc->t_tinc = NULL;
			rds_inc_put(&tinc->ti_inc);
			tinc = NULL;
		}
	}
out:
	rdsdebug("returning len %zu left %zu skb len %d rx queue depth %d\n",
		 len, left, skb->len,
		 skb_queue_len(&tc->t_sock->sk->sk_receive_queue));
	return len - left;
}

/* the caller has to hold the sock lock */
static int rds_tcp_read_sock(struct rds_conn_path *cp, gfp_t gfp)
{
	struct rds_tcp_connection *tc = cp->cp_transport_data;
	struct socket *sock = tc->t_sock;
	read_descriptor_t desc;
	struct rds_tcp_desc_arg arg;

	/* It's like glib in the kernel! */
	arg.conn_path = cp;
	arg.gfp = gfp;
	desc.arg.data = &arg;
	desc.error = 0;
	desc.count = 1; /* give more than one skb per call */

	tcp_read_sock(sock->sk, &desc, rds_tcp_data_recv);
	rdsdebug("tcp_read_sock for tc %p gfp 0x%x returned %d\n", tc, gfp,
		 desc.error);

	return desc.error;
}

/*
 * We hold the sock lock to serialize our rds_tcp_recv->tcp_read_sock from
 * data_ready.
 *
 * if we fail to allocate we're in trouble.. blindly wait some time before
 * trying again to see if the VM can free up something for us.
 */
int rds_tcp_recv_path(struct rds_conn_path *cp)
{
	struct rds_tcp_connection *tc = cp->cp_transport_data;
	struct socket *sock = tc->t_sock;
	int ret = 0;

	rdsdebug("recv worker path [%d] tc %p sock %p\n",
		 cp->cp_index, tc, sock);

	lock_sock(sock->sk);
	ret = rds_tcp_read_sock(cp, GFP_KERNEL);
	release_sock(sock->sk);

	return ret;
}

void rds_tcp_data_ready(struct sock *sk)
{
	void (*ready)(struct sock *sk);
	struct rds_conn_path *cp;
	struct rds_tcp_connection *tc;

	rdsdebug("data ready sk %p\n", sk);

	read_lock_bh(&sk->sk_callback_lock);
	cp = sk->sk_user_data;
	if (!cp) { /* check for teardown race */
		ready = sk->sk_data_ready;
		goto out;
	}

	tc = cp->cp_transport_data;
	ready = tc->t_orig_data_ready;
	rds_tcp_stats_inc(s_tcp_data_ready_calls);

	if (rds_tcp_read_sock(cp, GFP_ATOMIC) == -ENOMEM)
		queue_delayed_work(rds_wq, &cp->cp_recv_w, 0);
out:
	read_unlock_bh(&sk->sk_callback_lock);
	ready(sk);
}

int rds_tcp_recv_init(void)
{
	rds_tcp_incoming_slab = kmem_cache_create("rds_tcp_incoming",
					sizeof(struct rds_tcp_incoming),
					0, 0, NULL);
	if (!rds_tcp_incoming_slab)
		return -ENOMEM;
	return 0;
}

void rds_tcp_recv_exit(void)
{
	kmem_cache_destroy(rds_tcp_incoming_slab);
}
