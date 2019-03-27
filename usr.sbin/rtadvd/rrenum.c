/*	$FreeBSD$	*/
/*	$KAME: rrenum.c,v 1.12 2002/06/10 19:59:47 itojun Exp $	*/

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
 */
#include <sys/types.h>
#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/route.h>
#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet/icmp6.h>

#include <arpa/inet.h>

#include <errno.h>
#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <syslog.h>
#include "rtadvd.h"
#include "rrenum.h"
#include "if.h"

#define	RR_ISSET_SEGNUM(segnum_bits, segnum) \
	((((segnum_bits)[(segnum) >> 5]) & (1 << ((segnum) & 31))) != 0)
#define	RR_SET_SEGNUM(segnum_bits, segnum) \
	(((segnum_bits)[(segnum) >> 5]) |= (1 << ((segnum) & 31)))

struct rr_operation {
	u_long	rro_seqnum;
	u_long	rro_segnum_bits[8];
};

static struct rr_operation rro;
static int rr_rcvifindex;
static int rrcmd2pco[RPM_PCO_MAX] = {
	0,
	SIOCAIFPREFIX_IN6,
	SIOCCIFPREFIX_IN6,
	SIOCSGIFPREFIX_IN6
};
static int s = -1;

/*
 * Check validity of a Prefix Control Operation(PCO).
 * return 0 on success, 1 on failure.
 */
static int
rr_pco_check(int len, struct rr_pco_match *rpm)
{
	struct rr_pco_use *rpu, *rpulim;
	int checklen;

	/* rpm->rpm_len must be (4N * 3) as router-renum-05.txt */
	if ((rpm->rpm_len - 3) < 0 || /* must be at least 3 */
	    (rpm->rpm_len - 3) & 0x3) { /* must be multiple of 4 */
		syslog(LOG_WARNING, "<%s> rpm_len %d is not 4N * 3",
		    __func__, rpm->rpm_len);
		return (1);
	}
	/* rpm->rpm_code must be valid value */
	switch (rpm->rpm_code) {
	case RPM_PCO_ADD:
	case RPM_PCO_CHANGE:
	case RPM_PCO_SETGLOBAL:
		break;
	default:
		syslog(LOG_WARNING, "<%s> unknown rpm_code %d", __func__,
		    rpm->rpm_code);
		return (1);
	}
	/* rpm->rpm_matchlen must be 0 to 128 inclusive */
	if (rpm->rpm_matchlen > 128) {
		syslog(LOG_WARNING, "<%s> rpm_matchlen %d is over 128",
		    __func__, rpm->rpm_matchlen);
		return (1);
	}

	/*
	 * rpu->rpu_uselen, rpu->rpu_keeplen, and sum of them must be
	 * between 0 and 128 inclusive
	 */
	for (rpu = (struct rr_pco_use *)(rpm + 1),
	     rpulim = (struct rr_pco_use *)((char *)rpm + len);
	     rpu < rpulim;
	     rpu += 1) {
		checklen = rpu->rpu_uselen;
		checklen += rpu->rpu_keeplen;
		/*
		 * omit these check, because either of rpu_uselen
		 * and rpu_keeplen is unsigned char
		 *  (128 > rpu_uselen > 0)
		 *  (128 > rpu_keeplen > 0)
		 *  (rpu_uselen + rpu_keeplen > 0)
		 */
		if (checklen > 128) {
			syslog(LOG_WARNING, "<%s> sum of rpu_uselen %d and"
			    " rpu_keeplen %d is %d(over 128)",
			    __func__, rpu->rpu_uselen, rpu->rpu_keeplen,
			    rpu->rpu_uselen + rpu->rpu_keeplen);
			return (1);
		}
	}
	return (0);
}

static void
do_use_prefix(int len, struct rr_pco_match *rpm,
	struct in6_rrenumreq *irr, int ifindex)
{
	struct rr_pco_use *rpu, *rpulim;
	struct rainfo *rai;
	struct ifinfo *ifi;
	struct prefix *pfx;

	rpu = (struct rr_pco_use *)(rpm + 1);
	rpulim = (struct rr_pco_use *)((char *)rpm + len);

	if (rpu == rpulim) {	/* no use prefix */
		if (rpm->rpm_code == RPM_PCO_ADD)
			return;

		irr->irr_u_uselen = 0;
		irr->irr_u_keeplen = 0;
		irr->irr_raf_mask_onlink = 0;
		irr->irr_raf_mask_auto = 0;
		irr->irr_vltime = 0;
		irr->irr_pltime = 0;
		memset(&irr->irr_flags, 0, sizeof(irr->irr_flags));
		irr->irr_useprefix.sin6_len = 0; /* let it mean, no addition */
		irr->irr_useprefix.sin6_family = 0;
		irr->irr_useprefix.sin6_addr = in6addr_any;
		if (ioctl(s, rrcmd2pco[rpm->rpm_code], (caddr_t)irr) < 0 &&
		    errno != EADDRNOTAVAIL)
			syslog(LOG_ERR, "<%s> ioctl: %s", __func__,
			    strerror(errno));
		return;
	}

