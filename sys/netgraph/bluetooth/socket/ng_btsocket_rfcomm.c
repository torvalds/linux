/*
 * ng_btsocket_rfcomm.c
 */

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001-2003 Maksim Yevmenkin <m_evmenkin@yahoo.com>
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
 * $Id: ng_btsocket_rfcomm.c,v 1.28 2003/09/14 23:29:06 max Exp $
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bitstring.h>
#include <sys/domain.h>
#include <sys/endian.h>
#include <sys/errno.h>
#include <sys/filedesc.h>
#include <sys/ioccom.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/protosw.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>
#include <sys/taskqueue.h>
#include <sys/uio.h>

#include <net/vnet.h>

#include <netgraph/ng_message.h>
#include <netgraph/netgraph.h>
#include <netgraph/bluetooth/include/ng_bluetooth.h>
#include <netgraph/bluetooth/include/ng_hci.h>
#include <netgraph/bluetooth/include/ng_l2cap.h>
#include <netgraph/bluetooth/include/ng_btsocket.h>
#include <netgraph/bluetooth/include/ng_btsocket_l2cap.h>
#include <netgraph/bluetooth/include/ng_btsocket_rfcomm.h>

/* MALLOC define */
#ifdef NG_SEPARATE_MALLOC
static MALLOC_DEFINE(M_NETGRAPH_BTSOCKET_RFCOMM, "netgraph_btsocks_rfcomm",
		"Netgraph Bluetooth RFCOMM sockets");
#else
#define M_NETGRAPH_BTSOCKET_RFCOMM M_NETGRAPH
#endif /* NG_SEPARATE_MALLOC */

/* Debug */
#define NG_BTSOCKET_RFCOMM_INFO \
	if (ng_btsocket_rfcomm_debug_level >= NG_BTSOCKET_INFO_LEVEL && \
	    ppsratecheck(&ng_btsocket_rfcomm_lasttime, &ng_btsocket_rfcomm_curpps, 1)) \
		printf

#define NG_BTSOCKET_RFCOMM_WARN \
	if (ng_btsocket_rfcomm_debug_level >= NG_BTSOCKET_WARN_LEVEL && \
	    ppsratecheck(&ng_btsocket_rfcomm_lasttime, &ng_btsocket_rfcomm_curpps, 1)) \
		printf

#define NG_BTSOCKET_RFCOMM_ERR \
	if (ng_btsocket_rfcomm_debug_level >= NG_BTSOCKET_ERR_LEVEL && \
	    ppsratecheck(&ng_btsocket_rfcomm_lasttime, &ng_btsocket_rfcomm_curpps, 1)) \
		printf

#define NG_BTSOCKET_RFCOMM_ALERT \
	if (ng_btsocket_rfcomm_debug_level >= NG_BTSOCKET_ALERT_LEVEL && \
	    ppsratecheck(&ng_btsocket_rfcomm_lasttime, &ng_btsocket_rfcomm_curpps, 1)) \
		printf

#define	ALOT	0x7fff

/* Local prototypes */
static int ng_btsocket_rfcomm_upcall
	(struct socket *so, void *arg, int waitflag);
static void ng_btsocket_rfcomm_sessions_task
	(void *ctx, int pending);
static void ng_btsocket_rfcomm_session_task
	(ng_btsocket_rfcomm_session_p s);
#define ng_btsocket_rfcomm_task_wakeup() \
	taskqueue_enqueue(taskqueue_swi_giant, &ng_btsocket_rfcomm_task)

static ng_btsocket_rfcomm_pcb_p ng_btsocket_rfcomm_connect_ind
	(ng_btsocket_rfcomm_session_p s, int channel);
static void ng_btsocket_rfcomm_connect_cfm
	(ng_btsocket_rfcomm_session_p s);

static int ng_btsocket_rfcomm_session_create
	(ng_btsocket_rfcomm_session_p *sp, struct socket *l2so,
	 bdaddr_p src, bdaddr_p dst, struct thread *td);
static int ng_btsocket_rfcomm_session_accept
	(ng_btsocket_rfcomm_session_p s0);
static int ng_btsocket_rfcomm_session_connect
	(ng_btsocket_rfcomm_session_p s);
static int ng_btsocket_rfcomm_session_receive
	(ng_btsocket_rfcomm_session_p s);
static int ng_btsocket_rfcomm_session_send
	(ng_btsocket_rfcomm_session_p s);
static void ng_btsocket_rfcomm_session_clean
	(ng_btsocket_rfcomm_session_p s);
static void ng_btsocket_rfcomm_session_process_pcb
	(ng_btsocket_rfcomm_session_p s);
static ng_btsocket_rfcomm_session_p ng_btsocket_rfcomm_session_by_addr
	(bdaddr_p src, bdaddr_p dst);

static int ng_btsocket_rfcomm_receive_frame
	(ng_btsocket_rfcomm_session_p s, struct mbuf *m0);
static int ng_btsocket_rfcomm_receive_sabm
	(ng_btsocket_rfcomm_session_p s, int dlci);
static int ng_btsocket_rfcomm_receive_disc
	(ng_btsocket_rfcomm_session_p s, int dlci);
static int ng_btsocket_rfcomm_receive_ua
	(ng_btsocket_rfcomm_session_p s, int dlci);
static int ng_btsocket_rfcomm_receive_dm
	(ng_btsocket_rfcomm_session_p s, int dlci);
static int ng_btsocket_rfcomm_receive_uih
	(ng_btsocket_rfcomm_session_p s, int dlci, int pf, struct mbuf *m0);
static int ng_btsocket_rfcomm_receive_mcc
	(ng_btsocket_rfcomm_session_p s, struct mbuf *m0);
static int ng_btsocket_rfcomm_receive_test
	(ng_btsocket_rfcomm_session_p s, struct mbuf *m0);
static int ng_btsocket_rfcomm_receive_fc
	(ng_btsocket_rfcomm_session_p s, struct mbuf *m0);
static int ng_btsocket_rfcomm_receive_msc
	(ng_btsocket_rfcomm_session_p s, struct mbuf *m0);
static int ng_btsocket_rfcomm_receive_rpn
	(ng_btsocket_rfcomm_session_p s, struct mbuf *m0);
static int ng_btsocket_rfcomm_receive_rls
	(ng_btsocket_rfcomm_session_p s, struct mbuf *m0);
static int ng_btsocket_rfcomm_receive_pn
	(ng_btsocket_rfcomm_session_p s, struct mbuf *m0);
static void ng_btsocket_rfcomm_set_pn
	(ng_btsocket_rfcomm_pcb_p pcb, u_int8_t cr, u_int8_t flow_control, 
	 u_int8_t credits, u_int16_t mtu);

static int ng_btsocket_rfcomm_send_command
	(ng_btsocket_rfcomm_session_p s, u_int8_t type, u_int8_t dlci);
static int ng_btsocket_rfcomm_send_uih
	(ng_btsocket_rfcomm_session_p s, u_int8_t address, u_int8_t pf, 
	 u_int8_t credits, struct mbuf *data);
static int ng_btsocket_rfcomm_send_msc
	(ng_btsocket_rfcomm_pcb_p pcb);
static int ng_btsocket_rfcomm_send_pn
	(ng_btsocket_rfcomm_pcb_p pcb);
static int ng_btsocket_rfcomm_send_credits
	(ng_btsocket_rfcomm_pcb_p pcb);

static int ng_btsocket_rfcomm_pcb_send
	(ng_btsocket_rfcomm_pcb_p pcb, int limit);
static void ng_btsocket_rfcomm_pcb_kill
	(ng_btsocket_rfcomm_pcb_p pcb, int error);
static ng_btsocket_rfcomm_pcb_p ng_btsocket_rfcomm_pcb_by_dlci
	(ng_btsocket_rfcomm_session_p s, int dlci);
static ng_btsocket_rfcomm_pcb_p ng_btsocket_rfcomm_pcb_listener
	(bdaddr_p src, int channel);

static void ng_btsocket_rfcomm_timeout
	(ng_btsocket_rfcomm_pcb_p pcb);
static void ng_btsocket_rfcomm_untimeout
	(ng_btsocket_rfcomm_pcb_p pcb);
static void ng_btsocket_rfcomm_process_timeout
	(void *xpcb);

static struct mbuf * ng_btsocket_rfcomm_prepare_packet
	(struct sockbuf *sb, int length);

/* Globals */
extern int					ifqmaxlen;
static u_int32_t				ng_btsocket_rfcomm_debug_level;
static u_int32_t				ng_btsocket_rfcomm_timo;
struct task					ng_btsocket_rfcomm_task;
static LIST_HEAD(, ng_btsocket_rfcomm_session)	ng_btsocket_rfcomm_sessions;
static struct mtx				ng_btsocket_rfcomm_sessions_mtx;
static LIST_HEAD(, ng_btsocket_rfcomm_pcb)	ng_btsocket_rfcomm_sockets;
static struct mtx				ng_btsocket_rfcomm_sockets_mtx;
static struct timeval				ng_btsocket_rfcomm_lasttime;
static int					ng_btsocket_rfcomm_curpps;

/* Sysctl tree */
SYSCTL_DECL(_net_bluetooth_rfcomm_sockets);
static SYSCTL_NODE(_net_bluetooth_rfcomm_sockets, OID_AUTO, stream, CTLFLAG_RW,
	0, "Bluetooth STREAM RFCOMM sockets family");
SYSCTL_UINT(_net_bluetooth_rfcomm_sockets_stream, OID_AUTO, debug_level,
	CTLFLAG_RW,
	&ng_btsocket_rfcomm_debug_level, NG_BTSOCKET_INFO_LEVEL,
	"Bluetooth STREAM RFCOMM sockets debug level");
SYSCTL_UINT(_net_bluetooth_rfcomm_sockets_stream, OID_AUTO, timeout,
	CTLFLAG_RW,
	&ng_btsocket_rfcomm_timo, 60,
	"Bluetooth STREAM RFCOMM sockets timeout");

/*****************************************************************************
 *****************************************************************************
 **                              RFCOMM CRC
 *****************************************************************************
 *****************************************************************************/

static u_int8_t	ng_btsocket_rfcomm_crc_table[256] = {
	0x00, 0x91, 0xe3, 0x72, 0x07, 0x96, 0xe4, 0x75,
	0x0e, 0x9f, 0xed, 0x7c, 0x09, 0x98, 0xea, 0x7b,
	0x1c, 0x8d, 0xff, 0x6e, 0x1b, 0x8a, 0xf8, 0x69,
	0x12, 0x83, 0xf1, 0x60, 0x15, 0x84, 0xf6, 0x67,

	0x38, 0xa9, 0xdb, 0x4a, 0x3f, 0xae, 0xdc, 0x4d,
	0x36, 0xa7, 0xd5, 0x44, 0x31, 0xa0, 0xd2, 0x43,
	0x24, 0xb5, 0xc7, 0x56, 0x23, 0xb2, 0xc0, 0x51,
	0x2a, 0xbb, 0xc9, 0x58, 0x2d, 0xbc, 0xce, 0x5f,

	0x70, 0xe1, 0x93, 0x02, 0x77, 0xe6, 0x94, 0x05,
	0x7e, 0xef, 0x9d, 0x0c, 0x79, 0xe8, 0x9a, 0x0b,
	0x6c, 0xfd, 0x8f, 0x1e, 0x6b, 0xfa, 0x88, 0x19,
	0x62, 0xf3, 0x81, 0x10, 0x65, 0xf4, 0x86, 0x17,

	0x48, 0xd9, 0xab, 0x3a, 0x4f, 0xde, 0xac, 0x3d,
	0x46, 0xd7, 0xa5, 0x34, 0x41, 0xd0, 0xa2, 0x33,
	0x54, 0xc5, 0xb7, 0x26, 0x53, 0xc2, 0xb0, 0x21,
	0x5a, 0xcb, 0xb9, 0x28, 0x5d, 0xcc, 0xbe, 0x2f,

	0xe0, 0x71, 0x03, 0x92, 0xe7, 0x76, 0x04, 0x95,
	0xee, 0x7f, 0x0d, 0x9c, 0xe9, 0x78, 0x0a, 0x9b,
	0xfc, 0x6d, 0x1f, 0x8e, 0xfb, 0x6a, 0x18, 0x89,
	0xf2, 0x63, 0x11, 0x80, 0xf5, 0x64, 0x16, 0x87,

	0xd8, 0x49, 0x3b, 0xaa, 0xdf, 0x4e, 0x3c, 0xad,
	0xd6, 0x47, 0x35, 0xa4, 0xd1, 0x40, 0x32, 0xa3,
	0xc4, 0x55, 0x27, 0xb6, 0xc3, 0x52, 0x20, 0xb1,
	0xca, 0x5b, 0x29, 0xb8, 0xcd, 0x5c, 0x2e, 0xbf,

	0x90, 0x01, 0x73, 0xe2, 0x97, 0x06, 0x74, 0xe5,
	0x9e, 0x0f, 0x7d, 0xec, 0x99, 0x08, 0x7a, 0xeb,
	0x8c, 0x1d, 0x6f, 0xfe, 0x8b, 0x1a, 0x68, 0xf9,
	0x82, 0x13, 0x61, 0xf0, 0x85, 0x14, 0x66, 0xf7,

	0xa8, 0x39, 0x4b, 0xda, 0xaf, 0x3e, 0x4c, 0xdd,
	0xa6, 0x37, 0x45, 0xd4, 0xa1, 0x30, 0x42, 0xd3,
	0xb4, 0x25, 0x57, 0xc6, 0xb3, 0x22, 0x50, 0xc1,
	0xba, 0x2b, 0x59, 0xc8, 0xbd, 0x2c, 0x5e, 0xcf
};

/* CRC */
static u_int8_t
ng_btsocket_rfcomm_crc(u_int8_t *data, int length)
{
	u_int8_t	crc = 0xff;

	while (length --)
		crc = ng_btsocket_rfcomm_crc_table[crc ^ *data++];

	return (crc);
} /* ng_btsocket_rfcomm_crc */

/* FCS on 2 bytes */
static u_int8_t
ng_btsocket_rfcomm_fcs2(u_int8_t *data)
{
	return (0xff - ng_btsocket_rfcomm_crc(data, 2));
} /* ng_btsocket_rfcomm_fcs2 */
  
/* FCS on 3 bytes */
static u_int8_t
ng_btsocket_rfcomm_fcs3(u_int8_t *data)
{
	return (0xff - ng_btsocket_rfcomm_crc(data, 3));
} /* ng_btsocket_rfcomm_fcs3 */

/* 
 * Check FCS
 *
 * From Bluetooth spec
 *
 * "... In 07.10, the frame check sequence (FCS) is calculated on different 
 * sets of fields for different frame types. These are the fields that the 
 * FCS are calculated on:
 *
 * For SABM, DISC, UA, DM frames: on Address, Control and length field.
 * For UIH frames: on Address and Control field.
 *
 * (This is stated here for clarification, and to set the standard for RFCOMM;
 * the fields included in FCS calculation have actually changed in version
 * 7.0.0 of TS 07.10, but RFCOMM will not change the FCS calculation scheme
 * from the one above.) ..."
 */

static int
ng_btsocket_rfcomm_check_fcs(u_int8_t *data, int type, u_int8_t fcs)
{
	if (type != RFCOMM_FRAME_UIH)
		return (ng_btsocket_rfcomm_fcs3(data) != fcs);

	return (ng_btsocket_rfcomm_fcs2(data) != fcs);
} /* ng_btsocket_rfcomm_check_fcs */

/*****************************************************************************
 *****************************************************************************
 **                              Socket interface
 *****************************************************************************
 *****************************************************************************/

/* 
 * Initialize everything
 */

void
ng_btsocket_rfcomm_init(void)
{

	/* Skip initialization of globals for non-default instances. */
	if (!IS_DEFAULT_VNET(curvnet))
		return;

	ng_btsocket_rfcomm_debug_level = NG_BTSOCKET_WARN_LEVEL;
	ng_btsocket_rfcomm_timo = 60;

	/* RFCOMM task */
	TASK_INIT(&ng_btsocket_rfcomm_task, 0,
		ng_btsocket_rfcomm_sessions_task, NULL);

	/* RFCOMM sessions list */
	LIST_INIT(&ng_btsocket_rfcomm_sessions);
	mtx_init(&ng_btsocket_rfcomm_sessions_mtx,
		"btsocks_rfcomm_sessions_mtx", NULL, MTX_DEF);

	/* RFCOMM sockets list */
	LIST_INIT(&ng_btsocket_rfcomm_sockets);
	mtx_init(&ng_btsocket_rfcomm_sockets_mtx,
		"btsocks_rfcomm_sockets_mtx", NULL, MTX_DEF);
} /* ng_btsocket_rfcomm_init */

/*
 * Abort connection on socket
 */

void
ng_btsocket_rfcomm_abort(struct socket *so)
{

	so->so_error = ECONNABORTED;
	(void)ng_btsocket_rfcomm_disconnect(so);
} /* ng_btsocket_rfcomm_abort */

void
ng_btsocket_rfcomm_close(struct socket *so)
{

	(void)ng_btsocket_rfcomm_disconnect(so);
} /* ng_btsocket_rfcomm_close */

/*
 * Accept connection on socket. Nothing to do here, socket must be connected
 * and ready, so just return peer address and be done with it.
 */

