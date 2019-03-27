/*-
 * ng_tcpmss.c
 *
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004, Alexey Popov <lollypop@flexuser.ru>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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
 * This software includes fragments of the following programs:
 *	tcpmssd		Ruslan Ermilov <ru@FreeBSD.org>
 *
 * $FreeBSD$
 */

/*
 * This node is netgraph tool for workaround of PMTUD problem. It acts
 * like filter for IP packets. If configured, it reduces MSS of TCP SYN
 * packets.
 *
 * Configuration can be done by sending NGM_TCPMSS_CONFIG message. The
 * message sets filter for incoming packets on hook 'inHook'. Packet's
 * TCP MSS field is lowered to 'maxMSS' parameter and resulting packet
 * is sent to 'outHook'.
 *
 * XXX: statistics are updated not atomically, so they may broke on SMP.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/endian.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>

#include <netgraph/ng_message.h>
#include <netgraph/netgraph.h>
#include <netgraph/ng_parse.h>
#include <netgraph/ng_tcpmss.h>

/* Per hook info. */
typedef struct {
	hook_p				outHook;
	struct ng_tcpmss_hookstat	stats;
} *hpriv_p;

/* Netgraph methods. */
static ng_constructor_t	ng_tcpmss_constructor;
static ng_rcvmsg_t	ng_tcpmss_rcvmsg;
static ng_newhook_t	ng_tcpmss_newhook;
static ng_rcvdata_t	ng_tcpmss_rcvdata;
static ng_disconnect_t	ng_tcpmss_disconnect;

static int correct_mss(struct tcphdr *, int, uint16_t, int);

/* Parse type for struct ng_tcpmss_hookstat. */
static const struct ng_parse_struct_field ng_tcpmss_hookstat_type_fields[]
	= NG_TCPMSS_HOOKSTAT_INFO;
static const struct ng_parse_type ng_tcpmss_hookstat_type = {
	&ng_parse_struct_type,
	&ng_tcpmss_hookstat_type_fields
};

/* Parse type for struct ng_tcpmss_config. */
static const struct ng_parse_struct_field ng_tcpmss_config_type_fields[]
	= NG_TCPMSS_CONFIG_INFO;
static const struct ng_parse_type ng_tcpmss_config_type = {
	&ng_parse_struct_type,
	ng_tcpmss_config_type_fields
};

/* List of commands and how to convert arguments to/from ASCII. */
static const struct ng_cmdlist ng_tcpmss_cmds[] = {
	{
	  NGM_TCPMSS_COOKIE,
	  NGM_TCPMSS_GET_STATS,
	  "getstats",
	  &ng_parse_hookbuf_type,
	  &ng_tcpmss_hookstat_type
	},
	{
	  NGM_TCPMSS_COOKIE,
	  NGM_TCPMSS_CLR_STATS,
	  "clrstats",
	  &ng_parse_hookbuf_type,
	  NULL
	},
	{
	  NGM_TCPMSS_COOKIE,
	  NGM_TCPMSS_GETCLR_STATS,
	  "getclrstats",
	  &ng_parse_hookbuf_type,
	  &ng_tcpmss_hookstat_type
	},
	{
	  NGM_TCPMSS_COOKIE,
	  NGM_TCPMSS_CONFIG,
	  "config",
	  &ng_tcpmss_config_type,
	  NULL
	},
	{ 0 }
};

/* Netgraph type descriptor. */
static struct ng_type ng_tcpmss_typestruct = {
	.version =	NG_ABI_VERSION,
	.name =		NG_TCPMSS_NODE_TYPE,
	.constructor =	ng_tcpmss_constructor,
	.rcvmsg =	ng_tcpmss_rcvmsg,
	.newhook =	ng_tcpmss_newhook,
	.rcvdata =	ng_tcpmss_rcvdata,
	.disconnect =	ng_tcpmss_disconnect,
	.cmdlist =	ng_tcpmss_cmds,
};

NETGRAPH_INIT(tcpmss, &ng_tcpmss_typestruct);

#define	ERROUT(x)	{ error = (x); goto done; }

/*
 * Node constructor. No special actions required.
 */
static int
ng_tcpmss_constructor(node_p node)
{
	return (0);
}

/*
 * Add a hook. Any unique name is OK.
 */
