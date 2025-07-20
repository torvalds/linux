// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2002-2005, Instant802 Networks, Inc.
 * Copyright 2005-2006, Devicescape Software, Inc.
 * Copyright 2006-2007	Jiri Benc <jbenc@suse.cz>
 * Copyright 2007	Johannes Berg <johannes@sipsolutions.net>
 * Copyright 2013-2014  Intel Mobile Communications GmbH
 * Copyright (C) 2015-2017	Intel Deutschland GmbH
 * Copyright (C) 2018-2024 Intel Corporation
 *
 * element parsing for mac80211
 */

#include <net/mac80211.h>
#include <linux/netdevice.h>
#include <linux/export.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/etherdevice.h>
#include <linux/if_arp.h>
#include <linux/bitmap.h>
#include <linux/crc32.h>
#include <net/net_namespace.h>
#include <net/cfg80211.h>
#include <net/rtnetlink.h>
#include <kunit/visibility.h>

#include "ieee80211_i.h"
#include "driver-ops.h"
#include "rate.h"
#include "mesh.h"
#include "wme.h"
#include "led.h"
#include "wep.h"

struct ieee80211_elems_parse {
	/* must be first for kfree to work */
	struct ieee802_11_elems elems;

	/* The basic Multi-Link element in the original elements */
	const struct element *ml_basic_elem;

	/* The reconfiguration Multi-Link element in the original elements */
	const struct element *ml_reconf_elem;

	/* The EPCS Multi-Link element in the original elements */
	const struct element *ml_epcs_elem;

	bool multi_link_inner;
	bool skip_vendor;

	/*
	 * scratch buffer that can be used for various element parsing related
	 * tasks, e.g., element de-fragmentation etc.
	 */
	size_t scratch_len;
	u8 *scratch_pos;
	u8 scratch[] __counted_by(scratch_len);
};

static void
ieee80211_parse_extension_element(u32 *crc,
				  const struct element *elem,
				  struct ieee80211_elems_parse *elems_parse,
				  struct ieee80211_elems_parse_params *params)
{
	struct ieee802_11_elems *elems = &elems_parse->elems;
	const void *data = elem->data + 1;
	bool calc_crc = false;
	u8 len;

	if (!elem->datalen)
		return;

	len = elem->datalen - 1;

	switch (elem->data[0]) {
	case WLAN_EID_EXT_HE_MU_EDCA:
		if (params->mode < IEEE80211_CONN_MODE_HE)
			break;
		calc_crc = true;
		if (len >= sizeof(*elems->mu_edca_param_set))
			elems->mu_edca_param_set = data;
		break;
	case WLAN_EID_EXT_HE_CAPABILITY:
		if (params->mode < IEEE80211_CONN_MODE_HE)
			break;
		if (ieee80211_he_capa_size_ok(data, len)) {
			elems->he_cap = data;
			elems->he_cap_len = len;
		}
		break;
	case WLAN_EID_EXT_HE_OPERATION:
		if (params->mode < IEEE80211_CONN_MODE_HE)
			break;
		calc_crc = true;
		if (len >= sizeof(*elems->he_operation) &&
		    len >= ieee80211_he_oper_size(data) - 1)
			elems->he_operation = data;
		break;
	case WLAN_EID_EXT_UORA:
		if (params->mode < IEEE80211_CONN_MODE_HE)
			break;
		if (len >= 1)
			elems->uora_element = data;
		break;
	case WLAN_EID_EXT_MAX_CHANNEL_SWITCH_TIME:
		if (len == 3)
			elems->max_channel_switch_time = data;
		break;
	case WLAN_EID_EXT_MULTIPLE_BSSID_CONFIGURATION:
		if (len >= sizeof(*elems->mbssid_config_ie))
			elems->mbssid_config_ie = data;
		break;
	case WLAN_EID_EXT_HE_SPR:
		if (params->mode < IEEE80211_CONN_MODE_HE)
			break;
		if (len >= sizeof(*elems->he_spr) &&
		    len >= ieee80211_he_spr_size(data) - 1)
			elems->he_spr = data;
		break;
	case WLAN_EID_EXT_HE_6GHZ_CAPA:
		if (params->mode < IEEE80211_CONN_MODE_HE)
			break;
		if (len >= sizeof(*elems->he_6ghz_capa))
			elems->he_6ghz_capa = data;
		break;
	case WLAN_EID_EXT_EHT_CAPABILITY:
		if (params->mode < IEEE80211_CONN_MODE_EHT)
			break;
		if (ieee80211_eht_capa_size_ok(elems->he_cap,
					       data, len,
					       params->from_ap)) {
			elems->eht_cap = data;
			elems->eht_cap_len = len;
		}
		break;
	case WLAN_EID_EXT_EHT_OPERATION:
		if (params->mode < IEEE80211_CONN_MODE_EHT)
			break;
		if (ieee80211_eht_oper_size_ok(data, len))
			elems->eht_operation = data;
		calc_crc = true;
		break;
	case WLAN_EID_EXT_EHT_MULTI_LINK:
		if (params->mode < IEEE80211_CONN_MODE_EHT)
			break;
		calc_crc = true;

		if (ieee80211_mle_size_ok(data, len)) {
			const struct ieee80211_multi_link_elem *mle =
				(void *)data;

			switch (le16_get_bits(mle->control,
					      IEEE80211_ML_CONTROL_TYPE)) {
			case IEEE80211_ML_CONTROL_TYPE_BASIC:
				if (elems_parse->multi_link_inner) {
					elems->parse_error |=
						IEEE80211_PARSE_ERR_DUP_NEST_ML_BASIC;
					break;
				}
				break;
			case IEEE80211_ML_CONTROL_TYPE_RECONF:
				elems_parse->ml_reconf_elem = elem;
				break;
			case IEEE80211_ML_CONTROL_TYPE_PRIO_ACCESS:
				elems_parse->ml_epcs_elem = elem;
				break;
			default:
				break;
			}
		}
		break;
	case WLAN_EID_EXT_BANDWIDTH_INDICATION:
		if (params->mode < IEEE80211_CONN_MODE_EHT)
			break;
		if (ieee80211_bandwidth_indication_size_ok(data, len))
			elems->bandwidth_indication = data;
		calc_crc = true;
		break;
	case WLAN_EID_EXT_TID_TO_LINK_MAPPING:
		if (params->mode < IEEE80211_CONN_MODE_EHT)
			break;
		calc_crc = true;
		if (ieee80211_tid_to_link_map_size_ok(data, len) &&
		    elems->ttlm_num < ARRAY_SIZE(elems->ttlm)) {
			elems->ttlm[elems->ttlm_num] = (void *)data;
			elems->ttlm_num++;
		}
		break;
	}

