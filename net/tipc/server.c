/*
 * net/tipc/server.c: TIPC server infrastructure
 *
 * Copyright (c) 2012-2013, Wind River Systems
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "server.h"
#include "core.h"
#include "socket.h"
#include <net/sock.h>

/* Number of messages to send before rescheduling */
#define MAX_SEND_MSG_COUNT	25
#define MAX_RECV_MSG_COUNT	25
#define CF_CONNECTED		1

#define sock2con(x) ((struct tipc_conn *)(x)->sk_user_data)

/**
 * struct tipc_conn - TIPC connection structure
 * @kref: reference counter to connection object
 * @conid: connection identifier
 * @sock: socket handler associated with connection
 * @flags: indicates connection state
 * @server: pointer to connected server
 * @rwork: receive work item
 * @usr_data: user-specified field
 * @rx_action: what to do when connection socket is active
 * @outqueue: pointer to first outbound message in queue
 * @outqueue_lock: control access to the outqueue
 * @outqueue: list of connection objects for its server
 * @swork: send work item
 */
struct tipc_conn {
	struct kref kref;
	int conid;
	struct socket *sock;
	unsigned long flags;
	struct tipc_server *server;
	struct work_struct rwork;
	int (*rx_action) (struct tipc_conn *con);
	void *usr_data;
	struct list_head outqueue;
	spinlock_t outqueue_lock;
	struct work_struct swork;
};

/* An entry waiting to be sent */
struct outqueue_entry {
	struct list_head list;
	struct kvec iov;
	struct sockaddr_tipc dest;
};

static void tipc_recv_work(struct work_struct *work);
static void tipc_send_work(struct work_struct *work);
static void tipc_clean_outqueues(struct tipc_conn *con);

static void tipc_conn_kref_release(struct kref *kref)
{
	struct tipc_conn *con = container_of(kref, struct tipc_conn, kref);

	if (con->sock) {
		tipc_sock_release_local(con->sock);
		con->sock = NULL;
	}

	tipc_clean_outqueues(con);
	kfree(con);
}

static void conn_put(struct tipc_conn *con)
{
	kref_put(&con->kref, tipc_conn_kref_release);
}

static void conn_get(struct tipc_conn *con)
{
	kref_get(&con->kref);
}

static struct tipc_conn *tipc_conn_lookup(struct tipc_server *s, int conid)
{
	struct tipc_conn *con;

	spin_lock_bh(&s->idr_lock);
	con = idr_find(&s->conn_idr, conid);
	if (con)
		conn_get(con);
	spin_unlock_bh(&s->idr_lock);
	return con;
}

static void sock_data_ready(struct sock *sk)
{
	struct tipc_conn *con;

	read_lock(&sk->sk_callback_lock);
	con = sock2con(sk);
	if (con && test_bit(CF_CONNECTED, &con->flags)) {
		conn_get(con);
		if (!queue_work(con->server->rcv_wq, &con->rwork))
			conn_put(con);
	}
	read_unlock(&sk->sk_callback_lock);
}

static void sock_write_space(struct sock *sk)
{
	struct tipc_conn *con;

	read_lock(&sk->sk_callback_lock);
	con = sock2con(sk);
	if (con && test_bit(CF_CONNECTED, &con->flags)) {
		conn_get(con);
		if (!queue_work(con->server->send_wq, &con->swork))
			conn_put(con);
	}
	read_unlock(&sk->sk_callback_lock);
}

static void tipc_register_callbacks(struct socket *sock, struct tipc_conn *con)
{
	struct sock *sk = sock->sk;

	write_lock_bh(&sk->sk_callback_lock);

	sk->sk_data_ready = sock_data_ready;
	sk->sk_write_space = sock_write_space;
	sk->sk_user_data = con;

	con->sock = sock;

	write_unlock_bh(&sk->sk_callback_lock);
}

static void tipc_unregister_callbacks(struct tipc_conn *con)
{
	struct sock *sk = con->sock->sk;

	write_lock_bh(&sk->sk_callback_lock);
	sk->sk_user_data = NULL;
	write_unlock_bh(&sk->sk_callback_lock);
}

