// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Support for INET connection oriented protocols.
 *
 * Authors:	See the TCP sources
 */

#include <linux/module.h>
#include <linux/jhash.h>

#include <net/inet_connection_sock.h>
#include <net/inet_hashtables.h>
#include <net/inet_timewait_sock.h>
#include <net/ip.h>
#include <net/route.h>
#include <net/tcp_states.h>
#include <net/xfrm.h>
#include <net/tcp.h>
#include <net/sock_reuseport.h>
#include <net/addrconf.h>

#if IS_ENABLED(CONFIG_IPV6)
/* match_wildcard == true:  IPV6_ADDR_ANY equals to any IPv6 addresses if IPv6
 *                          only, and any IPv4 addresses if not IPv6 only
 * match_wildcard == false: addresses must be exactly the same, i.e.
 *                          IPV6_ADDR_ANY only equals to IPV6_ADDR_ANY,
 *                          and 0.0.0.0 equals to 0.0.0.0 only
 */
static bool ipv6_rcv_saddr_equal(const struct in6_addr *sk1_rcv_saddr6,
				 const struct in6_addr *sk2_rcv_saddr6,
				 __be32 sk1_rcv_saddr, __be32 sk2_rcv_saddr,
				 bool sk1_ipv6only, bool sk2_ipv6only,
				 bool match_wildcard)
{
	int addr_type = ipv6_addr_type(sk1_rcv_saddr6);
	int addr_type2 = sk2_rcv_saddr6 ? ipv6_addr_type(sk2_rcv_saddr6) : IPV6_ADDR_MAPPED;

	/* if both are mapped, treat as IPv4 */
	if (addr_type == IPV6_ADDR_MAPPED && addr_type2 == IPV6_ADDR_MAPPED) {
		if (!sk2_ipv6only) {
			if (sk1_rcv_saddr == sk2_rcv_saddr)
				return true;
			if (!sk1_rcv_saddr || !sk2_rcv_saddr)
				return match_wildcard;
		}
		return false;
	}

	if (addr_type == IPV6_ADDR_ANY && addr_type2 == IPV6_ADDR_ANY)
		return true;

	if (addr_type2 == IPV6_ADDR_ANY && match_wildcard &&
	    !(sk2_ipv6only && addr_type == IPV6_ADDR_MAPPED))
		return true;

	if (addr_type == IPV6_ADDR_ANY && match_wildcard &&
	    !(sk1_ipv6only && addr_type2 == IPV6_ADDR_MAPPED))
		return true;

	if (sk2_rcv_saddr6 &&
	    ipv6_addr_equal(sk1_rcv_saddr6, sk2_rcv_saddr6))
		return true;

	return false;
}
#endif

/* match_wildcard == true:  0.0.0.0 equals to any IPv4 addresses
 * match_wildcard == false: addresses must be exactly the same, i.e.
 *                          0.0.0.0 only equals to 0.0.0.0
 */
static bool ipv4_rcv_saddr_equal(__be32 sk1_rcv_saddr, __be32 sk2_rcv_saddr,
				 bool sk2_ipv6only, bool match_wildcard)
{
	if (!sk2_ipv6only) {
		if (sk1_rcv_saddr == sk2_rcv_saddr)
			return true;
		if (!sk1_rcv_saddr || !sk2_rcv_saddr)
			return match_wildcard;
	}
	return false;
}

bool inet_rcv_saddr_equal(const struct sock *sk, const struct sock *sk2,
			  bool match_wildcard)
{
#if IS_ENABLED(CONFIG_IPV6)
	if (sk->sk_family == AF_INET6)
		return ipv6_rcv_saddr_equal(&sk->sk_v6_rcv_saddr,
					    inet6_rcv_saddr(sk2),
					    sk->sk_rcv_saddr,
					    sk2->sk_rcv_saddr,
					    ipv6_only_sock(sk),
					    ipv6_only_sock(sk2),
					    match_wildcard);
#endif
	return ipv4_rcv_saddr_equal(sk->sk_rcv_saddr, sk2->sk_rcv_saddr,
				    ipv6_only_sock(sk2), match_wildcard);
}
EXPORT_SYMBOL(inet_rcv_saddr_equal);

bool inet_rcv_saddr_any(const struct sock *sk)
{
#if IS_ENABLED(CONFIG_IPV6)
	if (sk->sk_family == AF_INET6)
		return ipv6_addr_any(&sk->sk_v6_rcv_saddr);
#endif
	return !sk->sk_rcv_saddr;
}

void inet_get_local_port_range(struct net *net, int *low, int *high)
{
	unsigned int seq;

	do {
		seq = read_seqbegin(&net->ipv4.ip_local_ports.lock);

		*low = net->ipv4.ip_local_ports.range[0];
		*high = net->ipv4.ip_local_ports.range[1];
	} while (read_seqretry(&net->ipv4.ip_local_ports.lock, seq));
}
EXPORT_SYMBOL(inet_get_local_port_range);

