/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004-2010 University of Zagreb
 * Copyright (c) 2007-2008 FreeBSD Foundation
 *
 * This software was developed by the University of Zagreb and the
 * FreeBSD Foundation under sponsorship by the Stichting NLnet and the
 * FreeBSD Foundation.
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

/*
 * This node permits simple traffic shaping by emulating bandwidth
 * and delay, as well as random packet losses.
 * The node has two hooks, upper and lower. Traffic flowing from upper to
 * lower hook is referenced as downstream, and vice versa. Parameters for 
 * both directions can be set separately, except for delay.
 */


#include <sys/param.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/time.h>

#include <vm/uma.h>

#include <net/vnet.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>

#include <netgraph/ng_message.h>
#include <netgraph/netgraph.h>
#include <netgraph/ng_parse.h>
#include <netgraph/ng_pipe.h>

static MALLOC_DEFINE(M_NG_PIPE, "ng_pipe", "ng_pipe");

/* Packet header struct */
struct ngp_hdr {
	TAILQ_ENTRY(ngp_hdr)	ngp_link;	/* next pkt in queue */
	struct timeval		when;		/* this packet's due time */
	struct mbuf		*m;		/* ptr to the packet data */
};
TAILQ_HEAD(p_head, ngp_hdr);

/* FIFO queue struct */
struct ngp_fifo {
	TAILQ_ENTRY(ngp_fifo)	fifo_le;	/* list of active queues only */
	struct p_head		packet_head;	/* FIFO queue head */
	u_int32_t		hash;		/* flow signature */
	struct timeval		vtime;		/* virtual time, for WFQ */
	u_int32_t		rr_deficit;	/* for DRR */
	u_int32_t		packets;	/* # of packets in this queue */
};

/* Per hook info */
struct hookinfo {
	hook_p			hook;
	int			noqueue;	/* bypass any processing */
	TAILQ_HEAD(, ngp_fifo)	fifo_head;	/* FIFO queues */
	TAILQ_HEAD(, ngp_hdr)	qout_head;	/* delay queue head */
	struct timeval		qin_utime;
	struct ng_pipe_hookcfg	cfg;
	struct ng_pipe_hookrun	run;
	struct ng_pipe_hookstat	stats;
	uint64_t		*ber_p;		/* loss_p(BER,psize) map */
};

/* Per node info */
struct node_priv {
	u_int64_t		delay;
	u_int32_t		overhead;
	u_int32_t		header_offset;
	struct hookinfo		lower;
	struct hookinfo		upper;
	struct callout		timer;
	int			timer_scheduled;
};
typedef struct node_priv *priv_p;

/* Macro for calculating the virtual time for packet dequeueing in WFQ */
#define FIFO_VTIME_SORT(plen)						\
	if (hinfo->cfg.wfq && hinfo->cfg.bandwidth) {			\
		ngp_f->vtime.tv_usec = now->tv_usec + ((uint64_t) (plen) \
			+ priv->overhead ) * hinfo->run.fifo_queues *	\
			8000000 / hinfo->cfg.bandwidth;			\
		ngp_f->vtime.tv_sec = now->tv_sec +			\
			ngp_f->vtime.tv_usec / 1000000;			\
		ngp_f->vtime.tv_usec = ngp_f->vtime.tv_usec % 1000000;	\
		TAILQ_FOREACH(ngp_f1, &hinfo->fifo_head, fifo_le)	\
			if (ngp_f1->vtime.tv_sec > ngp_f->vtime.tv_sec || \
			    (ngp_f1->vtime.tv_sec == ngp_f->vtime.tv_sec && \
			    ngp_f1->vtime.tv_usec > ngp_f->vtime.tv_usec)) \
				break;					\
		if (ngp_f1 == NULL)					\
			TAILQ_INSERT_TAIL(&hinfo->fifo_head, ngp_f, fifo_le); \
		else							\
			TAILQ_INSERT_BEFORE(ngp_f1, ngp_f, fifo_le);	\
	} else								\
		TAILQ_INSERT_TAIL(&hinfo->fifo_head, ngp_f, fifo_le);	\


static void	parse_cfg(struct ng_pipe_hookcfg *, struct ng_pipe_hookcfg *,
			struct hookinfo *, priv_p);
static void	pipe_dequeue(struct hookinfo *, struct timeval *);
static void	ngp_callout(node_p, hook_p, void *, int);
static int	ngp_modevent(module_t, int, void *);

/* zone for storing ngp_hdr-s */
static uma_zone_t ngp_zone;

