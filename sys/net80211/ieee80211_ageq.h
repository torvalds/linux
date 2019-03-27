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
 *
 * $FreeBSD$
 */
#ifndef _NET80211_IEEE80211_STAGEQ_H_
#define _NET80211_IEEE80211_STAGEQ_H_

struct ieee80211_node;
struct mbuf;

struct ieee80211_ageq {
	ieee80211_ageq_lock_t	aq_lock;
	int			aq_len;		/* # items on queue */
	int			aq_maxlen;	/* max queue length */
	int			aq_drops;	/* frames dropped */
	struct mbuf		*aq_head;	/* frames linked w/ m_nextpkt */
	struct mbuf		*aq_tail;	/* last frame in queue */
};

void	ieee80211_ageq_init(struct ieee80211_ageq *, int maxlen,
	    const char *name);
void	ieee80211_ageq_cleanup(struct ieee80211_ageq *);
void	ieee80211_ageq_mfree(struct mbuf *);
int	ieee80211_ageq_append(struct ieee80211_ageq *, struct mbuf *,
	    int age);
void	ieee80211_ageq_drain(struct ieee80211_ageq *);
void	ieee80211_ageq_drain_node(struct ieee80211_ageq *,
	    struct ieee80211_node *);
struct mbuf *ieee80211_ageq_age(struct ieee80211_ageq *, int quanta);
struct mbuf *ieee80211_ageq_remove(struct ieee80211_ageq *,
	    struct ieee80211_node *match);
#endif /* _NET80211_IEEE80211_STAGEQ_H_ */
