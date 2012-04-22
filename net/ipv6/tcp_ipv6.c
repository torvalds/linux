/*
 *	TCP over IPv6
 *	Linux INET6 implementation
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>
 *
 *	Based on:
 *	linux/net/ipv4/tcp.c
 *	linux/net/ipv4/tcp_input.c
 *	linux/net/ipv4/tcp_output.c
 *
 *	Fixes:
 *	Hideaki YOSHIFUJI	:	sin6_scope_id support
 *	YOSHIFUJI Hideaki @USAGI and:	Support IPV6_V6ONLY socket option, which
 *	Alexey Kuznetsov		allow both IPv4 and IPv6 sockets to bind
 *					a single port at the same time.
 *	YOSHIFUJI Hideaki @USAGI:	convert /proc/net/tcp6 to seq_file.
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/bottom_half.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/jiffies.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <linux/netdevice.h>
#include <linux/init.h>
#include <linux/jhash.h>
#include <linux/ipsec.h>
#include <linux/times.h>
#include <linux/slab.h>

#include <linux/ipv6.h>
#include <linux/icmpv6.h>
#include <linux/random.h>

#include <net/tcp.h>
#include <net/ndisc.h>
#include <net/inet6_hashtables.h>
#include <net/inet6_connection_sock.h>
#include <net/ipv6.h>
#include <net/transp_v6.h>
#include <net/addrconf.h>
#include <net/ip6_route.h>
#include <net/ip6_checksum.h>
#include <net/inet_ecn.h>
#include <net/protocol.h>
#include <net/xfrm.h>
#include <net/snmp.h>
#include <net/dsfield.h>
#include <net/timewait_sock.h>
#include <net/netdma.h>
#include <net/inet_common.h>
#include <net/secure_seq.h>
#include <net/tcp_memcontrol.h>

#include <asm/uaccess.h>

#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include <linux/crypto.h>
#include <linux/scatterlist.h>

static void	tcp_v6_send_reset(struct sock *sk, struct sk_buff *skb);
static void	tcp_v6_reqsk_send_ack(struct sock *sk, struct sk_buff *skb,
				      struct request_sock *req);

static int	tcp_v6_do_rcv(struct sock *sk, struct sk_buff *skb);
static void	__tcp_v6_send_check(struct sk_buff *skb,
				    const struct in6_addr *saddr,
				    const struct in6_addr *daddr);

static const struct inet_connection_sock_af_ops ipv6_mapped;
static const struct inet_connection_sock_af_ops ipv6_specific;
#ifdef CONFIG_TCP_MD5SIG
static const struct tcp_sock_af_ops tcp_sock_ipv6_specific;
static const struct tcp_sock_af_ops tcp_sock_ipv6_mapped_specific;
#else
static struct tcp_md5sig_key *tcp_v6_md5_do_lookup(struct sock *sk,
						   const struct in6_addr *addr)
{
	return NULL;
}
#endif

static void tcp_v6_hash(struct sock *sk)
{
	if (sk->sk_state != TCP_CLOSE) {
		if (inet_csk(sk)->icsk_af_ops == &ipv6_mapped) {
			tcp_prot.hash(sk);
			return;
		}
		local_bh_disable();
		__inet6_hash(sk, NULL);
		local_bh_enable();
	}
}

static __inline__ __sum16 tcp_v6_check(int len,
				   const struct in6_addr *saddr,
				   const struct in6_addr *daddr,
				   __wsum base)
{
	return csum_ipv6_magic(saddr, daddr, len, IPPROTO_TCP, base);
}

static __u32 tcp_v6_init_sequence(const struct sk_buff *skb)
{
	return secure_tcpv6_sequence_number(ipv6_hdr(skb)->daddr.s6_addr32,
					    ipv6_hdr(skb)->saddr.s6_addr32,
					    tcp_hdr(skb)->dest,
					    tcp_hdr(skb)->source);
}

static int tcp_v6_connect(struct sock *sk, struct sockaddr *uaddr,
			  int addr_len)
{
	struct sockaddr_in6 *usin = (struct sockaddr_in6 *) uaddr;
	struct inet_sock *inet = inet_sk(sk);
	struct inet_connection_sock *icsk = inet_csk(sk);
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct tcp_sock *tp = tcp_sk(sk);
	struct in6_addr *saddr = NULL, *final_p, final;
	struct rt6_info *rt;
	struct flowi6 fl6;
	struct dst_entry *dst;
	int addr_type;
	int err;

	if (addr_len < SIN6_LEN_RFC2133)
		return -EINVAL;

	if (usin->sin6_family != AF_INET6)
		return -EAFNOSUPPORT;

	memset(&fl6, 0, sizeof(fl6));

	if (np->sndflow) {
		fl6.flowlabel = usin->sin6_flowinfo&IPV6_FLOWINFO_MASK;
		IP6_ECN_flow_init(fl6.flowlabel);
		if (fl6.flowlabel&IPV6_FLOWLABEL_MASK) {
			struct ip6_flowlabel *flowlabel;
			flowlabel = fl6_sock_lookup(sk, fl6.flowlabel);
			if (flowlabel == NULL)
				return -EINVAL;
			usin->sin6_addr = flowlabel->dst;
			fl6_sock_release(flowlabel);
		}
	}

	/*
	 *	connect() to INADDR_ANY means loopback (BSD'ism).
	 */

	if(ipv6_addr_any(&usin->sin6_addr))
		usin->sin6_addr.s6_addr[15] = 0x1;

	addr_type = ipv6_addr_type(&usin->sin6_addr);

	if(addr_type & IPV6_ADDR_MULTICAST)
		return -ENETUNREACH;

	if (addr_type&IPV6_ADDR_LINKLOCAL) {
		if (addr_len >= sizeof(struct sockaddr_in6) &&
		    usin->sin6_scope_id) {
			/* If interface is set while binding, indices
			 * must coincide.
			 */
			if (sk->sk_bound_dev_if &&
			    sk->sk_bound_dev_if != usin->sin6_scope_id)
				return -EINVAL;

			sk->sk_bound_dev_if = usin->sin6_scope_id;
		}

		/* Connect to link-local address requires an interface */
		if (!sk->sk_bound_dev_if)
			return -EINVAL;
	}

	if (tp->rx_opt.ts_recent_stamp &&
	    !ipv6_addr_equal(&np->daddr, &usin->sin6_addr)) {
		tp->rx_opt.ts_recent = 0;
		tp->rx_opt.ts_recent_stamp = 0;
		tp->write_seq = 0;
	}

	np->daddr = usin->sin6_addr;
	np->flow_label = fl6.flowlabel;

	/*
	 *	TCP over IPv4
	 */

	if (addr_type == IPV6_ADDR_MAPPED) {
		u32 exthdrlen = icsk->icsk_ext_hdr_len;
		struct sockaddr_in sin;

		SOCK_DEBUG(sk, "connect: ipv4 mapped\n");

		if (__ipv6_only_sock(sk))
			return -ENETUNREACH;

		sin.sin_family = AF_INET;
		sin.sin_port = usin->sin6_port;
		sin.sin_addr.s_addr = usin->sin6_addr.s6_addr32[3];

		icsk->icsk_af_ops = &ipv6_mapped;
		sk->sk_backlog_rcv = tcp_v4_do_rcv;
#ifdef CONFIG_TCP_MD5SIG
		tp->af_specific = &tcp_sock_ipv6_mapped_specific;
#endif

		err = tcp_v4_connect(sk, (struct sockaddr *)&sin, sizeof(sin));

		if (err) {
			icsk->icsk_ext_hdr_len = exthdrlen;
			icsk->icsk_af_ops = &ipv6_specific;
			sk->sk_backlog_rcv = tcp_v6_do_rcv;
#ifdef CONFIG_TCP_MD5SIG
			tp->af_specific = &tcp_sock_ipv6_specific;
#endif
			goto failure;
		} else {
			ipv6_addr_set_v4mapped(inet->inet_saddr, &np->saddr);
			ipv6_addr_set_v4mapped(inet->inet_rcv_saddr,
					       &np->rcv_saddr);
		}

		return err;
	}

	if (!ipv6_addr_any(&np->rcv_saddr))
		saddr = &np->rcv_saddr;

	fl6.flowi6_proto = IPPROTO_TCP;
	fl6.daddr = np->daddr;
	fl6.saddr = saddr ? *saddr : np->saddr;
	fl6.flowi6_oif = sk->sk_bound_dev_if;
	fl6.flowi6_mark = sk->sk_mark;
	fl6.fl6_dport = usin->sin6_port;
	fl6.fl6_sport = inet->inet_sport;

	final_p = fl6_update_dst(&fl6, np->opt, &final);

	security_sk_classify_flow(sk, flowi6_to_flowi(&fl6));

	dst = ip6_dst_lookup_flow(sk, &fl6, final_p, true);
	if (IS_ERR(dst)) {
		err = PTR_ERR(dst);
		goto failure;
	}

	if (saddr == NULL) {
		saddr = &fl6.saddr;
		np->rcv_saddr = *saddr;
	}

	/* set the source address */
	np->saddr = *saddr;
	inet->inet_rcv_saddr = LOOPBACK4_IPV6;

	sk->sk_gso_type = SKB_GSO_TCPV6;
	__ip6_dst_store(sk, dst, NULL, NULL);

	rt = (struct rt6_info *) dst;
	if (tcp_death_row.sysctl_tw_recycle &&
	    !tp->rx_opt.ts_recent_stamp &&
	    ipv6_addr_equal(&rt->rt6i_dst.addr, &np->daddr)) {
		struct inet_peer *peer = rt6_get_peer(rt);
		/*
		 * VJ's idea. We save last timestamp seen from
		 * the destination in peer table, when entering state
		 * TIME-WAIT * and initialize rx_opt.ts_recent from it,
		 * when trying new connection.
		 */
		if (peer) {
			inet_peer_refcheck(peer);
			if ((u32)get_seconds() - peer->tcp_ts_stamp <= TCP_PAWS_MSL) {
				tp->rx_opt.ts_recent_stamp = peer->tcp_ts_stamp;
				tp->rx_opt.ts_recent = peer->tcp_ts;
			}
		}
	}

	icsk->icsk_ext_hdr_len = 0;
	if (np->opt)
		icsk->icsk_ext_hdr_len = (np->opt->opt_flen +
					  np->opt->opt_nflen);

	tp->rx_opt.mss_clamp = IPV6_MIN_MTU - sizeof(struct tcphdr) - sizeof(struct ipv6hdr);

	inet->inet_dport = usin->sin6_port;

	tcp_set_state(sk, TCP_SYN_SENT);
	err = inet6_hash_connect(&tcp_death_row, sk);
	if (err)
		goto late_failure;

	if (!tp->write_seq)
		tp->write_seq = secure_tcpv6_sequence_number(np->saddr.s6_addr32,
							     np->daddr.s6_addr32,
							     inet->inet_sport,
							     inet->inet_dport);

	err = tcp_connect(sk);
	if (err)
		goto late_failure;

	return 0;