/* Netgraph methods */
static ng_constructor_t	ngp_constructor;
static ng_rcvmsg_t	ngp_rcvmsg;
static ng_shutdown_t	ngp_shutdown;
static ng_newhook_t	ngp_newhook;
static ng_rcvdata_t	ngp_rcvdata;
static ng_disconnect_t	ngp_disconnect;

/* Parse type for struct ng_pipe_hookstat */
static const struct ng_parse_struct_field
	ng_pipe_hookstat_type_fields[] = NG_PIPE_HOOKSTAT_INFO;
static const struct ng_parse_type ng_pipe_hookstat_type = {
	&ng_parse_struct_type,
	&ng_pipe_hookstat_type_fields
};

/* Parse type for struct ng_pipe_stats */
static const struct ng_parse_struct_field ng_pipe_stats_type_fields[] =
	NG_PIPE_STATS_INFO(&ng_pipe_hookstat_type);
static const struct ng_parse_type ng_pipe_stats_type = {
	&ng_parse_struct_type,
	&ng_pipe_stats_type_fields
};

/* Parse type for struct ng_pipe_hookrun */
static const struct ng_parse_struct_field
	ng_pipe_hookrun_type_fields[] = NG_PIPE_HOOKRUN_INFO;
static const struct ng_parse_type ng_pipe_hookrun_type = {
	&ng_parse_struct_type,
	&ng_pipe_hookrun_type_fields
};

/* Parse type for struct ng_pipe_run */
static const struct ng_parse_struct_field
	ng_pipe_run_type_fields[] = NG_PIPE_RUN_INFO(&ng_pipe_hookrun_type);
static const struct ng_parse_type ng_pipe_run_type = {
	&ng_parse_struct_type,
	&ng_pipe_run_type_fields
};

/* Parse type for struct ng_pipe_hookcfg */
static const struct ng_parse_struct_field
	ng_pipe_hookcfg_type_fields[] = NG_PIPE_HOOKCFG_INFO;
static const struct ng_parse_type ng_pipe_hookcfg_type = {
	&ng_parse_struct_type,
	&ng_pipe_hookcfg_type_fields
};

/* Parse type for struct ng_pipe_cfg */
static const struct ng_parse_struct_field
	ng_pipe_cfg_type_fields[] = NG_PIPE_CFG_INFO(&ng_pipe_hookcfg_type);
static const struct ng_parse_type ng_pipe_cfg_type = {
	&ng_parse_struct_type,
	&ng_pipe_cfg_type_fields
};

/* List of commands and how to convert arguments to/from ASCII */
static const struct ng_cmdlist ngp_cmds[] = {
	{
		.cookie =	NGM_PIPE_COOKIE,
		.cmd =		NGM_PIPE_GET_STATS,
		.name = 	"getstats",
		.respType =	 &ng_pipe_stats_type
	},
	{
		.cookie =	NGM_PIPE_COOKIE,
		.cmd =		NGM_PIPE_CLR_STATS,
		.name =		"clrstats"
	},
	{
		.cookie =	NGM_PIPE_COOKIE,
		.cmd =		NGM_PIPE_GETCLR_STATS,
		.name =		"getclrstats",
		.respType =	&ng_pipe_stats_type
	},
	{
		.cookie =	NGM_PIPE_COOKIE,
		.cmd =		NGM_PIPE_GET_RUN,
		.name =		"getrun",
		.respType =	&ng_pipe_run_type
	},
	{
		.cookie =	NGM_PIPE_COOKIE,
		.cmd =		NGM_PIPE_GET_CFG,
		.name =		"getcfg",
		.respType =	&ng_pipe_cfg_type
	},
	{
		.cookie =	NGM_PIPE_COOKIE,
		.cmd =		NGM_PIPE_SET_CFG,
		.name =		"setcfg",
		.mesgType =	&ng_pipe_cfg_type,
	},
	{ 0 }
};

/* Netgraph type descriptor */
static struct ng_type ng_pipe_typestruct = {
	.version =	NG_ABI_VERSION,
	.name =		NG_PIPE_NODE_TYPE,
	.mod_event =	ngp_modevent,
	.constructor =	ngp_constructor,
	.shutdown =	ngp_shutdown,
	.rcvmsg =	ngp_rcvmsg,
	.newhook =	ngp_newhook,
	.rcvdata =	ngp_rcvdata,
	.disconnect =	ngp_disconnect,
	.cmdlist =	ngp_cmds
};
NETGRAPH_INIT(pipe, &ng_pipe_typestruct);

