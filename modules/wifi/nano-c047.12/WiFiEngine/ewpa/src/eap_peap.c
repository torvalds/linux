/*
 * EAP peer method: EAP-PEAP (draft-josefsson-pppext-eap-tls-eap-07.txt)
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
#include "eap_tls_common.h"
#include "config_ssid.h"
#include "tls.h"
#include "eap_tlv.h"


/* Maximum supported PEAP version
 * 0 = Microsoft's PEAP version 0; draft-kamath-pppext-peapv0-00.txt
 * 1 = draft-josefsson-ppext-eap-tls-eap-05.txt
 * 2 = draft-josefsson-ppext-eap-tls-eap-07.txt
 */
#define EAP_PEAP_VERSION 0


static void eap_peap_deinit(struct eap_sm *sm, void *priv);


struct eap_peap_data {
	struct eap_ssl_data ssl;

	int peap_version, force_peap_version, force_new_label;

	const struct eap_method *phase2_method;
	void *phase2_priv;
	int phase2_success;
	int phase2_eap_success;
	int phase2_eap_started;

	struct eap_method_type phase2_type;
	struct eap_method_type *phase2_types;
	size_t num_phase2_types;

	int peap_outer_success; /* 0 = PEAP terminated on Phase 2 inner
				 * EAP-Success
				 * 1 = reply with tunneled EAP-Success to inner
				 * EAP-Success and expect AS to send outer
				 * (unencrypted) EAP-Success after this
				 * 2 = reply with PEAP/TLS ACK to inner
				 * EAP-Success and expect AS to send outer
				 * (unencrypted) EAP-Success after this */
	int resuming; /* starting a resumed session */
	u8 *key_data;

	u8 *pending_phase2_req;
	size_t pending_phase2_req_len;
};


static void * eap_peap_init(struct eap_sm *sm)
{
	struct eap_peap_data *data;
	struct wpa_ssid *config = eap_get_config(sm);

	data = os_zalloc(sizeof(*data));
	if (data == NULL)
		return NULL;
	sm->peap_done = FALSE;
	data->peap_version = EAP_PEAP_VERSION;
	data->force_peap_version = -1;
	data->peap_outer_success = 2;

	if (config && config->phase1) {
		char *pos = os_strstr(config->phase1, "peapver=");
		if (pos) {
			data->force_peap_version = atoi(pos + 8);
			data->peap_version = data->force_peap_version;
			wpa_printf(MSG_DEBUG, "EAP-PEAP: Forced PEAP version "
				   "%d", data->force_peap_version);
		}

		if (os_strstr(config->phase1, "peaplabel=1")) {
			data->force_new_label = 1;
			wpa_printf(MSG_DEBUG, "EAP-PEAP: Force new label for "
				   "key derivation");
		}

		if (os_strstr(config->phase1, "peap_outer_success=0")) {
			data->peap_outer_success = 0;
			wpa_printf(MSG_DEBUG, "EAP-PEAP: terminate "
				   "authentication on tunneled EAP-Success");
		} else if (os_strstr(config->phase1, "peap_outer_success=1")) {
			data->peap_outer_success = 1;
			wpa_printf(MSG_DEBUG, "EAP-PEAP: send tunneled "
				   "EAP-Success after receiving tunneled "
				   "EAP-Success");
		} else if (os_strstr(config->phase1, "peap_outer_success=2")) {
			data->peap_outer_success = 2;
			wpa_printf(MSG_DEBUG, "EAP-PEAP: send PEAP/TLS ACK "
				   "after receiving tunneled EAP-Success");
		}
	}

	if (config && config->phase2) {
		char *start, *pos, *buf;
		struct eap_method_type *methods = NULL, *_methods;
		u8 method;
		size_t num_methods = 0;
		start = buf = os_strdup(config->phase2);
		if (buf == NULL) {
			eap_peap_deinit(sm, data);
			return NULL;
		}
		while (start && *start != '\0') {
			int vendor;
			pos = os_strstr(start, "auth=");
			if (pos == NULL)
				break;
			if (start != pos && *(pos - 1) != ' ') {
				start = pos + 5;
				continue;
			}

			start = pos + 5;
			pos = os_strchr(start, ' ');
			if (pos)
				*pos++ = '\0';
			method = eap_get_phase2_type(start, &vendor);
			if (vendor == EAP_VENDOR_IETF &&
			    method == EAP_TYPE_NONE) {
				wpa_printf(MSG_ERROR, "EAP-PEAP: Unsupported "
					   "Phase2 method '%s'", start);
			} else {
				num_methods++;
				_methods = os_realloc(
					methods,
					num_methods * sizeof(*methods));
				if (_methods == NULL) {
					os_free(methods);
					os_free(buf);
					eap_peap_deinit(sm, data);
					return NULL;
				}
				methods = _methods;
				methods[num_methods - 1].vendor = vendor;
				methods[num_methods - 1].method = method;
			}

			start = pos;
		}
		os_free(buf);
		data->phase2_types = methods;
		data->num_phase2_types = num_methods;
	}
	if (data->phase2_types == NULL) {
		data->phase2_types =
			eap_get_phase2_types(config, &data->num_phase2_types);
	}
	if (data->phase2_types == NULL) {
		wpa_printf(MSG_ERROR, "EAP-PEAP: No Phase2 method available");
		eap_peap_deinit(sm, data);
		return NULL;
	}
	wpa_hexdump(MSG_DEBUG, "EAP-PEAP: Phase2 EAP types",
		    (u8 *) data->phase2_types,
		    data->num_phase2_types * sizeof(struct eap_method_type));
	data->phase2_type.vendor = EAP_VENDOR_IETF;
	data->phase2_type.method = EAP_TYPE_NONE;

	if (eap_tls_ssl_init(sm, &data->ssl, config)) {
		wpa_printf(MSG_INFO, "EAP-PEAP: Failed to initialize SSL.");
		eap_peap_deinit(sm, data);
		return NULL;
	}

	return data;
}