int
ng_btsocket_rfcomm_accept(struct socket *so, struct sockaddr **nam)
{
	return (ng_btsocket_rfcomm_peeraddr(so, nam));
} /* ng_btsocket_rfcomm_accept */

/*
 * Create and attach new socket
 */

int
ng_btsocket_rfcomm_attach(struct socket *so, int proto, struct thread *td)
{
	ng_btsocket_rfcomm_pcb_p	pcb = so2rfcomm_pcb(so);
	int				error;

	/* Check socket and protocol */
	if (so->so_type != SOCK_STREAM)
		return (ESOCKTNOSUPPORT);

#if 0 /* XXX sonewconn() calls "pru_attach" with proto == 0 */
	if (proto != 0) 
		if (proto != BLUETOOTH_PROTO_RFCOMM)
			return (EPROTONOSUPPORT);
#endif /* XXX */

	if (pcb != NULL)
		return (EISCONN);

	/* Reserve send and receive space if it is not reserved yet */
	if ((so->so_snd.sb_hiwat == 0) || (so->so_rcv.sb_hiwat == 0)) {
		error = soreserve(so, NG_BTSOCKET_RFCOMM_SENDSPACE,
					NG_BTSOCKET_RFCOMM_RECVSPACE);
		if (error != 0)
			return (error);
	}

	/* Allocate the PCB */
        pcb = malloc(sizeof(*pcb),
		M_NETGRAPH_BTSOCKET_RFCOMM, M_NOWAIT | M_ZERO);
        if (pcb == NULL)
                return (ENOMEM);

	/* Link the PCB and the socket */
	so->so_pcb = (caddr_t) pcb;
	pcb->so = so;

	/* Initialize PCB */
	pcb->state = NG_BTSOCKET_RFCOMM_DLC_CLOSED;
	pcb->flags = NG_BTSOCKET_RFCOMM_DLC_CFC;

	pcb->lmodem =
	pcb->rmodem = (RFCOMM_MODEM_RTC | RFCOMM_MODEM_RTR | RFCOMM_MODEM_DV);

	pcb->mtu = RFCOMM_DEFAULT_MTU;
	pcb->tx_cred = 0;
	pcb->rx_cred = RFCOMM_DEFAULT_CREDITS;

	mtx_init(&pcb->pcb_mtx, "btsocks_rfcomm_pcb_mtx", NULL, MTX_DEF);
	callout_init_mtx(&pcb->timo, &pcb->pcb_mtx, 0);

	/* Add the PCB to the list */
	mtx_lock(&ng_btsocket_rfcomm_sockets_mtx);
	LIST_INSERT_HEAD(&ng_btsocket_rfcomm_sockets, pcb, next);
	mtx_unlock(&ng_btsocket_rfcomm_sockets_mtx);

        return (0);
} /* ng_btsocket_rfcomm_attach */

/*
 * Bind socket
 */

int
ng_btsocket_rfcomm_bind(struct socket *so, struct sockaddr *nam, 
		struct thread *td)
{
	ng_btsocket_rfcomm_pcb_t	*pcb = so2rfcomm_pcb(so), *pcb1;
	struct sockaddr_rfcomm		*sa = (struct sockaddr_rfcomm *) nam;

	if (pcb == NULL)
		return (EINVAL);

	/* Verify address */
	if (sa == NULL)
		return (EINVAL);
	if (sa->rfcomm_family != AF_BLUETOOTH)
		return (EAFNOSUPPORT);
	if (sa->rfcomm_len != sizeof(*sa))
		return (EINVAL);
	if (sa->rfcomm_channel > 30)
		return (EINVAL);

	mtx_lock(&pcb->pcb_mtx);

	if (sa->rfcomm_channel != 0) {
		mtx_lock(&ng_btsocket_rfcomm_sockets_mtx);

		LIST_FOREACH(pcb1, &ng_btsocket_rfcomm_sockets, next) {
			if (pcb1->channel == sa->rfcomm_channel &&
			    bcmp(&pcb1->src, &sa->rfcomm_bdaddr,
					sizeof(pcb1->src)) == 0) {
				mtx_unlock(&ng_btsocket_rfcomm_sockets_mtx);
				mtx_unlock(&pcb->pcb_mtx);

				return (EADDRINUSE);
			}
		}

		mtx_unlock(&ng_btsocket_rfcomm_sockets_mtx);
	}

	bcopy(&sa->rfcomm_bdaddr, &pcb->src, sizeof(pcb->src));
	pcb->channel = sa->rfcomm_channel;

	mtx_unlock(&pcb->pcb_mtx);

	return (0);
} /* ng_btsocket_rfcomm_bind */

/*
 * Connect socket
 */

int
ng_btsocket_rfcomm_connect(struct socket *so, struct sockaddr *nam, 
		struct thread *td)
{
	ng_btsocket_rfcomm_pcb_t	*pcb = so2rfcomm_pcb(so);
	struct sockaddr_rfcomm		*sa = (struct sockaddr_rfcomm *) nam;
	ng_btsocket_rfcomm_session_t	*s = NULL;
	struct socket			*l2so = NULL;
	int				 dlci, error = 0;

	if (pcb == NULL)
		return (EINVAL);

	/* Verify address */
	if (sa == NULL)
		return (EINVAL);
	if (sa->rfcomm_family != AF_BLUETOOTH)
		return (EAFNOSUPPORT);
	if (sa->rfcomm_len != sizeof(*sa))
		return (EINVAL);
	if (sa->rfcomm_channel > 30)
		return (EINVAL);
	if (sa->rfcomm_channel == 0 ||
	    bcmp(&sa->rfcomm_bdaddr, NG_HCI_BDADDR_ANY, sizeof(bdaddr_t)) == 0)
		return (EDESTADDRREQ);

	/*
	 * Note that we will not check for errors in socreate() because
	 * if we failed to create L2CAP socket at this point we still
	 * might have already open session.
	 */

	error = socreate(PF_BLUETOOTH, &l2so, SOCK_SEQPACKET,
			BLUETOOTH_PROTO_L2CAP, td->td_ucred, td);

	/* 
	 * Look for session between "pcb->src" and "sa->rfcomm_bdaddr" (dst)
	 */

	mtx_lock(&ng_btsocket_rfcomm_sessions_mtx);

	s = ng_btsocket_rfcomm_session_by_addr(&pcb->src, &sa->rfcomm_bdaddr);
	if (s == NULL) {
		/*
		 * We need to create new RFCOMM session. Check if we have L2CAP
		 * socket. If l2so == NULL then error has the error code from
		 * socreate()
		 */

		if (l2so == NULL) {
			mtx_unlock(&ng_btsocket_rfcomm_sessions_mtx);
			return (error);
		}

		error = ng_btsocket_rfcomm_session_create(&s, l2so,
				&pcb->src, &sa->rfcomm_bdaddr, td);
		if (error != 0) {
			mtx_unlock(&ng_btsocket_rfcomm_sessions_mtx);
			soclose(l2so);

			return (error);
		}
	} else if (l2so != NULL)
		soclose(l2so); /* we don't need new L2CAP socket */

	/*
	 * Check if we already have the same DLCI the same session
	 */

	mtx_lock(&s->session_mtx);
	mtx_lock(&pcb->pcb_mtx);

	dlci = RFCOMM_MKDLCI(!INITIATOR(s), sa->rfcomm_channel);

	if (ng_btsocket_rfcomm_pcb_by_dlci(s, dlci) != NULL) {
		mtx_unlock(&pcb->pcb_mtx);
		mtx_unlock(&s->session_mtx);
		mtx_unlock(&ng_btsocket_rfcomm_sessions_mtx);

		return (EBUSY);
	}

	/*
	 * Check session state and if its not acceptable then refuse connection
	 */

	switch (s->state) {
	case NG_BTSOCKET_RFCOMM_SESSION_CONNECTING:
	case NG_BTSOCKET_RFCOMM_SESSION_CONNECTED:
	case NG_BTSOCKET_RFCOMM_SESSION_OPEN:
		/*
		 * Update destination address and channel and attach 
		 * DLC to the session
		 */

		bcopy(&sa->rfcomm_bdaddr, &pcb->dst, sizeof(pcb->dst));
		pcb->channel = sa->rfcomm_channel;
		pcb->dlci = dlci;

		LIST_INSERT_HEAD(&s->dlcs, pcb, session_next);
		pcb->session = s;

		ng_btsocket_rfcomm_timeout(pcb);
		soisconnecting(pcb->so);

		if (s->state == NG_BTSOCKET_RFCOMM_SESSION_OPEN) {
			pcb->mtu = s->mtu;
			bcopy(&so2l2cap_pcb(s->l2so)->src, &pcb->src,
				sizeof(pcb->src));

			pcb->state = NG_BTSOCKET_RFCOMM_DLC_CONFIGURING;

			error = ng_btsocket_rfcomm_send_pn(pcb);
			if (error == 0)
				error = ng_btsocket_rfcomm_task_wakeup();
		} else
			pcb->state = NG_BTSOCKET_RFCOMM_DLC_W4_CONNECT;
		break;

	default:
		error = ECONNRESET;
		break;
	}

	mtx_unlock(&pcb->pcb_mtx);
	mtx_unlock(&s->session_mtx);
	mtx_unlock(&ng_btsocket_rfcomm_sessions_mtx);

	return (error);
} /* ng_btsocket_rfcomm_connect */

/*
 * Process ioctl's calls on socket.
 * XXX FIXME this should provide interface to the RFCOMM multiplexor channel
 */

int
ng_btsocket_rfcomm_control(struct socket *so, u_long cmd, caddr_t data,
		struct ifnet *ifp, struct thread *td)
{
	return (EINVAL);
} /* ng_btsocket_rfcomm_control */

/*
 * Process getsockopt/setsockopt system calls
 */

int
ng_btsocket_rfcomm_ctloutput(struct socket *so, struct sockopt *sopt)
{
	ng_btsocket_rfcomm_pcb_p		pcb = so2rfcomm_pcb(so);
	struct ng_btsocket_rfcomm_fc_info	fcinfo;
	int					error = 0;

	if (pcb == NULL)
		return (EINVAL);
	if (sopt->sopt_level != SOL_RFCOMM)
		return (0);

	mtx_lock(&pcb->pcb_mtx);

	switch (sopt->sopt_dir) {
	case SOPT_GET:
		switch (sopt->sopt_name) {
		case SO_RFCOMM_MTU:
			error = sooptcopyout(sopt, &pcb->mtu, sizeof(pcb->mtu));
			break;

		case SO_RFCOMM_FC_INFO:
			fcinfo.lmodem = pcb->lmodem;
			fcinfo.rmodem = pcb->rmodem;
			fcinfo.tx_cred = pcb->tx_cred;
			fcinfo.rx_cred = pcb->rx_cred;
			fcinfo.cfc = (pcb->flags & NG_BTSOCKET_RFCOMM_DLC_CFC)?
				1 : 0;
			fcinfo.reserved = 0;

			error = sooptcopyout(sopt, &fcinfo, sizeof(fcinfo));
			break;

		default:
			error = ENOPROTOOPT;
			break;
		}
		break;

	case SOPT_SET:
		switch (sopt->sopt_name) {
		default:
			error = ENOPROTOOPT;
			break;
		}
		break;

	default:
		error = EINVAL;
		break;
	}

	mtx_unlock(&pcb->pcb_mtx);

	return (error);
} /* ng_btsocket_rfcomm_ctloutput */

/*
 * Detach and destroy socket
 */

void
ng_btsocket_rfcomm_detach(struct socket *so)
{
	ng_btsocket_rfcomm_pcb_p	pcb = so2rfcomm_pcb(so);

	KASSERT(pcb != NULL, ("ng_btsocket_rfcomm_detach: pcb == NULL"));

	mtx_lock(&pcb->pcb_mtx);

	switch (pcb->state) {
	case NG_BTSOCKET_RFCOMM_DLC_W4_CONNECT:
	case NG_BTSOCKET_RFCOMM_DLC_CONFIGURING:
	case NG_BTSOCKET_RFCOMM_DLC_CONNECTING:
	case NG_BTSOCKET_RFCOMM_DLC_CONNECTED:
		/* XXX What to do with pending request? */
		if (pcb->flags & NG_BTSOCKET_RFCOMM_DLC_TIMO)
			ng_btsocket_rfcomm_untimeout(pcb);

		if (pcb->state == NG_BTSOCKET_RFCOMM_DLC_W4_CONNECT)
			pcb->flags |= NG_BTSOCKET_RFCOMM_DLC_DETACHED;
		else
			pcb->state = NG_BTSOCKET_RFCOMM_DLC_DISCONNECTING;

		ng_btsocket_rfcomm_task_wakeup();
		break;

	case NG_BTSOCKET_RFCOMM_DLC_DISCONNECTING:
		ng_btsocket_rfcomm_task_wakeup();
		break;
	}
	
	while (pcb->state != NG_BTSOCKET_RFCOMM_DLC_CLOSED)
		msleep(&pcb->state, &pcb->pcb_mtx, PZERO, "rf_det", 0);

	if (pcb->session != NULL)
		panic("%s: pcb->session != NULL\n", __func__);
	if (pcb->flags & NG_BTSOCKET_RFCOMM_DLC_TIMO)
		panic("%s: timeout on closed DLC, flags=%#x\n",
			__func__, pcb->flags);

	mtx_lock(&ng_btsocket_rfcomm_sockets_mtx);
	LIST_REMOVE(pcb, next);
	mtx_unlock(&ng_btsocket_rfcomm_sockets_mtx);

	mtx_unlock(&pcb->pcb_mtx);

	mtx_destroy(&pcb->pcb_mtx);
	bzero(pcb, sizeof(*pcb));
	free(pcb, M_NETGRAPH_BTSOCKET_RFCOMM);

	soisdisconnected(so);
	so->so_pcb = NULL;
} /* ng_btsocket_rfcomm_detach */

/*
 * Disconnect socket
 */

int
ng_btsocket_rfcomm_disconnect(struct socket *so)
{
	ng_btsocket_rfcomm_pcb_p	pcb = so2rfcomm_pcb(so);

	if (pcb == NULL)
		return (EINVAL);

	mtx_lock(&pcb->pcb_mtx);

	if (pcb->state == NG_BTSOCKET_RFCOMM_DLC_DISCONNECTING) {
		mtx_unlock(&pcb->pcb_mtx);
		return (EINPROGRESS);
	}

	/* XXX What to do with pending request? */
	if (pcb->flags & NG_BTSOCKET_RFCOMM_DLC_TIMO)
		ng_btsocket_rfcomm_untimeout(pcb);

	switch (pcb->state) {
	case NG_BTSOCKET_RFCOMM_DLC_CONFIGURING: /* XXX can we get here? */
	case NG_BTSOCKET_RFCOMM_DLC_CONNECTING: /* XXX can we get here? */
	case NG_BTSOCKET_RFCOMM_DLC_CONNECTED:

		/*
		 * Just change DLC state and enqueue RFCOMM task. It will
		 * queue and send DISC on the DLC.
		 */ 

		pcb->state = NG_BTSOCKET_RFCOMM_DLC_DISCONNECTING;
		soisdisconnecting(so);

		ng_btsocket_rfcomm_task_wakeup();
		break;

	case NG_BTSOCKET_RFCOMM_DLC_CLOSED:
	case NG_BTSOCKET_RFCOMM_DLC_W4_CONNECT:
		break;

	default:
		panic("%s: Invalid DLC state=%d, flags=%#x\n",
			__func__, pcb->state, pcb->flags);
		break;
	}

	mtx_unlock(&pcb->pcb_mtx);

	return (0);
} /* ng_btsocket_rfcomm_disconnect */

/*
 * Listen on socket. First call to listen() will create listening RFCOMM session
 */

