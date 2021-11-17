// SPDX-License-Identifier: GPL-2.0-only
/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2009 Red Hat, Inc.  All rights reserved.
**
**
*******************************************************************************
******************************************************************************/

/*
 * lowcomms.c
 *
 * This is the "low-level" comms layer.
 *
 * It is responsible for sending/receiving messages
 * from other nodes in the cluster.
 *
 * Cluster nodes are referred to by their nodeids. nodeids are
 * simply 32 bit numbers to the locking module - if they need to
 * be expanded for the cluster infrastructure then that is its
 * responsibility. It is this layer's
 * responsibility to resolve these into IP address or
 * whatever it needs for inter-node communication.
 *
 * The comms level is two kernel threads that deal mainly with
 * the receiving of messages from other nodes and passing them
 * up to the mid-level comms layer (which understands the
 * message format) for execution by the locking core, and
 * a send thread which does all the setting up of connections
 * to remote nodes and the sending of data. Threads are not allowed
 * to send their own data because it may cause them to wait in times
 * of high load. Also, this way, the sending thread can collect together
 * messages bound for one node and send them in one block.
 *
 * lowcomms will choose to use either TCP or SCTP as its transport layer
 * depending on the configuration variable 'protocol'. This should be set
 * to 0 (default) for TCP or 1 for SCTP. It should be configured using a
 * cluster-wide mechanism as it must be the same on all nodes of the cluster
 * for the DLM to function.
 *
 */

#include <asm/ioctls.h>
#include <net/sock.h>
#include <net/tcp.h>
#include <linux/pagemap.h>
#include <linux/file.h>
#include <linux/mutex.h>
#include <linux/sctp.h>
#include <linux/slab.h>
#include <net/sctp/sctp.h>
#include <net/ipv6.h>

#include "dlm_internal.h"
#include "lowcomms.h"
#include "midcomms.h"
#include "config.h"

#define NEEDED_RMEM (4*1024*1024)
#define CONN_HASH_SIZE 32

/* Number of messages to send before rescheduling */
#define MAX_SEND_MSG_COUNT 25
#define DLM_SHUTDOWN_WAIT_TIMEOUT msecs_to_jiffies(10000)

struct connection {
	struct socket *sock;	/* NULL if not connected */
	uint32_t nodeid;	/* So we know who we are in the list */
	struct mutex sock_mutex;
	unsigned long flags;
#define CF_READ_PENDING 1
#define CF_WRITE_PENDING 2
#define CF_INIT_PENDING 4
#define CF_IS_OTHERCON 5
#define CF_CLOSE 6
#define CF_APP_LIMITED 7
#define CF_CLOSING 8
#define CF_SHUTDOWN 9
	struct list_head writequeue;  /* List of outgoing writequeue_entries */
	spinlock_t writequeue_lock;
	int (*rx_action) (struct connection *);	/* What to do when active */
	void (*connect_action) (struct connection *);	/* What to do to connect */
	void (*shutdown_action)(struct connection *con); /* What to do to shutdown */
	int retries;
#define MAX_CONNECT_RETRIES 3
	struct hlist_node list;
	struct connection *othercon;
	struct work_struct rwork; /* Receive workqueue */
	struct work_struct swork; /* Send workqueue */
	wait_queue_head_t shutdown_wait; /* wait for graceful shutdown */
	unsigned char *rx_buf;
	int rx_buflen;
	int rx_leftover;
	struct rcu_head rcu;
};
#define sock2con(x) ((struct connection *)(x)->sk_user_data)

/* An entry waiting to be sent */
struct writequeue_entry {
	struct list_head list;
	struct page *page;
	int offset;
	int len;
	int end;
	int users;
	struct connection *con;
};

struct dlm_node_addr {
	struct list_head list;
	int nodeid;
	int addr_count;
	int curr_addr_index;
	struct sockaddr_storage *addr[DLM_MAX_ADDR_COUNT];
};

static struct listen_sock_callbacks {
	void (*sk_error_report)(struct sock *);
	void (*sk_data_ready)(struct sock *);
	void (*sk_state_change)(struct sock *);
	void (*sk_write_space)(struct sock *);
} listen_sock;

static LIST_HEAD(dlm_node_addrs);
static DEFINE_SPINLOCK(dlm_node_addrs_spin);

static struct sockaddr_storage *dlm_local_addr[DLM_MAX_ADDR_COUNT];
static int dlm_local_count;
static int dlm_allow_conn;

/* Work queues */
static struct workqueue_struct *recv_workqueue;
static struct workqueue_struct *send_workqueue;

static struct hlist_head connection_hash[CONN_HASH_SIZE];
static DEFINE_SPINLOCK(connections_lock);
DEFINE_STATIC_SRCU(connections_srcu);

static void process_recv_sockets(struct work_struct *work);
static void process_send_sockets(struct work_struct *work);


/* This is deliberately very simple because most clusters have simple
   sequential nodeids, so we should be able to go straight to a connection
   struct in the array */
static inline int nodeid_hash(int nodeid)
{
	return nodeid & (CONN_HASH_SIZE-1);
}

static struct connection *__find_con(int nodeid)
{
	int r, idx;
	struct connection *con;

	r = nodeid_hash(nodeid);

	idx = srcu_read_lock(&connections_srcu);
	hlist_for_each_entry_rcu(con, &connection_hash[r], list) {
		if (con->nodeid == nodeid) {
			srcu_read_unlock(&connections_srcu, idx);
			return con;
		}
	}
	srcu_read_unlock(&connections_srcu, idx);

	return NULL;
}

/*
 * If 'allocation' is zero then we don't attempt to create a new
 * connection structure for this node.
 */
static struct connection *nodeid2con(int nodeid, gfp_t alloc)
{
	struct connection *con, *tmp;
	int r;

	con = __find_con(nodeid);
	if (con || !alloc)
		return con;

	con = kzalloc(sizeof(*con), alloc);
	if (!con)
		return NULL;

	con->rx_buflen = dlm_config.ci_buffer_size;
	con->rx_buf = kmalloc(con->rx_buflen, GFP_NOFS);
	if (!con->rx_buf) {
		kfree(con);
		return NULL;
	}

	con->nodeid = nodeid;
	mutex_init(&con->sock_mutex);
	INIT_LIST_HEAD(&con->writequeue);
	spin_lock_init(&con->writequeue_lock);
	INIT_WORK(&con->swork, process_send_sockets);
	INIT_WORK(&con->rwork, process_recv_sockets);
	init_waitqueue_head(&con->shutdown_wait);

