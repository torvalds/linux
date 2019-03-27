/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1983, 1993
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
 * $FreeBSD$
 */

#include <stdint.h>

#include "defs.h"
#include "pathnames.h"

#ifdef __NetBSD__
__RCSID("$NetBSD$");
#elif defined(__FreeBSD__)
__RCSID("$FreeBSD$");
#else
__RCSID("$Revision: 2.27 $");
#ident "$Revision: 2.27 $"
#endif

struct ifhead ifnet = LIST_HEAD_INITIALIZER(ifnet);	/* all interfaces */
struct ifhead remote_if = LIST_HEAD_INITIALIZER(remote_if);	/* remote interfaces */

/* hash table for all interfaces, big enough to tolerate ridiculous
 * numbers of IP aliases.  Crazy numbers of aliases such as 7000
 * still will not do well, but not just in looking up interfaces
 * by name or address.
 */
#define AHASH_LEN 211			/* must be prime */
#define AHASH(a) &ahash_tbl[(a)%AHASH_LEN]
static struct interface *ahash_tbl[AHASH_LEN];

#define BHASH_LEN 211			/* must be prime */
#define BHASH(a) &bhash_tbl[(a)%BHASH_LEN]
static struct interface *bhash_tbl[BHASH_LEN];


/* hash for physical interface names.
 * Assume there are never more 100 or 200 real interfaces, and that
 * aliases are put on the end of the hash chains.
 */
#define NHASH_LEN 97
static struct interface *nhash_tbl[NHASH_LEN];

int	tot_interfaces;			/* # of remote and local interfaces */
int	rip_interfaces;			/* # of interfaces doing RIP */
static int foundloopback;			/* valid flag for loopaddr */
naddr	loopaddr;			/* our address on loopback */
static struct rt_spare loop_rts;

struct timeval ifinit_timer;
static struct timeval last_ifinit;
#define IF_RESCAN_DELAY() (last_ifinit.tv_sec == now.tv_sec		\
			   && last_ifinit.tv_usec == now.tv_usec	\
			   && timercmp(&ifinit_timer, &now, >))

int	have_ripv1_out;			/* have a RIPv1 interface */
static int have_ripv1_in;


static void if_bad(struct interface *);
static int addrouteforif(struct interface *);

static struct interface**
nhash(char *p)
{
	u_int i;

	for (i = 0; *p != '\0'; p++) {
		i = ((i<<1) & 0x7fffffff) | ((i>>31) & 1);
		i ^= *p;
	}
	return &nhash_tbl[i % NHASH_LEN];
}


/* Link a new interface into the lists and hash tables.
 */
void
if_link(struct interface *ifp)
{
	struct interface **hifp;

	LIST_INSERT_HEAD(&ifnet, ifp, int_list);

	hifp = AHASH(ifp->int_addr);
	ifp->int_ahash_prev = hifp;
	if ((ifp->int_ahash = *hifp) != NULL)
		(*hifp)->int_ahash_prev = &ifp->int_ahash;
	*hifp = ifp;

	if (ifp->int_if_flags & IFF_BROADCAST) {
		hifp = BHASH(ifp->int_brdaddr);
		ifp->int_bhash_prev = hifp;
		if ((ifp->int_bhash = *hifp) != NULL)
			(*hifp)->int_bhash_prev = &ifp->int_bhash;
		*hifp = ifp;
	}

	if (ifp->int_state & IS_REMOTE)
		LIST_INSERT_HEAD(&remote_if, ifp, remote_list);

	hifp = nhash(ifp->int_name);
	if (ifp->int_state & IS_ALIAS) {
		/* put aliases on the end of the hash chain */
		while (*hifp != NULL)
			hifp = &(*hifp)->int_nhash;
	}
	ifp->int_nhash_prev = hifp;
	if ((ifp->int_nhash = *hifp) != NULL)
		(*hifp)->int_nhash_prev = &ifp->int_nhash;
	*hifp = ifp;
}


/* Find the interface with an address
 */
struct interface *
ifwithaddr(naddr addr,
	   int	bcast,			/* notice IFF_BROADCAST address */
	   int	remote)			/* include IS_REMOTE interfaces */
{
	struct interface *ifp, *possible = NULL;

	remote = (remote == 0) ? IS_REMOTE : 0;

	for (ifp = *AHASH(addr); ifp; ifp = ifp->int_ahash) {
		if (ifp->int_addr != addr)
			continue;
		if ((ifp->int_state & remote) != 0)
			continue;
		if ((ifp->int_state & (IS_BROKE | IS_PASSIVE)) == 0)
			return ifp;
		possible = ifp;
	}

	if (possible || !bcast)
		return possible;

	for (ifp = *BHASH(addr); ifp; ifp = ifp->int_bhash) {
		if (ifp->int_brdaddr != addr)
			continue;
		if ((ifp->int_state & remote) != 0)
			continue;
		if ((ifp->int_state & (IS_BROKE | IS_PASSIVE)) == 0)
			return ifp;
		possible = ifp;
	}

	return possible;
}


/* find the interface with a name
 */
static struct interface *
ifwithname(char *name,			/* "ec0" or whatever */
	   naddr addr)			/* 0 or network address */
{
	struct interface *ifp;

	for (;;) {
		for (ifp = *nhash(name); ifp != NULL; ifp = ifp->int_nhash) {
			/* If the network address is not specified,
			 * ignore any alias interfaces.  Otherwise, look
			 * for the interface with the target name and address.
			 */
			if (!strcmp(ifp->int_name, name)
			    && ((addr == 0 && !(ifp->int_state & IS_ALIAS))
				|| (ifp->int_addr == addr)))
				return ifp;
		}

		/* If there is no known interface, maybe there is a
		 * new interface.  So just once look for new interfaces.
		 */
		if (IF_RESCAN_DELAY())
			return 0;
		ifinit();
	}
}


