// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * NET4:	Implementation of BSD Unix domain sockets.
 *
 * Authors:	Alan Cox, <alan@lxorguk.ukuu.org.uk>
 *
 * Fixes:
 *		Linus Torvalds	:	Assorted bug cures.
 *		Niibe Yutaka	:	async I/O support.
 *		Carsten Paeth	:	PF_UNIX check, address fixes.
 *		Alan Cox	:	Limit size of allocated blocks.
 *		Alan Cox	:	Fixed the stupid socketpair bug.
 *		Alan Cox	:	BSD compatibility fine tuning.
 *		Alan Cox	:	Fixed a bug in connect when interrupted.
 *		Alan Cox	:	Sorted out a proper draft version of
 *					file descriptor passing hacked up from
 *					Mike Shaver's work.
 *		Marty Leisner	:	Fixes to fd passing
 *		Nick Nevin	:	recvmsg bugfix.
 *		Alan Cox	:	Started proper garbage collector
 *		Heiko EiBfeldt	:	Missing verify_area check
 *		Alan Cox	:	Started POSIXisms
 *		Andreas Schwab	:	Replace inode by dentry for proper
 *					reference counting
 *		Kirk Petersen	:	Made this a module
 *	    Christoph Rohland	:	Elegant non-blocking accept/connect algorithm.
 *					Lots of bug fixes.
 *	     Alexey Kuznetosv	:	Repaired (I hope) bugs introduces
 *					by above two patches.
 *	     Andrea Arcangeli	:	If possible we block in connect(2)
 *					if the max backlog of the listen socket
 *					is been reached. This won't break
 *					old apps and it will avoid huge amount
 *					of socks hashed (this for unix_gc()
 *					performances reasons).
 *					Security fix that limits the max
 *					number of socks to 2*max_files and
 *					the number of skb queueable in the
 *					dgram receiver.
 *		Artur Skawina   :	Hash function optimizations
 *	     Alexey Kuznetsov   :	Full scale SMP. Lot of bugs are introduced 8)
 *	      Malcolm Beattie   :	Set peercred for socketpair
 *	     Michal Ostrowski   :       Module initialization cleanup.
 *	     Arnaldo C. Melo	:	Remove MOD_{INC,DEC}_USE_COUNT,
 *	     				the core infrastructure is doing that
 *	     				for all net proto families now (2.5.69+)
 *
 * Known differences from reference BSD that was tested:
 *
 *	[TO FIX]
 *	ECONNREFUSED is not returned from one end of a connected() socket to the
 *		other the moment one end closes.
 *	fstat() doesn't return st_dev=0, and give the blksize as high water mark
 *		and a fake inode identifier (nor the BSD first socket fstat twice bug).
 *	[NOT TO FIX]
 *	accept() returns a path name even if the connecting socket has closed
 *		in the meantime (BSD loses the path and gives up).
 *	accept() returns 0 length path for an unbound connector. BSD returns 16
 *		and a null first byte in the path (but not for gethost/peername - BSD bug ??)
 *	socketpair(...SOCK_RAW..) doesn't panic the kernel.
 *	BSD af_unix apparently has connect forgetting to block properly.
 *		(need to check this with the POSIX spec in detail)
 *
 * Differences from 2.0.0-11-... (ANK)
 *	Bug fixes and improvements.
 *		- client shutdown killed server socket.
 *		- removed all useless cli/sti pairs.
 *
 *	Semantic changes/extensions.
 *		- generic control message passing.
 *		- SCM_CREDENTIALS control message.
 *		- "Abstract" (not FS based) socket bindings.
 *		  Abstract names are sequences of bytes (not zero terminated)
 *		  started by 0, so that this name space does not intersect
 *		  with BSD names.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/sched/signal.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/dcache.h>
#include <linux/namei.h>
#include <linux/socket.h>
#include <linux/un.h>
#include <linux/fcntl.h>
#include <linux/termios.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/in.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <net/net_namespace.h>
#include <net/sock.h>
#include <net/tcp_states.h>
#include <net/af_unix.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <net/scm.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/rtnetlink.h>
#include <linux/mount.h>
#include <net/checksum.h>
#include <linux/security.h>
#include <linux/freezer.h>
#include <linux/file.h>
#include <linux/btf_ids.h>

#include "scm.h"

struct hlist_head unix_socket_table[2 * UNIX_HASH_SIZE];
EXPORT_SYMBOL_GPL(unix_socket_table);
DEFINE_SPINLOCK(unix_table_lock);
EXPORT_SYMBOL_GPL(unix_table_lock);
static atomic_long_t unix_nr_socks;


static struct hlist_head *unix_sockets_unbound(void *addr)
{
	unsigned long hash = (unsigned long)addr;

	hash ^= hash >> 16;
	hash ^= hash >> 8;
	hash %= UNIX_HASH_SIZE;
	return &unix_socket_table[UNIX_HASH_SIZE + hash];
}

#define UNIX_ABSTRACT(sk)	(unix_sk(sk)->addr->hash < UNIX_HASH_SIZE)

#ifdef CONFIG_SECURITY_NETWORK
static void unix_get_secdata(struct scm_cookie *scm, struct sk_buff *skb)
{
	UNIXCB(skb).secid = scm->secid;
}

static inline void unix_set_secdata(struct scm_cookie *scm, struct sk_buff *skb)
{
	scm->secid = UNIXCB(skb).secid;
}

static inline bool unix_secdata_eq(struct scm_cookie *scm, struct sk_buff *skb)
{
	return (scm->secid == UNIXCB(skb).secid);
}
#else
static inline void unix_get_secdata(struct scm_cookie *scm, struct sk_buff *skb)
{ }

static inline void unix_set_secdata(struct scm_cookie *scm, struct sk_buff *skb)
{ }

static inline bool unix_secdata_eq(struct scm_cookie *scm, struct sk_buff *skb)
{
	return true;
}
#endif /* CONFIG_SECURITY_NETWORK */

/*
 *  SMP locking strategy:
 *    hash table is protected with spinlock unix_table_lock
 *    each socket state is protected by separate spin lock.
 */

static inline unsigned int unix_hash_fold(__wsum n)
{
	unsigned int hash = (__force unsigned int)csum_fold(n);

	hash ^= hash>>8;
	return hash&(UNIX_HASH_SIZE-1);
}

#define unix_peer(sk) (unix_sk(sk)->peer)

static inline int unix_our_peer(struct sock *sk, struct sock *osk)
{
	return unix_peer(osk) == sk;
}

static inline int unix_may_send(struct sock *sk, struct sock *osk)
{
	return unix_peer(osk) == NULL || unix_our_peer(sk, osk);
}

static inline int unix_recvq_full(const struct sock *sk)
{
	return skb_queue_len(&sk->sk_receive_queue) > sk->sk_max_ack_backlog;
}

static inline int unix_recvq_full_lockless(const struct sock *sk)
{
	return skb_queue_len_lockless(&sk->sk_receive_queue) >
		READ_ONCE(sk->sk_max_ack_backlog);
}

struct sock *unix_peer_get(struct sock *s)
{
	struct sock *peer;

	unix_state_lock(s);
	peer = unix_peer(s);
	if (peer)
		sock_hold(peer);
	unix_state_unlock(s);
	return peer;
}
EXPORT_SYMBOL_GPL(unix_peer_get);

static inline void unix_release_addr(struct unix_address *addr)
{
	if (refcount_dec_and_test(&addr->refcnt))
		kfree(addr);
}

/*
 *	Check unix socket name:
 *		- should be not zero length.
 *	        - if started by not zero, should be NULL terminated (FS object)
 *		- if started by zero, it is abstract name.
 */

static int unix_mkname(struct sockaddr_un *sunaddr, int len, unsigned int *hashp)
{
	*hashp = 0;

	if (len <= sizeof(short) || len > sizeof(*sunaddr))
		return -EINVAL;
	if (!sunaddr || sunaddr->sun_family != AF_UNIX)
		return -EINVAL;
	if (sunaddr->sun_path[0]) {
		/*
		 * This may look like an off by one error but it is a bit more
		 * subtle. 108 is the longest valid AF_UNIX path for a binding.
		 * sun_path[108] doesn't as such exist.  However in kernel space
		 * we are guaranteed that it is a valid memory location in our
		 * kernel address buffer.
		 */
		((char *)sunaddr)[len] = 0;
		len = strlen(sunaddr->sun_path)+1+sizeof(short);
		return len;
	}

	*hashp = unix_hash_fold(csum_partial(sunaddr, len, 0));
	return len;
}

static void __unix_remove_socket(struct sock *sk)
{
	sk_del_node_init(sk);
}

static void __unix_insert_socket(struct hlist_head *list, struct sock *sk)
{
	WARN_ON(!sk_unhashed(sk));
	sk_add_node(sk, list);
}

static void __unix_set_addr(struct sock *sk, struct unix_address *addr,
			    unsigned hash)
{
	__unix_remove_socket(sk);
	smp_store_release(&unix_sk(sk)->addr, addr);
	__unix_insert_socket(&unix_socket_table[hash], sk);
}

static inline void unix_remove_socket(struct sock *sk)
{
	spin_lock(&unix_table_lock);
	__unix_remove_socket(sk);
	spin_unlock(&unix_table_lock);
}

static inline void unix_insert_socket(struct hlist_head *list, struct sock *sk)
{
	spin_lock(&unix_table_lock);
	__unix_insert_socket(list, sk);
	spin_unlock(&unix_table_lock);
}

static struct sock *__unix_find_socket_byname(struct net *net,
					      struct sockaddr_un *sunname,
					      int len, unsigned int hash)
{
	struct sock *s;

	sk_for_each(s, &unix_socket_table[hash]) {
		struct unix_sock *u = unix_sk(s);

		if (!net_eq(sock_net(s), net))
			continue;

		if (u->addr->len == len &&
		    !memcmp(u->addr->name, sunname, len))
			return s;
	}
	return NULL;
}

static inline struct sock *unix_find_socket_byname(struct net *net,
						   struct sockaddr_un *sunname,
						   int len, unsigned int hash)
{
	struct sock *s;

	spin_lock(&unix_table_lock);
	s = __unix_find_socket_byname(net, sunname, len, hash);
	if (s)
		sock_hold(s);
	spin_unlock(&unix_table_lock);
	return s;
}

static struct sock *unix_find_socket_byinode(struct inode *i)
{
	struct sock *s;

	spin_lock(&unix_table_lock);
	sk_for_each(s,
		    &unix_socket_table[i->i_ino & (UNIX_HASH_SIZE - 1)]) {
		struct dentry *dentry = unix_sk(s)->path.dentry;

		if (dentry && d_backing_inode(dentry) == i) {
			sock_hold(s);
			goto found;
		}
	}
	s = NULL;
found:
	spin_unlock(&unix_table_lock);
	return s;
}

/* Support code for asymmetrically connected dgram sockets
 *
 * If a datagram socket is connected to a socket not itself connected
 * to the first socket (eg, /dev/log), clients may only enqueue more
 * messages if the present receive queue of the server socket is not
 * "too large". This means there's a second writeability condition
 * poll and sendmsg need to test. The dgram recv code will do a wake
 * up on the peer_wait wait queue of a socket upon reception of a
 * datagram which needs to be propagated to sleeping would-be writers
 * since these might not have sent anything so far. This can't be
 * accomplished via poll_wait because the lifetime of the server
 * socket might be less than that of its clients if these break their
 * association with it or if the server socket is closed while clients
 * are still connected to it and there's no way to inform "a polling
 * implementation" that it should let go of a certain wait queue
 *
 * In order to propagate a wake up, a wait_queue_entry_t of the client
 * socket is enqueued on the peer_wait queue of the server socket
 * whose wake function does a wake_up on the ordinary client socket
 * wait queue. This connection is established whenever a write (or
 * poll for write) hit the flow control condition and broken when the
 * association to the server socket is dissolved or after a wake up
 * was relayed.
 */

static int unix_dgram_peer_wake_relay(wait_queue_entry_t *q, unsigned mode, int flags,
				      void *key)
{
	struct unix_sock *u;
	wait_queue_head_t *u_sleep;

	u = container_of(q, struct unix_sock, peer_wake);

	__remove_wait_queue(&unix_sk(u->peer_wake.private)->peer_wait,
			    q);
	u->peer_wake.private = NULL;

	/* relaying can only happen while the wq still exists */
	u_sleep = sk_sleep(&u->sk);
	if (u_sleep)
		wake_up_interruptible_poll(u_sleep, key_to_poll(key));

	return 0;
}

static int unix_dgram_peer_wake_connect(struct sock *sk, struct sock *other)
{
	struct unix_sock *u, *u_other;
	int rc;

	u = unix_sk(sk);
	u_other = unix_sk(other);
	rc = 0;
	spin_lock(&u_other->peer_wait.lock);

	if (!u->peer_wake.private) {
		u->peer_wake.private = other;
		__add_wait_queue(&u_other->peer_wait, &u->peer_wake);

		rc = 1;
	}

	spin_unlock(&u_other->peer_wait.lock);
	return rc;
}

