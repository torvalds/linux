/*
 * WPA Supplicant - Driver event processing
 * Copyright (c) 2003-2006, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */

#include "includes.h"

#include "common.h"
#include "eapol_sm.h"
#include "wpa.h"
#include "wapi.h"
#include "wapi_i.h"
#include "cert.h"
#include "ecc.h"
#include "eloop.h"
#include "wpa_supplicant.h"
#include "config.h"
#include "l2_packet.h"
#include "wpa_supplicant_i.h"
#include "pcsc_funcs.h"
#include "preauth.h"
#include "pmksa_cache.h"
#include "wpa_ctrl.h"
#include "eap.h"
#include "ctrl_iface_dbus.h"

int cleanup_cert(void);

static int wpa_supplicant_select_config(struct wpa_supplicant *wpa_s)
{
	struct wpa_ssid *ssid;

	if (wpa_s->conf->ap_scan == 1 && wpa_s->current_ssid)
		return 0;

	ssid = wpa_supplicant_get_ssid(wpa_s);
	if (ssid == NULL) {
		wpa_printf(MSG_INFO, "No network configuration found for the "
			   "current AP");
		return -1;
	}

	if (ssid->disabled) {
		wpa_printf(MSG_DEBUG, "Selected network is disabled");
		return -1;
	}

	wpa_printf(MSG_DEBUG, "Network configuration found for the current "
		   "AP");
	
	if (ssid->key_mgmt & (WPA_KEY_MGMT_PSK | WPA_KEY_MGMT_IEEE8021X |
			      WPA_KEY_MGMT_WPA_NONE)) {
		u8 wpa_ie[80];
		size_t wpa_ie_len = sizeof(wpa_ie);
		wpa_supplicant_wpa_set_suites(wpa_s, NULL, ssid,
					  wpa_ie, &wpa_ie_len);
	} else if (ssid->key_mgmt & (WAPI_KEY_MGMT_PSK | WAPI_KEY_MGMT_CERT)) {
		u8 wapi_ie[80];
		size_t wapi_ie_len = sizeof(wapi_ie);
		wpa_supplicant_wapi_set_suites(wpa_s, NULL, ssid,
					  wapi_ie, &wapi_ie_len);
		
	} else {
		wpa_supplicant_set_non_wpa_policy(wpa_s, ssid);
	}
	if (wpa_s->current_ssid && wpa_s->current_ssid != ssid)
		eapol_sm_invalidate_cached_session(wpa_s->eapol);
	wpa_s->current_ssid = ssid;
	
	if (wpa_s->key_mgmt == WAPI_KEY_MGMT_PSK || wpa_s->key_mgmt == WAPI_KEY_MGMT_CERT)
		wapi_sm_set_config(wpa_s->wapi, wpa_s->current_ssid);
	else
		wpa_sm_set_config(wpa_s->wpa, wpa_s->current_ssid);
	
	wpa_supplicant_initiate_eapol(wpa_s);
	return 0;
}


static void wpa_supplicant_stop_countermeasures(void *eloop_ctx,
						void *sock_ctx)
{
	struct wpa_supplicant *wpa_s = eloop_ctx;

	if (wpa_s->countermeasures) {
		wpa_s->countermeasures = 0;
		wpa_drv_set_countermeasures(wpa_s, 0);
		wpa_msg(wpa_s, MSG_INFO, "WPA: TKIP countermeasures stopped");
		wpa_supplicant_req_scan(wpa_s, 0, 0);
	}
}


void wpa_supplicant_mark_disassoc(struct wpa_supplicant *wpa_s)
{
	wpa_supplicant_set_state(wpa_s, WPA_DISCONNECTED);
	os_memset(wpa_s->bssid, 0, ETH_ALEN);
	os_memset(wpa_s->pending_bssid, 0, ETH_ALEN);
	eapol_sm_notify_portEnabled(wpa_s->eapol, FALSE);
	eapol_sm_notify_portValid(wpa_s->eapol, FALSE);
	
	if ( (wpa_s->key_mgmt == WPA_KEY_MGMT_PSK) || (wpa_s->key_mgmt == WAPI_KEY_MGMT_PSK))
		eapol_sm_notify_eap_success(wpa_s->eapol, FALSE);
}


static void wpa_find_assoc_pmkid(struct wpa_supplicant *wpa_s)
{
	struct wpa_ie_data ie;
	int i, pmksa_set = -1;

	if (wpa_sm_parse_own_wpa_ie(wpa_s->wpa, &ie) < 0 ||
	    ie.pmkid == NULL)
		return;

	for (i = 0; i < ie.num_pmkid; i++) {
		pmksa_set = pmksa_cache_set_current(wpa_s->wpa,
						    ie.pmkid + i * PMKID_LEN,
						    NULL, NULL, 0);
		if (pmksa_set == 0) {
			eapol_sm_notify_pmkid_attempt(wpa_s->eapol, 1);
			break;
		}
	}

	wpa_printf(MSG_DEBUG, "RSN: PMKID from assoc IE %sfound from PMKSA "
		   "cache", pmksa_set == 0 ? "" : "not ");
}