int
ng_btsocket_rfcomm_listen(struct socket *so, int backlog, struct thread *td)
{
	ng_btsocket_rfcomm_pcb_p	 pcb = so2rfcomm_pcb(so), pcb1;
	ng_btsocket_rfcomm_session_p	 s = NULL;
	struct socket			*l2so = NULL;
	int				 error, socreate_error, usedchannels;

	if (pcb == NULL)
		return (EINVAL);
	if (pcb->channel > 30)
		return (EADDRNOTAVAIL);

	usedchannels = 0;

	mtx_lock(&pcb->pcb_mtx);

	if (pcb->channel == 0) {
		mtx_lock(&ng_btsocket_rfcomm_sockets_mtx);

		LIST_FOREACH(pcb1, &ng_btsocket_rfcomm_sockets, next)
			if (pcb1->channel != 0 &&
			    bcmp(&pcb1->src, &pcb->src, sizeof(pcb->src)) == 0)
				usedchannels |= (1 << (pcb1->channel - 1));

		for (pcb->channel = 30; pcb->channel > 0; pcb->channel --)
			if (!(usedchannels & (1 << (pcb->channel - 1))))
				break;

		if (pcb->channel == 0) {
			mtx_unlock(&ng_btsocket_rfcomm_sockets_mtx);
			mtx_unlock(&pcb->pcb_mtx);

			return (EADDRNOTAVAIL);
		}

		mtx_unlock(&ng_btsocket_rfcomm_sockets_mtx);
	}

	mtx_unlock(&pcb->pcb_mtx);

	/*
	 * Note that we will not check for errors in socreate() because
	 * if we failed to create L2CAP socket at this point we still
	 * might have already open session.
	 */

	socreate_error = socreate(PF_BLUETOOTH, &l2so, SOCK_SEQPACKET,
			BLUETOOTH_PROTO_L2CAP, td->td_ucred, td);

	/*
	 * Transition the socket and session into the LISTENING state.  Check
	 * for collisions first, as there can only be one.
	 */
	mtx_lock(&ng_btsocket_rfcomm_sessions_mtx);
	SOCK_LOCK(so);
	error = solisten_proto_check(so);
	SOCK_UNLOCK(so);
	if (error != 0)
		goto out;

	LIST_FOREACH(s, &ng_btsocket_rfcomm_sessions, next)
		if (s->state == NG_BTSOCKET_RFCOMM_SESSION_LISTENING)
			break;

	if (s == NULL) {
		/*
		 * We need to create default RFCOMM session. Check if we have 
		 * L2CAP socket. If l2so == NULL then error has the error code 
		 * from socreate()
		 */
		if (l2so == NULL) {
			error = socreate_error;
			goto out;
		}

		/* 
		 * Create default listen RFCOMM session. The default RFCOMM 
		 * session will listen on ANY address.
		 *
		 * XXX FIXME Note that currently there is no way to adjust MTU
		 * for the default session.
		 */
		error = ng_btsocket_rfcomm_session_create(&s, l2so,
					NG_HCI_BDADDR_ANY, NULL, td);
		if (error != 0)
			goto out;
		l2so = NULL;
	}
	SOCK_LOCK(so);
	solisten_proto(so, backlog);
	SOCK_UNLOCK(so);
out:
	mtx_unlock(&ng_btsocket_rfcomm_sessions_mtx);
	/*
	 * If we still have an l2so reference here, it's unneeded, so release
	 * it.
	 */
	if (l2so != NULL)
		soclose(l2so);
	return (error);
} /* ng_btsocket_listen */

/*
 * Get peer address
 */

int
ng_btsocket_rfcomm_peeraddr(struct socket *so, struct sockaddr **nam)
{
	ng_btsocket_rfcomm_pcb_p	pcb = so2rfcomm_pcb(so);
	struct sockaddr_rfcomm		sa;

	if (pcb == NULL)
		return (EINVAL);

	bcopy(&pcb->dst, &sa.rfcomm_bdaddr, sizeof(sa.rfcomm_bdaddr));
	sa.rfcomm_channel = pcb->channel;
	sa.rfcomm_len = sizeof(sa);
	sa.rfcomm_family = AF_BLUETOOTH;

	*nam = sodupsockaddr((struct sockaddr *) &sa, M_NOWAIT);

	return ((*nam == NULL)? ENOMEM : 0);
} /* ng_btsocket_rfcomm_peeraddr */

/*
 * Send data to socket
 */

int
ng_btsocket_rfcomm_send(struct socket *so, int flags, struct mbuf *m,
		struct sockaddr *nam, struct mbuf *control, struct thread *td)
{
	ng_btsocket_rfcomm_pcb_t	*pcb = so2rfcomm_pcb(so);
	int				 error = 0;

	/* Check socket and input */
	if (pcb == NULL || m == NULL || control != NULL) {
		error = EINVAL;
		goto drop;
	}

	mtx_lock(&pcb->pcb_mtx);

	/* Make sure DLC is connected */
	if (pcb->state != NG_BTSOCKET_RFCOMM_DLC_CONNECTED) {
		mtx_unlock(&pcb->pcb_mtx);
		error = ENOTCONN;
		goto drop;
	}

	/* Put the packet on the socket's send queue and wakeup RFCOMM task */
	sbappend(&pcb->so->so_snd, m, flags);
	m = NULL;
	
	if (!(pcb->flags & NG_BTSOCKET_RFCOMM_DLC_SENDING)) {
		pcb->flags |= NG_BTSOCKET_RFCOMM_DLC_SENDING;
		error = ng_btsocket_rfcomm_task_wakeup();
	}

	mtx_unlock(&pcb->pcb_mtx);
drop:
	NG_FREE_M(m); /* checks for != NULL */
	NG_FREE_M(control);

	return (error);
} /* ng_btsocket_rfcomm_send */

/*
 * Get socket address
 */

int
ng_btsocket_rfcomm_sockaddr(struct socket *so, struct sockaddr **nam)
{
	ng_btsocket_rfcomm_pcb_p	pcb = so2rfcomm_pcb(so);
	struct sockaddr_rfcomm		sa;

	if (pcb == NULL)
		return (EINVAL);

	bcopy(&pcb->src, &sa.rfcomm_bdaddr, sizeof(sa.rfcomm_bdaddr));
	sa.rfcomm_channel = pcb->channel;
	sa.rfcomm_len = sizeof(sa);
	sa.rfcomm_family = AF_BLUETOOTH;

	*nam = sodupsockaddr((struct sockaddr *) &sa, M_NOWAIT);

	return ((*nam == NULL)? ENOMEM : 0);
} /* ng_btsocket_rfcomm_sockaddr */

/*
 * Upcall function for L2CAP sockets. Enqueue RFCOMM task.
 */

static int
ng_btsocket_rfcomm_upcall(struct socket *so, void *arg, int waitflag)
{
	int	error;

	if (so == NULL)
		panic("%s: so == NULL\n", __func__);

	if ((error = ng_btsocket_rfcomm_task_wakeup()) != 0)
		NG_BTSOCKET_RFCOMM_ALERT(
"%s: Could not enqueue RFCOMM task, error=%d\n", __func__, error);
	return (SU_OK);
} /* ng_btsocket_rfcomm_upcall */

/*
 * RFCOMM task. Will handle all RFCOMM sessions in one pass.
 * XXX FIXME does not scale very well
 */

static void
ng_btsocket_rfcomm_sessions_task(void *ctx, int pending)
{
	ng_btsocket_rfcomm_session_p	s = NULL, s_next = NULL;

	mtx_lock(&ng_btsocket_rfcomm_sessions_mtx);

	for (s = LIST_FIRST(&ng_btsocket_rfcomm_sessions); s != NULL; ) {
		mtx_lock(&s->session_mtx);
		s_next = LIST_NEXT(s, next);

		ng_btsocket_rfcomm_session_task(s);

		if (s->state == NG_BTSOCKET_RFCOMM_SESSION_CLOSED) {
			/* Unlink and clean the session */
			LIST_REMOVE(s, next);

			NG_BT_MBUFQ_DRAIN(&s->outq);
			if (!LIST_EMPTY(&s->dlcs))
				panic("%s: DLC list is not empty\n", __func__);

			/* Close L2CAP socket */
			SOCKBUF_LOCK(&s->l2so->so_rcv);
			soupcall_clear(s->l2so, SO_RCV);
			SOCKBUF_UNLOCK(&s->l2so->so_rcv);
			SOCKBUF_LOCK(&s->l2so->so_snd);
			soupcall_clear(s->l2so, SO_SND);
			SOCKBUF_UNLOCK(&s->l2so->so_snd);
			soclose(s->l2so);

			mtx_unlock(&s->session_mtx);

			mtx_destroy(&s->session_mtx);
			bzero(s, sizeof(*s));
			free(s, M_NETGRAPH_BTSOCKET_RFCOMM);
		} else
			mtx_unlock(&s->session_mtx);

		s = s_next;
	}

	mtx_unlock(&ng_btsocket_rfcomm_sessions_mtx);
} /* ng_btsocket_rfcomm_sessions_task */

/*
 * Process RFCOMM session. Will handle all RFCOMM sockets in one pass.
 */

static void
ng_btsocket_rfcomm_session_task(ng_btsocket_rfcomm_session_p s)
{
	mtx_assert(&s->session_mtx, MA_OWNED);

	if (s->l2so->so_rcv.sb_state & SBS_CANTRCVMORE) {
		NG_BTSOCKET_RFCOMM_INFO(
"%s: L2CAP connection has been terminated, so=%p, so_state=%#x, so_count=%d, " \
"state=%d, flags=%#x\n", __func__, s->l2so, s->l2so->so_state, 
			s->l2so->so_count, s->state, s->flags);

		s->state = NG_BTSOCKET_RFCOMM_SESSION_CLOSED;
		ng_btsocket_rfcomm_session_clean(s);
	}

	/* Now process upcall */
	switch (s->state) {
	/* Try to accept new L2CAP connection(s) */
	case NG_BTSOCKET_RFCOMM_SESSION_LISTENING:
		while (ng_btsocket_rfcomm_session_accept(s) == 0)
			;
		break;

	/* Process the results of the L2CAP connect */
	case NG_BTSOCKET_RFCOMM_SESSION_CONNECTING:
		ng_btsocket_rfcomm_session_process_pcb(s);

		if (ng_btsocket_rfcomm_session_connect(s) != 0) {
			s->state = NG_BTSOCKET_RFCOMM_SESSION_CLOSED;
			ng_btsocket_rfcomm_session_clean(s);
		} 
		break;

	/* Try to receive/send more data */
	case NG_BTSOCKET_RFCOMM_SESSION_CONNECTED:
	case NG_BTSOCKET_RFCOMM_SESSION_OPEN:
	case NG_BTSOCKET_RFCOMM_SESSION_DISCONNECTING:
		ng_btsocket_rfcomm_session_process_pcb(s);

		if (ng_btsocket_rfcomm_session_receive(s) != 0) {
			s->state = NG_BTSOCKET_RFCOMM_SESSION_CLOSED;
			ng_btsocket_rfcomm_session_clean(s);
		} else if (ng_btsocket_rfcomm_session_send(s) != 0) {
			s->state = NG_BTSOCKET_RFCOMM_SESSION_CLOSED;
			ng_btsocket_rfcomm_session_clean(s);
		}
		break;

	case NG_BTSOCKET_RFCOMM_SESSION_CLOSED:
		break;

	default:
		panic("%s: Invalid session state=%d, flags=%#x\n",
			__func__, s->state, s->flags);
		break;
	}
} /* ng_btsocket_rfcomm_session_task */

/*
 * Process RFCOMM connection indicator. Caller must hold s->session_mtx
 */

static ng_btsocket_rfcomm_pcb_p
ng_btsocket_rfcomm_connect_ind(ng_btsocket_rfcomm_session_p s, int channel)
{
	ng_btsocket_rfcomm_pcb_p	 pcb = NULL, pcb1 = NULL;
	ng_btsocket_l2cap_pcb_p		 l2pcb = NULL;
	struct socket			*so1;

	mtx_assert(&s->session_mtx, MA_OWNED);

	/*
	 * Try to find RFCOMM socket that listens on given source address 
	 * and channel. This will return the best possible match.
	 */

	l2pcb = so2l2cap_pcb(s->l2so);
	pcb = ng_btsocket_rfcomm_pcb_listener(&l2pcb->src, channel);
	if (pcb == NULL)
		return (NULL);

	/*
	 * Check the pending connections queue and if we have space then 
	 * create new socket and set proper source and destination address,
	 * and channel.
	 */

	mtx_lock(&pcb->pcb_mtx);

	CURVNET_SET(pcb->so->so_vnet);
	so1 = sonewconn(pcb->so, 0);
	CURVNET_RESTORE();

	mtx_unlock(&pcb->pcb_mtx);

	if (so1 == NULL)
		return (NULL);

	/*
	 * If we got here than we have created new socket. So complete the 
	 * connection. Set source and destination address from the session.
	 */

	pcb1 = so2rfcomm_pcb(so1);
	if (pcb1 == NULL)
		panic("%s: pcb1 == NULL\n", __func__);

	mtx_lock(&pcb1->pcb_mtx);

	bcopy(&l2pcb->src, &pcb1->src, sizeof(pcb1->src));
	bcopy(&l2pcb->dst, &pcb1->dst, sizeof(pcb1->dst));
	pcb1->channel = channel;

	/* Link new DLC to the session. We already hold s->session_mtx */
	LIST_INSERT_HEAD(&s->dlcs, pcb1, session_next);
	pcb1->session = s;
			
	mtx_unlock(&pcb1->pcb_mtx);

	return (pcb1);
} /* ng_btsocket_rfcomm_connect_ind */

/*
 * Process RFCOMM connect confirmation. Caller must hold s->session_mtx.
 */

static void
ng_btsocket_rfcomm_connect_cfm(ng_btsocket_rfcomm_session_p s)
{
	ng_btsocket_rfcomm_pcb_p	pcb = NULL, pcb_next = NULL;
	int				error;

	mtx_assert(&s->session_mtx, MA_OWNED);

	/*
	 * Wake up all waiting sockets and send PN request for each of them. 
	 * Note that timeout already been set in ng_btsocket_rfcomm_connect()
	 *
	 * Note: cannot use LIST_FOREACH because ng_btsocket_rfcomm_pcb_kill
	 * will unlink DLC from the session
	 */

	for (pcb = LIST_FIRST(&s->dlcs); pcb != NULL; ) {
		mtx_lock(&pcb->pcb_mtx);
		pcb_next = LIST_NEXT(pcb, session_next);

		if (pcb->state == NG_BTSOCKET_RFCOMM_DLC_W4_CONNECT) {
			pcb->mtu = s->mtu;
			bcopy(&so2l2cap_pcb(s->l2so)->src, &pcb->src,
				sizeof(pcb->src));

			error = ng_btsocket_rfcomm_send_pn(pcb);
			if (error == 0)
				pcb->state = NG_BTSOCKET_RFCOMM_DLC_CONFIGURING;
			else
				ng_btsocket_rfcomm_pcb_kill(pcb, error);
		}

		mtx_unlock(&pcb->pcb_mtx);
		pcb = pcb_next;
	}
} /* ng_btsocket_rfcomm_connect_cfm */

/*****************************************************************************
 *****************************************************************************
 **                              RFCOMM sessions
 *****************************************************************************
 *****************************************************************************/

/*
 * Create new RFCOMM session. That function WILL NOT take ownership over l2so.
 * Caller MUST free l2so if function failed.
 */

static int
ng_btsocket_rfcomm_session_create(ng_btsocket_rfcomm_session_p *sp,
		struct socket *l2so, bdaddr_p src, bdaddr_p dst,
		struct thread *td)
{
	ng_btsocket_rfcomm_session_p	s = NULL;
	struct sockaddr_l2cap		l2sa;
	struct sockopt			l2sopt;
	int				error;
	u_int16_t			mtu;

	mtx_assert(&ng_btsocket_rfcomm_sessions_mtx, MA_OWNED);

	/* Allocate the RFCOMM session */
        s = malloc(sizeof(*s),
		M_NETGRAPH_BTSOCKET_RFCOMM, M_NOWAIT | M_ZERO);
        if (s == NULL)
                return (ENOMEM);

	/* Set defaults */
	s->mtu = RFCOMM_DEFAULT_MTU;
	s->flags = 0;
	s->state = NG_BTSOCKET_RFCOMM_SESSION_CLOSED;
	NG_BT_MBUFQ_INIT(&s->outq, ifqmaxlen);

	/*
	 * XXX Mark session mutex as DUPOK to prevent "duplicated lock of 
	 * the same type" message. When accepting new L2CAP connection
	 * ng_btsocket_rfcomm_session_accept() holds both session mutexes 
	 * for "old" (accepting) session and "new" (created) session.
	 */

	mtx_init(&s->session_mtx, "btsocks_rfcomm_session_mtx", NULL,
		MTX_DEF|MTX_DUPOK);

	LIST_INIT(&s->dlcs);

	/* Prepare L2CAP socket */
	SOCKBUF_LOCK(&l2so->so_rcv);
	soupcall_set(l2so, SO_RCV, ng_btsocket_rfcomm_upcall, NULL);
	SOCKBUF_UNLOCK(&l2so->so_rcv);
	SOCKBUF_LOCK(&l2so->so_snd);
	soupcall_set(l2so, SO_SND, ng_btsocket_rfcomm_upcall, NULL);
	SOCKBUF_UNLOCK(&l2so->so_snd);
	l2so->so_state |= SS_NBIO;
	s->l2so = l2so;

	mtx_lock(&s->session_mtx);

	/*
	 * "src" == NULL and "dst" == NULL means just create session.
	 * caller must do the rest
	 */

	if (src == NULL && dst == NULL)
		goto done;

	/*
	 * Set incoming MTU on L2CAP socket. It is RFCOMM session default MTU 
	 * plus 5 bytes: RFCOMM frame header, one extra byte for length and one
	 * extra byte for credits.
	 */

	mtu = s->mtu + sizeof(struct rfcomm_frame_hdr) + 1 + 1;

	l2sopt.sopt_dir = SOPT_SET;
	l2sopt.sopt_level = SOL_L2CAP;
	l2sopt.sopt_name = SO_L2CAP_IMTU;
	l2sopt.sopt_val = (void *) &mtu;
	l2sopt.sopt_valsize = sizeof(mtu);
	l2sopt.sopt_td = NULL;

	error = sosetopt(s->l2so, &l2sopt);
	if (error != 0)
		goto bad;

	/* Bind socket to "src" address */
	l2sa.l2cap_len = sizeof(l2sa);
	l2sa.l2cap_family = AF_BLUETOOTH;
	l2sa.l2cap_psm = (dst == NULL)? htole16(NG_L2CAP_PSM_RFCOMM) : 0;
	bcopy(src, &l2sa.l2cap_bdaddr, sizeof(l2sa.l2cap_bdaddr));
	l2sa.l2cap_cid = 0;
	l2sa.l2cap_bdaddr_type = BDADDR_BREDR;

