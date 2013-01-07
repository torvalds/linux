/*
 * include/linux/tipc.h: Include file for TIPC socket interface
 *
 * Copyright (c) 2003-2006, Ericsson AB
 * Copyright (c) 2005, 2010-2011, Wind River Systems
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

#ifndef _LINUX_TIPC_H_
#define _LINUX_TIPC_H_

#include <linux/types.h>

/*
 * TIPC addressing primitives
 */

struct tipc_portid {
	__u32 ref;
	__u32 node;
};

struct tipc_name {
	__u32 type;
	__u32 instance;
};

struct tipc_name_seq {
	__u32 type;
	__u32 lower;
	__u32 upper;
};

static inline __u32 tipc_addr(unsigned int zone,
			      unsigned int cluster,
			      unsigned int node)
{
	return (zone << 24) | (cluster << 12) | node;
}

static inline unsigned int tipc_zone(__u32 addr)
{
	return addr >> 24;
}

static inline unsigned int tipc_cluster(__u32 addr)
{
	return (addr >> 12) & 0xfff;
}

static inline unsigned int tipc_node(__u32 addr)
{
	return addr & 0xfff;
}

/*
 * Application-accessible port name types
 */

#define TIPC_CFG_SRV		0	/* configuration service name type */
#define TIPC_TOP_SRV		1	/* topology service name type */
#define TIPC_RESERVED_TYPES	64	/* lowest user-publishable name type */

/*
 * Publication scopes when binding port names and port name sequences
 */

#define TIPC_ZONE_SCOPE		1
#define TIPC_CLUSTER_SCOPE	2
#define TIPC_NODE_SCOPE		3

/*
 * Limiting values for messages
 */

#define TIPC_MAX_USER_MSG_SIZE	66000U

/*
 * Message importance levels
 */

#define TIPC_LOW_IMPORTANCE		0
#define TIPC_MEDIUM_IMPORTANCE		1
#define TIPC_HIGH_IMPORTANCE		2
#define TIPC_CRITICAL_IMPORTANCE	3

/*
 * Msg rejection/connection shutdown reasons
 */

#define TIPC_OK			0
#define TIPC_ERR_NO_NAME	1
#define TIPC_ERR_NO_PORT	2
#define TIPC_ERR_NO_NODE	3
#define TIPC_ERR_OVERLOAD	4
#define TIPC_CONN_SHUTDOWN	5

/*
 * TIPC topology subscription service definitions
 */

#define TIPC_SUB_PORTS		0x01	/* filter for port availability */
#define TIPC_SUB_SERVICE	0x02	/* filter for service availability */
#define TIPC_SUB_CANCEL		0x04	/* cancel a subscription */

#define TIPC_WAIT_FOREVER	(~0)	/* timeout for permanent subscription */

struct tipc_subscr {
	struct tipc_name_seq seq;	/* name sequence of interest */
	__u32 timeout;			/* subscription duration (in ms) */
	__u32 filter;			/* bitmask of filter options */
	char usr_handle[8];		/* available for subscriber use */
};

#define TIPC_PUBLISHED		1	/* publication event */
#define TIPC_WITHDRAWN		2	/* withdraw event */
#define TIPC_SUBSCR_TIMEOUT	3	/* subscription timeout event */

struct tipc_event {
	__u32 event;			/* event type */
	__u32 found_lower;		/* matching name seq instances */
	__u32 found_upper;		/*    "      "    "     "      */
	struct tipc_portid port;	/* associated port */
	struct tipc_subscr s;		/* associated subscription */
};

/*
 * Socket API
 */

#ifndef AF_TIPC
#define AF_TIPC		30
#endif

#ifndef PF_TIPC
#define PF_TIPC		AF_TIPC
#endif

#ifndef SOL_TIPC
#define SOL_TIPC	271
#endif

#define TIPC_ADDR_NAMESEQ	1
#define TIPC_ADDR_MCAST		1
#define TIPC_ADDR_NAME		2
#define TIPC_ADDR_ID		3

struct sockaddr_tipc {
	unsigned short family;
	unsigned char  addrtype;
	signed   char  scope;
	union {
		struct tipc_portid id;
		struct tipc_name_seq nameseq;
		struct {
			struct tipc_name name;
			__u32 domain;
		} name;
	} addr;
};

/*
 * Ancillary data objects supported by recvmsg()
 */

#define TIPC_ERRINFO	1	/* error info */
#define TIPC_RETDATA	2	/* returned data */
#define TIPC_DESTNAME	3	/* destination name */

/*
 * TIPC-specific socket option values
 */

#define TIPC_IMPORTANCE		127	/* Default: TIPC_LOW_IMPORTANCE */
#define TIPC_SRC_DROPPABLE	128	/* Default: based on socket type */
#define TIPC_DEST_DROPPABLE	129	/* Default: based on socket type */
#define TIPC_CONN_TIMEOUT	130	/* Default: 8000 (ms)  */
#define TIPC_NODE_RECVQ_DEPTH	131	/* Default: none (read only) */
#define TIPC_SOCK_RECVQ_DEPTH	132	/* Default: none (read only) */

#endif
