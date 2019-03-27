/*-
 * Copyright (c) 2001-2002 Packet Design, LLC.
 * All rights reserved.
 * 
 * Subject to the following obligations and disclaimer of warranty,
 * use and redistribution of this software, in source or object code
 * forms, with or without modifications are expressly permitted by
 * Packet Design; provided, however, that:
 * 
 *    (i)  Any and all reproductions of the source or object code
 *         must include the copyright notice above and the following
 *         disclaimer of warranties; and
 *    (ii) No rights are granted, in any manner or form, to use
 *         Packet Design trademarks, including the mark "PACKET DESIGN"
 *         on advertising, endorsements, or otherwise except as such
 *         appears in the above copyright notice or in the software.
 * 
 * THIS SOFTWARE IS BEING PROVIDED BY PACKET DESIGN "AS IS", AND
 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, PACKET DESIGN MAKES NO
 * REPRESENTATIONS OR WARRANTIES, EXPRESS OR IMPLIED, REGARDING
 * THIS SOFTWARE, INCLUDING WITHOUT LIMITATION, ANY AND ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE,
 * OR NON-INFRINGEMENT.  PACKET DESIGN DOES NOT WARRANT, GUARANTEE,
 * OR MAKE ANY REPRESENTATIONS REGARDING THE USE OF, OR THE RESULTS
 * OF THE USE OF THIS SOFTWARE IN TERMS OF ITS CORRECTNESS, ACCURACY,
 * RELIABILITY OR OTHERWISE.  IN NO EVENT SHALL PACKET DESIGN BE
 * LIABLE FOR ANY DAMAGES RESULTING FROM OR ARISING OUT OF ANY USE
 * OF THIS SOFTWARE, INCLUDING WITHOUT LIMITATION, ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, PUNITIVE, OR CONSEQUENTIAL
 * DAMAGES, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES, LOSS OF
 * USE, DATA OR PROFITS, HOWEVER CAUSED AND UNDER ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF PACKET DESIGN IS ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 * 
 * Author: Archie Cobbs <archie@freebsd.org>
 *
 * $FreeBSD$
 */

/*
 * L2TP netgraph node type.
 *
 * This node type implements the lower layer of the
 * L2TP protocol as specified in RFC 2661.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/time.h>
#include <sys/conf.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/errno.h>
#include <sys/libkern.h>

#include <netgraph/ng_message.h>
#include <netgraph/netgraph.h>
#include <netgraph/ng_parse.h>
#include <netgraph/ng_l2tp.h>

#ifdef NG_SEPARATE_MALLOC
static MALLOC_DEFINE(M_NETGRAPH_L2TP, "netgraph_l2tp", "netgraph l2tp node");
#else
#define M_NETGRAPH_L2TP M_NETGRAPH
#endif

/* L2TP header format (first 2 bytes only) */
#define L2TP_HDR_CTRL		0x8000			/* control packet */
#define L2TP_HDR_LEN		0x4000			/* has length field */
#define L2TP_HDR_SEQ		0x0800			/* has ns, nr fields */
#define L2TP_HDR_OFF		0x0200			/* has offset field */
#define L2TP_HDR_PRIO		0x0100			/* give priority */
#define L2TP_HDR_VERS_MASK	0x000f			/* version field mask */
#define L2TP_HDR_VERSION	0x0002			/* version field */

/* Bits that must be zero or one in first two bytes of header */
#define L2TP_CTRL_0BITS		0x030d			/* ctrl: must be 0 */
#define L2TP_CTRL_1BITS		0xc802			/* ctrl: must be 1 */
#define L2TP_DATA_0BITS		0x800d			/* data: must be 0 */
#define L2TP_DATA_1BITS		0x0002			/* data: must be 1 */

/* Standard xmit ctrl and data header bits */
#define L2TP_CTRL_HDR		(L2TP_HDR_CTRL | L2TP_HDR_LEN \
				    | L2TP_HDR_SEQ | L2TP_HDR_VERSION)
#define L2TP_DATA_HDR		(L2TP_HDR_VERSION)	/* optional: len, seq */

/* Some hard coded values */
#define L2TP_MAX_XWIN		128			/* my max xmit window */
#define L2TP_MAX_REXMIT		5			/* default max rexmit */
#define L2TP_MAX_REXMIT_TO	30			/* default rexmit to */
#define L2TP_DELAYED_ACK	((hz + 19) / 20)	/* delayed ack: 50 ms */

/* Default data sequence number configuration for new sessions */
#define L2TP_CONTROL_DSEQ	1			/* we are the lns */
#define L2TP_ENABLE_DSEQ	1			/* enable data seq # */

/* Compare sequence numbers using circular math */
#define L2TP_SEQ_DIFF(x, y)	((int16_t)((x) - (y)))

#define SESSHASHSIZE		0x0020
#define SESSHASH(x)		(((x) ^ ((x) >> 8)) & (SESSHASHSIZE - 1))

/* Hook private data (data session hooks only) */
struct ng_l2tp_hook_private {
	struct ng_l2tp_sess_config	conf;	/* hook/session config */
	struct ng_l2tp_session_stats	stats;	/* per sessions statistics */
	hook_p				hook;	/* hook reference */
	u_int16_t			ns;	/* data ns sequence number */
	u_int16_t			nr;	/* data nr sequence number */
	LIST_ENTRY(ng_l2tp_hook_private) sessions;
};
typedef struct ng_l2tp_hook_private *hookpriv_p;

/*
 * Sequence number state
 *
 * Invariants:
 *    - If cwnd < ssth, we're doing slow start, otherwise congestion avoidance
 *    - The number of unacknowledged xmit packets is (ns - rack) <= seq->wmax
 *    - The first (ns - rack) mbuf's in xwin[] array are copies of these
 *	unacknowledged packets; the remainder of xwin[] consists first of
 *	zero or more further untransmitted packets in the transmit queue
 *    - We try to keep the peer's receive window as full as possible.
 *	Therefore, (i < cwnd && xwin[i] != NULL) implies (ns - rack) > i.
 *    - rack_timer is running iff (ns - rack) > 0 (unack'd xmit'd pkts)
 *    - If xack != nr, there are unacknowledged recv packet(s) (delayed ack)
 *    - xack_timer is running iff xack != nr (unack'd rec'd pkts)
 */
struct l2tp_seq {
	u_int16_t		ns;		/* next xmit seq we send */
	u_int16_t		nr;		/* next recv seq we expect */
	u_int16_t		inproc;		/* packet is in processing */
	u_int16_t		rack;		/* last 'nr' we rec'd */
	u_int16_t		xack;		/* last 'nr' we sent */
	u_int16_t		wmax;		/* peer's max recv window */
	u_int16_t		cwnd;		/* current congestion window */
	u_int16_t		ssth;		/* slow start threshold */
	u_int16_t		acks;		/* # consecutive acks rec'd */
	u_int16_t		rexmits;	/* # retransmits sent */
	struct callout		rack_timer;	/* retransmit timer */
	struct callout		xack_timer;	/* delayed ack timer */
	struct mbuf		*xwin[L2TP_MAX_XWIN];	/* transmit window */
	struct mtx		mtx;			/* seq mutex */
};

