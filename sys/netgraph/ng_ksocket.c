/*
 * ng_ksocket.c
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
 * $Whistle: ng_ksocket.c,v 1.1 1999/11/16 20:04:40 archie Exp $
 */

/*
 * Kernel socket node type.  This node type is basically a kernel-mode
 * version of a socket... kindof like the reverse of the socket node type.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/mbuf.h>
#include <sys/proc.h>
#include <sys/malloc.h>
#include <sys/ctype.h>
#include <sys/protosw.h>
#include <sys/errno.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/uio.h>
#include <sys/un.h>

#include <netgraph/ng_message.h>
#include <netgraph/netgraph.h>
#include <netgraph/ng_parse.h>
#include <netgraph/ng_ksocket.h>

#include <netinet/in.h>
#include <netinet/ip.h>

#ifdef NG_SEPARATE_MALLOC
static MALLOC_DEFINE(M_NETGRAPH_KSOCKET, "netgraph_ksock",
    "netgraph ksock node");
#else
#define M_NETGRAPH_KSOCKET M_NETGRAPH
#endif

#define OFFSETOF(s, e) ((char *)&((s *)0)->e - (char *)((s *)0))
#define SADATA_OFFSET	(OFFSETOF(struct sockaddr, sa_data))

/* Node private data */
struct ng_ksocket_private {
	node_p		node;
	hook_p		hook;
	struct socket	*so;
	int		fn_sent;	/* FN call on incoming event was sent */
	LIST_HEAD(, ng_ksocket_private)	embryos;
	LIST_ENTRY(ng_ksocket_private)	siblings;
	u_int32_t	flags;
	u_int32_t	response_token;
	ng_ID_t		response_addr;
};
typedef struct ng_ksocket_private *priv_p;

/* Flags for priv_p */
#define	KSF_CONNECTING	0x00000001	/* Waiting for connection complete */
#define	KSF_ACCEPTING	0x00000002	/* Waiting for accept complete */
#define	KSF_EOFSEEN	0x00000004	/* Have sent 0-length EOF mbuf */
#define	KSF_CLONED	0x00000008	/* Cloned from an accepting socket */
#define	KSF_EMBRYONIC	0x00000010	/* Cloned node with no hooks yet */

/* Netgraph node methods */
static ng_constructor_t	ng_ksocket_constructor;
static ng_rcvmsg_t	ng_ksocket_rcvmsg;
static ng_shutdown_t	ng_ksocket_shutdown;
static ng_newhook_t	ng_ksocket_newhook;
static ng_rcvdata_t	ng_ksocket_rcvdata;
static ng_connect_t	ng_ksocket_connect;
static ng_disconnect_t	ng_ksocket_disconnect;

/* Alias structure */
struct ng_ksocket_alias {
	const char	*name;
	const int	value;
	const int	family;
};

/* Protocol family aliases */
static const struct ng_ksocket_alias ng_ksocket_families[] = {
	{ "local",	PF_LOCAL	},
	{ "inet",	PF_INET		},
	{ "inet6",	PF_INET6	},
	{ "atm",	PF_ATM		},
	{ NULL,		-1		},
};

/* Socket type aliases */
static const struct ng_ksocket_alias ng_ksocket_types[] = {
	{ "stream",	SOCK_STREAM	},
	{ "dgram",	SOCK_DGRAM	},
	{ "raw",	SOCK_RAW	},
	{ "rdm",	SOCK_RDM	},
	{ "seqpacket",	SOCK_SEQPACKET	},
	{ NULL,		-1		},
};

/* Protocol aliases */
static const struct ng_ksocket_alias ng_ksocket_protos[] = {
	{ "ip",		IPPROTO_IP,		PF_INET		},
	{ "raw",	IPPROTO_RAW,		PF_INET		},
	{ "icmp",	IPPROTO_ICMP,		PF_INET		},
	{ "igmp",	IPPROTO_IGMP,		PF_INET		},
	{ "tcp",	IPPROTO_TCP,		PF_INET		},
	{ "udp",	IPPROTO_UDP,		PF_INET		},
	{ "gre",	IPPROTO_GRE,		PF_INET		},
	{ "esp",	IPPROTO_ESP,		PF_INET		},
	{ "ah",		IPPROTO_AH,		PF_INET		},
	{ "swipe",	IPPROTO_SWIPE,		PF_INET		},
	{ "encap",	IPPROTO_ENCAP,		PF_INET		},
	{ "divert",	IPPROTO_DIVERT,		PF_INET		},
	{ "pim",	IPPROTO_PIM,		PF_INET		},
	{ NULL,		-1					},
};

/* Helper functions */
static int	ng_ksocket_accept(priv_p);
static int	ng_ksocket_incoming(struct socket *so, void *arg, int waitflag);
static int	ng_ksocket_parse(const struct ng_ksocket_alias *aliases,
			const char *s, int family);
static void	ng_ksocket_incoming2(node_p node, hook_p hook,
			void *arg1, int arg2);

/************************************************************************
			STRUCT SOCKADDR PARSE TYPE
 ************************************************************************/

/* Get the length of the data portion of a generic struct sockaddr */
static int
ng_parse_generic_sockdata_getLength(const struct ng_parse_type *type,
	const u_char *start, const u_char *buf)
{
	const struct sockaddr *sa;

	sa = (const struct sockaddr *)(buf - SADATA_OFFSET);
	return (sa->sa_len < SADATA_OFFSET) ? 0 : sa->sa_len - SADATA_OFFSET;
}

