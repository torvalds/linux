/*
 * ng_frame_relay.c
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
 * $Whistle: ng_frame_relay.c,v 1.20 1999/11/01 09:24:51 julian Exp $
 */

/*
 * This node implements the frame relay protocol, not including
 * the LMI line management. This means basically keeping track
 * of which DLCI's are active, doing frame (de)multiplexing, etc.
 *
 * It has a 'downstream' hook that goes to the line, and a
 * hook for each DLCI (eg, 'dlci16').
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/errno.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/syslog.h>
#include <sys/ctype.h>

#include <netgraph/ng_message.h>
#include <netgraph/netgraph.h>
#include <netgraph/ng_frame_relay.h>

/*
 * Line info, and status per channel.
 */
struct ctxinfo {		/* one per active hook */
	u_int   flags;
#define CHAN_VALID	0x01	/* assigned to a channel */
#define CHAN_ACTIVE	0x02	/* bottom level active */
	int     dlci;		/* the dlci assigned to this context */
	hook_p  hook;		/* if there's a hook assigned.. */
};

#define MAX_CT 16		/* # of dlci's active at a time (POWER OF 2!) */
struct frmrel_softc {
	int     unit;		/* which card are we? */
	int     datahooks;	/* number of data hooks attached */
	node_p  node;		/* netgraph node */
	int     addrlen;	/* address header length */
	int     flags;		/* state */
	int     mtu;		/* guess */
	u_char  remote_seq;	/* sequence number the remote sent */
	u_char  local_seq;	/* sequence number the remote rcvd */
	u_short ALT[1024];	/* map DLCIs to CTX */
#define	CTX_VALID	0x8000		/* this bit means it's a valid CTX */
#define	CTX_VALUE	(MAX_CT - 1)	/* mask for context part */
	struct	ctxinfo channel[MAX_CT];
	struct	ctxinfo downstream;
};
typedef struct frmrel_softc *sc_p;

#define BYTEX_EA	0x01	/* End Address. Always 0 on byte1 */
#define BYTE1_C_R	0x02
#define BYTE2_FECN	0x08	/* forwards congestion notification */
#define BYTE2_BECN	0x04	/* Backward congestion notification */
#define BYTE2_DE	0x02	/* Discard elligability */
#define LASTBYTE_D_C	0x02	/* last byte is dl_core or dlci info */

/* Used to do headers */
const static struct segment {
	u_char  mask;
	u_char  shift;
	u_char  width;
} makeup[] = {
	{ 0xfc, 2, 6 },
	{ 0xf0, 4, 4 },
	{ 0xfe, 1, 7 },
	{ 0xfc, 2, 6 }
};

#define SHIFTIN(segment, byte, dlci) 					     \
	{								     \
		(dlci) <<= (segment)->width;				     \
		(dlci) |=						     \
			(((byte) & (segment)->mask) >> (segment)->shift);    \
	}

#define SHIFTOUT(segment, byte, dlci)					     \
	{								     \
		(byte) |= (((dlci) << (segment)->shift) & (segment)->mask);  \
		(dlci) >>= (segment)->width;				     \
	}

/* Netgraph methods */
static ng_constructor_t	ngfrm_constructor;
static ng_shutdown_t	ngfrm_shutdown;
static ng_newhook_t	ngfrm_newhook;
static ng_rcvdata_t	ngfrm_rcvdata;
static ng_disconnect_t	ngfrm_disconnect;

/* Other internal functions */
static int ngfrm_decode(node_p node, item_p item);
static int ngfrm_addrlen(char *hdr);
static int ngfrm_allocate_CTX(sc_p sc, int dlci);

/* Netgraph type */
static struct ng_type typestruct = {
	.version =	NG_ABI_VERSION,
	.name =		NG_FRAMERELAY_NODE_TYPE,
	.constructor =	ngfrm_constructor,
	.shutdown =	ngfrm_shutdown,
	.newhook =	ngfrm_newhook,
	.rcvdata =	ngfrm_rcvdata,
	.disconnect =	ngfrm_disconnect,
};
NETGRAPH_INIT(framerelay, &typestruct);

