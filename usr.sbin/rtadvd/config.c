/*	$FreeBSD$	*/
/*	$KAME: config.c,v 1.84 2003/08/05 12:34:23 itojun Exp $	*/

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

#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <net/if.h>
#include <net/route.h>
#include <net/if_dl.h>

#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>
#include <netinet/icmp6.h>
#include <netinet6/nd6.h>

#include <arpa/inet.h>

#include <stdio.h>
#include <syslog.h>
#include <errno.h>
#include <inttypes.h>
#include <netdb.h>
#include <string.h>
#include <search.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <ifaddrs.h>

#include "rtadvd.h"
#include "advcap.h"
#include "timer.h"
#include "if.h"
#include "config.h"

/* label of tcapcode + number + domain name + zero octet */
static char entbuf[10 + 3 + NI_MAXHOST + 1];
static char oentbuf[10 + 3 + NI_MAXHOST + 1];
static char abuf[DNAME_LABELENC_MAXLEN];

static time_t prefix_timo = (60 * 120);	/* 2 hours.
					 * XXX: should be configurable. */

static struct rtadvd_timer *prefix_timeout(void *);
static void makeentry(char *, size_t, int, const char *);
static ssize_t dname_labelenc(char *, const char *);

/* Encode domain name label encoding in RFC 1035 Section 3.1 */
static ssize_t
dname_labelenc(char *dst, const char *src)
{
	char *dst_origin;
	char *p;
	size_t len;

	dst_origin = dst;
	len = strlen(src);

	if (len + len / 64 + 1 + 1 > DNAME_LABELENC_MAXLEN)
		return (-1);
	/* Length fields per 63 octets + '\0' (<= DNAME_LABELENC_MAXLEN) */
	memset(dst, 0, len + len / 64 + 1 + 1);

	syslog(LOG_DEBUG, "<%s> labelenc = %s", __func__, src);
	while (src && (len = strlen(src)) != 0) {
		/* Put a length field with 63 octet limitation first. */
		p = strchr(src, '.');
		if (p == NULL)
			*dst = len = MIN(63, len);
		else
			*dst = len = MIN(63, p - src);
		if (dst + 1 + len < dst_origin + DNAME_LABELENC_MAXLEN)
			dst++;
		else
			return (-1);
		/* Copy 63 octets at most. */
		memcpy(dst, src, len);
		dst += len;
		if (p == NULL) /* the last label */
			break;
		src = p + 1;
	}
	/* Always need a 0-length label at the tail. */
	*dst++ = '\0';

	syslog(LOG_DEBUG, "<%s> labellen = %td", __func__, dst - dst_origin);
	return (dst - dst_origin);
}

#define	MUSTHAVE(var, cap)						\
    do {								\
	int64_t t;							\
	if ((t = agetnum(cap)) < 0) {					\
		fprintf(stderr, "rtadvd: need %s for interface %s\n",	\
			cap, intface);					\
		exit(1);						\
	}								\
	var = t;							\
     } while (0)

#define	MAYHAVE(var, cap, def)						\
     do {								\
	if ((var = agetnum(cap)) < 0)					\
		var = def;						\
     } while (0)

int
loadconfig_index(int idx)
{
	char ifname[IFNAMSIZ];

	syslog(LOG_DEBUG, "<%s> enter", __func__);

	if (if_indextoname(idx, ifname) != NULL)
		return (loadconfig_ifname(ifname));
	else
		return (1);
}

int
loadconfig_ifname(char *ifname)
{
	struct ifinfo *ifi;

	syslog(LOG_DEBUG, "<%s> enter", __func__);

	update_ifinfo(&ifilist, UPDATE_IFINFO_ALL);
	TAILQ_FOREACH(ifi, &ifilist, ifi_next) {
		/* NULL means all IFs will be processed. */
		if (ifname != NULL &&
		    strcmp(ifi->ifi_ifname, ifname) != 0)
			continue;

		if (!ifi->ifi_persist) {
			syslog(LOG_INFO,
			    "<%s> %s is not a target interface.  "
			    "Ignored at this moment.", __func__,
			    ifi->ifi_ifname);
			continue;

		}
		if (ifi->ifi_ifindex == 0) {
			syslog(LOG_ERR,
			    "<%s> %s not found.  "
			    "Ignored at this moment.", __func__,
			    ifi->ifi_ifname);
			continue;
		}
		if (getconfig(ifi) == NULL) {
			syslog(LOG_ERR,
			    "<%s> invalid configuration for %s.  "
			    "Ignored at this moment.", __func__,
			    ifi->ifi_ifname);
			continue;
		}
	}
	return (0);
}

int
rm_ifinfo_index(int idx)
{
	struct ifinfo *ifi;

	ifi = if_indextoifinfo(idx);
	if (ifi == NULL) {
		syslog(LOG_ERR, "<%s>: ifinfo not found (idx=%d)",
		    __func__, idx);
		return (-1);
	}

	return (rm_ifinfo(ifi));
}

int
rm_ifinfo(struct ifinfo *ifi)
{
	int error;

	syslog(LOG_DEBUG, "<%s> enter (%s).", __func__, ifi->ifi_ifname);
	switch (ifi->ifi_state) {
	case IFI_STATE_UNCONFIGURED:
		return (0);
		break;
	default:
		ifi->ifi_state = IFI_STATE_UNCONFIGURED;
		syslog(LOG_DEBUG,
		    "<%s> ifname=%s marked as UNCONFIGURED.",
		    __func__, ifi->ifi_ifname);

		/* XXX: No MC leaving here because index is disappeared */

		/* Inactivate timer */
		rtadvd_remove_timer(ifi->ifi_ra_timer);
		ifi->ifi_ra_timer = NULL;
		break;
	}

	/* clean up ifi */
	if (!ifi->ifi_persist) {
		TAILQ_REMOVE(&ifilist, ifi, ifi_next);
		syslog(LOG_DEBUG, "<%s>: ifinfo (idx=%d) removed.",
		    __func__, ifi->ifi_ifindex);
	} else {
		/* recreate an empty entry */
		update_persist_ifinfo(&ifilist, ifi->ifi_ifname);
		syslog(LOG_DEBUG, "<%s>: ifname=%s is persistent.",
		    __func__, ifi->ifi_ifname);
	}

	/* clean up rai if any */
	switch (ifi->ifi_state) {
	case IFI_STATE_CONFIGURED:
		if (ifi->ifi_rainfo != NULL) {
			error = rm_rainfo(ifi->ifi_rainfo);
			if (error)
				return (error);
			ifi->ifi_rainfo = NULL;
		}
		break;
	case IFI_STATE_TRANSITIVE:
		if (ifi->ifi_rainfo == ifi->ifi_rainfo_trans) {
			if (ifi->ifi_rainfo != NULL) {
				error = rm_rainfo(ifi->ifi_rainfo);
				if (error)
					return (error);
				ifi->ifi_rainfo = NULL;
				ifi->ifi_rainfo_trans = NULL;
			}
		} else {
			if (ifi->ifi_rainfo != NULL) {
				error = rm_rainfo(ifi->ifi_rainfo);
				if (error)
					return (error);
				ifi->ifi_rainfo = NULL;
			}
			if (ifi->ifi_rainfo_trans != NULL) {
				error = rm_rainfo(ifi->ifi_rainfo_trans);
				if (error)
					return (error);
				ifi->ifi_rainfo_trans = NULL;
			}
		}
	}

	syslog(LOG_DEBUG, "<%s> leave (%s).", __func__, ifi->ifi_ifname);
	if (!ifi->ifi_persist)
		free(ifi);
	return (0);
}