/* Type for the variable length data portion of a generic struct sockaddr */
static const struct ng_parse_type ng_ksocket_generic_sockdata_type = {
	&ng_parse_bytearray_type,
	&ng_parse_generic_sockdata_getLength
};

/* Type for a generic struct sockaddr */
static const struct ng_parse_struct_field
    ng_parse_generic_sockaddr_type_fields[] = {
	  { "len",	&ng_parse_uint8_type			},
	  { "family",	&ng_parse_uint8_type			},
	  { "data",	&ng_ksocket_generic_sockdata_type	},
	  { NULL }
};
static const struct ng_parse_type ng_ksocket_generic_sockaddr_type = {
	&ng_parse_struct_type,
	&ng_parse_generic_sockaddr_type_fields
};

/* Convert a struct sockaddr from ASCII to binary.  If its a protocol
   family that we specially handle, do that, otherwise defer to the
   generic parse type ng_ksocket_generic_sockaddr_type. */
static int
ng_ksocket_sockaddr_parse(const struct ng_parse_type *type,
	const char *s, int *off, const u_char *const start,
	u_char *const buf, int *buflen)
{
	struct sockaddr *const sa = (struct sockaddr *)buf;
	enum ng_parse_token tok;
	char fambuf[32];
	int family, len;
	char *t;

	/* If next token is a left curly brace, use generic parse type */
	if ((tok = ng_parse_get_token(s, off, &len)) == T_LBRACE) {
		return (*ng_ksocket_generic_sockaddr_type.supertype->parse)
		    (&ng_ksocket_generic_sockaddr_type,
		    s, off, start, buf, buflen);
	}

	/* Get socket address family followed by a slash */
	while (isspace(s[*off]))
		(*off)++;
	if ((t = strchr(s + *off, '/')) == NULL)
		return (EINVAL);
	if ((len = t - (s + *off)) > sizeof(fambuf) - 1)
		return (EINVAL);
	strncpy(fambuf, s + *off, len);
	fambuf[len] = '\0';
	*off += len + 1;
	if ((family = ng_ksocket_parse(ng_ksocket_families, fambuf, 0)) == -1)
		return (EINVAL);

	/* Set family */
	if (*buflen < SADATA_OFFSET)
		return (ERANGE);
	sa->sa_family = family;

	/* Set family-specific data and length */
	switch (sa->sa_family) {
	case PF_LOCAL:		/* Get pathname */
	    {
		const int pathoff = OFFSETOF(struct sockaddr_un, sun_path);
		struct sockaddr_un *const sun = (struct sockaddr_un *)sa;
		int toklen, pathlen;
		char *path;

		if ((path = ng_get_string_token(s, off, &toklen, NULL)) == NULL)
			return (EINVAL);
		pathlen = strlen(path);
		if (pathlen > SOCK_MAXADDRLEN) {
			free(path, M_NETGRAPH_KSOCKET);
			return (E2BIG);
		}
		if (*buflen < pathoff + pathlen) {
			free(path, M_NETGRAPH_KSOCKET);
			return (ERANGE);
		}
		*off += toklen;
		bcopy(path, sun->sun_path, pathlen);
		sun->sun_len = pathoff + pathlen;
		free(path, M_NETGRAPH_KSOCKET);
		break;
	    }

	case PF_INET:		/* Get an IP address with optional port */
	    {
		struct sockaddr_in *const sin = (struct sockaddr_in *)sa;
		int i;

		/* Parse this: <ipaddress>[:port] */
		for (i = 0; i < 4; i++) {
			u_long val;
			char *eptr;

			val = strtoul(s + *off, &eptr, 10);
			if (val > 0xff || eptr == s + *off)
				return (EINVAL);
			*off += (eptr - (s + *off));
			((u_char *)&sin->sin_addr)[i] = (u_char)val;
			if (i < 3) {
				if (s[*off] != '.')
					return (EINVAL);
				(*off)++;
			} else if (s[*off] == ':') {
				(*off)++;
				val = strtoul(s + *off, &eptr, 10);
				if (val > 0xffff || eptr == s + *off)
					return (EINVAL);
				*off += (eptr - (s + *off));
				sin->sin_port = htons(val);
			} else
				sin->sin_port = 0;
		}
		bzero(&sin->sin_zero, sizeof(sin->sin_zero));
		sin->sin_len = sizeof(*sin);
		break;
	    }

#if 0
	case PF_INET6:	/* XXX implement this someday */
#endif

	default:
		return (EINVAL);
	}

	/* Done */
	*buflen = sa->sa_len;
	return (0);
}

