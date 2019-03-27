/*	$KAME: ifmcstat.c,v 1.48 2006/11/15 05:13:59 itojun Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2007-2009 Bruce Simpson.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/socket.h>
#include <sys/queue.h>
#include <sys/tree.h>

#include <net/if.h>
#include <net/if_types.h>
#include <net/if_dl.h>
#include <net/route.h>

#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/igmp.h>
#include <netinet/if_ether.h>
#include <netinet/igmp_var.h>

#ifdef INET6
#include <netinet/icmp6.h>
#include <netinet6/mld6_var.h>
#endif /* INET6 */

#include <arpa/inet.h>
#include <netdb.h>

#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <ifaddrs.h>
#include <sysexits.h>
#include <unistd.h>

#ifdef KVM
/*
 * Currently the KVM build is broken. To be fixed it requires uncovering
 * large amount of _KERNEL code in include files, and it is also very
 * tentative to internal kernel ABI changes. If anyone wishes to restore
 * it, please move it out of src/usr.sbin to src/tools/tools.
 */
#include <kvm.h>
#include <nlist.h>
#endif

/* XXX: This file currently assumes INET support in the base system. */
#ifndef INET
#define INET
#endif

extern void	printb(const char *, unsigned int, const char *);

union sockunion {
	struct sockaddr_storage	ss;
	struct sockaddr		sa;
	struct sockaddr_dl	sdl;
#ifdef INET
	struct sockaddr_in	sin;
#endif
#ifdef INET6
	struct sockaddr_in6	sin6;
#endif
};
typedef union sockunion sockunion_t;

uint32_t	ifindex = 0;
int		af = AF_UNSPEC;
#ifdef WITH_KVM
int		Kflag = 0;
#endif
int		vflag = 0;

#define	sa_dl_equal(a1, a2)	\
	((((struct sockaddr_dl *)(a1))->sdl_len ==			\
	 ((struct sockaddr_dl *)(a2))->sdl_len) &&			\
	 (bcmp(LLADDR((struct sockaddr_dl *)(a1)),			\
	       LLADDR((struct sockaddr_dl *)(a2)),			\
	       ((struct sockaddr_dl *)(a1))->sdl_alen) == 0))

/*
 * Most of the code in this utility is to support the use of KVM for
 * post-mortem debugging of the multicast code.
 */
#ifdef WITH_KVM

#ifdef INET
static void		if_addrlist(struct ifaddr *);
static struct in_multi *
			in_multientry(struct in_multi *);
#endif /* INET */

#ifdef INET6
static void		if6_addrlist(struct ifaddr *);
static struct in6_multi *
			in6_multientry(struct in6_multi *);
#endif /* INET6 */

static void		kread(u_long, void *, int);
static void		ll_addrlist(struct ifaddr *);

static int		ifmcstat_kvm(const char *kernel, const char *core);

#define	KREAD(addr, buf, type) \
	kread((u_long)addr, (void *)buf, sizeof(type))

kvm_t	*kvmd;
struct	nlist nl[] = {
	{ "_ifnet", 0, 0, 0, 0, },
	{ "", 0, 0, 0, 0, },
};
#define	N_IFNET	0

#endif /* WITH_KVM */

static int		ifmcstat_getifmaddrs(void);
#ifdef INET
static void		in_ifinfo(struct igmp_ifinfo *);
static const char *	inm_mode(u_int mode);
#endif
#ifdef INET6
static void		in6_ifinfo(struct mld_ifinfo *);
static const char *	inet6_n2a(struct in6_addr *, uint32_t);
#endif
int			main(int, char **);

static void
usage()
{

	fprintf(stderr,
	    "usage: ifmcstat [-i interface] [-f address family]"
	    " [-v]"
#ifdef WITH_KVM
	    " [-K] [-M core] [-N system]"
#endif
	    "\n");
	exit(EX_USAGE);
}

static const char *options = "i:f:vM:N:"
#ifdef WITH_KVM
	"K"
#endif
	;

