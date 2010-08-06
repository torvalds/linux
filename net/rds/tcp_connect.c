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
#include <net/tcp.h>

#include "rds.h"
#include "tcp.h"

void rds_tcp_state_change(struct sock *sk)
{
	void (*state_change)(struct sock *sk);
	struct rds_connection *conn;
	struct rds_tcp_connection *tc;

	read_lock(&sk->sk_callback_lock);
	conn = sk->sk_user_data;
	if (conn == NULL) {
		state_change = sk->sk_state_change;
		goto out;
	}
	tc = conn->c_transport_data;
	state_change = tc->t_orig_state_change;

	rdsdebug("sock %p state_change to %d\n", tc->t_sock, sk->sk_state);

	switch(sk->sk_state) {
		/* ignore connecting sockets as they make progress */
		case TCP_SYN_SENT:
		case TCP_SYN_RECV:
			break;
		case TCP_ESTABLISHED:
			rds_connect_complete(conn);
			break;
		case TCP_CLOSE:
			rds_conn_drop(conn);
		default:
			break;
	}
out:
	read_unlock(&sk->sk_callback_lock);
	state_change(sk);
}

int rds_tcp_conn_connect(struct rds_connection *conn)
{
	struct socket *sock = NULL;
	struct sockaddr_in src, dest;
	int ret;

	ret = sock_create(PF_INET, SOCK_STREAM, IPPROTO_TCP, &sock);
	if (ret < 0)
		goto out;

	rds_tcp_tune(sock);

	src.sin_family = AF_INET;
	src.sin_addr.s_addr = (__force u32)conn->c_laddr;
	src.sin_port = (__force u16)htons(0);

	ret = sock->ops->bind(sock, (struct sockaddr *)&src, sizeof(src));
	if (ret) {
		rdsdebug("bind failed with %d at address %pI4\n",
			 ret, &conn->c_laddr);
		goto out;
	}

	dest.sin_family = AF_INET;
	dest.sin_addr.s_addr = (__force u32)conn->c_faddr;
	dest.sin_port = (__force u16)htons(RDS_TCP_PORT);

	/*
	 * once we call connect() we can start getting callbacks and they
	 * own the socket
	 */
	rds_tcp_set_callbacks(sock, conn);
	ret = sock->ops->connect(sock, (struct sockaddr *)&dest, sizeof(dest),
				 O_NONBLOCK);
	sock = NULL;

	rdsdebug("connect to address %pI4 returned %d\n", &conn->c_faddr, ret);
	if (ret == -EINPROGRESS)
		ret = 0;

out:
	if (sock)
		sock_release(sock);
	return ret;
}

/*
 * Before killing the tcp socket this needs to serialize with callbacks.  The
 * caller has already grabbed the sending sem so we're serialized with other
 * senders.
 *
 * TCP calls the callbacks with the sock lock so we hold it while we reset the
 * callbacks to those set by TCP.  Our callbacks won't execute again once we
 * hold the sock lock.
 */
void rds_tcp_conn_shutdown(struct rds_connection *conn)
{
	struct rds_tcp_connection *tc = conn->c_transport_data;
	struct socket *sock = tc->t_sock;

	rdsdebug("shutting down conn %p tc %p sock %p\n", conn, tc, sock);

	if (sock) {
		sock->ops->shutdown(sock, RCV_SHUTDOWN | SEND_SHUTDOWN);
		lock_sock(sock->sk);
		rds_tcp_restore_callbacks(sock, tc); /* tc->tc_sock = NULL */

		release_sock(sock->sk);
		sock_release(sock);
	}

	if (tc->t_tinc) {
		rds_inc_put(&tc->t_tinc->ti_inc);
		tc->t_tinc = NULL;
	}
	tc->t_tinc_hdr_rem = sizeof(struct rds_header);
	tc->t_tinc_data_rem = 0;
}
