/*- 
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 The FreeBSD Foundation 
 * All rights reserved. 
 * 
 * This software was developed by Rui Paulo under sponsorship from the
 * FreeBSD Foundation. 
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE 
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL 
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS 
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) 
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT 
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY 
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF 
 * SUCH DAMAGE. 
 */ 
#include <sys/cdefs.h>
#ifdef __FreeBSD__
__FBSDID("$FreeBSD$");
#endif

/*
 * IEEE 802.11s Mesh Point (MBSS) support.
 *
 * Based on March 2009, D3.0 802.11s draft spec.
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

#include <net/bpf.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_media.h>
#include <net/if_llc.h>
#include <net/ethernet.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_action.h>
#ifdef IEEE80211_SUPPORT_SUPERG
#include <net80211/ieee80211_superg.h>
#endif
#include <net80211/ieee80211_input.h>
#include <net80211/ieee80211_mesh.h>

static void	mesh_rt_flush_invalid(struct ieee80211vap *);
static int	mesh_select_proto_path(struct ieee80211vap *, const char *);
static int	mesh_select_proto_metric(struct ieee80211vap *, const char *);
static void	mesh_vattach(struct ieee80211vap *);
static int	mesh_newstate(struct ieee80211vap *, enum ieee80211_state, int);
static void	mesh_rt_cleanup_cb(void *);
static void	mesh_gatemode_setup(struct ieee80211vap *);
static void	mesh_gatemode_cb(void *);
static void	mesh_linkchange(struct ieee80211_node *,
		    enum ieee80211_mesh_mlstate);
static void	mesh_checkid(void *, struct ieee80211_node *);
static uint32_t	mesh_generateid(struct ieee80211vap *);
static int	mesh_checkpseq(struct ieee80211vap *,
		    const uint8_t [IEEE80211_ADDR_LEN], uint32_t);
static void	mesh_transmit_to_gate(struct ieee80211vap *, struct mbuf *,
		    struct ieee80211_mesh_route *);
static void	mesh_forward(struct ieee80211vap *, struct mbuf *,
		    const struct ieee80211_meshcntl *);
static int	mesh_input(struct ieee80211_node *, struct mbuf *,
		    const struct ieee80211_rx_stats *rxs, int, int);
static void	mesh_recv_mgmt(struct ieee80211_node *, struct mbuf *, int,
		    const struct ieee80211_rx_stats *rxs, int, int);
static void	mesh_recv_ctl(struct ieee80211_node *, struct mbuf *, int);
static void	mesh_peer_timeout_setup(struct ieee80211_node *);
static void	mesh_peer_timeout_backoff(struct ieee80211_node *);
static void	mesh_peer_timeout_cb(void *);
static __inline void
		mesh_peer_timeout_stop(struct ieee80211_node *);
static int	mesh_verify_meshid(struct ieee80211vap *, const uint8_t *);
static int	mesh_verify_meshconf(struct ieee80211vap *, const uint8_t *);
static int	mesh_verify_meshpeer(struct ieee80211vap *, uint8_t,
    		    const uint8_t *);
uint32_t	mesh_airtime_calc(struct ieee80211_node *);

/*
 * Timeout values come from the specification and are in milliseconds.
 */
static SYSCTL_NODE(_net_wlan, OID_AUTO, mesh, CTLFLAG_RD, 0,
    "IEEE 802.11s parameters");
static int	ieee80211_mesh_gateint = -1;
SYSCTL_PROC(_net_wlan_mesh, OID_AUTO, gateint, CTLTYPE_INT | CTLFLAG_RW,
    &ieee80211_mesh_gateint, 0, ieee80211_sysctl_msecs_ticks, "I",
    "mesh gate interval (ms)");
static int ieee80211_mesh_retrytimeout = -1;
SYSCTL_PROC(_net_wlan_mesh, OID_AUTO, retrytimeout, CTLTYPE_INT | CTLFLAG_RW,
    &ieee80211_mesh_retrytimeout, 0, ieee80211_sysctl_msecs_ticks, "I",
    "Retry timeout (msec)");
static int ieee80211_mesh_holdingtimeout = -1;

SYSCTL_PROC(_net_wlan_mesh, OID_AUTO, holdingtimeout, CTLTYPE_INT | CTLFLAG_RW,
    &ieee80211_mesh_holdingtimeout, 0, ieee80211_sysctl_msecs_ticks, "I",
    "Holding state timeout (msec)");
static int ieee80211_mesh_confirmtimeout = -1;
SYSCTL_PROC(_net_wlan_mesh, OID_AUTO, confirmtimeout, CTLTYPE_INT | CTLFLAG_RW,
    &ieee80211_mesh_confirmtimeout, 0, ieee80211_sysctl_msecs_ticks, "I",
    "Confirm state timeout (msec)");
static int ieee80211_mesh_backofftimeout = -1;
SYSCTL_PROC(_net_wlan_mesh, OID_AUTO, backofftimeout, CTLTYPE_INT | CTLFLAG_RW,
    &ieee80211_mesh_backofftimeout, 0, ieee80211_sysctl_msecs_ticks, "I",
    "Backoff timeout (msec). This is to throutles peering forever when "
    "not receiving answer or is rejected by a neighbor");
static int ieee80211_mesh_maxretries = 2;
SYSCTL_INT(_net_wlan_mesh, OID_AUTO, maxretries, CTLFLAG_RW,
    &ieee80211_mesh_maxretries, 0,
    "Maximum retries during peer link establishment");
static int ieee80211_mesh_maxholding = 2;
SYSCTL_INT(_net_wlan_mesh, OID_AUTO, maxholding, CTLFLAG_RW,
    &ieee80211_mesh_maxholding, 0,
    "Maximum times we are allowed to transition to HOLDING state before "
    "backinoff during peer link establishment");

