/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Maxim Ignatenko <gelraen.ua@gmail.com>
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

#ifndef _NETGRAPH_NG_PATCH_H_
#define _NETGRAPH_NG_PATCH_H_

/* Node type name. */
#define	NG_PATCH_NODE_TYPE	"patch"

/* Node type cookie. */
#define	NGM_PATCH_COOKIE	1262445509

/* Hook names */
#define	NG_PATCH_HOOK_IN	"in"
#define	NG_PATCH_HOOK_OUT	"out"

/* Checksum flags */
#define NG_PATCH_CSUM_IPV4	(CSUM_IP|CSUM_TCP|CSUM_UDP|CSUM_SCTP)
#define NG_PATCH_CSUM_IPV6	(CSUM_TCP_IPV6|CSUM_UDP_IPV6|CSUM_SCTP_IPV6)

/* Netgraph commands understood by this node type */
enum {
	NGM_PATCH_SETCONFIG = 1,
	NGM_PATCH_GETCONFIG,
	NGM_PATCH_GET_STATS,
	NGM_PATCH_CLR_STATS,
	NGM_PATCH_GETCLR_STATS,
	NGM_PATCH_GETDLT,
	NGM_PATCH_SETDLT
};

/* Patching modes */
enum {
	NG_PATCH_MODE_SET = 1,
	NG_PATCH_MODE_ADD = 2,
	NG_PATCH_MODE_SUB = 3,
	NG_PATCH_MODE_MUL = 4,
	NG_PATCH_MODE_DIV = 5,
	NG_PATCH_MODE_NEG = 6,
	NG_PATCH_MODE_AND = 7,
	NG_PATCH_MODE_OR  = 8,
	NG_PATCH_MODE_XOR = 9,
	NG_PATCH_MODE_SHL = 10,
	NG_PATCH_MODE_SHR = 11
};

/* Parsing declarations */

#define	NG_PATCH_CONFIG_TYPE {						\
	{ "count",		&ng_parse_uint32_type		},	\
	{ "csum_flags",		&ng_parse_uint64_type		},	\
	{ "relative_offset",	&ng_parse_uint32_type		},	\
	{ "ops",		&ng_patch_ops_array_type	},	\
	{ NULL }							\
}

#define	NG_PATCH_OP_TYPE {				\
	{ "offset",	&ng_parse_uint32_type	},	\
	{ "length",	&ng_parse_uint16_type	},	\
	{ "mode",	&ng_parse_uint16_type	},	\
	{ "value",	&ng_parse_uint64_type	},	\
	{ NULL }					\
}

#define	NG_PATCH_STATS_TYPE {				\
	{ "Received",	&ng_parse_uint64_type	},	\
	{ "Patched",	&ng_parse_uint64_type	},	\
	{ "Dropped",	&ng_parse_uint64_type	},	\
	{ NULL }					\
}

union ng_patch_op_val {
	uint8_t		v1;
	uint16_t	v2;
	uint32_t	v4;
	uint64_t	v8;
};

struct ng_patch_op {
	uint32_t	offset;
	uint16_t	length;	/* 1, 2, 4 or 8 (bytes) */
	uint16_t	mode;
	union ng_patch_op_val val;
};

struct ng_patch_config {
	uint32_t	count;
	uint64_t	csum_flags;
	uint32_t	relative_offset;
	struct ng_patch_op ops[];
};

struct ng_patch_stats {
	uint64_t	received;
	uint64_t	patched;
	uint64_t	dropped;
};

struct ng_patch_vlan_header {
	u_int16_t tag;
	u_int16_t etype;
};

#define NG_PATCH_CONF_SIZE(count) (sizeof(struct ng_patch_config) + \
	(count) * sizeof(struct ng_patch_op))

#endif /* _NETGRAPH_NG_PATCH_H_ */
