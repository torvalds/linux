/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010-2011 Alexander V. Chernikov <melifaro@ipfw.ru>
 * Copyright (c) 2004-2005 Gleb Smirnoff <glebius@FreeBSD.org>
 * Copyright (c) 2001-2003 Roman V. Palagin <romanp@unshadow.net>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $SourceForge: ng_netflow.c,v 1.30 2004/09/05 11:37:43 glebius Exp $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet6.h"
#include "opt_route.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/counter.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/limits.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/syslog.h>
#include <sys/ctype.h>
#include <vm/uma.h>

#include <net/if.h>
#include <net/ethernet.h>
#include <net/route.h>
#include <net/if_arp.h>
#include <net/if_var.h>
#include <net/if_vlan_var.h>
#include <net/bpf.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <netinet/sctp.h>

#include <netgraph/ng_message.h>
#include <netgraph/ng_parse.h>
#include <netgraph/netgraph.h>
#include <netgraph/netflow/netflow.h>
#include <netgraph/netflow/netflow_v9.h>
#include <netgraph/netflow/ng_netflow.h>

/* Netgraph methods */
static ng_constructor_t	ng_netflow_constructor;
static ng_rcvmsg_t	ng_netflow_rcvmsg;
static ng_close_t	ng_netflow_close;
static ng_shutdown_t	ng_netflow_rmnode;
static ng_newhook_t	ng_netflow_newhook;
static ng_rcvdata_t	ng_netflow_rcvdata;
static ng_disconnect_t	ng_netflow_disconnect;

/* Parse type for struct ng_netflow_info */
static const struct ng_parse_struct_field ng_netflow_info_type_fields[]
	= NG_NETFLOW_INFO_TYPE;
static const struct ng_parse_type ng_netflow_info_type = {
	&ng_parse_struct_type,
	&ng_netflow_info_type_fields
};

/*  Parse type for struct ng_netflow_ifinfo */
static const struct ng_parse_struct_field ng_netflow_ifinfo_type_fields[]
	= NG_NETFLOW_IFINFO_TYPE;
static const struct ng_parse_type ng_netflow_ifinfo_type = {
	&ng_parse_struct_type,
	&ng_netflow_ifinfo_type_fields
};

/* Parse type for struct ng_netflow_setdlt */
static const struct ng_parse_struct_field ng_netflow_setdlt_type_fields[]
	= NG_NETFLOW_SETDLT_TYPE;
static const struct ng_parse_type ng_netflow_setdlt_type = {
	&ng_parse_struct_type,
	&ng_netflow_setdlt_type_fields
};

/* Parse type for ng_netflow_setifindex */
static const struct ng_parse_struct_field ng_netflow_setifindex_type_fields[]
	= NG_NETFLOW_SETIFINDEX_TYPE;
static const struct ng_parse_type ng_netflow_setifindex_type = {
	&ng_parse_struct_type,
	&ng_netflow_setifindex_type_fields
};

/* Parse type for ng_netflow_settimeouts */
static const struct ng_parse_struct_field ng_netflow_settimeouts_type_fields[]
	= NG_NETFLOW_SETTIMEOUTS_TYPE;
static const struct ng_parse_type ng_netflow_settimeouts_type = {
	&ng_parse_struct_type,
	&ng_netflow_settimeouts_type_fields
};

/* Parse type for ng_netflow_setconfig */
static const struct ng_parse_struct_field ng_netflow_setconfig_type_fields[]
	= NG_NETFLOW_SETCONFIG_TYPE;
static const struct ng_parse_type ng_netflow_setconfig_type = {
	&ng_parse_struct_type,
	&ng_netflow_setconfig_type_fields
};

/* Parse type for ng_netflow_settemplate */
static const struct ng_parse_struct_field ng_netflow_settemplate_type_fields[]
	= NG_NETFLOW_SETTEMPLATE_TYPE;
static const struct ng_parse_type ng_netflow_settemplate_type = {
	&ng_parse_struct_type,
	&ng_netflow_settemplate_type_fields
};

/* Parse type for ng_netflow_setmtu */
static const struct ng_parse_struct_field ng_netflow_setmtu_type_fields[]
	= NG_NETFLOW_SETMTU_TYPE;