static const uint8_t broadcastaddr[IEEE80211_ADDR_LEN] =
	{ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

static	ieee80211_recv_action_func mesh_recv_action_meshpeering_open;
static	ieee80211_recv_action_func mesh_recv_action_meshpeering_confirm;
static	ieee80211_recv_action_func mesh_recv_action_meshpeering_close;
static	ieee80211_recv_action_func mesh_recv_action_meshlmetric;
static	ieee80211_recv_action_func mesh_recv_action_meshgate;

static	ieee80211_send_action_func mesh_send_action_meshpeering_open;
static	ieee80211_send_action_func mesh_send_action_meshpeering_confirm;
static	ieee80211_send_action_func mesh_send_action_meshpeering_close;
static	ieee80211_send_action_func mesh_send_action_meshlmetric;
static	ieee80211_send_action_func mesh_send_action_meshgate;

static const struct ieee80211_mesh_proto_metric mesh_metric_airtime = {
	.mpm_descr	= "AIRTIME",
	.mpm_ie		= IEEE80211_MESHCONF_METRIC_AIRTIME,
	.mpm_metric	= mesh_airtime_calc,
};

static struct ieee80211_mesh_proto_path		mesh_proto_paths[4];
static struct ieee80211_mesh_proto_metric	mesh_proto_metrics[4];

MALLOC_DEFINE(M_80211_MESH_PREQ, "80211preq", "802.11 MESH Path Request frame");
MALLOC_DEFINE(M_80211_MESH_PREP, "80211prep", "802.11 MESH Path Reply frame");
MALLOC_DEFINE(M_80211_MESH_PERR, "80211perr", "802.11 MESH Path Error frame");

/* The longer one of the lifetime should be stored as new lifetime */
#define MESH_ROUTE_LIFETIME_MAX(a, b)	(a > b ? a : b)

MALLOC_DEFINE(M_80211_MESH_RT, "80211mesh_rt", "802.11s routing table");
MALLOC_DEFINE(M_80211_MESH_GT_RT, "80211mesh_gt", "802.11s known gates table");

/*
 * Helper functions to manipulate the Mesh routing table.
 */

static struct ieee80211_mesh_route *
mesh_rt_find_locked(struct ieee80211_mesh_state *ms,
    const uint8_t dest[IEEE80211_ADDR_LEN])
{
	struct ieee80211_mesh_route *rt;

	MESH_RT_LOCK_ASSERT(ms);

	TAILQ_FOREACH(rt, &ms->ms_routes, rt_next) {
		if (IEEE80211_ADDR_EQ(dest, rt->rt_dest))
			return rt;
	}
	return NULL;
}

static struct ieee80211_mesh_route *
mesh_rt_add_locked(struct ieee80211vap *vap,
    const uint8_t dest[IEEE80211_ADDR_LEN])
{
	struct ieee80211_mesh_state *ms = vap->iv_mesh;
	struct ieee80211_mesh_route *rt;

	KASSERT(!IEEE80211_ADDR_EQ(broadcastaddr, dest),
	    ("%s: adding broadcast to the routing table", __func__));

	MESH_RT_LOCK_ASSERT(ms);

	rt = IEEE80211_MALLOC(ALIGN(sizeof(struct ieee80211_mesh_route)) +
	    ms->ms_ppath->mpp_privlen, M_80211_MESH_RT,
	    IEEE80211_M_NOWAIT | IEEE80211_M_ZERO);
	if (rt != NULL) {
		rt->rt_vap = vap;
		IEEE80211_ADDR_COPY(rt->rt_dest, dest);
		rt->rt_priv = (void *)ALIGN(&rt[1]);
		MESH_RT_ENTRY_LOCK_INIT(rt, "MBSS_RT");
		callout_init(&rt->rt_discovery, 1);
		rt->rt_updtime = ticks;	/* create time */
		TAILQ_INSERT_TAIL(&ms->ms_routes, rt, rt_next);
	}
	return rt;
}

struct ieee80211_mesh_route *
ieee80211_mesh_rt_find(struct ieee80211vap *vap,
    const uint8_t dest[IEEE80211_ADDR_LEN])
{
	struct ieee80211_mesh_state *ms = vap->iv_mesh;
	struct ieee80211_mesh_route *rt;

	MESH_RT_LOCK(ms);
	rt = mesh_rt_find_locked(ms, dest);
	MESH_RT_UNLOCK(ms);
	return rt;
}

struct ieee80211_mesh_route *
ieee80211_mesh_rt_add(struct ieee80211vap *vap,
    const uint8_t dest[IEEE80211_ADDR_LEN])
{
	struct ieee80211_mesh_state *ms = vap->iv_mesh;
	struct ieee80211_mesh_route *rt;

	KASSERT(ieee80211_mesh_rt_find(vap, dest) == NULL,
	    ("%s: duplicate entry in the routing table", __func__));
	KASSERT(!IEEE80211_ADDR_EQ(vap->iv_myaddr, dest),
	    ("%s: adding self to the routing table", __func__));

	MESH_RT_LOCK(ms);
	rt = mesh_rt_add_locked(vap, dest);
	MESH_RT_UNLOCK(ms);
	return rt;
}

/*
 * Update the route lifetime and returns the updated lifetime.
 * If new_lifetime is zero and route is timedout it will be invalidated.
 * new_lifetime is in msec
 */
int
ieee80211_mesh_rt_update(struct ieee80211_mesh_route *rt, int new_lifetime)
{
	int timesince, now;
	uint32_t lifetime = 0;

	KASSERT(rt != NULL, ("route is NULL"));

	now = ticks;
	MESH_RT_ENTRY_LOCK(rt);

	/* dont clobber a proxy entry gated by us */
	if (rt->rt_flags & IEEE80211_MESHRT_FLAGS_PROXY && rt->rt_nhops == 0) {
		MESH_RT_ENTRY_UNLOCK(rt);
		return rt->rt_lifetime;
	}

	timesince = ticks_to_msecs(now - rt->rt_updtime);
	rt->rt_updtime = now;
	if (timesince >= rt->rt_lifetime) {
		if (new_lifetime != 0) {
			rt->rt_lifetime = new_lifetime;
		}
		else {
			rt->rt_flags &= ~IEEE80211_MESHRT_FLAGS_VALID;
			rt->rt_lifetime = 0;
		}
	} else {
		/* update what is left of lifetime */
		rt->rt_lifetime = rt->rt_lifetime - timesince;
		rt->rt_lifetime  = MESH_ROUTE_LIFETIME_MAX(
			new_lifetime, rt->rt_lifetime);
	}
	lifetime = rt->rt_lifetime;
	MESH_RT_ENTRY_UNLOCK(rt);

	return lifetime;
}

/*
 * Add a proxy route (as needed) for the specified destination.
 */
void
ieee80211_mesh_proxy_check(struct ieee80211vap *vap,
    const uint8_t dest[IEEE80211_ADDR_LEN])
{
	struct ieee80211_mesh_state *ms = vap->iv_mesh;
	struct ieee80211_mesh_route *rt;

	MESH_RT_LOCK(ms);
	rt = mesh_rt_find_locked(ms, dest);
	if (rt == NULL) {
		rt = mesh_rt_add_locked(vap, dest);
		if (rt == NULL) {
			IEEE80211_NOTE_MAC(vap, IEEE80211_MSG_MESH, dest,
			    "%s", "unable to add proxy entry");
			vap->iv_stats.is_mesh_rtaddfailed++;
		} else {
			IEEE80211_NOTE_MAC(vap, IEEE80211_MSG_MESH, dest,
			    "%s", "add proxy entry");
			IEEE80211_ADDR_COPY(rt->rt_mesh_gate, vap->iv_myaddr);
			IEEE80211_ADDR_COPY(rt->rt_nexthop, vap->iv_myaddr);
			rt->rt_flags |= IEEE80211_MESHRT_FLAGS_VALID
				     |  IEEE80211_MESHRT_FLAGS_PROXY;
		}
	} else if ((rt->rt_flags & IEEE80211_MESHRT_FLAGS_VALID) == 0) {
		KASSERT(rt->rt_flags & IEEE80211_MESHRT_FLAGS_PROXY,
		    ("no proxy flag for poxy entry"));
		struct ieee80211com *ic = vap->iv_ic;
		/*
		 * Fix existing entry created by received frames from
		 * stations that have some memory of dest.  We also
		 * flush any frames held on the staging queue; delivering
		 * them is too much trouble right now.
		 */
		IEEE80211_NOTE_MAC(vap, IEEE80211_MSG_MESH, dest,
		    "%s", "fix proxy entry");
		IEEE80211_ADDR_COPY(rt->rt_nexthop, vap->iv_myaddr);
		rt->rt_flags |= IEEE80211_MESHRT_FLAGS_VALID
			     |  IEEE80211_MESHRT_FLAGS_PROXY;
		/* XXX belongs in hwmp */
		ieee80211_ageq_drain_node(&ic->ic_stageq,
		   (void *)(uintptr_t) ieee80211_mac_hash(ic, dest));
		/* XXX stat? */
	}
	MESH_RT_UNLOCK(ms);
}

static __inline void
mesh_rt_del(struct ieee80211_mesh_state *ms, struct ieee80211_mesh_route *rt)
{
	TAILQ_REMOVE(&ms->ms_routes, rt, rt_next);
	/*
	 * Grab the lock before destroying it, to be sure no one else
	 * is holding the route.
	 */
	MESH_RT_ENTRY_LOCK(rt);
	callout_drain(&rt->rt_discovery);
	MESH_RT_ENTRY_LOCK_DESTROY(rt);
	IEEE80211_FREE(rt, M_80211_MESH_RT);
}

void
ieee80211_mesh_rt_del(struct ieee80211vap *vap,
    const uint8_t dest[IEEE80211_ADDR_LEN])
{
	struct ieee80211_mesh_state *ms = vap->iv_mesh;
	struct ieee80211_mesh_route *rt, *next;

	MESH_RT_LOCK(ms);
	TAILQ_FOREACH_SAFE(rt, &ms->ms_routes, rt_next, next) {
		if (IEEE80211_ADDR_EQ(rt->rt_dest, dest)) {
			if (rt->rt_flags & IEEE80211_MESHRT_FLAGS_PROXY) {
				ms->ms_ppath->mpp_senderror(vap, dest, rt,
				    IEEE80211_REASON_MESH_PERR_NO_PROXY);
			} else {
				ms->ms_ppath->mpp_senderror(vap, dest, rt,
				    IEEE80211_REASON_MESH_PERR_DEST_UNREACH);
			}
			mesh_rt_del(ms, rt);
			MESH_RT_UNLOCK(ms);
			return;
		}
	}
	MESH_RT_UNLOCK(ms);
}

void
ieee80211_mesh_rt_flush(struct ieee80211vap *vap)
{
	struct ieee80211_mesh_state *ms = vap->iv_mesh;
	struct ieee80211_mesh_route *rt, *next;

	if (ms == NULL)
		return;
	MESH_RT_LOCK(ms);
	TAILQ_FOREACH_SAFE(rt, &ms->ms_routes, rt_next, next)
		mesh_rt_del(ms, rt);
	MESH_RT_UNLOCK(ms);
}

void
ieee80211_mesh_rt_flush_peer(struct ieee80211vap *vap,
    const uint8_t peer[IEEE80211_ADDR_LEN])
{
	struct ieee80211_mesh_state *ms = vap->iv_mesh;
	struct ieee80211_mesh_route *rt, *next;

	MESH_RT_LOCK(ms);
	TAILQ_FOREACH_SAFE(rt, &ms->ms_routes, rt_next, next) {
		if (IEEE80211_ADDR_EQ(rt->rt_nexthop, peer))
			mesh_rt_del(ms, rt);
	}
	MESH_RT_UNLOCK(ms);
}

/*
 * Flush expired routing entries, i.e. those in invalid state for
 * some time.
 */
static void
mesh_rt_flush_invalid(struct ieee80211vap *vap)
{
	struct ieee80211_mesh_state *ms = vap->iv_mesh;
	struct ieee80211_mesh_route *rt, *next;

	if (ms == NULL)
		return;
	MESH_RT_LOCK(ms);
	TAILQ_FOREACH_SAFE(rt, &ms->ms_routes, rt_next, next) {
		/* Discover paths will be deleted by their own callout */
		if (rt->rt_flags & IEEE80211_MESHRT_FLAGS_DISCOVER)
			continue;
		ieee80211_mesh_rt_update(rt, 0);
		if ((rt->rt_flags & IEEE80211_MESHRT_FLAGS_VALID) == 0)
			mesh_rt_del(ms, rt);
	}
	MESH_RT_UNLOCK(ms);
}

int
ieee80211_mesh_register_proto_path(const struct ieee80211_mesh_proto_path *mpp)
{
	int i, firstempty = -1;

	for (i = 0; i < nitems(mesh_proto_paths); i++) {
		if (strncmp(mpp->mpp_descr, mesh_proto_paths[i].mpp_descr,
		    IEEE80211_MESH_PROTO_DSZ) == 0)
			return EEXIST;
		if (!mesh_proto_paths[i].mpp_active && firstempty == -1)
			firstempty = i;
	}
	if (firstempty < 0)
		return ENOSPC;
	memcpy(&mesh_proto_paths[firstempty], mpp, sizeof(*mpp));
	mesh_proto_paths[firstempty].mpp_active = 1;
	return 0;
}

int
ieee80211_mesh_register_proto_metric(const struct
    ieee80211_mesh_proto_metric *mpm)
{
	int i, firstempty = -1;

	for (i = 0; i < nitems(mesh_proto_metrics); i++) {
		if (strncmp(mpm->mpm_descr, mesh_proto_metrics[i].mpm_descr,
		    IEEE80211_MESH_PROTO_DSZ) == 0)
			return EEXIST;
		if (!mesh_proto_metrics[i].mpm_active && firstempty == -1)
			firstempty = i;
	}
	if (firstempty < 0)
		return ENOSPC;
	memcpy(&mesh_proto_metrics[firstempty], mpm, sizeof(*mpm));
	mesh_proto_metrics[firstempty].mpm_active = 1;
	return 0;
}

static int
mesh_select_proto_path(struct ieee80211vap *vap, const char *name)
{
	struct ieee80211_mesh_state *ms = vap->iv_mesh;
	int i;

	for (i = 0; i < nitems(mesh_proto_paths); i++) {
		if (strcasecmp(mesh_proto_paths[i].mpp_descr, name) == 0) {
			ms->ms_ppath = &mesh_proto_paths[i];
			return 0;
		}
	}
	return ENOENT;
}

static int
mesh_select_proto_metric(struct ieee80211vap *vap, const char *name)
{
	struct ieee80211_mesh_state *ms = vap->iv_mesh;
	int i;

	for (i = 0; i < nitems(mesh_proto_metrics); i++) {
		if (strcasecmp(mesh_proto_metrics[i].mpm_descr, name) == 0) {
			ms->ms_pmetric = &mesh_proto_metrics[i];
			return 0;
		}
	}
	return ENOENT;
}

static void
mesh_gatemode_setup(struct ieee80211vap *vap)
{
	struct ieee80211_mesh_state *ms = vap->iv_mesh;

	/*
	 * NB: When a mesh gate is running as a ROOT it shall
	 * not send out periodic GANNs but instead mark the
	 * mesh gate flag for the corresponding proactive PREQ
	 * and RANN frames.
	 */
	if (ms->ms_flags & IEEE80211_MESHFLAGS_ROOT ||
	    (ms->ms_flags & IEEE80211_MESHFLAGS_GATE) == 0) {
		callout_drain(&ms->ms_gatetimer);
		return ;
	}
	callout_reset(&ms->ms_gatetimer, ieee80211_mesh_gateint,
	    mesh_gatemode_cb, vap);
}

static void
mesh_gatemode_cb(void *arg)
{
	struct ieee80211vap *vap = (struct ieee80211vap *)arg;
	struct ieee80211_mesh_state *ms = vap->iv_mesh;
	struct ieee80211_meshgann_ie gann;

	gann.gann_flags = 0; /* Reserved */
	gann.gann_hopcount = 0;
	gann.gann_ttl = ms->ms_ttl;
	IEEE80211_ADDR_COPY(gann.gann_addr, vap->iv_myaddr);
	gann.gann_seq = ms->ms_gateseq++;
	gann.gann_interval = ieee80211_mesh_gateint;

	IEEE80211_NOTE(vap, IEEE80211_MSG_MESH, vap->iv_bss,
	    "send broadcast GANN (seq %u)", gann.gann_seq);

	ieee80211_send_action(vap->iv_bss, IEEE80211_ACTION_CAT_MESH,
	    IEEE80211_ACTION_MESH_GANN, &gann);
	mesh_gatemode_setup(vap);
}

static void
ieee80211_mesh_init(void)
{

	memset(mesh_proto_paths, 0, sizeof(mesh_proto_paths));
	memset(mesh_proto_metrics, 0, sizeof(mesh_proto_metrics));

	/*
	 * Setup mesh parameters that depends on the clock frequency.
	 */
	ieee80211_mesh_gateint = msecs_to_ticks(10000);
	ieee80211_mesh_retrytimeout = msecs_to_ticks(40);
	ieee80211_mesh_holdingtimeout = msecs_to_ticks(40);
	ieee80211_mesh_confirmtimeout = msecs_to_ticks(40);
	ieee80211_mesh_backofftimeout = msecs_to_ticks(5000);

	/*
	 * Register action frame handlers.
	 */
	ieee80211_recv_action_register(IEEE80211_ACTION_CAT_SELF_PROT,
	    IEEE80211_ACTION_MESHPEERING_OPEN,
	    mesh_recv_action_meshpeering_open);
	ieee80211_recv_action_register(IEEE80211_ACTION_CAT_SELF_PROT,
	    IEEE80211_ACTION_MESHPEERING_CONFIRM,
	    mesh_recv_action_meshpeering_confirm);
	ieee80211_recv_action_register(IEEE80211_ACTION_CAT_SELF_PROT,
	    IEEE80211_ACTION_MESHPEERING_CLOSE,
	    mesh_recv_action_meshpeering_close);
	ieee80211_recv_action_register(IEEE80211_ACTION_CAT_MESH,
	    IEEE80211_ACTION_MESH_LMETRIC, mesh_recv_action_meshlmetric);
	ieee80211_recv_action_register(IEEE80211_ACTION_CAT_MESH,
	    IEEE80211_ACTION_MESH_GANN, mesh_recv_action_meshgate);

	ieee80211_send_action_register(IEEE80211_ACTION_CAT_SELF_PROT,
	    IEEE80211_ACTION_MESHPEERING_OPEN,
	    mesh_send_action_meshpeering_open);
	ieee80211_send_action_register(IEEE80211_ACTION_CAT_SELF_PROT,
	    IEEE80211_ACTION_MESHPEERING_CONFIRM,
	    mesh_send_action_meshpeering_confirm);
	ieee80211_send_action_register(IEEE80211_ACTION_CAT_SELF_PROT,
	    IEEE80211_ACTION_MESHPEERING_CLOSE,
	    mesh_send_action_meshpeering_close);
	ieee80211_send_action_register(IEEE80211_ACTION_CAT_MESH,
	    IEEE80211_ACTION_MESH_LMETRIC,
	    mesh_send_action_meshlmetric);
	ieee80211_send_action_register(IEEE80211_ACTION_CAT_MESH,
	    IEEE80211_ACTION_MESH_GANN,
	    mesh_send_action_meshgate);

	/*
	 * Register Airtime Link Metric.
	 */
	ieee80211_mesh_register_proto_metric(&mesh_metric_airtime);

}
SYSINIT(wlan_mesh, SI_SUB_DRIVERS, SI_ORDER_FIRST, ieee80211_mesh_init, NULL);

void
ieee80211_mesh_attach(struct ieee80211com *ic)
{
	ic->ic_vattach[IEEE80211_M_MBSS] = mesh_vattach;
}

void
ieee80211_mesh_detach(struct ieee80211com *ic)
{
}

static void
mesh_vdetach_peers(void *arg, struct ieee80211_node *ni)
{
	struct ieee80211com *ic = ni->ni_ic;
	uint16_t args[3];

	if (ni->ni_mlstate == IEEE80211_NODE_MESH_ESTABLISHED) {
		args[0] = ni->ni_mlpid;
		args[1] = ni->ni_mllid;
		args[2] = IEEE80211_REASON_PEER_LINK_CANCELED;
		ieee80211_send_action(ni,
		    IEEE80211_ACTION_CAT_SELF_PROT,
		    IEEE80211_ACTION_MESHPEERING_CLOSE,
		    args);
	}
	callout_drain(&ni->ni_mltimer);
	/* XXX belongs in hwmp */
	ieee80211_ageq_drain_node(&ic->ic_stageq,
	   (void *)(uintptr_t) ieee80211_mac_hash(ic, ni->ni_macaddr));
}

static void
mesh_vdetach(struct ieee80211vap *vap)
{
	struct ieee80211_mesh_state *ms = vap->iv_mesh;

	callout_drain(&ms->ms_cleantimer);
	ieee80211_iterate_nodes(&vap->iv_ic->ic_sta, mesh_vdetach_peers,
	    NULL);
	ieee80211_mesh_rt_flush(vap);
	MESH_RT_LOCK_DESTROY(ms);
	ms->ms_ppath->mpp_vdetach(vap);
	IEEE80211_FREE(vap->iv_mesh, M_80211_VAP);
	vap->iv_mesh = NULL;
}

static void
mesh_vattach(struct ieee80211vap *vap)
{
	struct ieee80211_mesh_state *ms;
	vap->iv_newstate = mesh_newstate;
	vap->iv_input = mesh_input;
	vap->iv_opdetach = mesh_vdetach;
	vap->iv_recv_mgmt = mesh_recv_mgmt;
	vap->iv_recv_ctl = mesh_recv_ctl;
	ms = IEEE80211_MALLOC(sizeof(struct ieee80211_mesh_state), M_80211_VAP,
	    IEEE80211_M_NOWAIT | IEEE80211_M_ZERO);
	if (ms == NULL) {
		printf("%s: couldn't alloc MBSS state\n", __func__);
		return;
	}
	vap->iv_mesh = ms;
	ms->ms_seq = 0;
	ms->ms_flags = (IEEE80211_MESHFLAGS_AP | IEEE80211_MESHFLAGS_FWD);
	ms->ms_ttl = IEEE80211_MESH_DEFAULT_TTL;
	TAILQ_INIT(&ms->ms_known_gates);
	TAILQ_INIT(&ms->ms_routes);
	MESH_RT_LOCK_INIT(ms, "MBSS");
	callout_init(&ms->ms_cleantimer, 1);
	callout_init(&ms->ms_gatetimer, 1);
	ms->ms_gateseq = 0;
	mesh_select_proto_metric(vap, "AIRTIME");
	KASSERT(ms->ms_pmetric, ("ms_pmetric == NULL"));
	mesh_select_proto_path(vap, "HWMP");
	KASSERT(ms->ms_ppath, ("ms_ppath == NULL"));
	ms->ms_ppath->mpp_vattach(vap);
}

/*
 * IEEE80211_M_MBSS vap state machine handler.
 */
static int
mesh_newstate(struct ieee80211vap *vap, enum ieee80211_state nstate, int arg)
{
	struct ieee80211_mesh_state *ms = vap->iv_mesh;
	struct ieee80211com *ic = vap->iv_ic;
	struct ieee80211_node *ni;
	enum ieee80211_state ostate;

	IEEE80211_LOCK_ASSERT(ic);

	ostate = vap->iv_state;
	IEEE80211_DPRINTF(vap, IEEE80211_MSG_STATE, "%s: %s -> %s (%d)\n",
	    __func__, ieee80211_state_name[ostate],
	    ieee80211_state_name[nstate], arg);
	vap->iv_state = nstate;		/* state transition */
	if (ostate != IEEE80211_S_SCAN)
		ieee80211_cancel_scan(vap);	/* background scan */
	ni = vap->iv_bss;			/* NB: no reference held */
	if (nstate != IEEE80211_S_RUN && ostate == IEEE80211_S_RUN) {
		callout_drain(&ms->ms_cleantimer);
		callout_drain(&ms->ms_gatetimer);
	}
	switch (nstate) {
	case IEEE80211_S_INIT:
		switch (ostate) {
		case IEEE80211_S_SCAN:
			ieee80211_cancel_scan(vap);
			break;
		case IEEE80211_S_CAC:
			ieee80211_dfs_cac_stop(vap);
			break;
		case IEEE80211_S_RUN:
			ieee80211_iterate_nodes(&ic->ic_sta,
			    mesh_vdetach_peers, NULL);
			break;
		default:
			break;
		}
		if (ostate != IEEE80211_S_INIT) {
			/* NB: optimize INIT -> INIT case */
			ieee80211_reset_bss(vap);
			ieee80211_mesh_rt_flush(vap);
		}
		break;
	case IEEE80211_S_SCAN:
		switch (ostate) {
		case IEEE80211_S_INIT:
			if (vap->iv_des_chan != IEEE80211_CHAN_ANYC &&
			    !IEEE80211_IS_CHAN_RADAR(vap->iv_des_chan) &&
			    ms->ms_idlen != 0) {
				/*
				 * Already have a channel and a mesh ID; bypass
				 * the scan and startup immediately.
				 */
				ieee80211_create_ibss(vap, vap->iv_des_chan);
				break;
			}
			/*
			 * Initiate a scan.  We can come here as a result
			 * of an IEEE80211_IOC_SCAN_REQ too in which case
			 * the vap will be marked with IEEE80211_FEXT_SCANREQ
			 * and the scan request parameters will be present
			 * in iv_scanreq.  Otherwise we do the default.
			*/
			if (vap->iv_flags_ext & IEEE80211_FEXT_SCANREQ) {
				ieee80211_check_scan(vap,
				    vap->iv_scanreq_flags,
				    vap->iv_scanreq_duration,
				    vap->iv_scanreq_mindwell,
				    vap->iv_scanreq_maxdwell,
				    vap->iv_scanreq_nssid, vap->iv_scanreq_ssid);
				vap->iv_flags_ext &= ~IEEE80211_FEXT_SCANREQ;
			} else
				ieee80211_check_scan_current(vap);
			break;
		default:
			break;
		}
		break;
	case IEEE80211_S_CAC:
		/*
		 * Start CAC on a DFS channel.  We come here when starting
		 * a bss on a DFS channel (see ieee80211_create_ibss).
		 */
		ieee80211_dfs_cac_start(vap);
		break;
	case IEEE80211_S_RUN:
		switch (ostate) {
		case IEEE80211_S_INIT:
			/*
			 * Already have a channel; bypass the
			 * scan and startup immediately.
			 * Note that ieee80211_create_ibss will call
			 * back to do a RUN->RUN state change.
			 */
			ieee80211_create_ibss(vap,
			    ieee80211_ht_adjust_channel(ic,
				ic->ic_curchan, vap->iv_flags_ht));
			/* NB: iv_bss is changed on return */
			break;
		case IEEE80211_S_CAC:
			/*
			 * NB: This is the normal state change when CAC
			 * expires and no radar was detected; no need to
			 * clear the CAC timer as it's already expired.
			 */
			/* fall thru... */
		case IEEE80211_S_CSA:
#if 0
			/*
			 * Shorten inactivity timer of associated stations
			 * to weed out sta's that don't follow a CSA.
			 */
			ieee80211_iterate_nodes(&ic->ic_sta, sta_csa, vap);
#endif
			/*
			 * Update bss node channel to reflect where
			 * we landed after CSA.
			 */
			ieee80211_node_set_chan(ni,
			    ieee80211_ht_adjust_channel(ic, ic->ic_curchan,
				ieee80211_htchanflags(ni->ni_chan)));
			/* XXX bypass debug msgs */
			break;
		case IEEE80211_S_SCAN:
		case IEEE80211_S_RUN:
#ifdef IEEE80211_DEBUG
			if (ieee80211_msg_debug(vap)) {
				ieee80211_note(vap,
				    "synchronized with %s meshid ",
				    ether_sprintf(ni->ni_meshid));
				ieee80211_print_essid(ni->ni_meshid,
				    ni->ni_meshidlen);
				/* XXX MCS/HT */
				printf(" channel %d\n",
				    ieee80211_chan2ieee(ic, ic->ic_curchan));
			}
#endif
			break;
		default:
			break;
		}
		ieee80211_node_authorize(ni);
		callout_reset(&ms->ms_cleantimer, ms->ms_ppath->mpp_inact,
                    mesh_rt_cleanup_cb, vap);
		mesh_gatemode_setup(vap);
		break;
	default:
		break;
	}
	/* NB: ostate not nstate */
	ms->ms_ppath->mpp_newstate(vap, ostate, arg);
	return 0;
}

static void
mesh_rt_cleanup_cb(void *arg)
{
	struct ieee80211vap *vap = arg;
	struct ieee80211_mesh_state *ms = vap->iv_mesh;

	mesh_rt_flush_invalid(vap);
	callout_reset(&ms->ms_cleantimer, ms->ms_ppath->mpp_inact,
	    mesh_rt_cleanup_cb, vap);
}

/*
 * Mark a mesh STA as gate and return a pointer to it.
 * If this is first time, we create a new gate route.
 * Always update the path route to this mesh gate.
 */
struct ieee80211_mesh_gate_route *
ieee80211_mesh_mark_gate(struct ieee80211vap *vap, const uint8_t *addr,
    struct ieee80211_mesh_route *rt)
{
	struct ieee80211_mesh_state *ms = vap->iv_mesh;
	struct ieee80211_mesh_gate_route *gr = NULL, *next;
	int found = 0;

	MESH_RT_LOCK(ms);
	TAILQ_FOREACH_SAFE(gr, &ms->ms_known_gates, gr_next, next) {
		if (IEEE80211_ADDR_EQ(gr->gr_addr, addr)) {
			found = 1;
			break;
		}
	}

	if (!found) {
		/* New mesh gate add it to known table. */
		IEEE80211_NOTE_MAC(vap, IEEE80211_MSG_MESH, addr,
		    "%s", "stored new gate information from pro-PREQ.");
		gr = IEEE80211_MALLOC(ALIGN(sizeof(struct ieee80211_mesh_gate_route)),
		    M_80211_MESH_GT_RT,
		    IEEE80211_M_NOWAIT | IEEE80211_M_ZERO);
		IEEE80211_ADDR_COPY(gr->gr_addr, addr);
		TAILQ_INSERT_TAIL(&ms->ms_known_gates, gr, gr_next);
	}
	gr->gr_route = rt;
	/* TODO: link from path route to gate route */
	MESH_RT_UNLOCK(ms);

	return gr;
}


/*
 * Helper function to note the Mesh Peer Link FSM change.
 */
static void
mesh_linkchange(struct ieee80211_node *ni, enum ieee80211_mesh_mlstate state)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211_mesh_state *ms = vap->iv_mesh;
#ifdef IEEE80211_DEBUG
	static const char *meshlinkstates[] = {
		[IEEE80211_NODE_MESH_IDLE]		= "IDLE",
		[IEEE80211_NODE_MESH_OPENSNT]		= "OPEN SENT",
		[IEEE80211_NODE_MESH_OPENRCV]		= "OPEN RECEIVED",
		[IEEE80211_NODE_MESH_CONFIRMRCV]	= "CONFIRM RECEIVED",
		[IEEE80211_NODE_MESH_ESTABLISHED]	= "ESTABLISHED",
		[IEEE80211_NODE_MESH_HOLDING]		= "HOLDING"
	};
#endif
	IEEE80211_NOTE(vap, IEEE80211_MSG_MESH,
	    ni, "peer link: %s -> %s",
	    meshlinkstates[ni->ni_mlstate], meshlinkstates[state]);

	/* track neighbor count */
	if (state == IEEE80211_NODE_MESH_ESTABLISHED &&
	    ni->ni_mlstate != IEEE80211_NODE_MESH_ESTABLISHED) {
		KASSERT(ms->ms_neighbors < 65535, ("neighbor count overflow"));
		ms->ms_neighbors++;
		ieee80211_beacon_notify(vap, IEEE80211_BEACON_MESHCONF);
	} else if (ni->ni_mlstate == IEEE80211_NODE_MESH_ESTABLISHED &&
	    state != IEEE80211_NODE_MESH_ESTABLISHED) {
		KASSERT(ms->ms_neighbors > 0, ("neighbor count 0"));
		ms->ms_neighbors--;
		ieee80211_beacon_notify(vap, IEEE80211_BEACON_MESHCONF);
	}
	ni->ni_mlstate = state;
	switch (state) {
	case IEEE80211_NODE_MESH_HOLDING:
		ms->ms_ppath->mpp_peerdown(ni);
		break;
	case IEEE80211_NODE_MESH_ESTABLISHED:
		ieee80211_mesh_discover(vap, ni->ni_macaddr, NULL);
		break;
	default:
		break;
	}
}

