/*
 * Copyright (c) 2014, Ericsson AB
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _LINUX_TIPC_NETLINK_H_
#define _LINUX_TIPC_NETLINK_H_

#define TIPC_GENL_V2_NAME      "TIPCv2"
#define TIPC_GENL_V2_VERSION   0x1

/* Netlink commands */
enum {
	TIPC_NL_UNSPEC,
	TIPC_NL_LEGACY,
	TIPC_NL_BEARER_DISABLE,
	TIPC_NL_BEARER_ENABLE,
	TIPC_NL_BEARER_GET,
	TIPC_NL_BEARER_SET,
	TIPC_NL_SOCK_GET,
	TIPC_NL_PUBL_GET,
	TIPC_NL_LINK_GET,
	TIPC_NL_LINK_SET,
	TIPC_NL_LINK_RESET_STATS,
	TIPC_NL_MEDIA_GET,
	TIPC_NL_MEDIA_SET,
	TIPC_NL_NODE_GET,
	TIPC_NL_NET_GET,
	TIPC_NL_NET_SET,
	TIPC_NL_NAME_TABLE_GET,
	TIPC_NL_MON_SET,
	TIPC_NL_MON_GET,
	TIPC_NL_MON_PEER_GET,
	TIPC_NL_PEER_REMOVE,
	TIPC_NL_BEARER_ADD,
	TIPC_NL_UDP_GET_REMOTEIP,

	__TIPC_NL_CMD_MAX,
	TIPC_NL_CMD_MAX = __TIPC_NL_CMD_MAX - 1
};

/* Top level netlink attributes */
enum {
	TIPC_NLA_UNSPEC,
	TIPC_NLA_BEARER,		/* nest */
	TIPC_NLA_SOCK,			/* nest */
	TIPC_NLA_PUBL,			/* nest */
	TIPC_NLA_LINK,			/* nest */
	TIPC_NLA_MEDIA,			/* nest */
	TIPC_NLA_NODE,			/* nest */
	TIPC_NLA_NET,			/* nest */
	TIPC_NLA_NAME_TABLE,		/* nest */
	TIPC_NLA_MON,			/* nest */
	TIPC_NLA_MON_PEER,		/* nest */

	__TIPC_NLA_MAX,
	TIPC_NLA_MAX = __TIPC_NLA_MAX - 1
};

/* Bearer info */
enum {
	TIPC_NLA_BEARER_UNSPEC,
	TIPC_NLA_BEARER_NAME,		/* string */
	TIPC_NLA_BEARER_PROP,		/* nest */
	TIPC_NLA_BEARER_DOMAIN,		/* u32 */
	TIPC_NLA_BEARER_UDP_OPTS,	/* nest */

	__TIPC_NLA_BEARER_MAX,
	TIPC_NLA_BEARER_MAX = __TIPC_NLA_BEARER_MAX - 1
};

enum {
	TIPC_NLA_UDP_UNSPEC,
	TIPC_NLA_UDP_LOCAL,		/* sockaddr_storage */
	TIPC_NLA_UDP_REMOTE,		/* sockaddr_storage */
	TIPC_NLA_UDP_MULTI_REMOTEIP,	/* flag */

	__TIPC_NLA_UDP_MAX,
	TIPC_NLA_UDP_MAX = __TIPC_NLA_UDP_MAX - 1
};
/* Socket info */
enum {
	TIPC_NLA_SOCK_UNSPEC,
	TIPC_NLA_SOCK_ADDR,		/* u32 */
	TIPC_NLA_SOCK_REF,		/* u32 */
	TIPC_NLA_SOCK_CON,		/* nest */
	TIPC_NLA_SOCK_HAS_PUBL,		/* flag */

	__TIPC_NLA_SOCK_MAX,
	TIPC_NLA_SOCK_MAX = __TIPC_NLA_SOCK_MAX - 1
};

