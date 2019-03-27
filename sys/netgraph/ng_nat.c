/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 2005, Gleb Smirnoff <glebius@FreeBSD.org>
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
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/ctype.h>
#include <sys/errno.h>
#include <sys/syslog.h>

#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/tcp.h>
#include <machine/in_cksum.h>

#include <net/dlt.h>
#include <net/ethernet.h>

#include <netinet/libalias/alias.h>
#include <netinet/libalias/alias_local.h>

#include <netgraph/ng_message.h>
#include <netgraph/ng_parse.h>
#include <netgraph/ng_nat.h>
#include <netgraph/netgraph.h>

static ng_constructor_t	ng_nat_constructor;
static ng_rcvmsg_t	ng_nat_rcvmsg;
static ng_shutdown_t	ng_nat_shutdown;
static ng_newhook_t	ng_nat_newhook;
static ng_rcvdata_t	ng_nat_rcvdata;
static ng_disconnect_t	ng_nat_disconnect;

static unsigned int	ng_nat_translate_flags(unsigned int x);

/* Parse type for struct ng_nat_mode. */
static const struct ng_parse_struct_field ng_nat_mode_fields[]
	= NG_NAT_MODE_INFO;
static const struct ng_parse_type ng_nat_mode_type = {
	&ng_parse_struct_type,
	&ng_nat_mode_fields
};

/* Parse type for 'description' field in structs. */
static const struct ng_parse_fixedstring_info ng_nat_description_info
	= { NG_NAT_DESC_LENGTH };
static const struct ng_parse_type ng_nat_description_type = {
	&ng_parse_fixedstring_type,
	&ng_nat_description_info
};

/* Parse type for struct ng_nat_redirect_port. */
static const struct ng_parse_struct_field ng_nat_redirect_port_fields[]
	= NG_NAT_REDIRECT_PORT_TYPE_INFO(&ng_nat_description_type);
static const struct ng_parse_type ng_nat_redirect_port_type = {
	&ng_parse_struct_type,
	&ng_nat_redirect_port_fields
};

/* Parse type for struct ng_nat_redirect_addr. */
static const struct ng_parse_struct_field ng_nat_redirect_addr_fields[]
	= NG_NAT_REDIRECT_ADDR_TYPE_INFO(&ng_nat_description_type);
static const struct ng_parse_type ng_nat_redirect_addr_type = {
	&ng_parse_struct_type,
	&ng_nat_redirect_addr_fields
};

/* Parse type for struct ng_nat_redirect_proto. */
static const struct ng_parse_struct_field ng_nat_redirect_proto_fields[]
	= NG_NAT_REDIRECT_PROTO_TYPE_INFO(&ng_nat_description_type);
static const struct ng_parse_type ng_nat_redirect_proto_type = {
	&ng_parse_struct_type,
	&ng_nat_redirect_proto_fields
};

/* Parse type for struct ng_nat_add_server. */
static const struct ng_parse_struct_field ng_nat_add_server_fields[]
	= NG_NAT_ADD_SERVER_TYPE_INFO;
static const struct ng_parse_type ng_nat_add_server_type = {
	&ng_parse_struct_type,
	&ng_nat_add_server_fields
};

/* Parse type for one struct ng_nat_listrdrs_entry. */
static const struct ng_parse_struct_field ng_nat_listrdrs_entry_fields[]
	= NG_NAT_LISTRDRS_ENTRY_TYPE_INFO(&ng_nat_description_type);
static const struct ng_parse_type ng_nat_listrdrs_entry_type = {
	&ng_parse_struct_type,
	&ng_nat_listrdrs_entry_fields
};

/* Parse type for 'redirects' array in struct ng_nat_list_redirects. */
static int
ng_nat_listrdrs_ary_getLength(const struct ng_parse_type *type,
	const u_char *start, const u_char *buf)
{
	const struct ng_nat_list_redirects *lr;

	lr = (const struct ng_nat_list_redirects *)
	    (buf - offsetof(struct ng_nat_list_redirects, redirects));
	return lr->total_count;
}

