#ifndef __NET_UDP_TUNNEL_H
#define __NET_UDP_TUNNEL_H

#include <net/ip_tunnels.h>
#include <net/udp.h>

#if IS_ENABLED(CONFIG_IPV6)
#include <net/ipv6.h>
#include <net/addrconf.h>
#endif

struct udp_port_cfg {
	u8			family;

	/* Used only for kernel-created sockets */
	union {
		struct in_addr		local_ip;
#if IS_ENABLED(CONFIG_IPV6)
		struct in6_addr		local_ip6;
#endif
	};

	union {
		struct in_addr		peer_ip;
#if IS_ENABLED(CONFIG_IPV6)
		struct in6_addr		peer_ip6;
#endif
	};

	__be16			local_udp_port;
	__be16			peer_udp_port;
	unsigned int		use_udp_checksums:1,
				use_udp6_tx_checksums:1,
				use_udp6_rx_checksums:1;
};

int udp_sock_create4(struct net *net, struct udp_port_cfg *cfg,
		     struct socket **sockp);

#if IS_ENABLED(CONFIG_IPV6)
int udp_sock_create6(struct net *net, struct udp_port_cfg *cfg,
		     struct socket **sockp);
#else
static inline int udp_sock_create6(struct net *net, struct udp_port_cfg *cfg,
				   struct socket **sockp)
{
	return 0;
}
#endif

static inline int udp_sock_create(struct net *net,
				  struct udp_port_cfg *cfg,
				  struct socket **sockp)
{
	if (cfg->family == AF_INET)
		return udp_sock_create4(net, cfg, sockp);

	if (cfg->family == AF_INET6)
		return udp_sock_create6(net, cfg, sockp);

	return -EPFNOSUPPORT;
}

typedef int (*udp_tunnel_encap_rcv_t)(struct sock *sk, struct sk_buff *skb);
typedef void (*udp_tunnel_encap_destroy_t)(struct sock *sk);

struct udp_tunnel_sock_cfg {
	void *sk_user_data;     /* user data used by encap_rcv call back */
	/* Used for setting up udp_sock fields, see udp.h for details */
	__u8  encap_type;
	udp_tunnel_encap_rcv_t encap_rcv;
	udp_tunnel_encap_destroy_t encap_destroy;
};

/* Setup the given (UDP) sock to receive UDP encapsulated packets */
void setup_udp_tunnel_sock(struct net *net, struct socket *sock,
			   struct udp_tunnel_sock_cfg *sock_cfg);

/* Transmit the skb using UDP encapsulation. */
int udp_tunnel_xmit_skb(struct rtable *rt, struct sk_buff *skb,
			__be32 src, __be32 dst, __u8 tos, __u8 ttl,
			__be16 df, __be16 src_port, __be16 dst_port,
			bool xnet, bool nocheck);

#if IS_ENABLED(CONFIG_IPV6)
int udp_tunnel6_xmit_skb(struct dst_entry *dst, struct sk_buff *skb,
			 struct net_device *dev, struct in6_addr *saddr,
			 struct in6_addr *daddr,
			 __u8 prio, __u8 ttl, __be16 src_port,
			 __be16 dst_port, bool nocheck);
#endif

void udp_tunnel_sock_release(struct socket *sock);

static inline struct sk_buff *udp_tunnel_handle_offloads(struct sk_buff *skb,
							 bool udp_csum)
{
	int type = udp_csum ? SKB_GSO_UDP_TUNNEL_CSUM : SKB_GSO_UDP_TUNNEL;

	return iptunnel_handle_offloads(skb, udp_csum, type);
}

static inline void udp_tunnel_gro_complete(struct sk_buff *skb, int nhoff)
{
	struct udphdr *uh;

	uh = (struct udphdr *)(skb->data + nhoff - sizeof(struct udphdr));
	skb_shinfo(skb)->gso_type |= uh->check ?
				SKB_GSO_UDP_TUNNEL_CSUM : SKB_GSO_UDP_TUNNEL;
}

static inline void udp_tunnel_encap_enable(struct socket *sock)
{
#if IS_ENABLED(CONFIG_IPV6)
	if (sock->sk->sk_family == PF_INET6)
		ipv6_stub->udpv6_encap_enable();
	else
#endif
		udp_encap_enable();
}

#endif
