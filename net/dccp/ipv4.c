// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  net/dccp/ipv4.c
 *
 *  An implementation of the DCCP protocol
 *  Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 */

#include <linux/dccp.h>
#include <linux/icmp.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/random.h>

#include <net/icmp.h>
#include <net/inet_common.h>
#include <net/inet_hashtables.h>
#include <net/inet_sock.h>
#include <net/protocol.h>
#include <net/sock.h>
#include <net/timewait_sock.h>
#include <net/tcp_states.h>
#include <net/xfrm.h>
#include <net/secure_seq.h>
#include <net/netns/generic.h>

#include "ackvec.h"
#include "ccid.h"
#include "dccp.h"
#include "feat.h"

struct dccp_v4_pernet {
	struct sock *v4_ctl_sk;
};

static unsigned int dccp_v4_pernet_id __read_mostly;

/*
 * The per-net v4_ctl_sk socket is used for responding to
 * the Out-of-the-blue (OOTB) packets. A control sock will be created
 * for this socket at the initialization time.
 */

int dccp_v4_connect(struct sock *sk, struct sockaddr *uaddr, int addr_len)
{
	const struct sockaddr_in *usin = (struct sockaddr_in *)uaddr;
	struct inet_sock *inet = inet_sk(sk);
	struct dccp_sock *dp = dccp_sk(sk);
	__be16 orig_sport, orig_dport;
	__be32 daddr, nexthop;
	struct flowi4 *fl4;
	struct rtable *rt;
	int err;
	struct ip_options_rcu *inet_opt;

	dp->dccps_role = DCCP_ROLE_CLIENT;

	if (addr_len < sizeof(struct sockaddr_in))
		return -EINVAL;

	if (usin->sin_family != AF_INET)
		return -EAFNOSUPPORT;

	nexthop = daddr = usin->sin_addr.s_addr;

	inet_opt = rcu_dereference_protected(inet->inet_opt,
					     lockdep_sock_is_held(sk));
	if (inet_opt != NULL && inet_opt->opt.srr) {
		if (daddr == 0)
			return -EINVAL;
		nexthop = inet_opt->opt.faddr;
	}

	orig_sport = inet->inet_sport;
	orig_dport = usin->sin_port;
	fl4 = &inet->cork.fl.u.ip4;
	rt = ip_route_connect(fl4, nexthop, inet->inet_saddr,
			      sk->sk_bound_dev_if, IPPROTO_DCCP, orig_sport,
			      orig_dport, sk);
	if (IS_ERR(rt))
		return PTR_ERR(rt);

	if (rt->rt_flags & (RTCF_MULTICAST | RTCF_BROADCAST)) {
		ip_rt_put(rt);
		return -ENETUNREACH;
	}

	if (inet_opt == NULL || !inet_opt->opt.srr)
		daddr = fl4->daddr;

	if (inet->inet_saddr == 0) {
		err = inet_bhash2_update_saddr(sk,  &fl4->saddr, AF_INET);
		if (err) {
			ip_rt_put(rt);
			return err;
		}
	} else {
		sk_rcv_saddr_set(sk, inet->inet_saddr);
	}

	inet->inet_dport = usin->sin_port;
	sk_daddr_set(sk, daddr);

	inet_csk(sk)->icsk_ext_hdr_len = 0;
	if (inet_opt)
		inet_csk(sk)->icsk_ext_hdr_len = inet_opt->opt.optlen;
	/*
	 * Socket identity is still unknown (sport may be zero).
	 * However we set state to DCCP_REQUESTING and not releasing socket
	 * lock select source port, enter ourselves into the hash tables and
	 * complete initialization after this.
	 */
	dccp_set_state(sk, DCCP_REQUESTING);
	err = inet_hash_connect(&dccp_death_row, sk);
	if (err != 0)
		goto failure;

	rt = ip_route_newports(fl4, rt, orig_sport, orig_dport,
			       inet->inet_sport, inet->inet_dport, sk);
	if (IS_ERR(rt)) {
		err = PTR_ERR(rt);
		rt = NULL;
		goto failure;
	}
	/* OK, now commit destination to socket.  */
	sk_setup_caps(sk, &rt->dst);

	dp->dccps_iss = secure_dccp_sequence_number(inet->inet_saddr,
						    inet->inet_daddr,
						    inet->inet_sport,
						    inet->inet_dport);
	atomic_set(&inet->inet_id, get_random_u16());

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
	inet_bhash2_reset_saddr(sk);
	ip_rt_put(rt);
	sk->sk_route_caps = 0;
	inet->inet_dport = 0;
	goto out;
}
EXPORT_SYMBOL_GPL(dccp_v4_connect);

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

	dst = inet_csk_update_pmtu(sk, mtu);
	if (!dst)
		return;

	/* Something is about to be wrong... Remember soft error
	 * for the case, if this connection will not able to recover.
	 */
	if (mtu < dst_mtu(dst) && ip_dont_fragment(sk, dst))
		WRITE_ONCE(sk->sk_err_soft, EMSGSIZE);

	mtu = dst_mtu(dst);

	if (inet->pmtudisc != IP_PMTUDISC_DONT &&
	    ip_sk_accept_pmtu(sk) &&
	    inet_csk(sk)->icsk_pmtu_cookie > mtu) {
		dccp_sync_mss(sk, mtu);

		/*
		 * From RFC 4340, sec. 14.1:
		 *
		 *	DCCP-Sync packets are the best choice for upward
		 *	probing, since DCCP-Sync probes do not risk application
		 *	data loss.
		 */
		dccp_send_sync(sk, dp->dccps_gsr, DCCP_PKT_SYNC);
	} /* else let the usual retransmit timer handle it */
}

