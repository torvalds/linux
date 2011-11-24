/*
 * WPA Supplicant - RSN pre-authentication
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
#include "wpa.h"
#include "driver.h"
#include "eloop.h"
#include "config.h"
#include "l2_packet.h"
#include "eapol_sm.h"
#include "preauth.h"
#include "pmksa_cache.h"
#include "wpa_i.h"


#define PMKID_CANDIDATE_PRIO_SCAN 1000


struct rsn_pmksa_candidate {
	struct rsn_pmksa_candidate *next;
	u8 bssid[ETH_ALEN];
	int priority;
};


/**
 * pmksa_candidate_free - Free all entries in PMKSA candidate list
 * @sm: Pointer to WPA state machine data from wpa_sm_init()
 */
void pmksa_candidate_free(struct wpa_sm *sm)
{
	struct rsn_pmksa_candidate *entry, *prev;

	if (sm == NULL)
		return;

	entry = sm->pmksa_candidates;
	sm->pmksa_candidates = NULL;
	while (entry) {
		prev = entry;
		entry = entry->next;
		os_free(prev);
	}
}


#if defined(IEEE8021X_EAPOL) && !defined(CONFIG_NO_WPA2)

static void rsn_preauth_receive(void *ctx, const u8 *src_addr,
				const u8 *buf, size_t len)
{
	struct wpa_sm *sm = ctx;

	wpa_printf(MSG_DEBUG, "RX pre-auth from " MACSTR, MAC2STR(src_addr));
	wpa_hexdump(MSG_MSGDUMP, "RX pre-auth", buf, len);

	if (sm->preauth_eapol == NULL ||
	    os_memcmp(sm->preauth_bssid, "\x00\x00\x00\x00\x00\x00",
		      ETH_ALEN) == 0 ||
	    os_memcmp(sm->preauth_bssid, src_addr, ETH_ALEN) != 0) {
		wpa_printf(MSG_WARNING, "RSN pre-auth frame received from "
			   "unexpected source " MACSTR " - dropped",
			   MAC2STR(src_addr));
		return;
	}

	eapol_sm_rx_eapol(sm->preauth_eapol, src_addr, buf, len);
}


static void rsn_preauth_eapol_cb(struct eapol_sm *eapol, int success,
				 void *ctx)
{
	struct wpa_sm *sm = ctx;
	u8 pmk[PMK_LEN];

	if (success) {
		int res, pmk_len;
		pmk_len = PMK_LEN;
		res = eapol_sm_get_key(eapol, pmk, PMK_LEN);
		if (res) {
			/*
			 * EAP-LEAP is an exception from other EAP methods: it
			 * uses only 16-byte PMK.
			 */
			res = eapol_sm_get_key(eapol, pmk, 16);
			pmk_len = 16;
		}
		if (res == 0) {
			wpa_hexdump_key(MSG_DEBUG, "RSN: PMK from pre-auth",
					pmk, pmk_len);
			sm->pmk_len = pmk_len;
			pmksa_cache_add(sm->pmksa, pmk, pmk_len,
					sm->preauth_bssid, sm->own_addr,
					sm->cur_ssid);
		} else {
			wpa_msg(sm->ctx->ctx, MSG_INFO, "RSN: failed to get "
				"master session key from pre-auth EAPOL state "
				"machines");
			success = 0;
		}
	}

	wpa_msg(sm->ctx->ctx, MSG_INFO, "RSN: pre-authentication with " MACSTR
		" %s", MAC2STR(sm->preauth_bssid),
		success ? "completed successfully" : "failed");

	rsn_preauth_deinit(sm);
	rsn_preauth_candidate_process(sm);
}


static void rsn_preauth_timeout(void *eloop_ctx, void *timeout_ctx)
{
	struct wpa_sm *sm = eloop_ctx;

	wpa_msg(sm->ctx->ctx, MSG_INFO, "RSN: pre-authentication with " MACSTR
		" timed out", MAC2STR(sm->preauth_bssid));
	rsn_preauth_deinit(sm);
	rsn_preauth_candidate_process(sm);
}


static int rsn_preauth_eapol_send(void *ctx, int type, const u8 *buf,
				  size_t len)
{
	struct wpa_sm *sm = ctx;
	u8 *msg;
	size_t msglen;
	int res;

	/* TODO: could add l2_packet_sendmsg that allows fragments to avoid
	 * extra copy here */

	if (sm->l2_preauth == NULL)
		return -1;

	msg = wpa_sm_alloc_eapol(sm, type, buf, len, &msglen, NULL);
	if (msg == NULL)
		return -1;

	wpa_hexdump(MSG_MSGDUMP, "TX EAPOL (preauth)", msg, msglen);
	res = l2_packet_send(sm->l2_preauth, sm->preauth_bssid,
			     ETH_P_RSN_PREAUTH, msg, msglen);
	os_free(msg);
	return res;
}