static void wpa_supplicant_event_pmkid_candidate(struct wpa_supplicant *wpa_s,
						 union wpa_event_data *data)
{
	if (data == NULL) {
		wpa_printf(MSG_DEBUG, "RSN: No data in PMKID candidate event");
		return;
	}
	wpa_printf(MSG_DEBUG, "RSN: PMKID candidate event - bssid=" MACSTR
		   " index=%d preauth=%d",
		   MAC2STR(data->pmkid_candidate.bssid),
		   data->pmkid_candidate.index,
		   data->pmkid_candidate.preauth);

	pmksa_candidate_add(wpa_s->wpa, data->pmkid_candidate.bssid,
			    data->pmkid_candidate.index,
			    data->pmkid_candidate.preauth);
}


static int wpa_supplicant_dynamic_keys(struct wpa_supplicant *wpa_s)
{
	if (wpa_s->key_mgmt == WPA_KEY_MGMT_NONE ||
	    wpa_s->key_mgmt == WPA_KEY_MGMT_WPA_NONE)
		return 0;

#ifdef IEEE8021X_EAPOL
	if (wpa_s->key_mgmt == WPA_KEY_MGMT_IEEE8021X_NO_WPA &&
	    wpa_s->current_ssid &&
	    !(wpa_s->current_ssid->eapol_flags &
	      (EAPOL_FLAG_REQUIRE_KEY_UNICAST |
	       EAPOL_FLAG_REQUIRE_KEY_BROADCAST))) {
		/* IEEE 802.1X, but not using dynamic WEP keys (i.e., either
		 * plaintext or static WEP keys). */
		return 0;
	}
#endif /* IEEE8021X_EAPOL */

	return 1;
}


/**
 * wpa_supplicant_scard_init - Initialize SIM/USIM access with PC/SC
 * @wpa_s: pointer to wpa_supplicant data
 * @ssid: Configuration data for the network
 * Returns: 0 on success, -1 on failure
 *
 * This function is called when starting authentication with a network that is
 * configured to use PC/SC for SIM/USIM access (EAP-SIM or EAP-AKA).
 */
int wpa_supplicant_scard_init(struct wpa_supplicant *wpa_s,
			      struct wpa_ssid *ssid)
{
#ifdef IEEE8021X_EAPOL
	int aka = 0, sim = 0; 
	scard_sim_type type;

	if (ssid->pcsc == NULL || wpa_s->scard != NULL)
		return 0;

	if (ssid->eap_methods == NULL) {
		sim = 1;
		aka = 1;
	} else {
		struct eap_method_type *eap = ssid->eap_methods;
		while (eap->vendor != EAP_VENDOR_IETF ||
		       eap->method != EAP_TYPE_NONE) {
			if (eap->vendor == EAP_VENDOR_IETF) {
				if (eap->method == EAP_TYPE_SIM)
					sim = 1;
				else if (eap->method == EAP_TYPE_AKA)
					aka = 1;
			}
			eap++;
		}
	}

#if 0
	/* This triggers a compiler error in ADS 1.2 842. */
	if (eap_sm_get_eap_methods(EAP_VENDOR_IETF, EAP_TYPE_SIM) == NULL)
		sim = 0;
	if (eap_sm_get_eap_methods(EAP_VENDOR_IETF, EAP_TYPE_AKA) == NULL)
		aka = 0;
#else
#ifndef EAP_SIM
	sim = 0;
#endif
#ifndef EAP_AKA
	aka = 0;
#endif
#endif
	if (!sim && !aka) {
		wpa_printf(MSG_DEBUG, "Selected network is configured to use "
			   "SIM, but neither EAP-SIM nor EAP-AKA are enabled");
		return 0;
	}

	wpa_printf(MSG_DEBUG, "Selected network is configured to use SIM "
		   "(sim=%d aka=%d) - initialize PCSC", sim, aka);
	if (sim && aka)
		type = SCARD_TRY_BOTH;
	else if (aka)
		type = SCARD_USIM_ONLY;
	else
		type = SCARD_GSM_SIM_ONLY;

	wpa_s->scard = scard_init(type);
	if (wpa_s->scard == NULL) {
		wpa_printf(MSG_WARNING, "Failed to initialize SIM "
			   "(pcsc-lite)");
		return -1;
	}
	wpa_sm_set_scard_ctx(wpa_s->wpa, wpa_s->scard);
	eapol_sm_register_scard_ctx(wpa_s->eapol, wpa_s->scard);
#endif /* IEEE8021X_EAPOL */

	return 0;
}


static int wpa_supplicant_match_privacy(struct wpa_scan_result *bss,
					struct wpa_ssid *ssid)
{
	int i, privacy = 0;

	if (ssid->mixed_cell)
		return 1;

	for (i = 0; i < NUM_WEP_KEYS; i++) {
		if (ssid->wep_key_len[i]) {
			privacy = 1;
			break;
		}
	}
#ifdef IEEE8021X_EAPOL
	if ((ssid->key_mgmt & WPA_KEY_MGMT_IEEE8021X_NO_WPA) &&
	    ssid->eapol_flags & (EAPOL_FLAG_REQUIRE_KEY_UNICAST |
				 EAPOL_FLAG_REQUIRE_KEY_BROADCAST))
		privacy = 1;
#endif /* IEEE8021X_EAPOL */

