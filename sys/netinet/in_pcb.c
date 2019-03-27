/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1982, 1986, 1991, 1993, 1995
 *	The Regents of the University of California.
 * Copyright (c) 2007-2009 Robert N. M. Watson
 * Copyright (c) 2010-2011 Juniper Networks, Inc.
 * All rights reserved.
 *
 * Portions of this software were developed by Robert N. M. Watson under
 * contract to Juniper Networks, Inc.
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
 *	@(#)in_pcb.c	8.4 (Berkeley) 5/24/95
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ddb.h"
#include "opt_ipsec.h"
#include "opt_inet.h"
#include "opt_inet6.h"
#include "opt_ratelimit.h"
#include "opt_pcbgroup.h"
#include "opt_rss.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/callout.h>
#include <sys/eventhandler.h>
#include <sys/domain.h>
#include <sys/protosw.h>
#include <sys/rmlock.h>
#include <sys/smp.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sockio.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/refcount.h>
#include <sys/jail.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>

#ifdef DDB
#include <ddb/ddb.h>
#endif

#include <vm/uma.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_types.h>
#include <net/if_llatbl.h>
#include <net/route.h>
#include <net/rss_config.h>
#include <net/vnet.h>

#if defined(INET) || defined(INET6)
#include <netinet/in.h>
#include <netinet/in_pcb.h>
#include <netinet/ip_var.h>
#include <netinet/tcp_var.h>
#ifdef TCPHPTS
#include <netinet/tcp_hpts.h>
#endif
#include <netinet/udp.h>
#include <netinet/udp_var.h>
#endif
#ifdef INET
#include <netinet/in_var.h>
#endif
#ifdef INET6
#include <netinet/ip6.h>
#include <netinet6/in6_pcb.h>
#include <netinet6/in6_var.h>
#include <netinet6/ip6_var.h>
#endif /* INET6 */

#include <netipsec/ipsec_support.h>

#include <security/mac/mac_framework.h>

#define	INPCBLBGROUP_SIZMIN	8
#define	INPCBLBGROUP_SIZMAX	256

static struct callout	ipport_tick_callout;

/*
 * These configure the range of local port addresses assigned to
 * "unspecified" outgoing connections/packets/whatever.
 */
VNET_DEFINE(int, ipport_lowfirstauto) = IPPORT_RESERVED - 1;	/* 1023 */
VNET_DEFINE(int, ipport_lowlastauto) = IPPORT_RESERVEDSTART;	/* 600 */
VNET_DEFINE(int, ipport_firstauto) = IPPORT_EPHEMERALFIRST;	/* 10000 */
VNET_DEFINE(int, ipport_lastauto) = IPPORT_EPHEMERALLAST;	/* 65535 */
VNET_DEFINE(int, ipport_hifirstauto) = IPPORT_HIFIRSTAUTO;	/* 49152 */
VNET_DEFINE(int, ipport_hilastauto) = IPPORT_HILASTAUTO;	/* 65535 */

/*
 * Reserved ports accessible only to root. There are significant
 * security considerations that must be accounted for when changing these,
 * but the security benefits can be great. Please be careful.
 */
VNET_DEFINE(int, ipport_reservedhigh) = IPPORT_RESERVED - 1;	/* 1023 */
VNET_DEFINE(int, ipport_reservedlow);

/* Variables dealing with random ephemeral port allocation. */
VNET_DEFINE(int, ipport_randomized) = 1;	/* user controlled via sysctl */
VNET_DEFINE(int, ipport_randomcps) = 10;	/* user controlled via sysctl */
VNET_DEFINE(int, ipport_randomtime) = 45;	/* user controlled via sysctl */
VNET_DEFINE(int, ipport_stoprandom);		/* toggled by ipport_tick */
VNET_DEFINE(int, ipport_tcpallocs);
VNET_DEFINE_STATIC(int, ipport_tcplastcount);

#define	V_ipport_tcplastcount		VNET(ipport_tcplastcount)

static void	in_pcbremlists(struct inpcb *inp);
#ifdef INET
static struct inpcb	*in_pcblookup_hash_locked(struct inpcbinfo *pcbinfo,
			    struct in_addr faddr, u_int fport_arg,
			    struct in_addr laddr, u_int lport_arg,
			    int lookupflags, struct ifnet *ifp);

#define RANGECHK(var, min, max) \
	if ((var) < (min)) { (var) = (min); } \
	else if ((var) > (max)) { (var) = (max); }

static int
sysctl_net_ipport_check(SYSCTL_HANDLER_ARGS)
{
	int error;

	error = sysctl_handle_int(oidp, arg1, arg2, req);
	if (error == 0) {
		RANGECHK(V_ipport_lowfirstauto, 1, IPPORT_RESERVED - 1);
		RANGECHK(V_ipport_lowlastauto, 1, IPPORT_RESERVED - 1);
		RANGECHK(V_ipport_firstauto, IPPORT_RESERVED, IPPORT_MAX);
		RANGECHK(V_ipport_lastauto, IPPORT_RESERVED, IPPORT_MAX);
		RANGECHK(V_ipport_hifirstauto, IPPORT_RESERVED, IPPORT_MAX);
		RANGECHK(V_ipport_hilastauto, IPPORT_RESERVED, IPPORT_MAX);
	}
	return (error);
}

#undef RANGECHK

static SYSCTL_NODE(_net_inet_ip, IPPROTO_IP, portrange, CTLFLAG_RW, 0,
    "IP Ports");

SYSCTL_PROC(_net_inet_ip_portrange, OID_AUTO, lowfirst,
	CTLFLAG_VNET | CTLTYPE_INT | CTLFLAG_RW,
	&VNET_NAME(ipport_lowfirstauto), 0, &sysctl_net_ipport_check, "I", "");
SYSCTL_PROC(_net_inet_ip_portrange, OID_AUTO, lowlast,
	CTLFLAG_VNET | CTLTYPE_INT | CTLFLAG_RW,
	&VNET_NAME(ipport_lowlastauto), 0, &sysctl_net_ipport_check, "I", "");
SYSCTL_PROC(_net_inet_ip_portrange, OID_AUTO, first,
	CTLFLAG_VNET | CTLTYPE_INT | CTLFLAG_RW,
	&VNET_NAME(ipport_firstauto), 0, &sysctl_net_ipport_check, "I", "");
SYSCTL_PROC(_net_inet_ip_portrange, OID_AUTO, last,
	CTLFLAG_VNET | CTLTYPE_INT | CTLFLAG_RW,
	&VNET_NAME(ipport_lastauto), 0, &sysctl_net_ipport_check, "I", "");
SYSCTL_PROC(_net_inet_ip_portrange, OID_AUTO, hifirst,
	CTLFLAG_VNET | CTLTYPE_INT | CTLFLAG_RW,
	&VNET_NAME(ipport_hifirstauto), 0, &sysctl_net_ipport_check, "I", "");
SYSCTL_PROC(_net_inet_ip_portrange, OID_AUTO, hilast,
	CTLFLAG_VNET | CTLTYPE_INT | CTLFLAG_RW,
	&VNET_NAME(ipport_hilastauto), 0, &sysctl_net_ipport_check, "I", "");
SYSCTL_INT(_net_inet_ip_portrange, OID_AUTO, reservedhigh,
	CTLFLAG_VNET | CTLFLAG_RW | CTLFLAG_SECURE,
	&VNET_NAME(ipport_reservedhigh), 0, "");
SYSCTL_INT(_net_inet_ip_portrange, OID_AUTO, reservedlow,
	CTLFLAG_RW|CTLFLAG_SECURE, &VNET_NAME(ipport_reservedlow), 0, "");
SYSCTL_INT(_net_inet_ip_portrange, OID_AUTO, randomized,
	CTLFLAG_VNET | CTLFLAG_RW,
	&VNET_NAME(ipport_randomized), 0, "Enable random port allocation");
SYSCTL_INT(_net_inet_ip_portrange, OID_AUTO, randomcps,
	CTLFLAG_VNET | CTLFLAG_RW,
	&VNET_NAME(ipport_randomcps), 0, "Maximum number of random port "
	"allocations before switching to a sequental one");
SYSCTL_INT(_net_inet_ip_portrange, OID_AUTO, randomtime,
	CTLFLAG_VNET | CTLFLAG_RW,
	&VNET_NAME(ipport_randomtime), 0,
	"Minimum time to keep sequental port "
	"allocation before switching to a random one");
#endif /* INET */

/*
 * in_pcb.c: manage the Protocol Control Blocks.
 *
 * NOTE: It is assumed that most of these functions will be called with
 * the pcbinfo lock held, and often, the inpcb lock held, as these utility
 * functions often modify hash chains or addresses in pcbs.
 */

static struct inpcblbgroup *
in_pcblbgroup_alloc(struct inpcblbgrouphead *hdr, u_char vflag,
    uint16_t port, const union in_dependaddr *addr, int size)
{
	struct inpcblbgroup *grp;
	size_t bytes;

	bytes = __offsetof(struct inpcblbgroup, il_inp[size]);
	grp = malloc(bytes, M_PCB, M_ZERO | M_NOWAIT);
	if (!grp)
		return (NULL);
	grp->il_vflag = vflag;
	grp->il_lport = port;
	grp->il_dependladdr = *addr;
	grp->il_inpsiz = size;
	CK_LIST_INSERT_HEAD(hdr, grp, il_list);
	return (grp);
}

static void
in_pcblbgroup_free_deferred(epoch_context_t ctx)
{
	struct inpcblbgroup *grp;

	grp = __containerof(ctx, struct inpcblbgroup, il_epoch_ctx);
	free(grp, M_PCB);
}

static void
in_pcblbgroup_free(struct inpcblbgroup *grp)
{

	CK_LIST_REMOVE(grp, il_list);
	epoch_call(net_epoch_preempt, &grp->il_epoch_ctx,
	    in_pcblbgroup_free_deferred);
}

static struct inpcblbgroup *
in_pcblbgroup_resize(struct inpcblbgrouphead *hdr,
    struct inpcblbgroup *old_grp, int size)
{
	struct inpcblbgroup *grp;
	int i;

	grp = in_pcblbgroup_alloc(hdr, old_grp->il_vflag,
	    old_grp->il_lport, &old_grp->il_dependladdr, size);
	if (grp == NULL)
		return (NULL);

	KASSERT(old_grp->il_inpcnt < grp->il_inpsiz,
	    ("invalid new local group size %d and old local group count %d",
	     grp->il_inpsiz, old_grp->il_inpcnt));

	for (i = 0; i < old_grp->il_inpcnt; ++i)
		grp->il_inp[i] = old_grp->il_inp[i];
	grp->il_inpcnt = old_grp->il_inpcnt;
	in_pcblbgroup_free(old_grp);
	return (grp);
}

/*
 * PCB at index 'i' is removed from the group. Pull up the ones below il_inp[i]
 * and shrink group if possible.
 */
static void
in_pcblbgroup_reorder(struct inpcblbgrouphead *hdr, struct inpcblbgroup **grpp,
    int i)
{
	struct inpcblbgroup *grp, *new_grp;

	grp = *grpp;
	for (; i + 1 < grp->il_inpcnt; ++i)
		grp->il_inp[i] = grp->il_inp[i + 1];
	grp->il_inpcnt--;

	if (grp->il_inpsiz > INPCBLBGROUP_SIZMIN &&
	    grp->il_inpcnt <= grp->il_inpsiz / 4) {
		/* Shrink this group. */
		new_grp = in_pcblbgroup_resize(hdr, grp, grp->il_inpsiz / 2);
		if (new_grp != NULL)
			*grpp = new_grp;
	}
}

/*
 * Add PCB to load balance group for SO_REUSEPORT_LB option.
 */
static int
in_pcbinslbgrouphash(struct inpcb *inp)
{
	const static struct timeval interval = { 60, 0 };
	static struct timeval lastprint;
	struct inpcbinfo *pcbinfo;
	struct inpcblbgrouphead *hdr;
	struct inpcblbgroup *grp;
	uint32_t idx;

	pcbinfo = inp->inp_pcbinfo;

	INP_WLOCK_ASSERT(inp);
	INP_HASH_WLOCK_ASSERT(pcbinfo);

	/*
	 * Don't allow jailed socket to join local group.
	 */
	if (inp->inp_socket != NULL && jailed(inp->inp_socket->so_cred))
		return (0);

#ifdef INET6
	/*
	 * Don't allow IPv4 mapped INET6 wild socket.
	 */
	if ((inp->inp_vflag & INP_IPV4) &&
	    inp->inp_laddr.s_addr == INADDR_ANY &&
	    INP_CHECK_SOCKAF(inp->inp_socket, AF_INET6)) {
		return (0);
	}
#endif

	idx = INP_PCBPORTHASH(inp->inp_lport, pcbinfo->ipi_lbgrouphashmask);
	hdr = &pcbinfo->ipi_lbgrouphashbase[idx];
	CK_LIST_FOREACH(grp, hdr, il_list) {
		if (grp->il_vflag == inp->inp_vflag &&
		    grp->il_lport == inp->inp_lport &&
		    memcmp(&grp->il_dependladdr,
		    &inp->inp_inc.inc_ie.ie_dependladdr,
		    sizeof(grp->il_dependladdr)) == 0)
			break;
	}
	if (grp == NULL) {
		/* Create new load balance group. */
		grp = in_pcblbgroup_alloc(hdr, inp->inp_vflag,
		    inp->inp_lport, &inp->inp_inc.inc_ie.ie_dependladdr,
		    INPCBLBGROUP_SIZMIN);
		if (grp == NULL)
			return (ENOBUFS);
	} else if (grp->il_inpcnt == grp->il_inpsiz) {
		if (grp->il_inpsiz >= INPCBLBGROUP_SIZMAX) {
			if (ratecheck(&lastprint, &interval))
				printf("lb group port %d, limit reached\n",
				    ntohs(grp->il_lport));
			return (0);
		}

		/* Expand this local group. */
		grp = in_pcblbgroup_resize(hdr, grp, grp->il_inpsiz * 2);
		if (grp == NULL)
			return (ENOBUFS);
	}

	KASSERT(grp->il_inpcnt < grp->il_inpsiz,
	    ("invalid local group size %d and count %d", grp->il_inpsiz,
	    grp->il_inpcnt));

	grp->il_inp[grp->il_inpcnt] = inp;
	grp->il_inpcnt++;
	return (0);
}

/*
 * Remove PCB from load balance group.
 */
static void
in_pcbremlbgrouphash(struct inpcb *inp)
{
	struct inpcbinfo *pcbinfo;
	struct inpcblbgrouphead *hdr;
	struct inpcblbgroup *grp;
	int i;

	pcbinfo = inp->inp_pcbinfo;

	INP_WLOCK_ASSERT(inp);
	INP_HASH_WLOCK_ASSERT(pcbinfo);

	hdr = &pcbinfo->ipi_lbgrouphashbase[
	    INP_PCBPORTHASH(inp->inp_lport, pcbinfo->ipi_lbgrouphashmask)];
	CK_LIST_FOREACH(grp, hdr, il_list) {
		for (i = 0; i < grp->il_inpcnt; ++i) {
			if (grp->il_inp[i] != inp)
				continue;

			if (grp->il_inpcnt == 1) {
				/* We are the last, free this local group. */
				in_pcblbgroup_free(grp);
			} else {
				/* Pull up inpcbs, shrink group if possible. */
				in_pcblbgroup_reorder(hdr, &grp, i);
			}
			return;
		}
	}
}

/*
 * Different protocols initialize their inpcbs differently - giving
 * different name to the lock.  But they all are disposed the same.
 */
static void
inpcb_fini(void *mem, int size)
{
	struct inpcb *inp = mem;

	INP_LOCK_DESTROY(inp);
}

/*
 * Initialize an inpcbinfo -- we should be able to reduce the number of
 * arguments in time.
 */
void
in_pcbinfo_init(struct inpcbinfo *pcbinfo, const char *name,
    struct inpcbhead *listhead, int hash_nelements, int porthash_nelements,
    char *inpcbzone_name, uma_init inpcbzone_init, u_int hashfields)
{

	porthash_nelements = imin(porthash_nelements, IPPORT_MAX + 1);

	INP_INFO_LOCK_INIT(pcbinfo, name);
	INP_HASH_LOCK_INIT(pcbinfo, "pcbinfohash");	/* XXXRW: argument? */
	INP_LIST_LOCK_INIT(pcbinfo, "pcbinfolist");
#ifdef VIMAGE
	pcbinfo->ipi_vnet = curvnet;
#endif
	pcbinfo->ipi_listhead = listhead;
	CK_LIST_INIT(pcbinfo->ipi_listhead);
	pcbinfo->ipi_count = 0;
	pcbinfo->ipi_hashbase = hashinit(hash_nelements, M_PCB,
	    &pcbinfo->ipi_hashmask);
	pcbinfo->ipi_porthashbase = hashinit(porthash_nelements, M_PCB,
	    &pcbinfo->ipi_porthashmask);
	pcbinfo->ipi_lbgrouphashbase = hashinit(porthash_nelements, M_PCB,
	    &pcbinfo->ipi_lbgrouphashmask);
#ifdef PCBGROUP
	in_pcbgroup_init(pcbinfo, hashfields, hash_nelements);
#endif
	pcbinfo->ipi_zone = uma_zcreate(inpcbzone_name, sizeof(struct inpcb),
	    NULL, NULL, inpcbzone_init, inpcb_fini, UMA_ALIGN_PTR, 0);
	uma_zone_set_max(pcbinfo->ipi_zone, maxsockets);
	uma_zone_set_warning(pcbinfo->ipi_zone,
	    "kern.ipc.maxsockets limit reached");
}

/*
 * Destroy an inpcbinfo.
 */
void
in_pcbinfo_destroy(struct inpcbinfo *pcbinfo)
{

	KASSERT(pcbinfo->ipi_count == 0,
	    ("%s: ipi_count = %u", __func__, pcbinfo->ipi_count));

	hashdestroy(pcbinfo->ipi_hashbase, M_PCB, pcbinfo->ipi_hashmask);
	hashdestroy(pcbinfo->ipi_porthashbase, M_PCB,
	    pcbinfo->ipi_porthashmask);
	hashdestroy(pcbinfo->ipi_lbgrouphashbase, M_PCB,
	    pcbinfo->ipi_lbgrouphashmask);
#ifdef PCBGROUP
	in_pcbgroup_destroy(pcbinfo);
#endif
	uma_zdestroy(pcbinfo->ipi_zone);
	INP_LIST_LOCK_DESTROY(pcbinfo);
	INP_HASH_LOCK_DESTROY(pcbinfo);
	INP_INFO_LOCK_DESTROY(pcbinfo);
}

/*
 * Allocate a PCB and associate it with the socket.
 * On success return with the PCB locked.
 */
int
in_pcballoc(struct socket *so, struct inpcbinfo *pcbinfo)
{
	struct inpcb *inp;
	int error;

#ifdef INVARIANTS
	if (pcbinfo == &V_tcbinfo) {
		INP_INFO_RLOCK_ASSERT(pcbinfo);
	} else {
		INP_INFO_WLOCK_ASSERT(pcbinfo);
	}
#endif

	error = 0;
	inp = uma_zalloc(pcbinfo->ipi_zone, M_NOWAIT);
	if (inp == NULL)
		return (ENOBUFS);
	bzero(&inp->inp_start_zero, inp_zero_size);
	inp->inp_pcbinfo = pcbinfo;
	inp->inp_socket = so;
	inp->inp_cred = crhold(so->so_cred);
	inp->inp_inc.inc_fibnum = so->so_fibnum;
#ifdef MAC
	error = mac_inpcb_init(inp, M_NOWAIT);
	if (error != 0)
		goto out;
	mac_inpcb_create(so, inp);
#endif
#if defined(IPSEC) || defined(IPSEC_SUPPORT)
	error = ipsec_init_pcbpolicy(inp);
	if (error != 0) {
#ifdef MAC
		mac_inpcb_destroy(inp);
#endif
		goto out;
	}
#endif /*IPSEC*/
#ifdef INET6
	if (INP_SOCKAF(so) == AF_INET6) {
		inp->inp_vflag |= INP_IPV6PROTO;
		if (V_ip6_v6only)
			inp->inp_flags |= IN6P_IPV6_V6ONLY;
	}
#endif
	INP_WLOCK(inp);
	INP_LIST_WLOCK(pcbinfo);
	CK_LIST_INSERT_HEAD(pcbinfo->ipi_listhead, inp, inp_list);
	pcbinfo->ipi_count++;
	so->so_pcb = (caddr_t)inp;
#ifdef INET6
	if (V_ip6_auto_flowlabel)
		inp->inp_flags |= IN6P_AUTOFLOWLABEL;
#endif
	inp->inp_gencnt = ++pcbinfo->ipi_gencnt;
	refcount_init(&inp->inp_refcount, 1);	/* Reference from inpcbinfo */

	/*
	 * Routes in inpcb's can cache L2 as well; they are guaranteed
	 * to be cleaned up.
	 */
	inp->inp_route.ro_flags = RT_LLE_CACHE;
	INP_LIST_WUNLOCK(pcbinfo);
#if defined(IPSEC) || defined(IPSEC_SUPPORT) || defined(MAC)
out:
	if (error != 0) {
		crfree(inp->inp_cred);
		uma_zfree(pcbinfo->ipi_zone, inp);
	}
#endif
	return (error);
}

#ifdef INET
int
in_pcbbind(struct inpcb *inp, struct sockaddr *nam, struct ucred *cred)
{
	int anonport, error;

	INP_WLOCK_ASSERT(inp);
	INP_HASH_WLOCK_ASSERT(inp->inp_pcbinfo);

	if (inp->inp_lport != 0 || inp->inp_laddr.s_addr != INADDR_ANY)
		return (EINVAL);
	anonport = nam == NULL || ((struct sockaddr_in *)nam)->sin_port == 0;
	error = in_pcbbind_setup(inp, nam, &inp->inp_laddr.s_addr,
	    &inp->inp_lport, cred);
	if (error)
		return (error);
	if (in_pcbinshash(inp) != 0) {
		inp->inp_laddr.s_addr = INADDR_ANY;
		inp->inp_lport = 0;
		return (EAGAIN);
	}
	if (anonport)
		inp->inp_flags |= INP_ANONPORT;
	return (0);
}
#endif

/*
 * Select a local port (number) to use.
 */
#if defined(INET) || defined(INET6)
int
in_pcb_lport(struct inpcb *inp, struct in_addr *laddrp, u_short *lportp,
    struct ucred *cred, int lookupflags)
{
	struct inpcbinfo *pcbinfo;
	struct inpcb *tmpinp;
	unsigned short *lastport;
	int count, dorandom, error;
	u_short aux, first, last, lport;
#ifdef INET
	struct in_addr laddr;
#endif

	pcbinfo = inp->inp_pcbinfo;

	/*
	 * Because no actual state changes occur here, a global write lock on
	 * the pcbinfo isn't required.
	 */
	INP_LOCK_ASSERT(inp);
	INP_HASH_LOCK_ASSERT(pcbinfo);

	if (inp->inp_flags & INP_HIGHPORT) {
		first = V_ipport_hifirstauto;	/* sysctl */
		last  = V_ipport_hilastauto;
		lastport = &pcbinfo->ipi_lasthi;
	} else if (inp->inp_flags & INP_LOWPORT) {
		error = priv_check_cred(cred, PRIV_NETINET_RESERVEDPORT);
		if (error)
			return (error);
		first = V_ipport_lowfirstauto;	/* 1023 */
		last  = V_ipport_lowlastauto;	/* 600 */
		lastport = &pcbinfo->ipi_lastlow;
	} else {
		first = V_ipport_firstauto;	/* sysctl */
		last  = V_ipport_lastauto;
		lastport = &pcbinfo->ipi_lastport;
	}
	/*
	 * For UDP(-Lite), use random port allocation as long as the user
	 * allows it.  For TCP (and as of yet unknown) connections,
	 * use random port allocation only if the user allows it AND
	 * ipport_tick() allows it.
	 */
	if (V_ipport_randomized &&
		(!V_ipport_stoprandom || pcbinfo == &V_udbinfo ||
		pcbinfo == &V_ulitecbinfo))
		dorandom = 1;
	else
		dorandom = 0;
	/*
	 * It makes no sense to do random port allocation if
	 * we have the only port available.
	 */
	if (first == last)
		dorandom = 0;
	/* Make sure to not include UDP(-Lite) packets in the count. */
	if (pcbinfo != &V_udbinfo || pcbinfo != &V_ulitecbinfo)
		V_ipport_tcpallocs++;
	/*
	 * Instead of having two loops further down counting up or down
	 * make sure that first is always <= last and go with only one
	 * code path implementing all logic.
	 */
	if (first > last) {
		aux = first;
		first = last;
		last = aux;
	}

#ifdef INET
	/* Make the compiler happy. */
	laddr.s_addr = 0;
	if ((inp->inp_vflag & (INP_IPV4|INP_IPV6)) == INP_IPV4) {
		KASSERT(laddrp != NULL, ("%s: laddrp NULL for v4 inp %p",
		    __func__, inp));
		laddr = *laddrp;
	}
#endif
	tmpinp = NULL;	/* Make compiler happy. */
	lport = *lportp;

	if (dorandom)
		*lastport = first + (arc4random() % (last - first));

	count = last - first;

	do {
		if (count-- < 0)	/* completely used? */
			return (EADDRNOTAVAIL);
		++*lastport;
		if (*lastport < first || *lastport > last)
			*lastport = first;
		lport = htons(*lastport);

#ifdef INET6
		if ((inp->inp_vflag & INP_IPV6) != 0)
			tmpinp = in6_pcblookup_local(pcbinfo,
			    &inp->in6p_laddr, lport, lookupflags, cred);
#endif
#if defined(INET) && defined(INET6)
		else
#endif
#ifdef INET
			tmpinp = in_pcblookup_local(pcbinfo, laddr,
			    lport, lookupflags, cred);
#endif
	} while (tmpinp != NULL);

#ifdef INET
	if ((inp->inp_vflag & (INP_IPV4|INP_IPV6)) == INP_IPV4)
		laddrp->s_addr = laddr.s_addr;
#endif
	*lportp = lport;

	return (0);
}

/*
 * Return cached socket options.
 */
int
inp_so_options(const struct inpcb *inp)
{
	int so_options;

	so_options = 0;

	if ((inp->inp_flags2 & INP_REUSEPORT_LB) != 0)
		so_options |= SO_REUSEPORT_LB;
	if ((inp->inp_flags2 & INP_REUSEPORT) != 0)
		so_options |= SO_REUSEPORT;
	if ((inp->inp_flags2 & INP_REUSEADDR) != 0)
		so_options |= SO_REUSEADDR;
	return (so_options);
}
#endif /* INET || INET6 */

/*
 * Check if a new BINDMULTI socket is allowed to be created.
 *
 * ni points to the new inp.
 * oi points to the exisitng inp.
 *
 * This checks whether the existing inp also has BINDMULTI and
 * whether the credentials match.
 */
int
in_pcbbind_check_bindmulti(const struct inpcb *ni, const struct inpcb *oi)
{
	/* Check permissions match */
	if ((ni->inp_flags2 & INP_BINDMULTI) &&
	    (ni->inp_cred->cr_uid !=
	    oi->inp_cred->cr_uid))
		return (0);

	/* Check the existing inp has BINDMULTI set */
	if ((ni->inp_flags2 & INP_BINDMULTI) &&
	    ((oi->inp_flags2 & INP_BINDMULTI) == 0))
		return (0);

	/*
	 * We're okay - either INP_BINDMULTI isn't set on ni, or
	 * it is and it matches the checks.
	 */
	return (1);
}

#ifdef INET
/*
 * Set up a bind operation on a PCB, performing port allocation
 * as required, but do not actually modify the PCB. Callers can
 * either complete the bind by setting inp_laddr/inp_lport and
 * calling in_pcbinshash(), or they can just use the resulting
 * port and address to authorise the sending of a once-off packet.
 *
 * On error, the values of *laddrp and *lportp are not changed.
 */
int
in_pcbbind_setup(struct inpcb *inp, struct sockaddr *nam, in_addr_t *laddrp,
    u_short *lportp, struct ucred *cred)
{
	struct socket *so = inp->inp_socket;
	struct sockaddr_in *sin;
	struct inpcbinfo *pcbinfo = inp->inp_pcbinfo;
	struct in_addr laddr;
	u_short lport = 0;
	int lookupflags = 0, reuseport = (so->so_options & SO_REUSEPORT);
	int error;

	/*
	 * XXX: Maybe we could let SO_REUSEPORT_LB set SO_REUSEPORT bit here
	 * so that we don't have to add to the (already messy) code below.
	 */
	int reuseport_lb = (so->so_options & SO_REUSEPORT_LB);

	/*
	 * No state changes, so read locks are sufficient here.
	 */
	INP_LOCK_ASSERT(inp);
	INP_HASH_LOCK_ASSERT(pcbinfo);

	if (CK_STAILQ_EMPTY(&V_in_ifaddrhead)) /* XXX broken! */
		return (EADDRNOTAVAIL);
	laddr.s_addr = *laddrp;
	if (nam != NULL && laddr.s_addr != INADDR_ANY)
		return (EINVAL);
	if ((so->so_options & (SO_REUSEADDR|SO_REUSEPORT|SO_REUSEPORT_LB)) == 0)
		lookupflags = INPLOOKUP_WILDCARD;
	if (nam == NULL) {
		if ((error = prison_local_ip4(cred, &laddr)) != 0)
			return (error);
	} else {
		sin = (struct sockaddr_in *)nam;
		if (nam->sa_len != sizeof (*sin))
			return (EINVAL);
#ifdef notdef
		/*
		 * We should check the family, but old programs
		 * incorrectly fail to initialize it.
		 */
		if (sin->sin_family != AF_INET)
			return (EAFNOSUPPORT);
#endif
		error = prison_local_ip4(cred, &sin->sin_addr);
		if (error)
			return (error);
		if (sin->sin_port != *lportp) {
			/* Don't allow the port to change. */
			if (*lportp != 0)
				return (EINVAL);
			lport = sin->sin_port;
		}
		/* NB: lport is left as 0 if the port isn't being changed. */
		if (IN_MULTICAST(ntohl(sin->sin_addr.s_addr))) {
			/*
			 * Treat SO_REUSEADDR as SO_REUSEPORT for multicast;
			 * allow complete duplication of binding if
			 * SO_REUSEPORT is set, or if SO_REUSEADDR is set
			 * and a multicast address is bound on both
			 * new and duplicated sockets.
			 */
			if ((so->so_options & (SO_REUSEADDR|SO_REUSEPORT)) != 0)
				reuseport = SO_REUSEADDR|SO_REUSEPORT;
			/*
			 * XXX: How to deal with SO_REUSEPORT_LB here?
			 * Treat same as SO_REUSEPORT for now.
			 */
			if ((so->so_options &
			    (SO_REUSEADDR|SO_REUSEPORT_LB)) != 0)
				reuseport_lb = SO_REUSEADDR|SO_REUSEPORT_LB;
		} else if (sin->sin_addr.s_addr != INADDR_ANY) {
			sin->sin_port = 0;		/* yech... */
			bzero(&sin->sin_zero, sizeof(sin->sin_zero));
			/*
			 * Is the address a local IP address?
			 * If INP_BINDANY is set, then the socket may be bound
			 * to any endpoint address, local or not.
			 */
			if ((inp->inp_flags & INP_BINDANY) == 0 &&
			    ifa_ifwithaddr_check((struct sockaddr *)sin) == 0)
				return (EADDRNOTAVAIL);
		}
		laddr = sin->sin_addr;
		if (lport) {
			struct inpcb *t;
			struct tcptw *tw;

			/* GROSS */
			if (ntohs(lport) <= V_ipport_reservedhigh &&
			    ntohs(lport) >= V_ipport_reservedlow &&
			    priv_check_cred(cred, PRIV_NETINET_RESERVEDPORT))
				return (EACCES);
			if (!IN_MULTICAST(ntohl(sin->sin_addr.s_addr)) &&
			    priv_check_cred(inp->inp_cred, PRIV_NETINET_REUSEPORT) != 0) {
				t = in_pcblookup_local(pcbinfo, sin->sin_addr,
				    lport, INPLOOKUP_WILDCARD, cred);
	/*
	 * XXX
	 * This entire block sorely needs a rewrite.
	 */
				if (t &&
				    ((inp->inp_flags2 & INP_BINDMULTI) == 0) &&
				    ((t->inp_flags & INP_TIMEWAIT) == 0) &&
				    (so->so_type != SOCK_STREAM ||
				     ntohl(t->inp_faddr.s_addr) == INADDR_ANY) &&
				    (ntohl(sin->sin_addr.s_addr) != INADDR_ANY ||
				     ntohl(t->inp_laddr.s_addr) != INADDR_ANY ||
				     (t->inp_flags2 & INP_REUSEPORT) ||
				     (t->inp_flags2 & INP_REUSEPORT_LB) == 0) &&
				    (inp->inp_cred->cr_uid !=
				     t->inp_cred->cr_uid))
					return (EADDRINUSE);

				/*
				 * If the socket is a BINDMULTI socket, then
				 * the credentials need to match and the
				 * original socket also has to have been bound
				 * with BINDMULTI.
				 */
				if (t && (! in_pcbbind_check_bindmulti(inp, t)))
					return (EADDRINUSE);
			}
			t = in_pcblookup_local(pcbinfo, sin->sin_addr,
			    lport, lookupflags, cred);
			if (t && (t->inp_flags & INP_TIMEWAIT)) {
				/*
				 * XXXRW: If an incpb has had its timewait
				 * state recycled, we treat the address as
				 * being in use (for now).  This is better
				 * than a panic, but not desirable.
				 */
				tw = intotw(t);
				if (tw == NULL ||
				    ((reuseport & tw->tw_so_options) == 0 &&
					(reuseport_lb &
				            tw->tw_so_options) == 0)) {
					return (EADDRINUSE);
				}
			} else if (t &&
				   ((inp->inp_flags2 & INP_BINDMULTI) == 0) &&
				   (reuseport & inp_so_options(t)) == 0 &&
				   (reuseport_lb & inp_so_options(t)) == 0) {
#ifdef INET6
				if (ntohl(sin->sin_addr.s_addr) !=
				    INADDR_ANY ||
				    ntohl(t->inp_laddr.s_addr) !=
				    INADDR_ANY ||
				    (inp->inp_vflag & INP_IPV6PROTO) == 0 ||
				    (t->inp_vflag & INP_IPV6PROTO) == 0)
#endif
						return (EADDRINUSE);
				if (t && (! in_pcbbind_check_bindmulti(inp, t)))
					return (EADDRINUSE);
			}
		}
	}
	if (*lportp != 0)
		lport = *lportp;
	if (lport == 0) {
		error = in_pcb_lport(inp, &laddr, &lport, cred, lookupflags);
		if (error != 0)
			return (error);

	}
	*laddrp = laddr.s_addr;
	*lportp = lport;
	return (0);
}

/*
 * Connect from a socket to a specified address.
 * Both address and port must be specified in argument sin.
 * If don't have a local address for this socket yet,
 * then pick one.
 */
int
in_pcbconnect_mbuf(struct inpcb *inp, struct sockaddr *nam,
    struct ucred *cred, struct mbuf *m)
{
	u_short lport, fport;
	in_addr_t laddr, faddr;
	int anonport, error;

	INP_WLOCK_ASSERT(inp);
	INP_HASH_WLOCK_ASSERT(inp->inp_pcbinfo);

	lport = inp->inp_lport;
	laddr = inp->inp_laddr.s_addr;
	anonport = (lport == 0);
	error = in_pcbconnect_setup(inp, nam, &laddr, &lport, &faddr, &fport,
	    NULL, cred);
	if (error)
		return (error);

	/* Do the initial binding of the local address if required. */
	if (inp->inp_laddr.s_addr == INADDR_ANY && inp->inp_lport == 0) {
		inp->inp_lport = lport;
		inp->inp_laddr.s_addr = laddr;
		if (in_pcbinshash(inp) != 0) {
			inp->inp_laddr.s_addr = INADDR_ANY;
			inp->inp_lport = 0;
			return (EAGAIN);
		}
	}

	/* Commit the remaining changes. */
	inp->inp_lport = lport;
	inp->inp_laddr.s_addr = laddr;
	inp->inp_faddr.s_addr = faddr;
	inp->inp_fport = fport;
	in_pcbrehash_mbuf(inp, m);

	if (anonport)
		inp->inp_flags |= INP_ANONPORT;
	return (0);
}

int
in_pcbconnect(struct inpcb *inp, struct sockaddr *nam, struct ucred *cred)
{

	return (in_pcbconnect_mbuf(inp, nam, cred, NULL));
}

/*
 * Do proper source address selection on an unbound socket in case
 * of connect. Take jails into account as well.
 */
int
in_pcbladdr(struct inpcb *inp, struct in_addr *faddr, struct in_addr *laddr,
    struct ucred *cred)
{
	struct ifaddr *ifa;
	struct sockaddr *sa;
	struct sockaddr_in *sin;
	struct route sro;
	struct epoch_tracker et;
	int error;

	KASSERT(laddr != NULL, ("%s: laddr NULL", __func__));
	/*
	 * Bypass source address selection and use the primary jail IP
	 * if requested.
	 */
	if (cred != NULL && !prison_saddrsel_ip4(cred, laddr))
		return (0);

	error = 0;
	bzero(&sro, sizeof(sro));

	sin = (struct sockaddr_in *)&sro.ro_dst;
	sin->sin_family = AF_INET;
	sin->sin_len = sizeof(struct sockaddr_in);
	sin->sin_addr.s_addr = faddr->s_addr;

	/*
	 * If route is known our src addr is taken from the i/f,
	 * else punt.
	 *
	 * Find out route to destination.
	 */
	if ((inp->inp_socket->so_options & SO_DONTROUTE) == 0)
		in_rtalloc_ign(&sro, 0, inp->inp_inc.inc_fibnum);

	/*
	 * If we found a route, use the address corresponding to
	 * the outgoing interface.
	 * 
	 * Otherwise assume faddr is reachable on a directly connected
	 * network and try to find a corresponding interface to take
	 * the source address from.
	 */
	NET_EPOCH_ENTER(et);
	if (sro.ro_rt == NULL || sro.ro_rt->rt_ifp == NULL) {
		struct in_ifaddr *ia;
		struct ifnet *ifp;

		ia = ifatoia(ifa_ifwithdstaddr((struct sockaddr *)sin,
					inp->inp_socket->so_fibnum));
		if (ia == NULL) {
			ia = ifatoia(ifa_ifwithnet((struct sockaddr *)sin, 0,
						inp->inp_socket->so_fibnum));

		}
		if (ia == NULL) {
			error = ENETUNREACH;
			goto done;
		}

		if (cred == NULL || !prison_flag(cred, PR_IP4)) {
			laddr->s_addr = ia->ia_addr.sin_addr.s_addr;
			goto done;
		}

		ifp = ia->ia_ifp;
		ia = NULL;
		CK_STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {

			sa = ifa->ifa_addr;
			if (sa->sa_family != AF_INET)
				continue;
			sin = (struct sockaddr_in *)sa;
			if (prison_check_ip4(cred, &sin->sin_addr) == 0) {
				ia = (struct in_ifaddr *)ifa;
				break;
			}
		}
		if (ia != NULL) {
			laddr->s_addr = ia->ia_addr.sin_addr.s_addr;
			goto done;
		}

		/* 3. As a last resort return the 'default' jail address. */
		error = prison_get_ip4(cred, laddr);
		goto done;
	}

	/*
	 * If the outgoing interface on the route found is not
	 * a loopback interface, use the address from that interface.
	 * In case of jails do those three steps:
	 * 1. check if the interface address belongs to the jail. If so use it.
	 * 2. check if we have any address on the outgoing interface
	 *    belonging to this jail. If so use it.
	 * 3. as a last resort return the 'default' jail address.
	 */
	if ((sro.ro_rt->rt_ifp->if_flags & IFF_LOOPBACK) == 0) {
		struct in_ifaddr *ia;
		struct ifnet *ifp;

		/* If not jailed, use the default returned. */
		if (cred == NULL || !prison_flag(cred, PR_IP4)) {
			ia = (struct in_ifaddr *)sro.ro_rt->rt_ifa;
			laddr->s_addr = ia->ia_addr.sin_addr.s_addr;
			goto done;
		}

		/* Jailed. */
		/* 1. Check if the iface address belongs to the jail. */
		sin = (struct sockaddr_in *)sro.ro_rt->rt_ifa->ifa_addr;
		if (prison_check_ip4(cred, &sin->sin_addr) == 0) {
			ia = (struct in_ifaddr *)sro.ro_rt->rt_ifa;
			laddr->s_addr = ia->ia_addr.sin_addr.s_addr;
			goto done;
		}

		/*
		 * 2. Check if we have any address on the outgoing interface
		 *    belonging to this jail.
		 */
		ia = NULL;
		ifp = sro.ro_rt->rt_ifp;
		CK_STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
			sa = ifa->ifa_addr;
			if (sa->sa_family != AF_INET)
				continue;
			sin = (struct sockaddr_in *)sa;
			if (prison_check_ip4(cred, &sin->sin_addr) == 0) {
				ia = (struct in_ifaddr *)ifa;
				break;
			}
		}
		if (ia != NULL) {
			laddr->s_addr = ia->ia_addr.sin_addr.s_addr;
			goto done;
		}

		/* 3. As a last resort return the 'default' jail address. */
		error = prison_get_ip4(cred, laddr);
		goto done;
	}

	/*
	 * The outgoing interface is marked with 'loopback net', so a route
	 * to ourselves is here.
	 * Try to find the interface of the destination address and then
	 * take the address from there. That interface is not necessarily
	 * a loopback interface.
	 * In case of jails, check that it is an address of the jail
	 * and if we cannot find, fall back to the 'default' jail address.
	 */
	if ((sro.ro_rt->rt_ifp->if_flags & IFF_LOOPBACK) != 0) {
		struct sockaddr_in sain;
		struct in_ifaddr *ia;

		bzero(&sain, sizeof(struct sockaddr_in));
		sain.sin_family = AF_INET;
		sain.sin_len = sizeof(struct sockaddr_in);
		sain.sin_addr.s_addr = faddr->s_addr;

		ia = ifatoia(ifa_ifwithdstaddr(sintosa(&sain),
					inp->inp_socket->so_fibnum));
		if (ia == NULL)
			ia = ifatoia(ifa_ifwithnet(sintosa(&sain), 0,
						inp->inp_socket->so_fibnum));
		if (ia == NULL)
			ia = ifatoia(ifa_ifwithaddr(sintosa(&sain)));

		if (cred == NULL || !prison_flag(cred, PR_IP4)) {
			if (ia == NULL) {
				error = ENETUNREACH;
				goto done;
			}
			laddr->s_addr = ia->ia_addr.sin_addr.s_addr;
			goto done;
		}

		/* Jailed. */
		if (ia != NULL) {
			struct ifnet *ifp;

			ifp = ia->ia_ifp;
			ia = NULL;
			CK_STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
				sa = ifa->ifa_addr;
				if (sa->sa_family != AF_INET)
					continue;
				sin = (struct sockaddr_in *)sa;
				if (prison_check_ip4(cred,
				    &sin->sin_addr) == 0) {
					ia = (struct in_ifaddr *)ifa;
					break;
				}
			}
			if (ia != NULL) {
				laddr->s_addr = ia->ia_addr.sin_addr.s_addr;
				goto done;
			}
		}

		/* 3. As a last resort return the 'default' jail address. */
		error = prison_get_ip4(cred, laddr);
		goto done;
	}

