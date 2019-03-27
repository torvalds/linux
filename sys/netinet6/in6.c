/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
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
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	$KAME: in6.c,v 1.259 2002/01/21 11:37:50 keiichi Exp $
 */

/*-
 * Copyright (c) 1982, 1986, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)in.c	8.2 (Berkeley) 11/15/93
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet.h"
#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/eventhandler.h>
#include <sys/errno.h>
#include <sys/jail.h>
#include <sys/malloc.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sockio.h>
#include <sys/systm.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/protosw.h>
#include <sys/time.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/rmlock.h>
#include <sys/syslog.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_types.h>
#include <net/route.h>
#include <net/if_dl.h>
#include <net/vnet.h>

#include <netinet/in.h>
#include <netinet/in_var.h>
#include <net/if_llatbl.h>
#include <netinet/if_ether.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/in_pcb.h>
#include <netinet/ip_carp.h>

#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>
#include <netinet6/nd6.h>
#include <netinet6/mld6_var.h>
#include <netinet6/ip6_mroute.h>
#include <netinet6/in6_ifattach.h>
#include <netinet6/scope6_var.h>
#include <netinet6/in6_fib.h>
#include <netinet6/in6_pcb.h>


/*
 * struct in6_ifreq and struct ifreq must be type punnable for common members
 * of ifr_ifru to allow accessors to be shared.
 */
_Static_assert(offsetof(struct in6_ifreq, ifr_ifru) ==
    offsetof(struct ifreq, ifr_ifru),
    "struct in6_ifreq and struct ifreq are not type punnable");

VNET_DECLARE(int, icmp6_nodeinfo_oldmcprefix);
#define V_icmp6_nodeinfo_oldmcprefix	VNET(icmp6_nodeinfo_oldmcprefix)

/*
 * Definitions of some costant IP6 addresses.
 */
const struct in6_addr in6addr_any = IN6ADDR_ANY_INIT;
const struct in6_addr in6addr_loopback = IN6ADDR_LOOPBACK_INIT;
const struct in6_addr in6addr_nodelocal_allnodes =
	IN6ADDR_NODELOCAL_ALLNODES_INIT;
const struct in6_addr in6addr_linklocal_allnodes =
	IN6ADDR_LINKLOCAL_ALLNODES_INIT;
const struct in6_addr in6addr_linklocal_allrouters =
	IN6ADDR_LINKLOCAL_ALLROUTERS_INIT;
const struct in6_addr in6addr_linklocal_allv2routers =
	IN6ADDR_LINKLOCAL_ALLV2ROUTERS_INIT;

const struct in6_addr in6mask0 = IN6MASK0;
const struct in6_addr in6mask32 = IN6MASK32;
const struct in6_addr in6mask64 = IN6MASK64;
const struct in6_addr in6mask96 = IN6MASK96;
const struct in6_addr in6mask128 = IN6MASK128;

const struct sockaddr_in6 sa6_any =
	{ sizeof(sa6_any), AF_INET6, 0, 0, IN6ADDR_ANY_INIT, 0 };

static int in6_notify_ifa(struct ifnet *, struct in6_ifaddr *,
	struct in6_aliasreq *, int);
static void in6_unlink_ifa(struct in6_ifaddr *, struct ifnet *);

static int in6_validate_ifra(struct ifnet *, struct in6_aliasreq *,
    struct in6_ifaddr *, int);
static struct in6_ifaddr *in6_alloc_ifa(struct ifnet *,
    struct in6_aliasreq *, int flags);
static int in6_update_ifa_internal(struct ifnet *, struct in6_aliasreq *,
    struct in6_ifaddr *, int, int);
static int in6_broadcast_ifa(struct ifnet *, struct in6_aliasreq *,
    struct in6_ifaddr *, int);

#define ifa2ia6(ifa)	((struct in6_ifaddr *)(ifa))
#define ia62ifa(ia6)	(&((ia6)->ia_ifa))


void
in6_newaddrmsg(struct in6_ifaddr *ia, int cmd)
{
	struct sockaddr_dl gateway;
	struct sockaddr_in6 mask, addr;
	struct rtentry rt;
	int fibnum;

	/*
	 * initialize for rtmsg generation
	 */
	bzero(&gateway, sizeof(gateway));
	gateway.sdl_len = sizeof(gateway);
	gateway.sdl_family = AF_LINK;

	bzero(&rt, sizeof(rt));
	rt.rt_gateway = (struct sockaddr *)&gateway;
	memcpy(&mask, &ia->ia_prefixmask, sizeof(ia->ia_prefixmask));
	memcpy(&addr, &ia->ia_addr, sizeof(ia->ia_addr));
	rt_mask(&rt) = (struct sockaddr *)&mask;
	rt_key(&rt) = (struct sockaddr *)&addr;
	rt.rt_flags = RTF_HOST | RTF_STATIC;
	if (cmd == RTM_ADD)
		rt.rt_flags |= RTF_UP;
	fibnum = V_rt_add_addr_allfibs ? RT_ALL_FIBS : ia62ifa(ia)->ifa_ifp->if_fib;
	/* Announce arrival of local address to this FIB. */
	rt_newaddrmsg_fib(cmd, &ia->ia_ifa, 0, &rt, fibnum);
}

int
in6_mask2len(struct in6_addr *mask, u_char *lim0)
{
	int x = 0, y;
	u_char *lim = lim0, *p;

	/* ignore the scope_id part */
	if (lim0 == NULL || lim0 - (u_char *)mask > sizeof(*mask))
		lim = (u_char *)mask + sizeof(*mask);
	for (p = (u_char *)mask; p < lim; x++, p++) {
		if (*p != 0xff)
			break;
	}
	y = 0;
	if (p < lim) {
		for (y = 0; y < 8; y++) {
			if ((*p & (0x80 >> y)) == 0)
				break;
		}
	}

	/*
	 * when the limit pointer is given, do a stricter check on the
	 * remaining bits.
	 */
	if (p < lim) {
		if (y != 0 && (*p & (0x00ff >> y)) != 0)
			return (-1);
		for (p = p + 1; p < lim; p++)
			if (*p != 0)
				return (-1);
	}

	return x * 8 + y;
}

#ifdef COMPAT_FREEBSD32
struct in6_ndifreq32 {
	char ifname[IFNAMSIZ];
	uint32_t ifindex;
};
#define	SIOCGDEFIFACE32_IN6	_IOWR('i', 86, struct in6_ndifreq32)
#endif

int
in6_control(struct socket *so, u_long cmd, caddr_t data,
    struct ifnet *ifp, struct thread *td)
{
	struct	in6_ifreq *ifr = (struct in6_ifreq *)data;
	struct	in6_ifaddr *ia = NULL;
	struct	in6_aliasreq *ifra = (struct in6_aliasreq *)data;
	struct sockaddr_in6 *sa6;
	int carp_attached = 0;
	int error;
	u_long ocmd = cmd;

	/*
	 * Compat to make pre-10.x ifconfig(8) operable.
	 */
	if (cmd == OSIOCAIFADDR_IN6)
		cmd = SIOCAIFADDR_IN6;

	switch (cmd) {
	case SIOCGETSGCNT_IN6:
	case SIOCGETMIFCNT_IN6:
		/*
		 * XXX mrt_ioctl has a 3rd, unused, FIB argument in route.c.
		 * We cannot see how that would be needed, so do not adjust the
		 * KPI blindly; more likely should clean up the IPv4 variant.
		 */
		return (mrt6_ioctl ? mrt6_ioctl(cmd, data) : EOPNOTSUPP);
	}

	switch (cmd) {
	case SIOCAADDRCTL_POLICY:
	case SIOCDADDRCTL_POLICY:
		if (td != NULL) {
			error = priv_check(td, PRIV_NETINET_ADDRCTRL6);
			if (error)
				return (error);
		}
		return (in6_src_ioctl(cmd, data));
	}

	if (ifp == NULL)
		return (EOPNOTSUPP);

	switch (cmd) {
	case SIOCSNDFLUSH_IN6:
	case SIOCSPFXFLUSH_IN6:
	case SIOCSRTRFLUSH_IN6:
	case SIOCSDEFIFACE_IN6:
	case SIOCSIFINFO_FLAGS:
	case SIOCSIFINFO_IN6:
		if (td != NULL) {
			error = priv_check(td, PRIV_NETINET_ND6);
			if (error)
				return (error);
		}
		/* FALLTHROUGH */
	case OSIOCGIFINFO_IN6:
	case SIOCGIFINFO_IN6:
	case SIOCGNBRINFO_IN6:
	case SIOCGDEFIFACE_IN6:
		return (nd6_ioctl(cmd, data, ifp));

#ifdef COMPAT_FREEBSD32
	case SIOCGDEFIFACE32_IN6:
		{
			struct in6_ndifreq ndif;
			struct in6_ndifreq32 *ndif32;

			error = nd6_ioctl(SIOCGDEFIFACE_IN6, (caddr_t)&ndif,
			    ifp);
			if (error)
				return (error);
			ndif32 = (struct in6_ndifreq32 *)data;
			ndif32->ifindex = ndif.ifindex;
			return (0);
		}
#endif
	}

	switch (cmd) {
	case SIOCSIFPREFIX_IN6:
	case SIOCDIFPREFIX_IN6:
	case SIOCAIFPREFIX_IN6:
	case SIOCCIFPREFIX_IN6:
	case SIOCSGIFPREFIX_IN6:
	case SIOCGIFPREFIX_IN6:
		log(LOG_NOTICE,
		    "prefix ioctls are now invalidated. "
		    "please use ifconfig.\n");
		return (EOPNOTSUPP);
	}

	switch (cmd) {
	case SIOCSSCOPE6:
		if (td != NULL) {
			error = priv_check(td, PRIV_NETINET_SCOPE6);
			if (error)
				return (error);
		}
		/* FALLTHROUGH */
	case SIOCGSCOPE6:
	case SIOCGSCOPE6DEF:
		return (scope6_ioctl(cmd, data, ifp));
	}

