/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1982, 1986, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following edsclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following edsclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE EDSCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	From: @(#)if_loop.c	8.1 (Berkeley) 6/10/93
 * $FreeBSD$
 */

/*
 * Discard interface driver for protocol testing and timing.
 * Mimics an Ethernet device so that VLANs can be attached to it etc.
 */

#include <sys/param.h>		/* types, important constants */
#include <sys/kernel.h>		/* SYSINIT for load-time initializations */
#include <sys/malloc.h>		/* malloc(9) */
#include <sys/module.h>		/* module(9) */
#include <sys/mbuf.h>		/* mbuf(9) */
#include <sys/socket.h>		/* struct ifreq */
#include <sys/sockio.h>		/* socket ioctl's */
/* #include <sys/systm.h> if you need printf(9) or other all-purpose globals */

#include <net/bpf.h>		/* bpf(9) */
#include <net/ethernet.h>	/* Ethernet related constants and types */
#include <net/if.h>
#include <net/if_var.h>		/* basic part of ifnet(9) */
#include <net/if_clone.h>	/* network interface cloning */
#include <net/if_types.h>	/* IFT_ETHER and friends */
#include <net/if_var.h>		/* kernel-only part of ifnet(9) */
#include <net/vnet.h>

static const char edscname[] = "edsc";

/*
 * Software configuration of an interface specific to this device type.
 */
struct edsc_softc {
	struct ifnet	*sc_ifp; /* ptr to generic interface configuration */

	/*
	 * A non-null driver can keep various things here, for instance,
	 * the hardware revision, cached values of write-only registers, etc.
	 */
};

/*
 * Attach to the interface cloning framework.
 */
VNET_DEFINE_STATIC(struct if_clone *, edsc_cloner);
#define	V_edsc_cloner	VNET(edsc_cloner)
static int	edsc_clone_create(struct if_clone *, int, caddr_t);
static void	edsc_clone_destroy(struct ifnet *);

/*
 * Interface driver methods.
 */
static void	edsc_init(void *dummy);
/* static void edsc_input(struct ifnet *ifp, struct mbuf *m); would be here */
static int	edsc_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data);
static void	edsc_start(struct ifnet *ifp);

/*
 * We'll allocate softc instances from this.
 */
static		MALLOC_DEFINE(M_EDSC, edscname, "Ethernet discard interface");

/*
 * Create an interface instance.
 */
static int
edsc_clone_create(struct if_clone *ifc, int unit, caddr_t params)
{
	struct edsc_softc	*sc;
	struct ifnet		*ifp;
	static u_char		 eaddr[ETHER_ADDR_LEN];	/* 0:0:0:0:0:0 */

	/*
	 * Allocate soft and ifnet structures.  Link each to the other.
	 */
	sc = malloc(sizeof(struct edsc_softc), M_EDSC, M_WAITOK | M_ZERO);
	ifp = sc->sc_ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		free(sc, M_EDSC);
		return (ENOSPC);
	}

	ifp->if_softc = sc;

	/*
	 * Get a name for this particular interface in its ifnet structure.
	 */
	if_initname(ifp, edscname, unit);

	/*
	 * Typical Ethernet interface flags: we can do broadcast and
	 * multicast but can't hear our own broadcasts or multicasts.
	 */
	ifp->if_flags = IFF_BROADCAST | IFF_MULTICAST | IFF_SIMPLEX;

	/*
	 * We can pretent we have the whole set of hardware features
	 * because we just discard all packets we get from the upper layer.
	 * However, the features are disabled initially.  They can be
	 * enabled via edsc_ioctl() when needed.
	 */
	ifp->if_capabilities =
	    IFCAP_VLAN_MTU | IFCAP_VLAN_HWTAGGING | IFCAP_VLAN_HWCSUM |
	    IFCAP_HWCSUM | IFCAP_TSO |
	    IFCAP_JUMBO_MTU;
	ifp->if_capenable = 0;

	/*
	 * Set the interface driver methods.
	 */
	ifp->if_init = edsc_init;
	/* ifp->if_input = edsc_input; */
	ifp->if_ioctl = edsc_ioctl;
	ifp->if_start = edsc_start;

	/*
	 * Set the maximum output queue length from the global parameter.
	 */
	ifp->if_snd.ifq_maxlen = ifqmaxlen;

	/*
	 * Do ifnet initializations common to all Ethernet drivers
	 * and attach to the network interface framework.
	 * TODO: Pick a non-zero link level address.
	 */
	ether_ifattach(ifp, eaddr);

	/*
	 * Now we can mark the interface as running, i.e., ready
	 * for operation.
	 */
	ifp->if_drv_flags |= IFF_DRV_RUNNING;

	return (0);
}

/*
 * Destroy an interface instance.
 */
static void
edsc_clone_destroy(struct ifnet *ifp)
{
	struct edsc_softc	*sc = ifp->if_softc;

	/*
	 * Detach from the network interface framework.
	 */
	ether_ifdetach(ifp);

	/*
	 * Free memory occupied by ifnet and softc.
	 */
	if_free(ifp);
	free(sc, M_EDSC);
}