/* Convert a struct sockaddr from binary to ASCII */
static int
ng_ksocket_sockaddr_unparse(const struct ng_parse_type *type,
	const u_char *data, int *off, char *cbuf, int cbuflen)
{
	const struct sockaddr *sa = (const struct sockaddr *)(data + *off);
	int slen = 0;

	/* Output socket address, either in special or generic format */
	switch (sa->sa_family) {
	case PF_LOCAL:
	    {
		const int pathoff = OFFSETOF(struct sockaddr_un, sun_path);
		const struct sockaddr_un *sun = (const struct sockaddr_un *)sa;
		const int pathlen = sun->sun_len - pathoff;
		char pathbuf[SOCK_MAXADDRLEN + 1];
		char *pathtoken;

		bcopy(sun->sun_path, pathbuf, pathlen);
		if ((pathtoken = ng_encode_string(pathbuf, pathlen)) == NULL)
			return (ENOMEM);
		slen += snprintf(cbuf, cbuflen, "local/%s", pathtoken);
		free(pathtoken, M_NETGRAPH_KSOCKET);
		if (slen >= cbuflen)
			return (ERANGE);
		*off += sun->sun_len;
		return (0);
	    }

	case PF_INET:
	    {
		const struct sockaddr_in *sin = (const struct sockaddr_in *)sa;

		slen += snprintf(cbuf, cbuflen, "inet/%d.%d.%d.%d",
		  ((const u_char *)&sin->sin_addr)[0],
		  ((const u_char *)&sin->sin_addr)[1],
		  ((const u_char *)&sin->sin_addr)[2],
		  ((const u_char *)&sin->sin_addr)[3]);
		if (sin->sin_port != 0) {
			slen += snprintf(cbuf + strlen(cbuf),
			    cbuflen - strlen(cbuf), ":%d",
			    (u_int)ntohs(sin->sin_port));
		}
		if (slen >= cbuflen)
			return (ERANGE);
		*off += sizeof(*sin);
		return(0);
	    }

#if 0
	case PF_INET6:	/* XXX implement this someday */
#endif

	default:
		return (*ng_ksocket_generic_sockaddr_type.supertype->unparse)
		    (&ng_ksocket_generic_sockaddr_type,
		    data, off, cbuf, cbuflen);
	}
}

/* Parse type for struct sockaddr */
static const struct ng_parse_type ng_ksocket_sockaddr_type = {
	NULL,
	NULL,
	NULL,
	&ng_ksocket_sockaddr_parse,
	&ng_ksocket_sockaddr_unparse,
	NULL		/* no such thing as a default struct sockaddr */
};

/************************************************************************
		STRUCT NG_KSOCKET_SOCKOPT PARSE TYPE
 ************************************************************************/

/* Get length of the struct ng_ksocket_sockopt value field, which is the
   just the excess of the message argument portion over the length of
   the struct ng_ksocket_sockopt. */
static int
ng_parse_sockoptval_getLength(const struct ng_parse_type *type,
	const u_char *start, const u_char *buf)
{
	static const int offset = OFFSETOF(struct ng_ksocket_sockopt, value);
	const struct ng_ksocket_sockopt *sopt;
	const struct ng_mesg *msg;

	sopt = (const struct ng_ksocket_sockopt *)(buf - offset);
	msg = (const struct ng_mesg *)((const u_char *)sopt - sizeof(*msg));
	return msg->header.arglen - sizeof(*sopt);
}

/* Parse type for the option value part of a struct ng_ksocket_sockopt
   XXX Eventually, we should handle the different socket options specially.
   XXX This would avoid byte order problems, eg an integer value of 1 is
   XXX going to be "[1]" for little endian or "[3=1]" for big endian. */
static const struct ng_parse_type ng_ksocket_sockoptval_type = {
	&ng_parse_bytearray_type,
	&ng_parse_sockoptval_getLength
};

/* Parse type for struct ng_ksocket_sockopt */
static const struct ng_parse_struct_field ng_ksocket_sockopt_type_fields[]
	= NG_KSOCKET_SOCKOPT_INFO(&ng_ksocket_sockoptval_type);
static const struct ng_parse_type ng_ksocket_sockopt_type = {
	&ng_parse_struct_type,
	&ng_ksocket_sockopt_type_fields
};

/* Parse type for struct ng_ksocket_accept */
static const struct ng_parse_struct_field ng_ksocket_accept_type_fields[]
	= NGM_KSOCKET_ACCEPT_INFO;
static const struct ng_parse_type ng_ksocket_accept_type = {
	&ng_parse_struct_type,
	&ng_ksocket_accept_type_fields
};

/* List of commands and how to convert arguments to/from ASCII */
static const struct ng_cmdlist ng_ksocket_cmds[] = {
	{
	  NGM_KSOCKET_COOKIE,
	  NGM_KSOCKET_BIND,
	  "bind",
	  &ng_ksocket_sockaddr_type,
	  NULL
	},
	{
	  NGM_KSOCKET_COOKIE,
	  NGM_KSOCKET_LISTEN,
	  "listen",
	  &ng_parse_int32_type,
	  NULL
	},
	{
	  NGM_KSOCKET_COOKIE,
	  NGM_KSOCKET_ACCEPT,
	  "accept",
	  NULL,
	  &ng_ksocket_accept_type
	},
	{
	  NGM_KSOCKET_COOKIE,
	  NGM_KSOCKET_CONNECT,
	  "connect",
	  &ng_ksocket_sockaddr_type,
	  &ng_parse_int32_type
	},
	{
	  NGM_KSOCKET_COOKIE,
	  NGM_KSOCKET_GETNAME,
	  "getname",
	  NULL,
	  &ng_ksocket_sockaddr_type
	},
	{
	  NGM_KSOCKET_COOKIE,
	  NGM_KSOCKET_GETPEERNAME,
	  "getpeername",
	  NULL,
	  &ng_ksocket_sockaddr_type
	},
	{
	  NGM_KSOCKET_COOKIE,
	  NGM_KSOCKET_SETOPT,
	  "setopt",
	  &ng_ksocket_sockopt_type,
	  NULL
	},
	{
	  NGM_KSOCKET_COOKIE,
	  NGM_KSOCKET_GETOPT,
	  "getopt",
	  &ng_ksocket_sockopt_type,
	  &ng_ksocket_sockopt_type
	},
	{ 0 }
};