/**
 * rsn_preauth_init - Start new RSN pre-authentication
 * @sm: Pointer to WPA state machine data from wpa_sm_init()
 * @dst: Authenticator address (BSSID) with which to preauthenticate
 * @config: Current network configuration
 * Returns: 0 on success, -1 on another pre-authentication is in progress,
 * -2 on layer 2 packet initialization failure, -3 on EAPOL state machine
 * initialization failure, -4 on memory allocation failure
 *
 * This function request an RSN pre-authentication with a given destination
 * address. This is usually called for PMKSA candidates found from scan results
 * or from driver reports. In addition, ctrl_iface PREAUTH command can trigger
 * pre-authentication.
 */
int rsn_preauth_init(struct wpa_sm *sm, const u8 *dst, struct wpa_ssid *config)
{
	struct eapol_config eapol_conf;
	struct eapol_ctx *ctx;

	if (sm->preauth_eapol)
		return -1;

	wpa_msg(sm->ctx->ctx, MSG_DEBUG, "RSN: starting pre-authentication "
		"with " MACSTR, MAC2STR(dst));

	sm->l2_preauth = l2_packet_init(sm->ifname, sm->own_addr,
					ETH_P_RSN_PREAUTH,
					rsn_preauth_receive, sm, 0);
	if (sm->l2_preauth == NULL) {
		wpa_printf(MSG_WARNING, "RSN: Failed to initialize L2 packet "
			   "processing for pre-authentication");
		return -2;
	}

	if (sm->bridge_ifname) {
		sm->l2_preauth_br = l2_packet_init(sm->bridge_ifname,
						   sm->own_addr,
						   ETH_P_RSN_PREAUTH,
						   rsn_preauth_receive, sm, 0);
		if (sm->l2_preauth_br == NULL) {
			wpa_printf(MSG_WARNING, "RSN: Failed to initialize L2 "
				   "packet processing (bridge) for "
				   "pre-authentication");
			return -2;
		}
	}

	ctx = os_zalloc(sizeof(*ctx));
	if (ctx == NULL) {
		wpa_printf(MSG_WARNING, "Failed to allocate EAPOL context.");
		return -4;
	}
	ctx->ctx = sm->ctx->ctx;
	ctx->msg_ctx = sm->ctx->ctx;
	ctx->preauth = 1;
	ctx->cb = rsn_preauth_eapol_cb;
	ctx->cb_ctx = sm;
	ctx->scard_ctx = sm->scard_ctx;
	ctx->eapol_send = rsn_preauth_eapol_send;
	ctx->eapol_send_ctx = sm;
	ctx->set_config_blob = sm->ctx->set_config_blob;
	ctx->get_config_blob = sm->ctx->get_config_blob;

	sm->preauth_eapol = eapol_sm_init(ctx);
	if (sm->preauth_eapol == NULL) {
		os_free(ctx);
		wpa_printf(MSG_WARNING, "RSN: Failed to initialize EAPOL "
			   "state machines for pre-authentication");
		return -3;
	}
	os_memset(&eapol_conf, 0, sizeof(eapol_conf));
	eapol_conf.accept_802_1x_keys = 0;
	eapol_conf.required_keys = 0;
	eapol_conf.fast_reauth = sm->fast_reauth;
	if (config)
		eapol_conf.workaround = config->eap_workaround;
	eapol_sm_notify_config(sm->preauth_eapol, config, &eapol_conf);
	/*
	 * Use a shorter startPeriod with preauthentication since the first
	 * preauth EAPOL-Start frame may end up being dropped due to race
	 * condition in the AP between the data receive and key configuration
	 * after the 4-Way Handshake.
	 */
	eapol_sm_configure(sm->preauth_eapol, -1, -1, 5, 6);
	os_memcpy(sm->preauth_bssid, dst, ETH_ALEN);

	eapol_sm_notify_portValid(sm->preauth_eapol, TRUE);
	/* 802.1X::portControl = Auto */
	eapol_sm_notify_portEnabled(sm->preauth_eapol, TRUE);

	eloop_register_timeout(sm->dot11RSNAConfigSATimeout, 0,
			       rsn_preauth_timeout, sm, NULL);

	return 0;
}


/**
 * rsn_preauth_deinit - Abort RSN pre-authentication
 * @sm: Pointer to WPA state machine data from wpa_sm_init()
 *
 * This function aborts the current RSN pre-authentication (if one is started)
 * and frees resources allocated for it.
 */
