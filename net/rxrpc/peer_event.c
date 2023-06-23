// SPDX-License-Identifier: GPL-2.0-or-later
/* Peer event handling, typically ICMP messages.
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/module.h>
#include <linux/net.h>
#include <linux/skbuff.h>
#include <linux/errqueue.h>
#include <linux/udp.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <linux/icmp.h>
#include <net/sock.h>
#include <net/af_rxrpc.h>
#include <net/ip.h>
#include <net/icmp.h>
#include "ar-internal.h"

static void rxrpc_adjust_mtu(struct rxrpc_peer *, unsigned int);
static void rxrpc_store_error(struct rxrpc_peer *, struct sock_exterr_skb *);
static void rxrpc_distribute_error(struct rxrpc_peer *, int,
				   enum rxrpc_call_completion);

/*
 * Find the peer associated with an ICMPv4 packet.
 */
static struct rxrpc_peer *rxrpc_lookup_peer_icmp_rcu(struct rxrpc_local *local,
						     struct sk_buff *skb,
						     unsigned int udp_offset,
						     unsigned int *info,
						     struct sockaddr_rxrpc *srx)
{
	struct iphdr *ip, *ip0 = ip_hdr(skb);
	struct icmphdr *icmp = icmp_hdr(skb);
	struct udphdr *udp = (struct udphdr *)(skb->data + udp_offset);

	_enter("%u,%u,%u", ip0->protocol, icmp->type, icmp->code);

	switch (icmp->type) {
	case ICMP_DEST_UNREACH:
		*info = ntohs(icmp->un.frag.mtu);
		fallthrough;
	case ICMP_TIME_EXCEEDED:
	case ICMP_PARAMETERPROB:
		ip = (struct iphdr *)((void *)icmp + 8);
		break;
	default:
		return NULL;
	}

	memset(srx, 0, sizeof(*srx));
	srx->transport_type = local->srx.transport_type;
	srx->transport_len = local->srx.transport_len;
	srx->transport.family = local->srx.transport.family;

	/* Can we see an ICMP4 packet on an ICMP6 listening socket?  and vice
	 * versa?
	 */
	switch (srx->transport.family) {
	case AF_INET:
		srx->transport_len = sizeof(srx->transport.sin);
		srx->transport.family = AF_INET;
		srx->transport.sin.sin_port = udp->dest;
		memcpy(&srx->transport.sin.sin_addr, &ip->daddr,
		       sizeof(struct in_addr));
		break;

#ifdef CONFIG_AF_RXRPC_IPV6
	case AF_INET6:
		srx->transport_len = sizeof(srx->transport.sin);
		srx->transport.family = AF_INET;
		srx->transport.sin.sin_port = udp->dest;
		memcpy(&srx->transport.sin.sin_addr, &ip->daddr,
		       sizeof(struct in_addr));
		break;
#endif

	default:
		WARN_ON_ONCE(1);
		return NULL;
	}

	_net("ICMP {%pISp}", &srx->transport);
	return rxrpc_lookup_peer_rcu(local, srx);
}

#ifdef CONFIG_AF_RXRPC_IPV6
/*
 * Find the peer associated with an ICMPv6 packet.
 */
static struct rxrpc_peer *rxrpc_lookup_peer_icmp6_rcu(struct rxrpc_local *local,
						      struct sk_buff *skb,
						      unsigned int udp_offset,
						      unsigned int *info,
						      struct sockaddr_rxrpc *srx)
{
	struct icmp6hdr *icmp = icmp6_hdr(skb);
	struct ipv6hdr *ip, *ip0 = ipv6_hdr(skb);
	struct udphdr *udp = (struct udphdr *)(skb->data + udp_offset);

	_enter("%u,%u,%u", ip0->nexthdr, icmp->icmp6_type, icmp->icmp6_code);

	switch (icmp->icmp6_type) {
	case ICMPV6_DEST_UNREACH:
		*info = ntohl(icmp->icmp6_mtu);
		fallthrough;
	case ICMPV6_PKT_TOOBIG:
	case ICMPV6_TIME_EXCEED:
	case ICMPV6_PARAMPROB:
		ip = (struct ipv6hdr *)((void *)icmp + 8);
		break;
	default:
		return NULL;
	}