/* Node type descriptor */
static struct ng_type ng_ksocket_typestruct = {
	.version =	NG_ABI_VERSION,
	.name =		NG_KSOCKET_NODE_TYPE,
	.constructor =	ng_ksocket_constructor,
	.rcvmsg =	ng_ksocket_rcvmsg,
	.shutdown =	ng_ksocket_shutdown,
	.newhook =	ng_ksocket_newhook,
	.connect =	ng_ksocket_connect,
	.rcvdata =	ng_ksocket_rcvdata,
	.disconnect =	ng_ksocket_disconnect,
	.cmdlist =	ng_ksocket_cmds,
};
NETGRAPH_INIT(ksocket, &ng_ksocket_typestruct);

#define ERROUT(x)	do { error = (x); goto done; } while (0)

/************************************************************************
			NETGRAPH NODE STUFF
 ************************************************************************/

/*
 * Node type constructor
 * The NODE part is assumed to be all set up.
 * There is already a reference to the node for us.
 */
static int
ng_ksocket_constructor(node_p node)
{
	priv_p priv;

	/* Allocate private structure */
	priv = malloc(sizeof(*priv), M_NETGRAPH_KSOCKET, M_NOWAIT | M_ZERO);
	if (priv == NULL)
		return (ENOMEM);

	LIST_INIT(&priv->embryos);
	/* cross link them */
	priv->node = node;
	NG_NODE_SET_PRIVATE(node, priv);

	/* Done */
	return (0);
}

/*
 * Give our OK for a hook to be added. The hook name is of the
 * form "<family>/<type>/<proto>" where the three components may
 * be decimal numbers or else aliases from the above lists.
 *
 * Connecting a hook amounts to opening the socket.  Disconnecting
 * the hook closes the socket and destroys the node as well.
 */
static int
ng_ksocket_newhook(node_p node, hook_p hook, const char *name0)
{
	struct thread *td = curthread;	/* XXX broken */
	const priv_p priv = NG_NODE_PRIVATE(node);
	char *s1, *s2, name[NG_HOOKSIZ];
	int family, type, protocol, error;

	/* Check if we're already connected */
	if (priv->hook != NULL)
		return (EISCONN);

	if (priv->flags & KSF_CLONED) {
		if (priv->flags & KSF_EMBRYONIC) {
			/* Remove ourselves from our parent's embryo list */
			LIST_REMOVE(priv, siblings);
			priv->flags &= ~KSF_EMBRYONIC;
		}
	} else {
		/* Extract family, type, and protocol from hook name */
		snprintf(name, sizeof(name), "%s", name0);
		s1 = name;
		if ((s2 = strchr(s1, '/')) == NULL)
			return (EINVAL);
		*s2++ = '\0';
		family = ng_ksocket_parse(ng_ksocket_families, s1, 0);
		if (family == -1)
			return (EINVAL);
		s1 = s2;
		if ((s2 = strchr(s1, '/')) == NULL)
			return (EINVAL);
		*s2++ = '\0';
		type = ng_ksocket_parse(ng_ksocket_types, s1, 0);
		if (type == -1)
			return (EINVAL);
		s1 = s2;
		protocol = ng_ksocket_parse(ng_ksocket_protos, s1, family);
		if (protocol == -1)
			return (EINVAL);

		/* Create the socket */
		error = socreate(family, &priv->so, type, protocol,
		   td->td_ucred, td);
		if (error != 0)
			return (error);

		/* XXX call soreserve() ? */

	}

	/* OK */
	priv->hook = hook;

	/*
	 * In case of misconfigured routing a packet may reenter
	 * ksocket node recursively. Decouple stack to avoid possible
	 * panics about sleeping with locks held.
	 */
	NG_HOOK_FORCE_QUEUE(hook);

	return(0);
}

static int
ng_ksocket_connect(hook_p hook)
{
	node_p node = NG_HOOK_NODE(hook);
	const priv_p priv = NG_NODE_PRIVATE(node);
	struct socket *const so = priv->so;

	/* Add our hook for incoming data and other events */
	SOCKBUF_LOCK(&priv->so->so_rcv);
	soupcall_set(priv->so, SO_RCV, ng_ksocket_incoming, node);
	SOCKBUF_UNLOCK(&priv->so->so_rcv);
	SOCKBUF_LOCK(&priv->so->so_snd);
	soupcall_set(priv->so, SO_SND, ng_ksocket_incoming, node);
	SOCKBUF_UNLOCK(&priv->so->so_snd);
	SOCK_LOCK(priv->so);
	priv->so->so_state |= SS_NBIO;
	SOCK_UNLOCK(priv->so);
	/*
	 * --Original comment--
	 * On a cloned socket we may have already received one or more
	 * upcalls which we couldn't handle without a hook.  Handle
	 * those now.
	 * We cannot call the upcall function directly
	 * from here, because until this function has returned our
	 * hook isn't connected.
	 *
	 * ---meta comment for -current ---
	 * XXX This is dubius.
	 * Upcalls between the time that the hook was
	 * first created and now (on another processesor) will
	 * be earlier on the queue than the request to finalise the hook.
	 * By the time the hook is finalised,
	 * The queued upcalls will have happened and the code
	 * will have discarded them because of a lack of a hook.
	 * (socket not open).
	 *
	 * This is a bad byproduct of the complicated way in which hooks
	 * are now created (3 daisy chained async events).
	 *
	 * Since we are a netgraph operation
	 * We know that we hold a lock on this node. This forces the
	 * request we make below to be queued rather than implemented
	 * immediately which will cause the upcall function to be called a bit
	 * later.
	 * However, as we will run any waiting queued operations immediately
	 * after doing this one, if we have not finalised the other end
	 * of the hook, those queued operations will fail.
	 */
	if (priv->flags & KSF_CLONED) {
		ng_send_fn(node, NULL, &ng_ksocket_incoming2, so, M_NOWAIT);
	}

	return (0);
}

