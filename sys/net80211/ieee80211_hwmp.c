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
 * IEEE 802.11s Hybrid Wireless Mesh Protocol, HWMP.
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

#include <net/if.h>
#include <net/if_media.h>
#include <net/if_llc.h>
#include <net/ethernet.h>

#include <net/bpf.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_action.h>
#include <net80211/ieee80211_input.h>
#include <net80211/ieee80211_mesh.h>

static void	hwmp_vattach(struct ieee80211vap *);
static void	hwmp_vdetach(struct ieee80211vap *);
static int	hwmp_newstate(struct ieee80211vap *,
		    enum ieee80211_state, int);
static int	hwmp_send_action(struct ieee80211vap *,
		    const uint8_t [IEEE80211_ADDR_LEN],
		    uint8_t *, size_t);
static uint8_t * hwmp_add_meshpreq(uint8_t *,
		    const struct ieee80211_meshpreq_ie *);
static uint8_t * hwmp_add_meshprep(uint8_t *,
		    const struct ieee80211_meshprep_ie *);
static uint8_t * hwmp_add_meshperr(uint8_t *,
		    const struct ieee80211_meshperr_ie *);
static uint8_t * hwmp_add_meshrann(uint8_t *,
		    const struct ieee80211_meshrann_ie *);
static void	hwmp_rootmode_setup(struct ieee80211vap *);
static void	hwmp_rootmode_cb(void *);
static void	hwmp_rootmode_rann_cb(void *);
static void	hwmp_recv_preq(struct ieee80211vap *, struct ieee80211_node *,
		    const struct ieee80211_frame *,
		    const struct ieee80211_meshpreq_ie *);
static int	hwmp_send_preq(struct ieee80211vap *,
		    const uint8_t [IEEE80211_ADDR_LEN],
		    struct ieee80211_meshpreq_ie *,
		    struct timeval *, struct timeval *);
static void	hwmp_recv_prep(struct ieee80211vap *, struct ieee80211_node *,
		    const struct ieee80211_frame *,
		    const struct ieee80211_meshprep_ie *);
static int	hwmp_send_prep(struct ieee80211vap *,
		    const uint8_t [IEEE80211_ADDR_LEN],
		    struct ieee80211_meshprep_ie *);
static void	hwmp_recv_perr(struct ieee80211vap *, struct ieee80211_node *,
		    const struct ieee80211_frame *,
		    const struct ieee80211_meshperr_ie *);
static int	hwmp_send_perr(struct ieee80211vap *,
		    const uint8_t [IEEE80211_ADDR_LEN],
		    struct ieee80211_meshperr_ie *);
static void	hwmp_senderror(struct ieee80211vap *,
		    const uint8_t [IEEE80211_ADDR_LEN],
		    struct ieee80211_mesh_route *, int);
static void	hwmp_recv_rann(struct ieee80211vap *, struct ieee80211_node *,
		   const struct ieee80211_frame *,
		   const struct ieee80211_meshrann_ie *);
static int	hwmp_send_rann(struct ieee80211vap *,
		    const uint8_t [IEEE80211_ADDR_LEN],
		    struct ieee80211_meshrann_ie *);
static struct ieee80211_node *
		hwmp_discover(struct ieee80211vap *,
		    const uint8_t [IEEE80211_ADDR_LEN], struct mbuf *);
static void	hwmp_peerdown(struct ieee80211_node *);

static struct timeval ieee80211_hwmp_preqminint = { 0, 100000 };
static struct timeval ieee80211_hwmp_perrminint = { 0, 100000 };


