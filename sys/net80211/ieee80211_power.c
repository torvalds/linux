/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002-2008 Sam Leffler, Errno Consulting
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
__FBSDID("$FreeBSD$");

/*
 * IEEE 802.11 power save support.
 */
#include "opt_wlan.h"

#include <sys/param.h>
#include <sys/systm.h> 
#include <sys/kernel.h>
#include <sys/malloc.h>
 
#include <sys/socket.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_media.h>
#include <net/ethernet.h>

#include <net80211/ieee80211_var.h>

#include <net/bpf.h>

static void ieee80211_update_ps(struct ieee80211vap *, int);
static int ieee80211_set_tim(struct ieee80211_node *, int);

static MALLOC_DEFINE(M_80211_POWER, "80211power", "802.11 power save state");

void
ieee80211_power_attach(struct ieee80211com *ic)
{
}

void
ieee80211_power_detach(struct ieee80211com *ic)
{
}

void
ieee80211_power_vattach(struct ieee80211vap *vap)
{
	if (vap->iv_opmode == IEEE80211_M_HOSTAP ||
	    vap->iv_opmode == IEEE80211_M_IBSS) {
		/* NB: driver should override */
		vap->iv_update_ps = ieee80211_update_ps;
		vap->iv_set_tim = ieee80211_set_tim;
	}
	vap->iv_node_ps = ieee80211_node_pwrsave;
	vap->iv_sta_ps = ieee80211_sta_pwrsave;
}

void
ieee80211_power_latevattach(struct ieee80211vap *vap)
{
	/*
	 * Allocate these only if needed.  Beware that we
	 * know adhoc mode doesn't support ATIM yet...
	 */
	if (vap->iv_opmode == IEEE80211_M_HOSTAP) {
		vap->iv_tim_len = howmany(vap->iv_max_aid,8) * sizeof(uint8_t);
		vap->iv_tim_bitmap = (uint8_t *) IEEE80211_MALLOC(vap->iv_tim_len,
			M_80211_POWER,
			IEEE80211_M_NOWAIT | IEEE80211_M_ZERO);
		if (vap->iv_tim_bitmap == NULL) {
			printf("%s: no memory for TIM bitmap!\n", __func__);
			/* XXX good enough to keep from crashing? */
			vap->iv_tim_len = 0;
		}
	}
}

void
ieee80211_power_vdetach(struct ieee80211vap *vap)
{
	if (vap->iv_tim_bitmap != NULL) {
		IEEE80211_FREE(vap->iv_tim_bitmap, M_80211_POWER);
		vap->iv_tim_bitmap = NULL;
	}
}

void
ieee80211_psq_init(struct ieee80211_psq *psq, const char *name)
{
	memset(psq, 0, sizeof(*psq));
	psq->psq_maxlen = IEEE80211_PS_MAX_QUEUE;
	IEEE80211_PSQ_INIT(psq, name);		/* OS-dependent setup */
}

void
ieee80211_psq_cleanup(struct ieee80211_psq *psq)
{
#if 0
	psq_drain(psq);				/* XXX should not be needed? */
#else
	KASSERT(psq->psq_len == 0, ("%d frames on ps q", psq->psq_len));
#endif
	IEEE80211_PSQ_DESTROY(psq);		/* OS-dependent cleanup */
}

/*
 * Return the highest priority frame in the ps queue.
 */
struct mbuf *
ieee80211_node_psq_dequeue(struct ieee80211_node *ni, int *qlen)
{
	struct ieee80211_psq *psq = &ni->ni_psq;
	struct ieee80211_psq_head *qhead;
	struct mbuf *m;

	IEEE80211_PSQ_LOCK(psq);
	qhead = &psq->psq_head[0];
again:
	if ((m = qhead->head) != NULL) {
		if ((qhead->head = m->m_nextpkt) == NULL)
			qhead->tail = NULL;
		KASSERT(qhead->len > 0, ("qhead len %d", qhead->len));
		qhead->len--;
		KASSERT(psq->psq_len > 0, ("psq len %d", psq->psq_len));
		psq->psq_len--;
		m->m_nextpkt = NULL;
	}
	if (m == NULL && qhead == &psq->psq_head[0]) {
		/* Algol-68 style for loop */
		qhead = &psq->psq_head[1];
		goto again;
	}
	if (qlen != NULL)
		*qlen = psq->psq_len;
	IEEE80211_PSQ_UNLOCK(psq);
	return m;
}