done:
	NET_EPOCH_EXIT(et);
	if (sro.ro_rt != NULL)
		RTFREE(sro.ro_rt);
	return (error);
}

/*
 * Set up for a connect from a socket to the specified address.
 * On entry, *laddrp and *lportp should contain the current local
 * address and port for the PCB; these are updated to the values
 * that should be placed in inp_laddr and inp_lport to complete
 * the connect.
 *
 * On success, *faddrp and *fportp will be set to the remote address
 * and port. These are not updated in the error case.
 *
 * If the operation fails because the connection already exists,
 * *oinpp will be set to the PCB of that connection so that the
 * caller can decide to override it. In all other cases, *oinpp
 * is set to NULL.
 */
int
in_pcbconnect_setup(struct inpcb *inp, struct sockaddr *nam,
    in_addr_t *laddrp, u_short *lportp, in_addr_t *faddrp, u_short *fportp,
    struct inpcb **oinpp, struct ucred *cred)
{
	struct rm_priotracker in_ifa_tracker;
	struct sockaddr_in *sin = (struct sockaddr_in *)nam;
	struct in_ifaddr *ia;
	struct inpcb *oinp;
	struct in_addr laddr, faddr;
	u_short lport, fport;
	int error;

	/*
	 * Because a global state change doesn't actually occur here, a read
	 * lock is sufficient.
	 */
	INP_LOCK_ASSERT(inp);
	INP_HASH_LOCK_ASSERT(inp->inp_pcbinfo);

