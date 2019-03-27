/*
 * ng_lmi.c
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
 * Author: Julian Elischer <julian@freebsd.org>
 *
 * $FreeBSD$
 * $Whistle: ng_lmi.c,v 1.38 1999/11/01 09:24:52 julian Exp $
 */

/*
 * This node performs the frame relay LMI protocol. It knows how
 * to do ITU Annex A, ANSI Annex D, and "Group-of-Four" variants
 * of the protocol.
 *
 * A specific protocol can be forced by connecting the corresponding
 * hook to DLCI 0 or 1023 (as appropriate) of a frame relay link.
 *
 * Alternately, this node can do auto-detection of the LMI protocol
 * by connecting hook "auto0" to DLCI 0 and "auto1023" to DLCI 1023.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/syslog.h>
#include <netgraph/ng_message.h>
#include <netgraph/netgraph.h>
#include <netgraph/ng_lmi.h>

/*
 * Human readable names for LMI
 */
#define NAME_ANNEXA	NG_LMI_HOOK_ANNEXA
#define NAME_ANNEXD	NG_LMI_HOOK_ANNEXD
#define NAME_GROUP4	NG_LMI_HOOK_GROUPOF4
#define NAME_NONE	"None"

#define MAX_DLCIS	128
#define MAXDLCI		1023

/*
 * DLCI states
 */
#define DLCI_NULL	0
#define DLCI_UP		1
#define DLCI_DOWN	2

/*
 * Any received LMI frame should be at least this long
 */
#define LMI_MIN_LENGTH	8	/* XXX verify */

/*
 * Netgraph node methods and type descriptor
 */
static ng_constructor_t	nglmi_constructor;
static ng_rcvmsg_t	nglmi_rcvmsg;
static ng_shutdown_t	nglmi_shutdown;
static ng_newhook_t	nglmi_newhook;
static ng_rcvdata_t	nglmi_rcvdata;
static ng_disconnect_t	nglmi_disconnect;
static int	nglmi_checkdata(hook_p hook, struct mbuf *m);

static struct ng_type typestruct = {
	.version =	NG_ABI_VERSION,
	.name =		NG_LMI_NODE_TYPE,
	.constructor =	nglmi_constructor,
	.rcvmsg	=	nglmi_rcvmsg,
	.shutdown =	nglmi_shutdown,
	.newhook =	nglmi_newhook,
	.rcvdata =	nglmi_rcvdata,
	.disconnect =	nglmi_disconnect,
};
NETGRAPH_INIT(lmi, &typestruct);

/*
 * Info and status per node
 */
struct nglmi_softc {
	node_p  node;		/* netgraph node */
	int     flags;		/* state */
	int     poll_count;	/* the count of times for autolmi */
	int     poll_state;	/* state of auto detect machine */
	u_char  remote_seq;	/* sequence number the remote sent */
	u_char  local_seq;	/* last sequence number we sent */
	u_char  protoID;	/* 9 for group of 4, 8 otherwise */
	u_long  seq_retries;	/* sent this how many time so far */
	struct	callout	handle;	/* see timeout(9) */
	int     liv_per_full;
	int     liv_rate;
	int     livs;
	int     need_full;
	hook_p  lmi_channel;	/* whatever we ended up using */
	hook_p  lmi_annexA;
	hook_p  lmi_annexD;
	hook_p  lmi_group4;
	hook_p  lmi_channel0;	/* auto-detect on DLCI 0 */
	hook_p  lmi_channel1023;/* auto-detect on DLCI 1023 */
	char   *protoname;	/* cache protocol name */
	u_char  dlci_state[MAXDLCI + 1];
	int     invalidx;	/* next dlci's to invalidate */
};
typedef struct nglmi_softc *sc_p;

/*
 * Other internal functions
 */
static void	LMI_ticker(node_p node, hook_p hook, void *arg1, int arg2);
static void	nglmi_startup_fixed(sc_p sc, hook_p hook);
static void	nglmi_startup_auto(sc_p sc);
static void	nglmi_startup(sc_p sc);
static void	nglmi_inquire(sc_p sc, int full);
static void	ngauto_state_machine(sc_p sc);

/*
 * Values for 'flags' field
 * NB: the SCF_CONNECTED flag is set if and only if the timer is running.
 */
#define	SCF_CONNECTED	0x01	/* connected to something */
#define	SCF_AUTO	0x02	/* we are auto-detecting */
#define	SCF_FIXED	0x04	/* we are fixed from the start */