	if (crc && calc_crc)
		*crc = crc32_be(*crc, (void *)elem, elem->datalen + 2);
}

static void ieee80211_parse_tpe(struct ieee80211_parsed_tpe *tpe,
				const u8 *data, u8 len)
{
	const struct ieee80211_tx_pwr_env *env = (const void *)data;
	u8 count, interpret, category;
	u8 *out, N, *cnt_out = NULL, *N_out = NULL;

	if (!ieee80211_valid_tpe_element(data, len))
		return;

	count = u8_get_bits(env->info, IEEE80211_TX_PWR_ENV_INFO_COUNT);
	interpret = u8_get_bits(env->info, IEEE80211_TX_PWR_ENV_INFO_INTERPRET);
	category = u8_get_bits(env->info, IEEE80211_TX_PWR_ENV_INFO_CATEGORY);

	switch (interpret) {
	case IEEE80211_TPE_LOCAL_EIRP:
		out = tpe->max_local[category].power;
		cnt_out = &tpe->max_local[category].count;
		tpe->max_local[category].valid = true;
		break;
	case IEEE80211_TPE_REG_CLIENT_EIRP:
		out = tpe->max_reg_client[category].power;
		cnt_out = &tpe->max_reg_client[category].count;
		tpe->max_reg_client[category].valid = true;
		break;
	case IEEE80211_TPE_LOCAL_EIRP_PSD:
		out = tpe->psd_local[category].power;
		cnt_out = &tpe->psd_local[category].count;
		N_out = &tpe->psd_local[category].n;
		tpe->psd_local[category].valid = true;
		break;
	case IEEE80211_TPE_REG_CLIENT_EIRP_PSD:
		out = tpe->psd_reg_client[category].power;
		cnt_out = &tpe->psd_reg_client[category].count;
		N_out = &tpe->psd_reg_client[category].n;
		tpe->psd_reg_client[category].valid = true;
		break;
	}

	switch (interpret) {
	case IEEE80211_TPE_LOCAL_EIRP:
	case IEEE80211_TPE_REG_CLIENT_EIRP:
		/* count was validated <= 3, plus 320 MHz */
		BUILD_BUG_ON(IEEE80211_TPE_EIRP_ENTRIES_320MHZ < 5);
		memcpy(out, env->variable, count + 1);
		*cnt_out = count + 1;
		/* separately take 320 MHz if present */
		if (count == 3 && len > sizeof(*env) + count + 1) {
			out[4] = env->variable[4];
			*cnt_out = 5;
		}
		break;
	case IEEE80211_TPE_LOCAL_EIRP_PSD:
	case IEEE80211_TPE_REG_CLIENT_EIRP_PSD:
		if (!count) {
			memset(out, env->variable[0],
			       IEEE80211_TPE_PSD_ENTRIES_320MHZ);
			*cnt_out = IEEE80211_TPE_PSD_ENTRIES_320MHZ;
			break;
		}

		N = 1 << (count - 1);
		memcpy(out, env->variable, N);
		*cnt_out = N;
		*N_out = N;

		if (len > sizeof(*env) + N) {
			int K = u8_get_bits(env->variable[N],
					    IEEE80211_TX_PWR_ENV_EXT_COUNT);

			K = min(K, IEEE80211_TPE_PSD_ENTRIES_320MHZ - N);
			memcpy(out + N, env->variable + N + 1, K);
			(*cnt_out) += K;
		}
		break;
	}
}

