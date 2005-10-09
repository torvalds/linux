/*
 *  net/dccp/ipv4.c
 *
 *  An implementation of the DCCP protocol
 *  Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/dccp.h>
#include <linux/icmp.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/random.h>

#include <net/icmp.h>
#include <net/inet_hashtables.h>
#include <net/sock.h>
#include <net/tcp_states.h>
#include <net/xfrm.h>

#include "ackvec.h"
#include "ccid.h"
#include "dccp.h"

struct inet_hashinfo __cacheline_aligned dccp_hashinfo = {
	.lhash_lock	= RW_LOCK_UNLOCKED,
	.lhash_users	= ATOMIC_INIT(0),
	.lhash_wait = __WAIT_QUEUE_HEAD_INITIALIZER(dccp_hashinfo.lhash_wait),
	.portalloc_lock	= SPIN_LOCK_UNLOCKED,
	.port_rover	= 1024 - 1,
};

EXPORT_SYMBOL_GPL(dccp_hashinfo);

static int dccp_v4_get_port(struct sock *sk, const unsigned short snum)
{
	return inet_csk_get_port(&dccp_hashinfo, sk, snum);
}

static void dccp_v4_hash(struct sock *sk)
{
	inet_hash(&dccp_hashinfo, sk);
}

static void dccp_v4_unhash(struct sock *sk)
{
	inet_unhash(&dccp_hashinfo, sk);
}

/* called with local bh disabled */
static int __dccp_v4_check_established(struct sock *sk, const __u16 lport,
				      struct inet_timewait_sock **twp)
{
	struct inet_sock *inet = inet_sk(sk);
	const u32 daddr = inet->rcv_saddr;
	const u32 saddr = inet->daddr;
	const int dif = sk->sk_bound_dev_if;
	INET_ADDR_COOKIE(acookie, saddr, daddr)
	const __u32 ports = INET_COMBINED_PORTS(inet->dport, lport);
	unsigned int hash = inet_ehashfn(daddr, lport, saddr, inet->dport);
	struct inet_ehash_bucket *head = inet_ehash_bucket(&dccp_hashinfo, hash);
	const struct sock *sk2;
	const struct hlist_node *node;
	struct inet_timewait_sock *tw;

	prefetch(head->chain.first);
	write_lock(&head->lock);

	/* Check TIME-WAIT sockets first. */
	sk_for_each(sk2, node, &(head + dccp_hashinfo.ehash_size)->chain) {
		tw = inet_twsk(sk2);

		if (INET_TW_MATCH(sk2, hash, acookie, saddr, daddr, ports, dif))
			goto not_unique;
	}
	tw = NULL;

	/* And established part... */
	sk_for_each(sk2, node, &head->chain) {
		if (INET_MATCH(sk2, hash, acookie, saddr, daddr, ports, dif))
			goto not_unique;
	}

	/* Must record num and sport now. Otherwise we will see
	 * in hash table socket with a funny identity. */
	inet->num = lport;
	inet->sport = htons(lport);
	sk->sk_hash = hash;
	BUG_TRAP(sk_unhashed(sk));
	__sk_add_node(sk, &head->chain);
	sock_prot_inc_use(sk->sk_prot);
	write_unlock(&head->lock);

	if (twp != NULL) {
		*twp = tw;
		NET_INC_STATS_BH(LINUX_MIB_TIMEWAITRECYCLED);
	} else if (tw != NULL) {
		/* Silly. Should hash-dance instead... */
		inet_twsk_deschedule(tw, &dccp_death_row);
		NET_INC_STATS_BH(LINUX_MIB_TIMEWAITRECYCLED);

		inet_twsk_put(tw);
	}

	return 0;

not_unique:
	write_unlock(&head->lock);
	return -EADDRNOTAVAIL;
}

/*
 * Bind a port for a connect operation and hash it.
 */
static int dccp_v4_hash_connect(struct sock *sk)
{
	const unsigned short snum = inet_sk(sk)->num;
 	struct inet_bind_hashbucket *head;
 	struct inet_bind_bucket *tb;
	int ret;

 	if (snum == 0) {
 		int rover;
 		int low = sysctl_local_port_range[0];
 		int high = sysctl_local_port_range[1];
 		int remaining = (high - low) + 1;
		struct hlist_node *node;
 		struct inet_timewait_sock *tw = NULL;

 		local_bh_disable();

 		/* TODO. Actually it is not so bad idea to remove
 		 * dccp_hashinfo.portalloc_lock before next submission to
		 * Linus.
 		 * As soon as we touch this place at all it is time to think.
 		 *
 		 * Now it protects single _advisory_ variable
		 * dccp_hashinfo.port_rover, hence it is mostly useless.
 		 * Code will work nicely if we just delete it, but
 		 * I am afraid in contented case it will work not better or
 		 * even worse: another cpu just will hit the same bucket
 		 * and spin there.
 		 * So some cpu salt could remove both contention and
 		 * memory pingpong. Any ideas how to do this in a nice way?
 		 */
 		spin_lock(&dccp_hashinfo.portalloc_lock);
 		rover = dccp_hashinfo.port_rover;

 		do {
 			rover++;
 			if ((rover < low) || (rover > high))
 				rover = low;
 			head = &dccp_hashinfo.bhash[inet_bhashfn(rover,
						    dccp_hashinfo.bhash_size)];
 			spin_lock(&head->lock);

 			/* Does not bother with rcv_saddr checks,
 			 * because the established check is already
 			 * unique enough.
 			 */
			inet_bind_bucket_for_each(tb, node, &head->chain) {
 				if (tb->port == rover) {
 					BUG_TRAP(!hlist_empty(&tb->owners));
 					if (tb->fastreuse >= 0)
 						goto next_port;
 					if (!__dccp_v4_check_established(sk,
									 rover,
									 &tw))
 						goto ok;
 					goto next_port;
 				}
 			}

 			tb = inet_bind_bucket_create(dccp_hashinfo.bind_bucket_cachep,
						     head, rover);
 			if (tb == NULL) {
 				spin_unlock(&head->lock);
 				break;
 			}
 			tb->fastreuse = -1;
 			goto ok;

 		next_port:
 			spin_unlock(&head->lock);
 		} while (--remaining > 0);
 		dccp_hashinfo.port_rover = rover;
 		spin_unlock(&dccp_hashinfo.portalloc_lock);

 		local_bh_enable();

 		return -EADDRNOTAVAIL;

ok:
 		/* All locks still held and bhs disabled */
 		dccp_hashinfo.port_rover = rover;
 		spin_unlock(&dccp_hashinfo.portalloc_lock);

 		inet_bind_hash(sk, tb, rover);
		if (sk_unhashed(sk)) {
 			inet_sk(sk)->sport = htons(rover);
 			__inet_hash(&dccp_hashinfo, sk, 0);
 		}
 		spin_unlock(&head->lock);

 		if (tw != NULL) {
 			inet_twsk_deschedule(tw, &dccp_death_row);
 			inet_twsk_put(tw);
 		}

		ret = 0;
		goto out;
 	}

 	head = &dccp_hashinfo.bhash[inet_bhashfn(snum,
						 dccp_hashinfo.bhash_size)];
 	tb   = inet_csk(sk)->icsk_bind_hash;
	spin_lock_bh(&head->lock);
	if (sk_head(&tb->owners) == sk && sk->sk_bind_node.next == NULL) {
		__inet_hash(&dccp_hashinfo, sk, 0);
		spin_unlock_bh(&head->lock);
		return 0;
	} else {
		spin_unlock(&head->lock);
		/* No definite answer... Walk to established hash table */
		ret = __dccp_v4_check_established(sk, snum, NULL);
out:
		local_bh_enable();
		return ret;
	}
}