	if (bss->caps & IEEE80211_CAP_PRIVACY)
		return privacy;
	return !privacy;
}


static int wpa_supplicant_ssid_bss_match(struct wpa_ssid *ssid,
					 struct wpa_scan_result *bss)
{
	struct wpa_ie_data ie;
	struct wapi_ie_data wapi_ie;
	int proto_match = 0;
	while ((ssid->proto & WAPI_PROTO) && bss->wapi_ie_len > 0) {
		proto_match++;

		if (wapi_parse_ie(bss->wapi_ie, bss->wapi_ie_len, &wapi_ie)) {
			wpa_printf(MSG_DEBUG, "   skip WAPI IE - parse failed");
			break;
		}
		if (!(wapi_ie.proto & ssid->proto)) {
			wpa_printf(MSG_DEBUG, "   skip WAPI IE - proto "
				   "mismatch");
			break;
		}

		if (!(wapi_ie.pairwise_cipher & ssid->pairwise_cipher)) {
			wpa_printf(MSG_DEBUG, "   skip WAPI IE - PTK cipher "
				   "mismatch");
			break;
		}

		if (!(wapi_ie.group_cipher & ssid->group_cipher)) {
			wpa_printf(MSG_DEBUG, "   skip WAPI IE - GTK cipher "
				   "mismatch");
			break;
		}

		if (!(wapi_ie.key_mgmt & ssid->key_mgmt)) {
			wpa_printf(MSG_DEBUG, "   skip WAPI IE - key mgmt "
				   "mismatch");
			break;
		}

		
		wpa_printf(MSG_DEBUG, "   selected based on WAPI IE");
		return 1;
	}

	while ((ssid->proto & WPA_PROTO_RSN) && bss->rsn_ie_len > 0) {
		proto_match++;

		if (wpa_parse_wpa_ie(bss->rsn_ie, bss->rsn_ie_len, &ie)) {
			wpa_printf(MSG_DEBUG, "   skip RSN IE - parse failed");
			break;
		}
		if (!(ie.proto & ssid->proto)) {
			wpa_printf(MSG_DEBUG, "   skip RSN IE - proto "
				   "mismatch");
			break;
		}

		if (!(ie.pairwise_cipher & ssid->pairwise_cipher)) {
			wpa_printf(MSG_DEBUG, "   skip RSN IE - PTK cipher "
				   "mismatch");
			break;
		}

		if (!(ie.group_cipher & ssid->group_cipher)) {
			wpa_printf(MSG_DEBUG, "   skip RSN IE - GTK cipher "
				   "mismatch");
			break;
		}

		if (!(ie.key_mgmt & ssid->key_mgmt)) {
			wpa_printf(MSG_DEBUG, "   skip RSN IE - key mgmt "
				   "mismatch");
			break;
		}

#ifdef CONFIG_IEEE80211W
		if (!(ie.capabilities & WPA_CAPABILITY_MGMT_FRAME_PROTECTION)
		    && ssid->ieee80211w == IEEE80211W_REQUIRED) {
			wpa_printf(MSG_DEBUG, "   skip RSN IE - no mgmt frame "
				   "protection");
			break;
		}
#endif /* CONFIG_IEEE80211W */

		wpa_printf(MSG_DEBUG, "   selected based on RSN IE");
		return 1;
	}

	while ((ssid->proto & WPA_PROTO_WPA) && bss->wpa_ie_len > 0) {
		proto_match++;

		if (wpa_parse_wpa_ie(bss->wpa_ie, bss->wpa_ie_len, &ie)) {
			wpa_printf(MSG_DEBUG, "   skip WPA IE - parse failed");
			break;
		}
		if (!(ie.proto & ssid->proto)) {
			wpa_printf(MSG_DEBUG, "   skip WPA IE - proto "
				   "mismatch");
			break;
		}

		if (!(ie.pairwise_cipher & ssid->pairwise_cipher)) {
			wpa_printf(MSG_DEBUG, "   skip WPA IE - PTK cipher "
				   "mismatch");
			wpa_printf(MSG_DEBUG, "ie pairwise_cipher: 0x%x", ie.pairwise_cipher);
			wpa_printf(MSG_DEBUG, "ssid pairwise_cipher: 0x%x", ssid->pairwise_cipher);
			break;
		}

		if (!(ie.group_cipher & ssid->group_cipher)) {
			wpa_printf(MSG_DEBUG, "   skip WPA IE - GTK cipher "
				   "mismatch");
			break;
		}

		if (!(ie.key_mgmt & ssid->key_mgmt)) {
			wpa_printf(MSG_DEBUG, "   skip WPA IE - key mgmt "
				   "mismatch");
			break;
		}

		wpa_printf(MSG_DEBUG, "   selected based on WPA IE");
		return 1;
	}