int
main(int argc, char **argv)
{
	int c, error;
#ifdef WITH_KVM
	const char *kernel = NULL;
	const char *core = NULL;
#endif

	while ((c = getopt(argc, argv, options)) != -1) {
		switch (c) {
		case 'i':
			if ((ifindex = if_nametoindex(optarg)) == 0) {
				fprintf(stderr, "%s: unknown interface\n",
				    optarg);
				exit(EX_NOHOST);
			}
			break;

		case 'f':
#ifdef INET
			if (strcmp(optarg, "inet") == 0) {
				af = AF_INET;
				break;
			}
#endif
#ifdef INET6
			if (strcmp(optarg, "inet6") == 0) {
				af = AF_INET6;
				break;
			}
#endif
			if (strcmp(optarg, "link") == 0) {
				af = AF_LINK;
				break;
			}
			fprintf(stderr, "%s: unknown address family\n", optarg);
			exit(EX_USAGE);
			/*NOTREACHED*/
			break;

#ifdef WITH_KVM
		case 'K':
			++Kflag;
			break;
#endif

		case 'v':
			++vflag;
			break;

#ifdef WITH_KVM
		case 'M':
			core = strdup(optarg);
			break;

		case 'N':
			kernel = strdup(optarg);
			break;
#endif

		default:
			usage();
			break;
			/*NOTREACHED*/
		}
	}

	if (af == AF_LINK && vflag)
		usage();

#ifdef WITH_KVM
	if (Kflag)
		error = ifmcstat_kvm(kernel, core);
	/*
	 * If KVM failed, and user did not explicitly specify a core file,
	 * or force KVM backend to be disabled, try the sysctl backend.
	 */
	if (!Kflag || (error != 0 && (core == NULL && kernel == NULL)))
#endif
	error = ifmcstat_getifmaddrs();
	if (error != 0)
		exit(EX_OSERR);

	exit(EX_OK);
	/*NOTREACHED*/
}

#ifdef INET

static void
in_ifinfo(struct igmp_ifinfo *igi)
{

	printf("\t");
	switch (igi->igi_version) {
	case IGMP_VERSION_1:
	case IGMP_VERSION_2:
	case IGMP_VERSION_3:
		printf("igmpv%d", igi->igi_version);
		break;
	default:
		printf("igmpv?(%d)", igi->igi_version);
		break;
	}
	if (igi->igi_flags)
		printb(" flags", igi->igi_flags, "\020\1SILENT\2LOOPBACK");
	if (igi->igi_version == IGMP_VERSION_3) {
		printf(" rv %u qi %u qri %u uri %u",
		    igi->igi_rv, igi->igi_qi, igi->igi_qri, igi->igi_uri);
	}
	if (vflag >= 2) {
		printf(" v1timer %u v2timer %u v3timer %u",
		    igi->igi_v1_timer, igi->igi_v2_timer, igi->igi_v3_timer);
	}
	printf("\n");
}

static const char *inm_modes[] = {
	"undefined",
	"include",
	"exclude",
};

static const char *
inm_mode(u_int mode)
{

	if (mode >= MCAST_UNDEFINED && mode <= MCAST_EXCLUDE)
		return (inm_modes[mode]);
	return (NULL);
}

#endif /* INET */

#ifdef WITH_KVM

static int
ifmcstat_kvm(const char *kernel, const char *core)
{
	char	buf[_POSIX2_LINE_MAX], ifname[IFNAMSIZ];
	struct	ifnet	*ifp, *nifp, ifnet;

	if ((kvmd = kvm_openfiles(kernel, core, NULL, O_RDONLY, buf)) ==
	    NULL) {
		perror("kvm_openfiles");
		return (-1);
	}
	if (kvm_nlist(kvmd, nl) < 0) {
		perror("kvm_nlist");
		return (-1);
	}
	if (nl[N_IFNET].n_value == 0) {
		printf("symbol %s not found\n", nl[N_IFNET].n_name);
		return (-1);
	}
	KREAD(nl[N_IFNET].n_value, &ifp, struct ifnet *);
	while (ifp) {
		KREAD(ifp, &ifnet, struct ifnet);
		nifp = ifnet.if_link.tqe_next;
		if (ifindex && ifindex != ifnet.if_index)
			goto next;
	
		printf("%s:\n", if_indextoname(ifnet.if_index, ifname));
#ifdef INET
		if_addrlist(TAILQ_FIRST(&ifnet.if_addrhead));
#endif
#ifdef INET6
		if6_addrlist(TAILQ_FIRST(&ifnet.if_addrhead));
#endif
		if (vflag)
			ll_addrlist(TAILQ_FIRST(&ifnet.if_addrhead));
	next:
		ifp = nifp;
	}

	return (0);
}

static void
kread(u_long addr, void *buf, int len)
{

	if (kvm_read(kvmd, addr, buf, len) != len) {
		perror("kvm_read");
		exit(EX_OSERR);
	}
}

