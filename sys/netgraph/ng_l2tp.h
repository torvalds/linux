/*-
 * Copyright (c) 2001-2002 Packet Design, LLC.
 * All rights reserved.
 * 
 * Subject to the following obligations and disclaimer of warranty,
 * use and redistribution of this software, in source or object code
 * forms, with or without modifications are expressly permitted by
 * Packet Design; provided, however, that:
 * 
 *    (i)  Any and all reproductions of the source or object code
 *         must include the copyright notice above and the following
 *         disclaimer of warranties; and
 *    (ii) No rights are granted, in any manner or form, to use
 *         Packet Design trademarks, including the mark "PACKET DESIGN"
 *         on advertising, endorsements, or otherwise except as such
 *         appears in the above copyright notice or in the software.
 * 
 * THIS SOFTWARE IS BEING PROVIDED BY PACKET DESIGN "AS IS", AND
 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, PACKET DESIGN MAKES NO
 * REPRESENTATIONS OR WARRANTIES, EXPRESS OR IMPLIED, REGARDING
 * THIS SOFTWARE, INCLUDING WITHOUT LIMITATION, ANY AND ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE,
 * OR NON-INFRINGEMENT.  PACKET DESIGN DOES NOT WARRANT, GUARANTEE,
 * OR MAKE ANY REPRESENTATIONS REGARDING THE USE OF, OR THE RESULTS
 * OF THE USE OF THIS SOFTWARE IN TERMS OF ITS CORRECTNESS, ACCURACY,
 * RELIABILITY OR OTHERWISE.  IN NO EVENT SHALL PACKET DESIGN BE
 * LIABLE FOR ANY DAMAGES RESULTING FROM OR ARISING OUT OF ANY USE
 * OF THIS SOFTWARE, INCLUDING WITHOUT LIMITATION, ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, PUNITIVE, OR CONSEQUENTIAL
 * DAMAGES, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES, LOSS OF
 * USE, DATA OR PROFITS, HOWEVER CAUSED AND UNDER ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF PACKET DESIGN IS ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 * 
 * Author: Archie Cobbs <archie@freebsd.org>
 *
 * $FreeBSD$
 */

#ifndef _NETGRAPH_NG_L2TP_H_
#define _NETGRAPH_NG_L2TP_H_

/* Node type name and magic cookie */
#define NG_L2TP_NODE_TYPE	"l2tp"
#define NGM_L2TP_COOKIE		1091515793

/* Hook names */
#define NG_L2TP_HOOK_CTRL	"ctrl"		/* control channel hook */
#define NG_L2TP_HOOK_LOWER	"lower"		/* hook to lower layers */

/* Session hooks: prefix plus hex session ID, e.g., "session_3e14" */
#define NG_L2TP_HOOK_SESSION_P	"session_"	/* session data hook (prefix) */
#define NG_L2TP_HOOK_SESSION_F	"session_%04x"	/* session data hook (format) */

/* Set initial sequence numbers to not yet enabled node. */
struct ng_l2tp_seq_config {
	u_int16_t	ns;		/* sequence number to send next */
	u_int16_t	nr;		/* sequence number to be recved next */
	u_int16_t	rack;		/* last 'nr' received */
	u_int16_t	xack;		/* last 'nr' sent */
};

/* Keep this in sync with the above structure definition. */
#define	NG_L2TP_SEQ_CONFIG_TYPE_INFO	{			\
	  { "ns",		&ng_parse_uint16_type	},	\
	  { "nr",		&ng_parse_uint16_type	},	\
	  { NULL }						\
}

/* Configuration for a node */
struct ng_l2tp_config {
	u_char		enabled;	/* enables traffic flow */
	u_char		match_id;	/* tunnel id must match 'tunnel_id' */
	u_int16_t	tunnel_id;	/* local tunnel id */
	u_int16_t	peer_id;	/* peer's tunnel id */
	u_int16_t	peer_win;	/* peer's max recv window size */
	u_int16_t	rexmit_max;	/* max retransmits before failure */
	u_int16_t	rexmit_max_to;	/* max delay between retransmits */
};

/* Keep this in sync with the above structure definition */
#define NG_L2TP_CONFIG_TYPE_INFO	{			\
	  { "enabled",		&ng_parse_uint8_type	},	\
	  { "match_id",		&ng_parse_uint8_type	},	\
	  { "tunnel_id",	&ng_parse_hint16_type	},	\
	  { "peer_id",		&ng_parse_hint16_type	},	\
	  { "peer_win",		&ng_parse_uint16_type	},	\
	  { "rexmit_max",	&ng_parse_uint16_type	},	\
	  { "rexmit_max_to",	&ng_parse_uint16_type	},	\
	  { NULL }						\
}

/* Configuration for a session hook */
struct ng_l2tp_sess_config {
	u_int16_t	session_id;	/* local session id */
	u_int16_t	peer_id;	/* peer's session id */
	u_char		control_dseq;	/* whether we control data sequencing */
	u_char		enable_dseq;	/* whether to enable data sequencing */
	u_char		include_length;	/* whether to include length field */
};

/* Keep this in sync with the above structure definition */
#define NG_L2TP_SESS_CONFIG_TYPE_INFO	{			\
	  { "session_id",	&ng_parse_hint16_type	},	\
	  { "peer_id",		&ng_parse_hint16_type	},	\
	  { "control_dseq",	&ng_parse_uint8_type	},	\
	  { "enable_dseq",	&ng_parse_uint8_type	},	\
	  { "include_length",	&ng_parse_uint8_type	},	\
	  { NULL }						\
}

