/*
 * ng_pppoe.c
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
 * $Whistle: ng_pppoe.c,v 1.10 1999/11/01 09:24:52 julian Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/errno.h>
#include <sys/syslog.h>
#include <net/ethernet.h>

#include <netgraph/ng_message.h>
#include <netgraph/netgraph.h>
#include <netgraph/ng_parse.h>
#include <netgraph/ng_pppoe.h>
#include <netgraph/ng_ether.h>

#ifdef NG_SEPARATE_MALLOC
static MALLOC_DEFINE(M_NETGRAPH_PPPOE, "netgraph_pppoe", "netgraph pppoe node");
#else
#define M_NETGRAPH_PPPOE M_NETGRAPH
#endif

#define SIGNOFF "session closed"

/*
 * This section contains the netgraph method declarations for the
 * pppoe node. These methods define the netgraph pppoe 'type'.
 */

static ng_constructor_t	ng_pppoe_constructor;
static ng_rcvmsg_t	ng_pppoe_rcvmsg;
static ng_shutdown_t	ng_pppoe_shutdown;
static ng_newhook_t	ng_pppoe_newhook;
static ng_connect_t	ng_pppoe_connect;
static ng_rcvdata_t	ng_pppoe_rcvdata;
static ng_rcvdata_t	ng_pppoe_rcvdata_ether;
static ng_rcvdata_t	ng_pppoe_rcvdata_debug;
static ng_disconnect_t	ng_pppoe_disconnect;

/* Parse type for struct ngpppoe_init_data */
static const struct ng_parse_struct_field ngpppoe_init_data_type_fields[]
	= NG_PPPOE_INIT_DATA_TYPE_INFO;
static const struct ng_parse_type ngpppoe_init_data_state_type = {
	&ng_parse_struct_type,
	&ngpppoe_init_data_type_fields
};

/* Parse type for struct ngpppoe_sts */
static const struct ng_parse_struct_field ng_pppoe_sts_type_fields[]
	= NG_PPPOE_STS_TYPE_INFO;
static const struct ng_parse_type ng_pppoe_sts_state_type = {
	&ng_parse_struct_type,
	&ng_pppoe_sts_type_fields
};

/* List of commands and how to convert arguments to/from ASCII */
static const struct ng_cmdlist ng_pppoe_cmds[] = {
	{
	  NGM_PPPOE_COOKIE,
	  NGM_PPPOE_CONNECT,
	  "pppoe_connect",
	  &ngpppoe_init_data_state_type,
	  NULL
	},
	{
	  NGM_PPPOE_COOKIE,
	  NGM_PPPOE_LISTEN,
	  "pppoe_listen",
	  &ngpppoe_init_data_state_type,
	  NULL
	},
	{
	  NGM_PPPOE_COOKIE,
	  NGM_PPPOE_OFFER,
	  "pppoe_offer",
	  &ngpppoe_init_data_state_type,
	  NULL
	},
	{
	  NGM_PPPOE_COOKIE,
	  NGM_PPPOE_SERVICE,
	  "pppoe_service",
	  &ngpppoe_init_data_state_type,
	  NULL
	},
	{
	  NGM_PPPOE_COOKIE,
	  NGM_PPPOE_SUCCESS,
	  "pppoe_success",
	  &ng_pppoe_sts_state_type,
	  NULL
	},
	{
	  NGM_PPPOE_COOKIE,
	  NGM_PPPOE_FAIL,
	  "pppoe_fail",
	  &ng_pppoe_sts_state_type,
	  NULL
	},
	{
	  NGM_PPPOE_COOKIE,
	  NGM_PPPOE_CLOSE,
	  "pppoe_close",
	  &ng_pppoe_sts_state_type,
	  NULL
	},
	{
	  NGM_PPPOE_COOKIE,
	  NGM_PPPOE_SETMODE,
	  "pppoe_setmode",
	  &ng_parse_string_type,
	  NULL
	},
	{
	  NGM_PPPOE_COOKIE,
	  NGM_PPPOE_GETMODE,
	  "pppoe_getmode",
	  NULL,
	  &ng_parse_string_type
	},
	{
	  NGM_PPPOE_COOKIE,
	  NGM_PPPOE_SETENADDR,
	  "setenaddr",
	  &ng_parse_enaddr_type,
	  NULL
	},
	{
	  NGM_PPPOE_COOKIE,
	  NGM_PPPOE_SETMAXP,
	  "setmaxp",
	  &ng_parse_uint16_type,
	  NULL
	},
        {
	  NGM_PPPOE_COOKIE,
	  NGM_PPPOE_SEND_HURL,
	  "send_hurl",
	  &ngpppoe_init_data_state_type,
	  NULL
        },
        {
	  NGM_PPPOE_COOKIE,
	  NGM_PPPOE_SEND_MOTM,
	  "send_motm",
	  &ngpppoe_init_data_state_type,
	  NULL
        },
	{ 0 }
};

/* Netgraph node type descriptor */
static struct ng_type typestruct = {
	.version =	NG_ABI_VERSION,
	.name =		NG_PPPOE_NODE_TYPE,
	.constructor =	ng_pppoe_constructor,
	.rcvmsg =	ng_pppoe_rcvmsg,
	.shutdown =	ng_pppoe_shutdown,
	.newhook =	ng_pppoe_newhook,
	.connect =	ng_pppoe_connect,
	.rcvdata =	ng_pppoe_rcvdata,
	.disconnect =	ng_pppoe_disconnect,
	.cmdlist =	ng_pppoe_cmds,
};
NETGRAPH_INIT(pppoe, &typestruct);

/*
 * States for the session state machine.
 * These have no meaning if there is no hook attached yet.
 */
enum state {
    PPPOE_SNONE=0,	/* [both] Initial state */
    PPPOE_LISTENING,	/* [Daemon] Listening for discover initiation pkt */
    PPPOE_SINIT,	/* [Client] Sent discovery initiation */
    PPPOE_PRIMED,	/* [Server] Awaiting PADI from daemon */
    PPPOE_SOFFER,	/* [Server] Sent offer message  (got PADI)*/
    PPPOE_SREQ,		/* [Client] Sent a Request */
    PPPOE_NEWCONNECTED,	/* [Server] Connection established, No data received */
    PPPOE_CONNECTED,	/* [Both] Connection established, Data received */
    PPPOE_DEAD		/* [Both] */
};

#define NUMTAGS 20 /* number of tags we are set up to work with */

/*
 * Information we store for each hook on each node for negotiating the
 * session. The mbuf and cluster are freed once negotiation has completed.
 * The whole negotiation block is then discarded.
 */

struct sess_neg {
	struct mbuf 		*m; /* holds cluster with last sent packet */
	union	packet		*pkt; /* points within the above cluster */
	struct callout		handle;   /* see timeout(9) */
	u_int			timeout; /* 0,1,2,4,8,16 etc. seconds */
	u_int			numtags;
	const struct pppoe_tag	*tags[NUMTAGS];
	u_int			service_len;
	u_int			ac_name_len;
	u_int			host_uniq_len;

	struct datatag		service;
	struct datatag		ac_name;
	struct datatag		host_uniq;
};
typedef struct sess_neg *negp;

/*
 * Session information that is needed after connection.
 */
struct sess_con {
	hook_p  		hook;
	uint16_t		Session_ID;
	enum state		state;
	ng_ID_t			creator;	/* who to notify */
	struct pppoe_full_hdr	pkt_hdr;	/* used when connected */
	negp			neg;		/* used when negotiating */
	LIST_ENTRY(sess_con)	sessions;
};
typedef struct sess_con *sessp;

#define SESSHASHSIZE	0x0100
#define SESSHASH(x)	(((x) ^ ((x) >> 8)) & (SESSHASHSIZE - 1))

struct sess_hash_entry {
	struct mtx	mtx;
	LIST_HEAD(hhead, sess_con) head;
};

/*
 * Information we store for each node
 */
struct PPPoE {
	node_p		node;		/* back pointer to node */
	hook_p  	ethernet_hook;
	hook_p  	debug_hook;
	u_int   	packets_in;	/* packets in from ethernet */
	u_int   	packets_out;	/* packets out towards ethernet */
	uint32_t	flags;
#define	COMPAT_3COM	0x00000001
#define	COMPAT_DLINK	0x00000002
	struct ether_header	eh;
	LIST_HEAD(, sess_con) listeners;
	struct sess_hash_entry	sesshash[SESSHASHSIZE];
	struct maxptag	max_payload;	/* PPP-Max-Payload (RFC4638) */
};
typedef struct PPPoE *priv_p;

union uniq {
	char bytes[sizeof(void *)];
	void *pointer;
};

#define	LEAVE(x) do { error = x; goto quit; } while(0)
static void	pppoe_start(sessp sp);
static void	pppoe_ticker(node_p node, hook_p hook, void *arg1, int arg2);
static const	struct pppoe_tag *scan_tags(sessp sp,
			const struct pppoe_hdr* ph);
static	int	pppoe_send_event(sessp sp, enum cmd cmdid);

/*************************************************************************
 * Some basic utilities  from the Linux version with author's permission.*
 * Author:	Michal Ostrowski <mostrows@styx.uwaterloo.ca>		 *
 ************************************************************************/



/*
 * Return the location where the next tag can be put
 */
static __inline const struct pppoe_tag*
next_tag(const struct pppoe_hdr* ph)
{
	return (const struct pppoe_tag*)(((const char*)(ph + 1))
	    + ntohs(ph->length));
}

/*
 * Look for a tag of a specific type.
 * Don't trust any length the other end says,
 * but assume we already sanity checked ph->length.
 */
