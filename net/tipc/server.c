/*
 * net/tipc/server.c: TIPC server infrastructure
 *
 * Copyright (c) 2012-2013, Wind River Systems
 * Copyright (c) 2017, Ericsson AB
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

#include "subscr.h"
#include "server.h"
#include "core.h"
#include "socket.h"
#include "addr.h"
#include "msg.h"
#include <net/sock.h>
#include <linux/module.h>

/* Number of messages to send before rescheduling */
#define MAX_SEND_MSG_COUNT	25
#define MAX_RECV_MSG_COUNT	25
#define CF_CONNECTED		1
#define CF_SERVER		2

#define sock2con(x) ((struct tipc_conn *)(x)->sk_user_data)

/**
 * struct tipc_conn - TIPC connection structure
 * @kref: reference counter to connection object
 * @conid: connection identifier
 * @sock: socket handler associated with connection
 * @flags: indicates connection state
 * @server: pointer to connected server
 * @sub_list: lsit to all pertaing subscriptions
 * @sub_lock: lock protecting the subscription list
 * @outqueue_lock: control access to the outqueue
 * @rwork: receive work item
 * @rx_action: what to do when connection socket is active
 * @outqueue: pointer to first outbound message in queue
 * @outqueue_lock: control access to the outqueue
 * @swork: send work item
 */
struct tipc_conn {
	struct kref kref;
	int conid;
	struct socket *sock;
	unsigned long flags;
	struct tipc_server *server;
	struct list_head sub_list;
	spinlock_t sub_lock; /* for subscription list */
	struct work_struct rwork;
	int (*rx_action) (struct tipc_conn *con);
	struct list_head outqueue;
	spinlock_t outqueue_lock;
	struct work_struct swork;
};

/* An entry waiting to be sent */
struct outqueue_entry {
	bool inactive;
	struct tipc_event evt;
	struct list_head list;
};

static void tipc_recv_work(struct work_struct *work);
static void tipc_send_work(struct work_struct *work);
static void tipc_clean_outqueues(struct tipc_conn *con);

static bool connected(struct tipc_conn *con)
{
	return con && test_bit(CF_CONNECTED, &con->flags);
}

static void tipc_conn_kref_release(struct kref *kref)
{
	struct tipc_conn *con = container_of(kref, struct tipc_conn, kref);
	struct tipc_server *s = con->server;
	struct socket *sock = con->sock;

	if (sock) {
		if (test_bit(CF_SERVER, &con->flags)) {
			__module_get(sock->ops->owner);
			__module_get(sock->sk->sk_prot_creator->owner);
		}
		sock_release(sock);
		con->sock = NULL;
	}
	spin_lock_bh(&s->idr_lock);
	idr_remove(&s->conn_idr, con->conid);
	s->idr_in_use--;
	spin_unlock_bh(&s->idr_lock);
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
	if (!connected(con) || !kref_get_unless_zero(&con->kref))
		con = NULL;
	spin_unlock_bh(&s->idr_lock);
	return con;
}

/* sock_data_ready - interrupt callback indicating the socket has data to read
 * The queued job is launched in tipc_recv_from_sock()
 */
static void sock_data_ready(struct sock *sk)
{
	struct tipc_conn *con;

	read_lock_bh(&sk->sk_callback_lock);
	con = sock2con(sk);
	if (connected(con)) {
		conn_get(con);
		if (!queue_work(con->server->rcv_wq, &con->rwork))
			conn_put(con);
	}
	read_unlock_bh(&sk->sk_callback_lock);
}

/* sock_write_space - interrupt callback after a sendmsg EAGAIN
 * Indicates that there now is more is space in the send buffer
 * The queued job is launched in tipc_send_to_sock()
 */
