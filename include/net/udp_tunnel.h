#ifndef __NET_UDP_TUNNEL_H
#define __NET_UDP_TUNNEL_H

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

#endif
