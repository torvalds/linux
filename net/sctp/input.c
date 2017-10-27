/* SCTP kernel implementation
 * Copyright (c) 1999-2000 Cisco, Inc.
 * Copyright (c) 1999-2001 Motorola, Inc.
 * Copyright (c) 2001-2003 International Business Machines, Corp.
 * Copyright (c) 2001 Intel Corp.
 * Copyright (c) 2001 Nokia, Inc.
 * Copyright (c) 2001 La Monte H.P. Yarroll
 *
 * This file is part of the SCTP kernel implementation
 *
 * These functions handle all input from the IP layer into SCTP.
 *
 * This SCTP implementation is free software;
 * you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This SCTP implementation is distributed in the hope that it
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied
 *                 ************************
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU CC; see the file COPYING.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * Please send any bug reports or fixes you make to the
 * email address(es):
 *    lksctp developers <linux-sctp@vger.kernel.org>
 *
 * Written or modified by:
 *    La Monte H.P. Yarroll <piggy@acm.org>
 *    Karl Knutson <karl@athena.chicago.il.us>
 *    Xingang Guo <xingang.guo@intel.com>
 *    Jon Grimm <jgrimm@us.ibm.com>
 *    Hui Huang <hui.huang@nokia.com>
 *    Daisy Chang <daisyc@us.ibm.com>
 *    Sridhar Samudrala <sri@us.ibm.com>
 *    Ardelle Fan <ardelle.fan@intel.com>
 */

#include <linux/types.h>
#include <linux/list.h> /* For struct list_head */
#include <linux/socket.h>
#include <linux/ip.h>
#include <linux/time.h> /* For struct timeval */
#include <linux/slab.h>
#include <net/ip.h>
#include <net/icmp.h>
#include <net/snmp.h>
#include <net/sock.h>
#include <net/xfrm.h>
#include <net/sctp/sctp.h>
#include <net/sctp/sm.h>
#include <net/sctp/checksum.h>
#include <net/net_namespace.h>

/* Forward declarations for internal helpers. */
static int sctp_rcv_ootb(struct sk_buff *);
static struct sctp_association *__sctp_rcv_lookup(struct net *net,
				      struct sk_buff *skb,
				      const union sctp_addr *paddr,
				      const union sctp_addr *laddr,
				      struct sctp_transport **transportp);
static struct sctp_endpoint *__sctp_rcv_lookup_endpoint(struct net *net,
						const union sctp_addr *laddr);
static struct sctp_association *__sctp_lookup_association(
					struct net *net,
					const union sctp_addr *local,
					const union sctp_addr *peer,
					struct sctp_transport **pt);

static int sctp_add_backlog(struct sock *sk, struct sk_buff *skb);


/* Calculate the SCTP checksum of an SCTP packet.  */
static inline int sctp_rcv_checksum(struct net *net, struct sk_buff *skb)
{
	struct sctphdr *sh = sctp_hdr(skb);
	__le32 cmp = sh->checksum;
	__le32 val = sctp_compute_cksum(skb, 0);

	if (val != cmp) {
		/* CRC failure, dump it. */
		__SCTP_INC_STATS(net, SCTP_MIB_CHECKSUMERRORS);
		return -1;
	}
	return 0;
}

/*
 * This is the routine which IP calls when receiving an SCTP packet.
 */
