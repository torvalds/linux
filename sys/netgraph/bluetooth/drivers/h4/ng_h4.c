/*
 * ng_h4.c
 */

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001-2002 Maksim Yevmenkin <m_evmenkin@yahoo.com>
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: ng_h4.c,v 1.10 2005/10/31 17:57:43 max Exp $
 * $FreeBSD$
 * 
 * Based on:
 * ---------
 *
 * FreeBSD: src/sys/netgraph/ng_tty.c
 * Author: Archie Cobbs <archie@freebsd.org>
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/conf.h>
#include <sys/endian.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/ioccom.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/priv.h>
#include <sys/socket.h>
#include <sys/tty.h>
#include <sys/ttycom.h>
#include <net/if.h>
#include <net/if_var.h>
#include <netgraph/ng_message.h>
#include <netgraph/netgraph.h>
#include <netgraph/ng_parse.h>
#include <netgraph/bluetooth/include/ng_bluetooth.h>
#include <netgraph/bluetooth/include/ng_hci.h>
#include <netgraph/bluetooth/include/ng_h4.h>
#include <netgraph/bluetooth/drivers/h4/ng_h4_var.h>
#include <netgraph/bluetooth/drivers/h4/ng_h4_prse.h>

/*****************************************************************************
 *****************************************************************************
 ** This node implements a Bluetooth HCI UART transport layer as per chapter
 ** H4 of the Bluetooth Specification Book v1.1. It is a terminal line 
 ** discipline that is also a netgraph node. Installing this line discipline 
 ** on a terminal device instantiates a new netgraph node of this type, which 
 ** allows access to the device via the "hook" hook of the node.
 **
 ** Once the line discipline is installed, you can find out the name of the 
 ** corresponding netgraph node via a NGIOCGINFO ioctl().
 *****************************************************************************
 *****************************************************************************/

/* MALLOC define */
#ifndef NG_SEPARATE_MALLOC
MALLOC_DEFINE(M_NETGRAPH_H4, "netgraph_h4", "Netgraph Bluetooth H4 node");
#else
#define M_NETGRAPH_H4 M_NETGRAPH
#endif /* NG_SEPARATE_MALLOC */

/* Line discipline methods */
static int	ng_h4_open	(struct cdev *, struct tty *);
static int	ng_h4_close	(struct tty *, int);
static int	ng_h4_read	(struct tty *, struct uio *, int);
static int	ng_h4_write	(struct tty *, struct uio *, int);
static int	ng_h4_input	(int, struct tty *);
static int	ng_h4_start	(struct tty *);
static int	ng_h4_ioctl	(struct tty *, u_long, caddr_t, 
					int, struct thread *);

/* Line discipline descriptor */
static struct linesw		ng_h4_disc = {
	ng_h4_open,		/* open */
	ng_h4_close,		/* close */
	ng_h4_read,		/* read */
	ng_h4_write,		/* write */
	ng_h4_ioctl,		/* ioctl */
	ng_h4_input,		/* input */
	ng_h4_start,		/* start */
	ttymodem		/* modem */
};

/* Netgraph methods */
static ng_constructor_t		ng_h4_constructor;
static ng_rcvmsg_t		ng_h4_rcvmsg;
static ng_shutdown_t		ng_h4_shutdown;
static ng_newhook_t		ng_h4_newhook;
static ng_connect_t		ng_h4_connect;
static ng_rcvdata_t		ng_h4_rcvdata;
static ng_disconnect_t		ng_h4_disconnect;

/* Other stuff */
static void	ng_h4_process_timeout	(node_p, hook_p, void *, int);
static int	ng_h4_mod_event		(module_t, int, void *);

/* Netgraph node type descriptor */
static struct ng_type		typestruct = {
	.version =	NG_ABI_VERSION,
	.name =		NG_H4_NODE_TYPE,
	.mod_event =	ng_h4_mod_event,
	.constructor =	ng_h4_constructor,
	.rcvmsg =	ng_h4_rcvmsg,
	.shutdown =	ng_h4_shutdown,
	.newhook =	ng_h4_newhook,
	.connect =	ng_h4_connect,
	.rcvdata =	ng_h4_rcvdata,
	.disconnect =	ng_h4_disconnect,
	.cmdlist =	ng_h4_cmdlist
};
NETGRAPH_INIT(h4, &typestruct);
MODULE_VERSION(ng_h4, NG_BLUETOOTH_VERSION);