int
rm_rainfo(struct rainfo *rai)
{
	struct prefix *pfx;
	struct soliciter *sol;
	struct rdnss *rdn;
	struct rdnss_addr *rdna;
	struct dnssl *dns;
	struct rtinfo *rti;

	syslog(LOG_DEBUG, "<%s>: enter",  __func__);

	TAILQ_REMOVE(&railist, rai, rai_next);
	if (rai->rai_ifinfo != NULL)
		syslog(LOG_DEBUG, "<%s>: rainfo (idx=%d) removed.",
		    __func__, rai->rai_ifinfo->ifi_ifindex);

	if (rai->rai_ra_data != NULL)
		free(rai->rai_ra_data);

	while ((pfx = TAILQ_FIRST(&rai->rai_prefix)) != NULL)
		delete_prefix(pfx);
	while ((sol = TAILQ_FIRST(&rai->rai_soliciter)) != NULL) {
		TAILQ_REMOVE(&rai->rai_soliciter, sol, sol_next);
		free(sol);
	}
	while ((rdn = TAILQ_FIRST(&rai->rai_rdnss)) != NULL) {
		TAILQ_REMOVE(&rai->rai_rdnss, rdn, rd_next);
		while ((rdna = TAILQ_FIRST(&rdn->rd_list)) != NULL) {
			TAILQ_REMOVE(&rdn->rd_list, rdna, ra_next);
			free(rdna);
		}
		free(rdn);
	}
	while ((dns = TAILQ_FIRST(&rai->rai_dnssl)) != NULL) {
		TAILQ_REMOVE(&rai->rai_dnssl, dns, dn_next);
		free(dns);
	}
	while ((rti = TAILQ_FIRST(&rai->rai_route)) != NULL) {
		TAILQ_REMOVE(&rai->rai_route, rti, rti_next);
		free(rti);
	}
	free(rai);
	syslog(LOG_DEBUG, "<%s>: leave",  __func__);

	return (0);
}