/* NB: the Target Address set in a Proactive PREQ is the broadcast address. */
static const uint8_t	broadcastaddr[IEEE80211_ADDR_LEN] =
	{ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

typedef uint32_t ieee80211_hwmp_seq;
#define	HWMP_SEQ_LT(a, b)	((int32_t)((a)-(b)) < 0)
#define	HWMP_SEQ_LEQ(a, b)	((int32_t)((a)-(b)) <= 0)
#define	HWMP_SEQ_EQ(a, b)	((int32_t)((a)-(b)) == 0)
#define	HWMP_SEQ_GT(a, b)	((int32_t)((a)-(b)) > 0)

#define HWMP_SEQ_MAX(a, b)	(a > b ? a : b)

/*
 * Private extension of ieee80211_mesh_route.
 */
struct ieee80211_hwmp_route {
	ieee80211_hwmp_seq	hr_seq;		/* last HWMP seq seen from dst*/
	ieee80211_hwmp_seq	hr_preqid;	/* last PREQ ID seen from dst */
	ieee80211_hwmp_seq	hr_origseq;	/* seq. no. on our latest PREQ*/
	struct timeval		hr_lastpreq;	/* last time we sent a PREQ */
	struct timeval		hr_lastrootconf; /* last sent PREQ root conf */
	int			hr_preqretries;	/* number of discoveries */
	int			hr_lastdiscovery; /* last discovery in ticks */
};
struct ieee80211_hwmp_state {
	ieee80211_hwmp_seq	hs_seq;		/* next seq to be used */
	ieee80211_hwmp_seq	hs_preqid;	/* next PREQ ID to be used */
	int			hs_rootmode;	/* proactive HWMP */
	struct timeval		hs_lastperr;	/* last time we sent a PERR */
	struct callout		hs_roottimer;
	uint8_t			hs_maxhops;	/* max hop count */
};

static SYSCTL_NODE(_net_wlan, OID_AUTO, hwmp, CTLFLAG_RD, 0,
    "IEEE 802.11s HWMP parameters");
static int	ieee80211_hwmp_targetonly = 0;
SYSCTL_INT(_net_wlan_hwmp, OID_AUTO, targetonly, CTLFLAG_RW,
    &ieee80211_hwmp_targetonly, 0, "Set TO bit on generated PREQs");
static int	ieee80211_hwmp_pathtimeout = -1;
SYSCTL_PROC(_net_wlan_hwmp, OID_AUTO, pathlifetime, CTLTYPE_INT | CTLFLAG_RW,
    &ieee80211_hwmp_pathtimeout, 0, ieee80211_sysctl_msecs_ticks, "I",
    "path entry lifetime (ms)");
static int	ieee80211_hwmp_maxpreq_retries = -1;
SYSCTL_PROC(_net_wlan_hwmp, OID_AUTO, maxpreq_retries, CTLTYPE_INT | CTLFLAG_RW,
    &ieee80211_hwmp_maxpreq_retries, 0, ieee80211_sysctl_msecs_ticks, "I",
    "maximum number of preq retries");
static int	ieee80211_hwmp_net_diameter_traversaltime = -1;
SYSCTL_PROC(_net_wlan_hwmp, OID_AUTO, net_diameter_traversal_time,
    CTLTYPE_INT | CTLFLAG_RW, &ieee80211_hwmp_net_diameter_traversaltime, 0,
    ieee80211_sysctl_msecs_ticks, "I",
    "estimate travelse time across the MBSS (ms)");
static int	ieee80211_hwmp_roottimeout = -1;
SYSCTL_PROC(_net_wlan_hwmp, OID_AUTO, roottimeout, CTLTYPE_INT | CTLFLAG_RW,
    &ieee80211_hwmp_roottimeout, 0, ieee80211_sysctl_msecs_ticks, "I",
    "root PREQ timeout (ms)");
static int	ieee80211_hwmp_rootint = -1;
SYSCTL_PROC(_net_wlan_hwmp, OID_AUTO, rootint, CTLTYPE_INT | CTLFLAG_RW,
    &ieee80211_hwmp_rootint, 0, ieee80211_sysctl_msecs_ticks, "I",
    "root interval (ms)");
static int	ieee80211_hwmp_rannint = -1;
SYSCTL_PROC(_net_wlan_hwmp, OID_AUTO, rannint, CTLTYPE_INT | CTLFLAG_RW,
    &ieee80211_hwmp_rannint, 0, ieee80211_sysctl_msecs_ticks, "I",
    "root announcement interval (ms)");
static struct timeval ieee80211_hwmp_rootconfint = { 0, 0 };
static int	ieee80211_hwmp_rootconfint_internal = -1;
SYSCTL_PROC(_net_wlan_hwmp, OID_AUTO, rootconfint, CTLTYPE_INT | CTLFLAG_RD,
    &ieee80211_hwmp_rootconfint_internal, 0, ieee80211_sysctl_msecs_ticks, "I",
    "root confirmation interval (ms) (read-only)");

#define	IEEE80211_HWMP_DEFAULT_MAXHOPS	31

static	ieee80211_recv_action_func hwmp_recv_action_meshpath;

static struct ieee80211_mesh_proto_path mesh_proto_hwmp = {
	.mpp_descr	= "HWMP",
	.mpp_ie		= IEEE80211_MESHCONF_PATH_HWMP,
	.mpp_discover	= hwmp_discover,
	.mpp_peerdown	= hwmp_peerdown,
	.mpp_senderror	= hwmp_senderror,
	.mpp_vattach	= hwmp_vattach,
	.mpp_vdetach	= hwmp_vdetach,
	.mpp_newstate	= hwmp_newstate,
	.mpp_privlen	= sizeof(struct ieee80211_hwmp_route),
};
SYSCTL_PROC(_net_wlan_hwmp, OID_AUTO, inact, CTLTYPE_INT | CTLFLAG_RW,
	&mesh_proto_hwmp.mpp_inact, 0, ieee80211_sysctl_msecs_ticks, "I",
	"mesh route inactivity timeout (ms)");


static void
ieee80211_hwmp_init(void)
{
	/* Default values as per amendment */
	ieee80211_hwmp_pathtimeout = msecs_to_ticks(5*1000);
	ieee80211_hwmp_roottimeout = msecs_to_ticks(5*1000);
	ieee80211_hwmp_rootint = msecs_to_ticks(2*1000);
	ieee80211_hwmp_rannint = msecs_to_ticks(1*1000);
	ieee80211_hwmp_rootconfint_internal = msecs_to_ticks(2*1000);
	ieee80211_hwmp_maxpreq_retries = 3;
	/*
	 * (TU): A measurement of time equal to 1024 μs,
	 * 500 TU is 512 ms.
	 */
	ieee80211_hwmp_net_diameter_traversaltime = msecs_to_ticks(512);

	/*
	 * NB: I dont know how to make SYSCTL_PROC that calls ms to ticks
	 * and return a struct timeval...
	 */
	ieee80211_hwmp_rootconfint.tv_usec =
	    ieee80211_hwmp_rootconfint_internal * 1000;

	/*
	 * Register action frame handler.
	 */
	ieee80211_recv_action_register(IEEE80211_ACTION_CAT_MESH,
	    IEEE80211_ACTION_MESH_HWMP, hwmp_recv_action_meshpath);

	/* NB: default is 5 secs per spec */
	mesh_proto_hwmp.mpp_inact = msecs_to_ticks(5*1000);

	/*
	 * Register HWMP.
	 */
	ieee80211_mesh_register_proto_path(&mesh_proto_hwmp);
}
SYSINIT(wlan_hwmp, SI_SUB_DRIVERS, SI_ORDER_SECOND, ieee80211_hwmp_init, NULL);

static void
hwmp_vattach(struct ieee80211vap *vap)
{
	struct ieee80211_hwmp_state *hs;

	KASSERT(vap->iv_opmode == IEEE80211_M_MBSS,
	    ("not a mesh vap, opmode %d", vap->iv_opmode));

	hs = IEEE80211_MALLOC(sizeof(struct ieee80211_hwmp_state), M_80211_VAP,
	    IEEE80211_M_NOWAIT | IEEE80211_M_ZERO);
	if (hs == NULL) {
		printf("%s: couldn't alloc HWMP state\n", __func__);
		return;
	}
	hs->hs_maxhops = IEEE80211_HWMP_DEFAULT_MAXHOPS;
	callout_init(&hs->hs_roottimer, 1);
	vap->iv_hwmp = hs;
}

static void
hwmp_vdetach(struct ieee80211vap *vap)
{
	struct ieee80211_hwmp_state *hs = vap->iv_hwmp;

	callout_drain(&hs->hs_roottimer);
	IEEE80211_FREE(vap->iv_hwmp, M_80211_VAP);
	vap->iv_hwmp = NULL;
} 

static int
hwmp_newstate(struct ieee80211vap *vap, enum ieee80211_state ostate, int arg)
{
	enum ieee80211_state nstate = vap->iv_state;
	struct ieee80211_hwmp_state *hs = vap->iv_hwmp;

	IEEE80211_DPRINTF(vap, IEEE80211_MSG_STATE, "%s: %s -> %s (%d)\n",
	    __func__, ieee80211_state_name[ostate],
	    ieee80211_state_name[nstate], arg);

	if (nstate != IEEE80211_S_RUN && ostate == IEEE80211_S_RUN)
		callout_drain(&hs->hs_roottimer);
	if (nstate == IEEE80211_S_RUN)
		hwmp_rootmode_setup(vap);
	return 0;
}

/*
 * Verify the length of an HWMP PREQ and return the number
 * of destinations >= 1, if verification fails -1 is returned.
 */
static int
verify_mesh_preq_len(struct ieee80211vap *vap,
    const struct ieee80211_frame *wh, const uint8_t *iefrm)
{
	int alloc_sz = -1;
	int ndest = -1;
	if (iefrm[2] & IEEE80211_MESHPREQ_FLAGS_AE) {
		/* Originator External Address  present */
		alloc_sz =  IEEE80211_MESHPREQ_BASE_SZ_AE;
		ndest = iefrm[IEEE80211_MESHPREQ_TCNT_OFFSET_AE];
	} else {
		/* w/o Originator External Address */
		alloc_sz =  IEEE80211_MESHPREQ_BASE_SZ;
		ndest = iefrm[IEEE80211_MESHPREQ_TCNT_OFFSET];
	}
	alloc_sz += ndest * IEEE80211_MESHPREQ_TRGT_SZ;

	if(iefrm[1] != (alloc_sz)) {
		IEEE80211_DISCARD(vap,
		    IEEE80211_MSG_ACTION | IEEE80211_MSG_HWMP,
		    wh, NULL, "PREQ (AE=%s) with wrong len",
		    iefrm[2] & IEEE80211_MESHPREQ_FLAGS_AE ? "1" : "0");
		return (-1);
	}
	return ndest;
}

/*
 * Verify the length of an HWMP PREP and returns 1 on success,
 * otherwise -1.
 */
static int
verify_mesh_prep_len(struct ieee80211vap *vap,
    const struct ieee80211_frame *wh, const uint8_t *iefrm)
{
	int alloc_sz = -1;
	if (iefrm[2] & IEEE80211_MESHPREP_FLAGS_AE) {
		if (iefrm[1] == IEEE80211_MESHPREP_BASE_SZ_AE)
			alloc_sz = IEEE80211_MESHPREP_BASE_SZ_AE;
	} else if (iefrm[1] == IEEE80211_MESHPREP_BASE_SZ)
		alloc_sz = IEEE80211_MESHPREP_BASE_SZ;
	if(alloc_sz < 0) {
		IEEE80211_DISCARD(vap,
		    IEEE80211_MSG_ACTION | IEEE80211_MSG_HWMP,
		    wh, NULL, "PREP (AE=%s) with wrong len",
		    iefrm[2] & IEEE80211_MESHPREP_FLAGS_AE ? "1" : "0");
		return (-1);
	}
	return (1);
}

/*
 * Verify the length of an HWMP PERR and return the number
 * of destinations >= 1, if verification fails -1 is returned.
 */
static int
verify_mesh_perr_len(struct ieee80211vap *vap,
    const struct ieee80211_frame *wh, const uint8_t *iefrm)
{
	int alloc_sz = -1;
	const uint8_t *iefrm_t = iefrm;
	uint8_t ndest = iefrm_t[IEEE80211_MESHPERR_NDEST_OFFSET];
	int i;

	if(ndest > IEEE80211_MESHPERR_MAXDEST) {
		IEEE80211_DISCARD(vap,
		    IEEE80211_MSG_ACTION | IEEE80211_MSG_HWMP,
		    wh, NULL, "PERR with wrong number of destionat (>19), %u",
		    ndest);
		return (-1);
	}

	iefrm_t += IEEE80211_MESHPERR_NDEST_OFFSET + 1; /* flag is next field */
	/* We need to check each destionation flag to know size */
	for(i = 0; i<ndest; i++) {
		if ((*iefrm_t) & IEEE80211_MESHPERR_FLAGS_AE)
			iefrm_t += IEEE80211_MESHPERR_DEST_SZ_AE;
		else
			iefrm_t += IEEE80211_MESHPERR_DEST_SZ;
	}

	alloc_sz = (iefrm_t - iefrm) - 2; /* action + code */
	if(alloc_sz !=  iefrm[1]) {
		IEEE80211_DISCARD(vap,
		    IEEE80211_MSG_ACTION | IEEE80211_MSG_HWMP,
		    wh, NULL, "%s", "PERR with wrong len");
		return (-1);
	}
	return ndest;
}

static int
hwmp_recv_action_meshpath(struct ieee80211_node *ni,
	const struct ieee80211_frame *wh,
	const uint8_t *frm, const uint8_t *efrm)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211_meshpreq_ie *preq;
	struct ieee80211_meshprep_ie *prep;
	struct ieee80211_meshperr_ie *perr;
	struct ieee80211_meshrann_ie rann;
	const uint8_t *iefrm = frm + 2; /* action + code */
	const uint8_t *iefrm_t = iefrm; /* temporary pointer */
	int ndest = -1;
	int found = 0;

	while (efrm - iefrm > 1) {
		IEEE80211_VERIFY_LENGTH(efrm - iefrm, iefrm[1] + 2, return 0);
		switch (*iefrm) {
		case IEEE80211_ELEMID_MESHPREQ:
		{
			int i = 0;

			iefrm_t = iefrm;
			ndest = verify_mesh_preq_len(vap, wh, iefrm_t);
			if (ndest < 0) {
				vap->iv_stats.is_rx_mgtdiscard++;
				break;
			}
			preq = IEEE80211_MALLOC(sizeof(*preq) +
			    (ndest - 1) * sizeof(*preq->preq_targets),
			    M_80211_MESH_PREQ,
			    IEEE80211_M_NOWAIT | IEEE80211_M_ZERO);
			KASSERT(preq != NULL, ("preq == NULL"));

			preq->preq_ie = *iefrm_t++;
			preq->preq_len = *iefrm_t++;
			preq->preq_flags = *iefrm_t++;
			preq->preq_hopcount = *iefrm_t++;
			preq->preq_ttl = *iefrm_t++;
			preq->preq_id = le32dec(iefrm_t); iefrm_t += 4;
			IEEE80211_ADDR_COPY(preq->preq_origaddr, iefrm_t);
			iefrm_t += 6;
			preq->preq_origseq = le32dec(iefrm_t); iefrm_t += 4;
			/* NB: may have Originator Proxied Address */
			if (preq->preq_flags & IEEE80211_MESHPREQ_FLAGS_AE)  {
				IEEE80211_ADDR_COPY(
				    preq->preq_orig_ext_addr, iefrm_t);
				iefrm_t += 6;
			}
			preq->preq_lifetime = le32dec(iefrm_t); iefrm_t += 4;
			preq->preq_metric = le32dec(iefrm_t); iefrm_t += 4;
			preq->preq_tcount = *iefrm_t++;
			
			for (i = 0; i < preq->preq_tcount; i++) {
				preq->preq_targets[i].target_flags = *iefrm_t++;
				IEEE80211_ADDR_COPY(
				    preq->preq_targets[i].target_addr, iefrm_t);
				iefrm_t += 6;
				preq->preq_targets[i].target_seq =
				    le32dec(iefrm_t);
				iefrm_t += 4;
			}

			hwmp_recv_preq(vap, ni, wh, preq);
			IEEE80211_FREE(preq, M_80211_MESH_PREQ);
			found++;
			break;
		}
		case IEEE80211_ELEMID_MESHPREP:
		{
			iefrm_t = iefrm;
			ndest = verify_mesh_prep_len(vap, wh, iefrm_t);
			if (ndest < 0) {
				vap->iv_stats.is_rx_mgtdiscard++;
				break;
			}
			prep = IEEE80211_MALLOC(sizeof(*prep),
			    M_80211_MESH_PREP,
			    IEEE80211_M_NOWAIT | IEEE80211_M_ZERO);
			KASSERT(prep != NULL, ("prep == NULL"));

			prep->prep_ie = *iefrm_t++;
			prep->prep_len = *iefrm_t++;
			prep->prep_flags = *iefrm_t++;
			prep->prep_hopcount = *iefrm_t++;
			prep->prep_ttl = *iefrm_t++;
			IEEE80211_ADDR_COPY(prep->prep_targetaddr, iefrm_t);
			iefrm_t += 6;
			prep->prep_targetseq = le32dec(iefrm_t); iefrm_t += 4;
			/* NB: May have Target Proxied Address */
			if (prep->prep_flags & IEEE80211_MESHPREP_FLAGS_AE)  {
				IEEE80211_ADDR_COPY(
				    prep->prep_target_ext_addr, iefrm_t);
				iefrm_t += 6;
			}
			prep->prep_lifetime = le32dec(iefrm_t); iefrm_t += 4;
			prep->prep_metric = le32dec(iefrm_t); iefrm_t += 4;
			IEEE80211_ADDR_COPY(prep->prep_origaddr, iefrm_t);
			iefrm_t += 6;
			prep->prep_origseq = le32dec(iefrm_t); iefrm_t += 4;

			hwmp_recv_prep(vap, ni, wh, prep);
			IEEE80211_FREE(prep, M_80211_MESH_PREP);
			found++;
			break;
		}
		case IEEE80211_ELEMID_MESHPERR:
		{
			int i = 0;

			iefrm_t = iefrm;
			ndest = verify_mesh_perr_len(vap, wh, iefrm_t);
			if (ndest < 0) {
				vap->iv_stats.is_rx_mgtdiscard++;
				break;
			}
			perr = IEEE80211_MALLOC(sizeof(*perr) +
			    (ndest - 1) * sizeof(*perr->perr_dests),
			    M_80211_MESH_PERR,
			    IEEE80211_M_NOWAIT | IEEE80211_M_ZERO);
			KASSERT(perr != NULL, ("perr == NULL"));

			perr->perr_ie = *iefrm_t++;
			perr->perr_len = *iefrm_t++;
			perr->perr_ttl = *iefrm_t++;
			perr->perr_ndests = *iefrm_t++;

			for (i = 0; i<perr->perr_ndests; i++) {
				perr->perr_dests[i].dest_flags = *iefrm_t++;
				IEEE80211_ADDR_COPY(
				    perr->perr_dests[i].dest_addr, iefrm_t);
				iefrm_t += 6;
				perr->perr_dests[i].dest_seq = le32dec(iefrm_t);
				iefrm_t += 4;
				/* NB: May have Target Proxied Address */
				if (perr->perr_dests[i].dest_flags &
				    IEEE80211_MESHPERR_FLAGS_AE) {
					IEEE80211_ADDR_COPY(
					    perr->perr_dests[i].dest_ext_addr,
					    iefrm_t);
					iefrm_t += 6;
				}
				perr->perr_dests[i].dest_rcode =
				    le16dec(iefrm_t);
				iefrm_t += 2;
			}

			hwmp_recv_perr(vap, ni, wh, perr);
			IEEE80211_FREE(perr, M_80211_MESH_PERR);
			found++;
			break;
		}
		case IEEE80211_ELEMID_MESHRANN:
		{
			const struct ieee80211_meshrann_ie *mrann =
			    (const struct ieee80211_meshrann_ie *) iefrm;
			if (mrann->rann_len !=
			    sizeof(struct ieee80211_meshrann_ie) - 2) {
				IEEE80211_DISCARD(vap,
				    IEEE80211_MSG_ACTION | IEEE80211_MSG_HWMP,
				    wh, NULL, "%s", "RAN with wrong len");
				    vap->iv_stats.is_rx_mgtdiscard++;
				return 1;
			}
			memcpy(&rann, mrann, sizeof(rann));
			rann.rann_seq = le32dec(&mrann->rann_seq);
			rann.rann_interval = le32dec(&mrann->rann_interval);
			rann.rann_metric = le32dec(&mrann->rann_metric);
			hwmp_recv_rann(vap, ni, wh, &rann);
			found++;
			break;
		}
		}
		iefrm += iefrm[1] + 2;
	}
	if (!found) {
		IEEE80211_DISCARD(vap,
		    IEEE80211_MSG_ACTION | IEEE80211_MSG_HWMP,
		    wh, NULL, "%s", "PATH SEL action without IE");
		vap->iv_stats.is_rx_mgtdiscard++;
	}
	return 0;
}

