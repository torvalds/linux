#include <linux/rcupdate.h>
#include <linux/spinlock.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/cache.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/tcp.h>
#include <linux/hash.h>
#include <linux/tcp_metrics.h>
#include <linux/vmalloc.h>

#include <net/inet_connection_sock.h>
#include <net/net_namespace.h>
#include <net/request_sock.h>
#include <net/inetpeer.h>
#include <net/sock.h>
#include <net/ipv6.h>
#include <net/dst.h>
#include <net/tcp.h>
#include <net/genetlink.h>

int sysctl_tcp_nometrics_save __read_mostly;

struct tcp_fastopen_metrics {
	u16	mss;
	u16	syn_loss:10;		/* Recurring Fast Open SYN losses */
	unsigned long	last_syn_loss;	/* Last Fast Open SYN loss */
	struct	tcp_fastopen_cookie	cookie;
};

struct tcp_metrics_block {
	struct tcp_metrics_block __rcu	*tcpm_next;
	struct inetpeer_addr		tcpm_addr;
	unsigned long			tcpm_stamp;
	u32				tcpm_ts;
	u32				tcpm_ts_stamp;
	u32				tcpm_lock;
	u32				tcpm_vals[TCP_METRIC_MAX + 1];
	struct tcp_fastopen_metrics	tcpm_fastopen;

	struct rcu_head			rcu_head;
};

static bool tcp_metric_locked(struct tcp_metrics_block *tm,
			      enum tcp_metric_index idx)
{
	return tm->tcpm_lock & (1 << idx);
}

static u32 tcp_metric_get(struct tcp_metrics_block *tm,
			  enum tcp_metric_index idx)
{
	return tm->tcpm_vals[idx];
}

static u32 tcp_metric_get_jiffies(struct tcp_metrics_block *tm,
				  enum tcp_metric_index idx)
{
	return msecs_to_jiffies(tm->tcpm_vals[idx]);
}

static void tcp_metric_set(struct tcp_metrics_block *tm,
			   enum tcp_metric_index idx,
			   u32 val)
{
	tm->tcpm_vals[idx] = val;
}

static void tcp_metric_set_msecs(struct tcp_metrics_block *tm,
				 enum tcp_metric_index idx,
				 u32 val)
{
	tm->tcpm_vals[idx] = jiffies_to_msecs(val);
}

static bool addr_same(const struct inetpeer_addr *a,
		      const struct inetpeer_addr *b)
{
	const struct in6_addr *a6, *b6;

	if (a->family != b->family)
		return false;
	if (a->family == AF_INET)
		return a->addr.a4 == b->addr.a4;

	a6 = (const struct in6_addr *) &a->addr.a6[0];
	b6 = (const struct in6_addr *) &b->addr.a6[0];

	return ipv6_addr_equal(a6, b6);
}

struct tcpm_hash_bucket {
	struct tcp_metrics_block __rcu	*chain;
};

static DEFINE_SPINLOCK(tcp_metrics_lock);

static void tcpm_suck_dst(struct tcp_metrics_block *tm, struct dst_entry *dst,
			  bool fastopen_clear)
{
	u32 val;

	tm->tcpm_stamp = jiffies;

	val = 0;
	if (dst_metric_locked(dst, RTAX_RTT))
		val |= 1 << TCP_METRIC_RTT;
	if (dst_metric_locked(dst, RTAX_RTTVAR))
		val |= 1 << TCP_METRIC_RTTVAR;
	if (dst_metric_locked(dst, RTAX_SSTHRESH))
		val |= 1 << TCP_METRIC_SSTHRESH;
	if (dst_metric_locked(dst, RTAX_CWND))
		val |= 1 << TCP_METRIC_CWND;
	if (dst_metric_locked(dst, RTAX_REORDERING))
		val |= 1 << TCP_METRIC_REORDERING;
	tm->tcpm_lock = val;

	tm->tcpm_vals[TCP_METRIC_RTT] = dst_metric_raw(dst, RTAX_RTT);
	tm->tcpm_vals[TCP_METRIC_RTTVAR] = dst_metric_raw(dst, RTAX_RTTVAR);
	tm->tcpm_vals[TCP_METRIC_SSTHRESH] = dst_metric_raw(dst, RTAX_SSTHRESH);
	tm->tcpm_vals[TCP_METRIC_CWND] = dst_metric_raw(dst, RTAX_CWND);
	tm->tcpm_vals[TCP_METRIC_REORDERING] = dst_metric_raw(dst, RTAX_REORDERING);
	tm->tcpm_ts = 0;
	tm->tcpm_ts_stamp = 0;
	if (fastopen_clear) {
		tm->tcpm_fastopen.mss = 0;
		tm->tcpm_fastopen.syn_loss = 0;
		tm->tcpm_fastopen.cookie.len = 0;
	}
}

