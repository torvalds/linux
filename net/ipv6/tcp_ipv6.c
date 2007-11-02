/*
 *	TCP over IPv6
 *	Linux INET6 implementation
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>
 *
 *	$Id: tcp_ipv6.c,v 1.144 2002/02/01 22:01:04 davem Exp $
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

#include <asm/uaccess.h>

#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include <linux/crypto.h>
#include <linux/scatterlist.h>

/* Socket used for sending RSTs and ACKs */
static struct socket *tcp6_socket;

static void	tcp_v6_send_reset(struct sock *sk, struct sk_buff *skb);
static void	tcp_v6_reqsk_send_ack(struct sk_buff *skb, struct request_sock *req);
static void	tcp_v6_send_check(struct sock *sk, int len,
				  struct sk_buff *skb);

static int	tcp_v6_do_rcv(struct sock *sk, struct sk_buff *skb);

static struct inet_connection_sock_af_ops ipv6_mapped;
static struct inet_connection_sock_af_ops ipv6_specific;
#ifdef CONFIG_TCP_MD5SIG
static struct tcp_sock_af_ops tcp_sock_ipv6_specific;
static struct tcp_sock_af_ops tcp_sock_ipv6_mapped_specific;
#endif

static int tcp_v6_get_port(struct sock *sk, unsigned short snum)
{
	return inet_csk_get_port(&tcp_hashinfo, sk, snum,
				 inet6_csk_bind_conflict);
}

static void tcp_v6_hash(struct sock *sk)
{
	if (sk->sk_state != TCP_CLOSE) {
		if (inet_csk(sk)->icsk_af_ops == &ipv6_mapped) {
			tcp_prot.hash(sk);
			return;
		}
		local_bh_disable();
		__inet6_hash(&tcp_hashinfo, sk);
		local_bh_enable();
	}
}

static __inline__ __sum16 tcp_v6_check(struct tcphdr *th, int len,
				   struct in6_addr *saddr,
				   struct in6_addr *daddr,
				   __wsum base)
{
	return csum_ipv6_magic(saddr, daddr, len, IPPROTO_TCP, base);
}

static __u32 tcp_v6_init_sequence(struct sk_buff *skb)
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
	struct in6_addr *saddr = NULL, *final_p = NULL, final;
	struct flowi fl;
	struct dst_entry *dst;
	int addr_type;
	int err;

	if (addr_len < SIN6_LEN_RFC2133)
		return -EINVAL;

	if (usin->sin6_family != AF_INET6)
		return(-EAFNOSUPPORT);

	memset(&fl, 0, sizeof(fl));

	if (np->sndflow) {
		fl.fl6_flowlabel = usin->sin6_flowinfo&IPV6_FLOWINFO_MASK;
		IP6_ECN_flow_init(fl.fl6_flowlabel);
		if (fl.fl6_flowlabel&IPV6_FLOWLABEL_MASK) {
			struct ip6_flowlabel *flowlabel;
			flowlabel = fl6_sock_lookup(sk, fl.fl6_flowlabel);
			if (flowlabel == NULL)
				return -EINVAL;
			ipv6_addr_copy(&usin->sin6_addr, &flowlabel->dst);
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

	ipv6_addr_copy(&np->daddr, &usin->sin6_addr);
	np->flow_label = fl.fl6_flowlabel;

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
			ipv6_addr_set(&np->saddr, 0, 0, htonl(0x0000FFFF),
				      inet->saddr);
			ipv6_addr_set(&np->rcv_saddr, 0, 0, htonl(0x0000FFFF),
				      inet->rcv_saddr);
		}

		return err;
	}

	if (!ipv6_addr_any(&np->rcv_saddr))
		saddr = &np->rcv_saddr;

	fl.proto = IPPROTO_TCP;
	ipv6_addr_copy(&fl.fl6_dst, &np->daddr);
	ipv6_addr_copy(&fl.fl6_src,
		       (saddr ? saddr : &np->saddr));
	fl.oif = sk->sk_bound_dev_if;
	fl.fl_ip_dport = usin->sin6_port;
	fl.fl_ip_sport = inet->sport;

	if (np->opt && np->opt->srcrt) {
		struct rt0_hdr *rt0 = (struct rt0_hdr *)np->opt->srcrt;
		ipv6_addr_copy(&final, &fl.fl6_dst);
		ipv6_addr_copy(&fl.fl6_dst, rt0->addr);
		final_p = &final;
	}

	security_sk_classify_flow(sk, &fl);

	err = ip6_dst_lookup(sk, &dst, &fl);
	if (err)
		goto failure;
	if (final_p)
		ipv6_addr_copy(&fl.fl6_dst, final_p);

	if ((err = __xfrm_lookup(&dst, &fl, sk, 1)) < 0) {
		if (err == -EREMOTE)
			err = ip6_dst_blackhole(sk, &dst, &fl);
		if (err < 0)
			goto failure;
	}

	if (saddr == NULL) {
		saddr = &fl.fl6_src;
		ipv6_addr_copy(&np->rcv_saddr, saddr);
	}

	/* set the source address */
	ipv6_addr_copy(&np->saddr, saddr);
	inet->rcv_saddr = LOOPBACK4_IPV6;

	sk->sk_gso_type = SKB_GSO_TCPV6;
	__ip6_dst_store(sk, dst, NULL, NULL);

	icsk->icsk_ext_hdr_len = 0;
	if (np->opt)
		icsk->icsk_ext_hdr_len = (np->opt->opt_flen +
					  np->opt->opt_nflen);

	tp->rx_opt.mss_clamp = IPV6_MIN_MTU - sizeof(struct tcphdr) - sizeof(struct ipv6hdr);

	inet->dport = usin->sin6_port;

	tcp_set_state(sk, TCP_SYN_SENT);
	err = inet6_hash_connect(&tcp_death_row, sk);
	if (err)
		goto late_failure;

	if (!tp->write_seq)
		tp->write_seq = secure_tcpv6_sequence_number(np->saddr.s6_addr32,
							     np->daddr.s6_addr32,
							     inet->sport,
							     inet->dport);

	err = tcp_connect(sk);
	if (err)
		goto late_failure;

	return 0;

late_failure:
	tcp_set_state(sk, TCP_CLOSE);
	__sk_dst_reset(sk);
failure:
	inet->dport = 0;
	sk->sk_route_caps = 0;
	return err;
}

