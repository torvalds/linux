/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Atsushi Onoe
 * Copyright (c) 2002-2009 Sam Leffler, Errno Consulting
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
	IEEE80211_S_CAC		= 4,	/* doing channel availability check */
	IEEE80211_S_RUN		= 5,	/* operational (e.g. associated) */
	IEEE80211_S_CSA		= 6,	/* channel switch announce pending */
	IEEE80211_S_SLEEP	= 7,	/* power save */
};
#define	IEEE80211_S_MAX		(IEEE80211_S_SLEEP+1)

#define	IEEE80211_SEND_MGMT(_ni,_type,_arg) \
	((*(_ni)->ni_ic->ic_send_mgmt)(_ni, _type, _arg))

extern	const char *mgt_subtype_name[];
extern	const char *ctl_subtype_name[];
extern	const char *ieee80211_phymode_name[IEEE80211_MODE_MAX];
extern	const int ieee80211_opcap[IEEE80211_OPMODE_MAX];

static __inline const char *
ieee80211_mgt_subtype_name(uint8_t subtype)
{
	return mgt_subtype_name[(subtype & IEEE80211_FC0_SUBTYPE_MASK) >>
		   IEEE80211_FC0_SUBTYPE_SHIFT];
}

static __inline const char *
ieee80211_ctl_subtype_name(uint8_t subtype)
{
	return ctl_subtype_name[(subtype & IEEE80211_FC0_SUBTYPE_MASK) >>
		   IEEE80211_FC0_SUBTYPE_SHIFT];
}

const char *ieee80211_reason_to_string(uint16_t);

void	ieee80211_proto_attach(struct ieee80211com *);
void	ieee80211_proto_detach(struct ieee80211com *);
void	ieee80211_proto_vattach(struct ieee80211vap *);
void	ieee80211_proto_vdetach(struct ieee80211vap *);

void	ieee80211_promisc(struct ieee80211vap *, bool);
void	ieee80211_allmulti(struct ieee80211vap *, bool);
void	ieee80211_syncflag(struct ieee80211vap *, int flag);
void	ieee80211_syncflag_ht(struct ieee80211vap *, int flag);
void	ieee80211_syncflag_vht(struct ieee80211vap *, int flag);
void	ieee80211_syncflag_ext(struct ieee80211vap *, int flag);

#define	ieee80211_input(ni, m, rssi, nf) \
	((ni)->ni_vap->iv_input(ni, m, NULL, rssi, nf))
int	ieee80211_input_all(struct ieee80211com *, struct mbuf *, int, int);

int	ieee80211_input_mimo(struct ieee80211_node *, struct mbuf *);
int	ieee80211_input_mimo_all(struct ieee80211com *, struct mbuf *);

struct ieee80211_bpf_params;
int	ieee80211_mgmt_output(struct ieee80211_node *, struct mbuf *, int,
		struct ieee80211_bpf_params *);
int	ieee80211_raw_xmit(struct ieee80211_node *, struct mbuf *,
		const struct ieee80211_bpf_params *);
int	ieee80211_output(struct ifnet *, struct mbuf *,
               const struct sockaddr *, struct route *ro);
int	ieee80211_vap_pkt_send_dest(struct ieee80211vap *, struct mbuf *,
		struct ieee80211_node *);
int	ieee80211_raw_output(struct ieee80211vap *, struct ieee80211_node *,
		struct mbuf *, const struct ieee80211_bpf_params *);
void	ieee80211_send_setup(struct ieee80211_node *, struct mbuf *, int, int,
        const uint8_t [IEEE80211_ADDR_LEN], const uint8_t [IEEE80211_ADDR_LEN],
        const uint8_t [IEEE80211_ADDR_LEN]);
int	ieee80211_vap_transmit(struct ifnet *ifp, struct mbuf *m);
void	ieee80211_vap_qflush(struct ifnet *ifp);
int	ieee80211_send_nulldata(struct ieee80211_node *);
int	ieee80211_classify(struct ieee80211_node *, struct mbuf *m);
struct mbuf *ieee80211_mbuf_adjust(struct ieee80211vap *, int,
		struct ieee80211_key *, struct mbuf *);
struct mbuf *ieee80211_encap(struct ieee80211vap *, struct ieee80211_node *,
		struct mbuf *);