struct ifinfo *
getconfig(struct ifinfo *ifi)
{
	int stat, i;
	int error;
	char tbuf[BUFSIZ];
	struct rainfo *rai;
	struct rainfo *rai_old;
	int32_t val;
	int64_t val64;
	char buf[BUFSIZ];
	char *bp = buf;
	char *addr, *flagstr;

	if (ifi == NULL)	/* if does not exist */
		return (NULL);

	if (ifi->ifi_state == IFI_STATE_TRANSITIVE &&
	    ifi->ifi_rainfo == NULL) {
		syslog(LOG_INFO, "<%s> %s is shutting down.  Skipped.",
		    __func__, ifi->ifi_ifname);
		return (NULL);
	}

	if ((stat = agetent(tbuf, ifi->ifi_ifname)) <= 0) {
		memset(tbuf, 0, sizeof(tbuf));
		syslog(LOG_INFO,
		    "<%s> %s isn't defined in the configuration file"
		    " or the configuration file doesn't exist."
		    " Treat it as default",
		     __func__, ifi->ifi_ifname);
	}

	ELM_MALLOC(rai, exit(1));
	TAILQ_INIT(&rai->rai_prefix);
	TAILQ_INIT(&rai->rai_route);
	TAILQ_INIT(&rai->rai_rdnss);
	TAILQ_INIT(&rai->rai_dnssl);
	TAILQ_INIT(&rai->rai_soliciter);
	rai->rai_ifinfo = ifi;

	/* gather on-link prefixes from the network interfaces. */
	if (agetflag("noifprefix"))
		rai->rai_advifprefix = 0;
	else
		rai->rai_advifprefix = 1;

	/* get interface information */
	if (agetflag("nolladdr"))
		rai->rai_advlinkopt = 0;
	else
		rai->rai_advlinkopt = 1;
	if (rai->rai_advlinkopt) {
		if (ifi->ifi_sdl.sdl_type == 0) {
			syslog(LOG_ERR,
			    "<%s> can't get information of %s",
			    __func__, ifi->ifi_ifname);
			goto getconfig_free_rai;
		}
	}

	/*
	 * set router configuration variables.
	 */
	MAYHAVE(val, "maxinterval", DEF_MAXRTRADVINTERVAL);
	if (val < MIN_MAXINTERVAL || val > MAX_MAXINTERVAL) {
		syslog(LOG_ERR,
		    "<%s> maxinterval (%" PRIu32 ") on %s is invalid "
		    "(must be between %u and %u)", __func__, val,
		    ifi->ifi_ifname, MIN_MAXINTERVAL, MAX_MAXINTERVAL);
		goto getconfig_free_rai;
	}
	rai->rai_maxinterval = (uint16_t)val;

	MAYHAVE(val, "mininterval", rai->rai_maxinterval/3);
	if ((uint16_t)val < MIN_MININTERVAL ||
	    (uint16_t)val > (rai->rai_maxinterval * 3) / 4) {
		syslog(LOG_ERR,
		    "<%s> mininterval (%" PRIu32 ") on %s is invalid "
		    "(must be between %d and %d)",
		    __func__, val, ifi->ifi_ifname, MIN_MININTERVAL,
		    (rai->rai_maxinterval * 3) / 4);
		goto getconfig_free_rai;
	}
	rai->rai_mininterval = (uint16_t)val;

	MAYHAVE(val, "chlim", DEF_ADVCURHOPLIMIT);
	rai->rai_hoplimit = val & 0xff;

	if ((flagstr = (char *)agetstr("raflags", &bp))) {
		val = 0;
		if (strchr(flagstr, 'm'))
			val |= ND_RA_FLAG_MANAGED;
		if (strchr(flagstr, 'o'))
			val |= ND_RA_FLAG_OTHER;
		if (strchr(flagstr, 'h'))
			val |= ND_RA_FLAG_RTPREF_HIGH;
		if (strchr(flagstr, 'l')) {
			if ((val & ND_RA_FLAG_RTPREF_HIGH)) {
				syslog(LOG_ERR, "<%s> the \'h\' and \'l\'"
				    " router flags are exclusive", __func__);
				goto getconfig_free_rai;
			}
			val |= ND_RA_FLAG_RTPREF_LOW;
		}
#ifdef DRAFT_IETF_6MAN_IPV6ONLY_FLAG
		if (strchr(flagstr, 'S'))
			val |= ND_RA_FLAG_IPV6_ONLY;
#endif
	} else
		MAYHAVE(val, "raflags", 0);

	rai->rai_managedflg = val & ND_RA_FLAG_MANAGED;
	rai->rai_otherflg = val & ND_RA_FLAG_OTHER;
#ifndef ND_RA_FLAG_RTPREF_MASK
#define ND_RA_FLAG_RTPREF_MASK	0x18 /* 00011000 */
#define ND_RA_FLAG_RTPREF_RSV	0x10 /* 00010000 */
#endif
	rai->rai_rtpref = val & ND_RA_FLAG_RTPREF_MASK;
	if (rai->rai_rtpref == ND_RA_FLAG_RTPREF_RSV) {
		syslog(LOG_ERR, "<%s> invalid router preference (%02x) on %s",
		    __func__, rai->rai_rtpref, ifi->ifi_ifname);
		goto getconfig_free_rai;
	}
#ifdef DRAFT_IETF_6MAN_IPV6ONLY_FLAG
	rai->rai_ipv6onlyflg = val & ND_RA_FLAG_IPV6_ONLY;
#endif

	MAYHAVE(val, "rltime", rai->rai_maxinterval * 3);
	if ((uint16_t)val && ((uint16_t)val < rai->rai_maxinterval ||
	    (uint16_t)val > MAXROUTERLIFETIME)) {
		syslog(LOG_ERR,
		    "<%s> router lifetime (%" PRIu32 ") on %s is invalid "
		    "(must be 0 or between %d and %d)",
		    __func__, val, ifi->ifi_ifname, rai->rai_maxinterval,
		    MAXROUTERLIFETIME);
		goto getconfig_free_rai;
	}
	rai->rai_lifetime = val & 0xffff;

	MAYHAVE(val, "rtime", DEF_ADVREACHABLETIME);
	if (val < 0 || val > MAXREACHABLETIME) {
		syslog(LOG_ERR,
		    "<%s> reachable time (%" PRIu32 ") on %s is invalid "
		    "(must be no greater than %d)",
		    __func__, val, ifi->ifi_ifname, MAXREACHABLETIME);
		goto getconfig_free_rai;
	}
	rai->rai_reachabletime = (uint32_t)val;

	MAYHAVE(val64, "retrans", DEF_ADVRETRANSTIMER);
	if (val64 < 0 || val64 > 0xffffffff) {
		syslog(LOG_ERR, "<%s> retrans time (%" PRIu64 ") on %s out of range",
		    __func__, val64, ifi->ifi_ifname);
		goto getconfig_free_rai;
	}
	rai->rai_retranstimer = (uint32_t)val64;

	if (agetnum("hapref") != -1 || agetnum("hatime") != -1) {
		syslog(LOG_ERR,
		    "<%s> mobile-ip6 configuration not supported",
		    __func__);
		goto getconfig_free_rai;
	}
	/* prefix information */

	/*
	 * This is an implementation specific parameter to consider
	 * link propagation delays and poorly synchronized clocks when
	 * checking consistency of advertised lifetimes.
	 */
	MAYHAVE(val, "clockskew", 0);
	rai->rai_clockskew = val;

	rai->rai_pfxs = 0;
	for (i = -1; i < MAXPREFIX; i++) {
		struct prefix *pfx;

		makeentry(entbuf, sizeof(entbuf), i, "addr");
		addr = (char *)agetstr(entbuf, &bp);
		if (addr == NULL)
			continue;

		/* allocate memory to store prefix information */
		ELM_MALLOC(pfx, exit(1));
		pfx->pfx_rainfo = rai;
		pfx->pfx_origin = PREFIX_FROM_CONFIG;

		if (inet_pton(AF_INET6, addr, &pfx->pfx_prefix) != 1) {
			syslog(LOG_ERR,
			    "<%s> inet_pton failed for %s",
			    __func__, addr);
			goto getconfig_free_pfx;
		}
		if (IN6_IS_ADDR_MULTICAST(&pfx->pfx_prefix)) {
			syslog(LOG_ERR,
			    "<%s> multicast prefix (%s) must "
			    "not be advertised on %s",
			    __func__, addr, ifi->ifi_ifname);
			goto getconfig_free_pfx;
		}
		if (IN6_IS_ADDR_LINKLOCAL(&pfx->pfx_prefix))
			syslog(LOG_NOTICE,
			    "<%s> link-local prefix (%s) will be"
			    " advertised on %s",
			    __func__, addr, ifi->ifi_ifname);

		makeentry(entbuf, sizeof(entbuf), i, "prefixlen");
		MAYHAVE(val, entbuf, 64);
		if (val < 0 || val > 128) {
			syslog(LOG_ERR, "<%s> prefixlen (%" PRIu32 ") for %s "
			    "on %s out of range",
			    __func__, val, addr, ifi->ifi_ifname);
			goto getconfig_free_pfx;
		}
		pfx->pfx_prefixlen = (int)val;

		makeentry(entbuf, sizeof(entbuf), i, "pinfoflags");
		if ((flagstr = (char *)agetstr(entbuf, &bp))) {
			val = 0;
			if (strchr(flagstr, 'l'))
				val |= ND_OPT_PI_FLAG_ONLINK;
			if (strchr(flagstr, 'a'))
				val |= ND_OPT_PI_FLAG_AUTO;
		} else {
			MAYHAVE(val, entbuf,
			    (ND_OPT_PI_FLAG_ONLINK|ND_OPT_PI_FLAG_AUTO));
		}
		pfx->pfx_onlinkflg = val & ND_OPT_PI_FLAG_ONLINK;
		pfx->pfx_autoconfflg = val & ND_OPT_PI_FLAG_AUTO;

		makeentry(entbuf, sizeof(entbuf), i, "vltime");
		MAYHAVE(val64, entbuf, DEF_ADVVALIDLIFETIME);
		if (val64 < 0 || val64 > 0xffffffff) {
			syslog(LOG_ERR, "<%s> vltime (%" PRIu64 ") for "
			    "%s/%d on %s is out of range",
			    __func__, val64,
			    addr, pfx->pfx_prefixlen, ifi->ifi_ifname);
			goto getconfig_free_pfx;
		}
		pfx->pfx_validlifetime = (uint32_t)val64;

		makeentry(entbuf, sizeof(entbuf), i, "vltimedecr");
		if (agetflag(entbuf)) {
			struct timespec now;

			clock_gettime(CLOCK_MONOTONIC_FAST, &now);
			pfx->pfx_vltimeexpire =
				now.tv_sec + pfx->pfx_validlifetime;
		}

		makeentry(entbuf, sizeof(entbuf), i, "pltime");
		MAYHAVE(val64, entbuf, DEF_ADVPREFERREDLIFETIME);
		if (val64 < 0 || val64 > 0xffffffff) {
			syslog(LOG_ERR,
			    "<%s> pltime (%" PRIu64 ") for %s/%d on %s "
			    "is out of range",
			    __func__, val64,
			    addr, pfx->pfx_prefixlen, ifi->ifi_ifname);
			goto getconfig_free_pfx;
		}
		pfx->pfx_preflifetime = (uint32_t)val64;

		makeentry(entbuf, sizeof(entbuf), i, "pltimedecr");
		if (agetflag(entbuf)) {
			struct timespec now;

			clock_gettime(CLOCK_MONOTONIC_FAST, &now);
			pfx->pfx_pltimeexpire =
			    now.tv_sec + pfx->pfx_preflifetime;
		}
		/* link into chain */
		TAILQ_INSERT_TAIL(&rai->rai_prefix, pfx, pfx_next);
		rai->rai_pfxs++;
		continue;
getconfig_free_pfx:
		free(pfx);
	}
	if (rai->rai_advifprefix && rai->rai_pfxs == 0)
		get_prefix(rai);

	MAYHAVE(val64, "mtu", 0);
	if (val < 0 || val64 > 0xffffffff) {
		syslog(LOG_ERR,
		    "<%s> mtu (%" PRIu64 ") on %s out of range",
		    __func__, val64, ifi->ifi_ifname);
		goto getconfig_free_rai;
	}
	rai->rai_linkmtu = (uint32_t)val64;
	if (rai->rai_linkmtu == 0) {
		char *mtustr;

		if ((mtustr = (char *)agetstr("mtu", &bp)) &&
		    strcmp(mtustr, "auto") == 0)
			rai->rai_linkmtu = ifi->ifi_phymtu;
	}
	else if (rai->rai_linkmtu < IPV6_MMTU ||
	    rai->rai_linkmtu > ifi->ifi_phymtu) {
		syslog(LOG_ERR,
		    "<%s> advertised link mtu (%" PRIu32 ") on %s is invalid (must "
		    "be between least MTU (%d) and physical link MTU (%d)",
		    __func__, rai->rai_linkmtu, ifi->ifi_ifname,
		    IPV6_MMTU, ifi->ifi_phymtu);
		goto getconfig_free_rai;
	}

#ifdef SIOCSIFINFO_IN6
	{
		struct in6_ndireq ndi;
		int s;

		if ((s = socket(AF_INET6, SOCK_DGRAM, 0)) < 0) {
			syslog(LOG_ERR, "<%s> socket: %s", __func__,
			    strerror(errno));
			exit(1);
		}
		memset(&ndi, 0, sizeof(ndi));
		strlcpy(ndi.ifname, ifi->ifi_ifname, sizeof(ndi.ifname));
		if (ioctl(s, SIOCGIFINFO_IN6, (caddr_t)&ndi) < 0)
			syslog(LOG_INFO, "<%s> ioctl:SIOCGIFINFO_IN6 at %s: %s",
			    __func__, ifi->ifi_ifname, strerror(errno));

		/* reflect the RA info to the host variables in kernel */
		ndi.ndi.chlim = rai->rai_hoplimit;
		ndi.ndi.retrans = rai->rai_retranstimer;
		ndi.ndi.basereachable = rai->rai_reachabletime;
		if (ioctl(s, SIOCSIFINFO_IN6, (caddr_t)&ndi) < 0)
			syslog(LOG_INFO, "<%s> ioctl:SIOCSIFINFO_IN6 at %s: %s",
			    __func__, ifi->ifi_ifname, strerror(errno));

		close(s);
	}
#endif

	/* route information */
	rai->rai_routes = 0;
	for (i = -1; i < MAXROUTE; i++) {
		struct rtinfo *rti;

		makeentry(entbuf, sizeof(entbuf), i, "rtprefix");
		addr = (char *)agetstr(entbuf, &bp);
		if (addr == NULL) {
			makeentry(oentbuf, sizeof(oentbuf), i, "rtrprefix");
			addr = (char *)agetstr(oentbuf, &bp);
			if (addr)
				fprintf(stderr, "%s was obsoleted.  Use %s.\n",
				    oentbuf, entbuf);
		}
		if (addr == NULL)
			continue;

		/* allocate memory to store prefix information */
		ELM_MALLOC(rti, exit(1));

		if (inet_pton(AF_INET6, addr, &rti->rti_prefix) != 1) {
			syslog(LOG_ERR, "<%s> inet_pton failed for %s",
			    __func__, addr);
			goto getconfig_free_rti;
		}
#if 0
		/*
		 * XXX: currently there's no restriction in route information
		 * prefix according to
		 * draft-ietf-ipngwg-router-selection-00.txt.
		 * However, I think the similar restriction be necessary.
		 */
		MAYHAVE(val64, entbuf, DEF_ADVVALIDLIFETIME);
		if (IN6_IS_ADDR_MULTICAST(&rti->prefix)) {
			syslog(LOG_ERR,
			    "<%s> multicast route (%s) must "
			    "not be advertised on %s",
			    __func__, addr, ifi->ifi_ifname);
			goto getconfig_free_rti;
		}
		if (IN6_IS_ADDR_LINKLOCAL(&rti->prefix)) {
			syslog(LOG_NOTICE,
			    "<%s> link-local route (%s) will "
			    "be advertised on %s",
			    __func__, addr, ifi->ifi_ifname);
			goto getconfig_free_rti;
		}
#endif

		makeentry(entbuf, sizeof(entbuf), i, "rtplen");
		/* XXX: 256 is a magic number for compatibility check. */
		MAYHAVE(val, entbuf, 256);
		if (val == 256) {
			makeentry(oentbuf, sizeof(oentbuf), i, "rtrplen");
			MAYHAVE(val, oentbuf, 256);
			if (val != 256)
				fprintf(stderr, "%s was obsoleted.  Use %s.\n",
				    oentbuf, entbuf);
			else
				val = 64;
		}
		if (val < 0 || val > 128) {
			syslog(LOG_ERR, "<%s> prefixlen (%" PRIu32 ") for %s on %s "
			    "out of range",
			    __func__, val, addr, ifi->ifi_ifname);
			goto getconfig_free_rti;
		}
		rti->rti_prefixlen = (int)val;

		makeentry(entbuf, sizeof(entbuf), i, "rtflags");
		if ((flagstr = (char *)agetstr(entbuf, &bp))) {
			val = 0;
			if (strchr(flagstr, 'h'))
				val |= ND_RA_FLAG_RTPREF_HIGH;
			if (strchr(flagstr, 'l')) {
				if ((val & ND_RA_FLAG_RTPREF_HIGH)) {
					syslog(LOG_ERR,
					    "<%s> the \'h\' and \'l\' route"
					    " preferences are exclusive",
					    __func__);
					goto getconfig_free_rti;
				}
				val |= ND_RA_FLAG_RTPREF_LOW;
			}
		} else
			MAYHAVE(val, entbuf, 256); /* XXX */
		if (val == 256) {
			makeentry(oentbuf, sizeof(oentbuf), i, "rtrflags");
			MAYHAVE(val, oentbuf, 256);
			if (val != 256) {
				fprintf(stderr, "%s was obsoleted.  Use %s.\n",
				    oentbuf, entbuf);
			} else
				val = 0;
		}
		rti->rti_rtpref = val & ND_RA_FLAG_RTPREF_MASK;
		if (rti->rti_rtpref == ND_RA_FLAG_RTPREF_RSV) {
			syslog(LOG_ERR, "<%s> invalid route preference (%02x) "
			    "for %s/%d on %s",
			    __func__, rti->rti_rtpref, addr,
			    rti->rti_prefixlen, ifi->ifi_ifname);
			goto getconfig_free_rti;
		}

		/*
		 * Since the spec does not a default value, we should make
		 * this entry mandatory.  However, FreeBSD 4.4 has shipped
		 * with this field being optional, we use the router lifetime
		 * as an ad-hoc default value with a warning message.
		 */
		makeentry(entbuf, sizeof(entbuf), i, "rtltime");
		MAYHAVE(val64, entbuf, -1);
		if (val64 == -1) {
			makeentry(oentbuf, sizeof(oentbuf), i, "rtrltime");
			MAYHAVE(val64, oentbuf, -1);
			if (val64 != -1)
				fprintf(stderr, "%s was obsoleted.  Use %s.\n",
				    oentbuf, entbuf);
			else {
				fprintf(stderr, "%s should be specified "
				    "for interface %s.\n", entbuf,
				    ifi->ifi_ifname);
				val64 = rai->rai_lifetime;
			}
		}
		if (val64 < 0 || val64 > 0xffffffff) {
			syslog(LOG_ERR, "<%s> route lifetime (%" PRIu64 ") for "
			    "%s/%d on %s out of range", __func__,
			    val64, addr, rti->rti_prefixlen,
			    ifi->ifi_ifname);
			goto getconfig_free_rti;
		}
		rti->rti_ltime = (uint32_t)val64;

		/* link into chain */
		TAILQ_INSERT_TAIL(&rai->rai_route, rti, rti_next);
		rai->rai_routes++;
		continue;
getconfig_free_rti:
		free(rti);
	}

	/* DNS server and DNS search list information */
	for (i = -1; i < MAXRDNSSENT ; i++) {
		struct rdnss *rdn;
		struct rdnss_addr *rdna;
		char *ap;
		int c;

		makeentry(entbuf, sizeof(entbuf), i, "rdnss");
		addr = (char *)agetstr(entbuf, &bp);
		if (addr == NULL)
			continue;
		ELM_MALLOC(rdn, exit(1));

		TAILQ_INIT(&rdn->rd_list);

		for (ap = addr; ap - addr < (ssize_t)strlen(addr); ap += c+1) {
			c = strcspn(ap, ",");
			strncpy(abuf, ap, c);
			abuf[c] = '\0';
			ELM_MALLOC(rdna, goto getconfig_free_rdn);
			if (inet_pton(AF_INET6, abuf, &rdna->ra_dns) != 1) {
				syslog(LOG_ERR, "<%s> inet_pton failed for %s",
				    __func__, abuf);
				free(rdna);
				goto getconfig_free_rdn;
			}
			TAILQ_INSERT_TAIL(&rdn->rd_list, rdna, ra_next);
		}

		makeentry(entbuf, sizeof(entbuf), i, "rdnssltime");
		MAYHAVE(val, entbuf, (rai->rai_maxinterval * 3 / 2));
		if ((uint16_t)val < rai->rai_maxinterval ||
		    (uint16_t)val > rai->rai_maxinterval * 2) {
			syslog(LOG_ERR, "%s (%" PRIu16 ") on %s is invalid "
			    "(must be between %d and %d)",
			    entbuf, val, ifi->ifi_ifname, rai->rai_maxinterval,
			    rai->rai_maxinterval * 2);
			goto getconfig_free_rdn;
		}
		rdn->rd_ltime = val;

		/* link into chain */
		TAILQ_INSERT_TAIL(&rai->rai_rdnss, rdn, rd_next);
		continue;
getconfig_free_rdn:
		while ((rdna = TAILQ_FIRST(&rdn->rd_list)) != NULL) {
			TAILQ_REMOVE(&rdn->rd_list, rdna, ra_next);
			free(rdna);
		}
		free(rdn);
	}

	for (i = -1; i < MAXDNSSLENT ; i++) {
		struct dnssl *dns;
		struct dnssl_addr *dnsa;
		char *ap;
		int c;

		makeentry(entbuf, sizeof(entbuf), i, "dnssl");
		addr = (char *)agetstr(entbuf, &bp);
		if (addr == NULL)
			continue;

		ELM_MALLOC(dns, exit(1));

		TAILQ_INIT(&dns->dn_list);

		for (ap = addr; ap - addr < (ssize_t)strlen(addr); ap += c+1) {
			c = strcspn(ap, ",");
			strncpy(abuf, ap, c);
			abuf[c] = '\0';
			ELM_MALLOC(dnsa, goto getconfig_free_dns);
			dnsa->da_len = dname_labelenc(dnsa->da_dom, abuf);
			if (dnsa->da_len < 0) {
				syslog(LOG_ERR, "Invalid dnssl entry: %s",
				    abuf);
				goto getconfig_free_dns;
			}
			syslog(LOG_DEBUG, "<%s>: dnsa->da_len = %d", __func__,
			    dnsa->da_len);
			TAILQ_INSERT_TAIL(&dns->dn_list, dnsa, da_next);
		}

		makeentry(entbuf, sizeof(entbuf), i, "dnsslltime");
		MAYHAVE(val, entbuf, (rai->rai_maxinterval * 3 / 2));
		if ((uint16_t)val < rai->rai_maxinterval ||
		    (uint16_t)val > rai->rai_maxinterval * 2) {
			syslog(LOG_ERR, "%s (%" PRIu16 ") on %s is invalid "
			    "(must be between %d and %d)",
			    entbuf, val, ifi->ifi_ifname, rai->rai_maxinterval,
			    rai->rai_maxinterval * 2);
			goto getconfig_free_dns;
		}
		dns->dn_ltime = val;

		/* link into chain */
		TAILQ_INSERT_TAIL(&rai->rai_dnssl, dns, dn_next);
		continue;
getconfig_free_dns:
		while ((dnsa = TAILQ_FIRST(&dns->dn_list)) != NULL) {
			TAILQ_REMOVE(&dns->dn_list, dnsa, da_next);
			free(dnsa);
		}
		free(dns);
	}
	/* construct the sending packet */
	make_packet(rai);

	/*
	 * If an entry with the same ifindex exists, remove it first.
	 * Before the removal, RDNSS and DNSSL options with
	 * zero-lifetime will be sent.
	 */
	switch (ifi->ifi_state) {
	case IFI_STATE_UNCONFIGURED:
		/* UNCONFIGURED -> TRANSITIVE */

		error = sock_mc_join(&sock, ifi->ifi_ifindex);
		if (error)
			exit(1);

		ifi->ifi_state = IFI_STATE_TRANSITIVE;
		ifi->ifi_burstcount = MAX_INITIAL_RTR_ADVERTISEMENTS;
		ifi->ifi_burstinterval = MAX_INITIAL_RTR_ADVERT_INTERVAL;

		/* The same two rai mean initial burst */
		ifi->ifi_rainfo = rai;
		ifi->ifi_rainfo_trans = rai;
		TAILQ_INSERT_TAIL(&railist, rai, rai_next);

		if (ifi->ifi_ra_timer == NULL)
			ifi->ifi_ra_timer = rtadvd_add_timer(ra_timeout,
			    ra_timer_update, ifi, ifi);
		ra_timer_update(ifi, &ifi->ifi_ra_timer->rat_tm);
		rtadvd_set_timer(&ifi->ifi_ra_timer->rat_tm,
		    ifi->ifi_ra_timer);

		syslog(LOG_DEBUG,
		    "<%s> ifname=%s marked as TRANSITIVE (initial burst).",
		    __func__, ifi->ifi_ifname);
		break;
	case IFI_STATE_CONFIGURED:
		/* CONFIGURED -> TRANSITIVE */
		rai_old = ifi->ifi_rainfo;
		if (rai_old == NULL) {
			syslog(LOG_ERR,
			    "<%s> ifi_rainfo is NULL"
			    " in IFI_STATE_CONFIGURED.", __func__);
			ifi = NULL;
			break;
		} else {
			struct rdnss *rdn;
			struct dnssl *dns;

			rai_old->rai_lifetime = 0;
			TAILQ_FOREACH(rdn, &rai_old->rai_rdnss, rd_next)
			    rdn->rd_ltime = 0;
			TAILQ_FOREACH(dns, &rai_old->rai_dnssl, dn_next)
			    dns->dn_ltime = 0;

			ifi->ifi_rainfo_trans = rai_old;
			ifi->ifi_state = IFI_STATE_TRANSITIVE;
			ifi->ifi_burstcount = MAX_FINAL_RTR_ADVERTISEMENTS;
			ifi->ifi_burstinterval = MIN_DELAY_BETWEEN_RAS;

			ra_timer_update(ifi, &ifi->ifi_ra_timer->rat_tm);
			rtadvd_set_timer(&ifi->ifi_ra_timer->rat_tm,
			    ifi->ifi_ra_timer);

			syslog(LOG_DEBUG,
			    "<%s> ifname=%s marked as TRANSITIVE"
			    " (transitional burst)",
			    __func__, ifi->ifi_ifname);
		}
		ifi->ifi_rainfo = rai;
		TAILQ_INSERT_TAIL(&railist, rai, rai_next);
		break;
	case IFI_STATE_TRANSITIVE:
		if (ifi->ifi_rainfo != NULL) {
			if (ifi->ifi_rainfo == ifi->ifi_rainfo_trans) {
				/* Reinitialize initial burst */
				rm_rainfo(ifi->ifi_rainfo);
				ifi->ifi_rainfo = rai;
				ifi->ifi_rainfo_trans = rai;
				ifi->ifi_burstcount =
				    MAX_INITIAL_RTR_ADVERTISEMENTS;
				ifi->ifi_burstinterval =
				    MAX_INITIAL_RTR_ADVERT_INTERVAL;
			} else {
				/* Replace ifi_rainfo with the new one */
				rm_rainfo(ifi->ifi_rainfo);
				ifi->ifi_rainfo = rai;
			}
			TAILQ_INSERT_TAIL(&railist, rai, rai_next);

			ra_timer_update(ifi, &ifi->ifi_ra_timer->rat_tm);
			rtadvd_set_timer(&ifi->ifi_ra_timer->rat_tm,
			    ifi->ifi_ra_timer);
		} else {
			/* XXX: NOTREACHED.  Being shut down. */
			syslog(LOG_ERR,
			    "<%s> %s is shutting down.  Skipped.",
			    __func__, ifi->ifi_ifname);
			rm_rainfo(rai);

			return (NULL);
		}
		break;
	}

	return (ifi);

getconfig_free_rai:
	free(rai);
	return (NULL);
}

