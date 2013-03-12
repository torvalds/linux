/*
 *	DCCP over IPv6
 *	Linux INET6 implementation
 *
 *	Based on net/dccp6/ipv6.c
 *
 *	Arnaldo Carvalho de Melo <acme@ghostprotocols.net>
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/xfrm.h>

#include <net/addrconf.h>
#include <net/inet_common.h>
#include <net/inet_hashtables.h>
#include <net/inet_sock.h>
#include <net/inet6_connection_sock.h>
#include <net/inet6_hashtables.h>
#include <net/ip6_route.h>
#include <net/ipv6.h>
#include <net/protocol.h>
#include <net/transp_v6.h>
#include <net/ip6_checksum.h>
#include <net/xfrm.h>
#include <net/secure_seq.h>

#include "dccp.h"
#include "ipv6.h"
#include "feat.h"

/* The per-net dccp.v6_ctl_sk is used for sending RSTs and ACKs */

static const struct inet_connection_sock_af_ops dccp_ipv6_mapped;
static const struct inet_connection_sock_af_ops dccp_ipv6_af_ops;

static void dccp_v6_hash(struct sock *sk)
{
	if (sk->sk_state != DCCP_CLOSED) {
		if (inet_csk(sk)->icsk_af_ops == &dccp_ipv6_mapped) {
			inet_hash(sk);
			return;
		}
		local_bh_disable();
		__inet6_hash(sk, NULL);
		local_bh_enable();
	}
}

/* add pseudo-header to DCCP checksum stored in skb->csum */
static inline __sum16 dccp_v6_csum_finish(struct sk_buff *skb,
				      const struct in6_addr *saddr,
				      const struct in6_addr *daddr)
{
	return csum_ipv6_magic(saddr, daddr, skb->len, IPPROTO_DCCP, skb->csum);
}

static inline void dccp_v6_send_check(struct sock *sk, struct sk_buff *skb)
{
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct dccp_hdr *dh = dccp_hdr(skb);

	dccp_csum_outgoing(skb);
	dh->dccph_checksum = dccp_v6_csum_finish(skb, &np->saddr, &np->daddr);
}

static inline __u64 dccp_v6_init_sequence(struct sk_buff *skb)
{
	return secure_dccpv6_sequence_number(ipv6_hdr(skb)->daddr.s6_addr32,
					     ipv6_hdr(skb)->saddr.s6_addr32,
					     dccp_hdr(skb)->dccph_dport,
					     dccp_hdr(skb)->dccph_sport     );

}

