/*
 * EAP peer: EAP-TLS/PEAP/TTLS/FAST common functions
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
#include "md5.h"
#include "sha1.h"
#include "tls.h"
#include "config.h"


static int eap_tls_check_blob(struct eap_sm *sm, const char **name,
			      const u8 **data, size_t *data_len)
{
	const struct wpa_config_blob *blob;

	if (*name == NULL || os_strncmp(*name, "blob://", 7) != 0)
		return 0;

	blob = eap_get_config_blob(sm, *name + 7);
	if (blob == NULL) {
		wpa_printf(MSG_ERROR, "%s: Named configuration blob '%s' not "
			   "found", __func__, *name + 7);
		return -1;
	}

	*name = NULL;
	*data = blob->data;
	*data_len = blob->len;

	return 0;
}


static void eap_tls_params_from_conf1(struct tls_connection_params *params,
				      struct wpa_ssid *config)
{
	params->ca_cert = (char *) config->ca_cert;
	params->ca_path = (char *) config->ca_path;
	params->client_cert = (char *) config->client_cert;
	params->private_key = (char *) config->private_key;
	params->private_key_passwd = (char *) config->private_key_passwd;
	params->dh_file = (char *) config->dh_file;
	params->subject_match = (char *) config->subject_match;
	params->altsubject_match = (char *) config->altsubject_match;
	params->engine_id = config->engine_id;
	params->pin = config->pin;
	params->key_id = config->key_id;
}


static void eap_tls_params_from_conf2(struct tls_connection_params *params,
				      struct wpa_ssid *config)
{
	params->ca_cert = (char *) config->ca_cert2;
	params->ca_path = (char *) config->ca_path2;
	params->client_cert = (char *) config->client_cert2;
	params->private_key = (char *) config->private_key2;
	params->private_key_passwd = (char *) config->private_key2_passwd;
	params->dh_file = (char *) config->dh_file2;
	params->subject_match = (char *) config->subject_match2;
	params->altsubject_match = (char *) config->altsubject_match2;
}


static int eap_tls_params_from_conf(struct eap_sm *sm,
				    struct eap_ssl_data *data,
				    struct tls_connection_params *params,
				    struct wpa_ssid *config, int phase2)
{
	os_memset(params, 0, sizeof(*params));
	params->engine = config->engine;
	if (phase2)
		eap_tls_params_from_conf2(params, config);
	else
		eap_tls_params_from_conf1(params, config);
	params->tls_ia = data->tls_ia;


	if (eap_tls_check_blob(sm, &params->ca_cert, &params->ca_cert_blob,
			       &params->ca_cert_blob_len) ||
	    eap_tls_check_blob(sm, &params->client_cert,
			       &params->client_cert_blob,
			       &params->client_cert_blob_len) ||
	    eap_tls_check_blob(sm, &params->private_key,
			       &params->private_key_blob,
			       &params->private_key_blob_len) ||
	    eap_tls_check_blob(sm, &params->dh_file, &params->dh_blob,
			       &params->dh_blob_len)) {
		wpa_printf(MSG_INFO, "SSL: Failed to get configuration blobs");
		return -1;
	}

	return 0;
}


static int eap_tls_init_connection(struct eap_sm *sm,
				   struct eap_ssl_data *data,
				   struct wpa_ssid *config,
				   struct tls_connection_params *params)
{
	int res;

	data->conn = tls_connection_init(sm->ssl_ctx);
	if (data->conn == NULL) {
		wpa_printf(MSG_INFO, "SSL: Failed to initialize new TLS "
			   "connection");
		return -1;
	}

	res = tls_connection_set_params(sm->ssl_ctx, data->conn, params);
	if (res == TLS_SET_PARAMS_ENGINE_PRV_INIT_FAILED) {
		/* At this point with the pkcs11 engine the PIN might be wrong.
		 * We reset the PIN in the configuration to be sure to not use
		 * it again and the calling function must request a new one */
		os_free(config->pin);
		config->pin = NULL;
	} else if (res == TLS_SET_PARAMS_ENGINE_PRV_VERIFY_FAILED) {
		wpa_printf(MSG_INFO, "TLS: Failed to load private key");
		/* We don't know exactly but maybe the PIN was wrong,
		 * so ask for a new one. */
		os_free(config->pin);
		config->pin = NULL;
		eap_sm_request_pin(sm);
		sm->ignore = TRUE;
		return -1;
	} else if (res) {
		wpa_printf(MSG_INFO, "TLS: Failed to set TLS connection "
			   "parameters");
		return -1;
	}

	return 0;
}