	if (proto_match == 0)
		wpa_printf(MSG_DEBUG, "   skip - no WPA/RSN proto match");

	return 0;
}


static struct wpa_scan_result *
wpa_supplicant_select_bss(struct wpa_supplicant *wpa_s, struct wpa_ssid *group,
			  struct wpa_scan_result *results, int num,
			  struct wpa_ssid **selected_ssid)
{
	struct wpa_ssid *ssid;
	struct wpa_scan_result *bss, *selected = NULL;
	int i;
	struct wpa_blacklist *e;

	wpa_printf(MSG_DEBUG, "Selecting BSS from priority group %d",
		   group->priority);

	bss = NULL;
	ssid = NULL;
	/* First, try to find WPA-enabled AP */
	wpa_printf(MSG_DEBUG, "Try to find WPA/WAPI-enabled AP");
	for (i = 0; i < num && !selected; i++) {
		bss = &results[i];
		wpa_printf(MSG_DEBUG, "%d: " MACSTR " ssid='%s' "
			   "wpa_ie_len=%lu rsn_ie_len=%lu caps=0x%x",
			   i, MAC2STR(bss->bssid),
			   wpa_ssid_txt(bss->ssid, bss->ssid_len),
			   (unsigned long) bss->wpa_ie_len,
			   (unsigned long) bss->rsn_ie_len, bss->caps);
		e = wpa_blacklist_get(wpa_s, bss->bssid);
		if (e && e->count > 1) {
			wpa_printf(MSG_DEBUG, "   skip - blacklisted");
			continue;
		}

		if (bss->wpa_ie_len == 0 && bss->rsn_ie_len == 0 && bss->wapi_ie_len == 0) {
			wpa_printf(MSG_DEBUG, "   skip - no WPA/RSN/WAPI IE");
			continue;
		}

		for (ssid = group; ssid; ssid = ssid->pnext) {
			if (ssid->disabled) {
				wpa_printf(MSG_DEBUG, "   skip - disabled");
				continue;
			}
			if (bss->ssid_len != ssid->ssid_len ||
			    os_memcmp(bss->ssid, ssid->ssid,
				      bss->ssid_len) != 0) {
				wpa_printf(MSG_DEBUG, "   skip - "
					   "SSID mismatch");
				continue;
			}
			if (ssid->bssid_set &&
			    os_memcmp(bss->bssid, ssid->bssid, ETH_ALEN) != 0)
			{
				wpa_printf(MSG_DEBUG, "   skip - "
					   "BSSID mismatch");
				continue;
			}
			if (wpa_supplicant_ssid_bss_match(ssid, bss)) {
				selected = bss;
				*selected_ssid = ssid;
				wpa_printf(MSG_DEBUG, "   selected WPA AP "
					   MACSTR " ssid='%s'",
					   MAC2STR(bss->bssid),
					   wpa_ssid_txt(bss->ssid,
							bss->ssid_len));
				break;
			}
		}
	}

	/* If no WPA-enabled AP found, try to find non-WPA AP, if configuration
	 * allows this. */
	wpa_printf(MSG_DEBUG, "Try to find non-WPA AP");
	for (i = 0; i < num && !selected; i++) {
		bss = &results[i];
		wpa_printf(MSG_DEBUG, "%d: " MACSTR " ssid='%s' "
			   "wpa_ie_len=%lu rsn_ie_len=%lu caps=0x%x",
			   i, MAC2STR(bss->bssid),
			   wpa_ssid_txt(bss->ssid, bss->ssid_len),
			   (unsigned long) bss->wpa_ie_len,
			   (unsigned long) bss->rsn_ie_len, bss->caps);
		e = wpa_blacklist_get(wpa_s, bss->bssid);
		if (e && e->count > 1) {
			wpa_printf(MSG_DEBUG, "   skip - blacklisted");
			continue;
		}
		for (ssid = group; ssid; ssid = ssid->pnext) {
			if (ssid->disabled) {
				wpa_printf(MSG_DEBUG, "   skip - disabled");
				continue;
			}
			if (ssid->ssid_len != 0 &&
			    (bss->ssid_len != ssid->ssid_len ||
			     os_memcmp(bss->ssid, ssid->ssid,
				       bss->ssid_len) != 0)) {
				wpa_printf(MSG_DEBUG, "   skip - "
					   "SSID mismatch");
				continue;
			}

			if (ssid->bssid_set &&
			    os_memcmp(bss->bssid, ssid->bssid, ETH_ALEN) != 0)
			{
				wpa_printf(MSG_DEBUG, "   skip - "
					   "BSSID mismatch");
				continue;
			}
			
			if (!(ssid->key_mgmt & WPA_KEY_MGMT_NONE) &&
			    !(ssid->key_mgmt & WPA_KEY_MGMT_IEEE8021X_NO_WPA))
			{
				wpa_printf(MSG_DEBUG, "   skip - "
					   "non-WPA network not allowed");
				continue;
			}

			if ((ssid->key_mgmt & 
			     (WPA_KEY_MGMT_IEEE8021X | WPA_KEY_MGMT_PSK)) &&
			    (bss->wpa_ie_len != 0 || bss->rsn_ie_len != 0)) {
				wpa_printf(MSG_DEBUG, "   skip - "
					   "WPA network");
				continue;
			}

			if (!wpa_supplicant_match_privacy(bss, ssid)) {
				wpa_printf(MSG_DEBUG, "   skip - "
					   "privacy mismatch");
				continue;
			}

			if (bss->caps & IEEE80211_CAP_IBSS) {
				wpa_printf(MSG_DEBUG, "   skip - "
					   "IBSS (adhoc) network");
				continue;
			}

			selected = bss;
			*selected_ssid = ssid;
			wpa_printf(MSG_DEBUG, "   selected non-WPA AP "
				   MACSTR " ssid='%s'",
				   MAC2STR(bss->bssid),
				   wpa_ssid_txt(bss->ssid, bss->ssid_len));
			break;
		}
	}