static void
ll_addrlist(struct ifaddr *ifap)
{
	char addrbuf[NI_MAXHOST];
	struct ifaddr ifa;
	struct sockaddr sa;
	struct sockaddr_dl sdl;
	struct ifaddr *ifap0;

	if (af && af != AF_LINK)
		return;

	ifap0 = ifap;
	while (ifap) {
		KREAD(ifap, &ifa, struct ifaddr);
		if (ifa.ifa_addr == NULL)
			goto nextifap;
		KREAD(ifa.ifa_addr, &sa, struct sockaddr);
		if (sa.sa_family != PF_LINK)
			goto nextifap;
		KREAD(ifa.ifa_addr, &sdl, struct sockaddr_dl);
		if (sdl.sdl_alen == 0)
			goto nextifap;
		addrbuf[0] = '\0';
		getnameinfo((struct sockaddr *)&sdl, sdl.sdl_len,
		    addrbuf, sizeof(addrbuf), NULL, 0, NI_NUMERICHOST);
		printf("\tlink %s\n", addrbuf);
	nextifap:
		ifap = ifa.ifa_link.tqe_next;
	}
	if (ifap0) {
		struct ifnet ifnet;
		struct ifmultiaddr ifm, *ifmp = 0;

		KREAD(ifap0, &ifa, struct ifaddr);
		KREAD(ifa.ifa_ifp, &ifnet, struct ifnet);
		if (TAILQ_FIRST(&ifnet.if_multiaddrs))
			ifmp = TAILQ_FIRST(&ifnet.if_multiaddrs);
		while (ifmp) {
			KREAD(ifmp, &ifm, struct ifmultiaddr);
			if (ifm.ifma_addr == NULL)
				goto nextmulti;
			KREAD(ifm.ifma_addr, &sa, struct sockaddr);
			if (sa.sa_family != AF_LINK)
				goto nextmulti;
			KREAD(ifm.ifma_addr, &sdl, struct sockaddr_dl);
			addrbuf[0] = '\0';
			getnameinfo((struct sockaddr *)&sdl,
			    sdl.sdl_len, addrbuf, sizeof(addrbuf),
			    NULL, 0, NI_NUMERICHOST);
			printf("\t\tgroup %s refcnt %d\n",
			    addrbuf, ifm.ifma_refcount);
		nextmulti:
			ifmp = TAILQ_NEXT(&ifm, ifma_link);
		}
	}
}

#ifdef INET6

static void
if6_addrlist(struct ifaddr *ifap)
{
	struct ifnet ifnet;
	struct ifaddr ifa;
	struct sockaddr sa;
	struct in6_ifaddr if6a;
	struct ifaddr *ifap0;

	if (af && af != AF_INET6)
		return;
	ifap0 = ifap;
	while (ifap) {
		KREAD(ifap, &ifa, struct ifaddr);
		if (ifa.ifa_addr == NULL)
			goto nextifap;
		KREAD(ifa.ifa_addr, &sa, struct sockaddr);
		if (sa.sa_family != PF_INET6)
			goto nextifap;
		KREAD(ifap, &if6a, struct in6_ifaddr);
		printf("\tinet6 %s\n", inet6_n2a(&if6a.ia_addr.sin6_addr,
		    if6a.ia_addr.sin6_scope_id));
		/*
		 * Print per-link MLD information, if available.
		 */
		if (ifa.ifa_ifp != NULL) {
			struct in6_ifextra ie;
			struct mld_ifinfo mli;

			KREAD(ifa.ifa_ifp, &ifnet, struct ifnet);
			KREAD(ifnet.if_afdata[AF_INET6], &ie,
			    struct in6_ifextra);
			if (ie.mld_ifinfo != NULL) {
				KREAD(ie.mld_ifinfo, &mli, struct mld_ifinfo);
				in6_ifinfo(&mli);
			}
		}
	nextifap:
		ifap = ifa.ifa_link.tqe_next;
	}
	if (ifap0) {
		struct ifnet ifnet;
		struct ifmultiaddr ifm, *ifmp = 0;
		struct sockaddr_dl sdl;

		KREAD(ifap0, &ifa, struct ifaddr);
		KREAD(ifa.ifa_ifp, &ifnet, struct ifnet);
		if (TAILQ_FIRST(&ifnet.if_multiaddrs))
			ifmp = TAILQ_FIRST(&ifnet.if_multiaddrs);
		while (ifmp) {
			KREAD(ifmp, &ifm, struct ifmultiaddr);
			if (ifm.ifma_addr == NULL)
				goto nextmulti;
			KREAD(ifm.ifma_addr, &sa, struct sockaddr);
			if (sa.sa_family != AF_INET6)
				goto nextmulti;
			(void)in6_multientry((struct in6_multi *)
					     ifm.ifma_protospec);
			if (ifm.ifma_lladdr == 0)
				goto nextmulti;
			KREAD(ifm.ifma_lladdr, &sdl, struct sockaddr_dl);
			printf("\t\t\tmcast-macaddr %s refcnt %d\n",
			       ether_ntoa((struct ether_addr *)LLADDR(&sdl)),
			       ifm.ifma_refcount);
		    nextmulti:
			ifmp = TAILQ_NEXT(&ifm, ifma_link);
		}
	}
}

