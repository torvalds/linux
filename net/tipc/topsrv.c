/*
 * net/tipc/server.c: TIPC server infrastructure
 *
 * Copyright (c) 2012-2013, Wind River Systems
 * Copyright (c) 2017-2018, Ericsson AB
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
#include "topsrv.h"
#include "core.h"
#include "socket.h"
#include "addr.h"
#include "msg.h"
#include "bearer.h"
#include <net/sock.h>
#include <linux/module.h>

/* Number of messages to send before rescheduling */
#define MAX_SEND_MSG_COUNT	25
#define MAX_RECV_MSG_COUNT	25
#define CF_CONNECTED		1

#define TIPC_SERVER_NAME_LEN	32

/**
 * struct tipc_topsrv - TIPC server structure
 * @conn_idr: identifier set of connection
 * @idr_lock: protect the connection identifier set
 * @idr_in_use: amount of allocated identifier entry
 * @net: network namspace instance
 * @awork: accept work item
 * @rcv_wq: receive workqueue
 * @send_wq: send workqueue
 * @listener: topsrv listener socket
 * @name: server name
 */
struct tipc_topsrv {
	struct idr conn_idr;
	spinlock_t idr_lock; /* for idr list */
	int idr_in_use;
	struct net *net;
	struct work_struct awork;
	struct workqueue_struct *rcv_wq;
	struct workqueue_struct *send_wq;
	struct socket *listener;
	char name[TIPC_SERVER_NAME_LEN];
};

/**
 * struct tipc_conn - TIPC connection structure
 * @kref: reference counter to connection object
 * @conid: connection identifier
 * @sock: socket handler associated with connection
 * @flags: indicates connection state
 * @server: pointer to connected server
 * @sub_list: lsit to all pertaing subscriptions
 * @sub_lock: lock protecting the subscription list
 * @rwork: receive work item
 * @outqueue: pointer to first outbound message in queue
 * @outqueue_lock: control access to the outqueue
 * @swork: send work item
 */
struct tipc_conn {
	struct kref kref;
	int conid;
	struct socket *sock;
	unsigned long flags;
	struct tipc_topsrv *server;
	struct list_head sub_list;
	spinlock_t sub_lock; /* for subscription list */
	struct work_struct rwork;
	struct list_head outqueue;
	spinlock_t outqueue_lock; /* for outqueue */
	struct work_struct swork;
};

/* An entry waiting to be sent */
struct outqueue_entry {
	bool inactive;
	struct tipc_event evt;
	struct list_head list;
};

static void tipc_conn_recv_work(struct work_struct *work);
static void tipc_conn_send_work(struct work_struct *work);
static void tipc_topsrv_kern_evt(struct net *net, struct tipc_event *evt);
static void tipc_conn_delete_sub(struct tipc_conn *con, struct tipc_subscr *s);

static bool connected(struct tipc_conn *con)
{
	return con && test_bit(CF_CONNECTED, &con->flags);
}

