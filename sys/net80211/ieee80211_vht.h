/*-
 * Copyright (c) 2016 Adrian Chadd <adrian@FreeBSD.org>
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
#ifndef _NET80211_IEEE80211_VHT_H_
#define _NET80211_IEEE80211_VHT_H_

void	ieee80211_vht_attach(struct ieee80211com *);
void	ieee80211_vht_detach(struct ieee80211com *);
void	ieee80211_vht_vattach(struct ieee80211vap *);
void	ieee80211_vht_vdetach(struct ieee80211vap *);

void	ieee80211_vht_announce(struct ieee80211com *);

void	ieee80211_vht_node_init(struct ieee80211_node *);
void	ieee80211_vht_node_cleanup(struct ieee80211_node *);

void	ieee80211_parse_vhtopmode(struct ieee80211_node *, const uint8_t *);
void	ieee80211_parse_vhtcap(struct ieee80211_node *, const uint8_t *);

int	ieee80211_vht_updateparams(struct ieee80211_node *,
	    const uint8_t *, const uint8_t *);
void	ieee80211_setup_vht_rates(struct ieee80211_node *,
	    const uint8_t *, const uint8_t *);

void	ieee80211_vht_timeout(struct ieee80211com *ic);

void	ieee80211_vht_node_join(struct ieee80211_node *ni);
void	ieee80211_vht_node_leave(struct ieee80211_node *ni);

uint8_t *	ieee80211_add_vhtcap(uint8_t *frm, struct ieee80211_node *);
uint8_t *	ieee80211_add_vhtinfo(uint8_t *frm, struct ieee80211_node *);

void	ieee80211_vht_update_cap(struct ieee80211_node *,
	    const uint8_t *, const uint8_t *);

struct ieee80211_channel *
	ieee80211_vht_adjust_channel(struct ieee80211com *,
	    struct ieee80211_channel *, int);

void	ieee80211_vht_get_vhtcap_ie(struct ieee80211_node *ni,
	    struct ieee80211_ie_vhtcap *, int);
void	ieee80211_vht_get_vhtinfo_ie(struct ieee80211_node *ni,
	    struct ieee80211_ie_vht_operation *, int);

#endif	/* _NET80211_IEEE80211_VHT_H_ */