static int dccp_v4_connect(struct sock *sk, struct sockaddr *uaddr,
			   int addr_len)
{
	struct inet_sock *inet = inet_sk(sk);
	struct dccp_sock *dp = dccp_sk(sk);
	const struct sockaddr_in *usin = (struct sockaddr_in *)uaddr;
	struct rtable *rt;
	u32 daddr, nexthop;
	int tmp;
	int err;

	dp->dccps_role = DCCP_ROLE_CLIENT;

	if (dccp_service_not_initialized(sk))
		return -EPROTO;

	if (addr_len < sizeof(struct sockaddr_in))
		return -EINVAL;

	if (usin->sin_family != AF_INET)
		return -EAFNOSUPPORT;

	nexthop = daddr = usin->sin_addr.s_addr;
	if (inet->opt != NULL && inet->opt->srr) {
		if (daddr == 0)
			return -EINVAL;
		nexthop = inet->opt->faddr;
	}

	tmp = ip_route_connect(&rt, nexthop, inet->saddr,
			       RT_CONN_FLAGS(sk), sk->sk_bound_dev_if,
			       IPPROTO_DCCP,
			       inet->sport, usin->sin_port, sk);
	if (tmp < 0)
		return tmp;

	if (rt->rt_flags & (RTCF_MULTICAST | RTCF_BROADCAST)) {
		ip_rt_put(rt);
		return -ENETUNREACH;
	}

	if (inet->opt == NULL || !inet->opt->srr)
		daddr = rt->rt_dst;

	if (inet->saddr == 0)
		inet->saddr = rt->rt_src;
	inet->rcv_saddr = inet->saddr;

	inet->dport = usin->sin_port;
	inet->daddr = daddr;

	dp->dccps_ext_header_len = 0;
	if (inet->opt != NULL)
		dp->dccps_ext_header_len = inet->opt->optlen;
	/*
	 * Socket identity is still unknown (sport may be zero).
	 * However we set state to DCCP_REQUESTING and not releasing socket
	 * lock select source port, enter ourselves into the hash tables and
	 * complete initialization after this.
	 */
	dccp_set_state(sk, DCCP_REQUESTING);
	err = dccp_v4_hash_connect(sk);
	if (err != 0)
		goto failure;

	err = ip_route_newports(&rt, inet->sport, inet->dport, sk);
	if (err != 0)
		goto failure;

	/* OK, now commit destination to socket.  */
	sk_setup_caps(sk, &rt->u.dst);

	dp->dccps_gar =
		dp->dccps_iss = secure_dccp_sequence_number(inet->saddr,
							    inet->daddr,
							    inet->sport,
							    usin->sin_port);
	dccp_update_gss(sk, dp->dccps_iss);

	/*
	 * SWL and AWL are initially adjusted so that they are not less than
	 * the initial Sequence Numbers received and sent, respectively:
	 *	SWL := max(GSR + 1 - floor(W/4), ISR),
	 *	AWL := max(GSS - W' + 1, ISS).
	 * These adjustments MUST be applied only at the beginning of the
	 * connection.
	 */
	dccp_set_seqno(&dp->dccps_awl, max48(dp->dccps_awl, dp->dccps_iss));

	inet->id = dp->dccps_iss ^ jiffies;

	err = dccp_connect(sk);
	rt = NULL;
	if (err != 0)
		goto failure;
out:
	return err;
failure:
	/*
	 * This unhashes the socket and releases the local port, if necessary.
	 */
	dccp_set_state(sk, DCCP_CLOSED);
	ip_rt_put(rt);
	sk->sk_route_caps = 0;
	inet->dport = 0;
	goto out;
}

/*
 * This routine does path mtu discovery as defined in RFC1191.
 */