static int	ng_h4_node = 0;

/*****************************************************************************
 *****************************************************************************
 **			    Line discipline methods
 *****************************************************************************
 *****************************************************************************/

/*
 * Set our line discipline on the tty.
 */

static int
ng_h4_open(struct cdev *dev, struct tty *tp)
{
	struct thread	*td = curthread;
	char		 name[NG_NODESIZ];
	ng_h4_info_p	 sc = NULL;
	int		 error;

	/* Super-user only */
	error = priv_check(td, PRIV_NETGRAPH_TTY); /* XXX */
	if (error != 0)
		return (error);

	/* Initialize private struct */
	sc = malloc(sizeof(*sc), M_NETGRAPH_H4, M_NOWAIT|M_ZERO);
	if (sc == NULL)
		return (ENOMEM);

	sc->tp = tp;
	sc->debug = NG_H4_WARN_LEVEL;

	sc->state = NG_H4_W4_PKT_IND;
	sc->want = 1;
	sc->got = 0;

	mtx_init(&sc->outq.ifq_mtx, "ng_h4 node+queue", NULL, MTX_DEF);
	IFQ_SET_MAXLEN(&sc->outq, NG_H4_DEFAULTQLEN);
	ng_callout_init(&sc->timo);

	NG_H4_LOCK(sc);

	/* Setup netgraph node */
	error = ng_make_node_common(&typestruct, &sc->node);
	if (error != 0) {
		NG_H4_UNLOCK(sc);

		printf("%s: Unable to create new node!\n", __func__);

		mtx_destroy(&sc->outq.ifq_mtx);
		bzero(sc, sizeof(*sc));
		free(sc, M_NETGRAPH_H4);

		return (error);
	}

	/* Assign node its name */
	snprintf(name, sizeof(name), "%s%d", typestruct.name, ng_h4_node ++);

	error = ng_name_node(sc->node, name);
	if (error != 0) {
		NG_H4_UNLOCK(sc);

		printf("%s: %s - node name exists?\n", __func__, name);

		NG_NODE_UNREF(sc->node);
		mtx_destroy(&sc->outq.ifq_mtx);
		bzero(sc, sizeof(*sc));
		free(sc, M_NETGRAPH_H4);

		return (error);
	}

	/* Set back pointers */
	NG_NODE_SET_PRIVATE(sc->node, sc);
	tp->t_lsc = (caddr_t) sc;

	/* The node has to be a WRITER because data can change node status */
	NG_NODE_FORCE_WRITER(sc->node);

	/*
	 * Pre-allocate cblocks to the an appropriate amount.
	 * I'm not sure what is appropriate.
	 */

	ttyflush(tp, FREAD | FWRITE);
	clist_alloc_cblocks(&tp->t_canq, 0, 0);
	clist_alloc_cblocks(&tp->t_rawq, 0, 0);
	clist_alloc_cblocks(&tp->t_outq,
		MLEN + NG_H4_HIWATER, MLEN + NG_H4_HIWATER);

	NG_H4_UNLOCK(sc);

	return (error);
} /* ng_h4_open */

/*
 * Line specific close routine, called from device close routine
 * and from ttioctl. This causes the node to be destroyed as well.
 */

static int
ng_h4_close(struct tty *tp, int flag)
{
	ng_h4_info_p	sc = (ng_h4_info_p) tp->t_lsc;

	ttyflush(tp, FREAD | FWRITE);
	clist_free_cblocks(&tp->t_outq);

	if (sc != NULL) {
		NG_H4_LOCK(sc);

		if (callout_pending(&sc->timo))
			ng_uncallout(&sc->timo, sc->node);

		tp->t_lsc = NULL;
		sc->dying = 1;

		NG_H4_UNLOCK(sc);

		ng_rmnode_self(sc->node);
	}

	return (0);
} /* ng_h4_close */

/*
 * Once the device has been turned into a node, we don't allow reading.
 */

static int
ng_h4_read(struct tty *tp, struct uio *uio, int flag)
{
	return (EIO);
} /* ng_h4_read */

/*
 * Once the device has been turned into a node, we don't allow writing.
 */

static int
ng_h4_write(struct tty *tp, struct uio *uio, int flag)
{
	return (EIO);
} /* ng_h4_write */