	/* Setup action pointers for child sockets */
	if (con->nodeid) {
		struct connection *zerocon = __find_con(0);

		con->connect_action = zerocon->connect_action;
		if (!con->rx_action)
			con->rx_action = zerocon->rx_action;
	}

	r = nodeid_hash(nodeid);

	spin_lock(&connections_lock);
	/* Because multiple workqueues/threads calls this function it can
	 * race on multiple cpu's. Instead of locking hot path __find_con()
	 * we just check in rare cases of recently added nodes again
	 * under protection of connections_lock. If this is the case we
	 * abort our connection creation and return the existing connection.
	 */
	tmp = __find_con(nodeid);
	if (tmp) {
		spin_unlock(&connections_lock);
		kfree(con->rx_buf);
		kfree(con);
		return tmp;
	}

	hlist_add_head_rcu(&con->list, &connection_hash[r]);
	spin_unlock(&connections_lock);

	return con;
}

/* Loop round all connections */
static void foreach_conn(void (*conn_func)(struct connection *c))
{
	int i, idx;
	struct connection *con;

	idx = srcu_read_lock(&connections_srcu);
	for (i = 0; i < CONN_HASH_SIZE; i++) {
		hlist_for_each_entry_rcu(con, &connection_hash[i], list)
			conn_func(con);
	}
	srcu_read_unlock(&connections_srcu, idx);
}

static struct dlm_node_addr *find_node_addr(int nodeid)
{
	struct dlm_node_addr *na;

	list_for_each_entry(na, &dlm_node_addrs, list) {
		if (na->nodeid == nodeid)
			return na;
	}
	return NULL;
}

static int addr_compare(struct sockaddr_storage *x, struct sockaddr_storage *y)
{
	switch (x->ss_family) {
	case AF_INET: {
		struct sockaddr_in *sinx = (struct sockaddr_in *)x;
		struct sockaddr_in *siny = (struct sockaddr_in *)y;
		if (sinx->sin_addr.s_addr != siny->sin_addr.s_addr)
			return 0;
		if (sinx->sin_port != siny->sin_port)
			return 0;
		break;
	}
	case AF_INET6: {
		struct sockaddr_in6 *sinx = (struct sockaddr_in6 *)x;
		struct sockaddr_in6 *siny = (struct sockaddr_in6 *)y;
		if (!ipv6_addr_equal(&sinx->sin6_addr, &siny->sin6_addr))
			return 0;
		if (sinx->sin6_port != siny->sin6_port)
			return 0;
		break;
	}
	default:
		return 0;
	}
	return 1;
}

static int nodeid_to_addr(int nodeid, struct sockaddr_storage *sas_out,
			  struct sockaddr *sa_out, bool try_new_addr)
{
	struct sockaddr_storage sas;
	struct dlm_node_addr *na;

	if (!dlm_local_count)
		return -1;

	spin_lock(&dlm_node_addrs_spin);
	na = find_node_addr(nodeid);
	if (na && na->addr_count) {
		memcpy(&sas, na->addr[na->curr_addr_index],
		       sizeof(struct sockaddr_storage));

		if (try_new_addr) {
			na->curr_addr_index++;
			if (na->curr_addr_index == na->addr_count)
				na->curr_addr_index = 0;
		}
	}
	spin_unlock(&dlm_node_addrs_spin);

	if (!na)
		return -EEXIST;

	if (!na->addr_count)
		return -ENOENT;

	if (sas_out)
		memcpy(sas_out, &sas, sizeof(struct sockaddr_storage));

	if (!sa_out)
		return 0;

	if (dlm_local_addr[0]->ss_family == AF_INET) {
		struct sockaddr_in *in4  = (struct sockaddr_in *) &sas;
		struct sockaddr_in *ret4 = (struct sockaddr_in *) sa_out;
		ret4->sin_addr.s_addr = in4->sin_addr.s_addr;
	} else {
		struct sockaddr_in6 *in6  = (struct sockaddr_in6 *) &sas;
		struct sockaddr_in6 *ret6 = (struct sockaddr_in6 *) sa_out;
		ret6->sin6_addr = in6->sin6_addr;
	}

	return 0;
}

static int addr_to_nodeid(struct sockaddr_storage *addr, int *nodeid)
{
	struct dlm_node_addr *na;
	int rv = -EEXIST;
	int addr_i;

	spin_lock(&dlm_node_addrs_spin);
	list_for_each_entry(na, &dlm_node_addrs, list) {
		if (!na->addr_count)
			continue;

		for (addr_i = 0; addr_i < na->addr_count; addr_i++) {
			if (addr_compare(na->addr[addr_i], addr)) {
				*nodeid = na->nodeid;
				rv = 0;
				goto unlock;
			}
		}
	}
unlock:
	spin_unlock(&dlm_node_addrs_spin);
	return rv;
}

int dlm_lowcomms_addr(int nodeid, struct sockaddr_storage *addr, int len)
{
	struct sockaddr_storage *new_addr;
	struct dlm_node_addr *new_node, *na;

	new_node = kzalloc(sizeof(struct dlm_node_addr), GFP_NOFS);
	if (!new_node)
		return -ENOMEM;

	new_addr = kzalloc(sizeof(struct sockaddr_storage), GFP_NOFS);
	if (!new_addr) {
		kfree(new_node);
		return -ENOMEM;
	}

	memcpy(new_addr, addr, len);

	spin_lock(&dlm_node_addrs_spin);
	na = find_node_addr(nodeid);
	if (!na) {
		new_node->nodeid = nodeid;
		new_node->addr[0] = new_addr;
		new_node->addr_count = 1;
		list_add(&new_node->list, &dlm_node_addrs);
		spin_unlock(&dlm_node_addrs_spin);
		return 0;
	}

	if (na->addr_count >= DLM_MAX_ADDR_COUNT) {
		spin_unlock(&dlm_node_addrs_spin);
		kfree(new_addr);
		kfree(new_node);
		return -ENOSPC;
	}

	na->addr[na->addr_count++] = new_addr;
	spin_unlock(&dlm_node_addrs_spin);
	kfree(new_node);
	return 0;
}

/* Data available on socket or listen socket received a connect */
static void lowcomms_data_ready(struct sock *sk)
{
	struct connection *con;

	read_lock_bh(&sk->sk_callback_lock);
	con = sock2con(sk);
	if (con && !test_and_set_bit(CF_READ_PENDING, &con->flags))
		queue_work(recv_workqueue, &con->rwork);
	read_unlock_bh(&sk->sk_callback_lock);
}