static const struct pppoe_tag*
get_tag(const struct pppoe_hdr* ph, uint16_t idx)
{
	const char *const end = (const char *)next_tag(ph);
	const struct pppoe_tag *pt = (const void *)(ph + 1);
	const char *ptn;

	/*
	 * Keep processing tags while a tag header will still fit.
	 */
	while((const char*)(pt + 1) <= end) {
		/*
		 * If the tag data would go past the end of the packet, abort.
		 */
		ptn = (((const char *)(pt + 1)) + ntohs(pt->tag_len));
		if (ptn > end) {
			CTR2(KTR_NET, "%20s: invalid length for tag %d",
			    __func__, idx);
			return (NULL);
		}
		if (pt->tag_type == idx) {
			CTR2(KTR_NET, "%20s: found tag %d", __func__, idx);
			return (pt);
		}

		pt = (const struct pppoe_tag*)ptn;
	}

	CTR2(KTR_NET, "%20s: not found tag %d", __func__, idx);
	return (NULL);
}

/**************************************************************************
 * Inlines to initialise or add tags to a session's tag list.
 **************************************************************************/
/*
 * Initialise the session's tag list.
 */
static void
init_tags(sessp sp)
{
	KASSERT(sp->neg != NULL, ("%s: no neg", __func__));
	sp->neg->numtags = 0;
}

static void
insert_tag(sessp sp, const struct pppoe_tag *tp)
{
	negp neg = sp->neg;
	int i;

	KASSERT(neg != NULL, ("%s: no neg", __func__));
	if ((i = neg->numtags++) < NUMTAGS) {
		neg->tags[i] = tp;
	} else {
		log(LOG_NOTICE, "ng_pppoe: asked to add too many tags to "
		    "packet\n");
		neg->numtags--;
	}
}

/*
 * Make up a packet, using the tags filled out for the session.
 *
 * Assume that the actual pppoe header and ethernet header
 * are filled out externally to this routine.
 * Also assume that neg->wh points to the correct
 * location at the front of the buffer space.
 */
static void
make_packet(sessp sp) {
	struct pppoe_full_hdr *wh = &sp->neg->pkt->pkt_header;
	const struct pppoe_tag **tag;
	char *dp;
	int count;
	int tlen;
	uint16_t length = 0;

	KASSERT((sp->neg != NULL) && (sp->neg->m != NULL),
	    ("%s: called from wrong state", __func__));
	CTR2(KTR_NET, "%20s: called %d", __func__, sp->Session_ID);

	dp = (char *)(&wh->ph + 1);
	for (count = 0, tag = sp->neg->tags;
	    ((count < sp->neg->numtags) && (count < NUMTAGS));
	    tag++, count++) {
		tlen = ntohs((*tag)->tag_len) + sizeof(**tag);
		if ((length + tlen) > (ETHER_MAX_LEN - 4 - sizeof(*wh))) {
			log(LOG_NOTICE, "ng_pppoe: tags too long\n");
			sp->neg->numtags = count;
			break;	/* XXX chop off what's too long */
		}
		bcopy(*tag, (char *)dp, tlen);
		length += tlen;
		dp += tlen;
	}
 	wh->ph.length = htons(length);
	sp->neg->m->m_len = length + sizeof(*wh);
	sp->neg->m->m_pkthdr.len = length + sizeof(*wh);
}

/**************************************************************************
 * Routines to match a service.						  *
 **************************************************************************/

/*
 * Find a hook that has a service string that matches that
 * we are seeking. For now use a simple string.
 * In the future we may need something like regexp().
 *
 * Null string is a wildcard (ANY service), according to RFC2516.
 * And historical FreeBSD wildcard is also "*".
 */

static hook_p
pppoe_match_svc(node_p node, const struct pppoe_tag *tag)
{
	const priv_p privp = NG_NODE_PRIVATE(node);
	sessp sp;

	LIST_FOREACH(sp, &privp->listeners, sessions) {
		negp neg = sp->neg;

		/* Empty Service-Name matches any service. */
		if (neg->service_len == 0)
			break;

		/* Special case for a blank or "*" service name (wildcard). */
		if (neg->service_len == 1 && neg->service.data[0] == '*')
			break;

		/* If the lengths don't match, that aint it. */
		if (neg->service_len != ntohs(tag->tag_len))
			continue;

		if (strncmp((const char *)(tag + 1), neg->service.data,
		    ntohs(tag->tag_len)) == 0)
			break;
	}
	CTR3(KTR_NET, "%20s: matched %p for %s", __func__,
	    sp?sp->hook:NULL, (const char *)(tag + 1));

	return (sp?sp->hook:NULL);
}

/*
 * Broadcast the PADI packet in m0 to all listening hooks.
 * This routine is called when a PADI with empty Service-Name
 * tag is received. Client should receive PADOs with all
 * available services.
 */
static int
pppoe_broadcast_padi(node_p node, struct mbuf *m0)
{
	const priv_p privp = NG_NODE_PRIVATE(node);
	sessp sp;
	int error = 0;

	LIST_FOREACH(sp, &privp->listeners, sessions) {
		struct mbuf *m;

		m = m_dup(m0, M_NOWAIT);
		if (m == NULL)
			return (ENOMEM);
		NG_SEND_DATA_ONLY(error, sp->hook, m);
		if (error)
			return (error);
	}

	return (0);
}

/*
 * Find a hook, which name equals to given service.
 */
static hook_p
pppoe_find_svc(node_p node, const char *svc_name, int svc_len)
{
	const priv_p privp = NG_NODE_PRIVATE(node);
	sessp sp;

	LIST_FOREACH(sp, &privp->listeners, sessions) {
		negp neg = sp->neg;

		if (neg->service_len == svc_len &&
		    strncmp(svc_name, neg->service.data, svc_len) == 0)
			return (sp->hook);
	}

	return (NULL);
}

/**************************************************************************
 * Routines to find a particular session that matches an incoming packet. *
 **************************************************************************/
/* Find free session and add to hash. */
static uint16_t
pppoe_getnewsession(sessp sp)
{
	const priv_p privp = NG_NODE_PRIVATE(NG_HOOK_NODE(sp->hook));
	static uint16_t pppoe_sid = 1;
	sessp	tsp;
	uint16_t val, hash;

restart:
	/* Atomicity is not needed here as value will be checked. */
	val = pppoe_sid++;
	/* Spec says 0xFFFF is reserved, also don't use 0x0000. */
	if (val == 0xffff || val == 0x0000)
		val = pppoe_sid = 1;

	/* Check it isn't already in use. */
	hash = SESSHASH(val);
	mtx_lock(&privp->sesshash[hash].mtx);
	LIST_FOREACH(tsp, &privp->sesshash[hash].head, sessions) {
		if (tsp->Session_ID == val)
			break;
	}
	if (!tsp) {
		sp->Session_ID = val;
		LIST_INSERT_HEAD(&privp->sesshash[hash].head, sp, sessions);
	}
	mtx_unlock(&privp->sesshash[hash].mtx);
	if (tsp)
		goto restart;

	CTR2(KTR_NET, "%20s: new sid %d", __func__, val);

	return (val);
}

/* Add specified session to hash. */
static void
pppoe_addsession(sessp sp)
{
	const priv_p	privp = NG_NODE_PRIVATE(NG_HOOK_NODE(sp->hook));
	uint16_t	hash = SESSHASH(sp->Session_ID);

	mtx_lock(&privp->sesshash[hash].mtx);
	LIST_INSERT_HEAD(&privp->sesshash[hash].head, sp, sessions);
	mtx_unlock(&privp->sesshash[hash].mtx);
}

/* Delete specified session from hash. */
static void
pppoe_delsession(sessp sp)
{
	const priv_p	privp = NG_NODE_PRIVATE(NG_HOOK_NODE(sp->hook));
	uint16_t	hash = SESSHASH(sp->Session_ID);

	mtx_lock(&privp->sesshash[hash].mtx);
	LIST_REMOVE(sp, sessions);
	mtx_unlock(&privp->sesshash[hash].mtx);
}

/* Find matching peer/session combination. */
static sessp
pppoe_findsession(priv_p privp, const struct pppoe_full_hdr *wh)
{
	uint16_t 	session = ntohs(wh->ph.sid);
	uint16_t	hash = SESSHASH(session);
	sessp		sp = NULL;

	mtx_lock(&privp->sesshash[hash].mtx);
	LIST_FOREACH(sp, &privp->sesshash[hash].head, sessions) {
		if (sp->Session_ID == session &&
		    bcmp(sp->pkt_hdr.eh.ether_dhost,
		     wh->eh.ether_shost, ETHER_ADDR_LEN) == 0) {
			break;
		}
	}
	mtx_unlock(&privp->sesshash[hash].mtx);
	CTR3(KTR_NET, "%20s: matched %p for %d", __func__, sp?sp->hook:NULL,
	    session);

	return (sp);
}

static hook_p
pppoe_finduniq(node_p node, const struct pppoe_tag *tag)
{
	hook_p	hook = NULL;
	sessp	sp;

	/* Cycle through all known hooks. */
	LIST_FOREACH(hook, &node->nd_hooks, hk_hooks) {
		/* Skip any nonsession hook. */
		if (NG_HOOK_PRIVATE(hook) == NULL)
			continue;
		sp = NG_HOOK_PRIVATE(hook);
		/* Skip already connected sessions. */
		if (sp->neg == NULL)
			continue;
		if (sp->neg->host_uniq_len == ntohs(tag->tag_len) &&
		    bcmp(sp->neg->host_uniq.data, (const char *)(tag + 1),
		     sp->neg->host_uniq_len) == 0)
			break;
	}
	CTR3(KTR_NET, "%20s: matched %p for %p", __func__, hook, sp);

	return (hook);
}