static void unix_dgram_peer_wake_disconnect(struct sock *sk,
					    struct sock *other)
{
	struct unix_sock *u, *u_other;

	u = unix_sk(sk);
	u_other = unix_sk(other);
	spin_lock(&u_other->peer_wait.lock);

	if (u->peer_wake.private == other) {
		__remove_wait_queue(&u_other->peer_wait, &u->peer_wake);
		u->peer_wake.private = NULL;
	}

	spin_unlock(&u_other->peer_wait.lock);
}

static void unix_dgram_peer_wake_disconnect_wakeup(struct sock *sk,
						   struct sock *other)
{
	unix_dgram_peer_wake_disconnect(sk, other);
	wake_up_interruptible_poll(sk_sleep(sk),
				   EPOLLOUT |
				   EPOLLWRNORM |
				   EPOLLWRBAND);
}

/* preconditions:
 *	- unix_peer(sk) == other
 *	- association is stable
 */
static int unix_dgram_peer_wake_me(struct sock *sk, struct sock *other)
{
	int connected;

	connected = unix_dgram_peer_wake_connect(sk, other);

	/* If other is SOCK_DEAD, we want to make sure we signal
	 * POLLOUT, such that a subsequent write() can get a
	 * -ECONNREFUSED. Otherwise, if we haven't queued any skbs
	 * to other and its full, we will hang waiting for POLLOUT.
	 */
	if (unix_recvq_full(other) && !sock_flag(other, SOCK_DEAD))
		return 1;

	if (connected)
		unix_dgram_peer_wake_disconnect(sk, other);

	return 0;
}

static int unix_writable(const struct sock *sk)
{
	return sk->sk_state != TCP_LISTEN &&
	       (refcount_read(&sk->sk_wmem_alloc) << 2) <= sk->sk_sndbuf;
}

static void unix_write_space(struct sock *sk)
{
	struct socket_wq *wq;

	rcu_read_lock();
	if (unix_writable(sk)) {
		wq = rcu_dereference(sk->sk_wq);
		if (skwq_has_sleeper(wq))
			wake_up_interruptible_sync_poll(&wq->wait,
				EPOLLOUT | EPOLLWRNORM | EPOLLWRBAND);
		sk_wake_async(sk, SOCK_WAKE_SPACE, POLL_OUT);
	}
	rcu_read_unlock();
}

/* When dgram socket disconnects (or changes its peer), we clear its receive
 * queue of packets arrived from previous peer. First, it allows to do
 * flow control based only on wmem_alloc; second, sk connected to peer
 * may receive messages only from that peer. */
static void unix_dgram_disconnected(struct sock *sk, struct sock *other)
{
	if (!skb_queue_empty(&sk->sk_receive_queue)) {
		skb_queue_purge(&sk->sk_receive_queue);
		wake_up_interruptible_all(&unix_sk(sk)->peer_wait);

		/* If one link of bidirectional dgram pipe is disconnected,
		 * we signal error. Messages are lost. Do not make this,
		 * when peer was not connected to us.
		 */
		if (!sock_flag(other, SOCK_DEAD) && unix_peer(other) == sk) {
			other->sk_err = ECONNRESET;
			sk_error_report(other);
		}
	}
	other->sk_state = TCP_CLOSE;
}

static void unix_sock_destructor(struct sock *sk)
{
	struct unix_sock *u = unix_sk(sk);

	skb_queue_purge(&sk->sk_receive_queue);

#if IS_ENABLED(CONFIG_AF_UNIX_OOB)
	if (u->oob_skb) {
		kfree_skb(u->oob_skb);
		u->oob_skb = NULL;
	}
#endif
	WARN_ON(refcount_read(&sk->sk_wmem_alloc));
	WARN_ON(!sk_unhashed(sk));
	WARN_ON(sk->sk_socket);
	if (!sock_flag(sk, SOCK_DEAD)) {
		pr_info("Attempt to release alive unix socket: %p\n", sk);
		return;
	}

	if (u->addr)
		unix_release_addr(u->addr);

	atomic_long_dec(&unix_nr_socks);
	local_bh_disable();
	sock_prot_inuse_add(sock_net(sk), sk->sk_prot, -1);
	local_bh_enable();
#ifdef UNIX_REFCNT_DEBUG
	pr_debug("UNIX %p is destroyed, %ld are still alive.\n", sk,
		atomic_long_read(&unix_nr_socks));
#endif
}

static void unix_release_sock(struct sock *sk, int embrion)
{
	struct unix_sock *u = unix_sk(sk);
	struct path path;
	struct sock *skpair;
	struct sk_buff *skb;
	int state;

	unix_remove_socket(sk);

	/* Clear state */
	unix_state_lock(sk);
	sock_orphan(sk);
	sk->sk_shutdown = SHUTDOWN_MASK;
	path	     = u->path;
	u->path.dentry = NULL;
	u->path.mnt = NULL;
	state = sk->sk_state;
	sk->sk_state = TCP_CLOSE;

	skpair = unix_peer(sk);
	unix_peer(sk) = NULL;

	unix_state_unlock(sk);

	wake_up_interruptible_all(&u->peer_wait);

	if (skpair != NULL) {
		if (sk->sk_type == SOCK_STREAM || sk->sk_type == SOCK_SEQPACKET) {
			unix_state_lock(skpair);
			/* No more writes */
			skpair->sk_shutdown = SHUTDOWN_MASK;
			if (!skb_queue_empty(&sk->sk_receive_queue) || embrion)
				skpair->sk_err = ECONNRESET;
			unix_state_unlock(skpair);
			skpair->sk_state_change(skpair);
			sk_wake_async(skpair, SOCK_WAKE_WAITD, POLL_HUP);
		}

		unix_dgram_peer_wake_disconnect(sk, skpair);
		sock_put(skpair); /* It may now die */
	}

	/* Try to flush out this socket. Throw out buffers at least */

	while ((skb = skb_dequeue(&sk->sk_receive_queue)) != NULL) {
		if (state == TCP_LISTEN)
			unix_release_sock(skb->sk, 1);
		/* passed fds are erased in the kfree_skb hook	      */
		UNIXCB(skb).consumed = skb->len;
		kfree_skb(skb);
	}

	if (path.dentry)
		path_put(&path);

	sock_put(sk);

	/* ---- Socket is dead now and most probably destroyed ---- */

	/*
	 * Fixme: BSD difference: In BSD all sockets connected to us get
	 *	  ECONNRESET and we die on the spot. In Linux we behave
	 *	  like files and pipes do and wait for the last
	 *	  dereference.
	 *
	 * Can't we simply set sock->err?
	 *
	 *	  What the above comment does talk about? --ANK(980817)
	 */

	if (unix_tot_inflight)
		unix_gc();		/* Garbage collect fds */
}

static void init_peercred(struct sock *sk)
{
	const struct cred *old_cred;
	struct pid *old_pid;

	spin_lock(&sk->sk_peer_lock);
	old_pid = sk->sk_peer_pid;
	old_cred = sk->sk_peer_cred;
	sk->sk_peer_pid  = get_pid(task_tgid(current));
	sk->sk_peer_cred = get_current_cred();
	spin_unlock(&sk->sk_peer_lock);

	put_pid(old_pid);
	put_cred(old_cred);
}

static void copy_peercred(struct sock *sk, struct sock *peersk)
{
	const struct cred *old_cred;
	struct pid *old_pid;

	if (sk < peersk) {
		spin_lock(&sk->sk_peer_lock);
		spin_lock_nested(&peersk->sk_peer_lock, SINGLE_DEPTH_NESTING);
	} else {
		spin_lock(&peersk->sk_peer_lock);
		spin_lock_nested(&sk->sk_peer_lock, SINGLE_DEPTH_NESTING);
	}
	old_pid = sk->sk_peer_pid;
	old_cred = sk->sk_peer_cred;
	sk->sk_peer_pid  = get_pid(peersk->sk_peer_pid);
	sk->sk_peer_cred = get_cred(peersk->sk_peer_cred);

	spin_unlock(&sk->sk_peer_lock);
	spin_unlock(&peersk->sk_peer_lock);

	put_pid(old_pid);
	put_cred(old_cred);
}

static int unix_listen(struct socket *sock, int backlog)
{
	int err;
	struct sock *sk = sock->sk;
	struct unix_sock *u = unix_sk(sk);

	err = -EOPNOTSUPP;
	if (sock->type != SOCK_STREAM && sock->type != SOCK_SEQPACKET)
		goto out;	/* Only stream/seqpacket sockets accept */
	err = -EINVAL;
	if (!u->addr)
		goto out;	/* No listens on an unbound socket */
	unix_state_lock(sk);
	if (sk->sk_state != TCP_CLOSE && sk->sk_state != TCP_LISTEN)
		goto out_unlock;
	if (backlog > sk->sk_max_ack_backlog)
		wake_up_interruptible_all(&u->peer_wait);
	sk->sk_max_ack_backlog	= backlog;
	sk->sk_state		= TCP_LISTEN;
	/* set credentials so connect can copy them */
	init_peercred(sk);
	err = 0;

out_unlock:
	unix_state_unlock(sk);
out:
	return err;
}

static int unix_release(struct socket *);
static int unix_bind(struct socket *, struct sockaddr *, int);
static int unix_stream_connect(struct socket *, struct sockaddr *,
			       int addr_len, int flags);
static int unix_socketpair(struct socket *, struct socket *);
static int unix_accept(struct socket *, struct socket *, int, bool);
static int unix_getname(struct socket *, struct sockaddr *, int);
static __poll_t unix_poll(struct file *, struct socket *, poll_table *);
static __poll_t unix_dgram_poll(struct file *, struct socket *,
				    poll_table *);
static int unix_ioctl(struct socket *, unsigned int, unsigned long);
#ifdef CONFIG_COMPAT
static int unix_compat_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg);
#endif
static int unix_shutdown(struct socket *, int);
static int unix_stream_sendmsg(struct socket *, struct msghdr *, size_t);
static int unix_stream_recvmsg(struct socket *, struct msghdr *, size_t, int);
static ssize_t unix_stream_sendpage(struct socket *, struct page *, int offset,
				    size_t size, int flags);
static ssize_t unix_stream_splice_read(struct socket *,  loff_t *ppos,
				       struct pipe_inode_info *, size_t size,
				       unsigned int flags);
static int unix_dgram_sendmsg(struct socket *, struct msghdr *, size_t);
static int unix_dgram_recvmsg(struct socket *, struct msghdr *, size_t, int);
static int unix_read_sock(struct sock *sk, read_descriptor_t *desc,
			  sk_read_actor_t recv_actor);
static int unix_stream_read_sock(struct sock *sk, read_descriptor_t *desc,
				 sk_read_actor_t recv_actor);
static int unix_dgram_connect(struct socket *, struct sockaddr *,
			      int, int);
static int unix_seqpacket_sendmsg(struct socket *, struct msghdr *, size_t);
static int unix_seqpacket_recvmsg(struct socket *, struct msghdr *, size_t,
				  int);

static int unix_set_peek_off(struct sock *sk, int val)
{
	struct unix_sock *u = unix_sk(sk);

	if (mutex_lock_interruptible(&u->iolock))
		return -EINTR;

	sk->sk_peek_off = val;
	mutex_unlock(&u->iolock);

	return 0;
}

#ifdef CONFIG_PROC_FS
static void unix_show_fdinfo(struct seq_file *m, struct socket *sock)
{
	struct sock *sk = sock->sk;
	struct unix_sock *u;

	if (sk) {
		u = unix_sk(sock->sk);
		seq_printf(m, "scm_fds: %u\n",
			   atomic_read(&u->scm_stat.nr_fds));
	}
}
#else
#define unix_show_fdinfo NULL
#endif

static const struct proto_ops unix_stream_ops = {
	.family =	PF_UNIX,
	.owner =	THIS_MODULE,
	.release =	unix_release,
	.bind =		unix_bind,
	.connect =	unix_stream_connect,
	.socketpair =	unix_socketpair,
	.accept =	unix_accept,
	.getname =	unix_getname,
	.poll =		unix_poll,
	.ioctl =	unix_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl =	unix_compat_ioctl,
#endif
	.listen =	unix_listen,
	.shutdown =	unix_shutdown,
	.sendmsg =	unix_stream_sendmsg,
	.recvmsg =	unix_stream_recvmsg,
	.read_sock =	unix_stream_read_sock,
	.mmap =		sock_no_mmap,
	.sendpage =	unix_stream_sendpage,
	.splice_read =	unix_stream_splice_read,
	.set_peek_off =	unix_set_peek_off,
	.show_fdinfo =	unix_show_fdinfo,
};

static const struct proto_ops unix_dgram_ops = {
	.family =	PF_UNIX,
	.owner =	THIS_MODULE,
	.release =	unix_release,
	.bind =		unix_bind,
	.connect =	unix_dgram_connect,
	.socketpair =	unix_socketpair,
	.accept =	sock_no_accept,
	.getname =	unix_getname,
	.poll =		unix_dgram_poll,
	.ioctl =	unix_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl =	unix_compat_ioctl,
#endif
	.listen =	sock_no_listen,
	.shutdown =	unix_shutdown,
	.sendmsg =	unix_dgram_sendmsg,
	.read_sock =	unix_read_sock,
	.recvmsg =	unix_dgram_recvmsg,
	.mmap =		sock_no_mmap,
	.sendpage =	sock_no_sendpage,
	.set_peek_off =	unix_set_peek_off,
	.show_fdinfo =	unix_show_fdinfo,
};