static void eap_peap_deinit(struct eap_sm *sm, void *priv)
{
	struct eap_peap_data *data = priv;
	if (data == NULL)
		return;
	if (data->phase2_priv && data->phase2_method)
		data->phase2_method->deinit(sm, data->phase2_priv);
	os_free(data->phase2_types);
	eap_tls_ssl_deinit(sm, &data->ssl);
	os_free(data->key_data);
	os_free(data->pending_phase2_req);
	os_free(data);
}


static int eap_peap_encrypt(struct eap_sm *sm, struct eap_peap_data *data,
			    int id, const u8 *plain, size_t plain_len,
			    u8 **out_data, size_t *out_len)
{
	int res;
	u8 *pos;
	struct eap_hdr *resp;

	/* TODO: add support for fragmentation, if needed. This will need to
	 * add TLS Message Length field, if the frame is fragmented.
	 * Note: Microsoft IAS did not seem to like TLS Message Length with
	 * PEAP/MSCHAPv2. */
	resp = os_malloc(sizeof(struct eap_hdr) + 2 + data->ssl.tls_out_limit);
	if (resp == NULL)
		return -1;

	resp->code = EAP_CODE_RESPONSE;
	resp->identifier = id;

	pos = (u8 *) (resp + 1);
	*pos++ = EAP_TYPE_PEAP;
	*pos++ = data->peap_version;

	res = tls_connection_encrypt(sm->ssl_ctx, data->ssl.conn,
				     plain, plain_len,
				     pos, data->ssl.tls_out_limit);
	if (res < 0) {
		wpa_printf(MSG_INFO, "EAP-PEAP: Failed to encrypt Phase 2 "
			   "data");
		os_free(resp);
		return -1;
	}

	*out_len = sizeof(struct eap_hdr) + 2 + res;
	resp->length = host_to_be16(*out_len);
	*out_data = (u8 *) resp;
	return 0;
}