static hook_p
pppoe_findcookie(node_p node, const struct pppoe_tag *tag)
{
	hook_p	hook = NULL;
	union uniq cookie;

	bcopy(tag + 1, cookie.bytes, sizeof(void *));
	/* Cycle through all known hooks. */
	LIST_FOREACH(hook, &node->nd_hooks, hk_hooks) {
		/* Skip any nonsession hook. */
		if (NG_HOOK_PRIVATE(hook) == NULL)
			continue;
		if (cookie.pointer == NG_HOOK_PRIVATE(hook))
			break;
	}
	CTR3(KTR_NET, "%20s: matched %p for %p", __func__, hook, cookie.pointer);

	return (hook);
}

/**************************************************************************
 * Start of Netgraph entrypoints.					  *
 **************************************************************************/

/*
 * Allocate the private data structure and link it with node.
 */
static int
ng_pppoe_constructor(node_p node)
{
	priv_p	privp;
	int	i;

	/* Initialize private descriptor. */
	privp = malloc(sizeof(*privp), M_NETGRAPH_PPPOE, M_WAITOK | M_ZERO);

	/* Link structs together; this counts as our one reference to *node. */
	NG_NODE_SET_PRIVATE(node, privp);
	privp->node = node;

	/* Initialize to standard mode. */
	memset(&privp->eh.ether_dhost, 0xff, ETHER_ADDR_LEN);
	privp->eh.ether_type = ETHERTYPE_PPPOE_DISC;

	LIST_INIT(&privp->listeners);
	for (i = 0; i < SESSHASHSIZE; i++) {
	    mtx_init(&privp->sesshash[i].mtx, "PPPoE hash mutex", NULL, MTX_DEF);
	    LIST_INIT(&privp->sesshash[i].head);
	}

	CTR3(KTR_NET, "%20s: created node [%x] (%p)",
	    __func__, node->nd_ID, node);

	return (0);
}

/*
 * Give our ok for a hook to be added...
 * point the hook's private info to the hook structure.
 *
 * The following hook names are special:
 *  "ethernet":  the hook that should be connected to a NIC.
 *  "debug":	copies of data sent out here  (when I write the code).
 * All other hook names need only be unique. (the framework checks this).
 */
static int
ng_pppoe_newhook(node_p node, hook_p hook, const char *name)
{
	const priv_p privp = NG_NODE_PRIVATE(node);
	sessp sp;

	if (strcmp(name, NG_PPPOE_HOOK_ETHERNET) == 0) {
		privp->ethernet_hook = hook;
		NG_HOOK_SET_RCVDATA(hook, ng_pppoe_rcvdata_ether);
	} else if (strcmp(name, NG_PPPOE_HOOK_DEBUG) == 0) {
		privp->debug_hook = hook;
		NG_HOOK_SET_RCVDATA(hook, ng_pppoe_rcvdata_debug);
	} else {
		/*
		 * Any other unique name is OK.
		 * The infrastructure has already checked that it's unique,
		 * so just allocate it and hook it in.
		 */
		sp = malloc(sizeof(*sp), M_NETGRAPH_PPPOE, M_NOWAIT | M_ZERO);
		if (sp == NULL)
			return (ENOMEM);

		NG_HOOK_SET_PRIVATE(hook, sp);
		sp->hook = hook;
	}
	CTR5(KTR_NET, "%20s: node [%x] (%p) connected hook %s (%p)",
	    __func__, node->nd_ID, node, name, hook);

	return(0);
}

/*
 * Hook has been added successfully. Request the MAC address of
 * the underlying Ethernet node.
 */
static int
ng_pppoe_connect(hook_p hook)
{
	const priv_p privp = NG_NODE_PRIVATE(NG_HOOK_NODE(hook));
	struct ng_mesg *msg;
	int error;

	if (hook != privp->ethernet_hook)
		return (0);

	/*
	 * If this is Ethernet hook, then request MAC address
	 * from our downstream.
	 */
	NG_MKMESSAGE(msg, NGM_ETHER_COOKIE, NGM_ETHER_GET_ENADDR, 0, M_NOWAIT);
	if (msg == NULL)
		return (ENOBUFS);

	/*
	 * Our hook and peer hook have HK_INVALID flag set,
	 * so we can't use NG_SEND_MSG_HOOK() macro here.
	 */
	NG_SEND_MSG_ID(error, privp->node, msg,
	    NG_NODE_ID(NG_PEER_NODE(privp->ethernet_hook)),
	    NG_NODE_ID(privp->node));

	return (error);
}
/*
 * Get a netgraph control message.
 * Check it is one we understand. If needed, send a response.
 * We sometimes save the address for an async action later.
 * Always free the message.
 */