static u32
_ieee802_11_parse_elems_full(struct ieee80211_elems_parse_params *params,
			     struct ieee80211_elems_parse *elems_parse,
			     const struct element *check_inherit)
{
	struct ieee802_11_elems *elems = &elems_parse->elems;
	const struct element *elem;
	bool calc_crc = params->filter != 0;
	DECLARE_BITMAP(seen_elems, 256);
	u32 crc = params->crc;

	bitmap_zero(seen_elems, 256);

	for_each_element(elem, params->start, params->len) {
		const struct element *subelem;
		u8 elem_parse_failed;
		u8 id = elem->id;
		u8 elen = elem->datalen;
		const u8 *pos = elem->data;

		if (check_inherit &&
		    !cfg80211_is_element_inherited(elem,
						   check_inherit))
			continue;

		switch (id) {
		case WLAN_EID_SSID:
		case WLAN_EID_SUPP_RATES:
		case WLAN_EID_FH_PARAMS:
		case WLAN_EID_DS_PARAMS:
		case WLAN_EID_CF_PARAMS:
		case WLAN_EID_TIM:
		case WLAN_EID_IBSS_PARAMS:
		case WLAN_EID_CHALLENGE:
		case WLAN_EID_RSN:
		case WLAN_EID_ERP_INFO:
		case WLAN_EID_EXT_SUPP_RATES:
		case WLAN_EID_HT_CAPABILITY:
		case WLAN_EID_HT_OPERATION:
		case WLAN_EID_VHT_CAPABILITY:
		case WLAN_EID_VHT_OPERATION:
		case WLAN_EID_MESH_ID:
		case WLAN_EID_MESH_CONFIG:
		case WLAN_EID_PEER_MGMT:
		case WLAN_EID_PREQ:
		case WLAN_EID_PREP:
		case WLAN_EID_PERR:
		case WLAN_EID_RANN:
		case WLAN_EID_CHANNEL_SWITCH:
		case WLAN_EID_EXT_CHANSWITCH_ANN:
		case WLAN_EID_COUNTRY:
		case WLAN_EID_PWR_CONSTRAINT:
		case WLAN_EID_TIMEOUT_INTERVAL:
		case WLAN_EID_SECONDARY_CHANNEL_OFFSET:
		case WLAN_EID_WIDE_BW_CHANNEL_SWITCH:
		case WLAN_EID_CHAN_SWITCH_PARAM:
		case WLAN_EID_EXT_CAPABILITY:
		case WLAN_EID_CHAN_SWITCH_TIMING:
		case WLAN_EID_LINK_ID:
		case WLAN_EID_BSS_MAX_IDLE_PERIOD:
		case WLAN_EID_RSNX:
		case WLAN_EID_S1G_BCN_COMPAT:
		case WLAN_EID_S1G_CAPABILITIES:
		case WLAN_EID_S1G_OPERATION:
		case WLAN_EID_AID_RESPONSE:
		case WLAN_EID_S1G_SHORT_BCN_INTERVAL:
		/*
		 * not listing WLAN_EID_CHANNEL_SWITCH_WRAPPER -- it seems possible
		 * that if the content gets bigger it might be needed more than once
		 */
			if (test_bit(id, seen_elems)) {
				elems->parse_error |=
					IEEE80211_PARSE_ERR_DUP_ELEM;
				continue;
			}
			break;
		}

		if (calc_crc && id < 64 && (params->filter & (1ULL << id)))
			crc = crc32_be(crc, pos - 2, elen + 2);

		elem_parse_failed = 0;

		switch (id) {
		case WLAN_EID_LINK_ID:
			if (elen + 2 < sizeof(struct ieee80211_tdls_lnkie)) {
				elem_parse_failed =
					IEEE80211_PARSE_ERR_BAD_ELEM_SIZE;
				break;
			}
			elems->lnk_id = (void *)(pos - 2);
			break;
		case WLAN_EID_CHAN_SWITCH_TIMING:
			if (elen < sizeof(struct ieee80211_ch_switch_timing)) {
				elem_parse_failed =
					IEEE80211_PARSE_ERR_BAD_ELEM_SIZE;
				break;
			}
			elems->ch_sw_timing = (void *)pos;
			break;
		case WLAN_EID_EXT_CAPABILITY:
			elems->ext_capab = pos;
			elems->ext_capab_len = elen;
			break;
		case WLAN_EID_SSID:
			elems->ssid = pos;
			elems->ssid_len = elen;
			break;
		case WLAN_EID_SUPP_RATES:
			elems->supp_rates = pos;
			elems->supp_rates_len = elen;
			break;
		case WLAN_EID_DS_PARAMS:
			if (elen >= 1)
				elems->ds_params = pos;
			else
				elem_parse_failed =
					IEEE80211_PARSE_ERR_BAD_ELEM_SIZE;
			break;
		case WLAN_EID_TIM:
			if (elen >= sizeof(struct ieee80211_tim_ie)) {
				elems->tim = (void *)pos;
				elems->tim_len = elen;
			} else
				elem_parse_failed =
					IEEE80211_PARSE_ERR_BAD_ELEM_SIZE;
			break;
		case WLAN_EID_VENDOR_SPECIFIC:
			if (elems_parse->skip_vendor)
				break;

			if (elen >= 4 && pos[0] == 0x00 && pos[1] == 0x50 &&
			    pos[2] == 0xf2) {
				/* Microsoft OUI (00:50:F2) */

				if (calc_crc)
					crc = crc32_be(crc, pos - 2, elen + 2);

				if (elen >= 5 && pos[3] == 2) {
					/* OUI Type 2 - WMM IE */
					if (pos[4] == 0) {
						elems->wmm_info = pos;
						elems->wmm_info_len = elen;
					} else if (pos[4] == 1) {
						elems->wmm_param = pos;
						elems->wmm_param_len = elen;
					}
				}
			}
			break;
		case WLAN_EID_RSN:
			elems->rsn = pos;
			elems->rsn_len = elen;
			break;
		case WLAN_EID_ERP_INFO:
			if (elen >= 1)
				elems->erp_info = pos;
			else
				elem_parse_failed =
					IEEE80211_PARSE_ERR_BAD_ELEM_SIZE;
			break;
		case WLAN_EID_EXT_SUPP_RATES:
			elems->ext_supp_rates = pos;
			elems->ext_supp_rates_len = elen;
			break;
		case WLAN_EID_HT_CAPABILITY:
			if (params->mode < IEEE80211_CONN_MODE_HT)
				break;
			if (elen >= sizeof(struct ieee80211_ht_cap))
				elems->ht_cap_elem = (void *)pos;
			else
				elem_parse_failed =
					IEEE80211_PARSE_ERR_BAD_ELEM_SIZE;
			break;
		case WLAN_EID_HT_OPERATION:
			if (params->mode < IEEE80211_CONN_MODE_HT)
				break;
			if (elen >= sizeof(struct ieee80211_ht_operation))
				elems->ht_operation = (void *)pos;
			else
				elem_parse_failed =
					IEEE80211_PARSE_ERR_BAD_ELEM_SIZE;
			break;
		case WLAN_EID_VHT_CAPABILITY:
			if (params->mode < IEEE80211_CONN_MODE_VHT)
				break;
			if (elen >= sizeof(struct ieee80211_vht_cap))
				elems->vht_cap_elem = (void *)pos;
			else
				elem_parse_failed =
					IEEE80211_PARSE_ERR_BAD_ELEM_SIZE;
			break;
		case WLAN_EID_VHT_OPERATION:
			if (params->mode < IEEE80211_CONN_MODE_VHT)
				break;
			if (elen >= sizeof(struct ieee80211_vht_operation)) {
				elems->vht_operation = (void *)pos;
				if (calc_crc)
					crc = crc32_be(crc, pos - 2, elen + 2);
				break;
			}
			elem_parse_failed =
				IEEE80211_PARSE_ERR_BAD_ELEM_SIZE;
			break;
		case WLAN_EID_OPMODE_NOTIF:
			if (params->mode < IEEE80211_CONN_MODE_VHT)
				break;
			if (elen > 0) {
				elems->opmode_notif = pos;
				if (calc_crc)
					crc = crc32_be(crc, pos - 2, elen + 2);
				break;
			}
			elem_parse_failed =
				IEEE80211_PARSE_ERR_BAD_ELEM_SIZE;
			break;
		case WLAN_EID_MESH_ID:
			elems->mesh_id = pos;
			elems->mesh_id_len = elen;
			break;
		case WLAN_EID_MESH_CONFIG:
			if (elen >= sizeof(struct ieee80211_meshconf_ie))
				elems->mesh_config = (void *)pos;
			else
				elem_parse_failed =
					IEEE80211_PARSE_ERR_BAD_ELEM_SIZE;
			break;
		case WLAN_EID_PEER_MGMT:
			elems->peering = pos;
			elems->peering_len = elen;
			break;
		case WLAN_EID_MESH_AWAKE_WINDOW:
			if (elen >= 2)
				elems->awake_window = (void *)pos;
			break;
		case WLAN_EID_PREQ:
			elems->preq = pos;
			elems->preq_len = elen;
			break;
		case WLAN_EID_PREP:
			elems->prep = pos;
			elems->prep_len = elen;
			break;
		case WLAN_EID_PERR:
			elems->perr = pos;
			elems->perr_len = elen;
			break;
		case WLAN_EID_RANN:
			if (elen >= sizeof(struct ieee80211_rann_ie))
				elems->rann = (void *)pos;
			else
				elem_parse_failed =
					IEEE80211_PARSE_ERR_BAD_ELEM_SIZE;
			break;
		case WLAN_EID_CHANNEL_SWITCH:
			if (elen != sizeof(struct ieee80211_channel_sw_ie)) {
				elem_parse_failed =
					IEEE80211_PARSE_ERR_BAD_ELEM_SIZE;
				break;
			}
			elems->ch_switch_ie = (void *)pos;
			break;
		case WLAN_EID_EXT_CHANSWITCH_ANN:
			if (elen != sizeof(struct ieee80211_ext_chansw_ie)) {
				elem_parse_failed =
					IEEE80211_PARSE_ERR_BAD_ELEM_SIZE;
				break;
			}
			elems->ext_chansw_ie = (void *)pos;
			break;
		case WLAN_EID_SECONDARY_CHANNEL_OFFSET:
			if (params->mode < IEEE80211_CONN_MODE_HT)
				break;
			if (elen != sizeof(struct ieee80211_sec_chan_offs_ie)) {
				elem_parse_failed =
					IEEE80211_PARSE_ERR_BAD_ELEM_SIZE;
				break;
			}
			elems->sec_chan_offs = (void *)pos;
			break;
		case WLAN_EID_CHAN_SWITCH_PARAM:
			if (elen <
			    sizeof(*elems->mesh_chansw_params_ie)) {
				elem_parse_failed =
					IEEE80211_PARSE_ERR_BAD_ELEM_SIZE;
				break;
			}
			elems->mesh_chansw_params_ie = (void *)pos;
			break;
		case WLAN_EID_WIDE_BW_CHANNEL_SWITCH:
			if (params->mode < IEEE80211_CONN_MODE_VHT)
				break;

			if (!params->action) {
				elem_parse_failed =
					IEEE80211_PARSE_ERR_UNEXPECTED_ELEM;
				break;
			}

			if (elen < sizeof(*elems->wide_bw_chansw_ie)) {
				elem_parse_failed =
					IEEE80211_PARSE_ERR_BAD_ELEM_SIZE;
				break;
			}
			elems->wide_bw_chansw_ie = (void *)pos;
			break;
		case WLAN_EID_CHANNEL_SWITCH_WRAPPER:
			if (params->mode < IEEE80211_CONN_MODE_VHT)
				break;
			if (params->action) {
				elem_parse_failed =
					IEEE80211_PARSE_ERR_UNEXPECTED_ELEM;
				break;
			}
			/*
			 * This is a bit tricky, but as we only care about
			 * a few elements, parse them out manually.
			 */
			subelem = cfg80211_find_elem(WLAN_EID_WIDE_BW_CHANNEL_SWITCH,
						     pos, elen);
			if (subelem) {
				if (subelem->datalen >= sizeof(*elems->wide_bw_chansw_ie))
					elems->wide_bw_chansw_ie =
						(void *)subelem->data;
				else
					elem_parse_failed =
						IEEE80211_PARSE_ERR_BAD_ELEM_SIZE;
			}

			if (params->mode < IEEE80211_CONN_MODE_EHT)
				break;

			subelem = cfg80211_find_ext_elem(WLAN_EID_EXT_BANDWIDTH_INDICATION,
							 pos, elen);
			if (subelem) {
				const void *edata = subelem->data + 1;
				u8 edatalen = subelem->datalen - 1;

				if (ieee80211_bandwidth_indication_size_ok(edata,
									   edatalen))
					elems->bandwidth_indication = edata;
				else
					elem_parse_failed =
						IEEE80211_PARSE_ERR_BAD_ELEM_SIZE;
			}

			subelem = cfg80211_find_ext_elem(WLAN_EID_TX_POWER_ENVELOPE,
							 pos, elen);
			if (subelem)
				ieee80211_parse_tpe(&elems->csa_tpe,
						    subelem->data + 1,
						    subelem->datalen - 1);
			break;
		case WLAN_EID_COUNTRY:
			elems->country_elem = pos;
			elems->country_elem_len = elen;
			break;
		case WLAN_EID_PWR_CONSTRAINT:
			if (elen != 1) {
				elem_parse_failed =
					IEEE80211_PARSE_ERR_BAD_ELEM_SIZE;
				break;
			}
			elems->pwr_constr_elem = pos;
			break;
		case WLAN_EID_CISCO_VENDOR_SPECIFIC:
			/* Lots of different options exist, but we only care
			 * about the Dynamic Transmit Power Control element.
			 * First check for the Cisco OUI, then for the DTPC
			 * tag (0x00).
			 */
			if (elen < 4) {
				elem_parse_failed =
					IEEE80211_PARSE_ERR_BAD_ELEM_SIZE;
				break;
			}

			if (pos[0] != 0x00 || pos[1] != 0x40 ||
			    pos[2] != 0x96 || pos[3] != 0x00)
				break;

			if (elen != 6) {
				elem_parse_failed =
					IEEE80211_PARSE_ERR_BAD_ELEM_SIZE;
				break;
			}

			if (calc_crc)
				crc = crc32_be(crc, pos - 2, elen + 2);

			elems->cisco_dtpc_elem = pos;
			break;
		case WLAN_EID_ADDBA_EXT:
			if (elen < sizeof(struct ieee80211_addba_ext_ie)) {
				elem_parse_failed =
					IEEE80211_PARSE_ERR_BAD_ELEM_SIZE;
				break;
			}
			elems->addba_ext_ie = (void *)pos;
			break;
		case WLAN_EID_TIMEOUT_INTERVAL:
			if (elen >= sizeof(struct ieee80211_timeout_interval_ie))
				elems->timeout_int = (void *)pos;
			else
				elem_parse_failed =
					IEEE80211_PARSE_ERR_BAD_ELEM_SIZE;
			break;
		case WLAN_EID_BSS_MAX_IDLE_PERIOD:
			if (elen >= sizeof(*elems->max_idle_period_ie))
				elems->max_idle_period_ie = (void *)pos;
			break;
		case WLAN_EID_RSNX:
			elems->rsnx = pos;
			elems->rsnx_len = elen;
			break;
		case WLAN_EID_TX_POWER_ENVELOPE:
			if (params->mode < IEEE80211_CONN_MODE_HE)
				break;
			ieee80211_parse_tpe(&elems->tpe, pos, elen);
			break;
		case WLAN_EID_EXTENSION:
			ieee80211_parse_extension_element(calc_crc ?
								&crc : NULL,
							  elem, elems_parse,
							  params);
			break;
		case WLAN_EID_S1G_CAPABILITIES:
			if (params->mode != IEEE80211_CONN_MODE_S1G)
				break;
			if (elen >= sizeof(*elems->s1g_capab))
				elems->s1g_capab = (void *)pos;
			else
				elem_parse_failed =
					IEEE80211_PARSE_ERR_BAD_ELEM_SIZE;
			break;
		case WLAN_EID_S1G_OPERATION:
			if (params->mode != IEEE80211_CONN_MODE_S1G)
				break;
			if (elen == sizeof(*elems->s1g_oper))
				elems->s1g_oper = (void *)pos;
			else
				elem_parse_failed =
					IEEE80211_PARSE_ERR_BAD_ELEM_SIZE;
			break;
		case WLAN_EID_S1G_BCN_COMPAT:
			if (params->mode != IEEE80211_CONN_MODE_S1G)
				break;
			if (elen == sizeof(*elems->s1g_bcn_compat))
				elems->s1g_bcn_compat = (void *)pos;
			else
				elem_parse_failed =
					IEEE80211_PARSE_ERR_BAD_ELEM_SIZE;
			break;
		case WLAN_EID_AID_RESPONSE:
			if (params->mode != IEEE80211_CONN_MODE_S1G)
				break;
			if (elen == sizeof(struct ieee80211_aid_response_ie))
				elems->aid_resp = (void *)pos;
			else
				elem_parse_failed =
					IEEE80211_PARSE_ERR_BAD_ELEM_SIZE;
			break;
		default:
			break;
		}

		if (elem_parse_failed)
			elems->parse_error |= elem_parse_failed;
		else
			__set_bit(id, seen_elems);
	}

	if (!for_each_element_completed(elem, params->start, params->len))
		elems->parse_error |= IEEE80211_PARSE_ERR_INVALID_END;

	return crc;
}