static inline void dccp_do_pmtu_discovery(struct sock *sk,
					  const struct iphdr *iph,
					  u32 mtu)
{
	struct dst_entry *dst;
	const struct inet_sock *inet = inet_sk(sk);
	const struct dccp_sock *dp = dccp_sk(sk);

	/* We are not interested in DCCP_LISTEN and request_socks (RESPONSEs
	 * send out by Linux are always < 576bytes so they should go through
	 * unfragmented).
	 */
	if (sk->sk_state == DCCP_LISTEN)
		return;

	/* We don't check in the destentry if pmtu discovery is forbidden
	 * on this route. We just assume that no packet_to_big packets
	 * are send back when pmtu discovery is not active.
     	 * There is a small race when the user changes this flag in the
	 * route, but I think that's acceptable.
	 */
	if ((dst = __sk_dst_check(sk, 0)) == NULL)
		return;

	dst->ops->update_pmtu(dst, mtu);

	/* Something is about to be wrong... Remember soft error
	 * for the case, if this connection will not able to recover.
	 */
	if (mtu < dst_mtu(dst) && ip_dont_fragment(sk, dst))
		sk->sk_err_soft = EMSGSIZE;

	mtu = dst_mtu(dst);

	if (inet->pmtudisc != IP_PMTUDISC_DONT &&
	    dp->dccps_pmtu_cookie > mtu) {
		dccp_sync_mss(sk, mtu);

		/*
		 * From: draft-ietf-dccp-spec-11.txt
		 *
		 *	DCCP-Sync packets are the best choice for upward
		 *	probing, since DCCP-Sync probes do not risk application
		 *	data loss.
		 */
		dccp_send_sync(sk, dp->dccps_gsr, DCCP_PKT_SYNC);
	} /* else let the usual retransmit timer handle it */
}

static void dccp_v4_ctl_send_ack(struct sk_buff *rxskb)
{
	int err;
	struct dccp_hdr *rxdh = dccp_hdr(rxskb), *dh;
	const int dccp_hdr_ack_len = sizeof(struct dccp_hdr) +
				     sizeof(struct dccp_hdr_ext) +
				     sizeof(struct dccp_hdr_ack_bits);
	struct sk_buff *skb;

	if (((struct rtable *)rxskb->dst)->rt_type != RTN_LOCAL)
		return;

	skb = alloc_skb(MAX_DCCP_HEADER + 15, GFP_ATOMIC);
	if (skb == NULL)
		return;

	/* Reserve space for headers. */
	skb_reserve(skb, MAX_DCCP_HEADER);

	skb->dst = dst_clone(rxskb->dst);

	skb->h.raw = skb_push(skb, dccp_hdr_ack_len);
	dh = dccp_hdr(skb);
	memset(dh, 0, dccp_hdr_ack_len);

	/* Build DCCP header and checksum it. */
	dh->dccph_type	   = DCCP_PKT_ACK;
	dh->dccph_sport	   = rxdh->dccph_dport;
	dh->dccph_dport	   = rxdh->dccph_sport;
	dh->dccph_doff	   = dccp_hdr_ack_len / 4;
	dh->dccph_x	   = 1;

	dccp_hdr_set_seq(dh, DCCP_SKB_CB(rxskb)->dccpd_ack_seq);
	dccp_hdr_set_ack(dccp_hdr_ack_bits(skb),
			 DCCP_SKB_CB(rxskb)->dccpd_seq);

	bh_lock_sock(dccp_ctl_socket->sk);
	err = ip_build_and_send_pkt(skb, dccp_ctl_socket->sk,
				    rxskb->nh.iph->daddr,
				    rxskb->nh.iph->saddr, NULL);
	bh_unlock_sock(dccp_ctl_socket->sk);

	if (err == NET_XMIT_CN || err == 0) {
		DCCP_INC_STATS_BH(DCCP_MIB_OUTSEGS);
		DCCP_INC_STATS_BH(DCCP_MIB_OUTRSTS);
	}
}

static void dccp_v4_reqsk_send_ack(struct sk_buff *skb,
				   struct request_sock *req)
{
	dccp_v4_ctl_send_ack(skb);
}

static int dccp_v4_send_response(struct sock *sk, struct request_sock *req,
				 struct dst_entry *dst)
{
	int err = -1;
	struct sk_buff *skb;

	/* First, grab a route. */
	
	if (dst == NULL && (dst = inet_csk_route_req(sk, req)) == NULL)
		goto out;

	skb = dccp_make_response(sk, dst, req);
	if (skb != NULL) {
		const struct inet_request_sock *ireq = inet_rsk(req);

		err = ip_build_and_send_pkt(skb, sk, ireq->loc_addr,
					    ireq->rmt_addr,
					    ireq->opt);
		if (err == NET_XMIT_CN)
			err = 0;
	}

out:
	dst_release(dst);
	return err;
}

/*
 * This routine is called by the ICMP module when it gets some sort of error
 * condition. If err < 0 then the socket should be closed and the error
 * returned to the user. If err > 0 it's just the icmp type << 8 | icmp code.
 * After adjustment header points to the first 8 bytes of the tcp header. We
 * need to find the appropriate port.
 *
 * The locking strategy used here is very "optimistic". When someone else
 * accesses the socket the ICMP is just dropped and for some paths there is no
 * check at all. A more general error queue to queue errors for later handling
 * is probably better.
 */
