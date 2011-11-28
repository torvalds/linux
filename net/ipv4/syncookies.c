/*
 *  Syncookies implementation for the Linux kernel
 *
 *  Copyright (C) 1997 Andi Kleen
 *  Based on ideas by D.J.Bernstein and Eric Schenk.
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/tcp.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/cryptohash.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <net/tcp.h>
#include <net/route.h>

/* Timestamps: lowest bits store TCP options */
#define TSBITS 6
#define TSMASK (((__u32)1 << TSBITS) - 1)

extern int sysctl_tcp_syncookies;

__u32 syncookie_secret[2][16-4+SHA_DIGEST_WORDS];
EXPORT_SYMBOL(syncookie_secret);

static __init int init_syncookies(void)
{
	get_random_bytes(syncookie_secret, sizeof(syncookie_secret));
	return 0;
}
__initcall(init_syncookies);

#define COOKIEBITS 24	/* Upper bits store count */
#define COOKIEMASK (((__u32)1 << COOKIEBITS) - 1)

static DEFINE_PER_CPU(__u32 [16 + 5 + SHA_WORKSPACE_WORDS],
		      ipv4_cookie_scratch);

static u32 cookie_hash(__be32 saddr, __be32 daddr, __be16 sport, __be16 dport,
		       u32 count, int c)
{
	__u32 *tmp = __get_cpu_var(ipv4_cookie_scratch);

	memcpy(tmp + 4, syncookie_secret[c], sizeof(syncookie_secret[c]));
	tmp[0] = (__force u32)saddr;
	tmp[1] = (__force u32)daddr;
	tmp[2] = ((__force u32)sport << 16) + (__force u32)dport;
	tmp[3] = count;
	sha_transform(tmp + 16, (__u8 *)tmp, tmp + 16 + 5);

	return tmp[17];
}


/*
 * when syncookies are in effect and tcp timestamps are enabled we encode
 * tcp options in the lower bits of the timestamp value that will be
 * sent in the syn-ack.
 * Since subsequent timestamps use the normal tcp_time_stamp value, we
 * must make sure that the resulting initial timestamp is <= tcp_time_stamp.
 */
__u32 cookie_init_timestamp(struct request_sock *req)
{
	struct inet_request_sock *ireq;
	u32 ts, ts_now = tcp_time_stamp;
	u32 options = 0;

	ireq = inet_rsk(req);

	options = ireq->wscale_ok ? ireq->snd_wscale : 0xf;
	options |= ireq->sack_ok << 4;
	options |= ireq->ecn_ok << 5;

	ts = ts_now & ~TSMASK;
	ts |= options;
	if (ts > ts_now) {
		ts >>= TSBITS;
		ts--;
		ts <<= TSBITS;
		ts |= options;
	}
	return ts;
}


static __u32 secure_tcp_syn_cookie(__be32 saddr, __be32 daddr, __be16 sport,
				   __be16 dport, __u32 sseq, __u32 count,
				   __u32 data)
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
 * The count value used to generate the cookie must be within
 * "maxdiff" if the current (passed-in) "count".  The return value
 * is (__u32)-1 if this test fails.
 */
static __u32 check_tcp_syn_cookie(__u32 cookie, __be32 saddr, __be32 daddr,
				  __be16 sport, __be16 dport, __u32 sseq,
				  __u32 count, __u32 maxdiff)
{
	__u32 diff;

	/* Strip away the layers from the cookie */
	cookie -= cookie_hash(saddr, daddr, sport, dport, 0, 0) + sseq;

	/* Cookie is now reduced to (count * 2^24) ^ (hash % 2^24) */
	diff = (count - (cookie >> COOKIEBITS)) & ((__u32) - 1 >> COOKIEBITS);
	if (diff >= maxdiff)
		return (__u32)-1;

	return (cookie -
		cookie_hash(saddr, daddr, sport, dport, count - diff, 1))
		& COOKIEMASK;	/* Leaving the data behind */
}

/*
 * MSS Values are taken from the 2009 paper
 * 'Measuring TCP Maximum Segment Size' by S. Alcock and R. Nelson:
 *  - values 1440 to 1460 accounted for 80% of observed mss values
 *  - values outside the 536-1460 range are rare (<0.2%).
 *
 * Table must be sorted.
 */