static int
hwmp_send_action(struct ieee80211vap *vap,
    const uint8_t da[IEEE80211_ADDR_LEN],
    uint8_t *ie, size_t len)
{
	struct ieee80211_node *ni;
	struct ieee80211com *ic;
	struct ieee80211_bpf_params params;
	struct mbuf *m;
	uint8_t *frm;
	int ret;

	if (IEEE80211_IS_MULTICAST(da)) {
		ni = ieee80211_ref_node(vap->iv_bss);
#ifdef IEEE80211_DEBUG_REFCNT
		IEEE80211_DPRINTF(vap, IEEE80211_MSG_NODE,
		"ieee80211_ref_node (%s:%u) %p<%s> refcnt %d\n",
		__func__, __LINE__,
		ni, ether_sprintf(ni->ni_macaddr),
		ieee80211_node_refcnt(ni)+1);
#endif
		ieee80211_ref_node(ni);
	}
	else
		ni = ieee80211_mesh_find_txnode(vap, da);

	if (vap->iv_state == IEEE80211_S_CAC) {
		IEEE80211_NOTE(vap, IEEE80211_MSG_OUTPUT, ni,
		    "block %s frame in CAC state", "HWMP action");
		vap->iv_stats.is_tx_badstate++;
		return EIO;	/* XXX */
	}

	KASSERT(ni != NULL, ("null node"));
	ic = ni->ni_ic;

	m = ieee80211_getmgtframe(&frm,
	    ic->ic_headroom + sizeof(struct ieee80211_frame),
	    sizeof(struct ieee80211_action) + len
	);
	if (m == NULL) {
		ieee80211_free_node(ni);
		vap->iv_stats.is_tx_nobuf++;
		return ENOMEM;
	}
	*frm++ = IEEE80211_ACTION_CAT_MESH;
	*frm++ = IEEE80211_ACTION_MESH_HWMP;
	switch (*ie) {
	case IEEE80211_ELEMID_MESHPREQ:
		frm = hwmp_add_meshpreq(frm,
		    (struct ieee80211_meshpreq_ie *)ie);
		break;
	case IEEE80211_ELEMID_MESHPREP:
		frm = hwmp_add_meshprep(frm,
		    (struct ieee80211_meshprep_ie *)ie);
		break;
	case IEEE80211_ELEMID_MESHPERR:
		frm = hwmp_add_meshperr(frm,
		    (struct ieee80211_meshperr_ie *)ie);
		break;
	case IEEE80211_ELEMID_MESHRANN:
		frm = hwmp_add_meshrann(frm,
		    (struct ieee80211_meshrann_ie *)ie);
		break;
	}

	m->m_pkthdr.len = m->m_len = frm - mtod(m, uint8_t *);
	M_PREPEND(m, sizeof(struct ieee80211_frame), M_NOWAIT);
	if (m == NULL) {
		ieee80211_free_node(ni);
		vap->iv_stats.is_tx_nobuf++;
		return ENOMEM;
	}

	IEEE80211_TX_LOCK(ic);

	ieee80211_send_setup(ni, m,
	    IEEE80211_FC0_TYPE_MGT | IEEE80211_FC0_SUBTYPE_ACTION,
	    IEEE80211_NONQOS_TID, vap->iv_myaddr, da, vap->iv_myaddr);

	m->m_flags |= M_ENCAP;		/* mark encapsulated */
	IEEE80211_NODE_STAT(ni, tx_mgmt);

	memset(&params, 0, sizeof(params));
	params.ibp_pri = WME_AC_VO;
	params.ibp_rate0 = ni->ni_txparms->mgmtrate;
	if (IEEE80211_IS_MULTICAST(da))
		params.ibp_try0 = 1;
	else
		params.ibp_try0 = ni->ni_txparms->maxretry;
	params.ibp_power = ni->ni_txpower;
	ret = ieee80211_raw_output(vap, ni, m, &params);
	IEEE80211_TX_UNLOCK(ic);
	return (ret);
}

#define ADDSHORT(frm, v) do {		\
	le16enc(frm, v);		\
	frm += 2;			\
} while (0)
#define ADDWORD(frm, v) do {		\
	le32enc(frm, v);		\
	frm += 4;			\
} while (0)
/*
 * Add a Mesh Path Request IE to a frame.
 */
#define	PREQ_TFLAGS(n)	preq->preq_targets[n].target_flags
#define	PREQ_TADDR(n)	preq->preq_targets[n].target_addr
#define	PREQ_TSEQ(n)	preq->preq_targets[n].target_seq
static uint8_t *
hwmp_add_meshpreq(uint8_t *frm, const struct ieee80211_meshpreq_ie *preq)
{
	int i;

	*frm++ = IEEE80211_ELEMID_MESHPREQ;
	*frm++ = preq->preq_len;	/* len already calculated */
	*frm++ = preq->preq_flags;
	*frm++ = preq->preq_hopcount;
	*frm++ = preq->preq_ttl;
	ADDWORD(frm, preq->preq_id);
	IEEE80211_ADDR_COPY(frm, preq->preq_origaddr); frm += 6;
	ADDWORD(frm, preq->preq_origseq);
	if (preq->preq_flags & IEEE80211_MESHPREQ_FLAGS_AE) {
		IEEE80211_ADDR_COPY(frm, preq->preq_orig_ext_addr);
		frm += 6;
	}
	ADDWORD(frm, preq->preq_lifetime);
	ADDWORD(frm, preq->preq_metric);
	*frm++ = preq->preq_tcount;
	for (i = 0; i < preq->preq_tcount; i++) {
		*frm++ = PREQ_TFLAGS(i);
		IEEE80211_ADDR_COPY(frm, PREQ_TADDR(i));
		frm += 6;
		ADDWORD(frm, PREQ_TSEQ(i));
	}
	return frm;
}
#undef	PREQ_TFLAGS
#undef	PREQ_TADDR
#undef	PREQ_TSEQ

/*
 * Add a Mesh Path Reply IE to a frame.
 */
static uint8_t *
hwmp_add_meshprep(uint8_t *frm, const struct ieee80211_meshprep_ie *prep)
{
	*frm++ = IEEE80211_ELEMID_MESHPREP;
	*frm++ = prep->prep_len;	/* len already calculated */
	*frm++ = prep->prep_flags;
	*frm++ = prep->prep_hopcount;
	*frm++ = prep->prep_ttl;
	IEEE80211_ADDR_COPY(frm, prep->prep_targetaddr); frm += 6;
	ADDWORD(frm, prep->prep_targetseq);
	if (prep->prep_flags & IEEE80211_MESHPREP_FLAGS_AE) {
		IEEE80211_ADDR_COPY(frm, prep->prep_target_ext_addr);
		frm += 6;
	}
	ADDWORD(frm, prep->prep_lifetime);
	ADDWORD(frm, prep->prep_metric);
	IEEE80211_ADDR_COPY(frm, prep->prep_origaddr); frm += 6;
	ADDWORD(frm, prep->prep_origseq);
	return frm;
}

/*
 * Add a Mesh Path Error IE to a frame.
 */
#define	PERR_DFLAGS(n)	perr->perr_dests[n].dest_flags
#define	PERR_DADDR(n)	perr->perr_dests[n].dest_addr
#define	PERR_DSEQ(n)	perr->perr_dests[n].dest_seq
#define	PERR_EXTADDR(n)	perr->perr_dests[n].dest_ext_addr
#define	PERR_DRCODE(n)	perr->perr_dests[n].dest_rcode
static uint8_t *
hwmp_add_meshperr(uint8_t *frm, const struct ieee80211_meshperr_ie *perr)
{
	int i;

	*frm++ = IEEE80211_ELEMID_MESHPERR;
	*frm++ = perr->perr_len;	/* len already calculated */
	*frm++ = perr->perr_ttl;
	*frm++ = perr->perr_ndests;
	for (i = 0; i < perr->perr_ndests; i++) {
		*frm++ = PERR_DFLAGS(i);
		IEEE80211_ADDR_COPY(frm, PERR_DADDR(i));
		frm += 6;
		ADDWORD(frm, PERR_DSEQ(i));
		if (PERR_DFLAGS(i) & IEEE80211_MESHPERR_FLAGS_AE) {
			IEEE80211_ADDR_COPY(frm, PERR_EXTADDR(i));
			frm += 6;
		}
		ADDSHORT(frm, PERR_DRCODE(i));
	}
	return frm;
}
#undef	PERR_DFLAGS
#undef	PERR_DADDR
#undef	PERR_DSEQ
#undef	PERR_EXTADDR
#undef	PERR_DRCODE

/*
 * Add a Root Annoucement IE to a frame.
 */
static uint8_t *
hwmp_add_meshrann(uint8_t *frm, const struct ieee80211_meshrann_ie *rann)
{
	*frm++ = IEEE80211_ELEMID_MESHRANN;
	*frm++ = rann->rann_len;
	*frm++ = rann->rann_flags;
	*frm++ = rann->rann_hopcount;
	*frm++ = rann->rann_ttl;
	IEEE80211_ADDR_COPY(frm, rann->rann_addr); frm += 6;
	ADDWORD(frm, rann->rann_seq);
	ADDWORD(frm, rann->rann_interval);
	ADDWORD(frm, rann->rann_metric);
	return frm;
}

static void
hwmp_rootmode_setup(struct ieee80211vap *vap)
{
	struct ieee80211_hwmp_state *hs = vap->iv_hwmp;
	struct ieee80211_mesh_state *ms = vap->iv_mesh;

	switch (hs->hs_rootmode) {
	case IEEE80211_HWMP_ROOTMODE_DISABLED:
		callout_drain(&hs->hs_roottimer);
		ms->ms_flags &= ~IEEE80211_MESHFLAGS_ROOT;
		break;
	case IEEE80211_HWMP_ROOTMODE_NORMAL:
	case IEEE80211_HWMP_ROOTMODE_PROACTIVE:
		callout_reset(&hs->hs_roottimer, ieee80211_hwmp_rootint,
		    hwmp_rootmode_cb, vap);
		ms->ms_flags |= IEEE80211_MESHFLAGS_ROOT;
		break;
	case IEEE80211_HWMP_ROOTMODE_RANN:
		callout_reset(&hs->hs_roottimer, ieee80211_hwmp_rannint,
		    hwmp_rootmode_rann_cb, vap);
		ms->ms_flags |= IEEE80211_MESHFLAGS_ROOT;
		break;
	}
}

/*
 * Send a broadcast Path Request to find all nodes on the mesh. We are
 * called when the vap is configured as a HWMP root node.
 */