static const struct proto_ops unix_seqpacket_ops = {
	.family =	PF_UNIX,
	.owner =	THIS_MODULE,
	.release =	unix_release,
	.bind =		unix_bind,
	.connect =	unix_stream_connect,
	.socketpair =	unix_socketpair,
	.accept =	unix_accept,
	.getname =	unix_getname,
	.poll =		unix_dgram_poll,
	.ioctl =	unix_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl =	unix_compat_ioctl,
#endif
	.listen =	unix_listen,
	.shutdown =	unix_shutdown,
	.sendmsg =	unix_seqpacket_sendmsg,
	.recvmsg =	unix_seqpacket_recvmsg,
	.mmap =		sock_no_mmap,
	.sendpage =	sock_no_sendpage,
	.set_peek_off =	unix_set_peek_off,
	.show_fdinfo =	unix_show_fdinfo,
};

static void unix_close(struct sock *sk, long timeout)
{
	/* Nothing to do here, unix socket does not need a ->close().
	 * This is merely for sockmap.
	 */
}

static void unix_unhash(struct sock *sk)
{
	/* Nothing to do here, unix socket does not need a ->unhash().
	 * This is merely for sockmap.
	 */
}

struct proto unix_dgram_proto = {
	.name			= "UNIX",
	.owner			= THIS_MODULE,
	.obj_size		= sizeof(struct unix_sock),
	.close			= unix_close,
#ifdef CONFIG_BPF_SYSCALL
	.psock_update_sk_prot	= unix_dgram_bpf_update_proto,
#endif
};

struct proto unix_stream_proto = {
	.name			= "UNIX-STREAM",
	.owner			= THIS_MODULE,
	.obj_size		= sizeof(struct unix_sock),
	.close			= unix_close,
	.unhash			= unix_unhash,
#ifdef CONFIG_BPF_SYSCALL
	.psock_update_sk_prot	= unix_stream_bpf_update_proto,
#endif
};

static struct sock *unix_create1(struct net *net, struct socket *sock, int kern, int type)
{
	struct unix_sock *u;
	struct sock *sk;
	int err;

	atomic_long_inc(&unix_nr_socks);
	if (atomic_long_read(&unix_nr_socks) > 2 * get_max_files()) {
		err = -ENFILE;
		goto err;
	}

	if (type == SOCK_STREAM)
		sk = sk_alloc(net, PF_UNIX, GFP_KERNEL, &unix_stream_proto, kern);
	else /*dgram and  seqpacket */
		sk = sk_alloc(net, PF_UNIX, GFP_KERNEL, &unix_dgram_proto, kern);

	if (!sk) {
		err = -ENOMEM;
		goto err;
	}

	sock_init_data(sock, sk);

	sk->sk_allocation	= GFP_KERNEL_ACCOUNT;
	sk->sk_write_space	= unix_write_space;
	sk->sk_max_ack_backlog	= net->unx.sysctl_max_dgram_qlen;
	sk->sk_destruct		= unix_sock_destructor;
	u	  = unix_sk(sk);
	u->path.dentry = NULL;
	u->path.mnt = NULL;
	spin_lock_init(&u->lock);
	atomic_long_set(&u->inflight, 0);
	INIT_LIST_HEAD(&u->link);
	mutex_init(&u->iolock); /* single task reading lock */
	mutex_init(&u->bindlock); /* single task binding lock */
	init_waitqueue_head(&u->peer_wait);
	init_waitqueue_func_entry(&u->peer_wake, unix_dgram_peer_wake_relay);
	memset(&u->scm_stat, 0, sizeof(struct scm_stat));
	unix_insert_socket(unix_sockets_unbound(sk), sk);

	local_bh_disable();
	sock_prot_inuse_add(sock_net(sk), sk->sk_prot, 1);
	local_bh_enable();

	return sk;

err:
	atomic_long_dec(&unix_nr_socks);
	return ERR_PTR(err);
}

static int unix_create(struct net *net, struct socket *sock, int protocol,
		       int kern)
{
	struct sock *sk;

	if (protocol && protocol != PF_UNIX)
		return -EPROTONOSUPPORT;

	sock->state = SS_UNCONNECTED;

	switch (sock->type) {
	case SOCK_STREAM:
		sock->ops = &unix_stream_ops;
		break;
		/*
		 *	Believe it or not BSD has AF_UNIX, SOCK_RAW though
		 *	nothing uses it.
		 */
	case SOCK_RAW:
		sock->type = SOCK_DGRAM;
		fallthrough;
	case SOCK_DGRAM:
		sock->ops = &unix_dgram_ops;
		break;
	case SOCK_SEQPACKET:
		sock->ops = &unix_seqpacket_ops;
		break;
	default:
		return -ESOCKTNOSUPPORT;
	}

	sk = unix_create1(net, sock, kern, sock->type);
	if (IS_ERR(sk))
		return PTR_ERR(sk);

	return 0;
}

static int unix_release(struct socket *sock)
{
	struct sock *sk = sock->sk;

	if (!sk)
		return 0;

	sk->sk_prot->close(sk, 0);
	unix_release_sock(sk, 0);
	sock->sk = NULL;

	return 0;
}

static int unix_autobind(struct socket *sock)
{
	struct sock *sk = sock->sk;
	struct net *net = sock_net(sk);
	struct unix_sock *u = unix_sk(sk);
	static u32 ordernum = 1;
	struct unix_address *addr;
	int err;
	unsigned int retries = 0;

	err = mutex_lock_interruptible(&u->bindlock);
	if (err)
		return err;

	if (u->addr)
		goto out;

	err = -ENOMEM;
	addr = kzalloc(sizeof(*addr) + sizeof(short) + 16, GFP_KERNEL);
	if (!addr)
		goto out;

	addr->name->sun_family = AF_UNIX;
	refcount_set(&addr->refcnt, 1);

retry:
	addr->len = sprintf(addr->name->sun_path+1, "%05x", ordernum) + 1 + sizeof(short);
	addr->hash = unix_hash_fold(csum_partial(addr->name, addr->len, 0));
	addr->hash ^= sk->sk_type;

	spin_lock(&unix_table_lock);
	ordernum = (ordernum+1)&0xFFFFF;

	if (__unix_find_socket_byname(net, addr->name, addr->len, addr->hash)) {
		spin_unlock(&unix_table_lock);
		/*
		 * __unix_find_socket_byname() may take long time if many names
		 * are already in use.
		 */
		cond_resched();
		/* Give up if all names seems to be in use. */
		if (retries++ == 0xFFFFF) {
			err = -ENOSPC;
			kfree(addr);
			goto out;
		}
		goto retry;
	}

	__unix_set_addr(sk, addr, addr->hash);
	spin_unlock(&unix_table_lock);
	err = 0;

out:	mutex_unlock(&u->bindlock);
	return err;
}

static struct sock *unix_find_other(struct net *net,
				    struct sockaddr_un *sunname, int len,
				    int type, unsigned int hash, int *error)
{
	struct sock *u;
	struct path path;
	int err = 0;

	if (sunname->sun_path[0]) {
		struct inode *inode;
		err = kern_path(sunname->sun_path, LOOKUP_FOLLOW, &path);
		if (err)
			goto fail;
		inode = d_backing_inode(path.dentry);
		err = path_permission(&path, MAY_WRITE);
		if (err)
			goto put_fail;

		err = -ECONNREFUSED;
		if (!S_ISSOCK(inode->i_mode))
			goto put_fail;
		u = unix_find_socket_byinode(inode);
		if (!u)
			goto put_fail;

		if (u->sk_type == type)
			touch_atime(&path);

		path_put(&path);

		err = -EPROTOTYPE;
		if (u->sk_type != type) {
			sock_put(u);
			goto fail;
		}
	} else {
		err = -ECONNREFUSED;
		u = unix_find_socket_byname(net, sunname, len, type ^ hash);
		if (u) {
			struct dentry *dentry;
			dentry = unix_sk(u)->path.dentry;
			if (dentry)
				touch_atime(&unix_sk(u)->path);
		} else
			goto fail;
	}
	return u;

put_fail:
	path_put(&path);
fail:
	*error = err;
	return NULL;
}

static int unix_bind_bsd(struct sock *sk, struct unix_address *addr)
{
	struct unix_sock *u = unix_sk(sk);
	umode_t mode = S_IFSOCK |
	       (SOCK_INODE(sk->sk_socket)->i_mode & ~current_umask());
	struct user_namespace *ns; // barf...
	struct path parent;
	struct dentry *dentry;
	unsigned int hash;
	int err;

	/*
	 * Get the parent directory, calculate the hash for last
	 * component.
	 */
	dentry = kern_path_create(AT_FDCWD, addr->name->sun_path, &parent, 0);
	if (IS_ERR(dentry))
		return PTR_ERR(dentry);
	ns = mnt_user_ns(parent.mnt);

	/*
	 * All right, let's create it.
	 */
	err = security_path_mknod(&parent, dentry, mode, 0);
	if (!err)
		err = vfs_mknod(ns, d_inode(parent.dentry), dentry, mode, 0);
	if (err)
		goto out;
	err = mutex_lock_interruptible(&u->bindlock);
	if (err)
		goto out_unlink;
	if (u->addr)
		goto out_unlock;

	addr->hash = UNIX_HASH_SIZE;
	hash = d_backing_inode(dentry)->i_ino & (UNIX_HASH_SIZE - 1);
	spin_lock(&unix_table_lock);
	u->path.mnt = mntget(parent.mnt);
	u->path.dentry = dget(dentry);
	__unix_set_addr(sk, addr, hash);
	spin_unlock(&unix_table_lock);
	mutex_unlock(&u->bindlock);
	done_path_create(&parent, dentry);
	return 0;

out_unlock:
	mutex_unlock(&u->bindlock);
	err = -EINVAL;
out_unlink:
	/* failed after successful mknod?  unlink what we'd created... */
	vfs_unlink(ns, d_inode(parent.dentry), dentry, NULL);
out:
	done_path_create(&parent, dentry);
	return err;
}

static int unix_bind_abstract(struct sock *sk, struct unix_address *addr)
{
	struct unix_sock *u = unix_sk(sk);
	int err;

	err = mutex_lock_interruptible(&u->bindlock);
	if (err)
		return err;

	if (u->addr) {
		mutex_unlock(&u->bindlock);
		return -EINVAL;
	}

	spin_lock(&unix_table_lock);
	if (__unix_find_socket_byname(sock_net(sk), addr->name, addr->len,
				      addr->hash)) {
		spin_unlock(&unix_table_lock);
		mutex_unlock(&u->bindlock);
		return -EADDRINUSE;
	}
	__unix_set_addr(sk, addr, addr->hash);
	spin_unlock(&unix_table_lock);
	mutex_unlock(&u->bindlock);
	return 0;
}

static int unix_bind(struct socket *sock, struct sockaddr *uaddr, int addr_len)
{
	struct sock *sk = sock->sk;
	struct sockaddr_un *sunaddr = (struct sockaddr_un *)uaddr;
	char *sun_path = sunaddr->sun_path;
	int err;
	unsigned int hash;
	struct unix_address *addr;

	if (addr_len < offsetofend(struct sockaddr_un, sun_family) ||
	    sunaddr->sun_family != AF_UNIX)
		return -EINVAL;

	if (addr_len == sizeof(short))
		return unix_autobind(sock);

	err = unix_mkname(sunaddr, addr_len, &hash);
	if (err < 0)
		return err;
	addr_len = err;
	addr = kmalloc(sizeof(*addr)+addr_len, GFP_KERNEL);
	if (!addr)
		return -ENOMEM;

	memcpy(addr->name, sunaddr, addr_len);
	addr->len = addr_len;
	addr->hash = hash ^ sk->sk_type;
	refcount_set(&addr->refcnt, 1);

	if (sun_path[0])
		err = unix_bind_bsd(sk, addr);
	else
		err = unix_bind_abstract(sk, addr);
	if (err)
		unix_release_addr(addr);
	return err == -EEXIST ? -EADDRINUSE : err;
}

static void unix_state_double_lock(struct sock *sk1, struct sock *sk2)
{
	if (unlikely(sk1 == sk2) || !sk2) {
		unix_state_lock(sk1);
		return;
	}
	if (sk1 < sk2) {
		unix_state_lock(sk1);
		unix_state_lock_nested(sk2);
	} else {
		unix_state_lock(sk2);
		unix_state_lock_nested(sk1);
	}
}

static void unix_state_double_unlock(struct sock *sk1, struct sock *sk2)
{
	if (unlikely(sk1 == sk2) || !sk2) {
		unix_state_unlock(sk1);
		return;
	}
	unix_state_unlock(sk1);
	unix_state_unlock(sk2);
}

