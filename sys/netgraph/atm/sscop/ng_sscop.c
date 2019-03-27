/*-
 * Copyright (c) 2001-2003
 *	Fraunhofer Institute for Open Communication Systems (FhG Fokus).
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
 * THIS SOFTWARE AND DOCUMENTATION IS PROVIDED BY FRAUNHOFER FOKUS
 * AND ITS CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL
 * FRAUNHOFER FOKUS OR ITS CONTRIBUTORS  BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Netgraph module for ITU-T Q.2110 SSCOP.
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
#include <sys/callout.h>
#include <sys/sbuf.h>
#include <sys/stdint.h>
#include <machine/stdarg.h>

#include <netgraph/ng_message.h>
#include <netgraph/netgraph.h>
#include <netgraph/ng_parse.h>
#include <netnatm/saal/sscopdef.h>
#include <netgraph/atm/ng_sscop.h>
#include <netgraph/atm/sscop/ng_sscop_cust.h>
#include <netnatm/saal/sscop.h>

#define DDD printf("%s: %d\n", __func__, __LINE__)

#ifdef SSCOP_DEBUG
#define VERBOSE(P,M,F)							\
    do {								\
	if (sscop_getdebug((P)->sscop) & (M))				\
		sscop_verbose F ;					\
    } while(0)
#else
#define VERBOSE(P,M,F)
#endif

MALLOC_DEFINE(M_NG_SSCOP, "netgraph_sscop", "netgraph sscop node");

MODULE_DEPEND(ng_sscop, ngatmbase, 1, 1, 1);

struct stats {
	uint64_t	in_packets;
	uint64_t	out_packets;
	uint64_t	aa_signals;
	uint64_t	errors;
	uint64_t	data_delivered;
	uint64_t	aa_dropped;
	uint64_t	maa_dropped;
	uint64_t	maa_signals;
	uint64_t	in_dropped;
	uint64_t	out_dropped;
};

/*
 * Private data
 */
struct priv {
	hook_p		upper;		/* SAAL interface */
	hook_p		lower;		/* AAL5 interface */
	hook_p		manage;		/* management interface */

	struct sscop	*sscop;		/* sscop state */
	int		enabled;	/* whether the protocol is enabled */
	int		flow;		/* flow control states */
	struct stats	stats;		/* sadistics */
};

/*
 * Parse PARAM type
 */
static const struct ng_parse_struct_field ng_sscop_param_type_info[] = 
    NG_SSCOP_PARAM_INFO;

static const struct ng_parse_type ng_sscop_param_type = {
	&ng_parse_struct_type,
	ng_sscop_param_type_info
};

/*
 * Parse a SET PARAM type.
 */
static const struct ng_parse_struct_field ng_sscop_setparam_type_info[] =
    NG_SSCOP_SETPARAM_INFO;

static const struct ng_parse_type ng_sscop_setparam_type = {
	&ng_parse_struct_type,
	ng_sscop_setparam_type_info,
};

/*
 * Parse a SET PARAM response
 */
static const struct ng_parse_struct_field ng_sscop_setparam_resp_type_info[] =
    NG_SSCOP_SETPARAM_RESP_INFO;

static const struct ng_parse_type ng_sscop_setparam_resp_type = {
	&ng_parse_struct_type,
	ng_sscop_setparam_resp_type_info,
};