/*
 * We implement the NGIOCGINFO ioctl() defined in ng_message.h.
 */

static int
ng_h4_ioctl(struct tty *tp, u_long cmd, caddr_t data, int flag,
		struct thread *td)
{
	ng_h4_info_p	sc = (ng_h4_info_p) tp->t_lsc;
	int		error = 0;

	if (sc == NULL)
		return (ENXIO);

	NG_H4_LOCK(sc);

	switch (cmd) {
	case NGIOCGINFO:
#undef	NI
#define NI(x)	((struct nodeinfo *)(x))

		bzero(data, sizeof(*NI(data)));

		if (NG_NODE_HAS_NAME(sc->node))
			strncpy(NI(data)->name, NG_NODE_NAME(sc->node), 
				sizeof(NI(data)->name) - 1);

		strncpy(NI(data)->type, sc->node->nd_type->name, 
			sizeof(NI(data)->type) - 1);

		NI(data)->id = (u_int32_t) ng_node2ID(sc->node);
		NI(data)->hooks = NG_NODE_NUMHOOKS(sc->node);
		break;

	default:
		error = ENOIOCTL;
		break;
	}

	NG_H4_UNLOCK(sc);

	return (error);
} /* ng_h4_ioctl */

/*
 * Receive data coming from the device. We get one character at a time, which 
 * is kindof silly.
 */