late_failure:
	tcp_set_state(sk, TCP_CLOSE);
	__sk_dst_reset(sk);
failure:
	inet->inet_dport = 0;
	sk->sk_route_caps = 0;
	return err;
}

static void tcp_v6_err(struct sk_buff *skb, struct inet6_skb_parm *opt,
		u8 type, u8 code, int offset, __be32 info)
{
	const struct ipv6hdr *hdr = (const struct ipv6hdr*)skb->data;
	const struct tcphdr *th = (struct tcphdr *)(skb->data+offset);
	struct ipv6_pinfo *np;
	struct sock *sk;
	int err;
	struct tcp_sock *tp;
	__u32 seq;
	struct net *net = dev_net(skb->dev);

	sk = inet6_lookup(net, &tcp_hashinfo, &hdr->daddr,
			th->dest, &hdr->saddr, th->source, skb->dev->ifindex);

	if (sk == NULL) {
		ICMP6_INC_STATS_BH(net, __in6_dev_get(skb->dev),
				   ICMP6_MIB_INERRORS);
		return;
	}

	if (sk->sk_state == TCP_TIME_WAIT) {
		inet_twsk_put(inet_twsk(sk));
		return;
	}

	bh_lock_sock(sk);
	if (sock_owned_by_user(sk))
		NET_INC_STATS_BH(net, LINUX_MIB_LOCKDROPPEDICMPS);

	if (sk->sk_state == TCP_CLOSE)
		goto out;

	if (ipv6_hdr(skb)->hop_limit < inet6_sk(sk)->min_hopcount) {
		NET_INC_STATS_BH(net, LINUX_MIB_TCPMINTTLDROP);
		goto out;
	}

	tp = tcp_sk(sk);
	seq = ntohl(th->seq);
	if (sk->sk_state != TCP_LISTEN &&
	    !between(seq, tp->snd_una, tp->snd_nxt)) {
		NET_INC_STATS_BH(net, LINUX_MIB_OUTOFWINDOWICMPS);
		goto out;
	}

	np = inet6_sk(sk);

	if (type == ICMPV6_PKT_TOOBIG) {
		struct dst_entry *dst;

		if (sock_owned_by_user(sk))
			goto out;
		if ((1 << sk->sk_state) & (TCPF_LISTEN | TCPF_CLOSE))
			goto out;

		/* icmp should have updated the destination cache entry */
		dst = __sk_dst_check(sk, np->dst_cookie);

		if (dst == NULL) {
			struct inet_sock *inet = inet_sk(sk);
			struct flowi6 fl6;

			/* BUGGG_FUTURE: Again, it is not clear how
			   to handle rthdr case. Ignore this complexity
			   for now.
			 */
			memset(&fl6, 0, sizeof(fl6));
			fl6.flowi6_proto = IPPROTO_TCP;
			fl6.daddr = np->daddr;
			fl6.saddr = np->saddr;
			fl6.flowi6_oif = sk->sk_bound_dev_if;
			fl6.flowi6_mark = sk->sk_mark;
			fl6.fl6_dport = inet->inet_dport;
			fl6.fl6_sport = inet->inet_sport;
			security_skb_classify_flow(skb, flowi6_to_flowi(&fl6));

			dst = ip6_dst_lookup_flow(sk, &fl6, NULL, false);
			if (IS_ERR(dst)) {
				sk->sk_err_soft = -PTR_ERR(dst);
				goto out;
			}

		} else
			dst_hold(dst);

		if (inet_csk(sk)->icsk_pmtu_cookie > dst_mtu(dst)) {
			tcp_sync_mss(sk, dst_mtu(dst));
			tcp_simple_retransmit(sk);
		} /* else let the usual retransmit timer handle it */
		dst_release(dst);
		goto out;
	}

	icmpv6_err_convert(type, code, &err);

	/* Might be for an request_sock */
	switch (sk->sk_state) {
		struct request_sock *req, **prev;
	case TCP_LISTEN:
		if (sock_owned_by_user(sk))
			goto out;

		req = inet6_csk_search_req(sk, &prev, th->dest, &hdr->daddr,
					   &hdr->saddr, inet6_iif(skb));
		if (!req)
			goto out;

		/* ICMPs are not backlogged, hence we cannot get
		 * an established socket here.
		 */
		WARN_ON(req->sk != NULL);

		if (seq != tcp_rsk(req)->snt_isn) {
			NET_INC_STATS_BH(net, LINUX_MIB_OUTOFWINDOWICMPS);
			goto out;
		}

		inet_csk_reqsk_queue_drop(sk, req, prev);
		goto out;

	case TCP_SYN_SENT:
	case TCP_SYN_RECV:  /* Cannot happen.
			       It can, it SYNs are crossed. --ANK */
		if (!sock_owned_by_user(sk)) {
			sk->sk_err = err;
			sk->sk_error_report(sk);		/* Wake people up to see the error (see connect in sock.c) */

			tcp_done(sk);
		} else
			sk->sk_err_soft = err;
		goto out;
	}

	if (!sock_owned_by_user(sk) && np->recverr) {
		sk->sk_err = err;
		sk->sk_error_report(sk);
	} else
		sk->sk_err_soft = err;

out:
	bh_unlock_sock(sk);
	sock_put(sk);
}


static int tcp_v6_send_synack(struct sock *sk, struct request_sock *req,
			      struct request_values *rvp)
{
	struct inet6_request_sock *treq = inet6_rsk(req);
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct sk_buff * skb;
	struct ipv6_txoptions *opt = NULL;
	struct in6_addr * final_p, final;
	struct flowi6 fl6;
	struct dst_entry *dst;
	int err;

	memset(&fl6, 0, sizeof(fl6));
	fl6.flowi6_proto = IPPROTO_TCP;
	fl6.daddr = treq->rmt_addr;
	fl6.saddr = treq->loc_addr;
	fl6.flowlabel = 0;
	fl6.flowi6_oif = treq->iif;
	fl6.flowi6_mark = sk->sk_mark;
	fl6.fl6_dport = inet_rsk(req)->rmt_port;
	fl6.fl6_sport = inet_rsk(req)->loc_port;
	security_req_classify_flow(req, flowi6_to_flowi(&fl6));

	opt = np->opt;
	final_p = fl6_update_dst(&fl6, opt, &final);

	dst = ip6_dst_lookup_flow(sk, &fl6, final_p, false);
	if (IS_ERR(dst)) {
		err = PTR_ERR(dst);
		dst = NULL;
		goto done;
	}
	skb = tcp_make_synack(sk, dst, req, rvp);
	err = -ENOMEM;
	if (skb) {
		__tcp_v6_send_check(skb, &treq->loc_addr, &treq->rmt_addr);

		fl6.daddr = treq->rmt_addr;
		err = ip6_xmit(sk, skb, &fl6, opt, np->tclass);
		err = net_xmit_eval(err);
	}

done:
	if (opt && opt != np->opt)
		sock_kfree_s(sk, opt, opt->tot_len);
	dst_release(dst);
	return err;
}

static int tcp_v6_rtx_synack(struct sock *sk, struct request_sock *req,
			     struct request_values *rvp)
{
	TCP_INC_STATS_BH(sock_net(sk), TCP_MIB_RETRANSSEGS);
	return tcp_v6_send_synack(sk, req, rvp);
}

static void tcp_v6_reqsk_destructor(struct request_sock *req)
{
	kfree_skb(inet6_rsk(req)->pktopts);
}

#ifdef CONFIG_TCP_MD5SIG
static struct tcp_md5sig_key *tcp_v6_md5_do_lookup(struct sock *sk,
						   const struct in6_addr *addr)
{
	return tcp_md5_do_lookup(sk, (union tcp_md5_addr *)addr, AF_INET6);
}

static struct tcp_md5sig_key *tcp_v6_md5_lookup(struct sock *sk,
						struct sock *addr_sk)
{
	return tcp_v6_md5_do_lookup(sk, &inet6_sk(addr_sk)->daddr);
}

static struct tcp_md5sig_key *tcp_v6_reqsk_md5_lookup(struct sock *sk,
						      struct request_sock *req)
{
	return tcp_v6_md5_do_lookup(sk, &inet6_rsk(req)->rmt_addr);
}

