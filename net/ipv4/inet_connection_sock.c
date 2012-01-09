/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Support for INET connection oriented protocols.
 *
 * Authors:	See the TCP sources
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or(at your option) any later version.
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

#ifdef INET_CSK_DEBUG
const char inet_csk_timer_bug_msg[] = "inet_csk BUG: unknown timer value\n";
EXPORT_SYMBOL(inet_csk_timer_bug_msg);
#endif

/*
 * This struct holds the first and last local port number.
 */
struct local_ports sysctl_local_ports __read_mostly = {
	.lock = __SEQLOCK_UNLOCKED(sysctl_local_ports.lock),
	.range = { 32768, 61000 },
};

unsigned long *sysctl_local_reserved_ports;
EXPORT_SYMBOL(sysctl_local_reserved_ports);

void inet_get_local_port_range(int *low, int *high)
{
	unsigned seq;
	do {
		seq = read_seqbegin(&sysctl_local_ports.lock);

		*low = sysctl_local_ports.range[0];
		*high = sysctl_local_ports.range[1];
	} while (read_seqretry(&sysctl_local_ports.lock, seq));
}
EXPORT_SYMBOL(inet_get_local_port_range);

int inet_csk_bind_conflict(const struct sock *sk,
			   const struct inet_bind_bucket *tb)
{
	struct sock *sk2;
	struct hlist_node *node;
	int reuse = sk->sk_reuse;

	/*
	 * Unlike other sk lookup places we do not check
	 * for sk_net here, since _all_ the socks listed
	 * in tb->owners list belong to the same net - the
	 * one this bucket belongs to.
	 */

	sk_for_each_bound(sk2, node, &tb->owners) {
		if (sk != sk2 &&
		    !inet_v6_ipv6only(sk2) &&
		    (!sk->sk_bound_dev_if ||
		     !sk2->sk_bound_dev_if ||
		     sk->sk_bound_dev_if == sk2->sk_bound_dev_if)) {
			if (!reuse || !sk2->sk_reuse ||
			    sk2->sk_state == TCP_LISTEN) {
				const __be32 sk2_rcv_saddr = sk_rcv_saddr(sk2);
				if (!sk2_rcv_saddr || !sk_rcv_saddr(sk) ||
				    sk2_rcv_saddr == sk_rcv_saddr(sk))
					break;
			}
		}
	}
	return node != NULL;
}
EXPORT_SYMBOL_GPL(inet_csk_bind_conflict);

/* Obtain a reference to a local port for the given sock,
 * if snum is zero it means select any available local port.
 */
