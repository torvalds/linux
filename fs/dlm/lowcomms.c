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

#include <trace/events/dlm.h>

#include "dlm_internal.h"
#include "lowcomms.h"
#include "midcomms.h"
#include "config.h"

#define NEEDED_RMEM (4*1024*1024)

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
#define CF_CONNECTED 10
#define CF_RECONNECT 11
#define CF_DELAY_CONNECT 12
#define CF_EOF 13
	struct list_head writequeue;  /* List of outgoing writequeue_entries */
	spinlock_t writequeue_lock;
	atomic_t writequeue_cnt;
	struct mutex wq_alloc;
	int retries;
#define MAX_CONNECT_RETRIES 3
	struct hlist_node list;
	struct connection *othercon;
	struct connection *sendcon;
	struct work_struct rwork; /* Receive workqueue */
	struct work_struct swork; /* Send workqueue */
	wait_queue_head_t shutdown_wait; /* wait for graceful shutdown */
	unsigned char *rx_buf;
	int rx_buflen;
	int rx_leftover;
	struct rcu_head rcu;
};
#define sock2con(x) ((struct connection *)(x)->sk_user_data)

struct listen_connection {
	struct socket *sock;
	struct work_struct rwork;
};

#define DLM_WQ_REMAIN_BYTES(e) (PAGE_SIZE - e->end)
#define DLM_WQ_LENGTH_BYTES(e) (e->end - e->offset)

/* An entry waiting to be sent */
struct writequeue_entry {
	struct list_head list;
	struct page *page;
	int offset;
	int len;
	int end;
	int users;
	bool dirty;
	struct connection *con;
	struct list_head msgs;
	struct kref ref;
};

struct dlm_msg {
	struct writequeue_entry *entry;
	struct dlm_msg *orig_msg;
	bool retransmit;
	void *ppc;
	int len;
	int idx; /* new()/commit() idx exchange */

	struct list_head list;
	struct kref ref;
};

struct dlm_node_addr {
	struct list_head list;
	int nodeid;
	int mark;
	int addr_count;
	int curr_addr_index;
	struct sockaddr_storage *addr[DLM_MAX_ADDR_COUNT];
};

struct dlm_proto_ops {
	bool try_new_addr;
	const char *name;
	int proto;

	int (*connect)(struct connection *con, struct socket *sock,
		       struct sockaddr *addr, int addr_len);
	void (*sockopts)(struct socket *sock);
	int (*bind)(struct socket *sock);
	int (*listen_validate)(void);
	void (*listen_sockopts)(struct socket *sock);
	int (*listen_bind)(struct socket *sock);
	/* What to do to shutdown */
	void (*shutdown_action)(struct connection *con);
	/* What to do to eof check */
	bool (*eof_condition)(struct connection *con);
};

static struct listen_sock_callbacks {
	void (*sk_error_report)(struct sock *);
	void (*sk_data_ready)(struct sock *);
	void (*sk_state_change)(struct sock *);
	void (*sk_write_space)(struct sock *);
} listen_sock;

static LIST_HEAD(dlm_node_addrs);
static DEFINE_SPINLOCK(dlm_node_addrs_spin);

static struct listen_connection listen_con;
static struct sockaddr_storage *dlm_local_addr[DLM_MAX_ADDR_COUNT];
static int dlm_local_count;
int dlm_allow_conn;

/* Work queues */
static struct workqueue_struct *recv_workqueue;
static struct workqueue_struct *send_workqueue;

static struct hlist_head connection_hash[CONN_HASH_SIZE];
static DEFINE_SPINLOCK(connections_lock);
DEFINE_STATIC_SRCU(connections_srcu);

static const struct dlm_proto_ops *dlm_proto_ops;

static void process_recv_sockets(struct work_struct *work);
static void process_send_sockets(struct work_struct *work);

/* need to held writequeue_lock */
static struct writequeue_entry *con_next_wq(struct connection *con)
{
	struct writequeue_entry *e;

	if (list_empty(&con->writequeue))
		return NULL;

	e = list_first_entry(&con->writequeue, struct writequeue_entry,
			     list);
	if (e->len == 0)
		return NULL;

	return e;
}

static struct connection *__find_con(int nodeid, int r)
{
	struct connection *con;

	hlist_for_each_entry_rcu(con, &connection_hash[r], list) {
		if (con->nodeid == nodeid)
			return con;
	}

	return NULL;
}

static bool tcp_eof_condition(struct connection *con)
{
	return atomic_read(&con->writequeue_cnt);
}

static int dlm_con_init(struct connection *con, int nodeid)
{
	con->rx_buflen = dlm_config.ci_buffer_size;
	con->rx_buf = kmalloc(con->rx_buflen, GFP_NOFS);
	if (!con->rx_buf)
		return -ENOMEM;

	con->nodeid = nodeid;
	mutex_init(&con->sock_mutex);
	INIT_LIST_HEAD(&con->writequeue);
	spin_lock_init(&con->writequeue_lock);
	atomic_set(&con->writequeue_cnt, 0);
	INIT_WORK(&con->swork, process_send_sockets);
	INIT_WORK(&con->rwork, process_recv_sockets);
	init_waitqueue_head(&con->shutdown_wait);

	return 0;
}

