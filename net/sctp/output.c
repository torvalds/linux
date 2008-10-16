/* SCTP kernel implementation
 * (C) Copyright IBM Corp. 2001, 2004
 * Copyright (c) 1999-2000 Cisco, Inc.
 * Copyright (c) 1999-2001 Motorola, Inc.
 *
 * This file is part of the SCTP kernel implementation
 *
 * These functions handle output processing.
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
 * along with GNU CC; see the file COPYING.  If not, write to
 * the Free Software Foundation, 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Please send any bug reports or fixes you make to the
 * email address(es):
 *    lksctp developers <lksctp-developers@lists.sourceforge.net>
 *
 * Or submit a bug report through the following website:
 *    http://www.sf.net/projects/lksctp
 *
 * Written or modified by:
 *    La Monte H.P. Yarroll <piggy@acm.org>
 *    Karl Knutson          <karl@athena.chicago.il.us>
 *    Jon Grimm             <jgrimm@austin.ibm.com>
 *    Sridhar Samudrala     <sri@us.ibm.com>
 *
 * Any bugs reported given to us we will try to fix... any fixes shared will
 * be incorporated into the next SCTP release.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/wait.h>
#include <linux/time.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/init.h>
#include <net/inet_ecn.h>
#include <net/icmp.h>
#include <net/net_namespace.h>

#ifndef TEST_FRAME
#include <net/tcp.h>
#endif /* TEST_FRAME (not defined) */

#include <linux/socket.h> /* for sa_family_t */
#include <net/sock.h>

#include <net/sctp/sctp.h>
#include <net/sctp/sm.h>
#include <net/sctp/checksum.h>

/* Forward declarations for private helpers. */
static sctp_xmit_t sctp_packet_append_data(struct sctp_packet *packet,
					   struct sctp_chunk *chunk);

/* Config a packet.
 * This appears to be a followup set of initializations.
 */
struct sctp_packet *sctp_packet_config(struct sctp_packet *packet,
				       __u32 vtag, int ecn_capable)
{
	struct sctp_chunk *chunk = NULL;

	SCTP_DEBUG_PRINTK("%s: packet:%p vtag:0x%x\n", __func__,
			  packet, vtag);

	packet->vtag = vtag;
	packet->has_cookie_echo = 0;
	packet->has_sack = 0;
	packet->has_auth = 0;
	packet->has_data = 0;
	packet->ipfragok = 0;
	packet->auth = NULL;

	if (ecn_capable && sctp_packet_empty(packet)) {
		chunk = sctp_get_ecne_prepend(packet->transport->asoc);

		/* If there a is a prepend chunk stick it on the list before
		 * any other chunks get appended.
		 */
		if (chunk)
			sctp_packet_append_chunk(packet, chunk);
	}

	return packet;
}

/* Initialize the packet structure. */
struct sctp_packet *sctp_packet_init(struct sctp_packet *packet,
				     struct sctp_transport *transport,
				     __u16 sport, __u16 dport)
{
	struct sctp_association *asoc = transport->asoc;
	size_t overhead;

	SCTP_DEBUG_PRINTK("%s: packet:%p transport:%p\n", __func__,
			  packet, transport);

	packet->transport = transport;
	packet->source_port = sport;
	packet->destination_port = dport;
	INIT_LIST_HEAD(&packet->chunk_list);
	if (asoc) {
		struct sctp_sock *sp = sctp_sk(asoc->base.sk);
		overhead = sp->pf->af->net_header_len;
	} else {
		overhead = sizeof(struct ipv6hdr);
	}
	overhead += sizeof(struct sctphdr);
	packet->overhead = overhead;
	packet->size = overhead;
	packet->vtag = 0;
	packet->has_cookie_echo = 0;
	packet->has_sack = 0;
	packet->has_auth = 0;
	packet->has_data = 0;
	packet->ipfragok = 0;
	packet->malloced = 0;
	packet->auth = NULL;
	return packet;
}

/* Free a packet.  */
void sctp_packet_free(struct sctp_packet *packet)
{
	struct sctp_chunk *chunk, *tmp;

	SCTP_DEBUG_PRINTK("%s: packet:%p\n", __func__, packet);

	list_for_each_entry_safe(chunk, tmp, &packet->chunk_list, list) {
		list_del_init(&chunk->list);
		sctp_chunk_free(chunk);
	}

	if (packet->malloced)
		kfree(packet);
}