/* Node private data */
struct ng_l2tp_private {
	node_p			node;		/* back pointer to node */
	hook_p			ctrl;		/* hook to upper layers */
	hook_p			lower;		/* hook to lower layers */
	struct ng_l2tp_config	conf;		/* node configuration */
	struct ng_l2tp_stats	stats;		/* node statistics */
	struct l2tp_seq		seq;		/* ctrl sequence number state */
	ng_ID_t			ftarget;	/* failure message target */
	LIST_HEAD(, ng_l2tp_hook_private) sesshash[SESSHASHSIZE];
};
typedef struct ng_l2tp_private *priv_p;

/* Netgraph node methods */
static ng_constructor_t	ng_l2tp_constructor;
static ng_rcvmsg_t	ng_l2tp_rcvmsg;
static ng_shutdown_t	ng_l2tp_shutdown;
static ng_newhook_t	ng_l2tp_newhook;
static ng_rcvdata_t	ng_l2tp_rcvdata;
static ng_rcvdata_t	ng_l2tp_rcvdata_lower;
static ng_rcvdata_t	ng_l2tp_rcvdata_ctrl;
static ng_disconnect_t	ng_l2tp_disconnect;

/* Internal functions */
static int	ng_l2tp_xmit_ctrl(priv_p priv, struct mbuf *m, u_int16_t ns);

static void	ng_l2tp_seq_init(priv_p priv);
static int	ng_l2tp_seq_set(priv_p priv,
			const struct ng_l2tp_seq_config *conf);
static int	ng_l2tp_seq_adjust(priv_p priv,
			const struct ng_l2tp_config *conf);
static void	ng_l2tp_seq_reset(priv_p priv);
static void	ng_l2tp_seq_failure(priv_p priv);
static void	ng_l2tp_seq_recv_nr(priv_p priv, u_int16_t nr);
static void	ng_l2tp_seq_xack_timeout(node_p node, hook_p hook,
		    void *arg1, int arg2);
static void	ng_l2tp_seq_rack_timeout(node_p node, hook_p hook,
		    void *arg1, int arg2);

static hookpriv_p	ng_l2tp_find_session(priv_p privp, u_int16_t sid);
static ng_fn_eachhook	ng_l2tp_reset_session;

#ifdef INVARIANTS
static void	ng_l2tp_seq_check(struct l2tp_seq *seq);
#endif

/* Parse type for struct ng_l2tp_seq_config. */
static const struct ng_parse_struct_field
	ng_l2tp_seq_config_fields[] = NG_L2TP_SEQ_CONFIG_TYPE_INFO;
static const struct ng_parse_type ng_l2tp_seq_config_type = {
	&ng_parse_struct_type,
	&ng_l2tp_seq_config_fields
};

/* Parse type for struct ng_l2tp_config */
static const struct ng_parse_struct_field
	ng_l2tp_config_type_fields[] = NG_L2TP_CONFIG_TYPE_INFO;
static const struct ng_parse_type ng_l2tp_config_type = {
	&ng_parse_struct_type,
	&ng_l2tp_config_type_fields,
};

/* Parse type for struct ng_l2tp_sess_config */
static const struct ng_parse_struct_field
	ng_l2tp_sess_config_type_fields[] = NG_L2TP_SESS_CONFIG_TYPE_INFO;
static const struct ng_parse_type ng_l2tp_sess_config_type = {
	&ng_parse_struct_type,
	&ng_l2tp_sess_config_type_fields,
};

/* Parse type for struct ng_l2tp_stats */
static const struct ng_parse_struct_field
	ng_l2tp_stats_type_fields[] = NG_L2TP_STATS_TYPE_INFO;
static const struct ng_parse_type ng_l2tp_stats_type = {
	&ng_parse_struct_type,
	&ng_l2tp_stats_type_fields
};

/* Parse type for struct ng_l2tp_session_stats. */
static const struct ng_parse_struct_field
	ng_l2tp_session_stats_type_fields[] = NG_L2TP_SESSION_STATS_TYPE_INFO;
static const struct ng_parse_type ng_l2tp_session_stats_type = {
	&ng_parse_struct_type,
	&ng_l2tp_session_stats_type_fields
};

/* List of commands and how to convert arguments to/from ASCII */
static const struct ng_cmdlist ng_l2tp_cmdlist[] = {
	{
	  NGM_L2TP_COOKIE,
	  NGM_L2TP_SET_CONFIG,
	  "setconfig",
	  &ng_l2tp_config_type,
	  NULL
	},
	{
	  NGM_L2TP_COOKIE,
	  NGM_L2TP_GET_CONFIG,
	  "getconfig",
	  NULL,
	  &ng_l2tp_config_type
	},
	{
	  NGM_L2TP_COOKIE,
	  NGM_L2TP_SET_SESS_CONFIG,
	  "setsessconfig",
	  &ng_l2tp_sess_config_type,
	  NULL
	},
	{
	  NGM_L2TP_COOKIE,
	  NGM_L2TP_GET_SESS_CONFIG,
	  "getsessconfig",
	  &ng_parse_hint16_type,
	  &ng_l2tp_sess_config_type
	},
	{
	  NGM_L2TP_COOKIE,
	  NGM_L2TP_GET_STATS,
	  "getstats",
	  NULL,
	  &ng_l2tp_stats_type
	},
	{
	  NGM_L2TP_COOKIE,
	  NGM_L2TP_CLR_STATS,
	  "clrstats",
	  NULL,
	  NULL
	},
	{
	  NGM_L2TP_COOKIE,
	  NGM_L2TP_GETCLR_STATS,
	  "getclrstats",
	  NULL,
	  &ng_l2tp_stats_type
	},
	{
	  NGM_L2TP_COOKIE,
	  NGM_L2TP_GET_SESSION_STATS,
	  "getsessstats",
	  &ng_parse_int16_type,
	  &ng_l2tp_session_stats_type
	},
	{
	  NGM_L2TP_COOKIE,
	  NGM_L2TP_CLR_SESSION_STATS,
	  "clrsessstats",
	  &ng_parse_int16_type,
	  NULL
	},
	{
	  NGM_L2TP_COOKIE,
	  NGM_L2TP_GETCLR_SESSION_STATS,
	  "getclrsessstats",
	  &ng_parse_int16_type,
	  &ng_l2tp_session_stats_type
	},
	{
	  NGM_L2TP_COOKIE,
	  NGM_L2TP_ACK_FAILURE,
	  "ackfailure",
	  NULL,
	  NULL
	},
	{
	  NGM_L2TP_COOKIE,
	  NGM_L2TP_SET_SEQ,
	  "setsequence",
	  &ng_l2tp_seq_config_type,
	  NULL
	},
	{ 0 }
};

/* Node type descriptor */
static struct ng_type ng_l2tp_typestruct = {
	.version =	NG_ABI_VERSION,
	.name =		NG_L2TP_NODE_TYPE,
	.constructor =	ng_l2tp_constructor,
	.rcvmsg =	ng_l2tp_rcvmsg,
	.shutdown =	ng_l2tp_shutdown,
	.newhook =	ng_l2tp_newhook,
	.rcvdata =	ng_l2tp_rcvdata,
	.disconnect =	ng_l2tp_disconnect,
	.cmdlist =	ng_l2tp_cmdlist,
};
NETGRAPH_INIT(l2tp, &ng_l2tp_typestruct);