void
get_prefix(struct rainfo *rai)
{
	struct ifaddrs *ifap, *ifa;
	struct prefix *pfx;
	struct in6_addr *a;
	struct ifinfo *ifi;
	char *p, *ep, *m, *lim;
	char ntopbuf[INET6_ADDRSTRLEN];

	if (getifaddrs(&ifap) < 0) {
		syslog(LOG_ERR,
		    "<%s> can't get interface addresses",
		    __func__);
		exit(1);
	}
	ifi = rai->rai_ifinfo;

	for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
		int plen;

		if (strcmp(ifa->ifa_name, ifi->ifi_ifname) != 0)
			continue;
		if (ifa->ifa_addr->sa_family != AF_INET6)
			continue;
		a = &((struct sockaddr_in6 *)ifa->ifa_addr)->sin6_addr;
		if (IN6_IS_ADDR_LINKLOCAL(a))
			continue;

		/* get prefix length */
		m = (char *)&((struct sockaddr_in6 *)ifa->ifa_netmask)->sin6_addr;
		lim = (char *)(ifa->ifa_netmask) + ifa->ifa_netmask->sa_len;
		plen = prefixlen(m, lim);
		if (plen <= 0 || plen > 128) {
			syslog(LOG_ERR, "<%s> failed to get prefixlen "
			    "or prefix is invalid",
			    __func__);
			exit(1);
		}
		if (plen == 128)	/* XXX */
			continue;
		if (find_prefix(rai, a, plen)) {
			/* ignore a duplicated prefix. */
			continue;
		}

		/* allocate memory to store prefix info. */
		ELM_MALLOC(pfx, exit(1));

		/* set prefix, sweep bits outside of prefixlen */
		pfx->pfx_prefixlen = plen;
		memcpy(&pfx->pfx_prefix, a, sizeof(*a));
		p = (char *)&pfx->pfx_prefix;
		ep = (char *)(&pfx->pfx_prefix + 1);
		while (m < lim && p < ep)
			*p++ &= *m++;
		while (p < ep)
			*p++ = 0x00;
	        if (!inet_ntop(AF_INET6, &pfx->pfx_prefix, ntopbuf,
	            sizeof(ntopbuf))) {
			syslog(LOG_ERR, "<%s> inet_ntop failed", __func__);
			exit(1);
		}
		syslog(LOG_DEBUG,
		    "<%s> add %s/%d to prefix list on %s",
		    __func__, ntopbuf, pfx->pfx_prefixlen, ifi->ifi_ifname);

		/* set other fields with protocol defaults */
		pfx->pfx_validlifetime = DEF_ADVVALIDLIFETIME;
		pfx->pfx_preflifetime = DEF_ADVPREFERREDLIFETIME;
		pfx->pfx_onlinkflg = 1;
		pfx->pfx_autoconfflg = 1;
		pfx->pfx_origin = PREFIX_FROM_KERNEL;
		pfx->pfx_rainfo = rai;

		/* link into chain */
		TAILQ_INSERT_TAIL(&rai->rai_prefix, pfx, pfx_next);

		/* counter increment */
		rai->rai_pfxs++;
	}

	freeifaddrs(ifap);
}

