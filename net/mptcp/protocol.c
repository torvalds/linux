// SPDX-License-Identifier: GPL-2.0
/* Multipath TCP
 *
 * Copyright (c) 2017 - 2019, Intel Corporation.
 */

#define pr_fmt(fmt) "MPTCP: " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <net/sock.h>
#include <net/inet_common.h>
#include <net/inet_hashtables.h>
#include <net/protocol.h>
#include <net/tcp.h>
#include <net/mptcp.h>
#include "protocol.h"

#define MPTCP_SAME_STATE TCP_MAX_STATES

/* If msk has an initial subflow socket, and the MP_CAPABLE handshake has not
 * completed yet or has failed, return the subflow socket.
 * Otherwise return NULL.
 */
static struct socket *__mptcp_nmpc_socket(const struct mptcp_sock *msk)
{
	if (!msk->subflow)
		return NULL;

	return msk->subflow;
}

static bool __mptcp_can_create_subflow(const struct mptcp_sock *msk)
{
	return ((struct sock *)msk)->sk_state == TCP_CLOSE;
}

static struct socket *__mptcp_socket_create(struct mptcp_sock *msk, int state)
{
	struct mptcp_subflow_context *subflow;
	struct sock *sk = (struct sock *)msk;
	struct socket *ssock;
	int err;

	ssock = __mptcp_nmpc_socket(msk);
	if (ssock)
		goto set_state;

	if (!__mptcp_can_create_subflow(msk))
		return ERR_PTR(-EINVAL);

	err = mptcp_subflow_create_socket(sk, &ssock);
	if (err)
		return ERR_PTR(err);

	msk->subflow = ssock;
	subflow = mptcp_subflow_ctx(ssock->sk);
	subflow->request_mptcp = 1;

set_state:
	if (state != MPTCP_SAME_STATE)
		inet_sk_state_store(sk, state);
	return ssock;
}

static int mptcp_sendmsg(struct sock *sk, struct msghdr *msg, size_t len)
{
	struct mptcp_sock *msk = mptcp_sk(sk);
	struct socket *subflow = msk->subflow;

	if (msg->msg_flags & ~(MSG_MORE | MSG_DONTWAIT | MSG_NOSIGNAL))
		return -EOPNOTSUPP;

	return sock_sendmsg(subflow, msg);
}

static int mptcp_recvmsg(struct sock *sk, struct msghdr *msg, size_t len,
			 int nonblock, int flags, int *addr_len)
{
	struct mptcp_sock *msk = mptcp_sk(sk);
	struct socket *subflow = msk->subflow;

	if (msg->msg_flags & ~(MSG_WAITALL | MSG_DONTWAIT))
		return -EOPNOTSUPP;

	return sock_recvmsg(subflow, msg, flags);
}

static int mptcp_init_sock(struct sock *sk)
{
	return 0;
}

static void mptcp_close(struct sock *sk, long timeout)
{
	struct mptcp_sock *msk = mptcp_sk(sk);
	struct socket *ssock;

	inet_sk_state_store(sk, TCP_CLOSE);

	ssock = __mptcp_nmpc_socket(msk);
	if (ssock) {
		pr_debug("subflow=%p", mptcp_subflow_ctx(ssock->sk));
		sock_release(ssock);
	}

	sock_orphan(sk);
	sock_put(sk);
}

static int mptcp_connect(struct sock *sk, struct sockaddr *saddr, int len)
{
	struct mptcp_sock *msk = mptcp_sk(sk);
	int err;

	saddr->sa_family = AF_INET;

	pr_debug("msk=%p, subflow=%p", msk,
		 mptcp_subflow_ctx(msk->subflow->sk));

	err = kernel_connect(msk->subflow, saddr, len, 0);

	sk->sk_state = TCP_ESTABLISHED;

	return err;
}

static struct proto mptcp_prot = {
	.name		= "MPTCP",
	.owner		= THIS_MODULE,
	.init		= mptcp_init_sock,
	.close		= mptcp_close,
	.accept		= inet_csk_accept,
	.connect	= mptcp_connect,
	.shutdown	= tcp_shutdown,
	.sendmsg	= mptcp_sendmsg,
	.recvmsg	= mptcp_recvmsg,
	.hash		= inet_hash,
	.unhash		= inet_unhash,
	.get_port	= inet_csk_get_port,
	.obj_size	= sizeof(struct mptcp_sock),
	.no_autobind	= true,
};