/* Node constructor */
static int
ngp_constructor(node_p node)
{
	priv_p priv;

	priv = malloc(sizeof(*priv), M_NG_PIPE, M_ZERO | M_WAITOK);
	NG_NODE_SET_PRIVATE(node, priv);

	/* Mark node as single-threaded */
	NG_NODE_FORCE_WRITER(node);

	ng_callout_init(&priv->timer);

	return (0);
}

/* Add a hook */
static int
ngp_newhook(node_p node, hook_p hook, const char *name)
{
	const priv_p priv = NG_NODE_PRIVATE(node);
	struct hookinfo *hinfo;

	if (strcmp(name, NG_PIPE_HOOK_UPPER) == 0) {
		bzero(&priv->upper, sizeof(priv->upper));
		priv->upper.hook = hook;
		NG_HOOK_SET_PRIVATE(hook, &priv->upper);
	} else if (strcmp(name, NG_PIPE_HOOK_LOWER) == 0) {
		bzero(&priv->lower, sizeof(priv->lower));
		priv->lower.hook = hook;
		NG_HOOK_SET_PRIVATE(hook, &priv->lower);
	} else
		return (EINVAL);

	/* Load non-zero initial cfg values */
	hinfo = NG_HOOK_PRIVATE(hook);
	hinfo->cfg.qin_size_limit = 50;
	hinfo->cfg.fifo = 1;
	hinfo->cfg.droptail = 1;
	TAILQ_INIT(&hinfo->fifo_head);
	TAILQ_INIT(&hinfo->qout_head);
	return (0);
}

