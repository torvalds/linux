/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005 Nuno Antunes <nuno.antunes@gmail.com>
 * Copyright (c) 2007 Alexander Motin <mav@freebsd.org>
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
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
 * ng_car - An implementation of committed access rate for netgraph
 *
 * TODO:
 *	- Sanitize input config values (impose some limits)
 *	- Implement internal packet painting (possibly using mbuf tags)
 *	- Implement color-aware mode
 *	- Implement DSCP marking for IPv4
 */

#include <sys/param.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>

#include <netgraph/ng_message.h>
#include <netgraph/ng_parse.h>
#include <netgraph/netgraph.h>
#include <netgraph/ng_car.h>

#define NG_CAR_QUEUE_SIZE	100	/* Maximum queue size for SHAPE mode */
#define NG_CAR_QUEUE_MIN_TH	8	/* Minimum RED threshold for SHAPE mode */

/* Hook private info */
struct hookinfo {
	hook_p		hook;		/* this (source) hook */
	hook_p		dest;		/* destination hook */

	int64_t 	tc;		/* committed token bucket counter */
	int64_t 	te;		/* exceeded/peak token bucket counter */
	struct bintime	lastRefill;	/* last token refill time */

	struct ng_car_hookconf conf;	/* hook configuration */
	struct ng_car_hookstats stats;	/* hook stats */

	struct mbuf	*q[NG_CAR_QUEUE_SIZE];	/* circular packet queue */
	u_int		q_first;	/* first queue element */
	u_int		q_last;		/* last queue element */
	struct callout	q_callout;	/* periodic queue processing routine */
	struct mtx	q_mtx;		/* queue mutex */
};

/* Private information for each node instance */
struct privdata {
	node_p node;				/* the node itself */
	struct hookinfo upper;			/* hook to upper layers */
	struct hookinfo lower;			/* hook to lower layers */
};
typedef struct privdata *priv_p;

static ng_constructor_t	ng_car_constructor;
static ng_rcvmsg_t	ng_car_rcvmsg;
static ng_shutdown_t	ng_car_shutdown;
static ng_newhook_t	ng_car_newhook;
static ng_rcvdata_t	ng_car_rcvdata;
static ng_disconnect_t	ng_car_disconnect;

static void	ng_car_refillhook(struct hookinfo *h);
static void	ng_car_schedule(struct hookinfo *h);
void		ng_car_q_event(node_p node, hook_p hook, void *arg, int arg2);
static void	ng_car_enqueue(struct hookinfo *h, item_p item);

/* Parse type for struct ng_car_hookstats */
static const struct ng_parse_struct_field ng_car_hookstats_type_fields[]
	= NG_CAR_HOOKSTATS;
static const struct ng_parse_type ng_car_hookstats_type = {
	&ng_parse_struct_type,
	&ng_car_hookstats_type_fields
};

/* Parse type for struct ng_car_bulkstats */
static const struct ng_parse_struct_field ng_car_bulkstats_type_fields[]
	= NG_CAR_BULKSTATS(&ng_car_hookstats_type);
static const struct ng_parse_type ng_car_bulkstats_type = {
	&ng_parse_struct_type,
	&ng_car_bulkstats_type_fields
};

/* Parse type for struct ng_car_hookconf */
static const struct ng_parse_struct_field ng_car_hookconf_type_fields[]
	= NG_CAR_HOOKCONF;
static const struct ng_parse_type ng_car_hookconf_type = {
	&ng_parse_struct_type,
	&ng_car_hookconf_type_fields
};

/* Parse type for struct ng_car_bulkconf */
static const struct ng_parse_struct_field ng_car_bulkconf_type_fields[]
	= NG_CAR_BULKCONF(&ng_car_hookconf_type);
static const struct ng_parse_type ng_car_bulkconf_type = {
	&ng_parse_struct_type,
	&ng_car_bulkconf_type_fields
};

/* Command list */
static struct ng_cmdlist ng_car_cmdlist[] = {
	{
	  NGM_CAR_COOKIE,
	  NGM_CAR_GET_STATS,
	  "getstats",
	  NULL,
	  &ng_car_bulkstats_type,
	},
	{
	  NGM_CAR_COOKIE,
	  NGM_CAR_CLR_STATS,
	  "clrstats",
	  NULL,
	  NULL,
	},
	{
	  NGM_CAR_COOKIE,
	  NGM_CAR_GETCLR_STATS,
	  "getclrstats",
	  NULL,
	  &ng_car_bulkstats_type,
	},

