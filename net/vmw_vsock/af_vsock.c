/*
 * VMware vSockets Driver
 *
 * Copyright (C) 2007-2013 VMware, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation version 2 and no later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

/* Implementation notes:
 *
 * - There are two kinds of sockets: those created by user action (such as
 * calling socket(2)) and those created by incoming connection request packets.
 *
 * - There are two "global" tables, one for bound sockets (sockets that have
 * specified an address that they are responsible for) and one for connected
 * sockets (sockets that have established a connection with another socket).
 * These tables are "global" in that all sockets on the system are placed
 * within them. - Note, though, that the bound table contains an extra entry
 * for a list of unbound sockets and SOCK_DGRAM sockets will always remain in
 * that list. The bound table is used solely for lookup of sockets when packets
 * are received and that's not necessary for SOCK_DGRAM sockets since we create
 * a datagram handle for each and need not perform a lookup.  Keeping SOCK_DGRAM
 * sockets out of the bound hash buckets will reduce the chance of collisions
 * when looking for SOCK_STREAM sockets and prevents us from having to check the
 * socket type in the hash table lookups.
 *
 * - Sockets created by user action will either be "client" sockets that
 * initiate a connection or "server" sockets that listen for connections; we do
 * not support simultaneous connects (two "client" sockets connecting).
 *
 * - "Server" sockets are referred to as listener sockets throughout this
 * implementation because they are in the TCP_LISTEN state.  When a
 * connection request is received (the second kind of socket mentioned above),
 * we create a new socket and refer to it as a pending socket.  These pending
 * sockets are placed on the pending connection list of the listener socket.
 * When future packets are received for the address the listener socket is
 * bound to, we check if the source of the packet is from one that has an
 * existing pending connection.  If it does, we process the packet for the
 * pending socket.  When that socket reaches the connected state, it is removed
 * from the listener socket's pending list and enqueued in the listener
 * socket's accept queue.  Callers of accept(2) will accept connected sockets
 * from the listener socket's accept queue.  If the socket cannot be accepted
 * for some reason then it is marked rejected.  Once the connection is
 * accepted, it is owned by the user process and the responsibility for cleanup
 * falls with that user process.
 *
 * - It is possible that these pending sockets will never reach the connected
 * state; in fact, we may never receive another packet after the connection
 * request.  Because of this, we must schedule a cleanup function to run in the
 * future, after some amount of time passes where a connection should have been
 * established.  This function ensures that the socket is off all lists so it
 * cannot be retrieved, then drops all references to the socket so it is cleaned
 * up (sock_put() -> sk_free() -> our sk_destruct implementation).  Note this
 * function will also cleanup rejected sockets, those that reach the connected
 * state but leave it before they have been accepted.
 *
 * - Lock ordering for pending or accept queue sockets is:
 *
 *     lock_sock(listener);
 *     lock_sock_nested(pending, SINGLE_DEPTH_NESTING);
 *
 * Using explicit nested locking keeps lockdep happy since normally only one
 * lock of a given class may be taken at a time.
 *
 * - Sockets created by user action will be cleaned up when the user process
 * calls close(2), causing our release implementation to be called. Our release
 * implementation will perform some cleanup then drop the last reference so our
 * sk_destruct implementation is invoked.  Our sk_destruct implementation will
 * perform additional cleanup that's common for both types of sockets.
 *
 * - A socket's reference count is what ensures that the structure won't be
 * freed.  Each entry in a list (such as the "global" bound and connected tables
 * and the listener socket's pending list and connected queue) ensures a
 * reference.  When we defer work until process context and pass a socket as our
 * argument, we must ensure the reference count is increased to ensure the
 * socket isn't freed before the function is run; the deferred function will
 * then drop the reference.
 *
 * - sk->sk_state uses the TCP state constants because they are widely used by
 * other address families and exposed to userspace tools like ss(8):
 *
 *   TCP_CLOSE - unconnected
 *   TCP_SYN_SENT - connecting
 *   TCP_ESTABLISHED - connected
 *   TCP_CLOSING - disconnecting
 *   TCP_LISTEN - listening
 */

#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/cred.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/sched/signal.h>
#include <linux/kmod.h>
#include <linux/list.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/net.h>
#include <linux/poll.h>
#include <linux/random.h>
#include <linux/skbuff.h>
#include <linux/smp.h>
#include <linux/socket.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <net/sock.h>
#include <net/af_vsock.h>

static int __vsock_bind(struct sock *sk, struct sockaddr_vm *addr);
static void vsock_sk_destruct(struct sock *sk);
static int vsock_queue_rcv_skb(struct sock *sk, struct sk_buff *skb);

/* Protocol family. */
static struct proto vsock_proto = {
	.name = "AF_VSOCK",
	.owner = THIS_MODULE,
	.obj_size = sizeof(struct vsock_sock),
};

/* The default peer timeout indicates how long we will wait for a peer response
 * to a control message.
 */
#define VSOCK_DEFAULT_CONNECT_TIMEOUT (2 * HZ)

static const struct vsock_transport *transport;
static DEFINE_MUTEX(vsock_register_mutex);

/**** EXPORTS ****/

/* Get the ID of the local context.  This is transport dependent. */

int vm_sockets_get_local_cid(void)
{
	return transport->get_local_cid();
}
EXPORT_SYMBOL_GPL(vm_sockets_get_local_cid);

/**** UTILS ****/

/* Each bound VSocket is stored in the bind hash table and each connected
 * VSocket is stored in the connected hash table.
 *
 * Unbound sockets are all put on the same list attached to the end of the hash
 * table (vsock_unbound_sockets).  Bound sockets are added to the hash table in
 * the bucket that their local address hashes to (vsock_bound_sockets(addr)
 * represents the list that addr hashes to).
 *
 * Specifically, we initialize the vsock_bind_table array to a size of
 * VSOCK_HASH_SIZE + 1 so that vsock_bind_table[0] through
 * vsock_bind_table[VSOCK_HASH_SIZE - 1] are for bound sockets and
 * vsock_bind_table[VSOCK_HASH_SIZE] is for unbound sockets.  The hash function
 * mods with VSOCK_HASH_SIZE to ensure this.
 */
#define MAX_PORT_RETRIES        24

#define VSOCK_HASH(addr)        ((addr)->svm_port % VSOCK_HASH_SIZE)
#define vsock_bound_sockets(addr) (&vsock_bind_table[VSOCK_HASH(addr)])
#define vsock_unbound_sockets     (&vsock_bind_table[VSOCK_HASH_SIZE])

/* XXX This can probably be implemented in a better way. */
#define VSOCK_CONN_HASH(src, dst)				\
	(((src)->svm_cid ^ (dst)->svm_port) % VSOCK_HASH_SIZE)
#define vsock_connected_sockets(src, dst)		\
	(&vsock_connected_table[VSOCK_CONN_HASH(src, dst)])
#define vsock_connected_sockets_vsk(vsk)				\
	vsock_connected_sockets(&(vsk)->remote_addr, &(vsk)->local_addr)

struct list_head vsock_bind_table[VSOCK_HASH_SIZE + 1];
EXPORT_SYMBOL_GPL(vsock_bind_table);
struct list_head vsock_connected_table[VSOCK_HASH_SIZE];
EXPORT_SYMBOL_GPL(vsock_connected_table);
DEFINE_SPINLOCK(vsock_table_lock);
EXPORT_SYMBOL_GPL(vsock_table_lock);