	if (oinpp != NULL)
		*oinpp = NULL;
	if (nam->sa_len != sizeof (*sin))
		return (EINVAL);
	if (sin->sin_family != AF_INET)
		return (EAFNOSUPPORT);
	if (sin->sin_port == 0)
		return (EADDRNOTAVAIL);
	laddr.s_addr = *laddrp;
	lport = *lportp;
	faddr = sin->sin_addr;
	fport = sin->sin_port;

	if (!CK_STAILQ_EMPTY(&V_in_ifaddrhead)) {
		/*
		 * If the destination address is INADDR_ANY,
		 * use the primary local address.
		 * If the supplied address is INADDR_BROADCAST,
		 * and the primary interface supports broadcast,
		 * choose the broadcast address for that interface.
		 */
		if (faddr.s_addr == INADDR_ANY) {
			IN_IFADDR_RLOCK(&in_ifa_tracker);
			faddr =
			    IA_SIN(CK_STAILQ_FIRST(&V_in_ifaddrhead))->sin_addr;
			IN_IFADDR_RUNLOCK(&in_ifa_tracker);
			if (cred != NULL &&
			    (error = prison_get_ip4(cred, &faddr)) != 0)
				return (error);
		} else if (faddr.s_addr == (u_long)INADDR_BROADCAST) {
			IN_IFADDR_RLOCK(&in_ifa_tracker);
			if (CK_STAILQ_FIRST(&V_in_ifaddrhead)->ia_ifp->if_flags &
			    IFF_BROADCAST)
				faddr = satosin(&CK_STAILQ_FIRST(
				    &V_in_ifaddrhead)->ia_broadaddr)->sin_addr;
			IN_IFADDR_RUNLOCK(&in_ifa_tracker);
		}
	}
	if (laddr.s_addr == INADDR_ANY) {
		error = in_pcbladdr(inp, &faddr, &laddr, cred);
		/*
		 * If the destination address is multicast and an outgoing
		 * interface has been set as a multicast option, prefer the
		 * address of that interface as our source address.
		 */
		if (IN_MULTICAST(ntohl(faddr.s_addr)) &&
		    inp->inp_moptions != NULL) {
			struct ip_moptions *imo;
			struct ifnet *ifp;

			imo = inp->inp_moptions;
			if (imo->imo_multicast_ifp != NULL) {
				ifp = imo->imo_multicast_ifp;
				IN_IFADDR_RLOCK(&in_ifa_tracker);
				CK_STAILQ_FOREACH(ia, &V_in_ifaddrhead, ia_link) {
					if ((ia->ia_ifp == ifp) &&
					    (cred == NULL ||
					    prison_check_ip4(cred,
					    &ia->ia_addr.sin_addr) == 0))
						break;
				}
				if (ia == NULL)
					error = EADDRNOTAVAIL;
				else {
					laddr = ia->ia_addr.sin_addr;
					error = 0;
				}
				IN_IFADDR_RUNLOCK(&in_ifa_tracker);
			}
		}
		if (error)
			return (error);
	}
	oinp = in_pcblookup_hash_locked(inp->inp_pcbinfo, faddr, fport,
	    laddr, lport, 0, NULL);
	if (oinp != NULL) {
		if (oinpp != NULL)
			*oinpp = oinp;
		return (EADDRINUSE);
	}
	if (lport == 0) {
		error = in_pcbbind_setup(inp, NULL, &laddr.s_addr, &lport,
		    cred);
		if (error)
			return (error);
	}
	*laddrp = laddr.s_addr;
	*lportp = lport;
	*faddrp = faddr.s_addr;
	*fportp = fport;
	return (0);
}