/**
 * eap_tls_ssl_init - Initialize shared TLS functionality
 * @sm: Pointer to EAP state machine allocated with eap_sm_init()
 * @data: Data for TLS processing
 * @config: Pointer to the network configuration
 * Returns: 0 on success, -1 on failure
 *
 * This function is used to initialize shared TLS functionality for EAP-TLS,
 * EAP-PEAP, EAP-TTLS, and EAP-FAST.
 */
int eap_tls_ssl_init(struct eap_sm *sm, struct eap_ssl_data *data,
		     struct wpa_ssid *config)
{
	int ret = -1;
	struct tls_connection_params params;

	if (config == NULL)
		return -1;

	data->eap = sm;
	data->phase2 = sm->init_phase2;
	if (eap_tls_params_from_conf(sm, data, &params, config, data->phase2) <
	    0)
		goto done;

	if (eap_tls_init_connection(sm, data, config, &params) < 0)
		goto done;

	data->tls_out_limit = 1398;//config->fragment_size;
	if (data->phase2) {
		/* Limit the fragment size in the inner TLS authentication
		 * since the outer authentication with EAP-PEAP does not yet
		 * support fragmentation */
		if (data->tls_out_limit > 100)
			data->tls_out_limit -= 100;
	}

	if (config->phase1 &&
	    os_strstr(config->phase1, "include_tls_length=1")) {
		wpa_printf(MSG_DEBUG, "TLS: Include TLS Message Length in "
			   "unfragmented packets");
		data->include_tls_length = 1;
	}

	ret = 0;

done:
	return ret;
}


/**
 * eap_tls_ssl_deinit - Deinitialize shared TLS functionality
 * @sm: Pointer to EAP state machine allocated with eap_sm_init()
 * @data: Data for TLS processing
 *
 * This function deinitializes shared TLS functionality that was initialized
 * with eap_tls_ssl_init().
 */
void eap_tls_ssl_deinit(struct eap_sm *sm, struct eap_ssl_data *data)
{
	tls_connection_deinit(sm->ssl_ctx, data->conn);
	os_free(data->tls_in);
	os_free(data->tls_out);
}


/**
 * eap_tls_derive_key - Derive a key based on TLS session data
 * @sm: Pointer to EAP state machine allocated with eap_sm_init()
 * @data: Data for TLS processing
 * @label: Label string for deriving the keys, e.g., "client EAP encryption"
 * @len: Length of the key material to generate (usually 64 for MSK)
 * Returns: Pointer to allocated key on success or %NULL on failure
 *
 * This function uses TLS-PRF to generate pseudo-random data based on the TLS
 * session data (client/server random and master key). Each key type may use a
 * different label to bind the key usage into the generated material.
 *
 * The caller is responsible for freeing the returned buffer.
 */