static struct tcp_metrics_block *tcpm_new(struct dst_entry *dst,
					  struct inetpeer_addr *addr,
					  unsigned int hash,
					  bool reclaim)
{
	struct tcp_metrics_block *tm;
	struct net *net;

	spin_lock_bh(&tcp_metrics_lock);
	net = dev_net(dst->dev);
	if (unlikely(reclaim)) {
		struct tcp_metrics_block *oldest;

		oldest = rcu_dereference(net->ipv4.tcp_metrics_hash[hash].chain);
		for (tm = rcu_dereference(oldest->tcpm_next); tm;
		     tm = rcu_dereference(tm->tcpm_next)) {
			if (time_before(tm->tcpm_stamp, oldest->tcpm_stamp))
				oldest = tm;
		}
		tm = oldest;
	} else {
		tm = kmalloc(sizeof(*tm), GFP_ATOMIC);
		if (!tm)
			goto out_unlock;
	}
	tm->tcpm_addr = *addr;

	tcpm_suck_dst(tm, dst, true);

	if (likely(!reclaim)) {
		tm->tcpm_next = net->ipv4.tcp_metrics_hash[hash].chain;
		rcu_assign_pointer(net->ipv4.tcp_metrics_hash[hash].chain, tm);
	}

out_unlock:
	spin_unlock_bh(&tcp_metrics_lock);
	return tm;
}

#define TCP_METRICS_TIMEOUT		(60 * 60 * HZ)

static void tcpm_check_stamp(struct tcp_metrics_block *tm, struct dst_entry *dst)
{
	if (tm && unlikely(time_after(jiffies, tm->tcpm_stamp + TCP_METRICS_TIMEOUT)))
		tcpm_suck_dst(tm, dst, false);
}

#define TCP_METRICS_RECLAIM_DEPTH	5
#define TCP_METRICS_RECLAIM_PTR		(struct tcp_metrics_block *) 0x1UL

static struct tcp_metrics_block *tcp_get_encode(struct tcp_metrics_block *tm, int depth)
{
	if (tm)
		return tm;
	if (depth > TCP_METRICS_RECLAIM_DEPTH)
		return TCP_METRICS_RECLAIM_PTR;
	return NULL;
}

static struct tcp_metrics_block *__tcp_get_metrics(const struct inetpeer_addr *addr,
						   struct net *net, unsigned int hash)
{
	struct tcp_metrics_block *tm;
	int depth = 0;

	for (tm = rcu_dereference(net->ipv4.tcp_metrics_hash[hash].chain); tm;
	     tm = rcu_dereference(tm->tcpm_next)) {
		if (addr_same(&tm->tcpm_addr, addr))
			break;
		depth++;
	}
	return tcp_get_encode(tm, depth);
}

static struct tcp_metrics_block *__tcp_get_metrics_req(struct request_sock *req,
						       struct dst_entry *dst)
{
	struct tcp_metrics_block *tm;
	struct inetpeer_addr addr;
	unsigned int hash;
	struct net *net;

	addr.family = req->rsk_ops->family;
	switch (addr.family) {
	case AF_INET:
		addr.addr.a4 = inet_rsk(req)->rmt_addr;
		hash = (__force unsigned int) addr.addr.a4;
		break;
	case AF_INET6:
		*(struct in6_addr *)addr.addr.a6 = inet6_rsk(req)->rmt_addr;
		hash = ipv6_addr_hash(&inet6_rsk(req)->rmt_addr);
		break;
	default:
		return NULL;
	}

	net = dev_net(dst->dev);
	hash = hash_32(hash, net->ipv4.tcp_metrics_hash_log);

	for (tm = rcu_dereference(net->ipv4.tcp_metrics_hash[hash].chain); tm;
	     tm = rcu_dereference(tm->tcpm_next)) {
		if (addr_same(&tm->tcpm_addr, &addr))
			break;
	}
	tcpm_check_stamp(tm, dst);
	return tm;
}

static struct tcp_metrics_block *__tcp_get_metrics_tw(struct inet_timewait_sock *tw)
{
	struct inet6_timewait_sock *tw6;
	struct tcp_metrics_block *tm;
	struct inetpeer_addr addr;
	unsigned int hash;
	struct net *net;

	addr.family = tw->tw_family;
	switch (addr.family) {
	case AF_INET:
		addr.addr.a4 = tw->tw_daddr;
		hash = (__force unsigned int) addr.addr.a4;
		break;
	case AF_INET6:
		tw6 = inet6_twsk((struct sock *)tw);
		*(struct in6_addr *)addr.addr.a6 = tw6->tw_v6_daddr;
		hash = ipv6_addr_hash(&tw6->tw_v6_daddr);
		break;
	default:
		return NULL;
	}

	net = twsk_net(tw);
	hash = hash_32(hash, net->ipv4.tcp_metrics_hash_log);

	for (tm = rcu_dereference(net->ipv4.tcp_metrics_hash[hash].chain); tm;
	     tm = rcu_dereference(tm->tcpm_next)) {
		if (addr_same(&tm->tcpm_addr, &addr))
			break;
	}
	return tm;
}

