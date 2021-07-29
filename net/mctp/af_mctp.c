// SPDX-License-Identifier: GPL-2.0
/*
 * Management Component Transport Protocol (MCTP)
 *
 * Copyright (c) 2021 Code Construct
 * Copyright (c) 2021 Google
 */

#include <linux/if_arp.h>
#include <linux/net.h>
#include <linux/mctp.h>
#include <linux/module.h>
#include <linux/socket.h>

#include <net/mctp.h>
#include <net/mctpdevice.h>
#include <net/sock.h>

/* socket implementation */

struct mctp_sock {
	struct sock	sk;
};

static int mctp_release(struct socket *sock)
{
	struct sock *sk = sock->sk;

	if (sk) {
		sock->sk = NULL;
		sk->sk_prot->close(sk, 0);
	}

	return 0;
}

static int mctp_bind(struct socket *sock, struct sockaddr *addr, int addrlen)
{
	return 0;
}

static int mctp_sendmsg(struct socket *sock, struct msghdr *msg, size_t len)
{
	return 0;
}

static int mctp_recvmsg(struct socket *sock, struct msghdr *msg, size_t len,
			int flags)
{
	return 0;
}

static int mctp_setsockopt(struct socket *sock, int level, int optname,
			   sockptr_t optval, unsigned int optlen)
{
	return -EINVAL;
}

static int mctp_getsockopt(struct socket *sock, int level, int optname,
			   char __user *optval, int __user *optlen)
{
	return -EINVAL;
}

static const struct proto_ops mctp_dgram_ops = {
	.family		= PF_MCTP,
	.release	= mctp_release,
	.bind		= mctp_bind,
	.connect	= sock_no_connect,
	.socketpair	= sock_no_socketpair,
	.accept		= sock_no_accept,
	.getname	= sock_no_getname,
	.poll		= datagram_poll,
	.ioctl		= sock_no_ioctl,
	.gettstamp	= sock_gettstamp,
	.listen		= sock_no_listen,
	.shutdown	= sock_no_shutdown,
	.setsockopt	= mctp_setsockopt,
	.getsockopt	= mctp_getsockopt,
	.sendmsg	= mctp_sendmsg,
	.recvmsg	= mctp_recvmsg,
	.mmap		= sock_no_mmap,
	.sendpage	= sock_no_sendpage,
};

static void mctp_sk_close(struct sock *sk, long timeout)
{
	sk_common_release(sk);
}

static struct proto mctp_proto = {
	.name		= "MCTP",
	.owner		= THIS_MODULE,
	.obj_size	= sizeof(struct mctp_sock),
	.close		= mctp_sk_close,
};

static int mctp_pf_create(struct net *net, struct socket *sock,
			  int protocol, int kern)
{
	const struct proto_ops *ops;
	struct proto *proto;
	struct sock *sk;
	int rc;

	if (protocol)
		return -EPROTONOSUPPORT;

	/* only datagram sockets are supported */
	if (sock->type != SOCK_DGRAM)
		return -ESOCKTNOSUPPORT;

	proto = &mctp_proto;
	ops = &mctp_dgram_ops;

	sock->state = SS_UNCONNECTED;
	sock->ops = ops;

	sk = sk_alloc(net, PF_MCTP, GFP_KERNEL, proto, kern);
	if (!sk)
		return -ENOMEM;

	sock_init_data(sock, sk);

	rc = 0;
	if (sk->sk_prot->init)
		rc = sk->sk_prot->init(sk);

	if (rc)
		goto err_sk_put;

	return 0;

err_sk_put:
	sock_orphan(sk);
	sock_put(sk);
	return rc;
}

static struct net_proto_family mctp_pf = {
	.family = PF_MCTP,
	.create = mctp_pf_create,
	.owner = THIS_MODULE,
};

static __init int mctp_init(void)
{
	int rc;

	pr_info("mctp: management component transport protocol core\n");

	rc = sock_register(&mctp_pf);
	if (rc)
		return rc;

	rc = proto_register(&mctp_proto, 0);
	if (rc)
		goto err_unreg_sock;

	mctp_device_init();

	return 0;

err_unreg_sock:
	sock_unregister(PF_MCTP);

	return rc;
}

static __exit void mctp_exit(void)
{
	mctp_device_exit();
	proto_unregister(&mctp_proto);
	sock_unregister(PF_MCTP);
}

module_init(mctp_init);
module_exit(mctp_exit);

MODULE_DESCRIPTION("MCTP core");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Jeremy Kerr <jk@codeconstruct.com.au>");

MODULE_ALIAS_NETPROTO(PF_MCTP);