void	ieee80211_free_mbuf(struct mbuf *);
int	ieee80211_send_mgmt(struct ieee80211_node *, int, int);
struct ieee80211_appie;
int	ieee80211_send_probereq(struct ieee80211_node *ni,
		const uint8_t sa[IEEE80211_ADDR_LEN],
		const uint8_t da[IEEE80211_ADDR_LEN],
		const uint8_t bssid[IEEE80211_ADDR_LEN],
		const uint8_t *ssid, size_t ssidlen);
struct mbuf *	ieee80211_ff_encap1(struct ieee80211vap *, struct mbuf *,
		const struct ether_header *);
void	ieee80211_tx_complete(struct ieee80211_node *,
		struct mbuf *, int);

/*
 * The formation of ProbeResponse frames requires guidance to
 * deal with legacy clients.  When the client is identified as
 * "legacy 11b" ieee80211_send_proberesp is passed this token.
 */
#define	IEEE80211_SEND_LEGACY_11B	0x1	/* legacy 11b client */
#define	IEEE80211_SEND_LEGACY_11	0x2	/* other legacy client */
#define	IEEE80211_SEND_LEGACY		0x3	/* any legacy client */
struct mbuf *ieee80211_alloc_proberesp(struct ieee80211_node *, int);
int	ieee80211_send_proberesp(struct ieee80211vap *,
		const uint8_t da[IEEE80211_ADDR_LEN], int);
struct mbuf *ieee80211_alloc_rts(struct ieee80211com *ic,
		const uint8_t [IEEE80211_ADDR_LEN],
		const uint8_t [IEEE80211_ADDR_LEN], uint16_t);
struct mbuf *ieee80211_alloc_cts(struct ieee80211com *,
		const uint8_t [IEEE80211_ADDR_LEN], uint16_t);
struct mbuf *ieee80211_alloc_prot(struct ieee80211_node *,
		const struct mbuf *, uint8_t, int);

uint8_t *ieee80211_add_rates(uint8_t *, const struct ieee80211_rateset *);
uint8_t *ieee80211_add_xrates(uint8_t *, const struct ieee80211_rateset *);
uint8_t *ieee80211_add_ssid(uint8_t *, const uint8_t *, u_int);
uint8_t *ieee80211_add_wpa(uint8_t *, const struct ieee80211vap *);
uint8_t *ieee80211_add_rsn(uint8_t *, const struct ieee80211vap *);
uint8_t *ieee80211_add_qos(uint8_t *, const struct ieee80211_node *);
uint16_t ieee80211_getcapinfo(struct ieee80211vap *,
		struct ieee80211_channel *);
struct ieee80211_wme_state;
uint8_t * ieee80211_add_wme_info(uint8_t *frm, struct ieee80211_wme_state *wme);

void	ieee80211_reset_erp(struct ieee80211com *);
void	ieee80211_set_shortslottime(struct ieee80211com *, int onoff);
int	ieee80211_iserp_rateset(const struct ieee80211_rateset *);
void	ieee80211_setbasicrates(struct ieee80211_rateset *,
		enum ieee80211_phymode);
void	ieee80211_addbasicrates(struct ieee80211_rateset *,
		enum ieee80211_phymode);

/*
 * Return the size of the 802.11 header for a management or data frame.
 */
static __inline int
ieee80211_hdrsize(const void *data)
{
	const struct ieee80211_frame *wh = data;
	int size = sizeof(struct ieee80211_frame);

	/* NB: we don't handle control frames */
	KASSERT((wh->i_fc[0]&IEEE80211_FC0_TYPE_MASK) != IEEE80211_FC0_TYPE_CTL,
		("%s: control frame", __func__));
	if (IEEE80211_IS_DSTODS(wh))
		size += IEEE80211_ADDR_LEN;
	if (IEEE80211_QOS_HAS_SEQ(wh))
		size += sizeof(uint16_t);
	return size;
}

/*
 * Like ieee80211_hdrsize, but handles any type of frame.
 */
static __inline int
ieee80211_anyhdrsize(const void *data)
{
	const struct ieee80211_frame *wh = data;

	if ((wh->i_fc[0]&IEEE80211_FC0_TYPE_MASK) == IEEE80211_FC0_TYPE_CTL) {
		switch (wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK) {
		case IEEE80211_FC0_SUBTYPE_CTS:
		case IEEE80211_FC0_SUBTYPE_ACK:
			return sizeof(struct ieee80211_frame_ack);
		case IEEE80211_FC0_SUBTYPE_BAR:
			return sizeof(struct ieee80211_frame_bar);
		}
		return sizeof(struct ieee80211_frame_min);
	} else
		return ieee80211_hdrsize(data);
}

