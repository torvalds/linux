/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1982, 1986, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (C) 2001 WIDE Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)in.c	8.4 (Berkeley) 1/9/95
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_mpath.h"

#include <sys/param.h>
#include <sys/eventhandler.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/malloc.h>
#include <sys/priv.h>
#include <sys/socket.h>
#include <sys/jail.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/proc.h>
#include <sys/rmlock.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>
#include <sys/sx.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_llatbl.h>
#include <net/if_types.h>
#include <net/route.h>
#include <net/vnet.h>

#include <netinet/if_ether.h>
#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet/in_pcb.h>
#include <netinet/ip_var.h>
#include <netinet/ip_carp.h>
#include <netinet/igmp_var.h>
#include <netinet/udp.h>
#include <netinet/udp_var.h>

static int in_aifaddr_ioctl(u_long, caddr_t, struct ifnet *, struct thread *);
static int in_difaddr_ioctl(u_long, caddr_t, struct ifnet *, struct thread *);

static void	in_socktrim(struct sockaddr_in *);
static void	in_purgemaddrs(struct ifnet *);

VNET_DEFINE_STATIC(int, nosameprefix);
#define	V_nosameprefix			VNET(nosameprefix)
SYSCTL_INT(_net_inet_ip, OID_AUTO, no_same_prefix, CTLFLAG_VNET | CTLFLAG_RW,
	&VNET_NAME(nosameprefix), 0,
	"Refuse to create same prefixes on different interfaces");

VNET_DECLARE(struct inpcbinfo, ripcbinfo);
#define	V_ripcbinfo			VNET(ripcbinfo)

static struct sx in_control_sx;
SX_SYSINIT(in_control_sx, &in_control_sx, "in_control");

/*
 * Return 1 if an internet address is for a ``local'' host
 * (one to which we have a connection).
 */
int
in_localaddr(struct in_addr in)
{
	struct rm_priotracker in_ifa_tracker;
	u_long i = ntohl(in.s_addr);
	struct in_ifaddr *ia;

	IN_IFADDR_RLOCK(&in_ifa_tracker);
	CK_STAILQ_FOREACH(ia, &V_in_ifaddrhead, ia_link) {
		if ((i & ia->ia_subnetmask) == ia->ia_subnet) {
			IN_IFADDR_RUNLOCK(&in_ifa_tracker);
			return (1);
		}
	}
	IN_IFADDR_RUNLOCK(&in_ifa_tracker);
	return (0);
}

/*
 * Return 1 if an internet address is for the local host and configured
 * on one of its interfaces.
 */
int
in_localip(struct in_addr in)
{
	struct rm_priotracker in_ifa_tracker;
	struct in_ifaddr *ia;

	IN_IFADDR_RLOCK(&in_ifa_tracker);
	LIST_FOREACH(ia, INADDR_HASH(in.s_addr), ia_hash) {
		if (IA_SIN(ia)->sin_addr.s_addr == in.s_addr) {
			IN_IFADDR_RUNLOCK(&in_ifa_tracker);
			return (1);
		}
	}
	IN_IFADDR_RUNLOCK(&in_ifa_tracker);
	return (0);
}

/*
 * Return 1 if an internet address is configured on an interface.
 */
int
in_ifhasaddr(struct ifnet *ifp, struct in_addr in)
{
	struct epoch_tracker et;
	struct ifaddr *ifa;
	struct in_ifaddr *ia;

	NET_EPOCH_ENTER(et);
	CK_STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
		if (ifa->ifa_addr->sa_family != AF_INET)
			continue;
		ia = (struct in_ifaddr *)ifa;
		if (ia->ia_addr.sin_addr.s_addr == in.s_addr) {
			NET_EPOCH_EXIT(et);
			return (1);
		}
	}
	NET_EPOCH_EXIT(et);

	return (0);
}

/*
 * Return a reference to the interface address which is different to
 * the supplied one but with same IP address value.
 */
static struct in_ifaddr *
in_localip_more(struct in_ifaddr *ia)
{
	struct rm_priotracker in_ifa_tracker;
	in_addr_t in = IA_SIN(ia)->sin_addr.s_addr;
	struct in_ifaddr *it;

	IN_IFADDR_RLOCK(&in_ifa_tracker);
	LIST_FOREACH(it, INADDR_HASH(in), ia_hash) {
		if (it != ia && IA_SIN(it)->sin_addr.s_addr == in) {
			ifa_ref(&it->ia_ifa);
			IN_IFADDR_RUNLOCK(&in_ifa_tracker);
			return (it);
		}
	}
	IN_IFADDR_RUNLOCK(&in_ifa_tracker);

	return (NULL);
}

/*
 * Determine whether an IP address is in a reserved set of addresses
 * that may not be forwarded, or whether datagrams to that destination
 * may be forwarded.
 */
int
in_canforward(struct in_addr in)
{
	u_long i = ntohl(in.s_addr);
	u_long net;

	if (IN_EXPERIMENTAL(i) || IN_MULTICAST(i) || IN_LINKLOCAL(i))
		return (0);
	if (IN_CLASSA(i)) {
		net = i & IN_CLASSA_NET;
		if (net == 0 || net == (IN_LOOPBACKNET << IN_CLASSA_NSHIFT))
			return (0);
	}
	return (1);
}

/*
 * Trim a mask in a sockaddr
 */
static void
in_socktrim(struct sockaddr_in *ap)
{
    char *cplim = (char *) &ap->sin_addr;
    char *cp = (char *) (&ap->sin_addr + 1);

    ap->sin_len = 0;
    while (--cp >= cplim)
	if (*cp) {
	    (ap)->sin_len = cp - (char *) (ap) + 1;
	    break;
	}
}

/*
 * Generic internet control operations (ioctl's).
 */
int
in_control(struct socket *so, u_long cmd, caddr_t data, struct ifnet *ifp,
    struct thread *td)
{
	struct ifreq *ifr = (struct ifreq *)data;
	struct sockaddr_in *addr = (struct sockaddr_in *)&ifr->ifr_addr;
	struct epoch_tracker et;
	struct ifaddr *ifa;
	struct in_ifaddr *ia;
	int error;

	if (ifp == NULL)
		return (EADDRNOTAVAIL);