	error = sobind(s->l2so, (struct sockaddr *) &l2sa, td);
	if (error != 0)
		goto bad;

	/* If "dst" is not NULL then initiate connect(), otherwise listen() */
	if (dst == NULL) {
		s->flags = 0;
		s->state = NG_BTSOCKET_RFCOMM_SESSION_LISTENING;

		error = solisten(s->l2so, 10, td);
		if (error != 0)
			goto bad;
	} else {
		s->flags = NG_BTSOCKET_RFCOMM_SESSION_INITIATOR;
		s->state = NG_BTSOCKET_RFCOMM_SESSION_CONNECTING;

		l2sa.l2cap_len = sizeof(l2sa);   
		l2sa.l2cap_family = AF_BLUETOOTH;
		l2sa.l2cap_psm = htole16(NG_L2CAP_PSM_RFCOMM);
	        bcopy(dst, &l2sa.l2cap_bdaddr, sizeof(l2sa.l2cap_bdaddr));
		l2sa.l2cap_cid = 0;
		l2sa.l2cap_bdaddr_type = BDADDR_BREDR;

		error = soconnect(s->l2so, (struct sockaddr *) &l2sa, td);
		if (error != 0)
			goto bad;
	}

done:
	LIST_INSERT_HEAD(&ng_btsocket_rfcomm_sessions, s, next);
	*sp = s;

	mtx_unlock(&s->session_mtx);

	return (0);

bad:
	mtx_unlock(&s->session_mtx);

	/* Return L2CAP socket back to its original state */
	SOCKBUF_LOCK(&l2so->so_rcv);
	soupcall_clear(s->l2so, SO_RCV);
	SOCKBUF_UNLOCK(&l2so->so_rcv);
	SOCKBUF_LOCK(&l2so->so_snd);
	soupcall_clear(s->l2so, SO_SND);
	SOCKBUF_UNLOCK(&l2so->so_snd);
	l2so->so_state &= ~SS_NBIO;

	mtx_destroy(&s->session_mtx);
	bzero(s, sizeof(*s));
	free(s, M_NETGRAPH_BTSOCKET_RFCOMM);

	return (error);
} /* ng_btsocket_rfcomm_session_create */

/*
 * Process accept() on RFCOMM session
 * XXX FIXME locking for "l2so"?
 */

static int
ng_btsocket_rfcomm_session_accept(ng_btsocket_rfcomm_session_p s0)
{
	struct socket			*l2so;
	struct sockaddr_l2cap		*l2sa = NULL;
	ng_btsocket_l2cap_pcb_t		*l2pcb = NULL;
	ng_btsocket_rfcomm_session_p	 s = NULL;
	int				 error;

	mtx_assert(&ng_btsocket_rfcomm_sessions_mtx, MA_OWNED);
	mtx_assert(&s0->session_mtx, MA_OWNED);

	SOLISTEN_LOCK(s0->l2so);
	error = solisten_dequeue(s0->l2so, &l2so, 0);
	if (error == EWOULDBLOCK)
		return (error);
	if (error) {
		NG_BTSOCKET_RFCOMM_ERR(
"%s: Could not accept connection on L2CAP socket, error=%d\n", __func__, error);
		return (error);
	}

	error = soaccept(l2so, (struct sockaddr **) &l2sa);
	if (error != 0) {
		NG_BTSOCKET_RFCOMM_ERR(
"%s: soaccept() on L2CAP socket failed, error=%d\n", __func__, error);
		soclose(l2so);

		return (error);
	}

	/*
	 * Check if there is already active RFCOMM session between two devices.
	 * If so then close L2CAP connection. We only support one RFCOMM session
	 * between each pair of devices. Note that here we assume session in any
	 * state. The session even could be in the middle of disconnecting.
	 */

	l2pcb = so2l2cap_pcb(l2so);
	s = ng_btsocket_rfcomm_session_by_addr(&l2pcb->src, &l2pcb->dst);
	if (s == NULL) {
		/* Create a new RFCOMM session */
		error = ng_btsocket_rfcomm_session_create(&s, l2so, NULL, NULL,
				curthread /* XXX */);
		if (error == 0) {
			mtx_lock(&s->session_mtx);

			s->flags = 0;
			s->state = NG_BTSOCKET_RFCOMM_SESSION_CONNECTED;

			/*
			 * Adjust MTU on incoming connection. Reserve 5 bytes:
			 * RFCOMM frame header, one extra byte for length and 
			 * one extra byte for credits.
			 */

			s->mtu = min(l2pcb->imtu, l2pcb->omtu) -
					sizeof(struct rfcomm_frame_hdr) - 1 - 1;

			mtx_unlock(&s->session_mtx);
		} else {
			NG_BTSOCKET_RFCOMM_ALERT(
"%s: Failed to create new RFCOMM session, error=%d\n", __func__, error);

			soclose(l2so);
		}
	} else {
		NG_BTSOCKET_RFCOMM_WARN(
"%s: Rejecting duplicating RFCOMM session between src=%x:%x:%x:%x:%x:%x and " \
"dst=%x:%x:%x:%x:%x:%x, state=%d, flags=%#x\n",	__func__,
			l2pcb->src.b[5], l2pcb->src.b[4], l2pcb->src.b[3],
			l2pcb->src.b[2], l2pcb->src.b[1], l2pcb->src.b[0],
			l2pcb->dst.b[5], l2pcb->dst.b[4], l2pcb->dst.b[3],
			l2pcb->dst.b[2], l2pcb->dst.b[1], l2pcb->dst.b[0],
			s->state, s->flags);

		error = EBUSY;
		soclose(l2so);
	}

	return (error);
} /* ng_btsocket_rfcomm_session_accept */

/*
 * Process connect() on RFCOMM session
 * XXX FIXME locking for "l2so"?
 */

static int
ng_btsocket_rfcomm_session_connect(ng_btsocket_rfcomm_session_p s)
{
	ng_btsocket_l2cap_pcb_p	l2pcb = so2l2cap_pcb(s->l2so);
	int			error;

	mtx_assert(&s->session_mtx, MA_OWNED);

	/* First check if connection has failed */
	if ((error = s->l2so->so_error) != 0) {
		s->l2so->so_error = 0;

		NG_BTSOCKET_RFCOMM_ERR(
"%s: Could not connect RFCOMM session, error=%d, state=%d, flags=%#x\n",
			__func__, error, s->state, s->flags);

		return (error);
	}

	/* Is connection still in progress? */
	if (s->l2so->so_state & SS_ISCONNECTING)
		return (0); 

	/* 
	 * If we got here then we are connected. Send SABM on DLCI 0 to 
	 * open multiplexor channel.
	 */

	if (error == 0) {
		s->state = NG_BTSOCKET_RFCOMM_SESSION_CONNECTED;

		/*
		 * Adjust MTU on outgoing connection. Reserve 5 bytes: RFCOMM 
		 * frame header, one extra byte for length and one extra byte 
		 * for credits.
		 */

		s->mtu = min(l2pcb->imtu, l2pcb->omtu) -
				sizeof(struct rfcomm_frame_hdr) - 1 - 1;

		error = ng_btsocket_rfcomm_send_command(s,RFCOMM_FRAME_SABM,0);
		if (error == 0)
			error = ng_btsocket_rfcomm_task_wakeup();
	}

	return (error);
}/* ng_btsocket_rfcomm_session_connect */

/*
 * Receive data on RFCOMM session
 * XXX FIXME locking for "l2so"?
 */

static int
ng_btsocket_rfcomm_session_receive(ng_btsocket_rfcomm_session_p s)
{
	struct mbuf	*m = NULL;
	struct uio	 uio;
	int		 more, flags, error;

	mtx_assert(&s->session_mtx, MA_OWNED);

	/* Can we read from the L2CAP socket? */
	if (!soreadable(s->l2so))
		return (0);

	/* First check for error on L2CAP socket */
	if ((error = s->l2so->so_error) != 0) {
		s->l2so->so_error = 0;

		NG_BTSOCKET_RFCOMM_ERR(
"%s: Could not receive data from L2CAP socket, error=%d, state=%d, flags=%#x\n",
			__func__, error, s->state, s->flags);

		return (error);
	}

	/*
	 * Read all packets from the L2CAP socket. 
	 * XXX FIXME/VERIFY is that correct? For now use m->m_nextpkt as
	 * indication that there is more packets on the socket's buffer.
	 * Also what should we use in uio.uio_resid?
	 * May be s->mtu + sizeof(struct rfcomm_frame_hdr) + 1 + 1?
	 */

	for (more = 1; more; ) {
		/* Try to get next packet from socket */
		bzero(&uio, sizeof(uio));
/*		uio.uio_td = NULL; */
		uio.uio_resid = 1000000000;
		flags = MSG_DONTWAIT;

		m = NULL;
		error = soreceive(s->l2so, NULL, &uio, &m,
		    (struct mbuf **) NULL, &flags);
		if (error != 0) {
			if (error == EWOULDBLOCK)
				return (0); /* XXX can happen? */

			NG_BTSOCKET_RFCOMM_ERR(
"%s: Could not receive data from L2CAP socket, error=%d\n", __func__, error);

			return (error);
		}
	
		more = (m->m_nextpkt != NULL);
		m->m_nextpkt = NULL;

		ng_btsocket_rfcomm_receive_frame(s, m);
	}

	return (0);
} /* ng_btsocket_rfcomm_session_receive */

/*
 * Send data on RFCOMM session
 * XXX FIXME locking for "l2so"?
 */

static int
ng_btsocket_rfcomm_session_send(ng_btsocket_rfcomm_session_p s)
{
	struct mbuf	*m = NULL;
	int		 error;

	mtx_assert(&s->session_mtx, MA_OWNED);

	/* Send as much as we can from the session queue */
	while (sowriteable(s->l2so)) {
		/* Check if socket still OK */
		if ((error = s->l2so->so_error) != 0) {
			s->l2so->so_error = 0;

			NG_BTSOCKET_RFCOMM_ERR(
"%s: Detected error=%d on L2CAP socket, state=%d, flags=%#x\n",
				__func__, error, s->state, s->flags);

			return (error);
		}

		NG_BT_MBUFQ_DEQUEUE(&s->outq, m);
		if (m == NULL)
			return (0); /* we are done */

		/* Call send function on the L2CAP socket */
		error = (*s->l2so->so_proto->pr_usrreqs->pru_send)(s->l2so,
				0, m, NULL, NULL, curthread /* XXX */);
		if (error != 0) {
			NG_BTSOCKET_RFCOMM_ERR(
"%s: Could not send data to L2CAP socket, error=%d\n", __func__, error);

			return (error);
		}
	}

	return (0);
} /* ng_btsocket_rfcomm_session_send */

/*
 * Close and disconnect all DLCs for the given session. Caller must hold 
 * s->sesson_mtx. Will wakeup session.
 */

static void
ng_btsocket_rfcomm_session_clean(ng_btsocket_rfcomm_session_p s)
{
	ng_btsocket_rfcomm_pcb_p	pcb = NULL, pcb_next = NULL;
	int				error;

	mtx_assert(&s->session_mtx, MA_OWNED);

	/*
	 * Note: cannot use LIST_FOREACH because ng_btsocket_rfcomm_pcb_kill
	 * will unlink DLC from the session
	 */

	for (pcb = LIST_FIRST(&s->dlcs); pcb != NULL; ) {
		mtx_lock(&pcb->pcb_mtx);
		pcb_next = LIST_NEXT(pcb, session_next);

		NG_BTSOCKET_RFCOMM_INFO(
"%s: Disconnecting dlci=%d, state=%d, flags=%#x\n",
			__func__, pcb->dlci, pcb->state, pcb->flags);

		if (pcb->state == NG_BTSOCKET_RFCOMM_DLC_CONNECTED)
			error = ECONNRESET;
		else
			error = ECONNREFUSED;

		ng_btsocket_rfcomm_pcb_kill(pcb, error);

		mtx_unlock(&pcb->pcb_mtx);
		pcb = pcb_next;
	}
} /* ng_btsocket_rfcomm_session_clean */

/*
 * Process all DLCs on the session. Caller MUST hold s->session_mtx.
 */

static void
ng_btsocket_rfcomm_session_process_pcb(ng_btsocket_rfcomm_session_p s)
{
	ng_btsocket_rfcomm_pcb_p	pcb = NULL, pcb_next = NULL;
	int				error;

	mtx_assert(&s->session_mtx, MA_OWNED);

	/*
	 * Note: cannot use LIST_FOREACH because ng_btsocket_rfcomm_pcb_kill
	 * will unlink DLC from the session
	 */

	for (pcb = LIST_FIRST(&s->dlcs); pcb != NULL; ) {
		mtx_lock(&pcb->pcb_mtx);
		pcb_next = LIST_NEXT(pcb, session_next);

		switch (pcb->state) {

		/*
		 * If DLC in W4_CONNECT state then we should check for both
		 * timeout and detach.
		 */

		case NG_BTSOCKET_RFCOMM_DLC_W4_CONNECT:
			if (pcb->flags & NG_BTSOCKET_RFCOMM_DLC_DETACHED)
				ng_btsocket_rfcomm_pcb_kill(pcb, 0);
			else if (pcb->flags & NG_BTSOCKET_RFCOMM_DLC_TIMEDOUT)
				ng_btsocket_rfcomm_pcb_kill(pcb, ETIMEDOUT);
			break;

		/*
		 * If DLC in CONFIGURING or CONNECTING state then we only
		 * should check for timeout. If detach() was called then
		 * DLC will be moved into DISCONNECTING state.
		 */

		case NG_BTSOCKET_RFCOMM_DLC_CONFIGURING:
		case NG_BTSOCKET_RFCOMM_DLC_CONNECTING:
			if (pcb->flags & NG_BTSOCKET_RFCOMM_DLC_TIMEDOUT)
				ng_btsocket_rfcomm_pcb_kill(pcb, ETIMEDOUT);
			break;

		/*
		 * If DLC in CONNECTED state then we need to send data (if any)
		 * from the socket's send queue. Note that we will send data
		 * from either all sockets or none. This may overload session's
		 * outgoing queue (but we do not check for that).
		 *
 		 * XXX FIXME need scheduler for RFCOMM sockets
		 */

		case NG_BTSOCKET_RFCOMM_DLC_CONNECTED:
			error = ng_btsocket_rfcomm_pcb_send(pcb, ALOT);
			if (error != 0)
				ng_btsocket_rfcomm_pcb_kill(pcb, error);
			break;

		/*
		 * If DLC in DISCONNECTING state then we must send DISC frame.
		 * Note that if DLC has timeout set then we do not need to 
		 * resend DISC frame.
		 *
		 * XXX FIXME need to drain all data from the socket's queue
		 * if LINGER option was set
		 */

		case NG_BTSOCKET_RFCOMM_DLC_DISCONNECTING:
			if (!(pcb->flags & NG_BTSOCKET_RFCOMM_DLC_TIMO)) {
				error = ng_btsocket_rfcomm_send_command(
						pcb->session, RFCOMM_FRAME_DISC,
						pcb->dlci);
				if (error == 0)
					ng_btsocket_rfcomm_timeout(pcb);
				else
					ng_btsocket_rfcomm_pcb_kill(pcb, error);
			} else if (pcb->flags & NG_BTSOCKET_RFCOMM_DLC_TIMEDOUT)
				ng_btsocket_rfcomm_pcb_kill(pcb, ETIMEDOUT);
			break;
		
/*		case NG_BTSOCKET_RFCOMM_DLC_CLOSED: */
		default:
			panic("%s: Invalid DLC state=%d, flags=%#x\n",
				__func__, pcb->state, pcb->flags);
			break;
		}

		mtx_unlock(&pcb->pcb_mtx);
		pcb = pcb_next;
	}
} /* ng_btsocket_rfcomm_session_process_pcb */

/*
 * Find RFCOMM session between "src" and "dst".
 * Caller MUST hold ng_btsocket_rfcomm_sessions_mtx.
 */

static ng_btsocket_rfcomm_session_p
ng_btsocket_rfcomm_session_by_addr(bdaddr_p src, bdaddr_p dst)
{
	ng_btsocket_rfcomm_session_p	s = NULL;
	ng_btsocket_l2cap_pcb_p		l2pcb = NULL;
	int				any_src;

	mtx_assert(&ng_btsocket_rfcomm_sessions_mtx, MA_OWNED);

	any_src = (bcmp(src, NG_HCI_BDADDR_ANY, sizeof(*src)) == 0);

	LIST_FOREACH(s, &ng_btsocket_rfcomm_sessions, next) {
		l2pcb = so2l2cap_pcb(s->l2so);

		if ((any_src || bcmp(&l2pcb->src, src, sizeof(*src)) == 0) &&
		    bcmp(&l2pcb->dst, dst, sizeof(*dst)) == 0)
			break;
	}

	return (s);
} /* ng_btsocket_rfcomm_session_by_addr */

/*****************************************************************************
 *****************************************************************************
 **                                  RFCOMM 
 *****************************************************************************
 *****************************************************************************/

/*
 * Process incoming RFCOMM frame. Caller must hold s->session_mtx.
 * XXX FIXME check frame length
 */