static void tipc_conn_kref_release(struct kref *kref)
{
	struct tipc_conn *con = container_of(kref, struct tipc_conn, kref);
	struct tipc_topsrv *s = con->server;
	struct outqueue_entry *e, *safe;

	spin_lock_bh(&s->idr_lock);
	idr_remove(&s->conn_idr, con->conid);
	s->idr_in_use--;
	spin_unlock_bh(&s->idr_lock);
	if (con->sock)
		sock_release(con->sock);

	spin_lock_bh(&con->outqueue_lock);
	list_for_each_entry_safe(e, safe, &con->outqueue, list) {
		list_del(&e->list);
		kfree(e);
	}
	spin_unlock_bh(&con->outqueue_lock);
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

static void tipc_conn_close(struct tipc_conn *con)
{
	struct sock *sk = con->sock->sk;
	bool disconnect = false;

	write_lock_bh(&sk->sk_callback_lock);
	disconnect = test_and_clear_bit(CF_CONNECTED, &con->flags);

	if (disconnect) {
		sk->sk_user_data = NULL;
		tipc_conn_delete_sub(con, NULL);
	}
	write_unlock_bh(&sk->sk_callback_lock);

	/* Handle concurrent calls from sending and receiving threads */
	if (!disconnect)
		return;

	/* Don't flush pending works, -just let them expire */
	kernel_sock_shutdown(con->sock, SHUT_RDWR);

	conn_put(con);
}

static struct tipc_conn *tipc_conn_alloc(struct tipc_topsrv *s, struct socket *sock)
{
	struct tipc_conn *con;
	int ret;

	con = kzalloc(sizeof(*con), GFP_ATOMIC);
	if (!con)
		return ERR_PTR(-ENOMEM);

	kref_init(&con->kref);
	INIT_LIST_HEAD(&con->outqueue);
	INIT_LIST_HEAD(&con->sub_list);
	spin_lock_init(&con->outqueue_lock);
	spin_lock_init(&con->sub_lock);
	INIT_WORK(&con->swork, tipc_conn_send_work);
	INIT_WORK(&con->rwork, tipc_conn_recv_work);

	spin_lock_bh(&s->idr_lock);
	ret = idr_alloc(&s->conn_idr, con, 0, 0, GFP_ATOMIC);
	if (ret < 0) {
		kfree(con);
		spin_unlock_bh(&s->idr_lock);
		return ERR_PTR(-ENOMEM);
	}
	con->conid = ret;
	s->idr_in_use++;

	set_bit(CF_CONNECTED, &con->flags);
	con->server = s;
	con->sock = sock;
	conn_get(con);
	spin_unlock_bh(&s->idr_lock);

	return con;
}

static struct tipc_conn *tipc_conn_lookup(struct tipc_topsrv *s, int conid)
{
	struct tipc_conn *con;

	spin_lock_bh(&s->idr_lock);
	con = idr_find(&s->conn_idr, conid);
	if (!connected(con) || !kref_get_unless_zero(&con->kref))
		con = NULL;
	spin_unlock_bh(&s->idr_lock);
	return con;
}

/* tipc_conn_delete_sub - delete a specific or all subscriptions
 * for a given subscriber
 */
static void tipc_conn_delete_sub(struct tipc_conn *con, struct tipc_subscr *s)
{
	struct tipc_net *tn = tipc_net(con->server->net);
	struct list_head *sub_list = &con->sub_list;
	struct tipc_subscription *sub, *tmp;

	spin_lock_bh(&con->sub_lock);
	list_for_each_entry_safe(sub, tmp, sub_list, sub_list) {
		if (!s || !memcmp(s, &sub->evt.s, sizeof(*s))) {
			tipc_sub_unsubscribe(sub);
			atomic_dec(&tn->subscription_count);
			if (s)
				break;
		}
	}
	spin_unlock_bh(&con->sub_lock);
}

static void tipc_conn_send_to_sock(struct tipc_conn *con)
{
	struct list_head *queue = &con->outqueue;
	struct tipc_topsrv *srv = con->server;
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
			tipc_conn_delete_sub(con, &evt->s);

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
				return;
			} else if (ret < 0) {
				return tipc_conn_close(con);
			}
		} else {
			tipc_topsrv_kern_evt(srv->net, evt);
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
}

static void tipc_conn_send_work(struct work_struct *work)
{
	struct tipc_conn *con = container_of(work, struct tipc_conn, swork);

	if (connected(con))
		tipc_conn_send_to_sock(con);

	conn_put(con);
}

/* tipc_topsrv_queue_evt() - interrupt level call from a subscription instance
 * The queued work is launched into tipc_conn_send_work()->tipc_conn_send_to_sock()
 */
void tipc_topsrv_queue_evt(struct net *net, int conid,
			   u32 event, struct tipc_event *evt)
{
	struct tipc_topsrv *srv = tipc_topsrv(net);
	struct outqueue_entry *e;
	struct tipc_conn *con;

	con = tipc_conn_lookup(srv, conid);
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

	if (queue_work(srv->send_wq, &con->swork))
		return;
err:
	conn_put(con);
}

/* tipc_conn_write_space - interrupt callback after a sendmsg EAGAIN
 * Indicates that there now is more space in the send buffer
 * The queued work is launched into tipc_send_work()->tipc_conn_send_to_sock()
 */
static void tipc_conn_write_space(struct sock *sk)
{
	struct tipc_conn *con;

	read_lock_bh(&sk->sk_callback_lock);
	con = sk->sk_user_data;
	if (connected(con)) {
		conn_get(con);
		if (!queue_work(con->server->send_wq, &con->swork))
			conn_put(con);
	}
	read_unlock_bh(&sk->sk_callback_lock);
}

static int tipc_conn_rcv_sub(struct tipc_topsrv *srv,
			     struct tipc_conn *con,
			     struct tipc_subscr *s)
{
	struct tipc_net *tn = tipc_net(srv->net);
	struct tipc_subscription *sub;
	u32 s_filter = tipc_sub_read(s, filter);