#define	SCF_LMITYPE	0x18	/* mask for determining Annex mode */
#define	SCF_NOLMI	0x00	/* no LMI type selected yet */
#define	SCF_ANNEX_A	0x08	/* running annex A mode */
#define	SCF_ANNEX_D	0x10	/* running annex D mode */
#define	SCF_GROUP4	0x18	/* running group of 4 */

#define SETLMITYPE(sc, annex)						\
do {									\
	(sc)->flags &= ~SCF_LMITYPE;					\
	(sc)->flags |= (annex);						\
} while (0)

#define NOPROTO(sc) (((sc)->flags & SCF_LMITYPE) == SCF_NOLMI)
#define ANNEXA(sc) (((sc)->flags & SCF_LMITYPE) == SCF_ANNEX_A)
#define ANNEXD(sc) (((sc)->flags & SCF_LMITYPE) == SCF_ANNEX_D)
#define GROUP4(sc) (((sc)->flags & SCF_LMITYPE) == SCF_GROUP4)

#define LMIPOLLSIZE	3
#define LMI_PATIENCE	8	/* declare all DLCI DOWN after N LMI failures */

/*
 * Node constructor
 */
static int
nglmi_constructor(node_p node)
{
	sc_p sc;

	sc = malloc(sizeof(*sc), M_NETGRAPH, M_WAITOK | M_ZERO);

	NG_NODE_SET_PRIVATE(node, sc);
	sc->node = node;

	ng_callout_init(&sc->handle);
	sc->protoname = NAME_NONE;
	sc->liv_per_full = NG_LMI_SEQ_PER_FULL;	/* make this dynamic */
	sc->liv_rate = NG_LMI_KEEPALIVE_RATE;
	return (0);
}

/*
 * The LMI channel has a private pointer which is the same as the
 * node private pointer. The debug channel has a NULL private pointer.
 */
static int
nglmi_newhook(node_p node, hook_p hook, const char *name)
{
	sc_p sc = NG_NODE_PRIVATE(node);

	if (strcmp(name, NG_LMI_HOOK_DEBUG) == 0) {
		NG_HOOK_SET_PRIVATE(hook, NULL);
		return (0);
	}
	if (sc->flags & SCF_CONNECTED) {
		/* already connected, return an error */
		return (EINVAL);
	}
	if (strcmp(name, NG_LMI_HOOK_ANNEXA) == 0) {
		sc->lmi_annexA = hook;
		NG_HOOK_SET_PRIVATE(hook, NG_NODE_PRIVATE(node));
		sc->protoID = 8;
		SETLMITYPE(sc, SCF_ANNEX_A);
		sc->protoname = NAME_ANNEXA;
		nglmi_startup_fixed(sc, hook);
	} else if (strcmp(name, NG_LMI_HOOK_ANNEXD) == 0) {
		sc->lmi_annexD = hook;
		NG_HOOK_SET_PRIVATE(hook, NG_NODE_PRIVATE(node));
		sc->protoID = 8;
		SETLMITYPE(sc, SCF_ANNEX_D);
		sc->protoname = NAME_ANNEXD;
		nglmi_startup_fixed(sc, hook);
	} else if (strcmp(name, NG_LMI_HOOK_GROUPOF4) == 0) {
		sc->lmi_group4 = hook;
		NG_HOOK_SET_PRIVATE(hook, NG_NODE_PRIVATE(node));
		sc->protoID = 9;
		SETLMITYPE(sc, SCF_GROUP4);
		sc->protoname = NAME_GROUP4;
		nglmi_startup_fixed(sc, hook);
	} else if (strcmp(name, NG_LMI_HOOK_AUTO0) == 0) {
		/* Note this, and if B is already installed, we're complete */
		sc->lmi_channel0 = hook;
		sc->protoname = NAME_NONE;
		NG_HOOK_SET_PRIVATE(hook, NG_NODE_PRIVATE(node));
		if (sc->lmi_channel1023)
			nglmi_startup_auto(sc);
	} else if (strcmp(name, NG_LMI_HOOK_AUTO1023) == 0) {
		/* Note this, and if A is already installed, we're complete */
		sc->lmi_channel1023 = hook;
		sc->protoname = NAME_NONE;
		NG_HOOK_SET_PRIVATE(hook, NG_NODE_PRIVATE(node));
		if (sc->lmi_channel0)
			nglmi_startup_auto(sc);
	} else
		return (EINVAL);		/* unknown hook */
	return (0);
}

