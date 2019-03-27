/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Sam Leffler, Errno Consulting
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
 * IEEE 802.11 age queue support.
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

/*
 * Initialize an ageq.
 */
void
ieee80211_ageq_init(struct ieee80211_ageq *aq, int maxlen, const char *name)
{
	memset(aq, 0, sizeof(*aq));
	aq->aq_maxlen = maxlen;
	IEEE80211_AGEQ_INIT(aq, name);		/* OS-dependent setup */
}

/*
 * Cleanup an ageq initialized with ieee80211_ageq_init.  Note
 * the queue is assumed empty; this can be done with ieee80211_ageq_drain.
 */
void
ieee80211_ageq_cleanup(struct ieee80211_ageq *aq)
{
	KASSERT(aq->aq_len == 0, ("%d frames on ageq", aq->aq_len));
	IEEE80211_AGEQ_DESTROY(aq);		/* OS-dependent cleanup */
}

/*
 * Free an mbuf according to ageq rules: if marked as holding
 * and 802.11 frame then also reclaim a node reference from
 * the packet header; this handles packets q'd in the tx path.
 */
static void
ageq_mfree(struct mbuf *m)
{
	if (m->m_flags & M_ENCAP) {
		struct ieee80211_node *ni = (void *) m->m_pkthdr.rcvif;
		ieee80211_free_node(ni);
	}
	m->m_nextpkt = NULL;
	m_freem(m);
}

/*
 * Free a list of mbufs using ageq rules (see above).
 */
void
ieee80211_ageq_mfree(struct mbuf *m)
{
	struct mbuf *next;

	for (; m != NULL; m = next) {
		next = m->m_nextpkt;
		ageq_mfree(m);
	}
}

/*
 * Append an mbuf to the ageq and mark it with the specified max age
 * If the frame is not removed before the age (in seconds) expires
 * then it is reclaimed (along with any node reference).
 */
int
ieee80211_ageq_append(struct ieee80211_ageq *aq, struct mbuf *m, int age)
{
	IEEE80211_AGEQ_LOCK(aq);
	if (__predict_true(aq->aq_len < aq->aq_maxlen)) {
		if (aq->aq_tail == NULL) {
			aq->aq_head = m;
		} else {
			aq->aq_tail->m_nextpkt = m;
			age -= M_AGE_GET(aq->aq_head);
		}
		KASSERT(age >= 0, ("age %d", age));
		M_AGE_SET(m, age);
		m->m_nextpkt = NULL;
		aq->aq_tail = m;
		aq->aq_len++;
		IEEE80211_AGEQ_UNLOCK(aq);
		return 0;
	} else {
		/*
		 * No space, drop and cleanup references.
		 */
		aq->aq_drops++;
		IEEE80211_AGEQ_UNLOCK(aq);
		/* XXX tail drop? */
		ageq_mfree(m);
		return ENOSPC;
	}
}

/*
 * Drain/reclaim all frames from an ageq.
 */
void
ieee80211_ageq_drain(struct ieee80211_ageq *aq)
{
	ieee80211_ageq_mfree(ieee80211_ageq_remove(aq, NULL));
}

/*
 * Drain/reclaim frames associated with a specific node from an ageq.
 */
void
ieee80211_ageq_drain_node(struct ieee80211_ageq *aq,
	struct ieee80211_node *ni)
{
	ieee80211_ageq_mfree(ieee80211_ageq_remove(aq, ni));
}

/*
 * Age frames on the age queue.  Ages are stored as time
 * deltas (in seconds) relative to the head so we can check
 * and/or adjust only the head of the list.  If a frame's age
 * exceeds the time quanta then remove it.  The list of removed
 * frames is returned to the caller joined by m_nextpkt.
 */
struct mbuf *
ieee80211_ageq_age(struct ieee80211_ageq *aq, int quanta)
{
	struct mbuf *head, **phead;
	struct mbuf *m;

	phead = &head;
	if (aq->aq_len != 0) {
		IEEE80211_AGEQ_LOCK(aq);
		while ((m = aq->aq_head) != NULL && M_AGE_GET(m) < quanta) {
			if ((aq->aq_head = m->m_nextpkt) == NULL)
				aq->aq_tail = NULL;
			KASSERT(aq->aq_len > 0, ("aq len %d", aq->aq_len));
			aq->aq_len--;
			/* add to private list for return */
			*phead = m;
			phead = &m->m_nextpkt;
		}
		if (m != NULL)
			M_AGE_SUB(m, quanta);
		IEEE80211_AGEQ_UNLOCK(aq);
	}
	*phead = NULL;
	return head;
}

/*
 * Remove all frames matching the specified node identifier
 * (NULL matches all).  Frames are returned as a list joined
 * by m_nextpkt.
 */
struct mbuf *
ieee80211_ageq_remove(struct ieee80211_ageq *aq,
	struct ieee80211_node *match)
{
	struct mbuf *m, **prev, *ohead;
	struct mbuf *head, **phead;

	IEEE80211_AGEQ_LOCK(aq);
	ohead = aq->aq_head;
	prev = &aq->aq_head;
	phead = &head;
	while ((m = *prev) != NULL) {
		if (match != NULL && m->m_pkthdr.rcvif != (void *) match) {
			prev = &m->m_nextpkt;
			continue;
		}
		/*
		 * Adjust q length.
		 */
		KASSERT(aq->aq_len > 0, ("aq len %d", aq->aq_len));
		aq->aq_len--;
		/*
		 * Remove from forward list; tail pointer is harder.
		 */
		if (aq->aq_tail == m) {
			KASSERT(m->m_nextpkt == NULL, ("not last"));
			if (aq->aq_head == m) {		/* list empty */
				KASSERT(aq->aq_len == 0,
				    ("not empty, len %d", aq->aq_len));
				aq->aq_tail = NULL;
			} else {			/* must be one before */
				aq->aq_tail = (struct mbuf *)((uintptr_t)prev -
				    offsetof(struct mbuf, m_nextpkt));
			}
		}
		*prev = m->m_nextpkt;

		/* add to private list for return */
		*phead = m;
		phead = &m->m_nextpkt;
	}
	if (head == ohead && aq->aq_head != NULL)	/* correct age */
		M_AGE_SET(aq->aq_head, M_AGE_GET(head));
	IEEE80211_AGEQ_UNLOCK(aq);

	*phead = NULL;
	return head;
}
