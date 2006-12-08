/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
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
 * be expanded for the cluster infrastructure then that is it's
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
 * I don't see any problem with the recv thread executing the locking
 * code on behalf of remote processes as the locking code is
 * short, efficient and never waits.
 *
 */


#include <asm/ioctls.h>
#include <net/sock.h>
#include <net/tcp.h>
#include <linux/pagemap.h>

#include "dlm_internal.h"
#include "lowcomms.h"
#include "midcomms.h"
#include "config.h"

struct cbuf {
	unsigned int base;
	unsigned int len;
	unsigned int mask;
};

#define NODE_INCREMENT 32
static void cbuf_add(struct cbuf *cb, int n)
{
	cb->len += n;
}

static int cbuf_data(struct cbuf *cb)
{
	return ((cb->base + cb->len) & cb->mask);
}

static void cbuf_init(struct cbuf *cb, int size)
{
	cb->base = cb->len = 0;
	cb->mask = size-1;
}

static void cbuf_eat(struct cbuf *cb, int n)
{
	cb->len  -= n;
	cb->base += n;
	cb->base &= cb->mask;
}

static bool cbuf_empty(struct cbuf *cb)
{
	return cb->len == 0;
}

/* Maximum number of incoming messages to process before
   doing a cond_resched()
*/
#define MAX_RX_MSG_COUNT 25

struct connection {
	struct socket *sock;	/* NULL if not connected */
	uint32_t nodeid;	/* So we know who we are in the list */
	struct rw_semaphore sock_sem; /* Stop connect races */
	struct list_head read_list;   /* On this list when ready for reading */
	struct list_head write_list;  /* On this list when ready for writing */
	struct list_head state_list;  /* On this list when ready to connect */
	unsigned long flags;	/* bit 1,2 = We are on the read/write lists */
#define CF_READ_PENDING 1
#define CF_WRITE_PENDING 2
#define CF_CONNECT_PENDING 3
#define CF_IS_OTHERCON 4
	struct list_head writequeue;  /* List of outgoing writequeue_entries */
	struct list_head listenlist;  /* List of allocated listening sockets */
	spinlock_t writequeue_lock;
	int (*rx_action) (struct connection *);	/* What to do when active */
	struct page *rx_page;
	struct cbuf cb;
	int retries;
	atomic_t waiting_requests;
#define MAX_CONNECT_RETRIES 3
	struct connection *othercon;
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

static struct sockaddr_storage dlm_local_addr;

/* Manage daemons */
static struct task_struct *recv_task;
static struct task_struct *send_task;

static wait_queue_t lowcomms_send_waitq_head;
static DECLARE_WAIT_QUEUE_HEAD(lowcomms_send_waitq);
static wait_queue_t lowcomms_recv_waitq_head;
static DECLARE_WAIT_QUEUE_HEAD(lowcomms_recv_waitq);

/* An array of pointers to connections, indexed by NODEID */
static struct connection **connections;
static DECLARE_MUTEX(connections_lock);
static struct kmem_cache *con_cache;
static int conn_array_size;

/* List of sockets that have reads pending */
static LIST_HEAD(read_sockets);
static DEFINE_SPINLOCK(read_sockets_lock);

/* List of sockets which have writes pending */
static LIST_HEAD(write_sockets);
static DEFINE_SPINLOCK(write_sockets_lock);

/* List of sockets which have connects pending */
static LIST_HEAD(state_sockets);
static DEFINE_SPINLOCK(state_sockets_lock);

static struct connection *nodeid2con(int nodeid, gfp_t allocation)
{
	struct connection *con = NULL;

	down(&connections_lock);
	if (nodeid >= conn_array_size) {
		int new_size = nodeid + NODE_INCREMENT;
		struct connection **new_conns;

		new_conns = kzalloc(sizeof(struct connection *) *
				    new_size, allocation);
		if (!new_conns)
			goto finish;

		memcpy(new_conns, connections,  sizeof(struct connection *) * conn_array_size);
		conn_array_size = new_size;
		kfree(connections);
		connections = new_conns;

	}