#define ERROUT(x)		do { error = (x); goto done; } while (0)

/*
 * Given a DLCI, return the index of the  context table entry for it,
 * Allocating a new one if needs be, or -1 if none available.
 */
static int
ngfrm_allocate_CTX(sc_p sc, int dlci)
{
	u_int   ctxnum = -1;	/* what ctx number we are using */
	volatile struct ctxinfo *CTXp = NULL;

	/* Sanity check the dlci value */
	if (dlci > 1023)
		return (-1);

	/* Check to see if we already have an entry for this DLCI */
	if (sc->ALT[dlci]) {
		if ((ctxnum = sc->ALT[dlci] & CTX_VALUE) < MAX_CT) {
			CTXp = sc->channel + ctxnum;
		} else {
			ctxnum = -1;
			sc->ALT[dlci] = 0;	/* paranoid but... */
		}
	}

	/*
	 * If the index has no valid entry yet, then we need to allocate a
	 * CTX number to it
	 */
	if (CTXp == NULL) {
		for (ctxnum = 0; ctxnum < MAX_CT; ctxnum++) {
			/*
			 * If the VALID flag is empty it is unused
			 */
			if ((sc->channel[ctxnum].flags & CHAN_VALID) == 0) {
				bzero(sc->channel + ctxnum,
				      sizeof(struct ctxinfo));
				CTXp = sc->channel + ctxnum;
				sc->ALT[dlci] = ctxnum | CTX_VALID;
				sc->channel[ctxnum].dlci = dlci;
				sc->channel[ctxnum].flags = CHAN_VALID;
				break;
			}
		}
	}

	/*
	 * If we still don't have a CTX pointer, then we never found a free
	 * spot so give up now..
	 */
	if (!CTXp) {
		log(LOG_ERR, "No CTX available for dlci %d\n", dlci);
		return (-1);
	}
	return (ctxnum);
}

/*
 * Node constructor
 */
static int
ngfrm_constructor(node_p node)
{
	sc_p sc;

	sc = malloc(sizeof(*sc), M_NETGRAPH, M_WAITOK | M_ZERO);
	sc->addrlen = 2;	/* default */

	/* Link the node and our private info */
	NG_NODE_SET_PRIVATE(node, sc);
	sc->node = node;
	return (0);
}

/*
 * Add a new hook
 *
 * We allow hooks called "debug", "downstream" and dlci[0-1023]
 * The hook's private info points to our stash of info about that
 * channel. A NULL pointer is debug and a DLCI of -1 means downstream.
 */