	return selected;
}


static void wpa_supplicant_event_scan_results(struct wpa_supplicant *wpa_s)
{
	int num, prio, timeout;
	struct wpa_scan_result *selected = NULL;
	struct wpa_ssid *ssid = NULL;
	struct wpa_scan_result *results;

	if (wpa_supplicant_get_scan_results(wpa_s) < 0) {
		if (wpa_s->conf->ap_scan == 2)
			return;
		wpa_printf(MSG_DEBUG, "Failed to get scan results - try "
			   "scanning again");
		timeout = 1;
		goto req_scan;
	}

	wpa_supplicant_dbus_notify_scan_results(wpa_s);

	if (wpa_s->conf->ap_scan == 2 || wpa_s->disconnected)
		return;
	results = wpa_s->scan_results;
	num = wpa_s->num_scan_results;

	while (selected == NULL) {
		for (prio = 0; prio < wpa_s->conf->num_prio; prio++) {
			selected = wpa_supplicant_select_bss(
				wpa_s, wpa_s->conf->pssid[prio], results, num,
				&ssid);
			if (selected)
				break;
		}

		if (selected == NULL && wpa_s->blacklist) {
			wpa_printf(MSG_DEBUG, "No APs found - clear blacklist "
				   "and try again");
			wpa_blacklist_clear(wpa_s);
		} else if (selected == NULL) {
			break;
		}
	}

	if (selected) {
		/* Do not trigger new association unless the BSSID has changed
		 * or if reassociation is requested. If we are in process of
		 * associating with the selected BSSID, do not trigger new
		 * attempt. */
		if (wpa_s->reassociate ||
		    (os_memcmp(selected->bssid, wpa_s->bssid, ETH_ALEN) != 0 &&
		     (wpa_s->wpa_state != WPA_ASSOCIATING ||
		      os_memcmp(selected->bssid, wpa_s->pending_bssid,
				ETH_ALEN) != 0))) {
			if (wpa_supplicant_scard_init(wpa_s, ssid)) {
				wpa_supplicant_req_scan(wpa_s, 10, 0);
				return;
			}
			wpa_supplicant_associate(wpa_s, selected, ssid);
		} else {
			wpa_printf(MSG_DEBUG, "Already associated with the "
				   "selected AP.");
		}
		rsn_preauth_scan_results(wpa_s->wpa, results, num);
	} else {
		wpa_printf(MSG_DEBUG, "No suitable AP found.");
		timeout = 5;
		goto req_scan;
	}

	return;

req_scan:
	if (wpa_s->scan_res_tried == 1 && wpa_s->conf->ap_scan == 1) {
		/*
		 * Quick recovery if the initial scan results were not
		 * complete when fetched before the first scan request.
		 */
		wpa_s->scan_res_tried++;
		timeout = 0;
	}
	wpa_supplicant_req_scan(wpa_s, timeout, 0);
}


static void wpa_supplicant_event_associnfo(struct wpa_supplicant *wpa_s,
					   union wpa_event_data *data)
{
	int l, len, found = 0, wpa_found, rsn_found, wapi_found;
	u8 *p;

	wpa_printf(MSG_DEBUG, "Association info event");
	if (data->assoc_info.req_ies)
		wpa_hexdump(MSG_DEBUG, "req_ies", data->assoc_info.req_ies,
			    data->assoc_info.req_ies_len);
	if (data->assoc_info.resp_ies)
		wpa_hexdump(MSG_DEBUG, "resp_ies", data->assoc_info.resp_ies,
			    data->assoc_info.resp_ies_len);
	if (data->assoc_info.beacon_ies)
		wpa_hexdump(MSG_DEBUG, "beacon_ies",
			    data->assoc_info.beacon_ies,
			    data->assoc_info.beacon_ies_len);

	p = data->assoc_info.req_ies;
	l = data->assoc_info.req_ies_len;

