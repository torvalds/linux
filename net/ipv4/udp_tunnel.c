#include <linux/module.h>
#include <linux/errno.h>
#include <linux/socket.h>
#include <linux/udp.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <net/dst_metadata.h>
#include <net/net_namespace.h>
#include <net/udp.h>
#include <net/udp_tunnel.h>

int udp_sock_create4(struct net *net, struct udp_port_cfg *cfg,
		     struct socket **sockp)
{
	int err;
	struct socket *sock = NULL;
	struct sockaddr_in udp_addr;

	err = sock_create_kern(net, AF_INET, SOCK_DGRAM, 0, &sock);
	if (err < 0)
		goto error;

	if (cfg->bind_ifindex) {
		struct net_device *dev;

		dev = dev_get_by_index(net, cfg->bind_ifindex);
		if (!dev) {
			err = -ENODEV;
			goto error;
		}

		err = kernel_setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE,
					dev->name, strlen(dev->name) + 1);
		dev_put(dev);

		if (err < 0)
			goto error;
	}

	udp_addr.sin_family = AF_INET;
	udp_addr.sin_addr = cfg->local_ip;
	udp_addr.sin_port = cfg->local_udp_port;
	err = kernel_bind(sock, (struct sockaddr *)&udp_addr,
			  sizeof(udp_addr));
	if (err < 0)
		goto error;

	if (cfg->peer_udp_port) {
		udp_addr.sin_family = AF_INET;
		udp_addr.sin_addr = cfg->peer_ip;
		udp_addr.sin_port = cfg->peer_udp_port;
		err = kernel_connect(sock, (struct sockaddr *)&udp_addr,
				     sizeof(udp_addr), 0);
		if (err < 0)
			goto error;
	}

	sock->sk->sk_no_check_tx = !cfg->use_udp_checksums;

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
EXPORT_SYMBOL(udp_sock_create4);

void setup_udp_tunnel_sock(struct net *net, struct socket *sock,
			   struct udp_tunnel_sock_cfg *cfg)
{
	struct sock *sk = sock->sk;

	/* Disable multicast loopback */
	inet_sk(sk)->mc_loop = 0;

	/* Enable CHECKSUM_UNNECESSARY to CHECKSUM_COMPLETE conversion */
	inet_inc_convert_csum(sk);

	rcu_assign_sk_user_data(sk, cfg->sk_user_data);

	udp_sk(sk)->encap_type = cfg->encap_type;
	udp_sk(sk)->encap_rcv = cfg->encap_rcv;
	udp_sk(sk)->encap_err_lookup = cfg->encap_err_lookup;
	udp_sk(sk)->encap_destroy = cfg->encap_destroy;
	udp_sk(sk)->gro_receive = cfg->gro_receive;
	udp_sk(sk)->gro_complete = cfg->gro_complete;

	udp_tunnel_encap_enable(sock);
}
EXPORT_SYMBOL_GPL(setup_udp_tunnel_sock);

void udp_tunnel_push_rx_port(struct net_device *dev, struct socket *sock,
			     unsigned short type)
{
	struct sock *sk = sock->sk;
	struct udp_tunnel_info ti;

	if (!dev->netdev_ops->ndo_udp_tunnel_add ||
	    !(dev->features & NETIF_F_RX_UDP_TUNNEL_PORT))
		return;

	ti.type = type;
	ti.sa_family = sk->sk_family;
	ti.port = inet_sk(sk)->inet_sport;

	dev->netdev_ops->ndo_udp_tunnel_add(dev, &ti);
}
EXPORT_SYMBOL_GPL(udp_tunnel_push_rx_port);

void udp_tunnel_drop_rx_port(struct net_device *dev, struct socket *sock,
			     unsigned short type)
{
	struct sock *sk = sock->sk;
	struct udp_tunnel_info ti;

	if (!dev->netdev_ops->ndo_udp_tunnel_del ||
	    !(dev->features & NETIF_F_RX_UDP_TUNNEL_PORT))
		return;

	ti.type = type;
	ti.sa_family = sk->sk_family;
	ti.port = inet_sk(sk)->inet_sport;

	dev->netdev_ops->ndo_udp_tunnel_del(dev, &ti);
}
EXPORT_SYMBOL_GPL(udp_tunnel_drop_rx_port);

