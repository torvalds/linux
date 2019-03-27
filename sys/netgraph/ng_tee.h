
/*
 * ng_tee.h
 */

/*-
 * Copyright (c) 1996-1999 Whistle Communications, Inc.
 * All rights reserved.
 * 
 * Subject to the following obligations and disclaimer of warranty, use and
 * redistribution of this software, in source or object code forms, with or
 * without modifications are expressly permitted by Whistle Communications;
 * provided, however, that:
 * 1. Any and all reproductions of the source or object code must include the
 *    copyright notice above and the following disclaimer of warranties; and
 * 2. No rights are granted, in any manner or form, to use Whistle
 *    Communications, Inc. trademarks, including the mark "WHISTLE
 *    COMMUNICATIONS" on advertising, endorsements, or otherwise except as
 *    such appears in the above copyright notice or in the software.
 * 
 * THIS SOFTWARE IS BEING PROVIDED BY WHISTLE COMMUNICATIONS "AS IS", AND
 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, WHISTLE COMMUNICATIONS MAKES NO
 * REPRESENTATIONS OR WARRANTIES, EXPRESS OR IMPLIED, REGARDING THIS SOFTWARE,
 * INCLUDING WITHOUT LIMITATION, ANY AND ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT.
 * WHISTLE COMMUNICATIONS DOES NOT WARRANT, GUARANTEE, OR MAKE ANY
 * REPRESENTATIONS REGARDING THE USE OF, OR THE RESULTS OF THE USE OF THIS
 * SOFTWARE IN TERMS OF ITS CORRECTNESS, ACCURACY, RELIABILITY OR OTHERWISE.
 * IN NO EVENT SHALL WHISTLE COMMUNICATIONS BE LIABLE FOR ANY DAMAGES
 * RESULTING FROM OR ARISING OUT OF ANY USE OF THIS SOFTWARE, INCLUDING
 * WITHOUT LIMITATION, ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * PUNITIVE, OR CONSEQUENTIAL DAMAGES, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES, LOSS OF USE, DATA OR PROFITS, HOWEVER CAUSED AND UNDER ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF WHISTLE COMMUNICATIONS IS ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * Author: Archie Cobbs <archie@freebsd.org>
 *
 * $FreeBSD$
 * $Whistle: ng_tee.h,v 1.2 1999/01/20 00:22:14 archie Exp $
 */

#ifndef _NETGRAPH_NG_TEE_H_
#define _NETGRAPH_NG_TEE_H_

/* Node type name and magic cookie */
#define NG_TEE_NODE_TYPE	"tee"
#define NGM_TEE_COOKIE		916107047

/* Hook names */
#define NG_TEE_HOOK_RIGHT	"right"
#define NG_TEE_HOOK_LEFT	"left"
#define NG_TEE_HOOK_RIGHT2LEFT	"right2left"
#define NG_TEE_HOOK_LEFT2RIGHT	"left2right"

/* Statistics structure for one hook */
struct ng_tee_hookstat {
	u_int64_t	inOctets;
	u_int64_t	inFrames;
	u_int64_t	outOctets;
	u_int64_t	outFrames;
};

/* Keep this in sync with the above structure definition */
#define NG_TEE_HOOKSTAT_INFO	{				\
	  { "inOctets",		&ng_parse_uint64_type	},	\
	  { "inFrames",		&ng_parse_uint64_type	},	\
	  { "outOctets",	&ng_parse_uint64_type	},	\
	  { "outFrames",	&ng_parse_uint64_type	},	\
	  { NULL }						\
}

/* Statistics structure returned by NGM_TEE_GET_STATS */
struct ng_tee_stats {
	struct ng_tee_hookstat	right;
	struct ng_tee_hookstat	left;
	struct ng_tee_hookstat	right2left;
	struct ng_tee_hookstat	left2right;
};

/* Keep this in sync with the above structure definition */
#define NG_TEE_STATS_INFO(hstype)	{			\
	  { "right",		(hstype)		},	\
	  { "left",		(hstype)		},	\
	  { "right2left",	(hstype)		},	\
	  { "left2right",	(hstype)		},	\
	  { NULL }						\
}

/* Netgraph commands */
enum {
	NGM_TEE_GET_STATS = 1,		/* get stats */
	NGM_TEE_CLR_STATS,		/* clear stats */
	NGM_TEE_GETCLR_STATS,		/* atomically get and clear stats */
};

#endif /* _NETGRAPH_NG_TEE_H_ */