static const struct ng_cmdlist ng_sscop_cmdlist[] = {
	{
	  NGM_SSCOP_COOKIE,
	  NGM_SSCOP_GETPARAM,
	  "getparam",
	  NULL,
	  &ng_sscop_param_type
	},
	{
	  NGM_SSCOP_COOKIE,
	  NGM_SSCOP_SETPARAM,
	  "setparam",
	  &ng_sscop_setparam_type,
	  &ng_sscop_setparam_resp_type
	},
	{
	  NGM_SSCOP_COOKIE,
	  NGM_SSCOP_ENABLE,
	  "enable",
	  NULL,
	  NULL
	},
	{
	  NGM_SSCOP_COOKIE,
	  NGM_SSCOP_DISABLE,
	  "disable",
	  NULL,
	  NULL
	},
	{
	  NGM_SSCOP_COOKIE,
	  NGM_SSCOP_GETDEBUG,
	  "getdebug",
	  NULL,
	  &ng_parse_hint32_type
	},
	{
	  NGM_SSCOP_COOKIE,
	  NGM_SSCOP_SETDEBUG,
	  "setdebug",
	  &ng_parse_hint32_type,
	  NULL
	},
	{
	  NGM_SSCOP_COOKIE,
	  NGM_SSCOP_GETSTATE,
	  "getstate",
	  NULL,
	  &ng_parse_uint32_type
	},
	{ 0 }
};

static ng_constructor_t ng_sscop_constructor;
static ng_shutdown_t	ng_sscop_shutdown;
static ng_rcvmsg_t	ng_sscop_rcvmsg;
static ng_newhook_t	ng_sscop_newhook;
static ng_disconnect_t	ng_sscop_disconnect;
static ng_rcvdata_t	ng_sscop_rcvlower;
static ng_rcvdata_t	ng_sscop_rcvupper;
static ng_rcvdata_t	ng_sscop_rcvmanage;

static int ng_sscop_mod_event(module_t, int, void *);

static struct ng_type ng_sscop_typestruct = {
	.version =	NG_ABI_VERSION,
	.name =		NG_SSCOP_NODE_TYPE,
	.mod_event =	ng_sscop_mod_event,
	.constructor =	ng_sscop_constructor,
	.rcvmsg =	ng_sscop_rcvmsg,
	.shutdown =	ng_sscop_shutdown,
	.newhook =	ng_sscop_newhook,
	.rcvdata =	ng_sscop_rcvlower,
	.disconnect =	ng_sscop_disconnect,
	.cmdlist =	ng_sscop_cmdlist,
};
NETGRAPH_INIT(sscop, &ng_sscop_typestruct);

static void sscop_send_manage(struct sscop *, void *, enum sscop_maasig,
	struct SSCOP_MBUF_T *, u_int, u_int);
static void sscop_send_upper(struct sscop *, void *, enum sscop_aasig,
	struct SSCOP_MBUF_T *, u_int);
static void sscop_send_lower(struct sscop *, void *,
	struct SSCOP_MBUF_T *);
static void sscop_verbose(struct sscop *, void *, const char *, ...)
	__printflike(3, 4);

static const struct sscop_funcs sscop_funcs = {
	sscop_send_manage,
	sscop_send_upper,
	sscop_send_lower,
	sscop_verbose
};

static void
sscop_verbose(struct sscop *sscop, void *arg, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	printf("sscop(%p): ", sscop);
	vprintf(fmt, ap);
	va_end(ap);
	printf("\n");
}

/************************************************************/
/*
 * NODE MANAGEMENT
 */
static int
ng_sscop_constructor(node_p node)
{
	struct priv *p;

	p = malloc(sizeof(*p), M_NG_SSCOP, M_WAITOK | M_ZERO);

	if ((p->sscop = sscop_create(node, &sscop_funcs)) == NULL) {
		free(p, M_NG_SSCOP);
		return (ENOMEM);
	}
	NG_NODE_SET_PRIVATE(node, p);

	/* All data message received by the node are expected to change the
	 * node's state. Therefor we must ensure, that we have a writer lock. */
	NG_NODE_FORCE_WRITER(node);

	return (0);
}
static int
ng_sscop_shutdown(node_p node)
{
	struct priv *priv = NG_NODE_PRIVATE(node);

	sscop_destroy(priv->sscop);

	free(priv, M_NG_SSCOP);
	NG_NODE_SET_PRIVATE(node, NULL);

	NG_NODE_UNREF(node);

	return (0);
}