/*
 * If 'allocation' is zero then we don't attempt to create a new
 * connection structure for this node.
 */
static struct connection *nodeid2con(int nodeid, gfp_t alloc)
{
	struct connection *con, *tmp;
	int r, ret;

	r = nodeid_hash(nodeid);
	con = __find_con(nodeid, r);
	if (con || !alloc)
		return con;

	con = kzalloc(sizeof(*con), alloc);
	if (!con)
		return NULL;

	ret = dlm_con_init(con, nodeid);
	if (ret) {
		kfree(con);
		return NULL;
	}

	mutex_init(&con->wq_alloc);

	spin_lock(&connections_lock);
	/* Because multiple workqueues/threads calls this function it can
	 * race on multiple cpu's. Instead of locking hot path __find_con()
	 * we just check in rare cases of recently added nodes again
	 * under protection of connections_lock. If this is the case we
	 * abort our connection creation and return the existing connection.
	 */
	tmp = __find_con(nodeid, r);
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
	int i;
	struct connection *con;

	for (i = 0; i < CONN_HASH_SIZE; i++) {
		hlist_for_each_entry_rcu(con, &connection_hash[i], list)
			conn_func(con);
	}
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

static int addr_compare(const struct sockaddr_storage *x,
			const struct sockaddr_storage *y)
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
			  struct sockaddr *sa_out, bool try_new_addr,
			  unsigned int *mark)
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

	*mark = na->mark;

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

static int addr_to_nodeid(struct sockaddr_storage *addr, int *nodeid,
			  unsigned int *mark)
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
				*mark = na->mark;
				rv = 0;
				goto unlock;
			}
		}
	}
unlock:
	spin_unlock(&dlm_node_addrs_spin);
	return rv;
}

/* caller need to held dlm_node_addrs_spin lock */
static bool dlm_lowcomms_na_has_addr(const struct dlm_node_addr *na,
				     const struct sockaddr_storage *addr)
{
	int i;

	for (i = 0; i < na->addr_count; i++) {
		if (addr_compare(na->addr[i], addr))
			return true;
	}

	return false;
}