/*
 * Template for an in-kernel authenticator.  Authenticators
 * register with the protocol code and are typically loaded
 * as separate modules as needed.  One special authenticator
 * is xauth; it intercepts requests so that protocols like
 * WPA can be handled in user space.
 */
struct ieee80211_authenticator {
	const char *ia_name;		/* printable name */
	int	(*ia_attach)(struct ieee80211vap *);
	void	(*ia_detach)(struct ieee80211vap *);
	void	(*ia_node_join)(struct ieee80211_node *);
	void	(*ia_node_leave)(struct ieee80211_node *);
};
void	ieee80211_authenticator_register(int type,
		const struct ieee80211_authenticator *);
void	ieee80211_authenticator_unregister(int type);
const struct ieee80211_authenticator *ieee80211_authenticator_get(int auth);

struct ieee80211req;
/*
 * Template for an MAC ACL policy module.  Such modules
 * register with the protocol code and are passed the sender's
 * address of each received auth frame for validation.
 */
struct ieee80211_aclator {
	const char *iac_name;		/* printable name */
	int	(*iac_attach)(struct ieee80211vap *);
	void	(*iac_detach)(struct ieee80211vap *);
	int	(*iac_check)(struct ieee80211vap *,
			const struct ieee80211_frame *wh);
	int	(*iac_add)(struct ieee80211vap *,
			const uint8_t mac[IEEE80211_ADDR_LEN]);
	int	(*iac_remove)(struct ieee80211vap *,
			const uint8_t mac[IEEE80211_ADDR_LEN]);
	int	(*iac_flush)(struct ieee80211vap *);
	int	(*iac_setpolicy)(struct ieee80211vap *, int);
	int	(*iac_getpolicy)(struct ieee80211vap *);
	int	(*iac_setioctl)(struct ieee80211vap *, struct ieee80211req *);
	int	(*iac_getioctl)(struct ieee80211vap *, struct ieee80211req *);
};
void	ieee80211_aclator_register(const struct ieee80211_aclator *);
void	ieee80211_aclator_unregister(const struct ieee80211_aclator *);
const struct ieee80211_aclator *ieee80211_aclator_get(const char *name);

/* flags for ieee80211_fix_rate() */
#define	IEEE80211_F_DOSORT	0x00000001	/* sort rate list */
#define	IEEE80211_F_DOFRATE	0x00000002	/* use fixed legacy rate */
#define	IEEE80211_F_DONEGO	0x00000004	/* calc negotiated rate */
#define	IEEE80211_F_DODEL	0x00000008	/* delete ignore rate */
#define	IEEE80211_F_DOBRS	0x00000010	/* check basic rate set */
#define	IEEE80211_F_JOIN	0x00000020	/* sta joining our bss */
#define	IEEE80211_F_DOFMCS	0x00000040	/* use fixed HT rate */
int	ieee80211_fix_rate(struct ieee80211_node *,
		struct ieee80211_rateset *, int);

/*
 * WME/WMM support.
 */
struct wmeParams {
	uint8_t		wmep_acm;
	uint8_t		wmep_aifsn;
	uint8_t		wmep_logcwmin;		/* log2(cwmin) */
	uint8_t		wmep_logcwmax;		/* log2(cwmax) */
	uint8_t		wmep_txopLimit;
	uint8_t		wmep_noackPolicy;	/* 0 (ack), 1 (no ack) */
};
#define	IEEE80211_TXOP_TO_US(_txop)	((_txop)<<5)
#define	IEEE80211_US_TO_TXOP(_us)	((_us)>>5)

struct chanAccParams {
	uint8_t		cap_info;		/* version of the current set */
	struct wmeParams cap_wmeParams[WME_NUM_AC];
};

struct ieee80211_wme_state {
	u_int	wme_flags;
#define	WME_F_AGGRMODE	0x00000001	/* STATUS: WME aggressive mode */
	u_int	wme_hipri_traffic;	/* VI/VO frames in beacon interval */
	u_int	wme_hipri_switch_thresh;/* aggressive mode switch thresh */
	u_int	wme_hipri_switch_hysteresis;/* aggressive mode switch hysteresis */

	struct wmeParams wme_params[4];		/* from assoc resp for each AC*/
	struct chanAccParams wme_wmeChanParams;	/* WME params applied to self */
	struct chanAccParams wme_wmeBssChanParams;/* WME params bcast to stations */
	struct chanAccParams wme_chanParams;	/* params applied to self */
	struct chanAccParams wme_bssChanParams;	/* params bcast to stations */