static struct tcp_metrics_block *tcp_get_metrics(struct sock *sk,
						 struct dst_entry *dst,
						 bool create)
{
	struct tcp_metrics_block *tm;
	struct inetpeer_addr addr;
	unsigned int hash;
	struct net *net;
	bool reclaim;

	addr.family = sk->sk_family;
	switch (addr.family) {
	case AF_INET:
		addr.addr.a4 = inet_sk(sk)->inet_daddr;
		hash = (__force unsigned int) addr.addr.a4;
		break;
	case AF_INET6:
		*(struct in6_addr *)addr.addr.a6 = inet6_sk(sk)->daddr;
		hash = ipv6_addr_hash(&inet6_sk(sk)->daddr);
		break;
	default:
		return NULL;
	}

	net = dev_net(dst->dev);
	hash = hash_32(hash, net->ipv4.tcp_metrics_hash_log);

	tm = __tcp_get_metrics(&addr, net, hash);
	reclaim = false;
	if (tm == TCP_METRICS_RECLAIM_PTR) {
		reclaim = true;
		tm = NULL;
	}
	if (!tm && create)
		tm = tcpm_new(dst, &addr, hash, reclaim);
	else
		tcpm_check_stamp(tm, dst);

	return tm;
}

/* Save metrics learned by this TCP session.  This function is called
 * only, when TCP finishes successfully i.e. when it enters TIME-WAIT
 * or goes from LAST-ACK to CLOSE.
 */
void tcp_update_metrics(struct sock *sk)
{
	const struct inet_connection_sock *icsk = inet_csk(sk);
	struct dst_entry *dst = __sk_dst_get(sk);
	struct tcp_sock *tp = tcp_sk(sk);
	struct tcp_metrics_block *tm;
	unsigned long rtt;
	u32 val;
	int m;

	if (sysctl_tcp_nometrics_save || !dst)
		return;

	if (dst->flags & DST_HOST)
		dst_confirm(dst);

	rcu_read_lock();
	if (icsk->icsk_backoff || !tp->srtt) {
		/* This session failed to estimate rtt. Why?
		 * Probably, no packets returned in time.  Reset our
		 * results.
		 */
		tm = tcp_get_metrics(sk, dst, false);
		if (tm && !tcp_metric_locked(tm, TCP_METRIC_RTT))
			tcp_metric_set(tm, TCP_METRIC_RTT, 0);
		goto out_unlock;
	} else
		tm = tcp_get_metrics(sk, dst, true);

	if (!tm)
		goto out_unlock;

	rtt = tcp_metric_get_jiffies(tm, TCP_METRIC_RTT);
	m = rtt - tp->srtt;

	/* If newly calculated rtt larger than stored one, store new
	 * one. Otherwise, use EWMA. Remember, rtt overestimation is
	 * always better than underestimation.
	 */
	if (!tcp_metric_locked(tm, TCP_METRIC_RTT)) {
		if (m <= 0)
			rtt = tp->srtt;
		else
			rtt -= (m >> 3);
		tcp_metric_set_msecs(tm, TCP_METRIC_RTT, rtt);
	}

	if (!tcp_metric_locked(tm, TCP_METRIC_RTTVAR)) {
		unsigned long var;

		if (m < 0)
			m = -m;

		/* Scale deviation to rttvar fixed point */
		m >>= 1;
		if (m < tp->mdev)
			m = tp->mdev;

		var = tcp_metric_get_jiffies(tm, TCP_METRIC_RTTVAR);
		if (m >= var)
			var = m;
		else
			var -= (var - m) >> 2;

		tcp_metric_set_msecs(tm, TCP_METRIC_RTTVAR, var);
	}

	if (tcp_in_initial_slowstart(tp)) {
		/* Slow start still did not finish. */
		if (!tcp_metric_locked(tm, TCP_METRIC_SSTHRESH)) {
			val = tcp_metric_get(tm, TCP_METRIC_SSTHRESH);
			if (val && (tp->snd_cwnd >> 1) > val)
				tcp_metric_set(tm, TCP_METRIC_SSTHRESH,
					       tp->snd_cwnd >> 1);
		}
		if (!tcp_metric_locked(tm, TCP_METRIC_CWND)) {
			val = tcp_metric_get(tm, TCP_METRIC_CWND);
			if (tp->snd_cwnd > val)
				tcp_metric_set(tm, TCP_METRIC_CWND,
					       tp->snd_cwnd);
		}
	} else if (tp->snd_cwnd > tp->snd_ssthresh &&
		   icsk->icsk_ca_state == TCP_CA_Open) {
		/* Cong. avoidance phase, cwnd is reliable. */
		if (!tcp_metric_locked(tm, TCP_METRIC_SSTHRESH))
			tcp_metric_set(tm, TCP_METRIC_SSTHRESH,
				       max(tp->snd_cwnd >> 1, tp->snd_ssthresh));
		if (!tcp_metric_locked(tm, TCP_METRIC_CWND)) {
			val = tcp_metric_get(tm, TCP_METRIC_CWND);
			tcp_metric_set(tm, TCP_METRIC_CWND, (val + tp->snd_cwnd) >> 1);
		}
	} else {
		/* Else slow start did not finish, cwnd is non-sense,
		 * ssthresh may be also invalid.
		 */
		if (!tcp_metric_locked(tm, TCP_METRIC_CWND)) {
			val = tcp_metric_get(tm, TCP_METRIC_CWND);
			tcp_metric_set(tm, TCP_METRIC_CWND,
				       (val + tp->snd_ssthresh) >> 1);
		}
		if (!tcp_metric_locked(tm, TCP_METRIC_SSTHRESH)) {
			val = tcp_metric_get(tm, TCP_METRIC_SSTHRESH);
			if (val && tp->snd_ssthresh > val)
				tcp_metric_set(tm, TCP_METRIC_SSTHRESH,
					       tp->snd_ssthresh);
		}
		if (!tcp_metric_locked(tm, TCP_METRIC_REORDERING)) {
			val = tcp_metric_get(tm, TCP_METRIC_REORDERING);
			if (val < tp->reordering &&
			    tp->reordering != sysctl_tcp_reordering)
				tcp_metric_set(tm, TCP_METRIC_REORDERING,
					       tp->reordering);
		}
	}
	tm->tcpm_stamp = jiffies;
out_unlock:
	rcu_read_unlock();
}