static void lowcomms_write_space(struct sock *sk)
{
	struct connection *con;

	read_lock_bh(&sk->sk_callback_lock);
	con = sock2con(sk);
	if (!con)
		goto out;

	clear_bit(SOCK_NOSPACE, &con->sock->flags);

	if (test_and_clear_bit(CF_APP_LIMITED, &con->flags)) {
		con->sock->sk->sk_write_pending--;
		clear_bit(SOCKWQ_ASYNC_NOSPACE, &con->sock->flags);
	}

	queue_work(send_workqueue, &con->swork);
out:
	read_unlock_bh(&sk->sk_callback_lock);
}

static inline void lowcomms_connect_sock(struct connection *con)
{
	if (test_bit(CF_CLOSE, &con->flags))
		return;
	queue_work(send_workqueue, &con->swork);
	cond_resched();
}

static void lowcomms_state_change(struct sock *sk)
{
	/* SCTP layer is not calling sk_data_ready when the connection
	 * is done, so we catch the signal through here. Also, it
	 * doesn't switch socket state when entering shutdown, so we
	 * skip the write in that case.
	 */
	if (sk->sk_shutdown) {
		if (sk->sk_shutdown == RCV_SHUTDOWN)
			lowcomms_data_ready(sk);
	} else if (sk->sk_state == TCP_ESTABLISHED) {
		lowcomms_write_space(sk);
	}
}

int dlm_lowcomms_connect_node(int nodeid)
{
	struct connection *con;

	if (nodeid == dlm_our_nodeid())
		return 0;

	con = nodeid2con(nodeid, GFP_NOFS);
	if (!con)
		return -ENOMEM;
	lowcomms_connect_sock(con);
	return 0;
}

static void lowcomms_error_report(struct sock *sk)
{
	struct connection *con;
	void (*orig_report)(struct sock *) = NULL;
	struct inet_sock *inet;

	read_lock_bh(&sk->sk_callback_lock);
	con = sock2con(sk);
	if (con == NULL)
		goto out;

	orig_report = listen_sock.sk_error_report;

	inet = inet_sk(sk);
	switch (sk->sk_family) {
	case AF_INET:
		printk_ratelimited(KERN_ERR "dlm: node %d: socket error "
				   "sending to node %d at %pI4, dport %d, "
				   "sk_err=%d/%d\n", dlm_our_nodeid(),
				   con->nodeid, &inet->inet_daddr,
				   ntohs(inet->inet_dport), sk->sk_err,
				   sk->sk_err_soft);
		break;
#if IS_ENABLED(CONFIG_IPV6)
	case AF_INET6:
		printk_ratelimited(KERN_ERR "dlm: node %d: socket error "
				   "sending to node %d at %pI6c, "
				   "dport %d, sk_err=%d/%d\n", dlm_our_nodeid(),
				   con->nodeid, &sk->sk_v6_daddr,
				   ntohs(inet->inet_dport), sk->sk_err,
				   sk->sk_err_soft);
		break;
#endif
	default:
		printk_ratelimited(KERN_ERR "dlm: node %d: socket error "
				   "invalid socket family %d set, "
				   "sk_err=%d/%d\n", dlm_our_nodeid(),
				   sk->sk_family, sk->sk_err, sk->sk_err_soft);
		goto out;
	}
out:
	read_unlock_bh(&sk->sk_callback_lock);
	if (orig_report)
		orig_report(sk);
}

/* Note: sk_callback_lock must be locked before calling this function. */
static void save_listen_callbacks(struct socket *sock)
{
	struct sock *sk = sock->sk;

	listen_sock.sk_data_ready = sk->sk_data_ready;
	listen_sock.sk_state_change = sk->sk_state_change;
	listen_sock.sk_write_space = sk->sk_write_space;
	listen_sock.sk_error_report = sk->sk_error_report;
}

static void restore_callbacks(struct socket *sock)
{
	struct sock *sk = sock->sk;

	write_lock_bh(&sk->sk_callback_lock);
	sk->sk_user_data = NULL;
	sk->sk_data_ready = listen_sock.sk_data_ready;
	sk->sk_state_change = listen_sock.sk_state_change;
	sk->sk_write_space = listen_sock.sk_write_space;
	sk->sk_error_report = listen_sock.sk_error_report;
	write_unlock_bh(&sk->sk_callback_lock);
}

/* Make a socket active */
static void add_sock(struct socket *sock, struct connection *con)
{
	struct sock *sk = sock->sk;

	write_lock_bh(&sk->sk_callback_lock);
	con->sock = sock;

	sk->sk_user_data = con;
	/* Install a data_ready callback */
	sk->sk_data_ready = lowcomms_data_ready;
	sk->sk_write_space = lowcomms_write_space;
	sk->sk_state_change = lowcomms_state_change;
	sk->sk_allocation = GFP_NOFS;
	sk->sk_error_report = lowcomms_error_report;
	write_unlock_bh(&sk->sk_callback_lock);
}

/* Add the port number to an IPv6 or 4 sockaddr and return the address
   length */
static void make_sockaddr(struct sockaddr_storage *saddr, uint16_t port,
			  int *addr_len)
{
	saddr->ss_family =  dlm_local_addr[0]->ss_family;
	if (saddr->ss_family == AF_INET) {
		struct sockaddr_in *in4_addr = (struct sockaddr_in *)saddr;
		in4_addr->sin_port = cpu_to_be16(port);
		*addr_len = sizeof(struct sockaddr_in);
		memset(&in4_addr->sin_zero, 0, sizeof(in4_addr->sin_zero));
	} else {
		struct sockaddr_in6 *in6_addr = (struct sockaddr_in6 *)saddr;
		in6_addr->sin6_port = cpu_to_be16(port);
		*addr_len = sizeof(struct sockaddr_in6);
	}
	memset((char *)saddr + *addr_len, 0, sizeof(struct sockaddr_storage) - *addr_len);
}

/* Close a remote connection and tidy up */
static void close_connection(struct connection *con, bool and_other,
			     bool tx, bool rx)
{
	bool closing = test_and_set_bit(CF_CLOSING, &con->flags);

	if (tx && !closing && cancel_work_sync(&con->swork)) {
		log_print("canceled swork for node %d", con->nodeid);
		clear_bit(CF_WRITE_PENDING, &con->flags);
	}
	if (rx && !closing && cancel_work_sync(&con->rwork)) {
		log_print("canceled rwork for node %d", con->nodeid);
		clear_bit(CF_READ_PENDING, &con->flags);
	}

	mutex_lock(&con->sock_mutex);
	if (con->sock) {
		restore_callbacks(con->sock);
		sock_release(con->sock);
		con->sock = NULL;
	}
	if (con->othercon && and_other) {
		/* Will only re-enter once. */
		close_connection(con->othercon, false, tx, rx);
	}

	con->rx_leftover = 0;
	con->retries = 0;
	mutex_unlock(&con->sock_mutex);
	clear_bit(CF_CLOSING, &con->flags);
}

