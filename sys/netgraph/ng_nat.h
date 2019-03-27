/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 2005, Gleb Smirnoff <glebius@FreeBSD.org>
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

#define NG_NAT_NODE_TYPE    "nat"
#define NGM_NAT_COOKIE      1107718711

#define	NG_NAT_HOOK_IN	"in"
#define	NG_NAT_HOOK_OUT	"out"

/* Arguments for NGM_NAT_SET_MODE message */
struct ng_nat_mode {
	uint32_t	flags;
	uint32_t	mask;
};

/* Keep this in sync with the above structure definition */
#define NG_NAT_MODE_INFO {				\
	  { "flags",	&ng_parse_uint32_type	},	\
	  { "mask",	&ng_parse_uint32_type	},	\
	  { NULL }					\
}

#define NG_NAT_LOG			0x01
#define NG_NAT_DENY_INCOMING		0x02
#define NG_NAT_SAME_PORTS		0x04
#define NG_NAT_UNREGISTERED_ONLY	0x10
#define NG_NAT_RESET_ON_ADDR_CHANGE	0x20
#define NG_NAT_PROXY_ONLY		0x40
#define NG_NAT_REVERSE			0x80

#define NG_NAT_DESC_LENGTH	64
#define NG_NAT_REDIRPROTO_ADDR	(IPPROTO_MAX + 3) 	/* LibAlias' LINK_ADDR, also unused in in.h */

/* Arguments for NGM_NAT_REDIRECT_PORT message */
struct ng_nat_redirect_port {
	struct in_addr	local_addr;
	struct in_addr	alias_addr;
	struct in_addr	remote_addr;
	uint16_t	local_port;
	uint16_t	alias_port;
	uint16_t	remote_port;
	uint8_t		proto;
	char		description[NG_NAT_DESC_LENGTH];
};

/* Keep this in sync with the above structure definition */
#define NG_NAT_REDIRECT_PORT_TYPE_INFO(desctype) {		\
	  { "local_addr",	&ng_parse_ipaddr_type	},	\
	  { "alias_addr",	&ng_parse_ipaddr_type	},	\
	  { "remote_addr",	&ng_parse_ipaddr_type	},	\
	  { "local_port",	&ng_parse_uint16_type	},	\
	  { "alias_port",	&ng_parse_uint16_type	},	\
	  { "remote_port",	&ng_parse_uint16_type	},	\
	  { "proto",		&ng_parse_uint8_type	},	\
	  { "description",	(desctype)		},	\
	  { NULL }						\
}

/* Arguments for NGM_NAT_REDIRECT_ADDR message */
struct ng_nat_redirect_addr {
	struct in_addr	local_addr;
	struct in_addr	alias_addr;
	char		description[NG_NAT_DESC_LENGTH];
};

/* Keep this in sync with the above structure definition */
#define NG_NAT_REDIRECT_ADDR_TYPE_INFO(desctype) {		\
	  { "local_addr",	&ng_parse_ipaddr_type	},	\
	  { "alias_addr",	&ng_parse_ipaddr_type	},	\
	  { "description",	(desctype)		},	\
	  { NULL }						\
}

/* Arguments for NGM_NAT_REDIRECT_PROTO message */
struct ng_nat_redirect_proto {
	struct in_addr	local_addr;
	struct in_addr	alias_addr;
	struct in_addr	remote_addr;
	uint8_t		proto;
	char		description[NG_NAT_DESC_LENGTH];
};

/* Keep this in sync with the above structure definition */
#define NG_NAT_REDIRECT_PROTO_TYPE_INFO(desctype) {		\
	  { "local_addr",	&ng_parse_ipaddr_type	},	\
	  { "alias_addr",	&ng_parse_ipaddr_type	},	\
	  { "remote_addr",	&ng_parse_ipaddr_type	},	\
	  { "proto",		&ng_parse_uint8_type	},	\
	  { "description",	(desctype)		},	\
	  { NULL }						\
}

/* Arguments for NGM_NAT_ADD_SERVER message */
struct ng_nat_add_server {
	uint32_t	id;
	struct in_addr	addr;
	uint16_t	port;
};

