/*
 * ng_message.h
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
 * Author: Julian Elischer <julian@freebsd.org>
 *
 * $FreeBSD$
 * $Whistle: ng_message.h,v 1.12 1999/01/25 01:17:44 archie Exp $
 */

#ifndef _NETGRAPH_NG_MESSAGE_H_
#define _NETGRAPH_NG_MESSAGE_H_

/* ASCII string size limits */
#define	NG_TYPESIZ	32	/* max type name len (including null) */
#define	NG_HOOKSIZ	32	/* max hook name len (including null) */
#define	NG_NODESIZ	32	/* max node name len (including null) */
#define	NG_PATHSIZ	512	/* max path len (including null) */
#define	NG_CMDSTRSIZ	32	/* max command string (including null) */

#define NG_TEXTRESPONSE 1024	/* allow this length for a text response */

/* A netgraph message */
struct ng_mesg {
	struct	ng_msghdr {
		u_char		version;		/*  == NGM_VERSION */
		u_char		spare;			/* pad to 4 bytes */
		u_int16_t	spare2;	
		u_int32_t	arglen;			/* length of data */
		u_int32_t	cmd;			/* command identifier */
		u_int32_t	flags;			/* message status */
		u_int32_t	token;			/* match with reply */
		u_int32_t	typecookie;		/* node's type cookie */
		u_char		cmdstr[NG_CMDSTRSIZ];	/* cmd string + \0 */
	} header;
	char	data[];			/* placeholder for actual data */
};

/* This command is guaranteed to not alter data (or'd into the command). */
#define NGM_READONLY	0x10000000
/* This command is guaranteed to have a reply (or'd into the command). */
#define NGM_HASREPLY	0x20000000

/* Keep this in sync with the above structure definition */
#define NG_GENERIC_NG_MESG_INFO(dtype)	{			\
	  { "version",		&ng_parse_uint8_type	},	\
	  { "spare",		&ng_parse_uint8_type	},	\
	  { "spare2",		&ng_parse_uint16_type	},	\
	  { "arglen",		&ng_parse_uint32_type	},	\
	  { "cmd",		&ng_parse_uint32_type	},	\
	  { "flags",		&ng_parse_hint32_type	},	\
	  { "token",		&ng_parse_uint32_type	},	\
	  { "typecookie",	&ng_parse_uint32_type	},	\
	  { "cmdstr",		&ng_parse_cmdbuf_type	},	\
	  { "data",		(dtype)			},	\
	  { NULL }						\
}

/*
 * Netgraph message header compatibility field
 * Interfaces within the kernel are defined by a different 
 * value (see NG_ABI_VERSION in netgraph.h)
 */
#define NG_VERSION	8

/* Flags field flags */
#define NGF_ORIG	0x00000000	/* the msg is the original request */
#define NGF_RESP	0x00000001	/* the message is a response */

/* Type of a unique node ID. */
#define ng_ID_t uint32_t

/*
 * Here we describe the "generic" messages that all nodes inherently
 * understand. With the exception of NGM_TEXT_STATUS, these are handled
 * automatically by the base netgraph code.
 */

/* Generic message type cookie */
#define NGM_GENERIC_COOKIE	1137070366

/* Generic messages defined for this type cookie. */
enum {
	NGM_SHUTDOWN	= 1,	/* Shut down node. */
	NGM_MKPEER	= 2,	/* Create and attach a peer node. */
	NGM_CONNECT	= 3,	/* Connect two nodes. */
	NGM_NAME	= 4,	/* Give a node a name. */
	NGM_RMHOOK	= 5,	/* Break a connection between two nodes. */

	/* Get nodeinfo for target. */
	NGM_NODEINFO	= (6|NGM_READONLY|NGM_HASREPLY),
	/* Get list of hooks on node. */
	NGM_LISTHOOKS	= (7|NGM_READONLY|NGM_HASREPLY),
	/* List globally named nodes. */
	NGM_LISTNAMES	= (8|NGM_READONLY|NGM_HASREPLY),
	/* List all nodes. */
	NGM_LISTNODES	= (9|NGM_READONLY|NGM_HASREPLY),
	/* List installed node types. */
	NGM_LISTTYPES	= (10|NGM_READONLY|NGM_HASREPLY),
	/* (optional) Get text status. */
	NGM_TEXT_STATUS	= (11|NGM_READONLY|NGM_HASREPLY),
	/* Convert struct ng_mesg to ASCII. */
	NGM_BINARY2ASCII= (12|NGM_READONLY|NGM_HASREPLY),
	/* Convert ASCII to struct ng_mesg. */
	NGM_ASCII2BINARY= (13|NGM_READONLY|NGM_HASREPLY),
	/* (optional) Get/set text config. */
	NGM_TEXT_CONFIG	= 14,
};