int dlm_lowcomms_addr(int nodeid, struct sockaddr_storage *addr, int len)
{
	struct sockaddr_storage *new_addr;
	struct dlm_node_addr *new_node, *na;
	bool ret;

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
		new_node->mark = dlm_config.ci_mark;
		list_add(&new_node->list, &dlm_node_addrs);
		spin_unlock(&dlm_node_addrs_spin);
		return 0;
	}

	ret = dlm_lowcomms_na_has_addr(na, addr);
	if (ret) {
		spin_unlock(&dlm_node_addrs_spin);
		kfree(new_addr);
		kfree(new_node);
		return -EEXIST;
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

static void lowcomms_listen_data_ready(struct sock *sk)
{
	if (!dlm_allow_conn)
		return;

	queue_work(recv_workqueue, &listen_con.rwork);
}

static void lowcomms_write_space(struct sock *sk)
{
	struct connection *con;

	read_lock_bh(&sk->sk_callback_lock);
	con = sock2con(sk);
	if (!con)
		goto out;

	if (!test_and_set_bit(CF_CONNECTED, &con->flags)) {
		log_print("successful connected to node %d", con->nodeid);
		queue_work(send_workqueue, &con->swork);
		goto out;
	}

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
	int idx;

	if (nodeid == dlm_our_nodeid())
		return 0;

	idx = srcu_read_lock(&connections_srcu);
	con = nodeid2con(nodeid, GFP_NOFS);
	if (!con) {
		srcu_read_unlock(&connections_srcu, idx);
		return -ENOMEM;
	}

	lowcomms_connect_sock(con);
	srcu_read_unlock(&connections_srcu, idx);

	return 0;
}

int dlm_lowcomms_nodes_set_mark(int nodeid, unsigned int mark)
{
	struct dlm_node_addr *na;

	spin_lock(&dlm_node_addrs_spin);
	na = find_node_addr(nodeid);
	if (!na) {
		spin_unlock(&dlm_node_addrs_spin);
		return -ENOENT;
	}

	na->mark = mark;
	spin_unlock(&dlm_node_addrs_spin);

	return 0;
}

static void lowcomms_error_report(struct sock *sk)
{
	struct connection *con;
	struct sockaddr_storage saddr;
	void (*orig_report)(struct sock *) = NULL;

	read_lock_bh(&sk->sk_callback_lock);
	con = sock2con(sk);
	if (con == NULL)
		goto out;

	orig_report = listen_sock.sk_error_report;
	if (kernel_getpeername(sk->sk_socket, (struct sockaddr *)&saddr) < 0) {
		printk_ratelimited(KERN_ERR "dlm: node %d: socket error "
				   "sending to node %d, port %d, "
				   "sk_err=%d/%d\n", dlm_our_nodeid(),
				   con->nodeid, dlm_config.ci_tcp_port,
				   sk->sk_err, sk->sk_err_soft);
	} else if (saddr.ss_family == AF_INET) {
		struct sockaddr_in *sin4 = (struct sockaddr_in *)&saddr;

		printk_ratelimited(KERN_ERR "dlm: node %d: socket error "
				   "sending to node %d at %pI4, port %d, "
				   "sk_err=%d/%d\n", dlm_our_nodeid(),
				   con->nodeid, &sin4->sin_addr.s_addr,
				   dlm_config.ci_tcp_port, sk->sk_err,
				   sk->sk_err_soft);
	} else {
		struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)&saddr;

		printk_ratelimited(KERN_ERR "dlm: node %d: socket error "
				   "sending to node %d at %u.%u.%u.%u, "
				   "port %d, sk_err=%d/%d\n", dlm_our_nodeid(),
				   con->nodeid, sin6->sin6_addr.s6_addr32[0],
				   sin6->sin6_addr.s6_addr32[1],
				   sin6->sin6_addr.s6_addr32[2],
				   sin6->sin6_addr.s6_addr32[3],
				   dlm_config.ci_tcp_port, sk->sk_err,
				   sk->sk_err_soft);
	}

	/* below sendcon only handling */
	if (test_bit(CF_IS_OTHERCON, &con->flags))
		con = con->sendcon;

	switch (sk->sk_err) {
	case ECONNREFUSED:
		set_bit(CF_DELAY_CONNECT, &con->flags);
		break;
	default:
		break;
	}

	if (!test_and_set_bit(CF_RECONNECT, &con->flags))
		queue_work(send_workqueue, &con->swork);

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

static void add_listen_sock(struct socket *sock, struct listen_connection *con)
{
	struct sock *sk = sock->sk;

	write_lock_bh(&sk->sk_callback_lock);
	save_listen_callbacks(sock);
	con->sock = sock;

	sk->sk_user_data = con;
	sk->sk_allocation = GFP_NOFS;
	/* Install a data_ready callback */
	sk->sk_data_ready = lowcomms_listen_data_ready;
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

static void dlm_page_release(struct kref *kref)
{
	struct writequeue_entry *e = container_of(kref, struct writequeue_entry,
						  ref);

	__free_page(e->page);
	kfree(e);
}

static void dlm_msg_release(struct kref *kref)
{
	struct dlm_msg *msg = container_of(kref, struct dlm_msg, ref);

	kref_put(&msg->entry->ref, dlm_page_release);
	kfree(msg);
}

static void free_entry(struct writequeue_entry *e)
{
	struct dlm_msg *msg, *tmp;

	list_for_each_entry_safe(msg, tmp, &e->msgs, list) {
		if (msg->orig_msg) {
			msg->orig_msg->retransmit = false;
			kref_put(&msg->orig_msg->ref, dlm_msg_release);
		}

		list_del(&msg->list);
		kref_put(&msg->ref, dlm_msg_release);
	}

	list_del(&e->list);
	atomic_dec(&e->con->writequeue_cnt);
	kref_put(&e->ref, dlm_page_release);
}

static void dlm_close_sock(struct socket **sock)
{
	if (*sock) {
		restore_callbacks(*sock);
		sock_release(*sock);
		*sock = NULL;
	}
}

/* Close a remote connection and tidy up */
static void close_connection(struct connection *con, bool and_other,
			     bool tx, bool rx)
{
	bool closing = test_and_set_bit(CF_CLOSING, &con->flags);
	struct writequeue_entry *e;

	if (tx && !closing && cancel_work_sync(&con->swork)) {
		log_print("canceled swork for node %d", con->nodeid);
		clear_bit(CF_WRITE_PENDING, &con->flags);
	}
	if (rx && !closing && cancel_work_sync(&con->rwork)) {
		log_print("canceled rwork for node %d", con->nodeid);
		clear_bit(CF_READ_PENDING, &con->flags);
	}

	mutex_lock(&con->sock_mutex);
	dlm_close_sock(&con->sock);

	if (con->othercon && and_other) {
		/* Will only re-enter once. */
		close_connection(con->othercon, false, tx, rx);
	}

	/* if we send a writequeue entry only a half way, we drop the
	 * whole entry because reconnection and that we not start of the
	 * middle of a msg which will confuse the other end.
	 *
	 * we can always drop messages because retransmits, but what we
	 * cannot allow is to transmit half messages which may be processed
	 * at the other side.
	 *
	 * our policy is to start on a clean state when disconnects, we don't
	 * know what's send/received on transport layer in this case.
	 */
	spin_lock(&con->writequeue_lock);
	if (!list_empty(&con->writequeue)) {
		e = list_first_entry(&con->writequeue, struct writequeue_entry,
				     list);
		if (e->dirty)
			free_entry(e);
	}
	spin_unlock(&con->writequeue_lock);

	con->rx_leftover = 0;
	con->retries = 0;
	clear_bit(CF_APP_LIMITED, &con->flags);
	clear_bit(CF_CONNECTED, &con->flags);
	clear_bit(CF_DELAY_CONNECT, &con->flags);
	clear_bit(CF_RECONNECT, &con->flags);
	clear_bit(CF_EOF, &con->flags);
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
	struct msghdr msg;
	struct kvec iov;
	int ret, buflen;

	mutex_lock(&con->sock_mutex);

	if (con->sock == NULL) {
		ret = -EAGAIN;
		goto out_close;
	}

	/* realloc if we get new buffer size to read out */
	buflen = dlm_config.ci_buffer_size;
	if (con->rx_buflen != buflen && con->rx_leftover <= buflen) {
		ret = con_realloc_receive_buf(con, buflen);
		if (ret < 0)
			goto out_resched;
	}

	for (;;) {
		/* calculate new buffer parameter regarding last receive and
		 * possible leftover bytes
		 */
		iov.iov_base = con->rx_buf + con->rx_leftover;
		iov.iov_len = con->rx_buflen - con->rx_leftover;

		memset(&msg, 0, sizeof(msg));
		msg.msg_flags = MSG_DONTWAIT | MSG_NOSIGNAL;
		ret = kernel_recvmsg(con->sock, &msg, &iov, 1, iov.iov_len,
				     msg.msg_flags);
		trace_dlm_recv(con->nodeid, ret);
		if (ret == -EAGAIN)
			break;
		else if (ret <= 0)
			goto out_close;

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
		}
	}

	dlm_midcomms_receive_done(con->nodeid);
	mutex_unlock(&con->sock_mutex);
	return 0;

out_resched:
	if (!test_and_set_bit(CF_READ_PENDING, &con->flags))
		queue_work(recv_workqueue, &con->rwork);
	mutex_unlock(&con->sock_mutex);
	return -EAGAIN;

out_close:
	if (ret == 0) {
		log_print("connection %p got EOF from %d",
			  con, con->nodeid);

		if (dlm_proto_ops->eof_condition &&
		    dlm_proto_ops->eof_condition(con)) {
			set_bit(CF_EOF, &con->flags);
			mutex_unlock(&con->sock_mutex);
		} else {
			mutex_unlock(&con->sock_mutex);
			close_connection(con, false, true, false);

			/* handling for tcp shutdown */
			clear_bit(CF_SHUTDOWN, &con->flags);
			wake_up(&con->shutdown_wait);
		}

		/* signal to breaking receive worker */
		ret = -1;
	} else {
		mutex_unlock(&con->sock_mutex);
	}
	return ret;
}

/* Listening socket is busy, accept a connection */
static int accept_from_sock(struct listen_connection *con)
{
	int result;
	struct sockaddr_storage peeraddr;
	struct socket *newsock;
	int len, idx;
	int nodeid;
	struct connection *newcon;
	struct connection *addcon;
	unsigned int mark;

	if (!con->sock)
		return -ENOTCONN;

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
	if (addr_to_nodeid(&peeraddr, &nodeid, &mark)) {
		unsigned char *b=(unsigned char *)&peeraddr;
		log_print("connect from non cluster node");
		print_hex_dump_bytes("ss: ", DUMP_PREFIX_NONE, 
				     b, sizeof(struct sockaddr_storage));
		sock_release(newsock);
		return -1;
	}

	log_print("got connection from %d", nodeid);

	/*  Check to see if we already have a connection to this node. This
	 *  could happen if the two nodes initiate a connection at roughly
	 *  the same time and the connections cross on the wire.
	 *  In this case we store the incoming one in "othercon"
	 */
	idx = srcu_read_lock(&connections_srcu);
	newcon = nodeid2con(nodeid, GFP_NOFS);
	if (!newcon) {
		srcu_read_unlock(&connections_srcu, idx);
		result = -ENOMEM;
		goto accept_err;
	}

	sock_set_mark(newsock->sk, mark);

	mutex_lock(&newcon->sock_mutex);
	if (newcon->sock) {
		struct connection *othercon = newcon->othercon;

		if (!othercon) {
			othercon = kzalloc(sizeof(*othercon), GFP_NOFS);
			if (!othercon) {
				log_print("failed to allocate incoming socket");
				mutex_unlock(&newcon->sock_mutex);
				srcu_read_unlock(&connections_srcu, idx);
				result = -ENOMEM;
				goto accept_err;
			}

			result = dlm_con_init(othercon, nodeid);
			if (result < 0) {
				kfree(othercon);
				mutex_unlock(&newcon->sock_mutex);
				srcu_read_unlock(&connections_srcu, idx);
				goto accept_err;
			}

			lockdep_set_subclass(&othercon->sock_mutex, 1);
			set_bit(CF_IS_OTHERCON, &othercon->flags);
			newcon->othercon = othercon;
			othercon->sendcon = newcon;
		} else {
			/* close other sock con if we have something new */
			close_connection(othercon, false, true, false);
		}

		mutex_lock(&othercon->sock_mutex);
		add_sock(newsock, othercon);
		addcon = othercon;
		mutex_unlock(&othercon->sock_mutex);
	}
	else {
		/* accept copies the sk after we've saved the callbacks, so we
		   don't want to save them a second time or comm errors will
		   result in calling sk_error_report recursively. */
		add_sock(newsock, newcon);
		addcon = newcon;
	}

	set_bit(CF_CONNECTED, &addcon->flags);
	mutex_unlock(&newcon->sock_mutex);

	/*
	 * Add it to the active queue in case we got data
	 * between processing the accept adding the socket
	 * to the read_sockets list
	 */
	if (!test_and_set_bit(CF_READ_PENDING, &addcon->flags))
		queue_work(recv_workqueue, &addcon->rwork);

	srcu_read_unlock(&connections_srcu, idx);

	return 0;

accept_err:
	if (newsock)
		sock_release(newsock);

	if (result != -EAGAIN)
		log_print("error accepting connection from node: %d", result);
	return result;
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
	/* signal that page was half way transmitted */
	e->dirty = true;

	if (e->len == 0 && e->users == 0)
		free_entry(e);
}

/*
 * sctp_bind_addrs - bind a SCTP socket to all our addresses
 */
static int sctp_bind_addrs(struct socket *sock, uint16_t port)
{
	struct sockaddr_storage localaddr;
	struct sockaddr *addr = (struct sockaddr *)&localaddr;
	int i, addr_len, result = 0;

	for (i = 0; i < dlm_local_count; i++) {
		memcpy(&localaddr, dlm_local_addr[i], sizeof(localaddr));
		make_sockaddr(&localaddr, port, &addr_len);

		if (!i)
			result = kernel_bind(sock, addr, addr_len);
		else
			result = sock_bind_add(sock->sk, addr, addr_len);

		if (result < 0) {
			log_print("Can't bind to %d addr number %d, %d.\n",
				  port, i + 1, result);
			break;
		}
	}
	return result;
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

static struct writequeue_entry *new_writequeue_entry(struct connection *con,
						     gfp_t allocation)
{
	struct writequeue_entry *entry;

	entry = kzalloc(sizeof(*entry), allocation);
	if (!entry)
		return NULL;

	entry->page = alloc_page(allocation | __GFP_ZERO);
	if (!entry->page) {
		kfree(entry);
		return NULL;
	}

	entry->con = con;
	entry->users = 1;
	kref_init(&entry->ref);
	INIT_LIST_HEAD(&entry->msgs);

	return entry;
}

static struct writequeue_entry *new_wq_entry(struct connection *con, int len,
					     gfp_t allocation, char **ppc,
					     void (*cb)(void *data), void *data)
{
	struct writequeue_entry *e;

	spin_lock(&con->writequeue_lock);
	if (!list_empty(&con->writequeue)) {
		e = list_last_entry(&con->writequeue, struct writequeue_entry, list);
		if (DLM_WQ_REMAIN_BYTES(e) >= len) {
			kref_get(&e->ref);

			*ppc = page_address(e->page) + e->end;
			if (cb)
				cb(data);

			e->end += len;
			e->users++;
			spin_unlock(&con->writequeue_lock);

			return e;
		}
	}
	spin_unlock(&con->writequeue_lock);

	e = new_writequeue_entry(con, allocation);
	if (!e)
		return NULL;

	kref_get(&e->ref);
	*ppc = page_address(e->page);
	e->end += len;
	atomic_inc(&con->writequeue_cnt);

	spin_lock(&con->writequeue_lock);
	if (cb)
		cb(data);

	list_add_tail(&e->list, &con->writequeue);
	spin_unlock(&con->writequeue_lock);

	return e;
};

static struct dlm_msg *dlm_lowcomms_new_msg_con(struct connection *con, int len,
						gfp_t allocation, char **ppc,
						void (*cb)(void *data),
						void *data)
{
	struct writequeue_entry *e;
	struct dlm_msg *msg;
	bool sleepable;

	msg = kzalloc(sizeof(*msg), allocation);
	if (!msg)
		return NULL;

	/* this mutex is being used as a wait to avoid multiple "fast"
	 * new writequeue page list entry allocs in new_wq_entry in
	 * normal operation which is sleepable context. Without it
	 * we could end in multiple writequeue entries with one
	 * dlm message because multiple callers were waiting at
	 * the writequeue_lock in new_wq_entry().
	 */
	sleepable = gfpflags_normal_context(allocation);
	if (sleepable)
		mutex_lock(&con->wq_alloc);

	kref_init(&msg->ref);

	e = new_wq_entry(con, len, allocation, ppc, cb, data);
	if (!e) {
		if (sleepable)
			mutex_unlock(&con->wq_alloc);

		kfree(msg);
		return NULL;
	}

	if (sleepable)
		mutex_unlock(&con->wq_alloc);

	msg->ppc = *ppc;
	msg->len = len;
	msg->entry = e;

	return msg;
}

struct dlm_msg *dlm_lowcomms_new_msg(int nodeid, int len, gfp_t allocation,
				     char **ppc, void (*cb)(void *data),
				     void *data)
{
	struct connection *con;
	struct dlm_msg *msg;
	int idx;

	if (len > DLM_MAX_SOCKET_BUFSIZE ||
	    len < sizeof(struct dlm_header)) {
		BUILD_BUG_ON(PAGE_SIZE < DLM_MAX_SOCKET_BUFSIZE);
		log_print("failed to allocate a buffer of size %d", len);
		WARN_ON(1);
		return NULL;
	}

	idx = srcu_read_lock(&connections_srcu);
	con = nodeid2con(nodeid, allocation);
	if (!con) {
		srcu_read_unlock(&connections_srcu, idx);
		return NULL;
	}

	msg = dlm_lowcomms_new_msg_con(con, len, allocation, ppc, cb, data);
	if (!msg) {
		srcu_read_unlock(&connections_srcu, idx);
		return NULL;
	}

	/* we assume if successful commit must called */
	msg->idx = idx;
	return msg;
}

static void _dlm_lowcomms_commit_msg(struct dlm_msg *msg)
{
	struct writequeue_entry *e = msg->entry;
	struct connection *con = e->con;
	int users;

	spin_lock(&con->writequeue_lock);
	kref_get(&msg->ref);
	list_add(&msg->list, &e->msgs);

	users = --e->users;
	if (users)
		goto out;

	e->len = DLM_WQ_LENGTH_BYTES(e);
	spin_unlock(&con->writequeue_lock);

	queue_work(send_workqueue, &con->swork);
	return;

out:
	spin_unlock(&con->writequeue_lock);
	return;
}

void dlm_lowcomms_commit_msg(struct dlm_msg *msg)
{
	_dlm_lowcomms_commit_msg(msg);
	srcu_read_unlock(&connections_srcu, msg->idx);
}

void dlm_lowcomms_put_msg(struct dlm_msg *msg)
{
	kref_put(&msg->ref, dlm_msg_release);
}

/* does not held connections_srcu, usage workqueue only */
int dlm_lowcomms_resend_msg(struct dlm_msg *msg)
{
	struct dlm_msg *msg_resend;
	char *ppc;

	if (msg->retransmit)
		return 1;

	msg_resend = dlm_lowcomms_new_msg_con(msg->entry->con, msg->len,
					      GFP_ATOMIC, &ppc, NULL, NULL);
	if (!msg_resend)
		return -ENOMEM;

	msg->retransmit = true;
	kref_get(&msg->ref);
	msg_resend->orig_msg = msg;

	memcpy(ppc, msg->ppc, msg->len);
	_dlm_lowcomms_commit_msg(msg_resend);
	dlm_lowcomms_put_msg(msg_resend);

	return 0;
}

/* Send a message */
static void send_to_sock(struct connection *con)
{
	const int msg_flags = MSG_DONTWAIT | MSG_NOSIGNAL;
	struct writequeue_entry *e;
	int len, offset, ret;
	int count = 0;

	mutex_lock(&con->sock_mutex);
	if (con->sock == NULL)
		goto out_connect;

	spin_lock(&con->writequeue_lock);
	for (;;) {
		e = con_next_wq(con);
		if (!e)
			break;

		len = e->len;
		offset = e->offset;
		BUG_ON(len == 0 && e->users == 0);
		spin_unlock(&con->writequeue_lock);

		ret = kernel_sendpage(con->sock, e->page, offset, len,
				      msg_flags);
		trace_dlm_send(con->nodeid, ret);
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
			goto out;

		/* Don't starve people filling buffers */
		if (++count >= MAX_SEND_MSG_COUNT) {
			cond_resched();
			count = 0;
		}

		spin_lock(&con->writequeue_lock);
		writequeue_entry_complete(e, ret);
	}
	spin_unlock(&con->writequeue_lock);

	/* close if we got EOF */
	if (test_and_clear_bit(CF_EOF, &con->flags)) {
		mutex_unlock(&con->sock_mutex);
		close_connection(con, false, false, true);

		/* handling for tcp shutdown */
		clear_bit(CF_SHUTDOWN, &con->flags);
		wake_up(&con->shutdown_wait);
	} else {
		mutex_unlock(&con->sock_mutex);
	}

	return;

out:
	mutex_unlock(&con->sock_mutex);
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
	int idx;

	log_print("closing connection to node %d", nodeid);
	idx = srcu_read_lock(&connections_srcu);
	con = nodeid2con(nodeid, 0);
	if (con) {
		set_bit(CF_CLOSE, &con->flags);
		close_connection(con, true, true, true);
		clean_one_writequeue(con);
		if (con->othercon)
			clean_one_writequeue(con->othercon);
	}
	srcu_read_unlock(&connections_srcu, idx);

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

	clear_bit(CF_READ_PENDING, &con->flags);
	receive_from_sock(con);
}

static void process_listen_recv_socket(struct work_struct *work)
{
	accept_from_sock(&listen_con);
}

static void dlm_connect(struct connection *con)
{
	struct sockaddr_storage addr;
	int result, addr_len;
	struct socket *sock;
	unsigned int mark;

	/* Some odd races can cause double-connects, ignore them */
	if (con->retries++ > MAX_CONNECT_RETRIES)
		return;

	if (con->sock) {
		log_print("node %d already connected.", con->nodeid);
		return;
	}

	memset(&addr, 0, sizeof(addr));
	result = nodeid_to_addr(con->nodeid, &addr, NULL,
				dlm_proto_ops->try_new_addr, &mark);
	if (result < 0) {
		log_print("no address for nodeid %d", con->nodeid);
		return;
	}

	/* Create a socket to communicate with */
	result = sock_create_kern(&init_net, dlm_local_addr[0]->ss_family,
				  SOCK_STREAM, dlm_proto_ops->proto, &sock);
	if (result < 0)
		goto socket_err;

	sock_set_mark(sock->sk, mark);
	dlm_proto_ops->sockopts(sock);

	add_sock(sock, con);

	result = dlm_proto_ops->bind(sock);
	if (result < 0)
		goto add_sock_err;

	log_print_ratelimited("connecting to %d", con->nodeid);
	make_sockaddr(&addr, dlm_config.ci_tcp_port, &addr_len);
	result = dlm_proto_ops->connect(con, sock, (struct sockaddr *)&addr,
					addr_len);
	if (result < 0)
		goto add_sock_err;

	return;

add_sock_err:
	dlm_close_sock(&con->sock);

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
		msleep(1000);
		lowcomms_connect_sock(con);
	}
}

/* Send workqueue function */
static void process_send_sockets(struct work_struct *work)
{
	struct connection *con = container_of(work, struct connection, swork);

	WARN_ON(test_bit(CF_IS_OTHERCON, &con->flags));

	clear_bit(CF_WRITE_PENDING, &con->flags);

	if (test_and_clear_bit(CF_RECONNECT, &con->flags)) {
		close_connection(con, false, false, true);
		dlm_midcomms_unack_msg_resend(con->nodeid);
	}

	if (con->sock == NULL) {
		if (test_and_clear_bit(CF_DELAY_CONNECT, &con->flags))
			msleep(1000);

		mutex_lock(&con->sock_mutex);
		dlm_connect(con);
		mutex_unlock(&con->sock_mutex);
	}

	if (!list_empty(&con->writequeue))
		send_to_sock(con);
}

static void work_stop(void)
{
	if (recv_workqueue) {
		destroy_workqueue(recv_workqueue);
		recv_workqueue = NULL;
	}

	if (send_workqueue) {
		destroy_workqueue(send_workqueue);
		send_workqueue = NULL;
	}
}

static int work_start(void)
{
	recv_workqueue = alloc_ordered_workqueue("dlm_recv", WQ_MEM_RECLAIM);
	if (!recv_workqueue) {
		log_print("can't start dlm_recv");
		return -ENOMEM;
	}

	send_workqueue = alloc_ordered_workqueue("dlm_send", WQ_MEM_RECLAIM);
	if (!send_workqueue) {
		log_print("can't start dlm_send");
		destroy_workqueue(recv_workqueue);
		recv_workqueue = NULL;
		return -ENOMEM;
	}

	return 0;
}

static void shutdown_conn(struct connection *con)
{
	if (dlm_proto_ops->shutdown_action)
		dlm_proto_ops->shutdown_action(con);
}

void dlm_lowcomms_shutdown(void)
{
	int idx;

	/* Set all the flags to prevent any
	 * socket activity.
	 */
	dlm_allow_conn = 0;

	if (recv_workqueue)
		flush_workqueue(recv_workqueue);
	if (send_workqueue)
		flush_workqueue(send_workqueue);

	dlm_close_sock(&listen_con.sock);

	idx = srcu_read_lock(&connections_srcu);
	foreach_conn(shutdown_conn);
	srcu_read_unlock(&connections_srcu, idx);
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
		call_srcu(&connections_srcu, &con->othercon->rcu,
			  connection_release);
	}
	clean_one_writequeue(con);
	call_srcu(&connections_srcu, &con->rcu, connection_release);
}