	int	(*wme_update)(struct ieee80211com *);
};

void	ieee80211_wme_initparams(struct ieee80211vap *);
void	ieee80211_wme_updateparams(struct ieee80211vap *);
void	ieee80211_wme_updateparams_locked(struct ieee80211vap *);
void	ieee80211_wme_vap_getparams(struct ieee80211vap *vap,
	    struct chanAccParams *);
void	ieee80211_wme_ic_getparams(struct ieee80211com *ic,
	    struct chanAccParams *);
int	ieee80211_wme_vap_ac_is_noack(struct ieee80211vap *vap, int ac);

/*
 * Return pointer to the QoS field from a Qos frame.
 */
static __inline uint8_t *
ieee80211_getqos(void *data)
{
	struct ieee80211_frame *wh = data;

	KASSERT(IEEE80211_QOS_HAS_SEQ(wh), ("QoS field is absent!"));

	if (IEEE80211_IS_DSTODS(wh))
		return (((struct ieee80211_qosframe_addr4 *)wh)->i_qos);
	else
		return (((struct ieee80211_qosframe *)wh)->i_qos);
}

/*
 * Return the WME TID from a QoS frame.  If no TID
 * is present return the index for the "non-QoS" entry.
 */
static __inline uint8_t
ieee80211_gettid(const struct ieee80211_frame *wh)
{
	uint8_t tid;

	if (IEEE80211_QOS_HAS_SEQ(wh)) {
		if (IEEE80211_IS_DSTODS(wh))
			tid = ((const struct ieee80211_qosframe_addr4 *)wh)->
				i_qos[0];
		else
			tid = ((const struct ieee80211_qosframe *)wh)->i_qos[0];
		tid &= IEEE80211_QOS_TID;
	} else
		tid = IEEE80211_NONQOS_TID;
	return tid;
}

void	ieee80211_waitfor_parent(struct ieee80211com *);
void	ieee80211_start_locked(struct ieee80211vap *);
void	ieee80211_init(void *);
void	ieee80211_start_all(struct ieee80211com *);
void	ieee80211_stop_locked(struct ieee80211vap *);
void	ieee80211_stop(struct ieee80211vap *);
void	ieee80211_stop_all(struct ieee80211com *);
void	ieee80211_suspend_all(struct ieee80211com *);
void	ieee80211_resume_all(struct ieee80211com *);
void	ieee80211_restart_all(struct ieee80211com *);
void	ieee80211_dturbo_switch(struct ieee80211vap *, int newflags);
void	ieee80211_swbmiss(void *arg);
void	ieee80211_beacon_miss(struct ieee80211com *);
int	ieee80211_new_state(struct ieee80211vap *, enum ieee80211_state, int);
int	ieee80211_new_state_locked(struct ieee80211vap *, enum ieee80211_state,
		int);
void	ieee80211_print_essid(const uint8_t *, int);
void	ieee80211_dump_pkt(struct ieee80211com *,
		const uint8_t *, int, int, int);

extern 	const char *ieee80211_opmode_name[];
extern	const char *ieee80211_state_name[IEEE80211_S_MAX];
extern	const char *ieee80211_wme_acnames[];

/*
 * Beacon frames constructed by ieee80211_beacon_alloc
 * have the following structure filled in so drivers
 * can update the frame later w/ minimal overhead.
 */
struct ieee80211_beacon_offsets {
	uint8_t		bo_flags[4];	/* update/state flags */
	uint16_t	*bo_caps;	/* capabilities */
	uint8_t		*bo_cfp;	/* start of CFParms element */
	uint8_t		*bo_tim;	/* start of atim/dtim */
	uint8_t		*bo_wme;	/* start of WME parameters */
	uint8_t		*bo_tdma;	/* start of TDMA parameters */
	uint8_t		*bo_tim_trailer;/* start of fixed-size trailer */
	uint16_t	bo_tim_len;	/* atim/dtim length in bytes */
	uint16_t	bo_tim_trailer_len;/* tim trailer length in bytes */
	uint8_t		*bo_erp;	/* start of ERP element */
	uint8_t		*bo_htinfo;	/* start of HT info element */
	uint8_t		*bo_ath;	/* start of ATH parameters */
	uint8_t		*bo_appie;	/* start of AppIE element */
	uint16_t	bo_appie_len;	/* AppIE length in bytes */
	uint16_t	bo_csa_trailer_len;
	uint8_t		*bo_csa;	/* start of CSA element */
	uint8_t		*bo_quiet;	/* start of Quiet element */
	uint8_t		*bo_meshconf;	/* start of MESHCONF element */
	uint8_t		*bo_vhtinfo;	/* start of VHT info element (XXX VHTCAP?) */
	uint8_t		*bo_spare[2];
};
struct mbuf *ieee80211_beacon_alloc(struct ieee80211_node *);

