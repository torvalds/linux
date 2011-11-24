/*
 * EAP peer method: EAP-MD5 (RFC 3748 and RFC 1994)
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
#include "md5.h"
#include "crypto.h"


static void * eap_md5_init(struct eap_sm *sm)
{
	/* No need for private data. However, must return non-NULL to indicate
	 * success. */
	return (void *) 1;
}


static void eap_md5_deinit(struct eap_sm *sm, void *priv)
{
}


static u8 * eap_md5_process(struct eap_sm *sm, void *priv,
			    struct eap_method_ret *ret,
			    const u8 *reqData, size_t reqDataLen,
			    size_t *respDataLen)
{
	const struct eap_hdr *req;
	struct eap_hdr *resp;
	const u8 *pos, *challenge, *password;
	u8 *rpos;
	size_t len, challenge_len, password_len;
	const u8 *addr[3];
	size_t elen[3];

	password = eap_get_config_password(sm, &password_len);
	if (password == NULL) {
		wpa_printf(MSG_INFO, "EAP-MD5: Password not configured");
		eap_sm_request_password(sm);
		ret->ignore = TRUE;
		return NULL;
	}

	pos = eap_hdr_validate(EAP_VENDOR_IETF, EAP_TYPE_MD5,
			       reqData, reqDataLen, &len);
	if (pos == NULL || len == 0) {
		wpa_printf(MSG_INFO, "EAP-MD5: Invalid frame (pos=%p len=%lu)",
			   pos, (unsigned long) len);
		ret->ignore = TRUE;
		return NULL;
	}

	/*
	 * CHAP Challenge:
	 * Value-Size (1 octet) | Value(Challenge) | Name(optional)
	 */
	req = (const struct eap_hdr *) reqData;
	challenge_len = *pos++;
	if (challenge_len == 0 || challenge_len > len - 1) {
		wpa_printf(MSG_INFO, "EAP-MD5: Invalid challenge "
			   "(challenge_len=%lu len=%lu)",
			   (unsigned long) challenge_len, (unsigned long) len);
		ret->ignore = TRUE;
		return NULL;
	}
	ret->ignore = FALSE;
	challenge = pos;
	wpa_hexdump(MSG_MSGDUMP, "EAP-MD5: Challenge",
		    challenge, challenge_len);

	wpa_printf(MSG_DEBUG, "EAP-MD5: Generating Challenge Response");
	ret->methodState = METHOD_DONE;
	ret->decision = DECISION_UNCOND_SUCC;
	ret->allowNotifications = TRUE;

	resp = eap_msg_alloc(EAP_VENDOR_IETF, EAP_TYPE_MD5, respDataLen,
			     1 + MD5_MAC_LEN, EAP_CODE_RESPONSE,
			     req->identifier, &rpos);
	if (resp == NULL)
		return NULL;

	/*
	 * CHAP Response:
	 * Value-Size (1 octet) | Value(Response) | Name(optional)
	 */
	*rpos++ = MD5_MAC_LEN;

	addr[0] = &resp->identifier;
	elen[0] = 1;
	addr[1] = password;
	elen[1] = password_len;
	addr[2] = challenge;
	elen[2] = challenge_len;
	md5_vector(3, addr, elen, rpos);
	wpa_hexdump(MSG_MSGDUMP, "EAP-MD5: Response", rpos, MD5_MAC_LEN);

	return (u8 *) resp;
}


int eap_peer_md5_register(void)
{
	struct eap_method *eap;
	int ret;

	eap = eap_peer_method_alloc(EAP_PEER_METHOD_INTERFACE_VERSION,
				    EAP_VENDOR_IETF, EAP_TYPE_MD5, "MD5");
	if (eap == NULL)
		return -1;

	eap->init = eap_md5_init;
	eap->deinit = eap_md5_deinit;
	eap->process = eap_md5_process;

	ret = eap_peer_method_register(eap);
	if (ret)
		eap_peer_method_free(eap);
	return ret;
}
