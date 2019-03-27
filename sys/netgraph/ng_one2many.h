/*
 * ng_one2many.h
 */

/*-
 * Copyright (c) 2000 Whistle Communications, Inc.
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
 */

#ifndef _NETGRAPH_NG_ONE2MANY_H_
#define _NETGRAPH_NG_ONE2MANY_H_

/* Node type name and magic cookie */
#define NG_ONE2MANY_NODE_TYPE		"one2many"
#define NGM_ONE2MANY_COOKIE		1100897444

/* Hook names */
#define NG_ONE2MANY_HOOK_ONE		"one"
#define NG_ONE2MANY_HOOK_MANY_PREFIX	"many"	 /* append decimal integer */
#define NG_ONE2MANY_HOOK_MANY_FMT	"many%d" /* for use with printf(3) */

/* Maximum number of supported "many" links */
#define NG_ONE2MANY_MAX_LINKS		64

/* Link number used to indicate the "one" hook */
#define NG_ONE2MANY_ONE_LINKNUM		(-1)

/* Algorithms for outgoing packet distribution (XXX only one so far) */
#define NG_ONE2MANY_XMIT_ROUNDROBIN	1	/* round-robin delivery */
#define NG_ONE2MANY_XMIT_ALL		2	/* send packets to all many hooks */
#define	NG_ONE2MANY_XMIT_FAILOVER	3	/* send packets to first active "many" */

/* Algorithms for detecting link failure (XXX only one so far) */
#define NG_ONE2MANY_FAIL_MANUAL		1	/* use enabledLinks[] array */
#define NG_ONE2MANY_FAIL_NOTIFY		2	/* listen to flow control msgs */

/* Node configuration structure */
struct ng_one2many_config {
	u_int32_t	xmitAlg;		/* how to distribute packets */
	u_int32_t	failAlg;		/* how to detect link failure */
	u_char		enabledLinks[NG_ONE2MANY_MAX_LINKS];
};

/* Keep this in sync with the above structure definition */
#define NG_ONE2MANY_CONFIG_TYPE_INFO(atype)	{		\
	  { "xmitAlg",		&ng_parse_uint32_type	},	\
	  { "failAlg",		&ng_parse_uint32_type	},	\
	  { "enabledLinks",	(atype)			},	\
	  { NULL }						\
}

/* Statistics structure (one for each link) */
struct ng_one2many_link_stats {
	u_int64_t	recvOctets;	/* total octets rec'd on link */
	u_int64_t	recvPackets;	/* total pkts rec'd on link */
	u_int64_t	xmitOctets;	/* total octets xmit'd on link */
	u_int64_t	xmitPackets;	/* total pkts xmit'd on link */
	u_int64_t	memoryFailures;	/* times couldn't get mem or mbuf */
};

/* Keep this in sync with the above structure definition */
#define NG_ONE2MANY_LINK_STATS_TYPE_INFO	{		\
	  { "recvOctets",	&ng_parse_uint64_type	},	\
	  { "recvPackets",	&ng_parse_uint64_type	},	\
	  { "xmitOctets",	&ng_parse_uint64_type	},	\
	  { "xmitPackets",	&ng_parse_uint64_type	},	\
	  { "memoryFailures",	&ng_parse_uint64_type	},	\
	  { NULL }						\
}

/* Netgraph control messages */
enum {
	NGM_ONE2MANY_SET_CONFIG,	/* set configuration */
	NGM_ONE2MANY_GET_CONFIG,	/* get configuration */
	NGM_ONE2MANY_GET_STATS,		/* get link stats */
	NGM_ONE2MANY_CLR_STATS,		/* clear link stats */
	NGM_ONE2MANY_GETCLR_STATS,	/* atomically get & clear link stats */
};

#endif /* _NETGRAPH_NG_ONE2MANY_H_ */

