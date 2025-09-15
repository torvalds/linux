
// SPDX-License-Identifier: GPL-2.0-only
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/socket.h>
#include <linux/udp.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/in6.h>
#include <net/udp.h>
#include <net/udp_tunnel.h>
#include <net/net_namespace.h>
#include <net/netns/generic.h>
#include <net/ip6_tunnel.h>
#include <net/ip6_checksum.h>

int udp_sock_create6(struct net *net, struct udp_port_cfg *cfg,
		     struct socket **sockp)
{
	struct sockaddr_in6 udp6_addr = {};
	int err;
	struct socket *sock = NULL;

	err = sock_create_kern(net, AF_INET6, SOCK_DGRAM, 0, &sock);
	if (err < 0)
		goto error;

	if (cfg->ipv6_v6only) {
		err = ip6_sock_set_v6only(sock->sk);
		if (err < 0)
			goto error;
	}
	if (cfg->bind_ifindex) {
		err = sock_bindtoindex(sock->sk, cfg->bind_ifindex, true);
		if (err < 0)
			goto error;
	}

	udp6_addr.sin6_family = AF_INET6;
	memcpy(&udp6_addr.sin6_addr, &cfg->local_ip6,
	       sizeof(udp6_addr.sin6_addr));
	udp6_addr.sin6_port = cfg->local_udp_port;
	err = kernel_bind(sock, (struct sockaddr *)&udp6_addr,
			  sizeof(udp6_addr));
	if (err < 0)
		goto error;

	if (cfg->peer_udp_port) {
		memset(&udp6_addr, 0, sizeof(udp6_addr));
		udp6_addr.sin6_family = AF_INET6;
		memcpy(&udp6_addr.sin6_addr, &cfg->peer_ip6,
		       sizeof(udp6_addr.sin6_addr));
		udp6_addr.sin6_port = cfg->peer_udp_port;
		err = kernel_connect(sock,
				     (struct sockaddr *)&udp6_addr,
				     sizeof(udp6_addr), 0);
	}
	if (err < 0)
		goto error;

	udp_set_no_check6_tx(sock->sk, !cfg->use_udp6_tx_checksums);
	udp_set_no_check6_rx(sock->sk, !cfg->use_udp6_rx_checksums);

	*sockp = sock;
	return 0;

error:
	if (sock) {
		kernel_sock_shutdown(sock, SHUT_RDWR);
		sock_release(sock);
	}
	*sockp = NULL;
	return err;
}
EXPORT_SYMBOL_GPL(udp_sock_create6);

void udp_tunnel6_xmit_skb(struct dst_entry *dst, struct sock *sk,
			  struct sk_buff *skb,
			  struct net_device *dev,
			  const struct in6_addr *saddr,
			  const struct in6_addr *daddr,
			  __u8 prio, __u8 ttl, __be32 label,
			  __be16 src_port, __be16 dst_port, bool nocheck,
			  u16 ip6cb_flags)
{
	struct udphdr *uh;
	struct ipv6hdr *ip6h;

	__skb_push(skb, sizeof(*uh));
	skb_reset_transport_header(skb);
	uh = udp_hdr(skb);

	uh->dest = dst_port;
	uh->source = src_port;

	uh->len = htons(skb->len);

	skb_dst_set(skb, dst);

	udp6_set_csum(nocheck, skb, saddr, daddr, skb->len);

	__skb_push(skb, sizeof(*ip6h));
	skb_reset_network_header(skb);
	ip6h		  = ipv6_hdr(skb);
	ip6_flow_hdr(ip6h, prio, label);
	ip6h->payload_len = htons(skb->len);
	ip6h->nexthdr     = IPPROTO_UDP;
	ip6h->hop_limit   = ttl;
	ip6h->daddr	  = *daddr;
	ip6h->saddr	  = *saddr;

	ip6tunnel_xmit(sk, skb, dev, ip6cb_flags);
}
EXPORT_SYMBOL_GPL(udp_tunnel6_xmit_skb);

/**
 *      udp_tunnel6_dst_lookup - perform route lookup on UDP tunnel
 *      @skb: Packet for which lookup is done
 *      @dev: Tunnel device
 *      @net: Network namespace of tunnel device
 *      @sock: Socket which provides route info
 *      @oif: Index of the output interface
 *      @saddr: Memory to store the src ip address
 *      @key: Tunnel information
 *      @sport: UDP source port
 *      @dport: UDP destination port
 *      @dsfield: The traffic class field
 *      @dst_cache: The dst cache to use for lookup
 *      This function performs a route lookup on a UDP tunnel
 *
 *      It returns a valid dst pointer and stores src address to be used in
 *      tunnel in param saddr on success, else a pointer encoded error code.
 */

struct dst_entry *udp_tunnel6_dst_lookup(struct sk_buff *skb,
					 struct net_device *dev,
					 struct net *net,
					 struct socket *sock,
					 int oif,
					 struct in6_addr *saddr,
					 const struct ip_tunnel_key *key,
					 __be16 sport, __be16 dport, u8 dsfield,
					 struct dst_cache *dst_cache)
{
	struct dst_entry *dst = NULL;
	struct flowi6 fl6;

#ifdef CONFIG_DST_CACHE
	if (dst_cache) {
		dst = dst_cache_get_ip6(dst_cache, saddr);
		if (dst)
			return dst;
	}
#endif
	memset(&fl6, 0, sizeof(fl6));
	fl6.flowi6_mark = skb->mark;
	fl6.flowi6_proto = IPPROTO_UDP;
	fl6.flowi6_oif = oif;
	fl6.daddr = key->u.ipv6.dst;
	fl6.saddr = key->u.ipv6.src;
	fl6.fl6_sport = sport;
	fl6.fl6_dport = dport;
	fl6.flowlabel = ip6_make_flowinfo(dsfield, key->label);

	dst = ipv6_stub->ipv6_dst_lookup_flow(net, sock->sk, &fl6,
					      NULL);
	if (IS_ERR(dst)) {
		netdev_dbg(dev, "no route to %pI6\n", &fl6.daddr);
		return ERR_PTR(-ENETUNREACH);
	}
	if (dst_dev(dst) == dev) { /* is this necessary? */
		netdev_dbg(dev, "circular route to %pI6\n", &fl6.daddr);
		dst_release(dst);
		return ERR_PTR(-ELOOP);
	}
#ifdef CONFIG_DST_CACHE
	if (dst_cache)
		dst_cache_set_ip6(dst_cache, dst, &fl6.saddr);
#endif
	*saddr = fl6.saddr;
	return dst;
}
EXPORT_SYMBOL_GPL(udp_tunnel6_dst_lookup);

MODULE_DESCRIPTION("IPv6 Foo over UDP tunnel driver");
MODULE_LICENSE("GPL");
