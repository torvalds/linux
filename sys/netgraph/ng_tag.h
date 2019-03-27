/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 Vadim Goncharov <vadimnuclight@tpu.ru>
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

#ifndef _NETGRAPH_NG_TAG_H_
#define _NETGRAPH_NG_TAG_H_

/* Node type name and magic cookie. */
#define NG_TAG_NODE_TYPE	"tag"
#define NGM_TAG_COOKIE		1149771193

/*
 * The types of tag_cookie, tag_len and tag_id in structures below
 * must be the same as corresponding members m_tag_cookie, m_tag_len
 * and m_tag_id in struct m_tag (defined in <sys/mbuf.h>).
 */

/* Tag match structure for every (input) hook. */
struct ng_tag_hookin {
	char		thisHook[NG_HOOKSIZ];		/* name of hook */
	char		ifMatch[NG_HOOKSIZ];		/* match dest hook */
	char		ifNotMatch[NG_HOOKSIZ];		/* !match dest hook */
	uint8_t		strip;				/* strip tag if found */
	uint32_t	tag_cookie;			/* ABI/Module ID */
	uint16_t	tag_id;				/* tag ID */
	uint16_t	tag_len;			/* length of data */
	uint8_t		tag_data[0];			/* tag data */
};

/* Tag set structure for every (output) hook. */
struct ng_tag_hookout {
	char		thisHook[NG_HOOKSIZ];		/* name of hook */
	uint32_t	tag_cookie;			/* ABI/Module ID */
	uint16_t	tag_id;				/* tag ID */
	uint16_t	tag_len;			/* length of data */
	uint8_t		tag_data[0];			/* tag data */
};

#define NG_TAG_HOOKIN_SIZE(taglen)	\
	(sizeof(struct ng_tag_hookin) + (taglen))

#define NG_TAG_HOOKOUT_SIZE(taglen)	\
	(sizeof(struct ng_tag_hookout) + (taglen))

/* Keep this in sync with the above structures definitions. */
#define NG_TAG_HOOKIN_TYPE_INFO(tdtype)	{		\
	  { "thisHook",		&ng_parse_hookbuf_type	},	\
	  { "ifMatch",		&ng_parse_hookbuf_type	},	\
	  { "ifNotMatch",	&ng_parse_hookbuf_type	},	\
	  { "strip",		&ng_parse_uint8_type	},	\
	  { "tag_cookie",	&ng_parse_uint32_type	},	\
	  { "tag_id",		&ng_parse_uint16_type	},	\
	  { "tag_len",		&ng_parse_uint16_type	},	\
	  { "tag_data",		(tdtype)		},	\
	  { NULL }						\
}

#define NG_TAG_HOOKOUT_TYPE_INFO(tdtype)	{		\
	  { "thisHook",		&ng_parse_hookbuf_type	},	\
	  { "tag_cookie",	&ng_parse_uint32_type	},	\
	  { "tag_id",		&ng_parse_uint16_type	},	\
	  { "tag_len",		&ng_parse_uint16_type	},	\
	  { "tag_data",		(tdtype)		},	\
	  { NULL }						\
}

#ifdef NG_TAG_DEBUG

/* Statistics structure for one hook. */
struct ng_tag_hookstat {
	uint64_t	recvFrames;
	uint64_t	recvOctets;
	uint64_t	recvMatchFrames;
	uint64_t	recvMatchOctets;
	uint64_t	xmitFrames;
	uint64_t	xmitOctets;
};

/* Keep this in sync with the above structure definition. */
#define NG_TAG_HOOKSTAT_TYPE_INFO	{			\
	  { "recvFrames",	&ng_parse_uint64_type	},	\
	  { "recvOctets",	&ng_parse_uint64_type	},	\
	  { "recvMatchFrames",	&ng_parse_uint64_type	},	\
	  { "recvMatchOctets",	&ng_parse_uint64_type	},	\
	  { "xmitFrames",	&ng_parse_uint64_type	},	\
	  { "xmitOctets",	&ng_parse_uint64_type	},	\
	  { NULL }						\
}

#endif /* NG_TAG_DEBUG */

/* Netgraph commands. */
enum {
	NGM_TAG_SET_HOOKIN = 1,		/* supply a struct ng_tag_hookin */
	NGM_TAG_GET_HOOKIN,		/* returns a struct ng_tag_hookin */
	NGM_TAG_SET_HOOKOUT,		/* supply a struct ng_tag_hookout */
	NGM_TAG_GET_HOOKOUT,		/* returns a struct ng_tag_hookout */
#ifdef NG_TAG_DEBUG
	NGM_TAG_GET_STATS,		/* supply name as char[NG_HOOKSIZ] */
	NGM_TAG_CLR_STATS,		/* supply name as char[NG_HOOKSIZ] */
	NGM_TAG_GETCLR_STATS,		/* supply name as char[NG_HOOKSIZ] */
#endif
};

#endif /* _NETGRAPH_NG_TAG_H_ */