	/*
	 * Find address for this interface, if it exists.
	 *
	 * In netinet code, we have checked ifra_addr in SIOCSIF*ADDR operation
	 * only, and used the first interface address as the target of other
	 * operations (without checking ifra_addr).  This was because netinet
	 * code/API assumed at most 1 interface address per interface.
	 * Since IPv6 allows a node to assign multiple addresses
	 * on a single interface, we almost always look and check the
	 * presence of ifra_addr, and reject invalid ones here.
	 * It also decreases duplicated code among SIOC*_IN6 operations.
	 */
	switch (cmd) {
	case SIOCAIFADDR_IN6:
	case SIOCSIFPHYADDR_IN6:
		sa6 = &ifra->ifra_addr;
		break;
	case SIOCSIFADDR_IN6:
	case SIOCGIFADDR_IN6:
	case SIOCSIFDSTADDR_IN6:
	case SIOCSIFNETMASK_IN6:
	case SIOCGIFDSTADDR_IN6:
	case SIOCGIFNETMASK_IN6:
	case SIOCDIFADDR_IN6:
	case SIOCGIFPSRCADDR_IN6:
	case SIOCGIFPDSTADDR_IN6:
	case SIOCGIFAFLAG_IN6:
	case SIOCSNDFLUSH_IN6:
	case SIOCSPFXFLUSH_IN6:
	case SIOCSRTRFLUSH_IN6:
	case SIOCGIFALIFETIME_IN6:
	case SIOCGIFSTAT_IN6:
	case SIOCGIFSTAT_ICMP6:
		sa6 = &ifr->ifr_addr;
		break;
	case SIOCSIFADDR:
	case SIOCSIFBRDADDR:
	case SIOCSIFDSTADDR:
	case SIOCSIFNETMASK:
		/*
		 * Although we should pass any non-INET6 ioctl requests
		 * down to driver, we filter some legacy INET requests.
		 * Drivers trust SIOCSIFADDR et al to come from an already
		 * privileged layer, and do not perform any credentials
		 * checks or input validation.
		 */
		return (EINVAL);
	default:
		sa6 = NULL;
		break;
	}
	if (sa6 && sa6->sin6_family == AF_INET6) {
		if (sa6->sin6_scope_id != 0)
			error = sa6_embedscope(sa6, 0);
		else
			error = in6_setscope(&sa6->sin6_addr, ifp, NULL);
		if (error != 0)
			return (error);
		if (td != NULL && (error = prison_check_ip6(td->td_ucred,
		    &sa6->sin6_addr)) != 0)
			return (error);
		ia = in6ifa_ifpwithaddr(ifp, &sa6->sin6_addr);
	} else
		ia = NULL;

	switch (cmd) {
	case SIOCSIFADDR_IN6:
	case SIOCSIFDSTADDR_IN6:
	case SIOCSIFNETMASK_IN6:
		/*
		 * Since IPv6 allows a node to assign multiple addresses
		 * on a single interface, SIOCSIFxxx ioctls are deprecated.
		 */
		/* we decided to obsolete this command (20000704) */
		error = EINVAL;
		goto out;

	case SIOCDIFADDR_IN6:
		/*
		 * for IPv4, we look for existing in_ifaddr here to allow
		 * "ifconfig if0 delete" to remove the first IPv4 address on
		 * the interface.  For IPv6, as the spec allows multiple
		 * interface address from the day one, we consider "remove the
		 * first one" semantics to be not preferable.
		 */
		if (ia == NULL) {
			error = EADDRNOTAVAIL;
			goto out;
		}
		/* FALLTHROUGH */
	case SIOCAIFADDR_IN6:
		/*
		 * We always require users to specify a valid IPv6 address for
		 * the corresponding operation.
		 */
		if (ifra->ifra_addr.sin6_family != AF_INET6 ||
		    ifra->ifra_addr.sin6_len != sizeof(struct sockaddr_in6)) {
			error = EAFNOSUPPORT;
			goto out;
		}

		if (td != NULL) {
			error = priv_check(td, (cmd == SIOCDIFADDR_IN6) ?
			    PRIV_NET_DELIFADDR : PRIV_NET_ADDIFADDR);
			if (error)
				goto out;
		}
		/* FALLTHROUGH */
	case SIOCGIFSTAT_IN6:
	case SIOCGIFSTAT_ICMP6:
		if (ifp->if_afdata[AF_INET6] == NULL) {
			error = EPFNOSUPPORT;
			goto out;
		}
		break;

	case SIOCGIFADDR_IN6:
		/* This interface is basically deprecated. use SIOCGIFCONF. */
		/* FALLTHROUGH */
	case SIOCGIFAFLAG_IN6:
	case SIOCGIFNETMASK_IN6:
	case SIOCGIFDSTADDR_IN6:
	case SIOCGIFALIFETIME_IN6:
		/* must think again about its semantics */
		if (ia == NULL) {
			error = EADDRNOTAVAIL;
			goto out;
		}
		break;
	}

	switch (cmd) {
	case SIOCGIFADDR_IN6:
		ifr->ifr_addr = ia->ia_addr;
		if ((error = sa6_recoverscope(&ifr->ifr_addr)) != 0)
			goto out;
		break;

	case SIOCGIFDSTADDR_IN6:
		if ((ifp->if_flags & IFF_POINTOPOINT) == 0) {
			error = EINVAL;
			goto out;
		}
		ifr->ifr_dstaddr = ia->ia_dstaddr;
		if ((error = sa6_recoverscope(&ifr->ifr_dstaddr)) != 0)
			goto out;
		break;

	case SIOCGIFNETMASK_IN6:
		ifr->ifr_addr = ia->ia_prefixmask;
		break;

	case SIOCGIFAFLAG_IN6:
		ifr->ifr_ifru.ifru_flags6 = ia->ia6_flags;
		break;

	case SIOCGIFSTAT_IN6:
		COUNTER_ARRAY_COPY(((struct in6_ifextra *)
		    ifp->if_afdata[AF_INET6])->in6_ifstat,
		    &ifr->ifr_ifru.ifru_stat,
		    sizeof(struct in6_ifstat) / sizeof(uint64_t));
		break;

	case SIOCGIFSTAT_ICMP6:
		COUNTER_ARRAY_COPY(((struct in6_ifextra *)
		    ifp->if_afdata[AF_INET6])->icmp6_ifstat,
		    &ifr->ifr_ifru.ifru_icmp6stat,
		    sizeof(struct icmp6_ifstat) / sizeof(uint64_t));
		break;

	case SIOCGIFALIFETIME_IN6:
		ifr->ifr_ifru.ifru_lifetime = ia->ia6_lifetime;
		if (ia->ia6_lifetime.ia6t_vltime != ND6_INFINITE_LIFETIME) {
			time_t maxexpire;
			struct in6_addrlifetime *retlt =
			    &ifr->ifr_ifru.ifru_lifetime;

			/*
			 * XXX: adjust expiration time assuming time_t is
			 * signed.
			 */
			maxexpire = (-1) &
			    ~((time_t)1 << ((sizeof(maxexpire) * 8) - 1));
			if (ia->ia6_lifetime.ia6t_vltime <
			    maxexpire - ia->ia6_updatetime) {
				retlt->ia6t_expire = ia->ia6_updatetime +
				    ia->ia6_lifetime.ia6t_vltime;
			} else
				retlt->ia6t_expire = maxexpire;
		}
		if (ia->ia6_lifetime.ia6t_pltime != ND6_INFINITE_LIFETIME) {
			time_t maxexpire;
			struct in6_addrlifetime *retlt =
			    &ifr->ifr_ifru.ifru_lifetime;

			/*
			 * XXX: adjust expiration time assuming time_t is
			 * signed.
			 */
			maxexpire = (-1) &
			    ~((time_t)1 << ((sizeof(maxexpire) * 8) - 1));
			if (ia->ia6_lifetime.ia6t_pltime <
			    maxexpire - ia->ia6_updatetime) {
				retlt->ia6t_preferred = ia->ia6_updatetime +
				    ia->ia6_lifetime.ia6t_pltime;
			} else
				retlt->ia6t_preferred = maxexpire;
		}
		break;

	case SIOCAIFADDR_IN6:
	{
		struct nd_prefixctl pr0;
		struct nd_prefix *pr;

		/*
		 * first, make or update the interface address structure,
		 * and link it to the list.
		 */
		if ((error = in6_update_ifa(ifp, ifra, ia, 0)) != 0)
			goto out;
		if (ia != NULL) {
			if (ia->ia_ifa.ifa_carp)
				(*carp_detach_p)(&ia->ia_ifa, true);
			ifa_free(&ia->ia_ifa);
		}
		if ((ia = in6ifa_ifpwithaddr(ifp, &ifra->ifra_addr.sin6_addr))
		    == NULL) {
			/*
			 * this can happen when the user specify the 0 valid
			 * lifetime.
			 */
			break;
		}

		if (cmd == ocmd && ifra->ifra_vhid > 0) {
			if (carp_attach_p != NULL)
				error = (*carp_attach_p)(&ia->ia_ifa,
				    ifra->ifra_vhid);
			else
				error = EPROTONOSUPPORT;
			if (error)
				goto out;
			else
				carp_attached = 1;
		}

		/*
		 * then, make the prefix on-link on the interface.
		 * XXX: we'd rather create the prefix before the address, but
		 * we need at least one address to install the corresponding
		 * interface route, so we configure the address first.
		 */

		/*
		 * convert mask to prefix length (prefixmask has already
		 * been validated in in6_update_ifa().
		 */
		bzero(&pr0, sizeof(pr0));
		pr0.ndpr_ifp = ifp;
		pr0.ndpr_plen = in6_mask2len(&ifra->ifra_prefixmask.sin6_addr,
		    NULL);
		if (pr0.ndpr_plen == 128) {
			/* we don't need to install a host route. */
			goto aifaddr_out;
		}
		pr0.ndpr_prefix = ifra->ifra_addr;
		/* apply the mask for safety. */
		IN6_MASK_ADDR(&pr0.ndpr_prefix.sin6_addr,
		    &ifra->ifra_prefixmask.sin6_addr);

		/*
		 * XXX: since we don't have an API to set prefix (not address)
		 * lifetimes, we just use the same lifetimes as addresses.
		 * The (temporarily) installed lifetimes can be overridden by
		 * later advertised RAs (when accept_rtadv is non 0), which is
		 * an intended behavior.
		 */
		pr0.ndpr_raf_onlink = 1; /* should be configurable? */
		pr0.ndpr_raf_auto =
		    ((ifra->ifra_flags & IN6_IFF_AUTOCONF) != 0);
		pr0.ndpr_vltime = ifra->ifra_lifetime.ia6t_vltime;
		pr0.ndpr_pltime = ifra->ifra_lifetime.ia6t_pltime;

		/* add the prefix if not yet. */
		if ((pr = nd6_prefix_lookup(&pr0)) == NULL) {
			/*
			 * nd6_prelist_add will install the corresponding
			 * interface route.
			 */
			if ((error = nd6_prelist_add(&pr0, NULL, &pr)) != 0) {
				if (carp_attached)
					(*carp_detach_p)(&ia->ia_ifa, false);
				goto out;
			}
		}

		/* relate the address to the prefix */
		if (ia->ia6_ndpr == NULL) {
			ia->ia6_ndpr = pr;
			pr->ndpr_addrcnt++;

			/*
			 * If this is the first autoconf address from the
			 * prefix, create a temporary address as well
			 * (when required).
			 */
			if ((ia->ia6_flags & IN6_IFF_AUTOCONF) &&
			    V_ip6_use_tempaddr && pr->ndpr_addrcnt == 1) {
				int e;
				if ((e = in6_tmpifadd(ia, 1, 0)) != 0) {
					log(LOG_NOTICE, "in6_control: failed "
					    "to create a temporary address, "
					    "errno=%d\n", e);
				}
			}
		}
		nd6_prefix_rele(pr);

		/*
		 * this might affect the status of autoconfigured addresses,
		 * that is, this address might make other addresses detached.
		 */
		pfxlist_onlink_check();

aifaddr_out:
		/*
		 * Try to clear the flag when a new IPv6 address is added
		 * onto an IFDISABLED interface and it succeeds.
		 */
		if (ND_IFINFO(ifp)->flags & ND6_IFF_IFDISABLED) {
			struct in6_ndireq nd;

			memset(&nd, 0, sizeof(nd));
			nd.ndi.flags = ND_IFINFO(ifp)->flags;
			nd.ndi.flags &= ~ND6_IFF_IFDISABLED;
			if (nd6_ioctl(SIOCSIFINFO_FLAGS, (caddr_t)&nd, ifp) < 0)
				log(LOG_NOTICE, "SIOCAIFADDR_IN6: "
				    "SIOCSIFINFO_FLAGS for -ifdisabled "
				    "failed.");
			/*
			 * Ignore failure of clearing the flag intentionally.
			 * The failure means address duplication was detected.
			 */
		}
		break;
	}

	case SIOCDIFADDR_IN6:
	{
		struct nd_prefix *pr;

		/*
		 * If the address being deleted is the only one that owns
		 * the corresponding prefix, expire the prefix as well.
		 * XXX: theoretically, we don't have to worry about such
		 * relationship, since we separate the address management
		 * and the prefix management.  We do this, however, to provide
		 * as much backward compatibility as possible in terms of
		 * the ioctl operation.
		 * Note that in6_purgeaddr() will decrement ndpr_addrcnt.
		 */
		pr = ia->ia6_ndpr;
		in6_purgeaddr(&ia->ia_ifa);
		if (pr != NULL && pr->ndpr_addrcnt == 0) {
			ND6_WLOCK();
			nd6_prefix_unlink(pr, NULL);
			ND6_WUNLOCK();
			nd6_prefix_del(pr);
		}
		EVENTHANDLER_INVOKE(ifaddr_event_ext, ifp, &ia->ia_ifa,
		    IFADDR_EVENT_DEL);
		break;
	}

	default:
		if (ifp->if_ioctl == NULL) {
			error = EOPNOTSUPP;
			goto out;
		}
		error = (*ifp->if_ioctl)(ifp, cmd, data);
		goto out;
	}