static void work_flush(void)
{
	int ok;
	int i;
	struct connection *con;

	do {
		ok = 1;
		foreach_conn(stop_conn);
		if (recv_workqueue)
			flush_workqueue(recv_workqueue);
		if (send_workqueue)
			flush_workqueue(send_workqueue);
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
	} while (!ok);
}

void dlm_lowcomms_stop(void)
{
	int idx;

	idx = srcu_read_lock(&connections_srcu);
	work_flush();
	foreach_conn(free_conn);
	srcu_read_unlock(&connections_srcu, idx);
	work_stop();
	deinit_local();

	dlm_proto_ops = NULL;
}

static int dlm_listen_for_all(void)
{
	struct socket *sock;
	int result;

	log_print("Using %s for communications",
		  dlm_proto_ops->name);

	result = dlm_proto_ops->listen_validate();
	if (result < 0)
		return result;

	result = sock_create_kern(&init_net, dlm_local_addr[0]->ss_family,
				  SOCK_STREAM, dlm_proto_ops->proto, &sock);
	if (result < 0) {
		log_print("Can't create comms socket: %d", result);
		goto out;
	}

	sock_set_mark(sock->sk, dlm_config.ci_mark);
	dlm_proto_ops->listen_sockopts(sock);

	result = dlm_proto_ops->listen_bind(sock);
	if (result < 0)
		goto out;

	save_listen_callbacks(sock);
	add_listen_sock(sock, &listen_con);

	INIT_WORK(&listen_con.rwork, process_listen_recv_socket);
	result = sock->ops->listen(sock, 5);
	if (result < 0) {
		dlm_close_sock(&listen_con.sock);
		goto out;
	}

	return 0;

out:
	sock_release(sock);
	return result;
}