u8 * eap_tls_derive_key(struct eap_sm *sm, struct eap_ssl_data *data,
			const char *label, size_t len)
{
	struct tls_keys keys;
	u8 *rnd = NULL, *out;

	out = os_malloc(len);
	if (out == NULL)
		return NULL;

	/* First, try to use TLS library function for PRF, if available. */
	if (tls_connection_prf(sm->ssl_ctx, data->conn, label, 0, out, len) ==
	    0)
		return out;

	/*
	 * TLS library did not support key generation, so get the needed TLS
	 * session parameters and use an internal implementation of TLS PRF to
	 * derive the key.
	 */
	if (tls_connection_get_keys(sm->ssl_ctx, data->conn, &keys))
		goto fail;

	if (keys.client_random == NULL || keys.server_random == NULL ||
	    keys.master_key == NULL)
		goto fail;

	rnd = os_malloc(keys.client_random_len + keys.server_random_len);
	if (rnd == NULL)
		goto fail;
	os_memcpy(rnd, keys.client_random, keys.client_random_len);
	os_memcpy(rnd + keys.client_random_len, keys.server_random,
		  keys.server_random_len);

	if (tls_prf(keys.master_key, keys.master_key_len,
		    label, rnd, keys.client_random_len +
		    keys.server_random_len, out, len))
		goto fail;

	os_free(rnd);
	return out;

fail:
	os_free(out);
	os_free(rnd);
	return NULL;
}


/**
 * eap_tls_data_reassemble - Reassemble TLS data
 * @sm: Pointer to EAP state machine allocated with eap_sm_init()
 * @data: Data for TLS processing
 * @in_data: Next incoming TLS segment
 * @in_len: Length of in_data
 * @out_len: Variable for returning output data length
 * @need_more_input: Variable for returning whether more input data is needed
 * to reassemble this TLS packet
 * Returns: Pointer to output data, %NULL on error or when more data is needed
 * for the full message (in which case, *need_more_input is also set to 1).
 *
 * This function reassembles TLS fragments. Caller must not free the returned
 * data buffer since an internal pointer to it is maintained.
 */
const u8 * eap_tls_data_reassemble(
	struct eap_sm *sm, struct eap_ssl_data *data, const u8 *in_data,
	size_t in_len, size_t *out_len, int *need_more_input)
{
	u8 *buf;

	*need_more_input = 0;

	if (data->tls_in_left > in_len || data->tls_in) {
		if (data->tls_in_len + in_len == 0) {
			os_free(data->tls_in);
			data->tls_in = NULL;
			data->tls_in_len = 0;
			wpa_printf(MSG_WARNING, "SSL: Invalid reassembly "
				   "state: tls_in_left=%lu tls_in_len=%lu "
				   "in_len=%lu",
				   (unsigned long) data->tls_in_left,
				   (unsigned long) data->tls_in_len,
				   (unsigned long) in_len);
			return NULL;
		}
		if (data->tls_in_len + in_len > 65536) {
			/* Limit length to avoid rogue servers from causing
			 * large memory allocations. */
			os_free(data->tls_in);
			data->tls_in = NULL;
			data->tls_in_len = 0;
			wpa_printf(MSG_INFO, "SSL: Too long TLS fragment (size"
				   " over 64 kB)");
			return NULL;
		}
		buf = os_realloc(data->tls_in, data->tls_in_len + in_len);
		if (buf == NULL) {
			os_free(data->tls_in);
			data->tls_in = NULL;
			data->tls_in_len = 0;
			wpa_printf(MSG_INFO, "SSL: Could not allocate memory "
				   "for TLS data");
			return NULL;
		}
		os_memcpy(buf + data->tls_in_len, in_data, in_len);
		data->tls_in = buf;
		data->tls_in_len += in_len;
		if (in_len > data->tls_in_left) {
			wpa_printf(MSG_INFO, "SSL: more data than TLS message "
				   "length indicated");
			data->tls_in_left = 0;
			return NULL;
		}
		data->tls_in_left -= in_len;
		if (data->tls_in_left > 0) {
			wpa_printf(MSG_DEBUG, "SSL: Need %lu bytes more input "
				   "data", (unsigned long) data->tls_in_left);
			*need_more_input = 1;
			return NULL;
		}
	} else {
		data->tls_in_left = 0;
		data->tls_in = os_malloc(in_len ? in_len : 1);
		if (data->tls_in == NULL)
			return NULL;
		os_memcpy(data->tls_in, in_data, in_len);
		data->tls_in_len = in_len;
	}

	*out_len = data->tls_in_len;
	return data->tls_in;
}


