/*-
 * Copyright (c) 2015 Dmitry Vagin <daemon.hammer@ya.ru>
 * All rights reserved.
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

#ifndef _NETGRAPH_NG_CHECKSUM_H_
#define _NETGRAPH_NG_CHECKSUM_H_

/* Node type name. */
#define	NG_CHECKSUM_NODE_TYPE	"checksum"

/* Node type cookie. */
#define	NGM_CHECKSUM_COOKIE	439419912

/* Hook names */
#define	NG_CHECKSUM_HOOK_IN	"in"
#define	NG_CHECKSUM_HOOK_OUT	"out"

/* Checksum flags */
#define NG_CHECKSUM_CSUM_IPV4	(CSUM_IP|CSUM_TCP|CSUM_UDP)
#define NG_CHECKSUM_CSUM_IPV6	(CSUM_TCP_IPV6|CSUM_UDP_IPV6)

/* Netgraph commands understood by this node type */
enum {
	NGM_CHECKSUM_GETDLT = 1,
	NGM_CHECKSUM_SETDLT,
	NGM_CHECKSUM_GETCONFIG,
	NGM_CHECKSUM_SETCONFIG,
	NGM_CHECKSUM_GETCLR_STATS,
	NGM_CHECKSUM_GET_STATS,
	NGM_CHECKSUM_CLR_STATS,
};

/* Parsing declarations */

#define	NG_CHECKSUM_CONFIG_TYPE {				\
	{ "csum_flags",		&ng_parse_uint64_type	},	\
	{ "csum_offload",	&ng_parse_uint64_type	},	\
	{ NULL }						\
}

#define	NG_CHECKSUM_STATS_TYPE {				\
	{ "Received",		&ng_parse_uint64_type	},	\
	{ "Processed",		&ng_parse_uint64_type	},	\
	{ "Dropped",		&ng_parse_uint64_type	},	\
	{ NULL }					\
}

struct ng_checksum_config {
	uint64_t	csum_flags;
	uint64_t	csum_offload;
};

struct ng_checksum_stats {
	uint64_t	received;
	uint64_t	processed;
	uint64_t	dropped;
};

struct ng_checksum_vlan_header {
	u_int16_t tag;
	u_int16_t etype;
};

#endif /* _NETGRAPH_NG_CHECKSUM_H_ */