static void shutdown_connection(struct connection *con)
{
	int ret;

	flush_work(&con->swork);

	mutex_lock(&con->sock_mutex);
	/* nothing to shutdown */
	if (!con->sock) {
		mutex_unlock(&con->sock_mutex);
		return;
	}

	set_bit(CF_SHUTDOWN, &con->flags);
	ret = kernel_sock_shutdown(con->sock, SHUT_WR);
	mutex_unlock(&con->sock_mutex);
	if (ret) {
		log_print("Connection %p failed to shutdown: %d will force close",
			  con, ret);
		goto force_close;
	} else {
		ret = wait_event_timeout(con->shutdown_wait,
					 !test_bit(CF_SHUTDOWN, &con->flags),
					 DLM_SHUTDOWN_WAIT_TIMEOUT);
		if (ret == 0) {
			log_print("Connection %p shutdown timed out, will force close",
				  con);
			goto force_close;
		}
	}

	return;

force_close:
	clear_bit(CF_SHUTDOWN, &con->flags);
	close_connection(con, false, true, true);
}

static void dlm_tcp_shutdown(struct connection *con)
{
	if (con->othercon)
		shutdown_connection(con->othercon);
	shutdown_connection(con);
}

static int con_realloc_receive_buf(struct connection *con, int newlen)
{
	unsigned char *newbuf;

	newbuf = kmalloc(newlen, GFP_NOFS);
	if (!newbuf)
		return -ENOMEM;

	/* copy any leftover from last receive */
	if (con->rx_leftover)
		memmove(newbuf, con->rx_buf, con->rx_leftover);

	/* swap to new buffer space */
	kfree(con->rx_buf);
	con->rx_buflen = newlen;
	con->rx_buf = newbuf;

	return 0;
}

/* Data received from remote end */
static int receive_from_sock(struct connection *con)
{
	int call_again_soon = 0;
	struct msghdr msg;
	struct kvec iov;
	int ret, buflen;

	mutex_lock(&con->sock_mutex);

	if (con->sock == NULL) {
		ret = -EAGAIN;
		goto out_close;
	}

	if (con->nodeid == 0) {
		ret = -EINVAL;
		goto out_close;
	}

	/* realloc if we get new buffer size to read out */
	buflen = dlm_config.ci_buffer_size;
	if (con->rx_buflen != buflen && con->rx_leftover <= buflen) {
		ret = con_realloc_receive_buf(con, buflen);
		if (ret < 0)
			goto out_resched;
	}

	/* calculate new buffer parameter regarding last receive and
	 * possible leftover bytes
	 */
	iov.iov_base = con->rx_buf + con->rx_leftover;
	iov.iov_len = con->rx_buflen - con->rx_leftover;

	memset(&msg, 0, sizeof(msg));
	msg.msg_flags = MSG_DONTWAIT | MSG_NOSIGNAL;
	ret = kernel_recvmsg(con->sock, &msg, &iov, 1, iov.iov_len,
			     msg.msg_flags);
	if (ret <= 0)
		goto out_close;
	else if (ret == iov.iov_len)
		call_again_soon = 1;

	/* new buflen according readed bytes and leftover from last receive */
	buflen = ret + con->rx_leftover;
	ret = dlm_process_incoming_buffer(con->nodeid, con->rx_buf, buflen);
	if (ret < 0)
		goto out_close;

	/* calculate leftover bytes from process and put it into begin of
	 * the receive buffer, so next receive we have the full message
	 * at the start address of the receive buffer.
	 */
	con->rx_leftover = buflen - ret;
	if (con->rx_leftover) {
		memmove(con->rx_buf, con->rx_buf + ret,
			con->rx_leftover);
		call_again_soon = true;
	}

	if (call_again_soon)
		goto out_resched;

	mutex_unlock(&con->sock_mutex);
	return 0;

out_resched:
	if (!test_and_set_bit(CF_READ_PENDING, &con->flags))
		queue_work(recv_workqueue, &con->rwork);
	mutex_unlock(&con->sock_mutex);
	return -EAGAIN;

out_close:
	mutex_unlock(&con->sock_mutex);
	if (ret != -EAGAIN) {
		/* Reconnect when there is something to send */
		close_connection(con, false, true, false);
		if (ret == 0) {
			log_print("connection %p got EOF from %d",
				  con, con->nodeid);
			/* handling for tcp shutdown */
			clear_bit(CF_SHUTDOWN, &con->flags);
			wake_up(&con->shutdown_wait);
			/* signal to breaking receive worker */
			ret = -1;
		}
	}
	return ret;
}