static __u16 const msstab[] = {
	64,
	512,
	536,
	1024,
	1440,
	1460,
	4312,
	8960,
};

/*
 * Generate a syncookie.  mssp points to the mss, which is returned
 * rounded down to the value encoded in the cookie.
 */
__u32 cookie_v4_init_sequence(struct sock *sk, struct sk_buff *skb, __u16 *mssp)
{
	const struct iphdr *iph = ip_hdr(skb);
	const struct tcphdr *th = tcp_hdr(skb);
	int mssind;
	const __u16 mss = *mssp;

	tcp_synq_overflow(sk);

	for (mssind = ARRAY_SIZE(msstab) - 1; mssind ; mssind--)
		if (mss >= msstab[mssind])
			break;
	*mssp = msstab[mssind];

	NET_INC_STATS_BH(sock_net(sk), LINUX_MIB_SYNCOOKIESSENT);

	return secure_tcp_syn_cookie(iph->saddr, iph->daddr,
				     th->source, th->dest, ntohl(th->seq),
				     jiffies / (HZ * 60), mssind);
}

/*
 * This (misnamed) value is the age of syncookie which is permitted.
 * Its ideal value should be dependent on TCP_TIMEOUT_INIT and
 * sysctl_tcp_retries1. It's a rather complicated formula (exponential
 * backoff) to compute at runtime so it's currently hardcoded here.
 */
#define COUNTER_TRIES 4
/*
 * Check if a ack sequence number is a valid syncookie.
 * Return the decoded mss if it is, or 0 if not.
 */
static inline int cookie_check(struct sk_buff *skb, __u32 cookie)
{
	const struct iphdr *iph = ip_hdr(skb);
	const struct tcphdr *th = tcp_hdr(skb);
	__u32 seq = ntohl(th->seq) - 1;
	__u32 mssind = check_tcp_syn_cookie(cookie, iph->saddr, iph->daddr,
					    th->source, th->dest, seq,
					    jiffies / (HZ * 60),
					    COUNTER_TRIES);

	return mssind < ARRAY_SIZE(msstab) ? msstab[mssind] : 0;
}

static inline struct sock *get_cookie_sock(struct sock *sk, struct sk_buff *skb,
					   struct request_sock *req,
					   struct dst_entry *dst)
{
	struct inet_connection_sock *icsk = inet_csk(sk);
	struct sock *child;

	child = icsk->icsk_af_ops->syn_recv_sock(sk, skb, req, dst);
	if (child)
		inet_csk_reqsk_queue_add(sk, req, child);
	else
		reqsk_free(req);

	return child;
}


/*
 * when syncookies are in effect and tcp timestamps are enabled we stored
 * additional tcp options in the timestamp.
 * This extracts these options from the timestamp echo.
 *
 * The lowest 4 bits store snd_wscale.
 * next 2 bits indicate SACK and ECN support.
 *
 * return false if we decode an option that should not be.
 */
bool cookie_check_timestamp(struct tcp_options_received *tcp_opt, bool *ecn_ok)
{
	/* echoed timestamp, lowest bits contain options */
	u32 options = tcp_opt->rcv_tsecr & TSMASK;

	if (!tcp_opt->saw_tstamp)  {
		tcp_clear_options(tcp_opt);
		return true;
	}

	if (!sysctl_tcp_timestamps)
		return false;

	tcp_opt->sack_ok = (options >> 4) & 0x1;
	*ecn_ok = (options >> 5) & 1;
	if (*ecn_ok && !sysctl_tcp_ecn)
		return false;

	if (tcp_opt->sack_ok && !sysctl_tcp_sack)
		return false;

	if ((options & 0xf) == 0xf)
		return true; /* no window scaling */

	tcp_opt->wscale_ok = 1;
	tcp_opt->snd_wscale = options & 0xf;
	return sysctl_tcp_window_scaling != 0;
}
EXPORT_SYMBOL(cookie_check_timestamp);

struct sock *cookie_v4_check(struct sock *sk, struct sk_buff *skb,
			     struct ip_options *opt)
{
	struct tcp_options_received tcp_opt;
	const u8 *hash_location;
	struct inet_request_sock *ireq;
	struct tcp_request_sock *treq;
	struct tcp_sock *tp = tcp_sk(sk);
	const struct tcphdr *th = tcp_hdr(skb);
	__u32 cookie = ntohl(th->ack_seq) - 1;
	struct sock *ret = sk;
	struct request_sock *req;
	int mss;
	struct rtable *rt;
	__u8 rcv_wscale;
	bool ecn_ok = false;

