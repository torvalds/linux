/*	$OpenBSD: ieee80211_proto.h,v 1.50 2025/06/14 08:46:34 jsg Exp $	*/
/*	$NetBSD: ieee80211_proto.h,v 1.3 2003/10/13 04:23:56 dyoung Exp $	*/

/*-
 * Copyright (c) 2001 Atsushi Onoe
 * Copyright (c) 2002, 2003 Sam Leffler, Errno Consulting
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
 * $FreeBSD: src/sys/net80211/ieee80211_proto.h,v 1.4 2003/08/19 22:17:03 sam Exp $
 */
#ifndef _NET80211_IEEE80211_PROTO_H_
#define _NET80211_IEEE80211_PROTO_H_

/*
 * 802.11 protocol implementation definitions.
 */

enum ieee80211_state {
	IEEE80211_S_INIT	= 0,	/* default state */
	IEEE80211_S_SCAN	= 1,	/* scanning */
	IEEE80211_S_AUTH	= 2,	/* try to authenticate */
	IEEE80211_S_ASSOC	= 3,	/* try to assoc */
	IEEE80211_S_RUN		= 4	/* associated */
};
#define	IEEE80211_S_MAX		(IEEE80211_S_RUN+1)

#define	IEEE80211_SEND_MGMT(_ic,_ni,_type,_arg) \
	((*(_ic)->ic_send_mgmt)(_ic, _ni, _type, _arg, 0))
/* shortcut */
#define IEEE80211_SEND_ACTION(_ic,_ni,_categ,_action,_arg) \
	((*(_ic)->ic_send_mgmt)(_ic, _ni, IEEE80211_FC0_SUBTYPE_ACTION, \
	    (_categ) << 16 | (_action), _arg))

extern	const char * const ieee80211_mgt_subtype_name[];
extern	const char * const ieee80211_state_name[IEEE80211_S_MAX];
extern	const char * const ieee80211_phymode_name[];

extern	void ieee80211_proto_attach(struct ifnet *);
extern	void ieee80211_proto_detach(struct ifnet *);

struct ieee80211_node;
struct ieee80211_rxinfo;
struct ieee80211_rsnparams;
extern	void ieee80211_rtm_80211info_task(void *);
extern	void ieee80211_set_link_state(struct ieee80211com *, int);
extern	u_int ieee80211_get_hdrlen(const struct ieee80211_frame *);
extern	int ieee80211_classify(struct ieee80211com *, struct mbuf *);
extern	void ieee80211_inputm(struct ifnet *, struct mbuf *,
		struct ieee80211_node *, struct ieee80211_rxinfo *,
		struct mbuf_list *);
extern	void ieee80211_input(struct ifnet *, struct mbuf *,
		struct ieee80211_node *, struct ieee80211_rxinfo *);
extern	int ieee80211_output(struct ifnet *, struct mbuf *, struct sockaddr *,
		struct rtentry *);
extern	void ieee80211_recv_mgmt(struct ieee80211com *, struct mbuf *,
		struct ieee80211_node *, struct ieee80211_rxinfo *, int);
extern	int ieee80211_send_mgmt(struct ieee80211com *, struct ieee80211_node *,
		int, int, int);
extern	void ieee80211_eapol_key_input(struct ieee80211com *, struct mbuf *,
		struct ieee80211_node *);
extern	void ieee80211_tx_compressed_bar(struct ieee80211com *,
		struct ieee80211_node *, int, uint16_t);
extern	struct mbuf *ieee80211_encap(struct ifnet *, struct mbuf *,
		struct ieee80211_node **);
extern	struct mbuf *ieee80211_get_rts(struct ieee80211com *,
		const struct ieee80211_frame *, u_int16_t);
extern	struct mbuf *ieee80211_get_cts_to_self(struct ieee80211com *,
		u_int16_t);
extern	struct mbuf *ieee80211_get_compressed_bar(struct ieee80211com *,
		struct ieee80211_node *, int, uint16_t);
extern	struct mbuf *ieee80211_beacon_alloc(struct ieee80211com *,
		struct ieee80211_node *);
extern int ieee80211_save_ie(const u_int8_t *, u_int8_t **);
extern	void ieee80211_eapol_timeout(void *);
extern	int ieee80211_send_4way_msg1(struct ieee80211com *,
		struct ieee80211_node *);
extern	int ieee80211_send_4way_msg2(struct ieee80211com *,
		struct ieee80211_node *, const u_int8_t *,
		const struct ieee80211_ptk *);
extern	int ieee80211_send_4way_msg3(struct ieee80211com *,
		struct ieee80211_node *);
extern	int ieee80211_send_4way_msg4(struct ieee80211com *,
		struct ieee80211_node *);
extern	int ieee80211_send_group_msg1(struct ieee80211com *,
		struct ieee80211_node *);
extern	int ieee80211_send_group_msg2(struct ieee80211com *,
		struct ieee80211_node *, const struct ieee80211_key *);
extern	int ieee80211_send_eapol_key_req(struct ieee80211com *,
		struct ieee80211_node *, u_int16_t, u_int64_t);
extern	int ieee80211_pwrsave(struct ieee80211com *, struct mbuf *,
		struct ieee80211_node *);