struct interface *
ifwithindex(u_short ifindex,
	    int rescan_ok)
{
	struct interface *ifp;

	for (;;) {
		LIST_FOREACH(ifp, &ifnet, int_list) {
			if (ifp->int_index == ifindex)
				return ifp;
		}

		/* If there is no known interface, maybe there is a
		 * new interface.  So just once look for new interfaces.
		 */
		if (!rescan_ok
		    || IF_RESCAN_DELAY())
			return 0;
		ifinit();
	}
}


/* Find an interface from which the specified address
 * should have come from.  Used for figuring out which
 * interface a packet came in on.
 */
struct interface *
iflookup(naddr addr)
{
	struct interface *ifp, *maybe;
	int once = 0;

	maybe = NULL;
	for (;;) {
		LIST_FOREACH(ifp, &ifnet, int_list) {
			if (ifp->int_if_flags & IFF_POINTOPOINT) {
				/* finished with a match */
				if (ifp->int_dstaddr == addr)
					return ifp;

			} else {
				/* finished with an exact match */
				if (ifp->int_addr == addr)
					return ifp;

				/* Look for the longest approximate match.
				 */
				if (on_net(addr, ifp->int_net, ifp->int_mask)
				    && (maybe == NULL
					|| ifp->int_mask > maybe->int_mask))
					maybe = ifp;
			}
		}

		if (maybe != NULL || once || IF_RESCAN_DELAY())
			return maybe;
		once = 1;

		/* If there is no known interface, maybe there is a
		 * new interface.  So just once look for new interfaces.
		 */
		ifinit();
	}
}


/* Return the classical netmask for an IP address.
 */
naddr					/* host byte order */
std_mask(naddr addr)			/* network byte order */
{
	addr = ntohl(addr);			/* was a host, not a network */

	if (addr == 0)			/* default route has mask 0 */
		return 0;
	if (IN_CLASSA(addr))
		return IN_CLASSA_NET;
	if (IN_CLASSB(addr))
		return IN_CLASSB_NET;
	return IN_CLASSC_NET;
}


/* Find the netmask that would be inferred by RIPv1 listeners
 *	on the given interface for a given network.
 *	If no interface is specified, look for the best fitting	interface.
 */
naddr
ripv1_mask_net(naddr addr,		/* in network byte order */
	       struct interface *ifp)	/* as seen on this interface */
{
	struct r1net *r1p;
	naddr mask = 0;

	if (addr == 0)			/* default always has 0 mask */
		return mask;

	if (ifp != NULL && ifp->int_ripv1_mask != HOST_MASK) {
		/* If the target network is that of the associated interface
		 * on which it arrived, then use the netmask of the interface.
		 */
		if (on_net(addr, ifp->int_net, ifp->int_std_mask))
			mask = ifp->int_ripv1_mask;

	} else {
		/* Examine all interfaces, and if it the target seems
		 * to have the same network number of an interface, use the
		 * netmask of that interface.  If there is more than one
		 * such interface, prefer the interface with the longest
		 * match.
		 */
		LIST_FOREACH(ifp, &ifnet, int_list) {
			if (on_net(addr, ifp->int_std_net, ifp->int_std_mask)
			    && ifp->int_ripv1_mask > mask
			    && ifp->int_ripv1_mask != HOST_MASK)
				mask = ifp->int_ripv1_mask;
		}

	}

	/* check special definitions */
	if (mask == 0) {
		for (r1p = r1nets; r1p != NULL; r1p = r1p->r1net_next) {
			if (on_net(addr, r1p->r1net_net, r1p->r1net_match)
			    && r1p->r1net_mask > mask)
				mask = r1p->r1net_mask;
		}

		/* Otherwise, make the classic A/B/C guess.
		 */
		if (mask == 0)
			mask = std_mask(addr);
	}

	return mask;
}


naddr
ripv1_mask_host(naddr addr,		/* in network byte order */
		struct interface *ifp)	/* as seen on this interface */
{
	naddr mask = ripv1_mask_net(addr, ifp);


	/* If the computed netmask does not mask the address,
	 * then assume it is a host address
	 */
	if ((ntohl(addr) & ~mask) != 0)
		mask = HOST_MASK;
	return mask;
}


/* See if an IP address looks reasonable as a destination.
 */
int					/* 0=bad */
check_dst(naddr addr)
{
	addr = ntohl(addr);

	if (IN_CLASSA(addr)) {
		if (addr == 0)
			return 1;	/* default */

		addr >>= IN_CLASSA_NSHIFT;
		return (addr != 0 && addr != IN_LOOPBACKNET);
	}

	return (IN_CLASSB(addr) || IN_CLASSC(addr));
}


/* See a new interface duplicates an existing interface.
 */
struct interface *
check_dup(naddr addr,			/* IP address, so network byte order */
	  naddr dstaddr,		/* ditto */
	  naddr mask,			/* mask, so host byte order */
	  int if_flags)
{
	struct interface *ifp;

	LIST_FOREACH(ifp, &ifnet, int_list) {
		if (ifp->int_mask != mask)
			continue;

		if (!iff_up(ifp->int_if_flags))
			continue;

		/* The local address can only be shared with a point-to-point
		 * link.
		 */
		if ((!(ifp->int_state & IS_REMOTE) || !(if_flags & IS_REMOTE))
		    && ifp->int_addr == addr
		    && (((if_flags|ifp->int_if_flags) & IFF_POINTOPOINT) == 0))
			return ifp;

		if (on_net(ifp->int_dstaddr, ntohl(dstaddr),mask))
			return ifp;
	}
	return 0;
}


/* See that a remote gateway is reachable.
 *	Note that the answer can change as real interfaces come and go.
 */
