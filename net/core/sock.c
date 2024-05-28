// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Generic socket support routines. Memory allocators, socket lock/release
 *		handler for protocols to use and generic option handler.
 *
 * Authors:	Ross Biro
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Florian La Roche, <flla@stud.uni-sb.de>
 *		Alan Cox, <A.Cox@swansea.ac.uk>
 *
 * Fixes:
 *		Alan Cox	: 	Numerous verify_area() problems
 *		Alan Cox	:	Connecting on a connecting socket
 *					now returns an error for tcp.
 *		Alan Cox	:	sock->protocol is set correctly.
 *					and is not sometimes left as 0.
 *		Alan Cox	:	connect handles icmp errors on a
 *					connect properly. Unfortunately there
 *					is a restart syscall nasty there. I
 *					can't match BSD without hacking the C
 *					library. Ideas urgently sought!
 *		Alan Cox	:	Disallow bind() to addresses that are
 *					not ours - especially broadcast ones!!
 *		Alan Cox	:	Socket 1024 _IS_ ok for users. (fencepost)
 *		Alan Cox	:	sock_wfree/sock_rfree don't destroy sockets,
 *					instead they leave that for the DESTROY timer.
 *		Alan Cox	:	Clean up error flag in accept
 *		Alan Cox	:	TCP ack handling is buggy, the DESTROY timer
 *					was buggy. Put a remove_sock() in the handler
 *					for memory when we hit 0. Also altered the timer
 *					code. The ACK stuff can wait and needs major
 *					TCP layer surgery.
 *		Alan Cox	:	Fixed TCP ack bug, removed remove sock
 *					and fixed timer/inet_bh race.
 *		Alan Cox	:	Added zapped flag for TCP
 *		Alan Cox	:	Move kfree_skb into skbuff.c and tidied up surplus code
 *		Alan Cox	:	for new sk_buff allocations wmalloc/rmalloc now call alloc_skb
 *		Alan Cox	:	kfree_s calls now are kfree_skbmem so we can track skb resources
 *		Alan Cox	:	Supports socket option broadcast now as does udp. Packet and raw need fixing.
 *		Alan Cox	:	Added RCVBUF,SNDBUF size setting. It suddenly occurred to me how easy it was so...
 *		Rick Sladkey	:	Relaxed UDP rules for matching packets.
 *		C.E.Hawkins	:	IFF_PROMISC/SIOCGHWADDR support
 *	Pauline Middelink	:	identd support
 *		Alan Cox	:	Fixed connect() taking signals I think.
 *		Alan Cox	:	SO_LINGER supported
 *		Alan Cox	:	Error reporting fixes
 *		Anonymous	:	inet_create tidied up (sk->reuse setting)
 *		Alan Cox	:	inet sockets don't set sk->type!
 *		Alan Cox	:	Split socket option code
 *		Alan Cox	:	Callbacks
 *		Alan Cox	:	Nagle flag for Charles & Johannes stuff
 *		Alex		:	Removed restriction on inet fioctl
 *		Alan Cox	:	Splitting INET from NET core
 *		Alan Cox	:	Fixed bogus SO_TYPE handling in getsockopt()
 *		Adam Caldwell	:	Missing return in SO_DONTROUTE/SO_DEBUG code
 *		Alan Cox	:	Split IP from generic code
 *		Alan Cox	:	New kfree_skbmem()
 *		Alan Cox	:	Make SO_DEBUG superuser only.
 *		Alan Cox	:	Allow anyone to clear SO_DEBUG
 *					(compatibility fix)
 *		Alan Cox	:	Added optimistic memory grabbing for AF_UNIX throughput.
 *		Alan Cox	:	Allocator for a socket is settable.
 *		Alan Cox	:	SO_ERROR includes soft errors.
 *		Alan Cox	:	Allow NULL arguments on some SO_ opts
 *		Alan Cox	: 	Generic socket allocation to make hooks
 *					easier (suggested by Craig Metz).
 *		Michael Pall	:	SO_ERROR returns positive errno again
 *              Steve Whitehouse:       Added default destructor to free
 *                                      protocol private data.
 *              Steve Whitehouse:       Added various other default routines
 *                                      common to several socket families.
 *              Chris Evans     :       Call suser() check last on F_SETOWN
 *		Jay Schulist	:	Added SO_ATTACH_FILTER and SO_DETACH_FILTER.
 *		Andi Kleen	:	Add sock_kmalloc()/sock_kfree_s()
 *		Andi Kleen	:	Fix write_space callback
 *		Chris Evans	:	Security fixes - signedness again
 *		Arnaldo C. Melo :       cleanups, use skb_queue_purge
 *
 * To Fix:
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <asm/unaligned.h>
#include <linux/capability.h>
#include <linux/errno.h>
#include <linux/errqueue.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/poll.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/init.h>
#include <linux/highmem.h>
#include <linux/user_namespace.h>
#include <linux/static_key.h>
#include <linux/memcontrol.h>
#include <linux/prefetch.h>
#include <linux/compat.h>
#include <linux/mroute.h>
#include <linux/mroute6.h>
#include <linux/icmpv6.h>

#include <linux/uaccess.h>

#include <linux/netdevice.h>
#include <net/protocol.h>
#include <linux/skbuff.h>
#include <net/net_namespace.h>
#include <net/request_sock.h>
#include <net/sock.h>
#include <linux/net_tstamp.h>
#include <net/xfrm.h>
#include <linux/ipsec.h>
#include <net/cls_cgroup.h>
#include <net/netprio_cgroup.h>
#include <linux/sock_diag.h>

#include <linux/filter.h>
#include <net/sock_reuseport.h>
#include <net/bpf_sk_storage.h>

#include <trace/events/sock.h>

#include <net/tcp.h>
#include <net/busy_poll.h>
#include <net/phonet/phonet.h>

#include <linux/ethtool.h>

#include "dev.h"

static DEFINE_MUTEX(proto_list_mutex);
static LIST_HEAD(proto_list);

static void sock_def_write_space_wfree(struct sock *sk);
static void sock_def_write_space(struct sock *sk);

/**
 * sk_ns_capable - General socket capability test
 * @sk: Socket to use a capability on or through
 * @user_ns: The user namespace of the capability to use
 * @cap: The capability to use
 *
 * Test to see if the opener of the socket had when the socket was
 * created and the current process has the capability @cap in the user
 * namespace @user_ns.
 */
bool sk_ns_capable(const struct sock *sk,
		   struct user_namespace *user_ns, int cap)
{
	return file_ns_capable(sk->sk_socket->file, user_ns, cap) &&
		ns_capable(user_ns, cap);
}
EXPORT_SYMBOL(sk_ns_capable);

/**
 * sk_capable - Socket global capability test
 * @sk: Socket to use a capability on or through
 * @cap: The global capability to use
 *
 * Test to see if the opener of the socket had when the socket was
 * created and the current process has the capability @cap in all user
 * namespaces.
 */
bool sk_capable(const struct sock *sk, int cap)
{
	return sk_ns_capable(sk, &init_user_ns, cap);
}
EXPORT_SYMBOL(sk_capable);

/**
 * sk_net_capable - Network namespace socket capability test
 * @sk: Socket to use a capability on or through
 * @cap: The capability to use
 *
 * Test to see if the opener of the socket had when the socket was created
 * and the current process has the capability @cap over the network namespace
 * the socket is a member of.
 */
bool sk_net_capable(const struct sock *sk, int cap)
{
	return sk_ns_capable(sk, sock_net(sk)->user_ns, cap);
}
EXPORT_SYMBOL(sk_net_capable);

/*
 * Each address family might have different locking rules, so we have
 * one slock key per address family and separate keys for internal and
 * userspace sockets.
 */
static struct lock_class_key af_family_keys[AF_MAX];
static struct lock_class_key af_family_kern_keys[AF_MAX];
static struct lock_class_key af_family_slock_keys[AF_MAX];
static struct lock_class_key af_family_kern_slock_keys[AF_MAX];

/*
 * Make lock validator output more readable. (we pre-construct these
 * strings build-time, so that runtime initialization of socket
 * locks is fast):
 */

#define _sock_locks(x)						  \
  x "AF_UNSPEC",	x "AF_UNIX"     ,	x "AF_INET"     , \
  x "AF_AX25"  ,	x "AF_IPX"      ,	x "AF_APPLETALK", \
  x "AF_NETROM",	x "AF_BRIDGE"   ,	x "AF_ATMPVC"   , \
  x "AF_X25"   ,	x "AF_INET6"    ,	x "AF_ROSE"     , \
  x "AF_DECnet",	x "AF_NETBEUI"  ,	x "AF_SECURITY" , \
  x "AF_KEY"   ,	x "AF_NETLINK"  ,	x "AF_PACKET"   , \
  x "AF_ASH"   ,	x "AF_ECONET"   ,	x "AF_ATMSVC"   , \
  x "AF_RDS"   ,	x "AF_SNA"      ,	x "AF_IRDA"     , \
  x "AF_PPPOX" ,	x "AF_WANPIPE"  ,	x "AF_LLC"      , \
  x "27"       ,	x "28"          ,	x "AF_CAN"      , \
  x "AF_TIPC"  ,	x "AF_BLUETOOTH",	x "IUCV"        , \
  x "AF_RXRPC" ,	x "AF_ISDN"     ,	x "AF_PHONET"   , \
  x "AF_IEEE802154",	x "AF_CAIF"	,	x "AF_ALG"      , \
  x "AF_NFC"   ,	x "AF_VSOCK"    ,	x "AF_KCM"      , \
  x "AF_QIPCRTR",	x "AF_SMC"	,	x "AF_XDP"	, \
  x "AF_MCTP"  , \
  x "AF_MAX"

static const char *const af_family_key_strings[AF_MAX+1] = {
	_sock_locks("sk_lock-")
};
static const char *const af_family_slock_key_strings[AF_MAX+1] = {
	_sock_locks("slock-")
};
static const char *const af_family_clock_key_strings[AF_MAX+1] = {
	_sock_locks("clock-")
};

static const char *const af_family_kern_key_strings[AF_MAX+1] = {
	_sock_locks("k-sk_lock-")
};
static const char *const af_family_kern_slock_key_strings[AF_MAX+1] = {
	_sock_locks("k-slock-")
};
static const char *const af_family_kern_clock_key_strings[AF_MAX+1] = {
	_sock_locks("k-clock-")
};
static const char *const af_family_rlock_key_strings[AF_MAX+1] = {
	_sock_locks("rlock-")
};
static const char *const af_family_wlock_key_strings[AF_MAX+1] = {
	_sock_locks("wlock-")
};
static const char *const af_family_elock_key_strings[AF_MAX+1] = {
	_sock_locks("elock-")
};

/*
 * sk_callback_lock and sk queues locking rules are per-address-family,
 * so split the lock classes by using a per-AF key:
 */
static struct lock_class_key af_callback_keys[AF_MAX];
static struct lock_class_key af_rlock_keys[AF_MAX];
static struct lock_class_key af_wlock_keys[AF_MAX];
static struct lock_class_key af_elock_keys[AF_MAX];
static struct lock_class_key af_kern_callback_keys[AF_MAX];

/* Run time adjustable parameters. */
__u32 sysctl_wmem_max __read_mostly = SK_WMEM_MAX;
EXPORT_SYMBOL(sysctl_wmem_max);
__u32 sysctl_rmem_max __read_mostly = SK_RMEM_MAX;
EXPORT_SYMBOL(sysctl_rmem_max);
__u32 sysctl_wmem_default __read_mostly = SK_WMEM_MAX;
__u32 sysctl_rmem_default __read_mostly = SK_RMEM_MAX;
int sysctl_mem_pcpu_rsv __read_mostly = SK_MEMORY_PCPU_RESERVE;

int sysctl_tstamp_allow_data __read_mostly = 1;

DEFINE_STATIC_KEY_FALSE(memalloc_socks_key);
EXPORT_SYMBOL_GPL(memalloc_socks_key);

/**
 * sk_set_memalloc - sets %SOCK_MEMALLOC
 * @sk: socket to set it on
 *
 * Set %SOCK_MEMALLOC on a socket for access to emergency reserves.
 * It's the responsibility of the admin to adjust min_free_kbytes
 * to meet the requirements
 */
void sk_set_memalloc(struct sock *sk)
{
	sock_set_flag(sk, SOCK_MEMALLOC);
	sk->sk_allocation |= __GFP_MEMALLOC;
	static_branch_inc(&memalloc_socks_key);
}
EXPORT_SYMBOL_GPL(sk_set_memalloc);

void sk_clear_memalloc(struct sock *sk)
{
	sock_reset_flag(sk, SOCK_MEMALLOC);
	sk->sk_allocation &= ~__GFP_MEMALLOC;
	static_branch_dec(&memalloc_socks_key);

	/*
	 * SOCK_MEMALLOC is allowed to ignore rmem limits to ensure forward
	 * progress of swapping. SOCK_MEMALLOC may be cleared while
	 * it has rmem allocations due to the last swapfile being deactivated
	 * but there is a risk that the socket is unusable due to exceeding
	 * the rmem limits. Reclaim the reserves and obey rmem limits again.
	 */
	sk_mem_reclaim(sk);
}
EXPORT_SYMBOL_GPL(sk_clear_memalloc);

int __sk_backlog_rcv(struct sock *sk, struct sk_buff *skb)
{
	int ret;
	unsigned int noreclaim_flag;

	/* these should have been dropped before queueing */
	BUG_ON(!sock_flag(sk, SOCK_MEMALLOC));

	noreclaim_flag = memalloc_noreclaim_save();
	ret = INDIRECT_CALL_INET(sk->sk_backlog_rcv,
				 tcp_v6_do_rcv,
				 tcp_v4_do_rcv,
				 sk, skb);
	memalloc_noreclaim_restore(noreclaim_flag);

	return ret;
}
EXPORT_SYMBOL(__sk_backlog_rcv);

void sk_error_report(struct sock *sk)
{
	sk->sk_error_report(sk);

	switch (sk->sk_family) {
	case AF_INET:
		fallthrough;
	case AF_INET6:
		trace_inet_sk_error_report(sk);
		break;
	default:
		break;
	}
}
EXPORT_SYMBOL(sk_error_report);

int sock_get_timeout(long timeo, void *optval, bool old_timeval)
{
	struct __kernel_sock_timeval tv;

	if (timeo == MAX_SCHEDULE_TIMEOUT) {
		tv.tv_sec = 0;
		tv.tv_usec = 0;
	} else {
		tv.tv_sec = timeo / HZ;
		tv.tv_usec = ((timeo % HZ) * USEC_PER_SEC) / HZ;
	}

	if (old_timeval && in_compat_syscall() && !COMPAT_USE_64BIT_TIME) {
		struct old_timeval32 tv32 = { tv.tv_sec, tv.tv_usec };
		*(struct old_timeval32 *)optval = tv32;
		return sizeof(tv32);
	}

	if (old_timeval) {
		struct __kernel_old_timeval old_tv;
		old_tv.tv_sec = tv.tv_sec;
		old_tv.tv_usec = tv.tv_usec;
		*(struct __kernel_old_timeval *)optval = old_tv;
		return sizeof(old_tv);
	}

	*(struct __kernel_sock_timeval *)optval = tv;
	return sizeof(tv);
}
EXPORT_SYMBOL(sock_get_timeout);

int sock_copy_user_timeval(struct __kernel_sock_timeval *tv,
			   sockptr_t optval, int optlen, bool old_timeval)
{
	if (old_timeval && in_compat_syscall() && !COMPAT_USE_64BIT_TIME) {
		struct old_timeval32 tv32;

		if (optlen < sizeof(tv32))
			return -EINVAL;

		if (copy_from_sockptr(&tv32, optval, sizeof(tv32)))
			return -EFAULT;
		tv->tv_sec = tv32.tv_sec;
		tv->tv_usec = tv32.tv_usec;
	} else if (old_timeval) {
		struct __kernel_old_timeval old_tv;

		if (optlen < sizeof(old_tv))
			return -EINVAL;
		if (copy_from_sockptr(&old_tv, optval, sizeof(old_tv)))
			return -EFAULT;
		tv->tv_sec = old_tv.tv_sec;
		tv->tv_usec = old_tv.tv_usec;
	} else {
		if (optlen < sizeof(*tv))
			return -EINVAL;
		if (copy_from_sockptr(tv, optval, sizeof(*tv)))
			return -EFAULT;
	}

	return 0;
}
EXPORT_SYMBOL(sock_copy_user_timeval);

static int sock_set_timeout(long *timeo_p, sockptr_t optval, int optlen,
			    bool old_timeval)
{
	struct __kernel_sock_timeval tv;
	int err = sock_copy_user_timeval(&tv, optval, optlen, old_timeval);
	long val;

	if (err)
		return err;

	if (tv.tv_usec < 0 || tv.tv_usec >= USEC_PER_SEC)
		return -EDOM;

	if (tv.tv_sec < 0) {
		static int warned __read_mostly;

		WRITE_ONCE(*timeo_p, 0);
		if (warned < 10 && net_ratelimit()) {
			warned++;
			pr_info("%s: `%s' (pid %d) tries to set negative timeout\n",
				__func__, current->comm, task_pid_nr(current));
		}
		return 0;
	}
	val = MAX_SCHEDULE_TIMEOUT;
	if ((tv.tv_sec || tv.tv_usec) &&
	    (tv.tv_sec < (MAX_SCHEDULE_TIMEOUT / HZ - 1)))
		val = tv.tv_sec * HZ + DIV_ROUND_UP((unsigned long)tv.tv_usec,
						    USEC_PER_SEC / HZ);
	WRITE_ONCE(*timeo_p, val);
	return 0;
}

static bool sock_needs_netstamp(const struct sock *sk)
{
	switch (sk->sk_family) {
	case AF_UNSPEC:
	case AF_UNIX:
		return false;
	default:
		return true;
	}
}

static void sock_disable_timestamp(struct sock *sk, unsigned long flags)
{
	if (sk->sk_flags & flags) {
		sk->sk_flags &= ~flags;
		if (sock_needs_netstamp(sk) &&
		    !(sk->sk_flags & SK_FLAGS_TIMESTAMP))
			net_disable_timestamp();
	}
}