	{
	  NGM_CAR_COOKIE,
	  NGM_CAR_GET_CONF,
	  "getconf",
	  NULL,
	  &ng_car_bulkconf_type,
	},
	{
	  NGM_CAR_COOKIE,
	  NGM_CAR_SET_CONF,
	  "setconf",
	  &ng_car_bulkconf_type,
	  NULL,
	},
	{ 0 }
};

/* Netgraph node type descriptor */
static struct ng_type ng_car_typestruct = {
	.version =	NG_ABI_VERSION,
	.name =		NG_CAR_NODE_TYPE,
	.constructor =	ng_car_constructor,
	.rcvmsg =	ng_car_rcvmsg,
	.shutdown =	ng_car_shutdown,
	.newhook =	ng_car_newhook,
	.rcvdata =	ng_car_rcvdata,
	.disconnect =	ng_car_disconnect,
	.cmdlist =	ng_car_cmdlist,
};
NETGRAPH_INIT(car, &ng_car_typestruct);

/*
 * Node constructor
 */
static int
ng_car_constructor(node_p node)
{
	priv_p priv;

	/* Initialize private descriptor. */
	priv = malloc(sizeof(*priv), M_NETGRAPH, M_WAITOK | M_ZERO);

	NG_NODE_SET_PRIVATE(node, priv);
	priv->node = node;

	/*
	 * Arbitrary default values
	 */

	priv->upper.hook = NULL;
	priv->upper.dest = NULL;
	priv->upper.tc = priv->upper.conf.cbs = NG_CAR_CBS_MIN;
	priv->upper.te = priv->upper.conf.ebs = NG_CAR_EBS_MIN;
	priv->upper.conf.cir = NG_CAR_CIR_DFLT;
	priv->upper.conf.green_action = NG_CAR_ACTION_FORWARD;
	priv->upper.conf.yellow_action = NG_CAR_ACTION_FORWARD;
	priv->upper.conf.red_action = NG_CAR_ACTION_DROP;
	priv->upper.conf.mode = 0;
	getbinuptime(&priv->upper.lastRefill);
	priv->upper.q_first = 0;
	priv->upper.q_last = 0;
	ng_callout_init(&priv->upper.q_callout);
	mtx_init(&priv->upper.q_mtx, "ng_car_u", NULL, MTX_DEF);

	priv->lower.hook = NULL;
	priv->lower.dest = NULL;
	priv->lower.tc = priv->lower.conf.cbs = NG_CAR_CBS_MIN;
	priv->lower.te = priv->lower.conf.ebs = NG_CAR_EBS_MIN;
	priv->lower.conf.cir = NG_CAR_CIR_DFLT;
	priv->lower.conf.green_action = NG_CAR_ACTION_FORWARD;
	priv->lower.conf.yellow_action = NG_CAR_ACTION_FORWARD;
	priv->lower.conf.red_action = NG_CAR_ACTION_DROP;
	priv->lower.conf.mode = 0;
	priv->lower.lastRefill = priv->upper.lastRefill;
	priv->lower.q_first = 0;
	priv->lower.q_last = 0;
	ng_callout_init(&priv->lower.q_callout);
	mtx_init(&priv->lower.q_mtx, "ng_car_l", NULL, MTX_DEF);

	return (0);
}

/*
 * Add a hook.
 */
static int
ng_car_newhook(node_p node, hook_p hook, const char *name)
{
	const priv_p priv = NG_NODE_PRIVATE(node);

	if (strcmp(name, NG_CAR_HOOK_LOWER) == 0) {
		priv->lower.hook = hook;
		priv->upper.dest = hook;
		bzero(&priv->lower.stats, sizeof(priv->lower.stats));
		NG_HOOK_SET_PRIVATE(hook, &priv->lower);
	} else if (strcmp(name, NG_CAR_HOOK_UPPER) == 0) {
		priv->upper.hook = hook;
		priv->lower.dest = hook;
		bzero(&priv->upper.stats, sizeof(priv->upper.stats));
		NG_HOOK_SET_PRIVATE(hook, &priv->upper);
	} else
		return (EINVAL);
	return(0);
}