	if (s_filter & TIPC_SUB_CANCEL) {
		tipc_sub_write(s, filter, s_filter & ~TIPC_SUB_CANCEL);
		tipc_conn_delete_sub(con, s);
		return 0;
	}
	if (atomic_read(&tn->subscription_count) >= TIPC_MAX_SUBSCR) {
		pr_warn("Subscription rejected, max (%u)\n", TIPC_MAX_SUBSCR);
		return -1;
	}
	sub = tipc_sub_subscribe(srv->net, s, con->conid);
	if (!sub)
		return -1;
	atomic_inc(&tn->subscription_count);
	spin_lock_bh(&con->sub_lock);
	list_add(&sub->sub_list, &con->sub_list);
	spin_unlock_bh(&con->sub_lock);
	return 0;
}

static int tipc_conn_rcv_from_sock(struct tipc_conn *con)
{
	struct tipc_topsrv *srv = con->server;
	struct sock *sk = con->sock->sk;
	struct msghdr msg = {};
	struct tipc_subscr s;
	struct kvec iov;
	int ret;

	iov.iov_base = &s;
	iov.iov_len = sizeof(s);
	msg.msg_name = NULL;
	iov_iter_kvec(&msg.msg_iter, ITER_DEST, &iov, 1, iov.iov_len);
	ret = sock_recvmsg(con->sock, &msg, MSG_DONTWAIT);
	if (ret == -EWOULDBLOCK)
		return -EWOULDBLOCK;
	if (ret == sizeof(s)) {
		read_lock_bh(&sk->sk_callback_lock);
		/* RACE: the connection can be closed in the meantime */
		if (likely(connected(con)))
			ret = tipc_conn_rcv_sub(srv, con, &s);
		read_unlock_bh(&sk->sk_callback_lock);
		if (!ret)
			return 0;
	}

	tipc_conn_close(con);
	return ret;
}

static void tipc_conn_recv_work(struct work_struct *work)
{
	struct tipc_conn *con = container_of(work, struct tipc_conn, rwork);
	int count = 0;

	while (connected(con)) {
		if (tipc_conn_rcv_from_sock(con))
			break;

		/* Don't flood Rx machine */
		if (++count >= MAX_RECV_MSG_COUNT) {
			cond_resched();
			count = 0;
		}
	}
	conn_put(con);
}

/* tipc_conn_data_ready - interrupt callback indicating the socket has data
 * The queued work is launched into tipc_recv_work()->tipc_conn_rcv_from_sock()
 */
static void tipc_conn_data_ready(struct sock *sk)
{
	struct tipc_conn *con;

	read_lock_bh(&sk->sk_callback_lock);
	con = sk->sk_user_data;
	if (connected(con)) {
		conn_get(con);
		if (!queue_work(con->server->rcv_wq, &con->rwork))
			conn_put(con);
	}
	read_unlock_bh(&sk->sk_callback_lock);
}

static void tipc_topsrv_accept(struct work_struct *work)
{
	struct tipc_topsrv *srv = container_of(work, struct tipc_topsrv, awork);
	struct socket *newsock, *lsock;
	struct tipc_conn *con;
	struct sock *newsk;
	int ret;

	spin_lock_bh(&srv->idr_lock);
	if (!srv->listener) {
		spin_unlock_bh(&srv->idr_lock);
		return;
	}
	lsock = srv->listener;
	spin_unlock_bh(&srv->idr_lock);

	while (1) {
		ret = kernel_accept(lsock, &newsock, O_NONBLOCK);
		if (ret < 0)
			return;
		con = tipc_conn_alloc(srv, newsock);
		if (IS_ERR(con)) {
			ret = PTR_ERR(con);
			sock_release(newsock);
			return;
		}
		/* Register callbacks */
		newsk = newsock->sk;
		write_lock_bh(&newsk->sk_callback_lock);
		newsk->sk_data_ready = tipc_conn_data_ready;
		newsk->sk_write_space = tipc_conn_write_space;
		newsk->sk_user_data = con;
		write_unlock_bh(&newsk->sk_callback_lock);

		/* Wake up receive process in case of 'SYN+' message */
		newsk->sk_data_ready(newsk);
		conn_put(con);
	}
}

/* tipc_topsrv_listener_data_ready - interrupt callback with connection request
 * The queued job is launched into tipc_topsrv_accept()
 */
static void tipc_topsrv_listener_data_ready(struct sock *sk)
{
	struct tipc_topsrv *srv;

	read_lock_bh(&sk->sk_callback_lock);
	srv = sk->sk_user_data;
	if (srv)
		queue_work(srv->rcv_wq, &srv->awork);
	read_unlock_bh(&sk->sk_callback_lock);
}