/* This routine tries to append the chunk to the offered packet. If adding
 * the chunk causes the packet to exceed the path MTU and COOKIE_ECHO chunk
 * is not present in the packet, it transmits the input packet.
 * Data can be bundled with a packet containing a COOKIE_ECHO chunk as long
 * as it can fit in the packet, but any more data that does not fit in this
 * packet can be sent only after receiving the COOKIE_ACK.
 */
sctp_xmit_t sctp_packet_transmit_chunk(struct sctp_packet *packet,
				       struct sctp_chunk *chunk,
				       int one_packet)
{
	sctp_xmit_t retval;
	int error = 0;

	SCTP_DEBUG_PRINTK("%s: packet:%p chunk:%p\n", __func__,
			  packet, chunk);

	switch ((retval = (sctp_packet_append_chunk(packet, chunk)))) {
	case SCTP_XMIT_PMTU_FULL:
		if (!packet->has_cookie_echo) {
			error = sctp_packet_transmit(packet);
			if (error < 0)
				chunk->skb->sk->sk_err = -error;

			/* If we have an empty packet, then we can NOT ever
			 * return PMTU_FULL.
			 */
			if (!one_packet)
				retval = sctp_packet_append_chunk(packet,
								  chunk);
		}
		break;

	case SCTP_XMIT_RWND_FULL:
	case SCTP_XMIT_OK:
	case SCTP_XMIT_NAGLE_DELAY:
		break;
	}

	return retval;
}

/* Try to bundle an auth chunk into the packet. */
static sctp_xmit_t sctp_packet_bundle_auth(struct sctp_packet *pkt,
					   struct sctp_chunk *chunk)
{
	struct sctp_association *asoc = pkt->transport->asoc;
	struct sctp_chunk *auth;
	sctp_xmit_t retval = SCTP_XMIT_OK;

	/* if we don't have an association, we can't do authentication */
	if (!asoc)
		return retval;

	/* See if this is an auth chunk we are bundling or if
	 * auth is already bundled.
	 */
	if (chunk->chunk_hdr->type == SCTP_CID_AUTH || pkt->auth)
		return retval;

	/* if the peer did not request this chunk to be authenticated,
	 * don't do it
	 */
	if (!chunk->auth)
		return retval;

	auth = sctp_make_auth(asoc);
	if (!auth)
		return retval;

	retval = sctp_packet_append_chunk(pkt, auth);

	return retval;
}

/* Try to bundle a SACK with the packet. */
static sctp_xmit_t sctp_packet_bundle_sack(struct sctp_packet *pkt,
					   struct sctp_chunk *chunk)
{
	sctp_xmit_t retval = SCTP_XMIT_OK;

	/* If sending DATA and haven't aleady bundled a SACK, try to
	 * bundle one in to the packet.
	 */
	if (sctp_chunk_is_data(chunk) && !pkt->has_sack &&
	    !pkt->has_cookie_echo) {
		struct sctp_association *asoc;
		asoc = pkt->transport->asoc;

		if (asoc->a_rwnd > asoc->rwnd) {
			struct sctp_chunk *sack;
			asoc->a_rwnd = asoc->rwnd;
			sack = sctp_make_sack(asoc);
			if (sack) {
				struct timer_list *timer;
				retval = sctp_packet_append_chunk(pkt, sack);
				asoc->peer.sack_needed = 0;
				timer = &asoc->timers[SCTP_EVENT_TIMEOUT_SACK];
				if (timer_pending(timer) && del_timer(timer))
					sctp_association_put(asoc);
			}
		}
	}
	return retval;
}

/* Append a chunk to the offered packet reporting back any inability to do
 * so.
 */
sctp_xmit_t sctp_packet_append_chunk(struct sctp_packet *packet,
				     struct sctp_chunk *chunk)
{
	sctp_xmit_t retval = SCTP_XMIT_OK;
	__u16 chunk_len = WORD_ROUND(ntohs(chunk->chunk_hdr->length));
	size_t psize;
	size_t pmtu;
	int too_big;

	SCTP_DEBUG_PRINTK("%s: packet:%p chunk:%p\n", __func__, packet,
			  chunk);

	/* Try to bundle AUTH chunk */
	retval = sctp_packet_bundle_auth(packet, chunk);
	if (retval != SCTP_XMIT_OK)
		goto finish;