void rsn_preauth_deinit(struct wpa_sm *sm)
{
	if (sm == NULL || !sm->preauth_eapol)
		return;

	eloop_cancel_timeout(rsn_preauth_timeout, sm, NULL);
	eapol_sm_deinit(sm->preauth_eapol);
	sm->preauth_eapol = NULL;
	os_memset(sm->preauth_bssid, 0, ETH_ALEN);

	l2_packet_deinit(sm->l2_preauth);
	sm->l2_preauth = NULL;
	if (sm->l2_preauth_br) {
		l2_packet_deinit(sm->l2_preauth_br);
		sm->l2_preauth_br = NULL;
	}
}


/**
 * rsn_preauth_candidate_process - Process PMKSA candidates
 * @sm: Pointer to WPA state machine data from wpa_sm_init()
 *
 * Go through the PMKSA candidates and start pre-authentication if a candidate
 * without an existing PMKSA cache entry is found. Processed candidates will be
 * removed from the list.
 */
void rsn_preauth_candidate_process(struct wpa_sm *sm)
{
	struct rsn_pmksa_candidate *candidate;

	if (sm->pmksa_candidates == NULL)
		return;

	/* TODO: drop priority for old candidate entries */

	wpa_msg(sm->ctx->ctx, MSG_DEBUG, "RSN: processing PMKSA candidate "
		"list");
	if (sm->preauth_eapol ||
	    sm->proto != WPA_PROTO_RSN ||
	    wpa_sm_get_state(sm) != WPA_COMPLETED ||
	    sm->key_mgmt != WPA_KEY_MGMT_IEEE8021X) {
		wpa_msg(sm->ctx->ctx, MSG_DEBUG, "RSN: not in suitable state "
			"for new pre-authentication");
		return; /* invalid state for new pre-auth */
	}

	while (sm->pmksa_candidates) {
		struct rsn_pmksa_cache_entry *p = NULL;
		candidate = sm->pmksa_candidates;
		p = pmksa_cache_get(sm->pmksa, candidate->bssid, NULL);
		if (os_memcmp(sm->bssid, candidate->bssid, ETH_ALEN) != 0 &&
		    (p == NULL || p->opportunistic)) {
			wpa_msg(sm->ctx->ctx, MSG_DEBUG, "RSN: PMKSA "
				"candidate " MACSTR
				" selected for pre-authentication",
				MAC2STR(candidate->bssid));
			sm->pmksa_candidates = candidate->next;
			rsn_preauth_init(sm, candidate->bssid, sm->cur_ssid);
			os_free(candidate);
			return;
		}
		wpa_msg(sm->ctx->ctx, MSG_DEBUG, "RSN: PMKSA candidate "
			MACSTR " does not need pre-authentication anymore",
			MAC2STR(candidate->bssid));
		/* Some drivers (e.g., NDIS) expect to get notified about the
		 * PMKIDs again, so report the existing data now. */
		if (p) {
			wpa_sm_add_pmkid(sm, candidate->bssid, p->pmkid);
		}

		sm->pmksa_candidates = candidate->next;
		os_free(candidate);
	}
	wpa_msg(sm->ctx->ctx, MSG_DEBUG, "RSN: no more pending PMKSA "
		"candidates");
}


/**
 * pmksa_candidate_add - Add a new PMKSA candidate
 * @sm: Pointer to WPA state machine data from wpa_sm_init()
 * @bssid: BSSID (authenticator address) of the candidate
 * @prio: Priority (the smaller number, the higher priority)
 * @preauth: Whether the candidate AP advertises support for pre-authentication
 *
 * This function is used to add PMKSA candidates for RSN pre-authentication. It
 * is called from scan result processing and from driver events for PMKSA
 * candidates, i.e., EVENT_PMKID_CANDIDATE events to wpa_supplicant_event().
 */
