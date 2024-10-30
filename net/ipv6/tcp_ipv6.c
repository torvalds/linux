// SPDX-License-Identifier: GPL-2.0-or-later
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
#include <linux/uaccess.h>
#include <linux/ipv6.h>
#include <linux/icmpv6.h>
#include <linux/random.h>
#include <linux/indirect_call_wrapper.h>

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
#include <net/inet_common.h>
#include <net/secure_seq.h>
#include <net/hotdata.h>
#include <net/busy_poll.h>
#include <net/rstreason.h>

#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include <crypto/hash.h>
#include <linux/scatterlist.h>

#include <trace/events/tcp.h>

static void tcp_v6_send_reset(const struct sock *sk, struct sk_buff *skb,
			      enum sk_rst_reason reason);
static void	tcp_v6_reqsk_send_ack(const struct sock *sk, struct sk_buff *skb,
				      struct request_sock *req);

INDIRECT_CALLABLE_SCOPE int tcp_v6_do_rcv(struct sock *sk, struct sk_buff *skb);

static const struct inet_connection_sock_af_ops ipv6_mapped;
const struct inet_connection_sock_af_ops ipv6_specific;
#if defined(CONFIG_TCP_MD5SIG) || defined(CONFIG_TCP_AO)
static const struct tcp_sock_af_ops tcp_sock_ipv6_specific;
static const struct tcp_sock_af_ops tcp_sock_ipv6_mapped_specific;
#endif

/* Helper returning the inet6 address from a given tcp socket.
 * It can be used in TCP stack instead of inet6_sk(sk).
 * This avoids a dereference and allow compiler optimizations.
 * It is a specialized version of inet6_sk_generic().
 */
#define tcp_inet6_sk(sk) (&container_of_const(tcp_sk(sk), \
					      struct tcp6_sock, tcp)->inet6)

static void inet6_sk_rx_dst_set(struct sock *sk, const struct sk_buff *skb)
{
	struct dst_entry *dst = skb_dst(skb);

	if (dst && dst_hold_safe(dst)) {
		rcu_assign_pointer(sk->sk_rx_dst, dst);
		sk->sk_rx_dst_ifindex = skb->skb_iif;
		sk->sk_rx_dst_cookie = rt6_get_cookie(dst_rt6_info(dst));
	}
}

static u32 tcp_v6_init_seq(const struct sk_buff *skb)
{
	return secure_tcpv6_seq(ipv6_hdr(skb)->daddr.s6_addr32,
				ipv6_hdr(skb)->saddr.s6_addr32,
				tcp_hdr(skb)->dest,
				tcp_hdr(skb)->source);
}

static u32 tcp_v6_init_ts_off(const struct net *net, const struct sk_buff *skb)
{
	return secure_tcpv6_ts_off(net, ipv6_hdr(skb)->daddr.s6_addr32,
				   ipv6_hdr(skb)->saddr.s6_addr32);
}

static int tcp_v6_pre_connect(struct sock *sk, struct sockaddr *uaddr,
			      int addr_len)
{
	/* This check is replicated from tcp_v6_connect() and intended to
	 * prevent BPF program called below from accessing bytes that are out
	 * of the bound specified by user in addr_len.
	 */
	if (addr_len < SIN6_LEN_RFC2133)
		return -EINVAL;

	sock_owned_by_me(sk);

	return BPF_CGROUP_RUN_PROG_INET6_CONNECT(sk, uaddr, &addr_len);
}

static int tcp_v6_connect(struct sock *sk, struct sockaddr *uaddr,
			  int addr_len)
{
	struct sockaddr_in6 *usin = (struct sockaddr_in6 *) uaddr;
	struct inet_connection_sock *icsk = inet_csk(sk);
	struct in6_addr *saddr = NULL, *final_p, final;
	struct inet_timewait_death_row *tcp_death_row;
	struct ipv6_pinfo *np = tcp_inet6_sk(sk);
	struct inet_sock *inet = inet_sk(sk);
	struct tcp_sock *tp = tcp_sk(sk);
	struct net *net = sock_net(sk);
	struct ipv6_txoptions *opt;
	struct dst_entry *dst;
	struct flowi6 fl6;
	int addr_type;
	int err;

	if (addr_len < SIN6_LEN_RFC2133)
		return -EINVAL;

	if (usin->sin6_family != AF_INET6)
		return -EAFNOSUPPORT;

	memset(&fl6, 0, sizeof(fl6));

	if (inet6_test_bit(SNDFLOW, sk)) {
		fl6.flowlabel = usin->sin6_flowinfo&IPV6_FLOWINFO_MASK;
		IP6_ECN_flow_init(fl6.flowlabel);
		if (fl6.flowlabel&IPV6_FLOWLABEL_MASK) {
			struct ip6_flowlabel *flowlabel;
			flowlabel = fl6_sock_lookup(sk, fl6.flowlabel);
			if (IS_ERR(flowlabel))
				return -EINVAL;
			fl6_sock_release(flowlabel);
		}
	}

	/*
	 *	connect() to INADDR_ANY means loopback (BSD'ism).
	 */

	if (ipv6_addr_any(&usin->sin6_addr)) {
		if (ipv6_addr_v4mapped(&sk->sk_v6_rcv_saddr))
			ipv6_addr_set_v4mapped(htonl(INADDR_LOOPBACK),
					       &usin->sin6_addr);
		else
			usin->sin6_addr = in6addr_loopback;
	}

	addr_type = ipv6_addr_type(&usin->sin6_addr);

	if (addr_type & IPV6_ADDR_MULTICAST)
		return -ENETUNREACH;

	if (addr_type&IPV6_ADDR_LINKLOCAL) {
		if (addr_len >= sizeof(struct sockaddr_in6) &&
		    usin->sin6_scope_id) {
			/* If interface is set while binding, indices
			 * must coincide.
			 */
			if (!sk_dev_equal_l3scope(sk, usin->sin6_scope_id))
				return -EINVAL;

			sk->sk_bound_dev_if = usin->sin6_scope_id;
		}

		/* Connect to link-local address requires an interface */
		if (!sk->sk_bound_dev_if)
			return -EINVAL;
	}

	if (tp->rx_opt.ts_recent_stamp &&
	    !ipv6_addr_equal(&sk->sk_v6_daddr, &usin->sin6_addr)) {
		tp->rx_opt.ts_recent = 0;
		tp->rx_opt.ts_recent_stamp = 0;
		WRITE_ONCE(tp->write_seq, 0);
	}

	sk->sk_v6_daddr = usin->sin6_addr;
	np->flow_label = fl6.flowlabel;

	/*
	 *	TCP over IPv4
	 */

	if (addr_type & IPV6_ADDR_MAPPED) {
		u32 exthdrlen = icsk->icsk_ext_hdr_len;
		struct sockaddr_in sin;

		if (ipv6_only_sock(sk))
			return -ENETUNREACH;

		sin.sin_family = AF_INET;
		sin.sin_port = usin->sin6_port;
		sin.sin_addr.s_addr = usin->sin6_addr.s6_addr32[3];

		/* Paired with READ_ONCE() in tcp_(get|set)sockopt() */
		WRITE_ONCE(icsk->icsk_af_ops, &ipv6_mapped);
		if (sk_is_mptcp(sk))
			mptcpv6_handle_mapped(sk, true);
		sk->sk_backlog_rcv = tcp_v4_do_rcv;
#if defined(CONFIG_TCP_MD5SIG) || defined(CONFIG_TCP_AO)
		tp->af_specific = &tcp_sock_ipv6_mapped_specific;
#endif

		err = tcp_v4_connect(sk, (struct sockaddr *)&sin, sizeof(sin));

		if (err) {
			icsk->icsk_ext_hdr_len = exthdrlen;
			/* Paired with READ_ONCE() in tcp_(get|set)sockopt() */
			WRITE_ONCE(icsk->icsk_af_ops, &ipv6_specific);
			if (sk_is_mptcp(sk))
				mptcpv6_handle_mapped(sk, false);
			sk->sk_backlog_rcv = tcp_v6_do_rcv;
#if defined(CONFIG_TCP_MD5SIG) || defined(CONFIG_TCP_AO)
			tp->af_specific = &tcp_sock_ipv6_specific;
#endif
			goto failure;
		}
		np->saddr = sk->sk_v6_rcv_saddr;

		return err;
	}

	if (!ipv6_addr_any(&sk->sk_v6_rcv_saddr))
		saddr = &sk->sk_v6_rcv_saddr;

	fl6.flowi6_proto = IPPROTO_TCP;
	fl6.daddr = sk->sk_v6_daddr;
	fl6.saddr = saddr ? *saddr : np->saddr;
	fl6.flowlabel = ip6_make_flowinfo(np->tclass, np->flow_label);
	fl6.flowi6_oif = sk->sk_bound_dev_if;
	fl6.flowi6_mark = sk->sk_mark;
	fl6.fl6_dport = usin->sin6_port;
	fl6.fl6_sport = inet->inet_sport;
	fl6.flowi6_uid = sk->sk_uid;

	opt = rcu_dereference_protected(np->opt, lockdep_sock_is_held(sk));
	final_p = fl6_update_dst(&fl6, opt, &final);

	security_sk_classify_flow(sk, flowi6_to_flowi_common(&fl6));

	dst = ip6_dst_lookup_flow(net, sk, &fl6, final_p);
	if (IS_ERR(dst)) {
		err = PTR_ERR(dst);
		goto failure;
	}

	tp->tcp_usec_ts = dst_tcp_usec_ts(dst);
	tcp_death_row = &sock_net(sk)->ipv4.tcp_death_row;

	if (!saddr) {
		saddr = &fl6.saddr;

		err = inet_bhash2_update_saddr(sk, saddr, AF_INET6);
		if (err)
			goto failure;
	}

	/* set the source address */
	np->saddr = *saddr;
	inet->inet_rcv_saddr = LOOPBACK4_IPV6;

	sk->sk_gso_type = SKB_GSO_TCPV6;
	ip6_dst_store(sk, dst, NULL, NULL);

	icsk->icsk_ext_hdr_len = 0;
	if (opt)
		icsk->icsk_ext_hdr_len = opt->opt_flen +
					 opt->opt_nflen;

	tp->rx_opt.mss_clamp = IPV6_MIN_MTU - sizeof(struct tcphdr) - sizeof(struct ipv6hdr);

	inet->inet_dport = usin->sin6_port;

	tcp_set_state(sk, TCP_SYN_SENT);
	err = inet6_hash_connect(tcp_death_row, sk);
	if (err)
		goto late_failure;

	sk_set_txhash(sk);

	if (likely(!tp->repair)) {
		if (!tp->write_seq)
			WRITE_ONCE(tp->write_seq,
				   secure_tcpv6_seq(np->saddr.s6_addr32,
						    sk->sk_v6_daddr.s6_addr32,
						    inet->inet_sport,
						    inet->inet_dport));
		tp->tsoffset = secure_tcpv6_ts_off(net, np->saddr.s6_addr32,
						   sk->sk_v6_daddr.s6_addr32);
	}

	if (tcp_fastopen_defer_connect(sk, &err))
		return err;
	if (err)
		goto late_failure;

	err = tcp_connect(sk);
	if (err)
		goto late_failure;

	return 0;

late_failure:
	tcp_set_state(sk, TCP_CLOSE);
	inet_bhash2_reset_saddr(sk);
failure:
	inet->inet_dport = 0;
	sk->sk_route_caps = 0;
	return err;
}