int					/* 0=bad */
check_remote(struct interface *ifp)
{
	struct rt_entry *rt;

	/* do not worry about other kinds */
	if (!(ifp->int_state & IS_REMOTE))
	    return 1;

	rt = rtfind(ifp->int_addr);
	if (rt != NULL 
	    && rt->rt_ifp != 0
	    &&on_net(ifp->int_addr,
		     rt->rt_ifp->int_net, rt->rt_ifp->int_mask))
		return 1;

	/* the gateway cannot be reached directly from one of our
	 * interfaces
	 */
	if (!(ifp->int_state & IS_BROKE)) {
		msglog("unreachable gateway %s in "_PATH_GATEWAYS,
		       naddr_ntoa(ifp->int_addr));
		if_bad(ifp);
	}
	return 0;
}


/* Delete an interface.
 */
static void
ifdel(struct interface *ifp)
{
	struct interface *ifp1;


	trace_if("Del", ifp);

	ifp->int_state |= IS_BROKE;

	LIST_REMOVE(ifp, int_list);
	*ifp->int_ahash_prev = ifp->int_ahash;
	if (ifp->int_ahash != 0)
		ifp->int_ahash->int_ahash_prev = ifp->int_ahash_prev;
	*ifp->int_nhash_prev = ifp->int_nhash;
	if (ifp->int_nhash != 0)
		ifp->int_nhash->int_nhash_prev = ifp->int_nhash_prev;
	if (ifp->int_if_flags & IFF_BROADCAST) {
		*ifp->int_bhash_prev = ifp->int_bhash;
		if (ifp->int_bhash != 0)
			ifp->int_bhash->int_bhash_prev = ifp->int_bhash_prev;
	}
	if (ifp->int_state & IS_REMOTE)
		LIST_REMOVE(ifp, remote_list);

	if (!(ifp->int_state & IS_ALIAS)) {
		/* delete aliases when the main interface dies
		 */
		LIST_FOREACH(ifp1, &ifnet, int_list) {
			if (ifp1 != ifp
			    && !strcmp(ifp->int_name, ifp1->int_name))
				ifdel(ifp1);
		}

		if ((ifp->int_if_flags & IFF_MULTICAST) && rip_sock >= 0) {
			struct group_req gr;
			struct sockaddr_in *sin;

			memset(&gr, 0, sizeof(gr));
			gr.gr_interface = ifp->int_index;
			sin = (struct sockaddr_in *)&gr.gr_group;
			sin->sin_family = AF_INET;
#ifdef _HAVE_SIN_LEN
			sin->sin_len = sizeof(struct sockaddr_in);
#endif
			sin->sin_addr.s_addr = htonl(INADDR_RIP_GROUP);
			if (setsockopt(rip_sock, IPPROTO_IP, MCAST_LEAVE_GROUP,
				       &gr, sizeof(gr)) < 0
			    && errno != EADDRNOTAVAIL
			    && !TRACEACTIONS)
				LOGERR("setsockopt(MCAST_LEAVE_GROUP RIP)");
			if (rip_sock_mcast == ifp)
				rip_sock_mcast = NULL;
		}
		if (ifp->int_rip_sock >= 0) {
			(void)close(ifp->int_rip_sock);
			ifp->int_rip_sock = -1;
			fix_select();
		}

		tot_interfaces--;
		if (!IS_RIP_OFF(ifp->int_state))
			rip_interfaces--;

		/* Zap all routes associated with this interface.
		 * Assume routes just using gateways beyond this interface
		 * will timeout naturally, and have probably already died.
		 */
		(void)rn_walktree(rhead, walk_bad, 0);

		set_rdisc_mg(ifp, 0);
		if_bad_rdisc(ifp);
	}

	free(ifp);
}


/* Mark an interface ill.
 */
void
if_sick(struct interface *ifp)
{
	if (0 == (ifp->int_state & (IS_SICK | IS_BROKE))) {
		ifp->int_state |= IS_SICK;
		ifp->int_act_time = NEVER;
		trace_if("Chg", ifp);

		LIM_SEC(ifinit_timer, now.tv_sec+CHECK_BAD_INTERVAL);
	}
}


/* Mark an interface dead.
 */
static void
if_bad(struct interface *ifp)
{
	struct interface *ifp1;


	if (ifp->int_state & IS_BROKE)
		return;

	LIM_SEC(ifinit_timer, now.tv_sec+CHECK_BAD_INTERVAL);

	ifp->int_state |= (IS_BROKE | IS_SICK);
	ifp->int_act_time = NEVER;
	ifp->int_query_time = NEVER;
	ifp->int_data.ts = now.tv_sec;

	trace_if("Chg", ifp);

	if (!(ifp->int_state & IS_ALIAS)) {
		LIST_FOREACH(ifp1, &ifnet, int_list) {
			if (ifp1 != ifp
			    && !strcmp(ifp->int_name, ifp1->int_name))
				if_bad(ifp1);
		}
		(void)rn_walktree(rhead, walk_bad, 0);
		if_bad_rdisc(ifp);
	}
}


/* Mark an interface alive
 */
int					/* 1=it was dead */
if_ok(struct interface *ifp,
      const char *type)
{
	struct interface *ifp1;


	if (!(ifp->int_state & IS_BROKE)) {
		if (ifp->int_state & IS_SICK) {
			trace_act("%sinterface %s to %s working better",
				  type,
				  ifp->int_name, naddr_ntoa(ifp->int_dstaddr));
			ifp->int_state &= ~IS_SICK;
		}
		return 0;
	}

	msglog("%sinterface %s to %s restored",
	       type, ifp->int_name, naddr_ntoa(ifp->int_dstaddr));
	ifp->int_state &= ~(IS_BROKE | IS_SICK);
	ifp->int_data.ts = 0;

	if (!(ifp->int_state & IS_ALIAS)) {
		LIST_FOREACH(ifp1, &ifnet, int_list) {
			if (ifp1 != ifp
			    && !strcmp(ifp->int_name, ifp1->int_name))
				if_ok(ifp1, type);
		}
		if_ok_rdisc(ifp);
	}

	if (ifp->int_state & IS_REMOTE) {
		if (!addrouteforif(ifp))
			return 0;
	}
	return 1;
}