static void
makeentry(char *buf, size_t len, int id, const char *string)
{

	if (id < 0)
		strlcpy(buf, string, len);
	else
		snprintf(buf, len, "%s%d", string, id);
}

/*
 * Add a prefix to the list of specified interface and reconstruct
 * the outgoing packet.
 * The prefix must not be in the list.
 * XXX: other parameters of the prefix (e.g. lifetime) should be
 * able to be specified.
 */
static void
add_prefix(struct rainfo *rai, struct in6_prefixreq *ipr)
{
	struct prefix *pfx;
	struct ifinfo *ifi;
	char ntopbuf[INET6_ADDRSTRLEN];

	ifi = rai->rai_ifinfo;
	ELM_MALLOC(pfx, return);
	pfx->pfx_prefix = ipr->ipr_prefix.sin6_addr;
	pfx->pfx_prefixlen = ipr->ipr_plen;
	pfx->pfx_validlifetime = ipr->ipr_vltime;
	pfx->pfx_preflifetime = ipr->ipr_pltime;
	pfx->pfx_onlinkflg = ipr->ipr_raf_onlink;
	pfx->pfx_autoconfflg = ipr->ipr_raf_auto;
	pfx->pfx_origin = PREFIX_FROM_DYNAMIC;
	pfx->pfx_rainfo = rai;

	TAILQ_INSERT_TAIL(&rai->rai_prefix, pfx, pfx_next);

	syslog(LOG_DEBUG, "<%s> new prefix %s/%d was added on %s",
	    __func__,
	    inet_ntop(AF_INET6, &ipr->ipr_prefix.sin6_addr, ntopbuf,
		sizeof(ntopbuf)), ipr->ipr_plen, ifi->ifi_ifname);

	rai->rai_pfxs++;
}