static void dccp_do_redirect(struct sk_buff *skb, struct sock *sk)
{
	struct dst_entry *dst = __sk_dst_check(sk, 0);

	if (dst)
		dst->ops->redirect(dst, sk, skb);
}

void dccp_req_err(struct sock *sk, u64 seq)
	{
	struct request_sock *req = inet_reqsk(sk);
	struct net *net = sock_net(sk);

	/*
	 * ICMPs are not backlogged, hence we cannot get an established
	 * socket here.
	 */
	if (!between48(seq, dccp_rsk(req)->dreq_iss, dccp_rsk(req)->dreq_gss)) {
		__NET_INC_STATS(net, LINUX_MIB_OUTOFWINDOWICMPS);
	} else {
		/*
		 * Still in RESPOND, just remove it silently.
		 * There is no good way to pass the error to the newly
		 * created socket, and POSIX does not want network
		 * errors returned from accept().
		 */
		inet_csk_reqsk_queue_drop(req->rsk_listener, req);
	}
	reqsk_put(req);
}
EXPORT_SYMBOL(dccp_req_err);

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
static int dccp_v4_err(struct sk_buff *skb, u32 info)
{
	const struct iphdr *iph = (struct iphdr *)skb->data;
	const u8 offset = iph->ihl << 2;
	const struct dccp_hdr *dh;
	struct dccp_sock *dp;
	const int type = icmp_hdr(skb)->type;
	const int code = icmp_hdr(skb)->code;
	struct sock *sk;
	__u64 seq;
	int err;
	struct net *net = dev_net(skb->dev);

	if (!pskb_may_pull(skb, offset + sizeof(*dh)))
		return -EINVAL;
	dh = (struct dccp_hdr *)(skb->data + offset);
	if (!pskb_may_pull(skb, offset + __dccp_basic_hdr_len(dh)))
		return -EINVAL;
	iph = (struct iphdr *)skb->data;
	dh = (struct dccp_hdr *)(skb->data + offset);

	sk = __inet_lookup_established(net, &dccp_hashinfo,
				       iph->daddr, dh->dccph_dport,
				       iph->saddr, ntohs(dh->dccph_sport),
				       inet_iif(skb), 0);
	if (!sk) {
		__ICMP_INC_STATS(net, ICMP_MIB_INERRORS);
		return -ENOENT;
	}

	if (sk->sk_state == DCCP_TIME_WAIT) {
		inet_twsk_put(inet_twsk(sk));
		return 0;
	}
	seq = dccp_hdr_seq(dh);
	if (sk->sk_state == DCCP_NEW_SYN_RECV) {
		dccp_req_err(sk, seq);
		return 0;
	}

	bh_lock_sock(sk);
	/* If too many ICMPs get dropped on busy
	 * servers this needs to be solved differently.
	 */
	if (sock_owned_by_user(sk))
		__NET_INC_STATS(net, LINUX_MIB_LOCKDROPPEDICMPS);

	if (sk->sk_state == DCCP_CLOSED)
		goto out;

	dp = dccp_sk(sk);
	if ((1 << sk->sk_state) & ~(DCCPF_REQUESTING | DCCPF_LISTEN) &&
	    !between48(seq, dp->dccps_awl, dp->dccps_awh)) {
		__NET_INC_STATS(net, LINUX_MIB_OUTOFWINDOWICMPS);
		goto out;
	}

	switch (type) {
	case ICMP_REDIRECT:
		if (!sock_owned_by_user(sk))
			dccp_do_redirect(skb, sk);
		goto out;
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
	case DCCP_REQUESTING:
	case DCCP_RESPOND:
		if (!sock_owned_by_user(sk)) {
			__DCCP_INC_STATS(DCCP_MIB_ATTEMPTFAILS);
			sk->sk_err = err;

			sk_error_report(sk);

			dccp_done(sk);
		} else {
			WRITE_ONCE(sk->sk_err_soft, err);
		}
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

	if (!sock_owned_by_user(sk) && inet_test_bit(RECVERR, sk)) {
		sk->sk_err = err;
		sk_error_report(sk);
	} else { /* Only an error on timeout */
		WRITE_ONCE(sk->sk_err_soft, err);
	}
out:
	bh_unlock_sock(sk);
	sock_put(sk);
	return 0;
}

static inline __sum16 dccp_v4_csum_finish(struct sk_buff *skb,
				      __be32 src, __be32 dst)
{
	return csum_tcpudp_magic(src, dst, skb->len, IPPROTO_DCCP, skb->csum);
}

void dccp_v4_send_check(struct sock *sk, struct sk_buff *skb)
{
	const struct inet_sock *inet = inet_sk(sk);
	struct dccp_hdr *dh = dccp_hdr(skb);

	dccp_csum_outgoing(skb);
	dh->dccph_checksum = dccp_v4_csum_finish(skb,
						 inet->inet_saddr,
						 inet->inet_daddr);
}
EXPORT_SYMBOL_GPL(dccp_v4_send_check);

static inline u64 dccp_v4_init_sequence(const struct sk_buff *skb)
{
	return secure_dccp_sequence_number(ip_hdr(skb)->daddr,
					   ip_hdr(skb)->saddr,
					   dccp_hdr(skb)->dccph_dport,
					   dccp_hdr(skb)->dccph_sport);
}

/*
 * The three way handshake has completed - we got a valid ACK or DATAACK -
 * now create the new socket.
 *
 * This is the equivalent of TCP's tcp_v4_syn_recv_sock
 */
struct sock *dccp_v4_request_recv_sock(const struct sock *sk,
				       struct sk_buff *skb,
				       struct request_sock *req,
				       struct dst_entry *dst,
				       struct request_sock *req_unhash,
				       bool *own_req)
{
	struct inet_request_sock *ireq;
	struct inet_sock *newinet;
	struct sock *newsk;

	if (sk_acceptq_is_full(sk))
		goto exit_overflow;

	newsk = dccp_create_openreq_child(sk, req, skb);
	if (newsk == NULL)
		goto exit_nonewsk;

	newinet		   = inet_sk(newsk);
	ireq		   = inet_rsk(req);
	sk_daddr_set(newsk, ireq->ir_rmt_addr);
	sk_rcv_saddr_set(newsk, ireq->ir_loc_addr);
	newinet->inet_saddr	= ireq->ir_loc_addr;
	RCU_INIT_POINTER(newinet->inet_opt, rcu_dereference(ireq->ireq_opt));
	newinet->mc_index  = inet_iif(skb);
	newinet->mc_ttl	   = ip_hdr(skb)->ttl;
	atomic_set(&newinet->inet_id, get_random_u16());

	if (dst == NULL && (dst = inet_csk_route_child_sock(sk, newsk, req)) == NULL)
		goto put_and_exit;

	sk_setup_caps(newsk, dst);

	dccp_sync_mss(newsk, dst_mtu(dst));

	if (__inet_inherit_port(sk, newsk) < 0)
		goto put_and_exit;
	*own_req = inet_ehash_nolisten(newsk, req_to_sk(req_unhash), NULL);
	if (*own_req)
		ireq->ireq_opt = NULL;
	else
		newinet->inet_opt = NULL;
	return newsk;

exit_overflow:
	__NET_INC_STATS(sock_net(sk), LINUX_MIB_LISTENOVERFLOWS);
exit_nonewsk:
	dst_release(dst);
exit:
	__NET_INC_STATS(sock_net(sk), LINUX_MIB_LISTENDROPS);
	return NULL;
put_and_exit:
	newinet->inet_opt = NULL;
	inet_csk_prepare_forced_close(newsk);
	dccp_done(newsk);
	goto exit;
}
EXPORT_SYMBOL_GPL(dccp_v4_request_recv_sock);

static struct dst_entry* dccp_v4_route_skb(struct net *net, struct sock *sk,
					   struct sk_buff *skb)
{
	struct rtable *rt;
	const struct iphdr *iph = ip_hdr(skb);
	struct flowi4 fl4 = {
		.flowi4_oif = inet_iif(skb),
		.daddr = iph->saddr,
		.saddr = iph->daddr,
		.flowi4_tos = ip_sock_rt_tos(sk),
		.flowi4_scope = ip_sock_rt_scope(sk),
		.flowi4_proto = sk->sk_protocol,
		.fl4_sport = dccp_hdr(skb)->dccph_dport,
		.fl4_dport = dccp_hdr(skb)->dccph_sport,
	};

	security_skb_classify_flow(skb, flowi4_to_flowi_common(&fl4));
	rt = ip_route_output_flow(net, &fl4, sk);
	if (IS_ERR(rt)) {
		IP_INC_STATS(net, IPSTATS_MIB_OUTNOROUTES);
		return NULL;
	}

	return &rt->dst;
}

static int dccp_v4_send_response(const struct sock *sk, struct request_sock *req)
{
	int err = -1;
	struct sk_buff *skb;
	struct dst_entry *dst;
	struct flowi4 fl4;

	dst = inet_csk_route_req(sk, &fl4, req);
	if (dst == NULL)
		goto out;

	skb = dccp_make_response(sk, dst, req);
	if (skb != NULL) {
		const struct inet_request_sock *ireq = inet_rsk(req);
		struct dccp_hdr *dh = dccp_hdr(skb);

		dh->dccph_checksum = dccp_v4_csum_finish(skb, ireq->ir_loc_addr,
							      ireq->ir_rmt_addr);
		rcu_read_lock();
		err = ip_build_and_send_pkt(skb, sk, ireq->ir_loc_addr,
					    ireq->ir_rmt_addr,
					    rcu_dereference(ireq->ireq_opt),
					    inet_sk(sk)->tos);
		rcu_read_unlock();
		err = net_xmit_eval(err);
	}

out:
	dst_release(dst);
	return err;
}

static void dccp_v4_ctl_send_reset(const struct sock *sk, struct sk_buff *rxskb)
{
	int err;
	const struct iphdr *rxiph;
	struct sk_buff *skb;
	struct dst_entry *dst;
	struct net *net = dev_net(skb_dst(rxskb)->dev);
	struct dccp_v4_pernet *pn;
	struct sock *ctl_sk;

	/* Never send a reset in response to a reset. */
	if (dccp_hdr(rxskb)->dccph_type == DCCP_PKT_RESET)
		return;

	if (skb_rtable(rxskb)->rt_type != RTN_LOCAL)
		return;

	pn = net_generic(net, dccp_v4_pernet_id);
	ctl_sk = pn->v4_ctl_sk;
	dst = dccp_v4_route_skb(net, ctl_sk, rxskb);
	if (dst == NULL)
		return;

	skb = dccp_ctl_make_reset(ctl_sk, rxskb);
	if (skb == NULL)
		goto out;

	rxiph = ip_hdr(rxskb);
	dccp_hdr(skb)->dccph_checksum = dccp_v4_csum_finish(skb, rxiph->saddr,
								 rxiph->daddr);
	skb_dst_set(skb, dst_clone(dst));

	local_bh_disable();
	bh_lock_sock(ctl_sk);
	err = ip_build_and_send_pkt(skb, ctl_sk,
				    rxiph->daddr, rxiph->saddr, NULL,
				    inet_sk(ctl_sk)->tos);
	bh_unlock_sock(ctl_sk);

	if (net_xmit_eval(err) == 0) {
		__DCCP_INC_STATS(DCCP_MIB_OUTSEGS);
		__DCCP_INC_STATS(DCCP_MIB_OUTRSTS);
	}
	local_bh_enable();
out:
	dst_release(dst);
}

static void dccp_v4_reqsk_destructor(struct request_sock *req)
{
	dccp_feat_list_purge(&dccp_rsk(req)->dreq_featneg);
	kfree(rcu_dereference_protected(inet_rsk(req)->ireq_opt, 1));
}

void dccp_syn_ack_timeout(const struct request_sock *req)
{
}
EXPORT_SYMBOL(dccp_syn_ack_timeout);

static struct request_sock_ops dccp_request_sock_ops __read_mostly = {
	.family		= PF_INET,
	.obj_size	= sizeof(struct dccp_request_sock),
	.rtx_syn_ack	= dccp_v4_send_response,
	.send_ack	= dccp_reqsk_send_ack,
	.destructor	= dccp_v4_reqsk_destructor,
	.send_reset	= dccp_v4_ctl_send_reset,
	.syn_ack_timeout = dccp_syn_ack_timeout,
};

int dccp_v4_conn_request(struct sock *sk, struct sk_buff *skb)
{
	struct inet_request_sock *ireq;
	struct request_sock *req;
	struct dccp_request_sock *dreq;
	const __be32 service = dccp_hdr_request(skb)->dccph_req_service;
	struct dccp_skb_cb *dcb = DCCP_SKB_CB(skb);

	/* Never answer to DCCP_PKT_REQUESTs send to broadcast or multicast */
	if (skb_rtable(skb)->rt_flags & (RTCF_BROADCAST | RTCF_MULTICAST))
		return 0;	/* discard, don't send a reset here */

	if (dccp_bad_service_code(sk, service)) {
		dcb->dccpd_reset_code = DCCP_RESET_CODE_BAD_SERVICE_CODE;
		goto drop;
	}
	/*
	 * TW buckets are converted to open requests without
	 * limitations, they conserve resources and peer is
	 * evidently real one.
	 */
	dcb->dccpd_reset_code = DCCP_RESET_CODE_TOO_BUSY;
	if (inet_csk_reqsk_queue_is_full(sk))
		goto drop;

	if (sk_acceptq_is_full(sk))
		goto drop;

	req = inet_reqsk_alloc(&dccp_request_sock_ops, sk, true);
	if (req == NULL)
		goto drop;

	if (dccp_reqsk_init(req, dccp_sk(sk), skb))
		goto drop_and_free;

	dreq = dccp_rsk(req);
	if (dccp_parse_options(sk, dreq, skb))
		goto drop_and_free;

	if (security_inet_conn_request(sk, skb, req))
		goto drop_and_free;

	ireq = inet_rsk(req);
	sk_rcv_saddr_set(req_to_sk(req), ip_hdr(skb)->daddr);
	sk_daddr_set(req_to_sk(req), ip_hdr(skb)->saddr);
	ireq->ir_mark = inet_request_mark(sk, skb);
	ireq->ireq_family = AF_INET;
	ireq->ir_iif = READ_ONCE(sk->sk_bound_dev_if);

	/*
	 * Step 3: Process LISTEN state
	 *
	 * Set S.ISR, S.GSR, S.SWL, S.SWH from packet or Init Cookie
	 *
	 * Setting S.SWL/S.SWH to is deferred to dccp_create_openreq_child().
	 */
	dreq->dreq_isr	   = dcb->dccpd_seq;
	dreq->dreq_gsr	   = dreq->dreq_isr;
	dreq->dreq_iss	   = dccp_v4_init_sequence(skb);
	dreq->dreq_gss     = dreq->dreq_iss;
	dreq->dreq_service = service;

	if (dccp_v4_send_response(sk, req))
		goto drop_and_free;

	inet_csk_reqsk_queue_hash_add(sk, req, DCCP_TIMEOUT_INIT);
	reqsk_put(req);
	return 0;

drop_and_free:
	reqsk_free(req);
drop:
	__DCCP_INC_STATS(DCCP_MIB_ATTEMPTFAILS);
	return -1;
}
EXPORT_SYMBOL_GPL(dccp_v4_conn_request);

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

	if (dccp_rcv_state_process(sk, skb, dh, skb->len))
		goto reset;
	return 0;

reset:
	dccp_v4_ctl_send_reset(sk, skb);
	kfree_skb(skb);
	return 0;
}
EXPORT_SYMBOL_GPL(dccp_v4_do_rcv);