	con = connections[nodeid];
	if (con == NULL && allocation) {
		con = kmem_cache_zalloc(con_cache, allocation);
		if (!con)
			goto finish;

		con->nodeid = nodeid;
		init_rwsem(&con->sock_sem);
		INIT_LIST_HEAD(&con->writequeue);
		spin_lock_init(&con->writequeue_lock);

		connections[nodeid] = con;
	}

finish:
	up(&connections_lock);
	return con;
}

/* Data available on socket or listen socket received a connect */
static void lowcomms_data_ready(struct sock *sk, int count_unused)
{
	struct connection *con = sock2con(sk);

	atomic_inc(&con->waiting_requests);
	if (test_and_set_bit(CF_READ_PENDING, &con->flags))
		return;

	spin_lock_bh(&read_sockets_lock);
	list_add_tail(&con->read_list, &read_sockets);
	spin_unlock_bh(&read_sockets_lock);

	wake_up_interruptible(&lowcomms_recv_waitq);
}

static void lowcomms_write_space(struct sock *sk)
{
	struct connection *con = sock2con(sk);

	if (test_and_set_bit(CF_WRITE_PENDING, &con->flags))
		return;

	spin_lock_bh(&write_sockets_lock);
	list_add_tail(&con->write_list, &write_sockets);
	spin_unlock_bh(&write_sockets_lock);

	wake_up_interruptible(&lowcomms_send_waitq);
}

static inline void lowcomms_connect_sock(struct connection *con)
{
	if (test_and_set_bit(CF_CONNECT_PENDING, &con->flags))
		return;

	spin_lock_bh(&state_sockets_lock);
	list_add_tail(&con->state_list, &state_sockets);
	spin_unlock_bh(&state_sockets_lock);

	wake_up_interruptible(&lowcomms_send_waitq);
}

static void lowcomms_state_change(struct sock *sk)
{
	if (sk->sk_state == TCP_ESTABLISHED)
		lowcomms_write_space(sk);
}

/* Make a socket active */
static int add_sock(struct socket *sock, struct connection *con)
{
	con->sock = sock;

	/* Install a data_ready callback */
	con->sock->sk->sk_data_ready = lowcomms_data_ready;
	con->sock->sk->sk_write_space = lowcomms_write_space;
	con->sock->sk->sk_state_change = lowcomms_state_change;

	return 0;
}

/* Add the port number to an IP6 or 4 sockaddr and return the address
   length */
static void make_sockaddr(struct sockaddr_storage *saddr, uint16_t port,
			  int *addr_len)
{
	saddr->ss_family =  dlm_local_addr.ss_family;
	if (saddr->ss_family == AF_INET) {
		struct sockaddr_in *in4_addr = (struct sockaddr_in *)saddr;
		in4_addr->sin_port = cpu_to_be16(port);
		*addr_len = sizeof(struct sockaddr_in);
	} else {
		struct sockaddr_in6 *in6_addr = (struct sockaddr_in6 *)saddr;
		in6_addr->sin6_port = cpu_to_be16(port);
		*addr_len = sizeof(struct sockaddr_in6);
	}
}

/* Close a remote connection and tidy up */
static void close_connection(struct connection *con, bool and_other)
{
	down_write(&con->sock_sem);

	if (con->sock) {
		sock_release(con->sock);
		con->sock = NULL;
	}
	if (con->othercon && and_other) {
		/* Will only re-enter once. */
		close_connection(con->othercon, false);
	}
	if (con->rx_page) {
		__free_page(con->rx_page);
		con->rx_page = NULL;
	}
	con->retries = 0;
	up_write(&con->sock_sem);
}

/* Data received from remote end */
static int receive_from_sock(struct connection *con)
{
	int ret = 0;
	struct msghdr msg;
	struct iovec iov[2];
	mm_segment_t fs;
	unsigned len;
	int r;
	int call_again_soon = 0;

	down_read(&con->sock_sem);

	if (con->sock == NULL)
		goto out;
	if (con->rx_page == NULL) {
		/*
		 * This doesn't need to be atomic, but I think it should
		 * improve performance if it is.
		 */
		con->rx_page = alloc_page(GFP_ATOMIC);
		if (con->rx_page == NULL)
			goto out_resched;
		cbuf_init(&con->cb, PAGE_CACHE_SIZE);
	}

	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_iovlen = 1;
	msg.msg_iov = iov;
	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_flags = 0;

	/*
	 * iov[0] is the bit of the circular buffer between the current end
	 * point (cb.base + cb.len) and the end of the buffer.
	 */
	iov[0].iov_len = con->cb.base - cbuf_data(&con->cb);
	iov[0].iov_base = page_address(con->rx_page) + cbuf_data(&con->cb);
	iov[1].iov_len = 0;

	/*
	 * iov[1] is the bit of the circular buffer between the start of the
	 * buffer and the start of the currently used section (cb.base)
	 */
	if (cbuf_data(&con->cb) >= con->cb.base) {
		iov[0].iov_len = PAGE_CACHE_SIZE - cbuf_data(&con->cb);
		iov[1].iov_len = con->cb.base;
		iov[1].iov_base = page_address(con->rx_page);
		msg.msg_iovlen = 2;
	}
	len = iov[0].iov_len + iov[1].iov_len;

	fs = get_fs();
	set_fs(get_ds());
	r = ret = sock_recvmsg(con->sock, &msg, len,
			       MSG_DONTWAIT | MSG_NOSIGNAL);
	set_fs(fs);

	if (ret <= 0)
		goto out_close;
	if (ret == len)
		call_again_soon = 1;
	cbuf_add(&con->cb, ret);
	ret = dlm_process_incoming_buffer(con->nodeid,
					  page_address(con->rx_page),
					  con->cb.base, con->cb.len,
					  PAGE_CACHE_SIZE);
	if (ret == -EBADMSG) {
		printk(KERN_INFO "dlm: lowcomms: addr=%p, base=%u, len=%u, "
		       "iov_len=%u, iov_base[0]=%p, read=%d\n",
		       page_address(con->rx_page), con->cb.base, con->cb.len,
		       len, iov[0].iov_base, r);
	}
	if (ret < 0)
		goto out_close;
	cbuf_eat(&con->cb, ret);

	if (cbuf_empty(&con->cb) && !call_again_soon) {
		__free_page(con->rx_page);
		con->rx_page = NULL;
	}

out:
	if (call_again_soon)
		goto out_resched;
	up_read(&con->sock_sem);
	return 0;

out_resched:
	lowcomms_data_ready(con->sock->sk, 0);
	up_read(&con->sock_sem);
	cond_resched();
	return 0;

out_close:
	up_read(&con->sock_sem);
	if (ret != -EAGAIN && !test_bit(CF_IS_OTHERCON, &con->flags)) {
		close_connection(con, false);
		/* Reconnect when there is something to send */
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

	memset(&peeraddr, 0, sizeof(peeraddr));
	result = sock_create_kern(dlm_local_addr.ss_family, SOCK_STREAM,
				  IPPROTO_TCP, &newsock);
	if (result < 0)
		return -ENOMEM;

	down_read(&con->sock_sem);

	result = -ENOTCONN;
	if (con->sock == NULL)
		goto accept_err;

	newsock->type = con->sock->type;
	newsock->ops = con->sock->ops;

	result = con->sock->ops->accept(con->sock, newsock, O_NONBLOCK);
	if (result < 0)
		goto accept_err;

	/* Get the connected socket's peer */
	memset(&peeraddr, 0, sizeof(peeraddr));
	if (newsock->ops->getname(newsock, (struct sockaddr *)&peeraddr,
				  &len, 2)) {
		result = -ECONNABORTED;
		goto accept_err;
	}

	/* Get the new node's NODEID */
	make_sockaddr(&peeraddr, 0, &len);
	if (dlm_addr_to_nodeid(&peeraddr, &nodeid)) {
		printk("dlm: connect from non cluster node\n");
		sock_release(newsock);
		up_read(&con->sock_sem);
		return -1;
	}

	log_print("got connection from %d", nodeid);

	/*  Check to see if we already have a connection to this node. This
	 *  could happen if the two nodes initiate a connection at roughly
	 *  the same time and the connections cross on the wire.
	 * TEMPORARY FIX:
	 *  In this case we store the incoming one in "othercon"
	 */
	newcon = nodeid2con(nodeid, GFP_KERNEL);
	if (!newcon) {
		result = -ENOMEM;
		goto accept_err;
	}
	down_write(&newcon->sock_sem);
	if (newcon->sock) {
		struct connection *othercon = newcon->othercon;

		if (!othercon) {
			othercon = kmem_cache_zalloc(con_cache, GFP_KERNEL);
			if (!othercon) {
				printk("dlm: failed to allocate incoming socket\n");
				up_write(&newcon->sock_sem);
				result = -ENOMEM;
				goto accept_err;
			}
			othercon->nodeid = nodeid;
			othercon->rx_action = receive_from_sock;
			init_rwsem(&othercon->sock_sem);
			set_bit(CF_IS_OTHERCON, &othercon->flags);
			newcon->othercon = othercon;
		}
		othercon->sock = newsock;
		newsock->sk->sk_user_data = othercon;
		add_sock(newsock, othercon);
	}
	else {
		newsock->sk->sk_user_data = newcon;
		newcon->rx_action = receive_from_sock;
		add_sock(newsock, newcon);

	}

	up_write(&newcon->sock_sem);

	/*
	 * Add it to the active queue in case we got data
	 * beween processing the accept adding the socket
	 * to the read_sockets list
	 */
	lowcomms_data_ready(newsock->sk, 0);
	up_read(&con->sock_sem);

	return 0;

accept_err:
	up_read(&con->sock_sem);
	sock_release(newsock);

	if (result != -EAGAIN)
		printk("dlm: error accepting connection from node: %d\n", result);
	return result;
}

/* Connect a new socket to its peer */
static void connect_to_sock(struct connection *con)
{
	int result = -EHOSTUNREACH;
	struct sockaddr_storage saddr;
	int addr_len;
	struct socket *sock;

	if (con->nodeid == 0) {
		log_print("attempt to connect sock 0 foiled");
		return;
	}

	down_write(&con->sock_sem);
	if (con->retries++ > MAX_CONNECT_RETRIES)
		goto out;

	/* Some odd races can cause double-connects, ignore them */
	if (con->sock) {
		result = 0;
		goto out;
	}

	/* Create a socket to communicate with */
	result = sock_create_kern(dlm_local_addr.ss_family, SOCK_STREAM,
				  IPPROTO_TCP, &sock);
	if (result < 0)
		goto out_err;

	memset(&saddr, 0, sizeof(saddr));
	if (dlm_nodeid_to_addr(con->nodeid, &saddr))
		goto out_err;

	sock->sk->sk_user_data = con;
	con->rx_action = receive_from_sock;

	make_sockaddr(&saddr, dlm_config.tcp_port, &addr_len);

	add_sock(sock, con);

	log_print("connecting to %d", con->nodeid);
	result =
		sock->ops->connect(sock, (struct sockaddr *)&saddr, addr_len,
				   O_NONBLOCK);
	if (result == -EINPROGRESS)
		result = 0;
	if (result == 0)
		goto out;

out_err:
	if (con->sock) {
		sock_release(con->sock);
		con->sock = NULL;
	}
	/*
	 * Some errors are fatal and this list might need adjusting. For other
	 * errors we try again until the max number of retries is reached.
	 */
	if (result != -EHOSTUNREACH && result != -ENETUNREACH &&
	    result != -ENETDOWN && result != EINVAL
	    && result != -EPROTONOSUPPORT) {
		lowcomms_connect_sock(con);
		result = 0;
	}
out:
	up_write(&con->sock_sem);
	return;
}

static struct socket *create_listen_sock(struct connection *con,
					 struct sockaddr_storage *saddr)
{
	struct socket *sock = NULL;
	mm_segment_t fs;
	int result = 0;
	int one = 1;
	int addr_len;

	if (dlm_local_addr.ss_family == AF_INET)
		addr_len = sizeof(struct sockaddr_in);
	else
		addr_len = sizeof(struct sockaddr_in6);

	/* Create a socket to communicate with */
	result = sock_create_kern(dlm_local_addr.ss_family, SOCK_STREAM, IPPROTO_TCP, &sock);
	if (result < 0) {
		printk("dlm: Can't create listening comms socket\n");
		goto create_out;
	}

	fs = get_fs();
	set_fs(get_ds());
	result = sock_setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
				 (char *)&one, sizeof(one));
	set_fs(fs);
	if (result < 0) {
		printk("dlm: Failed to set SO_REUSEADDR on socket: result=%d\n",
		       result);
	}
	sock->sk->sk_user_data = con;
	con->rx_action = accept_from_sock;
	con->sock = sock;

	/* Bind to our port */
	make_sockaddr(saddr, dlm_config.tcp_port, &addr_len);
	result = sock->ops->bind(sock, (struct sockaddr *) saddr, addr_len);
	if (result < 0) {
		printk("dlm: Can't bind to port %d\n", dlm_config.tcp_port);
		sock_release(sock);
		sock = NULL;
		con->sock = NULL;
		goto create_out;
	}

	fs = get_fs();
	set_fs(get_ds());

	result = sock_setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE,
				 (char *)&one, sizeof(one));
	set_fs(fs);
	if (result < 0) {
		printk("dlm: Set keepalive failed: %d\n", result);
	}