/*
 * Helper function to generate a unique local ID required for mesh
 * peer establishment.
 */
static void
mesh_checkid(void *arg, struct ieee80211_node *ni)
{
	uint16_t *r = arg;
	
	if (*r == ni->ni_mllid)
		*(uint16_t *)arg = 0;
}

static uint32_t
mesh_generateid(struct ieee80211vap *vap)
{
	int maxiter = 4;
	uint16_t r;

	do {
		get_random_bytes(&r, 2);
		ieee80211_iterate_nodes(&vap->iv_ic->ic_sta, mesh_checkid, &r);
		maxiter--;
	} while (r == 0 && maxiter > 0);
	return r;
}

/*
 * Verifies if we already received this packet by checking its
 * sequence number.
 * Returns 0 if the frame is to be accepted, 1 otherwise.
 */
static int
mesh_checkpseq(struct ieee80211vap *vap,
    const uint8_t source[IEEE80211_ADDR_LEN], uint32_t seq)
{
	struct ieee80211_mesh_route *rt;

	rt = ieee80211_mesh_rt_find(vap, source);
	if (rt == NULL) {
		rt = ieee80211_mesh_rt_add(vap, source);
		if (rt == NULL) {
			IEEE80211_NOTE_MAC(vap, IEEE80211_MSG_MESH, source,
			    "%s", "add mcast route failed");
			vap->iv_stats.is_mesh_rtaddfailed++;
			return 1;
		}
		IEEE80211_NOTE_MAC(vap, IEEE80211_MSG_MESH, source,
		    "add mcast route, mesh seqno %d", seq);
		rt->rt_lastmseq = seq;
		return 0;
	}
	if (IEEE80211_MESH_SEQ_GEQ(rt->rt_lastmseq, seq)) {
		return 1;
	} else {
		rt->rt_lastmseq = seq;
		return 0;
	}
}

/*
 * Iterate the routing table and locate the next hop.
 */
struct ieee80211_node *
ieee80211_mesh_find_txnode(struct ieee80211vap *vap,
    const uint8_t dest[IEEE80211_ADDR_LEN])
{
	struct ieee80211_mesh_route *rt;

	rt = ieee80211_mesh_rt_find(vap, dest);
	if (rt == NULL)
		return NULL;
	if ((rt->rt_flags & IEEE80211_MESHRT_FLAGS_VALID) == 0) {
		IEEE80211_NOTE_MAC(vap, IEEE80211_MSG_MESH, dest,
		    "%s: !valid, flags 0x%x", __func__, rt->rt_flags);
		/* XXX stat */
		return NULL;
	}
	if (rt->rt_flags & IEEE80211_MESHRT_FLAGS_PROXY) {
		rt = ieee80211_mesh_rt_find(vap, rt->rt_mesh_gate);
		if (rt == NULL) return NULL;
		if ((rt->rt_flags & IEEE80211_MESHRT_FLAGS_VALID) == 0) {
			IEEE80211_NOTE_MAC(vap, IEEE80211_MSG_MESH, dest,
			    "%s: meshgate !valid, flags 0x%x", __func__,
			    rt->rt_flags);
			/* XXX stat */
			return NULL;
		}
	}
	return ieee80211_find_txnode(vap, rt->rt_nexthop);
}

static void
mesh_transmit_to_gate(struct ieee80211vap *vap, struct mbuf *m,
    struct ieee80211_mesh_route *rt_gate)
{
	struct ifnet *ifp = vap->iv_ifp;
	struct ieee80211_node *ni;

	IEEE80211_TX_UNLOCK_ASSERT(vap->iv_ic);

	ni = ieee80211_mesh_find_txnode(vap, rt_gate->rt_dest);
	if (ni == NULL) {
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		m_freem(m);
		return;
	}

	/*
	 * Send through the VAP packet transmit path.
	 * This consumes the node ref grabbed above and
	 * the mbuf, regardless of whether there's a problem
	 * or not.
	 */
	(void) ieee80211_vap_pkt_send_dest(vap, m, ni);
}

/*
 * Forward the queued frames to known valid mesh gates.
 * Assume destination to be outside the MBSS (i.e. proxy entry),
 * If no valid mesh gates are known silently discard queued frames.
 * After transmitting frames to all known valid mesh gates, this route
 * will be marked invalid, and a new path discovery will happen in the hopes
 * that (at least) one of the mesh gates have a new proxy entry for us to use.
 */
void
ieee80211_mesh_forward_to_gates(struct ieee80211vap *vap,
    struct ieee80211_mesh_route *rt_dest)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct ieee80211_mesh_state *ms = vap->iv_mesh;
	struct ieee80211_mesh_route *rt_gate;
	struct ieee80211_mesh_gate_route *gr = NULL, *gr_next;
	struct mbuf *m, *mcopy, *next;

	IEEE80211_TX_UNLOCK_ASSERT(ic);

	KASSERT( rt_dest->rt_flags == IEEE80211_MESHRT_FLAGS_DISCOVER,
	    ("Route is not marked with IEEE80211_MESHRT_FLAGS_DISCOVER"));

	/* XXX: send to more than one valid mash gate */
	MESH_RT_LOCK(ms);

	m = ieee80211_ageq_remove(&ic->ic_stageq,
	    (struct ieee80211_node *)(uintptr_t)
	    ieee80211_mac_hash(ic, rt_dest->rt_dest));

	TAILQ_FOREACH_SAFE(gr, &ms->ms_known_gates, gr_next, gr_next) {
		rt_gate = gr->gr_route;
		if (rt_gate == NULL) {
			IEEE80211_NOTE_MAC(vap, IEEE80211_MSG_HWMP,
				rt_dest->rt_dest,
				"mesh gate with no path %6D",
				gr->gr_addr, ":");
			continue;
		}
		if ((rt_gate->rt_flags & IEEE80211_MESHRT_FLAGS_VALID) == 0)
			continue;
		KASSERT(rt_gate->rt_flags & IEEE80211_MESHRT_FLAGS_GATE,
		    ("route not marked as a mesh gate"));
		KASSERT((rt_gate->rt_flags &
			IEEE80211_MESHRT_FLAGS_PROXY) == 0,
			("found mesh gate that is also marked porxy"));
		/*
		 * convert route to a proxy route gated by the current
		 * mesh gate, this is needed so encap can built data
		 * frame with correct address.
		 */
		rt_dest->rt_flags = IEEE80211_MESHRT_FLAGS_PROXY |
			IEEE80211_MESHRT_FLAGS_VALID;
		rt_dest->rt_ext_seq = 1; /* random value */
		IEEE80211_ADDR_COPY(rt_dest->rt_mesh_gate, rt_gate->rt_dest);
		IEEE80211_ADDR_COPY(rt_dest->rt_nexthop, rt_gate->rt_nexthop);
		rt_dest->rt_metric = rt_gate->rt_metric;
		rt_dest->rt_nhops = rt_gate->rt_nhops;
		ieee80211_mesh_rt_update(rt_dest, ms->ms_ppath->mpp_inact);
		MESH_RT_UNLOCK(ms);
		/* XXX: lock?? */
		mcopy = m_dup(m, M_NOWAIT);
		for (; mcopy != NULL; mcopy = next) {
			next = mcopy->m_nextpkt;
			mcopy->m_nextpkt = NULL;
			IEEE80211_NOTE_MAC(vap, IEEE80211_MSG_HWMP,
			    rt_dest->rt_dest,
			    "flush queued frame %p len %d", mcopy,
			    mcopy->m_pkthdr.len);
			mesh_transmit_to_gate(vap, mcopy, rt_gate);
		}
		MESH_RT_LOCK(ms);
	}
	rt_dest->rt_flags = 0; /* Mark invalid */
	m_freem(m);
	MESH_RT_UNLOCK(ms);
}

/*
 * Forward the specified frame.
 * Decrement the TTL and set TA to our MAC address.
 */
static void
mesh_forward(struct ieee80211vap *vap, struct mbuf *m,
    const struct ieee80211_meshcntl *mc)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct ieee80211_mesh_state *ms = vap->iv_mesh;
	struct ifnet *ifp = vap->iv_ifp;
	const struct ieee80211_frame *wh =
	    mtod(m, const struct ieee80211_frame *);
	struct mbuf *mcopy;
	struct ieee80211_meshcntl *mccopy;
	struct ieee80211_frame *whcopy;
	struct ieee80211_node *ni;
	int err;

	/* This is called from the RX path - don't hold this lock */
	IEEE80211_TX_UNLOCK_ASSERT(ic);

	/*
	 * mesh ttl of 1 means we are the last one receiving it,
	 * according to amendment we decrement and then check if
	 * 0, if so we dont forward.
	 */
	if (mc->mc_ttl < 1) {
		IEEE80211_NOTE_FRAME(vap, IEEE80211_MSG_MESH, wh,
		    "%s", "frame not fwd'd, ttl 1");
		vap->iv_stats.is_mesh_fwd_ttl++;
		return;
	}
	if (!(ms->ms_flags & IEEE80211_MESHFLAGS_FWD)) {
		IEEE80211_NOTE_FRAME(vap, IEEE80211_MSG_MESH, wh,
		    "%s", "frame not fwd'd, fwding disabled");
		vap->iv_stats.is_mesh_fwd_disabled++;
		return;
	}
	mcopy = m_dup(m, M_NOWAIT);
	if (mcopy == NULL) {
		IEEE80211_NOTE_FRAME(vap, IEEE80211_MSG_MESH, wh,
		    "%s", "frame not fwd'd, cannot dup");
		vap->iv_stats.is_mesh_fwd_nobuf++;
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		return;
	}
	mcopy = m_pullup(mcopy, ieee80211_hdrspace(ic, wh) +
	    sizeof(struct ieee80211_meshcntl));
	if (mcopy == NULL) {
		IEEE80211_NOTE_FRAME(vap, IEEE80211_MSG_MESH, wh,
		    "%s", "frame not fwd'd, too short");
		vap->iv_stats.is_mesh_fwd_tooshort++;
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		m_freem(mcopy);
		return;
	}
	whcopy = mtod(mcopy, struct ieee80211_frame *);
	mccopy = (struct ieee80211_meshcntl *)
	    (mtod(mcopy, uint8_t *) + ieee80211_hdrspace(ic, wh));
	/* XXX clear other bits? */
	whcopy->i_fc[1] &= ~IEEE80211_FC1_RETRY;
	IEEE80211_ADDR_COPY(whcopy->i_addr2, vap->iv_myaddr);
	if (IEEE80211_IS_MULTICAST(wh->i_addr1)) {
		ni = ieee80211_ref_node(vap->iv_bss);
		mcopy->m_flags |= M_MCAST;
	} else {
		ni = ieee80211_mesh_find_txnode(vap, whcopy->i_addr3);
		if (ni == NULL) {
			/*
			 * [Optional] any of the following three actions:
			 * o silently discard
			 * o trigger a path discovery
			 * o inform TA that meshDA is unknown.
			 */
			IEEE80211_NOTE_FRAME(vap, IEEE80211_MSG_MESH, wh,
			    "%s", "frame not fwd'd, no path");
			ms->ms_ppath->mpp_senderror(vap, whcopy->i_addr3, NULL,
			    IEEE80211_REASON_MESH_PERR_NO_FI);
			vap->iv_stats.is_mesh_fwd_nopath++;
			m_freem(mcopy);
			return;
		}
		IEEE80211_ADDR_COPY(whcopy->i_addr1, ni->ni_macaddr);
	}
	KASSERT(mccopy->mc_ttl > 0, ("%s called with wrong ttl", __func__));
	mccopy->mc_ttl--;

	/* XXX calculate priority so drivers can find the tx queue */
	M_WME_SETAC(mcopy, WME_AC_BE);

	/* XXX do we know m_nextpkt is NULL? */
	mcopy->m_pkthdr.rcvif = (void *) ni;

	/*
	 * XXX this bypasses all of the VAP TX handling; it passes frames
	 * directly to the parent interface.
	 *
	 * Because of this, there's no TX lock being held as there's no
	 * encaps state being used.
	 *
	 * Doing a direct parent transmit may not be the correct thing
	 * to do here; we'll have to re-think this soon.
	 */
	IEEE80211_TX_LOCK(ic);
	err = ieee80211_parent_xmitpkt(ic, mcopy);
	IEEE80211_TX_UNLOCK(ic);
	if (!err)
		if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
}

static struct mbuf *
mesh_decap(struct ieee80211vap *vap, struct mbuf *m, int hdrlen, int meshdrlen)
{
#define	WHDIR(wh)	((wh)->i_fc[1] & IEEE80211_FC1_DIR_MASK)
#define	MC01(mc)	((const struct ieee80211_meshcntl_ae01 *)mc)
	uint8_t b[sizeof(struct ieee80211_qosframe_addr4) +
		  sizeof(struct ieee80211_meshcntl_ae10)];
	const struct ieee80211_qosframe_addr4 *wh;
	const struct ieee80211_meshcntl_ae10 *mc;
	struct ether_header *eh;
	struct llc *llc;
	int ae;

	if (m->m_len < hdrlen + sizeof(*llc) &&
	    (m = m_pullup(m, hdrlen + sizeof(*llc))) == NULL) {
		IEEE80211_DPRINTF(vap, IEEE80211_MSG_ANY,
		    "discard data frame: %s", "m_pullup failed");
		vap->iv_stats.is_rx_tooshort++;
		return NULL;
	}
	memcpy(b, mtod(m, caddr_t), hdrlen);
	wh = (const struct ieee80211_qosframe_addr4 *)&b[0];
	mc = (const struct ieee80211_meshcntl_ae10 *)&b[hdrlen - meshdrlen];
	KASSERT(WHDIR(wh) == IEEE80211_FC1_DIR_FROMDS ||
		WHDIR(wh) == IEEE80211_FC1_DIR_DSTODS,
	    ("bogus dir, fc 0x%x:0x%x", wh->i_fc[0], wh->i_fc[1]));

	llc = (struct llc *)(mtod(m, caddr_t) + hdrlen);
	if (llc->llc_dsap == LLC_SNAP_LSAP && llc->llc_ssap == LLC_SNAP_LSAP &&
	    llc->llc_control == LLC_UI && llc->llc_snap.org_code[0] == 0 &&
	    llc->llc_snap.org_code[1] == 0 && llc->llc_snap.org_code[2] == 0 &&
	    /* NB: preserve AppleTalk frames that have a native SNAP hdr */
	    !(llc->llc_snap.ether_type == htons(ETHERTYPE_AARP) ||
	      llc->llc_snap.ether_type == htons(ETHERTYPE_IPX))) {
		m_adj(m, hdrlen + sizeof(struct llc) - sizeof(*eh));
		llc = NULL;
	} else {
		m_adj(m, hdrlen - sizeof(*eh));
	}
	eh = mtod(m, struct ether_header *);
	ae = mc->mc_flags & IEEE80211_MESH_AE_MASK;
	if (WHDIR(wh) == IEEE80211_FC1_DIR_FROMDS) {
		IEEE80211_ADDR_COPY(eh->ether_dhost, wh->i_addr1);
		if (ae == IEEE80211_MESH_AE_00) {
			IEEE80211_ADDR_COPY(eh->ether_shost, wh->i_addr3);
		} else if (ae == IEEE80211_MESH_AE_01) {
			IEEE80211_ADDR_COPY(eh->ether_shost,
			    MC01(mc)->mc_addr4);
		} else {
			IEEE80211_DISCARD(vap, IEEE80211_MSG_ANY,
			    (const struct ieee80211_frame *)wh, NULL,
			    "bad AE %d", ae);
			vap->iv_stats.is_mesh_badae++;
			m_freem(m);
			return NULL;
		}
	} else {
		if (ae == IEEE80211_MESH_AE_00) {
			IEEE80211_ADDR_COPY(eh->ether_dhost, wh->i_addr3);
			IEEE80211_ADDR_COPY(eh->ether_shost, wh->i_addr4);
		} else if (ae == IEEE80211_MESH_AE_10) {
			IEEE80211_ADDR_COPY(eh->ether_dhost, mc->mc_addr5);
			IEEE80211_ADDR_COPY(eh->ether_shost, mc->mc_addr6);
		} else {
			IEEE80211_DISCARD(vap, IEEE80211_MSG_ANY,
			    (const struct ieee80211_frame *)wh, NULL,
			    "bad AE %d", ae);
			vap->iv_stats.is_mesh_badae++;
			m_freem(m);
			return NULL;
		}
	}
#ifndef __NO_STRICT_ALIGNMENT
	if (!ALIGNED_POINTER(mtod(m, caddr_t) + sizeof(*eh), uint32_t)) {
		m = ieee80211_realign(vap, m, sizeof(*eh));
		if (m == NULL)
			return NULL;
	}
#endif /* !__NO_STRICT_ALIGNMENT */
	if (llc != NULL) {
		eh = mtod(m, struct ether_header *);
		eh->ether_type = htons(m->m_pkthdr.len - sizeof(*eh));
	}
	return m;
#undef	WDIR
#undef	MC01
}

/*
 * Return non-zero if the unicast mesh data frame should be processed
 * locally.  Frames that are not proxy'd have our address, otherwise
 * we need to consult the routing table to look for a proxy entry.
 */
