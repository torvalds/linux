#ifndef _NF_TPROXY_CORE_H
#define _NF_TPROXY_CORE_H

#include <linux/types.h>
#include <linux/in.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/inet_hashtables.h>
#include <net/inet6_hashtables.h>
#include <net/tcp.h>

#define NFT_LOOKUP_ANY         0
#define NFT_LOOKUP_LISTENER    1
#define NFT_LOOKUP_ESTABLISHED 2

/* look up and get a reference to a matching socket */


/* This function is used by the 'TPROXY' target and the 'socket'
 * match. The following lookups are supported:
 *
 * Explicit TProxy target rule
 * ===========================
 *
 * This is used when the user wants to intercept a connection matching
 * an explicit iptables rule. In this case the sockets are assumed
 * matching in preference order:
 *
 *   - match: if there's a fully established connection matching the
 *     _packet_ tuple, it is returned, assuming the redirection
 *     already took place and we process a packet belonging to an
 *     established connection
 *
 *   - match: if there's a listening socket matching the redirection
 *     (e.g. on-port & on-ip of the connection), it is returned,
 *     regardless if it was bound to 0.0.0.0 or an explicit
 *     address. The reasoning is that if there's an explicit rule, it
 *     does not really matter if the listener is bound to an interface
 *     or to 0. The user already stated that he wants redirection
 *     (since he added the rule).
 *
 * "socket" match based redirection (no specific rule)
 * ===================================================
 *
 * There are connections with dynamic endpoints (e.g. FTP data
 * connection) that the user is unable to add explicit rules
 * for. These are taken care of by a generic "socket" rule. It is
 * assumed that the proxy application is trusted to open such
 * connections without explicit iptables rule (except of course the
 * generic 'socket' rule). In this case the following sockets are
 * matched in preference order:
 *
 *   - match: if there's a fully established connection matching the
 *     _packet_ tuple
 *
 *   - match: if there's a non-zero bound listener (possibly with a
 *     non-local address) We don't accept zero-bound listeners, since
 *     then local services could intercept traffic going through the
 *     box.
 *
 * Please note that there's an overlap between what a TPROXY target
 * and a socket match will match. Normally if you have both rules the
 * "socket" match will be the first one, effectively all packets
 * belonging to established connections going through that one.
 */
static inline struct sock *
nf_tproxy_get_sock_v4(struct net *net, const u8 protocol,
		      const __be32 saddr, const __be32 daddr,
		      const __be16 sport, const __be16 dport,
		      const struct net_device *in, int lookup_type)
{
	struct sock *sk;

	/* look up socket */
	switch (protocol) {
	case IPPROTO_TCP:
		switch (lookup_type) {
		case NFT_LOOKUP_ANY:
			sk = __inet_lookup(net, &tcp_hashinfo,
					   saddr, sport, daddr, dport,
					   in->ifindex);
			break;
		case NFT_LOOKUP_LISTENER:
			sk = inet_lookup_listener(net, &tcp_hashinfo,
						    daddr, dport,
						    in->ifindex);

			/* NOTE: we return listeners even if bound to
			 * 0.0.0.0, those are filtered out in
			 * xt_socket, since xt_TPROXY needs 0 bound
			 * listeners too */

			break;
		case NFT_LOOKUP_ESTABLISHED:
			sk = inet_lookup_established(net, &tcp_hashinfo,
						    saddr, sport, daddr, dport,
						    in->ifindex);
			break;
		default:
			WARN_ON(1);
			sk = NULL;
			break;
		}
		break;
	case IPPROTO_UDP:
		sk = udp4_lib_lookup(net, saddr, sport, daddr, dport,
				     in->ifindex);
		if (sk && lookup_type != NFT_LOOKUP_ANY) {
			int connected = (sk->sk_state == TCP_ESTABLISHED);
			int wildcard = (inet_sk(sk)->inet_rcv_saddr == 0);

			/* NOTE: we return listeners even if bound to
			 * 0.0.0.0, those are filtered out in
			 * xt_socket, since xt_TPROXY needs 0 bound
			 * listeners too */
			if ((lookup_type == NFT_LOOKUP_ESTABLISHED && (!connected || wildcard)) ||
			    (lookup_type == NFT_LOOKUP_LISTENER && connected)) {
				sock_put(sk);
				sk = NULL;
			}
		}
		break;
	default:
		WARN_ON(1);
		sk = NULL;
	}

	pr_debug("tproxy socket lookup: proto %u %08x:%u -> %08x:%u, lookup type: %d, sock %p\n",
		 protocol, ntohl(saddr), ntohs(sport), ntohl(daddr), ntohs(dport), lookup_type, sk);

	return sk;
}

#if IS_ENABLED(CONFIG_IPV6)
static inline struct sock *
nf_tproxy_get_sock_v6(struct net *net, const u8 protocol,
		      const struct in6_addr *saddr, const struct in6_addr *daddr,
		      const __be16 sport, const __be16 dport,
		      const struct net_device *in, int lookup_type)
{
	struct sock *sk;

	/* look up socket */
	switch (protocol) {
	case IPPROTO_TCP:
		switch (lookup_type) {
		case NFT_LOOKUP_ANY:
			sk = inet6_lookup(net, &tcp_hashinfo,
					  saddr, sport, daddr, dport,
					  in->ifindex);
			break;
		case NFT_LOOKUP_LISTENER:
			sk = inet6_lookup_listener(net, &tcp_hashinfo,
						   daddr, ntohs(dport),
						   in->ifindex);

			/* NOTE: we return listeners even if bound to
			 * 0.0.0.0, those are filtered out in
			 * xt_socket, since xt_TPROXY needs 0 bound
			 * listeners too */

			break;
		case NFT_LOOKUP_ESTABLISHED:
			sk = __inet6_lookup_established(net, &tcp_hashinfo,
							saddr, sport, daddr, ntohs(dport),
							in->ifindex);
			break;
		default:
			WARN_ON(1);
			sk = NULL;
			break;
		}
		break;
	case IPPROTO_UDP:
		sk = udp6_lib_lookup(net, saddr, sport, daddr, dport,
				     in->ifindex);
		if (sk && lookup_type != NFT_LOOKUP_ANY) {
			int connected = (sk->sk_state == TCP_ESTABLISHED);
			int wildcard = ipv6_addr_any(&inet6_sk(sk)->rcv_saddr);

			/* NOTE: we return listeners even if bound to
			 * 0.0.0.0, those are filtered out in
			 * xt_socket, since xt_TPROXY needs 0 bound
			 * listeners too */
			if ((lookup_type == NFT_LOOKUP_ESTABLISHED && (!connected || wildcard)) ||
			    (lookup_type == NFT_LOOKUP_LISTENER && connected)) {
				sock_put(sk);
				sk = NULL;
			}
		}
		break;
	default:
		WARN_ON(1);
		sk = NULL;
	}

	pr_debug("tproxy socket lookup: proto %u %pI6:%u -> %pI6:%u, lookup type: %d, sock %p\n",
		 protocol, saddr, ntohs(sport), daddr, ntohs(dport), lookup_type, sk);

	return sk;
}
#endif

/* assign a socket to the skb -- consumes sk */
void
nf_tproxy_assign_sock(struct sk_buff *skb, struct sock *sk);

#endif