static int
ng_pppoe_rcvmsg(node_p node, item_p item, hook_p lasthook)
{
	priv_p privp = NG_NODE_PRIVATE(node);
	struct ngpppoe_init_data *ourmsg = NULL;
	struct ng_mesg *resp = NULL;
	int error = 0;
	hook_p hook = NULL;
	sessp sp = NULL;
	negp neg = NULL;
	struct ng_mesg *msg;

	NGI_GET_MSG(item, msg);
	CTR5(KTR_NET, "%20s: node [%x] (%p) got message %d with cookie %d",
	    __func__, node->nd_ID, node, msg->header.cmd,
	    msg->header.typecookie);

	/* Deal with message according to cookie and command. */
	switch (msg->header.typecookie) {
	case NGM_PPPOE_COOKIE:
		switch (msg->header.cmd) {
		case NGM_PPPOE_CONNECT:
		case NGM_PPPOE_LISTEN:
		case NGM_PPPOE_OFFER:
		case NGM_PPPOE_SERVICE:
		case NGM_PPPOE_SEND_HURL:
		case NGM_PPPOE_SEND_MOTM:
			ourmsg = (struct ngpppoe_init_data *)msg->data;
			if (msg->header.arglen < sizeof(*ourmsg)) {
				log(LOG_ERR, "ng_pppoe[%x]: init data too "
				    "small\n", node->nd_ID);
				LEAVE(EMSGSIZE);
			}
			if (msg->header.cmd == NGM_PPPOE_SEND_HURL ||
			    msg->header.cmd == NGM_PPPOE_SEND_MOTM) {
				if (msg->header.arglen - sizeof(*ourmsg) >
				    PPPOE_PADM_VALUE_SIZE) {
					log(LOG_ERR, "ng_pppoe[%x]: message "
					    "too big\n", node->nd_ID);
					LEAVE(EMSGSIZE);
				}
			} else {
				if (msg->header.arglen - sizeof(*ourmsg) >
				    PPPOE_SERVICE_NAME_SIZE) {
					log(LOG_ERR, "ng_pppoe[%x]: service name "
					    "too big\n", node->nd_ID);
					LEAVE(EMSGSIZE);
				}
			}
			if (msg->header.arglen - sizeof(*ourmsg) <
			    ourmsg->data_len) {
				log(LOG_ERR, "ng_pppoe[%x]: init data has bad "
				    "length, %d should be %zd\n", node->nd_ID,
				    ourmsg->data_len,
				    msg->header.arglen - sizeof (*ourmsg));
				LEAVE(EMSGSIZE);
			}

			/* Make sure strcmp will terminate safely. */
			ourmsg->hook[sizeof(ourmsg->hook) - 1] = '\0';

			/* Find hook by name. */
			hook = ng_findhook(node, ourmsg->hook);
			if (hook == NULL)
				LEAVE(ENOENT);

			sp = NG_HOOK_PRIVATE(hook);
			if (sp == NULL)
				LEAVE(EINVAL);

			if (msg->header.cmd == NGM_PPPOE_LISTEN) {
				/*
				 * Ensure we aren't already listening for this
				 * service.
				 */
				if (pppoe_find_svc(node, ourmsg->data,
				    ourmsg->data_len) != NULL)
					LEAVE(EEXIST);
			}

			/*
			 * PPPOE_SERVICE advertisements are set up
			 * on sessions that are in PRIMED state.
			 */
			if (msg->header.cmd == NGM_PPPOE_SERVICE)
				break;

			/*
			 * PADM messages are set up on active sessions.
			 */
			if (msg->header.cmd == NGM_PPPOE_SEND_HURL ||
			    msg->header.cmd == NGM_PPPOE_SEND_MOTM) {
				if (sp->state != PPPOE_NEWCONNECTED &&
				    sp->state != PPPOE_CONNECTED) {
					log(LOG_NOTICE, "ng_pppoe[%x]: session is not "
					    "active\n", node->nd_ID);
					LEAVE(EISCONN);
				}
				break;
			}

			if (sp->state != PPPOE_SNONE) {
				log(LOG_NOTICE, "ng_pppoe[%x]: Session already "
				    "active\n", node->nd_ID);
				LEAVE(EISCONN);
			}

			/*
			 * Set up prototype header.
			 */
			neg = malloc(sizeof(*neg), M_NETGRAPH_PPPOE,
			    M_NOWAIT | M_ZERO);

			if (neg == NULL)
				LEAVE(ENOMEM);

			neg->m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
			if (neg->m == NULL) {
				free(neg, M_NETGRAPH_PPPOE);
				LEAVE(ENOBUFS);
			}
			neg->m->m_pkthdr.rcvif = NULL;
			sp->neg = neg;
			ng_callout_init(&neg->handle);
			neg->m->m_len = sizeof(struct pppoe_full_hdr);
			neg->pkt = mtod(neg->m, union packet*);
			memcpy((void *)&neg->pkt->pkt_header.eh,
			    &privp->eh, sizeof(struct ether_header));
			neg->pkt->pkt_header.ph.ver = 0x1;
			neg->pkt->pkt_header.ph.type = 0x1;
			neg->pkt->pkt_header.ph.sid = 0x0000;
			neg->timeout = 0;

			sp->creator = NGI_RETADDR(item);
		}
		switch (msg->header.cmd) {
		case NGM_PPPOE_GET_STATUS:
		    {
			struct ngpppoestat *stats;

			NG_MKRESPONSE(resp, msg, sizeof(*stats), M_NOWAIT);
			if (!resp)
				LEAVE(ENOMEM);

			stats = (struct ngpppoestat *) resp->data;
			stats->packets_in = privp->packets_in;
			stats->packets_out = privp->packets_out;
			break;
		    }
		case NGM_PPPOE_CONNECT:
		    {
			/*
			 * Check the hook exists and is Uninitialised.
			 * Send a PADI request, and start the timeout logic.
			 * Store the originator of this message so we can send
			 * a success or fail message to them later.
			 * Move the session to SINIT.
			 * Set up the session to the correct state and
			 * start it.
			 */
			int	acnpos, acnlen = 0, acnsep = 0;
			int	hupos, hulen = 0, husep = 0;
			int	i, srvpos, srvlen;
			acnpos = 0;
			for (i = 0; i < ourmsg->data_len; i++) {
				if (ourmsg->data[i] == '\\') {
					acnlen = i;
					acnsep = 1;
					break;
				}
			}
			hupos = acnlen + acnsep;
			for (i = hupos; i < ourmsg->data_len; i++) {
				if (ourmsg->data[i] == '|') {
					hulen = i - hupos;
					husep = 1;
					break;
				}
			}
			srvpos = hupos + hulen + husep;
			srvlen = ourmsg->data_len - srvpos;

			bcopy(ourmsg->data + acnpos, neg->ac_name.data, acnlen);
			neg->ac_name_len = acnlen;

			neg->host_uniq.hdr.tag_type = PTT_HOST_UNIQ;
			if (hulen == 0) {
				/* Not provided, generate one */
				neg->host_uniq.hdr.tag_len = htons(sizeof(sp));
				bcopy(&sp, neg->host_uniq.data, sizeof(sp));
				neg->host_uniq_len = sizeof(sp);
			} else if (hulen > 2 && ourmsg->data[hupos] == '0' &&
			  ourmsg->data[hupos + 1] == 'x' && hulen % 2 == 0) {
				/* Hex encoded */
				static const char hexdig[16] = "0123456789abcdef";
				int j;

				neg->host_uniq.hdr.tag_len = htons((uint16_t)(hulen / 2 - 1));
				for (i = 0; i < hulen - 2; i++) {
					for (j = 0;
					     j < 16 &&
					     ourmsg->data[hupos + 2 + i] != hexdig[j];
					     j++);
					if (j == 16)
						LEAVE(EINVAL);
					if (i % 2 == 0)
						neg->host_uniq.data[i / 2] = j << 4;
					else
						neg->host_uniq.data[i / 2] |= j;
				}
				neg->host_uniq_len = hulen / 2 - 1;
			} else {
				/* Plain string */
				neg->host_uniq.hdr.tag_len = htons((uint16_t)hulen);
				bcopy(ourmsg->data + hupos, neg->host_uniq.data, hulen);
				neg->host_uniq_len = hulen;
			}

			neg->service.hdr.tag_type = PTT_SRV_NAME;
			neg->service.hdr.tag_len = htons((uint16_t)srvlen);
			bcopy(ourmsg->data + srvpos, neg->service.data, srvlen);
			neg->service_len = srvlen;
			pppoe_start(sp);
			break;
		    }
		case NGM_PPPOE_LISTEN:
			/*
			 * Check the hook exists and is Uninitialised.
			 * Install the service matching string.
			 * Store the originator of this message so we can send
			 * a success or fail message to them later.
			 * Move the hook to 'LISTENING'
			 */
			neg->service.hdr.tag_type = PTT_SRV_NAME;
			neg->service.hdr.tag_len =
			    htons((uint16_t)ourmsg->data_len);

			if (ourmsg->data_len)
				bcopy(ourmsg->data, neg->service.data,
				    ourmsg->data_len);
			neg->service_len = ourmsg->data_len;
			neg->pkt->pkt_header.ph.code = PADT_CODE;
			/*
			 * Wait for PADI packet coming from Ethernet.
			 */
			sp->state = PPPOE_LISTENING;
			LIST_INSERT_HEAD(&privp->listeners, sp, sessions);
			break;
		case NGM_PPPOE_OFFER:
			/*
			 * Check the hook exists and is Uninitialised.
			 * Store the originator of this message so we can send
			 * a success of fail message to them later.
			 * Store the AC-Name given and go to PRIMED.
			 */
			neg->ac_name.hdr.tag_type = PTT_AC_NAME;
			neg->ac_name.hdr.tag_len =
			    htons((uint16_t)ourmsg->data_len);
			if (ourmsg->data_len)
				bcopy(ourmsg->data, neg->ac_name.data,
				    ourmsg->data_len);
			neg->ac_name_len = ourmsg->data_len;
			neg->pkt->pkt_header.ph.code = PADO_CODE;
			/*
			 * Wait for PADI packet coming from hook.
			 */
			sp->state = PPPOE_PRIMED;
			break;
		case NGM_PPPOE_SERVICE:
			/*
			 * Check the session is primed.
			 * for now just allow ONE service to be advertised.
			 * If you do it twice you just overwrite.
			 */
			if (sp->state != PPPOE_PRIMED) {
				log(LOG_NOTICE, "ng_pppoe[%x]: session not "
				    "primed\n", node->nd_ID);
				LEAVE(EISCONN);
			}
			neg = sp->neg;
			neg->service.hdr.tag_type = PTT_SRV_NAME;
			neg->service.hdr.tag_len =
			    htons((uint16_t)ourmsg->data_len);

			if (ourmsg->data_len)
				bcopy(ourmsg->data, neg->service.data,
				    ourmsg->data_len);
			neg->service_len = ourmsg->data_len;
			break;
		case NGM_PPPOE_SETMODE:
		    {
			char *s;
			size_t len;

			if (msg->header.arglen == 0)
				LEAVE(EINVAL);

			s = (char *)msg->data;
			len = msg->header.arglen - 1;

			/* Search for matching mode string. */
			if (len == strlen(NG_PPPOE_STANDARD) &&
			    (strncmp(NG_PPPOE_STANDARD, s, len) == 0)) {
				privp->flags = 0;
				privp->eh.ether_type = ETHERTYPE_PPPOE_DISC;
				break;
			}
			if (len == strlen(NG_PPPOE_3COM) &&
			    (strncmp(NG_PPPOE_3COM, s, len) == 0)) {
				privp->flags |= COMPAT_3COM;
				privp->eh.ether_type =
				    ETHERTYPE_PPPOE_3COM_DISC;
				break;
			}
			if (len == strlen(NG_PPPOE_DLINK) &&
			    (strncmp(NG_PPPOE_DLINK, s, len) == 0)) {
				privp->flags |= COMPAT_DLINK;
				break;
			}
			error = EINVAL;
			break;
		    }
		case NGM_PPPOE_GETMODE:
		    {
			char *s;
			size_t len = 0;

			if (privp->flags == 0)
				len += strlen(NG_PPPOE_STANDARD) + 1;
			if (privp->flags & COMPAT_3COM)
				len += strlen(NG_PPPOE_3COM) + 1;
			if (privp->flags & COMPAT_DLINK)
				len += strlen(NG_PPPOE_DLINK) + 1;

			NG_MKRESPONSE(resp, msg, len, M_NOWAIT);
			if (resp == NULL)
				LEAVE(ENOMEM);

			s = (char *)resp->data;
			if (privp->flags == 0) {
				len = strlen(NG_PPPOE_STANDARD);
				strlcpy(s, NG_PPPOE_STANDARD, len + 1);
				break;
			}
			if (privp->flags & COMPAT_3COM) {
				len = strlen(NG_PPPOE_3COM);
				strlcpy(s, NG_PPPOE_3COM, len + 1);
				s += len;
			}
			if (privp->flags & COMPAT_DLINK) {
				if (s != resp->data)
					*s++ = '|';
				len = strlen(NG_PPPOE_DLINK);
				strlcpy(s, NG_PPPOE_DLINK, len + 1);
			}
			break;
		    }
		case NGM_PPPOE_SETENADDR:
			if (msg->header.arglen != ETHER_ADDR_LEN)
				LEAVE(EINVAL);
			bcopy(msg->data, &privp->eh.ether_shost,
			    ETHER_ADDR_LEN);
			break;
		case NGM_PPPOE_SETMAXP:
			if (msg->header.arglen != sizeof(uint16_t))
				LEAVE(EINVAL);
			privp->max_payload.hdr.tag_type = PTT_MAX_PAYL;
			privp->max_payload.hdr.tag_len = htons(sizeof(uint16_t));
			privp->max_payload.data = htons(*((uint16_t *)msg->data));
			break;
		case NGM_PPPOE_SEND_HURL:
		    {
			struct mbuf *m;

			/* Generate a packet of that type. */
			m = m_gethdr(M_NOWAIT, MT_DATA);
			if (m == NULL)
				log(LOG_NOTICE, "ng_pppoe[%x]: session out of "
				    "mbufs\n", node->nd_ID);
			else {
				struct pppoe_full_hdr *wh;
				struct pppoe_tag *tag;
				int     error = 0;

				wh = mtod(m, struct pppoe_full_hdr *);
				bcopy(&sp->pkt_hdr, wh, sizeof(*wh));

				/* Revert the stored header to DISC/PADM mode. */
				wh->ph.code = PADM_CODE;
				/*
				 * Configure ethertype depending on what
				 * was used during sessions stage.
				 */
				if (wh->eh.ether_type ==
				    ETHERTYPE_PPPOE_3COM_SESS)
					wh->eh.ether_type = ETHERTYPE_PPPOE_3COM_DISC;
				else
					wh->eh.ether_type = ETHERTYPE_PPPOE_DISC;
				/*
				 * Add PADM message and adjust sizes.
				 */
				tag = (void *)(&wh->ph + 1);
				tag->tag_type = PTT_HURL;
				tag->tag_len = htons(ourmsg->data_len);
				strncpy((char *)(tag + 1), ourmsg->data, ourmsg->data_len);
				m->m_pkthdr.len = m->m_len = sizeof(*wh) + sizeof(*tag) +
				    ourmsg->data_len;
				wh->ph.length = htons(sizeof(*tag) + ourmsg->data_len);
				NG_SEND_DATA_ONLY(error,
				    privp->ethernet_hook, m);
			}
			break;
		    }
		case NGM_PPPOE_SEND_MOTM:
		    {
			struct mbuf *m;

			/* Generate a packet of that type. */
			m = m_gethdr(M_NOWAIT, MT_DATA);
			if (m == NULL)
				log(LOG_NOTICE, "ng_pppoe[%x]: session out of "
				    "mbufs\n", node->nd_ID);
			else {
				struct pppoe_full_hdr *wh;
				struct pppoe_tag *tag;
				int     error = 0;

				wh = mtod(m, struct pppoe_full_hdr *);
				bcopy(&sp->pkt_hdr, wh, sizeof(*wh));

				/* Revert the stored header to DISC/PADM mode. */
				wh->ph.code = PADM_CODE;
				/*
				 * Configure ethertype depending on what
				 * was used during sessions stage.
				 */
				if (wh->eh.ether_type ==
				    ETHERTYPE_PPPOE_3COM_SESS)
					wh->eh.ether_type = ETHERTYPE_PPPOE_3COM_DISC;
				else
					wh->eh.ether_type = ETHERTYPE_PPPOE_DISC;
				/*
				 * Add PADM message and adjust sizes.
				 */
				tag = (void *)(&wh->ph + 1);
				tag->tag_type = PTT_MOTM;
				tag->tag_len = htons(ourmsg->data_len);
				strncpy((char *)(tag + 1), ourmsg->data, ourmsg->data_len);
				m->m_pkthdr.len = m->m_len = sizeof(*wh) + sizeof(*tag) +
				    ourmsg->data_len;
				wh->ph.length = htons(sizeof(*tag) + ourmsg->data_len);
				NG_SEND_DATA_ONLY(error,
				    privp->ethernet_hook, m);
			}
			break;
		    }
		default:
			LEAVE(EINVAL);
		}
		break;
	case NGM_ETHER_COOKIE:
		if (!(msg->header.flags & NGF_RESP))
			LEAVE(EINVAL);
		switch (msg->header.cmd) {
		case NGM_ETHER_GET_ENADDR:
			if (msg->header.arglen != ETHER_ADDR_LEN)
				LEAVE(EINVAL);
			bcopy(msg->data, &privp->eh.ether_shost,
			    ETHER_ADDR_LEN);
			break;
		default:
			LEAVE(EINVAL);
		}
		break;
	default:
		LEAVE(EINVAL);
	}

	/* Take care of synchronous response, if any. */
quit:
	CTR2(KTR_NET, "%20s: returning %d", __func__, error);
	NG_RESPOND_MSG(error, node, item, resp);
	/* Free the message and return. */
	NG_FREE_MSG(msg);
	return(error);
}