static int
ng_btsocket_rfcomm_receive_frame(ng_btsocket_rfcomm_session_p s,
		struct mbuf *m0)
{
	struct rfcomm_frame_hdr	*hdr = NULL;
	struct mbuf		*m = NULL;
	u_int16_t		 length;
	u_int8_t		 dlci, type;
	int			 error = 0;

	mtx_assert(&s->session_mtx, MA_OWNED);

	/* Pullup as much as we can into first mbuf (for direct access) */
	length = min(m0->m_pkthdr.len, MHLEN);
	if (m0->m_len < length) {
		if ((m0 = m_pullup(m0, length)) == NULL) {
			NG_BTSOCKET_RFCOMM_ALERT(
"%s: m_pullup(%d) failed\n", __func__, length);

			return (ENOBUFS);
		}
	}

	hdr = mtod(m0, struct rfcomm_frame_hdr *);
	dlci = RFCOMM_DLCI(hdr->address);
	type = RFCOMM_TYPE(hdr->control);

	/* Test EA bit in length. If not set then we have 2 bytes of length */
	if (!RFCOMM_EA(hdr->length)) {
		bcopy(&hdr->length, &length, sizeof(length));
		length = le16toh(length) >> 1;
		m_adj(m0, sizeof(*hdr) + 1);
	} else {
		length = hdr->length >> 1;
		m_adj(m0, sizeof(*hdr));
	}

	NG_BTSOCKET_RFCOMM_INFO(
"%s: Got frame type=%#x, dlci=%d, length=%d, cr=%d, pf=%d, len=%d\n",
		__func__, type, dlci, length, RFCOMM_CR(hdr->address),
		RFCOMM_PF(hdr->control), m0->m_pkthdr.len);

	/*
	 * Get FCS (the last byte in the frame)
	 * XXX this will not work if mbuf chain ends with empty mbuf.
	 * XXX let's hope it never happens :)
	 */

	for (m = m0; m->m_next != NULL; m = m->m_next)
		;
	if (m->m_len <= 0)
		panic("%s: Empty mbuf at the end of the chain, len=%d\n",
			__func__, m->m_len);

	/*
	 * Check FCS. We only need to calculate FCS on first 2 or 3 bytes
	 * and already m_pullup'ed mbuf chain, so it should be safe.
	 */

	if (ng_btsocket_rfcomm_check_fcs((u_int8_t *) hdr, type, m->m_data[m->m_len - 1])) {
		NG_BTSOCKET_RFCOMM_ERR(
"%s: Invalid RFCOMM packet. Bad checksum\n", __func__);
		NG_FREE_M(m0);

		return (EINVAL);
	}

	m_adj(m0, -1); /* Trim FCS byte */

	/*
	 * Process RFCOMM frame.
	 *
	 * From TS 07.10 spec
	 * 
	 * "... In the case where a SABM or DISC command with the P bit set
	 * to 0 is received then the received frame shall be discarded..."
 	 *
	 * "... If a unsolicited DM response is received then the frame shall
	 * be processed irrespective of the P/F setting... "
	 *
	 * "... The station may transmit response frames with the F bit set 
	 * to 0 at any opportunity on an asynchronous basis. However, in the 
	 * case where a UA response is received with the F bit set to 0 then 
	 * the received frame shall be discarded..."
	 *
	 * From Bluetooth spec
	 *
	 * "... When credit based flow control is being used, the meaning of
	 * the P/F bit in the control field of the RFCOMM header is redefined
	 * for UIH frames..."
	 */

	switch (type) {
	case RFCOMM_FRAME_SABM:
		if (RFCOMM_PF(hdr->control))
			error = ng_btsocket_rfcomm_receive_sabm(s, dlci);
		break;

	case RFCOMM_FRAME_DISC:
		if (RFCOMM_PF(hdr->control))
			error = ng_btsocket_rfcomm_receive_disc(s, dlci);
		break;

	case RFCOMM_FRAME_UA:
		if (RFCOMM_PF(hdr->control))
			error = ng_btsocket_rfcomm_receive_ua(s, dlci);
		break;

	case RFCOMM_FRAME_DM:
		error = ng_btsocket_rfcomm_receive_dm(s, dlci);
		break;

	case RFCOMM_FRAME_UIH:
		if (dlci == 0)
			error = ng_btsocket_rfcomm_receive_mcc(s, m0);
		else
			error = ng_btsocket_rfcomm_receive_uih(s, dlci,
					RFCOMM_PF(hdr->control), m0);

		return (error);
		/* NOT REACHED */

	default:
		NG_BTSOCKET_RFCOMM_ERR(
"%s: Invalid RFCOMM packet. Unknown type=%#x\n", __func__, type);
		error = EINVAL;
		break;
	}

	NG_FREE_M(m0);

	return (error);
} /* ng_btsocket_rfcomm_receive_frame */

/*
 * Process RFCOMM SABM frame
 */

static int
ng_btsocket_rfcomm_receive_sabm(ng_btsocket_rfcomm_session_p s, int dlci)
{
	ng_btsocket_rfcomm_pcb_p	pcb = NULL;
	int				error = 0;

	mtx_assert(&s->session_mtx, MA_OWNED);

	NG_BTSOCKET_RFCOMM_INFO(
"%s: Got SABM, session state=%d, flags=%#x, mtu=%d, dlci=%d\n",
		__func__, s->state, s->flags, s->mtu, dlci);

	/* DLCI == 0 means open multiplexor channel */
	if (dlci == 0) {
		switch (s->state) {
		case NG_BTSOCKET_RFCOMM_SESSION_CONNECTED:
		case NG_BTSOCKET_RFCOMM_SESSION_OPEN:
			error = ng_btsocket_rfcomm_send_command(s,
					RFCOMM_FRAME_UA, dlci);
			if (error == 0) {
				s->state = NG_BTSOCKET_RFCOMM_SESSION_OPEN;
				ng_btsocket_rfcomm_connect_cfm(s);
			} else {
				s->state = NG_BTSOCKET_RFCOMM_SESSION_CLOSED;
				ng_btsocket_rfcomm_session_clean(s);
			}
			break;

		default:
			NG_BTSOCKET_RFCOMM_WARN(
"%s: Got SABM for session in invalid state state=%d, flags=%#x\n",
				__func__, s->state, s->flags);
			error = EINVAL;
			break;
		}

		return (error);
	}

	/* Make sure multiplexor channel is open */
	if (s->state != NG_BTSOCKET_RFCOMM_SESSION_OPEN) {
		NG_BTSOCKET_RFCOMM_ERR(
"%s: Got SABM for dlci=%d with mulitplexor channel closed, state=%d, " \
"flags=%#x\n",		__func__, dlci, s->state, s->flags);

		return (EINVAL);
	}

	/*
	 * Check if we have this DLCI. This might happen when remote
	 * peer uses PN command before actual open (SABM) happens.
	 */

	pcb = ng_btsocket_rfcomm_pcb_by_dlci(s, dlci);
	if (pcb != NULL) {
		mtx_lock(&pcb->pcb_mtx);

		if (pcb->state != NG_BTSOCKET_RFCOMM_DLC_CONNECTING) {
			NG_BTSOCKET_RFCOMM_ERR(
"%s: Got SABM for dlci=%d in invalid state=%d, flags=%#x\n",
				__func__, dlci, pcb->state, pcb->flags);
			mtx_unlock(&pcb->pcb_mtx);

			return (ENOENT);
		}

		ng_btsocket_rfcomm_untimeout(pcb);

		error = ng_btsocket_rfcomm_send_command(s,RFCOMM_FRAME_UA,dlci);
		if (error == 0)
			error = ng_btsocket_rfcomm_send_msc(pcb);

		if (error == 0) {
			pcb->state = NG_BTSOCKET_RFCOMM_DLC_CONNECTED;
			soisconnected(pcb->so);
		} else
			ng_btsocket_rfcomm_pcb_kill(pcb, error);

		mtx_unlock(&pcb->pcb_mtx);

		return (error);
	}

	/*
	 * We do not have requested DLCI, so it must be an incoming connection
	 * with default parameters. Try to accept it.
	 */ 

	pcb = ng_btsocket_rfcomm_connect_ind(s, RFCOMM_SRVCHANNEL(dlci));
	if (pcb != NULL) {
		mtx_lock(&pcb->pcb_mtx);

		pcb->dlci = dlci;

		error = ng_btsocket_rfcomm_send_command(s,RFCOMM_FRAME_UA,dlci);
		if (error == 0)
			error = ng_btsocket_rfcomm_send_msc(pcb);

		if (error == 0) {
			pcb->state = NG_BTSOCKET_RFCOMM_DLC_CONNECTED;
			soisconnected(pcb->so);
		} else
			ng_btsocket_rfcomm_pcb_kill(pcb, error);

		mtx_unlock(&pcb->pcb_mtx);
	} else
		/* Nobody is listen()ing on the requested DLCI */
		error = ng_btsocket_rfcomm_send_command(s,RFCOMM_FRAME_DM,dlci);

	return (error);
} /* ng_btsocket_rfcomm_receive_sabm */

/*
 * Process RFCOMM DISC frame
 */

static int
ng_btsocket_rfcomm_receive_disc(ng_btsocket_rfcomm_session_p s, int dlci)
{
	ng_btsocket_rfcomm_pcb_p	pcb = NULL;
	int				error = 0;

	mtx_assert(&s->session_mtx, MA_OWNED);

	NG_BTSOCKET_RFCOMM_INFO(
"%s: Got DISC, session state=%d, flags=%#x, mtu=%d, dlci=%d\n",
		__func__, s->state, s->flags, s->mtu, dlci);

	/* DLCI == 0 means close multiplexor channel */
	if (dlci == 0) {
		/* XXX FIXME assume that remote side will close the socket */
		error = ng_btsocket_rfcomm_send_command(s, RFCOMM_FRAME_UA, 0);
		if (error == 0) {
			if (s->state == NG_BTSOCKET_RFCOMM_SESSION_DISCONNECTING)
				s->state = NG_BTSOCKET_RFCOMM_SESSION_CLOSED; /* XXX */
			else
				s->state = NG_BTSOCKET_RFCOMM_SESSION_DISCONNECTING;
		} else
			s->state = NG_BTSOCKET_RFCOMM_SESSION_CLOSED; /* XXX */

		ng_btsocket_rfcomm_session_clean(s);
	} else {
		pcb = ng_btsocket_rfcomm_pcb_by_dlci(s, dlci);
		if (pcb != NULL) {
			int	err;

			mtx_lock(&pcb->pcb_mtx);

			NG_BTSOCKET_RFCOMM_INFO(
"%s: Got DISC for dlci=%d, state=%d, flags=%#x\n",
				__func__, dlci, pcb->state, pcb->flags);

			error = ng_btsocket_rfcomm_send_command(s,
					RFCOMM_FRAME_UA, dlci);

			if (pcb->state == NG_BTSOCKET_RFCOMM_DLC_CONNECTED)
				err = 0;
			else
				err = ECONNREFUSED;

			ng_btsocket_rfcomm_pcb_kill(pcb, err);

			mtx_unlock(&pcb->pcb_mtx);
		} else {
			NG_BTSOCKET_RFCOMM_WARN(
"%s: Got DISC for non-existing dlci=%d\n", __func__, dlci);

			error = ng_btsocket_rfcomm_send_command(s,
					RFCOMM_FRAME_DM, dlci);
		}
	}

	return (error);
} /* ng_btsocket_rfcomm_receive_disc */

/*
 * Process RFCOMM UA frame
 */

static int
ng_btsocket_rfcomm_receive_ua(ng_btsocket_rfcomm_session_p s, int dlci)
{
	ng_btsocket_rfcomm_pcb_p	pcb = NULL;
	int				error = 0;

	mtx_assert(&s->session_mtx, MA_OWNED);

	NG_BTSOCKET_RFCOMM_INFO(
"%s: Got UA, session state=%d, flags=%#x, mtu=%d, dlci=%d\n",
		__func__, s->state, s->flags, s->mtu, dlci);

	/* dlci == 0 means multiplexor channel */
	if (dlci == 0) {
		switch (s->state) {
		case NG_BTSOCKET_RFCOMM_SESSION_CONNECTED:
			s->state = NG_BTSOCKET_RFCOMM_SESSION_OPEN;
			ng_btsocket_rfcomm_connect_cfm(s);
			break;

		case NG_BTSOCKET_RFCOMM_SESSION_DISCONNECTING:
			s->state = NG_BTSOCKET_RFCOMM_SESSION_CLOSED;
			ng_btsocket_rfcomm_session_clean(s);
			break;

		default:
			NG_BTSOCKET_RFCOMM_WARN(
"%s: Got UA for session in invalid state=%d(%d), flags=%#x, mtu=%d\n",
				__func__, s->state, INITIATOR(s), s->flags,
				s->mtu);
			error = ENOENT;
			break;
		}

		return (error);
	}

	/* Check if we have this DLCI */
	pcb = ng_btsocket_rfcomm_pcb_by_dlci(s, dlci);
	if (pcb != NULL) {
		mtx_lock(&pcb->pcb_mtx);

		NG_BTSOCKET_RFCOMM_INFO(
"%s: Got UA for dlci=%d, state=%d, flags=%#x\n",
			__func__, dlci, pcb->state, pcb->flags);

		switch (pcb->state) {
		case NG_BTSOCKET_RFCOMM_DLC_CONNECTING:
			ng_btsocket_rfcomm_untimeout(pcb);

			error = ng_btsocket_rfcomm_send_msc(pcb);
			if (error == 0) {
				pcb->state = NG_BTSOCKET_RFCOMM_DLC_CONNECTED;
				soisconnected(pcb->so);
			}
			break;

		case NG_BTSOCKET_RFCOMM_DLC_DISCONNECTING:
			ng_btsocket_rfcomm_pcb_kill(pcb, 0);
			break;

		default:
			NG_BTSOCKET_RFCOMM_WARN(
"%s: Got UA for dlci=%d in invalid state=%d, flags=%#x\n",
				__func__, dlci, pcb->state, pcb->flags);
			error = ENOENT;
			break;
		}

		mtx_unlock(&pcb->pcb_mtx);
	} else {
		NG_BTSOCKET_RFCOMM_WARN(
"%s: Got UA for non-existing dlci=%d\n", __func__, dlci);

		error = ng_btsocket_rfcomm_send_command(s,RFCOMM_FRAME_DM,dlci);
	}

	return (error);
} /* ng_btsocket_rfcomm_receive_ua */

/*
 * Process RFCOMM DM frame
 */

static int
ng_btsocket_rfcomm_receive_dm(ng_btsocket_rfcomm_session_p s, int dlci)
{
	ng_btsocket_rfcomm_pcb_p	pcb = NULL;
	int				error;

	mtx_assert(&s->session_mtx, MA_OWNED);

	NG_BTSOCKET_RFCOMM_INFO(
"%s: Got DM, session state=%d, flags=%#x, mtu=%d, dlci=%d\n",
		__func__, s->state, s->flags, s->mtu, dlci);

	/* DLCI == 0 means multiplexor channel */
	if (dlci == 0) {
		/* Disconnect all dlc's on the session */
		s->state = NG_BTSOCKET_RFCOMM_SESSION_CLOSED;
		ng_btsocket_rfcomm_session_clean(s);
	} else {
		pcb = ng_btsocket_rfcomm_pcb_by_dlci(s, dlci);
		if (pcb != NULL) {
			mtx_lock(&pcb->pcb_mtx);

			NG_BTSOCKET_RFCOMM_INFO(
"%s: Got DM for dlci=%d, state=%d, flags=%#x\n",
				__func__, dlci, pcb->state, pcb->flags);

			if (pcb->state == NG_BTSOCKET_RFCOMM_DLC_CONNECTED)
				error = ECONNRESET;
			else
				error = ECONNREFUSED;

			ng_btsocket_rfcomm_pcb_kill(pcb, error);

			mtx_unlock(&pcb->pcb_mtx);
		} else
			NG_BTSOCKET_RFCOMM_WARN(
"%s: Got DM for non-existing dlci=%d\n", __func__, dlci);
	}

	return (0);
} /* ng_btsocket_rfcomm_receive_dm */

/*
 * Process RFCOMM UIH frame (data)
 */