/**
 *	dccp_invalid_packet  -  check for malformed packets
 *	@skb: Packet to validate
 *
 *	Implements RFC 4340, 8.5:  Step 1: Check header basics
 *	Packets that fail these checks are ignored and do not receive Resets.
 */
int dccp_invalid_packet(struct sk_buff *skb)
{
	const struct dccp_hdr *dh;
	unsigned int cscov;
	u8 dccph_doff;

	if (skb->pkt_type != PACKET_HOST)
		return 1;

	/* If the packet is shorter than 12 bytes, drop packet and return */
	if (!pskb_may_pull(skb, sizeof(struct dccp_hdr))) {
		DCCP_WARN("pskb_may_pull failed\n");
		return 1;
	}

	dh = dccp_hdr(skb);

	/* If P.type is not understood, drop packet and return */
	if (dh->dccph_type >= DCCP_PKT_INVALID) {
		DCCP_WARN("invalid packet type\n");
		return 1;
	}

	/*
	 * If P.Data Offset is too small for packet type, drop packet and return
	 */
	dccph_doff = dh->dccph_doff;
	if (dccph_doff < dccp_hdr_len(skb) / sizeof(u32)) {
		DCCP_WARN("P.Data Offset(%u) too small\n", dccph_doff);
		return 1;
	}
	/*
	 * If P.Data Offset is too large for packet, drop packet and return
	 */
	if (!pskb_may_pull(skb, dccph_doff * sizeof(u32))) {
		DCCP_WARN("P.Data Offset(%u) too large\n", dccph_doff);
		return 1;
	}
	dh = dccp_hdr(skb);
	/*
	 * If P.type is not Data, Ack, or DataAck and P.X == 0 (the packet
	 * has short sequence numbers), drop packet and return
	 */
	if ((dh->dccph_type < DCCP_PKT_DATA    ||
	    dh->dccph_type > DCCP_PKT_DATAACK) && dh->dccph_x == 0)  {
		DCCP_WARN("P.type (%s) not Data || [Data]Ack, while P.X == 0\n",
			  dccp_packet_name(dh->dccph_type));
		return 1;
	}

	/*
	 * If P.CsCov is too large for the packet size, drop packet and return.
	 * This must come _before_ checksumming (not as RFC 4340 suggests).
	 */
	cscov = dccp_csum_coverage(skb);
	if (cscov > skb->len) {
		DCCP_WARN("P.CsCov %u exceeds packet length %d\n",
			  dh->dccph_cscov, skb->len);
		return 1;
	}

	/* If header checksum is incorrect, drop packet and return.
	 * (This step is completed in the AF-dependent functions.) */
	skb->csum = skb_checksum(skb, 0, cscov, 0);

	return 0;
}
EXPORT_SYMBOL_GPL(dccp_invalid_packet);