/*
 * Start a client into the first state. A separate function because
 * it can be needed if the negotiation times out.
 */
static void
pppoe_start(sessp sp)
{
	hook_p	hook = sp->hook;
	node_p	node = NG_HOOK_NODE(hook);
	priv_p	privp = NG_NODE_PRIVATE(node);
	negp	neg = sp->neg;
	struct  mbuf *m0;
	int	error;

	/*
	 * Kick the state machine into starting up.
	 */
	CTR2(KTR_NET, "%20s: called %d", __func__, sp->Session_ID);
	sp->state = PPPOE_SINIT;
	/*
	 * Reset the packet header to broadcast. Since we are
	 * in a client mode use configured ethertype.
	 */
	memcpy((void *)&neg->pkt->pkt_header.eh, &privp->eh,
	    sizeof(struct ether_header));
	neg->pkt->pkt_header.ph.code = PADI_CODE;
	init_tags(sp);
	insert_tag(sp, &neg->host_uniq.hdr);
	insert_tag(sp, &neg->service.hdr);
	if (privp->max_payload.data != 0)
		insert_tag(sp, &privp->max_payload.hdr);
	make_packet(sp);
	/*
	 * Send packet and prepare to retransmit it after timeout.
	 */
	ng_callout(&neg->handle, node, hook, PPPOE_INITIAL_TIMEOUT * hz,
	    pppoe_ticker, NULL, 0);
	neg->timeout = PPPOE_INITIAL_TIMEOUT * 2;
	m0 = m_copypacket(neg->m, M_NOWAIT);
	NG_SEND_DATA_ONLY(error, privp->ethernet_hook, m0);
}

static int
send_acname(sessp sp, const struct pppoe_tag *tag)
{
	int error, tlen;
	struct ng_mesg *msg;
	struct ngpppoe_sts *sts;

	CTR2(KTR_NET, "%20s: called %d", __func__, sp->Session_ID);

	NG_MKMESSAGE(msg, NGM_PPPOE_COOKIE, NGM_PPPOE_ACNAME,
	    sizeof(struct ngpppoe_sts), M_NOWAIT);
	if (msg == NULL)
		return (ENOMEM);

	sts = (struct ngpppoe_sts *)msg->data;
	tlen = min(NG_HOOKSIZ - 1, ntohs(tag->tag_len));
	strncpy(sts->hook, (const char *)(tag + 1), tlen);
	sts->hook[tlen] = '\0';
	NG_SEND_MSG_ID(error, NG_HOOK_NODE(sp->hook), msg, sp->creator, 0);

	return (error);
}

static int
send_sessionid(sessp sp)
{
	int error;
	struct ng_mesg *msg;

	CTR2(KTR_NET, "%20s: called %d", __func__, sp->Session_ID);

	NG_MKMESSAGE(msg, NGM_PPPOE_COOKIE, NGM_PPPOE_SESSIONID,
	    sizeof(uint16_t), M_NOWAIT);
	if (msg == NULL)
		return (ENOMEM);

	*(uint16_t *)msg->data = sp->Session_ID;
	NG_SEND_MSG_ID(error, NG_HOOK_NODE(sp->hook), msg, sp->creator, 0);

	return (error);
}

static int
send_maxp(sessp sp, const struct pppoe_tag *tag)
{
	int error;
	struct ng_mesg *msg;
	struct ngpppoe_maxp *maxp;

	CTR2(KTR_NET, "%20s: called %d", __func__, sp->Session_ID);

	NG_MKMESSAGE(msg, NGM_PPPOE_COOKIE, NGM_PPPOE_SETMAXP,
	    sizeof(struct ngpppoe_maxp), M_NOWAIT);
	if (msg == NULL)
		return (ENOMEM);

	maxp = (struct ngpppoe_maxp *)msg->data;
	strncpy(maxp->hook, NG_HOOK_NAME(sp->hook), NG_HOOKSIZ);
	maxp->data = ntohs(((const struct maxptag *)tag)->data);
	NG_SEND_MSG_ID(error, NG_HOOK_NODE(sp->hook), msg, sp->creator, 0);

	return (error);
}

static int
send_hurl(sessp sp, const struct pppoe_tag *tag)
{
	int error, tlen;
	struct ng_mesg *msg;
	struct ngpppoe_padm *padm;

	CTR2(KTR_NET, "%20s: called %d", __func__, sp->Session_ID);

	NG_MKMESSAGE(msg, NGM_PPPOE_COOKIE, NGM_PPPOE_HURL,
	    sizeof(struct ngpppoe_padm), M_NOWAIT);
	if (msg == NULL)
		return (ENOMEM);

	padm = (struct ngpppoe_padm *)msg->data;
	tlen = min(PPPOE_PADM_VALUE_SIZE - 1, ntohs(tag->tag_len));
	strncpy(padm->msg, (const char *)(tag + 1), tlen);
	padm->msg[tlen] = '\0';
	NG_SEND_MSG_ID(error, NG_HOOK_NODE(sp->hook), msg, sp->creator, 0);

	return (error);
}

static int
send_motm(sessp sp, const struct pppoe_tag *tag)
{
	int error, tlen;
	struct ng_mesg *msg;
	struct ngpppoe_padm *padm;

	CTR2(KTR_NET, "%20s: called %d", __func__, sp->Session_ID);

	NG_MKMESSAGE(msg, NGM_PPPOE_COOKIE, NGM_PPPOE_MOTM,
	    sizeof(struct ngpppoe_padm), M_NOWAIT);
	if (msg == NULL)
		return (ENOMEM);

	padm = (struct ngpppoe_padm *)msg->data;
	tlen = min(PPPOE_PADM_VALUE_SIZE - 1, ntohs(tag->tag_len));
	strncpy(padm->msg, (const char *)(tag + 1), tlen);
	padm->msg[tlen] = '\0';
	NG_SEND_MSG_ID(error, NG_HOOK_NODE(sp->hook), msg, sp->creator, 0);

	return (error);
}

/*
 * Receive data from session hook and do something with it.
 */