static void dccp_v6_err(struct sk_buff *skb, struct inet6_skb_parm *opt,
			u8 type, u8 code, int offset, __be32 info)
{
	const struct ipv6hdr *hdr = (const struct ipv6hdr *)skb->data;
	const struct dccp_hdr *dh = (struct dccp_hdr *)(skb->data + offset);
	struct dccp_sock *dp;
	struct ipv6_pinfo *np;
	struct sock *sk;
	int err;
	__u64 seq;
	struct net *net = dev_net(skb->dev);

	if (skb->len < offset + sizeof(*dh) ||
	    skb->len < offset + __dccp_basic_hdr_len(dh)) {
		ICMP6_INC_STATS_BH(net, __in6_dev_get(skb->dev),
				   ICMP6_MIB_INERRORS);
		return;
	}

	sk = inet6_lookup(net, &dccp_hashinfo,
			&hdr->daddr, dh->dccph_dport,
			&hdr->saddr, dh->dccph_sport, inet6_iif(skb));

	if (sk == NULL) {
		ICMP6_INC_STATS_BH(net, __in6_dev_get(skb->dev),
				   ICMP6_MIB_INERRORS);
		return;
	}

	if (sk->sk_state == DCCP_TIME_WAIT) {
		inet_twsk_put(inet_twsk(sk));
		return;
	}

	bh_lock_sock(sk);
	if (sock_owned_by_user(sk))
		NET_INC_STATS_BH(net, LINUX_MIB_LOCKDROPPEDICMPS);

	if (sk->sk_state == DCCP_CLOSED)
		goto out;

	dp = dccp_sk(sk);
	seq = dccp_hdr_seq(dh);
	if ((1 << sk->sk_state) & ~(DCCPF_REQUESTING | DCCPF_LISTEN) &&
	    !between48(seq, dp->dccps_awl, dp->dccps_awh)) {
		NET_INC_STATS_BH(net, LINUX_MIB_OUTOFWINDOWICMPS);
		goto out;
	}

	np = inet6_sk(sk);

	if (type == ICMPV6_PKT_TOOBIG) {
		struct dst_entry *dst = NULL;

		if (sock_owned_by_user(sk))
			goto out;
		if ((1 << sk->sk_state) & (DCCPF_LISTEN | DCCPF_CLOSED))
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
			fl6.flowi6_proto = IPPROTO_DCCP;
			fl6.daddr = np->daddr;
			fl6.saddr = np->saddr;
			fl6.flowi6_oif = sk->sk_bound_dev_if;
			fl6.fl6_dport = inet->inet_dport;
			fl6.fl6_sport = inet->inet_sport;
			security_sk_classify_flow(sk, flowi6_to_flowi(&fl6));

			dst = ip6_dst_lookup_flow(sk, &fl6, NULL, false);
			if (IS_ERR(dst)) {
				sk->sk_err_soft = -PTR_ERR(dst);
				goto out;
			}
		} else
			dst_hold(dst);

		if (inet_csk(sk)->icsk_pmtu_cookie > dst_mtu(dst)) {
			dccp_sync_mss(sk, dst_mtu(dst));
		} /* else let the usual retransmit timer handle it */
		dst_release(dst);
		goto out;
	}

	icmpv6_err_convert(type, code, &err);

	/* Might be for an request_sock */
	switch (sk->sk_state) {
		struct request_sock *req, **prev;
	case DCCP_LISTEN:
		if (sock_owned_by_user(sk))
			goto out;

		req = inet6_csk_search_req(sk, &prev, dh->dccph_dport,
					   &hdr->daddr, &hdr->saddr,
					   inet6_iif(skb));
		if (req == NULL)
			goto out;

		/*
		 * ICMPs are not backlogged, hence we cannot get an established
		 * socket here.
		 */
		WARN_ON(req->sk != NULL);

		if (!between48(seq, dccp_rsk(req)->dreq_iss,
				    dccp_rsk(req)->dreq_gss)) {
			NET_INC_STATS_BH(net, LINUX_MIB_OUTOFWINDOWICMPS);
			goto out;
		}

		inet_csk_reqsk_queue_drop(sk, req, prev);
		goto out;

	case DCCP_REQUESTING:
	case DCCP_RESPOND:  /* Cannot happen.
			       It can, it SYNs are crossed. --ANK */
		if (!sock_owned_by_user(sk)) {
			DCCP_INC_STATS_BH(DCCP_MIB_ATTEMPTFAILS);
			sk->sk_err = err;
			/*
			 * Wake people up to see the error
			 * (see connect in sock.c)
			 */
			sk->sk_error_report(sk);
			dccp_done(sk);
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


static int dccp_v6_send_response(struct sock *sk, struct request_sock *req,
				 struct request_values *rv_unused)
{
	struct inet6_request_sock *ireq6 = inet6_rsk(req);
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct sk_buff *skb;
	struct ipv6_txoptions *opt = NULL;
	struct in6_addr *final_p, final;
	struct flowi6 fl6;
	int err = -1;
	struct dst_entry *dst;

	memset(&fl6, 0, sizeof(fl6));
	fl6.flowi6_proto = IPPROTO_DCCP;
	fl6.daddr = ireq6->rmt_addr;
	fl6.saddr = ireq6->loc_addr;
	fl6.flowlabel = 0;
	fl6.flowi6_oif = ireq6->iif;
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

	skb = dccp_make_response(sk, dst, req);
	if (skb != NULL) {
		struct dccp_hdr *dh = dccp_hdr(skb);

		dh->dccph_checksum = dccp_v6_csum_finish(skb,
							 &ireq6->loc_addr,
							 &ireq6->rmt_addr);
		fl6.daddr = ireq6->rmt_addr;
		err = ip6_xmit(sk, skb, &fl6, opt, np->tclass);
		err = net_xmit_eval(err);
	}

done:
	if (opt != NULL && opt != np->opt)
		sock_kfree_s(sk, opt, opt->tot_len);
	dst_release(dst);
	return err;
}

static void dccp_v6_reqsk_destructor(struct request_sock *req)
{
	dccp_feat_list_purge(&dccp_rsk(req)->dreq_featneg);
	if (inet6_rsk(req)->pktopts != NULL)
		kfree_skb(inet6_rsk(req)->pktopts);
}

static void dccp_v6_ctl_send_reset(struct sock *sk, struct sk_buff *rxskb)
{
	const struct ipv6hdr *rxip6h;
	struct sk_buff *skb;
	struct flowi6 fl6;
	struct net *net = dev_net(skb_dst(rxskb)->dev);
	struct sock *ctl_sk = net->dccp.v6_ctl_sk;
	struct dst_entry *dst;

	if (dccp_hdr(rxskb)->dccph_type == DCCP_PKT_RESET)
		return;

	if (!ipv6_unicast_destination(rxskb))
		return;

	skb = dccp_ctl_make_reset(ctl_sk, rxskb);
	if (skb == NULL)
		return;

	rxip6h = ipv6_hdr(rxskb);
	dccp_hdr(skb)->dccph_checksum = dccp_v6_csum_finish(skb, &rxip6h->saddr,
							    &rxip6h->daddr);

	memset(&fl6, 0, sizeof(fl6));
	fl6.daddr = rxip6h->saddr;
	fl6.saddr = rxip6h->daddr;

	fl6.flowi6_proto = IPPROTO_DCCP;
	fl6.flowi6_oif = inet6_iif(rxskb);
	fl6.fl6_dport = dccp_hdr(skb)->dccph_dport;
	fl6.fl6_sport = dccp_hdr(skb)->dccph_sport;
	security_skb_classify_flow(rxskb, flowi6_to_flowi(&fl6));

	/* sk = NULL, but it is safe for now. RST socket required. */
	dst = ip6_dst_lookup_flow(ctl_sk, &fl6, NULL, false);
	if (!IS_ERR(dst)) {
		skb_dst_set(skb, dst);
		ip6_xmit(ctl_sk, skb, &fl6, NULL, 0);
		DCCP_INC_STATS_BH(DCCP_MIB_OUTSEGS);
		DCCP_INC_STATS_BH(DCCP_MIB_OUTRSTS);
		return;
	}

	kfree_skb(skb);
}

static struct request_sock_ops dccp6_request_sock_ops = {
	.family		= AF_INET6,
	.obj_size	= sizeof(struct dccp6_request_sock),
	.rtx_syn_ack	= dccp_v6_send_response,
	.send_ack	= dccp_reqsk_send_ack,
	.destructor	= dccp_v6_reqsk_destructor,
	.send_reset	= dccp_v6_ctl_send_reset,
};

static struct sock *dccp_v6_hnd_req(struct sock *sk,struct sk_buff *skb)
{
	const struct dccp_hdr *dh = dccp_hdr(skb);
	const struct ipv6hdr *iph = ipv6_hdr(skb);
	struct sock *nsk;
	struct request_sock **prev;
	/* Find possible connection requests. */
	struct request_sock *req = inet6_csk_search_req(sk, &prev,
							dh->dccph_sport,
							&iph->saddr,
							&iph->daddr,
							inet6_iif(skb));
	if (req != NULL)
		return dccp_check_req(sk, skb, req, prev);

	nsk = __inet6_lookup_established(sock_net(sk), &dccp_hashinfo,
					 &iph->saddr, dh->dccph_sport,
					 &iph->daddr, ntohs(dh->dccph_dport),
					 inet6_iif(skb));
	if (nsk != NULL) {
		if (nsk->sk_state != DCCP_TIME_WAIT) {
			bh_lock_sock(nsk);
			return nsk;
		}
		inet_twsk_put(inet_twsk(nsk));
		return NULL;
	}

	return sk;
}

static int dccp_v6_conn_request(struct sock *sk, struct sk_buff *skb)
{
	struct request_sock *req;
	struct dccp_request_sock *dreq;
	struct inet6_request_sock *ireq6;
	struct ipv6_pinfo *np = inet6_sk(sk);
	const __be32 service = dccp_hdr_request(skb)->dccph_req_service;
	struct dccp_skb_cb *dcb = DCCP_SKB_CB(skb);

	if (skb->protocol == htons(ETH_P_IP))
		return dccp_v4_conn_request(sk, skb);

	if (!ipv6_unicast_destination(skb))
		return 0;	/* discard, don't send a reset here */

	if (dccp_bad_service_code(sk, service)) {
		dcb->dccpd_reset_code = DCCP_RESET_CODE_BAD_SERVICE_CODE;
		goto drop;
	}
	/*
	 * There are no SYN attacks on IPv6, yet...
	 */
	dcb->dccpd_reset_code = DCCP_RESET_CODE_TOO_BUSY;
	if (inet_csk_reqsk_queue_is_full(sk))
		goto drop;

	if (sk_acceptq_is_full(sk) && inet_csk_reqsk_queue_young(sk) > 1)
		goto drop;

	req = inet6_reqsk_alloc(&dccp6_request_sock_ops);
	if (req == NULL)
		goto drop;

	if (dccp_reqsk_init(req, dccp_sk(sk), skb))
		goto drop_and_free;

	dreq = dccp_rsk(req);
	if (dccp_parse_options(sk, dreq, skb))
		goto drop_and_free;

	if (security_inet_conn_request(sk, skb, req))
		goto drop_and_free;

	ireq6 = inet6_rsk(req);
	ireq6->rmt_addr = ipv6_hdr(skb)->saddr;
	ireq6->loc_addr = ipv6_hdr(skb)->daddr;

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

	/*
	 * Step 3: Process LISTEN state
	 *
	 *   Set S.ISR, S.GSR, S.SWL, S.SWH from packet or Init Cookie
	 *
	 * Setting S.SWL/S.SWH to is deferred to dccp_create_openreq_child().
	 */
	dreq->dreq_isr	   = dcb->dccpd_seq;
	dreq->dreq_gsr     = dreq->dreq_isr;
	dreq->dreq_iss	   = dccp_v6_init_sequence(skb);
	dreq->dreq_gss     = dreq->dreq_iss;
	dreq->dreq_service = service;

	if (dccp_v6_send_response(sk, req, NULL))
		goto drop_and_free;

	inet6_csk_reqsk_queue_hash_add(sk, req, DCCP_TIMEOUT_INIT);
	return 0;

drop_and_free:
	reqsk_free(req);
drop:
	DCCP_INC_STATS_BH(DCCP_MIB_ATTEMPTFAILS);
	return -1;
}

static struct sock *dccp_v6_request_recv_sock(struct sock *sk,
					      struct sk_buff *skb,
					      struct request_sock *req,
					      struct dst_entry *dst)
{
	struct inet6_request_sock *ireq6 = inet6_rsk(req);
	struct ipv6_pinfo *newnp, *np = inet6_sk(sk);
	struct inet_sock *newinet;
	struct dccp6_sock *newdp6;
	struct sock *newsk;
	struct ipv6_txoptions *opt;

	if (skb->protocol == htons(ETH_P_IP)) {
		/*
		 *	v6 mapped
		 */
		newsk = dccp_v4_request_recv_sock(sk, skb, req, dst);
		if (newsk == NULL)
			return NULL;

		newdp6 = (struct dccp6_sock *)newsk;
		newinet = inet_sk(newsk);
		newinet->pinet6 = &newdp6->inet6;
		newnp = inet6_sk(newsk);

		memcpy(newnp, np, sizeof(struct ipv6_pinfo));

		ipv6_addr_set_v4mapped(newinet->inet_daddr, &newnp->daddr);

		ipv6_addr_set_v4mapped(newinet->inet_saddr, &newnp->saddr);

		newnp->rcv_saddr = newnp->saddr;

		inet_csk(newsk)->icsk_af_ops = &dccp_ipv6_mapped;
		newsk->sk_backlog_rcv = dccp_v4_do_rcv;
		newnp->pktoptions  = NULL;
		newnp->opt	   = NULL;
		newnp->mcast_oif   = inet6_iif(skb);
		newnp->mcast_hops  = ipv6_hdr(skb)->hop_limit;

		/*
		 * No need to charge this sock to the relevant IPv6 refcnt debug socks count
		 * here, dccp_create_openreq_child now does this for us, see the comment in
		 * that function for the gory details. -acme
		 */

		/* It is tricky place. Until this moment IPv4 tcp
		   worked with IPv6 icsk.icsk_af_ops.
		   Sync it now.
		 */
		dccp_sync_mss(newsk, inet_csk(newsk)->icsk_pmtu_cookie);

		return newsk;
	}

	opt = np->opt;

	if (sk_acceptq_is_full(sk))
		goto out_overflow;

	if (dst == NULL) {
		struct in6_addr *final_p, final;
		struct flowi6 fl6;

		memset(&fl6, 0, sizeof(fl6));
		fl6.flowi6_proto = IPPROTO_DCCP;
		fl6.daddr = ireq6->rmt_addr;
		final_p = fl6_update_dst(&fl6, opt, &final);
		fl6.saddr = ireq6->loc_addr;
		fl6.flowi6_oif = sk->sk_bound_dev_if;
		fl6.fl6_dport = inet_rsk(req)->rmt_port;
		fl6.fl6_sport = inet_rsk(req)->loc_port;
		security_sk_classify_flow(sk, flowi6_to_flowi(&fl6));

		dst = ip6_dst_lookup_flow(sk, &fl6, final_p, false);
		if (IS_ERR(dst))
			goto out;
	}

	newsk = dccp_create_openreq_child(sk, req, skb);
	if (newsk == NULL)
		goto out_nonewsk;

	/*
	 * No need to charge this sock to the relevant IPv6 refcnt debug socks
	 * count here, dccp_create_openreq_child now does this for us, see the
	 * comment in that function for the gory details. -acme
	 */

	__ip6_dst_store(newsk, dst, NULL, NULL);
	newsk->sk_route_caps = dst->dev->features & ~(NETIF_F_IP_CSUM |
						      NETIF_F_TSO);
	newdp6 = (struct dccp6_sock *)newsk;
	newinet = inet_sk(newsk);
	newinet->pinet6 = &newdp6->inet6;
	newnp = inet6_sk(newsk);

	memcpy(newnp, np, sizeof(struct ipv6_pinfo));

	newnp->daddr = ireq6->rmt_addr;
	newnp->saddr = ireq6->loc_addr;
	newnp->rcv_saddr = ireq6->loc_addr;
	newsk->sk_bound_dev_if = ireq6->iif;

	/* Now IPv6 options...

	   First: no IPv4 options.
	 */
	newinet->inet_opt = NULL;

	/* Clone RX bits */
	newnp->rxopt.all = np->rxopt.all;

	/* Clone pktoptions received with SYN */
	newnp->pktoptions = NULL;
	if (ireq6->pktopts != NULL) {
		newnp->pktoptions = skb_clone(ireq6->pktopts, GFP_ATOMIC);
		kfree_skb(ireq6->pktopts);
		ireq6->pktopts = NULL;
		if (newnp->pktoptions)
			skb_set_owner_r(newnp->pktoptions, newsk);
	}
	newnp->opt	  = NULL;
	newnp->mcast_oif  = inet6_iif(skb);
	newnp->mcast_hops = ipv6_hdr(skb)->hop_limit;

	/*
	 * Clone native IPv6 options from listening socket (if any)
	 *
	 * Yes, keeping reference count would be much more clever, but we make
	 * one more one thing there: reattach optmem to newsk.
	 */
	if (opt != NULL) {
		newnp->opt = ipv6_dup_options(newsk, opt);
		if (opt != np->opt)
			sock_kfree_s(sk, opt, opt->tot_len);
	}

	inet_csk(newsk)->icsk_ext_hdr_len = 0;
	if (newnp->opt != NULL)
		inet_csk(newsk)->icsk_ext_hdr_len = (newnp->opt->opt_nflen +
						     newnp->opt->opt_flen);

	dccp_sync_mss(newsk, dst_mtu(dst));

	newinet->inet_daddr = newinet->inet_saddr = LOOPBACK4_IPV6;
	newinet->inet_rcv_saddr = LOOPBACK4_IPV6;

	if (__inet_inherit_port(sk, newsk) < 0) {
		inet_csk_prepare_forced_close(newsk);
		dccp_done(newsk);
		goto out;
	}
	__inet6_hash(newsk, NULL);

	return newsk;

out_overflow:
	NET_INC_STATS_BH(sock_net(sk), LINUX_MIB_LISTENOVERFLOWS);
out_nonewsk:
	dst_release(dst);
out:
	NET_INC_STATS_BH(sock_net(sk), LINUX_MIB_LISTENDROPS);
	if (opt != NULL && opt != np->opt)
		sock_kfree_s(sk, opt, opt->tot_len);
	return NULL;
}

/* The socket must have it's spinlock held when we get
 * here.
 *
 * We have a potential double-lock case here, so even when
 * doing backlog processing we use the BH locking scheme.
 * This is because we cannot sleep with the original spinlock
 * held.
 */
static int dccp_v6_do_rcv(struct sock *sk, struct sk_buff *skb)
{
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct sk_buff *opt_skb = NULL;

	/* Imagine: socket is IPv6. IPv4 packet arrives,
	   goes to IPv4 receive handler and backlogged.
	   From backlog it always goes here. Kerboom...
	   Fortunately, dccp_rcv_established and rcv_established
	   handle them correctly, but it is not case with
	   dccp_v6_hnd_req and dccp_v6_ctl_send_reset().   --ANK
	 */

	if (skb->protocol == htons(ETH_P_IP))
		return dccp_v4_do_rcv(sk, skb);

	if (sk_filter(sk, skb))
		goto discard;

	/*
	 * socket locking is here for SMP purposes as backlog rcv is currently
	 * called with bh processing disabled.
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
	/*
	 * FIXME: Add handling of IPV6_PKTOPTIONS skb. See the comments below
	 *        (wrt ipv6_pktopions) and net/ipv6/tcp_ipv6.c for an example.
	 */
		opt_skb = skb_clone(skb, GFP_ATOMIC);

	if (sk->sk_state == DCCP_OPEN) { /* Fast path */
		if (dccp_rcv_established(sk, skb, dccp_hdr(skb), skb->len))
			goto reset;
		if (opt_skb) {
			/* XXX This is where we would goto ipv6_pktoptions. */
			__kfree_skb(opt_skb);
		}
		return 0;
	}

	/*
	 *  Step 3: Process LISTEN state
	 *     If S.state == LISTEN,
	 *	 If P.type == Request or P contains a valid Init Cookie option,
	 *	      (* Must scan the packet's options to check for Init
	 *		 Cookies.  Only Init Cookies are processed here,
	 *		 however; other options are processed in Step 8.  This
	 *		 scan need only be performed if the endpoint uses Init
	 *		 Cookies *)
	 *	      (* Generate a new socket and switch to that socket *)
	 *	      Set S := new socket for this port pair
	 *	      S.state = RESPOND
	 *	      Choose S.ISS (initial seqno) or set from Init Cookies
	 *	      Initialize S.GAR := S.ISS
	 *	      Set S.ISR, S.GSR, S.SWL, S.SWH from packet or Init Cookies
	 *	      Continue with S.state == RESPOND
	 *	      (* A Response packet will be generated in Step 11 *)
	 *	 Otherwise,
	 *	      Generate Reset(No Connection) unless P.type == Reset
	 *	      Drop packet and return
	 *
	 * NOTE: the check for the packet types is done in
	 *	 dccp_rcv_state_process
	 */
	if (sk->sk_state == DCCP_LISTEN) {
		struct sock *nsk = dccp_v6_hnd_req(sk, skb);

		if (nsk == NULL)
			goto discard;
		/*
		 * Queue it on the new socket if the new socket is active,
		 * otherwise we just shortcircuit this and continue with
		 * the new socket..
		 */
		if (nsk != sk) {
			if (dccp_child_process(sk, nsk, skb))
				goto reset;
			if (opt_skb != NULL)
				__kfree_skb(opt_skb);
			return 0;
		}
	}

	if (dccp_rcv_state_process(sk, skb, dccp_hdr(skb), skb->len))
		goto reset;
	if (opt_skb) {
		/* XXX This is where we would goto ipv6_pktoptions. */
		__kfree_skb(opt_skb);
	}
	return 0;

reset:
	dccp_v6_ctl_send_reset(sk, skb);
discard:
	if (opt_skb != NULL)
		__kfree_skb(opt_skb);
	kfree_skb(skb);
	return 0;
}

static int dccp_v6_rcv(struct sk_buff *skb)
{
	const struct dccp_hdr *dh;
	struct sock *sk;
	int min_cov;

	/* Step 1: Check header basics */

	if (dccp_invalid_packet(skb))
		goto discard_it;

	/* Step 1: If header checksum is incorrect, drop packet and return. */
	if (dccp_v6_csum_finish(skb, &ipv6_hdr(skb)->saddr,
				     &ipv6_hdr(skb)->daddr)) {
		DCCP_WARN("dropped packet with invalid checksum\n");
		goto discard_it;
	}

	dh = dccp_hdr(skb);

	DCCP_SKB_CB(skb)->dccpd_seq  = dccp_hdr_seq(dh);
	DCCP_SKB_CB(skb)->dccpd_type = dh->dccph_type;

	if (dccp_packet_without_ack(skb))
		DCCP_SKB_CB(skb)->dccpd_ack_seq = DCCP_PKT_WITHOUT_ACK_SEQ;
	else
		DCCP_SKB_CB(skb)->dccpd_ack_seq = dccp_hdr_ack_seq(skb);

	/* Step 2:
	 *	Look up flow ID in table and get corresponding socket */
	sk = __inet6_lookup_skb(&dccp_hashinfo, skb,
			        dh->dccph_sport, dh->dccph_dport);
	/*
	 * Step 2:
	 *	If no socket ...
	 */
	if (sk == NULL) {
		dccp_pr_debug("failed to look up flow ID in table and "
			      "get corresponding socket\n");
		goto no_dccp_socket;
	}

	/*
	 * Step 2:
	 *	... or S.state == TIMEWAIT,
	 *		Generate Reset(No Connection) unless P.type == Reset
	 *		Drop packet and return
	 */
	if (sk->sk_state == DCCP_TIME_WAIT) {
		dccp_pr_debug("sk->sk_state == DCCP_TIME_WAIT: do_time_wait\n");
		inet_twsk_put(inet_twsk(sk));
		goto no_dccp_socket;
	}

	/*
	 * RFC 4340, sec. 9.2.1: Minimum Checksum Coverage
	 *	o if MinCsCov = 0, only packets with CsCov = 0 are accepted
	 *	o if MinCsCov > 0, also accept packets with CsCov >= MinCsCov
	 */
	min_cov = dccp_sk(sk)->dccps_pcrlen;
	if (dh->dccph_cscov  &&  (min_cov == 0 || dh->dccph_cscov < min_cov))  {
		dccp_pr_debug("Packet CsCov %d does not satisfy MinCsCov %d\n",
			      dh->dccph_cscov, min_cov);
		/* FIXME: send Data Dropped option (see also dccp_v4_rcv) */
		goto discard_and_relse;
	}

	if (!xfrm6_policy_check(sk, XFRM_POLICY_IN, skb))
		goto discard_and_relse;

	return sk_receive_skb(sk, skb, 1) ? -1 : 0;

no_dccp_socket:
	if (!xfrm6_policy_check(NULL, XFRM_POLICY_IN, skb))
		goto discard_it;
	/*
	 * Step 2:
	 *	If no socket ...
	 *		Generate Reset(No Connection) unless P.type == Reset
	 *		Drop packet and return
	 */
	if (dh->dccph_type != DCCP_PKT_RESET) {
		DCCP_SKB_CB(skb)->dccpd_reset_code =
					DCCP_RESET_CODE_NO_CONNECTION;
		dccp_v6_ctl_send_reset(sk, skb);
	}

discard_it:
	kfree_skb(skb);
	return 0;

discard_and_relse:
	sock_put(sk);
	goto discard_it;
}

static int dccp_v6_connect(struct sock *sk, struct sockaddr *uaddr,
			   int addr_len)
{
	struct sockaddr_in6 *usin = (struct sockaddr_in6 *)uaddr;
	struct inet_connection_sock *icsk = inet_csk(sk);
	struct inet_sock *inet = inet_sk(sk);
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct dccp_sock *dp = dccp_sk(sk);
	struct in6_addr *saddr = NULL, *final_p, final;
	struct flowi6 fl6;
	struct dst_entry *dst;
	int addr_type;
	int err;

	dp->dccps_role = DCCP_ROLE_CLIENT;

	if (addr_len < SIN6_LEN_RFC2133)
		return -EINVAL;

	if (usin->sin6_family != AF_INET6)
		return -EAFNOSUPPORT;

	memset(&fl6, 0, sizeof(fl6));

	if (np->sndflow) {
		fl6.flowlabel = usin->sin6_flowinfo & IPV6_FLOWINFO_MASK;
		IP6_ECN_flow_init(fl6.flowlabel);
		if (fl6.flowlabel & IPV6_FLOWLABEL_MASK) {
			struct ip6_flowlabel *flowlabel;
			flowlabel = fl6_sock_lookup(sk, fl6.flowlabel);
			if (flowlabel == NULL)
				return -EINVAL;
			usin->sin6_addr = flowlabel->dst;
			fl6_sock_release(flowlabel);
		}
	}
	/*
	 * connect() to INADDR_ANY means loopback (BSD'ism).
	 */
	if (ipv6_addr_any(&usin->sin6_addr))
		usin->sin6_addr.s6_addr[15] = 1;

	addr_type = ipv6_addr_type(&usin->sin6_addr);

	if (addr_type & IPV6_ADDR_MULTICAST)
		return -ENETUNREACH;

	if (addr_type & IPV6_ADDR_LINKLOCAL) {
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

	np->daddr = usin->sin6_addr;
	np->flow_label = fl6.flowlabel;

	/*
	 * DCCP over IPv4
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

		icsk->icsk_af_ops = &dccp_ipv6_mapped;
		sk->sk_backlog_rcv = dccp_v4_do_rcv;

		err = dccp_v4_connect(sk, (struct sockaddr *)&sin, sizeof(sin));
		if (err) {
			icsk->icsk_ext_hdr_len = exthdrlen;
			icsk->icsk_af_ops = &dccp_ipv6_af_ops;
			sk->sk_backlog_rcv = dccp_v6_do_rcv;
			goto failure;
		}
		ipv6_addr_set_v4mapped(inet->inet_saddr, &np->saddr);
		ipv6_addr_set_v4mapped(inet->inet_rcv_saddr, &np->rcv_saddr);

		return err;
	}

	if (!ipv6_addr_any(&np->rcv_saddr))
		saddr = &np->rcv_saddr;

	fl6.flowi6_proto = IPPROTO_DCCP;
	fl6.daddr = np->daddr;
	fl6.saddr = saddr ? *saddr : np->saddr;
	fl6.flowi6_oif = sk->sk_bound_dev_if;
	fl6.fl6_dport = usin->sin6_port;
	fl6.fl6_sport = inet->inet_sport;
	security_sk_classify_flow(sk, flowi6_to_flowi(&fl6));

	final_p = fl6_update_dst(&fl6, np->opt, &final);

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

	__ip6_dst_store(sk, dst, NULL, NULL);

	icsk->icsk_ext_hdr_len = 0;
	if (np->opt != NULL)
		icsk->icsk_ext_hdr_len = (np->opt->opt_flen +
					  np->opt->opt_nflen);

	inet->inet_dport = usin->sin6_port;

	dccp_set_state(sk, DCCP_REQUESTING);
	err = inet6_hash_connect(&dccp_death_row, sk);
	if (err)
		goto late_failure;

	dp->dccps_iss = secure_dccpv6_sequence_number(np->saddr.s6_addr32,
						      np->daddr.s6_addr32,
						      inet->inet_sport,
						      inet->inet_dport);
	err = dccp_connect(sk);
	if (err)
		goto late_failure;

	return 0;

late_failure:
	dccp_set_state(sk, DCCP_CLOSED);
	__sk_dst_reset(sk);
failure:
	inet->inet_dport = 0;
	sk->sk_route_caps = 0;
	return err;
}

static const struct inet_connection_sock_af_ops dccp_ipv6_af_ops = {
	.queue_xmit	   = inet6_csk_xmit,
	.send_check	   = dccp_v6_send_check,
	.rebuild_header	   = inet6_sk_rebuild_header,
	.conn_request	   = dccp_v6_conn_request,
	.syn_recv_sock	   = dccp_v6_request_recv_sock,
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

/*
 *	DCCP over IPv4 via INET6 API
 */
static const struct inet_connection_sock_af_ops dccp_ipv6_mapped = {
	.queue_xmit	   = ip_queue_xmit,
	.send_check	   = dccp_v4_send_check,
	.rebuild_header	   = inet_sk_rebuild_header,
	.conn_request	   = dccp_v6_conn_request,
	.syn_recv_sock	   = dccp_v6_request_recv_sock,
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

/* NOTE: A lot of things set to zero explicitly by call to
 *       sk_alloc() so need not be done here.
 */
static int dccp_v6_init_sock(struct sock *sk)
{
	static __u8 dccp_v6_ctl_sock_initialized;
	int err = dccp_init_sock(sk, dccp_v6_ctl_sock_initialized);

	if (err == 0) {
		if (unlikely(!dccp_v6_ctl_sock_initialized))
			dccp_v6_ctl_sock_initialized = 1;
		inet_csk(sk)->icsk_af_ops = &dccp_ipv6_af_ops;
	}

	return err;
}

static void dccp_v6_destroy_sock(struct sock *sk)
{
	dccp_destroy_sock(sk);
	inet6_destroy_sock(sk);
}

static struct timewait_sock_ops dccp6_timewait_sock_ops = {
	.twsk_obj_size	= sizeof(struct dccp6_timewait_sock),
};

static struct proto dccp_v6_prot = {
	.name		   = "DCCPv6",
	.owner		   = THIS_MODULE,
	.close		   = dccp_close,
	.connect	   = dccp_v6_connect,
	.disconnect	   = dccp_disconnect,
	.ioctl		   = dccp_ioctl,
	.init		   = dccp_v6_init_sock,
	.setsockopt	   = dccp_setsockopt,
	.getsockopt	   = dccp_getsockopt,
	.sendmsg	   = dccp_sendmsg,
	.recvmsg	   = dccp_recvmsg,
	.backlog_rcv	   = dccp_v6_do_rcv,
	.hash		   = dccp_v6_hash,
	.unhash		   = inet_unhash,
	.accept		   = inet_csk_accept,
	.get_port	   = inet_csk_get_port,
	.shutdown	   = dccp_shutdown,
	.destroy	   = dccp_v6_destroy_sock,
	.orphan_count	   = &dccp_orphan_count,
	.max_header	   = MAX_DCCP_HEADER,
	.obj_size	   = sizeof(struct dccp6_sock),
	.slab_flags	   = SLAB_DESTROY_BY_RCU,
	.rsk_prot	   = &dccp6_request_sock_ops,
	.twsk_prot	   = &dccp6_timewait_sock_ops,
	.h.hashinfo	   = &dccp_hashinfo,
#ifdef CONFIG_COMPAT
	.compat_setsockopt = compat_dccp_setsockopt,
	.compat_getsockopt = compat_dccp_getsockopt,
#endif
};

static const struct inet6_protocol dccp_v6_protocol = {
	.handler	= dccp_v6_rcv,
	.err_handler	= dccp_v6_err,
	.flags		= INET6_PROTO_NOPOLICY | INET6_PROTO_FINAL,
};

static const struct proto_ops inet6_dccp_ops = {
	.family		   = PF_INET6,
	.owner		   = THIS_MODULE,
	.release	   = inet6_release,
	.bind		   = inet6_bind,
	.connect	   = inet_stream_connect,
	.socketpair	   = sock_no_socketpair,
	.accept		   = inet_accept,
	.getname	   = inet6_getname,
	.poll		   = dccp_poll,
	.ioctl		   = inet6_ioctl,
	.listen		   = inet_dccp_listen,
	.shutdown	   = inet_shutdown,
	.setsockopt	   = sock_common_setsockopt,
	.getsockopt	   = sock_common_getsockopt,
	.sendmsg	   = inet_sendmsg,
	.recvmsg	   = sock_common_recvmsg,
	.mmap		   = sock_no_mmap,
	.sendpage	   = sock_no_sendpage,
#ifdef CONFIG_COMPAT
	.compat_setsockopt = compat_sock_common_setsockopt,
	.compat_getsockopt = compat_sock_common_getsockopt,
#endif
};

static struct inet_protosw dccp_v6_protosw = {
	.type		= SOCK_DCCP,
	.protocol	= IPPROTO_DCCP,
	.prot		= &dccp_v6_prot,
	.ops		= &inet6_dccp_ops,
	.flags		= INET_PROTOSW_ICSK,
};

static int __net_init dccp_v6_init_net(struct net *net)
{
	if (dccp_hashinfo.bhash == NULL)
		return -ESOCKTNOSUPPORT;

	return inet_ctl_sock_create(&net->dccp.v6_ctl_sk, PF_INET6,
				    SOCK_DCCP, IPPROTO_DCCP, net);
}

static void __net_exit dccp_v6_exit_net(struct net *net)
{
	inet_ctl_sock_destroy(net->dccp.v6_ctl_sk);
}

static struct pernet_operations dccp_v6_ops = {
	.init   = dccp_v6_init_net,
	.exit   = dccp_v6_exit_net,
};

static int __init dccp_v6_init(void)
{
	int err = proto_register(&dccp_v6_prot, 1);

	if (err != 0)
		goto out;

	err = inet6_add_protocol(&dccp_v6_protocol, IPPROTO_DCCP);
	if (err != 0)
		goto out_unregister_proto;

	inet6_register_protosw(&dccp_v6_protosw);

	err = register_pernet_subsys(&dccp_v6_ops);
	if (err != 0)
		goto out_destroy_ctl_sock;
out:
	return err;

out_destroy_ctl_sock:
	inet6_del_protocol(&dccp_v6_protocol, IPPROTO_DCCP);
	inet6_unregister_protosw(&dccp_v6_protosw);
out_unregister_proto:
	proto_unregister(&dccp_v6_prot);
	goto out;
}

static void __exit dccp_v6_exit(void)
{
	unregister_pernet_subsys(&dccp_v6_ops);
	inet6_del_protocol(&dccp_v6_protocol, IPPROTO_DCCP);
	inet6_unregister_protosw(&dccp_v6_protosw);
	proto_unregister(&dccp_v6_prot);
}

module_init(dccp_v6_init);
module_exit(dccp_v6_exit);

/*
 * __stringify doesn't likes enums, so use SOCK_DCCP (6) and IPPROTO_DCCP (33)
 * values directly, Also cover the case where the protocol is not specified,
 * i.e. net-pf-PF_INET6-proto-0-type-SOCK_DCCP
 */
MODULE_ALIAS_NET_PF_PROTO_TYPE(PF_INET6, 33, 6);
MODULE_ALIAS_NET_PF_PROTO_TYPE(PF_INET6, 0, 6);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Arnaldo Carvalho de Melo <acme@mandriva.com>");
MODULE_DESCRIPTION("DCCPv6 - Datagram Congestion Controlled Protocol");