/*
 * Delete a prefix to the list of specified interface and reconstruct
 * the outgoing packet.
 * The prefix must be in the list.
 */
void
delete_prefix(struct prefix *pfx)
{
	struct rainfo *rai;
	struct ifinfo *ifi;
	char ntopbuf[INET6_ADDRSTRLEN];

	rai = pfx->pfx_rainfo;
	ifi = rai->rai_ifinfo;
	TAILQ_REMOVE(&rai->rai_prefix, pfx, pfx_next);
	syslog(LOG_DEBUG, "<%s> prefix %s/%d was deleted on %s",
	    __func__,
	    inet_ntop(AF_INET6, &pfx->pfx_prefix, ntopbuf,
		sizeof(ntopbuf)), pfx->pfx_prefixlen, ifi->ifi_ifname);
	if (pfx->pfx_timer)
		rtadvd_remove_timer(pfx->pfx_timer);
	free(pfx);

	rai->rai_pfxs--;
}

void
invalidate_prefix(struct prefix *pfx)
{
	struct timespec timo;
	struct rainfo *rai;
	struct ifinfo *ifi;
	char ntopbuf[INET6_ADDRSTRLEN];

	rai = pfx->pfx_rainfo;
	ifi = rai->rai_ifinfo;
	if (pfx->pfx_timer) {	/* sanity check */
		syslog(LOG_ERR,
		    "<%s> assumption failure: timer already exists",
		    __func__);
		exit(1);
	}

	syslog(LOG_DEBUG, "<%s> prefix %s/%d was invalidated on %s, "
	    "will expire in %ld seconds", __func__,
	    inet_ntop(AF_INET6, &pfx->pfx_prefix, ntopbuf, sizeof(ntopbuf)),
	    pfx->pfx_prefixlen, ifi->ifi_ifname, (long)prefix_timo);

	/* set the expiration timer */
	pfx->pfx_timer = rtadvd_add_timer(prefix_timeout, NULL, pfx, NULL);
	if (pfx->pfx_timer == NULL) {
		syslog(LOG_ERR, "<%s> failed to add a timer for a prefix. "
		    "remove the prefix", __func__);
		delete_prefix(pfx);
	}
	timo.tv_sec = prefix_timo;
	timo.tv_nsec = 0;
	rtadvd_set_timer(&timo, pfx->pfx_timer);
}