static struct in6_multi *
in6_multientry(struct in6_multi *mc)
{
	struct in6_multi multi;

	KREAD(mc, &multi, struct in6_multi);
	printf("\t\tgroup %s", inet6_n2a(&multi.in6m_addr, 0));
	printf(" refcnt %u\n", multi.in6m_refcount);

	return (multi.in6m_entry.le_next);
}

#endif /* INET6 */

#ifdef INET

static void
if_addrlist(struct ifaddr *ifap)
{
	struct ifaddr ifa;
	struct ifnet ifnet;
	struct sockaddr sa;
	struct in_ifaddr ia;
	struct ifaddr *ifap0;

	if (af && af != AF_INET)
		return;
	ifap0 = ifap;
	while (ifap) {
		KREAD(ifap, &ifa, struct ifaddr);
		if (ifa.ifa_addr == NULL)
			goto nextifap;
		KREAD(ifa.ifa_addr, &sa, struct sockaddr);
		if (sa.sa_family != PF_INET)
			goto nextifap;
		KREAD(ifap, &ia, struct in_ifaddr);
		printf("\tinet %s\n", inet_ntoa(ia.ia_addr.sin_addr));
		/*
		 * Print per-link IGMP information, if available.
		 */
		if (ifa.ifa_ifp != NULL) {
			struct in_ifinfo ii;
			struct igmp_ifinfo igi;

			KREAD(ifa.ifa_ifp, &ifnet, struct ifnet);
			KREAD(ifnet.if_afdata[AF_INET], &ii, struct in_ifinfo);
			if (ii.ii_igmp != NULL) {
				KREAD(ii.ii_igmp, &igi, struct igmp_ifinfo);
				in_ifinfo(&igi);
			}
		}
	nextifap:
		ifap = ifa.ifa_link.tqe_next;
	}
	if (ifap0) {
		struct ifmultiaddr ifm, *ifmp = 0;
		struct sockaddr_dl sdl;

		KREAD(ifap0, &ifa, struct ifaddr);
		KREAD(ifa.ifa_ifp, &ifnet, struct ifnet);
		if (TAILQ_FIRST(&ifnet.if_multiaddrs))
			ifmp = TAILQ_FIRST(&ifnet.if_multiaddrs);
		while (ifmp) {
			KREAD(ifmp, &ifm, struct ifmultiaddr);
			if (ifm.ifma_addr == NULL)
				goto nextmulti;
			KREAD(ifm.ifma_addr, &sa, struct sockaddr);
			if (sa.sa_family != AF_INET)
				goto nextmulti;
			(void)in_multientry((struct in_multi *)
					    ifm.ifma_protospec);
			if (ifm.ifma_lladdr == 0)
				goto nextmulti;
			KREAD(ifm.ifma_lladdr, &sdl, struct sockaddr_dl);
			printf("\t\t\tmcast-macaddr %s refcnt %d\n",
			       ether_ntoa((struct ether_addr *)LLADDR(&sdl)),
			       ifm.ifma_refcount);
		    nextmulti:
			ifmp = TAILQ_NEXT(&ifm, ifma_link);
		}
	}
}

static const char *inm_states[] = {
	"not-member",
	"silent",
	"idle",
	"lazy",
	"sleeping",
	"awakening",
	"query-pending",
	"sg-query-pending",
	"leaving"
};

static const char *
inm_state(u_int state)
{

	if (state >= IGMP_NOT_MEMBER && state <= IGMP_LEAVING_MEMBER)
		return (inm_states[state]);
	return (NULL);
}

#if 0
static struct ip_msource *
ims_min_kvm(struct in_multi *pinm)
{
	struct ip_msource ims0;
	struct ip_msource *tmp, *parent;

	parent = NULL;
	tmp = RB_ROOT(&pinm->inm_srcs);
	while (tmp) {
		parent = tmp;
		KREAD(tmp, &ims0, struct ip_msource);
		tmp = RB_LEFT(&ims0, ims_link);
	}
	return (parent); /* kva */
}