static int
ng_h4_input(int c, struct tty *tp)
{
	ng_h4_info_p	sc = (ng_h4_info_p) tp->t_lsc;

	if (sc == NULL || tp != sc->tp ||
	    sc->node == NULL || NG_NODE_NOT_VALID(sc->node))
		return (0);

	NG_H4_LOCK(sc);

	/* Check for error conditions */
	if ((tp->t_state & TS_CONNECTED) == 0) {
		NG_H4_INFO("%s: %s - no carrier\n", __func__,
			NG_NODE_NAME(sc->node));

		sc->state = NG_H4_W4_PKT_IND;
		sc->want = 1;
		sc->got = 0;

		NG_H4_UNLOCK(sc);

		return (0); /* XXX Loss of synchronization here! */
	}

	/* Check for framing error or overrun on this char */
	if (c & TTY_ERRORMASK) {
		NG_H4_ERR("%s: %s - line error %#x, c=%#x\n", __func__, 
			NG_NODE_NAME(sc->node), c & TTY_ERRORMASK,
			c & TTY_CHARMASK);

		NG_H4_STAT_IERROR(sc->stat);

		sc->state = NG_H4_W4_PKT_IND;
		sc->want = 1;
		sc->got = 0;

		NG_H4_UNLOCK(sc);

		return (0); /* XXX Loss of synchronization here! */
	}

	NG_H4_STAT_BYTES_RECV(sc->stat, 1);

	/* Append char to mbuf */
	if (sc->got >= sizeof(sc->ibuf)) {
		NG_H4_ALERT("%s: %s - input buffer overflow, c=%#x, got=%d\n",
			__func__, NG_NODE_NAME(sc->node), c & TTY_CHARMASK,
			sc->got);

		NG_H4_STAT_IERROR(sc->stat);

		sc->state = NG_H4_W4_PKT_IND;
		sc->want = 1;
		sc->got = 0;

		NG_H4_UNLOCK(sc);

		return (0); /* XXX Loss of synchronization here! */
	}

	sc->ibuf[sc->got ++] = (c & TTY_CHARMASK);

	NG_H4_INFO("%s: %s - got char %#x, want=%d, got=%d\n", __func__,
		NG_NODE_NAME(sc->node), c, sc->want, sc->got);

	if (sc->got < sc->want) {
		NG_H4_UNLOCK(sc);

		return (0); /* Wait for more */
	}

	switch (sc->state) {
	/* Got packet indicator */
	case NG_H4_W4_PKT_IND:
		NG_H4_INFO("%s: %s - got packet indicator %#x\n", __func__,
			NG_NODE_NAME(sc->node), sc->ibuf[0]);

		sc->state = NG_H4_W4_PKT_HDR;

		/*
		 * Since packet indicator included in the packet header
		 * just set sc->want to sizeof(packet header).
		 */

		switch (sc->ibuf[0]) {
		case NG_HCI_ACL_DATA_PKT:
			sc->want = sizeof(ng_hci_acldata_pkt_t);
			break;

		case NG_HCI_SCO_DATA_PKT:
			sc->want = sizeof(ng_hci_scodata_pkt_t);
			break;

		case NG_HCI_EVENT_PKT:
			sc->want = sizeof(ng_hci_event_pkt_t);
			break;

		default:
			NG_H4_WARN("%s: %s - ignoring unknown packet " \
				"type=%#x\n", __func__, NG_NODE_NAME(sc->node),
				sc->ibuf[0]);

			NG_H4_STAT_IERROR(sc->stat);

			sc->state = NG_H4_W4_PKT_IND;
			sc->want = 1;
			sc->got = 0;
			break;
		}
		break;

	/* Got packet header */
	case NG_H4_W4_PKT_HDR:
		sc->state = NG_H4_W4_PKT_DATA;

		switch (sc->ibuf[0]) {
		case NG_HCI_ACL_DATA_PKT:
			c = le16toh(((ng_hci_acldata_pkt_t *)
				(sc->ibuf))->length);
			break;

		case NG_HCI_SCO_DATA_PKT:
			c = ((ng_hci_scodata_pkt_t *)(sc->ibuf))->length;
			break;

		case NG_HCI_EVENT_PKT:
			c = ((ng_hci_event_pkt_t *)(sc->ibuf))->length;
			break;

		default:
			KASSERT((0), ("Invalid packet type=%#x\n",
				sc->ibuf[0]));
			break;
		}

		NG_H4_INFO("%s: %s - got packet header, packet type=%#x, " \
			"packet size=%d, payload size=%d\n", __func__, 
			NG_NODE_NAME(sc->node), sc->ibuf[0], sc->got, c);

		if (c > 0) {
			sc->want += c;

			/* 
			 * Try to prevent possible buffer overrun
			 *
			 * XXX I'm *really* confused here. It turns out
			 * that Xircom card sends us packets with length
			 * greater then 512 bytes! This is greater then
			 * our old receive buffer (ibuf) size. In the same
			 * time the card demands from us *not* to send 
			 * packets greater then 192 bytes. Weird! How the 
			 * hell i should know how big *receive* buffer 
			 * should be? For now increase receiving buffer 
			 * size to 1K and add the following check.
			 */

			if (sc->want >= sizeof(sc->ibuf)) {
				int	b;

				NG_H4_ALERT("%s: %s - packet too big for " \
					"buffer, type=%#x, got=%d, want=%d, " \
					"length=%d\n", __func__, 
					NG_NODE_NAME(sc->node), sc->ibuf[0],
					sc->got, sc->want, c);

				NG_H4_ALERT("Packet header:\n");
				for (b = 0; b < sc->got; b++)
					NG_H4_ALERT("%#x ", sc->ibuf[b]);
				NG_H4_ALERT("\n");

				/* Reset state */
				NG_H4_STAT_IERROR(sc->stat);

				sc->state = NG_H4_W4_PKT_IND;
				sc->want = 1;
				sc->got = 0;
			}

			break;
		}

		/* else FALLTHROUGH and deliver frame */
		/* XXX Is this true? Should we deliver empty frame? */

	/* Got packet data */
	case NG_H4_W4_PKT_DATA:
		NG_H4_INFO("%s: %s - got full packet, packet type=%#x, " \
			"packet size=%d\n", __func__,
			NG_NODE_NAME(sc->node), sc->ibuf[0], sc->got);

		if (sc->hook != NULL && NG_HOOK_IS_VALID(sc->hook)) {
			struct mbuf	*m = NULL;

			MGETHDR(m, M_NOWAIT, MT_DATA);
			if (m != NULL) {
				m->m_pkthdr.len = 0;

				/* XXX m_copyback() is stupid */
				m->m_len = min(MHLEN, sc->got);

				m_copyback(m, 0, sc->got, sc->ibuf);
				NG_SEND_DATA_ONLY(c, sc->hook, m);
			} else {
				NG_H4_ERR("%s: %s - could not get mbuf\n",
					__func__, NG_NODE_NAME(sc->node));

				NG_H4_STAT_IERROR(sc->stat);
			}
		}

		sc->state = NG_H4_W4_PKT_IND;
		sc->want = 1;
		sc->got = 0;

		NG_H4_STAT_PCKTS_RECV(sc->stat);
		break;

	default:
		KASSERT((0), ("Invalid H4 node state=%d", sc->state));
		break;
	}

	NG_H4_UNLOCK(sc);

	return (0);
} /* ng_h4_input */