static int
ng_pppoe_rcvdata(hook_p hook, item_p item)
{
	node_p			node = NG_HOOK_NODE(hook);
	const priv_p		privp = NG_NODE_PRIVATE(node);
	sessp			sp = NG_HOOK_PRIVATE(hook);
	struct pppoe_full_hdr	*wh;
	struct mbuf		*m;
	int			error;

	CTR6(KTR_NET, "%20s: node [%x] (%p) received %p on \"%s\" (%p)",
	    __func__, node->nd_ID, node, item, hook->hk_name, hook);

	NGI_GET_M(item, m);
	switch (sp->state) {
	case	PPPOE_NEWCONNECTED:
	case	PPPOE_CONNECTED: {
		/*
		 * Remove PPP address and control fields, if any.
		 * For example, ng_ppp(4) always sends LCP packets
		 * with address and control fields as required by
		 * generic PPP. PPPoE is an exception to the rule.
		 */
		if (m->m_pkthdr.len >= 2) {
			if (m->m_len < 2 && !(m = m_pullup(m, 2)))
				LEAVE(ENOBUFS);
			if (mtod(m, u_char *)[0] == 0xff &&
			    mtod(m, u_char *)[1] == 0x03)
				m_adj(m, 2);
		}
		/*
		 * Bang in a pre-made header, and set the length up
		 * to be correct. Then send it to the ethernet driver.
		 */
		M_PREPEND(m, sizeof(*wh), M_NOWAIT);
		if (m == NULL)
			LEAVE(ENOBUFS);

		wh = mtod(m, struct pppoe_full_hdr *);
		bcopy(&sp->pkt_hdr, wh, sizeof(*wh));
		wh->ph.length = htons(m->m_pkthdr.len - sizeof(*wh));
		NG_FWD_NEW_DATA(error, item, privp->ethernet_hook, m);
		privp->packets_out++;
		break;
		}
	case	PPPOE_PRIMED: {
		struct {
			struct pppoe_tag hdr;
			union	uniq	data;
		} __packed 	uniqtag;
		const struct pppoe_tag	*tag;
		struct mbuf 	*m0;
		const struct pppoe_hdr	*ph;
		negp		neg = sp->neg;
	        uint16_t	session;
		uint16_t	length;
		uint8_t		code;

		/*
		 * A PADI packet is being returned by the application
		 * that has set up this hook. This indicates that it
		 * wants us to offer service.
		 */
		if (m->m_len < sizeof(*wh)) {
			m = m_pullup(m, sizeof(*wh));
			if (m == NULL)
				LEAVE(ENOBUFS);
		}
		wh = mtod(m, struct pppoe_full_hdr *);
		ph = &wh->ph;
		session = ntohs(wh->ph.sid);
		length = ntohs(wh->ph.length);
		code = wh->ph.code;
		/* Use peers mode in session. */
		neg->pkt->pkt_header.eh.ether_type = wh->eh.ether_type;
		if (code != PADI_CODE)
			LEAVE(EINVAL);
		ng_uncallout(&neg->handle, node);

		/*
		 * This is the first time we hear
		 * from the client, so note it's
		 * unicast address, replacing the
		 * broadcast address.
		 */
		bcopy(wh->eh.ether_shost,
			neg->pkt->pkt_header.eh.ether_dhost,
			ETHER_ADDR_LEN);
		sp->state = PPPOE_SOFFER;
		neg->timeout = 0;
		neg->pkt->pkt_header.ph.code = PADO_CODE;

		/*
		 * Start working out the tags to respond with.
		 */
		uniqtag.hdr.tag_type = PTT_AC_COOKIE;
		uniqtag.hdr.tag_len = htons((u_int16_t)sizeof(sp));
		uniqtag.data.pointer = sp;
		init_tags(sp);
		insert_tag(sp, &neg->ac_name.hdr); /* AC_NAME */
		if ((tag = get_tag(ph, PTT_SRV_NAME)))
			insert_tag(sp, tag);	  /* return service */
		/*
		 * If we have a NULL service request
		 * and have an extra service defined in this hook,
		 * then also add a tag for the extra service.
		 * XXX this is a hack. eventually we should be able
		 * to support advertising many services, not just one
		 */
		if (((tag == NULL) || (tag->tag_len == 0)) &&
		    (neg->service.hdr.tag_len != 0)) {
			insert_tag(sp, &neg->service.hdr); /* SERVICE */
		}
		if ((tag = get_tag(ph, PTT_HOST_UNIQ)))
			insert_tag(sp, tag); /* returned hostunique */
		insert_tag(sp, &uniqtag.hdr);
		scan_tags(sp, ph);
		make_packet(sp);
		/*
		 * Send the offer but if they don't respond
		 * in PPPOE_OFFER_TIMEOUT seconds, forget about it.
		 */
		ng_callout(&neg->handle, node, hook, PPPOE_OFFER_TIMEOUT * hz,
		    pppoe_ticker, NULL, 0);
		m0 = m_copypacket(sp->neg->m, M_NOWAIT);
		NG_FWD_NEW_DATA(error, item, privp->ethernet_hook, m0);
		privp->packets_out++;
		break;
		}

	/*
	 * Packets coming from the hook make no sense
	 * to sessions in the rest of states. Throw them away.
	 */
	default:
		LEAVE(ENETUNREACH);
	}
quit:
	if (item)
		NG_FREE_ITEM(item);
	NG_FREE_M(m);
	return (error);
}

/*
 * Receive data from ether and do something with it.
 */
