// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Syncookies implementation for the Linux kernel
 *
 *  Copyright (C) 1997 Andi Kleen
 *  Based on ideas by D.J.Bernstein and Eric Schenk.
 */

#include <linux/tcp.h>
#include <linux/siphash.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <net/secure_seq.h>
#include <net/tcp.h>
#include <net/route.h>

static siphash_aligned_key_t syncookie_secret[2];

#define COOKIEBITS 24	/* Upper bits store count */
#define COOKIEMASK (((__u32)1 << COOKIEBITS) - 1)

/* TCP Timestamp: 6 lowest bits of timestamp sent in the cookie SYN-ACK
 * stores TCP options:
 *
 * MSB                               LSB
 * | 31 ...   6 |  5  |  4   | 3 2 1 0 |
 * |  Timestamp | ECN | SACK | WScale  |
 *
 * When we receive a valid cookie-ACK, we look at the echoed tsval (if
 * any) to figure out which TCP options we should use for the rebuilt
 * connection.
 *
 * A WScale setting of '0xf' (which is an invalid scaling value)
 * means that original syn did not include the TCP window scaling option.
 */
#define TS_OPT_WSCALE_MASK	0xf
#define TS_OPT_SACK		BIT(4)
#define TS_OPT_ECN		BIT(5)
/* There is no TS_OPT_TIMESTAMP:
 * if ACK contains timestamp option, we already know it was
 * requested/supported by the syn/synack exchange.
 */
#define TSBITS	6

static u32 cookie_hash(__be32 saddr, __be32 daddr, __be16 sport, __be16 dport,
		       u32 count, int c)
{
	net_get_random_once(syncookie_secret, sizeof(syncookie_secret));
	return siphash_4u32((__force u32)saddr, (__force u32)daddr,
			    (__force u32)sport << 16 | (__force u32)dport,
			    count, &syncookie_secret[c]);
}

/*
 * when syncookies are in effect and tcp timestamps are enabled we encode
 * tcp options in the lower bits of the timestamp value that will be
 * sent in the syn-ack.
 * Since subsequent timestamps use the normal tcp_time_stamp value, we
 * must make sure that the resulting initial timestamp is <= tcp_time_stamp.
 */
u64 cookie_init_timestamp(struct request_sock *req, u64 now)
{
	const struct inet_request_sock *ireq = inet_rsk(req);
	u64 ts, ts_now = tcp_ns_to_ts(false, now);
	u32 options = 0;

	options = ireq->wscale_ok ? ireq->snd_wscale : TS_OPT_WSCALE_MASK;
	if (ireq->sack_ok)
		options |= TS_OPT_SACK;
	if (ireq->ecn_ok)
		options |= TS_OPT_ECN;

	ts = (ts_now >> TSBITS) << TSBITS;
	ts |= options;
	if (ts > ts_now)
		ts -= (1UL << TSBITS);

	if (tcp_rsk(req)->req_usec_ts)
		return ts * NSEC_PER_USEC;
	return ts * NSEC_PER_MSEC;
}


static __u32 secure_tcp_syn_cookie(__be32 saddr, __be32 daddr, __be16 sport,
				   __be16 dport, __u32 sseq, __u32 data)
{
	/*
	 * Compute the secure sequence number.
	 * The output should be:
	 *   HASH(sec1,saddr,sport,daddr,dport,sec1) + sseq + (count * 2^24)
	 *      + (HASH(sec2,saddr,sport,daddr,dport,count,sec2) % 2^24).
	 * Where sseq is their sequence number and count increases every
	 * minute by 1.
	 * As an extra hack, we add a small "data" value that encodes the
	 * MSS into the second hash value.
	 */
	u32 count = tcp_cookie_time();
	return (cookie_hash(saddr, daddr, sport, dport, 0, 0) +
		sseq + (count << COOKIEBITS) +
		((cookie_hash(saddr, daddr, sport, dport, count, 1) + data)
		 & COOKIEMASK));
}

/*
 * This retrieves the small "data" value from the syncookie.
 * If the syncookie is bad, the data returned will be out of
 * range.  This must be checked by the caller.
 *
 * The count value used to generate the cookie must be less than
 * MAX_SYNCOOKIE_AGE minutes in the past.
 * The return value (__u32)-1 if this test fails.
 */
static __u32 check_tcp_syn_cookie(__u32 cookie, __be32 saddr, __be32 daddr,
				  __be16 sport, __be16 dport, __u32 sseq)
{
	u32 diff, count = tcp_cookie_time();

	/* Strip away the layers from the cookie */
	cookie -= cookie_hash(saddr, daddr, sport, dport, 0, 0) + sseq;

	/* Cookie is now reduced to (count * 2^24) ^ (hash % 2^24) */
	diff = (count - (cookie >> COOKIEBITS)) & ((__u32) -1 >> COOKIEBITS);
	if (diff >= MAX_SYNCOOKIE_AGE)
		return (__u32)-1;

	return (cookie -
		cookie_hash(saddr, daddr, sport, dport, count - diff, 1))
		& COOKIEMASK;	/* Leaving the data behind */
}

