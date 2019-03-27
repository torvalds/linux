/*
 * ng_tty.c
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
 * Updated by Andrew Thompson <thompsa@FreeBSD.org> for MPSAFE TTY.
 *
 * $FreeBSD$
 * $Whistle: ng_tty.c,v 1.21 1999/11/01 09:24:52 julian Exp $
 */

/*
 * This file implements TTY hooks to link in to the netgraph system.  The node
 * is created and then passed the callers opened TTY file descriptor number to
 * NGM_TTY_SET_TTY, this will hook the tty via ttyhook_register().
 *
 * Incoming data is delivered directly to ng_tty via the TTY bypass hook as a
 * buffer pointer and length, this is converted to a mbuf and passed to the
 * peer.
 *
 * If the TTY device does not support bypass then incoming characters are
 * delivered to the hook one at a time, each in its own mbuf. You may
 * optionally define a ``hotchar,'' which causes incoming characters to be
 * buffered up until either the hotchar is seen or the mbuf is full (MHLEN
 * bytes). Then all buffered characters are immediately delivered.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/ioccom.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/priv.h>
#include <sys/socket.h>
#include <sys/syslog.h>
#include <sys/tty.h>
#include <sys/ttycom.h>
#include <sys/proc.h>

#include <net/if.h>
#include <net/if_var.h>

#include <netgraph/ng_message.h>
#include <netgraph/netgraph.h>
#include <netgraph/ng_tty.h>

/* Per-node private info */
struct ngt_softc {
	struct tty	*tp;		/* Terminal device */
	node_p		node;		/* Netgraph node */
	hook_p		hook;		/* Netgraph hook */
	struct ifqueue	outq;		/* Queue of outgoing data */
	size_t		outqlen;	/* Number of bytes in outq */
	struct mbuf	*m;		/* Incoming non-bypass data buffer */
	short		hotchar;	/* Hotchar, or -1 if none */
	u_int		flags;		/* Flags */
};
typedef struct ngt_softc *sc_p;

/* Flags */
#define FLG_DEBUG		0x0002

/* Netgraph methods */
static ng_constructor_t		ngt_constructor;
static ng_rcvmsg_t		ngt_rcvmsg;
static ng_shutdown_t		ngt_shutdown;
static ng_newhook_t		ngt_newhook;
static ng_connect_t		ngt_connect;
static ng_rcvdata_t		ngt_rcvdata;
static ng_disconnect_t		ngt_disconnect;

#define ERROUT(x)		do { error = (x); goto done; } while (0)

static th_getc_inject_t		ngt_getc_inject;
static th_getc_poll_t		ngt_getc_poll;
static th_rint_t		ngt_rint;
static th_rint_bypass_t		ngt_rint_bypass;
static th_rint_poll_t		ngt_rint_poll;

static struct ttyhook ngt_hook = {
	.th_getc_inject = ngt_getc_inject,
	.th_getc_poll = ngt_getc_poll,
	.th_rint = ngt_rint,
	.th_rint_bypass = ngt_rint_bypass,
	.th_rint_poll = ngt_rint_poll,
};

/* Netgraph node type descriptor */
static struct ng_type typestruct = {
	.version =	NG_ABI_VERSION,
	.name =		NG_TTY_NODE_TYPE,
	.constructor =	ngt_constructor,
	.rcvmsg =	ngt_rcvmsg,
	.shutdown =	ngt_shutdown,
	.newhook =	ngt_newhook,
	.connect =	ngt_connect,
	.rcvdata =	ngt_rcvdata,
	.disconnect =	ngt_disconnect,
};
NETGRAPH_INIT(tty, &typestruct);

#define	NGTLOCK(sc)	IF_LOCK(&sc->outq)
#define	NGTUNLOCK(sc)	IF_UNLOCK(&sc->outq)

/******************************************************************
		    NETGRAPH NODE METHODS
******************************************************************/

/*
 * Initialize a new node of this type.
 *
 * We only allow nodes to be created as a result of setting
 * the line discipline on a tty, so always return an error if not.
 */
static int
ngt_constructor(node_p node)
{
	sc_p sc;

	/* Allocate private structure */
	sc = malloc(sizeof(*sc), M_NETGRAPH, M_WAITOK | M_ZERO);

	NG_NODE_SET_PRIVATE(node, sc);
	sc->node = node;

	mtx_init(&sc->outq.ifq_mtx, "ng_tty node+queue", NULL, MTX_DEF);
	IFQ_SET_MAXLEN(&sc->outq, ifqmaxlen);

	return (0);
}