static int unix_dgram_connect(struct socket *sock, struct sockaddr *addr,
			      int alen, int flags)
{
	struct sock *sk = sock->sk;
	struct net *net = sock_net(sk);
	struct sockaddr_un *sunaddr = (struct sockaddr_un *)addr;
	struct sock *other;
	unsigned int hash;
	int err;

	err = -EINVAL;
	if (alen < offsetofend(struct sockaddr, sa_family))
		goto out;

	if (addr->sa_family != AF_UNSPEC) {
		err = unix_mkname(sunaddr, alen, &hash);
		if (err < 0)
			goto out;
		alen = err;

		if (test_bit(SOCK_PASSCRED, &sock->flags) &&
		    !unix_sk(sk)->addr && (err = unix_autobind(sock)) != 0)
			goto out;

restart:
		other = unix_find_other(net, sunaddr, alen, sock->type, hash, &err);
		if (!other)
			goto out;

		unix_state_double_lock(sk, other);

		/* Apparently VFS overslept socket death. Retry. */
		if (sock_flag(other, SOCK_DEAD)) {
			unix_state_double_unlock(sk, other);
			sock_put(other);
			goto restart;
		}

		err = -EPERM;
		if (!unix_may_send(sk, other))
			goto out_unlock;

		err = security_unix_may_send(sk->sk_socket, other->sk_socket);
		if (err)
			goto out_unlock;

		sk->sk_state = other->sk_state = TCP_ESTABLISHED;
	} else {
		/*
		 *	1003.1g breaking connected state with AF_UNSPEC
		 */
		other = NULL;
		unix_state_double_lock(sk, other);
	}

	/*
	 * If it was connected, reconnect.
	 */
	if (unix_peer(sk)) {
		struct sock *old_peer = unix_peer(sk);

		unix_peer(sk) = other;
		if (!other)
			sk->sk_state = TCP_CLOSE;
		unix_dgram_peer_wake_disconnect_wakeup(sk, old_peer);

		unix_state_double_unlock(sk, other);

		if (other != old_peer)
			unix_dgram_disconnected(sk, old_peer);
		sock_put(old_peer);
	} else {
		unix_peer(sk) = other;
		unix_state_double_unlock(sk, other);
	}

	return 0;

out_unlock:
	unix_state_double_unlock(sk, other);
	sock_put(other);
out:
	return err;
}

static long unix_wait_for_peer(struct sock *other, long timeo)
	__releases(&unix_sk(other)->lock)
{
	struct unix_sock *u = unix_sk(other);
	int sched;
	DEFINE_WAIT(wait);

	prepare_to_wait_exclusive(&u->peer_wait, &wait, TASK_INTERRUPTIBLE);

	sched = !sock_flag(other, SOCK_DEAD) &&
		!(other->sk_shutdown & RCV_SHUTDOWN) &&
		unix_recvq_full(other);

	unix_state_unlock(other);

	if (sched)
		timeo = schedule_timeout(timeo);

	finish_wait(&u->peer_wait, &wait);
	return timeo;
}

static int unix_stream_connect(struct socket *sock, struct sockaddr *uaddr,
			       int addr_len, int flags)
{
	struct sockaddr_un *sunaddr = (struct sockaddr_un *)uaddr;
	struct sock *sk = sock->sk;
	struct net *net = sock_net(sk);
	struct unix_sock *u = unix_sk(sk), *newu, *otheru;
	struct sock *newsk = NULL;
	struct sock *other = NULL;
	struct sk_buff *skb = NULL;
	unsigned int hash;
	int st;
	int err;
	long timeo;

	err = unix_mkname(sunaddr, addr_len, &hash);
	if (err < 0)
		goto out;
	addr_len = err;

	if (test_bit(SOCK_PASSCRED, &sock->flags) && !u->addr &&
	    (err = unix_autobind(sock)) != 0)
		goto out;

	timeo = sock_sndtimeo(sk, flags & O_NONBLOCK);

	/* First of all allocate resources.
	   If we will make it after state is locked,
	   we will have to recheck all again in any case.
	 */

	/* create new sock for complete connection */
	newsk = unix_create1(sock_net(sk), NULL, 0, sock->type);
	if (IS_ERR(newsk)) {
		err = PTR_ERR(newsk);
		newsk = NULL;
		goto out;
	}

	err = -ENOMEM;

	/* Allocate skb for sending to listening sock */
	skb = sock_wmalloc(newsk, 1, 0, GFP_KERNEL);
	if (skb == NULL)
		goto out;

restart:
	/*  Find listening sock. */
	other = unix_find_other(net, sunaddr, addr_len, sk->sk_type, hash, &err);
	if (!other)
		goto out;

	/* Latch state of peer */
	unix_state_lock(other);

	/* Apparently VFS overslept socket death. Retry. */
	if (sock_flag(other, SOCK_DEAD)) {
		unix_state_unlock(other);
		sock_put(other);
		goto restart;
	}

	err = -ECONNREFUSED;
	if (other->sk_state != TCP_LISTEN)
		goto out_unlock;
	if (other->sk_shutdown & RCV_SHUTDOWN)
		goto out_unlock;

	if (unix_recvq_full(other)) {
		err = -EAGAIN;
		if (!timeo)
			goto out_unlock;

		timeo = unix_wait_for_peer(other, timeo);

		err = sock_intr_errno(timeo);
		if (signal_pending(current))
			goto out;
		sock_put(other);
		goto restart;
	}

	/* Latch our state.

	   It is tricky place. We need to grab our state lock and cannot
	   drop lock on peer. It is dangerous because deadlock is
	   possible. Connect to self case and simultaneous
	   attempt to connect are eliminated by checking socket
	   state. other is TCP_LISTEN, if sk is TCP_LISTEN we
	   check this before attempt to grab lock.

	   Well, and we have to recheck the state after socket locked.
	 */
	st = sk->sk_state;

	switch (st) {
	case TCP_CLOSE:
		/* This is ok... continue with connect */
		break;
	case TCP_ESTABLISHED:
		/* Socket is already connected */
		err = -EISCONN;
		goto out_unlock;
	default:
		err = -EINVAL;
		goto out_unlock;
	}

	unix_state_lock_nested(sk);

	if (sk->sk_state != st) {
		unix_state_unlock(sk);
		unix_state_unlock(other);
		sock_put(other);
		goto restart;
	}

	err = security_unix_stream_connect(sk, other, newsk);
	if (err) {
		unix_state_unlock(sk);
		goto out_unlock;
	}

	/* The way is open! Fastly set all the necessary fields... */

	sock_hold(sk);
	unix_peer(newsk)	= sk;
	newsk->sk_state		= TCP_ESTABLISHED;
	newsk->sk_type		= sk->sk_type;
	init_peercred(newsk);
	newu = unix_sk(newsk);
	RCU_INIT_POINTER(newsk->sk_wq, &newu->peer_wq);
	otheru = unix_sk(other);

	/* copy address information from listening to new sock
	 *
	 * The contents of *(otheru->addr) and otheru->path
	 * are seen fully set up here, since we have found
	 * otheru in hash under unix_table_lock.  Insertion
	 * into the hash chain we'd found it in had been done
	 * in an earlier critical area protected by unix_table_lock,
	 * the same one where we'd set *(otheru->addr) contents,
	 * as well as otheru->path and otheru->addr itself.
	 *
	 * Using smp_store_release() here to set newu->addr
	 * is enough to make those stores, as well as stores
	 * to newu->path visible to anyone who gets newu->addr
	 * by smp_load_acquire().  IOW, the same warranties
	 * as for unix_sock instances bound in unix_bind() or
	 * in unix_autobind().
	 */
	if (otheru->path.dentry) {
		path_get(&otheru->path);
		newu->path = otheru->path;
	}
	refcount_inc(&otheru->addr->refcnt);
	smp_store_release(&newu->addr, otheru->addr);

	/* Set credentials */
	copy_peercred(sk, other);

	sock->state	= SS_CONNECTED;
	sk->sk_state	= TCP_ESTABLISHED;
	sock_hold(newsk);

	smp_mb__after_atomic();	/* sock_hold() does an atomic_inc() */
	unix_peer(sk)	= newsk;

	unix_state_unlock(sk);

	/* take ten and send info to listening sock */
	spin_lock(&other->sk_receive_queue.lock);
	__skb_queue_tail(&other->sk_receive_queue, skb);
	spin_unlock(&other->sk_receive_queue.lock);
	unix_state_unlock(other);
	other->sk_data_ready(other);
	sock_put(other);
	return 0;

out_unlock:
	if (other)
		unix_state_unlock(other);

out:
	kfree_skb(skb);
	if (newsk)
		unix_release_sock(newsk, 0);
	if (other)
		sock_put(other);
	return err;
}

static int unix_socketpair(struct socket *socka, struct socket *sockb)
{
	struct sock *ska = socka->sk, *skb = sockb->sk;

	/* Join our sockets back to back */
	sock_hold(ska);
	sock_hold(skb);
	unix_peer(ska) = skb;
	unix_peer(skb) = ska;
	init_peercred(ska);
	init_peercred(skb);

	ska->sk_state = TCP_ESTABLISHED;
	skb->sk_state = TCP_ESTABLISHED;
	socka->state  = SS_CONNECTED;
	sockb->state  = SS_CONNECTED;
	return 0;
}

static void unix_sock_inherit_flags(const struct socket *old,
				    struct socket *new)
{
	if (test_bit(SOCK_PASSCRED, &old->flags))
		set_bit(SOCK_PASSCRED, &new->flags);
	if (test_bit(SOCK_PASSSEC, &old->flags))
		set_bit(SOCK_PASSSEC, &new->flags);
}

static int unix_accept(struct socket *sock, struct socket *newsock, int flags,
		       bool kern)
{
	struct sock *sk = sock->sk;
	struct sock *tsk;
	struct sk_buff *skb;
	int err;

	err = -EOPNOTSUPP;
	if (sock->type != SOCK_STREAM && sock->type != SOCK_SEQPACKET)
		goto out;

	err = -EINVAL;
	if (sk->sk_state != TCP_LISTEN)
		goto out;

	/* If socket state is TCP_LISTEN it cannot change (for now...),
	 * so that no locks are necessary.
	 */

	skb = skb_recv_datagram(sk, 0, flags&O_NONBLOCK, &err);
	if (!skb) {
		/* This means receive shutdown. */
		if (err == 0)
			err = -EINVAL;
		goto out;
	}

	tsk = skb->sk;
	skb_free_datagram(sk, skb);
	wake_up_interruptible(&unix_sk(sk)->peer_wait);

	/* attach accepted sock to socket */
	unix_state_lock(tsk);
	newsock->state = SS_CONNECTED;
	unix_sock_inherit_flags(sock, newsock);
	sock_graft(tsk, newsock);
	unix_state_unlock(tsk);
	return 0;

out:
	return err;
}


static int unix_getname(struct socket *sock, struct sockaddr *uaddr, int peer)
{
	struct sock *sk = sock->sk;
	struct unix_address *addr;
	DECLARE_SOCKADDR(struct sockaddr_un *, sunaddr, uaddr);
	int err = 0;

	if (peer) {
		sk = unix_peer_get(sk);

		err = -ENOTCONN;
		if (!sk)
			goto out;
		err = 0;
	} else {
		sock_hold(sk);
	}

	addr = smp_load_acquire(&unix_sk(sk)->addr);
	if (!addr) {
		sunaddr->sun_family = AF_UNIX;
		sunaddr->sun_path[0] = 0;
		err = sizeof(short);
	} else {
		err = addr->len;
		memcpy(sunaddr, addr->name, addr->len);
	}
	sock_put(sk);
out:
	return err;
}

static void unix_peek_fds(struct scm_cookie *scm, struct sk_buff *skb)
{
	scm->fp = scm_fp_dup(UNIXCB(skb).fp);

	/*
	 * Garbage collection of unix sockets starts by selecting a set of
	 * candidate sockets which have reference only from being in flight
	 * (total_refs == inflight_refs).  This condition is checked once during
	 * the candidate collection phase, and candidates are marked as such, so
	 * that non-candidates can later be ignored.  While inflight_refs is
	 * protected by unix_gc_lock, total_refs (file count) is not, hence this
	 * is an instantaneous decision.
	 *
	 * Once a candidate, however, the socket must not be reinstalled into a
	 * file descriptor while the garbage collection is in progress.
	 *
	 * If the above conditions are met, then the directed graph of
	 * candidates (*) does not change while unix_gc_lock is held.
	 *
	 * Any operations that changes the file count through file descriptors
	 * (dup, close, sendmsg) does not change the graph since candidates are
	 * not installed in fds.
	 *
	 * Dequeing a candidate via recvmsg would install it into an fd, but
	 * that takes unix_gc_lock to decrement the inflight count, so it's
	 * serialized with garbage collection.
	 *
	 * MSG_PEEK is special in that it does not change the inflight count,
	 * yet does install the socket into an fd.  The following lock/unlock
	 * pair is to ensure serialization with garbage collection.  It must be
	 * done between incrementing the file count and installing the file into
	 * an fd.
	 *
	 * If garbage collection starts after the barrier provided by the
	 * lock/unlock, then it will see the elevated refcount and not mark this
	 * as a candidate.  If a garbage collection is already in progress
	 * before the file count was incremented, then the lock/unlock pair will
	 * ensure that garbage collection is finished before progressing to
	 * installing the fd.
	 *
	 * (*) A -> B where B is on the queue of A or B is on the queue of C
	 * which is on the queue of listening socket A.
	 */
	spin_lock(&unix_gc_lock);
	spin_unlock(&unix_gc_lock);
}