static int tcp_v6_parse_md5_keys (struct sock *sk, char __user *optval,
				  int optlen)
{
	struct tcp_md5sig cmd;
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)&cmd.tcpm_addr;

	if (optlen < sizeof(cmd))
		return -EINVAL;

	if (copy_from_user(&cmd, optval, sizeof(cmd)))
		return -EFAULT;

	if (sin6->sin6_family != AF_INET6)
		return -EINVAL;

	if (!cmd.tcpm_keylen) {
		if (ipv6_addr_v4mapped(&sin6->sin6_addr))
			return tcp_md5_do_del(sk, (union tcp_md5_addr *)&sin6->sin6_addr.s6_addr32[3],
					      AF_INET);
		return tcp_md5_do_del(sk, (union tcp_md5_addr *)&sin6->sin6_addr,
				      AF_INET6);
	}

	if (cmd.tcpm_keylen > TCP_MD5SIG_MAXKEYLEN)
		return -EINVAL;

	if (ipv6_addr_v4mapped(&sin6->sin6_addr))
		return tcp_md5_do_add(sk, (union tcp_md5_addr *)&sin6->sin6_addr.s6_addr32[3],
				      AF_INET, cmd.tcpm_key, cmd.tcpm_keylen, GFP_KERNEL);

	return tcp_md5_do_add(sk, (union tcp_md5_addr *)&sin6->sin6_addr,
			      AF_INET6, cmd.tcpm_key, cmd.tcpm_keylen, GFP_KERNEL);
}

static int tcp_v6_md5_hash_pseudoheader(struct tcp_md5sig_pool *hp,
					const struct in6_addr *daddr,
					const struct in6_addr *saddr, int nbytes)
{
	struct tcp6_pseudohdr *bp;
	struct scatterlist sg;

	bp = &hp->md5_blk.ip6;
	/* 1. TCP pseudo-header (RFC2460) */
	bp->saddr = *saddr;
	bp->daddr = *daddr;
	bp->protocol = cpu_to_be32(IPPROTO_TCP);
	bp->len = cpu_to_be32(nbytes);

	sg_init_one(&sg, bp, sizeof(*bp));
	return crypto_hash_update(&hp->md5_desc, &sg, sizeof(*bp));
}

static int tcp_v6_md5_hash_hdr(char *md5_hash, struct tcp_md5sig_key *key,
			       const struct in6_addr *daddr, struct in6_addr *saddr,
			       const struct tcphdr *th)
{
	struct tcp_md5sig_pool *hp;
	struct hash_desc *desc;

	hp = tcp_get_md5sig_pool();
	if (!hp)
		goto clear_hash_noput;
	desc = &hp->md5_desc;

	if (crypto_hash_init(desc))
		goto clear_hash;
	if (tcp_v6_md5_hash_pseudoheader(hp, daddr, saddr, th->doff << 2))
		goto clear_hash;
	if (tcp_md5_hash_header(hp, th))
		goto clear_hash;
	if (tcp_md5_hash_key(hp, key))
		goto clear_hash;
	if (crypto_hash_final(desc, md5_hash))
		goto clear_hash;

	tcp_put_md5sig_pool();
	return 0;

clear_hash:
	tcp_put_md5sig_pool();
clear_hash_noput:
	memset(md5_hash, 0, 16);
	return 1;
}

static int tcp_v6_md5_hash_skb(char *md5_hash, struct tcp_md5sig_key *key,
			       const struct sock *sk,
			       const struct request_sock *req,
			       const struct sk_buff *skb)
{
	const struct in6_addr *saddr, *daddr;
	struct tcp_md5sig_pool *hp;
	struct hash_desc *desc;
	const struct tcphdr *th = tcp_hdr(skb);

	if (sk) {
		saddr = &inet6_sk(sk)->saddr;
		daddr = &inet6_sk(sk)->daddr;
	} else if (req) {
		saddr = &inet6_rsk(req)->loc_addr;
		daddr = &inet6_rsk(req)->rmt_addr;
	} else {
		const struct ipv6hdr *ip6h = ipv6_hdr(skb);
		saddr = &ip6h->saddr;
		daddr = &ip6h->daddr;
	}

	hp = tcp_get_md5sig_pool();
	if (!hp)
		goto clear_hash_noput;
	desc = &hp->md5_desc;

	if (crypto_hash_init(desc))
		goto clear_hash;

	if (tcp_v6_md5_hash_pseudoheader(hp, daddr, saddr, skb->len))
		goto clear_hash;
	if (tcp_md5_hash_header(hp, th))
		goto clear_hash;
	if (tcp_md5_hash_skb_data(hp, skb, th->doff << 2))
		goto clear_hash;
	if (tcp_md5_hash_key(hp, key))
		goto clear_hash;
	if (crypto_hash_final(desc, md5_hash))
		goto clear_hash;

	tcp_put_md5sig_pool();
	return 0;

clear_hash:
	tcp_put_md5sig_pool();
clear_hash_noput:
	memset(md5_hash, 0, 16);
	return 1;
}

static int tcp_v6_inbound_md5_hash(struct sock *sk, const struct sk_buff *skb)
{
	const __u8 *hash_location = NULL;
	struct tcp_md5sig_key *hash_expected;
	const struct ipv6hdr *ip6h = ipv6_hdr(skb);
	const struct tcphdr *th = tcp_hdr(skb);
	int genhash;
	u8 newhash[16];

	hash_expected = tcp_v6_md5_do_lookup(sk, &ip6h->saddr);
	hash_location = tcp_parse_md5sig_option(th);

	/* We've parsed the options - do we have a hash? */
	if (!hash_expected && !hash_location)
		return 0;

	if (hash_expected && !hash_location) {
		NET_INC_STATS_BH(sock_net(sk), LINUX_MIB_TCPMD5NOTFOUND);
		return 1;
	}

	if (!hash_expected && hash_location) {
		NET_INC_STATS_BH(sock_net(sk), LINUX_MIB_TCPMD5UNEXPECTED);
		return 1;
	}

	/* check the signature */
	genhash = tcp_v6_md5_hash_skb(newhash,
				      hash_expected,
				      NULL, NULL, skb);

	if (genhash || memcmp(hash_location, newhash, 16) != 0) {
		if (net_ratelimit()) {
			printk(KERN_INFO "MD5 Hash %s for [%pI6c]:%u->[%pI6c]:%u\n",
			       genhash ? "failed" : "mismatch",
			       &ip6h->saddr, ntohs(th->source),
			       &ip6h->daddr, ntohs(th->dest));
		}
		return 1;
	}
	return 0;
}
#endif

struct request_sock_ops tcp6_request_sock_ops __read_mostly = {
	.family		=	AF_INET6,
	.obj_size	=	sizeof(struct tcp6_request_sock),
	.rtx_syn_ack	=	tcp_v6_rtx_synack,
	.send_ack	=	tcp_v6_reqsk_send_ack,
	.destructor	=	tcp_v6_reqsk_destructor,
	.send_reset	=	tcp_v6_send_reset,
	.syn_ack_timeout = 	tcp_syn_ack_timeout,
};

#ifdef CONFIG_TCP_MD5SIG
static const struct tcp_request_sock_ops tcp_request_sock_ipv6_ops = {
	.md5_lookup	=	tcp_v6_reqsk_md5_lookup,
	.calc_md5_hash	=	tcp_v6_md5_hash_skb,
};
#endif

static void __tcp_v6_send_check(struct sk_buff *skb,
				const struct in6_addr *saddr, const struct in6_addr *daddr)
{
	struct tcphdr *th = tcp_hdr(skb);

	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		th->check = ~tcp_v6_check(skb->len, saddr, daddr, 0);
		skb->csum_start = skb_transport_header(skb) - skb->head;
		skb->csum_offset = offsetof(struct tcphdr, check);
	} else {
		th->check = tcp_v6_check(skb->len, saddr, daddr,
					 csum_partial(th, th->doff << 2,
						      skb->csum));
	}
}

static void tcp_v6_send_check(struct sock *sk, struct sk_buff *skb)
{
	struct ipv6_pinfo *np = inet6_sk(sk);

	__tcp_v6_send_check(skb, &np->saddr, &np->daddr);
}

static int tcp_v6_gso_send_check(struct sk_buff *skb)
{
	const struct ipv6hdr *ipv6h;
	struct tcphdr *th;

	if (!pskb_may_pull(skb, sizeof(*th)))
		return -EINVAL;

	ipv6h = ipv6_hdr(skb);
	th = tcp_hdr(skb);

	th->check = 0;
	skb->ip_summed = CHECKSUM_PARTIAL;
	__tcp_v6_send_check(skb, &ipv6h->saddr, &ipv6h->daddr);
	return 0;
}

static struct sk_buff **tcp6_gro_receive(struct sk_buff **head,
					 struct sk_buff *skb)
{
	const struct ipv6hdr *iph = skb_gro_network_header(skb);

	switch (skb->ip_summed) {
	case CHECKSUM_COMPLETE:
		if (!tcp_v6_check(skb_gro_len(skb), &iph->saddr, &iph->daddr,
				  skb->csum)) {
			skb->ip_summed = CHECKSUM_UNNECESSARY;
			break;
		}

		/* fall through */
	case CHECKSUM_NONE:
		NAPI_GRO_CB(skb)->flush = 1;
		return NULL;
	}

	return tcp_gro_receive(head, skb);
}

static int tcp6_gro_complete(struct sk_buff *skb)
{
	const struct ipv6hdr *iph = ipv6_hdr(skb);
	struct tcphdr *th = tcp_hdr(skb);

	th->check = ~tcp_v6_check(skb->len - skb_transport_offset(skb),
				  &iph->saddr, &iph->daddr, 0);
	skb_shinfo(skb)->gso_type = SKB_GSO_TCPV6;

	return tcp_gro_complete(skb);
}