static const struct ng_parse_array_info ng_nat_listrdrs_ary_info = {
	&ng_nat_listrdrs_entry_type,
	&ng_nat_listrdrs_ary_getLength,
	NULL
};
static const struct ng_parse_type ng_nat_listrdrs_ary_type = {
	&ng_parse_array_type,
	&ng_nat_listrdrs_ary_info
};

/* Parse type for struct ng_nat_list_redirects. */
static const struct ng_parse_struct_field ng_nat_list_redirects_fields[]
	= NG_NAT_LIST_REDIRECTS_TYPE_INFO(&ng_nat_listrdrs_ary_type);
static const struct ng_parse_type ng_nat_list_redirects_type = {
	&ng_parse_struct_type,
	&ng_nat_list_redirects_fields
};

/* Parse type for struct ng_nat_libalias_info. */
static const struct ng_parse_struct_field ng_nat_libalias_info_fields[]
	= NG_NAT_LIBALIAS_INFO;
static const struct ng_parse_type ng_nat_libalias_info_type = {
	&ng_parse_struct_type,
	&ng_nat_libalias_info_fields
};

/* List of commands and how to convert arguments to/from ASCII. */
static const struct ng_cmdlist ng_nat_cmdlist[] = {
	{
	  NGM_NAT_COOKIE,
	  NGM_NAT_SET_IPADDR,
	  "setaliasaddr",
	  &ng_parse_ipaddr_type,
	  NULL
	},
	{
	  NGM_NAT_COOKIE,
	  NGM_NAT_SET_MODE,
	  "setmode",
	  &ng_nat_mode_type,
	  NULL
	},
	{
	  NGM_NAT_COOKIE,
	  NGM_NAT_SET_TARGET,
	  "settarget",
	  &ng_parse_ipaddr_type,
	  NULL
	},
	{
	  NGM_NAT_COOKIE,
	  NGM_NAT_REDIRECT_PORT,
	  "redirectport",
	  &ng_nat_redirect_port_type,
	  &ng_parse_uint32_type
	},
	{
	  NGM_NAT_COOKIE,
	  NGM_NAT_REDIRECT_ADDR,
	  "redirectaddr",
	  &ng_nat_redirect_addr_type,
	  &ng_parse_uint32_type
	},
	{
	  NGM_NAT_COOKIE,
	  NGM_NAT_REDIRECT_PROTO,
	  "redirectproto",
	  &ng_nat_redirect_proto_type,
	  &ng_parse_uint32_type
	},
	{
	  NGM_NAT_COOKIE,
	  NGM_NAT_REDIRECT_DYNAMIC,
	  "redirectdynamic",
	  &ng_parse_uint32_type,
	  NULL
	},
	{
	  NGM_NAT_COOKIE,
	  NGM_NAT_REDIRECT_DELETE,
	  "redirectdelete",
	  &ng_parse_uint32_type,
	  NULL
	},
	{
	  NGM_NAT_COOKIE,
	  NGM_NAT_ADD_SERVER,
	  "addserver",
	  &ng_nat_add_server_type,
	  NULL
	},
	{
	  NGM_NAT_COOKIE,
	  NGM_NAT_LIST_REDIRECTS,
	  "listredirects",
	  NULL,
	  &ng_nat_list_redirects_type
	},
	{
	  NGM_NAT_COOKIE,
	  NGM_NAT_PROXY_RULE,
	  "proxyrule",
	  &ng_parse_string_type,
	  NULL
	},
	{
	  NGM_NAT_COOKIE,
	  NGM_NAT_LIBALIAS_INFO,
	  "libaliasinfo",
	  NULL,
	  &ng_nat_libalias_info_type
	},
	{
	  NGM_NAT_COOKIE,
	  NGM_NAT_SET_DLT,
	  "setdlt",
	  &ng_parse_uint8_type,
	  NULL
	},
	{
	  NGM_NAT_COOKIE,
	  NGM_NAT_GET_DLT,
	  "getdlt",
	  NULL,
	  &ng_parse_uint8_type
	},
	{ 0 }
};