	error = 0;
out:
	if (ia != NULL)
		ifa_free(&ia->ia_ifa);
	return (error);
}


static struct in6_multi_mship *
in6_joingroup_legacy(struct ifnet *ifp, const struct in6_addr *mcaddr,
    int *errorp, int delay)
{
	struct in6_multi_mship *imm;
	int error;

	imm = malloc(sizeof(*imm), M_IP6MADDR, M_NOWAIT);
	if (imm == NULL) {
		*errorp = ENOBUFS;
		return (NULL);
	}

	delay = (delay * PR_FASTHZ) / hz;

	error = in6_joingroup(ifp, mcaddr, NULL, &imm->i6mm_maddr, delay);
	if (error) {
		*errorp = error;
		free(imm, M_IP6MADDR);
		return (NULL);
	}

	return (imm);
}
/*
 * Join necessary multicast groups.  Factored out from in6_update_ifa().
 * This entire work should only be done once, for the default FIB.
 */
static int
in6_update_ifa_join_mc(struct ifnet *ifp, struct in6_aliasreq *ifra,
    struct in6_ifaddr *ia, int flags, struct in6_multi **in6m_sol)
{
	char ip6buf[INET6_ADDRSTRLEN];
	struct in6_addr mltaddr;
	struct in6_multi_mship *imm;
	int delay, error;

	KASSERT(in6m_sol != NULL, ("%s: in6m_sol is NULL", __func__));

	/* Join solicited multicast addr for new host id. */
	bzero(&mltaddr, sizeof(struct in6_addr));
	mltaddr.s6_addr32[0] = IPV6_ADDR_INT32_MLL;
	mltaddr.s6_addr32[2] = htonl(1);
	mltaddr.s6_addr32[3] = ifra->ifra_addr.sin6_addr.s6_addr32[3];
	mltaddr.s6_addr8[12] = 0xff;
	if ((error = in6_setscope(&mltaddr, ifp, NULL)) != 0) {
		/* XXX: should not happen */
		log(LOG_ERR, "%s: in6_setscope failed\n", __func__);
		goto cleanup;
	}
	delay = error = 0;
	if ((flags & IN6_IFAUPDATE_DADDELAY)) {
		/*
		 * We need a random delay for DAD on the address being
		 * configured.  It also means delaying transmission of the
		 * corresponding MLD report to avoid report collision.
		 * [RFC 4861, Section 6.3.7]
		 */
		delay = arc4random() % (MAX_RTR_SOLICITATION_DELAY * hz);
	}
	imm = in6_joingroup_legacy(ifp, &mltaddr, &error, delay);
	if (imm == NULL) {
		nd6log((LOG_WARNING, "%s: in6_joingroup failed for %s on %s "
		    "(errno=%d)\n", __func__, ip6_sprintf(ip6buf, &mltaddr),
		    if_name(ifp), error));
		goto cleanup;
	}
	LIST_INSERT_HEAD(&ia->ia6_memberships, imm, i6mm_chain);
	*in6m_sol = imm->i6mm_maddr;

	/*
	 * Join link-local all-nodes address.
	 */
	mltaddr = in6addr_linklocal_allnodes;
	if ((error = in6_setscope(&mltaddr, ifp, NULL)) != 0)
		goto cleanup; /* XXX: should not fail */

	imm = in6_joingroup_legacy(ifp, &mltaddr, &error, 0);
	if (imm == NULL) {
		nd6log((LOG_WARNING, "%s: in6_joingroup failed for %s on %s "
		    "(errno=%d)\n", __func__, ip6_sprintf(ip6buf, &mltaddr),
		    if_name(ifp), error));
		goto cleanup;
	}
	LIST_INSERT_HEAD(&ia->ia6_memberships, imm, i6mm_chain);

	/*
	 * Join node information group address.
	 */
	delay = 0;
	if ((flags & IN6_IFAUPDATE_DADDELAY)) {
		/*
		 * The spec does not say anything about delay for this group,
		 * but the same logic should apply.
		 */
		delay = arc4random() % (MAX_RTR_SOLICITATION_DELAY * hz);
	}
	if (in6_nigroup(ifp, NULL, -1, &mltaddr) == 0) {
		/* XXX jinmei */
		imm = in6_joingroup_legacy(ifp, &mltaddr, &error, delay);
		if (imm == NULL)
			nd6log((LOG_WARNING,
			    "%s: in6_joingroup failed for %s on %s "
			    "(errno=%d)\n", __func__, ip6_sprintf(ip6buf,
			    &mltaddr), if_name(ifp), error));
			/* XXX not very fatal, go on... */
		else
			LIST_INSERT_HEAD(&ia->ia6_memberships, imm, i6mm_chain);
	}
	if (V_icmp6_nodeinfo_oldmcprefix &&
	    in6_nigroup_oldmcprefix(ifp, NULL, -1, &mltaddr) == 0) {
		imm = in6_joingroup_legacy(ifp, &mltaddr, &error, delay);
		if (imm == NULL)
			nd6log((LOG_WARNING,
			    "%s: in6_joingroup failed for %s on %s "
			    "(errno=%d)\n", __func__, ip6_sprintf(ip6buf,
			    &mltaddr), if_name(ifp), error));
			/* XXX not very fatal, go on... */
		else
			LIST_INSERT_HEAD(&ia->ia6_memberships, imm, i6mm_chain);
	}

	/*
	 * Join interface-local all-nodes address.
	 * (ff01::1%ifN, and ff01::%ifN/32)
	 */
	mltaddr = in6addr_nodelocal_allnodes;
	if ((error = in6_setscope(&mltaddr, ifp, NULL)) != 0)
		goto cleanup; /* XXX: should not fail */

	imm = in6_joingroup_legacy(ifp, &mltaddr, &error, 0);
	if (imm == NULL) {
		nd6log((LOG_WARNING, "%s: in6_joingroup failed for %s on %s "
		    "(errno=%d)\n", __func__, ip6_sprintf(ip6buf,
		    &mltaddr), if_name(ifp), error));
		goto cleanup;
	}
	LIST_INSERT_HEAD(&ia->ia6_memberships, imm, i6mm_chain);

cleanup:
	return (error);
}

/*
 * Update parameters of an IPv6 interface address.
 * If necessary, a new entry is created and linked into address chains.
 * This function is separated from in6_control().
 */
int
in6_update_ifa(struct ifnet *ifp, struct in6_aliasreq *ifra,
    struct in6_ifaddr *ia, int flags)
{
	int error, hostIsNew = 0;

	if ((error = in6_validate_ifra(ifp, ifra, ia, flags)) != 0)
		return (error);

	if (ia == NULL) {
		hostIsNew = 1;
		if ((ia = in6_alloc_ifa(ifp, ifra, flags)) == NULL)
			return (ENOBUFS);
	}

	error = in6_update_ifa_internal(ifp, ifra, ia, hostIsNew, flags);
	if (error != 0) {
		if (hostIsNew != 0) {
			in6_unlink_ifa(ia, ifp);
			ifa_free(&ia->ia_ifa);
		}
		return (error);
	}

	if (hostIsNew)
		error = in6_broadcast_ifa(ifp, ifra, ia, flags);

	return (error);
}

/*
 * Fill in basic IPv6 address request info.
 */
void
in6_prepare_ifra(struct in6_aliasreq *ifra, const struct in6_addr *addr,
    const struct in6_addr *mask)
{

	memset(ifra, 0, sizeof(struct in6_aliasreq));

	ifra->ifra_addr.sin6_family = AF_INET6;
	ifra->ifra_addr.sin6_len = sizeof(struct sockaddr_in6);
	if (addr != NULL)
		ifra->ifra_addr.sin6_addr = *addr;

	ifra->ifra_prefixmask.sin6_family = AF_INET6;
	ifra->ifra_prefixmask.sin6_len = sizeof(struct sockaddr_in6);
	if (mask != NULL)
		ifra->ifra_prefixmask.sin6_addr = *mask;
}

static int
in6_validate_ifra(struct ifnet *ifp, struct in6_aliasreq *ifra,
    struct in6_ifaddr *ia, int flags)
{
	int plen = -1;
	struct sockaddr_in6 dst6;
	struct in6_addrlifetime *lt;
	char ip6buf[INET6_ADDRSTRLEN];

	/* Validate parameters */
	if (ifp == NULL || ifra == NULL) /* this maybe redundant */
		return (EINVAL);

	/*
	 * The destination address for a p2p link must have a family
	 * of AF_UNSPEC or AF_INET6.
	 */
	if ((ifp->if_flags & IFF_POINTOPOINT) != 0 &&
	    ifra->ifra_dstaddr.sin6_family != AF_INET6 &&
	    ifra->ifra_dstaddr.sin6_family != AF_UNSPEC)
		return (EAFNOSUPPORT);

	/*
	 * Validate address
	 */
	if (ifra->ifra_addr.sin6_len != sizeof(struct sockaddr_in6) ||
	    ifra->ifra_addr.sin6_family != AF_INET6)
		return (EINVAL);

