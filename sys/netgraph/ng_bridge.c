/*
 * ng_bridge.c
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

/*
 * ng_bridge(4) netgraph node type
 *
 * The node performs standard intelligent Ethernet bridging over
 * each of its connected hooks, or links.  A simple loop detection
 * algorithm is included which disables a link for priv->conf.loopTimeout
 * seconds when a host is seen to have jumped from one link to
 * another within priv->conf.minStableAge seconds.
 *
 * We keep a hashtable that maps Ethernet addresses to host info,
 * which is contained in struct ng_bridge_host's. These structures
 * tell us on which link the host may be found. A host's entry will
 * expire after priv->conf.maxStaleness seconds.
 *
 * This node is optimzed for stable networks, where machines jump
 * from one port to the other only rarely.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/errno.h>
#include <sys/rwlock.h>
#include <sys/syslog.h>
#include <sys/socket.h>
#include <sys/ctype.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/ethernet.h>
#include <net/vnet.h>

#include <netinet/in.h>
#if 0	/* not used yet */
#include <netinet/ip_fw.h>
#endif
#include <netgraph/ng_message.h>
#include <netgraph/netgraph.h>
#include <netgraph/ng_parse.h>
#include <netgraph/ng_bridge.h>

#ifdef NG_SEPARATE_MALLOC
static MALLOC_DEFINE(M_NETGRAPH_BRIDGE, "netgraph_bridge",
    "netgraph bridge node");
#else
#define M_NETGRAPH_BRIDGE M_NETGRAPH
#endif

/* Per-link private data */
struct ng_bridge_link {
	hook_p				hook;		/* netgraph hook */
	u_int16_t			loopCount;	/* loop ignore timer */
	struct ng_bridge_link_stats	stats;		/* link stats */
};

/* Per-node private data */
struct ng_bridge_private {
	struct ng_bridge_bucket	*tab;		/* hash table bucket array */
	struct ng_bridge_link	*links[NG_BRIDGE_MAX_LINKS];
	struct ng_bridge_config	conf;		/* node configuration */
	node_p			node;		/* netgraph node */
	u_int			numHosts;	/* num entries in table */
	u_int			numBuckets;	/* num buckets in table */
	u_int			hashMask;	/* numBuckets - 1 */
	int			numLinks;	/* num connected links */
	int			persistent;	/* can exist w/o hooks */
	struct callout		timer;		/* one second periodic timer */
};
typedef struct ng_bridge_private *priv_p;

/* Information about a host, stored in a hash table entry */
struct ng_bridge_hent {
	struct ng_bridge_host		host;	/* actual host info */
	SLIST_ENTRY(ng_bridge_hent)	next;	/* next entry in bucket */
};

/* Hash table bucket declaration */
SLIST_HEAD(ng_bridge_bucket, ng_bridge_hent);

/* Netgraph node methods */
static ng_constructor_t	ng_bridge_constructor;
static ng_rcvmsg_t	ng_bridge_rcvmsg;
static ng_shutdown_t	ng_bridge_shutdown;
static ng_newhook_t	ng_bridge_newhook;
static ng_rcvdata_t	ng_bridge_rcvdata;
static ng_disconnect_t	ng_bridge_disconnect;

/* Other internal functions */
static struct	ng_bridge_host *ng_bridge_get(priv_p priv, const u_char *addr);
static int	ng_bridge_put(priv_p priv, const u_char *addr, int linkNum);
static void	ng_bridge_rehash(priv_p priv);
static void	ng_bridge_remove_hosts(priv_p priv, int linkNum);
static void	ng_bridge_timeout(node_p node, hook_p hook, void *arg1, int arg2);
static const	char *ng_bridge_nodename(node_p node);

/* Ethernet broadcast */
static const u_char ng_bridge_bcast_addr[ETHER_ADDR_LEN] =
    { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

/* Store each hook's link number in the private field */
#define LINK_NUM(hook)		(*(u_int16_t *)(&(hook)->private))

/* Compare Ethernet addresses using 32 and 16 bit words instead of bytewise */
#define ETHER_EQUAL(a,b)	(((const u_int32_t *)(a))[0] \
					== ((const u_int32_t *)(b))[0] \
				    && ((const u_int16_t *)(a))[2] \
					== ((const u_int16_t *)(b))[2])