int sctp_rcv(struct sk_buff *skb)
{
	struct sock *sk;
	struct sctp_association *asoc;
	struct sctp_endpoint *ep = NULL;
	struct sctp_ep_common *rcvr;
	struct sctp_transport *transport = NULL;
	struct sctp_chunk *chunk;
	union sctp_addr src;
	union sctp_addr dest;
	int family;
	struct sctp_af *af;
	struct net *net = dev_net(skb->dev);

	if (skb->pkt_type != PACKET_HOST)
		goto discard_it;

	__SCTP_INC_STATS(net, SCTP_MIB_INSCTPPACKS);

	/* If packet is too small to contain a single chunk, let's not
	 * waste time on it anymore.
	 */
	if (skb->len < sizeof(struct sctphdr) + sizeof(struct sctp_chunkhdr) +
		       skb_transport_offset(skb))
		goto discard_it;

	/* If the packet is fragmented and we need to do crc checking,
	 * it's better to just linearize it otherwise crc computing
	 * takes longer.
	 */
	if ((!(skb_shinfo(skb)->gso_type & SKB_GSO_SCTP) &&
	     skb_linearize(skb)) ||
	    !pskb_may_pull(skb, sizeof(struct sctphdr)))
		goto discard_it;

	/* Pull up the IP header. */
	__skb_pull(skb, skb_transport_offset(skb));

	skb->csum_valid = 0; /* Previous value not applicable */
	if (skb_csum_unnecessary(skb))
		__skb_decr_checksum_unnecessary(skb);
	else if (!sctp_checksum_disable &&
		 !(skb_shinfo(skb)->gso_type & SKB_GSO_SCTP) &&
		 sctp_rcv_checksum(net, skb) < 0)
		goto discard_it;
	skb->csum_valid = 1;

	__skb_pull(skb, sizeof(struct sctphdr));

	family = ipver2af(ip_hdr(skb)->version);
	af = sctp_get_af_specific(family);
	if (unlikely(!af))
		goto discard_it;
	SCTP_INPUT_CB(skb)->af = af;

	/* Initialize local addresses for lookups. */
	af->from_skb(&src, skb, 1);
	af->from_skb(&dest, skb, 0);

	/* If the packet is to or from a non-unicast address,
	 * silently discard the packet.
	 *
	 * This is not clearly defined in the RFC except in section
	 * 8.4 - OOTB handling.  However, based on the book "Stream Control
	 * Transmission Protocol" 2.1, "It is important to note that the
	 * IP address of an SCTP transport address must be a routable
	 * unicast address.  In other words, IP multicast addresses and
	 * IP broadcast addresses cannot be used in an SCTP transport
	 * address."
	 */
	if (!af->addr_valid(&src, NULL, skb) ||
	    !af->addr_valid(&dest, NULL, skb))
		goto discard_it;

	asoc = __sctp_rcv_lookup(net, skb, &src, &dest, &transport);

	if (!asoc)
		ep = __sctp_rcv_lookup_endpoint(net, &dest);

	/* Retrieve the common input handling substructure. */
	rcvr = asoc ? &asoc->base : &ep->base;
	sk = rcvr->sk;

	/*
	 * If a frame arrives on an interface and the receiving socket is
	 * bound to another interface, via SO_BINDTODEVICE, treat it as OOTB
	 */
	if (sk->sk_bound_dev_if && (sk->sk_bound_dev_if != af->skb_iif(skb))) {
		if (transport) {
			sctp_transport_put(transport);
			asoc = NULL;
			transport = NULL;
		} else {
			sctp_endpoint_put(ep);
			ep = NULL;
		}
		sk = net->sctp.ctl_sock;
		ep = sctp_sk(sk)->ep;
		sctp_endpoint_hold(ep);
		rcvr = &ep->base;
	}

	/*
	 * RFC 2960, 8.4 - Handle "Out of the blue" Packets.
	 * An SCTP packet is called an "out of the blue" (OOTB)
	 * packet if it is correctly formed, i.e., passed the
	 * receiver's checksum check, but the receiver is not
	 * able to identify the association to which this
	 * packet belongs.
	 */
	if (!asoc) {
		if (sctp_rcv_ootb(skb)) {
			__SCTP_INC_STATS(net, SCTP_MIB_OUTOFBLUES);
			goto discard_release;
		}
	}

	if (!xfrm_policy_check(sk, XFRM_POLICY_IN, skb, family))
		goto discard_release;
	nf_reset(skb);

	if (sk_filter(sk, skb))
		goto discard_release;

	/* Create an SCTP packet structure. */
	chunk = sctp_chunkify(skb, asoc, sk, GFP_ATOMIC);
	if (!chunk)
		goto discard_release;
	SCTP_INPUT_CB(skb)->chunk = chunk;

	/* Remember what endpoint is to handle this packet. */
	chunk->rcvr = rcvr;

	/* Remember the SCTP header. */
	chunk->sctp_hdr = sctp_hdr(skb);

	/* Set the source and destination addresses of the incoming chunk.  */
	sctp_init_addrs(chunk, &src, &dest);

	/* Remember where we came from.  */
	chunk->transport = transport;

	/* Acquire access to the sock lock. Note: We are safe from other
	 * bottom halves on this lock, but a user may be in the lock too,
	 * so check if it is busy.
	 */
	bh_lock_sock(sk);

	if (sk != rcvr->sk) {
		/* Our cached sk is different from the rcvr->sk.  This is
		 * because migrate()/accept() may have moved the association
		 * to a new socket and released all the sockets.  So now we
		 * are holding a lock on the old socket while the user may
		 * be doing something with the new socket.  Switch our veiw
		 * of the current sk.
		 */
		bh_unlock_sock(sk);
		sk = rcvr->sk;
		bh_lock_sock(sk);
	}

	if (sock_owned_by_user(sk)) {
		if (sctp_add_backlog(sk, skb)) {
			bh_unlock_sock(sk);
			sctp_chunk_free(chunk);
			skb = NULL; /* sctp_chunk_free already freed the skb */
			goto discard_release;
		}
		__SCTP_INC_STATS(net, SCTP_MIB_IN_PKT_BACKLOG);
	} else {
		__SCTP_INC_STATS(net, SCTP_MIB_IN_PKT_SOFTIRQ);
		sctp_inq_push(&chunk->rcvr->inqueue, chunk);
	}

	bh_unlock_sock(sk);

	/* Release the asoc/ep ref we took in the lookup calls. */
	if (transport)
		sctp_transport_put(transport);
	else
		sctp_endpoint_put(ep);

	return 0;

discard_it:
	__SCTP_INC_STATS(net, SCTP_MIB_IN_PKT_DISCARDS);
	kfree_skb(skb);
	return 0;

discard_release:
	/* Release the asoc/ep ref we took in the lookup calls. */
	if (transport)
		sctp_transport_put(transport);
	else
		sctp_endpoint_put(ep);

	goto discard_it;
}

/* Process the backlog queue of the socket.  Every skb on
 * the backlog holds a ref on an association or endpoint.
 * We hold this ref throughout the state machine to make
 * sure that the structure we need is still around.
 */