static int inet_csk_bind_conflict(const struct sock *sk,
				  const struct inet_bind_bucket *tb,
				  bool relax, bool reuseport_ok)
{
	struct sock *sk2;
	bool reuse = sk->sk_reuse;
	bool reuseport = !!sk->sk_reuseport && reuseport_ok;
	kuid_t uid = sock_i_uid((struct sock *)sk);

	/*
	 * Unlike other sk lookup places we do not check
	 * for sk_net here, since _all_ the socks listed
	 * in tb->owners list belong to the same net - the
	 * one this bucket belongs to.
	 */

	sk_for_each_bound(sk2, &tb->owners) {
		if (sk != sk2 &&
		    (!sk->sk_bound_dev_if ||
		     !sk2->sk_bound_dev_if ||
		     sk->sk_bound_dev_if == sk2->sk_bound_dev_if)) {
			if ((!reuse || !sk2->sk_reuse ||
			    sk2->sk_state == TCP_LISTEN) &&
			    (!reuseport || !sk2->sk_reuseport ||
			     rcu_access_pointer(sk->sk_reuseport_cb) ||
			     (sk2->sk_state != TCP_TIME_WAIT &&
			     !uid_eq(uid, sock_i_uid(sk2))))) {
				if (inet_rcv_saddr_equal(sk, sk2, true))
					break;
			}
			if (!relax && reuse && sk2->sk_reuse &&
			    sk2->sk_state != TCP_LISTEN) {
				if (inet_rcv_saddr_equal(sk, sk2, true))
					break;
			}
		}
	}
	return sk2 != NULL;
}

/*
 * Find an open port number for the socket.  Returns with the
 * inet_bind_hashbucket lock held.
 */
static struct inet_bind_hashbucket *
inet_csk_find_open_port(struct sock *sk, struct inet_bind_bucket **tb_ret, int *port_ret)
{
	struct inet_hashinfo *hinfo = sk->sk_prot->h.hashinfo;
	int port = 0;
	struct inet_bind_hashbucket *head;
	struct net *net = sock_net(sk);
	int i, low, high, attempt_half;
	struct inet_bind_bucket *tb;
	u32 remaining, offset;
	int l3mdev;

	l3mdev = inet_sk_bound_l3mdev(sk);
	attempt_half = (sk->sk_reuse == SK_CAN_REUSE) ? 1 : 0;
other_half_scan:
	inet_get_local_port_range(net, &low, &high);
	high++; /* [32768, 60999] -> [32768, 61000[ */
	if (high - low < 4)
		attempt_half = 0;
	if (attempt_half) {
		int half = low + (((high - low) >> 2) << 1);

		if (attempt_half == 1)
			high = half;
		else
			low = half;
	}
	remaining = high - low;
	if (likely(remaining > 1))
		remaining &= ~1U;

	offset = prandom_u32() % remaining;
	/* __inet_hash_connect() favors ports having @low parity
	 * We do the opposite to not pollute connect() users.
	 */
	offset |= 1U;

other_parity_scan:
	port = low + offset;
	for (i = 0; i < remaining; i += 2, port += 2) {
		if (unlikely(port >= high))
			port -= remaining;
		if (inet_is_local_reserved_port(net, port))
			continue;
		head = &hinfo->bhash[inet_bhashfn(net, port,
						  hinfo->bhash_size)];
		spin_lock_bh(&head->lock);
		inet_bind_bucket_for_each(tb, &head->chain)
			if (net_eq(ib_net(tb), net) && tb->l3mdev == l3mdev &&
			    tb->port == port) {
				if (!inet_csk_bind_conflict(sk, tb, false, false))
					goto success;
				goto next_port;
			}
		tb = NULL;
		goto success;
next_port:
		spin_unlock_bh(&head->lock);
		cond_resched();
	}

	offset--;
	if (!(offset & 1))
		goto other_parity_scan;

	if (attempt_half == 1) {
		/* OK we now try the upper half of the range */
		attempt_half = 2;
		goto other_half_scan;
	}
	return NULL;
success:
	*port_ret = port;
	*tb_ret = tb;
	return head;
}

static inline int sk_reuseport_match(struct inet_bind_bucket *tb,
				     struct sock *sk)
{
	kuid_t uid = sock_i_uid(sk);

	if (tb->fastreuseport <= 0)
		return 0;
	if (!sk->sk_reuseport)
		return 0;
	if (rcu_access_pointer(sk->sk_reuseport_cb))
		return 0;
	if (!uid_eq(tb->fastuid, uid))
		return 0;
	/* We only need to check the rcv_saddr if this tb was once marked
	 * without fastreuseport and then was reset, as we can only know that
	 * the fast_*rcv_saddr doesn't have any conflicts with the socks on the
	 * owners list.
	 */
	if (tb->fastreuseport == FASTREUSEPORT_ANY)
		return 1;
#if IS_ENABLED(CONFIG_IPV6)
	if (tb->fast_sk_family == AF_INET6)
		return ipv6_rcv_saddr_equal(&tb->fast_v6_rcv_saddr,
					    inet6_rcv_saddr(sk),
					    tb->fast_rcv_saddr,
					    sk->sk_rcv_saddr,
					    tb->fast_ipv6_only,
					    ipv6_only_sock(sk), true);
#endif
	return ipv4_rcv_saddr_equal(tb->fast_rcv_saddr, sk->sk_rcv_saddr,
				    ipv6_only_sock(sk), true);
}

