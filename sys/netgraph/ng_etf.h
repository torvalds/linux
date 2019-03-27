/*-
 * ng_etf.h
 */

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001, FreeBSD Incorporated 
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
 * Author: Julian Elischer <julian@freebsd.org>
 *
 * $FreeBSD$
 */

#ifndef _NETGRAPH_NG_ETF_H_
#define _NETGRAPH_NG_ETF_H_

/* Node type name. This should be unique among all netgraph node types */
#define NG_ETF_NODE_TYPE	"etf"

/* Node type cookie. Should also be unique. This value MUST change whenever
   an incompatible change is made to this header file, to insure consistency.
   The de facto method for generating cookies is to take the output of the
   date command: date -u +'%s' */
#define NGM_ETF_COOKIE		983084516

/* Hook names */
#define NG_ETF_HOOK_DOWNSTREAM	"downstream"
#define NG_ETF_HOOK_NOMATCH	"nomatch"

/* Netgraph commands understood by this node type */
enum {
	NGM_ETF_SET_FLAG = 1,
	NGM_ETF_GET_STATUS,
	NGM_ETF_SET_FILTER,

};

/* This structure is returned by the NGM_ETF_GET_STATUS command */
struct ng_etfstat {
	u_int32_t   packets_in;		/* packets in from downstream */
	u_int32_t   packets_out;	/* packets out towards downstream */
};

/*
 * This needs to be kept in sync with the above structure definition
 */
#define NG_ETF_STATS_TYPE_INFO	{				\
	  { "packets_in",	&ng_parse_uint32_type	},	\
	  { "packets_out",	&ng_parse_uint32_type	},	\
	  { NULL }						\
}

/* This structure is returned by the NGM_ETF_GET_STATUS command */
struct ng_etffilter {
	char		matchhook[NG_HOOKSIZ]; /* hook name */
	u_int16_t	ethertype;	/* this ethertype to this hook */
};	

/*
 * This needs to be kept in sync with the above structure definition
 */
#define NG_ETF_FILTER_TYPE_INFO	{				\
          { "matchhook",	&ng_parse_hookbuf_type  },	\
	  { "ethertype",	&ng_parse_uint16_type   },	\
	  { NULL }						\
}

#endif /* _NETGRAPH_NG_ETF_H_ */