/* Autobind this socket to the local address if necessary. */
static int vsock_auto_bind(struct vsock_sock *vsk)
{
	struct sock *sk = sk_vsock(vsk);
	struct sockaddr_vm local_addr;

	if (vsock_addr_bound(&vsk->local_addr))
		return 0;
	vsock_addr_init(&local_addr, VMADDR_CID_ANY, VMADDR_PORT_ANY);
	return __vsock_bind(sk, &local_addr);
}

static int __init vsock_init_tables(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(vsock_bind_table); i++)
		INIT_LIST_HEAD(&vsock_bind_table[i]);

	for (i = 0; i < ARRAY_SIZE(vsock_connected_table); i++)
		INIT_LIST_HEAD(&vsock_connected_table[i]);
	return 0;
}

static void __vsock_insert_bound(struct list_head *list,
				 struct vsock_sock *vsk)
{
	sock_hold(&vsk->sk);
	list_add(&vsk->bound_table, list);
}

static void __vsock_insert_connected(struct list_head *list,
				     struct vsock_sock *vsk)
{
	sock_hold(&vsk->sk);
	list_add(&vsk->connected_table, list);
}

static void __vsock_remove_bound(struct vsock_sock *vsk)
{
	list_del_init(&vsk->bound_table);
	sock_put(&vsk->sk);
}

static void __vsock_remove_connected(struct vsock_sock *vsk)
{
	list_del_init(&vsk->connected_table);
	sock_put(&vsk->sk);
}

static struct sock *__vsock_find_bound_socket(struct sockaddr_vm *addr)
{
	struct vsock_sock *vsk;

	list_for_each_entry(vsk, vsock_bound_sockets(addr), bound_table)
		if (addr->svm_port == vsk->local_addr.svm_port)
			return sk_vsock(vsk);

	return NULL;
}

static struct sock *__vsock_find_connected_socket(struct sockaddr_vm *src,
						  struct sockaddr_vm *dst)
{
	struct vsock_sock *vsk;

	list_for_each_entry(vsk, vsock_connected_sockets(src, dst),
			    connected_table) {
		if (vsock_addr_equals_addr(src, &vsk->remote_addr) &&
		    dst->svm_port == vsk->local_addr.svm_port) {
			return sk_vsock(vsk);
		}
	}

	return NULL;
}

static void vsock_insert_unbound(struct vsock_sock *vsk)
{
	spin_lock_bh(&vsock_table_lock);
	__vsock_insert_bound(vsock_unbound_sockets, vsk);
	spin_unlock_bh(&vsock_table_lock);
}

void vsock_insert_connected(struct vsock_sock *vsk)
{
	struct list_head *list = vsock_connected_sockets(
		&vsk->remote_addr, &vsk->local_addr);

	spin_lock_bh(&vsock_table_lock);
	__vsock_insert_connected(list, vsk);
	spin_unlock_bh(&vsock_table_lock);
}
EXPORT_SYMBOL_GPL(vsock_insert_connected);

void vsock_remove_bound(struct vsock_sock *vsk)
{
	spin_lock_bh(&vsock_table_lock);
	if (__vsock_in_bound_table(vsk))
		__vsock_remove_bound(vsk);
	spin_unlock_bh(&vsock_table_lock);
}
EXPORT_SYMBOL_GPL(vsock_remove_bound);

void vsock_remove_connected(struct vsock_sock *vsk)
{
	spin_lock_bh(&vsock_table_lock);
	if (__vsock_in_connected_table(vsk))
		__vsock_remove_connected(vsk);
	spin_unlock_bh(&vsock_table_lock);
}
EXPORT_SYMBOL_GPL(vsock_remove_connected);

struct sock *vsock_find_bound_socket(struct sockaddr_vm *addr)
{
	struct sock *sk;

	spin_lock_bh(&vsock_table_lock);
	sk = __vsock_find_bound_socket(addr);
	if (sk)
		sock_hold(sk);

	spin_unlock_bh(&vsock_table_lock);

	return sk;
}
EXPORT_SYMBOL_GPL(vsock_find_bound_socket);

struct sock *vsock_find_connected_socket(struct sockaddr_vm *src,
					 struct sockaddr_vm *dst)
{
	struct sock *sk;

	spin_lock_bh(&vsock_table_lock);
	sk = __vsock_find_connected_socket(src, dst);
	if (sk)
		sock_hold(sk);

	spin_unlock_bh(&vsock_table_lock);

	return sk;
}
EXPORT_SYMBOL_GPL(vsock_find_connected_socket);

void vsock_remove_sock(struct vsock_sock *vsk)
{
	vsock_remove_bound(vsk);
	vsock_remove_connected(vsk);
}
EXPORT_SYMBOL_GPL(vsock_remove_sock);

void vsock_for_each_connected_socket(void (*fn)(struct sock *sk))
{
	int i;

	spin_lock_bh(&vsock_table_lock);

	for (i = 0; i < ARRAY_SIZE(vsock_connected_table); i++) {
		struct vsock_sock *vsk;
		list_for_each_entry(vsk, &vsock_connected_table[i],
				    connected_table)
			fn(sk_vsock(vsk));
	}

	spin_unlock_bh(&vsock_table_lock);
}
EXPORT_SYMBOL_GPL(vsock_for_each_connected_socket);

void vsock_add_pending(struct sock *listener, struct sock *pending)
{
	struct vsock_sock *vlistener;
	struct vsock_sock *vpending;

	vlistener = vsock_sk(listener);
	vpending = vsock_sk(pending);

	sock_hold(pending);
	sock_hold(listener);
	list_add_tail(&vpending->pending_links, &vlistener->pending_links);
}
EXPORT_SYMBOL_GPL(vsock_add_pending);

void vsock_remove_pending(struct sock *listener, struct sock *pending)
{
	struct vsock_sock *vpending = vsock_sk(pending);

	list_del_init(&vpending->pending_links);
	sock_put(listener);
	sock_put(pending);
}
EXPORT_SYMBOL_GPL(vsock_remove_pending);

void vsock_enqueue_accept(struct sock *listener, struct sock *connected)
{
	struct vsock_sock *vlistener;
	struct vsock_sock *vconnected;

	vlistener = vsock_sk(listener);
	vconnected = vsock_sk(connected);

	sock_hold(connected);
	sock_hold(listener);
	list_add_tail(&vconnected->accept_queue, &vlistener->accept_queue);
}
EXPORT_SYMBOL_GPL(vsock_enqueue_accept);

static struct sock *vsock_dequeue_accept(struct sock *listener)
{
	struct vsock_sock *vlistener;
	struct vsock_sock *vconnected;

	vlistener = vsock_sk(listener);

	if (list_empty(&vlistener->accept_queue))
		return NULL;

	vconnected = list_entry(vlistener->accept_queue.next,
				struct vsock_sock, accept_queue);

	list_del_init(&vconnected->accept_queue);
	sock_put(listener);
	/* The caller will need a reference on the connected socket so we let
	 * it call sock_put().
	 */

	return sk_vsock(vconnected);
}

static bool vsock_is_accept_queue_empty(struct sock *sk)
{
	struct vsock_sock *vsk = vsock_sk(sk);
	return list_empty(&vsk->accept_queue);
}

static bool vsock_is_pending(struct sock *sk)
{
	struct vsock_sock *vsk = vsock_sk(sk);
	return !list_empty(&vsk->pending_links);
}

static int vsock_send_shutdown(struct sock *sk, int mode)
{
	return transport->shutdown(vsock_sk(sk), mode);
}

