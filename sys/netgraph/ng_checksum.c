/*-
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

#include "opt_inet.h"
#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/endian.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>

#include <net/bpf.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_vlan_var.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <machine/in_cksum.h>

#include <netgraph/ng_message.h>
#include <netgraph/ng_parse.h>
#include <netgraph/netgraph.h>

#include <netgraph/ng_checksum.h>

/* private data */
struct ng_checksum_priv {
	hook_p in;
	hook_p out;
	uint8_t dlt;	/* DLT_XXX from bpf.h */
	struct ng_checksum_config *conf;
	struct ng_checksum_stats stats;
};

typedef struct ng_checksum_priv *priv_p;

/* Netgraph methods */
static ng_constructor_t	ng_checksum_constructor;
static ng_rcvmsg_t	ng_checksum_rcvmsg;
static ng_shutdown_t	ng_checksum_shutdown;
static ng_newhook_t	ng_checksum_newhook;
static ng_rcvdata_t	ng_checksum_rcvdata;
static ng_disconnect_t	ng_checksum_disconnect;

#define ERROUT(x) { error = (x); goto done; }

static const struct ng_parse_struct_field ng_checksum_config_type_fields[]
	= NG_CHECKSUM_CONFIG_TYPE;
static const struct ng_parse_type ng_checksum_config_type = {
	&ng_parse_struct_type,
	&ng_checksum_config_type_fields
};

static const struct ng_parse_struct_field ng_checksum_stats_fields[]
	= NG_CHECKSUM_STATS_TYPE;
static const struct ng_parse_type ng_checksum_stats_type = {
	&ng_parse_struct_type,
	&ng_checksum_stats_fields
};

static const struct ng_cmdlist ng_checksum_cmdlist[] = {
	{
		NGM_CHECKSUM_COOKIE,
		NGM_CHECKSUM_GETDLT,
		"getdlt",
		NULL,
		&ng_parse_uint8_type
	},
	{
		NGM_CHECKSUM_COOKIE,
		NGM_CHECKSUM_SETDLT,
		"setdlt",
		&ng_parse_uint8_type,
		NULL
	},
	{
		NGM_CHECKSUM_COOKIE,
		NGM_CHECKSUM_GETCONFIG,
		"getconfig",
		NULL,
		&ng_checksum_config_type
	},
	{
		NGM_CHECKSUM_COOKIE,
		NGM_CHECKSUM_SETCONFIG,
		"setconfig",
		&ng_checksum_config_type,
		NULL
	},
	{
		NGM_CHECKSUM_COOKIE,
		NGM_CHECKSUM_GET_STATS,
		"getstats",
		NULL,
		&ng_checksum_stats_type
	},
	{
		NGM_CHECKSUM_COOKIE,
		NGM_CHECKSUM_CLR_STATS,
		"clrstats",
		NULL,
		NULL
	},
	{
		NGM_CHECKSUM_COOKIE,
		NGM_CHECKSUM_GETCLR_STATS,
		"getclrstats",
		NULL,
		&ng_checksum_stats_type
	},
	{ 0 }
};

static struct ng_type typestruct = {
	.version =	NG_ABI_VERSION,
	.name =		NG_CHECKSUM_NODE_TYPE,
	.constructor =	ng_checksum_constructor,
	.rcvmsg =	ng_checksum_rcvmsg,
	.shutdown =	ng_checksum_shutdown,
	.newhook =	ng_checksum_newhook,
	.rcvdata =	ng_checksum_rcvdata,
	.disconnect =	ng_checksum_disconnect,
	.cmdlist =	ng_checksum_cmdlist,
};

NETGRAPH_INIT(checksum, &typestruct);

static int
ng_checksum_constructor(node_p node)
{
	priv_p priv;

	priv = malloc(sizeof(*priv), M_NETGRAPH, M_WAITOK|M_ZERO);
	priv->dlt = DLT_RAW;

	NG_NODE_SET_PRIVATE(node, priv);

	return (0);
}