/*
 * Flow control and intra node control messages.
 * These are routed between nodes to allow flow control and to allow
 * events to be passed around the graph. 
 * There will be some form of default handling for these but I 
 * do not yet know what it is..
 */

/* Generic message type cookie */
#define NGM_FLOW_COOKIE	851672669 /* temp for debugging */

/* Upstream messages */
#define NGM_LINK_IS_UP		32	/* e.g. carrier found - no data */
#define NGM_LINK_IS_DOWN	33	/* carrier lost, includes queue state */
#define NGM_HIGH_WATER_PASSED	34	/* includes queue state */
#define NGM_LOW_WATER_PASSED	35	/* includes queue state */
#define NGM_SYNC_QUEUE_STATE	36	/* sync response from sending packet */

/* Downstream messages */
#define NGM_DROP_LINK		41	/* drop DTR, etc. - stay in the graph */
#define NGM_RAISE_LINK		42	/* if you previously dropped it */
#define NGM_FLUSH_QUEUE		43	/* no data */
#define NGM_GET_BANDWIDTH	(44|NGM_READONLY)	/* either real or measured */
#define NGM_SET_XMIT_Q_LIMITS	45	/* includes queue state */
#define NGM_GET_XMIT_Q_LIMITS	(46|NGM_READONLY)	/* returns queue state */
#define NGM_MICROMANAGE		47	/* We want sync. queue state
						reply for each packet sent */
#define NGM_SET_FLOW_MANAGER	48	/* send flow control here */ 
/* Structure used for NGM_MKPEER */
struct ngm_mkpeer {
	char	type[NG_TYPESIZ];		/* peer type */
	char	ourhook[NG_HOOKSIZ];		/* hook name */
	char	peerhook[NG_HOOKSIZ];		/* peer hook name */
};

/* Keep this in sync with the above structure definition */
#define NG_GENERIC_MKPEER_INFO()	{			\
	  { "type",		&ng_parse_typebuf_type	},	\
	  { "ourhook",		&ng_parse_hookbuf_type	},	\
	  { "peerhook",		&ng_parse_hookbuf_type	},	\
	  { NULL }						\
}

/* Structure used for NGM_CONNECT */
struct ngm_connect {
	char	path[NG_PATHSIZ];		/* peer path */
	char	ourhook[NG_HOOKSIZ];		/* hook name */
	char	peerhook[NG_HOOKSIZ];		/* peer hook name */
};

/* Keep this in sync with the above structure definition */
#define NG_GENERIC_CONNECT_INFO()	{			\
	  { "path",		&ng_parse_pathbuf_type	},	\
	  { "ourhook",		&ng_parse_hookbuf_type	},	\
	  { "peerhook",		&ng_parse_hookbuf_type	},	\
	  { NULL }						\
}

/* Structure used for NGM_NAME */
struct ngm_name {
	char	name[NG_NODESIZ];			/* node name */
};

/* Keep this in sync with the above structure definition */
#define NG_GENERIC_NAME_INFO()	{				\
	  { "name",		&ng_parse_nodebuf_type	},	\
	  { NULL }						\
}

/* Structure used for NGM_RMHOOK */
struct ngm_rmhook {
	char	ourhook[NG_HOOKSIZ];		/* hook name */
};

/* Keep this in sync with the above structure definition */
#define NG_GENERIC_RMHOOK_INFO()	{			\
	  { "hook",		&ng_parse_hookbuf_type	},	\
	  { NULL }						\
}

/* Structure used for NGM_NODEINFO */
struct nodeinfo {
	char		name[NG_NODESIZ];	/* node name (if any) */
        char    	type[NG_TYPESIZ];	/* peer type */
	ng_ID_t		id;			/* unique identifier */
	u_int32_t	hooks;			/* number of active hooks */
};

