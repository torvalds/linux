/*-
 * Copyright (c) 1999-2002, 2007 Robert N. M. Watson
 * Copyright (c) 2001-2002 Networks Associates Technology, Inc.
 * Copyright (c) 2006 SPARTA, Inc.
 * All rights reserved.
 *
 * This software was developed by Robert Watson for the TrustedBSD Project.
 *
 * This software was developed for the FreeBSD Project in part by Network
 * Associates Laboratories, the Security Research Division of Network
 * Associates, Inc. under DARPA/SPAWAR contract N66001-01-C-8035 ("CBOSS"),
 * as part of the DARPA CHATS research program.
 *
 * This software was enhanced by SPARTA ISSO under SPAWAR contract
 * N66001-04-C-6019 ("SEFOS").
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
 * Developed by the TrustedBSD Project.
 *
 * Limit access to interfaces until they are specifically administratively
 * enabled.  Prevents protocol stack-driven packet leakage in unsafe
 * environments.
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_types.h>
#include <net/bpfdesc.h>

#include <security/mac/mac_policy.h>

SYSCTL_DECL(_security_mac);

static SYSCTL_NODE(_security_mac, OID_AUTO, ifoff, CTLFLAG_RW, 0,
    "TrustedBSD mac_ifoff policy controls");

static int	ifoff_enabled = 1;
SYSCTL_INT(_security_mac_ifoff, OID_AUTO, enabled, CTLFLAG_RWTUN,
    &ifoff_enabled, 0, "Enforce ifoff policy");

static int	ifoff_lo_enabled = 1;
SYSCTL_INT(_security_mac_ifoff, OID_AUTO, lo_enabled, CTLFLAG_RWTUN,
    &ifoff_lo_enabled, 0, "Enable loopback interfaces");

static int	ifoff_other_enabled = 0;
SYSCTL_INT(_security_mac_ifoff, OID_AUTO, other_enabled, CTLFLAG_RWTUN,
    &ifoff_other_enabled, 0, "Enable other interfaces");

static int	ifoff_bpfrecv_enabled = 0;
SYSCTL_INT(_security_mac_ifoff, OID_AUTO, bpfrecv_enabled, CTLFLAG_RWTUN,
    &ifoff_bpfrecv_enabled, 0, "Enable BPF reception even when interface "
    "is disabled");

static int
ifnet_check_outgoing(struct ifnet *ifp)
{

	if (!ifoff_enabled)
		return (0);

	if (ifoff_lo_enabled && ifp->if_type == IFT_LOOP)
		return (0);

	if (ifoff_other_enabled && ifp->if_type != IFT_LOOP)
		return (0);

	return (EPERM);
}

static int
ifnet_check_incoming(struct ifnet *ifp, int viabpf)
{
	if (!ifoff_enabled)
		return (0);

	if (ifoff_lo_enabled && ifp->if_type == IFT_LOOP)
		return (0);

	if (ifoff_other_enabled && ifp->if_type != IFT_LOOP)
		return (0);

	if (viabpf && ifoff_bpfrecv_enabled)
		return (0);

	return (EPERM);
}

/*
 * Object-specific entry point implementations are sorted alphabetically by
 * object type and then by operation.
 */
static int
ifoff_bpfdesc_check_receive(struct bpf_d *d, struct label *dlabel,
    struct ifnet *ifp, struct label *ifplabel)
{

	return (ifnet_check_incoming(ifp, 1));
}

static int
ifoff_ifnet_check_transmit(struct ifnet *ifp, struct label *ifplabel,
    struct mbuf *m, struct label *mlabel)
{

	return (ifnet_check_outgoing(ifp));
}

static int
ifoff_inpcb_check_deliver(struct inpcb *inp, struct label *inplabel,
    struct mbuf *m, struct label *mlabel)
{

	M_ASSERTPKTHDR(m);
	if (m->m_pkthdr.rcvif != NULL)
		return (ifnet_check_incoming(m->m_pkthdr.rcvif, 0));

	return (0);
}

static int
ifoff_socket_check_deliver(struct socket *so, struct label *solabel,
    struct mbuf *m, struct label *mlabel)
{

	M_ASSERTPKTHDR(m);
	if (m->m_pkthdr.rcvif != NULL)
		return (ifnet_check_incoming(m->m_pkthdr.rcvif, 0));

	return (0);
}

static struct mac_policy_ops ifoff_ops =
{
	.mpo_bpfdesc_check_receive = ifoff_bpfdesc_check_receive,
	.mpo_ifnet_check_transmit = ifoff_ifnet_check_transmit,
	.mpo_inpcb_check_deliver = ifoff_inpcb_check_deliver,
	.mpo_socket_check_deliver = ifoff_socket_check_deliver,
};

MAC_POLICY_SET(&ifoff_ops, mac_ifoff, "TrustedBSD MAC/ifoff",
    MPC_LOADTIME_FLAG_UNLOADOK, NULL);