static void tcp_v6_send_response(struct sk_buff *skb, u32 seq, u32 ack, u32 win,
				 u32 ts, struct tcp_md5sig_key *key, int rst, u8 tclass)
{
	const struct tcphdr *th = tcp_hdr(skb);
	struct tcphdr *t1;
	struct sk_buff *buff;
	struct flowi6 fl6;
	struct net *net = dev_net(skb_dst(skb)->dev);
	struct sock *ctl_sk = net->ipv6.tcp_sk;
	unsigned int tot_len = sizeof(struct tcphdr);
	struct dst_entry *dst;
	__be32 *topt;

	if (ts)
		tot_len += TCPOLEN_TSTAMP_ALIGNED;
#ifdef CONFIG_TCP_MD5SIG
	if (key)
		tot_len += TCPOLEN_MD5SIG_ALIGNED;
#endif

	buff = alloc_skb(MAX_HEADER + sizeof(struct ipv6hdr) + tot_len,
			 GFP_ATOMIC);
	if (buff == NULL)
		return;

	skb_reserve(buff, MAX_HEADER + sizeof(struct ipv6hdr) + tot_len);

	t1 = (struct tcphdr *) skb_push(buff, tot_len);
	skb_reset_transport_header(buff);

	/* Swap the send and the receive. */
	memset(t1, 0, sizeof(*t1));
	t1->dest = th->source;
	t1->source = th->dest;
	t1->doff = tot_len / 4;
	t1->seq = htonl(seq);
	t1->ack_seq = htonl(ack);
	t1->ack = !rst || !th->ack;
	t1->rst = rst;
	t1->window = htons(win);

	topt = (__be32 *)(t1 + 1);

	if (ts) {
		*topt++ = htonl((TCPOPT_NOP << 24) | (TCPOPT_NOP << 16) |
				(TCPOPT_TIMESTAMP << 8) | TCPOLEN_TIMESTAMP);
		*topt++ = htonl(tcp_time_stamp);
		*topt++ = htonl(ts);
	}

#ifdef CONFIG_TCP_MD5SIG
	if (key) {
		*topt++ = htonl((TCPOPT_NOP << 24) | (TCPOPT_NOP << 16) |
				(TCPOPT_MD5SIG << 8) | TCPOLEN_MD5SIG);
		tcp_v6_md5_hash_hdr((__u8 *)topt, key,
				    &ipv6_hdr(skb)->saddr,
				    &ipv6_hdr(skb)->daddr, t1);
	}
#endif

	memset(&fl6, 0, sizeof(fl6));
	fl6.daddr = ipv6_hdr(skb)->saddr;
	fl6.saddr = ipv6_hdr(skb)->daddr;

	buff->ip_summed = CHECKSUM_PARTIAL;
	buff->csum = 0;

	__tcp_v6_send_check(buff, &fl6.saddr, &fl6.daddr);

	fl6.flowi6_proto = IPPROTO_TCP;
	fl6.flowi6_oif = inet6_iif(skb);
	fl6.fl6_dport = t1->dest;
	fl6.fl6_sport = t1->source;
	security_skb_classify_flow(skb, flowi6_to_flowi(&fl6));

	/* Pass a socket to ip6_dst_lookup either it is for RST
	 * Underlying function will use this to retrieve the network
	 * namespace
	 */
	dst = ip6_dst_lookup_flow(ctl_sk, &fl6, NULL, false);
	if (!IS_ERR(dst)) {
		skb_dst_set(buff, dst);
		ip6_xmit(ctl_sk, buff, &fl6, NULL, tclass);
		TCP_INC_STATS_BH(net, TCP_MIB_OUTSEGS);
		if (rst)
			TCP_INC_STATS_BH(net, TCP_MIB_OUTRSTS);
		return;
	}

	kfree_skb(buff);
}

static void tcp_v6_send_reset(struct sock *sk, struct sk_buff *skb)
{
	const struct tcphdr *th = tcp_hdr(skb);
	u32 seq = 0, ack_seq = 0;
	struct tcp_md5sig_key *key = NULL;
#ifdef CONFIG_TCP_MD5SIG
	const __u8 *hash_location = NULL;
	struct ipv6hdr *ipv6h = ipv6_hdr(skb);
	unsigned char newhash[16];
	int genhash;
	struct sock *sk1 = NULL;
#endif

	if (th->rst)
		return;

	if (!ipv6_unicast_destination(skb))
		return;

#ifdef CONFIG_TCP_MD5SIG
	hash_location = tcp_parse_md5sig_option(th);
	if (!sk && hash_location) {
		/*
		 * active side is lost. Try to find listening socket through
		 * source port, and then find md5 key through listening socket.
		 * we are not loose security here:
		 * Incoming packet is checked with md5 hash with finding key,
		 * no RST generated if md5 hash doesn't match.
		 */
		sk1 = inet6_lookup_listener(dev_net(skb_dst(skb)->dev),
					   &tcp_hashinfo, &ipv6h->daddr,
					   ntohs(th->source), inet6_iif(skb));
		if (!sk1)
			return;

		rcu_read_lock();
		key = tcp_v6_md5_do_lookup(sk1, &ipv6h->saddr);
		if (!key)
			goto release_sk1;

		genhash = tcp_v6_md5_hash_skb(newhash, key, NULL, NULL, skb);
		if (genhash || memcmp(hash_location, newhash, 16) != 0)
			goto release_sk1;
	} else {
		key = sk ? tcp_v6_md5_do_lookup(sk, &ipv6h->saddr) : NULL;
	}
#endif

	if (th->ack)
		seq = ntohl(th->ack_seq);
	else
		ack_seq = ntohl(th->seq) + th->syn + th->fin + skb->len -
			  (th->doff << 2);

	tcp_v6_send_response(skb, seq, ack_seq, 0, 0, key, 1, 0);

#ifdef CONFIG_TCP_MD5SIG
release_sk1:
	if (sk1) {
		rcu_read_unlock();
		sock_put(sk1);
	}
#endif
}

static void tcp_v6_send_ack(struct sk_buff *skb, u32 seq, u32 ack, u32 win, u32 ts,
			    struct tcp_md5sig_key *key, u8 tclass)
{
	tcp_v6_send_response(skb, seq, ack, win, ts, key, 0, tclass);
}

static void tcp_v6_timewait_ack(struct sock *sk, struct sk_buff *skb)
{
	struct inet_timewait_sock *tw = inet_twsk(sk);
	struct tcp_timewait_sock *tcptw = tcp_twsk(sk);

	tcp_v6_send_ack(skb, tcptw->tw_snd_nxt, tcptw->tw_rcv_nxt,
			tcptw->tw_rcv_wnd >> tw->tw_rcv_wscale,
			tcptw->tw_ts_recent, tcp_twsk_md5_key(tcptw),
			tw->tw_tclass);

	inet_twsk_put(tw);
}

static void tcp_v6_reqsk_send_ack(struct sock *sk, struct sk_buff *skb,
				  struct request_sock *req)
{
	tcp_v6_send_ack(skb, tcp_rsk(req)->snt_isn + 1, tcp_rsk(req)->rcv_isn + 1, req->rcv_wnd, req->ts_recent,
			tcp_v6_md5_do_lookup(sk, &ipv6_hdr(skb)->daddr), 0);
}


static struct sock *tcp_v6_hnd_req(struct sock *sk,struct sk_buff *skb)
{
	struct request_sock *req, **prev;
	const struct tcphdr *th = tcp_hdr(skb);
	struct sock *nsk;

	/* Find possible connection requests. */
	req = inet6_csk_search_req(sk, &prev, th->source,
				   &ipv6_hdr(skb)->saddr,
				   &ipv6_hdr(skb)->daddr, inet6_iif(skb));
	if (req)
		return tcp_check_req(sk, skb, req, prev);

	nsk = __inet6_lookup_established(sock_net(sk), &tcp_hashinfo,
			&ipv6_hdr(skb)->saddr, th->source,
			&ipv6_hdr(skb)->daddr, ntohs(th->dest), inet6_iif(skb));

	if (nsk) {
		if (nsk->sk_state != TCP_TIME_WAIT) {
			bh_lock_sock(nsk);
			return nsk;
		}
		inet_twsk_put(inet_twsk(nsk));
		return NULL;
	}

#ifdef CONFIG_SYN_COOKIES
	if (!th->syn)
		sk = cookie_v6_check(sk, skb);
#endif
	return sk;
}

/* FIXME: this is substantially similar to the ipv4 code.
 * Can some kind of merge be done? -- erics
 */
