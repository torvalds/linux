/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001-2003
 *	Fraunhofer Institute for Open Communication Systems (FhG Fokus).
 * 	All rights reserved.
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
 * Author: Hartmut Brandt <harti@freebsd.org>
 *
 * Netgraph module for ITU-T Q.2120 UNI SSCF.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/errno.h>
#include <sys/syslog.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sbuf.h>
#include <machine/stdarg.h>

#include <netgraph/ng_message.h>
#include <netgraph/netgraph.h>
#include <netgraph/ng_parse.h>
#include <netnatm/saal/sscopdef.h>
#include <netnatm/saal/sscfudef.h>
#include <netgraph/atm/ng_sscop.h>
#include <netgraph/atm/ng_sscfu.h>
#include <netgraph/atm/sscfu/ng_sscfu_cust.h>
#include <netnatm/saal/sscfu.h>

MALLOC_DEFINE(M_NG_SSCFU, "netgraph_sscfu", "netgraph uni sscf node");

MODULE_DEPEND(ng_sscfu, ngatmbase, 1, 1, 1);

/*
 * Private data
 */
struct priv {
	hook_p		upper;	/* SAAL interface */
	hook_p		lower;	/* SSCOP interface */
	struct sscfu	*sscf;	/* the instance */
	int		enabled;
};

/*
 * PARSING
 */
/*
 * Parse PARAM type
 */
static const struct ng_parse_struct_field ng_sscop_param_type_info[] =
    NG_SSCOP_PARAM_INFO;

static const struct ng_parse_type ng_sscop_param_type = {
	&ng_parse_struct_type,
	ng_sscop_param_type_info
};

static const struct ng_parse_struct_field ng_sscfu_getdefparam_type_info[] =
    NG_SSCFU_GETDEFPARAM_INFO;

static const struct ng_parse_type ng_sscfu_getdefparam_type = {
	&ng_parse_struct_type,
	ng_sscfu_getdefparam_type_info
};


static const struct ng_cmdlist ng_sscfu_cmdlist[] = {
	{
	  NGM_SSCFU_COOKIE,
	  NGM_SSCFU_GETDEFPARAM,
	  "getdefparam",
	  NULL,
	  &ng_sscfu_getdefparam_type
	},
	{
	  NGM_SSCFU_COOKIE,
	  NGM_SSCFU_ENABLE,
	  "enable",
	  NULL,
	  NULL
	},
	{
	  NGM_SSCFU_COOKIE,
	  NGM_SSCFU_DISABLE,
	  "disable",
	  NULL,
	  NULL
	},
	{
	  NGM_SSCFU_COOKIE,
	  NGM_SSCFU_GETDEBUG,
	  "getdebug",
	  NULL,
	  &ng_parse_hint32_type
	},
	{
	  NGM_SSCFU_COOKIE,
	  NGM_SSCFU_SETDEBUG,
	  "setdebug",
	  &ng_parse_hint32_type,
	  NULL
	},
	{
	  NGM_SSCFU_COOKIE,
	  NGM_SSCFU_GETSTATE,
	  "getstate",
	  NULL,
	  &ng_parse_uint32_type
	},
	{ 0 }
};

static ng_constructor_t ng_sscfu_constructor;
static ng_shutdown_t	ng_sscfu_shutdown;
static ng_rcvmsg_t	ng_sscfu_rcvmsg;
static ng_newhook_t	ng_sscfu_newhook;
static ng_disconnect_t	ng_sscfu_disconnect;
static ng_rcvdata_t	ng_sscfu_rcvupper;
static ng_rcvdata_t	ng_sscfu_rcvlower;

static int ng_sscfu_mod_event(module_t, int, void *);

static struct ng_type ng_sscfu_typestruct = {
	.version =	NG_ABI_VERSION,
	.name =		NG_SSCFU_NODE_TYPE,
	.mod_event =	ng_sscfu_mod_event,
	.constructor =	ng_sscfu_constructor,
	.rcvmsg =	ng_sscfu_rcvmsg,
	.shutdown =	ng_sscfu_shutdown,
	.newhook =	ng_sscfu_newhook,
	.rcvdata =	ng_sscfu_rcvupper,
	.disconnect =	ng_sscfu_disconnect,
	.cmdlist =	ng_sscfu_cmdlist,
};
NETGRAPH_INIT(sscfu, &ng_sscfu_typestruct);