int sctp_backlog_rcv(struct sock *sk, struct sk_buff *skb)
{
	struct sctp_chunk *chunk = SCTP_INPUT_CB(skb)->chunk;
	struct sctp_inq *inqueue = &chunk->rcvr->inqueue;
	struct sctp_transport *t = chunk->transport;
	struct sctp_ep_common *rcvr = NULL;
	int backloged = 0;

	rcvr = chunk->rcvr;

	/* If the rcvr is dead then the association or endpoint
	 * has been deleted and we can safely drop the chunk
	 * and refs that we are holding.
	 */
	if (rcvr->dead) {
		sctp_chunk_free(chunk);
		goto done;
	}

	if (unlikely(rcvr->sk != sk)) {
		/* In this case, the association moved from one socket to
		 * another.  We are currently sitting on the backlog of the
		 * old socket, so we need to move.
		 * However, since we are here in the process context we
		 * need to take make sure that the user doesn't own
		 * the new socket when we process the packet.
		 * If the new socket is user-owned, queue the chunk to the
		 * backlog of the new socket without dropping any refs.
		 * Otherwise, we can safely push the chunk on the inqueue.
		 */

		sk = rcvr->sk;
		local_bh_disable();
		bh_lock_sock(sk);

		if (sock_owned_by_user(sk)) {
			if (sk_add_backlog(sk, skb, sk->sk_rcvbuf))
				sctp_chunk_free(chunk);
			else
				backloged = 1;
		} else
			sctp_inq_push(inqueue, chunk);

		bh_unlock_sock(sk);
		local_bh_enable();

		/* If the chunk was backloged again, don't drop refs */
		if (backloged)
			return 0;
	} else {
		sctp_inq_push(inqueue, chunk);
	}

done:
	/* Release the refs we took in sctp_add_backlog */
	if (SCTP_EP_TYPE_ASSOCIATION == rcvr->type)
		sctp_transport_put(t);
	else if (SCTP_EP_TYPE_SOCKET == rcvr->type)
		sctp_endpoint_put(sctp_ep(rcvr));
	else
		BUG();

	return 0;
}

static int sctp_add_backlog(struct sock *sk, struct sk_buff *skb)
{
	struct sctp_chunk *chunk = SCTP_INPUT_CB(skb)->chunk;
	struct sctp_transport *t = chunk->transport;
	struct sctp_ep_common *rcvr = chunk->rcvr;
	int ret;

	ret = sk_add_backlog(sk, skb, sk->sk_rcvbuf);
	if (!ret) {
		/* Hold the assoc/ep while hanging on the backlog queue.
		 * This way, we know structures we need will not disappear
		 * from us
		 */
		if (SCTP_EP_TYPE_ASSOCIATION == rcvr->type)
			sctp_transport_hold(t);
		else if (SCTP_EP_TYPE_SOCKET == rcvr->type)
			sctp_endpoint_hold(sctp_ep(rcvr));
		else
			BUG();
	}
	return ret;

}

/* Handle icmp frag needed error. */
void sctp_icmp_frag_needed(struct sock *sk, struct sctp_association *asoc,
			   struct sctp_transport *t, __u32 pmtu)
{
	if (!t || (t->pathmtu <= pmtu))
		return;

	if (sock_owned_by_user(sk)) {
		asoc->pmtu_pending = 1;
		t->pmtu_pending = 1;
		return;
	}

	if (t->param_flags & SPP_PMTUD_ENABLE) {
		/* Update transports view of the MTU */
		sctp_transport_update_pmtu(t, pmtu);

		/* Update association pmtu. */
		sctp_assoc_sync_pmtu(asoc);
	}

	/* Retransmit with the new pmtu setting.
	 * Normally, if PMTU discovery is disabled, an ICMP Fragmentation
	 * Needed will never be sent, but if a message was sent before
	 * PMTU discovery was disabled that was larger than the PMTU, it
	 * would not be fragmented, so it must be re-transmitted fragmented.
	 */
	sctp_retransmit(&asoc->outqueue, t, SCTP_RTXR_PMTUD);
}

void sctp_icmp_redirect(struct sock *sk, struct sctp_transport *t,
			struct sk_buff *skb)
{
	struct dst_entry *dst;

	if (sock_owned_by_user(sk) || !t)
		return;
	dst = sctp_transport_dst_check(t);
	if (dst)
		dst->ops->redirect(dst, sk, skb);
}

/*
 * SCTP Implementer's Guide, 2.37 ICMP handling procedures
 *
 * ICMP8) If the ICMP code is a "Unrecognized next header type encountered"
 *        or a "Protocol Unreachable" treat this message as an abort
 *        with the T bit set.
 *
 * This function sends an event to the state machine, which will abort the
 * association.
 *
 */
void sctp_icmp_proto_unreachable(struct sock *sk,
			   struct sctp_association *asoc,
			   struct sctp_transport *t)
{
	if (sock_owned_by_user(sk)) {
		if (timer_pending(&t->proto_unreach_timer))
			return;
		else {
			if (!mod_timer(&t->proto_unreach_timer,
						jiffies + (HZ/20)))
				sctp_association_hold(asoc);
		}
	} else {
		struct net *net = sock_net(sk);

		pr_debug("%s: unrecognized next header type "
			 "encountered!\n", __func__);

		if (del_timer(&t->proto_unreach_timer))
			sctp_association_put(asoc);

		sctp_do_sm(net, SCTP_EVENT_T_OTHER,
			   SCTP_ST_OTHER(SCTP_EVENT_ICMP_PROTO_UNREACH),
			   asoc->state, asoc->ep, asoc, t,
			   GFP_ATOMIC);
	}
}

