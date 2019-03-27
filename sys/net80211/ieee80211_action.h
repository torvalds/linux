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
#ifndef _NET80211_IEEE80211_ACTION_H_
#define _NET80211_IEEE80211_ACTION_H_

/*
 * 802.11 send/recv action frame support.
 */

struct ieee80211_node;
struct ieee80211_frame;

typedef int ieee80211_send_action_func(struct ieee80211_node *,
    int, int, void *);
int	ieee80211_send_action_register(int cat, int act,
		ieee80211_send_action_func *f);
void	ieee80211_send_action_unregister(int cat, int act);
int	ieee80211_send_action(struct ieee80211_node *, int, int, void *);

typedef int ieee80211_recv_action_func(struct ieee80211_node *,
    const struct ieee80211_frame *, const uint8_t *, const uint8_t *);
int	ieee80211_recv_action_register(int cat, int act,
		ieee80211_recv_action_func *);
void	ieee80211_recv_action_unregister(int cat, int act);
int	ieee80211_recv_action(struct ieee80211_node *,
		const struct ieee80211_frame *,
		const uint8_t *, const uint8_t *);
#endif /* _NET80211_IEEE80211_ACTION_H_ */
