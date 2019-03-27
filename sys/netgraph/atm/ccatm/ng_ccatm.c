/*-
 * Copyright (c) 2001-2002
 *	Fraunhofer Institute for Open Communication Systems (FhG Fokus).
 *	All rights reserved.
 * Copyright (c) 2003-2004
 *	Hartmut Brandt
 *	All rights reserved.
 *
 * Author: Harti Brandt <harti@freebsd.org>
 *
 * Redistribution of this software and documentation and use in source and
 * binary forms, with or without modification, are permitted provided that
 * the following conditions are met:
 *
 * 1. Redistributions of source code or documentation must retain the above
 *    copyright notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE AND DOCUMENTATION IS PROVIDED BY THE AUTHOR
 * AND ITS CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL
 * THE AUTHOR OR ITS CONTRIBUTORS  BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 * ATM call control and API
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/errno.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sbuf.h>
#include <machine/stdarg.h>

#include <netgraph/ng_message.h>
#include <netgraph/netgraph.h>
#include <netgraph/ng_parse.h>
#include <netnatm/unimsg.h>
#include <netnatm/msg/unistruct.h>
#include <netnatm/api/unisap.h>
#include <netnatm/sig/unidef.h>
#include <netgraph/atm/ngatmbase.h>
#include <netgraph/atm/ng_uni.h>
#include <netnatm/api/atmapi.h>
#include <netgraph/atm/ng_ccatm.h>
#include <netnatm/api/ccatm.h>

MODULE_DEPEND(ng_ccatm, ngatmbase, 1, 1, 1);

MALLOC_DEFINE(M_NG_CCATM, "ng_ccatm", "netgraph uni api node");

/*
 * Command structure parsing
 */

/* ESI */
static const struct ng_parse_fixedarray_info ng_ccatm_esi_type_info =
    NGM_CCATM_ESI_INFO;
static const struct ng_parse_type ng_ccatm_esi_type = {
	&ng_parse_fixedarray_type,
	&ng_ccatm_esi_type_info
};

/* PORT PARAMETERS */
static const struct ng_parse_struct_field ng_ccatm_atm_port_type_info[] =
    NGM_CCATM_ATM_PORT_INFO;
static const struct ng_parse_type ng_ccatm_atm_port_type = {
	&ng_parse_struct_type,
	ng_ccatm_atm_port_type_info
};

/* PORT structure */
static const struct ng_parse_struct_field ng_ccatm_port_type_info[] =
    NGM_CCATM_PORT_INFO;
static const struct ng_parse_type ng_ccatm_port_type = {
	&ng_parse_struct_type,
	ng_ccatm_port_type_info
};

/* the ADDRESS array itself */
static const struct ng_parse_fixedarray_info ng_ccatm_addr_array_type_info =
    NGM_CCATM_ADDR_ARRAY_INFO;
static const struct ng_parse_type ng_ccatm_addr_array_type = {
	&ng_parse_fixedarray_type,
	&ng_ccatm_addr_array_type_info
};

/* one ADDRESS */
static const struct ng_parse_struct_field ng_ccatm_uni_addr_type_info[] =
    NGM_CCATM_UNI_ADDR_INFO;
static const struct ng_parse_type ng_ccatm_uni_addr_type = {
	&ng_parse_struct_type,
	ng_ccatm_uni_addr_type_info
};

/* ADDRESS request */
static const struct ng_parse_struct_field ng_ccatm_addr_req_type_info[] =
    NGM_CCATM_ADDR_REQ_INFO;
static const struct ng_parse_type ng_ccatm_addr_req_type = {
	&ng_parse_struct_type,
	ng_ccatm_addr_req_type_info
};

/* ADDRESS var-array */
static int
ng_ccatm_addr_req_array_getlen(const struct ng_parse_type *type,
    const u_char *start, const u_char *buf)
{
	const struct ngm_ccatm_get_addresses *p;

	p = (const struct ngm_ccatm_get_addresses *)
	    (buf - offsetof(struct ngm_ccatm_get_addresses, addr));
	return (p->count);
}
static const struct ng_parse_array_info ng_ccatm_addr_req_array_type_info =
    NGM_CCATM_ADDR_REQ_ARRAY_INFO;