static int tcp_v6_conn_request(struct sock *sk, struct sk_buff *skb)
{
	struct tcp_extend_values tmp_ext;
	struct tcp_options_received tmp_opt;
	const u8 *hash_location;
	struct request_sock *req;
	struct inet6_request_sock *treq;
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct tcp_sock *tp = tcp_sk(sk);
	__u32 isn = TCP_SKB_CB(skb)->when;
	struct dst_entry *dst = NULL;
	int want_cookie = 0;

	if (skb->protocol == htons(ETH_P_IP))
		return tcp_v4_conn_request(sk, skb);

	if (!ipv6_unicast_destination(skb))
		goto drop;

	if (inet_csk_reqsk_queue_is_full(sk) && !isn) {
		want_cookie = tcp_syn_flood_action(sk, skb, "TCPv6");
		if (!want_cookie)
			goto drop;
	}

	if (sk_acceptq_is_full(sk) && inet_csk_reqsk_queue_young(sk) > 1)
		goto drop;

	req = inet6_reqsk_alloc(&tcp6_request_sock_ops);
	if (req == NULL)
		goto drop;

#ifdef CONFIG_TCP_MD5SIG
	tcp_rsk(req)->af_specific = &tcp_request_sock_ipv6_ops;
#endif

	tcp_clear_options(&tmp_opt);
	tmp_opt.mss_clamp = IPV6_MIN_MTU - sizeof(struct tcphdr) - sizeof(struct ipv6hdr);
	tmp_opt.user_mss = tp->rx_opt.user_mss;
	tcp_parse_options(skb, &tmp_opt, &hash_location, 0);

	if (tmp_opt.cookie_plus > 0 &&
	    tmp_opt.saw_tstamp &&
	    !tp->rx_opt.cookie_out_never &&
	    (sysctl_tcp_cookie_size > 0 ||
	     (tp->cookie_values != NULL &&
	      tp->cookie_values->cookie_desired > 0))) {
		u8 *c;
		u32 *d;
		u32 *mess = &tmp_ext.cookie_bakery[COOKIE_DIGEST_WORDS];
		int l = tmp_opt.cookie_plus - TCPOLEN_COOKIE_BASE;

		if (tcp_cookie_generator(&tmp_ext.cookie_bakery[0]) != 0)
			goto drop_and_free;

		/* Secret recipe starts with IP addresses */
		d = (__force u32 *)&ipv6_hdr(skb)->daddr.s6_addr32[0];
		*mess++ ^= *d++;
		*mess++ ^= *d++;
		*mess++ ^= *d++;
		*mess++ ^= *d++;
		d = (__force u32 *)&ipv6_hdr(skb)->saddr.s6_addr32[0];
		*mess++ ^= *d++;
		*mess++ ^= *d++;
		*mess++ ^= *d++;
		*mess++ ^= *d++;

		/* plus variable length Initiator Cookie */
		c = (u8 *)mess;
		while (l-- > 0)
			*c++ ^= *hash_location++;

		want_cookie = 0;	/* not our kind of cookie */
		tmp_ext.cookie_out_never = 0; /* false */
		tmp_ext.cookie_plus = tmp_opt.cookie_plus;
	} else if (!tp->rx_opt.cookie_in_always) {
		/* redundant indications, but ensure initialization. */
		tmp_ext.cookie_out_never = 1; /* true */
		tmp_ext.cookie_plus = 0;
	} else {
		goto drop_and_free;
	}
	tmp_ext.cookie_in_always = tp->rx_opt.cookie_in_always;

	if (want_cookie && !tmp_opt.saw_tstamp)
		tcp_clear_options(&tmp_opt);

	tmp_opt.tstamp_ok = tmp_opt.saw_tstamp;
	tcp_openreq_init(req, &tmp_opt, skb);

	treq = inet6_rsk(req);
	treq->rmt_addr = ipv6_hdr(skb)->saddr;
	treq->loc_addr = ipv6_hdr(skb)->daddr;
	if (!want_cookie || tmp_opt.tstamp_ok)
		TCP_ECN_create_request(req, tcp_hdr(skb));

	treq->iif = sk->sk_bound_dev_if;

	/* So that link locals have meaning */
	if (!sk->sk_bound_dev_if &&
	    ipv6_addr_type(&treq->rmt_addr) & IPV6_ADDR_LINKLOCAL)
		treq->iif = inet6_iif(skb);

	if (!isn) {
		struct inet_peer *peer = NULL;

		if (ipv6_opt_accepted(sk, skb) ||
		    np->rxopt.bits.rxinfo || np->rxopt.bits.rxoinfo ||
		    np->rxopt.bits.rxhlim || np->rxopt.bits.rxohlim) {
			atomic_inc(&skb->users);
			treq->pktopts = skb;
		}

		if (want_cookie) {
			isn = cookie_v6_init_sequence(sk, skb, &req->mss);
			req->cookie_ts = tmp_opt.tstamp_ok;
			goto have_isn;
		}

		/* VJ's idea. We save last timestamp seen
		 * from the destination in peer table, when entering
		 * state TIME-WAIT, and check against it before
		 * accepting new connection request.
		 *
		 * If "isn" is not zero, this request hit alive
		 * timewait bucket, so that all the necessary checks
		 * are made in the function processing timewait state.
		 */
		if (tmp_opt.saw_tstamp &&
		    tcp_death_row.sysctl_tw_recycle &&
		    (dst = inet6_csk_route_req(sk, req)) != NULL &&
		    (peer = rt6_get_peer((struct rt6_info *)dst)) != NULL &&
		    ipv6_addr_equal((struct in6_addr *)peer->daddr.addr.a6,
				    &treq->rmt_addr)) {
			inet_peer_refcheck(peer);
			if ((u32)get_seconds() - peer->tcp_ts_stamp < TCP_PAWS_MSL &&
			    (s32)(peer->tcp_ts - req->ts_recent) >
							TCP_PAWS_WINDOW) {
				NET_INC_STATS_BH(sock_net(sk), LINUX_MIB_PAWSPASSIVEREJECTED);
				goto drop_and_release;
			}
		}
		/* Kill the following clause, if you dislike this way. */
		else if (!sysctl_tcp_syncookies &&
			 (sysctl_max_syn_backlog - inet_csk_reqsk_queue_len(sk) <
			  (sysctl_max_syn_backlog >> 2)) &&
			 (!peer || !peer->tcp_ts_stamp) &&
			 (!dst || !dst_metric(dst, RTAX_RTT))) {
			/* Without syncookies last quarter of
			 * backlog is filled with destinations,
			 * proven to be alive.
			 * It means that we continue to communicate
			 * to destinations, already remembered
			 * to the moment of synflood.
			 */
			LIMIT_NETDEBUG(KERN_DEBUG "TCP: drop open request from %pI6/%u\n",
				       &treq->rmt_addr, ntohs(tcp_hdr(skb)->source));
			goto drop_and_release;
		}

		isn = tcp_v6_init_sequence(skb);
	}
have_isn:
	tcp_rsk(req)->snt_isn = isn;
	tcp_rsk(req)->snt_synack = tcp_time_stamp;

	security_inet_conn_request(sk, skb, req);

	if (tcp_v6_send_synack(sk, req,
			       (struct request_values *)&tmp_ext) ||
	    want_cookie)
		goto drop_and_free;

	inet6_csk_reqsk_queue_hash_add(sk, req, TCP_TIMEOUT_INIT);
	return 0;

drop_and_release:
	dst_release(dst);
drop_and_free:
	reqsk_free(req);
drop:
	return 0; /* don't send reset */
}

static struct sock * tcp_v6_syn_recv_sock(struct sock *sk, struct sk_buff *skb,
					  struct request_sock *req,
					  struct dst_entry *dst)
{
	struct inet6_request_sock *treq;
	struct ipv6_pinfo *newnp, *np = inet6_sk(sk);
	struct tcp6_sock *newtcp6sk;
	struct inet_sock *newinet;
	struct tcp_sock *newtp;
	struct sock *newsk;
	struct ipv6_txoptions *opt;
#ifdef CONFIG_TCP_MD5SIG
	struct tcp_md5sig_key *key;
#endif

	if (skb->protocol == htons(ETH_P_IP)) {
		/*
		 *	v6 mapped
		 */

		newsk = tcp_v4_syn_recv_sock(sk, skb, req, dst);

		if (newsk == NULL)
			return NULL;

		newtcp6sk = (struct tcp6_sock *)newsk;
		inet_sk(newsk)->pinet6 = &newtcp6sk->inet6;

		newinet = inet_sk(newsk);
		newnp = inet6_sk(newsk);
		newtp = tcp_sk(newsk);

		memcpy(newnp, np, sizeof(struct ipv6_pinfo));

		ipv6_addr_set_v4mapped(newinet->inet_daddr, &newnp->daddr);

		ipv6_addr_set_v4mapped(newinet->inet_saddr, &newnp->saddr);

		newnp->rcv_saddr = newnp->saddr;

		inet_csk(newsk)->icsk_af_ops = &ipv6_mapped;
		newsk->sk_backlog_rcv = tcp_v4_do_rcv;
#ifdef CONFIG_TCP_MD5SIG
		newtp->af_specific = &tcp_sock_ipv6_mapped_specific;
#endif

		newnp->ipv6_ac_list = NULL;
		newnp->ipv6_fl_list = NULL;
		newnp->pktoptions  = NULL;
		newnp->opt	   = NULL;
		newnp->mcast_oif   = inet6_iif(skb);
		newnp->mcast_hops  = ipv6_hdr(skb)->hop_limit;
		newnp->rcv_tclass  = ipv6_tclass(ipv6_hdr(skb));

		/*
		 * No need to charge this sock to the relevant IPv6 refcnt debug socks count
		 * here, tcp_create_openreq_child now does this for us, see the comment in
		 * that function for the gory details. -acme
		 */

		/* It is tricky place. Until this moment IPv4 tcp
		   worked with IPv6 icsk.icsk_af_ops.
		   Sync it now.
		 */
		tcp_sync_mss(newsk, inet_csk(newsk)->icsk_pmtu_cookie);

		return newsk;
	}

	treq = inet6_rsk(req);
	opt = np->opt;

	if (sk_acceptq_is_full(sk))
		goto out_overflow;

	if (!dst) {
		dst = inet6_csk_route_req(sk, req);
		if (!dst)
			goto out;
	}

	newsk = tcp_create_openreq_child(sk, req, skb);
	if (newsk == NULL)
		goto out_nonewsk;

	/*
	 * No need to charge this sock to the relevant IPv6 refcnt debug socks
	 * count here, tcp_create_openreq_child now does this for us, see the
	 * comment in that function for the gory details. -acme
	 */

	newsk->sk_gso_type = SKB_GSO_TCPV6;
	__ip6_dst_store(newsk, dst, NULL, NULL);

	newtcp6sk = (struct tcp6_sock *)newsk;
	inet_sk(newsk)->pinet6 = &newtcp6sk->inet6;

	newtp = tcp_sk(newsk);
	newinet = inet_sk(newsk);
	newnp = inet6_sk(newsk);

	memcpy(newnp, np, sizeof(struct ipv6_pinfo));

	newnp->daddr = treq->rmt_addr;
	newnp->saddr = treq->loc_addr;
	newnp->rcv_saddr = treq->loc_addr;
	newsk->sk_bound_dev_if = treq->iif;

	/* Now IPv6 options...

	   First: no IPv4 options.
	 */
	newinet->inet_opt = NULL;
	newnp->ipv6_ac_list = NULL;
	newnp->ipv6_fl_list = NULL;

	/* Clone RX bits */
	newnp->rxopt.all = np->rxopt.all;