int inet_csk_get_port(struct sock *sk, unsigned short snum)
{
	struct inet_hashinfo *hashinfo = sk->sk_prot->h.hashinfo;
	struct inet_bind_hashbucket *head;
	struct hlist_node *node;
	struct inet_bind_bucket *tb;
	int ret, attempts = 5;
	struct net *net = sock_net(sk);
	int smallest_size = -1, smallest_rover;

	local_bh_disable();
	if (!snum) {
		int remaining, rover, low, high;

again:
		inet_get_local_port_range(&low, &high);
		remaining = (high - low) + 1;
		smallest_rover = rover = net_random() % remaining + low;

		smallest_size = -1;
		do {
			if (inet_is_reserved_local_port(rover))
				goto next_nolock;
			head = &hashinfo->bhash[inet_bhashfn(net, rover,
					hashinfo->bhash_size)];
			spin_lock(&head->lock);
			inet_bind_bucket_for_each(tb, node, &head->chain)
				if (net_eq(ib_net(tb), net) && tb->port == rover) {
					if (tb->fastreuse > 0 &&
					    sk->sk_reuse &&
					    sk->sk_state != TCP_LISTEN &&
					    (tb->num_owners < smallest_size || smallest_size == -1)) {
						smallest_size = tb->num_owners;
						smallest_rover = rover;
						if (atomic_read(&hashinfo->bsockets) > (high - low) + 1) {
							spin_unlock(&head->lock);
							snum = smallest_rover;
							goto have_snum;
						}
					}
					goto next;
				}
			break;
		next:
			spin_unlock(&head->lock);
		next_nolock:
			if (++rover > high)
				rover = low;
		} while (--remaining > 0);

		/* Exhausted local port range during search?  It is not
		 * possible for us to be holding one of the bind hash
		 * locks if this test triggers, because if 'remaining'
		 * drops to zero, we broke out of the do/while loop at
		 * the top level, not from the 'break;' statement.
		 */
		ret = 1;
		if (remaining <= 0) {
			if (smallest_size != -1) {
				snum = smallest_rover;
				goto have_snum;
			}
			goto fail;
		}
		/* OK, here is the one we will use.  HEAD is
		 * non-NULL and we hold it's mutex.
		 */
		snum = rover;
	} else {
have_snum:
		head = &hashinfo->bhash[inet_bhashfn(net, snum,
				hashinfo->bhash_size)];
		spin_lock(&head->lock);
		inet_bind_bucket_for_each(tb, node, &head->chain)
			if (net_eq(ib_net(tb), net) && tb->port == snum)
				goto tb_found;
	}
	tb = NULL;
	goto tb_not_found;
tb_found:
	if (!hlist_empty(&tb->owners)) {
		if (tb->fastreuse > 0 &&
		    sk->sk_reuse && sk->sk_state != TCP_LISTEN &&
		    smallest_size == -1) {
			goto success;
		} else {
			ret = 1;
			if (inet_csk(sk)->icsk_af_ops->bind_conflict(sk, tb)) {
				if (sk->sk_reuse && sk->sk_state != TCP_LISTEN &&
				    smallest_size != -1 && --attempts >= 0) {
					spin_unlock(&head->lock);
					goto again;
				}
				goto fail_unlock;
			}
		}
	}
tb_not_found:
	ret = 1;
	if (!tb && (tb = inet_bind_bucket_create(hashinfo->bind_bucket_cachep,
					net, head, snum)) == NULL)
		goto fail_unlock;
	if (hlist_empty(&tb->owners)) {
		if (sk->sk_reuse && sk->sk_state != TCP_LISTEN)
			tb->fastreuse = 1;
		else
			tb->fastreuse = 0;
	} else if (tb->fastreuse &&
		   (!sk->sk_reuse || sk->sk_state == TCP_LISTEN))
		tb->fastreuse = 0;
success:
	if (!inet_csk(sk)->icsk_bind_hash)
		inet_bind_hash(sk, tb, snum);
	WARN_ON(inet_csk(sk)->icsk_bind_hash != tb);
	ret = 0;

fail_unlock:
	spin_unlock(&head->lock);
fail:
	local_bh_enable();
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
struct sock *inet_csk_accept(struct sock *sk, int flags, int *err)
{
	struct inet_connection_sock *icsk = inet_csk(sk);
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
	if (reqsk_queue_empty(&icsk->icsk_accept_queue)) {
		long timeo = sock_rcvtimeo(sk, flags & O_NONBLOCK);

		/* If this is a non blocking socket don't sleep */
		error = -EAGAIN;
		if (!timeo)
			goto out_err;

		error = inet_csk_wait_for_connect(sk, timeo);
		if (error)
			goto out_err;
	}

	newsk = reqsk_queue_get_child(&icsk->icsk_accept_queue, sk);
	WARN_ON(newsk->sk_state == TCP_SYN_RECV);
out:
	release_sock(sk);
	return newsk;
out_err:
	newsk = NULL;
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
			       void (*retransmit_handler)(unsigned long),
			       void (*delack_handler)(unsigned long),
			       void (*keepalive_handler)(unsigned long))
{
	struct inet_connection_sock *icsk = inet_csk(sk);

	setup_timer(&icsk->icsk_retransmit_timer, retransmit_handler,
			(unsigned long)sk);
	setup_timer(&icsk->icsk_delack_timer, delack_handler,
			(unsigned long)sk);
	setup_timer(&sk->sk_timer, keepalive_handler, (unsigned long)sk);
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

struct dst_entry *inet_csk_route_req(struct sock *sk,
				     struct flowi4 *fl4,
				     const struct request_sock *req)
{
	struct rtable *rt;
	const struct inet_request_sock *ireq = inet_rsk(req);
	struct ip_options_rcu *opt = inet_rsk(req)->opt;
	struct net *net = sock_net(sk);

	flowi4_init_output(fl4, sk->sk_bound_dev_if, sk->sk_mark,
			   RT_CONN_FLAGS(sk), RT_SCOPE_UNIVERSE,
			   sk->sk_protocol, inet_sk_flowi_flags(sk),
			   (opt && opt->opt.srr) ? opt->opt.faddr : ireq->rmt_addr,
			   ireq->loc_addr, ireq->rmt_port, inet_sk(sk)->inet_sport);
	security_req_classify_flow(req, flowi4_to_flowi(fl4));
	rt = ip_route_output_flow(net, fl4, sk);
	if (IS_ERR(rt))
		goto no_route;
	if (opt && opt->opt.is_strictroute && fl4->daddr != rt->rt_gateway)
		goto route_err;
	return &rt->dst;

route_err:
	ip_rt_put(rt);
no_route:
	IP_INC_STATS_BH(net, IPSTATS_MIB_OUTNOROUTES);
	return NULL;
}
EXPORT_SYMBOL_GPL(inet_csk_route_req);

struct dst_entry *inet_csk_route_child_sock(struct sock *sk,
					    struct sock *newsk,
					    const struct request_sock *req)
{
	const struct inet_request_sock *ireq = inet_rsk(req);
	struct inet_sock *newinet = inet_sk(newsk);
	struct ip_options_rcu *opt = ireq->opt;
	struct net *net = sock_net(sk);
	struct flowi4 *fl4;
	struct rtable *rt;

	fl4 = &newinet->cork.fl.u.ip4;
	flowi4_init_output(fl4, sk->sk_bound_dev_if, sk->sk_mark,
			   RT_CONN_FLAGS(sk), RT_SCOPE_UNIVERSE,
			   sk->sk_protocol, inet_sk_flowi_flags(sk),
			   (opt && opt->opt.srr) ? opt->opt.faddr : ireq->rmt_addr,
			   ireq->loc_addr, ireq->rmt_port, inet_sk(sk)->inet_sport);
	security_req_classify_flow(req, flowi4_to_flowi(fl4));
	rt = ip_route_output_flow(net, fl4, sk);
	if (IS_ERR(rt))
		goto no_route;
	if (opt && opt->opt.is_strictroute && fl4->daddr != rt->rt_gateway)
		goto route_err;
	return &rt->dst;

route_err:
	ip_rt_put(rt);
no_route:
	IP_INC_STATS_BH(net, IPSTATS_MIB_OUTNOROUTES);
	return NULL;
}
EXPORT_SYMBOL_GPL(inet_csk_route_child_sock);

static inline u32 inet_synq_hash(const __be32 raddr, const __be16 rport,
				 const u32 rnd, const u32 synq_hsize)
{
	return jhash_2words((__force u32)raddr, (__force u32)rport, rnd) & (synq_hsize - 1);
}

#if IS_ENABLED(CONFIG_IPV6)
#define AF_INET_FAMILY(fam) ((fam) == AF_INET)
#else
#define AF_INET_FAMILY(fam) 1
#endif

struct request_sock *inet_csk_search_req(const struct sock *sk,
					 struct request_sock ***prevp,
					 const __be16 rport, const __be32 raddr,
					 const __be32 laddr)
{
	const struct inet_connection_sock *icsk = inet_csk(sk);
	struct listen_sock *lopt = icsk->icsk_accept_queue.listen_opt;
	struct request_sock *req, **prev;

	for (prev = &lopt->syn_table[inet_synq_hash(raddr, rport, lopt->hash_rnd,
						    lopt->nr_table_entries)];
	     (req = *prev) != NULL;
	     prev = &req->dl_next) {
		const struct inet_request_sock *ireq = inet_rsk(req);

		if (ireq->rmt_port == rport &&
		    ireq->rmt_addr == raddr &&
		    ireq->loc_addr == laddr &&
		    AF_INET_FAMILY(req->rsk_ops->family)) {
			WARN_ON(req->sk);
			*prevp = prev;
			break;
		}
	}

	return req;
}
EXPORT_SYMBOL_GPL(inet_csk_search_req);

void inet_csk_reqsk_queue_hash_add(struct sock *sk, struct request_sock *req,
				   unsigned long timeout)
{
	struct inet_connection_sock *icsk = inet_csk(sk);
	struct listen_sock *lopt = icsk->icsk_accept_queue.listen_opt;
	const u32 h = inet_synq_hash(inet_rsk(req)->rmt_addr, inet_rsk(req)->rmt_port,
				     lopt->hash_rnd, lopt->nr_table_entries);

	reqsk_queue_hash_req(&icsk->icsk_accept_queue, h, req, timeout);
	inet_csk_reqsk_queue_added(sk, timeout);
}
EXPORT_SYMBOL_GPL(inet_csk_reqsk_queue_hash_add);

/* Only thing we need from tcp.h */
extern int sysctl_tcp_synack_retries;


/* Decide when to expire the request and when to resend SYN-ACK */
static inline void syn_ack_recalc(struct request_sock *req, const int thresh,
				  const int max_retries,
				  const u8 rskq_defer_accept,
				  int *expire, int *resend)
{
	if (!rskq_defer_accept) {
		*expire = req->retrans >= thresh;
		*resend = 1;
		return;
	}
	*expire = req->retrans >= thresh &&
		  (!inet_rsk(req)->acked || req->retrans >= max_retries);
	/*
	 * Do not resend while waiting for data after ACK,
	 * start to resend on end of deferring period to give
	 * last chance for data or ACK to create established socket.
	 */
	*resend = !inet_rsk(req)->acked ||
		  req->retrans >= rskq_defer_accept - 1;
}

void inet_csk_reqsk_queue_prune(struct sock *parent,
				const unsigned long interval,
				const unsigned long timeout,
				const unsigned long max_rto)
{
	struct inet_connection_sock *icsk = inet_csk(parent);
	struct request_sock_queue *queue = &icsk->icsk_accept_queue;
	struct listen_sock *lopt = queue->listen_opt;
	int max_retries = icsk->icsk_syn_retries ? : sysctl_tcp_synack_retries;
	int thresh = max_retries;
	unsigned long now = jiffies;
	struct request_sock **reqp, *req;
	int i, budget;

	if (lopt == NULL || lopt->qlen == 0)
		return;

	/* Normally all the openreqs are young and become mature
	 * (i.e. converted to established socket) for first timeout.
	 * If synack was not acknowledged for 3 seconds, it means
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
	if (lopt->qlen>>(lopt->max_qlen_log-1)) {
		int young = (lopt->qlen_young<<1);

		while (thresh > 2) {
			if (lopt->qlen < young)
				break;
			thresh--;
			young <<= 1;
		}
	}

	if (queue->rskq_defer_accept)
		max_retries = queue->rskq_defer_accept;

	budget = 2 * (lopt->nr_table_entries / (timeout / interval));
	i = lopt->clock_hand;

	do {
		reqp=&lopt->syn_table[i];
		while ((req = *reqp) != NULL) {
			if (time_after_eq(now, req->expires)) {
				int expire = 0, resend = 0;

				syn_ack_recalc(req, thresh, max_retries,
					       queue->rskq_defer_accept,
					       &expire, &resend);
				if (req->rsk_ops->syn_ack_timeout)
					req->rsk_ops->syn_ack_timeout(parent, req);
				if (!expire &&
				    (!resend ||
				     !req->rsk_ops->rtx_syn_ack(parent, req, NULL) ||
				     inet_rsk(req)->acked)) {
					unsigned long timeo;

					if (req->retrans++ == 0)
						lopt->qlen_young--;
					timeo = min((timeout << req->retrans), max_rto);
					req->expires = now + timeo;
					reqp = &req->dl_next;
					continue;
				}

				/* Drop this request */
				inet_csk_reqsk_queue_unlink(parent, req, reqp);
				reqsk_queue_removed(queue, req);
				reqsk_free(req);
				continue;
			}
			reqp = &req->dl_next;
		}

		i = (i + 1) & (lopt->nr_table_entries - 1);

	} while (--budget > 0);

	lopt->clock_hand = i;

	if (lopt->qlen)
		inet_csk_reset_keepalive_timer(parent, interval);
}
EXPORT_SYMBOL_GPL(inet_csk_reqsk_queue_prune);

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

	if (newsk != NULL) {
		struct inet_connection_sock *newicsk = inet_csk(newsk);

		newsk->sk_state = TCP_SYN_RECV;
		newicsk->icsk_bind_hash = NULL;

		inet_sk(newsk)->inet_dport = inet_rsk(req)->rmt_port;
		inet_sk(newsk)->inet_num = ntohs(inet_rsk(req)->loc_port);
		inet_sk(newsk)->inet_sport = inet_rsk(req)->loc_port;
		newsk->sk_write_space = sk_stream_write_space;

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

int inet_csk_listen_start(struct sock *sk, const int nr_table_entries)
{
	struct inet_sock *inet = inet_sk(sk);
	struct inet_connection_sock *icsk = inet_csk(sk);
	int rc = reqsk_queue_alloc(&icsk->icsk_accept_queue, nr_table_entries);

	if (rc != 0)
		return rc;

	sk->sk_max_ack_backlog = 0;
	sk->sk_ack_backlog = 0;
	inet_csk_delack_init(sk);

	/* There is race window here: we announce ourselves listening,
	 * but this transition is still not validated by get_port().
	 * It is OK, because this socket enters to hash table only
	 * after validation is complete.
	 */
	sk->sk_state = TCP_LISTEN;
	if (!sk->sk_prot->get_port(sk, inet->inet_num)) {
		inet->inet_sport = htons(inet->inet_num);

		sk_dst_reset(sk);
		sk->sk_prot->hash(sk);

		return 0;
	}

	sk->sk_state = TCP_CLOSE;
	__reqsk_queue_destroy(&icsk->icsk_accept_queue);
	return -EADDRINUSE;
}
EXPORT_SYMBOL_GPL(inet_csk_listen_start);

/*
 *	This routine closes sockets which have been at least partially
 *	opened, but not yet accepted.
 */
void inet_csk_listen_stop(struct sock *sk)
{
	struct inet_connection_sock *icsk = inet_csk(sk);
	struct request_sock *acc_req;
	struct request_sock *req;

	inet_csk_delete_keepalive_timer(sk);

	/* make all the listen_opt local to us */
	acc_req = reqsk_queue_yank_acceptq(&icsk->icsk_accept_queue);

	/* Following specs, it would be better either to send FIN
	 * (and enter FIN-WAIT-1, it is normal close)
	 * or to send active reset (abort).
	 * Certainly, it is pretty dangerous while synflood, but it is
	 * bad justification for our negligence 8)
	 * To be honest, we are not able to make either
	 * of the variants now.			--ANK
	 */
	reqsk_queue_destroy(&icsk->icsk_accept_queue);

	while ((req = acc_req) != NULL) {
		struct sock *child = req->sk;

		acc_req = req->dl_next;

		local_bh_disable();
		bh_lock_sock(child);
		WARN_ON(sock_owned_by_user(child));
		sock_hold(child);

		sk->sk_prot->disconnect(child, O_NONBLOCK);

		sock_orphan(child);

		percpu_counter_inc(sk->sk_prot->orphan_count);

		inet_csk_destroy_sock(child);

		bh_unlock_sock(child);
		local_bh_enable();
		sock_put(child);

		sk_acceptq_removed(sk);
		__reqsk_free(req);
	}
	WARN_ON(sk->sk_ack_backlog);
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

	if (icsk->icsk_af_ops->compat_getsockopt != NULL)
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

	if (icsk->icsk_af_ops->compat_setsockopt != NULL)
		return icsk->icsk_af_ops->compat_setsockopt(sk, level, optname,
							    optval, optlen);
	return icsk->icsk_af_ops->setsockopt(sk, level, optname,
					     optval, optlen);
}
EXPORT_SYMBOL_GPL(inet_csk_compat_setsockopt);
#endif