static const struct ng_parse_type ng_ccatm_addr_req_array_type = {
	&ng_parse_array_type,
	&ng_ccatm_addr_req_array_type_info
};

/* Outer get_ADDRESSes structure */
static const struct ng_parse_struct_field ng_ccatm_get_addresses_type_info[] =
    NGM_CCATM_GET_ADDRESSES_INFO;
static const struct ng_parse_type ng_ccatm_get_addresses_type = {
	&ng_parse_struct_type,
	ng_ccatm_get_addresses_type_info
};

/* Port array */
static int
ng_ccatm_port_array_getlen(const struct ng_parse_type *type,
    const u_char *start, const u_char *buf)
{
	const struct ngm_ccatm_portlist *p;

	p = (const struct ngm_ccatm_portlist *)
	    (buf - offsetof(struct ngm_ccatm_portlist, ports));
	return (p->nports);
}
static const struct ng_parse_array_info ng_ccatm_port_array_type_info =
    NGM_CCATM_PORT_ARRAY_INFO;
static const struct ng_parse_type ng_ccatm_port_array_type = {
	&ng_parse_array_type,
	&ng_ccatm_port_array_type_info
};

/* Portlist structure */
static const struct ng_parse_struct_field ng_ccatm_portlist_type_info[] =
    NGM_CCATM_PORTLIST_INFO;
static const struct ng_parse_type ng_ccatm_portlist_type = {
	&ng_parse_struct_type,
	ng_ccatm_portlist_type_info
};

/*
 * Command list
 */
static const struct ng_cmdlist ng_ccatm_cmdlist[] = {
	{
	  NGM_CCATM_COOKIE,
	  NGM_CCATM_DUMP,
	  "dump",
	  NULL,
	  NULL
	},
	{
	  NGM_CCATM_COOKIE,
	  NGM_CCATM_STOP,
	  "stop",
	  &ng_ccatm_port_type,
	  NULL
	},
	{
	  NGM_CCATM_COOKIE,
	  NGM_CCATM_START,
	  "start",
	  &ng_ccatm_port_type,
	  NULL
	},
	{
	  NGM_CCATM_COOKIE,
	  NGM_CCATM_GETSTATE,
	  "getstate",
	  &ng_ccatm_port_type,
	  &ng_parse_uint32_type
	},
	{
	  NGM_CCATM_COOKIE,
	  NGM_CCATM_GET_ADDRESSES,
	  "get_addresses",
	  &ng_ccatm_port_type,
	  &ng_ccatm_get_addresses_type
	},
	{
	  NGM_CCATM_COOKIE,
	  NGM_CCATM_CLEAR,
	  "clear",
	  &ng_ccatm_port_type,
	  NULL
	},
	{
	  NGM_CCATM_COOKIE,
	  NGM_CCATM_ADDRESS_REGISTERED,
	  "address_reg",
	  &ng_ccatm_addr_req_type,
	  NULL
	},
	{
	  NGM_CCATM_COOKIE,
	  NGM_CCATM_ADDRESS_UNREGISTERED,
	  "address_unreg",
	  &ng_ccatm_addr_req_type,
	  NULL
	},
	{
	  NGM_CCATM_COOKIE,
	  NGM_CCATM_SET_PORT_PARAM,
	  "set_port_param",
	  &ng_ccatm_atm_port_type,
	  NULL
	},
	{
	  NGM_CCATM_COOKIE,
	  NGM_CCATM_GET_PORT_PARAM,
	  "get_port_param",
	  &ng_ccatm_port_type,
	  &ng_ccatm_atm_port_type,
	},
	{
	  NGM_CCATM_COOKIE,
	  NGM_CCATM_GET_PORTLIST,
	  "get_portlist",
	  NULL,
	  &ng_ccatm_portlist_type,
	},
	{
	  NGM_CCATM_COOKIE,
	  NGM_CCATM_SETLOG,
	  "setlog",
	  &ng_parse_hint32_type,
	  &ng_parse_hint32_type,
	},
	{
	  NGM_CCATM_COOKIE,
	  NGM_CCATM_RESET,
	  "reset",
	  NULL,
	  NULL,
	},
	{ 0 }
};

/*
 * Module data
 */