/*
 * Add a new hook. There can only be one.
 */
static int
ngt_newhook(node_p node, hook_p hook, const char *name)
{
	const sc_p sc = NG_NODE_PRIVATE(node);

	if (strcmp(name, NG_TTY_HOOK))
		return (EINVAL);

	if (sc->hook)
		return (EISCONN);

	NGTLOCK(sc);
	sc->hook = hook;
	NGTUNLOCK(sc);

	return (0);
}

/*
 * Set the hook into queueing mode (for outgoing packets),
 * so that we wont deliver mbuf through the whole graph holding
 * tty locks.
 */
static int
ngt_connect(hook_p hook)
{
	NG_HOOK_FORCE_QUEUE(hook);
	return (0);
}

/*
 * Disconnect the hook
 */
static int
ngt_disconnect(hook_p hook)
{
	const sc_p sc = NG_NODE_PRIVATE(NG_HOOK_NODE(hook));

	if (hook != sc->hook)
		panic("%s", __func__);

	NGTLOCK(sc);
	sc->hook = NULL;
	NGTUNLOCK(sc);

	return (0);
}

/*
 * Remove this node. The does the netgraph portion of the shutdown.
 */
static int
ngt_shutdown(node_p node)
{
	const sc_p sc = NG_NODE_PRIVATE(node);
	struct tty *tp;

	tp = sc->tp;
	if (tp != NULL) {
		tty_lock(tp);
		ttyhook_unregister(tp);
	}
	/* Free resources */
	IF_DRAIN(&sc->outq);
	mtx_destroy(&(sc)->outq.ifq_mtx);
	NG_NODE_UNREF(sc->node);
	free(sc, M_NETGRAPH);

	return (0);
}

/*
 * Receive control message
 */