static int dlm_tcp_bind(struct socket *sock)
{
	struct sockaddr_storage src_addr;
	int result, addr_len;

	/* Bind to our cluster-known address connecting to avoid
	 * routing problems.
	 */
	memcpy(&src_addr, dlm_local_addr[0], sizeof(src_addr));
	make_sockaddr(&src_addr, 0, &addr_len);

	result = sock->ops->bind(sock, (struct sockaddr *)&src_addr,
				 addr_len);
	if (result < 0) {
		/* This *may* not indicate a critical error */
		log_print("could not bind for connect: %d", result);
	}

	return 0;
}

static int dlm_tcp_connect(struct connection *con, struct socket *sock,
			   struct sockaddr *addr, int addr_len)
{
	int ret;

	ret = sock->ops->connect(sock, addr, addr_len, O_NONBLOCK);
	switch (ret) {
	case -EINPROGRESS:
		fallthrough;
	case 0:
		return 0;
	}

	return ret;
}

static int dlm_tcp_listen_validate(void)
{
	/* We don't support multi-homed hosts */
	if (dlm_local_count > 1) {
		log_print("TCP protocol can't handle multi-homed hosts, try SCTP");
		return -EINVAL;
	}

	return 0;
}

static void dlm_tcp_sockopts(struct socket *sock)
{
	/* Turn off Nagle's algorithm */
	tcp_sock_set_nodelay(sock->sk);
}

