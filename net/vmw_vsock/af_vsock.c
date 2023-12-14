// SPDX-License-Identifier: GPL-2.0-only
/*
 * VMware vSockets Driver
 *
 * Copyright (C) 2007-2013 VMware, Inc. All rights reserved.
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

#include <linux/compat.h>
#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/cred.h>
#include <linux/errqueue.h>
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
#include <uapi/linux/vm_sockets.h>

static int __vsock_bind(struct sock *sk, struct sockaddr_vm *addr);
static void vsock_sk_destruct(struct sock *sk);
static int vsock_queue_rcv_skb(struct sock *sk, struct sk_buff *skb);

/* Protocol family. */
struct proto vsock_proto = {
	.name = "AF_VSOCK",
	.owner = THIS_MODULE,
	.obj_size = sizeof(struct vsock_sock),
#ifdef CONFIG_BPF_SYSCALL
	.psock_update_sk_prot = vsock_bpf_update_proto,
#endif
};

/* The default peer timeout indicates how long we will wait for a peer response
 * to a control message.
 */
#define VSOCK_DEFAULT_CONNECT_TIMEOUT (2 * HZ)

#define VSOCK_DEFAULT_BUFFER_SIZE     (1024 * 256)
#define VSOCK_DEFAULT_BUFFER_MAX_SIZE (1024 * 256)
#define VSOCK_DEFAULT_BUFFER_MIN_SIZE 128

/* Transport used for host->guest communication */
static const struct vsock_transport *transport_h2g;
/* Transport used for guest->host communication */
static const struct vsock_transport *transport_g2h;
/* Transport used for DGRAM communication */
static const struct vsock_transport *transport_dgram;
/* Transport used for local communication */
static const struct vsock_transport *transport_local;
static DEFINE_MUTEX(vsock_register_mutex);

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

static void vsock_init_tables(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(vsock_bind_table); i++)
		INIT_LIST_HEAD(&vsock_bind_table[i]);

	for (i = 0; i < ARRAY_SIZE(vsock_connected_table); i++)
		INIT_LIST_HEAD(&vsock_connected_table[i]);
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

	list_for_each_entry(vsk, vsock_bound_sockets(addr), bound_table) {
		if (vsock_addr_equals_addr(addr, &vsk->local_addr))
			return sk_vsock(vsk);

		if (addr->svm_port == vsk->local_addr.svm_port &&
		    (vsk->local_addr.svm_cid == VMADDR_CID_ANY ||
		     addr->svm_cid == VMADDR_CID_ANY))
			return sk_vsock(vsk);
	}

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