	/*
	 * validate ifra_prefixmask.  don't check sin6_family, netmask
	 * does not carry fields other than sin6_len.
	 */
	if (ifra->ifra_prefixmask.sin6_len > sizeof(struct sockaddr_in6))
		return (EINVAL);
	/*
	 * Because the IPv6 address architecture is classless, we require
	 * users to specify a (non 0) prefix length (mask) for a new address.
	 * We also require the prefix (when specified) mask is valid, and thus
	 * reject a non-consecutive mask.
	 */
	if (ia == NULL && ifra->ifra_prefixmask.sin6_len == 0)
		return (EINVAL);
	if (ifra->ifra_prefixmask.sin6_len != 0) {
		plen = in6_mask2len(&ifra->ifra_prefixmask.sin6_addr,
		    (u_char *)&ifra->ifra_prefixmask +
		    ifra->ifra_prefixmask.sin6_len);
		if (plen <= 0)
			return (EINVAL);
	} else {
		/*
		 * In this case, ia must not be NULL.  We just use its prefix
		 * length.
		 */
		plen = in6_mask2len(&ia->ia_prefixmask.sin6_addr, NULL);
	}
	/*
	 * If the destination address on a p2p interface is specified,
	 * and the address is a scoped one, validate/set the scope
	 * zone identifier.
	 */
	dst6 = ifra->ifra_dstaddr;
	if ((ifp->if_flags & (IFF_POINTOPOINT|IFF_LOOPBACK)) != 0 &&
	    (dst6.sin6_family == AF_INET6)) {
		struct in6_addr in6_tmp;
		u_int32_t zoneid;

		in6_tmp = dst6.sin6_addr;
		if (in6_setscope(&in6_tmp, ifp, &zoneid))
			return (EINVAL); /* XXX: should be impossible */

		if (dst6.sin6_scope_id != 0) {
			if (dst6.sin6_scope_id != zoneid)
				return (EINVAL);
		} else		/* user omit to specify the ID. */
			dst6.sin6_scope_id = zoneid;

		/* convert into the internal form */
		if (sa6_embedscope(&dst6, 0))
			return (EINVAL); /* XXX: should be impossible */
	}
	/* Modify original ifra_dstaddr to reflect changes */
	ifra->ifra_dstaddr = dst6;

	/*
	 * The destination address can be specified only for a p2p or a
	 * loopback interface.  If specified, the corresponding prefix length
	 * must be 128.
	 */
	if (ifra->ifra_dstaddr.sin6_family == AF_INET6) {
		if ((ifp->if_flags & (IFF_POINTOPOINT|IFF_LOOPBACK)) == 0) {
			/* XXX: noisy message */
			nd6log((LOG_INFO, "in6_update_ifa: a destination can "
			    "be specified for a p2p or a loopback IF only\n"));
			return (EINVAL);
		}
		if (plen != 128) {
			nd6log((LOG_INFO, "in6_update_ifa: prefixlen should "
			    "be 128 when dstaddr is specified\n"));
			return (EINVAL);
		}
	}
	/* lifetime consistency check */
	lt = &ifra->ifra_lifetime;
	if (lt->ia6t_pltime > lt->ia6t_vltime)
		return (EINVAL);
	if (lt->ia6t_vltime == 0) {
		/*
		 * the following log might be noisy, but this is a typical
		 * configuration mistake or a tool's bug.
		 */
		nd6log((LOG_INFO,
		    "in6_update_ifa: valid lifetime is 0 for %s\n",
		    ip6_sprintf(ip6buf, &ifra->ifra_addr.sin6_addr)));

		if (ia == NULL)
			return (0); /* there's nothing to do */
	}

	/* Check prefix mask */
	if (ia != NULL && ifra->ifra_prefixmask.sin6_len != 0) {
		/*
		 * We prohibit changing the prefix length of an existing
		 * address, because
		 * + such an operation should be rare in IPv6, and
		 * + the operation would confuse prefix management.
		 */
		if (ia->ia_prefixmask.sin6_len != 0 &&
		    in6_mask2len(&ia->ia_prefixmask.sin6_addr, NULL) != plen) {
			nd6log((LOG_INFO, "in6_validate_ifa: the prefix length "
			    "of an existing %s address should not be changed\n",
			    ip6_sprintf(ip6buf, &ia->ia_addr.sin6_addr)));

			return (EINVAL);
		}
	}

	return (0);
}


/*
 * Allocate a new ifaddr and link it into chains.
 */
static struct in6_ifaddr *
in6_alloc_ifa(struct ifnet *ifp, struct in6_aliasreq *ifra, int flags)
{
	struct in6_ifaddr *ia;

	/*
	 * When in6_alloc_ifa() is called in a process of a received
	 * RA, it is called under an interrupt context.  So, we should
	 * call malloc with M_NOWAIT.
	 */
	ia = (struct in6_ifaddr *)ifa_alloc(sizeof(*ia), M_NOWAIT);
	if (ia == NULL)
		return (NULL);
	LIST_INIT(&ia->ia6_memberships);
	/* Initialize the address and masks, and put time stamp */
	ia->ia_ifa.ifa_addr = (struct sockaddr *)&ia->ia_addr;
	ia->ia_addr.sin6_family = AF_INET6;
	ia->ia_addr.sin6_len = sizeof(ia->ia_addr);
	/* XXX: Can we assign ,sin6_addr and skip the rest? */
	ia->ia_addr = ifra->ifra_addr;
	ia->ia6_createtime = time_uptime;
	if ((ifp->if_flags & (IFF_POINTOPOINT | IFF_LOOPBACK)) != 0) {
		/*
		 * Some functions expect that ifa_dstaddr is not
		 * NULL for p2p interfaces.
		 */
		ia->ia_ifa.ifa_dstaddr =
		    (struct sockaddr *)&ia->ia_dstaddr;
	} else {
		ia->ia_ifa.ifa_dstaddr = NULL;
	}

	/* set prefix mask if any */
	ia->ia_ifa.ifa_netmask = (struct sockaddr *)&ia->ia_prefixmask;
	if (ifra->ifra_prefixmask.sin6_len != 0) {
		ia->ia_prefixmask.sin6_family = AF_INET6;
		ia->ia_prefixmask.sin6_len = ifra->ifra_prefixmask.sin6_len;
		ia->ia_prefixmask.sin6_addr = ifra->ifra_prefixmask.sin6_addr;
	}

	ia->ia_ifp = ifp;
	ifa_ref(&ia->ia_ifa);			/* if_addrhead */
	IF_ADDR_WLOCK(ifp);
	CK_STAILQ_INSERT_TAIL(&ifp->if_addrhead, &ia->ia_ifa, ifa_link);
	IF_ADDR_WUNLOCK(ifp);

	ifa_ref(&ia->ia_ifa);			/* in6_ifaddrhead */
	IN6_IFADDR_WLOCK();
	CK_STAILQ_INSERT_TAIL(&V_in6_ifaddrhead, ia, ia_link);
	CK_LIST_INSERT_HEAD(IN6ADDR_HASH(&ia->ia_addr.sin6_addr), ia, ia6_hash);
	IN6_IFADDR_WUNLOCK();

	return (ia);
}

/*
 * Update/configure interface address parameters:
 *
 * 1) Update lifetime
 * 2) Update interface metric ad flags
 * 3) Notify other subsystems
 */
static int
in6_update_ifa_internal(struct ifnet *ifp, struct in6_aliasreq *ifra,
    struct in6_ifaddr *ia, int hostIsNew, int flags)
{
	int error;

	/* update timestamp */
	ia->ia6_updatetime = time_uptime;

	/*
	 * Set lifetimes.  We do not refer to ia6t_expire and ia6t_preferred
	 * to see if the address is deprecated or invalidated, but initialize
	 * these members for applications.
	 */
	ia->ia6_lifetime = ifra->ifra_lifetime;
	if (ia->ia6_lifetime.ia6t_vltime != ND6_INFINITE_LIFETIME) {
		ia->ia6_lifetime.ia6t_expire =
		    time_uptime + ia->ia6_lifetime.ia6t_vltime;
	} else
		ia->ia6_lifetime.ia6t_expire = 0;
	if (ia->ia6_lifetime.ia6t_pltime != ND6_INFINITE_LIFETIME) {
		ia->ia6_lifetime.ia6t_preferred =
		    time_uptime + ia->ia6_lifetime.ia6t_pltime;
	} else
		ia->ia6_lifetime.ia6t_preferred = 0;

	/*
	 * backward compatibility - if IN6_IFF_DEPRECATED is set from the
	 * userland, make it deprecated.
	 */
	if ((ifra->ifra_flags & IN6_IFF_DEPRECATED) != 0) {
		ia->ia6_lifetime.ia6t_pltime = 0;
		ia->ia6_lifetime.ia6t_preferred = time_uptime;
	}

	/*
	 * configure address flags.
	 */
	ia->ia6_flags = ifra->ifra_flags;

	/*
	 * Make the address tentative before joining multicast addresses,
	 * so that corresponding MLD responses would not have a tentative
	 * source address.
	 */
	ia->ia6_flags &= ~IN6_IFF_DUPLICATED;	/* safety */

	/*
	 * DAD should be performed for an new address or addresses on
	 * an interface with ND6_IFF_IFDISABLED.
	 */
	if (in6if_do_dad(ifp) &&
	    (hostIsNew || (ND_IFINFO(ifp)->flags & ND6_IFF_IFDISABLED)))
		ia->ia6_flags |= IN6_IFF_TENTATIVE;

	/* notify other subsystems */
	error = in6_notify_ifa(ifp, ia, ifra, hostIsNew);

	return (error);
}

/*
 * Do link-level ifa job:
 * 1) Add lle entry for added address
 * 2) Notifies routing socket users about new address
 * 3) join appropriate multicast group
 * 4) start DAD if enabled
 */
static int
in6_broadcast_ifa(struct ifnet *ifp, struct in6_aliasreq *ifra,
    struct in6_ifaddr *ia, int flags)
{
	struct in6_multi *in6m_sol;
	int error = 0;

	/* Add local address to lltable, if necessary (ex. on p2p link). */
	if ((error = nd6_add_ifa_lle(ia)) != 0) {
		in6_purgeaddr(&ia->ia_ifa);
		ifa_free(&ia->ia_ifa);
		return (error);
	}

	/* Join necessary multicast groups. */
	in6m_sol = NULL;
	if ((ifp->if_flags & IFF_MULTICAST) != 0) {
		error = in6_update_ifa_join_mc(ifp, ifra, ia, flags, &in6m_sol);
		if (error != 0) {
			in6_purgeaddr(&ia->ia_ifa);
			ifa_free(&ia->ia_ifa);
			return (error);
		}
	}

	/* Perform DAD, if the address is TENTATIVE. */
	if ((ia->ia6_flags & IN6_IFF_TENTATIVE)) {
		int delay, mindelay, maxdelay;

		delay = 0;
		if ((flags & IN6_IFAUPDATE_DADDELAY)) {
			/*
			 * We need to impose a delay before sending an NS
			 * for DAD.  Check if we also needed a delay for the
			 * corresponding MLD message.  If we did, the delay
			 * should be larger than the MLD delay (this could be
			 * relaxed a bit, but this simple logic is at least
			 * safe).
			 * XXX: Break data hiding guidelines and look at
			 * state for the solicited multicast group.
			 */
			mindelay = 0;
			if (in6m_sol != NULL &&
			    in6m_sol->in6m_state == MLD_REPORTING_MEMBER) {
				mindelay = in6m_sol->in6m_timer;
			}
			maxdelay = MAX_RTR_SOLICITATION_DELAY * hz;
			if (maxdelay - mindelay == 0)
				delay = 0;
			else {
				delay =
				    (arc4random() % (maxdelay - mindelay)) +
				    mindelay;
			}
		}
		nd6_dad_start((struct ifaddr *)ia, delay);
	}

	in6_newaddrmsg(ia, RTM_ADD);
	ifa_free(&ia->ia_ifa);
	return (error);
}