/*
 * MSS Values are chosen based on the 2011 paper
 * 'An Analysis of TCP Maximum Segement Sizes' by S. Alcock and R. Nelson.
 * Values ..
 *  .. lower than 536 are rare (< 0.2%)
 *  .. between 537 and 1299 account for less than < 1.5% of observed values
 *  .. in the 1300-1349 range account for about 15 to 20% of observed mss values
 *  .. exceeding 1460 are very rare (< 0.04%)
 *
 *  1460 is the single most frequently announced mss value (30 to 46% depending
 *  on monitor location).  Table must be sorted.
 */
static __u16 const msstab[] = {
	536,
	1300,
	1440,	/* 1440, 1452: PPPoE */
	1460,
};

/*
 * Generate a syncookie.  mssp points to the mss, which is returned
 * rounded down to the value encoded in the cookie.
 */
u32 __cookie_v4_init_sequence(const struct iphdr *iph, const struct tcphdr *th,
			      u16 *mssp)
{
	int mssind;
	const __u16 mss = *mssp;

	for (mssind = ARRAY_SIZE(msstab) - 1; mssind ; mssind--)
		if (mss >= msstab[mssind])
			break;
	*mssp = msstab[mssind];

	return secure_tcp_syn_cookie(iph->saddr, iph->daddr,
				     th->source, th->dest, ntohl(th->seq),
				     mssind);
}
EXPORT_SYMBOL_GPL(__cookie_v4_init_sequence);

__u32 cookie_v4_init_sequence(const struct sk_buff *skb, __u16 *mssp)
{
	const struct iphdr *iph = ip_hdr(skb);
	const struct tcphdr *th = tcp_hdr(skb);

	return __cookie_v4_init_sequence(iph, th, mssp);
}

/*
 * Check if a ack sequence number is a valid syncookie.
 * Return the decoded mss if it is, or 0 if not.
 */
int __cookie_v4_check(const struct iphdr *iph, const struct tcphdr *th)
{
	__u32 cookie = ntohl(th->ack_seq) - 1;
	__u32 seq = ntohl(th->seq) - 1;
	__u32 mssind;

	mssind = check_tcp_syn_cookie(cookie, iph->saddr, iph->daddr,
				      th->source, th->dest, seq);

	return mssind < ARRAY_SIZE(msstab) ? msstab[mssind] : 0;
}
EXPORT_SYMBOL_GPL(__cookie_v4_check);

struct sock *tcp_get_cookie_sock(struct sock *sk, struct sk_buff *skb,
				 struct request_sock *req,
				 struct dst_entry *dst)
{
	struct inet_connection_sock *icsk = inet_csk(sk);
	struct sock *child;
	bool own_req;

	child = icsk->icsk_af_ops->syn_recv_sock(sk, skb, req, dst,
						 NULL, &own_req);
	if (child) {
		refcount_set(&req->rsk_refcnt, 1);
		sock_rps_save_rxhash(child, skb);

		if (rsk_drop_req(req)) {
			reqsk_put(req);
			return child;
		}

		if (inet_csk_reqsk_queue_add(sk, req, child))
			return child;

		bh_unlock_sock(child);
		sock_put(child);
	}
	__reqsk_free(req);

	return NULL;
}
EXPORT_SYMBOL(tcp_get_cookie_sock);

/*
 * when syncookies are in effect and tcp timestamps are enabled we stored
 * additional tcp options in the timestamp.
 * This extracts these options from the timestamp echo.
 *
 * return false if we decode a tcp option that is disabled
 * on the host.
 */
bool cookie_timestamp_decode(const struct net *net,
			     struct tcp_options_received *tcp_opt)
{
	/* echoed timestamp, lowest bits contain options */
	u32 options = tcp_opt->rcv_tsecr;

	if (!tcp_opt->saw_tstamp)  {
		tcp_clear_options(tcp_opt);
		return true;
	}

	if (!READ_ONCE(net->ipv4.sysctl_tcp_timestamps))
		return false;

	tcp_opt->sack_ok = (options & TS_OPT_SACK) ? TCP_SACK_SEEN : 0;

	if (tcp_opt->sack_ok && !READ_ONCE(net->ipv4.sysctl_tcp_sack))
		return false;

	if ((options & TS_OPT_WSCALE_MASK) == TS_OPT_WSCALE_MASK)
		return true; /* no window scaling */