static void tcp_v6_mtu_reduced(struct sock *sk)
{
	struct dst_entry *dst;
	u32 mtu;

	if ((1 << sk->sk_state) & (TCPF_LISTEN | TCPF_CLOSE))
		return;

	mtu = READ_ONCE(tcp_sk(sk)->mtu_info);

	/* Drop requests trying to increase our current mss.
	 * Check done in __ip6_rt_update_pmtu() is too late.
	 */
	if (tcp_mtu_to_mss(sk, mtu) >= tcp_sk(sk)->mss_cache)
		return;

	dst = inet6_csk_update_pmtu(sk, mtu);
	if (!dst)
		return;

	if (inet_csk(sk)->icsk_pmtu_cookie > dst_mtu(dst)) {
		tcp_sync_mss(sk, dst_mtu(dst));
		tcp_simple_retransmit(sk);
	}
}

static int tcp_v6_err(struct sk_buff *skb, struct inet6_skb_parm *opt,
		u8 type, u8 code, int offset, __be32 info)
{
	const struct ipv6hdr *hdr = (const struct ipv6hdr *)skb->data;
	const struct tcphdr *th = (struct tcphdr *)(skb->data+offset);
	struct net *net = dev_net(skb->dev);
	struct request_sock *fastopen;
	struct ipv6_pinfo *np;
	struct tcp_sock *tp;
	__u32 seq, snd_una;
	struct sock *sk;
	bool fatal;
	int err;

	sk = __inet6_lookup_established(net, net->ipv4.tcp_death_row.hashinfo,
					&hdr->daddr, th->dest,
					&hdr->saddr, ntohs(th->source),
					skb->dev->ifindex, inet6_sdif(skb));

	if (!sk) {
		__ICMP6_INC_STATS(net, __in6_dev_get(skb->dev),
				  ICMP6_MIB_INERRORS);
		return -ENOENT;
	}

	if (sk->sk_state == TCP_TIME_WAIT) {
		/* To increase the counter of ignored icmps for TCP-AO */
		tcp_ao_ignore_icmp(sk, AF_INET6, type, code);
		inet_twsk_put(inet_twsk(sk));
		return 0;
	}
	seq = ntohl(th->seq);
	fatal = icmpv6_err_convert(type, code, &err);
	if (sk->sk_state == TCP_NEW_SYN_RECV) {
		tcp_req_err(sk, seq, fatal);
		return 0;
	}

	if (tcp_ao_ignore_icmp(sk, AF_INET6, type, code)) {
		sock_put(sk);
		return 0;
	}

	bh_lock_sock(sk);
	if (sock_owned_by_user(sk) && type != ICMPV6_PKT_TOOBIG)
		__NET_INC_STATS(net, LINUX_MIB_LOCKDROPPEDICMPS);

	if (sk->sk_state == TCP_CLOSE)
		goto out;

	if (static_branch_unlikely(&ip6_min_hopcount)) {
		/* min_hopcount can be changed concurrently from do_ipv6_setsockopt() */
		if (ipv6_hdr(skb)->hop_limit < READ_ONCE(tcp_inet6_sk(sk)->min_hopcount)) {
			__NET_INC_STATS(net, LINUX_MIB_TCPMINTTLDROP);
			goto out;
		}
	}

	tp = tcp_sk(sk);
	/* XXX (TFO) - tp->snd_una should be ISN (tcp_create_openreq_child() */
	fastopen = rcu_dereference(tp->fastopen_rsk);
	snd_una = fastopen ? tcp_rsk(fastopen)->snt_isn : tp->snd_una;
	if (sk->sk_state != TCP_LISTEN &&
	    !between(seq, snd_una, tp->snd_nxt)) {
		__NET_INC_STATS(net, LINUX_MIB_OUTOFWINDOWICMPS);
		goto out;
	}

	np = tcp_inet6_sk(sk);

	if (type == NDISC_REDIRECT) {
		if (!sock_owned_by_user(sk)) {
			struct dst_entry *dst = __sk_dst_check(sk, np->dst_cookie);

			if (dst)
				dst->ops->redirect(dst, sk, skb);
		}
		goto out;
	}

	if (type == ICMPV6_PKT_TOOBIG) {
		u32 mtu = ntohl(info);

		/* We are not interested in TCP_LISTEN and open_requests
		 * (SYN-ACKs send out by Linux are always <576bytes so
		 * they should go through unfragmented).
		 */
		if (sk->sk_state == TCP_LISTEN)
			goto out;

		if (!ip6_sk_accept_pmtu(sk))
			goto out;

		if (mtu < IPV6_MIN_MTU)
			goto out;

		WRITE_ONCE(tp->mtu_info, mtu);

		if (!sock_owned_by_user(sk))
			tcp_v6_mtu_reduced(sk);
		else if (!test_and_set_bit(TCP_MTU_REDUCED_DEFERRED,
					   &sk->sk_tsq_flags))
			sock_hold(sk);
		goto out;
	}


	/* Might be for an request_sock */
	switch (sk->sk_state) {
	case TCP_SYN_SENT:
	case TCP_SYN_RECV:
		/* Only in fast or simultaneous open. If a fast open socket is
		 * already accepted it is treated as a connected one below.
		 */
		if (fastopen && !fastopen->sk)
			break;

		ipv6_icmp_error(sk, skb, err, th->dest, ntohl(info), (u8 *)th);

		if (!sock_owned_by_user(sk))
			tcp_done_with_error(sk, err);
		else
			WRITE_ONCE(sk->sk_err_soft, err);
		goto out;
	case TCP_LISTEN:
		break;
	default:
		/* check if this ICMP message allows revert of backoff.
		 * (see RFC 6069)
		 */
		if (!fastopen && type == ICMPV6_DEST_UNREACH &&
		    code == ICMPV6_NOROUTE)
			tcp_ld_RTO_revert(sk, seq);
	}

	if (!sock_owned_by_user(sk) && inet6_test_bit(RECVERR6, sk)) {
		WRITE_ONCE(sk->sk_err, err);
		sk_error_report(sk);
	} else {
		WRITE_ONCE(sk->sk_err_soft, err);
	}
out:
	bh_unlock_sock(sk);
	sock_put(sk);
	return 0;
}


static int tcp_v6_send_synack(const struct sock *sk, struct dst_entry *dst,
			      struct flowi *fl,
			      struct request_sock *req,
			      struct tcp_fastopen_cookie *foc,
			      enum tcp_synack_type synack_type,
			      struct sk_buff *syn_skb)
{
	struct inet_request_sock *ireq = inet_rsk(req);
	const struct ipv6_pinfo *np = tcp_inet6_sk(sk);
	struct ipv6_txoptions *opt;
	struct flowi6 *fl6 = &fl->u.ip6;
	struct sk_buff *skb;
	int err = -ENOMEM;
	u8 tclass;

	/* First, grab a route. */
	if (!dst && (dst = inet6_csk_route_req(sk, fl6, req,
					       IPPROTO_TCP)) == NULL)
		goto done;

	skb = tcp_make_synack(sk, dst, req, foc, synack_type, syn_skb);

	if (skb) {
		__tcp_v6_send_check(skb, &ireq->ir_v6_loc_addr,
				    &ireq->ir_v6_rmt_addr);

		fl6->daddr = ireq->ir_v6_rmt_addr;
		if (inet6_test_bit(REPFLOW, sk) && ireq->pktopts)
			fl6->flowlabel = ip6_flowlabel(ipv6_hdr(ireq->pktopts));

		tclass = READ_ONCE(sock_net(sk)->ipv4.sysctl_tcp_reflect_tos) ?
				(tcp_rsk(req)->syn_tos & ~INET_ECN_MASK) |
				(np->tclass & INET_ECN_MASK) :
				np->tclass;

		if (!INET_ECN_is_capable(tclass) &&
		    tcp_bpf_ca_needs_ecn((struct sock *)req))
			tclass |= INET_ECN_ECT_0;

		rcu_read_lock();
		opt = ireq->ipv6_opt;
		if (!opt)
			opt = rcu_dereference(np->opt);
		err = ip6_xmit(sk, skb, fl6, skb->mark ? : READ_ONCE(sk->sk_mark),
			       opt, tclass, READ_ONCE(sk->sk_priority));
		rcu_read_unlock();
		err = net_xmit_eval(err);
	}

done:
	return err;
}


static void tcp_v6_reqsk_destructor(struct request_sock *req)
{
	kfree(inet_rsk(req)->ipv6_opt);
	consume_skb(inet_rsk(req)->pktopts);
}

#ifdef CONFIG_TCP_MD5SIG
static struct tcp_md5sig_key *tcp_v6_md5_do_lookup(const struct sock *sk,
						   const struct in6_addr *addr,
						   int l3index)
{
	return tcp_md5_do_lookup(sk, l3index,
				 (union tcp_md5_addr *)addr, AF_INET6);
}

static struct tcp_md5sig_key *tcp_v6_md5_lookup(const struct sock *sk,
						const struct sock *addr_sk)
{
	int l3index;

	l3index = l3mdev_master_ifindex_by_index(sock_net(sk),
						 addr_sk->sk_bound_dev_if);
	return tcp_v6_md5_do_lookup(sk, &addr_sk->sk_v6_daddr,
				    l3index);
}