	/*
	 * Filter out 4 ioctls we implement directly.  Forward the rest
	 * to specific functions and ifp->if_ioctl().
	 */
	switch (cmd) {
	case SIOCGIFADDR:
	case SIOCGIFBRDADDR:
	case SIOCGIFDSTADDR:
	case SIOCGIFNETMASK:
		break;
	case SIOCDIFADDR:
		sx_xlock(&in_control_sx);
		error = in_difaddr_ioctl(cmd, data, ifp, td);
		sx_xunlock(&in_control_sx);
		return (error);
	case OSIOCAIFADDR:	/* 9.x compat */
	case SIOCAIFADDR:
		sx_xlock(&in_control_sx);
		error = in_aifaddr_ioctl(cmd, data, ifp, td);
		sx_xunlock(&in_control_sx);
		return (error);
	case SIOCSIFADDR:
	case SIOCSIFBRDADDR:
	case SIOCSIFDSTADDR:
	case SIOCSIFNETMASK:
		/* We no longer support that old commands. */
		return (EINVAL);
	default:
		if (ifp->if_ioctl == NULL)
			return (EOPNOTSUPP);
		return ((*ifp->if_ioctl)(ifp, cmd, data));
	}

	if (addr->sin_addr.s_addr != INADDR_ANY &&
	    prison_check_ip4(td->td_ucred, &addr->sin_addr) != 0)
		return (EADDRNOTAVAIL);

	/*
	 * Find address for this interface, if it exists.  If an
	 * address was specified, find that one instead of the
	 * first one on the interface, if possible.
	 */
	NET_EPOCH_ENTER(et);
	CK_STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
		if (ifa->ifa_addr->sa_family != AF_INET)
			continue;
		ia = (struct in_ifaddr *)ifa;
		if (ia->ia_addr.sin_addr.s_addr == addr->sin_addr.s_addr)
			break;
	}
	if (ifa == NULL)
		CK_STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link)
			if (ifa->ifa_addr->sa_family == AF_INET) {
				ia = (struct in_ifaddr *)ifa;
				if (prison_check_ip4(td->td_ucred,
				    &ia->ia_addr.sin_addr) == 0)
					break;
			}

	if (ifa == NULL) {
		NET_EPOCH_EXIT(et);
		return (EADDRNOTAVAIL);
	}

	error = 0;
	switch (cmd) {
	case SIOCGIFADDR:
		*addr = ia->ia_addr;
		break;

	case SIOCGIFBRDADDR:
		if ((ifp->if_flags & IFF_BROADCAST) == 0) {
			error = EINVAL;
			break;
		}
		*addr = ia->ia_broadaddr;
		break;

	case SIOCGIFDSTADDR:
		if ((ifp->if_flags & IFF_POINTOPOINT) == 0) {
			error = EINVAL;
			break;
		}
		*addr = ia->ia_dstaddr;
		break;

	case SIOCGIFNETMASK:
		*addr = ia->ia_sockmask;
		break;
	}

	NET_EPOCH_EXIT(et);

	return (error);
}