	/* Go through the IEs and make a copy of the WPA/RSN IE, if present. */
	while (p && l >= 2) {
		len = p[1] + 2;
		if (len > l) {
			wpa_hexdump(MSG_DEBUG, "Truncated IE in assoc_info",
				    p, l);
			break;
		}
		/* FIXME */
		if (p[0] == WAPI_INFO_ELEM && p[1] >= 2) {
			if (wapi_sm_set_assoc_ie(wpa_s->wapi, p, len))
				break;
			found = 1;
			//wpa_find_assoc_pmkid(wpa_s);
			break;
		}

		if ((p[0] == GENERIC_INFO_ELEM && p[1] >= 6 &&
		     (os_memcmp(&p[2], "\x00\x50\xF2\x01\x01\x00", 6) == 0)) ||
		    (p[0] == RSN_INFO_ELEM && p[1] >= 2)) {
			if (wpa_sm_set_assoc_wpa_ie(wpa_s->wpa, p, len))
				break;
			found = 1;
			wpa_find_assoc_pmkid(wpa_s);
			break;
		}
		l -= len;
		p += len;
	}
	if (!found && data->assoc_info.req_ies) {
		if (wpa_s->key_mgmt == WAPI_KEY_MGMT_PSK || wpa_s->key_mgmt == WAPI_KEY_MGMT_CERT)
			wapi_sm_set_assoc_ie(wpa_s->wapi, NULL, 0);
		else
		wpa_sm_set_assoc_wpa_ie(wpa_s->wpa, NULL, 0);
	}

	/* WPA/RSN/WAPI IE from Beacon/ProbeResp */
	p = data->assoc_info.beacon_ies;
	l = data->assoc_info.beacon_ies_len;

	/* Go through the IEs and make a copy of the WPA/RSN IEs, if present.
	 */
	wpa_found = rsn_found = wapi_found = 0;
	while (p && l >= 2) {
		len = p[1] + 2;
		if (len > l) {
			wpa_hexdump(MSG_DEBUG, "Truncated IE in beacon_ies",
				    p, l);
			break;
		}
		if (!wpa_found &&
		    p[0] == GENERIC_INFO_ELEM && p[1] >= 6 &&
		    os_memcmp(&p[2], "\x00\x50\xF2\x01\x01\x00", 6) == 0) {
			wpa_found = 1;
			wpa_sm_set_ap_wpa_ie(wpa_s->wpa, p, len);
		}

		if (!rsn_found &&
		    p[0] == RSN_INFO_ELEM && p[1] >= 2) {
			rsn_found = 1;
			wpa_sm_set_ap_rsn_ie(wpa_s->wpa, p, len);
		}

		if (!wapi_found &&
		    p[0] == WAPI_INFO_ELEM && p[1] >= 2) {
			wapi_found = 1;
			wapi_sm_set_ap_ie(wpa_s->wapi, p, len);
		}

		l -= len;
		p += len;
	}

	if (!wpa_found && data->assoc_info.beacon_ies)
		wpa_sm_set_ap_wpa_ie(wpa_s->wpa, NULL, 0);
	if (!rsn_found && data->assoc_info.beacon_ies)
		wpa_sm_set_ap_rsn_ie(wpa_s->wpa, NULL, 0);
	if (!wapi_found && data->assoc_info.beacon_ies)
		wapi_sm_set_ap_ie(wpa_s->wapi, NULL, 0);
}

#ifdef USE_WPS
struct wpa_sm *g_wpa = NULL;
int wps_enabled(void);
#endif

static int wapi_sm_init_cert_crypto(void)
{
#if (DE_BUILTIN_WAPI == CFG_INCLUDED)
	wpa_printf(MSG_DEBUG, "WAPI Certificate init");
	/* ECC/X509 Libs init */
	ECC_Init();
	X509_init();
	init_cert();
	return 0;
#else //(DE_BUILTIN_WAPI == CFG_INCLUDED)
	return 1;
#endif //(DE_BUILTIN_WAPI == CFG_INCLUDED)
}
static void wpa_supplicant_event_assoc(struct wpa_supplicant *wpa_s,
				       union wpa_event_data *data)
{
	u8 bssid[ETH_ALEN];

#ifdef EAP_WSC
	if (data && !wpa_s->conf->ssid->use_wps)
	{
		wpa_supplicant_event_associnfo(wpa_s, data);
		g_wpa = wpa_s->wpa;
		wpa_msg(wpa_s, MSG_INFO, "use_wps %d; assigning g_wpa",
				wpa_s->conf->ssid->use_wps);
	}
#else
	if (data)
	{
		wpa_supplicant_event_associnfo(wpa_s, data);
	}
#endif //EAP_WSC

	wpa_supplicant_set_state(wpa_s, WPA_ASSOCIATED);
	if (wpa_s->use_client_mlme)
		os_memcpy(bssid, wpa_s->bssid, ETH_ALEN);
	if (wpa_s->use_client_mlme ||
	    (wpa_drv_get_bssid(wpa_s, bssid) >= 0 &&
	     os_memcmp(bssid, wpa_s->bssid, ETH_ALEN) != 0)) {
		wpa_msg(wpa_s, MSG_DEBUG, "Associated to a new BSS: BSSID="
			MACSTR, MAC2STR(bssid));
		os_memcpy(wpa_s->bssid, bssid, ETH_ALEN);
		os_memset(wpa_s->pending_bssid, 0, ETH_ALEN);
		if (wpa_supplicant_dynamic_keys(wpa_s)) {
			wpa_clear_keys(wpa_s, bssid);
		}
		if (wpa_supplicant_select_config(wpa_s) < 0) {
			wpa_supplicant_disassociate(wpa_s,
						    REASON_DEAUTH_LEAVING);
			return;
		}
	}