void dccp_v4_err(struct sk_buff *skb, u32 info)
{
	const struct iphdr *iph = (struct iphdr *)skb->data;
	const struct dccp_hdr *dh = (struct dccp_hdr *)(skb->data +
							(iph->ihl << 2));
	struct dccp_sock *dp;
	struct inet_sock *inet;
	const int type = skb->h.icmph->type;
	const int code = skb->h.icmph->code;
	struct sock *sk;
	__u64 seq;
	int err;

	if (skb->len < (iph->ihl << 2) + 8) {
		ICMP_INC_STATS_BH(ICMP_MIB_INERRORS);
		return;
	}

	sk = inet_lookup(&dccp_hashinfo, iph->daddr, dh->dccph_dport,
			 iph->saddr, dh->dccph_sport, inet_iif(skb));
	if (sk == NULL) {
		ICMP_INC_STATS_BH(ICMP_MIB_INERRORS);
		return;
	}

	if (sk->sk_state == DCCP_TIME_WAIT) {
		inet_twsk_put((struct inet_timewait_sock *)sk);
		return;
	}

	bh_lock_sock(sk);
	/* If too many ICMPs get dropped on busy
	 * servers this needs to be solved differently.
	 */
	if (sock_owned_by_user(sk))
		NET_INC_STATS_BH(LINUX_MIB_LOCKDROPPEDICMPS);

	if (sk->sk_state == DCCP_CLOSED)
		goto out;

	dp = dccp_sk(sk);
	seq = dccp_hdr_seq(skb);
	if (sk->sk_state != DCCP_LISTEN &&
	    !between48(seq, dp->dccps_swl, dp->dccps_swh)) {
		NET_INC_STATS(LINUX_MIB_OUTOFWINDOWICMPS);
		goto out;
	}

	switch (type) {
	case ICMP_SOURCE_QUENCH:
		/* Just silently ignore these. */
		goto out;
	case ICMP_PARAMETERPROB:
		err = EPROTO;
		break;
	case ICMP_DEST_UNREACH:
		if (code > NR_ICMP_UNREACH)
			goto out;

		if (code == ICMP_FRAG_NEEDED) { /* PMTU discovery (RFC1191) */
			if (!sock_owned_by_user(sk))
				dccp_do_pmtu_discovery(sk, iph, info);
			goto out;
		}

		err = icmp_err_convert[code].errno;
		break;
	case ICMP_TIME_EXCEEDED:
		err = EHOSTUNREACH;
		break;
	default:
		goto out;
	}

	switch (sk->sk_state) {
		struct request_sock *req , **prev;
	case DCCP_LISTEN:
		if (sock_owned_by_user(sk))
			goto out;
		req = inet_csk_search_req(sk, &prev, dh->dccph_dport,
					  iph->daddr, iph->saddr);
		if (!req)
			goto out;

		/*
		 * ICMPs are not backlogged, hence we cannot get an established
		 * socket here.
		 */
		BUG_TRAP(!req->sk);

		if (seq != dccp_rsk(req)->dreq_iss) {
			NET_INC_STATS_BH(LINUX_MIB_OUTOFWINDOWICMPS);
			goto out;
		}
		/*
		 * Still in RESPOND, just remove it silently.
		 * There is no good way to pass the error to the newly
		 * created socket, and POSIX does not want network
		 * errors returned from accept().
		 */
		inet_csk_reqsk_queue_drop(sk, req, prev);
		goto out;

	case DCCP_REQUESTING:
	case DCCP_RESPOND:
		if (!sock_owned_by_user(sk)) {
			DCCP_INC_STATS_BH(DCCP_MIB_ATTEMPTFAILS);
			sk->sk_err = err;

			sk->sk_error_report(sk);

			dccp_done(sk);
		} else
			sk->sk_err_soft = err;
		goto out;
	}

	/* If we've already connected we will keep trying
	 * until we time out, or the user gives up.
	 *
	 * rfc1122 4.2.3.9 allows to consider as hard errors
	 * only PROTO_UNREACH and PORT_UNREACH (well, FRAG_FAILED too,
	 * but it is obsoleted by pmtu discovery).
	 *
	 * Note, that in modern internet, where routing is unreliable
	 * and in each dark corner broken firewalls sit, sending random
	 * errors ordered by their masters even this two messages finally lose
	 * their original sense (even Linux sends invalid PORT_UNREACHs)
	 *
	 * Now we are in compliance with RFCs.
	 *							--ANK (980905)
	 */

	inet = inet_sk(sk);
	if (!sock_owned_by_user(sk) && inet->recverr) {
		sk->sk_err = err;
		sk->sk_error_report(sk);
	} else /* Only an error on timeout */
		sk->sk_err_soft = err;
out:
	bh_unlock_sock(sk);
	sock_put(sk);
}

int dccp_v4_send_reset(struct sock *sk, enum dccp_reset_codes code)
{
	struct sk_buff *skb;
	/*
	 * FIXME: what if rebuild_header fails?
	 * Should we be doing a rebuild_header here?
	 */
	int err = inet_sk_rebuild_header(sk);

	if (err != 0)
		return err;

	skb = dccp_make_reset(sk, sk->sk_dst_cache, code);
	if (skb != NULL) {
		const struct inet_sock *inet = inet_sk(sk);

		err = ip_build_and_send_pkt(skb, sk,
					    inet->saddr, inet->daddr, NULL);
		if (err == NET_XMIT_CN)
			err = 0;
	}

	return err;
}

static inline u64 dccp_v4_init_sequence(const struct sock *sk,
					const struct sk_buff *skb)
{
	return secure_dccp_sequence_number(skb->nh.iph->daddr,
					   skb->nh.iph->saddr,
					   dccp_hdr(skb)->dccph_dport,
					   dccp_hdr(skb)->dccph_sport);
}

static inline int dccp_bad_service_code(const struct sock *sk,
					const __u32 service)
{
	const struct dccp_sock *dp = dccp_sk(sk);

	if (dp->dccps_service == service)
		return 0;
	return !dccp_list_has_service(dp->dccps_service_list, service);
}