	/* Try to bundle SACK chunk */
	retval = sctp_packet_bundle_sack(packet, chunk);
	if (retval != SCTP_XMIT_OK)
		goto finish;

	psize = packet->size;
	pmtu  = ((packet->transport->asoc) ?
		 (packet->transport->asoc->pathmtu) :
		 (packet->transport->pathmtu));

	too_big = (psize + chunk_len > pmtu);

	/* Decide if we need to fragment or resubmit later. */
	if (too_big) {
		/* It's OK to fragmet at IP level if any one of the following
		 * is true:
		 * 	1. The packet is empty (meaning this chunk is greater
		 * 	   the MTU)
		 * 	2. The chunk we are adding is a control chunk
		 * 	3. The packet doesn't have any data in it yet and data
		 * 	requires authentication.
		 */
		if (sctp_packet_empty(packet) || !sctp_chunk_is_data(chunk) ||
		    (!packet->has_data && chunk->auth)) {
			/* We no longer do re-fragmentation.
			 * Just fragment at the IP layer, if we
			 * actually hit this condition
			 */
			packet->ipfragok = 1;
			goto append;

		} else {
			retval = SCTP_XMIT_PMTU_FULL;
			goto finish;
		}
	}

append:
	/* We believe that this chunk is OK to add to the packet (as
	 * long as we have the cwnd for it).
	 */

	/* DATA is a special case since we must examine both rwnd and cwnd
	 * before we send DATA.
	 */
	switch (chunk->chunk_hdr->type) {
	    case SCTP_CID_DATA:
		retval = sctp_packet_append_data(packet, chunk);
		/* Disallow SACK bundling after DATA. */
		packet->has_sack = 1;
		/* Disallow AUTH bundling after DATA */
		packet->has_auth = 1;
		/* Let it be knows that packet has DATA in it */
		packet->has_data = 1;
		if (SCTP_XMIT_OK != retval)
			goto finish;
		break;
	    case SCTP_CID_COOKIE_ECHO:
		packet->has_cookie_echo = 1;
		break;

	    case SCTP_CID_SACK:
		packet->has_sack = 1;
		break;

	    case SCTP_CID_AUTH:
		packet->has_auth = 1;
		packet->auth = chunk;
		break;
	}

	/* It is OK to send this chunk.  */
	list_add_tail(&chunk->list, &packet->chunk_list);
	packet->size += chunk_len;
	chunk->transport = packet->transport;
finish:
	return retval;
}

/* All packets are sent to the network through this function from
 * sctp_outq_tail().
 *
 * The return value is a normal kernel error return value.
 */