/* disassemble routing message
 */
void
rt_xaddrs(struct rt_addrinfo *info,
	  struct sockaddr *sa,
	  struct sockaddr *lim,
	  int addrs)
{
	int i;
#ifdef _HAVE_SA_LEN
	static struct sockaddr sa_zero;
#endif

	memset(info, 0, sizeof(*info));
	info->rti_addrs = addrs;
	for (i = 0; i < RTAX_MAX && sa < lim; i++) {
		if ((addrs & (1 << i)) == 0)
			continue;
		info->rti_info[i] = (sa->sa_len != 0) ? sa : &sa_zero;
		sa = (struct sockaddr *)((char*)(sa) + SA_SIZE(sa));
	}
}


/* Find the network interfaces which have configured themselves.
 *	This must be done regularly, if only for extra addresses
 *	that come and go on interfaces.
 */
void
ifinit(void)
{
	static struct ifa_msghdr *sysctl_buf;
	static size_t sysctl_buf_size = 0;
	uint complaints = 0;
	static u_int prev_complaints = 0;
#	define COMP_NOT_INET	0x001
#	define COMP_NOADDR	0x002
#	define COMP_BADADDR	0x004
#	define COMP_NODST	0x008
#	define COMP_NOBADR	0x010
#	define COMP_NOMASK	0x020
#	define COMP_DUP		0x040
#	define COMP_BAD_METRIC	0x080
#	define COMP_NETMASK	0x100

	struct interface ifs, ifs0, *ifp, *ifp1;
	struct rt_entry *rt;
	size_t needed;
	int mib[6];
	struct if_msghdr *ifm;
	void *ifam_lim;
	struct ifa_msghdr *ifam, *ifam2;
	int in, ierr, out, oerr;
	struct intnet *intnetp;
	struct rt_addrinfo info;
#ifdef SIOCGIFMETRIC
	struct ifreq ifr;
#endif


	last_ifinit = now;
	ifinit_timer.tv_sec = now.tv_sec + (supplier
					    ? CHECK_ACT_INTERVAL
					    : CHECK_QUIET_INTERVAL);

	/* mark all interfaces so we can get rid of those that disappear */
	LIST_FOREACH(ifp, &ifnet, int_list)
		ifp->int_state &= ~(IS_CHECKED | IS_DUP);

	/* Fetch the interface list, without too many system calls
	 * since we do it repeatedly.
	 */
	mib[0] = CTL_NET;
	mib[1] = PF_ROUTE;
	mib[2] = 0;
	mib[3] = AF_INET;
	mib[4] = NET_RT_IFLIST;
	mib[5] = 0;
	for (;;) {
		if ((needed = sysctl_buf_size) != 0) {
			if (sysctl(mib, 6, sysctl_buf,&needed, 0, 0) >= 0)
				break;
			/* retry if the table grew */
			if (errno != ENOMEM && errno != EFAULT)
				BADERR(1, "ifinit: sysctl(RT_IFLIST)");
			free(sysctl_buf);
			needed = 0;
		}
		if (sysctl(mib, 6, 0, &needed, 0, 0) < 0)
			BADERR(1,"ifinit: sysctl(RT_IFLIST) estimate");
		sysctl_buf = rtmalloc(sysctl_buf_size = needed,
				      "ifinit sysctl");
	}

	/* XXX: thanks to malloc(3), alignment can be presumed OK */
	ifam_lim = (char *)sysctl_buf + needed;
	for (ifam = sysctl_buf; (void *)ifam < ifam_lim; ifam = ifam2) {

		ifam2 = (struct ifa_msghdr*)((char*)ifam + ifam->ifam_msglen);

#ifdef RTM_OIFINFO
		if (ifam->ifam_type == RTM_OIFINFO)
			continue;	/* just ignore compat message */
#endif
		if (ifam->ifam_type == RTM_IFINFO) {
			struct sockaddr_dl *sdl;

			ifm = (struct if_msghdr *)ifam;
			/* make prototype structure for the IP aliases
			 */
			memset(&ifs0, 0, sizeof(ifs0));
			ifs0.int_rip_sock = -1;
			ifs0.int_index = ifm->ifm_index;
			ifs0.int_if_flags = ifm->ifm_flags;
			ifs0.int_state = IS_CHECKED;
			ifs0.int_query_time = NEVER;
			ifs0.int_act_time = now.tv_sec;
			ifs0.int_data.ts = now.tv_sec;
			ifs0.int_data.ipackets = ifm->ifm_data.ifi_ipackets;
			ifs0.int_data.ierrors = ifm->ifm_data.ifi_ierrors;
			ifs0.int_data.opackets = ifm->ifm_data.ifi_opackets;
			ifs0.int_data.oerrors = ifm->ifm_data.ifi_oerrors;
#ifdef sgi
			ifs0.int_data.odrops = ifm->ifm_data.ifi_odrops;
#endif
			sdl = (struct sockaddr_dl *)(ifm + 1);
			sdl->sdl_data[sdl->sdl_nlen] = 0;
			strncpy(ifs0.int_name, sdl->sdl_data,
				MIN(sizeof(ifs0.int_name), sdl->sdl_nlen));
			continue;
		}
		if (ifam->ifam_type != RTM_NEWADDR) {
			logbad(1,"ifinit: out of sync");
			continue;
		}
		rt_xaddrs(&info, (struct sockaddr *)(ifam+1),
			  (struct sockaddr *)ifam2,
			  ifam->ifam_addrs);

		/* Prepare for the next address of this interface, which
		 * will be an alias.
		 * Do not output RIP or Router-Discovery packets via aliases.
		 */
		memcpy(&ifs, &ifs0, sizeof(ifs));
		ifs0.int_state |= (IS_ALIAS | IS_NO_RIP_OUT | IS_NO_RDISC);

		if (INFO_IFA(&info) == 0) {
			if (iff_up(ifs.int_if_flags)) {
				if (!(prev_complaints & COMP_NOADDR))
					msglog("%s has no address",
					       ifs.int_name);
				complaints |= COMP_NOADDR;
			}
			continue;
		}
		if (INFO_IFA(&info)->sa_family != AF_INET) {
			if (iff_up(ifs.int_if_flags)) {
				if (!(prev_complaints & COMP_NOT_INET))
					trace_act("%s: not AF_INET",
						  ifs.int_name);
				complaints |= COMP_NOT_INET;
			}
			continue;
		}

		ifs.int_addr = S_ADDR(INFO_IFA(&info));

		if (ntohl(ifs.int_addr)>>24 == 0
		    || ntohl(ifs.int_addr)>>24 == 0xff) {
			if (iff_up(ifs.int_if_flags)) {
				if (!(prev_complaints & COMP_BADADDR))
					msglog("%s has a bad address",
					       ifs.int_name);
				complaints |= COMP_BADADDR;
			}
			continue;
		}

		if (ifs.int_if_flags & IFF_LOOPBACK) {
			ifs.int_state |= IS_NO_RIP | IS_NO_RDISC;
			if (ifs.int_addr == htonl(INADDR_LOOPBACK))
				ifs.int_state |= IS_PASSIVE;
			ifs.int_dstaddr = ifs.int_addr;
			ifs.int_mask = HOST_MASK;
			ifs.int_ripv1_mask = HOST_MASK;
			ifs.int_std_mask = std_mask(ifs.int_dstaddr);
			ifs.int_net = ntohl(ifs.int_dstaddr);
			if (!foundloopback) {
				foundloopback = 1;
				loopaddr = ifs.int_addr;
				loop_rts.rts_gate = loopaddr;
				loop_rts.rts_router = loopaddr;
			}

		} else if (ifs.int_if_flags & IFF_POINTOPOINT) {
			if (INFO_BRD(&info) == 0
			    || INFO_BRD(&info)->sa_family != AF_INET) {
				if (iff_up(ifs.int_if_flags)) {
					if (!(prev_complaints & COMP_NODST))
						msglog("%s has a bad"
						       " destination address",
						       ifs.int_name);
					complaints |= COMP_NODST;
				}
				continue;
			}
			ifs.int_dstaddr = S_ADDR(INFO_BRD(&info));
			if (ntohl(ifs.int_dstaddr)>>24 == 0
			    || ntohl(ifs.int_dstaddr)>>24 == 0xff) {
				if (iff_up(ifs.int_if_flags)) {
					if (!(prev_complaints & COMP_NODST))
						msglog("%s has a bad"
						       " destination address",
						       ifs.int_name);
					complaints |= COMP_NODST;
				}
				continue;
			}
			ifs.int_mask = HOST_MASK;
			ifs.int_ripv1_mask = ntohl(S_ADDR(INFO_MASK(&info)));
			ifs.int_std_mask = std_mask(ifs.int_dstaddr);
			ifs.int_net = ntohl(ifs.int_dstaddr);

		}  else {
			if (INFO_MASK(&info) == 0) {
				if (iff_up(ifs.int_if_flags)) {
					if (!(prev_complaints & COMP_NOMASK))
						msglog("%s has no netmask",
						       ifs.int_name);
					complaints |= COMP_NOMASK;
				}
				continue;
			}
			ifs.int_dstaddr = ifs.int_addr;
			ifs.int_mask = ntohl(S_ADDR(INFO_MASK(&info)));
			ifs.int_ripv1_mask = ifs.int_mask;
			ifs.int_std_mask = std_mask(ifs.int_addr);
			ifs.int_net = ntohl(ifs.int_addr) & ifs.int_mask;
			if (ifs.int_mask != ifs.int_std_mask)
				ifs.int_state |= IS_SUBNET;

			if (ifs.int_if_flags & IFF_BROADCAST) {
				if (INFO_BRD(&info) == 0) {
					if (iff_up(ifs.int_if_flags)) {
					    if (!(prev_complaints
						  & COMP_NOBADR))
						msglog("%s has"
						       "no broadcast address",
						       ifs.int_name);
					    complaints |= COMP_NOBADR;
					}
					continue;
				}
				ifs.int_brdaddr = S_ADDR(INFO_BRD(&info));
			}
		}
		ifs.int_std_net = ifs.int_net & ifs.int_std_mask;
		ifs.int_std_addr = htonl(ifs.int_std_net);

		/* Use a minimum metric of one.  Treat the interface metric
		 * (default 0) as an increment to the hop count of one.
		 *
		 * The metric obtained from the routing socket dump of
		 * interface addresses is wrong.  It is not set by the
		 * SIOCSIFMETRIC ioctl.
		 */
#ifdef SIOCGIFMETRIC
		strncpy(ifr.ifr_name, ifs.int_name, sizeof(ifr.ifr_name));
		if (ioctl(rt_sock, SIOCGIFMETRIC, &ifr) < 0) {
			DBGERR(1, "ioctl(SIOCGIFMETRIC)");
			ifs.int_metric = 0;
		} else {
			ifs.int_metric = ifr.ifr_metric;
		}
#else
		ifs.int_metric = ifam->ifam_metric;
#endif
		if (ifs.int_metric > HOPCNT_INFINITY) {
			ifs.int_metric = 0;
			if (!(prev_complaints & COMP_BAD_METRIC)
			    && iff_up(ifs.int_if_flags)) {
				complaints |= COMP_BAD_METRIC;
				msglog("%s has a metric of %d",
				       ifs.int_name, ifs.int_metric);
			}
		}

		/* See if this is a familiar interface.
		 * If so, stop worrying about it if it is the same.
		 * Start it over if it now is to somewhere else, as happens
		 * frequently with PPP and SLIP.
		 */
		ifp = ifwithname(ifs.int_name, ((ifs.int_state & IS_ALIAS)
						? ifs.int_addr
						: 0));
		if (ifp != NULL) {
			ifp->int_state |= IS_CHECKED;

			if (0 != ((ifp->int_if_flags ^ ifs.int_if_flags)
				  & (IFF_BROADCAST
				     | IFF_LOOPBACK
				     | IFF_POINTOPOINT
				     | IFF_MULTICAST))
			    || 0 != ((ifp->int_state ^ ifs.int_state)
				     & IS_ALIAS)
			    || ifp->int_addr != ifs.int_addr
			    || ifp->int_brdaddr != ifs.int_brdaddr
			    || ifp->int_dstaddr != ifs.int_dstaddr
			    || ifp->int_mask != ifs.int_mask
			    || ifp->int_metric != ifs.int_metric) {
				/* Forget old information about
				 * a changed interface.
				 */
				trace_act("interface %s has changed",
					  ifp->int_name);
				ifdel(ifp);
				ifp = NULL;
			}
		}

		if (ifp != NULL) {
			/* The primary representative of an alias worries
			 * about how things are working.
			 */
			if (ifp->int_state & IS_ALIAS)
				continue;

			/* note interfaces that have been turned off
			 */
			if (!iff_up(ifs.int_if_flags)) {
				if (iff_up(ifp->int_if_flags)) {
					msglog("interface %s to %s turned off",
					       ifp->int_name,
					       naddr_ntoa(ifp->int_dstaddr));
					if_bad(ifp);
					ifp->int_if_flags &= ~IFF_UP;
				} else if (now.tv_sec>(ifp->int_data.ts
						       + CHECK_BAD_INTERVAL)) {
					trace_act("interface %s has been off"
						  " %jd seconds; forget it",
						  ifp->int_name,
						  (intmax_t)now.tv_sec -
						      ifp->int_data.ts);
					ifdel(ifp);
					ifp = NULL;
				}
				continue;
			}
			/* or that were off and are now ok */
			if (!iff_up(ifp->int_if_flags)) {
				ifp->int_if_flags |= IFF_UP;
				(void)if_ok(ifp, "");
			}

			/* If it has been long enough,
			 * see if the interface is broken.
			 */
			if (now.tv_sec < ifp->int_data.ts+CHECK_BAD_INTERVAL)
				continue;

			in = ifs.int_data.ipackets - ifp->int_data.ipackets;
			ierr = ifs.int_data.ierrors - ifp->int_data.ierrors;
			out = ifs.int_data.opackets - ifp->int_data.opackets;
			oerr = ifs.int_data.oerrors - ifp->int_data.oerrors;
#ifdef sgi
			/* Through at least IRIX 6.2, PPP and SLIP
			 * count packets dropped by the filters.
			 * But FDDI rings stuck non-operational count
			 * dropped packets as they wait for improvement.
			 */
			if (!(ifp->int_if_flags & IFF_POINTOPOINT))
				oerr += (ifs.int_data.odrops
					 - ifp->int_data.odrops);
#endif
			/* If the interface just awoke, restart the counters.
			 */
			if (ifp->int_data.ts == 0) {
				ifp->int_data = ifs.int_data;
				continue;
			}
			ifp->int_data = ifs.int_data;

			/* Withhold judgment when the short error
			 * counters wrap or the interface is reset.
			 */
			if (ierr < 0 || in < 0 || oerr < 0 || out < 0) {
				LIM_SEC(ifinit_timer,
					now.tv_sec+CHECK_BAD_INTERVAL);
				continue;
			}

			/* Withhold judgement when there is no traffic
			 */
			if (in == 0 && out == 0 && ierr == 0 && oerr == 0)
				continue;

			/* It is bad if input or output is not working.
			 * Require presistent problems before marking it dead.
			 */
			if ((in <= ierr && ierr > 0)
			    || (out <= oerr && oerr > 0)) {
				if (!(ifp->int_state & IS_SICK)) {
					trace_act("interface %s to %s"
						  " sick: in=%d ierr=%d"
						  " out=%d oerr=%d",
						  ifp->int_name,
						  naddr_ntoa(ifp->int_dstaddr),
						  in, ierr, out, oerr);
					if_sick(ifp);
					continue;
				}
				if (!(ifp->int_state & IS_BROKE)) {
					msglog("interface %s to %s broken:"
					       " in=%d ierr=%d out=%d oerr=%d",
					       ifp->int_name,
					       naddr_ntoa(ifp->int_dstaddr),
					       in, ierr, out, oerr);
					if_bad(ifp);
				}
				continue;
			}

			/* otherwise, it is active and healthy
			 */
			ifp->int_act_time = now.tv_sec;
			(void)if_ok(ifp, "");
			continue;
		}

		/* This is a new interface.
		 * If it is dead, forget it.
		 */
		if (!iff_up(ifs.int_if_flags))
			continue;

		/* If it duplicates an existing interface,
		 * complain about it, mark the other one
		 * duplicated, and forget this one.
		 */
		ifp = check_dup(ifs.int_addr,ifs.int_dstaddr,ifs.int_mask,
				ifs.int_if_flags);
		if (ifp != NULL) {
			/* Ignore duplicates of itself, caused by having
			 * IP aliases on the same network.
			 */
			if (!strcmp(ifp->int_name, ifs.int_name))
				continue;

			if (!(prev_complaints & COMP_DUP)) {
				complaints |= COMP_DUP;
				msglog("%s (%s%s%s) is duplicated by"
				       " %s (%s%s%s)",
				       ifs.int_name,
				       addrname(ifs.int_addr,ifs.int_mask,1),
				       ((ifs.int_if_flags & IFF_POINTOPOINT)
					? "-->" : ""),
				       ((ifs.int_if_flags & IFF_POINTOPOINT)
					? naddr_ntoa(ifs.int_dstaddr) : ""),
				       ifp->int_name,
				       addrname(ifp->int_addr,ifp->int_mask,1),
				       ((ifp->int_if_flags & IFF_POINTOPOINT)
					? "-->" : ""),
				       ((ifp->int_if_flags & IFF_POINTOPOINT)
					? naddr_ntoa(ifp->int_dstaddr) : ""));
			}
			ifp->int_state |= IS_DUP;
			continue;
		}

		if (0 == (ifs.int_if_flags & (IFF_POINTOPOINT | IFF_BROADCAST | IFF_LOOPBACK))) {
			trace_act("%s is neither broadcast, point-to-point,"
				  " nor loopback",
				  ifs.int_name);
			if (!(ifs.int_state & IFF_MULTICAST))
				ifs.int_state |= IS_NO_RDISC;
		}


		/* It is new and ok.   Add it to the list of interfaces
		 */
		ifp = (struct interface *)rtmalloc(sizeof(*ifp), "ifinit ifp");
		memcpy(ifp, &ifs, sizeof(*ifp));
		get_parms(ifp);
		if_link(ifp);
		trace_if("Add", ifp);

		/* Notice likely bad netmask.
		 */
		if (!(prev_complaints & COMP_NETMASK)
		    && !(ifp->int_if_flags & IFF_POINTOPOINT)
		    && ifp->int_addr != RIP_DEFAULT) {
			LIST_FOREACH(ifp1, &ifnet, int_list) {
				if (ifp1->int_mask == ifp->int_mask)
					continue;
				if (ifp1->int_if_flags & IFF_POINTOPOINT)
					continue;
				if (ifp1->int_dstaddr == RIP_DEFAULT)
					continue;
				/* ignore aliases on the right network */
				if (!strcmp(ifp->int_name, ifp1->int_name))
					continue;
				if (on_net(ifp->int_dstaddr,
					   ifp1->int_net, ifp1->int_mask)
				    || on_net(ifp1->int_dstaddr,
					      ifp->int_net, ifp->int_mask)) {
					msglog("possible netmask problem"
					       " between %s:%s and %s:%s",
					       ifp->int_name,
					       addrname(htonl(ifp->int_net),
							ifp->int_mask, 1),
					       ifp1->int_name,
					       addrname(htonl(ifp1->int_net),
							ifp1->int_mask, 1));
					complaints |= COMP_NETMASK;
				}
			}
		}

		if (!(ifp->int_state & IS_ALIAS)) {
			/* Count the # of directly connected networks.
			 */
			if (!(ifp->int_if_flags & IFF_LOOPBACK))
				tot_interfaces++;
			if (!IS_RIP_OFF(ifp->int_state))
				rip_interfaces++;

			/* turn on router discovery and RIP If needed */
			if_ok_rdisc(ifp);
			rip_on(ifp);
		}
	}

	/* If we are multi-homed and have at least two interfaces
	 * listening to RIP, then output by default.
	 */
	if (!supplier_set && rip_interfaces > 1)
		set_supplier();

	/* If we are multi-homed, optionally advertise a route to
	 * our main address.
	 */
	if ((advertise_mhome && ifp)
	    || (tot_interfaces > 1
		&& mhome
		&& (ifp = ifwithaddr(myaddr, 0, 0)) != NULL
		&& foundloopback)) {
		advertise_mhome = 1;
		rt = rtget(myaddr, HOST_MASK);
		if (rt != NULL) {
			if (rt->rt_ifp != ifp
			    || rt->rt_router != loopaddr) {
				rtdelete(rt);
				rt = NULL;
			} else {
				loop_rts.rts_ifp = ifp;
				loop_rts.rts_metric = 0;
				loop_rts.rts_time = rt->rt_time;
				rtchange(rt, rt->rt_state | RS_MHOME,
					 &loop_rts, 0);
			}
		}
		if (rt == NULL) {
			loop_rts.rts_ifp = ifp;
			loop_rts.rts_metric = 0;
			rtadd(myaddr, HOST_MASK, RS_MHOME, &loop_rts);
		}
	}

	LIST_FOREACH_SAFE(ifp, &ifnet, int_list, ifp1) {
		/* Forget any interfaces that have disappeared.
		 */
		if (!(ifp->int_state & (IS_CHECKED | IS_REMOTE))) {
			trace_act("interface %s has disappeared",
				  ifp->int_name);
			ifdel(ifp);
			continue;
		}

		if ((ifp->int_state & IS_BROKE)
		    && !(ifp->int_state & IS_PASSIVE))
			LIM_SEC(ifinit_timer, now.tv_sec+CHECK_BAD_INTERVAL);

		/* If we ever have a RIPv1 interface, assume we always will.
		 * It might come back if it ever goes away.
		 */
		if (!(ifp->int_state & IS_NO_RIPV1_OUT) && supplier)
			have_ripv1_out = 1;
		if (!(ifp->int_state & IS_NO_RIPV1_IN))
			have_ripv1_in = 1;
	}

	LIST_FOREACH(ifp, &ifnet, int_list) {
		/* Ensure there is always a network route for interfaces,
		 * after any dead interfaces have been deleted, which
		 * might affect routes for point-to-point links.
		 */
		if (!addrouteforif(ifp))
			continue;

		/* Add routes to the local end of point-to-point interfaces
		 * using loopback.
		 */
		if ((ifp->int_if_flags & IFF_POINTOPOINT)
		    && !(ifp->int_state & IS_REMOTE)
		    && foundloopback) {
			/* Delete any routes to the network address through
			 * foreign routers. Remove even static routes.
			 */
			del_static(ifp->int_addr, HOST_MASK, 0, 0);
			rt = rtget(ifp->int_addr, HOST_MASK);
			if (rt != NULL && rt->rt_router != loopaddr) {
				rtdelete(rt);
				rt = NULL;
			}
			if (rt != NULL) {
				if (!(rt->rt_state & RS_LOCAL)
				    || rt->rt_metric > ifp->int_metric) {
					ifp1 = ifp;
				} else {
					ifp1 = rt->rt_ifp;
				}
				loop_rts.rts_ifp = ifp1;
				loop_rts.rts_metric = 0;
				loop_rts.rts_time = rt->rt_time;
				rtchange(rt, ((rt->rt_state & ~RS_NET_SYN)
					      | (RS_IF|RS_LOCAL)),
					 &loop_rts, 0);
			} else {
				loop_rts.rts_ifp = ifp;
				loop_rts.rts_metric = 0;
				rtadd(ifp->int_addr, HOST_MASK,
				      (RS_IF | RS_LOCAL), &loop_rts);
			}
		}
	}

	/* add the authority routes */
	for (intnetp = intnets; intnetp != NULL;
	    intnetp = intnetp->intnet_next) {
		rt = rtget(intnetp->intnet_addr, intnetp->intnet_mask);
		if (rt != NULL
		    && !(rt->rt_state & RS_NO_NET_SYN)
		    && !(rt->rt_state & RS_NET_INT)) {
			rtdelete(rt);
			rt = NULL;
		}
		if (rt == NULL) {
			loop_rts.rts_ifp = NULL;
			loop_rts.rts_metric = intnetp->intnet_metric-1;
			rtadd(intnetp->intnet_addr, intnetp->intnet_mask,
			      RS_NET_SYN | RS_NET_INT, &loop_rts);
		}
	}

	prev_complaints = complaints;
}