/* Common lookup code for icmp/icmpv6 error handler. */
struct sock *sctp_err_lookup(struct net *net, int family, struct sk_buff *skb,
			     struct sctphdr *sctphdr,
			     struct sctp_association **app,
			     struct sctp_transport **tpp)
{
	struct sctp_init_chunk *chunkhdr, _chunkhdr;
	union sctp_addr saddr;
	union sctp_addr daddr;
	struct sctp_af *af;
	struct sock *sk = NULL;
	struct sctp_association *asoc;
	struct sctp_transport *transport = NULL;
	__u32 vtag = ntohl(sctphdr->vtag);

	*app = NULL; *tpp = NULL;

	af = sctp_get_af_specific(family);
	if (unlikely(!af)) {
		return NULL;
	}

	/* Initialize local addresses for lookups. */
	af->from_skb(&saddr, skb, 1);
	af->from_skb(&daddr, skb, 0);

	/* Look for an association that matches the incoming ICMP error
	 * packet.
	 */
	asoc = __sctp_lookup_association(net, &saddr, &daddr, &transport);
	if (!asoc)
		return NULL;

	sk = asoc->base.sk;

	/* RFC 4960, Appendix C. ICMP Handling
	 *
	 * ICMP6) An implementation MUST validate that the Verification Tag
	 * contained in the ICMP message matches the Verification Tag of
	 * the peer.  If the Verification Tag is not 0 and does NOT
	 * match, discard the ICMP message.  If it is 0 and the ICMP
	 * message contains enough bytes to verify that the chunk type is
	 * an INIT chunk and that the Initiate Tag matches the tag of the
	 * peer, continue with ICMP7.  If the ICMP message is too short
	 * or the chunk type or the Initiate Tag does not match, silently
	 * discard the packet.
	 */
	if (vtag == 0) {
		/* chunk header + first 4 octects of init header */
		chunkhdr = skb_header_pointer(skb, skb_transport_offset(skb) +
					      sizeof(struct sctphdr),
					      sizeof(struct sctp_chunkhdr) +
					      sizeof(__be32), &_chunkhdr);
		if (!chunkhdr ||
		    chunkhdr->chunk_hdr.type != SCTP_CID_INIT ||
		    ntohl(chunkhdr->init_hdr.init_tag) != asoc->c.my_vtag)
			goto out;

	} else if (vtag != asoc->c.peer_vtag) {
		goto out;
	}

	bh_lock_sock(sk);

	/* If too many ICMPs get dropped on busy
	 * servers this needs to be solved differently.
	 */
	if (sock_owned_by_user(sk))
		__NET_INC_STATS(net, LINUX_MIB_LOCKDROPPEDICMPS);

	*app = asoc;
	*tpp = transport;
	return sk;

out:
	sctp_transport_put(transport);
	return NULL;
}

/* Common cleanup code for icmp/icmpv6 error handler. */
void sctp_err_finish(struct sock *sk, struct sctp_transport *t)
{
	bh_unlock_sock(sk);
	sctp_transport_put(t);
}

/*
 * This routine is called by the ICMP module when it gets some
 * sort of error condition.  If err < 0 then the socket should
 * be closed and the error returned to the user.  If err > 0
 * it's just the icmp type << 8 | icmp code.  After adjustment
 * header points to the first 8 bytes of the sctp header.  We need
 * to find the appropriate port.
 *
 * The locking strategy used here is very "optimistic". When
 * someone else accesses the socket the ICMP is just dropped
 * and for some paths there is no check at all.
 * A more general error queue to queue errors for later handling
 * is probably better.
 *
 */
void sctp_v4_err(struct sk_buff *skb, __u32 info)
{
	const struct iphdr *iph = (const struct iphdr *)skb->data;
	const int ihlen = iph->ihl * 4;
	const int type = icmp_hdr(skb)->type;
	const int code = icmp_hdr(skb)->code;
	struct sock *sk;
	struct sctp_association *asoc = NULL;
	struct sctp_transport *transport;
	struct inet_sock *inet;
	__u16 saveip, savesctp;
	int err;
	struct net *net = dev_net(skb->dev);

	/* Fix up skb to look at the embedded net header. */
	saveip = skb->network_header;
	savesctp = skb->transport_header;
	skb_reset_network_header(skb);
	skb_set_transport_header(skb, ihlen);
	sk = sctp_err_lookup(net, AF_INET, skb, sctp_hdr(skb), &asoc, &transport);
	/* Put back, the original values. */
	skb->network_header = saveip;
	skb->transport_header = savesctp;
	if (!sk) {
		__ICMP_INC_STATS(net, ICMP_MIB_INERRORS);
		return;
	}
	/* Warning:  The sock lock is held.  Remember to call
	 * sctp_err_finish!
	 */

	switch (type) {
	case ICMP_PARAMETERPROB:
		err = EPROTO;
		break;
	case ICMP_DEST_UNREACH:
		if (code > NR_ICMP_UNREACH)
			goto out_unlock;

		/* PMTU discovery (RFC1191) */
		if (ICMP_FRAG_NEEDED == code) {
			sctp_icmp_frag_needed(sk, asoc, transport,
					      SCTP_TRUNC4(info));
			goto out_unlock;
		} else {
			if (ICMP_PROT_UNREACH == code) {
				sctp_icmp_proto_unreachable(sk, asoc,
							    transport);
				goto out_unlock;
			}
		}
		err = icmp_err_convert[code].errno;
		break;
	case ICMP_TIME_EXCEEDED:
		/* Ignore any time exceeded errors due to fragment reassembly
		 * timeouts.
		 */
		if (ICMP_EXC_FRAGTIME == code)
			goto out_unlock;

		err = EHOSTUNREACH;
		break;
	case ICMP_REDIRECT:
		sctp_icmp_redirect(sk, transport, skb);
		/* Fall through to out_unlock. */
	default:
		goto out_unlock;
	}

	inet = inet_sk(sk);
	if (!sock_owned_by_user(sk) && inet->recverr) {
		sk->sk_err = err;
		sk->sk_error_report(sk);
	} else {  /* Only an error on timeout */
		sk->sk_err_soft = err;
	}

out_unlock:
	sctp_err_finish(sk, transport);
}