void vsock_for_each_connected_socket(struct vsock_transport *transport,
				     void (*fn)(struct sock *sk))
{
	int i;

	spin_lock_bh(&vsock_table_lock);

	for (i = 0; i < ARRAY_SIZE(vsock_connected_table); i++) {
		struct vsock_sock *vsk;
		list_for_each_entry(vsk, &vsock_connected_table[i],
				    connected_table) {
			if (vsk->transport != transport)
				continue;

			fn(sk_vsock(vsk));
		}
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

static bool vsock_use_local_transport(unsigned int remote_cid)
{
	if (!transport_local)
		return false;

	if (remote_cid == VMADDR_CID_LOCAL)
		return true;

	if (transport_g2h) {
		return remote_cid == transport_g2h->get_local_cid();
	} else {
		return remote_cid == VMADDR_CID_HOST;
	}
}

static void vsock_deassign_transport(struct vsock_sock *vsk)
{
	if (!vsk->transport)
		return;

	vsk->transport->destruct(vsk);
	module_put(vsk->transport->module);
	vsk->transport = NULL;
}

/* Assign a transport to a socket and call the .init transport callback.
 *
 * Note: for connection oriented socket this must be called when vsk->remote_addr
 * is set (e.g. during the connect() or when a connection request on a listener
 * socket is received).
 * The vsk->remote_addr is used to decide which transport to use:
 *  - remote CID == VMADDR_CID_LOCAL or g2h->local_cid or VMADDR_CID_HOST if
 *    g2h is not loaded, will use local transport;
 *  - remote CID <= VMADDR_CID_HOST or h2g is not loaded or remote flags field
 *    includes VMADDR_FLAG_TO_HOST flag value, will use guest->host transport;
 *  - remote CID > VMADDR_CID_HOST will use host->guest transport;
 */
int vsock_assign_transport(struct vsock_sock *vsk, struct vsock_sock *psk)
{
	const struct vsock_transport *new_transport;
	struct sock *sk = sk_vsock(vsk);
	unsigned int remote_cid = vsk->remote_addr.svm_cid;
	__u8 remote_flags;
	int ret;

	/* If the packet is coming with the source and destination CIDs higher
	 * than VMADDR_CID_HOST, then a vsock channel where all the packets are
	 * forwarded to the host should be established. Then the host will
	 * need to forward the packets to the guest.
	 *
	 * The flag is set on the (listen) receive path (psk is not NULL). On
	 * the connect path the flag can be set by the user space application.
	 */
	if (psk && vsk->local_addr.svm_cid > VMADDR_CID_HOST &&
	    vsk->remote_addr.svm_cid > VMADDR_CID_HOST)
		vsk->remote_addr.svm_flags |= VMADDR_FLAG_TO_HOST;

	remote_flags = vsk->remote_addr.svm_flags;

	switch (sk->sk_type) {
	case SOCK_DGRAM:
		new_transport = transport_dgram;
		break;
	case SOCK_STREAM:
	case SOCK_SEQPACKET:
		if (vsock_use_local_transport(remote_cid))
			new_transport = transport_local;
		else if (remote_cid <= VMADDR_CID_HOST || !transport_h2g ||
			 (remote_flags & VMADDR_FLAG_TO_HOST))
			new_transport = transport_g2h;
		else
			new_transport = transport_h2g;
		break;
	default:
		return -ESOCKTNOSUPPORT;
	}

	if (vsk->transport) {
		if (vsk->transport == new_transport)
			return 0;

		/* transport->release() must be called with sock lock acquired.
		 * This path can only be taken during vsock_connect(), where we
		 * have already held the sock lock. In the other cases, this
		 * function is called on a new socket which is not assigned to
		 * any transport.
		 */
		vsk->transport->release(vsk);
		vsock_deassign_transport(vsk);
	}

	/* We increase the module refcnt to prevent the transport unloading
	 * while there are open sockets assigned to it.
	 */
	if (!new_transport || !try_module_get(new_transport->module))
		return -ENODEV;

	if (sk->sk_type == SOCK_SEQPACKET) {
		if (!new_transport->seqpacket_allow ||
		    !new_transport->seqpacket_allow(remote_cid)) {
			module_put(new_transport->module);
			return -ESOCKTNOSUPPORT;
		}
	}

	ret = new_transport->init(vsk, psk);
	if (ret) {
		module_put(new_transport->module);
		return ret;
	}

	vsk->transport = new_transport;

	return 0;
}
EXPORT_SYMBOL_GPL(vsock_assign_transport);

bool vsock_find_cid(unsigned int cid)
{
	if (transport_g2h && cid == transport_g2h->get_local_cid())
		return true;

	if (transport_h2g && cid == VMADDR_CID_HOST)
		return true;

	if (transport_local && cid == VMADDR_CID_LOCAL)
		return true;

	return false;
}
EXPORT_SYMBOL_GPL(vsock_find_cid);

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
	struct vsock_sock *vsk = vsock_sk(sk);

	if (!vsk->transport)
		return -ENODEV;

	return vsk->transport->shutdown(vsk, mode);
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

		sk_acceptq_removed(listener);
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

static int __vsock_bind_connectible(struct vsock_sock *vsk,
				    struct sockaddr_vm *addr)
{
	static u32 port;
	struct sockaddr_vm new_addr;

	if (!port)
		port = get_random_u32_above(LAST_RESERVED_PORT);

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

	/* Remove connection oriented sockets from the unbound list and add them
	 * to the hash table for easy lookup by its address.  The unbound list
	 * is simply an extra entry at the end of the hash table, a trick used
	 * by AF_UNIX.
	 */
	__vsock_remove_bound(vsk);
	__vsock_insert_bound(vsock_bound_sockets(&vsk->local_addr), vsk);

	return 0;
}

static int __vsock_bind_dgram(struct vsock_sock *vsk,
			      struct sockaddr_vm *addr)
{
	return vsk->transport->dgram_bind(vsk, addr);
}

static int __vsock_bind(struct sock *sk, struct sockaddr_vm *addr)
{
	struct vsock_sock *vsk = vsock_sk(sk);
	int retval;

	/* First ensure this socket isn't already bound. */
	if (vsock_addr_bound(&vsk->local_addr))
		return -EINVAL;

	/* Now bind to the provided address or select appropriate values if
	 * none are provided (VMADDR_CID_ANY and VMADDR_PORT_ANY).  Note that
	 * like AF_INET prevents binding to a non-local IP address (in most
	 * cases), we only allow binding to a local CID.
	 */
	if (addr->svm_cid != VMADDR_CID_ANY && !vsock_find_cid(addr->svm_cid))
		return -EADDRNOTAVAIL;

	switch (sk->sk_socket->type) {
	case SOCK_STREAM:
	case SOCK_SEQPACKET:
		spin_lock_bh(&vsock_table_lock);
		retval = __vsock_bind_connectible(vsk, addr);
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

static struct sock *__vsock_create(struct net *net,
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
		vsk->buffer_size = psk->buffer_size;
		vsk->buffer_min_size = psk->buffer_min_size;
		vsk->buffer_max_size = psk->buffer_max_size;
		security_sk_clone(parent, sk);
	} else {
		vsk->trusted = ns_capable_noaudit(&init_user_ns, CAP_NET_ADMIN);
		vsk->owner = get_current_cred();
		vsk->connect_timeout = VSOCK_DEFAULT_CONNECT_TIMEOUT;
		vsk->buffer_size = VSOCK_DEFAULT_BUFFER_SIZE;
		vsk->buffer_min_size = VSOCK_DEFAULT_BUFFER_MIN_SIZE;
		vsk->buffer_max_size = VSOCK_DEFAULT_BUFFER_MAX_SIZE;
	}

	return sk;
}

static bool sock_type_connectible(u16 type)
{
	return (type == SOCK_STREAM) || (type == SOCK_SEQPACKET);
}

static void __vsock_release(struct sock *sk, int level)
{
	if (sk) {
		struct sock *pending;
		struct vsock_sock *vsk;

		vsk = vsock_sk(sk);
		pending = NULL;	/* Compiler warning. */

		/* When "level" is SINGLE_DEPTH_NESTING, use the nested
		 * version to avoid the warning "possible recursive locking
		 * detected". When "level" is 0, lock_sock_nested(sk, level)
		 * is the same as lock_sock(sk).
		 */
		lock_sock_nested(sk, level);

		if (vsk->transport)
			vsk->transport->release(vsk);
		else if (sock_type_connectible(sk->sk_type))
			vsock_remove_sock(vsk);

		sock_orphan(sk);
		sk->sk_shutdown = SHUTDOWN_MASK;

		skb_queue_purge(&sk->sk_receive_queue);

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

	vsock_deassign_transport(vsk);

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

struct sock *vsock_create_connected(struct sock *parent)
{
	return __vsock_create(sock_net(parent), NULL, parent, GFP_KERNEL,
			      parent->sk_type, 0);
}
EXPORT_SYMBOL_GPL(vsock_create_connected);

s64 vsock_stream_has_data(struct vsock_sock *vsk)
{
	return vsk->transport->stream_has_data(vsk);
}
EXPORT_SYMBOL_GPL(vsock_stream_has_data);

s64 vsock_connectible_has_data(struct vsock_sock *vsk)
{
	struct sock *sk = sk_vsock(vsk);

	if (sk->sk_type == SOCK_SEQPACKET)
		return vsk->transport->seqpacket_has_data(vsk);
	else
		return vsock_stream_has_data(vsk);
}
EXPORT_SYMBOL_GPL(vsock_connectible_has_data);

s64 vsock_stream_has_space(struct vsock_sock *vsk)
{
	return vsk->transport->stream_has_space(vsk);
}
EXPORT_SYMBOL_GPL(vsock_stream_has_space);

void vsock_data_ready(struct sock *sk)
{
	struct vsock_sock *vsk = vsock_sk(sk);

	if (vsock_stream_has_data(vsk) >= sk->sk_rcvlowat ||
	    sock_flag(sk, SOCK_DONE))
		sk->sk_data_ready(sk);
}
EXPORT_SYMBOL_GPL(vsock_data_ready);

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

	/* If this is a connection oriented socket and it is not connected then
	 * bail out immediately.  If it is a DGRAM socket then we must first
	 * kick the socket so that it wakes up from any sleeping calls, for
	 * example recv(), and then afterwards return the error.
	 */

	sk = sock->sk;

	lock_sock(sk);
	if (sock->state == SS_UNCONNECTED) {
		err = -ENOTCONN;
		if (sock_type_connectible(sk->sk_type))
			goto out;
	} else {
		sock->state = SS_DISCONNECTING;
		err = 0;
	}

	/* Receive and send shutdowns are treated alike. */
	mode = mode & (RCV_SHUTDOWN | SEND_SHUTDOWN);
	if (mode) {
		sk->sk_shutdown |= mode;
		sk->sk_state_change(sk);

		if (sock_type_connectible(sk->sk_type)) {
			sock_reset_flag(sk, SOCK_DONE);
			vsock_send_shutdown(sk, mode);
		}
	}

out:
	release_sock(sk);
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

	} else if (sock_type_connectible(sk->sk_type)) {
		const struct vsock_transport *transport;

		lock_sock(sk);

		transport = vsk->transport;

		/* Listening sockets that have connections in their accept
		 * queue can be read.
		 */
		if (sk->sk_state == TCP_LISTEN
		    && !vsock_is_accept_queue_empty(sk))
			mask |= EPOLLIN | EPOLLRDNORM;

		/* If there is something in the queue then we can read. */
		if (transport && transport->stream_is_active(vsk) &&
		    !(sk->sk_shutdown & RCV_SHUTDOWN)) {
			bool data_ready_now = false;
			int target = sock_rcvlowat(sk, 0, INT_MAX);
			int ret = transport->notify_poll_in(
					vsk, target, &data_ready_now);
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
		if (transport && sk->sk_state == TCP_ESTABLISHED) {
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

static int vsock_read_skb(struct sock *sk, skb_read_actor_t read_actor)
{
	struct vsock_sock *vsk = vsock_sk(sk);

	return vsk->transport->read_skb(vsk, read_actor);
}

static int vsock_dgram_sendmsg(struct socket *sock, struct msghdr *msg,
			       size_t len)
{
	int err;
	struct sock *sk;
	struct vsock_sock *vsk;
	struct sockaddr_vm *remote_addr;
	const struct vsock_transport *transport;

	if (msg->msg_flags & MSG_OOB)
		return -EOPNOTSUPP;

	/* For now, MSG_DONTWAIT is always assumed... */
	err = 0;
	sk = sock->sk;
	vsk = vsock_sk(sk);

	lock_sock(sk);

	transport = vsk->transport;

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

	if (!vsk->transport->dgram_allow(remote_addr->svm_cid,
					 remote_addr->svm_port)) {
		err = -EINVAL;
		goto out;
	}

	memcpy(&vsk->remote_addr, remote_addr, sizeof(vsk->remote_addr));
	sock->state = SS_CONNECTED;

	/* sock map disallows redirection of non-TCP sockets with sk_state !=
	 * TCP_ESTABLISHED (see sock_map_redirect_allowed()), so we set
	 * TCP_ESTABLISHED here to allow redirection of connected vsock dgrams.
	 *
	 * This doesn't seem to be abnormal state for datagram sockets, as the
	 * same approach can be see in other datagram socket types as well
	 * (such as unix sockets).
	 */
	sk->sk_state = TCP_ESTABLISHED;

out:
	release_sock(sk);
	return err;
}

int vsock_dgram_recvmsg(struct socket *sock, struct msghdr *msg,
			size_t len, int flags)
{
#ifdef CONFIG_BPF_SYSCALL
	const struct proto *prot;
#endif
	struct vsock_sock *vsk;
	struct sock *sk;

	sk = sock->sk;
	vsk = vsock_sk(sk);

#ifdef CONFIG_BPF_SYSCALL
	prot = READ_ONCE(sk->sk_prot);
	if (prot != &vsock_proto)
		return prot->recvmsg(sk, msg, len, flags, NULL);
#endif

	return vsk->transport->dgram_dequeue(vsk, msg, len, flags);
}
EXPORT_SYMBOL_GPL(vsock_dgram_recvmsg);

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
	.sendmsg = vsock_dgram_sendmsg,
	.recvmsg = vsock_dgram_recvmsg,
	.mmap = sock_no_mmap,
	.read_skb = vsock_read_skb,
};

static int vsock_transport_cancel_pkt(struct vsock_sock *vsk)
{
	const struct vsock_transport *transport = vsk->transport;

	if (!transport || !transport->cancel_pkt)
		return -EOPNOTSUPP;

	return transport->cancel_pkt(vsk);
}

static void vsock_connect_timeout(struct work_struct *work)
{
	struct sock *sk;
	struct vsock_sock *vsk;

	vsk = container_of(work, struct vsock_sock, connect_work.work);
	sk = sk_vsock(vsk);

	lock_sock(sk);
	if (sk->sk_state == TCP_SYN_SENT &&
	    (sk->sk_shutdown != SHUTDOWN_MASK)) {
		sk->sk_state = TCP_CLOSE;
		sk->sk_socket->state = SS_UNCONNECTED;
		sk->sk_err = ETIMEDOUT;
		sk_error_report(sk);
		vsock_transport_cancel_pkt(vsk);
	}
	release_sock(sk);

	sock_put(sk);
}

static int vsock_connect(struct socket *sock, struct sockaddr *addr,
			 int addr_len, int flags)
{
	int err;
	struct sock *sk;
	struct vsock_sock *vsk;
	const struct vsock_transport *transport;
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
		if (flags & O_NONBLOCK)
			goto out;
		break;
	default:
		if ((sk->sk_state == TCP_LISTEN) ||
		    vsock_addr_cast(addr, addr_len, &remote_addr) != 0) {
			err = -EINVAL;
			goto out;
		}

		/* Set the remote address that we are connecting to. */
		memcpy(&vsk->remote_addr, remote_addr,
		       sizeof(vsk->remote_addr));

		err = vsock_assign_transport(vsk, NULL);
		if (err)
			goto out;

		transport = vsk->transport;

		/* The hypervisor and well-known contexts do not have socket
		 * endpoints.
		 */
		if (!transport ||
		    !transport->stream_allow(remote_addr->svm_cid,
					     remote_addr->svm_port)) {
			err = -ENETUNREACH;
			goto out;
		}

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

			/* If the timeout function is already scheduled,
			 * reschedule it, then ungrab the socket refcount to
			 * keep it balanced.
			 */
			if (mod_delayed_work(system_wq, &vsk->connect_work,
					     timeout))
				sock_put(sk);

			/* Skip ahead to preserve error code set above. */
			goto out_wait;
		}

		release_sock(sk);
		timeout = schedule_timeout(timeout);
		lock_sock(sk);

		if (signal_pending(current)) {
			err = sock_intr_errno(timeout);
			sk->sk_state = sk->sk_state == TCP_ESTABLISHED ? TCP_CLOSING : TCP_CLOSE;
			sock->state = SS_UNCONNECTED;
			vsock_transport_cancel_pkt(vsk);
			vsock_remove_connected(vsk);
			goto out_wait;
		} else if ((sk->sk_state != TCP_ESTABLISHED) && (timeout == 0)) {
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

	if (!sock_type_connectible(sock->type)) {
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
		sk_acceptq_removed(listener);

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

	if (!sock_type_connectible(sk->sk_type)) {
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

static void vsock_update_buffer_size(struct vsock_sock *vsk,
				     const struct vsock_transport *transport,
				     u64 val)
{
	if (val > vsk->buffer_max_size)
		val = vsk->buffer_max_size;

	if (val < vsk->buffer_min_size)
		val = vsk->buffer_min_size;

	if (val != vsk->buffer_size &&
	    transport && transport->notify_buffer_size)
		transport->notify_buffer_size(vsk, &val);

	vsk->buffer_size = val;
}

static int vsock_connectible_setsockopt(struct socket *sock,
					int level,
					int optname,
					sockptr_t optval,
					unsigned int optlen)
{
	int err;
	struct sock *sk;
	struct vsock_sock *vsk;
	const struct vsock_transport *transport;
	u64 val;

	if (level != AF_VSOCK)
		return -ENOPROTOOPT;

#define COPY_IN(_v)                                       \
	do {						  \
		if (optlen < sizeof(_v)) {		  \
			err = -EINVAL;			  \
			goto exit;			  \
		}					  \
		if (copy_from_sockptr(&_v, optval, sizeof(_v)) != 0) {	\
			err = -EFAULT;					\
			goto exit;					\
		}							\
	} while (0)

	err = 0;
	sk = sock->sk;
	vsk = vsock_sk(sk);

	lock_sock(sk);

	transport = vsk->transport;

	switch (optname) {
	case SO_VM_SOCKETS_BUFFER_SIZE:
		COPY_IN(val);
		vsock_update_buffer_size(vsk, transport, val);
		break;

	case SO_VM_SOCKETS_BUFFER_MAX_SIZE:
		COPY_IN(val);
		vsk->buffer_max_size = val;
		vsock_update_buffer_size(vsk, transport, vsk->buffer_size);
		break;

	case SO_VM_SOCKETS_BUFFER_MIN_SIZE:
		COPY_IN(val);
		vsk->buffer_min_size = val;
		vsock_update_buffer_size(vsk, transport, vsk->buffer_size);
		break;

	case SO_VM_SOCKETS_CONNECT_TIMEOUT_NEW:
	case SO_VM_SOCKETS_CONNECT_TIMEOUT_OLD: {
		struct __kernel_sock_timeval tv;

		err = sock_copy_user_timeval(&tv, optval, optlen,
					     optname == SO_VM_SOCKETS_CONNECT_TIMEOUT_OLD);
		if (err)
			break;
		if (tv.tv_sec >= 0 && tv.tv_usec < USEC_PER_SEC &&
		    tv.tv_sec < (MAX_SCHEDULE_TIMEOUT / HZ - 1)) {
			vsk->connect_timeout = tv.tv_sec * HZ +
				DIV_ROUND_UP((unsigned long)tv.tv_usec, (USEC_PER_SEC / HZ));
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

static int vsock_connectible_getsockopt(struct socket *sock,
					int level, int optname,
					char __user *optval,
					int __user *optlen)
{
	struct sock *sk = sock->sk;
	struct vsock_sock *vsk = vsock_sk(sk);

	union {
		u64 val64;
		struct old_timeval32 tm32;
		struct __kernel_old_timeval tm;
		struct  __kernel_sock_timeval stm;
	} v;

	int lv = sizeof(v.val64);
	int len;

	if (level != AF_VSOCK)
		return -ENOPROTOOPT;

	if (get_user(len, optlen))
		return -EFAULT;

	memset(&v, 0, sizeof(v));

	switch (optname) {
	case SO_VM_SOCKETS_BUFFER_SIZE:
		v.val64 = vsk->buffer_size;
		break;

	case SO_VM_SOCKETS_BUFFER_MAX_SIZE:
		v.val64 = vsk->buffer_max_size;
		break;

	case SO_VM_SOCKETS_BUFFER_MIN_SIZE:
		v.val64 = vsk->buffer_min_size;
		break;

	case SO_VM_SOCKETS_CONNECT_TIMEOUT_NEW:
	case SO_VM_SOCKETS_CONNECT_TIMEOUT_OLD:
		lv = sock_get_timeout(vsk->connect_timeout, &v,
				      optname == SO_VM_SOCKETS_CONNECT_TIMEOUT_OLD);
		break;

	default:
		return -ENOPROTOOPT;
	}

	if (len < lv)
		return -EINVAL;
	if (len > lv)
		len = lv;
	if (copy_to_user(optval, &v, len))
		return -EFAULT;

	if (put_user(len, optlen))
		return -EFAULT;

	return 0;
}

static int vsock_connectible_sendmsg(struct socket *sock, struct msghdr *msg,
				     size_t len)
{
	struct sock *sk;
	struct vsock_sock *vsk;
	const struct vsock_transport *transport;
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

	transport = vsk->transport;

	/* Callers should not provide a destination with connection oriented
	 * sockets.
	 */
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

	if (!transport || sk->sk_state != TCP_ESTABLISHED ||
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

		if (sk->sk_type == SOCK_SEQPACKET) {
			written = transport->seqpacket_enqueue(vsk,
						msg, len - total_written);
		} else {
			written = transport->stream_enqueue(vsk,
					msg, len - total_written);
		}

		if (written < 0) {
			err = written;
			goto out_err;
		}

		total_written += written;

		err = transport->notify_send_post_enqueue(
				vsk, written, &send_data);
		if (err < 0)
			goto out_err;

	}

out_err:
	if (total_written > 0) {
		/* Return number of written bytes only if:
		 * 1) SOCK_STREAM socket.
		 * 2) SOCK_SEQPACKET socket when whole buffer is sent.
		 */
		if (sk->sk_type == SOCK_STREAM || total_written == len)
			err = total_written;
	}
out:
	release_sock(sk);
	return err;
}

static int vsock_connectible_wait_data(struct sock *sk,
				       struct wait_queue_entry *wait,
				       long timeout,
				       struct vsock_transport_recv_notify_data *recv_data,
				       size_t target)
{
	const struct vsock_transport *transport;
	struct vsock_sock *vsk;
	s64 data;
	int err;

	vsk = vsock_sk(sk);
	err = 0;
	transport = vsk->transport;

	while (1) {
		prepare_to_wait(sk_sleep(sk), wait, TASK_INTERRUPTIBLE);
		data = vsock_connectible_has_data(vsk);
		if (data != 0)
			break;

		if (sk->sk_err != 0 ||
		    (sk->sk_shutdown & RCV_SHUTDOWN) ||
		    (vsk->peer_shutdown & SEND_SHUTDOWN)) {
			break;
		}

		/* Don't wait for non-blocking sockets. */
		if (timeout == 0) {
			err = -EAGAIN;
			break;
		}

		if (recv_data) {
			err = transport->notify_recv_pre_block(vsk, target, recv_data);
			if (err < 0)
				break;
		}

		release_sock(sk);
		timeout = schedule_timeout(timeout);
		lock_sock(sk);

		if (signal_pending(current)) {
			err = sock_intr_errno(timeout);
			break;
		} else if (timeout == 0) {
			err = -EAGAIN;
			break;
		}
	}

	finish_wait(sk_sleep(sk), wait);

	if (err)
		return err;

	/* Internal transport error when checking for available
	 * data. XXX This should be changed to a connection
	 * reset in a later change.
	 */
	if (data < 0)
		return -ENOMEM;

	return data;
}

static int __vsock_stream_recvmsg(struct sock *sk, struct msghdr *msg,
				  size_t len, int flags)
{
	struct vsock_transport_recv_notify_data recv_data;
	const struct vsock_transport *transport;
	struct vsock_sock *vsk;
	ssize_t copied;
	size_t target;
	long timeout;
	int err;

	DEFINE_WAIT(wait);

	vsk = vsock_sk(sk);
	transport = vsk->transport;

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
		ssize_t read;

		err = vsock_connectible_wait_data(sk, &wait, timeout,
						  &recv_data, target);
		if (err <= 0)
			break;

		err = transport->notify_recv_pre_dequeue(vsk, target,
							 &recv_data);
		if (err < 0)
			break;

		read = transport->stream_dequeue(vsk, msg, len - copied, flags);
		if (read < 0) {
			err = read;
			break;
		}

		copied += read;

		err = transport->notify_recv_post_dequeue(vsk, target, read,
						!(flags & MSG_PEEK), &recv_data);
		if (err < 0)
			goto out;

		if (read >= target || flags & MSG_PEEK)
			break;

		target -= read;
	}

	if (sk->sk_err)
		err = -sk->sk_err;
	else if (sk->sk_shutdown & RCV_SHUTDOWN)
		err = 0;

	if (copied > 0)
		err = copied;

out:
	return err;
}

static int __vsock_seqpacket_recvmsg(struct sock *sk, struct msghdr *msg,
				     size_t len, int flags)
{
	const struct vsock_transport *transport;
	struct vsock_sock *vsk;
	ssize_t msg_len;
	long timeout;
	int err = 0;
	DEFINE_WAIT(wait);

	vsk = vsock_sk(sk);
	transport = vsk->transport;

	timeout = sock_rcvtimeo(sk, flags & MSG_DONTWAIT);

	err = vsock_connectible_wait_data(sk, &wait, timeout, NULL, 0);
	if (err <= 0)
		goto out;

	msg_len = transport->seqpacket_dequeue(vsk, msg, flags);

	if (msg_len < 0) {
		err = msg_len;
		goto out;
	}

	if (sk->sk_err) {
		err = -sk->sk_err;
	} else if (sk->sk_shutdown & RCV_SHUTDOWN) {
		err = 0;
	} else {
		/* User sets MSG_TRUNC, so return real length of
		 * packet.
		 */
		if (flags & MSG_TRUNC)
			err = msg_len;
		else
			err = len - msg_data_left(msg);

		/* Always set MSG_TRUNC if real length of packet is
		 * bigger than user's buffer.
		 */
		if (msg_len > len)
			msg->msg_flags |= MSG_TRUNC;
	}

out:
	return err;
}

int
vsock_connectible_recvmsg(struct socket *sock, struct msghdr *msg, size_t len,
			  int flags)
{
	struct sock *sk;
	struct vsock_sock *vsk;
	const struct vsock_transport *transport;
#ifdef CONFIG_BPF_SYSCALL
	const struct proto *prot;
#endif
	int err;

	sk = sock->sk;

	if (unlikely(flags & MSG_ERRQUEUE))
		return sock_recv_errqueue(sk, msg, len, SOL_VSOCK, VSOCK_RECVERR);

	vsk = vsock_sk(sk);
	err = 0;

	lock_sock(sk);

	transport = vsk->transport;

	if (!transport || sk->sk_state != TCP_ESTABLISHED) {
		/* Recvmsg is supposed to return 0 if a peer performs an
		 * orderly shutdown. Differentiate between that case and when a
		 * peer has not connected or a local shutdown occurred with the
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

#ifdef CONFIG_BPF_SYSCALL
	prot = READ_ONCE(sk->sk_prot);
	if (prot != &vsock_proto) {
		release_sock(sk);
		return prot->recvmsg(sk, msg, len, flags, NULL);
	}
#endif

	if (sk->sk_type == SOCK_STREAM)
		err = __vsock_stream_recvmsg(sk, msg, len, flags);
	else
		err = __vsock_seqpacket_recvmsg(sk, msg, len, flags);

out:
	release_sock(sk);
	return err;
}
EXPORT_SYMBOL_GPL(vsock_connectible_recvmsg);

static int vsock_set_rcvlowat(struct sock *sk, int val)
{
	const struct vsock_transport *transport;
	struct vsock_sock *vsk;

	vsk = vsock_sk(sk);

	if (val > vsk->buffer_size)
		return -EINVAL;

	transport = vsk->transport;

	if (transport && transport->set_rcvlowat)
		return transport->set_rcvlowat(vsk, val);

	WRITE_ONCE(sk->sk_rcvlowat, val ? : 1);
	return 0;
}

static const struct proto_ops vsock_stream_ops = {
	.family = PF_VSOCK,
	.owner = THIS_MODULE,
	.release = vsock_release,
	.bind = vsock_bind,
	.connect = vsock_connect,
	.socketpair = sock_no_socketpair,
	.accept = vsock_accept,
	.getname = vsock_getname,
	.poll = vsock_poll,
	.ioctl = sock_no_ioctl,
	.listen = vsock_listen,
	.shutdown = vsock_shutdown,
	.setsockopt = vsock_connectible_setsockopt,
	.getsockopt = vsock_connectible_getsockopt,
	.sendmsg = vsock_connectible_sendmsg,
	.recvmsg = vsock_connectible_recvmsg,
	.mmap = sock_no_mmap,
	.set_rcvlowat = vsock_set_rcvlowat,
	.read_skb = vsock_read_skb,
};

static const struct proto_ops vsock_seqpacket_ops = {
	.family = PF_VSOCK,
	.owner = THIS_MODULE,
	.release = vsock_release,
	.bind = vsock_bind,
	.connect = vsock_connect,
	.socketpair = sock_no_socketpair,
	.accept = vsock_accept,
	.getname = vsock_getname,
	.poll = vsock_poll,
	.ioctl = sock_no_ioctl,
	.listen = vsock_listen,
	.shutdown = vsock_shutdown,
	.setsockopt = vsock_connectible_setsockopt,
	.getsockopt = vsock_connectible_getsockopt,
	.sendmsg = vsock_connectible_sendmsg,
	.recvmsg = vsock_connectible_recvmsg,
	.mmap = sock_no_mmap,
	.read_skb = vsock_read_skb,
};

static int vsock_create(struct net *net, struct socket *sock,
			int protocol, int kern)
{
	struct vsock_sock *vsk;
	struct sock *sk;
	int ret;

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
	case SOCK_SEQPACKET:
		sock->ops = &vsock_seqpacket_ops;
		break;
	default:
		return -ESOCKTNOSUPPORT;
	}

	sock->state = SS_UNCONNECTED;

	sk = __vsock_create(net, sock, NULL, GFP_KERNEL, 0, kern);
	if (!sk)
		return -ENOMEM;

	vsk = vsock_sk(sk);

	if (sock->type == SOCK_DGRAM) {
		ret = vsock_assign_transport(vsk, NULL);
		if (ret < 0) {
			sock_put(sk);
			return ret;
		}
	}

	vsock_insert_unbound(vsk);

	return 0;
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
	u32 cid = VMADDR_CID_ANY;
	int retval = 0;

	switch (cmd) {
	case IOCTL_VM_SOCKETS_GET_LOCAL_CID:
		/* To be compatible with the VMCI behavior, we prioritize the
		 * guest CID instead of well-know host CID (VMADDR_CID_HOST).
		 */
		if (transport_g2h)
			cid = transport_g2h->get_local_cid();
		else if (transport_h2g)
			cid = transport_h2g->get_local_cid();

		if (put_user(cid, p) != 0)
			retval = -EFAULT;
		break;

	default:
		retval = -ENOIOCTLCMD;
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

static int __init vsock_init(void)
{
	int err = 0;

	vsock_init_tables();

	vsock_proto.owner = THIS_MODULE;
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

	vsock_bpf_build_proto();

	return 0;

err_unregister_proto:
	proto_unregister(&vsock_proto);
err_deregister_misc:
	misc_deregister(&vsock_device);
err_reset_transport:
	return err;
}

static void __exit vsock_exit(void)
{
	misc_deregister(&vsock_device);
	sock_unregister(AF_VSOCK);
	proto_unregister(&vsock_proto);
}

const struct vsock_transport *vsock_core_get_transport(struct vsock_sock *vsk)
{
	return vsk->transport;
}
EXPORT_SYMBOL_GPL(vsock_core_get_transport);

int vsock_core_register(const struct vsock_transport *t, int features)
{
	const struct vsock_transport *t_h2g, *t_g2h, *t_dgram, *t_local;
	int err = mutex_lock_interruptible(&vsock_register_mutex);

	if (err)
		return err;

	t_h2g = transport_h2g;
	t_g2h = transport_g2h;
	t_dgram = transport_dgram;
	t_local = transport_local;

	if (features & VSOCK_TRANSPORT_F_H2G) {
		if (t_h2g) {
			err = -EBUSY;
			goto err_busy;
		}
		t_h2g = t;
	}

	if (features & VSOCK_TRANSPORT_F_G2H) {
		if (t_g2h) {
			err = -EBUSY;
			goto err_busy;
		}
		t_g2h = t;
	}

	if (features & VSOCK_TRANSPORT_F_DGRAM) {
		if (t_dgram) {
			err = -EBUSY;
			goto err_busy;
		}
		t_dgram = t;
	}

	if (features & VSOCK_TRANSPORT_F_LOCAL) {
		if (t_local) {
			err = -EBUSY;
			goto err_busy;
		}
		t_local = t;
	}

	transport_h2g = t_h2g;
	transport_g2h = t_g2h;
	transport_dgram = t_dgram;
	transport_local = t_local;

err_busy:
	mutex_unlock(&vsock_register_mutex);
	return err;
}
EXPORT_SYMBOL_GPL(vsock_core_register);

void vsock_core_unregister(const struct vsock_transport *t)
{
	mutex_lock(&vsock_register_mutex);

	if (transport_h2g == t)
		transport_h2g = NULL;

	if (transport_g2h == t)
		transport_g2h = NULL;

	if (transport_dgram == t)
		transport_dgram = NULL;

	if (transport_local == t)
		transport_local = NULL;

	mutex_unlock(&vsock_register_mutex);
}
EXPORT_SYMBOL_GPL(vsock_core_unregister);

module_init(vsock_init);
module_exit(vsock_exit);

MODULE_AUTHOR("VMware, Inc.");
MODULE_DESCRIPTION("VMware Virtual Socket Family");
MODULE_VERSION("1.0.2.0-k");
MODULE_LICENSE("GPL v2");