static void sscfu_send_upper(struct sscfu *, void *, enum saal_sig,
	struct mbuf *);
static void sscfu_send_lower(struct sscfu *, void *, enum sscop_aasig,
	struct mbuf *, u_int);
static void sscfu_window(struct sscfu *, void *, u_int);
static void sscfu_verbose(struct sscfu *, void *, const char *, ...)
	__printflike(3, 4);

static const struct sscfu_funcs sscfu_funcs = {
	sscfu_send_upper,
	sscfu_send_lower,
	sscfu_window,
	sscfu_verbose
};

/************************************************************/
/*
 * CONTROL MESSAGES
 */
static int
text_status(node_p node, struct priv *priv, char *arg, u_int len)
{
	struct sbuf sbuf;

	sbuf_new(&sbuf, arg, len, 0);

	if (priv->upper)
		sbuf_printf(&sbuf, "upper hook: %s connected to %s:%s\n",
		    NG_HOOK_NAME(priv->upper),
		    NG_NODE_NAME(NG_HOOK_NODE(NG_HOOK_PEER(priv->upper))),
		    NG_HOOK_NAME(NG_HOOK_PEER(priv->upper)));
	else
		sbuf_printf(&sbuf, "upper hook: <not connected>\n");

	if (priv->lower)
		sbuf_printf(&sbuf, "lower hook: %s connected to %s:%s\n",
		    NG_HOOK_NAME(priv->lower),
		    NG_NODE_NAME(NG_HOOK_NODE(NG_HOOK_PEER(priv->lower))),
		    NG_HOOK_NAME(NG_HOOK_PEER(priv->lower)));
	else
		sbuf_printf(&sbuf, "lower hook: <not connected>\n");

	sbuf_printf(&sbuf, "sscf state: %s\n",
	    priv->enabled == 0 ? "<disabled>" :
	    sscfu_statename(sscfu_getstate(priv->sscf)));

	sbuf_finish(&sbuf);
	return (sbuf_len(&sbuf));
}