static void dlm_tcp_listen_sockopts(struct socket *sock)
{
	dlm_tcp_sockopts(sock);
	sock_set_reuseaddr(sock->sk);
}

static int dlm_tcp_listen_bind(struct socket *sock)
{
	int addr_len;

	/* Bind to our port */
	make_sockaddr(dlm_local_addr[0], dlm_config.ci_tcp_port, &addr_len);
	return sock->ops->bind(sock, (struct sockaddr *)dlm_local_addr[0],
			       addr_len);
}

static const struct dlm_proto_ops dlm_tcp_ops = {
	.name = "TCP",
	.proto = IPPROTO_TCP,
	.connect = dlm_tcp_connect,
	.sockopts = dlm_tcp_sockopts,
	.bind = dlm_tcp_bind,
	.listen_validate = dlm_tcp_listen_validate,
	.listen_sockopts = dlm_tcp_listen_sockopts,
	.listen_bind = dlm_tcp_listen_bind,
	.shutdown_action = dlm_tcp_shutdown,
	.eof_condition = tcp_eof_condition,
};

static int dlm_sctp_bind(struct socket *sock)
{
	return sctp_bind_addrs(sock, 0);
}

static int dlm_sctp_connect(struct connection *con, struct socket *sock,
			    struct sockaddr *addr, int addr_len)
{
	int ret;

