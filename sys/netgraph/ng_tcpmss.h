/*-
 * ng_tcpmss.h
 *
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004, Alexey Popov <lollypop@flexuser.ru>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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

#ifndef _NETGRAPH_TCPMSS_H_
#define _NETGRAPH_TCPMSS_H_

/* Node type name and magic cookie */
#define NG_TCPMSS_NODE_TYPE	"tcpmss"
#define NGM_TCPMSS_COOKIE	1097623478

/* Statistics structure for one hook. */
struct ng_tcpmss_hookstat {
	uint64_t	Octets;
	uint64_t	Packets;
	uint16_t	maxMSS;
	uint64_t	SYNPkts;
	uint64_t	FixedPkts;
};

/* Keep this in sync with the above structure definition. */
#define NG_TCPMSS_HOOKSTAT_INFO	{			\
	{ "Octets",	&ng_parse_uint64_type	},	\
	{ "Packets",	&ng_parse_uint64_type	},	\
	{ "maxMSS",	&ng_parse_uint16_type	},	\
	{ "SYNPkts",	&ng_parse_uint64_type	},	\
	{ "FixedPkts",	&ng_parse_uint64_type	},	\
	{ NULL }					\
}


/* Structure for NGM_TCPMSS_CONFIG. */
struct ng_tcpmss_config {
	char		inHook[NG_HOOKSIZ];
	char		outHook[NG_HOOKSIZ];
	uint16_t	maxMSS;
};

/* Keep this in sync with the above structure definition. */
#define NG_TCPMSS_CONFIG_INFO {				\
	{ "inHook",	&ng_parse_hookbuf_type	},	\
	{ "outHook",	&ng_parse_hookbuf_type	},	\
	{ "maxMSS",	&ng_parse_uint16_type	},	\
	{ NULL }					\
}

/* Netgraph commands */
enum {
	NGM_TCPMSS_GET_STATS = 1,	/* Get stats. */
	NGM_TCPMSS_CLR_STATS,		/* Clear stats. */
	NGM_TCPMSS_GETCLR_STATS,	/* "Atomically" get and clear stats. */
	NGM_TCPMSS_CONFIG		/* Set configuration. */
};

#endif /* _NETGRAPH_TCPMSS_H_ */