/*
 * Data has arrived.
 */
static int
ng_car_rcvdata(hook_p hook, item_p item )
{
	struct hookinfo *const hinfo = NG_HOOK_PRIVATE(hook);
	struct mbuf *m;
	int error = 0;
	u_int len;

	/* If queue is not empty now then enqueue packet. */
	if (hinfo->q_first != hinfo->q_last) {
		ng_car_enqueue(hinfo, item);
		return (0);
	}

	m = NGI_M(item);

#define NG_CAR_PERFORM_MATCH_ACTION(a)			\
	do {						\
		switch (a) {				\
		case NG_CAR_ACTION_FORWARD:		\
			/* Do nothing. */		\
			break;				\
		case NG_CAR_ACTION_MARK:		\
			/* XXX find a way to mark packets (mbuf tag?) */ \
			++hinfo->stats.errors;		\
			break;				\
		case NG_CAR_ACTION_DROP:		\
		default:				\
			/* Drop packet and return. */	\
			NG_FREE_ITEM(item);		\
			++hinfo->stats.droped_pkts;	\
			return (0);			\
		}					\
	} while (0)

	/* Packet is counted as 128 tokens for better resolution */
	if (hinfo->conf.opt & NG_CAR_COUNT_PACKETS) {
		len = 128;
	} else {
		len = m->m_pkthdr.len;
	}

	/* Check committed token bucket. */
	if (hinfo->tc - len >= 0) {
		/* This packet is green. */
		++hinfo->stats.green_pkts;
		hinfo->tc -= len;
		NG_CAR_PERFORM_MATCH_ACTION(hinfo->conf.green_action);
	} else {

		/* Refill only if not green without it. */
		ng_car_refillhook(hinfo);

		 /* Check committed token bucket again after refill. */
		if (hinfo->tc - len >= 0) {
			/* This packet is green */
			++hinfo->stats.green_pkts;
			hinfo->tc -= len;
			NG_CAR_PERFORM_MATCH_ACTION(hinfo->conf.green_action);

		/* If not green and mode is SHAPE, enqueue packet. */
		} else if (hinfo->conf.mode == NG_CAR_SHAPE) {
			ng_car_enqueue(hinfo, item);
			return (0);

		/* If not green and mode is RED, calculate probability. */
		} else if (hinfo->conf.mode == NG_CAR_RED) {
			/* Is packet is bigger then extended burst? */
			if (len - (hinfo->tc - len) > hinfo->conf.ebs) {
				/* This packet is definitely red. */
				++hinfo->stats.red_pkts;
				hinfo->te = 0;
				NG_CAR_PERFORM_MATCH_ACTION(hinfo->conf.red_action);

			/* Use token bucket to simulate RED-like drop
			   probability. */
			} else if (hinfo->te + (len - hinfo->tc) <
			    hinfo->conf.ebs) {
				/* This packet is yellow */
				++hinfo->stats.yellow_pkts;
				hinfo->te += len - hinfo->tc;
				/* Go to negative tokens. */
				hinfo->tc -= len;
				NG_CAR_PERFORM_MATCH_ACTION(hinfo->conf.yellow_action);
			} else {
				/* This packet is probably red. */
				++hinfo->stats.red_pkts;
				hinfo->te = 0;
				NG_CAR_PERFORM_MATCH_ACTION(hinfo->conf.red_action);
			}
		/* If not green and mode is SINGLE/DOUBLE RATE. */
		} else {
			/* Check extended token bucket. */
			if (hinfo->te - len >= 0) {
				/* This packet is yellow */
				++hinfo->stats.yellow_pkts;
				hinfo->te -= len;
				NG_CAR_PERFORM_MATCH_ACTION(hinfo->conf.yellow_action);
			} else {
				/* This packet is red */
				++hinfo->stats.red_pkts;
				NG_CAR_PERFORM_MATCH_ACTION(hinfo->conf.red_action);
			}
		}
	}

#undef NG_CAR_PERFORM_MATCH_ACTION

	NG_FWD_ITEM_HOOK(error, item, hinfo->dest);
	if (error != 0)
		++hinfo->stats.errors;
	++hinfo->stats.passed_pkts;

	return (error);
}