static const struct ng_parse_type ng_netflow_setmtu_type = {
	&ng_parse_struct_type,
	&ng_netflow_setmtu_type_fields
};

/* Parse type for struct ng_netflow_v9info */
static const struct ng_parse_struct_field ng_netflow_v9info_type_fields[]
	= NG_NETFLOW_V9INFO_TYPE;
static const struct ng_parse_type ng_netflow_v9info_type = {
	&ng_parse_struct_type,
	&ng_netflow_v9info_type_fields
};

/* List of commands and how to convert arguments to/from ASCII */
static const struct ng_cmdlist ng_netflow_cmds[] = {
       {
	 NGM_NETFLOW_COOKIE,
	 NGM_NETFLOW_INFO,
	 "info",
	 NULL,
	 &ng_netflow_info_type
       },
       {
	NGM_NETFLOW_COOKIE,
	NGM_NETFLOW_IFINFO,
	"ifinfo",
	&ng_parse_uint16_type,
	&ng_netflow_ifinfo_type
       },
       {
	NGM_NETFLOW_COOKIE,
	NGM_NETFLOW_SETDLT,
	"setdlt",
	&ng_netflow_setdlt_type,
	NULL
       },
       {
	NGM_NETFLOW_COOKIE,
	NGM_NETFLOW_SETIFINDEX,
	"setifindex",
	&ng_netflow_setifindex_type,
	NULL
       },
       {
	NGM_NETFLOW_COOKIE,
	NGM_NETFLOW_SETTIMEOUTS,
	"settimeouts",
	&ng_netflow_settimeouts_type,
	NULL
       },
       {
	NGM_NETFLOW_COOKIE,
	NGM_NETFLOW_SETCONFIG,
	"setconfig",
	&ng_netflow_setconfig_type,
	NULL
       },
       {
	NGM_NETFLOW_COOKIE,
	NGM_NETFLOW_SETTEMPLATE,
	"settemplate",
	&ng_netflow_settemplate_type,
	NULL
       },
       {
	NGM_NETFLOW_COOKIE,
	NGM_NETFLOW_SETMTU,
	"setmtu",
	&ng_netflow_setmtu_type,
	NULL
       },
       {
	 NGM_NETFLOW_COOKIE,
	 NGM_NETFLOW_V9INFO,
	 "v9info",
	 NULL,
	 &ng_netflow_v9info_type
       },
       { 0 }
};


/* Netgraph node type descriptor */
static struct ng_type ng_netflow_typestruct = {
	.version =	NG_ABI_VERSION,
	.name =		NG_NETFLOW_NODE_TYPE,
	.constructor =	ng_netflow_constructor,
	.rcvmsg =	ng_netflow_rcvmsg,
	.close =	ng_netflow_close,
	.shutdown =	ng_netflow_rmnode,
	.newhook =	ng_netflow_newhook,
	.rcvdata =	ng_netflow_rcvdata,
	.disconnect =	ng_netflow_disconnect,
	.cmdlist =	ng_netflow_cmds,
};
NETGRAPH_INIT(netflow, &ng_netflow_typestruct);

/* Called at node creation */
static int
ng_netflow_constructor(node_p node)
{
	priv_p priv;
	int i;

	/* Initialize private data */
	priv = malloc(sizeof(*priv), M_NETGRAPH, M_WAITOK | M_ZERO);

	/* Initialize fib data */
	priv->maxfibs = rt_numfibs;
	priv->fib_data = malloc(sizeof(fib_export_p) * priv->maxfibs,
	    M_NETGRAPH, M_WAITOK | M_ZERO);

	/* Make node and its data point at each other */
	NG_NODE_SET_PRIVATE(node, priv);
	priv->node = node;

	/* Initialize timeouts to default values */
	priv->nfinfo_inact_t = INACTIVE_TIMEOUT;
	priv->nfinfo_act_t = ACTIVE_TIMEOUT;

	/* Set default config */
	for (i = 0; i < NG_NETFLOW_MAXIFACES; i++)
		priv->ifaces[i].info.conf = NG_NETFLOW_CONF_INGRESS;

	/* Initialize callout handle */
	callout_init(&priv->exp_callout, 1);

	/* Allocate memory and set up flow cache */
	ng_netflow_cache_init(priv);

	return (0);
}