static __inline int
mesh_isucastforme(struct ieee80211vap *vap, const struct ieee80211_frame *wh,
    const struct ieee80211_meshcntl *mc)
{
	int ae = mc->mc_flags & 3;

	KASSERT((wh->i_fc[1] & IEEE80211_FC1_DIR_MASK) == IEEE80211_FC1_DIR_DSTODS,
	    ("bad dir 0x%x:0x%x", wh->i_fc[0], wh->i_fc[1]));
	KASSERT(ae == IEEE80211_MESH_AE_00 || ae == IEEE80211_MESH_AE_10,
	    ("bad AE %d", ae));
	if (ae == IEEE80211_MESH_AE_10) {	/* ucast w/ proxy */
		const struct ieee80211_meshcntl_ae10 *mc10 =
		    (const struct ieee80211_meshcntl_ae10 *) mc;
		struct ieee80211_mesh_route *rt =
		    ieee80211_mesh_rt_find(vap, mc10->mc_addr5);
		/* check for proxy route to ourself */
		return (rt != NULL &&
		    (rt->rt_flags & IEEE80211_MESHRT_FLAGS_PROXY));
	} else					/* ucast w/o proxy */
		return IEEE80211_ADDR_EQ(wh->i_addr3, vap->iv_myaddr);
}

/*
 * Verifies transmitter, updates lifetime, precursor list and forwards data.
 * > 0 means we have forwarded data and no need to process locally
 * == 0 means we want to process locally (and we may have forwarded data
 * < 0 means there was an error and data should be discarded
 */
static int
mesh_recv_indiv_data_to_fwrd(struct ieee80211vap *vap, struct mbuf *m,
    struct ieee80211_frame *wh, const struct ieee80211_meshcntl *mc)
{
	struct ieee80211_qosframe_addr4 *qwh;
	struct ieee80211_mesh_state *ms = vap->iv_mesh;
	struct ieee80211_mesh_route *rt_meshda, *rt_meshsa;

	/* This is called from the RX path - don't hold this lock */
	IEEE80211_TX_UNLOCK_ASSERT(vap->iv_ic);

	qwh = (struct ieee80211_qosframe_addr4 *)wh;

	/*
	 * TODO:
	 * o verify addr2 is  a legitimate transmitter
	 * o lifetime of precursor of addr3 (addr2) is max(init, curr)
	 * o lifetime of precursor of addr4 (nexthop) is max(init, curr)
	 */

	/* set lifetime of addr3 (meshDA) to initial value */
	rt_meshda = ieee80211_mesh_rt_find(vap, qwh->i_addr3);
	if (rt_meshda == NULL) {
		IEEE80211_NOTE_MAC(vap, IEEE80211_MSG_MESH, qwh->i_addr2,
		    "no route to meshDA(%6D)", qwh->i_addr3, ":");
		/*
		 * [Optional] any of the following three actions:
		 * o silently discard 				[X]
		 * o trigger a path discovery			[ ]
		 * o inform TA that meshDA is unknown.		[ ]
		 */
		/* XXX: stats */
		return (-1);
	}

	ieee80211_mesh_rt_update(rt_meshda, ticks_to_msecs(
	    ms->ms_ppath->mpp_inact));

	/* set lifetime of addr4 (meshSA) to initial value */
	rt_meshsa = ieee80211_mesh_rt_find(vap, qwh->i_addr4);
	KASSERT(rt_meshsa != NULL, ("no route"));
	ieee80211_mesh_rt_update(rt_meshsa, ticks_to_msecs(
	    ms->ms_ppath->mpp_inact));

	mesh_forward(vap, m, mc);
	return (1); /* dont process locally */
}

/*
 * Verifies transmitter, updates lifetime, precursor list and process data
 * locally, if data is proxy with AE = 10 it could mean data should go
 * on another mesh path or data should be forwarded to the DS.
 *
 * > 0 means we have forwarded data and no need to process locally
 * == 0 means we want to process locally (and we may have forwarded data
 * < 0 means there was an error and data should be discarded
 */
static int
mesh_recv_indiv_data_to_me(struct ieee80211vap *vap, struct mbuf *m,
    struct ieee80211_frame *wh, const struct ieee80211_meshcntl *mc)
{
	struct ieee80211_qosframe_addr4 *qwh;
	const struct ieee80211_meshcntl_ae10 *mc10;
	struct ieee80211_mesh_state *ms = vap->iv_mesh;
	struct ieee80211_mesh_route *rt;
	int ae;

	/* This is called from the RX path - don't hold this lock */
	IEEE80211_TX_UNLOCK_ASSERT(vap->iv_ic);

	qwh = (struct ieee80211_qosframe_addr4 *)wh;
	mc10 = (const struct ieee80211_meshcntl_ae10 *)mc;

	/*
	 * TODO:
	 * o verify addr2 is  a legitimate transmitter
	 * o lifetime of precursor entry is max(init, curr)
	 */

	/* set lifetime of addr4 (meshSA) to initial value */
	rt = ieee80211_mesh_rt_find(vap, qwh->i_addr4);
	KASSERT(rt != NULL, ("no route"));
	ieee80211_mesh_rt_update(rt, ticks_to_msecs(ms->ms_ppath->mpp_inact));
	rt = NULL;

	ae = mc10->mc_flags & IEEE80211_MESH_AE_MASK;
	KASSERT(ae == IEEE80211_MESH_AE_00 ||
	    ae == IEEE80211_MESH_AE_10, ("bad AE %d", ae));
	if (ae == IEEE80211_MESH_AE_10) {
		if (IEEE80211_ADDR_EQ(mc10->mc_addr5, qwh->i_addr3)) {
			return (0); /* process locally */
		}

		rt =  ieee80211_mesh_rt_find(vap, mc10->mc_addr5);
		if (rt != NULL &&
		    (rt->rt_flags & IEEE80211_MESHRT_FLAGS_VALID) &&
		    (rt->rt_flags & IEEE80211_MESHRT_FLAGS_PROXY) == 0) {
			/*
			 * Forward on another mesh-path, according to
			 * amendment as specified in 9.32.4.1
			 */
			IEEE80211_ADDR_COPY(qwh->i_addr3, mc10->mc_addr5);
			mesh_forward(vap, m,
			    (const struct ieee80211_meshcntl *)mc10);
			return (1); /* dont process locally */
		}
		/*
		 * All other cases: forward of MSDUs from the MBSS to DS indiv.
		 * addressed according to 13.11.3.2.
		 */
		IEEE80211_NOTE_MAC(vap, IEEE80211_MSG_OUTPUT, qwh->i_addr2,
		    "forward frame to DS, SA(%6D) DA(%6D)",
		    mc10->mc_addr6, ":", mc10->mc_addr5, ":");
	}
	return (0); /* process locally */
}

/*
 * Try to forward the group addressed data on to other mesh STAs, and
 * also to the DS.
 *
 * > 0 means we have forwarded data and no need to process locally
 * == 0 means we want to process locally (and we may have forwarded data
 * < 0 means there was an error and data should be discarded
 */
static int
mesh_recv_group_data(struct ieee80211vap *vap, struct mbuf *m,
    struct ieee80211_frame *wh, const struct ieee80211_meshcntl *mc)
{
#define	MC01(mc)	((const struct ieee80211_meshcntl_ae01 *)mc)
	struct ieee80211_mesh_state *ms = vap->iv_mesh;

	/* This is called from the RX path - don't hold this lock */
	IEEE80211_TX_UNLOCK_ASSERT(vap->iv_ic);

	mesh_forward(vap, m, mc);

	if(mc->mc_ttl > 0) {
		if (mc->mc_flags & IEEE80211_MESH_AE_01) {
			/*
			 * Forward of MSDUs from the MBSS to DS group addressed
			 * (according to 13.11.3.2)
			 * This happens by delivering the packet, and a bridge
			 * will sent it on another port member.
			 */
			if (ms->ms_flags & IEEE80211_MESHFLAGS_GATE &&
			    ms->ms_flags & IEEE80211_MESHFLAGS_FWD) {
				IEEE80211_NOTE_MAC(vap, IEEE80211_MSG_MESH,
				    MC01(mc)->mc_addr4, "%s",
				    "forward from MBSS to the DS");
			}
		}
	}
	return (0); /* process locally */
#undef	MC01
}

static int
mesh_input(struct ieee80211_node *ni, struct mbuf *m,
    const struct ieee80211_rx_stats *rxs, int rssi, int nf)
{
#define	HAS_SEQ(type)	((type & 0x4) == 0)
#define	MC01(mc)	((const struct ieee80211_meshcntl_ae01 *)mc)
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211com *ic = ni->ni_ic;
	struct ifnet *ifp = vap->iv_ifp;
	struct ieee80211_frame *wh;
	const struct ieee80211_meshcntl *mc;
	int hdrspace, meshdrlen, need_tap, error;
	uint8_t dir, type, subtype, ae;
	uint32_t seq;
	const uint8_t *addr;
	uint8_t qos[2];

	KASSERT(ni != NULL, ("null node"));
	ni->ni_inact = ni->ni_inact_reload;

	need_tap = 1;			/* mbuf need to be tapped. */
	type = -1;			/* undefined */

	/* This is called from the RX path - don't hold this lock */
	IEEE80211_TX_UNLOCK_ASSERT(ic);

	if (m->m_pkthdr.len < sizeof(struct ieee80211_frame_min)) {
		IEEE80211_DISCARD_MAC(vap, IEEE80211_MSG_ANY,
		    ni->ni_macaddr, NULL,
		    "too short (1): len %u", m->m_pkthdr.len);
		vap->iv_stats.is_rx_tooshort++;
		goto out;
	}
	/*
	 * Bit of a cheat here, we use a pointer for a 3-address
	 * frame format but don't reference fields past outside
	 * ieee80211_frame_min w/o first validating the data is
	 * present.
	*/
	wh = mtod(m, struct ieee80211_frame *);

	if ((wh->i_fc[0] & IEEE80211_FC0_VERSION_MASK) !=
	    IEEE80211_FC0_VERSION_0) {
		IEEE80211_DISCARD_MAC(vap, IEEE80211_MSG_ANY,
		    ni->ni_macaddr, NULL, "wrong version %x", wh->i_fc[0]);
		vap->iv_stats.is_rx_badversion++;
		goto err;
	}
	dir = wh->i_fc[1] & IEEE80211_FC1_DIR_MASK;
	type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;
	subtype = wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK;
	if ((ic->ic_flags & IEEE80211_F_SCAN) == 0) {
		IEEE80211_RSSI_LPF(ni->ni_avgrssi, rssi);
		ni->ni_noise = nf;
		if (HAS_SEQ(type)) {
			uint8_t tid = ieee80211_gettid(wh);

			if (IEEE80211_QOS_HAS_SEQ(wh) &&
			    TID_TO_WME_AC(tid) >= WME_AC_VI)
				ic->ic_wme.wme_hipri_traffic++;
			if (! ieee80211_check_rxseq(ni, wh, wh->i_addr1, rxs))
				goto out;
		}
	}
#ifdef IEEE80211_DEBUG
	/*
	 * It's easier, but too expensive, to simulate different mesh
	 * topologies by consulting the ACL policy very early, so do this
	 * only under DEBUG.
	 *
	 * NB: this check is also done upon peering link initiation.
	 */
	if (vap->iv_acl != NULL && !vap->iv_acl->iac_check(vap, wh)) {
		IEEE80211_DISCARD(vap, IEEE80211_MSG_ACL,
		    wh, NULL, "%s", "disallowed by ACL");
		vap->iv_stats.is_rx_acl++;
		goto out;
	}
#endif
	switch (type) {
	case IEEE80211_FC0_TYPE_DATA:
		if (ni == vap->iv_bss)
			goto out;
		if (ni->ni_mlstate != IEEE80211_NODE_MESH_ESTABLISHED) {
			IEEE80211_DISCARD_MAC(vap, IEEE80211_MSG_MESH,
			    ni->ni_macaddr, NULL,
			    "peer link not yet established (%d)",
			    ni->ni_mlstate);
			vap->iv_stats.is_mesh_nolink++;
			goto out;
		}
		if (dir != IEEE80211_FC1_DIR_FROMDS &&
		    dir != IEEE80211_FC1_DIR_DSTODS) {
			IEEE80211_DISCARD(vap, IEEE80211_MSG_INPUT,
			    wh, "data", "incorrect dir 0x%x", dir);
			vap->iv_stats.is_rx_wrongdir++;
			goto err;
		}

		/* All Mesh data frames are QoS subtype */
		if (!HAS_SEQ(type)) {
			IEEE80211_DISCARD(vap, IEEE80211_MSG_INPUT,
			    wh, "data", "incorrect subtype 0x%x", subtype);
			vap->iv_stats.is_rx_badsubtype++;
			goto err;
		}

		/*
		 * Next up, any fragmentation.
		 * XXX: we defrag before we even try to forward,
		 * Mesh Control field is not present in sub-sequent
		 * fragmented frames. This is in contrast to Draft 4.0.
		 */
		hdrspace = ieee80211_hdrspace(ic, wh);
		if (!IEEE80211_IS_MULTICAST(wh->i_addr1)) {
			m = ieee80211_defrag(ni, m, hdrspace);
			if (m == NULL) {
				/* Fragment dropped or frame not complete yet */
				goto out;
			}
		}
		wh = mtod(m, struct ieee80211_frame *); /* NB: after defrag */

		/*
		 * Now we have a complete Mesh Data frame.
		 */

		/*
		 * Only fromDStoDS data frames use 4 address qos frames
		 * as specified in amendment. Otherwise addr4 is located
		 * in the Mesh Control field and a 3 address qos frame
		 * is used.
		 */
		*(uint16_t *)qos = *(uint16_t *)ieee80211_getqos(wh);

		/*
		 * NB: The mesh STA sets the Mesh Control Present
		 * subfield to 1 in the Mesh Data frame containing
		 * an unfragmented MSDU, an A-MSDU, or the first
		 * fragment of an MSDU.
		 * After defrag it should always be present.
		 */
		if (!(qos[1] & IEEE80211_QOS_MC)) {
			IEEE80211_DISCARD_MAC(vap, IEEE80211_MSG_MESH,
			    ni->ni_macaddr, NULL,
			    "%s", "Mesh control field not present");
			vap->iv_stats.is_rx_elem_missing++; /* XXX: kinda */
			goto err;
		}

		/* pull up enough to get to the mesh control */
		if (m->m_len < hdrspace + sizeof(struct ieee80211_meshcntl) &&
		    (m = m_pullup(m, hdrspace +
		        sizeof(struct ieee80211_meshcntl))) == NULL) {
			IEEE80211_DISCARD_MAC(vap, IEEE80211_MSG_ANY,
			    ni->ni_macaddr, NULL,
			    "data too short: expecting %u", hdrspace);
			vap->iv_stats.is_rx_tooshort++;
			goto out;		/* XXX */
		}
		/*
		 * Now calculate the full extent of the headers. Note
		 * mesh_decap will pull up anything we didn't get
		 * above when it strips the 802.11 headers.
		 */
		mc = (const struct ieee80211_meshcntl *)
		    (mtod(m, const uint8_t *) + hdrspace);
		ae = mc->mc_flags & IEEE80211_MESH_AE_MASK;
		meshdrlen = sizeof(struct ieee80211_meshcntl) +
		    ae * IEEE80211_ADDR_LEN;
		hdrspace += meshdrlen;

		/* pull complete hdrspace = ieee80211_hdrspace + meshcontrol */
		if ((meshdrlen > sizeof(struct ieee80211_meshcntl)) &&
		    (m->m_len < hdrspace) &&
		    ((m = m_pullup(m, hdrspace)) == NULL)) {
			IEEE80211_DISCARD_MAC(vap, IEEE80211_MSG_ANY,
			    ni->ni_macaddr, NULL,
			    "data too short: expecting %u", hdrspace);
			vap->iv_stats.is_rx_tooshort++;
			goto out;		/* XXX */
		}
		/* XXX: are we sure there is no reallocating after m_pullup? */

		seq = le32dec(mc->mc_seq);
		if (IEEE80211_IS_MULTICAST(wh->i_addr1))
			addr = wh->i_addr3;
		else if (ae == IEEE80211_MESH_AE_01)
			addr = MC01(mc)->mc_addr4;
		else
			addr = ((struct ieee80211_qosframe_addr4 *)wh)->i_addr4;
		if (IEEE80211_ADDR_EQ(vap->iv_myaddr, addr)) {
			IEEE80211_DISCARD_MAC(vap, IEEE80211_MSG_INPUT,
			    addr, "data", "%s", "not to me");
			vap->iv_stats.is_rx_wrongbss++;	/* XXX kinda */
			goto out;
		}
		if (mesh_checkpseq(vap, addr, seq) != 0) {
			vap->iv_stats.is_rx_dup++;
			goto out;
		}

		/* This code "routes" the frame to the right control path */
		if (!IEEE80211_IS_MULTICAST(wh->i_addr1)) {
			if (IEEE80211_ADDR_EQ(vap->iv_myaddr, wh->i_addr3))
				error =
				    mesh_recv_indiv_data_to_me(vap, m, wh, mc);
			else if (IEEE80211_IS_MULTICAST(wh->i_addr3))
				error = mesh_recv_group_data(vap, m, wh, mc);
			else
				error = mesh_recv_indiv_data_to_fwrd(vap, m,
				    wh, mc);
		} else
			error = mesh_recv_group_data(vap, m, wh, mc);
		if (error < 0)
			goto err;
		else if (error > 0)
			goto out;

		if (ieee80211_radiotap_active_vap(vap))
			ieee80211_radiotap_rx(vap, m);
		need_tap = 0;

		/*
		 * Finally, strip the 802.11 header.
		 */
		m = mesh_decap(vap, m, hdrspace, meshdrlen);
		if (m == NULL) {
			/* XXX mask bit to check for both */
			/* don't count Null data frames as errors */
			if (subtype == IEEE80211_FC0_SUBTYPE_NODATA ||
			    subtype == IEEE80211_FC0_SUBTYPE_QOS_NULL)
				goto out;
			IEEE80211_DISCARD_MAC(vap, IEEE80211_MSG_INPUT,
			    ni->ni_macaddr, "data", "%s", "decap error");
			vap->iv_stats.is_rx_decap++;
			IEEE80211_NODE_STAT(ni, rx_decap);
			goto err;
		}
		if (qos[0] & IEEE80211_QOS_AMSDU) {
			m = ieee80211_decap_amsdu(ni, m);
			if (m == NULL)
				return IEEE80211_FC0_TYPE_DATA;
		}
		ieee80211_deliver_data(vap, ni, m);
		return type;
	case IEEE80211_FC0_TYPE_MGT:
		vap->iv_stats.is_rx_mgmt++;
		IEEE80211_NODE_STAT(ni, rx_mgmt);
		if (dir != IEEE80211_FC1_DIR_NODS) {
			IEEE80211_DISCARD(vap, IEEE80211_MSG_INPUT,
			    wh, "mgt", "incorrect dir 0x%x", dir);
			vap->iv_stats.is_rx_wrongdir++;
			goto err;
		}
		if (m->m_pkthdr.len < sizeof(struct ieee80211_frame)) {
			IEEE80211_DISCARD_MAC(vap, IEEE80211_MSG_ANY,
			    ni->ni_macaddr, "mgt", "too short: len %u",
			    m->m_pkthdr.len);
			vap->iv_stats.is_rx_tooshort++;
			goto out;
		}
#ifdef IEEE80211_DEBUG
		if ((ieee80211_msg_debug(vap) && 
		    (vap->iv_ic->ic_flags & IEEE80211_F_SCAN)) ||
		    ieee80211_msg_dumppkts(vap)) {
			if_printf(ifp, "received %s from %s rssi %d\n",
			    ieee80211_mgt_subtype_name(subtype),
			    ether_sprintf(wh->i_addr2), rssi);
		}
#endif
		if (wh->i_fc[1] & IEEE80211_FC1_PROTECTED) {
			IEEE80211_DISCARD(vap, IEEE80211_MSG_INPUT,
			    wh, NULL, "%s", "WEP set but not permitted");
			vap->iv_stats.is_rx_mgtdiscard++; /* XXX */
			goto out;
		}
		vap->iv_recv_mgmt(ni, m, subtype, rxs, rssi, nf);
		goto out;
	case IEEE80211_FC0_TYPE_CTL:
		vap->iv_stats.is_rx_ctl++;
		IEEE80211_NODE_STAT(ni, rx_ctrl);
		goto out;
	default:
		IEEE80211_DISCARD(vap, IEEE80211_MSG_ANY,
		    wh, "bad", "frame type 0x%x", type);
		/* should not come here */
		break;
	}
err:
	if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
out:
	if (m != NULL) {
		if (need_tap && ieee80211_radiotap_active_vap(vap))
			ieee80211_radiotap_rx(vap, m);
		m_freem(m);
	}
	return type;
#undef	HAS_SEQ
#undef	MC01
}