/* Netgraph node type descriptor. */
static struct ng_type typestruct = {
	.version =	NG_ABI_VERSION,
	.name =		NG_NAT_NODE_TYPE,
	.constructor =	ng_nat_constructor,
	.rcvmsg =	ng_nat_rcvmsg,
	.shutdown =	ng_nat_shutdown,
	.newhook =	ng_nat_newhook,
	.rcvdata =	ng_nat_rcvdata,
	.disconnect =	ng_nat_disconnect,
	.cmdlist =	ng_nat_cmdlist,
};
NETGRAPH_INIT(nat, &typestruct);
MODULE_DEPEND(ng_nat, libalias, 1, 1, 1);

/* Element for list of redirects. */
struct ng_nat_rdr_lst {
	STAILQ_ENTRY(ng_nat_rdr_lst) entries;
	struct alias_link	*lnk;
	struct ng_nat_listrdrs_entry rdr;
};
STAILQ_HEAD(rdrhead, ng_nat_rdr_lst);

/* Information we store for each node. */
struct ng_nat_priv {
	node_p		node;		/* back pointer to node */
	hook_p		in;		/* hook for demasquerading */
	hook_p		out;		/* hook for masquerading */
	struct libalias	*lib;		/* libalias handler */
	uint32_t	flags;		/* status flags */
	uint32_t	rdrcount;	/* number or redirects in list */
	uint32_t	nextid;		/* for next in turn in list */
	struct rdrhead	redirhead;	/* redirect list header */
	uint8_t		dlt;		/* DLT_XXX from bpf.h */
};
typedef struct ng_nat_priv *priv_p;

/* Values of flags */
#define	NGNAT_CONNECTED		0x1	/* We have both hooks connected */
#define	NGNAT_ADDR_DEFINED	0x2	/* NGM_NAT_SET_IPADDR happened */

static int
ng_nat_constructor(node_p node)
{
	priv_p priv;

	/* Initialize private descriptor. */
	priv = malloc(sizeof(*priv), M_NETGRAPH, M_WAITOK | M_ZERO);

	/* Init aliasing engine. */
	priv->lib = LibAliasInit(NULL);

	/* Set same ports on. */
	(void )LibAliasSetMode(priv->lib, PKT_ALIAS_SAME_PORTS,
	    PKT_ALIAS_SAME_PORTS);

	/* Init redirects housekeeping. */
	priv->rdrcount = 0;
	priv->nextid = 1;
	priv->dlt = DLT_RAW;
	STAILQ_INIT(&priv->redirhead);

	/* Link structs together. */
	NG_NODE_SET_PRIVATE(node, priv);
	priv->node = node;

	/*
	 * libalias is not thread safe, so our node
	 * must be single threaded.
	 */
	NG_NODE_FORCE_WRITER(node);

	return (0);
}

static int
ng_nat_newhook(node_p node, hook_p hook, const char *name)
{
	const priv_p priv = NG_NODE_PRIVATE(node);

	if (strcmp(name, NG_NAT_HOOK_IN) == 0) {
		priv->in = hook;
	} else if (strcmp(name, NG_NAT_HOOK_OUT) == 0) {
		priv->out = hook;
	} else
		return (EINVAL);

	if (priv->out != NULL &&
	    priv->in != NULL)
		priv->flags |= NGNAT_CONNECTED;

	return(0);
}