#define	PREQ_TFLAGS(n)	preq.preq_targets[n].target_flags
#define	PREQ_TADDR(n)	preq.preq_targets[n].target_addr
#define	PREQ_TSEQ(n)	preq.preq_targets[n].target_seq
static void
hwmp_rootmode_cb(void *arg)
{
	struct ieee80211vap *vap = (struct ieee80211vap *)arg;
	struct ieee80211_hwmp_state *hs = vap->iv_hwmp;
	struct ieee80211_mesh_state *ms = vap->iv_mesh;
	struct ieee80211_meshpreq_ie preq;

	IEEE80211_NOTE(vap, IEEE80211_MSG_HWMP, vap->iv_bss,
	    "%s", "send broadcast PREQ");

	preq.preq_flags = 0;
	if (ms->ms_flags & IEEE80211_MESHFLAGS_GATE)
		preq.preq_flags |= IEEE80211_MESHPREQ_FLAGS_GATE;
	if (hs->hs_rootmode == IEEE80211_HWMP_ROOTMODE_PROACTIVE)
		preq.preq_flags |= IEEE80211_MESHPREQ_FLAGS_PP;
	preq.preq_hopcount = 0;
	preq.preq_ttl = ms->ms_ttl;
	preq.preq_id = ++hs->hs_preqid;
	IEEE80211_ADDR_COPY(preq.preq_origaddr, vap->iv_myaddr);
	preq.preq_origseq = ++hs->hs_seq;
	preq.preq_lifetime = ticks_to_msecs(ieee80211_hwmp_roottimeout);
	preq.preq_metric = IEEE80211_MESHLMETRIC_INITIALVAL;
	preq.preq_tcount = 1;
	IEEE80211_ADDR_COPY(PREQ_TADDR(0), broadcastaddr);
	PREQ_TFLAGS(0) = IEEE80211_MESHPREQ_TFLAGS_TO |
	    IEEE80211_MESHPREQ_TFLAGS_USN;
	PREQ_TSEQ(0) = 0;
	vap->iv_stats.is_hwmp_rootreqs++;
	/* NB: we enforce rate check ourself */
	hwmp_send_preq(vap, broadcastaddr, &preq, NULL, NULL);
	hwmp_rootmode_setup(vap);
}
#undef	PREQ_TFLAGS
#undef	PREQ_TADDR
#undef	PREQ_TSEQ

/*
 * Send a Root Annoucement (RANN) to find all the nodes on the mesh. We are
 * called when the vap is configured as a HWMP RANN root node.
 */
static void
hwmp_rootmode_rann_cb(void *arg)
{
	struct ieee80211vap *vap = (struct ieee80211vap *)arg;
	struct ieee80211_hwmp_state *hs = vap->iv_hwmp;
	struct ieee80211_mesh_state *ms = vap->iv_mesh;
	struct ieee80211_meshrann_ie rann;

	IEEE80211_NOTE(vap, IEEE80211_MSG_HWMP, vap->iv_bss,
	    "%s", "send broadcast RANN");

	rann.rann_flags = 0;
	if (ms->ms_flags & IEEE80211_MESHFLAGS_GATE)
		rann.rann_flags |= IEEE80211_MESHFLAGS_GATE;
	rann.rann_hopcount = 0;
	rann.rann_ttl = ms->ms_ttl;
	IEEE80211_ADDR_COPY(rann.rann_addr, vap->iv_myaddr);
	rann.rann_seq = ++hs->hs_seq;
	rann.rann_interval = ieee80211_hwmp_rannint;
	rann.rann_metric = IEEE80211_MESHLMETRIC_INITIALVAL;

	vap->iv_stats.is_hwmp_rootrann++;
	hwmp_send_rann(vap, broadcastaddr, &rann);
	hwmp_rootmode_setup(vap);
}

/*
 * Update forwarding information to TA if metric improves.
 */
static void
hwmp_update_transmitter(struct ieee80211vap *vap, struct ieee80211_node *ni,
    const char *hwmp_frame)
{
	struct ieee80211_mesh_state *ms = vap->iv_mesh;
	struct ieee80211_mesh_route *rttran = NULL;	/* Transmitter */
	int metric = 0;

	rttran = ieee80211_mesh_rt_find(vap, ni->ni_macaddr);
	if (rttran == NULL) {
		rttran = ieee80211_mesh_rt_add(vap, ni->ni_macaddr);
		if (rttran == NULL) {
			IEEE80211_NOTE(vap, IEEE80211_MSG_HWMP, ni,
			    "unable to add path to transmitter %6D of %s",
			    ni->ni_macaddr, ":", hwmp_frame);
			vap->iv_stats.is_mesh_rtaddfailed++;
			return;
		}
	}
	metric = ms->ms_pmetric->mpm_metric(ni);
	if (!(rttran->rt_flags & IEEE80211_MESHRT_FLAGS_VALID) ||
	    rttran->rt_metric > metric)
	{
		IEEE80211_NOTE(vap, IEEE80211_MSG_HWMP, ni,
		    "%s path to transmitter %6D of %s, metric %d:%d",
		    rttran->rt_flags & IEEE80211_MESHRT_FLAGS_VALID ?
		    "prefer" : "update", ni->ni_macaddr, ":", hwmp_frame,
		    rttran->rt_metric, metric);
		IEEE80211_ADDR_COPY(rttran->rt_nexthop, ni->ni_macaddr);
		rttran->rt_metric = metric;
		rttran->rt_nhops  = 1;
		ieee80211_mesh_rt_update(rttran, ms->ms_ppath->mpp_inact);
		rttran->rt_flags = IEEE80211_MESHRT_FLAGS_VALID;
	}
}