/*
 * Reclaim an mbuf from the ps q.  If marked with M_ENCAP
 * we assume there is a node reference that must be relcaimed.
 */
static void
psq_mfree(struct mbuf *m)
{
	if (m->m_flags & M_ENCAP) {
		struct ieee80211_node *ni = (void *) m->m_pkthdr.rcvif;
		ieee80211_free_node(ni);
	}
	m->m_nextpkt = NULL;
	m_freem(m);
}

/*
 * Clear any frames queued in the power save queue.
 * The number of frames that were present is returned.
 */
static int
psq_drain(struct ieee80211_psq *psq)
{
	struct ieee80211_psq_head *qhead;
	struct mbuf *m;
	int qlen;

	IEEE80211_PSQ_LOCK(psq);
	qlen = psq->psq_len;
	qhead = &psq->psq_head[0];
again:
	while ((m = qhead->head) != NULL) {
		qhead->head = m->m_nextpkt;
		psq_mfree(m);
	}
	qhead->tail = NULL;
	qhead->len = 0;
	if (qhead == &psq->psq_head[0]) {	/* Algol-68 style for loop */
		qhead = &psq->psq_head[1];
		goto again;
	}
	psq->psq_len = 0;
	IEEE80211_PSQ_UNLOCK(psq);

	return qlen;
}

/*
 * Clear any frames queued in the power save queue.
 * The number of frames that were present is returned.
 */
int
ieee80211_node_psq_drain(struct ieee80211_node *ni)
{
	return psq_drain(&ni->ni_psq);
}

/*
 * Age frames on the power save queue. The aging interval is
 * 4 times the listen interval specified by the station.  This
 * number is factored into the age calculations when the frame
 * is placed on the queue.  We store ages as time differences
 * so we can check and/or adjust only the head of the list.
 * If a frame's age exceeds the threshold then discard it.
 * The number of frames discarded is returned so the caller
 * can check if it needs to adjust the tim.
 */
int
ieee80211_node_psq_age(struct ieee80211_node *ni)
{
	struct ieee80211_psq *psq = &ni->ni_psq;
	int discard = 0;

	if (psq->psq_len != 0) {
#ifdef IEEE80211_DEBUG
		struct ieee80211vap *vap = ni->ni_vap;
#endif
		struct ieee80211_psq_head *qhead;
		struct mbuf *m;

		IEEE80211_PSQ_LOCK(psq);
		qhead = &psq->psq_head[0];
	again:
		while ((m = qhead->head) != NULL &&
		    M_AGE_GET(m) < IEEE80211_INACT_WAIT) {
			IEEE80211_NOTE(vap, IEEE80211_MSG_POWER, ni,
			     "discard frame, age %u", M_AGE_GET(m));
			if ((qhead->head = m->m_nextpkt) == NULL)
				qhead->tail = NULL;
			KASSERT(qhead->len > 0, ("qhead len %d", qhead->len));
			qhead->len--;
			KASSERT(psq->psq_len > 0, ("psq len %d", psq->psq_len));
			psq->psq_len--;
			psq_mfree(m);
			discard++;
		}
		if (qhead == &psq->psq_head[0]) { /* Algol-68 style for loop */
			qhead = &psq->psq_head[1];
			goto again;
		}
		if (m != NULL)
			M_AGE_SUB(m, IEEE80211_INACT_WAIT);
		IEEE80211_PSQ_UNLOCK(psq);

		IEEE80211_NOTE(vap, IEEE80211_MSG_POWER, ni,
		    "discard %u frames for age", discard);
		IEEE80211_NODE_STAT_ADD(ni, ps_discard, discard);
	}
	return discard;
}

/*
 * Handle a change in the PS station occupancy.
 */
static void
ieee80211_update_ps(struct ieee80211vap *vap, int nsta)
{

	KASSERT(vap->iv_opmode == IEEE80211_M_HOSTAP ||
		vap->iv_opmode == IEEE80211_M_IBSS,
		("operating mode %u", vap->iv_opmode));
}

/*
 * Indicate whether there are frames queued for a station in power-save mode.
 */
