/*
 * ng_pptpgre.h
 */

/*-
 * Copyright (c) 1999 Whistle Communications, Inc.
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
 * $Whistle: ng_pptpgre.h,v 1.3 1999/12/08 00:11:36 archie Exp $
 */

#ifndef _NETGRAPH_NG_PPTPGRE_H_
#define _NETGRAPH_NG_PPTPGRE_H_

/* Node type name and magic cookie */
#define NG_PPTPGRE_NODE_TYPE	"pptpgre"
#define NGM_PPTPGRE_COOKIE	1082548365

/* Hook names */
#define NG_PPTPGRE_HOOK_UPPER	"upper"		/* to upper layers */
#define NG_PPTPGRE_HOOK_LOWER	"lower"		/* to lower layers */

/* Session hooks: prefix plus hex session ID, e.g., "session_3e14" */
#define NG_PPTPGRE_HOOK_SESSION_P	"session_"
#define NG_PPTPGRE_HOOK_SESSION_F	"session_%04x"

/* Configuration for a session */
struct ng_pptpgre_conf {
	u_char		enabled;	/* enables traffic flow */
	u_char		enableDelayedAck;/* enables delayed acks */
	u_char		enableAlwaysAck;/* always include ack with data */
	u_char		enableWindowing;/* enable windowing algorithm */
	u_int16_t	cid;		/* my call id */
	u_int16_t	peerCid;	/* peer call id */
	u_int16_t	recvWin;	/* peer recv window size */
	u_int16_t	peerPpd;	/* peer packet processing delay
					   (in units of 1/10 of a second) */
};

/* Keep this in sync with the above structure definition */
#define NG_PPTPGRE_CONF_TYPE_INFO	{			\
	  { "enabled",		&ng_parse_uint8_type	},	\
	  { "enableDelayedAck",	&ng_parse_uint8_type	},	\
	  { "enableAlwaysAck",	&ng_parse_uint8_type	},	\
	  { "enableWindowing",	&ng_parse_uint8_type	},	\
	  { "cid",		&ng_parse_hint16_type	},	\
	  { "peerCid",		&ng_parse_hint16_type	},	\
	  { "recvWin",		&ng_parse_uint16_type	},	\
	  { "peerPpd",		&ng_parse_uint16_type	},	\
	  { NULL }						\
}

/* Statistics struct */
struct ng_pptpgre_stats {
	u_int32_t xmitPackets;		/* number of GRE packets xmit */
	u_int32_t xmitOctets;		/* number of GRE octets xmit */
	u_int32_t xmitLoneAcks;		/* ack-only packets transmitted */
	u_int32_t xmitDrops;		/* xmits dropped due to full window */
	u_int32_t xmitTooBig;		/* xmits dropped because too big */
	u_int32_t recvPackets;		/* number of GRE packets rec'd */
	u_int32_t recvOctets;		/* number of GRE octets rec'd */
	u_int32_t recvRunts;		/* too short packets rec'd */
	u_int32_t recvBadGRE;		/* bogus packets rec'd (bad GRE hdr) */
	u_int32_t recvBadAcks;		/* bogus ack's rec'd in GRE header */
	u_int32_t recvBadCID;		/* pkts with unknown call ID rec'd */
	u_int32_t recvOutOfOrder;	/* packets rec'd out of order */
	u_int32_t recvDuplicates;	/* packets rec'd with duplicate seq # */
	u_int32_t recvLoneAcks;		/* ack-only packets rec'd */
	u_int32_t recvAckTimeouts;	/* times peer failed to ack in time */
	u_int32_t memoryFailures;	/* times we couldn't allocate memory */
	u_int32_t recvReorderOverflow;	/* times we dropped GRE packet
					   due to overflow of reorder queue */
	u_int32_t recvReorderTimeouts;	/* times we flushed reorder queue
					   due to timeout */
};

/* Keep this in sync with the above structure definition */
#define NG_PPTPGRE_STATS_TYPE_INFO	{			\
	  { "xmitPackets",	&ng_parse_uint32_type	},	\
	  { "xmitOctets",	&ng_parse_uint32_type	},	\
	  { "xmitLoneAcks",	&ng_parse_uint32_type	},	\
	  { "xmitDrops",	&ng_parse_uint32_type	},	\
	  { "xmitTooBig",	&ng_parse_uint32_type	},	\
	  { "recvPackets",	&ng_parse_uint32_type	},	\
	  { "recvOctets",	&ng_parse_uint32_type	},	\
	  { "recvRunts",	&ng_parse_uint32_type	},	\
	  { "recvBadGRE",	&ng_parse_uint32_type	},	\
	  { "recvBadAcks",	&ng_parse_uint32_type	},	\
	  { "recvBadCID",	&ng_parse_uint32_type	},	\
	  { "recvOutOfOrder",	&ng_parse_uint32_type	},	\
	  { "recvDuplicates",	&ng_parse_uint32_type	},	\
	  { "recvLoneAcks",	&ng_parse_uint32_type	},	\
	  { "recvAckTimeouts",	&ng_parse_uint32_type	},	\
	  { "memoryFailures",	&ng_parse_uint32_type	},	\
	  { "recvReorderOverflow", &ng_parse_uint32_type},	\
	  { "recvReorderTimeouts", &ng_parse_uint32_type},	\
	  { NULL }						\
}

/* Netgraph commands */
enum {
	NGM_PPTPGRE_SET_CONFIG = 1,	/* supply a struct ng_pptpgre_conf */
	NGM_PPTPGRE_GET_CONFIG,		/* returns a struct ng_pptpgre_conf */
	NGM_PPTPGRE_GET_STATS,		/* returns struct ng_pptpgre_stats */
	NGM_PPTPGRE_CLR_STATS,		/* clears stats */
	NGM_PPTPGRE_GETCLR_STATS,	/* returns & clears stats */
};

#endif /* _NETGRAPH_NG_PPTPGRE_H_ */