int dccp_v4_conn_request(struct sock *sk, struct sk_buff *skb)
{
	struct inet_request_sock *ireq;
	struct dccp_sock dp;
	struct request_sock *req;
	struct dccp_request_sock *dreq;
	const __u32 saddr = skb->nh.iph->saddr;
	const __u32 daddr = skb->nh.iph->daddr;
 	const __u32 service = dccp_hdr_request(skb)->dccph_req_service;
	struct dccp_skb_cb *dcb = DCCP_SKB_CB(skb);
	__u8 reset_code = DCCP_RESET_CODE_TOO_BUSY;
	struct dst_entry *dst = NULL;

	/* Never answer to DCCP_PKT_REQUESTs send to broadcast or multicast */
	if (((struct rtable *)skb->dst)->rt_flags &
	    (RTCF_BROADCAST | RTCF_MULTICAST)) {
		reset_code = DCCP_RESET_CODE_NO_CONNECTION;
		goto drop;
	}

	if (dccp_bad_service_code(sk, service)) {
		reset_code = DCCP_RESET_CODE_BAD_SERVICE_CODE;
		goto drop;
 	}
	/*
	 * TW buckets are converted to open requests without
	 * limitations, they conserve resources and peer is
	 * evidently real one.
	 */
	if (inet_csk_reqsk_queue_is_full(sk))
		goto drop;

	/*
	 * Accept backlog is full. If we have already queued enough
	 * of warm entries in syn queue, drop request. It is better than
	 * clogging syn queue with openreqs with exponentially increasing
	 * timeout.
	 */
	if (sk_acceptq_is_full(sk) && inet_csk_reqsk_queue_young(sk) > 1)
		goto drop;

	req = reqsk_alloc(sk->sk_prot->rsk_prot);
	if (req == NULL)
		goto drop;

	/* FIXME: process options */

	dccp_openreq_init(req, &dp, skb);

	ireq = inet_rsk(req);
	ireq->loc_addr = daddr;
	ireq->rmt_addr = saddr;
	/* FIXME: Merge Aristeu's option parsing code when ready */
	req->rcv_wnd	= 100; /* Fake, option parsing will get the
				  right value */
	ireq->opt	= NULL;

	/* 
	 * Step 3: Process LISTEN state
	 *
	 * Set S.ISR, S.GSR, S.SWL, S.SWH from packet or Init Cookie
	 *
	 * In fact we defer setting S.GSR, S.SWL, S.SWH to
	 * dccp_create_openreq_child.
	 */
	dreq = dccp_rsk(req);
	dreq->dreq_isr	   = dcb->dccpd_seq;
	dreq->dreq_iss	   = dccp_v4_init_sequence(sk, skb);
	dreq->dreq_service = service;

	if (dccp_v4_send_response(sk, req, dst))
		goto drop_and_free;

	inet_csk_reqsk_queue_hash_add(sk, req, DCCP_TIMEOUT_INIT);
	return 0;

drop_and_free:
	/*
	 * FIXME: should be reqsk_free after implementing req->rsk_ops
	 */
	__reqsk_free(req);
drop:
	DCCP_INC_STATS_BH(DCCP_MIB_ATTEMPTFAILS);
	dcb->dccpd_reset_code = reset_code;
	return -1;
}

/*
 * The three way handshake has completed - we got a valid ACK or DATAACK -
 * now create the new socket.
 *
 * This is the equivalent of TCP's tcp_v4_syn_recv_sock
 */
struct sock *dccp_v4_request_recv_sock(struct sock *sk, struct sk_buff *skb,
				       struct request_sock *req,
				       struct dst_entry *dst)
{
	struct inet_request_sock *ireq;
	struct inet_sock *newinet;
	struct dccp_sock *newdp;
	struct sock *newsk;

	if (sk_acceptq_is_full(sk))
		goto exit_overflow;

	if (dst == NULL && (dst = inet_csk_route_req(sk, req)) == NULL)
		goto exit;

	newsk = dccp_create_openreq_child(sk, req, skb);
	if (newsk == NULL)
		goto exit;

	sk_setup_caps(newsk, dst);

	newdp		   = dccp_sk(newsk);
	newinet		   = inet_sk(newsk);
	ireq		   = inet_rsk(req);
	newinet->daddr	   = ireq->rmt_addr;
	newinet->rcv_saddr = ireq->loc_addr;
	newinet->saddr	   = ireq->loc_addr;
	newinet->opt	   = ireq->opt;
	ireq->opt	   = NULL;
	newinet->mc_index  = inet_iif(skb);
	newinet->mc_ttl	   = skb->nh.iph->ttl;
	newinet->id	   = jiffies;

	dccp_sync_mss(newsk, dst_mtu(dst));

	__inet_hash(&dccp_hashinfo, newsk, 0);
	__inet_inherit_port(&dccp_hashinfo, sk, newsk);

	return newsk;

exit_overflow:
	NET_INC_STATS_BH(LINUX_MIB_LISTENOVERFLOWS);
exit:
	NET_INC_STATS_BH(LINUX_MIB_LISTENDROPS);
	dst_release(dst);
	return NULL;
}

static struct sock *dccp_v4_hnd_req(struct sock *sk, struct sk_buff *skb)
{
	const struct dccp_hdr *dh = dccp_hdr(skb);
	const struct iphdr *iph = skb->nh.iph;
	struct sock *nsk;
	struct request_sock **prev;
	/* Find possible connection requests. */
	struct request_sock *req = inet_csk_search_req(sk, &prev,
						       dh->dccph_sport,
						       iph->saddr, iph->daddr);
	if (req != NULL)
		return dccp_check_req(sk, skb, req, prev);

	nsk = __inet_lookup_established(&dccp_hashinfo,
					iph->saddr, dh->dccph_sport,
					iph->daddr, ntohs(dh->dccph_dport),
					inet_iif(skb));
	if (nsk != NULL) {
		if (nsk->sk_state != DCCP_TIME_WAIT) {
			bh_lock_sock(nsk);
			return nsk;
		}
		inet_twsk_put((struct inet_timewait_sock *)nsk);
		return NULL;
	}

	return sk;
}

int dccp_v4_checksum(const struct sk_buff *skb, const u32 saddr,
		     const u32 daddr)
{
	const struct dccp_hdr* dh = dccp_hdr(skb);
	int checksum_len;
	u32 tmp;

	if (dh->dccph_cscov == 0)
		checksum_len = skb->len;
	else {
		checksum_len = (dh->dccph_cscov + dh->dccph_x) * sizeof(u32);
		checksum_len = checksum_len < skb->len ? checksum_len :
							 skb->len;
	}

	tmp = csum_partial((unsigned char *)dh, checksum_len, 0);
	return csum_tcpudp_magic(saddr, daddr, checksum_len,
				 IPPROTO_DCCP, tmp);
}

static int dccp_v4_verify_checksum(struct sk_buff *skb,
				   const u32 saddr, const u32 daddr)
{
	struct dccp_hdr *dh = dccp_hdr(skb);
	int checksum_len;
	u32 tmp;

	if (dh->dccph_cscov == 0)
		checksum_len = skb->len;
	else {
		checksum_len = (dh->dccph_cscov + dh->dccph_x) * sizeof(u32);
		checksum_len = checksum_len < skb->len ? checksum_len :
							 skb->len;
	}
	tmp = csum_partial((unsigned char *)dh, checksum_len, 0);
	return csum_tcpudp_magic(saddr, daddr, checksum_len,
				 IPPROTO_DCCP, tmp) == 0 ? 0 : -1;
}