/* Sequence number state sanity checking */
#ifdef INVARIANTS
#define L2TP_SEQ_CHECK(seq)	ng_l2tp_seq_check(seq)
#else
#define L2TP_SEQ_CHECK(x)	do { } while (0)
#endif

/* Whether to use m_copypacket() or m_dup() */
#define L2TP_COPY_MBUF		m_copypacket

#define ERROUT(x)	do { error = (x); goto done; } while (0)

/************************************************************************
			NETGRAPH NODE STUFF
************************************************************************/

/*
 * Node type constructor
 */
static int
ng_l2tp_constructor(node_p node)
{
	priv_p priv;
	int	i;

	/* Allocate private structure */
	priv = malloc(sizeof(*priv), M_NETGRAPH_L2TP, M_WAITOK | M_ZERO);
	NG_NODE_SET_PRIVATE(node, priv);
	priv->node = node;

	/* Apply a semi-reasonable default configuration */
	priv->conf.peer_win = 1;
	priv->conf.rexmit_max = L2TP_MAX_REXMIT;
	priv->conf.rexmit_max_to = L2TP_MAX_REXMIT_TO;

	/* Initialize sequence number state */
	ng_l2tp_seq_init(priv);

	for (i = 0; i < SESSHASHSIZE; i++)
	    LIST_INIT(&priv->sesshash[i]);

	/* Done */
	return (0);
}

/*
 * Give our OK for a hook to be added.
 */
static int
ng_l2tp_newhook(node_p node, hook_p hook, const char *name)
{
	const priv_p priv = NG_NODE_PRIVATE(node);

	/* Check hook name */
	if (strcmp(name, NG_L2TP_HOOK_CTRL) == 0) {
		if (priv->ctrl != NULL)
			return (EISCONN);
		priv->ctrl = hook;
		NG_HOOK_SET_RCVDATA(hook, ng_l2tp_rcvdata_ctrl);
	} else if (strcmp(name, NG_L2TP_HOOK_LOWER) == 0) {
		if (priv->lower != NULL)
			return (EISCONN);
		priv->lower = hook;
		NG_HOOK_SET_RCVDATA(hook, ng_l2tp_rcvdata_lower);
	} else {
		static const char hexdig[16] = "0123456789abcdef";
		u_int16_t session_id;
		hookpriv_p hpriv;
		uint16_t hash;
		const char *hex;
		int i;
		int j;

		/* Parse hook name to get session ID */
		if (strncmp(name, NG_L2TP_HOOK_SESSION_P,
		    sizeof(NG_L2TP_HOOK_SESSION_P) - 1) != 0)
			return (EINVAL);
		hex = name + sizeof(NG_L2TP_HOOK_SESSION_P) - 1;
		for (session_id = i = 0; i < 4; i++) {
			for (j = 0; j < 16 && hex[i] != hexdig[j]; j++);
			if (j == 16)
				return (EINVAL);
			session_id = (session_id << 4) | j;
		}
		if (hex[i] != '\0')
			return (EINVAL);

		/* Create hook private structure */
		hpriv = malloc(sizeof(*hpriv),
		    M_NETGRAPH_L2TP, M_NOWAIT | M_ZERO);
		if (hpriv == NULL)
			return (ENOMEM);
		hpriv->conf.session_id = session_id;
		hpriv->conf.control_dseq = L2TP_CONTROL_DSEQ;
		hpriv->conf.enable_dseq = L2TP_ENABLE_DSEQ;
		hpriv->hook = hook;
		NG_HOOK_SET_PRIVATE(hook, hpriv);
		hash = SESSHASH(hpriv->conf.session_id);
		LIST_INSERT_HEAD(&priv->sesshash[hash], hpriv, sessions);
	}

	/* Done */
	return (0);
}

/*
 * Receive a control message.
 */
