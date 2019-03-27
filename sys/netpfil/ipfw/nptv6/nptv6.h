/*-
 * Copyright (c) 2016 Yandex LLC
 * Copyright (c) 2016 Andrey V. Elsukov <ae@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef	_IP_FW_NPTV6_H_
#define	_IP_FW_NPTV6_H_

#include <netinet6/ip_fw_nptv6.h>

#ifdef _KERNEL
#define	NPTV6STATS	(sizeof(struct ipfw_nptv6_stats) / sizeof(uint64_t))
#define	NPTV6STAT_ADD(c, f, v)		\
    counter_u64_add((c)->stats[		\
	offsetof(struct ipfw_nptv6_stats, f) / sizeof(uint64_t)], (v))
#define	NPTV6STAT_INC(c, f)	NPTV6STAT_ADD(c, f, 1)
#define	NPTV6STAT_FETCH(c, f)		\
    counter_u64_fetch((c)->stats[	\
	offsetof(struct ipfw_nptv6_stats, f) / sizeof(uint64_t)])

struct nptv6_cfg {
	struct named_object	no;

	struct in6_addr		internal;   /* Internal IPv6 prefix */
	struct in6_addr		external;   /* External IPv6 prefix */
	struct in6_addr		mask;	    /* IPv6 prefix mask */
	uint16_t		adjustment; /* Checksum adjustment value */
	uint8_t			plen;	    /* Prefix length */
	uint8_t			flags;	    /* Flags for internal use */
#define	NPTV6_READY		0x80
#define	NPTV6_48PLEN		0x40

	char			if_name[IF_NAMESIZE];
	char			name[64];   /* Instance name */
	counter_u64_t		stats[NPTV6STATS]; /* Statistics counters */
};
#define	NPTV6_FLAGSMASK		(NPTV6_DYNAMIC_PREFIX)

int nptv6_init(struct ip_fw_chain *ch, int first);
void nptv6_uninit(struct ip_fw_chain *ch, int last);
#endif /* _KERNEL */

#endif /* _IP_FW_NPTV6_H_ */