static int
ngt_rcvmsg(node_p node, item_p item, hook_p lasthook)
{
	struct proc *p;
	const sc_p sc = NG_NODE_PRIVATE(node);
	struct ng_mesg *msg, *resp = NULL;
	int error = 0;

	NGI_GET_MSG(item, msg);
	switch (msg->header.typecookie) {
	case NGM_TTY_COOKIE:
		switch (msg->header.cmd) {
		case NGM_TTY_SET_TTY:
			if (sc->tp != NULL)
				return (EBUSY);
			
			p = pfind(((int *)msg->data)[0]);
			if (p == NULL || (p->p_flag & P_WEXIT))
				return (ESRCH);
			_PHOLD(p);
			PROC_UNLOCK(p);
			error = ttyhook_register(&sc->tp, p, ((int *)msg->data)[1],
			    &ngt_hook, sc);
			PRELE(p);
			if (error != 0)
				return (error);
			break;
		case NGM_TTY_SET_HOTCHAR:
		    {
			int     hotchar;

			if (msg->header.arglen != sizeof(int))
				ERROUT(EINVAL);
			hotchar = *((int *) msg->data);
			if (hotchar != (u_char) hotchar && hotchar != -1)
				ERROUT(EINVAL);
			sc->hotchar = hotchar;	/* race condition is OK */
			break;
		    }
		case NGM_TTY_GET_HOTCHAR:
			NG_MKRESPONSE(resp, msg, sizeof(int), M_NOWAIT);
			if (!resp)
				ERROUT(ENOMEM);
			/* Race condition here is OK */
			*((int *) resp->data) = sc->hotchar;
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
 * Receive incoming data from netgraph system. Put it on our
 * output queue and start output if necessary.
 */
static int
ngt_rcvdata(hook_p hook, item_p item)
{
	const sc_p sc = NG_NODE_PRIVATE(NG_HOOK_NODE(hook));
	struct tty *tp = sc->tp;
	struct mbuf *m;

	if (hook != sc->hook)
		panic("%s", __func__);

	NGI_GET_M(item, m);
	NG_FREE_ITEM(item);

	if (tp == NULL) {
		NG_FREE_M(m);
		return (ENXIO);
	}

	IF_LOCK(&sc->outq);
	if (_IF_QFULL(&sc->outq)) {
		IF_UNLOCK(&sc->outq);
		NG_FREE_M(m);
		return (ENOBUFS);
	}

	_IF_ENQUEUE(&sc->outq, m);
	sc->outqlen += m->m_pkthdr.len;
	IF_UNLOCK(&sc->outq);

	/* notify the TTY that data is ready */
	tty_lock(tp);
	if (!tty_gone(tp))
		ttydevsw_outwakeup(tp);
	tty_unlock(tp);

	return (0);
}

static size_t
ngt_getc_inject(struct tty *tp, void *buf, size_t len)
{
	sc_p sc = ttyhook_softc(tp);
	size_t total = 0;
	int length;

	while (len) {
		struct mbuf *m;

		/* Remove first mbuf from queue */
		IF_DEQUEUE(&sc->outq, m);
		if (m == NULL)
			break;

		/* Send as much of it as possible */
		while (m != NULL) {
			length = min(m->m_len, len);
			memcpy((char *)buf + total, mtod(m, char *), length);

			m->m_data += length;
			m->m_len -= length;
			total += length;
			len -= length;

			if (m->m_len > 0)
				break;	/* device can't take any more */
			m = m_free(m);
		}

		/* Put remainder of mbuf chain (if any) back on queue */
		if (m != NULL) {
			IF_PREPEND(&sc->outq, m);
			break;
		}
	}
	IF_LOCK(&sc->outq);
	sc->outqlen -= total;
	IF_UNLOCK(&sc->outq);
	MPASS(sc->outqlen >= 0);

	return (total);
}

static size_t
ngt_getc_poll(struct tty *tp)
{
	sc_p sc = ttyhook_softc(tp);

	return (sc->outqlen);
}

/*
 * Optimised TTY input.
 *
 * We get a buffer pointer to hopefully a complete data frame. Do not check for
 * the hotchar, just pass it on.
 */
static size_t
ngt_rint_bypass(struct tty *tp, const void *buf, size_t len)
{
	sc_p sc = ttyhook_softc(tp);
	node_p node = sc->node;
	struct mbuf *m, *mb;
	size_t total = 0;
	int error = 0, length;

	tty_lock_assert(tp, MA_OWNED);

	if (sc->hook == NULL)
		return (0);

	m = m_getm2(NULL, len, M_NOWAIT, MT_DATA, M_PKTHDR);
	if (m == NULL) {
		if (sc->flags & FLG_DEBUG)
			log(LOG_ERR,
			    "%s: can't get mbuf\n", NG_NODE_NAME(node));
		return (0);
	}
	m->m_pkthdr.rcvif = NULL;

	for (mb = m; mb != NULL; mb = mb->m_next) {
		length = min(M_TRAILINGSPACE(mb), len - total);

		memcpy(mtod(m, char *), (const char *)buf + total, length);
		mb->m_len = length;
		total += length;
		m->m_pkthdr.len += length;
	}
	if (sc->m != NULL) {
		/*
		 * Odd, we have changed from non-bypass to bypass. It is
		 * unlikely but not impossible, flush the data first.
		 */
		sc->m->m_data = sc->m->m_pktdat;
		NG_SEND_DATA_ONLY(error, sc->hook, sc->m);
		sc->m = NULL;
	}
	NG_SEND_DATA_ONLY(error, sc->hook, m);

	return (total);
}

/*
 * Receive data coming from the device one char at a time, when it is not in
 * bypass mode.
 */
static int
ngt_rint(struct tty *tp, char c, int flags)
{
	sc_p sc = ttyhook_softc(tp);
	node_p node = sc->node;
	struct mbuf *m;
	int error = 0;

	tty_lock_assert(tp, MA_OWNED);

	if (sc->hook == NULL)
		return (0);

	if (flags != 0) {
		/* framing error or overrun on this char */
		if (sc->flags & FLG_DEBUG)
			log(LOG_DEBUG, "%s: line error %x\n",
			    NG_NODE_NAME(node), flags);
		return (0);
	}

	/* Get a new header mbuf if we need one */
	if (!(m = sc->m)) {
		MGETHDR(m, M_NOWAIT, MT_DATA);
		if (!m) {
			if (sc->flags & FLG_DEBUG)
				log(LOG_ERR,
				    "%s: can't get mbuf\n", NG_NODE_NAME(node));
			return (ENOBUFS);
		}
		m->m_len = m->m_pkthdr.len = 0;
		m->m_pkthdr.rcvif = NULL;
		sc->m = m;
	}

	/* Add char to mbuf */
	*mtod(m, u_char *) = c;
	m->m_data++;
	m->m_len++;
	m->m_pkthdr.len++;

	/* Ship off mbuf if it's time */
	if (sc->hotchar == -1 || c == sc->hotchar || m->m_len >= MHLEN) {
		m->m_data = m->m_pktdat;
		sc->m = NULL;
		NG_SEND_DATA_ONLY(error, sc->hook, m);	/* Will queue */
	}

	return (error);
}

static size_t
ngt_rint_poll(struct tty *tp)
{
	/* We can always accept input */
	return (1);
}