/*
 * Beacon frame updates are signaled through calls to iv_update_beacon
 * with one of the IEEE80211_BEACON_* tokens defined below.  For devices
 * that construct beacon frames on the host this can trigger a rebuild
 * or defer the processing.  For devices that offload beacon frame
 * handling this callback can be used to signal a rebuild.  The bo_flags
 * array in the ieee80211_beacon_offsets structure is intended to record
 * deferred processing requirements; ieee80211_beacon_update uses the
 * state to optimize work.  Since this structure is owned by the driver
 * and not visible to the 802.11 layer drivers must supply an iv_update_beacon
 * callback that marks the flag bits and schedules (as necessary) an update.
 */
enum {
	IEEE80211_BEACON_CAPS	= 0,	/* capabilities */
	IEEE80211_BEACON_TIM	= 1,	/* DTIM/ATIM */
	IEEE80211_BEACON_WME	= 2,
	IEEE80211_BEACON_ERP	= 3,	/* Extended Rate Phy */
	IEEE80211_BEACON_HTINFO	= 4,	/* HT Information */
	IEEE80211_BEACON_APPIE	= 5,	/* Application IE's */
	IEEE80211_BEACON_CFP	= 6,	/* CFParms */
	IEEE80211_BEACON_CSA	= 7,	/* Channel Switch Announcement */
	IEEE80211_BEACON_TDMA	= 9,	/* TDMA Info */
	IEEE80211_BEACON_ATH	= 10,	/* ATH parameters */
	IEEE80211_BEACON_MESHCONF = 11,	/* Mesh Configuration */
	IEEE80211_BEACON_QUIET	= 12,	/* Quiet time IE */
	IEEE80211_BEACON_VHTINFO	= 13,	/* VHT information */
};
int	ieee80211_beacon_update(struct ieee80211_node *,
		struct mbuf *, int mcast);

void	ieee80211_csa_startswitch(struct ieee80211com *,
		struct ieee80211_channel *, int mode, int count);
void	ieee80211_csa_completeswitch(struct ieee80211com *);
void	ieee80211_csa_cancelswitch(struct ieee80211com *);
void	ieee80211_cac_completeswitch(struct ieee80211vap *);

/*
 * Notification methods called from the 802.11 state machine.
 * Note that while these are defined here, their implementation
 * is OS-specific.
 */
void	ieee80211_notify_node_join(struct ieee80211_node *, int newassoc);
void	ieee80211_notify_node_leave(struct ieee80211_node *);
void	ieee80211_notify_scan_done(struct ieee80211vap *);
void	ieee80211_notify_wds_discover(struct ieee80211_node *);
void	ieee80211_notify_csa(struct ieee80211com *,
		const struct ieee80211_channel *, int mode, int count);
void	ieee80211_notify_radar(struct ieee80211com *,
		const struct ieee80211_channel *);
enum ieee80211_notify_cac_event {
	IEEE80211_NOTIFY_CAC_START  = 0, /* CAC timer started */
	IEEE80211_NOTIFY_CAC_STOP   = 1, /* CAC intentionally stopped */
	IEEE80211_NOTIFY_CAC_RADAR  = 2, /* CAC stopped due to radar detectio */
	IEEE80211_NOTIFY_CAC_EXPIRE = 3, /* CAC expired w/o radar */
};
void	ieee80211_notify_cac(struct ieee80211com *,
		const struct ieee80211_channel *,
		enum ieee80211_notify_cac_event);
void	ieee80211_notify_node_deauth(struct ieee80211_node *);
void	ieee80211_notify_node_auth(struct ieee80211_node *);
void	ieee80211_notify_country(struct ieee80211vap *, const uint8_t [],
		const uint8_t cc[2]);
void	ieee80211_notify_radio(struct ieee80211com *, int);
#endif /* _NET80211_IEEE80211_PROTO_H_ */