	tcp_opt->wscale_ok = 1;
	tcp_opt->snd_wscale = options & TS_OPT_WSCALE_MASK;

	return READ_ONCE(net->ipv4.sysctl_tcp_window_scaling) != 0;
}
EXPORT_SYMBOL(cookie_timestamp_decode);

static int cookie_tcp_reqsk_init(struct sock *sk, struct sk_buff *skb,
				 struct request_sock *req)
{
	struct inet_request_sock *ireq = inet_rsk(req);
	struct tcp_request_sock *treq = tcp_rsk(req);
	const struct tcphdr *th = tcp_hdr(skb);

	req->num_retrans = 0;

	ireq->ir_num = ntohs(th->dest);
	ireq->ir_rmt_port = th->source;
	ireq->ir_iif = inet_request_bound_dev_if(sk, skb);
	ireq->ir_mark = inet_request_mark(sk, skb);

	if (IS_ENABLED(CONFIG_SMC))
		ireq->smc_ok = 0;

	treq->snt_synack = 0;
	treq->tfo_listener = false;
	treq->txhash = net_tx_rndhash();
	treq->rcv_isn = ntohl(th->seq) - 1;
	treq->snt_isn = ntohl(th->ack_seq) - 1;
	treq->syn_tos = TCP_SKB_CB(skb)->ip_dsfield;
	treq->req_usec_ts = false;

#if IS_ENABLED(CONFIG_MPTCP)
	treq->is_mptcp = sk_is_mptcp(sk);
	if (treq->is_mptcp)
		return mptcp_subflow_init_cookie_req(req, sk, skb);
#endif

	return 0;
}

#if IS_ENABLED(CONFIG_BPF)
struct request_sock *cookie_bpf_check(struct sock *sk, struct sk_buff *skb)
{
	struct request_sock *req = inet_reqsk(skb->sk);

	skb->sk = NULL;
	skb->destructor = NULL;

	if (cookie_tcp_reqsk_init(sk, skb, req)) {
		reqsk_free(req);
		req = NULL;
	}

	return req;
}
EXPORT_SYMBOL_GPL(cookie_bpf_check);
#endif

struct request_sock *cookie_tcp_reqsk_alloc(const struct request_sock_ops *ops,
					    struct sock *sk, struct sk_buff *skb,
					    struct tcp_options_received *tcp_opt,
					    int mss, u32 tsoff)
{
	struct inet_request_sock *ireq;
	struct tcp_request_sock *treq;
	struct request_sock *req;

	if (sk_is_mptcp(sk))
		req = mptcp_subflow_reqsk_alloc(ops, sk, false);
	else
		req = inet_reqsk_alloc(ops, sk, false);

	if (!req)
		return NULL;

	if (cookie_tcp_reqsk_init(sk, skb, req)) {
		reqsk_free(req);
		return NULL;
	}

	ireq = inet_rsk(req);
	treq = tcp_rsk(req);

	req->mss = mss;
	req->ts_recent = tcp_opt->saw_tstamp ? tcp_opt->rcv_tsval : 0;

	ireq->snd_wscale = tcp_opt->snd_wscale;
	ireq->tstamp_ok = tcp_opt->saw_tstamp;
	ireq->sack_ok = tcp_opt->sack_ok;
	ireq->wscale_ok = tcp_opt->wscale_ok;
	ireq->ecn_ok = !!(tcp_opt->rcv_tsecr & TS_OPT_ECN);

	treq->ts_off = tsoff;

	return req;
}
EXPORT_SYMBOL_GPL(cookie_tcp_reqsk_alloc);

static struct request_sock *cookie_tcp_check(struct net *net, struct sock *sk,
					     struct sk_buff *skb)
{
	struct tcp_options_received tcp_opt;
	u32 tsoff = 0;
	int mss;

	if (tcp_synq_no_recent_overflow(sk))
		goto out;

	mss = __cookie_v4_check(ip_hdr(skb), tcp_hdr(skb));
	if (!mss) {
		__NET_INC_STATS(net, LINUX_MIB_SYNCOOKIESFAILED);
		goto out;
	}

	__NET_INC_STATS(net, LINUX_MIB_SYNCOOKIESRECV);

	/* check for timestamp cookie support */
	memset(&tcp_opt, 0, sizeof(tcp_opt));
	tcp_parse_options(net, skb, &tcp_opt, 0, NULL);

	if (tcp_opt.saw_tstamp && tcp_opt.rcv_tsecr) {
		tsoff = secure_tcp_ts_off(net,
					  ip_hdr(skb)->daddr,
					  ip_hdr(skb)->saddr);
		tcp_opt.rcv_tsecr -= tsoff;
	}