/* Keep this in sync with the above structure definition */
#define NG_GENERIC_NODEINFO_INFO()	{			\
	  { "name",		&ng_parse_nodebuf_type	},	\
	  { "type",		&ng_parse_typebuf_type	},	\
	  { "id",		&ng_parse_hint32_type	},	\
	  { "hooks",		&ng_parse_uint32_type	},	\
	  { NULL }						\
}

/* Structure used for NGM_LISTHOOKS */
struct linkinfo {
	char		ourhook[NG_HOOKSIZ];	/* hook name */
	char		peerhook[NG_HOOKSIZ];	/* peer hook */
	struct nodeinfo	nodeinfo;
};

/* Keep this in sync with the above structure definition */
#define NG_GENERIC_LINKINFO_INFO(nitype)	{		\
	  { "ourhook",		&ng_parse_hookbuf_type	},	\
	  { "peerhook",		&ng_parse_hookbuf_type	},	\
	  { "nodeinfo",		(nitype)		},	\
	  { NULL }						\
}

struct hooklist {
	struct nodeinfo nodeinfo;		/* node information */
	struct linkinfo link[];			/* info about each hook */
};

/* Keep this in sync with the above structure definition */
#define NG_GENERIC_HOOKLIST_INFO(nitype,litype)	{		\
	  { "nodeinfo",		(nitype)		},	\
	  { "linkinfo",		(litype)		},	\
	  { NULL }						\
}

/* Structure used for NGM_LISTNAMES/NGM_LISTNODES */
struct namelist {
	u_int32_t	numnames;
	struct nodeinfo	nodeinfo[];
};

/* Keep this in sync with the above structure definition */
#define NG_GENERIC_LISTNODES_INFO(niarraytype)	{		\
	  { "numnames",		&ng_parse_uint32_type	},	\
	  { "nodeinfo",		(niarraytype)		},	\
	  { NULL }						\
}

/* Structure used for NGM_LISTTYPES */
struct typeinfo {
	char		type_name[NG_TYPESIZ];	/* name of type */
	u_int32_t	numnodes;		/* number alive */
};

/* Keep this in sync with the above structure definition */
#define NG_GENERIC_TYPEINFO_INFO()		{		\
	  { "typename",		&ng_parse_typebuf_type	},	\
	  { "numnodes",		&ng_parse_uint32_type	},	\
	  { NULL }						\
}

struct typelist {
	u_int32_t	numtypes;
	struct typeinfo	typeinfo[];
};

/* Keep this in sync with the above structure definition */
#define NG_GENERIC_TYPELIST_INFO(tiarraytype)	{		\
	  { "numtypes",		&ng_parse_uint32_type	},	\
	  { "typeinfo",		(tiarraytype)		},	\
	  { NULL }						\
}

struct ngm_bandwidth {
	u_int64_t	nominal_in;
	u_int64_t	seen_in;
	u_int64_t	nominal_out;
	u_int64_t	seen_out;
};

/* Keep this in sync with the above structure definition */
#define NG_GENERIC_BANDWIDTH_INFO()	{			\
	  { "nominal_in",	&ng_parse_uint64_type	},	\
	  { "seen_in",		&ng_parse_uint64_type	},	\
	  { "nominal_out",	&ng_parse_uint64_type	},	\
	  { "seen_out",		&ng_parse_uint64_type	},	\
	  { NULL }						\
}

/*
 * Information about a node's 'output' queue.
 * This is NOT the netgraph input queueing mechanism,
 * but rather any queue the node may implement internally
 * This has to consider ALTQ if we are to work with it.
 * As far as I can see, ALTQ counts PACKETS, not bytes.
 * If ALTQ has several queues and one has passed a watermark
 * we should have the priority of that queue be real (and not -1)
 * XXX ALTQ stuff is just an idea.....
 */
struct ngm_queue_state {
	u_int queue_priority; /* maybe only low-pri is full. -1 = all*/
	u_int	max_queuelen_bytes;
	u_int	max_queuelen_packets;
	u_int	low_watermark;
	u_int	high_watermark;
	u_int	current;
};

