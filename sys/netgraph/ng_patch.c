/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Maxim Ignatenko <gelraen.ua@gmail.com>
 * Copyright (c) 2015 Dmitry Vagin <daemon.hammer@ya.ru>
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/endian.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>

#include <net/bpf.h>
#include <net/ethernet.h>

#include <netgraph/ng_message.h>
#include <netgraph/ng_parse.h>
#include <netgraph/netgraph.h>

#include <netgraph/ng_patch.h>

/* private data */
struct ng_patch_priv {
	hook_p		in;
	hook_p		out;
	uint8_t		dlt;	/* DLT_XXX from bpf.h */
	struct ng_patch_stats stats;
	struct ng_patch_config *conf;
};

typedef struct ng_patch_priv *priv_p;

/* Netgraph methods */
static ng_constructor_t	ng_patch_constructor;
static ng_rcvmsg_t	ng_patch_rcvmsg;
static ng_shutdown_t	ng_patch_shutdown;
static ng_newhook_t	ng_patch_newhook;
static ng_rcvdata_t	ng_patch_rcvdata;
static ng_disconnect_t	ng_patch_disconnect;

#define ERROUT(x) { error = (x); goto done; }

static int
ng_patch_config_getlen(const struct ng_parse_type *type,
    const u_char *start, const u_char *buf)
{
	const struct ng_patch_config *conf;

	conf = (const struct ng_patch_config *)(buf -
	    offsetof(struct ng_patch_config, ops));

	return (conf->count);
}

static const struct ng_parse_struct_field ng_patch_op_type_fields[]
	= NG_PATCH_OP_TYPE;
static const struct ng_parse_type ng_patch_op_type = {
	&ng_parse_struct_type,
	&ng_patch_op_type_fields
};

static const struct ng_parse_array_info ng_patch_ops_array_info = {
	&ng_patch_op_type,
	&ng_patch_config_getlen
};
static const struct ng_parse_type ng_patch_ops_array_type = {
	&ng_parse_array_type,
	&ng_patch_ops_array_info
};

static const struct ng_parse_struct_field ng_patch_config_type_fields[]
	= NG_PATCH_CONFIG_TYPE;
static const struct ng_parse_type ng_patch_config_type = {
	&ng_parse_struct_type,
	&ng_patch_config_type_fields
};

static const struct ng_parse_struct_field ng_patch_stats_fields[]
	= NG_PATCH_STATS_TYPE;
static const struct ng_parse_type ng_patch_stats_type = {
	&ng_parse_struct_type,
	&ng_patch_stats_fields
};

static const struct ng_cmdlist ng_patch_cmdlist[] = {
	{
		NGM_PATCH_COOKIE,
		NGM_PATCH_GETDLT,
		"getdlt",
		NULL,
		&ng_parse_uint8_type
	},
	{
		NGM_PATCH_COOKIE,
		NGM_PATCH_SETDLT,
		"setdlt",
		&ng_parse_uint8_type,
		NULL
	},
	{
		NGM_PATCH_COOKIE,
		NGM_PATCH_GETCONFIG,
		"getconfig",
		NULL,
		&ng_patch_config_type
	},
	{
		NGM_PATCH_COOKIE,
		NGM_PATCH_SETCONFIG,
		"setconfig",
		&ng_patch_config_type,
		NULL
	},
	{
		NGM_PATCH_COOKIE,
		NGM_PATCH_GET_STATS,
		"getstats",
		NULL,
		&ng_patch_stats_type
	},
	{
		NGM_PATCH_COOKIE,
		NGM_PATCH_CLR_STATS,
		"clrstats",
		NULL,
		NULL
	},
	{
		NGM_PATCH_COOKIE,
		NGM_PATCH_GETCLR_STATS,
		"getclrstats",
		NULL,
		&ng_patch_stats_type
	},
	{ 0 }
};