/************************************************************/
/*
 * CONTROL MESSAGES
 */
/*
 * Flow control message from upper layer.
 * This is very experimental:
 * If we get a message from the upper layer, that somebody has passed its
 * high water mark, we stop updating the receive window.
 * If we get a low watermark passed, then we raise the window up
 * to max - current.
 * If we get a queue status and it indicates a current below the
 * high watermark, we unstop window updates (if they are stopped) and
 * raise the window to highwater - current.
 */
static int
flow_upper(node_p node, struct ng_mesg *msg)
{
	struct ngm_queue_state *q;
	struct priv *priv = NG_NODE_PRIVATE(node);
	u_int window, space;

	if (msg->header.arglen != sizeof(struct ngm_queue_state))
		return (EINVAL);
	q = (struct ngm_queue_state *)msg->data;

	switch (msg->header.cmd) {

	  case NGM_HIGH_WATER_PASSED:
		if (priv->flow) {
			VERBOSE(priv, SSCOP_DBG_FLOW, (priv->sscop, priv,
			    "flow control stopped"));
			priv->flow = 0;
		}
		break;

	  case NGM_LOW_WATER_PASSED:
		window = sscop_window(priv->sscop, 0);
		space = q->max_queuelen_packets - q->current;
		if (space > window) {
			VERBOSE(priv, SSCOP_DBG_FLOW, (priv->sscop, priv,
			    "flow control opened window by %u messages",
			    space - window));
			(void)sscop_window(priv->sscop, space - window);
		}
		priv->flow = 1;
		break;

	  case NGM_SYNC_QUEUE_STATE:
		if (q->high_watermark <= q->current)
			break;
		window = sscop_window(priv->sscop, 0);
		if (priv->flow)
			space = q->max_queuelen_packets - q->current;
		else
			space = q->high_watermark - q->current;
		if (space > window) {
			VERBOSE(priv, SSCOP_DBG_FLOW, (priv->sscop, priv,
			    "flow control opened window by %u messages",
			    space - window));
			(void)sscop_window(priv->sscop, space - window);
		}
		priv->flow = 1;
		break;

	  default:
		return (EINVAL);
	}
	return (0);
}

static int
flow_lower(node_p node, struct ng_mesg *msg)
{
	struct priv *priv = NG_NODE_PRIVATE(node);

	if (msg->header.arglen != sizeof(struct ngm_queue_state))
		return (EINVAL);

	switch (msg->header.cmd) {

	  case NGM_HIGH_WATER_PASSED:
		sscop_setbusy(priv->sscop, 1);
		break;

	  case NGM_LOW_WATER_PASSED:
		sscop_setbusy(priv->sscop, 1);
		break;

	  default:
		return (EINVAL);
	}
	return (0);
}

/*
 * Produce a readable status description
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

	if (priv->manage)
		sbuf_printf(&sbuf, "manage hook: %s connected to %s:%s\n",
		    NG_HOOK_NAME(priv->manage),
		    NG_NODE_NAME(NG_HOOK_NODE(NG_HOOK_PEER(priv->manage))),
		    NG_HOOK_NAME(NG_HOOK_PEER(priv->manage)));
	else
		sbuf_printf(&sbuf, "manage hook: <not connected>\n");

	sbuf_printf(&sbuf, "sscop state: %s\n",
	    !priv->enabled ? "<disabled>" :
	    sscop_statename(sscop_getstate(priv->sscop)));

	sbuf_printf(&sbuf, "input packets:  %ju\n",
	    (uintmax_t)priv->stats.in_packets);
	sbuf_printf(&sbuf, "input dropped:  %ju\n",
	    (uintmax_t)priv->stats.in_dropped);
	sbuf_printf(&sbuf, "output packets: %ju\n",
	    (uintmax_t)priv->stats.out_packets);
	sbuf_printf(&sbuf, "output dropped: %ju\n",
	    (uintmax_t)priv->stats.out_dropped);
	sbuf_printf(&sbuf, "aa signals:     %ju\n",
	    (uintmax_t)priv->stats.aa_signals);
	sbuf_printf(&sbuf, "aa dropped:     %ju\n",
	    (uintmax_t)priv->stats.aa_dropped);
	sbuf_printf(&sbuf, "maa signals:    %ju\n",
	    (uintmax_t)priv->stats.maa_signals);
	sbuf_printf(&sbuf, "maa dropped:    %ju\n",
	    (uintmax_t)priv->stats.maa_dropped);
	sbuf_printf(&sbuf, "errors:         %ju\n",
	    (uintmax_t)priv->stats.errors);
	sbuf_printf(&sbuf, "data delivered: %ju\n",
	    (uintmax_t)priv->stats.data_delivered);
	sbuf_printf(&sbuf, "window:         %u\n",
	    sscop_window(priv->sscop, 0));

	sbuf_finish(&sbuf);
	return (sbuf_len(&sbuf));
}


/*
 * Control message received.
 */