int __sock_queue_rcv_skb(struct sock *sk, struct sk_buff *skb)
{
	unsigned long flags;
	struct sk_buff_head *list = &sk->sk_receive_queue;

	if (atomic_read(&sk->sk_rmem_alloc) >= READ_ONCE(sk->sk_rcvbuf)) {
		atomic_inc(&sk->sk_drops);
		trace_sock_rcvqueue_full(sk, skb);
		return -ENOMEM;
	}

	if (!sk_rmem_schedule(sk, skb, skb->truesize)) {
		atomic_inc(&sk->sk_drops);
		return -ENOBUFS;
	}

	skb->dev = NULL;
	skb_set_owner_r(skb, sk);

	/* we escape from rcu protected region, make sure we dont leak
	 * a norefcounted dst
	 */
	skb_dst_force(skb);

	spin_lock_irqsave(&list->lock, flags);
	sock_skb_set_dropcount(sk, skb);
	__skb_queue_tail(list, skb);
	spin_unlock_irqrestore(&list->lock, flags);

	if (!sock_flag(sk, SOCK_DEAD))
		sk->sk_data_ready(sk);
	return 0;
}
EXPORT_SYMBOL(__sock_queue_rcv_skb);

int sock_queue_rcv_skb_reason(struct sock *sk, struct sk_buff *skb,
			      enum skb_drop_reason *reason)
{
	enum skb_drop_reason drop_reason;
	int err;

	err = sk_filter(sk, skb);
	if (err) {
		drop_reason = SKB_DROP_REASON_SOCKET_FILTER;
		goto out;
	}
	err = __sock_queue_rcv_skb(sk, skb);
	switch (err) {
	case -ENOMEM:
		drop_reason = SKB_DROP_REASON_SOCKET_RCVBUFF;
		break;
	case -ENOBUFS:
		drop_reason = SKB_DROP_REASON_PROTO_MEM;
		break;
	default:
		drop_reason = SKB_NOT_DROPPED_YET;
		break;
	}
out:
	if (reason)
		*reason = drop_reason;
	return err;
}
EXPORT_SYMBOL(sock_queue_rcv_skb_reason);

int __sk_receive_skb(struct sock *sk, struct sk_buff *skb,
		     const int nested, unsigned int trim_cap, bool refcounted)
{
	int rc = NET_RX_SUCCESS;

	if (sk_filter_trim_cap(sk, skb, trim_cap))
		goto discard_and_relse;

	skb->dev = NULL;

	if (sk_rcvqueues_full(sk, READ_ONCE(sk->sk_rcvbuf))) {
		atomic_inc(&sk->sk_drops);
		goto discard_and_relse;
	}
	if (nested)
		bh_lock_sock_nested(sk);
	else
		bh_lock_sock(sk);
	if (!sock_owned_by_user(sk)) {
		/*
		 * trylock + unlock semantics:
		 */
		mutex_acquire(&sk->sk_lock.dep_map, 0, 1, _RET_IP_);

		rc = sk_backlog_rcv(sk, skb);

		mutex_release(&sk->sk_lock.dep_map, _RET_IP_);
	} else if (sk_add_backlog(sk, skb, READ_ONCE(sk->sk_rcvbuf))) {
		bh_unlock_sock(sk);
		atomic_inc(&sk->sk_drops);
		goto discard_and_relse;
	}

	bh_unlock_sock(sk);
out:
	if (refcounted)
		sock_put(sk);
	return rc;
discard_and_relse:
	kfree_skb(skb);
	goto out;
}
EXPORT_SYMBOL(__sk_receive_skb);

INDIRECT_CALLABLE_DECLARE(struct dst_entry *ip6_dst_check(struct dst_entry *,
							  u32));
INDIRECT_CALLABLE_DECLARE(struct dst_entry *ipv4_dst_check(struct dst_entry *,
							   u32));
struct dst_entry *__sk_dst_check(struct sock *sk, u32 cookie)
{
	struct dst_entry *dst = __sk_dst_get(sk);

	if (dst && dst->obsolete &&
	    INDIRECT_CALL_INET(dst->ops->check, ip6_dst_check, ipv4_dst_check,
			       dst, cookie) == NULL) {
		sk_tx_queue_clear(sk);
		WRITE_ONCE(sk->sk_dst_pending_confirm, 0);
		RCU_INIT_POINTER(sk->sk_dst_cache, NULL);
		dst_release(dst);
		return NULL;
	}

	return dst;
}
EXPORT_SYMBOL(__sk_dst_check);

struct dst_entry *sk_dst_check(struct sock *sk, u32 cookie)
{
	struct dst_entry *dst = sk_dst_get(sk);

	if (dst && dst->obsolete &&
	    INDIRECT_CALL_INET(dst->ops->check, ip6_dst_check, ipv4_dst_check,
			       dst, cookie) == NULL) {
		sk_dst_reset(sk);
		dst_release(dst);
		return NULL;
	}

	return dst;
}
EXPORT_SYMBOL(sk_dst_check);

static int sock_bindtoindex_locked(struct sock *sk, int ifindex)
{
	int ret = -ENOPROTOOPT;
#ifdef CONFIG_NETDEVICES
	struct net *net = sock_net(sk);

	/* Sorry... */
	ret = -EPERM;
	if (sk->sk_bound_dev_if && !ns_capable(net->user_ns, CAP_NET_RAW))
		goto out;

	ret = -EINVAL;
	if (ifindex < 0)
		goto out;

	/* Paired with all READ_ONCE() done locklessly. */
	WRITE_ONCE(sk->sk_bound_dev_if, ifindex);

	if (sk->sk_prot->rehash)
		sk->sk_prot->rehash(sk);
	sk_dst_reset(sk);

	ret = 0;

out:
#endif

	return ret;
}

int sock_bindtoindex(struct sock *sk, int ifindex, bool lock_sk)
{
	int ret;

	if (lock_sk)
		lock_sock(sk);
	ret = sock_bindtoindex_locked(sk, ifindex);
	if (lock_sk)
		release_sock(sk);

	return ret;
}
EXPORT_SYMBOL(sock_bindtoindex);

static int sock_setbindtodevice(struct sock *sk, sockptr_t optval, int optlen)
{
	int ret = -ENOPROTOOPT;
#ifdef CONFIG_NETDEVICES
	struct net *net = sock_net(sk);
	char devname[IFNAMSIZ];
	int index;

	ret = -EINVAL;
	if (optlen < 0)
		goto out;

	/* Bind this socket to a particular device like "eth0",
	 * as specified in the passed interface name. If the
	 * name is "" or the option length is zero the socket
	 * is not bound.
	 */
	if (optlen > IFNAMSIZ - 1)
		optlen = IFNAMSIZ - 1;
	memset(devname, 0, sizeof(devname));

	ret = -EFAULT;
	if (copy_from_sockptr(devname, optval, optlen))
		goto out;

	index = 0;
	if (devname[0] != '\0') {
		struct net_device *dev;

		rcu_read_lock();
		dev = dev_get_by_name_rcu(net, devname);
		if (dev)
			index = dev->ifindex;
		rcu_read_unlock();
		ret = -ENODEV;
		if (!dev)
			goto out;
	}

	sockopt_lock_sock(sk);
	ret = sock_bindtoindex_locked(sk, index);
	sockopt_release_sock(sk);
out:
#endif

	return ret;
}

static int sock_getbindtodevice(struct sock *sk, sockptr_t optval,
				sockptr_t optlen, int len)
{
	int ret = -ENOPROTOOPT;
#ifdef CONFIG_NETDEVICES
	int bound_dev_if = READ_ONCE(sk->sk_bound_dev_if);
	struct net *net = sock_net(sk);
	char devname[IFNAMSIZ];

	if (bound_dev_if == 0) {
		len = 0;
		goto zero;
	}

	ret = -EINVAL;
	if (len < IFNAMSIZ)
		goto out;

	ret = netdev_get_name(net, devname, bound_dev_if);
	if (ret)
		goto out;

	len = strlen(devname) + 1;

	ret = -EFAULT;
	if (copy_to_sockptr(optval, devname, len))
		goto out;

zero:
	ret = -EFAULT;
	if (copy_to_sockptr(optlen, &len, sizeof(int)))
		goto out;

	ret = 0;

out:
#endif

	return ret;
}

bool sk_mc_loop(const struct sock *sk)
{
	if (dev_recursion_level())
		return false;
	if (!sk)
		return true;
	/* IPV6_ADDRFORM can change sk->sk_family under us. */
	switch (READ_ONCE(sk->sk_family)) {
	case AF_INET:
		return inet_test_bit(MC_LOOP, sk);
#if IS_ENABLED(CONFIG_IPV6)
	case AF_INET6:
		return inet6_test_bit(MC6_LOOP, sk);
#endif
	}
	WARN_ON_ONCE(1);
	return true;
}
EXPORT_SYMBOL(sk_mc_loop);

void sock_set_reuseaddr(struct sock *sk)
{
	lock_sock(sk);
	sk->sk_reuse = SK_CAN_REUSE;
	release_sock(sk);
}
EXPORT_SYMBOL(sock_set_reuseaddr);

void sock_set_reuseport(struct sock *sk)
{
	lock_sock(sk);
	sk->sk_reuseport = true;
	release_sock(sk);
}
EXPORT_SYMBOL(sock_set_reuseport);

void sock_no_linger(struct sock *sk)
{
	lock_sock(sk);
	WRITE_ONCE(sk->sk_lingertime, 0);
	sock_set_flag(sk, SOCK_LINGER);
	release_sock(sk);
}
EXPORT_SYMBOL(sock_no_linger);

void sock_set_priority(struct sock *sk, u32 priority)
{
	WRITE_ONCE(sk->sk_priority, priority);
}
EXPORT_SYMBOL(sock_set_priority);

void sock_set_sndtimeo(struct sock *sk, s64 secs)
{
	lock_sock(sk);
	if (secs && secs < MAX_SCHEDULE_TIMEOUT / HZ - 1)
		WRITE_ONCE(sk->sk_sndtimeo, secs * HZ);
	else
		WRITE_ONCE(sk->sk_sndtimeo, MAX_SCHEDULE_TIMEOUT);
	release_sock(sk);
}
EXPORT_SYMBOL(sock_set_sndtimeo);

static void __sock_set_timestamps(struct sock *sk, bool val, bool new, bool ns)
{
	if (val)  {
		sock_valbool_flag(sk, SOCK_TSTAMP_NEW, new);
		sock_valbool_flag(sk, SOCK_RCVTSTAMPNS, ns);
		sock_set_flag(sk, SOCK_RCVTSTAMP);
		sock_enable_timestamp(sk, SOCK_TIMESTAMP);
	} else {
		sock_reset_flag(sk, SOCK_RCVTSTAMP);
		sock_reset_flag(sk, SOCK_RCVTSTAMPNS);
	}
}

void sock_enable_timestamps(struct sock *sk)
{
	lock_sock(sk);
	__sock_set_timestamps(sk, true, false, true);
	release_sock(sk);
}
EXPORT_SYMBOL(sock_enable_timestamps);

void sock_set_timestamp(struct sock *sk, int optname, bool valbool)
{
	switch (optname) {
	case SO_TIMESTAMP_OLD:
		__sock_set_timestamps(sk, valbool, false, false);
		break;
	case SO_TIMESTAMP_NEW:
		__sock_set_timestamps(sk, valbool, true, false);
		break;
	case SO_TIMESTAMPNS_OLD:
		__sock_set_timestamps(sk, valbool, false, true);
		break;
	case SO_TIMESTAMPNS_NEW:
		__sock_set_timestamps(sk, valbool, true, true);
		break;
	}
}

static int sock_timestamping_bind_phc(struct sock *sk, int phc_index)
{
	struct net *net = sock_net(sk);
	struct net_device *dev = NULL;
	bool match = false;
	int *vclock_index;
	int i, num;

	if (sk->sk_bound_dev_if)
		dev = dev_get_by_index(net, sk->sk_bound_dev_if);

	if (!dev) {
		pr_err("%s: sock not bind to device\n", __func__);
		return -EOPNOTSUPP;
	}

	num = ethtool_get_phc_vclocks(dev, &vclock_index);
	dev_put(dev);

	for (i = 0; i < num; i++) {
		if (*(vclock_index + i) == phc_index) {
			match = true;
			break;
		}
	}

	if (num > 0)
		kfree(vclock_index);

	if (!match)
		return -EINVAL;

	WRITE_ONCE(sk->sk_bind_phc, phc_index);

	return 0;
}

int sock_set_timestamping(struct sock *sk, int optname,
			  struct so_timestamping timestamping)
{
	int val = timestamping.flags;
	int ret;

	if (val & ~SOF_TIMESTAMPING_MASK)
		return -EINVAL;

	if (val & SOF_TIMESTAMPING_OPT_ID_TCP &&
	    !(val & SOF_TIMESTAMPING_OPT_ID))
		return -EINVAL;

	if (val & SOF_TIMESTAMPING_OPT_ID &&
	    !(sk->sk_tsflags & SOF_TIMESTAMPING_OPT_ID)) {
		if (sk_is_tcp(sk)) {
			if ((1 << sk->sk_state) &
			    (TCPF_CLOSE | TCPF_LISTEN))
				return -EINVAL;
			if (val & SOF_TIMESTAMPING_OPT_ID_TCP)
				atomic_set(&sk->sk_tskey, tcp_sk(sk)->write_seq);
			else
				atomic_set(&sk->sk_tskey, tcp_sk(sk)->snd_una);
		} else {
			atomic_set(&sk->sk_tskey, 0);
		}
	}

	if (val & SOF_TIMESTAMPING_OPT_STATS &&
	    !(val & SOF_TIMESTAMPING_OPT_TSONLY))
		return -EINVAL;

	if (val & SOF_TIMESTAMPING_BIND_PHC) {
		ret = sock_timestamping_bind_phc(sk, timestamping.bind_phc);
		if (ret)
			return ret;
	}

	WRITE_ONCE(sk->sk_tsflags, val);
	sock_valbool_flag(sk, SOCK_TSTAMP_NEW, optname == SO_TIMESTAMPING_NEW);

	if (val & SOF_TIMESTAMPING_RX_SOFTWARE)
		sock_enable_timestamp(sk,
				      SOCK_TIMESTAMPING_RX_SOFTWARE);
	else
		sock_disable_timestamp(sk,
				       (1UL << SOCK_TIMESTAMPING_RX_SOFTWARE));
	return 0;
}

void sock_set_keepalive(struct sock *sk)
{
	lock_sock(sk);
	if (sk->sk_prot->keepalive)
		sk->sk_prot->keepalive(sk, true);
	sock_valbool_flag(sk, SOCK_KEEPOPEN, true);
	release_sock(sk);
}
EXPORT_SYMBOL(sock_set_keepalive);

static void __sock_set_rcvbuf(struct sock *sk, int val)
{
	/* Ensure val * 2 fits into an int, to prevent max_t() from treating it
	 * as a negative value.
	 */
	val = min_t(int, val, INT_MAX / 2);
	sk->sk_userlocks |= SOCK_RCVBUF_LOCK;

	/* We double it on the way in to account for "struct sk_buff" etc.
	 * overhead.   Applications assume that the SO_RCVBUF setting they make
	 * will allow that much actual data to be received on that socket.
	 *
	 * Applications are unaware that "struct sk_buff" and other overheads
	 * allocate from the receive buffer during socket buffer allocation.
	 *
	 * And after considering the possible alternatives, returning the value
	 * we actually used in getsockopt is the most desirable behavior.
	 */
	WRITE_ONCE(sk->sk_rcvbuf, max_t(int, val * 2, SOCK_MIN_RCVBUF));
}

void sock_set_rcvbuf(struct sock *sk, int val)
{
	lock_sock(sk);
	__sock_set_rcvbuf(sk, val);
	release_sock(sk);
}
EXPORT_SYMBOL(sock_set_rcvbuf);

static void __sock_set_mark(struct sock *sk, u32 val)
{
	if (val != sk->sk_mark) {
		WRITE_ONCE(sk->sk_mark, val);
		sk_dst_reset(sk);
	}
}

void sock_set_mark(struct sock *sk, u32 val)
{
	lock_sock(sk);
	__sock_set_mark(sk, val);
	release_sock(sk);
}
EXPORT_SYMBOL(sock_set_mark);

static void sock_release_reserved_memory(struct sock *sk, int bytes)
{
	/* Round down bytes to multiple of pages */
	bytes = round_down(bytes, PAGE_SIZE);

	WARN_ON(bytes > sk->sk_reserved_mem);
	WRITE_ONCE(sk->sk_reserved_mem, sk->sk_reserved_mem - bytes);
	sk_mem_reclaim(sk);
}

static int sock_reserve_memory(struct sock *sk, int bytes)
{
	long allocated;
	bool charged;
	int pages;

	if (!mem_cgroup_sockets_enabled || !sk->sk_memcg || !sk_has_account(sk))
		return -EOPNOTSUPP;

	if (!bytes)
		return 0;

	pages = sk_mem_pages(bytes);

	/* pre-charge to memcg */
	charged = mem_cgroup_charge_skmem(sk->sk_memcg, pages,
					  GFP_KERNEL | __GFP_RETRY_MAYFAIL);
	if (!charged)
		return -ENOMEM;

	/* pre-charge to forward_alloc */
	sk_memory_allocated_add(sk, pages);
	allocated = sk_memory_allocated(sk);
	/* If the system goes into memory pressure with this
	 * precharge, give up and return error.
	 */
	if (allocated > sk_prot_mem_limits(sk, 1)) {
		sk_memory_allocated_sub(sk, pages);
		mem_cgroup_uncharge_skmem(sk->sk_memcg, pages);
		return -ENOMEM;
	}
	sk_forward_alloc_add(sk, pages << PAGE_SHIFT);

	WRITE_ONCE(sk->sk_reserved_mem,
		   sk->sk_reserved_mem + (pages << PAGE_SHIFT));

	return 0;
}

void sockopt_lock_sock(struct sock *sk)
{
	/* When current->bpf_ctx is set, the setsockopt is called from
	 * a bpf prog.  bpf has ensured the sk lock has been
	 * acquired before calling setsockopt().
	 */
	if (has_current_bpf_ctx())
		return;

	lock_sock(sk);
}
EXPORT_SYMBOL(sockopt_lock_sock);