static int
in_aifaddr_ioctl(u_long cmd, caddr_t data, struct ifnet *ifp, struct thread *td)
{
	const struct in_aliasreq *ifra = (struct in_aliasreq *)data;
	const struct sockaddr_in *addr = &ifra->ifra_addr;
	const struct sockaddr_in *broadaddr = &ifra->ifra_broadaddr;
	const struct sockaddr_in *mask = &ifra->ifra_mask;
	const struct sockaddr_in *dstaddr = &ifra->ifra_dstaddr;
	const int vhid = (cmd == SIOCAIFADDR) ? ifra->ifra_vhid : 0;
	struct epoch_tracker et;
	struct ifaddr *ifa;
	struct in_ifaddr *ia;
	bool iaIsFirst;
	int error = 0;

	error = priv_check(td, PRIV_NET_ADDIFADDR);
	if (error)
		return (error);

	/*
	 * ifra_addr must be present and be of INET family.
	 * ifra_broadaddr/ifra_dstaddr and ifra_mask are optional.
	 */
	if (addr->sin_len != sizeof(struct sockaddr_in) ||
	    addr->sin_family != AF_INET)
		return (EINVAL);
	if (broadaddr->sin_len != 0 &&
	    (broadaddr->sin_len != sizeof(struct sockaddr_in) ||
	    broadaddr->sin_family != AF_INET))
		return (EINVAL);
	if (mask->sin_len != 0 &&
	    (mask->sin_len != sizeof(struct sockaddr_in) ||
	    mask->sin_family != AF_INET))
		return (EINVAL);
	if ((ifp->if_flags & IFF_POINTOPOINT) &&
	    (dstaddr->sin_len != sizeof(struct sockaddr_in) ||
	     dstaddr->sin_addr.s_addr == INADDR_ANY))
		return (EDESTADDRREQ);
	if (vhid > 0 && carp_attach_p == NULL)
		return (EPROTONOSUPPORT);

	/*
	 * See whether address already exist.
	 */
	iaIsFirst = true;
	ia = NULL;
	NET_EPOCH_ENTER(et);
	CK_STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
		struct in_ifaddr *it;

		if (ifa->ifa_addr->sa_family != AF_INET)
			continue;

		it = (struct in_ifaddr *)ifa;
		iaIsFirst = false;
		if (it->ia_addr.sin_addr.s_addr == addr->sin_addr.s_addr &&
		    prison_check_ip4(td->td_ucred, &addr->sin_addr) == 0)
			ia = it;
	}
	NET_EPOCH_EXIT(et);

	if (ia != NULL)
		(void )in_difaddr_ioctl(cmd, data, ifp, td);

	ifa = ifa_alloc(sizeof(struct in_ifaddr), M_WAITOK);
	ia = (struct in_ifaddr *)ifa;
	ifa->ifa_addr = (struct sockaddr *)&ia->ia_addr;
	ifa->ifa_dstaddr = (struct sockaddr *)&ia->ia_dstaddr;
	ifa->ifa_netmask = (struct sockaddr *)&ia->ia_sockmask;
	callout_init_rw(&ia->ia_garp_timer, &ifp->if_addr_lock,
	    CALLOUT_RETURNUNLOCKED);

	ia->ia_ifp = ifp;
	ia->ia_addr = *addr;
	if (mask->sin_len != 0) {
		ia->ia_sockmask = *mask;
		ia->ia_subnetmask = ntohl(ia->ia_sockmask.sin_addr.s_addr);
	} else {
		in_addr_t i = ntohl(addr->sin_addr.s_addr);

		/*
	 	 * Be compatible with network classes, if netmask isn't
		 * supplied, guess it based on classes.
	 	 */
		if (IN_CLASSA(i))
			ia->ia_subnetmask = IN_CLASSA_NET;
		else if (IN_CLASSB(i))
			ia->ia_subnetmask = IN_CLASSB_NET;
		else
			ia->ia_subnetmask = IN_CLASSC_NET;
		ia->ia_sockmask.sin_addr.s_addr = htonl(ia->ia_subnetmask);
	}
	ia->ia_subnet = ntohl(addr->sin_addr.s_addr) & ia->ia_subnetmask;
	in_socktrim(&ia->ia_sockmask);

	if (ifp->if_flags & IFF_BROADCAST) {
		if (broadaddr->sin_len != 0) {
			ia->ia_broadaddr = *broadaddr;
		} else if (ia->ia_subnetmask == IN_RFC3021_MASK) {
			ia->ia_broadaddr.sin_addr.s_addr = INADDR_BROADCAST;
			ia->ia_broadaddr.sin_len = sizeof(struct sockaddr_in);
			ia->ia_broadaddr.sin_family = AF_INET;
		} else {
			ia->ia_broadaddr.sin_addr.s_addr =
			    htonl(ia->ia_subnet | ~ia->ia_subnetmask);
			ia->ia_broadaddr.sin_len = sizeof(struct sockaddr_in);
			ia->ia_broadaddr.sin_family = AF_INET;
		}
	}

	if (ifp->if_flags & IFF_POINTOPOINT)
		ia->ia_dstaddr = *dstaddr;

	/* XXXGL: rtinit() needs this strange assignment. */
	if (ifp->if_flags & IFF_LOOPBACK)
                ia->ia_dstaddr = ia->ia_addr;

	if (vhid != 0) {
		error = (*carp_attach_p)(&ia->ia_ifa, vhid);
		if (error)
			return (error);
	}

	/* if_addrhead is already referenced by ifa_alloc() */
	IF_ADDR_WLOCK(ifp);
	CK_STAILQ_INSERT_TAIL(&ifp->if_addrhead, ifa, ifa_link);
	IF_ADDR_WUNLOCK(ifp);

	ifa_ref(ifa);			/* in_ifaddrhead */
	IN_IFADDR_WLOCK();
	CK_STAILQ_INSERT_TAIL(&V_in_ifaddrhead, ia, ia_link);
	LIST_INSERT_HEAD(INADDR_HASH(ia->ia_addr.sin_addr.s_addr), ia, ia_hash);
	IN_IFADDR_WUNLOCK();

	/*
	 * Give the interface a chance to initialize
	 * if this is its first address,
	 * and to validate the address if necessary.
	 */
	if (ifp->if_ioctl != NULL) {
		error = (*ifp->if_ioctl)(ifp, SIOCSIFADDR, (caddr_t)ia);
		if (error)
			goto fail1;
	}

	/*
	 * Add route for the network.
	 */
	if (vhid == 0) {
		int flags = RTF_UP;

		if (ifp->if_flags & (IFF_LOOPBACK|IFF_POINTOPOINT))
			flags |= RTF_HOST;

		error = in_addprefix(ia, flags);
		if (error)
			goto fail1;
	}

	/*
	 * Add a loopback route to self.
	 */
	if (vhid == 0 && (ifp->if_flags & IFF_LOOPBACK) == 0 &&
	    ia->ia_addr.sin_addr.s_addr != INADDR_ANY &&
	    !((ifp->if_flags & IFF_POINTOPOINT) &&
	     ia->ia_dstaddr.sin_addr.s_addr == ia->ia_addr.sin_addr.s_addr)) {
		struct in_ifaddr *eia;

		eia = in_localip_more(ia);

		if (eia == NULL) {
			error = ifa_add_loopback_route((struct ifaddr *)ia,
			    (struct sockaddr *)&ia->ia_addr);
			if (error)
				goto fail2;
		} else
			ifa_free(&eia->ia_ifa);
	}

	if (iaIsFirst && (ifp->if_flags & IFF_MULTICAST)) {
		struct in_addr allhosts_addr;
		struct in_ifinfo *ii;

		ii = ((struct in_ifinfo *)ifp->if_afdata[AF_INET]);
		allhosts_addr.s_addr = htonl(INADDR_ALLHOSTS_GROUP);

		error = in_joingroup(ifp, &allhosts_addr, NULL,
			&ii->ii_allhosts);
	}

	/*
	 * Note: we don't need extra reference for ifa, since we called
	 * with sx lock held, and ifaddr can not be deleted in concurrent
	 * thread.
	 */
	EVENTHANDLER_INVOKE(ifaddr_event_ext, ifp, ifa, IFADDR_EVENT_ADD);

	return (error);

fail2:
	if (vhid == 0)
		(void )in_scrubprefix(ia, LLE_STATIC);

fail1:
	if (ia->ia_ifa.ifa_carp)
		(*carp_detach_p)(&ia->ia_ifa, false);

	IF_ADDR_WLOCK(ifp);
	CK_STAILQ_REMOVE(&ifp->if_addrhead, &ia->ia_ifa, ifaddr, ifa_link);
	IF_ADDR_WUNLOCK(ifp);
	ifa_free(&ia->ia_ifa);		/* if_addrhead */

	IN_IFADDR_WLOCK();
	CK_STAILQ_REMOVE(&V_in_ifaddrhead, ia, in_ifaddr, ia_link);
	LIST_REMOVE(ia, ia_hash);
	IN_IFADDR_WUNLOCK();
	ifa_free(&ia->ia_ifa);		/* in_ifaddrhead */

	return (error);
}