	wpa_msg(wpa_s, MSG_INFO, "Associated with " MACSTR, MAC2STR(bssid));
	if (wpa_s->current_ssid) {
		/* When using scanning (ap_scan=1), SIM PC/SC interface can be
		 * initialized before association, but for other modes,
		 * initialize PC/SC here, if the current configuration needs
		 * smartcard or SIM/USIM. */
		wpa_supplicant_scard_init(wpa_s, wpa_s->current_ssid);
	}
        if (wpa_s->key_mgmt == WAPI_KEY_MGMT_PSK || wpa_s->key_mgmt == WAPI_KEY_MGMT_CERT)
		wapi_sm_notify_assoc(wpa_s->wapi, bssid);
	else
		wpa_sm_notify_assoc(wpa_s->wpa, bssid);

	if (wpa_s->key_mgmt == WAPI_KEY_MGMT_CERT)
		if(wapi_sm_init_cert_crypto()) {
			wpa_printf(MSG_DEBUG, "ERROR: WAPI Certificate Initialization failed. Aborting...");
			wpa_supplicant_disassociate(wpa_s,
                                                    REASON_DEAUTH_LEAVING);
			return;
		}
			

	l2_packet_notify_auth_start(wpa_s->l2);

#ifdef USE_WPS
	/* This is used for WPS+Open network settings */
	if (wps_enabled() && (wpa_s->conf->ssid->key_mgmt == WPA_KEY_MGMT_NONE)) {
		wpa_supplicant_cancel_auth_timeout(wpa_s);
		wpa_supplicant_set_state(wpa_s, WPA_COMPLETED);
		wpa_supplicant_cancel_scan(wpa_s);
		return;
	}
#endif
	/*
	 * Set portEnabled first to FALSE in order to get EAP state machine out
	 * of the SUCCESS state and eapSuccess cleared. Without this, EAPOL PAE
	 * state machine may transit to AUTHENTICATING state based on obsolete
	 * eapSuccess and then trigger BE_AUTH to SUCCESS and PAE to
	 * AUTHENTICATED without ever giving chance to EAP state machine to
	 * reset the state.
	 */
	eapol_sm_notify_portEnabled(wpa_s->eapol, FALSE);
	eapol_sm_notify_portValid(wpa_s->eapol, FALSE);
	if ((wpa_s->key_mgmt == WPA_KEY_MGMT_PSK) || (wpa_s->key_mgmt == WAPI_KEY_MGMT_PSK))
		eapol_sm_notify_eap_success(wpa_s->eapol, FALSE);
	/* 802.1X::portControl = Auto */
	eapol_sm_notify_portEnabled(wpa_s->eapol, TRUE);
	wpa_s->eapol_received = 0;
	if (wpa_s->key_mgmt == WPA_KEY_MGMT_NONE ||
	    wpa_s->key_mgmt == WPA_KEY_MGMT_WPA_NONE) {
		wpa_supplicant_cancel_auth_timeout(wpa_s);
		wpa_supplicant_set_state(wpa_s, WPA_COMPLETED);
	} else {
		/* Timeout for receiving the first EAPOL packet */
		wpa_supplicant_req_auth_timeout(wpa_s, 10, 0);
	}
	wpa_supplicant_cancel_scan(wpa_s);
}


static void wpa_supplicant_event_disassoc(struct wpa_supplicant *wpa_s)
{
	const u8 *bssid;

	if (wpa_s->key_mgmt == WPA_KEY_MGMT_WPA_NONE) {
		/*
		 * At least Host AP driver and a Prism3 card seemed to be
		 * generating streams of disconnected events when configuring
		 * IBSS for WPA-None. Ignore them for now.
		 */
		wpa_printf(MSG_DEBUG, "Disconnect event - ignore in "
			   "IBSS/WPA-None mode");
		return;
	}

	if (wpa_s->wpa_state == WPA_4WAY_HANDSHAKE &&
	    ( wpa_s->key_mgmt == WPA_KEY_MGMT_PSK || wpa_s->key_mgmt == WAPI_KEY_MGMT_PSK)) {
		wpa_msg(wpa_s, MSG_INFO, "WPA/WAPI: 4-Way Handshake failed - "
			"pre-shared key may be incorrect");
	}

	if ( ( wpa_s->conf->ap_scan > 0) 
     && (wpa_s->wpa_state >= WPA_ASSOCIATED) )
	{
		wpa_supplicant_req_scan(wpa_s, 0, 100000);
	}
	bssid = wpa_s->bssid;
	if (os_memcmp(bssid, "\x00\x00\x00\x00\x00\x00", ETH_ALEN) == 0)
	{
		bssid = wpa_s->pending_bssid;
	}
   wpa_blacklist_add(wpa_s, bssid);
   if (wpa_s->key_mgmt == WAPI_KEY_MGMT_PSK || wpa_s->key_mgmt == WAPI_KEY_MGMT_CERT) {
	   if((wpa_s->key_mgmt == WAPI_KEY_MGMT_CERT) && (wpa_s->wpa_state >= WPA_ASSOCIATED)) {
			wpa_printf(MSG_DEBUG, "Cleanup certificate objs");
			cleanup_cert();
			X509_exit();
		}
		wapi_sm_notify_disassoc(wpa_s->wapi);
	}
	else
	   wpa_sm_notify_disassoc(wpa_s->wpa);
	wpa_msg(wpa_s, MSG_INFO, WPA_EVENT_DISCONNECTED "- Disconnect event - "
		"remove keys");
	if (wpa_supplicant_dynamic_keys(wpa_s)) {
		wpa_s->keys_cleared = 0;
		wpa_clear_keys(wpa_s, wpa_s->bssid);
	}
	wpa_supplicant_mark_disassoc(wpa_s);
}