static void sock_write_space(struct sock *sk)
{
	struct tipc_conn *con;

	read_lock_bh(&sk->sk_callback_lock);
	con = sock2con(sk);
	if (connected(con)) {
		conn_get(con);
		if (!queue_work(con->server->send_wq, &con->swork))
			conn_put(con);
	}
	read_unlock_bh(&sk->sk_callback_lock);
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

/* tipc_con_delete_sub - delete a specific or all subscriptions
 *  for a given subscriber
 */
static void tipc_con_delete_sub(struct tipc_conn *con, struct tipc_subscr *s)
{
	struct list_head *sub_list = &con->sub_list;
	struct tipc_subscription *sub, *tmp;

	spin_lock_bh(&con->sub_lock);
	list_for_each_entry_safe(sub, tmp, sub_list, subscrp_list) {
		if (!s || !memcmp(s, &sub->evt.s, sizeof(*s)))
			tipc_sub_unsubscribe(sub);
		else if (s)
			break;
	}
	spin_unlock_bh(&con->sub_lock);
}

static void tipc_close_conn(struct tipc_conn *con)
{
	struct sock *sk = con->sock->sk;
	bool disconnect = false;

	write_lock_bh(&sk->sk_callback_lock);
	disconnect = test_and_clear_bit(CF_CONNECTED, &con->flags);

	if (disconnect) {
		sk->sk_user_data = NULL;
		if (con->conid)
			tipc_con_delete_sub(con, NULL);
	}
	write_unlock_bh(&sk->sk_callback_lock);

	/* Handle concurrent calls from sending and receiving threads */
	if (!disconnect)
		return;

	/* Don't flush pending works, -just let them expire */
	kernel_sock_shutdown(con->sock, SHUT_RDWR);
	conn_put(con);
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
	INIT_LIST_HEAD(&con->sub_list);
	spin_lock_init(&con->outqueue_lock);
	spin_lock_init(&con->sub_lock);
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

static int tipc_con_rcv_sub(struct tipc_server *srv,
			    struct tipc_conn *con,
			    struct tipc_subscr *s)
{
	struct tipc_subscription *sub;

	if (tipc_sub_read(s, filter) & TIPC_SUB_CANCEL) {
		tipc_con_delete_sub(con, s);
		return 0;
	}
	sub = tipc_sub_subscribe(srv, s, con->conid);
	if (!sub)
		return -1;

	spin_lock_bh(&con->sub_lock);
	list_add(&sub->subscrp_list, &con->sub_list);
	spin_unlock_bh(&con->sub_lock);
	return 0;
}

static int tipc_receive_from_sock(struct tipc_conn *con)
{
	struct tipc_server *srv = con->server;
	struct sock *sk = con->sock->sk;
	struct msghdr msg = {};
	struct tipc_subscr s;
	struct kvec iov;
	int ret;

	iov.iov_base = &s;
	iov.iov_len = sizeof(s);
	msg.msg_name = NULL;
	iov_iter_kvec(&msg.msg_iter, READ | ITER_KVEC, &iov, 1, iov.iov_len);
	ret = sock_recvmsg(con->sock, &msg, MSG_DONTWAIT);
	if (ret == -EWOULDBLOCK)
		return -EWOULDBLOCK;
	if (ret > 0) {
		read_lock_bh(&sk->sk_callback_lock);
		ret = tipc_con_rcv_sub(srv, con, &s);
		read_unlock_bh(&sk->sk_callback_lock);
	}
	if (ret < 0)
		tipc_close_conn(con);

	return ret;
}

static int tipc_accept_from_sock(struct tipc_conn *con)
{
	struct socket *sock = con->sock;
	struct socket *newsock;
	struct tipc_conn *newcon;
	int ret;

	ret = kernel_accept(sock, &newsock, O_NONBLOCK);
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

	/* Wake up receive process in case of 'SYN+' message */
	newsock->sk->sk_data_ready(newsock->sk);
	return ret;
}

static struct socket *tipc_create_listen_sock(struct tipc_conn *con)
{
	struct tipc_server *s = con->server;
	struct socket *sock = NULL;
	int imp = TIPC_CRITICAL_IMPORTANCE;
	int ret;

	ret = sock_create_kern(s->net, AF_TIPC, SOCK_SEQPACKET, 0, &sock);
	if (ret < 0)
		return NULL;
	ret = kernel_setsockopt(sock, SOL_TIPC, TIPC_IMPORTANCE,
				(char *)&imp, sizeof(imp));
	if (ret < 0)
		goto create_err;
	ret = kernel_bind(sock, (struct sockaddr *)s->saddr, sizeof(*s->saddr));
	if (ret < 0)
		goto create_err;

	con->rx_action = tipc_accept_from_sock;
	ret = kernel_listen(sock, 0);
	if (ret < 0)
		goto create_err;

	/* As server's listening socket owner and creator is the same module,
	 * we have to decrease TIPC module reference count to guarantee that
	 * it remains zero after the server socket is created, otherwise,
	 * executing "rmmod" command is unable to make TIPC module deleted
	 * after TIPC module is inserted successfully.
	 *
	 * However, the reference count is ever increased twice in
	 * sock_create_kern(): one is to increase the reference count of owner
	 * of TIPC socket's proto_ops struct; another is to increment the
	 * reference count of owner of TIPC proto struct. Therefore, we must
	 * decrement the module reference count twice to ensure that it keeps
	 * zero after server's listening socket is created. Of course, we
	 * must bump the module reference count twice as well before the socket
	 * is closed.
	 */
	module_put(sock->ops->owner);
	module_put(sock->sk->sk_prot_creator->owner);
	set_bit(CF_SERVER, &con->flags);

	return sock;

create_err:
	kernel_sock_shutdown(sock, SHUT_RDWR);
	sock_release(sock);
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

static void tipc_clean_outqueues(struct tipc_conn *con)
{
	struct outqueue_entry *e, *safe;

	spin_lock_bh(&con->outqueue_lock);
	list_for_each_entry_safe(e, safe, &con->outqueue, list) {
		list_del(&e->list);
		kfree(e);
	}
	spin_unlock_bh(&con->outqueue_lock);
}

/* tipc_conn_queue_evt - interrupt level call from a subscription instance
 * The queued job is launched in tipc_send_to_sock()
 */
void tipc_conn_queue_evt(struct tipc_server *s, int conid,
			 u32 event, struct tipc_event *evt)
{
	struct outqueue_entry *e;
	struct tipc_conn *con;

	con = tipc_conn_lookup(s, conid);
	if (!con)
		return;

	if (!connected(con))
		goto err;

	e = kmalloc(sizeof(*e), GFP_ATOMIC);
	if (!e)
		goto err;
	e->inactive = (event == TIPC_SUBSCR_TIMEOUT);
	memcpy(&e->evt, evt, sizeof(*evt));
	spin_lock_bh(&con->outqueue_lock);
	list_add_tail(&e->list, &con->outqueue);
	spin_unlock_bh(&con->outqueue_lock);

	if (queue_work(s->send_wq, &con->swork))
		return;
err:
	conn_put(con);
}

bool tipc_topsrv_kern_subscr(struct net *net, u32 port, u32 type, u32 lower,
			     u32 upper, u32 filter, int *conid)
{
	struct tipc_subscr sub;
	struct tipc_conn *con;
	int rc;

	sub.seq.type = type;
	sub.seq.lower = lower;
	sub.seq.upper = upper;
	sub.timeout = TIPC_WAIT_FOREVER;
	sub.filter = filter;
	*(u32 *)&sub.usr_handle = port;

	con = tipc_alloc_conn(tipc_topsrv(net));
	if (IS_ERR(con))
		return false;

	*conid = con->conid;
	con->sock = NULL;
	rc = tipc_con_rcv_sub(tipc_topsrv(net), con, &sub);
	if (rc < 0)
		tipc_close_conn(con);
	return !rc;
}

void tipc_topsrv_kern_unsubscr(struct net *net, int conid)
{
	struct tipc_conn *con;

	con = tipc_conn_lookup(tipc_topsrv(net), conid);
	if (!con)
		return;

	test_and_clear_bit(CF_CONNECTED, &con->flags);
	tipc_con_delete_sub(con, NULL);
	conn_put(con);
	conn_put(con);
}

static void tipc_send_kern_top_evt(struct net *net, struct tipc_event *evt)
{
	u32 port = *(u32 *)&evt->s.usr_handle;
	u32 self = tipc_own_addr(net);
	struct sk_buff_head evtq;
	struct sk_buff *skb;

	skb = tipc_msg_create(TOP_SRV, 0, INT_H_SIZE, sizeof(*evt),
			      self, self, port, port, 0);
	if (!skb)
		return;
	msg_set_dest_droppable(buf_msg(skb), true);
	memcpy(msg_data(buf_msg(skb)), evt, sizeof(*evt));
	skb_queue_head_init(&evtq);
	__skb_queue_tail(&evtq, skb);
	tipc_sk_rcv(net, &evtq);
}

static void tipc_send_to_sock(struct tipc_conn *con)
{
	struct list_head *queue = &con->outqueue;
	struct tipc_server *srv = con->server;
	struct outqueue_entry *e;
	struct tipc_event *evt;
	struct msghdr msg;
	struct kvec iov;
	int count = 0;
	int ret;

	spin_lock_bh(&con->outqueue_lock);

	while (!list_empty(queue)) {
		e = list_first_entry(queue, struct outqueue_entry, list);
		evt = &e->evt;
		spin_unlock_bh(&con->outqueue_lock);

		if (e->inactive)
			tipc_con_delete_sub(con, &evt->s);

		memset(&msg, 0, sizeof(msg));
		msg.msg_flags = MSG_DONTWAIT;
		iov.iov_base = evt;
		iov.iov_len = sizeof(*evt);
		msg.msg_name = NULL;

		if (con->sock) {
			ret = kernel_sendmsg(con->sock, &msg, &iov,
					     1, sizeof(*evt));
			if (ret == -EWOULDBLOCK || ret == 0) {
				cond_resched();
				goto out;
			} else if (ret < 0) {
				goto err;
			}
		} else {
			tipc_send_kern_top_evt(srv->net, evt);
		}

		/* Don't starve users filling buffers */
		if (++count >= MAX_SEND_MSG_COUNT) {
			cond_resched();
			count = 0;
		}
		spin_lock_bh(&con->outqueue_lock);
		list_del(&e->list);
		kfree(e);
	}
	spin_unlock_bh(&con->outqueue_lock);
out:
	return;
err:
	tipc_close_conn(con);
}

static void tipc_recv_work(struct work_struct *work)
{
	struct tipc_conn *con = container_of(work, struct tipc_conn, rwork);
	int count = 0;

	while (connected(con)) {
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

	if (connected(con))
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
	s->rcv_wq = alloc_ordered_workqueue("tipc_rcv", 0);
	if (!s->rcv_wq) {
		pr_err("can't start tipc receive workqueue\n");
		return -ENOMEM;
	}

	s->send_wq = alloc_ordered_workqueue("tipc_send", 0);
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

	ret = tipc_work_start(s);
	if (ret < 0)
		return ret;

	ret = tipc_open_listening_sock(s);
	if (ret < 0)
		tipc_work_stop(s);

	return ret;
}

void tipc_server_stop(struct tipc_server *s)
{
	struct tipc_conn *con;
	int id;

	spin_lock_bh(&s->idr_lock);
	for (id = 0; s->idr_in_use; id++) {
		con = idr_find(&s->conn_idr, id);
		if (con) {
			spin_unlock_bh(&s->idr_lock);
			tipc_close_conn(con);
			spin_lock_bh(&s->idr_lock);
		}
	}
	spin_unlock_bh(&s->idr_lock);

	tipc_work_stop(s);
	idr_destroy(&s->conn_idr);
}
