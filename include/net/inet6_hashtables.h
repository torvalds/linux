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

#ifndef _INET6_HASHTABLES_H
#define _INET6_HASHTABLES_H

#include <linux/types.h>

struct in6_addr;
struct inet_hashinfo;

extern struct sock *inet6_lookup(struct inet_hashinfo *hashinfo,
				 const struct in6_addr *saddr, const u16 sport,
				 const struct in6_addr *daddr, const u16 dport,
				 const int dif);
#endif /* _INET6_HASHTABLES_H */