static int tcp_v6_parse_md5_keys(struct sock *sk, int optname,
				 sockptr_t optval, int optlen)
{
	struct tcp_md5sig cmd;
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)&cmd.tcpm_addr;
	union tcp_ao_addr *addr;
	int l3index = 0;
	u8 prefixlen;
	bool l3flag;
	u8 flags;

	if (optlen < sizeof(cmd))
		return -EINVAL;

	if (copy_from_sockptr(&cmd, optval, sizeof(cmd)))
		return -EFAULT;

	if (sin6->sin6_family != AF_INET6)
		return -EINVAL;

	flags = cmd.tcpm_flags & TCP_MD5SIG_FLAG_IFINDEX;
	l3flag = cmd.tcpm_flags & TCP_MD5SIG_FLAG_IFINDEX;

	if (optname == TCP_MD5SIG_EXT &&
	    cmd.tcpm_flags & TCP_MD5SIG_FLAG_PREFIX) {
		prefixlen = cmd.tcpm_prefixlen;
		if (prefixlen > 128 || (ipv6_addr_v4mapped(&sin6->sin6_addr) &&
					prefixlen > 32))
			return -EINVAL;
	} else {
		prefixlen = ipv6_addr_v4mapped(&sin6->sin6_addr) ? 32 : 128;
	}

	if (optname == TCP_MD5SIG_EXT && cmd.tcpm_ifindex &&
	    cmd.tcpm_flags & TCP_MD5SIG_FLAG_IFINDEX) {
		struct net_device *dev;

		rcu_read_lock();
		dev = dev_get_by_index_rcu(sock_net(sk), cmd.tcpm_ifindex);
		if (dev && netif_is_l3_master(dev))
			l3index = dev->ifindex;
		rcu_read_unlock();

		/* ok to reference set/not set outside of rcu;
		 * right now device MUST be an L3 master
		 */
		if (!dev || !l3index)
			return -EINVAL;
	}

	if (!cmd.tcpm_keylen) {
		if (ipv6_addr_v4mapped(&sin6->sin6_addr))
			return tcp_md5_do_del(sk, (union tcp_md5_addr *)&sin6->sin6_addr.s6_addr32[3],
					      AF_INET, prefixlen,
					      l3index, flags);
		return tcp_md5_do_del(sk, (union tcp_md5_addr *)&sin6->sin6_addr,
				      AF_INET6, prefixlen, l3index, flags);
	}

	if (cmd.tcpm_keylen > TCP_MD5SIG_MAXKEYLEN)
		return -EINVAL;

	if (ipv6_addr_v4mapped(&sin6->sin6_addr)) {
		addr = (union tcp_md5_addr *)&sin6->sin6_addr.s6_addr32[3];

		/* Don't allow keys for peers that have a matching TCP-AO key.
		 * See the comment in tcp_ao_add_cmd()
		 */
		if (tcp_ao_required(sk, addr, AF_INET,
				    l3flag ? l3index : -1, false))
			return -EKEYREJECTED;
		return tcp_md5_do_add(sk, addr,
				      AF_INET, prefixlen, l3index, flags,
				      cmd.tcpm_key, cmd.tcpm_keylen);
	}

	addr = (union tcp_md5_addr *)&sin6->sin6_addr;

	/* Don't allow keys for peers that have a matching TCP-AO key.
	 * See the comment in tcp_ao_add_cmd()
	 */
	if (tcp_ao_required(sk, addr, AF_INET6, l3flag ? l3index : -1, false))
		return -EKEYREJECTED;

	return tcp_md5_do_add(sk, addr, AF_INET6, prefixlen, l3index, flags,
			      cmd.tcpm_key, cmd.tcpm_keylen);
}

static int tcp_v6_md5_hash_headers(struct tcp_sigpool *hp,
				   const struct in6_addr *daddr,
				   const struct in6_addr *saddr,
				   const struct tcphdr *th, int nbytes)
{
	struct tcp6_pseudohdr *bp;
	struct scatterlist sg;
	struct tcphdr *_th;

	bp = hp->scratch;
	/* 1. TCP pseudo-header (RFC2460) */
	bp->saddr = *saddr;
	bp->daddr = *daddr;
	bp->protocol = cpu_to_be32(IPPROTO_TCP);
	bp->len = cpu_to_be32(nbytes);

	_th = (struct tcphdr *)(bp + 1);
	memcpy(_th, th, sizeof(*th));
	_th->check = 0;

	sg_init_one(&sg, bp, sizeof(*bp) + sizeof(*th));
	ahash_request_set_crypt(hp->req, &sg, NULL,
				sizeof(*bp) + sizeof(*th));
	return crypto_ahash_update(hp->req);
}

static int tcp_v6_md5_hash_hdr(char *md5_hash, const struct tcp_md5sig_key *key,
			       const struct in6_addr *daddr, struct in6_addr *saddr,
			       const struct tcphdr *th)
{
	struct tcp_sigpool hp;

	if (tcp_sigpool_start(tcp_md5_sigpool_id, &hp))
		goto clear_hash_nostart;

	if (crypto_ahash_init(hp.req))
		goto clear_hash;
	if (tcp_v6_md5_hash_headers(&hp, daddr, saddr, th, th->doff << 2))
		goto clear_hash;
	if (tcp_md5_hash_key(&hp, key))
		goto clear_hash;
	ahash_request_set_crypt(hp.req, NULL, md5_hash, 0);
	if (crypto_ahash_final(hp.req))
		goto clear_hash;

	tcp_sigpool_end(&hp);
	return 0;

clear_hash:
	tcp_sigpool_end(&hp);
clear_hash_nostart:
	memset(md5_hash, 0, 16);
	return 1;
}

static int tcp_v6_md5_hash_skb(char *md5_hash,
			       const struct tcp_md5sig_key *key,
			       const struct sock *sk,
			       const struct sk_buff *skb)
{
	const struct tcphdr *th = tcp_hdr(skb);
	const struct in6_addr *saddr, *daddr;
	struct tcp_sigpool hp;

	if (sk) { /* valid for establish/request sockets */
		saddr = &sk->sk_v6_rcv_saddr;
		daddr = &sk->sk_v6_daddr;
	} else {
		const struct ipv6hdr *ip6h = ipv6_hdr(skb);
		saddr = &ip6h->saddr;
		daddr = &ip6h->daddr;
	}

	if (tcp_sigpool_start(tcp_md5_sigpool_id, &hp))
		goto clear_hash_nostart;

	if (crypto_ahash_init(hp.req))
		goto clear_hash;

	if (tcp_v6_md5_hash_headers(&hp, daddr, saddr, th, skb->len))
		goto clear_hash;
	if (tcp_sigpool_hash_skb_data(&hp, skb, th->doff << 2))
		goto clear_hash;
	if (tcp_md5_hash_key(&hp, key))
		goto clear_hash;
	ahash_request_set_crypt(hp.req, NULL, md5_hash, 0);
	if (crypto_ahash_final(hp.req))
		goto clear_hash;

	tcp_sigpool_end(&hp);
	return 0;

clear_hash:
	tcp_sigpool_end(&hp);
clear_hash_nostart:
	memset(md5_hash, 0, 16);
	return 1;
}
#endif

static void tcp_v6_init_req(struct request_sock *req,
			    const struct sock *sk_listener,
			    struct sk_buff *skb,
			    u32 tw_isn)
{
	bool l3_slave = ipv6_l3mdev_skb(TCP_SKB_CB(skb)->header.h6.flags);
	struct inet_request_sock *ireq = inet_rsk(req);
	const struct ipv6_pinfo *np = tcp_inet6_sk(sk_listener);

	ireq->ir_v6_rmt_addr = ipv6_hdr(skb)->saddr;
	ireq->ir_v6_loc_addr = ipv6_hdr(skb)->daddr;

	/* So that link locals have meaning */
	if ((!sk_listener->sk_bound_dev_if || l3_slave) &&
	    ipv6_addr_type(&ireq->ir_v6_rmt_addr) & IPV6_ADDR_LINKLOCAL)
		ireq->ir_iif = tcp_v6_iif(skb);

	if (!tw_isn &&
	    (ipv6_opt_accepted(sk_listener, skb, &TCP_SKB_CB(skb)->header.h6) ||
	     np->rxopt.bits.rxinfo ||
	     np->rxopt.bits.rxoinfo || np->rxopt.bits.rxhlim ||
	     np->rxopt.bits.rxohlim || inet6_test_bit(REPFLOW, sk_listener))) {
		refcount_inc(&skb->users);
		ireq->pktopts = skb;
	}
}

static struct dst_entry *tcp_v6_route_req(const struct sock *sk,
					  struct sk_buff *skb,
					  struct flowi *fl,
					  struct request_sock *req,
					  u32 tw_isn)
{
	tcp_v6_init_req(req, sk, skb, tw_isn);

	if (security_inet_conn_request(sk, skb, req))
		return NULL;

	return inet6_csk_route_req(sk, &fl->u.ip6, req, IPPROTO_TCP);
}

struct request_sock_ops tcp6_request_sock_ops __read_mostly = {
	.family		=	AF_INET6,
	.obj_size	=	sizeof(struct tcp6_request_sock),
	.rtx_syn_ack	=	tcp_rtx_synack,
	.send_ack	=	tcp_v6_reqsk_send_ack,
	.destructor	=	tcp_v6_reqsk_destructor,
	.send_reset	=	tcp_v6_send_reset,
	.syn_ack_timeout =	tcp_syn_ack_timeout,
};

const struct tcp_request_sock_ops tcp_request_sock_ipv6_ops = {
	.mss_clamp	=	IPV6_MIN_MTU - sizeof(struct tcphdr) -
				sizeof(struct ipv6hdr),
#ifdef CONFIG_TCP_MD5SIG
	.req_md5_lookup	=	tcp_v6_md5_lookup,
	.calc_md5_hash	=	tcp_v6_md5_hash_skb,
#endif
#ifdef CONFIG_TCP_AO
	.ao_lookup	=	tcp_v6_ao_lookup_rsk,
	.ao_calc_key	=	tcp_v6_ao_calc_key_rsk,
	.ao_synack_hash =	tcp_v6_ao_synack_hash,
#endif
#ifdef CONFIG_SYN_COOKIES
	.cookie_init_seq =	cookie_v6_init_sequence,
#endif
	.route_req	=	tcp_v6_route_req,
	.init_seq	=	tcp_v6_init_seq,
	.init_ts_off	=	tcp_v6_init_ts_off,
	.send_synack	=	tcp_v6_send_synack,
};

