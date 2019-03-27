/*	$FreeBSD$	*/
/*	$KAME: rtadvd.h,v 1.26 2003/08/05 12:34:23 itojun Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 1998 WIDE Project.
 * Copyright (C) 2011 Hiroki Sato <hrs@FreeBSD.org>
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

#define	ELM_MALLOC(p,error_action)					\
	do {								\
		p = malloc(sizeof(*p));					\
		if (p == NULL) {					\
			syslog(LOG_ERR, "<%s> malloc failed: %s",	\
			    __func__, strerror(errno));			\
			error_action;					\
		}							\
		memset(p, 0, sizeof(*p));				\
	} while(0)

#define IN6ADDR_LINKLOCAL_ALLNODES_INIT				\
	{{{ 0xff, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	\
	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 }}}

#define IN6ADDR_LINKLOCAL_ALLROUTERS_INIT			\
	{{{ 0xff, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	\
	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02 }}}

#define IN6ADDR_SITELOCAL_ALLROUTERS_INIT			\
	{{{ 0xff, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	\
	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02 }}}

extern struct sockaddr_in6 sin6_linklocal_allnodes;
extern struct sockaddr_in6 sin6_linklocal_allrouters;
extern struct sockaddr_in6 sin6_sitelocal_allrouters;

/*
 * RFC 3542 API deprecates IPV6_PKTINFO in favor of
 * IPV6_RECVPKTINFO
 */
#ifndef IPV6_RECVPKTINFO
#ifdef IPV6_PKTINFO
#define IPV6_RECVPKTINFO	IPV6_PKTINFO
#endif
#endif

/*
 * RFC 3542 API deprecates IPV6_HOPLIMIT in favor of
 * IPV6_RECVHOPLIMIT
 */
#ifndef IPV6_RECVHOPLIMIT
#ifdef IPV6_HOPLIMIT
#define IPV6_RECVHOPLIMIT	IPV6_HOPLIMIT
#endif
#endif

/* protocol constants and default values */
#define DEF_MAXRTRADVINTERVAL 600
#define DEF_ADVLINKMTU 0
#define DEF_ADVREACHABLETIME 0
#define DEF_ADVRETRANSTIMER 0
#define DEF_ADVCURHOPLIMIT 64
#define DEF_ADVVALIDLIFETIME 2592000
#define DEF_ADVPREFERREDLIFETIME 604800

#define MAXROUTERLIFETIME 9000
#define MIN_MAXINTERVAL 4
#define MAX_MAXINTERVAL 1800
#define MIN_MININTERVAL 3
#define MAXREACHABLETIME 3600000

#define MAX_INITIAL_RTR_ADVERT_INTERVAL  16
#define MAX_INITIAL_RTR_ADVERTISEMENTS    3
#define MAX_FINAL_RTR_ADVERTISEMENTS      3
#define MIN_DELAY_BETWEEN_RAS             3
#define MAX_RA_DELAY_TIME                 500000 /* usec */

#define PREFIX_FROM_KERNEL 1
#define PREFIX_FROM_CONFIG 2
#define PREFIX_FROM_DYNAMIC 3

struct prefix {
	TAILQ_ENTRY(prefix)	pfx_next;

	struct rainfo *pfx_rainfo;	/* back pointer to the interface */
	/*
	 * Expiration timer.  This is used when a prefix derived from
	 * the kernel is deleted.
	 */
	struct rtadvd_timer *pfx_timer;

	uint32_t	pfx_validlifetime;	/* AdvValidLifetime */
	uint32_t       	pfx_vltimeexpire;	/* Expiration of vltime */
	uint32_t	pfx_preflifetime;	/* AdvPreferredLifetime */
	uint32_t	pfx_pltimeexpire;	/* Expiration of pltime */
	int		pfx_onlinkflg;		/* bool: AdvOnLinkFlag */
	int		pfx_autoconfflg;	/* bool: AdvAutonomousFlag */
	int		pfx_prefixlen;
	int		pfx_origin;		/* From kernel or config */

	struct in6_addr	pfx_prefix;
};

struct rtinfo {
	TAILQ_ENTRY(rtinfo)	rti_next;

	uint32_t	rti_ltime;	/* route lifetime */
	int		rti_rtpref;	/* route preference */
	int		rti_prefixlen;
	struct in6_addr	rti_prefix;
};

struct rdnss_addr {
	TAILQ_ENTRY(rdnss_addr)	ra_next;

	struct in6_addr ra_dns;	/* DNS server entry */
};

struct rdnss {
	TAILQ_ENTRY(rdnss) rd_next;

	TAILQ_HEAD(, rdnss_addr) rd_list;	/* list of DNS servers */
	uint32_t rd_ltime;	/* number of seconds valid */
};

/*
 * The maximum length of a domain name in a DNS search list is calculated
 * by a domain name + length fields per 63 octets + a zero octet at
 * the tail and adding 8 octet boundary padding.
 */
#define _DNAME_LABELENC_MAXLEN \
	(NI_MAXHOST + (NI_MAXHOST / 64 + 1) + 1)

#define DNAME_LABELENC_MAXLEN \
	(_DNAME_LABELENC_MAXLEN + 8 - _DNAME_LABELENC_MAXLEN % 8)