	if (!cookie_timestamp_decode(net, &tcp_opt))
		goto out;

	return cookie_tcp_reqsk_alloc(&tcp_request_sock_ops, sk, skb,
				      &tcp_opt, mss, tsoff);
out:
	return ERR_PTR(-EINVAL);
}

/* On input, sk is a listener.
 * Output is listener if incoming packet would not create a child
 *           NULL if memory could not be allocated.
 */
struct sock *cookie_v4_check(struct sock *sk, struct sk_buff *skb)
{
	struct ip_options *opt = &TCP_SKB_CB(skb)->header.h4.opt;
	const struct tcphdr *th = tcp_hdr(skb);
	struct tcp_sock *tp = tcp_sk(sk);
	struct inet_request_sock *ireq;
	struct net *net = sock_net(sk);
	struct request_sock *req;
	struct sock *ret = sk;
	struct flowi4 fl4;
	struct rtable *rt;
	__u8 rcv_wscale;
	int full_space;
	SKB_DR(reason);

	if (!READ_ONCE(net->ipv4.sysctl_tcp_syncookies) ||
	    !th->ack || th->rst)
		goto out;

	if (cookie_bpf_ok(skb)) {
		req = cookie_bpf_check(sk, skb);
	} else {
		req = cookie_tcp_check(net, sk, skb);
		if (IS_ERR(req))
			goto out;
	}
	if (!req) {
		SKB_DR_SET(reason, NO_SOCKET);
		goto out_drop;
	}

	ireq = inet_rsk(req);

	sk_rcv_saddr_set(req_to_sk(req), ip_hdr(skb)->daddr);
	sk_daddr_set(req_to_sk(req), ip_hdr(skb)->saddr);

	/* We throwed the options of the initial SYN away, so we hope
	 * the ACK carries the same options again (see RFC1122 4.2.3.8)
	 */
	RCU_INIT_POINTER(ireq->ireq_opt, tcp_v4_save_options(net, skb));

	if (security_inet_conn_request(sk, skb, req)) {
		SKB_DR_SET(reason, SECURITY_HOOK);
		goto out_free;
	}

	tcp_ao_syncookie(sk, skb, req, AF_INET);

	/*
	 * We need to lookup the route here to get at the correct
	 * window size. We should better make sure that the window size
	 * hasn't changed since we received the original syn, but I see
	 * no easy way to do this.
	 */
	flowi4_init_output(&fl4, ireq->ir_iif, ireq->ir_mark,
			   ip_sock_rt_tos(sk), ip_sock_rt_scope(sk),
			   IPPROTO_TCP, inet_sk_flowi_flags(sk),
			   opt->srr ? opt->faddr : ireq->ir_rmt_addr,
			   ireq->ir_loc_addr, th->source, th->dest, sk->sk_uid);
	security_req_classify_flow(req, flowi4_to_flowi_common(&fl4));
	rt = ip_route_output_key(net, &fl4);
	if (IS_ERR(rt)) {
		SKB_DR_SET(reason, IP_OUTNOROUTES);
		goto out_free;
	}

	/* Try to redo what tcp_v4_send_synack did. */
	req->rsk_window_clamp = READ_ONCE(tp->window_clamp) ? :
				dst_metric(&rt->dst, RTAX_WINDOW);
	/* limit the window selection if the user enforce a smaller rx buffer */
	full_space = tcp_full_space(sk);
	if (sk->sk_userlocks & SOCK_RCVBUF_LOCK &&
	    (req->rsk_window_clamp > full_space || req->rsk_window_clamp == 0))
		req->rsk_window_clamp = full_space;

	tcp_select_initial_window(sk, full_space, req->mss,
				  &req->rsk_rcv_wnd, &req->rsk_window_clamp,
				  ireq->wscale_ok, &rcv_wscale,
				  dst_metric(&rt->dst, RTAX_INITRWND));

	/* req->syncookie is set true only if ACK is validated
	 * by BPF kfunc, then, rcv_wscale is already configured.
	 */
	if (!req->syncookie)
		ireq->rcv_wscale = rcv_wscale;
	ireq->ecn_ok &= cookie_ecn_ok(net, &rt->dst);

	ret = tcp_get_cookie_sock(sk, skb, req, &rt->dst);
	/* ip_queue_xmit() depends on our flow being setup
	 * Normal sockets get it right from inet_csk_route_child_sock()
	 */
	if (!ret) {
		SKB_DR_SET(reason, NO_SOCKET);
		goto out_drop;
	}
	inet_sk(ret)->cork.fl.u.ip4 = fl4;
out:
	return ret;
out_free:
	reqsk_free(req);
out_drop:
	kfree_skb_reason(skb, reason);
	return NULL;
}