void
in6_purgeaddr(struct ifaddr *ifa)
{
	struct ifnet *ifp = ifa->ifa_ifp;
	struct in6_ifaddr *ia = (struct in6_ifaddr *) ifa;
	struct in6_multi_mship *imm;
	int plen, error;

	if (ifa->ifa_carp)
		(*carp_detach_p)(ifa, false);

	/*
	 * Remove the loopback route to the interface address.
	 * The check for the current setting of "nd6_useloopback"
	 * is not needed.
	 */
	if (ia->ia_flags & IFA_RTSELF) {
		error = ifa_del_loopback_route((struct ifaddr *)ia,
		    (struct sockaddr *)&ia->ia_addr);
		if (error == 0)
			ia->ia_flags &= ~IFA_RTSELF;
	}

	/* stop DAD processing */
	nd6_dad_stop(ifa);

	/* Leave multicast groups. */
	while ((imm = LIST_FIRST(&ia->ia6_memberships)) != NULL) {
		LIST_REMOVE(imm, i6mm_chain);
		if (imm->i6mm_maddr != NULL)
			in6_leavegroup(imm->i6mm_maddr, NULL);
		free(imm, M_IP6MADDR);
	}
	plen = in6_mask2len(&ia->ia_prefixmask.sin6_addr, NULL); /* XXX */
	if ((ia->ia_flags & IFA_ROUTE) && plen == 128) {
		error = rtinit(&(ia->ia_ifa), RTM_DELETE, ia->ia_flags |
		    (ia->ia_dstaddr.sin6_family == AF_INET6 ? RTF_HOST : 0));
		if (error != 0)
			log(LOG_INFO, "%s: err=%d, destination address delete "
			    "failed\n", __func__, error);
		ia->ia_flags &= ~IFA_ROUTE;
	}

	in6_newaddrmsg(ia, RTM_DELETE);
	in6_unlink_ifa(ia, ifp);
}

static void
in6_unlink_ifa(struct in6_ifaddr *ia, struct ifnet *ifp)
{
	char ip6buf[INET6_ADDRSTRLEN];
	int remove_lle;

	IF_ADDR_WLOCK(ifp);
	CK_STAILQ_REMOVE(&ifp->if_addrhead, &ia->ia_ifa, ifaddr, ifa_link);
	IF_ADDR_WUNLOCK(ifp);
	ifa_free(&ia->ia_ifa);			/* if_addrhead */

	/*
	 * Defer the release of what might be the last reference to the
	 * in6_ifaddr so that it can't be freed before the remainder of the
	 * cleanup.
	 */
	IN6_IFADDR_WLOCK();
	CK_STAILQ_REMOVE(&V_in6_ifaddrhead, ia, in6_ifaddr, ia_link);
	CK_LIST_REMOVE(ia, ia6_hash);
	IN6_IFADDR_WUNLOCK();

	/*
	 * Release the reference to the base prefix.  There should be a
	 * positive reference.
	 */
	remove_lle = 0;
	if (ia->ia6_ndpr == NULL) {
		nd6log((LOG_NOTICE,
		    "in6_unlink_ifa: autoconf'ed address "
		    "%s has no prefix\n", ip6_sprintf(ip6buf, IA6_IN6(ia))));
	} else {
		ia->ia6_ndpr->ndpr_addrcnt--;
		/* Do not delete lles within prefix if refcont != 0 */
		if (ia->ia6_ndpr->ndpr_addrcnt == 0)
			remove_lle = 1;
		ia->ia6_ndpr = NULL;
	}

	nd6_rem_ifa_lle(ia, remove_lle);

	/*
	 * Also, if the address being removed is autoconf'ed, call
	 * pfxlist_onlink_check() since the release might affect the status of
	 * other (detached) addresses.
	 */
	if ((ia->ia6_flags & IN6_IFF_AUTOCONF)) {
		pfxlist_onlink_check();
	}
	ifa_free(&ia->ia_ifa);			/* in6_ifaddrhead */
}

/*
 * Notifies other subsystems about address change/arrival:
 * 1) Notifies device handler on the first IPv6 address assignment
 * 2) Handle routing table changes for P2P links and route
 * 3) Handle routing table changes for address host route
 */
static int
in6_notify_ifa(struct ifnet *ifp, struct in6_ifaddr *ia,
    struct in6_aliasreq *ifra, int hostIsNew)
{
	int	error = 0, plen, ifacount = 0;
	struct ifaddr *ifa;
	struct sockaddr_in6 *pdst;
	char ip6buf[INET6_ADDRSTRLEN];

	/*
	 * Give the interface a chance to initialize
	 * if this is its first address,
	 */
	if (hostIsNew != 0) {
		struct epoch_tracker et;

		NET_EPOCH_ENTER(et);
		CK_STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
			if (ifa->ifa_addr->sa_family != AF_INET6)
				continue;
			ifacount++;
		}
		NET_EPOCH_EXIT(et);
	}

	if (ifacount <= 1 && ifp->if_ioctl) {
		error = (*ifp->if_ioctl)(ifp, SIOCSIFADDR, (caddr_t)ia);
		if (error)
			goto done;
	}

	/*
	 * If a new destination address is specified, scrub the old one and
	 * install the new destination.  Note that the interface must be
	 * p2p or loopback.
	 */
	pdst = &ifra->ifra_dstaddr;
	if (pdst->sin6_family == AF_INET6 &&
	    !IN6_ARE_ADDR_EQUAL(&pdst->sin6_addr, &ia->ia_dstaddr.sin6_addr)) {
		if ((ia->ia_flags & IFA_ROUTE) != 0 &&
		    (rtinit(&(ia->ia_ifa), (int)RTM_DELETE, RTF_HOST) != 0)) {
			nd6log((LOG_ERR, "in6_update_ifa_internal: failed to "
			    "remove a route to the old destination: %s\n",
			    ip6_sprintf(ip6buf, &ia->ia_addr.sin6_addr)));
			/* proceed anyway... */
		} else
			ia->ia_flags &= ~IFA_ROUTE;
		ia->ia_dstaddr = *pdst;
	}

	/*
	 * If a new destination address is specified for a point-to-point
	 * interface, install a route to the destination as an interface
	 * direct route.
	 * XXX: the logic below rejects assigning multiple addresses on a p2p
	 * interface that share the same destination.
	 */
	plen = in6_mask2len(&ia->ia_prefixmask.sin6_addr, NULL); /* XXX */
	if (!(ia->ia_flags & IFA_ROUTE) && plen == 128 &&
	    ia->ia_dstaddr.sin6_family == AF_INET6) {
		int rtflags = RTF_UP | RTF_HOST;
		/*
		 * Handle the case for ::1 .
		 */
		if (ifp->if_flags & IFF_LOOPBACK)
			ia->ia_flags |= IFA_RTSELF;
		error = rtinit(&ia->ia_ifa, RTM_ADD, ia->ia_flags | rtflags);
		if (error)
			goto done;
		ia->ia_flags |= IFA_ROUTE;
	}

	/*
	 * add a loopback route to self if not exists
	 */
	if (!(ia->ia_flags & IFA_RTSELF) && V_nd6_useloopback) {
		error = ifa_add_loopback_route((struct ifaddr *)ia,
		    (struct sockaddr *)&ia->ia_addr);
		if (error == 0)
			ia->ia_flags |= IFA_RTSELF;
	}
done:
	WITNESS_WARN(WARN_GIANTOK | WARN_SLEEPOK, NULL,
	    "Invoking IPv6 network device address event may sleep");

	ifa_ref(&ia->ia_ifa);
	EVENTHANDLER_INVOKE(ifaddr_event_ext, ifp, &ia->ia_ifa,
	    IFADDR_EVENT_ADD);
	ifa_free(&ia->ia_ifa);

	return (error);
}

/*
 * Find an IPv6 interface link-local address specific to an interface.
 * ifaddr is returned referenced.
 */
struct in6_ifaddr *
in6ifa_ifpforlinklocal(struct ifnet *ifp, int ignoreflags)
{
	struct epoch_tracker et;
	struct ifaddr *ifa;

	NET_EPOCH_ENTER(et);
	CK_STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
		if (ifa->ifa_addr->sa_family != AF_INET6)
			continue;
		if (IN6_IS_ADDR_LINKLOCAL(IFA_IN6(ifa))) {
			if ((((struct in6_ifaddr *)ifa)->ia6_flags &
			    ignoreflags) != 0)
				continue;
			ifa_ref(ifa);
			break;
		}
	}
	NET_EPOCH_EXIT(et);

	return ((struct in6_ifaddr *)ifa);
}


/*
 * find the interface address corresponding to a given IPv6 address.
 * ifaddr is returned referenced.
 */
struct in6_ifaddr *
in6ifa_ifwithaddr(const struct in6_addr *addr, uint32_t zoneid)
{
	struct rm_priotracker in6_ifa_tracker;
	struct in6_ifaddr *ia;

	IN6_IFADDR_RLOCK(&in6_ifa_tracker);
	CK_LIST_FOREACH(ia, IN6ADDR_HASH(addr), ia6_hash) {
		if (IN6_ARE_ADDR_EQUAL(IA6_IN6(ia), addr)) {
			if (zoneid != 0 &&
			    zoneid != ia->ia_addr.sin6_scope_id)
				continue;
			ifa_ref(&ia->ia_ifa);
			break;
		}
	}
	IN6_IFADDR_RUNLOCK(&in6_ifa_tracker);
	return (ia);
}

/*
 * find the internet address corresponding to a given interface and address.
 * ifaddr is returned referenced.
 */
struct in6_ifaddr *
in6ifa_ifpwithaddr(struct ifnet *ifp, const struct in6_addr *addr)
{
	struct epoch_tracker et;
	struct ifaddr *ifa;

	NET_EPOCH_ENTER(et);
	CK_STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
		if (ifa->ifa_addr->sa_family != AF_INET6)
			continue;
		if (IN6_ARE_ADDR_EQUAL(addr, IFA_IN6(ifa))) {
			ifa_ref(ifa);
			break;
		}
	}
	NET_EPOCH_EXIT(et);

	return ((struct in6_ifaddr *)ifa);
}

/*
 * Find a link-local scoped address on ifp and return it if any.
 */
struct in6_ifaddr *
in6ifa_llaonifp(struct ifnet *ifp)
{
	struct epoch_tracker et;
	struct sockaddr_in6 *sin6;
	struct ifaddr *ifa;

	if (ND_IFINFO(ifp)->flags & ND6_IFF_IFDISABLED)
		return (NULL);
	NET_EPOCH_ENTER(et);
	CK_STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
		if (ifa->ifa_addr->sa_family != AF_INET6)
			continue;
		sin6 = (struct sockaddr_in6 *)ifa->ifa_addr;
		if (IN6_IS_SCOPE_LINKLOCAL(&sin6->sin6_addr) ||
		    IN6_IS_ADDR_MC_INTFACELOCAL(&sin6->sin6_addr) ||
		    IN6_IS_ADDR_MC_NODELOCAL(&sin6->sin6_addr))
			break;
	}
	NET_EPOCH_EXIT(et);

	return ((struct in6_ifaddr *)ifa);
}

/*
 * Convert IP6 address to printable (loggable) representation. Caller
 * has to make sure that ip6buf is at least INET6_ADDRSTRLEN long.
 */