static size_t ieee802_11_find_bssid_profile(const u8 *start, size_t len,
					    struct ieee802_11_elems *elems,
					    struct cfg80211_bss *bss,
					    u8 *nontransmitted_profile)
{
	const struct element *elem, *sub;
	size_t profile_len = 0;

	if (!bss || !bss->transmitted_bss)
		return profile_len;

	for_each_element_id(elem, WLAN_EID_MULTIPLE_BSSID, start, len) {
		if (elem->datalen < 2)
			continue;
		if (elem->data[0] < 1 || elem->data[0] > 8)
			continue;

		for_each_element(sub, elem->data + 1, elem->datalen - 1) {
			u8 new_bssid[ETH_ALEN];
			const u8 *index;

			if (sub->id != 0 || sub->datalen < 4) {
				/* not a valid BSS profile */
				continue;
			}

			if (sub->data[0] != WLAN_EID_NON_TX_BSSID_CAP ||
			    sub->data[1] != 2) {
				/* The first element of the
				 * Nontransmitted BSSID Profile is not
				 * the Nontransmitted BSSID Capability
				 * element.
				 */
				continue;
			}

			memset(nontransmitted_profile, 0, len);
			profile_len = cfg80211_merge_profile(start, len,
							     elem,
							     sub,
							     nontransmitted_profile,
							     len);

			/* found a Nontransmitted BSSID Profile */
			index = cfg80211_find_ie(WLAN_EID_MULTI_BSSID_IDX,
						 nontransmitted_profile,
						 profile_len);
			if (!index || index[1] < 1 || index[2] == 0) {
				/* Invalid MBSSID Index element */
				continue;
			}

			cfg80211_gen_new_bssid(bss->transmitted_bss->bssid,
					       elem->data[0],
					       index[2],
					       new_bssid);
			if (ether_addr_equal(new_bssid, bss->bssid)) {
				elems->bssid_index_len = index[1];
				elems->bssid_index = (void *)&index[2];
				return profile_len;
			}
		}
	}

	return 0;
}