/*
 * ng_netflow supports two hooks: data and export.
 * Incoming traffic is expected on data, and expired
 * netflow datagrams are sent to export.
 */
static int
ng_netflow_newhook(node_p node, hook_p hook, const char *name)
{
	const priv_p priv = NG_NODE_PRIVATE(node);

	if (strncmp(name, NG_NETFLOW_HOOK_DATA,	/* an iface hook? */
	    strlen(NG_NETFLOW_HOOK_DATA)) == 0) {
		iface_p iface;
		int ifnum = -1;
		const char *cp;
		char *eptr;

		cp = name + strlen(NG_NETFLOW_HOOK_DATA);
		if (!isdigit(*cp) || (cp[0] == '0' && cp[1] != '\0'))
			return (EINVAL);

		ifnum = (int)strtoul(cp, &eptr, 10);
		if (*eptr != '\0' || ifnum < 0 || ifnum >= NG_NETFLOW_MAXIFACES)
			return (EINVAL);

		/* See if hook is already connected */
		if (priv->ifaces[ifnum].hook != NULL)
			return (EISCONN);

		iface = &priv->ifaces[ifnum];

		/* Link private info and hook together */
		NG_HOOK_SET_PRIVATE(hook, iface);
		iface->hook = hook;

		/*
		 * In most cases traffic accounting is done on an
		 * Ethernet interface, so default data link type
		 * will be DLT_EN10MB.
		 */
		iface->info.ifinfo_dlt = DLT_EN10MB;

	} else if (strncmp(name, NG_NETFLOW_HOOK_OUT,
	    strlen(NG_NETFLOW_HOOK_OUT)) == 0) {
		iface_p iface;
		int ifnum = -1;
		const char *cp;
		char *eptr;

		cp = name + strlen(NG_NETFLOW_HOOK_OUT);
		if (!isdigit(*cp) || (cp[0] == '0' && cp[1] != '\0'))
			return (EINVAL);

		ifnum = (int)strtoul(cp, &eptr, 10);
		if (*eptr != '\0' || ifnum < 0 || ifnum >= NG_NETFLOW_MAXIFACES)
			return (EINVAL);

		/* See if hook is already connected */
		if (priv->ifaces[ifnum].out != NULL)
			return (EISCONN);

		iface = &priv->ifaces[ifnum];

		/* Link private info and hook together */
		NG_HOOK_SET_PRIVATE(hook, iface);
		iface->out = hook;

	} else if (strcmp(name, NG_NETFLOW_HOOK_EXPORT) == 0) {

		if (priv->export != NULL)
			return (EISCONN);

		/* Netflow version 5 supports 32-bit counters only */
		if (CNTR_MAX == UINT64_MAX)
			return (EINVAL);

		priv->export = hook;

		/* Exporter is ready. Let's schedule expiry. */
		callout_reset(&priv->exp_callout, (1*hz), &ng_netflow_expire,
		    (void *)priv);
	} else if (strcmp(name, NG_NETFLOW_HOOK_EXPORT9) == 0) {

		if (priv->export9 != NULL)
			return (EISCONN);

		priv->export9 = hook;

		/* Exporter is ready. Let's schedule expiry. */
		callout_reset(&priv->exp_callout, (1*hz), &ng_netflow_expire,
		    (void *)priv);
	} else
		return (EINVAL);

	return (0);
}

