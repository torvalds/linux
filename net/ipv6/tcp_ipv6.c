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
#include <linux/config.h>
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
#include <net/ipv6.h>
#include <net/transp_v6.h>
#include <net/addrconf.h>
#include <net/ip6_route.h>
#include <net/ip6_checksum.h>
#include <net/inet_ecn.h>
#include <net/protocol.h>
#include <net/xfrm.h>
#include <net/addrconf.h>
#include <net/snmp.h>
#include <net/dsfield.h>

#include <asm/uaccess.h>

#include <linux/proc_fs.h>
#include <linux/seq_file.h>

static void	tcp_v6_send_reset(struct sk_buff *skb);
static void	tcp_v6_reqsk_send_ack(struct sk_buff *skb, struct request_sock *req);
static void	tcp_v6_send_check(struct sock *sk, struct tcphdr *th, int len, 
				  struct sk_buff *skb);

static int	tcp_v6_do_rcv(struct sock *sk, struct sk_buff *skb);
static int	tcp_v6_xmit(struct sk_buff *skb, int ipfragok);

static struct tcp_func ipv6_mapped;
static struct tcp_func ipv6_specific;

static inline int tcp_v6_bind_conflict(const struct sock *sk,
				       const struct inet_bind_bucket *tb)
{
	const struct sock *sk2;
	const struct hlist_node *node;

	/* We must walk the whole port owner list in this case. -DaveM */
	sk_for_each_bound(sk2, node, &tb->owners) {
		if (sk != sk2 &&
		    (!sk->sk_bound_dev_if ||
		     !sk2->sk_bound_dev_if ||
		     sk->sk_bound_dev_if == sk2->sk_bound_dev_if) &&
		    (!sk->sk_reuse || !sk2->sk_reuse ||
		     sk2->sk_state == TCP_LISTEN) &&
		     ipv6_rcv_saddr_equal(sk, sk2))
			break;
	}

	return node != NULL;
}

/* Grrr, addr_type already calculated by caller, but I don't want
 * to add some silly "cookie" argument to this method just for that.
 * But it doesn't matter, the recalculation is in the rarest path
 * this function ever takes.
 */