static void
ieee80211_mle_get_sta_prof(struct ieee80211_elems_parse *elems_parse,
			   u8 link_id)
{
	struct ieee802_11_elems *elems = &elems_parse->elems;
	const struct ieee80211_multi_link_elem *ml = elems->ml_basic;
	ssize_t ml_len = elems->ml_basic_len;
	const struct element *sub;

	for_each_mle_subelement(sub, (u8 *)ml, ml_len) {
		struct ieee80211_mle_per_sta_profile *prof = (void *)sub->data;
		ssize_t sta_prof_len;
		u16 control;

		if (sub->id != IEEE80211_MLE_SUBELEM_PER_STA_PROFILE)
			continue;

		if (!ieee80211_mle_basic_sta_prof_size_ok(sub->data,
							  sub->datalen))
			return;

		control = le16_to_cpu(prof->control);

		if (link_id != u16_get_bits(control,
					    IEEE80211_MLE_STA_CONTROL_LINK_ID))
			continue;

		if (!(control & IEEE80211_MLE_STA_CONTROL_COMPLETE_PROFILE))
			return;

		/* the sub element can be fragmented */
		sta_prof_len =
			cfg80211_defragment_element(sub,
						    (u8 *)ml, ml_len,
						    elems_parse->scratch_pos,
						    elems_parse->scratch +
							elems_parse->scratch_len -
							elems_parse->scratch_pos,
						    IEEE80211_MLE_SUBELEM_FRAGMENT);

		if (sta_prof_len < 0)
			return;

		elems->prof = (void *)elems_parse->scratch_pos;
		elems->sta_prof_len = sta_prof_len;
		elems_parse->scratch_pos += sta_prof_len;

		return;
	}
}