/* Get a netgraph control message. */
static int
ng_netflow_rcvmsg (node_p node, item_p item, hook_p lasthook)
{
	const priv_p priv = NG_NODE_PRIVATE(node);
	struct ng_mesg *resp = NULL;
	int error = 0;
	struct ng_mesg *msg;

	NGI_GET_MSG(item, msg);

	/* Deal with message according to cookie and command */
	switch (msg->header.typecookie) {
	case NGM_NETFLOW_COOKIE:
		switch (msg->header.cmd) {
		case NGM_NETFLOW_INFO:
		    {
			struct ng_netflow_info *i;

			NG_MKRESPONSE(resp, msg, sizeof(struct ng_netflow_info),
			    M_NOWAIT);
			i = (struct ng_netflow_info *)resp->data;
			ng_netflow_copyinfo(priv, i);

			break;
		    }
		case NGM_NETFLOW_IFINFO:
		    {
			struct ng_netflow_ifinfo *i;
			const uint16_t *index;

			if (msg->header.arglen != sizeof(uint16_t))
				 ERROUT(EINVAL);

			index  = (uint16_t *)msg->data;
			if (*index >= NG_NETFLOW_MAXIFACES)
				ERROUT(EINVAL);

			/* connected iface? */
			if (priv->ifaces[*index].hook == NULL)
				 ERROUT(EINVAL);

			NG_MKRESPONSE(resp, msg,
			     sizeof(struct ng_netflow_ifinfo), M_NOWAIT);
			i = (struct ng_netflow_ifinfo *)resp->data;
			memcpy((void *)i, (void *)&priv->ifaces[*index].info,
			    sizeof(priv->ifaces[*index].info));

			break;
		    }
		case NGM_NETFLOW_SETDLT:
		    {
			struct ng_netflow_setdlt *set;
			struct ng_netflow_iface *iface;

			if (msg->header.arglen !=
			    sizeof(struct ng_netflow_setdlt))
				ERROUT(EINVAL);

			set = (struct ng_netflow_setdlt *)msg->data;
			if (set->iface >= NG_NETFLOW_MAXIFACES)
				ERROUT(EINVAL);
			iface = &priv->ifaces[set->iface];

			/* connected iface? */
			if (iface->hook == NULL)
				ERROUT(EINVAL);

			switch (set->dlt) {
			case	DLT_EN10MB:
				iface->info.ifinfo_dlt = DLT_EN10MB;
				break;
			case	DLT_RAW:
				iface->info.ifinfo_dlt = DLT_RAW;
				break;
			default:
				ERROUT(EINVAL);
			}
			break;
		    }
		case NGM_NETFLOW_SETIFINDEX:
		    {
			struct ng_netflow_setifindex *set;
			struct ng_netflow_iface *iface;

			if (msg->header.arglen !=
			    sizeof(struct ng_netflow_setifindex))
				ERROUT(EINVAL);

			set = (struct ng_netflow_setifindex *)msg->data;
			if (set->iface >= NG_NETFLOW_MAXIFACES)
				ERROUT(EINVAL);
			iface = &priv->ifaces[set->iface];

			/* connected iface? */
			if (iface->hook == NULL)
				ERROUT(EINVAL);

			iface->info.ifinfo_index = set->index;

			break;
		    }
		case NGM_NETFLOW_SETTIMEOUTS:
		    {
			struct ng_netflow_settimeouts *set;

			if (msg->header.arglen !=
			    sizeof(struct ng_netflow_settimeouts))
				ERROUT(EINVAL);

			set = (struct ng_netflow_settimeouts *)msg->data;

			priv->nfinfo_inact_t = set->inactive_timeout;
			priv->nfinfo_act_t = set->active_timeout;

			break;
		    }
		case NGM_NETFLOW_SETCONFIG:
		    {
			struct ng_netflow_setconfig *set;

			if (msg->header.arglen !=
			    sizeof(struct ng_netflow_setconfig))
				ERROUT(EINVAL);

			set = (struct ng_netflow_setconfig *)msg->data;

			if (set->iface >= NG_NETFLOW_MAXIFACES)
				ERROUT(EINVAL);
			
			priv->ifaces[set->iface].info.conf = set->conf;
	
			break;
		    }
		case NGM_NETFLOW_SETTEMPLATE:
		    {
			struct ng_netflow_settemplate *set;

			if (msg->header.arglen !=
			    sizeof(struct ng_netflow_settemplate))
				ERROUT(EINVAL);

			set = (struct ng_netflow_settemplate *)msg->data;

			priv->templ_packets = set->packets;
			priv->templ_time = set->time;

			break;
		    }
		case NGM_NETFLOW_SETMTU:
		    {
			struct ng_netflow_setmtu *set;

			if (msg->header.arglen !=
			    sizeof(struct ng_netflow_setmtu))
				ERROUT(EINVAL);

			set = (struct ng_netflow_setmtu *)msg->data;
			if ((set->mtu < MIN_MTU) || (set->mtu > MAX_MTU))
				ERROUT(EINVAL);

			priv->mtu = set->mtu;

			break;
		    }
		case NGM_NETFLOW_SHOW:
			if (msg->header.arglen !=
			    sizeof(struct ngnf_show_header))
				ERROUT(EINVAL);

			NG_MKRESPONSE(resp, msg, NGRESP_SIZE, M_NOWAIT);

			if (!resp)
				ERROUT(ENOMEM);

			error = ng_netflow_flow_show(priv,
			    (struct ngnf_show_header *)msg->data,
			    (struct ngnf_show_header *)resp->data);

			if (error)
				NG_FREE_MSG(resp);

			break;
		case NGM_NETFLOW_V9INFO:
		    {
			struct ng_netflow_v9info *i;

			NG_MKRESPONSE(resp, msg,
			    sizeof(struct ng_netflow_v9info), M_NOWAIT);
			i = (struct ng_netflow_v9info *)resp->data;
			ng_netflow_copyv9info(priv, i);

			break;
		    }
		default:
			ERROUT(EINVAL);		/* unknown command */
			break;
		}
		break;
	default:
		ERROUT(EINVAL);		/* incorrect cookie */
		break;
	}

	/*
	 * Take care of synchronous response, if any.
	 * Free memory and return.
	 */
done:
	NG_RESPOND_MSG(error, node, item, resp);
	NG_FREE_MSG(msg);

	return (error);
}