/* XXX This routine is buggy. See RB_NEXT in sys/tree.h. */
static struct ip_msource *
ims_next_kvm(struct ip_msource *ims)
{
	struct ip_msource ims0, ims1;
	struct ip_msource *tmp;

	KREAD(ims, &ims0, struct ip_msource);
	if (RB_RIGHT(&ims0, ims_link)) {
		ims = RB_RIGHT(&ims0, ims_link);
		KREAD(ims, &ims1, struct ip_msource);
		while ((tmp = RB_LEFT(&ims1, ims_link))) {
			KREAD(tmp, &ims0, struct ip_msource);
			ims = RB_LEFT(&ims0, ims_link);
		}
	} else {
		tmp = RB_PARENT(&ims0, ims_link);
		if (tmp) {
			KREAD(tmp, &ims1, struct ip_msource);
			if (ims == RB_LEFT(&ims1, ims_link))
				ims = tmp;
		} else {
			while ((tmp = RB_PARENT(&ims0, ims_link))) {
				KREAD(tmp, &ims1, struct ip_msource);
				if (ims == RB_RIGHT(&ims1, ims_link)) {
					ims = tmp;
					KREAD(ims, &ims0, struct ip_msource);
				} else
					break;
			}
			ims = RB_PARENT(&ims0, ims_link);
		}
	}
	return (ims); /* kva */
}

static void
inm_print_sources_kvm(struct in_multi *pinm)
{
	struct ip_msource ims0;
	struct ip_msource *ims;
	struct in_addr src;
	int cnt;
	uint8_t fmode;

	cnt = 0;
	fmode = pinm->inm_st[1].iss_fmode;
	if (fmode == MCAST_UNDEFINED)
		return;
	for (ims = ims_min_kvm(pinm); ims != NULL; ims = ims_next_kvm(ims)) {
		if (cnt == 0)
			printf(" srcs ");
		KREAD(ims, &ims0, struct ip_msource);
		/* Only print sources in-mode at t1. */
		if (fmode != ims_get_mode(pinm, ims, 1))
			continue;
		src.s_addr = htonl(ims0.ims_haddr);
		printf("%s%s", (cnt++ == 0 ? "" : ","), inet_ntoa(src));
	}
}
#endif

static struct in_multi *
in_multientry(struct in_multi *pinm)
{
	struct in_multi inm;
	const char *state, *mode;

	KREAD(pinm, &inm, struct in_multi);
	printf("\t\tgroup %s", inet_ntoa(inm.inm_addr));
	printf(" refcnt %u", inm.inm_refcount);

	state = inm_state(inm.inm_state);
	if (state)
		printf(" state %s", state);
	else
		printf(" state (%d)", inm.inm_state);

	mode = inm_mode(inm.inm_st[1].iss_fmode);
	if (mode)
		printf(" mode %s", mode);
	else
		printf(" mode (%d)", inm.inm_st[1].iss_fmode);

	if (vflag >= 2) {
		printf(" asm %u ex %u in %u rec %u",
		    (u_int)inm.inm_st[1].iss_asm,
		    (u_int)inm.inm_st[1].iss_ex,
		    (u_int)inm.inm_st[1].iss_in,
		    (u_int)inm.inm_st[1].iss_rec);
	}

#if 0
	/* Buggy. */
	if (vflag)
		inm_print_sources_kvm(&inm);
#endif

	printf("\n");
	return (NULL);
}

#endif /* INET */

#endif /* WITH_KVM */

#ifdef INET6

static void
in6_ifinfo(struct mld_ifinfo *mli)
{

	printf("\t");
	switch (mli->mli_version) {
	case MLD_VERSION_1:
	case MLD_VERSION_2:
		printf("mldv%d", mli->mli_version);
		break;
	default:
		printf("mldv?(%d)", mli->mli_version);
		break;
	}
	if (mli->mli_flags)
		printb(" flags", mli->mli_flags, "\020\1SILENT\2USEALLOW");
	if (mli->mli_version == MLD_VERSION_2) {
		printf(" rv %u qi %u qri %u uri %u",
		    mli->mli_rv, mli->mli_qi, mli->mli_qri, mli->mli_uri);
	}
	if (vflag >= 2) {
		printf(" v1timer %u v2timer %u", mli->mli_v1_timer,
		   mli->mli_v2_timer);
	}
	printf("\n");
}

static const char *
inet6_n2a(struct in6_addr *p, uint32_t scope_id)
{
	static char buf[NI_MAXHOST];
	struct sockaddr_in6 sin6;
	const int niflags = NI_NUMERICHOST;

	memset(&sin6, 0, sizeof(sin6));
	sin6.sin6_family = AF_INET6;
	sin6.sin6_len = sizeof(struct sockaddr_in6);
	sin6.sin6_addr = *p;
	sin6.sin6_scope_id = scope_id;
	if (getnameinfo((struct sockaddr *)&sin6, sin6.sin6_len,
	    buf, sizeof(buf), NULL, 0, niflags) == 0) {
		return (buf);
	} else {
		return ("(invalid)");
	}
}
#endif /* INET6 */

#ifdef INET
/*
 * Retrieve per-group source filter mode and lists via sysctl.
 */