static char digits[] = "0123456789abcdef";
char *
ip6_sprintf(char *ip6buf, const struct in6_addr *addr)
{
	int i, cnt = 0, maxcnt = 0, idx = 0, index = 0;
	char *cp;
	const u_int16_t *a = (const u_int16_t *)addr;
	const u_int8_t *d;
	int dcolon = 0, zero = 0;

	cp = ip6buf;

	for (i = 0; i < 8; i++) {
		if (*(a + i) == 0) {
			cnt++;
			if (cnt == 1)
				idx = i;
		}
		else if (maxcnt < cnt) {
			maxcnt = cnt;
			index = idx;
			cnt = 0;
		}
	}
	if (maxcnt < cnt) {
		maxcnt = cnt;
		index = idx;
	}

	for (i = 0; i < 8; i++) {
		if (dcolon == 1) {
			if (*a == 0) {
				if (i == 7)
					*cp++ = ':';
				a++;
				continue;
			} else
				dcolon = 2;
		}
		if (*a == 0) {
			if (dcolon == 0 && *(a + 1) == 0 && i == index) {
				if (i == 0)
					*cp++ = ':';
				*cp++ = ':';
				dcolon = 1;
			} else {
				*cp++ = '0';
				*cp++ = ':';
			}
			a++;
			continue;
		}
		d = (const u_char *)a;
		/* Try to eliminate leading zeros in printout like in :0001. */
		zero = 1;
		*cp = digits[*d >> 4];
		if (*cp != '0') {
			zero = 0;
			cp++;
		}
		*cp = digits[*d++ & 0xf];
		if (zero == 0 || (*cp != '0')) {
			zero = 0;
			cp++;
		}
		*cp = digits[*d >> 4];
		if (zero == 0 || (*cp != '0')) {
			zero = 0;
			cp++;
		}
		*cp++ = digits[*d & 0xf];
		*cp++ = ':';
		a++;
	}
	*--cp = '\0';
	return (ip6buf);
}

int
in6_localaddr(struct in6_addr *in6)
{
	struct rm_priotracker in6_ifa_tracker;
	struct in6_ifaddr *ia;

	if (IN6_IS_ADDR_LOOPBACK(in6) || IN6_IS_ADDR_LINKLOCAL(in6))
		return 1;

	IN6_IFADDR_RLOCK(&in6_ifa_tracker);
	CK_STAILQ_FOREACH(ia, &V_in6_ifaddrhead, ia_link) {
		if (IN6_ARE_MASKED_ADDR_EQUAL(in6, &ia->ia_addr.sin6_addr,
		    &ia->ia_prefixmask.sin6_addr)) {
			IN6_IFADDR_RUNLOCK(&in6_ifa_tracker);
			return 1;
		}
	}
	IN6_IFADDR_RUNLOCK(&in6_ifa_tracker);

	return (0);
}

/*
 * Return 1 if an internet address is for the local host and configured
 * on one of its interfaces.
 */
int
in6_localip(struct in6_addr *in6)
{
	struct rm_priotracker in6_ifa_tracker;
	struct in6_ifaddr *ia;

	IN6_IFADDR_RLOCK(&in6_ifa_tracker);
	CK_LIST_FOREACH(ia, IN6ADDR_HASH(in6), ia6_hash) {
		if (IN6_ARE_ADDR_EQUAL(in6, &ia->ia_addr.sin6_addr)) {
			IN6_IFADDR_RUNLOCK(&in6_ifa_tracker);
			return (1);
		}
	}
	IN6_IFADDR_RUNLOCK(&in6_ifa_tracker);
	return (0);
}
 
/*
 * Return 1 if an internet address is configured on an interface.
 */
int
in6_ifhasaddr(struct ifnet *ifp, struct in6_addr *addr)
{
	struct in6_addr in6;
	struct epoch_tracker et;
	struct ifaddr *ifa;
	struct in6_ifaddr *ia6;

	in6 = *addr;
	if (in6_clearscope(&in6))
		return (0);
	in6_setscope(&in6, ifp, NULL);

	NET_EPOCH_ENTER(et);
	CK_STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
		if (ifa->ifa_addr->sa_family != AF_INET6)
			continue;
		ia6 = (struct in6_ifaddr *)ifa;
		if (IN6_ARE_ADDR_EQUAL(&ia6->ia_addr.sin6_addr, &in6)) {
			NET_EPOCH_EXIT(et);
			return (1);
		}
	}
	NET_EPOCH_EXIT(et);

	return (0);
}

int
in6_is_addr_deprecated(struct sockaddr_in6 *sa6)
{
	struct rm_priotracker in6_ifa_tracker;
	struct in6_ifaddr *ia;

	IN6_IFADDR_RLOCK(&in6_ifa_tracker);
	CK_LIST_FOREACH(ia, IN6ADDR_HASH(&sa6->sin6_addr), ia6_hash) {
		if (IN6_ARE_ADDR_EQUAL(IA6_IN6(ia), &sa6->sin6_addr)) {
			if (ia->ia6_flags & IN6_IFF_DEPRECATED) {
				IN6_IFADDR_RUNLOCK(&in6_ifa_tracker);
				return (1); /* true */
			}
			break;
		}
	}
	IN6_IFADDR_RUNLOCK(&in6_ifa_tracker);

	return (0);		/* false */
}

/*
 * return length of part which dst and src are equal
 * hard coding...
 */
int
in6_matchlen(struct in6_addr *src, struct in6_addr *dst)
{
	int match = 0;
	u_char *s = (u_char *)src, *d = (u_char *)dst;
	u_char *lim = s + 16, r;

	while (s < lim)
		if ((r = (*d++ ^ *s++)) != 0) {
			while (r < 128) {
				match++;
				r <<= 1;
			}
			break;
		} else
			match += 8;
	return match;
}

/* XXX: to be scope conscious */
int
in6_are_prefix_equal(struct in6_addr *p1, struct in6_addr *p2, int len)
{
	int bytelen, bitlen;

	/* sanity check */
	if (0 > len || len > 128) {
		log(LOG_ERR, "in6_are_prefix_equal: invalid prefix length(%d)\n",
		    len);
		return (0);
	}

	bytelen = len / 8;
	bitlen = len % 8;

	if (bcmp(&p1->s6_addr, &p2->s6_addr, bytelen))
		return (0);
	if (bitlen != 0 &&
	    p1->s6_addr[bytelen] >> (8 - bitlen) !=
	    p2->s6_addr[bytelen] >> (8 - bitlen))
		return (0);

	return (1);
}

void
in6_prefixlen2mask(struct in6_addr *maskp, int len)
{
	u_char maskarray[8] = {0x80, 0xc0, 0xe0, 0xf0, 0xf8, 0xfc, 0xfe, 0xff};
	int bytelen, bitlen, i;

	/* sanity check */
	if (0 > len || len > 128) {
		log(LOG_ERR, "in6_prefixlen2mask: invalid prefix length(%d)\n",
		    len);
		return;
	}

	bzero(maskp, sizeof(*maskp));
	bytelen = len / 8;
	bitlen = len % 8;
	for (i = 0; i < bytelen; i++)
		maskp->s6_addr[i] = 0xff;
	if (bitlen)
		maskp->s6_addr[bytelen] = maskarray[bitlen - 1];
}

/*
 * return the best address out of the same scope. if no address was
 * found, return the first valid address from designated IF.
 */
struct in6_ifaddr *
in6_ifawithifp(struct ifnet *ifp, struct in6_addr *dst)
{
	struct epoch_tracker et;
	int dst_scope =	in6_addrscope(dst), blen = -1, tlen;
	struct ifaddr *ifa;
	struct in6_ifaddr *besta = NULL;
	struct in6_ifaddr *dep[2];	/* last-resort: deprecated */

	dep[0] = dep[1] = NULL;

	/*
	 * We first look for addresses in the same scope.
	 * If there is one, return it.
	 * If two or more, return one which matches the dst longest.
	 * If none, return one of global addresses assigned other ifs.
	 */
	NET_EPOCH_ENTER(et);
	CK_STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
		if (ifa->ifa_addr->sa_family != AF_INET6)
			continue;
		if (((struct in6_ifaddr *)ifa)->ia6_flags & IN6_IFF_ANYCAST)
			continue; /* XXX: is there any case to allow anycast? */
		if (((struct in6_ifaddr *)ifa)->ia6_flags & IN6_IFF_NOTREADY)
			continue; /* don't use this interface */
		if (((struct in6_ifaddr *)ifa)->ia6_flags & IN6_IFF_DETACHED)
			continue;
		if (((struct in6_ifaddr *)ifa)->ia6_flags & IN6_IFF_DEPRECATED) {
			if (V_ip6_use_deprecated)
				dep[0] = (struct in6_ifaddr *)ifa;
			continue;
		}

		if (dst_scope == in6_addrscope(IFA_IN6(ifa))) {
			/*
			 * call in6_matchlen() as few as possible
			 */
			if (besta) {
				if (blen == -1)
					blen = in6_matchlen(&besta->ia_addr.sin6_addr, dst);
				tlen = in6_matchlen(IFA_IN6(ifa), dst);
				if (tlen > blen) {
					blen = tlen;
					besta = (struct in6_ifaddr *)ifa;
				}
			} else
				besta = (struct in6_ifaddr *)ifa;
		}
	}
	if (besta) {
		ifa_ref(&besta->ia_ifa);
		NET_EPOCH_EXIT(et);
		return (besta);
	}

	CK_STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
		if (ifa->ifa_addr->sa_family != AF_INET6)
			continue;
		if (((struct in6_ifaddr *)ifa)->ia6_flags & IN6_IFF_ANYCAST)
			continue; /* XXX: is there any case to allow anycast? */
		if (((struct in6_ifaddr *)ifa)->ia6_flags & IN6_IFF_NOTREADY)
			continue; /* don't use this interface */
		if (((struct in6_ifaddr *)ifa)->ia6_flags & IN6_IFF_DETACHED)
			continue;
		if (((struct in6_ifaddr *)ifa)->ia6_flags & IN6_IFF_DEPRECATED) {
			if (V_ip6_use_deprecated)
				dep[1] = (struct in6_ifaddr *)ifa;
			continue;
		}

		if (ifa != NULL)
			ifa_ref(ifa);
		NET_EPOCH_EXIT(et);
		return (struct in6_ifaddr *)ifa;
	}

	/* use the last-resort values, that are, deprecated addresses */
	if (dep[0]) {
		ifa_ref((struct ifaddr *)dep[0]);
		NET_EPOCH_EXIT(et);
		return dep[0];
	}
	if (dep[1]) {
		ifa_ref((struct ifaddr *)dep[1]);
		NET_EPOCH_EXIT(et);
		return dep[1];
	}

	NET_EPOCH_EXIT(et);
	return NULL;
}

/*
 * perform DAD when interface becomes IFF_UP.
 */