/* Statistics struct */
struct ng_l2tp_stats {
	u_int32_t xmitPackets;		/* number of packets xmit */
	u_int32_t xmitOctets;		/* number of octets xmit */
	u_int32_t xmitZLBs;		/* ack-only packets transmitted */
	u_int32_t xmitDrops;		/* xmits dropped due to full window */
	u_int32_t xmitTooBig;		/* ctrl pkts dropped because too big */
	u_int32_t xmitInvalid;		/* ctrl packets with no session ID */
	u_int32_t xmitDataTooBig;	/* data pkts dropped because too big */
	u_int32_t xmitRetransmits;	/* retransmitted packets */
	u_int32_t recvPackets;		/* number of packets rec'd */
	u_int32_t recvOctets;		/* number of octets rec'd */
	u_int32_t recvRunts;		/* too short packets rec'd */
	u_int32_t recvInvalid;		/* invalid packets rec'd */
	u_int32_t recvWrongTunnel;	/* packets rec'd with wrong tunnel id */
	u_int32_t recvUnknownSID;	/* pkts rec'd with unknown session id */
	u_int32_t recvBadAcks;		/* ctrl pkts rec'd with invalid 'nr' */
	u_int32_t recvOutOfOrder;	/* out of order ctrl pkts rec'd */
	u_int32_t recvDuplicates;	/* duplicate ctrl pkts rec'd */
	u_int32_t recvDataDrops;	/* dup/out of order data pkts rec'd */
	u_int32_t recvZLBs;		/* ack-only packets rec'd */
	u_int32_t memoryFailures;	/* times we couldn't allocate memory */
};

/* Keep this in sync with the above structure definition */
#define NG_L2TP_STATS_TYPE_INFO	{			\
	  { "xmitPackets",	&ng_parse_uint32_type	},	\
	  { "xmitOctets",	&ng_parse_uint32_type	},	\
	  { "xmitZLBs",		&ng_parse_uint32_type	},	\
	  { "xmitDrops",	&ng_parse_uint32_type	},	\
	  { "xmitTooBig",	&ng_parse_uint32_type	},	\
	  { "xmitInvalid",	&ng_parse_uint32_type	},	\
	  { "xmitDataTooBig",	&ng_parse_uint32_type	},	\
	  { "xmitRetransmits",	&ng_parse_uint32_type	},	\
	  { "recvPackets",	&ng_parse_uint32_type	},	\
	  { "recvOctets",	&ng_parse_uint32_type	},	\
	  { "recvRunts",	&ng_parse_uint32_type	},	\
	  { "recvInvalid",	&ng_parse_uint32_type	},	\
	  { "recvWrongTunnel",	&ng_parse_uint32_type	},	\
	  { "recvUnknownSID",	&ng_parse_uint32_type	},	\
	  { "recvBadAcks",	&ng_parse_uint32_type	},	\
	  { "recvOutOfOrder",	&ng_parse_uint32_type	},	\
	  { "recvDuplicates",	&ng_parse_uint32_type	},	\
	  { "recvDataDrops",	&ng_parse_uint32_type	},	\
	  { "recvZLBs",		&ng_parse_uint32_type	},	\
	  { "memoryFailures",	&ng_parse_uint32_type	},	\
	  { NULL }						\
}

/* Session statistics struct. */
struct ng_l2tp_session_stats {
	u_int64_t xmitPackets;		/* number of packets xmit */
	u_int64_t xmitOctets;		/* number of octets xmit */
	u_int64_t recvPackets;		/* number of packets received */
	u_int64_t recvOctets;		/* number of octets received */
};

/* Keep this in sync with the above structure definition. */
#define NG_L2TP_SESSION_STATS_TYPE_INFO	{			\
	  { "xmitPackets",	&ng_parse_uint64_type	},	\
	  { "xmitOctets",	&ng_parse_uint64_type	},	\
	  { "recvPackets",	&ng_parse_uint64_type	},	\
	  { "recvOctets",	&ng_parse_uint64_type	},	\
	  { NULL }						\
}

/* Netgraph commands */
enum {
	NGM_L2TP_SET_CONFIG = 1,	/* supply a struct ng_l2tp_config */
	NGM_L2TP_GET_CONFIG,		/* returns a struct ng_l2tp_config */
	NGM_L2TP_SET_SESS_CONFIG,	/* supply struct ng_l2tp_sess_config */
	NGM_L2TP_GET_SESS_CONFIG,	/* supply a session id (u_int16_t) */
	NGM_L2TP_GET_STATS,		/* returns struct ng_l2tp_stats */
	NGM_L2TP_CLR_STATS,		/* clears stats */
	NGM_L2TP_GETCLR_STATS,		/* returns & clears stats */
	NGM_L2TP_GET_SESSION_STATS,	/* returns session stats */
	NGM_L2TP_CLR_SESSION_STATS,	/* clears session stats */
	NGM_L2TP_GETCLR_SESSION_STATS,	/* returns & clears session stats */
	NGM_L2TP_ACK_FAILURE,		/* sent *from* node after ack timeout */
	NGM_L2TP_SET_SEQ		/* supply a struct ng_l2tp_seq_config */
};

#endif /* _NETGRAPH_NG_L2TP_H_ */