void pmksa_candidate_add(struct wpa_sm *sm, const u8 *bssid,
			 int prio, int preauth)
{
	struct rsn_pmksa_candidate *cand, *prev, *pos;

	if (sm->cur_ssid && sm->cur_ssid->proactive_key_caching)
		pmksa_cache_get_opportunistic(sm->pmksa, sm->cur_ssid, bssid);

	if (!preauth) {
		wpa_printf(MSG_DEBUG, "RSN: Ignored PMKID candidate without "
			   "preauth flag");
		return;
	}

	/* If BSSID already on candidate list, update the priority of the old
	 * entry. Do not override priority based on normal scan results. */
	prev = NULL;
	cand = sm->pmksa_candidates;
	while (cand) {
		if (os_memcmp(cand->bssid, bssid, ETH_ALEN) == 0) {
			if (prev)
				prev->next = cand->next;
			else
				sm->pmksa_candidates = cand->next;
			break;
		}
		prev = cand;
		cand = cand->next;
	}

	if (cand) {
		if (prio < PMKID_CANDIDATE_PRIO_SCAN)
			cand->priority = prio;
	} else {
		cand = os_zalloc(sizeof(*cand));
		if (cand == NULL)
			return;
		os_memcpy(cand->bssid, bssid, ETH_ALEN);
		cand->priority = prio;
	}

	/* Add candidate to the list; order by increasing priority value. i.e.,
	 * highest priority (smallest value) first. */
	prev = NULL;
	pos = sm->pmksa_candidates;
	while (pos) {
		if (cand->priority <= pos->priority)
			break;
		prev = pos;
		pos = pos->next;
	}
	cand->next = pos;
	if (prev)
		prev->next = cand;
	else
		sm->pmksa_candidates = cand;

	wpa_msg(sm->ctx->ctx, MSG_DEBUG, "RSN: added PMKSA cache "
		"candidate " MACSTR " prio %d", MAC2STR(bssid), prio);
	rsn_preauth_candidate_process(sm);
}


/* TODO: schedule periodic scans if current AP supports preauth */

/**
 * rsn_preauth_scan_results - Process scan results to find PMKSA candidates
 * @sm: Pointer to WPA state machine data from wpa_sm_init()
 * @results: Scan results
 * @count: Number of BSSes in scan results
 *
 * This functions goes through the scan results and adds all suitable APs
 * (Authenticators) into PMKSA candidate list.
 */
void rsn_preauth_scan_results(struct wpa_sm *sm,
			      struct wpa_scan_result *results, int count)
{
	struct wpa_scan_result *r;
	struct wpa_ie_data ie;
	int i;
	struct rsn_pmksa_cache_entry *pmksa;

	if (sm->cur_ssid == NULL)
		return;

	/*
	 * TODO: is it ok to free all candidates? What about the entries
	 * received from EVENT_PMKID_CANDIDATE?
	 */
	pmksa_candidate_free(sm);

	for (i = count - 1; i >= 0; i--) {
		r = &results[i];
		if (r->ssid_len != sm->cur_ssid->ssid_len ||
		    os_memcmp(r->ssid, sm->cur_ssid->ssid,
			      r->ssid_len) != 0)
			continue;

		if (os_memcmp(r->bssid, sm->bssid, ETH_ALEN) == 0)
			continue;

		if (r->rsn_ie_len == 0 ||
		    wpa_parse_wpa_ie(r->rsn_ie, r->rsn_ie_len, &ie))
			continue;

		pmksa = pmksa_cache_get(sm->pmksa, r->bssid, NULL);
		if (pmksa &&
		    (!pmksa->opportunistic ||
		     !(ie.capabilities & WPA_CAPABILITY_PREAUTH)))
			continue;

		/*
		 * Give less priority to candidates found from normal
		 * scan results.
		 */
		pmksa_candidate_add(sm, r->bssid,
				    PMKID_CANDIDATE_PRIO_SCAN,
				    ie.capabilities & WPA_CAPABILITY_PREAUTH);
	}
}


#ifdef CONFIG_CTRL_IFACE
/**
 * rsn_preauth_get_status - Get pre-authentication status
 * @sm: Pointer to WPA state machine data from wpa_sm_init()
 * @buf: Buffer for status information
 * @buflen: Maximum buffer length
 * @verbose: Whether to include verbose status information
 * Returns: Number of bytes written to buf.
 *
 * Query WPA2 pre-authentication for status information. This function fills in
 * a text area with current status information. If the buffer (buf) is not
 * large enough, status information will be truncated to fit the buffer.
 */
int rsn_preauth_get_status(struct wpa_sm *sm, char *buf, size_t buflen,
			   int verbose)
{
	char *pos = buf, *end = buf + buflen;
	int res, ret;

	if (sm->preauth_eapol) {
		ret = os_snprintf(pos, end - pos, "Pre-authentication "
				  "EAPOL state machines:\n");
		if (ret < 0 || ret >= end - pos)
			return pos - buf;
		pos += ret;
		res = eapol_sm_get_status(sm->preauth_eapol,
					  pos, end - pos, verbose);
		if (res >= 0)
			pos += res;
	}

	return pos - buf;
}
#endif /* CONFIG_CTRL_IFACE */


/**
 * rsn_preauth_in_progress - Verify whether pre-authentication is in progress
 * @sm: Pointer to WPA state machine data from wpa_sm_init()
 */
int rsn_preauth_in_progress(struct wpa_sm *sm)
{
	return sm->preauth_eapol != NULL;
}

#endif /* IEEE8021X_EAPOL and !CONFIG_NO_WPA2 */