static struct ng_type typestruct = {
	.version =	NG_ABI_VERSION,
	.name =		NG_PATCH_NODE_TYPE,
	.constructor =	ng_patch_constructor,
	.rcvmsg =	ng_patch_rcvmsg,
	.shutdown =	ng_patch_shutdown,
	.newhook =	ng_patch_newhook,
	.rcvdata =	ng_patch_rcvdata,
	.disconnect =	ng_patch_disconnect,
	.cmdlist =	ng_patch_cmdlist,
};

NETGRAPH_INIT(patch, &typestruct);

static int
ng_patch_constructor(node_p node)
{
	priv_p privdata;

	privdata = malloc(sizeof(*privdata), M_NETGRAPH, M_WAITOK | M_ZERO);
	privdata->dlt = DLT_RAW;

	NG_NODE_SET_PRIVATE(node, privdata);

	return (0);
}

static int
ng_patch_newhook(node_p node, hook_p hook, const char *name)
{
	const priv_p privp = NG_NODE_PRIVATE(node);

	if (strncmp(name, NG_PATCH_HOOK_IN, strlen(NG_PATCH_HOOK_IN)) == 0) {
		privp->in = hook;
	} else if (strncmp(name, NG_PATCH_HOOK_OUT,
	    strlen(NG_PATCH_HOOK_OUT)) == 0) {
		privp->out = hook;
	} else
		return (EINVAL);

	return (0);
}

static int
ng_patch_rcvmsg(node_p node, item_p item, hook_p lasthook)
{
	const priv_p privp = NG_NODE_PRIVATE(node);
	struct ng_patch_config *conf, *newconf;
	struct ng_mesg *msg;
	struct ng_mesg *resp = NULL;
	int i, error = 0;

	NGI_GET_MSG(item, msg);

	if  (msg->header.typecookie != NGM_PATCH_COOKIE)
		ERROUT(EINVAL);

	switch (msg->header.cmd)
	{
		case NGM_PATCH_GETCONFIG:
			if (privp->conf == NULL)
				ERROUT(0);

			NG_MKRESPONSE(resp, msg,
			    NG_PATCH_CONF_SIZE(privp->conf->count), M_WAITOK);

			if (resp == NULL)
				ERROUT(ENOMEM);

			bcopy(privp->conf, resp->data,
			    NG_PATCH_CONF_SIZE(privp->conf->count));

			conf = (struct ng_patch_config *) resp->data;

			for (i = 0; i < conf->count; i++) {
				switch (conf->ops[i].length)
				{
					case 1:
						conf->ops[i].val.v8 = conf->ops[i].val.v1;
						break;
					case 2:
						conf->ops[i].val.v8 = conf->ops[i].val.v2;
						break;
					case 4:
						conf->ops[i].val.v8 = conf->ops[i].val.v4;
						break;
					case 8:
						break;
				}
			}

			break;

		case NGM_PATCH_SETCONFIG:
			conf = (struct ng_patch_config *) msg->data;

			if (msg->header.arglen < sizeof(struct ng_patch_config) ||
			    msg->header.arglen < NG_PATCH_CONF_SIZE(conf->count))
				ERROUT(EINVAL);

			for (i = 0; i < conf->count; i++) {
				switch (conf->ops[i].length)
				{
					case 1:
						conf->ops[i].val.v1 = (uint8_t) conf->ops[i].val.v8;
						break;
					case 2:
						conf->ops[i].val.v2 = (uint16_t) conf->ops[i].val.v8;
						break;
					case 4:
						conf->ops[i].val.v4 = (uint32_t) conf->ops[i].val.v8;
						break;
					case 8:
						break;
					default:
						ERROUT(EINVAL);
				}
			}

			conf->csum_flags &= NG_PATCH_CSUM_IPV4|NG_PATCH_CSUM_IPV6;
			conf->relative_offset = !!conf->relative_offset;

			newconf = malloc(NG_PATCH_CONF_SIZE(conf->count), M_NETGRAPH, M_WAITOK | M_ZERO);

			bcopy(conf, newconf, NG_PATCH_CONF_SIZE(conf->count));

			if (privp->conf)
				free(privp->conf, M_NETGRAPH);

			privp->conf = newconf;

			break;

		case NGM_PATCH_GET_STATS:
		case NGM_PATCH_CLR_STATS:
		case NGM_PATCH_GETCLR_STATS:
			if (msg->header.cmd != NGM_PATCH_CLR_STATS) {
				NG_MKRESPONSE(resp, msg, sizeof(struct ng_patch_stats), M_WAITOK);

				if (resp == NULL)
					ERROUT(ENOMEM);

				bcopy(&(privp->stats), resp->data, sizeof(struct ng_patch_stats));
			}

			if (msg->header.cmd != NGM_PATCH_GET_STATS)
				bzero(&(privp->stats), sizeof(struct ng_patch_stats));

			break;

		case NGM_PATCH_GETDLT:
			NG_MKRESPONSE(resp, msg, sizeof(uint8_t), M_WAITOK);

			if (resp == NULL)
				ERROUT(ENOMEM);

			*((uint8_t *) resp->data) = privp->dlt;

			break;

		case NGM_PATCH_SETDLT:
			if (msg->header.arglen != sizeof(uint8_t))
				ERROUT(EINVAL);

			switch (*(uint8_t *) msg->data)
			{
				case DLT_EN10MB:
				case DLT_RAW:
					privp->dlt = *(uint8_t *) msg->data;
					break;

				default:
					ERROUT(EINVAL);
			}

			break;

		default:
			ERROUT(EINVAL);
	}

done:
	NG_RESPOND_MSG(error, node, item, resp);
	NG_FREE_MSG(msg);

	return (error);
}