/*
 * This is called when the device driver is ready for more output. Called from 
 * tty system. 
 */

static int
ng_h4_start(struct tty *tp)
{
	ng_h4_info_p	 sc = (ng_h4_info_p) tp->t_lsc;
	struct mbuf	*m = NULL;
	int		 size;

	if (sc == NULL || tp != sc->tp || 
	    sc->node == NULL || NG_NODE_NOT_VALID(sc->node))
		return (0);

#if 0
	while (tp->t_outq.c_cc < NG_H4_HIWATER) { /* XXX 2.2 specific ? */
#else
	while (1) {
#endif
		/* Remove first mbuf from queue */
		IF_DEQUEUE(&sc->outq, m);
		if (m == NULL)
			break;

		/* Send as much of it as possible */
		while (m != NULL) {
			size = m->m_len - b_to_q(mtod(m, u_char *),
					m->m_len, &tp->t_outq);

			NG_H4_LOCK(sc);
			NG_H4_STAT_BYTES_SENT(sc->stat, size);
			NG_H4_UNLOCK(sc);

			m->m_data += size;
			m->m_len -= size;
			if (m->m_len > 0)
				break;	/* device can't take no more */

			m = m_free(m);
		}

		/* Put remainder of mbuf chain (if any) back on queue */
		if (m != NULL) {
			IF_PREPEND(&sc->outq, m);
			break;
		}

		/* Full packet has been sent */
		NG_H4_LOCK(sc);
		NG_H4_STAT_PCKTS_SENT(sc->stat);
		NG_H4_UNLOCK(sc);
	}

	/* 
	 * Call output process whether or not there is any output. We are
	 * being called in lieu of ttstart and must do what it would.
	 */

	tt_oproc(sc->tp);

	/*
	 * This timeout is needed for operation on a pseudo-tty, because the
	 * pty code doesn't call pppstart after it has drained the t_outq.
	 */

	NG_H4_LOCK(sc);

	if (!IFQ_IS_EMPTY(&sc->outq) && !callout_pending(&sc->timo))
		ng_callout(&sc->timo, sc->node, NULL, 1,
			ng_h4_process_timeout, NULL, 0);

	NG_H4_UNLOCK(sc);

	return (0);
} /* ng_h4_start */

/*****************************************************************************
 *****************************************************************************
 **			    Netgraph node methods
 *****************************************************************************
 *****************************************************************************/

/*
 * Initialize a new node of this type. We only allow nodes to be created as 
 * a result of setting the line discipline on a tty, so always return an error
 * if not.
 */

static int
ng_h4_constructor(node_p node)
{
	return (EOPNOTSUPP);
} /* ng_h4_constructor */

/*
 * Add a new hook. There can only be one.
 */

static int
ng_h4_newhook(node_p node, hook_p hook, const char *name)
{
	ng_h4_info_p	sc = (ng_h4_info_p) NG_NODE_PRIVATE(node);

	if (strcmp(name, NG_H4_HOOK) != 0)
		return (EINVAL);

	NG_H4_LOCK(sc);

	if (sc->hook != NULL) {
		NG_H4_UNLOCK(sc);
		return (EISCONN);
	}
	sc->hook = hook;

	NG_H4_UNLOCK(sc);

	return (0);
} /* ng_h4_newhook */

/*
 * Connect hook. Just say yes.
 */

static int
ng_h4_connect(hook_p hook)
{
	ng_h4_info_p	sc = (ng_h4_info_p) NG_NODE_PRIVATE(NG_HOOK_NODE(hook));

	if (hook != sc->hook)
		panic("%s: hook != sc->hook\n", __func__);

	NG_HOOK_FORCE_QUEUE(NG_HOOK_PEER(hook));
	NG_HOOK_FORCE_QUEUE(hook);

	return (0);
} /* ng_h4_connect */

/*
 * Disconnect the hook
 */

static int
ng_h4_disconnect(hook_p hook)
{
	ng_h4_info_p	sc = (ng_h4_info_p) NG_NODE_PRIVATE(NG_HOOK_NODE(hook));

	/*
	 * We need to check for sc != NULL because we can be called from
	 * ng_h4_close() via ng_rmnode_self()
	 */

	if (sc != NULL) {
		if (hook != sc->hook)
			panic("%s: hook != sc->hook\n", __func__);

		NG_H4_LOCK(sc);

		/* XXX do we have to untimeout and drain out queue? */
		if (callout_pending(&sc->timo))
			ng_uncallout(&sc->timo, sc->node);

		_IF_DRAIN(&sc->outq); 

		sc->state = NG_H4_W4_PKT_IND;
		sc->want = 1;
		sc->got = 0;

		sc->hook = NULL;

		NG_H4_UNLOCK(sc);
	}

	return (0);
} /* ng_h4_disconnect */

/*
 * Remove this node. The does the netgraph portion of the shutdown.
 * This should only be called indirectly from ng_h4_close().
 */

static int
ng_h4_shutdown(node_p node)
{
	ng_h4_info_p	sc = (ng_h4_info_p) NG_NODE_PRIVATE(node);

	NG_H4_LOCK(sc);

	if (!sc->dying) {
		NG_H4_UNLOCK(sc);

		NG_NODE_REVIVE(node);	/* we will persist */

		return (EOPNOTSUPP);
	}

	NG_H4_UNLOCK(sc);

	NG_NODE_SET_PRIVATE(node, NULL);

	_IF_DRAIN(&sc->outq);

	NG_NODE_UNREF(node);
	mtx_destroy(&sc->outq.ifq_mtx);
	bzero(sc, sizeof(*sc));
	free(sc, M_NETGRAPH_H4);

	return (0);
} /* ng_h4_shutdown */

/*
 * Receive incoming data from Netgraph system. Put it on our
 * output queue and start output if necessary.
 */

static int
ng_h4_rcvdata(hook_p hook, item_p item)
{
	ng_h4_info_p	 sc = (ng_h4_info_p)NG_NODE_PRIVATE(NG_HOOK_NODE(hook));
	struct mbuf	*m = NULL;
	int		 qlen;

	if (sc == NULL)
		return (EHOSTDOWN);

	if (hook != sc->hook)
		panic("%s: hook != sc->hook\n", __func__);

	NGI_GET_M(item, m);
	NG_FREE_ITEM(item);

	NG_H4_LOCK(sc);

	if (_IF_QFULL(&sc->outq)) {
		NG_H4_ERR("%s: %s - dropping mbuf, len=%d\n", __func__,
			NG_NODE_NAME(sc->node), m->m_pkthdr.len);

		NG_H4_STAT_OERROR(sc->stat);

		NG_H4_UNLOCK(sc);

		NG_FREE_M(m);

		return (ENOBUFS);
	}

	NG_H4_INFO("%s: %s - queue mbuf, len=%d\n", __func__,
		NG_NODE_NAME(sc->node), m->m_pkthdr.len);

	_IF_ENQUEUE(&sc->outq, m);
	qlen = _IF_QLEN(&sc->outq);

	NG_H4_UNLOCK(sc);

	/*
	 * If qlen > 1, then we should already have a scheduled callout
	 */

	if (qlen == 1) {
		mtx_lock(&Giant);
		ng_h4_start(sc->tp);
		mtx_unlock(&Giant);
	}

	return (0);
} /* ng_h4_rcvdata */

/*
 * Receive control message
 */

static int
ng_h4_rcvmsg(node_p node, item_p item, hook_p lasthook)
{
	ng_h4_info_p	 sc = (ng_h4_info_p) NG_NODE_PRIVATE(node);
	struct ng_mesg	*msg = NULL, *resp = NULL;
	int		 error = 0;

	if (sc == NULL)
		return (EHOSTDOWN);

	NGI_GET_MSG(item, msg);
	NG_H4_LOCK(sc);

	switch (msg->header.typecookie) {
	case NGM_GENERIC_COOKIE:
		switch (msg->header.cmd) {
		case NGM_TEXT_STATUS:
			NG_MKRESPONSE(resp, msg, NG_TEXTRESPONSE, M_NOWAIT);
			if (resp == NULL)
				error = ENOMEM;
			else
				snprintf(resp->data, NG_TEXTRESPONSE,
					"Hook: %s\n"   \
					"Debug: %d\n"  \
					"State: %d\n"  \
					"Queue: [have:%d,max:%d]\n" \
					"Input: [got:%d,want:%d]",
					(sc->hook != NULL)? NG_H4_HOOK : "",
					sc->debug,
					sc->state,
					_IF_QLEN(&sc->outq),
					sc->outq.ifq_maxlen,
					sc->got,
					sc->want);
			break;

		default:
			error = EINVAL;
			break;
		}
		break;

	case NGM_H4_COOKIE:
		switch (msg->header.cmd) {
		case NGM_H4_NODE_RESET:
			_IF_DRAIN(&sc->outq); 
			sc->state = NG_H4_W4_PKT_IND;
			sc->want = 1;
			sc->got = 0;
			break;

		case NGM_H4_NODE_GET_STATE:
			NG_MKRESPONSE(resp, msg, sizeof(ng_h4_node_state_ep),
				M_NOWAIT);
			if (resp == NULL)
				error = ENOMEM;
			else
				*((ng_h4_node_state_ep *)(resp->data)) = 
					sc->state;
			break;

		case NGM_H4_NODE_GET_DEBUG:
			NG_MKRESPONSE(resp, msg, sizeof(ng_h4_node_debug_ep),
				M_NOWAIT);
			if (resp == NULL)
				error = ENOMEM;
			else
				*((ng_h4_node_debug_ep *)(resp->data)) = 
					sc->debug;
			break;

		case NGM_H4_NODE_SET_DEBUG:
			if (msg->header.arglen != sizeof(ng_h4_node_debug_ep))
				error = EMSGSIZE;
			else
				sc->debug =
					*((ng_h4_node_debug_ep *)(msg->data));
			break;

		case NGM_H4_NODE_GET_QLEN:
			NG_MKRESPONSE(resp, msg, sizeof(ng_h4_node_qlen_ep),
				M_NOWAIT);
			if (resp == NULL)
				error = ENOMEM;
			else
				*((ng_h4_node_qlen_ep *)(resp->data)) = 
					sc->outq.ifq_maxlen;
			break;

		case NGM_H4_NODE_SET_QLEN:
			if (msg->header.arglen != sizeof(ng_h4_node_qlen_ep))
				error = EMSGSIZE;
			else if (*((ng_h4_node_qlen_ep *)(msg->data)) <= 0)
				error = EINVAL;
			else
				IFQ_SET_MAXLEN(&sc->outq,
					*((ng_h4_node_qlen_ep *)(msg->data)));
			break;

		case NGM_H4_NODE_GET_STAT:
			NG_MKRESPONSE(resp, msg, sizeof(ng_h4_node_stat_ep),
				M_NOWAIT);
			if (resp == NULL)
				error = ENOMEM;
			else
				bcopy(&sc->stat, resp->data,
					sizeof(ng_h4_node_stat_ep));
			break;

		case NGM_H4_NODE_RESET_STAT:
			NG_H4_STAT_RESET(sc->stat);
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

	NG_H4_UNLOCK(sc);

	NG_RESPOND_MSG(error, node, item, resp);
	NG_FREE_MSG(msg);

	return (error);
} /* ng_h4_rcvmsg */

/*
 * Timeout processing function.
 * We still have data to output to the device, so try sending more.
 */

static void
ng_h4_process_timeout(node_p node, hook_p hook, void *arg1, int arg2)
{
	ng_h4_info_p	sc = (ng_h4_info_p) NG_NODE_PRIVATE(node);

	mtx_lock(&Giant);
	ng_h4_start(sc->tp);
	mtx_unlock(&Giant);
} /* ng_h4_process_timeout */

/*
 * Handle loading and unloading for this node type
 */

static int
ng_h4_mod_event(module_t mod, int event, void *data)
{
	static int	ng_h4_ldisc;
	int		error = 0;

	switch (event) {
	case MOD_LOAD:
		/* Register line discipline */
		mtx_lock(&Giant);
		ng_h4_ldisc = ldisc_register(H4DISC, &ng_h4_disc);
		mtx_unlock(&Giant);

		if (ng_h4_ldisc < 0) {
			printf("%s: can't register H4 line discipline\n",
				__func__);
			error = EIO;
		}
		break;

	case MOD_UNLOAD:
		/* Unregister line discipline */
		mtx_lock(&Giant);
		ldisc_deregister(ng_h4_ldisc);
		mtx_unlock(&Giant);
		break;

	default:
		error = EOPNOTSUPP;
		break;
	}

	return (error);
} /* ng_h4_mod_event */