static int eap_tls_process_input(struct eap_sm *sm, struct eap_ssl_data *data,
				 const u8 *in_data, size_t in_len,
				 u8 **out_data, size_t *out_len)
{
	const u8 *msg;
	size_t msg_len;
	int need_more_input;
	u8 *appl_data;
	size_t appl_data_len = 0;

	msg = eap_tls_data_reassemble(sm, data, in_data, in_len,
				      &msg_len, &need_more_input);
	if (msg == NULL)
		return need_more_input ? 1 : -1;

	/* Full TLS message reassembled - continue handshake processing */
	if (data->tls_out) {
		/* This should not happen.. */
		wpa_printf(MSG_INFO, "SSL: eap_tls_process_helper - pending "
			   "tls_out data even though tls_out_len = 0");
		os_free(data->tls_out);
		WPA_ASSERT(data->tls_out == NULL);
	}
	appl_data = NULL;
	data->tls_out = tls_connection_handshake(sm->ssl_ctx, data->conn,
						 msg, msg_len,
						 &data->tls_out_len,
						 &appl_data, &appl_data_len);

	/* Clear reassembled input data (if the buffer was needed). */
	data->tls_in_left = data->tls_in_total = data->tls_in_len = 0;
	os_free(data->tls_in);
	data->tls_in = NULL;

	if (appl_data &&
	    tls_connection_established(sm->ssl_ctx, data->conn) &&
	    !tls_connection_get_failed(sm->ssl_ctx, data->conn)) {
		wpa_hexdump_key(MSG_MSGDUMP, "SSL: Application data",
				appl_data, appl_data_len);
		*out_data = appl_data;
		*out_len = appl_data_len;
		return 2;
	}

	os_free(appl_data);

	return 0;
}


static int eap_tls_process_output(struct eap_ssl_data *data, EapType eap_type,
				  int peap_version, u8 id, int ret,
				  u8 **out_data, size_t *out_len)
{
	size_t len;
	u8 *pos, *flags;
	int more_fragments, length_included;
	
	wpa_printf(MSG_DEBUG, "SSL: %lu bytes left to be sent out (of total "
		   "%lu bytes)",
		   (unsigned long) data->tls_out_len - data->tls_out_pos,
		   (unsigned long) data->tls_out_len);

	len = data->tls_out_len - data->tls_out_pos;
	if (len > data->tls_out_limit) {
		more_fragments = 1;
		len = data->tls_out_limit;
		wpa_printf(MSG_DEBUG, "SSL: sending %lu bytes, more fragments "
			   "will follow", (unsigned long) len);
	} else
		more_fragments = 0;

	length_included = data->tls_out_pos == 0 &&
		(data->tls_out_len > data->tls_out_limit ||
		 data->include_tls_length);

	*out_data = (u8 *)
		eap_msg_alloc(EAP_VENDOR_IETF, eap_type, out_len,
			      1 + length_included * 4 + len, EAP_CODE_RESPONSE,
			      id, &pos);
	if (*out_data == NULL)
		return -1;

	flags = pos++;
	*flags = peap_version;
	if (more_fragments)
		*flags |= EAP_TLS_FLAGS_MORE_FRAGMENTS;
	if (length_included) {
		*flags |= EAP_TLS_FLAGS_LENGTH_INCLUDED;
		WPA_PUT_BE32(pos, data->tls_out_len);
		pos += 4;
	}

	os_memcpy(pos, &data->tls_out[data->tls_out_pos], len);
	data->tls_out_pos += len;

	if (!more_fragments) {
		data->tls_out_len = 0;
		data->tls_out_pos = 0;
		os_free(data->tls_out);
		data->tls_out = NULL;
	}

	return ret;
}