void
in_pcbdisconnect(struct inpcb *inp)
{

	INP_WLOCK_ASSERT(inp);
	INP_HASH_WLOCK_ASSERT(inp->inp_pcbinfo);

	inp->inp_faddr.s_addr = INADDR_ANY;
	inp->inp_fport = 0;
	in_pcbrehash(inp);
}
#endif /* INET */

/*
 * in_pcbdetach() is responsibe for disassociating a socket from an inpcb.
 * For most protocols, this will be invoked immediately prior to calling
 * in_pcbfree().  However, with TCP the inpcb may significantly outlive the
 * socket, in which case in_pcbfree() is deferred.
 */
void
in_pcbdetach(struct inpcb *inp)
{

	KASSERT(inp->inp_socket != NULL, ("%s: inp_socket == NULL", __func__));

#ifdef RATELIMIT
	if (inp->inp_snd_tag != NULL)
		in_pcbdetach_txrtlmt(inp);
#endif
	inp->inp_socket->so_pcb = NULL;
	inp->inp_socket = NULL;
}

/*
 * in_pcbref() bumps the reference count on an inpcb in order to maintain
 * stability of an inpcb pointer despite the inpcb lock being released.  This
 * is used in TCP when the inpcbinfo lock needs to be acquired or upgraded,
 * but where the inpcb lock may already held, or when acquiring a reference
 * via a pcbgroup.
 *
 * in_pcbref() should be used only to provide brief memory stability, and
 * must always be followed by a call to INP_WLOCK() and in_pcbrele() to
 * garbage collect the inpcb if it has been in_pcbfree()'d from another
 * context.  Until in_pcbrele() has returned that the inpcb is still valid,
 * lock and rele are the *only* safe operations that may be performed on the
 * inpcb.
 *
 * While the inpcb will not be freed, releasing the inpcb lock means that the
 * connection's state may change, so the caller should be careful to
 * revalidate any cached state on reacquiring the lock.  Drop the reference
 * using in_pcbrele().
 */
void
in_pcbref(struct inpcb *inp)
{

	KASSERT(inp->inp_refcount > 0, ("%s: refcount 0", __func__));

	refcount_acquire(&inp->inp_refcount);
}

/*
 * Drop a refcount on an inpcb elevated using in_pcbref(); because a call to
 * in_pcbfree() may have been made between in_pcbref() and in_pcbrele(), we
 * return a flag indicating whether or not the inpcb remains valid.  If it is
 * valid, we return with the inpcb lock held.
 *
 * Notice that, unlike in_pcbref(), the inpcb lock must be held to drop a
 * reference on an inpcb.  Historically more work was done here (actually, in
 * in_pcbfree_internal()) but has been moved to in_pcbfree() to avoid the
 * need for the pcbinfo lock in in_pcbrele().  Deferring the free is entirely
 * about memory stability (and continued use of the write lock).
 */
int
in_pcbrele_rlocked(struct inpcb *inp)
{
	struct inpcbinfo *pcbinfo;

	KASSERT(inp->inp_refcount > 0, ("%s: refcount 0", __func__));

	INP_RLOCK_ASSERT(inp);

	if (refcount_release(&inp->inp_refcount) == 0) {
		/*
		 * If the inpcb has been freed, let the caller know, even if
		 * this isn't the last reference.
		 */
		if (inp->inp_flags2 & INP_FREED) {
			INP_RUNLOCK(inp);
			return (1);
		}
		return (0);
	}
	
	KASSERT(inp->inp_socket == NULL, ("%s: inp_socket != NULL", __func__));
#ifdef TCPHPTS
	if (inp->inp_in_hpts || inp->inp_in_input) {
		struct tcp_hpts_entry *hpts;
		/*
		 * We should not be on the hpts at 
		 * this point in any form. we must
		 * get the lock to be sure.
		 */
		hpts = tcp_hpts_lock(inp);
		if (inp->inp_in_hpts)
			panic("Hpts:%p inp:%p at free still on hpts",
			      hpts, inp);
		mtx_unlock(&hpts->p_mtx);
		hpts = tcp_input_lock(inp);
		if (inp->inp_in_input) 
			panic("Hpts:%p inp:%p at free still on input hpts",
			      hpts, inp);
		mtx_unlock(&hpts->p_mtx);
	}
#endif
	INP_RUNLOCK(inp);
	pcbinfo = inp->inp_pcbinfo;
	uma_zfree(pcbinfo->ipi_zone, inp);
	return (1);
}

int
in_pcbrele_wlocked(struct inpcb *inp)
{
	struct inpcbinfo *pcbinfo;

	KASSERT(inp->inp_refcount > 0, ("%s: refcount 0", __func__));

	INP_WLOCK_ASSERT(inp);

	if (refcount_release(&inp->inp_refcount) == 0) {
		/*
		 * If the inpcb has been freed, let the caller know, even if
		 * this isn't the last reference.
		 */
		if (inp->inp_flags2 & INP_FREED) {
			INP_WUNLOCK(inp);
			return (1);
		}
		return (0);
	}

	KASSERT(inp->inp_socket == NULL, ("%s: inp_socket != NULL", __func__));
#ifdef TCPHPTS
	if (inp->inp_in_hpts || inp->inp_in_input) {
		struct tcp_hpts_entry *hpts;
		/*
		 * We should not be on the hpts at 
		 * this point in any form. we must
		 * get the lock to be sure.
		 */
		hpts = tcp_hpts_lock(inp);
		if (inp->inp_in_hpts)
			panic("Hpts:%p inp:%p at free still on hpts",
			      hpts, inp);
		mtx_unlock(&hpts->p_mtx);
		hpts = tcp_input_lock(inp);
		if (inp->inp_in_input) 
			panic("Hpts:%p inp:%p at free still on input hpts",
			      hpts, inp);
		mtx_unlock(&hpts->p_mtx);
	}
#endif
	INP_WUNLOCK(inp);
	pcbinfo = inp->inp_pcbinfo;
	uma_zfree(pcbinfo->ipi_zone, inp);
	return (1);
}

/*
 * Temporary wrapper.
 */
int
in_pcbrele(struct inpcb *inp)
{

	return (in_pcbrele_wlocked(inp));
}

void
in_pcblist_rele_rlocked(epoch_context_t ctx)
{
	struct in_pcblist *il;
	struct inpcb *inp;
	struct inpcbinfo *pcbinfo;
	int i, n;

	il = __containerof(ctx, struct in_pcblist, il_epoch_ctx);
	pcbinfo = il->il_pcbinfo;
	n = il->il_count;
	INP_INFO_WLOCK(pcbinfo);
	for (i = 0; i < n; i++) {
		inp = il->il_inp_list[i];
		INP_RLOCK(inp);
		if (!in_pcbrele_rlocked(inp))
			INP_RUNLOCK(inp);
	}
	INP_INFO_WUNLOCK(pcbinfo);
	free(il, M_TEMP);
}

static void
inpcbport_free(epoch_context_t ctx)
{
	struct inpcbport *phd;

	phd = __containerof(ctx, struct inpcbport, phd_epoch_ctx);
	free(phd, M_PCB);
}

static void
in_pcbfree_deferred(epoch_context_t ctx)
{
	struct inpcb *inp;
	int released __unused;

	inp = __containerof(ctx, struct inpcb, inp_epoch_ctx);

	INP_WLOCK(inp);
	CURVNET_SET(inp->inp_vnet);
#ifdef INET
	struct ip_moptions *imo = inp->inp_moptions;
	inp->inp_moptions = NULL;
#endif
	/* XXXRW: Do as much as possible here. */
#if defined(IPSEC) || defined(IPSEC_SUPPORT)
	if (inp->inp_sp != NULL)
		ipsec_delete_pcbpolicy(inp);
#endif
#ifdef INET6
	struct ip6_moptions *im6o = NULL;
	if (inp->inp_vflag & INP_IPV6PROTO) {
		ip6_freepcbopts(inp->in6p_outputopts);
		im6o = inp->in6p_moptions;
		inp->in6p_moptions = NULL;
	}
#endif
	if (inp->inp_options)
		(void)m_free(inp->inp_options);
	inp->inp_vflag = 0;
	crfree(inp->inp_cred);
#ifdef MAC
	mac_inpcb_destroy(inp);
#endif
	released = in_pcbrele_wlocked(inp);
	MPASS(released);
#ifdef INET6
	ip6_freemoptions(im6o);
#endif
#ifdef INET
	inp_freemoptions(imo);
#endif	
	CURVNET_RESTORE();
}

/*
 * Unconditionally schedule an inpcb to be freed by decrementing its
 * reference count, which should occur only after the inpcb has been detached
 * from its socket.  If another thread holds a temporary reference (acquired
 * using in_pcbref()) then the free is deferred until that reference is
 * released using in_pcbrele(), but the inpcb is still unlocked.  Almost all
 * work, including removal from global lists, is done in this context, where
 * the pcbinfo lock is held.
 */
void
in_pcbfree(struct inpcb *inp)
{
	struct inpcbinfo *pcbinfo = inp->inp_pcbinfo;

	KASSERT(inp->inp_socket == NULL, ("%s: inp_socket != NULL", __func__));
	KASSERT((inp->inp_flags2 & INP_FREED) == 0,
	    ("%s: called twice for pcb %p", __func__, inp));
	if (inp->inp_flags2 & INP_FREED) {
		INP_WUNLOCK(inp);
		return;
	}

#ifdef INVARIANTS
	if (pcbinfo == &V_tcbinfo) {
		INP_INFO_LOCK_ASSERT(pcbinfo);
	} else {
		INP_INFO_WLOCK_ASSERT(pcbinfo);
	}
#endif
	INP_WLOCK_ASSERT(inp);
	INP_LIST_WLOCK(pcbinfo);
	in_pcbremlists(inp);
	INP_LIST_WUNLOCK(pcbinfo);
	RO_INVALIDATE_CACHE(&inp->inp_route);
	/* mark as destruction in progress */
	inp->inp_flags2 |= INP_FREED;
	INP_WUNLOCK(inp);
	epoch_call(net_epoch_preempt, &inp->inp_epoch_ctx, in_pcbfree_deferred);
}