static int tcp_v6_get_port(struct sock *sk, unsigned short snum)
{
	struct inet_bind_hashbucket *head;
	struct inet_bind_bucket *tb;
	struct hlist_node *node;
	int ret;

	local_bh_disable();
	if (snum == 0) {
		int low = sysctl_local_port_range[0];
		int high = sysctl_local_port_range[1];
		int remaining = (high - low) + 1;
		int rover = net_random() % (high - low) + low;

		do {
			head = &tcp_hashinfo.bhash[inet_bhashfn(rover, tcp_hashinfo.bhash_size)];
			spin_lock(&head->lock);
			inet_bind_bucket_for_each(tb, node, &head->chain)
				if (tb->port == rover)
					goto next;
			break;
		next:
			spin_unlock(&head->lock);
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
		if (unlikely(remaining <= 0))
			goto fail;

		/* OK, here is the one we will use. */
		snum = rover;
	} else {
		head = &tcp_hashinfo.bhash[inet_bhashfn(snum, tcp_hashinfo.bhash_size)];
		spin_lock(&head->lock);
		inet_bind_bucket_for_each(tb, node, &head->chain)
			if (tb->port == snum)
				goto tb_found;
	}
	tb = NULL;
	goto tb_not_found;
tb_found:
	if (tb && !hlist_empty(&tb->owners)) {
		if (tb->fastreuse > 0 && sk->sk_reuse &&
		    sk->sk_state != TCP_LISTEN) {
			goto success;
		} else {
			ret = 1;
			if (tcp_v6_bind_conflict(sk, tb))
				goto fail_unlock;
		}
	}
tb_not_found:
	ret = 1;
	if (tb == NULL) {
	       	tb = inet_bind_bucket_create(tcp_hashinfo.bind_bucket_cachep, head, snum);
		if (tb == NULL)
			goto fail_unlock;
	}
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
	BUG_TRAP(inet_csk(sk)->icsk_bind_hash == tb);
	ret = 0;

fail_unlock:
	spin_unlock(&head->lock);
fail:
	local_bh_enable();
	return ret;
}

static __inline__ void __tcp_v6_hash(struct sock *sk)
{
	struct hlist_head *list;
	rwlock_t *lock;

	BUG_TRAP(sk_unhashed(sk));

	if (sk->sk_state == TCP_LISTEN) {
		list = &tcp_hashinfo.listening_hash[inet_sk_listen_hashfn(sk)];
		lock = &tcp_hashinfo.lhash_lock;
		inet_listen_wlock(&tcp_hashinfo);
	} else {
		unsigned int hash;
		sk->sk_hash = hash = inet6_sk_ehashfn(sk);
		hash &= (tcp_hashinfo.ehash_size - 1);
		list = &tcp_hashinfo.ehash[hash].chain;
		lock = &tcp_hashinfo.ehash[hash].lock;
		write_lock(lock);
	}

	__sk_add_node(sk, list);
	sock_prot_inc_use(sk->sk_prot);
	write_unlock(lock);
}


static void tcp_v6_hash(struct sock *sk)
{
	if (sk->sk_state != TCP_CLOSE) {
		struct tcp_sock *tp = tcp_sk(sk);

		if (tp->af_specific == &ipv6_mapped) {
			tcp_prot.hash(sk);
			return;
		}
		local_bh_disable();
		__tcp_v6_hash(sk);
		local_bh_enable();
	}
}

/*
 * Open request hash tables.
 */

static u32 tcp_v6_synq_hash(const struct in6_addr *raddr, const u16 rport, const u32 rnd)
{
	u32 a, b, c;

	a = raddr->s6_addr32[0];
	b = raddr->s6_addr32[1];
	c = raddr->s6_addr32[2];

	a += JHASH_GOLDEN_RATIO;
	b += JHASH_GOLDEN_RATIO;
	c += rnd;
	__jhash_mix(a, b, c);

	a += raddr->s6_addr32[3];
	b += (u32) rport;
	__jhash_mix(a, b, c);

	return c & (TCP_SYNQ_HSIZE - 1);
}

static struct request_sock *tcp_v6_search_req(const struct sock *sk,
					      struct request_sock ***prevp,
					      __u16 rport,
					      struct in6_addr *raddr,
					      struct in6_addr *laddr,
					      int iif)
{
	const struct inet_connection_sock *icsk = inet_csk(sk);
	struct listen_sock *lopt = icsk->icsk_accept_queue.listen_opt;
	struct request_sock *req, **prev;  

	for (prev = &lopt->syn_table[tcp_v6_synq_hash(raddr, rport, lopt->hash_rnd)];
	     (req = *prev) != NULL;
	     prev = &req->dl_next) {
		const struct tcp6_request_sock *treq = tcp6_rsk(req);

		if (inet_rsk(req)->rmt_port == rport &&
		    req->rsk_ops->family == AF_INET6 &&
		    ipv6_addr_equal(&treq->rmt_addr, raddr) &&
		    ipv6_addr_equal(&treq->loc_addr, laddr) &&
		    (!treq->iif || treq->iif == iif)) {
			BUG_TRAP(req->sk == NULL);
			*prevp = prev;
			return req;
		}
	}

	return NULL;
}

static __inline__ u16 tcp_v6_check(struct tcphdr *th, int len,
				   struct in6_addr *saddr, 
				   struct in6_addr *daddr, 
				   unsigned long base)
{
	return csum_ipv6_magic(saddr, daddr, len, IPPROTO_TCP, base);
}

static __u32 tcp_v6_init_sequence(struct sock *sk, struct sk_buff *skb)
{
	if (skb->protocol == htons(ETH_P_IPV6)) {
		return secure_tcpv6_sequence_number(skb->nh.ipv6h->daddr.s6_addr32,
						    skb->nh.ipv6h->saddr.s6_addr32,
						    skb->h.th->dest,
						    skb->h.th->source);
	} else {
		return secure_tcp_sequence_number(skb->nh.iph->daddr,
						  skb->nh.iph->saddr,
						  skb->h.th->dest,
						  skb->h.th->source);
	}
}

static int __tcp_v6_check_established(struct sock *sk, const __u16 lport,
				      struct inet_timewait_sock **twp)
{
	struct inet_sock *inet = inet_sk(sk);
	const struct ipv6_pinfo *np = inet6_sk(sk);
	const struct in6_addr *daddr = &np->rcv_saddr;
	const struct in6_addr *saddr = &np->daddr;
	const int dif = sk->sk_bound_dev_if;
	const u32 ports = INET_COMBINED_PORTS(inet->dport, lport);
	unsigned int hash = inet6_ehashfn(daddr, inet->num, saddr, inet->dport);
	struct inet_ehash_bucket *head = inet_ehash_bucket(&tcp_hashinfo, hash);
	struct sock *sk2;
	const struct hlist_node *node;
	struct inet_timewait_sock *tw;

	prefetch(head->chain.first);
	write_lock(&head->lock);

	/* Check TIME-WAIT sockets first. */
	sk_for_each(sk2, node, &(head + tcp_hashinfo.ehash_size)->chain) {
		const struct tcp6_timewait_sock *tcp6tw = tcp6_twsk(sk2);

		tw = inet_twsk(sk2);

		if(*((__u32 *)&(tw->tw_dport))	== ports	&&
		   sk2->sk_family		== PF_INET6	&&
		   ipv6_addr_equal(&tcp6tw->tw_v6_daddr, saddr)	&&
		   ipv6_addr_equal(&tcp6tw->tw_v6_rcv_saddr, daddr)	&&
		   sk2->sk_bound_dev_if == sk->sk_bound_dev_if) {
			const struct tcp_timewait_sock *tcptw = tcp_twsk(sk2);
			struct tcp_sock *tp = tcp_sk(sk);

			if (tcptw->tw_ts_recent_stamp &&
			    (!twp ||
			     (sysctl_tcp_tw_reuse &&
			      xtime.tv_sec - tcptw->tw_ts_recent_stamp > 1))) {
				/* See comment in tcp_ipv4.c */
				tp->write_seq = tcptw->tw_snd_nxt + 65535 + 2;
				if (!tp->write_seq)
					tp->write_seq = 1;
				tp->rx_opt.ts_recent	   = tcptw->tw_ts_recent;
				tp->rx_opt.ts_recent_stamp = tcptw->tw_ts_recent_stamp;
				sock_hold(sk2);
				goto unique;
			} else
				goto not_unique;
		}
	}
	tw = NULL;

	/* And established part... */
	sk_for_each(sk2, node, &head->chain) {
		if (INET6_MATCH(sk2, hash, saddr, daddr, ports, dif))
			goto not_unique;
	}

unique:
	BUG_TRAP(sk_unhashed(sk));
	__sk_add_node(sk, &head->chain);
	sk->sk_hash = hash;
	sock_prot_inc_use(sk->sk_prot);
	write_unlock(&head->lock);

	if (twp) {
		*twp = tw;
		NET_INC_STATS_BH(LINUX_MIB_TIMEWAITRECYCLED);
	} else if (tw) {
		/* Silly. Should hash-dance instead... */
		inet_twsk_deschedule(tw, &tcp_death_row);
		NET_INC_STATS_BH(LINUX_MIB_TIMEWAITRECYCLED);

		inet_twsk_put(tw);
	}
	return 0;

not_unique:
	write_unlock(&head->lock);
	return -EADDRNOTAVAIL;
}

static inline u32 tcpv6_port_offset(const struct sock *sk)
{
	const struct inet_sock *inet = inet_sk(sk);
	const struct ipv6_pinfo *np = inet6_sk(sk);

	return secure_tcpv6_port_ephemeral(np->rcv_saddr.s6_addr32,
					   np->daddr.s6_addr32,
					   inet->dport);
}

static int tcp_v6_hash_connect(struct sock *sk)
{
	unsigned short snum = inet_sk(sk)->num;
 	struct inet_bind_hashbucket *head;
 	struct inet_bind_bucket *tb;
	int ret;

 	if (!snum) {
 		int low = sysctl_local_port_range[0];
 		int high = sysctl_local_port_range[1];
		int range = high - low;
 		int i;
		int port;
		static u32 hint;
		u32 offset = hint + tcpv6_port_offset(sk);
		struct hlist_node *node;
 		struct inet_timewait_sock *tw = NULL;

 		local_bh_disable();
		for (i = 1; i <= range; i++) {
			port = low + (i + offset) % range;
 			head = &tcp_hashinfo.bhash[inet_bhashfn(port, tcp_hashinfo.bhash_size)];
 			spin_lock(&head->lock);

 			/* Does not bother with rcv_saddr checks,
 			 * because the established check is already
 			 * unique enough.
 			 */
			inet_bind_bucket_for_each(tb, node, &head->chain) {
 				if (tb->port == port) {
 					BUG_TRAP(!hlist_empty(&tb->owners));
 					if (tb->fastreuse >= 0)
 						goto next_port;
 					if (!__tcp_v6_check_established(sk,
									port,
									&tw))
 						goto ok;
 					goto next_port;
 				}
 			}

 			tb = inet_bind_bucket_create(tcp_hashinfo.bind_bucket_cachep, head, port);
 			if (!tb) {
 				spin_unlock(&head->lock);
 				break;
 			}
 			tb->fastreuse = -1;
 			goto ok;

 		next_port:
 			spin_unlock(&head->lock);
 		}
 		local_bh_enable();

 		return -EADDRNOTAVAIL;

ok:
		hint += i;

 		/* Head lock still held and bh's disabled */
 		inet_bind_hash(sk, tb, port);
		if (sk_unhashed(sk)) {
 			inet_sk(sk)->sport = htons(port);
 			__tcp_v6_hash(sk);
 		}
 		spin_unlock(&head->lock);

 		if (tw) {
 			inet_twsk_deschedule(tw, &tcp_death_row);
 			inet_twsk_put(tw);
 		}

		ret = 0;
		goto out;
 	}

 	head = &tcp_hashinfo.bhash[inet_bhashfn(snum, tcp_hashinfo.bhash_size)];
 	tb   = inet_csk(sk)->icsk_bind_hash;
	spin_lock_bh(&head->lock);

	if (sk_head(&tb->owners) == sk && !sk->sk_bind_node.next) {
		__tcp_v6_hash(sk);
		spin_unlock_bh(&head->lock);
		return 0;
	} else {
		spin_unlock(&head->lock);
		/* No definite answer... Walk to established hash table */
		ret = __tcp_v6_check_established(sk, snum, NULL);
out:
		local_bh_enable();
		return ret;
	}
}

static int tcp_v6_connect(struct sock *sk, struct sockaddr *uaddr, 
			  int addr_len)
{
	struct sockaddr_in6 *usin = (struct sockaddr_in6 *) uaddr;
	struct inet_sock *inet = inet_sk(sk);
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
		u32 exthdrlen = tp->ext_header_len;
		struct sockaddr_in sin;

		SOCK_DEBUG(sk, "connect: ipv4 mapped\n");

		if (__ipv6_only_sock(sk))
			return -ENETUNREACH;

		sin.sin_family = AF_INET;
		sin.sin_port = usin->sin6_port;
		sin.sin_addr.s_addr = usin->sin6_addr.s6_addr32[3];

		tp->af_specific = &ipv6_mapped;
		sk->sk_backlog_rcv = tcp_v4_do_rcv;

		err = tcp_v4_connect(sk, (struct sockaddr *)&sin, sizeof(sin));

		if (err) {
			tp->ext_header_len = exthdrlen;
			tp->af_specific = &ipv6_specific;
			sk->sk_backlog_rcv = tcp_v6_do_rcv;
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

	err = ip6_dst_lookup(sk, &dst, &fl);
	if (err)
		goto failure;
	if (final_p)
		ipv6_addr_copy(&fl.fl6_dst, final_p);

	if ((err = xfrm_lookup(&dst, &fl, sk, 0)) < 0)
		goto failure;

	if (saddr == NULL) {
		saddr = &fl.fl6_src;
		ipv6_addr_copy(&np->rcv_saddr, saddr);
	}

	/* set the source address */
	ipv6_addr_copy(&np->saddr, saddr);
	inet->rcv_saddr = LOOPBACK4_IPV6;

	ip6_dst_store(sk, dst, NULL);
	sk->sk_route_caps = dst->dev->features &
		~(NETIF_F_IP_CSUM | NETIF_F_TSO);

	tp->ext_header_len = 0;
	if (np->opt)
		tp->ext_header_len = np->opt->opt_flen + np->opt->opt_nflen;

	tp->rx_opt.mss_clamp = IPV6_MIN_MTU - sizeof(struct tcphdr) - sizeof(struct ipv6hdr);

	inet->dport = usin->sin6_port;

	tcp_set_state(sk, TCP_SYN_SENT);
	err = tcp_v6_hash_connect(sk);
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
		int type, int code, int offset, __u32 info)
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
		inet_twsk_put((struct inet_timewait_sock *)sk);
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

		if (tp->pmtu_cookie > dst_mtu(dst)) {
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

		req = tcp_v6_search_req(sk, &prev, th->dest, &hdr->daddr,
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
			TCP_INC_STATS_BH(TCP_MIB_ATTEMPTFAILS);
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
	struct tcp6_request_sock *treq = tcp6_rsk(req);
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

	if (dst == NULL) {
		opt = np->opt;
		if (opt == NULL &&
		    np->rxopt.bits.osrcrt == 2 &&
		    treq->pktopts) {
			struct sk_buff *pktopts = treq->pktopts;
			struct inet6_skb_parm *rxopt = IP6CB(pktopts);
			if (rxopt->srcrt)
				opt = ipv6_invert_rthdr(sk, (struct ipv6_rt_hdr*)(pktopts->nh.raw + rxopt->srcrt));
		}

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
		struct tcphdr *th = skb->h.th;

		th->check = tcp_v6_check(th, skb->len,
					 &treq->loc_addr, &treq->rmt_addr,
					 csum_partial((char *)th, skb->len, skb->csum));

		ipv6_addr_copy(&fl.fl6_dst, &treq->rmt_addr);
		err = ip6_xmit(sk, skb, &fl, opt, 0);
		if (err == NET_XMIT_CN)
			err = 0;
	}

done:
        if (opt && opt != np->opt)
		sock_kfree_s(sk, opt, opt->tot_len);
	return err;
}

static void tcp_v6_reqsk_destructor(struct request_sock *req)
{
	if (tcp6_rsk(req)->pktopts)
		kfree_skb(tcp6_rsk(req)->pktopts);
}

static struct request_sock_ops tcp6_request_sock_ops = {
	.family		=	AF_INET6,
	.obj_size	=	sizeof(struct tcp6_request_sock),
	.rtx_syn_ack	=	tcp_v6_send_synack,
	.send_ack	=	tcp_v6_reqsk_send_ack,
	.destructor	=	tcp_v6_reqsk_destructor,
	.send_reset	=	tcp_v6_send_reset
};

static int ipv6_opt_accepted(struct sock *sk, struct sk_buff *skb)
{
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct inet6_skb_parm *opt = IP6CB(skb);

	if (np->rxopt.all) {
		if ((opt->hop && (np->rxopt.bits.hopopts || np->rxopt.bits.ohopopts)) ||
		    ((IPV6_FLOWINFO_MASK & *(u32*)skb->nh.raw) && np->rxopt.bits.rxflow) ||
		    (opt->srcrt && (np->rxopt.bits.srcrt || np->rxopt.bits.osrcrt)) ||
		    ((opt->dst1 || opt->dst0) && (np->rxopt.bits.dstopts || np->rxopt.bits.odstopts)))
			return 1;
	}
	return 0;
}


static void tcp_v6_send_check(struct sock *sk, struct tcphdr *th, int len, 
			      struct sk_buff *skb)
{
	struct ipv6_pinfo *np = inet6_sk(sk);

	if (skb->ip_summed == CHECKSUM_HW) {
		th->check = ~csum_ipv6_magic(&np->saddr, &np->daddr, len, IPPROTO_TCP,  0);
		skb->csum = offsetof(struct tcphdr, check);
	} else {
		th->check = csum_ipv6_magic(&np->saddr, &np->daddr, len, IPPROTO_TCP, 
					    csum_partial((char *)th, th->doff<<2, 
							 skb->csum));
	}
}


static void tcp_v6_send_reset(struct sk_buff *skb)
{
	struct tcphdr *th = skb->h.th, *t1; 
	struct sk_buff *buff;
	struct flowi fl;

	if (th->rst)
		return;

	if (!ipv6_unicast_destination(skb))
		return; 

	/*
	 * We need to grab some memory, and put together an RST,
	 * and then put it into the queue to be sent.
	 */

	buff = alloc_skb(MAX_HEADER + sizeof(struct ipv6hdr) + sizeof(struct tcphdr),
			 GFP_ATOMIC);
	if (buff == NULL) 
	  	return;

	skb_reserve(buff, MAX_HEADER + sizeof(struct ipv6hdr) + sizeof(struct tcphdr));

	t1 = (struct tcphdr *) skb_push(buff,sizeof(struct tcphdr));

	/* Swap the send and the receive. */
	memset(t1, 0, sizeof(*t1));
	t1->dest = th->source;
	t1->source = th->dest;
	t1->doff = sizeof(*t1)/4;
	t1->rst = 1;
  
	if(th->ack) {
	  	t1->seq = th->ack_seq;
	} else {
		t1->ack = 1;
		t1->ack_seq = htonl(ntohl(th->seq) + th->syn + th->fin
				    + skb->len - (th->doff<<2));
	}

	buff->csum = csum_partial((char *)t1, sizeof(*t1), 0);

	memset(&fl, 0, sizeof(fl));
	ipv6_addr_copy(&fl.fl6_dst, &skb->nh.ipv6h->saddr);
	ipv6_addr_copy(&fl.fl6_src, &skb->nh.ipv6h->daddr);

	t1->check = csum_ipv6_magic(&fl.fl6_src, &fl.fl6_dst,
				    sizeof(*t1), IPPROTO_TCP,
				    buff->csum);

	fl.proto = IPPROTO_TCP;
	fl.oif = inet6_iif(skb);
	fl.fl_ip_dport = t1->dest;
	fl.fl_ip_sport = t1->source;

	/* sk = NULL, but it is safe for now. RST socket required. */
	if (!ip6_dst_lookup(NULL, &buff->dst, &fl)) {

		if (xfrm_lookup(&buff->dst, &fl, NULL, 0) >= 0) {
			ip6_xmit(NULL, buff, &fl, NULL, 0);
			TCP_INC_STATS_BH(TCP_MIB_OUTSEGS);
			TCP_INC_STATS_BH(TCP_MIB_OUTRSTS);
			return;
		}
	}

	kfree_skb(buff);
}

static void tcp_v6_send_ack(struct sk_buff *skb, u32 seq, u32 ack, u32 win, u32 ts)
{
	struct tcphdr *th = skb->h.th, *t1;
	struct sk_buff *buff;
	struct flowi fl;
	int tot_len = sizeof(struct tcphdr);

	if (ts)
		tot_len += 3*4;

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
	
	if (ts) {
		u32 *ptr = (u32*)(t1 + 1);
		*ptr++ = htonl((TCPOPT_NOP << 24) | (TCPOPT_NOP << 16) |
			       (TCPOPT_TIMESTAMP << 8) | TCPOLEN_TIMESTAMP);
		*ptr++ = htonl(tcp_time_stamp);
		*ptr = htonl(ts);
	}

	buff->csum = csum_partial((char *)t1, tot_len, 0);

	memset(&fl, 0, sizeof(fl));
	ipv6_addr_copy(&fl.fl6_dst, &skb->nh.ipv6h->saddr);
	ipv6_addr_copy(&fl.fl6_src, &skb->nh.ipv6h->daddr);

	t1->check = csum_ipv6_magic(&fl.fl6_src, &fl.fl6_dst,
				    tot_len, IPPROTO_TCP,
				    buff->csum);

	fl.proto = IPPROTO_TCP;
	fl.oif = inet6_iif(skb);
	fl.fl_ip_dport = t1->dest;
	fl.fl_ip_sport = t1->source;

	if (!ip6_dst_lookup(NULL, &buff->dst, &fl)) {
		if (xfrm_lookup(&buff->dst, &fl, NULL, 0) >= 0) {
			ip6_xmit(NULL, buff, &fl, NULL, 0);
			TCP_INC_STATS_BH(TCP_MIB_OUTSEGS);
			return;
		}
	}

	kfree_skb(buff);
}

static void tcp_v6_timewait_ack(struct sock *sk, struct sk_buff *skb)
{
	struct inet_timewait_sock *tw = inet_twsk(sk);
	const struct tcp_timewait_sock *tcptw = tcp_twsk(sk);

	tcp_v6_send_ack(skb, tcptw->tw_snd_nxt, tcptw->tw_rcv_nxt,
			tcptw->tw_rcv_wnd >> tw->tw_rcv_wscale,
			tcptw->tw_ts_recent);

	inet_twsk_put(tw);
}

static void tcp_v6_reqsk_send_ack(struct sk_buff *skb, struct request_sock *req)
{
	tcp_v6_send_ack(skb, tcp_rsk(req)->snt_isn + 1, tcp_rsk(req)->rcv_isn + 1, req->rcv_wnd, req->ts_recent);
}


static struct sock *tcp_v6_hnd_req(struct sock *sk,struct sk_buff *skb)
{
	struct request_sock *req, **prev;
	const struct tcphdr *th = skb->h.th;
	struct sock *nsk;

	/* Find possible connection requests. */
	req = tcp_v6_search_req(sk, &prev, th->source, &skb->nh.ipv6h->saddr,
				&skb->nh.ipv6h->daddr, inet6_iif(skb));
	if (req)
		return tcp_check_req(sk, skb, req, prev);

	nsk = __inet6_lookup_established(&tcp_hashinfo, &skb->nh.ipv6h->saddr,
					 th->source, &skb->nh.ipv6h->daddr,
					 ntohs(th->dest), inet6_iif(skb));

	if (nsk) {
		if (nsk->sk_state != TCP_TIME_WAIT) {
			bh_lock_sock(nsk);
			return nsk;
		}
		inet_twsk_put((struct inet_timewait_sock *)nsk);
		return NULL;
	}

#if 0 /*def CONFIG_SYN_COOKIES*/
	if (!th->rst && !th->syn && th->ack)
		sk = cookie_v6_check(sk, skb, &(IPCB(skb)->opt));
#endif
	return sk;
}

static void tcp_v6_synq_add(struct sock *sk, struct request_sock *req)
{
	struct inet_connection_sock *icsk = inet_csk(sk);
	struct listen_sock *lopt = icsk->icsk_accept_queue.listen_opt;
	const u32 h = tcp_v6_synq_hash(&tcp6_rsk(req)->rmt_addr, inet_rsk(req)->rmt_port, lopt->hash_rnd);

	reqsk_queue_hash_req(&icsk->icsk_accept_queue, h, req, TCP_TIMEOUT_INIT);
	inet_csk_reqsk_queue_added(sk, TCP_TIMEOUT_INIT);
}


/* FIXME: this is substantially similar to the ipv4 code.
 * Can some kind of merge be done? -- erics
 */
static int tcp_v6_conn_request(struct sock *sk, struct sk_buff *skb)
{
	struct tcp6_request_sock *treq;
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

	req = reqsk_alloc(&tcp6_request_sock_ops);
	if (req == NULL)
		goto drop;

	tcp_clear_options(&tmp_opt);
	tmp_opt.mss_clamp = IPV6_MIN_MTU - sizeof(struct tcphdr) - sizeof(struct ipv6hdr);
	tmp_opt.user_mss = tp->rx_opt.user_mss;

	tcp_parse_options(skb, &tmp_opt, 0);

	tmp_opt.tstamp_ok = tmp_opt.saw_tstamp;
	tcp_openreq_init(req, &tmp_opt, skb);

	treq = tcp6_rsk(req);
	ipv6_addr_copy(&treq->rmt_addr, &skb->nh.ipv6h->saddr);
	ipv6_addr_copy(&treq->loc_addr, &skb->nh.ipv6h->daddr);
	TCP_ECN_create_request(req, skb->h.th);
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
		isn = tcp_v6_init_sequence(sk,skb);

	tcp_rsk(req)->snt_isn = isn;

	if (tcp_v6_send_synack(sk, req, NULL))
		goto drop;

	tcp_v6_synq_add(sk, req);

	return 0;

drop:
	if (req)
		reqsk_free(req);

	TCP_INC_STATS_BH(TCP_MIB_ATTEMPTFAILS);
	return 0; /* don't send reset */
}

static struct sock * tcp_v6_syn_recv_sock(struct sock *sk, struct sk_buff *skb,
					  struct request_sock *req,
					  struct dst_entry *dst)
{
	struct tcp6_request_sock *treq = tcp6_rsk(req);
	struct ipv6_pinfo *newnp, *np = inet6_sk(sk);
	struct tcp6_sock *newtcp6sk;
	struct inet_sock *newinet;
	struct tcp_sock *newtp;
	struct sock *newsk;
	struct ipv6_txoptions *opt;

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

		newtp->af_specific = &ipv6_mapped;
		newsk->sk_backlog_rcv = tcp_v4_do_rcv;
		newnp->pktoptions  = NULL;
		newnp->opt	   = NULL;
		newnp->mcast_oif   = inet6_iif(skb);
		newnp->mcast_hops  = skb->nh.ipv6h->hop_limit;

		/*
		 * No need to charge this sock to the relevant IPv6 refcnt debug socks count
		 * here, tcp_create_openreq_child now does this for us, see the comment in
		 * that function for the gory details. -acme
		 */

		/* It is tricky place. Until this moment IPv4 tcp
		   worked with IPv6 af_tcp.af_specific.
		   Sync it now.
		 */
		tcp_sync_mss(newsk, newtp->pmtu_cookie);

		return newsk;
	}

	opt = np->opt;

	if (sk_acceptq_is_full(sk))
		goto out_overflow;

	if (np->rxopt.bits.osrcrt == 2 &&
	    opt == NULL && treq->pktopts) {
		struct inet6_skb_parm *rxopt = IP6CB(treq->pktopts);
		if (rxopt->srcrt)
			opt = ipv6_invert_rthdr(sk, (struct ipv6_rt_hdr *)(treq->pktopts->nh.raw + rxopt->srcrt));
	}

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

	ip6_dst_store(newsk, dst, NULL);
	newsk->sk_route_caps = dst->dev->features &
		~(NETIF_F_IP_CSUM | NETIF_F_TSO);

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
	newnp->mcast_hops = skb->nh.ipv6h->hop_limit;

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

	newtp->ext_header_len = 0;
	if (newnp->opt)
		newtp->ext_header_len = newnp->opt->opt_nflen +
					newnp->opt->opt_flen;

	tcp_sync_mss(newsk, dst_mtu(dst));
	newtp->advmss = dst_metric(dst, RTAX_ADVMSS);
	tcp_initialize_rcv_mss(newsk);

	newinet->daddr = newinet->saddr = newinet->rcv_saddr = LOOPBACK4_IPV6;

	__tcp_v6_hash(newsk);
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

static int tcp_v6_checksum_init(struct sk_buff *skb)
{
	if (skb->ip_summed == CHECKSUM_HW) {
		if (!tcp_v6_check(skb->h.th,skb->len,&skb->nh.ipv6h->saddr,
				  &skb->nh.ipv6h->daddr,skb->csum)) {
			skb->ip_summed = CHECKSUM_UNNECESSARY;
			return 0;
		}
	}

	skb->csum = ~tcp_v6_check(skb->h.th,skb->len,&skb->nh.ipv6h->saddr,
				  &skb->nh.ipv6h->daddr, 0);

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

	if (sk_filter(sk, skb, 0))
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
		if (tcp_rcv_established(sk, skb, skb->h.th, skb->len))
			goto reset;
		TCP_CHECK_TIMER(sk);
		if (opt_skb)
			goto ipv6_pktoptions;
		return 0;
	}

	if (skb->len < (skb->h.th->doff<<2) || tcp_checksum_complete(skb))
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
	if (tcp_rcv_state_process(sk, skb, skb->h.th, skb->len))
		goto reset;
	TCP_CHECK_TIMER(sk);
	if (opt_skb)
		goto ipv6_pktoptions;
	return 0;

reset:
	tcp_v6_send_reset(skb);
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
			np->mcast_hops = opt_skb->nh.ipv6h->hop_limit;
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

static int tcp_v6_rcv(struct sk_buff **pskb, unsigned int *nhoffp)
{
	struct sk_buff *skb = *pskb;
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

	th = skb->h.th;

	if (th->doff < sizeof(struct tcphdr)/4)
		goto bad_packet;
	if (!pskb_may_pull(skb, th->doff*4))
		goto discard_it;

	if ((skb->ip_summed != CHECKSUM_UNNECESSARY &&
	     tcp_v6_checksum_init(skb)))
		goto bad_packet;

	th = skb->h.th;
	TCP_SKB_CB(skb)->seq = ntohl(th->seq);
	TCP_SKB_CB(skb)->end_seq = (TCP_SKB_CB(skb)->seq + th->syn + th->fin +
				    skb->len - th->doff*4);
	TCP_SKB_CB(skb)->ack_seq = ntohl(th->ack_seq);
	TCP_SKB_CB(skb)->when = 0;
	TCP_SKB_CB(skb)->flags = ipv6_get_dsfield(skb->nh.ipv6h);
	TCP_SKB_CB(skb)->sacked = 0;

	sk = __inet6_lookup(&tcp_hashinfo, &skb->nh.ipv6h->saddr, th->source,
			    &skb->nh.ipv6h->daddr, ntohs(th->dest),
			    inet6_iif(skb));

	if (!sk)
		goto no_tcp_socket;

process:
	if (sk->sk_state == TCP_TIME_WAIT)
		goto do_time_wait;

	if (!xfrm6_policy_check(sk, XFRM_POLICY_IN, skb))
		goto discard_and_relse;

	if (sk_filter(sk, skb, 0))
		goto discard_and_relse;

	skb->dev = NULL;

	bh_lock_sock(sk);
	ret = 0;
	if (!sock_owned_by_user(sk)) {
		if (!tcp_prequeue(sk, skb))
			ret = tcp_v6_do_rcv(sk, skb);
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
		tcp_v6_send_reset(skb);
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
		inet_twsk_put((struct inet_timewait_sock *)sk);
		goto discard_it;
	}

	if (skb->len < (th->doff<<2) || tcp_checksum_complete(skb)) {
		TCP_INC_STATS_BH(TCP_MIB_INERRS);
		inet_twsk_put((struct inet_timewait_sock *)sk);
		goto discard_it;
	}

	switch (tcp_timewait_state_process((struct inet_timewait_sock *)sk,
					   skb, th)) {
	case TCP_TW_SYN:
	{
		struct sock *sk2;

		sk2 = inet6_lookup_listener(&tcp_hashinfo,
					    &skb->nh.ipv6h->daddr,
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

static int tcp_v6_rebuild_header(struct sock *sk)
{
	int err;
	struct dst_entry *dst;
	struct ipv6_pinfo *np = inet6_sk(sk);

	dst = __sk_dst_check(sk, np->dst_cookie);

	if (dst == NULL) {
		struct inet_sock *inet = inet_sk(sk);
		struct in6_addr *final_p = NULL, final;
		struct flowi fl;

		memset(&fl, 0, sizeof(fl));
		fl.proto = IPPROTO_TCP;
		ipv6_addr_copy(&fl.fl6_dst, &np->daddr);
		ipv6_addr_copy(&fl.fl6_src, &np->saddr);
		fl.fl6_flowlabel = np->flow_label;
		fl.oif = sk->sk_bound_dev_if;
		fl.fl_ip_dport = inet->dport;
		fl.fl_ip_sport = inet->sport;

		if (np->opt && np->opt->srcrt) {
			struct rt0_hdr *rt0 = (struct rt0_hdr *) np->opt->srcrt;
			ipv6_addr_copy(&final, &fl.fl6_dst);
			ipv6_addr_copy(&fl.fl6_dst, rt0->addr);
			final_p = &final;
		}

		err = ip6_dst_lookup(sk, &dst, &fl);
		if (err) {
			sk->sk_route_caps = 0;
			return err;
		}
		if (final_p)
			ipv6_addr_copy(&fl.fl6_dst, final_p);

		if ((err = xfrm_lookup(&dst, &fl, sk, 0)) < 0) {
			sk->sk_err_soft = -err;
			return err;
		}

		ip6_dst_store(sk, dst, NULL);
		sk->sk_route_caps = dst->dev->features &
			~(NETIF_F_IP_CSUM | NETIF_F_TSO);
	}

	return 0;
}

static int tcp_v6_xmit(struct sk_buff *skb, int ipfragok)
{
	struct sock *sk = skb->sk;
	struct inet_sock *inet = inet_sk(sk);
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct flowi fl;
	struct dst_entry *dst;
	struct in6_addr *final_p = NULL, final;

	memset(&fl, 0, sizeof(fl));
	fl.proto = IPPROTO_TCP;
	ipv6_addr_copy(&fl.fl6_dst, &np->daddr);
	ipv6_addr_copy(&fl.fl6_src, &np->saddr);
	fl.fl6_flowlabel = np->flow_label;
	IP6_ECN_flow_xmit(sk, fl.fl6_flowlabel);
	fl.oif = sk->sk_bound_dev_if;
	fl.fl_ip_sport = inet->sport;
	fl.fl_ip_dport = inet->dport;

	if (np->opt && np->opt->srcrt) {
		struct rt0_hdr *rt0 = (struct rt0_hdr *) np->opt->srcrt;
		ipv6_addr_copy(&final, &fl.fl6_dst);
		ipv6_addr_copy(&fl.fl6_dst, rt0->addr);
		final_p = &final;
	}

	dst = __sk_dst_check(sk, np->dst_cookie);

	if (dst == NULL) {
		int err = ip6_dst_lookup(sk, &dst, &fl);

		if (err) {
			sk->sk_err_soft = -err;
			return err;
		}

		if (final_p)
			ipv6_addr_copy(&fl.fl6_dst, final_p);

		if ((err = xfrm_lookup(&dst, &fl, sk, 0)) < 0) {
			sk->sk_route_caps = 0;
			return err;
		}

		ip6_dst_store(sk, dst, NULL);
		sk->sk_route_caps = dst->dev->features &
			~(NETIF_F_IP_CSUM | NETIF_F_TSO);
	}

	skb->dst = dst_clone(dst);

	/* Restore final destination back after routing done */
	ipv6_addr_copy(&fl.fl6_dst, &np->daddr);

	return ip6_xmit(sk, skb, &fl, np->opt, 0);
}

static void v6_addr2sockaddr(struct sock *sk, struct sockaddr * uaddr)
{
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *) uaddr;

	sin6->sin6_family = AF_INET6;
	ipv6_addr_copy(&sin6->sin6_addr, &np->daddr);
	sin6->sin6_port	= inet_sk(sk)->dport;
	/* We do not store received flowlabel for TCP */
	sin6->sin6_flowinfo = 0;
	sin6->sin6_scope_id = 0;
	if (sk->sk_bound_dev_if &&
	    ipv6_addr_type(&sin6->sin6_addr) & IPV6_ADDR_LINKLOCAL)
		sin6->sin6_scope_id = sk->sk_bound_dev_if;
}

static int tcp_v6_remember_stamp(struct sock *sk)
{
	/* Alas, not yet... */
	return 0;
}

static struct tcp_func ipv6_specific = {
	.queue_xmit	=	tcp_v6_xmit,
	.send_check	=	tcp_v6_send_check,
	.rebuild_header	=	tcp_v6_rebuild_header,
	.conn_request	=	tcp_v6_conn_request,
	.syn_recv_sock	=	tcp_v6_syn_recv_sock,
	.remember_stamp	=	tcp_v6_remember_stamp,
	.net_header_len	=	sizeof(struct ipv6hdr),

	.setsockopt	=	ipv6_setsockopt,
	.getsockopt	=	ipv6_getsockopt,
	.addr2sockaddr	=	v6_addr2sockaddr,
	.sockaddr_len	=	sizeof(struct sockaddr_in6)
};

/*
 *	TCP over IPv4 via INET6 API
 */

static struct tcp_func ipv6_mapped = {
	.queue_xmit	=	ip_queue_xmit,
	.send_check	=	tcp_v4_send_check,
	.rebuild_header	=	inet_sk_rebuild_header,
	.conn_request	=	tcp_v6_conn_request,
	.syn_recv_sock	=	tcp_v6_syn_recv_sock,
	.remember_stamp	=	tcp_v4_remember_stamp,
	.net_header_len	=	sizeof(struct iphdr),

	.setsockopt	=	ipv6_setsockopt,
	.getsockopt	=	ipv6_getsockopt,
	.addr2sockaddr	=	v6_addr2sockaddr,
	.sockaddr_len	=	sizeof(struct sockaddr_in6)
};



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

	tp->af_specific = &ipv6_specific;
	icsk->icsk_ca_ops = &tcp_init_congestion_ops;
	sk->sk_write_space = sk_stream_write_space;
	sock_set_flag(sk, SOCK_USE_WRITE_QUEUE);

	sk->sk_sndbuf = sysctl_tcp_wmem[1];
	sk->sk_rcvbuf = sysctl_tcp_rmem[1];

	atomic_inc(&tcp_sockets_allocated);

	return 0;
}

static int tcp_v6_destroy_sock(struct sock *sk)
{
	tcp_v4_destroy_sock(sk);
	return inet6_destroy_sock(sk);
}

/* Proc filesystem TCPv6 sock list dumping. */
static void get_openreq6(struct seq_file *seq, 
			 struct sock *sk, struct request_sock *req, int i, int uid)
{
	struct in6_addr *dest, *src;
	int ttd = req->expires - jiffies;

	if (ttd < 0)
		ttd = 0;

	src = &tcp6_rsk(req)->loc_addr;
	dest = &tcp6_rsk(req)->rmt_addr;
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
		   tp->write_seq-tp->snd_una, tp->rcv_nxt-tp->copied_seq,
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
	struct tcp6_timewait_sock *tcp6tw = tcp6_twsk((struct sock *)tw);
	int ttd = tw->tw_ttd - jiffies;

	if (ttd < 0)
		ttd = 0;

	dest = &tcp6tw->tw_v6_daddr;
	src  = &tcp6tw->tw_v6_rcv_saddr;
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

#ifdef CONFIG_PROC_FS
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
	.sendmsg		= tcp_sendmsg,
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
	.twsk_obj_size		= sizeof(struct tcp6_timewait_sock),
	.rsk_prot		= &tcp6_request_sock_ops,
};

static struct inet6_protocol tcpv6_protocol = {
	.handler	=	tcp_v6_rcv,
	.err_handler	=	tcp_v6_err,
	.flags		=	INET6_PROTO_NOPOLICY|INET6_PROTO_FINAL,
};

static struct inet_protosw tcpv6_protosw = {
	.type		=	SOCK_STREAM,
	.protocol	=	IPPROTO_TCP,
	.prot		=	&tcpv6_prot,
	.ops		=	&inet6_stream_ops,
	.capability	=	-1,
	.no_check	=	0,
	.flags		=	INET_PROTOSW_PERMANENT,
};

void __init tcpv6_init(void)
{
	/* register inet6 protocol */
	if (inet6_add_protocol(&tcpv6_protocol, IPPROTO_TCP) < 0)
		printk(KERN_ERR "tcpv6_init: Could not register protocol\n");
	inet6_register_protosw(&tcpv6_protosw);
}
