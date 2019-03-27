/*
 * ng_btsocket.c
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
 * $Id: ng_btsocket.c,v 1.4 2003/09/14 23:29:06 max Exp $
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bitstring.h>
#include <sys/errno.h>
#include <sys/domain.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mbuf.h>
#include <sys/mutex.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>
#include <sys/taskqueue.h>

#include <net/vnet.h>

#include <netgraph/ng_message.h>
#include <netgraph/netgraph.h>
#include <netgraph/bluetooth/include/ng_bluetooth.h>
#include <netgraph/bluetooth/include/ng_hci.h>
#include <netgraph/bluetooth/include/ng_l2cap.h>
#include <netgraph/bluetooth/include/ng_btsocket.h>
#include <netgraph/bluetooth/include/ng_btsocket_hci_raw.h>
#include <netgraph/bluetooth/include/ng_btsocket_l2cap.h>
#include <netgraph/bluetooth/include/ng_btsocket_rfcomm.h>
#include <netgraph/bluetooth/include/ng_btsocket_sco.h>

static int			ng_btsocket_modevent (module_t, int, void *);
static struct domain		ng_btsocket_domain;

/*
 * Bluetooth raw HCI sockets
 */

static struct pr_usrreqs	ng_btsocket_hci_raw_usrreqs = {
	.pru_abort =		ng_btsocket_hci_raw_abort,
	.pru_attach =		ng_btsocket_hci_raw_attach,
	.pru_bind =		ng_btsocket_hci_raw_bind,
	.pru_connect =		ng_btsocket_hci_raw_connect,
	.pru_control =		ng_btsocket_hci_raw_control,
	.pru_detach =		ng_btsocket_hci_raw_detach,
	.pru_disconnect =	ng_btsocket_hci_raw_disconnect,
	.pru_peeraddr =		ng_btsocket_hci_raw_peeraddr,
	.pru_send =		ng_btsocket_hci_raw_send,
	.pru_shutdown =		NULL,
	.pru_sockaddr =		ng_btsocket_hci_raw_sockaddr,
	.pru_close =		ng_btsocket_hci_raw_close,
};

/*
 * Bluetooth raw L2CAP sockets
 */

static struct pr_usrreqs	ng_btsocket_l2cap_raw_usrreqs = {
	.pru_abort =		ng_btsocket_l2cap_raw_abort,
	.pru_attach =		ng_btsocket_l2cap_raw_attach,
	.pru_bind =		ng_btsocket_l2cap_raw_bind,
	.pru_connect =		ng_btsocket_l2cap_raw_connect,
	.pru_control =		ng_btsocket_l2cap_raw_control,
	.pru_detach =		ng_btsocket_l2cap_raw_detach,
	.pru_disconnect =	ng_btsocket_l2cap_raw_disconnect,
	.pru_peeraddr =		ng_btsocket_l2cap_raw_peeraddr,
	.pru_send =		ng_btsocket_l2cap_raw_send,
	.pru_shutdown =		NULL,
	.pru_sockaddr =		ng_btsocket_l2cap_raw_sockaddr,
	.pru_close =		ng_btsocket_l2cap_raw_close,
};

/*
 * Bluetooth SEQPACKET L2CAP sockets
 */

static struct pr_usrreqs	ng_btsocket_l2cap_usrreqs = {
	.pru_abort =		ng_btsocket_l2cap_abort,
	.pru_accept =		ng_btsocket_l2cap_accept,
	.pru_attach =		ng_btsocket_l2cap_attach,
	.pru_bind =		ng_btsocket_l2cap_bind,
	.pru_connect =		ng_btsocket_l2cap_connect,
	.pru_control =		ng_btsocket_l2cap_control,
	.pru_detach =		ng_btsocket_l2cap_detach,
	.pru_disconnect =	ng_btsocket_l2cap_disconnect,
        .pru_listen =		ng_btsocket_l2cap_listen,
	.pru_peeraddr =		ng_btsocket_l2cap_peeraddr,
	.pru_send =		ng_btsocket_l2cap_send,
	.pru_shutdown =		NULL,
	.pru_sockaddr =		ng_btsocket_l2cap_sockaddr,
	.pru_close =		ng_btsocket_l2cap_close,
};

/*
 * Bluetooth STREAM RFCOMM sockets
 */

static struct pr_usrreqs	ng_btsocket_rfcomm_usrreqs = {
	.pru_abort =		ng_btsocket_rfcomm_abort,
	.pru_accept =		ng_btsocket_rfcomm_accept,
	.pru_attach =		ng_btsocket_rfcomm_attach,
	.pru_bind =		ng_btsocket_rfcomm_bind,
	.pru_connect =		ng_btsocket_rfcomm_connect,
	.pru_control =		ng_btsocket_rfcomm_control,
	.pru_detach =		ng_btsocket_rfcomm_detach,
	.pru_disconnect =	ng_btsocket_rfcomm_disconnect,
        .pru_listen =		ng_btsocket_rfcomm_listen,
	.pru_peeraddr =		ng_btsocket_rfcomm_peeraddr,
	.pru_send =		ng_btsocket_rfcomm_send,
	.pru_shutdown =		NULL,
	.pru_sockaddr =		ng_btsocket_rfcomm_sockaddr,
	.pru_close =		ng_btsocket_rfcomm_close,
};

/*
 * Bluetooth SEQPACKET SCO sockets
 */