static ng_constructor_t		ng_ccatm_constructor;
static ng_rcvmsg_t		ng_ccatm_rcvmsg;
static ng_shutdown_t		ng_ccatm_shutdown;
static ng_newhook_t		ng_ccatm_newhook;
static ng_rcvdata_t		ng_ccatm_rcvdata;
static ng_disconnect_t		ng_ccatm_disconnect;
static int ng_ccatm_mod_event(module_t, int, void *);

static struct ng_type ng_ccatm_typestruct = {
	.version =	NG_ABI_VERSION,
	.name =		NG_CCATM_NODE_TYPE,
	.mod_event =	ng_ccatm_mod_event,
	.constructor =	ng_ccatm_constructor,	/* Node constructor */
	.rcvmsg =	ng_ccatm_rcvmsg,	/* Control messages */
	.shutdown =	ng_ccatm_shutdown,	/* Node destructor */
	.newhook =	ng_ccatm_newhook,	/* Arrival of new hook */
	.rcvdata =	ng_ccatm_rcvdata,	/* receive data */
	.disconnect =	ng_ccatm_disconnect,	/* disconnect a hook */
	.cmdlist =	ng_ccatm_cmdlist,
};
NETGRAPH_INIT(ccatm, &ng_ccatm_typestruct);

static ng_rcvdata_t	ng_ccatm_rcvuni;
static ng_rcvdata_t	ng_ccatm_rcvdump;
static ng_rcvdata_t	ng_ccatm_rcvmanage;

/*
 * Private node data.
 */
struct ccnode {
	node_p	node;		/* the owning node */
	hook_p	dump;		/* dump hook */
	hook_p	manage;		/* hook to ILMI */

	struct ccdata *data;
	struct mbuf *dump_first;
	struct mbuf *dump_last;	/* first and last mbuf when dumping */

	u_int	hook_cnt;	/* count user and port hooks */
};

/*
 * Private UNI hook data
 */
struct cchook {
	int		is_uni;	/* true if uni hook, user otherwise */
	struct ccnode	*node;	/* the owning node */
	hook_p		hook;
	void		*inst;	/* port or user */
};

static void ng_ccatm_send_user(struct ccuser *, void *, u_int, void *, size_t);
static void ng_ccatm_respond_user(struct ccuser *, void *, int, u_int,
    void *, size_t);
static void ng_ccatm_send_uni(struct ccconn *, void *, u_int, u_int,
    struct uni_msg *);
static void ng_ccatm_send_uni_glob(struct ccport *, void *, u_int, u_int,
    struct uni_msg *);
static void ng_ccatm_log(const char *, ...) __printflike(1, 2);

static const struct cc_funcs cc_funcs = {
	.send_user =		ng_ccatm_send_user,
	.respond_user =		ng_ccatm_respond_user,
	.send_uni =		ng_ccatm_send_uni,
	.send_uni_glob =	ng_ccatm_send_uni_glob,
	.log =			ng_ccatm_log,
};

/************************************************************
 *
 * Create a new node
 */
static int
ng_ccatm_constructor(node_p node)
{
	struct ccnode *priv;

	priv = malloc(sizeof(*priv), M_NG_CCATM, M_WAITOK | M_ZERO);

	priv->node = node;
	priv->data = cc_create(&cc_funcs);
	if (priv->data == NULL) {
		free(priv, M_NG_CCATM);
		return (ENOMEM);
	}

	NG_NODE_SET_PRIVATE(node, priv);

	return (0);
}

/*
 * Destroy a node. The user list is empty here, because all hooks are
 * previously disconnected. The connection lists may not be empty, because
 * connections may be waiting for responses from the stack. This also means,
 * that no orphaned connections will be made by the port_destroy routine.
 */
static int
ng_ccatm_shutdown(node_p node)
{
	struct ccnode *priv = NG_NODE_PRIVATE(node);

	cc_destroy(priv->data);

	free(priv, M_NG_CCATM);
	NG_NODE_SET_PRIVATE(node, NULL);

	NG_NODE_UNREF(node);

	return (0);
}

/*
 * Retrieve the registered addresses for one port or all ports.
 * Returns an error code or 0 on success.
 */
static int
ng_ccatm_get_addresses(node_p node, uint32_t portno, struct ng_mesg *msg,
    struct ng_mesg **resp)
{
	struct ccnode *priv = NG_NODE_PRIVATE(node);
	struct uni_addr *addrs;
	u_int *ports;
	struct ngm_ccatm_get_addresses *list;
	u_int count, i;
	size_t len;
	int err;