/* Notify netdevs that UDP port started listening */
void udp_tunnel_notify_add_rx_port(struct socket *sock, unsigned short type)
{
	struct sock *sk = sock->sk;
	struct net *net = sock_net(sk);
	struct udp_tunnel_info ti;
	struct net_device *dev;

	ti.type = type;
	ti.sa_family = sk->sk_family;
	ti.port = inet_sk(sk)->inet_sport;

	rcu_read_lock();
	for_each_netdev_rcu(net, dev) {
		if (!dev->netdev_ops->ndo_udp_tunnel_add)
			continue;
		if (!(dev->features & NETIF_F_RX_UDP_TUNNEL_PORT))
			continue;
		dev->netdev_ops->ndo_udp_tunnel_add(dev, &ti);
	}
	rcu_read_unlock();
}
EXPORT_SYMBOL_GPL(udp_tunnel_notify_add_rx_port);

/* Notify netdevs that UDP port is no more listening */
void udp_tunnel_notify_del_rx_port(struct socket *sock, unsigned short type)
{
	struct sock *sk = sock->sk;
	struct net *net = sock_net(sk);
	struct udp_tunnel_info ti;
	struct net_device *dev;

	ti.type = type;
	ti.sa_family = sk->sk_family;
	ti.port = inet_sk(sk)->inet_sport;

	rcu_read_lock();
	for_each_netdev_rcu(net, dev) {
		if (!dev->netdev_ops->ndo_udp_tunnel_del)
			continue;
		if (!(dev->features & NETIF_F_RX_UDP_TUNNEL_PORT))
			continue;
		dev->netdev_ops->ndo_udp_tunnel_del(dev, &ti);
	}
	rcu_read_unlock();
}
EXPORT_SYMBOL_GPL(udp_tunnel_notify_del_rx_port);

void udp_tunnel_xmit_skb(struct rtable *rt, struct sock *sk, struct sk_buff *skb,
			 __be32 src, __be32 dst, __u8 tos, __u8 ttl,
			 __be16 df, __be16 src_port, __be16 dst_port,
			 bool xnet, bool nocheck)
{
	struct udphdr *uh;

	__skb_push(skb, sizeof(*uh));
	skb_reset_transport_header(skb);
	uh = udp_hdr(skb);

	uh->dest = dst_port;
	uh->source = src_port;
	uh->len = htons(skb->len);

	memset(&(IPCB(skb)->opt), 0, sizeof(IPCB(skb)->opt));

	udp_set_csum(nocheck, skb, src, dst, skb->len);

	iptunnel_xmit(sk, rt, skb, src, dst, IPPROTO_UDP, tos, ttl, df, xnet);
}
EXPORT_SYMBOL_GPL(udp_tunnel_xmit_skb);

void udp_tunnel_sock_release(struct socket *sock)
{
	rcu_assign_sk_user_data(sock->sk, NULL);
	kernel_sock_shutdown(sock, SHUT_RDWR);
	sock_release(sock);
}
EXPORT_SYMBOL_GPL(udp_tunnel_sock_release);

struct metadata_dst *udp_tun_rx_dst(struct sk_buff *skb,  unsigned short family,
				    __be16 flags, __be64 tunnel_id, int md_size)
{
	struct metadata_dst *tun_dst;
	struct ip_tunnel_info *info;

	if (family == AF_INET)
		tun_dst = ip_tun_rx_dst(skb, flags, tunnel_id, md_size);
	else
		tun_dst = ipv6_tun_rx_dst(skb, flags, tunnel_id, md_size);
	if (!tun_dst)
		return NULL;

	info = &tun_dst->u.tun_info;
	info->key.tp_src = udp_hdr(skb)->source;
	info->key.tp_dst = udp_hdr(skb)->dest;
	if (udp_hdr(skb)->check)
		info->key.tun_flags |= TUNNEL_CSUM;
	return tun_dst;
}
EXPORT_SYMBOL_GPL(udp_tun_rx_dst);

MODULE_LICENSE("GPL");
