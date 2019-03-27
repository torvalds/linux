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
 *
 * $FreeBSD$
 */
#ifndef _NET80211_IEEE80211_DFS_H_
#define _NET80211_IEEE80211_DFS_H_

/*
 * 802.11h/DFS definitions.
 */

typedef enum {
	DFS_DBG_NONE		= 0,
	DFS_DBG_NONOL		= 1,
	DFS_DBG_NOCSANOL	= 2
} dfs_debug_t;

struct ieee80211_dfs_state {
	int		nol_event[IEEE80211_CHAN_MAX];
	struct callout	nol_timer;		/* NOL list processing */
	struct callout	cac_timer;		/* CAC timer */
	struct timeval	lastevent;		/* time of last radar event */
	int		cureps;			/* current events/second */
	const struct ieee80211_channel *lastchan;/* chan w/ last radar event */
	struct ieee80211_channel *newchan;	/* chan selected next */
};

void	ieee80211_dfs_attach(struct ieee80211com *);
void	ieee80211_dfs_detach(struct ieee80211com *);

void	ieee80211_dfs_reset(struct ieee80211com *);

void	ieee80211_dfs_cac_start(struct ieee80211vap *);
void	ieee80211_dfs_cac_stop(struct ieee80211vap *);
void	ieee80211_dfs_cac_clear(struct ieee80211com *,
		const struct ieee80211_channel *);

void	ieee80211_dfs_notify_radar(struct ieee80211com *,
		struct ieee80211_channel *);
struct ieee80211_channel *ieee80211_dfs_pickchannel(struct ieee80211com *);
#endif /* _NET80211_IEEE80211_DFS_H_ */