	/* Clone pktoptions received with SYN */
	newnp->pktoptions = NULL;
	if (treq->pktopts != NULL) {
		newnp->pktoptions = skb_clone(treq->pktopts, GFP_ATOMIC);
		consume_skb(treq->pktopts);
		treq->pktopts = NULL;
		if (newnp->pktoptions)
			skb_set_owner_r(newnp->pktoptions, newsk);
	}
	newnp->opt	  = NULL;
	newnp->mcast_oif  = inet6_iif(skb);
	newnp->mcast_hops = ipv6_hdr(skb)->hop_limit;
	newnp->rcv_tclass = ipv6_tclass(ipv6_hdr(skb));

	/* Clone native IPv6 options from listening socket (if any)

	   Yes, keeping reference count would be much more clever,
	   but we make one more one thing there: reattach optmem
	   to newsk.
	 */
	if (opt) {
		newnp->opt = ipv6_dup_options(newsk, opt);
		if (opt != np->opt)
			sock_kfree_s(sk, opt, opt->tot_len);
	}

	inet_csk(newsk)->icsk_ext_hdr_len = 0;
	if (newnp->opt)
		inet_csk(newsk)->icsk_ext_hdr_len = (newnp->opt->opt_nflen +
						     newnp->opt->opt_flen);

	tcp_mtup_init(newsk);
	tcp_sync_mss(newsk, dst_mtu(dst));
	newtp->advmss = dst_metric_advmss(dst);
	tcp_initialize_rcv_mss(newsk);
	if (tcp_rsk(req)->snt_synack)
		tcp_valid_rtt_meas(newsk,
		    tcp_time_stamp - tcp_rsk(req)->snt_synack);
	newtp->total_retrans = req->retrans;

	newinet->inet_daddr = newinet->inet_saddr = LOOPBACK4_IPV6;
	newinet->inet_rcv_saddr = LOOPBACK4_IPV6;

#ifdef CONFIG_TCP_MD5SIG
	/* Copy over the MD5 key from the original socket */
	if ((key = tcp_v6_md5_do_lookup(sk, &newnp->daddr)) != NULL) {
		/* We're using one, so create a matching key
		 * on the newsk structure. If we fail to get
		 * memory, then we end up not copying the key
		 * across. Shucks.
		 */
		tcp_md5_do_add(newsk, (union tcp_md5_addr *)&newnp->daddr,
			       AF_INET6, key->key, key->keylen, GFP_ATOMIC);
	}
#endif

	if (__inet_inherit_port(sk, newsk) < 0) {
		sock_put(newsk);
		goto out;
	}
	__inet6_hash(newsk, NULL);

	return newsk;

out_overflow:
	NET_INC_STATS_BH(sock_net(sk), LINUX_MIB_LISTENOVERFLOWS);
out_nonewsk:
	if (opt && opt != np->opt)
		sock_kfree_s(sk, opt, opt->tot_len);
	dst_release(dst);
out:
	NET_INC_STATS_BH(sock_net(sk), LINUX_MIB_LISTENDROPS);
	return NULL;
}

static __sum16 tcp_v6_checksum_init(struct sk_buff *skb)
{
	if (skb->ip_summed == CHECKSUM_COMPLETE) {
		if (!tcp_v6_check(skb->len, &ipv6_hdr(skb)->saddr,
				  &ipv6_hdr(skb)->daddr, skb->csum)) {
			skb->ip_summed = CHECKSUM_UNNECESSARY;
			return 0;
		}
	}

	skb->csum = ~csum_unfold(tcp_v6_check(skb->len,
					      &ipv6_hdr(skb)->saddr,
					      &ipv6_hdr(skb)->daddr, 0));

	if (skb->len <= 76) {
		return __skb_checksum_complete(skb);
	}
	return 0;
}

/* The socket must have it's spinlock held when we get
 * here.
 *
 * We have a potential double-lock case here, so even when
 * doing backlog processing we use the BH locking scheme.
 * This is because we cannot sleep with the original spinlock
 * held.
 */
static int tcp_v6_do_rcv(struct sock *sk, struct sk_buff *skb)
{
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct tcp_sock *tp;
	struct sk_buff *opt_skb = NULL;

	/* Imagine: socket is IPv6. IPv4 packet arrives,
	   goes to IPv4 receive handler and backlogged.
	   From backlog it always goes here. Kerboom...
	   Fortunately, tcp_rcv_established and rcv_established
	   handle them correctly, but it is not case with
	   tcp_v6_hnd_req and tcp_v6_send_reset().   --ANK
	 */

	if (skb->protocol == htons(ETH_P_IP))
		return tcp_v4_do_rcv(sk, skb);

#ifdef CONFIG_TCP_MD5SIG
	if (tcp_v6_inbound_md5_hash (sk, skb))
		goto discard;
#endif

	if (sk_filter(sk, skb))
		goto discard;

	/*
	 *	socket locking is here for SMP purposes as backlog rcv
	 *	is currently called with bh processing disabled.
	 */

	/* Do Stevens' IPV6_PKTOPTIONS.

	   Yes, guys, it is the only place in our code, where we
	   may make it not affecting IPv4.
	   The rest of code is protocol independent,
	   and I do not like idea to uglify IPv4.

	   Actually, all the idea behind IPV6_PKTOPTIONS
	   looks not very well thought. For now we latch
	   options, received in the last packet, enqueued
	   by tcp. Feel free to propose better solution.
					       --ANK (980728)
	 */
	if (np->rxopt.all)
		opt_skb = skb_clone(skb, GFP_ATOMIC);

	if (sk->sk_state == TCP_ESTABLISHED) { /* Fast path */
		sock_rps_save_rxhash(sk, skb);
		if (tcp_rcv_established(sk, skb, tcp_hdr(skb), skb->len))
			goto reset;
		if (opt_skb)
			goto ipv6_pktoptions;
		return 0;
	}

	if (skb->len < tcp_hdrlen(skb) || tcp_checksum_complete(skb))
		goto csum_err;

	if (sk->sk_state == TCP_LISTEN) {
		struct sock *nsk = tcp_v6_hnd_req(sk, skb);
		if (!nsk)
			goto discard;

		/*
		 * Queue it on the new socket if the new socket is active,
		 * otherwise we just shortcircuit this and continue with
		 * the new socket..
		 */
		if(nsk != sk) {
			sock_rps_save_rxhash(nsk, skb);
			if (tcp_child_process(sk, nsk, skb))
				goto reset;
			if (opt_skb)
				__kfree_skb(opt_skb);
			return 0;
		}
	} else
		sock_rps_save_rxhash(sk, skb);

	if (tcp_rcv_state_process(sk, skb, tcp_hdr(skb), skb->len))
		goto reset;
	if (opt_skb)
		goto ipv6_pktoptions;
	return 0;

reset:
	tcp_v6_send_reset(sk, skb);
discard:
	if (opt_skb)
		__kfree_skb(opt_skb);
	kfree_skb(skb);
	return 0;
csum_err:
	TCP_INC_STATS_BH(sock_net(sk), TCP_MIB_INERRS);
	goto discard;


ipv6_pktoptions:
	/* Do you ask, what is it?

	   1. skb was enqueued by tcp.
	   2. skb is added to tail of read queue, rather than out of order.
	   3. socket is not in passive state.
	   4. Finally, it really contains options, which user wants to receive.
	 */
	tp = tcp_sk(sk);
	if (TCP_SKB_CB(opt_skb)->end_seq == tp->rcv_nxt &&
	    !((1 << sk->sk_state) & (TCPF_CLOSE | TCPF_LISTEN))) {
		if (np->rxopt.bits.rxinfo || np->rxopt.bits.rxoinfo)
			np->mcast_oif = inet6_iif(opt_skb);
		if (np->rxopt.bits.rxhlim || np->rxopt.bits.rxohlim)
			np->mcast_hops = ipv6_hdr(opt_skb)->hop_limit;
		if (np->rxopt.bits.rxtclass)
			np->rcv_tclass = ipv6_tclass(ipv6_hdr(skb));
		if (ipv6_opt_accepted(sk, opt_skb)) {
			skb_set_owner_r(opt_skb, sk);
			opt_skb = xchg(&np->pktoptions, opt_skb);
		} else {
			__kfree_skb(opt_skb);
			opt_skb = xchg(&np->pktoptions, NULL);
		}
	}

	kfree_skb(opt_skb);
	return 0;
}