/* Receive data on hook. */
static int
ng_netflow_rcvdata (hook_p hook, item_p item)
{
	const node_p node = NG_HOOK_NODE(hook);
	const priv_p priv = NG_NODE_PRIVATE(node);
	const iface_p iface = NG_HOOK_PRIVATE(hook);
	hook_p out;
	struct mbuf *m = NULL, *m_old = NULL;
	struct ip *ip = NULL;
	struct ip6_hdr *ip6 = NULL;
	struct m_tag *mtag;
	int pullup_len = 0, off;
	uint8_t acct = 0, bypass = 0, flags = 0, upper_proto = 0;
	int error = 0, l3_off = 0;
	unsigned int src_if_index;
	caddr_t upper_ptr = NULL;
	fib_export_p fe;	
	uint32_t fib;

	if ((hook == priv->export) || (hook == priv->export9)) {
		/*
		 * Data arrived on export hook.
		 * This must not happen.
		 */
		log(LOG_ERR, "ng_netflow: incoming data on export hook!\n");
		ERROUT(EINVAL);
	}

	if (hook == iface->hook) {
		if ((iface->info.conf & NG_NETFLOW_CONF_INGRESS) == 0)
			bypass = 1;
		out = iface->out;
	} else if (hook == iface->out) {
		if ((iface->info.conf & NG_NETFLOW_CONF_EGRESS) == 0)
			bypass = 1;
		out = iface->hook;
	} else
		ERROUT(EINVAL);

	if ((!bypass) && (iface->info.conf &
	    (NG_NETFLOW_CONF_ONCE | NG_NETFLOW_CONF_THISONCE))) {
		mtag = m_tag_locate(NGI_M(item), MTAG_NETFLOW,
		    MTAG_NETFLOW_CALLED, NULL);
		while (mtag != NULL) {
			if ((iface->info.conf & NG_NETFLOW_CONF_ONCE) ||
			    ((ng_ID_t *)(mtag + 1))[0] == NG_NODE_ID(node)) {
				bypass = 1;
				break;
			}
			mtag = m_tag_locate(NGI_M(item), MTAG_NETFLOW,
			    MTAG_NETFLOW_CALLED, mtag);
		}
	}
	
	if (bypass) {
		if (out == NULL)
			ERROUT(ENOTCONN);

		NG_FWD_ITEM_HOOK(error, item, out);
		return (error);
	}
	
	if (iface->info.conf &
	    (NG_NETFLOW_CONF_ONCE | NG_NETFLOW_CONF_THISONCE)) {
		mtag = m_tag_alloc(MTAG_NETFLOW, MTAG_NETFLOW_CALLED,
		    sizeof(ng_ID_t), M_NOWAIT);
		if (mtag) {
			((ng_ID_t *)(mtag + 1))[0] = NG_NODE_ID(node);
			m_tag_prepend(NGI_M(item), mtag);
		}
	}

	/* Import configuration flags related to flow creation */
	flags = iface->info.conf & NG_NETFLOW_FLOW_FLAGS;

	NGI_GET_M(item, m);
	m_old = m;

	/* Increase counters. */
	iface->info.ifinfo_packets++;

	/*
	 * Depending on interface data link type and packet contents
	 * we pullup enough data, so that ng_netflow_flow_add() does not
	 * need to know about mbuf at all. We keep current length of data
	 * needed to be contiguous in pullup_len. mtod() is done at the
	 * very end one more time, since m can had changed after pulluping.
	 *
	 * In case of unrecognized data we don't return error, but just
	 * pass data to downstream hook, if it is available.
	 */

#define	M_CHECK(length)	do {					\
	pullup_len += length;					\
	if (((m)->m_pkthdr.len < (pullup_len)) ||		\
	   ((pullup_len) > MHLEN)) {				\
		error = EINVAL;					\
		goto bypass;					\
	} 							\
	if ((m)->m_len < (pullup_len) &&			\
	   (((m) = m_pullup((m),(pullup_len))) == NULL)) {	\
		error = ENOBUFS;				\
		goto done;					\
	}							\
} while (0)

	switch (iface->info.ifinfo_dlt) {
	case DLT_EN10MB:	/* Ethernet */
	    {
		struct ether_header *eh;
		uint16_t etype;

		M_CHECK(sizeof(struct ether_header));
		eh = mtod(m, struct ether_header *);

		/* Make sure this is IP frame. */
		etype = ntohs(eh->ether_type);
		switch (etype) {
		case ETHERTYPE_IP:
			M_CHECK(sizeof(struct ip));
			eh = mtod(m, struct ether_header *);
			ip = (struct ip *)(eh + 1);
			l3_off = sizeof(struct ether_header);
			break;
#ifdef INET6
		case ETHERTYPE_IPV6:
			/*
			 * m_pullup() called by M_CHECK() pullups
			 * kern.ipc.max_protohdr (default 60 bytes)
			 * which is enough.
			 */
			M_CHECK(sizeof(struct ip6_hdr));
			eh = mtod(m, struct ether_header *);
			ip6 = (struct ip6_hdr *)(eh + 1);
			l3_off = sizeof(struct ether_header);
			break;
#endif
		case ETHERTYPE_VLAN:
		    {
			struct ether_vlan_header *evh;

			M_CHECK(sizeof(struct ether_vlan_header) -
			    sizeof(struct ether_header));
			evh = mtod(m, struct ether_vlan_header *);
			etype = ntohs(evh->evl_proto);
			l3_off = sizeof(struct ether_vlan_header);

			if (etype == ETHERTYPE_IP) {
				M_CHECK(sizeof(struct ip));
				ip = (struct ip *)(evh + 1);
				break;
#ifdef INET6
			} else if (etype == ETHERTYPE_IPV6) {
				M_CHECK(sizeof(struct ip6_hdr));
				ip6 = (struct ip6_hdr *)(evh + 1);
				break;
#endif
			}
		    }
		default:
			goto bypass;	/* pass this frame */
		}
		break;
	    }
	case DLT_RAW:		/* IP packets */
		M_CHECK(sizeof(struct ip));
		ip = mtod(m, struct ip *);
		/* l3_off is already zero */
#ifdef INET6
		/*
		 * If INET6 is not defined IPv6 packets
		 * will be discarded in ng_netflow_flow_add().
		 */
		if (ip->ip_v == IP6VERSION) {
			ip = NULL;
			M_CHECK(sizeof(struct ip6_hdr) - sizeof(struct ip));
			ip6 = mtod(m, struct ip6_hdr *);
		}
#endif
		break;
	default:
		goto bypass;
		break;
	}

	off = pullup_len;

	if ((ip != NULL) && ((ip->ip_off & htons(IP_OFFMASK)) == 0)) {
		if ((ip->ip_v != IPVERSION) ||
		    ((ip->ip_hl << 2) < sizeof(struct ip)))
			goto bypass;
		/*
		 * In case of IPv4 header with options, we haven't pulled
		 * up enough, yet.
		 */
		M_CHECK((ip->ip_hl << 2) - sizeof(struct ip));

		/* Save upper layer offset and proto */
		off = pullup_len;
		upper_proto = ip->ip_p;

		/*
		 * XXX: in case of wrong upper layer header we will
		 * forward this packet but skip this record in netflow.
		 */
		switch (ip->ip_p) {
		case IPPROTO_TCP:
			M_CHECK(sizeof(struct tcphdr));
			break;
		case IPPROTO_UDP:
			M_CHECK(sizeof(struct udphdr));
			break;
		case IPPROTO_SCTP:
			M_CHECK(sizeof(struct sctphdr));
			break;
		}
	} else if (ip != NULL) {
		/*
		 * Nothing to save except upper layer proto,
		 * since this is a packet fragment.
		 */
		flags |= NG_NETFLOW_IS_FRAG;
		upper_proto = ip->ip_p;
		if ((ip->ip_v != IPVERSION) ||
		    ((ip->ip_hl << 2) < sizeof(struct ip)))
			goto bypass;
#ifdef INET6
	} else if (ip6 != NULL) {
		int cur = ip6->ip6_nxt, hdr_off = 0;
		struct ip6_ext *ip6e;
		struct ip6_frag *ip6f;

		if (priv->export9 == NULL)
			goto bypass;

		/* Save upper layer info. */
		off = pullup_len;
		upper_proto = cur;

		if ((ip6->ip6_vfc & IPV6_VERSION_MASK) != IPV6_VERSION)
			goto bypass;

		/*
		 * Loop through IPv6 extended headers to get upper
		 * layer header / frag.
		 */
		for (;;) {
			switch (cur) {
			/*
			 * Same as in IPv4, we can forward a 'bad'
			 * packet without accounting.
			 */
			case IPPROTO_TCP:
				M_CHECK(sizeof(struct tcphdr));
				goto loopend;
			case IPPROTO_UDP:
				M_CHECK(sizeof(struct udphdr));
				goto loopend;
			case IPPROTO_SCTP:
				M_CHECK(sizeof(struct sctphdr));
				goto loopend;

			/* Loop until 'real' upper layer headers */
			case IPPROTO_HOPOPTS:
			case IPPROTO_ROUTING:
			case IPPROTO_DSTOPTS:
				M_CHECK(sizeof(struct ip6_ext));
				ip6e = (struct ip6_ext *)(mtod(m, caddr_t) +
				    off);
				upper_proto = ip6e->ip6e_nxt;
				hdr_off = (ip6e->ip6e_len + 1) << 3;
				break;

			/* RFC4302, can be before DSTOPTS */
			case IPPROTO_AH:
				M_CHECK(sizeof(struct ip6_ext));
				ip6e = (struct ip6_ext *)(mtod(m, caddr_t) +
				    off);
				upper_proto = ip6e->ip6e_nxt;
				hdr_off = (ip6e->ip6e_len + 2) << 2;
				break;

			case IPPROTO_FRAGMENT:
				M_CHECK(sizeof(struct ip6_frag));
				ip6f = (struct ip6_frag *)(mtod(m, caddr_t) +
				    off);
				upper_proto = ip6f->ip6f_nxt;
				hdr_off = sizeof(struct ip6_frag);
				off += hdr_off;
				flags |= NG_NETFLOW_IS_FRAG;
				goto loopend;

#if 0				
			case IPPROTO_NONE:
				goto loopend;
#endif
			/*
			 * Any unknown header (new extension or IPv6/IPv4
			 * header for tunnels) ends loop.
			 */
			default:
				goto loopend;
			}

			off += hdr_off;
			cur = upper_proto;
		}
#endif
	}