static int
ng_tcpmss_newhook(node_p node, hook_p hook, const char *name)
{
	hpriv_p priv;

	priv = malloc(sizeof(*priv), M_NETGRAPH, M_NOWAIT | M_ZERO);
	if (priv == NULL)
		return (ENOMEM);

	NG_HOOK_SET_PRIVATE(hook, priv);

	return (0);
}

/*
 * Receive a control message.
 */
static int
ng_tcpmss_rcvmsg
(node_p node, item_p item, hook_p lasthook)
{
	struct ng_mesg *msg, *resp = NULL;
	int error = 0;

	NGI_GET_MSG(item, msg);

	switch (msg->header.typecookie) {
	case NGM_TCPMSS_COOKIE:
		switch (msg->header.cmd) {
		case NGM_TCPMSS_GET_STATS:
		case NGM_TCPMSS_CLR_STATS:
		case NGM_TCPMSS_GETCLR_STATS:
		    {
			hook_p hook;
			hpriv_p priv;

			/* Check that message is long enough. */
			if (msg->header.arglen != NG_HOOKSIZ)
				ERROUT(EINVAL);

			/* Find this hook. */
			hook = ng_findhook(node, (char *)msg->data);
			if (hook == NULL)
				ERROUT(ENOENT);

			priv = NG_HOOK_PRIVATE(hook);

			/* Create response. */
			if (msg->header.cmd != NGM_TCPMSS_CLR_STATS) {
				NG_MKRESPONSE(resp, msg,
				    sizeof(struct ng_tcpmss_hookstat), M_NOWAIT);
				if (resp == NULL)
					ERROUT(ENOMEM);
				bcopy(&priv->stats, resp->data,
				    sizeof(struct ng_tcpmss_hookstat));	
			}

			if (msg->header.cmd != NGM_TCPMSS_GET_STATS)
				bzero(&priv->stats,
				    sizeof(struct ng_tcpmss_hookstat));
			break;
		    }
		case NGM_TCPMSS_CONFIG:
		    {
			struct ng_tcpmss_config *set;
			hook_p in, out;
			hpriv_p priv;

			/* Check that message is long enough. */
			if (msg->header.arglen !=
			    sizeof(struct ng_tcpmss_config))
				ERROUT(EINVAL);

			set = (struct ng_tcpmss_config *)msg->data;
			in = ng_findhook(node, set->inHook);
			out = ng_findhook(node, set->outHook);
			if (in == NULL || out == NULL)
				ERROUT(ENOENT);

			/* Configure MSS hack. */
			priv = NG_HOOK_PRIVATE(in);
			priv->outHook = out;
			priv->stats.maxMSS = set->maxMSS;

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

done:
	NG_RESPOND_MSG(error, node, item, resp);
	NG_FREE_MSG(msg);

	return (error);
}

/*
 * Receive data on a hook, and hack MSS.
 *
 */
static int
ng_tcpmss_rcvdata(hook_p hook, item_p item)
{
	hpriv_p priv = NG_HOOK_PRIVATE(hook);
	struct mbuf *m = NULL;
	struct ip *ip;
	struct tcphdr *tcp;
	int iphlen, tcphlen, pktlen;
	int pullup_len = 0;
	int error = 0;

	/* Drop packets if filter is not configured on this hook. */
	if (priv->outHook == NULL)
		goto done;

	NGI_GET_M(item, m);

	/* Update stats on incoming hook. */
	pktlen = m->m_pkthdr.len;
	priv->stats.Octets += pktlen;
	priv->stats.Packets++;

	/* Check whether we configured to fix MSS. */
	if (priv->stats.maxMSS == 0)
		goto send;

#define	M_CHECK(length) do {					\
	pullup_len += length;					\
	if ((m)->m_pkthdr.len < pullup_len)			\
		goto send;					\
	if ((m)->m_len < pullup_len &&				\
	   (((m) = m_pullup((m), pullup_len)) == NULL))		\
		ERROUT(ENOBUFS);				\
	} while (0)

	/* Check mbuf packet size and arrange for IP header. */
	M_CHECK(sizeof(struct ip));
	ip = mtod(m, struct ip *);

	/* Check IP version. */
	if (ip->ip_v != IPVERSION)
		ERROUT(EINVAL);

	/* Check IP header length. */
	iphlen = ip->ip_hl << 2;
	if (iphlen < sizeof(struct ip) || iphlen > pktlen )
		ERROUT(EINVAL);

        /* Check if it is TCP. */
	if (!(ip->ip_p == IPPROTO_TCP))
		goto send;

	/* Check mbuf packet size and arrange for IP+TCP header */
	M_CHECK(iphlen - sizeof(struct ip) + sizeof(struct tcphdr));
	ip = mtod(m, struct ip *);
	tcp = (struct tcphdr *)((caddr_t )ip + iphlen);

	/* Check TCP header length. */
	tcphlen = tcp->th_off << 2;
	if (tcphlen < sizeof(struct tcphdr) || tcphlen > pktlen - iphlen)
		ERROUT(EINVAL);

	/* Check SYN packet and has options. */
	if (!(tcp->th_flags & TH_SYN) || tcphlen == sizeof(struct tcphdr))
		goto send;
	
	/* Update SYN stats. */
	priv->stats.SYNPkts++;

	M_CHECK(tcphlen - sizeof(struct tcphdr));
	ip = mtod(m, struct ip *);
	tcp = (struct tcphdr *)((caddr_t )ip + iphlen);

#undef	M_CHECK

	/* Fix MSS and update stats. */
	if (correct_mss(tcp, tcphlen, priv->stats.maxMSS,
	    m->m_pkthdr.csum_flags))
		priv->stats.FixedPkts++;

send:
	/* Deliver frame out destination hook. */
	NG_FWD_NEW_DATA(error, item, priv->outHook, m);

	return (error);

done:
	NG_FREE_ITEM(item);
	NG_FREE_M(m);

	return (error);
}

/*
 * Hook disconnection.
 * We must check all hooks, since they may reference this one.
 */
static int
ng_tcpmss_disconnect(hook_p hook)
{
	node_p node = NG_HOOK_NODE(hook);
	hook_p hook2;

	LIST_FOREACH(hook2, &node->nd_hooks, hk_hooks) {
		hpriv_p priv = NG_HOOK_PRIVATE(hook2);

		if (priv->outHook == hook)
			priv->outHook = NULL;
	}

	free(NG_HOOK_PRIVATE(hook), M_NETGRAPH);

	if (NG_NODE_NUMHOOKS(NG_HOOK_NODE(hook)) == 0)
		ng_rmnode_self(NG_HOOK_NODE(hook));

	return (0);
}

/*
 * Code from tcpmssd.
 */

/*-
 * The following macro is used to update an
 * internet checksum.  "acc" is a 32-bit
 * accumulation of all the changes to the
 * checksum (adding in old 16-bit words and
 * subtracting out new words), and "cksum"
 * is the checksum value to be updated.
 */
#define TCPMSS_ADJUST_CHECKSUM(acc, cksum) do {		\
	acc += cksum;					\
	if (acc < 0) {					\
		acc = -acc;				\
		acc = (acc >> 16) + (acc & 0xffff);	\
		acc += acc >> 16;			\
		cksum = (u_short) ~acc;			\
	} else {					\
		acc = (acc >> 16) + (acc & 0xffff);	\
		acc += acc >> 16;			\
		cksum = (u_short) acc;			\
	}						\
} while (0);

static int
correct_mss(struct tcphdr *tc, int hlen, uint16_t maxmss, int flags)
{
	int olen, optlen;
	u_char *opt;
	int accumulate;
	int res = 0;
	uint16_t sum;

	for (olen = hlen - sizeof(struct tcphdr), opt = (u_char *)(tc + 1);
	     olen > 0; olen -= optlen, opt += optlen) {
		if (*opt == TCPOPT_EOL)
			break;
		else if (*opt == TCPOPT_NOP)
			optlen = 1;
		else {
			optlen = *(opt + 1);
			if (optlen <= 0 || optlen > olen)
				break;
			if (*opt == TCPOPT_MAXSEG) {
				if (optlen != TCPOLEN_MAXSEG)
					continue;
				accumulate = be16dec(opt + 2);
				if (accumulate > maxmss) {
					if ((flags & CSUM_TCP) == 0) {
						accumulate -= maxmss;
						sum = be16dec(&tc->th_sum);
						TCPMSS_ADJUST_CHECKSUM(accumulate, sum);
						be16enc(&tc->th_sum, sum);
					}
					be16enc(opt + 2, maxmss);
					res = 1;
				}
			}
		}
	}
	return (res);
}