void sockopt_release_sock(struct sock *sk)
{
	if (has_current_bpf_ctx())
		return;

	release_sock(sk);
}
EXPORT_SYMBOL(sockopt_release_sock);

bool sockopt_ns_capable(struct user_namespace *ns, int cap)
{
	return has_current_bpf_ctx() || ns_capable(ns, cap);
}
EXPORT_SYMBOL(sockopt_ns_capable);

bool sockopt_capable(int cap)
{
	return has_current_bpf_ctx() || capable(cap);
}
EXPORT_SYMBOL(sockopt_capable);

/*
 *	This is meant for all protocols to use and covers goings on
 *	at the socket level. Everything here is generic.
 */

int sk_setsockopt(struct sock *sk, int level, int optname,
		  sockptr_t optval, unsigned int optlen)
{
	struct so_timestamping timestamping;
	struct socket *sock = sk->sk_socket;
	struct sock_txtime sk_txtime;
	int val;
	int valbool;
	struct linger ling;
	int ret = 0;

	/*
	 *	Options without arguments
	 */

	if (optname == SO_BINDTODEVICE)
		return sock_setbindtodevice(sk, optval, optlen);

	if (optlen < sizeof(int))
		return -EINVAL;

	if (copy_from_sockptr(&val, optval, sizeof(val)))
		return -EFAULT;

	valbool = val ? 1 : 0;

	/* handle options which do not require locking the socket. */
	switch (optname) {
	case SO_PRIORITY:
		if ((val >= 0 && val <= 6) ||
		    sockopt_ns_capable(sock_net(sk)->user_ns, CAP_NET_RAW) ||
		    sockopt_ns_capable(sock_net(sk)->user_ns, CAP_NET_ADMIN)) {
			sock_set_priority(sk, val);
			return 0;
		}
		return -EPERM;
	case SO_PASSSEC:
		assign_bit(SOCK_PASSSEC, &sock->flags, valbool);
		return 0;
	case SO_PASSCRED:
		assign_bit(SOCK_PASSCRED, &sock->flags, valbool);
		return 0;
	case SO_PASSPIDFD:
		assign_bit(SOCK_PASSPIDFD, &sock->flags, valbool);
		return 0;
	case SO_TYPE:
	case SO_PROTOCOL:
	case SO_DOMAIN:
	case SO_ERROR:
		return -ENOPROTOOPT;
#ifdef CONFIG_NET_RX_BUSY_POLL
	case SO_BUSY_POLL:
		if (val < 0)
			return -EINVAL;
		WRITE_ONCE(sk->sk_ll_usec, val);
		return 0;
	case SO_PREFER_BUSY_POLL:
		if (valbool && !sockopt_capable(CAP_NET_ADMIN))
			return -EPERM;
		WRITE_ONCE(sk->sk_prefer_busy_poll, valbool);
		return 0;
	case SO_BUSY_POLL_BUDGET:
		if (val > READ_ONCE(sk->sk_busy_poll_budget) &&
		    !sockopt_capable(CAP_NET_ADMIN))
			return -EPERM;
		if (val < 0 || val > U16_MAX)
			return -EINVAL;
		WRITE_ONCE(sk->sk_busy_poll_budget, val);
		return 0;
#endif
	case SO_MAX_PACING_RATE:
		{
		unsigned long ulval = (val == ~0U) ? ~0UL : (unsigned int)val;
		unsigned long pacing_rate;

		if (sizeof(ulval) != sizeof(val) &&
		    optlen >= sizeof(ulval) &&
		    copy_from_sockptr(&ulval, optval, sizeof(ulval))) {
			return -EFAULT;
		}
		if (ulval != ~0UL)
			cmpxchg(&sk->sk_pacing_status,
				SK_PACING_NONE,
				SK_PACING_NEEDED);
		/* Pairs with READ_ONCE() from sk_getsockopt() */
		WRITE_ONCE(sk->sk_max_pacing_rate, ulval);
		pacing_rate = READ_ONCE(sk->sk_pacing_rate);
		if (ulval < pacing_rate)
			WRITE_ONCE(sk->sk_pacing_rate, ulval);
		return 0;
		}
	case SO_TXREHASH:
		if (val < -1 || val > 1)
			return -EINVAL;
		if ((u8)val == SOCK_TXREHASH_DEFAULT)
			val = READ_ONCE(sock_net(sk)->core.sysctl_txrehash);
		/* Paired with READ_ONCE() in tcp_rtx_synack()
		 * and sk_getsockopt().
		 */
		WRITE_ONCE(sk->sk_txrehash, (u8)val);
		return 0;
	case SO_PEEK_OFF:
		{
		int (*set_peek_off)(struct sock *sk, int val);

		set_peek_off = READ_ONCE(sock->ops)->set_peek_off;
		if (set_peek_off)
			ret = set_peek_off(sk, val);
		else
			ret = -EOPNOTSUPP;
		return ret;
		}
	}

	sockopt_lock_sock(sk);

	switch (optname) {
	case SO_DEBUG:
		if (val && !sockopt_capable(CAP_NET_ADMIN))
			ret = -EACCES;
		else
			sock_valbool_flag(sk, SOCK_DBG, valbool);
		break;
	case SO_REUSEADDR:
		sk->sk_reuse = (valbool ? SK_CAN_REUSE : SK_NO_REUSE);
		break;
	case SO_REUSEPORT:
		sk->sk_reuseport = valbool;
		break;
	case SO_DONTROUTE:
		sock_valbool_flag(sk, SOCK_LOCALROUTE, valbool);
		sk_dst_reset(sk);
		break;
	case SO_BROADCAST:
		sock_valbool_flag(sk, SOCK_BROADCAST, valbool);
		break;
	case SO_SNDBUF:
		/* Don't error on this BSD doesn't and if you think
		 * about it this is right. Otherwise apps have to
		 * play 'guess the biggest size' games. RCVBUF/SNDBUF
		 * are treated in BSD as hints
		 */
		val = min_t(u32, val, READ_ONCE(sysctl_wmem_max));
set_sndbuf:
		/* Ensure val * 2 fits into an int, to prevent max_t()
		 * from treating it as a negative value.
		 */
		val = min_t(int, val, INT_MAX / 2);
		sk->sk_userlocks |= SOCK_SNDBUF_LOCK;
		WRITE_ONCE(sk->sk_sndbuf,
			   max_t(int, val * 2, SOCK_MIN_SNDBUF));
		/* Wake up sending tasks if we upped the value. */
		sk->sk_write_space(sk);
		break;

	case SO_SNDBUFFORCE:
		if (!sockopt_capable(CAP_NET_ADMIN)) {
			ret = -EPERM;
			break;
		}

		/* No negative values (to prevent underflow, as val will be
		 * multiplied by 2).
		 */
		if (val < 0)
			val = 0;
		goto set_sndbuf;

	case SO_RCVBUF:
		/* Don't error on this BSD doesn't and if you think
		 * about it this is right. Otherwise apps have to
		 * play 'guess the biggest size' games. RCVBUF/SNDBUF
		 * are treated in BSD as hints
		 */
		__sock_set_rcvbuf(sk, min_t(u32, val, READ_ONCE(sysctl_rmem_max)));
		break;

	case SO_RCVBUFFORCE:
		if (!sockopt_capable(CAP_NET_ADMIN)) {
			ret = -EPERM;
			break;
		}

		/* No negative values (to prevent underflow, as val will be
		 * multiplied by 2).
		 */
		__sock_set_rcvbuf(sk, max(val, 0));
		break;

	case SO_KEEPALIVE:
		if (sk->sk_prot->keepalive)
			sk->sk_prot->keepalive(sk, valbool);
		sock_valbool_flag(sk, SOCK_KEEPOPEN, valbool);
		break;

	case SO_OOBINLINE:
		sock_valbool_flag(sk, SOCK_URGINLINE, valbool);
		break;

	case SO_NO_CHECK:
		sk->sk_no_check_tx = valbool;
		break;

	case SO_LINGER:
		if (optlen < sizeof(ling)) {
			ret = -EINVAL;	/* 1003.1g */
			break;
		}
		if (copy_from_sockptr(&ling, optval, sizeof(ling))) {
			ret = -EFAULT;
			break;
		}
		if (!ling.l_onoff) {
			sock_reset_flag(sk, SOCK_LINGER);
		} else {
			unsigned long t_sec = ling.l_linger;

			if (t_sec >= MAX_SCHEDULE_TIMEOUT / HZ)
				WRITE_ONCE(sk->sk_lingertime, MAX_SCHEDULE_TIMEOUT);
			else
				WRITE_ONCE(sk->sk_lingertime, t_sec * HZ);
			sock_set_flag(sk, SOCK_LINGER);
		}
		break;

	case SO_BSDCOMPAT:
		break;

	case SO_TIMESTAMP_OLD:
	case SO_TIMESTAMP_NEW:
	case SO_TIMESTAMPNS_OLD:
	case SO_TIMESTAMPNS_NEW:
		sock_set_timestamp(sk, optname, valbool);
		break;

	case SO_TIMESTAMPING_NEW:
	case SO_TIMESTAMPING_OLD:
		if (optlen == sizeof(timestamping)) {
			if (copy_from_sockptr(&timestamping, optval,
					      sizeof(timestamping))) {
				ret = -EFAULT;
				break;
			}
		} else {
			memset(&timestamping, 0, sizeof(timestamping));
			timestamping.flags = val;
		}
		ret = sock_set_timestamping(sk, optname, timestamping);
		break;

	case SO_RCVLOWAT:
		{
		int (*set_rcvlowat)(struct sock *sk, int val) = NULL;

		if (val < 0)
			val = INT_MAX;
		if (sock)
			set_rcvlowat = READ_ONCE(sock->ops)->set_rcvlowat;
		if (set_rcvlowat)
			ret = set_rcvlowat(sk, val);
		else
			WRITE_ONCE(sk->sk_rcvlowat, val ? : 1);
		break;
		}
	case SO_RCVTIMEO_OLD:
	case SO_RCVTIMEO_NEW:
		ret = sock_set_timeout(&sk->sk_rcvtimeo, optval,
				       optlen, optname == SO_RCVTIMEO_OLD);
		break;

	case SO_SNDTIMEO_OLD:
	case SO_SNDTIMEO_NEW:
		ret = sock_set_timeout(&sk->sk_sndtimeo, optval,
				       optlen, optname == SO_SNDTIMEO_OLD);
		break;

	case SO_ATTACH_FILTER: {
		struct sock_fprog fprog;

		ret = copy_bpf_fprog_from_user(&fprog, optval, optlen);
		if (!ret)
			ret = sk_attach_filter(&fprog, sk);
		break;
	}
	case SO_ATTACH_BPF:
		ret = -EINVAL;
		if (optlen == sizeof(u32)) {
			u32 ufd;

			ret = -EFAULT;
			if (copy_from_sockptr(&ufd, optval, sizeof(ufd)))
				break;

			ret = sk_attach_bpf(ufd, sk);
		}
		break;

	case SO_ATTACH_REUSEPORT_CBPF: {
		struct sock_fprog fprog;

		ret = copy_bpf_fprog_from_user(&fprog, optval, optlen);
		if (!ret)
			ret = sk_reuseport_attach_filter(&fprog, sk);
		break;
	}
	case SO_ATTACH_REUSEPORT_EBPF:
		ret = -EINVAL;
		if (optlen == sizeof(u32)) {
			u32 ufd;

			ret = -EFAULT;
			if (copy_from_sockptr(&ufd, optval, sizeof(ufd)))
				break;

			ret = sk_reuseport_attach_bpf(ufd, sk);
		}
		break;

	case SO_DETACH_REUSEPORT_BPF:
		ret = reuseport_detach_prog(sk);
		break;

	case SO_DETACH_FILTER:
		ret = sk_detach_filter(sk);
		break;

	case SO_LOCK_FILTER:
		if (sock_flag(sk, SOCK_FILTER_LOCKED) && !valbool)
			ret = -EPERM;
		else
			sock_valbool_flag(sk, SOCK_FILTER_LOCKED, valbool);
		break;

	case SO_MARK:
		if (!sockopt_ns_capable(sock_net(sk)->user_ns, CAP_NET_RAW) &&
		    !sockopt_ns_capable(sock_net(sk)->user_ns, CAP_NET_ADMIN)) {
			ret = -EPERM;
			break;
		}

		__sock_set_mark(sk, val);
		break;
	case SO_RCVMARK:
		sock_valbool_flag(sk, SOCK_RCVMARK, valbool);
		break;

	case SO_RXQ_OVFL:
		sock_valbool_flag(sk, SOCK_RXQ_OVFL, valbool);
		break;

	case SO_WIFI_STATUS:
		sock_valbool_flag(sk, SOCK_WIFI_STATUS, valbool);
		break;

	case SO_NOFCS:
		sock_valbool_flag(sk, SOCK_NOFCS, valbool);
		break;

	case SO_SELECT_ERR_QUEUE:
		sock_valbool_flag(sk, SOCK_SELECT_ERR_QUEUE, valbool);
		break;


	case SO_INCOMING_CPU:
		reuseport_update_incoming_cpu(sk, val);
		break;

	case SO_CNX_ADVICE:
		if (val == 1)
			dst_negative_advice(sk);
		break;

	case SO_ZEROCOPY:
		if (sk->sk_family == PF_INET || sk->sk_family == PF_INET6) {
			if (!(sk_is_tcp(sk) ||
			      (sk->sk_type == SOCK_DGRAM &&
			       sk->sk_protocol == IPPROTO_UDP)))
				ret = -EOPNOTSUPP;
		} else if (sk->sk_family != PF_RDS) {
			ret = -EOPNOTSUPP;
		}
		if (!ret) {
			if (val < 0 || val > 1)
				ret = -EINVAL;
			else
				sock_valbool_flag(sk, SOCK_ZEROCOPY, valbool);
		}
		break;

	case SO_TXTIME:
		if (optlen != sizeof(struct sock_txtime)) {
			ret = -EINVAL;
			break;
		} else if (copy_from_sockptr(&sk_txtime, optval,
			   sizeof(struct sock_txtime))) {
			ret = -EFAULT;
			break;
		} else if (sk_txtime.flags & ~SOF_TXTIME_FLAGS_MASK) {
			ret = -EINVAL;
			break;
		}
		/* CLOCK_MONOTONIC is only used by sch_fq, and this packet
		 * scheduler has enough safe guards.
		 */
		if (sk_txtime.clockid != CLOCK_MONOTONIC &&
		    !sockopt_ns_capable(sock_net(sk)->user_ns, CAP_NET_ADMIN)) {
			ret = -EPERM;
			break;
		}
		sock_valbool_flag(sk, SOCK_TXTIME, true);
		sk->sk_clockid = sk_txtime.clockid;
		sk->sk_txtime_deadline_mode =
			!!(sk_txtime.flags & SOF_TXTIME_DEADLINE_MODE);
		sk->sk_txtime_report_errors =
			!!(sk_txtime.flags & SOF_TXTIME_REPORT_ERRORS);
		break;

	case SO_BINDTOIFINDEX:
		ret = sock_bindtoindex_locked(sk, val);
		break;

	case SO_BUF_LOCK:
		if (val & ~SOCK_BUF_LOCK_MASK) {
			ret = -EINVAL;
			break;
		}
		sk->sk_userlocks = val | (sk->sk_userlocks &
					  ~SOCK_BUF_LOCK_MASK);
		break;

	case SO_RESERVE_MEM:
	{
		int delta;

		if (val < 0) {
			ret = -EINVAL;
			break;
		}

		delta = val - sk->sk_reserved_mem;
		if (delta < 0)
			sock_release_reserved_memory(sk, -delta);
		else
			ret = sock_reserve_memory(sk, delta);
		break;
	}

	default:
		ret = -ENOPROTOOPT;
		break;
	}
	sockopt_release_sock(sk);
	return ret;
}

int sock_setsockopt(struct socket *sock, int level, int optname,
		    sockptr_t optval, unsigned int optlen)
{
	return sk_setsockopt(sock->sk, level, optname,
			     optval, optlen);
}
EXPORT_SYMBOL(sock_setsockopt);

static const struct cred *sk_get_peer_cred(struct sock *sk)
{
	const struct cred *cred;

	spin_lock(&sk->sk_peer_lock);
	cred = get_cred(sk->sk_peer_cred);
	spin_unlock(&sk->sk_peer_lock);

	return cred;
}

static void cred_to_ucred(struct pid *pid, const struct cred *cred,
			  struct ucred *ucred)
{
	ucred->pid = pid_vnr(pid);
	ucred->uid = ucred->gid = -1;
	if (cred) {
		struct user_namespace *current_ns = current_user_ns();

		ucred->uid = from_kuid_munged(current_ns, cred->euid);
		ucred->gid = from_kgid_munged(current_ns, cred->egid);
	}
}

static int groups_to_user(sockptr_t dst, const struct group_info *src)
{
	struct user_namespace *user_ns = current_user_ns();
	int i;

	for (i = 0; i < src->ngroups; i++) {
		gid_t gid = from_kgid_munged(user_ns, src->gid[i]);

		if (copy_to_sockptr_offset(dst, i * sizeof(gid), &gid, sizeof(gid)))
			return -EFAULT;
	}

	return 0;
}