/*
 * RFC 2960, 8.4 - Handle "Out of the blue" Packets.
 *
 * This function scans all the chunks in the OOTB packet to determine if
 * the packet should be discarded right away.  If a response might be needed
 * for this packet, or, if further processing is possible, the packet will
 * be queued to a proper inqueue for the next phase of handling.
 *
 * Output:
 * Return 0 - If further processing is needed.
 * Return 1 - If the packet can be discarded right away.
 */
static int sctp_rcv_ootb(struct sk_buff *skb)
{
	struct sctp_chunkhdr *ch, _ch;
	int ch_end, offset = 0;

	/* Scan through all the chunks in the packet.  */
	do {
		/* Make sure we have at least the header there */
		if (offset + sizeof(_ch) > skb->len)
			break;

		ch = skb_header_pointer(skb, offset, sizeof(*ch), &_ch);

		/* Break out if chunk length is less then minimal. */
		if (ntohs(ch->length) < sizeof(_ch))
			break;

		ch_end = offset + SCTP_PAD4(ntohs(ch->length));
		if (ch_end > skb->len)
			break;

		/* RFC 8.4, 2) If the OOTB packet contains an ABORT chunk, the
		 * receiver MUST silently discard the OOTB packet and take no
		 * further action.
		 */
		if (SCTP_CID_ABORT == ch->type)
			goto discard;

		/* RFC 8.4, 6) If the packet contains a SHUTDOWN COMPLETE
		 * chunk, the receiver should silently discard the packet
		 * and take no further action.
		 */
		if (SCTP_CID_SHUTDOWN_COMPLETE == ch->type)
			goto discard;

		/* RFC 4460, 2.11.2
		 * This will discard packets with INIT chunk bundled as
		 * subsequent chunks in the packet.  When INIT is first,
		 * the normal INIT processing will discard the chunk.
		 */
		if (SCTP_CID_INIT == ch->type && (void *)ch != skb->data)
			goto discard;

		offset = ch_end;
	} while (ch_end < skb->len);

	return 0;

discard:
	return 1;
}

/* Insert endpoint into the hash table.  */
static void __sctp_hash_endpoint(struct sctp_endpoint *ep)
{
	struct net *net = sock_net(ep->base.sk);
	struct sctp_ep_common *epb;
	struct sctp_hashbucket *head;

	epb = &ep->base;

	epb->hashent = sctp_ep_hashfn(net, epb->bind_addr.port);
	head = &sctp_ep_hashtable[epb->hashent];

	write_lock(&head->lock);
	hlist_add_head(&epb->node, &head->chain);
	write_unlock(&head->lock);
}

/* Add an endpoint to the hash. Local BH-safe. */
void sctp_hash_endpoint(struct sctp_endpoint *ep)
{
	local_bh_disable();
	__sctp_hash_endpoint(ep);
	local_bh_enable();
}

/* Remove endpoint from the hash table.  */
static void __sctp_unhash_endpoint(struct sctp_endpoint *ep)
{
	struct net *net = sock_net(ep->base.sk);
	struct sctp_hashbucket *head;
	struct sctp_ep_common *epb;

	epb = &ep->base;

	epb->hashent = sctp_ep_hashfn(net, epb->bind_addr.port);

	head = &sctp_ep_hashtable[epb->hashent];

	write_lock(&head->lock);
	hlist_del_init(&epb->node);
	write_unlock(&head->lock);
}

/* Remove endpoint from the hash.  Local BH-safe. */
void sctp_unhash_endpoint(struct sctp_endpoint *ep)
{
	local_bh_disable();
	__sctp_unhash_endpoint(ep);
	local_bh_enable();
}

/* Look up an endpoint. */
static struct sctp_endpoint *__sctp_rcv_lookup_endpoint(struct net *net,
						const union sctp_addr *laddr)
{
	struct sctp_hashbucket *head;
	struct sctp_ep_common *epb;
	struct sctp_endpoint *ep;
	int hash;

	hash = sctp_ep_hashfn(net, ntohs(laddr->v4.sin_port));
	head = &sctp_ep_hashtable[hash];
	read_lock(&head->lock);
	sctp_for_each_hentry(epb, &head->chain) {
		ep = sctp_ep(epb);
		if (sctp_endpoint_is_match(ep, net, laddr))
			goto hit;
	}

	ep = sctp_sk(net->sctp.ctl_sock)->ep;

hit:
	sctp_endpoint_hold(ep);
	read_unlock(&head->lock);
	return ep;
}

