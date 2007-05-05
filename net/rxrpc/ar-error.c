/* Error message handling (ICMP)
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
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
#include "ar-internal.h"

/*
 * handle an error received on the local endpoint
 */
void rxrpc_UDP_error_report(struct sock *sk)
{
	struct sock_exterr_skb *serr;
	struct rxrpc_transport *trans;
	struct rxrpc_local *local = sk->sk_user_data;
	struct rxrpc_peer *peer;
	struct sk_buff *skb;
	__be32 addr;
	__be16 port;

	_enter("%p{%d}", sk, local->debug_id);

	skb = skb_dequeue(&sk->sk_error_queue);
	if (!skb) {
		_leave("UDP socket errqueue empty");
		return;
	}

	rxrpc_new_skb(skb);

	serr = SKB_EXT_ERR(skb);
	addr = *(__be32 *)(skb_network_header(skb) + serr->addr_offset);
	port = serr->port;

	_net("Rx UDP Error from "NIPQUAD_FMT":%hu",
	     NIPQUAD(addr), ntohs(port));
	_debug("Msg l:%d d:%d", skb->len, skb->data_len);

	peer = rxrpc_find_peer(local, addr, port);
	if (IS_ERR(peer)) {
		rxrpc_free_skb(skb);
		_leave(" [no peer]");
		return;
	}

	trans = rxrpc_find_transport(local, peer);
	if (!trans) {
		rxrpc_put_peer(peer);
		rxrpc_free_skb(skb);
		_leave(" [no trans]");
		return;
	}

	if (serr->ee.ee_origin == SO_EE_ORIGIN_ICMP &&
	    serr->ee.ee_type == ICMP_DEST_UNREACH &&
	    serr->ee.ee_code == ICMP_FRAG_NEEDED
	    ) {
		u32 mtu = serr->ee.ee_info;

		_net("Rx Received ICMP Fragmentation Needed (%d)", mtu);

		/* wind down the local interface MTU */
		if (mtu > 0 && peer->if_mtu == 65535 && mtu < peer->if_mtu) {
			peer->if_mtu = mtu;
			_net("I/F MTU %u", mtu);
		}

		/* ip_rt_frag_needed() may have eaten the info */
		if (mtu == 0)
			mtu = ntohs(icmp_hdr(skb)->un.frag.mtu);

		if (mtu == 0) {
			/* they didn't give us a size, estimate one */
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

	rxrpc_put_peer(peer);

	/* pass the transport ref to error_handler to release */
	skb_queue_tail(&trans->error_queue, skb);
	rxrpc_queue_work(&trans->error_handler);

	/* reset and regenerate socket error */
	spin_lock_bh(&sk->sk_error_queue.lock);
	sk->sk_err = 0;
	skb = skb_peek(&sk->sk_error_queue);
	if (skb) {
		sk->sk_err = SKB_EXT_ERR(skb)->ee.ee_errno;
		spin_unlock_bh(&sk->sk_error_queue.lock);
		sk->sk_error_report(sk);
	} else {
		spin_unlock_bh(&sk->sk_error_queue.lock);
	}

	_leave("");
}

/*
 * deal with UDP error messages
 */
void rxrpc_UDP_error_handler(struct work_struct *work)
{
	struct sock_extended_err *ee;
	struct sock_exterr_skb *serr;
	struct rxrpc_transport *trans =
		container_of(work, struct rxrpc_transport, error_handler);
	struct sk_buff *skb;
	int local, err;

	_enter("");

	skb = skb_dequeue(&trans->error_queue);
	if (!skb)
		return;

	serr = SKB_EXT_ERR(skb);
	ee = &serr->ee;

	_net("Rx Error o=%d t=%d c=%d e=%d",
	     ee->ee_origin, ee->ee_type, ee->ee_code, ee->ee_errno);

	err = ee->ee_errno;

	switch (ee->ee_origin) {
	case SO_EE_ORIGIN_ICMP:
		local = 0;
		switch (ee->ee_type) {
		case ICMP_DEST_UNREACH:
			switch (ee->ee_code) {
			case ICMP_NET_UNREACH:
				_net("Rx Received ICMP Network Unreachable");
				err = ENETUNREACH;
				break;
			case ICMP_HOST_UNREACH:
				_net("Rx Received ICMP Host Unreachable");
				err = EHOSTUNREACH;
				break;
			case ICMP_PORT_UNREACH:
				_net("Rx Received ICMP Port Unreachable");
				err = ECONNREFUSED;
				break;
			case ICMP_FRAG_NEEDED:
				_net("Rx Received ICMP Fragmentation Needed (%d)",
				     ee->ee_info);
				err = 0; /* dealt with elsewhere */
				break;
			case ICMP_NET_UNKNOWN:
				_net("Rx Received ICMP Unknown Network");
				err = ENETUNREACH;
				break;
			case ICMP_HOST_UNKNOWN:
				_net("Rx Received ICMP Unknown Host");
				err = EHOSTUNREACH;
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

	case SO_EE_ORIGIN_LOCAL:
		_proto("Rx Received local error { error=%d }",
		       ee->ee_errno);
		local = 1;
		break;

	case SO_EE_ORIGIN_NONE:
	case SO_EE_ORIGIN_ICMP6:
	default:
		_proto("Rx Received error report { orig=%u }",
		       ee->ee_origin);
		local = 0;
		break;
	}

	/* terminate all the affected calls if there's an unrecoverable
	 * error */
	if (err) {
		struct rxrpc_call *call, *_n;

		_debug("ISSUE ERROR %d", err);

		spin_lock_bh(&trans->peer->lock);
		trans->peer->net_error = err;

		list_for_each_entry_safe(call, _n, &trans->peer->error_targets,
					 error_link) {
			write_lock(&call->state_lock);
			if (call->state != RXRPC_CALL_COMPLETE &&
			    call->state < RXRPC_CALL_NETWORK_ERROR) {
				call->state = RXRPC_CALL_NETWORK_ERROR;
				set_bit(RXRPC_CALL_RCVD_ERROR, &call->events);
				rxrpc_queue_call(call);
			}
			write_unlock(&call->state_lock);
			list_del_init(&call->error_link);
		}

		spin_unlock_bh(&trans->peer->lock);
	}

	if (!skb_queue_empty(&trans->error_queue))
		rxrpc_queue_work(&trans->error_handler);

	rxrpc_free_skb(skb);
	rxrpc_put_transport(trans);
	_leave("");
}
