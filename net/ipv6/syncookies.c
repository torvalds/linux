/*
 *  IPv6 Syncookies implementation for the Linux kernel
 *
 *  Authors:
 *  Glenn Griffin	<ggriffin.kernel@gmail.com>
 *
 *  Based on IPv4 implementation by Andi Kleen
 *  linux/net/ipv4/syncookies.c
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 */

#include <linux/tcp.h>
#include <linux/random.h>
#include <linux/cryptohash.h>
#include <linux/kernel.h>
#include <net/ipv6.h>
#include <net/tcp.h>

extern int sysctl_tcp_syncookies;
extern __u32 syncookie_secret[2][16-4+SHA_DIGEST_WORDS];

#define COOKIEBITS 24	/* Upper bits store count */
#define COOKIEMASK (((__u32)1 << COOKIEBITS) - 1)

/*
 * This table has to be sorted and terminated with (__u16)-1.
 * XXX generate a better table.
 * Unresolved Issues: HIPPI with a 64k MSS is not well supported.
 *
 * Taken directly from ipv4 implementation.
 * Should this list be modified for ipv6 use or is it close enough?
 * rfc 2460 8.3 suggests mss values 20 bytes less than ipv4 counterpart
 */
static __u16 const msstab[] = {
	64 - 1,
	256 - 1,
	512 - 1,
	536 - 1,
	1024 - 1,
	1440 - 1,
	1460 - 1,
	4312 - 1,
	(__u16)-1
};
/* The number doesn't include the -1 terminator */
#define NUM_MSS (ARRAY_SIZE(msstab) - 1)

/*
 * This (misnamed) value is the age of syncookie which is permitted.
 * Its ideal value should be dependent on TCP_TIMEOUT_INIT and
 * sysctl_tcp_retries1. It's a rather complicated formula (exponential
 * backoff) to compute at runtime so it's currently hardcoded here.
 */
#define COUNTER_TRIES 4

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

static DEFINE_PER_CPU(__u32, cookie_scratch)[16 + 5 + SHA_WORKSPACE_WORDS];

static u32 cookie_hash(struct in6_addr *saddr, struct in6_addr *daddr,
		       __be16 sport, __be16 dport, u32 count, int c)
{
	__u32 *tmp = __get_cpu_var(cookie_scratch);

	/*
	 * we have 320 bits of information to hash, copy in the remaining
	 * 192 bits required for sha_transform, from the syncookie_secret
	 * and overwrite the digest with the secret
	 */
	memcpy(tmp + 10, syncookie_secret[c], 44);
	memcpy(tmp, saddr, 16);
	memcpy(tmp + 4, daddr, 16);
	tmp[8] = ((__force u32)sport << 16) + (__force u32)dport;
	tmp[9] = count;
	sha_transform(tmp + 16, (__u8 *)tmp, tmp + 16 + 5);

	return tmp[17];
}

static __u32 secure_tcp_syn_cookie(struct in6_addr *saddr, struct in6_addr *daddr,
				   __be16 sport, __be16 dport, __u32 sseq,
				   __u32 count, __u32 data)
{
	return (cookie_hash(saddr, daddr, sport, dport, 0, 0) +
		sseq + (count << COOKIEBITS) +
		((cookie_hash(saddr, daddr, sport, dport, count, 1) + data)
		& COOKIEMASK));
}

static __u32 check_tcp_syn_cookie(__u32 cookie, struct in6_addr *saddr,
				  struct in6_addr *daddr, __be16 sport,
				  __be16 dport, __u32 sseq, __u32 count,
				  __u32 maxdiff)
{
	__u32 diff;

	cookie -= cookie_hash(saddr, daddr, sport, dport, 0, 0) + sseq;

	diff = (count - (cookie >> COOKIEBITS)) & ((__u32) -1 >> COOKIEBITS);
	if (diff >= maxdiff)
		return (__u32)-1;

	return (cookie -
		cookie_hash(saddr, daddr, sport, dport, count - diff, 1))
		& COOKIEMASK;
}

__u32 cookie_v6_init_sequence(struct sock *sk, struct sk_buff *skb, __u16 *mssp)
{
	struct ipv6hdr *iph = ipv6_hdr(skb);
	const struct tcphdr *th = tcp_hdr(skb);
	int mssind;
	const __u16 mss = *mssp;

	tcp_synq_overflow(sk);

	for (mssind = 0; mss > msstab[mssind + 1]; mssind++)
		;
	*mssp = msstab[mssind] + 1;

	NET_INC_STATS_BH(sock_net(sk), LINUX_MIB_SYNCOOKIESSENT);

	return secure_tcp_syn_cookie(&iph->saddr, &iph->daddr, th->source,
				     th->dest, ntohl(th->seq),
				     jiffies / (HZ * 60), mssind);
}

static inline int cookie_check(struct sk_buff *skb, __u32 cookie)
{
	struct ipv6hdr *iph = ipv6_hdr(skb);
	const struct tcphdr *th = tcp_hdr(skb);
	__u32 seq = ntohl(th->seq) - 1;
	__u32 mssind = check_tcp_syn_cookie(cookie, &iph->saddr, &iph->daddr,
					    th->source, th->dest, seq,
					    jiffies / (HZ * 60), COUNTER_TRIES);

	return mssind < NUM_MSS ? msstab[mssind] + 1 : 0;
}