/* Listening socket is busy, accept a connection */
static int accept_from_sock(struct connection *con)
{
	int result;
	struct sockaddr_storage peeraddr;
	struct socket *newsock;
	int len;
	int nodeid;
	struct connection *newcon;
	struct connection *addcon;
	unsigned int mark;

	if (!dlm_allow_conn) {
		return -1;
	}

	mutex_lock_nested(&con->sock_mutex, 0);

	if (!con->sock) {
		mutex_unlock(&con->sock_mutex);
		return -ENOTCONN;
	}

	result = kernel_accept(con->sock, &newsock, O_NONBLOCK);
	if (result < 0)
		goto accept_err;

	/* Get the connected socket's peer */
	memset(&peeraddr, 0, sizeof(peeraddr));
	len = newsock->ops->getname(newsock, (struct sockaddr *)&peeraddr, 2);
	if (len < 0) {
		result = -ECONNABORTED;
		goto accept_err;
	}

	/* Get the new node's NODEID */
	make_sockaddr(&peeraddr, 0, &len);
	if (addr_to_nodeid(&peeraddr, &nodeid)) {
		unsigned char *b=(unsigned char *)&peeraddr;
		log_print("connect from non cluster node");
		print_hex_dump_bytes("ss: ", DUMP_PREFIX_NONE, 
				     b, sizeof(struct sockaddr_storage));
		sock_release(newsock);
		mutex_unlock(&con->sock_mutex);
		return -1;
	}

	dlm_comm_mark(nodeid, &mark);
	sock_set_mark(newsock->sk, mark);

	log_print("got connection from %d", nodeid);

	/*  Check to see if we already have a connection to this node. This
	 *  could happen if the two nodes initiate a connection at roughly
	 *  the same time and the connections cross on the wire.
	 *  In this case we store the incoming one in "othercon"
	 */
	newcon = nodeid2con(nodeid, GFP_NOFS);
	if (!newcon) {
		result = -ENOMEM;
		goto accept_err;
	}
	mutex_lock_nested(&newcon->sock_mutex, 1);
	if (newcon->sock) {
		struct connection *othercon = newcon->othercon;

		if (!othercon) {
			othercon = kzalloc(sizeof(*othercon), GFP_NOFS);
			if (!othercon) {
				log_print("failed to allocate incoming socket");
				mutex_unlock(&newcon->sock_mutex);
				result = -ENOMEM;
				goto accept_err;
			}

			othercon->rx_buflen = dlm_config.ci_buffer_size;
			othercon->rx_buf = kmalloc(othercon->rx_buflen, GFP_NOFS);
			if (!othercon->rx_buf) {
				mutex_unlock(&newcon->sock_mutex);
				kfree(othercon);
				log_print("failed to allocate incoming socket receive buffer");
				result = -ENOMEM;
				goto accept_err;
			}

			othercon->nodeid = nodeid;
			othercon->rx_action = receive_from_sock;
			mutex_init(&othercon->sock_mutex);
			INIT_LIST_HEAD(&othercon->writequeue);
			spin_lock_init(&othercon->writequeue_lock);
			INIT_WORK(&othercon->swork, process_send_sockets);
			INIT_WORK(&othercon->rwork, process_recv_sockets);
			init_waitqueue_head(&othercon->shutdown_wait);
			set_bit(CF_IS_OTHERCON, &othercon->flags);
		} else {
			/* close other sock con if we have something new */
			close_connection(othercon, false, true, false);
		}

		mutex_lock_nested(&othercon->sock_mutex, 2);
		newcon->othercon = othercon;
		add_sock(newsock, othercon);
		addcon = othercon;
		mutex_unlock(&othercon->sock_mutex);
	}
	else {
		newcon->rx_action = receive_from_sock;
		/* accept copies the sk after we've saved the callbacks, so we
		   don't want to save them a second time or comm errors will
		   result in calling sk_error_report recursively. */
		add_sock(newsock, newcon);
		addcon = newcon;
	}

	mutex_unlock(&newcon->sock_mutex);

	/*
	 * Add it to the active queue in case we got data
	 * between processing the accept adding the socket
	 * to the read_sockets list
	 */
	if (!test_and_set_bit(CF_READ_PENDING, &addcon->flags))
		queue_work(recv_workqueue, &addcon->rwork);
	mutex_unlock(&con->sock_mutex);

	return 0;

accept_err:
	mutex_unlock(&con->sock_mutex);
	if (newsock)
		sock_release(newsock);

	if (result != -EAGAIN)
		log_print("error accepting connection from node: %d", result);
	return result;
}

static void free_entry(struct writequeue_entry *e)
{
	__free_page(e->page);
	kfree(e);
}

/*
 * writequeue_entry_complete - try to delete and free write queue entry
 * @e: write queue entry to try to delete
 * @completed: bytes completed
 *
 * writequeue_lock must be held.
 */
static void writequeue_entry_complete(struct writequeue_entry *e, int completed)
{
	e->offset += completed;
	e->len -= completed;

	if (e->len == 0 && e->users == 0) {
		list_del(&e->list);
		free_entry(e);
	}
}

/*
 * sctp_bind_addrs - bind a SCTP socket to all our addresses
 */
static int sctp_bind_addrs(struct connection *con, uint16_t port)
{
	struct sockaddr_storage localaddr;
	struct sockaddr *addr = (struct sockaddr *)&localaddr;
	int i, addr_len, result = 0;

	for (i = 0; i < dlm_local_count; i++) {
		memcpy(&localaddr, dlm_local_addr[i], sizeof(localaddr));
		make_sockaddr(&localaddr, port, &addr_len);

		if (!i)
			result = kernel_bind(con->sock, addr, addr_len);
		else
			result = sock_bind_add(con->sock->sk, addr, addr_len);

		if (result < 0) {
			log_print("Can't bind to %d addr number %d, %d.\n",
				  port, i + 1, result);
			break;
		}
	}
	return result;
}

/* Initiate an SCTP association.
   This is a special case of send_to_sock() in that we don't yet have a
   peeled-off socket for this association, so we use the listening socket
   and add the primary IP address of the remote node.
 */
static void sctp_connect_to_sock(struct connection *con)
{
	struct sockaddr_storage daddr;
	int result;
	int addr_len;
	struct socket *sock;
	unsigned int mark;

	if (con->nodeid == 0) {
		log_print("attempt to connect sock 0 foiled");
		return;
	}

	dlm_comm_mark(con->nodeid, &mark);

	mutex_lock(&con->sock_mutex);

	/* Some odd races can cause double-connects, ignore them */
	if (con->retries++ > MAX_CONNECT_RETRIES)
		goto out;

	if (con->sock) {
		log_print("node %d already connected.", con->nodeid);
		goto out;
	}

	memset(&daddr, 0, sizeof(daddr));
	result = nodeid_to_addr(con->nodeid, &daddr, NULL, true);
	if (result < 0) {
		log_print("no address for nodeid %d", con->nodeid);
		goto out;
	}

	/* Create a socket to communicate with */
	result = sock_create_kern(&init_net, dlm_local_addr[0]->ss_family,
				  SOCK_STREAM, IPPROTO_SCTP, &sock);
	if (result < 0)
		goto socket_err;

	sock_set_mark(sock->sk, mark);

	con->rx_action = receive_from_sock;
	con->connect_action = sctp_connect_to_sock;
	add_sock(sock, con);

	/* Bind to all addresses. */
	if (sctp_bind_addrs(con, 0))
		goto bind_err;

	make_sockaddr(&daddr, dlm_config.ci_tcp_port, &addr_len);

	log_print("connecting to %d", con->nodeid);

	/* Turn off Nagle's algorithm */
	sctp_sock_set_nodelay(sock->sk);

	/*
	 * Make sock->ops->connect() function return in specified time,
	 * since O_NONBLOCK argument in connect() function does not work here,
	 * then, we should restore the default value of this attribute.
	 */
	sock_set_sndtimeo(sock->sk, 5);
	result = sock->ops->connect(sock, (struct sockaddr *)&daddr, addr_len,
				   0);
	sock_set_sndtimeo(sock->sk, 0);

	if (result == -EINPROGRESS)
		result = 0;
	if (result == 0)
		goto out;

bind_err:
	con->sock = NULL;
	sock_release(sock);

socket_err:
	/*
	 * Some errors are fatal and this list might need adjusting. For other
	 * errors we try again until the max number of retries is reached.
	 */
	if (result != -EHOSTUNREACH &&
	    result != -ENETUNREACH &&
	    result != -ENETDOWN &&
	    result != -EINVAL &&
	    result != -EPROTONOSUPPORT) {
		log_print("connect %d try %d error %d", con->nodeid,
			  con->retries, result);
		mutex_unlock(&con->sock_mutex);
		msleep(1000);
		lowcomms_connect_sock(con);
		return;
	}

out:
	mutex_unlock(&con->sock_mutex);
}