#define	ieee80211_new_state(_ic, _nstate, _arg) \
	(((_ic)->ic_newstate)((_ic), (_nstate), (_arg)))
extern	enum ieee80211_edca_ac ieee80211_up_to_ac(struct ieee80211com *, int);
extern	u_int8_t *ieee80211_add_capinfo(u_int8_t *, struct ieee80211com *,
		const struct ieee80211_node *);
extern	u_int8_t *ieee80211_add_ssid(u_int8_t *, const u_int8_t *, u_int);
extern	u_int8_t *ieee80211_add_rates(u_int8_t *,
		const struct ieee80211_rateset *);
extern	u_int8_t *ieee80211_add_ds_params(u_int8_t *, struct ieee80211com *,
		const struct ieee80211_node *);
extern	u_int8_t *ieee80211_add_tim(u_int8_t *, struct ieee80211com *);
extern	u_int8_t *ieee80211_add_ibss_params(u_int8_t *,
		const struct ieee80211_node *);
extern	u_int8_t *ieee80211_add_edca_params(u_int8_t *, struct ieee80211com *);
extern	u_int8_t *ieee80211_add_erp(u_int8_t *, struct ieee80211com *);
extern	u_int8_t *ieee80211_add_qos_capability(u_int8_t *,
		struct ieee80211com *);
extern	u_int8_t *ieee80211_add_rsn(u_int8_t *, struct ieee80211com *,
		const struct ieee80211_node *);
extern	u_int8_t *ieee80211_add_wpa(u_int8_t *, struct ieee80211com *,
		const struct ieee80211_node *);
extern	u_int8_t *ieee80211_add_xrates(u_int8_t *,
		const struct ieee80211_rateset *);
extern	u_int8_t *ieee80211_add_htcaps(u_int8_t *, struct ieee80211com *);
extern	u_int8_t *ieee80211_add_htop(u_int8_t *, struct ieee80211com *);
extern	u_int8_t *ieee80211_add_vhtcaps(u_int8_t *, struct ieee80211com *);
extern	u_int8_t *ieee80211_add_tie(u_int8_t *, u_int8_t, u_int32_t);

extern	int ieee80211_parse_rsn(struct ieee80211com *, const u_int8_t *,
		struct ieee80211_rsnparams *);
extern	int ieee80211_parse_wpa(struct ieee80211com *, const u_int8_t *,
		struct ieee80211_rsnparams *);
extern	void ieee80211_print_essid(const u_int8_t *, int);
#ifdef IEEE80211_DEBUG
extern	void ieee80211_dump_pkt(const u_int8_t *, int, int, int);
#endif
extern	int ieee80211_ibss_merge(struct ieee80211com *,
		struct ieee80211_node *, u_int64_t);
extern	void ieee80211_reset_erp(struct ieee80211com *);
extern	void ieee80211_set_shortslottime(struct ieee80211com *, int);
extern	void ieee80211_auth_open_confirm(struct ieee80211com *,
	    struct ieee80211_node *, uint16_t);
extern	void ieee80211_auth_open(struct ieee80211com *,
	    const struct ieee80211_frame *, struct ieee80211_node *,
	    struct ieee80211_rxinfo *rs, u_int16_t, u_int16_t);
extern	void ieee80211_stop_ampdu_tx(struct ieee80211com *,
	    struct ieee80211_node *, int);
extern	void ieee80211_gtk_rekey_timeout(void *);
extern	int ieee80211_keyrun(struct ieee80211com *, u_int8_t *);
extern	void ieee80211_setkeys(struct ieee80211com *);
extern	void ieee80211_setkeysdone(struct ieee80211com *);
extern	void ieee80211_sa_query_timeout(void *);
extern	void ieee80211_sa_query_request(struct ieee80211com *,
	    struct ieee80211_node *);
extern	void ieee80211_ht_negotiate(struct ieee80211com *,
    struct ieee80211_node *);
extern	void ieee80211_vht_negotiate(struct ieee80211com *,
    struct ieee80211_node *);
extern	void ieee80211_tx_ba_timeout(void *);
extern	void ieee80211_rx_ba_timeout(void *);
extern	int ieee80211_addba_request(struct ieee80211com *,
	    struct ieee80211_node *,  u_int16_t, u_int8_t);
extern	void ieee80211_delba_request(struct ieee80211com *,
	    struct ieee80211_node *, u_int16_t, u_int8_t, u_int8_t);
extern	void ieee80211_addba_req_accept(struct ieee80211com *,
	    struct ieee80211_node *, uint8_t);
extern	void ieee80211_addba_req_refuse(struct ieee80211com *,
	    struct ieee80211_node *, uint8_t);
extern	void ieee80211_addba_resp_accept(struct ieee80211com *,
	    struct ieee80211_node *, uint8_t);
extern	void ieee80211_addba_resp_refuse(struct ieee80211com *,
	    struct ieee80211_node *, uint8_t, uint16_t);
extern	void ieee80211_output_ba_move_window(struct ieee80211com *,
	    struct ieee80211_node *, uint8_t, uint16_t);

#endif /* _NET80211_IEEE80211_PROTO_H_ */