static void
mesh_recv_mgmt(struct ieee80211_node *ni, struct mbuf *m0, int subtype,
    const struct ieee80211_rx_stats *rxs, int rssi, int nf)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211_mesh_state *ms = vap->iv_mesh;
	struct ieee80211com *ic = ni->ni_ic;
	struct ieee80211_channel *rxchan = ic->ic_curchan;
	struct ieee80211_frame *wh;
	struct ieee80211_mesh_route *rt;
	uint8_t *frm, *efrm;

	wh = mtod(m0, struct ieee80211_frame *);
	frm = (uint8_t *)&wh[1];
	efrm = mtod(m0, uint8_t *) + m0->m_len;
	switch (subtype) {
	case IEEE80211_FC0_SUBTYPE_PROBE_RESP:
	case IEEE80211_FC0_SUBTYPE_BEACON:
	{
		struct ieee80211_scanparams scan;
		struct ieee80211_channel *c;
		/*
		 * We process beacon/probe response
		 * frames to discover neighbors.
		 */
		if (rxs != NULL) {
			c = ieee80211_lookup_channel_rxstatus(vap, rxs);
			if (c != NULL)
				rxchan = c;
		}
		if (ieee80211_parse_beacon(ni, m0, rxchan, &scan) != 0)
			return;
		/*
		 * Count frame now that we know it's to be processed.
		 */
		if (subtype == IEEE80211_FC0_SUBTYPE_BEACON) {
			vap->iv_stats.is_rx_beacon++;	/* XXX remove */
			IEEE80211_NODE_STAT(ni, rx_beacons);
		} else
			IEEE80211_NODE_STAT(ni, rx_proberesp);
		/*
		 * If scanning, just pass information to the scan module.
		 */
		if (ic->ic_flags & IEEE80211_F_SCAN) {
			if (ic->ic_flags_ext & IEEE80211_FEXT_PROBECHAN) {
				/*
				 * Actively scanning a channel marked passive;
				 * send a probe request now that we know there
				 * is 802.11 traffic present.
				 *
				 * XXX check if the beacon we recv'd gives
				 * us what we need and suppress the probe req
				 */
				ieee80211_probe_curchan(vap, 1);
				ic->ic_flags_ext &= ~IEEE80211_FEXT_PROBECHAN;
			}
			ieee80211_add_scan(vap, rxchan, &scan, wh,
			    subtype, rssi, nf);
			return;
		}

		/* The rest of this code assumes we are running */
		if (vap->iv_state != IEEE80211_S_RUN)
			return;
		/*
		 * Ignore non-mesh STAs.
		 */
		if ((scan.capinfo &
		     (IEEE80211_CAPINFO_ESS|IEEE80211_CAPINFO_IBSS)) ||
		    scan.meshid == NULL || scan.meshconf == NULL) {
			IEEE80211_DISCARD(vap, IEEE80211_MSG_INPUT,
			    wh, "beacon", "%s", "not a mesh sta");
			vap->iv_stats.is_mesh_wrongmesh++;
			return;
		}
		/*
		 * Ignore STAs for other mesh networks.
		 */
		if (memcmp(scan.meshid+2, ms->ms_id, ms->ms_idlen) != 0 ||
		    mesh_verify_meshconf(vap, scan.meshconf)) {
			IEEE80211_DISCARD(vap, IEEE80211_MSG_INPUT,
			    wh, "beacon", "%s", "not for our mesh");
			vap->iv_stats.is_mesh_wrongmesh++;
			return;
		}
		/*
		 * Peer only based on the current ACL policy.
		 */
		if (vap->iv_acl != NULL && !vap->iv_acl->iac_check(vap, wh)) {
			IEEE80211_DISCARD(vap, IEEE80211_MSG_ACL,
			    wh, NULL, "%s", "disallowed by ACL");
			vap->iv_stats.is_rx_acl++;
			return;
		}
		/*
		 * Do neighbor discovery.
		 */
		if (!IEEE80211_ADDR_EQ(wh->i_addr2, ni->ni_macaddr)) {
			/*
			 * Create a new entry in the neighbor table.
			 */
			ni = ieee80211_add_neighbor(vap, wh, &scan);
		}
		/*
		 * Automatically peer with discovered nodes if possible.
		 */
		if (ni != vap->iv_bss &&
		    (ms->ms_flags & IEEE80211_MESHFLAGS_AP)) {
			switch (ni->ni_mlstate) {
			case IEEE80211_NODE_MESH_IDLE:
			{
				uint16_t args[1];

				/* Wait for backoff callout to reset counter */
				if (ni->ni_mlhcnt >= ieee80211_mesh_maxholding)
					return;

				ni->ni_mlpid = mesh_generateid(vap);
				if (ni->ni_mlpid == 0)
					return;
				mesh_linkchange(ni, IEEE80211_NODE_MESH_OPENSNT);
				args[0] = ni->ni_mlpid;
				ieee80211_send_action(ni,
				IEEE80211_ACTION_CAT_SELF_PROT,
				IEEE80211_ACTION_MESHPEERING_OPEN, args);
				ni->ni_mlrcnt = 0;
				mesh_peer_timeout_setup(ni);
				break;
			}
			case IEEE80211_NODE_MESH_ESTABLISHED:
			{
				/*
				 * Valid beacon from a peer mesh STA
				 * bump TA lifetime
				 */
				rt = ieee80211_mesh_rt_find(vap, wh->i_addr2);
				if(rt != NULL) {
					ieee80211_mesh_rt_update(rt,
					    ticks_to_msecs(
					    ms->ms_ppath->mpp_inact));
				}
				break;
			}
			default:
				break; /* ignore */
			}
		}
		break;
	}
	case IEEE80211_FC0_SUBTYPE_PROBE_REQ:
	{
		uint8_t *ssid, *meshid, *rates, *xrates;

		if (vap->iv_state != IEEE80211_S_RUN) {
			IEEE80211_DISCARD(vap, IEEE80211_MSG_INPUT,
			    wh, NULL, "wrong state %s",
			    ieee80211_state_name[vap->iv_state]);
			vap->iv_stats.is_rx_mgtdiscard++;
			return;
		}
		if (IEEE80211_IS_MULTICAST(wh->i_addr2)) {
			/* frame must be directed */
			IEEE80211_DISCARD(vap, IEEE80211_MSG_INPUT,
			    wh, NULL, "%s", "not unicast");
			vap->iv_stats.is_rx_mgtdiscard++;	/* XXX stat */
			return;
		}
		/*
		 * prreq frame format
		 *      [tlv] ssid
		 *      [tlv] supported rates
		 *      [tlv] extended supported rates
		 *	[tlv] mesh id
		 */
		ssid = meshid = rates = xrates = NULL;
		while (efrm - frm > 1) {
			IEEE80211_VERIFY_LENGTH(efrm - frm, frm[1] + 2, return);
			switch (*frm) {
			case IEEE80211_ELEMID_SSID:
				ssid = frm;
				break;
			case IEEE80211_ELEMID_RATES:
				rates = frm;
				break;
			case IEEE80211_ELEMID_XRATES:
				xrates = frm;
				break;
			case IEEE80211_ELEMID_MESHID:
				meshid = frm;
				break;
			}
			frm += frm[1] + 2;
		}
		IEEE80211_VERIFY_ELEMENT(ssid, IEEE80211_NWID_LEN, return);
		IEEE80211_VERIFY_ELEMENT(rates, IEEE80211_RATE_MAXSIZE, return);
		if (xrates != NULL)
			IEEE80211_VERIFY_ELEMENT(xrates,
			    IEEE80211_RATE_MAXSIZE - rates[1], return);
		if (meshid != NULL) {
			IEEE80211_VERIFY_ELEMENT(meshid,
			    IEEE80211_MESHID_LEN, return);
			/* NB: meshid, not ssid */
			IEEE80211_VERIFY_SSID(vap->iv_bss, meshid, return);
		}

		/* XXX find a better class or define it's own */
		IEEE80211_NOTE_MAC(vap, IEEE80211_MSG_INPUT, wh->i_addr2,
		    "%s", "recv probe req");
		/*
		 * Some legacy 11b clients cannot hack a complete
		 * probe response frame.  When the request includes
		 * only a bare-bones rate set, communicate this to
		 * the transmit side.
		 */
		ieee80211_send_proberesp(vap, wh->i_addr2, 0);
		break;
	}

	case IEEE80211_FC0_SUBTYPE_ACTION:
	case IEEE80211_FC0_SUBTYPE_ACTION_NOACK:
		if (ni == vap->iv_bss) {
			IEEE80211_DISCARD(vap, IEEE80211_MSG_INPUT,
			    wh, NULL, "%s", "unknown node");
			vap->iv_stats.is_rx_mgtdiscard++;
		} else if (!IEEE80211_ADDR_EQ(vap->iv_myaddr, wh->i_addr1) &&
		    !IEEE80211_IS_MULTICAST(wh->i_addr1)) {
			IEEE80211_DISCARD(vap, IEEE80211_MSG_INPUT,
			    wh, NULL, "%s", "not for us");
			vap->iv_stats.is_rx_mgtdiscard++;
		} else if (vap->iv_state != IEEE80211_S_RUN) {
			IEEE80211_DISCARD(vap, IEEE80211_MSG_INPUT,
			    wh, NULL, "wrong state %s",
			    ieee80211_state_name[vap->iv_state]);
			vap->iv_stats.is_rx_mgtdiscard++;
		} else {
			if (ieee80211_parse_action(ni, m0) == 0)
				(void)ic->ic_recv_action(ni, wh, frm, efrm);
		}
		break;

	case IEEE80211_FC0_SUBTYPE_ASSOC_REQ:
	case IEEE80211_FC0_SUBTYPE_ASSOC_RESP:
	case IEEE80211_FC0_SUBTYPE_REASSOC_REQ:
	case IEEE80211_FC0_SUBTYPE_REASSOC_RESP:
	case IEEE80211_FC0_SUBTYPE_TIMING_ADV:
	case IEEE80211_FC0_SUBTYPE_ATIM:
	case IEEE80211_FC0_SUBTYPE_DISASSOC:
	case IEEE80211_FC0_SUBTYPE_AUTH:
	case IEEE80211_FC0_SUBTYPE_DEAUTH:
		IEEE80211_DISCARD(vap, IEEE80211_MSG_INPUT,
		    wh, NULL, "%s", "not handled");
		vap->iv_stats.is_rx_mgtdiscard++;
		break;

	default:
		IEEE80211_DISCARD(vap, IEEE80211_MSG_ANY,
		    wh, "mgt", "subtype 0x%x not handled", subtype);
		vap->iv_stats.is_rx_badsubtype++;
		break;
	}
}

static void
mesh_recv_ctl(struct ieee80211_node *ni, struct mbuf *m, int subtype)
{

	switch (subtype) {
	case IEEE80211_FC0_SUBTYPE_BAR:
		ieee80211_recv_bar(ni, m);
		break;
	}
}

/*
 * Parse meshpeering action ie's for MPM frames
 */
static const struct ieee80211_meshpeer_ie *
mesh_parse_meshpeering_action(struct ieee80211_node *ni,
	const struct ieee80211_frame *wh,	/* XXX for VERIFY_LENGTH */
	const uint8_t *frm, const uint8_t *efrm,
	struct ieee80211_meshpeer_ie *mp, uint8_t subtype)
{
	struct ieee80211vap *vap = ni->ni_vap;
	const struct ieee80211_meshpeer_ie *mpie;
	uint16_t args[3];
	const uint8_t *meshid, *meshconf;
	uint8_t sendclose = 0; /* 1 = MPM frame rejected, close will be sent */

	meshid = meshconf = NULL;
	while (efrm - frm > 1) {
		IEEE80211_VERIFY_LENGTH(efrm - frm, frm[1] + 2, return NULL);
		switch (*frm) {
		case IEEE80211_ELEMID_MESHID:
			meshid = frm;
			break;
		case IEEE80211_ELEMID_MESHCONF:
			meshconf = frm;
			break;
		case IEEE80211_ELEMID_MESHPEER:
			mpie = (const struct ieee80211_meshpeer_ie *) frm;
			memset(mp, 0, sizeof(*mp));
			mp->peer_len = mpie->peer_len;
			mp->peer_proto = le16dec(&mpie->peer_proto);
			mp->peer_llinkid = le16dec(&mpie->peer_llinkid);
			switch (subtype) {
			case IEEE80211_ACTION_MESHPEERING_CONFIRM:
				mp->peer_linkid =
				    le16dec(&mpie->peer_linkid);
				break;
			case IEEE80211_ACTION_MESHPEERING_CLOSE:
				/* NB: peer link ID is optional */
				if (mpie->peer_len ==
				    (IEEE80211_MPM_BASE_SZ + 2)) {
					mp->peer_linkid = 0;
					mp->peer_rcode =
					    le16dec(&mpie->peer_linkid);
				} else {
					mp->peer_linkid =
					    le16dec(&mpie->peer_linkid);
					mp->peer_rcode =
					    le16dec(&mpie->peer_rcode);
				}
				break;
			}
			break;
		}
		frm += frm[1] + 2;
	}

	/*
	 * Verify the contents of the frame.
	 * If it fails validation, close the peer link.
	 */
	if (mesh_verify_meshpeer(vap, subtype, (const uint8_t *)mp)) {
		sendclose = 1;
		IEEE80211_DISCARD(vap,
		    IEEE80211_MSG_ACTION | IEEE80211_MSG_MESH,
		    wh, NULL, "%s", "MPM validation failed");
	}

	/* If meshid is not the same reject any frames type. */
	if (sendclose == 0 && mesh_verify_meshid(vap, meshid)) {
		sendclose = 1;
		IEEE80211_DISCARD(vap,
		    IEEE80211_MSG_ACTION | IEEE80211_MSG_MESH,
		    wh, NULL, "%s", "not for our mesh");
		if (subtype == IEEE80211_ACTION_MESHPEERING_CLOSE) {
			/*
			 * Standard not clear about this, if we dont ignore
			 * there will be an endless loop between nodes sending
			 * CLOSE frames between each other with wrong meshid.
			 * Discard and timers will bring FSM to IDLE state.
			 */
			return NULL;
		}
	}
	
	/*
	 * Close frames are accepted if meshid is the same.
	 * Verify the other two types.
	 */
	if (sendclose == 0 && subtype != IEEE80211_ACTION_MESHPEERING_CLOSE &&
	    mesh_verify_meshconf(vap, meshconf)) {
		sendclose = 1;
		IEEE80211_DISCARD(vap,
		    IEEE80211_MSG_ACTION | IEEE80211_MSG_MESH,
		    wh, NULL, "%s", "configuration missmatch");
	}

	if (sendclose) {
		vap->iv_stats.is_rx_mgtdiscard++;
		switch (ni->ni_mlstate) {
		case IEEE80211_NODE_MESH_IDLE:
		case IEEE80211_NODE_MESH_ESTABLISHED:
		case IEEE80211_NODE_MESH_HOLDING:
			/* ignore */
			break;
		case IEEE80211_NODE_MESH_OPENSNT:
		case IEEE80211_NODE_MESH_OPENRCV:
		case IEEE80211_NODE_MESH_CONFIRMRCV:
			args[0] = ni->ni_mlpid;
			args[1] = ni->ni_mllid;
			/* Reason codes for rejection */
			switch (subtype) {
			case IEEE80211_ACTION_MESHPEERING_OPEN:
				args[2] = IEEE80211_REASON_MESH_CPVIOLATION;
				break;
			case IEEE80211_ACTION_MESHPEERING_CONFIRM:
				args[2] = IEEE80211_REASON_MESH_INCONS_PARAMS;
				break;
			}
			ieee80211_send_action(ni,
			    IEEE80211_ACTION_CAT_SELF_PROT,
			    IEEE80211_ACTION_MESHPEERING_CLOSE,
			    args);
			mesh_linkchange(ni, IEEE80211_NODE_MESH_HOLDING);
			mesh_peer_timeout_setup(ni);
			break;
		}
		return NULL;
	}
	
	return (const struct ieee80211_meshpeer_ie *) mp;
}