static int unix_scm_to_skb(struct scm_cookie *scm, struct sk_buff *skb, bool send_fds)
{
	int err = 0;

	UNIXCB(skb).pid  = get_pid(scm->pid);
	UNIXCB(skb).uid = scm->creds.uid;
	UNIXCB(skb).gid = scm->creds.gid;
	UNIXCB(skb).fp = NULL;
	unix_get_secdata(scm, skb);
	if (scm->fp && send_fds)
		err = unix_attach_fds(scm, skb);

	skb->destructor = unix_destruct_scm;
	return err;
}

static bool unix_passcred_enabled(const struct socket *sock,
				  const struct sock *other)
{
	return test_bit(SOCK_PASSCRED, &sock->flags) ||
	       !other->sk_socket ||
	       test_bit(SOCK_PASSCRED, &other->sk_socket->flags);
}

/*
 * Some apps rely on write() giving SCM_CREDENTIALS
 * We include credentials if source or destination socket
 * asserted SOCK_PASSCRED.
 */
static void maybe_add_creds(struct sk_buff *skb, const struct socket *sock,
			    const struct sock *other)
{
	if (UNIXCB(skb).pid)
		return;
	if (unix_passcred_enabled(sock, other)) {
		UNIXCB(skb).pid  = get_pid(task_tgid(current));
		current_uid_gid(&UNIXCB(skb).uid, &UNIXCB(skb).gid);
	}
}

static int maybe_init_creds(struct scm_cookie *scm,
			    struct socket *socket,
			    const struct sock *other)
{
	int err;
	struct msghdr msg = { .msg_controllen = 0 };

	err = scm_send(socket, &msg, scm, false);
	if (err)
		return err;

	if (unix_passcred_enabled(socket, other)) {
		scm->pid = get_pid(task_tgid(current));
		current_uid_gid(&scm->creds.uid, &scm->creds.gid);
	}
	return err;
}

static bool unix_skb_scm_eq(struct sk_buff *skb,
			    struct scm_cookie *scm)
{
	const struct unix_skb_parms *u = &UNIXCB(skb);

	return u->pid == scm->pid &&
	       uid_eq(u->uid, scm->creds.uid) &&
	       gid_eq(u->gid, scm->creds.gid) &&
	       unix_secdata_eq(scm, skb);
}

static void scm_stat_add(struct sock *sk, struct sk_buff *skb)
{
	struct scm_fp_list *fp = UNIXCB(skb).fp;
	struct unix_sock *u = unix_sk(sk);

	if (unlikely(fp && fp->count))
		atomic_add(fp->count, &u->scm_stat.nr_fds);
}

static void scm_stat_del(struct sock *sk, struct sk_buff *skb)
{
	struct scm_fp_list *fp = UNIXCB(skb).fp;
	struct unix_sock *u = unix_sk(sk);

	if (unlikely(fp && fp->count))
		atomic_sub(fp->count, &u->scm_stat.nr_fds);
}

/*
 *	Send AF_UNIX data.
 */

static int unix_dgram_sendmsg(struct socket *sock, struct msghdr *msg,
			      size_t len)
{
	struct sock *sk = sock->sk;
	struct net *net = sock_net(sk);
	struct unix_sock *u = unix_sk(sk);
	DECLARE_SOCKADDR(struct sockaddr_un *, sunaddr, msg->msg_name);
	struct sock *other = NULL;
	int namelen = 0; /* fake GCC */
	int err;
	unsigned int hash;
	struct sk_buff *skb;
	long timeo;
	struct scm_cookie scm;
	int data_len = 0;
	int sk_locked;

	wait_for_unix_gc();
	err = scm_send(sock, msg, &scm, false);
	if (err < 0)
		return err;

	err = -EOPNOTSUPP;
	if (msg->msg_flags&MSG_OOB)
		goto out;

	if (msg->msg_namelen) {
		err = unix_mkname(sunaddr, msg->msg_namelen, &hash);
		if (err < 0)
			goto out;
		namelen = err;
	} else {
		sunaddr = NULL;
		err = -ENOTCONN;
		other = unix_peer_get(sk);
		if (!other)
			goto out;
	}

	if (test_bit(SOCK_PASSCRED, &sock->flags) && !u->addr
	    && (err = unix_autobind(sock)) != 0)
		goto out;

	err = -EMSGSIZE;
	if (len > sk->sk_sndbuf - 32)
		goto out;

	if (len > SKB_MAX_ALLOC) {
		data_len = min_t(size_t,
				 len - SKB_MAX_ALLOC,
				 MAX_SKB_FRAGS * PAGE_SIZE);
		data_len = PAGE_ALIGN(data_len);

		BUILD_BUG_ON(SKB_MAX_ALLOC < PAGE_SIZE);
	}

	skb = sock_alloc_send_pskb(sk, len - data_len, data_len,
				   msg->msg_flags & MSG_DONTWAIT, &err,
				   PAGE_ALLOC_COSTLY_ORDER);
	if (skb == NULL)
		goto out;

	err = unix_scm_to_skb(&scm, skb, true);
	if (err < 0)
		goto out_free;

	skb_put(skb, len - data_len);
	skb->data_len = data_len;
	skb->len = len;
	err = skb_copy_datagram_from_iter(skb, 0, &msg->msg_iter, len);
	if (err)
		goto out_free;

	timeo = sock_sndtimeo(sk, msg->msg_flags & MSG_DONTWAIT);

restart:
	if (!other) {
		err = -ECONNRESET;
		if (sunaddr == NULL)
			goto out_free;

		other = unix_find_other(net, sunaddr, namelen, sk->sk_type,
					hash, &err);
		if (other == NULL)
			goto out_free;
	}

	if (sk_filter(other, skb) < 0) {
		/* Toss the packet but do not return any error to the sender */
		err = len;
		goto out_free;
	}

	sk_locked = 0;
	unix_state_lock(other);
restart_locked:
	err = -EPERM;
	if (!unix_may_send(sk, other))
		goto out_unlock;

	if (unlikely(sock_flag(other, SOCK_DEAD))) {
		/*
		 *	Check with 1003.1g - what should
		 *	datagram error
		 */
		unix_state_unlock(other);
		sock_put(other);

		if (!sk_locked)
			unix_state_lock(sk);

		err = 0;
		if (unix_peer(sk) == other) {
			unix_peer(sk) = NULL;
			unix_dgram_peer_wake_disconnect_wakeup(sk, other);

			unix_state_unlock(sk);

			sk->sk_state = TCP_CLOSE;
			unix_dgram_disconnected(sk, other);
			sock_put(other);
			err = -ECONNREFUSED;
		} else {
			unix_state_unlock(sk);
		}

		other = NULL;
		if (err)
			goto out_free;
		goto restart;
	}

	err = -EPIPE;
	if (other->sk_shutdown & RCV_SHUTDOWN)
		goto out_unlock;

	if (sk->sk_type != SOCK_SEQPACKET) {
		err = security_unix_may_send(sk->sk_socket, other->sk_socket);
		if (err)
			goto out_unlock;
	}

	/* other == sk && unix_peer(other) != sk if
	 * - unix_peer(sk) == NULL, destination address bound to sk
	 * - unix_peer(sk) == sk by time of get but disconnected before lock
	 */
	if (other != sk &&
	    unlikely(unix_peer(other) != sk &&
	    unix_recvq_full_lockless(other))) {
		if (timeo) {
			timeo = unix_wait_for_peer(other, timeo);

			err = sock_intr_errno(timeo);
			if (signal_pending(current))
				goto out_free;

			goto restart;
		}

		if (!sk_locked) {
			unix_state_unlock(other);
			unix_state_double_lock(sk, other);
		}

		if (unix_peer(sk) != other ||
		    unix_dgram_peer_wake_me(sk, other)) {
			err = -EAGAIN;
			sk_locked = 1;
			goto out_unlock;
		}

		if (!sk_locked) {
			sk_locked = 1;
			goto restart_locked;
		}
	}

	if (unlikely(sk_locked))
		unix_state_unlock(sk);

	if (sock_flag(other, SOCK_RCVTSTAMP))
		__net_timestamp(skb);
	maybe_add_creds(skb, sock, other);
	scm_stat_add(other, skb);
	skb_queue_tail(&other->sk_receive_queue, skb);
	unix_state_unlock(other);
	other->sk_data_ready(other);
	sock_put(other);
	scm_destroy(&scm);
	return len;

out_unlock:
	if (sk_locked)
		unix_state_unlock(sk);
	unix_state_unlock(other);
out_free:
	kfree_skb(skb);
out:
	if (other)
		sock_put(other);
	scm_destroy(&scm);
	return err;
}

/* We use paged skbs for stream sockets, and limit occupancy to 32768
 * bytes, and a minimum of a full page.
 */
#define UNIX_SKB_FRAGS_SZ (PAGE_SIZE << get_order(32768))

#if (IS_ENABLED(CONFIG_AF_UNIX_OOB))
static int queue_oob(struct socket *sock, struct msghdr *msg, struct sock *other)
{
	struct unix_sock *ousk = unix_sk(other);
	struct sk_buff *skb;
	int err = 0;

	skb = sock_alloc_send_skb(sock->sk, 1, msg->msg_flags & MSG_DONTWAIT, &err);

	if (!skb)
		return err;

	skb_put(skb, 1);
	err = skb_copy_datagram_from_iter(skb, 0, &msg->msg_iter, 1);

	if (err) {
		kfree_skb(skb);
		return err;
	}

	unix_state_lock(other);

	if (sock_flag(other, SOCK_DEAD) ||
	    (other->sk_shutdown & RCV_SHUTDOWN)) {
		unix_state_unlock(other);
		kfree_skb(skb);
		return -EPIPE;
	}

	maybe_add_creds(skb, sock, other);
	skb_get(skb);

	if (ousk->oob_skb)
		consume_skb(ousk->oob_skb);

	ousk->oob_skb = skb;

	scm_stat_add(other, skb);
	skb_queue_tail(&other->sk_receive_queue, skb);
	sk_send_sigurg(other);
	unix_state_unlock(other);
	other->sk_data_ready(other);

	return err;
}
#endif

static int unix_stream_sendmsg(struct socket *sock, struct msghdr *msg,
			       size_t len)
{
	struct sock *sk = sock->sk;
	struct sock *other = NULL;
	int err, size;
	struct sk_buff *skb;
	int sent = 0;
	struct scm_cookie scm;
	bool fds_sent = false;
	int data_len;

	wait_for_unix_gc();
	err = scm_send(sock, msg, &scm, false);
	if (err < 0)
		return err;

	err = -EOPNOTSUPP;
	if (msg->msg_flags & MSG_OOB) {
#if (IS_ENABLED(CONFIG_AF_UNIX_OOB))
		if (len)
			len--;
		else
#endif
			goto out_err;
	}

	if (msg->msg_namelen) {
		err = sk->sk_state == TCP_ESTABLISHED ? -EISCONN : -EOPNOTSUPP;
		goto out_err;
	} else {
		err = -ENOTCONN;
		other = unix_peer(sk);
		if (!other)
			goto out_err;
	}

	if (sk->sk_shutdown & SEND_SHUTDOWN)
		goto pipe_err;

	while (sent < len) {
		size = len - sent;

		/* Keep two messages in the pipe so it schedules better */
		size = min_t(int, size, (sk->sk_sndbuf >> 1) - 64);

		/* allow fallback to order-0 allocations */
		size = min_t(int, size, SKB_MAX_HEAD(0) + UNIX_SKB_FRAGS_SZ);

		data_len = max_t(int, 0, size - SKB_MAX_HEAD(0));

		data_len = min_t(size_t, size, PAGE_ALIGN(data_len));

		skb = sock_alloc_send_pskb(sk, size - data_len, data_len,
					   msg->msg_flags & MSG_DONTWAIT, &err,
					   get_order(UNIX_SKB_FRAGS_SZ));
		if (!skb)
			goto out_err;

		/* Only send the fds in the first buffer */
		err = unix_scm_to_skb(&scm, skb, !fds_sent);
		if (err < 0) {
			kfree_skb(skb);
			goto out_err;
		}
		fds_sent = true;

		skb_put(skb, size - data_len);
		skb->data_len = data_len;
		skb->len = size;
		err = skb_copy_datagram_from_iter(skb, 0, &msg->msg_iter, size);
		if (err) {
			kfree_skb(skb);
			goto out_err;
		}

		unix_state_lock(other);

		if (sock_flag(other, SOCK_DEAD) ||
		    (other->sk_shutdown & RCV_SHUTDOWN))
			goto pipe_err_free;

		maybe_add_creds(skb, sock, other);
		scm_stat_add(other, skb);
		skb_queue_tail(&other->sk_receive_queue, skb);
		unix_state_unlock(other);
		other->sk_data_ready(other);
		sent += size;
	}

#if (IS_ENABLED(CONFIG_AF_UNIX_OOB))
	if (msg->msg_flags & MSG_OOB) {
		err = queue_oob(sock, msg, other);
		if (err)
			goto out_err;
		sent++;
	}
#endif

	scm_destroy(&scm);

	return sent;

pipe_err_free:
	unix_state_unlock(other);
	kfree_skb(skb);
pipe_err:
	if (sent == 0 && !(msg->msg_flags&MSG_NOSIGNAL))
		send_sig(SIGPIPE, current, 0);
	err = -EPIPE;
out_err:
	scm_destroy(&scm);
	return sent ? : err;
}

