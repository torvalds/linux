/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Shteryana Sotirova Shopova under
 * sponsorship from the FreeBSD Foundation.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#define	WLAN_IFMODE_MAX				WlanIfaceOperatingModeType_tdma
#define	WLAN_COUNTRY_CODE_SIZE			3
#define	WLAN_BGSCAN_IDLE_MIN			100 /* XXX */
#define	WLAN_SCAN_VALID_MIN			10 /* XXX */
#define	WLAN_TDMA_MAXSLOTS			2 /* XXX */

struct wlan_iface;

struct wlan_peer {
	uint8_t				pmac[IEEE80211_ADDR_LEN]; /* key */
	uint16_t			associd;
	uint16_t			vlan;
	uint16_t			frequency;
	uint32_t			fflags;
	uint8_t				txrate;
	int8_t				rssi;
	uint16_t			idle;
	uint16_t			txseqs;
	uint16_t			rxseqs;
	uint16_t			txpower;
	uint8_t				capinfo;
	uint32_t			state;
	uint16_t			local_id;
	uint16_t			peer_id;
	SLIST_ENTRY(wlan_peer)		wp;
};

SLIST_HEAD(wlan_peerlist, wlan_peer);

struct wlan_scan_result {
	uint8_t				ssid[IEEE80211_NWID_LEN + 1];
	uint8_t				bssid[IEEE80211_ADDR_LEN];
	uint8_t				opchannel;
	int8_t				rssi;
	uint16_t			frequency;
	int8_t				noise;
	uint16_t			bintval;
	uint8_t				capinfo;
	struct wlan_iface		*pwif;
	SLIST_ENTRY(wlan_scan_result)	wsr;
};

SLIST_HEAD(wlan_scanlist, wlan_scan_result);

struct wlan_mac_mac {
	uint8_t				mac[IEEE80211_ADDR_LEN];
	enum RowStatus			mac_status;
	SLIST_ENTRY(wlan_mac_mac)	wm;
};

SLIST_HEAD(wlan_maclist, wlan_mac_mac);

struct wlan_mesh_route {
	struct ieee80211req_mesh_route	imroute;
	enum RowStatus			mroute_status;
	SLIST_ENTRY(wlan_mesh_route)	wr;
};

SLIST_HEAD(wlan_mesh_routes, wlan_mesh_route);

struct wlan_iface {
	char				wname[IFNAMSIZ];
	uint32_t			index;
	char				pname[IFNAMSIZ];
	enum WlanIfaceOperatingModeType	mode;
	uint32_t			flags;
	uint8_t				dbssid[IEEE80211_ADDR_LEN];
	uint8_t				dlmac[IEEE80211_ADDR_LEN];
	enum RowStatus			status;
	enum wlanIfaceState		state;
	uint8_t				internal;

	uint32_t			drivercaps;
	uint32_t			cryptocaps;
	uint32_t			htcaps;

	uint32_t			packet_burst;
	uint8_t				country_code[WLAN_COUNTRY_CODE_SIZE];
	enum WlanRegDomainCode		reg_domain;
	uint8_t				desired_ssid[IEEE80211_NWID_LEN + 1];
	uint32_t			desired_channel;
	enum TruthValue			dyn_frequency;
	enum TruthValue			fast_frames;
	enum TruthValue			dturbo;
	int32_t				tx_power;
	int32_t				frag_threshold;
	int32_t				rts_threshold;
	enum TruthValue			priv_subscribe;
	enum TruthValue			bg_scan;
	int32_t				bg_scan_idle;
	int32_t				bg_scan_interval;
	int32_t				beacons_missed;
	uint8_t				desired_bssid[IEEE80211_ADDR_LEN];
	enum wlanIfaceRoamingMode	roam_mode;
	enum TruthValue			dot11d;
	enum TruthValue			dot11h;
	enum TruthValue			dynamic_wds;
	enum TruthValue			power_save;
	enum TruthValue			ap_bridge;
	int32_t				beacon_interval;
	int32_t				dtim_period;
	enum TruthValue			hide_ssid;
	enum TruthValue			inact_process;
	enum wlanIfaceDot11gProtMode	do11g_protect;
	enum TruthValue			dot11g_pure;
	enum TruthValue			dot11n_pure;
	enum WlanIfaceDot11nPduType	ampdu;
	int32_t				ampdu_density;
	int32_t				ampdu_limit;
	enum WlanIfaceDot11nPduType	amsdu;
	int32_t				amsdu_limit;
	enum TruthValue			ht_enabled;
	enum TruthValue			ht_compatible;
	enum wlanIfaceDot11nHTProtMode	ht_prot_mode;
	enum TruthValue			rifs;
	enum TruthValue			short_gi;
	enum wlanIfaceDot11nSMPSMode	smps_mode;
	int32_t				tdma_slot;
	int32_t				tdma_slot_count;
	int32_t				tdma_slot_length;
	int32_t				tdma_binterval;