#define	PREQ_TFLAGS(n)	preq->preq_targets[n].target_flags
#define	PREQ_TADDR(n)	preq->preq_targets[n].target_addr
#define	PREQ_TSEQ(n)	preq->preq_targets[n].target_seq
static void
hwmp_recv_preq(struct ieee80211vap *vap, struct ieee80211_node *ni,
    const struct ieee80211_frame *wh, const struct ieee80211_meshpreq_ie *preq)
{
	struct ieee80211_mesh_state *ms = vap->iv_mesh;
	struct ieee80211_mesh_route *rtorig = NULL;
	struct ieee80211_mesh_route *rtorig_ext = NULL;
	struct ieee80211_mesh_route *rttarg = NULL;
	struct ieee80211_hwmp_route *hrorig = NULL;
	struct ieee80211_hwmp_route *hrtarg = NULL;
	struct ieee80211_hwmp_state *hs = vap->iv_hwmp;
	ieee80211_hwmp_seq preqid;	/* last seen preqid for orig */
	uint32_t metric = 0;

	/*
	 * Ignore PREQs from us. Could happen because someone forward it
	 * back to us.
	 */
	if (IEEE80211_ADDR_EQ(vap->iv_myaddr, preq->preq_origaddr))
		return;

	IEEE80211_NOTE(vap, IEEE80211_MSG_HWMP, ni,
	    "received PREQ, orig %6D, targ(0) %6D", preq->preq_origaddr, ":",
	    PREQ_TADDR(0), ":");

	/*
	 * Acceptance criteria: (if the PREQ is not for us or not broadcast,
	 * or an external mac address not proxied by us),
	 * AND forwarding is disabled, discard this PREQ.
	 */
	rttarg = ieee80211_mesh_rt_find(vap, PREQ_TADDR(0));
	if (!(ms->ms_flags & IEEE80211_MESHFLAGS_FWD) &&
	    (!IEEE80211_ADDR_EQ(vap->iv_myaddr, PREQ_TADDR(0)) ||
	    !IEEE80211_IS_MULTICAST(PREQ_TADDR(0)) ||
	    (rttarg != NULL &&
	    rttarg->rt_flags & IEEE80211_MESHRT_FLAGS_PROXY &&
	    IEEE80211_ADDR_EQ(vap->iv_myaddr, rttarg->rt_mesh_gate)))) {
		IEEE80211_DISCARD_MAC(vap, IEEE80211_MSG_HWMP,
		    preq->preq_origaddr, NULL, "%s", "not accepting PREQ");
		return;
	}
	/*
	 * Acceptance criteria: if unicast addressed 
	 * AND no valid forwarding for Target of PREQ, discard this PREQ.
	 */
	if(rttarg != NULL)
		hrtarg = IEEE80211_MESH_ROUTE_PRIV(rttarg,
		    struct ieee80211_hwmp_route);
	/* Address mode: ucast */
	if(preq->preq_flags & IEEE80211_MESHPREQ_FLAGS_AM &&
	    rttarg == NULL &&
	    !IEEE80211_ADDR_EQ(vap->iv_myaddr, PREQ_TADDR(0))) {
		IEEE80211_DISCARD_MAC(vap, IEEE80211_MSG_HWMP,
		    preq->preq_origaddr, NULL,
		    "unicast addressed PREQ of unknown target %6D",
		    PREQ_TADDR(0), ":");
		return;
	}

	/* PREQ ACCEPTED */

	rtorig = ieee80211_mesh_rt_find(vap, preq->preq_origaddr);
	if (rtorig == NULL) {
		rtorig = ieee80211_mesh_rt_add(vap, preq->preq_origaddr);
		if (rtorig == NULL) {
			IEEE80211_NOTE(vap, IEEE80211_MSG_HWMP, ni,
			    "unable to add orig path to %6D",
			    preq->preq_origaddr, ":");
			vap->iv_stats.is_mesh_rtaddfailed++;
			return;
		}
		IEEE80211_NOTE(vap, IEEE80211_MSG_HWMP, ni,
		    "adding originator %6D", preq->preq_origaddr, ":");
	}
	hrorig = IEEE80211_MESH_ROUTE_PRIV(rtorig, struct ieee80211_hwmp_route);

	/* record last seen preqid */
	preqid = hrorig->hr_preqid;
	hrorig->hr_preqid = HWMP_SEQ_MAX(hrorig->hr_preqid, preq->preq_id);

	/* Data creation and update of forwarding information
	 * according to Table 11C-8 for originator mesh STA.
	 */
	metric = preq->preq_metric + ms->ms_pmetric->mpm_metric(ni);
	if (HWMP_SEQ_GT(preq->preq_origseq, hrorig->hr_seq) ||
	    (HWMP_SEQ_EQ(preq->preq_origseq, hrorig->hr_seq) &&
	    metric < rtorig->rt_metric)) {
		hrorig->hr_seq = preq->preq_origseq;
		IEEE80211_ADDR_COPY(rtorig->rt_nexthop, wh->i_addr2);
		rtorig->rt_metric = metric;
		rtorig->rt_nhops  = preq->preq_hopcount + 1;
		ieee80211_mesh_rt_update(rtorig, preq->preq_lifetime);
		/* Path to orig is valid now.
		 * NB: we know it can't be Proxy, and if it is GATE
		 * it will be marked below.
		 */
		rtorig->rt_flags = IEEE80211_MESHRT_FLAGS_VALID;
	} else if ((hrtarg != NULL &&
	    !HWMP_SEQ_EQ(hrtarg->hr_seq, PREQ_TSEQ(0))) ||
	    (rtorig->rt_flags & IEEE80211_MESHRT_FLAGS_VALID &&
	    preqid >= preq->preq_id)) {
		IEEE80211_NOTE(vap, IEEE80211_MSG_HWMP, ni,
		    "discard PREQ from %6D, old seqno %u <= %u,"
		    " or old preqid %u < %u",
		    preq->preq_origaddr, ":",
		    preq->preq_origseq, hrorig->hr_seq,
		    preq->preq_id, preqid);
		return;
	}

	/* Update forwarding information to TA if metric improves. */
	hwmp_update_transmitter(vap, ni, "PREQ");

	/*
	 * Check if the PREQ is addressed to us.
	 * or a Proxy currently gated by us.
	 */
	if (IEEE80211_ADDR_EQ(vap->iv_myaddr, PREQ_TADDR(0)) ||
	    (ms->ms_flags & IEEE80211_MESHFLAGS_GATE &&
	    rttarg != NULL &&
	    IEEE80211_ADDR_EQ(vap->iv_myaddr, rttarg->rt_mesh_gate) &&
	    rttarg->rt_flags & IEEE80211_MESHRT_FLAGS_PROXY &&
	    rttarg->rt_flags & IEEE80211_MESHRT_FLAGS_VALID)) {
		struct ieee80211_meshprep_ie prep;

		/*
		 * When we are the target we shall update our own HWMP seq
		 * number with max of (current and preq->seq) + 1
		 */
		hs->hs_seq = HWMP_SEQ_MAX(hs->hs_seq, PREQ_TSEQ(0)) + 1;

		prep.prep_flags = 0;
		prep.prep_hopcount = 0;
		prep.prep_metric = IEEE80211_MESHLMETRIC_INITIALVAL;
		IEEE80211_ADDR_COPY(prep.prep_targetaddr, vap->iv_myaddr);
		if (rttarg != NULL && /* if NULL it means we are the target */
		    rttarg->rt_flags & IEEE80211_MESHRT_FLAGS_PROXY) {
			IEEE80211_NOTE(vap, IEEE80211_MSG_HWMP, ni,
			    "reply for proxy %6D", rttarg->rt_dest, ":");
			prep.prep_flags |= IEEE80211_MESHPREP_FLAGS_AE;
			IEEE80211_ADDR_COPY(prep.prep_target_ext_addr,
			    rttarg->rt_dest);
			/* update proxy seqno to HWMP seqno */
			rttarg->rt_ext_seq = hs->hs_seq;
			prep.prep_hopcount = rttarg->rt_nhops;
			prep.prep_metric = rttarg->rt_metric;
			IEEE80211_ADDR_COPY(prep.prep_targetaddr, rttarg->rt_mesh_gate);
		}
		/*
		 * Build and send a PREP frame.
		 */
		prep.prep_ttl = ms->ms_ttl;
		prep.prep_targetseq = hs->hs_seq;
		prep.prep_lifetime = preq->preq_lifetime;
		IEEE80211_ADDR_COPY(prep.prep_origaddr, preq->preq_origaddr);
		prep.prep_origseq = preq->preq_origseq;

		IEEE80211_NOTE(vap, IEEE80211_MSG_HWMP, ni,
		    "reply to %6D", preq->preq_origaddr, ":");
		hwmp_send_prep(vap, wh->i_addr2, &prep);
		return;
	}
	/* we may update our proxy information for the orig external */
	else if (preq->preq_flags & IEEE80211_MESHPREQ_FLAGS_AE) {
		rtorig_ext =
		    ieee80211_mesh_rt_find(vap, preq->preq_orig_ext_addr);
		if (rtorig_ext == NULL) {
			rtorig_ext = ieee80211_mesh_rt_add(vap,
			    preq->preq_orig_ext_addr);
			if (rtorig_ext == NULL) {
				IEEE80211_NOTE(vap, IEEE80211_MSG_HWMP, ni,
				    "unable to add orig ext proxy to %6D",
				    preq->preq_orig_ext_addr, ":");
				vap->iv_stats.is_mesh_rtaddfailed++;
				return;
			}
			IEEE80211_ADDR_COPY(rtorig_ext->rt_mesh_gate,
			    preq->preq_origaddr);
		}
		rtorig_ext->rt_ext_seq = preq->preq_origseq;
		ieee80211_mesh_rt_update(rtorig_ext, preq->preq_lifetime);
	}
	/*
	 * Proactive PREQ: reply with a proactive PREP to the
	 * root STA if requested.
	 */
	if (IEEE80211_ADDR_EQ(PREQ_TADDR(0), broadcastaddr) &&
	    (PREQ_TFLAGS(0) & IEEE80211_MESHPREQ_TFLAGS_TO)) {
		IEEE80211_NOTE(vap, IEEE80211_MSG_HWMP, ni,
		    "root mesh station @ %6D", preq->preq_origaddr, ":");

		/* Check if root is a mesh gate, mark it */
		if (preq->preq_flags & IEEE80211_MESHPREQ_FLAGS_GATE) {
			struct ieee80211_mesh_gate_route *gr;

			rtorig->rt_flags |= IEEE80211_MESHRT_FLAGS_GATE;
			gr = ieee80211_mesh_mark_gate(vap, preq->preq_origaddr,
			    rtorig);
			gr->gr_lastseq = 0; /* NOT GANN */
		}

		/*
		 * Reply with a PREP if we don't have a path to the root
		 * or if the root sent us a proactive PREQ.
		 */
		if ((rtorig->rt_flags & IEEE80211_MESHRT_FLAGS_VALID) == 0 ||
		    (preq->preq_flags & IEEE80211_MESHPREQ_FLAGS_PP)) {
			struct ieee80211_meshprep_ie prep;

			prep.prep_flags = 0;
			prep.prep_hopcount = 0;
			prep.prep_ttl = ms->ms_ttl;
			IEEE80211_ADDR_COPY(prep.prep_origaddr,
			    preq->preq_origaddr);
			prep.prep_origseq = preq->preq_origseq;
			prep.prep_lifetime = preq->preq_lifetime;
			prep.prep_metric = IEEE80211_MESHLMETRIC_INITIALVAL;
			IEEE80211_ADDR_COPY(prep.prep_targetaddr,
			    vap->iv_myaddr);
			prep.prep_targetseq = ++hs->hs_seq;
			hwmp_send_prep(vap, rtorig->rt_nexthop, &prep);
		}
	}

	/*
	 * Forwarding and Intermediate reply for PREQs with 1 target.
	 */
	if ((preq->preq_tcount == 1) && (preq->preq_ttl > 1) &&
	    (ms->ms_flags & IEEE80211_MESHFLAGS_FWD)) {
		struct ieee80211_meshpreq_ie ppreq; /* propagated PREQ */

		memcpy(&ppreq, preq, sizeof(ppreq));

		/*
		 * We have a valid route to this node.
		 * NB: if target is proxy dont reply.
		 */
		if (rttarg != NULL &&
		    rttarg->rt_flags & IEEE80211_MESHRT_FLAGS_VALID &&
		    !(rttarg->rt_flags & IEEE80211_MESHRT_FLAGS_PROXY)) {
			/*
			 * Check if we can send an intermediate Path Reply,
			 * i.e., Target Only bit is not set and target is not
			 * the MAC broadcast address.
			 */
			if (!(PREQ_TFLAGS(0) & IEEE80211_MESHPREQ_TFLAGS_TO) &&
			    !IEEE80211_ADDR_EQ(PREQ_TADDR(0), broadcastaddr)) {
				struct ieee80211_meshprep_ie prep;

				IEEE80211_NOTE(vap, IEEE80211_MSG_HWMP, ni,
				    "intermediate reply for PREQ from %6D",
				    preq->preq_origaddr, ":");
				prep.prep_flags = 0;
				prep.prep_hopcount = rttarg->rt_nhops;
				prep.prep_ttl = ms->ms_ttl;
				IEEE80211_ADDR_COPY(&prep.prep_targetaddr,
				    PREQ_TADDR(0));
				prep.prep_targetseq = hrtarg->hr_seq;
				prep.prep_lifetime = preq->preq_lifetime;
				prep.prep_metric =rttarg->rt_metric;
				IEEE80211_ADDR_COPY(&prep.prep_origaddr,
				    preq->preq_origaddr);
				prep.prep_origseq = hrorig->hr_seq;
				hwmp_send_prep(vap, rtorig->rt_nexthop, &prep);

				/*
				 * Set TO and unset RF bits because we have
				 * sent a PREP.
				 */
				ppreq.preq_targets[0].target_flags |=
				    IEEE80211_MESHPREQ_TFLAGS_TO;
			}
		}

		IEEE80211_NOTE(vap, IEEE80211_MSG_HWMP, ni,
		    "forward PREQ from %6D",
		    preq->preq_origaddr, ":");
		ppreq.preq_hopcount += 1;
		ppreq.preq_ttl -= 1;
		ppreq.preq_metric += ms->ms_pmetric->mpm_metric(ni);

		/* don't do PREQ ratecheck when we propagate */
		hwmp_send_preq(vap, broadcastaddr, &ppreq, NULL, NULL);
	}
}
#undef	PREQ_TFLAGS
#undef	PREQ_TADDR
#undef	PREQ_TSEQ

static int
hwmp_send_preq(struct ieee80211vap *vap,
    const uint8_t da[IEEE80211_ADDR_LEN],
    struct ieee80211_meshpreq_ie *preq,
    struct timeval *last, struct timeval *minint)
{

	/*
	 * Enforce PREQ interval.
	 * NB: Proactive ROOT PREQs rate is handled by cb task.
	 */
	if (last != NULL && minint != NULL) {
		if (ratecheck(last, minint) == 0)
			return EALREADY; /* XXX: we should postpone */
		getmicrouptime(last);
	}

	/*
	 * mesh preq action frame format
	 *     [6] da
	 *     [6] sa
	 *     [6] addr3 = sa
	 *     [1] action
	 *     [1] category
	 *     [tlv] mesh path request
	 */
	preq->preq_ie = IEEE80211_ELEMID_MESHPREQ;
	preq->preq_len = (preq->preq_flags & IEEE80211_MESHPREQ_FLAGS_AE ?
	    IEEE80211_MESHPREQ_BASE_SZ_AE : IEEE80211_MESHPREQ_BASE_SZ) +
	    preq->preq_tcount * IEEE80211_MESHPREQ_TRGT_SZ;
	return hwmp_send_action(vap, da, (uint8_t *)preq, preq->preq_len+2);
}

