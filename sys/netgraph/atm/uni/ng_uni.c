/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001-2003
 *	Fraunhofer Institute for Open Communication Systems (FhG Fokus).
 * 	All rights reserved.
 *
 * Author: Hartmut Brandt <harti@freebsd.org>
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
 * Netgraph module for ATM-Forum UNI 4.0 signalling
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
#include <machine/stdarg.h>

#include <netgraph/ng_message.h>
#include <netgraph/netgraph.h>
#include <netgraph/ng_parse.h>
#include <netnatm/unimsg.h>
#include <netnatm/msg/unistruct.h>
#include <netgraph/atm/ngatmbase.h>
#include <netnatm/saal/sscopdef.h>
#include <netnatm/saal/sscfudef.h>
#include <netgraph/atm/uni/ng_uni_cust.h>
#include <netnatm/sig/uni.h>
#include <netnatm/sig/unisig.h>
#include <netgraph/atm/ng_sscop.h>
#include <netgraph/atm/ng_sscfu.h>
#include <netgraph/atm/ng_uni.h>

static MALLOC_DEFINE(M_NG_UNI, "netgraph_uni_node", "netgraph uni node");
static MALLOC_DEFINE(M_UNI, "netgraph_uni_data", "uni protocol data");

MODULE_DEPEND(ng_uni, ngatmbase, 1, 1, 1);

/*
 * Private node data
 */
struct priv {
	hook_p	upper;
	hook_p	lower;
	struct uni *uni;
	int	enabled;
};

/* UNI CONFIG MASK */
static const struct ng_parse_struct_field ng_uni_config_mask_type_info[] =
	NGM_UNI_CONFIG_MASK_INFO;
static const struct ng_parse_type ng_uni_config_mask_type = {
	&ng_parse_struct_type,
	ng_uni_config_mask_type_info
};

/* UNI_CONFIG */
static const struct ng_parse_struct_field ng_uni_config_type_info[] =
	NGM_UNI_CONFIG_INFO;
static const struct ng_parse_type ng_uni_config_type = {
	&ng_parse_struct_type,
	ng_uni_config_type_info
};

/* SET CONFIG */
static const struct ng_parse_struct_field ng_uni_set_config_type_info[] =
	NGM_UNI_SET_CONFIG_INFO;
static const struct ng_parse_type ng_uni_set_config_type = {
	&ng_parse_struct_type,
	ng_uni_set_config_type_info
};

/*
 * Parse DEBUG
 */
static const struct ng_parse_fixedarray_info ng_uni_debuglevel_type_info =
    NGM_UNI_DEBUGLEVEL_INFO;
static const struct ng_parse_type ng_uni_debuglevel_type = {
	&ng_parse_fixedarray_type,
	&ng_uni_debuglevel_type_info
};
static const struct ng_parse_struct_field ng_uni_debug_type_info[] =
    NGM_UNI_DEBUG_INFO;
static const struct ng_parse_type ng_uni_debug_type = {
	&ng_parse_struct_type,
	ng_uni_debug_type_info
};

/*
 * Command list
 */
static const struct ng_cmdlist ng_uni_cmdlist[] = {
	{
	  NGM_UNI_COOKIE,
	  NGM_UNI_GETDEBUG,
	  "getdebug",
	  NULL,
	  &ng_uni_debug_type
	},
	{
	  NGM_UNI_COOKIE,
	  NGM_UNI_SETDEBUG,
	  "setdebug",
	  &ng_uni_debug_type,
	  NULL
	},
	{
	  NGM_UNI_COOKIE,
	  NGM_UNI_GET_CONFIG,
	  "get_config",
	  NULL,
	  &ng_uni_config_type
	},
	{
	  NGM_UNI_COOKIE,
	  NGM_UNI_SET_CONFIG,
	  "set_config",
	  &ng_uni_set_config_type,
	  &ng_uni_config_mask_type,
	},
	{
	  NGM_UNI_COOKIE,
	  NGM_UNI_ENABLE,
	  "enable",
	  NULL,
	  NULL,
	},
	{
	  NGM_UNI_COOKIE,
	  NGM_UNI_DISABLE,
	  "disable",
	  NULL,
	  NULL,
	},
	{
	  NGM_UNI_COOKIE,
	  NGM_UNI_GETSTATE,
	  "getstate",
	  NULL,
	  &ng_parse_uint32_type
	},
	{ 0 }
};