static ssize_t unix_stream_sendpage(struct socket *socket, struct page *page,
				    int offset, size_t size, int flags)
{
	int err;
	bool send_sigpipe = false;
	bool init_scm = true;
	struct scm_cookie scm;
	struct sock *other, *sk = socket->sk;
	struct sk_buff *skb, *newskb = NULL, *tail = NULL;

	if (flags & MSG_OOB)
		return -EOPNOTSUPP;

	other = unix_peer(sk);
	if (!other || sk->sk_state != TCP_ESTABLISHED)
		return -ENOTCONN;

	if (false) {
alloc_skb:
		unix_state_unlock(other);
		mutex_unlock(&unix_sk(other)->iolock);
		newskb = sock_alloc_send_pskb(sk, 0, 0, flags & MSG_DONTWAIT,
					      &err, 0);
		if (!newskb)
			goto err;
	}

	/* we must acquire iolock as we modify already present
	 * skbs in the sk_receive_queue and mess with skb->len
	 */
	err = mutex_lock_interruptible(&unix_sk(other)->iolock);
	if (err) {
		err = flags & MSG_DONTWAIT ? -EAGAIN : -ERESTARTSYS;
		goto err;
	}

	if (sk->sk_shutdown & SEND_SHUTDOWN) {
		err = -EPIPE;
		send_sigpipe = true;
		goto err_unlock;
	}

	unix_state_lock(other);

	if (sock_flag(other, SOCK_DEAD) ||
	    other->sk_shutdown & RCV_SHUTDOWN) {
		err = -EPIPE;
		send_sigpipe = true;
		goto err_state_unlock;
	}

	if (init_scm) {
		err = maybe_init_creds(&scm, socket, other);
		if (err)
			goto err_state_unlock;
		init_scm = false;
	}

	skb = skb_peek_tail(&other->sk_receive_queue);
	if (tail && tail == skb) {
		skb = newskb;
	} else if (!skb || !unix_skb_scm_eq(skb, &scm)) {
		if (newskb) {
			skb = newskb;
		} else {
			tail = skb;
			goto alloc_skb;
		}
	} else if (newskb) {
		/* this is fast path, we don't necessarily need to
		 * call to kfree_skb even though with newskb == NULL
		 * this - does no harm
		 */
		consume_skb(newskb);
		newskb = NULL;
	}

	if (skb_append_pagefrags(skb, page, offset, size)) {
		tail = skb;
		goto alloc_skb;
	}

	skb->len += size;
	skb->data_len += size;
	skb->truesize += size;
	refcount_add(size, &sk->sk_wmem_alloc);

	if (newskb) {
		err = unix_scm_to_skb(&scm, skb, false);
		if (err)
			goto err_state_unlock;
		spin_lock(&other->sk_receive_queue.lock);
		__skb_queue_tail(&other->sk_receive_queue, newskb);
		spin_unlock(&other->sk_receive_queue.lock);
	}

	unix_state_unlock(other);
	mutex_unlock(&unix_sk(other)->iolock);

	other->sk_data_ready(other);
	scm_destroy(&scm);
	return size;

err_state_unlock:
	unix_state_unlock(other);
err_unlock:
	mutex_unlock(&unix_sk(other)->iolock);
err:
	kfree_skb(newskb);
	if (send_sigpipe && !(flags & MSG_NOSIGNAL))
		send_sig(SIGPIPE, current, 0);
	if (!init_scm)
		scm_destroy(&scm);
	return err;
}

static int unix_seqpacket_sendmsg(struct socket *sock, struct msghdr *msg,
				  size_t len)
{
	int err;
	struct sock *sk = sock->sk;

	err = sock_error(sk);
	if (err)
		return err;

	if (sk->sk_state != TCP_ESTABLISHED)
		return -ENOTCONN;

	if (msg->msg_namelen)
		msg->msg_namelen = 0;

	return unix_dgram_sendmsg(sock, msg, len);
}

static int unix_seqpacket_recvmsg(struct socket *sock, struct msghdr *msg,
				  size_t size, int flags)
{
	struct sock *sk = sock->sk;

	if (sk->sk_state != TCP_ESTABLISHED)
		return -ENOTCONN;

	return unix_dgram_recvmsg(sock, msg, size, flags);
}

static void unix_copy_addr(struct msghdr *msg, struct sock *sk)
{
	struct unix_address *addr = smp_load_acquire(&unix_sk(sk)->addr);

	if (addr) {
		msg->msg_namelen = addr->len;
		memcpy(msg->msg_name, addr->name, addr->len);
	}
}

int __unix_dgram_recvmsg(struct sock *sk, struct msghdr *msg, size_t size,
			 int flags)
{
	struct scm_cookie scm;
	struct socket *sock = sk->sk_socket;
	struct unix_sock *u = unix_sk(sk);
	struct sk_buff *skb, *last;
	long timeo;
	int skip;
	int err;

	err = -EOPNOTSUPP;
	if (flags&MSG_OOB)
		goto out;

	timeo = sock_rcvtimeo(sk, flags & MSG_DONTWAIT);

	do {
		mutex_lock(&u->iolock);

		skip = sk_peek_offset(sk, flags);
		skb = __skb_try_recv_datagram(sk, &sk->sk_receive_queue, flags,
					      &skip, &err, &last);
		if (skb) {
			if (!(flags & MSG_PEEK))
				scm_stat_del(sk, skb);
			break;
		}

		mutex_unlock(&u->iolock);

		if (err != -EAGAIN)
			break;
	} while (timeo &&
		 !__skb_wait_for_more_packets(sk, &sk->sk_receive_queue,
					      &err, &timeo, last));

	if (!skb) { /* implies iolock unlocked */
		unix_state_lock(sk);
		/* Signal EOF on disconnected non-blocking SEQPACKET socket. */
		if (sk->sk_type == SOCK_SEQPACKET && err == -EAGAIN &&
		    (sk->sk_shutdown & RCV_SHUTDOWN))
			err = 0;
		unix_state_unlock(sk);
		goto out;
	}

	if (wq_has_sleeper(&u->peer_wait))
		wake_up_interruptible_sync_poll(&u->peer_wait,
						EPOLLOUT | EPOLLWRNORM |
						EPOLLWRBAND);

	if (msg->msg_name)
		unix_copy_addr(msg, skb->sk);

	if (size > skb->len - skip)
		size = skb->len - skip;
	else if (size < skb->len - skip)
		msg->msg_flags |= MSG_TRUNC;

	err = skb_copy_datagram_msg(skb, skip, msg, size);
	if (err)
		goto out_free;

	if (sock_flag(sk, SOCK_RCVTSTAMP))
		__sock_recv_timestamp(msg, sk, skb);

	memset(&scm, 0, sizeof(scm));

	scm_set_cred(&scm, UNIXCB(skb).pid, UNIXCB(skb).uid, UNIXCB(skb).gid);
	unix_set_secdata(&scm, skb);

	if (!(flags & MSG_PEEK)) {
		if (UNIXCB(skb).fp)
			unix_detach_fds(&scm, skb);

		sk_peek_offset_bwd(sk, skb->len);
	} else {
		/* It is questionable: on PEEK we could:
		   - do not return fds - good, but too simple 8)
		   - return fds, and do not return them on read (old strategy,
		     apparently wrong)
		   - clone fds (I chose it for now, it is the most universal
		     solution)

		   POSIX 1003.1g does not actually define this clearly
		   at all. POSIX 1003.1g doesn't define a lot of things
		   clearly however!

		*/

		sk_peek_offset_fwd(sk, size);

		if (UNIXCB(skb).fp)
			unix_peek_fds(&scm, skb);
	}
	err = (flags & MSG_TRUNC) ? skb->len - skip : size;

	scm_recv(sock, msg, &scm, flags);

out_free:
	skb_free_datagram(sk, skb);
	mutex_unlock(&u->iolock);
out:
	return err;
}

static int unix_dgram_recvmsg(struct socket *sock, struct msghdr *msg, size_t size,
			      int flags)
{
	struct sock *sk = sock->sk;

#ifdef CONFIG_BPF_SYSCALL
	const struct proto *prot = READ_ONCE(sk->sk_prot);

	if (prot != &unix_dgram_proto)
		return prot->recvmsg(sk, msg, size, flags & MSG_DONTWAIT,
					    flags & ~MSG_DONTWAIT, NULL);
#endif
	return __unix_dgram_recvmsg(sk, msg, size, flags);
}

static int unix_read_sock(struct sock *sk, read_descriptor_t *desc,
			  sk_read_actor_t recv_actor)
{
	int copied = 0;

	while (1) {
		struct unix_sock *u = unix_sk(sk);
		struct sk_buff *skb;
		int used, err;

		mutex_lock(&u->iolock);
		skb = skb_recv_datagram(sk, 0, 1, &err);
		mutex_unlock(&u->iolock);
		if (!skb)
			return err;

		used = recv_actor(desc, skb, 0, skb->len);
		if (used <= 0) {
			if (!copied)
				copied = used;
			kfree_skb(skb);
			break;
		} else if (used <= skb->len) {
			copied += used;
		}

		kfree_skb(skb);
		if (!desc->count)
			break;
	}

	return copied;
}

/*
 *	Sleep until more data has arrived. But check for races..
 */
static long unix_stream_data_wait(struct sock *sk, long timeo,
				  struct sk_buff *last, unsigned int last_len,
				  bool freezable)
{
	struct sk_buff *tail;
	DEFINE_WAIT(wait);

	unix_state_lock(sk);

	for (;;) {
		prepare_to_wait(sk_sleep(sk), &wait, TASK_INTERRUPTIBLE);

		tail = skb_peek_tail(&sk->sk_receive_queue);
		if (tail != last ||
		    (tail && tail->len != last_len) ||
		    sk->sk_err ||
		    (sk->sk_shutdown & RCV_SHUTDOWN) ||
		    signal_pending(current) ||
		    !timeo)
			break;

		sk_set_bit(SOCKWQ_ASYNC_WAITDATA, sk);
		unix_state_unlock(sk);
		if (freezable)
			timeo = freezable_schedule_timeout(timeo);
		else
			timeo = schedule_timeout(timeo);
		unix_state_lock(sk);

		if (sock_flag(sk, SOCK_DEAD))
			break;

		sk_clear_bit(SOCKWQ_ASYNC_WAITDATA, sk);
	}

	finish_wait(sk_sleep(sk), &wait);
	unix_state_unlock(sk);
	return timeo;
}

static unsigned int unix_skb_len(const struct sk_buff *skb)
{
	return skb->len - UNIXCB(skb).consumed;
}

struct unix_stream_read_state {
	int (*recv_actor)(struct sk_buff *, int, int,
			  struct unix_stream_read_state *);
	struct socket *socket;
	struct msghdr *msg;
	struct pipe_inode_info *pipe;
	size_t size;
	int flags;
	unsigned int splice_flags;
};

#if IS_ENABLED(CONFIG_AF_UNIX_OOB)
static int unix_stream_recv_urg(struct unix_stream_read_state *state)
{
	struct socket *sock = state->socket;
	struct sock *sk = sock->sk;
	struct unix_sock *u = unix_sk(sk);
	int chunk = 1;
	struct sk_buff *oob_skb;

	mutex_lock(&u->iolock);
	unix_state_lock(sk);

	if (sock_flag(sk, SOCK_URGINLINE) || !u->oob_skb) {
		unix_state_unlock(sk);
		mutex_unlock(&u->iolock);
		return -EINVAL;
	}

	oob_skb = u->oob_skb;

	if (!(state->flags & MSG_PEEK)) {
		u->oob_skb = NULL;
	}

	unix_state_unlock(sk);

	chunk = state->recv_actor(oob_skb, 0, chunk, state);

	if (!(state->flags & MSG_PEEK)) {
		UNIXCB(oob_skb).consumed += 1;
		kfree_skb(oob_skb);
	}

	mutex_unlock(&u->iolock);

	if (chunk < 0)
		return -EFAULT;

	state->msg->msg_flags |= MSG_OOB;
	return 1;
}