static struct dst_entry* dccp_v4_route_skb(struct sock *sk,
					   struct sk_buff *skb)
{
	struct rtable *rt;
	struct flowi fl = { .oif = ((struct rtable *)skb->dst)->rt_iif,
			    .nl_u = { .ip4_u =
				      { .daddr = skb->nh.iph->saddr,
					.saddr = skb->nh.iph->daddr,
					.tos = RT_CONN_FLAGS(sk) } },
			    .proto = sk->sk_protocol,
			    .uli_u = { .ports =
				       { .sport = dccp_hdr(skb)->dccph_dport,
					 .dport = dccp_hdr(skb)->dccph_sport }
			   	     }
			  };

	if (ip_route_output_flow(&rt, &fl, sk, 0)) {
		IP_INC_STATS_BH(IPSTATS_MIB_OUTNOROUTES);
		return NULL;
	}

	return &rt->u.dst;
}

static void dccp_v4_ctl_send_reset(struct sk_buff *rxskb)
{
	int err;
	struct dccp_hdr *rxdh = dccp_hdr(rxskb), *dh;
	const int dccp_hdr_reset_len = sizeof(struct dccp_hdr) +
				       sizeof(struct dccp_hdr_ext) +
				       sizeof(struct dccp_hdr_reset);
	struct sk_buff *skb;
	struct dst_entry *dst;
	u64 seqno;

	/* Never send a reset in response to a reset. */
	if (rxdh->dccph_type == DCCP_PKT_RESET)
		return;

	if (((struct rtable *)rxskb->dst)->rt_type != RTN_LOCAL)
		return;

	dst = dccp_v4_route_skb(dccp_ctl_socket->sk, rxskb);
	if (dst == NULL)
		return;

	skb = alloc_skb(MAX_DCCP_HEADER + 15, GFP_ATOMIC);
	if (skb == NULL)
		goto out;

	/* Reserve space for headers. */
	skb_reserve(skb, MAX_DCCP_HEADER);
	skb->dst = dst_clone(dst);

	skb->h.raw = skb_push(skb, dccp_hdr_reset_len);
	dh = dccp_hdr(skb);
	memset(dh, 0, dccp_hdr_reset_len);

	/* Build DCCP header and checksum it. */
	dh->dccph_type	   = DCCP_PKT_RESET;
	dh->dccph_sport	   = rxdh->dccph_dport;
	dh->dccph_dport	   = rxdh->dccph_sport;
	dh->dccph_doff	   = dccp_hdr_reset_len / 4;
	dh->dccph_x	   = 1;
	dccp_hdr_reset(skb)->dccph_reset_code =
				DCCP_SKB_CB(rxskb)->dccpd_reset_code;

	/* See "8.3.1. Abnormal Termination" in draft-ietf-dccp-spec-11 */
	seqno = 0;
	if (DCCP_SKB_CB(rxskb)->dccpd_ack_seq != DCCP_PKT_WITHOUT_ACK_SEQ)
		dccp_set_seqno(&seqno, DCCP_SKB_CB(rxskb)->dccpd_ack_seq + 1);

	dccp_hdr_set_seq(dh, seqno);
	dccp_hdr_set_ack(dccp_hdr_ack_bits(skb),
			 DCCP_SKB_CB(rxskb)->dccpd_seq);

	dh->dccph_checksum = dccp_v4_checksum(skb, rxskb->nh.iph->saddr,
					      rxskb->nh.iph->daddr);

	bh_lock_sock(dccp_ctl_socket->sk);
	err = ip_build_and_send_pkt(skb, dccp_ctl_socket->sk,
				    rxskb->nh.iph->daddr,
				    rxskb->nh.iph->saddr, NULL);
	bh_unlock_sock(dccp_ctl_socket->sk);

	if (err == NET_XMIT_CN || err == 0) {
		DCCP_INC_STATS_BH(DCCP_MIB_OUTSEGS);
		DCCP_INC_STATS_BH(DCCP_MIB_OUTRSTS);
	}
out:
	 dst_release(dst);
}

int dccp_v4_do_rcv(struct sock *sk, struct sk_buff *skb)
{
	struct dccp_hdr *dh = dccp_hdr(skb);

	if (sk->sk_state == DCCP_OPEN) { /* Fast path */
		if (dccp_rcv_established(sk, skb, dh, skb->len))
			goto reset;
		return 0;
	}

	/*
	 *  Step 3: Process LISTEN state
	 *     If S.state == LISTEN,
	 *	  If P.type == Request or P contains a valid Init Cookie
	 *	  	option,
	 *	     * Must scan the packet's options to check for an Init
	 *		Cookie.  Only the Init Cookie is processed here,
	 *		however; other options are processed in Step 8.  This
	 *		scan need only be performed if the endpoint uses Init
	 *		Cookies *
	 *	     * Generate a new socket and switch to that socket *
	 *	     Set S := new socket for this port pair
	 *	     S.state = RESPOND
	 *	     Choose S.ISS (initial seqno) or set from Init Cookie
	 *	     Set S.ISR, S.GSR, S.SWL, S.SWH from packet or Init Cookie
	 *	     Continue with S.state == RESPOND
	 *	     * A Response packet will be generated in Step 11 *
	 *	  Otherwise,
	 *	     Generate Reset(No Connection) unless P.type == Reset
	 *	     Drop packet and return
	 *
	 * NOTE: the check for the packet types is done in
	 *	 dccp_rcv_state_process
	 */
	if (sk->sk_state == DCCP_LISTEN) {
		struct sock *nsk = dccp_v4_hnd_req(sk, skb);

		if (nsk == NULL)
			goto discard;

		if (nsk != sk) {
			if (dccp_child_process(sk, nsk, skb))
				goto reset;
			return 0;
		}
	}

	if (dccp_rcv_state_process(sk, skb, dh, skb->len))
		goto reset;
	return 0;

reset:
	dccp_v4_ctl_send_reset(skb);
discard:
	kfree_skb(skb);
	return 0;
}

