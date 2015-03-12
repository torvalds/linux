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

#define COOKIEBITS 24	/* Upper bits store count */
#define COOKIEMASK (((__u32)1 << COOKIEBITS) - 1)

static u32 syncookie6_secret[2][16-4+SHA_DIGEST_WORDS] __read_mostly;

/* RFC 2460, Section 8.3:
 * [ipv6 tcp] MSS must be computed as the maximum packet size minus 60 [..]
 *
 * Due to IPV6_MIN_MTU=1280 the lowest possible MSS is 1220, which allows
 * using higher values than ipv4 tcp syncookies.
 * The other values are chosen based on ethernet (1500 and 9k MTU), plus
 * one that accounts for common encap (PPPoe) overhead. Table must be sorted.
 */
static __u16 const msstab[] = {
	1280 - 60, /* IPV6_MIN_MTU - 60 */
	1480 - 60,
	1500 - 60,
	9000 - 60,
};

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

static DEFINE_PER_CPU(__u32 [16 + 5 + SHA_WORKSPACE_WORDS],
		      ipv6_cookie_scratch);

static u32 cookie_hash(const struct in6_addr *saddr, const struct in6_addr *daddr,
		       __be16 sport, __be16 dport, u32 count, int c)
{
	__u32 *tmp;

	net_get_random_once(syncookie6_secret, sizeof(syncookie6_secret));

	tmp  = this_cpu_ptr(ipv6_cookie_scratch);

	/*
	 * we have 320 bits of information to hash, copy in the remaining
	 * 192 bits required for sha_transform, from the syncookie6_secret
	 * and overwrite the digest with the secret
	 */
	memcpy(tmp + 10, syncookie6_secret[c], 44);
	memcpy(tmp, saddr, 16);
	memcpy(tmp + 4, daddr, 16);
	tmp[8] = ((__force u32)sport << 16) + (__force u32)dport;
	tmp[9] = count;
	sha_transform(tmp + 16, (__u8 *)tmp, tmp + 16 + 5);

	return tmp[17];
}

static __u32 secure_tcp_syn_cookie(const struct in6_addr *saddr,
				   const struct in6_addr *daddr,
				   __be16 sport, __be16 dport, __u32 sseq,
				   __u32 data)
{
	u32 count = tcp_cookie_time();
	return (cookie_hash(saddr, daddr, sport, dport, 0, 0) +
		sseq + (count << COOKIEBITS) +
		((cookie_hash(saddr, daddr, sport, dport, count, 1) + data)
		& COOKIEMASK));
}

static __u32 check_tcp_syn_cookie(__u32 cookie, const struct in6_addr *saddr,
				  const struct in6_addr *daddr, __be16 sport,
				  __be16 dport, __u32 sseq)
{
	__u32 diff, count = tcp_cookie_time();

	cookie -= cookie_hash(saddr, daddr, sport, dport, 0, 0) + sseq;

	diff = (count - (cookie >> COOKIEBITS)) & ((__u32) -1 >> COOKIEBITS);
	if (diff >= MAX_SYNCOOKIE_AGE)
		return (__u32)-1;

	return (cookie -
		cookie_hash(saddr, daddr, sport, dport, count - diff, 1))
		& COOKIEMASK;
}

u32 __cookie_v6_init_sequence(const struct ipv6hdr *iph,
			      const struct tcphdr *th, __u16 *mssp)
{
	int mssind;
	const __u16 mss = *mssp;

	for (mssind = ARRAY_SIZE(msstab) - 1; mssind ; mssind--)
		if (mss >= msstab[mssind])
			break;

	*mssp = msstab[mssind];

	return secure_tcp_syn_cookie(&iph->saddr, &iph->daddr, th->source,
				     th->dest, ntohl(th->seq), mssind);
}
EXPORT_SYMBOL_GPL(__cookie_v6_init_sequence);

__u32 cookie_v6_init_sequence(struct sock *sk, const struct sk_buff *skb, __u16 *mssp)
{
	const struct ipv6hdr *iph = ipv6_hdr(skb);
	const struct tcphdr *th = tcp_hdr(skb);

	tcp_synq_overflow(sk);
	NET_INC_STATS_BH(sock_net(sk), LINUX_MIB_SYNCOOKIESSENT);

	return __cookie_v6_init_sequence(iph, th, mssp);
}

int __cookie_v6_check(const struct ipv6hdr *iph, const struct tcphdr *th,
		      __u32 cookie)
{
	__u32 seq = ntohl(th->seq) - 1;
	__u32 mssind = check_tcp_syn_cookie(cookie, &iph->saddr, &iph->daddr,
					    th->source, th->dest, seq);

	return mssind < ARRAY_SIZE(msstab) ? msstab[mssind] : 0;
}
EXPORT_SYMBOL_GPL(__cookie_v6_check);