static int
in_difaddr_ioctl(u_long cmd, caddr_t data, struct ifnet *ifp, struct thread *td)
{
	const struct ifreq *ifr = (struct ifreq *)data;
	const struct sockaddr_in *addr = (const struct sockaddr_in *)
	    &ifr->ifr_addr;
	struct ifaddr *ifa;
	struct in_ifaddr *ia;
	bool deleteAny, iaIsLast;
	int error;

	if (td != NULL) {
		error = priv_check(td, PRIV_NET_DELIFADDR);
		if (error)
			return (error);
	}

	if (addr->sin_len != sizeof(struct sockaddr_in) ||
	    addr->sin_family != AF_INET)
		deleteAny = true;
	else
		deleteAny = false;

	iaIsLast = true;
	ia = NULL;
	IF_ADDR_WLOCK(ifp);
	CK_STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
		struct in_ifaddr *it;

		if (ifa->ifa_addr->sa_family != AF_INET)
			continue;

		it = (struct in_ifaddr *)ifa;
		if (deleteAny && ia == NULL && (td == NULL ||
		    prison_check_ip4(td->td_ucred, &it->ia_addr.sin_addr) == 0))
			ia = it;

		if (it->ia_addr.sin_addr.s_addr == addr->sin_addr.s_addr &&
		    (td == NULL || prison_check_ip4(td->td_ucred,
		    &addr->sin_addr) == 0))
			ia = it;

		if (it != ia)
			iaIsLast = false;
	}

	if (ia == NULL) {
		IF_ADDR_WUNLOCK(ifp);
		return (EADDRNOTAVAIL);
	}

	CK_STAILQ_REMOVE(&ifp->if_addrhead, &ia->ia_ifa, ifaddr, ifa_link);
	IF_ADDR_WUNLOCK(ifp);
	ifa_free(&ia->ia_ifa);		/* if_addrhead */

	IN_IFADDR_WLOCK();
	CK_STAILQ_REMOVE(&V_in_ifaddrhead, ia, in_ifaddr, ia_link);
	LIST_REMOVE(ia, ia_hash);
	IN_IFADDR_WUNLOCK();

	/*
	 * in_scrubprefix() kills the interface route.
	 */
	in_scrubprefix(ia, LLE_STATIC);

	/*
	 * in_ifadown gets rid of all the rest of
	 * the routes.  This is not quite the right
	 * thing to do, but at least if we are running
	 * a routing process they will come back.
	 */
	in_ifadown(&ia->ia_ifa, 1);

	if (ia->ia_ifa.ifa_carp)
		(*carp_detach_p)(&ia->ia_ifa, cmd == SIOCAIFADDR);

	/*
	 * If this is the last IPv4 address configured on this
	 * interface, leave the all-hosts group.
	 * No state-change report need be transmitted.
	 */
	if (iaIsLast && (ifp->if_flags & IFF_MULTICAST)) {
		struct in_ifinfo *ii;

		ii = ((struct in_ifinfo *)ifp->if_afdata[AF_INET]);
		if (ii->ii_allhosts) {
			(void)in_leavegroup(ii->ii_allhosts, NULL);
			ii->ii_allhosts = NULL;
		}
	}

	IF_ADDR_WLOCK(ifp);
	if (callout_stop(&ia->ia_garp_timer) == 1) {
		ifa_free(&ia->ia_ifa);
	}
	IF_ADDR_WUNLOCK(ifp);

	EVENTHANDLER_INVOKE(ifaddr_event_ext, ifp, &ia->ia_ifa,
	    IFADDR_EVENT_DEL);
	ifa_free(&ia->ia_ifa);		/* in_ifaddrhead */

	return (0);
}

#define rtinitflags(x) \
	((((x)->ia_ifp->if_flags & (IFF_LOOPBACK | IFF_POINTOPOINT)) != 0) \
	    ? RTF_HOST : 0)

/*
 * Check if we have a route for the given prefix already or add one accordingly.
 */
int
in_addprefix(struct in_ifaddr *target, int flags)
{
	struct rm_priotracker in_ifa_tracker;
	struct in_ifaddr *ia;
	struct in_addr prefix, mask, p, m;
	int error;

	if ((flags & RTF_HOST) != 0) {
		prefix = target->ia_dstaddr.sin_addr;
		mask.s_addr = 0;
	} else {
		prefix = target->ia_addr.sin_addr;
		mask = target->ia_sockmask.sin_addr;
		prefix.s_addr &= mask.s_addr;
	}

	IN_IFADDR_RLOCK(&in_ifa_tracker);
	/* Look for an existing address with the same prefix, mask, and fib */
	CK_STAILQ_FOREACH(ia, &V_in_ifaddrhead, ia_link) {
		if (rtinitflags(ia)) {
			p = ia->ia_dstaddr.sin_addr;

			if (prefix.s_addr != p.s_addr)
				continue;
		} else {
			p = ia->ia_addr.sin_addr;
			m = ia->ia_sockmask.sin_addr;
			p.s_addr &= m.s_addr;

			if (prefix.s_addr != p.s_addr ||
			    mask.s_addr != m.s_addr)
				continue;
		}
		if (target->ia_ifp->if_fib != ia->ia_ifp->if_fib)
			continue;

		/*
		 * If we got a matching prefix route inserted by other
		 * interface address, we are done here.
		 */
		if (ia->ia_flags & IFA_ROUTE) {
#ifdef RADIX_MPATH
			if (ia->ia_addr.sin_addr.s_addr ==
			    target->ia_addr.sin_addr.s_addr) {
				IN_IFADDR_RUNLOCK(&in_ifa_tracker);
				return (EEXIST);
			} else
				break;
#endif
			if (V_nosameprefix) {
				IN_IFADDR_RUNLOCK(&in_ifa_tracker);
				return (EEXIST);
			} else {
				int fibnum;

				fibnum = V_rt_add_addr_allfibs ? RT_ALL_FIBS :
					target->ia_ifp->if_fib;
				rt_addrmsg(RTM_ADD, &target->ia_ifa, fibnum);
				IN_IFADDR_RUNLOCK(&in_ifa_tracker);
				return (0);
			}
		}
	}
	IN_IFADDR_RUNLOCK(&in_ifa_tracker);

	/*
	 * No-one seem to have this prefix route, so we try to insert it.
	 */
	error = rtinit(&target->ia_ifa, (int)RTM_ADD, flags);
	if (!error)
		target->ia_flags |= IFA_ROUTE;
	return (error);
}

/*
 * Removes either all lle entries for given @ia, or lle
 * corresponding to @ia address.
 */
static void
in_scrubprefixlle(struct in_ifaddr *ia, int all, u_int flags)
{
	struct sockaddr_in addr, mask;
	struct sockaddr *saddr, *smask;
	struct ifnet *ifp;

	saddr = (struct sockaddr *)&addr;
	bzero(&addr, sizeof(addr));
	addr.sin_len = sizeof(addr);
	addr.sin_family = AF_INET;
	smask = (struct sockaddr *)&mask;
	bzero(&mask, sizeof(mask));
	mask.sin_len = sizeof(mask);
	mask.sin_family = AF_INET;
	mask.sin_addr.s_addr = ia->ia_subnetmask;
	ifp = ia->ia_ifp;

	if (all) {

		/*
		 * Remove all L2 entries matching given prefix.
		 * Convert address to host representation to avoid
		 * doing this on every callback. ia_subnetmask is already
		 * stored in host representation.
		 */
		addr.sin_addr.s_addr = ntohl(ia->ia_addr.sin_addr.s_addr);
		lltable_prefix_free(AF_INET, saddr, smask, flags);
	} else {
		/* Remove interface address only */
		addr.sin_addr.s_addr = ia->ia_addr.sin_addr.s_addr;
		lltable_delete_addr(LLTABLE(ifp), LLE_IFADDR, saddr);
	}
}