static int
ngfrm_newhook(node_p node, hook_p hook, const char *name)
{
	const sc_p sc = NG_NODE_PRIVATE(node);
	const char *cp;
	char *eptr;
	int dlci = 0;
	int ctxnum;

	/* Check if it's our friend the control hook */
	if (strcmp(name, NG_FRAMERELAY_HOOK_DEBUG) == 0) {
		NG_HOOK_SET_PRIVATE(hook, NULL);	/* paranoid */
		return (0);
	}

	/*
	 * All other hooks either start with 'dlci' and have a decimal
	 * trailing channel number up to 4 digits, or are the downstream
	 * hook.
	 */
	if (strncmp(name, NG_FRAMERELAY_HOOK_DLCI,
	    strlen(NG_FRAMERELAY_HOOK_DLCI)) != 0) {

		/* It must be the downstream connection */
		if (strcmp(name, NG_FRAMERELAY_HOOK_DOWNSTREAM) != 0)
			return EINVAL;

		/* Make sure we haven't already got one (paranoid) */
		if (sc->downstream.hook)
			return (EADDRINUSE);

		/* OK add it */
		NG_HOOK_SET_PRIVATE(hook, &sc->downstream);
		sc->downstream.hook = hook;
		sc->downstream.dlci = -1;
		sc->downstream.flags |= CHAN_ACTIVE;
		sc->datahooks++;
		return (0);
	}

	/* Must be a dlci hook at this point */
	cp = name + strlen(NG_FRAMERELAY_HOOK_DLCI);
	if (!isdigit(*cp) || (cp[0] == '0' && cp[1] != '\0'))
		return (EINVAL);
	dlci = (int)strtoul(cp, &eptr, 10);
	if (*eptr != '\0' || dlci < 0 || dlci > 1023)
		return (EINVAL);

	/*
	 * We have a dlci, now either find it, or allocate it. It's possible
	 * that we might have seen packets for it already and made an entry
	 * for it.
	 */
	ctxnum = ngfrm_allocate_CTX(sc, dlci);
	if (ctxnum == -1)
		return (ENOBUFS);

	/*
	 * Be paranoid: if it's got a hook already, that dlci is in use .
	 * Generic code can not catch all the synonyms (e.g. dlci016 vs
	 * dlci16)
	 */
	if (sc->channel[ctxnum].hook != NULL)
		return (EADDRINUSE);

	/*
	 * Put our hooks into it (pun not intended)
	 */
	sc->channel[ctxnum].flags |= CHAN_ACTIVE;
	NG_HOOK_SET_PRIVATE(hook, sc->channel + ctxnum);
	sc->channel[ctxnum].hook = hook;
	sc->datahooks++;
	return (0);
}

/*
 * Count up the size of the address header if we don't already know
 */
int
ngfrm_addrlen(char *hdr)
{
	if (hdr[0] & BYTEX_EA)
		return 0;
	if (hdr[1] & BYTEX_EA)
		return 2;
	if (hdr[2] & BYTEX_EA)
		return 3;
	if (hdr[3] & BYTEX_EA)
		return 4;
	return 0;
}

/*
 * Receive data packet
 */
static int
ngfrm_rcvdata(hook_p hook, item_p item)
{
	struct	ctxinfo *const ctxp = NG_HOOK_PRIVATE(hook);
	struct	mbuf *m = NULL;
	int     error = 0;
	int     dlci;
	sc_p    sc;
	int     alen;
	char   *data;

	/* Data doesn't come in from just anywhere (e.g debug hook) */
	if (ctxp == NULL)
		ERROUT(ENETDOWN);

	/* If coming from downstream, decode it to a channel */
	dlci = ctxp->dlci;
	if (dlci == -1)
		return (ngfrm_decode(NG_HOOK_NODE(hook), item));

	NGI_GET_M(item, m);
	/* Derive the softc we will need */
	sc = NG_NODE_PRIVATE(NG_HOOK_NODE(hook));

	/* If there is no live channel, throw it away */
	if ((sc->downstream.hook == NULL)
	    || ((ctxp->flags & CHAN_ACTIVE) == 0))
		ERROUT(ENETDOWN);

	/* Store the DLCI on the front of the packet */
	alen = sc->addrlen;
	if (alen == 0)
		alen = 2;	/* default value for transmit */
	M_PREPEND(m, alen, M_NOWAIT);
	if (m == NULL)
		ERROUT(ENOBUFS);
	data = mtod(m, char *);

	/*
	 * Shift the lowest bits into the address field until we are done.
	 * First byte is MSBits of addr so work backwards.
	 */
	switch (alen) {
	case 2:
		data[0] = data[1] = '\0';
		SHIFTOUT(makeup + 1, data[1], dlci);
		SHIFTOUT(makeup + 0, data[0], dlci);
		data[1] |= BYTEX_EA;
		break;
	case 3:
		data[0] = data[1] = data[2] = '\0';
		SHIFTOUT(makeup + 3, data[2], dlci);	/* 3 and 2 is correct */
		SHIFTOUT(makeup + 1, data[1], dlci);
		SHIFTOUT(makeup + 0, data[0], dlci);
		data[2] |= BYTEX_EA;
		break;
	case 4:
		data[0] = data[1] = data[2] = data[3] = '\0';
		SHIFTOUT(makeup + 3, data[3], dlci);
		SHIFTOUT(makeup + 2, data[2], dlci);
		SHIFTOUT(makeup + 1, data[1], dlci);
		SHIFTOUT(makeup + 0, data[0], dlci);
		data[3] |= BYTEX_EA;
		break;
	default:
		panic("%s", __func__);
	}

	/* Send it */
	NG_FWD_NEW_DATA(error, item, sc->downstream.hook, m);
	return (error);

done:
	NG_FREE_ITEM(item);
	NG_FREE_M(m);
	return (error);
}