	if (!sysctl_tcp_syncookies || !th->ack || th->rst)
		goto out;

	if (tcp_synq_no_recent_overflow(sk) ||
	    (mss = cookie_check(skb, cookie)) == 0) {
		NET_INC_STATS_BH(sock_net(sk), LINUX_MIB_SYNCOOKIESFAILED);
		goto out;
	}

	NET_INC_STATS_BH(sock_net(sk), LINUX_MIB_SYNCOOKIESRECV);

	/* check for timestamp cookie support */
	memset(&tcp_opt, 0, sizeof(tcp_opt));
	tcp_parse_options(skb, &tcp_opt, &hash_location, 0);

	if (!cookie_check_timestamp(&tcp_opt, &ecn_ok))
		goto out;

	ret = NULL;
	req = inet_reqsk_alloc(&tcp_request_sock_ops); /* for safety */
	if (!req)
		goto out;

	ireq = inet_rsk(req);
	treq = tcp_rsk(req);
	treq->rcv_isn		= ntohl(th->seq) - 1;
	treq->snt_isn		= cookie;
	req->mss		= mss;
	ireq->loc_port		= th->dest;
	ireq->rmt_port		= th->source;
	ireq->loc_addr		= ip_hdr(skb)->daddr;
	ireq->rmt_addr		= ip_hdr(skb)->saddr;
	ireq->ecn_ok		= ecn_ok;
	ireq->snd_wscale	= tcp_opt.snd_wscale;
	ireq->sack_ok		= tcp_opt.sack_ok;
	ireq->wscale_ok		= tcp_opt.wscale_ok;
	ireq->tstamp_ok		= tcp_opt.saw_tstamp;
	req->ts_recent		= tcp_opt.saw_tstamp ? tcp_opt.rcv_tsval : 0;
	treq->snt_synack	= tcp_opt.saw_tstamp ? tcp_opt.rcv_tsecr : 0;

	/* We throwed the options of the initial SYN away, so we hope
	 * the ACK carries the same options again (see RFC1122 4.2.3.8)
	 */
	if (opt && opt->optlen) {
		int opt_size = sizeof(struct ip_options_rcu) + opt->optlen;

		ireq->opt = kmalloc(opt_size, GFP_ATOMIC);
		if (ireq->opt != NULL && ip_options_echo(&ireq->opt->opt, skb)) {
			kfree(ireq->opt);
			ireq->opt = NULL;
		}
	}

	if (security_inet_conn_request(sk, skb, req)) {
		reqsk_free(req);
		goto out;
	}

	req->expires	= 0UL;
	req->retrans	= 0;

	/*
	 * We need to lookup the route here to get at the correct
	 * window size. We should better make sure that the window size
	 * hasn't changed since we received the original syn, but I see
	 * no easy way to do this.
	 */
	{
		struct flowi4 fl4;

		flowi4_init_output(&fl4, 0, sk->sk_mark, RT_CONN_FLAGS(sk),
				   RT_SCOPE_UNIVERSE, IPPROTO_TCP,
				   inet_sk_flowi_flags(sk),
				   (opt && opt->srr) ? opt->faddr : ireq->rmt_addr,
				   ireq->loc_addr, th->source, th->dest);
		security_req_classify_flow(req, flowi4_to_flowi(&fl4));
		rt = ip_route_output_key(sock_net(sk), &fl4);
		if (IS_ERR(rt)) {
			reqsk_free(req);
			goto out;
		}
	}

	/* Try to redo what tcp_v4_send_synack did. */
	req->window_clamp = tp->window_clamp ? :dst_metric(&rt->dst, RTAX_WINDOW);

	tcp_select_initial_window(tcp_full_space(sk), req->mss,
				  &req->rcv_wnd, &req->window_clamp,
				  ireq->wscale_ok, &rcv_wscale,
				  dst_metric(&rt->dst, RTAX_INITRWND));

	ireq->rcv_wscale  = rcv_wscale;

	ret = get_cookie_sock(sk, skb, req, &rt->dst);
out:	return ret;
}
