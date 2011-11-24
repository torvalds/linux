/*
 * WPA Supplicant / TLS interface functions and an internal TLS implementation
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
 *
 * This file interface functions for hostapd/wpa_supplicant to use the
 * integrated TLSv1 implementation.
 */

#include "includes.h"

#include "common.h"
#include "tls.h"
#include "tlsv1_client.h"


static int tls_ref_count = 0;

struct tls_global {
	int dummy;
};

struct tls_connection {
	struct tlsv1_client *client;
};


void * tls_init(const struct tls_config *conf)
{
	struct tls_global *global;

	if (tls_ref_count == 0) {
		if (tlsv1_client_global_init())
			return NULL;
	}
	tls_ref_count++;

	global = os_zalloc(sizeof(*global));
	if (global == NULL)
		return NULL;

	return global;
}

void tls_deinit(void *ssl_ctx)
{
	struct tls_global *global = ssl_ctx;
	tls_ref_count--;
	if (tls_ref_count == 0) {
		tlsv1_client_global_deinit();
	}
	os_free(global);
}


int tls_get_errors(void *tls_ctx)
{
	return 0;
}


struct tls_connection * tls_connection_init(void *tls_ctx)
{
	struct tls_connection *conn;

	conn = os_zalloc(sizeof(*conn));
	if (conn == NULL)
		return NULL;

	conn->client = tlsv1_client_init();
	if (conn->client == NULL) {
		os_free(conn);
		return NULL;
	}

	return conn;
}


void tls_connection_deinit(void *tls_ctx, struct tls_connection *conn)
{
	if (conn == NULL)
		return;
	tlsv1_client_deinit(conn->client);
	os_free(conn);
}


int tls_connection_established(void *tls_ctx, struct tls_connection *conn)
{
	return tlsv1_client_established(conn->client);
}


int tls_connection_shutdown(void *tls_ctx, struct tls_connection *conn)
{
	return tlsv1_client_shutdown(conn->client);
}


int tls_connection_set_params(void *tls_ctx, struct tls_connection *conn,
			      const struct tls_connection_params *params)
{
	if (tlsv1_client_set_ca_cert(conn->client, params->ca_cert,
				     params->ca_cert_blob,
				     params->ca_cert_blob_len,
				     params->ca_path)) {
		wpa_printf(MSG_INFO, "TLS: Failed to configure trusted CA "
			   "certificates");
		return -1;
	}

	if (tlsv1_client_set_client_cert(conn->client, params->client_cert,
					 params->client_cert_blob,
					 params->client_cert_blob_len)) {
		wpa_printf(MSG_INFO, "TLS: Failed to configure client "
			   "certificate");
		return -1;
	}

	if (tlsv1_client_set_private_key(conn->client,
					 params->private_key,
					 params->private_key_passwd,
					 params->private_key_blob,
					 params->private_key_blob_len)) {
		wpa_printf(MSG_INFO, "TLS: Failed to load private key");
		return -1;
	}

	return 0;
}


int tls_global_set_params(void *tls_ctx,
			  const struct tls_connection_params *params)
{
	wpa_printf(MSG_INFO, "TLS: not implemented - %s", __func__);
	return -1;
}


int tls_global_set_verify(void *tls_ctx, int check_crl)
{
	wpa_printf(MSG_INFO, "TLS: not implemented - %s", __func__);
	return -1;
}


int tls_connection_set_verify(void *tls_ctx, struct tls_connection *conn,
			      int verify_peer)
{
	return -1;
}


int tls_connection_set_ia(void *tls_ctx, struct tls_connection *conn,
			  int tls_ia)
{
	return -1;
}


int tls_connection_get_keys(void *tls_ctx, struct tls_connection *conn,
			    struct tls_keys *keys)
{
	return tlsv1_client_get_keys(conn->client, keys);
}


int tls_connection_prf(void *tls_ctx, struct tls_connection *conn,
		       const char *label, int server_random_first,
		       u8 *out, size_t out_len)
{
	return tlsv1_client_prf(conn->client, label, server_random_first,
				out, out_len);
}


u8 * tls_connection_handshake(void *tls_ctx, struct tls_connection *conn,
			      const u8 *in_data, size_t in_len,
			      size_t *out_len, u8 **appl_data,
			      size_t *appl_data_len)
{
	if (appl_data)
		*appl_data = NULL;

	wpa_printf(MSG_DEBUG, "TLS: %s(in_data=%p in_len=%lu)",
		   __func__, in_data, (unsigned long) in_len);
	return tlsv1_client_handshake(conn->client, in_data, in_len, out_len);
}


u8 * tls_connection_server_handshake(void *tls_ctx,
				     struct tls_connection *conn,
				     const u8 *in_data, size_t in_len,
				     size_t *out_len)
{
	wpa_printf(MSG_INFO, "TLS: not implemented - %s", __func__);
	return NULL;
}


int tls_connection_encrypt(void *tls_ctx, struct tls_connection *conn,
			   const u8 *in_data, size_t in_len,
			   u8 *out_data, size_t out_len)
{
	return tlsv1_client_encrypt(conn->client, in_data, in_len, out_data,
				    out_len);
}


int tls_connection_decrypt(void *tls_ctx, struct tls_connection *conn,
			   const u8 *in_data, size_t in_len,
			   u8 *out_data, size_t out_len)
{
	return tlsv1_client_decrypt(conn->client, in_data, in_len, out_data,
				    out_len);
}


int tls_connection_resumed(void *tls_ctx, struct tls_connection *conn)
{
	return tlsv1_client_resumed(conn->client);
}


int tls_connection_set_master_key(void *tls_ctx, struct tls_connection *conn,
				  const u8 *key, size_t key_len)
{
	return tlsv1_client_set_master_key(conn->client, key, key_len);
}


int tls_connection_set_cipher_list(void *tls_ctx, struct tls_connection *conn,
				   u8 *ciphers)
{
	return tlsv1_client_set_cipher_list(conn->client, ciphers);
}


int tls_get_cipher(void *tls_ctx, struct tls_connection *conn,
		   char *buf, size_t buflen)
{
	if (conn == NULL)
		return -1;
	return tlsv1_client_get_cipher(conn->client, buf, buflen);
}


int tls_connection_enable_workaround(void *tls_ctx,
				     struct tls_connection *conn)
{
	return -1;
}


int tls_connection_client_hello_ext(void *tls_ctx, struct tls_connection *conn,
				    int ext_type, const u8 *data,
				    size_t data_len)
{
	return tlsv1_client_hello_ext(conn->client, ext_type, data, data_len);
}


int tls_connection_get_failed(void *tls_ctx, struct tls_connection *conn)
{
	return 0;
}


int tls_connection_get_read_alerts(void *tls_ctx, struct tls_connection *conn)
{
	return 0;
}


int tls_connection_get_write_alerts(void *tls_ctx,
				    struct tls_connection *conn)
{
	return 0;
}


int tls_connection_get_keyblock_size(void *tls_ctx,
				     struct tls_connection *conn)
{
	return tlsv1_client_get_keyblock_size(conn->client);
}


unsigned int tls_capabilities(void *tls_ctx)
{
	return 0;
}


int tls_connection_ia_send_phase_finished(void *tls_ctx,
					  struct tls_connection *conn,
					  int final,
					  u8 *out_data, size_t out_len)
{
	return -1;
}


int tls_connection_ia_final_phase_finished(void *tls_ctx,
					   struct tls_connection *conn)
{
	return -1;
}


int tls_connection_ia_permute_inner_secret(void *tls_ctx,
					   struct tls_connection *conn,
					   const u8 *key, size_t key_len)
{
	return -1;
}
