/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004-2008 University of Zagreb
 * Copyright (c) 2007-2008 FreeBSD Foundation
 *
 * This software was developed by the University of Zagreb and the
 * FreeBSD Foundation under sponsorship by the Stichting NLnet and the
 * FreeBSD Foundation.
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

#ifndef _NETGRAPH_PIPE_H_
#define _NETGRAPH_PIPE_H_

/* Node type name and magic cookie */
#define NG_PIPE_NODE_TYPE	"pipe"
#define NGM_PIPE_COOKIE		200708191

/* Hook names */
#define NG_PIPE_HOOK_UPPER	"upper"
#define NG_PIPE_HOOK_LOWER	"lower"

#define MAX_FSIZE 16384	/* Largest supported frame size, in bytes, for BER */
#define MAX_OHSIZE 256	/* Largest supported dummy-framing size, in bytes */

/* Statistics structure for one hook */
struct ng_pipe_hookstat {
	u_int64_t		fwd_octets;
	u_int64_t		fwd_frames;
	u_int64_t		in_disc_octets;
	u_int64_t		in_disc_frames;
	u_int64_t		out_disc_octets;
	u_int64_t		out_disc_frames;
};

/* Keep this in sync with the above structure definition */
#define NG_PIPE_HOOKSTAT_INFO	{					\
	{ "FwdOctets",		&ng_parse_uint64_type	},		\
	{ "FwdFrames",		&ng_parse_uint64_type	},		\
	{ "queueDropOctets",	&ng_parse_uint64_type	},		\
	{ "queueDropFrames",	&ng_parse_uint64_type	},		\
	{ "delayDropOctets",	&ng_parse_uint64_type	},		\
	{ "delayDropFrames",	&ng_parse_uint64_type	},		\
	{ NULL },							\
}

/* Statistics structure returned by NGM_PIPE_GET_STATS */
struct ng_pipe_stats {
	struct ng_pipe_hookstat	downstream;
	struct ng_pipe_hookstat	upstream;
};

/* Keep this in sync with the above structure definition */
#define NG_PIPE_STATS_INFO(hstype)	{				\
	{ "downstream",		(hstype) },				\
	{ "upstream",		(hstype) },				\
	{ NULL },							\
}

/* Runtime structure for one hook */
struct ng_pipe_hookrun {
	u_int32_t		fifo_queues;
	u_int32_t		qin_octets;
	u_int32_t		qin_frames;
	u_int32_t		qout_octets;
	u_int32_t		qout_frames;
};

/* Keep this in sync with the above structure definition */
#define NG_PIPE_HOOKRUN_INFO	{					\
	{ "queues",		&ng_parse_uint32_type	},		\
	{ "queuedOctets",	&ng_parse_uint32_type	},		\
	{ "queuedFrames",	&ng_parse_uint32_type	},		\
	{ "delayedOctets",	&ng_parse_uint32_type	},		\
	{ "delayedFrames",	&ng_parse_uint32_type	},		\
	{ NULL },							\
}

/* Runtime structure returned by NGM_PIPE_GET_RUN */
struct ng_pipe_run {
	struct ng_pipe_hookrun	downstream;
	struct ng_pipe_hookrun	upstream;
};

/* Keep this in sync with the above structure definition */
#define NG_PIPE_RUN_INFO(hstype)	{				\
	{ "downstream",		(hstype) },				\
	{ "upstream",		(hstype) },				\
	{ NULL },							\
}

/* Config structure for one hook */
struct ng_pipe_hookcfg {
	u_int64_t		bandwidth;
	u_int64_t		ber;
	u_int32_t		qin_size_limit;
	u_int32_t		qout_size_limit;
	u_int32_t		duplicate;
	u_int32_t		fifo;
	u_int32_t		drr;
	u_int32_t		wfq;
	u_int32_t		droptail;
	u_int32_t		drophead;
};

/* Keep this in sync with the above structure definition */
#define NG_PIPE_HOOKCFG_INFO	{					\
	{ "bandwidth",		&ng_parse_uint64_type	},		\
	{ "BER",		&ng_parse_uint64_type	},		\
	{ "queuelen",		&ng_parse_uint32_type	},		\
	{ "delaylen",		&ng_parse_uint32_type	},		\
	{ "duplicate",		&ng_parse_uint32_type	},		\
	{ "fifo",		&ng_parse_uint32_type	},		\
	{ "drr",		&ng_parse_uint32_type	},		\
	{ "wfq",		&ng_parse_uint32_type	},		\
	{ "droptail",		&ng_parse_uint32_type	},		\
	{ "drophead",		&ng_parse_uint32_type	},		\
	{ NULL },							\
}

/* Config structure returned by NGM_PIPE_GET_CFG */
struct ng_pipe_cfg {
	u_int64_t		bandwidth;
	u_int64_t		delay;
	u_int32_t		header_offset;
	u_int32_t		overhead;
	struct ng_pipe_hookcfg	downstream;
	struct ng_pipe_hookcfg	upstream;
};

/* Keep this in sync with the above structure definition */
#define NG_PIPE_CFG_INFO(hstype)	{				\
	{ "bandwidth",		&ng_parse_uint64_type	},		\
	{ "delay",		&ng_parse_uint64_type	},		\
	{ "header_offset",	&ng_parse_uint32_type	},		\
	{ "overhead",		&ng_parse_uint32_type	},		\
	{ "downstream",		(hstype)		},		\
	{ "upstream",		(hstype)		},		\
	{ NULL },							\
}

/* Netgraph commands */
enum {
	NGM_PIPE_GET_STATS=1,		/* get stats */
	NGM_PIPE_CLR_STATS,		/* clear stats */
	NGM_PIPE_GETCLR_STATS,		/* atomically get and clear stats */
	NGM_PIPE_GET_RUN,		/* get current runtime status */
	NGM_PIPE_GET_CFG,		/* get configurable parameters */
	NGM_PIPE_SET_CFG,		/* set configurable parameters */
};

#endif /* _NETGRAPH_PIPE_H_ */