	result = sock->ops->listen(sock, 5);
	if (result < 0) {
		printk("dlm: Can't listen on port %d\n", dlm_config.tcp_port);
		sock_release(sock);
		sock = NULL;
		goto create_out;
	}

create_out:
	return sock;
}


/* Listen on all interfaces */
static int listen_for_all(void)
{
	struct socket *sock = NULL;
	struct connection *con = nodeid2con(0, GFP_KERNEL);
	int result = -EINVAL;

	/* We don't support multi-homed hosts */
	set_bit(CF_IS_OTHERCON, &con->flags);

	sock = create_listen_sock(con, &dlm_local_addr);
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

void *dlm_lowcomms_get_buffer(int nodeid, int len,
			      gfp_t allocation, char **ppc)
{
	struct connection *con;
	struct writequeue_entry *e;
	int offset = 0;
	int users = 0;

	con = nodeid2con(nodeid, allocation);
	if (!con)
		return NULL;

	e = list_entry(con->writequeue.prev, struct writequeue_entry, list);
	if ((&e->list == &con->writequeue) ||
	    (PAGE_CACHE_SIZE - e->end < len)) {
		e = NULL;
	} else {
		offset = e->end;
		e->end += len;
		users = e->users++;
	}
	spin_unlock(&con->writequeue_lock);

	if (e) {
	got_one:
		if (users == 0)
			kmap(e->page);
		*ppc = page_address(e->page) + offset;
		return e;
	}

	e = new_writequeue_entry(con, allocation);
	if (e) {
		spin_lock(&con->writequeue_lock);
		offset = e->end;
		e->end += len;
		users = e->users++;
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

	users = --e->users;
	if (users)
		goto out;
	e->len = e->end - e->offset;
	kunmap(e->page);
	spin_unlock(&con->writequeue_lock);

	if (test_and_set_bit(CF_WRITE_PENDING, &con->flags) == 0) {
		spin_lock_bh(&write_sockets_lock);
		list_add_tail(&con->write_list, &write_sockets);
		spin_unlock_bh(&write_sockets_lock);

		wake_up_interruptible(&lowcomms_send_waitq);
	}
	return;

out:
	spin_unlock(&con->writequeue_lock);
	return;
}

static void free_entry(struct writequeue_entry *e)
{
	__free_page(e->page);
	kfree(e);
}

/* Send a message */
static void send_to_sock(struct connection *con)
{
	int ret = 0;
	ssize_t(*sendpage) (struct socket *, struct page *, int, size_t, int);
	const int msg_flags = MSG_DONTWAIT | MSG_NOSIGNAL;
	struct writequeue_entry *e;
	int len, offset;

	down_read(&con->sock_sem);
	if (con->sock == NULL)
		goto out_connect;

	sendpage = con->sock->ops->sendpage;

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
			ret = sendpage(con->sock, e->page, offset, len,
				       msg_flags);
			if (ret == -EAGAIN || ret == 0)
				goto out;
			if (ret <= 0)
				goto send_error;
		}
		else {
			/* Don't starve people filling buffers */
			cond_resched();
		}

		spin_lock(&con->writequeue_lock);
		e->offset += ret;
		e->len -= ret;

		if (e->len == 0 && e->users == 0) {
			list_del(&e->list);
			kunmap(e->page);
			free_entry(e);
			continue;
		}
	}
	spin_unlock(&con->writequeue_lock);
out:
	up_read(&con->sock_sem);
	return;

send_error:
	up_read(&con->sock_sem);
	close_connection(con, false);
	lowcomms_connect_sock(con);
	return;

out_connect:
	up_read(&con->sock_sem);
	lowcomms_connect_sock(con);
	return;
}