static int tcp_v6_rcv(struct sk_buff *skb)
{
	const struct tcphdr *th;
	const struct ipv6hdr *hdr;
	struct sock *sk;
	int ret;
	struct net *net = dev_net(skb->dev);

	if (skb->pkt_type != PACKET_HOST)
		goto discard_it;

	/*
	 *	Count it even if it's bad.
	 */
	TCP_INC_STATS_BH(net, TCP_MIB_INSEGS);

	if (!pskb_may_pull(skb, sizeof(struct tcphdr)))
		goto discard_it;

	th = tcp_hdr(skb);

	if (th->doff < sizeof(struct tcphdr)/4)
		goto bad_packet;
	if (!pskb_may_pull(skb, th->doff*4))
		goto discard_it;

	if (!skb_csum_unnecessary(skb) && tcp_v6_checksum_init(skb))
		goto bad_packet;

	th = tcp_hdr(skb);
	hdr = ipv6_hdr(skb);
	TCP_SKB_CB(skb)->seq = ntohl(th->seq);
	TCP_SKB_CB(skb)->end_seq = (TCP_SKB_CB(skb)->seq + th->syn + th->fin +
				    skb->len - th->doff*4);
	TCP_SKB_CB(skb)->ack_seq = ntohl(th->ack_seq);
	TCP_SKB_CB(skb)->when = 0;
	TCP_SKB_CB(skb)->ip_dsfield = ipv6_get_dsfield(hdr);
	TCP_SKB_CB(skb)->sacked = 0;

	sk = __inet6_lookup_skb(&tcp_hashinfo, skb, th->source, th->dest);
	if (!sk)
		goto no_tcp_socket;

process:
	if (sk->sk_state == TCP_TIME_WAIT)
		goto do_time_wait;

	if (hdr->hop_limit < inet6_sk(sk)->min_hopcount) {
		NET_INC_STATS_BH(net, LINUX_MIB_TCPMINTTLDROP);
		goto discard_and_relse;
	}

	if (!xfrm6_policy_check(sk, XFRM_POLICY_IN, skb))
		goto discard_and_relse;

	if (sk_filter(sk, skb))
		goto discard_and_relse;

	skb->dev = NULL;

	bh_lock_sock_nested(sk);
	ret = 0;
	if (!sock_owned_by_user(sk)) {
#ifdef CONFIG_NET_DMA
		struct tcp_sock *tp = tcp_sk(sk);
		if (!tp->ucopy.dma_chan && tp->ucopy.pinned_list)
			tp->ucopy.dma_chan = net_dma_find_channel();
		if (tp->ucopy.dma_chan)
			ret = tcp_v6_do_rcv(sk, skb);
		else
#endif
		{
			if (!tcp_prequeue(sk, skb))
				ret = tcp_v6_do_rcv(sk, skb);
		}
	} else if (unlikely(sk_add_backlog(sk, skb, sk->sk_rcvbuf))) {
		bh_unlock_sock(sk);
		NET_INC_STATS_BH(net, LINUX_MIB_TCPBACKLOGDROP);
		goto discard_and_relse;
	}
	bh_unlock_sock(sk);

	sock_put(sk);
	return ret ? -1 : 0;

no_tcp_socket:
	if (!xfrm6_policy_check(NULL, XFRM_POLICY_IN, skb))
		goto discard_it;

	if (skb->len < (th->doff<<2) || tcp_checksum_complete(skb)) {
bad_packet:
		TCP_INC_STATS_BH(net, TCP_MIB_INERRS);
	} else {
		tcp_v6_send_reset(NULL, skb);
	}

discard_it:

	/*
	 *	Discard frame
	 */

	kfree_skb(skb);
	return 0;

discard_and_relse:
	sock_put(sk);
	goto discard_it;

do_time_wait:
	if (!xfrm6_policy_check(NULL, XFRM_POLICY_IN, skb)) {
		inet_twsk_put(inet_twsk(sk));
		goto discard_it;
	}

	if (skb->len < (th->doff<<2) || tcp_checksum_complete(skb)) {
		TCP_INC_STATS_BH(net, TCP_MIB_INERRS);
		inet_twsk_put(inet_twsk(sk));
		goto discard_it;
	}

	switch (tcp_timewait_state_process(inet_twsk(sk), skb, th)) {
	case TCP_TW_SYN:
	{
		struct sock *sk2;

		sk2 = inet6_lookup_listener(dev_net(skb->dev), &tcp_hashinfo,
					    &ipv6_hdr(skb)->daddr,
					    ntohs(th->dest), inet6_iif(skb));
		if (sk2 != NULL) {
			struct inet_timewait_sock *tw = inet_twsk(sk);
			inet_twsk_deschedule(tw, &tcp_death_row);
			inet_twsk_put(tw);
			sk = sk2;
			goto process;
		}
		/* Fall through to ACK */
	}
	case TCP_TW_ACK:
		tcp_v6_timewait_ack(sk, skb);
		break;
	case TCP_TW_RST:
		goto no_tcp_socket;
	case TCP_TW_SUCCESS:;
	}
	goto discard_it;
}

static struct inet_peer *tcp_v6_get_peer(struct sock *sk, bool *release_it)
{
	struct rt6_info *rt = (struct rt6_info *) __sk_dst_get(sk);
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct inet_peer *peer;

	if (!rt ||
	    !ipv6_addr_equal(&np->daddr, &rt->rt6i_dst.addr)) {
		peer = inet_getpeer_v6(&np->daddr, 1);
		*release_it = true;
	} else {
		if (!rt->rt6i_peer)
			rt6_bind_peer(rt, 1);
		peer = rt->rt6i_peer;
		*release_it = false;
	}

	return peer;
}

static void *tcp_v6_tw_get_peer(struct sock *sk)
{
	const struct inet6_timewait_sock *tw6 = inet6_twsk(sk);
	const struct inet_timewait_sock *tw = inet_twsk(sk);

	if (tw->tw_family == AF_INET)
		return tcp_v4_tw_get_peer(sk);

	return inet_getpeer_v6(&tw6->tw_v6_daddr, 1);
}

static struct timewait_sock_ops tcp6_timewait_sock_ops = {
	.twsk_obj_size	= sizeof(struct tcp6_timewait_sock),
	.twsk_unique	= tcp_twsk_unique,
	.twsk_destructor= tcp_twsk_destructor,
	.twsk_getpeer	= tcp_v6_tw_get_peer,
};

static const struct inet_connection_sock_af_ops ipv6_specific = {
	.queue_xmit	   = inet6_csk_xmit,
	.send_check	   = tcp_v6_send_check,
	.rebuild_header	   = inet6_sk_rebuild_header,
	.conn_request	   = tcp_v6_conn_request,
	.syn_recv_sock	   = tcp_v6_syn_recv_sock,
	.get_peer	   = tcp_v6_get_peer,
	.net_header_len	   = sizeof(struct ipv6hdr),
	.setsockopt	   = ipv6_setsockopt,
	.getsockopt	   = ipv6_getsockopt,
	.addr2sockaddr	   = inet6_csk_addr2sockaddr,
	.sockaddr_len	   = sizeof(struct sockaddr_in6),
	.bind_conflict	   = inet6_csk_bind_conflict,
#ifdef CONFIG_COMPAT
	.compat_setsockopt = compat_ipv6_setsockopt,
	.compat_getsockopt = compat_ipv6_getsockopt,
#endif
};

#ifdef CONFIG_TCP_MD5SIG
static const struct tcp_sock_af_ops tcp_sock_ipv6_specific = {
	.md5_lookup	=	tcp_v6_md5_lookup,
	.calc_md5_hash	=	tcp_v6_md5_hash_skb,
	.md5_parse	=	tcp_v6_parse_md5_keys,
};
#endif

/*
 *	TCP over IPv4 via INET6 API
 */

static const struct inet_connection_sock_af_ops ipv6_mapped = {
	.queue_xmit	   = ip_queue_xmit,
	.send_check	   = tcp_v4_send_check,
	.rebuild_header	   = inet_sk_rebuild_header,
	.conn_request	   = tcp_v6_conn_request,
	.syn_recv_sock	   = tcp_v6_syn_recv_sock,
	.get_peer	   = tcp_v4_get_peer,
	.net_header_len	   = sizeof(struct iphdr),
	.setsockopt	   = ipv6_setsockopt,
	.getsockopt	   = ipv6_getsockopt,
	.addr2sockaddr	   = inet6_csk_addr2sockaddr,
	.sockaddr_len	   = sizeof(struct sockaddr_in6),
	.bind_conflict	   = inet6_csk_bind_conflict,
#ifdef CONFIG_COMPAT
	.compat_setsockopt = compat_ipv6_setsockopt,
	.compat_getsockopt = compat_ipv6_getsockopt,
#endif
};

#ifdef CONFIG_TCP_MD5SIG
static const struct tcp_sock_af_ops tcp_sock_ipv6_mapped_specific = {
	.md5_lookup	=	tcp_v4_md5_lookup,
	.calc_md5_hash	=	tcp_v4_md5_hash_skb,
	.md5_parse	=	tcp_v6_parse_md5_keys,
};
#endif

/* NOTE: A lot of things set to zero explicitly by call to
 *       sk_alloc() so need not be done here.
 */
static int tcp_v6_init_sock(struct sock *sk)
{
	struct inet_connection_sock *icsk = inet_csk(sk);

	tcp_init_sock(sk);

	icsk->icsk_af_ops = &ipv6_specific;

#ifdef CONFIG_TCP_MD5SIG
	tcp_sk(sk)->af_specific = &tcp_sock_ipv6_specific;
#endif

	return 0;
}

static void tcp_v6_destroy_sock(struct sock *sk)
{
	tcp_v4_destroy_sock(sk);
	inet6_destroy_sock(sk);
}

#ifdef CONFIG_PROC_FS
/* Proc filesystem TCPv6 sock list dumping. */
static void get_openreq6(struct seq_file *seq,
			 const struct sock *sk, struct request_sock *req, int i, int uid)
{
	int ttd = req->expires - jiffies;
	const struct in6_addr *src = &inet6_rsk(req)->loc_addr;
	const struct in6_addr *dest = &inet6_rsk(req)->rmt_addr;

	if (ttd < 0)
		ttd = 0;

	seq_printf(seq,
		   "%4d: %08X%08X%08X%08X:%04X %08X%08X%08X%08X:%04X "
		   "%02X %08X:%08X %02X:%08lX %08X %5d %8d %d %d %pK\n",
		   i,
		   src->s6_addr32[0], src->s6_addr32[1],
		   src->s6_addr32[2], src->s6_addr32[3],
		   ntohs(inet_rsk(req)->loc_port),
		   dest->s6_addr32[0], dest->s6_addr32[1],
		   dest->s6_addr32[2], dest->s6_addr32[3],
		   ntohs(inet_rsk(req)->rmt_port),
		   TCP_SYN_RECV,
		   0,0, /* could print option size, but that is af dependent. */
		   1,   /* timers active (only the expire timer) */
		   jiffies_to_clock_t(ttd),
		   req->retrans,
		   uid,
		   0,  /* non standard timer */
		   0, /* open_requests have no inode */
		   0, req);
}

