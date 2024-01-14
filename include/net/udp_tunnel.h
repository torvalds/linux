/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NET_UDP_TUNNEL_H
#define __NET_UDP_TUNNEL_H

#include <net/ip_tunnels.h>
#include <net/udp.h>

#if IS_ENABLED(CONFIG_IPV6)
#include <net/ipv6.h>
#include <net/ipv6_stubs.h>
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
typedef void (*udp_tunnel_encap_err_rcv_t)(struct sock *sk,
					   struct sk_buff *skb,
					   unsigned int udp_offset);
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
	udp_tunnel_encap_err_rcv_t encap_err_rcv;
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
	UDP_TUNNEL_TYPE_VXLAN	  = BIT(0), /* RFC 7348 */
	UDP_TUNNEL_TYPE_GENEVE	  = BIT(1), /* draft-ietf-nvo3-geneve */
	UDP_TUNNEL_TYPE_VXLAN_GPE = BIT(2), /* draft-ietf-nvo3-vxlan-gpe */
};

struct udp_tunnel_info {
	unsigned short type;
	sa_family_t sa_family;
	__be16 port;
	u8 hw_priv;
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
	if (!(dev->features & NETIF_F_RX_UDP_TUNNEL_PORT))
		return;
	call_netdevice_notifiers(NETDEV_UDP_TUNNEL_PUSH_INFO, dev);
}

static inline void udp_tunnel_drop_rx_info(struct net_device *dev)
{
	ASSERT_RTNL();
	if (!(dev->features & NETIF_F_RX_UDP_TUNNEL_PORT))
		return;
	call_netdevice_notifiers(NETDEV_UDP_TUNNEL_DROP_INFO, dev);
}

/* Transmit the skb using UDP encapsulation. */
void udp_tunnel_xmit_skb(struct rtable *rt, struct sock *sk, struct sk_buff *skb,
			 __be32 src, __be32 dst, __u8 tos, __u8 ttl,
			 __be16 df, __be16 src_port, __be16 dst_port,
			 bool xnet, bool nocheck);

int udp_tunnel6_xmit_skb(struct dst_entry *dst, struct sock *sk,
			 struct sk_buff *skb,
			 struct net_device *dev, struct in6_addr *saddr,
			 struct in6_addr *daddr,
			 __u8 prio, __u8 ttl, __be32 label,
			 __be16 src_port, __be16 dst_port, bool nocheck);

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

static inline void udp_tunnel_encap_enable(struct sock *sk)
{
	if (udp_test_and_set_bit(ENCAP_ENABLED, sk))
		return;

#if IS_ENABLED(CONFIG_IPV6)
	if (READ_ONCE(sk->sk_family) == PF_INET6)
		ipv6_stub->udpv6_encap_enable();
#endif
	udp_encap_enable();
}

#define UDP_TUNNEL_NIC_MAX_TABLES	4

enum udp_tunnel_nic_info_flags {
	/* Device callbacks may sleep */
	UDP_TUNNEL_NIC_INFO_MAY_SLEEP	= BIT(0),
	/* Device only supports offloads when it's open, all ports
	 * will be removed before close and re-added after open.
	 */
	UDP_TUNNEL_NIC_INFO_OPEN_ONLY	= BIT(1),
	/* Device supports only IPv4 tunnels */
	UDP_TUNNEL_NIC_INFO_IPV4_ONLY	= BIT(2),
	/* Device has hard-coded the IANA VXLAN port (4789) as VXLAN.
	 * This port must not be counted towards n_entries of any table.
	 * Driver will not receive any callback associated with port 4789.
	 */
	UDP_TUNNEL_NIC_INFO_STATIC_IANA_VXLAN	= BIT(3),
};

struct udp_tunnel_nic;

#define UDP_TUNNEL_NIC_MAX_SHARING_DEVICES	(U16_MAX / 2)

struct udp_tunnel_nic_shared {
	struct udp_tunnel_nic *udp_tunnel_nic_info;

	struct list_head devices;
};

struct udp_tunnel_nic_shared_node {
	struct net_device *dev;
	struct list_head list;
};

/**
 * struct udp_tunnel_nic_info - driver UDP tunnel offload information
 * @set_port:	callback for adding a new port
 * @unset_port:	callback for removing a port
 * @sync_table:	callback for syncing the entire port table at once
 * @shared:	reference to device global state (optional)
 * @flags:	device flags from enum udp_tunnel_nic_info_flags
 * @tables:	UDP port tables this device has
 * @tables.n_entries:		number of entries in this table
 * @tables.tunnel_types:	types of tunnels this table accepts
 *
 * Drivers are expected to provide either @set_port and @unset_port callbacks
 * or the @sync_table callback. Callbacks are invoked with rtnl lock held.
 *
 * Devices which (misguidedly) share the UDP tunnel port table across multiple
 * netdevs should allocate an instance of struct udp_tunnel_nic_shared and
 * point @shared at it.
 * There must never be more than %UDP_TUNNEL_NIC_MAX_SHARING_DEVICES devices
 * sharing a table.
 *
 * Known limitations:
 *  - UDP tunnel port notifications are fundamentally best-effort -
 *    it is likely the driver will both see skbs which use a UDP tunnel port,
 *    while not being a tunneled skb, and tunnel skbs from other ports -
 *    drivers should only use these ports for non-critical RX-side offloads,
 *    e.g. the checksum offload;
 *  - none of the devices care about the socket family at present, so we don't
 *    track it. Please extend this code if you care.
 */