static void clean_one_writequeue(struct connection *con)
{
	struct list_head *list;
	struct list_head *temp;

	spin_lock(&con->writequeue_lock);
	list_for_each_safe(list, temp, &con->writequeue) {
		struct writequeue_entry *e =
			list_entry(list, struct writequeue_entry, list);
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

	if (!connections)
		goto out;

	log_print("closing connection to node %d", nodeid);
	con = nodeid2con(nodeid, 0);
	if (con) {
		clean_one_writequeue(con);
		close_connection(con, true);
		atomic_set(&con->waiting_requests, 0);
	}
	return 0;

out:
	return -1;
}

/* API send message call, may queue the request */
/* N.B. This is the old interface - use the new one for new calls */
int lowcomms_send_message(int nodeid, char *buf, int len, gfp_t allocation)
{
	struct writequeue_entry *e;
	char *b;

	e = dlm_lowcomms_get_buffer(nodeid, len, allocation, &b);
	if (e) {
		memcpy(b, buf, len);
		dlm_lowcomms_commit_buffer(e);
		return 0;
	}
	return -ENOBUFS;
}

/* Look for activity on active sockets */
static void process_sockets(void)
{
	struct list_head *list;
	struct list_head *temp;
	int count = 0;

	spin_lock_bh(&read_sockets_lock);
	list_for_each_safe(list, temp, &read_sockets) {

		struct connection *con =
			list_entry(list, struct connection, read_list);
		list_del(&con->read_list);
		clear_bit(CF_READ_PENDING, &con->flags);

		spin_unlock_bh(&read_sockets_lock);

		/* This can reach zero if we are processing requests
		 * as they come in.
		 */
		if (atomic_read(&con->waiting_requests) == 0) {
			spin_lock_bh(&read_sockets_lock);
			continue;
		}

		do {
			con->rx_action(con);

			/* Don't starve out everyone else */
			if (++count >= MAX_RX_MSG_COUNT) {
				cond_resched();
				count = 0;
			}

		} while (!atomic_dec_and_test(&con->waiting_requests) &&
			 !kthread_should_stop());

		spin_lock_bh(&read_sockets_lock);
	}
	spin_unlock_bh(&read_sockets_lock);
}

/* Try to send any messages that are pending
 */
static void process_output_queue(void)
{
	struct list_head *list;
	struct list_head *temp;

	spin_lock_bh(&write_sockets_lock);
	list_for_each_safe(list, temp, &write_sockets) {
		struct connection *con =
			list_entry(list, struct connection, write_list);
		clear_bit(CF_WRITE_PENDING, &con->flags);
		list_del(&con->write_list);

		spin_unlock_bh(&write_sockets_lock);
		send_to_sock(con);
		spin_lock_bh(&write_sockets_lock);
	}
	spin_unlock_bh(&write_sockets_lock);
}

static void process_state_queue(void)
{
	struct list_head *list;
	struct list_head *temp;

	spin_lock_bh(&state_sockets_lock);
	list_for_each_safe(list, temp, &state_sockets) {
		struct connection *con =
			list_entry(list, struct connection, state_list);
		list_del(&con->state_list);
		clear_bit(CF_CONNECT_PENDING, &con->flags);
		spin_unlock_bh(&state_sockets_lock);

		connect_to_sock(con);
		spin_lock_bh(&state_sockets_lock);
	}
	spin_unlock_bh(&state_sockets_lock);
}


/* Discard all entries on the write queues */
static void clean_writequeues(void)
{
	int nodeid;

	for (nodeid = 1; nodeid < conn_array_size; nodeid++) {
		struct connection *con = nodeid2con(nodeid, 0);

		if (con)
			clean_one_writequeue(con);
	}
}

static int read_list_empty(void)
{
	int status;

	spin_lock_bh(&read_sockets_lock);
	status = list_empty(&read_sockets);
	spin_unlock_bh(&read_sockets_lock);

	return status;
}

/* DLM Transport comms receive daemon */
static int dlm_recvd(void *data)
{
	init_waitqueue_entry(&lowcomms_recv_waitq_head, current);
	add_wait_queue(&lowcomms_recv_waitq, &lowcomms_recv_waitq_head);

	while (!kthread_should_stop()) {
		set_current_state(TASK_INTERRUPTIBLE);
		if (read_list_empty())
			cond_resched();
		set_current_state(TASK_RUNNING);

		process_sockets();
	}

	return 0;
}

static int write_and_state_lists_empty(void)
{
	int status;

	spin_lock_bh(&write_sockets_lock);
	status = list_empty(&write_sockets);
	spin_unlock_bh(&write_sockets_lock);

	spin_lock_bh(&state_sockets_lock);
	if (list_empty(&state_sockets) == 0)
		status = 0;
	spin_unlock_bh(&state_sockets_lock);

	return status;
}

/* DLM Transport send daemon */
static int dlm_sendd(void *data)
{
	init_waitqueue_entry(&lowcomms_send_waitq_head, current);
	add_wait_queue(&lowcomms_send_waitq, &lowcomms_send_waitq_head);

	while (!kthread_should_stop()) {
		set_current_state(TASK_INTERRUPTIBLE);
		if (write_and_state_lists_empty())
			cond_resched();
		set_current_state(TASK_RUNNING);

		process_state_queue();
		process_output_queue();
	}

	return 0;
}

static void daemons_stop(void)
{
	kthread_stop(recv_task);
	kthread_stop(send_task);
}

static int daemons_start(void)
{
	struct task_struct *p;
	int error;

	p = kthread_run(dlm_recvd, NULL, "dlm_recvd");
	error = IS_ERR(p);
	if (error) {
		log_print("can't start dlm_recvd %d", error);
		return error;
	}
	recv_task = p;

	p = kthread_run(dlm_sendd, NULL, "dlm_sendd");
	error = IS_ERR(p);
	if (error) {
		log_print("can't start dlm_sendd %d", error);
		kthread_stop(recv_task);
		return error;
	}
	send_task = p;

	return 0;
}

/*
 * Return the largest buffer size we can cope with.
 */
int lowcomms_max_buffer_size(void)
{
	return PAGE_CACHE_SIZE;
}

void dlm_lowcomms_stop(void)
{
	int i;

	/* Set all the flags to prevent any
	   socket activity.
	*/
	for (i = 0; i < conn_array_size; i++) {
		if (connections[i])
			connections[i]->flags |= 0xFF;
	}

	daemons_stop();
	clean_writequeues();

	for (i = 0; i < conn_array_size; i++) {
		if (connections[i]) {
			close_connection(connections[i], true);
			if (connections[i]->othercon)
				kmem_cache_free(con_cache, connections[i]->othercon);
			kmem_cache_free(con_cache, connections[i]);
		}
	}

	kfree(connections);
	connections = NULL;

	kmem_cache_destroy(con_cache);
}

/* This is quite likely to sleep... */
int dlm_lowcomms_start(void)
{
	int error = 0;

	error = -ENOMEM;
	connections = kzalloc(sizeof(struct connection *) *
			      NODE_INCREMENT, GFP_KERNEL);
	if (!connections)
		goto out;

	conn_array_size = NODE_INCREMENT;

	if (dlm_our_addr(&dlm_local_addr, 0)) {
		log_print("no local IP address has been set");
		goto fail_free_conn;
	}
	if (!dlm_our_addr(&dlm_local_addr, 1)) {
		log_print("This dlm comms module does not support multi-homed clustering");
		goto fail_free_conn;
	}

	con_cache = kmem_cache_create("dlm_conn", sizeof(struct connection),
				      __alignof__(struct connection), 0,
				      NULL, NULL);
	if (!con_cache)
		goto fail_free_conn;


	/* Start listening */
	error = listen_for_all();
	if (error)
		goto fail_unlisten;

	error = daemons_start();
	if (error)
		goto fail_unlisten;

	return 0;

fail_unlisten:
	close_connection(connections[0], false);
	kmem_cache_free(con_cache, connections[0]);
	kmem_cache_destroy(con_cache);

fail_free_conn:
	kfree(connections);

out:
	return error;
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