/* this is called when real data arrives */
static int dccp_v4_rcv(struct sk_buff *skb)
{
	const struct dccp_hdr *dh;
	const struct iphdr *iph;
	bool refcounted;
	struct sock *sk;
	int min_cov;

	/* Step 1: Check header basics */

	if (dccp_invalid_packet(skb))
		goto discard_it;

	iph = ip_hdr(skb);
	/* Step 1: If header checksum is incorrect, drop packet and return */
	if (dccp_v4_csum_finish(skb, iph->saddr, iph->daddr)) {
		DCCP_WARN("dropped packet with invalid checksum\n");
		goto discard_it;
	}

	dh = dccp_hdr(skb);

	DCCP_SKB_CB(skb)->dccpd_seq  = dccp_hdr_seq(dh);
	DCCP_SKB_CB(skb)->dccpd_type = dh->dccph_type;

	dccp_pr_debug("%8.8s src=%pI4@%-5d dst=%pI4@%-5d seq=%llu",
		      dccp_packet_name(dh->dccph_type),
		      &iph->saddr, ntohs(dh->dccph_sport),
		      &iph->daddr, ntohs(dh->dccph_dport),
		      (unsigned long long) DCCP_SKB_CB(skb)->dccpd_seq);

	if (dccp_packet_without_ack(skb)) {
		DCCP_SKB_CB(skb)->dccpd_ack_seq = DCCP_PKT_WITHOUT_ACK_SEQ;
		dccp_pr_debug_cat("\n");
	} else {
		DCCP_SKB_CB(skb)->dccpd_ack_seq = dccp_hdr_ack_seq(skb);
		dccp_pr_debug_cat(", ack=%llu\n", (unsigned long long)
				  DCCP_SKB_CB(skb)->dccpd_ack_seq);
	}

lookup:
	sk = __inet_lookup_skb(&dccp_hashinfo, skb, __dccp_hdr_len(dh),
			       dh->dccph_sport, dh->dccph_dport, 0, &refcounted);
	if (!sk) {
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

	if (sk->sk_state == DCCP_NEW_SYN_RECV) {
		struct request_sock *req = inet_reqsk(sk);
		struct sock *nsk;

		sk = req->rsk_listener;
		if (unlikely(sk->sk_state != DCCP_LISTEN)) {
			inet_csk_reqsk_queue_drop_and_put(sk, req);
			goto lookup;
		}
		sock_hold(sk);
		refcounted = true;
		nsk = dccp_check_req(sk, skb, req);
		if (!nsk) {
			reqsk_put(req);
			goto discard_and_relse;
		}
		if (nsk == sk) {
			reqsk_put(req);
		} else if (dccp_child_process(sk, nsk, skb)) {
			dccp_v4_ctl_send_reset(sk, skb);
			goto discard_and_relse;
		} else {
			sock_put(sk);
			return 0;
		}
	}
	/*
	 * RFC 4340, sec. 9.2.1: Minimum Checksum Coverage
	 *	o if MinCsCov = 0, only packets with CsCov = 0 are accepted
	 *	o if MinCsCov > 0, also accept packets with CsCov >= MinCsCov
	 */
	min_cov = dccp_sk(sk)->dccps_pcrlen;
	if (dh->dccph_cscov && (min_cov == 0 || dh->dccph_cscov < min_cov))  {
		dccp_pr_debug("Packet CsCov %d does not satisfy MinCsCov %d\n",
			      dh->dccph_cscov, min_cov);
		/* FIXME: "Such packets SHOULD be reported using Data Dropped
		 *         options (Section 11.7) with Drop Code 0, Protocol
		 *         Constraints."                                     */
		goto discard_and_relse;
	}

	if (!xfrm4_policy_check(sk, XFRM_POLICY_IN, skb))
		goto discard_and_relse;
	nf_reset_ct(skb);

	return __sk_receive_skb(sk, skb, 1, dh->dccph_doff * 4, refcounted);

no_dccp_socket:
	if (!xfrm4_policy_check(NULL, XFRM_POLICY_IN, skb))
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
		dccp_v4_ctl_send_reset(sk, skb);
	}

discard_it:
	kfree_skb(skb);
	return 0;

discard_and_relse:
	if (refcounted)
		sock_put(sk);
	goto discard_it;
}

