/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007-2009 Sam Leffler, Errno Consulting
 * Copyright (c) 2007-2009 Intel Corporation
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
#ifndef _NET80211_IEEE80211_TDMA_H_
#define _NET80211_IEEE80211_TDMA_H_

/*
 * TDMA-mode implementation definitions.
 */

#define	TDMA_SUBTYPE_PARAM	0x01
#define	TDMA_VERSION_V2		2
#define	TDMA_VERSION		TDMA_VERSION_V2

/* NB: we only support 2 right now but protocol handles up to 8 */
#define	TDMA_MAXSLOTS		2	/* max slots/sta's */

#define	TDMA_PARAM_LEN_V2	sizeof(struct ieee80211_tdma_param)

/*
 * TDMA information element.
 */
struct ieee80211_tdma_param {
	u_int8_t	tdma_id;	/* IEEE80211_ELEMID_VENDOR */
	u_int8_t	tdma_len;
	u_int8_t	tdma_oui[3];	/* TDMA_OUI */
	u_int8_t	tdma_type;	/* TDMA_OUI_TYPE */
	u_int8_t	tdma_subtype;	/* TDMA_SUBTYPE_PARAM */
	u_int8_t	tdma_version;	/* spec revision */
	u_int8_t	tdma_slot;	/* station slot # [0..7] */
	u_int8_t	tdma_slotcnt;	/* bss slot count [1..8] */
	u_int16_t	tdma_slotlen;	/* bss slot len (100us) */
	u_int8_t	tdma_bintval;	/* beacon interval (superframes) */
	u_int8_t	tdma_inuse[1];	/* slot occupancy map */
	u_int8_t	tdma_pad[2];
	u_int8_t	tdma_tstamp[8];	/* timestamp from last beacon */
} __packed;

#ifdef _KERNEL
/*
 * Implementation state.
 */
struct ieee80211_tdma_state {
	u_int	tdma_slotlen;		/* bss slot length (us) */
	uint8_t	tdma_version;		/* protocol version to use */
	uint8_t	tdma_slotcnt;		/* bss slot count */
	uint8_t	tdma_bintval;		/* beacon interval (slots) */
	uint8_t	tdma_slot;		/* station slot # */
	uint8_t	tdma_inuse[1];		/* mask of slots in use */
	uint8_t	tdma_active[1];		/* mask of active slots */
	int	tdma_count;		/* active/inuse countdown */
	void	*tdma_peer;		/* peer station cookie */
	struct timeval tdma_lastprint;	/* time of last rate-limited printf */
	int	tdma_fails;		/* fail count for rate-limiting */

	/* parent method pointers */
	int	(*tdma_newstate)(struct ieee80211vap *, enum ieee80211_state,
		    int arg);
	void	(*tdma_recv_mgmt)(struct ieee80211_node *,
		    struct mbuf *, int,
		    const struct ieee80211_rx_stats *rxs, int, int);
	void	(*tdma_opdetach)(struct ieee80211vap *);
};
 
#define	TDMA_UPDATE_SLOT	0x0001	/* tdma_slot changed */
#define	TDMA_UPDATE_SLOTCNT	0x0002	/* tdma_slotcnt changed */
#define	TDMA_UPDATE_SLOTLEN	0x0004	/* tdma_slotlen changed */
#define	TDMA_UPDATE_BINTVAL	0x0008	/* tdma_bintval changed */

void	ieee80211_tdma_vattach(struct ieee80211vap *);

int	ieee80211_tdma_getslot(struct ieee80211vap *vap);
void	ieee80211_parse_tdma(struct ieee80211_node *ni, const uint8_t *ie);
uint8_t *ieee80211_add_tdma(uint8_t *frm, struct ieee80211vap *vap);
struct ieee80211_beacon_offsets;
void	ieee80211_tdma_update_beacon(struct ieee80211vap *vap,
	    struct ieee80211_beacon_offsets *bo);
#endif /* _KERNEL */
#endif /* !_NET80211_IEEE80211_TDMA_H_ */