static void
do_patch(priv_p privp, struct mbuf *m, int global_offset)
{
	int i, offset, patched = 0;
	union ng_patch_op_val val;

	for (i = 0; i < privp->conf->count; i++) {
		offset = global_offset + privp->conf->ops[i].offset;

		if (offset + privp->conf->ops[i].length > m->m_pkthdr.len)
			continue;

		/* for "=" operation we don't need to copy data from mbuf */
		if (privp->conf->ops[i].mode != NG_PATCH_MODE_SET)
			m_copydata(m, offset, privp->conf->ops[i].length, (caddr_t) &val);

		switch (privp->conf->ops[i].length)
		{
			case 1:
				switch (privp->conf->ops[i].mode)
				{
					case NG_PATCH_MODE_SET:
						val.v1 = privp->conf->ops[i].val.v1;
						break;
					case NG_PATCH_MODE_ADD:
						val.v1 += privp->conf->ops[i].val.v1;
						break;
					case NG_PATCH_MODE_SUB:
						val.v1 -= privp->conf->ops[i].val.v1;
						break;
					case NG_PATCH_MODE_MUL:
						val.v1 *= privp->conf->ops[i].val.v1;
						break;
					case NG_PATCH_MODE_DIV:
						val.v1 /= privp->conf->ops[i].val.v1;
						break;
					case NG_PATCH_MODE_NEG:
						*((int8_t *) &val) = - *((int8_t *) &val);
						break;
					case NG_PATCH_MODE_AND:
						val.v1 &= privp->conf->ops[i].val.v1;
						break;
					case NG_PATCH_MODE_OR:
						val.v1 |= privp->conf->ops[i].val.v1;
						break;
					case NG_PATCH_MODE_XOR:
						val.v1 ^= privp->conf->ops[i].val.v1;
						break;
					case NG_PATCH_MODE_SHL:
						val.v1 <<= privp->conf->ops[i].val.v1;
						break;
					case NG_PATCH_MODE_SHR:
						val.v1 >>= privp->conf->ops[i].val.v1;
						break;
				}
				break;

			case 2:
				val.v2 = ntohs(val.v2);

				switch (privp->conf->ops[i].mode)
				{
					case NG_PATCH_MODE_SET:
						val.v2 = privp->conf->ops[i].val.v2;
						break;
					case NG_PATCH_MODE_ADD:
						val.v2 += privp->conf->ops[i].val.v2;
						break;
					case NG_PATCH_MODE_SUB:
						val.v2 -= privp->conf->ops[i].val.v2;
						break;
					case NG_PATCH_MODE_MUL:
						val.v2 *= privp->conf->ops[i].val.v2;
						break;
					case NG_PATCH_MODE_DIV:
						val.v2 /= privp->conf->ops[i].val.v2;
						break;
					case NG_PATCH_MODE_NEG:
						*((int16_t *) &val) = - *((int16_t *) &val);
						break;
					case NG_PATCH_MODE_AND:
						val.v2 &= privp->conf->ops[i].val.v2;
						break;
					case NG_PATCH_MODE_OR:
						val.v2 |= privp->conf->ops[i].val.v2;
						break;
					case NG_PATCH_MODE_XOR:
						val.v2 ^= privp->conf->ops[i].val.v2;
						break;
					case NG_PATCH_MODE_SHL:
						val.v2 <<= privp->conf->ops[i].val.v2;
						break;
					case NG_PATCH_MODE_SHR:
						val.v2 >>= privp->conf->ops[i].val.v2;
						break;
				}

				val.v2 = htons(val.v2);

				break;

			case 4:
				val.v4 = ntohl(val.v4);

				switch (privp->conf->ops[i].mode)
				{
					case NG_PATCH_MODE_SET:
						val.v4 = privp->conf->ops[i].val.v4;
						break;
					case NG_PATCH_MODE_ADD:
						val.v4 += privp->conf->ops[i].val.v4;
						break;
					case NG_PATCH_MODE_SUB:
						val.v4 -= privp->conf->ops[i].val.v4;
						break;
					case NG_PATCH_MODE_MUL:
						val.v4 *= privp->conf->ops[i].val.v4;
						break;
					case NG_PATCH_MODE_DIV:
						val.v4 /= privp->conf->ops[i].val.v4;
						break;
					case NG_PATCH_MODE_NEG:
						*((int32_t *) &val) = - *((int32_t *) &val);
						break;
					case NG_PATCH_MODE_AND:
						val.v4 &= privp->conf->ops[i].val.v4;
						break;
					case NG_PATCH_MODE_OR:
						val.v4 |= privp->conf->ops[i].val.v4;
						break;
					case NG_PATCH_MODE_XOR:
						val.v4 ^= privp->conf->ops[i].val.v4;
						break;
					case NG_PATCH_MODE_SHL:
						val.v4 <<= privp->conf->ops[i].val.v4;
						break;
					case NG_PATCH_MODE_SHR:
						val.v4 >>= privp->conf->ops[i].val.v4;
						break;
				}

				val.v4 = htonl(val.v4);

				break;

			case 8:
				val.v8 = be64toh(val.v8);

				switch (privp->conf->ops[i].mode)
				{
					case NG_PATCH_MODE_SET:
						val.v8 = privp->conf->ops[i].val.v8;
						break;
					case NG_PATCH_MODE_ADD:
						val.v8 += privp->conf->ops[i].val.v8;
						break;
					case NG_PATCH_MODE_SUB:
						val.v8 -= privp->conf->ops[i].val.v8;
						break;
					case NG_PATCH_MODE_MUL:
						val.v8 *= privp->conf->ops[i].val.v8;
						break;
					case NG_PATCH_MODE_DIV:
						val.v8 /= privp->conf->ops[i].val.v8;
						break;
					case NG_PATCH_MODE_NEG:
						*((int64_t *) &val) = - *((int64_t *) &val);
						break;
					case NG_PATCH_MODE_AND:
						val.v8 &= privp->conf->ops[i].val.v8;
						break;
					case NG_PATCH_MODE_OR:
						val.v8 |= privp->conf->ops[i].val.v8;
						break;
					case NG_PATCH_MODE_XOR:
						val.v8 ^= privp->conf->ops[i].val.v8;
						break;
					case NG_PATCH_MODE_SHL:
						val.v8 <<= privp->conf->ops[i].val.v8;
						break;
					case NG_PATCH_MODE_SHR:
						val.v8 >>= privp->conf->ops[i].val.v8;
						break;
				}

				val.v8 = htobe64(val.v8);

				break;
		}

		m_copyback(m, offset, privp->conf->ops[i].length, (caddr_t) &val);
		patched = 1;
	}

	if (patched)
		privp->stats.patched++;
}