/* Obtain a reference to a local port for the given sock,
 * if snum is zero it means select any available local port.
 * We try to allocate an odd port (and leave even ports for connect())
 */
int inet_csk_get_port(struct sock *sk, unsigned short snum)
{
	bool reuse = sk->sk_reuse && sk->sk_state != TCP_LISTEN;
	struct inet_hashinfo *hinfo = sk->sk_prot->h.hashinfo;
	int ret = 1, port = snum;
	struct inet_bind_hashbucket *head;
	struct net *net = sock_net(sk);
	struct inet_bind_bucket *tb = NULL;
	kuid_t uid = sock_i_uid(sk);
	int l3mdev;

	l3mdev = inet_sk_bound_l3mdev(sk);

	if (!port) {
		head = inet_csk_find_open_port(sk, &tb, &port);
		if (!head)
			return ret;
		if (!tb)
			goto tb_not_found;
		goto success;
	}
	head = &hinfo->bhash[inet_bhashfn(net, port,
					  hinfo->bhash_size)];
	spin_lock_bh(&head->lock);
	inet_bind_bucket_for_each(tb, &head->chain)
		if (net_eq(ib_net(tb), net) && tb->l3mdev == l3mdev &&
		    tb->port == port)
			goto tb_found;
tb_not_found:
	tb = inet_bind_bucket_create(hinfo->bind_bucket_cachep,
				     net, head, port, l3mdev);
	if (!tb)
		goto fail_unlock;
tb_found:
	if (!hlist_empty(&tb->owners)) {
		if (sk->sk_reuse == SK_FORCE_REUSE)
			goto success;

		if ((tb->fastreuse > 0 && reuse) ||
		    sk_reuseport_match(tb, sk))
			goto success;
		if (inet_csk_bind_conflict(sk, tb, true, true))
			goto fail_unlock;
	}
success:
	if (hlist_empty(&tb->owners)) {
		tb->fastreuse = reuse;
		if (sk->sk_reuseport) {
			tb->fastreuseport = FASTREUSEPORT_ANY;
			tb->fastuid = uid;
			tb->fast_rcv_saddr = sk->sk_rcv_saddr;
			tb->fast_ipv6_only = ipv6_only_sock(sk);
			tb->fast_sk_family = sk->sk_family;
#if IS_ENABLED(CONFIG_IPV6)
			tb->fast_v6_rcv_saddr = sk->sk_v6_rcv_saddr;
#endif
		} else {
			tb->fastreuseport = 0;
		}
	} else {
		if (!reuse)
			tb->fastreuse = 0;
		if (sk->sk_reuseport) {
			/* We didn't match or we don't have fastreuseport set on
			 * the tb, but we have sk_reuseport set on this socket
			 * and we know that there are no bind conflicts with
			 * this socket in this tb, so reset our tb's reuseport
			 * settings so that any subsequent sockets that match
			 * our current socket will be put on the fast path.
			 *
			 * If we reset we need to set FASTREUSEPORT_STRICT so we
			 * do extra checking for all subsequent sk_reuseport
			 * socks.
			 */
			if (!sk_reuseport_match(tb, sk)) {
				tb->fastreuseport = FASTREUSEPORT_STRICT;
				tb->fastuid = uid;
				tb->fast_rcv_saddr = sk->sk_rcv_saddr;
				tb->fast_ipv6_only = ipv6_only_sock(sk);
				tb->fast_sk_family = sk->sk_family;
#if IS_ENABLED(CONFIG_IPV6)
				tb->fast_v6_rcv_saddr = sk->sk_v6_rcv_saddr;
#endif
			}
		} else {
			tb->fastreuseport = 0;
		}
	}
	if (!inet_csk(sk)->icsk_bind_hash)
		inet_bind_hash(sk, tb, port);
	WARN_ON(inet_csk(sk)->icsk_bind_hash != tb);
	ret = 0;

fail_unlock:
	spin_unlock_bh(&head->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(inet_csk_get_port);

/*
 * Wait for an incoming connection, avoid race conditions. This must be called
 * with the socket locked.
 */
static int inet_csk_wait_for_connect(struct sock *sk, long timeo)
{
	struct inet_connection_sock *icsk = inet_csk(sk);
	DEFINE_WAIT(wait);
	int err;

	/*
	 * True wake-one mechanism for incoming connections: only
	 * one process gets woken up, not the 'whole herd'.
	 * Since we do not 'race & poll' for established sockets
	 * anymore, the common case will execute the loop only once.
	 *
	 * Subtle issue: "add_wait_queue_exclusive()" will be added
	 * after any current non-exclusive waiters, and we know that
	 * it will always _stay_ after any new non-exclusive waiters
	 * because all non-exclusive waiters are added at the
	 * beginning of the wait-queue. As such, it's ok to "drop"
	 * our exclusiveness temporarily when we get woken up without
	 * having to remove and re-insert us on the wait queue.
	 */
	for (;;) {
		prepare_to_wait_exclusive(sk_sleep(sk), &wait,
					  TASK_INTERRUPTIBLE);
		release_sock(sk);
		if (reqsk_queue_empty(&icsk->icsk_accept_queue))
			timeo = schedule_timeout(timeo);
		sched_annotate_sleep();
		lock_sock(sk);
		err = 0;
		if (!reqsk_queue_empty(&icsk->icsk_accept_queue))
			break;
		err = -EINVAL;
		if (sk->sk_state != TCP_LISTEN)
			break;
		err = sock_intr_errno(timeo);
		if (signal_pending(current))
			break;
		err = -EAGAIN;
		if (!timeo)
			break;
	}
	finish_wait(sk_sleep(sk), &wait);
	return err;
}

/*
 * This will accept the next outstanding connection.
 */
struct sock *inet_csk_accept(struct sock *sk, int flags, int *err, bool kern)
{
	struct inet_connection_sock *icsk = inet_csk(sk);
	struct request_sock_queue *queue = &icsk->icsk_accept_queue;
	struct request_sock *req;
	struct sock *newsk;
	int error;

	lock_sock(sk);

	/* We need to make sure that this socket is listening,
	 * and that it has something pending.
	 */
	error = -EINVAL;
	if (sk->sk_state != TCP_LISTEN)
		goto out_err;

	/* Find already established connection */
	if (reqsk_queue_empty(queue)) {
		long timeo = sock_rcvtimeo(sk, flags & O_NONBLOCK);

		/* If this is a non blocking socket don't sleep */
		error = -EAGAIN;
		if (!timeo)
			goto out_err;

		error = inet_csk_wait_for_connect(sk, timeo);
		if (error)
			goto out_err;
	}
	req = reqsk_queue_remove(queue, sk);
	newsk = req->sk;

	if (sk->sk_protocol == IPPROTO_TCP &&
	    tcp_rsk(req)->tfo_listener) {
		spin_lock_bh(&queue->fastopenq.lock);
		if (tcp_rsk(req)->tfo_listener) {
			/* We are still waiting for the final ACK from 3WHS
			 * so can't free req now. Instead, we set req->sk to
			 * NULL to signify that the child socket is taken
			 * so reqsk_fastopen_remove() will free the req
			 * when 3WHS finishes (or is aborted).
			 */
			req->sk = NULL;
			req = NULL;
		}
		spin_unlock_bh(&queue->fastopenq.lock);
	}
out:
	release_sock(sk);
	if (req)
		reqsk_put(req);
	return newsk;
out_err:
	newsk = NULL;
	req = NULL;
	*err = error;
	goto out;
}
EXPORT_SYMBOL(inet_csk_accept);

/*
 * Using different timers for retransmit, delayed acks and probes
 * We may wish use just one timer maintaining a list of expire jiffies
 * to optimize.
 */
void inet_csk_init_xmit_timers(struct sock *sk,
			       void (*retransmit_handler)(struct timer_list *t),
			       void (*delack_handler)(struct timer_list *t),
			       void (*keepalive_handler)(struct timer_list *t))
{
	struct inet_connection_sock *icsk = inet_csk(sk);

	timer_setup(&icsk->icsk_retransmit_timer, retransmit_handler, 0);
	timer_setup(&icsk->icsk_delack_timer, delack_handler, 0);
	timer_setup(&sk->sk_timer, keepalive_handler, 0);
	icsk->icsk_pending = icsk->icsk_ack.pending = 0;
}
EXPORT_SYMBOL(inet_csk_init_xmit_timers);

void inet_csk_clear_xmit_timers(struct sock *sk)
{
	struct inet_connection_sock *icsk = inet_csk(sk);

	icsk->icsk_pending = icsk->icsk_ack.pending = icsk->icsk_ack.blocked = 0;

	sk_stop_timer(sk, &icsk->icsk_retransmit_timer);
	sk_stop_timer(sk, &icsk->icsk_delack_timer);
	sk_stop_timer(sk, &sk->sk_timer);
}
EXPORT_SYMBOL(inet_csk_clear_xmit_timers);

void inet_csk_delete_keepalive_timer(struct sock *sk)
{
	sk_stop_timer(sk, &sk->sk_timer);
}
EXPORT_SYMBOL(inet_csk_delete_keepalive_timer);

void inet_csk_reset_keepalive_timer(struct sock *sk, unsigned long len)
{
	sk_reset_timer(sk, &sk->sk_timer, jiffies + len);
}
EXPORT_SYMBOL(inet_csk_reset_keepalive_timer);

struct dst_entry *inet_csk_route_req(const struct sock *sk,
				     struct flowi4 *fl4,
				     const struct request_sock *req)
{
	const struct inet_request_sock *ireq = inet_rsk(req);
	struct net *net = read_pnet(&ireq->ireq_net);
	struct ip_options_rcu *opt;
	struct rtable *rt;

	rcu_read_lock();
	opt = rcu_dereference(ireq->ireq_opt);

	flowi4_init_output(fl4, ireq->ir_iif, ireq->ir_mark,
			   RT_CONN_FLAGS(sk), RT_SCOPE_UNIVERSE,
			   sk->sk_protocol, inet_sk_flowi_flags(sk),
			   (opt && opt->opt.srr) ? opt->opt.faddr : ireq->ir_rmt_addr,
			   ireq->ir_loc_addr, ireq->ir_rmt_port,
			   htons(ireq->ir_num), sk->sk_uid);
	security_req_classify_flow(req, flowi4_to_flowi(fl4));
	rt = ip_route_output_flow(net, fl4, sk);
	if (IS_ERR(rt))
		goto no_route;
	if (opt && opt->opt.is_strictroute && rt->rt_uses_gateway)
		goto route_err;
	rcu_read_unlock();
	return &rt->dst;

route_err:
	ip_rt_put(rt);
no_route:
	rcu_read_unlock();
	__IP_INC_STATS(net, IPSTATS_MIB_OUTNOROUTES);
	return NULL;
}
EXPORT_SYMBOL_GPL(inet_csk_route_req);

struct dst_entry *inet_csk_route_child_sock(const struct sock *sk,
					    struct sock *newsk,
					    const struct request_sock *req)
{
	const struct inet_request_sock *ireq = inet_rsk(req);
	struct net *net = read_pnet(&ireq->ireq_net);
	struct inet_sock *newinet = inet_sk(newsk);
	struct ip_options_rcu *opt;
	struct flowi4 *fl4;
	struct rtable *rt;

	opt = rcu_dereference(ireq->ireq_opt);
	fl4 = &newinet->cork.fl.u.ip4;

	flowi4_init_output(fl4, ireq->ir_iif, ireq->ir_mark,
			   RT_CONN_FLAGS(sk), RT_SCOPE_UNIVERSE,
			   sk->sk_protocol, inet_sk_flowi_flags(sk),
			   (opt && opt->opt.srr) ? opt->opt.faddr : ireq->ir_rmt_addr,
			   ireq->ir_loc_addr, ireq->ir_rmt_port,
			   htons(ireq->ir_num), sk->sk_uid);
	security_req_classify_flow(req, flowi4_to_flowi(fl4));
	rt = ip_route_output_flow(net, fl4, sk);
	if (IS_ERR(rt))
		goto no_route;
	if (opt && opt->opt.is_strictroute && rt->rt_uses_gateway)
		goto route_err;
	return &rt->dst;

route_err:
	ip_rt_put(rt);
no_route:
	__IP_INC_STATS(net, IPSTATS_MIB_OUTNOROUTES);
	return NULL;
}
EXPORT_SYMBOL_GPL(inet_csk_route_child_sock);

#if IS_ENABLED(CONFIG_IPV6)
#define AF_INET_FAMILY(fam) ((fam) == AF_INET)
#else
#define AF_INET_FAMILY(fam) true
#endif

/* Decide when to expire the request and when to resend SYN-ACK */
static inline void syn_ack_recalc(struct request_sock *req, const int thresh,
				  const int max_retries,
				  const u8 rskq_defer_accept,
				  int *expire, int *resend)
{
	if (!rskq_defer_accept) {
		*expire = req->num_timeout >= thresh;
		*resend = 1;
		return;
	}
	*expire = req->num_timeout >= thresh &&
		  (!inet_rsk(req)->acked || req->num_timeout >= max_retries);
	/*
	 * Do not resend while waiting for data after ACK,
	 * start to resend on end of deferring period to give
	 * last chance for data or ACK to create established socket.
	 */
	*resend = !inet_rsk(req)->acked ||
		  req->num_timeout >= rskq_defer_accept - 1;
}

int inet_rtx_syn_ack(const struct sock *parent, struct request_sock *req)
{
	int err = req->rsk_ops->rtx_syn_ack(parent, req);

	if (!err)
		req->num_retrans++;
	return err;
}
EXPORT_SYMBOL(inet_rtx_syn_ack);

/* return true if req was found in the ehash table */
static bool reqsk_queue_unlink(struct request_sock *req)
{
	struct inet_hashinfo *hashinfo = req_to_sk(req)->sk_prot->h.hashinfo;
	bool found = false;

	if (sk_hashed(req_to_sk(req))) {
		spinlock_t *lock = inet_ehash_lockp(hashinfo, req->rsk_hash);

		spin_lock(lock);
		found = __sk_nulls_del_node_init_rcu(req_to_sk(req));
		spin_unlock(lock);
	}
	if (timer_pending(&req->rsk_timer) && del_timer_sync(&req->rsk_timer))
		reqsk_put(req);
	return found;
}

void inet_csk_reqsk_queue_drop(struct sock *sk, struct request_sock *req)
{
	if (reqsk_queue_unlink(req)) {
		reqsk_queue_removed(&inet_csk(sk)->icsk_accept_queue, req);
		reqsk_put(req);
	}
}
EXPORT_SYMBOL(inet_csk_reqsk_queue_drop);

void inet_csk_reqsk_queue_drop_and_put(struct sock *sk, struct request_sock *req)
{
	inet_csk_reqsk_queue_drop(sk, req);
	reqsk_put(req);
}
EXPORT_SYMBOL(inet_csk_reqsk_queue_drop_and_put);

static void reqsk_timer_handler(struct timer_list *t)
{
	struct request_sock *req = from_timer(req, t, rsk_timer);
	struct sock *sk_listener = req->rsk_listener;
	struct net *net = sock_net(sk_listener);
	struct inet_connection_sock *icsk = inet_csk(sk_listener);
	struct request_sock_queue *queue = &icsk->icsk_accept_queue;
	int qlen, expire = 0, resend = 0;
	int max_retries, thresh;
	u8 defer_accept;

	if (inet_sk_state_load(sk_listener) != TCP_LISTEN)
		goto drop;

	max_retries = icsk->icsk_syn_retries ? : net->ipv4.sysctl_tcp_synack_retries;
	thresh = max_retries;
	/* Normally all the openreqs are young and become mature
	 * (i.e. converted to established socket) for first timeout.
	 * If synack was not acknowledged for 1 second, it means
	 * one of the following things: synack was lost, ack was lost,
	 * rtt is high or nobody planned to ack (i.e. synflood).
	 * When server is a bit loaded, queue is populated with old
	 * open requests, reducing effective size of queue.
	 * When server is well loaded, queue size reduces to zero
	 * after several minutes of work. It is not synflood,
	 * it is normal operation. The solution is pruning
	 * too old entries overriding normal timeout, when
	 * situation becomes dangerous.
	 *
	 * Essentially, we reserve half of room for young
	 * embrions; and abort old ones without pity, if old
	 * ones are about to clog our table.
	 */
	qlen = reqsk_queue_len(queue);
	if ((qlen << 1) > max(8U, sk_listener->sk_max_ack_backlog)) {
		int young = reqsk_queue_len_young(queue) << 1;

		while (thresh > 2) {
			if (qlen < young)
				break;
			thresh--;
			young <<= 1;
		}
	}
	defer_accept = READ_ONCE(queue->rskq_defer_accept);
	if (defer_accept)
		max_retries = defer_accept;
	syn_ack_recalc(req, thresh, max_retries, defer_accept,
		       &expire, &resend);
	req->rsk_ops->syn_ack_timeout(req);
	if (!expire &&
	    (!resend ||
	     !inet_rtx_syn_ack(sk_listener, req) ||
	     inet_rsk(req)->acked)) {
		unsigned long timeo;

		if (req->num_timeout++ == 0)
			atomic_dec(&queue->young);
		timeo = min(TCP_TIMEOUT_INIT << req->num_timeout, TCP_RTO_MAX);
		mod_timer(&req->rsk_timer, jiffies + timeo);
		return;
	}
drop:
	inet_csk_reqsk_queue_drop_and_put(sk_listener, req);
}

static void reqsk_queue_hash_req(struct request_sock *req,
				 unsigned long timeout)
{
	timer_setup(&req->rsk_timer, reqsk_timer_handler, TIMER_PINNED);
	mod_timer(&req->rsk_timer, jiffies + timeout);

	inet_ehash_insert(req_to_sk(req), NULL);
	/* before letting lookups find us, make sure all req fields
	 * are committed to memory and refcnt initialized.
	 */
	smp_wmb();
	refcount_set(&req->rsk_refcnt, 2 + 1);
}

void inet_csk_reqsk_queue_hash_add(struct sock *sk, struct request_sock *req,
				   unsigned long timeout)
{
	reqsk_queue_hash_req(req, timeout);
	inet_csk_reqsk_queue_added(sk);
}
EXPORT_SYMBOL_GPL(inet_csk_reqsk_queue_hash_add);

/**
 *	inet_csk_clone_lock - clone an inet socket, and lock its clone
 *	@sk: the socket to clone
 *	@req: request_sock
 *	@priority: for allocation (%GFP_KERNEL, %GFP_ATOMIC, etc)
 *
 *	Caller must unlock socket even in error path (bh_unlock_sock(newsk))
 */
struct sock *inet_csk_clone_lock(const struct sock *sk,
				 const struct request_sock *req,
				 const gfp_t priority)
{
	struct sock *newsk = sk_clone_lock(sk, priority);

	if (newsk) {
		struct inet_connection_sock *newicsk = inet_csk(newsk);

		inet_sk_set_state(newsk, TCP_SYN_RECV);
		newicsk->icsk_bind_hash = NULL;

		inet_sk(newsk)->inet_dport = inet_rsk(req)->ir_rmt_port;
		inet_sk(newsk)->inet_num = inet_rsk(req)->ir_num;
		inet_sk(newsk)->inet_sport = htons(inet_rsk(req)->ir_num);

		/* listeners have SOCK_RCU_FREE, not the children */
		sock_reset_flag(newsk, SOCK_RCU_FREE);

		inet_sk(newsk)->mc_list = NULL;

		newsk->sk_mark = inet_rsk(req)->ir_mark;
		atomic64_set(&newsk->sk_cookie,
			     atomic64_read(&inet_rsk(req)->ir_cookie));

		newicsk->icsk_retransmits = 0;
		newicsk->icsk_backoff	  = 0;
		newicsk->icsk_probes_out  = 0;

		/* Deinitialize accept_queue to trap illegal accesses. */
		memset(&newicsk->icsk_accept_queue, 0, sizeof(newicsk->icsk_accept_queue));

		security_inet_csk_clone(newsk, req);
	}
	return newsk;
}
EXPORT_SYMBOL_GPL(inet_csk_clone_lock);

/*
 * At this point, there should be no process reference to this
 * socket, and thus no user references at all.  Therefore we
 * can assume the socket waitqueue is inactive and nobody will
 * try to jump onto it.
 */
void inet_csk_destroy_sock(struct sock *sk)
{
	WARN_ON(sk->sk_state != TCP_CLOSE);
	WARN_ON(!sock_flag(sk, SOCK_DEAD));

	/* It cannot be in hash table! */
	WARN_ON(!sk_unhashed(sk));

	/* If it has not 0 inet_sk(sk)->inet_num, it must be bound */
	WARN_ON(inet_sk(sk)->inet_num && !inet_csk(sk)->icsk_bind_hash);

	sk->sk_prot->destroy(sk);

	sk_stream_kill_queues(sk);

	xfrm_sk_free_policy(sk);

	sk_refcnt_debug_release(sk);

	percpu_counter_dec(sk->sk_prot->orphan_count);

	sock_put(sk);
}
EXPORT_SYMBOL(inet_csk_destroy_sock);

/* This function allows to force a closure of a socket after the call to
 * tcp/dccp_create_openreq_child().
 */
void inet_csk_prepare_forced_close(struct sock *sk)
	__releases(&sk->sk_lock.slock)
{
	/* sk_clone_lock locked the socket and set refcnt to 2 */
	bh_unlock_sock(sk);
	sock_put(sk);

	/* The below has to be done to allow calling inet_csk_destroy_sock */
	sock_set_flag(sk, SOCK_DEAD);
	percpu_counter_inc(sk->sk_prot->orphan_count);
	inet_sk(sk)->inet_num = 0;
}
EXPORT_SYMBOL(inet_csk_prepare_forced_close);

int inet_csk_listen_start(struct sock *sk, int backlog)
{
	struct inet_connection_sock *icsk = inet_csk(sk);
	struct inet_sock *inet = inet_sk(sk);
	int err = -EADDRINUSE;

	reqsk_queue_alloc(&icsk->icsk_accept_queue);

	sk->sk_ack_backlog = 0;
	inet_csk_delack_init(sk);

	/* There is race window here: we announce ourselves listening,
	 * but this transition is still not validated by get_port().
	 * It is OK, because this socket enters to hash table only
	 * after validation is complete.
	 */
	inet_sk_state_store(sk, TCP_LISTEN);
	if (!sk->sk_prot->get_port(sk, inet->inet_num)) {
		inet->inet_sport = htons(inet->inet_num);

		sk_dst_reset(sk);
		err = sk->sk_prot->hash(sk);

		if (likely(!err))
			return 0;
	}

	inet_sk_set_state(sk, TCP_CLOSE);
	return err;
}
EXPORT_SYMBOL_GPL(inet_csk_listen_start);

static void inet_child_forget(struct sock *sk, struct request_sock *req,
			      struct sock *child)
{
	sk->sk_prot->disconnect(child, O_NONBLOCK);

	sock_orphan(child);

	percpu_counter_inc(sk->sk_prot->orphan_count);

	if (sk->sk_protocol == IPPROTO_TCP && tcp_rsk(req)->tfo_listener) {
		BUG_ON(tcp_sk(child)->fastopen_rsk != req);
		BUG_ON(sk != req->rsk_listener);

		/* Paranoid, to prevent race condition if
		 * an inbound pkt destined for child is
		 * blocked by sock lock in tcp_v4_rcv().
		 * Also to satisfy an assertion in
		 * tcp_v4_destroy_sock().
		 */
		tcp_sk(child)->fastopen_rsk = NULL;
	}
	inet_csk_destroy_sock(child);
}

struct sock *inet_csk_reqsk_queue_add(struct sock *sk,
				      struct request_sock *req,
				      struct sock *child)
{
	struct request_sock_queue *queue = &inet_csk(sk)->icsk_accept_queue;

	spin_lock(&queue->rskq_lock);
	if (unlikely(sk->sk_state != TCP_LISTEN)) {
		inet_child_forget(sk, req, child);
		child = NULL;
	} else {
		req->sk = child;
		req->dl_next = NULL;
		if (queue->rskq_accept_head == NULL)
			queue->rskq_accept_head = req;
		else
			queue->rskq_accept_tail->dl_next = req;
		queue->rskq_accept_tail = req;
		sk_acceptq_added(sk);
	}
	spin_unlock(&queue->rskq_lock);
	return child;
}
EXPORT_SYMBOL(inet_csk_reqsk_queue_add);

struct sock *inet_csk_complete_hashdance(struct sock *sk, struct sock *child,
					 struct request_sock *req, bool own_req)
{
	if (own_req) {
		inet_csk_reqsk_queue_drop(sk, req);
		reqsk_queue_removed(&inet_csk(sk)->icsk_accept_queue, req);
		if (inet_csk_reqsk_queue_add(sk, req, child))
			return child;
	}
	/* Too bad, another child took ownership of the request, undo. */
	bh_unlock_sock(child);
	sock_put(child);
	return NULL;
}
EXPORT_SYMBOL(inet_csk_complete_hashdance);

/*
 *	This routine closes sockets which have been at least partially
 *	opened, but not yet accepted.
 */
void inet_csk_listen_stop(struct sock *sk)
{
	struct inet_connection_sock *icsk = inet_csk(sk);
	struct request_sock_queue *queue = &icsk->icsk_accept_queue;
	struct request_sock *next, *req;

	/* Following specs, it would be better either to send FIN
	 * (and enter FIN-WAIT-1, it is normal close)
	 * or to send active reset (abort).
	 * Certainly, it is pretty dangerous while synflood, but it is
	 * bad justification for our negligence 8)
	 * To be honest, we are not able to make either
	 * of the variants now.			--ANK
	 */
	while ((req = reqsk_queue_remove(queue, sk)) != NULL) {
		struct sock *child = req->sk;

		local_bh_disable();
		bh_lock_sock(child);
		WARN_ON(sock_owned_by_user(child));
		sock_hold(child);

		inet_child_forget(sk, req, child);
		reqsk_put(req);
		bh_unlock_sock(child);
		local_bh_enable();
		sock_put(child);

		cond_resched();
	}
	if (queue->fastopenq.rskq_rst_head) {
		/* Free all the reqs queued in rskq_rst_head. */
		spin_lock_bh(&queue->fastopenq.lock);
		req = queue->fastopenq.rskq_rst_head;
		queue->fastopenq.rskq_rst_head = NULL;
		spin_unlock_bh(&queue->fastopenq.lock);
		while (req != NULL) {
			next = req->dl_next;
			reqsk_put(req);
			req = next;
		}
	}
	WARN_ON_ONCE(sk->sk_ack_backlog);
}
EXPORT_SYMBOL_GPL(inet_csk_listen_stop);

void inet_csk_addr2sockaddr(struct sock *sk, struct sockaddr *uaddr)
{
	struct sockaddr_in *sin = (struct sockaddr_in *)uaddr;
	const struct inet_sock *inet = inet_sk(sk);

	sin->sin_family		= AF_INET;
	sin->sin_addr.s_addr	= inet->inet_daddr;
	sin->sin_port		= inet->inet_dport;
}
EXPORT_SYMBOL_GPL(inet_csk_addr2sockaddr);

#ifdef CONFIG_COMPAT
int inet_csk_compat_getsockopt(struct sock *sk, int level, int optname,
			       char __user *optval, int __user *optlen)
{
	const struct inet_connection_sock *icsk = inet_csk(sk);

	if (icsk->icsk_af_ops->compat_getsockopt)
		return icsk->icsk_af_ops->compat_getsockopt(sk, level, optname,
							    optval, optlen);
	return icsk->icsk_af_ops->getsockopt(sk, level, optname,
					     optval, optlen);
}
EXPORT_SYMBOL_GPL(inet_csk_compat_getsockopt);

int inet_csk_compat_setsockopt(struct sock *sk, int level, int optname,
			       char __user *optval, unsigned int optlen)
{
	const struct inet_connection_sock *icsk = inet_csk(sk);

	if (icsk->icsk_af_ops->compat_setsockopt)
		return icsk->icsk_af_ops->compat_setsockopt(sk, level, optname,
							    optval, optlen);
	return icsk->icsk_af_ops->setsockopt(sk, level, optname,
					     optval, optlen);
}
EXPORT_SYMBOL_GPL(inet_csk_compat_setsockopt);
#endif

static struct dst_entry *inet_csk_rebuild_route(struct sock *sk, struct flowi *fl)
{
	const struct inet_sock *inet = inet_sk(sk);
	const struct ip_options_rcu *inet_opt;
	__be32 daddr = inet->inet_daddr;
	struct flowi4 *fl4;
	struct rtable *rt;

	rcu_read_lock();
	inet_opt = rcu_dereference(inet->inet_opt);
	if (inet_opt && inet_opt->opt.srr)
		daddr = inet_opt->opt.faddr;
	fl4 = &fl->u.ip4;
	rt = ip_route_output_ports(sock_net(sk), fl4, sk, daddr,
				   inet->inet_saddr, inet->inet_dport,
				   inet->inet_sport, sk->sk_protocol,
				   RT_CONN_FLAGS(sk), sk->sk_bound_dev_if);
	if (IS_ERR(rt))
		rt = NULL;
	if (rt)
		sk_setup_caps(sk, &rt->dst);
	rcu_read_unlock();

	return &rt->dst;
}

struct dst_entry *inet_csk_update_pmtu(struct sock *sk, u32 mtu)
{
	struct dst_entry *dst = __sk_dst_check(sk, 0);
	struct inet_sock *inet = inet_sk(sk);

	if (!dst) {
		dst = inet_csk_rebuild_route(sk, &inet->cork.fl);
		if (!dst)
			goto out;
	}
	dst->ops->update_pmtu(dst, sk, NULL, mtu);

	dst = __sk_dst_check(sk, 0);
	if (!dst)
		dst = inet_csk_rebuild_route(sk, &inet->cork.fl);
out:
	return dst;
}
EXPORT_SYMBOL_GPL(inet_csk_update_pmtu);