static void vsock_pending_work(struct work_struct *work)
{
	struct sock *sk;
	struct sock *listener;
	struct vsock_sock *vsk;
	bool cleanup;

	vsk = container_of(work, struct vsock_sock, pending_work.work);
	sk = sk_vsock(vsk);
	listener = vsk->listener;
	cleanup = true;

	lock_sock(listener);
	lock_sock_nested(sk, SINGLE_DEPTH_NESTING);

	if (vsock_is_pending(sk)) {
		vsock_remove_pending(listener, sk);

		listener->sk_ack_backlog--;
	} else if (!vsk->rejected) {
		/* We are not on the pending list and accept() did not reject
		 * us, so we must have been accepted by our user process.  We
		 * just need to drop our references to the sockets and be on
		 * our way.
		 */
		cleanup = false;
		goto out;
	}

	/* We need to remove ourself from the global connected sockets list so
	 * incoming packets can't find this socket, and to reduce the reference
	 * count.
	 */
	vsock_remove_connected(vsk);

	sk->sk_state = TCP_CLOSE;

out:
	release_sock(sk);
	release_sock(listener);
	if (cleanup)
		sock_put(sk);

	sock_put(sk);
	sock_put(listener);
}

/**** SOCKET OPERATIONS ****/

static int __vsock_bind_stream(struct vsock_sock *vsk,
			       struct sockaddr_vm *addr)
{
	static u32 port = 0;
	struct sockaddr_vm new_addr;

	if (!port)
		port = LAST_RESERVED_PORT + 1 +
			prandom_u32_max(U32_MAX - LAST_RESERVED_PORT);

	vsock_addr_init(&new_addr, addr->svm_cid, addr->svm_port);

	if (addr->svm_port == VMADDR_PORT_ANY) {
		bool found = false;
		unsigned int i;

		for (i = 0; i < MAX_PORT_RETRIES; i++) {
			if (port <= LAST_RESERVED_PORT)
				port = LAST_RESERVED_PORT + 1;

			new_addr.svm_port = port++;

			if (!__vsock_find_bound_socket(&new_addr)) {
				found = true;
				break;
			}
		}

		if (!found)
			return -EADDRNOTAVAIL;
	} else {
		/* If port is in reserved range, ensure caller
		 * has necessary privileges.
		 */
		if (addr->svm_port <= LAST_RESERVED_PORT &&
		    !capable(CAP_NET_BIND_SERVICE)) {
			return -EACCES;
		}

		if (__vsock_find_bound_socket(&new_addr))
			return -EADDRINUSE;
	}

	vsock_addr_init(&vsk->local_addr, new_addr.svm_cid, new_addr.svm_port);

	/* Remove stream sockets from the unbound list and add them to the hash
	 * table for easy lookup by its address.  The unbound list is simply an
	 * extra entry at the end of the hash table, a trick used by AF_UNIX.
	 */
	__vsock_remove_bound(vsk);
	__vsock_insert_bound(vsock_bound_sockets(&vsk->local_addr), vsk);

	return 0;
}

static int __vsock_bind_dgram(struct vsock_sock *vsk,
			      struct sockaddr_vm *addr)
{
	return transport->dgram_bind(vsk, addr);
}

static int __vsock_bind(struct sock *sk, struct sockaddr_vm *addr)
{
	struct vsock_sock *vsk = vsock_sk(sk);
	u32 cid;
	int retval;

	/* First ensure this socket isn't already bound. */
	if (vsock_addr_bound(&vsk->local_addr))
		return -EINVAL;

	/* Now bind to the provided address or select appropriate values if
	 * none are provided (VMADDR_CID_ANY and VMADDR_PORT_ANY).  Note that
	 * like AF_INET prevents binding to a non-local IP address (in most
	 * cases), we only allow binding to the local CID.
	 */
	cid = transport->get_local_cid();
	if (addr->svm_cid != cid && addr->svm_cid != VMADDR_CID_ANY)
		return -EADDRNOTAVAIL;

	switch (sk->sk_socket->type) {
	case SOCK_STREAM:
		spin_lock_bh(&vsock_table_lock);
		retval = __vsock_bind_stream(vsk, addr);
		spin_unlock_bh(&vsock_table_lock);
		break;

	case SOCK_DGRAM:
		retval = __vsock_bind_dgram(vsk, addr);
		break;

	default:
		retval = -EINVAL;
		break;
	}

	return retval;
}

static void vsock_connect_timeout(struct work_struct *work);

struct sock *__vsock_create(struct net *net,
			    struct socket *sock,
			    struct sock *parent,
			    gfp_t priority,
			    unsigned short type,
			    int kern)
{
	struct sock *sk;
	struct vsock_sock *psk;
	struct vsock_sock *vsk;

	sk = sk_alloc(net, AF_VSOCK, priority, &vsock_proto, kern);
	if (!sk)
		return NULL;

	sock_init_data(sock, sk);

	/* sk->sk_type is normally set in sock_init_data, but only if sock is
	 * non-NULL. We make sure that our sockets always have a type by
	 * setting it here if needed.
	 */
	if (!sock)
		sk->sk_type = type;

	vsk = vsock_sk(sk);
	vsock_addr_init(&vsk->local_addr, VMADDR_CID_ANY, VMADDR_PORT_ANY);
	vsock_addr_init(&vsk->remote_addr, VMADDR_CID_ANY, VMADDR_PORT_ANY);

	sk->sk_destruct = vsock_sk_destruct;
	sk->sk_backlog_rcv = vsock_queue_rcv_skb;
	sock_reset_flag(sk, SOCK_DONE);

	INIT_LIST_HEAD(&vsk->bound_table);
	INIT_LIST_HEAD(&vsk->connected_table);
	vsk->listener = NULL;
	INIT_LIST_HEAD(&vsk->pending_links);
	INIT_LIST_HEAD(&vsk->accept_queue);
	vsk->rejected = false;
	vsk->sent_request = false;
	vsk->ignore_connecting_rst = false;
	vsk->peer_shutdown = 0;
	INIT_DELAYED_WORK(&vsk->connect_work, vsock_connect_timeout);
	INIT_DELAYED_WORK(&vsk->pending_work, vsock_pending_work);

	psk = parent ? vsock_sk(parent) : NULL;
	if (parent) {
		vsk->trusted = psk->trusted;
		vsk->owner = get_cred(psk->owner);
		vsk->connect_timeout = psk->connect_timeout;
	} else {
		vsk->trusted = capable(CAP_NET_ADMIN);
		vsk->owner = get_current_cred();
		vsk->connect_timeout = VSOCK_DEFAULT_CONNECT_TIMEOUT;
	}

	if (transport->init(vsk, psk) < 0) {
		sk_free(sk);
		return NULL;
	}

	if (sock)
		vsock_insert_unbound(vsk);

	return sk;
}
EXPORT_SYMBOL_GPL(__vsock_create);

static void __vsock_release(struct sock *sk, int level)
{
	if (sk) {
		struct sk_buff *skb;
		struct sock *pending;
		struct vsock_sock *vsk;

		vsk = vsock_sk(sk);
		pending = NULL;	/* Compiler warning. */

		/* The release call is supposed to use lock_sock_nested()
		 * rather than lock_sock(), if a sock lock should be acquired.
		 */
		transport->release(vsk);

		/* When "level" is SINGLE_DEPTH_NESTING, use the nested
		 * version to avoid the warning "possible recursive locking
		 * detected". When "level" is 0, lock_sock_nested(sk, level)
		 * is the same as lock_sock(sk).
		 */
		lock_sock_nested(sk, level);
		sock_orphan(sk);
		sk->sk_shutdown = SHUTDOWN_MASK;

		while ((skb = skb_dequeue(&sk->sk_receive_queue)))
			kfree_skb(skb);

		/* Clean up any sockets that never were accepted. */
		while ((pending = vsock_dequeue_accept(sk)) != NULL) {
			__vsock_release(pending, SINGLE_DEPTH_NESTING);
			sock_put(pending);
		}

		release_sock(sk);
		sock_put(sk);
	}
}