/* Initialize metrics on socket. */

void tcp_init_metrics(struct sock *sk)
{
	struct dst_entry *dst = __sk_dst_get(sk);
	struct tcp_sock *tp = tcp_sk(sk);
	struct tcp_metrics_block *tm;
	u32 val, crtt = 0; /* cached RTT scaled by 8 */

	if (dst == NULL)
		goto reset;

	dst_confirm(dst);

	rcu_read_lock();
	tm = tcp_get_metrics(sk, dst, true);
	if (!tm) {
		rcu_read_unlock();
		goto reset;
	}

	if (tcp_metric_locked(tm, TCP_METRIC_CWND))
		tp->snd_cwnd_clamp = tcp_metric_get(tm, TCP_METRIC_CWND);

	val = tcp_metric_get(tm, TCP_METRIC_SSTHRESH);
	if (val) {
		tp->snd_ssthresh = val;
		if (tp->snd_ssthresh > tp->snd_cwnd_clamp)
			tp->snd_ssthresh = tp->snd_cwnd_clamp;
	} else {
		/* ssthresh may have been reduced unnecessarily during.
		 * 3WHS. Restore it back to its initial default.
		 */
		tp->snd_ssthresh = TCP_INFINITE_SSTHRESH;
	}
	val = tcp_metric_get(tm, TCP_METRIC_REORDERING);
	if (val && tp->reordering != val) {
		tcp_disable_fack(tp);
		tcp_disable_early_retrans(tp);
		tp->reordering = val;
	}

	crtt = tcp_metric_get_jiffies(tm, TCP_METRIC_RTT);
	rcu_read_unlock();
reset:
	/* The initial RTT measurement from the SYN/SYN-ACK is not ideal
	 * to seed the RTO for later data packets because SYN packets are
	 * small. Use the per-dst cached values to seed the RTO but keep
	 * the RTT estimator variables intact (e.g., srtt, mdev, rttvar).
	 * Later the RTO will be updated immediately upon obtaining the first
	 * data RTT sample (tcp_rtt_estimator()). Hence the cached RTT only
	 * influences the first RTO but not later RTT estimation.
	 *
	 * But if RTT is not available from the SYN (due to retransmits or
	 * syn cookies) or the cache, force a conservative 3secs timeout.
	 *
	 * A bit of theory. RTT is time passed after "normal" sized packet
	 * is sent until it is ACKed. In normal circumstances sending small
	 * packets force peer to delay ACKs and calculation is correct too.
	 * The algorithm is adaptive and, provided we follow specs, it
	 * NEVER underestimate RTT. BUT! If peer tries to make some clever
	 * tricks sort of "quick acks" for time long enough to decrease RTT
	 * to low value, and then abruptly stops to do it and starts to delay
	 * ACKs, wait for troubles.
	 */
	if (crtt > tp->srtt) {
		/* Set RTO like tcp_rtt_estimator(), but from cached RTT. */
		crtt >>= 3;
		inet_csk(sk)->icsk_rto = crtt + max(2 * crtt, tcp_rto_min(sk));
	} else if (tp->srtt == 0) {
		/* RFC6298: 5.7 We've failed to get a valid RTT sample from
		 * 3WHS. This is most likely due to retransmission,
		 * including spurious one. Reset the RTO back to 3secs
		 * from the more aggressive 1sec to avoid more spurious
		 * retransmission.
		 */
		tp->mdev = tp->mdev_max = tp->rttvar = TCP_TIMEOUT_FALLBACK;
		inet_csk(sk)->icsk_rto = TCP_TIMEOUT_FALLBACK;
	}
	/* Cut cwnd down to 1 per RFC5681 if SYN or SYN-ACK has been
	 * retransmitted. In light of RFC6298 more aggressive 1sec
	 * initRTO, we only reset cwnd when more than 1 SYN/SYN-ACK
	 * retransmission has occurred.
	 */
	if (tp->total_retrans > 1)
		tp->snd_cwnd = 1;
	else
		tp->snd_cwnd = tcp_init_cwnd(tp, dst);
	tp->snd_cwnd_stamp = tcp_time_stamp;
}

