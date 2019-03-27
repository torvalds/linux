/*
 * ng_async.c
 */

/*-
 * Copyright (c) 1996-1999 Whistle Communications, Inc.
 * All rights reserved.
 * 
 * Subject to the following obligations and disclaimer of warranty, use and
 * redistribution of this software, in source or object code forms, with or
 * without modifications are expressly permitted by Whistle Communications;
 * provided, however, that:
 * 1. Any and all reproductions of the source or object code must include the
 *    copyright notice above and the following disclaimer of warranties; and
 * 2. No rights are granted, in any manner or form, to use Whistle
 *    Communications, Inc. trademarks, including the mark "WHISTLE
 *    COMMUNICATIONS" on advertising, endorsements, or otherwise except as
 *    such appears in the above copyright notice or in the software.
 * 
 * THIS SOFTWARE IS BEING PROVIDED BY WHISTLE COMMUNICATIONS "AS IS", AND
 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, WHISTLE COMMUNICATIONS MAKES NO
 * REPRESENTATIONS OR WARRANTIES, EXPRESS OR IMPLIED, REGARDING THIS SOFTWARE,
 * INCLUDING WITHOUT LIMITATION, ANY AND ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT.
 * WHISTLE COMMUNICATIONS DOES NOT WARRANT, GUARANTEE, OR MAKE ANY
 * REPRESENTATIONS REGARDING THE USE OF, OR THE RESULTS OF THE USE OF THIS
 * SOFTWARE IN TERMS OF ITS CORRECTNESS, ACCURACY, RELIABILITY OR OTHERWISE.
 * IN NO EVENT SHALL WHISTLE COMMUNICATIONS BE LIABLE FOR ANY DAMAGES
 * RESULTING FROM OR ARISING OUT OF ANY USE OF THIS SOFTWARE, INCLUDING
 * WITHOUT LIMITATION, ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * PUNITIVE, OR CONSEQUENTIAL DAMAGES, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES, LOSS OF USE, DATA OR PROFITS, HOWEVER CAUSED AND UNDER ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF WHISTLE COMMUNICATIONS IS ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * Author: Archie Cobbs <archie@freebsd.org>
 *
 * $FreeBSD$
 * $Whistle: ng_async.c,v 1.17 1999/11/01 09:24:51 julian Exp $
 */

/*
 * This node type implements a PPP style sync <-> async converter.
 * See RFC 1661 for details of how asynchronous encoding works.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/errno.h>

#include <netgraph/ng_message.h>
#include <netgraph/netgraph.h>
#include <netgraph/ng_async.h>
#include <netgraph/ng_parse.h>

#include <net/ppp_defs.h>

#ifdef NG_SEPARATE_MALLOC
static MALLOC_DEFINE(M_NETGRAPH_ASYNC, "netgraph_async", "netgraph async node");
#else
#define M_NETGRAPH_ASYNC M_NETGRAPH
#endif


/* Async decode state */
#define MODE_HUNT	0
#define MODE_NORMAL	1
#define MODE_ESC	2

/* Private data structure */
struct ng_async_private {
	node_p  	node;		/* Our node */
	hook_p  	async;		/* Asynchronous side */
	hook_p  	sync;		/* Synchronous side */
	u_char  	amode;		/* Async hunt/esape mode */
	u_int16_t	fcs;		/* Decoded async FCS (so far) */
	u_char	       *abuf;		/* Buffer to encode sync into */
	u_char	       *sbuf;		/* Buffer to decode async into */
	u_int		slen;		/* Length of data in sbuf */
	long		lasttime;	/* Time of last async packet sent */
	struct		ng_async_cfg	cfg;	/* Configuration */
	struct		ng_async_stat	stats;	/* Statistics */
};
typedef struct ng_async_private *sc_p;

/* Useful macros */
#define ASYNC_BUF_SIZE(smru)	(2 * (smru) + 10)
#define SYNC_BUF_SIZE(amru)	((amru) + 10)
#define ERROUT(x)		do { error = (x); goto done; } while (0)