void
in6_if_up(struct ifnet *ifp)
{
	struct epoch_tracker et;
	struct ifaddr *ifa;
	struct in6_ifaddr *ia;

	NET_EPOCH_ENTER(et);
	CK_STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
		if (ifa->ifa_addr->sa_family != AF_INET6)
			continue;
		ia = (struct in6_ifaddr *)ifa;
		if (ia->ia6_flags & IN6_IFF_TENTATIVE) {
			/*
			 * The TENTATIVE flag was likely set by hand
			 * beforehand, implicitly indicating the need for DAD.
			 * We may be able to skip the random delay in this
			 * case, but we impose delays just in case.
			 */
			nd6_dad_start(ifa,
			    arc4random() % (MAX_RTR_SOLICITATION_DELAY * hz));
		}
	}
	NET_EPOCH_EXIT(et);

	/*
	 * special cases, like 6to4, are handled in in6_ifattach
	 */
	in6_ifattach(ifp, NULL);
}

int
in6if_do_dad(struct ifnet *ifp)
{
	if ((ifp->if_flags & IFF_LOOPBACK) != 0)
		return (0);

	if ((ND_IFINFO(ifp)->flags & ND6_IFF_IFDISABLED) ||
	    (ND_IFINFO(ifp)->flags & ND6_IFF_NO_DAD))
		return (0);

	/*
	 * Our DAD routine requires the interface up and running.
	 * However, some interfaces can be up before the RUNNING
	 * status.  Additionally, users may try to assign addresses
	 * before the interface becomes up (or running).
	 * This function returns EAGAIN in that case.
	 * The caller should mark "tentative" on the address instead of
	 * performing DAD immediately.
	 */
	if (!((ifp->if_flags & IFF_UP) &&
	    (ifp->if_drv_flags & IFF_DRV_RUNNING)))
		return (EAGAIN);

	return (1);
}

/*
 * Calculate max IPv6 MTU through all the interfaces and store it
 * to in6_maxmtu.
 */
void
in6_setmaxmtu(void)
{
	struct epoch_tracker et;
	unsigned long maxmtu = 0;
	struct ifnet *ifp;

	NET_EPOCH_ENTER(et);
	CK_STAILQ_FOREACH(ifp, &V_ifnet, if_link) {
		/* this function can be called during ifnet initialization */
		if (!ifp->if_afdata[AF_INET6])
			continue;
		if ((ifp->if_flags & IFF_LOOPBACK) == 0 &&
		    IN6_LINKMTU(ifp) > maxmtu)
			maxmtu = IN6_LINKMTU(ifp);
	}
	NET_EPOCH_EXIT(et);
	if (maxmtu)	/* update only when maxmtu is positive */
		V_in6_maxmtu = maxmtu;
}

/*
 * Provide the length of interface identifiers to be used for the link attached
 * to the given interface.  The length should be defined in "IPv6 over
 * xxx-link" document.  Note that address architecture might also define
 * the length for a particular set of address prefixes, regardless of the
 * link type.  As clarified in rfc2462bis, those two definitions should be
 * consistent, and those really are as of August 2004.
 */
int
in6_if2idlen(struct ifnet *ifp)
{
	switch (ifp->if_type) {
	case IFT_ETHER:		/* RFC2464 */
	case IFT_PROPVIRTUAL:	/* XXX: no RFC. treat it as ether */
	case IFT_L2VLAN:	/* ditto */
	case IFT_BRIDGE:	/* bridge(4) only does Ethernet-like links */
	case IFT_INFINIBAND:
		return (64);
	case IFT_PPP:		/* RFC2472 */
		return (64);
	case IFT_FRELAY:	/* RFC2590 */
		return (64);
	case IFT_IEEE1394:	/* RFC3146 */
		return (64);
	case IFT_GIF:
		return (64);	/* draft-ietf-v6ops-mech-v2-07 */
	case IFT_LOOP:
		return (64);	/* XXX: is this really correct? */
	default:
		/*
		 * Unknown link type:
		 * It might be controversial to use the today's common constant
		 * of 64 for these cases unconditionally.  For full compliance,
		 * we should return an error in this case.  On the other hand,
		 * if we simply miss the standard for the link type or a new
		 * standard is defined for a new link type, the IFID length
		 * is very likely to be the common constant.  As a compromise,
		 * we always use the constant, but make an explicit notice
		 * indicating the "unknown" case.
		 */
		printf("in6_if2idlen: unknown link type (%d)\n", ifp->if_type);
		return (64);
	}
}

#include <sys/sysctl.h>

struct in6_llentry {
	struct llentry		base;
};

#define	IN6_LLTBL_DEFAULT_HSIZE	32
#define	IN6_LLTBL_HASH(k, h) \
	(((((((k >> 8) ^ k) >> 8) ^ k) >> 8) ^ k) & ((h) - 1))

/*
 * Do actual deallocation of @lle.
 */
static void
in6_lltable_destroy_lle_unlocked(epoch_context_t ctx)
{
	struct llentry *lle;

	lle = __containerof(ctx, struct llentry, lle_epoch_ctx);
	LLE_LOCK_DESTROY(lle);
	LLE_REQ_DESTROY(lle);
	free(lle, M_LLTABLE);
}

/*
 * Called by LLE_FREE_LOCKED when number of references
 * drops to zero.
 */
static void
in6_lltable_destroy_lle(struct llentry *lle)
{

	LLE_WUNLOCK(lle);
	epoch_call(net_epoch_preempt,  &lle->lle_epoch_ctx, in6_lltable_destroy_lle_unlocked);
}

static struct llentry *
in6_lltable_new(const struct in6_addr *addr6, u_int flags)
{
	struct in6_llentry *lle;

	lle = malloc(sizeof(struct in6_llentry), M_LLTABLE, M_NOWAIT | M_ZERO);
	if (lle == NULL)		/* NB: caller generates msg */
		return NULL;

	lle->base.r_l3addr.addr6 = *addr6;
	lle->base.lle_refcnt = 1;
	lle->base.lle_free = in6_lltable_destroy_lle;
	LLE_LOCK_INIT(&lle->base);
	LLE_REQ_INIT(&lle->base);
	callout_init(&lle->base.lle_timer, 1);

	return (&lle->base);
}

static int
in6_lltable_match_prefix(const struct sockaddr *saddr,
    const struct sockaddr *smask, u_int flags, struct llentry *lle)
{
	const struct in6_addr *addr, *mask, *lle_addr;

	addr = &((const struct sockaddr_in6 *)saddr)->sin6_addr;
	mask = &((const struct sockaddr_in6 *)smask)->sin6_addr;
	lle_addr = &lle->r_l3addr.addr6;

	if (IN6_ARE_MASKED_ADDR_EQUAL(lle_addr, addr, mask) == 0)
		return (0);

	if (lle->la_flags & LLE_IFADDR) {

		/*
		 * Delete LLE_IFADDR records IFF address & flag matches.
		 * Note that addr is the interface address within prefix
		 * being matched.
		 */
		if (IN6_ARE_ADDR_EQUAL(addr, lle_addr) &&
		    (flags & LLE_STATIC) != 0)
			return (1);
		return (0);
	}

	/* flags & LLE_STATIC means deleting both dynamic and static entries */
	if ((flags & LLE_STATIC) || !(lle->la_flags & LLE_STATIC))
		return (1);

	return (0);
}

static void
in6_lltable_free_entry(struct lltable *llt, struct llentry *lle)
{
	struct ifnet *ifp;

	LLE_WLOCK_ASSERT(lle);
	KASSERT(llt != NULL, ("lltable is NULL"));

	/* Unlink entry from table */
	if ((lle->la_flags & LLE_LINKED) != 0) {

		ifp = llt->llt_ifp;
		IF_AFDATA_WLOCK_ASSERT(ifp);
		lltable_unlink_entry(llt, lle);
	}

	llentry_free(lle);
}

static int
in6_lltable_rtcheck(struct ifnet *ifp,
		    u_int flags,
		    const struct sockaddr *l3addr)
{
	const struct sockaddr_in6 *sin6;
	struct nhop6_basic nh6;
	struct in6_addr dst;
	uint32_t scopeid;
	int error;
	char ip6buf[INET6_ADDRSTRLEN];
	int fibnum;

	KASSERT(l3addr->sa_family == AF_INET6,
	    ("sin_family %d", l3addr->sa_family));

	sin6 = (const struct sockaddr_in6 *)l3addr;
	in6_splitscope(&sin6->sin6_addr, &dst, &scopeid);
	fibnum = V_rt_add_addr_allfibs ? RT_DEFAULT_FIB : ifp->if_fib;
	error = fib6_lookup_nh_basic(fibnum, &dst, scopeid, 0, 0, &nh6);
	if (error != 0 || (nh6.nh_flags & NHF_GATEWAY) || nh6.nh_ifp != ifp) {
		struct epoch_tracker et;
		struct ifaddr *ifa;
		/*
		 * Create an ND6 cache for an IPv6 neighbor
		 * that is not covered by our own prefix.
		 */
		NET_EPOCH_ENTER(et);
		ifa = ifaof_ifpforaddr(l3addr, ifp);
		if (ifa != NULL) {
			NET_EPOCH_EXIT(et);
			return 0;
		}
		NET_EPOCH_EXIT(et);
		log(LOG_INFO, "IPv6 address: \"%s\" is not on the network\n",
		    ip6_sprintf(ip6buf, &sin6->sin6_addr));
		return EINVAL;
	}
	return 0;
}

/*
 * Called by the datapath to indicate that the entry was used.
 */
static void
in6_lltable_mark_used(struct llentry *lle)
{

	LLE_REQ_LOCK(lle);
	lle->r_skip_req = 0;

	/*
	 * Set the hit time so the callback function
	 * can determine the remaining time before
	 * transiting to the DELAY state.
	 */
	lle->lle_hittime = time_uptime;
	LLE_REQ_UNLOCK(lle);
}

static inline uint32_t
in6_lltable_hash_dst(const struct in6_addr *dst, uint32_t hsize)
{

	return (IN6_LLTBL_HASH(dst->s6_addr32[3], hsize));
}

static uint32_t
in6_lltable_hash(const struct llentry *lle, uint32_t hsize)
{

	return (in6_lltable_hash_dst(&lle->r_l3addr.addr6, hsize));
}

static void
in6_lltable_fill_sa_entry(const struct llentry *lle, struct sockaddr *sa)
{
	struct sockaddr_in6 *sin6;

	sin6 = (struct sockaddr_in6 *)sa;
	bzero(sin6, sizeof(*sin6));
	sin6->sin6_family = AF_INET6;
	sin6->sin6_len = sizeof(*sin6);
	sin6->sin6_addr = lle->r_l3addr.addr6;
}

static inline struct llentry *
in6_lltable_find_dst(struct lltable *llt, const struct in6_addr *dst)
{
	struct llentry *lle;
	struct llentries *lleh;
	u_int hashidx;

	hashidx = in6_lltable_hash_dst(dst, llt->llt_hsize);
	lleh = &llt->lle_head[hashidx];
	CK_LIST_FOREACH(lle, lleh, lle_next) {
		if (lle->la_flags & LLE_DELETED)
			continue;
		if (IN6_ARE_ADDR_EQUAL(&lle->r_l3addr.addr6, dst))
			break;
	}

	return (lle);
}

static void
in6_lltable_delete_entry(struct lltable *llt, struct llentry *lle)
{

	lle->la_flags |= LLE_DELETED;
	EVENTHANDLER_INVOKE(lle_event, lle, LLENTRY_DELETED);
#ifdef DIAGNOSTIC
	log(LOG_INFO, "ifaddr cache = %p is deleted\n", lle);
#endif
	llentry_free(lle);
}

