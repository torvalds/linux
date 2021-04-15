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

static u32 sockopt_seq_reset(const struct sock *sk)
{
	sock_owned_by_me(sk);

	/* Highbits contain state.  Allows to distinguish sockopt_seq
	 * of listener and established:
	 * s0 = new_listener()
	 * sockopt(s0) - seq is 1
	 * s1 = accept(s0) - s1 inherits seq 1 if listener sk (s0)
	 * sockopt(s0) - seq increments to 2 on s0
	 * sockopt(s1) // seq increments to 2 on s1 (different option)
	 * new ssk completes join, inherits options from s0 // seq 2
	 * Needs sync from mptcp join logic, but ssk->seq == msk->seq
	 *
	 * Set High order bits to sk_state so ssk->seq == msk->seq test
	 * will fail.
	 */

	return (u32)sk->sk_state << 24u;
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

static bool mptcp_supported_sockopt(int level, int optname)
{
	if (level == SOL_SOCKET) {
		switch (optname) {
		case SO_DEBUG:
		case SO_REUSEPORT:
		case SO_REUSEADDR:

		/* the following ones need a better implementation,
		 * but are quite common we want to preserve them
		 */
		case SO_BINDTODEVICE:
		case SO_SNDBUF:
		case SO_SNDBUFFORCE:
		case SO_RCVBUF:
		case SO_RCVBUFFORCE:
		case SO_KEEPALIVE:
		case SO_PRIORITY:
		case SO_LINGER:
		case SO_TIMESTAMP_OLD:
		case SO_TIMESTAMP_NEW:
		case SO_TIMESTAMPNS_OLD:
		case SO_TIMESTAMPNS_NEW:
		case SO_TIMESTAMPING_OLD:
		case SO_TIMESTAMPING_NEW:
		case SO_RCVLOWAT:
		case SO_RCVTIMEO_OLD:
		case SO_RCVTIMEO_NEW:
		case SO_SNDTIMEO_OLD:
		case SO_SNDTIMEO_NEW:
		case SO_MARK:
		case SO_INCOMING_CPU:
		case SO_BINDTOIFINDEX:
		case SO_BUSY_POLL:
		case SO_PREFER_BUSY_POLL:
		case SO_BUSY_POLL_BUDGET:

		/* next ones are no-op for plain TCP */
		case SO_NO_CHECK:
		case SO_DONTROUTE:
		case SO_BROADCAST:
		case SO_BSDCOMPAT:
		case SO_PASSCRED:
		case SO_PASSSEC:
		case SO_RXQ_OVFL:
		case SO_WIFI_STATUS:
		case SO_NOFCS:
		case SO_SELECT_ERR_QUEUE:
			return true;
		}

		/* SO_OOBINLINE is not supported, let's avoid the related mess */
		/* SO_ATTACH_FILTER, SO_ATTACH_BPF, SO_ATTACH_REUSEPORT_CBPF,
		 * SO_DETACH_REUSEPORT_BPF, SO_DETACH_FILTER, SO_LOCK_FILTER,
		 * we must be careful with subflows
		 */
		/* SO_ATTACH_REUSEPORT_EBPF is not supported, at it checks
		 * explicitly the sk_protocol field
		 */
		/* SO_PEEK_OFF is unsupported, as it is for plain TCP */
		/* SO_MAX_PACING_RATE is unsupported, we must be careful with subflows */
		/* SO_CNX_ADVICE is currently unsupported, could possibly be relevant,
		 * but likely needs careful design
		 */
		/* SO_ZEROCOPY is currently unsupported, TODO in sndmsg */
		/* SO_TXTIME is currently unsupported */
		return false;
	}
	if (level == SOL_IP) {
		switch (optname) {
		/* should work fine */
		case IP_FREEBIND:
		case IP_TRANSPARENT:

		/* the following are control cmsg related */
		case IP_PKTINFO:
		case IP_RECVTTL:
		case IP_RECVTOS:
		case IP_RECVOPTS:
		case IP_RETOPTS:
		case IP_PASSSEC:
		case IP_RECVORIGDSTADDR:
		case IP_CHECKSUM:
		case IP_RECVFRAGSIZE:

		/* common stuff that need some love */
		case IP_TOS:
		case IP_TTL:
		case IP_BIND_ADDRESS_NO_PORT:
		case IP_MTU_DISCOVER:
		case IP_RECVERR:

		/* possibly less common may deserve some love */
		case IP_MINTTL:

		/* the following is apparently a no-op for plain TCP */
		case IP_RECVERR_RFC4884:
			return true;
		}

		/* IP_OPTIONS is not supported, needs subflow care */
		/* IP_HDRINCL, IP_NODEFRAG are not supported, RAW specific */
		/* IP_MULTICAST_TTL, IP_MULTICAST_LOOP, IP_UNICAST_IF,
		 * IP_ADD_MEMBERSHIP, IP_ADD_SOURCE_MEMBERSHIP, IP_DROP_MEMBERSHIP,
		 * IP_DROP_SOURCE_MEMBERSHIP, IP_BLOCK_SOURCE, IP_UNBLOCK_SOURCE,
		 * MCAST_JOIN_GROUP, MCAST_LEAVE_GROUP MCAST_JOIN_SOURCE_GROUP,
		 * MCAST_LEAVE_SOURCE_GROUP, MCAST_BLOCK_SOURCE, MCAST_UNBLOCK_SOURCE,
		 * MCAST_MSFILTER, IP_MULTICAST_ALL are not supported, better not deal
		 * with mcast stuff
		 */
		/* IP_IPSEC_POLICY, IP_XFRM_POLICY are nut supported, unrelated here */
		return false;
	}
	if (level == SOL_IPV6) {
		switch (optname) {
		case IPV6_V6ONLY:

		/* the following are control cmsg related */
		case IPV6_RECVPKTINFO:
		case IPV6_2292PKTINFO:
		case IPV6_RECVHOPLIMIT:
		case IPV6_2292HOPLIMIT:
		case IPV6_RECVRTHDR:
		case IPV6_2292RTHDR:
		case IPV6_RECVHOPOPTS:
		case IPV6_2292HOPOPTS:
		case IPV6_RECVDSTOPTS:
		case IPV6_2292DSTOPTS:
		case IPV6_RECVTCLASS:
		case IPV6_FLOWINFO:
		case IPV6_RECVPATHMTU:
		case IPV6_RECVORIGDSTADDR:
		case IPV6_RECVFRAGSIZE:

		/* the following ones need some love but are quite common */
		case IPV6_TCLASS:
		case IPV6_TRANSPARENT:
		case IPV6_FREEBIND:
		case IPV6_PKTINFO:
		case IPV6_2292PKTOPTIONS:
		case IPV6_UNICAST_HOPS:
		case IPV6_MTU_DISCOVER:
		case IPV6_MTU:
		case IPV6_RECVERR:
		case IPV6_FLOWINFO_SEND:
		case IPV6_FLOWLABEL_MGR:
		case IPV6_MINHOPCOUNT:
		case IPV6_DONTFRAG:
		case IPV6_AUTOFLOWLABEL:

		/* the following one is a no-op for plain TCP */
		case IPV6_RECVERR_RFC4884:
			return true;
		}

		/* IPV6_HOPOPTS, IPV6_RTHDRDSTOPTS, IPV6_RTHDR, IPV6_DSTOPTS are
		 * not supported
		 */
		/* IPV6_MULTICAST_HOPS, IPV6_MULTICAST_LOOP, IPV6_UNICAST_IF,
		 * IPV6_MULTICAST_IF, IPV6_ADDRFORM,
		 * IPV6_ADD_MEMBERSHIP, IPV6_DROP_MEMBERSHIP, IPV6_JOIN_ANYCAST,
		 * IPV6_LEAVE_ANYCAST, IPV6_MULTICAST_ALL, MCAST_JOIN_GROUP, MCAST_LEAVE_GROUP,
		 * MCAST_JOIN_SOURCE_GROUP, MCAST_LEAVE_SOURCE_GROUP,
		 * MCAST_BLOCK_SOURCE, MCAST_UNBLOCK_SOURCE, MCAST_MSFILTER
		 * are not supported better not deal with mcast
		 */
		/* IPV6_ROUTER_ALERT, IPV6_ROUTER_ALERT_ISOLATE are not supported, since are evil */

		/* IPV6_IPSEC_POLICY, IPV6_XFRM_POLICY are not supported */
		/* IPV6_ADDR_PREFERENCES is not supported, we must be careful with subflows */
		return false;
	}
	if (level == SOL_TCP) {
		switch (optname) {
		/* the following are no-op or should work just fine */
		case TCP_THIN_DUPACK:
		case TCP_DEFER_ACCEPT:

		/* the following need some love */
		case TCP_MAXSEG:
		case TCP_NODELAY:
		case TCP_THIN_LINEAR_TIMEOUTS:
		case TCP_CONGESTION:
		case TCP_ULP:
		case TCP_CORK:
		case TCP_KEEPIDLE:
		case TCP_KEEPINTVL:
		case TCP_KEEPCNT:
		case TCP_SYNCNT:
		case TCP_SAVE_SYN:
		case TCP_LINGER2:
		case TCP_WINDOW_CLAMP:
		case TCP_QUICKACK:
		case TCP_USER_TIMEOUT:
		case TCP_TIMESTAMP:
		case TCP_NOTSENT_LOWAT:
		case TCP_TX_DELAY:
			return true;
		}

		/* TCP_MD5SIG, TCP_MD5SIG_EXT are not supported, MD5 is not compatible with MPTCP */

		/* TCP_REPAIR, TCP_REPAIR_QUEUE, TCP_QUEUE_SEQ, TCP_REPAIR_OPTIONS,
		 * TCP_REPAIR_WINDOW are not supported, better avoid this mess
		 */
		/* TCP_FASTOPEN_KEY, TCP_FASTOPEN TCP_FASTOPEN_CONNECT, TCP_FASTOPEN_NO_COOKIE,
		 * are not supported fastopen is currently unsupported
		 */
		/* TCP_INQ is currently unsupported, needs some recvmsg work */
	}
	return false;
}

int mptcp_setsockopt(struct sock *sk, int level, int optname,
		     sockptr_t optval, unsigned int optlen)
{
	struct mptcp_sock *msk = mptcp_sk(sk);
	struct sock *ssk;

	pr_debug("msk=%p", msk);

	if (!mptcp_supported_sockopt(level, optname))
		return -ENOPROTOOPT;

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

static void __mptcp_sockopt_sync(struct mptcp_sock *msk, struct sock *ssk)
{
}

void mptcp_sockopt_sync(struct mptcp_sock *msk, struct sock *ssk)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(ssk);

	msk_owned_by_me(msk);

	if (READ_ONCE(subflow->setsockopt_seq) != msk->setsockopt_seq) {
		__mptcp_sockopt_sync(msk, ssk);

		subflow->setsockopt_seq = msk->setsockopt_seq;
	}
}

void mptcp_sockopt_sync_all(struct mptcp_sock *msk)
{
	struct mptcp_subflow_context *subflow;
	struct sock *sk = (struct sock *)msk;
	u32 seq;

	seq = sockopt_seq_reset(sk);

	mptcp_for_each_subflow(msk, subflow) {
		struct sock *ssk = mptcp_subflow_tcp_sock(subflow);
		u32 sseq = READ_ONCE(subflow->setsockopt_seq);

		if (sseq != msk->setsockopt_seq) {
			__mptcp_sockopt_sync(msk, ssk);
			WRITE_ONCE(subflow->setsockopt_seq, seq);
		} else if (sseq != seq) {
			WRITE_ONCE(subflow->setsockopt_seq, seq);
		}

		cond_resched();
	}

	msk->setsockopt_seq = seq;
}