static int
mesh_recv_action_meshpeering_open(struct ieee80211_node *ni,
	const struct ieee80211_frame *wh,
	const uint8_t *frm, const uint8_t *efrm)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211_mesh_state *ms = vap->iv_mesh;
	struct ieee80211_meshpeer_ie ie;
	const struct ieee80211_meshpeer_ie *meshpeer;
	uint16_t args[3];

	/* +2+2 for action + code + capabilites */
	meshpeer = mesh_parse_meshpeering_action(ni, wh, frm+2+2, efrm, &ie,
	    IEEE80211_ACTION_MESHPEERING_OPEN);
	if (meshpeer == NULL) {
		return 0;
	}

	/* XXX move up */
	IEEE80211_NOTE(vap, IEEE80211_MSG_ACTION | IEEE80211_MSG_MESH, ni,
	    "recv PEER OPEN, lid 0x%x", meshpeer->peer_llinkid);

	switch (ni->ni_mlstate) {
	case IEEE80211_NODE_MESH_IDLE:
		/* Reject open request if reached our maximum neighbor count */
		if (ms->ms_neighbors >= IEEE80211_MESH_MAX_NEIGHBORS) {
			args[0] = meshpeer->peer_llinkid;
			args[1] = 0;
			args[2] = IEEE80211_REASON_MESH_MAX_PEERS;
			ieee80211_send_action(ni,
			    IEEE80211_ACTION_CAT_SELF_PROT,
			    IEEE80211_ACTION_MESHPEERING_CLOSE,
			    args);
			/* stay in IDLE state */
			return (0);
		}
		/* Open frame accepted */
		mesh_linkchange(ni, IEEE80211_NODE_MESH_OPENRCV);
		ni->ni_mllid = meshpeer->peer_llinkid;
		ni->ni_mlpid = mesh_generateid(vap);
		if (ni->ni_mlpid == 0)
			return 0;		/* XXX */
		args[0] = ni->ni_mlpid;
		/* Announce we're open too... */
		ieee80211_send_action(ni,
		    IEEE80211_ACTION_CAT_SELF_PROT,
		    IEEE80211_ACTION_MESHPEERING_OPEN, args);
		/* ...and confirm the link. */
		args[0] = ni->ni_mlpid;
		args[1] = ni->ni_mllid;
		ieee80211_send_action(ni,
		    IEEE80211_ACTION_CAT_SELF_PROT,
		    IEEE80211_ACTION_MESHPEERING_CONFIRM,
		    args);
		mesh_peer_timeout_setup(ni);
		break;
	case IEEE80211_NODE_MESH_OPENRCV:
		/* Wrong Link ID */
		if (ni->ni_mllid != meshpeer->peer_llinkid) {
			args[0] = ni->ni_mllid;
			args[1] = ni->ni_mlpid;
			args[2] = IEEE80211_REASON_PEER_LINK_CANCELED;
			ieee80211_send_action(ni,
			    IEEE80211_ACTION_CAT_SELF_PROT,
			    IEEE80211_ACTION_MESHPEERING_CLOSE,
			    args);
			mesh_linkchange(ni, IEEE80211_NODE_MESH_HOLDING);
			mesh_peer_timeout_setup(ni);
			break;
		}
		/* Duplicate open, confirm again. */
		args[0] = ni->ni_mlpid;
		args[1] = ni->ni_mllid;
		ieee80211_send_action(ni,
		    IEEE80211_ACTION_CAT_SELF_PROT,
		    IEEE80211_ACTION_MESHPEERING_CONFIRM,
		    args);
		break;
	case IEEE80211_NODE_MESH_OPENSNT:
		ni->ni_mllid = meshpeer->peer_llinkid;
		mesh_linkchange(ni, IEEE80211_NODE_MESH_OPENRCV);
		args[0] = ni->ni_mlpid;
		args[1] = ni->ni_mllid;
		ieee80211_send_action(ni,
		    IEEE80211_ACTION_CAT_SELF_PROT,
		    IEEE80211_ACTION_MESHPEERING_CONFIRM,
		    args);
		/* NB: don't setup/clear any timeout */
		break;
	case IEEE80211_NODE_MESH_CONFIRMRCV:
		if (ni->ni_mlpid != meshpeer->peer_linkid ||
		    ni->ni_mllid != meshpeer->peer_llinkid) {
			args[0] = ni->ni_mlpid;
			args[1] = ni->ni_mllid;
			args[2] = IEEE80211_REASON_PEER_LINK_CANCELED;
			ieee80211_send_action(ni,
			    IEEE80211_ACTION_CAT_SELF_PROT,
			    IEEE80211_ACTION_MESHPEERING_CLOSE,
			    args);
			mesh_linkchange(ni,
			    IEEE80211_NODE_MESH_HOLDING);
			mesh_peer_timeout_setup(ni);
			break;
		}
		mesh_linkchange(ni, IEEE80211_NODE_MESH_ESTABLISHED);
		ni->ni_mllid = meshpeer->peer_llinkid;
		args[0] = ni->ni_mlpid;
		args[1] = ni->ni_mllid;
		ieee80211_send_action(ni,
		    IEEE80211_ACTION_CAT_SELF_PROT,
		    IEEE80211_ACTION_MESHPEERING_CONFIRM,
		    args);
		mesh_peer_timeout_stop(ni);
		break;
	case IEEE80211_NODE_MESH_ESTABLISHED:
		if (ni->ni_mllid != meshpeer->peer_llinkid) {
			args[0] = ni->ni_mllid;
			args[1] = ni->ni_mlpid;
			args[2] = IEEE80211_REASON_PEER_LINK_CANCELED;
			ieee80211_send_action(ni,
			    IEEE80211_ACTION_CAT_SELF_PROT,
			    IEEE80211_ACTION_MESHPEERING_CLOSE,
			    args);
			mesh_linkchange(ni, IEEE80211_NODE_MESH_HOLDING);
			mesh_peer_timeout_setup(ni);
			break;
		}
		args[0] = ni->ni_mlpid;
		args[1] = ni->ni_mllid;
		ieee80211_send_action(ni,
		    IEEE80211_ACTION_CAT_SELF_PROT,
		    IEEE80211_ACTION_MESHPEERING_CONFIRM,
		    args);
		break;
	case IEEE80211_NODE_MESH_HOLDING:
		args[0] = ni->ni_mlpid;
		args[1] = meshpeer->peer_llinkid;
		/* Standard not clear about what the reaason code should be */
		args[2] = IEEE80211_REASON_PEER_LINK_CANCELED;
		ieee80211_send_action(ni,
		    IEEE80211_ACTION_CAT_SELF_PROT,
		    IEEE80211_ACTION_MESHPEERING_CLOSE,
		    args);
		break;
	}
	return 0;
}

static int
mesh_recv_action_meshpeering_confirm(struct ieee80211_node *ni,
	const struct ieee80211_frame *wh,
	const uint8_t *frm, const uint8_t *efrm)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211_meshpeer_ie ie;
	const struct ieee80211_meshpeer_ie *meshpeer;
	uint16_t args[3];

	/* +2+2+2+2 for action + code + capabilites + status code + AID */
	meshpeer = mesh_parse_meshpeering_action(ni, wh, frm+2+2+2+2, efrm, &ie,
	    IEEE80211_ACTION_MESHPEERING_CONFIRM);
	if (meshpeer == NULL) {
		return 0;
	}

	IEEE80211_NOTE(vap, IEEE80211_MSG_ACTION | IEEE80211_MSG_MESH, ni,
	    "recv PEER CONFIRM, local id 0x%x, peer id 0x%x",
	    meshpeer->peer_llinkid, meshpeer->peer_linkid);

	switch (ni->ni_mlstate) {
	case IEEE80211_NODE_MESH_OPENRCV:
		mesh_linkchange(ni, IEEE80211_NODE_MESH_ESTABLISHED);
		mesh_peer_timeout_stop(ni);
		break;
	case IEEE80211_NODE_MESH_OPENSNT:
		mesh_linkchange(ni, IEEE80211_NODE_MESH_CONFIRMRCV);
		mesh_peer_timeout_setup(ni);
		break;
	case IEEE80211_NODE_MESH_HOLDING:
		args[0] = ni->ni_mlpid;
		args[1] = meshpeer->peer_llinkid;
		/* Standard not clear about what the reaason code should be */
		args[2] = IEEE80211_REASON_PEER_LINK_CANCELED;
		ieee80211_send_action(ni,
		    IEEE80211_ACTION_CAT_SELF_PROT,
		    IEEE80211_ACTION_MESHPEERING_CLOSE,
		    args);
		break;
	case IEEE80211_NODE_MESH_CONFIRMRCV:
		if (ni->ni_mllid != meshpeer->peer_llinkid) {
			args[0] = ni->ni_mlpid;
			args[1] = ni->ni_mllid;
			args[2] = IEEE80211_REASON_PEER_LINK_CANCELED;
			ieee80211_send_action(ni,
			    IEEE80211_ACTION_CAT_SELF_PROT,
			    IEEE80211_ACTION_MESHPEERING_CLOSE,
			    args);
			mesh_linkchange(ni, IEEE80211_NODE_MESH_HOLDING);
			mesh_peer_timeout_setup(ni);
		}
		break;
	default:
		IEEE80211_DISCARD(vap,
		    IEEE80211_MSG_ACTION | IEEE80211_MSG_MESH,
		    wh, NULL, "received confirm in invalid state %d",
		    ni->ni_mlstate);
		vap->iv_stats.is_rx_mgtdiscard++;
		break;
	}
	return 0;
}

static int
mesh_recv_action_meshpeering_close(struct ieee80211_node *ni,
	const struct ieee80211_frame *wh,
	const uint8_t *frm, const uint8_t *efrm)
{
	struct ieee80211_meshpeer_ie ie;
	const struct ieee80211_meshpeer_ie *meshpeer;
	uint16_t args[3];

	/* +2 for action + code */
	meshpeer = mesh_parse_meshpeering_action(ni, wh, frm+2, efrm, &ie,
	    IEEE80211_ACTION_MESHPEERING_CLOSE);
	if (meshpeer == NULL) {
		return 0;
	}

	/*
	 * XXX: check reason code, for example we could receive
	 * IEEE80211_REASON_MESH_MAX_PEERS then we should not attempt
	 * to peer again.
	 */

	IEEE80211_NOTE(ni->ni_vap, IEEE80211_MSG_ACTION | IEEE80211_MSG_MESH,
	    ni, "%s", "recv PEER CLOSE");

	switch (ni->ni_mlstate) {
	case IEEE80211_NODE_MESH_IDLE:
		/* ignore */
		break;
	case IEEE80211_NODE_MESH_OPENRCV:
	case IEEE80211_NODE_MESH_OPENSNT:
	case IEEE80211_NODE_MESH_CONFIRMRCV:
	case IEEE80211_NODE_MESH_ESTABLISHED:
		args[0] = ni->ni_mlpid;
		args[1] = ni->ni_mllid;
		args[2] = IEEE80211_REASON_MESH_CLOSE_RCVD;
		ieee80211_send_action(ni,
		    IEEE80211_ACTION_CAT_SELF_PROT,
		    IEEE80211_ACTION_MESHPEERING_CLOSE,
		    args);
		mesh_linkchange(ni, IEEE80211_NODE_MESH_HOLDING);
		mesh_peer_timeout_setup(ni);
		break;
	case IEEE80211_NODE_MESH_HOLDING:
		mesh_linkchange(ni, IEEE80211_NODE_MESH_IDLE);
		mesh_peer_timeout_stop(ni);
		break;
	}
	return 0;
}

/*
 * Link Metric handling.
 */
static int
mesh_recv_action_meshlmetric(struct ieee80211_node *ni,
	const struct ieee80211_frame *wh,
	const uint8_t *frm, const uint8_t *efrm)
{
	const struct ieee80211_meshlmetric_ie *ie =
	    (const struct ieee80211_meshlmetric_ie *)
	    (frm+2); /* action + code */
	struct ieee80211_meshlmetric_ie lm_rep;
	
	if (ie->lm_flags & IEEE80211_MESH_LMETRIC_FLAGS_REQ) {
		lm_rep.lm_flags = 0;
		lm_rep.lm_metric = mesh_airtime_calc(ni);
		ieee80211_send_action(ni,
		    IEEE80211_ACTION_CAT_MESH,
		    IEEE80211_ACTION_MESH_LMETRIC,
		    &lm_rep);
	}
	/* XXX: else do nothing for now */
	return 0;
}

/*
 * Parse meshgate action ie's for GANN frames.
 * Returns -1 if parsing fails, otherwise 0.
 */
static int
mesh_parse_meshgate_action(struct ieee80211_node *ni,
    const struct ieee80211_frame *wh,	/* XXX for VERIFY_LENGTH */
    struct ieee80211_meshgann_ie *ie, const uint8_t *frm, const uint8_t *efrm)
{
	struct ieee80211vap *vap = ni->ni_vap;
	const struct ieee80211_meshgann_ie *gannie;

	while (efrm - frm > 1) {
		IEEE80211_VERIFY_LENGTH(efrm - frm, frm[1] + 2, return -1);
		switch (*frm) {
		case IEEE80211_ELEMID_MESHGANN:
			gannie = (const struct ieee80211_meshgann_ie *) frm;
			memset(ie, 0, sizeof(*ie));
			ie->gann_ie = gannie->gann_ie;
			ie->gann_len = gannie->gann_len;
			ie->gann_flags = gannie->gann_flags;
			ie->gann_hopcount = gannie->gann_hopcount;
			ie->gann_ttl = gannie->gann_ttl;
			IEEE80211_ADDR_COPY(ie->gann_addr, gannie->gann_addr);
			ie->gann_seq = le32dec(&gannie->gann_seq);
			ie->gann_interval = le16dec(&gannie->gann_interval);
			break;
		}
		frm += frm[1] + 2;
	}

	return 0;
}

/*
 * Mesh Gate Announcement handling.
 */
static int
mesh_recv_action_meshgate(struct ieee80211_node *ni,
	const struct ieee80211_frame *wh,
	const uint8_t *frm, const uint8_t *efrm)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211_mesh_state *ms = vap->iv_mesh;
	struct ieee80211_mesh_gate_route *gr, *next;
	struct ieee80211_mesh_route *rt_gate;
	struct ieee80211_meshgann_ie pgann;
	struct ieee80211_meshgann_ie ie;
	int found = 0;

	/* +2 for action + code */
	if (mesh_parse_meshgate_action(ni, wh, &ie, frm+2, efrm) != 0) {
		IEEE80211_DISCARD_MAC(vap, IEEE80211_MSG_MESH,
		    ni->ni_macaddr, NULL, "%s",
		    "GANN parsing failed");
		vap->iv_stats.is_rx_mgtdiscard++;
		return (0);
	}

	if (IEEE80211_ADDR_EQ(vap->iv_myaddr, ie.gann_addr))
		return 0;

	IEEE80211_NOTE_MAC(vap, IEEE80211_MSG_MESH, ni->ni_macaddr,
	    "received GANN, meshgate: %6D (seq %u)", ie.gann_addr, ":",
	    ie.gann_seq);

	if (ms == NULL)
		return (0);
	MESH_RT_LOCK(ms);
	TAILQ_FOREACH_SAFE(gr, &ms->ms_known_gates, gr_next, next) {
		if (!IEEE80211_ADDR_EQ(gr->gr_addr, ie.gann_addr))
			continue;
		if (ie.gann_seq <= gr->gr_lastseq) {
			IEEE80211_DISCARD_MAC(vap, IEEE80211_MSG_MESH,
			    ni->ni_macaddr, NULL,
			    "GANN old seqno %u <= %u",
			    ie.gann_seq, gr->gr_lastseq);
			MESH_RT_UNLOCK(ms);
			return (0);
		}
		/* corresponding mesh gate found & GANN accepted */
		found = 1;
		break;

	}
	if (found == 0) {
		/* this GANN is from a new mesh Gate add it to known table. */
		IEEE80211_NOTE_MAC(vap, IEEE80211_MSG_MESH, ie.gann_addr,
		    "stored new GANN information, seq %u.", ie.gann_seq);
		gr = IEEE80211_MALLOC(ALIGN(sizeof(struct ieee80211_mesh_gate_route)),
		    M_80211_MESH_GT_RT,
		    IEEE80211_M_NOWAIT | IEEE80211_M_ZERO);
		IEEE80211_ADDR_COPY(gr->gr_addr, ie.gann_addr);
		TAILQ_INSERT_TAIL(&ms->ms_known_gates, gr, gr_next);
	}
	gr->gr_lastseq = ie.gann_seq;

	/* check if we have a path to this gate */
	rt_gate = mesh_rt_find_locked(ms, gr->gr_addr);
	if (rt_gate != NULL &&
	    rt_gate->rt_flags & IEEE80211_MESHRT_FLAGS_VALID) {
		gr->gr_route = rt_gate;
		rt_gate->rt_flags |= IEEE80211_MESHRT_FLAGS_GATE;
	}

	MESH_RT_UNLOCK(ms);

	/* popagate only if decremented ttl >= 1 && forwarding is enabled */
	if ((ie.gann_ttl - 1) < 1 && !(ms->ms_flags & IEEE80211_MESHFLAGS_FWD))
		return 0;
	pgann.gann_flags = ie.gann_flags; /* Reserved */
	pgann.gann_hopcount = ie.gann_hopcount + 1;
	pgann.gann_ttl = ie.gann_ttl - 1;
	IEEE80211_ADDR_COPY(pgann.gann_addr, ie.gann_addr);
	pgann.gann_seq = ie.gann_seq;
	pgann.gann_interval = ie.gann_interval;

	IEEE80211_NOTE_MAC(vap, IEEE80211_MSG_MESH, ie.gann_addr,
	    "%s", "propagate GANN");

	ieee80211_send_action(vap->iv_bss, IEEE80211_ACTION_CAT_MESH,
	    IEEE80211_ACTION_MESH_GANN, &pgann);

	return 0;
}

static int
mesh_send_action(struct ieee80211_node *ni,
    const uint8_t sa[IEEE80211_ADDR_LEN],
    const uint8_t da[IEEE80211_ADDR_LEN],
    struct mbuf *m)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211com *ic = ni->ni_ic;
	struct ieee80211_bpf_params params;
	int ret;

	KASSERT(ni != NULL, ("null node"));

	if (vap->iv_state == IEEE80211_S_CAC) {
		IEEE80211_NOTE(vap, IEEE80211_MSG_OUTPUT, ni,
		    "block %s frame in CAC state", "Mesh action");
		vap->iv_stats.is_tx_badstate++;
		ieee80211_free_node(ni);
		m_freem(m);
		return EIO;		/* XXX */
	}

	M_PREPEND(m, sizeof(struct ieee80211_frame), M_NOWAIT);
	if (m == NULL) {
		ieee80211_free_node(ni);
		return ENOMEM;
	}

	IEEE80211_TX_LOCK(ic);
	ieee80211_send_setup(ni, m,
	     IEEE80211_FC0_TYPE_MGT | IEEE80211_FC0_SUBTYPE_ACTION,
	     IEEE80211_NONQOS_TID, sa, da, sa);
	m->m_flags |= M_ENCAP;		/* mark encapsulated */

	memset(&params, 0, sizeof(params));
	params.ibp_pri = WME_AC_VO;
	params.ibp_rate0 = ni->ni_txparms->mgmtrate;
	if (IEEE80211_IS_MULTICAST(da))
		params.ibp_try0 = 1;
	else
		params.ibp_try0 = ni->ni_txparms->maxretry;
	params.ibp_power = ni->ni_txpower;

	IEEE80211_NODE_STAT(ni, tx_mgmt);

	ret = ieee80211_raw_output(vap, ni, m, &params);
	IEEE80211_TX_UNLOCK(ic);
	return (ret);
}