#undef	M_CHECK

#ifdef INET6
loopend:
#endif
	/* Just in case of real reallocation in M_CHECK() / m_pullup() */
	if (m != m_old) {
		priv->nfinfo_realloc_mbuf++;
		/* Restore ip/ipv6 pointer */
		if (ip != NULL)
			ip = (struct ip *)(mtod(m, caddr_t) + l3_off);
		else if (ip6 != NULL)
			ip6 = (struct ip6_hdr *)(mtod(m, caddr_t) + l3_off);
 	}

	upper_ptr = (caddr_t)(mtod(m, caddr_t) + off);

	/* Determine packet input interface. Prefer configured. */
	src_if_index = 0;
	if (hook == iface->out || iface->info.ifinfo_index == 0) {
		if (m->m_pkthdr.rcvif != NULL)
			src_if_index = m->m_pkthdr.rcvif->if_index;
	} else
		src_if_index = iface->info.ifinfo_index;
	
	/* Check packet FIB */
	fib = M_GETFIB(m);
	if (fib >= priv->maxfibs) {
		CTR2(KTR_NET, "ng_netflow_rcvdata(): packet fib %d is out of "
		    "range of available fibs: 0 .. %d",
		    fib, priv->maxfibs);
		goto bypass;
	}

	if ((fe = priv_to_fib(priv, fib)) == NULL) {
		/* Setup new FIB */
		if (ng_netflow_fib_init(priv, fib) != 0) {
			/* malloc() failed */
			goto bypass;
		}

		fe = priv_to_fib(priv, fib);
	}

	if (ip != NULL)
		error = ng_netflow_flow_add(priv, fe, ip, upper_ptr,
		    upper_proto, flags, src_if_index);