/*
 * Receive a control message
 */
static int
ng_ksocket_rcvmsg(node_p node, item_p item, hook_p lasthook)
{
	struct thread *td = curthread;	/* XXX broken */
	const priv_p priv = NG_NODE_PRIVATE(node);
	struct socket *const so = priv->so;
	struct ng_mesg *resp = NULL;
	int error = 0;
	struct ng_mesg *msg;
	ng_ID_t raddr;

	NGI_GET_MSG(item, msg);
	switch (msg->header.typecookie) {
	case NGM_KSOCKET_COOKIE:
		switch (msg->header.cmd) {
		case NGM_KSOCKET_BIND:
		    {
			struct sockaddr *const sa
			    = (struct sockaddr *)msg->data;

			/* Sanity check */
			if (msg->header.arglen < SADATA_OFFSET
			    || msg->header.arglen < sa->sa_len)
				ERROUT(EINVAL);
			if (so == NULL)
				ERROUT(ENXIO);

			/* Bind */
			error = sobind(so, sa, td);
			break;
		    }
		case NGM_KSOCKET_LISTEN:
		    {
			/* Sanity check */
			if (msg->header.arglen != sizeof(int32_t))
				ERROUT(EINVAL);
			if (so == NULL)
				ERROUT(ENXIO);

			/* Listen */
			so->so_state |= SS_NBIO;
			error = solisten(so, *((int32_t *)msg->data), td);
			break;
		    }

		case NGM_KSOCKET_ACCEPT:
		    {
			/* Sanity check */
			if (msg->header.arglen != 0)
				ERROUT(EINVAL);
			if (so == NULL)
				ERROUT(ENXIO);

			/* Make sure the socket is capable of accepting */
			if (!(so->so_options & SO_ACCEPTCONN))
				ERROUT(EINVAL);
			if (priv->flags & KSF_ACCEPTING)
				ERROUT(EALREADY);

			/*
			 * If a connection is already complete, take it.
			 * Otherwise let the upcall function deal with
			 * the connection when it comes in.
			 */
			error = ng_ksocket_accept(priv);
			if (error != 0 && error != EWOULDBLOCK)
				ERROUT(error);
			priv->response_token = msg->header.token;
			raddr = priv->response_addr = NGI_RETADDR(item);
			break;
		    }

		case NGM_KSOCKET_CONNECT:
		    {
			struct sockaddr *const sa
			    = (struct sockaddr *)msg->data;

			/* Sanity check */
			if (msg->header.arglen < SADATA_OFFSET
			    || msg->header.arglen < sa->sa_len)
				ERROUT(EINVAL);
			if (so == NULL)
				ERROUT(ENXIO);

			/* Do connect */
			if ((so->so_state & SS_ISCONNECTING) != 0)
				ERROUT(EALREADY);
			if ((error = soconnect(so, sa, td)) != 0) {
				so->so_state &= ~SS_ISCONNECTING;
				ERROUT(error);
			}
			if ((so->so_state & SS_ISCONNECTING) != 0) {
				/* We will notify the sender when we connect */
				priv->response_token = msg->header.token;
				raddr = priv->response_addr = NGI_RETADDR(item);
				priv->flags |= KSF_CONNECTING;
				ERROUT(EINPROGRESS);
			}
			break;
		    }

		case NGM_KSOCKET_GETNAME:
		case NGM_KSOCKET_GETPEERNAME:
		    {
			int (*func)(struct socket *so, struct sockaddr **nam);
			struct sockaddr *sa = NULL;
			int len;

			/* Sanity check */
			if (msg->header.arglen != 0)
				ERROUT(EINVAL);
			if (so == NULL)
				ERROUT(ENXIO);

			/* Get function */
			if (msg->header.cmd == NGM_KSOCKET_GETPEERNAME) {
				if ((so->so_state
				    & (SS_ISCONNECTED|SS_ISCONFIRMING)) == 0)
					ERROUT(ENOTCONN);
				func = so->so_proto->pr_usrreqs->pru_peeraddr;
			} else
				func = so->so_proto->pr_usrreqs->pru_sockaddr;

			/* Get local or peer address */
			if ((error = (*func)(so, &sa)) != 0)
				goto bail;
			len = (sa == NULL) ? 0 : sa->sa_len;

			/* Send it back in a response */
			NG_MKRESPONSE(resp, msg, len, M_NOWAIT);
			if (resp == NULL) {
				error = ENOMEM;
				goto bail;
			}
			bcopy(sa, resp->data, len);

		bail:
			/* Cleanup */
			if (sa != NULL)
				free(sa, M_SONAME);
			break;
		    }

		case NGM_KSOCKET_GETOPT:
		    {
			struct ng_ksocket_sockopt *ksopt =
			    (struct ng_ksocket_sockopt *)msg->data;
			struct sockopt sopt;

			/* Sanity check */
			if (msg->header.arglen != sizeof(*ksopt))
				ERROUT(EINVAL);
			if (so == NULL)
				ERROUT(ENXIO);

			/* Get response with room for option value */
			NG_MKRESPONSE(resp, msg, sizeof(*ksopt)
			    + NG_KSOCKET_MAX_OPTLEN, M_NOWAIT);
			if (resp == NULL)
				ERROUT(ENOMEM);

			/* Get socket option, and put value in the response */
			sopt.sopt_dir = SOPT_GET;
			sopt.sopt_level = ksopt->level;
			sopt.sopt_name = ksopt->name;
			sopt.sopt_td = NULL;
			sopt.sopt_valsize = NG_KSOCKET_MAX_OPTLEN;
			ksopt = (struct ng_ksocket_sockopt *)resp->data;
			sopt.sopt_val = ksopt->value;
			if ((error = sogetopt(so, &sopt)) != 0) {
				NG_FREE_MSG(resp);
				break;
			}

			/* Set actual value length */
			resp->header.arglen = sizeof(*ksopt)
			    + sopt.sopt_valsize;
			break;
		    }

		case NGM_KSOCKET_SETOPT:
		    {
			struct ng_ksocket_sockopt *const ksopt =
			    (struct ng_ksocket_sockopt *)msg->data;
			const int valsize = msg->header.arglen - sizeof(*ksopt);
			struct sockopt sopt;

			/* Sanity check */
			if (valsize < 0)
				ERROUT(EINVAL);
			if (so == NULL)
				ERROUT(ENXIO);

			/* Set socket option */
			sopt.sopt_dir = SOPT_SET;
			sopt.sopt_level = ksopt->level;
			sopt.sopt_name = ksopt->name;
			sopt.sopt_val = ksopt->value;
			sopt.sopt_valsize = valsize;
			sopt.sopt_td = NULL;
			error = sosetopt(so, &sopt);
			break;
		    }

		default:
			error = EINVAL;
			break;
		}
		break;
	default:
		error = EINVAL;
		break;
	}
done:
	NG_RESPOND_MSG(error, node, item, resp);
	NG_FREE_MSG(msg);
	return (error);
}