static const struct element *
ieee80211_prep_mle_link_parse(struct ieee80211_elems_parse *elems_parse,
			      struct ieee80211_elems_parse_params *params,
			      struct ieee80211_elems_parse_params *sub)
{
	struct ieee802_11_elems *elems = &elems_parse->elems;
	struct ieee80211_mle_per_sta_profile *prof;
	const struct element *tmp;
	ssize_t ml_len;
	const u8 *end;

	if (params->mode < IEEE80211_CONN_MODE_EHT)
		return NULL;

	for_each_element_extid(tmp, WLAN_EID_EXT_EHT_MULTI_LINK,
			       elems->ie_start, elems->total_len) {
		const struct ieee80211_multi_link_elem *mle =
			(void *)tmp->data + 1;

		if (!ieee80211_mle_size_ok(tmp->data + 1, tmp->datalen - 1))
			continue;

		if (le16_get_bits(mle->control, IEEE80211_ML_CONTROL_TYPE) !=
		    IEEE80211_ML_CONTROL_TYPE_BASIC)
			continue;

		elems_parse->ml_basic_elem = tmp;
		break;
	}

	ml_len = cfg80211_defragment_element(elems_parse->ml_basic_elem,
					     elems->ie_start,
					     elems->total_len,
					     elems_parse->scratch_pos,
					     elems_parse->scratch +
						elems_parse->scratch_len -
						elems_parse->scratch_pos,
					     WLAN_EID_FRAGMENT);

