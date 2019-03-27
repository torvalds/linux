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

#ifndef	_NETINET6_IP_FW_NPTV6_H_
#define	_NETINET6_IP_FW_NPTV6_H_

struct ipfw_nptv6_stats {
	uint64_t	in2ex;		/* Int->Ext packets translated */
	uint64_t	ex2in;		/* Ext->Int packets translated */
	uint64_t	dropped;	/* dropped due to some errors */
	uint64_t	reserved[5];
};

typedef struct _ipfw_nptv6_cfg {
	char		name[64];	/* NPTv6 instance name */
	struct in6_addr	internal;	/* NPTv6 internal prefix */
	union {
		struct in6_addr	external; /* NPTv6 external prefix */
		char	if_name[IF_NAMESIZE];
	};
	uint8_t		plen;		/* Prefix length */
	uint8_t		set;		/* Named instance set [0..31] */
	uint8_t		spare[2];
	uint32_t	flags;
#define	NPTV6_DYNAMIC_PREFIX	1	/* Use dynamic external prefix */
} ipfw_nptv6_cfg;

#endif /* _NETINET6_IP_FW_NPTV6_H_ */