/*
 * Receive incoming data on our hook.  Send it out the socket.
 */
static int
ng_ksocket_rcvdata(hook_p hook, item_p item)
{
	struct thread *td = curthread;	/* XXX broken */
	const node_p node = NG_HOOK_NODE(hook);
	const priv_p priv = NG_NODE_PRIVATE(node);
	struct socket *const so = priv->so;
	struct sockaddr *sa = NULL;
	int error;
	struct mbuf *m;
#ifdef ALIGNED_POINTER
	struct mbuf *n;
#endif /* ALIGNED_POINTER */
	struct sa_tag *stag;

	/* Extract data */
	NGI_GET_M(item, m);
	NG_FREE_ITEM(item);
#ifdef ALIGNED_POINTER
	if (!ALIGNED_POINTER(mtod(m, caddr_t), uint32_t)) {
		n = m_defrag(m, M_NOWAIT);
		if (n == NULL) {
			m_freem(m);
			return (ENOBUFS);
		}
		m = n;
	}
#endif /* ALIGNED_POINTER */
	/*
	 * Look if socket address is stored in packet tags.
	 * If sockaddr is ours, or provided by a third party (zero id),
	 * then we accept it.
	 */
	if (((stag = (struct sa_tag *)m_tag_locate(m, NGM_KSOCKET_COOKIE,
	    NG_KSOCKET_TAG_SOCKADDR, NULL)) != NULL) &&
	    (stag->id == NG_NODE_ID(node) || stag->id == 0))
		sa = &stag->sa;

	/* Reset specific mbuf flags to prevent addressing problems. */
	m->m_flags &= ~(M_BCAST|M_MCAST);

	/* Send packet */
	error = sosend(so, sa, 0, m, 0, 0, td);

	return (error);
}

/*
 * Destroy node
 */
static int
ng_ksocket_shutdown(node_p node)
{
	const priv_p priv = NG_NODE_PRIVATE(node);
	priv_p embryo;

	/* Close our socket (if any) */
	if (priv->so != NULL) {
		SOCKBUF_LOCK(&priv->so->so_rcv);
		soupcall_clear(priv->so, SO_RCV);
		SOCKBUF_UNLOCK(&priv->so->so_rcv);
		SOCKBUF_LOCK(&priv->so->so_snd);
		soupcall_clear(priv->so, SO_SND);
		SOCKBUF_UNLOCK(&priv->so->so_snd);
		soclose(priv->so);
		priv->so = NULL;
	}

	/* If we are an embryo, take ourselves out of the parent's list */
	if (priv->flags & KSF_EMBRYONIC) {
		LIST_REMOVE(priv, siblings);
		priv->flags &= ~KSF_EMBRYONIC;
	}

	/* Remove any embryonic children we have */
	while (!LIST_EMPTY(&priv->embryos)) {
		embryo = LIST_FIRST(&priv->embryos);
		ng_rmnode_self(embryo->node);
	}

	/* Take down netgraph node */
	bzero(priv, sizeof(*priv));
	free(priv, M_NETGRAPH_KSOCKET);
	NG_NODE_SET_PRIVATE(node, NULL);
	NG_NODE_UNREF(node);		/* let the node escape */
	return (0);
}