/* Netgraph methods */
static ng_constructor_t		nga_constructor;
static ng_rcvdata_t		nga_rcvdata;
static ng_rcvmsg_t		nga_rcvmsg;
static ng_shutdown_t		nga_shutdown;
static ng_newhook_t		nga_newhook;
static ng_disconnect_t		nga_disconnect;

/* Helper stuff */
static int	nga_rcv_sync(const sc_p sc, item_p item);
static int	nga_rcv_async(const sc_p sc, item_p item);

/* Parse type for struct ng_async_cfg */
static const struct ng_parse_struct_field nga_config_type_fields[]
	= NG_ASYNC_CONFIG_TYPE_INFO;
static const struct ng_parse_type nga_config_type = {
	&ng_parse_struct_type,
	&nga_config_type_fields
};

/* Parse type for struct ng_async_stat */
static const struct ng_parse_struct_field nga_stats_type_fields[]
	= NG_ASYNC_STATS_TYPE_INFO;
static const struct ng_parse_type nga_stats_type = {
	&ng_parse_struct_type,
	&nga_stats_type_fields
};

/* List of commands and how to convert arguments to/from ASCII */
static const struct ng_cmdlist nga_cmdlist[] = {
	{
	  NGM_ASYNC_COOKIE,
	  NGM_ASYNC_CMD_SET_CONFIG,
	  "setconfig",
	  &nga_config_type,
	  NULL
	},
	{
	  NGM_ASYNC_COOKIE,
	  NGM_ASYNC_CMD_GET_CONFIG,
	  "getconfig",
	  NULL,
	  &nga_config_type
	},
	{
	  NGM_ASYNC_COOKIE,
	  NGM_ASYNC_CMD_GET_STATS,
	  "getstats",
	  NULL,
	  &nga_stats_type
	},
	{
	  NGM_ASYNC_COOKIE,
	  NGM_ASYNC_CMD_CLR_STATS,
	  "clrstats",
	  &nga_stats_type,
	  NULL
	},
	{ 0 }
};

/* Define the netgraph node type */
static struct ng_type typestruct = {
	.version =	NG_ABI_VERSION,
	.name =		NG_ASYNC_NODE_TYPE,
	.constructor =	nga_constructor,
	.rcvmsg =	nga_rcvmsg,
	.shutdown = 	nga_shutdown,
	.newhook =	nga_newhook,
	.rcvdata =	nga_rcvdata,
	.disconnect =	nga_disconnect,
	.cmdlist =	nga_cmdlist
};
NETGRAPH_INIT(async, &typestruct);

/* CRC table */
static const u_int16_t fcstab[];

/******************************************************************
		    NETGRAPH NODE METHODS
******************************************************************/

/*
 * Initialize a new node
 */
static int
nga_constructor(node_p node)
{
	sc_p sc;

	sc = malloc(sizeof(*sc), M_NETGRAPH_ASYNC, M_WAITOK | M_ZERO);
	sc->amode = MODE_HUNT;
	sc->cfg.accm = ~0;
	sc->cfg.amru = NG_ASYNC_DEFAULT_MRU;
	sc->cfg.smru = NG_ASYNC_DEFAULT_MRU;
	sc->abuf = malloc(ASYNC_BUF_SIZE(sc->cfg.smru),
	    M_NETGRAPH_ASYNC, M_WAITOK);
	sc->sbuf = malloc(SYNC_BUF_SIZE(sc->cfg.amru),
	    M_NETGRAPH_ASYNC, M_WAITOK);
	NG_NODE_SET_PRIVATE(node, sc);
	sc->node = node;
	return (0);
}

/*
 * Reserve a hook for a pending connection
 */