static struct sk_buff *manage_oob(struct sk_buff *skb, struct sock *sk,
				  int flags, int copied)
{
	struct unix_sock *u = unix_sk(sk);

	if (!unix_skb_len(skb) && !(flags & MSG_PEEK)) {
		skb_unlink(skb, &sk->sk_receive_queue);
		consume_skb(skb);
		skb = NULL;
	} else {
		if (skb == u->oob_skb) {
			if (copied) {
				skb = NULL;
			} else if (sock_flag(sk, SOCK_URGINLINE)) {
				if (!(flags & MSG_PEEK)) {
					u->oob_skb = NULL;
					consume_skb(skb);
				}
			} else if (!(flags & MSG_PEEK)) {
				skb_unlink(skb, &sk->sk_receive_queue);
				consume_skb(skb);
				skb = skb_peek(&sk->sk_receive_queue);
			}
		}
	}
	return skb;
}
#endif

static int unix_stream_read_sock(struct sock *sk, read_descriptor_t *desc,
				 sk_read_actor_t recv_actor)
{
	if (unlikely(sk->sk_state != TCP_ESTABLISHED))
		return -ENOTCONN;

	return unix_read_sock(sk, desc, recv_actor);
}

static int unix_stream_read_generic(struct unix_stream_read_state *state,
				    bool freezable)
{
	struct scm_cookie scm;
	struct socket *sock = state->socket;
	struct sock *sk = sock->sk;
	struct unix_sock *u = unix_sk(sk);
	int copied = 0;
	int flags = state->flags;
	int noblock = flags & MSG_DONTWAIT;
	bool check_creds = false;
	int target;
	int err = 0;
	long timeo;
	int skip;
	size_t size = state->size;
	unsigned int last_len;

	if (unlikely(sk->sk_state != TCP_ESTABLISHED)) {
		err = -EINVAL;
		goto out;
	}

	if (unlikely(flags & MSG_OOB)) {
		err = -EOPNOTSUPP;
#if IS_ENABLED(CONFIG_AF_UNIX_OOB)
		err = unix_stream_recv_urg(state);
#endif
		goto out;
	}

	target = sock_rcvlowat(sk, flags & MSG_WAITALL, size);
	timeo = sock_rcvtimeo(sk, noblock);

	memset(&scm, 0, sizeof(scm));

	/* Lock the socket to prevent queue disordering
	 * while sleeps in memcpy_tomsg
	 */
	mutex_lock(&u->iolock);

	skip = max(sk_peek_offset(sk, flags), 0);

	do {
		int chunk;
		bool drop_skb;
		struct sk_buff *skb, *last;

redo:
		unix_state_lock(sk);
		if (sock_flag(sk, SOCK_DEAD)) {
			err = -ECONNRESET;
			goto unlock;
		}
		last = skb = skb_peek(&sk->sk_receive_queue);
		last_len = last ? last->len : 0;

#if IS_ENABLED(CONFIG_AF_UNIX_OOB)
		if (skb) {
			skb = manage_oob(skb, sk, flags, copied);
			if (!skb) {
				unix_state_unlock(sk);
				if (copied)
					break;
				goto redo;
			}
		}
#endif
again:
		if (skb == NULL) {
			if (copied >= target)
				goto unlock;

			/*
			 *	POSIX 1003.1g mandates this order.
			 */

			err = sock_error(sk);
			if (err)
				goto unlock;
			if (sk->sk_shutdown & RCV_SHUTDOWN)
				goto unlock;

			unix_state_unlock(sk);
			if (!timeo) {
				err = -EAGAIN;
				break;
			}

			mutex_unlock(&u->iolock);

			timeo = unix_stream_data_wait(sk, timeo, last,
						      last_len, freezable);

			if (signal_pending(current)) {
				err = sock_intr_errno(timeo);
				scm_destroy(&scm);
				goto out;
			}

			mutex_lock(&u->iolock);
			goto redo;
unlock:
			unix_state_unlock(sk);
			break;
		}

		while (skip >= unix_skb_len(skb)) {
			skip -= unix_skb_len(skb);
			last = skb;
			last_len = skb->len;
			skb = skb_peek_next(skb, &sk->sk_receive_queue);
			if (!skb)
				goto again;
		}

		unix_state_unlock(sk);

		if (check_creds) {
			/* Never glue messages from different writers */
			if (!unix_skb_scm_eq(skb, &scm))
				break;
		} else if (test_bit(SOCK_PASSCRED, &sock->flags)) {
			/* Copy credentials */
			scm_set_cred(&scm, UNIXCB(skb).pid, UNIXCB(skb).uid, UNIXCB(skb).gid);
			unix_set_secdata(&scm, skb);
			check_creds = true;
		}

		/* Copy address just once */
		if (state->msg && state->msg->msg_name) {
			DECLARE_SOCKADDR(struct sockaddr_un *, sunaddr,
					 state->msg->msg_name);
			unix_copy_addr(state->msg, skb->sk);
			sunaddr = NULL;
		}

		chunk = min_t(unsigned int, unix_skb_len(skb) - skip, size);
		skb_get(skb);
		chunk = state->recv_actor(skb, skip, chunk, state);
		drop_skb = !unix_skb_len(skb);
		/* skb is only safe to use if !drop_skb */
		consume_skb(skb);
		if (chunk < 0) {
			if (copied == 0)
				copied = -EFAULT;
			break;
		}
		copied += chunk;
		size -= chunk;

		if (drop_skb) {
			/* the skb was touched by a concurrent reader;
			 * we should not expect anything from this skb
			 * anymore and assume it invalid - we can be
			 * sure it was dropped from the socket queue
			 *
			 * let's report a short read
			 */
			err = 0;
			break;
		}

		/* Mark read part of skb as used */
		if (!(flags & MSG_PEEK)) {
			UNIXCB(skb).consumed += chunk;

			sk_peek_offset_bwd(sk, chunk);

			if (UNIXCB(skb).fp) {
				scm_stat_del(sk, skb);
				unix_detach_fds(&scm, skb);
			}

			if (unix_skb_len(skb))
				break;

			skb_unlink(skb, &sk->sk_receive_queue);
			consume_skb(skb);

			if (scm.fp)
				break;
		} else {
			/* It is questionable, see note in unix_dgram_recvmsg.
			 */
			if (UNIXCB(skb).fp)
				unix_peek_fds(&scm, skb);

			sk_peek_offset_fwd(sk, chunk);

			if (UNIXCB(skb).fp)
				break;

			skip = 0;
			last = skb;
			last_len = skb->len;
			unix_state_lock(sk);
			skb = skb_peek_next(skb, &sk->sk_receive_queue);
			if (skb)
				goto again;
			unix_state_unlock(sk);
			break;
		}
	} while (size);

	mutex_unlock(&u->iolock);
	if (state->msg)
		scm_recv(sock, state->msg, &scm, flags);
	else
		scm_destroy(&scm);
out:
	return copied ? : err;
}

static int unix_stream_read_actor(struct sk_buff *skb,
				  int skip, int chunk,
				  struct unix_stream_read_state *state)
{
	int ret;

	ret = skb_copy_datagram_msg(skb, UNIXCB(skb).consumed + skip,
				    state->msg, chunk);
	return ret ?: chunk;
}

int __unix_stream_recvmsg(struct sock *sk, struct msghdr *msg,
			  size_t size, int flags)
{
	struct unix_stream_read_state state = {
		.recv_actor = unix_stream_read_actor,
		.socket = sk->sk_socket,
		.msg = msg,
		.size = size,
		.flags = flags
	};

	return unix_stream_read_generic(&state, true);
}

static int unix_stream_recvmsg(struct socket *sock, struct msghdr *msg,
			       size_t size, int flags)
{
	struct unix_stream_read_state state = {
		.recv_actor = unix_stream_read_actor,
		.socket = sock,
		.msg = msg,
		.size = size,
		.flags = flags
	};

#ifdef CONFIG_BPF_SYSCALL
	struct sock *sk = sock->sk;
	const struct proto *prot = READ_ONCE(sk->sk_prot);

	if (prot != &unix_stream_proto)
		return prot->recvmsg(sk, msg, size, flags & MSG_DONTWAIT,
					    flags & ~MSG_DONTWAIT, NULL);
#endif
	return unix_stream_read_generic(&state, true);
}

static int unix_stream_splice_actor(struct sk_buff *skb,
				    int skip, int chunk,
				    struct unix_stream_read_state *state)
{
	return skb_splice_bits(skb, state->socket->sk,
			       UNIXCB(skb).consumed + skip,
			       state->pipe, chunk, state->splice_flags);
}

static ssize_t unix_stream_splice_read(struct socket *sock,  loff_t *ppos,
				       struct pipe_inode_info *pipe,
				       size_t size, unsigned int flags)
{
	struct unix_stream_read_state state = {
		.recv_actor = unix_stream_splice_actor,
		.socket = sock,
		.pipe = pipe,
		.size = size,
		.splice_flags = flags,
	};

	if (unlikely(*ppos))
		return -ESPIPE;

	if (sock->file->f_flags & O_NONBLOCK ||
	    flags & SPLICE_F_NONBLOCK)
		state.flags = MSG_DONTWAIT;

	return unix_stream_read_generic(&state, false);
}

static int unix_shutdown(struct socket *sock, int mode)
{
	struct sock *sk = sock->sk;
	struct sock *other;

	if (mode < SHUT_RD || mode > SHUT_RDWR)
		return -EINVAL;
	/* This maps:
	 * SHUT_RD   (0) -> RCV_SHUTDOWN  (1)
	 * SHUT_WR   (1) -> SEND_SHUTDOWN (2)
	 * SHUT_RDWR (2) -> SHUTDOWN_MASK (3)
	 */
	++mode;

	unix_state_lock(sk);
	sk->sk_shutdown |= mode;
	other = unix_peer(sk);
	if (other)
		sock_hold(other);
	unix_state_unlock(sk);
	sk->sk_state_change(sk);

	if (other &&
		(sk->sk_type == SOCK_STREAM || sk->sk_type == SOCK_SEQPACKET)) {

		int peer_mode = 0;
		const struct proto *prot = READ_ONCE(other->sk_prot);

		if (prot->unhash)
			prot->unhash(other);
		if (mode&RCV_SHUTDOWN)
			peer_mode |= SEND_SHUTDOWN;
		if (mode&SEND_SHUTDOWN)
			peer_mode |= RCV_SHUTDOWN;
		unix_state_lock(other);
		other->sk_shutdown |= peer_mode;
		unix_state_unlock(other);
		other->sk_state_change(other);
		if (peer_mode == SHUTDOWN_MASK)
			sk_wake_async(other, SOCK_WAKE_WAITD, POLL_HUP);
		else if (peer_mode & RCV_SHUTDOWN)
			sk_wake_async(other, SOCK_WAKE_WAITD, POLL_IN);
	}
	if (other)
		sock_put(other);

	return 0;
}

long unix_inq_len(struct sock *sk)
{
	struct sk_buff *skb;
	long amount = 0;

	if (sk->sk_state == TCP_LISTEN)
		return -EINVAL;

	spin_lock(&sk->sk_receive_queue.lock);
	if (sk->sk_type == SOCK_STREAM ||
	    sk->sk_type == SOCK_SEQPACKET) {
		skb_queue_walk(&sk->sk_receive_queue, skb)
			amount += unix_skb_len(skb);
	} else {
		skb = skb_peek(&sk->sk_receive_queue);
		if (skb)
			amount = skb->len;
	}
	spin_unlock(&sk->sk_receive_queue.lock);

	return amount;
}
EXPORT_SYMBOL_GPL(unix_inq_len);

long unix_outq_len(struct sock *sk)
{
	return sk_wmem_alloc_get(sk);
}
EXPORT_SYMBOL_GPL(unix_outq_len);

static int unix_open_file(struct sock *sk)
{
	struct path path;
	struct file *f;
	int fd;

	if (!ns_capable(sock_net(sk)->user_ns, CAP_NET_ADMIN))
		return -EPERM;

	if (!smp_load_acquire(&unix_sk(sk)->addr))
		return -ENOENT;

	path = unix_sk(sk)->path;
	if (!path.dentry)
		return -ENOENT;

	path_get(&path);

	fd = get_unused_fd_flags(O_CLOEXEC);
	if (fd < 0)
		goto out;

	f = dentry_open(&path, O_PATH, current_cred());
	if (IS_ERR(f)) {
		put_unused_fd(fd);
		fd = PTR_ERR(f);
		goto out;
	}

	fd_install(fd, f);
out:
	path_put(&path);

	return fd;
}

static int unix_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	struct sock *sk = sock->sk;
	long amount = 0;
	int err;

	switch (cmd) {
	case SIOCOUTQ:
		amount = unix_outq_len(sk);
		err = put_user(amount, (int __user *)arg);
		break;
	case SIOCINQ:
		amount = unix_inq_len(sk);
		if (amount < 0)
			err = amount;
		else
			err = put_user(amount, (int __user *)arg);
		break;
	case SIOCUNIXFILE:
		err = unix_open_file(sk);
		break;
#if IS_ENABLED(CONFIG_AF_UNIX_OOB)
	case SIOCATMARK:
		{
			struct sk_buff *skb;
			struct unix_sock *u = unix_sk(sk);
			int answ = 0;

			skb = skb_peek(&sk->sk_receive_queue);
			if (skb && skb == u->oob_skb)
				answ = 1;
			err = put_user(answ, (int __user *)arg);
		}
		break;
#endif
	default:
		err = -ENOIOCTLCMD;
		break;
	}
	return err;
}