static int eap_peap_phase2_nak(struct eap_peap_data *data, struct eap_hdr *hdr,
			       u8 **resp, size_t *resp_len)
{
	struct eap_hdr *resp_hdr;
	u8 *pos = (u8 *) (hdr + 1);
	size_t i;

	/* TODO: add support for expanded Nak */
	wpa_printf(MSG_DEBUG, "EAP-PEAP: Phase 2 Request: Nak type=%d", *pos);
	wpa_hexdump(MSG_DEBUG, "EAP-PEAP: Allowed Phase2 EAP types",
		    (u8 *) data->phase2_types,
		    data->num_phase2_types * sizeof(struct eap_method_type));
	*resp_len = sizeof(struct eap_hdr) + 1;
	*resp = os_malloc(*resp_len + data->num_phase2_types);
	if (*resp == NULL)
		return -1;

	resp_hdr = (struct eap_hdr *) (*resp);
	resp_hdr->code = EAP_CODE_RESPONSE;
	resp_hdr->identifier = hdr->identifier;
	pos = (u8 *) (resp_hdr + 1);
	*pos++ = EAP_TYPE_NAK;
	for (i = 0; i < data->num_phase2_types; i++) {
		if (data->phase2_types[i].vendor == EAP_VENDOR_IETF &&
		    data->phase2_types[i].method < 256) {
			(*resp_len)++;
			*pos++ = data->phase2_types[i].method;
		}
	}
	resp_hdr->length = host_to_be16(*resp_len);

	return 0;
}


static int eap_peap_phase2_request(struct eap_sm *sm,
				   struct eap_peap_data *data,
				   struct eap_method_ret *ret,
				   struct eap_hdr *hdr,
				   u8 **resp, size_t *resp_len)
{
	size_t len = be_to_host16(hdr->length);
	u8 *pos;
	struct eap_method_ret iret;
	struct wpa_ssid *config = eap_get_config(sm);

	if (len <= sizeof(struct eap_hdr)) {
		wpa_printf(MSG_INFO, "EAP-PEAP: too short "
			   "Phase 2 request (len=%lu)", (unsigned long) len);
		return -1;
	}
	pos = (u8 *) (hdr + 1);
	wpa_printf(MSG_DEBUG, "EAP-PEAP: Phase 2 Request: type=%d", *pos);
	switch (*pos) {
	case EAP_TYPE_IDENTITY:
		*resp = eap_sm_buildIdentity(sm, hdr->identifier, resp_len, 1);
		break;
	case EAP_TYPE_TLV:
		os_memset(&iret, 0, sizeof(iret));
		if (eap_tlv_process(sm, &iret, hdr, resp, resp_len,
				    data->phase2_eap_started &&
				    !data->phase2_eap_success)) {
			ret->methodState = METHOD_DONE;
			ret->decision = DECISION_FAIL;
			return -1;
		}
		if (iret.methodState == METHOD_DONE ||
		    iret.methodState == METHOD_MAY_CONT) {
			ret->methodState = iret.methodState;
			ret->decision = iret.decision;
			data->phase2_success = 1;
		}
		break;
	default:
		if (data->phase2_type.vendor == EAP_VENDOR_IETF &&
		    data->phase2_type.method == EAP_TYPE_NONE) {
			size_t i;
			for (i = 0; i < data->num_phase2_types; i++) {
				if (data->phase2_types[i].vendor !=
				    EAP_VENDOR_IETF ||
				    data->phase2_types[i].method != *pos)
					continue;

				data->phase2_type.vendor =
					data->phase2_types[i].vendor;
				data->phase2_type.method =
					data->phase2_types[i].method;
				wpa_printf(MSG_DEBUG, "EAP-PEAP: Selected "
					   "Phase 2 EAP vendor %d method %d",
					   data->phase2_type.vendor,
					   data->phase2_type.method);
				break;
			}
		}
		if (*pos != data->phase2_type.method ||
		    *pos == EAP_TYPE_NONE) {
			if (eap_peap_phase2_nak(data, hdr, resp, resp_len))
				return -1;
			return 0;
		}

		if (data->phase2_priv == NULL) {
			data->phase2_method = eap_sm_get_eap_methods(
				data->phase2_type.vendor,
				(EapType)data->phase2_type.method);
			if (data->phase2_method) {
				sm->init_phase2 = 1;
				data->phase2_priv =
					data->phase2_method->init(sm);
				sm->init_phase2 = 0;
			}
		}
		if (data->phase2_priv == NULL || data->phase2_method == NULL) {
			wpa_printf(MSG_INFO, "EAP-PEAP: failed to initialize "
				   "Phase 2 EAP method %d", *pos);
			ret->methodState = METHOD_DONE;
			ret->decision = DECISION_FAIL;
			return -1;
		}
		data->phase2_eap_started = 1;
		os_memset(&iret, 0, sizeof(iret));
		*resp = data->phase2_method->process(sm, data->phase2_priv,
						     &iret, (u8 *) hdr, len,
						     resp_len);
		if ((iret.methodState == METHOD_DONE ||
		     iret.methodState == METHOD_MAY_CONT) &&
		    (iret.decision == DECISION_UNCOND_SUCC ||
		     iret.decision == DECISION_COND_SUCC)) {
			data->phase2_eap_success = 1;
			data->phase2_success = 1;
		}
		break;
	}

	if (*resp == NULL &&
	    (config->pending_req_identity || config->pending_req_password ||
	     config->pending_req_otp || config->pending_req_new_password)) {
		os_free(data->pending_phase2_req);
		data->pending_phase2_req = os_malloc(len);
		if (data->pending_phase2_req) {
			os_memcpy(data->pending_phase2_req, hdr, len);
			data->pending_phase2_req_len = len;
		}
	}

	return 0;
}