static int
ng_pppoe_rcvdata_ether(hook_p hook, item_p item)
{
	node_p			node = NG_HOOK_NODE(hook);
	const priv_p		privp = NG_NODE_PRIVATE(node);
	sessp			sp;
	const struct pppoe_tag	*utag = NULL, *tag = NULL;
	const struct pppoe_tag	sntag = { PTT_SRV_NAME, 0 };
	const struct pppoe_full_hdr *wh;
	const struct pppoe_hdr	*ph;
	negp			neg = NULL;
	struct mbuf		*m;
	hook_p 			sendhook;
	int			error = 0;
	uint16_t		session;
	uint16_t		length;
	uint8_t			code;
	struct	mbuf 		*m0;

	CTR6(KTR_NET, "%20s: node [%x] (%p) received %p on \"%s\" (%p)",
	    __func__, node->nd_ID, node, item, hook->hk_name, hook);

	NGI_GET_M(item, m);
	/*
	 * Dig out various fields from the packet.
	 * Use them to decide where to send it.
	 */
	privp->packets_in++;
	if( m->m_len < sizeof(*wh)) {
		m = m_pullup(m, sizeof(*wh)); /* Checks length */
		if (m == NULL) {
			log(LOG_NOTICE, "ng_pppoe[%x]: couldn't "
			    "m_pullup(wh)\n", node->nd_ID);
			LEAVE(ENOBUFS);
		}
	}
	wh = mtod(m, struct pppoe_full_hdr *);
	length = ntohs(wh->ph.length);
	switch(wh->eh.ether_type) {
	case	ETHERTYPE_PPPOE_3COM_DISC: /* fall through */
	case	ETHERTYPE_PPPOE_DISC:
		/*
		 * We need to try to make sure that the tag area
		 * is contiguous, or we could wander off the end
		 * of a buffer and make a mess.
		 * (Linux wouldn't have this problem).
		 */
		if (m->m_pkthdr.len <= MHLEN) {
			if( m->m_len < m->m_pkthdr.len) {
				m = m_pullup(m, m->m_pkthdr.len);
				if (m == NULL) {
					log(LOG_NOTICE, "ng_pppoe[%x]: "
					    "couldn't m_pullup(pkthdr)\n",
					    node->nd_ID);
					LEAVE(ENOBUFS);
				}
			}
		}
		if (m->m_len != m->m_pkthdr.len) {
			/*
			 * It's not all in one piece.
			 * We need to do extra work.
			 * Put it into a cluster.
			 */
			struct mbuf *n;
			n = m_dup(m, M_NOWAIT);
			m_freem(m);
			m = n;
			if (m) {
				/* just check we got a cluster */
				if (m->m_len != m->m_pkthdr.len) {
					m_freem(m);
					m = NULL;
				}
			}
			if (m == NULL) {
				log(LOG_NOTICE, "ng_pppoe[%x]: packet "
				    "fragmented\n", node->nd_ID);
				LEAVE(EMSGSIZE);
			}
		}
		wh = mtod(m, struct pppoe_full_hdr *);
		length = ntohs(wh->ph.length);
		ph = &wh->ph;
		session = ntohs(wh->ph.sid);
		code = wh->ph.code;

		switch(code) {
		case	PADI_CODE:
			/*
			 * We are a server:
			 * Look for a hook with the required service and send
			 * the ENTIRE packet up there. It should come back to
			 * a new hook in PRIMED state. Look there for further
			 * processing.
			 */
			tag = get_tag(ph, PTT_SRV_NAME);
			if (tag == NULL)
				tag = &sntag;

			/*
			 * First, try to match Service-Name against our 
			 * listening hooks. If no success and we are in D-Link
			 * compat mode and Service-Name is empty, then we 
			 * broadcast the PADI to all listening hooks.
			 */
			sendhook = pppoe_match_svc(node, tag);
			if (sendhook != NULL)
				NG_FWD_NEW_DATA(error, item, sendhook, m);
			else if (privp->flags & COMPAT_DLINK &&
				 ntohs(tag->tag_len) == 0)
				error = pppoe_broadcast_padi(node, m);
			else
				error = ENETUNREACH;
			break;
		case	PADO_CODE:
			/*
			 * We are a client:
			 * Use the host_uniq tag to find the hook this is in
			 * response to. Received #2, now send #3
			 * For now simply accept the first we receive.
			 */
			utag = get_tag(ph, PTT_HOST_UNIQ);
			if (utag == NULL) {
				log(LOG_NOTICE, "ng_pppoe[%x]: no host "
				    "unique field\n", node->nd_ID);
				LEAVE(ENETUNREACH);
			}

			sendhook = pppoe_finduniq(node, utag);
			if (sendhook == NULL) {
				log(LOG_NOTICE, "ng_pppoe[%x]: no "
				    "matching session\n", node->nd_ID);
				LEAVE(ENETUNREACH);
			}

			/*
			 * Check the session is in the right state.
			 * It needs to be in PPPOE_SINIT.
			 */
			sp = NG_HOOK_PRIVATE(sendhook);
			if (sp->state == PPPOE_SREQ ||
			    sp->state == PPPOE_CONNECTED) {
				break;	/* Multiple PADO is OK. */
			}
			if (sp->state != PPPOE_SINIT) {
				log(LOG_NOTICE, "ng_pppoe[%x]: session "
				    "in wrong state\n", node->nd_ID);
				LEAVE(ENETUNREACH);
			}
			neg = sp->neg;
			/* If requested specific AC-name, check it. */
			if (neg->ac_name_len) {
				tag = get_tag(ph, PTT_AC_NAME);
				if (!tag) {
					/* No PTT_AC_NAME in PADO */
					break;
				}
				if (neg->ac_name_len != htons(tag->tag_len) ||
				    strncmp(neg->ac_name.data,
				    (const char *)(tag + 1),
				    neg->ac_name_len) != 0) {
					break;
				}
			}
			sp->state = PPPOE_SREQ;
			ng_uncallout(&neg->handle, node);

			/*
			 * This is the first time we hear
			 * from the server, so note it's
			 * unicast address, replacing the
			 * broadcast address .
			 */
			bcopy(wh->eh.ether_shost,
				neg->pkt->pkt_header.eh.ether_dhost,
				ETHER_ADDR_LEN);
			neg->timeout = 0;
			neg->pkt->pkt_header.ph.code = PADR_CODE;
			init_tags(sp);
			insert_tag(sp, utag);      	/* Host Unique */
			if ((tag = get_tag(ph, PTT_AC_COOKIE)))
				insert_tag(sp, tag); 	/* return cookie */
			if ((tag = get_tag(ph, PTT_AC_NAME))) {	
				insert_tag(sp, tag); 	/* return it */
				send_acname(sp, tag);
			}
			if ((tag = get_tag(ph, PTT_MAX_PAYL)) &&
			    (privp->max_payload.data != 0))
				insert_tag(sp, tag);	/* return it */
			insert_tag(sp, &neg->service.hdr); /* Service */
			scan_tags(sp, ph);
			make_packet(sp);
			sp->state = PPPOE_SREQ;
			ng_callout(&neg->handle, node, sp->hook,
			    PPPOE_INITIAL_TIMEOUT * hz,
			    pppoe_ticker, NULL, 0);
			neg->timeout = PPPOE_INITIAL_TIMEOUT * 2;
			m0 = m_copypacket(neg->m, M_NOWAIT);
			NG_FWD_NEW_DATA(error, item, privp->ethernet_hook, m0);
			break;
		case	PADR_CODE:
			/*
			 * We are a server:
			 * Use the ac_cookie tag to find the
			 * hook this is in response to.
			 */
			utag = get_tag(ph, PTT_AC_COOKIE);
			if ((utag == NULL) ||
			    (ntohs(utag->tag_len) != sizeof(sp))) {
				LEAVE(ENETUNREACH);
			}

			sendhook = pppoe_findcookie(node, utag);
			if (sendhook == NULL)
				LEAVE(ENETUNREACH);

			/*
			 * Check the session is in the right state.
			 * It needs to be in PPPOE_SOFFER or PPPOE_NEWCONNECTED.
			 * If the latter, then this is a retry by the client,
			 * so be nice, and resend.
			 */
			sp = NG_HOOK_PRIVATE(sendhook);
			if (sp->state == PPPOE_NEWCONNECTED) {
				/*
				 * Whoa! drop back to resend that PADS packet.
				 * We should still have a copy of it.
				 */
				sp->state = PPPOE_SOFFER;
			} else if (sp->state != PPPOE_SOFFER)
				LEAVE (ENETUNREACH);
			neg = sp->neg;
			ng_uncallout(&neg->handle, node);
			neg->pkt->pkt_header.ph.code = PADS_CODE;
			if (sp->Session_ID == 0) {
				neg->pkt->pkt_header.ph.sid =
				    htons(pppoe_getnewsession(sp));
			}
			send_sessionid(sp);
			neg->timeout = 0;
			/*
			 * start working out the tags to respond with.
			 */
			init_tags(sp);
			insert_tag(sp, &neg->ac_name.hdr); /* AC_NAME */
			if ((tag = get_tag(ph, PTT_SRV_NAME)))
				insert_tag(sp, tag);/* return service */
			if ((tag = get_tag(ph, PTT_HOST_UNIQ)))
				insert_tag(sp, tag); /* return it */
			insert_tag(sp, utag);	/* ac_cookie */
			scan_tags(sp, ph);
			make_packet(sp);
			sp->state = PPPOE_NEWCONNECTED;

			/* Send the PADS without a timeout - we're now connected. */
			m0 = m_copypacket(sp->neg->m, M_NOWAIT);
			NG_FWD_NEW_DATA(error, item, privp->ethernet_hook, m0);

			/*
			 * Having sent the last Negotiation header,
			 * Set up the stored packet header to be correct for
			 * the actual session. But keep the negotialtion stuff
			 * around in case we need to resend this last packet.
			 * We'll discard it when we move from NEWCONNECTED
			 * to CONNECTED
			 */
			sp->pkt_hdr = neg->pkt->pkt_header;
			/* Configure ethertype depending on what
			 * ethertype was used at discovery phase */
			if (sp->pkt_hdr.eh.ether_type ==
			    ETHERTYPE_PPPOE_3COM_DISC)
				sp->pkt_hdr.eh.ether_type
					= ETHERTYPE_PPPOE_3COM_SESS;
			else
				sp->pkt_hdr.eh.ether_type
					= ETHERTYPE_PPPOE_SESS;
			sp->pkt_hdr.ph.code = 0;
			pppoe_send_event(sp, NGM_PPPOE_SUCCESS);
			break;
		case	PADS_CODE:
			/*
			 * We are a client:
			 * Use the host_uniq tag to find the hook this is in
			 * response to. Take the session ID and store it away.
			 * Also make sure the pre-made header is correct and
			 * set us into Session mode.
			 */
			utag = get_tag(ph, PTT_HOST_UNIQ);
			if (utag == NULL) {
				LEAVE (ENETUNREACH);
			}
			sendhook = pppoe_finduniq(node, utag);
			if (sendhook == NULL)
				LEAVE(ENETUNREACH);

			/*
			 * Check the session is in the right state.
			 * It needs to be in PPPOE_SREQ.
			 */
			sp = NG_HOOK_PRIVATE(sendhook);
			if (sp->state != PPPOE_SREQ)
				LEAVE(ENETUNREACH);
			neg = sp->neg;
			ng_uncallout(&neg->handle, node);
			neg->pkt->pkt_header.ph.sid = wh->ph.sid;
			sp->Session_ID = ntohs(wh->ph.sid);
			pppoe_addsession(sp);
			send_sessionid(sp);
			neg->timeout = 0;
			sp->state = PPPOE_CONNECTED;
			/*
			 * Now we have gone to Connected mode,
			 * Free all resources needed for negotiation.
			 * Keep a copy of the header we will be using.
			 */
			sp->pkt_hdr = neg->pkt->pkt_header;
			if (privp->flags & COMPAT_3COM)
				sp->pkt_hdr.eh.ether_type
					= ETHERTYPE_PPPOE_3COM_SESS;
			else
				sp->pkt_hdr.eh.ether_type
					= ETHERTYPE_PPPOE_SESS;
			sp->pkt_hdr.ph.code = 0;
			m_freem(neg->m);
			free(sp->neg, M_NETGRAPH_PPPOE);
			sp->neg = NULL;
			if ((tag = get_tag(ph, PTT_MAX_PAYL)) &&
			    (privp->max_payload.data != 0))
				send_maxp(sp, tag);
			pppoe_send_event(sp, NGM_PPPOE_SUCCESS);
			break;
		case	PADT_CODE:
			/*
			 * Find matching peer/session combination.
			 */
			sp = pppoe_findsession(privp, wh);
			if (sp == NULL)
				LEAVE(ENETUNREACH);
			/* Disconnect that hook. */
			ng_rmhook_self(sp->hook);
			break;
		case	PADM_CODE:
			/*
			 * We are a client:
			 * find matching peer/session combination.
			 */
			sp = pppoe_findsession(privp, wh);
			if (sp == NULL)
				LEAVE (ENETUNREACH);
			if ((tag = get_tag(ph, PTT_HURL)))
				send_hurl(sp, tag);
			if ((tag = get_tag(ph, PTT_MOTM)))
				send_motm(sp, tag);
			break;
		default:
			LEAVE(EPFNOSUPPORT);
		}
		break;
	case	ETHERTYPE_PPPOE_3COM_SESS:
	case	ETHERTYPE_PPPOE_SESS:
		/*
		 * Find matching peer/session combination.
		 */
		sp = pppoe_findsession(privp, wh);
		if (sp == NULL)
			LEAVE (ENETUNREACH);
		m_adj(m, sizeof(*wh));

		/* If packet too short, dump it. */
		if (m->m_pkthdr.len < length)
			LEAVE(EMSGSIZE);
		/* Also need to trim excess at the end */
		if (m->m_pkthdr.len > length) {
			m_adj(m, -((int)(m->m_pkthdr.len - length)));
		}
		if ( sp->state != PPPOE_CONNECTED) {
			if (sp->state == PPPOE_NEWCONNECTED) {
				sp->state = PPPOE_CONNECTED;
				/*
				 * Now we have gone to Connected mode,
				 * Free all resources needed for negotiation.
				 * Be paranoid about whether there may be
				 * a timeout.
				 */
				m_freem(sp->neg->m);
				ng_uncallout(&sp->neg->handle, node);
				free(sp->neg, M_NETGRAPH_PPPOE);
				sp->neg = NULL;
			} else {
				LEAVE (ENETUNREACH);
			}
		}
		NG_FWD_NEW_DATA(error, item, sp->hook, m);
		break;
	default:
		LEAVE(EPFNOSUPPORT);
	}
quit:
	if (item)
		NG_FREE_ITEM(item);
	NG_FREE_M(m);
	return (error);
}