/* Connect a new socket to its peer */
static void tcp_connect_to_sock(struct connection *con)
{
	struct sockaddr_storage saddr, src_addr;
	int addr_len;
	struct socket *sock = NULL;
	unsigned int mark;
	int result;

	if (con->nodeid == 0) {
		log_print("attempt to connect sock 0 foiled");
		return;
	}

	dlm_comm_mark(con->nodeid, &mark);

	mutex_lock(&con->sock_mutex);
	if (con->retries++ > MAX_CONNECT_RETRIES)
		goto out;

	/* Some odd races can cause double-connects, ignore them */
	if (con->sock)
		goto out;

	/* Create a socket to communicate with */
	result = sock_create_kern(&init_net, dlm_local_addr[0]->ss_family,
				  SOCK_STREAM, IPPROTO_TCP, &sock);
	if (result < 0)
		goto out_err;

	sock_set_mark(sock->sk, mark);

	memset(&saddr, 0, sizeof(saddr));
	result = nodeid_to_addr(con->nodeid, &saddr, NULL, false);
	if (result < 0) {
		log_print("no address for nodeid %d", con->nodeid);
		goto out_err;
	}

	con->rx_action = receive_from_sock;
	con->connect_action = tcp_connect_to_sock;
	con->shutdown_action = dlm_tcp_shutdown;
	add_sock(sock, con);

	/* Bind to our cluster-known address connecting to avoid
	   routing problems */
	memcpy(&src_addr, dlm_local_addr[0], sizeof(src_addr));
	make_sockaddr(&src_addr, 0, &addr_len);
	result = sock->ops->bind(sock, (struct sockaddr *) &src_addr,
				 addr_len);
	if (result < 0) {
		log_print("could not bind for connect: %d", result);
		/* This *may* not indicate a critical error */
	}

	make_sockaddr(&saddr, dlm_config.ci_tcp_port, &addr_len);

	log_print("connecting to %d", con->nodeid);

	/* Turn off Nagle's algorithm */
	tcp_sock_set_nodelay(sock->sk);

	result = sock->ops->connect(sock, (struct sockaddr *)&saddr, addr_len,
				   O_NONBLOCK);
	if (result == -EINPROGRESS)
		result = 0;
	if (result == 0)
		goto out;

out_err:
	if (con->sock) {
		sock_release(con->sock);
		con->sock = NULL;
	} else if (sock) {
		sock_release(sock);
	}
	/*
	 * Some errors are fatal and this list might need adjusting. For other
	 * errors we try again until the max number of retries is reached.
	 */
	if (result != -EHOSTUNREACH &&
	    result != -ENETUNREACH &&
	    result != -ENETDOWN && 
	    result != -EINVAL &&
	    result != -EPROTONOSUPPORT) {
		log_print("connect %d try %d error %d", con->nodeid,
			  con->retries, result);
		mutex_unlock(&con->sock_mutex);
		msleep(1000);
		lowcomms_connect_sock(con);
		return;
	}
out:
	mutex_unlock(&con->sock_mutex);
	return;
}

static struct socket *tcp_create_listen_sock(struct connection *con,
					     struct sockaddr_storage *saddr)
{
	struct socket *sock = NULL;
	int result = 0;
	int addr_len;

	if (dlm_local_addr[0]->ss_family == AF_INET)
		addr_len = sizeof(struct sockaddr_in);
	else
		addr_len = sizeof(struct sockaddr_in6);

	/* Create a socket to communicate with */
	result = sock_create_kern(&init_net, dlm_local_addr[0]->ss_family,
				  SOCK_STREAM, IPPROTO_TCP, &sock);
	if (result < 0) {
		log_print("Can't create listening comms socket");
		goto create_out;
	}

	sock_set_mark(sock->sk, dlm_config.ci_mark);

	/* Turn off Nagle's algorithm */
	tcp_sock_set_nodelay(sock->sk);

	sock_set_reuseaddr(sock->sk);

	write_lock_bh(&sock->sk->sk_callback_lock);
	sock->sk->sk_user_data = con;
	save_listen_callbacks(sock);
	con->rx_action = accept_from_sock;
	con->connect_action = tcp_connect_to_sock;
	write_unlock_bh(&sock->sk->sk_callback_lock);

	/* Bind to our port */
	make_sockaddr(saddr, dlm_config.ci_tcp_port, &addr_len);
	result = sock->ops->bind(sock, (struct sockaddr *) saddr, addr_len);
	if (result < 0) {
		log_print("Can't bind to port %d", dlm_config.ci_tcp_port);
		sock_release(sock);
		sock = NULL;
		con->sock = NULL;
		goto create_out;
	}
	sock_set_keepalive(sock->sk);

	result = sock->ops->listen(sock, 5);
	if (result < 0) {
		log_print("Can't listen on port %d", dlm_config.ci_tcp_port);
		sock_release(sock);
		sock = NULL;
		goto create_out;
	}

create_out:
	return sock;
}

/* Get local addresses */
static void init_local(void)
{
	struct sockaddr_storage sas, *addr;
	int i;

	dlm_local_count = 0;
	for (i = 0; i < DLM_MAX_ADDR_COUNT; i++) {
		if (dlm_our_addr(&sas, i))
			break;

		addr = kmemdup(&sas, sizeof(*addr), GFP_NOFS);
		if (!addr)
			break;
		dlm_local_addr[dlm_local_count++] = addr;
	}
}

static void deinit_local(void)
{
	int i;

	for (i = 0; i < dlm_local_count; i++)
		kfree(dlm_local_addr[i]);
}