static int eap_peap_decrypt(struct eap_sm *sm, struct eap_peap_data *data,
			    struct eap_method_ret *ret,
			    const struct eap_hdr *req,
			    const u8 *in_data, size_t in_len,
			    u8 **out_data, size_t *out_len)
{
	u8 *in_decrypted;
	int res, skip_change = 0;
	struct eap_hdr *hdr, *rhdr;
	u8 *resp = NULL;
	size_t resp_len = 0;
	size_t len_decrypted, len, buf_len;
	const u8 *msg;
	size_t msg_len;
	int need_more_input;

	wpa_printf(MSG_DEBUG, "EAP-PEAP: received %lu bytes encrypted data for"
		   " Phase 2", (unsigned long) in_len);

	if (data->pending_phase2_req) {
		wpa_printf(MSG_DEBUG, "EAP-PEAP: Pending Phase 2 request - "
			   "skip decryption and use old data");
		/* Clear TLS reassembly state. */
		os_free(data->ssl.tls_in);
		data->ssl.tls_in = NULL;
		data->ssl.tls_in_len = 0;
		data->ssl.tls_in_left = 0;
		data->ssl.tls_in_total = 0;
		in_decrypted = data->pending_phase2_req;
		data->pending_phase2_req = NULL;
		len_decrypted = data->pending_phase2_req_len;
		skip_change = 1;
		goto continue_req;
	}

	msg = eap_tls_data_reassemble(sm, &data->ssl, in_data, in_len,
				      &msg_len, &need_more_input);
	if (msg == NULL)
		return need_more_input ? 1 : -1;

	if (in_len == 0 && sm->workaround && data->phase2_success) {
		/*
		 * Cisco ACS seems to be using TLS ACK to terminate
		 * EAP-PEAPv0/GTC. Try to reply with TLS ACK.
		 */
		wpa_printf(MSG_DEBUG, "EAP-PEAP: Received TLS ACK, but "
			   "expected data - acknowledge with TLS ACK since "
			   "Phase 2 has been completed");
		ret->decision = DECISION_COND_SUCC;
		ret->methodState = METHOD_DONE;
		return 1;
	}

	buf_len = in_len;
	if (data->ssl.tls_in_total > buf_len)
		buf_len = data->ssl.tls_in_total;
	in_decrypted = os_malloc(buf_len);
	if (in_decrypted == NULL) {
		os_free(data->ssl.tls_in);
		data->ssl.tls_in = NULL;
		data->ssl.tls_in_len = 0;
		wpa_printf(MSG_WARNING, "EAP-PEAP: failed to allocate memory "
			   "for decryption");
		return -1;
	}

	res = tls_connection_decrypt(sm->ssl_ctx, data->ssl.conn,
				     msg, msg_len, in_decrypted, buf_len);
	os_free(data->ssl.tls_in);
	data->ssl.tls_in = NULL;
	data->ssl.tls_in_len = 0;
	if (res < 0) {
		wpa_printf(MSG_INFO, "EAP-PEAP: Failed to decrypt Phase 2 "
			   "data");
		os_free(in_decrypted);
		return 0;
	}
	len_decrypted = res;

continue_req:
	wpa_hexdump(MSG_DEBUG, "EAP-PEAP: Decrypted Phase 2 EAP", in_decrypted,
		    len_decrypted);

	hdr = (struct eap_hdr *) in_decrypted;
	if (len_decrypted == 5 && hdr->code == EAP_CODE_REQUEST &&
	    be_to_host16(hdr->length) == 5 &&
	    in_decrypted[4] == EAP_TYPE_IDENTITY) {
		/* At least FreeRADIUS seems to send full EAP header with
		 * EAP Request Identity */
		skip_change = 1;
	}
	if (len_decrypted >= 5 && hdr->code == EAP_CODE_REQUEST &&
	    in_decrypted[4] == EAP_TYPE_TLV) {
		skip_change = 1;
	}

	if (data->peap_version == 0 && !skip_change) {
		struct eap_hdr *nhdr = os_malloc(sizeof(struct eap_hdr) +
						 len_decrypted);
		if (nhdr == NULL) {
			os_free(in_decrypted);
			return 0;
		}
		os_memcpy((u8 *) (nhdr + 1), in_decrypted, len_decrypted);
		os_free(in_decrypted);
		nhdr->code = req->code;
		nhdr->identifier = req->identifier;
		nhdr->length = host_to_be16(sizeof(struct eap_hdr) +
					    len_decrypted);

		len_decrypted += sizeof(struct eap_hdr);
		in_decrypted = (u8 *) nhdr;
	}
	hdr = (struct eap_hdr *) in_decrypted;
	if (len_decrypted < sizeof(*hdr)) {
		os_free(in_decrypted);
		wpa_printf(MSG_INFO, "EAP-PEAP: Too short Phase 2 "
			   "EAP frame (len=%lu)",
			   (unsigned long) len_decrypted);
		return 0;
	}
	len = be_to_host16(hdr->length);
	if (len > len_decrypted) {
		os_free(in_decrypted);
		wpa_printf(MSG_INFO, "EAP-PEAP: Length mismatch in "
			   "Phase 2 EAP frame (len=%lu hdr->length=%lu)",
			   (unsigned long) len_decrypted, (unsigned long) len);
		return 0;
	}
	if (len < len_decrypted) {
		wpa_printf(MSG_INFO, "EAP-PEAP: Odd.. Phase 2 EAP header has "
			   "shorter length than full decrypted data "
			   "(%lu < %lu)",
			   (unsigned long) len, (unsigned long) len_decrypted);
		if (sm->workaround && len == 4 && len_decrypted == 5 &&
		    in_decrypted[4] == EAP_TYPE_IDENTITY) {
			/* Radiator 3.9 seems to set Phase 2 EAP header to use
			 * incorrect length for the EAP-Request Identity
			 * packet, so fix the inner header to interoperate..
			 * This was fixed in 2004-06-23 patch for Radiator and
			 * this workaround can be removed at some point. */
			wpa_printf(MSG_INFO, "EAP-PEAP: workaround -> replace "
				   "Phase 2 EAP header len (%lu) with real "
				   "decrypted len (%lu)",
				   (unsigned long) len,
				   (unsigned long) len_decrypted);
			len = len_decrypted;
			hdr->length = host_to_be16(len);
		}
	}
	wpa_printf(MSG_DEBUG, "EAP-PEAP: received Phase 2: code=%d "
		   "identifier=%d length=%lu", hdr->code, hdr->identifier,
		   (unsigned long) len);
	switch (hdr->code) {
	case EAP_CODE_REQUEST:
		if (eap_peap_phase2_request(sm, data, ret, hdr,
					    &resp, &resp_len)) {
			os_free(in_decrypted);
			wpa_printf(MSG_INFO, "EAP-PEAP: Phase2 Request "
				   "processing failed");
			return 0;
		}
		break;
	case EAP_CODE_SUCCESS:
		wpa_printf(MSG_DEBUG, "EAP-PEAP: Phase 2 Success");
		if (data->peap_version == 1) {
			/* EAP-Success within TLS tunnel is used to indicate
			 * shutdown of the TLS channel. The authentication has
			 * been completed. */
			if (data->phase2_eap_started &&
			    !data->phase2_eap_success) {
				wpa_printf(MSG_DEBUG, "EAP-PEAP: Phase 2 "
					   "Success used to indicate success, "
					   "but Phase 2 EAP was not yet "
					   "completed successfully");
				ret->methodState = METHOD_DONE;
				ret->decision = DECISION_FAIL;
				os_free(in_decrypted);
				return 0;
			}
			wpa_printf(MSG_DEBUG, "EAP-PEAP: Version 1 - "
				   "EAP-Success within TLS tunnel - "
				   "authentication completed");
			ret->decision = DECISION_UNCOND_SUCC;
			ret->methodState = METHOD_DONE;
			data->phase2_success = 1;
			if (data->peap_outer_success == 2) {
				os_free(in_decrypted);
				wpa_printf(MSG_DEBUG, "EAP-PEAP: Use TLS ACK "
					   "to finish authentication");
				return 1;
			} else if (data->peap_outer_success == 1) {
				/* Reply with EAP-Success within the TLS
				 * channel to complete the authentication. */
				resp_len = sizeof(struct eap_hdr);
				resp = os_zalloc(resp_len);
				if (resp) {
					rhdr = (struct eap_hdr *) resp;
					rhdr->code = EAP_CODE_SUCCESS;
					rhdr->identifier = hdr->identifier;
					rhdr->length = host_to_be16(resp_len);
				}
			} else {
				/* No EAP-Success expected for Phase 1 (outer,
				 * unencrypted auth), so force EAP state
				 * machine to SUCCESS state. */
				sm->peap_done = TRUE;
			}
		} else {
			/* FIX: ? */
		}
		break;
	case EAP_CODE_FAILURE:
		wpa_printf(MSG_DEBUG, "EAP-PEAP: Phase 2 Failure");
		ret->decision = DECISION_FAIL;
		ret->methodState = METHOD_MAY_CONT;
		ret->allowNotifications = FALSE;
		/* Reply with EAP-Failure within the TLS channel to complete
		 * failure reporting. */
		resp_len = sizeof(struct eap_hdr);
		resp = os_zalloc(resp_len);
		if (resp) {
			rhdr = (struct eap_hdr *) resp;
			rhdr->code = EAP_CODE_FAILURE;
			rhdr->identifier = hdr->identifier;
			rhdr->length = host_to_be16(resp_len);
		}
		break;
	default:
		wpa_printf(MSG_INFO, "EAP-PEAP: Unexpected code=%d in "
			   "Phase 2 EAP header", hdr->code);
		break;
	}

	os_free(in_decrypted);

	if (resp) {
		u8 *resp_pos;
		size_t resp_send_len;
		int skip_change2 = 0;

		wpa_hexdump_key(MSG_DEBUG, "EAP-PEAP: Encrypting Phase 2 data",
				resp, resp_len);
		/* PEAP version changes */
		if (resp_len >= 5 && resp[0] == EAP_CODE_RESPONSE &&
		    resp[4] == EAP_TYPE_TLV)
			skip_change2 = 1;
		if (data->peap_version == 0 && !skip_change2) {
			resp_pos = resp + sizeof(struct eap_hdr);
			resp_send_len = resp_len - sizeof(struct eap_hdr);
		} else {
			resp_pos = resp;
			resp_send_len = resp_len;
		}

		if (eap_peap_encrypt(sm, data, req->identifier,
				     resp_pos, resp_send_len,
				     out_data, out_len)) {
			wpa_printf(MSG_INFO, "EAP-PEAP: Failed to encrypt "
				   "a Phase 2 frame");
		}
		os_free(resp);
	}

	return 0;
}