	/*
	 * Make sock->ops->connect() function return in specified time,
	 * since O_NONBLOCK argument in connect() function does not work here,
	 * then, we should restore the default value of this attribute.
	 */
	sock_set_sndtimeo(sock->sk, 5);
	ret = sock->ops->connect(sock, addr, addr_len, 0);
	sock_set_sndtimeo(sock->sk, 0);
	if (ret < 0)
		return ret;

	if (!test_and_set_bit(CF_CONNECTED, &con->flags))
		log_print("successful connected to node %d", con->nodeid);

	return 0;
}

static int dlm_sctp_listen_validate(void)
{
	if (!IS_ENABLED(CONFIG_IP_SCTP)) {
		log_print("SCTP is not enabled by this kernel");
		return -EOPNOTSUPP;
	}

	request_module("sctp");
	return 0;
}

static int dlm_sctp_bind_listen(struct socket *sock)
{
	return sctp_bind_addrs(sock, dlm_config.ci_tcp_port);
}

static void dlm_sctp_sockopts(struct socket *sock)
{
	/* Turn off Nagle's algorithm */
	sctp_sock_set_nodelay(sock->sk);
	sock_set_rcvbuf(sock->sk, NEEDED_RMEM);
}

static const struct dlm_proto_ops dlm_sctp_ops = {
	.name = "SCTP",
	.proto = IPPROTO_SCTP,
	.try_new_addr = true,
	.connect = dlm_sctp_connect,
	.sockopts = dlm_sctp_sockopts,
	.bind = dlm_sctp_bind,
	.listen_validate = dlm_sctp_listen_validate,
	.listen_sockopts = dlm_sctp_sockopts,
	.listen_bind = dlm_sctp_bind_listen,
};