static int
ng_nat_rcvmsg(node_p node, item_p item, hook_p lasthook)
{
	const priv_p priv = NG_NODE_PRIVATE(node);
	struct ng_mesg *resp = NULL;
	struct ng_mesg *msg;
	int error = 0;

	NGI_GET_MSG(item, msg);

	switch (msg->header.typecookie) {
	case NGM_NAT_COOKIE:
		switch (msg->header.cmd) {
		case NGM_NAT_SET_IPADDR:
		    {
			struct in_addr *const ia = (struct in_addr *)msg->data;

			if (msg->header.arglen < sizeof(*ia)) {
				error = EINVAL;
				break;
			}

			LibAliasSetAddress(priv->lib, *ia);

			priv->flags |= NGNAT_ADDR_DEFINED;
		    }
			break;
		case NGM_NAT_SET_MODE:
		    {
			struct ng_nat_mode *const mode = 
			    (struct ng_nat_mode *)msg->data;

			if (msg->header.arglen < sizeof(*mode)) {
				error = EINVAL;
				break;
			}
			
			if (LibAliasSetMode(priv->lib, 
			    ng_nat_translate_flags(mode->flags),
			    ng_nat_translate_flags(mode->mask)) < 0) {
				error = ENOMEM;
				break;
			}
		    }
			break;
		case NGM_NAT_SET_TARGET:
		    {
			struct in_addr *const ia = (struct in_addr *)msg->data;

			if (msg->header.arglen < sizeof(*ia)) {
				error = EINVAL;
				break;
			}

			LibAliasSetTarget(priv->lib, *ia);
		    }
			break;
		case NGM_NAT_REDIRECT_PORT:
		    {
			struct ng_nat_rdr_lst *entry;
			struct ng_nat_redirect_port *const rp =
			    (struct ng_nat_redirect_port *)msg->data;

			if (msg->header.arglen < sizeof(*rp)) {
				error = EINVAL;
				break;
			}

			if ((entry = malloc(sizeof(struct ng_nat_rdr_lst),
			    M_NETGRAPH, M_NOWAIT | M_ZERO)) == NULL) {
				error = ENOMEM;
				break;
			}

			/* Try actual redirect. */
			entry->lnk = LibAliasRedirectPort(priv->lib,
				rp->local_addr, htons(rp->local_port),
				rp->remote_addr, htons(rp->remote_port),
				rp->alias_addr, htons(rp->alias_port),
				rp->proto);

			if (entry->lnk == NULL) {
				error = ENOMEM;
				free(entry, M_NETGRAPH);
				break;
			}

			/* Successful, save info in our internal list. */
			entry->rdr.local_addr = rp->local_addr;
			entry->rdr.alias_addr = rp->alias_addr;
			entry->rdr.remote_addr = rp->remote_addr;
			entry->rdr.local_port = rp->local_port;
			entry->rdr.alias_port = rp->alias_port;
			entry->rdr.remote_port = rp->remote_port;
			entry->rdr.proto = rp->proto;
			bcopy(rp->description, entry->rdr.description,
			    NG_NAT_DESC_LENGTH);

			/* Safety precaution. */
			entry->rdr.description[NG_NAT_DESC_LENGTH-1] = '\0';

			entry->rdr.id = priv->nextid++;
			priv->rdrcount++;

			/* Link to list of redirects. */
			STAILQ_INSERT_TAIL(&priv->redirhead, entry, entries);

			/* Response with id of newly added entry. */
			NG_MKRESPONSE(resp, msg, sizeof(entry->rdr.id), M_NOWAIT);
			if (resp == NULL) {
				error = ENOMEM;
				break;
			}
			bcopy(&entry->rdr.id, resp->data, sizeof(entry->rdr.id));
		    }
			break;
		case NGM_NAT_REDIRECT_ADDR:
		    {
			struct ng_nat_rdr_lst *entry;
			struct ng_nat_redirect_addr *const ra =
			    (struct ng_nat_redirect_addr *)msg->data;

			if (msg->header.arglen < sizeof(*ra)) {
				error = EINVAL;
				break;
			}

			if ((entry = malloc(sizeof(struct ng_nat_rdr_lst),
			    M_NETGRAPH, M_NOWAIT | M_ZERO)) == NULL) {
				error = ENOMEM;
				break;
			}

			/* Try actual redirect. */
			entry->lnk = LibAliasRedirectAddr(priv->lib,
				ra->local_addr, ra->alias_addr);

			if (entry->lnk == NULL) {
				error = ENOMEM;
				free(entry, M_NETGRAPH);
				break;
			}

			/* Successful, save info in our internal list. */
			entry->rdr.local_addr = ra->local_addr;
			entry->rdr.alias_addr = ra->alias_addr;
			entry->rdr.proto = NG_NAT_REDIRPROTO_ADDR;
			bcopy(ra->description, entry->rdr.description,
			    NG_NAT_DESC_LENGTH);

			/* Safety precaution. */
			entry->rdr.description[NG_NAT_DESC_LENGTH-1] = '\0';

			entry->rdr.id = priv->nextid++;
			priv->rdrcount++;

			/* Link to list of redirects. */
			STAILQ_INSERT_TAIL(&priv->redirhead, entry, entries);

			/* Response with id of newly added entry. */
			NG_MKRESPONSE(resp, msg, sizeof(entry->rdr.id), M_NOWAIT);
			if (resp == NULL) {
				error = ENOMEM;
				break;
			}
			bcopy(&entry->rdr.id, resp->data, sizeof(entry->rdr.id));
		    }
			break;
		case NGM_NAT_REDIRECT_PROTO:
		    {
			struct ng_nat_rdr_lst *entry;
			struct ng_nat_redirect_proto *const rp =
			    (struct ng_nat_redirect_proto *)msg->data;

			if (msg->header.arglen < sizeof(*rp)) {
				error = EINVAL;
				break;
			}

			if ((entry = malloc(sizeof(struct ng_nat_rdr_lst),
			    M_NETGRAPH, M_NOWAIT | M_ZERO)) == NULL) {
				error = ENOMEM;
				break;
			}

			/* Try actual redirect. */
			entry->lnk = LibAliasRedirectProto(priv->lib,
				rp->local_addr, rp->remote_addr,
				rp->alias_addr, rp->proto);

			if (entry->lnk == NULL) {
				error = ENOMEM;
				free(entry, M_NETGRAPH);
				break;
			}

			/* Successful, save info in our internal list. */
			entry->rdr.local_addr = rp->local_addr;
			entry->rdr.alias_addr = rp->alias_addr;
			entry->rdr.remote_addr = rp->remote_addr;
			entry->rdr.proto = rp->proto;
			bcopy(rp->description, entry->rdr.description,
			    NG_NAT_DESC_LENGTH);

			/* Safety precaution. */
			entry->rdr.description[NG_NAT_DESC_LENGTH-1] = '\0';

			entry->rdr.id = priv->nextid++;
			priv->rdrcount++;

			/* Link to list of redirects. */
			STAILQ_INSERT_TAIL(&priv->redirhead, entry, entries);

			/* Response with id of newly added entry. */
			NG_MKRESPONSE(resp, msg, sizeof(entry->rdr.id), M_NOWAIT);
			if (resp == NULL) {
				error = ENOMEM;
				break;
			}
			bcopy(&entry->rdr.id, resp->data, sizeof(entry->rdr.id));
		    }
			break;
		case NGM_NAT_REDIRECT_DYNAMIC:
		case NGM_NAT_REDIRECT_DELETE:
		    {
			struct ng_nat_rdr_lst *entry;
			uint32_t *const id = (uint32_t *)msg->data;

			if (msg->header.arglen < sizeof(*id)) {
				error = EINVAL;
				break;
			}

			/* Find entry with supplied id. */
			STAILQ_FOREACH(entry, &priv->redirhead, entries) {
				if (entry->rdr.id == *id)
					break;
			}

			/* Not found. */
			if (entry == NULL) {
				error = ENOENT;
				break;
			}

			if (msg->header.cmd == NGM_NAT_REDIRECT_DYNAMIC) {
				if (LibAliasRedirectDynamic(priv->lib,
				    entry->lnk) == -1) {
					error = ENOTTY;	/* XXX Something better? */
					break;
				}
			} else {	/* NGM_NAT_REDIRECT_DELETE */
				LibAliasRedirectDelete(priv->lib, entry->lnk);
			}

			/* Delete entry from our internal list. */
			priv->rdrcount--;
			STAILQ_REMOVE(&priv->redirhead, entry, ng_nat_rdr_lst, entries);
			free(entry, M_NETGRAPH);
		    }
			break;
		case NGM_NAT_ADD_SERVER:
		    {
			struct ng_nat_rdr_lst *entry;
			struct ng_nat_add_server *const as =
			    (struct ng_nat_add_server *)msg->data;

			if (msg->header.arglen < sizeof(*as)) {
				error = EINVAL;
				break;
			}

			/* Find entry with supplied id. */
			STAILQ_FOREACH(entry, &priv->redirhead, entries) {
				if (entry->rdr.id == as->id)
					break;
			}

			/* Not found. */
			if (entry == NULL) {
				error = ENOENT;
				break;
			}

			if (LibAliasAddServer(priv->lib, entry->lnk,
			    as->addr, htons(as->port)) == -1) {
				error = ENOMEM;
				break;
			}

			entry->rdr.lsnat++;
		    }
			break;
		case NGM_NAT_LIST_REDIRECTS:
		    {
			struct ng_nat_rdr_lst *entry;
			struct ng_nat_list_redirects *ary; 
			int i = 0;

			NG_MKRESPONSE(resp, msg, sizeof(*ary) +
			    (priv->rdrcount) * sizeof(*entry), M_NOWAIT);
			if (resp == NULL) {
				error = ENOMEM;
				break;
			}

			ary = (struct ng_nat_list_redirects *)resp->data;
			ary->total_count = priv->rdrcount;

			STAILQ_FOREACH(entry, &priv->redirhead, entries) {
				bcopy(&entry->rdr, &ary->redirects[i++],
				    sizeof(struct ng_nat_listrdrs_entry));
			}
		    }
			break;
		case NGM_NAT_PROXY_RULE:
		    {
			char *cmd = (char *)msg->data;

			if (msg->header.arglen < 6) {
				error = EINVAL;
				break;
			}

			if (LibAliasProxyRule(priv->lib, cmd) != 0)
				error = ENOMEM;
		    }
			break;
		case NGM_NAT_LIBALIAS_INFO:
		    {
			struct ng_nat_libalias_info *i;

			NG_MKRESPONSE(resp, msg,
			    sizeof(struct ng_nat_libalias_info), M_NOWAIT);
			if (resp == NULL) {
				error = ENOMEM;
				break;
			}
			i = (struct ng_nat_libalias_info *)resp->data;
#define	COPY(F)	do {						\
	if (priv->lib->F >= 0 && priv->lib->F < UINT32_MAX)	\
		i->F = priv->lib->F;				\
	else							\
		i->F = UINT32_MAX;				\
} while (0)
		
			COPY(icmpLinkCount);
			COPY(udpLinkCount);
			COPY(tcpLinkCount);
			COPY(pptpLinkCount);
			COPY(sctpLinkCount);
			COPY(protoLinkCount);
			COPY(fragmentIdLinkCount);
			COPY(fragmentPtrLinkCount);
			COPY(sockCount);
#undef COPY
		    }
			break;
		case NGM_NAT_SET_DLT:
			if (msg->header.arglen != sizeof(uint8_t)) {
				error = EINVAL;
				break;
			}
			switch (*(uint8_t *) msg->data) {
			case DLT_EN10MB:
			case DLT_RAW:
				priv->dlt = *(uint8_t *) msg->data;
				break;
			default:
				error = EINVAL;
				break;
			}
			break;
		default:
			error = EINVAL;		/* unknown command */
			break;
		}
		break;
		case NGM_NAT_GET_DLT:
			NG_MKRESPONSE(resp, msg, sizeof(uint8_t), M_WAITOK);
                        if (resp == NULL) {
                                error = ENOMEM;
				break;
			}
			*((uint8_t *) resp->data) = priv->dlt;
			break;
	default:
		error = EINVAL;			/* unknown cookie type */
		break;
	}

	NG_RESPOND_MSG(error, node, item, resp);
	NG_FREE_MSG(msg);
	return (error);
}