/*
 * in_pcbdrop() removes an inpcb from hashed lists, releasing its address and
 * port reservation, and preventing it from being returned by inpcb lookups.
 *
 * It is used by TCP to mark an inpcb as unused and avoid future packet
 * delivery or event notification when a socket remains open but TCP has
 * closed.  This might occur as a result of a shutdown()-initiated TCP close
 * or a RST on the wire, and allows the port binding to be reused while still
 * maintaining the invariant that so_pcb always points to a valid inpcb until
 * in_pcbdetach().
 *
 * XXXRW: Possibly in_pcbdrop() should also prevent future notifications by
 * in_pcbnotifyall() and in_pcbpurgeif0()?
 */
void
in_pcbdrop(struct inpcb *inp)
{

	INP_WLOCK_ASSERT(inp);
#ifdef INVARIANTS
	if (inp->inp_socket != NULL && inp->inp_ppcb != NULL)
		MPASS(inp->inp_refcount > 1);
#endif

	/*
	 * XXXRW: Possibly we should protect the setting of INP_DROPPED with
	 * the hash lock...?
	 */
	inp->inp_flags |= INP_DROPPED;
	if (inp->inp_flags & INP_INHASHLIST) {
		struct inpcbport *phd = inp->inp_phd;

		INP_HASH_WLOCK(inp->inp_pcbinfo);
		in_pcbremlbgrouphash(inp);
		CK_LIST_REMOVE(inp, inp_hash);
		CK_LIST_REMOVE(inp, inp_portlist);
		if (CK_LIST_FIRST(&phd->phd_pcblist) == NULL) {
			CK_LIST_REMOVE(phd, phd_hash);
			epoch_call(net_epoch_preempt, &phd->phd_epoch_ctx, inpcbport_free);
		}
		INP_HASH_WUNLOCK(inp->inp_pcbinfo);
		inp->inp_flags &= ~INP_INHASHLIST;
#ifdef PCBGROUP
		in_pcbgroup_remove(inp);
#endif
	}
}

#ifdef INET
/*
 * Common routines to return the socket addresses associated with inpcbs.
 */
struct sockaddr *
in_sockaddr(in_port_t port, struct in_addr *addr_p)
{
	struct sockaddr_in *sin;

	sin = malloc(sizeof *sin, M_SONAME,
		M_WAITOK | M_ZERO);
	sin->sin_family = AF_INET;
	sin->sin_len = sizeof(*sin);
	sin->sin_addr = *addr_p;
	sin->sin_port = port;

	return (struct sockaddr *)sin;
}

int
in_getsockaddr(struct socket *so, struct sockaddr **nam)
{
	struct inpcb *inp;
	struct in_addr addr;
	in_port_t port;

	inp = sotoinpcb(so);
	KASSERT(inp != NULL, ("in_getsockaddr: inp == NULL"));

	INP_RLOCK(inp);
	port = inp->inp_lport;
	addr = inp->inp_laddr;
	INP_RUNLOCK(inp);

	*nam = in_sockaddr(port, &addr);
	return 0;
}

int
in_getpeeraddr(struct socket *so, struct sockaddr **nam)
{
	struct inpcb *inp;
	struct in_addr addr;
	in_port_t port;

	inp = sotoinpcb(so);
	KASSERT(inp != NULL, ("in_getpeeraddr: inp == NULL"));

	INP_RLOCK(inp);
	port = inp->inp_fport;
	addr = inp->inp_faddr;
	INP_RUNLOCK(inp);

	*nam = in_sockaddr(port, &addr);
	return 0;
}

void
in_pcbnotifyall(struct inpcbinfo *pcbinfo, struct in_addr faddr, int errno,
    struct inpcb *(*notify)(struct inpcb *, int))
{
	struct inpcb *inp, *inp_temp;

	INP_INFO_WLOCK(pcbinfo);
	CK_LIST_FOREACH_SAFE(inp, pcbinfo->ipi_listhead, inp_list, inp_temp) {
		INP_WLOCK(inp);
#ifdef INET6
		if ((inp->inp_vflag & INP_IPV4) == 0) {
			INP_WUNLOCK(inp);
			continue;
		}
#endif
		if (inp->inp_faddr.s_addr != faddr.s_addr ||
		    inp->inp_socket == NULL) {
			INP_WUNLOCK(inp);
			continue;
		}
		if ((*notify)(inp, errno))
			INP_WUNLOCK(inp);
	}
	INP_INFO_WUNLOCK(pcbinfo);
}

void
in_pcbpurgeif0(struct inpcbinfo *pcbinfo, struct ifnet *ifp)
{
	struct inpcb *inp;
	struct ip_moptions *imo;
	int i, gap;

	INP_INFO_WLOCK(pcbinfo);
	CK_LIST_FOREACH(inp, pcbinfo->ipi_listhead, inp_list) {
		INP_WLOCK(inp);
		imo = inp->inp_moptions;
		if ((inp->inp_vflag & INP_IPV4) &&
		    imo != NULL) {
			/*
			 * Unselect the outgoing interface if it is being
			 * detached.
			 */
			if (imo->imo_multicast_ifp == ifp)
				imo->imo_multicast_ifp = NULL;

			/*
			 * Drop multicast group membership if we joined
			 * through the interface being detached.
			 *
			 * XXX This can all be deferred to an epoch_call
			 */
			for (i = 0, gap = 0; i < imo->imo_num_memberships;
			    i++) {
				if (imo->imo_membership[i]->inm_ifp == ifp) {
					IN_MULTI_LOCK_ASSERT();
					in_leavegroup_locked(imo->imo_membership[i], NULL);
					gap++;
				} else if (gap != 0)
					imo->imo_membership[i - gap] =
					    imo->imo_membership[i];
			}
			imo->imo_num_memberships -= gap;
		}
		INP_WUNLOCK(inp);
	}
	INP_INFO_WUNLOCK(pcbinfo);
}

/*
 * Lookup a PCB based on the local address and port.  Caller must hold the
 * hash lock.  No inpcb locks or references are acquired.
 */
#define INP_LOOKUP_MAPPED_PCB_COST	3
struct inpcb *
in_pcblookup_local(struct inpcbinfo *pcbinfo, struct in_addr laddr,
    u_short lport, int lookupflags, struct ucred *cred)
{
	struct inpcb *inp;
#ifdef INET6
	int matchwild = 3 + INP_LOOKUP_MAPPED_PCB_COST;
#else
	int matchwild = 3;
#endif
	int wildcard;

	KASSERT((lookupflags & ~(INPLOOKUP_WILDCARD)) == 0,
	    ("%s: invalid lookup flags %d", __func__, lookupflags));

	INP_HASH_LOCK_ASSERT(pcbinfo);

	if ((lookupflags & INPLOOKUP_WILDCARD) == 0) {
		struct inpcbhead *head;
		/*
		 * Look for an unconnected (wildcard foreign addr) PCB that
		 * matches the local address and port we're looking for.
		 */
		head = &pcbinfo->ipi_hashbase[INP_PCBHASH(INADDR_ANY, lport,
		    0, pcbinfo->ipi_hashmask)];
		CK_LIST_FOREACH(inp, head, inp_hash) {
#ifdef INET6
			/* XXX inp locking */
			if ((inp->inp_vflag & INP_IPV4) == 0)
				continue;
#endif
			if (inp->inp_faddr.s_addr == INADDR_ANY &&
			    inp->inp_laddr.s_addr == laddr.s_addr &&
			    inp->inp_lport == lport) {
				/*
				 * Found?
				 */
				if (cred == NULL ||
				    prison_equal_ip4(cred->cr_prison,
					inp->inp_cred->cr_prison))
					return (inp);
			}
		}
		/*
		 * Not found.
		 */
		return (NULL);
	} else {
		struct inpcbporthead *porthash;
		struct inpcbport *phd;
		struct inpcb *match = NULL;
		/*
		 * Best fit PCB lookup.
		 *
		 * First see if this local port is in use by looking on the
		 * port hash list.
		 */
		porthash = &pcbinfo->ipi_porthashbase[INP_PCBPORTHASH(lport,
		    pcbinfo->ipi_porthashmask)];
		CK_LIST_FOREACH(phd, porthash, phd_hash) {
			if (phd->phd_port == lport)
				break;
		}
		if (phd != NULL) {
			/*
			 * Port is in use by one or more PCBs. Look for best
			 * fit.
			 */
			CK_LIST_FOREACH(inp, &phd->phd_pcblist, inp_portlist) {
				wildcard = 0;
				if (cred != NULL &&
				    !prison_equal_ip4(inp->inp_cred->cr_prison,
					cred->cr_prison))
					continue;
#ifdef INET6
				/* XXX inp locking */
				if ((inp->inp_vflag & INP_IPV4) == 0)
					continue;
				/*
				 * We never select the PCB that has
				 * INP_IPV6 flag and is bound to :: if
				 * we have another PCB which is bound
				 * to 0.0.0.0.  If a PCB has the
				 * INP_IPV6 flag, then we set its cost
				 * higher than IPv4 only PCBs.
				 *
				 * Note that the case only happens
				 * when a socket is bound to ::, under
				 * the condition that the use of the
				 * mapped address is allowed.
				 */
				if ((inp->inp_vflag & INP_IPV6) != 0)
					wildcard += INP_LOOKUP_MAPPED_PCB_COST;
#endif
				if (inp->inp_faddr.s_addr != INADDR_ANY)
					wildcard++;
				if (inp->inp_laddr.s_addr != INADDR_ANY) {
					if (laddr.s_addr == INADDR_ANY)
						wildcard++;
					else if (inp->inp_laddr.s_addr != laddr.s_addr)
						continue;
				} else {
					if (laddr.s_addr != INADDR_ANY)
						wildcard++;
				}
				if (wildcard < matchwild) {
					match = inp;
					matchwild = wildcard;
					if (matchwild == 0)
						break;
				}
			}
		}
		return (match);
	}
}
#undef INP_LOOKUP_MAPPED_PCB_COST

static struct inpcb *
in_pcblookup_lbgroup(const struct inpcbinfo *pcbinfo,
    const struct in_addr *laddr, uint16_t lport, const struct in_addr *faddr,
    uint16_t fport, int lookupflags)
{
	struct inpcb *local_wild;
	const struct inpcblbgrouphead *hdr;
	struct inpcblbgroup *grp;
	uint32_t idx;

	INP_HASH_LOCK_ASSERT(pcbinfo);

	hdr = &pcbinfo->ipi_lbgrouphashbase[
	    INP_PCBPORTHASH(lport, pcbinfo->ipi_lbgrouphashmask)];

	/*
	 * Order of socket selection:
	 * 1. non-wild.
	 * 2. wild (if lookupflags contains INPLOOKUP_WILDCARD).
	 *
	 * NOTE:
	 * - Load balanced group does not contain jailed sockets
	 * - Load balanced group does not contain IPv4 mapped INET6 wild sockets
	 */
	local_wild = NULL;
	CK_LIST_FOREACH(grp, hdr, il_list) {
#ifdef INET6
		if (!(grp->il_vflag & INP_IPV4))
			continue;
#endif
		if (grp->il_lport != lport)
			continue;

		idx = INP_PCBLBGROUP_PKTHASH(faddr->s_addr, lport, fport) %
		    grp->il_inpcnt;
		if (grp->il_laddr.s_addr == laddr->s_addr)
			return (grp->il_inp[idx]);
		if (grp->il_laddr.s_addr == INADDR_ANY &&
		    (lookupflags & INPLOOKUP_WILDCARD) != 0)
			local_wild = grp->il_inp[idx];
	}
	return (local_wild);
}

#ifdef PCBGROUP
/*
 * Lookup PCB in hash list, using pcbgroup tables.
 */