/*
 * If there is no other address in the system that can serve a route to the
 * same prefix, remove the route.  Hand over the route to the new address
 * otherwise.
 */
int
in_scrubprefix(struct in_ifaddr *target, u_int flags)
{
	struct rm_priotracker in_ifa_tracker;
	struct in_ifaddr *ia;
	struct in_addr prefix, mask, p, m;
	int error = 0;

	/*
	 * Remove the loopback route to the interface address.
	 */
	if ((target->ia_addr.sin_addr.s_addr != INADDR_ANY) &&
	    !(target->ia_ifp->if_flags & IFF_LOOPBACK) &&
	    (flags & LLE_STATIC)) {
		struct in_ifaddr *eia;

		/*
		 * XXXME: add fib-aware in_localip.
		 * We definitely don't want to switch between
		 * prefixes in different fibs.
		 */
		eia = in_localip_more(target);

		if (eia != NULL) {
			error = ifa_switch_loopback_route((struct ifaddr *)eia,
			    (struct sockaddr *)&target->ia_addr);
			ifa_free(&eia->ia_ifa);
		} else {
			error = ifa_del_loopback_route((struct ifaddr *)target,
			    (struct sockaddr *)&target->ia_addr);
		}
	}

	if (rtinitflags(target)) {
		prefix = target->ia_dstaddr.sin_addr;
		mask.s_addr = 0;
	} else {
		prefix = target->ia_addr.sin_addr;
		mask = target->ia_sockmask.sin_addr;
		prefix.s_addr &= mask.s_addr;
	}

	if ((target->ia_flags & IFA_ROUTE) == 0) {
		int fibnum;
		
		fibnum = V_rt_add_addr_allfibs ? RT_ALL_FIBS :
			target->ia_ifp->if_fib;
		rt_addrmsg(RTM_DELETE, &target->ia_ifa, fibnum);
	
		/*
		 * Removing address from !IFF_UP interface or
		 * prefix which exists on other interface (along with route).
		 * No entries should exist here except target addr.
		 * Given that, delete this entry only.
		 */
		in_scrubprefixlle(target, 0, flags);
		return (0);
	}

	IN_IFADDR_RLOCK(&in_ifa_tracker);
	CK_STAILQ_FOREACH(ia, &V_in_ifaddrhead, ia_link) {
		if (rtinitflags(ia)) {
			p = ia->ia_dstaddr.sin_addr;

			if (prefix.s_addr != p.s_addr)
				continue;
		} else {
			p = ia->ia_addr.sin_addr;
			m = ia->ia_sockmask.sin_addr;
			p.s_addr &= m.s_addr;

			if (prefix.s_addr != p.s_addr ||
			    mask.s_addr != m.s_addr)
				continue;
		}

		if ((ia->ia_ifp->if_flags & IFF_UP) == 0)
			continue;

		/*
		 * If we got a matching prefix address, move IFA_ROUTE and
		 * the route itself to it.  Make sure that routing daemons
		 * get a heads-up.
		 */
		if ((ia->ia_flags & IFA_ROUTE) == 0) {
			ifa_ref(&ia->ia_ifa);
			IN_IFADDR_RUNLOCK(&in_ifa_tracker);
			error = rtinit(&(target->ia_ifa), (int)RTM_DELETE,
			    rtinitflags(target));
			if (error == 0)
				target->ia_flags &= ~IFA_ROUTE;
			else
				log(LOG_INFO, "in_scrubprefix: err=%d, old prefix delete failed\n",
					error);
			/* Scrub all entries IFF interface is different */
			in_scrubprefixlle(target, target->ia_ifp != ia->ia_ifp,
			    flags);
			error = rtinit(&ia->ia_ifa, (int)RTM_ADD,
			    rtinitflags(ia) | RTF_UP);
			if (error == 0)
				ia->ia_flags |= IFA_ROUTE;
			else
				log(LOG_INFO, "in_scrubprefix: err=%d, new prefix add failed\n",
					error);
			ifa_free(&ia->ia_ifa);
			return (error);
		}
	}
	IN_IFADDR_RUNLOCK(&in_ifa_tracker);

	/*
	 * remove all L2 entries on the given prefix
	 */
	in_scrubprefixlle(target, 1, flags);

	/*
	 * As no-one seem to have this prefix, we can remove the route.
	 */
	error = rtinit(&(target->ia_ifa), (int)RTM_DELETE, rtinitflags(target));
	if (error == 0)
		target->ia_flags &= ~IFA_ROUTE;
	else
		log(LOG_INFO, "in_scrubprefix: err=%d, prefix delete failed\n", error);
	return (error);
}

#undef rtinitflags

void
in_ifscrub_all(void)
{
	struct ifnet *ifp;
	struct ifaddr *ifa, *nifa;
	struct ifaliasreq ifr;

	IFNET_RLOCK();
	CK_STAILQ_FOREACH(ifp, &V_ifnet, if_link) {
		/* Cannot lock here - lock recursion. */
		/* NET_EPOCH_ENTER(et); */
		CK_STAILQ_FOREACH_SAFE(ifa, &ifp->if_addrhead, ifa_link, nifa) {
			if (ifa->ifa_addr->sa_family != AF_INET)
				continue;

			/*
			 * This is ugly but the only way for legacy IP to
			 * cleanly remove addresses and everything attached.
			 */
			bzero(&ifr, sizeof(ifr));
			ifr.ifra_addr = *ifa->ifa_addr;
			if (ifa->ifa_dstaddr)
			ifr.ifra_broadaddr = *ifa->ifa_dstaddr;
			(void)in_control(NULL, SIOCDIFADDR, (caddr_t)&ifr,
			    ifp, NULL);
		}
		/* NET_EPOCH_EXIT(et); */
		in_purgemaddrs(ifp);
		igmp_domifdetach(ifp);
	}
	IFNET_RUNLOCK();
}

int
in_ifaddr_broadcast(struct in_addr in, struct in_ifaddr *ia)
{

	return ((in.s_addr == ia->ia_broadaddr.sin_addr.s_addr ||
	     /*
	      * Check for old-style (host 0) broadcast, but
	      * taking into account that RFC 3021 obsoletes it.
	      */
	    (ia->ia_subnetmask != IN_RFC3021_MASK &&
	    ntohl(in.s_addr) == ia->ia_subnet)) &&
	     /*
	      * Check for an all one subnetmask. These
	      * only exist when an interface gets a secondary
	      * address.
	      */
	    ia->ia_subnetmask != (u_long)0xffffffff);
}