static struct rtadvd_timer *
prefix_timeout(void *arg)
{

	delete_prefix((struct prefix *)arg);

	return (NULL);
}

void
update_prefix(struct prefix *pfx)
{
	struct rainfo *rai;
	struct ifinfo *ifi;
	char ntopbuf[INET6_ADDRSTRLEN];

	rai = pfx->pfx_rainfo;
	ifi = rai->rai_ifinfo;
	if (pfx->pfx_timer == NULL) { /* sanity check */
		syslog(LOG_ERR,
		    "<%s> assumption failure: timer does not exist",
		    __func__);
		exit(1);
	}

	syslog(LOG_DEBUG, "<%s> prefix %s/%d was re-enabled on %s",
	    __func__, inet_ntop(AF_INET6, &pfx->pfx_prefix, ntopbuf,
		sizeof(ntopbuf)), pfx->pfx_prefixlen, ifi->ifi_ifname);

	/* stop the expiration timer */
	rtadvd_remove_timer(pfx->pfx_timer);
	pfx->pfx_timer = NULL;
}

/*
 * Try to get an in6_prefixreq contents for a prefix which matches
 * ipr->ipr_prefix and ipr->ipr_plen and belongs to
 * the interface whose name is ipr->ipr_name[].
 */
static int
init_prefix(struct in6_prefixreq *ipr)
{
#if 0
	int s;

	if ((s = socket(AF_INET6, SOCK_DGRAM, 0)) < 0) {
		syslog(LOG_ERR, "<%s> socket: %s", __func__,
		    strerror(errno));
		exit(1);
	}

	if (ioctl(s, SIOCGIFPREFIX_IN6, (caddr_t)ipr) < 0) {
		syslog(LOG_INFO, "<%s> ioctl:SIOCGIFPREFIX %s", __func__,
		    strerror(errno));

		ipr->ipr_vltime = DEF_ADVVALIDLIFETIME;
		ipr->ipr_pltime = DEF_ADVPREFERREDLIFETIME;
		ipr->ipr_raf_onlink = 1;
		ipr->ipr_raf_auto = 1;
		/* omit other field initialization */
	}
	else if (ipr->ipr_origin < PR_ORIG_RR) {
		char ntopbuf[INET6_ADDRSTRLEN];

		syslog(LOG_WARNING, "<%s> Added prefix(%s)'s origin %d is"
		    "lower than PR_ORIG_RR(router renumbering)."
		    "This should not happen if I am router", __func__,
		    inet_ntop(AF_INET6, &ipr->ipr_prefix.sin6_addr, ntopbuf,
			sizeof(ntopbuf)), ipr->ipr_origin);
		close(s);
		return (1);
	}

	close(s);
	return (0);
#else
	ipr->ipr_vltime = DEF_ADVVALIDLIFETIME;
	ipr->ipr_pltime = DEF_ADVPREFERREDLIFETIME;
	ipr->ipr_raf_onlink = 1;
	ipr->ipr_raf_auto = 1;
	return (0);
#endif
}

void
make_prefix(struct rainfo *rai, int ifindex, struct in6_addr *addr, int plen)
{
	struct in6_prefixreq ipr;

	memset(&ipr, 0, sizeof(ipr));
	if (if_indextoname(ifindex, ipr.ipr_name) == NULL) {
		syslog(LOG_ERR, "<%s> Prefix added interface No.%d doesn't "
		    "exist. This should not happen! %s", __func__,
		    ifindex, strerror(errno));
		exit(1);
	}
	ipr.ipr_prefix.sin6_len = sizeof(ipr.ipr_prefix);
	ipr.ipr_prefix.sin6_family = AF_INET6;
	ipr.ipr_prefix.sin6_addr = *addr;
	ipr.ipr_plen = plen;

	if (init_prefix(&ipr))
		return; /* init failed by some error */
	add_prefix(rai, &ipr);
}

