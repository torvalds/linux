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

int tcp_v6_parse_ao(struct sock *sk, int cmd,
		    sockptr_t optval, int optlen)
{
	return tcp_parse_ao(sk, cmd, AF_INET6, optval, optlen);
}