/*
 * Decode an incoming frame coming from the switch
 */
static int
ngfrm_decode(node_p node, item_p item)
{
	const sc_p  sc = NG_NODE_PRIVATE(node);
	char       *data;
	int         alen;
	u_int	    dlci = 0;
	int	    error = 0;
	int	    ctxnum;
	struct mbuf *m;

	NGI_GET_M(item, m);
	if (m->m_len < 4 && (m = m_pullup(m, 4)) == NULL)
		ERROUT(ENOBUFS);
	data = mtod(m, char *);
	if ((alen = sc->addrlen) == 0) {
		sc->addrlen = alen = ngfrm_addrlen(data);
	}
	switch (alen) {
	case 2:
		SHIFTIN(makeup + 0, data[0], dlci);
		SHIFTIN(makeup + 1, data[1], dlci);
		break;
	case 3:
		SHIFTIN(makeup + 0, data[0], dlci);
		SHIFTIN(makeup + 1, data[1], dlci);
		SHIFTIN(makeup + 3, data[2], dlci);	/* 3 and 2 is correct */
		break;
	case 4:
		SHIFTIN(makeup + 0, data[0], dlci);
		SHIFTIN(makeup + 1, data[1], dlci);
		SHIFTIN(makeup + 2, data[2], dlci);
		SHIFTIN(makeup + 3, data[3], dlci);
		break;
	default:
		ERROUT(EINVAL);
	}

	if (dlci > 1023)
		ERROUT(EINVAL);
	ctxnum = sc->ALT[dlci];
	if ((ctxnum & CTX_VALID) && sc->channel[ctxnum &= CTX_VALUE].hook) {
		/* Send it */
		m_adj(m, alen);
		NG_FWD_NEW_DATA(error, item, sc->channel[ctxnum].hook, m);
		return (error);
	} else {
		error = ENETDOWN;
	}
done:
	NG_FREE_ITEM(item);
	NG_FREE_M(m);
	return (error);
}

/*
 * Shutdown node
 */
static int
ngfrm_shutdown(node_p node)
{
	const sc_p sc = NG_NODE_PRIVATE(node);

	NG_NODE_SET_PRIVATE(node, NULL);
	free(sc, M_NETGRAPH);
	NG_NODE_UNREF(node);
	return (0);
}

/*
 * Hook disconnection
 *
 * Invalidate the private data associated with this dlci.
 * For this type, removal of the last link resets tries to destroy the node.
 */
static int
ngfrm_disconnect(hook_p hook)
{
	const sc_p sc = NG_NODE_PRIVATE(NG_HOOK_NODE(hook));
	struct ctxinfo *const cp = NG_HOOK_PRIVATE(hook);
	int dlci;

	/* If it's a regular dlci hook, then free resources etc.. */
	if (cp != NULL) {
		cp->hook = NULL;
		dlci = cp->dlci;
		if (dlci != -1)
			sc->ALT[dlci] = 0;
		cp->flags = 0;
		sc->datahooks--;
	}
	if ((NG_NODE_NUMHOOKS(NG_HOOK_NODE(hook)) == 0)
	&& (NG_NODE_IS_VALID(NG_HOOK_NODE(hook))))
		ng_rmnode_self(NG_HOOK_NODE(hook));
	return (0);
}