/* rhashtable for transport */
struct sctp_hash_cmp_arg {
	const union sctp_addr	*paddr;
	const struct net	*net;
	u16			lport;
};

static inline int sctp_hash_cmp(struct rhashtable_compare_arg *arg,
				const void *ptr)
{
	struct sctp_transport *t = (struct sctp_transport *)ptr;
	const struct sctp_hash_cmp_arg *x = arg->key;
	int err = 1;

	if (!sctp_cmp_addr_exact(&t->ipaddr, x->paddr))
		return err;
	if (!sctp_transport_hold(t))
		return err;

	if (!net_eq(sock_net(t->asoc->base.sk), x->net))
		goto out;
	if (x->lport != htons(t->asoc->base.bind_addr.port))
		goto out;

	err = 0;
out:
	sctp_transport_put(t);
	return err;
}

static inline u32 sctp_hash_obj(const void *data, u32 len, u32 seed)
{
	const struct sctp_transport *t = data;
	const union sctp_addr *paddr = &t->ipaddr;
	const struct net *net = sock_net(t->asoc->base.sk);
	u16 lport = htons(t->asoc->base.bind_addr.port);
	u32 addr;

	if (paddr->sa.sa_family == AF_INET6)
		addr = jhash(&paddr->v6.sin6_addr, 16, seed);
	else
		addr = paddr->v4.sin_addr.s_addr;

	return  jhash_3words(addr, ((__u32)paddr->v4.sin_port) << 16 |
			     (__force __u32)lport, net_hash_mix(net), seed);
}

static inline u32 sctp_hash_key(const void *data, u32 len, u32 seed)
{
	const struct sctp_hash_cmp_arg *x = data;
	const union sctp_addr *paddr = x->paddr;
	const struct net *net = x->net;
	u16 lport = x->lport;
	u32 addr;

	if (paddr->sa.sa_family == AF_INET6)
		addr = jhash(&paddr->v6.sin6_addr, 16, seed);
	else
		addr = paddr->v4.sin_addr.s_addr;

	return  jhash_3words(addr, ((__u32)paddr->v4.sin_port) << 16 |
			     (__force __u32)lport, net_hash_mix(net), seed);
}

static const struct rhashtable_params sctp_hash_params = {
	.head_offset		= offsetof(struct sctp_transport, node),
	.hashfn			= sctp_hash_key,
	.obj_hashfn		= sctp_hash_obj,
	.obj_cmpfn		= sctp_hash_cmp,
	.automatic_shrinking	= true,
};

int sctp_transport_hashtable_init(void)
{
	return rhltable_init(&sctp_transport_hashtable, &sctp_hash_params);
}

void sctp_transport_hashtable_destroy(void)
{
	rhltable_destroy(&sctp_transport_hashtable);
}

int sctp_hash_transport(struct sctp_transport *t)
{
	struct sctp_transport *transport;
	struct rhlist_head *tmp, *list;
	struct sctp_hash_cmp_arg arg;
	int err;

	if (t->asoc->temp)
		return 0;

	arg.net   = sock_net(t->asoc->base.sk);
	arg.paddr = &t->ipaddr;
	arg.lport = htons(t->asoc->base.bind_addr.port);

	rcu_read_lock();
	list = rhltable_lookup(&sctp_transport_hashtable, &arg,
			       sctp_hash_params);

	rhl_for_each_entry_rcu(transport, tmp, list, node)
		if (transport->asoc->ep == t->asoc->ep) {
			rcu_read_unlock();
			err = -EEXIST;
			goto out;
		}
	rcu_read_unlock();

	err = rhltable_insert_key(&sctp_transport_hashtable, &arg,
				  &t->node, sctp_hash_params);

out:
	if (err)
		pr_err_once("insert transport fail, errno %d\n", err);

	return err;
}

void sctp_unhash_transport(struct sctp_transport *t)
{
	if (t->asoc->temp)
		return;

	rhltable_remove(&sctp_transport_hashtable, &t->node,
			sctp_hash_params);
}

/* return a transport with holding it */
struct sctp_transport *sctp_addrs_lookup_transport(
				struct net *net,
				const union sctp_addr *laddr,
				const union sctp_addr *paddr)
{
	struct rhlist_head *tmp, *list;
	struct sctp_transport *t;
	struct sctp_hash_cmp_arg arg = {
		.paddr = paddr,
		.net   = net,
		.lport = laddr->v4.sin_port,
	};

	list = rhltable_lookup(&sctp_transport_hashtable, &arg,
			       sctp_hash_params);

	rhl_for_each_entry_rcu(t, tmp, list, node) {
		if (!sctp_transport_hold(t))
			continue;

		if (sctp_bind_addr_match(&t->asoc->base.bind_addr,
					 laddr, sctp_sk(t->asoc->base.sk)))
			return t;
		sctp_transport_put(t);
	}

	return NULL;
}