/*
 * This method is invoked from ether_ioctl() when it's time
 * to bring up the hardware.
 */
static void
edsc_init(void *dummy)
{
#if 0	/* what a hardware driver would do here... */
	struct edsc_soft	*sc = (struct edsc_softc *)dummy;
	struct ifnet		*ifp = sc->sc_ifp;

	/* blah-blah-blah */
#endif
}

/*
 * Network interfaces are controlled via the ioctl(2) syscall.
 */
static int
edsc_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct ifreq		*ifr = (struct ifreq *)data;

	switch (cmd) {
	case SIOCSIFCAP:
#if 1
		/*
		 * Just turn on any capabilities requested.
		 * The generic ifioctl() function has already made sure
		 * that they are supported, i.e., set in if_capabilities.
		 */
		ifp->if_capenable = ifr->ifr_reqcap;
#else
		/*
		 * A h/w driver would need to analyze the requested
		 * bits and program the hardware, e.g.:
		 */
		mask = ifp->if_capenable ^ ifr->ifr_reqcap;

		if (mask & IFCAP_VLAN_HWTAGGING) {
			ifp->if_capenable ^= IFCAP_VLAN_HWTAGGING;

			if (ifp->if_capenable & IFCAP_VLAN_HWTAGGING)
				/* blah-blah-blah */
			else
				/* etc-etc-etc */
		}
#endif
		break;

	default:
		/*
		 * Offload the rest onto the common Ethernet handler.
		 */
		return (ether_ioctl(ifp, cmd, data));
	}

	return (0);
}

/*
 * Process the output queue.
 */
static void
edsc_start(struct ifnet *ifp)
{
	struct mbuf		*m;

	/*
	 * A hardware interface driver can set IFF_DRV_OACTIVE
	 * in ifp->if_drv_flags:
	 *
	 * ifp->if_drv_flags |= IFF_DRV_OACTIVE;
	 *
	 * to prevent if_start from being invoked again while the
	 * transmission is under way.  The flag is to protect the
	 * device's transmitter, not the method itself.  The output
	 * queue is locked and several threads can process it in
	 * parallel safely, so the driver can use other means to
	 * serialize access to the transmitter.
	 *
	 * If using IFF_DRV_OACTIVE, the driver should clear the flag
	 * not earlier than the current transmission is complete, e.g.,
	 * upon an interrupt from the device, not just before returning
	 * from if_start.  This method merely starts the transmission,
	 * which may proceed asynchronously.
	 */

	/*
	 * We loop getting packets from the queue until it's empty.
	 * A h/w driver would loop until the device can accept more
	 * data into its buffer, or while there are free transmit
	 * descriptors, or whatever.
	 */
	for (;;) {
		/*
		 * Try to dequeue one packet.  Stop if the queue is empty.
		 * Use IF_DEQUEUE() here if ALTQ(9) support is unneeded.
		 */
		IFQ_DEQUEUE(&ifp->if_snd, m);
		if (m == NULL)
			break;

		/*
		 * Let bpf(9) at the packet.
		 */
		BPF_MTAP(ifp, m);

		/*
		 * Update the interface counters.
		 */
		if_inc_counter(ifp, IFCOUNTER_OBYTES, m->m_pkthdr.len);
		if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);

		/*
		 * Finally, just drop the packet.
		 * TODO: Reply to ARP requests unless IFF_NOARP is set.
		 */
		m_freem(m);
	}

	/*
	 * ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
	 * would be here only if the transmission were synchronous.
	 */
}

static void
vnet_edsc_init(const void *unused __unused)
{

	/*
	 * Connect to the network interface cloning framework.
	 * The last argument is the number of units to be created
	 * from the outset.  It's also the minimum number of units
	 * allowed.  We don't want any units created as soon as the
	 * driver is loaded.
	 */
	V_edsc_cloner = if_clone_simple(edscname, edsc_clone_create,
	    edsc_clone_destroy, 0);
}
VNET_SYSINIT(vnet_edsc_init, SI_SUB_PROTO_IFATTACHDOMAIN, SI_ORDER_ANY,
    vnet_edsc_init, NULL);

static void
vnet_edsc_uninit(const void *unused __unused)
{

	/*
	 * Disconnect from the cloning framework.
	 * Existing interfaces will be disposed of properly.
	 */
	if_clone_detach(V_edsc_cloner);
}
VNET_SYSUNINIT(vnet_edsc_uninit, SI_SUB_INIT_IF, SI_ORDER_ANY,
    vnet_edsc_uninit, NULL);

/*
 * This function provides handlers for module events, namely load and unload.
 */
static int
edsc_modevent(module_t mod, int type, void *data)
{

	switch (type) {
	case MOD_LOAD:
	case MOD_UNLOAD:
		break;
	default:
		/*
		 * There are other event types, but we don't handle them.
		 * See module(9).
		 */
		return (EOPNOTSUPP);
	}
	return (0);
}

static moduledata_t edsc_mod = {
	"if_edsc",			/* name */
	edsc_modevent,			/* event handler */
	NULL				/* additional data */
};

DECLARE_MODULE(if_edsc, edsc_mod, SI_SUB_PSEUDO, SI_ORDER_ANY);