	err = cc_get_addrs(priv->data, portno, &addrs, &ports, &count);
	if (err != 0)
		return (err);

	len = sizeof(*list) + count * sizeof(list->addr[0]);
	NG_MKRESPONSE(*resp, msg, len, M_NOWAIT);
	if (*resp == NULL) {
		free(addrs, M_NG_CCATM);
		free(ports, M_NG_CCATM);
		return (ENOMEM);
	}
	list = (struct ngm_ccatm_get_addresses *)(*resp)->data;

	list->count = count;
	for (i = 0; i < count; i++) {
		list->addr[i].port = ports[i];
		list->addr[i].addr = addrs[i];
	}

	free(addrs, M_NG_CCATM);
	free(ports, M_NG_CCATM);

	return (0);
}

/*
 * Dumper function. Pack the data into an mbuf chain.
 */
static int
send_dump(struct ccdata *data, void *uarg, const char *buf)
{
	struct mbuf *m;
	struct ccnode *priv = uarg;

	if (priv->dump == NULL) {
		m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
		if (m == NULL)
			return (ENOBUFS);
		priv->dump_first = priv->dump_last = m;
		m->m_pkthdr.len = 0;
	} else {
		m = m_getcl(M_NOWAIT, MT_DATA, 0);
		if (m == NULL) {
			m_freem(priv->dump_first);
			return (ENOBUFS);
		}
		priv->dump_last->m_next = m;
		priv->dump_last = m;
	}

	strcpy(m->m_data, buf);
	priv->dump_first->m_pkthdr.len += (m->m_len = strlen(buf));

	return (0);
}

/*
 * Dump current status to dump hook
 */
static int
ng_ccatm_dump(node_p node)
{
	struct ccnode *priv = NG_NODE_PRIVATE(node);
	struct mbuf *m;
	int error;

	priv->dump_first = priv->dump_last = NULL;
	error = cc_dump(priv->data, MCLBYTES, send_dump, priv);
	if (error != 0)
		return (error);

	if ((m = priv->dump_first) != NULL) {
		priv->dump_first = priv->dump_last = NULL;
		NG_SEND_DATA_ONLY(error, priv->dump, m);
		return (error);
	}
	return (0);
}

/*
 * Control message
 */