int sk_getsockopt(struct sock *sk, int level, int optname,
		  sockptr_t optval, sockptr_t optlen)
{
	struct socket *sock = sk->sk_socket;

	union {
		int val;
		u64 val64;
		unsigned long ulval;
		struct linger ling;
		struct old_timeval32 tm32;
		struct __kernel_old_timeval tm;
		struct  __kernel_sock_timeval stm;
		struct sock_txtime txtime;
		struct so_timestamping timestamping;
	} v;

	int lv = sizeof(int);
	int len;

	if (copy_from_sockptr(&len, optlen, sizeof(int)))
		return -EFAULT;
	if (len < 0)
		return -EINVAL;

	memset(&v, 0, sizeof(v));

	switch (optname) {
	case SO_DEBUG:
		v.val = sock_flag(sk, SOCK_DBG);
		break;

	case SO_DONTROUTE:
		v.val = sock_flag(sk, SOCK_LOCALROUTE);
		break;

	case SO_BROADCAST:
		v.val = sock_flag(sk, SOCK_BROADCAST);
		break;

	case SO_SNDBUF:
		v.val = READ_ONCE(sk->sk_sndbuf);
		break;

	case SO_RCVBUF:
		v.val = READ_ONCE(sk->sk_rcvbuf);
		break;

	case SO_REUSEADDR:
		v.val = sk->sk_reuse;
		break;

	case SO_REUSEPORT:
		v.val = sk->sk_reuseport;
		break;

	case SO_KEEPALIVE:
		v.val = sock_flag(sk, SOCK_KEEPOPEN);
		break;

	case SO_TYPE:
		v.val = sk->sk_type;
		break;

	case SO_PROTOCOL:
		v.val = sk->sk_protocol;
		break;

	case SO_DOMAIN:
		v.val = sk->sk_family;
		break;

	case SO_ERROR:
		v.val = -sock_error(sk);
		if (v.val == 0)
			v.val = xchg(&sk->sk_err_soft, 0);
		break;

	case SO_OOBINLINE:
		v.val = sock_flag(sk, SOCK_URGINLINE);
		break;

	case SO_NO_CHECK:
		v.val = sk->sk_no_check_tx;
		break;

	case SO_PRIORITY:
		v.val = READ_ONCE(sk->sk_priority);
		break;

	case SO_LINGER:
		lv		= sizeof(v.ling);
		v.ling.l_onoff	= sock_flag(sk, SOCK_LINGER);
		v.ling.l_linger	= READ_ONCE(sk->sk_lingertime) / HZ;
		break;

	case SO_BSDCOMPAT:
		break;

	case SO_TIMESTAMP_OLD:
		v.val = sock_flag(sk, SOCK_RCVTSTAMP) &&
				!sock_flag(sk, SOCK_TSTAMP_NEW) &&
				!sock_flag(sk, SOCK_RCVTSTAMPNS);
		break;

	case SO_TIMESTAMPNS_OLD:
		v.val = sock_flag(sk, SOCK_RCVTSTAMPNS) && !sock_flag(sk, SOCK_TSTAMP_NEW);
		break;

	case SO_TIMESTAMP_NEW:
		v.val = sock_flag(sk, SOCK_RCVTSTAMP) && sock_flag(sk, SOCK_TSTAMP_NEW);
		break;

	case SO_TIMESTAMPNS_NEW:
		v.val = sock_flag(sk, SOCK_RCVTSTAMPNS) && sock_flag(sk, SOCK_TSTAMP_NEW);
		break;

	case SO_TIMESTAMPING_OLD:
	case SO_TIMESTAMPING_NEW:
		lv = sizeof(v.timestamping);
		/* For the later-added case SO_TIMESTAMPING_NEW: Be strict about only
		 * returning the flags when they were set through the same option.
		 * Don't change the beviour for the old case SO_TIMESTAMPING_OLD.
		 */
		if (optname == SO_TIMESTAMPING_OLD || sock_flag(sk, SOCK_TSTAMP_NEW)) {
			v.timestamping.flags = READ_ONCE(sk->sk_tsflags);
			v.timestamping.bind_phc = READ_ONCE(sk->sk_bind_phc);
		}
		break;

	case SO_RCVTIMEO_OLD:
	case SO_RCVTIMEO_NEW:
		lv = sock_get_timeout(READ_ONCE(sk->sk_rcvtimeo), &v,
				      SO_RCVTIMEO_OLD == optname);
		break;

	case SO_SNDTIMEO_OLD:
	case SO_SNDTIMEO_NEW:
		lv = sock_get_timeout(READ_ONCE(sk->sk_sndtimeo), &v,
				      SO_SNDTIMEO_OLD == optname);
		break;

	case SO_RCVLOWAT:
		v.val = READ_ONCE(sk->sk_rcvlowat);
		break;

	case SO_SNDLOWAT:
		v.val = 1;
		break;

	case SO_PASSCRED:
		v.val = !!test_bit(SOCK_PASSCRED, &sock->flags);
		break;

	case SO_PASSPIDFD:
		v.val = !!test_bit(SOCK_PASSPIDFD, &sock->flags);
		break;

	case SO_PEERCRED:
	{
		struct ucred peercred;
		if (len > sizeof(peercred))
			len = sizeof(peercred);

		spin_lock(&sk->sk_peer_lock);
		cred_to_ucred(sk->sk_peer_pid, sk->sk_peer_cred, &peercred);
		spin_unlock(&sk->sk_peer_lock);

		if (copy_to_sockptr(optval, &peercred, len))
			return -EFAULT;
		goto lenout;
	}

	case SO_PEERPIDFD:
	{
		struct pid *peer_pid;
		struct file *pidfd_file = NULL;
		int pidfd;

		if (len > sizeof(pidfd))
			len = sizeof(pidfd);

		spin_lock(&sk->sk_peer_lock);
		peer_pid = get_pid(sk->sk_peer_pid);
		spin_unlock(&sk->sk_peer_lock);

		if (!peer_pid)
			return -ENODATA;

		pidfd = pidfd_prepare(peer_pid, 0, &pidfd_file);
		put_pid(peer_pid);
		if (pidfd < 0)
			return pidfd;

		if (copy_to_sockptr(optval, &pidfd, len) ||
		    copy_to_sockptr(optlen, &len, sizeof(int))) {
			put_unused_fd(pidfd);
			fput(pidfd_file);

			return -EFAULT;
		}

		fd_install(pidfd, pidfd_file);
		return 0;
	}

	case SO_PEERGROUPS:
	{
		const struct cred *cred;
		int ret, n;

		cred = sk_get_peer_cred(sk);
		if (!cred)
			return -ENODATA;

		n = cred->group_info->ngroups;
		if (len < n * sizeof(gid_t)) {
			len = n * sizeof(gid_t);
			put_cred(cred);
			return copy_to_sockptr(optlen, &len, sizeof(int)) ? -EFAULT : -ERANGE;
		}
		len = n * sizeof(gid_t);

		ret = groups_to_user(optval, cred->group_info);
		put_cred(cred);
		if (ret)
			return ret;
		goto lenout;
	}

	case SO_PEERNAME:
	{
		struct sockaddr_storage address;

		lv = READ_ONCE(sock->ops)->getname(sock, (struct sockaddr *)&address, 2);
		if (lv < 0)
			return -ENOTCONN;
		if (lv < len)
			return -EINVAL;
		if (copy_to_sockptr(optval, &address, len))
			return -EFAULT;
		goto lenout;
	}

	/* Dubious BSD thing... Probably nobody even uses it, but
	 * the UNIX standard wants it for whatever reason... -DaveM
	 */
	case SO_ACCEPTCONN:
		v.val = sk->sk_state == TCP_LISTEN;
		break;

	case SO_PASSSEC:
		v.val = !!test_bit(SOCK_PASSSEC, &sock->flags);
		break;

	case SO_PEERSEC:
		return security_socket_getpeersec_stream(sock,
							 optval, optlen, len);

	case SO_MARK:
		v.val = READ_ONCE(sk->sk_mark);
		break;

	case SO_RCVMARK:
		v.val = sock_flag(sk, SOCK_RCVMARK);
		break;

	case SO_RXQ_OVFL:
		v.val = sock_flag(sk, SOCK_RXQ_OVFL);
		break;

	case SO_WIFI_STATUS:
		v.val = sock_flag(sk, SOCK_WIFI_STATUS);
		break;

	case SO_PEEK_OFF:
		if (!READ_ONCE(sock->ops)->set_peek_off)
			return -EOPNOTSUPP;

		v.val = READ_ONCE(sk->sk_peek_off);
		break;
	case SO_NOFCS:
		v.val = sock_flag(sk, SOCK_NOFCS);
		break;

	case SO_BINDTODEVICE:
		return sock_getbindtodevice(sk, optval, optlen, len);

	case SO_GET_FILTER:
		len = sk_get_filter(sk, optval, len);
		if (len < 0)
			return len;

		goto lenout;

	case SO_LOCK_FILTER:
		v.val = sock_flag(sk, SOCK_FILTER_LOCKED);
		break;

	case SO_BPF_EXTENSIONS:
		v.val = bpf_tell_extensions();
		break;

	case SO_SELECT_ERR_QUEUE:
		v.val = sock_flag(sk, SOCK_SELECT_ERR_QUEUE);
		break;

#ifdef CONFIG_NET_RX_BUSY_POLL
	case SO_BUSY_POLL:
		v.val = READ_ONCE(sk->sk_ll_usec);
		break;
	case SO_PREFER_BUSY_POLL:
		v.val = READ_ONCE(sk->sk_prefer_busy_poll);
		break;
#endif

	case SO_MAX_PACING_RATE:
		/* The READ_ONCE() pair with the WRITE_ONCE() in sk_setsockopt() */
		if (sizeof(v.ulval) != sizeof(v.val) && len >= sizeof(v.ulval)) {
			lv = sizeof(v.ulval);
			v.ulval = READ_ONCE(sk->sk_max_pacing_rate);
		} else {
			/* 32bit version */
			v.val = min_t(unsigned long, ~0U,
				      READ_ONCE(sk->sk_max_pacing_rate));
		}
		break;

	case SO_INCOMING_CPU:
		v.val = READ_ONCE(sk->sk_incoming_cpu);
		break;

	case SO_MEMINFO:
	{
		u32 meminfo[SK_MEMINFO_VARS];

		sk_get_meminfo(sk, meminfo);

		len = min_t(unsigned int, len, sizeof(meminfo));
		if (copy_to_sockptr(optval, &meminfo, len))
			return -EFAULT;

		goto lenout;
	}

#ifdef CONFIG_NET_RX_BUSY_POLL
	case SO_INCOMING_NAPI_ID:
		v.val = READ_ONCE(sk->sk_napi_id);

		/* aggregate non-NAPI IDs down to 0 */
		if (v.val < MIN_NAPI_ID)
			v.val = 0;

		break;
#endif

	case SO_COOKIE:
		lv = sizeof(u64);
		if (len < lv)
			return -EINVAL;
		v.val64 = sock_gen_cookie(sk);
		break;

	case SO_ZEROCOPY:
		v.val = sock_flag(sk, SOCK_ZEROCOPY);
		break;

	case SO_TXTIME:
		lv = sizeof(v.txtime);
		v.txtime.clockid = sk->sk_clockid;
		v.txtime.flags |= sk->sk_txtime_deadline_mode ?
				  SOF_TXTIME_DEADLINE_MODE : 0;
		v.txtime.flags |= sk->sk_txtime_report_errors ?
				  SOF_TXTIME_REPORT_ERRORS : 0;
		break;

	case SO_BINDTOIFINDEX:
		v.val = READ_ONCE(sk->sk_bound_dev_if);
		break;

	case SO_NETNS_COOKIE:
		lv = sizeof(u64);
		if (len != lv)
			return -EINVAL;
		v.val64 = sock_net(sk)->net_cookie;
		break;

	case SO_BUF_LOCK:
		v.val = sk->sk_userlocks & SOCK_BUF_LOCK_MASK;
		break;

	case SO_RESERVE_MEM:
		v.val = READ_ONCE(sk->sk_reserved_mem);
		break;

	case SO_TXREHASH:
		/* Paired with WRITE_ONCE() in sk_setsockopt() */
		v.val = READ_ONCE(sk->sk_txrehash);
		break;

	default:
		/* We implement the SO_SNDLOWAT etc to not be settable
		 * (1003.1g 7).
		 */
		return -ENOPROTOOPT;
	}

	if (len > lv)
		len = lv;
	if (copy_to_sockptr(optval, &v, len))
		return -EFAULT;
lenout:
	if (copy_to_sockptr(optlen, &len, sizeof(int)))
		return -EFAULT;
	return 0;
}

/*
 * Initialize an sk_lock.
 *
 * (We also register the sk_lock with the lock validator.)
 */
static inline void sock_lock_init(struct sock *sk)
{
	if (sk->sk_kern_sock)
		sock_lock_init_class_and_name(
			sk,
			af_family_kern_slock_key_strings[sk->sk_family],
			af_family_kern_slock_keys + sk->sk_family,
			af_family_kern_key_strings[sk->sk_family],
			af_family_kern_keys + sk->sk_family);
	else
		sock_lock_init_class_and_name(
			sk,
			af_family_slock_key_strings[sk->sk_family],
			af_family_slock_keys + sk->sk_family,
			af_family_key_strings[sk->sk_family],
			af_family_keys + sk->sk_family);
}

/*
 * Copy all fields from osk to nsk but nsk->sk_refcnt must not change yet,
 * even temporarly, because of RCU lookups. sk_node should also be left as is.
 * We must not copy fields between sk_dontcopy_begin and sk_dontcopy_end
 */
static void sock_copy(struct sock *nsk, const struct sock *osk)
{
	const struct proto *prot = READ_ONCE(osk->sk_prot);
#ifdef CONFIG_SECURITY_NETWORK
	void *sptr = nsk->sk_security;
#endif

	/* If we move sk_tx_queue_mapping out of the private section,
	 * we must check if sk_tx_queue_clear() is called after
	 * sock_copy() in sk_clone_lock().
	 */
	BUILD_BUG_ON(offsetof(struct sock, sk_tx_queue_mapping) <
		     offsetof(struct sock, sk_dontcopy_begin) ||
		     offsetof(struct sock, sk_tx_queue_mapping) >=
		     offsetof(struct sock, sk_dontcopy_end));

	memcpy(nsk, osk, offsetof(struct sock, sk_dontcopy_begin));

	unsafe_memcpy(&nsk->sk_dontcopy_end, &osk->sk_dontcopy_end,
		      prot->obj_size - offsetof(struct sock, sk_dontcopy_end),
		      /* alloc is larger than struct, see sk_prot_alloc() */);

#ifdef CONFIG_SECURITY_NETWORK
	nsk->sk_security = sptr;
	security_sk_clone(osk, nsk);
#endif
}

static struct sock *sk_prot_alloc(struct proto *prot, gfp_t priority,
		int family)
{
	struct sock *sk;
	struct kmem_cache *slab;

	slab = prot->slab;
	if (slab != NULL) {
		sk = kmem_cache_alloc(slab, priority & ~__GFP_ZERO);
		if (!sk)
			return sk;
		if (want_init_on_alloc(priority))
			sk_prot_clear_nulls(sk, prot->obj_size);
	} else
		sk = kmalloc(prot->obj_size, priority);

	if (sk != NULL) {
		if (security_sk_alloc(sk, family, priority))
			goto out_free;

		if (!try_module_get(prot->owner))
			goto out_free_sec;
	}

	return sk;

out_free_sec:
	security_sk_free(sk);
out_free:
	if (slab != NULL)
		kmem_cache_free(slab, sk);
	else
		kfree(sk);
	return NULL;
}

static void sk_prot_free(struct proto *prot, struct sock *sk)
{
	struct kmem_cache *slab;
	struct module *owner;

	owner = prot->owner;
	slab = prot->slab;

	cgroup_sk_free(&sk->sk_cgrp_data);
	mem_cgroup_sk_free(sk);
	security_sk_free(sk);
	if (slab != NULL)
		kmem_cache_free(slab, sk);
	else
		kfree(sk);
	module_put(owner);
}

/**
 *	sk_alloc - All socket objects are allocated here
 *	@net: the applicable net namespace
 *	@family: protocol family
 *	@priority: for allocation (%GFP_KERNEL, %GFP_ATOMIC, etc)
 *	@prot: struct proto associated with this new sock instance
 *	@kern: is this to be a kernel socket?
 */
struct sock *sk_alloc(struct net *net, int family, gfp_t priority,
		      struct proto *prot, int kern)
{
	struct sock *sk;

	sk = sk_prot_alloc(prot, priority | __GFP_ZERO, family);
	if (sk) {
		sk->sk_family = family;
		/*
		 * See comment in struct sock definition to understand
		 * why we need sk_prot_creator -acme
		 */
		sk->sk_prot = sk->sk_prot_creator = prot;
		sk->sk_kern_sock = kern;
		sock_lock_init(sk);
		sk->sk_net_refcnt = kern ? 0 : 1;
		if (likely(sk->sk_net_refcnt)) {
			get_net_track(net, &sk->ns_tracker, priority);
			sock_inuse_add(net, 1);
		} else {
			__netns_tracker_alloc(net, &sk->ns_tracker,
					      false, priority);
		}

		sock_net_set(sk, net);
		refcount_set(&sk->sk_wmem_alloc, 1);

		mem_cgroup_sk_alloc(sk);
		cgroup_sk_alloc(&sk->sk_cgrp_data);
		sock_update_classid(&sk->sk_cgrp_data);
		sock_update_netprioidx(&sk->sk_cgrp_data);
		sk_tx_queue_clear(sk);
	}

	return sk;
}
EXPORT_SYMBOL(sk_alloc);

/* Sockets having SOCK_RCU_FREE will call this function after one RCU
 * grace period. This is the case for UDP sockets and TCP listeners.
 */
static void __sk_destruct(struct rcu_head *head)
{
	struct sock *sk = container_of(head, struct sock, sk_rcu);
	struct sk_filter *filter;

	if (sk->sk_destruct)
		sk->sk_destruct(sk);

	filter = rcu_dereference_check(sk->sk_filter,
				       refcount_read(&sk->sk_wmem_alloc) == 0);
	if (filter) {
		sk_filter_uncharge(sk, filter);
		RCU_INIT_POINTER(sk->sk_filter, NULL);
	}

	sock_disable_timestamp(sk, SK_FLAGS_TIMESTAMP);

#ifdef CONFIG_BPF_SYSCALL
	bpf_sk_storage_free(sk);
#endif

	if (atomic_read(&sk->sk_omem_alloc))
		pr_debug("%s: optmem leakage (%d bytes) detected\n",
			 __func__, atomic_read(&sk->sk_omem_alloc));

	if (sk->sk_frag.page) {
		put_page(sk->sk_frag.page);
		sk->sk_frag.page = NULL;
	}

	/* We do not need to acquire sk->sk_peer_lock, we are the last user. */
	put_cred(sk->sk_peer_cred);
	put_pid(sk->sk_peer_pid);

	if (likely(sk->sk_net_refcnt))
		put_net_track(sock_net(sk), &sk->ns_tracker);
	else
		__netns_tracker_free(sock_net(sk), &sk->ns_tracker, false);

	sk_prot_free(sk->sk_prot_creator, sk);
}