static void vsock_sk_destruct(struct sock *sk)
{
	struct vsock_sock *vsk = vsock_sk(sk);

	transport->destruct(vsk);

	/* When clearing these addresses, there's no need to set the family and
	 * possibly register the address family with the kernel.
	 */
	vsock_addr_init(&vsk->local_addr, VMADDR_CID_ANY, VMADDR_PORT_ANY);
	vsock_addr_init(&vsk->remote_addr, VMADDR_CID_ANY, VMADDR_PORT_ANY);

	put_cred(vsk->owner);
}

static int vsock_queue_rcv_skb(struct sock *sk, struct sk_buff *skb)
{
	int err;

	err = sock_queue_rcv_skb(sk, skb);
	if (err)
		kfree_skb(skb);

	return err;
}

s64 vsock_stream_has_data(struct vsock_sock *vsk)
{
	return transport->stream_has_data(vsk);
}
EXPORT_SYMBOL_GPL(vsock_stream_has_data);

s64 vsock_stream_has_space(struct vsock_sock *vsk)
{
	return transport->stream_has_space(vsk);
}
EXPORT_SYMBOL_GPL(vsock_stream_has_space);

static int vsock_release(struct socket *sock)
{
	__vsock_release(sock->sk, 0);
	sock->sk = NULL;
	sock->state = SS_FREE;

	return 0;
}

static int
vsock_bind(struct socket *sock, struct sockaddr *addr, int addr_len)
{
	int err;
	struct sock *sk;
	struct sockaddr_vm *vm_addr;

	sk = sock->sk;

	if (vsock_addr_cast(addr, addr_len, &vm_addr) != 0)
		return -EINVAL;

	lock_sock(sk);
	err = __vsock_bind(sk, vm_addr);
	release_sock(sk);

	return err;
}

static int vsock_getname(struct socket *sock,
			 struct sockaddr *addr, int peer)
{
	int err;
	struct sock *sk;
	struct vsock_sock *vsk;
	struct sockaddr_vm *vm_addr;

	sk = sock->sk;
	vsk = vsock_sk(sk);
	err = 0;

	lock_sock(sk);

	if (peer) {
		if (sock->state != SS_CONNECTED) {
			err = -ENOTCONN;
			goto out;
		}
		vm_addr = &vsk->remote_addr;
	} else {
		vm_addr = &vsk->local_addr;
	}

	if (!vm_addr) {
		err = -EINVAL;
		goto out;
	}

	/* sys_getsockname() and sys_getpeername() pass us a
	 * MAX_SOCK_ADDR-sized buffer and don't set addr_len.  Unfortunately
	 * that macro is defined in socket.c instead of .h, so we hardcode its
	 * value here.
	 */
	BUILD_BUG_ON(sizeof(*vm_addr) > 128);
	memcpy(addr, vm_addr, sizeof(*vm_addr));
	err = sizeof(*vm_addr);

out:
	release_sock(sk);
	return err;
}

static int vsock_shutdown(struct socket *sock, int mode)
{
	int err;
	struct sock *sk;

	/* User level uses SHUT_RD (0) and SHUT_WR (1), but the kernel uses
	 * RCV_SHUTDOWN (1) and SEND_SHUTDOWN (2), so we must increment mode
	 * here like the other address families do.  Note also that the
	 * increment makes SHUT_RDWR (2) into RCV_SHUTDOWN | SEND_SHUTDOWN (3),
	 * which is what we want.
	 */
	mode++;

	if ((mode & ~SHUTDOWN_MASK) || !mode)
		return -EINVAL;

	/* If this is a STREAM socket and it is not connected then bail out
	 * immediately.  If it is a DGRAM socket then we must first kick the
	 * socket so that it wakes up from any sleeping calls, for example
	 * recv(), and then afterwards return the error.
	 */

	sk = sock->sk;
	if (sock->state == SS_UNCONNECTED) {
		err = -ENOTCONN;
		if (sk->sk_type == SOCK_STREAM)
			return err;
	} else {
		sock->state = SS_DISCONNECTING;
		err = 0;
	}

	/* Receive and send shutdowns are treated alike. */
	mode = mode & (RCV_SHUTDOWN | SEND_SHUTDOWN);
	if (mode) {
		lock_sock(sk);
		sk->sk_shutdown |= mode;
		sk->sk_state_change(sk);
		release_sock(sk);

		if (sk->sk_type == SOCK_STREAM) {
			sock_reset_flag(sk, SOCK_DONE);
			vsock_send_shutdown(sk, mode);
		}
	}

	return err;
}

static __poll_t vsock_poll(struct file *file, struct socket *sock,
			       poll_table *wait)
{
	struct sock *sk;
	__poll_t mask;
	struct vsock_sock *vsk;

	sk = sock->sk;
	vsk = vsock_sk(sk);

	poll_wait(file, sk_sleep(sk), wait);
	mask = 0;

	if (sk->sk_err)
		/* Signify that there has been an error on this socket. */
		mask |= EPOLLERR;

	/* INET sockets treat local write shutdown and peer write shutdown as a
	 * case of EPOLLHUP set.
	 */
	if ((sk->sk_shutdown == SHUTDOWN_MASK) ||
	    ((sk->sk_shutdown & SEND_SHUTDOWN) &&
	     (vsk->peer_shutdown & SEND_SHUTDOWN))) {
		mask |= EPOLLHUP;
	}

	if (sk->sk_shutdown & RCV_SHUTDOWN ||
	    vsk->peer_shutdown & SEND_SHUTDOWN) {
		mask |= EPOLLRDHUP;
	}

	if (sock->type == SOCK_DGRAM) {
		/* For datagram sockets we can read if there is something in
		 * the queue and write as long as the socket isn't shutdown for
		 * sending.
		 */
		if (!skb_queue_empty_lockless(&sk->sk_receive_queue) ||
		    (sk->sk_shutdown & RCV_SHUTDOWN)) {
			mask |= EPOLLIN | EPOLLRDNORM;
		}

		if (!(sk->sk_shutdown & SEND_SHUTDOWN))
			mask |= EPOLLOUT | EPOLLWRNORM | EPOLLWRBAND;

	} else if (sock->type == SOCK_STREAM) {
		lock_sock(sk);

		/* Listening sockets that have connections in their accept
		 * queue can be read.
		 */
		if (sk->sk_state == TCP_LISTEN
		    && !vsock_is_accept_queue_empty(sk))
			mask |= EPOLLIN | EPOLLRDNORM;

		/* If there is something in the queue then we can read. */
		if (transport->stream_is_active(vsk) &&
		    !(sk->sk_shutdown & RCV_SHUTDOWN)) {
			bool data_ready_now = false;
			int ret = transport->notify_poll_in(
					vsk, 1, &data_ready_now);
			if (ret < 0) {
				mask |= EPOLLERR;
			} else {
				if (data_ready_now)
					mask |= EPOLLIN | EPOLLRDNORM;

			}
		}

		/* Sockets whose connections have been closed, reset, or
		 * terminated should also be considered read, and we check the
		 * shutdown flag for that.
		 */
		if (sk->sk_shutdown & RCV_SHUTDOWN ||
		    vsk->peer_shutdown & SEND_SHUTDOWN) {
			mask |= EPOLLIN | EPOLLRDNORM;
		}

		/* Connected sockets that can produce data can be written. */
		if (sk->sk_state == TCP_ESTABLISHED) {
			if (!(sk->sk_shutdown & SEND_SHUTDOWN)) {
				bool space_avail_now = false;
				int ret = transport->notify_poll_out(
						vsk, 1, &space_avail_now);
				if (ret < 0) {
					mask |= EPOLLERR;
				} else {
					if (space_avail_now)
						/* Remove EPOLLWRBAND since INET
						 * sockets are not setting it.
						 */
						mask |= EPOLLOUT | EPOLLWRNORM;

				}
			}
		}

		/* Simulate INET socket poll behaviors, which sets
		 * EPOLLOUT|EPOLLWRNORM when peer is closed and nothing to read,
		 * but local send is not shutdown.
		 */
		if (sk->sk_state == TCP_CLOSE || sk->sk_state == TCP_CLOSING) {
			if (!(sk->sk_shutdown & SEND_SHUTDOWN))
				mask |= EPOLLOUT | EPOLLWRNORM;

		}

		release_sock(sk);
	}

	return mask;
}

