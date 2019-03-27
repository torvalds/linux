/*-
 * SPDX-License-Identifier: BSD-4-Clause AND BSD-3-Clause
 *
 * Copyright (C) 1998 WIDE Project.
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
/*-
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

#ifdef INET6
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>
#include <sys/protosw.h>
#include <sys/mbuf.h>
#include <sys/time.h>

#include <net/if.h>
#include <net/route.h>

#include <netinet/in.h>

#include <err.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <libxo/xo.h>

#define	KERNEL 1
#include <netinet6/ip6_mroute.h>
#undef KERNEL

#include "netstat.h"

#define	WID_ORG	(Wflag ? 39 : (numeric_addr ? 29 : 18)) /* width of origin column */
#define	WID_GRP	(Wflag ? 18 : (numeric_addr ? 16 : 18)) /* width of group column */

void
mroute6pr()
{
	struct mf6c *mf6ctable[MF6CTBLSIZ], *mfcp;
	struct mif6_sctl mif6table[MAXMIFS];
	struct mf6c mfc;
	struct rtdetq rte, *rtep;
	struct mif6_sctl *mifp;
	mifi_t mifi;
	int i;
	int banner_printed;
	int saved_numeric_addr;
	mifi_t maxmif = 0;
	long int waitings;
	size_t len;

	if (live == 0)
		return;

	len = sizeof(mif6table);
	if (sysctlbyname("net.inet6.ip6.mif6table", mif6table, &len, NULL, 0) <
	    0) {
		xo_warn("sysctl: net.inet6.ip6.mif6table");
		return;
	}

	saved_numeric_addr = numeric_addr;
	numeric_addr = 1;
	banner_printed = 0;

	for (mifi = 0, mifp = mif6table; mifi < MAXMIFS; ++mifi, ++mifp) {
		char ifname[IFNAMSIZ];

		if (mifp->m6_ifp == 0)
			continue;

		maxmif = mifi;
		if (!banner_printed) {
			xo_open_list("multicast-interface");
			xo_emit("\n{T:IPv6 Multicast Interface Table}\n"
			    "{T: Mif   Rate   PhyIF   Pkts-In   Pkts-Out}\n");
			banner_printed = 1;
		}

		xo_open_instance("multicast-interface");
		xo_emit("  {:mif/%2u}   {:rate-limit/%4d}",
		    mifi, mifp->m6_rate_limit);
		xo_emit("   {:ifname/%5s}", (mifp->m6_flags & MIFF_REGISTER) ?
		    "reg0" : if_indextoname(mifp->m6_ifp, ifname));

		xo_emit(" {:received-packets/%9ju}  {:sent-packets/%9ju}\n",
		    (uintmax_t)mifp->m6_pkt_in,
		    (uintmax_t)mifp->m6_pkt_out);
		xo_close_instance("multicast-interface");
	}
	if (banner_printed)
		xo_open_list("multicast-interface");
	else
		xo_emit("\n{T:IPv6 Multicast Interface Table is empty}\n");

	len = sizeof(mf6ctable);
	if (sysctlbyname("net.inet6.ip6.mf6ctable", mf6ctable, &len, NULL, 0) <
	    0) {
		xo_warn("sysctl: net.inet6.ip6.mf6ctable");
		return;
	}

	banner_printed = 0;

	for (i = 0; i < MF6CTBLSIZ; ++i) {
		mfcp = mf6ctable[i];
		while(mfcp) {
			kread((u_long)mfcp, (char *)&mfc, sizeof(mfc));
			if (!banner_printed) {
				xo_open_list("multicast-forwarding-cache");
				xo_emit("\n"
				    "{T:IPv6 Multicast Forwarding Cache}\n");
				xo_emit(" {T:%-*.*s} {T:%-*.*s} {T:%s}",
				    WID_ORG, WID_ORG, "Origin",
				    WID_GRP, WID_GRP, "Group",
				    "  Packets Waits In-Mif  Out-Mifs\n");
				banner_printed = 1;
			}

			xo_open_instance("multicast-forwarding-cache");

			xo_emit(" {:origin/%-*.*s}", WID_ORG, WID_ORG,
			    routename(sin6tosa(&mfc.mf6c_origin),
			    numeric_addr));
			xo_emit(" {:group/%-*.*s}", WID_GRP, WID_GRP,
			    routename(sin6tosa(&mfc.mf6c_mcastgrp),
			    numeric_addr));
			xo_emit(" {:total-packets/%9ju}",
			    (uintmax_t)mfc.mf6c_pkt_cnt);

			for (waitings = 0, rtep = mfc.mf6c_stall; rtep; ) {
				waitings++;
				/* XXX KVM */
				kread((u_long)rtep, (char *)&rte, sizeof(rte));
				rtep = rte.next;
			}
			xo_emit("   {:waitings/%3ld}", waitings);

			if (mfc.mf6c_parent == MF6C_INCOMPLETE_PARENT)
				xo_emit(" ---   ");
			else
				xo_emit("  {:parent/%3d}   ", mfc.mf6c_parent);
			xo_open_list("mif");
			for (mifi = 0; mifi <= maxmif; mifi++) {
				if (IF_ISSET(mifi, &mfc.mf6c_ifset))
					xo_emit(" {l:%u}", mifi);
			}
			xo_close_list("mif");
			xo_emit("\n");

			mfcp = mfc.mf6c_next;
			xo_close_instance("multicast-forwarding-cache");
		}
	}
	if (banner_printed)
		xo_close_list("multicast-forwarding-cache");
	else
		xo_emit("\n{T:IPv6 Multicast Forwarding Table is empty}\n");

	xo_emit("\n");
	numeric_addr = saved_numeric_addr;
}