static int
nga_newhook(node_p node, hook_p hook, const char *name)
{
	const sc_p sc = NG_NODE_PRIVATE(node);
	hook_p *hookp;

	if (!strcmp(name, NG_ASYNC_HOOK_ASYNC)) {
		/*
		 * We use a static buffer here so only one packet
		 * at a time can be allowed to travel in this direction.
		 * Force Writer semantics.
		 */
		NG_HOOK_FORCE_WRITER(hook);
		hookp = &sc->async;
	} else if (!strcmp(name, NG_ASYNC_HOOK_SYNC)) {
		/*
		 * We use a static state here so only one packet
		 * at a time can be allowed to travel in this direction.
		 * Force Writer semantics.
		 * Since we set this for both directions
		 * we might as well set it for the whole node
		 * bit I haven;t done that (yet).
		 */
		NG_HOOK_FORCE_WRITER(hook);
		hookp = &sc->sync;
	} else {
		return (EINVAL);
	}
	if (*hookp) /* actually can't happen I think [JRE] */
		return (EISCONN);
	*hookp = hook;
	return (0);
}

/*
 * Receive incoming data
 */
static int
nga_rcvdata(hook_p hook, item_p item)
{
	const sc_p sc = NG_NODE_PRIVATE(NG_HOOK_NODE(hook));

	if (hook == sc->sync)
		return (nga_rcv_sync(sc, item));
	if (hook == sc->async)
		return (nga_rcv_async(sc, item));
	panic("%s", __func__);
}

/*
 * Receive incoming control message
 */