/* Initialise SCTP socket and bind to all interfaces */
static int sctp_listen_for_all(void)
{
	struct socket *sock = NULL;
	int result = -EINVAL;
	struct connection *con = nodeid2con(0, GFP_NOFS);

	if (!con)
		return -ENOMEM;

	log_print("Using SCTP for communications");

	result = sock_create_kern(&init_net, dlm_local_addr[0]->ss_family,
				  SOCK_STREAM, IPPROTO_SCTP, &sock);
	if (result < 0) {
		log_print("Can't create comms socket, check SCTP is loaded");
		goto out;
	}

	sock_set_rcvbuf(sock->sk, NEEDED_RMEM);
	sock_set_mark(sock->sk, dlm_config.ci_mark);
	sctp_sock_set_nodelay(sock->sk);

	write_lock_bh(&sock->sk->sk_callback_lock);
	/* Init con struct */
	sock->sk->sk_user_data = con;
	save_listen_callbacks(sock);
	con->sock = sock;
	con->sock->sk->sk_data_ready = lowcomms_data_ready;
	con->rx_action = accept_from_sock;
	con->connect_action = sctp_connect_to_sock;

	write_unlock_bh(&sock->sk->sk_callback_lock);

	/* Bind to all addresses. */
	if (sctp_bind_addrs(con, dlm_config.ci_tcp_port))
		goto create_delsock;

	result = sock->ops->listen(sock, 5);
	if (result < 0) {
		log_print("Can't set socket listening");
		goto create_delsock;
	}

	return 0;

create_delsock:
	sock_release(sock);
	con->sock = NULL;
out:
	return result;
}

static int tcp_listen_for_all(void)
{
	struct socket *sock = NULL;
	struct connection *con = nodeid2con(0, GFP_NOFS);
	int result = -EINVAL;

	if (!con)
		return -ENOMEM;

	/* We don't support multi-homed hosts */
	if (dlm_local_addr[1] != NULL) {
		log_print("TCP protocol can't handle multi-homed hosts, "
			  "try SCTP");
		return -EINVAL;
	}

	log_print("Using TCP for communications");

	sock = tcp_create_listen_sock(con, dlm_local_addr[0]);
	if (sock) {
		add_sock(sock, con);
		result = 0;
	}
	else {
		result = -EADDRINUSE;
	}

	return result;
}



static struct writequeue_entry *new_writequeue_entry(struct connection *con,
						     gfp_t allocation)
{
	struct writequeue_entry *entry;

	entry = kmalloc(sizeof(struct writequeue_entry), allocation);
	if (!entry)
		return NULL;

	entry->page = alloc_page(allocation);
	if (!entry->page) {
		kfree(entry);
		return NULL;
	}

	entry->offset = 0;
	entry->len = 0;
	entry->end = 0;
	entry->users = 0;
	entry->con = con;

	return entry;
}

void *dlm_lowcomms_get_buffer(int nodeid, int len, gfp_t allocation, char **ppc)
{
	struct connection *con;
	struct writequeue_entry *e;
	int offset = 0;

	con = nodeid2con(nodeid, allocation);
	if (!con)
		return NULL;

	spin_lock(&con->writequeue_lock);
	e = list_entry(con->writequeue.prev, struct writequeue_entry, list);
	if ((&e->list == &con->writequeue) ||
	    (PAGE_SIZE - e->end < len)) {
		e = NULL;
	} else {
		offset = e->end;
		e->end += len;
		e->users++;
	}
	spin_unlock(&con->writequeue_lock);

	if (e) {
	got_one:
		*ppc = page_address(e->page) + offset;
		return e;
	}

	e = new_writequeue_entry(con, allocation);
	if (e) {
		spin_lock(&con->writequeue_lock);
		offset = e->end;
		e->end += len;
		e->users++;
		list_add_tail(&e->list, &con->writequeue);
		spin_unlock(&con->writequeue_lock);
		goto got_one;
	}
	return NULL;
}

void dlm_lowcomms_commit_buffer(void *mh)
{
	struct writequeue_entry *e = (struct writequeue_entry *)mh;
	struct connection *con = e->con;
	int users;

	spin_lock(&con->writequeue_lock);
	users = --e->users;
	if (users)
		goto out;
	e->len = e->end - e->offset;
	spin_unlock(&con->writequeue_lock);

	queue_work(send_workqueue, &con->swork);
	return;

out:
	spin_unlock(&con->writequeue_lock);
	return;
}

/* Send a message */
static void send_to_sock(struct connection *con)
{
	int ret = 0;
	const int msg_flags = MSG_DONTWAIT | MSG_NOSIGNAL;
	struct writequeue_entry *e;
	int len, offset;
	int count = 0;

	mutex_lock(&con->sock_mutex);
	if (con->sock == NULL)
		goto out_connect;

	spin_lock(&con->writequeue_lock);
	for (;;) {
		e = list_entry(con->writequeue.next, struct writequeue_entry,
			       list);
		if ((struct list_head *) e == &con->writequeue)
			break;

		len = e->len;
		offset = e->offset;
		BUG_ON(len == 0 && e->users == 0);
		spin_unlock(&con->writequeue_lock);

		ret = 0;
		if (len) {
			ret = kernel_sendpage(con->sock, e->page, offset, len,
					      msg_flags);
			if (ret == -EAGAIN || ret == 0) {
				if (ret == -EAGAIN &&
				    test_bit(SOCKWQ_ASYNC_NOSPACE, &con->sock->flags) &&
				    !test_and_set_bit(CF_APP_LIMITED, &con->flags)) {
					/* Notify TCP that we're limited by the
					 * application window size.
					 */
					set_bit(SOCK_NOSPACE, &con->sock->flags);
					con->sock->sk->sk_write_pending++;
				}
				cond_resched();
				goto out;
			} else if (ret < 0)
				goto send_error;
		}

		/* Don't starve people filling buffers */
		if (++count >= MAX_SEND_MSG_COUNT) {
			cond_resched();
			count = 0;
		}

		spin_lock(&con->writequeue_lock);
		writequeue_entry_complete(e, ret);
	}
	spin_unlock(&con->writequeue_lock);
out:
	mutex_unlock(&con->sock_mutex);
	return;

send_error:
	mutex_unlock(&con->sock_mutex);
	close_connection(con, false, false, true);
	/* Requeue the send work. When the work daemon runs again, it will try
	   a new connection, then call this function again. */
	queue_work(send_workqueue, &con->swork);
	return;

out_connect:
	mutex_unlock(&con->sock_mutex);
	queue_work(send_workqueue, &con->swork);
	cond_resched();
}