/* Link info */
enum {
	TIPC_NLA_LINK_UNSPEC,
	TIPC_NLA_LINK_NAME,		/* string */
	TIPC_NLA_LINK_DEST,		/* u32 */
	TIPC_NLA_LINK_MTU,		/* u32 */
	TIPC_NLA_LINK_BROADCAST,	/* flag */
	TIPC_NLA_LINK_UP,		/* flag */
	TIPC_NLA_LINK_ACTIVE,		/* flag */
	TIPC_NLA_LINK_PROP,		/* nest */
	TIPC_NLA_LINK_STATS,		/* nest */
	TIPC_NLA_LINK_RX,		/* u32 */
	TIPC_NLA_LINK_TX,		/* u32 */

	__TIPC_NLA_LINK_MAX,
	TIPC_NLA_LINK_MAX = __TIPC_NLA_LINK_MAX - 1
};

/* Media info */
enum {
	TIPC_NLA_MEDIA_UNSPEC,
	TIPC_NLA_MEDIA_NAME,		/* string */
	TIPC_NLA_MEDIA_PROP,		/* nest */

	__TIPC_NLA_MEDIA_MAX,
	TIPC_NLA_MEDIA_MAX = __TIPC_NLA_MEDIA_MAX - 1
};

/* Node info */
enum {
	TIPC_NLA_NODE_UNSPEC,
	TIPC_NLA_NODE_ADDR,		/* u32 */
	TIPC_NLA_NODE_UP,		/* flag */

	__TIPC_NLA_NODE_MAX,
	TIPC_NLA_NODE_MAX = __TIPC_NLA_NODE_MAX - 1
};

/* Net info */
enum {
	TIPC_NLA_NET_UNSPEC,
	TIPC_NLA_NET_ID,		/* u32 */
	TIPC_NLA_NET_ADDR,		/* u32 */

	__TIPC_NLA_NET_MAX,
	TIPC_NLA_NET_MAX = __TIPC_NLA_NET_MAX - 1
};

/* Name table info */
enum {
	TIPC_NLA_NAME_TABLE_UNSPEC,
	TIPC_NLA_NAME_TABLE_PUBL,	/* nest */

	__TIPC_NLA_NAME_TABLE_MAX,
	TIPC_NLA_NAME_TABLE_MAX = __TIPC_NLA_NAME_TABLE_MAX - 1
};

/* Monitor info */
enum {
	TIPC_NLA_MON_UNSPEC,
	TIPC_NLA_MON_ACTIVATION_THRESHOLD,	/* u32 */
	TIPC_NLA_MON_REF,			/* u32 */
	TIPC_NLA_MON_ACTIVE,			/* flag */
	TIPC_NLA_MON_BEARER_NAME,		/* string */
	TIPC_NLA_MON_PEERCNT,			/* u32 */
	TIPC_NLA_MON_LISTGEN,			/* u32 */

	__TIPC_NLA_MON_MAX,
	TIPC_NLA_MON_MAX = __TIPC_NLA_MON_MAX - 1
};

/* Publication info */
enum {
	TIPC_NLA_PUBL_UNSPEC,

	TIPC_NLA_PUBL_TYPE,		/* u32 */
	TIPC_NLA_PUBL_LOWER,		/* u32 */
	TIPC_NLA_PUBL_UPPER,		/* u32 */
	TIPC_NLA_PUBL_SCOPE,		/* u32 */
	TIPC_NLA_PUBL_NODE,		/* u32 */
	TIPC_NLA_PUBL_REF,		/* u32 */
	TIPC_NLA_PUBL_KEY,		/* u32 */

	__TIPC_NLA_PUBL_MAX,
	TIPC_NLA_PUBL_MAX = __TIPC_NLA_PUBL_MAX - 1
};

/* Monitor peer info */
enum {
	TIPC_NLA_MON_PEER_UNSPEC,

	TIPC_NLA_MON_PEER_ADDR,			/* u32 */
	TIPC_NLA_MON_PEER_DOMGEN,		/* u32 */
	TIPC_NLA_MON_PEER_APPLIED,		/* u32 */
	TIPC_NLA_MON_PEER_UPMAP,		/* u64 */
	TIPC_NLA_MON_PEER_MEMBERS,		/* tlv */
	TIPC_NLA_MON_PEER_UP,			/* flag */
	TIPC_NLA_MON_PEER_HEAD,			/* flag */
	TIPC_NLA_MON_PEER_LOCAL,		/* flag */
	TIPC_NLA_MON_PEER_PAD,			/* flag */