static int
ng_l2tp_rcvmsg(node_p node, item_p item, hook_p lasthook)
{
	const priv_p priv = NG_NODE_PRIVATE(node);
	struct ng_mesg *resp = NULL;
	struct ng_mesg *msg;
	int error = 0;

	NGI_GET_MSG(item, msg);
	switch (msg->header.typecookie) {
	case NGM_L2TP_COOKIE:
		switch (msg->header.cmd) {
		case NGM_L2TP_SET_CONFIG:
		    {
			struct ng_l2tp_config *const conf =
				(struct ng_l2tp_config *)msg->data;

			/* Check for invalid or illegal config */
			if (msg->header.arglen != sizeof(*conf)) {
				error = EINVAL;
				break;
			}
			conf->enabled = !!conf->enabled;
			conf->match_id = !!conf->match_id;
			if (priv->conf.enabled
			    && ((priv->conf.tunnel_id != 0
			       && conf->tunnel_id != priv->conf.tunnel_id)
			      || ((priv->conf.peer_id != 0
			       && conf->peer_id != priv->conf.peer_id)))) {
				error = EBUSY;
				break;
			}

			/* Save calling node as failure target */
			priv->ftarget = NGI_RETADDR(item);

			/* Adjust sequence number state */
			if ((error = ng_l2tp_seq_adjust(priv, conf)) != 0)
				break;

			/* Update node's config */
			priv->conf = *conf;
			break;
		    }
		case NGM_L2TP_GET_CONFIG:
		    {
			struct ng_l2tp_config *conf;

			NG_MKRESPONSE(resp, msg, sizeof(*conf), M_NOWAIT);
			if (resp == NULL) {
				error = ENOMEM;
				break;
			}
			conf = (struct ng_l2tp_config *)resp->data;
			*conf = priv->conf;
			break;
		    }
		case NGM_L2TP_SET_SESS_CONFIG:
		    {
			struct ng_l2tp_sess_config *const conf =
			    (struct ng_l2tp_sess_config *)msg->data;
			hookpriv_p hpriv;

			/* Check for invalid or illegal config. */
			if (msg->header.arglen != sizeof(*conf)) {
				error = EINVAL;
				break;
			}

			/* Find matching hook */
			hpriv = ng_l2tp_find_session(priv, conf->session_id);
			if (hpriv == NULL) {
				error = ENOENT;
				break;
			}

			/* Update hook's config */
			hpriv->conf = *conf;
			break;
		    }
		case NGM_L2TP_GET_SESS_CONFIG:
		    {
			struct ng_l2tp_sess_config *conf;
			u_int16_t session_id;
			hookpriv_p hpriv;

			/* Get session ID */
			if (msg->header.arglen != sizeof(session_id)) {
				error = EINVAL;
				break;
			}
			memcpy(&session_id, msg->data, 2);

			/* Find matching hook */
			hpriv = ng_l2tp_find_session(priv, session_id);
			if (hpriv == NULL) {
				error = ENOENT;
				break;
			}

			/* Send response */
			NG_MKRESPONSE(resp, msg, sizeof(hpriv->conf), M_NOWAIT);
			if (resp == NULL) {
				error = ENOMEM;
				break;
			}
			conf = (struct ng_l2tp_sess_config *)resp->data;
			*conf = hpriv->conf;
			break;
		    }
		case NGM_L2TP_GET_STATS:
		case NGM_L2TP_CLR_STATS:
		case NGM_L2TP_GETCLR_STATS:
		    {
			if (msg->header.cmd != NGM_L2TP_CLR_STATS) {
				NG_MKRESPONSE(resp, msg,
				    sizeof(priv->stats), M_NOWAIT);
				if (resp == NULL) {
					error = ENOMEM;
					break;
				}
				memcpy(resp->data,
				    &priv->stats, sizeof(priv->stats));
			}
			if (msg->header.cmd != NGM_L2TP_GET_STATS)
				memset(&priv->stats, 0, sizeof(priv->stats));
			break;
		    }
		case NGM_L2TP_GET_SESSION_STATS:
		case NGM_L2TP_CLR_SESSION_STATS:
		case NGM_L2TP_GETCLR_SESSION_STATS:
		    {
			uint16_t session_id;
			hookpriv_p hpriv;

			/* Get session ID. */
			if (msg->header.arglen != sizeof(session_id)) {
				error = EINVAL;
				break;
			}
			bcopy(msg->data, &session_id, sizeof(uint16_t));

			/* Find matching hook. */
			hpriv = ng_l2tp_find_session(priv, session_id);
			if (hpriv == NULL) {
				error = ENOENT;
				break;
			}

			if (msg->header.cmd != NGM_L2TP_CLR_SESSION_STATS) {
				NG_MKRESPONSE(resp, msg,
				    sizeof(hpriv->stats), M_NOWAIT);
				if (resp == NULL) {
					error = ENOMEM;
					break;
				}
				bcopy(&hpriv->stats, resp->data,
					sizeof(hpriv->stats));
			}
			if (msg->header.cmd != NGM_L2TP_GET_SESSION_STATS)
				bzero(&hpriv->stats, sizeof(hpriv->stats));
			break;
		    }
		case NGM_L2TP_SET_SEQ:
		    {
			struct ng_l2tp_seq_config *const conf =
				(struct ng_l2tp_seq_config *)msg->data;

			/* Check for invalid or illegal seq config. */
			if (msg->header.arglen != sizeof(*conf)) {
				error = EINVAL;
				break;
			}
			conf->ns = htons(conf->ns);
			conf->nr = htons(conf->nr);
			conf->rack = htons(conf->rack);
			conf->xack = htons(conf->xack);

			/* Set sequence numbers. */
			error = ng_l2tp_seq_set(priv, conf);
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

	/* Done */
	NG_RESPOND_MSG(error, node, item, resp);
	NG_FREE_MSG(msg);
	return (error);
}

/*
 * Destroy node
 */
static int
ng_l2tp_shutdown(node_p node)
{
	const priv_p priv = NG_NODE_PRIVATE(node);
	struct l2tp_seq *const seq = &priv->seq;

	/* Sanity check */
	L2TP_SEQ_CHECK(seq);

	/* Reset sequence number state */
	ng_l2tp_seq_reset(priv);

	/* Free private data if neither timer is running */
	ng_uncallout(&seq->rack_timer, node);
	ng_uncallout(&seq->xack_timer, node);

	mtx_destroy(&seq->mtx);

	free(priv, M_NETGRAPH_L2TP);

	/* Unref node */
	NG_NODE_UNREF(node);
	return (0);
}

/*
 * Hook disconnection
 */
static int
ng_l2tp_disconnect(hook_p hook)
{
	const node_p node = NG_HOOK_NODE(hook);
	const priv_p priv = NG_NODE_PRIVATE(node);

	/* Zero out hook pointer */
	if (hook == priv->ctrl)
		priv->ctrl = NULL;
	else if (hook == priv->lower)
		priv->lower = NULL;
	else {
		const hookpriv_p hpriv = NG_HOOK_PRIVATE(hook);
		LIST_REMOVE(hpriv, sessions);
		free(hpriv, M_NETGRAPH_L2TP);
		NG_HOOK_SET_PRIVATE(hook, NULL);
	}

	/* Go away if no longer connected to anything */
	if (NG_NODE_NUMHOOKS(node) == 0 && NG_NODE_IS_VALID(node))
		ng_rmnode_self(node);
	return (0);
}

/*************************************************************************
			INTERNAL FUNCTIONS
*************************************************************************/

/*
 * Find the hook with a given session ID.
 */
static hookpriv_p
ng_l2tp_find_session(priv_p privp, u_int16_t sid)
{
	uint16_t	hash = SESSHASH(sid);
	hookpriv_p	hpriv = NULL;

	LIST_FOREACH(hpriv, &privp->sesshash[hash], sessions) {
		if (hpriv->conf.session_id == sid)
			break;
	}

	return (hpriv);
}

/*
 * Reset a hook's session state.
 */
static int
ng_l2tp_reset_session(hook_p hook, void *arg)
{
	const hookpriv_p hpriv = NG_HOOK_PRIVATE(hook);

	if (hpriv != NULL) {
		hpriv->conf.control_dseq = 0;
		hpriv->conf.enable_dseq = 0;
		bzero(&hpriv->stats, sizeof(struct ng_l2tp_session_stats));
		hpriv->nr = 0;
		hpriv->ns = 0;
	}
	return (-1);
}

/*
 * Handle an incoming frame from below.
 */
static int
ng_l2tp_rcvdata_lower(hook_p h, item_p item)
{
	static const u_int16_t req_bits[2][2] = {
		{ L2TP_DATA_0BITS, L2TP_DATA_1BITS },
		{ L2TP_CTRL_0BITS, L2TP_CTRL_1BITS },
	};
	const node_p node = NG_HOOK_NODE(h);
	const priv_p priv = NG_NODE_PRIVATE(node);
	hookpriv_p hpriv = NULL;
	hook_p hook = NULL;
	struct mbuf *m;
	u_int16_t tid, sid;
	u_int16_t hdr;
	u_int16_t ns, nr;
	int is_ctrl;
	int error;
	int len, plen;

	/* Sanity check */
	L2TP_SEQ_CHECK(&priv->seq);

	/* If not configured, reject */
	if (!priv->conf.enabled) {
		NG_FREE_ITEM(item);
		ERROUT(ENXIO);
	}

	/* Grab mbuf */
	NGI_GET_M(item, m);

	/* Remember full packet length; needed for per session accounting. */
	plen = m->m_pkthdr.len;

	/* Update stats */
	priv->stats.recvPackets++;
	priv->stats.recvOctets += plen;

	/* Get initial header */
	if (m->m_pkthdr.len < 6) {
		priv->stats.recvRunts++;
		NG_FREE_ITEM(item);
		NG_FREE_M(m);
		ERROUT(EINVAL);
	}
	if (m->m_len < 2 && (m = m_pullup(m, 2)) == NULL) {
		priv->stats.memoryFailures++;
		NG_FREE_ITEM(item);
		ERROUT(EINVAL);
	}
	hdr = (mtod(m, uint8_t *)[0] << 8) + mtod(m, uint8_t *)[1];
	m_adj(m, 2);

	/* Check required header bits and minimum length */
	is_ctrl = (hdr & L2TP_HDR_CTRL) != 0;
	if ((hdr & req_bits[is_ctrl][0]) != 0
	    || (~hdr & req_bits[is_ctrl][1]) != 0) {
		priv->stats.recvInvalid++;
		NG_FREE_ITEM(item);
		NG_FREE_M(m);
		ERROUT(EINVAL);
	}
	if (m->m_pkthdr.len < 4				/* tunnel, session id */
	    + (2 * ((hdr & L2TP_HDR_LEN) != 0))		/* length field */
	    + (4 * ((hdr & L2TP_HDR_SEQ) != 0))		/* seq # fields */
	    + (2 * ((hdr & L2TP_HDR_OFF) != 0))) {	/* offset field */
		priv->stats.recvRunts++;
		NG_FREE_ITEM(item);
		NG_FREE_M(m);
		ERROUT(EINVAL);
	}

	/* Get and validate length field if present */
	if ((hdr & L2TP_HDR_LEN) != 0) {
		if (m->m_len < 2 && (m = m_pullup(m, 2)) == NULL) {
			priv->stats.memoryFailures++;
			NG_FREE_ITEM(item);
			ERROUT(EINVAL);
		}
		len = (mtod(m, uint8_t *)[0] << 8) + mtod(m, uint8_t *)[1] - 4;
		m_adj(m, 2);
		if (len < 0 || len > m->m_pkthdr.len) {
			priv->stats.recvInvalid++;
			NG_FREE_ITEM(item);
			NG_FREE_M(m);
			ERROUT(EINVAL);
		}
		if (len < m->m_pkthdr.len)		/* trim extra bytes */
			m_adj(m, -(m->m_pkthdr.len - len));
	}

	/* Get tunnel ID and session ID */
	if (m->m_len < 4 && (m = m_pullup(m, 4)) == NULL) {
		priv->stats.memoryFailures++;
		NG_FREE_ITEM(item);
		ERROUT(EINVAL);
	}
	tid = (mtod(m, u_int8_t *)[0] << 8) + mtod(m, u_int8_t *)[1];
	sid = (mtod(m, u_int8_t *)[2] << 8) + mtod(m, u_int8_t *)[3];
	m_adj(m, 4);

	/* Check tunnel ID */
	if (tid != priv->conf.tunnel_id &&
	    (priv->conf.match_id || tid != 0)) {
		priv->stats.recvWrongTunnel++;
		NG_FREE_ITEM(item);
		NG_FREE_M(m);
		ERROUT(EADDRNOTAVAIL);
	}

	/* Check session ID (for data packets only) */
	if ((hdr & L2TP_HDR_CTRL) == 0) {
		hpriv = ng_l2tp_find_session(priv, sid);
		if (hpriv == NULL) {
			priv->stats.recvUnknownSID++;
			NG_FREE_ITEM(item);
			NG_FREE_M(m);
			ERROUT(ENOTCONN);
		}
		hook = hpriv->hook;
	}

	/* Get Ns, Nr fields if present */
	if ((hdr & L2TP_HDR_SEQ) != 0) {
		if (m->m_len < 4 && (m = m_pullup(m, 4)) == NULL) {
			priv->stats.memoryFailures++;
			NG_FREE_ITEM(item);
			ERROUT(EINVAL);
		}
		ns = (mtod(m, u_int8_t *)[0] << 8) + mtod(m, u_int8_t *)[1];
		nr = (mtod(m, u_int8_t *)[2] << 8) + mtod(m, u_int8_t *)[3];
		m_adj(m, 4);
	} else
		ns = nr = 0;

	/* Strip offset padding if present */
	if ((hdr & L2TP_HDR_OFF) != 0) {
		u_int16_t offset;

		/* Get length of offset padding */
		if (m->m_len < 2 && (m = m_pullup(m, 2)) == NULL) {
			priv->stats.memoryFailures++;
			NG_FREE_ITEM(item);
			ERROUT(EINVAL);
		}
		offset = (mtod(m, u_int8_t *)[0] << 8) + mtod(m, u_int8_t *)[1];

		/* Trim offset padding */
		if ((2+offset) > m->m_pkthdr.len) {
			priv->stats.recvInvalid++;
			NG_FREE_ITEM(item);
			NG_FREE_M(m);
			ERROUT(EINVAL);
		}
		m_adj(m, 2+offset);
	}

	/* Handle control packets */
	if ((hdr & L2TP_HDR_CTRL) != 0) {
		struct l2tp_seq *const seq = &priv->seq;

		/* Handle receive ack sequence number Nr */
		ng_l2tp_seq_recv_nr(priv, nr);

		/* Discard ZLB packets */
		if (m->m_pkthdr.len == 0) {
			priv->stats.recvZLBs++;
			NG_FREE_ITEM(item);
			NG_FREE_M(m);
			ERROUT(0);
		}

		mtx_lock(&seq->mtx);
		/*
		 * If not what we expect or we are busy, drop packet and
		 * send an immediate ZLB ack.
		 */
		if (ns != seq->nr || seq->inproc) {
			if (L2TP_SEQ_DIFF(ns, seq->nr) <= 0)
				priv->stats.recvDuplicates++;
			else
				priv->stats.recvOutOfOrder++;
			mtx_unlock(&seq->mtx);
			ng_l2tp_xmit_ctrl(priv, NULL, seq->ns);
			NG_FREE_ITEM(item);
			NG_FREE_M(m);
			ERROUT(0);
		}
		/*
		 * Until we deliver this packet we can't receive next one as
		 * we have no information for sending ack.
		 */
		seq->inproc = 1;
		mtx_unlock(&seq->mtx);

		/* Prepend session ID to packet. */
		M_PREPEND(m, 2, M_NOWAIT);
		if (m == NULL) {
			seq->inproc = 0;
			priv->stats.memoryFailures++;
			NG_FREE_ITEM(item);
			ERROUT(ENOBUFS);
		}
		mtod(m, u_int8_t *)[0] = sid >> 8;
		mtod(m, u_int8_t *)[1] = sid & 0xff;

		/* Deliver packet to upper layers */
		NG_FWD_NEW_DATA(error, item, priv->ctrl, m);
		
		mtx_lock(&seq->mtx);
		/* Ready to process next packet. */
		seq->inproc = 0;

		/* If packet was successfully delivered send ack. */
		if (error == 0) {
			/* Update recv sequence number */
			seq->nr++;
			/* Start receive ack timer, if not already running */
			if (!callout_active(&seq->xack_timer)) {
				ng_callout(&seq->xack_timer, priv->node, NULL,
				    L2TP_DELAYED_ACK, ng_l2tp_seq_xack_timeout,
				    NULL, 0);
			}
		}
		mtx_unlock(&seq->mtx);

		ERROUT(error);
	}

	/* Per session packet, account it. */
	hpriv->stats.recvPackets++;
	hpriv->stats.recvOctets += plen;

	/* Follow peer's lead in data sequencing, if configured to do so */
	if (!hpriv->conf.control_dseq)
		hpriv->conf.enable_dseq = ((hdr & L2TP_HDR_SEQ) != 0);

	/* Handle data sequence numbers if present and enabled */
	if ((hdr & L2TP_HDR_SEQ) != 0) {
		if (hpriv->conf.enable_dseq
		    && L2TP_SEQ_DIFF(ns, hpriv->nr) < 0) {
			NG_FREE_ITEM(item);	/* duplicate or out of order */
			NG_FREE_M(m);
			priv->stats.recvDataDrops++;
			ERROUT(0);
		}
		hpriv->nr = ns + 1;
	}

	/* Drop empty data packets */
	if (m->m_pkthdr.len == 0) {
		NG_FREE_ITEM(item);
		NG_FREE_M(m);
		ERROUT(0);
	}

	/* Deliver data */
	NG_FWD_NEW_DATA(error, item, hook, m);
done:
	/* Done */
	L2TP_SEQ_CHECK(&priv->seq);
	return (error);
}

/*
 * Handle an outgoing control frame.
 */
static int
ng_l2tp_rcvdata_ctrl(hook_p hook, item_p item)
{
	const node_p node = NG_HOOK_NODE(hook);
	const priv_p priv = NG_NODE_PRIVATE(node);
	struct l2tp_seq *const seq = &priv->seq;
	struct mbuf *m;
	int error;
	int i;
	u_int16_t	ns;

	/* Sanity check */
	L2TP_SEQ_CHECK(&priv->seq);

	/* If not configured, reject */
	if (!priv->conf.enabled) {
		NG_FREE_ITEM(item);
		ERROUT(ENXIO);
	}

	/* Grab mbuf and discard other stuff XXX */
	NGI_GET_M(item, m);
	NG_FREE_ITEM(item);

	/* Packet should have session ID prepended */
	if (m->m_pkthdr.len < 2) {
		priv->stats.xmitInvalid++;
		m_freem(m);
		ERROUT(EINVAL);
	}

	/* Check max length */
	if (m->m_pkthdr.len >= 0x10000 - 14) {
		priv->stats.xmitTooBig++;
		m_freem(m);
		ERROUT(EOVERFLOW);
	}

	mtx_lock(&seq->mtx);

	/* Find next empty slot in transmit queue */
	for (i = 0; i < L2TP_MAX_XWIN && seq->xwin[i] != NULL; i++);
	if (i == L2TP_MAX_XWIN) {
		mtx_unlock(&seq->mtx);
		priv->stats.xmitDrops++;
		m_freem(m);
		ERROUT(ENOBUFS);
	}
	seq->xwin[i] = m;

	/* If peer's receive window is already full, nothing else to do */
	if (i >= seq->cwnd) {
		mtx_unlock(&seq->mtx);
		ERROUT(0);
	}

	/* Start retransmit timer if not already running */
	if (!callout_active(&seq->rack_timer))
		ng_callout(&seq->rack_timer, node, NULL,
		    hz, ng_l2tp_seq_rack_timeout, NULL, 0);
	
	ns = seq->ns++;
	
	mtx_unlock(&seq->mtx);

	/* Copy packet */
	if ((m = L2TP_COPY_MBUF(m, M_NOWAIT)) == NULL) {
		priv->stats.memoryFailures++;
		ERROUT(ENOBUFS);
	}

	/* Send packet and increment xmit sequence number */
	error = ng_l2tp_xmit_ctrl(priv, m, ns);
done:
	/* Done */
	L2TP_SEQ_CHECK(&priv->seq);
	return (error);
}

/*
 * Handle an outgoing data frame.
 */
static int
ng_l2tp_rcvdata(hook_p hook, item_p item)
{
	const priv_p priv = NG_NODE_PRIVATE(NG_HOOK_NODE(hook));
	const hookpriv_p hpriv = NG_HOOK_PRIVATE(hook);
	struct mbuf *m;
	uint8_t *p;
	u_int16_t hdr;
	int error;
	int i = 2;

	/* Sanity check */
	L2TP_SEQ_CHECK(&priv->seq);

	/* If not configured, reject */
	if (!priv->conf.enabled) {
		NG_FREE_ITEM(item);
		ERROUT(ENXIO);
	}

	/* Get mbuf */
	NGI_GET_M(item, m);

	/* Check max length */
	if (m->m_pkthdr.len >= 0x10000 - 12) {
		priv->stats.xmitDataTooBig++;
		NG_FREE_ITEM(item);
		NG_FREE_M(m);
		ERROUT(EOVERFLOW);
	}

	/* Prepend L2TP header */
	M_PREPEND(m, 6
	    + (2 * (hpriv->conf.include_length != 0))
	    + (4 * (hpriv->conf.enable_dseq != 0)),
	    M_NOWAIT);
	if (m == NULL) {
		priv->stats.memoryFailures++;
		NG_FREE_ITEM(item);
		ERROUT(ENOBUFS);
	}
	p = mtod(m, uint8_t *);
	hdr = L2TP_DATA_HDR;
	if (hpriv->conf.include_length) {
		hdr |= L2TP_HDR_LEN;
		p[i++] = m->m_pkthdr.len >> 8;
		p[i++] = m->m_pkthdr.len & 0xff;
	}
	p[i++] = priv->conf.peer_id >> 8;
	p[i++] = priv->conf.peer_id & 0xff;
	p[i++] = hpriv->conf.peer_id >> 8;
	p[i++] = hpriv->conf.peer_id & 0xff;
	if (hpriv->conf.enable_dseq) {
		hdr |= L2TP_HDR_SEQ;
		p[i++] = hpriv->ns >> 8;
		p[i++] = hpriv->ns & 0xff;
		p[i++] = hpriv->nr >> 8;
		p[i++] = hpriv->nr & 0xff;
		hpriv->ns++;
	}
	p[0] = hdr >> 8;
	p[1] = hdr & 0xff;

	/* Update per session stats. */
	hpriv->stats.xmitPackets++;
	hpriv->stats.xmitOctets += m->m_pkthdr.len;

	/* And the global one. */
	priv->stats.xmitPackets++;
	priv->stats.xmitOctets += m->m_pkthdr.len;

	/* Send packet */
	NG_FWD_NEW_DATA(error, item, priv->lower, m);
done:
	/* Done */
	L2TP_SEQ_CHECK(&priv->seq);
	return (error);
}

/*
 * Send a message to our controlling node that we've failed.
 */
static void
ng_l2tp_seq_failure(priv_p priv)
{
	struct ng_mesg *msg;
	int error;

	NG_MKMESSAGE(msg, NGM_L2TP_COOKIE, NGM_L2TP_ACK_FAILURE, 0, M_NOWAIT);
	if (msg == NULL)
		return;
	NG_SEND_MSG_ID(error, priv->node, msg, priv->ftarget, 0);
}

/************************************************************************
			SEQUENCE NUMBER HANDLING
************************************************************************/

/*
 * Initialize sequence number state.
 */
static void
ng_l2tp_seq_init(priv_p priv)
{
	struct l2tp_seq *const seq = &priv->seq;

	KASSERT(priv->conf.peer_win >= 1,
	    ("%s: peer_win is zero", __func__));
	memset(seq, 0, sizeof(*seq));
	seq->cwnd = 1;
	seq->wmax = priv->conf.peer_win;
	if (seq->wmax > L2TP_MAX_XWIN)
		seq->wmax = L2TP_MAX_XWIN;
	seq->ssth = seq->wmax;
	ng_callout_init(&seq->rack_timer);
	ng_callout_init(&seq->xack_timer);
	mtx_init(&seq->mtx, "ng_l2tp", NULL, MTX_DEF);
	L2TP_SEQ_CHECK(seq);
}

/*
 * Set sequence number state as given from user.
 */
static int
ng_l2tp_seq_set(priv_p priv, const struct ng_l2tp_seq_config *conf)
{
	struct l2tp_seq *const seq = &priv->seq;

	/* If node is enabled, deny update to sequence numbers. */
	if (priv->conf.enabled)
		return (EBUSY);

	/* We only can handle the simple cases. */
	if (conf->xack != conf->nr || conf->ns != conf->rack)
		return (EINVAL);

	/* Set ns,nr,rack,xack parameters. */
	seq->ns = conf->ns;
	seq->nr = conf->nr;
	seq->rack = conf->rack;
	seq->xack = conf->xack;

	return (0);
}

/*
 * Adjust sequence number state accordingly after reconfiguration.
 */
static int
ng_l2tp_seq_adjust(priv_p priv, const struct ng_l2tp_config *conf)
{
	struct l2tp_seq *const seq = &priv->seq;
	u_int16_t new_wmax;

	/* If disabling node, reset state sequence number */
	if (!conf->enabled) {
		ng_l2tp_seq_reset(priv);
		return (0);
	}

	/* Adjust peer's max recv window; it can only increase */
	new_wmax = conf->peer_win;
	if (new_wmax > L2TP_MAX_XWIN)
		new_wmax = L2TP_MAX_XWIN;
	if (new_wmax == 0)
		return (EINVAL);
	if (new_wmax < seq->wmax)
		return (EBUSY);
	seq->wmax = new_wmax;

	/* Done */
	return (0);
}

/*
 * Reset sequence number state.
 */
static void
ng_l2tp_seq_reset(priv_p priv)
{
	struct l2tp_seq *const seq = &priv->seq;
	hook_p hook;
	int i;

	/* Sanity check */
	L2TP_SEQ_CHECK(seq);

	/* Stop timers */
	ng_uncallout(&seq->rack_timer, priv->node);
	ng_uncallout(&seq->xack_timer, priv->node);

	/* Free retransmit queue */
	for (i = 0; i < L2TP_MAX_XWIN; i++) {
		if (seq->xwin[i] == NULL)
			break;
		m_freem(seq->xwin[i]);
	}

	/* Reset session hooks' sequence number states */
	NG_NODE_FOREACH_HOOK(priv->node, ng_l2tp_reset_session, NULL, hook);

	/* Reset node's sequence number state */
	seq->ns = 0;
	seq->nr = 0;
	seq->rack = 0;
	seq->xack = 0;
	seq->wmax = L2TP_MAX_XWIN;
	seq->cwnd = 1;
	seq->ssth = seq->wmax;
	seq->acks = 0;
	seq->rexmits = 0;
	bzero(seq->xwin, sizeof(seq->xwin));

	/* Done */
	L2TP_SEQ_CHECK(seq);
}

/*
 * Handle receipt of an acknowledgement value (Nr) from peer.
 */
static void
ng_l2tp_seq_recv_nr(priv_p priv, u_int16_t nr)
{
	struct l2tp_seq *const seq = &priv->seq;
	struct mbuf	*xwin[L2TP_MAX_XWIN];	/* partial local copy */
	int		nack;
	int		i, j;
	uint16_t	ns;

	mtx_lock(&seq->mtx);

	/* Verify peer's ACK is in range */
	if ((nack = L2TP_SEQ_DIFF(nr, seq->rack)) <= 0) {
		mtx_unlock(&seq->mtx);
		return;				/* duplicate ack */
	}
	if (L2TP_SEQ_DIFF(nr, seq->ns) > 0) {
		mtx_unlock(&seq->mtx);
		priv->stats.recvBadAcks++;	/* ack for packet not sent */
		return;
	}
	KASSERT(nack <= L2TP_MAX_XWIN,
	    ("%s: nack=%d > %d", __func__, nack, L2TP_MAX_XWIN));

	/* Update receive ack stats */
	seq->rack = nr;
	seq->rexmits = 0;

	/* Free acknowledged packets and shift up packets in the xmit queue */
	for (i = 0; i < nack; i++)
		m_freem(seq->xwin[i]);
	memmove(seq->xwin, seq->xwin + nack,
	    (L2TP_MAX_XWIN - nack) * sizeof(*seq->xwin));
	memset(seq->xwin + (L2TP_MAX_XWIN - nack), 0,
	    nack * sizeof(*seq->xwin));

	/*
	 * Do slow-start/congestion avoidance windowing algorithm described
	 * in RFC 2661, Appendix A. Here we handle a multiple ACK as if each
	 * ACK had arrived separately.
	 */
	if (seq->cwnd < seq->wmax) {

		/* Handle slow start phase */
		if (seq->cwnd < seq->ssth) {
			seq->cwnd += nack;
			nack = 0;
			if (seq->cwnd > seq->ssth) {	/* into cg.av. phase */
				nack = seq->cwnd - seq->ssth;
				seq->cwnd = seq->ssth;
			}
		}

		/* Handle congestion avoidance phase */
		if (seq->cwnd >= seq->ssth) {
			seq->acks += nack;
			while (seq->acks >= seq->cwnd) {
				seq->acks -= seq->cwnd;
				if (seq->cwnd < seq->wmax)
					seq->cwnd++;
			}
		}
	}

	/* Stop xmit timer */
	if (callout_active(&seq->rack_timer))
		ng_uncallout(&seq->rack_timer, priv->node);

	/* If transmit queue is empty, we're done for now */
	if (seq->xwin[0] == NULL) {
		mtx_unlock(&seq->mtx);
		return;
	}

	/* Start restransmit timer again */
	ng_callout(&seq->rack_timer, priv->node, NULL,
	    hz, ng_l2tp_seq_rack_timeout, NULL, 0);

	/*
	 * Send more packets, trying to keep peer's receive window full.
	 * Make copy of everything we need before lock release.
	 */
	ns = seq->ns;
	j = 0;
	while ((i = L2TP_SEQ_DIFF(seq->ns, seq->rack)) < seq->cwnd
	    && seq->xwin[i] != NULL) {
		xwin[j++] = seq->xwin[i];
		seq->ns++;
	}

	mtx_unlock(&seq->mtx);

	/*
	 * Send prepared.
	 * If there is a memory error, pretend packet was sent, as it
	 * will get retransmitted later anyway.
	 */
	for (i = 0; i < j; i++) {
		struct mbuf 	*m;
		if ((m = L2TP_COPY_MBUF(xwin[i], M_NOWAIT)) == NULL)
			priv->stats.memoryFailures++;
		else
			ng_l2tp_xmit_ctrl(priv, m, ns);
		ns++;
	}
}

/*
 * Handle an ack timeout. We have an outstanding ack that we
 * were hoping to piggy-back, but haven't, so send a ZLB.
 */
static void
ng_l2tp_seq_xack_timeout(node_p node, hook_p hook, void *arg1, int arg2)
{
	const priv_p priv = NG_NODE_PRIVATE(node);
	struct l2tp_seq *const seq = &priv->seq;

	/* Make sure callout is still active before doing anything */
	if (callout_pending(&seq->xack_timer) ||
	    (!callout_active(&seq->xack_timer)))
		return;

	/* Sanity check */
	L2TP_SEQ_CHECK(seq);

	/* Send a ZLB */
	ng_l2tp_xmit_ctrl(priv, NULL, seq->ns);

	/* callout_deactivate() is not needed here 
	    as ng_uncallout() was called by ng_l2tp_xmit_ctrl() */

	/* Sanity check */
	L2TP_SEQ_CHECK(seq);
}

/* 
 * Handle a transmit timeout. The peer has failed to respond
 * with an ack for our packet, so retransmit it.
 */
static void
ng_l2tp_seq_rack_timeout(node_p node, hook_p hook, void *arg1, int arg2)
{
	const priv_p priv = NG_NODE_PRIVATE(node);
	struct l2tp_seq *const seq = &priv->seq;
	struct mbuf *m;
	u_int delay;

	/* Make sure callout is still active before doing anything */
	if (callout_pending(&seq->rack_timer) ||
	    (!callout_active(&seq->rack_timer)))
		return;

	/* Sanity check */
	L2TP_SEQ_CHECK(seq);

	priv->stats.xmitRetransmits++;

	/* Have we reached the retransmit limit? If so, notify owner. */
	if (seq->rexmits++ >= priv->conf.rexmit_max)
		ng_l2tp_seq_failure(priv);

	/* Restart timer, this time with an increased delay */
	delay = (seq->rexmits > 12) ? (1 << 12) : (1 << seq->rexmits);
	if (delay > priv->conf.rexmit_max_to)
		delay = priv->conf.rexmit_max_to;
	ng_callout(&seq->rack_timer, node, NULL,
	    hz * delay, ng_l2tp_seq_rack_timeout, NULL, 0);

	/* Do slow-start/congestion algorithm windowing algorithm */
	seq->ns = seq->rack;
	seq->ssth = (seq->cwnd + 1) / 2;
	seq->cwnd = 1;
	seq->acks = 0;

	/* Retransmit oldest unack'd packet */
	if ((m = L2TP_COPY_MBUF(seq->xwin[0], M_NOWAIT)) == NULL)
		priv->stats.memoryFailures++;
	else
		ng_l2tp_xmit_ctrl(priv, m, seq->ns++);

	/* callout_deactivate() is not needed here 
	    as ng_callout() is getting called each time */

	/* Sanity check */
	L2TP_SEQ_CHECK(seq);
}

/*
 * Transmit a control stream packet, payload optional.
 * The transmit sequence number is not incremented.
 */
static int
ng_l2tp_xmit_ctrl(priv_p priv, struct mbuf *m, u_int16_t ns)
{
	struct l2tp_seq *const seq = &priv->seq;
	uint8_t *p;
	u_int16_t session_id = 0;
	int error;

	mtx_lock(&seq->mtx);

	/* Stop ack timer: we're sending an ack with this packet.
	   Doing this before to keep state predictable after error. */
	if (callout_active(&seq->xack_timer))
		ng_uncallout(&seq->xack_timer, priv->node);

	seq->xack = seq->nr;

	mtx_unlock(&seq->mtx);

	/* If no mbuf passed, send an empty packet (ZLB) */
	if (m == NULL) {

		/* Create a new mbuf for ZLB packet */
		MGETHDR(m, M_NOWAIT, MT_DATA);
		if (m == NULL) {
			priv->stats.memoryFailures++;
			return (ENOBUFS);
		}
		m->m_len = m->m_pkthdr.len = 12;
		m->m_pkthdr.rcvif = NULL;
		priv->stats.xmitZLBs++;
	} else {

		/* Strip off session ID */
		if (m->m_len < 2 && (m = m_pullup(m, 2)) == NULL) {
			priv->stats.memoryFailures++;
			return (ENOBUFS);
		}
		session_id = (mtod(m, u_int8_t *)[0] << 8) + mtod(m, u_int8_t *)[1];

		/* Make room for L2TP header */
		M_PREPEND(m, 10, M_NOWAIT);	/* - 2 + 12 = 10 */
		if (m == NULL) {
			priv->stats.memoryFailures++;
			return (ENOBUFS);
		}

		/*
		 * The below requires 12 contiguous bytes for the L2TP header
		 * to be written into.
		 */
		m = m_pullup(m, 12);
		if (m == NULL) {
			priv->stats.memoryFailures++;
			return (ENOBUFS);
		}
	}

	/* Fill in L2TP header */
	p = mtod(m, u_int8_t *);
	p[0] = L2TP_CTRL_HDR >> 8;
	p[1] = L2TP_CTRL_HDR & 0xff;
	p[2] = m->m_pkthdr.len >> 8;
	p[3] = m->m_pkthdr.len & 0xff;
	p[4] = priv->conf.peer_id >> 8;
	p[5] = priv->conf.peer_id & 0xff;
	p[6] = session_id >> 8;
	p[7] = session_id & 0xff;
	p[8] = ns >> 8;
	p[9] = ns & 0xff;
	p[10] = seq->nr >> 8;
	p[11] = seq->nr & 0xff;

	/* Update sequence number info and stats */
	priv->stats.xmitPackets++;
	priv->stats.xmitOctets += m->m_pkthdr.len;

	/* Send packet */
	NG_SEND_DATA_ONLY(error, priv->lower, m);
	return (error);
}

#ifdef INVARIANTS
/*
 * Sanity check sequence number state.
 */
static void
ng_l2tp_seq_check(struct l2tp_seq *seq)
{
	int self_unack, peer_unack;
	int i;

#define CHECK(p)	KASSERT((p), ("%s: not: %s", __func__, #p))

	mtx_lock(&seq->mtx);

	self_unack = L2TP_SEQ_DIFF(seq->nr, seq->xack);
	peer_unack = L2TP_SEQ_DIFF(seq->ns, seq->rack);
	CHECK(seq->wmax <= L2TP_MAX_XWIN);
	CHECK(seq->cwnd >= 1);
	CHECK(seq->cwnd <= seq->wmax);
	CHECK(seq->ssth >= 1);
	CHECK(seq->ssth <= seq->wmax);
	if (seq->cwnd < seq->ssth)
		CHECK(seq->acks == 0);
	else
		CHECK(seq->acks <= seq->cwnd);
	CHECK(self_unack >= 0);
	CHECK(peer_unack >= 0);
	CHECK(peer_unack <= seq->wmax);
	CHECK((self_unack == 0) ^ callout_active(&seq->xack_timer));
	CHECK((peer_unack == 0) ^ callout_active(&seq->rack_timer));
	for (i = 0; i < peer_unack; i++)
		CHECK(seq->xwin[i] != NULL);
	for ( ; i < seq->cwnd; i++)	    /* verify peer's recv window full */
		CHECK(seq->xwin[i] == NULL);

	mtx_unlock(&seq->mtx);

#undef CHECK
}
#endif	/* INVARIANTS */