bool tcp_peer_is_proven(struct request_sock *req, struct dst_entry *dst, bool paws_check)
{
	struct tcp_metrics_block *tm;
	bool ret;

	if (!dst)
		return false;

	rcu_read_lock();
	tm = __tcp_get_metrics_req(req, dst);
	if (paws_check) {
		if (tm &&
		    (u32)get_seconds() - tm->tcpm_ts_stamp < TCP_PAWS_MSL &&
		    (s32)(tm->tcpm_ts - req->ts_recent) > TCP_PAWS_WINDOW)
			ret = false;
		else
			ret = true;
	} else {
		if (tm && tcp_metric_get(tm, TCP_METRIC_RTT) && tm->tcpm_ts_stamp)
			ret = true;
		else
			ret = false;
	}
	rcu_read_unlock();

	return ret;
}
EXPORT_SYMBOL_GPL(tcp_peer_is_proven);

void tcp_fetch_timewait_stamp(struct sock *sk, struct dst_entry *dst)
{
	struct tcp_metrics_block *tm;

	rcu_read_lock();
	tm = tcp_get_metrics(sk, dst, true);
	if (tm) {
		struct tcp_sock *tp = tcp_sk(sk);

		if ((u32)get_seconds() - tm->tcpm_ts_stamp <= TCP_PAWS_MSL) {
			tp->rx_opt.ts_recent_stamp = tm->tcpm_ts_stamp;
			tp->rx_opt.ts_recent = tm->tcpm_ts;
		}
	}
	rcu_read_unlock();
}
EXPORT_SYMBOL_GPL(tcp_fetch_timewait_stamp);

/* VJ's idea. Save last timestamp seen from this destination and hold
 * it at least for normal timewait interval to use for duplicate
 * segment detection in subsequent connections, before they enter
 * synchronized state.
 */
bool tcp_remember_stamp(struct sock *sk)
{
	struct dst_entry *dst = __sk_dst_get(sk);
	bool ret = false;

	if (dst) {
		struct tcp_metrics_block *tm;

		rcu_read_lock();
		tm = tcp_get_metrics(sk, dst, true);
		if (tm) {
			struct tcp_sock *tp = tcp_sk(sk);

			if ((s32)(tm->tcpm_ts - tp->rx_opt.ts_recent) <= 0 ||
			    ((u32)get_seconds() - tm->tcpm_ts_stamp > TCP_PAWS_MSL &&
			     tm->tcpm_ts_stamp <= (u32)tp->rx_opt.ts_recent_stamp)) {
				tm->tcpm_ts_stamp = (u32)tp->rx_opt.ts_recent_stamp;
				tm->tcpm_ts = tp->rx_opt.ts_recent;
			}
			ret = true;
		}
		rcu_read_unlock();
	}
	return ret;
}

bool tcp_tw_remember_stamp(struct inet_timewait_sock *tw)
{
	struct tcp_metrics_block *tm;
	bool ret = false;

	rcu_read_lock();
	tm = __tcp_get_metrics_tw(tw);
	if (tm) {
		const struct tcp_timewait_sock *tcptw;
		struct sock *sk = (struct sock *) tw;

		tcptw = tcp_twsk(sk);
		if ((s32)(tm->tcpm_ts - tcptw->tw_ts_recent) <= 0 ||
		    ((u32)get_seconds() - tm->tcpm_ts_stamp > TCP_PAWS_MSL &&
		     tm->tcpm_ts_stamp <= (u32)tcptw->tw_ts_recent_stamp)) {
			tm->tcpm_ts_stamp = (u32)tcptw->tw_ts_recent_stamp;
			tm->tcpm_ts	   = tcptw->tw_ts_recent;
		}
		ret = true;
	}
	rcu_read_unlock();

	return ret;
}

static DEFINE_SEQLOCK(fastopen_seqlock);

void tcp_fastopen_cache_get(struct sock *sk, u16 *mss,
			    struct tcp_fastopen_cookie *cookie,
			    int *syn_loss, unsigned long *last_syn_loss)
{
	struct tcp_metrics_block *tm;

	rcu_read_lock();
	tm = tcp_get_metrics(sk, __sk_dst_get(sk), false);
	if (tm) {
		struct tcp_fastopen_metrics *tfom = &tm->tcpm_fastopen;
		unsigned int seq;

		do {
			seq = read_seqbegin(&fastopen_seqlock);
			if (tfom->mss)
				*mss = tfom->mss;
			*cookie = tfom->cookie;
			*syn_loss = tfom->syn_loss;
			*last_syn_loss = *syn_loss ? tfom->last_syn_loss : 0;
		} while (read_seqretry(&fastopen_seqlock, seq));
	}
	rcu_read_unlock();
}

void tcp_fastopen_cache_set(struct sock *sk, u16 mss,
			    struct tcp_fastopen_cookie *cookie, bool syn_lost)
{
	struct tcp_metrics_block *tm;