/*
 * We have just attached to a live (we hope) node.
 * Fire out a LMI inquiry, and then start up the timers.
 */
static void
LMI_ticker(node_p node, hook_p hook, void *arg1, int arg2)
{
	sc_p sc = NG_NODE_PRIVATE(node);

	if (sc->flags & SCF_AUTO) {
		ngauto_state_machine(sc);
		ng_callout(&sc->handle, node, NULL, NG_LMI_POLL_RATE * hz,
		    LMI_ticker, NULL, 0);
	} else {
		if (sc->livs++ >= sc->liv_per_full) {
			nglmi_inquire(sc, 1);
			/* sc->livs = 0; *//* do this when we get the answer! */
		} else {
			nglmi_inquire(sc, 0);
		}
		ng_callout(&sc->handle, node, NULL, sc->liv_rate * hz,
		    LMI_ticker, NULL, 0);
	}
}

static void
nglmi_startup_fixed(sc_p sc, hook_p hook)
{
	sc->flags |= (SCF_FIXED | SCF_CONNECTED);
	sc->lmi_channel = hook;
	nglmi_startup(sc);
}

static void
nglmi_startup_auto(sc_p sc)
{
	sc->flags |= (SCF_AUTO | SCF_CONNECTED);
	sc->poll_state = 0;	/* reset state machine */
	sc->poll_count = 0;
	nglmi_startup(sc);
}

static void
nglmi_startup(sc_p sc)
{
	sc->remote_seq = 0;
	sc->local_seq = 1;
	sc->seq_retries = 0;
	sc->livs = sc->liv_per_full - 1;
	/* start off the ticker in 1 sec */
	ng_callout(&sc->handle, sc->node, NULL, hz, LMI_ticker, NULL, 0);
}

static void
nglmi_inquire(sc_p sc, int full)
{
	struct mbuf *m;
	struct ng_tag_prio *ptag;
	char   *cptr, *start;
	int     error;

	if (sc->lmi_channel == NULL)
		return;
	MGETHDR(m, M_NOWAIT, MT_DATA);
	if (m == NULL) {
		log(LOG_ERR, "nglmi: unable to start up LMI processing\n");
		return;
	}
	m->m_pkthdr.rcvif = NULL;

	/* Attach a tag to packet, marking it of link level state priority, so
	 * that device driver would put it in the beginning of queue */

	ptag = (struct ng_tag_prio *)m_tag_alloc(NGM_GENERIC_COOKIE, NG_TAG_PRIO,
	    (sizeof(struct ng_tag_prio) - sizeof(struct m_tag)), M_NOWAIT);
	if (ptag != NULL) {	/* if it failed, well, it was optional anyhow */
		ptag->priority = NG_PRIO_LINKSTATE;
		ptag->discardability = -1;
		m_tag_prepend(m, &ptag->tag);
	}

	m->m_data += 4;		/* leave some room for a header */
	cptr = start = mtod(m, char *);
	/* add in the header for an LMI inquiry. */
	*cptr++ = 0x03;		/* UI frame */
	if (GROUP4(sc))
		*cptr++ = 0x09;	/* proto discriminator */
	else
		*cptr++ = 0x08;	/* proto discriminator */
	*cptr++ = 0x00;		/* call reference */
	*cptr++ = 0x75;		/* inquiry */

	/* If we are Annex-D, add locking shift to codeset 5. */
	if (ANNEXD(sc))
		*cptr++ = 0x95;	/* locking shift */
	/* Add a request type */
	if (ANNEXA(sc))
		*cptr++ = 0x51;	/* report type */
	else
		*cptr++ = 0x01;	/* report type */
	*cptr++ = 0x01;		/* size = 1 */
	if (full)
		*cptr++ = 0x00;	/* full */
	else
		*cptr++ = 0x01;	/* partial */

	/* Add a link verification IE */
	if (ANNEXA(sc))
		*cptr++ = 0x53;	/* verification IE */
	else
		*cptr++ = 0x03;	/* verification IE */
	*cptr++ = 0x02;		/* 2 extra bytes */
	*cptr++ = sc->local_seq;
	*cptr++ = sc->remote_seq;
	sc->seq_retries++;

	/* Send it */
	m->m_len = m->m_pkthdr.len = cptr - start;
	NG_SEND_DATA_ONLY(error, sc->lmi_channel, m);

	/* If we've been sending requests for long enough, and there has
	 * been no response, then mark as DOWN, any DLCIs that are UP. */
	if (sc->seq_retries == LMI_PATIENCE) {
		int     count;

		for (count = 0; count < MAXDLCI; count++)
			if (sc->dlci_state[count] == DLCI_UP)
				sc->dlci_state[count] = DLCI_DOWN;
	}
}