	memset(srx, 0, sizeof(*srx));
	srx->transport_type = local->srx.transport_type;
	srx->transport_len = local->srx.transport_len;
	srx->transport.family = local->srx.transport.family;

	/* Can we see an ICMP4 packet on an ICMP6 listening socket?  and vice
	 * versa?
	 */
	switch (srx->transport.family) {
	case AF_INET:
		_net("Rx ICMP6 on v4 sock");
		srx->transport_len = sizeof(srx->transport.sin);
		srx->transport.family = AF_INET;
		srx->transport.sin.sin_port = udp->dest;
		memcpy(&srx->transport.sin.sin_addr,
		       &ip->daddr.s6_addr32[3], sizeof(struct in_addr));
		break;
	case AF_INET6:
		_net("Rx ICMP6");
		srx->transport.sin.sin_port = udp->dest;
		memcpy(&srx->transport.sin6.sin6_addr, &ip->daddr,
		       sizeof(struct in6_addr));
		break;
	default:
		WARN_ON_ONCE(1);
		return NULL;
	}

	_net("ICMP {%pISp}", &srx->transport);
	return rxrpc_lookup_peer_rcu(local, srx);
}
#endif /* CONFIG_AF_RXRPC_IPV6 */

/*
 * Handle an error received on the local endpoint as a tunnel.
 */
void rxrpc_encap_err_rcv(struct sock *sk, struct sk_buff *skb,
			 unsigned int udp_offset)
{
	struct sock_extended_err ee;
	struct sockaddr_rxrpc srx;
	struct rxrpc_local *local;
	struct rxrpc_peer *peer;
	unsigned int info = 0;
	int err;
	u8 version = ip_hdr(skb)->version;
	u8 type = icmp_hdr(skb)->type;
	u8 code = icmp_hdr(skb)->code;

	rcu_read_lock();
	local = rcu_dereference_sk_user_data(sk);
	if (unlikely(!local)) {
		rcu_read_unlock();
		return;
	}

	rxrpc_new_skb(skb, rxrpc_skb_received);

	switch (ip_hdr(skb)->version) {
	case IPVERSION:
		peer = rxrpc_lookup_peer_icmp_rcu(local, skb, udp_offset,
						  &info, &srx);
		break;
#ifdef CONFIG_AF_RXRPC_IPV6
	case 6:
		peer = rxrpc_lookup_peer_icmp6_rcu(local, skb, udp_offset,
						   &info, &srx);
		break;
#endif
	default:
		rcu_read_unlock();
		return;
	}

	if (peer && !rxrpc_get_peer_maybe(peer))
		peer = NULL;
	if (!peer) {
		rcu_read_unlock();
		return;
	}

	memset(&ee, 0, sizeof(ee));

	switch (version) {
	case IPVERSION:
		switch (type) {
		case ICMP_DEST_UNREACH:
			switch (code) {
			case ICMP_FRAG_NEEDED:
				rxrpc_adjust_mtu(peer, info);
				rcu_read_unlock();
				rxrpc_put_peer(peer);
				return;
			default:
				break;
			}

			err = EHOSTUNREACH;
			if (code <= NR_ICMP_UNREACH) {
				/* Might want to do something different with
				 * non-fatal errors
				 */
				//harderr = icmp_err_convert[code].fatal;
				err = icmp_err_convert[code].errno;
			}
			break;

		case ICMP_TIME_EXCEEDED:
			err = EHOSTUNREACH;
			break;
		default:
			err = EPROTO;
			break;
		}

		ee.ee_origin = SO_EE_ORIGIN_ICMP;
		ee.ee_type = type;
		ee.ee_code = code;
		ee.ee_errno = err;
		break;

#ifdef CONFIG_AF_RXRPC_IPV6
	case 6:
		switch (type) {
		case ICMPV6_PKT_TOOBIG:
			rxrpc_adjust_mtu(peer, info);
			rcu_read_unlock();
			rxrpc_put_peer(peer);
			return;
		}

		icmpv6_err_convert(type, code, &err);

		if (err == EACCES)
			err = EHOSTUNREACH;

		ee.ee_origin = SO_EE_ORIGIN_ICMP6;
		ee.ee_type = type;
		ee.ee_code = code;
		ee.ee_errno = err;
		break;
#endif
	}

	trace_rxrpc_rx_icmp(peer, &ee, &srx);

	rxrpc_distribute_error(peer, err, RXRPC_CALL_NETWORK_ERROR);
	rcu_read_unlock();
	rxrpc_put_peer(peer);
}