	rcu_read_lock();
	tm = tcp_get_metrics(sk, __sk_dst_get(sk), true);
	if (tm) {
		struct tcp_fastopen_metrics *tfom = &tm->tcpm_fastopen;

		write_seqlock_bh(&fastopen_seqlock);
		tfom->mss = mss;
		if (cookie->len > 0)
			tfom->cookie = *cookie;
		if (syn_lost) {
			++tfom->syn_loss;
			tfom->last_syn_loss = jiffies;
		} else
			tfom->syn_loss = 0;
		write_sequnlock_bh(&fastopen_seqlock);
	}
	rcu_read_unlock();
}

static struct genl_family tcp_metrics_nl_family = {
	.id		= GENL_ID_GENERATE,
	.hdrsize	= 0,
	.name		= TCP_METRICS_GENL_NAME,
	.version	= TCP_METRICS_GENL_VERSION,
	.maxattr	= TCP_METRICS_ATTR_MAX,
	.netnsok	= true,
};

static struct nla_policy tcp_metrics_nl_policy[TCP_METRICS_ATTR_MAX + 1] = {
	[TCP_METRICS_ATTR_ADDR_IPV4]	= { .type = NLA_U32, },
	[TCP_METRICS_ATTR_ADDR_IPV6]	= { .type = NLA_BINARY,
					    .len = sizeof(struct in6_addr), },
	/* Following attributes are not received for GET/DEL,
	 * we keep them for reference
	 */
#if 0
	[TCP_METRICS_ATTR_AGE]		= { .type = NLA_MSECS, },
	[TCP_METRICS_ATTR_TW_TSVAL]	= { .type = NLA_U32, },
	[TCP_METRICS_ATTR_TW_TS_STAMP]	= { .type = NLA_S32, },
	[TCP_METRICS_ATTR_VALS]		= { .type = NLA_NESTED, },
	[TCP_METRICS_ATTR_FOPEN_MSS]	= { .type = NLA_U16, },
	[TCP_METRICS_ATTR_FOPEN_SYN_DROPS]	= { .type = NLA_U16, },
	[TCP_METRICS_ATTR_FOPEN_SYN_DROP_TS]	= { .type = NLA_MSECS, },
	[TCP_METRICS_ATTR_FOPEN_COOKIE]	= { .type = NLA_BINARY,
					    .len = TCP_FASTOPEN_COOKIE_MAX, },
#endif
};

/* Add attributes, caller cancels its header on failure */
static int tcp_metrics_fill_info(struct sk_buff *msg,
				 struct tcp_metrics_block *tm)
{
	struct nlattr *nest;
	int i;

	switch (tm->tcpm_addr.family) {
	case AF_INET:
		if (nla_put_be32(msg, TCP_METRICS_ATTR_ADDR_IPV4,
				tm->tcpm_addr.addr.a4) < 0)
			goto nla_put_failure;
		break;
	case AF_INET6:
		if (nla_put(msg, TCP_METRICS_ATTR_ADDR_IPV6, 16,
			    tm->tcpm_addr.addr.a6) < 0)
			goto nla_put_failure;
		break;
	default:
		return -EAFNOSUPPORT;
	}

	if (nla_put_msecs(msg, TCP_METRICS_ATTR_AGE,
			  jiffies - tm->tcpm_stamp) < 0)
		goto nla_put_failure;
	if (tm->tcpm_ts_stamp) {
		if (nla_put_s32(msg, TCP_METRICS_ATTR_TW_TS_STAMP,
				(s32) (get_seconds() - tm->tcpm_ts_stamp)) < 0)
			goto nla_put_failure;
		if (nla_put_u32(msg, TCP_METRICS_ATTR_TW_TSVAL,
				tm->tcpm_ts) < 0)
			goto nla_put_failure;
	}

	{
		int n = 0;

		nest = nla_nest_start(msg, TCP_METRICS_ATTR_VALS);
		if (!nest)
			goto nla_put_failure;
		for (i = 0; i < TCP_METRIC_MAX + 1; i++) {
			if (!tm->tcpm_vals[i])
				continue;
			if (nla_put_u32(msg, i + 1, tm->tcpm_vals[i]) < 0)
				goto nla_put_failure;
			n++;
		}
		if (n)
			nla_nest_end(msg, nest);
		else
			nla_nest_cancel(msg, nest);
	}

	{
		struct tcp_fastopen_metrics tfom_copy[1], *tfom;
		unsigned int seq;

		do {
			seq = read_seqbegin(&fastopen_seqlock);
			tfom_copy[0] = tm->tcpm_fastopen;
		} while (read_seqretry(&fastopen_seqlock, seq));

		tfom = tfom_copy;
		if (tfom->mss &&
		    nla_put_u16(msg, TCP_METRICS_ATTR_FOPEN_MSS,
				tfom->mss) < 0)
			goto nla_put_failure;
		if (tfom->syn_loss &&
		    (nla_put_u16(msg, TCP_METRICS_ATTR_FOPEN_SYN_DROPS,
				tfom->syn_loss) < 0 ||
		     nla_put_msecs(msg, TCP_METRICS_ATTR_FOPEN_SYN_DROP_TS,
				jiffies - tfom->last_syn_loss) < 0))
			goto nla_put_failure;
		if (tfom->cookie.len > 0 &&
		    nla_put(msg, TCP_METRICS_ATTR_FOPEN_COOKIE,
			    tfom->cookie.len, tfom->cookie.val) < 0)
			goto nla_put_failure;
	}

