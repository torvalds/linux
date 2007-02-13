/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2007 Red Hat, Inc.  All rights reserved.
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
	struct mutex sock_mutex;
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
#define MAX_CONNECT_RETRIES 3
	struct connection *othercon;
	struct work_struct rwork; /* Receive workqueue */
	struct work_struct swork; /* Send workqueue */
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

/* Work queues */
static struct workqueue_struct *recv_workqueue;
static struct workqueue_struct *send_workqueue;

/* An array of pointers to connections, indexed by NODEID */
static struct connection **connections;
static DECLARE_MUTEX(connections_lock);
static struct kmem_cache *con_cache;
static int conn_array_size;

static void process_recv_sockets(struct work_struct *work);
static void process_send_sockets(struct work_struct *work);

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
		mutex_init(&con->sock_mutex);
		INIT_LIST_HEAD(&con->writequeue);
		spin_lock_init(&con->writequeue_lock);
		INIT_WORK(&con->swork, process_send_sockets);
		INIT_WORK(&con->rwork, process_recv_sockets);

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

	if (!test_and_set_bit(CF_READ_PENDING, &con->flags))
		queue_work(recv_workqueue, &con->rwork);
}

static void lowcomms_write_space(struct sock *sk)
{
	struct connection *con = sock2con(sk);

	if (!test_and_set_bit(CF_WRITE_PENDING, &con->flags))
		queue_work(send_workqueue, &con->swork);
}

static inline void lowcomms_connect_sock(struct connection *con)
{
	if (!test_and_set_bit(CF_CONNECT_PENDING, &con->flags))
		queue_work(send_workqueue, &con->swork);
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
	mutex_lock(&con->sock_mutex);

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
	mutex_unlock(&con->sock_mutex);
}