/*
 * Return 1 if the address might be a local broadcast address.
 */
int
in_broadcast(struct in_addr in, struct ifnet *ifp)
{
	struct epoch_tracker et;
	struct ifaddr *ifa;
	int found;

	if (in.s_addr == INADDR_BROADCAST ||
	    in.s_addr == INADDR_ANY)
		return (1);
	if ((ifp->if_flags & IFF_BROADCAST) == 0)
		return (0);
	found = 0;
	/*
	 * Look through the list of addresses for a match
	 * with a broadcast address.
	 */
	NET_EPOCH_ENTER(et);
	CK_STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link)
		if (ifa->ifa_addr->sa_family == AF_INET &&
		    in_ifaddr_broadcast(in, (struct in_ifaddr *)ifa)) {
			found = 1;
			break;
		}
	NET_EPOCH_EXIT(et);
	return (found);
}

/*
 * On interface removal, clean up IPv4 data structures hung off of the ifnet.
 */
void
in_ifdetach(struct ifnet *ifp)
{
	IN_MULTI_LOCK();
	in_pcbpurgeif0(&V_ripcbinfo, ifp);
	in_pcbpurgeif0(&V_udbinfo, ifp);
	in_pcbpurgeif0(&V_ulitecbinfo, ifp);
	in_purgemaddrs(ifp);
	IN_MULTI_UNLOCK();
}

/*
 * Delete all IPv4 multicast address records, and associated link-layer
 * multicast address records, associated with ifp.
 * XXX It looks like domifdetach runs AFTER the link layer cleanup.
 * XXX This should not race with ifma_protospec being set during
 * a new allocation, if it does, we have bigger problems.
 */
static void
in_purgemaddrs(struct ifnet *ifp)
{
	struct in_multi_head purgeinms;
	struct in_multi		*inm;
	struct ifmultiaddr	*ifma, *next;

	SLIST_INIT(&purgeinms);
	IN_MULTI_LIST_LOCK();

	/*
	 * Extract list of in_multi associated with the detaching ifp
	 * which the PF_INET layer is about to release.
	 * We need to do this as IF_ADDR_LOCK() may be re-acquired
	 * by code further down.
	 */
	IF_ADDR_WLOCK(ifp);
 restart:
	CK_STAILQ_FOREACH_SAFE(ifma, &ifp->if_multiaddrs, ifma_link, next) {
		if (ifma->ifma_addr->sa_family != AF_INET ||
		    ifma->ifma_protospec == NULL)
			continue;
		inm = (struct in_multi *)ifma->ifma_protospec;
		inm_rele_locked(&purgeinms, inm);
		if (__predict_false(ifma_restart)) {
			ifma_restart = true;
			goto restart;
		}
	}
	IF_ADDR_WUNLOCK(ifp);

	inm_release_list_deferred(&purgeinms);
	igmp_ifdetach(ifp);
	IN_MULTI_LIST_UNLOCK();
}

struct in_llentry {
	struct llentry		base;
};

#define	IN_LLTBL_DEFAULT_HSIZE	32
#define	IN_LLTBL_HASH(k, h) \
	(((((((k >> 8) ^ k) >> 8) ^ k) >> 8) ^ k) & ((h) - 1))

/*
 * Do actual deallocation of @lle.
 */
static void
in_lltable_destroy_lle_unlocked(epoch_context_t ctx)
{
	struct llentry *lle;

	lle = __containerof(ctx, struct llentry, lle_epoch_ctx);
	LLE_LOCK_DESTROY(lle);
	LLE_REQ_DESTROY(lle);
	free(lle, M_LLTABLE);
}

/*
 * Called by the datapath to indicate that
 * the entry was used.
 */
static void
in_lltable_mark_used(struct llentry *lle)
{

	LLE_REQ_LOCK(lle);
	lle->r_skip_req = 0;
	LLE_REQ_UNLOCK(lle);
}

/*
 * Called by LLE_FREE_LOCKED when number of references
 * drops to zero.
 */
static void
in_lltable_destroy_lle(struct llentry *lle)
{

	LLE_WUNLOCK(lle);
	epoch_call(net_epoch_preempt,  &lle->lle_epoch_ctx, in_lltable_destroy_lle_unlocked);
}

static struct llentry *
in_lltable_new(struct in_addr addr4, u_int flags)
{
	struct in_llentry *lle;

	lle = malloc(sizeof(struct in_llentry), M_LLTABLE, M_NOWAIT | M_ZERO);
	if (lle == NULL)		/* NB: caller generates msg */
		return NULL;

	/*
	 * For IPv4 this will trigger "arpresolve" to generate
	 * an ARP request.
	 */
	lle->base.la_expire = time_uptime; /* mark expired */
	lle->base.r_l3addr.addr4 = addr4;
	lle->base.lle_refcnt = 1;
	lle->base.lle_free = in_lltable_destroy_lle;
	LLE_LOCK_INIT(&lle->base);
	LLE_REQ_INIT(&lle->base);
	callout_init(&lle->base.lle_timer, 1);

	return (&lle->base);
}

#define IN_ARE_MASKED_ADDR_EQUAL(d, a, m)	(		\
	((((d).s_addr ^ (a).s_addr) & (m).s_addr)) == 0 )

static int
in_lltable_match_prefix(const struct sockaddr *saddr,
    const struct sockaddr *smask, u_int flags, struct llentry *lle)
{
	struct in_addr addr, mask, lle_addr;

	addr = ((const struct sockaddr_in *)saddr)->sin_addr;
	mask = ((const struct sockaddr_in *)smask)->sin_addr;
	lle_addr.s_addr = ntohl(lle->r_l3addr.addr4.s_addr);

	if (IN_ARE_MASKED_ADDR_EQUAL(lle_addr, addr, mask) == 0)
		return (0);

	if (lle->la_flags & LLE_IFADDR) {

		/*
		 * Delete LLE_IFADDR records IFF address & flag matches.
		 * Note that addr is the interface address within prefix
		 * being matched.
		 * Note also we should handle 'ifdown' cases without removing
		 * ifaddr macs.
		 */
		if (addr.s_addr == lle_addr.s_addr && (flags & LLE_STATIC) != 0)
			return (1);
		return (0);
	}

	/* flags & LLE_STATIC means deleting both dynamic and static entries */
	if ((flags & LLE_STATIC) || !(lle->la_flags & LLE_STATIC))
		return (1);

	return (0);
}