int sctp_packet_transmit(struct sctp_packet *packet)
{
	struct sctp_transport *tp = packet->transport;
	struct sctp_association *asoc = tp->asoc;
	struct sctphdr *sh;
	__be32 crc32 = __constant_cpu_to_be32(0);
	struct sk_buff *nskb;
	struct sctp_chunk *chunk, *tmp;
	struct sock *sk;
	int err = 0;
	int padding;		/* How much padding do we need?  */
	__u8 has_data = 0;
	struct dst_entry *dst = tp->dst;
	unsigned char *auth = NULL;	/* pointer to auth in skb data */
	__u32 cksum_buf_len = sizeof(struct sctphdr);

	SCTP_DEBUG_PRINTK("%s: packet:%p\n", __func__, packet);

	/* Do NOT generate a chunkless packet. */
	if (list_empty(&packet->chunk_list))
		return err;

	/* Set up convenience variables... */
	chunk = list_entry(packet->chunk_list.next, struct sctp_chunk, list);
	sk = chunk->skb->sk;

	/* Allocate the new skb.  */
	nskb = alloc_skb(packet->size + LL_MAX_HEADER, GFP_ATOMIC);
	if (!nskb)
		goto nomem;

	/* Make sure the outbound skb has enough header room reserved. */
	skb_reserve(nskb, packet->overhead + LL_MAX_HEADER);

	/* Set the owning socket so that we know where to get the
	 * destination IP address.
	 */
	skb_set_owner_w(nskb, sk);

	/* The 'obsolete' field of dst is set to 2 when a dst is freed. */
	if (!dst || (dst->obsolete > 1)) {
		dst_release(dst);
		sctp_transport_route(tp, NULL, sctp_sk(sk));
		if (asoc && (asoc->param_flags & SPP_PMTUD_ENABLE)) {
			sctp_assoc_sync_pmtu(asoc);
		}
	}
	nskb->dst = dst_clone(tp->dst);
	if (!nskb->dst)
		goto no_route;
	dst = nskb->dst;

	/* Build the SCTP header.  */
	sh = (struct sctphdr *)skb_push(nskb, sizeof(struct sctphdr));
	sh->source = htons(packet->source_port);
	sh->dest   = htons(packet->destination_port);

	/* From 6.8 Adler-32 Checksum Calculation:
	 * After the packet is constructed (containing the SCTP common
	 * header and one or more control or DATA chunks), the
	 * transmitter shall:
	 *
	 * 1) Fill in the proper Verification Tag in the SCTP common
	 *    header and initialize the checksum field to 0's.
	 */
	sh->vtag     = htonl(packet->vtag);
	sh->checksum = 0;

	/**
	 * 6.10 Bundling
	 *
	 *    An endpoint bundles chunks by simply including multiple
	 *    chunks in one outbound SCTP packet.  ...
	 */

	/**
	 * 3.2  Chunk Field Descriptions
	 *
	 * The total length of a chunk (including Type, Length and
	 * Value fields) MUST be a multiple of 4 bytes.  If the length
	 * of the chunk is not a multiple of 4 bytes, the sender MUST
	 * pad the chunk with all zero bytes and this padding is not
	 * included in the chunk length field.  The sender should
	 * never pad with more than 3 bytes.
	 *
	 * [This whole comment explains WORD_ROUND() below.]
	 */
	SCTP_DEBUG_PRINTK("***sctp_transmit_packet***\n");
	list_for_each_entry_safe(chunk, tmp, &packet->chunk_list, list) {
		list_del_init(&chunk->list);
		if (sctp_chunk_is_data(chunk)) {

			if (!chunk->has_tsn) {
				sctp_chunk_assign_ssn(chunk);
				sctp_chunk_assign_tsn(chunk);

			/* 6.3.1 C4) When data is in flight and when allowed
			 * by rule C5, a new RTT measurement MUST be made each
			 * round trip.  Furthermore, new RTT measurements
			 * SHOULD be made no more than once per round-trip
			 * for a given destination transport address.
			 */

				if (!tp->rto_pending) {
					chunk->rtt_in_progress = 1;
					tp->rto_pending = 1;
				}
			} else
				chunk->resent = 1;

			chunk->sent_at = jiffies;
			has_data = 1;
		}

		padding = WORD_ROUND(chunk->skb->len) - chunk->skb->len;
		if (padding)
			memset(skb_put(chunk->skb, padding), 0, padding);

		/* if this is the auth chunk that we are adding,
		 * store pointer where it will be added and put
		 * the auth into the packet.
		 */
		if (chunk == packet->auth)
			auth = skb_tail_pointer(nskb);

		cksum_buf_len += chunk->skb->len;
		memcpy(skb_put(nskb, chunk->skb->len),
			       chunk->skb->data, chunk->skb->len);

		SCTP_DEBUG_PRINTK("%s %p[%s] %s 0x%x, %s %d, %s %d, %s %d\n",
				  "*** Chunk", chunk,
				  sctp_cname(SCTP_ST_CHUNK(
					  chunk->chunk_hdr->type)),
				  chunk->has_tsn ? "TSN" : "No TSN",
				  chunk->has_tsn ?
				  ntohl(chunk->subh.data_hdr->tsn) : 0,
				  "length", ntohs(chunk->chunk_hdr->length),
				  "chunk->skb->len", chunk->skb->len,
				  "rtt_in_progress", chunk->rtt_in_progress);

		/*
		 * If this is a control chunk, this is our last
		 * reference. Free data chunks after they've been
		 * acknowledged or have failed.
		 */
		if (!sctp_chunk_is_data(chunk))
			sctp_chunk_free(chunk);
	}

	/* SCTP-AUTH, Section 6.2
	 *    The sender MUST calculate the MAC as described in RFC2104 [2]
	 *    using the hash function H as described by the MAC Identifier and
	 *    the shared association key K based on the endpoint pair shared key
	 *    described by the shared key identifier.  The 'data' used for the
	 *    computation of the AUTH-chunk is given by the AUTH chunk with its
	 *    HMAC field set to zero (as shown in Figure 6) followed by all
	 *    chunks that are placed after the AUTH chunk in the SCTP packet.
	 */
	if (auth)
		sctp_auth_calculate_hmac(asoc, nskb,
					(struct sctp_auth_chunk *)auth,
					GFP_ATOMIC);

	/* 2) Calculate the Adler-32 checksum of the whole packet,
	 *    including the SCTP common header and all the
	 *    chunks.
	 *
	 * Note: Adler-32 is no longer applicable, as has been replaced
	 * by CRC32-C as described in <draft-ietf-tsvwg-sctpcsum-02.txt>.
	 */
	if (!(dst->dev->features & NETIF_F_NO_CSUM)) {
		crc32 = sctp_start_cksum((__u8 *)sh, cksum_buf_len);
		crc32 = sctp_end_cksum(crc32);
	} else
		nskb->ip_summed = CHECKSUM_UNNECESSARY;

	/* 3) Put the resultant value into the checksum field in the
	 *    common header, and leave the rest of the bits unchanged.
	 */
	sh->checksum = crc32;

	/* IP layer ECN support
	 * From RFC 2481
	 *  "The ECN-Capable Transport (ECT) bit would be set by the
	 *   data sender to indicate that the end-points of the
	 *   transport protocol are ECN-capable."
	 *
	 * Now setting the ECT bit all the time, as it should not cause
	 * any problems protocol-wise even if our peer ignores it.
	 *
	 * Note: The works for IPv6 layer checks this bit too later
	 * in transmission.  See IP6_ECN_flow_xmit().
	 */
	(*tp->af_specific->ecn_capable)(nskb->sk);

	/* Set up the IP options.  */
	/* BUG: not implemented
	 * For v4 this all lives somewhere in sk->sk_opt...
	 */

	/* Dump that on IP!  */
	if (asoc && asoc->peer.last_sent_to != tp) {
		/* Considering the multiple CPU scenario, this is a
		 * "correcter" place for last_sent_to.  --xguo
		 */
		asoc->peer.last_sent_to = tp;
	}

	if (has_data) {
		struct timer_list *timer;
		unsigned long timeout;

		tp->last_time_used = jiffies;

		/* Restart the AUTOCLOSE timer when sending data. */
		if (sctp_state(asoc, ESTABLISHED) && asoc->autoclose) {
			timer = &asoc->timers[SCTP_EVENT_TIMEOUT_AUTOCLOSE];
			timeout = asoc->timeouts[SCTP_EVENT_TIMEOUT_AUTOCLOSE];

			if (!mod_timer(timer, jiffies + timeout))
				sctp_association_hold(asoc);
		}
	}

	SCTP_DEBUG_PRINTK("***sctp_transmit_packet*** skb len %d\n",
			  nskb->len);

	nskb->local_df = packet->ipfragok;
	(*tp->af_specific->sctp_xmit)(nskb, tp);

out:
	packet->size = packet->overhead;
	return err;
no_route:
	kfree_skb(nskb);
	IP_INC_STATS_BH(&init_net, IPSTATS_MIB_OUTNOROUTES);

	/* FIXME: Returning the 'err' will effect all the associations
	 * associated with a socket, although only one of the paths of the
	 * association is unreachable.
	 * The real failure of a transport or association can be passed on
	 * to the user via notifications. So setting this error may not be
	 * required.
	 */
	 /* err = -EHOSTUNREACH; */
err:
	/* Control chunks are unreliable so just drop them.  DATA chunks
	 * will get resent or dropped later.
	 */

	list_for_each_entry_safe(chunk, tmp, &packet->chunk_list, list) {
		list_del_init(&chunk->list);
		if (!sctp_chunk_is_data(chunk))
			sctp_chunk_free(chunk);
	}
	goto out;
nomem:
	err = -ENOMEM;
	goto err;
}

