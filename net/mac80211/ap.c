// SPDX-License-Identifier: GPL-2.0-only
/*
 * AP handling
 *
 * Partially
 * Copyright (C) 2026 Intel Corporation
 */

#include "driver-ops.h"
#include "ieee80211_i.h"
#include "rate.h"

static void
ieee80211_send_eml_op_mode_notif(struct ieee80211_sub_if_data *sdata,
				 struct ieee80211_mgmt *req, int opt_len)
{
	int len = IEEE80211_MIN_ACTION_SIZE(eml_omn);
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_mgmt *mgmt;
	struct sk_buff *skb;

	len += opt_len; /* optional len */
	skb = dev_alloc_skb(local->tx_headroom + len);
	if (!skb)
		return;

	skb_reserve(skb, local->tx_headroom);
	mgmt = skb_put_zero(skb, len);
	mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
					  IEEE80211_STYPE_ACTION);
	memcpy(mgmt->da, req->sa, ETH_ALEN);
	memcpy(mgmt->sa, sdata->vif.addr, ETH_ALEN);
	memcpy(mgmt->bssid, sdata->vif.addr, ETH_ALEN);

	mgmt->u.action.category = WLAN_CATEGORY_PROTECTED_EHT;
	mgmt->u.action.action_code =
		WLAN_PROTECTED_EHT_ACTION_EML_OP_MODE_NOTIF;
	mgmt->u.action.eml_omn.dialog_token =
		req->u.action.eml_omn.dialog_token;
	mgmt->u.action.eml_omn.control = req->u.action.eml_omn.control &
		~(IEEE80211_EML_CTRL_EMLSR_PARAM_UPDATE |
		  IEEE80211_EML_CTRL_INDEV_COEX_ACT);
	/* Copy optional fields from the received notification frame */
	memcpy(mgmt->u.action.eml_omn.variable,
	       req->u.action.eml_omn.variable, opt_len);

	ieee80211_tx_skb(sdata, skb);
}

static void
ieee80211_rx_eml_op_mode_notif(struct ieee80211_sub_if_data *sdata,
			       struct sk_buff *skb)
{
	int len = IEEE80211_MIN_ACTION_SIZE(eml_omn);
	enum nl80211_iftype type = ieee80211_vif_type_p2p(&sdata->vif);
	struct ieee80211_rx_status *status = IEEE80211_SKB_RXCB(skb);
	const struct wiphy_iftype_ext_capab *ift_ext_capa;
	struct ieee80211_mgmt *mgmt = (void *)skb->data;
	struct ieee80211_local *local = sdata->local;
	u8 control = mgmt->u.action.eml_omn.control;
	u8 *ptr = mgmt->u.action.eml_omn.variable;
	struct ieee80211_eml_params eml_params = {
		.link_id = status->link_id,
		.control = control,
	};
	struct sta_info *sta;
	int opt_len = 0;

	if (!ieee80211_vif_is_mld(&sdata->vif))
		return;

	/* eMLSR and eMLMR can't be enabled at the same time */
	if ((control & IEEE80211_EML_CTRL_EMLSR_MODE) &&
	    (control & IEEE80211_EML_CTRL_EMLMR_MODE))
		return;

	if ((control & IEEE80211_EML_CTRL_EMLMR_MODE) &&
	    (control & IEEE80211_EML_CTRL_EMLSR_PARAM_UPDATE))
		return;

	ift_ext_capa = cfg80211_get_iftype_ext_capa(local->hw.wiphy, type);
	if (!ift_ext_capa)
		return;

	if (!status->link_valid)
		return;

	sta = sta_info_get_bss(sdata, mgmt->sa);
	if (!sta)
		return;

