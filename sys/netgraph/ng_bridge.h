/*
 * ng_bridge.h
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

#ifndef _NETGRAPH_NG_BRIDGE_H_
#define _NETGRAPH_NG_BRIDGE_H_

/* Node type name and magic cookie */
#define NG_BRIDGE_NODE_TYPE		"bridge"
#define NGM_BRIDGE_COOKIE		967239368

/* Hook names */
#define NG_BRIDGE_HOOK_LINK_PREFIX	"link"	 /* append decimal integer */
#define NG_BRIDGE_HOOK_LINK_FMT		"link%d" /* for use with printf(3) */

/* Maximum number of supported links */
#define NG_BRIDGE_MAX_LINKS		32

/* Node configuration structure */
struct ng_bridge_config {
	u_char		ipfw[NG_BRIDGE_MAX_LINKS]; 	/* enable ipfw */
	u_char		debugLevel;		/* debug level */
	u_int32_t	loopTimeout;		/* link loopback mute time */
	u_int32_t	maxStaleness;		/* max host age before nuking */
	u_int32_t	minStableAge;		/* min time for a stable host */
};

/* Keep this in sync with the above structure definition */
#define NG_BRIDGE_CONFIG_TYPE_INFO(ainfo)	{		\
	  { "ipfw",		(ainfo)			},	\
	  { "debugLevel",	&ng_parse_uint8_type	},	\
	  { "loopTimeout",	&ng_parse_uint32_type	},	\
	  { "maxStaleness",	&ng_parse_uint32_type	},	\
	  { "minStableAge",	&ng_parse_uint32_type	},	\
	  { NULL }						\
}

/* Statistics structure (one for each link) */
struct ng_bridge_link_stats {
	u_int64_t	recvOctets;	/* total octets rec'd on link */
	u_int64_t	recvPackets;	/* total pkts rec'd on link */
	u_int64_t	recvMulticasts;	/* multicast pkts rec'd on link */
	u_int64_t	recvBroadcasts;	/* broadcast pkts rec'd on link */
	u_int64_t	recvUnknown;	/* pkts rec'd with unknown dest addr */
	u_int64_t	recvRunts;	/* pkts rec'd less than 14 bytes */
	u_int64_t	recvInvalid;	/* pkts rec'd with bogus source addr */
	u_int64_t	xmitOctets;	/* total octets xmit'd on link */
	u_int64_t	xmitPackets;	/* total pkts xmit'd on link */
	u_int64_t	xmitMulticasts;	/* multicast pkts xmit'd on link */
	u_int64_t	xmitBroadcasts;	/* broadcast pkts xmit'd on link */
	u_int64_t	loopDrops;	/* pkts dropped due to loopback */
	u_int64_t	loopDetects;	/* number of loop detections */
	u_int64_t	memoryFailures;	/* times couldn't get mem or mbuf */
};

/* Keep this in sync with the above structure definition */
#define NG_BRIDGE_STATS_TYPE_INFO	{			\
	  { "recvOctets",	&ng_parse_uint64_type	},	\
	  { "recvPackets",	&ng_parse_uint64_type	},	\
	  { "recvMulticast",	&ng_parse_uint64_type	},	\
	  { "recvBroadcast",	&ng_parse_uint64_type	},	\
	  { "recvUnknown",	&ng_parse_uint64_type	},	\
	  { "recvRunts",	&ng_parse_uint64_type	},	\
	  { "recvInvalid",	&ng_parse_uint64_type	},	\
	  { "xmitOctets",	&ng_parse_uint64_type	},	\
	  { "xmitPackets",	&ng_parse_uint64_type	},	\
	  { "xmitMulticasts",	&ng_parse_uint64_type	},	\
	  { "xmitBroadcasts",	&ng_parse_uint64_type	},	\
	  { "loopDrops",	&ng_parse_uint64_type	},	\
	  { "loopDetects",	&ng_parse_uint64_type	},	\
	  { "memoryFailures",	&ng_parse_uint64_type	},	\
	  { NULL }						\
}

/* Structure describing a single host */
struct ng_bridge_host {
	u_char		addr[6];	/* ethernet address */
	u_int16_t	linkNum;	/* link where addr can be found */
	u_int16_t	age;		/* seconds ago entry was created */
	u_int16_t	staleness;	/* seconds ago host last heard from */
};

/* Keep this in sync with the above structure definition */
#define NG_BRIDGE_HOST_TYPE_INFO(entype)	{		\
	  { "addr",		(entype)		},	\
	  { "linkNum",		&ng_parse_uint16_type	},	\
	  { "age",		&ng_parse_uint16_type	},	\
	  { "staleness",	&ng_parse_uint16_type	},	\
	  { NULL }						\
}

/* Structure returned by NGM_BRIDGE_GET_TABLE */
struct ng_bridge_host_ary {
	u_int32_t		numHosts;
	struct ng_bridge_host	hosts[];
};

/* Keep this in sync with the above structure definition */
#define NG_BRIDGE_HOST_ARY_TYPE_INFO(harytype)	{		\
	  { "numHosts",		&ng_parse_uint32_type	},	\
	  { "hosts",		(harytype)		},	\
	  { NULL }						\
}

/* Netgraph control messages */
enum {
	NGM_BRIDGE_SET_CONFIG = 1,	/* set node configuration */
	NGM_BRIDGE_GET_CONFIG,		/* get node configuration */
	NGM_BRIDGE_RESET,		/* reset (forget) all information */
	NGM_BRIDGE_GET_STATS,		/* get link stats */
	NGM_BRIDGE_CLR_STATS,		/* clear link stats */
	NGM_BRIDGE_GETCLR_STATS,	/* atomically get & clear link stats */
	NGM_BRIDGE_GET_TABLE,		/* get link table */
	NGM_BRIDGE_SET_PERSISTENT,	/* set persistent mode */
};

#endif /* _NETGRAPH_NG_BRIDGE_H_ */