	for (rpu = (struct rr_pco_use *)(rpm + 1),
	     rpulim = (struct rr_pco_use *)((char *)rpm + len);
	     rpu < rpulim;
	     rpu += 1) {
		/* init in6_rrenumreq fields */
		irr->irr_u_uselen = rpu->rpu_uselen;
		irr->irr_u_keeplen = rpu->rpu_keeplen;
		irr->irr_raf_mask_onlink =
		    !!(rpu->rpu_ramask & ICMP6_RR_PCOUSE_RAFLAGS_ONLINK);
		irr->irr_raf_mask_auto =
		    !!(rpu->rpu_ramask & ICMP6_RR_PCOUSE_RAFLAGS_AUTO);
		irr->irr_vltime = ntohl(rpu->rpu_vltime);
		irr->irr_pltime = ntohl(rpu->rpu_pltime);
		irr->irr_raf_onlink =
		    (rpu->rpu_raflags & ICMP6_RR_PCOUSE_RAFLAGS_ONLINK) == 0 ?
		    0 : 1;
		irr->irr_raf_auto =
		    (rpu->rpu_raflags & ICMP6_RR_PCOUSE_RAFLAGS_AUTO) == 0 ?
		    0 : 1;
		irr->irr_rrf_decrvalid =
		    (rpu->rpu_flags & ICMP6_RR_PCOUSE_FLAGS_DECRVLTIME) == 0 ?
		    0 : 1;
		irr->irr_rrf_decrprefd =
		    (rpu->rpu_flags & ICMP6_RR_PCOUSE_FLAGS_DECRPLTIME) == 0 ?
		    0 : 1;
		irr->irr_useprefix.sin6_len = sizeof(irr->irr_useprefix);
		irr->irr_useprefix.sin6_family = AF_INET6;
		irr->irr_useprefix.sin6_addr = rpu->rpu_prefix;

		if (ioctl(s, rrcmd2pco[rpm->rpm_code], (caddr_t)irr) < 0 &&
		    errno != EADDRNOTAVAIL)
			syslog(LOG_ERR, "<%s> ioctl: %s", __func__,
			    strerror(errno));

		/* very adhoc: should be rewritten */
		if (rpm->rpm_code == RPM_PCO_CHANGE &&
		    IN6_ARE_ADDR_EQUAL(&rpm->rpm_prefix, &rpu->rpu_prefix) &&
		    rpm->rpm_matchlen == rpu->rpu_uselen &&
		    rpu->rpu_uselen == rpu->rpu_keeplen) {
			ifi = if_indextoifinfo(ifindex);
			if (ifi == NULL || ifi->ifi_rainfo == NULL)
				continue; /* non-advertising IF */
			rai = ifi->ifi_rainfo;

			TAILQ_FOREACH(pfx, &rai->rai_prefix, pfx_next) {
				struct timespec now;

				if (prefix_match(&pfx->pfx_prefix,
				    pfx->pfx_prefixlen, &rpm->rpm_prefix,
				    rpm->rpm_matchlen)) {
					/* change parameters */
					pfx->pfx_validlifetime =
					    ntohl(rpu->rpu_vltime);
					pfx->pfx_preflifetime =
					    ntohl(rpu->rpu_pltime);
					if (irr->irr_rrf_decrvalid) {
						clock_gettime(CLOCK_MONOTONIC_FAST,
						    &now);
						pfx->pfx_vltimeexpire =
						    now.tv_sec +
						    pfx->pfx_validlifetime;
					} else
						pfx->pfx_vltimeexpire = 0;
					if (irr->irr_rrf_decrprefd) {
						clock_gettime(CLOCK_MONOTONIC_FAST,
						    &now);
						pfx->pfx_pltimeexpire =
						    now.tv_sec +
						    pfx->pfx_preflifetime;
					} else
						pfx->pfx_pltimeexpire = 0;
				}
			}
		}
	}
}

/*
 * process a Prefix Control Operation(PCO).
 * return 0 on success, 1 on failure
 */