void sk_destruct(struct sock *sk)
{
	bool use_call_rcu = sock_flag(sk, SOCK_RCU_FREE);

	if (rcu_access_pointer(sk->sk_reuseport_cb)) {
		reuseport_detach_sock(sk);
		use_call_rcu = true;
	}

	if (use_call_rcu)
		call_rcu(&sk->sk_rcu, __sk_destruct);
	else
		__sk_destruct(&sk->sk_rcu);
}

static void __sk_free(struct sock *sk)
{
	if (likely(sk->sk_net_refcnt))
		sock_inuse_add(sock_net(sk), -1);

	if (unlikely(sk->sk_net_refcnt && sock_diag_has_destroy_listeners(sk)))
		sock_diag_broadcast_destroy(sk);
	else
		sk_destruct(sk);
}

void sk_free(struct sock *sk)
{
	/*
	 * We subtract one from sk_wmem_alloc and can know if
	 * some packets are still in some tx queue.
	 * If not null, sock_wfree() will call __sk_free(sk) later
	 */
	if (refcount_dec_and_test(&sk->sk_wmem_alloc))
		__sk_free(sk);
}
EXPORT_SYMBOL(sk_free);

static void sk_init_common(struct sock *sk)
{
	skb_queue_head_init(&sk->sk_receive_queue);
	skb_queue_head_init(&sk->sk_write_queue);
	skb_queue_head_init(&sk->sk_error_queue);

	rwlock_init(&sk->sk_callback_lock);
	lockdep_set_class_and_name(&sk->sk_receive_queue.lock,
			af_rlock_keys + sk->sk_family,
			af_family_rlock_key_strings[sk->sk_family]);
	lockdep_set_class_and_name(&sk->sk_write_queue.lock,
			af_wlock_keys + sk->sk_family,
			af_family_wlock_key_strings[sk->sk_family]);
	lockdep_set_class_and_name(&sk->sk_error_queue.lock,
			af_elock_keys + sk->sk_family,
			af_family_elock_key_strings[sk->sk_family]);
	lockdep_set_class_and_name(&sk->sk_callback_lock,
			af_callback_keys + sk->sk_family,
			af_family_clock_key_strings[sk->sk_family]);
}

/**
 *	sk_clone_lock - clone a socket, and lock its clone
 *	@sk: the socket to clone
 *	@priority: for allocation (%GFP_KERNEL, %GFP_ATOMIC, etc)
 *
 *	Caller must unlock socket even in error path (bh_unlock_sock(newsk))
 */
struct sock *sk_clone_lock(const struct sock *sk, const gfp_t priority)
{
	struct proto *prot = READ_ONCE(sk->sk_prot);
	struct sk_filter *filter;
	bool is_charged = true;
	struct sock *newsk;

	newsk = sk_prot_alloc(prot, priority, sk->sk_family);
	if (!newsk)
		goto out;

	sock_copy(newsk, sk);

	newsk->sk_prot_creator = prot;

	/* SANITY */
	if (likely(newsk->sk_net_refcnt)) {
		get_net_track(sock_net(newsk), &newsk->ns_tracker, priority);
		sock_inuse_add(sock_net(newsk), 1);
	} else {
		/* Kernel sockets are not elevating the struct net refcount.
		 * Instead, use a tracker to more easily detect if a layer
		 * is not properly dismantling its kernel sockets at netns
		 * destroy time.
		 */
		__netns_tracker_alloc(sock_net(newsk), &newsk->ns_tracker,
				      false, priority);
	}
	sk_node_init(&newsk->sk_node);
	sock_lock_init(newsk);
	bh_lock_sock(newsk);
	newsk->sk_backlog.head	= newsk->sk_backlog.tail = NULL;
	newsk->sk_backlog.len = 0;

	atomic_set(&newsk->sk_rmem_alloc, 0);

	/* sk_wmem_alloc set to one (see sk_free() and sock_wfree()) */
	refcount_set(&newsk->sk_wmem_alloc, 1);

	atomic_set(&newsk->sk_omem_alloc, 0);
	sk_init_common(newsk);

	newsk->sk_dst_cache	= NULL;
	newsk->sk_dst_pending_confirm = 0;
	newsk->sk_wmem_queued	= 0;
	newsk->sk_forward_alloc = 0;
	newsk->sk_reserved_mem  = 0;
	atomic_set(&newsk->sk_drops, 0);
	newsk->sk_send_head	= NULL;
	newsk->sk_userlocks	= sk->sk_userlocks & ~SOCK_BINDPORT_LOCK;
	atomic_set(&newsk->sk_zckey, 0);

	sock_reset_flag(newsk, SOCK_DONE);

	/* sk->sk_memcg will be populated at accept() time */
	newsk->sk_memcg = NULL;

	cgroup_sk_clone(&newsk->sk_cgrp_data);

	rcu_read_lock();
	filter = rcu_dereference(sk->sk_filter);
	if (filter != NULL)
		/* though it's an empty new sock, the charging may fail
		 * if sysctl_optmem_max was changed between creation of
		 * original socket and cloning
		 */
		is_charged = sk_filter_charge(newsk, filter);
	RCU_INIT_POINTER(newsk->sk_filter, filter);
	rcu_read_unlock();

	if (unlikely(!is_charged || xfrm_sk_clone_policy(newsk, sk))) {
		/* We need to make sure that we don't uncharge the new
		 * socket if we couldn't charge it in the first place
		 * as otherwise we uncharge the parent's filter.
		 */
		if (!is_charged)
			RCU_INIT_POINTER(newsk->sk_filter, NULL);
		sk_free_unlock_clone(newsk);
		newsk = NULL;
		goto out;
	}
	RCU_INIT_POINTER(newsk->sk_reuseport_cb, NULL);

	if (bpf_sk_storage_clone(sk, newsk)) {
		sk_free_unlock_clone(newsk);
		newsk = NULL;
		goto out;
	}

	/* Clear sk_user_data if parent had the pointer tagged
	 * as not suitable for copying when cloning.
	 */
	if (sk_user_data_is_nocopy(newsk))
		newsk->sk_user_data = NULL;

	newsk->sk_err	   = 0;
	newsk->sk_err_soft = 0;
	newsk->sk_priority = 0;
	newsk->sk_incoming_cpu = raw_smp_processor_id();

	/* Before updating sk_refcnt, we must commit prior changes to memory
	 * (Documentation/RCU/rculist_nulls.rst for details)
	 */
	smp_wmb();
	refcount_set(&newsk->sk_refcnt, 2);

	sk_set_socket(newsk, NULL);
	sk_tx_queue_clear(newsk);
	RCU_INIT_POINTER(newsk->sk_wq, NULL);

	if (newsk->sk_prot->sockets_allocated)
		sk_sockets_allocated_inc(newsk);

	if (sock_needs_netstamp(sk) && newsk->sk_flags & SK_FLAGS_TIMESTAMP)
		net_enable_timestamp();
out:
	return newsk;
}
EXPORT_SYMBOL_GPL(sk_clone_lock);

void sk_free_unlock_clone(struct sock *sk)
{
	/* It is still raw copy of parent, so invalidate
	 * destructor and make plain sk_free() */
	sk->sk_destruct = NULL;
	bh_unlock_sock(sk);
	sk_free(sk);
}
EXPORT_SYMBOL_GPL(sk_free_unlock_clone);

static u32 sk_dst_gso_max_size(struct sock *sk, struct dst_entry *dst)
{
	bool is_ipv6 = false;
	u32 max_size;

#if IS_ENABLED(CONFIG_IPV6)
	is_ipv6 = (sk->sk_family == AF_INET6 &&
		   !ipv6_addr_v4mapped(&sk->sk_v6_rcv_saddr));
#endif
	/* pairs with the WRITE_ONCE() in netif_set_gso(_ipv4)_max_size() */
	max_size = is_ipv6 ? READ_ONCE(dst->dev->gso_max_size) :
			READ_ONCE(dst->dev->gso_ipv4_max_size);
	if (max_size > GSO_LEGACY_MAX_SIZE && !sk_is_tcp(sk))
		max_size = GSO_LEGACY_MAX_SIZE;

	return max_size - (MAX_TCP_HEADER + 1);
}

void sk_setup_caps(struct sock *sk, struct dst_entry *dst)
{
	u32 max_segs = 1;

	sk->sk_route_caps = dst->dev->features;
	if (sk_is_tcp(sk))
		sk->sk_route_caps |= NETIF_F_GSO;
	if (sk->sk_route_caps & NETIF_F_GSO)
		sk->sk_route_caps |= NETIF_F_GSO_SOFTWARE;
	if (unlikely(sk->sk_gso_disabled))
		sk->sk_route_caps &= ~NETIF_F_GSO_MASK;
	if (sk_can_gso(sk)) {
		if (dst->header_len && !xfrm_dst_offload_ok(dst)) {
			sk->sk_route_caps &= ~NETIF_F_GSO_MASK;
		} else {
			sk->sk_route_caps |= NETIF_F_SG | NETIF_F_HW_CSUM;
			sk->sk_gso_max_size = sk_dst_gso_max_size(sk, dst);
			/* pairs with the WRITE_ONCE() in netif_set_gso_max_segs() */
			max_segs = max_t(u32, READ_ONCE(dst->dev->gso_max_segs), 1);
		}
	}
	sk->sk_gso_max_segs = max_segs;
	sk_dst_set(sk, dst);
}
EXPORT_SYMBOL_GPL(sk_setup_caps);

/*
 *	Simple resource managers for sockets.
 */


/*
 * Write buffer destructor automatically called from kfree_skb.
 */
void sock_wfree(struct sk_buff *skb)
{
	struct sock *sk = skb->sk;
	unsigned int len = skb->truesize;
	bool free;

	if (!sock_flag(sk, SOCK_USE_WRITE_QUEUE)) {
		if (sock_flag(sk, SOCK_RCU_FREE) &&
		    sk->sk_write_space == sock_def_write_space) {
			rcu_read_lock();
			free = refcount_sub_and_test(len, &sk->sk_wmem_alloc);
			sock_def_write_space_wfree(sk);
			rcu_read_unlock();
			if (unlikely(free))
				__sk_free(sk);
			return;
		}

		/*
		 * Keep a reference on sk_wmem_alloc, this will be released
		 * after sk_write_space() call
		 */
		WARN_ON(refcount_sub_and_test(len - 1, &sk->sk_wmem_alloc));
		sk->sk_write_space(sk);
		len = 1;
	}
	/*
	 * if sk_wmem_alloc reaches 0, we must finish what sk_free()
	 * could not do because of in-flight packets
	 */
	if (refcount_sub_and_test(len, &sk->sk_wmem_alloc))
		__sk_free(sk);
}
EXPORT_SYMBOL(sock_wfree);

/* This variant of sock_wfree() is used by TCP,
 * since it sets SOCK_USE_WRITE_QUEUE.
 */
void __sock_wfree(struct sk_buff *skb)
{
	struct sock *sk = skb->sk;

	if (refcount_sub_and_test(skb->truesize, &sk->sk_wmem_alloc))
		__sk_free(sk);
}

void skb_set_owner_w(struct sk_buff *skb, struct sock *sk)
{
	skb_orphan(skb);
	skb->sk = sk;
#ifdef CONFIG_INET
	if (unlikely(!sk_fullsock(sk))) {
		skb->destructor = sock_edemux;
		sock_hold(sk);
		return;
	}
#endif
	skb->destructor = sock_wfree;
	skb_set_hash_from_sk(skb, sk);
	/*
	 * We used to take a refcount on sk, but following operation
	 * is enough to guarantee sk_free() wont free this sock until
	 * all in-flight packets are completed
	 */
	refcount_add(skb->truesize, &sk->sk_wmem_alloc);
}
EXPORT_SYMBOL(skb_set_owner_w);

static bool can_skb_orphan_partial(const struct sk_buff *skb)
{
#ifdef CONFIG_TLS_DEVICE
	/* Drivers depend on in-order delivery for crypto offload,
	 * partial orphan breaks out-of-order-OK logic.
	 */
	if (skb->decrypted)
		return false;
#endif
	return (skb->destructor == sock_wfree ||
		(IS_ENABLED(CONFIG_INET) && skb->destructor == tcp_wfree));
}

/* This helper is used by netem, as it can hold packets in its
 * delay queue. We want to allow the owner socket to send more
 * packets, as if they were already TX completed by a typical driver.
 * But we also want to keep skb->sk set because some packet schedulers
 * rely on it (sch_fq for example).
 */
void skb_orphan_partial(struct sk_buff *skb)
{
	if (skb_is_tcp_pure_ack(skb))
		return;

	if (can_skb_orphan_partial(skb) && skb_set_owner_sk_safe(skb, skb->sk))
		return;

	skb_orphan(skb);
}
EXPORT_SYMBOL(skb_orphan_partial);

/*
 * Read buffer destructor automatically called from kfree_skb.
 */
void sock_rfree(struct sk_buff *skb)
{
	struct sock *sk = skb->sk;
	unsigned int len = skb->truesize;

	atomic_sub(len, &sk->sk_rmem_alloc);
	sk_mem_uncharge(sk, len);
}
EXPORT_SYMBOL(sock_rfree);

/*
 * Buffer destructor for skbs that are not used directly in read or write
 * path, e.g. for error handler skbs. Automatically called from kfree_skb.
 */
void sock_efree(struct sk_buff *skb)
{
	sock_put(skb->sk);
}
EXPORT_SYMBOL(sock_efree);

/* Buffer destructor for prefetch/receive path where reference count may
 * not be held, e.g. for listen sockets.
 */
#ifdef CONFIG_INET
void sock_pfree(struct sk_buff *skb)
{
	struct sock *sk = skb->sk;

	if (!sk_is_refcounted(sk))
		return;

	if (sk->sk_state == TCP_NEW_SYN_RECV && inet_reqsk(sk)->syncookie) {
		inet_reqsk(sk)->rsk_listener = NULL;
		reqsk_free(inet_reqsk(sk));
		return;
	}

	sock_gen_put(sk);
}
EXPORT_SYMBOL(sock_pfree);
#endif /* CONFIG_INET */

kuid_t sock_i_uid(struct sock *sk)
{
	kuid_t uid;

	read_lock_bh(&sk->sk_callback_lock);
	uid = sk->sk_socket ? SOCK_INODE(sk->sk_socket)->i_uid : GLOBAL_ROOT_UID;
	read_unlock_bh(&sk->sk_callback_lock);
	return uid;
}
EXPORT_SYMBOL(sock_i_uid);

unsigned long __sock_i_ino(struct sock *sk)
{
	unsigned long ino;

	read_lock(&sk->sk_callback_lock);
	ino = sk->sk_socket ? SOCK_INODE(sk->sk_socket)->i_ino : 0;
	read_unlock(&sk->sk_callback_lock);
	return ino;
}
EXPORT_SYMBOL(__sock_i_ino);

unsigned long sock_i_ino(struct sock *sk)
{
	unsigned long ino;

	local_bh_disable();
	ino = __sock_i_ino(sk);
	local_bh_enable();
	return ino;
}
EXPORT_SYMBOL(sock_i_ino);

/*
 * Allocate a skb from the socket's send buffer.
 */
struct sk_buff *sock_wmalloc(struct sock *sk, unsigned long size, int force,
			     gfp_t priority)
{
	if (force ||
	    refcount_read(&sk->sk_wmem_alloc) < READ_ONCE(sk->sk_sndbuf)) {
		struct sk_buff *skb = alloc_skb(size, priority);

		if (skb) {
			skb_set_owner_w(skb, sk);
			return skb;
		}
	}
	return NULL;
}
EXPORT_SYMBOL(sock_wmalloc);

static void sock_ofree(struct sk_buff *skb)
{
	struct sock *sk = skb->sk;

	atomic_sub(skb->truesize, &sk->sk_omem_alloc);
}

struct sk_buff *sock_omalloc(struct sock *sk, unsigned long size,
			     gfp_t priority)
{
	struct sk_buff *skb;

	/* small safe race: SKB_TRUESIZE may differ from final skb->truesize */
	if (atomic_read(&sk->sk_omem_alloc) + SKB_TRUESIZE(size) >
	    READ_ONCE(sock_net(sk)->core.sysctl_optmem_max))
		return NULL;

	skb = alloc_skb(size, priority);
	if (!skb)
		return NULL;

	atomic_add(skb->truesize, &sk->sk_omem_alloc);
	skb->sk = sk;
	skb->destructor = sock_ofree;
	return skb;
}

/*
 * Allocate a memory block from the socket's option memory buffer.
 */
void *sock_kmalloc(struct sock *sk, int size, gfp_t priority)
{
	int optmem_max = READ_ONCE(sock_net(sk)->core.sysctl_optmem_max);

	if ((unsigned int)size <= optmem_max &&
	    atomic_read(&sk->sk_omem_alloc) + size < optmem_max) {
		void *mem;
		/* First do the add, to avoid the race if kmalloc
		 * might sleep.
		 */
		atomic_add(size, &sk->sk_omem_alloc);
		mem = kmalloc(size, priority);
		if (mem)
			return mem;
		atomic_sub(size, &sk->sk_omem_alloc);
	}
	return NULL;
}
EXPORT_SYMBOL(sock_kmalloc);

/* Free an option memory block. Note, we actually want the inline
 * here as this allows gcc to detect the nullify and fold away the
 * condition entirely.
 */
static inline void __sock_kfree_s(struct sock *sk, void *mem, int size,
				  const bool nullify)
{
	if (WARN_ON_ONCE(!mem))
		return;
	if (nullify)
		kfree_sensitive(mem);
	else
		kfree(mem);
	atomic_sub(size, &sk->sk_omem_alloc);
}

void sock_kfree_s(struct sock *sk, void *mem, int size)
{
	__sock_kfree_s(sk, mem, size, false);
}
EXPORT_SYMBOL(sock_kfree_s);

void sock_kzfree_s(struct sock *sk, void *mem, int size)
{
	__sock_kfree_s(sk, mem, size, true);
}
EXPORT_SYMBOL(sock_kzfree_s);

/* It is almost wait_for_tcp_memory minus release_sock/lock_sock.
   I think, these locks should be removed for datagram sockets.
 */