struct sock *cookie_v6_check(struct sock *sk, struct sk_buff *skb)
{
	struct tcp_options_received tcp_opt;
	struct inet_request_sock *ireq;
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

	if (!sysctl_tcp_syncookies || !th->ack || th->rst)
		goto out;

	if (tcp_synq_no_recent_overflow(sk))
		goto out;

	mss = __cookie_v6_check(ipv6_hdr(skb), th, cookie);
	if (mss == 0) {
		NET_INC_STATS_BH(sock_net(sk), LINUX_MIB_SYNCOOKIESFAILED);
		goto out;
	}

	NET_INC_STATS_BH(sock_net(sk), LINUX_MIB_SYNCOOKIESRECV);

	/* check for timestamp cookie support */
	memset(&tcp_opt, 0, sizeof(tcp_opt));
	tcp_parse_options(skb, &tcp_opt, 0, NULL);

	if (!cookie_timestamp_decode(&tcp_opt))
		goto out;

	ret = NULL;
	req = inet_reqsk_alloc(&tcp6_request_sock_ops);
	if (!req)
		goto out;

	ireq = inet_rsk(req);
	treq = tcp_rsk(req);
	treq->listener = NULL;
	write_pnet(&ireq->ireq_net, sock_net(sk));
	ireq->ireq_family = AF_INET6;

	if (security_inet_conn_request(sk, skb, req))
		goto out_free;

	req->mss = mss;
	ireq->ir_rmt_port = th->source;
	ireq->ir_num = ntohs(th->dest);
	ireq->ir_v6_rmt_addr = ipv6_hdr(skb)->saddr;
	ireq->ir_v6_loc_addr = ipv6_hdr(skb)->daddr;
	if (ipv6_opt_accepted(sk, skb, &TCP_SKB_CB(skb)->header.h6) ||
	    np->rxopt.bits.rxinfo || np->rxopt.bits.rxoinfo ||
	    np->rxopt.bits.rxhlim || np->rxopt.bits.rxohlim) {
		atomic_inc(&skb->users);
		ireq->pktopts = skb;
	}

	ireq->ir_iif = sk->sk_bound_dev_if;
	/* So that link locals have meaning */
	if (!sk->sk_bound_dev_if &&
	    ipv6_addr_type(&ireq->ir_v6_rmt_addr) & IPV6_ADDR_LINKLOCAL)
		ireq->ir_iif = tcp_v6_iif(skb);

	ireq->ir_mark = inet_request_mark(sk, skb);

	req->expires = 0UL;
	req->num_retrans = 0;
	ireq->snd_wscale	= tcp_opt.snd_wscale;
	ireq->sack_ok		= tcp_opt.sack_ok;
	ireq->wscale_ok		= tcp_opt.wscale_ok;
	ireq->tstamp_ok		= tcp_opt.saw_tstamp;
	req->ts_recent		= tcp_opt.saw_tstamp ? tcp_opt.rcv_tsval : 0;
	treq->snt_synack	= tcp_opt.saw_tstamp ? tcp_opt.rcv_tsecr : 0;
	treq->rcv_isn = ntohl(th->seq) - 1;
	treq->snt_isn = cookie;

	/*
	 * We need to lookup the dst_entry to get the correct window size.
	 * This is taken from tcp_v6_syn_recv_sock.  Somebody please enlighten
	 * me if there is a preferred way.
	 */
	{
		struct in6_addr *final_p, final;
		struct flowi6 fl6;
		memset(&fl6, 0, sizeof(fl6));
		fl6.flowi6_proto = IPPROTO_TCP;
		fl6.daddr = ireq->ir_v6_rmt_addr;
		final_p = fl6_update_dst(&fl6, np->opt, &final);
		fl6.saddr = ireq->ir_v6_loc_addr;
		fl6.flowi6_oif = sk->sk_bound_dev_if;
		fl6.flowi6_mark = ireq->ir_mark;
		fl6.fl6_dport = ireq->ir_rmt_port;
		fl6.fl6_sport = inet_sk(sk)->inet_sport;
		security_req_classify_flow(req, flowi6_to_flowi(&fl6));

		dst = ip6_dst_lookup_flow(sk, &fl6, final_p);
		if (IS_ERR(dst))
			goto out_free;
	}

	req->window_clamp = tp->window_clamp ? :dst_metric(dst, RTAX_WINDOW);
	tcp_select_initial_window(tcp_full_space(sk), req->mss,
				  &req->rcv_wnd, &req->window_clamp,
				  ireq->wscale_ok, &rcv_wscale,
				  dst_metric(dst, RTAX_INITRWND));

	ireq->rcv_wscale = rcv_wscale;
	ireq->ecn_ok = cookie_ecn_ok(&tcp_opt, sock_net(sk), dst);

	ret = get_cookie_sock(sk, skb, req, dst);
out:
	return ret;
out_free:
	reqsk_free(req);
	return NULL;
}