static void
wpa_supplicant_event_michael_mic_failure(struct wpa_supplicant *wpa_s,
					 union wpa_event_data *data)
{
	int pairwise;
	struct os_time t;

	wpa_msg(wpa_s, MSG_WARNING, "Michael MIC failure detected");
	pairwise = (data && data->michael_mic_failure.unicast);
	wpa_sm_key_request(wpa_s->wpa, 1, pairwise);
	os_get_time(&t);
	if (wpa_s->last_michael_mic_error &&
	    t.sec - wpa_s->last_michael_mic_error <= 60) {
		/* initialize countermeasures */
		wpa_s->countermeasures = 1;
		wpa_msg(wpa_s, MSG_WARNING, "TKIP countermeasures started");

		wpa_drv_set_countermeasures(wpa_s, 1);
		eloop_cancel_timeout(wpa_supplicant_stop_countermeasures,
				     wpa_s, NULL);
		eloop_register_timeout(60, 0,
				       wpa_supplicant_stop_countermeasures,
				       wpa_s, NULL);
		/* TODO: mark the AP rejected for 60 second. STA is
		 * allowed to associate with another AP.. */
	}
	wpa_s->last_michael_mic_error = t.sec;
}


static void
wpa_supplicant_event_interface_status(struct wpa_supplicant *wpa_s,
				      union wpa_event_data *data)
{
	if (os_strcmp(wpa_s->ifname, data->interface_status.ifname) != 0)
		return;

	switch (data->interface_status.ievent) {
	case EVENT_INTERFACE_ADDED:
		if (!wpa_s->interface_removed)
			break;
		wpa_s->interface_removed = 0;
		wpa_printf(MSG_DEBUG, "Configured interface was added.");
		if (wpa_supplicant_driver_init(wpa_s, 1) < 0) {
			wpa_printf(MSG_INFO, "Failed to initialize the driver "
				   "after interface was added.");
		}
		break;
	case EVENT_INTERFACE_REMOVED:
		wpa_printf(MSG_DEBUG, "Configured interface was removed.");
		wpa_s->interface_removed = 1;
		wpa_supplicant_mark_disassoc(wpa_s);
		l2_packet_deinit(wpa_s->l2);
		wpa_s->l2 = NULL;
		break;
	}
}


#ifdef CONFIG_PEERKEY
static void
wpa_supplicant_event_stkstart(struct wpa_supplicant *wpa_s,
			      union wpa_event_data *data)
{
	if (data == NULL)
		return;
	wpa_sm_stkstart(wpa_s->wpa, data->stkstart.peer);
}
#endif /* CONFIG_PEERKEY */


void wpa_supplicant_event(struct wpa_supplicant *wpa_s, wpa_event_type event,
			  union wpa_event_data *data)
{
	switch (event) {
	case EVENT_ASSOC:
		wpa_supplicant_event_assoc(wpa_s, data);
		break;
	case EVENT_DISASSOC:
		wpa_supplicant_event_disassoc(wpa_s);
		break;
	case EVENT_MICHAEL_MIC_FAILURE:
		wpa_supplicant_event_michael_mic_failure(wpa_s, data);
		break;
	case EVENT_SCAN_RESULTS:
		wpa_supplicant_event_scan_results(wpa_s);
		break;
	case EVENT_ASSOCINFO:
		wpa_supplicant_event_associnfo(wpa_s, data);
		break;
	case EVENT_INTERFACE_STATUS:
		wpa_supplicant_event_interface_status(wpa_s, data);
		break;
	case EVENT_PMKID_CANDIDATE:
		wpa_supplicant_event_pmkid_candidate(wpa_s, data);
		break;
#ifdef CONFIG_PEERKEY
	case EVENT_STKSTART:
		wpa_supplicant_event_stkstart(wpa_s, data);
		break;
#endif /* CONFIG_PEERKEY */
	default:
		wpa_printf(MSG_INFO, "Unknown event %d", event);
		break;
	}
}

/* Local Variables: */
/* c-basic-offset: 8 */
/* indent-tabs-mode: t */
/* End: */