#define	ADDSHORT(frm, v) do {			\
	frm[0] = (v) & 0xff;			\
	frm[1] = (v) >> 8;			\
	frm += 2;				\
} while (0)
#define	ADDWORD(frm, v) do {			\
	frm[0] = (v) & 0xff;			\
	frm[1] = ((v) >> 8) & 0xff;		\
	frm[2] = ((v) >> 16) & 0xff;		\
	frm[3] = ((v) >> 24) & 0xff;		\
	frm += 4;				\
} while (0)

static int
mesh_send_action_meshpeering_open(struct ieee80211_node *ni,
	int category, int action, void *args0)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211com *ic = ni->ni_ic;
	uint16_t *args = args0;
	const struct ieee80211_rateset *rs;
	struct mbuf *m;
	uint8_t *frm;

	IEEE80211_NOTE(vap, IEEE80211_MSG_ACTION | IEEE80211_MSG_MESH, ni,
	    "send PEER OPEN action: localid 0x%x", args[0]);

	IEEE80211_DPRINTF(vap, IEEE80211_MSG_NODE,
	    "ieee80211_ref_node (%s:%u) %p<%s> refcnt %d\n", __func__, __LINE__,
	    ni, ether_sprintf(ni->ni_macaddr), ieee80211_node_refcnt(ni)+1);
	ieee80211_ref_node(ni);

	m = ieee80211_getmgtframe(&frm,
	    ic->ic_headroom + sizeof(struct ieee80211_frame),
	    sizeof(uint16_t)	/* action+category */
	    + sizeof(uint16_t)	/* capabilites */
	    + 2 + IEEE80211_RATE_SIZE
	    + 2 + (IEEE80211_RATE_MAXSIZE - IEEE80211_RATE_SIZE)
	    + 2 + IEEE80211_MESHID_LEN
	    + sizeof(struct ieee80211_meshconf_ie)
	    + sizeof(struct ieee80211_meshpeer_ie)
	);
	if (m != NULL) {
		/*
		 * mesh peer open action frame format:
		 *   [1] category
		 *   [1] action
		 *   [2] capabilities
		 *   [tlv] rates
		 *   [tlv] xrates
		 *   [tlv] mesh id
		 *   [tlv] mesh conf
		 *   [tlv] mesh peer link mgmt
		 */
		*frm++ = category;
		*frm++ = action;
		ADDSHORT(frm, ieee80211_getcapinfo(vap, ni->ni_chan));
		rs = ieee80211_get_suprates(ic, ic->ic_curchan);
		frm = ieee80211_add_rates(frm, rs);
		frm = ieee80211_add_xrates(frm, rs);
		frm = ieee80211_add_meshid(frm, vap);
		frm = ieee80211_add_meshconf(frm, vap);
		frm = ieee80211_add_meshpeer(frm, IEEE80211_ACTION_MESHPEERING_OPEN,
		    args[0], 0, 0);
		m->m_pkthdr.len = m->m_len = frm - mtod(m, uint8_t *);
		return mesh_send_action(ni, vap->iv_myaddr, ni->ni_macaddr, m);
	} else {
		vap->iv_stats.is_tx_nobuf++;
		ieee80211_free_node(ni);
		return ENOMEM;
	}
}

static int
mesh_send_action_meshpeering_confirm(struct ieee80211_node *ni,
	int category, int action, void *args0)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211com *ic = ni->ni_ic;
	uint16_t *args = args0;
	const struct ieee80211_rateset *rs;
	struct mbuf *m;
	uint8_t *frm;

	IEEE80211_NOTE(vap, IEEE80211_MSG_ACTION | IEEE80211_MSG_MESH, ni,
	    "send PEER CONFIRM action: localid 0x%x, peerid 0x%x",
	    args[0], args[1]);

	IEEE80211_DPRINTF(vap, IEEE80211_MSG_NODE,
	    "ieee80211_ref_node (%s:%u) %p<%s> refcnt %d\n", __func__, __LINE__,
	    ni, ether_sprintf(ni->ni_macaddr), ieee80211_node_refcnt(ni)+1);
	ieee80211_ref_node(ni);

	m = ieee80211_getmgtframe(&frm,
	    ic->ic_headroom + sizeof(struct ieee80211_frame),
	    sizeof(uint16_t)	/* action+category */
	    + sizeof(uint16_t)	/* capabilites */
	    + sizeof(uint16_t)	/* status code */
	    + sizeof(uint16_t)	/* AID */
	    + 2 + IEEE80211_RATE_SIZE
	    + 2 + (IEEE80211_RATE_MAXSIZE - IEEE80211_RATE_SIZE)
	    + 2 + IEEE80211_MESHID_LEN
	    + sizeof(struct ieee80211_meshconf_ie)
	    + sizeof(struct ieee80211_meshpeer_ie)
	);
	if (m != NULL) {
		/*
		 * mesh peer confirm action frame format:
		 *   [1] category
		 *   [1] action
		 *   [2] capabilities
		 *   [2] status code
		 *   [2] association id (peer ID)
		 *   [tlv] rates
		 *   [tlv] xrates
		 *   [tlv] mesh id
		 *   [tlv] mesh conf
		 *   [tlv] mesh peer link mgmt
		 */
		*frm++ = category;
		*frm++ = action;
		ADDSHORT(frm, ieee80211_getcapinfo(vap, ni->ni_chan));
		ADDSHORT(frm, 0);		/* status code */
		ADDSHORT(frm, args[1]);		/* AID */
		rs = ieee80211_get_suprates(ic, ic->ic_curchan);
		frm = ieee80211_add_rates(frm, rs);
		frm = ieee80211_add_xrates(frm, rs);
		frm = ieee80211_add_meshid(frm, vap);
		frm = ieee80211_add_meshconf(frm, vap);
		frm = ieee80211_add_meshpeer(frm,
		    IEEE80211_ACTION_MESHPEERING_CONFIRM,
		    args[0], args[1], 0);
		m->m_pkthdr.len = m->m_len = frm - mtod(m, uint8_t *);
		return mesh_send_action(ni, vap->iv_myaddr, ni->ni_macaddr, m);
	} else {
		vap->iv_stats.is_tx_nobuf++;
		ieee80211_free_node(ni);
		return ENOMEM;
	}
}

static int
mesh_send_action_meshpeering_close(struct ieee80211_node *ni,
	int category, int action, void *args0)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211com *ic = ni->ni_ic;
	uint16_t *args = args0;
	struct mbuf *m;
	uint8_t *frm;

	IEEE80211_NOTE(vap, IEEE80211_MSG_ACTION | IEEE80211_MSG_MESH, ni,
	    "send PEER CLOSE action: localid 0x%x, peerid 0x%x reason %d (%s)",
	    args[0], args[1], args[2], ieee80211_reason_to_string(args[2]));

	IEEE80211_DPRINTF(vap, IEEE80211_MSG_NODE,
	    "ieee80211_ref_node (%s:%u) %p<%s> refcnt %d\n", __func__, __LINE__,
	    ni, ether_sprintf(ni->ni_macaddr), ieee80211_node_refcnt(ni)+1);
	ieee80211_ref_node(ni);

	m = ieee80211_getmgtframe(&frm,
	    ic->ic_headroom + sizeof(struct ieee80211_frame),
	    sizeof(uint16_t)	/* action+category */
	    + sizeof(uint16_t)	/* reason code */
	    + 2 + IEEE80211_MESHID_LEN
	    + sizeof(struct ieee80211_meshpeer_ie)
	);
	if (m != NULL) {
		/*
		 * mesh peer close action frame format:
		 *   [1] category
		 *   [1] action
		 *   [tlv] mesh id
		 *   [tlv] mesh peer link mgmt
		 */
		*frm++ = category;
		*frm++ = action;
		frm = ieee80211_add_meshid(frm, vap);
		frm = ieee80211_add_meshpeer(frm,
		    IEEE80211_ACTION_MESHPEERING_CLOSE,
		    args[0], args[1], args[2]);
		m->m_pkthdr.len = m->m_len = frm - mtod(m, uint8_t *);
		return mesh_send_action(ni, vap->iv_myaddr, ni->ni_macaddr, m);
	} else {
		vap->iv_stats.is_tx_nobuf++;
		ieee80211_free_node(ni);
		return ENOMEM;
	}
}

static int
mesh_send_action_meshlmetric(struct ieee80211_node *ni,
	int category, int action, void *arg0)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211com *ic = ni->ni_ic;
	struct ieee80211_meshlmetric_ie *ie = arg0;
	struct mbuf *m;
	uint8_t *frm;

	if (ie->lm_flags & IEEE80211_MESH_LMETRIC_FLAGS_REQ) {
		IEEE80211_NOTE(vap, IEEE80211_MSG_ACTION | IEEE80211_MSG_MESH,
		    ni, "%s", "send LINK METRIC REQUEST action");
	} else {
		IEEE80211_NOTE(vap, IEEE80211_MSG_ACTION | IEEE80211_MSG_MESH,
		    ni, "send LINK METRIC REPLY action: metric 0x%x",
		    ie->lm_metric);
	}
	IEEE80211_DPRINTF(vap, IEEE80211_MSG_NODE,
	    "ieee80211_ref_node (%s:%u) %p<%s> refcnt %d\n", __func__, __LINE__,
	    ni, ether_sprintf(ni->ni_macaddr), ieee80211_node_refcnt(ni)+1);
	ieee80211_ref_node(ni);

	m = ieee80211_getmgtframe(&frm,
	    ic->ic_headroom + sizeof(struct ieee80211_frame),
	    sizeof(uint16_t) +	/* action+category */
	    sizeof(struct ieee80211_meshlmetric_ie)
	);
	if (m != NULL) {
		/*
		 * mesh link metric
		 *   [1] category
		 *   [1] action
		 *   [tlv] mesh link metric
		 */
		*frm++ = category;
		*frm++ = action;
		frm = ieee80211_add_meshlmetric(frm,
		    ie->lm_flags, ie->lm_metric);
		m->m_pkthdr.len = m->m_len = frm - mtod(m, uint8_t *);
		return mesh_send_action(ni, vap->iv_myaddr, ni->ni_macaddr, m);
	} else {
		vap->iv_stats.is_tx_nobuf++;
		ieee80211_free_node(ni);
		return ENOMEM;
	}
}

static int
mesh_send_action_meshgate(struct ieee80211_node *ni,
	int category, int action, void *arg0)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211com *ic = ni->ni_ic;
	struct ieee80211_meshgann_ie *ie = arg0;
	struct mbuf *m;
	uint8_t *frm;

	IEEE80211_DPRINTF(vap, IEEE80211_MSG_NODE,
	    "ieee80211_ref_node (%s:%u) %p<%s> refcnt %d\n", __func__, __LINE__,
	    ni, ether_sprintf(ni->ni_macaddr), ieee80211_node_refcnt(ni)+1);
	ieee80211_ref_node(ni);

	m = ieee80211_getmgtframe(&frm,
	    ic->ic_headroom + sizeof(struct ieee80211_frame),
	    sizeof(uint16_t) +	/* action+category */
	    IEEE80211_MESHGANN_BASE_SZ
	);
	if (m != NULL) {
		/*
		 * mesh link metric
		 *   [1] category
		 *   [1] action
		 *   [tlv] mesh gate annoucement
		 */
		*frm++ = category;
		*frm++ = action;
		frm = ieee80211_add_meshgate(frm, ie);
		m->m_pkthdr.len = m->m_len = frm - mtod(m, uint8_t *);
		return mesh_send_action(ni, vap->iv_myaddr, broadcastaddr, m);
	} else {
		vap->iv_stats.is_tx_nobuf++;
		ieee80211_free_node(ni);
		return ENOMEM;
	}
}

static void
mesh_peer_timeout_setup(struct ieee80211_node *ni)
{
	switch (ni->ni_mlstate) {
	case IEEE80211_NODE_MESH_HOLDING:
		ni->ni_mltval = ieee80211_mesh_holdingtimeout;
		break;
	case IEEE80211_NODE_MESH_CONFIRMRCV:
		ni->ni_mltval = ieee80211_mesh_confirmtimeout;
		break;
	case IEEE80211_NODE_MESH_IDLE:
		ni->ni_mltval = 0;
		break;
	default:
		ni->ni_mltval = ieee80211_mesh_retrytimeout;
		break;
	}
	if (ni->ni_mltval)
		callout_reset(&ni->ni_mltimer, ni->ni_mltval,
		    mesh_peer_timeout_cb, ni);
}

/*
 * Same as above but backoffs timer statisically 50%.
 */
static void
mesh_peer_timeout_backoff(struct ieee80211_node *ni)
{
	uint32_t r;
	
	r = arc4random();
	ni->ni_mltval += r % ni->ni_mltval;
	callout_reset(&ni->ni_mltimer, ni->ni_mltval, mesh_peer_timeout_cb,
	    ni);
}

static __inline void
mesh_peer_timeout_stop(struct ieee80211_node *ni)
{
	callout_drain(&ni->ni_mltimer);
}

static void
mesh_peer_backoff_cb(void *arg)
{
	struct ieee80211_node *ni = (struct ieee80211_node *)arg;

	/* After backoff timeout, try to peer automatically again. */
	ni->ni_mlhcnt = 0;
}

/*
 * Mesh Peer Link Management FSM timeout handling.
 */
static void
mesh_peer_timeout_cb(void *arg)
{
	struct ieee80211_node *ni = (struct ieee80211_node *)arg;
	uint16_t args[3];

	IEEE80211_NOTE(ni->ni_vap, IEEE80211_MSG_MESH,
	    ni, "mesh link timeout, state %d, retry counter %d",
	    ni->ni_mlstate, ni->ni_mlrcnt);
	
	switch (ni->ni_mlstate) {
	case IEEE80211_NODE_MESH_IDLE:
	case IEEE80211_NODE_MESH_ESTABLISHED:
		break;
	case IEEE80211_NODE_MESH_OPENSNT:
	case IEEE80211_NODE_MESH_OPENRCV:
		if (ni->ni_mlrcnt == ieee80211_mesh_maxretries) {
			args[0] = ni->ni_mlpid;
			args[2] = IEEE80211_REASON_MESH_MAX_RETRIES;
			ieee80211_send_action(ni,
			    IEEE80211_ACTION_CAT_SELF_PROT,
			    IEEE80211_ACTION_MESHPEERING_CLOSE, args);
			ni->ni_mlrcnt = 0;
			mesh_linkchange(ni, IEEE80211_NODE_MESH_HOLDING);
			mesh_peer_timeout_setup(ni);
		} else {
			args[0] = ni->ni_mlpid;
			ieee80211_send_action(ni,
			    IEEE80211_ACTION_CAT_SELF_PROT,
			    IEEE80211_ACTION_MESHPEERING_OPEN, args);
			ni->ni_mlrcnt++;
			mesh_peer_timeout_backoff(ni);
		}
		break;
	case IEEE80211_NODE_MESH_CONFIRMRCV:
		args[0] = ni->ni_mlpid;
		args[2] = IEEE80211_REASON_MESH_CONFIRM_TIMEOUT;
		ieee80211_send_action(ni,
		    IEEE80211_ACTION_CAT_SELF_PROT,
		    IEEE80211_ACTION_MESHPEERING_CLOSE, args);
		mesh_linkchange(ni, IEEE80211_NODE_MESH_HOLDING);
		mesh_peer_timeout_setup(ni);
		break;
	case IEEE80211_NODE_MESH_HOLDING:
		ni->ni_mlhcnt++;
		if (ni->ni_mlhcnt >= ieee80211_mesh_maxholding)
			callout_reset(&ni->ni_mlhtimer,
			    ieee80211_mesh_backofftimeout,
			    mesh_peer_backoff_cb, ni);
		mesh_linkchange(ni, IEEE80211_NODE_MESH_IDLE);
		break;
	}
}

static int
mesh_verify_meshid(struct ieee80211vap *vap, const uint8_t *ie)
{
	struct ieee80211_mesh_state *ms = vap->iv_mesh;

	if (ie == NULL || ie[1] != ms->ms_idlen)
		return 1;
	return memcmp(ms->ms_id, ie + 2, ms->ms_idlen);
}

/*
 * Check if we are using the same algorithms for this mesh.
 */
static int
mesh_verify_meshconf(struct ieee80211vap *vap, const uint8_t *ie)
{
	const struct ieee80211_meshconf_ie *meshconf =
	    (const struct ieee80211_meshconf_ie *) ie;
	const struct ieee80211_mesh_state *ms = vap->iv_mesh;

	if (meshconf == NULL)
		return 1;
	if (meshconf->conf_pselid != ms->ms_ppath->mpp_ie) {
		IEEE80211_DPRINTF(vap, IEEE80211_MSG_MESH,
		    "unknown path selection algorithm: 0x%x\n",
		    meshconf->conf_pselid);
		return 1;
	}
	if (meshconf->conf_pmetid != ms->ms_pmetric->mpm_ie) {
		IEEE80211_DPRINTF(vap, IEEE80211_MSG_MESH,
		    "unknown path metric algorithm: 0x%x\n",
		    meshconf->conf_pmetid);
		return 1;
	}
	if (meshconf->conf_ccid != 0) {
		IEEE80211_DPRINTF(vap, IEEE80211_MSG_MESH,
		    "unknown congestion control algorithm: 0x%x\n",
		    meshconf->conf_ccid);
		return 1;
	}
	if (meshconf->conf_syncid != IEEE80211_MESHCONF_SYNC_NEIGHOFF) {
		IEEE80211_DPRINTF(vap, IEEE80211_MSG_MESH,
		    "unknown sync algorithm: 0x%x\n",
		    meshconf->conf_syncid);
		return 1;
	}
	if (meshconf->conf_authid != 0) {
		IEEE80211_DPRINTF(vap, IEEE80211_MSG_MESH,
		    "unknown auth auth algorithm: 0x%x\n",
		    meshconf->conf_pselid);
		return 1;
	}
	/* Not accepting peers */
	if (!(meshconf->conf_cap & IEEE80211_MESHCONF_CAP_AP)) {
		IEEE80211_DPRINTF(vap, IEEE80211_MSG_MESH,
		    "not accepting peers: 0x%x\n", meshconf->conf_cap);
		return 1;
	}
	return 0;
}