static struct inpcb *
in_pcblookup_group(struct inpcbinfo *pcbinfo, struct inpcbgroup *pcbgroup,
    struct in_addr faddr, u_int fport_arg, struct in_addr laddr,
    u_int lport_arg, int lookupflags, struct ifnet *ifp)
{
	struct inpcbhead *head;
	struct inpcb *inp, *tmpinp;
	u_short fport = fport_arg, lport = lport_arg;
	bool locked;

	/*
	 * First look for an exact match.
	 */
	tmpinp = NULL;
	INP_GROUP_LOCK(pcbgroup);
	head = &pcbgroup->ipg_hashbase[INP_PCBHASH(faddr.s_addr, lport, fport,
	    pcbgroup->ipg_hashmask)];
	CK_LIST_FOREACH(inp, head, inp_pcbgrouphash) {
#ifdef INET6
		/* XXX inp locking */
		if ((inp->inp_vflag & INP_IPV4) == 0)
			continue;
#endif
		if (inp->inp_faddr.s_addr == faddr.s_addr &&
		    inp->inp_laddr.s_addr == laddr.s_addr &&
		    inp->inp_fport == fport &&
		    inp->inp_lport == lport) {
			/*
			 * XXX We should be able to directly return
			 * the inp here, without any checks.
			 * Well unless both bound with SO_REUSEPORT?
			 */
			if (prison_flag(inp->inp_cred, PR_IP4))
				goto found;
			if (tmpinp == NULL)
				tmpinp = inp;
		}
	}
	if (tmpinp != NULL) {
		inp = tmpinp;
		goto found;
	}

#ifdef	RSS
	/*
	 * For incoming connections, we may wish to do a wildcard
	 * match for an RSS-local socket.
	 */
	if ((lookupflags & INPLOOKUP_WILDCARD) != 0) {
		struct inpcb *local_wild = NULL, *local_exact = NULL;
#ifdef INET6
		struct inpcb *local_wild_mapped = NULL;
#endif
		struct inpcb *jail_wild = NULL;
		struct inpcbhead *head;
		int injail;

		/*
		 * Order of socket selection - we always prefer jails.
		 *      1. jailed, non-wild.
		 *      2. jailed, wild.
		 *      3. non-jailed, non-wild.
		 *      4. non-jailed, wild.
		 */

		head = &pcbgroup->ipg_hashbase[INP_PCBHASH(INADDR_ANY,
		    lport, 0, pcbgroup->ipg_hashmask)];
		CK_LIST_FOREACH(inp, head, inp_pcbgrouphash) {
#ifdef INET6
			/* XXX inp locking */
			if ((inp->inp_vflag & INP_IPV4) == 0)
				continue;
#endif
			if (inp->inp_faddr.s_addr != INADDR_ANY ||
			    inp->inp_lport != lport)
				continue;

			injail = prison_flag(inp->inp_cred, PR_IP4);
			if (injail) {
				if (prison_check_ip4(inp->inp_cred,
				    &laddr) != 0)
					continue;
			} else {
				if (local_exact != NULL)
					continue;
			}

			if (inp->inp_laddr.s_addr == laddr.s_addr) {
				if (injail)
					goto found;
				else
					local_exact = inp;
			} else if (inp->inp_laddr.s_addr == INADDR_ANY) {
#ifdef INET6
				/* XXX inp locking, NULL check */
				if (inp->inp_vflag & INP_IPV6PROTO)
					local_wild_mapped = inp;
				else
#endif
					if (injail)
						jail_wild = inp;
					else
						local_wild = inp;
			}
		} /* LIST_FOREACH */

		inp = jail_wild;
		if (inp == NULL)
			inp = local_exact;
		if (inp == NULL)
			inp = local_wild;
#ifdef INET6
		if (inp == NULL)
			inp = local_wild_mapped;
#endif
		if (inp != NULL)
			goto found;
	}
#endif

	/*
	 * Then look for a wildcard match, if requested.
	 */
	if ((lookupflags & INPLOOKUP_WILDCARD) != 0) {
		struct inpcb *local_wild = NULL, *local_exact = NULL;
#ifdef INET6
		struct inpcb *local_wild_mapped = NULL;
#endif
		struct inpcb *jail_wild = NULL;
		struct inpcbhead *head;
		int injail;

		/*
		 * Order of socket selection - we always prefer jails.
		 *      1. jailed, non-wild.
		 *      2. jailed, wild.
		 *      3. non-jailed, non-wild.
		 *      4. non-jailed, wild.
		 */
		head = &pcbinfo->ipi_wildbase[INP_PCBHASH(INADDR_ANY, lport,
		    0, pcbinfo->ipi_wildmask)];
		CK_LIST_FOREACH(inp, head, inp_pcbgroup_wild) {
#ifdef INET6
			/* XXX inp locking */
			if ((inp->inp_vflag & INP_IPV4) == 0)
				continue;
#endif
			if (inp->inp_faddr.s_addr != INADDR_ANY ||
			    inp->inp_lport != lport)
				continue;

			injail = prison_flag(inp->inp_cred, PR_IP4);
			if (injail) {
				if (prison_check_ip4(inp->inp_cred,
				    &laddr) != 0)
					continue;
			} else {
				if (local_exact != NULL)
					continue;
			}

			if (inp->inp_laddr.s_addr == laddr.s_addr) {
				if (injail)
					goto found;
				else
					local_exact = inp;
			} else if (inp->inp_laddr.s_addr == INADDR_ANY) {
#ifdef INET6
				/* XXX inp locking, NULL check */
				if (inp->inp_vflag & INP_IPV6PROTO)
					local_wild_mapped = inp;
				else
#endif
					if (injail)
						jail_wild = inp;
					else
						local_wild = inp;
			}
		} /* LIST_FOREACH */
		inp = jail_wild;
		if (inp == NULL)
			inp = local_exact;
		if (inp == NULL)
			inp = local_wild;
#ifdef INET6
		if (inp == NULL)
			inp = local_wild_mapped;
#endif
		if (inp != NULL)
			goto found;
	} /* if (lookupflags & INPLOOKUP_WILDCARD) */
	INP_GROUP_UNLOCK(pcbgroup);
	return (NULL);

found:
	if (lookupflags & INPLOOKUP_WLOCKPCB)
		locked = INP_TRY_WLOCK(inp);
	else if (lookupflags & INPLOOKUP_RLOCKPCB)
		locked = INP_TRY_RLOCK(inp);
	else
		panic("%s: locking bug", __func__);
	if (__predict_false(locked && (inp->inp_flags2 & INP_FREED))) {
		if (lookupflags & INPLOOKUP_WLOCKPCB)
			INP_WUNLOCK(inp);
		else
			INP_RUNLOCK(inp);
		return (NULL);
	} else if (!locked)
		in_pcbref(inp);
	INP_GROUP_UNLOCK(pcbgroup);
	if (!locked) {
		if (lookupflags & INPLOOKUP_WLOCKPCB) {
			INP_WLOCK(inp);
			if (in_pcbrele_wlocked(inp))
				return (NULL);
		} else {
			INP_RLOCK(inp);
			if (in_pcbrele_rlocked(inp))
				return (NULL);
		}
	}
#ifdef INVARIANTS
	if (lookupflags & INPLOOKUP_WLOCKPCB)
		INP_WLOCK_ASSERT(inp);
	else
		INP_RLOCK_ASSERT(inp);
#endif
	return (inp);
}
#endif /* PCBGROUP */

/*
 * Lookup PCB in hash list, using pcbinfo tables.  This variation assumes
 * that the caller has locked the hash list, and will not perform any further
 * locking or reference operations on either the hash list or the connection.
 */
static struct inpcb *
in_pcblookup_hash_locked(struct inpcbinfo *pcbinfo, struct in_addr faddr,
    u_int fport_arg, struct in_addr laddr, u_int lport_arg, int lookupflags,
    struct ifnet *ifp)
{
	struct inpcbhead *head;
	struct inpcb *inp, *tmpinp;
	u_short fport = fport_arg, lport = lport_arg;

#ifdef INVARIANTS
	KASSERT((lookupflags & ~(INPLOOKUP_WILDCARD)) == 0,
	    ("%s: invalid lookup flags %d", __func__, lookupflags));
	if (!mtx_owned(&pcbinfo->ipi_hash_lock))
		MPASS(in_epoch_verbose(net_epoch_preempt, 1));
#endif
	/*
	 * First look for an exact match.
	 */
	tmpinp = NULL;
	head = &pcbinfo->ipi_hashbase[INP_PCBHASH(faddr.s_addr, lport, fport,
	    pcbinfo->ipi_hashmask)];
	CK_LIST_FOREACH(inp, head, inp_hash) {
#ifdef INET6
		/* XXX inp locking */
		if ((inp->inp_vflag & INP_IPV4) == 0)
			continue;
#endif
		if (inp->inp_faddr.s_addr == faddr.s_addr &&
		    inp->inp_laddr.s_addr == laddr.s_addr &&
		    inp->inp_fport == fport &&
		    inp->inp_lport == lport) {
			/*
			 * XXX We should be able to directly return
			 * the inp here, without any checks.
			 * Well unless both bound with SO_REUSEPORT?
			 */
			if (prison_flag(inp->inp_cred, PR_IP4))
				return (inp);
			if (tmpinp == NULL)
				tmpinp = inp;
		}
	}
	if (tmpinp != NULL)
		return (tmpinp);

	/*
	 * Then look in lb group (for wildcard match).
	 */
	if ((lookupflags & INPLOOKUP_WILDCARD) != 0) {
		inp = in_pcblookup_lbgroup(pcbinfo, &laddr, lport, &faddr,
		    fport, lookupflags);
		if (inp != NULL)
			return (inp);
	}

	/*
	 * Then look for a wildcard match, if requested.
	 */
	if ((lookupflags & INPLOOKUP_WILDCARD) != 0) {
		struct inpcb *local_wild = NULL, *local_exact = NULL;
#ifdef INET6
		struct inpcb *local_wild_mapped = NULL;
#endif
		struct inpcb *jail_wild = NULL;
		int injail;

		/*
		 * Order of socket selection - we always prefer jails.
		 *      1. jailed, non-wild.
		 *      2. jailed, wild.
		 *      3. non-jailed, non-wild.
		 *      4. non-jailed, wild.
		 */

		head = &pcbinfo->ipi_hashbase[INP_PCBHASH(INADDR_ANY, lport,
		    0, pcbinfo->ipi_hashmask)];
		CK_LIST_FOREACH(inp, head, inp_hash) {
#ifdef INET6
			/* XXX inp locking */
			if ((inp->inp_vflag & INP_IPV4) == 0)
				continue;
#endif
			if (inp->inp_faddr.s_addr != INADDR_ANY ||
			    inp->inp_lport != lport)
				continue;

			injail = prison_flag(inp->inp_cred, PR_IP4);
			if (injail) {
				if (prison_check_ip4(inp->inp_cred,
				    &laddr) != 0)
					continue;
			} else {
				if (local_exact != NULL)
					continue;
			}

			if (inp->inp_laddr.s_addr == laddr.s_addr) {
				if (injail)
					return (inp);
				else
					local_exact = inp;
			} else if (inp->inp_laddr.s_addr == INADDR_ANY) {
#ifdef INET6
				/* XXX inp locking, NULL check */
				if (inp->inp_vflag & INP_IPV6PROTO)
					local_wild_mapped = inp;
				else
#endif
					if (injail)
						jail_wild = inp;
					else
						local_wild = inp;
			}
		} /* LIST_FOREACH */
		if (jail_wild != NULL)
			return (jail_wild);
		if (local_exact != NULL)
			return (local_exact);
		if (local_wild != NULL)
			return (local_wild);
#ifdef INET6
		if (local_wild_mapped != NULL)
			return (local_wild_mapped);
#endif
	} /* if ((lookupflags & INPLOOKUP_WILDCARD) != 0) */

	return (NULL);
}

/*
 * Lookup PCB in hash list, using pcbinfo tables.  This variation locks the
 * hash list lock, and will return the inpcb locked (i.e., requires
 * INPLOOKUP_LOCKPCB).
 */
static struct inpcb *
in_pcblookup_hash(struct inpcbinfo *pcbinfo, struct in_addr faddr,
    u_int fport, struct in_addr laddr, u_int lport, int lookupflags,
    struct ifnet *ifp)
{
	struct inpcb *inp;

	INP_HASH_RLOCK(pcbinfo);
	inp = in_pcblookup_hash_locked(pcbinfo, faddr, fport, laddr, lport,
	    (lookupflags & ~(INPLOOKUP_RLOCKPCB | INPLOOKUP_WLOCKPCB)), ifp);
	if (inp != NULL) {
		if (lookupflags & INPLOOKUP_WLOCKPCB) {
			INP_WLOCK(inp);
			if (__predict_false(inp->inp_flags2 & INP_FREED)) {
				INP_WUNLOCK(inp);
				inp = NULL;
			}
		} else if (lookupflags & INPLOOKUP_RLOCKPCB) {
			INP_RLOCK(inp);
			if (__predict_false(inp->inp_flags2 & INP_FREED)) {
				INP_RUNLOCK(inp);
				inp = NULL;
			}
		} else
			panic("%s: locking bug", __func__);
#ifdef INVARIANTS
		if (inp != NULL) {
			if (lookupflags & INPLOOKUP_WLOCKPCB)
				INP_WLOCK_ASSERT(inp);
			else
				INP_RLOCK_ASSERT(inp);
		}
#endif
	}
	INP_HASH_RUNLOCK(pcbinfo);
	return (inp);
}

/*
 * Public inpcb lookup routines, accepting a 4-tuple, and optionally, an mbuf
 * from which a pre-calculated hash value may be extracted.
 *
 * Possibly more of this logic should be in in_pcbgroup.c.
 */
struct inpcb *
in_pcblookup(struct inpcbinfo *pcbinfo, struct in_addr faddr, u_int fport,
    struct in_addr laddr, u_int lport, int lookupflags, struct ifnet *ifp)
{
#if defined(PCBGROUP) && !defined(RSS)
	struct inpcbgroup *pcbgroup;
#endif

	KASSERT((lookupflags & ~INPLOOKUP_MASK) == 0,
	    ("%s: invalid lookup flags %d", __func__, lookupflags));
	KASSERT((lookupflags & (INPLOOKUP_RLOCKPCB | INPLOOKUP_WLOCKPCB)) != 0,
	    ("%s: LOCKPCB not set", __func__));

	/*
	 * When not using RSS, use connection groups in preference to the
	 * reservation table when looking up 4-tuples.  When using RSS, just
	 * use the reservation table, due to the cost of the Toeplitz hash
	 * in software.
	 *
	 * XXXRW: This policy belongs in the pcbgroup code, as in principle
	 * we could be doing RSS with a non-Toeplitz hash that is affordable
	 * in software.
	 */
#if defined(PCBGROUP) && !defined(RSS)
	if (in_pcbgroup_enabled(pcbinfo)) {
		pcbgroup = in_pcbgroup_bytuple(pcbinfo, laddr, lport, faddr,
		    fport);
		return (in_pcblookup_group(pcbinfo, pcbgroup, faddr, fport,
		    laddr, lport, lookupflags, ifp));
	}
#endif
	return (in_pcblookup_hash(pcbinfo, faddr, fport, laddr, lport,
	    lookupflags, ifp));
}

struct inpcb *
in_pcblookup_mbuf(struct inpcbinfo *pcbinfo, struct in_addr faddr,
    u_int fport, struct in_addr laddr, u_int lport, int lookupflags,
    struct ifnet *ifp, struct mbuf *m)
{
#ifdef PCBGROUP
	struct inpcbgroup *pcbgroup;
#endif

	KASSERT((lookupflags & ~INPLOOKUP_MASK) == 0,
	    ("%s: invalid lookup flags %d", __func__, lookupflags));
	KASSERT((lookupflags & (INPLOOKUP_RLOCKPCB | INPLOOKUP_WLOCKPCB)) != 0,
	    ("%s: LOCKPCB not set", __func__));

#ifdef PCBGROUP
	/*
	 * If we can use a hardware-generated hash to look up the connection
	 * group, use that connection group to find the inpcb.  Otherwise
	 * fall back on a software hash -- or the reservation table if we're
	 * using RSS.
	 *
	 * XXXRW: As above, that policy belongs in the pcbgroup code.
	 */
	if (in_pcbgroup_enabled(pcbinfo) &&
	    !(M_HASHTYPE_TEST(m, M_HASHTYPE_NONE))) {
		pcbgroup = in_pcbgroup_byhash(pcbinfo, M_HASHTYPE_GET(m),
		    m->m_pkthdr.flowid);
		if (pcbgroup != NULL)
			return (in_pcblookup_group(pcbinfo, pcbgroup, faddr,
			    fport, laddr, lport, lookupflags, ifp));
#ifndef RSS
		pcbgroup = in_pcbgroup_bytuple(pcbinfo, laddr, lport, faddr,
		    fport);
		return (in_pcblookup_group(pcbinfo, pcbgroup, faddr, fport,
		    laddr, lport, lookupflags, ifp));
#endif
	}
#endif
	return (in_pcblookup_hash(pcbinfo, faddr, fport, laddr, lport,
	    lookupflags, ifp));
}
#endif /* INET */

/*
 * Insert PCB onto various hash lists.
 */