/* Data received from remote end */
static int receive_from_sock(struct connection *con)
{
	int ret = 0;
	struct msghdr msg = {};
	struct kvec iov[2];
	unsigned len;
	int r;
	int call_again_soon = 0;
	int nvec;

	mutex_lock(&con->sock_mutex);

	if (con->sock == NULL) {
		ret = -EAGAIN;
		goto out_close;
	}

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

	/*
	 * iov[0] is the bit of the circular buffer between the current end
	 * point (cb.base + cb.len) and the end of the buffer.
	 */
	iov[0].iov_len = con->cb.base - cbuf_data(&con->cb);
	iov[0].iov_base = page_address(con->rx_page) + cbuf_data(&con->cb);
	nvec = 1;

	/*
	 * iov[1] is the bit of the circular buffer between the start of the
	 * buffer and the start of the currently used section (cb.base)
	 */
	if (cbuf_data(&con->cb) >= con->cb.base) {
		iov[0].iov_len = PAGE_CACHE_SIZE - cbuf_data(&con->cb);
		iov[1].iov_len = con->cb.base;
		iov[1].iov_base = page_address(con->rx_page);
		nvec = 2;
	}
	len = iov[0].iov_len + iov[1].iov_len;

	r = ret = kernel_recvmsg(con->sock, &msg, iov, nvec, len,
			       MSG_DONTWAIT | MSG_NOSIGNAL);

	if (ret <= 0)
		goto out_close;
	if (ret == -EAGAIN)
		goto out_resched;

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
	if (ret != -EAGAIN && !test_bit(CF_IS_OTHERCON, &con->flags)) {
		close_connection(con, false);
		/* Reconnect when there is something to send */
	}
	/* Don't return success if we really got EOF */
	if (ret == 0)
		ret = -EAGAIN;

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

	memset(&peeraddr, 0, sizeof(peeraddr));
	result = sock_create_kern(dlm_local_addr.ss_family, SOCK_STREAM,
				  IPPROTO_TCP, &newsock);
	if (result < 0)
		return -ENOMEM;

	mutex_lock_nested(&con->sock_mutex, 0);

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
		mutex_unlock(&con->sock_mutex);
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
	mutex_lock_nested(&newcon->sock_mutex, 1);
	if (newcon->sock) {
		struct connection *othercon = newcon->othercon;

		if (!othercon) {
			othercon = kmem_cache_zalloc(con_cache, GFP_KERNEL);
			if (!othercon) {
				printk("dlm: failed to allocate incoming socket\n");
				mutex_unlock(&newcon->sock_mutex);
				result = -ENOMEM;
				goto accept_err;
			}
			othercon->nodeid = nodeid;
			othercon->rx_action = receive_from_sock;
			mutex_init(&othercon->sock_mutex);
			INIT_WORK(&othercon->swork, process_send_sockets);
			INIT_WORK(&othercon->rwork, process_recv_sockets);
			set_bit(CF_IS_OTHERCON, &othercon->flags);
			newcon->othercon = othercon;
		}
		othercon->sock = newsock;
		newsock->sk->sk_user_data = othercon;
		add_sock(newsock, othercon);
		addcon = othercon;
	}
	else {
		newsock->sk->sk_user_data = newcon;
		newcon->rx_action = receive_from_sock;
		add_sock(newsock, newcon);
		addcon = newcon;
	}

	mutex_unlock(&newcon->sock_mutex);

	/*
	 * Add it to the active queue in case we got data
	 * beween processing the accept adding the socket
	 * to the read_sockets list
	 */
	if (!test_and_set_bit(CF_READ_PENDING, &addcon->flags))
		queue_work(recv_workqueue, &addcon->rwork);
	mutex_unlock(&con->sock_mutex);

	return 0;

accept_err:
	mutex_unlock(&con->sock_mutex);
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

	mutex_lock(&con->sock_mutex);
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

	make_sockaddr(&saddr, dlm_config.ci_tcp_port, &addr_len);

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
	mutex_unlock(&con->sock_mutex);
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
	make_sockaddr(saddr, dlm_config.ci_tcp_port, &addr_len);
	result = sock->ops->bind(sock, (struct sockaddr *) saddr, addr_len);
	if (result < 0) {
		printk("dlm: Can't bind to port %d\n", dlm_config.ci_tcp_port);
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
		printk("dlm: Can't listen on port %d\n", dlm_config.ci_tcp_port);
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

	spin_lock(&con->writequeue_lock);
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

	spin_lock(&con->writequeue_lock);
	users = --e->users;
	if (users)
		goto out;
	e->len = e->end - e->offset;
	kunmap(e->page);
	spin_unlock(&con->writequeue_lock);

	if (!test_and_set_bit(CF_WRITE_PENDING, &con->flags)) {
		queue_work(send_workqueue, &con->swork);
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

	mutex_lock(&con->sock_mutex);
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
		kmap(e->page);

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
	mutex_unlock(&con->sock_mutex);
	return;

send_error:
	mutex_unlock(&con->sock_mutex);
	close_connection(con, false);
	lowcomms_connect_sock(con);
	return;

out_connect:
	mutex_unlock(&con->sock_mutex);
	connect_to_sock(con);
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
	}
	return 0;

out:
	return -1;
}

/* Look for activity on active sockets */
static void process_recv_sockets(struct work_struct *work)
{
	struct connection *con = container_of(work, struct connection, rwork);
	int err;

	clear_bit(CF_READ_PENDING, &con->flags);
	do {
		err = con->rx_action(con);
	} while (!err);
}


static void process_send_sockets(struct work_struct *work)
{
	struct connection *con = container_of(work, struct connection, swork);

	if (test_and_clear_bit(CF_CONNECT_PENDING, &con->flags)) {
		connect_to_sock(con);
	}

	clear_bit(CF_WRITE_PENDING, &con->flags);
	send_to_sock(con);
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

static void work_stop(void)
{
	destroy_workqueue(recv_workqueue);
	destroy_workqueue(send_workqueue);
}

static int work_start(void)
{
	int error;
	recv_workqueue = create_workqueue("dlm_recv");
	error = IS_ERR(recv_workqueue);
	if (error) {
		log_print("can't start dlm_recv %d", error);
		return error;
	}

	send_workqueue = create_singlethread_workqueue("dlm_send");
	error = IS_ERR(send_workqueue);
	if (error) {
		log_print("can't start dlm_send %d", error);
		destroy_workqueue(recv_workqueue);
		return error;
	}

	return 0;
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

	work_stop();
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

	error = work_start();
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
