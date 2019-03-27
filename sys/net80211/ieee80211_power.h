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
 *
 * $FreeBSD$
 */
#ifndef _NET80211_IEEE80211_POWER_H_
#define _NET80211_IEEE80211_POWER_H_

struct ieee80211com;
struct ieee80211vap;
struct ieee80211_node;
struct mbuf;

/*
 * Power save packet queues.  There are two queues, one
 * for frames coming from the net80211 layer and the other
 * for frames that come from the driver. Frames from the
 * driver are expected to have M_ENCAP marked to indicate
 * they have already been encapsulated and are treated as
 * higher priority: they are sent first when flushing the
 * queue on a power save state change or in response to a
 * ps-poll frame.
 *
 * Note that frames sent from the high priority queue are
 * fed directly to the driver without going through
 * ieee80211_start again; drivers that send up encap'd
 * frames are required to handle them when they come back.
 */
struct ieee80211_psq {
	ieee80211_psq_lock_t psq_lock;
	int	psq_len;
	int	psq_maxlen;
	int	psq_drops;
	struct ieee80211_psq_head {
		struct mbuf *head;
		struct mbuf *tail;
		int len;
	} psq_head[2];			/* 2 priorities */
};

void	ieee80211_psq_init(struct ieee80211_psq *, const char *);
void	ieee80211_psq_cleanup(struct ieee80211_psq *);

void	ieee80211_power_attach(struct ieee80211com *);
void	ieee80211_power_detach(struct ieee80211com *);
void	ieee80211_power_vattach(struct ieee80211vap *);
void	ieee80211_power_vdetach(struct ieee80211vap *);
void	ieee80211_power_latevattach(struct ieee80211vap *);

struct mbuf *ieee80211_node_psq_dequeue(struct ieee80211_node *ni, int *qlen);
int	ieee80211_node_psq_drain(struct ieee80211_node *);
int	ieee80211_node_psq_age(struct ieee80211_node *);

/*
 * Don't call these directly from the stack; they are vap methods
 * that should be overridden.
 */
int	ieee80211_pwrsave(struct ieee80211_node *, struct mbuf *);
void	ieee80211_node_pwrsave(struct ieee80211_node *, int enable);
void	ieee80211_sta_pwrsave(struct ieee80211vap *, int enable);
void	ieee80211_sta_tim_notify(struct ieee80211vap *vap, int set);
void	ieee80211_sta_ps_timer_check(struct ieee80211vap *vap);

/* XXX what's this? */
void	ieee80211_power_poll(struct ieee80211com *);
#endif /* _NET80211_IEEE80211_POWER_H_ */