static u8 * eap_peap_process(struct eap_sm *sm, void *priv,
			     struct eap_method_ret *ret,
			     const u8 *reqData, size_t reqDataLen,
			     size_t *respDataLen)
{
	const struct eap_hdr *req;
	size_t left;
	int res;
	u8 flags, *resp, id;
	const u8 *pos;
	struct eap_peap_data *data = priv;

	pos = eap_tls_process_init(sm, &data->ssl, EAP_TYPE_PEAP, ret,
				   reqData, reqDataLen, &left, &flags);
	if (pos == NULL)
		return NULL;
	req = (const struct eap_hdr *) reqData;
	id = req->identifier;

	if (flags & EAP_TLS_FLAGS_START) {
		wpa_printf(MSG_DEBUG, "EAP-PEAP: Start (server ver=%d, own "
			   "ver=%d)", flags & EAP_PEAP_VERSION_MASK,
			data->peap_version);
		if ((flags & EAP_PEAP_VERSION_MASK) < data->peap_version)
			data->peap_version = flags & EAP_PEAP_VERSION_MASK;
		if (data->force_peap_version >= 0 &&
		    data->force_peap_version != data->peap_version) {
			wpa_printf(MSG_WARNING, "EAP-PEAP: Failed to select "
				   "forced PEAP version %d",
				   data->force_peap_version);
			ret->methodState = METHOD_DONE;
			ret->decision = DECISION_FAIL;
			ret->allowNotifications = FALSE;
			return NULL;
		}
		wpa_printf(MSG_DEBUG, "EAP-PEAP: Using PEAP version %d",
			   data->peap_version);
		left = 0; /* make sure that this frame is empty, even though it
			   * should always be, anyway */
	}

	resp = NULL;
	if (tls_connection_established(sm->ssl_ctx, data->ssl.conn) &&
	    !data->resuming) {
		res = eap_peap_decrypt(sm, data, ret, req, pos, left,
				       &resp, respDataLen);
	} else {
		res = eap_tls_process_helper(sm, &data->ssl, EAP_TYPE_PEAP,
					     data->peap_version, id, pos, left,
					     &resp, respDataLen);

		if (tls_connection_established(sm->ssl_ctx, data->ssl.conn)) {
			char *label;
			wpa_printf(MSG_DEBUG,
				   "EAP-PEAP: TLS done, proceed to Phase 2");
			os_free(data->key_data);
			/* draft-josefsson-ppext-eap-tls-eap-05.txt
			 * specifies that PEAPv1 would use "client PEAP
			 * encryption" as the label. However, most existing
			 * PEAPv1 implementations seem to be using the old
			 * label, "client EAP encryption", instead. Use the old
			 * label by default, but allow it to be configured with
			 * phase1 parameter peaplabel=1. */
			if (data->peap_version > 1 || data->force_new_label)
				label = "client PEAP encryption";
			else
				label = "client EAP encryption";
			wpa_printf(MSG_DEBUG, "EAP-PEAP: using label '%s' in "
				   "key derivation", label);
			data->key_data =
				eap_tls_derive_key(sm, &data->ssl, label,
						   EAP_TLS_KEY_LEN);
			if (data->key_data) {
				wpa_hexdump_key(MSG_DEBUG, 
						"EAP-PEAP: Derived key",
						data->key_data,
						EAP_TLS_KEY_LEN);
			} else {
				wpa_printf(MSG_DEBUG, "EAP-PEAP: Failed to "
					   "derive key");
			}

			if (sm->workaround && data->resuming) {
				/*
				 * At least few RADIUS servers (Aegis v1.1.6;
				 * but not v1.1.4; and Cisco ACS) seem to be
				 * terminating PEAPv1 (Aegis) or PEAPv0 (Cisco
				 * ACS) session resumption with outer
				 * EAP-Success. This does not seem to follow
				 * draft-josefsson-pppext-eap-tls-eap-05.txt
				 * section 4.2, so only allow this if EAP
				 * workarounds are enabled.
				 */
				wpa_printf(MSG_DEBUG, "EAP-PEAP: Workaround - "
					   "allow outer EAP-Success to "
					   "terminate PEAP resumption");
				ret->decision = DECISION_COND_SUCC;
				data->phase2_success = 1;
			}

			data->resuming = 0;
		}

		if (res == 2) {
			/*
			 * Application data included in the handshake message.
			 */
			os_free(data->pending_phase2_req);
			data->pending_phase2_req = resp;
			data->pending_phase2_req_len = *respDataLen;
			resp = NULL;
			*respDataLen = 0;
			res = eap_peap_decrypt(sm, data, ret, req, pos, left,
					       &resp, respDataLen);
		}
	}

	if (ret->methodState == METHOD_DONE) {
		ret->allowNotifications = FALSE;
	}

	if (res == 1) {
		return eap_tls_build_ack(&data->ssl, respDataLen, id,
					 EAP_TYPE_PEAP, data->peap_version);
	}

	return resp;
}