static int
ng_nat_rcvdata(hook_p hook, item_p item )
{
	const priv_p priv = NG_NODE_PRIVATE(NG_HOOK_NODE(hook));
	struct mbuf	*m;
	struct ip	*ip;
	int rval, ipofs, error = 0;
	char *c;

	/* We have no required hooks. */
	if (!(priv->flags & NGNAT_CONNECTED)) {
		NG_FREE_ITEM(item);
		return (ENXIO);
	}

	/* We have no alias address yet to do anything. */
	if (!(priv->flags & NGNAT_ADDR_DEFINED))
		goto send;

	m = NGI_M(item);

	if ((m = m_megapullup(m, m->m_pkthdr.len)) == NULL) {
		NGI_M(item) = NULL;	/* avoid double free */
		NG_FREE_ITEM(item);
		return (ENOBUFS);
	}

	NGI_M(item) = m;

	switch (priv->dlt) {
	case DLT_RAW:
		ipofs = 0;
		break;
	case DLT_EN10MB:
	    {
		struct ether_header *eh;

		if (m->m_pkthdr.len < sizeof(struct ether_header)) {
			NG_FREE_ITEM(item);
			return (ENXIO);
		}
		eh = mtod(m, struct ether_header *);
		switch (ntohs(eh->ether_type)) {
		case ETHERTYPE_IP:
		case ETHERTYPE_IPV6:
			ipofs = sizeof(struct ether_header);
			break;
		default:
			goto send;
		}
		break;
	    }
	default:
		panic("Corrupted priv->dlt: %u", priv->dlt);
	}

	c = (char *)mtodo(m, ipofs);
	ip = (struct ip *)mtodo(m, ipofs);

	KASSERT(m->m_pkthdr.len == ipofs + ntohs(ip->ip_len),
	    ("ng_nat: ip_len != m_pkthdr.len"));

	/*
	 * We drop packet when:
	 * 1. libalias returns PKT_ALIAS_ERROR;
	 * 2. For incoming packets:
	 *	a) for unresolved fragments;
	 *	b) libalias returns PKT_ALIAS_IGNORED and
	 *		PKT_ALIAS_DENY_INCOMING flag is set.
	 */
	if (hook == priv->in) {
		rval = LibAliasIn(priv->lib, c, m->m_len - ipofs +
		    M_TRAILINGSPACE(m));
		if (rval == PKT_ALIAS_ERROR ||
		    rval == PKT_ALIAS_UNRESOLVED_FRAGMENT ||
		    (rval == PKT_ALIAS_IGNORED &&
		     (priv->lib->packetAliasMode &
		      PKT_ALIAS_DENY_INCOMING) != 0)) {
			NG_FREE_ITEM(item);
			return (EINVAL);
		}
	} else if (hook == priv->out) {
		rval = LibAliasOut(priv->lib, c, m->m_len - ipofs +
		    M_TRAILINGSPACE(m));
		if (rval == PKT_ALIAS_ERROR) {
			NG_FREE_ITEM(item);
			return (EINVAL);
		}
	} else
		panic("ng_nat: unknown hook!\n");

	if (rval == PKT_ALIAS_RESPOND)
		m->m_flags |= M_SKIP_FIREWALL;
	m->m_pkthdr.len = m->m_len = ntohs(ip->ip_len) + ipofs;

	if ((ip->ip_off & htons(IP_OFFMASK)) == 0 &&
	    ip->ip_p == IPPROTO_TCP) {
		struct tcphdr *th = (struct tcphdr *)((caddr_t)ip +
		    (ip->ip_hl << 2));

		/*
		 * Here is our terrible HACK.
		 *
		 * Sometimes LibAlias edits contents of TCP packet.
		 * In this case it needs to recompute full TCP
		 * checksum. However, the problem is that LibAlias
		 * doesn't have any idea about checksum offloading
		 * in kernel. To workaround this, we do not do
		 * checksumming in LibAlias, but only mark the
		 * packets in th_x2 field. If we receive a marked
		 * packet, we calculate correct checksum for it
		 * aware of offloading.
		 *
		 * Why do I do such a terrible hack instead of
		 * recalculating checksum for each packet?
		 * Because the previous checksum was not checked!
		 * Recalculating checksums for EVERY packet will
		 * hide ALL transmission errors. Yes, marked packets
		 * still suffer from this problem. But, sigh, natd(8)
		 * has this problem, too.
		 */

		if (th->th_x2) {
			uint16_t ip_len = ntohs(ip->ip_len);

			th->th_x2 = 0;
			th->th_sum = in_pseudo(ip->ip_src.s_addr,
			    ip->ip_dst.s_addr, htons(IPPROTO_TCP +
			    ip_len - (ip->ip_hl << 2)));
	
			if ((m->m_pkthdr.csum_flags & CSUM_TCP) == 0) {
				m->m_pkthdr.csum_data = offsetof(struct tcphdr,
				    th_sum);
				in_delayed_cksum(m);
			}
		}
	}

send:
	if (hook == priv->in)
		NG_FWD_ITEM_HOOK(error, item, priv->out);
	else
		NG_FWD_ITEM_HOOK(error, item, priv->in);

	return (error);
}

