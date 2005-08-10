/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the BSD Socket
 *		interface as the means of communication with the user level.
 *
 * Authors:	Lotsa people, from code originally in tcp
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#ifndef _INET_HASHTABLES_H
#define _INET_HASHTABLES_H

#include <linux/types.h>

static inline int inet_ehashfn(const __u32 laddr, const __u16 lport,
			       const __u32 faddr, const __u16 fport,
			       const int ehash_size)
{
	int h = (laddr ^ lport) ^ (faddr ^ fport);
	h ^= h >> 16;
	h ^= h >> 8;
	return h & (ehash_size - 1);
}

static inline int inet_sk_ehashfn(const struct sock *sk, const int ehash_size)
{
	const struct inet_sock *inet = inet_sk(sk);
	const __u32 laddr = inet->rcv_saddr;
	const __u16 lport = inet->num;
	const __u32 faddr = inet->daddr;
	const __u16 fport = inet->dport;

	return inet_ehashfn(laddr, lport, faddr, fport, ehash_size);
}

#endif /* _INET_HASHTABLES_H */