	struct wlan_peerlist		peerlist;
	struct ieee80211_stats		stats;
	uint32_t			nchannels;
	struct ieee80211_channel	*chanlist;
	struct ieee80211_roamparams_req	roamparams;
	struct ieee80211_txparams_req	txparams;

	uint32_t			scan_flags;
	uint32_t			scan_duration;
	uint32_t			scan_mindwell;
	uint32_t			scan_maxdwell;
	enum wlanScanConfigStatus	scan_status;
	struct wlan_scanlist		scanlist;

	uint8_t				wepsupported;
	enum wlanWepMode		wepmode;
	int32_t				weptxkey;

	uint8_t				macsupported;
	enum wlanMACAccessControlPolicy	mac_policy;
	uint32_t			mac_nacls;
	struct wlan_maclist		mac_maclist;

	uint32_t			mesh_ttl;
	enum wlanMeshPeeringEnabled	mesh_peering;
	enum wlanMeshForwardingEnabled	mesh_forwarding;
	enum wlanMeshMetric		mesh_metric;
	enum wlanMeshPath		mesh_path;
	enum wlanHWMPRootMode		hwmp_root_mode;
	uint32_t			hwmp_max_hops;
	struct wlan_mesh_routes		mesh_routelist;

	SLIST_ENTRY(wlan_iface)		w_if;
};

enum wlan_syscl {
	WLAN_MESH_RETRY_TO = 0,
	WLAN_MESH_HOLDING_TO,
	WLAN_MESH_CONFIRM_TO,
	WLAN_MESH_MAX_RETRIES,
	WLAN_HWMP_TARGET_ONLY,
	WLAN_HWMP_REPLY_FORWARD,
	WLAN_HWMP_PATH_LIFETIME,
	WLAN_HWMP_ROOT_TO,
	WLAN_HWMP_ROOT_INT,
	WLAN_HWMP_RANN_INT,
	WLAN_HWMP_INACTIVITY_TO,
	WLAN_SYSCTL_MAX
};

struct wlan_config {
	int32_t				mesh_retryto;
	int32_t				mesh_holdingto;
	int32_t				mesh_confirmto;
	int32_t				mesh_maxretries;
	int32_t				hwmp_targetonly;
	int32_t				hwmp_replyforward;
	int32_t				hwmp_pathlifetime;
	int32_t				hwmp_roottimeout;
	int32_t				hwmp_rootint;
	int32_t				hwmp_rannint;
	int32_t				hwmp_inact;
};