static int
ng_nat_shutdown(node_p node)
{
	const priv_p priv = NG_NODE_PRIVATE(node);

	NG_NODE_SET_PRIVATE(node, NULL);
	NG_NODE_UNREF(node);

	/* Free redirects list. */
	while (!STAILQ_EMPTY(&priv->redirhead)) {
		struct ng_nat_rdr_lst *entry = STAILQ_FIRST(&priv->redirhead);
		STAILQ_REMOVE_HEAD(&priv->redirhead, entries);
		free(entry, M_NETGRAPH);
	}

	/* Final free. */
	LibAliasUninit(priv->lib);
	free(priv, M_NETGRAPH);

	return (0);
}

static int
ng_nat_disconnect(hook_p hook)
{
	const priv_p priv = NG_NODE_PRIVATE(NG_HOOK_NODE(hook));

	priv->flags &= ~NGNAT_CONNECTED;

	if (hook == priv->out)
		priv->out = NULL;
	if (hook == priv->in)
		priv->in = NULL;

	if (priv->out == NULL && priv->in == NULL)
		ng_rmnode_self(NG_HOOK_NODE(hook));

	return (0);
}

static unsigned int
ng_nat_translate_flags(unsigned int x)
{
	unsigned int	res = 0;
	
	if (x & NG_NAT_LOG)
		res |= PKT_ALIAS_LOG;
	if (x & NG_NAT_DENY_INCOMING)
		res |= PKT_ALIAS_DENY_INCOMING;
	if (x & NG_NAT_SAME_PORTS)
		res |= PKT_ALIAS_SAME_PORTS;
	if (x & NG_NAT_UNREGISTERED_ONLY)
		res |= PKT_ALIAS_UNREGISTERED_ONLY;
	if (x & NG_NAT_RESET_ON_ADDR_CHANGE)
		res |= PKT_ALIAS_RESET_ON_ADDR_CHANGE;
	if (x & NG_NAT_PROXY_ONLY)
		res |= PKT_ALIAS_PROXY_ONLY;
	if (x & NG_NAT_REVERSE)
		res |= PKT_ALIAS_REVERSE;

	return (res);
}