	if (ml_len < 0)
		return NULL;

	elems->ml_basic = (const void *)elems_parse->scratch_pos;
	elems->ml_basic_len = ml_len;
	elems_parse->scratch_pos += ml_len;

	if (params->link_id == -1)
		return NULL;

	ieee80211_mle_get_sta_prof(elems_parse, params->link_id);
	prof = elems->prof;

	if (!prof)
		return NULL;

	/* check if we have the 4 bytes for the fixed part in assoc response */
	if (elems->sta_prof_len < sizeof(*prof) + prof->sta_info_len - 1 + 4) {
		elems->prof = NULL;
		elems->sta_prof_len = 0;
		return NULL;
	}

	/*
	 * Skip the capability information and the status code that are expected
	 * as part of the station profile in association response frames. Note
	 * the -1 is because the 'sta_info_len' is accounted to as part of the
	 * per-STA profile, but not part of the 'u8 variable[]' portion.
	 */
	sub->start = prof->variable + prof->sta_info_len - 1 + 4;
	end = (const u8 *)prof + elems->sta_prof_len;
	sub->len = end - sub->start;

	sub->mode = params->mode;
	sub->action = params->action;
	sub->from_ap = params->from_ap;
	sub->link_id = -1;

	return cfg80211_find_ext_elem(WLAN_EID_EXT_NON_INHERITANCE,
				      sub->start, sub->len);
}

static void
ieee80211_mle_defrag_reconf(struct ieee80211_elems_parse *elems_parse)
{
	struct ieee802_11_elems *elems = &elems_parse->elems;
	ssize_t ml_len;

	ml_len = cfg80211_defragment_element(elems_parse->ml_reconf_elem,
					     elems->ie_start,
					     elems->total_len,
					     elems_parse->scratch_pos,
					     elems_parse->scratch +
						elems_parse->scratch_len -
						elems_parse->scratch_pos,
					     WLAN_EID_FRAGMENT);
	if (ml_len < 0)
		return;
	elems->ml_reconf = (void *)elems_parse->scratch_pos;
	elems->ml_reconf_len = ml_len;
	elems_parse->scratch_pos += ml_len;
}