static int
ieee80211_set_tim(struct ieee80211_node *ni, int set)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211com *ic = ni->ni_ic;
	uint16_t aid;
	int changed;

	KASSERT(vap->iv_opmode == IEEE80211_M_HOSTAP ||
		vap->iv_opmode == IEEE80211_M_IBSS,
		("operating mode %u", vap->iv_opmode));

	aid = IEEE80211_AID(ni->ni_associd);
	KASSERT(aid < vap->iv_max_aid,
		("bogus aid %u, max %u", aid, vap->iv_max_aid));

	IEEE80211_LOCK(ic);
	changed = (set != (isset(vap->iv_tim_bitmap, aid) != 0));
	if (changed) {
		if (set) {
			setbit(vap->iv_tim_bitmap, aid);
			vap->iv_ps_pending++;
		} else {
			clrbit(vap->iv_tim_bitmap, aid);
			vap->iv_ps_pending--;
		}
		/* NB: we know vap is in RUN state so no need to check */
		vap->iv_update_beacon(vap, IEEE80211_BEACON_TIM);
	}
	IEEE80211_UNLOCK(ic);

	return changed;
}

/*
 * Save an outbound packet for a node in power-save sleep state.
 * The new packet is placed on the node's saved queue, and the TIM
 * is changed, if necessary.
 */
int
ieee80211_pwrsave(struct ieee80211_node *ni, struct mbuf *m)
{
	struct ieee80211_psq *psq = &ni->ni_psq;
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211com *ic = ni->ni_ic;
	struct ieee80211_psq_head *qhead;
	int qlen, age;

	IEEE80211_PSQ_LOCK(psq);
	if (psq->psq_len >= psq->psq_maxlen) {
		psq->psq_drops++;
		IEEE80211_PSQ_UNLOCK(psq);
		IEEE80211_NOTE(vap, IEEE80211_MSG_ANY, ni,
		    "pwr save q overflow, drops %d (size %d)",
		    psq->psq_drops, psq->psq_len);
#ifdef IEEE80211_DEBUG
		if (ieee80211_msg_dumppkts(vap))
			ieee80211_dump_pkt(ni->ni_ic, mtod(m, caddr_t),
			    m->m_len, -1, -1);
#endif
		psq_mfree(m);
		return ENOSPC;
	}
	/*
	 * Tag the frame with it's expiry time and insert it in
	 * the appropriate queue.  The aging interval is 4 times
	 * the listen interval specified by the station. Frames
	 * that sit around too long are reclaimed using this
	 * information.
	 */
	/* TU -> secs.  XXX handle overflow? */
	age = IEEE80211_TU_TO_MS((ni->ni_intval * ic->ic_bintval) << 2) / 1000;
	/*
	 * Encapsulated frames go on the high priority queue,
	 * other stuff goes on the low priority queue.  We use
	 * this to order frames returned out of the driver
	 * ahead of frames we collect in ieee80211_start.
	 */
	if (m->m_flags & M_ENCAP)
		qhead = &psq->psq_head[0];
	else
		qhead = &psq->psq_head[1];
	if (qhead->tail == NULL) {
		struct mbuf *mh;

		qhead->head = m;
		/*
		 * Take care to adjust age when inserting the first
		 * frame of a queue and the other queue already has
		 * frames.  We need to preserve the age difference
		 * relationship so ieee80211_node_psq_age works.
		 */
		if (qhead == &psq->psq_head[1]) {
			mh = psq->psq_head[0].head;
			if (mh != NULL)
				age-= M_AGE_GET(mh);
		} else {
			mh = psq->psq_head[1].head;
			if (mh != NULL) {
				int nage = M_AGE_GET(mh) - age;
				/* XXX is clamping to zero good 'nuf? */
				M_AGE_SET(mh, nage < 0 ? 0 : nage);
			}
		}
	} else {
		qhead->tail->m_nextpkt = m;
		age -= M_AGE_GET(qhead->head);
	}
	KASSERT(age >= 0, ("age %d", age));
	M_AGE_SET(m, age);
	m->m_nextpkt = NULL;
	qhead->tail = m;
	qhead->len++;
	qlen = ++(psq->psq_len);
	IEEE80211_PSQ_UNLOCK(psq);

	IEEE80211_NOTE(vap, IEEE80211_MSG_POWER, ni,
	    "save frame with age %d, %u now queued", age, qlen);

	if (qlen == 1 && vap->iv_set_tim != NULL)
		vap->iv_set_tim(ni, 1);

	return 0;
}

/*
 * Move frames from the ps q to the vap's send queue
 * and/or the driver's send queue; and kick the start
 * method for each, as appropriate.  Note we're careful
 * to preserve packet ordering here.
 */