static int
ng_sscfu_rcvmsg(node_p node, item_p item, hook_p lasthook)
{
	struct priv *priv = NG_NODE_PRIVATE(node);
	struct ng_mesg *resp = NULL;
	struct ng_mesg *msg;
	int error = 0;

	NGI_GET_MSG(item, msg);

	switch (msg->header.typecookie) {

	  case NGM_GENERIC_COOKIE:
		switch (msg->header.cmd) {

		  case NGM_TEXT_STATUS:
			NG_MKRESPONSE(resp, msg, NG_TEXTRESPONSE, M_NOWAIT);
			if (resp == NULL) {
				error = ENOMEM;
				break;
			}
			resp->header.arglen = text_status(node, priv,
			    (char *)resp->data, resp->header.arglen) + 1;
			break;

		  default:
			error = EINVAL;
			break;
		}
		break;

	  case NGM_SSCFU_COOKIE:
		switch (msg->header.cmd) {

		  case NGM_SSCFU_GETDEFPARAM:
		    {
			struct ng_sscfu_getdefparam *p;

			if (msg->header.arglen != 0) {
				error = EINVAL;
				break;
			}
			NG_MKRESPONSE(resp, msg, sizeof(*p), M_NOWAIT);
			if (resp == NULL) {
				error = ENOMEM;
				break;
			}
			p = (struct ng_sscfu_getdefparam *)resp->data;
			p->mask = sscfu_getdefparam(&p->param);
			break;
		    }

		  case NGM_SSCFU_ENABLE:
			if (msg->header.arglen != 0) {
				error = EINVAL;
				break;
			}
			if (priv->enabled) {
				error = EISCONN;
				break;
			}
			priv->enabled = 1;
			break;

		  case NGM_SSCFU_DISABLE:
			if (msg->header.arglen != 0) {
				error = EINVAL;
				break;
			}
			if (!priv->enabled) {
				error = ENOTCONN;
				break;
			}
			priv->enabled = 0;
			sscfu_reset(priv->sscf);
			break;

		  case NGM_SSCFU_GETSTATE:
			if (msg->header.arglen != 0) {
				error = EINVAL;
				break;
			}
			NG_MKRESPONSE(resp, msg, sizeof(uint32_t), M_NOWAIT);
			if(resp == NULL) {
				error = ENOMEM;
				break;
			}
			*(uint32_t *)resp->data =
			    priv->enabled ? (sscfu_getstate(priv->sscf) + 1)
			                  : 0;
			break;

		  case NGM_SSCFU_GETDEBUG:
			if (msg->header.arglen != 0) {
				error = EINVAL;
				break;
			}
			NG_MKRESPONSE(resp, msg, sizeof(uint32_t), M_NOWAIT);
			if(resp == NULL) {
				error = ENOMEM;
				break;
			}
			*(uint32_t *)resp->data = sscfu_getdebug(priv->sscf);
			break;

		  case NGM_SSCFU_SETDEBUG:
			if (msg->header.arglen != sizeof(uint32_t)) {
				error = EINVAL;
				break;
			}
			sscfu_setdebug(priv->sscf, *(uint32_t *)msg->data);
			break;

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

/************************************************************/
/*
 * HOOK MANAGEMENT
 */
static int
ng_sscfu_newhook(node_p node, hook_p hook, const char *name)
{
	struct priv *priv = NG_NODE_PRIVATE(node);

	if (strcmp(name, "upper") == 0)
		priv->upper = hook;
	else if (strcmp(name, "lower") == 0) {
		priv->lower = hook;
		NG_HOOK_SET_RCVDATA(hook, ng_sscfu_rcvlower);
	} else
		return (EINVAL);
	return (0);
}

static int
ng_sscfu_disconnect(hook_p hook)
{
	node_p node = NG_HOOK_NODE(hook);
	struct priv *priv = NG_NODE_PRIVATE(node);

	if (hook == priv->upper)
		priv->upper = NULL;
	else if (hook == priv->lower)
		priv->lower = NULL;
	else {
		log(LOG_ERR, "bogus hook");
		return (EINVAL);
	}

	if (NG_NODE_NUMHOOKS(node) == 0) {
		if (NG_NODE_IS_VALID(node))
			ng_rmnode_self(node);
	} else {
		/*
		 * Because there are no timeouts reset the protocol
		 * if the lower layer is disconnected.
		 */
		if (priv->lower == NULL &&
		    priv->enabled &&
		    sscfu_getstate(priv->sscf) != SSCFU_RELEASED)
			sscfu_reset(priv->sscf);
	}
	return (0);
}

/************************************************************/
/*
 * DATA
 */
static int
ng_sscfu_rcvupper(hook_p hook, item_p item)
{
	node_p node = NG_HOOK_NODE(hook);
	struct priv *priv = NG_NODE_PRIVATE(node);
	struct mbuf *m;
	struct sscfu_arg a;

	if (!priv->enabled || priv->lower == NULL) {
		NG_FREE_ITEM(item);
		return (0);
	}

	NGI_GET_M(item, m);
	NG_FREE_ITEM(item);

	if (!(m->m_flags & M_PKTHDR)) {
		printf("no pkthdr\n");
		m_freem(m);
		return (EINVAL);
	}
	if (m->m_len < (int)sizeof(a) && (m = m_pullup(m, sizeof(a))) == NULL)
		return (ENOMEM);
	bcopy((caddr_t)mtod(m, struct sscfu_arg *), &a, sizeof(a));
	m_adj(m, sizeof(a));

	return (sscfu_saalsig(priv->sscf, a.sig, m));
}

static void
sscfu_send_upper(struct sscfu *sscf, void *p, enum saal_sig sig, struct mbuf *m)
{
	node_p node = (node_p)p;
	struct priv *priv = NG_NODE_PRIVATE(node);
	int error;
	struct sscfu_arg *a;

	if (priv->upper == NULL) {
		if (m != NULL)
			m_freem(m);
		return;
	}
	if (m == NULL) {
		MGETHDR(m, M_NOWAIT, MT_DATA);
		if (m == NULL)
			return;
		m->m_len = sizeof(struct sscfu_arg);
		m->m_pkthdr.len = m->m_len;
	} else {
		M_PREPEND(m, sizeof(struct sscfu_arg), M_NOWAIT);
		if (m == NULL)
			return;
	}
	a = mtod(m, struct sscfu_arg *);
	a->sig = sig;

	NG_SEND_DATA_ONLY(error, priv->upper, m);
}

static int
ng_sscfu_rcvlower(hook_p hook, item_p item)
{
	node_p node = NG_HOOK_NODE(hook);
	struct priv *priv = NG_NODE_PRIVATE(node);
	struct mbuf *m;
	struct sscop_arg a;

	if (!priv->enabled || priv->upper == NULL) {
		NG_FREE_ITEM(item);
		return (0);
	}

	NGI_GET_M(item, m);
	NG_FREE_ITEM(item);

	if (!(m->m_flags & M_PKTHDR)) {
		printf("no pkthdr\n");
		m_freem(m);
		return (EINVAL);
	}

	/*
	 * Strip of the SSCOP header.
	 */
	if (m->m_len < (int)sizeof(a) && (m = m_pullup(m, sizeof(a))) == NULL)
		return (ENOMEM);
	bcopy((caddr_t)mtod(m, struct sscop_arg *), &a, sizeof(a));
	m_adj(m, sizeof(a));

	sscfu_input(priv->sscf, a.sig, m, a.arg);

	return (0);
}

static void
sscfu_send_lower(struct sscfu *sscf, void *p, enum sscop_aasig sig,
    struct mbuf *m, u_int arg)
{
	node_p node = (node_p)p;
	struct priv *priv = NG_NODE_PRIVATE(node);
	int error;
	struct sscop_arg *a;

	if (priv->lower == NULL) {
		if (m != NULL)
			m_freem(m);
		return;
	}
	if (m == NULL) {
		MGETHDR(m, M_NOWAIT, MT_DATA);
		if (m == NULL)
			return;
		m->m_len = sizeof(struct sscop_arg);
		m->m_pkthdr.len = m->m_len;
	} else {
		M_PREPEND(m, sizeof(struct sscop_arg), M_NOWAIT);
		if (m == NULL)
			return;
	}
	a = mtod(m, struct sscop_arg *);
	a->sig = sig;
	a->arg = arg;

	NG_SEND_DATA_ONLY(error, priv->lower, m);
}

/*
 * Window is handled by ng_sscop so make this a NOP.
 */
static void
sscfu_window(struct sscfu *sscfu, void *arg, u_int w)
{
}

/************************************************************/
/*
 * NODE MANAGEMENT
 */
static int
ng_sscfu_constructor(node_p node)
{
	struct priv *priv;

	priv = malloc(sizeof(*priv), M_NG_SSCFU, M_WAITOK | M_ZERO);

	if ((priv->sscf = sscfu_create(node, &sscfu_funcs)) == NULL) {
		free(priv, M_NG_SSCFU);
		return (ENOMEM);
	}

	NG_NODE_SET_PRIVATE(node, priv);

	return (0);
}

static int
ng_sscfu_shutdown(node_p node)
{
	struct priv *priv = NG_NODE_PRIVATE(node);

	sscfu_destroy(priv->sscf);

	free(priv, M_NG_SSCFU);
	NG_NODE_SET_PRIVATE(node, NULL);

	NG_NODE_UNREF(node);

	return (0);
}

static void
sscfu_verbose(struct sscfu *sscfu, void *arg, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	printf("sscfu(%p): ", sscfu);
	vprintf(fmt, ap);
	va_end(ap);
	printf("\n");
}

/************************************************************/
/*
 * INITIALISATION
 */
/*
 * Loading and unloading of node type
 */
static int
ng_sscfu_mod_event(module_t mod, int event, void *data)
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