static int
ng_patch_rcvdata(hook_p hook, item_p item)
{
	const priv_p priv = NG_NODE_PRIVATE(NG_HOOK_NODE(hook));
	struct mbuf *m;
	hook_p out;
	int pullup_len = 0;
	int error = 0;

	priv->stats.received++;

	NGI_GET_M(item, m);

#define	PULLUP_CHECK(mbuf, length) do {					\
	pullup_len += length;						\
	if (((mbuf)->m_pkthdr.len < pullup_len) ||			\
	    (pullup_len > MHLEN)) {					\
		error = EINVAL;						\
		goto bypass;						\
	}								\
	if ((mbuf)->m_len < pullup_len &&				\
	    (((mbuf) = m_pullup((mbuf), pullup_len)) == NULL)) {	\
		error = ENOBUFS;					\
		goto drop;						\
	}								\
} while (0)

	if (priv->conf && hook == priv->in &&
	    m && (m->m_flags & M_PKTHDR)) {

		m = m_unshare(m, M_NOWAIT);

		if (m == NULL)
			ERROUT(ENOMEM);

		if (priv->conf->relative_offset) {
			struct ether_header *eh;
			struct ng_patch_vlan_header *vh;
			uint16_t etype;

			switch (priv->dlt)
			{
				case DLT_EN10MB:
					PULLUP_CHECK(m, sizeof(struct ether_header));
					eh = mtod(m, struct ether_header *);
					etype = ntohs(eh->ether_type);

					for (;;) {	/* QinQ support */
						switch (etype)
						{
							case 0x8100:
							case 0x88A8:
							case 0x9100:
								PULLUP_CHECK(m, sizeof(struct ng_patch_vlan_header));
								vh = (struct ng_patch_vlan_header *) mtodo(m,
								    pullup_len - sizeof(struct ng_patch_vlan_header));
								etype = ntohs(vh->etype);
								break;

							default:
								goto loopend;
						}
					}
loopend:
					break;

				case DLT_RAW:
					break;

				default:
					ERROUT(EINVAL);
			}
		}

		do_patch(priv, m, pullup_len);

		m->m_pkthdr.csum_flags |= priv->conf->csum_flags;
	}