static void
pwrsave_flushq(struct ieee80211_node *ni)
{
	struct ieee80211_psq *psq = &ni->ni_psq;
	struct ieee80211com *ic = ni->ni_ic;
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211_psq_head *qhead;
	struct mbuf *parent_q = NULL, *ifp_q = NULL;
	struct mbuf *m;

	IEEE80211_NOTE(vap, IEEE80211_MSG_POWER, ni,
	    "flush ps queue, %u packets queued", psq->psq_len);

	IEEE80211_PSQ_LOCK(psq);
	qhead = &psq->psq_head[0];	/* 802.11 frames */
	if (qhead->head != NULL) {
		/* XXX could dispatch through vap and check M_ENCAP */
		/* XXX need different driver interface */
		/* XXX bypasses q max and OACTIVE */
		parent_q = qhead->head;
		qhead->head = qhead->tail = NULL;
		qhead->len = 0;
	}

	qhead = &psq->psq_head[1];	/* 802.3 frames */
	if (qhead->head != NULL) {
		/* XXX need different driver interface */
		/* XXX bypasses q max and OACTIVE */
		ifp_q = qhead->head;
		qhead->head = qhead->tail = NULL;
		qhead->len = 0;
	}
	psq->psq_len = 0;
	IEEE80211_PSQ_UNLOCK(psq);

	/* NB: do this outside the psq lock */
	/* XXX packets might get reordered if parent is OACTIVE */
	/* parent frames, should be encapsulated */
	while (parent_q != NULL) {
		m = parent_q;
		parent_q = m->m_nextpkt;
		m->m_nextpkt = NULL;
		/* must be encapsulated */
		KASSERT((m->m_flags & M_ENCAP),
		    ("%s: parentq with non-M_ENCAP frame!\n",
		    __func__));
		(void) ieee80211_parent_xmitpkt(ic, m);
	}

	/* VAP frames, aren't encapsulated */
	while (ifp_q != NULL) {
		m = ifp_q;
		ifp_q = m->m_nextpkt;
		m->m_nextpkt = NULL;
		KASSERT((!(m->m_flags & M_ENCAP)),
		    ("%s: vapq with M_ENCAP frame!\n", __func__));
		(void) ieee80211_vap_xmitpkt(vap, m);
	}
}

/*
 * Handle station power-save state change.
 */
void
ieee80211_node_pwrsave(struct ieee80211_node *ni, int enable)
{
	struct ieee80211vap *vap = ni->ni_vap;
	int update;

	update = 0;
	if (enable) {
		if ((ni->ni_flags & IEEE80211_NODE_PWR_MGT) == 0) {
			vap->iv_ps_sta++;
			update = 1;
		}
		ni->ni_flags |= IEEE80211_NODE_PWR_MGT;
		IEEE80211_NOTE(vap, IEEE80211_MSG_POWER, ni,
		    "power save mode on, %u sta's in ps mode", vap->iv_ps_sta);

		if (update)
			vap->iv_update_ps(vap, vap->iv_ps_sta);
	} else {
		if (ni->ni_flags & IEEE80211_NODE_PWR_MGT) {
			vap->iv_ps_sta--;
			update = 1;
		}
		ni->ni_flags &= ~IEEE80211_NODE_PWR_MGT;
		IEEE80211_NOTE(vap, IEEE80211_MSG_POWER, ni,
		    "power save mode off, %u sta's in ps mode", vap->iv_ps_sta);

		/* NB: order here is intentional so TIM is clear before flush */
		if (vap->iv_set_tim != NULL)
			vap->iv_set_tim(ni, 0);
		if (update) {
			/* NB if no sta's in ps, driver should flush mc q */
			vap->iv_update_ps(vap, vap->iv_ps_sta);
		}
		if (ni->ni_psq.psq_len != 0)
			pwrsave_flushq(ni);
	}
}

/*
 * Handle power-save state change in station mode.
 */
void
ieee80211_sta_pwrsave(struct ieee80211vap *vap, int enable)
{
	struct ieee80211_node *ni = vap->iv_bss;

	if (!((enable != 0) ^ ((ni->ni_flags & IEEE80211_NODE_PWR_MGT) != 0)))
		return;

	IEEE80211_NOTE(vap, IEEE80211_MSG_POWER, ni,
	    "sta power save mode %s", enable ? "on" : "off");
	if (!enable) {
		ni->ni_flags &= ~IEEE80211_NODE_PWR_MGT;
		ieee80211_send_nulldata(ieee80211_ref_node(ni));
		/*
		 * Flush any queued frames; we can do this immediately
		 * because we know they'll be queued behind the null
		 * data frame we send the ap.
		 * XXX can we use a data frame to take us out of ps?
		 */
		if (ni->ni_psq.psq_len != 0)
			pwrsave_flushq(ni);
	} else {
		ni->ni_flags |= IEEE80211_NODE_PWR_MGT;
		ieee80211_send_nulldata(ieee80211_ref_node(ni));
	}
}