/*
 * State machine for LMI auto-detect. The transitions are ordered
 * to try the more likely possibilities first.
 */
static void
ngauto_state_machine(sc_p sc)
{
	if ((sc->poll_count <= 0) || (sc->poll_count > LMIPOLLSIZE)) {
		/* time to change states in the auto probe machine */
		/* capture wild values of poll_count while we are at it */
		sc->poll_count = LMIPOLLSIZE;
		sc->poll_state++;
	}
	switch (sc->poll_state) {
	case 7:
		log(LOG_WARNING, "nglmi: no response from exchange\n");
	default:		/* capture bad states */
		sc->poll_state = 1;
	case 1:
		sc->lmi_channel = sc->lmi_channel0;
		SETLMITYPE(sc, SCF_ANNEX_D);
		break;
	case 2:
		sc->lmi_channel = sc->lmi_channel1023;
		SETLMITYPE(sc, SCF_ANNEX_D);
		break;
	case 3:
		sc->lmi_channel = sc->lmi_channel0;
		SETLMITYPE(sc, SCF_ANNEX_A);
		break;
	case 4:
		sc->lmi_channel = sc->lmi_channel1023;
		SETLMITYPE(sc, SCF_GROUP4);
		break;
	case 5:
		sc->lmi_channel = sc->lmi_channel1023;
		SETLMITYPE(sc, SCF_ANNEX_A);
		break;
	case 6:
		sc->lmi_channel = sc->lmi_channel0;
		SETLMITYPE(sc, SCF_GROUP4);
		break;
	}

	/* send an inquirey encoded appropriately */
	nglmi_inquire(sc, 0);
	sc->poll_count--;
}

/*
 * Receive a netgraph control message.
 */