static void tcp_v6_send_response(const struct sock *sk, struct sk_buff *skb, u32 seq,
				 u32 ack, u32 win, u32 tsval, u32 tsecr,
				 int oif, int rst, u8 tclass, __be32 label,
				 u32 priority, u32 txhash, struct tcp_key *key)
{
	const struct tcphdr *th = tcp_hdr(skb);
	struct tcphdr *t1;
	struct sk_buff *buff;
	struct flowi6 fl6;
	struct net *net = sk ? sock_net(sk) : dev_net(skb_dst(skb)->dev);
	struct sock *ctl_sk = net->ipv6.tcp_sk;
	unsigned int tot_len = sizeof(struct tcphdr);
	__be32 mrst = 0, *topt;
	struct dst_entry *dst;
	__u32 mark = 0;

	if (tsecr)
		tot_len += TCPOLEN_TSTAMP_ALIGNED;
	if (tcp_key_is_md5(key))
		tot_len += TCPOLEN_MD5SIG_ALIGNED;
	if (tcp_key_is_ao(key))
		tot_len += tcp_ao_len_aligned(key->ao_key);

#ifdef CONFIG_MPTCP
	if (rst && !tcp_key_is_md5(key)) {
		mrst = mptcp_reset_option(skb);

		if (mrst)
			tot_len += sizeof(__be32);
	}
#endif

	buff = alloc_skb(MAX_TCP_HEADER, GFP_ATOMIC);
	if (!buff)
		return;

	skb_reserve(buff, MAX_TCP_HEADER);

	t1 = skb_push(buff, tot_len);
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

	if (tsecr) {
		*topt++ = htonl((TCPOPT_NOP << 24) | (TCPOPT_NOP << 16) |
				(TCPOPT_TIMESTAMP << 8) | TCPOLEN_TIMESTAMP);
		*topt++ = htonl(tsval);
		*topt++ = htonl(tsecr);
	}

	if (mrst)
		*topt++ = mrst;

#ifdef CONFIG_TCP_MD5SIG
	if (tcp_key_is_md5(key)) {
		*topt++ = htonl((TCPOPT_NOP << 24) | (TCPOPT_NOP << 16) |
				(TCPOPT_MD5SIG << 8) | TCPOLEN_MD5SIG);
		tcp_v6_md5_hash_hdr((__u8 *)topt, key->md5_key,
				    &ipv6_hdr(skb)->saddr,
				    &ipv6_hdr(skb)->daddr, t1);
	}
#endif
#ifdef CONFIG_TCP_AO
	if (tcp_key_is_ao(key)) {
		*topt++ = htonl((TCPOPT_AO << 24) |
				(tcp_ao_len(key->ao_key) << 16) |
				(key->ao_key->sndid << 8) |
				(key->rcv_next));

		tcp_ao_hash_hdr(AF_INET6, (char *)topt, key->ao_key,
				key->traffic_key,
				(union tcp_ao_addr *)&ipv6_hdr(skb)->saddr,
				(union tcp_ao_addr *)&ipv6_hdr(skb)->daddr,
				t1, key->sne);
	}
#endif

	memset(&fl6, 0, sizeof(fl6));
	fl6.daddr = ipv6_hdr(skb)->saddr;
	fl6.saddr = ipv6_hdr(skb)->daddr;
	fl6.flowlabel = label;

	buff->ip_summed = CHECKSUM_PARTIAL;

	__tcp_v6_send_check(buff, &fl6.saddr, &fl6.daddr);

	fl6.flowi6_proto = IPPROTO_TCP;
	if (rt6_need_strict(&fl6.daddr) && !oif)
		fl6.flowi6_oif = tcp_v6_iif(skb);
	else {
		if (!oif && netif_index_is_l3_master(net, skb->skb_iif))
			oif = skb->skb_iif;

		fl6.flowi6_oif = oif;
	}

	if (sk) {
		if (sk->sk_state == TCP_TIME_WAIT)
			mark = inet_twsk(sk)->tw_mark;
		else
			mark = READ_ONCE(sk->sk_mark);
		skb_set_delivery_time(buff, tcp_transmit_time(sk), SKB_CLOCK_MONOTONIC);
	}
	if (txhash) {
		/* autoflowlabel/skb_get_hash_flowi6 rely on buff->hash */
		skb_set_hash(buff, txhash, PKT_HASH_TYPE_L4);
	}
	fl6.flowi6_mark = IP6_REPLY_MARK(net, skb->mark) ?: mark;
	fl6.fl6_dport = t1->dest;
	fl6.fl6_sport = t1->source;
	fl6.flowi6_uid = sock_net_uid(net, sk && sk_fullsock(sk) ? sk : NULL);
	security_skb_classify_flow(skb, flowi6_to_flowi_common(&fl6));

	/* Pass a socket to ip6_dst_lookup either it is for RST
	 * Underlying function will use this to retrieve the network
	 * namespace
	 */
	if (sk && sk->sk_state != TCP_TIME_WAIT)
		dst = ip6_dst_lookup_flow(net, sk, &fl6, NULL); /*sk's xfrm_policy can be referred*/
	else
		dst = ip6_dst_lookup_flow(net, ctl_sk, &fl6, NULL);
	if (!IS_ERR(dst)) {
		skb_dst_set(buff, dst);
		ip6_xmit(ctl_sk, buff, &fl6, fl6.flowi6_mark, NULL,
			 tclass & ~INET_ECN_MASK, priority);
		TCP_INC_STATS(net, TCP_MIB_OUTSEGS);
		if (rst)
			TCP_INC_STATS(net, TCP_MIB_OUTRSTS);
		return;
	}

	kfree_skb(buff);
}

static void tcp_v6_send_reset(const struct sock *sk, struct sk_buff *skb,
			      enum sk_rst_reason reason)
{
	const struct tcphdr *th = tcp_hdr(skb);
	struct ipv6hdr *ipv6h = ipv6_hdr(skb);
	const __u8 *md5_hash_location = NULL;
#if defined(CONFIG_TCP_MD5SIG) || defined(CONFIG_TCP_AO)
	bool allocated_traffic_key = false;
#endif
	const struct tcp_ao_hdr *aoh;
	struct tcp_key key = {};
	u32 seq = 0, ack_seq = 0;
	__be32 label = 0;
	u32 priority = 0;
	struct net *net;
	u32 txhash = 0;
	int oif = 0;
#ifdef CONFIG_TCP_MD5SIG
	unsigned char newhash[16];
	int genhash;
	struct sock *sk1 = NULL;
#endif

	if (th->rst)
		return;

	/* If sk not NULL, it means we did a successful lookup and incoming
	 * route had to be correct. prequeue might have dropped our dst.
	 */
	if (!sk && !ipv6_unicast_destination(skb))
		return;

	net = sk ? sock_net(sk) : dev_net(skb_dst(skb)->dev);
	/* Invalid TCP option size or twice included auth */
	if (tcp_parse_auth_options(th, &md5_hash_location, &aoh))
		return;
#if defined(CONFIG_TCP_MD5SIG) || defined(CONFIG_TCP_AO)
	rcu_read_lock();
#endif
#ifdef CONFIG_TCP_MD5SIG
	if (sk && sk_fullsock(sk)) {
		int l3index;

		/* sdif set, means packet ingressed via a device
		 * in an L3 domain and inet_iif is set to it.
		 */
		l3index = tcp_v6_sdif(skb) ? tcp_v6_iif_l3_slave(skb) : 0;
		key.md5_key = tcp_v6_md5_do_lookup(sk, &ipv6h->saddr, l3index);
		if (key.md5_key)
			key.type = TCP_KEY_MD5;
	} else if (md5_hash_location) {
		int dif = tcp_v6_iif_l3_slave(skb);
		int sdif = tcp_v6_sdif(skb);
		int l3index;

		/*
		 * active side is lost. Try to find listening socket through
		 * source port, and then find md5 key through listening socket.
		 * we are not loose security here:
		 * Incoming packet is checked with md5 hash with finding key,
		 * no RST generated if md5 hash doesn't match.
		 */
		sk1 = inet6_lookup_listener(net, net->ipv4.tcp_death_row.hashinfo,
					    NULL, 0, &ipv6h->saddr, th->source,
					    &ipv6h->daddr, ntohs(th->source),
					    dif, sdif);
		if (!sk1)
			goto out;

		/* sdif set, means packet ingressed via a device
		 * in an L3 domain and dif is set to it.
		 */
		l3index = tcp_v6_sdif(skb) ? dif : 0;

		key.md5_key = tcp_v6_md5_do_lookup(sk1, &ipv6h->saddr, l3index);
		if (!key.md5_key)
			goto out;
		key.type = TCP_KEY_MD5;

		genhash = tcp_v6_md5_hash_skb(newhash, key.md5_key, NULL, skb);
		if (genhash || memcmp(md5_hash_location, newhash, 16) != 0)
			goto out;
	}
#endif

	if (th->ack)
		seq = ntohl(th->ack_seq);
	else
		ack_seq = ntohl(th->seq) + th->syn + th->fin + skb->len -
			  (th->doff << 2);

#ifdef CONFIG_TCP_AO
	if (aoh) {
		int l3index;

		l3index = tcp_v6_sdif(skb) ? tcp_v6_iif_l3_slave(skb) : 0;
		if (tcp_ao_prepare_reset(sk, skb, aoh, l3index, seq,
					 &key.ao_key, &key.traffic_key,
					 &allocated_traffic_key,
					 &key.rcv_next, &key.sne))
			goto out;
		key.type = TCP_KEY_AO;
	}
#endif

	if (sk) {
		oif = sk->sk_bound_dev_if;
		if (sk_fullsock(sk)) {
			if (inet6_test_bit(REPFLOW, sk))
				label = ip6_flowlabel(ipv6h);
			priority = READ_ONCE(sk->sk_priority);
			txhash = sk->sk_txhash;
		}
		if (sk->sk_state == TCP_TIME_WAIT) {
			label = cpu_to_be32(inet_twsk(sk)->tw_flowlabel);
			priority = inet_twsk(sk)->tw_priority;
			txhash = inet_twsk(sk)->tw_txhash;
		}
	} else {
		if (net->ipv6.sysctl.flowlabel_reflect & FLOWLABEL_REFLECT_TCP_RESET)
			label = ip6_flowlabel(ipv6h);
	}

	trace_tcp_send_reset(sk, skb, reason);

	tcp_v6_send_response(sk, skb, seq, ack_seq, 0, 0, 0, oif, 1,
			     ipv6_get_dsfield(ipv6h), label, priority, txhash,
			     &key);

#if defined(CONFIG_TCP_MD5SIG) || defined(CONFIG_TCP_AO)
out:
	if (allocated_traffic_key)
		kfree(key.traffic_key);
	rcu_read_unlock();
#endif
}

static void tcp_v6_send_ack(const struct sock *sk, struct sk_buff *skb, u32 seq,
			    u32 ack, u32 win, u32 tsval, u32 tsecr, int oif,
			    struct tcp_key *key, u8 tclass,
			    __be32 label, u32 priority, u32 txhash)
{
	tcp_v6_send_response(sk, skb, seq, ack, win, tsval, tsecr, oif, 0,
			     tclass, label, priority, txhash, key);
}