#ifdef INET6		
	else if (ip6 != NULL)
		error = ng_netflow_flow6_add(priv, fe, ip6, upper_ptr,
		    upper_proto, flags, src_if_index);
#endif
	else
		goto bypass;
	
	acct = 1;
bypass:
	if (out != NULL) {
		if (acct == 0) {
			/* Accounting failure */
			if (ip != NULL) {
				counter_u64_add(priv->nfinfo_spackets, 1);
				counter_u64_add(priv->nfinfo_sbytes,
				    m->m_pkthdr.len);
			} else if (ip6 != NULL) {
				counter_u64_add(priv->nfinfo_spackets6, 1);
				counter_u64_add(priv->nfinfo_sbytes6,
				    m->m_pkthdr.len);
			}
		}

		/* XXX: error gets overwritten here */
		NG_FWD_NEW_DATA(error, item, out, m);
		return (error);
	}
done:
	if (item)
		NG_FREE_ITEM(item);
	if (m)
		NG_FREE_M(m);

	return (error);	
}

/* We will be shut down in a moment */
static int
ng_netflow_close(node_p node)
{
	const priv_p priv = NG_NODE_PRIVATE(node);

	callout_drain(&priv->exp_callout);
	ng_netflow_cache_flush(priv);

	return (0);
}