static int
ng_checksum_newhook(node_p node, hook_p hook, const char *name)
{
	const priv_p priv = NG_NODE_PRIVATE(node);

	if (strncmp(name, NG_CHECKSUM_HOOK_IN, strlen(NG_CHECKSUM_HOOK_IN)) == 0) {
		priv->in = hook;
	} else if (strncmp(name, NG_CHECKSUM_HOOK_OUT, strlen(NG_CHECKSUM_HOOK_OUT)) == 0) {
		priv->out = hook;
	} else
		return (EINVAL);

	return (0);
}

static int
ng_checksum_rcvmsg(node_p node, item_p item, hook_p lasthook)
{
	const priv_p priv = NG_NODE_PRIVATE(node);
	struct ng_checksum_config *conf, *newconf;
	struct ng_mesg *msg;
	struct ng_mesg *resp = NULL;
	int error = 0;

	NGI_GET_MSG(item, msg);

	if  (msg->header.typecookie != NGM_CHECKSUM_COOKIE)
		ERROUT(EINVAL);

	switch (msg->header.cmd)
	{
		case NGM_CHECKSUM_GETDLT:
			NG_MKRESPONSE(resp, msg, sizeof(uint8_t), M_WAITOK);

			if (resp == NULL)
				ERROUT(ENOMEM);

			*((uint8_t *) resp->data) = priv->dlt;

			break;

		case NGM_CHECKSUM_SETDLT:
			if (msg->header.arglen != sizeof(uint8_t))
				ERROUT(EINVAL);

			switch (*(uint8_t *) msg->data)
			{
				case DLT_EN10MB:
				case DLT_RAW:
					priv->dlt = *(uint8_t *) msg->data;
					break;

				default:
					ERROUT(EINVAL);
			}

			break;

		case NGM_CHECKSUM_GETCONFIG:
			if (priv->conf == NULL)
				ERROUT(0);

			NG_MKRESPONSE(resp, msg, sizeof(struct ng_checksum_config), M_WAITOK);

			if (resp == NULL)
				ERROUT(ENOMEM);

			bcopy(priv->conf, resp->data, sizeof(struct ng_checksum_config));

			break;

		case NGM_CHECKSUM_SETCONFIG:
			conf = (struct ng_checksum_config *) msg->data;

			if (msg->header.arglen != sizeof(struct ng_checksum_config))
				ERROUT(EINVAL);

			conf->csum_flags &= NG_CHECKSUM_CSUM_IPV4|NG_CHECKSUM_CSUM_IPV6;
			conf->csum_offload &= NG_CHECKSUM_CSUM_IPV4|NG_CHECKSUM_CSUM_IPV6;

			newconf = malloc(sizeof(struct ng_checksum_config), M_NETGRAPH, M_WAITOK|M_ZERO);

			bcopy(conf, newconf, sizeof(struct ng_checksum_config));

			if (priv->conf)
				free(priv->conf, M_NETGRAPH);

			priv->conf = newconf;

			break;

		case NGM_CHECKSUM_GET_STATS:
		case NGM_CHECKSUM_CLR_STATS:
		case NGM_CHECKSUM_GETCLR_STATS:
			if (msg->header.cmd != NGM_CHECKSUM_CLR_STATS) {
				NG_MKRESPONSE(resp, msg, sizeof(struct ng_checksum_stats), M_WAITOK);

				if (resp == NULL)
					ERROUT(ENOMEM);

				bcopy(&(priv->stats), resp->data, sizeof(struct ng_checksum_stats));
			}

			if (msg->header.cmd != NGM_CHECKSUM_GET_STATS)
				bzero(&(priv->stats), sizeof(struct ng_checksum_stats));

			break;

		default:
			ERROUT(EINVAL);
	}

done:
	NG_RESPOND_MSG(error, node, item, resp);
	NG_FREE_MSG(msg);

	return (error);
}