static int
do_pco(struct icmp6_router_renum *rr, int len, struct rr_pco_match *rpm)
{
	int ifindex = 0;
	struct in6_rrenumreq irr;
	struct ifinfo *ifi;
	
	if ((rr_pco_check(len, rpm) != 0))
		return (1);

	if (s == -1 && (s = socket(AF_INET6, SOCK_DGRAM, 0)) < 0) {
		syslog(LOG_ERR, "<%s> socket: %s", __func__,
		    strerror(errno));
		exit(1);
	}

	memset(&irr, 0, sizeof(irr));
	irr.irr_origin = PR_ORIG_RR;
	irr.irr_m_len = rpm->rpm_matchlen;
	irr.irr_m_minlen = rpm->rpm_minlen;
	irr.irr_m_maxlen = rpm->rpm_maxlen;
	irr.irr_matchprefix.sin6_len = sizeof(irr.irr_matchprefix);
	irr.irr_matchprefix.sin6_family = AF_INET6;
	irr.irr_matchprefix.sin6_addr = rpm->rpm_prefix;

	while (if_indextoname(++ifindex, irr.irr_name)) {
		ifi = if_indextoifinfo(ifindex);
		if (ifi == NULL) {
			syslog(LOG_ERR, "<%s> ifindex not found.",
			    __func__);
			return (1);
		}
		/*
		 * if ICMP6_RR_FLAGS_FORCEAPPLY(A flag) is 0 and
		 * IFF_UP is off, the interface is not applied
		 */
		if ((rr->rr_flags & ICMP6_RR_FLAGS_FORCEAPPLY) == 0 &&
		    (ifi->ifi_flags & IFF_UP) == 0)
			continue;
		/* TODO: interface scope check */
		do_use_prefix(len, rpm, &irr, ifindex);
	}
	if (errno == ENXIO)
		return (0);
	else if (errno) {
		syslog(LOG_ERR, "<%s> if_indextoname: %s", __func__,
		    strerror(errno));
		return (1);
	}
	return (0);
}

/*
 * call do_pco() for each Prefix Control Operations(PCOs) in a received
 * Router Renumbering Command packet.
 * return 0 on success, 1 on failure
 */
static int
do_rr(int len, struct icmp6_router_renum *rr)
{
	struct rr_pco_match *rpm;
	char *cp, *lim;

	lim = (char *)rr + len;
	cp = (char *)(rr + 1);
	len -= sizeof(struct icmp6_router_renum);

	update_ifinfo(&ifilist, UPDATE_IFINFO_ALL);

	while (cp < lim) {
		int rpmlen;

		rpm = (struct rr_pco_match *)cp;
		if ((size_t)len < sizeof(struct rr_pco_match)) {
		    tooshort:
			syslog(LOG_ERR, "<%s> pkt too short. left len = %d. "
			    "garbage at end of pkt?", __func__, len);
			return (1);
		}
		rpmlen = rpm->rpm_len << 3;
		if (len < rpmlen)
			goto tooshort;

		if (do_pco(rr, rpmlen, rpm)) {
			syslog(LOG_WARNING, "<%s> invalid PCO", __func__);
			goto next;
		}

	    next:
		cp += rpmlen;
		len -= rpmlen;
	}

	return (0);
}

/*
 * check validity of a router renumbering command packet
 * return 0 on success, 1 on failure
 */
static int
rr_command_check(int len, struct icmp6_router_renum *rr, struct in6_addr *from,
	struct in6_addr *dst)
{
	u_char ntopbuf[INET6_ADDRSTRLEN];

	/* omit rr minimal length check. hope kernel have done it. */
	/* rr_command length check */
	if ((size_t)len < (sizeof(struct icmp6_router_renum) +
	    sizeof(struct rr_pco_match))) {
		syslog(LOG_ERR,	"<%s> rr_command len %d is too short",
		    __func__, len);
		return (1);
	}

	/* destination check. only for multicast. omit unicast check. */
	if (IN6_IS_ADDR_MULTICAST(dst) && !IN6_IS_ADDR_MC_LINKLOCAL(dst) &&
	    !IN6_IS_ADDR_MC_SITELOCAL(dst)) {
		syslog(LOG_ERR,	"<%s> dst mcast addr %s is illegal",
		    __func__,
		    inet_ntop(AF_INET6, dst, ntopbuf, sizeof(ntopbuf)));
		return (1);
	}

	/* seqnum and segnum check */
	if (rro.rro_seqnum > rr->rr_seqnum) {
		syslog(LOG_WARNING,
		    "<%s> rcvd old seqnum %d from %s",
		    __func__, (u_int32_t)ntohl(rr->rr_seqnum),
		   inet_ntop(AF_INET6, from, ntopbuf, sizeof(ntopbuf)));
		return (1);
	}
	if (rro.rro_seqnum == rr->rr_seqnum &&
	    (rr->rr_flags & ICMP6_RR_FLAGS_TEST) == 0 &&
	    RR_ISSET_SEGNUM(rro.rro_segnum_bits, rr->rr_segnum)) {
		if ((rr->rr_flags & ICMP6_RR_FLAGS_REQRESULT) != 0)
			syslog(LOG_WARNING,
			    "<%s> rcvd duped segnum %d from %s",
			    __func__, rr->rr_segnum, inet_ntop(AF_INET6, from,
				ntopbuf, sizeof(ntopbuf)));
		return (0);
	}