static long sock_wait_for_wmem(struct sock *sk, long timeo)
{
	DEFINE_WAIT(wait);

	sk_clear_bit(SOCKWQ_ASYNC_NOSPACE, sk);
	for (;;) {
		if (!timeo)
			break;
		if (signal_pending(current))
			break;
		set_bit(SOCK_NOSPACE, &sk->sk_socket->flags);
		prepare_to_wait(sk_sleep(sk), &wait, TASK_INTERRUPTIBLE);
		if (refcount_read(&sk->sk_wmem_alloc) < READ_ONCE(sk->sk_sndbuf))
			break;
		if (READ_ONCE(sk->sk_shutdown) & SEND_SHUTDOWN)
			break;
		if (READ_ONCE(sk->sk_err))
			break;
		timeo = schedule_timeout(timeo);
	}
	finish_wait(sk_sleep(sk), &wait);
	return timeo;
}


/*
 *	Generic send/receive buffer handlers
 */

struct sk_buff *sock_alloc_send_pskb(struct sock *sk, unsigned long header_len,
				     unsigned long data_len, int noblock,
				     int *errcode, int max_page_order)
{
	struct sk_buff *skb;
	long timeo;
	int err;

	timeo = sock_sndtimeo(sk, noblock);
	for (;;) {
		err = sock_error(sk);
		if (err != 0)
			goto failure;

		err = -EPIPE;
		if (READ_ONCE(sk->sk_shutdown) & SEND_SHUTDOWN)
			goto failure;

		if (sk_wmem_alloc_get(sk) < READ_ONCE(sk->sk_sndbuf))
			break;

		sk_set_bit(SOCKWQ_ASYNC_NOSPACE, sk);
		set_bit(SOCK_NOSPACE, &sk->sk_socket->flags);
		err = -EAGAIN;
		if (!timeo)
			goto failure;
		if (signal_pending(current))
			goto interrupted;
		timeo = sock_wait_for_wmem(sk, timeo);
	}
	skb = alloc_skb_with_frags(header_len, data_len, max_page_order,
				   errcode, sk->sk_allocation);
	if (skb)
		skb_set_owner_w(skb, sk);
	return skb;

interrupted:
	err = sock_intr_errno(timeo);
failure:
	*errcode = err;
	return NULL;
}
EXPORT_SYMBOL(sock_alloc_send_pskb);

int __sock_cmsg_send(struct sock *sk, struct cmsghdr *cmsg,
		     struct sockcm_cookie *sockc)
{
	u32 tsflags;

	switch (cmsg->cmsg_type) {
	case SO_MARK:
		if (!ns_capable(sock_net(sk)->user_ns, CAP_NET_RAW) &&
		    !ns_capable(sock_net(sk)->user_ns, CAP_NET_ADMIN))
			return -EPERM;
		if (cmsg->cmsg_len != CMSG_LEN(sizeof(u32)))
			return -EINVAL;
		sockc->mark = *(u32 *)CMSG_DATA(cmsg);
		break;
	case SO_TIMESTAMPING_OLD:
	case SO_TIMESTAMPING_NEW:
		if (cmsg->cmsg_len != CMSG_LEN(sizeof(u32)))
			return -EINVAL;

		tsflags = *(u32 *)CMSG_DATA(cmsg);
		if (tsflags & ~SOF_TIMESTAMPING_TX_RECORD_MASK)
			return -EINVAL;

		sockc->tsflags &= ~SOF_TIMESTAMPING_TX_RECORD_MASK;
		sockc->tsflags |= tsflags;
		break;
	case SCM_TXTIME:
		if (!sock_flag(sk, SOCK_TXTIME))
			return -EINVAL;
		if (cmsg->cmsg_len != CMSG_LEN(sizeof(u64)))
			return -EINVAL;
		sockc->transmit_time = get_unaligned((u64 *)CMSG_DATA(cmsg));
		break;
	/* SCM_RIGHTS and SCM_CREDENTIALS are semantically in SOL_UNIX. */
	case SCM_RIGHTS:
	case SCM_CREDENTIALS:
		break;
	default:
		return -EINVAL;
	}
	return 0;
}
EXPORT_SYMBOL(__sock_cmsg_send);

int sock_cmsg_send(struct sock *sk, struct msghdr *msg,
		   struct sockcm_cookie *sockc)
{
	struct cmsghdr *cmsg;
	int ret;

	for_each_cmsghdr(cmsg, msg) {
		if (!CMSG_OK(msg, cmsg))
			return -EINVAL;
		if (cmsg->cmsg_level != SOL_SOCKET)
			continue;
		ret = __sock_cmsg_send(sk, cmsg, sockc);
		if (ret)
			return ret;
	}
	return 0;
}
EXPORT_SYMBOL(sock_cmsg_send);

static void sk_enter_memory_pressure(struct sock *sk)
{
	if (!sk->sk_prot->enter_memory_pressure)
		return;

	sk->sk_prot->enter_memory_pressure(sk);
}

static void sk_leave_memory_pressure(struct sock *sk)
{
	if (sk->sk_prot->leave_memory_pressure) {
		INDIRECT_CALL_INET_1(sk->sk_prot->leave_memory_pressure,
				     tcp_leave_memory_pressure, sk);
	} else {
		unsigned long *memory_pressure = sk->sk_prot->memory_pressure;

		if (memory_pressure && READ_ONCE(*memory_pressure))
			WRITE_ONCE(*memory_pressure, 0);
	}
}

DEFINE_STATIC_KEY_FALSE(net_high_order_alloc_disable_key);

/**
 * skb_page_frag_refill - check that a page_frag contains enough room
 * @sz: minimum size of the fragment we want to get
 * @pfrag: pointer to page_frag
 * @gfp: priority for memory allocation
 *
 * Note: While this allocator tries to use high order pages, there is
 * no guarantee that allocations succeed. Therefore, @sz MUST be
 * less or equal than PAGE_SIZE.
 */
bool skb_page_frag_refill(unsigned int sz, struct page_frag *pfrag, gfp_t gfp)
{
	if (pfrag->page) {
		if (page_ref_count(pfrag->page) == 1) {
			pfrag->offset = 0;
			return true;
		}
		if (pfrag->offset + sz <= pfrag->size)
			return true;
		put_page(pfrag->page);
	}

	pfrag->offset = 0;
	if (SKB_FRAG_PAGE_ORDER &&
	    !static_branch_unlikely(&net_high_order_alloc_disable_key)) {
		/* Avoid direct reclaim but allow kswapd to wake */
		pfrag->page = alloc_pages((gfp & ~__GFP_DIRECT_RECLAIM) |
					  __GFP_COMP | __GFP_NOWARN |
					  __GFP_NORETRY,
					  SKB_FRAG_PAGE_ORDER);
		if (likely(pfrag->page)) {
			pfrag->size = PAGE_SIZE << SKB_FRAG_PAGE_ORDER;
			return true;
		}
	}
	pfrag->page = alloc_page(gfp);
	if (likely(pfrag->page)) {
		pfrag->size = PAGE_SIZE;
		return true;
	}
	return false;
}
EXPORT_SYMBOL(skb_page_frag_refill);

bool sk_page_frag_refill(struct sock *sk, struct page_frag *pfrag)
{
	if (likely(skb_page_frag_refill(32U, pfrag, sk->sk_allocation)))
		return true;

	sk_enter_memory_pressure(sk);
	sk_stream_moderate_sndbuf(sk);
	return false;
}
EXPORT_SYMBOL(sk_page_frag_refill);

void __lock_sock(struct sock *sk)
	__releases(&sk->sk_lock.slock)
	__acquires(&sk->sk_lock.slock)
{
	DEFINE_WAIT(wait);

	for (;;) {
		prepare_to_wait_exclusive(&sk->sk_lock.wq, &wait,
					TASK_UNINTERRUPTIBLE);
		spin_unlock_bh(&sk->sk_lock.slock);
		schedule();
		spin_lock_bh(&sk->sk_lock.slock);
		if (!sock_owned_by_user(sk))
			break;
	}
	finish_wait(&sk->sk_lock.wq, &wait);
}

void __release_sock(struct sock *sk)
	__releases(&sk->sk_lock.slock)
	__acquires(&sk->sk_lock.slock)
{
	struct sk_buff *skb, *next;

	while ((skb = sk->sk_backlog.head) != NULL) {
		sk->sk_backlog.head = sk->sk_backlog.tail = NULL;

		spin_unlock_bh(&sk->sk_lock.slock);

		do {
			next = skb->next;
			prefetch(next);
			DEBUG_NET_WARN_ON_ONCE(skb_dst_is_noref(skb));
			skb_mark_not_on_list(skb);
			sk_backlog_rcv(sk, skb);

			cond_resched();

			skb = next;
		} while (skb != NULL);

		spin_lock_bh(&sk->sk_lock.slock);
	}

	/*
	 * Doing the zeroing here guarantee we can not loop forever
	 * while a wild producer attempts to flood us.
	 */
	sk->sk_backlog.len = 0;
}

void __sk_flush_backlog(struct sock *sk)
{
	spin_lock_bh(&sk->sk_lock.slock);
	__release_sock(sk);

	if (sk->sk_prot->release_cb)
		INDIRECT_CALL_INET_1(sk->sk_prot->release_cb,
				     tcp_release_cb, sk);

	spin_unlock_bh(&sk->sk_lock.slock);
}
EXPORT_SYMBOL_GPL(__sk_flush_backlog);

/**
 * sk_wait_data - wait for data to arrive at sk_receive_queue
 * @sk:    sock to wait on
 * @timeo: for how long
 * @skb:   last skb seen on sk_receive_queue
 *
 * Now socket state including sk->sk_err is changed only under lock,
 * hence we may omit checks after joining wait queue.
 * We check receive queue before schedule() only as optimization;
 * it is very likely that release_sock() added new data.
 */
int sk_wait_data(struct sock *sk, long *timeo, const struct sk_buff *skb)
{
	DEFINE_WAIT_FUNC(wait, woken_wake_function);
	int rc;

	add_wait_queue(sk_sleep(sk), &wait);
	sk_set_bit(SOCKWQ_ASYNC_WAITDATA, sk);
	rc = sk_wait_event(sk, timeo, skb_peek_tail(&sk->sk_receive_queue) != skb, &wait);
	sk_clear_bit(SOCKWQ_ASYNC_WAITDATA, sk);
	remove_wait_queue(sk_sleep(sk), &wait);
	return rc;
}
EXPORT_SYMBOL(sk_wait_data);

/**
 *	__sk_mem_raise_allocated - increase memory_allocated
 *	@sk: socket
 *	@size: memory size to allocate
 *	@amt: pages to allocate
 *	@kind: allocation type
 *
 *	Similar to __sk_mem_schedule(), but does not update sk_forward_alloc.
 *
 *	Unlike the globally shared limits among the sockets under same protocol,
 *	consuming the budget of a memcg won't have direct effect on other ones.
 *	So be optimistic about memcg's tolerance, and leave the callers to decide
 *	whether or not to raise allocated through sk_under_memory_pressure() or
 *	its variants.
 */
int __sk_mem_raise_allocated(struct sock *sk, int size, int amt, int kind)
{
	struct mem_cgroup *memcg = mem_cgroup_sockets_enabled ? sk->sk_memcg : NULL;
	struct proto *prot = sk->sk_prot;
	bool charged = false;
	long allocated;

	sk_memory_allocated_add(sk, amt);
	allocated = sk_memory_allocated(sk);

	if (memcg) {
		if (!mem_cgroup_charge_skmem(memcg, amt, gfp_memcg_charge()))
			goto suppress_allocation;
		charged = true;
	}

	/* Under limit. */
	if (allocated <= sk_prot_mem_limits(sk, 0)) {
		sk_leave_memory_pressure(sk);
		return 1;
	}

	/* Under pressure. */
	if (allocated > sk_prot_mem_limits(sk, 1))
		sk_enter_memory_pressure(sk);

	/* Over hard limit. */
	if (allocated > sk_prot_mem_limits(sk, 2))
		goto suppress_allocation;

	/* Guarantee minimum buffer size under pressure (either global
	 * or memcg) to make sure features described in RFC 7323 (TCP
	 * Extensions for High Performance) work properly.
	 *
	 * This rule does NOT stand when exceeds global or memcg's hard
	 * limit, or else a DoS attack can be taken place by spawning
	 * lots of sockets whose usage are under minimum buffer size.
	 */
	if (kind == SK_MEM_RECV) {
		if (atomic_read(&sk->sk_rmem_alloc) < sk_get_rmem0(sk, prot))
			return 1;

	} else { /* SK_MEM_SEND */
		int wmem0 = sk_get_wmem0(sk, prot);

		if (sk->sk_type == SOCK_STREAM) {
			if (sk->sk_wmem_queued < wmem0)
				return 1;
		} else if (refcount_read(&sk->sk_wmem_alloc) < wmem0) {
				return 1;
		}
	}

	if (sk_has_memory_pressure(sk)) {
		u64 alloc;

		/* The following 'average' heuristic is within the
		 * scope of global accounting, so it only makes
		 * sense for global memory pressure.
		 */
		if (!sk_under_global_memory_pressure(sk))
			return 1;

		/* Try to be fair among all the sockets under global
		 * pressure by allowing the ones that below average
		 * usage to raise.
		 */
		alloc = sk_sockets_allocated_read_positive(sk);
		if (sk_prot_mem_limits(sk, 2) > alloc *
		    sk_mem_pages(sk->sk_wmem_queued +
				 atomic_read(&sk->sk_rmem_alloc) +
				 sk->sk_forward_alloc))
			return 1;
	}

suppress_allocation:

	if (kind == SK_MEM_SEND && sk->sk_type == SOCK_STREAM) {
		sk_stream_moderate_sndbuf(sk);

		/* Fail only if socket is _under_ its sndbuf.
		 * In this case we cannot block, so that we have to fail.
		 */
		if (sk->sk_wmem_queued + size >= sk->sk_sndbuf) {
			/* Force charge with __GFP_NOFAIL */
			if (memcg && !charged) {
				mem_cgroup_charge_skmem(memcg, amt,
					gfp_memcg_charge() | __GFP_NOFAIL);
			}
			return 1;
		}
	}

	if (kind == SK_MEM_SEND || (kind == SK_MEM_RECV && charged))
		trace_sock_exceed_buf_limit(sk, prot, allocated, kind);

	sk_memory_allocated_sub(sk, amt);

	if (charged)
		mem_cgroup_uncharge_skmem(memcg, amt);

	return 0;
}

/**
 *	__sk_mem_schedule - increase sk_forward_alloc and memory_allocated
 *	@sk: socket
 *	@size: memory size to allocate
 *	@kind: allocation type
 *
 *	If kind is SK_MEM_SEND, it means wmem allocation. Otherwise it means
 *	rmem allocation. This function assumes that protocols which have
 *	memory_pressure use sk_wmem_queued as write buffer accounting.
 */
int __sk_mem_schedule(struct sock *sk, int size, int kind)
{
	int ret, amt = sk_mem_pages(size);

	sk_forward_alloc_add(sk, amt << PAGE_SHIFT);
	ret = __sk_mem_raise_allocated(sk, size, amt, kind);
	if (!ret)
		sk_forward_alloc_add(sk, -(amt << PAGE_SHIFT));
	return ret;
}
EXPORT_SYMBOL(__sk_mem_schedule);

/**
 *	__sk_mem_reduce_allocated - reclaim memory_allocated
 *	@sk: socket
 *	@amount: number of quanta
 *
 *	Similar to __sk_mem_reclaim(), but does not update sk_forward_alloc
 */
void __sk_mem_reduce_allocated(struct sock *sk, int amount)
{
	sk_memory_allocated_sub(sk, amount);

	if (mem_cgroup_sockets_enabled && sk->sk_memcg)
		mem_cgroup_uncharge_skmem(sk->sk_memcg, amount);

	if (sk_under_global_memory_pressure(sk) &&
	    (sk_memory_allocated(sk) < sk_prot_mem_limits(sk, 0)))
		sk_leave_memory_pressure(sk);
}

/**
 *	__sk_mem_reclaim - reclaim sk_forward_alloc and memory_allocated
 *	@sk: socket
 *	@amount: number of bytes (rounded down to a PAGE_SIZE multiple)
 */
void __sk_mem_reclaim(struct sock *sk, int amount)
{
	amount >>= PAGE_SHIFT;
	sk_forward_alloc_add(sk, -(amount << PAGE_SHIFT));
	__sk_mem_reduce_allocated(sk, amount);
}
EXPORT_SYMBOL(__sk_mem_reclaim);

int sk_set_peek_off(struct sock *sk, int val)
{
	WRITE_ONCE(sk->sk_peek_off, val);
	return 0;
}
EXPORT_SYMBOL_GPL(sk_set_peek_off);

/*
 * Set of default routines for initialising struct proto_ops when
 * the protocol does not support a particular function. In certain
 * cases where it makes no sense for a protocol to have a "do nothing"
 * function, some default processing is provided.
 */