/*
 * Receive data from debug hook and bypass it to ether.
 */
static int
ng_pppoe_rcvdata_debug(hook_p hook, item_p item)
{
	node_p		node = NG_HOOK_NODE(hook);
	const priv_p	privp = NG_NODE_PRIVATE(node);
	int		error;

	CTR6(KTR_NET, "%20s: node [%x] (%p) received %p on \"%s\" (%p)",
	    __func__, node->nd_ID, node, item, hook->hk_name, hook);

	NG_FWD_ITEM_HOOK(error, item, privp->ethernet_hook);
	privp->packets_out++;
	return (error);
}

/*
 * Do local shutdown processing..
 * If we are a persistent device, we might refuse to go away, and
 * we'd only remove our links and reset ourself.
 */
static int
ng_pppoe_shutdown(node_p node)
{
	const priv_p privp = NG_NODE_PRIVATE(node);
	int	i;

	for (i = 0; i < SESSHASHSIZE; i++)
	    mtx_destroy(&privp->sesshash[i].mtx);
	NG_NODE_SET_PRIVATE(node, NULL);
	NG_NODE_UNREF(privp->node);
	free(privp, M_NETGRAPH_PPPOE);
	return (0);
}

/*
 * Hook disconnection
 *
 * Clean up all dangling links and information about the session/hook.
 * For this type, removal of the last link destroys the node.
 */
static int
ng_pppoe_disconnect(hook_p hook)
{
	node_p node = NG_HOOK_NODE(hook);
	priv_p privp = NG_NODE_PRIVATE(node);
	sessp	sp;

	if (hook == privp->debug_hook) {
		privp->debug_hook = NULL;
	} else if (hook == privp->ethernet_hook) {
		privp->ethernet_hook = NULL;
		if (NG_NODE_IS_VALID(node))
			ng_rmnode_self(node);
	} else {
		sp = NG_HOOK_PRIVATE(hook);
		if (sp->state != PPPOE_SNONE ) {
			pppoe_send_event(sp, NGM_PPPOE_CLOSE);
		}
		/*
		 * According to the spec, if we are connected,
		 * we should send a DISC packet if we are shutting down
		 * a session.
		 */
		if ((privp->ethernet_hook)
		&& ((sp->state == PPPOE_CONNECTED)
		 || (sp->state == PPPOE_NEWCONNECTED))) {
			struct mbuf *m;

			/* Generate a packet of that type. */
			m = m_gethdr(M_NOWAIT, MT_DATA);
			if (m == NULL)
				log(LOG_NOTICE, "ng_pppoe[%x]: session out of "
				    "mbufs\n", node->nd_ID);
			else {
				struct pppoe_full_hdr *wh;
				struct pppoe_tag *tag;
				int	msglen = strlen(SIGNOFF);
				int	error = 0;

				wh = mtod(m, struct pppoe_full_hdr *);
				bcopy(&sp->pkt_hdr, wh, sizeof(*wh));

				/* Revert the stored header to DISC/PADT mode. */
				wh->ph.code = PADT_CODE;
				/*
				 * Configure ethertype depending on what
				 * was used during sessions stage.
				 */
				if (wh->eh.ether_type == 
				    ETHERTYPE_PPPOE_3COM_SESS)
					wh->eh.ether_type = ETHERTYPE_PPPOE_3COM_DISC;
				else
					wh->eh.ether_type = ETHERTYPE_PPPOE_DISC;
				/*
				 * Add a General error message and adjust
				 * sizes.
				 */
				tag = (void *)(&wh->ph + 1);
				tag->tag_type = PTT_GEN_ERR;
				tag->tag_len = htons((u_int16_t)msglen);
				strncpy((char *)(tag + 1), SIGNOFF, msglen);
				m->m_pkthdr.len = m->m_len = sizeof(*wh) + sizeof(*tag) +
				    msglen;
				wh->ph.length = htons(sizeof(*tag) + msglen);
				NG_SEND_DATA_ONLY(error,
					privp->ethernet_hook, m);
			}
		}
		if (sp->state == PPPOE_LISTENING)
			LIST_REMOVE(sp, sessions);
		else if (sp->Session_ID)
			pppoe_delsession(sp);
		/*
		 * As long as we have somewhere to store the timeout handle,
		 * we may have a timeout pending.. get rid of it.
		 */
		if (sp->neg) {
			ng_uncallout(&sp->neg->handle, node);
			if (sp->neg->m)
				m_freem(sp->neg->m);
			free(sp->neg, M_NETGRAPH_PPPOE);
		}
		free(sp, M_NETGRAPH_PPPOE);
		NG_HOOK_SET_PRIVATE(hook, NULL);
	}
	if ((NG_NODE_NUMHOOKS(node) == 0) &&
	    (NG_NODE_IS_VALID(node)))
		ng_rmnode_self(node);
	return (0);
}

/*
 * Timeouts come here.
 */
static void
pppoe_ticker(node_p node, hook_p hook, void *arg1, int arg2)
{
	priv_p privp = NG_NODE_PRIVATE(NG_HOOK_NODE(hook));
	sessp	sp = NG_HOOK_PRIVATE(hook);
	negp	neg = sp->neg;
	struct mbuf *m0 = NULL;
	int	error = 0;

	CTR6(KTR_NET, "%20s: node [%x] (%p) hook \"%s\" (%p) session %d",
	    __func__, node->nd_ID, node, hook->hk_name, hook, sp->Session_ID);
	switch(sp->state) {
		/*
		 * Resend the last packet, using an exponential backoff.
		 * After a period of time, stop growing the backoff,
		 * And either leave it, or revert to the start.
		 */
	case	PPPOE_SINIT:
	case	PPPOE_SREQ:
		/* Timeouts on these produce resends. */
		m0 = m_copypacket(sp->neg->m, M_NOWAIT);
		NG_SEND_DATA_ONLY( error, privp->ethernet_hook, m0);
		ng_callout(&neg->handle, node, hook, neg->timeout * hz,
		    pppoe_ticker, NULL, 0);
		if ((neg->timeout <<= 1) > PPPOE_TIMEOUT_LIMIT) {
			if (sp->state == PPPOE_SREQ) {
				/* Revert to SINIT mode. */
				pppoe_start(sp);
			} else {
				neg->timeout = PPPOE_TIMEOUT_LIMIT;
			}
		}
		break;
	case	PPPOE_PRIMED:
	case	PPPOE_SOFFER:
		/* A timeout on these says "give up" */
		ng_rmhook_self(hook);
		break;
	default:
		/* Timeouts have no meaning in other states. */
		log(LOG_NOTICE, "ng_pppoe[%x]: unexpected timeout\n",
		    node->nd_ID);
	}
}

/*
 * Parse an incoming packet to see if any tags should be copied to the
 * output packet. Don't do any tags that have been handled in the main
 * state machine.
 */
static const struct pppoe_tag*
scan_tags(sessp	sp, const struct pppoe_hdr* ph)
{
	const char *const end = (const char *)next_tag(ph);
	const char *ptn;
	const struct pppoe_tag *pt = (const void *)(ph + 1);

	/*
	 * Keep processing tags while a tag header will still fit.
	 */
	CTR2(KTR_NET, "%20s: called %d", __func__, sp->Session_ID);

	while((const char*)(pt + 1) <= end) {
		/*
		 * If the tag data would go past the end of the packet, abort.
		 */
		ptn = (((const char *)(pt + 1)) + ntohs(pt->tag_len));
		if(ptn > end)
			return NULL;

		switch (pt->tag_type) {
		case	PTT_RELAY_SID:
			insert_tag(sp, pt);
			break;
		case	PTT_EOL:
			return NULL;
		case	PTT_SRV_NAME:
		case	PTT_AC_NAME:
		case	PTT_HOST_UNIQ:
		case	PTT_AC_COOKIE:
		case	PTT_VENDOR:
		case	PTT_SRV_ERR:
		case	PTT_SYS_ERR:
		case	PTT_GEN_ERR:
		case	PTT_MAX_PAYL:
		case	PTT_HURL:
		case	PTT_MOTM:
			break;
		}
		pt = (const struct pppoe_tag*)ptn;
	}
	return NULL;
}
	
static	int
pppoe_send_event(sessp sp, enum cmd cmdid)
{
	int error;
	struct ng_mesg *msg;
	struct ngpppoe_sts *sts;

	CTR2(KTR_NET, "%20s: called %d", __func__, sp->Session_ID);

	NG_MKMESSAGE(msg, NGM_PPPOE_COOKIE, cmdid,
			sizeof(struct ngpppoe_sts), M_NOWAIT);
	if (msg == NULL)
		return (ENOMEM);
	sts = (struct ngpppoe_sts *)msg->data;
	strncpy(sts->hook, NG_HOOK_NAME(sp->hook), NG_HOOKSIZ);
	NG_SEND_MSG_ID(error, NG_HOOK_NODE(sp->hook), msg, sp->creator, 0);
	return (error);
}