static const struct inet_connection_sock_af_ops dccp_ipv4_af_ops = {
	.queue_xmit	   = ip_queue_xmit,
	.send_check	   = dccp_v4_send_check,
	.rebuild_header	   = inet_sk_rebuild_header,
	.conn_request	   = dccp_v4_conn_request,
	.syn_recv_sock	   = dccp_v4_request_recv_sock,
	.net_header_len	   = sizeof(struct iphdr),
	.setsockopt	   = ip_setsockopt,
	.getsockopt	   = ip_getsockopt,
	.addr2sockaddr	   = inet_csk_addr2sockaddr,
	.sockaddr_len	   = sizeof(struct sockaddr_in),
};

static int dccp_v4_init_sock(struct sock *sk)
{
	static __u8 dccp_v4_ctl_sock_initialized;
	int err = dccp_init_sock(sk, dccp_v4_ctl_sock_initialized);

	if (err == 0) {
		if (unlikely(!dccp_v4_ctl_sock_initialized))
			dccp_v4_ctl_sock_initialized = 1;
		inet_csk(sk)->icsk_af_ops = &dccp_ipv4_af_ops;
	}

	return err;
}

static struct timewait_sock_ops dccp_timewait_sock_ops = {
	.twsk_obj_size	= sizeof(struct inet_timewait_sock),
};