static void get_tcp6_sock(struct seq_file *seq, struct sock *sp, int i)
{
	const struct in6_addr *dest, *src;
	__u16 destp, srcp;
	int timer_active;
	unsigned long timer_expires;
	const struct inet_sock *inet = inet_sk(sp);
	const struct tcp_sock *tp = tcp_sk(sp);
	const struct inet_connection_sock *icsk = inet_csk(sp);
	const struct ipv6_pinfo *np = inet6_sk(sp);

	dest  = &np->daddr;
	src   = &np->rcv_saddr;
	destp = ntohs(inet->inet_dport);
	srcp  = ntohs(inet->inet_sport);

	if (icsk->icsk_pending == ICSK_TIME_RETRANS) {
		timer_active	= 1;
		timer_expires	= icsk->icsk_timeout;
	} else if (icsk->icsk_pending == ICSK_TIME_PROBE0) {
		timer_active	= 4;
		timer_expires	= icsk->icsk_timeout;
	} else if (timer_pending(&sp->sk_timer)) {
		timer_active	= 2;
		timer_expires	= sp->sk_timer.expires;
	} else {
		timer_active	= 0;
		timer_expires = jiffies;
	}

	seq_printf(seq,
		   "%4d: %08X%08X%08X%08X:%04X %08X%08X%08X%08X:%04X "
		   "%02X %08X:%08X %02X:%08lX %08X %5d %8d %lu %d %pK %lu %lu %u %u %d\n",
		   i,
		   src->s6_addr32[0], src->s6_addr32[1],
		   src->s6_addr32[2], src->s6_addr32[3], srcp,
		   dest->s6_addr32[0], dest->s6_addr32[1],
		   dest->s6_addr32[2], dest->s6_addr32[3], destp,
		   sp->sk_state,
		   tp->write_seq-tp->snd_una,
		   (sp->sk_state == TCP_LISTEN) ? sp->sk_ack_backlog : (tp->rcv_nxt - tp->copied_seq),
		   timer_active,
		   jiffies_to_clock_t(timer_expires - jiffies),
		   icsk->icsk_retransmits,
		   sock_i_uid(sp),
		   icsk->icsk_probes_out,
		   sock_i_ino(sp),
		   atomic_read(&sp->sk_refcnt), sp,
		   jiffies_to_clock_t(icsk->icsk_rto),
		   jiffies_to_clock_t(icsk->icsk_ack.ato),
		   (icsk->icsk_ack.quick << 1 ) | icsk->icsk_ack.pingpong,
		   tp->snd_cwnd,
		   tcp_in_initial_slowstart(tp) ? -1 : tp->snd_ssthresh
		   );
}

static void get_timewait6_sock(struct seq_file *seq,
			       struct inet_timewait_sock *tw, int i)
{
	const struct in6_addr *dest, *src;
	__u16 destp, srcp;
	const struct inet6_timewait_sock *tw6 = inet6_twsk((struct sock *)tw);
	int ttd = tw->tw_ttd - jiffies;

	if (ttd < 0)
		ttd = 0;

	dest = &tw6->tw_v6_daddr;
	src  = &tw6->tw_v6_rcv_saddr;
	destp = ntohs(tw->tw_dport);
	srcp  = ntohs(tw->tw_sport);

	seq_printf(seq,
		   "%4d: %08X%08X%08X%08X:%04X %08X%08X%08X%08X:%04X "
		   "%02X %08X:%08X %02X:%08lX %08X %5d %8d %d %d %pK\n",
		   i,
		   src->s6_addr32[0], src->s6_addr32[1],
		   src->s6_addr32[2], src->s6_addr32[3], srcp,
		   dest->s6_addr32[0], dest->s6_addr32[1],
		   dest->s6_addr32[2], dest->s6_addr32[3], destp,
		   tw->tw_substate, 0, 0,
		   3, jiffies_to_clock_t(ttd), 0, 0, 0, 0,
		   atomic_read(&tw->tw_refcnt), tw);
}

static int tcp6_seq_show(struct seq_file *seq, void *v)
{
	struct tcp_iter_state *st;

	if (v == SEQ_START_TOKEN) {
		seq_puts(seq,
			 "  sl  "
			 "local_address                         "
			 "remote_address                        "
			 "st tx_queue rx_queue tr tm->when retrnsmt"
			 "   uid  timeout inode\n");
		goto out;
	}
	st = seq->private;

	switch (st->state) {
	case TCP_SEQ_STATE_LISTENING:
	case TCP_SEQ_STATE_ESTABLISHED:
		get_tcp6_sock(seq, v, st->num);
		break;
	case TCP_SEQ_STATE_OPENREQ:
		get_openreq6(seq, st->syn_wait_sk, v, st->num, st->uid);
		break;
	case TCP_SEQ_STATE_TIME_WAIT:
		get_timewait6_sock(seq, v, st->num);
		break;
	}
out:
	return 0;
}

static const struct file_operations tcp6_afinfo_seq_fops = {
	.owner   = THIS_MODULE,
	.open    = tcp_seq_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release_net
};

static struct tcp_seq_afinfo tcp6_seq_afinfo = {
	.name		= "tcp6",
	.family		= AF_INET6,
	.seq_fops	= &tcp6_afinfo_seq_fops,
	.seq_ops	= {
		.show		= tcp6_seq_show,
	},
};

int __net_init tcp6_proc_init(struct net *net)
{
	return tcp_proc_register(net, &tcp6_seq_afinfo);
}

void tcp6_proc_exit(struct net *net)
{
	tcp_proc_unregister(net, &tcp6_seq_afinfo);
}
#endif

struct proto tcpv6_prot = {
	.name			= "TCPv6",
	.owner			= THIS_MODULE,
	.close			= tcp_close,
	.connect		= tcp_v6_connect,
	.disconnect		= tcp_disconnect,
	.accept			= inet_csk_accept,
	.ioctl			= tcp_ioctl,
	.init			= tcp_v6_init_sock,
	.destroy		= tcp_v6_destroy_sock,
	.shutdown		= tcp_shutdown,
	.setsockopt		= tcp_setsockopt,
	.getsockopt		= tcp_getsockopt,
	.recvmsg		= tcp_recvmsg,
	.sendmsg		= tcp_sendmsg,
	.sendpage		= tcp_sendpage,
	.backlog_rcv		= tcp_v6_do_rcv,
	.hash			= tcp_v6_hash,
	.unhash			= inet_unhash,
	.get_port		= inet_csk_get_port,
	.enter_memory_pressure	= tcp_enter_memory_pressure,
	.sockets_allocated	= &tcp_sockets_allocated,
	.memory_allocated	= &tcp_memory_allocated,
	.memory_pressure	= &tcp_memory_pressure,
	.orphan_count		= &tcp_orphan_count,
	.sysctl_wmem		= sysctl_tcp_wmem,
	.sysctl_rmem		= sysctl_tcp_rmem,
	.max_header		= MAX_TCP_HEADER,
	.obj_size		= sizeof(struct tcp6_sock),
	.slab_flags		= SLAB_DESTROY_BY_RCU,
	.twsk_prot		= &tcp6_timewait_sock_ops,
	.rsk_prot		= &tcp6_request_sock_ops,
	.h.hashinfo		= &tcp_hashinfo,
	.no_autobind		= true,
#ifdef CONFIG_COMPAT
	.compat_setsockopt	= compat_tcp_setsockopt,
	.compat_getsockopt	= compat_tcp_getsockopt,
#endif
#ifdef CONFIG_CGROUP_MEM_RES_CTLR_KMEM
	.proto_cgroup		= tcp_proto_cgroup,
#endif
};

static const struct inet6_protocol tcpv6_protocol = {
	.handler	=	tcp_v6_rcv,
	.err_handler	=	tcp_v6_err,
	.gso_send_check	=	tcp_v6_gso_send_check,
	.gso_segment	=	tcp_tso_segment,
	.gro_receive	=	tcp6_gro_receive,
	.gro_complete	=	tcp6_gro_complete,
	.flags		=	INET6_PROTO_NOPOLICY|INET6_PROTO_FINAL,
};

static struct inet_protosw tcpv6_protosw = {
	.type		=	SOCK_STREAM,
	.protocol	=	IPPROTO_TCP,
	.prot		=	&tcpv6_prot,
	.ops		=	&inet6_stream_ops,
	.no_check	=	0,
	.flags		=	INET_PROTOSW_PERMANENT |
				INET_PROTOSW_ICSK,
};

static int __net_init tcpv6_net_init(struct net *net)
{
	return inet_ctl_sock_create(&net->ipv6.tcp_sk, PF_INET6,
				    SOCK_RAW, IPPROTO_TCP, net);
}

static void __net_exit tcpv6_net_exit(struct net *net)
{
	inet_ctl_sock_destroy(net->ipv6.tcp_sk);
}

static void __net_exit tcpv6_net_exit_batch(struct list_head *net_exit_list)
{
	inet_twsk_purge(&tcp_hashinfo, &tcp_death_row, AF_INET6);
}

static struct pernet_operations tcpv6_net_ops = {
	.init	    = tcpv6_net_init,
	.exit	    = tcpv6_net_exit,
	.exit_batch = tcpv6_net_exit_batch,
};

int __init tcpv6_init(void)
{
	int ret;

	ret = inet6_add_protocol(&tcpv6_protocol, IPPROTO_TCP);
	if (ret)
		goto out;

	/* register inet6 protocol */
	ret = inet6_register_protosw(&tcpv6_protosw);
	if (ret)
		goto out_tcpv6_protocol;

	ret = register_pernet_subsys(&tcpv6_net_ops);
	if (ret)
		goto out_tcpv6_protosw;
out:
	return ret;

out_tcpv6_protocol:
	inet6_del_protocol(&tcpv6_protocol, IPPROTO_TCP);
out_tcpv6_protosw:
	inet6_unregister_protosw(&tcpv6_protosw);
	goto out;
}

void tcpv6_exit(void)
{
	unregister_pernet_subsys(&tcpv6_net_ops);
	inet6_unregister_protosw(&tcpv6_protosw);
	inet6_del_protocol(&tcpv6_protocol, IPPROTO_TCP);
}