/*
 * Netgraph module data
 */
static ng_constructor_t ng_uni_constructor;
static ng_shutdown_t	ng_uni_shutdown;
static ng_rcvmsg_t	ng_uni_rcvmsg;
static ng_newhook_t	ng_uni_newhook;
static ng_disconnect_t	ng_uni_disconnect;
static ng_rcvdata_t	ng_uni_rcvlower;
static ng_rcvdata_t	ng_uni_rcvupper;

static int ng_uni_mod_event(module_t, int, void *);

static struct ng_type ng_uni_typestruct = {
	.version =	NG_ABI_VERSION,
	.name =		NG_UNI_NODE_TYPE,
	.mod_event =	ng_uni_mod_event,
	.constructor =	ng_uni_constructor,
	.rcvmsg =	ng_uni_rcvmsg,
	.shutdown =	ng_uni_shutdown,
	.newhook =	ng_uni_newhook,
	.rcvdata =	ng_uni_rcvlower,
	.disconnect =	ng_uni_disconnect,
	.cmdlist =	ng_uni_cmdlist,
};
NETGRAPH_INIT(uni, &ng_uni_typestruct);

static void uni_uni_output(struct uni *, void *, enum uni_sig, u_int32_t,
    struct uni_msg *);
static void uni_saal_output(struct uni *, void *, enum saal_sig,
    struct uni_msg *);
static void uni_verbose(struct uni *, void *, u_int, const char *, ...)
    __printflike(4, 5);
static void uni_do_status(struct uni *, void *, void *, const char *, ...)
    __printflike(4, 5);

static const struct uni_funcs uni_funcs = {
	uni_uni_output,
	uni_saal_output,
	uni_verbose,
	uni_do_status
};

/************************************************************/
/*
 * NODE MANAGEMENT
 */
static int
ng_uni_constructor(node_p node)
{
	struct priv *priv;

	priv = malloc(sizeof(*priv), M_NG_UNI, M_WAITOK | M_ZERO);

	if ((priv->uni = uni_create(node, &uni_funcs)) == NULL) {
		free(priv, M_NG_UNI);
		return (ENOMEM);
	}

	NG_NODE_SET_PRIVATE(node, priv);
	NG_NODE_FORCE_WRITER(node);

	return (0);
}

static int
ng_uni_shutdown(node_p node)
{
	struct priv *priv = NG_NODE_PRIVATE(node);

	uni_destroy(priv->uni);

	free(priv, M_NG_UNI);
	NG_NODE_SET_PRIVATE(node, NULL);

	NG_NODE_UNREF(node);

	return (0);
}

/************************************************************/
/*
 * CONTROL MESSAGES
 */
static void
uni_do_status(struct uni *uni, void *uarg, void *sbuf, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	sbuf_printf(sbuf, fmt, ap);
	va_end(ap);
}

static int
text_status(node_p node, struct priv *priv, char *buf, u_int len)
{
	struct sbuf sbuf;
	u_int f;

	sbuf_new(&sbuf, buf, len, 0);

	if (priv->lower != NULL)
		sbuf_printf(&sbuf, "lower hook: connected to %s:%s\n",
		    NG_NODE_NAME(NG_HOOK_NODE(NG_HOOK_PEER(priv->lower))),
		    NG_HOOK_NAME(NG_HOOK_PEER(priv->lower)));
	else
		sbuf_printf(&sbuf, "lower hook: <not connected>\n");

	if (priv->upper != NULL)
		sbuf_printf(&sbuf, "upper hook: connected to %s:%s\n",
		    NG_NODE_NAME(NG_HOOK_NODE(NG_HOOK_PEER(priv->upper))),
		    NG_HOOK_NAME(NG_HOOK_PEER(priv->upper)));
	else
		sbuf_printf(&sbuf, "upper hook: <not connected>\n");

	sbuf_printf(&sbuf, "debugging:");
	for (f = 0; f < UNI_MAXFACILITY; f++)
		if (uni_get_debug(priv->uni, f) != 0)
			sbuf_printf(&sbuf, " %s=%u", uni_facname(f),
			    uni_get_debug(priv->uni, f));
	sbuf_printf(&sbuf, "\n");

	if (priv->uni)
		uni_status(priv->uni, &sbuf);

	sbuf_finish(&sbuf);
	return (sbuf_len(&sbuf));
}