/* Keep this in sync with the above structure definition */
#define NG_GENERIC_QUEUE_INFO()	{				\
	  { "max_queuelen_bytes", &ng_parse_uint_type	},	\
	  { "max_queuelen_packets", &ng_parse_uint_type	},	\
	  { "high_watermark",	&ng_parse_uint_type	},	\
	  { "low_watermark",	&ng_parse_uint_type	},	\
	  { "current",		&ng_parse_uint_type	},	\
	  { NULL }						\
}

/* Tell a node who to send async flow control info to. */
struct flow_manager {
	ng_ID_t		id;			/* unique identifier */
};

/* Keep this in sync with the above structure definition */
#define NG_GENERIC_FLOW_MANAGER_INFO()	{			\
	  { "id",		&ng_parse_hint32_type	},	\
	  { NULL }						\
}


/*
 * For netgraph nodes that are somehow associated with file descriptors
 * (e.g., a device that has a /dev entry and is also a netgraph node),
 * we define a generic ioctl for requesting the corresponding nodeinfo
 * structure and for assigning a name (if there isn't one already).
 *
 * For these to you need to also #include <sys/ioccom.h>.
 */

#define NGIOCGINFO	_IOR('N', 40, struct nodeinfo)	/* get node info */
#define NGIOCSETNAME	_IOW('N', 41, struct ngm_name)	/* set node name */

#ifdef _KERNEL
/*
 * Allocate and initialize a netgraph message "msg" with "len"
 * extra bytes of argument. Sets "msg" to NULL if fails.
 * Does not initialize token.
 */
#define NG_MKMESSAGE(msg, cookie, cmdid, len, how)			\
	do {								\
	  (msg) = malloc(sizeof(struct ng_mesg)				\
	    + (len), M_NETGRAPH_MSG, (how) | M_ZERO);			\
	  if ((msg) == NULL)						\
	    break;							\
	  (msg)->header.version = NG_VERSION;				\
	  (msg)->header.typecookie = (cookie);				\
	  (msg)->header.cmd = (cmdid);					\
	  (msg)->header.arglen = (len);					\
	  strncpy((msg)->header.cmdstr, #cmdid,				\
	    sizeof((msg)->header.cmdstr) - 1);				\
	} while (0)

/*
 * Allocate and initialize a response "rsp" to a message "msg"
 * with "len" extra bytes of argument. Sets "rsp" to NULL if fails.
 */
#define NG_MKRESPONSE(rsp, msg, len, how)				\
	do {								\
	  (rsp) = malloc(sizeof(struct ng_mesg)				\
	    + (len), M_NETGRAPH_MSG, (how) | M_ZERO);			\
	  if ((rsp) == NULL)						\
	    break;							\
	  (rsp)->header.version = NG_VERSION;				\
	  (rsp)->header.arglen = (len);					\
	  (rsp)->header.token = (msg)->header.token;			\
	  (rsp)->header.typecookie = (msg)->header.typecookie;		\
	  (rsp)->header.cmd = (msg)->header.cmd;			\
	  bcopy((msg)->header.cmdstr, (rsp)->header.cmdstr,		\
	    sizeof((rsp)->header.cmdstr));				\
	  (rsp)->header.flags |= NGF_RESP;				\
	} while (0)

/*
 * Make a copy of message. Sets "copy" to NULL if fails.
 */
#define	NG_COPYMESSAGE(copy, msg, how)					\
	do {								\
	  (copy) = malloc(sizeof(struct ng_mesg)			\
	    + (msg)->header.arglen, M_NETGRAPH_MSG, (how) | M_ZERO);	\
	  if ((copy) == NULL)						\
	    break;							\
	  (copy)->header.version = NG_VERSION;				\
	  (copy)->header.arglen = (msg)->header.arglen;			\
	  (copy)->header.token = (msg)->header.token;			\
	  (copy)->header.typecookie = (msg)->header.typecookie;		\
	  (copy)->header.cmd = (msg)->header.cmd;			\
	  (copy)->header.flags = (msg)->header.flags;			\
	  bcopy((msg)->header.cmdstr, (copy)->header.cmdstr,		\
	    sizeof((copy)->header.cmdstr));				\
	  if ((msg)->header.arglen > 0)					\
	    bcopy((msg)->data, (copy)->data, (msg)->header.arglen);	\
	} while (0)

#endif /* _KERNEL */

#endif /* _NETGRAPH_NG_MESSAGE_H_ */