/*
 * Find the peer associated with a local error.
 */
static struct rxrpc_peer *rxrpc_lookup_peer_local_rcu(struct rxrpc_local *local,
						      const struct sk_buff *skb,
						      struct sockaddr_rxrpc *srx)
{
	struct sock_exterr_skb *serr = SKB_EXT_ERR(skb);

	_enter("");

	memset(srx, 0, sizeof(*srx));
	srx->transport_type = local->srx.transport_type;
	srx->transport_len = local->srx.transport_len;
	srx->transport.family = local->srx.transport.family;

	switch (srx->transport.family) {
	case AF_INET:
		srx->transport_len = sizeof(srx->transport.sin);
		srx->transport.family = AF_INET;
		srx->transport.sin.sin_port = serr->port;
		switch (serr->ee.ee_origin) {
		case SO_EE_ORIGIN_ICMP:
			_net("Rx ICMP");
			memcpy(&srx->transport.sin.sin_addr,
			       skb_network_header(skb) + serr->addr_offset,
			       sizeof(struct in_addr));
			break;
		case SO_EE_ORIGIN_ICMP6:
			_net("Rx ICMP6 on v4 sock");
			memcpy(&srx->transport.sin.sin_addr,
			       skb_network_header(skb) + serr->addr_offset + 12,
			       sizeof(struct in_addr));
			break;
		default:
			memcpy(&srx->transport.sin.sin_addr, &ip_hdr(skb)->saddr,
			       sizeof(struct in_addr));
			break;
		}
		break;

#ifdef CONFIG_AF_RXRPC_IPV6
	case AF_INET6:
		switch (serr->ee.ee_origin) {
		case SO_EE_ORIGIN_ICMP6:
			_net("Rx ICMP6");
			srx->transport.sin6.sin6_port = serr->port;
			memcpy(&srx->transport.sin6.sin6_addr,
			       skb_network_header(skb) + serr->addr_offset,
			       sizeof(struct in6_addr));
			break;
		case SO_EE_ORIGIN_ICMP:
			_net("Rx ICMP on v6 sock");
			srx->transport_len = sizeof(srx->transport.sin);
			srx->transport.family = AF_INET;
			srx->transport.sin.sin_port = serr->port;
			memcpy(&srx->transport.sin.sin_addr,
			       skb_network_header(skb) + serr->addr_offset,
			       sizeof(struct in_addr));
			break;
		default:
			memcpy(&srx->transport.sin6.sin6_addr,
			       &ipv6_hdr(skb)->saddr,
			       sizeof(struct in6_addr));
			break;
		}
		break;
#endif

	default:
		BUG();
	}

	return rxrpc_lookup_peer_rcu(local, srx);
}

/*
 * Handle an MTU/fragmentation problem.
 */
static void rxrpc_adjust_mtu(struct rxrpc_peer *peer, unsigned int mtu)
{
	_net("Rx ICMP Fragmentation Needed (%d)", mtu);

	/* wind down the local interface MTU */
	if (mtu > 0 && peer->if_mtu == 65535 && mtu < peer->if_mtu) {
		peer->if_mtu = mtu;
		_net("I/F MTU %u", mtu);
	}

	if (mtu == 0) {
		/* they didn't give us a size, estimate one */
		mtu = peer->if_mtu;
		if (mtu > 1500) {
			mtu >>= 1;
			if (mtu < 1500)
				mtu = 1500;
		} else {
			mtu -= 100;
			if (mtu < peer->hdrsize)
				mtu = peer->hdrsize + 4;
		}
	}

	if (mtu < peer->mtu) {
		spin_lock_bh(&peer->lock);
		peer->mtu = mtu;
		peer->maxdata = peer->mtu - peer->hdrsize;
		spin_unlock_bh(&peer->lock);
		_net("Net MTU %u (maxdata %u)",
		     peer->mtu, peer->maxdata);
	}
}