static inline int dccp_invalid_packet(struct sk_buff *skb)
{
	const struct dccp_hdr *dh;

	if (skb->pkt_type != PACKET_HOST)
		return 1;

	if (!pskb_may_pull(skb, sizeof(struct dccp_hdr))) {
		LIMIT_NETDEBUG(KERN_WARNING "DCCP: pskb_may_pull failed\n");
		return 1;
	}

	dh = dccp_hdr(skb);

	/* If the packet type is not understood, drop packet and return */
	if (dh->dccph_type >= DCCP_PKT_INVALID) {
		LIMIT_NETDEBUG(KERN_WARNING "DCCP: invalid packet type\n");
		return 1;
	}

	/*
	 * If P.Data Offset is too small for packet type, or too large for
	 * packet, drop packet and return
	 */
	if (dh->dccph_doff < dccp_hdr_len(skb) / sizeof(u32)) {
		LIMIT_NETDEBUG(KERN_WARNING "DCCP: P.Data Offset(%u) "
					    "too small 1\n",
			       dh->dccph_doff);
		return 1;
	}

	if (!pskb_may_pull(skb, dh->dccph_doff * sizeof(u32))) {
		LIMIT_NETDEBUG(KERN_WARNING "DCCP: P.Data Offset(%u) "
					    "too small 2\n",
			       dh->dccph_doff);
		return 1;
	}

	dh = dccp_hdr(skb);

	/*
	 * If P.type is not Data, Ack, or DataAck and P.X == 0 (the packet
	 * has short sequence numbers), drop packet and return
	 */
	if (dh->dccph_x == 0 &&
	    dh->dccph_type != DCCP_PKT_DATA &&
	    dh->dccph_type != DCCP_PKT_ACK &&
	    dh->dccph_type != DCCP_PKT_DATAACK) {
		LIMIT_NETDEBUG(KERN_WARNING "DCCP: P.type (%s) not Data, Ack "
					    "nor DataAck and P.X == 0\n",
			       dccp_packet_name(dh->dccph_type));
		return 1;
	}

	/* If the header checksum is incorrect, drop packet and return */
	if (dccp_v4_verify_checksum(skb, skb->nh.iph->saddr,
				    skb->nh.iph->daddr) < 0) {
		LIMIT_NETDEBUG(KERN_WARNING "DCCP: header checksum is "
					    "incorrect\n");
		return 1;
	}

	return 0;
}

/* this is called when real data arrives */
int dccp_v4_rcv(struct sk_buff *skb)
{
	const struct dccp_hdr *dh;
	struct sock *sk;
	int rc;

	/* Step 1: Check header basics: */

	if (dccp_invalid_packet(skb))
		goto discard_it;

	dh = dccp_hdr(skb);

	DCCP_SKB_CB(skb)->dccpd_seq  = dccp_hdr_seq(skb);
	DCCP_SKB_CB(skb)->dccpd_type = dh->dccph_type;

	dccp_pr_debug("%8.8s "
		      "src=%u.%u.%u.%u@%-5d "
		      "dst=%u.%u.%u.%u@%-5d seq=%llu",
		      dccp_packet_name(dh->dccph_type),
		      NIPQUAD(skb->nh.iph->saddr), ntohs(dh->dccph_sport),
		      NIPQUAD(skb->nh.iph->daddr), ntohs(dh->dccph_dport),
		      (unsigned long long) DCCP_SKB_CB(skb)->dccpd_seq);

	if (dccp_packet_without_ack(skb)) {
		DCCP_SKB_CB(skb)->dccpd_ack_seq = DCCP_PKT_WITHOUT_ACK_SEQ;
		dccp_pr_debug_cat("\n");
	} else {
		DCCP_SKB_CB(skb)->dccpd_ack_seq = dccp_hdr_ack_seq(skb);
		dccp_pr_debug_cat(", ack=%llu\n",
				  (unsigned long long)
				  DCCP_SKB_CB(skb)->dccpd_ack_seq);
	}

	/* Step 2:
	 * 	Look up flow ID in table and get corresponding socket */
	sk = __inet_lookup(&dccp_hashinfo,
			   skb->nh.iph->saddr, dh->dccph_sport,
			   skb->nh.iph->daddr, ntohs(dh->dccph_dport),
			   inet_iif(skb));

	/* 
	 * Step 2:
	 * 	If no socket ...
	 *		Generate Reset(No Connection) unless P.type == Reset
	 *		Drop packet and return
	 */
	if (sk == NULL) {
		dccp_pr_debug("failed to look up flow ID in table and "
			      "get corresponding socket\n");
		goto no_dccp_socket;
	}

	/* 
	 * Step 2:
	 * 	... or S.state == TIMEWAIT,
	 *		Generate Reset(No Connection) unless P.type == Reset
	 *		Drop packet and return
	 */
	       
	if (sk->sk_state == DCCP_TIME_WAIT) {
		dccp_pr_debug("sk->sk_state == DCCP_TIME_WAIT: "
			      "do_time_wait\n");
                goto do_time_wait;
	}

	if (!xfrm4_policy_check(sk, XFRM_POLICY_IN, skb)) {
		dccp_pr_debug("xfrm4_policy_check failed\n");
		goto discard_and_relse;
	}

        if (sk_filter(sk, skb, 0)) {
		dccp_pr_debug("sk_filter failed\n");
                goto discard_and_relse;
	}

	skb->dev = NULL;

	bh_lock_sock(sk);
	rc = 0;
	if (!sock_owned_by_user(sk))
		rc = dccp_v4_do_rcv(sk, skb);
	else
		sk_add_backlog(sk, skb);
	bh_unlock_sock(sk);

	sock_put(sk);
	return rc;

no_dccp_socket:
	if (!xfrm4_policy_check(NULL, XFRM_POLICY_IN, skb))
		goto discard_it;
	/*
	 * Step 2:
	 *		Generate Reset(No Connection) unless P.type == Reset
	 *		Drop packet and return
	 */
	if (dh->dccph_type != DCCP_PKT_RESET) {
		DCCP_SKB_CB(skb)->dccpd_reset_code =
					DCCP_RESET_CODE_NO_CONNECTION;
		dccp_v4_ctl_send_reset(skb);
	}

discard_it:
	/* Discard frame. */
	kfree_skb(skb);
	return 0;

discard_and_relse:
	sock_put(sk);
	goto discard_it;

do_time_wait:
	inet_twsk_put((struct inet_timewait_sock *)sk);
	goto no_dccp_socket;
}