static int vsock_dgram_sendmsg(struct socket *sock, struct msghdr *msg,
			       size_t len)
{
	int err;
	struct sock *sk;
	struct vsock_sock *vsk;
	struct sockaddr_vm *remote_addr;

	if (msg->msg_flags & MSG_OOB)
		return -EOPNOTSUPP;

	/* For now, MSG_DONTWAIT is always assumed... */
	err = 0;
	sk = sock->sk;
	vsk = vsock_sk(sk);

	lock_sock(sk);

	err = vsock_auto_bind(vsk);
	if (err)
		goto out;


	/* If the provided message contains an address, use that.  Otherwise
	 * fall back on the socket's remote handle (if it has been connected).
	 */
	if (msg->msg_name &&
	    vsock_addr_cast(msg->msg_name, msg->msg_namelen,
			    &remote_addr) == 0) {
		/* Ensure this address is of the right type and is a valid
		 * destination.
		 */

		if (remote_addr->svm_cid == VMADDR_CID_ANY)
			remote_addr->svm_cid = transport->get_local_cid();

		if (!vsock_addr_bound(remote_addr)) {
			err = -EINVAL;
			goto out;
		}
	} else if (sock->state == SS_CONNECTED) {
		remote_addr = &vsk->remote_addr;

		if (remote_addr->svm_cid == VMADDR_CID_ANY)
			remote_addr->svm_cid = transport->get_local_cid();

		/* XXX Should connect() or this function ensure remote_addr is
		 * bound?
		 */
		if (!vsock_addr_bound(&vsk->remote_addr)) {
			err = -EINVAL;
			goto out;
		}
	} else {
		err = -EINVAL;
		goto out;
	}

	if (!transport->dgram_allow(remote_addr->svm_cid,
				    remote_addr->svm_port)) {
		err = -EINVAL;
		goto out;
	}

	err = transport->dgram_enqueue(vsk, remote_addr, msg, len);

out:
	release_sock(sk);
	return err;
}

static int vsock_dgram_connect(struct socket *sock,
			       struct sockaddr *addr, int addr_len, int flags)
{
	int err;
	struct sock *sk;
	struct vsock_sock *vsk;
	struct sockaddr_vm *remote_addr;

	sk = sock->sk;
	vsk = vsock_sk(sk);

	err = vsock_addr_cast(addr, addr_len, &remote_addr);
	if (err == -EAFNOSUPPORT && remote_addr->svm_family == AF_UNSPEC) {
		lock_sock(sk);
		vsock_addr_init(&vsk->remote_addr, VMADDR_CID_ANY,
				VMADDR_PORT_ANY);
		sock->state = SS_UNCONNECTED;
		release_sock(sk);
		return 0;
	} else if (err != 0)
		return -EINVAL;

	lock_sock(sk);

	err = vsock_auto_bind(vsk);
	if (err)
		goto out;

	if (!transport->dgram_allow(remote_addr->svm_cid,
				    remote_addr->svm_port)) {
		err = -EINVAL;
		goto out;
	}

	memcpy(&vsk->remote_addr, remote_addr, sizeof(vsk->remote_addr));
	sock->state = SS_CONNECTED;

out:
	release_sock(sk);
	return err;
}

static int vsock_dgram_recvmsg(struct socket *sock, struct msghdr *msg,
			       size_t len, int flags)
{
	return transport->dgram_dequeue(vsock_sk(sock->sk), msg, len, flags);
}

static const struct proto_ops vsock_dgram_ops = {
	.family = PF_VSOCK,
	.owner = THIS_MODULE,
	.release = vsock_release,
	.bind = vsock_bind,
	.connect = vsock_dgram_connect,
	.socketpair = sock_no_socketpair,
	.accept = sock_no_accept,
	.getname = vsock_getname,
	.poll = vsock_poll,
	.ioctl = sock_no_ioctl,
	.listen = sock_no_listen,
	.shutdown = vsock_shutdown,
	.setsockopt = sock_no_setsockopt,
	.getsockopt = sock_no_getsockopt,
	.sendmsg = vsock_dgram_sendmsg,
	.recvmsg = vsock_dgram_recvmsg,
	.mmap = sock_no_mmap,
	.sendpage = sock_no_sendpage,
};

static int vsock_transport_cancel_pkt(struct vsock_sock *vsk)
{
	if (!transport->cancel_pkt)
		return -EOPNOTSUPP;

	return transport->cancel_pkt(vsk);
}

static void vsock_connect_timeout(struct work_struct *work)
{
	struct sock *sk;
	struct vsock_sock *vsk;
	int cancel = 0;

	vsk = container_of(work, struct vsock_sock, connect_work.work);
	sk = sk_vsock(vsk);

	lock_sock(sk);
	if (sk->sk_state == TCP_SYN_SENT &&
	    (sk->sk_shutdown != SHUTDOWN_MASK)) {
		sk->sk_state = TCP_CLOSE;
		sk->sk_err = ETIMEDOUT;
		sk->sk_error_report(sk);
		cancel = 1;
	}
	release_sock(sk);
	if (cancel)
		vsock_transport_cancel_pkt(vsk);

	sock_put(sk);
}