static void
hwmp_recv_prep(struct ieee80211vap *vap, struct ieee80211_node *ni,
    const struct ieee80211_frame *wh, const struct ieee80211_meshprep_ie *prep)
{
#define	IS_PROXY(rt)	(rt->rt_flags & IEEE80211_MESHRT_FLAGS_PROXY)
#define	PROXIED_BY_US(rt)		\
    (IEEE80211_ADDR_EQ(vap->iv_myaddr, rt->rt_mesh_gate))
	struct ieee80211_mesh_state *ms = vap->iv_mesh;
	struct ieee80211_hwmp_state *hs = vap->iv_hwmp;
	struct ieee80211_mesh_route *rt = NULL;
	struct ieee80211_mesh_route *rtorig = NULL;
	struct ieee80211_mesh_route *rtext = NULL;
	struct ieee80211_hwmp_route *hr;
	struct ieee80211com *ic = vap->iv_ic;
	struct mbuf *m, *next;
	uint32_t metric = 0;
	const uint8_t *addr;

	IEEE80211_NOTE(vap, IEEE80211_MSG_HWMP, ni,
	    "received PREP, orig %6D, targ %6D", prep->prep_origaddr, ":",
	    prep->prep_targetaddr, ":");

	/*
	 * Acceptance criteria: (If the corresponding PREP was not generated
	 * by us OR not generated by an external mac that is not proxied by us)
	 * AND forwarding is disabled, discard this PREP.
	 */
	rtorig = ieee80211_mesh_rt_find(vap, prep->prep_origaddr);
	if ((!IEEE80211_ADDR_EQ(vap->iv_myaddr, prep->prep_origaddr) ||
	    (rtorig != NULL && IS_PROXY(rtorig) && !PROXIED_BY_US(rtorig))) &&
	    !(ms->ms_flags & IEEE80211_MESHFLAGS_FWD)){
		IEEE80211_NOTE(vap, IEEE80211_MSG_HWMP, ni,
		    "discard PREP, orig(%6D) not proxied or generated by us",
		    prep->prep_origaddr, ":");
		return;
	}

	/* PREP ACCEPTED */

	/*
	 * If accepted shall create or update the active forwarding information
	 * it maintains for the target mesh STA of the PREP (according to the
	 * rules defined in 13.10.8.4). If the conditions for creating or
	 * updating the forwarding information have not been met in those
	 * rules, no further steps are applied to the PREP.
	 */
	rt = ieee80211_mesh_rt_find(vap, prep->prep_targetaddr);
	if (rt == NULL) {
		rt = ieee80211_mesh_rt_add(vap, prep->prep_targetaddr);
		if (rt == NULL) {
			IEEE80211_NOTE(vap, IEEE80211_MSG_HWMP, ni,
			    "unable to add PREP path to %6D",
			    prep->prep_targetaddr, ":");
			vap->iv_stats.is_mesh_rtaddfailed++;
			return;
		}
		IEEE80211_NOTE(vap, IEEE80211_MSG_HWMP, ni,
		    "adding target %6D", prep->prep_targetaddr, ":");
	}
	hr = IEEE80211_MESH_ROUTE_PRIV(rt, struct ieee80211_hwmp_route);
	/* update path metric */
	metric = prep->prep_metric + ms->ms_pmetric->mpm_metric(ni);
	if ((rt->rt_flags & IEEE80211_MESHRT_FLAGS_VALID)) {
		if (HWMP_SEQ_LT(prep->prep_targetseq, hr->hr_seq)) {
			IEEE80211_NOTE(vap, IEEE80211_MSG_HWMP, ni,
			    "discard PREP from %6D, old seq no %u < %u",
			    prep->prep_targetaddr, ":",
			    prep->prep_targetseq, hr->hr_seq);
			return;
		} else if (HWMP_SEQ_LEQ(prep->prep_targetseq, hr->hr_seq) &&
		    metric > rt->rt_metric) {
			IEEE80211_NOTE(vap, IEEE80211_MSG_HWMP, ni,
			    "discard PREP from %6D, new metric %u > %u",
			    prep->prep_targetaddr, ":",
			    metric, rt->rt_metric);
			return;
		}
	}

	IEEE80211_NOTE(vap, IEEE80211_MSG_HWMP, ni,
	    "%s path to %6D, hopcount %d:%d metric %d:%d",
	    rt->rt_flags & IEEE80211_MESHRT_FLAGS_VALID ?
	    "prefer" : "update",
	    prep->prep_targetaddr, ":",
	    rt->rt_nhops, prep->prep_hopcount + 1,
	    rt->rt_metric, metric);

	hr->hr_seq = prep->prep_targetseq;
	hr->hr_preqretries = 0;
	IEEE80211_ADDR_COPY(rt->rt_nexthop, ni->ni_macaddr);
	rt->rt_metric = metric;
	rt->rt_nhops = prep->prep_hopcount + 1;
	ieee80211_mesh_rt_update(rt, prep->prep_lifetime);
	if (rt->rt_flags & IEEE80211_MESHRT_FLAGS_DISCOVER) {
		/* discovery complete */
		rt->rt_flags &= ~IEEE80211_MESHRT_FLAGS_DISCOVER;
	}
	rt->rt_flags |= IEEE80211_MESHRT_FLAGS_VALID; /* mark valid */

	/* Update forwarding information to TA if metric improves */
	hwmp_update_transmitter(vap, ni, "PREP");

	/*
	 * If it's NOT for us, propagate the PREP
	 */
	if (!IEEE80211_ADDR_EQ(vap->iv_myaddr, prep->prep_origaddr) &&
	    prep->prep_ttl > 1 &&
	    prep->prep_hopcount < hs->hs_maxhops) {
		struct ieee80211_meshprep_ie pprep; /* propagated PREP */
		/*
		 * NB: We should already have setup the path to orig
		 * mesh STA when we propagated PREQ to target mesh STA,
		 * no PREP is generated without a corresponding PREQ.
		 * XXX: for now just ignore.
		 */
		if (rtorig == NULL) {
			IEEE80211_NOTE(vap, IEEE80211_MSG_HWMP, ni,
			    "received PREP for an unknown orig(%6D)",
			    prep->prep_origaddr, ":");
			return;
		}

		IEEE80211_NOTE(vap, IEEE80211_MSG_HWMP, ni,
		    "propagate PREP from %6D",
		    prep->prep_targetaddr, ":");

		memcpy(&pprep, prep, sizeof(pprep));
		pprep.prep_hopcount += 1;
		pprep.prep_ttl -= 1;
		pprep.prep_metric += ms->ms_pmetric->mpm_metric(ni);
		hwmp_send_prep(vap, rtorig->rt_nexthop, &pprep);

		/* precursor list for the Target Mesh STA Address is updated */
	}

	/*
	 * Check if we received a PREP w/ AE and store target external address.
	 * We may store target external address if recevied PREP w/ AE
	 * and we are not final destination
	 */
	if (prep->prep_flags & IEEE80211_MESHPREP_FLAGS_AE) {
		rtext = ieee80211_mesh_rt_find(vap,
			prep->prep_target_ext_addr);
		if (rtext == NULL) {
			rtext = ieee80211_mesh_rt_add(vap,
				prep->prep_target_ext_addr);
			if (rtext == NULL) {
				IEEE80211_NOTE(vap, IEEE80211_MSG_HWMP, ni,
				    "unable to add PREP path to proxy %6D",
				    prep->prep_targetaddr, ":");
				vap->iv_stats.is_mesh_rtaddfailed++;
				return;
			}
		}
		IEEE80211_NOTE(vap, IEEE80211_MSG_HWMP, ni,
		    "%s path to %6D, hopcount %d:%d metric %d:%d",
		    rtext->rt_flags & IEEE80211_MESHRT_FLAGS_VALID ?
		    "prefer" : "update",
		    prep->prep_target_ext_addr, ":",
		    rtext->rt_nhops, prep->prep_hopcount + 1,
		    rtext->rt_metric, metric);

		rtext->rt_flags = IEEE80211_MESHRT_FLAGS_PROXY |
			IEEE80211_MESHRT_FLAGS_VALID;
		IEEE80211_ADDR_COPY(rtext->rt_dest,
		    prep->prep_target_ext_addr);
		IEEE80211_ADDR_COPY(rtext->rt_mesh_gate,
		    prep->prep_targetaddr);
		IEEE80211_ADDR_COPY(rtext->rt_nexthop, wh->i_addr2);
		rtext->rt_metric = metric;
		rtext->rt_lifetime = prep->prep_lifetime;
		rtext->rt_nhops = prep->prep_hopcount + 1;
		rtext->rt_ext_seq = prep->prep_origseq; /* new proxy seq */
		/*
		 * XXX: proxy entries have no HWMP priv data,
		 * nullify them to be sure?
		 */
	}
	/*
	 * Check for frames queued awaiting path discovery.
	 * XXX probably can tell exactly and avoid remove call
	 * NB: hash may have false matches, if so they will get
	 *     stuck back on the stageq because there won't be
	 *     a path.
	 */
	addr = prep->prep_flags & IEEE80211_MESHPREP_FLAGS_AE ?
	    prep->prep_target_ext_addr : prep->prep_targetaddr;
	m = ieee80211_ageq_remove(&ic->ic_stageq,
	    (struct ieee80211_node *)(uintptr_t)
	    ieee80211_mac_hash(ic, addr)); /* either dest or ext_dest */

	/*
	 * All frames in the stageq here should be non-M_ENCAP; or things
	 * will get very unhappy.
	 */
	for (; m != NULL; m = next) {
		next = m->m_nextpkt;
		m->m_nextpkt = NULL;
		IEEE80211_NOTE(vap, IEEE80211_MSG_HWMP, ni,
		    "flush queued frame %p len %d", m, m->m_pkthdr.len);
		/*
		 * If the mbuf has M_ENCAP set, ensure we free it.
		 * Note that after if_transmit() is called, m is invalid.
		 */
		(void) ieee80211_vap_xmitpkt(vap, m);
	}
#undef	IS_PROXY
#undef	PROXIED_BY_US
}

static int
hwmp_send_prep(struct ieee80211vap *vap,
    const uint8_t da[IEEE80211_ADDR_LEN],
    struct ieee80211_meshprep_ie *prep)
{
	/* NB: there's no PREP minimum interval. */

	/*
	 * mesh prep action frame format
	 *     [6] da
	 *     [6] sa
	 *     [6] addr3 = sa
	 *     [1] action
	 *     [1] category
	 *     [tlv] mesh path reply
	 */
	prep->prep_ie = IEEE80211_ELEMID_MESHPREP;
	prep->prep_len = prep->prep_flags & IEEE80211_MESHPREP_FLAGS_AE ?
	    IEEE80211_MESHPREP_BASE_SZ_AE : IEEE80211_MESHPREP_BASE_SZ;
	return hwmp_send_action(vap, da, (uint8_t *)prep, prep->prep_len + 2);
}

#define	PERR_DFLAGS(n)	perr.perr_dests[n].dest_flags
#define	PERR_DADDR(n)	perr.perr_dests[n].dest_addr
#define	PERR_DSEQ(n)	perr.perr_dests[n].dest_seq
#define	PERR_DRCODE(n)	perr.perr_dests[n].dest_rcode
static void
hwmp_peerdown(struct ieee80211_node *ni)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211_mesh_state *ms = vap->iv_mesh;
	struct ieee80211_meshperr_ie perr;
	struct ieee80211_mesh_route *rt;
	struct ieee80211_hwmp_route *hr;

	rt = ieee80211_mesh_rt_find(vap, ni->ni_macaddr);
	if (rt == NULL)
		return;
	hr = IEEE80211_MESH_ROUTE_PRIV(rt, struct ieee80211_hwmp_route);
	IEEE80211_NOTE(vap, IEEE80211_MSG_HWMP, ni,
	    "%s", "delete route entry");
	perr.perr_ttl = ms->ms_ttl;
	perr.perr_ndests = 1;
	PERR_DFLAGS(0) = 0;
	if (hr->hr_seq == 0)
		PERR_DFLAGS(0) |= IEEE80211_MESHPERR_DFLAGS_USN;
	PERR_DFLAGS(0) |= IEEE80211_MESHPERR_DFLAGS_RC;
	IEEE80211_ADDR_COPY(PERR_DADDR(0), rt->rt_dest);
	PERR_DSEQ(0) = ++hr->hr_seq;
	PERR_DRCODE(0) = IEEE80211_REASON_MESH_PERR_DEST_UNREACH;
	/* NB: flush everything passing through peer */
	ieee80211_mesh_rt_flush_peer(vap, ni->ni_macaddr);
	hwmp_send_perr(vap, broadcastaddr, &perr);
}
#undef	PERR_DFLAGS
#undef	PERR_DADDR
#undef	PERR_DSEQ
#undef	PERR_DRCODE