static int dccp_v4_init_sock(struct sock *sk)
{
	struct dccp_sock *dp = dccp_sk(sk);
	static int dccp_ctl_socket_init = 1;

	dccp_options_init(&dp->dccps_options);
	do_gettimeofday(&dp->dccps_epoch);

	if (dp->dccps_options.dccpo_send_ack_vector) {
		dp->dccps_hc_rx_ackvec = dccp_ackvec_alloc(DCCP_MAX_ACKVEC_LEN,
							   GFP_KERNEL);
		if (dp->dccps_hc_rx_ackvec == NULL)
			return -ENOMEM;
	}

	/*
	 * FIXME: We're hardcoding the CCID, and doing this at this point makes
	 * the listening (master) sock get CCID control blocks, which is not
	 * necessary, but for now, to not mess with the test userspace apps,
	 * lets leave it here, later the real solution is to do this in a
	 * setsockopt(CCIDs-I-want/accept). -acme
	 */
	if (likely(!dccp_ctl_socket_init)) {
		dp->dccps_hc_rx_ccid = ccid_init(dp->dccps_options.dccpo_rx_ccid,
						 sk);
		dp->dccps_hc_tx_ccid = ccid_init(dp->dccps_options.dccpo_tx_ccid,
						 sk);
	    	if (dp->dccps_hc_rx_ccid == NULL ||
		    dp->dccps_hc_tx_ccid == NULL) {
			ccid_exit(dp->dccps_hc_rx_ccid, sk);
			ccid_exit(dp->dccps_hc_tx_ccid, sk);
			if (dp->dccps_options.dccpo_send_ack_vector) {
				dccp_ackvec_free(dp->dccps_hc_rx_ackvec);
				dp->dccps_hc_rx_ackvec = NULL;
			}
			dp->dccps_hc_rx_ccid = dp->dccps_hc_tx_ccid = NULL;
			return -ENOMEM;
		}
	} else
		dccp_ctl_socket_init = 0;

	dccp_init_xmit_timers(sk);
	inet_csk(sk)->icsk_rto = DCCP_TIMEOUT_INIT;
	sk->sk_state = DCCP_CLOSED;
	sk->sk_write_space = dccp_write_space;
	dp->dccps_mss_cache = 536;
	dp->dccps_role = DCCP_ROLE_UNDEFINED;
	dp->dccps_service = DCCP_SERVICE_INVALID_VALUE;

	return 0;
}

static int dccp_v4_destroy_sock(struct sock *sk)
{
	struct dccp_sock *dp = dccp_sk(sk);

	/*
	 * DCCP doesn't use sk_qrite_queue, just sk_send_head
	 * for retransmissions
	 */
	if (sk->sk_send_head != NULL) {
		kfree_skb(sk->sk_send_head);
		sk->sk_send_head = NULL;
	}

	/* Clean up a referenced DCCP bind bucket. */
	if (inet_csk(sk)->icsk_bind_hash != NULL)
		inet_put_port(&dccp_hashinfo, sk);

	if (dp->dccps_service_list != NULL) {
		kfree(dp->dccps_service_list);
		dp->dccps_service_list = NULL;
	}

	ccid_hc_rx_exit(dp->dccps_hc_rx_ccid, sk);
	ccid_hc_tx_exit(dp->dccps_hc_tx_ccid, sk);
	if (dp->dccps_options.dccpo_send_ack_vector) {
		dccp_ackvec_free(dp->dccps_hc_rx_ackvec);
		dp->dccps_hc_rx_ackvec = NULL;
	}
	ccid_exit(dp->dccps_hc_rx_ccid, sk);
	ccid_exit(dp->dccps_hc_tx_ccid, sk);
	dp->dccps_hc_rx_ccid = dp->dccps_hc_tx_ccid = NULL;

	return 0;
}

static void dccp_v4_reqsk_destructor(struct request_sock *req)
{
	kfree(inet_rsk(req)->opt);
}

static struct request_sock_ops dccp_request_sock_ops = {
	.family		= PF_INET,
	.obj_size	= sizeof(struct dccp_request_sock),
	.rtx_syn_ack	= dccp_v4_send_response,
	.send_ack	= dccp_v4_reqsk_send_ack,
	.destructor	= dccp_v4_reqsk_destructor,
	.send_reset	= dccp_v4_ctl_send_reset,
};

struct proto dccp_v4_prot = {
	.name			= "DCCP",
	.owner			= THIS_MODULE,
	.close			= dccp_close,
	.connect		= dccp_v4_connect,
	.disconnect		= dccp_disconnect,
	.ioctl			= dccp_ioctl,
	.init			= dccp_v4_init_sock,
	.setsockopt		= dccp_setsockopt,
	.getsockopt		= dccp_getsockopt,
	.sendmsg		= dccp_sendmsg,
	.recvmsg		= dccp_recvmsg,
	.backlog_rcv		= dccp_v4_do_rcv,
	.hash			= dccp_v4_hash,
	.unhash			= dccp_v4_unhash,
	.accept			= inet_csk_accept,
	.get_port		= dccp_v4_get_port,
	.shutdown		= dccp_shutdown,
	.destroy		= dccp_v4_destroy_sock,
	.orphan_count		= &dccp_orphan_count,
	.max_header		= MAX_DCCP_HEADER,
	.obj_size		= sizeof(struct dccp_sock),
	.rsk_prot		= &dccp_request_sock_ops,
	.twsk_obj_size		= sizeof(struct inet_timewait_sock),
};