static int vsock_stream_connect(struct socket *sock, struct sockaddr *addr,
				int addr_len, int flags)
{
	int err;
	struct sock *sk;
	struct vsock_sock *vsk;
	struct sockaddr_vm *remote_addr;
	long timeout;
	DEFINE_WAIT(wait);

	err = 0;
	sk = sock->sk;
	vsk = vsock_sk(sk);

	lock_sock(sk);

	/* XXX AF_UNSPEC should make us disconnect like AF_INET. */
	switch (sock->state) {
	case SS_CONNECTED:
		err = -EISCONN;
		goto out;
	case SS_DISCONNECTING:
		err = -EINVAL;
		goto out;
	case SS_CONNECTING:
		/* This continues on so we can move sock into the SS_CONNECTED
		 * state once the connection has completed (at which point err
		 * will be set to zero also).  Otherwise, we will either wait
		 * for the connection or return -EALREADY should this be a
		 * non-blocking call.
		 */
		err = -EALREADY;
		break;
	default:
		if ((sk->sk_state == TCP_LISTEN) ||
		    vsock_addr_cast(addr, addr_len, &remote_addr) != 0) {
			err = -EINVAL;
			goto out;
		}

		/* The hypervisor and well-known contexts do not have socket
		 * endpoints.
		 */
		if (!transport->stream_allow(remote_addr->svm_cid,
					     remote_addr->svm_port)) {
			err = -ENETUNREACH;
			goto out;
		}

		/* Set the remote address that we are connecting to. */
		memcpy(&vsk->remote_addr, remote_addr,
		       sizeof(vsk->remote_addr));

		err = vsock_auto_bind(vsk);
		if (err)
			goto out;

		sk->sk_state = TCP_SYN_SENT;

		err = transport->connect(vsk);
		if (err < 0)
			goto out;

		/* Mark sock as connecting and set the error code to in
		 * progress in case this is a non-blocking connect.
		 */
		sock->state = SS_CONNECTING;
		err = -EINPROGRESS;
	}

	/* The receive path will handle all communication until we are able to
	 * enter the connected state.  Here we wait for the connection to be
	 * completed or a notification of an error.
	 */
	timeout = vsk->connect_timeout;
	prepare_to_wait(sk_sleep(sk), &wait, TASK_INTERRUPTIBLE);

	while (sk->sk_state != TCP_ESTABLISHED && sk->sk_err == 0) {
		if (flags & O_NONBLOCK) {
			/* If we're not going to block, we schedule a timeout
			 * function to generate a timeout on the connection
			 * attempt, in case the peer doesn't respond in a
			 * timely manner. We hold on to the socket until the
			 * timeout fires.
			 */
			sock_hold(sk);
			schedule_delayed_work(&vsk->connect_work, timeout);

			/* Skip ahead to preserve error code set above. */
			goto out_wait;
		}

		release_sock(sk);
		timeout = schedule_timeout(timeout);
		lock_sock(sk);

		if (signal_pending(current)) {
			err = sock_intr_errno(timeout);
			sk->sk_state = TCP_CLOSE;
			sock->state = SS_UNCONNECTED;
			vsock_transport_cancel_pkt(vsk);
			goto out_wait;
		} else if (timeout == 0) {
			err = -ETIMEDOUT;
			sk->sk_state = TCP_CLOSE;
			sock->state = SS_UNCONNECTED;
			vsock_transport_cancel_pkt(vsk);
			goto out_wait;
		}

		prepare_to_wait(sk_sleep(sk), &wait, TASK_INTERRUPTIBLE);
	}

	if (sk->sk_err) {
		err = -sk->sk_err;
		sk->sk_state = TCP_CLOSE;
		sock->state = SS_UNCONNECTED;
	} else {
		err = 0;
	}

out_wait:
	finish_wait(sk_sleep(sk), &wait);
out:
	release_sock(sk);
	return err;
}

static int vsock_accept(struct socket *sock, struct socket *newsock, int flags,
			bool kern)
{
	struct sock *listener;
	int err;
	struct sock *connected;
	struct vsock_sock *vconnected;
	long timeout;
	DEFINE_WAIT(wait);

	err = 0;
	listener = sock->sk;

	lock_sock(listener);

	if (sock->type != SOCK_STREAM) {
		err = -EOPNOTSUPP;
		goto out;
	}

	if (listener->sk_state != TCP_LISTEN) {
		err = -EINVAL;
		goto out;
	}

	/* Wait for children sockets to appear; these are the new sockets
	 * created upon connection establishment.
	 */
	timeout = sock_rcvtimeo(listener, flags & O_NONBLOCK);
	prepare_to_wait(sk_sleep(listener), &wait, TASK_INTERRUPTIBLE);

	while ((connected = vsock_dequeue_accept(listener)) == NULL &&
	       listener->sk_err == 0) {
		release_sock(listener);
		timeout = schedule_timeout(timeout);
		finish_wait(sk_sleep(listener), &wait);
		lock_sock(listener);

		if (signal_pending(current)) {
			err = sock_intr_errno(timeout);
			goto out;
		} else if (timeout == 0) {
			err = -EAGAIN;
			goto out;
		}

		prepare_to_wait(sk_sleep(listener), &wait, TASK_INTERRUPTIBLE);
	}
	finish_wait(sk_sleep(listener), &wait);

	if (listener->sk_err)
		err = -listener->sk_err;

	if (connected) {
		listener->sk_ack_backlog--;

		lock_sock_nested(connected, SINGLE_DEPTH_NESTING);
		vconnected = vsock_sk(connected);

		/* If the listener socket has received an error, then we should
		 * reject this socket and return.  Note that we simply mark the
		 * socket rejected, drop our reference, and let the cleanup
		 * function handle the cleanup; the fact that we found it in
		 * the listener's accept queue guarantees that the cleanup
		 * function hasn't run yet.
		 */
		if (err) {
			vconnected->rejected = true;
		} else {
			newsock->state = SS_CONNECTED;
			sock_graft(connected, newsock);
		}

		release_sock(connected);
		sock_put(connected);
	}

out:
	release_sock(listener);
	return err;
}

static int vsock_listen(struct socket *sock, int backlog)
{
	int err;
	struct sock *sk;
	struct vsock_sock *vsk;

	sk = sock->sk;

	lock_sock(sk);

	if (sock->type != SOCK_STREAM) {
		err = -EOPNOTSUPP;
		goto out;
	}

	if (sock->state != SS_UNCONNECTED) {
		err = -EINVAL;
		goto out;
	}

	vsk = vsock_sk(sk);

	if (!vsock_addr_bound(&vsk->local_addr)) {
		err = -EINVAL;
		goto out;
	}

	sk->sk_max_ack_backlog = backlog;
	sk->sk_state = TCP_LISTEN;

	err = 0;

out:
	release_sock(sk);
	return err;
}

static int vsock_stream_setsockopt(struct socket *sock,
				   int level,
				   int optname,
				   char __user *optval,
				   unsigned int optlen)
{
	int err;
	struct sock *sk;
	struct vsock_sock *vsk;
	u64 val;

	if (level != AF_VSOCK)
		return -ENOPROTOOPT;

#define COPY_IN(_v)                                       \
	do {						  \
		if (optlen < sizeof(_v)) {		  \
			err = -EINVAL;			  \
			goto exit;			  \
		}					  \
		if (copy_from_user(&_v, optval, sizeof(_v)) != 0) {	\
			err = -EFAULT;					\
			goto exit;					\
		}							\
	} while (0)

	err = 0;
	sk = sock->sk;
	vsk = vsock_sk(sk);

	lock_sock(sk);

	switch (optname) {
	case SO_VM_SOCKETS_BUFFER_SIZE:
		COPY_IN(val);
		transport->set_buffer_size(vsk, val);
		break;

	case SO_VM_SOCKETS_BUFFER_MAX_SIZE:
		COPY_IN(val);
		transport->set_max_buffer_size(vsk, val);
		break;

	case SO_VM_SOCKETS_BUFFER_MIN_SIZE:
		COPY_IN(val);
		transport->set_min_buffer_size(vsk, val);
		break;

	case SO_VM_SOCKETS_CONNECT_TIMEOUT: {
		struct timeval tv;
		COPY_IN(tv);
		if (tv.tv_sec >= 0 && tv.tv_usec < USEC_PER_SEC &&
		    tv.tv_sec < (MAX_SCHEDULE_TIMEOUT / HZ - 1)) {
			vsk->connect_timeout = tv.tv_sec * HZ +
			    DIV_ROUND_UP(tv.tv_usec, (1000000 / HZ));
			if (vsk->connect_timeout == 0)
				vsk->connect_timeout =
				    VSOCK_DEFAULT_CONNECT_TIMEOUT;

		} else {
			err = -ERANGE;
		}
		break;
	}

	default:
		err = -ENOPROTOOPT;
		break;
	}

#undef COPY_IN

exit:
	release_sock(sk);
	return err;
}