/* Minimum and maximum number of hash buckets. Must be a power of two. */
#define MIN_BUCKETS		(1 << 5)	/* 32 */
#define MAX_BUCKETS		(1 << 14)	/* 16384 */

/* Configuration default values */
#define DEFAULT_LOOP_TIMEOUT	60
#define DEFAULT_MAX_STALENESS	(15 * 60)	/* same as ARP timeout */
#define DEFAULT_MIN_STABLE_AGE	1

/******************************************************************
		    NETGRAPH PARSE TYPES
******************************************************************/

/*
 * How to determine the length of the table returned by NGM_BRIDGE_GET_TABLE
 */
static int
ng_bridge_getTableLength(const struct ng_parse_type *type,
	const u_char *start, const u_char *buf)
{
	const struct ng_bridge_host_ary *const hary
	    = (const struct ng_bridge_host_ary *)(buf - sizeof(u_int32_t));

	return hary->numHosts;
}

/* Parse type for struct ng_bridge_host_ary */
static const struct ng_parse_struct_field ng_bridge_host_type_fields[]
	= NG_BRIDGE_HOST_TYPE_INFO(&ng_parse_enaddr_type);
static const struct ng_parse_type ng_bridge_host_type = {
	&ng_parse_struct_type,
	&ng_bridge_host_type_fields
};
static const struct ng_parse_array_info ng_bridge_hary_type_info = {
	&ng_bridge_host_type,
	ng_bridge_getTableLength
};
static const struct ng_parse_type ng_bridge_hary_type = {
	&ng_parse_array_type,
	&ng_bridge_hary_type_info
};
static const struct ng_parse_struct_field ng_bridge_host_ary_type_fields[]
	= NG_BRIDGE_HOST_ARY_TYPE_INFO(&ng_bridge_hary_type);
static const struct ng_parse_type ng_bridge_host_ary_type = {
	&ng_parse_struct_type,
	&ng_bridge_host_ary_type_fields
};

/* Parse type for struct ng_bridge_config */
static const struct ng_parse_fixedarray_info ng_bridge_ipfwary_type_info = {
	&ng_parse_uint8_type,
	NG_BRIDGE_MAX_LINKS
};
static const struct ng_parse_type ng_bridge_ipfwary_type = {
	&ng_parse_fixedarray_type,
	&ng_bridge_ipfwary_type_info
};
static const struct ng_parse_struct_field ng_bridge_config_type_fields[]
	= NG_BRIDGE_CONFIG_TYPE_INFO(&ng_bridge_ipfwary_type);
static const struct ng_parse_type ng_bridge_config_type = {
	&ng_parse_struct_type,
	&ng_bridge_config_type_fields
};

/* Parse type for struct ng_bridge_link_stat */
static const struct ng_parse_struct_field ng_bridge_stats_type_fields[]
	= NG_BRIDGE_STATS_TYPE_INFO;
static const struct ng_parse_type ng_bridge_stats_type = {
	&ng_parse_struct_type,
	&ng_bridge_stats_type_fields
};

/* List of commands and how to convert arguments to/from ASCII */
static const struct ng_cmdlist ng_bridge_cmdlist[] = {
	{
	  NGM_BRIDGE_COOKIE,
	  NGM_BRIDGE_SET_CONFIG,
	  "setconfig",
	  &ng_bridge_config_type,
	  NULL
	},
	{
	  NGM_BRIDGE_COOKIE,
	  NGM_BRIDGE_GET_CONFIG,
	  "getconfig",
	  NULL,
	  &ng_bridge_config_type
	},
	{
	  NGM_BRIDGE_COOKIE,
	  NGM_BRIDGE_RESET,
	  "reset",
	  NULL,
	  NULL
	},
	{
	  NGM_BRIDGE_COOKIE,
	  NGM_BRIDGE_GET_STATS,
	  "getstats",
	  &ng_parse_uint32_type,
	  &ng_bridge_stats_type
	},
	{
	  NGM_BRIDGE_COOKIE,
	  NGM_BRIDGE_CLR_STATS,
	  "clrstats",
	  &ng_parse_uint32_type,
	  NULL
	},
	{
	  NGM_BRIDGE_COOKIE,
	  NGM_BRIDGE_GETCLR_STATS,
	  "getclrstats",
	  &ng_parse_uint32_type,
	  &ng_bridge_stats_type
	},
	{
	  NGM_BRIDGE_COOKIE,
	  NGM_BRIDGE_GET_TABLE,
	  "gettable",
	  NULL,
	  &ng_bridge_host_ary_type
	},
	{
	  NGM_BRIDGE_COOKIE,
	  NGM_BRIDGE_SET_PERSISTENT,
	  "setpersistent",
	  NULL,
	  NULL
	},
	{ 0 }
};

