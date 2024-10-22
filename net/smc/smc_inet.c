// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Shared Memory Communications over RDMA (SMC-R) and RoCE
 *
 *  Definitions for the IPPROTO_SMC (socket related)
 *
 *  Copyright IBM Corp. 2016, 2018
 *  Copyright (c) 2024, Alibaba Inc.
 *
 *  Author: D. Wythe <alibuda@linux.alibaba.com>
 */

#include <net/protocol.h>
#include <net/sock.h>

#include "smc_inet.h"
#include "smc.h"

static int smc_inet_init_sock(struct sock *sk);

static struct proto smc_inet_prot = {
	.name		= "INET_SMC",
	.owner		= THIS_MODULE,
	.init		= smc_inet_init_sock,
	.hash		= smc_hash_sk,
	.unhash		= smc_unhash_sk,
	.release_cb	= smc_release_cb,
	.obj_size	= sizeof(struct smc_sock),
	.h.smc_hash	= &smc_v4_hashinfo,
	.slab_flags	= SLAB_TYPESAFE_BY_RCU,
};

static const struct proto_ops smc_inet_stream_ops = {
	.family		= PF_INET,
	.owner		= THIS_MODULE,
	.release	= smc_release,
	.bind		= smc_bind,
	.connect	= smc_connect,
	.socketpair	= sock_no_socketpair,
	.accept		= smc_accept,
	.getname	= smc_getname,
	.poll		= smc_poll,
	.ioctl		= smc_ioctl,
	.listen		= smc_listen,
	.shutdown	= smc_shutdown,
	.setsockopt	= smc_setsockopt,
	.getsockopt	= smc_getsockopt,
	.sendmsg	= smc_sendmsg,
	.recvmsg	= smc_recvmsg,
	.mmap		= sock_no_mmap,
	.splice_read	= smc_splice_read,
};

static struct inet_protosw smc_inet_protosw = {
	.type		= SOCK_STREAM,
	.protocol	= IPPROTO_SMC,
	.prot		= &smc_inet_prot,
	.ops		= &smc_inet_stream_ops,
	.flags		= INET_PROTOSW_ICSK,
};

#if IS_ENABLED(CONFIG_IPV6)
struct smc6_sock {
	struct smc_sock		smc;
	struct ipv6_pinfo	inet6;
};

static struct proto smc_inet6_prot = {
	.name		= "INET6_SMC",
	.owner		= THIS_MODULE,
	.init		= smc_inet_init_sock,
	.hash		= smc_hash_sk,
	.unhash		= smc_unhash_sk,
	.release_cb	= smc_release_cb,
	.obj_size	= sizeof(struct smc6_sock),
	.h.smc_hash	= &smc_v6_hashinfo,
	.slab_flags	= SLAB_TYPESAFE_BY_RCU,
	.ipv6_pinfo_offset	= offsetof(struct smc6_sock, inet6),
};

static const struct proto_ops smc_inet6_stream_ops = {
	.family		= PF_INET6,
	.owner		= THIS_MODULE,
	.release	= smc_release,
	.bind		= smc_bind,
	.connect	= smc_connect,
	.socketpair	= sock_no_socketpair,
	.accept		= smc_accept,
	.getname	= smc_getname,
	.poll		= smc_poll,
	.ioctl		= smc_ioctl,
	.listen		= smc_listen,
	.shutdown	= smc_shutdown,
	.setsockopt	= smc_setsockopt,
	.getsockopt	= smc_getsockopt,
	.sendmsg	= smc_sendmsg,
	.recvmsg	= smc_recvmsg,
	.mmap		= sock_no_mmap,
	.splice_read	= smc_splice_read,
};

static struct inet_protosw smc_inet6_protosw = {
	.type		= SOCK_STREAM,
	.protocol	= IPPROTO_SMC,
	.prot		= &smc_inet6_prot,
	.ops		= &smc_inet6_stream_ops,
	.flags		= INET_PROTOSW_ICSK,
};
#endif /* CONFIG_IPV6 */

static int smc_inet_init_sock(struct sock *sk)
{
	struct net *net = sock_net(sk);

	/* init common smc sock */
	smc_sk_init(net, sk, IPPROTO_SMC);
	/* create clcsock */
	return smc_create_clcsk(net, sk, sk->sk_family);
}

int __init smc_inet_init(void)
{
	int rc;

	rc = proto_register(&smc_inet_prot, 1);
	if (rc) {
		pr_err("%s: proto_register smc_inet_prot fails with %d\n",
		       __func__, rc);
		return rc;
	}
	/* no return value */
	inet_register_protosw(&smc_inet_protosw);

#if IS_ENABLED(CONFIG_IPV6)
	rc = proto_register(&smc_inet6_prot, 1);
	if (rc) {
		pr_err("%s: proto_register smc_inet6_prot fails with %d\n",
		       __func__, rc);
		goto out_inet6_prot;
	}
	rc = inet6_register_protosw(&smc_inet6_protosw);
	if (rc) {
		pr_err("%s: inet6_register_protosw smc_inet6_protosw fails with %d\n",
		       __func__, rc);
		goto out_inet6_protosw;
	}
	return rc;
out_inet6_protosw:
	proto_unregister(&smc_inet6_prot);
out_inet6_prot:
	inet_unregister_protosw(&smc_inet_protosw);
	proto_unregister(&smc_inet_prot);
#endif /* CONFIG_IPV6 */
	return rc;
}

void smc_inet_exit(void)
{
#if IS_ENABLED(CONFIG_IPV6)
	inet6_unregister_protosw(&smc_inet6_protosw);
	proto_unregister(&smc_inet6_prot);
#endif /* CONFIG_IPV6 */
	inet_unregister_protosw(&smc_inet_protosw);
	proto_unregister(&smc_inet_prot);
}