static int vsock_stream_getsockopt(struct socket *sock,
				   int level, int optname,
				   char __user *optval,
				   int __user *optlen)
{
	int err;
	int len;
	struct sock *sk;
	struct vsock_sock *vsk;
	u64 val;

	if (level != AF_VSOCK)
		return -ENOPROTOOPT;

	err = get_user(len, optlen);
	if (err != 0)
		return err;

#define COPY_OUT(_v)                            \
	do {					\
		if (len < sizeof(_v))		\
			return -EINVAL;		\
						\
		len = sizeof(_v);		\
		if (copy_to_user(optval, &_v, len) != 0)	\
			return -EFAULT;				\
								\
	} while (0)

	err = 0;
	sk = sock->sk;
	vsk = vsock_sk(sk);

	switch (optname) {
	case SO_VM_SOCKETS_BUFFER_SIZE:
		val = transport->get_buffer_size(vsk);
		COPY_OUT(val);
		break;

	case SO_VM_SOCKETS_BUFFER_MAX_SIZE:
		val = transport->get_max_buffer_size(vsk);
		COPY_OUT(val);
		break;

	case SO_VM_SOCKETS_BUFFER_MIN_SIZE:
		val = transport->get_min_buffer_size(vsk);
		COPY_OUT(val);
		break;

	case SO_VM_SOCKETS_CONNECT_TIMEOUT: {
		struct timeval tv;
		tv.tv_sec = vsk->connect_timeout / HZ;
		tv.tv_usec =
		    (vsk->connect_timeout -
		     tv.tv_sec * HZ) * (1000000 / HZ);
		COPY_OUT(tv);
		break;
	}
	default:
		return -ENOPROTOOPT;
	}

	err = put_user(len, optlen);
	if (err != 0)
		return -EFAULT;

#undef COPY_OUT

	return 0;
}

static int vsock_stream_sendmsg(struct socket *sock, struct msghdr *msg,
				size_t len)
{
	struct sock *sk;
	struct vsock_sock *vsk;
	ssize_t total_written;
	long timeout;
	int err;
	struct vsock_transport_send_notify_data send_data;
	DEFINE_WAIT_FUNC(wait, woken_wake_function);

	sk = sock->sk;
	vsk = vsock_sk(sk);
	total_written = 0;
	err = 0;

	if (msg->msg_flags & MSG_OOB)
		return -EOPNOTSUPP;

	lock_sock(sk);

	/* Callers should not provide a destination with stream sockets. */
	if (msg->msg_namelen) {
		err = sk->sk_state == TCP_ESTABLISHED ? -EISCONN : -EOPNOTSUPP;
		goto out;
	}

	/* Send data only if both sides are not shutdown in the direction. */
	if (sk->sk_shutdown & SEND_SHUTDOWN ||
	    vsk->peer_shutdown & RCV_SHUTDOWN) {
		err = -EPIPE;
		goto out;
	}

	if (sk->sk_state != TCP_ESTABLISHED ||
	    !vsock_addr_bound(&vsk->local_addr)) {
		err = -ENOTCONN;
		goto out;
	}

	if (!vsock_addr_bound(&vsk->remote_addr)) {
		err = -EDESTADDRREQ;
		goto out;
	}

	/* Wait for room in the produce queue to enqueue our user's data. */
	timeout = sock_sndtimeo(sk, msg->msg_flags & MSG_DONTWAIT);

	err = transport->notify_send_init(vsk, &send_data);
	if (err < 0)
		goto out;

	while (total_written < len) {
		ssize_t written;

		add_wait_queue(sk_sleep(sk), &wait);
		while (vsock_stream_has_space(vsk) == 0 &&
		       sk->sk_err == 0 &&
		       !(sk->sk_shutdown & SEND_SHUTDOWN) &&
		       !(vsk->peer_shutdown & RCV_SHUTDOWN)) {

			/* Don't wait for non-blocking sockets. */
			if (timeout == 0) {
				err = -EAGAIN;
				remove_wait_queue(sk_sleep(sk), &wait);
				goto out_err;
			}

			err = transport->notify_send_pre_block(vsk, &send_data);
			if (err < 0) {
				remove_wait_queue(sk_sleep(sk), &wait);
				goto out_err;
			}

			release_sock(sk);
			timeout = wait_woken(&wait, TASK_INTERRUPTIBLE, timeout);
			lock_sock(sk);
			if (signal_pending(current)) {
				err = sock_intr_errno(timeout);
				remove_wait_queue(sk_sleep(sk), &wait);
				goto out_err;
			} else if (timeout == 0) {
				err = -EAGAIN;
				remove_wait_queue(sk_sleep(sk), &wait);
				goto out_err;
			}
		}
		remove_wait_queue(sk_sleep(sk), &wait);

		/* These checks occur both as part of and after the loop
		 * conditional since we need to check before and after
		 * sleeping.
		 */
		if (sk->sk_err) {
			err = -sk->sk_err;
			goto out_err;
		} else if ((sk->sk_shutdown & SEND_SHUTDOWN) ||
			   (vsk->peer_shutdown & RCV_SHUTDOWN)) {
			err = -EPIPE;
			goto out_err;
		}

		err = transport->notify_send_pre_enqueue(vsk, &send_data);
		if (err < 0)
			goto out_err;

		/* Note that enqueue will only write as many bytes as are free
		 * in the produce queue, so we don't need to ensure len is
		 * smaller than the queue size.  It is the caller's
		 * responsibility to check how many bytes we were able to send.
		 */

		written = transport->stream_enqueue(
				vsk, msg,
				len - total_written);
		if (written < 0) {
			err = -ENOMEM;
			goto out_err;
		}

		total_written += written;

		err = transport->notify_send_post_enqueue(
				vsk, written, &send_data);
		if (err < 0)
			goto out_err;

	}

out_err:
	if (total_written > 0)
		err = total_written;
out:
	release_sock(sk);
	return err;
}