static void
in_lltable_free_entry(struct lltable *llt, struct llentry *lle)
{
	size_t pkts_dropped;

	LLE_WLOCK_ASSERT(lle);
	KASSERT(llt != NULL, ("lltable is NULL"));

	/* Unlink entry from table if not already */
	if ((lle->la_flags & LLE_LINKED) != 0) {
		IF_AFDATA_WLOCK_ASSERT(llt->llt_ifp);
		lltable_unlink_entry(llt, lle);
	}

	/* Drop hold queue */
	pkts_dropped = llentry_free(lle);
	ARPSTAT_ADD(dropped, pkts_dropped);
}

static int
in_lltable_rtcheck(struct ifnet *ifp, u_int flags, const struct sockaddr *l3addr)
{
	struct rt_addrinfo info;
	struct sockaddr_in rt_key, rt_mask;
	struct sockaddr rt_gateway;
	int rt_flags;

	KASSERT(l3addr->sa_family == AF_INET,
	    ("sin_family %d", l3addr->sa_family));

	bzero(&rt_key, sizeof(rt_key));
	rt_key.sin_len = sizeof(rt_key);
	bzero(&rt_mask, sizeof(rt_mask));
	rt_mask.sin_len = sizeof(rt_mask);
	bzero(&rt_gateway, sizeof(rt_gateway));
	rt_gateway.sa_len = sizeof(rt_gateway);

	bzero(&info, sizeof(info));
	info.rti_info[RTAX_DST] = (struct sockaddr *)&rt_key;
	info.rti_info[RTAX_NETMASK] = (struct sockaddr *)&rt_mask;
	info.rti_info[RTAX_GATEWAY] = (struct sockaddr *)&rt_gateway;

	if (rib_lookup_info(ifp->if_fib, l3addr, NHR_REF, 0, &info) != 0)
		return (EINVAL);

	rt_flags = info.rti_flags;

	/*
	 * If the gateway for an existing host route matches the target L3
	 * address, which is a special route inserted by some implementation
	 * such as MANET, and the interface is of the correct type, then
	 * allow for ARP to proceed.
	 */
	if (rt_flags & RTF_GATEWAY) {
		if (!(rt_flags & RTF_HOST) || !info.rti_ifp ||
		    info.rti_ifp->if_type != IFT_ETHER ||
		    (info.rti_ifp->if_flags & (IFF_NOARP | IFF_STATICARP)) != 0 ||
		    memcmp(rt_gateway.sa_data, l3addr->sa_data,
		    sizeof(in_addr_t)) != 0) {
			rib_free_info(&info);
			return (EINVAL);
		}
	}
	rib_free_info(&info);

	/*
	 * Make sure that at least the destination address is covered
	 * by the route. This is for handling the case where 2 or more
	 * interfaces have the same prefix. An incoming packet arrives
	 * on one interface and the corresponding outgoing packet leaves
	 * another interface.
	 */
	if (!(rt_flags & RTF_HOST) && info.rti_ifp != ifp) {
		const char *sa, *mask, *addr, *lim;
		const struct sockaddr_in *l3sin;

		mask = (const char *)&rt_mask;
		/*
		 * Just being extra cautious to avoid some custom
		 * code getting into trouble.
		 */
		if ((info.rti_addrs & RTA_NETMASK) == 0)
			return (EINVAL);

		sa = (const char *)&rt_key;
		addr = (const char *)l3addr;
		l3sin = (const struct sockaddr_in *)l3addr;
		lim = addr + l3sin->sin_len;

		for ( ; addr < lim; sa++, mask++, addr++) {
			if ((*sa ^ *addr) & *mask) {
#ifdef DIAGNOSTIC
				char addrbuf[INET_ADDRSTRLEN];

				log(LOG_INFO, "IPv4 address: \"%s\" "
				    "is not on the network\n",
				    inet_ntoa_r(l3sin->sin_addr, addrbuf));
#endif
				return (EINVAL);
			}
		}
	}

	return (0);
}

static inline uint32_t
in_lltable_hash_dst(const struct in_addr dst, uint32_t hsize)
{

	return (IN_LLTBL_HASH(dst.s_addr, hsize));
}

static uint32_t
in_lltable_hash(const struct llentry *lle, uint32_t hsize)
{

	return (in_lltable_hash_dst(lle->r_l3addr.addr4, hsize));
}

static void
in_lltable_fill_sa_entry(const struct llentry *lle, struct sockaddr *sa)
{
	struct sockaddr_in *sin;

	sin = (struct sockaddr_in *)sa;
	bzero(sin, sizeof(*sin));
	sin->sin_family = AF_INET;
	sin->sin_len = sizeof(*sin);
	sin->sin_addr = lle->r_l3addr.addr4;
}

static inline struct llentry *
in_lltable_find_dst(struct lltable *llt, struct in_addr dst)
{
	struct llentry *lle;
	struct llentries *lleh;
	u_int hashidx;

	hashidx = in_lltable_hash_dst(dst, llt->llt_hsize);
	lleh = &llt->lle_head[hashidx];
	CK_LIST_FOREACH(lle, lleh, lle_next) {
		if (lle->la_flags & LLE_DELETED)
			continue;
		if (lle->r_l3addr.addr4.s_addr == dst.s_addr)
			break;
	}

	return (lle);
}

static void
in_lltable_delete_entry(struct lltable *llt, struct llentry *lle)
{

	lle->la_flags |= LLE_DELETED;
	EVENTHANDLER_INVOKE(lle_event, lle, LLENTRY_DELETED);
#ifdef DIAGNOSTIC
	log(LOG_INFO, "ifaddr cache = %p is deleted\n", lle);
#endif
	llentry_free(lle);
}

static struct llentry *
in_lltable_alloc(struct lltable *llt, u_int flags, const struct sockaddr *l3addr)
{
	const struct sockaddr_in *sin = (const struct sockaddr_in *)l3addr;
	struct ifnet *ifp = llt->llt_ifp;
	struct llentry *lle;
	char linkhdr[LLE_MAX_LINKHDR];
	size_t linkhdrsize;
	int lladdr_off;

	KASSERT(l3addr->sa_family == AF_INET,
	    ("sin_family %d", l3addr->sa_family));

	/*
	 * A route that covers the given address must have
	 * been installed 1st because we are doing a resolution,
	 * verify this.
	 */
	if (!(flags & LLE_IFADDR) &&
	    in_lltable_rtcheck(ifp, flags, l3addr) != 0)
		return (NULL);

	lle = in_lltable_new(sin->sin_addr, flags);
	if (lle == NULL) {
		log(LOG_INFO, "lla_lookup: new lle malloc failed\n");
		return (NULL);
	}
	lle->la_flags = flags;
	if (flags & LLE_STATIC)
		lle->r_flags |= RLLE_VALID;
	if ((flags & LLE_IFADDR) == LLE_IFADDR) {
		linkhdrsize = LLE_MAX_LINKHDR;
		if (lltable_calc_llheader(ifp, AF_INET, IF_LLADDR(ifp),
		    linkhdr, &linkhdrsize, &lladdr_off) != 0) {
			epoch_call(net_epoch_preempt,  &lle->lle_epoch_ctx, in_lltable_destroy_lle_unlocked);
			return (NULL);
		}
		lltable_set_entry_addr(ifp, lle, linkhdr, linkhdrsize,
		    lladdr_off);
		lle->la_flags |= LLE_STATIC;
		lle->r_flags |= (RLLE_VALID | RLLE_IFADDR);
	}

	return (lle);
}