struct udp_tunnel_nic_info {
	/* one-by-one */
	int (*set_port)(struct net_device *dev,
			unsigned int table, unsigned int entry,
			struct udp_tunnel_info *ti);
	int (*unset_port)(struct net_device *dev,
			  unsigned int table, unsigned int entry,
			  struct udp_tunnel_info *ti);

	/* all at once */
	int (*sync_table)(struct net_device *dev, unsigned int table);

	struct udp_tunnel_nic_shared *shared;

	unsigned int flags;

	struct udp_tunnel_nic_table_info {
		unsigned int n_entries;
		unsigned int tunnel_types;
	} tables[UDP_TUNNEL_NIC_MAX_TABLES];
};

/* UDP tunnel module dependencies
 *
 * Tunnel drivers are expected to have a hard dependency on the udp_tunnel
 * module. NIC drivers are not, they just attach their
 * struct udp_tunnel_nic_info to the netdev and wait for callbacks to come.
 * Loading a tunnel driver will cause the udp_tunnel module to be loaded
 * and only then will all the required state structures be allocated.
 * Since we want a weak dependency from the drivers and the core to udp_tunnel
 * we call things through the following stubs.
 */
struct udp_tunnel_nic_ops {
	void (*get_port)(struct net_device *dev, unsigned int table,
			 unsigned int idx, struct udp_tunnel_info *ti);
	void (*set_port_priv)(struct net_device *dev, unsigned int table,
			      unsigned int idx, u8 priv);
	void (*add_port)(struct net_device *dev, struct udp_tunnel_info *ti);
	void (*del_port)(struct net_device *dev, struct udp_tunnel_info *ti);
	void (*reset_ntf)(struct net_device *dev);

	size_t (*dump_size)(struct net_device *dev, unsigned int table);
	int (*dump_write)(struct net_device *dev, unsigned int table,
			  struct sk_buff *skb);
};

#ifdef CONFIG_INET
extern const struct udp_tunnel_nic_ops *udp_tunnel_nic_ops;
#else
#define udp_tunnel_nic_ops	((struct udp_tunnel_nic_ops *)NULL)
#endif

static inline void
udp_tunnel_nic_get_port(struct net_device *dev, unsigned int table,
			unsigned int idx, struct udp_tunnel_info *ti)
{
	/* This helper is used from .sync_table, we indicate empty entries
	 * by zero'ed @ti. Drivers which need to know the details of a port
	 * when it gets deleted should use the .set_port / .unset_port
	 * callbacks.
	 * Zero out here, otherwise !CONFIG_INET causes uninitilized warnings.
	 */
	memset(ti, 0, sizeof(*ti));

	if (udp_tunnel_nic_ops)
		udp_tunnel_nic_ops->get_port(dev, table, idx, ti);
}

static inline void
udp_tunnel_nic_set_port_priv(struct net_device *dev, unsigned int table,
			     unsigned int idx, u8 priv)
{
	if (udp_tunnel_nic_ops)
		udp_tunnel_nic_ops->set_port_priv(dev, table, idx, priv);
}

static inline void
udp_tunnel_nic_add_port(struct net_device *dev, struct udp_tunnel_info *ti)
{
	if (!(dev->features & NETIF_F_RX_UDP_TUNNEL_PORT))
		return;
	if (udp_tunnel_nic_ops)
		udp_tunnel_nic_ops->add_port(dev, ti);
}

static inline void
udp_tunnel_nic_del_port(struct net_device *dev, struct udp_tunnel_info *ti)
{
	if (!(dev->features & NETIF_F_RX_UDP_TUNNEL_PORT))
		return;
	if (udp_tunnel_nic_ops)
		udp_tunnel_nic_ops->del_port(dev, ti);
}

/**
 * udp_tunnel_nic_reset_ntf() - device-originating reset notification
 * @dev: network interface device structure
 *
 * Called by the driver to inform the core that the entire UDP tunnel port
 * state has been lost, usually due to device reset. Core will assume device
 * forgot all the ports and issue .set_port and .sync_table callbacks as
 * necessary.
 *
 * This function must be called with rtnl lock held, and will issue all
 * the callbacks before returning.
 */
static inline void udp_tunnel_nic_reset_ntf(struct net_device *dev)
{
	if (udp_tunnel_nic_ops)
		udp_tunnel_nic_ops->reset_ntf(dev);
}

static inline size_t
udp_tunnel_nic_dump_size(struct net_device *dev, unsigned int table)
{
	if (!udp_tunnel_nic_ops)
		return 0;
	return udp_tunnel_nic_ops->dump_size(dev, table);
}

static inline int
udp_tunnel_nic_dump_write(struct net_device *dev, unsigned int table,
			  struct sk_buff *skb)
{
	if (!udp_tunnel_nic_ops)
		return 0;
	return udp_tunnel_nic_ops->dump_write(dev, table, skb);
}
#endif