#define	PULLUP_CHECK(mbuf, length) do {					\
	pullup_len += length;						\
	if (((mbuf)->m_pkthdr.len < pullup_len) ||			\
	    (pullup_len > MHLEN)) {					\
		return (EINVAL);					\
	}								\
	if ((mbuf)->m_len < pullup_len &&				\
	    (((mbuf) = m_pullup((mbuf), pullup_len)) == NULL)) {	\
		return (ENOBUFS);					\
	}								\
} while (0)

#ifdef INET
static int
checksum_ipv4(priv_p priv, struct mbuf *m, int l3_offset)
{
	struct ip *ip4;
	int pullup_len;
	int hlen, plen;
	int processed = 0;

	pullup_len = l3_offset;

	PULLUP_CHECK(m, sizeof(struct ip));
	ip4 = (struct ip *) mtodo(m, l3_offset);

	if (ip4->ip_v != IPVERSION)
		return (EOPNOTSUPP);

	hlen = ip4->ip_hl << 2;
	plen = ntohs(ip4->ip_len);

	if (hlen < sizeof(struct ip) || m->m_pkthdr.len < l3_offset + plen)
		return (EINVAL);

	if (m->m_pkthdr.csum_flags & CSUM_IP) {
		ip4->ip_sum = 0;

		if ((priv->conf->csum_offload & CSUM_IP) == 0) {
			if (hlen == sizeof(struct ip))
				ip4->ip_sum = in_cksum_hdr(ip4);
			else
				ip4->ip_sum = in_cksum_skip(m, l3_offset + hlen, l3_offset);

			m->m_pkthdr.csum_flags &= ~CSUM_IP;
		}

		processed = 1;
	}

	pullup_len = l3_offset + hlen;

	/* We can not calculate a checksum fragmented packets */
	if (ip4->ip_off & htons(IP_MF|IP_OFFMASK)) {
		m->m_pkthdr.csum_flags &= ~(CSUM_TCP|CSUM_UDP);
		return (0);
	}

	switch (ip4->ip_p)
	{
		case IPPROTO_TCP:
			if (m->m_pkthdr.csum_flags & CSUM_TCP) {
				struct tcphdr *th;

				PULLUP_CHECK(m, sizeof(struct tcphdr));
				th = (struct tcphdr *) mtodo(m, l3_offset + hlen);

				th->th_sum = in_pseudo(ip4->ip_src.s_addr,
				    ip4->ip_dst.s_addr, htons(ip4->ip_p + plen - hlen));

				if ((priv->conf->csum_offload & CSUM_TCP) == 0) {
					th->th_sum = in_cksum_skip(m, l3_offset + plen, l3_offset + hlen);
					m->m_pkthdr.csum_flags &= ~CSUM_TCP;
				}

				processed = 1;
			}

			m->m_pkthdr.csum_flags &= ~CSUM_UDP;
			break;

		case IPPROTO_UDP:
			if (m->m_pkthdr.csum_flags & CSUM_UDP) {
				struct udphdr *uh;

				PULLUP_CHECK(m, sizeof(struct udphdr));
				uh = (struct udphdr *) mtodo(m, l3_offset + hlen);

				uh->uh_sum = in_pseudo(ip4->ip_src.s_addr,
				    ip4->ip_dst.s_addr, htons(ip4->ip_p + plen - hlen));

				if ((priv->conf->csum_offload & CSUM_UDP) == 0) {
					uh->uh_sum = in_cksum_skip(m,
					    l3_offset + plen, l3_offset + hlen);

					if (uh->uh_sum == 0)
						uh->uh_sum = 0xffff;

					m->m_pkthdr.csum_flags &= ~CSUM_UDP;
				}

				processed = 1;
			}

			m->m_pkthdr.csum_flags &= ~CSUM_TCP;
			break;

		default:
			m->m_pkthdr.csum_flags &= ~(CSUM_TCP|CSUM_UDP);
			break;
	}

	m->m_pkthdr.csum_flags &= ~NG_CHECKSUM_CSUM_IPV6;

	if (processed)
		priv->stats.processed++;

	return (0);
}
#endif /* INET */