/* return a transport without holding it, as it's only used under sock lock */
struct sctp_transport *sctp_epaddr_lookup_transport(
				const struct sctp_endpoint *ep,
				const union sctp_addr *paddr)
{
	struct net *net = sock_net(ep->base.sk);
	struct rhlist_head *tmp, *list;
	struct sctp_transport *t;
	struct sctp_hash_cmp_arg arg = {
		.paddr = paddr,
		.net   = net,
		.lport = htons(ep->base.bind_addr.port),
	};

	list = rhltable_lookup(&sctp_transport_hashtable, &arg,
			       sctp_hash_params);

	rhl_for_each_entry_rcu(t, tmp, list, node)
		if (ep == t->asoc->ep)
			return t;

	return NULL;
}

/* Look up an association. */
static struct sctp_association *__sctp_lookup_association(
					struct net *net,
					const union sctp_addr *local,
					const union sctp_addr *peer,
					struct sctp_transport **pt)
{
	struct sctp_transport *t;
	struct sctp_association *asoc = NULL;

	t = sctp_addrs_lookup_transport(net, local, peer);
	if (!t)
		goto out;

	asoc = t->asoc;
	*pt = t;

out:
	return asoc;
}

/* Look up an association. protected by RCU read lock */
static
struct sctp_association *sctp_lookup_association(struct net *net,
						 const union sctp_addr *laddr,
						 const union sctp_addr *paddr,
						 struct sctp_transport **transportp)
{
	struct sctp_association *asoc;

	rcu_read_lock();
	asoc = __sctp_lookup_association(net, laddr, paddr, transportp);
	rcu_read_unlock();

	return asoc;
}

/* Is there an association matching the given local and peer addresses? */
int sctp_has_association(struct net *net,
			 const union sctp_addr *laddr,
			 const union sctp_addr *paddr)
{
	struct sctp_association *asoc;
	struct sctp_transport *transport;

	if ((asoc = sctp_lookup_association(net, laddr, paddr, &transport))) {
		sctp_transport_put(transport);
		return 1;
	}

	return 0;
}

/*
 * SCTP Implementors Guide, 2.18 Handling of address
 * parameters within the INIT or INIT-ACK.
 *
 * D) When searching for a matching TCB upon reception of an INIT
 *    or INIT-ACK chunk the receiver SHOULD use not only the
 *    source address of the packet (containing the INIT or
 *    INIT-ACK) but the receiver SHOULD also use all valid
 *    address parameters contained within the chunk.
 *
 * 2.18.3 Solution description
 *
 * This new text clearly specifies to an implementor the need
 * to look within the INIT or INIT-ACK. Any implementation that
 * does not do this, may not be able to establish associations
 * in certain circumstances.
 *
 */
static struct sctp_association *__sctp_rcv_init_lookup(struct net *net,
	struct sk_buff *skb,
	const union sctp_addr *laddr, struct sctp_transport **transportp)
{
	struct sctp_association *asoc;
	union sctp_addr addr;
	union sctp_addr *paddr = &addr;
	struct sctphdr *sh = sctp_hdr(skb);
	union sctp_params params;
	struct sctp_init_chunk *init;
	struct sctp_af *af;

	/*
	 * This code will NOT touch anything inside the chunk--it is
	 * strictly READ-ONLY.
	 *
	 * RFC 2960 3  SCTP packet Format
	 *
	 * Multiple chunks can be bundled into one SCTP packet up to
	 * the MTU size, except for the INIT, INIT ACK, and SHUTDOWN
	 * COMPLETE chunks.  These chunks MUST NOT be bundled with any
	 * other chunk in a packet.  See Section 6.10 for more details
	 * on chunk bundling.
	 */

	/* Find the start of the TLVs and the end of the chunk.  This is
	 * the region we search for address parameters.
	 */
	init = (struct sctp_init_chunk *)skb->data;

	/* Walk the parameters looking for embedded addresses. */
	sctp_walk_params(params, init, init_hdr.params) {

		/* Note: Ignoring hostname addresses. */
		af = sctp_get_af_specific(param_type2af(params.p->type));
		if (!af)
			continue;

		af->from_addr_param(paddr, params.addr, sh->source, 0);

		asoc = __sctp_lookup_association(net, laddr, paddr, transportp);
		if (asoc)
			return asoc;
	}

	return NULL;
}

/* ADD-IP, Section 5.2
 * When an endpoint receives an ASCONF Chunk from the remote peer
 * special procedures may be needed to identify the association the
 * ASCONF Chunk is associated with. To properly find the association
 * the following procedures SHOULD be followed:
 *
 * D2) If the association is not found, use the address found in the
 * Address Parameter TLV combined with the port number found in the
 * SCTP common header. If found proceed to rule D4.
 *
 * D2-ext) If more than one ASCONF Chunks are packed together, use the
 * address found in the ASCONF Address Parameter TLV of each of the
 * subsequent ASCONF Chunks. If found, proceed to rule D4.
 */
static struct sctp_association *__sctp_rcv_asconf_lookup(
					struct net *net,
					struct sctp_chunkhdr *ch,
					const union sctp_addr *laddr,
					__be16 peer_port,
					struct sctp_transport **transportp)
{
	struct sctp_addip_chunk *asconf = (struct sctp_addip_chunk *)ch;
	struct sctp_af *af;
	union sctp_addr_param *param;
	union sctp_addr paddr;

	/* Skip over the ADDIP header and find the Address parameter */
	param = (union sctp_addr_param *)(asconf + 1);

	af = sctp_get_af_specific(param_type2af(param->p.type));
	if (unlikely(!af))
		return NULL;