static struct llentry *
in6_lltable_alloc(struct lltable *llt, u_int flags,
	const struct sockaddr *l3addr)
{
	const struct sockaddr_in6 *sin6 = (const struct sockaddr_in6 *)l3addr;
	struct ifnet *ifp = llt->llt_ifp;
	struct llentry *lle;
	char linkhdr[LLE_MAX_LINKHDR];
	size_t linkhdrsize;
	int lladdr_off;

	KASSERT(l3addr->sa_family == AF_INET6,
	    ("sin_family %d", l3addr->sa_family));

	/*
	 * A route that covers the given address must have
	 * been installed 1st because we are doing a resolution,
	 * verify this.
	 */
	if (!(flags & LLE_IFADDR) &&
	    in6_lltable_rtcheck(ifp, flags, l3addr) != 0)
		return (NULL);

	lle = in6_lltable_new(&sin6->sin6_addr, flags);
	if (lle == NULL) {
		log(LOG_INFO, "lla_lookup: new lle malloc failed\n");
		return (NULL);
	}
	lle->la_flags = flags;
	if ((flags & LLE_IFADDR) == LLE_IFADDR) {
		linkhdrsize = LLE_MAX_LINKHDR;
		if (lltable_calc_llheader(ifp, AF_INET6, IF_LLADDR(ifp),
		    linkhdr, &linkhdrsize, &lladdr_off) != 0) {
			epoch_call(net_epoch_preempt,  &lle->lle_epoch_ctx, in6_lltable_destroy_lle_unlocked);
			return (NULL);
		}
		lltable_set_entry_addr(ifp, lle, linkhdr, linkhdrsize,
		    lladdr_off);
		lle->la_flags |= LLE_STATIC;
	}

	if ((lle->la_flags & LLE_STATIC) != 0)
		lle->ln_state = ND6_LLINFO_REACHABLE;

	return (lle);
}

static struct llentry *
in6_lltable_lookup(struct lltable *llt, u_int flags,
	const struct sockaddr *l3addr)
{
	const struct sockaddr_in6 *sin6 = (const struct sockaddr_in6 *)l3addr;
	struct llentry *lle;

	IF_AFDATA_LOCK_ASSERT(llt->llt_ifp);
	KASSERT(l3addr->sa_family == AF_INET6,
	    ("sin_family %d", l3addr->sa_family));
	KASSERT((flags & (LLE_UNLOCKED | LLE_EXCLUSIVE)) !=
	    (LLE_UNLOCKED | LLE_EXCLUSIVE),
	    ("wrong lle request flags: %#x", flags));

	lle = in6_lltable_find_dst(llt, &sin6->sin6_addr);
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
in6_lltable_dump_entry(struct lltable *llt, struct llentry *lle,
    struct sysctl_req *wr)
{
	struct ifnet *ifp = llt->llt_ifp;
	/* XXX stack use */
	struct {
		struct rt_msghdr	rtm;
		struct sockaddr_in6	sin6;
		/*
		 * ndp.c assumes that sdl is word aligned
		 */
#ifdef __LP64__
		uint32_t		pad;
#endif
		struct sockaddr_dl	sdl;
	} ndpc;
	struct sockaddr_dl *sdl;
	int error;

	bzero(&ndpc, sizeof(ndpc));
	/* skip deleted entries */
	if ((lle->la_flags & LLE_DELETED) == LLE_DELETED)
		return (0);
	/* Skip if jailed and not a valid IP of the prison. */
	lltable_fill_sa_entry(lle, (struct sockaddr *)&ndpc.sin6);
	if (prison_if(wr->td->td_ucred, (struct sockaddr *)&ndpc.sin6) != 0)
		return (0);
	/*
	 * produce a msg made of:
	 *  struct rt_msghdr;
	 *  struct sockaddr_in6 (IPv6)
	 *  struct sockaddr_dl;
	 */
	ndpc.rtm.rtm_msglen = sizeof(ndpc);
	ndpc.rtm.rtm_version = RTM_VERSION;
	ndpc.rtm.rtm_type = RTM_GET;
	ndpc.rtm.rtm_flags = RTF_UP;
	ndpc.rtm.rtm_addrs = RTA_DST | RTA_GATEWAY;
	if (V_deembed_scopeid)
		sa6_recoverscope(&ndpc.sin6);

	/* publish */
	if (lle->la_flags & LLE_PUB)
		ndpc.rtm.rtm_flags |= RTF_ANNOUNCE;

	sdl = &ndpc.sdl;
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
	if (lle->la_expire != 0)
		ndpc.rtm.rtm_rmx.rmx_expire = lle->la_expire +
		    lle->lle_remtime / hz + time_second - time_uptime;
	ndpc.rtm.rtm_flags |= (RTF_HOST | RTF_LLDATA);
	if (lle->la_flags & LLE_STATIC)
		ndpc.rtm.rtm_flags |= RTF_STATIC;
	if (lle->la_flags & LLE_IFADDR)
		ndpc.rtm.rtm_flags |= RTF_PINNED;
	if (lle->ln_router != 0)
		ndpc.rtm.rtm_flags |= RTF_GATEWAY;
	ndpc.rtm.rtm_rmx.rmx_pksent = lle->la_asked;
	/* Store state in rmx_weight value */
	ndpc.rtm.rtm_rmx.rmx_state = lle->ln_state;
	ndpc.rtm.rtm_index = ifp->if_index;
	error = SYSCTL_OUT(wr, &ndpc, sizeof(ndpc));

	return (error);
}

static struct lltable *
in6_lltattach(struct ifnet *ifp)
{
	struct lltable *llt;

	llt = lltable_allocate_htbl(IN6_LLTBL_DEFAULT_HSIZE);
	llt->llt_af = AF_INET6;
	llt->llt_ifp = ifp;

	llt->llt_lookup = in6_lltable_lookup;
	llt->llt_alloc_entry = in6_lltable_alloc;
	llt->llt_delete_entry = in6_lltable_delete_entry;
	llt->llt_dump_entry = in6_lltable_dump_entry;
	llt->llt_hash = in6_lltable_hash;
	llt->llt_fill_sa_entry = in6_lltable_fill_sa_entry;
	llt->llt_free_entry = in6_lltable_free_entry;
	llt->llt_match_prefix = in6_lltable_match_prefix;
	llt->llt_mark_used = in6_lltable_mark_used;
 	lltable_link(llt);

	return (llt);
}

void *
in6_domifattach(struct ifnet *ifp)
{
	struct in6_ifextra *ext;

	/* There are not IPv6-capable interfaces. */
	switch (ifp->if_type) {
	case IFT_PFLOG:
	case IFT_PFSYNC:
	case IFT_USB:
		return (NULL);
	}
	ext = (struct in6_ifextra *)malloc(sizeof(*ext), M_IFADDR, M_WAITOK);
	bzero(ext, sizeof(*ext));

	ext->in6_ifstat = malloc(sizeof(counter_u64_t) *
	    sizeof(struct in6_ifstat) / sizeof(uint64_t), M_IFADDR, M_WAITOK);
	COUNTER_ARRAY_ALLOC(ext->in6_ifstat,
	    sizeof(struct in6_ifstat) / sizeof(uint64_t), M_WAITOK);

	ext->icmp6_ifstat = malloc(sizeof(counter_u64_t) *
	    sizeof(struct icmp6_ifstat) / sizeof(uint64_t), M_IFADDR,
	    M_WAITOK);
	COUNTER_ARRAY_ALLOC(ext->icmp6_ifstat,
	    sizeof(struct icmp6_ifstat) / sizeof(uint64_t), M_WAITOK);

	ext->nd_ifinfo = nd6_ifattach(ifp);
	ext->scope6_id = scope6_ifattach(ifp);
	ext->lltable = in6_lltattach(ifp);

	ext->mld_ifinfo = mld_domifattach(ifp);

	return ext;
}

int
in6_domifmtu(struct ifnet *ifp)
{
	if (ifp->if_afdata[AF_INET6] == NULL)
		return ifp->if_mtu;

	return (IN6_LINKMTU(ifp));
}

void
in6_domifdetach(struct ifnet *ifp, void *aux)
{
	struct in6_ifextra *ext = (struct in6_ifextra *)aux;

	mld_domifdetach(ifp);
	scope6_ifdetach(ext->scope6_id);
	nd6_ifdetach(ifp, ext->nd_ifinfo);
	lltable_free(ext->lltable);
	COUNTER_ARRAY_FREE(ext->in6_ifstat,
	    sizeof(struct in6_ifstat) / sizeof(uint64_t));
	free(ext->in6_ifstat, M_IFADDR);
	COUNTER_ARRAY_FREE(ext->icmp6_ifstat,
	    sizeof(struct icmp6_ifstat) / sizeof(uint64_t));
	free(ext->icmp6_ifstat, M_IFADDR);
	free(ext, M_IFADDR);
}

/*
 * Convert sockaddr_in6 to sockaddr_in.  Original sockaddr_in6 must be
 * v4 mapped addr or v4 compat addr
 */
void
in6_sin6_2_sin(struct sockaddr_in *sin, struct sockaddr_in6 *sin6)
{

	bzero(sin, sizeof(*sin));
	sin->sin_len = sizeof(struct sockaddr_in);
	sin->sin_family = AF_INET;
	sin->sin_port = sin6->sin6_port;
	sin->sin_addr.s_addr = sin6->sin6_addr.s6_addr32[3];
}

/* Convert sockaddr_in to sockaddr_in6 in v4 mapped addr format. */
void
in6_sin_2_v4mapsin6(struct sockaddr_in *sin, struct sockaddr_in6 *sin6)
{
	bzero(sin6, sizeof(*sin6));
	sin6->sin6_len = sizeof(struct sockaddr_in6);
	sin6->sin6_family = AF_INET6;
	sin6->sin6_port = sin->sin_port;
	sin6->sin6_addr.s6_addr32[0] = 0;
	sin6->sin6_addr.s6_addr32[1] = 0;
	sin6->sin6_addr.s6_addr32[2] = IPV6_ADDR_INT32_SMP;
	sin6->sin6_addr.s6_addr32[3] = sin->sin_addr.s_addr;
}

/* Convert sockaddr_in6 into sockaddr_in. */
void
in6_sin6_2_sin_in_sock(struct sockaddr *nam)
{
	struct sockaddr_in *sin_p;
	struct sockaddr_in6 sin6;

	/*
	 * Save original sockaddr_in6 addr and convert it
	 * to sockaddr_in.
	 */
	sin6 = *(struct sockaddr_in6 *)nam;
	sin_p = (struct sockaddr_in *)nam;
	in6_sin6_2_sin(sin_p, &sin6);
}

/* Convert sockaddr_in into sockaddr_in6 in v4 mapped addr format. */
void
in6_sin_2_v4mapsin6_in_sock(struct sockaddr **nam)
{
	struct sockaddr_in *sin_p;
	struct sockaddr_in6 *sin6_p;

	sin6_p = malloc(sizeof *sin6_p, M_SONAME, M_WAITOK);
	sin_p = (struct sockaddr_in *)*nam;
	in6_sin_2_v4mapsin6(sin_p, sin6_p);
	free(*nam, M_SONAME);
	*nam = (struct sockaddr *)sin6_p;
}