struct dnssl_addr {
	TAILQ_ENTRY(dnssl_addr)	da_next;

	int da_len;				/* length of entry */
	char da_dom[DNAME_LABELENC_MAXLEN];	/* search domain name entry */
};

struct dnssl {
	TAILQ_ENTRY(dnssl)	dn_next;

	TAILQ_HEAD(, dnssl_addr) dn_list;	/* list of search domains */
	uint32_t dn_ltime;			/* number of seconds valid */
};

struct soliciter {
	TAILQ_ENTRY(soliciter)	sol_next;

	struct sockaddr_in6	sol_addr;
};

struct	rainfo {
	/* pointer for list */
	TAILQ_ENTRY(rainfo)	rai_next;

	/* interface information */
	struct ifinfo *rai_ifinfo;

	int	rai_advlinkopt;		/* bool: whether include link-layer addr opt */
	int	rai_advifprefix;	/* bool: gather IF prefixes? */

	/* Router configuration variables */
	uint16_t	rai_lifetime;		/* AdvDefaultLifetime */
	uint16_t	rai_maxinterval;	/* MaxRtrAdvInterval */
	uint16_t	rai_mininterval;	/* MinRtrAdvInterval */
	int 	rai_managedflg;		/* AdvManagedFlag */
	int	rai_otherflg;		/* AdvOtherConfigFlag */
#ifdef DRAFT_IETF_6MAN_IPV6ONLY_FLAG
	int	rai_ipv6onlyflg;	/* AdvIPv6OnlyFlag */
#endif

	int	rai_rtpref;		/* router preference */
	uint32_t	rai_linkmtu;		/* AdvLinkMTU */
	uint32_t	rai_reachabletime;	/* AdvReachableTime */
	uint32_t	rai_retranstimer;	/* AdvRetransTimer */
	uint8_t	rai_hoplimit;		/* AdvCurHopLimit */

	TAILQ_HEAD(, prefix) rai_prefix;/* AdvPrefixList(link head) */
	int	rai_pfxs;		/* number of prefixes */

	uint16_t	rai_clockskew;	/* used for consisitency check of lifetimes */

	TAILQ_HEAD(, rdnss) rai_rdnss;	/* DNS server list */
	TAILQ_HEAD(, dnssl) rai_dnssl;	/* search domain list */
	TAILQ_HEAD(, rtinfo) rai_route;	/* route information option (link head) */
	int	rai_routes;		/* number of route information options */
	/* actual RA packet data and its length */
	size_t	rai_ra_datalen;
	char	*rai_ra_data;

	/* info about soliciter */
	TAILQ_HEAD(, soliciter) rai_soliciter;	/* recent solication source */
};

/* RA information list */
extern TAILQ_HEAD(railist_head_t, rainfo) railist;

/*
 * ifi_state:
 *
 *           (INIT)
 *              |
 *              | update_ifinfo()
 *              | update_persist_ifinfo()
 *              v
 *         UNCONFIGURED
 *               |  ^
 *   loadconfig()|  |rm_ifinfo(), ra_output()
 *      (MC join)|  |(MC leave)
 *               |  |
 *               |  |
 *               v  |
 *         TRANSITIVE
 *               |  ^
 *    ra_output()|  |getconfig()
 *               |  |
 *               |  |
 *               |  |
 *               v  |
 *         CONFIGURED
 *
 *
 */
#define	IFI_STATE_UNCONFIGURED	0
#define	IFI_STATE_CONFIGURED	1
#define	IFI_STATE_TRANSITIVE	2

struct	ifinfo {
	TAILQ_ENTRY(ifinfo)	ifi_next;

	uint16_t	ifi_state;
	uint16_t	ifi_persist;
	uint16_t	ifi_ifindex;
	char	ifi_ifname[IFNAMSIZ];
	uint8_t	ifi_type;
	uint16_t	ifi_flags;
	uint32_t	ifi_nd_flags;
	uint32_t	ifi_phymtu;
	struct sockaddr_dl	ifi_sdl;

	struct rainfo	*ifi_rainfo;
	struct rainfo	*ifi_rainfo_trans;
	uint16_t	ifi_burstcount;
	uint32_t	ifi_burstinterval;
	struct rtadvd_timer	*ifi_ra_timer;
	/* timestamp when the latest RA was sent */
	struct timespec		ifi_ra_lastsent;
	uint16_t	ifi_rs_waitcount;

	/* statistics */
	uint64_t ifi_raoutput;		/* # of RAs sent */
	uint64_t ifi_rainput;		/* # of RAs received */
	uint64_t ifi_rainconsistent;	/* # of inconsistent recv'd RAs  */
	uint64_t ifi_rsinput;		/* # of RSs received */
};

/* Interface list */
extern TAILQ_HEAD(ifilist_head_t, ifinfo) ifilist;

extern char *mcastif;

struct rtadvd_timer	*ra_timeout(void *);
void			ra_timer_update(void *, struct timespec *);
void			ra_output(struct ifinfo *);

int			prefix_match(struct in6_addr *, int,
			    struct in6_addr *, int);
struct ifinfo		*if_indextoifinfo(int);
struct prefix		*find_prefix(struct rainfo *,
			    struct in6_addr *, int);
void			rtadvd_set_reload(int);
void			rtadvd_set_shutdown(int);