/*
 * Hook disconnection
 */
static int
ng_ksocket_disconnect(hook_p hook)
{
	KASSERT(NG_NODE_NUMHOOKS(NG_HOOK_NODE(hook)) == 0,
	    ("%s: numhooks=%d?", __func__,
	    NG_NODE_NUMHOOKS(NG_HOOK_NODE(hook))));
	if (NG_NODE_IS_VALID(NG_HOOK_NODE(hook)))
		ng_rmnode_self(NG_HOOK_NODE(hook));
	return (0);
}

/************************************************************************
			HELPER STUFF
 ************************************************************************/
/*
 * You should not "just call" a netgraph node function from an external
 * asynchronous event. This is because in doing so you are ignoring the
 * locking on the netgraph nodes. Instead call your function via ng_send_fn().
 * This will call the function you chose, but will first do all the
 * locking rigmarole. Your function MAY only be called at some distant future
 * time (several millisecs away) so don't give it any arguments
 * that may be revoked soon (e.g. on your stack).
 *
 * To decouple stack, we use queue version of ng_send_fn().
 */

static int
ng_ksocket_incoming(struct socket *so, void *arg, int waitflag)
{
	const node_p node = arg;
	const priv_p priv = NG_NODE_PRIVATE(node);
	int wait = ((waitflag & M_WAITOK) ? NG_WAITOK : 0) | NG_QUEUE;

	/*
	 * Even if node is not locked, as soon as we are called, we assume
	 * it exist and it's private area is valid. With some care we can
	 * access it. Mark node that incoming event for it was sent to
	 * avoid unneded queue trashing.
	 */
	if (atomic_cmpset_int(&priv->fn_sent, 0, 1) &&
	    ng_send_fn1(node, NULL, &ng_ksocket_incoming2, so, 0, wait)) {
		atomic_store_rel_int(&priv->fn_sent, 0);
	}
	return (SU_OK);
}


/*
 * When incoming data is appended to the socket, we get notified here.
 * This is also called whenever a significant event occurs for the socket.
 * Our original caller may have queued this even some time ago and
 * we cannot trust that he even still exists. The node however is being
 * held with a reference by the queueing code and guarantied to be valid.
 */
static void
ng_ksocket_incoming2(node_p node, hook_p hook, void *arg1, int arg2)
{
	struct socket *so = arg1;
	const priv_p priv = NG_NODE_PRIVATE(node);
	struct ng_mesg *response;
	int error;

	KASSERT(so == priv->so, ("%s: wrong socket", __func__));

	/* Allow next incoming event to be queued. */
	atomic_store_rel_int(&priv->fn_sent, 0);

	/* Check whether a pending connect operation has completed */
	if (priv->flags & KSF_CONNECTING) {
		if ((error = so->so_error) != 0) {
			so->so_error = 0;
			so->so_state &= ~SS_ISCONNECTING;
		}
		if (!(so->so_state & SS_ISCONNECTING)) {
			NG_MKMESSAGE(response, NGM_KSOCKET_COOKIE,
			    NGM_KSOCKET_CONNECT, sizeof(int32_t), M_NOWAIT);
			if (response != NULL) {
				response->header.flags |= NGF_RESP;
				response->header.token = priv->response_token;
				*(int32_t *)response->data = error;
				/*
				 * send an async "response" message
				 * to the node that set us up
				 * (if it still exists)
				 */
				NG_SEND_MSG_ID(error, node,
				    response, priv->response_addr, 0);
			}
			priv->flags &= ~KSF_CONNECTING;
		}
	}

	/* Check whether a pending accept operation has completed */
	if (priv->flags & KSF_ACCEPTING)
		(void )ng_ksocket_accept(priv);

	/*
	 * If we don't have a hook, we must handle data events later.  When
	 * the hook gets created and is connected, this upcall function
	 * will be called again.
	 */
	if (priv->hook == NULL)
		return;

	/* Read and forward available mbufs. */
	while (1) {
		struct uio uio;
		struct sockaddr *sa;
		struct mbuf *m;
		int flags;

		/* Try to get next packet from socket. */
		uio.uio_td = NULL;
		uio.uio_resid = IP_MAXPACKET;
		flags = MSG_DONTWAIT;
		sa = NULL;
		if ((error = soreceive(so, (so->so_state & SS_ISCONNECTED) ?
		    NULL : &sa, &uio, &m, NULL, &flags)) != 0)
			break;

		/* See if we got anything. */
		if (flags & MSG_TRUNC) {
			m_freem(m);
			m = NULL;
		}
		if (m == NULL) {
			if (sa != NULL)
				free(sa, M_SONAME);
			break;
		}

		KASSERT(m->m_nextpkt == NULL, ("%s: nextpkt", __func__));

		/*
		 * Stream sockets do not have packet boundaries, so
		 * we have to allocate a header mbuf and attach the
		 * stream of data to it.
		 */
		if (so->so_type == SOCK_STREAM) {
			struct mbuf *mh;

			mh = m_gethdr(M_NOWAIT, MT_DATA);
			if (mh == NULL) {
				m_freem(m);
				if (sa != NULL)
					free(sa, M_SONAME);
				break;
			}

			mh->m_next = m;
			for (; m; m = m->m_next)
				mh->m_pkthdr.len += m->m_len;
			m = mh;
		}

		/* Put peer's socket address (if any) into a tag */
		if (sa != NULL) {
			struct sa_tag	*stag;

			stag = (struct sa_tag *)m_tag_alloc(NGM_KSOCKET_COOKIE,
			    NG_KSOCKET_TAG_SOCKADDR, sizeof(ng_ID_t) +
			    sa->sa_len, M_NOWAIT);
			if (stag == NULL) {
				free(sa, M_SONAME);
				goto sendit;
			}
			bcopy(sa, &stag->sa, sa->sa_len);
			free(sa, M_SONAME);
			stag->id = NG_NODE_ID(node);
			m_tag_prepend(m, &stag->tag);
		}

sendit:		/* Forward data with optional peer sockaddr as packet tag */
		NG_SEND_DATA_ONLY(error, priv->hook, m);
	}

	/*
	 * If the peer has closed the connection, forward a 0-length mbuf
	 * to indicate end-of-file.
	 */
	if (so->so_rcv.sb_state & SBS_CANTRCVMORE &&
	    !(priv->flags & KSF_EOFSEEN)) {
		struct mbuf *m;

		m = m_gethdr(M_NOWAIT, MT_DATA);
		if (m != NULL)
			NG_SEND_DATA_ONLY(error, priv->hook, m);
		priv->flags |= KSF_EOFSEEN;
	}
}