static void tipc_close_conn(struct tipc_conn *con)
{
	struct tipc_server *s = con->server;

	if (test_and_clear_bit(CF_CONNECTED, &con->flags)) {
		if (con->conid)
			s->tipc_conn_shutdown(con->conid, con->usr_data);

		spin_lock_bh(&s->idr_lock);
		idr_remove(&s->conn_idr, con->conid);
		s->idr_in_use--;
		spin_unlock_bh(&s->idr_lock);

		tipc_unregister_callbacks(con);

		/* We shouldn't flush pending works as we may be in the
		 * thread. In fact the races with pending rx/tx work structs
		 * are harmless for us here as we have already deleted this
		 * connection from server connection list and set
		 * sk->sk_user_data to 0 before releasing connection object.
		 */
		kernel_sock_shutdown(con->sock, SHUT_RDWR);

		conn_put(con);
	}
}

static struct tipc_conn *tipc_alloc_conn(struct tipc_server *s)
{
	struct tipc_conn *con;
	int ret;

	con = kzalloc(sizeof(struct tipc_conn), GFP_ATOMIC);
	if (!con)
		return ERR_PTR(-ENOMEM);

	kref_init(&con->kref);
	INIT_LIST_HEAD(&con->outqueue);
	spin_lock_init(&con->outqueue_lock);
	INIT_WORK(&con->swork, tipc_send_work);
	INIT_WORK(&con->rwork, tipc_recv_work);

	spin_lock_bh(&s->idr_lock);
	ret = idr_alloc(&s->conn_idr, con, 0, 0, GFP_ATOMIC);
	if (ret < 0) {
		kfree(con);
		spin_unlock_bh(&s->idr_lock);
		return ERR_PTR(-ENOMEM);
	}
	con->conid = ret;
	s->idr_in_use++;
	spin_unlock_bh(&s->idr_lock);

	set_bit(CF_CONNECTED, &con->flags);
	con->server = s;

	return con;
}

static int tipc_receive_from_sock(struct tipc_conn *con)
{
	struct msghdr msg = {};
	struct tipc_server *s = con->server;
	struct sockaddr_tipc addr;
	struct kvec iov;
	void *buf;
	int ret;

	buf = kmem_cache_alloc(s->rcvbuf_cache, GFP_ATOMIC);
	if (!buf) {
		ret = -ENOMEM;
		goto out_close;
	}

	iov.iov_base = buf;
	iov.iov_len = s->max_rcvbuf_size;
	msg.msg_name = &addr;
	ret = kernel_recvmsg(con->sock, &msg, &iov, 1, iov.iov_len,
			     MSG_DONTWAIT);
	if (ret <= 0) {
		kmem_cache_free(s->rcvbuf_cache, buf);
		goto out_close;
	}

	s->tipc_conn_recvmsg(sock_net(con->sock->sk), con->conid, &addr,
			     con->usr_data, buf, ret);

	kmem_cache_free(s->rcvbuf_cache, buf);

	return 0;

out_close:
	if (ret != -EWOULDBLOCK)
		tipc_close_conn(con);
	else if (ret == 0)
		/* Don't return success if we really got EOF */
		ret = -EAGAIN;

	return ret;
}

static int tipc_accept_from_sock(struct tipc_conn *con)
{
	struct tipc_server *s = con->server;
	struct socket *sock = con->sock;
	struct socket *newsock;
	struct tipc_conn *newcon;
	int ret;

	ret = tipc_sock_accept_local(sock, &newsock, O_NONBLOCK);
	if (ret < 0)
		return ret;

	newcon = tipc_alloc_conn(con->server);
	if (IS_ERR(newcon)) {
		ret = PTR_ERR(newcon);
		sock_release(newsock);
		return ret;
	}

	newcon->rx_action = tipc_receive_from_sock;
	tipc_register_callbacks(newsock, newcon);

	/* Notify that new connection is incoming */
	newcon->usr_data = s->tipc_conn_new(newcon->conid);

	/* Wake up receive process in case of 'SYN+' message */
	newsock->sk->sk_data_ready(newsock->sk);
	return ret;
}