static int tipc_topsrv_create_listener(struct tipc_topsrv *srv)
{
	struct socket *lsock = NULL;
	struct sockaddr_tipc saddr;
	struct sock *sk;
	int rc;

	rc = sock_create_kern(srv->net, AF_TIPC, SOCK_SEQPACKET, 0, &lsock);
	if (rc < 0)
		return rc;

	srv->listener = lsock;
	sk = lsock->sk;
	write_lock_bh(&sk->sk_callback_lock);
	sk->sk_data_ready = tipc_topsrv_listener_data_ready;
	sk->sk_user_data = srv;
	write_unlock_bh(&sk->sk_callback_lock);

	lock_sock(sk);
	rc = tsk_set_importance(sk, TIPC_CRITICAL_IMPORTANCE);
	release_sock(sk);
	if (rc < 0)
		goto err;

	saddr.family	                = AF_TIPC;
	saddr.addrtype		        = TIPC_SERVICE_RANGE;
	saddr.addr.nameseq.type	= TIPC_TOP_SRV;
	saddr.addr.nameseq.lower	= TIPC_TOP_SRV;
	saddr.addr.nameseq.upper	= TIPC_TOP_SRV;
	saddr.scope			= TIPC_NODE_SCOPE;

	rc = tipc_sk_bind(lsock, (struct sockaddr *)&saddr, sizeof(saddr));
	if (rc < 0)
		goto err;
	rc = kernel_listen(lsock, 0);
	if (rc < 0)
		goto err;

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
	module_put(lsock->ops->owner);
	module_put(sk->sk_prot_creator->owner);

	return 0;
err:
	sock_release(lsock);
	return -EINVAL;
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
	*(u64 *)&sub.usr_handle = (u64)port;

	con = tipc_conn_alloc(tipc_topsrv(net), NULL);
	if (IS_ERR(con))
		return false;

	*conid = con->conid;
	rc = tipc_conn_rcv_sub(tipc_topsrv(net), con, &sub);
	if (rc)
		conn_put(con);

	conn_put(con);
	return !rc;
}

void tipc_topsrv_kern_unsubscr(struct net *net, int conid)
{
	struct tipc_conn *con;

	con = tipc_conn_lookup(tipc_topsrv(net), conid);
	if (!con)
		return;

	test_and_clear_bit(CF_CONNECTED, &con->flags);
	tipc_conn_delete_sub(con, NULL);
	conn_put(con);
	conn_put(con);
}

static void tipc_topsrv_kern_evt(struct net *net, struct tipc_event *evt)
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
	tipc_loopback_trace(net, &evtq);
	tipc_sk_rcv(net, &evtq);
}

static int tipc_topsrv_work_start(struct tipc_topsrv *s)
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

static void tipc_topsrv_work_stop(struct tipc_topsrv *s)
{
	destroy_workqueue(s->rcv_wq);
	destroy_workqueue(s->send_wq);
}

static int tipc_topsrv_start(struct net *net)
{
	struct tipc_net *tn = tipc_net(net);
	const char name[] = "topology_server";
	struct tipc_topsrv *srv;
	int ret;

	srv = kzalloc(sizeof(*srv), GFP_ATOMIC);
	if (!srv)
		return -ENOMEM;

	srv->net = net;
	INIT_WORK(&srv->awork, tipc_topsrv_accept);

	strscpy(srv->name, name, sizeof(srv->name));
	tn->topsrv = srv;
	atomic_set(&tn->subscription_count, 0);

	spin_lock_init(&srv->idr_lock);
	idr_init(&srv->conn_idr);
	srv->idr_in_use = 0;

	ret = tipc_topsrv_work_start(srv);
	if (ret < 0)
		goto err_start;

	ret = tipc_topsrv_create_listener(srv);
	if (ret < 0)
		goto err_create;

	return 0;

err_create:
	tipc_topsrv_work_stop(srv);
err_start:
	kfree(srv);
	return ret;
}

static void tipc_topsrv_stop(struct net *net)
{
	struct tipc_topsrv *srv = tipc_topsrv(net);
	struct socket *lsock = srv->listener;
	struct tipc_conn *con;
	int id;

	spin_lock_bh(&srv->idr_lock);
	for (id = 0; srv->idr_in_use; id++) {
		con = idr_find(&srv->conn_idr, id);
		if (con) {
			spin_unlock_bh(&srv->idr_lock);
			tipc_conn_close(con);
			spin_lock_bh(&srv->idr_lock);
		}
	}
	__module_get(lsock->ops->owner);
	__module_get(lsock->sk->sk_prot_creator->owner);
	srv->listener = NULL;
	spin_unlock_bh(&srv->idr_lock);

	tipc_topsrv_work_stop(srv);
	sock_release(lsock);
	idr_destroy(&srv->conn_idr);
	kfree(srv);
}

int __net_init tipc_topsrv_init_net(struct net *net)
{
	return tipc_topsrv_start(net);
}

void __net_exit tipc_topsrv_exit_net(struct net *net)
{
	tipc_topsrv_stop(net);
}