/*
 * Return NULL if not found or marked for deletion.
 * If found return lle read locked.
 */
static struct llentry *
in_lltable_lookup(struct lltable *llt, u_int flags, const struct sockaddr *l3addr)
{
	const struct sockaddr_in *sin = (const struct sockaddr_in *)l3addr;
	struct llentry *lle;

	IF_AFDATA_LOCK_ASSERT(llt->llt_ifp);
	KASSERT(l3addr->sa_family == AF_INET,
	    ("sin_family %d", l3addr->sa_family));
	KASSERT((flags & (LLE_UNLOCKED | LLE_EXCLUSIVE)) !=
	    (LLE_UNLOCKED | LLE_EXCLUSIVE),
	    ("wrong lle request flags: %#x", flags));

	lle = in_lltable_find_dst(llt, sin->sin_addr);
	if (lle == NULL)
		return (NULL);
	if (flags & LLE_UNLOCKED)
		return (lle);

	if (flags & LLE_EXCLUSIVE)
		LLE_WLOCK(lle);
	else
		LLE_RLOCK(lle);

	/*
	 * If the afdata lock is not held, the LLE may have been unlinked while
	 * we were blocked on the LLE lock.  Check for this case.
	 */
	if (__predict_false((lle->la_flags & LLE_LINKED) == 0)) {
		if (flags & LLE_EXCLUSIVE)
			LLE_WUNLOCK(lle);
		else
			LLE_RUNLOCK(lle);
		return (NULL);
	}
	return (lle);
}

static int
in_lltable_dump_entry(struct lltable *llt, struct llentry *lle,
    struct sysctl_req *wr)
{
	struct ifnet *ifp = llt->llt_ifp;
	/* XXX stack use */
	struct {
		struct rt_msghdr	rtm;
		struct sockaddr_in	sin;
		struct sockaddr_dl	sdl;
	} arpc;
	struct sockaddr_dl *sdl;
	int error;

	bzero(&arpc, sizeof(arpc));
	/* skip deleted entries */
	if ((lle->la_flags & LLE_DELETED) == LLE_DELETED)
		return (0);
	/* Skip if jailed and not a valid IP of the prison. */
	lltable_fill_sa_entry(lle,(struct sockaddr *)&arpc.sin);
	if (prison_if(wr->td->td_ucred, (struct sockaddr *)&arpc.sin) != 0)
		return (0);
	/*
	 * produce a msg made of:
	 *  struct rt_msghdr;
	 *  struct sockaddr_in; (IPv4)
	 *  struct sockaddr_dl;
	 */
	arpc.rtm.rtm_msglen = sizeof(arpc);
	arpc.rtm.rtm_version = RTM_VERSION;
	arpc.rtm.rtm_type = RTM_GET;
	arpc.rtm.rtm_flags = RTF_UP;
	arpc.rtm.rtm_addrs = RTA_DST | RTA_GATEWAY;

	/* publish */
	if (lle->la_flags & LLE_PUB)
		arpc.rtm.rtm_flags |= RTF_ANNOUNCE;

	sdl = &arpc.sdl;
	sdl->sdl_family = AF_LINK;
	sdl->sdl_len = sizeof(*sdl);
	sdl->sdl_index = ifp->if_index;
	sdl->sdl_type = ifp->if_type;
	if ((lle->la_flags & LLE_VALID) == LLE_VALID) {
		sdl->sdl_alen = ifp->if_addrlen;
		bcopy(lle->ll_addr, LLADDR(sdl), ifp->if_addrlen);
	} else {
		sdl->sdl_alen = 0;
		bzero(LLADDR(sdl), ifp->if_addrlen);
	}

	arpc.rtm.rtm_rmx.rmx_expire =
	    lle->la_flags & LLE_STATIC ? 0 : lle->la_expire;
	arpc.rtm.rtm_flags |= (RTF_HOST | RTF_LLDATA);
	if (lle->la_flags & LLE_STATIC)
		arpc.rtm.rtm_flags |= RTF_STATIC;
	if (lle->la_flags & LLE_IFADDR)
		arpc.rtm.rtm_flags |= RTF_PINNED;
	arpc.rtm.rtm_index = ifp->if_index;
	error = SYSCTL_OUT(wr, &arpc, sizeof(arpc));

	return (error);
}

static struct lltable *
in_lltattach(struct ifnet *ifp)
{
	struct lltable *llt;

	llt = lltable_allocate_htbl(IN_LLTBL_DEFAULT_HSIZE);
 	llt->llt_af = AF_INET;
 	llt->llt_ifp = ifp;

	llt->llt_lookup = in_lltable_lookup;
	llt->llt_alloc_entry = in_lltable_alloc;
	llt->llt_delete_entry = in_lltable_delete_entry;
	llt->llt_dump_entry = in_lltable_dump_entry;
	llt->llt_hash = in_lltable_hash;
	llt->llt_fill_sa_entry = in_lltable_fill_sa_entry;
	llt->llt_free_entry = in_lltable_free_entry;
	llt->llt_match_prefix = in_lltable_match_prefix;
	llt->llt_mark_used = in_lltable_mark_used;
 	lltable_link(llt);

	return (llt);
}

void *
in_domifattach(struct ifnet *ifp)
{
	struct in_ifinfo *ii;

	ii = malloc(sizeof(struct in_ifinfo), M_IFADDR, M_WAITOK|M_ZERO);

	ii->ii_llt = in_lltattach(ifp);
	ii->ii_igmp = igmp_domifattach(ifp);

	return (ii);
}

void
in_domifdetach(struct ifnet *ifp, void *aux)
{
	struct in_ifinfo *ii = (struct in_ifinfo *)aux;

	igmp_domifdetach(ifp);
	lltable_free(ii->ii_llt);
	free(ii, M_IFADDR);
}