	if (control & IEEE80211_EML_CTRL_EMLSR_MODE) {
		u8 emlsr_param_update_len;

		if (!(ift_ext_capa->eml_capabilities &
		      IEEE80211_EML_CAP_EMLSR_SUPP))
			return;

		opt_len += sizeof(__le16); /* eMLSR link_bitmap */
		/*
		 * eMLSR param update field is not part of Notification frame
		 * sent by the AP to client so account it separately.
		 */
		emlsr_param_update_len =
			!!(control & IEEE80211_EML_CTRL_EMLSR_PARAM_UPDATE);

		if (skb->len < len + opt_len + emlsr_param_update_len)
			return;

		if (control & IEEE80211_EML_CTRL_EMLSR_PARAM_UPDATE) {
			u8 pad_delay, trans_delay;

			pad_delay = u8_get_bits(ptr[2],
						IEEE80211_EML_EMLSR_PAD_DELAY);
			if (pad_delay >
			    IEEE80211_EML_CAP_EML_PADDING_DELAY_256US)
				return;

			trans_delay = u8_get_bits(ptr[2],
					IEEE80211_EML_EMLSR_TRANS_DELAY);
			if (trans_delay >
			    IEEE80211_EML_CAP_EMLSR_TRANSITION_DELAY_256US)
				return;

			/* Update sta padding and transition delay */
			sta->sta.eml_cap =
				u8_replace_bits(sta->sta.eml_cap,
						pad_delay,
						IEEE80211_EML_CAP_EML_PADDING_DELAY);
			sta->sta.eml_cap =
				u8_replace_bits(sta->sta.eml_cap,
						trans_delay,
						IEEE80211_EML_CAP_EML_TRANSITION_DELAY);
		}
	}

	if (control & IEEE80211_EML_CTRL_EMLMR_MODE) {
		u8 mcs_map_size;
		int i;

		if (!(ift_ext_capa->eml_capabilities &
		      IEEE80211_EML_CAP_EMLMR_SUPPORT))
			return;

		opt_len += sizeof(__le16); /* eMLMR link_bitmap */
		opt_len++; /* eMLMR mcs_map_count */
		if (skb->len < len + opt_len)
			return;

		eml_params.emlmr_mcs_map_count = ptr[2];
		if (eml_params.emlmr_mcs_map_count > 2)
			return;

		mcs_map_size = 3 * (1 + eml_params.emlmr_mcs_map_count);
		opt_len += mcs_map_size;
		if (skb->len < len + opt_len)
			return;

		for (i = 0; i < mcs_map_size; i++) {
			u8 rx_mcs, tx_mcs;

			rx_mcs = u8_get_bits(ptr[3 + i],
					     IEEE80211_EML_EMLMR_RX_MCS_MAP);
			if (rx_mcs > 8)
				return;

			tx_mcs = u8_get_bits(ptr[3 + i],
					     IEEE80211_EML_EMLMR_TX_MCS_MAP);
			if (tx_mcs > 8)
				return;
		}

		memcpy(eml_params.emlmr_mcs_map_bw, &ptr[3], mcs_map_size);
	}

	if ((control & IEEE80211_EML_CTRL_EMLSR_MODE) ||
	    (control & IEEE80211_EML_CTRL_EMLMR_MODE)) {
		eml_params.link_bitmap = get_unaligned_le16(ptr);
		if ((eml_params.link_bitmap & sdata->vif.active_links) !=
		    eml_params.link_bitmap)
			return;
	}

	if (drv_set_eml_op_mode(sdata, &sta->sta, &eml_params))
		return;

	ieee80211_send_eml_op_mode_notif(sdata, mgmt, opt_len);
}

static void
ieee80211_rx_uhr_link_reconfig_req(struct ieee80211_sub_if_data *sdata,
				   struct sk_buff *skb)
{
	struct ieee80211_mgmt *mgmt = (void *)skb->data;
	const struct element *sub;
	struct sta_info *sta;

	/*
	 * rx.c only accepts IEEE80211_UHR_LINK_RECONFIG_REQUEST_OMP_REQUEST
	 * which is valid, so no need to check the frame type/format/etc.
	 */

	sta = sta_info_get_bss(sdata, mgmt->sa);
	if (!sta)
		return;

	struct ieee802_11_elems *elems __free(kfree) =
		ieee802_11_parse_elems(mgmt->u.action.uhr_link_reconf_req.variable,
				       skb->len - IEEE80211_MIN_ACTION_SIZE(uhr_link_reconf_req),
				       IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_ACTION,
				       NULL);
	/* STA will assume we processed it, not good */
	if (!elems)
		return;

	if (!elems->ml_reconf)
		return;