#define	PERR_DFLAGS(n)		perr->perr_dests[n].dest_flags
#define	PERR_DADDR(n)		perr->perr_dests[n].dest_addr
#define	PERR_DSEQ(n)		perr->perr_dests[n].dest_seq
#define	PERR_DEXTADDR(n)	perr->perr_dests[n].dest_ext_addr
static void
hwmp_recv_perr(struct ieee80211vap *vap, struct ieee80211_node *ni,
    const struct ieee80211_frame *wh, const struct ieee80211_meshperr_ie *perr)
{
	struct ieee80211_mesh_state *ms = vap->iv_mesh;
	struct ieee80211_mesh_route *rt = NULL;
	struct ieee80211_mesh_route *rt_ext = NULL;
	struct ieee80211_hwmp_route *hr;
	struct ieee80211_meshperr_ie *pperr = NULL;
	int i, j = 0, forward = 0;

	IEEE80211_NOTE(vap, IEEE80211_MSG_HWMP, ni,
	    "received PERR from %6D", wh->i_addr2, ":");

	/*
	 * if forwarding is true, prepare pperr
	 */
	if (ms->ms_flags & IEEE80211_MESHFLAGS_FWD) {
		forward = 1;
		pperr = IEEE80211_MALLOC(sizeof(*perr) + 31*sizeof(*perr->perr_dests),
		    M_80211_MESH_PERR, IEEE80211_M_NOWAIT); /* XXX: magic number, 32 err dests */
	}

	/*
	 * Acceptance criteria: check if we have forwarding information
	 * stored about destination, and that nexthop == TA of this PERR.
	 * NB: we also build a new PERR to propagate in case we should forward.
	 */
	for (i = 0; i < perr->perr_ndests; i++) {
		rt = ieee80211_mesh_rt_find(vap, PERR_DADDR(i));
		if (rt == NULL)
			continue;
		if (!IEEE80211_ADDR_EQ(rt->rt_nexthop, wh->i_addr2))
			continue;

		/* found and accepted a PERR ndest element, process it... */
		if (forward)
			memcpy(&pperr->perr_dests[j], &perr->perr_dests[i],
			    sizeof(*perr->perr_dests));
		hr = IEEE80211_MESH_ROUTE_PRIV(rt, struct ieee80211_hwmp_route);
		switch(PERR_DFLAGS(i)) {
		case (IEEE80211_REASON_MESH_PERR_NO_FI):
			if (PERR_DSEQ(i) == 0) {
				hr->hr_seq++;
				if (forward) {
					pperr->perr_dests[j].dest_seq =
					    hr->hr_seq;
				}
			} else {
				hr->hr_seq = PERR_DSEQ(i);
			}
			rt->rt_flags &= ~IEEE80211_MESHRT_FLAGS_VALID;
			j++;
			break;
		case (IEEE80211_REASON_MESH_PERR_DEST_UNREACH):
			if(HWMP_SEQ_GT(PERR_DSEQ(i), hr->hr_seq)) {
				hr->hr_seq = PERR_DSEQ(i);
				rt->rt_flags &= ~IEEE80211_MESHRT_FLAGS_VALID;
				j++;
			}
			break;
		case (IEEE80211_REASON_MESH_PERR_NO_PROXY):
			rt_ext = ieee80211_mesh_rt_find(vap, PERR_DEXTADDR(i));
			if (rt_ext != NULL) {
				rt_ext->rt_flags &=
				    ~IEEE80211_MESHRT_FLAGS_VALID;
				j++;
			}
			break;
		default:
			IEEE80211_DISCARD(vap, IEEE80211_MSG_HWMP, wh, NULL,
			    "PERR, unknown reason code %u\n", PERR_DFLAGS(i));
			goto done; /* XXX: stats?? */
		}
		ieee80211_mesh_rt_flush_peer(vap, PERR_DADDR(i));
		KASSERT(j < 32, ("PERR, error ndest >= 32 (%u)", j));
	}
	if (j == 0) {
		IEEE80211_DISCARD(vap, IEEE80211_MSG_HWMP, wh, NULL, "%s",
		    "PERR not accepted");
		goto done; /* XXX: stats?? */
	}

	/*
	 * Propagate the PERR if we previously found it on our routing table.
	 */
	if (forward && perr->perr_ttl > 1) {
		IEEE80211_NOTE(vap, IEEE80211_MSG_HWMP, ni,
		    "propagate PERR from %6D", wh->i_addr2, ":");
		pperr->perr_ndests = j;
		pperr->perr_ttl--;
		hwmp_send_perr(vap, broadcastaddr, pperr);
	}
done:
	if (pperr != NULL)
		IEEE80211_FREE(pperr, M_80211_MESH_PERR);
}
#undef	PERR_DFLAGS
#undef	PERR_DADDR
#undef	PERR_DSEQ
#undef	PERR_DEXTADDR

static int
hwmp_send_perr(struct ieee80211vap *vap,
    const uint8_t da[IEEE80211_ADDR_LEN],
    struct ieee80211_meshperr_ie *perr)
{
	struct ieee80211_hwmp_state *hs = vap->iv_hwmp;
	int i;
	uint8_t length = 0;

	/*
	 * Enforce PERR interval.
	 */
	if (ratecheck(&hs->hs_lastperr, &ieee80211_hwmp_perrminint) == 0)
		return EALREADY;
	getmicrouptime(&hs->hs_lastperr);

	/*
	 * mesh perr action frame format
	 *     [6] da
	 *     [6] sa
	 *     [6] addr3 = sa
	 *     [1] action
	 *     [1] category
	 *     [tlv] mesh path error
	 */
	perr->perr_ie = IEEE80211_ELEMID_MESHPERR;
	length = IEEE80211_MESHPERR_BASE_SZ;
	for (i = 0; i<perr->perr_ndests; i++) {
		if (perr->perr_dests[i].dest_flags &
		    IEEE80211_MESHPERR_FLAGS_AE) {
			length += IEEE80211_MESHPERR_DEST_SZ_AE;
			continue ;
		}
		length += IEEE80211_MESHPERR_DEST_SZ;
	}
	perr->perr_len =length;
	return hwmp_send_action(vap, da, (uint8_t *)perr, perr->perr_len+2);
}

/*
 * Called from the rest of the net80211 code (mesh code for example).
 * NB: IEEE80211_REASON_MESH_PERR_DEST_UNREACH can be trigger by the fact that
 * a mesh STA is unable to forward an MSDU/MMPDU to a next-hop mesh STA.
 */
#define	PERR_DFLAGS(n)		perr.perr_dests[n].dest_flags
#define	PERR_DADDR(n)		perr.perr_dests[n].dest_addr
#define	PERR_DSEQ(n)		perr.perr_dests[n].dest_seq
#define	PERR_DEXTADDR(n)	perr.perr_dests[n].dest_ext_addr
#define	PERR_DRCODE(n)		perr.perr_dests[n].dest_rcode
static void
hwmp_senderror(struct ieee80211vap *vap,
    const uint8_t addr[IEEE80211_ADDR_LEN],
    struct ieee80211_mesh_route *rt, int rcode)
{
	struct ieee80211_mesh_state *ms = vap->iv_mesh;
	struct ieee80211_hwmp_route *hr = NULL;
	struct ieee80211_meshperr_ie perr;

	if (rt != NULL)
		hr = IEEE80211_MESH_ROUTE_PRIV(rt,
		    struct ieee80211_hwmp_route);

	perr.perr_ndests = 1;
	perr.perr_ttl = ms->ms_ttl;
	PERR_DFLAGS(0) = 0;
	PERR_DRCODE(0) = rcode;

	switch (rcode) {
	case IEEE80211_REASON_MESH_PERR_NO_FI:
		IEEE80211_ADDR_COPY(PERR_DADDR(0), addr);
		PERR_DSEQ(0) = 0; /* reserved */
		break;
	case IEEE80211_REASON_MESH_PERR_NO_PROXY:
		KASSERT(rt != NULL, ("no proxy info for sending PERR"));
		KASSERT(rt->rt_flags & IEEE80211_MESHRT_FLAGS_PROXY,
		    ("route is not marked proxy"));
		PERR_DFLAGS(0) |= IEEE80211_MESHPERR_FLAGS_AE;
		IEEE80211_ADDR_COPY(PERR_DADDR(0), vap->iv_myaddr);
		PERR_DSEQ(0) = rt->rt_ext_seq;
		IEEE80211_ADDR_COPY(PERR_DEXTADDR(0), addr);
		break;
	case IEEE80211_REASON_MESH_PERR_DEST_UNREACH:
		KASSERT(rt != NULL, ("no route info for sending PERR"));
		IEEE80211_ADDR_COPY(PERR_DADDR(0), addr);
		PERR_DSEQ(0) = hr->hr_seq;
		break;
	default:
		KASSERT(0, ("unknown reason code for HWMP PERR (%u)", rcode));
	}
	hwmp_send_perr(vap, broadcastaddr, &perr);
}
#undef	PERR_DFLAGS
#undef	PEER_DADDR
#undef	PERR_DSEQ
#undef	PERR_DEXTADDR
#undef	PERR_DRCODE

static void
hwmp_recv_rann(struct ieee80211vap *vap, struct ieee80211_node *ni,
    const struct ieee80211_frame *wh, const struct ieee80211_meshrann_ie *rann)
{
	struct ieee80211_mesh_state *ms = vap->iv_mesh;
	struct ieee80211_hwmp_state *hs = vap->iv_hwmp;
	struct ieee80211_mesh_route *rt = NULL;
	struct ieee80211_hwmp_route *hr;
	struct ieee80211_meshpreq_ie preq;
	struct ieee80211_meshrann_ie prann;

	if (IEEE80211_ADDR_EQ(rann->rann_addr, vap->iv_myaddr))
		return;

	rt = ieee80211_mesh_rt_find(vap, rann->rann_addr);
	if (rt != NULL && rt->rt_flags & IEEE80211_MESHRT_FLAGS_VALID) {
		hr = IEEE80211_MESH_ROUTE_PRIV(rt, struct ieee80211_hwmp_route);

		/* Acceptance criteria: if RANN.seq < stored seq, discard RANN */
		if (HWMP_SEQ_LT(rann->rann_seq, hr->hr_seq)) {
			IEEE80211_DISCARD(vap, IEEE80211_MSG_HWMP, wh, NULL,
			"RANN seq %u < %u", rann->rann_seq, hr->hr_seq);
			return;
		}

		/* Acceptance criteria: if RANN.seq == stored seq AND
		* RANN.metric > stored metric, discard RANN */
		if (HWMP_SEQ_EQ(rann->rann_seq, hr->hr_seq) &&
		rann->rann_metric > rt->rt_metric) {
			IEEE80211_DISCARD(vap, IEEE80211_MSG_HWMP, wh, NULL,
			"RANN metric %u > %u", rann->rann_metric, rt->rt_metric);
			return;
		}
	}

	/* RANN ACCEPTED */

	ieee80211_hwmp_rannint = rann->rann_interval; /* XXX: mtx lock? */

	if (rt == NULL) {
		rt = ieee80211_mesh_rt_add(vap, rann->rann_addr);
		if (rt == NULL) {
			IEEE80211_DISCARD(vap, IEEE80211_MSG_HWMP, wh, NULL,
			    "unable to add mac for RANN root %6D",
			    rann->rann_addr, ":");
			    vap->iv_stats.is_mesh_rtaddfailed++;
			return;
		}
	}
	hr = IEEE80211_MESH_ROUTE_PRIV(rt, struct ieee80211_hwmp_route);
	/* Check if root is a mesh gate, mark it */
	if (rann->rann_flags & IEEE80211_MESHRANN_FLAGS_GATE) {
		struct ieee80211_mesh_gate_route *gr;

		rt->rt_flags |= IEEE80211_MESHRT_FLAGS_GATE;
		gr = ieee80211_mesh_mark_gate(vap, rann->rann_addr,
			rt);
		gr->gr_lastseq = 0; /* NOT GANN */
	}
	/* discovery timeout */
	ieee80211_mesh_rt_update(rt,
	    ticks_to_msecs(ieee80211_hwmp_roottimeout));

	preq.preq_flags = IEEE80211_MESHPREQ_FLAGS_AM;
	preq.preq_hopcount = 0;
	preq.preq_ttl = ms->ms_ttl;
	preq.preq_id = 0; /* reserved */
	IEEE80211_ADDR_COPY(preq.preq_origaddr, vap->iv_myaddr);
	preq.preq_origseq = ++hs->hs_seq;
	preq.preq_lifetime = ieee80211_hwmp_roottimeout;
	preq.preq_metric = IEEE80211_MESHLMETRIC_INITIALVAL;
	preq.preq_tcount = 1;
	preq.preq_targets[0].target_flags = IEEE80211_MESHPREQ_TFLAGS_TO;
	/* NB: IEEE80211_MESHPREQ_TFLAGS_USN = 0 implicitly implied */
	IEEE80211_ADDR_COPY(preq.preq_targets[0].target_addr, rann->rann_addr);
	preq.preq_targets[0].target_seq = rann->rann_seq;
	/* XXX: if rootconfint have not passed, we built this preq in vain */
	hwmp_send_preq(vap, wh->i_addr2, &preq, &hr->hr_lastrootconf,
	    &ieee80211_hwmp_rootconfint);

	/* propagate a RANN */
	if (rt->rt_flags & IEEE80211_MESHRT_FLAGS_VALID &&
	    rann->rann_ttl > 1 &&
	    ms->ms_flags & IEEE80211_MESHFLAGS_FWD) {
		hr->hr_seq = rann->rann_seq;
		memcpy(&prann, rann, sizeof(prann));
		prann.rann_hopcount += 1;
		prann.rann_ttl -= 1;
		prann.rann_metric += ms->ms_pmetric->mpm_metric(ni);
		hwmp_send_rann(vap, broadcastaddr, &prann);
	}
}