static struct pr_usrreqs	ng_btsocket_sco_usrreqs = {
	.pru_abort =		ng_btsocket_sco_abort,
	.pru_accept =		ng_btsocket_sco_accept,
	.pru_attach =		ng_btsocket_sco_attach,
	.pru_bind =		ng_btsocket_sco_bind,
	.pru_connect =		ng_btsocket_sco_connect,
	.pru_control =		ng_btsocket_sco_control,
	.pru_detach =		ng_btsocket_sco_detach,
	.pru_disconnect =	ng_btsocket_sco_disconnect,
	.pru_listen =		ng_btsocket_sco_listen,
	.pru_peeraddr =		ng_btsocket_sco_peeraddr,
	.pru_send =		ng_btsocket_sco_send,
	.pru_shutdown =		NULL,
	.pru_sockaddr =		ng_btsocket_sco_sockaddr,
	.pru_close =		ng_btsocket_sco_close,
};

/* 
 * Definitions of protocols supported in the BLUETOOTH domain 
 */

static struct protosw		ng_btsocket_protosw[] = {
{
	.pr_type =		SOCK_RAW,
	.pr_domain =		&ng_btsocket_domain,
	.pr_protocol =		BLUETOOTH_PROTO_HCI,
	.pr_flags =		PR_ATOMIC|PR_ADDR,
	.pr_ctloutput =		ng_btsocket_hci_raw_ctloutput,
	.pr_init =		ng_btsocket_hci_raw_init,
	.pr_usrreqs =		&ng_btsocket_hci_raw_usrreqs,
},
{
	.pr_type =		SOCK_RAW,
	.pr_domain =		&ng_btsocket_domain,
	.pr_protocol =		BLUETOOTH_PROTO_L2CAP,
	.pr_flags =		PR_ATOMIC|PR_ADDR,
	.pr_init =		ng_btsocket_l2cap_raw_init,
	.pr_usrreqs =		&ng_btsocket_l2cap_raw_usrreqs,
},
{
	.pr_type =		SOCK_SEQPACKET,
	.pr_domain =		&ng_btsocket_domain,
	.pr_protocol =		BLUETOOTH_PROTO_L2CAP,
	.pr_flags =		PR_ATOMIC|PR_CONNREQUIRED,
	.pr_ctloutput =		ng_btsocket_l2cap_ctloutput,
	.pr_init =		ng_btsocket_l2cap_init,
	.pr_usrreqs =		&ng_btsocket_l2cap_usrreqs,
},
{
	.pr_type =		SOCK_STREAM,
	.pr_domain =		&ng_btsocket_domain,
	.pr_protocol =		BLUETOOTH_PROTO_RFCOMM,
	.pr_flags =		PR_CONNREQUIRED,
	.pr_ctloutput =		ng_btsocket_rfcomm_ctloutput,
	.pr_init =		ng_btsocket_rfcomm_init,
	.pr_usrreqs =		&ng_btsocket_rfcomm_usrreqs,
},
{
	.pr_type =		SOCK_SEQPACKET,
	.pr_domain =		&ng_btsocket_domain,
	.pr_protocol =		BLUETOOTH_PROTO_SCO,
	.pr_flags =		PR_ATOMIC|PR_CONNREQUIRED,
	.pr_ctloutput =		ng_btsocket_sco_ctloutput,
	.pr_init =		ng_btsocket_sco_init,
	.pr_usrreqs =		&ng_btsocket_sco_usrreqs,
},
};

#define ng_btsocket_protosw_end \
	&ng_btsocket_protosw[nitems(ng_btsocket_protosw)]

/*
 * BLUETOOTH domain
 */

static struct domain ng_btsocket_domain = {
	.dom_family =		AF_BLUETOOTH,
	.dom_name =		"bluetooth",
	.dom_protosw =		ng_btsocket_protosw,
	.dom_protoswNPROTOSW =	ng_btsocket_protosw_end
};

/* 
 * Socket sysctl tree 
 */

SYSCTL_NODE(_net_bluetooth_hci, OID_AUTO, sockets, CTLFLAG_RW,
	0, "Bluetooth HCI sockets family");
SYSCTL_NODE(_net_bluetooth_l2cap, OID_AUTO, sockets, CTLFLAG_RW,
	0, "Bluetooth L2CAP sockets family");
SYSCTL_NODE(_net_bluetooth_rfcomm, OID_AUTO, sockets, CTLFLAG_RW,
	0, "Bluetooth RFCOMM sockets family");
SYSCTL_NODE(_net_bluetooth_sco, OID_AUTO, sockets, CTLFLAG_RW,
	0, "Bluetooth SCO sockets family");

/* 
 * Module 
 */

static moduledata_t	ng_btsocket_mod = {
	"ng_btsocket",
	ng_btsocket_modevent,
	NULL
};

DECLARE_MODULE(ng_btsocket, ng_btsocket_mod, SI_SUB_PROTO_DOMAIN,
	SI_ORDER_ANY);
MODULE_VERSION(ng_btsocket, NG_BLUETOOTH_VERSION);
MODULE_DEPEND(ng_btsocket, ng_bluetooth, NG_BLUETOOTH_VERSION,
	NG_BLUETOOTH_VERSION, NG_BLUETOOTH_VERSION);
MODULE_DEPEND(ng_btsocket, netgraph, NG_ABI_VERSION,
	NG_ABI_VERSION, NG_ABI_VERSION);

/*
 * Handle loading and unloading for this node type.
 * This is to handle auxiliary linkages (e.g protocol domain addition).
 */

static int  
ng_btsocket_modevent(module_t mod, int event, void *data)
{
	int	error = 0;
        
	switch (event) {
	case MOD_LOAD:
		break;

	case MOD_UNLOAD:
		/* XXX can't unload protocol domain yet */
		error = EBUSY;
		break;

	default:
		error = EOPNOTSUPP;
		break;
	}

	return (error);
} /* ng_btsocket_modevent */

VNET_DOMAIN_SET(ng_btsocket_);