#ifdef INET6
static int
checksum_ipv6(priv_p priv, struct mbuf *m, int l3_offset)
{
	struct ip6_hdr *ip6;
	struct ip6_ext *ip6e = NULL;
	int pullup_len;
	int hlen, plen;
	int nxt;
	int processed = 0;

	pullup_len = l3_offset;

	PULLUP_CHECK(m, sizeof(struct ip6_hdr));
	ip6 = (struct ip6_hdr *) mtodo(m, l3_offset);

	if ((ip6->ip6_vfc & IPV6_VERSION_MASK) != IPV6_VERSION)
		return (EOPNOTSUPP);

	hlen = sizeof(struct ip6_hdr);
	plen = ntohs(ip6->ip6_plen) + hlen;

	if (m->m_pkthdr.len < l3_offset + plen)
		return (EINVAL);

	nxt = ip6->ip6_nxt;

	for (;;) {
		switch (nxt)
		{
			case IPPROTO_DSTOPTS:
			case IPPROTO_HOPOPTS:
			case IPPROTO_ROUTING:
				PULLUP_CHECK(m, sizeof(struct ip6_ext));
				ip6e = (struct ip6_ext *) mtodo(m, l3_offset + hlen);
				nxt = ip6e->ip6e_nxt;
				hlen += (ip6e->ip6e_len + 1) << 3;
				pullup_len = l3_offset + hlen;
				break;

			case IPPROTO_AH:
				PULLUP_CHECK(m, sizeof(struct ip6_ext));
				ip6e = (struct ip6_ext *) mtodo(m, l3_offset + hlen);
				nxt = ip6e->ip6e_nxt;
				hlen += (ip6e->ip6e_len + 2) << 2;
				pullup_len = l3_offset + hlen;
				break;

			case IPPROTO_FRAGMENT:
				/* We can not calculate a checksum fragmented packets */
				m->m_pkthdr.csum_flags &= ~(CSUM_TCP_IPV6|CSUM_UDP_IPV6);
				return (0);

			default:
				goto loopend;
		}

		if (nxt == 0)
			return (EINVAL);
	}

loopend:

	switch (nxt)
	{
		case IPPROTO_TCP:
			if (m->m_pkthdr.csum_flags & CSUM_TCP_IPV6) {
				struct tcphdr *th;

				PULLUP_CHECK(m, sizeof(struct tcphdr));
				th = (struct tcphdr *) mtodo(m, l3_offset + hlen);

				th->th_sum = in6_cksum_pseudo(ip6, plen - hlen, nxt, 0);

				if ((priv->conf->csum_offload & CSUM_TCP_IPV6) == 0) {
					th->th_sum = in_cksum_skip(m, l3_offset + plen, l3_offset + hlen);
					m->m_pkthdr.csum_flags &= ~CSUM_TCP_IPV6;
				}

				processed = 1;
			}

			m->m_pkthdr.csum_flags &= ~CSUM_UDP_IPV6;
			break;

		case IPPROTO_UDP:
			if (m->m_pkthdr.csum_flags & CSUM_UDP_IPV6) {
				struct udphdr *uh;

				PULLUP_CHECK(m, sizeof(struct udphdr));
				uh = (struct udphdr *) mtodo(m, l3_offset + hlen);

				uh->uh_sum = in6_cksum_pseudo(ip6, plen - hlen, nxt, 0);

				if ((priv->conf->csum_offload & CSUM_UDP_IPV6) == 0) {
					uh->uh_sum = in_cksum_skip(m,
					    l3_offset + plen, l3_offset + hlen);

					if (uh->uh_sum == 0)
						uh->uh_sum = 0xffff;

					m->m_pkthdr.csum_flags &= ~CSUM_UDP_IPV6;
				}

				processed = 1;
			}

			m->m_pkthdr.csum_flags &= ~CSUM_TCP_IPV6;
			break;

		default:
			m->m_pkthdr.csum_flags &= ~(CSUM_TCP_IPV6|CSUM_UDP_IPV6);
			break;
	}

	m->m_pkthdr.csum_flags &= ~NG_CHECKSUM_CSUM_IPV4;

	if (processed)
		priv->stats.processed++;

	return (0);
}
#endif /* INET6 */