static int
vsock_stream_recvmsg(struct socket *sock, struct msghdr *msg, size_t len,
		     int flags)
{
	struct sock *sk;
	struct vsock_sock *vsk;
	int err;
	size_t target;
	ssize_t copied;
	long timeout;
	struct vsock_transport_recv_notify_data recv_data;

	DEFINE_WAIT(wait);

	sk = sock->sk;
	vsk = vsock_sk(sk);
	err = 0;

	lock_sock(sk);

	if (sk->sk_state != TCP_ESTABLISHED) {
		/* Recvmsg is supposed to return 0 if a peer performs an
		 * orderly shutdown. Differentiate between that case and when a
		 * peer has not connected or a local shutdown occured with the
		 * SOCK_DONE flag.
		 */
		if (sock_flag(sk, SOCK_DONE))
			err = 0;
		else
			err = -ENOTCONN;

		goto out;
	}

	if (flags & MSG_OOB) {
		err = -EOPNOTSUPP;
		goto out;
	}

	/* We don't check peer_shutdown flag here since peer may actually shut
	 * down, but there can be data in the queue that a local socket can
	 * receive.
	 */
	if (sk->sk_shutdown & RCV_SHUTDOWN) {
		err = 0;
		goto out;
	}

	/* It is valid on Linux to pass in a zero-length receive buffer.  This
	 * is not an error.  We may as well bail out now.
	 */
	if (!len) {
		err = 0;
		goto out;
	}

	/* We must not copy less than target bytes into the user's buffer
	 * before returning successfully, so we wait for the consume queue to
	 * have that much data to consume before dequeueing.  Note that this
	 * makes it impossible to handle cases where target is greater than the
	 * queue size.
	 */
	target = sock_rcvlowat(sk, flags & MSG_WAITALL, len);
	if (target >= transport->stream_rcvhiwat(vsk)) {
		err = -ENOMEM;
		goto out;
	}
	timeout = sock_rcvtimeo(sk, flags & MSG_DONTWAIT);
	copied = 0;

	err = transport->notify_recv_init(vsk, target, &recv_data);
	if (err < 0)
		goto out;


	while (1) {
		s64 ready;

		prepare_to_wait(sk_sleep(sk), &wait, TASK_INTERRUPTIBLE);
		ready = vsock_stream_has_data(vsk);

		if (ready == 0) {
			if (sk->sk_err != 0 ||
			    (sk->sk_shutdown & RCV_SHUTDOWN) ||
			    (vsk->peer_shutdown & SEND_SHUTDOWN)) {
				finish_wait(sk_sleep(sk), &wait);
				break;
			}
			/* Don't wait for non-blocking sockets. */
			if (timeout == 0) {
				err = -EAGAIN;
				finish_wait(sk_sleep(sk), &wait);
				break;
			}

			err = transport->notify_recv_pre_block(
					vsk, target, &recv_data);
			if (err < 0) {
				finish_wait(sk_sleep(sk), &wait);
				break;
			}
			release_sock(sk);
			timeout = schedule_timeout(timeout);
			lock_sock(sk);

			if (signal_pending(current)) {
				err = sock_intr_errno(timeout);
				finish_wait(sk_sleep(sk), &wait);
				break;
			} else if (timeout == 0) {
				err = -EAGAIN;
				finish_wait(sk_sleep(sk), &wait);
				break;
			}
		} else {
			ssize_t read;

			finish_wait(sk_sleep(sk), &wait);

			if (ready < 0) {
				/* Invalid queue pair content. XXX This should
				* be changed to a connection reset in a later
				* change.
				*/

				err = -ENOMEM;
				goto out;
			}

			err = transport->notify_recv_pre_dequeue(
					vsk, target, &recv_data);
			if (err < 0)
				break;

			read = transport->stream_dequeue(
					vsk, msg,
					len - copied, flags);
			if (read < 0) {
				err = -ENOMEM;
				break;
			}

			copied += read;

			err = transport->notify_recv_post_dequeue(
					vsk, target, read,
					!(flags & MSG_PEEK), &recv_data);
			if (err < 0)
				goto out;

			if (read >= target || flags & MSG_PEEK)
				break;

			target -= read;
		}
	}

	if (sk->sk_err)
		err = -sk->sk_err;
	else if (sk->sk_shutdown & RCV_SHUTDOWN)
		err = 0;

	if (copied > 0)
		err = copied;

out:
	release_sock(sk);
	return err;
}

static const struct proto_ops vsock_stream_ops = {
	.family = PF_VSOCK,
	.owner = THIS_MODULE,
	.release = vsock_release,
	.bind = vsock_bind,
	.connect = vsock_stream_connect,
	.socketpair = sock_no_socketpair,
	.accept = vsock_accept,
	.getname = vsock_getname,
	.poll = vsock_poll,
	.ioctl = sock_no_ioctl,
	.listen = vsock_listen,
	.shutdown = vsock_shutdown,
	.setsockopt = vsock_stream_setsockopt,
	.getsockopt = vsock_stream_getsockopt,
	.sendmsg = vsock_stream_sendmsg,
	.recvmsg = vsock_stream_recvmsg,
	.mmap = sock_no_mmap,
	.sendpage = sock_no_sendpage,
};

static int vsock_create(struct net *net, struct socket *sock,
			int protocol, int kern)
{
	if (!sock)
		return -EINVAL;

	if (protocol && protocol != PF_VSOCK)
		return -EPROTONOSUPPORT;

	switch (sock->type) {
	case SOCK_DGRAM:
		sock->ops = &vsock_dgram_ops;
		break;
	case SOCK_STREAM:
		sock->ops = &vsock_stream_ops;
		break;
	default:
		return -ESOCKTNOSUPPORT;
	}

	sock->state = SS_UNCONNECTED;

	return __vsock_create(net, sock, NULL, GFP_KERNEL, 0, kern) ? 0 : -ENOMEM;
}

static const struct net_proto_family vsock_family_ops = {
	.family = AF_VSOCK,
	.create = vsock_create,
	.owner = THIS_MODULE,
};

static long vsock_dev_do_ioctl(struct file *filp,
			       unsigned int cmd, void __user *ptr)
{
	u32 __user *p = ptr;
	int retval = 0;

	switch (cmd) {
	case IOCTL_VM_SOCKETS_GET_LOCAL_CID:
		if (put_user(transport->get_local_cid(), p) != 0)
			retval = -EFAULT;
		break;

	default:
		pr_err("Unknown ioctl %d\n", cmd);
		retval = -EINVAL;
	}

	return retval;
}

static long vsock_dev_ioctl(struct file *filp,
			    unsigned int cmd, unsigned long arg)
{
	return vsock_dev_do_ioctl(filp, cmd, (void __user *)arg);
}

#ifdef CONFIG_COMPAT
static long vsock_dev_compat_ioctl(struct file *filp,
				   unsigned int cmd, unsigned long arg)
{
	return vsock_dev_do_ioctl(filp, cmd, compat_ptr(arg));
}
#endif

static const struct file_operations vsock_device_ops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= vsock_dev_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= vsock_dev_compat_ioctl,
#endif
	.open		= nonseekable_open,
};

static struct miscdevice vsock_device = {
	.name		= "vsock",
	.fops		= &vsock_device_ops,
};

int __vsock_core_init(const struct vsock_transport *t, struct module *owner)
{
	int err = mutex_lock_interruptible(&vsock_register_mutex);

	if (err)
		return err;

	if (transport) {
		err = -EBUSY;
		goto err_busy;
	}

	/* Transport must be the owner of the protocol so that it can't
	 * unload while there are open sockets.
	 */
	vsock_proto.owner = owner;
	transport = t;

	vsock_device.minor = MISC_DYNAMIC_MINOR;
	err = misc_register(&vsock_device);
	if (err) {
		pr_err("Failed to register misc device\n");
		goto err_reset_transport;
	}

	err = proto_register(&vsock_proto, 1);	/* we want our slab */
	if (err) {
		pr_err("Cannot register vsock protocol\n");
		goto err_deregister_misc;
	}

	err = sock_register(&vsock_family_ops);
	if (err) {
		pr_err("could not register af_vsock (%d) address family: %d\n",
		       AF_VSOCK, err);
		goto err_unregister_proto;
	}

	mutex_unlock(&vsock_register_mutex);
	return 0;

err_unregister_proto:
	proto_unregister(&vsock_proto);
err_deregister_misc:
	misc_deregister(&vsock_device);
err_reset_transport:
	transport = NULL;
err_busy:
	mutex_unlock(&vsock_register_mutex);
	return err;
}
EXPORT_SYMBOL_GPL(__vsock_core_init);

void vsock_core_exit(void)
{
	mutex_lock(&vsock_register_mutex);

	misc_deregister(&vsock_device);
	sock_unregister(AF_VSOCK);
	proto_unregister(&vsock_proto);

	/* We do not want the assignment below re-ordered. */
	mb();
	transport = NULL;

	mutex_unlock(&vsock_register_mutex);
}
EXPORT_SYMBOL_GPL(vsock_core_exit);

const struct vsock_transport *vsock_core_get_transport(void)
{
	/* vsock_register_mutex not taken since only the transport uses this
	 * function and only while registered.
	 */
	return transport;
}
EXPORT_SYMBOL_GPL(vsock_core_get_transport);

static void __exit vsock_exit(void)
{
	/* Do nothing.  This function makes this module removable. */
}

module_init(vsock_init_tables);
module_exit(vsock_exit);

MODULE_AUTHOR("VMware, Inc.");
MODULE_DESCRIPTION("VMware Virtual Socket Family");
MODULE_VERSION("1.0.2.0-k");
MODULE_LICENSE("GPL v2");