static void
check_net_syn(struct interface *ifp)
{
	struct rt_entry *rt;
	static struct rt_spare new;


	/* Turn on the need to automatically synthesize a network route
	 * for this interface only if we are running RIPv1 on some other
	 * interface that is on a different class-A,B,or C network.
	 */
	if (have_ripv1_out || have_ripv1_in) {
		ifp->int_state |= IS_NEED_NET_SYN;
		rt = rtget(ifp->int_std_addr, ifp->int_std_mask);
		if (rt != NULL
		    && 0 == (rt->rt_state & RS_NO_NET_SYN)
		    && (!(rt->rt_state & RS_NET_SYN)
			|| rt->rt_metric > ifp->int_metric)) {
			rtdelete(rt);
			rt = NULL;
		}
		if (rt == NULL) {
			new.rts_ifp = ifp;
			new.rts_gate = ifp->int_addr;
			new.rts_router = ifp->int_addr;
			new.rts_metric = ifp->int_metric;
			rtadd(ifp->int_std_addr, ifp->int_std_mask,
			      RS_NET_SYN, &new);
		}

	} else {
		ifp->int_state &= ~IS_NEED_NET_SYN;

		rt = rtget(ifp->int_std_addr,
			   ifp->int_std_mask);
		if (rt != NULL
		    && (rt->rt_state & RS_NET_SYN)
		    && rt->rt_ifp == ifp)
			rtbad_sub(rt);
	}
}