	return 0;

nla_put_failure:
	return -EMSGSIZE;
}

static int tcp_metrics_dump_info(struct sk_buff *skb,
				 struct netlink_callback *cb,
				 struct tcp_metrics_block *tm)
{
	void *hdr;

	hdr = genlmsg_put(skb, NETLINK_CB(cb->skb).portid, cb->nlh->nlmsg_seq,
			  &tcp_metrics_nl_family, NLM_F_MULTI,
			  TCP_METRICS_CMD_GET);
	if (!hdr)
		return -EMSGSIZE;

	if (tcp_metrics_fill_info(skb, tm) < 0)
		goto nla_put_failure;

	return genlmsg_end(skb, hdr);

nla_put_failure:
	genlmsg_cancel(skb, hdr);
	return -EMSGSIZE;
}

static int tcp_metrics_nl_dump(struct sk_buff *skb,
			       struct netlink_callback *cb)
{
	struct net *net = sock_net(skb->sk);
	unsigned int max_rows = 1U << net->ipv4.tcp_metrics_hash_log;
	unsigned int row, s_row = cb->args[0];
	int s_col = cb->args[1], col = s_col;

	for (row = s_row; row < max_rows; row++, s_col = 0) {
		struct tcp_metrics_block *tm;
		struct tcpm_hash_bucket *hb = net->ipv4.tcp_metrics_hash + row;

		rcu_read_lock();
		for (col = 0, tm = rcu_dereference(hb->chain); tm;
		     tm = rcu_dereference(tm->tcpm_next), col++) {
			if (col < s_col)
				continue;
			if (tcp_metrics_dump_info(skb, cb, tm) < 0) {
				rcu_read_unlock();
				goto done;
			}
		}
		rcu_read_unlock();
	}

done:
	cb->args[0] = row;
	cb->args[1] = col;
	return skb->len;
}

static int parse_nl_addr(struct genl_info *info, struct inetpeer_addr *addr,
			 unsigned int *hash, int optional)
{
	struct nlattr *a;

	a = info->attrs[TCP_METRICS_ATTR_ADDR_IPV4];
	if (a) {
		addr->family = AF_INET;
		addr->addr.a4 = nla_get_be32(a);
		*hash = (__force unsigned int) addr->addr.a4;
		return 0;
	}
	a = info->attrs[TCP_METRICS_ATTR_ADDR_IPV6];
	if (a) {
		if (nla_len(a) != sizeof(struct in6_addr))
			return -EINVAL;
		addr->family = AF_INET6;
		memcpy(addr->addr.a6, nla_data(a), sizeof(addr->addr.a6));
		*hash = ipv6_addr_hash((struct in6_addr *) addr->addr.a6);
		return 0;
	}
	return optional ? 1 : -EAFNOSUPPORT;
}

static int tcp_metrics_nl_cmd_get(struct sk_buff *skb, struct genl_info *info)
{
	struct tcp_metrics_block *tm;
	struct inetpeer_addr addr;
	unsigned int hash;
	struct sk_buff *msg;
	struct net *net = genl_info_net(info);
	void *reply;
	int ret;

	ret = parse_nl_addr(info, &addr, &hash, 0);
	if (ret < 0)
		return ret;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	reply = genlmsg_put_reply(msg, info, &tcp_metrics_nl_family, 0,
				  info->genlhdr->cmd);
	if (!reply)
		goto nla_put_failure;

	hash = hash_32(hash, net->ipv4.tcp_metrics_hash_log);
	ret = -ESRCH;
	rcu_read_lock();
	for (tm = rcu_dereference(net->ipv4.tcp_metrics_hash[hash].chain); tm;
	     tm = rcu_dereference(tm->tcpm_next)) {
		if (addr_same(&tm->tcpm_addr, &addr)) {
			ret = tcp_metrics_fill_info(msg, tm);
			break;
		}
	}
	rcu_read_unlock();
	if (ret < 0)
		goto out_free;

	genlmsg_end(msg, reply);
	return genlmsg_reply(msg, info);

nla_put_failure:
	ret = -EMSGSIZE;

out_free:
	nlmsg_free(msg);
	return ret;
}

#define deref_locked_genl(p)	\
	rcu_dereference_protected(p, lockdep_genl_is_held() && \
				     lockdep_is_held(&tcp_metrics_lock))

#define deref_genl(p)	rcu_dereference_protected(p, lockdep_genl_is_held())