static Boolean eap_peap_has_reauth_data(struct eap_sm *sm, void *priv)
{
	struct eap_peap_data *data = priv;
	return (Boolean) (tls_connection_established(sm->ssl_ctx, data->ssl.conn) &&
		              data->phase2_success);
}


static void eap_peap_deinit_for_reauth(struct eap_sm *sm, void *priv)
{
	struct eap_peap_data *data = priv;
	os_free(data->pending_phase2_req);
	data->pending_phase2_req = NULL;
}


static void * eap_peap_init_for_reauth(struct eap_sm *sm, void *priv)
{
	struct eap_peap_data *data = priv;
	os_free(data->key_data);
	data->key_data = NULL;
	if (eap_tls_reauth_init(sm, &data->ssl)) {
		os_free(data);
		return NULL;
	}
	if (data->phase2_priv && data->phase2_method &&
	    data->phase2_method->init_for_reauth)
		data->phase2_method->init_for_reauth(sm, data->phase2_priv);
	data->phase2_success = 0;
	data->phase2_eap_success = 0;
	data->phase2_eap_started = 0;
	data->resuming = 1;
	sm->peap_done = FALSE;
	return priv;
}


static int eap_peap_get_status(struct eap_sm *sm, void *priv, char *buf,
			       size_t buflen, int verbose)
{
	struct eap_peap_data *data = priv;
	int len, ret;

	len = eap_tls_status(sm, &data->ssl, buf, buflen, verbose);
	if (data->phase2_method) {
		ret = os_snprintf(buf + len, buflen - len,
				  "EAP-PEAPv%d Phase2 method=%s\n",
				  data->peap_version,
				  data->phase2_method->name);
		if (ret < 0 || (size_t) ret >= buflen - len)
			return len;
		len += ret;
	}
	return len;
}