#undef	PULLUP_CHECK

static int
ng_checksum_rcvdata(hook_p hook, item_p item)
{
	const priv_p priv = NG_NODE_PRIVATE(NG_HOOK_NODE(hook));
	struct mbuf *m;
	hook_p out;
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

	if (!(priv->conf && hook == priv->in && m && (m->m_flags & M_PKTHDR)))
		goto bypass;

	m->m_pkthdr.csum_flags |= priv->conf->csum_flags;

	if (m->m_pkthdr.csum_flags & (NG_CHECKSUM_CSUM_IPV4|NG_CHECKSUM_CSUM_IPV6))
	{
		struct ether_header *eh;
		struct ng_checksum_vlan_header *vh;
		int pullup_len = 0;
		uint16_t etype;

		m = m_unshare(m, M_NOWAIT);

		if (m == NULL)
			ERROUT(ENOMEM);

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
							PULLUP_CHECK(m, sizeof(struct ng_checksum_vlan_header));
							vh = (struct ng_checksum_vlan_header *) mtodo(m,
							    pullup_len - sizeof(struct ng_checksum_vlan_header));
							etype = ntohs(vh->etype);
							break;

						default:
							goto loopend;
					}
				}
loopend:
#ifdef INET
				if (etype == ETHERTYPE_IP &&
				    (m->m_pkthdr.csum_flags & NG_CHECKSUM_CSUM_IPV4)) {
					error = checksum_ipv4(priv, m, pullup_len);
					if (error == ENOBUFS)
						goto drop;
				} else
#endif
#ifdef INET6
				if (etype == ETHERTYPE_IPV6 &&
				    (m->m_pkthdr.csum_flags & NG_CHECKSUM_CSUM_IPV6)) {
					error = checksum_ipv6(priv, m, pullup_len);
					if (error == ENOBUFS)
						goto drop;
				} else
#endif
				{
					m->m_pkthdr.csum_flags &=
					    ~(NG_CHECKSUM_CSUM_IPV4|NG_CHECKSUM_CSUM_IPV6);
				}

				break;

			case DLT_RAW:
#ifdef INET
				if (m->m_pkthdr.csum_flags & NG_CHECKSUM_CSUM_IPV4)
				{
					error = checksum_ipv4(priv, m, pullup_len);

					if (error == 0)
						goto bypass;
					else if (error == ENOBUFS)
						goto drop;
				}
#endif
#ifdef INET6
				if (m->m_pkthdr.csum_flags & NG_CHECKSUM_CSUM_IPV6)
				{
					error = checksum_ipv6(priv, m, pullup_len);

					if (error == 0)
						goto bypass;
					else if (error == ENOBUFS)
						goto drop;
				}
#endif
				if (error)
					m->m_pkthdr.csum_flags &=
					    ~(NG_CHECKSUM_CSUM_IPV4|NG_CHECKSUM_CSUM_IPV6);

				break;

			default:
				ERROUT(EINVAL);
		}
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
ng_checksum_shutdown(node_p node)
{
	const priv_p priv = NG_NODE_PRIVATE(node);

	NG_NODE_SET_PRIVATE(node, NULL);
	NG_NODE_UNREF(node);

	if (priv->conf)
		free(priv->conf, M_NETGRAPH);

	free(priv, M_NETGRAPH);

	return (0);
}

static int
ng_checksum_disconnect(hook_p hook)
{
	priv_p priv;

	priv = NG_NODE_PRIVATE(NG_HOOK_NODE(hook));

	if (hook == priv->in)
		priv->in = NULL;

	if (hook == priv->out)
		priv->out = NULL;

	if (NG_NODE_NUMHOOKS(NG_HOOK_NODE(hook)) == 0 &&
	    NG_NODE_IS_VALID(NG_HOOK_NODE(hook))) /* already shutting down? */
		ng_rmnode_self(NG_HOOK_NODE(hook));

	return (0);
}
