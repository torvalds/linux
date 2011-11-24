/*
 * EAP peer method: EAP-GTC (RFC 3748)
 * Copyright (c) 2004-2006, Jouni Malinen <j@w1.fi>
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
#include "eap_i.h"


struct eap_gtc_data {
	int prefix;
};


static void * eap_gtc_init(struct eap_sm *sm)
{
	struct eap_gtc_data *data;
	data = os_zalloc(sizeof(*data));
	if (data == NULL)
		return NULL;

	if (sm->m && sm->m->method == EAP_TYPE_FAST) {
		wpa_printf(MSG_DEBUG, "EAP-GTC: EAP-FAST tunnel - use prefix "
			   "with challenge/response");
		data->prefix = 1;
	}
	return data;
}


static void eap_gtc_deinit(struct eap_sm *sm, void *priv)
{
	struct eap_gtc_data *data = priv;
	os_free(data);
}


static u8 * eap_gtc_process(struct eap_sm *sm, void *priv,
			    struct eap_method_ret *ret,
			    const u8 *reqData, size_t reqDataLen,
			    size_t *respDataLen)
{
	struct eap_gtc_data *data = priv;
	const struct eap_hdr *req;
	struct eap_hdr *resp;
	const u8 *pos, *password, *identity;
	u8 *rpos;
	size_t password_len, identity_len, len, plen;
	int otp;

	pos = eap_hdr_validate(EAP_VENDOR_IETF, EAP_TYPE_GTC,
			       reqData, reqDataLen, &len);
	if (pos == NULL) {
		ret->ignore = TRUE;
		return NULL;
	}
	req = (const struct eap_hdr *) reqData;

	wpa_hexdump_ascii(MSG_MSGDUMP, "EAP-GTC: Request message", pos, len);
	if (data->prefix &&
	    (len < 10 || os_memcmp(pos, "CHALLENGE=", 10) != 0)) {
		wpa_printf(MSG_DEBUG, "EAP-GTC: Challenge did not start with "
			   "expected prefix");

		/* Send an empty response in order to allow tunneled
		 * acknowledgement of the failure. This will also cover the
		 * error case which seems to use EAP-MSCHAPv2 like error
		 * reporting with EAP-GTC inside EAP-FAST tunnel. */
		resp = eap_msg_alloc(EAP_VENDOR_IETF, EAP_TYPE_GTC,
				     respDataLen, 0, EAP_CODE_RESPONSE,
				     req->identifier, NULL);
		return (u8 *) resp;
	}

	password = eap_get_config_otp(sm, &password_len);
	if (password)
		otp = 1;
	else {
		password = eap_get_config_password(sm, &password_len);
		otp = 0;
	}

	if (password == NULL) {
		wpa_printf(MSG_INFO, "EAP-GTC: Password not configured");
		eap_sm_request_otp(sm, (const char *) pos, len);
		ret->ignore = TRUE;
		return NULL;
	}

	ret->ignore = FALSE;

	ret->methodState = data->prefix ? METHOD_MAY_CONT : METHOD_DONE;
	ret->decision = DECISION_COND_SUCC;
	ret->allowNotifications = FALSE;

	plen = password_len;
	identity = eap_get_config_identity(sm, &identity_len);
	if (identity == NULL)
		return NULL;
	if (data->prefix)
		plen += 9 + identity_len + 1;
	resp = eap_msg_alloc(EAP_VENDOR_IETF, EAP_TYPE_GTC, respDataLen,
			     plen, EAP_CODE_RESPONSE, req->identifier, &rpos);
	if (resp == NULL)
		return NULL;
	if (data->prefix) {
		os_memcpy(rpos, "RESPONSE=", 9);
		rpos += 9;
		os_memcpy(rpos, identity, identity_len);
		rpos += identity_len;
		*rpos++ = '\0';
	}
	os_memcpy(rpos, password, password_len);
	wpa_hexdump_ascii_key(MSG_MSGDUMP, "EAP-GTC: Response",
			      (u8 *) (resp + 1) + 1, plen);

	if (otp) {
		wpa_printf(MSG_DEBUG, "EAP-GTC: Forgetting used password");
		eap_clear_config_otp(sm);
	}

	return (u8 *) resp;
}


int eap_peer_gtc_register(void)
{
	struct eap_method *eap;
	int ret;

	eap = eap_peer_method_alloc(EAP_PEER_METHOD_INTERFACE_VERSION,
				    EAP_VENDOR_IETF, EAP_TYPE_GTC, "GTC");
	if (eap == NULL)
		return -1;

	eap->init = eap_gtc_init;
	eap->deinit = eap_gtc_deinit;
	eap->process = eap_gtc_process;

	ret = eap_peer_method_register(eap);
	if (ret)
		eap_peer_method_free(eap);
	return ret;
}