static int
nga_rcvmsg(node_p node, item_p item, hook_p lasthook)
{
	const sc_p sc = NG_NODE_PRIVATE(node);
	struct ng_mesg *resp = NULL;
	int error = 0;
	struct ng_mesg *msg;
	
	NGI_GET_MSG(item, msg);
	switch (msg->header.typecookie) {
	case NGM_ASYNC_COOKIE:
		switch (msg->header.cmd) {
		case NGM_ASYNC_CMD_GET_STATS:
			NG_MKRESPONSE(resp, msg, sizeof(sc->stats), M_NOWAIT);
			if (resp == NULL)
				ERROUT(ENOMEM);
			*((struct ng_async_stat *) resp->data) = sc->stats;
			break;
		case NGM_ASYNC_CMD_CLR_STATS:
			bzero(&sc->stats, sizeof(sc->stats));
			break;
		case NGM_ASYNC_CMD_SET_CONFIG:
		    {
			struct ng_async_cfg *const cfg =
				(struct ng_async_cfg *) msg->data;
			u_char *buf;

			if (msg->header.arglen != sizeof(*cfg))
				ERROUT(EINVAL);
			if (cfg->amru < NG_ASYNC_MIN_MRU
			    || cfg->amru > NG_ASYNC_MAX_MRU
			    || cfg->smru < NG_ASYNC_MIN_MRU
			    || cfg->smru > NG_ASYNC_MAX_MRU)
				ERROUT(EINVAL);
			cfg->enabled = !!cfg->enabled;	/* normalize */
			if (cfg->smru > sc->cfg.smru) {	/* reallocate buffer */
				buf = malloc(ASYNC_BUF_SIZE(cfg->smru),
				    M_NETGRAPH_ASYNC, M_NOWAIT);
				if (!buf)
					ERROUT(ENOMEM);
				free(sc->abuf, M_NETGRAPH_ASYNC);
				sc->abuf = buf;
			}
			if (cfg->amru > sc->cfg.amru) {	/* reallocate buffer */
				buf = malloc(SYNC_BUF_SIZE(cfg->amru),
				    M_NETGRAPH_ASYNC, M_NOWAIT);
				if (!buf)
					ERROUT(ENOMEM);
				free(sc->sbuf, M_NETGRAPH_ASYNC);
				sc->sbuf = buf;
				sc->amode = MODE_HUNT;
				sc->slen = 0;
			}
			if (!cfg->enabled) {
				sc->amode = MODE_HUNT;
				sc->slen = 0;
			}
			sc->cfg = *cfg;
			break;
		    }
		case NGM_ASYNC_CMD_GET_CONFIG:
			NG_MKRESPONSE(resp, msg, sizeof(sc->cfg), M_NOWAIT);
			if (!resp)
				ERROUT(ENOMEM);
			*((struct ng_async_cfg *) resp->data) = sc->cfg;
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

/*
 * Shutdown this node
 */
static int
nga_shutdown(node_p node)
{
	const sc_p sc = NG_NODE_PRIVATE(node);

	free(sc->abuf, M_NETGRAPH_ASYNC);
	free(sc->sbuf, M_NETGRAPH_ASYNC);
	bzero(sc, sizeof(*sc));
	free(sc, M_NETGRAPH_ASYNC);
	NG_NODE_SET_PRIVATE(node, NULL);
	NG_NODE_UNREF(node);
	return (0);
}

/*
 * Lose a hook. When both hooks go away, we disappear.
 */
static int
nga_disconnect(hook_p hook)
{
	const sc_p sc = NG_NODE_PRIVATE(NG_HOOK_NODE(hook));
	hook_p *hookp;

	if (hook == sc->async)
		hookp = &sc->async;
	else if (hook == sc->sync)
		hookp = &sc->sync;
	else
		panic("%s", __func__);
	if (!*hookp)
		panic("%s 2", __func__);
	*hookp = NULL;
	bzero(&sc->stats, sizeof(sc->stats));
	sc->lasttime = 0;
	if ((NG_NODE_NUMHOOKS(NG_HOOK_NODE(hook)) == 0)
	&& (NG_NODE_IS_VALID(NG_HOOK_NODE(hook))))
		ng_rmnode_self(NG_HOOK_NODE(hook));
	return (0);
}

/******************************************************************
		    INTERNAL HELPER STUFF
******************************************************************/

/*
 * Encode a byte into the async buffer
 */
static __inline void
nga_async_add(const sc_p sc, u_int16_t *fcs, u_int32_t accm, int *len, u_char x)
{
	*fcs = PPP_FCS(*fcs, x);
	if ((x < 32 && ((1 << x) & accm))
	    || (x == PPP_ESCAPE)
	    || (x == PPP_FLAG)) {
		sc->abuf[(*len)++] = PPP_ESCAPE;
		x ^= PPP_TRANS;
	}
	sc->abuf[(*len)++] = x;
}

/*
 * Receive incoming synchronous data.
 */
static int
nga_rcv_sync(const sc_p sc, item_p item)
{
	struct ifnet *rcvif;
	int alen, error = 0;
	struct timeval time;
	u_int16_t fcs, fcs0;
	u_int32_t accm;
	struct mbuf *m;


#define ADD_BYTE(x)	nga_async_add(sc, &fcs, accm, &alen, (x))

	/* Check for bypass mode */
	if (!sc->cfg.enabled) {
		NG_FWD_ITEM_HOOK(error, item, sc->async );
		return (error);
	}
	NGI_GET_M(item, m);

	rcvif = m->m_pkthdr.rcvif;

	/* Get ACCM; special case LCP frames, which use full ACCM */
	accm = sc->cfg.accm;
	if (m->m_pkthdr.len >= 4) {
		static const u_char lcphdr[4] = {
		    PPP_ALLSTATIONS,
		    PPP_UI,
		    (u_char)(PPP_LCP >> 8),
		    (u_char)(PPP_LCP & 0xff)
		};
		u_char buf[4];

		m_copydata(m, 0, 4, (caddr_t)buf);
		if (bcmp(buf, &lcphdr, 4) == 0)
			accm = ~0;
	}

	/* Check for overflow */
	if (m->m_pkthdr.len > sc->cfg.smru) {
		sc->stats.syncOverflows++;
		NG_FREE_M(m);
		NG_FREE_ITEM(item);
		return (EMSGSIZE);
	}

	/* Update stats */
	sc->stats.syncFrames++;
	sc->stats.syncOctets += m->m_pkthdr.len;

	/* Initialize async encoded version of input mbuf */
	alen = 0;
	fcs = PPP_INITFCS;

	/* Add beginning sync flag if it's been long enough to need one */
	getmicrotime(&time);
	if (time.tv_sec >= sc->lasttime + 1) {
		sc->abuf[alen++] = PPP_FLAG;
		sc->lasttime = time.tv_sec;
	}

	/* Add packet payload */
	while (m != NULL) {
		while (m->m_len > 0) {
			ADD_BYTE(*mtod(m, u_char *));
			m->m_data++;
			m->m_len--;
		}
		m = m_free(m);
	}

	/* Add checksum and final sync flag */
	fcs0 = fcs;
	ADD_BYTE(~fcs0 & 0xff);
	ADD_BYTE(~fcs0 >> 8);
	sc->abuf[alen++] = PPP_FLAG;

	/* Put frame in an mbuf and ship it off */
	if (!(m = m_devget(sc->abuf, alen, 0, rcvif, NULL))) {
		NG_FREE_ITEM(item);
		error = ENOBUFS;
	} else {
		NG_FWD_NEW_DATA(error, item, sc->async, m);
	}
	return (error);
}

/*
 * Receive incoming asynchronous data
 * XXX Technically, we should strip out incoming characters
 *     that are in our ACCM. Not sure if this is good or not.
 */
static int
nga_rcv_async(const sc_p sc, item_p item)
{
	struct ifnet *rcvif;
	int error;
	struct mbuf *m;

	if (!sc->cfg.enabled) {
		NG_FWD_ITEM_HOOK(error, item,  sc->sync);
		return (error);
	}
	NGI_GET_M(item, m);
	rcvif = m->m_pkthdr.rcvif;
	while (m) {
		struct mbuf *n;

		for (; m->m_len > 0; m->m_data++, m->m_len--) {
			u_char  ch = *mtod(m, u_char *);

			sc->stats.asyncOctets++;
			if (ch == PPP_FLAG) {	/* Flag overrides everything */
				int     skip = 0;

				/* Check for runts */
				if (sc->slen < 2) {
					if (sc->slen > 0)
						sc->stats.asyncRunts++;
					goto reset;
				}

				/* Verify CRC */
				if (sc->fcs != PPP_GOODFCS) {
					sc->stats.asyncBadCheckSums++;
					goto reset;
				}
				sc->slen -= 2;

				/* Strip address and control fields */
				if (sc->slen >= 2
				    && sc->sbuf[0] == PPP_ALLSTATIONS
				    && sc->sbuf[1] == PPP_UI)
					skip = 2;

				/* Check for frame too big */
				if (sc->slen - skip > sc->cfg.amru) {
					sc->stats.asyncOverflows++;
					goto reset;
				}

				/* OK, ship it out */
				if ((n = m_devget(sc->sbuf + skip,
					   sc->slen - skip, 0, rcvif, NULL))) {
					if (item) { /* sets NULL -> item */
						NG_FWD_NEW_DATA(error, item,
							sc->sync, n);
					} else {
						NG_SEND_DATA_ONLY(error,
							sc->sync ,n);
					}
				}
				sc->stats.asyncFrames++;
reset:
				sc->amode = MODE_NORMAL;
				sc->fcs = PPP_INITFCS;
				sc->slen = 0;
				continue;
			}
			switch (sc->amode) {
			case MODE_NORMAL:
				if (ch == PPP_ESCAPE) {
					sc->amode = MODE_ESC;
					continue;
				}
				break;
			case MODE_ESC:
				ch ^= PPP_TRANS;
				sc->amode = MODE_NORMAL;
				break;
			case MODE_HUNT:
			default:
				continue;
			}

			/* Add byte to frame */
			if (sc->slen >= SYNC_BUF_SIZE(sc->cfg.amru)) {
				sc->stats.asyncOverflows++;
				sc->amode = MODE_HUNT;
				sc->slen = 0;
			} else {
				sc->sbuf[sc->slen++] = ch;
				sc->fcs = PPP_FCS(sc->fcs, ch);
			}
		}
		m = m_free(m);
	}
	if (item)
		NG_FREE_ITEM(item);
	return (0);
}

/*
 * CRC table
 *
 * Taken from RFC 1171 Appendix B
 */
static const u_int16_t fcstab[256] = {
	 0x0000, 0x1189, 0x2312, 0x329b, 0x4624, 0x57ad, 0x6536, 0x74bf,
	 0x8c48, 0x9dc1, 0xaf5a, 0xbed3, 0xca6c, 0xdbe5, 0xe97e, 0xf8f7,
	 0x1081, 0x0108, 0x3393, 0x221a, 0x56a5, 0x472c, 0x75b7, 0x643e,
	 0x9cc9, 0x8d40, 0xbfdb, 0xae52, 0xdaed, 0xcb64, 0xf9ff, 0xe876,
	 0x2102, 0x308b, 0x0210, 0x1399, 0x6726, 0x76af, 0x4434, 0x55bd,
	 0xad4a, 0xbcc3, 0x8e58, 0x9fd1, 0xeb6e, 0xfae7, 0xc87c, 0xd9f5,
	 0x3183, 0x200a, 0x1291, 0x0318, 0x77a7, 0x662e, 0x54b5, 0x453c,
	 0xbdcb, 0xac42, 0x9ed9, 0x8f50, 0xfbef, 0xea66, 0xd8fd, 0xc974,
	 0x4204, 0x538d, 0x6116, 0x709f, 0x0420, 0x15a9, 0x2732, 0x36bb,
	 0xce4c, 0xdfc5, 0xed5e, 0xfcd7, 0x8868, 0x99e1, 0xab7a, 0xbaf3,
	 0x5285, 0x430c, 0x7197, 0x601e, 0x14a1, 0x0528, 0x37b3, 0x263a,
	 0xdecd, 0xcf44, 0xfddf, 0xec56, 0x98e9, 0x8960, 0xbbfb, 0xaa72,
	 0x6306, 0x728f, 0x4014, 0x519d, 0x2522, 0x34ab, 0x0630, 0x17b9,
	 0xef4e, 0xfec7, 0xcc5c, 0xddd5, 0xa96a, 0xb8e3, 0x8a78, 0x9bf1,
	 0x7387, 0x620e, 0x5095, 0x411c, 0x35a3, 0x242a, 0x16b1, 0x0738,
	 0xffcf, 0xee46, 0xdcdd, 0xcd54, 0xb9eb, 0xa862, 0x9af9, 0x8b70,
	 0x8408, 0x9581, 0xa71a, 0xb693, 0xc22c, 0xd3a5, 0xe13e, 0xf0b7,
	 0x0840, 0x19c9, 0x2b52, 0x3adb, 0x4e64, 0x5fed, 0x6d76, 0x7cff,
	 0x9489, 0x8500, 0xb79b, 0xa612, 0xd2ad, 0xc324, 0xf1bf, 0xe036,
	 0x18c1, 0x0948, 0x3bd3, 0x2a5a, 0x5ee5, 0x4f6c, 0x7df7, 0x6c7e,
	 0xa50a, 0xb483, 0x8618, 0x9791, 0xe32e, 0xf2a7, 0xc03c, 0xd1b5,
	 0x2942, 0x38cb, 0x0a50, 0x1bd9, 0x6f66, 0x7eef, 0x4c74, 0x5dfd,
	 0xb58b, 0xa402, 0x9699, 0x8710, 0xf3af, 0xe226, 0xd0bd, 0xc134,
	 0x39c3, 0x284a, 0x1ad1, 0x0b58, 0x7fe7, 0x6e6e, 0x5cf5, 0x4d7c,
	 0xc60c, 0xd785, 0xe51e, 0xf497, 0x8028, 0x91a1, 0xa33a, 0xb2b3,
	 0x4a44, 0x5bcd, 0x6956, 0x78df, 0x0c60, 0x1de9, 0x2f72, 0x3efb,
	 0xd68d, 0xc704, 0xf59f, 0xe416, 0x90a9, 0x8120, 0xb3bb, 0xa232,
	 0x5ac5, 0x4b4c, 0x79d7, 0x685e, 0x1ce1, 0x0d68, 0x3ff3, 0x2e7a,
	 0xe70e, 0xf687, 0xc41c, 0xd595, 0xa12a, 0xb0a3, 0x8238, 0x93b1,
	 0x6b46, 0x7acf, 0x4854, 0x59dd, 0x2d62, 0x3ceb, 0x0e70, 0x1ff9,
	 0xf78f, 0xe606, 0xd49d, 0xc514, 0xb1ab, 0xa022, 0x92b9, 0x8330,
	 0x7bc7, 0x6a4e, 0x58d5, 0x495c, 0x3de3, 0x2c6a, 0x1ef1, 0x0f78
};