static void
ieee80211_mle_defrag_epcs(struct ieee80211_elems_parse *elems_parse)
{
	struct ieee802_11_elems *elems = &elems_parse->elems;
	ssize_t ml_len;

	ml_len = cfg80211_defragment_element(elems_parse->ml_epcs_elem,
					     elems->ie_start,
					     elems->total_len,
					     elems_parse->scratch_pos,
					     elems_parse->scratch +
						elems_parse->scratch_len -
						elems_parse->scratch_pos,
					     WLAN_EID_FRAGMENT);
	if (ml_len < 0)
		return;
	elems->ml_epcs = (void *)elems_parse->scratch_pos;
	elems->ml_epcs_len = ml_len;
	elems_parse->scratch_pos += ml_len;
}

struct ieee802_11_elems *
ieee802_11_parse_elems_full(struct ieee80211_elems_parse_params *params)
{
	struct ieee80211_elems_parse_params sub = {};
	struct ieee80211_elems_parse *elems_parse;
	const struct element *non_inherit = NULL;
	struct ieee802_11_elems *elems;
	size_t scratch_len = 3 * params->len;
	bool multi_link_inner = false;

	BUILD_BUG_ON(offsetof(typeof(*elems_parse), elems) != 0);

	/* cannot parse for both a specific link and non-transmitted BSS */
	if (WARN_ON(params->link_id >= 0 && params->bss))
		return NULL;

	elems_parse = kzalloc(struct_size(elems_parse, scratch, scratch_len),
			      GFP_ATOMIC);
	if (!elems_parse)
		return NULL;

	elems_parse->scratch_len = scratch_len;
	elems_parse->scratch_pos = elems_parse->scratch;

	elems = &elems_parse->elems;
	elems->ie_start = params->start;
	elems->total_len = params->len;

	/* set all TPE entries to unlimited (but invalid) */
	ieee80211_clear_tpe(&elems->tpe);
	ieee80211_clear_tpe(&elems->csa_tpe);

	/*
	 * If we're looking for a non-transmitted BSS then we cannot at
	 * the same time be looking for a second link as the two can only
	 * appear in the same frame carrying info for different BSSes.
	 *
	 * In any case, we only look for one at a time, as encoded by
	 * the WARN_ON above.
	 */
	if (params->bss) {
		int nontx_len =
			ieee802_11_find_bssid_profile(params->start,
						      params->len,
						      elems, params->bss,
						      elems_parse->scratch_pos);
		sub.start = elems_parse->scratch_pos;
		sub.mode = params->mode;
		sub.len = nontx_len;
		sub.action = params->action;
		sub.link_id = params->link_id;

		/* consume the space used for non-transmitted profile */
		elems_parse->scratch_pos += nontx_len;

		non_inherit = cfg80211_find_ext_elem(WLAN_EID_EXT_NON_INHERITANCE,
						     sub.start, nontx_len);
	} else {
		/* must always parse to get elems_parse->ml_basic_elem */
		non_inherit = ieee80211_prep_mle_link_parse(elems_parse, params,
							    &sub);
		multi_link_inner = true;
	}

	elems_parse->skip_vendor =
		cfg80211_find_elem(WLAN_EID_VENDOR_SPECIFIC,
				   sub.start, sub.len);
	elems->crc = _ieee802_11_parse_elems_full(params, elems_parse,
						  non_inherit);

	/* Override with nontransmitted/per-STA profile if found */
	if (sub.len) {
		elems_parse->multi_link_inner = multi_link_inner;
		elems_parse->skip_vendor = false;
		_ieee802_11_parse_elems_full(&sub, elems_parse, NULL);
	}

	ieee80211_mle_defrag_reconf(elems_parse);

	ieee80211_mle_defrag_epcs(elems_parse);

	if (elems->tim && !elems->parse_error) {
		const struct ieee80211_tim_ie *tim_ie = elems->tim;

		elems->dtim_period = tim_ie->dtim_period;
		elems->dtim_count = tim_ie->dtim_count;
	}

	/* Override DTIM period and count if needed */
	if (elems->bssid_index &&
	    elems->bssid_index_len >=
	    offsetofend(struct ieee80211_bssid_index, dtim_period))
		elems->dtim_period = elems->bssid_index->dtim_period;

	if (elems->bssid_index &&
	    elems->bssid_index_len >=
	    offsetofend(struct ieee80211_bssid_index, dtim_count))
		elems->dtim_count = elems->bssid_index->dtim_count;

	return elems;
}
EXPORT_SYMBOL_IF_KUNIT(ieee802_11_parse_elems_full);

int ieee80211_parse_bitrates(enum nl80211_chan_width width,
			     const struct ieee80211_supported_band *sband,
			     const u8 *srates, int srates_len, u32 *rates)
{
	struct ieee80211_rate *br;
	int brate, rate, i, j, count = 0;

	*rates = 0;

	for (i = 0; i < srates_len; i++) {
		rate = srates[i] & 0x7f;

		for (j = 0; j < sband->n_bitrates; j++) {
			br = &sband->bitrates[j];

			brate = DIV_ROUND_UP(br->bitrate, 5);
			if (brate == rate) {
				*rates |= BIT(j);
				count++;
				break;
			}
		}
	}
	return count;
}