static int mptcp_bind(struct socket *sock, struct sockaddr *uaddr, int addr_len)
{
	struct mptcp_sock *msk = mptcp_sk(sock->sk);
	struct socket *ssock;
	int err = -ENOTSUPP;

	if (uaddr->sa_family != AF_INET) // @@ allow only IPv4 for now
		return err;

	lock_sock(sock->sk);
	ssock = __mptcp_socket_create(msk, MPTCP_SAME_STATE);
	if (IS_ERR(ssock)) {
		err = PTR_ERR(ssock);
		goto unlock;
	}

	err = ssock->ops->bind(ssock, uaddr, addr_len);

unlock:
	release_sock(sock->sk);
	return err;
}

static int mptcp_stream_connect(struct socket *sock, struct sockaddr *uaddr,
				int addr_len, int flags)
{
	struct mptcp_sock *msk = mptcp_sk(sock->sk);
	struct socket *ssock;
	int err;

	lock_sock(sock->sk);
	ssock = __mptcp_socket_create(msk, TCP_SYN_SENT);
	if (IS_ERR(ssock)) {
		err = PTR_ERR(ssock);
		goto unlock;
	}

	err = ssock->ops->connect(ssock, uaddr, addr_len, flags);
	inet_sk_state_store(sock->sk, inet_sk_state_load(ssock->sk));

unlock:
	release_sock(sock->sk);
	return err;
}

static __poll_t mptcp_poll(struct file *file, struct socket *sock,
			   struct poll_table_struct *wait)
{
	__poll_t mask = 0;

	return mask;
}

static struct proto_ops mptcp_stream_ops;

static struct inet_protosw mptcp_protosw = {
	.type		= SOCK_STREAM,
	.protocol	= IPPROTO_MPTCP,
	.prot		= &mptcp_prot,
	.ops		= &mptcp_stream_ops,
	.flags		= INET_PROTOSW_ICSK,
};

void __init mptcp_init(void)
{
	mptcp_prot.h.hashinfo = tcp_prot.h.hashinfo;
	mptcp_stream_ops = inet_stream_ops;
	mptcp_stream_ops.bind = mptcp_bind;
	mptcp_stream_ops.connect = mptcp_stream_connect;
	mptcp_stream_ops.poll = mptcp_poll;

	mptcp_subflow_init();

	if (proto_register(&mptcp_prot, 1) != 0)
		panic("Failed to register MPTCP proto.\n");

	inet_register_protosw(&mptcp_protosw);
}

#if IS_ENABLED(CONFIG_MPTCP_IPV6)
static struct proto_ops mptcp_v6_stream_ops;
static struct proto mptcp_v6_prot;

static struct inet_protosw mptcp_v6_protosw = {
	.type		= SOCK_STREAM,
	.protocol	= IPPROTO_MPTCP,
	.prot		= &mptcp_v6_prot,
	.ops		= &mptcp_v6_stream_ops,
	.flags		= INET_PROTOSW_ICSK,
};

int mptcpv6_init(void)
{
	int err;

	mptcp_v6_prot = mptcp_prot;
	strcpy(mptcp_v6_prot.name, "MPTCPv6");
	mptcp_v6_prot.slab = NULL;
	mptcp_v6_prot.obj_size = sizeof(struct mptcp_sock) +
				 sizeof(struct ipv6_pinfo);

	err = proto_register(&mptcp_v6_prot, 1);
	if (err)
		return err;

	mptcp_v6_stream_ops = inet6_stream_ops;
	mptcp_v6_stream_ops.bind = mptcp_bind;
	mptcp_v6_stream_ops.connect = mptcp_stream_connect;
	mptcp_v6_stream_ops.poll = mptcp_poll;

	err = inet6_register_protosw(&mptcp_v6_protosw);
	if (err)
		proto_unregister(&mptcp_v6_prot);

	return err;
}
#endif