int wlan_ioctl_init(void);
int wlan_kmodules_load(void);
int wlan_check_media(char *);
int wlan_config_state(struct wlan_iface *, uint8_t);
int wlan_get_opmode(struct wlan_iface *wif);
int wlan_get_local_addr(struct wlan_iface *wif);
int wlan_get_parent(struct wlan_iface *wif);
int wlan_get_driver_caps(struct wlan_iface *wif);
uint8_t wlan_channel_state_to_snmp(uint8_t cstate);
uint32_t wlan_channel_flags_to_snmp(uint32_t cflags);
int wlan_get_channel_list(struct wlan_iface *wif);
int wlan_get_roam_params(struct wlan_iface *wif);
int wlan_get_tx_params(struct wlan_iface *wif);
int wlan_set_tx_params(struct wlan_iface *wif, int32_t pmode);
int wlan_clone_create(struct wlan_iface *);
int wlan_clone_destroy(struct wlan_iface *wif);
int wlan_config_get_dssid(struct wlan_iface *wif);
int wlan_config_set_dssid(struct wlan_iface *wif, char *ssid, int slen);
int wlan_config_get_ioctl(struct wlan_iface *wif, int which);
int wlan_config_set_ioctl(struct wlan_iface *wif, int which, int val,
    char *strval, int len);
int wlan_set_scan_config(struct wlan_iface *wif);
int wlan_get_scan_results(struct wlan_iface *wif);
int wlan_get_stats(struct wlan_iface *wif);
int wlan_get_wepmode(struct wlan_iface *wif);
int wlan_set_wepmode(struct wlan_iface *wif);
int wlan_get_weptxkey(struct wlan_iface *wif);
int wlan_set_weptxkey(struct wlan_iface *wif);
int wlan_get_wepkeys(struct wlan_iface *wif);
int wlan_set_wepkeys(struct wlan_iface *wif);
int wlan_get_mac_policy(struct wlan_iface *wif);
int wlan_set_mac_policy(struct wlan_iface *wif);
int wlan_flush_mac_mac(struct wlan_iface *wif);
int wlan_get_mac_acl_macs(struct wlan_iface *wif);
int wlan_add_mac_acl_mac(struct wlan_iface *wif, struct wlan_mac_mac *mmac);
int wlan_del_mac_acl_mac(struct wlan_iface *wif, struct wlan_mac_mac *mmac);

int32_t wlan_do_sysctl(struct wlan_config *cfg, enum wlan_syscl which, int set);
int wlan_mesh_config_get(struct wlan_iface *wif, int which);
int wlan_mesh_config_set(struct wlan_iface *wif, int which);
int wlan_mesh_flush_routes(struct wlan_iface *wif);
int wlan_mesh_add_route(struct wlan_iface *wif, struct wlan_mesh_route *wmr);
int wlan_mesh_del_route(struct wlan_iface *wif, struct wlan_mesh_route *wmr);
int wlan_mesh_get_routelist(struct wlan_iface *wif);
int wlan_hwmp_config_get(struct wlan_iface *wif, int which);
int wlan_hwmp_config_set(struct wlan_iface *wif, int which);

/* XXX: static */

int wlan_peer_set_vlan(struct wlan_iface *wif, struct wlan_peer *wip, int vlan);
int wlan_get_peerinfo(struct wlan_iface *wif);

/* XXX*/
struct wlan_peer *wlan_new_peer(const uint8_t *pmac);
void wlan_free_peer(struct wlan_peer *wip);
int wlan_add_peer(struct wlan_iface *wif, struct wlan_peer *wip);

struct wlan_scan_result * wlan_scan_new_result(const uint8_t *ssid,
    const uint8_t *bssid);
void wlan_scan_free_result(struct wlan_scan_result *sr);
int wlan_scan_add_result(struct wlan_iface *wif, struct wlan_scan_result *sr);

struct wlan_mac_mac *wlan_mac_new_mac(const uint8_t *mac);
void wlan_mac_free_mac(struct wlan_mac_mac *wmm);
int wlan_mac_add_mac(struct wlan_iface *wif, struct wlan_mac_mac *wmm);

struct wlan_mesh_route *wlan_mesh_new_route(const uint8_t *dstmac);
int wlan_mesh_add_rtentry(struct wlan_iface *wif, struct wlan_mesh_route *wmr);
void wlan_mesh_free_route(struct wlan_mesh_route *wmr);