void
make_packet(struct rainfo *rai)
{
	size_t packlen, lladdroptlen = 0;
	char *buf;
	struct nd_router_advert *ra;
	struct nd_opt_prefix_info *ndopt_pi;
	struct nd_opt_mtu *ndopt_mtu;
	struct nd_opt_route_info *ndopt_rti;
	struct rtinfo *rti;
	struct nd_opt_rdnss *ndopt_rdnss;
	struct rdnss *rdn;
	struct nd_opt_dnssl *ndopt_dnssl;
	struct dnssl *dns;
	size_t len;
	struct prefix *pfx;
	struct ifinfo *ifi;

	ifi = rai->rai_ifinfo;
	/* calculate total length */
	packlen = sizeof(struct nd_router_advert);
	if (rai->rai_advlinkopt) {
		if ((lladdroptlen = lladdropt_length(&ifi->ifi_sdl)) == 0) {
			syslog(LOG_INFO,
			    "<%s> link-layer address option has"
			    " null length on %s.  Treat as not included.",
			    __func__, ifi->ifi_ifname);
			rai->rai_advlinkopt = 0;
		}
		packlen += lladdroptlen;
	}
	if (rai->rai_pfxs)
		packlen += sizeof(struct nd_opt_prefix_info) * rai->rai_pfxs;
	if (rai->rai_linkmtu)
		packlen += sizeof(struct nd_opt_mtu);

	TAILQ_FOREACH(rti, &rai->rai_route, rti_next)
		packlen += sizeof(struct nd_opt_route_info) +
			   ((rti->rti_prefixlen + 0x3f) >> 6) * 8;

	TAILQ_FOREACH(rdn, &rai->rai_rdnss, rd_next) {
		struct rdnss_addr *rdna;

		packlen += sizeof(struct nd_opt_rdnss);
		TAILQ_FOREACH(rdna, &rdn->rd_list, ra_next)
			packlen += sizeof(rdna->ra_dns);
	}
	TAILQ_FOREACH(dns, &rai->rai_dnssl, dn_next) {
		struct dnssl_addr *dnsa;

		packlen += sizeof(struct nd_opt_dnssl);
		len = 0;
		TAILQ_FOREACH(dnsa, &dns->dn_list, da_next)
			len += dnsa->da_len;

		/* A zero octet and 8 octet boundary */
		len++;
		len += (len % 8) ? 8 - len % 8 : 0;

		packlen += len;
	}
	/* allocate memory for the packet */
	if ((buf = malloc(packlen)) == NULL) {
		syslog(LOG_ERR,
		    "<%s> can't get enough memory for an RA packet",
		    __func__);
		exit(1);
	}
	memset(buf, 0, packlen);
	if (rai->rai_ra_data)	/* Free old data if any. */
		free(rai->rai_ra_data);
	rai->rai_ra_data = buf;
	/* XXX: what if packlen > 576? */
	rai->rai_ra_datalen = packlen;

	/*
	 * construct the packet
	 */
	ra = (struct nd_router_advert *)buf;
	ra->nd_ra_type = ND_ROUTER_ADVERT;
	ra->nd_ra_code = 0;
	ra->nd_ra_cksum = 0;
	ra->nd_ra_curhoplimit = (uint8_t)(0xff & rai->rai_hoplimit);
	/*
	 * XXX: the router preference field, which is a 2-bit field, should be
	 * initialized before other fields.
	 */
	ra->nd_ra_flags_reserved = 0xff & rai->rai_rtpref;
	ra->nd_ra_flags_reserved |=
		rai->rai_managedflg ? ND_RA_FLAG_MANAGED : 0;
	ra->nd_ra_flags_reserved |=
		rai->rai_otherflg ? ND_RA_FLAG_OTHER : 0;
#ifdef DRAFT_IETF_6MAN_IPV6ONLY_FLAG
	ra->nd_ra_flags_reserved |=
		rai->rai_ipv6onlyflg ? ND_RA_FLAG_IPV6_ONLY : 0;
#endif
	ra->nd_ra_router_lifetime = htons(rai->rai_lifetime);
	ra->nd_ra_reachable = htonl(rai->rai_reachabletime);
	ra->nd_ra_retransmit = htonl(rai->rai_retranstimer);
	buf += sizeof(*ra);

	if (rai->rai_advlinkopt) {
		lladdropt_fill(&ifi->ifi_sdl, (struct nd_opt_hdr *)buf);
		buf += lladdroptlen;
	}

	if (rai->rai_linkmtu) {
		ndopt_mtu = (struct nd_opt_mtu *)buf;
		ndopt_mtu->nd_opt_mtu_type = ND_OPT_MTU;
		ndopt_mtu->nd_opt_mtu_len = 1;
		ndopt_mtu->nd_opt_mtu_reserved = 0;
		ndopt_mtu->nd_opt_mtu_mtu = htonl(rai->rai_linkmtu);
		buf += sizeof(struct nd_opt_mtu);
	}

	TAILQ_FOREACH(pfx, &rai->rai_prefix, pfx_next) {
		uint32_t vltime, pltime;
		struct timespec now;

		ndopt_pi = (struct nd_opt_prefix_info *)buf;
		ndopt_pi->nd_opt_pi_type = ND_OPT_PREFIX_INFORMATION;
		ndopt_pi->nd_opt_pi_len = 4;
		ndopt_pi->nd_opt_pi_prefix_len = pfx->pfx_prefixlen;
		ndopt_pi->nd_opt_pi_flags_reserved = 0;
		if (pfx->pfx_onlinkflg)
			ndopt_pi->nd_opt_pi_flags_reserved |=
				ND_OPT_PI_FLAG_ONLINK;
		if (pfx->pfx_autoconfflg)
			ndopt_pi->nd_opt_pi_flags_reserved |=
				ND_OPT_PI_FLAG_AUTO;
		if (pfx->pfx_timer)
			vltime = 0;
		else {
			if (pfx->pfx_vltimeexpire || pfx->pfx_pltimeexpire)
				clock_gettime(CLOCK_MONOTONIC_FAST, &now);
			if (pfx->pfx_vltimeexpire == 0)
				vltime = pfx->pfx_validlifetime;
			else
				vltime = ((time_t)pfx->pfx_vltimeexpire > now.tv_sec) ?
				    pfx->pfx_vltimeexpire - now.tv_sec : 0;
		}
		if (pfx->pfx_timer)
			pltime = 0;
		else {
			if (pfx->pfx_pltimeexpire == 0)
				pltime = pfx->pfx_preflifetime;
			else
				pltime = ((time_t)pfx->pfx_pltimeexpire > now.tv_sec) ?
				    pfx->pfx_pltimeexpire - now.tv_sec : 0;
		}
		if (vltime < pltime) {
			/*
			 * this can happen if vltime is decrement but pltime
			 * is not.
			 */
			pltime = vltime;
		}
		ndopt_pi->nd_opt_pi_valid_time = htonl(vltime);
		ndopt_pi->nd_opt_pi_preferred_time = htonl(pltime);
		ndopt_pi->nd_opt_pi_reserved2 = 0;
		ndopt_pi->nd_opt_pi_prefix = pfx->pfx_prefix;

		buf += sizeof(struct nd_opt_prefix_info);
	}

	TAILQ_FOREACH(rti, &rai->rai_route, rti_next) {
		uint8_t psize = (rti->rti_prefixlen + 0x3f) >> 6;

		ndopt_rti = (struct nd_opt_route_info *)buf;
		ndopt_rti->nd_opt_rti_type = ND_OPT_ROUTE_INFO;
		ndopt_rti->nd_opt_rti_len = 1 + psize;
		ndopt_rti->nd_opt_rti_prefixlen = rti->rti_prefixlen;
		ndopt_rti->nd_opt_rti_flags = 0xff & rti->rti_rtpref;
		ndopt_rti->nd_opt_rti_lifetime = htonl(rti->rti_ltime);
		memcpy(ndopt_rti + 1, &rti->rti_prefix, psize * 8);
		buf += sizeof(struct nd_opt_route_info) + psize * 8;
	}

	TAILQ_FOREACH(rdn, &rai->rai_rdnss, rd_next) {
		struct rdnss_addr *rdna;

		ndopt_rdnss = (struct nd_opt_rdnss *)buf;
		ndopt_rdnss->nd_opt_rdnss_type = ND_OPT_RDNSS;
		ndopt_rdnss->nd_opt_rdnss_len = 0;
		ndopt_rdnss->nd_opt_rdnss_reserved = 0;
		ndopt_rdnss->nd_opt_rdnss_lifetime = htonl(rdn->rd_ltime);
		buf += sizeof(struct nd_opt_rdnss);

		TAILQ_FOREACH(rdna, &rdn->rd_list, ra_next) {
			memcpy(buf, &rdna->ra_dns, sizeof(rdna->ra_dns));
			buf += sizeof(rdna->ra_dns);
		}
		/* Length field should be in 8 octets */
		ndopt_rdnss->nd_opt_rdnss_len = (buf - (char *)ndopt_rdnss) / 8;

		syslog(LOG_DEBUG, "<%s>: nd_opt_dnss_len = %d", __func__,
		    ndopt_rdnss->nd_opt_rdnss_len);
	}

	TAILQ_FOREACH(dns, &rai->rai_dnssl, dn_next) {
		struct dnssl_addr *dnsa;

		ndopt_dnssl = (struct nd_opt_dnssl *)buf;
		ndopt_dnssl->nd_opt_dnssl_type = ND_OPT_DNSSL;
		ndopt_dnssl->nd_opt_dnssl_len = 0;
		ndopt_dnssl->nd_opt_dnssl_reserved = 0;
		ndopt_dnssl->nd_opt_dnssl_lifetime = htonl(dns->dn_ltime);
		buf += sizeof(*ndopt_dnssl);

		TAILQ_FOREACH(dnsa, &dns->dn_list, da_next) {
			memcpy(buf, dnsa->da_dom, dnsa->da_len);
			buf += dnsa->da_len;
		}

		/* A zero octet after encoded DNS server list. */
		*buf++ = '\0';

		/* Padding to next 8 octets boundary */
		len = buf - (char *)ndopt_dnssl;
		len += (len % 8) ? 8 - len % 8 : 0;
		buf = (char *)ndopt_dnssl + len;

		/* Length field must be in 8 octets */
		ndopt_dnssl->nd_opt_dnssl_len = len / 8;

		syslog(LOG_DEBUG, "<%s>: nd_opt_dnssl_len = %d", __func__,
		    ndopt_dnssl->nd_opt_dnssl_len);
	}
	return;
}