static int
ng_ccatm_rcvmsg(node_p node, item_p item, hook_p lasthook)
{
	struct ng_mesg *resp = NULL;
	struct ng_mesg *msg;
	struct ccnode *priv = NG_NODE_PRIVATE(node);
	int error = 0;

	NGI_GET_MSG(item, msg);

	switch (msg->header.typecookie) {

	  case NGM_CCATM_COOKIE:
		switch (msg->header.cmd) {

		  case NGM_CCATM_DUMP:
			if (priv->dump)
				error = ng_ccatm_dump(node);
			else
				error = ENOTCONN;
			break;

		  case NGM_CCATM_STOP:
		    {
			struct ngm_ccatm_port *arg;

			if (msg->header.arglen != sizeof(*arg)) {
				error = EINVAL;
				break;
			}
			arg = (struct ngm_ccatm_port *)msg->data;
			error = cc_port_stop(priv->data, arg->port);
			break;
		    }

		  case NGM_CCATM_START:
		    {
			struct ngm_ccatm_port *arg;

			if (msg->header.arglen != sizeof(*arg)) {
				error = EINVAL;
				break;
			}
			arg = (struct ngm_ccatm_port *)msg->data;
			error = cc_port_start(priv->data, arg->port);
			break;
		    }

		  case NGM_CCATM_GETSTATE:
		    {
			struct ngm_ccatm_port *arg;
			int state;

			if (msg->header.arglen != sizeof(*arg)) {
				error = EINVAL;
				break;
			}
			arg = (struct ngm_ccatm_port *)msg->data;
			error = cc_port_isrunning(priv->data, arg->port,
			    &state);
			if (error == 0) {
				NG_MKRESPONSE(resp, msg, sizeof(uint32_t),
				    M_NOWAIT);
				if (resp == NULL) {
					error = ENOMEM;
					break;
				}
				*(uint32_t *)resp->data = state;
			}
			break;
		    }

		  case NGM_CCATM_GET_ADDRESSES:
		   {
			struct ngm_ccatm_port *arg;

			if (msg->header.arglen != sizeof(*arg)) {
				error = EINVAL;
				break;
			}
			arg = (struct ngm_ccatm_port *)msg->data;
			error = ng_ccatm_get_addresses(node, arg->port, msg,
			    &resp);
			break;
		    }

		  case NGM_CCATM_CLEAR:
		    {
			struct ngm_ccatm_port *arg;

			if (msg->header.arglen != sizeof(*arg)) {
				error = EINVAL;
				break;
			}
			arg = (struct ngm_ccatm_port *)msg->data;
			error = cc_port_clear(priv->data, arg->port);
			break;
		    }

		  case NGM_CCATM_ADDRESS_REGISTERED:
		    {
			struct ngm_ccatm_addr_req *arg;

			if (msg->header.arglen != sizeof(*arg)) {
				error = EINVAL;
				break;
			}
			arg = (struct ngm_ccatm_addr_req *)msg->data;
			error = cc_addr_register(priv->data, arg->port,
			    &arg->addr);
			break;
		    }

		  case NGM_CCATM_ADDRESS_UNREGISTERED:
		    {
			struct ngm_ccatm_addr_req *arg;

			if (msg->header.arglen != sizeof(*arg)) {
				error = EINVAL;
				break;
			}
			arg = (struct ngm_ccatm_addr_req *)msg->data;
			error = cc_addr_unregister(priv->data, arg->port,
			    &arg->addr);
			break;
		    }

		  case NGM_CCATM_GET_PORT_PARAM:
		    {
			struct ngm_ccatm_port *arg;

			if (msg->header.arglen != sizeof(*arg)) {
				error = EINVAL;
				break;
			}
			arg = (struct ngm_ccatm_port *)msg->data;
			NG_MKRESPONSE(resp, msg, sizeof(struct atm_port_info),
			    M_NOWAIT);
			if (resp == NULL) {
				error = ENOMEM;
				break;
			}
			error = cc_port_get_param(priv->data, arg->port,
			    (struct atm_port_info *)resp->data);
			if (error != 0) {
				free(resp, M_NETGRAPH_MSG);
				resp = NULL;
			}
			break;
		    }

		  case NGM_CCATM_SET_PORT_PARAM:
		    {
			struct atm_port_info *arg;

			if (msg->header.arglen != sizeof(*arg)) {
				error = EINVAL;
				break;
			}
			arg = (struct atm_port_info *)msg->data;
			error = cc_port_set_param(priv->data, arg);
			break;
		    }

		  case NGM_CCATM_GET_PORTLIST:
		    {
			struct ngm_ccatm_portlist *arg;
			u_int n, *ports;

			if (msg->header.arglen != 0) {
				error = EINVAL;
				break;
			}
			error = cc_port_getlist(priv->data, &n, &ports);
			if (error != 0)
				break;

			NG_MKRESPONSE(resp, msg, sizeof(*arg) +
			    n * sizeof(arg->ports[0]), M_NOWAIT);
			if (resp == NULL) {
				free(ports, M_NG_CCATM);
				error = ENOMEM;
				break;
			}
			arg = (struct ngm_ccatm_portlist *)resp->data;

			arg->nports = 0;
			for (arg->nports = 0; arg->nports < n; arg->nports++)
				arg->ports[arg->nports] = ports[arg->nports];
			free(ports, M_NG_CCATM);
			break;
		    }

		  case NGM_CCATM_SETLOG:
		    {
			uint32_t log_level;

			log_level = cc_get_log(priv->data);
			if (msg->header.arglen != 0) {
				if (msg->header.arglen != sizeof(log_level)) {
					error = EINVAL;
					break;
				}
				cc_set_log(priv->data, *(uint32_t *)msg->data);
			}

			NG_MKRESPONSE(resp, msg, sizeof(uint32_t), M_NOWAIT);
			if (resp == NULL) {
				error = ENOMEM;
				if (msg->header.arglen != 0)
					cc_set_log(priv->data, log_level);
				break;
			}
			*(uint32_t *)resp->data = log_level;
			break;
		    }

		  case NGM_CCATM_RESET:
			if (msg->header.arglen != 0) {
				error = EINVAL;
				break;
			}

			if (priv->hook_cnt != 0) {
				error = EBUSY;
				break;
			}
			cc_reset(priv->data);
			break;

		  case NGM_CCATM_GET_EXSTAT:
		    {
			struct atm_exstatus s;
			struct atm_exstatus_ep *eps;
			struct atm_exstatus_port *ports;
			struct atm_exstatus_conn *conns;
			struct atm_exstatus_party *parties;
			size_t offs;

			if (msg->header.arglen != 0) {
				error = EINVAL;
				break;
			}
			error = cc_get_extended_status(priv->data,
			    &s, &eps, &ports, &conns, &parties);
			if (error != 0)
				break;

			offs = sizeof(s) + s.neps * sizeof(*eps) +
			    s.nports * sizeof(*ports) +
			    s.nconns * sizeof(*conns) +
			    s.nparties * sizeof(*parties);

			NG_MKRESPONSE(resp, msg, offs, M_NOWAIT);
			if (resp == NULL) {
				error = ENOMEM;
				break;
			}

			memcpy(resp->data, &s, sizeof(s));
			offs = sizeof(s);

			memcpy(resp->data + offs, eps,
			    sizeof(*eps) * s.neps);
			offs += sizeof(*eps) * s.neps;

			memcpy(resp->data + offs, ports,
			    sizeof(*ports) * s.nports);
			offs += sizeof(*ports) * s.nports;

			memcpy(resp->data + offs, conns,
			    sizeof(*conns) * s.nconns);
			offs += sizeof(*conns) * s.nconns;

			memcpy(resp->data + offs, parties,
			    sizeof(*parties) * s.nparties);
			offs += sizeof(*parties) * s.nparties;

			free(eps, M_NG_CCATM);
			free(ports, M_NG_CCATM);
			free(conns, M_NG_CCATM);
			free(parties, M_NG_CCATM);
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

	NG_RESPOND_MSG(error, node, item, resp);
	NG_FREE_MSG(msg);
	return (error);
}

/************************************************************
 *
 * New hook arrival
 */
static int
ng_ccatm_newhook(node_p node, hook_p hook, const char *name)
{
	struct ccnode *priv = NG_NODE_PRIVATE(node);
	struct ccport *port;
	struct ccuser *user;
	struct cchook *hd;
	u_long lport;
	char *end;

	if (strncmp(name, "uni", 3) == 0) {
		/*
		 * This is a UNI hook. Should be a new port.
		 */
		if (name[3] == '\0')
			return (EINVAL);
		lport = strtoul(name + 3, &end, 10);
		if (*end != '\0' || lport == 0 || lport > 0xffffffff)
			return (EINVAL);

		hd = malloc(sizeof(*hd), M_NG_CCATM, M_NOWAIT);
		if (hd == NULL)
			return (ENOMEM);
		hd->is_uni = 1;
		hd->node = priv;
		hd->hook = hook;

		port = cc_port_create(priv->data, hd, (u_int)lport);
		if (port == NULL) {
			free(hd, M_NG_CCATM);
			return (ENOMEM);
		}
		hd->inst = port;

		NG_HOOK_SET_PRIVATE(hook, hd);
		NG_HOOK_SET_RCVDATA(hook, ng_ccatm_rcvuni);
		NG_HOOK_FORCE_QUEUE(hook);

		priv->hook_cnt++;

		return (0);
	}

	if (strcmp(name, "dump") == 0) {
		priv->dump = hook;
		NG_HOOK_SET_RCVDATA(hook, ng_ccatm_rcvdump);
		return (0);
	}

	if (strcmp(name, "manage") == 0) {
		priv->manage = hook;
		NG_HOOK_SET_RCVDATA(hook, ng_ccatm_rcvmanage);
		return (0);
	}

	/*
	 * User hook
	 */
	hd = malloc(sizeof(*hd), M_NG_CCATM, M_NOWAIT);
	if (hd == NULL)
		return (ENOMEM);
	hd->is_uni = 0;
	hd->node = priv;
	hd->hook = hook;

	user = cc_user_create(priv->data, hd, NG_HOOK_NAME(hook));
	if (user == NULL) {
		free(hd, M_NG_CCATM);
		return (ENOMEM);
	}

	hd->inst = user;
	NG_HOOK_SET_PRIVATE(hook, hd);
	NG_HOOK_FORCE_QUEUE(hook);

	priv->hook_cnt++;

	return (0);
}

/*
 * Disconnect a hook
 */
static int
ng_ccatm_disconnect(hook_p hook)
{
	node_p node = NG_HOOK_NODE(hook);
	struct ccnode *priv = NG_NODE_PRIVATE(node);
	struct cchook *hd = NG_HOOK_PRIVATE(hook);
	struct ccdata *cc;

	if (hook == priv->dump) {
		priv->dump = NULL;

	} else if (hook == priv->manage) {
		priv->manage = NULL;
		cc_unmanage(priv->data);

	} else {
		if (hd->is_uni)
			cc_port_destroy(hd->inst, 0);
		else
			cc_user_destroy(hd->inst);

		cc = hd->node->data;

		free(hd, M_NG_CCATM);
		NG_HOOK_SET_PRIVATE(hook, NULL);

		priv->hook_cnt--;

		cc_work(cc);
	}

	/*
	 * When the number of hooks drops to zero, delete the node.
	 */
	if (NG_NODE_NUMHOOKS(node) == 0 && NG_NODE_IS_VALID(node))
		ng_rmnode_self(node);

	return (0);
}

/************************************************************
 *
 * Receive data from user hook
 */
static int
ng_ccatm_rcvdata(hook_p hook, item_p item)
{
	struct cchook *hd = NG_HOOK_PRIVATE(hook);
	struct uni_msg *msg;
	struct mbuf *m;
	struct ccatm_op op;
	int err;

	NGI_GET_M(item, m);
	NG_FREE_ITEM(item);

	if ((err = uni_msg_unpack_mbuf(m, &msg)) != 0) {
		m_freem(m);
		return (err);
	}
	m_freem(m);

	if (uni_msg_len(msg) < sizeof(op)) {
		printf("%s: packet too short\n", __func__);
		uni_msg_destroy(msg);
		return (EINVAL);
	}

	bcopy(msg->b_rptr, &op, sizeof(op));
	msg->b_rptr += sizeof(op);

	err = cc_user_signal(hd->inst, op.op, msg);
	cc_work(hd->node->data);
	return (err);
}

/*
 * Pack a header and a data area into an mbuf chain
 */
static struct mbuf *
pack_buf(void *h, size_t hlen, void *t, size_t tlen)
{
	struct mbuf *m, *m0, *last;
	u_char *buf = (u_char *)t;
	size_t n;

	/* header should fit into a normal mbuf */
	MGETHDR(m0, M_NOWAIT, MT_DATA);
	if (m0 == NULL)
		return NULL;

	KASSERT(hlen <= MHLEN, ("hlen > MHLEN"));

	bcopy(h, m0->m_data, hlen);
	m0->m_len = hlen;
	m0->m_pkthdr.len = hlen;

	last = m0;
	while ((n = tlen) != 0) {
		if (n > MLEN) {
			m = m_getcl(M_NOWAIT, MT_DATA, 0);
			if (n > MCLBYTES)
				n = MCLBYTES;
		} else
			MGET(m, M_NOWAIT, MT_DATA);

		if(m == NULL)
			goto drop;

		last->m_next = m;
		last = m;

		bcopy(buf, m->m_data, n);
		buf += n;
		tlen -= n;
		m->m_len = n;
		m0->m_pkthdr.len += n;
	}

	return (m0);

  drop:
	m_freem(m0);
	return NULL;
}

/*
 * Send an indication to the user.
 */
static void
ng_ccatm_send_user(struct ccuser *user, void *uarg, u_int op,
    void *val, size_t len)
{
	struct cchook *hd = uarg;
	struct mbuf *m;
	struct ccatm_op	h;
	int error;

	h.op = op;
	m = pack_buf(&h, sizeof(h), val, len);
	if (m == NULL)
		return;

	NG_SEND_DATA_ONLY(error, hd->hook, m);
	if (error != 0)
		printf("%s: error=%d\n", __func__, error);
}

/*
 * Send a response to the user.
 */
static void
ng_ccatm_respond_user(struct ccuser *user, void *uarg, int err, u_int data,
    void *val, size_t len)
{
	struct cchook *hd = uarg;
	struct mbuf *m;
	struct {
		struct ccatm_op	op;
		struct atm_resp resp;
	} resp;
	int error;

	resp.op.op = ATMOP_RESP;
	resp.resp.resp = err;
	resp.resp.data = data;
	m = pack_buf(&resp, sizeof(resp), val, len);
	if (m == NULL)
		return;

	NG_SEND_DATA_ONLY(error, hd->hook, m);
	if (error != 0)
		printf("%s: error=%d\n", __func__, error);
}

/*
 * Receive data from UNI.
 */
static int
ng_ccatm_rcvuni(hook_p hook, item_p item)
{
	struct cchook *hd = NG_HOOK_PRIVATE(hook);
	struct uni_msg *msg;
	struct uni_arg arg;
	struct mbuf *m;
	int err;

	NGI_GET_M(item, m);
	NG_FREE_ITEM(item);

	if ((err = uni_msg_unpack_mbuf(m, &msg)) != 0) {
		m_freem(m);
		return (err);
	}
	m_freem(m);

	if (uni_msg_len(msg) < sizeof(arg)) {
		printf("%s: packet too short\n", __func__);
		uni_msg_destroy(msg);
		return (EINVAL);
	}

	bcopy(msg->b_rptr, &arg, sizeof(arg));
	msg->b_rptr += sizeof(arg);

	if (arg.sig == UNIAPI_ERROR) {
		if (uni_msg_len(msg) != sizeof(struct uniapi_error)) {
			printf("%s: bad UNIAPI_ERROR size %zu\n", __func__,
			    uni_msg_len(msg));
			uni_msg_destroy(msg);
			return (EINVAL);
		}
		err = cc_uni_response(hd->inst, arg.cookie,
		    ((struct uniapi_error *)msg->b_rptr)->reason,
		    ((struct uniapi_error *)msg->b_rptr)->state);
		uni_msg_destroy(msg);
	} else
		err = cc_uni_signal(hd->inst, arg.cookie, arg.sig, msg);

	cc_work(hd->node->data);
	return (err);
}

/*
 * Uarg is the port's uarg.
 */
static void
ng_ccatm_send_uni(struct ccconn *conn, void *uarg, u_int op, u_int cookie,
    struct uni_msg *msg)
{
	struct cchook *hd = uarg;
	struct uni_arg arg;
	struct mbuf *m;
	int error;

	arg.sig = op;
	arg.cookie = cookie;

	m = uni_msg_pack_mbuf(msg, &arg, sizeof(arg));
	uni_msg_destroy(msg);
	if (m == NULL)
		return;

	NG_SEND_DATA_ONLY(error, hd->hook, m);
	if (error != 0)
		printf("%s: error=%d\n", __func__, error);
}

/*
 * Send a global message to the UNI
 */
static void
ng_ccatm_send_uni_glob(struct ccport *port, void *uarg, u_int op, u_int cookie,
    struct uni_msg *msg)
{
	struct cchook *hd = uarg;
	struct uni_arg arg;
	struct mbuf *m;
	int error;

	arg.sig = op;
	arg.cookie = cookie;

	m = uni_msg_pack_mbuf(msg, &arg, sizeof(arg));
	if (msg != NULL)
		uni_msg_destroy(msg);
	if (m == NULL)
		return;

	NG_SEND_DATA_ONLY(error, hd->hook, m);
	if (error != 0)
		printf("%s: error=%d\n", __func__, error);
}
/*
 * Receive from ILMID
 */
static int
ng_ccatm_rcvmanage(hook_p hook, item_p item)
{
	NG_FREE_ITEM(item);
	return (0);
}

static int
ng_ccatm_rcvdump(hook_p hook, item_p item)
{
	NG_FREE_ITEM(item);
	return (0);
}

static void
ng_ccatm_log(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vprintf(fmt, ap);
	printf("\n");
	va_end(ap);
}

/*
 * Loading and unloading of node type
 */
static int
ng_ccatm_mod_event(module_t mod, int event, void *data)
{
	int error = 0;

	switch (event) {

	  case MOD_LOAD:
		break;

	  case MOD_UNLOAD:
		break;

	  default:
		error = EOPNOTSUPP;
		break;
	}
	return (error);
}