static int
nglmi_rcvmsg(node_p node, item_p item, hook_p lasthook)
{
	sc_p    sc = NG_NODE_PRIVATE(node);
	struct ng_mesg *resp = NULL;
	int     error = 0;
	struct ng_mesg *msg;

	NGI_GET_MSG(item, msg);
	switch (msg->header.typecookie) {
	case NGM_GENERIC_COOKIE:
		switch (msg->header.cmd) {
		case NGM_TEXT_STATUS:
		    {
			char   *arg;
			int     pos, count;

			NG_MKRESPONSE(resp, msg, NG_TEXTRESPONSE, M_NOWAIT);
			if (resp == NULL) {
				error = ENOMEM;
				break;
			}
			arg = resp->data;
			pos = sprintf(arg, "protocol %s ", sc->protoname);
			if (sc->flags & SCF_FIXED)
				pos += sprintf(arg + pos, "fixed\n");
			else if (sc->flags & SCF_AUTO)
				pos += sprintf(arg + pos, "auto-detecting\n");
			else
				pos += sprintf(arg + pos, "auto on dlci %d\n",
				    (sc->lmi_channel == sc->lmi_channel0) ?
				    0 : 1023);
			pos += sprintf(arg + pos,
			    "keepalive period: %d seconds\n", sc->liv_rate);
			pos += sprintf(arg + pos,
			    "unacknowledged keepalives: %ld\n",
			    sc->seq_retries);
			for (count = 0;
			     ((count <= MAXDLCI)
			      && (pos < (NG_TEXTRESPONSE - 20)));
			     count++) {
				if (sc->dlci_state[count]) {
					pos += sprintf(arg + pos,
					       "dlci %d %s\n", count,
					       (sc->dlci_state[count]
					== DLCI_UP) ? "up" : "down");
				}
			}
			resp->header.arglen = pos + 1;
			break;
		    }
		default:
			error = EINVAL;
			break;
		}
		break;
	case NGM_LMI_COOKIE:
		switch (msg->header.cmd) {
		case NGM_LMI_GET_STATUS:
		    {
			struct nglmistat *stat;
			int k;

			NG_MKRESPONSE(resp, msg, sizeof(*stat), M_NOWAIT);
			if (!resp) {
				error = ENOMEM;
				break;
			}
			stat = (struct nglmistat *) resp->data;
			strncpy(stat->proto,
			     sc->protoname, sizeof(stat->proto) - 1);
			strncpy(stat->hook,
			      sc->protoname, sizeof(stat->hook) - 1);
			stat->autod = !!(sc->flags & SCF_AUTO);
			stat->fixed = !!(sc->flags & SCF_FIXED);
			for (k = 0; k <= MAXDLCI; k++) {
				switch (sc->dlci_state[k]) {
				case DLCI_UP:
					stat->up[k / 8] |= (1 << (k % 8));
					/* fall through */
				case DLCI_DOWN:
					stat->seen[k / 8] |= (1 << (k % 8));
					break;
				}
			}
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

	NG_RESPOND_MSG(error, node, item, resp);
	NG_FREE_MSG(msg);
	return (error);
}

#define STEPBY(stepsize)			\
	do {					\
		packetlen -= (stepsize);	\
		data += (stepsize);		\
	} while (0)

/*
 * receive data, and use it to update our status.
 * Anything coming in on the debug port is discarded.
 */
static int
nglmi_rcvdata(hook_p hook, item_p item)
{
	sc_p    sc = NG_NODE_PRIVATE(NG_HOOK_NODE(hook));
	const	u_char *data;
	unsigned short dlci;
	u_short packetlen;
	int     resptype_seen = 0;
	struct mbuf *m;

	NGI_GET_M(item, m);
	NG_FREE_ITEM(item);
	if (NG_HOOK_PRIVATE(hook) == NULL) {
		goto drop;
	}
	packetlen = m->m_len;

	/* XXX what if it's more than 1 mbuf? */
	if ((packetlen > MHLEN) && !(m->m_flags & M_EXT)) {
		log(LOG_WARNING, "nglmi: packetlen (%d) too big\n", packetlen);
		goto drop;
	}
	if (m->m_len < packetlen && (m = m_pullup(m, packetlen)) == NULL) {
		log(LOG_WARNING,
		    "nglmi: m_pullup failed for %d bytes\n", packetlen);
		return (0);
	}
	if (nglmi_checkdata(hook, m) == 0)
		return (0);

	/* pass the first 4 bytes (already checked in the nglmi_checkdata()) */
	data = mtod(m, const u_char *);
	STEPBY(4);

	/* Now check if there is a 'locking shift'. This is only seen in
	 * Annex D frames. don't bother checking, we already did that. Don't
	 * increment immediately as it might not be there. */
	if (ANNEXD(sc))
		STEPBY(1);

	/* If we get this far we should consider that it is a legitimate
	 * frame and we know what it is. */
	if (sc->flags & SCF_AUTO) {
		/* note the hook that this valid channel came from and drop
		 * out of auto probe mode. */
		if (ANNEXA(sc))
			sc->protoname = NAME_ANNEXA;
		else if (ANNEXD(sc))
			sc->protoname = NAME_ANNEXD;
		else if (GROUP4(sc))
			sc->protoname = NAME_GROUP4;
		else {
			log(LOG_ERR, "nglmi: No known type\n");
			goto drop;
		}
		sc->lmi_channel = hook;
		sc->flags &= ~SCF_AUTO;
		log(LOG_INFO, "nglmi: auto-detected %s LMI on DLCI %d\n",
		    sc->protoname, hook == sc->lmi_channel0 ? 0 : 1023);
	}

	/* While there is more data in the status packet, keep processing
	 * status items. First make sure there is enough data for the
	 * segment descriptor's length field. */
	while (packetlen >= 2) {
		u_int   segtype = data[0];
		u_int   segsize = data[1];

		/* Now that we know how long it claims to be, make sure
		 * there is enough data for the next seg. */
		if (packetlen < segsize + 2)
			break;
		switch (segtype) {
		case 0x01:
		case 0x51:
			if (resptype_seen) {
				log(LOG_WARNING, "nglmi: dup MSGTYPE\n");
				goto nextIE;
			}
			resptype_seen++;
			/* The remote end tells us what kind of response
			 * this is. Only expect a type 0 or 1. if we are a
			 * full status, invalidate a few DLCIs just to see
			 * that they are still ok. */
			if (segsize != 1)
				goto nextIE;
			switch (data[2]) {
			case 1:
				/* partial status, do no extra processing */
				break;
			case 0:
			    {
				int     count = 0;
				int     idx = sc->invalidx;

				for (count = 0; count < 10; count++) {
					if (idx > MAXDLCI)
						idx = 0;
					if (sc->dlci_state[idx] == DLCI_UP)
						sc->dlci_state[idx] = DLCI_DOWN;
					idx++;
				}
				sc->invalidx = idx;
				/* we got and we wanted one. relax
				 * now.. but don't reset to 0 if it
				 * was unrequested. */
				if (sc->livs > sc->liv_per_full)
					sc->livs = 0;
				break;
			    }
			}
			break;
		case 0x03:
		case 0x53:
			/* The remote tells us what it thinks the sequence
			 * numbers are. If it's not size 2, it must be a
			 * duplicate to have gotten this far, skip it. */
			if (segsize != 2)
				goto nextIE;
			sc->remote_seq = data[2];
			if (sc->local_seq == data[3]) {
				sc->local_seq++;
				sc->seq_retries = 0;
				/* Note that all 3 Frame protocols seem to
				 * not like 0 as a sequence number. */
				if (sc->local_seq == 0)
					sc->local_seq = 1;
			}
			break;
		case 0x07:
		case 0x57:
			/* The remote tells us about a DLCI that it knows
			 * about. There may be many of these in a single
			 * status response */
			switch (segsize) {
			case 6:/* only on 'group of 4' */
				dlci = ((u_short) data[2] & 0xff) << 8;
				dlci |= (data[3] & 0xff);
				if ((dlci < 1024) && (dlci > 0)) {
				  /* XXX */
				}
				break;
			case 3:
				dlci = ((u_short) data[2] & 0x3f) << 4;
				dlci |= ((data[3] & 0x78) >> 3);
				if ((dlci < 1024) && (dlci > 0)) {
					/* set up the bottom half of the
					 * support for that dlci if it's not
					 * already been done */
					/* store this information somewhere */
				}
				break;
			default:
				goto nextIE;
			}
			if (sc->dlci_state[dlci] != DLCI_UP) {
				/* bring new DLCI to life */
				/* may do more here some day */
				if (sc->dlci_state[dlci] != DLCI_DOWN)
					log(LOG_INFO,
					    "nglmi: DLCI %d became active\n",
					    dlci);
				sc->dlci_state[dlci] = DLCI_UP;
			}
			break;
		}
nextIE:
		STEPBY(segsize + 2);
	}
	NG_FREE_M(m);
	return (0);

drop:
	NG_FREE_M(m);
	return (EINVAL);
}

/*
 * Check that a packet is entirely kosha.
 * return 1 of ok, and 0 if not.
 * All data is discarded if a 0 is returned.
 */
static int
nglmi_checkdata(hook_p hook, struct mbuf *m)
{
	sc_p    sc = NG_NODE_PRIVATE(NG_HOOK_NODE(hook));
	const	u_char *data;
	u_short packetlen;
	unsigned short dlci;
	u_char  type;
	u_char  nextbyte;
	int     seq_seen = 0;
	int     resptype_seen = 0;	/* 0 , 1 (partial) or 2 (full) */
	int     highest_dlci = 0;

	packetlen = m->m_len;
	data = mtod(m, const u_char *);
	if (*data != 0x03) {
		log(LOG_WARNING, "nglmi: unexpected value in LMI(%d)\n", 1);
		goto reject;
	}
	STEPBY(1);

	/* look at the protocol ID */
	nextbyte = *data;
	if (sc->flags & SCF_AUTO) {
		SETLMITYPE(sc, SCF_NOLMI);	/* start with a clean slate */
		switch (nextbyte) {
		case 0x8:
			sc->protoID = 8;
			break;
		case 0x9:
			SETLMITYPE(sc, SCF_GROUP4);
			sc->protoID = 9;
			break;
		default:
			log(LOG_WARNING, "nglmi: bad Protocol ID(%d)\n",
			    (int) nextbyte);
			goto reject;
		}
	} else {
		if (nextbyte != sc->protoID) {
			log(LOG_WARNING, "nglmi: unexpected Protocol ID(%d)\n",
			    (int) nextbyte);
			goto reject;
		}
	}
	STEPBY(1);

	/* check call reference (always null in non ISDN frame relay) */
	if (*data != 0x00) {
		log(LOG_WARNING, "nglmi: unexpected Call Reference (0x%x)\n",
		    data[-1]);
		goto reject;
	}
	STEPBY(1);

	/* check message type */
	switch ((type = *data)) {
	case 0x75:		/* Status enquiry */
		log(LOG_WARNING, "nglmi: unexpected message type(0x%x)\n",
		    data[-1]);
		goto reject;
	case 0x7D:		/* Status message */
		break;
	default:
		log(LOG_WARNING,
		    "nglmi: unexpected msg type(0x%x) \n", (int) type);
		goto reject;
	}
	STEPBY(1);

	/* Now check if there is a 'locking shift'. This is only seen in
	 * Annex D frames. Don't increment immediately as it might not be
	 * there. */
	nextbyte = *data;
	if (sc->flags & SCF_AUTO) {
		if (!(GROUP4(sc))) {
			if (nextbyte == 0x95) {
				SETLMITYPE(sc, SCF_ANNEX_D);
				STEPBY(1);
			} else
				SETLMITYPE(sc, SCF_ANNEX_A);
		} else if (nextbyte == 0x95) {
			log(LOG_WARNING, "nglmi: locking shift seen in G4\n");
			goto reject;
		}
	} else {
		if (ANNEXD(sc)) {
			if (*data == 0x95)
				STEPBY(1);
			else {
				log(LOG_WARNING,
				    "nglmi: locking shift missing\n");
				goto reject;
			}
		} else if (*data == 0x95) {
			log(LOG_WARNING, "nglmi: locking shift seen\n");
			goto reject;
		}
	}

	/* While there is more data in the status packet, keep processing
	 * status items. First make sure there is enough data for the
	 * segment descriptor's length field. */
	while (packetlen >= 2) {
		u_int   segtype = data[0];
		u_int   segsize = data[1];

		/* Now that we know how long it claims to be, make sure
		 * there is enough data for the next seg. */
		if (packetlen < (segsize + 2)) {
			log(LOG_WARNING, "nglmi: IE longer than packet\n");
			break;
		}
		switch (segtype) {
		case 0x01:
		case 0x51:
			/* According to MCI's HP analyser, we should just
			 * ignore if there is mor ethan one of these (?). */
			if (resptype_seen) {
				log(LOG_WARNING, "nglmi: dup MSGTYPE\n");
				goto nextIE;
			}
			if (segsize != 1) {
				log(LOG_WARNING, "nglmi: MSGTYPE wrong size\n");
				goto reject;
			}
			/* The remote end tells us what kind of response
			 * this is. Only expect a type 0 or 1. if it was a
			 * full (type 0) check we just asked for a type
			 * full. */
			switch (data[2]) {
			case 1:/* partial */
				if (sc->livs > sc->liv_per_full) {
					log(LOG_WARNING,
					  "nglmi: LIV when FULL expected\n");
					goto reject;	/* need full */
				}
				resptype_seen = 1;
				break;
			case 0:/* full */
				/* Full response is always acceptable */
				resptype_seen = 2;
				break;
			default:
				log(LOG_WARNING,
				 "nglmi: Unknown report type %d\n", data[2]);
				goto reject;
			}
			break;
		case 0x03:
		case 0x53:
			/* The remote tells us what it thinks the sequence
			 * numbers are. I would have thought that there
			 * needs to be one and only one of these, but MCI
			 * want us to just ignore extras. (?) */
			if (resptype_seen == 0) {
				log(LOG_WARNING, "nglmi: no TYPE before SEQ\n");
				goto reject;
			}
			if (seq_seen != 0)	/* already seen seq numbers */
				goto nextIE;
			if (segsize != 2) {
				log(LOG_WARNING, "nglmi: bad SEQ sts size\n");
				goto reject;
			}
			if (sc->local_seq != data[3]) {
				log(LOG_WARNING, "nglmi: unexpected SEQ\n");
				goto reject;
			}
			seq_seen = 1;
			break;
		case 0x07:
		case 0x57:
			/* The remote tells us about a DLCI that it knows
			 * about. There may be many of these in a single
			 * status response */
			if (seq_seen != 1) {	/* already seen seq numbers? */
				log(LOG_WARNING,
				    "nglmi: No sequence before DLCI\n");
				goto reject;
			}
			if (resptype_seen != 2) {	/* must be full */
				log(LOG_WARNING,
				    "nglmi: No resp type before DLCI\n");
				goto reject;
			}
			if (GROUP4(sc)) {
				if (segsize != 6) {
					log(LOG_WARNING,
					    "nglmi: wrong IE segsize\n");
					goto reject;
				}
				dlci = ((u_short) data[2] & 0xff) << 8;
				dlci |= (data[3] & 0xff);
			} else {
				if (segsize != 3) {
					log(LOG_WARNING,
					    "nglmi: DLCI headersize of %d"
					    " not supported\n", segsize - 1);
					goto reject;
				}
				dlci = ((u_short) data[2] & 0x3f) << 4;
				dlci |= ((data[3] & 0x78) >> 3);
			}
			/* async can only have one of these */
#if 0				/* async not yet accepted */
			if (async && highest_dlci) {
				log(LOG_WARNING,
				    "nglmi: Async with > 1 DLCI\n");
				goto reject;
			}
#endif
			/* Annex D says these will always be Ascending, but
			 * the HP test for G4 says we should accept
			 * duplicates, so for now allow that. ( <= vs. < ) */
#if 0
			/* MCI tests want us to accept out of order for AnxD */
			if ((!GROUP4(sc)) && (dlci < highest_dlci)) {
				/* duplicate or mis-ordered dlci */
				/* (spec says they will increase in number) */
				log(LOG_WARNING, "nglmi: DLCI out of order\n");
				goto reject;
			}
#endif
			if (dlci > 1023) {
				log(LOG_WARNING, "nglmi: DLCI out of range\n");
				goto reject;
			}
			highest_dlci = dlci;
			break;
		default:
			log(LOG_WARNING,
			    "nglmi: unknown LMI segment type %d\n", segtype);
		}
nextIE:
		STEPBY(segsize + 2);
	}
	if (packetlen != 0) {	/* partial junk at end? */
		log(LOG_WARNING,
		    "nglmi: %d bytes extra at end of packet\n", packetlen);
		goto print;
	}
	if (resptype_seen == 0) {
		log(LOG_WARNING, "nglmi: No response type seen\n");
		goto reject;	/* had no response type */
	}
	if (seq_seen == 0) {
		log(LOG_WARNING, "nglmi: No sequence numbers seen\n");
		goto reject;	/* had no sequence numbers */
	}
	return (1);

print:
	{
		int     i, j, k, pos;
		char    buf[100];
		int     loc;
		const	u_char *bp = mtod(m, const u_char *);

		k = i = 0;
		loc = (m->m_len - packetlen);
		log(LOG_WARNING, "nglmi: error at location %d\n", loc);
		while (k < m->m_len) {
			pos = 0;
			j = 0;
			while ((j++ < 16) && k < m->m_len) {
				pos += sprintf(buf + pos, "%c%02x",
					       ((loc == k) ? '>' : ' '),
					       bp[k]);
				k++;
			}
			if (i == 0)
				log(LOG_WARNING, "nglmi: packet data:%s\n", buf);
			else
				log(LOG_WARNING, "%04d              :%s\n", k, buf);
			i++;
		}
	}
	return (1);
reject:
	{
		int     i, j, k, pos;
		char    buf[100];
		int     loc;
		const	u_char *bp = mtod(m, const u_char *);

		k = i = 0;
		loc = (m->m_len - packetlen);
		log(LOG_WARNING, "nglmi: error at location %d\n", loc);
		while (k < m->m_len) {
			pos = 0;
			j = 0;
			while ((j++ < 16) && k < m->m_len) {
				pos += sprintf(buf + pos, "%c%02x",
					       ((loc == k) ? '>' : ' '),
					       bp[k]);
				k++;
			}
			if (i == 0)
				log(LOG_WARNING, "nglmi: packet data:%s\n", buf);
			else
				log(LOG_WARNING, "%04d              :%s\n", k, buf);
			i++;
		}
	}
	NG_FREE_M(m);
	return (0);
}

/*
 * Do local shutdown processing..
 * Cut any remaining links and free our local resources.
 */
static int
nglmi_shutdown(node_p node)
{
	const sc_p sc = NG_NODE_PRIVATE(node);

	NG_NODE_SET_PRIVATE(node, NULL);
	NG_NODE_UNREF(sc->node);
	free(sc, M_NETGRAPH);
	return (0);
}

/*
 * Hook disconnection
 * For this type, removal of any link except "debug" destroys the node.
 */
static int
nglmi_disconnect(hook_p hook)
{
	const sc_p sc = NG_NODE_PRIVATE(NG_HOOK_NODE(hook));

	/* OK to remove debug hook(s) */
	if (NG_HOOK_PRIVATE(hook) == NULL)
		return (0);

	/* Stop timer if it's currently active */
	if (sc->flags & SCF_CONNECTED)
		ng_uncallout(&sc->handle, sc->node);

	/* Self-destruct */
	if (NG_NODE_IS_VALID(NG_HOOK_NODE(hook)))
		ng_rmnode_self(NG_HOOK_NODE(hook));
	return (0);
}