static void
inm_print_sources_sysctl(uint32_t ifindex, struct in_addr gina)
{
#define	MAX_SYSCTL_TRY	5
	int mib[7];
	int ntry = 0;
	size_t mibsize;
	size_t len;
	size_t needed;
	size_t cnt;
	int i;
	char *buf;
	struct in_addr *pina;
	uint32_t *p;
	uint32_t fmode;
	const char *modestr;

	mibsize = nitems(mib);
	if (sysctlnametomib("net.inet.ip.mcast.filters", mib, &mibsize) == -1) {
		perror("sysctlnametomib");
		return;
	}

	needed = 0;
	mib[5] = ifindex;
	mib[6] = gina.s_addr;	/* 32 bits wide */
	mibsize = nitems(mib);
	do {
		if (sysctl(mib, mibsize, NULL, &needed, NULL, 0) == -1) {
			perror("sysctl net.inet.ip.mcast.filters");
			return;
		}
		if ((buf = malloc(needed)) == NULL) {
			perror("malloc");
			return;
		}
		if (sysctl(mib, mibsize, buf, &needed, NULL, 0) == -1) {
			if (errno != ENOMEM || ++ntry >= MAX_SYSCTL_TRY) {
				perror("sysctl");
				goto out_free;
			}
			free(buf);
			buf = NULL;
		} 
	} while (buf == NULL);

	len = needed;
	if (len < sizeof(uint32_t)) {
		perror("sysctl");
		goto out_free;
	}

	p = (uint32_t *)buf;
	fmode = *p++;
	len -= sizeof(uint32_t);

	modestr = inm_mode(fmode);
	if (modestr)
		printf(" mode %s", modestr);
	else
		printf(" mode (%u)", fmode);

	if (vflag == 0)
		goto out_free;

	cnt = len / sizeof(struct in_addr);
	pina = (struct in_addr *)p;

	for (i = 0; i < cnt; i++) {
		if (i == 0)
			printf(" srcs ");
		fprintf(stdout, "%s%s", (i == 0 ? "" : ","),
		    inet_ntoa(*pina++));
		len -= sizeof(struct in_addr);
	}
	if (len > 0) {
		fprintf(stderr, "warning: %u trailing bytes from %s\n",
		    (unsigned int)len, "net.inet.ip.mcast.filters");
	}

out_free:
	free(buf);
#undef	MAX_SYSCTL_TRY
}

#endif /* INET */

#ifdef INET6
/*
 * Retrieve MLD per-group source filter mode and lists via sysctl.
 *
 * Note: The 128-bit IPv6 group address needs to be segmented into
 * 32-bit pieces for marshaling to sysctl. So the MIB name ends
 * up looking like this:
 *  a.b.c.d.e.ifindex.g[0].g[1].g[2].g[3]
 * Assumes that pgroup originated from the kernel, so its components
 * are already in network-byte order.
 */
static void
in6m_print_sources_sysctl(uint32_t ifindex, struct in6_addr *pgroup)
{
#define	MAX_SYSCTL_TRY	5
	char addrbuf[INET6_ADDRSTRLEN];
	int mib[10];
	int ntry = 0;
	int *pi;
	size_t mibsize;
	size_t len;
	size_t needed;
	size_t cnt;
	int i;
	char *buf;
	struct in6_addr *pina;
	uint32_t *p;
	uint32_t fmode;
	const char *modestr;

	mibsize = nitems(mib);
	if (sysctlnametomib("net.inet6.ip6.mcast.filters", mib,
	    &mibsize) == -1) {
		perror("sysctlnametomib");
		return;
	}

	needed = 0;
	mib[5] = ifindex;
	pi = (int *)pgroup;
	for (i = 0; i < 4; i++)
		mib[6 + i] = *pi++;

	mibsize = nitems(mib);
	do {
		if (sysctl(mib, mibsize, NULL, &needed, NULL, 0) == -1) {
			perror("sysctl net.inet6.ip6.mcast.filters");
			return;
		}
		if ((buf = malloc(needed)) == NULL) {
			perror("malloc");
			return;
		}
		if (sysctl(mib, mibsize, buf, &needed, NULL, 0) == -1) {
			if (errno != ENOMEM || ++ntry >= MAX_SYSCTL_TRY) {
				perror("sysctl");
				goto out_free;
			}
			free(buf);
			buf = NULL;
		} 
	} while (buf == NULL);

	len = needed;
	if (len < sizeof(uint32_t)) {
		perror("sysctl");
		goto out_free;
	}

	p = (uint32_t *)buf;
	fmode = *p++;
	len -= sizeof(uint32_t);

	modestr = inm_mode(fmode);
	if (modestr)
		printf(" mode %s", modestr);
	else
		printf(" mode (%u)", fmode);

	if (vflag == 0)
		goto out_free;

	cnt = len / sizeof(struct in6_addr);
	pina = (struct in6_addr *)p;

	for (i = 0; i < cnt; i++) {
		if (i == 0)
			printf(" srcs ");
		inet_ntop(AF_INET6, (const char *)pina++, addrbuf,
		    INET6_ADDRSTRLEN);
		fprintf(stdout, "%s%s", (i == 0 ? "" : ","), addrbuf);
		len -= sizeof(struct in6_addr);
	}
	if (len > 0) {
		fprintf(stderr, "warning: %u trailing bytes from %s\n",
		    (unsigned int)len, "net.inet6.ip6.mcast.filters");
	}

out_free:
	free(buf);
#undef	MAX_SYSCTL_TRY
}
#endif /* INET6 */