#ifdef CONFIG_COMPAT
static int unix_compat_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	return unix_ioctl(sock, cmd, (unsigned long)compat_ptr(arg));
}
#endif

static __poll_t unix_poll(struct file *file, struct socket *sock, poll_table *wait)
{
	struct sock *sk = sock->sk;
	__poll_t mask;

	sock_poll_wait(file, sock, wait);
	mask = 0;

	/* exceptional events? */
	if (sk->sk_err)
		mask |= EPOLLERR;
	if (sk->sk_shutdown == SHUTDOWN_MASK)
		mask |= EPOLLHUP;
	if (sk->sk_shutdown & RCV_SHUTDOWN)
		mask |= EPOLLRDHUP | EPOLLIN | EPOLLRDNORM;

	/* readable? */
	if (!skb_queue_empty_lockless(&sk->sk_receive_queue))
		mask |= EPOLLIN | EPOLLRDNORM;
	if (sk_is_readable(sk))
		mask |= EPOLLIN | EPOLLRDNORM;

	/* Connection-based need to check for termination and startup */
	if ((sk->sk_type == SOCK_STREAM || sk->sk_type == SOCK_SEQPACKET) &&
	    sk->sk_state == TCP_CLOSE)
		mask |= EPOLLHUP;

	/*
	 * we set writable also when the other side has shut down the
	 * connection. This prevents stuck sockets.
	 */
	if (unix_writable(sk))
		mask |= EPOLLOUT | EPOLLWRNORM | EPOLLWRBAND;

	return mask;
}

static __poll_t unix_dgram_poll(struct file *file, struct socket *sock,
				    poll_table *wait)
{
	struct sock *sk = sock->sk, *other;
	unsigned int writable;
	__poll_t mask;

	sock_poll_wait(file, sock, wait);
	mask = 0;

	/* exceptional events? */
	if (sk->sk_err || !skb_queue_empty_lockless(&sk->sk_error_queue))
		mask |= EPOLLERR |
			(sock_flag(sk, SOCK_SELECT_ERR_QUEUE) ? EPOLLPRI : 0);

	if (sk->sk_shutdown & RCV_SHUTDOWN)
		mask |= EPOLLRDHUP | EPOLLIN | EPOLLRDNORM;
	if (sk->sk_shutdown == SHUTDOWN_MASK)
		mask |= EPOLLHUP;

	/* readable? */
	if (!skb_queue_empty_lockless(&sk->sk_receive_queue))
		mask |= EPOLLIN | EPOLLRDNORM;
	if (sk_is_readable(sk))
		mask |= EPOLLIN | EPOLLRDNORM;

	/* Connection-based need to check for termination and startup */
	if (sk->sk_type == SOCK_SEQPACKET) {
		if (sk->sk_state == TCP_CLOSE)
			mask |= EPOLLHUP;
		/* connection hasn't started yet? */
		if (sk->sk_state == TCP_SYN_SENT)
			return mask;
	}

	/* No write status requested, avoid expensive OUT tests. */
	if (!(poll_requested_events(wait) & (EPOLLWRBAND|EPOLLWRNORM|EPOLLOUT)))
		return mask;

	writable = unix_writable(sk);
	if (writable) {
		unix_state_lock(sk);

		other = unix_peer(sk);
		if (other && unix_peer(other) != sk &&
		    unix_recvq_full_lockless(other) &&
		    unix_dgram_peer_wake_me(sk, other))
			writable = 0;

		unix_state_unlock(sk);
	}

	if (writable)
		mask |= EPOLLOUT | EPOLLWRNORM | EPOLLWRBAND;
	else
		sk_set_bit(SOCKWQ_ASYNC_NOSPACE, sk);

	return mask;
}

#ifdef CONFIG_PROC_FS

#define BUCKET_SPACE (BITS_PER_LONG - (UNIX_HASH_BITS + 1) - 1)

#define get_bucket(x) ((x) >> BUCKET_SPACE)
#define get_offset(x) ((x) & ((1L << BUCKET_SPACE) - 1))
#define set_bucket_offset(b, o) ((b) << BUCKET_SPACE | (o))

static struct sock *unix_from_bucket(struct seq_file *seq, loff_t *pos)
{
	unsigned long offset = get_offset(*pos);
	unsigned long bucket = get_bucket(*pos);
	struct sock *sk;
	unsigned long count = 0;

	for (sk = sk_head(&unix_socket_table[bucket]); sk; sk = sk_next(sk)) {
		if (sock_net(sk) != seq_file_net(seq))
			continue;
		if (++count == offset)
			break;
	}

	return sk;
}

static struct sock *unix_next_socket(struct seq_file *seq,
				     struct sock *sk,
				     loff_t *pos)
{
	unsigned long bucket;

	while (sk > (struct sock *)SEQ_START_TOKEN) {
		sk = sk_next(sk);
		if (!sk)
			goto next_bucket;
		if (sock_net(sk) == seq_file_net(seq))
			return sk;
	}

	do {
		sk = unix_from_bucket(seq, pos);
		if (sk)
			return sk;

next_bucket:
		bucket = get_bucket(*pos) + 1;
		*pos = set_bucket_offset(bucket, 1);
	} while (bucket < ARRAY_SIZE(unix_socket_table));

	return NULL;
}

static void *unix_seq_start(struct seq_file *seq, loff_t *pos)
	__acquires(unix_table_lock)
{
	spin_lock(&unix_table_lock);

	if (!*pos)
		return SEQ_START_TOKEN;

	if (get_bucket(*pos) >= ARRAY_SIZE(unix_socket_table))
		return NULL;

	return unix_next_socket(seq, NULL, pos);
}

static void *unix_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	++*pos;
	return unix_next_socket(seq, v, pos);
}

static void unix_seq_stop(struct seq_file *seq, void *v)
	__releases(unix_table_lock)
{
	spin_unlock(&unix_table_lock);
}

static int unix_seq_show(struct seq_file *seq, void *v)
{

	if (v == SEQ_START_TOKEN)
		seq_puts(seq, "Num       RefCount Protocol Flags    Type St "
			 "Inode Path\n");
	else {
		struct sock *s = v;
		struct unix_sock *u = unix_sk(s);
		unix_state_lock(s);

		seq_printf(seq, "%pK: %08X %08X %08X %04X %02X %5lu",
			s,
			refcount_read(&s->sk_refcnt),
			0,
			s->sk_state == TCP_LISTEN ? __SO_ACCEPTCON : 0,
			s->sk_type,
			s->sk_socket ?
			(s->sk_state == TCP_ESTABLISHED ? SS_CONNECTED : SS_UNCONNECTED) :
			(s->sk_state == TCP_ESTABLISHED ? SS_CONNECTING : SS_DISCONNECTING),
			sock_i_ino(s));

		if (u->addr) {	// under unix_table_lock here
			int i, len;
			seq_putc(seq, ' ');

			i = 0;
			len = u->addr->len - sizeof(short);
			if (!UNIX_ABSTRACT(s))
				len--;
			else {
				seq_putc(seq, '@');
				i++;
			}
			for ( ; i < len; i++)
				seq_putc(seq, u->addr->name->sun_path[i] ?:
					 '@');
		}
		unix_state_unlock(s);
		seq_putc(seq, '\n');
	}

	return 0;
}

static const struct seq_operations unix_seq_ops = {
	.start  = unix_seq_start,
	.next   = unix_seq_next,
	.stop   = unix_seq_stop,
	.show   = unix_seq_show,
};

#if IS_BUILTIN(CONFIG_UNIX) && defined(CONFIG_BPF_SYSCALL)
struct bpf_iter__unix {
	__bpf_md_ptr(struct bpf_iter_meta *, meta);
	__bpf_md_ptr(struct unix_sock *, unix_sk);
	uid_t uid __aligned(8);
};

static int unix_prog_seq_show(struct bpf_prog *prog, struct bpf_iter_meta *meta,
			      struct unix_sock *unix_sk, uid_t uid)
{
	struct bpf_iter__unix ctx;

	meta->seq_num--;  /* skip SEQ_START_TOKEN */
	ctx.meta = meta;
	ctx.unix_sk = unix_sk;
	ctx.uid = uid;
	return bpf_iter_run_prog(prog, &ctx);
}

static int bpf_iter_unix_seq_show(struct seq_file *seq, void *v)
{
	struct bpf_iter_meta meta;
	struct bpf_prog *prog;
	struct sock *sk = v;
	uid_t uid;

	if (v == SEQ_START_TOKEN)
		return 0;

	uid = from_kuid_munged(seq_user_ns(seq), sock_i_uid(sk));
	meta.seq = seq;
	prog = bpf_iter_get_info(&meta, false);
	return unix_prog_seq_show(prog, &meta, v, uid);
}

static void bpf_iter_unix_seq_stop(struct seq_file *seq, void *v)
{
	struct bpf_iter_meta meta;
	struct bpf_prog *prog;

	if (!v) {
		meta.seq = seq;
		prog = bpf_iter_get_info(&meta, true);
		if (prog)
			(void)unix_prog_seq_show(prog, &meta, v, 0);
	}

	unix_seq_stop(seq, v);
}

static const struct seq_operations bpf_iter_unix_seq_ops = {
	.start	= unix_seq_start,
	.next	= unix_seq_next,
	.stop	= bpf_iter_unix_seq_stop,
	.show	= bpf_iter_unix_seq_show,
};
#endif
#endif

static const struct net_proto_family unix_family_ops = {
	.family = PF_UNIX,
	.create = unix_create,
	.owner	= THIS_MODULE,
};


static int __net_init unix_net_init(struct net *net)
{
	int error = -ENOMEM;

	net->unx.sysctl_max_dgram_qlen = 10;
	if (unix_sysctl_register(net))
		goto out;

#ifdef CONFIG_PROC_FS
	if (!proc_create_net("unix", 0, net->proc_net, &unix_seq_ops,
			sizeof(struct seq_net_private))) {
		unix_sysctl_unregister(net);
		goto out;
	}
#endif
	error = 0;
out:
	return error;
}

static void __net_exit unix_net_exit(struct net *net)
{
	unix_sysctl_unregister(net);
	remove_proc_entry("unix", net->proc_net);
}

static struct pernet_operations unix_net_ops = {
	.init = unix_net_init,
	.exit = unix_net_exit,
};

#if IS_BUILTIN(CONFIG_UNIX) && defined(CONFIG_BPF_SYSCALL) && defined(CONFIG_PROC_FS)
DEFINE_BPF_ITER_FUNC(unix, struct bpf_iter_meta *meta,
		     struct unix_sock *unix_sk, uid_t uid)

static const struct bpf_iter_seq_info unix_seq_info = {
	.seq_ops		= &bpf_iter_unix_seq_ops,
	.init_seq_private	= bpf_iter_init_seq_net,
	.fini_seq_private	= bpf_iter_fini_seq_net,
	.seq_priv_size		= sizeof(struct seq_net_private),
};

static struct bpf_iter_reg unix_reg_info = {
	.target			= "unix",
	.ctx_arg_info_size	= 1,
	.ctx_arg_info		= {
		{ offsetof(struct bpf_iter__unix, unix_sk),
		  PTR_TO_BTF_ID_OR_NULL },
	},
	.seq_info		= &unix_seq_info,
};

static void __init bpf_iter_register(void)
{
	unix_reg_info.ctx_arg_info[0].btf_id = btf_sock_ids[BTF_SOCK_TYPE_UNIX];
	if (bpf_iter_reg_target(&unix_reg_info))
		pr_warn("Warning: could not register bpf iterator unix\n");
}
#endif

static int __init af_unix_init(void)
{
	int rc = -1;

	BUILD_BUG_ON(sizeof(struct unix_skb_parms) > sizeof_field(struct sk_buff, cb));

	rc = proto_register(&unix_dgram_proto, 1);
	if (rc != 0) {
		pr_crit("%s: Cannot create unix_sock SLAB cache!\n", __func__);
		goto out;
	}

	rc = proto_register(&unix_stream_proto, 1);
	if (rc != 0) {
		pr_crit("%s: Cannot create unix_sock SLAB cache!\n", __func__);
		goto out;
	}

	sock_register(&unix_family_ops);
	register_pernet_subsys(&unix_net_ops);
	unix_bpf_build_proto();

#if IS_BUILTIN(CONFIG_UNIX) && defined(CONFIG_BPF_SYSCALL) && defined(CONFIG_PROC_FS)
	bpf_iter_register();
#endif

out:
	return rc;
}

static void __exit af_unix_exit(void)
{
	sock_unregister(PF_UNIX);
	proto_unregister(&unix_dgram_proto);
	proto_unregister(&unix_stream_proto);
	unregister_pernet_subsys(&unix_net_ops);
}

/* Earlier than device_initcall() so that other drivers invoking
   request_module() don't end up in a loop when modprobe tries
   to use a UNIX socket. But later than subsys_initcall() because
   we depend on stuff initialised there */
fs_initcall(af_unix_init);
module_exit(af_unix_exit);

MODULE_LICENSE("GPL");
MODULE_ALIAS_NETPROTO(PF_UNIX);