static struct proto dccp_v4_prot = {
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
	.hash			= inet_hash,
	.unhash			= inet_unhash,
	.accept			= inet_csk_accept,
	.get_port		= inet_csk_get_port,
	.shutdown		= dccp_shutdown,
	.destroy		= dccp_destroy_sock,
	.orphan_count		= &dccp_orphan_count,
	.max_header		= MAX_DCCP_HEADER,
	.obj_size		= sizeof(struct dccp_sock),
	.slab_flags		= SLAB_TYPESAFE_BY_RCU,
	.rsk_prot		= &dccp_request_sock_ops,
	.twsk_prot		= &dccp_timewait_sock_ops,
	.h.hashinfo		= &dccp_hashinfo,
};

static const struct net_protocol dccp_v4_protocol = {
	.handler	= dccp_v4_rcv,
	.err_handler	= dccp_v4_err,
	.no_policy	= 1,
	.icmp_strict_tag_validation = 1,
};

static const struct proto_ops inet_dccp_ops = {
	.family		   = PF_INET,
	.owner		   = THIS_MODULE,
	.release	   = inet_release,
	.bind		   = inet_bind,
	.connect	   = inet_stream_connect,
	.socketpair	   = sock_no_socketpair,
	.accept		   = inet_accept,
	.getname	   = inet_getname,
	/* FIXME: work on tcp_poll to rename it to inet_csk_poll */
	.poll		   = dccp_poll,
	.ioctl		   = inet_ioctl,
	.gettstamp	   = sock_gettstamp,
	/* FIXME: work on inet_listen to rename it to sock_common_listen */
	.listen		   = inet_dccp_listen,
	.shutdown	   = inet_shutdown,
	.setsockopt	   = sock_common_setsockopt,
	.getsockopt	   = sock_common_getsockopt,
	.sendmsg	   = inet_sendmsg,
	.recvmsg	   = sock_common_recvmsg,
	.mmap		   = sock_no_mmap,
};