/**
 * eap_tls_process_helper - Process TLS handshake message
 * @sm: Pointer to EAP state machine allocated with eap_sm_init()
 * @data: Data for TLS processing
 * @eap_type: EAP type (EAP_TYPE_TLS, EAP_TYPE_PEAP, ...)
 * @peap_version: Version number for EAP-PEAP/TTLS
 * @id: EAP identifier for the response
 * @in_data: Message received from the server
 * @in_len: Length of in_data
 * @out_data: Buffer for returning a pointer to the response message
 * @out_len: Buffer for returning the length of the response message
 * Returns: 0 on success, 1 if more input data is needed, or -1 on failure
 *
 * This function can be used to process TLS handshake messages. It reassembles
 * the received fragments and uses a TLS library to process the messages. The
 * response data from the TLS library is fragmented to suitable output messages
 * that the caller can send out.
 *
 * out_data is used to return the response message if the return value of this
 * function is 0 or -1. In case of failure, the message is likely a TLS alarm
 * message. The caller is responsible for freeing the allocated buffer if
 * *out_data is not %NULL.
 */
int eap_tls_process_helper(struct eap_sm *sm, struct eap_ssl_data *data,
			   EapType eap_type, int peap_version,
			   u8 id, const u8 *in_data, size_t in_len,
			   u8 **out_data, size_t *out_len)
{
	int ret = 0;

	WPA_ASSERT(data->tls_out_len == 0 || in_len == 0);
	*out_len = 0;
	*out_data = NULL;

	if (data->tls_out_len == 0) {
		/* No more data to send out - expect to receive more data from
		 * the AS. */
		int res = eap_tls_process_input(sm, data, in_data, in_len,
						out_data, out_len);
		if (res)
			return res;
	}

	if (data->tls_out == NULL) {
		data->tls_out_len = 0;
		return -1;
	}

	if (tls_connection_get_failed(sm->ssl_ctx, data->conn)) {
		wpa_printf(MSG_DEBUG, "SSL: Failed - tls_out available to "
			   "report error");
		ret = -1;
		/* TODO: clean pin if engine used? */
	}

	if (data->tls_out_len == 0) {
		/* TLS negotiation should now be complete since all other cases
		 * needing more data should have been caught above based on
		 * the TLS Message Length field. */
		wpa_printf(MSG_DEBUG, "SSL: No data to be sent out");
		os_free(data->tls_out);
		data->tls_out = NULL;
		return 1;
	}

	return eap_tls_process_output(data, eap_type, peap_version, id, ret,
				      out_data, out_len);
}


/**
 * eap_tls_build_ack - Build a TLS ACK frames
 * @data: Data for TLS processing
 * @respDataLen: Buffer for returning the length of the response message
 * @id: EAP identifier for the response
 * @eap_type: EAP type (EAP_TYPE_TLS, EAP_TYPE_PEAP, ...)
 * @peap_version: Version number for EAP-PEAP/TTLS
 * Returns: Pointer to allocated ACK frames or %NULL on failure
 */
u8 * eap_tls_build_ack(struct eap_ssl_data *data, size_t *respDataLen, u8 id,
		       EapType eap_type, int peap_version)
{
	struct eap_hdr *resp;
	u8 *pos;

	resp = eap_msg_alloc(EAP_VENDOR_IETF, eap_type, respDataLen,
			     1, EAP_CODE_RESPONSE, id, &pos);
	if (resp == NULL)
		return NULL;
	wpa_printf(MSG_DEBUG, "SSL: Building ACK");
	*pos = peap_version; /* Flags */
	return (u8 *) resp;
}


/**
 * eap_tls_reauth_init - Re-initialize shared TLS for session resumption
 * @sm: Pointer to EAP state machine allocated with eap_sm_init()
 * @data: Data for TLS processing
 * Returns: 0 on success, -1 on failure
 */