/* Add route for interface if not currently installed.
 * Create route to other end if a point-to-point link,
 * otherwise a route to this (sub)network.
 */
static int					/* 0=bad interface */
addrouteforif(struct interface *ifp)
{
	struct rt_entry *rt;
	static struct rt_spare new;
	naddr dst;


	/* skip sick interfaces
	 */
	if (ifp->int_state & IS_BROKE)
		return 0;

	/* If the interface on a subnet, then install a RIPv1 route to
	 * the network as well (unless it is sick).
	 */
	if (ifp->int_state & IS_SUBNET)
		check_net_syn(ifp);

	dst = (0 != (ifp->int_if_flags & (IFF_POINTOPOINT | IFF_LOOPBACK))
	       ? ifp->int_dstaddr
	       : htonl(ifp->int_net));

	new.rts_ifp = ifp;
	new.rts_router = ifp->int_addr;
	new.rts_gate = ifp->int_addr;
	new.rts_metric = ifp->int_metric;
	new.rts_time = now.tv_sec;

	/* If we are going to send packets to the gateway,
	 * it must be reachable using our physical interfaces
	 */
	if ((ifp->int_state & IS_REMOTE)
	    && !(ifp->int_state & IS_EXTERNAL)
	    && !check_remote(ifp))
		return 0;

	/* We are finished if the correct main interface route exists.
	 * The right route must be for the right interface, not synthesized
	 * from a subnet, be a "gateway" or not as appropriate, and so forth.
	 */
	del_static(dst, ifp->int_mask, 0, 0);
	rt = rtget(dst, ifp->int_mask);
	if (rt != NULL) {
		if ((rt->rt_ifp != ifp
		     || rt->rt_router != ifp->int_addr)
		    && (!(ifp->int_state & IS_DUP)
			|| rt->rt_ifp == 0
			|| (rt->rt_ifp->int_state & IS_BROKE))) {
			rtdelete(rt);
			rt = NULL;
		} else {
			rtchange(rt, ((rt->rt_state | RS_IF)
				      & ~(RS_NET_SYN | RS_LOCAL)),
				 &new, 0);
		}
	}
	if (rt == NULL) {
		if (ifp->int_transitions++ > 0)
			trace_act("re-install interface %s",
				  ifp->int_name);

		rtadd(dst, ifp->int_mask, RS_IF, &new);
	}

	return 1;
}
