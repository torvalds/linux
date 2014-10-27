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
	struct sockaddr_in6 udp6_addr;
	int err;
	struct socket *sock = NULL;

	err = sock_create_kern(AF_INET6, SOCK_DGRAM, 0, &sock);
	if (err < 0)
		goto error;

	sk_change_net(sock->sk, net);

	udp6_addr.sin6_family = AF_INET6;
	memcpy(&udp6_addr.sin6_addr, &cfg->local_ip6,
	       sizeof(udp6_addr.sin6_addr));
	udp6_addr.sin6_port = cfg->local_udp_port;
	err = kernel_bind(sock, (struct sockaddr *)&udp6_addr,
			  sizeof(udp6_addr));
	if (err < 0)
		goto error;

	if (cfg->peer_udp_port) {
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
		sk_release_kernel(sock->sk);
	}
	*sockp = NULL;
	return err;
}
EXPORT_SYMBOL_GPL(udp_sock_create6);

int udp_tunnel6_xmit_skb(struct socket *sock, struct dst_entry *dst,
			 struct sk_buff *skb, struct net_device *dev,
			 struct in6_addr *saddr, struct in6_addr *daddr,
			 __u8 prio, __u8 ttl, __be16 src_port, __be16 dst_port)
{
	struct udphdr *uh;
	struct ipv6hdr *ip6h;
	struct sock *sk = sock->sk;

	__skb_push(skb, sizeof(*uh));
	skb_reset_transport_header(skb);
	uh = udp_hdr(skb);

	uh->dest = dst_port;
	uh->source = src_port;

	uh->len = htons(skb->len);
	uh->check = 0;

	memset(&(IPCB(skb)->opt), 0, sizeof(IPCB(skb)->opt));
	IPCB(skb)->flags &= ~(IPSKB_XFRM_TUNNEL_SIZE | IPSKB_XFRM_TRANSFORMED
			    | IPSKB_REROUTED);
	skb_dst_set(skb, dst);

	udp6_set_csum(udp_get_no_check6_tx(sk), skb, &inet6_sk(sk)->saddr,
		      &sk->sk_v6_daddr, skb->len);

	__skb_push(skb, sizeof(*ip6h));
	skb_reset_network_header(skb);
	ip6h		  = ipv6_hdr(skb);
	ip6_flow_hdr(ip6h, prio, htonl(0));
	ip6h->payload_len = htons(skb->len);
	ip6h->nexthdr     = IPPROTO_UDP;
	ip6h->hop_limit   = ttl;
	ip6h->daddr	  = *daddr;
	ip6h->saddr	  = *saddr;

	ip6tunnel_xmit(skb, dev);
	return 0;
}
EXPORT_SYMBOL_GPL(udp_tunnel6_xmit_skb);

MODULE_LICENSE("GPL");