/*
 * Handle an error received on the local endpoint.
 */
void rxrpc_error_report(struct sock *sk)
{
	struct sock_exterr_skb *serr;
	struct sockaddr_rxrpc srx;
	struct rxrpc_local *local;
	struct rxrpc_peer *peer = NULL;
	struct sk_buff *skb;

	rcu_read_lock();
	local = rcu_dereference_sk_user_data(sk);
	if (unlikely(!local)) {
		rcu_read_unlock();
		return;
	}
	_enter("%p{%d}", sk, local->debug_id);

	/* Clear the outstanding error value on the socket so that it doesn't
	 * cause kernel_sendmsg() to return it later.
	 */
	sock_error(sk);

	skb = sock_dequeue_err_skb(sk);
	if (!skb) {
		rcu_read_unlock();
		_leave("UDP socket errqueue empty");
		return;
	}
	rxrpc_new_skb(skb, rxrpc_skb_received);
	serr = SKB_EXT_ERR(skb);

	if (serr->ee.ee_origin == SO_EE_ORIGIN_LOCAL) {
		peer = rxrpc_lookup_peer_local_rcu(local, skb, &srx);
		if (peer && !rxrpc_get_peer_maybe(peer))
			peer = NULL;
		if (peer) {
			trace_rxrpc_rx_icmp(peer, &serr->ee, &srx);
			rxrpc_store_error(peer, serr);
		}
	}

	rcu_read_unlock();
	rxrpc_free_skb(skb, rxrpc_skb_freed);
	rxrpc_put_peer(peer);
	_leave("");
}

/*
 * Map an error report to error codes on the peer record.
 */
static void rxrpc_store_error(struct rxrpc_peer *peer,
			      struct sock_exterr_skb *serr)
{
	enum rxrpc_call_completion compl = RXRPC_CALL_NETWORK_ERROR;
	struct sock_extended_err *ee;
	int err;

	_enter("");

	ee = &serr->ee;

	err = ee->ee_errno;

	switch (ee->ee_origin) {
	case SO_EE_ORIGIN_ICMP:
		switch (ee->ee_type) {
		case ICMP_DEST_UNREACH:
			switch (ee->ee_code) {
			case ICMP_NET_UNREACH:
				_net("Rx Received ICMP Network Unreachable");
				break;
			case ICMP_HOST_UNREACH:
				_net("Rx Received ICMP Host Unreachable");
				break;
			case ICMP_PORT_UNREACH:
				_net("Rx Received ICMP Port Unreachable");
				break;
			case ICMP_NET_UNKNOWN:
				_net("Rx Received ICMP Unknown Network");
				break;
			case ICMP_HOST_UNKNOWN:
				_net("Rx Received ICMP Unknown Host");
				break;
			default:
				_net("Rx Received ICMP DestUnreach code=%u",
				     ee->ee_code);
				break;
			}
			break;

		case ICMP_TIME_EXCEEDED:
			_net("Rx Received ICMP TTL Exceeded");
			break;

		default:
			_proto("Rx Received ICMP error { type=%u code=%u }",
			       ee->ee_type, ee->ee_code);
			break;
		}
		break;

	case SO_EE_ORIGIN_NONE:
	case SO_EE_ORIGIN_LOCAL:
		_proto("Rx Received local error { error=%d }", err);
		compl = RXRPC_CALL_LOCAL_ERROR;
		break;

	case SO_EE_ORIGIN_ICMP6:
		if (err == EACCES)
			err = EHOSTUNREACH;
		fallthrough;
	default:
		_proto("Rx Received error report { orig=%u }", ee->ee_origin);
		break;
	}

	rxrpc_distribute_error(peer, err, compl);
}

/*
 * Distribute an error that occurred on a peer.
 */
static void rxrpc_distribute_error(struct rxrpc_peer *peer, int error,
				   enum rxrpc_call_completion compl)
{
	struct rxrpc_call *call;

	hlist_for_each_entry_rcu(call, &peer->error_targets, error_link) {
		rxrpc_see_call(call);
		rxrpc_set_call_completion(call, compl, 0, -error);
	}
}