static int tcp_metrics_flush_all(struct net *net)
{
	unsigned int max_rows = 1U << net->ipv4.tcp_metrics_hash_log;
	struct tcpm_hash_bucket *hb = net->ipv4.tcp_metrics_hash;
	struct tcp_metrics_block *tm;
	unsigned int row;

	for (row = 0; row < max_rows; row++, hb++) {
		spin_lock_bh(&tcp_metrics_lock);
		tm = deref_locked_genl(hb->chain);
		if (tm)
			hb->chain = NULL;
		spin_unlock_bh(&tcp_metrics_lock);
		while (tm) {
			struct tcp_metrics_block *next;

			next = deref_genl(tm->tcpm_next);
			kfree_rcu(tm, rcu_head);
			tm = next;
		}
	}
	return 0;
}

static int tcp_metrics_nl_cmd_del(struct sk_buff *skb, struct genl_info *info)
{
	struct tcpm_hash_bucket *hb;
	struct tcp_metrics_block *tm;
	struct tcp_metrics_block __rcu **pp;
	struct inetpeer_addr addr;
	unsigned int hash;
	struct net *net = genl_info_net(info);
	int ret;

	ret = parse_nl_addr(info, &addr, &hash, 1);
	if (ret < 0)
		return ret;
	if (ret > 0)
		return tcp_metrics_flush_all(net);

	hash = hash_32(hash, net->ipv4.tcp_metrics_hash_log);
	hb = net->ipv4.tcp_metrics_hash + hash;
	pp = &hb->chain;
	spin_lock_bh(&tcp_metrics_lock);
	for (tm = deref_locked_genl(*pp); tm;
	     pp = &tm->tcpm_next, tm = deref_locked_genl(*pp)) {
		if (addr_same(&tm->tcpm_addr, &addr)) {
			*pp = tm->tcpm_next;
			break;
		}
	}
	spin_unlock_bh(&tcp_metrics_lock);
	if (!tm)
		return -ESRCH;
	kfree_rcu(tm, rcu_head);
	return 0;
}

static struct genl_ops tcp_metrics_nl_ops[] = {
	{
		.cmd = TCP_METRICS_CMD_GET,
		.doit = tcp_metrics_nl_cmd_get,
		.dumpit = tcp_metrics_nl_dump,
		.policy = tcp_metrics_nl_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = TCP_METRICS_CMD_DEL,
		.doit = tcp_metrics_nl_cmd_del,
		.policy = tcp_metrics_nl_policy,
		.flags = GENL_ADMIN_PERM,
	},
};

static unsigned int tcpmhash_entries;
static int __init set_tcpmhash_entries(char *str)
{
	ssize_t ret;

	if (!str)
		return 0;

	ret = kstrtouint(str, 0, &tcpmhash_entries);
	if (ret)
		return 0;

	return 1;
}
__setup("tcpmhash_entries=", set_tcpmhash_entries);

static int __net_init tcp_net_metrics_init(struct net *net)
{
	size_t size;
	unsigned int slots;

	slots = tcpmhash_entries;
	if (!slots) {
		if (totalram_pages >= 128 * 1024)
			slots = 16 * 1024;
		else
			slots = 8 * 1024;
	}

	net->ipv4.tcp_metrics_hash_log = order_base_2(slots);
	size = sizeof(struct tcpm_hash_bucket) << net->ipv4.tcp_metrics_hash_log;

	net->ipv4.tcp_metrics_hash = kzalloc(size, GFP_KERNEL | __GFP_NOWARN);
	if (!net->ipv4.tcp_metrics_hash)
		net->ipv4.tcp_metrics_hash = vzalloc(size);

	if (!net->ipv4.tcp_metrics_hash)
		return -ENOMEM;

	return 0;
}

static void __net_exit tcp_net_metrics_exit(struct net *net)
{
	unsigned int i;

	for (i = 0; i < (1U << net->ipv4.tcp_metrics_hash_log) ; i++) {
		struct tcp_metrics_block *tm, *next;

		tm = rcu_dereference_protected(net->ipv4.tcp_metrics_hash[i].chain, 1);
		while (tm) {
			next = rcu_dereference_protected(tm->tcpm_next, 1);
			kfree(tm);
			tm = next;
		}
	}
	if (is_vmalloc_addr(net->ipv4.tcp_metrics_hash))
		vfree(net->ipv4.tcp_metrics_hash);
	else
		kfree(net->ipv4.tcp_metrics_hash);
}

static __net_initdata struct pernet_operations tcp_net_metrics_ops = {
	.init	=	tcp_net_metrics_init,
	.exit	=	tcp_net_metrics_exit,
};

void __init tcp_metrics_init(void)
{
	int ret;

	ret = register_pernet_subsys(&tcp_net_metrics_ops);
	if (ret < 0)
		goto cleanup;
	ret = genl_register_family_with_ops(&tcp_metrics_nl_family,
					    tcp_metrics_nl_ops,
					    ARRAY_SIZE(tcp_metrics_nl_ops));
	if (ret < 0)
		goto cleanup_subsys;
	return;

cleanup_subsys:
	unregister_pernet_subsys(&tcp_net_metrics_ops);

cleanup:
	return;
}