static int
ng_btsocket_rfcomm_receive_uih(ng_btsocket_rfcomm_session_p s, int dlci,
		int pf, struct mbuf *m0)
{
	ng_btsocket_rfcomm_pcb_p	pcb = NULL;
	int				error = 0;

	mtx_assert(&s->session_mtx, MA_OWNED);

	NG_BTSOCKET_RFCOMM_INFO(
"%s: Got UIH, session state=%d, flags=%#x, mtu=%d, dlci=%d, pf=%d, len=%d\n",
		__func__, s->state, s->flags, s->mtu, dlci, pf,
		m0->m_pkthdr.len);

	/* XXX should we do it here? Check for session flow control */
	if (s->flags & NG_BTSOCKET_RFCOMM_SESSION_LFC) {
		NG_BTSOCKET_RFCOMM_WARN(
"%s: Got UIH with session flow control asserted, state=%d, flags=%#x\n",
			__func__, s->state, s->flags);
		goto drop;
	}

	/* Check if we have this dlci */
	pcb = ng_btsocket_rfcomm_pcb_by_dlci(s, dlci);
	if (pcb == NULL) {
		NG_BTSOCKET_RFCOMM_WARN(
"%s: Got UIH for non-existing dlci=%d\n", __func__, dlci);
		error = ng_btsocket_rfcomm_send_command(s,RFCOMM_FRAME_DM,dlci);
		goto drop;
	}

	mtx_lock(&pcb->pcb_mtx);

	/* Check dlci state */	
	if (pcb->state != NG_BTSOCKET_RFCOMM_DLC_CONNECTED) {
		NG_BTSOCKET_RFCOMM_WARN(
"%s: Got UIH for dlci=%d in invalid state=%d, flags=%#x\n",
			__func__, dlci, pcb->state, pcb->flags);
		error = EINVAL;
		goto drop1;
	}

	/* Check dlci flow control */
	if (((pcb->flags & NG_BTSOCKET_RFCOMM_DLC_CFC) && pcb->rx_cred <= 0) ||
	     (pcb->lmodem & RFCOMM_MODEM_FC)) {
		NG_BTSOCKET_RFCOMM_ERR(
"%s: Got UIH for dlci=%d with asserted flow control, state=%d, " \
"flags=%#x, rx_cred=%d, lmodem=%#x\n",
			__func__, dlci, pcb->state, pcb->flags,
			pcb->rx_cred, pcb->lmodem);
		goto drop1;
	}

	/* Did we get any credits? */
	if ((pcb->flags & NG_BTSOCKET_RFCOMM_DLC_CFC) && pf) {
		NG_BTSOCKET_RFCOMM_INFO(
"%s: Got %d more credits for dlci=%d, state=%d, flags=%#x, " \
"rx_cred=%d, tx_cred=%d\n",
			__func__, *mtod(m0, u_int8_t *), dlci, pcb->state, 
			pcb->flags, pcb->rx_cred, pcb->tx_cred);

		pcb->tx_cred += *mtod(m0, u_int8_t *);
		m_adj(m0, 1);

		/* Send more from the DLC. XXX check for errors? */
		ng_btsocket_rfcomm_pcb_send(pcb, ALOT);
	} 

	/* OK the of the rest of the mbuf is the data */
	if (m0->m_pkthdr.len > 0) {
		/* If we are using credit flow control decrease rx_cred here */
		if (pcb->flags & NG_BTSOCKET_RFCOMM_DLC_CFC) {
			/* Give remote peer more credits (if needed) */
			if (-- pcb->rx_cred <= RFCOMM_MAX_CREDITS / 2)
				ng_btsocket_rfcomm_send_credits(pcb);
			else
				NG_BTSOCKET_RFCOMM_INFO(
"%s: Remote side still has credits, dlci=%d, state=%d, flags=%#x, " \
"rx_cred=%d, tx_cred=%d\n",		__func__, dlci, pcb->state, pcb->flags,
					pcb->rx_cred, pcb->tx_cred);
		}
		
		/* Check packet against mtu on dlci */
		if (m0->m_pkthdr.len > pcb->mtu) {
			NG_BTSOCKET_RFCOMM_ERR(
"%s: Got oversized UIH for dlci=%d, state=%d, flags=%#x, mtu=%d, len=%d\n",
				__func__, dlci, pcb->state, pcb->flags,
				pcb->mtu, m0->m_pkthdr.len);

			error = EMSGSIZE;
		} else if (m0->m_pkthdr.len > sbspace(&pcb->so->so_rcv)) {
 
			/*
			 * This is really bad. Receive queue on socket does
			 * not have enough space for the packet. We do not
			 * have any other choice but drop the packet. 
			 */
 
			NG_BTSOCKET_RFCOMM_ERR(
"%s: Not enough space in socket receive queue. Dropping UIH for dlci=%d, " \
"state=%d, flags=%#x, len=%d, space=%ld\n",
				__func__, dlci, pcb->state, pcb->flags,
				m0->m_pkthdr.len, sbspace(&pcb->so->so_rcv));

			error = ENOBUFS;
		} else {
			/* Append packet to the socket receive queue */
			sbappend(&pcb->so->so_rcv, m0, 0);
			m0 = NULL;

			sorwakeup(pcb->so);
		}
	}
drop1:
	mtx_unlock(&pcb->pcb_mtx);
drop:
	NG_FREE_M(m0); /* checks for != NULL */

	return (error);
} /* ng_btsocket_rfcomm_receive_uih */

/*
 * Process RFCOMM MCC command (Multiplexor)
 * 
 * From TS 07.10 spec
 *
 * "5.4.3.1 Information Data
 * 
 *  ...The frames (UIH) sent by the initiating station have the C/R bit set 
 *  to 1 and those sent by the responding station have the C/R bit set to 0..."
 *
 * "5.4.6.2 Operating procedures
 *
 *  Messages always exist in pairs; a command message and a corresponding 
 *  response message. If the C/R bit is set to 1 the message is a command, 
 *  if it is set to 0 the message is a response...
 *
 *  ...
 * 
 *  NOTE: Notice that when UIH frames are used to convey information on DLCI 0
 *  there are at least two different fields that contain a C/R bit, and the 
 *  bits are set of different form. The C/R bit in the Type field shall be set
 *  as it is stated above, while the C/R bit in the Address field (see subclause
 *  5.2.1.2) shall be set as it is described in subclause 5.4.3.1."
 */

static int
ng_btsocket_rfcomm_receive_mcc(ng_btsocket_rfcomm_session_p s, struct mbuf *m0)
{
	struct rfcomm_mcc_hdr	*hdr = NULL;
	u_int8_t		 cr, type, length;

	mtx_assert(&s->session_mtx, MA_OWNED);

	/*
	 * We can access data directly in the first mbuf, because we have
	 * m_pullup()'ed mbuf chain in ng_btsocket_rfcomm_receive_frame().
	 * All MCC commands should fit into single mbuf (except probably TEST).
	 */

	hdr = mtod(m0, struct rfcomm_mcc_hdr *);
	cr = RFCOMM_CR(hdr->type);
	type = RFCOMM_MCC_TYPE(hdr->type);
	length = RFCOMM_MCC_LENGTH(hdr->length);

	/* Check MCC frame length */
	if (sizeof(*hdr) + length != m0->m_pkthdr.len) {
		NG_BTSOCKET_RFCOMM_ERR(
"%s: Invalid MCC frame length=%d, len=%d\n",
			__func__, length, m0->m_pkthdr.len);
		NG_FREE_M(m0);

		return (EMSGSIZE);
	}

	switch (type) {
	case RFCOMM_MCC_TEST:
		return (ng_btsocket_rfcomm_receive_test(s, m0));
		/* NOT REACHED */

	case RFCOMM_MCC_FCON:
	case RFCOMM_MCC_FCOFF:
		return (ng_btsocket_rfcomm_receive_fc(s, m0));
		/* NOT REACHED */

	case RFCOMM_MCC_MSC:
		return (ng_btsocket_rfcomm_receive_msc(s, m0));
		/* NOT REACHED */

	case RFCOMM_MCC_RPN:
		return (ng_btsocket_rfcomm_receive_rpn(s, m0));
		/* NOT REACHED */

	case RFCOMM_MCC_RLS:
		return (ng_btsocket_rfcomm_receive_rls(s, m0));
		/* NOT REACHED */

	case RFCOMM_MCC_PN:
		return (ng_btsocket_rfcomm_receive_pn(s, m0));
		/* NOT REACHED */

	case RFCOMM_MCC_NSC:
		NG_BTSOCKET_RFCOMM_ERR(
"%s: Got MCC NSC, type=%#x, cr=%d, length=%d, session state=%d, flags=%#x, " \
"mtu=%d, len=%d\n",	__func__, RFCOMM_MCC_TYPE(*((u_int8_t *)(hdr + 1))), cr,
			 length, s->state, s->flags, s->mtu, m0->m_pkthdr.len);
		NG_FREE_M(m0);
		break;

	default:
		NG_BTSOCKET_RFCOMM_ERR(
"%s: Got unknown MCC, type=%#x, cr=%d, length=%d, session state=%d, " \
"flags=%#x, mtu=%d, len=%d\n",
			__func__, type, cr, length, s->state, s->flags,
			s->mtu, m0->m_pkthdr.len);

		/* Reuse mbuf to send NSC */
		hdr = mtod(m0, struct rfcomm_mcc_hdr *);
		m0->m_pkthdr.len = m0->m_len = sizeof(*hdr);

		/* Create MCC NSC header */
		hdr->type = RFCOMM_MKMCC_TYPE(0, RFCOMM_MCC_NSC);
		hdr->length = RFCOMM_MKLEN8(1);

		/* Put back MCC command type we did not like */
		m0->m_data[m0->m_len] = RFCOMM_MKMCC_TYPE(cr, type);
		m0->m_pkthdr.len ++;
		m0->m_len ++;

		/* Send UIH frame */
		return (ng_btsocket_rfcomm_send_uih(s,
				RFCOMM_MKADDRESS(INITIATOR(s), 0), 0, 0, m0));
		/* NOT REACHED */
	}

	return (0);
} /* ng_btsocket_rfcomm_receive_mcc */

/*
 * Receive RFCOMM TEST MCC command
 */

static int
ng_btsocket_rfcomm_receive_test(ng_btsocket_rfcomm_session_p s, struct mbuf *m0)
{
	struct rfcomm_mcc_hdr	*hdr = mtod(m0, struct rfcomm_mcc_hdr *);
	int			 error = 0;

	mtx_assert(&s->session_mtx, MA_OWNED);

	NG_BTSOCKET_RFCOMM_INFO(
"%s: Got MCC TEST, cr=%d, length=%d, session state=%d, flags=%#x, mtu=%d, " \
"len=%d\n",	__func__, RFCOMM_CR(hdr->type), RFCOMM_MCC_LENGTH(hdr->length),
		s->state, s->flags, s->mtu, m0->m_pkthdr.len);

	if (RFCOMM_CR(hdr->type)) {
		hdr->type = RFCOMM_MKMCC_TYPE(0, RFCOMM_MCC_TEST);
		error = ng_btsocket_rfcomm_send_uih(s,
				RFCOMM_MKADDRESS(INITIATOR(s), 0), 0, 0, m0);
	} else
		NG_FREE_M(m0); /* XXX ignore response */

	return (error);
} /* ng_btsocket_rfcomm_receive_test */

/*
 * Receive RFCOMM FCON/FCOFF MCC command
 */

static int
ng_btsocket_rfcomm_receive_fc(ng_btsocket_rfcomm_session_p s, struct mbuf *m0)
{
	struct rfcomm_mcc_hdr	*hdr = mtod(m0, struct rfcomm_mcc_hdr *);
	u_int8_t		 type = RFCOMM_MCC_TYPE(hdr->type);
	int			 error = 0;

	mtx_assert(&s->session_mtx, MA_OWNED);

	/*
	 * Turn ON/OFF aggregate flow on the entire session. When remote peer 
	 * asserted flow control no transmission shall occur except on dlci 0
	 * (control channel).
	 */

	NG_BTSOCKET_RFCOMM_INFO(
"%s: Got MCC FC%s, cr=%d, length=%d, session state=%d, flags=%#x, mtu=%d, " \
"len=%d\n",	__func__, (type == RFCOMM_MCC_FCON)? "ON" : "OFF",
		RFCOMM_CR(hdr->type), RFCOMM_MCC_LENGTH(hdr->length),
		s->state, s->flags, s->mtu, m0->m_pkthdr.len);

	if (RFCOMM_CR(hdr->type)) {
		if (type == RFCOMM_MCC_FCON)
			s->flags &= ~NG_BTSOCKET_RFCOMM_SESSION_RFC;
		else
			s->flags |= NG_BTSOCKET_RFCOMM_SESSION_RFC;

		hdr->type = RFCOMM_MKMCC_TYPE(0, type);
		error = ng_btsocket_rfcomm_send_uih(s,
				RFCOMM_MKADDRESS(INITIATOR(s), 0), 0, 0, m0);
	} else
		NG_FREE_M(m0); /* XXX ignore response */

	return (error);
} /* ng_btsocket_rfcomm_receive_fc  */

/*
 * Receive RFCOMM MSC MCC command
 */

static int
ng_btsocket_rfcomm_receive_msc(ng_btsocket_rfcomm_session_p s, struct mbuf *m0)
{
	struct rfcomm_mcc_hdr		*hdr = mtod(m0, struct rfcomm_mcc_hdr*);
	struct rfcomm_mcc_msc		*msc = (struct rfcomm_mcc_msc *)(hdr+1);
	ng_btsocket_rfcomm_pcb_t	*pcb = NULL;
	int				 error = 0;

	mtx_assert(&s->session_mtx, MA_OWNED);

	NG_BTSOCKET_RFCOMM_INFO(
"%s: Got MCC MSC, dlci=%d, cr=%d, length=%d, session state=%d, flags=%#x, " \
"mtu=%d, len=%d\n",
		__func__,  RFCOMM_DLCI(msc->address), RFCOMM_CR(hdr->type),
		RFCOMM_MCC_LENGTH(hdr->length), s->state, s->flags,
		s->mtu, m0->m_pkthdr.len);

	if (RFCOMM_CR(hdr->type)) {
		pcb = ng_btsocket_rfcomm_pcb_by_dlci(s, RFCOMM_DLCI(msc->address));
		if (pcb == NULL) {
			NG_BTSOCKET_RFCOMM_WARN(
"%s: Got MSC command for non-existing dlci=%d\n",
				__func__, RFCOMM_DLCI(msc->address));
			NG_FREE_M(m0);

			return (ENOENT);
		}

		mtx_lock(&pcb->pcb_mtx);

		if (pcb->state != NG_BTSOCKET_RFCOMM_DLC_CONNECTING &&
		    pcb->state != NG_BTSOCKET_RFCOMM_DLC_CONNECTED) {
			NG_BTSOCKET_RFCOMM_WARN(
"%s: Got MSC on dlci=%d in invalid state=%d\n",
				__func__, RFCOMM_DLCI(msc->address),
				pcb->state);

			mtx_unlock(&pcb->pcb_mtx);
			NG_FREE_M(m0);

			return (EINVAL);
		}

		pcb->rmodem = msc->modem; /* Update remote port signals */

		hdr->type = RFCOMM_MKMCC_TYPE(0, RFCOMM_MCC_MSC);
		error = ng_btsocket_rfcomm_send_uih(s,
				RFCOMM_MKADDRESS(INITIATOR(s), 0), 0, 0, m0);

#if 0 /* YYY */
		/* Send more data from DLC. XXX check for errors? */
		if (!(pcb->rmodem & RFCOMM_MODEM_FC) &&
		    !(pcb->flags & NG_BTSOCKET_RFCOMM_DLC_CFC))
			ng_btsocket_rfcomm_pcb_send(pcb, ALOT);
#endif /* YYY */

		mtx_unlock(&pcb->pcb_mtx);
	} else
		NG_FREE_M(m0); /* XXX ignore response */

	return (error);
} /* ng_btsocket_rfcomm_receive_msc */

/*
 * Receive RFCOMM RPN MCC command
 * XXX FIXME do we need htole16/le16toh for RPN param_mask?
 */