/*
 * Perform keep-alive pings.
 */
static void rxrpc_peer_keepalive_dispatch(struct rxrpc_net *rxnet,
					  struct list_head *collector,
					  time64_t base,
					  u8 cursor)
{
	struct rxrpc_peer *peer;
	const u8 mask = ARRAY_SIZE(rxnet->peer_keepalive) - 1;
	time64_t keepalive_at;
	int slot;

	spin_lock_bh(&rxnet->peer_hash_lock);

	while (!list_empty(collector)) {
		peer = list_entry(collector->next,
				  struct rxrpc_peer, keepalive_link);

		list_del_init(&peer->keepalive_link);
		if (!rxrpc_get_peer_maybe(peer))
			continue;

		if (__rxrpc_use_local(peer->local)) {
			spin_unlock_bh(&rxnet->peer_hash_lock);

			keepalive_at = peer->last_tx_at + RXRPC_KEEPALIVE_TIME;
			slot = keepalive_at - base;
			_debug("%02x peer %u t=%d {%pISp}",
			       cursor, peer->debug_id, slot, &peer->srx.transport);

			if (keepalive_at <= base ||
			    keepalive_at > base + RXRPC_KEEPALIVE_TIME) {
				rxrpc_send_keepalive(peer);
				slot = RXRPC_KEEPALIVE_TIME;
			}

			/* A transmission to this peer occurred since last we
			 * examined it so put it into the appropriate future
			 * bucket.
			 */
			slot += cursor;
			slot &= mask;
			spin_lock_bh(&rxnet->peer_hash_lock);
			list_add_tail(&peer->keepalive_link,
				      &rxnet->peer_keepalive[slot & mask]);
			rxrpc_unuse_local(peer->local);
		}
		rxrpc_put_peer_locked(peer);
	}

	spin_unlock_bh(&rxnet->peer_hash_lock);
}

/*
 * Perform keep-alive pings with VERSION packets to keep any NAT alive.
 */
void rxrpc_peer_keepalive_worker(struct work_struct *work)
{
	struct rxrpc_net *rxnet =
		container_of(work, struct rxrpc_net, peer_keepalive_work);
	const u8 mask = ARRAY_SIZE(rxnet->peer_keepalive) - 1;
	time64_t base, now, delay;
	u8 cursor, stop;
	LIST_HEAD(collector);

	now = ktime_get_seconds();
	base = rxnet->peer_keepalive_base;
	cursor = rxnet->peer_keepalive_cursor;
	_enter("%lld,%u", base - now, cursor);

	if (!rxnet->live)
		return;

	/* Remove to a temporary list all the peers that are currently lodged
	 * in expired buckets plus all new peers.
	 *
	 * Everything in the bucket at the cursor is processed this
	 * second; the bucket at cursor + 1 goes at now + 1s and so
	 * on...
	 */
	spin_lock_bh(&rxnet->peer_hash_lock);
	list_splice_init(&rxnet->peer_keepalive_new, &collector);

	stop = cursor + ARRAY_SIZE(rxnet->peer_keepalive);
	while (base <= now && (s8)(cursor - stop) < 0) {
		list_splice_tail_init(&rxnet->peer_keepalive[cursor & mask],
				      &collector);
		base++;
		cursor++;
	}

	base = now;
	spin_unlock_bh(&rxnet->peer_hash_lock);

	rxnet->peer_keepalive_base = base;
	rxnet->peer_keepalive_cursor = cursor;
	rxrpc_peer_keepalive_dispatch(rxnet, &collector, base, cursor);
	ASSERT(list_empty(&collector));

	/* Schedule the timer for the next occupied timeslot. */
	cursor = rxnet->peer_keepalive_cursor;
	stop = cursor + RXRPC_KEEPALIVE_TIME - 1;
	for (; (s8)(cursor - stop) < 0; cursor++) {
		if (!list_empty(&rxnet->peer_keepalive[cursor & mask]))
			break;
		base++;
	}

	now = ktime_get_seconds();
	delay = base - now;
	if (delay < 1)
		delay = 1;
	delay *= HZ;
	if (rxnet->live)
		timer_reduce(&rxnet->peer_keepalive_timer, jiffies + delay);

	_leave("");
}