static struct socket *tipc_create_listen_sock(struct tipc_conn *con)
{
	struct tipc_server *s = con->server;
	struct socket *sock = NULL;
	int ret;

	ret = tipc_sock_create_local(s->net, s->type, &sock);
	if (ret < 0)
		return NULL;
	ret = kernel_setsockopt(sock, SOL_TIPC, TIPC_IMPORTANCE,
				(char *)&s->imp, sizeof(s->imp));
	if (ret < 0)
		goto create_err;
	ret = kernel_bind(sock, (struct sockaddr *)s->saddr, sizeof(*s->saddr));
	if (ret < 0)
		goto create_err;

	switch (s->type) {
	case SOCK_STREAM:
	case SOCK_SEQPACKET:
		con->rx_action = tipc_accept_from_sock;

		ret = kernel_listen(sock, 0);
		if (ret < 0)
			goto create_err;
		break;
	case SOCK_DGRAM:
	case SOCK_RDM:
		con->rx_action = tipc_receive_from_sock;
		break;
	default:
		pr_err("Unknown socket type %d\n", s->type);
		goto create_err;
	}
	return sock;

create_err:
	sock_release(sock);
	con->sock = NULL;
	return NULL;
}

static int tipc_open_listening_sock(struct tipc_server *s)
{
	struct socket *sock;
	struct tipc_conn *con;

	con = tipc_alloc_conn(s);
	if (IS_ERR(con))
		return PTR_ERR(con);

	sock = tipc_create_listen_sock(con);
	if (!sock) {
		idr_remove(&s->conn_idr, con->conid);
		s->idr_in_use--;
		kfree(con);
		return -EINVAL;
	}

	tipc_register_callbacks(sock, con);
	return 0;
}

static struct outqueue_entry *tipc_alloc_entry(void *data, int len)
{
	struct outqueue_entry *entry;
	void *buf;

	entry = kmalloc(sizeof(struct outqueue_entry), GFP_ATOMIC);
	if (!entry)
		return NULL;

	buf = kmalloc(len, GFP_ATOMIC);
	if (!buf) {
		kfree(entry);
		return NULL;
	}

	memcpy(buf, data, len);
	entry->iov.iov_base = buf;
	entry->iov.iov_len = len;

	return entry;
}

static void tipc_free_entry(struct outqueue_entry *e)
{
	kfree(e->iov.iov_base);
	kfree(e);
}

static void tipc_clean_outqueues(struct tipc_conn *con)
{
	struct outqueue_entry *e, *safe;

	spin_lock_bh(&con->outqueue_lock);
	list_for_each_entry_safe(e, safe, &con->outqueue, list) {
		list_del(&e->list);
		tipc_free_entry(e);
	}
	spin_unlock_bh(&con->outqueue_lock);
}

int tipc_conn_sendmsg(struct tipc_server *s, int conid,
		      struct sockaddr_tipc *addr, void *data, size_t len)
{
	struct outqueue_entry *e;
	struct tipc_conn *con;

	con = tipc_conn_lookup(s, conid);
	if (!con)
		return -EINVAL;

	e = tipc_alloc_entry(data, len);
	if (!e) {
		conn_put(con);
		return -ENOMEM;
	}

	if (addr)
		memcpy(&e->dest, addr, sizeof(struct sockaddr_tipc));

	spin_lock_bh(&con->outqueue_lock);
	list_add_tail(&e->list, &con->outqueue);
	spin_unlock_bh(&con->outqueue_lock);

	if (test_bit(CF_CONNECTED, &con->flags)) {
		if (!queue_work(s->send_wq, &con->swork))
			conn_put(con);
	} else {
		conn_put(con);
	}
	return 0;
}

void tipc_conn_terminate(struct tipc_server *s, int conid)
{
	struct tipc_conn *con;

	con = tipc_conn_lookup(s, conid);
	if (con) {
		tipc_close_conn(con);
		conn_put(con);
	}
}