	__TIPC_NLA_MON_PEER_MAX,
	TIPC_NLA_MON_PEER_MAX = __TIPC_NLA_MON_PEER_MAX - 1
};

/* Nest, connection info */
enum {
	TIPC_NLA_CON_UNSPEC,

	TIPC_NLA_CON_FLAG,		/* flag */
	TIPC_NLA_CON_NODE,		/* u32 */
	TIPC_NLA_CON_SOCK,		/* u32 */
	TIPC_NLA_CON_TYPE,		/* u32 */
	TIPC_NLA_CON_INST,		/* u32 */

	__TIPC_NLA_CON_MAX,
	TIPC_NLA_CON_MAX = __TIPC_NLA_CON_MAX - 1
};

/* Nest, link propreties. Valid for link, media and bearer */
enum {
	TIPC_NLA_PROP_UNSPEC,

	TIPC_NLA_PROP_PRIO,		/* u32 */
	TIPC_NLA_PROP_TOL,		/* u32 */
	TIPC_NLA_PROP_WIN,		/* u32 */

	__TIPC_NLA_PROP_MAX,
	TIPC_NLA_PROP_MAX = __TIPC_NLA_PROP_MAX - 1
};

/* Nest, statistics info */
enum {
	TIPC_NLA_STATS_UNSPEC,

	TIPC_NLA_STATS_RX_INFO,		/* u32 */
	TIPC_NLA_STATS_RX_FRAGMENTS,	/* u32 */
	TIPC_NLA_STATS_RX_FRAGMENTED,	/* u32 */
	TIPC_NLA_STATS_RX_BUNDLES,	/* u32 */
	TIPC_NLA_STATS_RX_BUNDLED,	/* u32 */
	TIPC_NLA_STATS_TX_INFO,		/* u32 */
	TIPC_NLA_STATS_TX_FRAGMENTS,	/* u32 */
	TIPC_NLA_STATS_TX_FRAGMENTED,	/* u32 */
	TIPC_NLA_STATS_TX_BUNDLES,	/* u32 */
	TIPC_NLA_STATS_TX_BUNDLED,	/* u32 */
	TIPC_NLA_STATS_MSG_PROF_TOT,	/* u32 */
	TIPC_NLA_STATS_MSG_LEN_CNT,	/* u32 */
	TIPC_NLA_STATS_MSG_LEN_TOT,	/* u32 */
	TIPC_NLA_STATS_MSG_LEN_P0,	/* u32 */
	TIPC_NLA_STATS_MSG_LEN_P1,	/* u32 */
	TIPC_NLA_STATS_MSG_LEN_P2,	/* u32 */
	TIPC_NLA_STATS_MSG_LEN_P3,	/* u32 */
	TIPC_NLA_STATS_MSG_LEN_P4,	/* u32 */
	TIPC_NLA_STATS_MSG_LEN_P5,	/* u32 */
	TIPC_NLA_STATS_MSG_LEN_P6,	/* u32 */
	TIPC_NLA_STATS_RX_STATES,	/* u32 */
	TIPC_NLA_STATS_RX_PROBES,	/* u32 */
	TIPC_NLA_STATS_RX_NACKS,	/* u32 */
	TIPC_NLA_STATS_RX_DEFERRED,	/* u32 */
	TIPC_NLA_STATS_TX_STATES,	/* u32 */
	TIPC_NLA_STATS_TX_PROBES,	/* u32 */
	TIPC_NLA_STATS_TX_NACKS,	/* u32 */
	TIPC_NLA_STATS_TX_ACKS,		/* u32 */
	TIPC_NLA_STATS_RETRANSMITTED,	/* u32 */
	TIPC_NLA_STATS_DUPLICATES,	/* u32 */
	TIPC_NLA_STATS_LINK_CONGS,	/* u32 */
	TIPC_NLA_STATS_MAX_QUEUE,	/* u32 */
	TIPC_NLA_STATS_AVG_QUEUE,	/* u32 */

	__TIPC_NLA_STATS_MAX,
	TIPC_NLA_STATS_MAX = __TIPC_NLA_STATS_MAX - 1
};

#endif