static Boolean eap_peap_isKeyAvailable(struct eap_sm *sm, void *priv)
{
	struct eap_peap_data *data = priv;
	return (Boolean) (data->key_data != NULL && data->phase2_success);
}


static u8 * eap_peap_getKey(struct eap_sm *sm, void *priv, size_t *len)
{
	struct eap_peap_data *data = priv;
	u8 *key;

	if (data->key_data == NULL || !data->phase2_success)
		return NULL;

	key = os_malloc(EAP_TLS_KEY_LEN);
	if (key == NULL)
		return NULL;

	*len = EAP_TLS_KEY_LEN;
	os_memcpy(key, data->key_data, EAP_TLS_KEY_LEN);

	return key;
}


int eap_peer_peap_register(void)
{
	struct eap_method *eap;
	int ret;

	eap = eap_peer_method_alloc(EAP_PEER_METHOD_INTERFACE_VERSION,
				    EAP_VENDOR_IETF, EAP_TYPE_PEAP, "PEAP");
	if (eap == NULL)
		return -1;

	eap->init = eap_peap_init;
	eap->deinit = eap_peap_deinit;
	eap->process = eap_peap_process;
	eap->isKeyAvailable = eap_peap_isKeyAvailable;
	eap->getKey = eap_peap_getKey;
	eap->get_status = eap_peap_get_status;
	eap->has_reauth_data = eap_peap_has_reauth_data;
	eap->deinit_for_reauth = eap_peap_deinit_for_reauth;
	eap->init_for_reauth = eap_peap_init_for_reauth;

	ret = eap_peer_method_register(eap);
	if (ret)
		eap_peer_method_free(eap);
	return ret;
}