static int
ng_btsocket_rfcomm_receive_rpn(ng_btsocket_rfcomm_session_p s, struct mbuf *m0)
{
	struct rfcomm_mcc_hdr	*hdr = mtod(m0, struct rfcomm_mcc_hdr *);
	struct rfcomm_mcc_rpn	*rpn = (struct rfcomm_mcc_rpn *)(hdr + 1);
	int			 error = 0;
	u_int16_t		 param_mask;
	u_int8_t		 bit_rate, data_bits, stop_bits, parity,
				 flow_control, xon_char, xoff_char;

	mtx_assert(&s->session_mtx, MA_OWNED);

	NG_BTSOCKET_RFCOMM_INFO(
"%s: Got MCC RPN, dlci=%d, cr=%d, length=%d, session state=%d, flags=%#x, " \
"mtu=%d, len=%d\n",
		__func__, RFCOMM_DLCI(rpn->dlci), RFCOMM_CR(hdr->type),
		RFCOMM_MCC_LENGTH(hdr->length), s->state, s->flags,
		s->mtu, m0->m_pkthdr.len);

	if (RFCOMM_CR(hdr->type)) {
		param_mask = RFCOMM_RPN_PM_ALL;

		if (RFCOMM_MCC_LENGTH(hdr->length) == 1) {
			/* Request - return default setting */
			bit_rate = RFCOMM_RPN_BR_115200;
			data_bits = RFCOMM_RPN_DATA_8;
			stop_bits = RFCOMM_RPN_STOP_1;
			parity = RFCOMM_RPN_PARITY_NONE;
			flow_control = RFCOMM_RPN_FLOW_NONE;
			xon_char = RFCOMM_RPN_XON_CHAR;
			xoff_char = RFCOMM_RPN_XOFF_CHAR;
                } else {
			/*
			 * Ignore/accept bit_rate, 8 bits, 1 stop bit, no 
			 * parity, no flow control lines, default XON/XOFF 
			 * chars.
			 */

			bit_rate = rpn->bit_rate;
			rpn->param_mask = le16toh(rpn->param_mask); /* XXX */

			data_bits = RFCOMM_RPN_DATA_BITS(rpn->line_settings);
			if (rpn->param_mask & RFCOMM_RPN_PM_DATA &&
			    data_bits != RFCOMM_RPN_DATA_8) {
				data_bits = RFCOMM_RPN_DATA_8;
				param_mask ^= RFCOMM_RPN_PM_DATA;
			}

			stop_bits = RFCOMM_RPN_STOP_BITS(rpn->line_settings);
			if (rpn->param_mask & RFCOMM_RPN_PM_STOP &&
			    stop_bits != RFCOMM_RPN_STOP_1) {
				stop_bits = RFCOMM_RPN_STOP_1;
				param_mask ^= RFCOMM_RPN_PM_STOP;
			}

			parity = RFCOMM_RPN_PARITY(rpn->line_settings);
			if (rpn->param_mask & RFCOMM_RPN_PM_PARITY &&
			    parity != RFCOMM_RPN_PARITY_NONE) {
				parity = RFCOMM_RPN_PARITY_NONE;
				param_mask ^= RFCOMM_RPN_PM_PARITY;
			}

			flow_control = rpn->flow_control;
			if (rpn->param_mask & RFCOMM_RPN_PM_FLOW &&
			    flow_control != RFCOMM_RPN_FLOW_NONE) {
				flow_control = RFCOMM_RPN_FLOW_NONE;
				param_mask ^= RFCOMM_RPN_PM_FLOW;
			}

			xon_char = rpn->xon_char;
			if (rpn->param_mask & RFCOMM_RPN_PM_XON &&
			    xon_char != RFCOMM_RPN_XON_CHAR) {
				xon_char = RFCOMM_RPN_XON_CHAR;
				param_mask ^= RFCOMM_RPN_PM_XON;
			}

			xoff_char = rpn->xoff_char;
			if (rpn->param_mask & RFCOMM_RPN_PM_XOFF &&
			    xoff_char != RFCOMM_RPN_XOFF_CHAR) {
				xoff_char = RFCOMM_RPN_XOFF_CHAR;
				param_mask ^= RFCOMM_RPN_PM_XOFF;
			}
		}

		rpn->bit_rate = bit_rate;
		rpn->line_settings = RFCOMM_MKRPN_LINE_SETTINGS(data_bits, 
						stop_bits, parity);
		rpn->flow_control = flow_control;
		rpn->xon_char = xon_char;
		rpn->xoff_char = xoff_char;
		rpn->param_mask = htole16(param_mask); /* XXX */

		m0->m_pkthdr.len = m0->m_len = sizeof(*hdr) + sizeof(*rpn);

		hdr->type = RFCOMM_MKMCC_TYPE(0, RFCOMM_MCC_RPN);
		error = ng_btsocket_rfcomm_send_uih(s,
				RFCOMM_MKADDRESS(INITIATOR(s), 0), 0, 0, m0);
	} else
		NG_FREE_M(m0); /* XXX ignore response */

	return (error);
} /* ng_btsocket_rfcomm_receive_rpn */

/*
 * Receive RFCOMM RLS MCC command
 */

static int
ng_btsocket_rfcomm_receive_rls(ng_btsocket_rfcomm_session_p s, struct mbuf *m0)
{
	struct rfcomm_mcc_hdr	*hdr = mtod(m0, struct rfcomm_mcc_hdr *);
	struct rfcomm_mcc_rls	*rls = (struct rfcomm_mcc_rls *)(hdr + 1);
	int			 error = 0;

	mtx_assert(&s->session_mtx, MA_OWNED);

	/*
	 * XXX FIXME Do we have to do anything else here? Remote peer tries to 
	 * tell us something about DLCI. Just report what we have received and
	 * return back received values as required by TS 07.10 spec.
	 */

	NG_BTSOCKET_RFCOMM_INFO(
"%s: Got MCC RLS, dlci=%d, status=%#x, cr=%d, length=%d, session state=%d, " \
"flags=%#x, mtu=%d, len=%d\n",
		__func__, RFCOMM_DLCI(rls->address), rls->status,
		RFCOMM_CR(hdr->type), RFCOMM_MCC_LENGTH(hdr->length),
		s->state, s->flags, s->mtu, m0->m_pkthdr.len);

	if (RFCOMM_CR(hdr->type)) {
		if (rls->status & 0x1)
			NG_BTSOCKET_RFCOMM_ERR(
"%s: Got RLS dlci=%d, error=%#x\n", __func__, RFCOMM_DLCI(rls->address),
				rls->status >> 1);

		hdr->type = RFCOMM_MKMCC_TYPE(0, RFCOMM_MCC_RLS);
		error = ng_btsocket_rfcomm_send_uih(s,
				RFCOMM_MKADDRESS(INITIATOR(s), 0), 0, 0, m0);
	} else
		NG_FREE_M(m0); /* XXX ignore responses */

	return (error);
} /* ng_btsocket_rfcomm_receive_rls */

/*
 * Receive RFCOMM PN MCC command
 */

static int
ng_btsocket_rfcomm_receive_pn(ng_btsocket_rfcomm_session_p s, struct mbuf *m0)
{
	struct rfcomm_mcc_hdr		*hdr = mtod(m0, struct rfcomm_mcc_hdr*);
	struct rfcomm_mcc_pn		*pn = (struct rfcomm_mcc_pn *)(hdr+1);
	ng_btsocket_rfcomm_pcb_t	*pcb = NULL;
	int				 error = 0;

	mtx_assert(&s->session_mtx, MA_OWNED);

	NG_BTSOCKET_RFCOMM_INFO(
"%s: Got MCC PN, dlci=%d, cr=%d, length=%d, flow_control=%#x, priority=%d, " \
"ack_timer=%d, mtu=%d, max_retrans=%d, credits=%d, session state=%d, " \
"flags=%#x, session mtu=%d, len=%d\n",
		__func__, pn->dlci, RFCOMM_CR(hdr->type),
		RFCOMM_MCC_LENGTH(hdr->length), pn->flow_control, pn->priority,
		pn->ack_timer, le16toh(pn->mtu), pn->max_retrans, pn->credits,
		s->state, s->flags, s->mtu, m0->m_pkthdr.len);

	if (pn->dlci == 0) {
		NG_BTSOCKET_RFCOMM_ERR("%s: Zero dlci in MCC PN\n", __func__);
		NG_FREE_M(m0);

		return (EINVAL);
	}

	/* Check if we have this dlci */
	pcb = ng_btsocket_rfcomm_pcb_by_dlci(s, pn->dlci);
	if (pcb != NULL) {
		mtx_lock(&pcb->pcb_mtx);

		if (RFCOMM_CR(hdr->type)) {
			/* PN Request */
			ng_btsocket_rfcomm_set_pn(pcb, 1, pn->flow_control,
				pn->credits, pn->mtu);

			if (pcb->flags & NG_BTSOCKET_RFCOMM_DLC_CFC) {
				pn->flow_control = 0xe0;
				pn->credits = RFCOMM_DEFAULT_CREDITS;
			} else {
				pn->flow_control = 0;
				pn->credits = 0;
			}

			hdr->type = RFCOMM_MKMCC_TYPE(0, RFCOMM_MCC_PN);
			error = ng_btsocket_rfcomm_send_uih(s, 
					RFCOMM_MKADDRESS(INITIATOR(s), 0),
					0, 0, m0);
		} else {
			/* PN Response - proceed with SABM. Timeout still set */
			if (pcb->state == NG_BTSOCKET_RFCOMM_DLC_CONFIGURING) {
				ng_btsocket_rfcomm_set_pn(pcb, 0,
					pn->flow_control, pn->credits, pn->mtu);

				pcb->state = NG_BTSOCKET_RFCOMM_DLC_CONNECTING;
				error = ng_btsocket_rfcomm_send_command(s,
						RFCOMM_FRAME_SABM, pn->dlci);
			} else
				NG_BTSOCKET_RFCOMM_WARN(
"%s: Got PN response for dlci=%d in invalid state=%d\n",
					__func__, pn->dlci, pcb->state);

			NG_FREE_M(m0);
		}

		mtx_unlock(&pcb->pcb_mtx);
	} else if (RFCOMM_CR(hdr->type)) {
		/* PN request to non-existing dlci - incoming connection */
		pcb = ng_btsocket_rfcomm_connect_ind(s,
				RFCOMM_SRVCHANNEL(pn->dlci));
		if (pcb != NULL) {
			mtx_lock(&pcb->pcb_mtx);

			pcb->dlci = pn->dlci;

			ng_btsocket_rfcomm_set_pn(pcb, 1, pn->flow_control,
				pn->credits, pn->mtu);

			if (pcb->flags & NG_BTSOCKET_RFCOMM_DLC_CFC) {
				pn->flow_control = 0xe0;
				pn->credits = RFCOMM_DEFAULT_CREDITS;
			} else {
				pn->flow_control = 0;
				pn->credits = 0;
			}

			hdr->type = RFCOMM_MKMCC_TYPE(0, RFCOMM_MCC_PN);
			error = ng_btsocket_rfcomm_send_uih(s, 
					RFCOMM_MKADDRESS(INITIATOR(s), 0),
					0, 0, m0);

			if (error == 0) {
				ng_btsocket_rfcomm_timeout(pcb);
				pcb->state = NG_BTSOCKET_RFCOMM_DLC_CONNECTING;
				soisconnecting(pcb->so);
			} else
				ng_btsocket_rfcomm_pcb_kill(pcb, error);

			mtx_unlock(&pcb->pcb_mtx);
		} else {
			/* Nobody is listen()ing on this channel */
			error = ng_btsocket_rfcomm_send_command(s,
					RFCOMM_FRAME_DM, pn->dlci);
			NG_FREE_M(m0);
		}
	} else
		NG_FREE_M(m0); /* XXX ignore response to non-existing dlci */

	return (error);
} /* ng_btsocket_rfcomm_receive_pn */

/*
 * Set PN parameters for dlci. Caller must hold pcb->pcb_mtx.
 * 
 * From Bluetooth spec.
 * 
 * "... The CL1 - CL4 field is completely redefined. (In TS07.10 this defines 
 *  the convergence layer to use, which is not applicable to RFCOMM. In RFCOMM,
 *  in Bluetooth versions up to 1.0B, this field was forced to 0).
 *
 *  In the PN request sent prior to a DLC establishment, this field must contain
 *  the value 15 (0xF), indicating support of credit based flow control in the 
 *  sender. See Table 5.3 below. If the PN response contains any other value 
 *  than 14 (0xE) in this field, it is inferred that the peer RFCOMM entity is 
 *  not supporting the credit based flow control feature. (This is only possible
 *  if the peer RFCOMM implementation is only conforming to Bluetooth version 
 *  1.0B.) If a PN request is sent on an already open DLC, then this field must
 *  contain the value zero; it is not possible to set initial credits  more 
 *  than once per DLC activation. A responding implementation must set this 
 *  field in the PN response to 14 (0xE), if (and only if) the value in the PN 
 *  request was 15..."
 */

static void
ng_btsocket_rfcomm_set_pn(ng_btsocket_rfcomm_pcb_p pcb, u_int8_t cr,
		u_int8_t flow_control, u_int8_t credits, u_int16_t mtu)
{
	mtx_assert(&pcb->pcb_mtx, MA_OWNED);

	pcb->mtu = le16toh(mtu);

	if (cr) {
		if (flow_control == 0xf0) {
			pcb->flags |= NG_BTSOCKET_RFCOMM_DLC_CFC;
			pcb->tx_cred = credits;
		} else {
			pcb->flags &= ~NG_BTSOCKET_RFCOMM_DLC_CFC;
			pcb->tx_cred = 0;
		}
	} else {
		if (flow_control == 0xe0) {
			pcb->flags |= NG_BTSOCKET_RFCOMM_DLC_CFC;
			pcb->tx_cred = credits;
		} else {
			pcb->flags &= ~NG_BTSOCKET_RFCOMM_DLC_CFC;
			pcb->tx_cred = 0;
		}
	}

	NG_BTSOCKET_RFCOMM_INFO(
"%s: cr=%d, dlci=%d, state=%d, flags=%#x, mtu=%d, rx_cred=%d, tx_cred=%d\n",
		__func__, cr, pcb->dlci, pcb->state, pcb->flags, pcb->mtu,
		pcb->rx_cred, pcb->tx_cred);
} /* ng_btsocket_rfcomm_set_pn */

/*
 * Send RFCOMM SABM/DISC/UA/DM frames. Caller must hold s->session_mtx
 */

static int
ng_btsocket_rfcomm_send_command(ng_btsocket_rfcomm_session_p s,
		u_int8_t type, u_int8_t dlci)
{
	struct rfcomm_cmd_hdr	*hdr = NULL;
	struct mbuf		*m = NULL;
	int			 cr;

	mtx_assert(&s->session_mtx, MA_OWNED);

	NG_BTSOCKET_RFCOMM_INFO(
"%s: Sending command type %#x, session state=%d, flags=%#x, mtu=%d, dlci=%d\n",
		__func__, type, s->state, s->flags, s->mtu, dlci);

	switch (type) {
	case RFCOMM_FRAME_SABM:
	case RFCOMM_FRAME_DISC:
		cr = INITIATOR(s);
		break;

	case RFCOMM_FRAME_UA:
	case RFCOMM_FRAME_DM:
		cr = !INITIATOR(s);
		break;

	default:
		panic("%s: Invalid frame type=%#x\n", __func__, type);
		return (EINVAL);
		/* NOT REACHED */
	}

	MGETHDR(m, M_NOWAIT, MT_DATA);
	if (m == NULL)
		return (ENOBUFS);

	m->m_pkthdr.len = m->m_len = sizeof(*hdr);

	hdr = mtod(m, struct rfcomm_cmd_hdr *);
	hdr->address = RFCOMM_MKADDRESS(cr, dlci);
	hdr->control = RFCOMM_MKCONTROL(type, 1);
	hdr->length = RFCOMM_MKLEN8(0);
	hdr->fcs = ng_btsocket_rfcomm_fcs3((u_int8_t *) hdr);

	NG_BT_MBUFQ_ENQUEUE(&s->outq, m);

	return (0);
} /* ng_btsocket_rfcomm_send_command */

/*
 * Send RFCOMM UIH frame. Caller must hold s->session_mtx
 */

static int
ng_btsocket_rfcomm_send_uih(ng_btsocket_rfcomm_session_p s, u_int8_t address,
		u_int8_t pf, u_int8_t credits, struct mbuf *data)
{
	struct rfcomm_frame_hdr	*hdr = NULL;
	struct mbuf		*m = NULL, *mcrc = NULL;
	u_int16_t		 length;

	mtx_assert(&s->session_mtx, MA_OWNED);

	MGETHDR(m, M_NOWAIT, MT_DATA);
	if (m == NULL) {
		NG_FREE_M(data);
		return (ENOBUFS);
	}
	m->m_pkthdr.len = m->m_len = sizeof(*hdr);

	MGET(mcrc, M_NOWAIT, MT_DATA);
	if (mcrc == NULL) {
		NG_FREE_M(data);
		return (ENOBUFS);
	}
	mcrc->m_len = 1;

	/* Fill UIH frame header */
	hdr = mtod(m, struct rfcomm_frame_hdr *);
	hdr->address = address;
	hdr->control = RFCOMM_MKCONTROL(RFCOMM_FRAME_UIH, pf);

	/* Calculate FCS */
	mcrc->m_data[0] = ng_btsocket_rfcomm_fcs2((u_int8_t *) hdr);

	/* Put length back */
	length = (data != NULL)? data->m_pkthdr.len : 0;
	if (length > 127) {
		u_int16_t	l = htole16(RFCOMM_MKLEN16(length));

		bcopy(&l, &hdr->length, sizeof(l));
		m->m_pkthdr.len ++;
		m->m_len ++;
	} else
		hdr->length = RFCOMM_MKLEN8(length);

	if (pf) {
		m->m_data[m->m_len] = credits;
		m->m_pkthdr.len ++;
		m->m_len ++;
	}

	/* Add payload */
	if (data != NULL) {
		m_cat(m, data);
		m->m_pkthdr.len += length;
	}

	/* Put FCS back */
	m_cat(m, mcrc);
	m->m_pkthdr.len ++;

	NG_BTSOCKET_RFCOMM_INFO(
"%s: Sending UIH state=%d, flags=%#x, address=%d, length=%d, pf=%d, " \
"credits=%d, len=%d\n",
		__func__, s->state, s->flags, address, length, pf, credits,
		m->m_pkthdr.len);

	NG_BT_MBUFQ_ENQUEUE(&s->outq, m);

	return (0);
} /* ng_btsocket_rfcomm_send_uih */

/*
 * Send MSC request. Caller must hold pcb->pcb_mtx and pcb->session->session_mtx
 */

static int
ng_btsocket_rfcomm_send_msc(ng_btsocket_rfcomm_pcb_p pcb)
{
	struct mbuf		*m = NULL;
	struct rfcomm_mcc_hdr	*hdr = NULL;
	struct rfcomm_mcc_msc	*msc = NULL;

	mtx_assert(&pcb->session->session_mtx, MA_OWNED);
	mtx_assert(&pcb->pcb_mtx, MA_OWNED);

	MGETHDR(m, M_NOWAIT, MT_DATA);
	if (m == NULL)
		return (ENOBUFS);

	m->m_pkthdr.len = m->m_len = sizeof(*hdr) + sizeof(*msc);

	hdr = mtod(m, struct rfcomm_mcc_hdr *);
	msc = (struct rfcomm_mcc_msc *)(hdr + 1);

	hdr->type = RFCOMM_MKMCC_TYPE(1, RFCOMM_MCC_MSC);
	hdr->length = RFCOMM_MKLEN8(sizeof(*msc));

	msc->address = RFCOMM_MKADDRESS(1, pcb->dlci);
	msc->modem = pcb->lmodem;

	NG_BTSOCKET_RFCOMM_INFO(
"%s: Sending MSC dlci=%d, state=%d, flags=%#x, address=%d, modem=%#x\n",
		__func__, pcb->dlci, pcb->state, pcb->flags, msc->address,
		msc->modem);

	return (ng_btsocket_rfcomm_send_uih(pcb->session,
			RFCOMM_MKADDRESS(INITIATOR(pcb->session), 0), 0, 0, m));
} /* ng_btsocket_rfcomm_send_msc */

