#include <linux/module.h>
#include <linux/errno.h>
#include <linux/socket.h>
#include <linux/udp.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <net/udp.h>
#include <net/udp_tunnel.h>
#include <net/net_namespace.h>

int udp_sock_create(struct net *net, struct udp_port_cfg *cfg,
		    struct socket **sockp)
{
	int err = -EINVAL;
	struct socket *sock = NULL;

#if IS_ENABLED(CONFIG_IPV6)
	if (cfg->family == AF_INET6) {
		struct sockaddr_in6 udp6_addr;

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
	} else
#endif
	if (cfg->family == AF_INET) {
		struct sockaddr_in udp_addr;

		err = sock_create_kern(AF_INET, SOCK_DGRAM, 0, &sock);
		if (err < 0)
			goto error;

		sk_change_net(sock->sk, net);

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
			err = kernel_connect(sock,
					     (struct sockaddr *)&udp_addr,
					     sizeof(udp_addr), 0);
			if (err < 0)
				goto error;
		}

		sock->sk->sk_no_check_tx = !cfg->use_udp_checksums;
	} else {
		return -EPFNOSUPPORT;
	}


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
EXPORT_SYMBOL(udp_sock_create);

MODULE_LICENSE("GPL");
