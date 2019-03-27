/*
 * ng_ksocket.h
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
 * $Whistle: ng_ksocket.h,v 1.1 1999/11/16 20:04:40 archie Exp $
 */

#ifndef _NETGRAPH_NG_KSOCKET_H_
#define _NETGRAPH_NG_KSOCKET_H_

#include <sys/socket.h>

/* Node type name and magic cookie */
#define NG_KSOCKET_NODE_TYPE	"ksocket"
#define NGM_KSOCKET_COOKIE	942710669

/* For NGM_KSOCKET_SETOPT and NGM_KSOCKET_GETOPT control messages */
struct ng_ksocket_sockopt {
	int32_t		level;		/* second arg of [gs]etsockopt() */
	int32_t		name;		/* third arg of [gs]etsockopt() */
	u_char		value[];	/* fourth arg of [gs]etsockopt() */
};

/* Max length socket option we can return via NGM_KSOCKET_GETOPT
   XXX This should not be necessary, we should dynamically size
   XXX the response. Until then.. */
#define NG_KSOCKET_MAX_OPTLEN	1024

/* Keep this in sync with the above structure definition */
#define NG_KSOCKET_SOCKOPT_INFO(svtype)	{			\
	  { "level",		&ng_parse_int32_type	},	\
	  { "name",		&ng_parse_int32_type	},	\
	  { "value",		(svtype)		},	\
	  { NULL }						\
}

/* For NGM_KSOCKET_ACCEPT control message responses */
struct ng_ksocket_accept {
	u_int32_t	nodeid;		/* node ID of connected ksocket */
	struct sockaddr	addr;		/* peer's address (variable length) */
};

/* Keep this in sync with the above structure definition */
#define	NGM_KSOCKET_ACCEPT_INFO {					\
	  { "nodeid",		&ng_parse_hint32_type		  },	\
	  { "addr",		&ng_ksocket_generic_sockaddr_type },	\
	  { NULL }							\
}

/* Netgraph commands */
enum {
	NGM_KSOCKET_BIND = 1,
	NGM_KSOCKET_LISTEN,
	NGM_KSOCKET_ACCEPT,
	NGM_KSOCKET_CONNECT,
	NGM_KSOCKET_GETNAME,
	NGM_KSOCKET_GETPEERNAME,
	NGM_KSOCKET_SETOPT,
	NGM_KSOCKET_GETOPT,
};

#ifdef _KERNEL

/* Structure for sockaddr tag */
struct sa_tag {
	struct m_tag	tag;
	ng_ID_t		id;
	struct sockaddr	sa;
};

/* Tag information ID's */
#define NG_KSOCKET_TAG_SOCKADDR	1	/* data is struct sockaddr */

#endif /* _KERNEL */
#endif /* _NETGRAPH_NG_KSOCKET_H_ */