	af->from_addr_param(&paddr, param, peer_port, 0);

	return __sctp_lookup_association(net, laddr, &paddr, transportp);
}


/* SCTP-AUTH, Section 6.3:
*    If the receiver does not find a STCB for a packet containing an AUTH
*    chunk as the first chunk and not a COOKIE-ECHO chunk as the second
*    chunk, it MUST use the chunks after the AUTH chunk to look up an existing
*    association.
*
* This means that any chunks that can help us identify the association need
* to be looked at to find this association.
*/
static struct sctp_association *__sctp_rcv_walk_lookup(struct net *net,
				      struct sk_buff *skb,
				      const union sctp_addr *laddr,
				      struct sctp_transport **transportp)
{
	struct sctp_association *asoc = NULL;
	struct sctp_chunkhdr *ch;
	int have_auth = 0;
	unsigned int chunk_num = 1;
	__u8 *ch_end;

	/* Walk through the chunks looking for AUTH or ASCONF chunks
	 * to help us find the association.
	 */
	ch = (struct sctp_chunkhdr *)skb->data;
	do {
		/* Break out if chunk length is less then minimal. */
		if (ntohs(ch->length) < sizeof(*ch))
			break;

		ch_end = ((__u8 *)ch) + SCTP_PAD4(ntohs(ch->length));
		if (ch_end > skb_tail_pointer(skb))
			break;

		switch (ch->type) {
		case SCTP_CID_AUTH:
			have_auth = chunk_num;
			break;

		case SCTP_CID_COOKIE_ECHO:
			/* If a packet arrives containing an AUTH chunk as
			 * a first chunk, a COOKIE-ECHO chunk as the second
			 * chunk, and possibly more chunks after them, and
			 * the receiver does not have an STCB for that
			 * packet, then authentication is based on
			 * the contents of the COOKIE- ECHO chunk.
			 */
			if (have_auth == 1 && chunk_num == 2)
				return NULL;
			break;

		case SCTP_CID_ASCONF:
			if (have_auth || net->sctp.addip_noauth)
				asoc = __sctp_rcv_asconf_lookup(
						net, ch, laddr,
						sctp_hdr(skb)->source,
						transportp);
		default:
			break;
		}

		if (asoc)
			break;

		ch = (struct sctp_chunkhdr *)ch_end;
		chunk_num++;
	} while (ch_end < skb_tail_pointer(skb));

	return asoc;
}

/*
 * There are circumstances when we need to look inside the SCTP packet
 * for information to help us find the association.   Examples
 * include looking inside of INIT/INIT-ACK chunks or after the AUTH
 * chunks.
 */
static struct sctp_association *__sctp_rcv_lookup_harder(struct net *net,
				      struct sk_buff *skb,
				      const union sctp_addr *laddr,
				      struct sctp_transport **transportp)
{
	struct sctp_chunkhdr *ch;

	/* We do not allow GSO frames here as we need to linearize and
	 * then cannot guarantee frame boundaries. This shouldn't be an
	 * issue as packets hitting this are mostly INIT or INIT-ACK and
	 * those cannot be on GSO-style anyway.
	 */
	if ((skb_shinfo(skb)->gso_type & SKB_GSO_SCTP) == SKB_GSO_SCTP)
		return NULL;

	ch = (struct sctp_chunkhdr *)skb->data;

	/* The code below will attempt to walk the chunk and extract
	 * parameter information.  Before we do that, we need to verify
	 * that the chunk length doesn't cause overflow.  Otherwise, we'll
	 * walk off the end.
	 */
	if (SCTP_PAD4(ntohs(ch->length)) > skb->len)
		return NULL;

	/* If this is INIT/INIT-ACK look inside the chunk too. */
	if (ch->type == SCTP_CID_INIT || ch->type == SCTP_CID_INIT_ACK)
		return __sctp_rcv_init_lookup(net, skb, laddr, transportp);

	return __sctp_rcv_walk_lookup(net, skb, laddr, transportp);
}

/* Lookup an association for an inbound skb. */
static struct sctp_association *__sctp_rcv_lookup(struct net *net,
				      struct sk_buff *skb,
				      const union sctp_addr *paddr,
				      const union sctp_addr *laddr,
				      struct sctp_transport **transportp)
{
	struct sctp_association *asoc;

	asoc = __sctp_lookup_association(net, laddr, paddr, transportp);
	if (asoc)
		goto out;

	/* Further lookup for INIT/INIT-ACK packets.
	 * SCTP Implementors Guide, 2.18 Handling of address
	 * parameters within the INIT or INIT-ACK.
	 */
	asoc = __sctp_rcv_lookup_harder(net, skb, laddr, transportp);
	if (asoc)
		goto out;

	if (paddr->sa.sa_family == AF_INET)
		pr_debug("sctp: asoc not found for src:%pI4:%d dst:%pI4:%d\n",
			 &laddr->v4.sin_addr, ntohs(laddr->v4.sin_port),
			 &paddr->v4.sin_addr, ntohs(paddr->v4.sin_port));
	else
		pr_debug("sctp: asoc not found for src:%pI6:%d dst:%pI6:%d\n",
			 &laddr->v6.sin6_addr, ntohs(laddr->v6.sin6_port),
			 &paddr->v6.sin6_addr, ntohs(paddr->v6.sin6_port));

out:
	return asoc;
}
