/* SPDX-License-Identifier: GPL-2.0 */
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
	int			bind_ifindex;
	unsigned int		use_udp_checksums:1,
				use_udp6_tx_checksums:1,
				use_udp6_rx_checksums:1,
				ipv6_v6only:1;
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
typedef int (*udp_tunnel_encap_err_lookup_t)(struct sock *sk,
					     struct sk_buff *skb);
typedef void (*udp_tunnel_encap_destroy_t)(struct sock *sk);
typedef struct sk_buff *(*udp_tunnel_gro_receive_t)(struct sock *sk,
						    struct list_head *head,
						    struct sk_buff *skb);
typedef int (*udp_tunnel_gro_complete_t)(struct sock *sk, struct sk_buff *skb,
					 int nhoff);

struct udp_tunnel_sock_cfg {
	void *sk_user_data;     /* user data used by encap_rcv call back */
	/* Used for setting up udp_sock fields, see udp.h for details */
	__u8  encap_type;
	udp_tunnel_encap_rcv_t encap_rcv;
	udp_tunnel_encap_err_lookup_t encap_err_lookup;
	udp_tunnel_encap_destroy_t encap_destroy;
	udp_tunnel_gro_receive_t gro_receive;
	udp_tunnel_gro_complete_t gro_complete;
};

/* Setup the given (UDP) sock to receive UDP encapsulated packets */
void setup_udp_tunnel_sock(struct net *net, struct socket *sock,
			   struct udp_tunnel_sock_cfg *sock_cfg);

/* -- List of parsable UDP tunnel types --
 *
 * Adding to this list will result in serious debate.  The main issue is
 * that this list is essentially a list of workarounds for either poorly
 * designed tunnels, or poorly designed device offloads.
 *
 * The parsing supported via these types should really be used for Rx
 * traffic only as the network stack will have already inserted offsets for
 * the location of the headers in the skb.  In addition any ports that are
 * pushed should be kept within the namespace without leaking to other
 * devices such as VFs or other ports on the same device.
 *
 * It is strongly encouraged to use CHECKSUM_COMPLETE for Rx to avoid the
 * need to use this for Rx checksum offload.  It should not be necessary to
 * call this function to perform Tx offloads on outgoing traffic.
 */
enum udp_parsable_tunnel_type {
	UDP_TUNNEL_TYPE_VXLAN,		/* RFC 7348 */
	UDP_TUNNEL_TYPE_GENEVE,		/* draft-ietf-nvo3-geneve */
	UDP_TUNNEL_TYPE_VXLAN_GPE,	/* draft-ietf-nvo3-vxlan-gpe */
};

struct udp_tunnel_info {
	unsigned short type;
	sa_family_t sa_family;
	__be16 port;
};

/* Notify network devices of offloadable types */
void udp_tunnel_push_rx_port(struct net_device *dev, struct socket *sock,
			     unsigned short type);
void udp_tunnel_drop_rx_port(struct net_device *dev, struct socket *sock,
			     unsigned short type);
void udp_tunnel_notify_add_rx_port(struct socket *sock, unsigned short type);
void udp_tunnel_notify_del_rx_port(struct socket *sock, unsigned short type);

static inline void udp_tunnel_get_rx_info(struct net_device *dev)
{
	ASSERT_RTNL();
	call_netdevice_notifiers(NETDEV_UDP_TUNNEL_PUSH_INFO, dev);
}

static inline void udp_tunnel_drop_rx_info(struct net_device *dev)
{
	ASSERT_RTNL();
	call_netdevice_notifiers(NETDEV_UDP_TUNNEL_DROP_INFO, dev);
}

/* Transmit the skb using UDP encapsulation. */
void udp_tunnel_xmit_skb(struct rtable *rt, struct sock *sk, struct sk_buff *skb,
			 __be32 src, __be32 dst, __u8 tos, __u8 ttl,
			 __be16 df, __be16 src_port, __be16 dst_port,
			 bool xnet, bool nocheck);

#if IS_ENABLED(CONFIG_IPV6)
int udp_tunnel6_xmit_skb(struct dst_entry *dst, struct sock *sk,
			 struct sk_buff *skb,
			 struct net_device *dev, struct in6_addr *saddr,
			 struct in6_addr *daddr,
			 __u8 prio, __u8 ttl, __be32 label,
			 __be16 src_port, __be16 dst_port, bool nocheck);
#endif

void udp_tunnel_sock_release(struct socket *sock);

struct metadata_dst *udp_tun_rx_dst(struct sk_buff *skb, unsigned short family,
				    __be16 flags, __be64 tunnel_id,
				    int md_size);

#ifdef CONFIG_INET
static inline int udp_tunnel_handle_offloads(struct sk_buff *skb, bool udp_csum)
{
	int type = udp_csum ? SKB_GSO_UDP_TUNNEL_CSUM : SKB_GSO_UDP_TUNNEL;

	return iptunnel_handle_offloads(skb, type);
}
#endif

static inline void udp_tunnel_encap_enable(struct socket *sock)
{
	struct udp_sock *up = udp_sk(sock->sk);

	if (up->encap_enabled)
		return;

	up->encap_enabled = 1;
#if IS_ENABLED(CONFIG_IPV6)
	if (sock->sk->sk_family == PF_INET6)
		ipv6_stub->udpv6_encap_enable();
	else
#endif
		udp_encap_enable();
}

#endif