/* Receive a control message */
static int
ngp_rcvmsg(node_p node, item_p item, hook_p lasthook)
{
	const priv_p priv = NG_NODE_PRIVATE(node);
	struct ng_mesg *resp = NULL;
	struct ng_mesg *msg, *flow_msg;
	struct ng_pipe_stats *stats;
	struct ng_pipe_run *run;
	struct ng_pipe_cfg *cfg;
	int error = 0;
	int prev_down, now_down, cmd;

	NGI_GET_MSG(item, msg);
	switch (msg->header.typecookie) {
	case NGM_PIPE_COOKIE:
		switch (msg->header.cmd) {
		case NGM_PIPE_GET_STATS:
		case NGM_PIPE_CLR_STATS:
		case NGM_PIPE_GETCLR_STATS:
			if (msg->header.cmd != NGM_PIPE_CLR_STATS) {
				NG_MKRESPONSE(resp, msg,
				    sizeof(*stats), M_NOWAIT);
				if (resp == NULL) {
					error = ENOMEM;
					break;
				}
				stats = (struct ng_pipe_stats *) resp->data;
				bcopy(&priv->upper.stats, &stats->downstream,
				    sizeof(stats->downstream));
				bcopy(&priv->lower.stats, &stats->upstream,
				    sizeof(stats->upstream));
			}
			if (msg->header.cmd != NGM_PIPE_GET_STATS) {
				bzero(&priv->upper.stats,
				    sizeof(priv->upper.stats));
				bzero(&priv->lower.stats,
				    sizeof(priv->lower.stats));
			}
			break;
		case NGM_PIPE_GET_RUN:
			NG_MKRESPONSE(resp, msg, sizeof(*run), M_NOWAIT);
			if (resp == NULL) {
				error = ENOMEM;
				break;
			}
			run = (struct ng_pipe_run *) resp->data;
			bcopy(&priv->upper.run, &run->downstream,
				sizeof(run->downstream));
			bcopy(&priv->lower.run, &run->upstream,
				sizeof(run->upstream));
			break;
		case NGM_PIPE_GET_CFG:
			NG_MKRESPONSE(resp, msg, sizeof(*cfg), M_NOWAIT);
			if (resp == NULL) {
				error = ENOMEM;
				break;
			}
			cfg = (struct ng_pipe_cfg *) resp->data;
			bcopy(&priv->upper.cfg, &cfg->downstream,
				sizeof(cfg->downstream));
			bcopy(&priv->lower.cfg, &cfg->upstream,
				sizeof(cfg->upstream));
			cfg->delay = priv->delay;
			cfg->overhead = priv->overhead;
			cfg->header_offset = priv->header_offset;
			if (cfg->upstream.bandwidth ==
			    cfg->downstream.bandwidth) {
				cfg->bandwidth = cfg->upstream.bandwidth;
				cfg->upstream.bandwidth = 0;
				cfg->downstream.bandwidth = 0;
			} else
				cfg->bandwidth = 0;
			break;
		case NGM_PIPE_SET_CFG:
			cfg = (struct ng_pipe_cfg *) msg->data;
			if (msg->header.arglen != sizeof(*cfg)) {
				error = EINVAL;
				break;
			}

			if (cfg->delay == -1)
				priv->delay = 0;
			else if (cfg->delay > 0 && cfg->delay < 10000000)
				priv->delay = cfg->delay;

			if (cfg->bandwidth == -1) {
				priv->upper.cfg.bandwidth = 0;
				priv->lower.cfg.bandwidth = 0;
				priv->overhead = 0;
			} else if (cfg->bandwidth >= 100 &&
			    cfg->bandwidth <= 1000000000) {
				priv->upper.cfg.bandwidth = cfg->bandwidth;
				priv->lower.cfg.bandwidth = cfg->bandwidth;
				if (cfg->bandwidth >= 10000000)
					priv->overhead = 8+4+12; /* Ethernet */
				else
					priv->overhead = 10; /* HDLC */
			}

			if (cfg->overhead == -1)
				priv->overhead = 0;
			else if (cfg->overhead > 0 &&
			    cfg->overhead < MAX_OHSIZE)
				priv->overhead = cfg->overhead;

			if (cfg->header_offset == -1)
				priv->header_offset = 0;
			else if (cfg->header_offset > 0 &&
			    cfg->header_offset < 64)
				priv->header_offset = cfg->header_offset;

			prev_down = priv->upper.cfg.ber == 1 ||
			    priv->lower.cfg.ber == 1;
			parse_cfg(&priv->upper.cfg, &cfg->downstream,
			    &priv->upper, priv);
			parse_cfg(&priv->lower.cfg, &cfg->upstream,
			    &priv->lower, priv);
			now_down = priv->upper.cfg.ber == 1 ||
			    priv->lower.cfg.ber == 1;

			if (prev_down != now_down) {
				if (now_down)
					cmd = NGM_LINK_IS_DOWN;
				else
					cmd = NGM_LINK_IS_UP;

				if (priv->lower.hook != NULL) {
					NG_MKMESSAGE(flow_msg, NGM_FLOW_COOKIE,
					    cmd, 0, M_NOWAIT);
					if (flow_msg != NULL)
						NG_SEND_MSG_HOOK(error, node,
						    flow_msg, priv->lower.hook,
						    0);
				}
				if (priv->upper.hook != NULL) {
					NG_MKMESSAGE(flow_msg, NGM_FLOW_COOKIE,
					    cmd, 0, M_NOWAIT);
					if (flow_msg != NULL)
						NG_SEND_MSG_HOOK(error, node,
						    flow_msg, priv->upper.hook,
						    0);
				}
			}
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

static void
parse_cfg(struct ng_pipe_hookcfg *current, struct ng_pipe_hookcfg *new,
	struct hookinfo *hinfo, priv_p priv)
{

	if (new->ber == -1) {
		current->ber = 0;
		if (hinfo->ber_p) {
			free(hinfo->ber_p, M_NG_PIPE);
			hinfo->ber_p = NULL;
		}
	} else if (new->ber >= 1 && new->ber <= 1000000000000) {
		static const uint64_t one = 0x1000000000000; /* = 2^48 */
		uint64_t p0, p;
		uint32_t fsize, i;

		if (hinfo->ber_p == NULL)
			hinfo->ber_p =
			    malloc((MAX_FSIZE + MAX_OHSIZE) * sizeof(uint64_t),
			    M_NG_PIPE, M_WAITOK);
		current->ber = new->ber;

		/*
		 * For given BER and each frame size N (in bytes) calculate
		 * the probability P_OK that the frame is clean:
		 *
		 * P_OK(BER,N) = (1 - 1/BER)^(N*8)
		 *
		 * We use a 64-bit fixed-point format with decimal point
		 * positioned between bits 47 and 48.
		 */
		p0 = one - one / new->ber;
		p = one;
		for (fsize = 0; fsize < MAX_FSIZE + MAX_OHSIZE; fsize++) {
			hinfo->ber_p[fsize] = p;
			for (i = 0; i < 8; i++)
				p = (p * (p0 & 0xffff) >> 48) +
				    (p * ((p0 >> 16) & 0xffff) >> 32) +
				    (p * (p0 >> 32) >> 16);
		}
	}

	if (new->qin_size_limit == -1)
		current->qin_size_limit = 0;
	else if (new->qin_size_limit >= 5) 
		current->qin_size_limit = new->qin_size_limit;

	if (new->qout_size_limit == -1)
		current->qout_size_limit = 0;
	else if (new->qout_size_limit >= 5)
		current->qout_size_limit = new->qout_size_limit;

	if (new->duplicate == -1)
		current->duplicate = 0;
	else if (new->duplicate > 0 && new->duplicate <= 50)
		current->duplicate = new->duplicate;

	if (new->fifo) {
		current->fifo = 1;
		current->wfq = 0;
		current->drr = 0;
	}

	if (new->wfq) {
		current->fifo = 0;
		current->wfq = 1;
		current->drr = 0;
	}

	if (new->drr) {
		current->fifo = 0;
		current->wfq = 0;
		/* DRR quantum */
		if (new->drr >= 32)
			current->drr = new->drr;
		else
			current->drr = 2048;		/* default quantum */
	}

	if (new->droptail) {
		current->droptail = 1;
		current->drophead = 0;
	}

	if (new->drophead) {
		current->droptail = 0;
		current->drophead = 1;
	}

	if (new->bandwidth == -1) {
		current->bandwidth = 0;
		current->fifo = 1;
		current->wfq = 0;
		current->drr = 0;
	} else if (new->bandwidth >= 100 && new->bandwidth <= 1000000000)
		current->bandwidth = new->bandwidth;

	if (current->bandwidth | priv->delay | 
	    current->duplicate | current->ber)
		hinfo->noqueue = 0;
	else
		hinfo->noqueue = 1;
}

/*
 * Compute a hash signature for a packet. This function suffers from the
 * NIH sindrome, so probably it would be wise to look around what other
 * folks have found out to be a good and efficient IP hash function...
 */
static int
ip_hash(struct mbuf *m, int offset)
{
	u_int64_t i;
	struct ip *ip = (struct ip *)(mtod(m, u_char *) + offset);

	if (m->m_len < sizeof(struct ip) + offset ||
	    ip->ip_v != 4 || ip->ip_hl << 2 != sizeof(struct ip))
		return 0;

	i = ((u_int64_t) ip->ip_src.s_addr ^
	    ((u_int64_t) ip->ip_src.s_addr << 13) ^
	    ((u_int64_t) ip->ip_dst.s_addr << 7) ^
	    ((u_int64_t) ip->ip_dst.s_addr << 19));
	return (i ^ (i >> 32));
}

/*
 * Receive data on a hook - both in upstream and downstream direction.
 * We put the frame on the inbound queue, and try to initiate dequeuing
 * sequence immediately. If inbound queue is full, discard one frame
 * depending on dropping policy (from the head or from the tail of the
 * queue).
 */
static int
ngp_rcvdata(hook_p hook, item_p item)
{
	struct hookinfo *const hinfo = NG_HOOK_PRIVATE(hook);
	const priv_p priv = NG_NODE_PRIVATE(NG_HOOK_NODE(hook));
	struct timeval uuptime;
	struct timeval *now = &uuptime;
	struct ngp_fifo *ngp_f = NULL, *ngp_f1;
	struct ngp_hdr *ngp_h = NULL;
	struct mbuf *m;
	int hash, plen;
	int error = 0;

	/*
	 * Shortcut from inbound to outbound hook when neither of
	 * bandwidth, delay, BER or duplication probability is
	 * configured, nor we have queued frames to drain.
	 */
	if (hinfo->run.qin_frames == 0 && hinfo->run.qout_frames == 0 &&
	    hinfo->noqueue) {
		struct hookinfo *dest;
		if (hinfo == &priv->lower)
			dest = &priv->upper;
		else
			dest = &priv->lower;

		/* Send the frame. */
		plen = NGI_M(item)->m_pkthdr.len;
		NG_FWD_ITEM_HOOK(error, item, dest->hook);

		/* Update stats. */
		if (error) {
			hinfo->stats.out_disc_frames++;
			hinfo->stats.out_disc_octets += plen;
		} else {
			hinfo->stats.fwd_frames++;
			hinfo->stats.fwd_octets += plen;
		}

		return (error);
	}

	microuptime(now);

	/*
	 * If this was an empty queue, update service deadline time.
	 */
	if (hinfo->run.qin_frames == 0) {
		struct timeval *when = &hinfo->qin_utime;
		if (when->tv_sec < now->tv_sec || (when->tv_sec == now->tv_sec
		    && when->tv_usec < now->tv_usec)) {
			when->tv_sec = now->tv_sec;
			when->tv_usec = now->tv_usec;
		}
	}

	/* Populate the packet header */
	ngp_h = uma_zalloc(ngp_zone, M_NOWAIT);
	KASSERT((ngp_h != NULL), ("ngp_h zalloc failed (1)"));
	NGI_GET_M(item, m);
	KASSERT(m != NULL, ("NGI_GET_M failed"));
	ngp_h->m = m;
	NG_FREE_ITEM(item);

	if (hinfo->cfg.fifo)
		hash = 0;	/* all packets go into a single FIFO queue */
	else
		hash = ip_hash(m, priv->header_offset);

	/* Find the appropriate FIFO queue for the packet and enqueue it*/
	TAILQ_FOREACH(ngp_f, &hinfo->fifo_head, fifo_le)
		if (hash == ngp_f->hash)
			break;
	if (ngp_f == NULL) {
		ngp_f = uma_zalloc(ngp_zone, M_NOWAIT);
		KASSERT(ngp_h != NULL, ("ngp_h zalloc failed (2)"));
		TAILQ_INIT(&ngp_f->packet_head);
		ngp_f->hash = hash;
		ngp_f->packets = 1;
		ngp_f->rr_deficit = hinfo->cfg.drr;	/* DRR quantum */
		hinfo->run.fifo_queues++;
		TAILQ_INSERT_TAIL(&ngp_f->packet_head, ngp_h, ngp_link);
		FIFO_VTIME_SORT(m->m_pkthdr.len);
	} else {
		TAILQ_INSERT_TAIL(&ngp_f->packet_head, ngp_h, ngp_link);
		ngp_f->packets++;
	}
	hinfo->run.qin_frames++;
	hinfo->run.qin_octets += m->m_pkthdr.len;

	/* Discard a frame if inbound queue limit has been reached */
	if (hinfo->run.qin_frames > hinfo->cfg.qin_size_limit) {
		struct mbuf *m1;
		int longest = 0;

		/* Find the longest queue */
		TAILQ_FOREACH(ngp_f1, &hinfo->fifo_head, fifo_le)
			if (ngp_f1->packets > longest) {
				longest = ngp_f1->packets;
				ngp_f = ngp_f1;
			}

		/* Drop a frame from the queue head/tail, depending on cfg */
		if (hinfo->cfg.drophead) 
			ngp_h = TAILQ_FIRST(&ngp_f->packet_head);
		else 
			ngp_h = TAILQ_LAST(&ngp_f->packet_head, p_head);
		TAILQ_REMOVE(&ngp_f->packet_head, ngp_h, ngp_link);
		m1 = ngp_h->m;
		uma_zfree(ngp_zone, ngp_h);
		hinfo->run.qin_octets -= m1->m_pkthdr.len;
		hinfo->stats.in_disc_octets += m1->m_pkthdr.len;
		m_freem(m1);
		if (--(ngp_f->packets) == 0) {
			TAILQ_REMOVE(&hinfo->fifo_head, ngp_f, fifo_le);
			uma_zfree(ngp_zone, ngp_f);
			hinfo->run.fifo_queues--;
		}
		hinfo->run.qin_frames--;
		hinfo->stats.in_disc_frames++;
	}

	/*
	 * Try to start the dequeuing process immediately.
	 */
	pipe_dequeue(hinfo, now);

	return (0);
}


/*
 * Dequeueing sequence - we basically do the following:
 *  1) Try to extract the frame from the inbound (bandwidth) queue;
 *  2) In accordance to BER specified, discard the frame randomly;
 *  3) If the frame survives BER, prepend it with delay info and move it
 *     to outbound (delay) queue;
 *  4) Loop to 2) until bandwidth quota for this timeslice is reached, or
 *     inbound queue is flushed completely;
 *  5) Dequeue frames from the outbound queue and send them downstream until
 *     outbound queue is flushed completely, or the next frame in the queue
 *     is not due to be dequeued yet
 */
static void
pipe_dequeue(struct hookinfo *hinfo, struct timeval *now) {
	static uint64_t rand, oldrand;
	const node_p node = NG_HOOK_NODE(hinfo->hook);
	const priv_p priv = NG_NODE_PRIVATE(node);
	struct hookinfo *dest;
	struct ngp_fifo *ngp_f, *ngp_f1;
	struct ngp_hdr *ngp_h;
	struct timeval *when;
	struct mbuf *m;
	int plen, error = 0;

	/* Which one is the destination hook? */
	if (hinfo == &priv->lower)
		dest = &priv->upper;
	else
		dest = &priv->lower;

	/* Bandwidth queue processing */
	while ((ngp_f = TAILQ_FIRST(&hinfo->fifo_head))) {
		when = &hinfo->qin_utime;
		if (when->tv_sec > now->tv_sec || (when->tv_sec == now->tv_sec
		    && when->tv_usec > now->tv_usec))
			break;

		ngp_h = TAILQ_FIRST(&ngp_f->packet_head);
		m = ngp_h->m;

		/* Deficit Round Robin (DRR) processing */
		if (hinfo->cfg.drr) {
			if (ngp_f->rr_deficit >= m->m_pkthdr.len) {
				ngp_f->rr_deficit -= m->m_pkthdr.len;
			} else {
				ngp_f->rr_deficit += hinfo->cfg.drr;
				TAILQ_REMOVE(&hinfo->fifo_head, ngp_f, fifo_le);
				TAILQ_INSERT_TAIL(&hinfo->fifo_head,
				    ngp_f, fifo_le);
				continue;
			}
		}

		/*
		 * Either create a duplicate and pass it on, or dequeue
		 * the original packet...
		 */
		if (hinfo->cfg.duplicate &&
		    random() % 100 <= hinfo->cfg.duplicate) {
			ngp_h = uma_zalloc(ngp_zone, M_NOWAIT);
			KASSERT(ngp_h != NULL, ("ngp_h zalloc failed (3)"));
			m = m_dup(m, M_NOWAIT);
			KASSERT(m != NULL, ("m_dup failed"));
			ngp_h->m = m;
		} else {
			TAILQ_REMOVE(&ngp_f->packet_head, ngp_h, ngp_link);
			hinfo->run.qin_frames--;
			hinfo->run.qin_octets -= m->m_pkthdr.len;
			ngp_f->packets--;
		}
		
		/* Calculate the serialization delay */
		if (hinfo->cfg.bandwidth) {
			hinfo->qin_utime.tv_usec +=
			    ((uint64_t) m->m_pkthdr.len + priv->overhead ) *
			    8000000 / hinfo->cfg.bandwidth;
			hinfo->qin_utime.tv_sec +=
			    hinfo->qin_utime.tv_usec / 1000000;
			hinfo->qin_utime.tv_usec =
			    hinfo->qin_utime.tv_usec % 1000000;
		}
		when = &ngp_h->when;
		when->tv_sec = hinfo->qin_utime.tv_sec;
		when->tv_usec = hinfo->qin_utime.tv_usec;

		/* Sort / rearrange inbound queues */
		if (ngp_f->packets) {
			if (hinfo->cfg.wfq) {
				TAILQ_REMOVE(&hinfo->fifo_head, ngp_f, fifo_le);
				FIFO_VTIME_SORT(TAILQ_FIRST(
				    &ngp_f->packet_head)->m->m_pkthdr.len)
			}
		} else {
			TAILQ_REMOVE(&hinfo->fifo_head, ngp_f, fifo_le);
			uma_zfree(ngp_zone, ngp_f);
			hinfo->run.fifo_queues--;
		}

		/* Randomly discard the frame, according to BER setting */
		if (hinfo->cfg.ber) {
			oldrand = rand;
			rand = random();
			if (((oldrand ^ rand) << 17) >=
			    hinfo->ber_p[priv->overhead + m->m_pkthdr.len]) {
				hinfo->stats.out_disc_frames++;
				hinfo->stats.out_disc_octets += m->m_pkthdr.len;
				uma_zfree(ngp_zone, ngp_h);
				m_freem(m);
				continue;
			}
		}

		/* Discard frame if outbound queue size limit exceeded */
		if (hinfo->cfg.qout_size_limit &&
		    hinfo->run.qout_frames>=hinfo->cfg.qout_size_limit) {
			hinfo->stats.out_disc_frames++;
			hinfo->stats.out_disc_octets += m->m_pkthdr.len;
			uma_zfree(ngp_zone, ngp_h);
			m_freem(m);
			continue;
		}

		/* Calculate the propagation delay */
		when->tv_usec += priv->delay;
		when->tv_sec += when->tv_usec / 1000000;
		when->tv_usec = when->tv_usec % 1000000;

		/* Put the frame into the delay queue */
		TAILQ_INSERT_TAIL(&hinfo->qout_head, ngp_h, ngp_link);
		hinfo->run.qout_frames++;
		hinfo->run.qout_octets += m->m_pkthdr.len;
	}

	/* Delay queue processing */
	while ((ngp_h = TAILQ_FIRST(&hinfo->qout_head))) {
		when = &ngp_h->when;
		m = ngp_h->m;
		if (when->tv_sec > now->tv_sec ||
		    (when->tv_sec == now->tv_sec &&
		    when->tv_usec > now->tv_usec))
			break;

		/* Update outbound queue stats */
		plen = m->m_pkthdr.len;
		hinfo->run.qout_frames--;
		hinfo->run.qout_octets -= plen;

		/* Dequeue the packet from qout */
		TAILQ_REMOVE(&hinfo->qout_head, ngp_h, ngp_link);
		uma_zfree(ngp_zone, ngp_h);

		NG_SEND_DATA(error, dest->hook, m, meta);
		if (error) {
			hinfo->stats.out_disc_frames++;
			hinfo->stats.out_disc_octets += plen;
		} else {
			hinfo->stats.fwd_frames++;
			hinfo->stats.fwd_octets += plen;
		}
	}

	if ((hinfo->run.qin_frames != 0 || hinfo->run.qout_frames != 0) &&
	    !priv->timer_scheduled) {
		ng_callout(&priv->timer, node, NULL, 1, ngp_callout, NULL, 0);
		priv->timer_scheduled = 1;
	}
}

/*
 * This routine is called on every clock tick.  We poll connected hooks
 * for queued frames by calling pipe_dequeue().
 */
static void
ngp_callout(node_p node, hook_p hook, void *arg1, int arg2)
{
	const priv_p priv = NG_NODE_PRIVATE(node);
	struct timeval now;

	priv->timer_scheduled = 0;
	microuptime(&now);
	if (priv->upper.hook != NULL)
		pipe_dequeue(&priv->upper, &now);
	if (priv->lower.hook != NULL)
		pipe_dequeue(&priv->lower, &now);
}

/*
 * Shutdown processing
 *
 * This is tricky. If we have both a lower and upper hook, then we
 * probably want to extricate ourselves and leave the two peers
 * still linked to each other. Otherwise we should just shut down as
 * a normal node would.
 */
static int
ngp_shutdown(node_p node)
{
	const priv_p priv = NG_NODE_PRIVATE(node);

	if (priv->timer_scheduled)
		ng_uncallout(&priv->timer, node);
	if (priv->lower.hook && priv->upper.hook)
		ng_bypass(priv->lower.hook, priv->upper.hook);
	else {
		if (priv->upper.hook != NULL)
			ng_rmhook_self(priv->upper.hook);
		if (priv->lower.hook != NULL)
			ng_rmhook_self(priv->lower.hook);
	}
	NG_NODE_UNREF(node);
	free(priv, M_NG_PIPE);
	return (0);
}


/*
 * Hook disconnection
 */
static int
ngp_disconnect(hook_p hook)
{
	struct hookinfo *const hinfo = NG_HOOK_PRIVATE(hook);
	struct ngp_fifo *ngp_f;
	struct ngp_hdr *ngp_h;

	KASSERT(hinfo != NULL, ("%s: null info", __FUNCTION__));
	hinfo->hook = NULL;

	/* Flush all fifo queues associated with the hook */
	while ((ngp_f = TAILQ_FIRST(&hinfo->fifo_head))) {
		while ((ngp_h = TAILQ_FIRST(&ngp_f->packet_head))) {
			TAILQ_REMOVE(&ngp_f->packet_head, ngp_h, ngp_link);
			m_freem(ngp_h->m);
			uma_zfree(ngp_zone, ngp_h);
		}
		TAILQ_REMOVE(&hinfo->fifo_head, ngp_f, fifo_le);
		uma_zfree(ngp_zone, ngp_f);
	}

	/* Flush the delay queue */
	while ((ngp_h = TAILQ_FIRST(&hinfo->qout_head))) {
		TAILQ_REMOVE(&hinfo->qout_head, ngp_h, ngp_link);
		m_freem(ngp_h->m);
		uma_zfree(ngp_zone, ngp_h);
	}

	/* Release the packet loss probability table (BER) */
	if (hinfo->ber_p)
		free(hinfo->ber_p, M_NG_PIPE);

	return (0);
}

static int
ngp_modevent(module_t mod, int type, void *unused)
{
	int error = 0;

	switch (type) {
	case MOD_LOAD:
		ngp_zone = uma_zcreate("ng_pipe", max(sizeof(struct ngp_hdr),
		    sizeof (struct ngp_fifo)), NULL, NULL, NULL, NULL,
		    UMA_ALIGN_PTR, 0);
		if (ngp_zone == NULL)
			panic("ng_pipe: couldn't allocate descriptor zone");
		break;
	case MOD_UNLOAD:
		uma_zdestroy(ngp_zone);
		break;
	default:
		error = EOPNOTSUPP;
		break;
	}

	return (error);
}
