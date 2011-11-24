/*
 * EAP peer: EAP-TLS/PEAP/TTLS/FAST common functions
 * Copyright (c) 2004-2005, Jouni Malinen <j@w1.fi>
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

#ifndef EAP_TLS_COMMON_H
#define EAP_TLS_COMMON_H

struct eap_ssl_data {
	struct tls_connection *conn;

	u8 *tls_out;
	size_t tls_out_len;
	size_t tls_out_pos;
	size_t tls_out_limit;
	u8 *tls_in;
	size_t tls_in_len;
	size_t tls_in_left;
	size_t tls_in_total;

	int phase2;
	int include_tls_length; /* include TLS length field even if the TLS
				 * data is not fragmented */
	int tls_ia; /* Enable TLS/IA */

	struct eap_sm *eap;
};


/* EAP TLS Flags */
#define EAP_TLS_FLAGS_LENGTH_INCLUDED 0x80
#define EAP_TLS_FLAGS_MORE_FRAGMENTS 0x40
#define EAP_TLS_FLAGS_START 0x20
#define EAP_PEAP_VERSION_MASK 0x07

 /* could be up to 128 bytes, but only the first 64 bytes are used */
#define EAP_TLS_KEY_LEN 64


int eap_tls_ssl_init(struct eap_sm *sm, struct eap_ssl_data *data,
		     struct wpa_ssid *config);
void eap_tls_ssl_deinit(struct eap_sm *sm, struct eap_ssl_data *data);
u8 * eap_tls_derive_key(struct eap_sm *sm, struct eap_ssl_data *data,
			const char *label, size_t len);
const u8 * eap_tls_data_reassemble(
	struct eap_sm *sm, struct eap_ssl_data *data, const u8 *in_data,
	size_t in_len, size_t *out_len, int *need_more_input);
int eap_tls_process_helper(struct eap_sm *sm, struct eap_ssl_data *data,
			   EapType eap_type, int peap_version,
			   u8 id, const u8 *in_data, size_t in_len,
			   u8 **out_data, size_t *out_len);
u8 * eap_tls_build_ack(struct eap_ssl_data *data, size_t *respDataLen, u8 id,
		       EapType eap_type, int peap_version);
int eap_tls_reauth_init(struct eap_sm *sm, struct eap_ssl_data *data);
int eap_tls_status(struct eap_sm *sm, struct eap_ssl_data *data, char *buf,
		   size_t buflen, int verbose);
const u8 * eap_tls_process_init(struct eap_sm *sm, struct eap_ssl_data *data,
				EapType eap_type, struct eap_method_ret *ret,
				const u8 *reqData, size_t reqDataLen,
				size_t *len, u8 *flags);

#endif /* EAP_TLS_COMMON_H */