void
mrt6_stats()
{
	struct mrt6stat mrtstat;

	if (fetch_stats("net.inet6.ip6.mrt6stat", 0, &mrtstat,
	    sizeof(mrtstat), kread_counters) != 0)
		return;

	xo_open_container("multicast-statistics");
	xo_emit("{T:IPv6 multicast forwarding}:\n");

#define	p(f, m) if (mrtstat.f || sflag <= 1) \
	xo_emit(m, (uintmax_t)mrtstat.f, plural(mrtstat.f))
#define	p2(f, m) if (mrtstat.f || sflag <= 1) \
	xo_emit(m, (uintmax_t)mrtstat.f, plurales(mrtstat.f))

	p(mrt6s_mfc_lookups, "\t{:cache-lookups/%ju} "
	    "{N:/multicast forwarding cache lookup%s}\n");
	p2(mrt6s_mfc_misses, "\t{:cache-misses/%ju} "
	    "{N:/multicast forwarding cache miss%s}\n");
	p(mrt6s_upcalls, "\t{:upcalls/%ju} "
	    "{N:/upcall%s to multicast routing daemon}\n");
	p(mrt6s_upq_ovflw, "\t{:upcall-overflows/%ju} "
	    "{N:/upcall queue overflow%s}\n");
	p(mrt6s_upq_sockfull, "\t{:upcalls-dropped-full-buffer/%ju} "
	    "{N:/upcall%s dropped due to full socket buffer}\n");
	p(mrt6s_cache_cleanups, "\t{:cache-cleanups/%ju} "
	    "{N:/cache cleanup%s}\n");
	p(mrt6s_no_route, "\t{:dropped-no-origin/%ju} "
	    "{N:/datagram%s with no route for origin}\n");
	p(mrt6s_bad_tunnel, "\t{:dropped-bad-tunnel/%ju} "
	    "{N:/datagram%s arrived with bad tunneling}\n");
	p(mrt6s_cant_tunnel, "\t{:dropped-could-not-tunnel/%ju} "
	    "{N:/datagram%s could not be tunneled}\n");
	p(mrt6s_wrong_if, "\t{:dropped-wrong-incoming-interface/%ju} "
	    "{N:/datagram%s arrived on wrong interface}\n");
	p(mrt6s_drop_sel, "\t{:dropped-selectively/%ju} "
	    "{N:/datagram%s selectively dropped}\n");
	p(mrt6s_q_overflow, "\t{:dropped-queue-overflow/%ju} "
	    "{N:/datagram%s dropped due to queue overflow}\n");
	p(mrt6s_pkt2large, "\t{:dropped-too-large/%ju} "
	    "{N:/datagram%s dropped for being too large}\n");

#undef	p2
#undef	p
	xo_close_container("multicast-statistics");
}
#endif /*INET6*/