static int
mesh_verify_meshpeer(struct ieee80211vap *vap, uint8_t subtype,
    const uint8_t *ie)
{
	const struct ieee80211_meshpeer_ie *meshpeer =
	    (const struct ieee80211_meshpeer_ie *) ie;

	if (meshpeer == NULL ||
	    meshpeer->peer_len < IEEE80211_MPM_BASE_SZ ||
	    meshpeer->peer_len > IEEE80211_MPM_MAX_SZ)
		return 1;
	if (meshpeer->peer_proto != IEEE80211_MPPID_MPM) {
		IEEE80211_DPRINTF(vap,
		    IEEE80211_MSG_ACTION | IEEE80211_MSG_MESH,
		    "Only MPM protocol is supported (proto: 0x%02X)",
		    meshpeer->peer_proto);
		return 1;
	}
	switch (subtype) {
	case IEEE80211_ACTION_MESHPEERING_OPEN:
		if (meshpeer->peer_len != IEEE80211_MPM_BASE_SZ)
			return 1;
		break;
	case IEEE80211_ACTION_MESHPEERING_CONFIRM:
		if (meshpeer->peer_len != IEEE80211_MPM_BASE_SZ + 2)
			return 1;
		break;
	case IEEE80211_ACTION_MESHPEERING_CLOSE:
		if (meshpeer->peer_len < IEEE80211_MPM_BASE_SZ + 2)
			return 1;
		if (meshpeer->peer_len == (IEEE80211_MPM_BASE_SZ + 2) &&
		    meshpeer->peer_linkid != 0)
			return 1;
		if (meshpeer->peer_rcode == 0)
			return 1;
		break;
	}
	return 0;
}

/*
 * Add a Mesh ID IE to a frame.
 */
uint8_t *
ieee80211_add_meshid(uint8_t *frm, struct ieee80211vap *vap)
{
	struct ieee80211_mesh_state *ms = vap->iv_mesh;

	KASSERT(vap->iv_opmode == IEEE80211_M_MBSS, ("not a mbss vap"));

	*frm++ = IEEE80211_ELEMID_MESHID;
	*frm++ = ms->ms_idlen;
	memcpy(frm, ms->ms_id, ms->ms_idlen);
	return frm + ms->ms_idlen;
}

/*
 * Add a Mesh Configuration IE to a frame.
 * For now just use HWMP routing, Airtime link metric, Null Congestion
 * Signaling, Null Sync Protocol and Null Authentication.
 */
uint8_t *
ieee80211_add_meshconf(uint8_t *frm, struct ieee80211vap *vap)
{
	const struct ieee80211_mesh_state *ms = vap->iv_mesh;
	uint16_t caps;

	KASSERT(vap->iv_opmode == IEEE80211_M_MBSS, ("not a MBSS vap"));

	*frm++ = IEEE80211_ELEMID_MESHCONF;
	*frm++ = IEEE80211_MESH_CONF_SZ;
	*frm++ = ms->ms_ppath->mpp_ie;		/* path selection */
	*frm++ = ms->ms_pmetric->mpm_ie;	/* link metric */
	*frm++ = IEEE80211_MESHCONF_CC_DISABLED;
	*frm++ = IEEE80211_MESHCONF_SYNC_NEIGHOFF;
	*frm++ = IEEE80211_MESHCONF_AUTH_DISABLED;
	/* NB: set the number of neighbors before the rest */
	*frm = (ms->ms_neighbors > IEEE80211_MESH_MAX_NEIGHBORS ?
	    IEEE80211_MESH_MAX_NEIGHBORS : ms->ms_neighbors) << 1;
	if (ms->ms_flags & IEEE80211_MESHFLAGS_GATE)
		*frm |= IEEE80211_MESHCONF_FORM_GATE;
	frm += 1;
	caps = 0;
	if (ms->ms_flags & IEEE80211_MESHFLAGS_AP)
		caps |= IEEE80211_MESHCONF_CAP_AP;
	if (ms->ms_flags & IEEE80211_MESHFLAGS_FWD)
		caps |= IEEE80211_MESHCONF_CAP_FWRD;
	*frm++ = caps;
	return frm;
}

/*
 * Add a Mesh Peer Management IE to a frame.
 */
uint8_t *
ieee80211_add_meshpeer(uint8_t *frm, uint8_t subtype, uint16_t localid,
    uint16_t peerid, uint16_t reason)
{

	KASSERT(localid != 0, ("localid == 0"));

	*frm++ = IEEE80211_ELEMID_MESHPEER;
	switch (subtype) {
	case IEEE80211_ACTION_MESHPEERING_OPEN:
		*frm++ = IEEE80211_MPM_BASE_SZ;		/* length */
		ADDSHORT(frm, IEEE80211_MPPID_MPM);	/* proto */
		ADDSHORT(frm, localid);			/* local ID */
		break;
	case IEEE80211_ACTION_MESHPEERING_CONFIRM:
		KASSERT(peerid != 0, ("sending peer confirm without peer id"));
		*frm++ = IEEE80211_MPM_BASE_SZ + 2;	/* length */
		ADDSHORT(frm, IEEE80211_MPPID_MPM);	/* proto */
		ADDSHORT(frm, localid);			/* local ID */
		ADDSHORT(frm, peerid);			/* peer ID */
		break;
	case IEEE80211_ACTION_MESHPEERING_CLOSE:
		if (peerid)
			*frm++ = IEEE80211_MPM_MAX_SZ;	/* length */
		else
			*frm++ = IEEE80211_MPM_BASE_SZ + 2; /* length */
		ADDSHORT(frm, IEEE80211_MPPID_MPM);	/* proto */
		ADDSHORT(frm, localid);	/* local ID */
		if (peerid)
			ADDSHORT(frm, peerid);	/* peer ID */
		ADDSHORT(frm, reason);
		break;
	}
	return frm;
}

/*
 * Compute an Airtime Link Metric for the link with this node.
 *
 * Based on Draft 3.0 spec (11B.10, p.149).
 */
/*
 * Max 802.11s overhead.
 */
#define IEEE80211_MESH_MAXOVERHEAD \
	(sizeof(struct ieee80211_qosframe_addr4) \
	 + sizeof(struct ieee80211_meshcntl_ae10) \
	+ sizeof(struct llc) \
	+ IEEE80211_ADDR_LEN \
	+ IEEE80211_WEP_IVLEN \
	+ IEEE80211_WEP_KIDLEN \
	+ IEEE80211_WEP_CRCLEN \
	+ IEEE80211_WEP_MICLEN \
	+ IEEE80211_CRC_LEN)
uint32_t
mesh_airtime_calc(struct ieee80211_node *ni)
{
#define M_BITS 8
#define S_FACTOR (2 * M_BITS)
	struct ieee80211com *ic = ni->ni_ic;
	struct ifnet *ifp = ni->ni_vap->iv_ifp;
	const static int nbits = 8192 << M_BITS;
	uint32_t overhead, rate, errrate;
	uint64_t res;

	/* Time to transmit a frame */
	rate = ni->ni_txrate;
	overhead = ieee80211_compute_duration(ic->ic_rt,
	    ifp->if_mtu + IEEE80211_MESH_MAXOVERHEAD, rate, 0) << M_BITS;
	/* Error rate in percentage */
	/* XXX assuming small failures are ok */
	errrate = (((ifp->if_get_counter(ifp, IFCOUNTER_OERRORS) +
	    ifp->if_get_counter(ifp, IFCOUNTER_IERRORS)) / 100) << M_BITS)
	    / 100;
	res = (overhead + (nbits / rate)) *
	    ((1 << S_FACTOR) / ((1 << M_BITS) - errrate));

	return (uint32_t)(res >> S_FACTOR);
#undef M_BITS
#undef S_FACTOR
}

/*
 * Add a Mesh Link Metric report IE to a frame.
 */
uint8_t *
ieee80211_add_meshlmetric(uint8_t *frm, uint8_t flags, uint32_t metric)
{
	*frm++ = IEEE80211_ELEMID_MESHLINK;
	*frm++ = 5;
	*frm++ = flags;
	ADDWORD(frm, metric);
	return frm;
}

/*
 * Add a Mesh Gate Announcement IE to a frame.
 */
uint8_t *
ieee80211_add_meshgate(uint8_t *frm, struct ieee80211_meshgann_ie *ie)
{
	*frm++ = IEEE80211_ELEMID_MESHGANN; /* ie */
	*frm++ = IEEE80211_MESHGANN_BASE_SZ; /* len */
	*frm++ = ie->gann_flags;
	*frm++ = ie->gann_hopcount;
	*frm++ = ie->gann_ttl;
	IEEE80211_ADDR_COPY(frm, ie->gann_addr);
	frm += 6;
	ADDWORD(frm, ie->gann_seq);
	ADDSHORT(frm, ie->gann_interval);
	return frm;
}
#undef ADDSHORT
#undef ADDWORD

/*
 * Initialize any mesh-specific node state.
 */
void
ieee80211_mesh_node_init(struct ieee80211vap *vap, struct ieee80211_node *ni)
{
	ni->ni_flags |= IEEE80211_NODE_QOS;
	callout_init(&ni->ni_mltimer, 1);
	callout_init(&ni->ni_mlhtimer, 1);
}

/*
 * Cleanup any mesh-specific node state.
 */
void
ieee80211_mesh_node_cleanup(struct ieee80211_node *ni)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211_mesh_state *ms = vap->iv_mesh;

	callout_drain(&ni->ni_mltimer);
	callout_drain(&ni->ni_mlhtimer);
	/* NB: short-circuit callbacks after mesh_vdetach */
	if (vap->iv_mesh != NULL)
		ms->ms_ppath->mpp_peerdown(ni);
}

void
ieee80211_parse_meshid(struct ieee80211_node *ni, const uint8_t *ie)
{
	ni->ni_meshidlen = ie[1];
	memcpy(ni->ni_meshid, ie + 2, ie[1]);
}

/*
 * Setup mesh-specific node state on neighbor discovery.
 */
void
ieee80211_mesh_init_neighbor(struct ieee80211_node *ni,
	const struct ieee80211_frame *wh,
	const struct ieee80211_scanparams *sp)
{
	ieee80211_parse_meshid(ni, sp->meshid);
}

void
ieee80211_mesh_update_beacon(struct ieee80211vap *vap,
	struct ieee80211_beacon_offsets *bo)
{
	KASSERT(vap->iv_opmode == IEEE80211_M_MBSS, ("not a MBSS vap"));

	if (isset(bo->bo_flags, IEEE80211_BEACON_MESHCONF)) {
		(void)ieee80211_add_meshconf(bo->bo_meshconf, vap);
		clrbit(bo->bo_flags, IEEE80211_BEACON_MESHCONF);
	}
}

static int
mesh_ioctl_get80211(struct ieee80211vap *vap, struct ieee80211req *ireq)
{
	struct ieee80211_mesh_state *ms = vap->iv_mesh;
	uint8_t tmpmeshid[IEEE80211_NWID_LEN];
	struct ieee80211_mesh_route *rt;
	struct ieee80211req_mesh_route *imr;
	size_t len, off;
	uint8_t *p;
	int error;

	if (vap->iv_opmode != IEEE80211_M_MBSS)
		return ENOSYS;

	error = 0;
	switch (ireq->i_type) {
	case IEEE80211_IOC_MESH_ID:
		ireq->i_len = ms->ms_idlen;
		memcpy(tmpmeshid, ms->ms_id, ireq->i_len);
		error = copyout(tmpmeshid, ireq->i_data, ireq->i_len);
		break;
	case IEEE80211_IOC_MESH_AP:
		ireq->i_val = (ms->ms_flags & IEEE80211_MESHFLAGS_AP) != 0;
		break;
	case IEEE80211_IOC_MESH_FWRD:
		ireq->i_val = (ms->ms_flags & IEEE80211_MESHFLAGS_FWD) != 0;
		break;
	case IEEE80211_IOC_MESH_GATE:
		ireq->i_val = (ms->ms_flags & IEEE80211_MESHFLAGS_GATE) != 0;
		break;
	case IEEE80211_IOC_MESH_TTL:
		ireq->i_val = ms->ms_ttl;
		break;
	case IEEE80211_IOC_MESH_RTCMD:
		switch (ireq->i_val) {
		case IEEE80211_MESH_RTCMD_LIST:
			len = 0;
			MESH_RT_LOCK(ms);
			TAILQ_FOREACH(rt, &ms->ms_routes, rt_next) {
				len += sizeof(*imr);
			}
			MESH_RT_UNLOCK(ms);
			if (len > ireq->i_len || ireq->i_len < sizeof(*imr)) {
				ireq->i_len = len;
				return ENOMEM;
			}
			ireq->i_len = len;
			/* XXX M_WAIT? */
			p = IEEE80211_MALLOC(len, M_TEMP,
			    IEEE80211_M_NOWAIT | IEEE80211_M_ZERO);
			if (p == NULL)
				return ENOMEM;
			off = 0;
			MESH_RT_LOCK(ms);
			TAILQ_FOREACH(rt, &ms->ms_routes, rt_next) {
				if (off >= len)
					break;
				imr = (struct ieee80211req_mesh_route *)
				    (p + off);
				IEEE80211_ADDR_COPY(imr->imr_dest,
				    rt->rt_dest);
				IEEE80211_ADDR_COPY(imr->imr_nexthop,
				    rt->rt_nexthop);
				imr->imr_metric = rt->rt_metric;
				imr->imr_nhops = rt->rt_nhops;
				imr->imr_lifetime =
				    ieee80211_mesh_rt_update(rt, 0);
				imr->imr_lastmseq = rt->rt_lastmseq;
				imr->imr_flags = rt->rt_flags; /* last */
				off += sizeof(*imr);
			}
			MESH_RT_UNLOCK(ms);
			error = copyout(p, (uint8_t *)ireq->i_data,
			    ireq->i_len);
			IEEE80211_FREE(p, M_TEMP);
			break;
		case IEEE80211_MESH_RTCMD_FLUSH:
		case IEEE80211_MESH_RTCMD_ADD:
		case IEEE80211_MESH_RTCMD_DELETE:
			return EINVAL;
		default:
			return ENOSYS;
		}
		break;
	case IEEE80211_IOC_MESH_PR_METRIC:
		len = strlen(ms->ms_pmetric->mpm_descr);
		if (ireq->i_len < len)
			return EINVAL;
		ireq->i_len = len;
		error = copyout(ms->ms_pmetric->mpm_descr,
		    (uint8_t *)ireq->i_data, len);
		break;
	case IEEE80211_IOC_MESH_PR_PATH:
		len = strlen(ms->ms_ppath->mpp_descr);
		if (ireq->i_len < len)
			return EINVAL;
		ireq->i_len = len;
		error = copyout(ms->ms_ppath->mpp_descr,
		    (uint8_t *)ireq->i_data, len);
		break;
	default:
		return ENOSYS;
	}

	return error;
}
IEEE80211_IOCTL_GET(mesh, mesh_ioctl_get80211);

static int
mesh_ioctl_set80211(struct ieee80211vap *vap, struct ieee80211req *ireq)
{
	struct ieee80211_mesh_state *ms = vap->iv_mesh;
	uint8_t tmpmeshid[IEEE80211_NWID_LEN];
	uint8_t tmpaddr[IEEE80211_ADDR_LEN];
	char tmpproto[IEEE80211_MESH_PROTO_DSZ];
	int error;

	if (vap->iv_opmode != IEEE80211_M_MBSS)
		return ENOSYS;

	error = 0;
	switch (ireq->i_type) {
	case IEEE80211_IOC_MESH_ID:
		if (ireq->i_val != 0 || ireq->i_len > IEEE80211_MESHID_LEN)
			return EINVAL;
		error = copyin(ireq->i_data, tmpmeshid, ireq->i_len);
		if (error != 0)
			break;
		memset(ms->ms_id, 0, IEEE80211_NWID_LEN);
		ms->ms_idlen = ireq->i_len;
		memcpy(ms->ms_id, tmpmeshid, ireq->i_len);
		error = ENETRESET;
		break;
	case IEEE80211_IOC_MESH_AP:
		if (ireq->i_val)
			ms->ms_flags |= IEEE80211_MESHFLAGS_AP;
		else
			ms->ms_flags &= ~IEEE80211_MESHFLAGS_AP;
		error = ENETRESET;
		break;
	case IEEE80211_IOC_MESH_FWRD:
		if (ireq->i_val)
			ms->ms_flags |= IEEE80211_MESHFLAGS_FWD;
		else
			ms->ms_flags &= ~IEEE80211_MESHFLAGS_FWD;
		mesh_gatemode_setup(vap);
		break;
	case IEEE80211_IOC_MESH_GATE:
		if (ireq->i_val)
			ms->ms_flags |= IEEE80211_MESHFLAGS_GATE;
		else
			ms->ms_flags &= ~IEEE80211_MESHFLAGS_GATE;
		break;
	case IEEE80211_IOC_MESH_TTL:
		ms->ms_ttl = (uint8_t) ireq->i_val;
		break;
	case IEEE80211_IOC_MESH_RTCMD:
		switch (ireq->i_val) {
		case IEEE80211_MESH_RTCMD_LIST:
			return EINVAL;
		case IEEE80211_MESH_RTCMD_FLUSH:
			ieee80211_mesh_rt_flush(vap);
			break;
		case IEEE80211_MESH_RTCMD_ADD:
			if (IEEE80211_ADDR_EQ(vap->iv_myaddr, ireq->i_data) ||
			    IEEE80211_ADDR_EQ(broadcastaddr, ireq->i_data))
				return EINVAL;
			error = copyin(ireq->i_data, &tmpaddr,
			    IEEE80211_ADDR_LEN);
			if (error == 0)
				ieee80211_mesh_discover(vap, tmpaddr, NULL);
			break;
		case IEEE80211_MESH_RTCMD_DELETE:
			ieee80211_mesh_rt_del(vap, ireq->i_data);
			break;
		default:
			return ENOSYS;
		}
		break;
	case IEEE80211_IOC_MESH_PR_METRIC:
		error = copyin(ireq->i_data, tmpproto, sizeof(tmpproto));
		if (error == 0) {
			error = mesh_select_proto_metric(vap, tmpproto);
			if (error == 0)
				error = ENETRESET;
		}
		break;
	case IEEE80211_IOC_MESH_PR_PATH:
		error = copyin(ireq->i_data, tmpproto, sizeof(tmpproto));
		if (error == 0) {
			error = mesh_select_proto_path(vap, tmpproto);
			if (error == 0)
				error = ENETRESET;
		}
		break;
	default:
		return ENOSYS;
	}
	return error;
}
IEEE80211_IOCTL_SET(mesh, mesh_ioctl_set80211);