/* Node type descriptor */
static struct ng_type ng_bridge_typestruct = {
	.version =	NG_ABI_VERSION,
	.name =		NG_BRIDGE_NODE_TYPE,
	.constructor =	ng_bridge_constructor,
	.rcvmsg =	ng_bridge_rcvmsg,
	.shutdown =	ng_bridge_shutdown,
	.newhook =	ng_bridge_newhook,
	.rcvdata =	ng_bridge_rcvdata,
	.disconnect =	ng_bridge_disconnect,
	.cmdlist =	ng_bridge_cmdlist,
};
NETGRAPH_INIT(bridge, &ng_bridge_typestruct);

/******************************************************************
		    NETGRAPH NODE METHODS
******************************************************************/

/*
 * Node constructor
 */
static int
ng_bridge_constructor(node_p node)
{
	priv_p priv;

	/* Allocate and initialize private info */
	priv = malloc(sizeof(*priv), M_NETGRAPH_BRIDGE, M_WAITOK | M_ZERO);
	ng_callout_init(&priv->timer);

	/* Allocate and initialize hash table, etc. */
	priv->tab = malloc(MIN_BUCKETS * sizeof(*priv->tab),
	    M_NETGRAPH_BRIDGE, M_WAITOK | M_ZERO);
	priv->numBuckets = MIN_BUCKETS;
	priv->hashMask = MIN_BUCKETS - 1;
	priv->conf.debugLevel = 1;
	priv->conf.loopTimeout = DEFAULT_LOOP_TIMEOUT;
	priv->conf.maxStaleness = DEFAULT_MAX_STALENESS;
	priv->conf.minStableAge = DEFAULT_MIN_STABLE_AGE;

	/*
	 * This node has all kinds of stuff that could be screwed by SMP.
	 * Until it gets it's own internal protection, we go through in 
	 * single file. This could hurt a machine bridging between two 
	 * GB ethernets so it should be fixed. 
	 * When it's fixed the process SHOULD NOT SLEEP, spinlocks please!
	 * (and atomic ops )
	 */
	NG_NODE_FORCE_WRITER(node);
	NG_NODE_SET_PRIVATE(node, priv);
	priv->node = node;

	/* Start timer; timer is always running while node is alive */
	ng_callout(&priv->timer, node, NULL, hz, ng_bridge_timeout, NULL, 0);

	/* Done */
	return (0);
}

/*
 * Method for attaching a new hook
 */
static	int
ng_bridge_newhook(node_p node, hook_p hook, const char *name)
{
	const priv_p priv = NG_NODE_PRIVATE(node);

	/* Check for a link hook */
	if (strncmp(name, NG_BRIDGE_HOOK_LINK_PREFIX,
	    strlen(NG_BRIDGE_HOOK_LINK_PREFIX)) == 0) {
		const char *cp;
		char *eptr;
		u_long linkNum;

		cp = name + strlen(NG_BRIDGE_HOOK_LINK_PREFIX);
		if (!isdigit(*cp) || (cp[0] == '0' && cp[1] != '\0'))
			return (EINVAL);
		linkNum = strtoul(cp, &eptr, 10);
		if (*eptr != '\0' || linkNum >= NG_BRIDGE_MAX_LINKS)
			return (EINVAL);
		if (priv->links[linkNum] != NULL)
			return (EISCONN);
		priv->links[linkNum] = malloc(sizeof(*priv->links[linkNum]),
		    M_NETGRAPH_BRIDGE, M_NOWAIT|M_ZERO);
		if (priv->links[linkNum] == NULL)
			return (ENOMEM);
		priv->links[linkNum]->hook = hook;
		NG_HOOK_SET_PRIVATE(hook, (void *)linkNum);
		priv->numLinks++;
		return (0);
	}

	/* Unknown hook name */
	return (EINVAL);
}

/*
 * Receive a control message
 */