/*
 * Send PN request. Caller must hold pcb->pcb_mtx and pcb->session->session_mtx
 */

static int
ng_btsocket_rfcomm_send_pn(ng_btsocket_rfcomm_pcb_p pcb)
{
	struct mbuf		*m = NULL;
	struct rfcomm_mcc_hdr	*hdr = NULL;
	struct rfcomm_mcc_pn	*pn = NULL;

	mtx_assert(&pcb->session->session_mtx, MA_OWNED);
	mtx_assert(&pcb->pcb_mtx, MA_OWNED);

	MGETHDR(m, M_NOWAIT, MT_DATA);
	if (m == NULL)
		return (ENOBUFS);

	m->m_pkthdr.len = m->m_len = sizeof(*hdr) + sizeof(*pn);

	hdr = mtod(m, struct rfcomm_mcc_hdr *);
	pn = (struct rfcomm_mcc_pn *)(hdr + 1);

	hdr->type = RFCOMM_MKMCC_TYPE(1, RFCOMM_MCC_PN);
	hdr->length = RFCOMM_MKLEN8(sizeof(*pn));

	pn->dlci = pcb->dlci;

	/*
	 * Set default DLCI priority as described in GSM 07.10
	 * (ETSI TS 101 369) clause 5.6 page 42
	 */

	pn->priority = (pcb->dlci < 56)? (((pcb->dlci >> 3) << 3) + 7) : 61;
	pn->ack_timer = 0;
	pn->mtu = htole16(pcb->mtu);
	pn->max_retrans = 0;

	if (pcb->flags & NG_BTSOCKET_RFCOMM_DLC_CFC) {
		pn->flow_control = 0xf0;
		pn->credits = pcb->rx_cred;
	} else {
		pn->flow_control = 0;
		pn->credits = 0;
	}

	NG_BTSOCKET_RFCOMM_INFO(
"%s: Sending PN dlci=%d, state=%d, flags=%#x, mtu=%d, flow_control=%#x, " \
"credits=%d\n",	__func__, pcb->dlci, pcb->state, pcb->flags, pcb->mtu,
		pn->flow_control, pn->credits);

	return (ng_btsocket_rfcomm_send_uih(pcb->session,
			RFCOMM_MKADDRESS(INITIATOR(pcb->session), 0), 0, 0, m));
} /* ng_btsocket_rfcomm_send_pn */

/*
 * Calculate and send credits based on available space in receive buffer
 */

static int
ng_btsocket_rfcomm_send_credits(ng_btsocket_rfcomm_pcb_p pcb)
{
	int		error = 0;
	u_int8_t	credits;

	mtx_assert(&pcb->pcb_mtx, MA_OWNED);
	mtx_assert(&pcb->session->session_mtx, MA_OWNED);

	NG_BTSOCKET_RFCOMM_INFO(
"%s: Sending more credits, dlci=%d, state=%d, flags=%#x, mtu=%d, " \
"space=%ld, tx_cred=%d, rx_cred=%d\n",
		__func__, pcb->dlci, pcb->state, pcb->flags, pcb->mtu,
		sbspace(&pcb->so->so_rcv), pcb->tx_cred, pcb->rx_cred);

	credits = sbspace(&pcb->so->so_rcv) / pcb->mtu;
	if (credits > 0) {
		if (pcb->rx_cred + credits > RFCOMM_MAX_CREDITS)
			credits = RFCOMM_MAX_CREDITS - pcb->rx_cred;

		error = ng_btsocket_rfcomm_send_uih(
				pcb->session,
				RFCOMM_MKADDRESS(INITIATOR(pcb->session),
					pcb->dlci), 1, credits, NULL);
		if (error == 0) {
			pcb->rx_cred += credits;

			NG_BTSOCKET_RFCOMM_INFO(
"%s: Gave remote side %d more credits, dlci=%d, state=%d, flags=%#x, " \
"rx_cred=%d, tx_cred=%d\n",	__func__, credits, pcb->dlci, pcb->state,
				pcb->flags, pcb->rx_cred, pcb->tx_cred);
		} else
			NG_BTSOCKET_RFCOMM_ERR(
"%s: Could not send credits, error=%d, dlci=%d, state=%d, flags=%#x, " \
"mtu=%d, space=%ld, tx_cred=%d, rx_cred=%d\n",
				__func__, error, pcb->dlci, pcb->state,
				pcb->flags, pcb->mtu, sbspace(&pcb->so->so_rcv),
				pcb->tx_cred, pcb->rx_cred);
	}

	return (error);
} /* ng_btsocket_rfcomm_send_credits */

/*****************************************************************************
 *****************************************************************************
 **                              RFCOMM DLCs
 *****************************************************************************
 *****************************************************************************/

/*
 * Send data from socket send buffer
 * Caller must hold pcb->pcb_mtx and pcb->session->session_mtx
 */

static int
ng_btsocket_rfcomm_pcb_send(ng_btsocket_rfcomm_pcb_p pcb, int limit)
{
	struct mbuf	*m = NULL;
	int		 sent, length, error;

	mtx_assert(&pcb->session->session_mtx, MA_OWNED);
	mtx_assert(&pcb->pcb_mtx, MA_OWNED);

	if (pcb->flags & NG_BTSOCKET_RFCOMM_DLC_CFC)
		limit = min(limit, pcb->tx_cred);
	else if (!(pcb->rmodem & RFCOMM_MODEM_FC))
		limit = min(limit, RFCOMM_MAX_CREDITS); /* XXX ??? */
	else
		limit = 0;

	if (limit == 0) {
		NG_BTSOCKET_RFCOMM_INFO(
"%s: Could not send - remote flow control asserted, dlci=%d, flags=%#x, " \
"rmodem=%#x, tx_cred=%d\n",
			__func__, pcb->dlci, pcb->flags, pcb->rmodem,
			pcb->tx_cred);

		return (0);
	}

	for (error = 0, sent = 0; sent < limit; sent ++) { 
		length = min(pcb->mtu, sbavail(&pcb->so->so_snd));
		if (length == 0)
			break;

		/* Get the chunk from the socket's send buffer */
		m = ng_btsocket_rfcomm_prepare_packet(&pcb->so->so_snd, length);
		if (m == NULL) {
			error = ENOBUFS;
			break;
		}

		sbdrop(&pcb->so->so_snd, length);

		error = ng_btsocket_rfcomm_send_uih(pcb->session,
				RFCOMM_MKADDRESS(INITIATOR(pcb->session),
					pcb->dlci), 0, 0, m);
		if (error != 0)
			break;
	}

	if (pcb->flags & NG_BTSOCKET_RFCOMM_DLC_CFC)
		pcb->tx_cred -= sent;

	if (error == 0 && sent > 0) {
		pcb->flags &= ~NG_BTSOCKET_RFCOMM_DLC_SENDING;
		sowwakeup(pcb->so);
	}

	return (error);
} /* ng_btsocket_rfcomm_pcb_send */

/*
 * Unlink and disconnect DLC. If ng_btsocket_rfcomm_pcb_kill() returns
 * non zero value than socket has no reference and has to be detached.
 * Caller must hold pcb->pcb_mtx and pcb->session->session_mtx
 */

static void
ng_btsocket_rfcomm_pcb_kill(ng_btsocket_rfcomm_pcb_p pcb, int error)
{
	ng_btsocket_rfcomm_session_p	s = pcb->session;

	NG_BTSOCKET_RFCOMM_INFO(
"%s: Killing DLC, so=%p, dlci=%d, state=%d, flags=%#x, error=%d\n",
		__func__, pcb->so, pcb->dlci, pcb->state, pcb->flags, error);

	if (pcb->session == NULL)
		panic("%s: DLC without session, pcb=%p, state=%d, flags=%#x\n",
			__func__, pcb, pcb->state, pcb->flags);

	mtx_assert(&pcb->session->session_mtx, MA_OWNED);
	mtx_assert(&pcb->pcb_mtx, MA_OWNED);

	if (pcb->flags & NG_BTSOCKET_RFCOMM_DLC_TIMO)
		ng_btsocket_rfcomm_untimeout(pcb);

	/* Detach DLC from the session. Does not matter which state DLC in */
	LIST_REMOVE(pcb, session_next);
	pcb->session = NULL;

	/* Change DLC state and wakeup all sleepers */
	pcb->state = NG_BTSOCKET_RFCOMM_DLC_CLOSED;
	pcb->so->so_error = error;
	soisdisconnected(pcb->so);
	wakeup(&pcb->state);

	/* Check if we have any DLCs left on the session */
	if (LIST_EMPTY(&s->dlcs) && INITIATOR(s)) {
		NG_BTSOCKET_RFCOMM_INFO(
"%s: Disconnecting session, state=%d, flags=%#x, mtu=%d\n",
			__func__, s->state, s->flags, s->mtu);

		switch (s->state) {
		case NG_BTSOCKET_RFCOMM_SESSION_CLOSED:
		case NG_BTSOCKET_RFCOMM_SESSION_DISCONNECTING:
			/*
			 * Do not have to do anything here. We can get here
			 * when L2CAP connection was terminated or we have 
			 * received DISC on multiplexor channel
			 */
			break;

		case NG_BTSOCKET_RFCOMM_SESSION_OPEN:
			/* Send DISC on multiplexor channel */
			error = ng_btsocket_rfcomm_send_command(s,
					RFCOMM_FRAME_DISC, 0);
			if (error == 0) {
				s->state = NG_BTSOCKET_RFCOMM_SESSION_DISCONNECTING;
				break;
			}
			/* FALL THROUGH */

		case NG_BTSOCKET_RFCOMM_SESSION_CONNECTING:
		case NG_BTSOCKET_RFCOMM_SESSION_CONNECTED:
			s->state = NG_BTSOCKET_RFCOMM_SESSION_CLOSED;
			break;

/*		case NG_BTSOCKET_RFCOMM_SESSION_LISTENING: */
		default:
			panic("%s: Invalid session state=%d, flags=%#x\n",
				__func__, s->state, s->flags);
			break;
		}

		ng_btsocket_rfcomm_task_wakeup();
	}
} /* ng_btsocket_rfcomm_pcb_kill */

/*
 * Look for given dlci for given RFCOMM session. Caller must hold s->session_mtx
 */

static ng_btsocket_rfcomm_pcb_p
ng_btsocket_rfcomm_pcb_by_dlci(ng_btsocket_rfcomm_session_p s, int dlci)
{
	ng_btsocket_rfcomm_pcb_p	pcb = NULL;

	mtx_assert(&s->session_mtx, MA_OWNED);

	LIST_FOREACH(pcb, &s->dlcs, session_next)
		if (pcb->dlci == dlci)
			break;

	return (pcb);
} /* ng_btsocket_rfcomm_pcb_by_dlci */

/*
 * Look for socket that listens on given src address and given channel
 */

static ng_btsocket_rfcomm_pcb_p
ng_btsocket_rfcomm_pcb_listener(bdaddr_p src, int channel)
{
	ng_btsocket_rfcomm_pcb_p	pcb = NULL, pcb1 = NULL;

	mtx_lock(&ng_btsocket_rfcomm_sockets_mtx);

	LIST_FOREACH(pcb, &ng_btsocket_rfcomm_sockets, next) {
		if (pcb->channel != channel ||
		    !(pcb->so->so_options & SO_ACCEPTCONN))
			continue;

		if (bcmp(&pcb->src, src, sizeof(*src)) == 0)
			break;

		if (bcmp(&pcb->src, NG_HCI_BDADDR_ANY, sizeof(bdaddr_t)) == 0)
			pcb1 = pcb;
	}

	mtx_unlock(&ng_btsocket_rfcomm_sockets_mtx);

	return ((pcb != NULL)? pcb : pcb1);
} /* ng_btsocket_rfcomm_pcb_listener */

/*****************************************************************************
 *****************************************************************************
 **                              Misc. functions 
 *****************************************************************************
 *****************************************************************************/

/*
 *  Set timeout. Caller MUST hold pcb_mtx
 */

static void
ng_btsocket_rfcomm_timeout(ng_btsocket_rfcomm_pcb_p pcb)
{
	mtx_assert(&pcb->pcb_mtx, MA_OWNED);

	if (!(pcb->flags & NG_BTSOCKET_RFCOMM_DLC_TIMO)) {
		pcb->flags |= NG_BTSOCKET_RFCOMM_DLC_TIMO;
		pcb->flags &= ~NG_BTSOCKET_RFCOMM_DLC_TIMEDOUT;
		callout_reset(&pcb->timo, ng_btsocket_rfcomm_timo * hz,
		    ng_btsocket_rfcomm_process_timeout, pcb);
	} else
		panic("%s: Duplicated socket timeout?!\n", __func__);
} /* ng_btsocket_rfcomm_timeout */

/*
 *  Unset pcb timeout. Caller MUST hold pcb_mtx
 */

static void
ng_btsocket_rfcomm_untimeout(ng_btsocket_rfcomm_pcb_p pcb)
{
	mtx_assert(&pcb->pcb_mtx, MA_OWNED);

	if (pcb->flags & NG_BTSOCKET_RFCOMM_DLC_TIMO) {
		callout_stop(&pcb->timo);
		pcb->flags &= ~NG_BTSOCKET_RFCOMM_DLC_TIMO;
		pcb->flags &= ~NG_BTSOCKET_RFCOMM_DLC_TIMEDOUT;
	} else
		panic("%s: No socket timeout?!\n", __func__);
} /* ng_btsocket_rfcomm_timeout */

/*
 * Process pcb timeout
 */

static void
ng_btsocket_rfcomm_process_timeout(void *xpcb)
{
	ng_btsocket_rfcomm_pcb_p	pcb = (ng_btsocket_rfcomm_pcb_p) xpcb;

	mtx_assert(&pcb->pcb_mtx, MA_OWNED);

	NG_BTSOCKET_RFCOMM_INFO(
"%s: Timeout, so=%p, dlci=%d, state=%d, flags=%#x\n",
		__func__, pcb->so, pcb->dlci, pcb->state, pcb->flags);

	pcb->flags &= ~NG_BTSOCKET_RFCOMM_DLC_TIMO;
	pcb->flags |= NG_BTSOCKET_RFCOMM_DLC_TIMEDOUT;

	switch (pcb->state) {
	case NG_BTSOCKET_RFCOMM_DLC_CONFIGURING:
	case NG_BTSOCKET_RFCOMM_DLC_CONNECTING:
		pcb->state = NG_BTSOCKET_RFCOMM_DLC_DISCONNECTING;
		break;

	case NG_BTSOCKET_RFCOMM_DLC_W4_CONNECT:
	case NG_BTSOCKET_RFCOMM_DLC_DISCONNECTING:
		break;

	default:
		panic(
"%s: DLC timeout in invalid state, dlci=%d, state=%d, flags=%#x\n",
			__func__, pcb->dlci, pcb->state, pcb->flags);
		break;
	}

	ng_btsocket_rfcomm_task_wakeup();
} /* ng_btsocket_rfcomm_process_timeout */

/*
 * Get up to length bytes from the socket buffer
 */

static struct mbuf *
ng_btsocket_rfcomm_prepare_packet(struct sockbuf *sb, int length)
{
	struct mbuf	*top = NULL, *m = NULL, *n = NULL, *nextpkt = NULL;
	int		 mlen, noff, len;

	MGETHDR(top, M_NOWAIT, MT_DATA);
	if (top == NULL)
		return (NULL);

	top->m_pkthdr.len = length;
	top->m_len = 0;
	mlen = MHLEN;

	m = top;
	n = sb->sb_mb;
	nextpkt = n->m_nextpkt;
	noff = 0;

	while (length > 0 && n != NULL) {
		len = min(mlen - m->m_len, n->m_len - noff);
		if (len > length)
			len = length;

		bcopy(mtod(n, caddr_t)+noff, mtod(m, caddr_t)+m->m_len, len);
		m->m_len += len;
		noff += len;
		length -= len;

		if (length > 0 && m->m_len == mlen) {
			MGET(m->m_next, M_NOWAIT, MT_DATA);
			if (m->m_next == NULL) {
				NG_FREE_M(top);
				return (NULL);
			}

			m = m->m_next;
			m->m_len = 0;
			mlen = MLEN;
		}

		if (noff == n->m_len) {
			noff = 0;
			n = n->m_next;

			if (n == NULL)
				n = nextpkt;

			nextpkt = (n != NULL)? n->m_nextpkt : NULL;
		}
	}

	if (length < 0)
		panic("%s: length=%d\n", __func__, length);
	if (length > 0 && n == NULL)
		panic("%s: bogus length=%d, n=%p\n", __func__, length, n);

	return (top);
} /* ng_btsocket_rfcomm_prepare_packet */