static struct inet_protosw dccp_v4_protosw = {
	.type		= SOCK_DCCP,
	.protocol	= IPPROTO_DCCP,
	.prot		= &dccp_v4_prot,
	.ops		= &inet_dccp_ops,
	.flags		= INET_PROTOSW_ICSK,
};

static int __net_init dccp_v4_init_net(struct net *net)
{
	struct dccp_v4_pernet *pn = net_generic(net, dccp_v4_pernet_id);

	if (dccp_hashinfo.bhash == NULL)
		return -ESOCKTNOSUPPORT;

	return inet_ctl_sock_create(&pn->v4_ctl_sk, PF_INET,
				    SOCK_DCCP, IPPROTO_DCCP, net);
}

static void __net_exit dccp_v4_exit_net(struct net *net)
{
	struct dccp_v4_pernet *pn = net_generic(net, dccp_v4_pernet_id);

	inet_ctl_sock_destroy(pn->v4_ctl_sk);
}

static void __net_exit dccp_v4_exit_batch(struct list_head *net_exit_list)
{
	inet_twsk_purge(&dccp_hashinfo, AF_INET);
}

static struct pernet_operations dccp_v4_ops = {
	.init	= dccp_v4_init_net,
	.exit	= dccp_v4_exit_net,
	.exit_batch = dccp_v4_exit_batch,
	.id	= &dccp_v4_pernet_id,
	.size   = sizeof(struct dccp_v4_pernet),
};