static int
ifmcstat_getifmaddrs(void)
{
	char			 thisifname[IFNAMSIZ];
	char			 addrbuf[NI_MAXHOST];
	struct ifaddrs		*ifap, *ifa;
	struct ifmaddrs		*ifmap, *ifma;
	sockunion_t		 lastifasa;
	sockunion_t		*psa, *pgsa, *pllsa, *pifasa;
	char			*pcolon;
	char			*pafname;
	uint32_t		 lastifindex, thisifindex;
	int			 error;

	error = 0;
	ifap = NULL;
	ifmap = NULL;
	lastifindex = 0;
	thisifindex = 0;
	lastifasa.ss.ss_family = AF_UNSPEC;

	if (getifaddrs(&ifap) != 0) {
		warn("getifmaddrs");
		return (-1);
	}

	if (getifmaddrs(&ifmap) != 0) {
		warn("getifmaddrs");
		error = -1;
		goto out;
	}

	for (ifma = ifmap; ifma; ifma = ifma->ifma_next) {
		error = 0;
		if (ifma->ifma_name == NULL || ifma->ifma_addr == NULL)
			continue;

		psa = (sockunion_t *)ifma->ifma_name;
		if (psa->sa.sa_family != AF_LINK) {
			fprintf(stderr,
			    "WARNING: Kernel returned invalid data.\n");
			error = -1;
			break;
		}

		/* Filter on interface name. */
		thisifindex = psa->sdl.sdl_index;
		if (ifindex != 0 && thisifindex != ifindex)
			continue;

		/* Filter on address family. */
		pgsa = (sockunion_t *)ifma->ifma_addr;
		if (af != 0 && pgsa->sa.sa_family != af)
			continue;

		strlcpy(thisifname, link_ntoa(&psa->sdl), IFNAMSIZ);
		pcolon = strchr(thisifname, ':');
		if (pcolon)
			*pcolon = '\0';

		/* Only print the banner for the first ifmaddrs entry. */
		if (lastifindex == 0 || lastifindex != thisifindex) {
			lastifindex = thisifindex;
			fprintf(stdout, "%s:\n", thisifname);
		}

		/*
		 * Currently, multicast joins only take place on the
		 * primary IPv4 address, and only on the link-local IPv6
		 * address, as per IGMPv2/3 and MLDv1/2 semantics.
		 * Therefore, we only look up the primary address on
		 * the first pass.
		 */
		pifasa = NULL;
		for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
			if ((strcmp(ifa->ifa_name, thisifname) != 0) ||
			    (ifa->ifa_addr == NULL) ||
			    (ifa->ifa_addr->sa_family != pgsa->sa.sa_family))
				continue;
			/*
			 * For AF_INET6 only the link-local address should
			 * be returned. If built without IPv6 support,
			 * skip this address entirely.
			 */
			pifasa = (sockunion_t *)ifa->ifa_addr;
			if (pifasa->sa.sa_family == AF_INET6
#ifdef INET6
			    && !IN6_IS_ADDR_LINKLOCAL(&pifasa->sin6.sin6_addr)
#endif
			) {
				pifasa = NULL;
				continue;
			}
			break;
		}
		if (pifasa == NULL)
			continue;	/* primary address not found */

		if (!vflag && pifasa->sa.sa_family == AF_LINK)
			continue;

		/* Parse and print primary address, if not already printed. */
		if (lastifasa.ss.ss_family == AF_UNSPEC ||
		    ((lastifasa.ss.ss_family == AF_LINK &&
		      !sa_dl_equal(&lastifasa.sa, &pifasa->sa)) ||
		     !sa_equal(&lastifasa.sa, &pifasa->sa))) {

			switch (pifasa->sa.sa_family) {
			case AF_INET:
				pafname = "inet";
				break;
			case AF_INET6:
				pafname = "inet6";
				break;
			case AF_LINK:
				pafname = "link";
				break;
			default:
				pafname = "unknown";
				break;
			}

			switch (pifasa->sa.sa_family) {
			case AF_INET6:
#ifdef INET6
			{
				const char *p =
				    inet6_n2a(&pifasa->sin6.sin6_addr,
					pifasa->sin6.sin6_scope_id);
				strlcpy(addrbuf, p, sizeof(addrbuf));
				break;
			}
#else
			/* FALLTHROUGH */
#endif
			case AF_INET:
			case AF_LINK:
				error = getnameinfo(&pifasa->sa,
				    pifasa->sa.sa_len,
				    addrbuf, sizeof(addrbuf), NULL, 0,
				    NI_NUMERICHOST);
				if (error)
					perror("getnameinfo");
				break;
			default:
				addrbuf[0] = '\0';
				break;
			}

			fprintf(stdout, "\t%s %s", pafname, addrbuf);
#ifdef INET6
			if (pifasa->sa.sa_family == AF_INET6 &&
			    pifasa->sin6.sin6_scope_id)
				fprintf(stdout, " scopeid 0x%x",
				    pifasa->sin6.sin6_scope_id);
#endif
			fprintf(stdout, "\n");
#ifdef INET
			/*
			 * Print per-link IGMP information, if available.
			 */
			if (pifasa->sa.sa_family == AF_INET) {
				struct igmp_ifinfo igi;
				size_t mibsize, len;
				int mib[5];

				mibsize = nitems(mib);
				if (sysctlnametomib("net.inet.igmp.ifinfo",
				    mib, &mibsize) == -1) {
					perror("sysctlnametomib");
					goto next_ifnet;
				}
				mib[mibsize] = thisifindex;
				len = sizeof(struct igmp_ifinfo);
				if (sysctl(mib, mibsize + 1, &igi, &len, NULL,
				    0) == -1) {
					perror("sysctl net.inet.igmp.ifinfo");
					goto next_ifnet;
				}
				in_ifinfo(&igi);
			}
#endif /* INET */
#ifdef INET6
			/*
			 * Print per-link MLD information, if available.
			 */
			if (pifasa->sa.sa_family == AF_INET6) {
				struct mld_ifinfo mli;
				size_t mibsize, len;
				int mib[5];

				mibsize = nitems(mib);
				if (sysctlnametomib("net.inet6.mld.ifinfo",
				    mib, &mibsize) == -1) {
					perror("sysctlnametomib");
					goto next_ifnet;
				}
				mib[mibsize] = thisifindex;
				len = sizeof(struct mld_ifinfo);
				if (sysctl(mib, mibsize + 1, &mli, &len, NULL,
				    0) == -1) {
					perror("sysctl net.inet6.mld.ifinfo");
					goto next_ifnet;
				}
				in6_ifinfo(&mli);
			}
#endif /* INET6 */
#if defined(INET) || defined(INET6)
next_ifnet:
#endif
			lastifasa = *pifasa;
		}

		/* Print this group address. */
#ifdef INET6
		if (pgsa->sa.sa_family == AF_INET6) {
			const char *p = inet6_n2a(&pgsa->sin6.sin6_addr,
			    pgsa->sin6.sin6_scope_id);
			strlcpy(addrbuf, p, sizeof(addrbuf));
		} else
#endif
		{
			error = getnameinfo(&pgsa->sa, pgsa->sa.sa_len,
			    addrbuf, sizeof(addrbuf), NULL, 0, NI_NUMERICHOST);
			if (error)
				perror("getnameinfo");
		}

		fprintf(stdout, "\t\tgroup %s", addrbuf);
#ifdef INET6
		if (pgsa->sa.sa_family == AF_INET6 &&
		    pgsa->sin6.sin6_scope_id)
			fprintf(stdout, " scopeid 0x%x",
			    pgsa->sin6.sin6_scope_id);
#endif
#ifdef INET
		if (pgsa->sa.sa_family == AF_INET) {
			inm_print_sources_sysctl(thisifindex,
			    pgsa->sin.sin_addr);
		}
#endif
#ifdef INET6
		if (pgsa->sa.sa_family == AF_INET6) {
			in6m_print_sources_sysctl(thisifindex,
			    &pgsa->sin6.sin6_addr);
		}
#endif
		fprintf(stdout, "\n");

		/* Link-layer mapping, if present. */
		pllsa = (sockunion_t *)ifma->ifma_lladdr;
		if (pllsa != NULL) {
			error = getnameinfo(&pllsa->sa, pllsa->sa.sa_len,
			    addrbuf, sizeof(addrbuf), NULL, 0, NI_NUMERICHOST);
			fprintf(stdout, "\t\t\tmcast-macaddr %s\n", addrbuf);
		}
	}
out:
	if (ifmap != NULL)
		freeifmaddrs(ifmap);
	if (ifap != NULL)
		freeifaddrs(ifap);

	return (error);
}