static void tipc_send_to_sock(struct tipc_conn *con)
{
	int count = 0;
	struct tipc_server *s = con->server;
	struct outqueue_entry *e;
	struct msghdr msg;
	int ret;

	spin_lock_bh(&con->outqueue_lock);
	while (1) {
		e = list_entry(con->outqueue.next, struct outqueue_entry,
			       list);
		if ((struct list_head *) e == &con->outqueue)
			break;
		spin_unlock_bh(&con->outqueue_lock);

		memset(&msg, 0, sizeof(msg));
		msg.msg_flags = MSG_DONTWAIT;

		if (s->type == SOCK_DGRAM || s->type == SOCK_RDM) {
			msg.msg_name = &e->dest;
			msg.msg_namelen = sizeof(struct sockaddr_tipc);
		}
		ret = kernel_sendmsg(con->sock, &msg, &e->iov, 1,
				     e->iov.iov_len);
		if (ret == -EWOULDBLOCK || ret == 0) {
			cond_resched();
			goto out;
		} else if (ret < 0) {
			goto send_err;
		}

		/* Don't starve users filling buffers */
		if (++count >= MAX_SEND_MSG_COUNT) {
			cond_resched();
			count = 0;
		}

		spin_lock_bh(&con->outqueue_lock);
		list_del(&e->list);
		tipc_free_entry(e);
	}
	spin_unlock_bh(&con->outqueue_lock);
out:
	return;

send_err:
	tipc_close_conn(con);
}

static void tipc_recv_work(struct work_struct *work)
{
	struct tipc_conn *con = container_of(work, struct tipc_conn, rwork);
	int count = 0;

	while (test_bit(CF_CONNECTED, &con->flags)) {
		if (con->rx_action(con))
			break;

		/* Don't flood Rx machine */
		if (++count >= MAX_RECV_MSG_COUNT) {
			cond_resched();
			count = 0;
		}
	}
	conn_put(con);
}

static void tipc_send_work(struct work_struct *work)
{
	struct tipc_conn *con = container_of(work, struct tipc_conn, swork);

	if (test_bit(CF_CONNECTED, &con->flags))
		tipc_send_to_sock(con);

	conn_put(con);
}

static void tipc_work_stop(struct tipc_server *s)
{
	destroy_workqueue(s->rcv_wq);
	destroy_workqueue(s->send_wq);
}

static int tipc_work_start(struct tipc_server *s)
{
	s->rcv_wq = alloc_workqueue("tipc_rcv", WQ_UNBOUND, 1);
	if (!s->rcv_wq) {
		pr_err("can't start tipc receive workqueue\n");
		return -ENOMEM;
	}

	s->send_wq = alloc_workqueue("tipc_send", WQ_UNBOUND, 1);
	if (!s->send_wq) {
		pr_err("can't start tipc send workqueue\n");
		destroy_workqueue(s->rcv_wq);
		return -ENOMEM;
	}

	return 0;
}

int tipc_server_start(struct tipc_server *s)
{
	int ret;

	spin_lock_init(&s->idr_lock);
	idr_init(&s->conn_idr);
	s->idr_in_use = 0;

	s->rcvbuf_cache = kmem_cache_create(s->name, s->max_rcvbuf_size,
					    0, SLAB_HWCACHE_ALIGN, NULL);
	if (!s->rcvbuf_cache)
		return -ENOMEM;

	ret = tipc_work_start(s);
	if (ret < 0) {
		kmem_cache_destroy(s->rcvbuf_cache);
		return ret;
	}
	ret = tipc_open_listening_sock(s);
	if (ret < 0) {
		tipc_work_stop(s);
		kmem_cache_destroy(s->rcvbuf_cache);
		return ret;
	}
	return ret;
}

void tipc_server_stop(struct tipc_server *s)
{
	struct tipc_conn *con;
	int total = 0;
	int id;

	spin_lock_bh(&s->idr_lock);
	for (id = 0; total < s->idr_in_use; id++) {
		con = idr_find(&s->conn_idr, id);
		if (con) {
			total++;
			spin_unlock_bh(&s->idr_lock);
			tipc_close_conn(con);
			spin_lock_bh(&s->idr_lock);
		}
	}
	spin_unlock_bh(&s->idr_lock);

	tipc_work_stop(s);
	kmem_cache_destroy(s->rcvbuf_cache);
	idr_destroy(&s->conn_idr);
}