struct sock *cookie_v6_check(struct sock *sk, struct sk_buff *skb)
{
	struct inet_request_sock *ireq;
	struct inet6_request_sock *ireq6;
	struct tcp_request_sock *treq;
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct tcp_sock *tp = tcp_sk(sk);
	const struct tcphdr *th = tcp_hdr(skb);
	__u32 cookie = ntohl(th->ack_seq) - 1;
	struct sock *ret = sk;
	struct request_sock *req;
	int mss;
	struct dst_entry *dst;
	__u8 rcv_wscale;
	struct tcp_options_received tcp_opt;

	if (!sysctl_tcp_syncookies || !th->ack)
		goto out;

	if (tcp_synq_no_recent_overflow(sk) ||
		(mss = cookie_check(skb, cookie)) == 0) {
		NET_INC_STATS_BH(sock_net(sk), LINUX_MIB_SYNCOOKIESFAILED);
		goto out;
	}

	NET_INC_STATS_BH(sock_net(sk), LINUX_MIB_SYNCOOKIESRECV);

	/* check for timestamp cookie support */
	memset(&tcp_opt, 0, sizeof(tcp_opt));
	tcp_parse_options(skb, &tcp_opt, 0);

	if (tcp_opt.saw_tstamp)
		cookie_check_timestamp(&tcp_opt);

	ret = NULL;
	req = inet6_reqsk_alloc(&tcp6_request_sock_ops);
	if (!req)
		goto out;

	ireq = inet_rsk(req);
	ireq6 = inet6_rsk(req);
	treq = tcp_rsk(req);

	if (security_inet_conn_request(sk, skb, req))
		goto out_free;

	req->mss = mss;
	ireq->rmt_port = th->source;
	ireq->loc_port = th->dest;
	ipv6_addr_copy(&ireq6->rmt_addr, &ipv6_hdr(skb)->saddr);
	ipv6_addr_copy(&ireq6->loc_addr, &ipv6_hdr(skb)->daddr);
	if (ipv6_opt_accepted(sk, skb) ||
	    np->rxopt.bits.rxinfo || np->rxopt.bits.rxoinfo ||
	    np->rxopt.bits.rxhlim || np->rxopt.bits.rxohlim) {
		atomic_inc(&skb->users);
		ireq6->pktopts = skb;
	}

	ireq6->iif = sk->sk_bound_dev_if;
	/* So that link locals have meaning */
	if (!sk->sk_bound_dev_if &&
	    ipv6_addr_type(&ireq6->rmt_addr) & IPV6_ADDR_LINKLOCAL)
		ireq6->iif = inet6_iif(skb);

	req->expires = 0UL;
	req->retrans = 0;
	ireq->ecn_ok		= 0;
	ireq->snd_wscale	= tcp_opt.snd_wscale;
	ireq->rcv_wscale	= tcp_opt.rcv_wscale;
	ireq->sack_ok		= tcp_opt.sack_ok;
	ireq->wscale_ok		= tcp_opt.wscale_ok;
	ireq->tstamp_ok		= tcp_opt.saw_tstamp;
	req->ts_recent		= tcp_opt.saw_tstamp ? tcp_opt.rcv_tsval : 0;
	treq->rcv_isn = ntohl(th->seq) - 1;
	treq->snt_isn = cookie;

	/*
	 * We need to lookup the dst_entry to get the correct window size.
	 * This is taken from tcp_v6_syn_recv_sock.  Somebody please enlighten
	 * me if there is a preferred way.
	 */
	{
		struct in6_addr *final_p = NULL, final;
		struct flowi fl;
		memset(&fl, 0, sizeof(fl));
		fl.proto = IPPROTO_TCP;
		ipv6_addr_copy(&fl.fl6_dst, &ireq6->rmt_addr);
		if (np->opt && np->opt->srcrt) {
			struct rt0_hdr *rt0 = (struct rt0_hdr *) np->opt->srcrt;
			ipv6_addr_copy(&final, &fl.fl6_dst);
			ipv6_addr_copy(&fl.fl6_dst, rt0->addr);
			final_p = &final;
		}
		ipv6_addr_copy(&fl.fl6_src, &ireq6->loc_addr);
		fl.oif = sk->sk_bound_dev_if;
		fl.fl_ip_dport = inet_rsk(req)->rmt_port;
		fl.fl_ip_sport = inet_sk(sk)->sport;
		security_req_classify_flow(req, &fl);
		if (ip6_dst_lookup(sk, &dst, &fl))
			goto out_free;

		if (final_p)
			ipv6_addr_copy(&fl.fl6_dst, final_p);
		if ((xfrm_lookup(sock_net(sk), &dst, &fl, sk, 0)) < 0)
			goto out_free;
	}

	req->window_clamp = tp->window_clamp ? :dst_metric(dst, RTAX_WINDOW);
	tcp_select_initial_window(tcp_full_space(sk), req->mss,
				  &req->rcv_wnd, &req->window_clamp,
				  ireq->wscale_ok, &rcv_wscale);

	ireq->rcv_wscale = rcv_wscale;

	ret = get_cookie_sock(sk, skb, req, dst);
out:
	return ret;
out_free:
	reqsk_free(req);
	return NULL;
}

