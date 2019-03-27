/*
 * ng_async.h
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
 * $Whistle: ng_async.h,v 1.5 1999/01/25 01:17:14 archie Exp $
 */

#ifndef _NETGRAPH_NG_ASYNC_H_
#define _NETGRAPH_NG_ASYNC_H_

/* Type name and cookie */
#define NG_ASYNC_NODE_TYPE	"async"
#define NGM_ASYNC_COOKIE	886473717

/* Hook names */
#define NG_ASYNC_HOOK_SYNC	"sync"	/* Sync frames */
#define NG_ASYNC_HOOK_ASYNC	"async"	/* Async-encoded frames */

/* Maximum receive size bounds (for both sync and async sides) */
#define NG_ASYNC_MIN_MRU	1
#define NG_ASYNC_MAX_MRU	8192
#define NG_ASYNC_DEFAULT_MRU	1600

/* Frame statistics */
struct ng_async_stat {
	u_int32_t	syncOctets;
	u_int32_t	syncFrames;
	u_int32_t	syncOverflows;
	u_int32_t	asyncOctets;
	u_int32_t	asyncFrames;
	u_int32_t	asyncRunts;
	u_int32_t	asyncOverflows;
	u_int32_t	asyncBadCheckSums;
};

/* Keep this in sync with the above structure definition */
#define NG_ASYNC_STATS_TYPE_INFO	{			\
	  { "syncOctets",	&ng_parse_uint32_type	},	\
	  { "syncFrames",	&ng_parse_uint32_type	},	\
	  { "syncOverflows",	&ng_parse_uint32_type	},	\
	  { "asyncOctets",	&ng_parse_uint32_type	},	\
	  { "asyncFrames",	&ng_parse_uint32_type	},	\
	  { "asyncRunts",	&ng_parse_uint32_type	},	\
	  { "asyncOverflows",	&ng_parse_uint32_type	},	\
	  { "asyncBadCheckSums",&ng_parse_uint32_type	},	\
	  { NULL }						\
}

/* Configuration for this node */
struct ng_async_cfg {
	u_char		enabled;	/* Turn encoding on/off */
	u_int16_t	amru;		/* Max receive async frame length */
	u_int16_t	smru;		/* Max receive sync frame length */
	u_int32_t	accm;		/* ACCM encoding */
};

/* Keep this in sync with the above structure definition */
#define NG_ASYNC_CONFIG_TYPE_INFO	{			\
	  { "enabled",		&ng_parse_int8_type	},	\
	  { "amru",		&ng_parse_uint16_type	},	\
	  { "smru",		&ng_parse_uint16_type	},	\
	  { "accm",		&ng_parse_hint32_type	},	\
	  { NULL }						\
}

/* Commands */
enum {
	NGM_ASYNC_CMD_GET_STATS = 1,	/* returns struct ng_async_stat */
	NGM_ASYNC_CMD_CLR_STATS,
	NGM_ASYNC_CMD_SET_CONFIG,	/* takes struct ng_async_cfg */
	NGM_ASYNC_CMD_GET_CONFIG,	/* returns struct ng_async_cfg */
};

#endif /* _NETGRAPH_NG_ASYNC_H_ */
