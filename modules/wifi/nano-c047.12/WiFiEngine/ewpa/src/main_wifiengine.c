/*
 * WPA Supplicant / Example program entrypoint
 * Copyright (c) 2003-2005, Jouni Malinen <j@w1.fi>
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
#include "wpa_supplicant.h"
#include "wpa_supplicant_i.h"

static struct wpa_global *global;
static we_ps_control_t *wpa_ps_control;

void
wpa_ps_state(int inhibit)
{
   DE_ASSERT(wpa_ps_control);
	if(inhibit)
		WiFiEngine_InhibitPowerSave(wpa_ps_control);
	else
		WiFiEngine_AllowPowerSave(wpa_ps_control);
}

#ifdef USE_WPS
int wps_enabled(void);
#endif

static void wpa_connect(wi_msg_param_t param, void* priv)
{
	union wpa_event_data data;
	int status;
	unsigned char buf[256];
	size_t buf_len;
	WiFiEngine_Auth_t amode;

	status = WiFiEngine_GetAuthenticationMode(&amode);
	if(status != WIFI_ENGINE_SUCCESS) {
		wpa_ps_state(FALSE);
		DE_TRACE_INT(TR_WPA, "failed to get authentication mode: %d\n", status);
		return;
	}

#ifdef USE_WPS
	if(!wps_enabled()) {
#endif
		if(amode < Authentication_8021X) {
			wpa_ps_state(FALSE);
			DE_TRACE_INT(TR_WPA, "not using WPA (%d)\n", amode);
			wpa_supplicant_cancel_auth_timeout(global->ifaces);
			return;
		}
#ifdef USE_WPS
	}
#endif

	buf_len = sizeof(buf);
	status = WiFiEngine_GetCachedAssocReqIEs(buf, &buf_len);

	os_memset(&data, 0, sizeof(data));
	if(status == WIFI_ENGINE_SUCCESS && buf_len > 0) {
		data.assoc_info.req_ies = buf;
		data.assoc_info.req_ies_len = buf_len;
	}
	
	wpa_supplicant_event(global->ifaces, EVENT_ASSOC, &data);

	return;
}


static void wpa_disconnect(wi_msg_param_t param, void* priv)
{
	wpa_supplicant_event(global->ifaces, EVENT_DISASSOC, NULL);
}


static int
wpa_eapol_handler(const void *pkt, size_t len)
{
	const u8 *p = pkt;
	wpa_supplicant_rx_eapol(global->ifaces, 
				p + 6, p + 14, len - 14);
	return TRUE;
}

static void wpa_driver_WE_mic_failure(int unicast)
{
	union wpa_event_data data;

	memset(&data, 0, sizeof(data));
	data.michael_mic_failure.unicast = unicast;
	wpa_supplicant_event(global->ifaces, EVENT_MICHAEL_MIC_FAILURE, &data);
}

void wpa_driver_WE_pairwise_mic_failure(wi_msg_param_t param, void* priv)
{
	wpa_driver_WE_mic_failure(TRUE);
}

void wpa_driver_WE_group_mic_failure(wi_msg_param_t param, void* priv)
{
	wpa_driver_WE_mic_failure(FALSE);
}

static void wpa_driver_WE_pmkid_candidate(wi_msg_param_t param, void* priv)
{
	union wpa_event_data data;
	struct can_info_s /* CandidateInfo */ {
		m80211_mac_addr_t bssId;
		int32_t rssi_info;
		uint16_t flag;
	} *c = (struct can_info_s*)param;

	DE_ASSERT(param!=NULL);

	if((c->flag & M80211_RSN_CAPABILITY_PREAUTHENTICATION) == 0) {
		return;
	}
	
	memset(&data, 0, sizeof(data));
	os_memcpy(&data.pmkid_candidate.bssid, c->bssId.octet, 6);
	data.pmkid_candidate.index = -c->rssi_info;
	data.pmkid_candidate.preauth = 1;
	wpa_supplicant_event(global->ifaces, EVENT_PMKID_CANDIDATE, &data);
}

struct wpa_handlers_s wpa_handlers = {
   NULL, NULL,
   NULL, NULL, NULL,
   NULL
};

int wpa_init(void)
{
	struct wpa_interface iface;
	struct wpa_params params;
   
	memset(&params, 0, sizeof(params));
	params.wpa_debug_level = MSG_MSGDUMP;
	params.wpa_debug_show_keys = 1;

   wpa_ps_control = WiFiEngine_PSControlAlloc("WPA");
	
	DE_ASSERT(global == NULL);
	global = wpa_supplicant_init(&params);
	if (global == NULL) {
		DE_TRACE_STATIC(TR_WPA, "Failed to initialise supplicant\n");
		return WIFI_ENGINE_FAILURE;
	}
	
	memset(&iface, 0, sizeof(iface));
	iface.ifname = "nrwifi";
	iface.confname = "default";
	/* TODO: set interface parameters */

	if (wpa_supplicant_add_iface(global, &iface) == NULL) {
		wpa_supplicant_deinit(global);
		global = NULL;
		return WIFI_ENGINE_FAILURE;
	}

	WiFiEngine_RegisterEAPOLHandler(wpa_eapol_handler);


	we_ind_cond_register(&wpa_handlers.wpa_disconnect, 
			WE_IND_80211_DISCONNECTED, "WE_IND_80211_DISCONNECTED", 
			wpa_disconnect, NULL,0,NULL);

	we_ind_cond_register(&wpa_handlers.wpa_connect, 
			WE_IND_80211_CONNECTED, "WE_IND_80211_CONNECTED", 
			wpa_connect, NULL,0,NULL);

	we_ind_cond_register(&wpa_handlers.wpa_pw_fail, 
			WE_IND_PAIRWISE_MIC_ERROR, "WE_IND_PAIRWISE_MIC_ERROR", 
			wpa_driver_WE_pairwise_mic_failure,NULL,0,NULL);

	we_ind_cond_register(&wpa_handlers.wpa_gw_fail, 
			WE_IND_GROUP_MIC_ERROR, "WE_IND_GROUP_MIC_ERROR", 
			wpa_driver_WE_group_mic_failure, NULL,0,NULL);

	we_ind_cond_register(&wpa_handlers.wpa_pmkid, 
			WE_IND_CANDIDATE_LIST, "WE_IND_CANDIDATE_LIST", 
			wpa_driver_WE_pmkid_candidate, NULL,0,NULL);

	return WIFI_ENGINE_SUCCESS;
}

int wpa_exit(void)
{
	DE_ASSERT(global != NULL);

	we_ind_deregister(wpa_handlers.wpa_pmkid);
	we_ind_deregister(wpa_handlers.wpa_gw_fail); 
	we_ind_deregister(wpa_handlers.wpa_pw_fail);
	we_ind_deregister(wpa_handlers.wpa_connect);
	we_ind_deregister(wpa_handlers.wpa_disconnect);

	WiFiEngine_RegisterEAPOLHandler(NULL);

	wpa_supplicant_deinit(global);
	global = NULL;
   
   WiFiEngine_PSControlFree(wpa_ps_control);
	wpa_ps_control = NULL;

	return WIFI_ENGINE_SUCCESS;
}

/* Local Variables: */
/* c-basic-offset: 8 */
/* indent-tabs-mode: t */
/* End: */