static int
in_pcbinshash_internal(struct inpcb *inp, int do_pcbgroup_update)
{
	struct inpcbhead *pcbhash;
	struct inpcbporthead *pcbporthash;
	struct inpcbinfo *pcbinfo = inp->inp_pcbinfo;
	struct inpcbport *phd;
	u_int32_t hashkey_faddr;
	int so_options;

	INP_WLOCK_ASSERT(inp);
	INP_HASH_WLOCK_ASSERT(pcbinfo);

	KASSERT((inp->inp_flags & INP_INHASHLIST) == 0,
	    ("in_pcbinshash: INP_INHASHLIST"));

#ifdef INET6
	if (inp->inp_vflag & INP_IPV6)
		hashkey_faddr = INP6_PCBHASHKEY(&inp->in6p_faddr);
	else
#endif
	hashkey_faddr = inp->inp_faddr.s_addr;

	pcbhash = &pcbinfo->ipi_hashbase[INP_PCBHASH(hashkey_faddr,
		 inp->inp_lport, inp->inp_fport, pcbinfo->ipi_hashmask)];

	pcbporthash = &pcbinfo->ipi_porthashbase[
	    INP_PCBPORTHASH(inp->inp_lport, pcbinfo->ipi_porthashmask)];

	/*
	 * Add entry to load balance group.
	 * Only do this if SO_REUSEPORT_LB is set.
	 */
	so_options = inp_so_options(inp);
	if (so_options & SO_REUSEPORT_LB) {
		int ret = in_pcbinslbgrouphash(inp);
		if (ret) {
			/* pcb lb group malloc fail (ret=ENOBUFS). */
			return (ret);
		}
	}

	/*
	 * Go through port list and look for a head for this lport.
	 */
	CK_LIST_FOREACH(phd, pcbporthash, phd_hash) {
		if (phd->phd_port == inp->inp_lport)
			break;
	}
	/*
	 * If none exists, malloc one and tack it on.
	 */
	if (phd == NULL) {
		phd = malloc(sizeof(struct inpcbport), M_PCB, M_NOWAIT);
		if (phd == NULL) {
			return (ENOBUFS); /* XXX */
		}
		bzero(&phd->phd_epoch_ctx, sizeof(struct epoch_context));
		phd->phd_port = inp->inp_lport;
		CK_LIST_INIT(&phd->phd_pcblist);
		CK_LIST_INSERT_HEAD(pcbporthash, phd, phd_hash);
	}
	inp->inp_phd = phd;
	CK_LIST_INSERT_HEAD(&phd->phd_pcblist, inp, inp_portlist);
	CK_LIST_INSERT_HEAD(pcbhash, inp, inp_hash);
	inp->inp_flags |= INP_INHASHLIST;
#ifdef PCBGROUP
	if (do_pcbgroup_update)
		in_pcbgroup_update(inp);
#endif
	return (0);
}

/*
 * For now, there are two public interfaces to insert an inpcb into the hash
 * lists -- one that does update pcbgroups, and one that doesn't.  The latter
 * is used only in the TCP syncache, where in_pcbinshash is called before the
 * full 4-tuple is set for the inpcb, and we don't want to install in the
 * pcbgroup until later.
 *
 * XXXRW: This seems like a misfeature.  in_pcbinshash should always update
 * connection groups, and partially initialised inpcbs should not be exposed
 * to either reservation hash tables or pcbgroups.
 */
int
in_pcbinshash(struct inpcb *inp)
{

	return (in_pcbinshash_internal(inp, 1));
}

int
in_pcbinshash_nopcbgroup(struct inpcb *inp)
{

	return (in_pcbinshash_internal(inp, 0));
}

/*
 * Move PCB to the proper hash bucket when { faddr, fport } have  been
 * changed. NOTE: This does not handle the case of the lport changing (the
 * hashed port list would have to be updated as well), so the lport must
 * not change after in_pcbinshash() has been called.
 */
void
in_pcbrehash_mbuf(struct inpcb *inp, struct mbuf *m)
{
	struct inpcbinfo *pcbinfo = inp->inp_pcbinfo;
	struct inpcbhead *head;
	u_int32_t hashkey_faddr;

	INP_WLOCK_ASSERT(inp);
	INP_HASH_WLOCK_ASSERT(pcbinfo);

	KASSERT(inp->inp_flags & INP_INHASHLIST,
	    ("in_pcbrehash: !INP_INHASHLIST"));

#ifdef INET6
	if (inp->inp_vflag & INP_IPV6)
		hashkey_faddr = INP6_PCBHASHKEY(&inp->in6p_faddr);
	else
#endif
	hashkey_faddr = inp->inp_faddr.s_addr;

	head = &pcbinfo->ipi_hashbase[INP_PCBHASH(hashkey_faddr,
		inp->inp_lport, inp->inp_fport, pcbinfo->ipi_hashmask)];

	CK_LIST_REMOVE(inp, inp_hash);
	CK_LIST_INSERT_HEAD(head, inp, inp_hash);

#ifdef PCBGROUP
	if (m != NULL)
		in_pcbgroup_update_mbuf(inp, m);
	else
		in_pcbgroup_update(inp);
#endif
}

void
in_pcbrehash(struct inpcb *inp)
{

	in_pcbrehash_mbuf(inp, NULL);
}

/*
 * Remove PCB from various lists.
 */
static void
in_pcbremlists(struct inpcb *inp)
{
	struct inpcbinfo *pcbinfo = inp->inp_pcbinfo;

#ifdef INVARIANTS
	if (pcbinfo == &V_tcbinfo) {
		INP_INFO_RLOCK_ASSERT(pcbinfo);
	} else {
		INP_INFO_WLOCK_ASSERT(pcbinfo);
	}
#endif

	INP_WLOCK_ASSERT(inp);
	INP_LIST_WLOCK_ASSERT(pcbinfo);

	inp->inp_gencnt = ++pcbinfo->ipi_gencnt;
	if (inp->inp_flags & INP_INHASHLIST) {
		struct inpcbport *phd = inp->inp_phd;

		INP_HASH_WLOCK(pcbinfo);

		/* XXX: Only do if SO_REUSEPORT_LB set? */
		in_pcbremlbgrouphash(inp);

		CK_LIST_REMOVE(inp, inp_hash);
		CK_LIST_REMOVE(inp, inp_portlist);
		if (CK_LIST_FIRST(&phd->phd_pcblist) == NULL) {
			CK_LIST_REMOVE(phd, phd_hash);
			epoch_call(net_epoch_preempt, &phd->phd_epoch_ctx, inpcbport_free);
		}
		INP_HASH_WUNLOCK(pcbinfo);
		inp->inp_flags &= ~INP_INHASHLIST;
	}
	CK_LIST_REMOVE(inp, inp_list);
	pcbinfo->ipi_count--;
#ifdef PCBGROUP
	in_pcbgroup_remove(inp);
#endif
}

/*
 * Check for alternatives when higher level complains
 * about service problems.  For now, invalidate cached
 * routing information.  If the route was created dynamically
 * (by a redirect), time to try a default gateway again.
 */
void
in_losing(struct inpcb *inp)
{

	RO_INVALIDATE_CACHE(&inp->inp_route);
	return;
}

/*
 * A set label operation has occurred at the socket layer, propagate the
 * label change into the in_pcb for the socket.
 */
void
in_pcbsosetlabel(struct socket *so)
{
#ifdef MAC
	struct inpcb *inp;

	inp = sotoinpcb(so);
	KASSERT(inp != NULL, ("in_pcbsosetlabel: so->so_pcb == NULL"));

	INP_WLOCK(inp);
	SOCK_LOCK(so);
	mac_inpcb_sosetlabel(so, inp);
	SOCK_UNLOCK(so);
	INP_WUNLOCK(inp);
#endif
}

/*
 * ipport_tick runs once per second, determining if random port allocation
 * should be continued.  If more than ipport_randomcps ports have been
 * allocated in the last second, then we return to sequential port
 * allocation. We return to random allocation only once we drop below
 * ipport_randomcps for at least ipport_randomtime seconds.
 */
static void
ipport_tick(void *xtp)
{
	VNET_ITERATOR_DECL(vnet_iter);

	VNET_LIST_RLOCK_NOSLEEP();
	VNET_FOREACH(vnet_iter) {
		CURVNET_SET(vnet_iter);	/* XXX appease INVARIANTS here */
		if (V_ipport_tcpallocs <=
		    V_ipport_tcplastcount + V_ipport_randomcps) {
			if (V_ipport_stoprandom > 0)
				V_ipport_stoprandom--;
		} else
			V_ipport_stoprandom = V_ipport_randomtime;
		V_ipport_tcplastcount = V_ipport_tcpallocs;
		CURVNET_RESTORE();
	}
	VNET_LIST_RUNLOCK_NOSLEEP();
	callout_reset(&ipport_tick_callout, hz, ipport_tick, NULL);
}

static void
ip_fini(void *xtp)
{

	callout_stop(&ipport_tick_callout);
}

/* 
 * The ipport_callout should start running at about the time we attach the
 * inet or inet6 domains.
 */
static void
ipport_tick_init(const void *unused __unused)
{

	/* Start ipport_tick. */
	callout_init(&ipport_tick_callout, 1);
	callout_reset(&ipport_tick_callout, 1, ipport_tick, NULL);
	EVENTHANDLER_REGISTER(shutdown_pre_sync, ip_fini, NULL,
		SHUTDOWN_PRI_DEFAULT);
}
SYSINIT(ipport_tick_init, SI_SUB_PROTO_DOMAIN, SI_ORDER_MIDDLE, 
    ipport_tick_init, NULL);

void
inp_wlock(struct inpcb *inp)
{

	INP_WLOCK(inp);
}

void
inp_wunlock(struct inpcb *inp)
{

	INP_WUNLOCK(inp);
}

void
inp_rlock(struct inpcb *inp)
{

	INP_RLOCK(inp);
}

void
inp_runlock(struct inpcb *inp)
{

	INP_RUNLOCK(inp);
}

#ifdef INVARIANT_SUPPORT
void
inp_lock_assert(struct inpcb *inp)
{

	INP_WLOCK_ASSERT(inp);
}

void
inp_unlock_assert(struct inpcb *inp)
{

	INP_UNLOCK_ASSERT(inp);
}
#endif

void
inp_apply_all(void (*func)(struct inpcb *, void *), void *arg)
{
	struct inpcb *inp;

	INP_INFO_WLOCK(&V_tcbinfo);
	CK_LIST_FOREACH(inp, V_tcbinfo.ipi_listhead, inp_list) {
		INP_WLOCK(inp);
		func(inp, arg);
		INP_WUNLOCK(inp);
	}
	INP_INFO_WUNLOCK(&V_tcbinfo);
}

struct socket *
inp_inpcbtosocket(struct inpcb *inp)
{

	INP_WLOCK_ASSERT(inp);
	return (inp->inp_socket);
}

struct tcpcb *
inp_inpcbtotcpcb(struct inpcb *inp)
{

	INP_WLOCK_ASSERT(inp);
	return ((struct tcpcb *)inp->inp_ppcb);
}

int
inp_ip_tos_get(const struct inpcb *inp)
{

	return (inp->inp_ip_tos);
}

void
inp_ip_tos_set(struct inpcb *inp, int val)
{

	inp->inp_ip_tos = val;
}

void
inp_4tuple_get(struct inpcb *inp, uint32_t *laddr, uint16_t *lp,
    uint32_t *faddr, uint16_t *fp)
{

	INP_LOCK_ASSERT(inp);
	*laddr = inp->inp_laddr.s_addr;
	*faddr = inp->inp_faddr.s_addr;
	*lp = inp->inp_lport;
	*fp = inp->inp_fport;
}

struct inpcb *
so_sotoinpcb(struct socket *so)
{

	return (sotoinpcb(so));
}

struct tcpcb *
so_sototcpcb(struct socket *so)
{

	return (sototcpcb(so));
}

/*
 * Create an external-format (``xinpcb'') structure using the information in
 * the kernel-format in_pcb structure pointed to by inp.  This is done to
 * reduce the spew of irrelevant information over this interface, to isolate
 * user code from changes in the kernel structure, and potentially to provide
 * information-hiding if we decide that some of this information should be
 * hidden from users.
 */
void
in_pcbtoxinpcb(const struct inpcb *inp, struct xinpcb *xi)
{

	bzero(xi, sizeof(*xi));
	xi->xi_len = sizeof(struct xinpcb);
	if (inp->inp_socket)
		sotoxsocket(inp->inp_socket, &xi->xi_socket);
	bcopy(&inp->inp_inc, &xi->inp_inc, sizeof(struct in_conninfo));
	xi->inp_gencnt = inp->inp_gencnt;
	xi->inp_ppcb = (uintptr_t)inp->inp_ppcb;
	xi->inp_flow = inp->inp_flow;
	xi->inp_flowid = inp->inp_flowid;
	xi->inp_flowtype = inp->inp_flowtype;
	xi->inp_flags = inp->inp_flags;
	xi->inp_flags2 = inp->inp_flags2;
	xi->inp_rss_listen_bucket = inp->inp_rss_listen_bucket;
	xi->in6p_cksum = inp->in6p_cksum;
	xi->in6p_hops = inp->in6p_hops;
	xi->inp_ip_tos = inp->inp_ip_tos;
	xi->inp_vflag = inp->inp_vflag;
	xi->inp_ip_ttl = inp->inp_ip_ttl;
	xi->inp_ip_p = inp->inp_ip_p;
	xi->inp_ip_minttl = inp->inp_ip_minttl;
}

#ifdef DDB
static void
db_print_indent(int indent)
{
	int i;

	for (i = 0; i < indent; i++)
		db_printf(" ");
}

static void
db_print_inconninfo(struct in_conninfo *inc, const char *name, int indent)
{
	char faddr_str[48], laddr_str[48];

	db_print_indent(indent);
	db_printf("%s at %p\n", name, inc);

	indent += 2;

#ifdef INET6
	if (inc->inc_flags & INC_ISIPV6) {
		/* IPv6. */
		ip6_sprintf(laddr_str, &inc->inc6_laddr);
		ip6_sprintf(faddr_str, &inc->inc6_faddr);
	} else
#endif
	{
		/* IPv4. */
		inet_ntoa_r(inc->inc_laddr, laddr_str);
		inet_ntoa_r(inc->inc_faddr, faddr_str);
	}
	db_print_indent(indent);
	db_printf("inc_laddr %s   inc_lport %u\n", laddr_str,
	    ntohs(inc->inc_lport));
	db_print_indent(indent);
	db_printf("inc_faddr %s   inc_fport %u\n", faddr_str,
	    ntohs(inc->inc_fport));
}