static void tcp_v6_timewait_ack(struct sock *sk, struct sk_buff *skb)
{
	struct inet_timewait_sock *tw = inet_twsk(sk);
	struct tcp_timewait_sock *tcptw = tcp_twsk(sk);
	struct tcp_key key = {};
#ifdef CONFIG_TCP_AO
	struct tcp_ao_info *ao_info;

	if (static_branch_unlikely(&tcp_ao_needed.key)) {

		/* FIXME: the segment to-be-acked is not verified yet */
		ao_info = rcu_dereference(tcptw->ao_info);
		if (ao_info) {
			const struct tcp_ao_hdr *aoh;

			/* Invalid TCP option size or twice included auth */
			if (tcp_parse_auth_options(tcp_hdr(skb), NULL, &aoh))
				goto out;
			if (aoh)
				key.ao_key = tcp_ao_established_key(sk, ao_info,
								    aoh->rnext_keyid, -1);
		}
	}
	if (key.ao_key) {
		struct tcp_ao_key *rnext_key;

		key.traffic_key = snd_other_key(key.ao_key);
		/* rcv_next switches to our rcv_next */
		rnext_key = READ_ONCE(ao_info->rnext_key);
		key.rcv_next = rnext_key->rcvid;
		key.sne = READ_ONCE(ao_info->snd_sne);
		key.type = TCP_KEY_AO;
#else
	if (0) {
#endif
#ifdef CONFIG_TCP_MD5SIG
	} else if (static_branch_unlikely(&tcp_md5_needed.key)) {
		key.md5_key = tcp_twsk_md5_key(tcptw);
		if (key.md5_key)
			key.type = TCP_KEY_MD5;
#endif
	}

	tcp_v6_send_ack(sk, skb, tcptw->tw_snd_nxt,
			READ_ONCE(tcptw->tw_rcv_nxt),
			tcptw->tw_rcv_wnd >> tw->tw_rcv_wscale,
			tcp_tw_tsval(tcptw),
			READ_ONCE(tcptw->tw_ts_recent), tw->tw_bound_dev_if,
			&key, tw->tw_tclass, cpu_to_be32(tw->tw_flowlabel),
			tw->tw_priority, tw->tw_txhash);

#ifdef CONFIG_TCP_AO
out:
#endif
	inet_twsk_put(tw);
}

static void tcp_v6_reqsk_send_ack(const struct sock *sk, struct sk_buff *skb,
				  struct request_sock *req)
{
	struct tcp_key key = {};

#ifdef CONFIG_TCP_AO
	if (static_branch_unlikely(&tcp_ao_needed.key) &&
	    tcp_rsk_used_ao(req)) {
		const struct in6_addr *addr = &ipv6_hdr(skb)->saddr;
		const struct tcp_ao_hdr *aoh;
		int l3index;

		l3index = tcp_v6_sdif(skb) ? tcp_v6_iif_l3_slave(skb) : 0;
		/* Invalid TCP option size or twice included auth */
		if (tcp_parse_auth_options(tcp_hdr(skb), NULL, &aoh))
			return;
		if (!aoh)
			return;
		key.ao_key = tcp_ao_do_lookup(sk, l3index,
					      (union tcp_ao_addr *)addr,
					      AF_INET6, aoh->rnext_keyid, -1);
		if (unlikely(!key.ao_key)) {
			/* Send ACK with any matching MKT for the peer */
			key.ao_key = tcp_ao_do_lookup(sk, l3index,
						      (union tcp_ao_addr *)addr,
						      AF_INET6, -1, -1);
			/* Matching key disappeared (user removed the key?)
			 * let the handshake timeout.
			 */
			if (!key.ao_key) {
				net_info_ratelimited("TCP-AO key for (%pI6, %d)->(%pI6, %d) suddenly disappeared, won't ACK new connection\n",
						     addr,
						     ntohs(tcp_hdr(skb)->source),
						     &ipv6_hdr(skb)->daddr,
						     ntohs(tcp_hdr(skb)->dest));
				return;
			}
		}
		key.traffic_key = kmalloc(tcp_ao_digest_size(key.ao_key), GFP_ATOMIC);
		if (!key.traffic_key)
			return;

		key.type = TCP_KEY_AO;
		key.rcv_next = aoh->keyid;
		tcp_v6_ao_calc_key_rsk(key.ao_key, key.traffic_key, req);
#else
	if (0) {
#endif
#ifdef CONFIG_TCP_MD5SIG
	} else if (static_branch_unlikely(&tcp_md5_needed.key)) {
		int l3index = tcp_v6_sdif(skb) ? tcp_v6_iif_l3_slave(skb) : 0;

		key.md5_key = tcp_v6_md5_do_lookup(sk, &ipv6_hdr(skb)->saddr,
						   l3index);
		if (key.md5_key)
			key.type = TCP_KEY_MD5;
#endif
	}

	/* sk->sk_state == TCP_LISTEN -> for regular TCP_SYN_RECV
	 * sk->sk_state == TCP_SYN_RECV -> for Fast Open.
	 */
	tcp_v6_send_ack(sk, skb, (sk->sk_state == TCP_LISTEN) ?
			tcp_rsk(req)->snt_isn + 1 : tcp_sk(sk)->snd_nxt,
			tcp_rsk(req)->rcv_nxt,
			tcp_synack_window(req) >> inet_rsk(req)->rcv_wscale,
			tcp_rsk_tsval(tcp_rsk(req)),
			READ_ONCE(req->ts_recent), sk->sk_bound_dev_if,
			&key, ipv6_get_dsfield(ipv6_hdr(skb)), 0,
			READ_ONCE(sk->sk_priority),
			READ_ONCE(tcp_rsk(req)->txhash));
	if (tcp_key_is_ao(&key))
		kfree(key.traffic_key);
}


static struct sock *tcp_v6_cookie_check(struct sock *sk, struct sk_buff *skb)
{
#ifdef CONFIG_SYN_COOKIES
	const struct tcphdr *th = tcp_hdr(skb);

	if (!th->syn)
		sk = cookie_v6_check(sk, skb);
#endif
	return sk;
}

u16 tcp_v6_get_syncookie(struct sock *sk, struct ipv6hdr *iph,
			 struct tcphdr *th, u32 *cookie)
{
	u16 mss = 0;
#ifdef CONFIG_SYN_COOKIES
	mss = tcp_get_syncookie_mss(&tcp6_request_sock_ops,
				    &tcp_request_sock_ipv6_ops, sk, th);
	if (mss) {
		*cookie = __cookie_v6_init_sequence(iph, th, &mss);
		tcp_synq_overflow(sk);
	}
#endif
	return mss;
}

static int tcp_v6_conn_request(struct sock *sk, struct sk_buff *skb)
{
	if (skb->protocol == htons(ETH_P_IP))
		return tcp_v4_conn_request(sk, skb);

	if (!ipv6_unicast_destination(skb))
		goto drop;

	if (ipv6_addr_v4mapped(&ipv6_hdr(skb)->saddr)) {
		__IP6_INC_STATS(sock_net(sk), NULL, IPSTATS_MIB_INHDRERRORS);
		return 0;
	}

	return tcp_conn_request(&tcp6_request_sock_ops,
				&tcp_request_sock_ipv6_ops, sk, skb);

drop:
	tcp_listendrop(sk);
	return 0; /* don't send reset */
}

static void tcp_v6_restore_cb(struct sk_buff *skb)
{
	/* We need to move header back to the beginning if xfrm6_policy_check()
	 * and tcp_v6_fill_cb() are going to be called again.
	 * ip6_datagram_recv_specific_ctl() also expects IP6CB to be there.
	 */
	memmove(IP6CB(skb), &TCP_SKB_CB(skb)->header.h6,
		sizeof(struct inet6_skb_parm));
}

static struct sock *tcp_v6_syn_recv_sock(const struct sock *sk, struct sk_buff *skb,
					 struct request_sock *req,
					 struct dst_entry *dst,
					 struct request_sock *req_unhash,
					 bool *own_req)
{
	struct inet_request_sock *ireq;
	struct ipv6_pinfo *newnp;
	const struct ipv6_pinfo *np = tcp_inet6_sk(sk);
	struct ipv6_txoptions *opt;
	struct inet_sock *newinet;
	bool found_dup_sk = false;
	struct tcp_sock *newtp;
	struct sock *newsk;
#ifdef CONFIG_TCP_MD5SIG
	struct tcp_md5sig_key *key;
	int l3index;
#endif
	struct flowi6 fl6;

	if (skb->protocol == htons(ETH_P_IP)) {
		/*
		 *	v6 mapped
		 */

		newsk = tcp_v4_syn_recv_sock(sk, skb, req, dst,
					     req_unhash, own_req);

		if (!newsk)
			return NULL;

		inet_sk(newsk)->pinet6 = tcp_inet6_sk(newsk);

		newnp = tcp_inet6_sk(newsk);
		newtp = tcp_sk(newsk);

		memcpy(newnp, np, sizeof(struct ipv6_pinfo));

		newnp->saddr = newsk->sk_v6_rcv_saddr;

		inet_csk(newsk)->icsk_af_ops = &ipv6_mapped;
		if (sk_is_mptcp(newsk))
			mptcpv6_handle_mapped(newsk, true);
		newsk->sk_backlog_rcv = tcp_v4_do_rcv;
#if defined(CONFIG_TCP_MD5SIG) || defined(CONFIG_TCP_AO)
		newtp->af_specific = &tcp_sock_ipv6_mapped_specific;
#endif

		newnp->ipv6_mc_list = NULL;
		newnp->ipv6_ac_list = NULL;
		newnp->ipv6_fl_list = NULL;
		newnp->pktoptions  = NULL;
		newnp->opt	   = NULL;
		newnp->mcast_oif   = inet_iif(skb);
		newnp->mcast_hops  = ip_hdr(skb)->ttl;
		newnp->rcv_flowinfo = 0;
		if (inet6_test_bit(REPFLOW, sk))
			newnp->flow_label = 0;

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

	ireq = inet_rsk(req);

	if (sk_acceptq_is_full(sk))
		goto out_overflow;

	if (!dst) {
		dst = inet6_csk_route_req(sk, &fl6, req, IPPROTO_TCP);
		if (!dst)
			goto out;
	}

	newsk = tcp_create_openreq_child(sk, req, skb);
	if (!newsk)
		goto out_nonewsk;

	/*
	 * No need to charge this sock to the relevant IPv6 refcnt debug socks
	 * count here, tcp_create_openreq_child now does this for us, see the
	 * comment in that function for the gory details. -acme
	 */

	newsk->sk_gso_type = SKB_GSO_TCPV6;
	inet6_sk_rx_dst_set(newsk, skb);

	inet_sk(newsk)->pinet6 = tcp_inet6_sk(newsk);

	newtp = tcp_sk(newsk);
	newinet = inet_sk(newsk);
	newnp = tcp_inet6_sk(newsk);

	memcpy(newnp, np, sizeof(struct ipv6_pinfo));

	ip6_dst_store(newsk, dst, NULL, NULL);

	newsk->sk_v6_daddr = ireq->ir_v6_rmt_addr;
	newnp->saddr = ireq->ir_v6_loc_addr;
	newsk->sk_v6_rcv_saddr = ireq->ir_v6_loc_addr;
	newsk->sk_bound_dev_if = ireq->ir_iif;

	/* Now IPv6 options...

	   First: no IPv4 options.
	 */
	newinet->inet_opt = NULL;
	newnp->ipv6_mc_list = NULL;
	newnp->ipv6_ac_list = NULL;
	newnp->ipv6_fl_list = NULL;

	/* Clone RX bits */
	newnp->rxopt.all = np->rxopt.all;

	newnp->pktoptions = NULL;
	newnp->opt	  = NULL;
	newnp->mcast_oif  = tcp_v6_iif(skb);
	newnp->mcast_hops = ipv6_hdr(skb)->hop_limit;
	newnp->rcv_flowinfo = ip6_flowinfo(ipv6_hdr(skb));
	if (inet6_test_bit(REPFLOW, sk))
		newnp->flow_label = ip6_flowlabel(ipv6_hdr(skb));

	/* Set ToS of the new socket based upon the value of incoming SYN.
	 * ECT bits are set later in tcp_init_transfer().
	 */
	if (READ_ONCE(sock_net(sk)->ipv4.sysctl_tcp_reflect_tos))
		newnp->tclass = tcp_rsk(req)->syn_tos & ~INET_ECN_MASK;

	/* Clone native IPv6 options from listening socket (if any)

	   Yes, keeping reference count would be much more clever,
	   but we make one more one thing there: reattach optmem
	   to newsk.
	 */
	opt = ireq->ipv6_opt;
	if (!opt)
		opt = rcu_dereference(np->opt);
	if (opt) {
		opt = ipv6_dup_options(newsk, opt);
		RCU_INIT_POINTER(newnp->opt, opt);
	}
	inet_csk(newsk)->icsk_ext_hdr_len = 0;
	if (opt)
		inet_csk(newsk)->icsk_ext_hdr_len = opt->opt_nflen +
						    opt->opt_flen;

	tcp_ca_openreq_child(newsk, dst);

	tcp_sync_mss(newsk, dst_mtu(dst));
	newtp->advmss = tcp_mss_clamp(tcp_sk(sk), dst_metric_advmss(dst));

	tcp_initialize_rcv_mss(newsk);

	newinet->inet_daddr = newinet->inet_saddr = LOOPBACK4_IPV6;
	newinet->inet_rcv_saddr = LOOPBACK4_IPV6;

#ifdef CONFIG_TCP_MD5SIG
	l3index = l3mdev_master_ifindex_by_index(sock_net(sk), ireq->ir_iif);

	if (!tcp_rsk_used_ao(req)) {
		/* Copy over the MD5 key from the original socket */
		key = tcp_v6_md5_do_lookup(sk, &newsk->sk_v6_daddr, l3index);
		if (key) {
			const union tcp_md5_addr *addr;

			addr = (union tcp_md5_addr *)&newsk->sk_v6_daddr;
			if (tcp_md5_key_copy(newsk, addr, AF_INET6, 128, l3index, key)) {
				inet_csk_prepare_forced_close(newsk);
				tcp_done(newsk);
				goto out;
			}
		}
	}
#endif
#ifdef CONFIG_TCP_AO
	/* Copy over tcp_ao_info if any */
	if (tcp_ao_copy_all_matching(sk, newsk, req, skb, AF_INET6))
		goto out; /* OOM */
#endif

	if (__inet_inherit_port(sk, newsk) < 0) {
		inet_csk_prepare_forced_close(newsk);
		tcp_done(newsk);
		goto out;
	}
	*own_req = inet_ehash_nolisten(newsk, req_to_sk(req_unhash),
				       &found_dup_sk);
	if (*own_req) {
		tcp_move_syn(newtp, req);

		/* Clone pktoptions received with SYN, if we own the req */
		if (ireq->pktopts) {
			newnp->pktoptions = skb_clone_and_charge_r(ireq->pktopts, newsk);
			consume_skb(ireq->pktopts);
			ireq->pktopts = NULL;
			if (newnp->pktoptions)
				tcp_v6_restore_cb(newnp->pktoptions);
		}
	} else {
		if (!req_unhash && found_dup_sk) {
			/* This code path should only be executed in the
			 * syncookie case only
			 */
			bh_unlock_sock(newsk);
			sock_put(newsk);
			newsk = NULL;
		}
	}

	return newsk;

out_overflow:
	__NET_INC_STATS(sock_net(sk), LINUX_MIB_LISTENOVERFLOWS);
out_nonewsk:
	dst_release(dst);
out:
	tcp_listendrop(sk);
	return NULL;
}

INDIRECT_CALLABLE_DECLARE(struct dst_entry *ipv4_dst_check(struct dst_entry *,
							   u32));
/* The socket must have it's spinlock held when we get
 * here, unless it is a TCP_LISTEN socket.
 *
 * We have a potential double-lock case here, so even when
 * doing backlog processing we use the BH locking scheme.
 * This is because we cannot sleep with the original spinlock
 * held.
 */
INDIRECT_CALLABLE_SCOPE
int tcp_v6_do_rcv(struct sock *sk, struct sk_buff *skb)
{
	struct ipv6_pinfo *np = tcp_inet6_sk(sk);
	struct sk_buff *opt_skb = NULL;
	enum skb_drop_reason reason;
	struct tcp_sock *tp;

	/* Imagine: socket is IPv6. IPv4 packet arrives,
	   goes to IPv4 receive handler and backlogged.
	   From backlog it always goes here. Kerboom...
	   Fortunately, tcp_rcv_established and rcv_established
	   handle them correctly, but it is not case with
	   tcp_v6_hnd_req and tcp_v6_send_reset().   --ANK
	 */

	if (skb->protocol == htons(ETH_P_IP))
		return tcp_v4_do_rcv(sk, skb);

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
	if (np->rxopt.all && sk->sk_state != TCP_LISTEN)
		opt_skb = skb_clone_and_charge_r(skb, sk);

	if (sk->sk_state == TCP_ESTABLISHED) { /* Fast path */
		struct dst_entry *dst;

		dst = rcu_dereference_protected(sk->sk_rx_dst,
						lockdep_sock_is_held(sk));

		sock_rps_save_rxhash(sk, skb);
		sk_mark_napi_id(sk, skb);
		if (dst) {
			if (sk->sk_rx_dst_ifindex != skb->skb_iif ||
			    INDIRECT_CALL_1(dst->ops->check, ip6_dst_check,
					    dst, sk->sk_rx_dst_cookie) == NULL) {
				RCU_INIT_POINTER(sk->sk_rx_dst, NULL);
				dst_release(dst);
			}
		}

		tcp_rcv_established(sk, skb);
		if (opt_skb)
			goto ipv6_pktoptions;
		return 0;
	}

	if (tcp_checksum_complete(skb))
		goto csum_err;

	if (sk->sk_state == TCP_LISTEN) {
		struct sock *nsk = tcp_v6_cookie_check(sk, skb);

		if (nsk != sk) {
			if (nsk) {
				reason = tcp_child_process(sk, nsk, skb);
				if (reason)
					goto reset;
			}
			return 0;
		}
	} else
		sock_rps_save_rxhash(sk, skb);

	reason = tcp_rcv_state_process(sk, skb);
	if (reason)
		goto reset;
	if (opt_skb)
		goto ipv6_pktoptions;
	return 0;

reset:
	tcp_v6_send_reset(sk, skb, sk_rst_convert_drop_reason(reason));
discard:
	if (opt_skb)
		__kfree_skb(opt_skb);
	sk_skb_reason_drop(sk, skb, reason);
	return 0;
csum_err:
	reason = SKB_DROP_REASON_TCP_CSUM;
	trace_tcp_bad_csum(skb);
	TCP_INC_STATS(sock_net(sk), TCP_MIB_CSUMERRORS);
	TCP_INC_STATS(sock_net(sk), TCP_MIB_INERRS);
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
			WRITE_ONCE(np->mcast_oif, tcp_v6_iif(opt_skb));
		if (np->rxopt.bits.rxhlim || np->rxopt.bits.rxohlim)
			WRITE_ONCE(np->mcast_hops,
				   ipv6_hdr(opt_skb)->hop_limit);
		if (np->rxopt.bits.rxflow || np->rxopt.bits.rxtclass)
			np->rcv_flowinfo = ip6_flowinfo(ipv6_hdr(opt_skb));
		if (inet6_test_bit(REPFLOW, sk))
			np->flow_label = ip6_flowlabel(ipv6_hdr(opt_skb));
		if (ipv6_opt_accepted(sk, opt_skb, &TCP_SKB_CB(opt_skb)->header.h6)) {
			tcp_v6_restore_cb(opt_skb);
			opt_skb = xchg(&np->pktoptions, opt_skb);
		} else {
			__kfree_skb(opt_skb);
			opt_skb = xchg(&np->pktoptions, NULL);
		}
	}

	consume_skb(opt_skb);
	return 0;
}

static void tcp_v6_fill_cb(struct sk_buff *skb, const struct ipv6hdr *hdr,
			   const struct tcphdr *th)
{
	/* This is tricky: we move IP6CB at its correct location into
	 * TCP_SKB_CB(). It must be done after xfrm6_policy_check(), because
	 * _decode_session6() uses IP6CB().
	 * barrier() makes sure compiler won't play aliasing games.
	 */
	memmove(&TCP_SKB_CB(skb)->header.h6, IP6CB(skb),
		sizeof(struct inet6_skb_parm));
	barrier();

	TCP_SKB_CB(skb)->seq = ntohl(th->seq);
	TCP_SKB_CB(skb)->end_seq = (TCP_SKB_CB(skb)->seq + th->syn + th->fin +
				    skb->len - th->doff*4);
	TCP_SKB_CB(skb)->ack_seq = ntohl(th->ack_seq);
	TCP_SKB_CB(skb)->tcp_flags = tcp_flag_byte(th);
	TCP_SKB_CB(skb)->ip_dsfield = ipv6_get_dsfield(hdr);
	TCP_SKB_CB(skb)->sacked = 0;
	TCP_SKB_CB(skb)->has_rxtstamp =
			skb->tstamp || skb_hwtstamps(skb)->hwtstamp;
}

INDIRECT_CALLABLE_SCOPE int tcp_v6_rcv(struct sk_buff *skb)
{
	enum skb_drop_reason drop_reason;
	int sdif = inet6_sdif(skb);
	int dif = inet6_iif(skb);
	const struct tcphdr *th;
	const struct ipv6hdr *hdr;
	struct sock *sk = NULL;
	bool refcounted;
	int ret;
	u32 isn;
	struct net *net = dev_net(skb->dev);

	drop_reason = SKB_DROP_REASON_NOT_SPECIFIED;
	if (skb->pkt_type != PACKET_HOST)
		goto discard_it;

	/*
	 *	Count it even if it's bad.
	 */
	__TCP_INC_STATS(net, TCP_MIB_INSEGS);

	if (!pskb_may_pull(skb, sizeof(struct tcphdr)))
		goto discard_it;

	th = (const struct tcphdr *)skb->data;

	if (unlikely(th->doff < sizeof(struct tcphdr) / 4)) {
		drop_reason = SKB_DROP_REASON_PKT_TOO_SMALL;
		goto bad_packet;
	}
	if (!pskb_may_pull(skb, th->doff*4))
		goto discard_it;

	if (skb_checksum_init(skb, IPPROTO_TCP, ip6_compute_pseudo))
		goto csum_error;

	th = (const struct tcphdr *)skb->data;
	hdr = ipv6_hdr(skb);

lookup:
	sk = __inet6_lookup_skb(net->ipv4.tcp_death_row.hashinfo, skb, __tcp_hdrlen(th),
				th->source, th->dest, inet6_iif(skb), sdif,
				&refcounted);
	if (!sk)
		goto no_tcp_socket;

	if (sk->sk_state == TCP_TIME_WAIT)
		goto do_time_wait;

	if (sk->sk_state == TCP_NEW_SYN_RECV) {
		struct request_sock *req = inet_reqsk(sk);
		bool req_stolen = false;
		struct sock *nsk;

		sk = req->rsk_listener;
		if (!xfrm6_policy_check(sk, XFRM_POLICY_IN, skb))
			drop_reason = SKB_DROP_REASON_XFRM_POLICY;
		else
			drop_reason = tcp_inbound_hash(sk, req, skb,
						       &hdr->saddr, &hdr->daddr,
						       AF_INET6, dif, sdif);
		if (drop_reason) {
			sk_drops_add(sk, skb);
			reqsk_put(req);
			goto discard_it;
		}
		if (tcp_checksum_complete(skb)) {
			reqsk_put(req);
			goto csum_error;
		}
		if (unlikely(sk->sk_state != TCP_LISTEN)) {
			nsk = reuseport_migrate_sock(sk, req_to_sk(req), skb);
			if (!nsk) {
				inet_csk_reqsk_queue_drop_and_put(sk, req);
				goto lookup;
			}
			sk = nsk;
			/* reuseport_migrate_sock() has already held one sk_refcnt
			 * before returning.
			 */
		} else {
			sock_hold(sk);
		}
		refcounted = true;
		nsk = NULL;
		if (!tcp_filter(sk, skb)) {
			th = (const struct tcphdr *)skb->data;
			hdr = ipv6_hdr(skb);
			tcp_v6_fill_cb(skb, hdr, th);
			nsk = tcp_check_req(sk, skb, req, false, &req_stolen);
		} else {
			drop_reason = SKB_DROP_REASON_SOCKET_FILTER;
		}
		if (!nsk) {
			reqsk_put(req);
			if (req_stolen) {
				/* Another cpu got exclusive access to req
				 * and created a full blown socket.
				 * Try to feed this packet to this socket
				 * instead of discarding it.
				 */
				tcp_v6_restore_cb(skb);
				sock_put(sk);
				goto lookup;
			}
			goto discard_and_relse;
		}
		nf_reset_ct(skb);
		if (nsk == sk) {
			reqsk_put(req);
			tcp_v6_restore_cb(skb);
		} else {
			drop_reason = tcp_child_process(sk, nsk, skb);
			if (drop_reason) {
				enum sk_rst_reason rst_reason;

				rst_reason = sk_rst_convert_drop_reason(drop_reason);
				tcp_v6_send_reset(nsk, skb, rst_reason);
				goto discard_and_relse;
			}
			sock_put(sk);
			return 0;
		}
	}

process:
	if (static_branch_unlikely(&ip6_min_hopcount)) {
		/* min_hopcount can be changed concurrently from do_ipv6_setsockopt() */
		if (unlikely(hdr->hop_limit < READ_ONCE(tcp_inet6_sk(sk)->min_hopcount))) {
			__NET_INC_STATS(net, LINUX_MIB_TCPMINTTLDROP);
			drop_reason = SKB_DROP_REASON_TCP_MINTTL;
			goto discard_and_relse;
		}
	}

	if (!xfrm6_policy_check(sk, XFRM_POLICY_IN, skb)) {
		drop_reason = SKB_DROP_REASON_XFRM_POLICY;
		goto discard_and_relse;
	}

	drop_reason = tcp_inbound_hash(sk, NULL, skb, &hdr->saddr, &hdr->daddr,
				       AF_INET6, dif, sdif);
	if (drop_reason)
		goto discard_and_relse;

	nf_reset_ct(skb);

	if (tcp_filter(sk, skb)) {
		drop_reason = SKB_DROP_REASON_SOCKET_FILTER;
		goto discard_and_relse;
	}
	th = (const struct tcphdr *)skb->data;
	hdr = ipv6_hdr(skb);
	tcp_v6_fill_cb(skb, hdr, th);

	skb->dev = NULL;

	if (sk->sk_state == TCP_LISTEN) {
		ret = tcp_v6_do_rcv(sk, skb);
		goto put_and_return;
	}

	sk_incoming_cpu_update(sk);

	bh_lock_sock_nested(sk);
	tcp_segs_in(tcp_sk(sk), skb);
	ret = 0;
	if (!sock_owned_by_user(sk)) {
		ret = tcp_v6_do_rcv(sk, skb);
	} else {
		if (tcp_add_backlog(sk, skb, &drop_reason))
			goto discard_and_relse;
	}
	bh_unlock_sock(sk);
put_and_return:
	if (refcounted)
		sock_put(sk);
	return ret ? -1 : 0;

no_tcp_socket:
	drop_reason = SKB_DROP_REASON_NO_SOCKET;
	if (!xfrm6_policy_check(NULL, XFRM_POLICY_IN, skb))
		goto discard_it;

	tcp_v6_fill_cb(skb, hdr, th);

	if (tcp_checksum_complete(skb)) {
csum_error:
		drop_reason = SKB_DROP_REASON_TCP_CSUM;
		trace_tcp_bad_csum(skb);
		__TCP_INC_STATS(net, TCP_MIB_CSUMERRORS);
bad_packet:
		__TCP_INC_STATS(net, TCP_MIB_INERRS);
	} else {
		tcp_v6_send_reset(NULL, skb, sk_rst_convert_drop_reason(drop_reason));
	}

discard_it:
	SKB_DR_OR(drop_reason, NOT_SPECIFIED);
	sk_skb_reason_drop(sk, skb, drop_reason);
	return 0;

discard_and_relse:
	sk_drops_add(sk, skb);
	if (refcounted)
		sock_put(sk);
	goto discard_it;

do_time_wait:
	if (!xfrm6_policy_check(NULL, XFRM_POLICY_IN, skb)) {
		drop_reason = SKB_DROP_REASON_XFRM_POLICY;
		inet_twsk_put(inet_twsk(sk));
		goto discard_it;
	}

	tcp_v6_fill_cb(skb, hdr, th);

	if (tcp_checksum_complete(skb)) {
		inet_twsk_put(inet_twsk(sk));
		goto csum_error;
	}

	switch (tcp_timewait_state_process(inet_twsk(sk), skb, th, &isn)) {
	case TCP_TW_SYN:
	{
		struct sock *sk2;

		sk2 = inet6_lookup_listener(net, net->ipv4.tcp_death_row.hashinfo,
					    skb, __tcp_hdrlen(th),
					    &ipv6_hdr(skb)->saddr, th->source,
					    &ipv6_hdr(skb)->daddr,
					    ntohs(th->dest),
					    tcp_v6_iif_l3_slave(skb),
					    sdif);
		if (sk2) {
			struct inet_timewait_sock *tw = inet_twsk(sk);
			inet_twsk_deschedule_put(tw);
			sk = sk2;
			tcp_v6_restore_cb(skb);
			refcounted = false;
			__this_cpu_write(tcp_tw_isn, isn);
			goto process;
		}
	}
		/* to ACK */
		fallthrough;
	case TCP_TW_ACK:
		tcp_v6_timewait_ack(sk, skb);
		break;
	case TCP_TW_RST:
		tcp_v6_send_reset(sk, skb, SK_RST_REASON_TCP_TIMEWAIT_SOCKET);
		inet_twsk_deschedule_put(inet_twsk(sk));
		goto discard_it;
	case TCP_TW_SUCCESS:
		;
	}
	goto discard_it;
}

void tcp_v6_early_demux(struct sk_buff *skb)
{
	struct net *net = dev_net(skb->dev);
	const struct ipv6hdr *hdr;
	const struct tcphdr *th;
	struct sock *sk;

	if (skb->pkt_type != PACKET_HOST)
		return;

	if (!pskb_may_pull(skb, skb_transport_offset(skb) + sizeof(struct tcphdr)))
		return;

	hdr = ipv6_hdr(skb);
	th = tcp_hdr(skb);

	if (th->doff < sizeof(struct tcphdr) / 4)
		return;

	/* Note : We use inet6_iif() here, not tcp_v6_iif() */
	sk = __inet6_lookup_established(net, net->ipv4.tcp_death_row.hashinfo,
					&hdr->saddr, th->source,
					&hdr->daddr, ntohs(th->dest),
					inet6_iif(skb), inet6_sdif(skb));
	if (sk) {
		skb->sk = sk;
		skb->destructor = sock_edemux;
		if (sk_fullsock(sk)) {
			struct dst_entry *dst = rcu_dereference(sk->sk_rx_dst);

			if (dst)
				dst = dst_check(dst, sk->sk_rx_dst_cookie);
			if (dst &&
			    sk->sk_rx_dst_ifindex == skb->skb_iif)
				skb_dst_set_noref(skb, dst);
		}
	}
}

static struct timewait_sock_ops tcp6_timewait_sock_ops = {
	.twsk_obj_size	= sizeof(struct tcp6_timewait_sock),
	.twsk_destructor = tcp_twsk_destructor,
};

INDIRECT_CALLABLE_SCOPE void tcp_v6_send_check(struct sock *sk, struct sk_buff *skb)
{
	__tcp_v6_send_check(skb, &sk->sk_v6_rcv_saddr, &sk->sk_v6_daddr);
}

const struct inet_connection_sock_af_ops ipv6_specific = {
	.queue_xmit	   = inet6_csk_xmit,
	.send_check	   = tcp_v6_send_check,
	.rebuild_header	   = inet6_sk_rebuild_header,
	.sk_rx_dst_set	   = inet6_sk_rx_dst_set,
	.conn_request	   = tcp_v6_conn_request,
	.syn_recv_sock	   = tcp_v6_syn_recv_sock,
	.net_header_len	   = sizeof(struct ipv6hdr),
	.setsockopt	   = ipv6_setsockopt,
	.getsockopt	   = ipv6_getsockopt,
	.addr2sockaddr	   = inet6_csk_addr2sockaddr,
	.sockaddr_len	   = sizeof(struct sockaddr_in6),
	.mtu_reduced	   = tcp_v6_mtu_reduced,
};

#if defined(CONFIG_TCP_MD5SIG) || defined(CONFIG_TCP_AO)
static const struct tcp_sock_af_ops tcp_sock_ipv6_specific = {
#ifdef CONFIG_TCP_MD5SIG
	.md5_lookup	=	tcp_v6_md5_lookup,
	.calc_md5_hash	=	tcp_v6_md5_hash_skb,
	.md5_parse	=	tcp_v6_parse_md5_keys,
#endif
#ifdef CONFIG_TCP_AO
	.ao_lookup	=	tcp_v6_ao_lookup,
	.calc_ao_hash	=	tcp_v6_ao_hash_skb,
	.ao_parse	=	tcp_v6_parse_ao,
	.ao_calc_key_sk	=	tcp_v6_ao_calc_key_sk,
#endif
};
#endif

/*
 *	TCP over IPv4 via INET6 API
 */
static const struct inet_connection_sock_af_ops ipv6_mapped = {
	.queue_xmit	   = ip_queue_xmit,
	.send_check	   = tcp_v4_send_check,
	.rebuild_header	   = inet_sk_rebuild_header,
	.sk_rx_dst_set	   = inet_sk_rx_dst_set,
	.conn_request	   = tcp_v6_conn_request,
	.syn_recv_sock	   = tcp_v6_syn_recv_sock,
	.net_header_len	   = sizeof(struct iphdr),
	.setsockopt	   = ipv6_setsockopt,
	.getsockopt	   = ipv6_getsockopt,
	.addr2sockaddr	   = inet6_csk_addr2sockaddr,
	.sockaddr_len	   = sizeof(struct sockaddr_in6),
	.mtu_reduced	   = tcp_v4_mtu_reduced,
};

#if defined(CONFIG_TCP_MD5SIG) || defined(CONFIG_TCP_AO)
static const struct tcp_sock_af_ops tcp_sock_ipv6_mapped_specific = {
#ifdef CONFIG_TCP_MD5SIG
	.md5_lookup	=	tcp_v4_md5_lookup,
	.calc_md5_hash	=	tcp_v4_md5_hash_skb,
	.md5_parse	=	tcp_v6_parse_md5_keys,
#endif
#ifdef CONFIG_TCP_AO
	.ao_lookup	=	tcp_v6_ao_lookup,
	.calc_ao_hash	=	tcp_v4_ao_hash_skb,
	.ao_parse	=	tcp_v6_parse_ao,
	.ao_calc_key_sk	=	tcp_v4_ao_calc_key_sk,
#endif
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

#if defined(CONFIG_TCP_MD5SIG) || defined(CONFIG_TCP_AO)
	tcp_sk(sk)->af_specific = &tcp_sock_ipv6_specific;
#endif

	return 0;
}

#ifdef CONFIG_PROC_FS
/* Proc filesystem TCPv6 sock list dumping. */
static void get_openreq6(struct seq_file *seq,
			 const struct request_sock *req, int i)
{
	long ttd = req->rsk_timer.expires - jiffies;
	const struct in6_addr *src = &inet_rsk(req)->ir_v6_loc_addr;
	const struct in6_addr *dest = &inet_rsk(req)->ir_v6_rmt_addr;

	if (ttd < 0)
		ttd = 0;

	seq_printf(seq,
		   "%4d: %08X%08X%08X%08X:%04X %08X%08X%08X%08X:%04X "
		   "%02X %08X:%08X %02X:%08lX %08X %5u %8d %d %d %pK\n",
		   i,
		   src->s6_addr32[0], src->s6_addr32[1],
		   src->s6_addr32[2], src->s6_addr32[3],
		   inet_rsk(req)->ir_num,
		   dest->s6_addr32[0], dest->s6_addr32[1],
		   dest->s6_addr32[2], dest->s6_addr32[3],
		   ntohs(inet_rsk(req)->ir_rmt_port),
		   TCP_SYN_RECV,
		   0, 0, /* could print option size, but that is af dependent. */
		   1,   /* timers active (only the expire timer) */
		   jiffies_to_clock_t(ttd),
		   req->num_timeout,
		   from_kuid_munged(seq_user_ns(seq),
				    sock_i_uid(req->rsk_listener)),
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
	const struct fastopen_queue *fastopenq = &icsk->icsk_accept_queue.fastopenq;
	int rx_queue;
	int state;

	dest  = &sp->sk_v6_daddr;
	src   = &sp->sk_v6_rcv_saddr;
	destp = ntohs(inet->inet_dport);
	srcp  = ntohs(inet->inet_sport);

	if (icsk->icsk_pending == ICSK_TIME_RETRANS ||
	    icsk->icsk_pending == ICSK_TIME_REO_TIMEOUT ||
	    icsk->icsk_pending == ICSK_TIME_LOSS_PROBE) {
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

	state = inet_sk_state_load(sp);
	if (state == TCP_LISTEN)
		rx_queue = READ_ONCE(sp->sk_ack_backlog);
	else
		/* Because we don't lock the socket,
		 * we might find a transient negative value.
		 */
		rx_queue = max_t(int, READ_ONCE(tp->rcv_nxt) -
				      READ_ONCE(tp->copied_seq), 0);

	seq_printf(seq,
		   "%4d: %08X%08X%08X%08X:%04X %08X%08X%08X%08X:%04X "
		   "%02X %08X:%08X %02X:%08lX %08X %5u %8d %lu %d %pK %lu %lu %u %u %d\n",
		   i,
		   src->s6_addr32[0], src->s6_addr32[1],
		   src->s6_addr32[2], src->s6_addr32[3], srcp,
		   dest->s6_addr32[0], dest->s6_addr32[1],
		   dest->s6_addr32[2], dest->s6_addr32[3], destp,
		   state,
		   READ_ONCE(tp->write_seq) - tp->snd_una,
		   rx_queue,
		   timer_active,
		   jiffies_delta_to_clock_t(timer_expires - jiffies),
		   icsk->icsk_retransmits,
		   from_kuid_munged(seq_user_ns(seq), sock_i_uid(sp)),
		   icsk->icsk_probes_out,
		   sock_i_ino(sp),
		   refcount_read(&sp->sk_refcnt), sp,
		   jiffies_to_clock_t(icsk->icsk_rto),
		   jiffies_to_clock_t(icsk->icsk_ack.ato),
		   (icsk->icsk_ack.quick << 1) | inet_csk_in_pingpong_mode(sp),
		   tcp_snd_cwnd(tp),
		   state == TCP_LISTEN ?
			fastopenq->max_qlen :
			(tcp_in_initial_slowstart(tp) ? -1 : tp->snd_ssthresh)
		   );
}

static void get_timewait6_sock(struct seq_file *seq,
			       struct inet_timewait_sock *tw, int i)
{
	long delta = tw->tw_timer.expires - jiffies;
	const struct in6_addr *dest, *src;
	__u16 destp, srcp;

	dest = &tw->tw_v6_daddr;
	src  = &tw->tw_v6_rcv_saddr;
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
		   READ_ONCE(tw->tw_substate), 0, 0,
		   3, jiffies_delta_to_clock_t(delta), 0, 0, 0, 0,
		   refcount_read(&tw->tw_refcnt), tw);
}

static int tcp6_seq_show(struct seq_file *seq, void *v)
{
	struct tcp_iter_state *st;
	struct sock *sk = v;

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

	if (sk->sk_state == TCP_TIME_WAIT)
		get_timewait6_sock(seq, v, st->num);
	else if (sk->sk_state == TCP_NEW_SYN_RECV)
		get_openreq6(seq, v, st->num);
	else
		get_tcp6_sock(seq, v, st->num);
out:
	return 0;
}

static const struct seq_operations tcp6_seq_ops = {
	.show		= tcp6_seq_show,
	.start		= tcp_seq_start,
	.next		= tcp_seq_next,
	.stop		= tcp_seq_stop,
};

static struct tcp_seq_afinfo tcp6_seq_afinfo = {
	.family		= AF_INET6,
};

int __net_init tcp6_proc_init(struct net *net)
{
	if (!proc_create_net_data("tcp6", 0444, net->proc_net, &tcp6_seq_ops,
			sizeof(struct tcp_iter_state), &tcp6_seq_afinfo))
		return -ENOMEM;
	return 0;
}

void tcp6_proc_exit(struct net *net)
{
	remove_proc_entry("tcp6", net->proc_net);
}
#endif

struct proto tcpv6_prot = {
	.name			= "TCPv6",
	.owner			= THIS_MODULE,
	.close			= tcp_close,
	.pre_connect		= tcp_v6_pre_connect,
	.connect		= tcp_v6_connect,
	.disconnect		= tcp_disconnect,
	.accept			= inet_csk_accept,
	.ioctl			= tcp_ioctl,
	.init			= tcp_v6_init_sock,
	.destroy		= tcp_v4_destroy_sock,
	.shutdown		= tcp_shutdown,
	.setsockopt		= tcp_setsockopt,
	.getsockopt		= tcp_getsockopt,
	.bpf_bypass_getsockopt	= tcp_bpf_bypass_getsockopt,
	.keepalive		= tcp_set_keepalive,
	.recvmsg		= tcp_recvmsg,
	.sendmsg		= tcp_sendmsg,
	.splice_eof		= tcp_splice_eof,
	.backlog_rcv		= tcp_v6_do_rcv,
	.release_cb		= tcp_release_cb,
	.hash			= inet6_hash,
	.unhash			= inet_unhash,
	.get_port		= inet_csk_get_port,
	.put_port		= inet_put_port,
#ifdef CONFIG_BPF_SYSCALL
	.psock_update_sk_prot	= tcp_bpf_update_proto,
#endif
	.enter_memory_pressure	= tcp_enter_memory_pressure,
	.leave_memory_pressure	= tcp_leave_memory_pressure,
	.stream_memory_free	= tcp_stream_memory_free,
	.sockets_allocated	= &tcp_sockets_allocated,

	.memory_allocated	= &tcp_memory_allocated,
	.per_cpu_fw_alloc	= &tcp_memory_per_cpu_fw_alloc,

	.memory_pressure	= &tcp_memory_pressure,
	.orphan_count		= &tcp_orphan_count,
	.sysctl_mem		= sysctl_tcp_mem,
	.sysctl_wmem_offset	= offsetof(struct net, ipv4.sysctl_tcp_wmem),
	.sysctl_rmem_offset	= offsetof(struct net, ipv4.sysctl_tcp_rmem),
	.max_header		= MAX_TCP_HEADER,
	.obj_size		= sizeof(struct tcp6_sock),
	.ipv6_pinfo_offset = offsetof(struct tcp6_sock, inet6),
	.slab_flags		= SLAB_TYPESAFE_BY_RCU,
	.twsk_prot		= &tcp6_timewait_sock_ops,
	.rsk_prot		= &tcp6_request_sock_ops,
	.h.hashinfo		= NULL,
	.no_autobind		= true,
	.diag_destroy		= tcp_abort,
};
EXPORT_SYMBOL_GPL(tcpv6_prot);


static struct inet_protosw tcpv6_protosw = {
	.type		=	SOCK_STREAM,
	.protocol	=	IPPROTO_TCP,
	.prot		=	&tcpv6_prot,
	.ops		=	&inet6_stream_ops,
	.flags		=	INET_PROTOSW_PERMANENT |
				INET_PROTOSW_ICSK,
};

static int __net_init tcpv6_net_init(struct net *net)
{
	int res;

	res = inet_ctl_sock_create(&net->ipv6.tcp_sk, PF_INET6,
				   SOCK_RAW, IPPROTO_TCP, net);
	if (!res)
		net->ipv6.tcp_sk->sk_clockid = CLOCK_MONOTONIC;

	return res;
}

static void __net_exit tcpv6_net_exit(struct net *net)
{
	inet_ctl_sock_destroy(net->ipv6.tcp_sk);
}

static struct pernet_operations tcpv6_net_ops = {
	.init	    = tcpv6_net_init,
	.exit	    = tcpv6_net_exit,
};

int __init tcpv6_init(void)
{
	int ret;

	net_hotdata.tcpv6_protocol = (struct inet6_protocol) {
		.handler     = tcp_v6_rcv,
		.err_handler = tcp_v6_err,
		.flags	     = INET6_PROTO_NOPOLICY | INET6_PROTO_FINAL,
	};
	ret = inet6_add_protocol(&net_hotdata.tcpv6_protocol, IPPROTO_TCP);
	if (ret)
		goto out;

	/* register inet6 protocol */
	ret = inet6_register_protosw(&tcpv6_protosw);
	if (ret)
		goto out_tcpv6_protocol;

	ret = register_pernet_subsys(&tcpv6_net_ops);
	if (ret)
		goto out_tcpv6_protosw;

	ret = mptcpv6_init();
	if (ret)
		goto out_tcpv6_pernet_subsys;

out:
	return ret;

out_tcpv6_pernet_subsys:
	unregister_pernet_subsys(&tcpv6_net_ops);
out_tcpv6_protosw:
	inet6_unregister_protosw(&tcpv6_protosw);
out_tcpv6_protocol:
	inet6_del_protocol(&net_hotdata.tcpv6_protocol, IPPROTO_TCP);
	goto out;
}

void tcpv6_exit(void)
{
	unregister_pernet_subsys(&tcpv6_net_ops);
	inet6_unregister_protosw(&tcpv6_protosw);
	inet6_del_protocol(&net_hotdata.tcpv6_protocol, IPPROTO_TCP);
}
