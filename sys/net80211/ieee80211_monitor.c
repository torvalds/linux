/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007-2008 Sam Leffler, Errno Consulting
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
#ifdef __FreeBSD__
__FBSDID("$FreeBSD$");
#endif

/*
 * IEEE 802.11 Monitor mode support.
 */
#include "opt_inet.h"
#include "opt_wlan.h"

#include <sys/param.h>
#include <sys/systm.h> 
#include <sys/mbuf.h>   
#include <sys/malloc.h>
#include <sys/kernel.h>

#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/endian.h>
#include <sys/errno.h>
#include <sys/proc.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_media.h>
#include <net/if_llc.h>
#include <net/ethernet.h>

#include <net/bpf.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_monitor.h>

static void monitor_vattach(struct ieee80211vap *);
static int monitor_newstate(struct ieee80211vap *, enum ieee80211_state, int);
static int monitor_input(struct ieee80211_node *ni, struct mbuf *m,
	const struct ieee80211_rx_stats *rxs, int rssi, int nf);

void
ieee80211_monitor_attach(struct ieee80211com *ic)
{
	ic->ic_vattach[IEEE80211_M_MONITOR] = monitor_vattach;
}

void
ieee80211_monitor_detach(struct ieee80211com *ic)
{
}

static void
monitor_vdetach(struct ieee80211vap *vap)
{
}

static void
monitor_vattach(struct ieee80211vap *vap)
{
	vap->iv_newstate = monitor_newstate;
	vap->iv_input = monitor_input;
	vap->iv_opdetach = monitor_vdetach;
}

/*
 * IEEE80211_M_MONITOR vap state machine handler.
 */
static int
monitor_newstate(struct ieee80211vap *vap, enum ieee80211_state nstate, int arg)
{
	struct ieee80211com *ic = vap->iv_ic;
	enum ieee80211_state ostate;

	IEEE80211_LOCK_ASSERT(ic);

	ostate = vap->iv_state;
	IEEE80211_DPRINTF(vap, IEEE80211_MSG_STATE, "%s: %s -> %s (%d)\n",
	    __func__, ieee80211_state_name[ostate],
	    ieee80211_state_name[nstate], arg);
	vap->iv_state = nstate;			/* state transition */
	if (nstate == IEEE80211_S_RUN) {
		switch (ostate) {
		case IEEE80211_S_INIT:
			ieee80211_create_ibss(vap, ic->ic_curchan);
			break;
		default:
			break;
		}
		/*
		 * NB: this shouldn't be here but many people use
		 * monitor mode for raw packets; once we switch
		 * them over to adhoc demo mode remove this.
		 */
		ieee80211_node_authorize(vap->iv_bss);
	}
	return 0;
}

/*
 * Process a received frame in monitor mode.
 */
static int
monitor_input(struct ieee80211_node *ni, struct mbuf *m,
    const struct ieee80211_rx_stats *rxs, int rssi, int nf)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct ifnet *ifp = vap->iv_ifp;

	if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);

	if (ieee80211_radiotap_active_vap(vap))
		ieee80211_radiotap_rx(vap, m);
	m_freem(m);
	return -1;
}