/*
 * Receive a control message.
 */
static int
ng_car_rcvmsg(node_p node, item_p item, hook_p lasthook)
{
	const priv_p priv = NG_NODE_PRIVATE(node);
	struct ng_mesg *resp = NULL;
	int error = 0;
	struct ng_mesg *msg;

	NGI_GET_MSG(item, msg);
	switch (msg->header.typecookie) {
	case NGM_CAR_COOKIE:
		switch (msg->header.cmd) {
		case NGM_CAR_GET_STATS:
		case NGM_CAR_GETCLR_STATS:
			{
				struct ng_car_bulkstats *bstats;

				NG_MKRESPONSE(resp, msg,
					sizeof(*bstats), M_NOWAIT);
				if (resp == NULL) {
					error = ENOMEM;
					break;
				}
				bstats = (struct ng_car_bulkstats *)resp->data;

				bcopy(&priv->upper.stats, &bstats->downstream,
				    sizeof(bstats->downstream));
				bcopy(&priv->lower.stats, &bstats->upstream,
				    sizeof(bstats->upstream));
			}
			if (msg->header.cmd == NGM_CAR_GET_STATS)
				break;
		case NGM_CAR_CLR_STATS:
			bzero(&priv->upper.stats,
				sizeof(priv->upper.stats));
			bzero(&priv->lower.stats,
				sizeof(priv->lower.stats));
			break;
		case NGM_CAR_GET_CONF:
			{
				struct ng_car_bulkconf *bconf;

				NG_MKRESPONSE(resp, msg,
					sizeof(*bconf), M_NOWAIT);
				if (resp == NULL) {
					error = ENOMEM;
					break;
				}
				bconf = (struct ng_car_bulkconf *)resp->data;

				bcopy(&priv->upper.conf, &bconf->downstream,
				    sizeof(bconf->downstream));
				bcopy(&priv->lower.conf, &bconf->upstream,
				    sizeof(bconf->upstream));
				/* Convert internal 1/(8*128) of pps into pps */
				if (bconf->downstream.opt & NG_CAR_COUNT_PACKETS) {
				    bconf->downstream.cir /= 1024;
				    bconf->downstream.pir /= 1024;
				    bconf->downstream.cbs /= 128;
				    bconf->downstream.ebs /= 128;
				}
				if (bconf->upstream.opt & NG_CAR_COUNT_PACKETS) {
				    bconf->upstream.cir /= 1024;
				    bconf->upstream.pir /= 1024;
				    bconf->upstream.cbs /= 128;
				    bconf->upstream.ebs /= 128;
				}
			}
			break;
		case NGM_CAR_SET_CONF:
			{
				struct ng_car_bulkconf *const bconf =
				(struct ng_car_bulkconf *)msg->data;

				/* Check for invalid or illegal config. */
				if (msg->header.arglen != sizeof(*bconf)) {
					error = EINVAL;
					break;
				}
				/* Convert pps into internal 1/(8*128) of pps */
				if (bconf->downstream.opt & NG_CAR_COUNT_PACKETS) {
				    bconf->downstream.cir *= 1024;
				    bconf->downstream.pir *= 1024;
				    bconf->downstream.cbs *= 125;
				    bconf->downstream.ebs *= 125;
				}
				if (bconf->upstream.opt & NG_CAR_COUNT_PACKETS) {
				    bconf->upstream.cir *= 1024;
				    bconf->upstream.pir *= 1024;
				    bconf->upstream.cbs *= 125;
				    bconf->upstream.ebs *= 125;
				}
				if ((bconf->downstream.cir > 1000000000) ||
				    (bconf->downstream.pir > 1000000000) ||
				    (bconf->upstream.cir > 1000000000) ||
				    (bconf->upstream.pir > 1000000000) ||
				    (bconf->downstream.cbs == 0 &&
					bconf->downstream.ebs == 0) ||
				    (bconf->upstream.cbs == 0 &&
					bconf->upstream.ebs == 0))
				{
					error = EINVAL;
					break;
				}
				if ((bconf->upstream.mode == NG_CAR_SHAPE) &&
				    (bconf->upstream.cir == 0)) {
					error = EINVAL;
					break;
				}
				if ((bconf->downstream.mode == NG_CAR_SHAPE) &&
				    (bconf->downstream.cir == 0)) {
					error = EINVAL;
					break;
				}

				/* Copy downstream config. */
				bcopy(&bconf->downstream, &priv->upper.conf,
				    sizeof(priv->upper.conf));
    				priv->upper.tc = priv->upper.conf.cbs;
				if (priv->upper.conf.mode == NG_CAR_RED ||
				    priv->upper.conf.mode == NG_CAR_SHAPE) {
					priv->upper.te = 0;
				} else {
					priv->upper.te = priv->upper.conf.ebs;
				}

				/* Copy upstream config. */
				bcopy(&bconf->upstream, &priv->lower.conf,
				    sizeof(priv->lower.conf));
    				priv->lower.tc = priv->lower.conf.cbs;
				if (priv->lower.conf.mode == NG_CAR_RED ||
				    priv->lower.conf.mode == NG_CAR_SHAPE) {
					priv->lower.te = 0;
				} else {
					priv->lower.te = priv->lower.conf.ebs;
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

/*
 * Do local shutdown processing.
 */
static int
ng_car_shutdown(node_p node)
{
	const priv_p priv = NG_NODE_PRIVATE(node);

	ng_uncallout(&priv->upper.q_callout, node);
	ng_uncallout(&priv->lower.q_callout, node);
	mtx_destroy(&priv->upper.q_mtx);
	mtx_destroy(&priv->lower.q_mtx);
	NG_NODE_UNREF(priv->node);
	free(priv, M_NETGRAPH);
	return (0);
}

/*
 * Hook disconnection.
 *
 * For this type, removal of the last link destroys the node.
 */
static int
ng_car_disconnect(hook_p hook)
{
	struct hookinfo *const hinfo = NG_HOOK_PRIVATE(hook);
	const node_p node = NG_HOOK_NODE(hook);
	const priv_p priv = NG_NODE_PRIVATE(node);

	if (hinfo) {
		/* Purge queue if not empty. */
		while (hinfo->q_first != hinfo->q_last) {
			NG_FREE_M(hinfo->q[hinfo->q_first]);
			hinfo->q_first++;
			if (hinfo->q_first >= NG_CAR_QUEUE_SIZE)
		    		hinfo->q_first = 0;
		}
		/* Remove hook refs. */
		if (hinfo->hook == priv->upper.hook)
			priv->lower.dest = NULL;
		else
			priv->upper.dest = NULL;
		hinfo->hook = NULL;
	}
	/* Already shutting down? */
	if ((NG_NODE_NUMHOOKS(NG_HOOK_NODE(hook)) == 0)
	    && (NG_NODE_IS_VALID(NG_HOOK_NODE(hook))))
		ng_rmnode_self(NG_HOOK_NODE(hook));
	return (0);
}

/*
 * Hook's token buckets refillment.
 */
static void
ng_car_refillhook(struct hookinfo *h)
{
	struct bintime newt, deltat;
	unsigned int deltat_us;

	/* Get current time. */
	getbinuptime(&newt);

	/* Get time delta since last refill. */
	deltat = newt;
	bintime_sub(&deltat, &h->lastRefill);

	/* Time must go forward. */
	if (deltat.sec < 0) {
	    h->lastRefill = newt;
	    return;
	}

	/* But not too far forward. */
	if (deltat.sec >= 1000) {
	    deltat_us = (1000 << 20);
	} else {
	    /* convert bintime to the 1/(2^20) of sec */
	    deltat_us = (deltat.sec << 20) + (deltat.frac >> 44);
	}

	if (h->conf.mode == NG_CAR_SINGLE_RATE) {
		int64_t	delta;
		/* Refill committed token bucket. */
		h->tc += (h->conf.cir * deltat_us) >> 23;
		delta = h->tc - h->conf.cbs;
		if (delta > 0) {
			h->tc = h->conf.cbs;

			/* Refill exceeded token bucket. */
			h->te += delta;
			if (h->te > ((int64_t)h->conf.ebs))
				h->te = h->conf.ebs;
		}

	} else if (h->conf.mode == NG_CAR_DOUBLE_RATE) {
		/* Refill committed token bucket. */
		h->tc += (h->conf.cir * deltat_us) >> 23;
		if (h->tc > ((int64_t)h->conf.cbs))
			h->tc = h->conf.cbs;

		/* Refill peak token bucket. */
		h->te += (h->conf.pir * deltat_us) >> 23;
		if (h->te > ((int64_t)h->conf.ebs))
			h->te = h->conf.ebs;

	} else { /* RED or SHAPE mode. */
		/* Refill committed token bucket. */
		h->tc += (h->conf.cir * deltat_us) >> 23;
		if (h->tc > ((int64_t)h->conf.cbs))
			h->tc = h->conf.cbs;
	}

	/* Remember this moment. */
	h->lastRefill = newt;
}

/*
 * Schedule callout when we will have required tokens.
 */
static void
ng_car_schedule(struct hookinfo *hinfo)
{
	int 	delay;

	delay = (-(hinfo->tc)) * hz * 8 / hinfo->conf.cir + 1;

	ng_callout(&hinfo->q_callout, NG_HOOK_NODE(hinfo->hook), hinfo->hook,
	    delay, &ng_car_q_event, NULL, 0);
}

/*
 * Queue processing callout handler.
 */
void
ng_car_q_event(node_p node, hook_p hook, void *arg, int arg2)
{
	struct hookinfo	*hinfo = NG_HOOK_PRIVATE(hook);
	struct mbuf 	*m;
	int		error;

	/* Refill tokens for time we have slept. */
	ng_car_refillhook(hinfo);

	/* If we have some tokens */
	while (hinfo->tc >= 0) {

		/* Send packet. */
		m = hinfo->q[hinfo->q_first];
		NG_SEND_DATA_ONLY(error, hinfo->dest, m);
		if (error != 0)
			++hinfo->stats.errors;
		++hinfo->stats.passed_pkts;

		/* Get next one. */
		hinfo->q_first++;
		if (hinfo->q_first >= NG_CAR_QUEUE_SIZE)
			hinfo->q_first = 0;

		/* Stop if none left. */
		if (hinfo->q_first == hinfo->q_last)
			break;

		/* If we have more packet, try it. */
		m = hinfo->q[hinfo->q_first];
		if (hinfo->conf.opt & NG_CAR_COUNT_PACKETS) {
			hinfo->tc -= 128;
		} else {
			hinfo->tc -= m->m_pkthdr.len;
		}
	}

	/* If something left */
	if (hinfo->q_first != hinfo->q_last)
		/* Schedule queue processing. */
		ng_car_schedule(hinfo);
}

/*
 * Enqueue packet.
 */
static void
ng_car_enqueue(struct hookinfo *hinfo, item_p item)
{
	struct mbuf 	*m;
	int		len;

	NGI_GET_M(item, m);
	NG_FREE_ITEM(item);

	/* Lock queue mutex. */
	mtx_lock(&hinfo->q_mtx);

	/* Calculate used queue length. */
	len = hinfo->q_last - hinfo->q_first;
	if (len < 0)
		len += NG_CAR_QUEUE_SIZE;

	/* If queue is overflowed or we have no RED tokens. */
	if ((len >= (NG_CAR_QUEUE_SIZE - 1)) ||
	    (hinfo->te + len >= NG_CAR_QUEUE_SIZE)) {
		/* Drop packet. */
		++hinfo->stats.red_pkts;
		++hinfo->stats.droped_pkts;
		NG_FREE_M(m);

		hinfo->te = 0;
	} else {
		/* This packet is yellow. */
		++hinfo->stats.yellow_pkts;

		/* Enqueue packet. */
		hinfo->q[hinfo->q_last] = m;
		hinfo->q_last++;
		if (hinfo->q_last >= NG_CAR_QUEUE_SIZE)
			hinfo->q_last = 0;

		/* Use RED tokens. */
		if (len > NG_CAR_QUEUE_MIN_TH)
			hinfo->te += len - NG_CAR_QUEUE_MIN_TH;

		/* If this is a first packet in the queue. */
		if (len == 0) {
			if (hinfo->conf.opt & NG_CAR_COUNT_PACKETS) {
				hinfo->tc -= 128;
			} else {
				hinfo->tc -= m->m_pkthdr.len;
			}

			/* Schedule queue processing. */
			ng_car_schedule(hinfo);
		}
	}

	/* Unlock queue mutex. */
	mtx_unlock(&hinfo->q_mtx);
}