static void
db_print_inpflags(int inp_flags)
{
	int comma;

	comma = 0;
	if (inp_flags & INP_RECVOPTS) {
		db_printf("%sINP_RECVOPTS", comma ? ", " : "");
		comma = 1;
	}
	if (inp_flags & INP_RECVRETOPTS) {
		db_printf("%sINP_RECVRETOPTS", comma ? ", " : "");
		comma = 1;
	}
	if (inp_flags & INP_RECVDSTADDR) {
		db_printf("%sINP_RECVDSTADDR", comma ? ", " : "");
		comma = 1;
	}
	if (inp_flags & INP_ORIGDSTADDR) {
		db_printf("%sINP_ORIGDSTADDR", comma ? ", " : "");
		comma = 1;
	}
	if (inp_flags & INP_HDRINCL) {
		db_printf("%sINP_HDRINCL", comma ? ", " : "");
		comma = 1;
	}
	if (inp_flags & INP_HIGHPORT) {
		db_printf("%sINP_HIGHPORT", comma ? ", " : "");
		comma = 1;
	}
	if (inp_flags & INP_LOWPORT) {
		db_printf("%sINP_LOWPORT", comma ? ", " : "");
		comma = 1;
	}
	if (inp_flags & INP_ANONPORT) {
		db_printf("%sINP_ANONPORT", comma ? ", " : "");
		comma = 1;
	}
	if (inp_flags & INP_RECVIF) {
		db_printf("%sINP_RECVIF", comma ? ", " : "");
		comma = 1;
	}
	if (inp_flags & INP_MTUDISC) {
		db_printf("%sINP_MTUDISC", comma ? ", " : "");
		comma = 1;
	}
	if (inp_flags & INP_RECVTTL) {
		db_printf("%sINP_RECVTTL", comma ? ", " : "");
		comma = 1;
	}
	if (inp_flags & INP_DONTFRAG) {
		db_printf("%sINP_DONTFRAG", comma ? ", " : "");
		comma = 1;
	}
	if (inp_flags & INP_RECVTOS) {
		db_printf("%sINP_RECVTOS", comma ? ", " : "");
		comma = 1;
	}
	if (inp_flags & IN6P_IPV6_V6ONLY) {
		db_printf("%sIN6P_IPV6_V6ONLY", comma ? ", " : "");
		comma = 1;
	}
	if (inp_flags & IN6P_PKTINFO) {
		db_printf("%sIN6P_PKTINFO", comma ? ", " : "");
		comma = 1;
	}
	if (inp_flags & IN6P_HOPLIMIT) {
		db_printf("%sIN6P_HOPLIMIT", comma ? ", " : "");
		comma = 1;
	}
	if (inp_flags & IN6P_HOPOPTS) {
		db_printf("%sIN6P_HOPOPTS", comma ? ", " : "");
		comma = 1;
	}
	if (inp_flags & IN6P_DSTOPTS) {
		db_printf("%sIN6P_DSTOPTS", comma ? ", " : "");
		comma = 1;
	}
	if (inp_flags & IN6P_RTHDR) {
		db_printf("%sIN6P_RTHDR", comma ? ", " : "");
		comma = 1;
	}
	if (inp_flags & IN6P_RTHDRDSTOPTS) {
		db_printf("%sIN6P_RTHDRDSTOPTS", comma ? ", " : "");
		comma = 1;
	}
	if (inp_flags & IN6P_TCLASS) {
		db_printf("%sIN6P_TCLASS", comma ? ", " : "");
		comma = 1;
	}
	if (inp_flags & IN6P_AUTOFLOWLABEL) {
		db_printf("%sIN6P_AUTOFLOWLABEL", comma ? ", " : "");
		comma = 1;
	}
	if (inp_flags & INP_TIMEWAIT) {
		db_printf("%sINP_TIMEWAIT", comma ? ", " : "");
		comma  = 1;
	}
	if (inp_flags & INP_ONESBCAST) {
		db_printf("%sINP_ONESBCAST", comma ? ", " : "");
		comma  = 1;
	}
	if (inp_flags & INP_DROPPED) {
		db_printf("%sINP_DROPPED", comma ? ", " : "");
		comma  = 1;
	}
	if (inp_flags & INP_SOCKREF) {
		db_printf("%sINP_SOCKREF", comma ? ", " : "");
		comma  = 1;
	}
	if (inp_flags & IN6P_RFC2292) {
		db_printf("%sIN6P_RFC2292", comma ? ", " : "");
		comma = 1;
	}
	if (inp_flags & IN6P_MTU) {
		db_printf("IN6P_MTU%s", comma ? ", " : "");
		comma = 1;
	}
}

static void
db_print_inpvflag(u_char inp_vflag)
{
	int comma;

	comma = 0;
	if (inp_vflag & INP_IPV4) {
		db_printf("%sINP_IPV4", comma ? ", " : "");
		comma  = 1;
	}
	if (inp_vflag & INP_IPV6) {
		db_printf("%sINP_IPV6", comma ? ", " : "");
		comma  = 1;
	}
	if (inp_vflag & INP_IPV6PROTO) {
		db_printf("%sINP_IPV6PROTO", comma ? ", " : "");
		comma  = 1;
	}
}

static void
db_print_inpcb(struct inpcb *inp, const char *name, int indent)
{

	db_print_indent(indent);
	db_printf("%s at %p\n", name, inp);

	indent += 2;

	db_print_indent(indent);
	db_printf("inp_flow: 0x%x\n", inp->inp_flow);

	db_print_inconninfo(&inp->inp_inc, "inp_conninfo", indent);

	db_print_indent(indent);
	db_printf("inp_ppcb: %p   inp_pcbinfo: %p   inp_socket: %p\n",
	    inp->inp_ppcb, inp->inp_pcbinfo, inp->inp_socket);

	db_print_indent(indent);
	db_printf("inp_label: %p   inp_flags: 0x%x (",
	   inp->inp_label, inp->inp_flags);
	db_print_inpflags(inp->inp_flags);
	db_printf(")\n");

	db_print_indent(indent);
	db_printf("inp_sp: %p   inp_vflag: 0x%x (", inp->inp_sp,
	    inp->inp_vflag);
	db_print_inpvflag(inp->inp_vflag);
	db_printf(")\n");

	db_print_indent(indent);
	db_printf("inp_ip_ttl: %d   inp_ip_p: %d   inp_ip_minttl: %d\n",
	    inp->inp_ip_ttl, inp->inp_ip_p, inp->inp_ip_minttl);

	db_print_indent(indent);
#ifdef INET6
	if (inp->inp_vflag & INP_IPV6) {
		db_printf("in6p_options: %p   in6p_outputopts: %p   "
		    "in6p_moptions: %p\n", inp->in6p_options,
		    inp->in6p_outputopts, inp->in6p_moptions);
		db_printf("in6p_icmp6filt: %p   in6p_cksum %d   "
		    "in6p_hops %u\n", inp->in6p_icmp6filt, inp->in6p_cksum,
		    inp->in6p_hops);
	} else
#endif
	{
		db_printf("inp_ip_tos: %d   inp_ip_options: %p   "
		    "inp_ip_moptions: %p\n", inp->inp_ip_tos,
		    inp->inp_options, inp->inp_moptions);
	}

	db_print_indent(indent);
	db_printf("inp_phd: %p   inp_gencnt: %ju\n", inp->inp_phd,
	    (uintmax_t)inp->inp_gencnt);
}

DB_SHOW_COMMAND(inpcb, db_show_inpcb)
{
	struct inpcb *inp;

	if (!have_addr) {
		db_printf("usage: show inpcb <addr>\n");
		return;
	}
	inp = (struct inpcb *)addr;

	db_print_inpcb(inp, "inpcb", 0);
}
#endif /* DDB */

#ifdef RATELIMIT
/*
 * Modify TX rate limit based on the existing "inp->inp_snd_tag",
 * if any.
 */
int
in_pcbmodify_txrtlmt(struct inpcb *inp, uint32_t max_pacing_rate)
{
	union if_snd_tag_modify_params params = {
		.rate_limit.max_rate = max_pacing_rate,
	};
	struct m_snd_tag *mst;
	struct ifnet *ifp;
	int error;

	mst = inp->inp_snd_tag;
	if (mst == NULL)
		return (EINVAL);

	ifp = mst->ifp;
	if (ifp == NULL)
		return (EINVAL);

	if (ifp->if_snd_tag_modify == NULL) {
		error = EOPNOTSUPP;
	} else {
		error = ifp->if_snd_tag_modify(mst, &params);
	}
	return (error);
}

/*
 * Query existing TX rate limit based on the existing
 * "inp->inp_snd_tag", if any.
 */
int
in_pcbquery_txrtlmt(struct inpcb *inp, uint32_t *p_max_pacing_rate)
{
	union if_snd_tag_query_params params = { };
	struct m_snd_tag *mst;
	struct ifnet *ifp;
	int error;

	mst = inp->inp_snd_tag;
	if (mst == NULL)
		return (EINVAL);

	ifp = mst->ifp;
	if (ifp == NULL)
		return (EINVAL);

	if (ifp->if_snd_tag_query == NULL) {
		error = EOPNOTSUPP;
	} else {
		error = ifp->if_snd_tag_query(mst, &params);
		if (error == 0 &&  p_max_pacing_rate != NULL)
			*p_max_pacing_rate = params.rate_limit.max_rate;
	}
	return (error);
}

/*
 * Query existing TX queue level based on the existing
 * "inp->inp_snd_tag", if any.
 */
int
in_pcbquery_txrlevel(struct inpcb *inp, uint32_t *p_txqueue_level)
{
	union if_snd_tag_query_params params = { };
	struct m_snd_tag *mst;
	struct ifnet *ifp;
	int error;

	mst = inp->inp_snd_tag;
	if (mst == NULL)
		return (EINVAL);

	ifp = mst->ifp;
	if (ifp == NULL)
		return (EINVAL);

	if (ifp->if_snd_tag_query == NULL)
		return (EOPNOTSUPP);

	error = ifp->if_snd_tag_query(mst, &params);
	if (error == 0 &&  p_txqueue_level != NULL)
		*p_txqueue_level = params.rate_limit.queue_level;
	return (error);
}

/*
 * Allocate a new TX rate limit send tag from the network interface
 * given by the "ifp" argument and save it in "inp->inp_snd_tag":
 */
int
in_pcbattach_txrtlmt(struct inpcb *inp, struct ifnet *ifp,
    uint32_t flowtype, uint32_t flowid, uint32_t max_pacing_rate)
{
	union if_snd_tag_alloc_params params = {
		.rate_limit.hdr.type = (max_pacing_rate == -1U) ?
		    IF_SND_TAG_TYPE_UNLIMITED : IF_SND_TAG_TYPE_RATE_LIMIT,
		.rate_limit.hdr.flowid = flowid,
		.rate_limit.hdr.flowtype = flowtype,
		.rate_limit.max_rate = max_pacing_rate,
	};
	int error;

	INP_WLOCK_ASSERT(inp);

	if (inp->inp_snd_tag != NULL)
		return (EINVAL);

	if (ifp->if_snd_tag_alloc == NULL) {
		error = EOPNOTSUPP;
	} else {
		error = ifp->if_snd_tag_alloc(ifp, &params, &inp->inp_snd_tag);

		/*
		 * At success increment the refcount on
		 * the send tag's network interface:
		 */
		if (error == 0)
			if_ref(inp->inp_snd_tag->ifp);
	}
	return (error);
}

/*
 * Free an existing TX rate limit tag based on the "inp->inp_snd_tag",
 * if any:
 */
void
in_pcbdetach_txrtlmt(struct inpcb *inp)
{
	struct m_snd_tag *mst;
	struct ifnet *ifp;

	INP_WLOCK_ASSERT(inp);

	mst = inp->inp_snd_tag;
	inp->inp_snd_tag = NULL;

	if (mst == NULL)
		return;

	ifp = mst->ifp;
	if (ifp == NULL)
		return;

	/*
	 * If the device was detached while we still had reference(s)
	 * on the ifp, we assume if_snd_tag_free() was replaced with
	 * stubs.
	 */
	ifp->if_snd_tag_free(mst);

	/* release reference count on network interface */
	if_rele(ifp);
}

/*
 * This function should be called when the INP_RATE_LIMIT_CHANGED flag
 * is set in the fast path and will attach/detach/modify the TX rate
 * limit send tag based on the socket's so_max_pacing_rate value.
 */
void
in_pcboutput_txrtlmt(struct inpcb *inp, struct ifnet *ifp, struct mbuf *mb)
{
	struct socket *socket;
	uint32_t max_pacing_rate;
	bool did_upgrade;
	int error;

	if (inp == NULL)
		return;

	socket = inp->inp_socket;
	if (socket == NULL)
		return;

	if (!INP_WLOCKED(inp)) {
		/*
		 * NOTE: If the write locking fails, we need to bail
		 * out and use the non-ratelimited ring for the
		 * transmit until there is a new chance to get the
		 * write lock.
		 */
		if (!INP_TRY_UPGRADE(inp))
			return;
		did_upgrade = 1;
	} else {
		did_upgrade = 0;
	}

	/*
	 * NOTE: The so_max_pacing_rate value is read unlocked,
	 * because atomic updates are not required since the variable
	 * is checked at every mbuf we send. It is assumed that the
	 * variable read itself will be atomic.
	 */
	max_pacing_rate = socket->so_max_pacing_rate;

	/*
	 * NOTE: When attaching to a network interface a reference is
	 * made to ensure the network interface doesn't go away until
	 * all ratelimit connections are gone. The network interface
	 * pointers compared below represent valid network interfaces,
	 * except when comparing towards NULL.
	 */
	if (max_pacing_rate == 0 && inp->inp_snd_tag == NULL) {
		error = 0;
	} else if (!(ifp->if_capenable & IFCAP_TXRTLMT)) {
		if (inp->inp_snd_tag != NULL)
			in_pcbdetach_txrtlmt(inp);
		error = 0;
	} else if (inp->inp_snd_tag == NULL) {
		/*
		 * In order to utilize packet pacing with RSS, we need
		 * to wait until there is a valid RSS hash before we
		 * can proceed:
		 */
		if (M_HASHTYPE_GET(mb) == M_HASHTYPE_NONE) {
			error = EAGAIN;
		} else {
			error = in_pcbattach_txrtlmt(inp, ifp, M_HASHTYPE_GET(mb),
			    mb->m_pkthdr.flowid, max_pacing_rate);
		}
	} else {
		error = in_pcbmodify_txrtlmt(inp, max_pacing_rate);
	}
	if (error == 0 || error == EOPNOTSUPP)
		inp->inp_flags2 &= ~INP_RATE_LIMIT_CHANGED;
	if (did_upgrade)
		INP_DOWNGRADE(inp);
}

/*
 * Track route changes for TX rate limiting.
 */
void
in_pcboutput_eagain(struct inpcb *inp)
{
	struct socket *socket;
	bool did_upgrade;

	if (inp == NULL)
		return;

	socket = inp->inp_socket;
	if (socket == NULL)
		return;

	if (inp->inp_snd_tag == NULL)
		return;

	if (!INP_WLOCKED(inp)) {
		/*
		 * NOTE: If the write locking fails, we need to bail
		 * out and use the non-ratelimited ring for the
		 * transmit until there is a new chance to get the
		 * write lock.
		 */
		if (!INP_TRY_UPGRADE(inp))
			return;
		did_upgrade = 1;
	} else {
		did_upgrade = 0;
	}

	/* detach rate limiting */
	in_pcbdetach_txrtlmt(inp);

	/* make sure new mbuf send tag allocation is made */
	inp->inp_flags2 |= INP_RATE_LIMIT_CHANGED;

	if (did_upgrade)
		INP_DOWNGRADE(inp);
}
#endif /* RATELIMIT */