/********************************************************************
 * 2nd Level Abstractions
 ********************************************************************/

/* This private function handles the specifics of appending DATA chunks.  */
static sctp_xmit_t sctp_packet_append_data(struct sctp_packet *packet,
					   struct sctp_chunk *chunk)
{
	sctp_xmit_t retval = SCTP_XMIT_OK;
	size_t datasize, rwnd, inflight;
	struct sctp_transport *transport = packet->transport;
	__u32 max_burst_bytes;
	struct sctp_association *asoc = transport->asoc;
	struct sctp_sock *sp = sctp_sk(asoc->base.sk);
	struct sctp_outq *q = &asoc->outqueue;

	/* RFC 2960 6.1  Transmission of DATA Chunks
	 *
	 * A) At any given time, the data sender MUST NOT transmit new data to
	 * any destination transport address if its peer's rwnd indicates
	 * that the peer has no buffer space (i.e. rwnd is 0, see Section
	 * 6.2.1).  However, regardless of the value of rwnd (including if it
	 * is 0), the data sender can always have one DATA chunk in flight to
	 * the receiver if allowed by cwnd (see rule B below).  This rule
	 * allows the sender to probe for a change in rwnd that the sender
	 * missed due to the SACK having been lost in transit from the data
	 * receiver to the data sender.
	 */