static int
hwmp_send_rann(struct ieee80211vap *vap,
    const uint8_t da[IEEE80211_ADDR_LEN],
    struct ieee80211_meshrann_ie *rann)
{
	/*
	 * mesh rann action frame format
	 *     [6] da
	 *     [6] sa
	 *     [6] addr3 = sa
	 *     [1] action
	 *     [1] category
	 *     [tlv] root annoucement
	 */
	rann->rann_ie = IEEE80211_ELEMID_MESHRANN;
	rann->rann_len = IEEE80211_MESHRANN_BASE_SZ;
	return hwmp_send_action(vap, da, (uint8_t *)rann, rann->rann_len + 2);
}

#define	PREQ_TFLAGS(n)	preq.preq_targets[n].target_flags
#define	PREQ_TADDR(n)	preq.preq_targets[n].target_addr
#define	PREQ_TSEQ(n)	preq.preq_targets[n].target_seq
static void
hwmp_rediscover_cb(void *arg)
{
	struct ieee80211_mesh_route *rt = arg;
	struct ieee80211vap *vap = rt->rt_vap;
	struct ieee80211_hwmp_state *hs = vap->iv_hwmp;
	struct ieee80211_mesh_state *ms = vap->iv_mesh;
	struct ieee80211_hwmp_route *hr;
	struct ieee80211_meshpreq_ie preq; /* Optimize: storing first preq? */

	if ((rt->rt_flags & IEEE80211_MESHRT_FLAGS_VALID))
		return ; /* nothing to do */

	hr = IEEE80211_MESH_ROUTE_PRIV(rt, struct ieee80211_hwmp_route);
	if (hr->hr_preqretries >=
		ieee80211_hwmp_maxpreq_retries) {
		IEEE80211_NOTE_MAC(vap, IEEE80211_MSG_ANY,
			rt->rt_dest, "%s",
			"max number of discovery, send queued frames to GATE");
		ieee80211_mesh_forward_to_gates(vap, rt);
		vap->iv_stats.is_mesh_fwd_nopath++;
		return ; /* XXX: flush queue? */
	}

	hr->hr_preqretries++;


	IEEE80211_NOTE_MAC(vap, IEEE80211_MSG_HWMP, rt->rt_dest,
	    "start path rediscovery , target seq %u", hr->hr_seq);
	/*
	 * Try to discover the path for this node.
	 * Group addressed PREQ Case A
	 */
	preq.preq_flags = 0;
	preq.preq_hopcount = 0;
	preq.preq_ttl = ms->ms_ttl;
	preq.preq_id = ++hs->hs_preqid;
	IEEE80211_ADDR_COPY(preq.preq_origaddr, vap->iv_myaddr);
	preq.preq_origseq = hr->hr_origseq;
	preq.preq_lifetime = ticks_to_msecs(ieee80211_hwmp_pathtimeout);
	preq.preq_metric = IEEE80211_MESHLMETRIC_INITIALVAL;
	preq.preq_tcount = 1;
	IEEE80211_ADDR_COPY(PREQ_TADDR(0), rt->rt_dest);
	PREQ_TFLAGS(0) = 0;
	if (ieee80211_hwmp_targetonly)
		PREQ_TFLAGS(0) |= IEEE80211_MESHPREQ_TFLAGS_TO;
	PREQ_TFLAGS(0) |= IEEE80211_MESHPREQ_TFLAGS_USN;
	PREQ_TSEQ(0) = 0; /* RESERVED when USN flag is set */
	/* XXX check return value */
	hwmp_send_preq(vap, broadcastaddr, &preq, &hr->hr_lastpreq,
	    &ieee80211_hwmp_preqminint);
	callout_reset(&rt->rt_discovery,
		ieee80211_hwmp_net_diameter_traversaltime * 2,
		hwmp_rediscover_cb, rt);
}

static struct ieee80211_node *
hwmp_discover(struct ieee80211vap *vap,
    const uint8_t dest[IEEE80211_ADDR_LEN], struct mbuf *m)
{
	struct ieee80211_hwmp_state *hs = vap->iv_hwmp;
	struct ieee80211_mesh_state *ms = vap->iv_mesh;
	struct ieee80211_mesh_route *rt = NULL;
	struct ieee80211_hwmp_route *hr;
	struct ieee80211_meshpreq_ie preq;
	struct ieee80211_node *ni;
	int sendpreq = 0;

	KASSERT(vap->iv_opmode == IEEE80211_M_MBSS,
	    ("not a mesh vap, opmode %d", vap->iv_opmode));

	KASSERT(!IEEE80211_ADDR_EQ(vap->iv_myaddr, dest),
	    ("%s: discovering self!", __func__));

	ni = NULL;
	if (!IEEE80211_IS_MULTICAST(dest)) {
		rt = ieee80211_mesh_rt_find(vap, dest);
		if (rt == NULL) {
			rt = ieee80211_mesh_rt_add(vap, dest);
			if (rt == NULL) {
				IEEE80211_NOTE(vap, IEEE80211_MSG_HWMP,
				    ni, "unable to add discovery path to %6D",
				    dest, ":");
				vap->iv_stats.is_mesh_rtaddfailed++;
				goto done;
			}
		}
		hr = IEEE80211_MESH_ROUTE_PRIV(rt,
		    struct ieee80211_hwmp_route);
		if (rt->rt_flags & IEEE80211_MESHRT_FLAGS_DISCOVER) {
			IEEE80211_NOTE_MAC(vap, IEEE80211_MSG_HWMP, dest,
			    "%s", "already discovering queue frame until path found");
			sendpreq = 1;
			goto done;
		}
		if ((rt->rt_flags & IEEE80211_MESHRT_FLAGS_VALID) == 0) {
			if (hr->hr_lastdiscovery != 0 &&
			    (ticks - hr->hr_lastdiscovery <
			    (ieee80211_hwmp_net_diameter_traversaltime * 2))) {
				IEEE80211_DISCARD_MAC(vap, IEEE80211_MSG_ANY,
			            dest, NULL, "%s",
				    "too frequent discovery requeust");
				sendpreq = 1;
				goto done;
			}
			hr->hr_lastdiscovery = ticks;
			if (hr->hr_preqretries >=
			    ieee80211_hwmp_maxpreq_retries) {
				IEEE80211_DISCARD_MAC(vap, IEEE80211_MSG_ANY,
			            dest, NULL, "%s",
				    "no valid path , max number of discovery");
				vap->iv_stats.is_mesh_fwd_nopath++;
				goto done;
			}
			rt->rt_flags = IEEE80211_MESHRT_FLAGS_DISCOVER;
			hr->hr_preqretries++;
			if (hr->hr_origseq == 0)
				hr->hr_origseq = ++hs->hs_seq;
			rt->rt_metric = IEEE80211_MESHLMETRIC_INITIALVAL;
			sendpreq = 1;
			IEEE80211_NOTE_MAC(vap, IEEE80211_MSG_HWMP, dest,
			    "start path discovery (src %s), target seq %u",
			    m == NULL ? "<none>" : ether_sprintf(
			    mtod(m, struct ether_header *)->ether_shost),
			    hr->hr_seq);
			/*
			 * Try to discover the path for this node.
			 * Group addressed PREQ Case A
			 */
			preq.preq_flags = 0;
			preq.preq_hopcount = 0;
			preq.preq_ttl = ms->ms_ttl;
			preq.preq_id = ++hs->hs_preqid;
			IEEE80211_ADDR_COPY(preq.preq_origaddr, vap->iv_myaddr);
			preq.preq_origseq = hr->hr_origseq;
			preq.preq_lifetime =
			    ticks_to_msecs(ieee80211_hwmp_pathtimeout);
			preq.preq_metric = IEEE80211_MESHLMETRIC_INITIALVAL;
			preq.preq_tcount = 1;
			IEEE80211_ADDR_COPY(PREQ_TADDR(0), dest);
			PREQ_TFLAGS(0) = 0;
			if (ieee80211_hwmp_targetonly)
				PREQ_TFLAGS(0) |= IEEE80211_MESHPREQ_TFLAGS_TO;
			PREQ_TFLAGS(0) |= IEEE80211_MESHPREQ_TFLAGS_USN;
			PREQ_TSEQ(0) = 0; /* RESERVED when USN flag is set */
			/* XXX check return value */
			hwmp_send_preq(vap, broadcastaddr, &preq,
			    &hr->hr_lastpreq, &ieee80211_hwmp_preqminint);
			callout_reset(&rt->rt_discovery,
			    ieee80211_hwmp_net_diameter_traversaltime * 2,
			    hwmp_rediscover_cb, rt);
		}
		if (rt->rt_flags & IEEE80211_MESHRT_FLAGS_VALID)
			ni = ieee80211_find_txnode(vap, rt->rt_nexthop);
	} else {
		ni = ieee80211_find_txnode(vap, dest);
		/* NB: if null then we leak mbuf */
		KASSERT(ni != NULL, ("leak mcast frame"));
		return ni;
	}
done:
	if (ni == NULL && m != NULL) {
		if (sendpreq) {
			struct ieee80211com *ic = vap->iv_ic;
			/*
			 * Queue packet for transmit when path discovery
			 * completes.  If discovery never completes the
			 * frame will be flushed by way of the aging timer.
			 */
			IEEE80211_NOTE_MAC(vap, IEEE80211_MSG_HWMP, dest,
			    "%s", "queue frame until path found");
			m->m_pkthdr.rcvif = (void *)(uintptr_t)
			    ieee80211_mac_hash(ic, dest);
			/* XXX age chosen randomly */
			ieee80211_ageq_append(&ic->ic_stageq, m,
			    IEEE80211_INACT_WAIT);
		} else {
			IEEE80211_DISCARD_MAC(vap, IEEE80211_MSG_HWMP,
			    dest, NULL, "%s", "no valid path to this node");
			m_freem(m);
		}
	}
	return ni;
}
#undef	PREQ_TFLAGS
#undef	PREQ_TADDR
#undef	PREQ_TSEQ

static int
hwmp_ioctl_get80211(struct ieee80211vap *vap, struct ieee80211req *ireq)
{
	struct ieee80211_hwmp_state *hs = vap->iv_hwmp;
	int error;

	if (vap->iv_opmode != IEEE80211_M_MBSS)
		return ENOSYS;
	error = 0;
	switch (ireq->i_type) {
	case IEEE80211_IOC_HWMP_ROOTMODE:
		ireq->i_val = hs->hs_rootmode;
		break;
	case IEEE80211_IOC_HWMP_MAXHOPS:
		ireq->i_val = hs->hs_maxhops;
		break;
	default:
		return ENOSYS;
	}
	return error;
}
IEEE80211_IOCTL_GET(hwmp, hwmp_ioctl_get80211);

static int
hwmp_ioctl_set80211(struct ieee80211vap *vap, struct ieee80211req *ireq)
{
	struct ieee80211_hwmp_state *hs = vap->iv_hwmp;
	int error;

	if (vap->iv_opmode != IEEE80211_M_MBSS)
		return ENOSYS;
	error = 0;
	switch (ireq->i_type) {
	case IEEE80211_IOC_HWMP_ROOTMODE:
		if (ireq->i_val < 0 || ireq->i_val > 3)
			return EINVAL;
		hs->hs_rootmode = ireq->i_val;
		hwmp_rootmode_setup(vap);
		break;
	case IEEE80211_IOC_HWMP_MAXHOPS:
		if (ireq->i_val <= 0 || ireq->i_val > 255)
			return EINVAL;
		hs->hs_maxhops = ireq->i_val;
		break;
	default:
		return ENOSYS;
	}
	return error;
}
IEEE80211_IOCTL_SET(hwmp, hwmp_ioctl_set80211);
