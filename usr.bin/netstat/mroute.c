/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1989 Stephen Deering
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Stephen Deering of Stanford University.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 *	@(#)mroute.c	8.2 (Berkeley) 4/28/95
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Print multicast routing structures and statistics.
 *
 * MROUTING 1.0
 */

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>
#include <sys/protosw.h>
#include <sys/mbuf.h>
#include <sys/time.h>

#include <net/if.h>
#include <netinet/in.h>
#include <netinet/igmp.h>
#include <net/route.h>

#define _KERNEL 1
#include <netinet/ip_mroute.h>
#undef _KERNEL

#include <err.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <libxo/xo.h>
#include "netstat.h"
#include "nl_defs.h"

static void	print_bw_meter(struct bw_meter *, int *);
static void	print_mfc(struct mfc *, int, int *);

static void
print_bw_meter(struct bw_meter *bw_meter, int *banner_printed)
{
	char s1[256], s2[256], s3[256];
	struct timeval now, end, delta;

	gettimeofday(&now, NULL);

	if (! *banner_printed) {
		xo_open_list("bandwidth-meter");
		xo_emit(" {T:Bandwidth Meters}\n");
		xo_emit("  {T:/%-30s}", "Measured(Start|Packets|Bytes)");
		xo_emit(" {T:/%s}", "Type");
		xo_emit("  {T:/%-30s}", "Thresh(Interval|Packets|Bytes)");
		xo_emit(" {T:Remain}");
		xo_emit("\n");
		*banner_printed = 1;
	}

	xo_open_instance("bandwidth-meter");

	/* The measured values */
	if (bw_meter->bm_flags & BW_METER_UNIT_PACKETS) {
		snprintf(s1, sizeof(s1), "%ju",
		    (uintmax_t)bw_meter->bm_measured.b_packets);
		xo_emit("{e:measured-packets/%ju}",
		    (uintmax_t)bw_meter->bm_measured.b_packets);
	} else
		strcpy(s1, "?");
	if (bw_meter->bm_flags & BW_METER_UNIT_BYTES) {
		snprintf(s2, sizeof(s2), "%ju",
		    (uintmax_t)bw_meter->bm_measured.b_bytes);
		xo_emit("{e:measured-bytes/%ju}",
		    (uintmax_t)bw_meter->bm_measured.b_bytes);
	} else
		strcpy(s2, "?");
	xo_emit("  {[:-30}{:start-time/%lu.%06lu}|{q:measured-packets/%s}"
	    "|{q:measured-bytes%s}{]:}",
	    (u_long)bw_meter->bm_start_time.tv_sec,
	    (u_long)bw_meter->bm_start_time.tv_usec, s1, s2);

	/* The type of entry */
	xo_emit("  {t:type/%-3s}", (bw_meter->bm_flags & BW_METER_GEQ) ? ">=" :
	    (bw_meter->bm_flags & BW_METER_LEQ) ? "<=" : "?");

	/* The threshold values */
	if (bw_meter->bm_flags & BW_METER_UNIT_PACKETS) {
		snprintf(s1, sizeof(s1), "%ju",
		    (uintmax_t)bw_meter->bm_threshold.b_packets);
		xo_emit("{e:threshold-packets/%ju}",
		    (uintmax_t)bw_meter->bm_threshold.b_packets);
	} else
		strcpy(s1, "?");
	if (bw_meter->bm_flags & BW_METER_UNIT_BYTES) {
		snprintf(s2, sizeof(s2), "%ju",
		    (uintmax_t)bw_meter->bm_threshold.b_bytes);
		xo_emit("{e:threshold-bytes/%ju}",
		    (uintmax_t)bw_meter->bm_threshold.b_bytes);
	} else
		strcpy(s2, "?");

	xo_emit("  {[:-30}{:threshold-time/%lu.%06lu}|{q:threshold-packets/%s}"
	    "|{q:threshold-bytes%s}{]:}",
	    (u_long)bw_meter->bm_threshold.b_time.tv_sec,
	    (u_long)bw_meter->bm_threshold.b_time.tv_usec, s1, s2);

	/* Remaining time */
	timeradd(&bw_meter->bm_start_time,
		 &bw_meter->bm_threshold.b_time, &end);
	if (timercmp(&now, &end, <=)) {
		timersub(&end, &now, &delta);
		snprintf(s3, sizeof(s3), "%lu.%06lu",
			(u_long)delta.tv_sec,
			(u_long)delta.tv_usec);
	} else {
		/* Negative time */
		timersub(&now, &end, &delta);
		snprintf(s3, sizeof(s3), "-%lu.06%lu",
			(u_long)delta.tv_sec,
			(u_long)delta.tv_usec);
	}
	xo_emit(" {:remaining-time/%s}", s3);

	xo_open_instance("bandwidth-meter");

	xo_emit("\n");
}

static void
print_mfc(struct mfc *m, int maxvif, int *banner_printed)
{
	struct sockaddr_in sin;
	struct sockaddr *sa = (struct sockaddr *)&sin;
	struct bw_meter bw_meter, *bwm;
	int bw_banner_printed;
	int error;
	vifi_t vifi;

	bw_banner_printed = 0;
	memset(&sin, 0, sizeof(sin));
	sin.sin_len = sizeof(sin);
	sin.sin_family = AF_INET;

	if (! *banner_printed) {
		xo_open_list("multicast-forwarding-entry");
		xo_emit("\n{T:IPv4 Multicast Forwarding Table}\n"
		    " {T:Origin}          {T:Group}            "
		    " {T:Packets In-Vif}  {T:Out-Vifs:Ttls}\n");
		*banner_printed = 1;
	}

	memcpy(&sin.sin_addr, &m->mfc_origin, sizeof(sin.sin_addr));
	xo_emit(" {:origin-address/%-15.15s}", routename(sa, numeric_addr));
	memcpy(&sin.sin_addr, &m->mfc_mcastgrp, sizeof(sin.sin_addr));
	xo_emit(" {:group-address/%-15.15s}",
	    routename(sa, numeric_addr));
	xo_emit(" {:sent-packets/%9lu}", m->mfc_pkt_cnt);
	xo_emit("  {:parent/%3d}   ", m->mfc_parent);
	xo_open_list("vif-ttl");
	for (vifi = 0; vifi <= maxvif; vifi++) {
		if (m->mfc_ttls[vifi] > 0) {
			xo_open_instance("vif-ttl");
			xo_emit(" {k:vif/%u}:{:ttl/%u}", vifi,
			    m->mfc_ttls[vifi]);
			xo_close_instance("vif-ttl");
		}
	}
	xo_close_list("vif-ttl");
	xo_emit("\n");

	/*
	 * XXX We break the rules and try to use KVM to read the
	 * bandwidth meters, they are not retrievable via sysctl yet.
	 */
	bwm = m->mfc_bw_meter;
	while (bwm != NULL) {
		error = kread((u_long)bwm, (char *)&bw_meter,
		    sizeof(bw_meter));
		if (error)
			break;
		print_bw_meter(&bw_meter, &bw_banner_printed);
		bwm = bw_meter.bm_mfc_next;
	}
	if (banner_printed)
		xo_close_list("bandwidth-meter");
}

void
mroutepr()
{
	struct sockaddr_in sin;
	struct sockaddr *sa = (struct sockaddr *)&sin;
	struct vif viftable[MAXVIFS];
	struct vif *v;
	struct mfc *m;
	u_long pmfchashtbl, pmfctablesize, pviftbl;
	int banner_printed;
	int saved_numeric_addr;
	size_t len;
	vifi_t vifi, maxvif;

	saved_numeric_addr = numeric_addr;
	numeric_addr = 1;

	memset(&sin, 0, sizeof(sin));
	sin.sin_len = sizeof(sin);
	sin.sin_family = AF_INET;

	/*
	 * TODO:
	 * The VIF table will move to hanging off the struct if_info for
	 * each IPv4 configured interface. Currently it is statically
	 * allocated, and retrieved either using KVM or an opaque SYSCTL.
	 *
	 * This can't happen until the API documented in multicast(4)
	 * is itself refactored. The historical reason why VIFs use
	 * a separate ifindex space is entirely due to the legacy
	 * capability of the MROUTING code to create IPIP tunnels on
	 * the fly to support DVMRP. When gif(4) became available, this
	 * functionality was deprecated, as PIM does not use it.
	 */
	maxvif = 0;
	pmfchashtbl = pmfctablesize = pviftbl = 0;

	len = sizeof(viftable);
	if (live) {
		if (sysctlbyname("net.inet.ip.viftable", viftable, &len, NULL,
		    0) < 0) {
			xo_warn("sysctl: net.inet.ip.viftable");
			return;
		}
	} else {
		pmfchashtbl = nl[N_MFCHASHTBL].n_value;
		pmfctablesize = nl[N_MFCTABLESIZE].n_value;
		pviftbl = nl[N_VIFTABLE].n_value;

		if (pmfchashtbl == 0 || pmfctablesize == 0 || pviftbl == 0) {
			xo_warnx("No IPv4 MROUTING kernel support.");
			return;
		}

		kread(pviftbl, (char *)viftable, sizeof(viftable));
	}

	banner_printed = 0;
	for (vifi = 0, v = viftable; vifi < MAXVIFS; ++vifi, ++v) {
		if (v->v_lcl_addr.s_addr == 0)
			continue;

		maxvif = vifi;
		if (!banner_printed) {
			xo_emit("\n{T:IPv4 Virtual Interface Table\n"
			    " Vif   Thresh   Local-Address   "
			    "Remote-Address    Pkts-In   Pkts-Out}\n");
			banner_printed = 1;
			xo_open_list("vif");
		}

		xo_open_instance("vif");
		memcpy(&sin.sin_addr, &v->v_lcl_addr, sizeof(sin.sin_addr));
		xo_emit(" {:vif/%2u}    {:threshold/%6u}   {:route/%-15.15s}",
					/* opposite math of add_vif() */
		    vifi, v->v_threshold,
		    routename(sa, numeric_addr));
		memcpy(&sin.sin_addr, &v->v_rmt_addr, sizeof(sin.sin_addr));
		xo_emit(" {:source/%-15.15s}", (v->v_flags & VIFF_TUNNEL) ?
		    routename(sa, numeric_addr) : "");

		xo_emit(" {:received-packets/%9lu}  {:sent-packets/%9lu}\n",
		    v->v_pkt_in, v->v_pkt_out);
		xo_close_instance("vif");
	}
	if (banner_printed)
		xo_close_list("vif");
	else
		xo_emit("\n{T:IPv4 Virtual Interface Table is empty}\n");

	banner_printed = 0;

	/*
	 * TODO:
	 * The MFC table will move into the AF_INET radix trie in future.
	 * In 8.x, it becomes a dynamically allocated structure referenced
	 * by a hashed LIST, allowing more than 256 entries w/o kernel tuning.
	 *
	 * If retrieved via opaque SYSCTL, the kernel will coalesce it into
	 * a static table for us.
	 * If retrieved via KVM, the hash list pointers must be followed.
	 */
	if (live) {
		struct mfc *mfctable;

		len = 0;
		if (sysctlbyname("net.inet.ip.mfctable", NULL, &len, NULL,
		    0) < 0) {
			xo_warn("sysctl: net.inet.ip.mfctable");
			return;
		}

		mfctable = malloc(len);
		if (mfctable == NULL) {
			xo_warnx("malloc %lu bytes", (u_long)len);
			return;
		}
		if (sysctlbyname("net.inet.ip.mfctable", mfctable, &len, NULL,
		    0) < 0) {
			free(mfctable);
			xo_warn("sysctl: net.inet.ip.mfctable");
			return;
		}

		m = mfctable;
		while (len >= sizeof(*m)) {
			print_mfc(m++, maxvif, &banner_printed);
			len -= sizeof(*m);
		}
		if (banner_printed)
			xo_close_list("multicast-forwarding-entry");
		if (len != 0)
			xo_warnx("print_mfc: %lu trailing bytes", (u_long)len);

		free(mfctable);
	} else {
		LIST_HEAD(, mfc) *mfchashtbl;
		u_long i, mfctablesize;
		struct mfc mfc;
		int error;

		error = kread(pmfctablesize, (char *)&mfctablesize,
		    sizeof(u_long));
		if (error) {
			xo_warn("kread: mfctablesize");
			return;
		}

		len = sizeof(*mfchashtbl) * mfctablesize;
		mfchashtbl = malloc(len);
		if (mfchashtbl == NULL) {
			xo_warnx("malloc %lu bytes", (u_long)len);
			return;
		}
		kread(pmfchashtbl, (char *)&mfchashtbl, len);

		for (i = 0; i < mfctablesize; i++) {
			LIST_FOREACH(m, &mfchashtbl[i], mfc_hash) {
				kread((u_long)m, (char *)&mfc, sizeof(mfc));
				print_mfc(m, maxvif, &banner_printed);
			}
		}
		if (banner_printed)
			xo_close_list("multicast-forwarding-entry");

		free(mfchashtbl);
	}

	if (!banner_printed)
		xo_emit("\n{T:IPv4 Multicast Forwarding Table is empty}\n");

	xo_emit("\n");
	numeric_addr = saved_numeric_addr;
}

void
mrt_stats()
{
	struct mrtstat mrtstat;
	u_long mstaddr;

	mstaddr = nl[N_MRTSTAT].n_value;

	if (mstaddr == 0) {
		fprintf(stderr, "No IPv4 MROUTING kernel support.\n");
		return;
	}

	if (fetch_stats("net.inet.ip.mrtstat", mstaddr, &mrtstat,
	    sizeof(mrtstat), kread_counters) != 0)
		return;

	xo_emit("{T:IPv4 multicast forwarding}:\n");

#define	p(f, m) if (mrtstat.f || sflag <= 1) \
	xo_emit(m, (uintmax_t)mrtstat.f, plural(mrtstat.f))
#define	p2(f, m) if (mrtstat.f || sflag <= 1) \
	xo_emit(m, (uintmax_t)mrtstat.f, plurales(mrtstat.f))

	xo_open_container("multicast-statistics");

	p(mrts_mfc_lookups, "\t{:cache-lookups/%ju} "
	    "{N:/multicast forwarding cache lookup%s}\n");
	p2(mrts_mfc_misses, "\t{:cache-misses/%ju} "
	    "{N:/multicast forwarding cache miss%s}\n");
	p(mrts_upcalls, "\t{:upcalls-total/%ju} "
	    "{N:/upcall%s to multicast routing daemon}\n");
	p(mrts_upq_ovflw, "\t{:upcall-overflows/%ju} "
	    "{N:/upcall queue overflow%s}\n");
	p(mrts_upq_sockfull,
	    "\t{:upcalls-dropped-full-buffer/%ju} "
	    "{N:/upcall%s dropped due to full socket buffer}\n");
	p(mrts_cache_cleanups, "\t{:cache-cleanups/%ju} "
	    "{N:/cache cleanup%s}\n");
	p(mrts_no_route, "\t{:dropped-no-origin/%ju} "
	    "{N:/datagram%s with no route for origin}\n");
	p(mrts_bad_tunnel, "\t{:dropped-bad-tunnel/%ju} "
	    "{N:/datagram%s arrived with bad tunneling}\n");
	p(mrts_cant_tunnel, "\t{:dropped-could-not-tunnel/%ju} "
	    "{N:/datagram%s could not be tunneled}\n");
	p(mrts_wrong_if, "\t{:dropped-wrong-incoming-interface/%ju} "
	    "{N:/datagram%s arrived on wrong interface}\n");
	p(mrts_drop_sel, "\t{:dropped-selectively/%ju} "
	    "{N:/datagram%s selectively dropped}\n");
	p(mrts_q_overflow, "\t{:dropped-queue-overflow/%ju} "
	    "{N:/datagram%s dropped due to queue overflow}\n");
	p(mrts_pkt2large, "\t{:dropped-too-large/%ju} "
	    "{N:/datagram%s dropped for being too large}\n");

#undef	p2
#undef	p
}