int sock_no_bind(struct socket *sock, struct sockaddr *saddr, int len)
{
	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(sock_no_bind);

int sock_no_connect(struct socket *sock, struct sockaddr *saddr,
		    int len, int flags)
{
	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(sock_no_connect);

int sock_no_socketpair(struct socket *sock1, struct socket *sock2)
{
	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(sock_no_socketpair);

int sock_no_accept(struct socket *sock, struct socket *newsock, int flags,
		   bool kern)
{
	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(sock_no_accept);

int sock_no_getname(struct socket *sock, struct sockaddr *saddr,
		    int peer)
{
	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(sock_no_getname);

int sock_no_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(sock_no_ioctl);

int sock_no_listen(struct socket *sock, int backlog)
{
	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(sock_no_listen);

int sock_no_shutdown(struct socket *sock, int how)
{
	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(sock_no_shutdown);

int sock_no_sendmsg(struct socket *sock, struct msghdr *m, size_t len)
{
	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(sock_no_sendmsg);

int sock_no_sendmsg_locked(struct sock *sk, struct msghdr *m, size_t len)
{
	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(sock_no_sendmsg_locked);

int sock_no_recvmsg(struct socket *sock, struct msghdr *m, size_t len,
		    int flags)
{
	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(sock_no_recvmsg);

int sock_no_mmap(struct file *file, struct socket *sock, struct vm_area_struct *vma)
{
	/* Mirror missing mmap method error code */
	return -ENODEV;
}
EXPORT_SYMBOL(sock_no_mmap);

/*
 * When a file is received (via SCM_RIGHTS, etc), we must bump the
 * various sock-based usage counts.
 */
void __receive_sock(struct file *file)
{
	struct socket *sock;

	sock = sock_from_file(file);
	if (sock) {
		sock_update_netprioidx(&sock->sk->sk_cgrp_data);
		sock_update_classid(&sock->sk->sk_cgrp_data);
	}
}

/*
 *	Default Socket Callbacks
 */

static void sock_def_wakeup(struct sock *sk)
{
	struct socket_wq *wq;

	rcu_read_lock();
	wq = rcu_dereference(sk->sk_wq);
	if (skwq_has_sleeper(wq))
		wake_up_interruptible_all(&wq->wait);
	rcu_read_unlock();
}

static void sock_def_error_report(struct sock *sk)
{
	struct socket_wq *wq;

	rcu_read_lock();
	wq = rcu_dereference(sk->sk_wq);
	if (skwq_has_sleeper(wq))
		wake_up_interruptible_poll(&wq->wait, EPOLLERR);
	sk_wake_async(sk, SOCK_WAKE_IO, POLL_ERR);
	rcu_read_unlock();
}

void sock_def_readable(struct sock *sk)
{
	struct socket_wq *wq;

	trace_sk_data_ready(sk);

	rcu_read_lock();
	wq = rcu_dereference(sk->sk_wq);
	if (skwq_has_sleeper(wq))
		wake_up_interruptible_sync_poll(&wq->wait, EPOLLIN | EPOLLPRI |
						EPOLLRDNORM | EPOLLRDBAND);
	sk_wake_async(sk, SOCK_WAKE_WAITD, POLL_IN);
	rcu_read_unlock();
}

static void sock_def_write_space(struct sock *sk)
{
	struct socket_wq *wq;

	rcu_read_lock();

	/* Do not wake up a writer until he can make "significant"
	 * progress.  --DaveM
	 */
	if (sock_writeable(sk)) {
		wq = rcu_dereference(sk->sk_wq);
		if (skwq_has_sleeper(wq))
			wake_up_interruptible_sync_poll(&wq->wait, EPOLLOUT |
						EPOLLWRNORM | EPOLLWRBAND);

		/* Should agree with poll, otherwise some programs break */
		sk_wake_async(sk, SOCK_WAKE_SPACE, POLL_OUT);
	}

	rcu_read_unlock();
}

/* An optimised version of sock_def_write_space(), should only be called
 * for SOCK_RCU_FREE sockets under RCU read section and after putting
 * ->sk_wmem_alloc.
 */
static void sock_def_write_space_wfree(struct sock *sk)
{
	/* Do not wake up a writer until he can make "significant"
	 * progress.  --DaveM
	 */
	if (sock_writeable(sk)) {
		struct socket_wq *wq = rcu_dereference(sk->sk_wq);

		/* rely on refcount_sub from sock_wfree() */
		smp_mb__after_atomic();
		if (wq && waitqueue_active(&wq->wait))
			wake_up_interruptible_sync_poll(&wq->wait, EPOLLOUT |
						EPOLLWRNORM | EPOLLWRBAND);

		/* Should agree with poll, otherwise some programs break */
		sk_wake_async(sk, SOCK_WAKE_SPACE, POLL_OUT);
	}
}

static void sock_def_destruct(struct sock *sk)
{
}

void sk_send_sigurg(struct sock *sk)
{
	if (sk->sk_socket && sk->sk_socket->file)
		if (send_sigurg(&sk->sk_socket->file->f_owner))
			sk_wake_async(sk, SOCK_WAKE_URG, POLL_PRI);
}
EXPORT_SYMBOL(sk_send_sigurg);

void sk_reset_timer(struct sock *sk, struct timer_list* timer,
		    unsigned long expires)
{
	if (!mod_timer(timer, expires))
		sock_hold(sk);
}
EXPORT_SYMBOL(sk_reset_timer);

void sk_stop_timer(struct sock *sk, struct timer_list* timer)
{
	if (del_timer(timer))
		__sock_put(sk);
}
EXPORT_SYMBOL(sk_stop_timer);

void sk_stop_timer_sync(struct sock *sk, struct timer_list *timer)
{
	if (del_timer_sync(timer))
		__sock_put(sk);
}
EXPORT_SYMBOL(sk_stop_timer_sync);

void sock_init_data_uid(struct socket *sock, struct sock *sk, kuid_t uid)
{
	sk_init_common(sk);
	sk->sk_send_head	=	NULL;

	timer_setup(&sk->sk_timer, NULL, 0);

	sk->sk_allocation	=	GFP_KERNEL;
	sk->sk_rcvbuf		=	READ_ONCE(sysctl_rmem_default);
	sk->sk_sndbuf		=	READ_ONCE(sysctl_wmem_default);
	sk->sk_state		=	TCP_CLOSE;
	sk->sk_use_task_frag	=	true;
	sk_set_socket(sk, sock);

	sock_set_flag(sk, SOCK_ZAPPED);

	if (sock) {
		sk->sk_type	=	sock->type;
		RCU_INIT_POINTER(sk->sk_wq, &sock->wq);
		sock->sk	=	sk;
	} else {
		RCU_INIT_POINTER(sk->sk_wq, NULL);
	}
	sk->sk_uid	=	uid;

	rwlock_init(&sk->sk_callback_lock);
	if (sk->sk_kern_sock)
		lockdep_set_class_and_name(
			&sk->sk_callback_lock,
			af_kern_callback_keys + sk->sk_family,
			af_family_kern_clock_key_strings[sk->sk_family]);
	else
		lockdep_set_class_and_name(
			&sk->sk_callback_lock,
			af_callback_keys + sk->sk_family,
			af_family_clock_key_strings[sk->sk_family]);

	sk->sk_state_change	=	sock_def_wakeup;
	sk->sk_data_ready	=	sock_def_readable;
	sk->sk_write_space	=	sock_def_write_space;
	sk->sk_error_report	=	sock_def_error_report;
	sk->sk_destruct		=	sock_def_destruct;

	sk->sk_frag.page	=	NULL;
	sk->sk_frag.offset	=	0;
	sk->sk_peek_off		=	-1;

	sk->sk_peer_pid 	=	NULL;
	sk->sk_peer_cred	=	NULL;
	spin_lock_init(&sk->sk_peer_lock);

	sk->sk_write_pending	=	0;
	sk->sk_rcvlowat		=	1;
	sk->sk_rcvtimeo		=	MAX_SCHEDULE_TIMEOUT;
	sk->sk_sndtimeo		=	MAX_SCHEDULE_TIMEOUT;

	sk->sk_stamp = SK_DEFAULT_STAMP;
#if BITS_PER_LONG==32
	seqlock_init(&sk->sk_stamp_seq);
#endif
	atomic_set(&sk->sk_zckey, 0);

#ifdef CONFIG_NET_RX_BUSY_POLL
	sk->sk_napi_id		=	0;
	sk->sk_ll_usec		=	READ_ONCE(sysctl_net_busy_read);
#endif

	sk->sk_max_pacing_rate = ~0UL;
	sk->sk_pacing_rate = ~0UL;
	WRITE_ONCE(sk->sk_pacing_shift, 10);
	sk->sk_incoming_cpu = -1;

	sk_rx_queue_clear(sk);
	/*
	 * Before updating sk_refcnt, we must commit prior changes to memory
	 * (Documentation/RCU/rculist_nulls.rst for details)
	 */
	smp_wmb();
	refcount_set(&sk->sk_refcnt, 1);
	atomic_set(&sk->sk_drops, 0);
}
EXPORT_SYMBOL(sock_init_data_uid);

void sock_init_data(struct socket *sock, struct sock *sk)
{
	kuid_t uid = sock ?
		SOCK_INODE(sock)->i_uid :
		make_kuid(sock_net(sk)->user_ns, 0);

	sock_init_data_uid(sock, sk, uid);
}
EXPORT_SYMBOL(sock_init_data);

void lock_sock_nested(struct sock *sk, int subclass)
{
	/* The sk_lock has mutex_lock() semantics here. */
	mutex_acquire(&sk->sk_lock.dep_map, subclass, 0, _RET_IP_);

	might_sleep();
	spin_lock_bh(&sk->sk_lock.slock);
	if (sock_owned_by_user_nocheck(sk))
		__lock_sock(sk);
	sk->sk_lock.owned = 1;
	spin_unlock_bh(&sk->sk_lock.slock);
}
EXPORT_SYMBOL(lock_sock_nested);

void release_sock(struct sock *sk)
{
	spin_lock_bh(&sk->sk_lock.slock);
	if (sk->sk_backlog.tail)
		__release_sock(sk);

	if (sk->sk_prot->release_cb)
		INDIRECT_CALL_INET_1(sk->sk_prot->release_cb,
				     tcp_release_cb, sk);

	sock_release_ownership(sk);
	if (waitqueue_active(&sk->sk_lock.wq))
		wake_up(&sk->sk_lock.wq);
	spin_unlock_bh(&sk->sk_lock.slock);
}
EXPORT_SYMBOL(release_sock);

bool __lock_sock_fast(struct sock *sk) __acquires(&sk->sk_lock.slock)
{
	might_sleep();
	spin_lock_bh(&sk->sk_lock.slock);

	if (!sock_owned_by_user_nocheck(sk)) {
		/*
		 * Fast path return with bottom halves disabled and
		 * sock::sk_lock.slock held.
		 *
		 * The 'mutex' is not contended and holding
		 * sock::sk_lock.slock prevents all other lockers to
		 * proceed so the corresponding unlock_sock_fast() can
		 * avoid the slow path of release_sock() completely and
		 * just release slock.
		 *
		 * From a semantical POV this is equivalent to 'acquiring'
		 * the 'mutex', hence the corresponding lockdep
		 * mutex_release() has to happen in the fast path of
		 * unlock_sock_fast().
		 */
		return false;
	}

	__lock_sock(sk);
	sk->sk_lock.owned = 1;
	__acquire(&sk->sk_lock.slock);
	spin_unlock_bh(&sk->sk_lock.slock);
	return true;
}
EXPORT_SYMBOL(__lock_sock_fast);

int sock_gettstamp(struct socket *sock, void __user *userstamp,
		   bool timeval, bool time32)
{
	struct sock *sk = sock->sk;
	struct timespec64 ts;

	sock_enable_timestamp(sk, SOCK_TIMESTAMP);
	ts = ktime_to_timespec64(sock_read_timestamp(sk));
	if (ts.tv_sec == -1)
		return -ENOENT;
	if (ts.tv_sec == 0) {
		ktime_t kt = ktime_get_real();
		sock_write_timestamp(sk, kt);
		ts = ktime_to_timespec64(kt);
	}

	if (timeval)
		ts.tv_nsec /= 1000;

#ifdef CONFIG_COMPAT_32BIT_TIME
	if (time32)
		return put_old_timespec32(&ts, userstamp);
#endif
#ifdef CONFIG_SPARC64
	/* beware of padding in sparc64 timeval */
	if (timeval && !in_compat_syscall()) {
		struct __kernel_old_timeval __user tv = {
			.tv_sec = ts.tv_sec,
			.tv_usec = ts.tv_nsec,
		};
		if (copy_to_user(userstamp, &tv, sizeof(tv)))
			return -EFAULT;
		return 0;
	}
#endif
	return put_timespec64(&ts, userstamp);
}
EXPORT_SYMBOL(sock_gettstamp);

void sock_enable_timestamp(struct sock *sk, enum sock_flags flag)
{
	if (!sock_flag(sk, flag)) {
		unsigned long previous_flags = sk->sk_flags;

		sock_set_flag(sk, flag);
		/*
		 * we just set one of the two flags which require net
		 * time stamping, but time stamping might have been on
		 * already because of the other one
		 */
		if (sock_needs_netstamp(sk) &&
		    !(previous_flags & SK_FLAGS_TIMESTAMP))
			net_enable_timestamp();
	}
}

int sock_recv_errqueue(struct sock *sk, struct msghdr *msg, int len,
		       int level, int type)
{
	struct sock_exterr_skb *serr;
	struct sk_buff *skb;
	int copied, err;

	err = -EAGAIN;
	skb = sock_dequeue_err_skb(sk);
	if (skb == NULL)
		goto out;

	copied = skb->len;
	if (copied > len) {
		msg->msg_flags |= MSG_TRUNC;
		copied = len;
	}
	err = skb_copy_datagram_msg(skb, 0, msg, copied);
	if (err)
		goto out_free_skb;

	sock_recv_timestamp(msg, sk, skb);

	serr = SKB_EXT_ERR(skb);
	put_cmsg(msg, level, type, sizeof(serr->ee), &serr->ee);

	msg->msg_flags |= MSG_ERRQUEUE;
	err = copied;

out_free_skb:
	kfree_skb(skb);
out:
	return err;
}
EXPORT_SYMBOL(sock_recv_errqueue);

/*
 *	Get a socket option on an socket.
 *
 *	FIX: POSIX 1003.1g is very ambiguous here. It states that
 *	asynchronous errors should be reported by getsockopt. We assume
 *	this means if you specify SO_ERROR (otherwise whats the point of it).
 */
int sock_common_getsockopt(struct socket *sock, int level, int optname,
			   char __user *optval, int __user *optlen)
{
	struct sock *sk = sock->sk;

	/* IPV6_ADDRFORM can change sk->sk_prot under us. */
	return READ_ONCE(sk->sk_prot)->getsockopt(sk, level, optname, optval, optlen);
}
EXPORT_SYMBOL(sock_common_getsockopt);

int sock_common_recvmsg(struct socket *sock, struct msghdr *msg, size_t size,
			int flags)
{
	struct sock *sk = sock->sk;
	int addr_len = 0;
	int err;

	err = sk->sk_prot->recvmsg(sk, msg, size, flags, &addr_len);
	if (err >= 0)
		msg->msg_namelen = addr_len;
	return err;
}
EXPORT_SYMBOL(sock_common_recvmsg);

/*
 *	Set socket options on an inet socket.
 */
int sock_common_setsockopt(struct socket *sock, int level, int optname,
			   sockptr_t optval, unsigned int optlen)
{
	struct sock *sk = sock->sk;

	/* IPV6_ADDRFORM can change sk->sk_prot under us. */
	return READ_ONCE(sk->sk_prot)->setsockopt(sk, level, optname, optval, optlen);
}
EXPORT_SYMBOL(sock_common_setsockopt);

void sk_common_release(struct sock *sk)
{
	if (sk->sk_prot->destroy)
		sk->sk_prot->destroy(sk);

	/*
	 * Observation: when sk_common_release is called, processes have
	 * no access to socket. But net still has.
	 * Step one, detach it from networking:
	 *
	 * A. Remove from hash tables.
	 */

	sk->sk_prot->unhash(sk);

	/*
	 * In this point socket cannot receive new packets, but it is possible
	 * that some packets are in flight because some CPU runs receiver and
	 * did hash table lookup before we unhashed socket. They will achieve
	 * receive queue and will be purged by socket destructor.
	 *
	 * Also we still have packets pending on receive queue and probably,
	 * our own packets waiting in device queues. sock_destroy will drain
	 * receive queue, but transmitted packets will delay socket destruction
	 * until the last reference will be released.
	 */

	sock_orphan(sk);

	xfrm_sk_free_policy(sk);

	sock_put(sk);
}
EXPORT_SYMBOL(sk_common_release);

void sk_get_meminfo(const struct sock *sk, u32 *mem)
{
	memset(mem, 0, sizeof(*mem) * SK_MEMINFO_VARS);

	mem[SK_MEMINFO_RMEM_ALLOC] = sk_rmem_alloc_get(sk);
	mem[SK_MEMINFO_RCVBUF] = READ_ONCE(sk->sk_rcvbuf);
	mem[SK_MEMINFO_WMEM_ALLOC] = sk_wmem_alloc_get(sk);
	mem[SK_MEMINFO_SNDBUF] = READ_ONCE(sk->sk_sndbuf);
	mem[SK_MEMINFO_FWD_ALLOC] = sk_forward_alloc_get(sk);
	mem[SK_MEMINFO_WMEM_QUEUED] = READ_ONCE(sk->sk_wmem_queued);
	mem[SK_MEMINFO_OPTMEM] = atomic_read(&sk->sk_omem_alloc);
	mem[SK_MEMINFO_BACKLOG] = READ_ONCE(sk->sk_backlog.len);
	mem[SK_MEMINFO_DROPS] = atomic_read(&sk->sk_drops);
}

#ifdef CONFIG_PROC_FS
static DECLARE_BITMAP(proto_inuse_idx, PROTO_INUSE_NR);

int sock_prot_inuse_get(struct net *net, struct proto *prot)
{
	int cpu, idx = prot->inuse_idx;
	int res = 0;

	for_each_possible_cpu(cpu)
		res += per_cpu_ptr(net->core.prot_inuse, cpu)->val[idx];

	return res >= 0 ? res : 0;
}
EXPORT_SYMBOL_GPL(sock_prot_inuse_get);

int sock_inuse_get(struct net *net)
{
	int cpu, res = 0;

	for_each_possible_cpu(cpu)
		res += per_cpu_ptr(net->core.prot_inuse, cpu)->all;

	return res;
}

EXPORT_SYMBOL_GPL(sock_inuse_get);

static int __net_init sock_inuse_init_net(struct net *net)
{
	net->core.prot_inuse = alloc_percpu(struct prot_inuse);
	if (net->core.prot_inuse == NULL)
		return -ENOMEM;
	return 0;
}

static void __net_exit sock_inuse_exit_net(struct net *net)
{
	free_percpu(net->core.prot_inuse);
}

static struct pernet_operations net_inuse_ops = {
	.init = sock_inuse_init_net,
	.exit = sock_inuse_exit_net,
};

static __init int net_inuse_init(void)
{
	if (register_pernet_subsys(&net_inuse_ops))
		panic("Cannot initialize net inuse counters");

	return 0;
}

core_initcall(net_inuse_init);

static int assign_proto_idx(struct proto *prot)
{
	prot->inuse_idx = find_first_zero_bit(proto_inuse_idx, PROTO_INUSE_NR);

	if (unlikely(prot->inuse_idx == PROTO_INUSE_NR - 1)) {
		pr_err("PROTO_INUSE_NR exhausted\n");
		return -ENOSPC;
	}

	set_bit(prot->inuse_idx, proto_inuse_idx);
	return 0;
}

static void release_proto_idx(struct proto *prot)
{
	if (prot->inuse_idx != PROTO_INUSE_NR - 1)
		clear_bit(prot->inuse_idx, proto_inuse_idx);
}
#else
static inline int assign_proto_idx(struct proto *prot)
{
	return 0;
}

static inline void release_proto_idx(struct proto *prot)
{
}

#endif

static void tw_prot_cleanup(struct timewait_sock_ops *twsk_prot)
{
	if (!twsk_prot)
		return;
	kfree(twsk_prot->twsk_slab_name);
	twsk_prot->twsk_slab_name = NULL;
	kmem_cache_destroy(twsk_prot->twsk_slab);
	twsk_prot->twsk_slab = NULL;
}

static int tw_prot_init(const struct proto *prot)
{
	struct timewait_sock_ops *twsk_prot = prot->twsk_prot;

	if (!twsk_prot)
		return 0;

	twsk_prot->twsk_slab_name = kasprintf(GFP_KERNEL, "tw_sock_%s",
					      prot->name);
	if (!twsk_prot->twsk_slab_name)
		return -ENOMEM;

	twsk_prot->twsk_slab =
		kmem_cache_create(twsk_prot->twsk_slab_name,
				  twsk_prot->twsk_obj_size, 0,
				  SLAB_ACCOUNT | prot->slab_flags,
				  NULL);
	if (!twsk_prot->twsk_slab) {
		pr_crit("%s: Can't create timewait sock SLAB cache!\n",
			prot->name);
		return -ENOMEM;
	}

	return 0;
}

static void req_prot_cleanup(struct request_sock_ops *rsk_prot)
{
	if (!rsk_prot)
		return;
	kfree(rsk_prot->slab_name);
	rsk_prot->slab_name = NULL;
	kmem_cache_destroy(rsk_prot->slab);
	rsk_prot->slab = NULL;
}

static int req_prot_init(const struct proto *prot)
{
	struct request_sock_ops *rsk_prot = prot->rsk_prot;

	if (!rsk_prot)
		return 0;

	rsk_prot->slab_name = kasprintf(GFP_KERNEL, "request_sock_%s",
					prot->name);
	if (!rsk_prot->slab_name)
		return -ENOMEM;

	rsk_prot->slab = kmem_cache_create(rsk_prot->slab_name,
					   rsk_prot->obj_size, 0,
					   SLAB_ACCOUNT | prot->slab_flags,
					   NULL);

	if (!rsk_prot->slab) {
		pr_crit("%s: Can't create request sock SLAB cache!\n",
			prot->name);
		return -ENOMEM;
	}
	return 0;
}

int proto_register(struct proto *prot, int alloc_slab)
{
	int ret = -ENOBUFS;

	if (prot->memory_allocated && !prot->sysctl_mem) {
		pr_err("%s: missing sysctl_mem\n", prot->name);
		return -EINVAL;
	}
	if (prot->memory_allocated && !prot->per_cpu_fw_alloc) {
		pr_err("%s: missing per_cpu_fw_alloc\n", prot->name);
		return -EINVAL;
	}
	if (alloc_slab) {
		prot->slab = kmem_cache_create_usercopy(prot->name,
					prot->obj_size, 0,
					SLAB_HWCACHE_ALIGN | SLAB_ACCOUNT |
					prot->slab_flags,
					prot->useroffset, prot->usersize,
					NULL);

		if (prot->slab == NULL) {
			pr_crit("%s: Can't create sock SLAB cache!\n",
				prot->name);
			goto out;
		}

		if (req_prot_init(prot))
			goto out_free_request_sock_slab;

		if (tw_prot_init(prot))
			goto out_free_timewait_sock_slab;
	}

	mutex_lock(&proto_list_mutex);
	ret = assign_proto_idx(prot);
	if (ret) {
		mutex_unlock(&proto_list_mutex);
		goto out_free_timewait_sock_slab;
	}
	list_add(&prot->node, &proto_list);
	mutex_unlock(&proto_list_mutex);
	return ret;

out_free_timewait_sock_slab:
	if (alloc_slab)
		tw_prot_cleanup(prot->twsk_prot);
out_free_request_sock_slab:
	if (alloc_slab) {
		req_prot_cleanup(prot->rsk_prot);

		kmem_cache_destroy(prot->slab);
		prot->slab = NULL;
	}
out:
	return ret;
}
EXPORT_SYMBOL(proto_register);

void proto_unregister(struct proto *prot)
{
	mutex_lock(&proto_list_mutex);
	release_proto_idx(prot);
	list_del(&prot->node);
	mutex_unlock(&proto_list_mutex);

	kmem_cache_destroy(prot->slab);
	prot->slab = NULL;

	req_prot_cleanup(prot->rsk_prot);
	tw_prot_cleanup(prot->twsk_prot);
}
EXPORT_SYMBOL(proto_unregister);

int sock_load_diag_module(int family, int protocol)
{
	if (!protocol) {
		if (!sock_is_registered(family))
			return -ENOENT;

		return request_module("net-pf-%d-proto-%d-type-%d", PF_NETLINK,
				      NETLINK_SOCK_DIAG, family);
	}

#ifdef CONFIG_INET
	if (family == AF_INET &&
	    protocol != IPPROTO_RAW &&
	    protocol < MAX_INET_PROTOS &&
	    !rcu_access_pointer(inet_protos[protocol]))
		return -ENOENT;
#endif

	return request_module("net-pf-%d-proto-%d-type-%d-%d", PF_NETLINK,
			      NETLINK_SOCK_DIAG, family, protocol);
}
EXPORT_SYMBOL(sock_load_diag_module);

#ifdef CONFIG_PROC_FS
static void *proto_seq_start(struct seq_file *seq, loff_t *pos)
	__acquires(proto_list_mutex)
{
	mutex_lock(&proto_list_mutex);
	return seq_list_start_head(&proto_list, *pos);
}

static void *proto_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	return seq_list_next(v, &proto_list, pos);
}

static void proto_seq_stop(struct seq_file *seq, void *v)
	__releases(proto_list_mutex)
{
	mutex_unlock(&proto_list_mutex);
}

static char proto_method_implemented(const void *method)
{
	return method == NULL ? 'n' : 'y';
}
static long sock_prot_memory_allocated(struct proto *proto)
{
	return proto->memory_allocated != NULL ? proto_memory_allocated(proto) : -1L;
}

static const char *sock_prot_memory_pressure(struct proto *proto)
{
	return proto->memory_pressure != NULL ?
	proto_memory_pressure(proto) ? "yes" : "no" : "NI";
}

static void proto_seq_printf(struct seq_file *seq, struct proto *proto)
{

	seq_printf(seq, "%-9s %4u %6d  %6ld   %-3s %6u   %-3s  %-10s "
			"%2c %2c %2c %2c %2c %2c %2c %2c %2c %2c %2c %2c %2c %2c %2c %2c %2c %2c\n",
		   proto->name,
		   proto->obj_size,
		   sock_prot_inuse_get(seq_file_net(seq), proto),
		   sock_prot_memory_allocated(proto),
		   sock_prot_memory_pressure(proto),
		   proto->max_header,
		   proto->slab == NULL ? "no" : "yes",
		   module_name(proto->owner),
		   proto_method_implemented(proto->close),
		   proto_method_implemented(proto->connect),
		   proto_method_implemented(proto->disconnect),
		   proto_method_implemented(proto->accept),
		   proto_method_implemented(proto->ioctl),
		   proto_method_implemented(proto->init),
		   proto_method_implemented(proto->destroy),
		   proto_method_implemented(proto->shutdown),
		   proto_method_implemented(proto->setsockopt),
		   proto_method_implemented(proto->getsockopt),
		   proto_method_implemented(proto->sendmsg),
		   proto_method_implemented(proto->recvmsg),
		   proto_method_implemented(proto->bind),
		   proto_method_implemented(proto->backlog_rcv),
		   proto_method_implemented(proto->hash),
		   proto_method_implemented(proto->unhash),
		   proto_method_implemented(proto->get_port),
		   proto_method_implemented(proto->enter_memory_pressure));
}

static int proto_seq_show(struct seq_file *seq, void *v)
{
	if (v == &proto_list)
		seq_printf(seq, "%-9s %-4s %-8s %-6s %-5s %-7s %-4s %-10s %s",
			   "protocol",
			   "size",
			   "sockets",
			   "memory",
			   "press",
			   "maxhdr",
			   "slab",
			   "module",
			   "cl co di ac io in de sh ss gs se re bi br ha uh gp em\n");
	else
		proto_seq_printf(seq, list_entry(v, struct proto, node));
	return 0;
}

static const struct seq_operations proto_seq_ops = {
	.start  = proto_seq_start,
	.next   = proto_seq_next,
	.stop   = proto_seq_stop,
	.show   = proto_seq_show,
};

static __net_init int proto_init_net(struct net *net)
{
	if (!proc_create_net("protocols", 0444, net->proc_net, &proto_seq_ops,
			sizeof(struct seq_net_private)))
		return -ENOMEM;

	return 0;
}

static __net_exit void proto_exit_net(struct net *net)
{
	remove_proc_entry("protocols", net->proc_net);
}


static __net_initdata struct pernet_operations proto_net_ops = {
	.init = proto_init_net,
	.exit = proto_exit_net,
};

static int __init proto_init(void)
{
	return register_pernet_subsys(&proto_net_ops);
}

subsys_initcall(proto_init);

#endif /* PROC_FS */

#ifdef CONFIG_NET_RX_BUSY_POLL
bool sk_busy_loop_end(void *p, unsigned long start_time)
{
	struct sock *sk = p;

	if (!skb_queue_empty_lockless(&sk->sk_receive_queue))
		return true;

	if (sk_is_udp(sk) &&
	    !skb_queue_empty_lockless(&udp_sk(sk)->reader_queue))
		return true;

	return sk_busy_loop_timeout(sk, start_time);
}
EXPORT_SYMBOL(sk_busy_loop_end);
#endif /* CONFIG_NET_RX_BUSY_POLL */

int sock_bind_add(struct sock *sk, struct sockaddr *addr, int addr_len)
{
	if (!sk->sk_prot->bind_add)
		return -EOPNOTSUPP;
	return sk->sk_prot->bind_add(sk, addr, addr_len);
}
EXPORT_SYMBOL(sock_bind_add);

/* Copy 'size' bytes from userspace and return `size` back to userspace */
int sock_ioctl_inout(struct sock *sk, unsigned int cmd,
		     void __user *arg, void *karg, size_t size)
{
	int ret;

	if (copy_from_user(karg, arg, size))
		return -EFAULT;

	ret = READ_ONCE(sk->sk_prot)->ioctl(sk, cmd, karg);
	if (ret)
		return ret;

	if (copy_to_user(arg, karg, size))
		return -EFAULT;

	return 0;
}
EXPORT_SYMBOL(sock_ioctl_inout);

/* This is the most common ioctl prep function, where the result (4 bytes) is
 * copied back to userspace if the ioctl() returns successfully. No input is
 * copied from userspace as input argument.
 */
static int sock_ioctl_out(struct sock *sk, unsigned int cmd, void __user *arg)
{
	int ret, karg = 0;

	ret = READ_ONCE(sk->sk_prot)->ioctl(sk, cmd, &karg);
	if (ret)
		return ret;

	return put_user(karg, (int __user *)arg);
}

/* A wrapper around sock ioctls, which copies the data from userspace
 * (depending on the protocol/ioctl), and copies back the result to userspace.
 * The main motivation for this function is to pass kernel memory to the
 * protocol ioctl callbacks, instead of userspace memory.
 */
int sk_ioctl(struct sock *sk, unsigned int cmd, void __user *arg)
{
	int rc = 1;

	if (sk->sk_type == SOCK_RAW && sk->sk_family == AF_INET)
		rc = ipmr_sk_ioctl(sk, cmd, arg);
	else if (sk->sk_type == SOCK_RAW && sk->sk_family == AF_INET6)
		rc = ip6mr_sk_ioctl(sk, cmd, arg);
	else if (sk_is_phonet(sk))
		rc = phonet_sk_ioctl(sk, cmd, arg);

	/* If ioctl was processed, returns its value */
	if (rc <= 0)
		return rc;

	/* Otherwise call the default handler */
	return sock_ioctl_out(sk, cmd, arg);
}
EXPORT_SYMBOL(sk_ioctl);

static int __init sock_struct_check(void)
{
	CACHELINE_ASSERT_GROUP_MEMBER(struct sock, sock_write_rx, sk_drops);
	CACHELINE_ASSERT_GROUP_MEMBER(struct sock, sock_write_rx, sk_peek_off);
	CACHELINE_ASSERT_GROUP_MEMBER(struct sock, sock_write_rx, sk_error_queue);
	CACHELINE_ASSERT_GROUP_MEMBER(struct sock, sock_write_rx, sk_receive_queue);
	CACHELINE_ASSERT_GROUP_MEMBER(struct sock, sock_write_rx, sk_backlog);

	CACHELINE_ASSERT_GROUP_MEMBER(struct sock, sock_read_rx, sk_rx_dst);
	CACHELINE_ASSERT_GROUP_MEMBER(struct sock, sock_read_rx, sk_rx_dst_ifindex);
	CACHELINE_ASSERT_GROUP_MEMBER(struct sock, sock_read_rx, sk_rx_dst_cookie);
	CACHELINE_ASSERT_GROUP_MEMBER(struct sock, sock_read_rx, sk_rcvbuf);
	CACHELINE_ASSERT_GROUP_MEMBER(struct sock, sock_read_rx, sk_filter);
	CACHELINE_ASSERT_GROUP_MEMBER(struct sock, sock_read_rx, sk_wq);
	CACHELINE_ASSERT_GROUP_MEMBER(struct sock, sock_read_rx, sk_data_ready);
	CACHELINE_ASSERT_GROUP_MEMBER(struct sock, sock_read_rx, sk_rcvtimeo);
	CACHELINE_ASSERT_GROUP_MEMBER(struct sock, sock_read_rx, sk_rcvlowat);

	CACHELINE_ASSERT_GROUP_MEMBER(struct sock, sock_read_rxtx, sk_err);
	CACHELINE_ASSERT_GROUP_MEMBER(struct sock, sock_read_rxtx, sk_socket);
	CACHELINE_ASSERT_GROUP_MEMBER(struct sock, sock_read_rxtx, sk_memcg);

	CACHELINE_ASSERT_GROUP_MEMBER(struct sock, sock_write_rxtx, sk_lock);
	CACHELINE_ASSERT_GROUP_MEMBER(struct sock, sock_write_rxtx, sk_reserved_mem);
	CACHELINE_ASSERT_GROUP_MEMBER(struct sock, sock_write_rxtx, sk_forward_alloc);
	CACHELINE_ASSERT_GROUP_MEMBER(struct sock, sock_write_rxtx, sk_tsflags);

	CACHELINE_ASSERT_GROUP_MEMBER(struct sock, sock_write_tx, sk_omem_alloc);
	CACHELINE_ASSERT_GROUP_MEMBER(struct sock, sock_write_tx, sk_omem_alloc);
	CACHELINE_ASSERT_GROUP_MEMBER(struct sock, sock_write_tx, sk_sndbuf);
	CACHELINE_ASSERT_GROUP_MEMBER(struct sock, sock_write_tx, sk_wmem_queued);
	CACHELINE_ASSERT_GROUP_MEMBER(struct sock, sock_write_tx, sk_wmem_alloc);
	CACHELINE_ASSERT_GROUP_MEMBER(struct sock, sock_write_tx, sk_tsq_flags);
	CACHELINE_ASSERT_GROUP_MEMBER(struct sock, sock_write_tx, sk_send_head);
	CACHELINE_ASSERT_GROUP_MEMBER(struct sock, sock_write_tx, sk_write_queue);
	CACHELINE_ASSERT_GROUP_MEMBER(struct sock, sock_write_tx, sk_write_pending);
	CACHELINE_ASSERT_GROUP_MEMBER(struct sock, sock_write_tx, sk_dst_pending_confirm);
	CACHELINE_ASSERT_GROUP_MEMBER(struct sock, sock_write_tx, sk_pacing_status);
	CACHELINE_ASSERT_GROUP_MEMBER(struct sock, sock_write_tx, sk_frag);
	CACHELINE_ASSERT_GROUP_MEMBER(struct sock, sock_write_tx, sk_timer);
	CACHELINE_ASSERT_GROUP_MEMBER(struct sock, sock_write_tx, sk_pacing_rate);
	CACHELINE_ASSERT_GROUP_MEMBER(struct sock, sock_write_tx, sk_zckey);
	CACHELINE_ASSERT_GROUP_MEMBER(struct sock, sock_write_tx, sk_tskey);

	CACHELINE_ASSERT_GROUP_MEMBER(struct sock, sock_read_tx, sk_max_pacing_rate);
	CACHELINE_ASSERT_GROUP_MEMBER(struct sock, sock_read_tx, sk_sndtimeo);
	CACHELINE_ASSERT_GROUP_MEMBER(struct sock, sock_read_tx, sk_priority);
	CACHELINE_ASSERT_GROUP_MEMBER(struct sock, sock_read_tx, sk_mark);
	CACHELINE_ASSERT_GROUP_MEMBER(struct sock, sock_read_tx, sk_dst_cache);
	CACHELINE_ASSERT_GROUP_MEMBER(struct sock, sock_read_tx, sk_route_caps);
	CACHELINE_ASSERT_GROUP_MEMBER(struct sock, sock_read_tx, sk_gso_type);
	CACHELINE_ASSERT_GROUP_MEMBER(struct sock, sock_read_tx, sk_gso_max_size);
	CACHELINE_ASSERT_GROUP_MEMBER(struct sock, sock_read_tx, sk_allocation);
	CACHELINE_ASSERT_GROUP_MEMBER(struct sock, sock_read_tx, sk_txhash);
	CACHELINE_ASSERT_GROUP_MEMBER(struct sock, sock_read_tx, sk_gso_max_segs);
	CACHELINE_ASSERT_GROUP_MEMBER(struct sock, sock_read_tx, sk_pacing_shift);
	CACHELINE_ASSERT_GROUP_MEMBER(struct sock, sock_read_tx, sk_use_task_frag);
	return 0;
}

core_initcall(sock_struct_check);
