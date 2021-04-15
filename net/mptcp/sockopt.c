// SPDX-License-Identifier: GPL-2.0
/* Multipath TCP
 *
 * Copyright (c) 2021, Red Hat.
 */

#define pr_fmt(fmt) "MPTCP: " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <net/sock.h>
#include <net/protocol.h>
#include <net/tcp.h>
#include <net/mptcp.h>
#include "protocol.h"

static struct sock *__mptcp_tcp_fallback(struct mptcp_sock *msk)
{
	sock_owned_by_me((const struct sock *)msk);

	if (likely(!__mptcp_check_fallback(msk)))
		return NULL;

	return msk->first;
}

static int mptcp_setsockopt_sol_socket(struct mptcp_sock *msk, int optname,
				       sockptr_t optval, unsigned int optlen)
{
	struct sock *sk = (struct sock *)msk;
	struct socket *ssock;
	int ret;

	switch (optname) {
	case SO_REUSEPORT:
	case SO_REUSEADDR:
		lock_sock(sk);
		ssock = __mptcp_nmpc_socket(msk);
		if (!ssock) {
			release_sock(sk);
			return -EINVAL;
		}

		ret = sock_setsockopt(ssock, SOL_SOCKET, optname, optval, optlen);
		if (ret == 0) {
			if (optname == SO_REUSEPORT)
				sk->sk_reuseport = ssock->sk->sk_reuseport;
			else if (optname == SO_REUSEADDR)
				sk->sk_reuse = ssock->sk->sk_reuse;
		}
		release_sock(sk);
		return ret;
	}

	return sock_setsockopt(sk->sk_socket, SOL_SOCKET, optname, optval, optlen);
}

static int mptcp_setsockopt_v6(struct mptcp_sock *msk, int optname,
			       sockptr_t optval, unsigned int optlen)
{
	struct sock *sk = (struct sock *)msk;
	int ret = -EOPNOTSUPP;
	struct socket *ssock;

	switch (optname) {
	case IPV6_V6ONLY:
		lock_sock(sk);
		ssock = __mptcp_nmpc_socket(msk);
		if (!ssock) {
			release_sock(sk);
			return -EINVAL;
		}

		ret = tcp_setsockopt(ssock->sk, SOL_IPV6, optname, optval, optlen);
		if (ret == 0)
			sk->sk_ipv6only = ssock->sk->sk_ipv6only;

		release_sock(sk);
		break;
	}

	return ret;
}

int mptcp_setsockopt(struct sock *sk, int level, int optname,
		     sockptr_t optval, unsigned int optlen)
{
	struct mptcp_sock *msk = mptcp_sk(sk);
	struct sock *ssk;

	pr_debug("msk=%p", msk);

	if (level == SOL_SOCKET)
		return mptcp_setsockopt_sol_socket(msk, optname, optval, optlen);

	/* @@ the meaning of setsockopt() when the socket is connected and
	 * there are multiple subflows is not yet defined. It is up to the
	 * MPTCP-level socket to configure the subflows until the subflow
	 * is in TCP fallback, when TCP socket options are passed through
	 * to the one remaining subflow.
	 */
	lock_sock(sk);
	ssk = __mptcp_tcp_fallback(msk);
	release_sock(sk);
	if (ssk)
		return tcp_setsockopt(ssk, level, optname, optval, optlen);

	if (level == SOL_IPV6)
		return mptcp_setsockopt_v6(msk, optname, optval, optlen);

	return -EOPNOTSUPP;
}

int mptcp_getsockopt(struct sock *sk, int level, int optname,
		     char __user *optval, int __user *option)
{
	struct mptcp_sock *msk = mptcp_sk(sk);
	struct sock *ssk;

	pr_debug("msk=%p", msk);

	/* @@ the meaning of setsockopt() when the socket is connected and
	 * there are multiple subflows is not yet defined. It is up to the
	 * MPTCP-level socket to configure the subflows until the subflow
	 * is in TCP fallback, when socket options are passed through
	 * to the one remaining subflow.
	 */
	lock_sock(sk);
	ssk = __mptcp_tcp_fallback(msk);
	release_sock(sk);
	if (ssk)
		return tcp_getsockopt(ssk, level, optname, optval, option);

	return -EOPNOTSUPP;
}