static int
ng_uni_rcvmsg(node_p node, item_p item, hook_p lasthook)
{
	struct priv *priv = NG_NODE_PRIVATE(node);
	struct ng_mesg *resp = NULL;
	struct ng_mesg *msg;
	int error = 0;
	u_int i;

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

	  case NGM_UNI_COOKIE:
		switch (msg->header.cmd) {

		  case NGM_UNI_SETDEBUG:
		    {
			struct ngm_uni_debug *arg;

			if (msg->header.arglen > sizeof(*arg)) {
				error = EINVAL;
				break;
			}
			arg = (struct ngm_uni_debug *)msg->data;
			for (i = 0; i < UNI_MAXFACILITY; i++)
				uni_set_debug(priv->uni, i, arg->level[i]);
			break;
		    }

		  case NGM_UNI_GETDEBUG:
		    {
			struct ngm_uni_debug *arg;

			NG_MKRESPONSE(resp, msg, sizeof(*arg), M_NOWAIT);
			if(resp == NULL) {
				error = ENOMEM;
				break;
			}
			arg = (struct ngm_uni_debug *)resp->data;
			for (i = 0; i < UNI_MAXFACILITY; i++)
				arg->level[i] = uni_get_debug(priv->uni, i);
			break;
		    }

		  case NGM_UNI_GET_CONFIG:
		    {
			struct uni_config *config;

			if (msg->header.arglen != 0) {
				error = EINVAL;
				break;
			}
			NG_MKRESPONSE(resp, msg, sizeof(*config), M_NOWAIT);
			if (resp == NULL) {
				error = ENOMEM;
				break;
			}
			config = (struct uni_config *)resp->data;
			uni_get_config(priv->uni, config);

			break;
		    }

		  case NGM_UNI_SET_CONFIG:
		    {
			struct ngm_uni_set_config *arg;
			struct ngm_uni_config_mask *mask;

			if (msg->header.arglen != sizeof(*arg)) {
				error = EINVAL;
				break;
			}
			arg = (struct ngm_uni_set_config *)msg->data;

			NG_MKRESPONSE(resp, msg, sizeof(*mask), M_NOWAIT);
			if (resp == NULL) {
				error = ENOMEM;
				break;
			}
			mask = (struct ngm_uni_config_mask *)resp->data;

			*mask = arg->mask;

			uni_set_config(priv->uni, &arg->config,
			    &mask->mask, &mask->popt_mask, &mask->option_mask);

			break;
		    }

		  case NGM_UNI_ENABLE:
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

		  case NGM_UNI_DISABLE:
			if (msg->header.arglen != 0) {
				error = EINVAL;
				break;
			}
			if (!priv->enabled) {
				error = ENOTCONN;
				break;
			}
			priv->enabled = 0;
			uni_reset(priv->uni);
			break;

		  case NGM_UNI_GETSTATE:
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
			    priv->enabled ? (uni_getcustate(priv->uni) + 1)
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
ng_uni_newhook(node_p node, hook_p hook, const char *name)
{
	struct priv *priv = NG_NODE_PRIVATE(node);

	if (strcmp(name, "lower") == 0) {
		priv->lower = hook;
	} else if(strcmp(name, "upper") == 0) {
		priv->upper = hook;
		NG_HOOK_SET_RCVDATA(hook, ng_uni_rcvupper);
	} else
		return EINVAL;

	return 0;
}

static int
ng_uni_disconnect(hook_p hook)
{
	node_p node = NG_HOOK_NODE(hook);
	struct priv *priv = NG_NODE_PRIVATE(node);

	if(hook == priv->lower)
		priv->lower = NULL;
	else if(hook == priv->upper)
		priv->upper = NULL;
	else
		printf("%s: bogus hook %s\n", __func__, NG_HOOK_NAME(hook));

	if (NG_NODE_NUMHOOKS(node) == 0) {
		if (NG_NODE_IS_VALID(node))
			ng_rmnode_self(node);
	}

	return (0);
}

/************************************************************/
/*
 * DATA
 */
/*
 * Receive signal from USER.
 *
 * Repackage the data into one large buffer.
 */
static int
ng_uni_rcvupper(hook_p hook, item_p item)
{
	node_p node = NG_HOOK_NODE(hook);
	struct priv *priv = NG_NODE_PRIVATE(node);
	struct mbuf *m;
	struct uni_arg arg;
	struct uni_msg *msg;
	int error;

	if (!priv->enabled) {
		NG_FREE_ITEM(item);
		return (ENOTCONN);
	}

	NGI_GET_M(item, m);
	NG_FREE_ITEM(item);

	if ((error = uni_msg_unpack_mbuf(m, &msg)) != 0) {
		m_freem(m);
		return (error);
	}
	m_freem(m);

	if (uni_msg_len(msg) < sizeof(arg)) {
		printf("%s: packet too short\n", __func__);
		uni_msg_destroy(msg);
		return (EINVAL);
	}

	bcopy(msg->b_rptr, &arg, sizeof(arg));
	msg->b_rptr += sizeof(arg);

	if (arg.sig >= UNIAPI_MAXSIG) {
		printf("%s: bogus signal\n", __func__);
		uni_msg_destroy(msg);
		return (EINVAL);
	}
	uni_uni_input(priv->uni, arg.sig, arg.cookie, msg);
	uni_work(priv->uni);

	return (0);
}


/*
 * Upper layer signal from UNI
 */
static void
uni_uni_output(struct uni *uni, void *varg, enum uni_sig sig, u_int32_t cookie,
    struct uni_msg *msg)
{
	node_p node = (node_p)varg;
	struct priv *priv = NG_NODE_PRIVATE(node);
	struct mbuf *m;
	struct uni_arg arg;
	int error;

	if (priv->upper == NULL) {
		if (msg != NULL)
			uni_msg_destroy(msg);
		return;
	}
	arg.sig = sig;
	arg.cookie = cookie;

	m = uni_msg_pack_mbuf(msg, &arg, sizeof(arg));
	if (msg != NULL)
		uni_msg_destroy(msg);
	if (m == NULL)
		return;

	NG_SEND_DATA_ONLY(error, priv->upper, m);
}


static void
dump_uni_msg(struct uni_msg *msg)
{
	u_int pos;

	for (pos = 0; pos < uni_msg_len(msg); pos++) {
		if (pos % 16 == 0)
			printf("%06o ", pos);
		if (pos % 16 == 8)
			printf("  ");
		printf(" %02x", msg->b_rptr[pos]);
		if (pos % 16 == 15)
			printf("\n");
	}
	if (pos % 16 != 0)
		printf("\n");
}


/*
 * Dump a SAAL signal in either direction
 */
static void
dump_saal_signal(node_p node, enum saal_sig sig, struct uni_msg *msg, int to)
{
	struct priv *priv = NG_NODE_PRIVATE(node);

	printf("signal %s SAAL: ", to ? "to" : "from");

	switch (sig) {

#define D(S) case S: printf("%s", #S); break

	D(SAAL_ESTABLISH_request);
	D(SAAL_ESTABLISH_indication);
	D(SAAL_ESTABLISH_confirm);
	D(SAAL_RELEASE_request);
	D(SAAL_RELEASE_confirm);
	D(SAAL_RELEASE_indication);
	D(SAAL_DATA_request);
	D(SAAL_DATA_indication);
	D(SAAL_UDATA_request);
	D(SAAL_UDATA_indication);

#undef D
	  default:
		printf("sig=%d", sig); break;
	}
	if (msg != NULL) {
		printf(" data=%zu\n", uni_msg_len(msg));
		if (uni_get_debug(priv->uni, UNI_FAC_SAAL) > 1)
			dump_uni_msg(msg);
	} else
		printf("\n");
}

/*
 * Receive signal from SSCOP.
 *
 * If this is a data signal, repackage the data into one large buffer.
 * UNI shouldn't be the bottleneck in a system and this greatly simplifies
 * parsing in UNI.
 */
static int
ng_uni_rcvlower(hook_p hook __unused, item_p item)
{
	node_p node = NG_HOOK_NODE(hook);
	struct priv *priv = NG_NODE_PRIVATE(node);
	struct mbuf *m;
	struct sscfu_arg arg;
	struct uni_msg *msg;
	int error;

	if (!priv->enabled) {
		NG_FREE_ITEM(item);
		return (ENOTCONN);
	}

	NGI_GET_M(item, m);
	NG_FREE_ITEM(item);

	if ((error = uni_msg_unpack_mbuf(m, &msg)) != 0) {
		m_freem(m);
		return (error);
	}
	m_freem(m);

	if (uni_msg_len(msg) < sizeof(arg)) {
		uni_msg_destroy(msg);
		printf("%s: packet too short\n", __func__);
		return (EINVAL);
	}
	bcopy(msg->b_rptr, &arg, sizeof(arg));
	msg->b_rptr += sizeof(arg);

	if (arg.sig > SAAL_UDATA_indication) {
		uni_msg_destroy(msg);
		printf("%s: bogus signal\n", __func__);
		return (EINVAL);
	}

	if (uni_get_debug(priv->uni, UNI_FAC_SAAL) > 0)
		dump_saal_signal(node, arg.sig, msg, 0);

	uni_saal_input(priv->uni, arg.sig, msg);
	uni_work(priv->uni);

	return (0);
}

/*
 * Send signal to sscop.
 * Pack the message into an mbuf chain.
 */
static void
uni_saal_output(struct uni *uni, void *varg, enum saal_sig sig, struct uni_msg *msg)
{
	node_p node = (node_p)varg;
	struct priv *priv = NG_NODE_PRIVATE(node);
	struct mbuf *m;
	struct sscfu_arg arg;
	int error;

	if (uni_get_debug(priv->uni, UNI_FAC_SAAL) > 0)
		dump_saal_signal(node, sig, msg, 1);

	if (priv->lower == NULL) {
		if (msg != NULL)
			uni_msg_destroy(msg);
		return;
	}

	arg.sig = sig;

	m = uni_msg_pack_mbuf(msg, &arg, sizeof(arg));
	if (msg != NULL)
		uni_msg_destroy(msg);
	if (m == NULL)
		return;

	NG_SEND_DATA_ONLY(error, priv->lower, m);
}

static void
uni_verbose(struct uni *uni, void *varg, u_int fac, const char *fmt, ...)
{
	va_list ap;

	static char *facnames[] = {
#define UNI_DEBUG_DEFINE(D) [UNI_FAC_##D] = #D,
		UNI_DEBUG_FACILITIES
#undef UNI_DEBUG_DEFINE
	};

	printf("%s: ", facnames[fac]);

	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);

	printf("\n");
}


/************************************************************/
/*
 * Memory debugging
 */
struct unimem_debug {
	const char	*file;
	u_int		lno;
	LIST_ENTRY(unimem_debug) link;
	char		data[0];
};
LIST_HEAD(unimem_debug_list, unimem_debug);

static struct unimem_debug_list nguni_freemem[UNIMEM_TYPES] = {
    LIST_HEAD_INITIALIZER(nguni_freemem[0]),
    LIST_HEAD_INITIALIZER(nguni_freemem[1]),
    LIST_HEAD_INITIALIZER(nguni_freemem[2]),
    LIST_HEAD_INITIALIZER(nguni_freemem[3]),
    LIST_HEAD_INITIALIZER(nguni_freemem[4]),
};
static struct unimem_debug_list nguni_usedmem[UNIMEM_TYPES] = {
    LIST_HEAD_INITIALIZER(nguni_usedmem[0]),
    LIST_HEAD_INITIALIZER(nguni_usedmem[1]),
    LIST_HEAD_INITIALIZER(nguni_usedmem[2]),
    LIST_HEAD_INITIALIZER(nguni_usedmem[3]),
    LIST_HEAD_INITIALIZER(nguni_usedmem[4]),
};

static struct mtx nguni_unilist_mtx;

static const char *unimem_names[UNIMEM_TYPES] = {
	"instance",
	"all",
	"signal",
	"call",
	"party"
};

static void
uni_init(void)
{
	mtx_init(&nguni_unilist_mtx, "netgraph UNI structure lists", NULL,
	    MTX_DEF);
}

static void
uni_fini(void)
{
	u_int type;
	struct unimem_debug *h;

	for (type = 0; type < UNIMEM_TYPES; type++) {
		while ((h = LIST_FIRST(&nguni_freemem[type])) != NULL) {
			LIST_REMOVE(h, link);
			free(h, M_UNI);
		}

		while ((h = LIST_FIRST(&nguni_usedmem[type])) != NULL) {
			LIST_REMOVE(h, link);
			printf("ng_uni: %s in use: %p (%s,%u)\n",
			    unimem_names[type], (caddr_t)h->data,
			    h->file, h->lno);
			free(h, M_UNI);
		}
	}

	mtx_destroy(&nguni_unilist_mtx);
}

/*
 * Allocate a chunk of memory from a given type.
 */
void *
ng_uni_malloc(enum unimem type, const char *file, u_int lno)
{
	struct unimem_debug *d;
	size_t full;

	/*
	 * Try to allocate
	 */
	mtx_lock(&nguni_unilist_mtx);
	if ((d = LIST_FIRST(&nguni_freemem[type])) != NULL)
		LIST_REMOVE(d, link);
	mtx_unlock(&nguni_unilist_mtx);

	if (d == NULL) {
		/*
		 * allocate
		 */
		full = unimem_sizes[type] + offsetof(struct unimem_debug, data);
		if ((d = malloc(full, M_UNI, M_NOWAIT | M_ZERO)) == NULL)
			return (NULL);
	} else {
		bzero(d->data, unimem_sizes[type]);
	}
	d->file = file;
	d->lno = lno;

	mtx_lock(&nguni_unilist_mtx);
	LIST_INSERT_HEAD(&nguni_usedmem[type], d, link);
	mtx_unlock(&nguni_unilist_mtx);
	return (d->data);
}

void
ng_uni_free(enum unimem type, void *ptr, const char *file, u_int lno)
{
	struct unimem_debug *d, *h;

	d = (struct unimem_debug *)
	    ((char *)ptr - offsetof(struct unimem_debug, data));

	mtx_lock(&nguni_unilist_mtx);

	LIST_FOREACH(h, &nguni_usedmem[type], link)
		if (d == h)
			break;

	if (h != NULL) {
		LIST_REMOVE(d, link);
		LIST_INSERT_HEAD(&nguni_freemem[type], d, link);
	} else {
		/*
		 * Not on used list - try free list.
		 */
		LIST_FOREACH(h, &nguni_freemem[type], link)
			if (d == h)
				break;
		if (h == NULL)
			printf("ng_uni: %s,%u: %p(%s) was never allocated\n",
			    file, lno, ptr, unimem_names[type]);
		else
			printf("ng_uni: %s,%u: %p(%s) was already destroyed "
			    "in %s,%u\n",
			    file, lno, ptr, unimem_names[type],
			    h->file, h->lno);
	}
	mtx_unlock(&nguni_unilist_mtx);
}
/************************************************************/
/*
 * INITIALISATION
 */

/*
 * Loading and unloading of node type
 */
static int
ng_uni_mod_event(module_t mod, int event, void *data)
{
	int error = 0;

	switch(event) {

	  case MOD_LOAD:
		uni_init();
		break;

	  case MOD_UNLOAD:
		uni_fini();
		break;

	  default:
		error = EOPNOTSUPP;
		break;
	}
	return (error);
}