int eap_tls_reauth_init(struct eap_sm *sm, struct eap_ssl_data *data)
{
	os_free(data->tls_in);
	data->tls_in = NULL;
	data->tls_in_len = data->tls_in_left = data->tls_in_total = 0;
	os_free(data->tls_out);
	data->tls_out = NULL;
	data->tls_out_len = data->tls_out_pos = 0;

	return tls_connection_shutdown(sm->ssl_ctx, data->conn);
}


/**
 * eap_tls_status - Get TLS status
 * @sm: Pointer to EAP state machine allocated with eap_sm_init()
 * @data: Data for TLS processing
 * @buf: Buffer for status information
 * @buflen: Maximum buffer length
 * @verbose: Whether to include verbose status information
 * Returns: Number of bytes written to buf.
 */
int eap_tls_status(struct eap_sm *sm, struct eap_ssl_data *data, char *buf,
		   size_t buflen, int verbose)
{
	char name[128];
	int len = 0, ret;

	if (tls_get_cipher(sm->ssl_ctx, data->conn, name, sizeof(name)) == 0) {
		ret = os_snprintf(buf + len, buflen - len,
				  "EAP TLS cipher=%s\n", name);
		if (ret < 0 || (size_t) ret >= buflen - len)
			return len;
		len += ret;
	}

	return len;
}


/**
 * eap_tls_process_init - Initial validation and processing of EAP requests
 * @sm: Pointer to EAP state machine allocated with eap_sm_init()
 * @data: Data for TLS processing
 * @eap_type: EAP type (EAP_TYPE_TLS, EAP_TYPE_PEAP, ...)
 * @ret: Return values from EAP request validation and processing
 * @reqData: EAP request to be processed (eapReqData)
 * @reqDataLen: Length of the EAP request
 * @len: Buffer for returning length of the remaining payload
 * @flags: Buffer for returning TLS flags
 * Returns: Buffer to payload after TLS flags and length or %NULL on failure
 */
const u8 * eap_tls_process_init(struct eap_sm *sm, struct eap_ssl_data *data,
				EapType eap_type, struct eap_method_ret *ret,
				const u8 *reqData, size_t reqDataLen,
				size_t *len, u8 *flags)
{
	const u8 *pos;
	size_t left;
	unsigned int tls_msg_len;

	if (tls_get_errors(sm->ssl_ctx)) {
		wpa_printf(MSG_INFO, "SSL: TLS errors detected");
		ret->ignore = TRUE;
		return NULL;
	}

	pos = eap_hdr_validate(EAP_VENDOR_IETF, eap_type, reqData, reqDataLen,
			       &left);
	if (pos == NULL) {
		ret->ignore = TRUE;
		return NULL;
	}
	*flags = *pos++;
	left--;
	wpa_printf(MSG_DEBUG, "SSL: Received packet(len=%lu) - "
		   "Flags 0x%02x", (unsigned long) reqDataLen, *flags);
	if (*flags & EAP_TLS_FLAGS_LENGTH_INCLUDED) {
		if (left < 4) {
			wpa_printf(MSG_INFO, "SSL: Short frame with TLS "
				   "length");
			ret->ignore = TRUE;
			return NULL;
		}
		tls_msg_len = WPA_GET_BE32(pos);
		wpa_printf(MSG_DEBUG, "SSL: TLS Message Length: %d",
			   tls_msg_len);
		if (data->tls_in_left == 0) {
			data->tls_in_total = tls_msg_len;
			data->tls_in_left = tls_msg_len;
			os_free(data->tls_in);
			data->tls_in = NULL;
			data->tls_in_len = 0;
		}
		pos += 4;
		left -= 4;
	}

	ret->ignore = FALSE;
	ret->methodState = METHOD_MAY_CONT;
	ret->decision = DECISION_FAIL;
	ret->allowNotifications = TRUE;

	*len = (size_t) left;
	return pos;
}