static int
ng_bridge_rcvmsg(node_p node, item_p item, hook_p lasthook)
{
	const priv_p priv = NG_NODE_PRIVATE(node);
	struct ng_mesg *resp = NULL;
	int error = 0;
	struct ng_mesg *msg;

	NGI_GET_MSG(item, msg);
	switch (msg->header.typecookie) {
	case NGM_BRIDGE_COOKIE:
		switch (msg->header.cmd) {
		case NGM_BRIDGE_GET_CONFIG:
		    {
			struct ng_bridge_config *conf;

			NG_MKRESPONSE(resp, msg,
			    sizeof(struct ng_bridge_config), M_NOWAIT);
			if (resp == NULL) {
				error = ENOMEM;
				break;
			}
			conf = (struct ng_bridge_config *)resp->data;
			*conf = priv->conf;	/* no sanity checking needed */
			break;
		    }
		case NGM_BRIDGE_SET_CONFIG:
		    {
			struct ng_bridge_config *conf;
			int i;

			if (msg->header.arglen
			    != sizeof(struct ng_bridge_config)) {
				error = EINVAL;
				break;
			}
			conf = (struct ng_bridge_config *)msg->data;
			priv->conf = *conf;
			for (i = 0; i < NG_BRIDGE_MAX_LINKS; i++)
				priv->conf.ipfw[i] = !!priv->conf.ipfw[i];
			break;
		    }
		case NGM_BRIDGE_RESET:
		    {
			int i;

			/* Flush all entries in the hash table */
			ng_bridge_remove_hosts(priv, -1);

			/* Reset all loop detection counters and stats */
			for (i = 0; i < NG_BRIDGE_MAX_LINKS; i++) {
				if (priv->links[i] == NULL)
					continue;
				priv->links[i]->loopCount = 0;
				bzero(&priv->links[i]->stats,
				    sizeof(priv->links[i]->stats));
			}
			break;
		    }
		case NGM_BRIDGE_GET_STATS:
		case NGM_BRIDGE_CLR_STATS:
		case NGM_BRIDGE_GETCLR_STATS:
		    {
			struct ng_bridge_link *link;
			int linkNum;

			/* Get link number */
			if (msg->header.arglen != sizeof(u_int32_t)) {
				error = EINVAL;
				break;
			}
			linkNum = *((u_int32_t *)msg->data);
			if (linkNum < 0 || linkNum >= NG_BRIDGE_MAX_LINKS) {
				error = EINVAL;
				break;
			}
			if ((link = priv->links[linkNum]) == NULL) {
				error = ENOTCONN;
				break;
			}

			/* Get/clear stats */
			if (msg->header.cmd != NGM_BRIDGE_CLR_STATS) {
				NG_MKRESPONSE(resp, msg,
				    sizeof(link->stats), M_NOWAIT);
				if (resp == NULL) {
					error = ENOMEM;
					break;
				}
				bcopy(&link->stats,
				    resp->data, sizeof(link->stats));
			}
			if (msg->header.cmd != NGM_BRIDGE_GET_STATS)
				bzero(&link->stats, sizeof(link->stats));
			break;
		    }
		case NGM_BRIDGE_GET_TABLE:
		    {
			struct ng_bridge_host_ary *ary;
			struct ng_bridge_hent *hent;
			int i = 0, bucket;

			NG_MKRESPONSE(resp, msg, sizeof(*ary)
			    + (priv->numHosts * sizeof(*ary->hosts)), M_NOWAIT);
			if (resp == NULL) {
				error = ENOMEM;
				break;
			}
			ary = (struct ng_bridge_host_ary *)resp->data;
			ary->numHosts = priv->numHosts;
			for (bucket = 0; bucket < priv->numBuckets; bucket++) {
				SLIST_FOREACH(hent, &priv->tab[bucket], next)
					ary->hosts[i++] = hent->host;
			}
			break;
		    }
		case NGM_BRIDGE_SET_PERSISTENT:
		    {
			priv->persistent = 1;
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

	/* Done */
	NG_RESPOND_MSG(error, node, item, resp);
	NG_FREE_MSG(msg);
	return (error);
}

/*
 * Receive data on a hook
 */
static int
ng_bridge_rcvdata(hook_p hook, item_p item)
{
	const node_p node = NG_HOOK_NODE(hook);
	const priv_p priv = NG_NODE_PRIVATE(node);
	struct ng_bridge_host *host;
	struct ng_bridge_link *link;
	struct ether_header *eh;
	int error = 0, linkNum, linksSeen;
	int manycast;
	struct mbuf *m;
	struct ng_bridge_link *firstLink;

	NGI_GET_M(item, m);
	/* Get link number */
	linkNum = (intptr_t)NG_HOOK_PRIVATE(hook);
	KASSERT(linkNum >= 0 && linkNum < NG_BRIDGE_MAX_LINKS,
	    ("%s: linkNum=%u", __func__, linkNum));
	link = priv->links[linkNum];
	KASSERT(link != NULL, ("%s: link%d null", __func__, linkNum));

	/* Sanity check packet and pull up header */
	if (m->m_pkthdr.len < ETHER_HDR_LEN) {
		link->stats.recvRunts++;
		NG_FREE_ITEM(item);
		NG_FREE_M(m);
		return (EINVAL);
	}
	if (m->m_len < ETHER_HDR_LEN && !(m = m_pullup(m, ETHER_HDR_LEN))) {
		link->stats.memoryFailures++;
		NG_FREE_ITEM(item);
		return (ENOBUFS);
	}
	eh = mtod(m, struct ether_header *);
	if ((eh->ether_shost[0] & 1) != 0) {
		link->stats.recvInvalid++;
		NG_FREE_ITEM(item);
		NG_FREE_M(m);
		return (EINVAL);
	}

	/* Is link disabled due to a loopback condition? */
	if (link->loopCount != 0) {
		link->stats.loopDrops++;
		NG_FREE_ITEM(item);
		NG_FREE_M(m);
		return (ELOOP);		/* XXX is this an appropriate error? */
	}

	/* Update stats */
	link->stats.recvPackets++;
	link->stats.recvOctets += m->m_pkthdr.len;
	if ((manycast = (eh->ether_dhost[0] & 1)) != 0) {
		if (ETHER_EQUAL(eh->ether_dhost, ng_bridge_bcast_addr)) {
			link->stats.recvBroadcasts++;
			manycast = 2;
		} else
			link->stats.recvMulticasts++;
	}

	/* Look up packet's source Ethernet address in hashtable */
	if ((host = ng_bridge_get(priv, eh->ether_shost)) != NULL) {

		/* Update time since last heard from this host */
		host->staleness = 0;

		/* Did host jump to a different link? */
		if (host->linkNum != linkNum) {

			/*
			 * If the host's old link was recently established
			 * on the old link and it's already jumped to a new
			 * link, declare a loopback condition.
			 */
			if (host->age < priv->conf.minStableAge) {

				/* Log the problem */
				if (priv->conf.debugLevel >= 2) {
					struct ifnet *ifp = m->m_pkthdr.rcvif;
					char suffix[32];

					if (ifp != NULL)
						snprintf(suffix, sizeof(suffix),
						    " (%s)", ifp->if_xname);
					else
						*suffix = '\0';
					log(LOG_WARNING, "ng_bridge: %s:"
					    " loopback detected on %s%s\n",
					    ng_bridge_nodename(node),
					    NG_HOOK_NAME(hook), suffix);
				}

				/* Mark link as linka non grata */
				link->loopCount = priv->conf.loopTimeout;
				link->stats.loopDetects++;

				/* Forget all hosts on this link */
				ng_bridge_remove_hosts(priv, linkNum);

				/* Drop packet */
				link->stats.loopDrops++;
				NG_FREE_ITEM(item);
				NG_FREE_M(m);
				return (ELOOP);		/* XXX appropriate? */
			}

			/* Move host over to new link */
			host->linkNum = linkNum;
			host->age = 0;
		}
	} else {
		if (!ng_bridge_put(priv, eh->ether_shost, linkNum)) {
			link->stats.memoryFailures++;
			NG_FREE_ITEM(item);
			NG_FREE_M(m);
			return (ENOMEM);
		}
	}

	/* Run packet through ipfw processing, if enabled */
#if 0
	if (priv->conf.ipfw[linkNum] && V_fw_enable && V_ip_fw_chk_ptr != NULL) {
		/* XXX not implemented yet */
	}
#endif

	/*
	 * If unicast and destination host known, deliver to host's link,
	 * unless it is the same link as the packet came in on.
	 */
	if (!manycast) {

		/* Determine packet destination link */
		if ((host = ng_bridge_get(priv, eh->ether_dhost)) != NULL) {
			struct ng_bridge_link *const destLink
			    = priv->links[host->linkNum];

			/* If destination same as incoming link, do nothing */
			KASSERT(destLink != NULL,
			    ("%s: link%d null", __func__, host->linkNum));
			if (destLink == link) {
				NG_FREE_ITEM(item);
				NG_FREE_M(m);
				return (0);
			}

			/* Deliver packet out the destination link */
			destLink->stats.xmitPackets++;
			destLink->stats.xmitOctets += m->m_pkthdr.len;
			NG_FWD_NEW_DATA(error, item, destLink->hook, m);
			return (error);
		}

		/* Destination host is not known */
		link->stats.recvUnknown++;
	}

	/* Distribute unknown, multicast, broadcast pkts to all other links */
	firstLink = NULL;
	for (linkNum = linksSeen = 0; linksSeen <= priv->numLinks; linkNum++) {
		struct ng_bridge_link *destLink;
		struct mbuf *m2 = NULL;

		/*
		 * If we have checked all the links then now
		 * send the original on its reserved link
		 */
		if (linksSeen == priv->numLinks) {
			/* If we never saw a good link, leave. */
			if (firstLink == NULL) {
				NG_FREE_ITEM(item);
				NG_FREE_M(m);
				return (0);
			}	
			destLink = firstLink;
		} else {
			destLink = priv->links[linkNum];
			if (destLink != NULL)
				linksSeen++;
			/* Skip incoming link and disconnected links */
			if (destLink == NULL || destLink == link) {
				continue;
			}
			if (firstLink == NULL) {
				/*
				 * This is the first usable link we have found.
				 * Reserve it for the originals.
				 * If we never find another we save a copy.
				 */
				firstLink = destLink;
				continue;
			}

			/*
			 * It's usable link but not the reserved (first) one.
			 * Copy mbuf info for sending.
			 */
			m2 = m_dup(m, M_NOWAIT);	/* XXX m_copypacket() */
			if (m2 == NULL) {
				link->stats.memoryFailures++;
				NG_FREE_ITEM(item);
				NG_FREE_M(m);
				return (ENOBUFS);
			}
		}

		/* Update stats */
		destLink->stats.xmitPackets++;
		destLink->stats.xmitOctets += m->m_pkthdr.len;
		switch (manycast) {
		case 0:					/* unicast */
			break;
		case 1:					/* multicast */
			destLink->stats.xmitMulticasts++;
			break;
		case 2:					/* broadcast */
			destLink->stats.xmitBroadcasts++;
			break;
		}

		/* Send packet */
		if (destLink == firstLink) { 
			/*
			 * If we've sent all the others, send the original
			 * on the first link we found.
			 */
			NG_FWD_NEW_DATA(error, item, destLink->hook, m);
			break; /* always done last - not really needed. */
		} else {
			NG_SEND_DATA_ONLY(error, destLink->hook, m2);
		}
	}
	return (error);
}

/*
 * Shutdown node
 */
static int
ng_bridge_shutdown(node_p node)
{
	const priv_p priv = NG_NODE_PRIVATE(node);

	/*
	 * Shut down everything including the timer.  Even if the
	 * callout has already been dequeued and is about to be
	 * run, ng_bridge_timeout() won't be fired as the node
	 * is already marked NGF_INVALID, so we're safe to free
	 * the node now.
	 */
	KASSERT(priv->numLinks == 0 && priv->numHosts == 0,
	    ("%s: numLinks=%d numHosts=%d",
	    __func__, priv->numLinks, priv->numHosts));
	ng_uncallout(&priv->timer, node);
	NG_NODE_SET_PRIVATE(node, NULL);
	NG_NODE_UNREF(node);
	free(priv->tab, M_NETGRAPH_BRIDGE);
	free(priv, M_NETGRAPH_BRIDGE);
	return (0);
}

/*
 * Hook disconnection.
 */
static int
ng_bridge_disconnect(hook_p hook)
{
	const priv_p priv = NG_NODE_PRIVATE(NG_HOOK_NODE(hook));
	int linkNum;

	/* Get link number */
	linkNum = (intptr_t)NG_HOOK_PRIVATE(hook);
	KASSERT(linkNum >= 0 && linkNum < NG_BRIDGE_MAX_LINKS,
	    ("%s: linkNum=%u", __func__, linkNum));

	/* Remove all hosts associated with this link */
	ng_bridge_remove_hosts(priv, linkNum);

	/* Free associated link information */
	KASSERT(priv->links[linkNum] != NULL, ("%s: no link", __func__));
	free(priv->links[linkNum], M_NETGRAPH_BRIDGE);
	priv->links[linkNum] = NULL;
	priv->numLinks--;

	/* If no more hooks, go away */
	if ((NG_NODE_NUMHOOKS(NG_HOOK_NODE(hook)) == 0)
	    && (NG_NODE_IS_VALID(NG_HOOK_NODE(hook)))
	    && !priv->persistent) {
		ng_rmnode_self(NG_HOOK_NODE(hook));
	}
	return (0);
}

/******************************************************************
		    HASH TABLE FUNCTIONS
******************************************************************/

/*
 * Hash algorithm
 */
#define HASH(addr,mask)		( (((const u_int16_t *)(addr))[0] 	\
				 ^ ((const u_int16_t *)(addr))[1] 	\
				 ^ ((const u_int16_t *)(addr))[2]) & (mask) )

/*
 * Find a host entry in the table.
 */
static struct ng_bridge_host *
ng_bridge_get(priv_p priv, const u_char *addr)
{
	const int bucket = HASH(addr, priv->hashMask);
	struct ng_bridge_hent *hent;

	SLIST_FOREACH(hent, &priv->tab[bucket], next) {
		if (ETHER_EQUAL(hent->host.addr, addr))
			return (&hent->host);
	}
	return (NULL);
}

/*
 * Add a new host entry to the table. This assumes the host doesn't
 * already exist in the table. Returns 1 on success, 0 if there
 * was a memory allocation failure.
 */
static int
ng_bridge_put(priv_p priv, const u_char *addr, int linkNum)
{
	const int bucket = HASH(addr, priv->hashMask);
	struct ng_bridge_hent *hent;

#ifdef INVARIANTS
	/* Assert that entry does not already exist in hashtable */
	SLIST_FOREACH(hent, &priv->tab[bucket], next) {
		KASSERT(!ETHER_EQUAL(hent->host.addr, addr),
		    ("%s: entry %6D exists in table", __func__, addr, ":"));
	}
#endif

	/* Allocate and initialize new hashtable entry */
	hent = malloc(sizeof(*hent), M_NETGRAPH_BRIDGE, M_NOWAIT);
	if (hent == NULL)
		return (0);
	bcopy(addr, hent->host.addr, ETHER_ADDR_LEN);
	hent->host.linkNum = linkNum;
	hent->host.staleness = 0;
	hent->host.age = 0;

	/* Add new element to hash bucket */
	SLIST_INSERT_HEAD(&priv->tab[bucket], hent, next);
	priv->numHosts++;

	/* Resize table if necessary */
	ng_bridge_rehash(priv);
	return (1);
}

/*
 * Resize the hash table. We try to maintain the number of buckets
 * such that the load factor is in the range 0.25 to 1.0.
 *
 * If we can't get the new memory then we silently fail. This is OK
 * because things will still work and we'll try again soon anyway.
 */
static void
ng_bridge_rehash(priv_p priv)
{
	struct ng_bridge_bucket *newTab;
	int oldBucket, newBucket;
	int newNumBuckets;
	u_int newMask;

	/* Is table too full or too empty? */
	if (priv->numHosts > priv->numBuckets
	    && (priv->numBuckets << 1) <= MAX_BUCKETS)
		newNumBuckets = priv->numBuckets << 1;
	else if (priv->numHosts < (priv->numBuckets >> 2)
	    && (priv->numBuckets >> 2) >= MIN_BUCKETS)
		newNumBuckets = priv->numBuckets >> 2;
	else
		return;
	newMask = newNumBuckets - 1;

	/* Allocate and initialize new table */
	newTab = malloc(newNumBuckets * sizeof(*newTab),
	    M_NETGRAPH_BRIDGE, M_NOWAIT | M_ZERO);
	if (newTab == NULL)
		return;

	/* Move all entries from old table to new table */
	for (oldBucket = 0; oldBucket < priv->numBuckets; oldBucket++) {
		struct ng_bridge_bucket *const oldList = &priv->tab[oldBucket];

		while (!SLIST_EMPTY(oldList)) {
			struct ng_bridge_hent *const hent
			    = SLIST_FIRST(oldList);

			SLIST_REMOVE_HEAD(oldList, next);
			newBucket = HASH(hent->host.addr, newMask);
			SLIST_INSERT_HEAD(&newTab[newBucket], hent, next);
		}
	}

	/* Replace old table with new one */
	if (priv->conf.debugLevel >= 3) {
		log(LOG_INFO, "ng_bridge: %s: table size %d -> %d\n",
		    ng_bridge_nodename(priv->node),
		    priv->numBuckets, newNumBuckets);
	}
	free(priv->tab, M_NETGRAPH_BRIDGE);
	priv->numBuckets = newNumBuckets;
	priv->hashMask = newMask;
	priv->tab = newTab;
	return;
}

/******************************************************************
		    MISC FUNCTIONS
******************************************************************/

/*
 * Remove all hosts associated with a specific link from the hashtable.
 * If linkNum == -1, then remove all hosts in the table.
 */
static void
ng_bridge_remove_hosts(priv_p priv, int linkNum)
{
	int bucket;

	for (bucket = 0; bucket < priv->numBuckets; bucket++) {
		struct ng_bridge_hent **hptr = &SLIST_FIRST(&priv->tab[bucket]);

		while (*hptr != NULL) {
			struct ng_bridge_hent *const hent = *hptr;

			if (linkNum == -1 || hent->host.linkNum == linkNum) {
				*hptr = SLIST_NEXT(hent, next);
				free(hent, M_NETGRAPH_BRIDGE);
				priv->numHosts--;
			} else
				hptr = &SLIST_NEXT(hent, next);
		}
	}
}

/*
 * Handle our once-per-second timeout event. We do two things:
 * we decrement link->loopCount for those links being muted due to
 * a detected loopback condition, and we remove any hosts from
 * the hashtable whom we haven't heard from in a long while.
 */
static void
ng_bridge_timeout(node_p node, hook_p hook, void *arg1, int arg2)
{
	const priv_p priv = NG_NODE_PRIVATE(node);
	int bucket;
	int counter = 0;
	int linkNum;

	/* Update host time counters and remove stale entries */
	for (bucket = 0; bucket < priv->numBuckets; bucket++) {
		struct ng_bridge_hent **hptr = &SLIST_FIRST(&priv->tab[bucket]);

		while (*hptr != NULL) {
			struct ng_bridge_hent *const hent = *hptr;

			/* Make sure host's link really exists */
			KASSERT(priv->links[hent->host.linkNum] != NULL,
			    ("%s: host %6D on nonexistent link %d\n",
			    __func__, hent->host.addr, ":",
			    hent->host.linkNum));

			/* Remove hosts we haven't heard from in a while */
			if (++hent->host.staleness >= priv->conf.maxStaleness) {
				*hptr = SLIST_NEXT(hent, next);
				free(hent, M_NETGRAPH_BRIDGE);
				priv->numHosts--;
			} else {
				if (hent->host.age < 0xffff)
					hent->host.age++;
				hptr = &SLIST_NEXT(hent, next);
				counter++;
			}
		}
	}
	KASSERT(priv->numHosts == counter,
	    ("%s: hosts: %d != %d", __func__, priv->numHosts, counter));

	/* Decrease table size if necessary */
	ng_bridge_rehash(priv);

	/* Decrease loop counter on muted looped back links */
	for (counter = linkNum = 0; linkNum < NG_BRIDGE_MAX_LINKS; linkNum++) {
		struct ng_bridge_link *const link = priv->links[linkNum];

		if (link != NULL) {
			if (link->loopCount != 0) {
				link->loopCount--;
				if (link->loopCount == 0
				    && priv->conf.debugLevel >= 2) {
					log(LOG_INFO, "ng_bridge: %s:"
					    " restoring looped back link%d\n",
					    ng_bridge_nodename(node), linkNum);
				}
			}
			counter++;
		}
	}
	KASSERT(priv->numLinks == counter,
	    ("%s: links: %d != %d", __func__, priv->numLinks, counter));

	/* Register a new timeout, keeping the existing node reference */
	ng_callout(&priv->timer, node, NULL, hz, ng_bridge_timeout, NULL, 0);
}

/*
 * Return node's "name", even if it doesn't have one.
 */
static const char *
ng_bridge_nodename(node_p node)
{
	static char name[NG_NODESIZ];

	if (NG_NODE_HAS_NAME(node))
		snprintf(name, sizeof(name), "%s", NG_NODE_NAME(node));
	else
		snprintf(name, sizeof(name), "[%x]", ng_node2ID(node));
	return name;
}