int dlm_lowcomms_start(void)
{
	int error = -EINVAL;
	int i;

	for (i = 0; i < CONN_HASH_SIZE; i++)
		INIT_HLIST_HEAD(&connection_hash[i]);

	init_local();
	if (!dlm_local_count) {
		error = -ENOTCONN;
		log_print("no local IP address has been set");
		goto fail;
	}

	INIT_WORK(&listen_con.rwork, process_listen_recv_socket);

	error = work_start();
	if (error)
		goto fail_local;

	dlm_allow_conn = 1;

	/* Start listening */
	switch (dlm_config.ci_protocol) {
	case DLM_PROTO_TCP:
		dlm_proto_ops = &dlm_tcp_ops;
		break;
	case DLM_PROTO_SCTP:
		dlm_proto_ops = &dlm_sctp_ops;
		break;
	default:
		log_print("Invalid protocol identifier %d set",
			  dlm_config.ci_protocol);
		error = -EINVAL;
		goto fail_proto_ops;
	}

	error = dlm_listen_for_all();
	if (error)
		goto fail_listen;

	return 0;

fail_listen:
	dlm_proto_ops = NULL;
fail_proto_ops:
	dlm_allow_conn = 0;
	dlm_close_sock(&listen_con.sock);
	work_stop();
fail_local:
	deinit_local();
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