static void clean_one_writequeue(struct connection *con)
{
	struct writequeue_entry *e, *safe;

	spin_lock(&con->writequeue_lock);
	list_for_each_entry_safe(e, safe, &con->writequeue, list) {
		list_del(&e->list);
		free_entry(e);
	}
	spin_unlock(&con->writequeue_lock);
}

/* Called from recovery when it knows that a node has
   left the cluster */
int dlm_lowcomms_close(int nodeid)
{
	struct connection *con;
	struct dlm_node_addr *na;

	log_print("closing connection to node %d", nodeid);
	con = nodeid2con(nodeid, 0);
	if (con) {
		set_bit(CF_CLOSE, &con->flags);
		close_connection(con, true, true, true);
		clean_one_writequeue(con);
	}

	spin_lock(&dlm_node_addrs_spin);
	na = find_node_addr(nodeid);
	if (na) {
		list_del(&na->list);
		while (na->addr_count--)
			kfree(na->addr[na->addr_count]);
		kfree(na);
	}
	spin_unlock(&dlm_node_addrs_spin);

	return 0;
}

/* Receive workqueue function */
static void process_recv_sockets(struct work_struct *work)
{
	struct connection *con = container_of(work, struct connection, rwork);
	int err;

	clear_bit(CF_READ_PENDING, &con->flags);
	do {
		err = con->rx_action(con);
	} while (!err);
}

/* Send workqueue function */
static void process_send_sockets(struct work_struct *work)
{
	struct connection *con = container_of(work, struct connection, swork);

	clear_bit(CF_WRITE_PENDING, &con->flags);
	if (con->sock == NULL) /* not mutex protected so check it inside too */
		con->connect_action(con);
	if (!list_empty(&con->writequeue))
		send_to_sock(con);
}

static void work_stop(void)
{
	if (recv_workqueue)
		destroy_workqueue(recv_workqueue);
	if (send_workqueue)
		destroy_workqueue(send_workqueue);
}

static int work_start(void)
{
	recv_workqueue = alloc_workqueue("dlm_recv",
					 WQ_UNBOUND | WQ_MEM_RECLAIM, 1);
	if (!recv_workqueue) {
		log_print("can't start dlm_recv");
		return -ENOMEM;
	}

	send_workqueue = alloc_workqueue("dlm_send",
					 WQ_UNBOUND | WQ_MEM_RECLAIM, 1);
	if (!send_workqueue) {
		log_print("can't start dlm_send");
		destroy_workqueue(recv_workqueue);
		return -ENOMEM;
	}

	return 0;
}

static void _stop_conn(struct connection *con, bool and_other)
{
	mutex_lock(&con->sock_mutex);
	set_bit(CF_CLOSE, &con->flags);
	set_bit(CF_READ_PENDING, &con->flags);
	set_bit(CF_WRITE_PENDING, &con->flags);
	if (con->sock && con->sock->sk) {
		write_lock_bh(&con->sock->sk->sk_callback_lock);
		con->sock->sk->sk_user_data = NULL;
		write_unlock_bh(&con->sock->sk->sk_callback_lock);
	}
	if (con->othercon && and_other)
		_stop_conn(con->othercon, false);
	mutex_unlock(&con->sock_mutex);
}

static void stop_conn(struct connection *con)
{
	_stop_conn(con, true);
}

static void shutdown_conn(struct connection *con)
{
	if (con->shutdown_action)
		con->shutdown_action(con);
}

static void connection_release(struct rcu_head *rcu)
{
	struct connection *con = container_of(rcu, struct connection, rcu);

	kfree(con->rx_buf);
	kfree(con);
}

static void free_conn(struct connection *con)
{
	close_connection(con, true, true, true);
	spin_lock(&connections_lock);
	hlist_del_rcu(&con->list);
	spin_unlock(&connections_lock);
	if (con->othercon) {
		clean_one_writequeue(con->othercon);
		call_rcu(&con->othercon->rcu, connection_release);
	}
	clean_one_writequeue(con);
	call_rcu(&con->rcu, connection_release);
}

static void work_flush(void)
{
	int ok, idx;
	int i;
	struct connection *con;

	do {
		ok = 1;
		foreach_conn(stop_conn);
		if (recv_workqueue)
			flush_workqueue(recv_workqueue);
		if (send_workqueue)
			flush_workqueue(send_workqueue);
		idx = srcu_read_lock(&connections_srcu);
		for (i = 0; i < CONN_HASH_SIZE && ok; i++) {
			hlist_for_each_entry_rcu(con, &connection_hash[i],
						 list) {
				ok &= test_bit(CF_READ_PENDING, &con->flags);
				ok &= test_bit(CF_WRITE_PENDING, &con->flags);
				if (con->othercon) {
					ok &= test_bit(CF_READ_PENDING,
						       &con->othercon->flags);
					ok &= test_bit(CF_WRITE_PENDING,
						       &con->othercon->flags);
				}
			}
		}
		srcu_read_unlock(&connections_srcu, idx);
	} while (!ok);
}

void dlm_lowcomms_stop(void)
{
	/* Set all the flags to prevent any
	   socket activity.
	*/
	dlm_allow_conn = 0;

	if (recv_workqueue)
		flush_workqueue(recv_workqueue);
	if (send_workqueue)
		flush_workqueue(send_workqueue);

	foreach_conn(shutdown_conn);
	work_flush();
	foreach_conn(free_conn);
	work_stop();
	deinit_local();
}

int dlm_lowcomms_start(void)
{
	int error = -EINVAL;
	struct connection *con;
	int i;

	for (i = 0; i < CONN_HASH_SIZE; i++)
		INIT_HLIST_HEAD(&connection_hash[i]);

	init_local();
	if (!dlm_local_count) {
		error = -ENOTCONN;
		log_print("no local IP address has been set");
		goto fail;
	}

	error = work_start();
	if (error)
		goto fail;

	dlm_allow_conn = 1;

	/* Start listening */
	if (dlm_config.ci_protocol == 0)
		error = tcp_listen_for_all();
	else
		error = sctp_listen_for_all();
	if (error)
		goto fail_unlisten;

	return 0;

fail_unlisten:
	dlm_allow_conn = 0;
	con = nodeid2con(0,0);
	if (con)
		free_conn(con);
fail:
	return error;
}

void dlm_lowcomms_exit(void)
{
	struct dlm_node_addr *na, *safe;

	spin_lock(&dlm_node_addrs_spin);
	list_for_each_entry_safe(na, safe, &dlm_node_addrs, list) {
		list_del(&na->list);
		while (na->addr_count--)
			kfree(na->addr[na->addr_count]);
		kfree(na);
	}
	spin_unlock(&dlm_node_addrs_spin);
}