static void tcp_v6_err(struct sk_buff *skb, struct inet6_skb_parm *opt,
		int type, int code, int offset, __be32 info)
{
	struct ipv6hdr *hdr = (struct ipv6hdr*)skb->data;
	const struct tcphdr *th = (struct tcphdr *)(skb->data+offset);
	struct ipv6_pinfo *np;
	struct sock *sk;
	int err;
	struct tcp_sock *tp;
	__u32 seq;

	sk = inet6_lookup(&tcp_hashinfo, &hdr->daddr, th->dest, &hdr->saddr,
			  th->source, skb->dev->ifindex);

	if (sk == NULL) {
		ICMP6_INC_STATS_BH(__in6_dev_get(skb->dev), ICMP6_MIB_INERRORS);
		return;
	}

	if (sk->sk_state == TCP_TIME_WAIT) {
		inet_twsk_put(inet_twsk(sk));
		return;
	}

	bh_lock_sock(sk);
	if (sock_owned_by_user(sk))
		NET_INC_STATS_BH(LINUX_MIB_LOCKDROPPEDICMPS);

	if (sk->sk_state == TCP_CLOSE)
		goto out;

	tp = tcp_sk(sk);
	seq = ntohl(th->seq);
	if (sk->sk_state != TCP_LISTEN &&
	    !between(seq, tp->snd_una, tp->snd_nxt)) {
		NET_INC_STATS_BH(LINUX_MIB_OUTOFWINDOWICMPS);
		goto out;
	}

	np = inet6_sk(sk);

	if (type == ICMPV6_PKT_TOOBIG) {
		struct dst_entry *dst = NULL;

		if (sock_owned_by_user(sk))
			goto out;
		if ((1 << sk->sk_state) & (TCPF_LISTEN | TCPF_CLOSE))
			goto out;

		/* icmp should have updated the destination cache entry */
		dst = __sk_dst_check(sk, np->dst_cookie);

		if (dst == NULL) {
			struct inet_sock *inet = inet_sk(sk);
			struct flowi fl;

			/* BUGGG_FUTURE: Again, it is not clear how
			   to handle rthdr case. Ignore this complexity
			   for now.
			 */
			memset(&fl, 0, sizeof(fl));
			fl.proto = IPPROTO_TCP;
			ipv6_addr_copy(&fl.fl6_dst, &np->daddr);
			ipv6_addr_copy(&fl.fl6_src, &np->saddr);
			fl.oif = sk->sk_bound_dev_if;
			fl.fl_ip_dport = inet->dport;
			fl.fl_ip_sport = inet->sport;
			security_skb_classify_flow(skb, &fl);

			if ((err = ip6_dst_lookup(sk, &dst, &fl))) {
				sk->sk_err_soft = -err;
				goto out;
			}

			if ((err = xfrm_lookup(&dst, &fl, sk, 0)) < 0) {
				sk->sk_err_soft = -err;
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
		BUG_TRAP(req->sk == NULL);

		if (seq != tcp_rsk(req)->snt_isn) {
			NET_INC_STATS_BH(LINUX_MIB_OUTOFWINDOWICMPS);
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
			      struct dst_entry *dst)
{
	struct inet6_request_sock *treq = inet6_rsk(req);
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct sk_buff * skb;
	struct ipv6_txoptions *opt = NULL;
	struct in6_addr * final_p = NULL, final;
	struct flowi fl;
	int err = -1;

	memset(&fl, 0, sizeof(fl));
	fl.proto = IPPROTO_TCP;
	ipv6_addr_copy(&fl.fl6_dst, &treq->rmt_addr);
	ipv6_addr_copy(&fl.fl6_src, &treq->loc_addr);
	fl.fl6_flowlabel = 0;
	fl.oif = treq->iif;
	fl.fl_ip_dport = inet_rsk(req)->rmt_port;
	fl.fl_ip_sport = inet_sk(sk)->sport;
	security_req_classify_flow(req, &fl);

	if (dst == NULL) {
		opt = np->opt;
		if (opt && opt->srcrt) {
			struct rt0_hdr *rt0 = (struct rt0_hdr *) opt->srcrt;
			ipv6_addr_copy(&final, &fl.fl6_dst);
			ipv6_addr_copy(&fl.fl6_dst, rt0->addr);
			final_p = &final;
		}

		err = ip6_dst_lookup(sk, &dst, &fl);
		if (err)
			goto done;
		if (final_p)
			ipv6_addr_copy(&fl.fl6_dst, final_p);
		if ((err = xfrm_lookup(&dst, &fl, sk, 0)) < 0)
			goto done;
	}

	skb = tcp_make_synack(sk, dst, req);
	if (skb) {
		struct tcphdr *th = tcp_hdr(skb);

		th->check = tcp_v6_check(th, skb->len,
					 &treq->loc_addr, &treq->rmt_addr,
					 csum_partial((char *)th, skb->len, skb->csum));

		ipv6_addr_copy(&fl.fl6_dst, &treq->rmt_addr);
		err = ip6_xmit(sk, skb, &fl, opt, 0);
		err = net_xmit_eval(err);
	}

done:
	if (opt && opt != np->opt)
		sock_kfree_s(sk, opt, opt->tot_len);
	dst_release(dst);
	return err;
}

static void tcp_v6_reqsk_destructor(struct request_sock *req)
{
	if (inet6_rsk(req)->pktopts)
		kfree_skb(inet6_rsk(req)->pktopts);
}

#ifdef CONFIG_TCP_MD5SIG
static struct tcp_md5sig_key *tcp_v6_md5_do_lookup(struct sock *sk,
						   struct in6_addr *addr)
{
	struct tcp_sock *tp = tcp_sk(sk);
	int i;

	BUG_ON(tp == NULL);

	if (!tp->md5sig_info || !tp->md5sig_info->entries6)
		return NULL;

	for (i = 0; i < tp->md5sig_info->entries6; i++) {
		if (ipv6_addr_cmp(&tp->md5sig_info->keys6[i].addr, addr) == 0)
			return &tp->md5sig_info->keys6[i].base;
	}
	return NULL;
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

static int tcp_v6_md5_do_add(struct sock *sk, struct in6_addr *peer,
			     char *newkey, u8 newkeylen)
{
	/* Add key to the list */
	struct tcp_md5sig_key *key;
	struct tcp_sock *tp = tcp_sk(sk);
	struct tcp6_md5sig_key *keys;

	key = tcp_v6_md5_do_lookup(sk, peer);
	if (key) {
		/* modify existing entry - just update that one */
		kfree(key->key);
		key->key = newkey;
		key->keylen = newkeylen;
	} else {
		/* reallocate new list if current one is full. */
		if (!tp->md5sig_info) {
			tp->md5sig_info = kzalloc(sizeof(*tp->md5sig_info), GFP_ATOMIC);
			if (!tp->md5sig_info) {
				kfree(newkey);
				return -ENOMEM;
			}
			sk->sk_route_caps &= ~NETIF_F_GSO_MASK;
		}
		tcp_alloc_md5sig_pool();
		if (tp->md5sig_info->alloced6 == tp->md5sig_info->entries6) {
			keys = kmalloc((sizeof (tp->md5sig_info->keys6[0]) *
				       (tp->md5sig_info->entries6 + 1)), GFP_ATOMIC);

			if (!keys) {
				tcp_free_md5sig_pool();
				kfree(newkey);
				return -ENOMEM;
			}

			if (tp->md5sig_info->entries6)
				memmove(keys, tp->md5sig_info->keys6,
					(sizeof (tp->md5sig_info->keys6[0]) *
					 tp->md5sig_info->entries6));

			kfree(tp->md5sig_info->keys6);
			tp->md5sig_info->keys6 = keys;
			tp->md5sig_info->alloced6++;
		}

		ipv6_addr_copy(&tp->md5sig_info->keys6[tp->md5sig_info->entries6].addr,
			       peer);
		tp->md5sig_info->keys6[tp->md5sig_info->entries6].base.key = newkey;
		tp->md5sig_info->keys6[tp->md5sig_info->entries6].base.keylen = newkeylen;

		tp->md5sig_info->entries6++;
	}
	return 0;
}

static int tcp_v6_md5_add_func(struct sock *sk, struct sock *addr_sk,
			       u8 *newkey, __u8 newkeylen)
{
	return tcp_v6_md5_do_add(sk, &inet6_sk(addr_sk)->daddr,
				 newkey, newkeylen);
}

static int tcp_v6_md5_do_del(struct sock *sk, struct in6_addr *peer)
{
	struct tcp_sock *tp = tcp_sk(sk);
	int i;

	for (i = 0; i < tp->md5sig_info->entries6; i++) {
		if (ipv6_addr_cmp(&tp->md5sig_info->keys6[i].addr, peer) == 0) {
			/* Free the key */
			kfree(tp->md5sig_info->keys6[i].base.key);
			tp->md5sig_info->entries6--;

			if (tp->md5sig_info->entries6 == 0) {
				kfree(tp->md5sig_info->keys6);
				tp->md5sig_info->keys6 = NULL;
				tp->md5sig_info->alloced6 = 0;

				tcp_free_md5sig_pool();

				return 0;
			} else {
				/* shrink the database */
				if (tp->md5sig_info->entries6 != i)
					memmove(&tp->md5sig_info->keys6[i],
						&tp->md5sig_info->keys6[i+1],
						(tp->md5sig_info->entries6 - i)
						* sizeof (tp->md5sig_info->keys6[0]));
			}
		}
	}
	return -ENOENT;
}

static void tcp_v6_clear_md5_list (struct sock *sk)
{
	struct tcp_sock *tp = tcp_sk(sk);
	int i;

	if (tp->md5sig_info->entries6) {
		for (i = 0; i < tp->md5sig_info->entries6; i++)
			kfree(tp->md5sig_info->keys6[i].base.key);
		tp->md5sig_info->entries6 = 0;
		tcp_free_md5sig_pool();
	}

	kfree(tp->md5sig_info->keys6);
	tp->md5sig_info->keys6 = NULL;
	tp->md5sig_info->alloced6 = 0;

	if (tp->md5sig_info->entries4) {
		for (i = 0; i < tp->md5sig_info->entries4; i++)
			kfree(tp->md5sig_info->keys4[i].base.key);
		tp->md5sig_info->entries4 = 0;
		tcp_free_md5sig_pool();
	}

	kfree(tp->md5sig_info->keys4);
	tp->md5sig_info->keys4 = NULL;
	tp->md5sig_info->alloced4 = 0;
}

static int tcp_v6_parse_md5_keys (struct sock *sk, char __user *optval,
				  int optlen)
{
	struct tcp_md5sig cmd;
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)&cmd.tcpm_addr;
	u8 *newkey;

	if (optlen < sizeof(cmd))
		return -EINVAL;

	if (copy_from_user(&cmd, optval, sizeof(cmd)))
		return -EFAULT;

	if (sin6->sin6_family != AF_INET6)
		return -EINVAL;

	if (!cmd.tcpm_keylen) {
		if (!tcp_sk(sk)->md5sig_info)
			return -ENOENT;
		if (ipv6_addr_v4mapped(&sin6->sin6_addr))
			return tcp_v4_md5_do_del(sk, sin6->sin6_addr.s6_addr32[3]);
		return tcp_v6_md5_do_del(sk, &sin6->sin6_addr);
	}

	if (cmd.tcpm_keylen > TCP_MD5SIG_MAXKEYLEN)
		return -EINVAL;

	if (!tcp_sk(sk)->md5sig_info) {
		struct tcp_sock *tp = tcp_sk(sk);
		struct tcp_md5sig_info *p;

		p = kzalloc(sizeof(struct tcp_md5sig_info), GFP_KERNEL);
		if (!p)
			return -ENOMEM;

		tp->md5sig_info = p;
		sk->sk_route_caps &= ~NETIF_F_GSO_MASK;
	}

	newkey = kmemdup(cmd.tcpm_key, cmd.tcpm_keylen, GFP_KERNEL);
	if (!newkey)
		return -ENOMEM;
	if (ipv6_addr_v4mapped(&sin6->sin6_addr)) {
		return tcp_v4_md5_do_add(sk, sin6->sin6_addr.s6_addr32[3],
					 newkey, cmd.tcpm_keylen);
	}
	return tcp_v6_md5_do_add(sk, &sin6->sin6_addr, newkey, cmd.tcpm_keylen);
}

static int tcp_v6_do_calc_md5_hash(char *md5_hash, struct tcp_md5sig_key *key,
				   struct in6_addr *saddr,
				   struct in6_addr *daddr,
				   struct tcphdr *th, int protocol,
				   int tcplen)
{
	struct scatterlist sg[4];
	__u16 data_len;
	int block = 0;
	__sum16 cksum;
	struct tcp_md5sig_pool *hp;
	struct tcp6_pseudohdr *bp;
	struct hash_desc *desc;
	int err;
	unsigned int nbytes = 0;

	hp = tcp_get_md5sig_pool();
	if (!hp) {
		printk(KERN_WARNING "%s(): hash pool not found...\n", __FUNCTION__);
		goto clear_hash_noput;
	}
	bp = &hp->md5_blk.ip6;
	desc = &hp->md5_desc;

	/* 1. TCP pseudo-header (RFC2460) */
	ipv6_addr_copy(&bp->saddr, saddr);
	ipv6_addr_copy(&bp->daddr, daddr);
	bp->len = htonl(tcplen);
	bp->protocol = htonl(protocol);

	sg_init_table(sg, 4);

	sg_set_buf(&sg[block++], bp, sizeof(*bp));
	nbytes += sizeof(*bp);

	/* 2. TCP header, excluding options */
	cksum = th->check;
	th->check = 0;
	sg_set_buf(&sg[block++], th, sizeof(*th));
	nbytes += sizeof(*th);

	/* 3. TCP segment data (if any) */
	data_len = tcplen - (th->doff << 2);
	if (data_len > 0) {
		u8 *data = (u8 *)th + (th->doff << 2);
		sg_set_buf(&sg[block++], data, data_len);
		nbytes += data_len;
	}

	/* 4. shared key */
	sg_set_buf(&sg[block++], key->key, key->keylen);
	nbytes += key->keylen;

	__sg_mark_end(&sg[block - 1]);

	/* Now store the hash into the packet */
	err = crypto_hash_init(desc);
	if (err) {
		printk(KERN_WARNING "%s(): hash_init failed\n", __FUNCTION__);
		goto clear_hash;
	}
	err = crypto_hash_update(desc, sg, nbytes);
	if (err) {
		printk(KERN_WARNING "%s(): hash_update failed\n", __FUNCTION__);
		goto clear_hash;
	}
	err = crypto_hash_final(desc, md5_hash);
	if (err) {
		printk(KERN_WARNING "%s(): hash_final failed\n", __FUNCTION__);
		goto clear_hash;
	}

	/* Reset header, and free up the crypto */
	tcp_put_md5sig_pool();
	th->check = cksum;
out:
	return 0;
clear_hash:
	tcp_put_md5sig_pool();
clear_hash_noput:
	memset(md5_hash, 0, 16);
	goto out;
}

static int tcp_v6_calc_md5_hash(char *md5_hash, struct tcp_md5sig_key *key,
				struct sock *sk,
				struct dst_entry *dst,
				struct request_sock *req,
				struct tcphdr *th, int protocol,
				int tcplen)
{
	struct in6_addr *saddr, *daddr;

	if (sk) {
		saddr = &inet6_sk(sk)->saddr;
		daddr = &inet6_sk(sk)->daddr;
	} else {
		saddr = &inet6_rsk(req)->loc_addr;
		daddr = &inet6_rsk(req)->rmt_addr;
	}
	return tcp_v6_do_calc_md5_hash(md5_hash, key,
				       saddr, daddr,
				       th, protocol, tcplen);
}

static int tcp_v6_inbound_md5_hash (struct sock *sk, struct sk_buff *skb)
{
	__u8 *hash_location = NULL;
	struct tcp_md5sig_key *hash_expected;
	struct ipv6hdr *ip6h = ipv6_hdr(skb);
	struct tcphdr *th = tcp_hdr(skb);
	int length = (th->doff << 2) - sizeof (*th);
	int genhash;
	u8 *ptr;
	u8 newhash[16];

	hash_expected = tcp_v6_md5_do_lookup(sk, &ip6h->saddr);

	/* If the TCP option is too short, we can short cut */
	if (length < TCPOLEN_MD5SIG)
		return hash_expected ? 1 : 0;

	/* parse options */
	ptr = (u8*)(th + 1);
	while (length > 0) {
		int opcode = *ptr++;
		int opsize;

		switch(opcode) {
		case TCPOPT_EOL:
			goto done_opts;
		case TCPOPT_NOP:
			length--;
			continue;
		default:
			opsize = *ptr++;
			if (opsize < 2 || opsize > length)
				goto done_opts;
			if (opcode == TCPOPT_MD5SIG) {
				hash_location = ptr;
				goto done_opts;
			}
		}
		ptr += opsize - 2;
		length -= opsize;
	}

done_opts:
	/* do we have a hash as expected? */
	if (!hash_expected) {
		if (!hash_location)
			return 0;
		if (net_ratelimit()) {
			printk(KERN_INFO "MD5 Hash NOT expected but found "
			       "(" NIP6_FMT ", %u)->"
			       "(" NIP6_FMT ", %u)\n",
			       NIP6(ip6h->saddr), ntohs(th->source),
			       NIP6(ip6h->daddr), ntohs(th->dest));
		}
		return 1;
	}

	if (!hash_location) {
		if (net_ratelimit()) {
			printk(KERN_INFO "MD5 Hash expected but NOT found "
			       "(" NIP6_FMT ", %u)->"
			       "(" NIP6_FMT ", %u)\n",
			       NIP6(ip6h->saddr), ntohs(th->source),
			       NIP6(ip6h->daddr), ntohs(th->dest));
		}
		return 1;
	}

	/* check the signature */
	genhash = tcp_v6_do_calc_md5_hash(newhash,
					  hash_expected,
					  &ip6h->saddr, &ip6h->daddr,
					  th, sk->sk_protocol,
					  skb->len);
	if (genhash || memcmp(hash_location, newhash, 16) != 0) {
		if (net_ratelimit()) {
			printk(KERN_INFO "MD5 Hash %s for "
			       "(" NIP6_FMT ", %u)->"
			       "(" NIP6_FMT ", %u)\n",
			       genhash ? "failed" : "mismatch",
			       NIP6(ip6h->saddr), ntohs(th->source),
			       NIP6(ip6h->daddr), ntohs(th->dest));
		}
		return 1;
	}
	return 0;
}
#endif

static struct request_sock_ops tcp6_request_sock_ops __read_mostly = {
	.family		=	AF_INET6,
	.obj_size	=	sizeof(struct tcp6_request_sock),
	.rtx_syn_ack	=	tcp_v6_send_synack,
	.send_ack	=	tcp_v6_reqsk_send_ack,
	.destructor	=	tcp_v6_reqsk_destructor,
	.send_reset	=	tcp_v6_send_reset
};

#ifdef CONFIG_TCP_MD5SIG
static struct tcp_request_sock_ops tcp_request_sock_ipv6_ops = {
	.md5_lookup	=	tcp_v6_reqsk_md5_lookup,
};
#endif

static struct timewait_sock_ops tcp6_timewait_sock_ops = {
	.twsk_obj_size	= sizeof(struct tcp6_timewait_sock),
	.twsk_unique	= tcp_twsk_unique,
	.twsk_destructor= tcp_twsk_destructor,
};

static void tcp_v6_send_check(struct sock *sk, int len, struct sk_buff *skb)
{
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct tcphdr *th = tcp_hdr(skb);

	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		th->check = ~csum_ipv6_magic(&np->saddr, &np->daddr, len, IPPROTO_TCP,  0);
		skb->csum_start = skb_transport_header(skb) - skb->head;
		skb->csum_offset = offsetof(struct tcphdr, check);
	} else {
		th->check = csum_ipv6_magic(&np->saddr, &np->daddr, len, IPPROTO_TCP,
					    csum_partial((char *)th, th->doff<<2,
							 skb->csum));
	}
}

static int tcp_v6_gso_send_check(struct sk_buff *skb)
{
	struct ipv6hdr *ipv6h;
	struct tcphdr *th;

	if (!pskb_may_pull(skb, sizeof(*th)))
		return -EINVAL;

	ipv6h = ipv6_hdr(skb);
	th = tcp_hdr(skb);

	th->check = 0;
	th->check = ~csum_ipv6_magic(&ipv6h->saddr, &ipv6h->daddr, skb->len,
				     IPPROTO_TCP, 0);
	skb->csum_start = skb_transport_header(skb) - skb->head;
	skb->csum_offset = offsetof(struct tcphdr, check);
	skb->ip_summed = CHECKSUM_PARTIAL;
	return 0;
}

static void tcp_v6_send_reset(struct sock *sk, struct sk_buff *skb)
{
	struct tcphdr *th = tcp_hdr(skb), *t1;
	struct sk_buff *buff;
	struct flowi fl;
	int tot_len = sizeof(*th);
#ifdef CONFIG_TCP_MD5SIG
	struct tcp_md5sig_key *key;
#endif

	if (th->rst)
		return;

	if (!ipv6_unicast_destination(skb))
		return;

#ifdef CONFIG_TCP_MD5SIG
	if (sk)
		key = tcp_v6_md5_do_lookup(sk, &ipv6_hdr(skb)->daddr);
	else
		key = NULL;

	if (key)
		tot_len += TCPOLEN_MD5SIG_ALIGNED;
#endif

	/*
	 * We need to grab some memory, and put together an RST,
	 * and then put it into the queue to be sent.
	 */

	buff = alloc_skb(MAX_HEADER + sizeof(struct ipv6hdr) + tot_len,
			 GFP_ATOMIC);
	if (buff == NULL)
		return;

	skb_reserve(buff, MAX_HEADER + sizeof(struct ipv6hdr) + tot_len);

	t1 = (struct tcphdr *) skb_push(buff, tot_len);

	/* Swap the send and the receive. */
	memset(t1, 0, sizeof(*t1));
	t1->dest = th->source;
	t1->source = th->dest;
	t1->doff = tot_len / 4;
	t1->rst = 1;

	if(th->ack) {
		t1->seq = th->ack_seq;
	} else {
		t1->ack = 1;
		t1->ack_seq = htonl(ntohl(th->seq) + th->syn + th->fin
				    + skb->len - (th->doff<<2));
	}

#ifdef CONFIG_TCP_MD5SIG
	if (key) {
		__be32 *opt = (__be32*)(t1 + 1);
		opt[0] = htonl((TCPOPT_NOP << 24) |
			       (TCPOPT_NOP << 16) |
			       (TCPOPT_MD5SIG << 8) |
			       TCPOLEN_MD5SIG);
		tcp_v6_do_calc_md5_hash((__u8 *)&opt[1], key,
					&ipv6_hdr(skb)->daddr,
					&ipv6_hdr(skb)->saddr,
					t1, IPPROTO_TCP, tot_len);
	}
#endif

	buff->csum = csum_partial((char *)t1, sizeof(*t1), 0);

	memset(&fl, 0, sizeof(fl));
	ipv6_addr_copy(&fl.fl6_dst, &ipv6_hdr(skb)->saddr);
	ipv6_addr_copy(&fl.fl6_src, &ipv6_hdr(skb)->daddr);

	t1->check = csum_ipv6_magic(&fl.fl6_src, &fl.fl6_dst,
				    sizeof(*t1), IPPROTO_TCP,
				    buff->csum);

	fl.proto = IPPROTO_TCP;
	fl.oif = inet6_iif(skb);
	fl.fl_ip_dport = t1->dest;
	fl.fl_ip_sport = t1->source;
	security_skb_classify_flow(skb, &fl);

	/* sk = NULL, but it is safe for now. RST socket required. */
	if (!ip6_dst_lookup(NULL, &buff->dst, &fl)) {

		if (xfrm_lookup(&buff->dst, &fl, NULL, 0) >= 0) {
			ip6_xmit(tcp6_socket->sk, buff, &fl, NULL, 0);
			TCP_INC_STATS_BH(TCP_MIB_OUTSEGS);
			TCP_INC_STATS_BH(TCP_MIB_OUTRSTS);
			return;
		}
	}

	kfree_skb(buff);
}

static void tcp_v6_send_ack(struct tcp_timewait_sock *tw,
			    struct sk_buff *skb, u32 seq, u32 ack, u32 win, u32 ts)
{
	struct tcphdr *th = tcp_hdr(skb), *t1;
	struct sk_buff *buff;
	struct flowi fl;
	int tot_len = sizeof(struct tcphdr);
	__be32 *topt;
#ifdef CONFIG_TCP_MD5SIG
	struct tcp_md5sig_key *key;
	struct tcp_md5sig_key tw_key;
#endif

#ifdef CONFIG_TCP_MD5SIG
	if (!tw && skb->sk) {
		key = tcp_v6_md5_do_lookup(skb->sk, &ipv6_hdr(skb)->daddr);
	} else if (tw && tw->tw_md5_keylen) {
		tw_key.key = tw->tw_md5_key;
		tw_key.keylen = tw->tw_md5_keylen;
		key = &tw_key;
	} else {
		key = NULL;
	}
#endif

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

	t1 = (struct tcphdr *) skb_push(buff,tot_len);

	/* Swap the send and the receive. */
	memset(t1, 0, sizeof(*t1));
	t1->dest = th->source;
	t1->source = th->dest;
	t1->doff = tot_len/4;
	t1->seq = htonl(seq);
	t1->ack_seq = htonl(ack);
	t1->ack = 1;
	t1->window = htons(win);

	topt = (__be32 *)(t1 + 1);

	if (ts) {
		*topt++ = htonl((TCPOPT_NOP << 24) | (TCPOPT_NOP << 16) |
				(TCPOPT_TIMESTAMP << 8) | TCPOLEN_TIMESTAMP);
		*topt++ = htonl(tcp_time_stamp);
		*topt = htonl(ts);
	}

#ifdef CONFIG_TCP_MD5SIG
	if (key) {
		*topt++ = htonl((TCPOPT_NOP << 24) | (TCPOPT_NOP << 16) |
				(TCPOPT_MD5SIG << 8) | TCPOLEN_MD5SIG);
		tcp_v6_do_calc_md5_hash((__u8 *)topt, key,
					&ipv6_hdr(skb)->daddr,
					&ipv6_hdr(skb)->saddr,
					t1, IPPROTO_TCP, tot_len);
	}
#endif

	buff->csum = csum_partial((char *)t1, tot_len, 0);

	memset(&fl, 0, sizeof(fl));
	ipv6_addr_copy(&fl.fl6_dst, &ipv6_hdr(skb)->saddr);
	ipv6_addr_copy(&fl.fl6_src, &ipv6_hdr(skb)->daddr);

	t1->check = csum_ipv6_magic(&fl.fl6_src, &fl.fl6_dst,
				    tot_len, IPPROTO_TCP,
				    buff->csum);

	fl.proto = IPPROTO_TCP;
	fl.oif = inet6_iif(skb);
	fl.fl_ip_dport = t1->dest;
	fl.fl_ip_sport = t1->source;
	security_skb_classify_flow(skb, &fl);

	if (!ip6_dst_lookup(NULL, &buff->dst, &fl)) {
		if (xfrm_lookup(&buff->dst, &fl, NULL, 0) >= 0) {
			ip6_xmit(tcp6_socket->sk, buff, &fl, NULL, 0);
			TCP_INC_STATS_BH(TCP_MIB_OUTSEGS);
			return;
		}
	}

	kfree_skb(buff);
}

static void tcp_v6_timewait_ack(struct sock *sk, struct sk_buff *skb)
{
	struct inet_timewait_sock *tw = inet_twsk(sk);
	struct tcp_timewait_sock *tcptw = tcp_twsk(sk);

	tcp_v6_send_ack(tcptw, skb, tcptw->tw_snd_nxt, tcptw->tw_rcv_nxt,
			tcptw->tw_rcv_wnd >> tw->tw_rcv_wscale,
			tcptw->tw_ts_recent);

	inet_twsk_put(tw);
}

static void tcp_v6_reqsk_send_ack(struct sk_buff *skb, struct request_sock *req)
{
	tcp_v6_send_ack(NULL, skb, tcp_rsk(req)->snt_isn + 1, tcp_rsk(req)->rcv_isn + 1, req->rcv_wnd, req->ts_recent);
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

	nsk = __inet6_lookup_established(&tcp_hashinfo, &ipv6_hdr(skb)->saddr,
					 th->source, &ipv6_hdr(skb)->daddr,
					 ntohs(th->dest), inet6_iif(skb));

	if (nsk) {
		if (nsk->sk_state != TCP_TIME_WAIT) {
			bh_lock_sock(nsk);
			return nsk;
		}
		inet_twsk_put(inet_twsk(nsk));
		return NULL;
	}

#if 0 /*def CONFIG_SYN_COOKIES*/
	if (!th->rst && !th->syn && th->ack)
		sk = cookie_v6_check(sk, skb, &(IPCB(skb)->opt));
#endif
	return sk;
}

/* FIXME: this is substantially similar to the ipv4 code.
 * Can some kind of merge be done? -- erics
 */
static int tcp_v6_conn_request(struct sock *sk, struct sk_buff *skb)
{
	struct inet6_request_sock *treq;
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct tcp_options_received tmp_opt;
	struct tcp_sock *tp = tcp_sk(sk);
	struct request_sock *req = NULL;
	__u32 isn = TCP_SKB_CB(skb)->when;

	if (skb->protocol == htons(ETH_P_IP))
		return tcp_v4_conn_request(sk, skb);

	if (!ipv6_unicast_destination(skb))
		goto drop;

	/*
	 *	There are no SYN attacks on IPv6, yet...
	 */
	if (inet_csk_reqsk_queue_is_full(sk) && !isn) {
		if (net_ratelimit())
			printk(KERN_INFO "TCPv6: dropping request, synflood is possible\n");
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

	tcp_parse_options(skb, &tmp_opt, 0);

	tmp_opt.tstamp_ok = tmp_opt.saw_tstamp;
	tcp_openreq_init(req, &tmp_opt, skb);

	treq = inet6_rsk(req);
	ipv6_addr_copy(&treq->rmt_addr, &ipv6_hdr(skb)->saddr);
	ipv6_addr_copy(&treq->loc_addr, &ipv6_hdr(skb)->daddr);
	TCP_ECN_create_request(req, tcp_hdr(skb));
	treq->pktopts = NULL;
	if (ipv6_opt_accepted(sk, skb) ||
	    np->rxopt.bits.rxinfo || np->rxopt.bits.rxoinfo ||
	    np->rxopt.bits.rxhlim || np->rxopt.bits.rxohlim) {
		atomic_inc(&skb->users);
		treq->pktopts = skb;
	}
	treq->iif = sk->sk_bound_dev_if;

	/* So that link locals have meaning */
	if (!sk->sk_bound_dev_if &&
	    ipv6_addr_type(&treq->rmt_addr) & IPV6_ADDR_LINKLOCAL)
		treq->iif = inet6_iif(skb);

	if (isn == 0)
		isn = tcp_v6_init_sequence(skb);

	tcp_rsk(req)->snt_isn = isn;

	security_inet_conn_request(sk, skb, req);

	if (tcp_v6_send_synack(sk, req, NULL))
		goto drop;

	inet6_csk_reqsk_queue_hash_add(sk, req, TCP_TIMEOUT_INIT);
	return 0;

drop:
	if (req)
		reqsk_free(req);

	return 0; /* don't send reset */
}

static struct sock * tcp_v6_syn_recv_sock(struct sock *sk, struct sk_buff *skb,
					  struct request_sock *req,
					  struct dst_entry *dst)
{
	struct inet6_request_sock *treq = inet6_rsk(req);
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

		ipv6_addr_set(&newnp->daddr, 0, 0, htonl(0x0000FFFF),
			      newinet->daddr);

		ipv6_addr_set(&newnp->saddr, 0, 0, htonl(0x0000FFFF),
			      newinet->saddr);

		ipv6_addr_copy(&newnp->rcv_saddr, &newnp->saddr);

		inet_csk(newsk)->icsk_af_ops = &ipv6_mapped;
		newsk->sk_backlog_rcv = tcp_v4_do_rcv;
#ifdef CONFIG_TCP_MD5SIG
		newtp->af_specific = &tcp_sock_ipv6_mapped_specific;
#endif

		newnp->pktoptions  = NULL;
		newnp->opt	   = NULL;
		newnp->mcast_oif   = inet6_iif(skb);
		newnp->mcast_hops  = ipv6_hdr(skb)->hop_limit;

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

	opt = np->opt;

	if (sk_acceptq_is_full(sk))
		goto out_overflow;

	if (dst == NULL) {
		struct in6_addr *final_p = NULL, final;
		struct flowi fl;

		memset(&fl, 0, sizeof(fl));
		fl.proto = IPPROTO_TCP;
		ipv6_addr_copy(&fl.fl6_dst, &treq->rmt_addr);
		if (opt && opt->srcrt) {
			struct rt0_hdr *rt0 = (struct rt0_hdr *) opt->srcrt;
			ipv6_addr_copy(&final, &fl.fl6_dst);
			ipv6_addr_copy(&fl.fl6_dst, rt0->addr);
			final_p = &final;
		}
		ipv6_addr_copy(&fl.fl6_src, &treq->loc_addr);
		fl.oif = sk->sk_bound_dev_if;
		fl.fl_ip_dport = inet_rsk(req)->rmt_port;
		fl.fl_ip_sport = inet_sk(sk)->sport;
		security_req_classify_flow(req, &fl);

		if (ip6_dst_lookup(sk, &dst, &fl))
			goto out;

		if (final_p)
			ipv6_addr_copy(&fl.fl6_dst, final_p);

		if ((xfrm_lookup(&dst, &fl, sk, 0)) < 0)
			goto out;
	}

	newsk = tcp_create_openreq_child(sk, req, skb);
	if (newsk == NULL)
		goto out;

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

	ipv6_addr_copy(&newnp->daddr, &treq->rmt_addr);
	ipv6_addr_copy(&newnp->saddr, &treq->loc_addr);
	ipv6_addr_copy(&newnp->rcv_saddr, &treq->loc_addr);
	newsk->sk_bound_dev_if = treq->iif;

	/* Now IPv6 options...

	   First: no IPv4 options.
	 */
	newinet->opt = NULL;
	newnp->ipv6_fl_list = NULL;

	/* Clone RX bits */
	newnp->rxopt.all = np->rxopt.all;

	/* Clone pktoptions received with SYN */
	newnp->pktoptions = NULL;
	if (treq->pktopts != NULL) {
		newnp->pktoptions = skb_clone(treq->pktopts, GFP_ATOMIC);
		kfree_skb(treq->pktopts);
		treq->pktopts = NULL;
		if (newnp->pktoptions)
			skb_set_owner_r(newnp->pktoptions, newsk);
	}
	newnp->opt	  = NULL;
	newnp->mcast_oif  = inet6_iif(skb);
	newnp->mcast_hops = ipv6_hdr(skb)->hop_limit;

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
	newtp->advmss = dst_metric(dst, RTAX_ADVMSS);
	tcp_initialize_rcv_mss(newsk);

	newinet->daddr = newinet->saddr = newinet->rcv_saddr = LOOPBACK4_IPV6;

#ifdef CONFIG_TCP_MD5SIG
	/* Copy over the MD5 key from the original socket */
	if ((key = tcp_v6_md5_do_lookup(sk, &newnp->daddr)) != NULL) {
		/* We're using one, so create a matching key
		 * on the newsk structure. If we fail to get
		 * memory, then we end up not copying the key
		 * across. Shucks.
		 */
		char *newkey = kmemdup(key->key, key->keylen, GFP_ATOMIC);
		if (newkey != NULL)
			tcp_v6_md5_do_add(newsk, &inet6_sk(sk)->daddr,
					  newkey, key->keylen);
	}
#endif

	__inet6_hash(&tcp_hashinfo, newsk);
	inet_inherit_port(&tcp_hashinfo, sk, newsk);

	return newsk;

out_overflow:
	NET_INC_STATS_BH(LINUX_MIB_LISTENOVERFLOWS);
out:
	NET_INC_STATS_BH(LINUX_MIB_LISTENDROPS);
	if (opt && opt != np->opt)
		sock_kfree_s(sk, opt, opt->tot_len);
	dst_release(dst);
	return NULL;
}

static __sum16 tcp_v6_checksum_init(struct sk_buff *skb)
{
	if (skb->ip_summed == CHECKSUM_COMPLETE) {
		if (!tcp_v6_check(tcp_hdr(skb), skb->len, &ipv6_hdr(skb)->saddr,
				  &ipv6_hdr(skb)->daddr, skb->csum)) {
			skb->ip_summed = CHECKSUM_UNNECESSARY;
			return 0;
		}
	}

	skb->csum = ~csum_unfold(tcp_v6_check(tcp_hdr(skb), skb->len,
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
		TCP_CHECK_TIMER(sk);
		if (tcp_rcv_established(sk, skb, tcp_hdr(skb), skb->len))
			goto reset;
		TCP_CHECK_TIMER(sk);
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
			if (tcp_child_process(sk, nsk, skb))
				goto reset;
			if (opt_skb)
				__kfree_skb(opt_skb);
			return 0;
		}
	}

	TCP_CHECK_TIMER(sk);
	if (tcp_rcv_state_process(sk, skb, tcp_hdr(skb), skb->len))
		goto reset;
	TCP_CHECK_TIMER(sk);
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
	TCP_INC_STATS_BH(TCP_MIB_INERRS);
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
		if (ipv6_opt_accepted(sk, opt_skb)) {
			skb_set_owner_r(opt_skb, sk);
			opt_skb = xchg(&np->pktoptions, opt_skb);
		} else {
			__kfree_skb(opt_skb);
			opt_skb = xchg(&np->pktoptions, NULL);
		}
	}

	if (opt_skb)
		kfree_skb(opt_skb);
	return 0;
}

static int tcp_v6_rcv(struct sk_buff *skb)
{
	struct tcphdr *th;
	struct sock *sk;
	int ret;

	if (skb->pkt_type != PACKET_HOST)
		goto discard_it;

	/*
	 *	Count it even if it's bad.
	 */
	TCP_INC_STATS_BH(TCP_MIB_INSEGS);

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
	TCP_SKB_CB(skb)->seq = ntohl(th->seq);
	TCP_SKB_CB(skb)->end_seq = (TCP_SKB_CB(skb)->seq + th->syn + th->fin +
				    skb->len - th->doff*4);
	TCP_SKB_CB(skb)->ack_seq = ntohl(th->ack_seq);
	TCP_SKB_CB(skb)->when = 0;
	TCP_SKB_CB(skb)->flags = ipv6_get_dsfield(ipv6_hdr(skb));
	TCP_SKB_CB(skb)->sacked = 0;

	sk = __inet6_lookup(&tcp_hashinfo, &ipv6_hdr(skb)->saddr, th->source,
			    &ipv6_hdr(skb)->daddr, ntohs(th->dest),
			    inet6_iif(skb));

	if (!sk)
		goto no_tcp_socket;

process:
	if (sk->sk_state == TCP_TIME_WAIT)
		goto do_time_wait;

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
			tp->ucopy.dma_chan = get_softnet_dma();
		if (tp->ucopy.dma_chan)
			ret = tcp_v6_do_rcv(sk, skb);
		else
#endif
		{
			if (!tcp_prequeue(sk, skb))
				ret = tcp_v6_do_rcv(sk, skb);
		}
	} else
		sk_add_backlog(sk, skb);
	bh_unlock_sock(sk);

	sock_put(sk);
	return ret ? -1 : 0;

no_tcp_socket:
	if (!xfrm6_policy_check(NULL, XFRM_POLICY_IN, skb))
		goto discard_it;

	if (skb->len < (th->doff<<2) || tcp_checksum_complete(skb)) {
bad_packet:
		TCP_INC_STATS_BH(TCP_MIB_INERRS);
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
		TCP_INC_STATS_BH(TCP_MIB_INERRS);
		inet_twsk_put(inet_twsk(sk));
		goto discard_it;
	}

	switch (tcp_timewait_state_process(inet_twsk(sk), skb, th)) {
	case TCP_TW_SYN:
	{
		struct sock *sk2;

		sk2 = inet6_lookup_listener(&tcp_hashinfo,
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

static int tcp_v6_remember_stamp(struct sock *sk)
{
	/* Alas, not yet... */
	return 0;
}

static struct inet_connection_sock_af_ops ipv6_specific = {
	.queue_xmit	   = inet6_csk_xmit,
	.send_check	   = tcp_v6_send_check,
	.rebuild_header	   = inet6_sk_rebuild_header,
	.conn_request	   = tcp_v6_conn_request,
	.syn_recv_sock	   = tcp_v6_syn_recv_sock,
	.remember_stamp	   = tcp_v6_remember_stamp,
	.net_header_len	   = sizeof(struct ipv6hdr),
	.setsockopt	   = ipv6_setsockopt,
	.getsockopt	   = ipv6_getsockopt,
	.addr2sockaddr	   = inet6_csk_addr2sockaddr,
	.sockaddr_len	   = sizeof(struct sockaddr_in6),
#ifdef CONFIG_COMPAT
	.compat_setsockopt = compat_ipv6_setsockopt,
	.compat_getsockopt = compat_ipv6_getsockopt,
#endif
};

#ifdef CONFIG_TCP_MD5SIG
static struct tcp_sock_af_ops tcp_sock_ipv6_specific = {
	.md5_lookup	=	tcp_v6_md5_lookup,
	.calc_md5_hash	=	tcp_v6_calc_md5_hash,
	.md5_add	=	tcp_v6_md5_add_func,
	.md5_parse	=	tcp_v6_parse_md5_keys,
};
#endif

/*
 *	TCP over IPv4 via INET6 API
 */

static struct inet_connection_sock_af_ops ipv6_mapped = {
	.queue_xmit	   = ip_queue_xmit,
	.send_check	   = tcp_v4_send_check,
	.rebuild_header	   = inet_sk_rebuild_header,
	.conn_request	   = tcp_v6_conn_request,
	.syn_recv_sock	   = tcp_v6_syn_recv_sock,
	.remember_stamp	   = tcp_v4_remember_stamp,
	.net_header_len	   = sizeof(struct iphdr),
	.setsockopt	   = ipv6_setsockopt,
	.getsockopt	   = ipv6_getsockopt,
	.addr2sockaddr	   = inet6_csk_addr2sockaddr,
	.sockaddr_len	   = sizeof(struct sockaddr_in6),
#ifdef CONFIG_COMPAT
	.compat_setsockopt = compat_ipv6_setsockopt,
	.compat_getsockopt = compat_ipv6_getsockopt,
#endif
};

#ifdef CONFIG_TCP_MD5SIG
static struct tcp_sock_af_ops tcp_sock_ipv6_mapped_specific = {
	.md5_lookup	=	tcp_v4_md5_lookup,
	.calc_md5_hash	=	tcp_v4_calc_md5_hash,
	.md5_add	=	tcp_v6_md5_add_func,
	.md5_parse	=	tcp_v6_parse_md5_keys,
};
#endif

/* NOTE: A lot of things set to zero explicitly by call to
 *       sk_alloc() so need not be done here.
 */
static int tcp_v6_init_sock(struct sock *sk)
{
	struct inet_connection_sock *icsk = inet_csk(sk);
	struct tcp_sock *tp = tcp_sk(sk);

	skb_queue_head_init(&tp->out_of_order_queue);
	tcp_init_xmit_timers(sk);
	tcp_prequeue_init(tp);

	icsk->icsk_rto = TCP_TIMEOUT_INIT;
	tp->mdev = TCP_TIMEOUT_INIT;

	/* So many TCP implementations out there (incorrectly) count the
	 * initial SYN frame in their delayed-ACK and congestion control
	 * algorithms that we must have the following bandaid to talk
	 * efficiently to them.  -DaveM
	 */
	tp->snd_cwnd = 2;

	/* See draft-stevens-tcpca-spec-01 for discussion of the
	 * initialization of these values.
	 */
	tp->snd_ssthresh = 0x7fffffff;
	tp->snd_cwnd_clamp = ~0;
	tp->mss_cache = 536;

	tp->reordering = sysctl_tcp_reordering;

	sk->sk_state = TCP_CLOSE;

	icsk->icsk_af_ops = &ipv6_specific;
	icsk->icsk_ca_ops = &tcp_init_congestion_ops;
	icsk->icsk_sync_mss = tcp_sync_mss;
	sk->sk_write_space = sk_stream_write_space;
	sock_set_flag(sk, SOCK_USE_WRITE_QUEUE);

#ifdef CONFIG_TCP_MD5SIG
	tp->af_specific = &tcp_sock_ipv6_specific;
#endif

	sk->sk_sndbuf = sysctl_tcp_wmem[1];
	sk->sk_rcvbuf = sysctl_tcp_rmem[1];

	atomic_inc(&tcp_sockets_allocated);

	return 0;
}

static int tcp_v6_destroy_sock(struct sock *sk)
{
#ifdef CONFIG_TCP_MD5SIG
	/* Clean up the MD5 key list */
	if (tcp_sk(sk)->md5sig_info)
		tcp_v6_clear_md5_list(sk);
#endif
	tcp_v4_destroy_sock(sk);
	return inet6_destroy_sock(sk);
}

#ifdef CONFIG_PROC_FS
/* Proc filesystem TCPv6 sock list dumping. */
static void get_openreq6(struct seq_file *seq,
			 struct sock *sk, struct request_sock *req, int i, int uid)
{
	int ttd = req->expires - jiffies;
	struct in6_addr *src = &inet6_rsk(req)->loc_addr;
	struct in6_addr *dest = &inet6_rsk(req)->rmt_addr;

	if (ttd < 0)
		ttd = 0;

	seq_printf(seq,
		   "%4d: %08X%08X%08X%08X:%04X %08X%08X%08X%08X:%04X "
		   "%02X %08X:%08X %02X:%08lX %08X %5d %8d %d %d %p\n",
		   i,
		   src->s6_addr32[0], src->s6_addr32[1],
		   src->s6_addr32[2], src->s6_addr32[3],
		   ntohs(inet_sk(sk)->sport),
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
	struct in6_addr *dest, *src;
	__u16 destp, srcp;
	int timer_active;
	unsigned long timer_expires;
	struct inet_sock *inet = inet_sk(sp);
	struct tcp_sock *tp = tcp_sk(sp);
	const struct inet_connection_sock *icsk = inet_csk(sp);
	struct ipv6_pinfo *np = inet6_sk(sp);

	dest  = &np->daddr;
	src   = &np->rcv_saddr;
	destp = ntohs(inet->dport);
	srcp  = ntohs(inet->sport);

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
		   "%02X %08X:%08X %02X:%08lX %08X %5d %8d %lu %d %p %u %u %u %u %d\n",
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
		   icsk->icsk_rto,
		   icsk->icsk_ack.ato,
		   (icsk->icsk_ack.quick << 1 ) | icsk->icsk_ack.pingpong,
		   tp->snd_cwnd, tp->snd_ssthresh>=0xFFFF?-1:tp->snd_ssthresh
		   );
}

static void get_timewait6_sock(struct seq_file *seq,
			       struct inet_timewait_sock *tw, int i)
{
	struct in6_addr *dest, *src;
	__u16 destp, srcp;
	struct inet6_timewait_sock *tw6 = inet6_twsk((struct sock *)tw);
	int ttd = tw->tw_ttd - jiffies;

	if (ttd < 0)
		ttd = 0;

	dest = &tw6->tw_v6_daddr;
	src  = &tw6->tw_v6_rcv_saddr;
	destp = ntohs(tw->tw_dport);
	srcp  = ntohs(tw->tw_sport);

	seq_printf(seq,
		   "%4d: %08X%08X%08X%08X:%04X %08X%08X%08X%08X:%04X "
		   "%02X %08X:%08X %02X:%08lX %08X %5d %8d %d %d %p\n",
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

static struct file_operations tcp6_seq_fops;
static struct tcp_seq_afinfo tcp6_seq_afinfo = {
	.owner		= THIS_MODULE,
	.name		= "tcp6",
	.family		= AF_INET6,
	.seq_show	= tcp6_seq_show,
	.seq_fops	= &tcp6_seq_fops,
};

int __init tcp6_proc_init(void)
{
	return tcp_proc_register(&tcp6_seq_afinfo);
}

void tcp6_proc_exit(void)
{
	tcp_proc_unregister(&tcp6_seq_afinfo);
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
	.backlog_rcv		= tcp_v6_do_rcv,
	.hash			= tcp_v6_hash,
	.unhash			= tcp_unhash,
	.get_port		= tcp_v6_get_port,
	.enter_memory_pressure	= tcp_enter_memory_pressure,
	.sockets_allocated	= &tcp_sockets_allocated,
	.memory_allocated	= &tcp_memory_allocated,
	.memory_pressure	= &tcp_memory_pressure,
	.orphan_count		= &tcp_orphan_count,
	.sysctl_mem		= sysctl_tcp_mem,
	.sysctl_wmem		= sysctl_tcp_wmem,
	.sysctl_rmem		= sysctl_tcp_rmem,
	.max_header		= MAX_TCP_HEADER,
	.obj_size		= sizeof(struct tcp6_sock),
	.twsk_prot		= &tcp6_timewait_sock_ops,
	.rsk_prot		= &tcp6_request_sock_ops,
#ifdef CONFIG_COMPAT
	.compat_setsockopt	= compat_tcp_setsockopt,
	.compat_getsockopt	= compat_tcp_getsockopt,
#endif
};

static struct inet6_protocol tcpv6_protocol = {
	.handler	=	tcp_v6_rcv,
	.err_handler	=	tcp_v6_err,
	.gso_send_check	=	tcp_v6_gso_send_check,
	.gso_segment	=	tcp_tso_segment,
	.flags		=	INET6_PROTO_NOPOLICY|INET6_PROTO_FINAL,
};

static struct inet_protosw tcpv6_protosw = {
	.type		=	SOCK_STREAM,
	.protocol	=	IPPROTO_TCP,
	.prot		=	&tcpv6_prot,
	.ops		=	&inet6_stream_ops,
	.capability	=	-1,
	.no_check	=	0,
	.flags		=	INET_PROTOSW_PERMANENT |
				INET_PROTOSW_ICSK,
};

void __init tcpv6_init(void)
{
	/* register inet6 protocol */
	if (inet6_add_protocol(&tcpv6_protocol, IPPROTO_TCP) < 0)
		printk(KERN_ERR "tcpv6_init: Could not register protocol\n");
	inet6_register_protosw(&tcpv6_protosw);

	if (inet_csk_ctl_sock_create(&tcp6_socket, PF_INET6, SOCK_RAW,
				     IPPROTO_TCP) < 0)
		panic("Failed to create the TCPv6 control socket.\n");
}