static int __init dccp_v4_init(void)
{
	int err = proto_register(&dccp_v4_prot, 1);

	if (err)
		goto out;

	inet_register_protosw(&dccp_v4_protosw);

	err = register_pernet_subsys(&dccp_v4_ops);
	if (err)
		goto out_destroy_ctl_sock;

	err = inet_add_protocol(&dccp_v4_protocol, IPPROTO_DCCP);
	if (err)
		goto out_proto_unregister;

out:
	return err;
out_proto_unregister:
	unregister_pernet_subsys(&dccp_v4_ops);
out_destroy_ctl_sock:
	inet_unregister_protosw(&dccp_v4_protosw);
	proto_unregister(&dccp_v4_prot);
	goto out;
}

static void __exit dccp_v4_exit(void)
{
	inet_del_protocol(&dccp_v4_protocol, IPPROTO_DCCP);
	unregister_pernet_subsys(&dccp_v4_ops);
	inet_unregister_protosw(&dccp_v4_protosw);
	proto_unregister(&dccp_v4_prot);
}

module_init(dccp_v4_init);
module_exit(dccp_v4_exit);

/*
 * __stringify doesn't likes enums, so use SOCK_DCCP (6) and IPPROTO_DCCP (33)
 * values directly, Also cover the case where the protocol is not specified,
 * i.e. net-pf-PF_INET-proto-0-type-SOCK_DCCP
 */
MODULE_ALIAS_NET_PF_PROTO_TYPE(PF_INET, 33, 6);
MODULE_ALIAS_NET_PF_PROTO_TYPE(PF_INET, 0, 6);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Arnaldo Carvalho de Melo <acme@mandriva.com>");
MODULE_DESCRIPTION("DCCP - Datagram Congestion Controlled Protocol");
