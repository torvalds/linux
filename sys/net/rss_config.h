/*-
 * Copyright (c) 2010-2011 Juniper Networks, Inc.
 * All rights reserved.
 *
 * This software was developed by Robert N. M. Watson under contract
 * to Juniper Networks, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _NET_RSS_CONFIG_H_
#define	_NET_RSS_CONFIG_H_

#include <netinet/in.h>		/* in_addr_t */

/*
 * Supported RSS hash functions.
 */
#define	RSS_HASH_NAIVE		0x00000001	/* Poor but fast hash. */
#define	RSS_HASH_TOEPLITZ	0x00000002	/* Required by RSS. */
#define	RSS_HASH_CRC32		0x00000004	/* Future; some NICs do it. */

#define	RSS_HASH_MASK		(RSS_HASH_NAIVE | RSS_HASH_TOEPLITZ)

/*
 * Instances of struct inpcbinfo declare an RSS hash type indicating what
 * header fields are covered.
 */
#define	RSS_HASHFIELDS_NONE		0
#define	RSS_HASHFIELDS_4TUPLE		1
#define	RSS_HASHFIELDS_2TUPLE		2

/*
 * Define RSS representations of the M_HASHTYPE_* values, representing
 * which particular bits are supported.  The NICs can then use this to
 * calculate which hash types to enable and which not to enable.
 *
 * The fact that these line up with M_HASHTYPE_* is not to be relied
 * upon.
 */
#define	RSS_HASHTYPE_RSS_IPV4		(1 << 1)	/* IPv4 2-tuple */
#define	RSS_HASHTYPE_RSS_TCP_IPV4	(1 << 2)	/* TCPv4 4-tuple */
#define	RSS_HASHTYPE_RSS_IPV6		(1 << 3)	/* IPv6 2-tuple */
#define	RSS_HASHTYPE_RSS_TCP_IPV6	(1 << 4)	/* TCPv6 4-tuple */
#define	RSS_HASHTYPE_RSS_IPV6_EX	(1 << 5)	/* IPv6 2-tuple + ext hdrs */
#define	RSS_HASHTYPE_RSS_TCP_IPV6_EX	(1 << 6)	/* TCPv6 4-tiple + ext hdrs */
#define	RSS_HASHTYPE_RSS_UDP_IPV4	(1 << 7)	/* IPv4 UDP 4-tuple */
#define	RSS_HASHTYPE_RSS_UDP_IPV6	(1 << 9)	/* IPv6 UDP 4-tuple */
#define	RSS_HASHTYPE_RSS_UDP_IPV6_EX	(1 << 10)	/* IPv6 UDP 4-tuple + ext hdrs */

/*
 * Compile-time limits on the size of the indirection table.
 */
#define	RSS_MAXBITS	7
#define	RSS_TABLE_MAXLEN	(1 << RSS_MAXBITS)

/*
 * Maximum key size used throughout.  It's OK for hardware to use only the
 * first 16 bytes, which is all that's required for IPv4.
 */
#define	RSS_KEYSIZE	40

/*
 * For RSS hash methods that do a software hash on an mbuf, the packet
 * direction (ingress / egress) is required.
 *
 * The default direction (INGRESS) is the "receive into the NIC" - ie,
 * what the hardware is hashing on.
 */
#define	RSS_HASH_PKT_INGRESS	0
#define	RSS_HASH_PKT_EGRESS	1

/*
 * Rate limited debugging routines.
 */
#define	RSS_DEBUG(format, ...)	do {					\
	if (rss_debug) {						\
		static struct timeval lastfail;				\
		static int curfail;					\
		if (ppsratecheck(&lastfail, &curfail, 5))		\
			printf("RSS (%s:%u): " format, __func__, __LINE__,\
			    ##__VA_ARGS__);				\
	}								\
} while (0)

extern int	rss_debug;

/*
 * Device driver interfaces to query RSS properties that must be programmed
 * into hardware.
 */
u_int	rss_getbits(void);
u_int	rss_getbucket(u_int hash);
u_int	rss_get_indirection_to_bucket(u_int index);
u_int	rss_getcpu(u_int bucket);
void	rss_getkey(uint8_t *key);
u_int	rss_gethashalgo(void);
u_int	rss_getnumbuckets(void);
u_int	rss_getnumcpus(void);
u_int	rss_gethashconfig(void);

/*
 * Hash calculation functions.
 */
uint32_t	rss_hash(u_int datalen, const uint8_t *data);

/*
 * Network stack interface to query desired CPU affinity of a packet.
 */
struct mbuf * rss_m2cpuid(struct mbuf *m, uintptr_t source, u_int *cpuid);
u_int	rss_hash2cpuid(uint32_t hash_val, uint32_t hash_type);
int	rss_hash2bucket(uint32_t hash_val, uint32_t hash_type,
	    uint32_t *bucket_id);
int	rss_m2bucket(struct mbuf *m, uint32_t *bucket_id);

#endif /* !_NET_RSS_CONFIG_H_ */