/*
 * Handle being notified that we have data available for us in a TIM/ATIM.
 *
 * This may schedule a transition from _SLEEP -> _RUN if it's appropriate.
 *
 * In STA mode, we may have put to sleep during scan and need to be dragged
 * back out of powersave mode.
 */
void
ieee80211_sta_tim_notify(struct ieee80211vap *vap, int set)
{
	struct ieee80211com *ic = vap->iv_ic;

	/*
	 * Schedule the driver state change.  It'll happen at some point soon.
	 * Since the hardware shouldn't know that we're running just yet
	 * (and thus tell the peer that we're awake before we actually wake
	 * up said hardware), we leave the actual node state transition
	 * up to the transition to RUN.
	 *
	 * XXX TODO: verify that the transition to RUN will wake up the
	 * BSS node!
	 */
	IEEE80211_LOCK(vap->iv_ic);
	if (set == 1 && vap->iv_state == IEEE80211_S_SLEEP) {
		ieee80211_new_state_locked(vap, IEEE80211_S_RUN, 0);
		IEEE80211_DPRINTF(vap, IEEE80211_MSG_POWER,
		    "%s: TIM=%d; wakeup\n", __func__, set);
	} else if ((set == 1) && (ic->ic_flags_ext & IEEE80211_FEXT_BGSCAN)) {
		/*
		 * XXX only do this if we're in RUN state?
		 */
		IEEE80211_DPRINTF(vap, IEEE80211_MSG_POWER,
		    "%s: wake up from bgscan vap sleep\n",
		    __func__);
		/*
		 * We may be in BGSCAN mode - this means the VAP is is in STA
		 * mode powersave.  If it is, we need to wake it up so we
		 * can process outbound traffic.
		 */
		vap->iv_sta_ps(vap, 0);
	}
	IEEE80211_UNLOCK(vap->iv_ic);
}

/*
 * Timer check on whether the VAP has had any transmit activity.
 *
 * This may schedule a transition from _RUN -> _SLEEP if it's appropriate.
 */
void
ieee80211_sta_ps_timer_check(struct ieee80211vap *vap)
{
	struct ieee80211com *ic = vap->iv_ic;

	/* XXX lock assert */

	/* For no, only do this in STA mode */
	if (! (vap->iv_caps & IEEE80211_C_SWSLEEP))
		goto out;

	if (vap->iv_opmode != IEEE80211_M_STA)
		goto out;

	/* If we're not at run state, bail */
	if (vap->iv_state != IEEE80211_S_RUN)
		goto out;

	IEEE80211_DPRINTF(vap, IEEE80211_MSG_POWER,
	    "%s: lastdata=%llu, ticks=%llu\n",
	    __func__, (unsigned long long) ic->ic_lastdata,
	    (unsigned long long) ticks);

	/* If powersave is disabled on the VAP, don't bother */
	if (! (vap->iv_flags & IEEE80211_F_PMGTON))
		goto out;

	/* If we've done any data within our idle interval, bail */
	/* XXX hard-coded to one second for now, ew! */
	if (ieee80211_time_after(ic->ic_lastdata + 500, ticks))
		goto out;

	/*
	 * Signify we're going into power save and transition the
	 * node to powersave.
	 */
	if ((vap->iv_bss->ni_flags & IEEE80211_NODE_PWR_MGT) == 0)
		vap->iv_sta_ps(vap, 1);

	/*
	 * XXX The driver has to handle the fact that we're going
	 * to sleep but frames may still be transmitted;
	 * hopefully it and/or us will do the right thing and mark any
	 * transmitted frames with PWRMGT set to 1.
	 */
	ieee80211_new_state_locked(vap, IEEE80211_S_SLEEP, 0);

	IEEE80211_DPRINTF(vap, IEEE80211_MSG_POWER,
	    "%s: time delta=%d msec\n", __func__,
	    (int) ticks_to_msecs(ticks - ic->ic_lastdata));

out:
	return;
}
