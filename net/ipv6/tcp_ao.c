// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * INET		An implementation of the TCP Authentication Option (TCP-AO).
 *		See RFC5925.
 *
 * Authors:	Dmitry Safonov <dima@arista.com>
 *		Francesco Ruggeri <fruggeri@arista.com>
 *		Salam Noureddine <noureddine@arista.com>
 */
#include <linux/tcp.h>

#include <net/tcp.h>
#include <net/ipv6.h>

static struct tcp_ao_key *tcp_v6_ao_do_lookup(const struct sock *sk,
					      const struct in6_addr *addr,
					      int sndid, int rcvid)
{
	return tcp_ao_do_lookup(sk, (union tcp_ao_addr *)addr, AF_INET6,
				sndid, rcvid);
}

struct tcp_ao_key *tcp_v6_ao_lookup(const struct sock *sk,
				    struct sock *addr_sk,
				    int sndid, int rcvid)
{
	struct in6_addr *addr = &addr_sk->sk_v6_daddr;

	return tcp_v6_ao_do_lookup(sk, addr, sndid, rcvid);
}

int tcp_v6_parse_ao(struct sock *sk, int cmd,
		    sockptr_t optval, int optlen)
{
	return tcp_parse_ao(sk, cmd, AF_INET6, optval, optlen);
}
