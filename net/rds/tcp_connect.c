/*
 * Copyright (c) 2006, 2017 Oracle and/or its affiliates. All rights reserved.
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
	struct rds_conn_path *cp;
	struct rds_tcp_connection *tc;

	read_lock_bh(&sk->sk_callback_lock);
	cp = sk->sk_user_data;
	if (!cp) {
		state_change = sk->sk_state_change;
		goto out;
	}
	tc = cp->cp_transport_data;
	state_change = tc->t_orig_state_change;

	rdsdebug("sock %p state_change to %d\n", tc->t_sock, sk->sk_state);

	switch (sk->sk_state) {
	/* ignore connecting sockets as they make progress */
	case TCP_SYN_SENT:
	case TCP_SYN_RECV:
		break;
	case TCP_ESTABLISHED:
		/* Force the peer to reconnect so that we have the
		 * TCP ports going from <smaller-ip>.<transient> to
		 * <larger-ip>.<RDS_TCP_PORT>. We avoid marking the
		 * RDS connection as RDS_CONN_UP until the reconnect,
		 * to avoid RDS datagram loss.
		 */
		if (rds_addr_cmp(&cp->cp_conn->c_laddr,
				 &cp->cp_conn->c_faddr) >= 0 &&
		    rds_conn_path_transition(cp, RDS_CONN_CONNECTING,
					     RDS_CONN_ERROR)) {
			rds_conn_path_drop(cp, false);
		} else {
			rds_connect_path_complete(cp, RDS_CONN_CONNECTING);
		}
		break;
	case TCP_CLOSE_WAIT:
	case TCP_CLOSE:
		rds_conn_path_drop(cp, false);
	default:
		break;
	}
out:
	read_unlock_bh(&sk->sk_callback_lock);
	state_change(sk);
}

int rds_tcp_conn_path_connect(struct rds_conn_path *cp)
{
	struct socket *sock = NULL;
	struct sockaddr_in6 sin6;
	struct sockaddr_in sin;
	struct sockaddr *addr;
	int addrlen;
	bool isv6;
	int ret;
	struct rds_connection *conn = cp->cp_conn;
	struct rds_tcp_connection *tc = cp->cp_transport_data;

	/* for multipath rds,we only trigger the connection after
	 * the handshake probe has determined the number of paths.
	 */
	if (cp->cp_index > 0 && cp->cp_conn->c_npaths < 2)
		return -EAGAIN;

	mutex_lock(&tc->t_conn_path_lock);

	if (rds_conn_path_up(cp)) {
		mutex_unlock(&tc->t_conn_path_lock);
		return 0;
	}
	if (ipv6_addr_v4mapped(&conn->c_laddr)) {
		ret = sock_create_kern(rds_conn_net(conn), PF_INET,
				       SOCK_STREAM, IPPROTO_TCP, &sock);
		isv6 = false;
	} else {
		ret = sock_create_kern(rds_conn_net(conn), PF_INET6,
				       SOCK_STREAM, IPPROTO_TCP, &sock);
		isv6 = true;
	}

	if (ret < 0)
		goto out;

	rds_tcp_tune(sock);

	if (isv6) {
		sin6.sin6_family = AF_INET6;
		sin6.sin6_addr = conn->c_laddr;
		sin6.sin6_port = 0;
		sin6.sin6_flowinfo = 0;
		sin6.sin6_scope_id = conn->c_dev_if;
		addr = (struct sockaddr *)&sin6;
		addrlen = sizeof(sin6);
	} else {
		sin.sin_family = AF_INET;
		sin.sin_addr.s_addr = conn->c_laddr.s6_addr32[3];
		sin.sin_port = 0;
		addr = (struct sockaddr *)&sin;
		addrlen = sizeof(sin);
	}

	ret = sock->ops->bind(sock, addr, addrlen);
	if (ret) {
		rdsdebug("bind failed with %d at address %pI6c\n",
			 ret, &conn->c_laddr);
		goto out;
	}

	if (isv6) {
		sin6.sin6_family = AF_INET6;
		sin6.sin6_addr = conn->c_faddr;
		sin6.sin6_port = htons(RDS_TCP_PORT);
		sin6.sin6_flowinfo = 0;
		sin6.sin6_scope_id = conn->c_dev_if;
		addr = (struct sockaddr *)&sin6;
		addrlen = sizeof(sin6);
	} else {
		sin.sin_family = AF_INET;
		sin.sin_addr.s_addr = conn->c_faddr.s6_addr32[3];
		sin.sin_port = htons(RDS_TCP_PORT);
		addr = (struct sockaddr *)&sin;
		addrlen = sizeof(sin);
	}

	/*
	 * once we call connect() we can start getting callbacks and they
	 * own the socket
	 */
	rds_tcp_set_callbacks(sock, cp);
	ret = sock->ops->connect(sock, addr, addrlen, O_NONBLOCK);

	rdsdebug("connect to address %pI6c returned %d\n", &conn->c_faddr, ret);
	if (ret == -EINPROGRESS)
		ret = 0;
	if (ret == 0) {
		rds_tcp_keepalive(sock);
		sock = NULL;
	} else {
		rds_tcp_restore_callbacks(sock, cp->cp_transport_data);
	}

out:
	mutex_unlock(&tc->t_conn_path_lock);
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
void rds_tcp_conn_path_shutdown(struct rds_conn_path *cp)
{
	struct rds_tcp_connection *tc = cp->cp_transport_data;
	struct socket *sock = tc->t_sock;

	rdsdebug("shutting down conn %p tc %p sock %p\n",
		 cp->cp_conn, tc, sock);

	if (sock) {
		if (rds_destroy_pending(cp->cp_conn))
			rds_tcp_set_linger(sock);
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