	/* update seqnum */
	if (rro.rro_seqnum != rr->rr_seqnum) {
		/* then must be "<" */

		/* init rro_segnum_bits */
		memset(rro.rro_segnum_bits, 0,
		    sizeof(rro.rro_segnum_bits));
	}
	rro.rro_seqnum = rr->rr_seqnum;

	return (0);
}

static void
rr_command_input(int len, struct icmp6_router_renum *rr,
	struct in6_addr *from, struct in6_addr *dst)
{
	/* rr_command validity check */
	if (rr_command_check(len, rr, from, dst))
		goto failed;
	if ((rr->rr_flags & (ICMP6_RR_FLAGS_TEST|ICMP6_RR_FLAGS_REQRESULT)) ==
	    ICMP6_RR_FLAGS_TEST)
		return;

	/* do router renumbering */
	if (do_rr(len, rr))
		goto failed;

	/* update segnum */
	RR_SET_SEGNUM(rro.rro_segnum_bits, rr->rr_segnum);

	return;

    failed:
	syslog(LOG_ERR, "<%s> received RR was invalid", __func__);
	return;
}

void
rr_input(int len, struct icmp6_router_renum *rr, struct in6_pktinfo *pi,
	struct sockaddr_in6 *from, struct in6_addr *dst)
{
	u_char ntopbuf[2][INET6_ADDRSTRLEN], ifnamebuf[IFNAMSIZ];

	syslog(LOG_DEBUG,
	    "<%s> RR received from %s to %s on %s",
	    __func__,
	    inet_ntop(AF_INET6, &from->sin6_addr, ntopbuf[0] ,sizeof(ntopbuf[0])),
	    inet_ntop(AF_INET6, &dst, ntopbuf[1], sizeof(ntopbuf[1])),
	    if_indextoname(pi->ipi6_ifindex, ifnamebuf));

	/* packet validation based on Section 4.1 of RFC2894 */
	if ((size_t)len < sizeof(struct icmp6_router_renum)) {
		syslog(LOG_NOTICE,
		    "<%s>: RR short message (size %d) from %s to %s on %s",
		    __func__, len,
		    inet_ntop(AF_INET6, &from->sin6_addr, ntopbuf[0],
			sizeof(ntopbuf[0])),
		    inet_ntop(AF_INET6, &dst, ntopbuf[1], sizeof(ntopbuf[1])),
		    if_indextoname(pi->ipi6_ifindex, ifnamebuf));
		return;
	}

	/*
	 * If the IPv6 destination address is neither an All Routers multicast
	 * address [AARCH] nor one of the receiving router's unicast addresses,
	 * the message MUST be discarded and SHOULD be logged to network
	 * management.
	 * We rely on the kernel input routine for unicast addresses, and thus
	 * check multicast destinations only.
	 */
	if (IN6_IS_ADDR_MULTICAST(&pi->ipi6_addr) && !IN6_ARE_ADDR_EQUAL(
	    &sin6_sitelocal_allrouters.sin6_addr, &pi->ipi6_addr)) {
		syslog(LOG_NOTICE,
		    "<%s>: RR message with invalid destination (%s) "
		    "from %s on %s",
		    __func__,
		    inet_ntop(AF_INET6, &dst, ntopbuf[0], sizeof(ntopbuf[0])),
		    inet_ntop(AF_INET6, &from->sin6_addr, ntopbuf[1],
			      sizeof(ntopbuf[1])),
		    if_indextoname(pi->ipi6_ifindex, ifnamebuf));
		return;
	}

	rr_rcvifindex = pi->ipi6_ifindex;

	switch (rr->rr_code) {
	case ICMP6_ROUTER_RENUMBERING_COMMAND:
		rr_command_input(len, rr, &from->sin6_addr, dst);
		/* TODO: send reply msg */
		break;
	case ICMP6_ROUTER_RENUMBERING_RESULT:
		/* RESULT will be processed by rrenumd */
		break;
	case ICMP6_ROUTER_RENUMBERING_SEQNUM_RESET:
		/* TODO: sequence number reset */
		break;
	default:
		syslog(LOG_ERR,	"<%s> received unknown code %d",
		    __func__, rr->rr_code);
		break;

	}

	return;
}