#undef	PULLUP_CHECK

bypass:
	out = NULL;

	if (hook == priv->in) {
		/* return frames on 'in' hook if 'out' not connected */
		out = priv->out ? priv->out : priv->in;
	} else if (hook == priv->out && priv->in) {
		/* pass frames on 'out' hook if 'in' connected */
		out = priv->in;
	}

	if (out == NULL)
		ERROUT(0);

	NG_FWD_NEW_DATA(error, item, out, m);

	return (error);

done:
drop:
	NG_FREE_ITEM(item);
	NG_FREE_M(m);

	priv->stats.dropped++;

	return (error);
}

static int
ng_patch_shutdown(node_p node)
{
	const priv_p privdata = NG_NODE_PRIVATE(node);

	NG_NODE_SET_PRIVATE(node, NULL);
	NG_NODE_UNREF(node);

	if (privdata->conf != NULL)
		free(privdata->conf, M_NETGRAPH);

	free(privdata, M_NETGRAPH);

	return (0);
}

static int
ng_patch_disconnect(hook_p hook)
{
	priv_p priv;

	priv = NG_NODE_PRIVATE(NG_HOOK_NODE(hook));

	if (hook == priv->in) {
		priv->in = NULL;
	}

	if (hook == priv->out) {
		priv->out = NULL;
	}

	if (NG_NODE_NUMHOOKS(NG_HOOK_NODE(hook)) == 0 &&
	    NG_NODE_IS_VALID(NG_HOOK_NODE(hook))) /* already shutting down? */
		ng_rmnode_self(NG_HOOK_NODE(hook));

	return (0);
}