/* Do local shutdown processing. */
static int
ng_netflow_rmnode(node_p node)
{
	const priv_p priv = NG_NODE_PRIVATE(node);

	NG_NODE_SET_PRIVATE(node, NULL);
	NG_NODE_UNREF(priv->node);

	free(priv->fib_data, M_NETGRAPH);
	free(priv, M_NETGRAPH);

	return (0);
}

/* Hook disconnection. */
static int
ng_netflow_disconnect(hook_p hook)
{
	node_p node = NG_HOOK_NODE(hook);
	priv_p priv = NG_NODE_PRIVATE(node);
	iface_p iface = NG_HOOK_PRIVATE(hook);

	if (iface != NULL) {
		if (iface->hook == hook)
			iface->hook = NULL;
		if (iface->out == hook)
			iface->out = NULL;
	}

	/* if export hook disconnected stop running expire(). */
	if (hook == priv->export) {
		if (priv->export9 == NULL)
			callout_drain(&priv->exp_callout);
		priv->export = NULL;
	}

	if (hook == priv->export9) {
		if (priv->export == NULL)
			callout_drain(&priv->exp_callout);
		priv->export9 = NULL;
	}

	/* Removal of the last link destroys the node. */
	if (NG_NODE_NUMHOOKS(node) == 0)
		ng_rmnode_self(node);

	return (0);
}