static int
ng_ksocket_accept(priv_p priv)
{
	struct socket *const head = priv->so;
	struct socket *so;
	struct sockaddr *sa = NULL;
	struct ng_mesg *resp;
	struct ng_ksocket_accept *resp_data;
	node_p node;
	priv_p priv2;
	int len;
	int error;

	SOLISTEN_LOCK(head);
	error = solisten_dequeue(head, &so, SOCK_NONBLOCK);
	if (error == EWOULDBLOCK) {
		priv->flags |= KSF_ACCEPTING;
		return (error);
	}
	priv->flags &= ~KSF_ACCEPTING;
	if (error)
		return (error);

	if ((error = soaccept(so, &sa)) != 0)
		return (error);

	len = OFFSETOF(struct ng_ksocket_accept, addr);
	if (sa != NULL)
		len += sa->sa_len;

	NG_MKMESSAGE(resp, NGM_KSOCKET_COOKIE, NGM_KSOCKET_ACCEPT, len,
	    M_NOWAIT);
	if (resp == NULL) {
		soclose(so);
		goto out;
	}
	resp->header.flags |= NGF_RESP;
	resp->header.token = priv->response_token;

	/* Clone a ksocket node to wrap the new socket */
	error = ng_make_node_common(&ng_ksocket_typestruct, &node);
	if (error) {
		free(resp, M_NETGRAPH);
		soclose(so);
		goto out;
	}

	if (ng_ksocket_constructor(node) != 0) {
		NG_NODE_UNREF(node);
		free(resp, M_NETGRAPH);
		soclose(so);
		goto out;
	}

	priv2 = NG_NODE_PRIVATE(node);
	priv2->so = so;
	priv2->flags |= KSF_CLONED | KSF_EMBRYONIC;

	/*
	 * Insert the cloned node into a list of embryonic children
	 * on the parent node.  When a hook is created on the cloned
	 * node it will be removed from this list.  When the parent
	 * is destroyed it will destroy any embryonic children it has.
	 */
	LIST_INSERT_HEAD(&priv->embryos, priv2, siblings);

	SOCKBUF_LOCK(&so->so_rcv);
	soupcall_set(so, SO_RCV, ng_ksocket_incoming, node);
	SOCKBUF_UNLOCK(&so->so_rcv);
	SOCKBUF_LOCK(&so->so_snd);
	soupcall_set(so, SO_SND, ng_ksocket_incoming, node);
	SOCKBUF_UNLOCK(&so->so_snd);

	/* Fill in the response data and send it or return it to the caller */
	resp_data = (struct ng_ksocket_accept *)resp->data;
	resp_data->nodeid = NG_NODE_ID(node);
	if (sa != NULL)
		bcopy(sa, &resp_data->addr, sa->sa_len);
	NG_SEND_MSG_ID(error, node, resp, priv->response_addr, 0);

out:
	if (sa != NULL)
		free(sa, M_SONAME);

	return (0);
}

/*
 * Parse out either an integer value or an alias.
 */
static int
ng_ksocket_parse(const struct ng_ksocket_alias *aliases,
	const char *s, int family)
{
	int k, val;
	char *eptr;

	/* Try aliases */
	for (k = 0; aliases[k].name != NULL; k++) {
		if (strcmp(s, aliases[k].name) == 0
		    && aliases[k].family == family)
			return aliases[k].value;
	}

	/* Try parsing as a number */
	val = (int)strtoul(s, &eptr, 10);
	if (val < 0 || *eptr != '\0')
		return (-1);
	return (val);
}