/* Keep this in sync with the above structure definition */
#define NG_NAT_ADD_SERVER_TYPE_INFO {				\
	  { "id",		&ng_parse_uint32_type	},	\
	  { "addr",		&ng_parse_ipaddr_type	},	\
	  { "port",		&ng_parse_uint16_type	},	\
	  { NULL }						\
}

/* List entry of array returned in NGM_NAT_LIST_REDIRECTS message */
struct ng_nat_listrdrs_entry {
	uint32_t	id;		/* Anything except zero */
	struct in_addr	local_addr;
	struct in_addr	alias_addr;
	struct in_addr	remote_addr;
	uint16_t	local_port;
	uint16_t	alias_port;
	uint16_t	remote_port;
	uint16_t	proto;		/* Valid proto or NG_NAT_REDIRPROTO_ADDR */
	uint16_t	lsnat;		/* LSNAT servers count */
	char		description[NG_NAT_DESC_LENGTH];
};

/* Keep this in sync with the above structure definition */
#define NG_NAT_LISTRDRS_ENTRY_TYPE_INFO(desctype) {			\
	  { "id",		&ng_parse_uint32_type	},	\
	  { "local_addr",	&ng_parse_ipaddr_type	},	\
	  { "alias_addr",	&ng_parse_ipaddr_type	},	\
	  { "remote_addr",	&ng_parse_ipaddr_type	},	\
	  { "local_port",	&ng_parse_uint16_type	},	\
	  { "alias_port",	&ng_parse_uint16_type	},	\
	  { "remote_port",	&ng_parse_uint16_type	},	\
	  { "proto",		&ng_parse_uint16_type	},	\
	  { "lsnat",		&ng_parse_uint16_type	},	\
	  { "description",	(desctype)		},	\
	  { NULL }						\
}

/* Structure returned by NGM_NAT_LIST_REDIRECTS */
struct ng_nat_list_redirects {
	uint32_t		total_count;
	struct ng_nat_listrdrs_entry redirects[];
};

/* Keep this in sync with the above structure definition */
#define NG_NAT_LIST_REDIRECTS_TYPE_INFO(redirtype) {		\
	  { "total_count",	&ng_parse_uint32_type	},	\
	  { "redirects",	(redirtype)		},	\
	  { NULL }						\
}

/* Structure returned by NGM_NAT_LIBALIAS_INFO */
struct ng_nat_libalias_info {
	uint32_t	icmpLinkCount;
	uint32_t	udpLinkCount;
	uint32_t	tcpLinkCount;
	uint32_t	sctpLinkCount;
	uint32_t	pptpLinkCount;
	uint32_t	protoLinkCount;
	uint32_t	fragmentIdLinkCount;
	uint32_t	fragmentPtrLinkCount;
	uint32_t	sockCount;
};

/* Keep this in sync with the above structure definition */
#define NG_NAT_LIBALIAS_INFO {					\
	  { "icmpLinkCount",	&ng_parse_uint32_type	},	\
	  { "udpLinkCount",	&ng_parse_uint32_type	},	\
	  { "tcpLinkCount",	&ng_parse_uint32_type	},	\
	  { "sctpLinkCount",	&ng_parse_uint32_type	},	\
	  { "pptpLinkCount",	&ng_parse_uint32_type	},	\
	  { "protoLinkCount",	&ng_parse_uint32_type	},	\
	  { "fragmentIdLinkCount", &ng_parse_uint32_type },	\
	  { "fragmentPtrLinkCount", &ng_parse_uint32_type },	\
	  { "sockCount",	&ng_parse_uint32_type	},	\
	  { NULL }						\
}

enum {
	NGM_NAT_SET_IPADDR = 1,
	NGM_NAT_SET_MODE,
	NGM_NAT_SET_TARGET,
	NGM_NAT_SET_DLT,
	NGM_NAT_GET_DLT,
	NGM_NAT_REDIRECT_PORT,
	NGM_NAT_REDIRECT_ADDR,
	NGM_NAT_REDIRECT_PROTO,
	NGM_NAT_REDIRECT_DYNAMIC,
	NGM_NAT_REDIRECT_DELETE,
	NGM_NAT_ADD_SERVER,
	NGM_NAT_LIST_REDIRECTS,
	NGM_NAT_PROXY_RULE,
	NGM_NAT_LIBALIAS_INFO,
};