	rwnd = asoc->peer.rwnd;
	inflight = asoc->outqueue.outstanding_bytes;

	datasize = sctp_data_size(chunk);

	if (datasize > rwnd) {
		if (inflight > 0) {
			/* We have (at least) one data chunk in flight,
			 * so we can't fall back to rule 6.1 B).
			 */
			retval = SCTP_XMIT_RWND_FULL;
			goto finish;
		}
	}

	/* sctpimpguide-05 2.14.2
	 * D) When the time comes for the sender to
	 * transmit new DATA chunks, the protocol parameter Max.Burst MUST
	 * first be applied to limit how many new DATA chunks may be sent.
	 * The limit is applied by adjusting cwnd as follows:
	 * 	if ((flightsize + Max.Burst * MTU) < cwnd)
	 *		cwnd = flightsize + Max.Burst * MTU
	 */
	max_burst_bytes = asoc->max_burst * asoc->pathmtu;
	if ((transport->flight_size + max_burst_bytes) < transport->cwnd) {
		transport->cwnd = transport->flight_size + max_burst_bytes;
		SCTP_DEBUG_PRINTK("%s: cwnd limited by max_burst: "
				  "transport: %p, cwnd: %d, "
				  "ssthresh: %d, flight_size: %d, "
				  "pba: %d\n",
				  __func__, transport,
				  transport->cwnd,
				  transport->ssthresh,
				  transport->flight_size,
				  transport->partial_bytes_acked);
	}

	/* RFC 2960 6.1  Transmission of DATA Chunks
	 *
	 * B) At any given time, the sender MUST NOT transmit new data
	 * to a given transport address if it has cwnd or more bytes
	 * of data outstanding to that transport address.
	 */
	/* RFC 7.2.4 & the Implementers Guide 2.8.
	 *
	 * 3) ...
	 *    When a Fast Retransmit is being performed the sender SHOULD
	 *    ignore the value of cwnd and SHOULD NOT delay retransmission.
	 */
	if (chunk->fast_retransmit != SCTP_NEED_FRTX)
		if (transport->flight_size >= transport->cwnd) {
			retval = SCTP_XMIT_RWND_FULL;
			goto finish;
		}

	/* Nagle's algorithm to solve small-packet problem:
	 * Inhibit the sending of new chunks when new outgoing data arrives
	 * if any previously transmitted data on the connection remains
	 * unacknowledged.
	 */
	if (!sp->nodelay && sctp_packet_empty(packet) &&
	    q->outstanding_bytes && sctp_state(asoc, ESTABLISHED)) {
		unsigned len = datasize + q->out_qlen;

		/* Check whether this chunk and all the rest of pending
		 * data will fit or delay in hopes of bundling a full
		 * sized packet.
		 */
		if (len < asoc->frag_point) {
			retval = SCTP_XMIT_NAGLE_DELAY;
			goto finish;
		}
	}

	/* Keep track of how many bytes are in flight over this transport. */
	transport->flight_size += datasize;

	/* Keep track of how many bytes are in flight to the receiver. */
	asoc->outqueue.outstanding_bytes += datasize;

	/* Update our view of the receiver's rwnd. Include sk_buff overhead
	 * while updating peer.rwnd so that it reduces the chances of a
	 * receiver running out of receive buffer space even when receive
	 * window is still open. This can happen when a sender is sending
	 * sending small messages.
	 */
	datasize += sizeof(struct sk_buff);
	if (datasize < rwnd)
		rwnd -= datasize;
	else
		rwnd = 0;

	asoc->peer.rwnd = rwnd;
	/* Has been accepted for transmission. */
	if (!asoc->peer.prsctp_capable)
		chunk->msg->can_abandon = 0;

finish:
	return retval;
}