	for_each_mle_subelement(sub, (u8 *)elems->ml_reconf,
				elems->ml_reconf_len) {
		const struct ieee80211_mle_per_sta_profile *prof =
			 (const void *)sub->data;
		struct ieee80211_chanctx_conf *chanctx_conf;
		struct ieee80211_chanctx *chanctx;
		struct ieee80211_link_data *link;
		struct link_sta_info *link_sta;
		const struct element *chg;
		u16 control;
		u8 link_id;

		if (sub->id != IEEE80211_MLE_SUBELEM_PER_STA_PROFILE)
			continue;

		if (!ieee80211_mle_reconf_sta_prof_size_ok(sub->data,
							   sub->datalen))
			return;

		control = le16_to_cpu(prof->control);
		link_id = control & IEEE80211_MLE_STA_RECONF_CONTROL_LINK_ID;

		if (link_id >= IEEE80211_MLD_MAX_NUM_LINKS)
			return;

		link = sdata_dereference(sdata->link[link_id], sdata);
		if (!link)
			continue;

		chanctx_conf = sdata_dereference(link->conf->chanctx_conf,
						 sdata);
		if (!chanctx_conf)
			continue;
		chanctx = container_of(chanctx_conf, struct ieee80211_chanctx,
				       conf);

		link_sta = sdata_dereference(sta->link[link_id], sdata);
		if (!link_sta)
			continue;

		/* do we need to handle any other bits? */
		if (control & ~(IEEE80211_MLE_STA_RECONF_CONTROL_LINK_ID |
				IEEE80211_MLE_STA_RECONF_CONTROL_OPERATION_TYPE))
			continue;

		if (u16_get_bits(control, IEEE80211_MLE_STA_RECONF_CONTROL_OPERATION_TYPE) !=
				IEEE80211_MLE_STA_RECONF_CONTROL_OPERATION_TYPE_UHR_OMP_UPD)
			continue;

		for_each_element_extid(chg, WLAN_EID_EXT_UHR_MODE_CHG,
				       prof->variable + prof->sta_info_len - 1,
				       sub->datalen - sizeof(*prof) -
				       prof->sta_info_len + 1) {
			const struct ieee80211_uhr_mode_change_tuple *tuple;

			for_each_uhr_mode_change_tuple(chg->data + 1,
						       chg->datalen - 1,
						       tuple) {
				u8 id = le16_get_bits(tuple->control,
						      IEEE80211_UHR_MODE_CHANGE_CONTROL_MODE_ID);
				bool enabled = le16_get_bits(tuple->control,
							     IEEE80211_UHR_MODE_CHANGE_CONTROL_MODE_ENABLE);

				/* only handle DBE (for now?) */
				if (id != IEEE80211_UHR_MODE_CHANGE_MODE_ID_DBE)
					continue;

				link_sta->uhr_dbe_enabled = enabled;
				/* also recalculates and updates per-STA bw */
				ieee80211_recalc_chanctx_min_def(sdata->local,
								 chanctx);
			}
		}
	}

	/* TODO: send a response */
}

void ieee80211_ap_rx_queued_frame(struct ieee80211_sub_if_data *sdata,
				  struct sk_buff *skb)
{
	struct ieee80211_mgmt *mgmt = (void *)skb->data;

	/* rx.c cannot queue any non-action frames to AP interfaces */
	if (WARN_ON(!ieee80211_is_action(mgmt->frame_control)))
		return;

	switch (mgmt->u.action.category) {
	case WLAN_CATEGORY_PROTECTED_EHT:
		switch (mgmt->u.action.action_code) {
		case WLAN_PROTECTED_EHT_ACTION_EML_OP_MODE_NOTIF:
			ieee80211_rx_eml_op_mode_notif(sdata, skb);
			break;
		}
		break;
	case WLAN_CATEGORY_PROTECTED_UHR:
		switch (mgmt->u.action.action_code) {
		case IEEE80211_PROTECTED_UHR_ACTION_LINK_RECONFIG_REQUEST:
			ieee80211_rx_uhr_link_reconfig_req(sdata, skb);
			break;
		}
		break;
	}
}

void ieee80211_uhr_disable_dbe_all_stas(struct ieee80211_link_data *link)
{
	struct ieee80211_sub_if_data *sdata = link->sdata;
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_chanctx_conf *chanctx_conf;
	struct ieee80211_chanctx *chanctx;
	int link_id = link->link_id;
	struct sta_info *sta;

	chanctx_conf = sdata_dereference(link->conf->chanctx_conf, sdata);
	if (!chanctx_conf)
		return;
	chanctx = container_of(chanctx_conf, struct ieee80211_chanctx, conf);

	list_for_each_entry(sta, &local->sta_list, list) {
		struct link_sta_info *link_sta;

		if (sta->sdata->bss != sdata->bss)
			continue;

		link_sta = sdata_dereference(sta->link[link_id], sdata);
		if (!link_sta)
			continue;

		link_sta->uhr_dbe_enabled = false;
	}

	/* also recalculates and updates per-STA bw */
	ieee80211_recalc_chanctx_min_def(local, chanctx);
}