static int
ng_sscop_rcvmsg(node_p node, item_p item, hook_p lasthook)
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

	  case NGM_FLOW_COOKIE:
		if (priv->enabled && lasthook != NULL) {
			if (lasthook == priv->upper)
				error = flow_upper(node, msg);
			else if (lasthook == priv->lower)
				error = flow_lower(node, msg);
		}
		break;

	  case NGM_SSCOP_COOKIE:
		switch (msg->header.cmd) {

		  case NGM_SSCOP_GETPARAM:
		    {
			struct sscop_param *p;

			NG_MKRESPONSE(resp, msg, sizeof(*p), M_NOWAIT);
			if (resp == NULL) {
				error = ENOMEM;
				break;
			}
			p = (struct sscop_param *)resp->data;
			sscop_getparam(priv->sscop, p);
			break;
		    }

		  case NGM_SSCOP_SETPARAM:
		    {
			struct ng_sscop_setparam *arg;
			struct ng_sscop_setparam_resp *p;

			if (msg->header.arglen != sizeof(*arg)) {
				error = EINVAL;
				break;
			}
			if (priv->enabled) {
				error = EISCONN;
				break;
			}
			arg = (struct ng_sscop_setparam *)msg->data;
			NG_MKRESPONSE(resp, msg, sizeof(*p), M_NOWAIT);
			if (resp == NULL) {
				error = ENOMEM;
				break;
			}
			p = (struct ng_sscop_setparam_resp *)resp->data;
			p->mask = arg->mask;
			p->error = sscop_setparam(priv->sscop,
			    &arg->param, &p->mask);
			break;
		    }

		  case NGM_SSCOP_ENABLE:
			if (msg->header.arglen != 0) {
				error = EINVAL;
				break;
			}
			if (priv->enabled) {
				error = EBUSY;
				break;
			}
			priv->enabled = 1;
			priv->flow = 1;
			memset(&priv->stats, 0, sizeof(priv->stats));
			break;

		  case NGM_SSCOP_DISABLE:
			if (msg->header.arglen != 0) {
				error = EINVAL;
				break;
			}
			if (!priv->enabled) {
				error = ENOTCONN;
				break;
			}
			priv->enabled = 0;
			sscop_reset(priv->sscop);
			break;

		  case NGM_SSCOP_GETDEBUG:
			if (msg->header.arglen != 0) {
				error = EINVAL;
				break;
			}
			NG_MKRESPONSE(resp, msg, sizeof(u_int32_t), M_NOWAIT);
			if(resp == NULL) {
				error = ENOMEM;
				break;
			}
			*(u_int32_t *)resp->data = sscop_getdebug(priv->sscop);
			break;

		  case NGM_SSCOP_SETDEBUG:
			if (msg->header.arglen != sizeof(u_int32_t)) {
				error = EINVAL;
				break;
			}
			sscop_setdebug(priv->sscop, *(u_int32_t *)msg->data);
			break;

		  case NGM_SSCOP_GETSTATE:
			if (msg->header.arglen != 0) {
				error = EINVAL;
				break;
			}
			NG_MKRESPONSE(resp, msg, sizeof(u_int32_t), M_NOWAIT);
			if(resp == NULL) {
				error = ENOMEM;
				break;
			}
			*(u_int32_t *)resp->data =
			    priv->enabled ? (sscop_getstate(priv->sscop) + 1)
			                  : 0;
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
ng_sscop_newhook(node_p node, hook_p hook, const char *name)
{
	struct priv *priv = NG_NODE_PRIVATE(node);

	if(strcmp(name, "upper") == 0) {
		priv->upper = hook;
		NG_HOOK_SET_RCVDATA(hook, ng_sscop_rcvupper);
	} else if(strcmp(name, "lower") == 0) {
		priv->lower = hook;
	} else if(strcmp(name, "manage") == 0) {
		priv->manage = hook;
		NG_HOOK_SET_RCVDATA(hook, ng_sscop_rcvmanage);
	} else
		return EINVAL;

	return 0;
}
static int
ng_sscop_disconnect(hook_p hook)
{
	node_p node = NG_HOOK_NODE(hook);
	struct priv *priv = NG_NODE_PRIVATE(node);

	if(hook == priv->upper)
		priv->upper = NULL;
	else if(hook == priv->lower)
		priv->lower = NULL;
	else if(hook == priv->manage)
		priv->manage = NULL;

	if(NG_NODE_NUMHOOKS(node) == 0) {
		if(NG_NODE_IS_VALID(node))
			ng_rmnode_self(node);
	} else {
		/*
		 * Imply a release request, if the upper layer is
		 * disconnected.
		 */
		if(priv->upper == NULL && priv->lower != NULL &&
		   priv->enabled &&
		   sscop_getstate(priv->sscop) != SSCOP_IDLE) {
			sscop_aasig(priv->sscop, SSCOP_RELEASE_request,
			    NULL, 0);
		}
	}
	return 0;
}

/************************************************************/
/*
 * DATA
 */
static int
ng_sscop_rcvlower(hook_p hook, item_p item)
{
	struct priv *priv = NG_NODE_PRIVATE(NG_HOOK_NODE(hook));
	struct mbuf *m;

	if (!priv->enabled) {
		NG_FREE_ITEM(item);
		return EINVAL;
	}

	/*
	 * If we are disconnected at the upper layer and in the IDLE
	 * state, drop any incoming packet.
	 */
	if (priv->upper != NULL || sscop_getstate(priv->sscop) != SSCOP_IDLE) {
		NGI_GET_M(item, m);
		priv->stats.in_packets++;
		sscop_input(priv->sscop, m);
	} else {
		priv->stats.in_dropped++;
	}
	NG_FREE_ITEM(item);

	return (0);
}

static void
sscop_send_lower(struct sscop *sscop, void *p, struct mbuf *m)
{
	node_p node = (node_p)p;
	struct priv *priv = NG_NODE_PRIVATE(node);
	int error;

	if (priv->lower == NULL) {
		m_freem(m);
		priv->stats.out_dropped++;
		return;
	}

	priv->stats.out_packets++;
	NG_SEND_DATA_ONLY(error, priv->lower, m);
}

static int
ng_sscop_rcvupper(hook_p hook, item_p item)
{
	struct priv *priv = NG_NODE_PRIVATE(NG_HOOK_NODE(hook));
	struct sscop_arg a;
	struct mbuf *m;

	if (!priv->enabled) {
		NG_FREE_ITEM(item);
		return (EINVAL);
	}

	/*
	 * If the lower layer is not connected allow to proceed.
	 * The lower layer sending function will drop outgoing frames,
	 * and the sscop will timeout any establish requests.
	 */
	NGI_GET_M(item, m);
	NG_FREE_ITEM(item);

	if (!(m->m_flags & M_PKTHDR)) {
		printf("no pkthdr\n");
		m_freem(m);
		return (EINVAL);
	}
	if (m->m_len < (int)sizeof(a) && (m = m_pullup(m, sizeof(a))) == NULL)
		return (ENOBUFS);
	bcopy((caddr_t)mtod(m, struct sscop_arg *), &a, sizeof(a));
	m_adj(m, sizeof(a));

	return (sscop_aasig(priv->sscop, a.sig, m, a.arg));
}

static void
sscop_send_upper(struct sscop *sscop, void *p, enum sscop_aasig sig,
    struct SSCOP_MBUF_T *m, u_int arg)
{
	node_p node = (node_p)p;
	struct priv *priv = NG_NODE_PRIVATE(node);
	int error;
	struct sscop_arg *a;

	if (sig == SSCOP_DATA_indication && priv->flow)
		sscop_window(priv->sscop, 1);

	if (priv->upper == NULL) {
		if (m != NULL)
			m_freem(m);
		priv->stats.aa_dropped++;
		return;
	}

	priv->stats.aa_signals++;
	if (sig == SSCOP_DATA_indication)
		priv->stats.data_delivered++;

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

	NG_SEND_DATA_ONLY(error, priv->upper, m);
}

static int
ng_sscop_rcvmanage(hook_p hook, item_p item)
{
	struct priv *priv = NG_NODE_PRIVATE(NG_HOOK_NODE(hook));
	struct sscop_marg a;
	struct mbuf *m;

	if (!priv->enabled) {
		NG_FREE_ITEM(item);
		return (EINVAL);
	}

	NGI_GET_M(item, m);
	NG_FREE_ITEM(item);

	if (m->m_len < (int)sizeof(a) && (m = m_pullup(m, sizeof(a))) == NULL)
		return (ENOBUFS);
	bcopy((caddr_t)mtod(m, struct sscop_arg *), &a, sizeof(a));
	m_adj(m, sizeof(a));

	return (sscop_maasig(priv->sscop, a.sig, m));
}

static void
sscop_send_manage(struct sscop *sscop, void *p, enum sscop_maasig sig,
    struct SSCOP_MBUF_T *m, u_int err, u_int cnt)
{
	node_p node = (node_p)p;
	struct priv *priv = NG_NODE_PRIVATE(node);
	int error;
	struct sscop_merr *e;
	struct sscop_marg *a;

	if (priv->manage == NULL) {
		if (m != NULL)
			m_freem(m);
		priv->stats.maa_dropped++;
		return;
	}

	if (sig == SSCOP_MERROR_indication) {
		MGETHDR(m, M_NOWAIT, MT_DATA);
		if (m == NULL)
			return;
		m->m_len = sizeof(*e);
		m->m_pkthdr.len = m->m_len;
		e = mtod(m, struct sscop_merr *);
		e->sig = sig;
		e->err = err;
		e->cnt = cnt;
		priv->stats.errors++;
	} else if (m == NULL) {
		MGETHDR(m, M_NOWAIT, MT_DATA);
		if (m == NULL)
			return;
		m->m_len = sizeof(*a);
		m->m_pkthdr.len = m->m_len;
		a = mtod(m, struct sscop_marg *);
		a->sig = sig;
		priv->stats.maa_signals++;
	} else {
		M_PREPEND(m, sizeof(*a), M_NOWAIT);
		if (m == NULL)
			return;
		a = mtod(m, struct sscop_marg *);
		a->sig = sig;
		priv->stats.maa_signals++;
	}

	NG_SEND_DATA_ONLY(error, priv->manage, m);
}

/************************************************************/
/*
 * INITIALISATION
 */

/*
 * Loading and unloading of node type
 */
static int
ng_sscop_mod_event(module_t mod, int event, void *data)
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
